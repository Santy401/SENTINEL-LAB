"""Store-and-forward delivery of alerts to an n8n webhook with HMAC signing."""

from __future__ import annotations

import hashlib
import hmac
import json
import stat
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any, Callable

from .history import History, now_ms

USER_AGENT_PREFIX = "SentinelLabGateway/"
PERMANENT_STATUSES = {400, 401, 403, 404, 410, 413, 422}

BASE_BACKOFF_MS = 5_000
MAX_BACKOFF_MS = 600_000


def load_secret(path: Path) -> str:
    """Reads an owner-only (chmod 600) secret file."""
    info = path.stat()
    if info.st_mode & (stat.S_IRWXG | stat.S_IRWXO):
        raise PermissionError(f"{path} must be chmod 600 (owner only)")
    secret = path.read_text(encoding="utf-8").strip()
    if len(secret) < 16:
        raise ValueError(f"{path}: secret too short (min 16 characters)")
    return secret


def sign(secret: str, timestamp: str, body: bytes) -> str:
    message = timestamp.encode("ascii") + b"." + body
    return hmac.new(secret.encode("utf-8"), message, hashlib.sha256).hexdigest()


def backoff_ms(attempts: int) -> int:
    return min(MAX_BACKOFF_MS, BASE_BACKOFF_MS * (2 ** max(0, attempts)))


def public_payload(alert: dict[str, Any]) -> dict[str, Any]:
    keys = ("id", "rule", "kind", "severity", "title", "message", "occurredAt", "device", "metrics")
    return {k: alert.get(k) for k in keys}


class AlertDelivery(threading.Thread):
    def __init__(self, history: History, url: str, secret: str,
                 clock: Callable[[], int] = now_ms, timeout: float = 8.0,
                 poll_s: float = 2.0, version: str = "0.0.0") -> None:
        super().__init__(daemon=True, name="alert-delivery")
        if not url.startswith(("https://", "http://127.0.0.1", "http://localhost")):
            raise ValueError("alert webhook must use https (http only for localhost tests)")
        self.history = history
        self.url = url
        # Cloudflare in front of n8n rejects the default Python-urllib agent.
        self.user_agent = USER_AGENT_PREFIX + version
        self.rejected = 0
        self._secret = secret
        self.clock = clock
        self.timeout = timeout
        self.poll_s = poll_s
        self.stop_event = threading.Event()
        self.delivered = 0
        self.failures = 0
        self.last_error: str | None = None

    def stop(self) -> None:
        self.stop_event.set()

    def run(self) -> None:
        while not self.stop_event.is_set():
            self.deliver_once()
            self.stop_event.wait(self.poll_s)

    def deliver_once(self) -> int:
        sent = 0
        for item in self.history.pending_alerts(self.clock()):
            body = json.dumps(public_payload(item["payload"]), ensure_ascii=False,
                              separators=(",", ":")).encode("utf-8")
            timestamp = str(int(time.time()))
            request = urllib.request.Request(
                self.url, data=body, method="POST",
                headers={"Content-Type": "application/json",
                         "User-Agent": self.user_agent,
                         "X-Sentinel-Timestamp": timestamp,
                         "X-Sentinel-Signature": sign(self._secret, timestamp, body)})
            try:
                with urllib.request.urlopen(request, timeout=self.timeout) as response:
                    status = response.status
                if not 200 <= status < 300:
                    raise urllib.error.HTTPError(self.url, status, "status", None, None)
                self.history.mark_delivered(item["id"], self.clock())
                self.delivered += 1
                sent += 1
            except urllib.error.HTTPError as exc:
                if exc.code in PERMANENT_STATUSES:
                    # Signature, payload or URL rejected: retrying cannot help.
                    self.rejected += 1
                    self.last_error = f"HTTP {exc.code} (rechazo permanente)"
                    self.history.mark_rejected(item["id"], self.last_error)
                    continue
                self._retry_later(item, f"HTTP {exc.code}")
                break
            except (urllib.error.URLError, OSError, TimeoutError) as exc:
                # Offline: keep it queued and retry with backoff.
                self._retry_later(item, type(exc).__name__)
                break  # stop this round; the uplink is probably down
        return sent

    def _retry_later(self, item: dict[str, Any], error: str) -> None:
        self.failures += 1
        self.last_error = error
        self.history.mark_failed(item["id"], self.clock() + backoff_ms(item["attempts"]), error)

    def snapshot(self) -> dict[str, Any]:
        return {"url": self.url.split("?")[0], "delivered": self.delivered,
                "failures": self.failures, "rejected": self.rejected,
                "lastError": self.last_error}
