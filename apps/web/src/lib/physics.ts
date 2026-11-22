// Physics and radio models used by the lab views. Pure functions only, so every
// number shown in the UI can be traced to a tested formula.

import type { Vector3 } from "./protocol";

/** Standard gravity (m/s²), used to express accelerations in g. */
export const STANDARD_GRAVITY = 9.80665;
/** iPhone 12 mass (Apple spec, 164 g). F = m·a is an estimate built on it. */
export const IPHONE_12_MASS_KG = 0.164;
/** |linear acceleration| that maps to full shake intensity (≈0.8 g). */
export const SHAKE_FULL_SCALE_MS2 = 8;

export function magnitude(v: Vector3): number {
  return Math.hypot(v.x, v.y, v.z);
}

export function toG(ms2: number): number {
  return ms2 / STANDARD_GRAVITY;
}

/** Derived force F = m·a in newtons for a body of the given mass. */
export function forceNewtons(accelerationMs2: number, massKg = IPHONE_12_MASS_KG): number {
  return massKg * accelerationMs2;
}

/** 0..1 intensity of a shake from |linear acceleration|. */
export function shakeIntensity(linearMs2: number, fullScale = SHAKE_FULL_SCALE_MS2): number {
  if (!Number.isFinite(linearMs2) || linearMs2 <= 0) return 0;
  return Math.min(1, linearMs2 / fullScale);
}

export function shakeLabel(intensity: number): string {
  if (intensity < 0.04) return "En reposo";
  if (intensity < 0.2) return "Movimiento suave";
  if (intensity < 0.5) return "Movimiento moderado";
  if (intensity < 0.85) return "Agitación fuerte";
  return "Agitación muy fuerte";
}

/**
 * Pitch and roll (degrees) from the gravity vector in the device frame.
 * Yaw (heading) is not observable from gravity alone.
 */
export function tiltFromGravity(g: Vector3): { pitchDeg: number; rollDeg: number } | null {
  const m = magnitude(g);
  if (!Number.isFinite(m) || m < 1e-6) return null;
  const pitch = Math.atan2(-g.y, Math.hypot(g.x, g.z));
  const roll = Math.atan2(g.x, -g.z);
  // "+ 0" turns −0 into 0 so a level phone never reads "-0°".
  return { pitchDeg: (pitch * 180) / Math.PI + 0, rollDeg: (roll * 180) / Math.PI + 0 };
}

/** Exponential moving average; alpha in (0, 1]. */
export class Ema {
  private value: number | null = null;
  constructor(private readonly alpha: number) {
    if (!(alpha > 0 && alpha <= 1)) throw new RangeError("alpha must be in (0, 1]");
  }
  push(x: number): number {
    this.value = this.value === null ? x : this.alpha * x + (1 - this.alpha) * this.value;
    return this.value;
  }
  get current(): number | null {
    return this.value;
  }
  reset(): void {
    this.value = null;
  }
}

export interface SeriesStats {
  count: number;
  min: number;
  max: number;
  mean: number;
  last: number;
}

export function seriesStats(values: readonly number[]): SeriesStats | null {
  const finite = values.filter(Number.isFinite);
  if (finite.length === 0) return null;
  let min = Infinity;
  let max = -Infinity;
  let sum = 0;
  for (const v of finite) {
    if (v < min) min = v;
    if (v > max) max = v;
    sum += v;
  }
  return { count: finite.length, min, max, mean: sum / finite.length, last: finite[finite.length - 1]! };
}

/** Samples per second over the given arrival times (ms), or null with < 2 samples. */
export function sampleRateHz(times: readonly number[]): number | null {
  if (times.length < 2) return null;
  const span = times[times.length - 1]! - times[0]!;
  return span > 0 ? ((times.length - 1) * 1000) / span : null;
}

/** Local maxima above a threshold, separated by at least minGap samples. */
export function findPeaks(values: readonly number[], threshold: number, minGap = 2): number[] {
  const peaks: number[] = [];
  for (let i = 1; i < values.length - 1; i += 1) {
    const v = values[i]!;
    if (v < threshold || v < values[i - 1]! || v < values[i + 1]!) continue;
    const last = peaks[peaks.length - 1];
    if (last !== undefined && i - last < minGap) {
      if (v > values[last]!) peaks[peaks.length - 1] = i;
      continue;
    }
    peaks.push(i);
  }
  return peaks;
}

// ------------------------------------------------------------------ radio

/** Typical received power at 1 m from the ESP32 soft-AP (conceptual default). */
export const DEFAULT_RSSI_AT_1M = -40;
/** Practical receive floor for a usable 802.11 link (conceptual). */
export const DEFAULT_SENSITIVITY_DBM = -90;

export const PATH_LOSS_PRESETS = [
  { id: "free", label: "Espacio libre (n = 2.0)", n: 2.0 },
  { id: "open", label: "Interior abierto (n = 2.7)", n: 2.7 },
  { id: "office", label: "Oficina / muros (n = 3.3)", n: 3.3 },
  { id: "dense", label: "Interior denso (n = 4.0)", n: 4.0 },
] as const;

/** Log-distance model: RSSI(d) = A − 10·n·log10(d / 1 m). */
export function rssiAtDistance(distanceM: number, n: number, rssiAt1m = DEFAULT_RSSI_AT_1M): number {
  return rssiAt1m - 10 * n * Math.log10(Math.max(distanceM, 1e-3));
}

/** Inverse of the log-distance model. An estimate, never a measurement. */
export function distanceFromRssi(rssiDbm: number, n: number, rssiAt1m = DEFAULT_RSSI_AT_1M): number | null {
  if (!Number.isFinite(rssiDbm) || rssiDbm >= 0 || !(n > 0)) return null;
  return 10 ** ((rssiAt1m - rssiDbm) / (10 * n));
}

/** Theoretical maximum range where the model reaches the sensitivity floor. */
export function theoreticalRangeM(n: number, rssiAt1m = DEFAULT_RSSI_AT_1M, sensitivity = DEFAULT_SENSITIVITY_DBM): number {
  return distanceFromRssi(sensitivity, n, rssiAt1m) ?? 0;
}

export type SignalBand = "excelente" | "buena" | "aceptable" | "débil" | "límite";

export function signalBand(rssiDbm: number): SignalBand {
  if (rssiDbm >= -55) return "excelente";
  if (rssiDbm >= -65) return "buena";
  if (rssiDbm >= -75) return "aceptable";
  if (rssiDbm >= -85) return "débil";
  return "límite";
}

/** Radar radius (0..1) from RSSI: −30 dBm at the centre ring, −95 dBm at the edge. */
export function radarRadius(rssiDbm: number): number {
  if (!Number.isFinite(rssiDbm)) return 1;
  const t = (-30 - rssiDbm) / 65;
  return Math.min(1, Math.max(0.08, t));
}

/** Stable pseudo-angle (radians) from a client id: presentation only, not measured. */
export function stableAngle(id: string | number): number {
  const text = String(id);
  let h = 2166136261;
  for (let i = 0; i < text.length; i += 1) {
    h ^= text.charCodeAt(i);
    h = Math.imul(h, 16777619);
  }
  return ((h >>> 0) / 4294967296) * Math.PI * 2;
}

/** Standard deviation of RSSI samples: a simple link-stability indicator. */
export function stdDev(values: readonly number[]): number | null {
  const s = seriesStats(values);
  if (!s || s.count < 2) return null;
  let acc = 0;
  for (const v of values) if (Number.isFinite(v)) acc += (v - s.mean) ** 2;
  return Math.sqrt(acc / (s.count - 1));
}
