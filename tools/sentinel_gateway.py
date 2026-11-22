#!/usr/bin/env python3
"""SENTINEL-LAB local gateway (stdlib only).

Responsibilities that the ESP32 cannot solve on its own:

1. Time authority: holds the bearer token, opens an authenticated Protocol v1
   session as role ``gateway`` and lets the firmware synchronize its UTC clock
   from the hello timestamp. Heartbeats every 15 s re-sync the clock.
2. Web host: serves the static web dashboard (``apps/web/dist``) and proxies
   read-only ``GET /api/*`` requests to the device, because the firmware HTTP
   API sends no CORS headers. Browsers connect to the WebSocket directly; they
   cannot send ``Authorization`` on a WebSocket, so the web stays read-only.
3. Optional evidence: appends received telemetry to a JSONL file.

Security limits: binds to 127.0.0.1 by default, never proxies writes, never
prints or serves the token. Laboratory network only; no TLS.
"""

from __future__ import annotations

import argparse
import json
import mimetypes
import os
import random
import stat
import sys
import threading
import time
import urllib.error
import urllib.request
import uuid
from datetime import datetime, timezone
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Callable

sys.path.insert(0, str(Path(__file__).resolve().parent))

import hw_common as hw  # noqa: E402
from gateway.alerts import AlertEngine  # noqa: E402
from gateway.delivery import AlertDelivery, load_secret  # noqa: E402
from gateway.history import DEFAULT_DB, History  # noqa: E402
from gateway.report import build_report, render_pdf  # noqa: E402
from gateway.runtime import GatewayRuntime, StatusPoller  # noqa: E402

GATEWAY_VERSION = "0.2.0"
SOURCE = "sentinel-gateway"
HEARTBEAT_INTERVAL_S = 15.0
MAX_BACKOFF_S = 30.0
MAX_PROXY_BYTES = 64 * 1024
ROOT = Path(__file__).resolve().parents[1]
DEFAULT_WEB_ROOT = ROOT / "apps" / "web" / "dist"


def utc_timestamp(now: datetime | None = None) -> str:
    return hw.format_utc_ms(now or datetime.now(timezone.utc))


def backoff_delay(attempt: int, rng: Callable[[], float] = random.random) -> float:
    """Exponential backoff with full jitter, capped at MAX_BACKOFF_S."""
    ceiling = min(MAX_BACKOFF_S, 0.5 * (2 ** max(0, attempt)))
    return max(0.25, ceiling * rng())


class EnvelopeBuilder:
    """Builds Protocol v1 envelopes for one gateway session."""

    def __init__(self, device_id: str, session_id: str | None = None) -> None:
        self.device_id = device_id
        self.session_id = session_id or f"gw-{uuid.uuid4().hex[:24]}"
        self.sequence = 0
        self.started = time.monotonic()

    def _envelope(self, message_type: str, payload: dict[str, Any]) -> dict[str, Any]:
        self.sequence += 1
        return {
            "protocolVersion": hw.PROTOCOL_VERSION,
            "type": message_type,
            "timestamp": utc_timestamp(),
            "sequence": self.sequence,
            "source": SOURCE,
            "sessionId": self.session_id,
            "payload": payload,
        }

    def hello(self) -> dict[str, Any]:
        return self._envelope("connection.hello", {
            "deviceId": self.device_id,
            "role": "gateway",
            "supportedVersion": hw.PROTOCOL_VERSION,
            "capabilities": ["clock.sync", "web.proxy"],
        })

    def heartbeat(self) -> dict[str, Any]:
        return self._envelope("connection.heartbeat", {
            "bootId": self.device_id,
            "uptimeMs": int((time.monotonic() - self.started) * 1000),
        })

    def goodbye(self, reason: str = "client-request") -> dict[str, Any]:
        return self._envelope("connection.goodbye", {"reason": reason})


def load_token(token_file: Path | None, env_name: str) -> str | None:
    """Token from an owner-only file (preferred) or an environment variable."""
    if token_file is not None:
        info = token_file.stat()
        if info.st_mode & (stat.S_IRWXG | stat.S_IRWXO):
            raise PermissionError(f"{token_file} must be chmod 600 (owner only)")
        token = token_file.read_text(encoding="utf-8").strip()
        return token or None
    token = os.environ.get(env_name, "").strip()
    return token or None


class GatewayState:
    """Thread-safe status snapshot served at /gateway/status."""

    def __init__(self, clock_authority: bool) -> None:
        self._lock = threading.Lock()
        self.started = time.monotonic()
        self.data: dict[str, Any] = {
            "state": "disconnected",
            "clockAuthority": clock_authority,
            "lastSyncAt": None,
            "lastError": None,
            "reconnects": 0,
            "framesReceived": 0,
            "framesByType": {},
            "deviceSessionId": None,
        }

    def update(self, **changes: Any) -> None:
        with self._lock:
            self.data.update(changes)

    def count_frame(self, message_type: str) -> None:
        with self._lock:
            self.data["framesReceived"] += 1
            by_type = self.data["framesByType"]
            by_type[message_type] = by_type.get(message_type, 0) + 1

    def snapshot(self) -> dict[str, Any]:
        with self._lock:
            result = json.loads(json.dumps(self.data))
        result["uptimeS"] = round(time.monotonic() - self.started, 1)
        result["gatewayVersion"] = GATEWAY_VERSION
        return result


class DeviceLink(threading.Thread):
    """Keeps one authenticated Protocol v1 session open to the firmware."""

    def __init__(self, host: str, port: int, token: str | None, state: GatewayState,
                 log_path: Path | None = None,
                 socket_factory: Callable[..., Any] = hw.MinimalWebSocket,
                 observer: Callable[[dict[str, Any]], None] | None = None) -> None:
        super().__init__(daemon=True, name="device-link")
        self.host = host
        self.port = port
        self.token = token
        self.state = state
        self.log_path = log_path
        self.socket_factory = socket_factory
        self.observer = observer
        self.stop_event = threading.Event()
        self.device_id = f"gateway-{uuid.uuid4().hex[:8]}"

    def stop(self) -> None:
        self.stop_event.set()

    def run(self) -> None:
        attempt = 0
        while not self.stop_event.is_set():
            self.state.update(state="connecting")
            try:
                self._session()
                attempt = 0
            except (OSError, hw.WebSocketError, TimeoutError, ValueError) as exc:
                self.state.update(lastError=f"{type(exc).__name__}: {exc}"[:200])
            self.state.update(state="disconnected")
            if self.stop_event.is_set():
                break
            with self.state._lock:
                self.state.data["reconnects"] += 1
            self.stop_event.wait(backoff_delay(attempt))
            attempt += 1

    def _session(self) -> None:
        client = self.socket_factory(self.host, self.port, hw.WS_PATH, timeout=5.0)
        builder = EnvelopeBuilder(self.device_id)
        client.connect(self.token)
        try:
            self.state.update(state="connected", lastError=None)
            client.send_text(json.dumps(builder.hello(), separators=(",", ":")))
            if self.token:
                self.state.update(lastSyncAt=utc_timestamp())
            next_heartbeat = time.monotonic() + HEARTBEAT_INTERVAL_S
            client.settimeout(1.0)
            while not self.stop_event.is_set():
                if time.monotonic() >= next_heartbeat:
                    client.send_text(json.dumps(builder.heartbeat(), separators=(",", ":")))
                    if self.token:
                        self.state.update(lastSyncAt=utc_timestamp())
                    next_heartbeat = time.monotonic() + HEARTBEAT_INTERVAL_S
                try:
                    frame = client.recv_frame()
                except TimeoutError:
                    continue
                if frame["type"] == "close":
                    raise hw.WebSocketError("device closed the session")
                if frame["type"] != "text":
                    continue
                self._handle_text(frame["payload"])
            client.send_text(json.dumps(builder.goodbye(), separators=(",", ":")))
        finally:
            client.close()

    def _handle_text(self, payload: bytes) -> None:
        try:
            message = hw.parse_json(payload.decode("utf-8"))
        except (UnicodeDecodeError, ValueError):
            self.state.count_frame("invalid")
            return
        if not isinstance(message, dict):
            self.state.count_frame("invalid")
            return
        message_type = str(message.get("type", "unknown"))[:32]
        self.state.count_frame(message_type)
        if message_type == "connection.hello":
            self.state.update(deviceSessionId=message.get("sessionId"))
        if self.observer is not None:
            try:
                self.observer(message)
            except Exception as exc:  # noqa: BLE001 - never break the link
                self.state.update(lastError=f"observer: {type(exc).__name__}")
        if message_type == "error":
            error = message.get("payload", {})
            self.state.update(lastError=f"device error {error.get('code')}: {error.get('message')}"[:200])
        if self.log_path is not None:
            record = {"receivedAt": utc_timestamp(), "message": message}
            clean, _ = hw.redact(record)
            with self.log_path.open("a", encoding="utf-8") as handle:
                handle.write(json.dumps(clean, separators=(",", ":")) + "\n")


def make_handler(device_host: str, ws_port: int, web_root: Path,
                 state: GatewayState, runtime: GatewayRuntime | None = None,
                 delivery: AlertDelivery | None = None) -> type[BaseHTTPRequestHandler]:
    web_root = web_root.resolve()

    class Handler(BaseHTTPRequestHandler):
        server_version = f"SentinelGateway/{GATEWAY_VERSION}"

        def log_message(self, fmt: str, *args: Any) -> None:  # quieter logs
            sys.stderr.write("gateway: " + (fmt % args) + "\n")

        def _send(self, code: int, body: bytes, content_type: str) -> None:
            self.send_response(code)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.end_headers()
            if self.command != "HEAD":
                self.wfile.write(body)

        def _json(self, code: int, value: Any) -> None:
            self._send(code, json.dumps(value).encode("utf-8"), "application/json")

        def do_GET(self) -> None:  # noqa: N802
            path = self.path.split("?", 1)[0]
            if path == "/gateway/config":
                self._json(200, {
                    "deviceHttp": f"http://{device_host}",
                    "wsUrl": f"ws://{device_host}:{ws_port}{hw.WS_PATH}",
                    "gatewayVersion": GATEWAY_VERSION,
                })
            elif path == "/gateway/status":
                snapshot = state.snapshot()
                if delivery is not None:
                    snapshot["alertDelivery"] = delivery.snapshot()
                self._json(200, snapshot)
            elif path == "/gateway/alerts":
                self._json(200, {"alerts": runtime.recent_alerts() if runtime else []})
            elif path == "/gateway/history/summary":
                if runtime is None or runtime.history is None:
                    self._json(200, {"enabled": False})
                else:
                    self._json(200, {"enabled": True, **runtime.history.summary(),
                                     "suppressedAlerts": runtime.engine.suppressed})
            elif path.startswith("/api/"):
                self._proxy()
            else:
                self._static(path)

        do_HEAD = do_GET

        def _reject_write(self) -> None:
            self._json(HTTPStatus.METHOD_NOT_ALLOWED, {"error": "gateway proxy is read-only"})

        do_POST = do_PUT = do_DELETE = do_PATCH = _reject_write

        def _proxy(self) -> None:
            url = f"http://{device_host}{self.path}"
            try:
                with urllib.request.urlopen(url, timeout=4.0) as response:
                    body = response.read(MAX_PROXY_BYTES + 1)
                    if len(body) > MAX_PROXY_BYTES:
                        self._json(HTTPStatus.BAD_GATEWAY, {"error": "device response too large"})
                        return
                    self._send(response.status, body,
                               response.headers.get("Content-Type", "application/json"))
            except urllib.error.HTTPError as exc:
                self._send(exc.code, exc.read(MAX_PROXY_BYTES), "application/json")
            except (urllib.error.URLError, OSError, TimeoutError) as exc:
                self._json(HTTPStatus.BAD_GATEWAY, {"error": f"device unreachable: {type(exc).__name__}"})

        def _static(self, path: str) -> None:
            relative = path.lstrip("/") or "index.html"
            target = (web_root / relative).resolve()
            if web_root not in target.parents and target != web_root:
                self._json(HTTPStatus.FORBIDDEN, {"error": "forbidden"})
                return
            if target.is_dir():
                target = target / "index.html"
            if not target.is_file():
                # Single-page app fallback for client-side routes.
                target = web_root / "index.html"
                if not target.is_file():
                    self._json(HTTPStatus.NOT_FOUND, {
                        "error": "web build missing; run npm run build in apps/web"})
                    return
            content_type = mimetypes.guess_type(target.name)[0] or "application/octet-stream"
            self._send(200, target.read_bytes(), content_type)

    return Handler


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--device", default="192.168.4.1", help="ESP32 host on the lab AP")
    parser.add_argument("--ws-port", type=int, default=hw.WS_PORT)
    parser.add_argument("--bind", default="127.0.0.1",
                        help="HTTP bind address (default loopback only)")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--web-root", type=Path, default=DEFAULT_WEB_ROOT)
    parser.add_argument("--token-file", type=Path,
                        help="owner-only file with the bearer token (preferred)")
    parser.add_argument("--token-env", default="SENTINEL_GATEWAY_TOKEN")
    parser.add_argument("--log-jsonl", type=Path, help="append received frames here")
    parser.add_argument("--no-link", action="store_true", help="serve web/proxy only")
    parser.add_argument("--db", type=Path, default=DEFAULT_DB, help="SQLite history file")
    parser.add_argument("--no-history", action="store_true", help="do not record history")
    parser.add_argument("--status-interval", type=float, default=10.0,
                        help="seconds between /api/realtime/status polls")
    parser.add_argument("--no-poll", action="store_true", help="disable HTTP status polling")
    parser.add_argument("--alert-webhook-url",
                        help="n8n webhook URL (https), e.g. https://n8n.example/webhook/sentinel-alerts")
    parser.add_argument("--alert-secret-file", type=Path,
                        help="owner-only file with the HMAC secret shared with n8n")
    parser.add_argument("--alert-dry-run", action="store_true",
                        help="print alerts instead of sending them")
    return parser.parse_args(argv)


def parse_report_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="sentinel_gateway.py report",
                                     description="Genera un informe HTML (y PDF) de una sesión.")
    parser.add_argument("--db", type=Path, default=DEFAULT_DB)
    parser.add_argument("--session", default="latest", help="id de sesión o 'latest'")
    parser.add_argument("--out", type=Path, default=Path("sentinel-report.html"))
    parser.add_argument("--pdf", action="store_true", help="también genera PDF (Chromium headless)")
    parser.add_argument("--list", action="store_true", help="lista las sesiones guardadas")
    return parser.parse_args(argv)


def report_main(argv: list[str]) -> int:
    args = parse_report_args(argv)
    if not args.db.exists():
        print(f"FAIL: no existe {args.db}", file=sys.stderr)
        return 2
    history = History(args.db)
    try:
        if args.list:
            for s in history.sessions():
                print(f"{s['id']:>4}  {s['started_at']}  ->  {s['ended_at'] or 'en curso'}")
            return 0
        try:
            html_text = build_report(history, args.session)
        except ValueError as exc:
            print(f"FAIL: {exc}", file=sys.stderr)
            return 2
        args.out.write_text(html_text, encoding="utf-8")
        print(f"OK   informe HTML: {args.out}")
        if args.pdf:
            pdf = args.out.with_suffix(".pdf")
            if render_pdf(args.out, pdf):
                print(f"OK   informe PDF: {pdf}")
            else:
                print("WARN: no se pudo generar el PDF (¿falta chromium?)", file=sys.stderr)
        return 0
    finally:
        history.close()


DEFAULT_ALERT_URL = "https://n8n.nexuscloud.sbs/webhook/sentinel-alerts"
DEFAULT_ALERT_SECRET = Path.home() / ".config" / "sentinel-lab" / "alert-secret"


def alert_test_main(argv: list[str]) -> int:
    """Queue one clearly labeled test alert and deliver it to n8n now."""
    parser = argparse.ArgumentParser(prog="sentinel_gateway.py alert-test",
                                     description="Send a signed test alert to n8n.")
    parser.add_argument("--alert-webhook-url", default=DEFAULT_ALERT_URL)
    parser.add_argument("--alert-secret-file", type=Path, default=DEFAULT_ALERT_SECRET)
    parser.add_argument("--db", type=Path, default=None,
                        help="history database (default: the gateway history)")
    args = parser.parse_args(argv)
    try:
        secret = load_secret(args.alert_secret_file)
    except (OSError, PermissionError, ValueError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 2
    history = History(args.db) if args.db else History()
    session_id = history.start_session(GATEWAY_VERSION, "alert-test")
    now = int(time.time() * 1000)
    alert = {
        "id": f"test-{uuid.uuid4().hex[:12]}", "rule": "test", "kind": "test",
        "severity": "info", "title": "Prueba de alertas",
        "message": "Mensaje de prueba del gateway SENTINEL-LAB. No es una medición.",
        "occurredAt": utc_timestamp(), "occurredMs": now,
        "device": {"bootId": None, "firmwareVersion": None}, "metrics": {},
    }
    history.add_alert(session_id, alert)
    delivery = AlertDelivery(history, args.alert_webhook_url, secret, version=GATEWAY_VERSION)
    sent = delivery.deliver_once()
    history.end_session(session_id)
    if sent:
        print(f"OK: alerta de prueba {alert['id']} aceptada por n8n")
        return 0
    print(f"FAIL: no entregada ({delivery.last_error}); queda en cola si es reintentable",
          file=sys.stderr)
    return 1


def main(argv: list[str] | None = None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    if argv[:1] == ["report"]:
        return report_main(argv[1:])
    if argv[:1] == ["alert-test"]:
        return alert_test_main(argv[1:])
    args = parse_args(argv)
    if not hw.valid_host(args.device):
        print("FAIL: invalid --device host", file=sys.stderr)
        return 2
    try:
        token = load_token(args.token_file, args.token_env)
    except (OSError, PermissionError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 2
    secret = None
    if args.alert_webhook_url and not args.alert_dry_run:
        if not args.alert_secret_file:
            print("FAIL: --alert-webhook-url requires --alert-secret-file", file=sys.stderr)
            return 2
        try:
            secret = load_secret(args.alert_secret_file)
        except (OSError, PermissionError, ValueError) as exc:
            print(f"FAIL: {exc}", file=sys.stderr)
            return 2

    state = GatewayState(clock_authority=bool(token))
    if not token:
        print("WARN: no bearer token; the gateway cannot synchronize the device clock",
              file=sys.stderr)
    history = None if args.no_history else History(args.db)
    session_id = history.start_session(GATEWAY_VERSION, args.device) if history else None
    runtime = GatewayRuntime(history, AlertEngine(), session_id, dry_run=args.alert_dry_run)
    delivery = None
    if secret and history is not None:
        try:
            delivery = AlertDelivery(history, args.alert_webhook_url, secret,
                                     version=GATEWAY_VERSION)
        except ValueError as exc:
            print(f"FAIL: {exc}", file=sys.stderr)
            return 2
        delivery.start()
    elif args.alert_webhook_url and history is None and not args.alert_dry_run:
        print("WARN: alert delivery needs history (store-and-forward); alerts disabled",
              file=sys.stderr)

    link = None
    if not args.no_link:
        link = DeviceLink(args.device, args.ws_port, token, state, args.log_jsonl,
                          observer=runtime.on_message)
        link.start()
    poller = None
    if not args.no_poll:
        poller = StatusPoller(args.device, runtime, interval_s=args.status_interval)
        poller.start()
    handler = make_handler(args.device, args.ws_port, args.web_root, state, runtime, delivery)
    server = ThreadingHTTPServer((args.bind, args.port), handler)
    print(f"SENTINEL gateway {GATEWAY_VERSION}: http://{args.bind}:{args.port} -> "
          f"device {args.device} (clock authority: {'yes' if token else 'no'}, "
          f"history: {'on' if history else 'off'}, alerts: "
          f"{'dry-run' if args.alert_dry_run else ('n8n' if delivery else 'local')})")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        for worker in (link, poller, delivery):
            if worker:
                worker.stop()
        for worker in (link, poller, delivery):
            if worker:
                worker.join(timeout=3)
        server.server_close()
        if history is not None:
            history.end_session(session_id)
            history.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
