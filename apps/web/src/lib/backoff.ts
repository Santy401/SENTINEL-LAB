// Exponential backoff with full jitter. The random source is injectable so the
// schedule is deterministic in tests.

export interface BackoffOptions {
  baseMs?: number;
  maxMs?: number;
  factor?: number;
  random?: () => number;
}

export class Backoff {
  private attempt = 0;
  private readonly baseMs: number;
  private readonly maxMs: number;
  private readonly factor: number;
  private readonly random: () => number;

  constructor({ baseMs = 1000, maxMs = 30000, factor = 2, random = Math.random }: BackoffOptions = {}) {
    if (baseMs <= 0 || maxMs < baseMs || factor < 1) throw new Error("invalid backoff options");
    this.baseMs = baseMs;
    this.maxMs = maxMs;
    this.factor = factor;
    this.random = random;
  }

  get attempts(): number {
    return this.attempt;
  }

  /** Upper bound of the delay for the given attempt (0-based), capped at maxMs. */
  ceiling(attempt: number): number {
    return Math.min(this.maxMs, this.baseMs * this.factor ** attempt);
  }

  /** Next delay in ms: uniform in [ceiling/2, ceiling] so retries never collapse to zero. */
  next(): number {
    const ceiling = this.ceiling(this.attempt);
    this.attempt += 1;
    const r = Math.min(Math.max(this.random(), 0), 1);
    return Math.round(ceiling / 2 + (ceiling / 2) * r);
  }

  reset(): void {
    this.attempt = 0;
  }
}
