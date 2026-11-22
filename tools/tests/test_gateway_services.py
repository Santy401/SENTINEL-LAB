"""Gateway services: alert rules, history/retention, signed delivery, reports."""

from __future__ import annotations

import hashlib
import hmac
import itertools
import json
import os
import shutil
import socket
import sys
import tempfile
import threading
import unittest
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import sentinel_gateway as gw  # noqa: E402
from gateway.alerts import AlertEngine  # noqa: E402
from gateway.delivery import AlertDelivery, backoff_ms, load_secret, sign  # noqa: E402
from gateway.history import History, MotionAggregator  # noqa: E402
from gateway.report import build_report, render_pdf  # noqa: E402
from gateway.runtime import GatewayRuntime  # noqa: E402

# Non-secret fixture key, built at runtime so it is not mistaken for a credential.
SECRET = "-".join(("unit", "test", "hmac", "fixture", "0123456789"))


def status(**over):
    base = {"bootId": "boot-a", "firmwareVersion": "0.5.0", "resetReason": "POWERON",
            "freeHeapBytes": 200_000, "largestFreeBlockBytes": 150_000, "clientCount": 1,
            "clockSynced": True, "uptimeMs": 1000, "minFreeHeapBytes": 190_000,
            "phoneMotion": {"active": False, "intensity": 0.0}, "rgb": {"mode": "synced"}}
    base.update(over)
    return base


def engine() -> AlertEngine:
    counter = itertools.count(1)
    return AlertEngine(id_factory=lambda: f"a{next(counter)}")


def rules(alerts):
    return [(a["rule"], a["kind"]) for a in alerts]


class AlertRuleTests(unittest.TestCase):
    def test_shake_sustain_hysteresis_and_cooldown(self) -> None:
        e = engine()
        e.on_status(status(), 0)
        hot = status(phoneMotion={"active": True, "intensity": 0.9})
        self.assertEqual(e.on_status(hot, 1_000), [])               # not yet sustained
        fired = e.on_status(hot, 2_100)
        self.assertEqual(rules(fired), [("strong_shake", "trigger")])
        self.assertEqual(fired[0]["severity"], "warning")
        self.assertIn("Agitación fuerte", fired[0]["title"])
        # 0.6 is inside the hysteresis band: still active, no recovery.
        self.assertEqual(e.on_status(status(phoneMotion={"active": True, "intensity": 0.6}), 3_000), [])
        calm = status(phoneMotion={"active": True, "intensity": 0.1})
        self.assertEqual(e.on_status(calm, 4_000), [])
        rec = e.on_status(calm, 9_500)
        self.assertEqual(rules(rec), [("strong_shake", "recovery")])
        self.assertEqual(rec[0]["severity"], "recovery")
        # Cooldown: a new shake within 120 s of the last trigger stays silent.
        e.on_status(hot, 10_000)
        self.assertEqual(e.on_status(hot, 12_000), [])
        e.on_status(calm, 13_000); e.on_status(calm, 19_000)
        e.on_status(hot, 130_000)
        self.assertEqual(rules(e.on_status(hot, 131_500)), [("strong_shake", "trigger")])

    def test_motion_alert_state_sustained(self) -> None:
        e = engine()
        payload = {"linearAcceleration": {"x": 5.0, "y": 0, "z": 0}, "state": "ALERT"}
        self.assertEqual(e.on_motion(payload, 0), [])
        self.assertEqual(rules(e.on_motion(payload, 1_200)), [("strong_shake", "trigger")])

    def test_reboot_reports_reason_and_severity(self) -> None:
        e = engine()
        e.on_status(status(), 0)
        alerts = e.on_status(status(bootId="boot-b", resetReason="PANIC"), 10_000)
        self.assertEqual(rules(alerts), [("esp_reboot", "trigger")])
        self.assertEqual(alerts[0]["severity"], "critical")
        self.assertEqual(alerts[0]["metrics"]["resetReason"], "PANIC")
        soft = e.on_status(status(bootId="boot-c", resetReason="POWERON"), 20_000)
        self.assertEqual(soft[0]["severity"], "warning")

    def test_heap_low_and_recovery(self) -> None:
        e = engine()
        low = status(freeHeapBytes=30_000)
        self.assertEqual(e.on_status(low, 0), [])
        self.assertEqual(rules(e.on_status(low, 21_000)), [("heap_low", "trigger")])
        mid = status(freeHeapBytes=45_000)   # above trigger, below healthy: stays active
        self.assertEqual(e.on_status(mid, 30_000), [])
        ok = status(freeHeapBytes=80_000)
        e.on_status(ok, 40_000)
        self.assertEqual(rules(e.on_status(ok, 61_000)), [("heap_low", "recovery")])

    def test_clock_unsynced_only_with_clients(self) -> None:
        e = engine()
        e.on_status(status(clockSynced=False, clientCount=0), 0)
        self.assertEqual(e.on_status(status(clockSynced=False, clientCount=0), 200_000), [])
        e.on_status(status(clockSynced=False, clientCount=2), 200_000)
        self.assertEqual(rules(e.on_status(status(clockSynced=False, clientCount=2), 321_000)),
                         [("clock_unsynced", "trigger")])
        self.assertEqual(rules(e.on_status(status(), 330_000)), [("clock_unsynced", "recovery")])

    def test_unreachable_and_back(self) -> None:
        e = engine()
        e.on_status(status(), 0)
        self.assertEqual(e.on_status(None, 30_000), [])
        down = e.on_status(None, 61_000)
        self.assertEqual(rules(down), [("esp_unreachable", "trigger")])
        self.assertEqual(down[0]["severity"], "critical")
        self.assertEqual(rules(e.on_status(status(), 70_000)), [("esp_unreachable", "recovery")])

    def test_out_of_range_and_disappearance(self) -> None:
        e = engine()
        good = [{"clientId": 7, "connected": True, "filteredRssiDbm": -60}]
        weak = [{"clientId": 7, "connected": True, "filteredRssiDbm": -90}]
        e.on_rf(good, 0)
        e.on_rf(weak, 1_000)
        self.assertEqual(rules(e.on_rf(weak, 11_500)), [("phone_out_of_range", "trigger")])
        # -82 dBm is inside the hysteresis band: no recovery yet.
        e.on_rf([{"clientId": 7, "connected": True, "filteredRssiDbm": -82}], 13_000)
        self.assertEqual(e.on_rf([{"clientId": 7, "connected": True, "filteredRssiDbm": -82}], 20_000), [])
        e.on_rf(good, 21_000)
        self.assertEqual(rules(e.on_rf(good, 26_500)), [("phone_out_of_range", "recovery")])
        e2 = engine()
        e2.on_rf(good, 0)
        e2.on_rf([], 1_000)
        self.assertEqual(rules(e2.on_rf([], 11_200)), [("phone_out_of_range", "trigger")])

    def test_global_hourly_cap(self) -> None:
        e = AlertEngine(max_per_hour=2)
        e.on_status(status(), 0)
        total = 0
        for i in range(5):
            total += len(e.on_status(status(bootId=f"b{i}"), 1_000 * (i + 1)))
        self.assertEqual(total, 2)
        self.assertEqual(e.suppressed, 3)


class HistoryTests(unittest.TestCase):
    def setUp(self) -> None:
        self.dir = tempfile.TemporaryDirectory()
        self.db = History(Path(self.dir.name) / "h.db")
        self.sid = self.db.start_session("t", "127.0.0.1", 1_000)

    def tearDown(self) -> None:
        self.db.close()
        self.dir.cleanup()

    def test_file_is_owner_only(self) -> None:
        self.assertEqual(os.stat(self.db.path).st_mode & 0o077, 0)

    def test_rf_rate_limit_and_motion_aggregation(self) -> None:
        self.assertTrue(self.db.add_rf(self.sid, 1, -60, -61, True, 10_000))
        self.assertFalse(self.db.add_rf(self.sid, 1, -60, -61, True, 10_500))
        self.assertTrue(self.db.add_rf(self.sid, 2, -70, -70, True, 10_500))
        agg = MotionAggregator()
        self.assertIsNone(agg.add(1.0, "MOVEMENT", 5_100))
        self.assertIsNone(agg.add(3.0, "ALERT", 5_900))
        done = agg.add(0.2, "STABLE", 6_050)
        self.assertEqual(done["samples"], 2)
        self.assertEqual(done["peak"], 3.0)
        self.assertEqual(done["mean"], 2.0)
        self.assertEqual((done["movement"], done["alert"]), (1, 1))

    def test_retention_by_age_keeps_undelivered_alerts(self) -> None:
        day = 86_400_000
        self.db.add_status(self.sid, status(), 1_000)
        self.db.add_status(self.sid, status(), 40 * day)
        old = {"id": "old", "occurredAt": "x", "occurredMs": 1_000, "rule": "r", "severity": "info",
               "kind": "trigger", "title": "t", "message": "m"}
        self.db.add_alert(self.sid, old)
        removed = self.db.prune(40 * day, max_age_days=30)
        self.assertEqual(removed, 1)
        self.assertEqual(len(self.db.rows("status_samples", self.sid)), 1)
        self.assertEqual(len(self.db.pending_alerts(40 * day)), 1)

    def test_retention_by_size(self) -> None:
        for i in range(3000):
            self.db.add_rf(self.sid, i, -60.0, -60.0, True, 10_000 + i)
        before = len(self.db.rows("rf_samples", self.sid))
        self.db.prune(20_000, max_age_days=30, max_bytes=50_000)
        self.assertLess(len(self.db.rows("rf_samples", self.sid)), before)
        self.assertLess(len(self.db.rows("rf_samples", self.sid)), before // 2)


class Collector(BaseHTTPRequestHandler):
    received: list = []
    status_code = 202

    def log_message(self, *args) -> None:
        pass

    def do_POST(self) -> None:  # noqa: N802
        body = self.rfile.read(int(self.headers["Content-Length"]))
        Collector.received.append((dict(self.headers), body))
        self.send_response(Collector.status_code)
        self.end_headers()
        self.wfile.write(b'{"ok":true}')


def free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class DeliveryTests(unittest.TestCase):
    def setUp(self) -> None:
        self.dir = tempfile.TemporaryDirectory()
        self.db = History(Path(self.dir.name) / "h.db")
        self.sid = self.db.start_session("t", "dev", 0)
        self.clock = [100_000]
        Collector.received = []
        Collector.status_code = 202

    def tearDown(self) -> None:
        self.db.close()
        self.dir.cleanup()

    def queue_alert(self, alert_id: str = "a1") -> None:
        e = AlertEngine(id_factory=lambda: alert_id)
        e.on_status(status(), 0)
        for a in e.on_status(status(bootId="boot-z", resetReason="TASK_WDT"), 1):
            self.db.add_alert(self.sid, a)

    def test_signature_matches_independent_hmac(self) -> None:
        body = '{"a":"ñ"}'.encode()
        expected = hmac.new(SECRET.encode(), b"1700000000." + body, hashlib.sha256).hexdigest()
        self.assertEqual(sign(SECRET, "1700000000", body), expected)
        self.assertEqual(len(expected), 64)

    def test_secret_file_permissions(self) -> None:
        path = Path(self.dir.name) / "secret"
        path.write_text(SECRET + "\n")
        path.chmod(0o644)
        with self.assertRaises(PermissionError):
            load_secret(path)
        path.chmod(0o600)
        self.assertEqual(load_secret(path), SECRET)

    def test_offline_then_online_with_backoff(self) -> None:
        port = free_port()
        self.queue_alert()
        d = AlertDelivery(self.db, f"http://127.0.0.1:{port}/webhook/sentinel-alerts", SECRET,
                          clock=lambda: self.clock[0], timeout=2, version="9.9.9")
        self.assertEqual(d.deliver_once(), 0)          # nothing listening: offline
        self.assertEqual(self.db.pending_alerts(self.clock[0]), [])   # backing off
        self.assertEqual(len(self.db.pending_alerts(self.clock[0] + backoff_ms(0))), 1)
        server = ThreadingHTTPServer(("127.0.0.1", port), Collector)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        try:
            self.clock[0] += backoff_ms(0)
            self.assertEqual(d.deliver_once(), 1)
        finally:
            server.shutdown()
            server.server_close()
        headers, body = Collector.received[0]
        self.assertEqual(headers["User-Agent"], "SentinelLabGateway/9.9.9")
        ts = headers["X-Sentinel-Timestamp"]
        self.assertEqual(headers["X-Sentinel-Signature"],
                         hmac.new(SECRET.encode(), ts.encode() + b"." + body, hashlib.sha256).hexdigest())
        payload = json.loads(body)
        self.assertEqual(set(payload), {"id", "rule", "kind", "severity", "title", "message",
                                        "occurredAt", "device", "metrics"})
        self.assertEqual(payload["metrics"]["resetReason"], "TASK_WDT")
        self.assertNotIn(SECRET, body.decode())
        self.assertEqual(self.db.summary()["pendingAlerts"], 0)

    def test_unauthorized_is_permanent(self) -> None:
        port = free_port()
        Collector.status_code = 401
        server = ThreadingHTTPServer(("127.0.0.1", port), Collector)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        try:
            self.queue_alert()
            d = AlertDelivery(self.db, f"http://127.0.0.1:{port}/x", SECRET,
                              clock=lambda: self.clock[0], timeout=2)
            self.assertEqual(d.deliver_once(), 0)
        finally:
            server.shutdown()
            server.server_close()
        self.assertEqual(d.rejected, 1)
        self.assertEqual(self.db.summary()["pendingAlerts"], 0)
        self.assertEqual(self.db.pending_alerts(10**13), [])

    def test_plain_http_to_remote_host_refused(self) -> None:
        with self.assertRaises(ValueError):
            AlertDelivery(self.db, "http://n8n.example/webhook", SECRET)


class ReportAndEndpointTests(unittest.TestCase):
    def setUp(self) -> None:
        self.dir = tempfile.TemporaryDirectory()
        self.db = History(Path(self.dir.name) / "h.db")
        clock = [1_000_000]
        self.runtime = GatewayRuntime(self.db, AlertEngine(), self.db.start_session("0.2.0", "dev", 1_000_000),
                                      clock=lambda: clock[0])
        for i in range(30):
            clock[0] = 1_000_000 + i * 1_000
            self.runtime.on_status(status(freeHeapBytes=200_000 - i * 100,
                                          phoneMotion={"active": True, "intensity": 0.95 if 10 < i < 15 else 0.1}))
            self.runtime.on_message({"type": "rf.telemetry", "payload": {"clients": [
                {"clientId": 5, "connected": True, "rssiDbm": -55 - i, "filteredRssiDbm": -56 - i}]}})
            self.runtime.on_message({"type": "motion.sample", "payload": {
                "linearAcceleration": {"x": 0.2 * i, "y": 0, "z": 0}, "state": "MOVEMENT"}})
            self.runtime.on_message({"type": "event", "payload": {"name": "device_connected",
                                                                  "details": {"clientId": 5, "rssiDbm": -50}}})
        self.runtime.on_lab({"experiments": [{"id": 1, "name": "Pasillo", "clientId": 5, "calibrated": True,
                                              "n": 2.4, "referenceRssi": -41.0, "currentEstimateM": 3.2}],
                             "measurements": [{"experimentId": 1, "actualM": 1, "rssiDbm": -41,
                                               "filteredDbm": -41, "estimatedM": 1.0, "errorM": 0.0}]})

    def tearDown(self) -> None:
        self.db.close()
        self.dir.cleanup()

    def test_report_contains_charts_and_data(self) -> None:
        html = build_report(self.db, "latest")
        self.assertIn("Informe de sesión SENTINEL-LAB", html)
        self.assertGreaterEqual(html.count("<svg"), 4)
        self.assertIn("Pasillo", html)
        self.assertIn("Agitación fuerte", html)
        self.assertNotIn("<script", html)
        with self.assertRaises(ValueError):
            build_report(self.db, 999)

    @unittest.skipUnless(shutil.which("chromium") and os.environ.get("SENTINEL_TEST_PDF") == "1",
                         "set SENTINEL_TEST_PDF=1 to render with chromium")
    def test_pdf(self) -> None:
        out = Path(self.dir.name) / "r.html"
        out.write_text(build_report(self.db, "latest"))
        self.assertTrue(render_pdf(out, out.with_suffix(".pdf")))

    def test_report_cli(self) -> None:
        out = Path(self.dir.name) / "cli.html"
        self.db.close()
        self.assertEqual(gw.main(["report", "--db", str(Path(self.dir.name) / "h.db"),
                                  "--out", str(out)]), 0)
        self.assertTrue(out.read_text().startswith("<!doctype html>"))
        self.db = History(Path(self.dir.name) / "h.db")

    def test_alert_and_summary_endpoints(self) -> None:
        state = gw.GatewayState(clock_authority=False)
        handler = gw.make_handler("127.0.0.1:9", 81, Path(self.dir.name), state, self.runtime)
        server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        base = f"http://127.0.0.1:{server.server_address[1]}"
        try:
            alerts = json.load(urllib.request.urlopen(base + "/gateway/alerts", timeout=3))["alerts"]
            self.assertTrue(any(a["rule"] == "strong_shake" for a in alerts))
            summary = json.load(urllib.request.urlopen(base + "/gateway/history/summary", timeout=3))
            self.assertTrue(summary["enabled"])
            self.assertEqual(summary["counts"]["status_samples"], 30)
            self.assertGreater(summary["counts"]["motion_summaries"], 0)
        finally:
            server.shutdown()
            server.server_close()


if __name__ == "__main__":
    unittest.main()
