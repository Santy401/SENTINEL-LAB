"""Gateway tests: unit checks plus an end-to-end run against the host emulator,
which executes the real firmware RealtimeServer code over loopback TCP.
This is host emulation, not hardware evidence."""

from __future__ import annotations

import json
import os
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "packages" / "protocol"))

import sentinel_gateway as gw  # noqa: E402

try:
    import validate_protocol  # noqa: E402
except ModuleNotFoundError:  # jsonschema missing on this host
    validate_protocol = None

EMULATOR = ROOT / "build" / "host_device"
TOKEN = "emulator-token-0123456789abcdef"


def free_port() -> int:
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def wait_for(predicate, timeout: float = 8.0) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return True
        time.sleep(0.05)
    return False


class UnitTests(unittest.TestCase):
    def test_backoff_is_bounded_and_grows(self) -> None:
        self.assertEqual(gw.backoff_delay(0, rng=lambda: 1.0), 0.5)
        self.assertEqual(gw.backoff_delay(3, rng=lambda: 1.0), 4.0)
        self.assertEqual(gw.backoff_delay(20, rng=lambda: 1.0), gw.MAX_BACKOFF_S)
        self.assertGreaterEqual(gw.backoff_delay(5, rng=lambda: 0.0), 0.25)

    def test_envelopes_follow_protocol_v1(self) -> None:
        builder = gw.EnvelopeBuilder("gateway-test")
        messages = [builder.hello(), builder.heartbeat(), builder.goodbye()]
        self.assertEqual([m["sequence"] for m in messages], [1, 2, 3])
        self.assertEqual({m["sessionId"] for m in messages}, {builder.session_id})
        for message in messages:
            self.assertLessEqual(len(json.dumps(message)), 1024)
            if validate_protocol is not None:
                self.assertEqual(validate_protocol.validate_message(message), [], message)

    def test_token_file_must_be_owner_only(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "token"
            path.write_text("secret-value\n")
            path.chmod(0o644)
            with self.assertRaises(PermissionError):
                gw.load_token(path, "UNUSED")
            path.chmod(0o600)
            self.assertEqual(gw.load_token(path, "UNUSED"), "secret-value")

    def test_status_never_contains_token(self) -> None:
        state = gw.GatewayState(clock_authority=True)
        self.assertNotIn(TOKEN, json.dumps(state.snapshot()))


class FakeDevice(BaseHTTPRequestHandler):
    def log_message(self, *args) -> None:
        pass

    def do_GET(self) -> None:  # noqa: N802
        body = json.dumps({"fw": "0.3.1", "path": self.path}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


class HttpTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.device = ThreadingHTTPServer(("127.0.0.1", 0), FakeDevice)
        threading.Thread(target=cls.device.serve_forever, daemon=True).start()
        cls.web = tempfile.TemporaryDirectory()
        Path(cls.web.name, "index.html").write_text("<!doctype html><title>t</title>")
        Path(cls.web.name, "app.js").write_text("console.log(1)")
        device_host = f"127.0.0.1:{cls.device.server_address[1]}"
        cls.state = gw.GatewayState(clock_authority=False)
        handler = gw.make_handler(device_host, 81, Path(cls.web.name), cls.state)
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        threading.Thread(target=cls.server.serve_forever, daemon=True).start()
        cls.base = f"http://127.0.0.1:{cls.server.server_address[1]}"

    @classmethod
    def tearDownClass(cls) -> None:
        cls.server.shutdown()
        cls.device.shutdown()
        cls.web.cleanup()

    def get(self, path: str):
        return urllib.request.urlopen(self.base + path, timeout=3)

    def test_proxy_forwards_get(self) -> None:
        with self.get("/api/state?x=1") as response:
            self.assertEqual(json.load(response), {"fw": "0.3.1", "path": "/api/state?x=1"})

    def test_proxy_rejects_writes(self) -> None:
        request = urllib.request.Request(self.base + "/api/experiments", data=b"{}", method="POST")
        with self.assertRaises(urllib.error.HTTPError) as caught:
            urllib.request.urlopen(request, timeout=3)
        self.assertEqual(caught.exception.code, 405)

    def test_static_and_spa_fallback(self) -> None:
        with self.get("/app.js") as response:
            self.assertIn("javascript", response.headers["Content-Type"])
        with self.get("/telemetria") as response:
            self.assertIn(b"<title>t</title>", response.read())

    def test_path_traversal_is_blocked(self) -> None:
        with self.assertRaises(urllib.error.HTTPError) as caught:
            self.get("/../../etc/passwd")
        self.assertIn(caught.exception.code, (403, 404))

    def test_config_and_status(self) -> None:
        with self.get("/gateway/config") as response:
            config = json.load(response)
        self.assertTrue(config["wsUrl"].endswith(":81/realtime"))
        with self.get("/gateway/status") as response:
            self.assertEqual(json.load(response)["clockAuthority"], False)


@unittest.skipUnless(EMULATOR.exists(), "build/host_device not built")
class EmulatorIntegration(unittest.TestCase):
    def start_emulator(self, token: str | None) -> tuple[subprocess.Popen, int]:
        port = free_port()
        env = dict(os.environ)
        env.pop("SENTINEL_EMULATOR_TOKEN", None)
        if token:
            env["SENTINEL_EMULATOR_TOKEN"] = token
        process = subprocess.Popen([str(EMULATOR), str(port)], env=env,
                                   stdout=subprocess.PIPE, text=True)
        self.assertIn("listening", process.stdout.readline())
        self.addCleanup(process.wait, 3)
        self.addCleanup(process.terminate)
        return process, port

    def test_gateway_syncs_clock_and_receives_frames(self) -> None:
        _, port = self.start_emulator(TOKEN)
        state = gw.GatewayState(clock_authority=True)
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "frames.jsonl"
            link = gw.DeviceLink("127.0.0.1", port, TOKEN, state, log)
            link.start()
            self.addCleanup(link.join, 3)
            self.addCleanup(link.stop)
            self.assertTrue(wait_for(lambda: state.snapshot()["framesByType"].get(
                "connection.heartbeat", 0) >= 2), state.snapshot())
            snapshot = state.snapshot()
            self.assertEqual(snapshot["state"], "connected")
            self.assertEqual(snapshot["framesByType"].get("connection.hello"), 1)
            self.assertTrue(snapshot["deviceSessionId"].startswith("host-emulator-p"))
            self.assertIsNone(snapshot["lastError"])
            lines = log.read_text().splitlines()
            self.assertGreaterEqual(len(lines), 3)
            self.assertNotIn(TOKEN, log.read_text())
            if validate_protocol is not None:
                for line in lines:
                    message = json.loads(line)["message"]
                    self.assertEqual(validate_protocol.validate_message(message), [], message)

    def test_wrong_token_does_not_sync(self) -> None:
        _, port = self.start_emulator(TOKEN)
        state = gw.GatewayState(clock_authority=True)
        link = gw.DeviceLink("127.0.0.1", port, "wrong-token", state)
        link.start()
        self.addCleanup(link.join, 3)
        self.addCleanup(link.stop)
        self.assertTrue(wait_for(lambda: state.snapshot()["state"] == "connected"))
        time.sleep(1.5)
        # Unsynced firmware emits nothing at all, not even an error envelope.
        self.assertEqual(state.snapshot()["framesReceived"], 0)

    def test_reconnects_after_device_restart(self) -> None:
        process, port = self.start_emulator(TOKEN)
        state = gw.GatewayState(clock_authority=True)
        link = gw.DeviceLink("127.0.0.1", port, TOKEN, state)
        link.start()
        self.addCleanup(link.join, 3)
        self.addCleanup(link.stop)
        self.assertTrue(wait_for(lambda: state.snapshot()["framesReceived"] >= 2))
        process.terminate()
        process.wait(3)
        self.assertTrue(wait_for(lambda: state.snapshot()["state"] != "connected"))
        env = dict(os.environ, SENTINEL_EMULATOR_TOKEN=TOKEN)
        restarted = subprocess.Popen([str(EMULATOR), str(port)], env=env,
                                     stdout=subprocess.DEVNULL)
        self.addCleanup(restarted.wait, 3)
        self.addCleanup(restarted.terminate)
        before = state.snapshot()["framesByType"].get("connection.hello", 0)
        self.assertTrue(wait_for(lambda: state.snapshot()["framesByType"].get(
            "connection.hello", 0) > before, timeout=40), state.snapshot())
        self.assertGreaterEqual(state.snapshot()["reconnects"], 1)


if __name__ == "__main__":
    unittest.main()
