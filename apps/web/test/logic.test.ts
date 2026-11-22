import { describe, expect, it } from "vitest";
import { parseRealtimeStatus, type RealtimeStatus } from "@/lib/api";
import { Backoff } from "@/lib/backoff";
import { demoFrames, demoStatus } from "@/lib/demo";
import { buildEvidence, redact } from "@/lib/evidence";
import { evaluateHealth, healthScore, overallLevel } from "@/lib/health";
import { Ring } from "@/lib/ring";

describe("Backoff", () => {
  it("doubles with jitter in [ceiling/2, ceiling] and caps at 30 s", () => {
    const low = new Backoff({ random: () => 0 });
    const high = new Backoff({ random: () => 1 });
    const lows = Array.from({ length: 8 }, () => low.next());
    const highs = Array.from({ length: 8 }, () => high.next());
    expect(lows).toEqual([500, 1000, 2000, 4000, 8000, 15000, 15000, 15000]);
    expect(highs).toEqual([1000, 2000, 4000, 8000, 16000, 30000, 30000, 30000]);
    high.reset();
    expect(high.next()).toBe(1000);
  });

  it("rejects nonsensical options", () => {
    expect(() => new Backoff({ baseMs: 0 })).toThrow();
    expect(() => new Backoff({ baseMs: 10, maxMs: 5 })).toThrow();
  });
});

describe("Ring", () => {
  it("keeps the newest items in order within capacity", () => {
    const ring = new Ring<number>(3);
    for (let i = 1; i <= 5; i += 1) ring.push(i);
    expect(ring.toArray()).toEqual([3, 4, 5]);
    expect(ring.size).toBe(3);
    expect(ring.last()).toBe(5);
    ring.clear();
    expect(ring.toArray()).toEqual([]);
    expect(() => new Ring(0)).toThrow();
  });
});

const status = (patch: Partial<RealtimeStatus> = {}): RealtimeStatus => ({
  ...parseRealtimeStatus({
    fw: "0.3.1",
    realtimeVersion: "0.4.1",
    firmwareVersion: "0.4.1",
    bootId: "boot-a",
    uptimeMs: 1000,
    clockSynced: true,
    clockSource: "peer",
    clockSyncAgeMs: 10,
    clientCount: 1,
    freeHeapBytes: 210000,
    minFreeHeapBytes: 200000,
    largestFreeBlockBytes: 100000,
    minLoopStackFreeBytes: 4000,
    heapSamples: 2,
    droppedFrames: 0,
    rejectedMessages: 0,
    droppedTelemetry: 0,
    skippedSimulatedEvents: 0,
    resetReason: "POWERON",
  }),
  ...patch,
});

describe("evaluateHealth", () => {
  it("is fully healthy with a synced clock and an open socket", () => {
    const indicators = evaluateHealth({ status: status(), previous: status(), httpError: null, socketState: "open" });
    expect(overallLevel(indicators)).toBe("ok");
    expect(healthScore(indicators)).toBe(100);
  });

  it("flags an unsynced clock, low heap, low stack, abnormal reset and reboots", () => {
    const current = status({ clockSynced: false, freeHeapBytes: 30000, minLoopStackFreeBytes: 900, resetReason: "TASK_WDT", bootId: "boot-b" });
    const indicators = evaluateHealth({ status: current, previous: status(), httpError: null, socketState: "open" });
    const byId = Object.fromEntries(indicators.map(i => [i.id, i.level]));
    expect(byId).toMatchObject({ clock: "warn", heap: "critical", stack: "critical", reset: "warn", reboot: "warn" });
    expect(overallLevel(indicators)).toBe("critical");
  });

  it("warns only when counters increase between readings", () => {
    const steady = evaluateHealth({ status: status({ droppedFrames: 5 }), previous: status({ droppedFrames: 5 }), httpError: null, socketState: "open" });
    expect(steady.find(i => i.id === "transport")?.level).toBe("ok");
    const rising = evaluateHealth({ status: status({ droppedFrames: 7 }), previous: status({ droppedFrames: 5 }), httpError: null, socketState: "open" });
    expect(rising.find(i => i.id === "transport")?.detail).toContain("+2");
  });

  it("reports an unreachable device as critical without other guesses", () => {
    const indicators = evaluateHealth({ status: null, previous: null, httpError: "HTTP 503", socketState: "closed" });
    expect(indicators).toHaveLength(1);
    expect(indicators[0]?.level).toBe("critical");
  });

  it("omits indicators the v0.4.0 firmware cannot report", () => {
    const indicators = evaluateHealth({ status: status({ minLoopStackFreeBytes: null, largestFreeBlockBytes: null, resetReason: null }), previous: null, httpError: null, socketState: "open" });
    expect(indicators.map(i => i.id)).not.toContain("stack");
    expect(indicators.map(i => i.id)).not.toContain("reset");
  });
});

describe("evidence", () => {
  it("redacts credential-like keys at any depth", () => {
    expect(redact({ a: 1, token: "x", nested: [{ Authorization: "Bearer y", ok: true }] })).toEqual({ a: 1, token: "[redactado]", nested: [{ Authorization: "[redactado]", ok: true }] });
  });

  it("labels demo evidence so it cannot pass for a measurement", () => {
    const evidence = buildEvidence({ appVersion: "0.1.0", demo: true, wsUrl: "demo://fixtures", generatedAt: new Date(0), realtimeStatus: null, deviceState: null, gatewayStatus: null, health: [], counters: {}, recentFrames: [], recentEvents: [] });
    expect(String(evidence.dataOrigin)).toMatch(/^DEMO/);
    expect(evidence.generatedAt).toBe("1970-01-01T00:00:00.000Z");
  });
});

describe("demo replay", () => {
  it("produces only contract-valid frames", () => {
    const types = new Set<string>();
    for (let tick = 0; tick < 40; tick += 1) for (const message of demoFrames(tick, new Date(1795277402100 + tick * 1000))) types.add(message.type);
    expect([...types].sort()).toEqual(["connection.heartbeat", "connection.hello", "event", "rf.telemetry", "system.status"]);
  });

  it("is clearly marked through its status bootId", () => {
    expect(demoStatus(0).bootId).toContain("demo");
  });
});
