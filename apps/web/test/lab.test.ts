import { readFileSync } from "node:fs";
import { join } from "node:path";
import { describe, expect, it } from "vitest";
import { parseLab, parsePhoneMotion, parseRealtimeStatus, parseRgbStatus } from "@/lib/api";
import { demoFrames, demoStatus } from "@/lib/demo";
import { clampStep, TOUR } from "@/lib/exhibition";
import { appendRows, columnStats, EXPERIMENTS, MAX_SESSION_ROWS, rgbLatencies, sampleSession, sessionToCsv, sessionToJson, startSession, stopSession } from "@/lib/experiments";
import { analyzeMotion } from "@/lib/motion-analysis";
import { parseServerFrame } from "@/lib/protocol";
import { SentinelStore, type MotionPoint, type Snapshot } from "@/lib/store";
import { examplesDir } from "./paths";

const example = JSON.parse(readFileSync(join(examplesDir, "valid", "motion.sample.json"), "utf8")) as Record<string, unknown>;

/** The firmware 0.5.0 relay: same payload, firmware envelope, CoreMotion source. */
function relayed(overrides: Record<string, unknown> = {}): Record<string, unknown> {
  return {
    ...example,
    source: "esp32-s3-realtime",
    sessionId: "boot1234-s7",
    payload: { ...(example.payload as object), deviceId: "iphone-1a2b3c4d", sourceType: "CoreMotion", ...overrides },
  };
}

describe("motion.sample relay", () => {
  it("accepts the shared example and the relayed CoreMotion form", () => {
    expect(parseServerFrame(JSON.stringify(example)).ok).toBe(true);
    const result = parseServerFrame(JSON.stringify(relayed()));
    expect(result.ok).toBe(true);
    if (result.ok && result.message.type === "motion.sample") {
      expect(result.message.payload.linearAcceleration.x).toBeCloseTo(0.29);
    }
  });

  it("rejects simulated samples, unknown fields and out-of-range vectors", () => {
    expect(parseServerFrame(JSON.stringify(relayed({ simulated: true }))).ok).toBe(false);
    expect(parseServerFrame(JSON.stringify(relayed({ extra: 1 }))).ok).toBe(false);
    expect(parseServerFrame(JSON.stringify(relayed({ linearAcceleration: { x: 900, y: 0, z: 0 } }))).ok).toBe(false);
    expect(parseServerFrame(JSON.stringify(relayed({ sourceType: "Accelerometer" }))).ok).toBe(false);
  });

  it("produces valid DEMO motion frames at 5 per tick", () => {
    const frames = demoFrames(3, new Date("2026-11-22T10:00:00.000Z"));
    expect(frames.filter(f => f.type === "motion.sample")).toHaveLength(5);
  });
});

describe("status extensions (firmware ≥ 0.5.0, lenient)", () => {
  const base = { ...demoStatus(0), rgb: undefined, phoneMotion: undefined } as Record<string, unknown>;

  it("parses rgb and phoneMotion when present", () => {
    const status = parseRealtimeStatus({
      ...base,
      rgb: { enabled: true, pin: 48, mode: "motion", r: 255, g: 100, b: 0 },
      phoneMotion: { active: true, deviceId: "iphone-1", linearAccelMs2: 3.2, intensity: 1.7, peakMs2: 9, samples: 40, sampleAgeMs: 150 },
    });
    expect(status.rgb).toEqual({ enabled: true, pin: 48, mode: "motion", r: 255, g: 100, b: 0 });
    expect(status.phoneMotion?.intensity).toBe(1);
  });

  it("tolerates older firmware and malformed extensions", () => {
    const status = parseRealtimeStatus(base);
    expect(status.rgb).toBeNull();
    expect(status.phoneMotion).toBeNull();
    expect(parseRgbStatus({ enabled: true, r: 300, g: 0, b: 0 })).toBeNull();
    expect(parseRgbStatus({ enabled: true, r: 1, g: 2, b: 3, mode: "party" })?.mode).toBeNull();
    expect(parsePhoneMotion({ deviceId: "x" })).toBeNull();
  });

  it("parses the lab API with firmware field names", () => {
    const lab = parseLab({
      experiments: [{ id: 1, name: "Pasillo", clientId: 7, environment: 1, calibrated: true, n: 2.6, referenceRssi: -42, currentEstimateM: 3.1 }],
      measurements: [{ id: 1, experimentId: 1, clientId: 7, actualM: 2, rssiDbm: -50, filteredDbm: -50.5, calibration: true, estimatedM: 2.1, errorM: 0.1 }],
    });
    expect(lab.experiments[0]?.calibrated).toBe(true);
    expect(lab.measurements[0]).toMatchObject({ rssi: -50, filtered: -50.5, errorM: 0.1 });
  });
});

function point(at: number, linear: number): MotionPoint {
  return { at, deviceId: "iphone", sampleIndex: at, linear: { x: linear, y: 0, z: 0 }, gravity: { x: 0, y: 0, z: -9.81 }, linearMs2: Math.abs(linear), state: "MOVEMENT" };
}

describe("motion analysis", () => {
  it("reports current, peak, force, rate and tilt", () => {
    const samples = [0, 0.2, 5, 0.1, 6, 0.3].map((v, i) => point(1000 + i * 200, v));
    const a = analyzeMotion(samples, { filtered: false, now: 2100 });
    expect(a.count).toBe(6);
    expect(a.currentMs2).toBeCloseTo(0.3);
    expect(a.peakMs2).toBe(6);
    expect(a.peaks).toEqual([2, 4]);
    expect(a.rateHz).toBeCloseTo(5);
    expect(a.forceN).toBeCloseTo(0.164 * 0.3);
    expect(a.tilt?.pitchDeg).toBeCloseTo(0);
    const f = analyzeMotion(samples, { filtered: true, now: 2100 });
    expect(f.filtered).toHaveLength(6);
    expect(f.currentMs2).not.toBe(a.currentMs2);
  });

  it("is empty without samples", () => {
    const a = analyzeMotion([], { filtered: true, now: 0 });
    expect(a.currentMs2).toBeNull();
    expect(a.intensity).toBe(0);
  });
});

describe("experiments", () => {
  const snapshot = (over: Partial<Snapshot>): Snapshot => ({ ...new SentinelStore({ refreshMs: 3000, demo: false }).getSnapshot(), ...over });

  it("defines the five sessions with honesty notes", () => {
    expect(EXPERIMENTS.map(e => e.id)).toEqual(["gravity", "motion", "coverage", "rgb", "stability"]);
    for (const e of EXPERIMENTS) expect(e.honesty.length).toBeGreaterThan(20);
  });

  it("records only new motion rows and exports CSV/JSON", () => {
    let record = startSession("motion", snapshot({}), 1000);
    const s1 = snapshot({ motionSamples: [point(900, 9), point(1100, 1), point(1300, 2)] });
    record = appendRows(record, sampleSession(record, s1, 1400));
    expect(record.rows).toHaveLength(2);
    expect(sampleSession(record, s1, 1500)).toHaveLength(0);
    record = stopSession(record, 2000);
    expect(columnStats(record, "aMag")?.max).toBe(2);
    const csv = sessionToCsv(record);
    expect(csv.split("\r\n")[0]).toBe("iso_time,t_ms,demo,ax,ay,az,aMag,aG,state");
    expect(csv).toContain(",100,0,1,0,0,1,");
    const json = JSON.parse(sessionToJson(record));
    expect(json.demo).toBe(false);
    expect(json.experiment.id).toBe("motion");
    expect(json.rows).toHaveLength(2);
  });

  it("derives tilt rows for the gravity experiment and caps rows", () => {
    const record = startSession("gravity", snapshot({}), 0);
    const rows = sampleSession(record, snapshot({ motionSamples: [point(10, 0)] }), 20);
    expect(rows[0]?.values).toMatchObject({ gMag: 9.81, pitchDeg: 0 });
    const big = appendRows(record, Array.from({ length: MAX_SESSION_ROWS + 5 }, (_, i) => ({ t: i + 1, values: {} })));
    expect(big.rows).toHaveLength(MAX_SESSION_ROWS);
  });

  it("pairs shake onsets with the next LED change into motion mode", () => {
    const motion = [{ at: 0, linearMs2: 0.1 }, { at: 1000, linearMs2: 4 }, { at: 1200, linearMs2: 5 }, { at: 5000, linearMs2: 0.2 }, { at: 9000, linearMs2: 6 }];
    const rgb = [{ at: 2500, mode: "motion" }, { at: 6000, mode: "synced" }, { at: 11000, mode: "motion" }];
    expect(rgbLatencies(motion, rgb)).toEqual([
      { motionAt: 1000, ledAt: 2500, latencyMs: 1500 },
      { motionAt: 9000, ledAt: 11000, latencyMs: 2000 },
    ]);
    expect(rgbLatencies(motion, [])).toEqual([]);
  });

  it("escapes CSV cells", () => {
    const record = { experiment: "rgb" as const, startedAt: 0, stoppedAt: 1, demo: true, bootId: null, rows: [{ t: 0, values: { mode: 'a,"b"' } }] };
    expect(sessionToCsv(record)).toContain('"a,""b"""');
  });
});

describe("exhibition tour", () => {
  it("covers problem, hardware, radar, motion, rgb and evolution", () => {
    expect(TOUR.length).toBeGreaterThanOrEqual(7);
    expect(TOUR.map(s => s.id)).toEqual(expect.arrayContaining(["problem", "radar", "motion", "rgb", "future"]));
    expect(TOUR.find(s => s.id === "radar")?.reality).toContain("estimación");
    expect(clampStep(-3)).toBe(0);
    expect(clampStep(99)).toBe(TOUR.length - 1);
  });
});
