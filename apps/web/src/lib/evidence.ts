import type { HealthIndicator } from "./health";

const SENSITIVE_KEY = /token|password|passphrase|secret|authorization|bearer|psk|cookie|credential|apikey|api_key/i;

/** Deep copy without any field whose name suggests a credential. */
export function redact<T>(value: T): T {
  if (Array.isArray(value)) return value.map(item => redact(item)) as unknown as T;
  if (value && typeof value === "object") {
    const out: Record<string, unknown> = {};
    for (const [key, item] of Object.entries(value as Record<string, unknown>)) {
      out[key] = SENSITIVE_KEY.test(key) ? "[redactado]" : redact(item);
    }
    return out as T;
  }
  return value;
}

export interface EvidenceInput {
  appVersion: string;
  demo: boolean;
  wsUrl: string;
  generatedAt: Date;
  realtimeStatus: unknown;
  deviceState: unknown;
  gatewayStatus: unknown;
  health: HealthIndicator[];
  counters: Record<string, number>;
  recentFrames: unknown[];
  recentEvents: unknown[];
}

export function buildEvidence(input: EvidenceInput): Record<string, unknown> {
  return redact({
    kind: "sentinel-lab.web-evidence",
    schemaVersion: 1,
    generatedAt: input.generatedAt.toISOString(),
    appVersion: input.appVersion,
    // Demo data must never be mistaken for a hardware measurement.
    dataOrigin: input.demo ? "DEMO (reproducción de fixtures, no es una medición)" : "dispositivo",
    wsUrl: input.wsUrl,
    realtimeStatus: input.realtimeStatus,
    deviceState: input.deviceState,
    gatewayStatus: input.gatewayStatus,
    health: input.health,
    counters: input.counters,
    recentFrames: input.recentFrames,
    recentEvents: input.recentEvents,
  });
}
