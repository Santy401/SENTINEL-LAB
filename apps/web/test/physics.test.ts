import { describe, expect, it } from "vitest";
import {
  DEFAULT_RSSI_AT_1M,
  Ema,
  IPHONE_12_MASS_KG,
  STANDARD_GRAVITY,
  distanceFromRssi,
  findPeaks,
  forceNewtons,
  magnitude,
  radarRadius,
  rssiAtDistance,
  sampleRateHz,
  seriesStats,
  shakeIntensity,
  shakeLabel,
  signalBand,
  stableAngle,
  stdDev,
  theoreticalRangeM,
  tiltFromGravity,
  toG,
} from "@/lib/physics";
import { radarPoint } from "@/views/Radar";
import { chartRange, tracePath } from "@/ui/TraceChart";
import { ledHex, ledLevel, boardPart, BOARD_PARTS } from "@/lib/board-parts";

describe("motion physics", () => {
  it("computes magnitude, g and F = m·a", () => {
    expect(magnitude({ x: 3, y: 4, z: 12 })).toBe(13);
    expect(toG(STANDARD_GRAVITY)).toBeCloseTo(1, 10);
    expect(forceNewtons(10)).toBeCloseTo(IPHONE_12_MASS_KG * 10, 10);
    expect(forceNewtons(2, 1)).toBe(2);
  });

  it("maps shake intensity to 0..1 with honest labels", () => {
    expect(shakeIntensity(0)).toBe(0);
    expect(shakeIntensity(-1)).toBe(0);
    expect(shakeIntensity(Number.NaN)).toBe(0);
    expect(shakeIntensity(4)).toBeCloseTo(0.5);
    expect(shakeIntensity(80)).toBe(1);
    expect(shakeLabel(0)).toBe("En reposo");
    expect(shakeLabel(0.95)).toBe("Agitación muy fuerte");
  });

  it("derives pitch and roll from gravity (yaw not observable)", () => {
    const flat = tiltFromGravity({ x: 0, y: 0, z: -9.81 })!;
    expect(flat.pitchDeg).toBeCloseTo(0, 6);
    expect(Math.abs(flat.rollDeg)).toBeCloseTo(0, 6);
    const upright = tiltFromGravity({ x: 0, y: -9.81, z: 0 })!;
    expect(upright.pitchDeg).toBeCloseTo(90, 6);
    const side = tiltFromGravity({ x: 9.81, y: 0, z: 0 })!;
    expect(side.rollDeg).toBeCloseTo(90, 6);
    expect(tiltFromGravity({ x: 0, y: 0, z: 0 })).toBeNull();
  });

  it("filters with an EMA and rejects invalid alpha", () => {
    const ema = new Ema(0.5);
    expect(ema.push(10)).toBe(10);
    expect(ema.push(0)).toBe(5);
    expect(ema.current).toBe(5);
    expect(() => new Ema(0)).toThrow();
  });

  it("summarises series, rates and peaks", () => {
    expect(seriesStats([1, 2, 3, Number.NaN])).toEqual({ count: 3, min: 1, max: 3, mean: 2, last: 3 });
    expect(seriesStats([])).toBeNull();
    expect(sampleRateHz([0, 200, 400, 600, 800])).toBeCloseTo(5);
    expect(sampleRateHz([5])).toBeNull();
    expect(findPeaks([0, 3, 0, 0, 4, 0, 5, 0], 2.5, 3)).toEqual([1, 6]);
    expect(stdDev([1, 1, 1])).toBe(0);
    expect(stdDev([1])).toBeNull();
  });
});

describe("radio model", () => {
  it("inverts the log-distance model", () => {
    for (const n of [2, 2.7, 3.3]) {
      for (const d of [0.5, 1, 4, 20]) {
        expect(distanceFromRssi(rssiAtDistance(d, n), n)).toBeCloseTo(d, 6);
      }
    }
    expect(rssiAtDistance(1, 2)).toBe(DEFAULT_RSSI_AT_1M);
    expect(distanceFromRssi(0, 2)).toBeNull();
    expect(distanceFromRssi(-60, 0)).toBeNull();
  });

  it("gives a theoretical range at the sensitivity floor", () => {
    // n = 2: 10^((−40 − (−90)) / 20) = 316.2 m
    expect(theoreticalRangeM(2)).toBeCloseTo(316.23, 1);
    expect(theoreticalRangeM(4)).toBeLessThan(theoreticalRangeM(2));
  });

  it("classifies RSSI bands and radar radius", () => {
    expect(signalBand(-50)).toBe("excelente");
    expect(signalBand(-70)).toBe("aceptable");
    expect(signalBand(-95)).toBe("límite");
    expect(radarRadius(-30)).toBe(0.08);
    expect(radarRadius(-95)).toBe(1);
    expect(radarRadius(-200)).toBe(1);
    expect(radarRadius(Number.NaN)).toBe(1);
    expect(radarRadius(-62.5)).toBeCloseTo(0.5);
  });

  it("assigns a stable presentation angle per client id", () => {
    const a = stableAngle(1234);
    expect(stableAngle("1234")).toBe(a);
    expect(a).toBeGreaterThanOrEqual(0);
    expect(a).toBeLessThan(Math.PI * 2);
    expect(stableAngle(1235)).not.toBe(a);
    const p = radarPoint(0, 0.5);
    expect(p.x).toBeCloseTo(50);
    expect(p.y).toBeCloseTo(0);
  });
});

describe("charts and board metadata", () => {
  it("computes ranges and SVG paths with gaps", () => {
    expect(chartRange([{ id: "a", label: "a", color: "#000", values: [0, 10] }])).toEqual([-1, 11]);
    expect(chartRange([], 0, 5)).toEqual([0, 5]);
    expect(tracePath([0, null, 10], 100, 10, 0, 10)).toBe("M0.0,10.0M100.0,0.0");
    expect(tracePath([], 100, 10, 0, 1)).toBe("");
  });

  it("describes every board part and LED colours", () => {
    expect(BOARD_PARTS.map(p => p.id)).toContain("rgb");
    expect(boardPart("rgb")?.name).toContain("GPIO48");
    expect(boardPart("nope")).toBeNull();
    expect(ledHex({ r: 255, g: 8, b: 0 })).toBe("#ff0800");
    expect(ledHex(null)).toBe("#2a2f36");
    expect(ledLevel({ r: 255, g: 255, b: 255 })).toBeCloseTo(1);
    expect(ledLevel(null)).toBe(0);
  });
});
