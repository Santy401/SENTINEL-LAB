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

GATEWAY_VERSION = "0.1.0"
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
                 socket_factory: Callable[..., Any] = hw.MinimalWebSocket) -> None:
        super().__init__(daemon=True, name="device-link")
        self.host = host
        self.port = port
        self.token = token
        self.state = state
        self.log_path = log_path
        self.socket_factory = socket_factory
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
        if message_type == "error":
            error = message.get("payload", {})
            self.state.update(lastError=f"device error {error.get('code')}: {error.get('message')}"[:200])
        if self.log_path is not None:
            record = {"receivedAt": utc_timestamp(), "message": message}
            clean, _ = hw.redact(record)
            with self.log_path.open("a", encoding="utf-8") as handle:
                handle.write(json.dumps(clean, separators=(",", ":")) + "\n")


def make_handler(device_host: str, ws_port: int, web_root: Path,
                 state: GatewayState) -> type[BaseHTTPRequestHandler]:
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
                self._json(200, state.snapshot())
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
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if not hw.valid_host(args.device):
        print("FAIL: invalid --device host", file=sys.stderr)
        return 2
    try:
        token = load_token(args.token_file, args.token_env)
    except (OSError, PermissionError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 2
    state = GatewayState(clock_authority=bool(token))
    if not token:
        print("WARN: no bearer token; the gateway cannot synchronize the device clock",
              file=sys.stderr)
    link = None
    if not args.no_link:
        link = DeviceLink(args.device, args.ws_port, token, state, args.log_jsonl)
        link.start()
    handler = make_handler(args.device, args.ws_port, args.web_root, state)
    server = ThreadingHTTPServer((args.bind, args.port), handler)
    print(f"SENTINEL gateway {GATEWAY_VERSION}: http://{args.bind}:{args.port} -> "
          f"device {args.device} (clock authority: {'yes' if token else 'no'})")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        if link:
            link.stop()
            link.join(timeout=3)
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
