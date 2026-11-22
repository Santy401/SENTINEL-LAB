"""Alert rules with sustain, hysteresis, cooldown and a global hourly cap.

Inputs are observations of the ESP32 (status polls, relayed motion samples,
rf.telemetry). Every method takes `now_ms` so behaviour is deterministic.
Messages are Spanish and written for a person, not an engineer.
"""

from __future__ import annotations

import uuid
from dataclasses import dataclass, field
from typing import Any, Callable

from .history import iso

SEVERITIES = ("info", "warning", "critical", "recovery")
SEVERE_RESETS = {"PANIC", "INT_WDT", "TASK_WDT", "WDT", "BROWNOUT"}


@dataclass
class Rule:
    name: str
    severity: str
    sustain_ms: int          # condition must hold this long before triggering
    clear_ms: int            # condition must be absent this long before recovery
    cooldown_ms: int         # minimum time between two triggers
    recovers: bool = True
    active: bool = False
    since_ms: int | None = None
    clear_since_ms: int | None = None
    last_trigger_ms: int | None = None


@dataclass
class AlertEngine:
    max_per_hour: int = 20
    out_of_range_dbm: float = -85.0
    in_range_dbm: float = -80.0
    heap_low_bytes: int = 40_000
    block_low_bytes: int = 20_000
    heap_ok_bytes: int = 50_000
    block_ok_bytes: int = 30_000
    shake_intensity: float = 0.85
    shake_clear_intensity: float = 0.5
    unreachable_ms: int = 60_000
    id_factory: Callable[[], str] = field(default=lambda: uuid.uuid4().hex)

    def __post_init__(self) -> None:
        self.rules: dict[str, Rule] = {
            "strong_shake": Rule("strong_shake", "warning", 1_000, 5_000, 120_000),
            "esp_unreachable": Rule("esp_unreachable", "critical", 0, 0, 300_000),
            "esp_reboot": Rule("esp_reboot", "warning", 0, 0, 0, recovers=False),
            "heap_low": Rule("heap_low", "critical", 20_000, 20_000, 600_000),
            "clock_unsynced": Rule("clock_unsynced", "warning", 120_000, 0, 600_000),
        }
        self.clients: dict[int, Rule] = {}
        self.sent: list[int] = []         # trigger timestamps in the last hour
        self.suppressed = 0
        self.last_ok_ms: int | None = None
        self.boot_id: str | None = None
        self.device: dict[str, Any] = {}
        self.motion_alert_since: int | None = None
        # Two independent sources; the freshest strongest one wins so that
        # HTTP status polls and relayed samples never cancel each other.
        self.status_intensity = 0.0
        self.status_intensity_ms: int | None = None
        self.motion_intensity = 0.0
        self.motion_intensity_ms: int | None = None

    # ------------------------------------------------------------ core
    def _alert(self, rule: Rule, kind: str, title: str, message: str,
               metrics: dict[str, Any], now_ms: int, severity: str | None = None) -> dict[str, Any]:
        return {
            "id": self.id_factory(), "rule": rule.name, "kind": kind,
            "severity": "recovery" if kind == "recovery" else (severity or rule.severity),
            "title": title, "message": message, "occurredAt": iso(now_ms),
            "occurredMs": now_ms, "device": dict(self.device), "metrics": metrics,
        }

    def _allow(self, now_ms: int) -> bool:
        self.sent = [t for t in self.sent if now_ms - t < 3_600_000]
        if len(self.sent) >= self.max_per_hour:
            self.suppressed += 1
            return False
        self.sent.append(now_ms)
        return True

    def _evaluate(self, rule: Rule, condition: bool, now_ms: int,
                  on_trigger: Callable[[], tuple[str, str, dict[str, Any]]],
                  on_recover: Callable[[], tuple[str, str, dict[str, Any]]] | None = None,
                  ) -> list[dict[str, Any]]:
        out: list[dict[str, Any]] = []
        if condition:
            rule.clear_since_ms = None
            if rule.since_ms is None:
                rule.since_ms = now_ms
            if (not rule.active and now_ms - rule.since_ms >= rule.sustain_ms
                    and (rule.last_trigger_ms is None
                         or now_ms - rule.last_trigger_ms >= rule.cooldown_ms)):
                rule.active = rule.recovers
                rule.last_trigger_ms = now_ms
                if self._allow(now_ms):
                    title, message, metrics = on_trigger()
                    out.append(self._alert(rule, "trigger", title, message, metrics, now_ms))
        else:
            rule.since_ms = None
            if rule.active:
                if rule.clear_since_ms is None:
                    rule.clear_since_ms = now_ms
                if now_ms - rule.clear_since_ms >= rule.clear_ms:
                    rule.active = False
                    rule.clear_since_ms = None
                    if on_recover:
                        title, message, metrics = on_recover()
                        out.append(self._alert(rule, "recovery", title, message, metrics, now_ms))
        return out

    # ------------------------------------------------------------ inputs
    def on_status(self, status: dict[str, Any] | None, now_ms: int) -> list[dict[str, Any]]:
        """`status` is /api/realtime/status, or None when the poll failed."""
        out: list[dict[str, Any]] = []
        if status is None:
            down_since = self.last_ok_ms if self.last_ok_ms is not None else now_ms
            if self.last_ok_ms is None:
                self.last_ok_ms = now_ms
            gone = now_ms - down_since
            out += self._evaluate(
                self.rules["esp_unreachable"], gone >= self.unreachable_ms, now_ms,
                lambda: ("ESP32 sin respuesta",
                         f"El gateway no recibe datos del ESP32 desde hace {gone // 1000} s. "
                         "Revisa la alimentación y la Wi-Fi SENTINEL-LAB.",
                         {"secondsWithoutData": gone // 1000}))
            return out

        self.last_ok_ms = now_ms
        self.device = {"bootId": status.get("bootId"),
                       "firmwareVersion": status.get("firmwareVersion") or status.get("realtimeVersion")}
        out += self._evaluate(
            self.rules["esp_unreachable"], False, now_ms, lambda: ("", "", {}),
            lambda: ("ESP32 de nuevo en línea", "El gateway vuelve a recibir datos del ESP32.", {}))

        boot = status.get("bootId")
        if self.boot_id is not None and boot and boot != self.boot_id:
            reason = status.get("resetReason") or "UNKNOWN"
            rule = self.rules["esp_reboot"]
            if self._allow(now_ms):
                out.append(self._alert(
                    rule, "trigger", "El ESP32 se reinició",
                    f"El ESP32 arrancó de nuevo (motivo: {reason}). "
                    + ("Es un reinicio anómalo; conviene revisar el registro."
                       if reason in SEVERE_RESETS else "Puede ser un reinicio manual o un corte de energía."),
                    {"resetReason": reason, "previousBootId": self.boot_id, "bootId": boot}, now_ms,
                    severity="critical" if reason in SEVERE_RESETS else "warning"))
        if boot:
            self.boot_id = boot

        heap = status.get("freeHeapBytes")
        block = status.get("largestFreeBlockBytes")
        low = (isinstance(heap, int) and heap < self.heap_low_bytes) or \
              (isinstance(block, int) and block < self.block_low_bytes)
        healthy = (not isinstance(heap, int) or heap >= self.heap_ok_bytes) and \
                  (not isinstance(block, int) or block >= self.block_ok_bytes)
        out += self._evaluate(
            self.rules["heap_low"], low or (self.rules["heap_low"].active and not healthy), now_ms,
            lambda: ("Memoria baja en el ESP32",
                     f"Quedan {heap} B libres (bloque mayor {block} B). El sistema podría volverse inestable.",
                     {"freeHeapBytes": heap, "largestFreeBlockBytes": block}),
            lambda: ("Memoria del ESP32 recuperada", f"Memoria libre: {heap} B.",
                     {"freeHeapBytes": heap, "largestFreeBlockBytes": block}))

        clients = status.get("clientCount") or 0
        unsynced = not status.get("clockSynced") and clients > 0
        out += self._evaluate(
            self.rules["clock_unsynced"], unsynced, now_ms,
            lambda: ("Reloj del ESP32 sin sincronizar",
                     f"Hay {clients} cliente(s) conectados pero el ESP32 no tiene hora UTC desde hace más de 2 min; "
                     "no transmite datos en tiempo real. Abre la app con token o el gateway.",
                     {"clientCount": clients}),
            lambda: ("Reloj del ESP32 sincronizado", "El ESP32 vuelve a tener hora UTC.", {}))

        phone = status.get("phoneMotion") or {}
        if phone.get("active"):
            self.status_intensity = float(phone.get("intensity") or 0.0)
            self.status_intensity_ms = now_ms
        out += self._shake(now_ms)
        return out

    def on_motion(self, payload: dict[str, Any], now_ms: int) -> list[dict[str, Any]]:
        """`payload` is a relayed motion.sample payload."""
        lin = payload.get("linearAcceleration") or {}
        mag = sum(float(lin.get(k, 0.0)) ** 2 for k in "xyz") ** 0.5
        target = min(1.0, mag / 8.0)
        previous = self.motion_intensity if self.motion_intensity_ms is not None else 0.0
        # Same shape as the firmware smoothing: rise fast, fall slowly.
        self.motion_intensity = (0.6 * target + 0.4 * previous if target > previous
                                 else 0.35 * target + 0.65 * previous)
        self.motion_intensity_ms = now_ms
        if payload.get("state") == "ALERT":
            if self.motion_alert_since is None:
                self.motion_alert_since = now_ms
        else:
            self.motion_alert_since = None
        return self._shake(now_ms, mag)

    def _shake(self, now_ms: int, magnitude: float | None = None) -> list[dict[str, Any]]:
        # Relayed samples arrive at 5 Hz; status polls every ~10 s.
        sources = []
        if self.motion_intensity_ms is not None and now_ms - self.motion_intensity_ms < 2_000:
            sources.append(self.motion_intensity)
        if self.status_intensity_ms is not None and now_ms - self.status_intensity_ms < 15_000:
            sources.append(self.status_intensity)
        fresh = bool(sources)
        intensity = max(sources) if sources else 0.0
        rule = self.rules["strong_shake"]
        condition = fresh and (intensity >= self.shake_intensity or self.motion_alert_since is not None)
        if rule.active and fresh and intensity >= self.shake_clear_intensity:
            condition = True  # hysteresis: stay active until clearly calm
        mag = magnitude if magnitude is not None else intensity * 8.0
        return self._evaluate(
            rule, condition, now_ms,
            lambda: ("Agitación fuerte detectada",
                     f"El iPhone registra una agitación fuerte ({mag:.1f} m/s², intensidad "
                     f"{intensity * 100:.0f} %). Posible manipulación o caída.",
                     {"linearAccelMs2": round(mag, 2), "intensity": round(intensity, 2)}),
            lambda: ("Movimiento normalizado", "El iPhone vuelve a estar en calma.",
                     {"intensity": round(intensity, 2)}))

    def on_rf(self, clients: list[dict[str, Any]], now_ms: int) -> list[dict[str, Any]]:
        """`clients` from rf.telemetry: clientId, connected, rssiDbm/filteredRssiDbm."""
        out: list[dict[str, Any]] = []
        seen: set[int] = set()
        for client in clients:
            cid = client.get("clientId")
            if not isinstance(cid, int):
                continue
            seen.add(cid)
            rssi = client.get("filteredRssiDbm", client.get("rssiDbm"))
            connected = bool(client.get("connected", True))
            rule = self.clients.setdefault(
                cid, Rule(f"phone_out_of_range", "warning", 10_000, 5_000, 300_000))
            weak = (not connected) or (isinstance(rssi, (int, float)) and rssi < self.out_of_range_dbm)
            if rule.active and isinstance(rssi, (int, float)) and connected:
                weak = rssi <= self.in_range_dbm  # hysteresis band
            out += self._client_eval(rule, cid, weak, rssi, now_ms)
        for cid, rule in self.clients.items():
            if cid not in seen:
                out += self._client_eval(rule, cid, True, None, now_ms)
        return out

    def _client_eval(self, rule: Rule, cid: int, weak: bool, rssi: Any,
                     now_ms: int) -> list[dict[str, Any]]:
        where = f"{rssi:.0f} dBm" if isinstance(rssi, (int, float)) else "sin señal"
        alerts = self._evaluate(
            rule, weak, now_ms,
            lambda: ("Dispositivo fuera de alcance",
                     f"El dispositivo {cid} se alejó del ESP32 o dejó de verse ({where}) durante más de 10 s.",
                     {"clientId": cid, "rssiDbm": rssi}),
            lambda: ("Dispositivo de nuevo en alcance",
                     f"El dispositivo {cid} vuelve a tener buena señal ({where}).",
                     {"clientId": cid, "rssiDbm": rssi}))
        return alerts
