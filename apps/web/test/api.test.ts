import { describe, expect, it } from "vitest";
import { ApiShapeError, getJson, HttpError, parseEvents, parseGatewayConfig, parseRealtimeStatus, parseRfSnapshot } from "@/lib/api";

// Exact body produced by writeRealtimeHealth() (see test/test_health.cpp).
const HEALTH_BODY =
  '{"fw":"0.3.1","httpApiVersion":"0.3.1","firmwareVersion":"0.4.1","realtimeVersion":"0.4.1","bootId":"rf-boot-id",' +
  '"resetReason":"POWERON","uptimeMs":45000,"clockSynced":false,"clockSource":"none","clockSyncAgeMs":null,"clientCount":2,' +
  '"freeHeapBytes":50000,"minFreeHeapBytes":50000,"largestFreeBlockBytes":25000,"minLoopStackFreeBytes":4096,' +
  '"heapSamples":4,"droppedFrames":3,"rejectedMessages":4,"droppedTelemetry":0,"skippedSimulatedEvents":0,' +
  '"handshakeTimeouts":0,"idleTimeouts":0,"remoteCloses":0}';

// Body served by the v0.4.0 image already flashed (13 fields, no diagnostics).
const LEGACY_BODY =
  '{"fw":"0.3.1","realtimeVersion":"0.4.0","bootId":"a1b2c3d4e5f60718","uptimeMs":150000,"clockSynced":false,' +
  '"clientCount":0,"freeHeapBytes":215132,"minFreeHeapBytes":215000,"heapSamples":10,"droppedFrames":0,' +
  '"rejectedMessages":0,"droppedTelemetry":0,"skippedSimulatedEvents":0}';

describe("parseRealtimeStatus", () => {
  it("reads every field of the v0.4.1 route", () => {
    const status = parseRealtimeStatus(JSON.parse(HEALTH_BODY));
    expect(status.firmwareVersion).toBe("0.4.1");
    expect(status.clockSource).toBe("none");
    expect(status.clockSyncAgeMs).toBeNull();
    expect(status.minLoopStackFreeBytes).toBe(4096);
    expect(status.remoteCloses).toBe(0);
  });

  it("accepts the v0.4.0 route and marks new diagnostics as absent", () => {
    const status = parseRealtimeStatus(JSON.parse(LEGACY_BODY));
    expect(status.realtimeVersion).toBe("0.4.0");
    expect(status.firmwareVersion).toBeNull();
    expect(status.resetReason).toBeNull();
    expect(status.largestFreeBlockBytes).toBeNull();
  });

  it("rejects bodies missing required fields instead of inventing values", () => {
    const body = JSON.parse(HEALTH_BODY) as Record<string, unknown>;
    delete body.freeHeapBytes;
    expect(() => parseRealtimeStatus(body)).toThrow(ApiShapeError);
    expect(() => parseRealtimeStatus([])).toThrow(ApiShapeError);
  });
});

describe("HTTP v0.3.1 parsers", () => {
  it("parses /api/rf with null RSSI and relative history", () => {
    const rf = parseRfSnapshot({
      bootId: "b",
      fw: "0.3.1",
      uptimeMs: 9000,
      mode: "AP",
      available: true,
      ssid: "SENTINEL-LAB",
      ip: "192.168.4.1",
      clients: 1,
      channel: 1,
      frequencyMHz: 2412,
      devices: [{ id: 3, connected: true, lastSeenMs: 8800, rssiDbm: -55, filteredDbm: null, history: [{ t: 8000, rssi: -54 }, { t: "x", rssi: -1 }] }],
      events: [],
    });
    expect(rf.devices[0]?.filteredDbm).toBeNull();
    expect(rf.devices[0]?.history).toEqual([{ t: 8000, rssi: -54 }]);
    expect(rf.channel).toBe(1);
  });

  it("parses /api/events using the snake_case firmware keys", () => {
    const events = parseEvents({ bootId: "b", notificationsEnabled: false, events: [{ boot_id: "b", sequence: 4, timestamp_ms: 1200, event: "SIGNAL_WEAK", client_id: 3, simulated: false, notify: true, rssi_dbm: -88 }] });
    expect(events.events[0]).toEqual({ sequence: 4, timestampMs: 1200, event: "SIGNAL_WEAK", clientId: 3, simulated: false, rssiDbm: -88 });
  });

  it("validates the gateway config URL scheme", () => {
    expect(parseGatewayConfig({ wsUrl: "ws://192.168.4.1:81/realtime", deviceHttp: "http://192.168.4.1", gatewayVersion: "0.1.0" }).wsUrl).toContain("ws://");
    expect(() => parseGatewayConfig({ wsUrl: "javascript:alert(1)" })).toThrow(ApiShapeError);
  });
});

describe("getJson", () => {
  const response = (status: number, body: string) => Promise.resolve(new Response(body, { status, headers: { "Content-Type": "application/json" } }));

  it("returns parsed bodies and maps HTTP errors", async () => {
    await expect(getJson("x", parseRealtimeStatus, { fetchImpl: () => response(200, HEALTH_BODY) })).resolves.toMatchObject({ bootId: "rf-boot-id" });
    await expect(getJson("x", parseRealtimeStatus, { fetchImpl: () => response(503, "{}") })).rejects.toBeInstanceOf(HttpError);
    await expect(getJson("x", parseRealtimeStatus, { fetchImpl: () => response(200, "<html>") })).rejects.toBeInstanceOf(ApiShapeError);
  });

  it("aborts after the timeout", async () => {
    const hang: typeof fetch = (_input, init) =>
      new Promise((_resolve, reject) => init?.signal?.addEventListener("abort", () => reject(new DOMException("aborted", "AbortError"))));
    await expect(getJson("x", parseRealtimeStatus, { fetchImpl: hang, timeoutMs: 20 })).rejects.toMatchObject({ name: "AbortError" });
  });
});
