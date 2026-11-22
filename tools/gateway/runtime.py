"""Glue between the device link, the status poller, history and alerts."""

from __future__ import annotations

import json
import threading
import urllib.request
from typing import Any, Callable

from .alerts import AlertEngine
from .history import History, MotionAggregator, now_ms

STATUS_INTERVAL_S = 10.0
LAB_INTERVAL_S = 60.0
PRUNE_INTERVAL_S = 3600.0


class GatewayRuntime:
    def __init__(self, history: History | None, engine: AlertEngine, session_id: int | None,
                 dry_run: bool = False, emit: Callable[[str], None] = print,
                 clock: Callable[[], int] = now_ms) -> None:
        self.history = history
        self.engine = engine
        self.session_id = session_id
        self.dry_run = dry_run
        self.emit = emit
        self.clock = clock
        self.motion = MotionAggregator()
        self.lock = threading.Lock()
        self.recent: list[dict[str, Any]] = []   # in-memory when history is off

    # ------------------------------------------------------------ alerts
    def _publish(self, alerts: list[dict[str, Any]]) -> None:
        for alert in alerts:
            line = f"[alerta {alert['severity']}] {alert['title']}: {alert['message']}"
            if self.dry_run:
                self.emit(line)
            with self.lock:
                self.recent = ([{k: alert[k] for k in ("id", "occurredAt", "rule", "severity",
                                                        "kind", "title", "message")}]
                               + self.recent)[:50]
            if self.history is not None and self.session_id is not None:
                self.history.add_alert(self.session_id, alert,
                                       delivered="dry-run" if self.dry_run else None)

    def recent_alerts(self) -> list[dict[str, Any]]:
        if self.history is not None:
            return self.history.recent_alerts(50)
        with self.lock:
            return list(self.recent)

    # ------------------------------------------------------------ inputs
    def on_message(self, message: dict[str, Any]) -> None:
        """Called by DeviceLink for every valid server envelope."""
        at = self.clock()
        kind = message.get("type")
        payload = message.get("payload") or {}
        with self.lock:
            if kind == "motion.sample":
                lin = payload.get("linearAcceleration") or {}
                mag = sum(float(lin.get(k, 0.0)) ** 2 for k in "xyz") ** 0.5
                done = self.motion.add(mag, str(payload.get("state")), at)
                if done and self.history is not None and self.session_id is not None:
                    self.history.add_motion_summary(self.session_id, done)
                alerts = self.engine.on_motion(payload, at)
            elif kind == "rf.telemetry":
                clients = payload.get("clients") or []
                if self.history is not None and self.session_id is not None:
                    for c in clients:
                        if isinstance(c.get("clientId"), int):
                            self.history.add_rf(self.session_id, c["clientId"], c.get("rssiDbm"),
                                                c.get("filteredRssiDbm"), bool(c.get("connected")), at)
                alerts = self.engine.on_rf(clients, at)
            elif kind == "event":
                details = payload.get("details") or {}
                if self.history is not None and self.session_id is not None:
                    self.history.add_event(self.session_id, str(payload.get("name")),
                                           details.get("clientId"), details.get("rssiDbm"), at)
                alerts = []
            else:
                alerts = []
        self._publish(alerts)

    def on_status(self, status: dict[str, Any] | None) -> None:
        at = self.clock()
        with self.lock:
            if status is not None and self.history is not None and self.session_id is not None:
                self.history.add_status(self.session_id, status, at)
            alerts = self.engine.on_status(status, at)
        self._publish(alerts)

    def flush_motion(self) -> None:
        with self.lock:
            done = self.motion.flush() if self.motion.second is not None and \
                self.clock() // 1000 > self.motion.second else None
            if done and self.history is not None and self.session_id is not None:
                self.history.add_motion_summary(self.session_id, done)

    def on_lab(self, data: dict[str, Any]) -> None:
        if self.history is not None and self.session_id is not None:
            self.history.add_lab_snapshot(self.session_id, data, self.clock())


class StatusPoller(threading.Thread):
    """Polls /api/realtime/status (10 s) and /api/lab (60 s) over HTTP."""

    def __init__(self, device_host: str, runtime: GatewayRuntime,
                 interval_s: float = STATUS_INTERVAL_S, timeout: float = 4.0) -> None:
        super().__init__(daemon=True, name="status-poller")
        self.device_host = device_host
        self.runtime = runtime
        self.interval_s = interval_s
        self.timeout = timeout
        self.stop_event = threading.Event()

    def stop(self) -> None:
        self.stop_event.set()

    def _get(self, path: str) -> dict[str, Any]:
        with urllib.request.urlopen(f"http://{self.device_host}{path}", timeout=self.timeout) as r:
            return json.loads(r.read(64 * 1024))

    def run(self) -> None:
        last_lab = last_prune = 0.0
        elapsed = 0.0
        while not self.stop_event.is_set():
            try:
                self.runtime.on_status(self._get("/api/realtime/status"))
            except (OSError, ValueError):
                self.runtime.on_status(None)
            if elapsed - last_lab >= LAB_INTERVAL_S or last_lab == 0.0:
                last_lab = elapsed or 0.001
                try:
                    self.runtime.on_lab(self._get("/api/lab"))
                except (OSError, ValueError):
                    pass
            history = self.runtime.history
            if history is not None and elapsed - last_prune >= PRUNE_INTERVAL_S:
                last_prune = elapsed
                history.prune(self.runtime.clock())
            # Sleep in 1 s steps so motion summaries are flushed every second.
            for _ in range(max(1, int(self.interval_s))):
                if self.stop_event.wait(1.0):
                    return
                self.runtime.flush_motion()
                elapsed += 1.0
