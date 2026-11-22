#!/usr/bin/env python3
"""Bounded physical integration run on the SENTINEL-LAB AP (v0.4.1).

Usage (the host must already be associated with the lab AP; see
docs/validation/p3-integration.md for the wrapper that restores Wi-Fi):

    build/venv/bin/python tools/p3_hw_integration.py <evidence-dir>

Every probe is read-only except the authenticated gateway hello, which is the
intended clock sync. The bearer token is read from
~/.config/sentinel-lab/realtime-token and asserted absent from all output.
Requires jsonschema (packages/protocol/requirements.txt) and chromium.
"""
import json
import socket
import subprocess
import sys
import threading
import time
import urllib.request
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tools"))
sys.path.insert(0, str(REPO / "packages" / "protocol"))
import hw_common as hw  # noqa: E402
import sentinel_gateway as gw  # noqa: E402
import validate_protocol  # noqa: E402

HOST = "192.168.4.1"
OUT = Path(sys.argv[1])
OUT.mkdir(parents=True, exist_ok=True)
TOKEN = (Path.home() / ".config" / "sentinel-lab" / "realtime-token").read_text().strip()
result = {"startedAt": gw.utc_timestamp(), "checks": {}}


def status():
    with urllib.request.urlopen(f"http://{HOST}/api/realtime/status", timeout=4) as r:
        return json.load(r)


def check(name, ok, detail=None):
    result["checks"][name] = {"ok": bool(ok), "detail": detail}


# 1. Before sync.
pre = status()
result["preSync"] = pre
check("firmware_0.4.1", pre.get("firmwareVersion") == "0.4.1", pre.get("firmwareVersion"))
check("fw_http_compat_0.3.1", pre.get("fw") == "0.3.1")
with urllib.request.urlopen(f"http://{HOST}/api/state", timeout=4) as r:
    state_doc = json.load(r)
check("http_api_state", "fw" in state_doc, state_doc.get("fw"))

# 2. Handshake timeout: TCP connect, send nothing.
s = socket.create_connection((HOST, 81), timeout=10)
t0 = time.monotonic()
data = b""
try:
    while True:
        chunk = s.recv(256)
        if not chunk:
            break
        data += chunk
except socket.timeout:
    pass
elapsed = time.monotonic() - t0
s.close()
check("handshake_timeout_408", b"408" in data and 4.0 <= elapsed <= 9.0,
      {"elapsedS": round(elapsed, 2), "response": data[:40].decode("latin1")})

# 3. Gateway (time authority) in background.
gstate = gw.GatewayState(clock_authority=True)
log = OUT / "gateway-frames.jsonl"
link = gw.DeviceLink(HOST, 81, TOKEN, gstate, log)
link.start()
from http.server import ThreadingHTTPServer
web_server = ThreadingHTTPServer(("127.0.0.1", 8099), gw.make_handler(HOST, 81, REPO / "apps" / "web" / "dist", gstate))
threading.Thread(target=web_server.serve_forever, daemon=True).start()
synced = False
for _ in range(50):
    time.sleep(0.2)
    if status().get("clockSynced"):
        synced = True
        break
post = status()
result["postSync"] = post
check("clock_synced_by_gateway", synced and post.get("clockSource") == "peer",
      {"clockSource": post.get("clockSource"), "clockSyncAgeMs": post.get("clockSyncAgeMs")})


# 4. Passive unauthenticated client over 75 s: must survive the 60 s idle
# limit by answering server pings, and must receive telemetry.
passive = {"frames": {}, "pings": 0, "alive": False, "error": None}


def run_passive():
    ws = hw.MinimalWebSocket(HOST, 81, hw.WS_PATH, timeout=5)
    try:
        ws.connect(None)
        deadline = time.monotonic() + 75
        ws.settimeout(1.0)
        while time.monotonic() < deadline:
            try:
                f = ws.recv_frame()
            except TimeoutError:
                continue
            if f["type"] == "ping":
                passive["pings"] += 1
            elif f["type"] == "close":
                passive["error"] = "closed by server"
                return
            elif f["type"] == "text":
                t = json.loads(f["payload"]).get("type", "?")
                passive["frames"][t] = passive["frames"].get(t, 0) + 1
        passive["alive"] = True
    except Exception as exc:  # noqa: BLE001
        passive["error"] = f"{type(exc).__name__}: {exc}"
    finally:
        ws.close()


pt = threading.Thread(target=run_passive)
pt.start()

# 5. FIN without Close frame: slot must be released promptly.
time.sleep(3)
before = status()
ws = hw.MinimalWebSocket(HOST, 81, hw.WS_PATH, timeout=5)
ws.connect(None)
time.sleep(0.5)
during = status()["clientCount"]
ws.socket.shutdown(socket.SHUT_RDWR)
ws.socket.close()
time.sleep(1.0)
after = status()
check("fin_releases_slot",
      during == before["clientCount"] + 1 and after["clientCount"] == before["clientCount"]
      and after["remoteCloses"] > before["remoteCloses"],
      {"before": before["clientCount"], "during": during, "after": after["clientCount"],
       "remoteCloses": [before["remoteCloses"], after["remoteCloses"]]})


# 5b. role "ios" with token is accepted; role "gateway" without token is refused.
def session_probe(token, role, source):
    ws = hw.MinimalWebSocket(HOST, 81, hw.WS_PATH, timeout=5)
    ws.connect(token)
    b = gw.EnvelopeBuilder(f"{role}-probe")
    hello = b.hello()
    hello["source"] = source
    hello["payload"]["role"] = role
    ws.send_text(json.dumps(hello))
    seen = []
    ws.settimeout(1.0)
    end = time.monotonic() + 4
    while time.monotonic() < end:
        try:
            f = ws.recv_frame()
        except TimeoutError:
            continue
        if f["type"] == "text":
            m = json.loads(f["payload"])
            seen.append(m["type"] if m["type"] != "error" else "error:" + m["payload"]["code"])
    ws.close()
    return seen

ios_seen = session_probe(TOKEN, "ios", "ios-app")
check("ios_role_with_token_accepted", "connection.hello" in ios_seen and not any(s.startswith("error") for s in ios_seen), ios_seen[:6])
anon_gw = session_probe(None, "gateway", "fake-gateway")
check("gateway_role_without_token_refused", "error:UNAUTHORIZED" in anon_gw, anon_gw[:6])

pt.join()
result["passive"] = passive
check("passive_client_survives_idle_limit", passive["alive"] and passive["pings"] >= 3,
      {"pings": passive["pings"], "error": passive["error"]})
check("passive_client_receives_telemetry",
      passive["frames"].get("connection.heartbeat", 0) > 0 and passive["frames"].get("connection.hello", 0) == 1,
      passive["frames"])


# 6b. Web dashboard through the gateway, rendered by headless Chromium.
with urllib.request.urlopen("http://127.0.0.1:8099/api/realtime/status", timeout=5) as r:
    proxied = json.load(r)
check("gateway_proxy_realtime_status", proxied.get("firmwareVersion") == "0.4.1")
with urllib.request.urlopen("http://127.0.0.1:8099/", timeout=5) as r:
    check("gateway_serves_web", b"<div id" in r.read())
chrome = ["chromium", "--headless=new", "--disable-gpu", "--no-first-run", "--no-default-browser-check",
          f"--user-data-dir={OUT}/chrome-profile", "--window-size=1280,1000", "--virtual-time-budget=12000"]
subprocess.run(chrome + [f"--screenshot={OUT}/web-dashboard.png", "http://127.0.0.1:8099/"],
               capture_output=True, timeout=60)
dom = subprocess.run(chrome + ["--dump-dom", "http://127.0.0.1:8099/"], capture_output=True, text=True, timeout=60).stdout
(OUT / "web-dom.html").write_text(dom)
check("web_renders_device_data", "0.4.1" in dom and pre["bootId"] in dom,
      {"domBytes": len(dom), "has041": "0.4.1" in dom, "hasBootId": pre["bootId"] in dom})

# 7. Soak: status every 10 s for 300 s while the gateway keeps streaming.
samples = []
for _ in range(24):
    time.sleep(10)
    st = status()
    samples.append({k: st[k] for k in ("bootId", "uptimeMs", "freeHeapBytes", "minFreeHeapBytes",
                                         "largestFreeBlockBytes", "minLoopStackFreeBytes", "clientCount",
                                         "droppedFrames", "droppedTelemetry", "clockSynced")})
result["soak"] = samples
mono = all(b["uptimeMs"] > a["uptimeMs"] for a, b in zip(samples, samples[1:]))
check("soak_240s_same_boot_monotonic", mono and len({s["bootId"] for s in samples}) == 1 and samples[0]["bootId"] == pre["bootId"],
      {"samples": len(samples), "heapMin": min(s["freeHeapBytes"] for s in samples),
       "heapMax": max(s["freeHeapBytes"] for s in samples), "stackMin": min(s["minLoopStackFreeBytes"] for s in samples)})
check("soak_clock_stays_synced", all(s["clockSynced"] for s in samples))

# 6. Health samples and schema validation of everything the gateway received.
link.stop()
link.join(5)
final = status()
result["final"] = final
result["gateway"] = gstate.snapshot()
messages = [json.loads(line)["message"] for line in log.read_text().splitlines()]
errors = {}
for m in messages:
    e = validate_protocol.validate_message(m)
    if e:
        errors.setdefault(m.get("type"), e[:2])
check("all_frames_schema_valid", messages and not errors,
      {"frames": len(messages), "types": gstate.snapshot()["framesByType"], "errors": errors})
check("no_drops", final["droppedFrames"] == 0 and final["droppedTelemetry"] == 0,
      {k: final[k] for k in ("droppedFrames", "droppedTelemetry", "rejectedMessages")})
check("same_boot", final["bootId"] == pre["bootId"] and final["uptimeMs"] > pre["uptimeMs"])
result["finishedAt"] = gw.utc_timestamp()
result["allOk"] = all(c["ok"] for c in result["checks"].values())
text = json.dumps(result, indent=2)
assert TOKEN not in text and TOKEN not in log.read_text()
(OUT / "summary.json").write_text(text)
print(text)
