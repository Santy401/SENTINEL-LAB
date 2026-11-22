import type { MotionPoint } from "./store";
import { Ema, findPeaks, forceNewtons, sampleRateHz, seriesStats, shakeIntensity, tiltFromGravity, toG, type SeriesStats } from "./physics";

export interface MotionAnalysis {
  count: number;
  deviceId: string | null;
  /** |linear acceleration| per sample (m/s²), raw and EMA-filtered. */
  raw: number[];
  filtered: number[];
  axes: { x: number[]; y: number[]; z: number[] };
  stats: SeriesStats | null;
  rateHz: number | null;
  peaks: number[];
  /** Latest values (filtered when requested). */
  currentMs2: number | null;
  currentG: number | null;
  intensity: number;
  forceN: number | null;
  peakMs2: number | null;
  tilt: { pitchDeg: number; rollDeg: number } | null;
  ageMs: number | null;
}

export const EMA_ALPHA = 0.35;
export const PEAK_THRESHOLD_MS2 = 2.5;

export function analyzeMotion(samples: readonly MotionPoint[], options: { filtered: boolean; now: number; windowMs?: number }): MotionAnalysis {
  const windowMs = options.windowMs ?? 60000;
  const recent = samples.filter(s => options.now - s.at <= windowMs);
  const ema = new Ema(EMA_ALPHA);
  const raw = recent.map(s => s.linearMs2);
  const filtered = raw.map(v => ema.push(v));
  const series = options.filtered ? filtered : raw;
  const stats = seriesStats(series);
  const last = recent.at(-1) ?? null;
  const current = series.at(-1) ?? null;
  return {
    count: recent.length,
    deviceId: last?.deviceId ?? null,
    raw,
    filtered,
    axes: { x: recent.map(s => s.linear.x), y: recent.map(s => s.linear.y), z: recent.map(s => s.linear.z) },
    stats,
    rateHz: sampleRateHz(recent.map(s => s.at)),
    peaks: findPeaks(raw, PEAK_THRESHOLD_MS2),
    currentMs2: current,
    currentG: current === null ? null : toG(current),
    intensity: current === null ? 0 : shakeIntensity(current),
    forceN: current === null ? null : forceNewtons(current),
    peakMs2: raw.length ? Math.max(...raw) : null,
    tilt: last ? tiltFromGravity(last.gravity) : null,
    ageMs: last ? Math.max(0, options.now - last.at) : null,
  };
}
