// HTTP models for the firmware v0.3.1 API and the additive realtime health
// route, plus the optional gateway endpoints. Parsers reject responses that
// lack required fields rather than filling in invented values.

type Obj = Record<string, unknown>;

const isObj = (v: unknown): v is Obj => typeof v === "object" && v !== null && !Array.isArray(v);
const numOr = (v: unknown): number | null => (typeof v === "number" && Number.isFinite(v) ? v : null);
const strOr = (v: unknown): string | null => (typeof v === "string" ? v : null);
const boolOr = (v: unknown): boolean | null => (typeof v === "boolean" ? v : null);

export class ApiShapeError extends Error {}

function need<T>(value: T | null, field: string): T {
  if (value === null) throw new ApiShapeError(`missing or invalid field ${field}`);
  return value;
}

export type ClockSource = "none" | "ntp" | "peer";

export type RgbMode = "boot" | "offline" | "client" | "synced" | "motion" | "event";
export const RGB_MODES: readonly RgbMode[] = ["boot", "offline", "client", "synced", "motion", "event"];

/** Physical WS2812 state reported by firmware ≥ 0.5.0 (absent before). */
export interface RgbStatus {
  enabled: boolean;
  pin: number | null;
  mode: RgbMode | null;
  r: number;
  g: number;
  b: number;
}

/** iPhone motion as last accepted by the firmware (≥ 0.5.0). */
export interface PhoneMotionStatus {
  active: boolean;
  deviceId: string | null;
  linearAccelMs2: number | null;
  intensity: number | null;
  peakMs2: number | null;
  samples: number | null;
  sampleAgeMs: number | null;
}

const byte = (v: unknown): number | null => {
  const n = numOr(v);
  return n !== null && Number.isInteger(n) && n >= 0 && n <= 255 ? n : null;
};

export function parseRgbStatus(value: unknown): RgbStatus | null {
  if (!isObj(value)) return null;
  const r = byte(value.r);
  const g = byte(value.g);
  const b = byte(value.b);
  const enabled = boolOr(value.enabled);
  if (r === null || g === null || b === null || enabled === null) return null;
  const mode = strOr(value.mode);
  return {
    enabled,
    pin: numOr(value.pin),
    mode: mode && (RGB_MODES as readonly string[]).includes(mode) ? (mode as RgbMode) : null,
    r,
    g,
    b,
  };
}

export function parsePhoneMotion(value: unknown): PhoneMotionStatus | null {
  if (!isObj(value)) return null;
  const active = boolOr(value.active);
  if (active === null) return null;
  const intensity = numOr(value.intensity);
  return {
    active,
    deviceId: strOr(value.deviceId),
    linearAccelMs2: numOr(value.linearAccelMs2),
    intensity: intensity === null ? null : Math.min(1, Math.max(0, intensity)),
    peakMs2: numOr(value.peakMs2),
    samples: numOr(value.samples),
    sampleAgeMs: numOr(value.sampleAgeMs),
  };
}

export interface RealtimeStatus {
  fw: string;
  httpApiVersion: string | null;
  firmwareVersion: string | null;
  realtimeVersion: string;
  bootId: string;
  resetReason: string | null;
  uptimeMs: number;
  clockSynced: boolean;
  clockSource: ClockSource | null;
  clockSyncAgeMs: number | null;
  clientCount: number;
  freeHeapBytes: number;
  minFreeHeapBytes: number;
  largestFreeBlockBytes: number | null;
  minLoopStackFreeBytes: number | null;
  heapSamples: number;
  droppedFrames: number;
  rejectedMessages: number;
  droppedTelemetry: number;
  skippedSimulatedEvents: number;
  handshakeTimeouts: number | null;
  idleTimeouts: number | null;
  remoteCloses: number | null;
  rgb: RgbStatus | null;
  phoneMotion: PhoneMotionStatus | null;
}

export function parseRealtimeStatus(value: unknown): RealtimeStatus {
  if (!isObj(value)) throw new ApiShapeError("expected object");
  const source = strOr(value.clockSource);
  return {
    fw: need(strOr(value.fw), "fw"),
    httpApiVersion: strOr(value.httpApiVersion),
    firmwareVersion: strOr(value.firmwareVersion),
    realtimeVersion: need(strOr(value.realtimeVersion), "realtimeVersion"),
    bootId: need(strOr(value.bootId), "bootId"),
    resetReason: strOr(value.resetReason),
    uptimeMs: need(numOr(value.uptimeMs), "uptimeMs"),
    clockSynced: need(boolOr(value.clockSynced), "clockSynced"),
    clockSource: source === "none" || source === "ntp" || source === "peer" ? source : null,
    clockSyncAgeMs: numOr(value.clockSyncAgeMs),
    clientCount: need(numOr(value.clientCount), "clientCount"),
    freeHeapBytes: need(numOr(value.freeHeapBytes), "freeHeapBytes"),
    minFreeHeapBytes: need(numOr(value.minFreeHeapBytes), "minFreeHeapBytes"),
    largestFreeBlockBytes: numOr(value.largestFreeBlockBytes),
    minLoopStackFreeBytes: numOr(value.minLoopStackFreeBytes),
    heapSamples: need(numOr(value.heapSamples), "heapSamples"),
    droppedFrames: need(numOr(value.droppedFrames), "droppedFrames"),
    rejectedMessages: need(numOr(value.rejectedMessages), "rejectedMessages"),
    droppedTelemetry: need(numOr(value.droppedTelemetry), "droppedTelemetry"),
    skippedSimulatedEvents: need(numOr(value.skippedSimulatedEvents), "skippedSimulatedEvents"),
    handshakeTimeouts: numOr(value.handshakeTimeouts),
    idleTimeouts: numOr(value.idleTimeouts),
    remoteCloses: numOr(value.remoteCloses),
    rgb: parseRgbStatus(value.rgb),
    phoneMotion: parsePhoneMotion(value.phoneMotion),
  };
}

export interface DeviceState {
  fw: string;
  uptimeMs: number;
  motionState: string;
  fresh: boolean | null;
  accelSource: string;
  simulation: boolean | null;
  wifi: { mode: string | null; ssid: string | null; ip: string | null; clients: number | null };
  sampleCount: number | null;
}

/** GET /api/state (HTTP API v0.3.1). */
export function parseDeviceState(value: unknown): DeviceState {
  if (!isObj(value)) throw new ApiShapeError("expected object");
  const wifi = isObj(value.wifi) ? value.wifi : {};
  return {
    fw: need(strOr(value.fw), "fw"),
    uptimeMs: need(numOr(value.uptimeMs), "uptimeMs"),
    motionState: need(strOr(value.state), "state"),
    fresh: boolOr(value.fresh),
    accelSource: strOr(value.source) ?? "None",
    simulation: boolOr(value.simulation),
    wifi: { mode: strOr(wifi.mode), ssid: strOr(wifi.ssid), ip: strOr(wifi.ip), clients: numOr(wifi.clients) },
    sampleCount: numOr(value.sampleCount),
  };
}

export interface RfHistoryPoint {
  t: number;
  rssi: number;
}

export interface RfDevice {
  id: number;
  connected: boolean;
  lastSeenMs: number;
  rssiDbm: number | null;
  filteredDbm: number | null;
  history: RfHistoryPoint[];
}

export interface RfSnapshot {
  bootId: string;
  uptimeMs: number;
  mode: string | null;
  available: boolean;
  ssid: string | null;
  ip: string | null;
  clients: number;
  channel: number | null;
  frequencyMHz: number | null;
  signalWeak: boolean;
  devices: RfDevice[];
}

/** GET /api/rf (HTTP API v0.3.1). Times are firmware uptime, not UTC. */
export function parseRfSnapshot(value: unknown): RfSnapshot {
  if (!isObj(value)) throw new ApiShapeError("expected object");
  const devices = Array.isArray(value.devices) ? value.devices : [];
  return {
    bootId: need(strOr(value.bootId), "bootId"),
    uptimeMs: need(numOr(value.uptimeMs), "uptimeMs"),
    mode: strOr(value.mode),
    available: need(boolOr(value.available), "available"),
    ssid: strOr(value.ssid),
    ip: strOr(value.ip),
    clients: numOr(value.clients) ?? 0,
    channel: numOr(value.channel) || null,
    frequencyMHz: numOr(value.frequencyMHz),
    signalWeak: boolOr(value.signalWeak) ?? false,
    devices: devices.filter(isObj).map(d => ({
      id: need(numOr(d.id), "devices[].id"),
      connected: boolOr(d.connected) ?? false,
      lastSeenMs: numOr(d.lastSeenMs) ?? 0,
      rssiDbm: numOr(d.rssiDbm),
      filteredDbm: numOr(d.filteredDbm),
      history: (Array.isArray(d.history) ? d.history : [])
        .filter(isObj)
        .map(h => ({ t: numOr(h.t), rssi: numOr(h.rssi) }))
        .filter((h): h is RfHistoryPoint => h.t !== null && h.rssi !== null),
    })),
  };
}

export interface FirmwareEvent {
  sequence: number;
  timestampMs: number;
  event: string;
  clientId: number;
  simulated: boolean;
  rssiDbm: number | null;
}

export interface EventsSnapshot {
  bootId: string;
  notificationsEnabled: boolean;
  events: FirmwareEvent[];
}

/** GET /api/events (HTTP API v0.3.1). */
export function parseEvents(value: unknown): EventsSnapshot {
  if (!isObj(value)) throw new ApiShapeError("expected object");
  const events = Array.isArray(value.events) ? value.events : [];
  return {
    bootId: need(strOr(value.bootId), "bootId"),
    notificationsEnabled: boolOr(value.notificationsEnabled) ?? false,
    events: events.filter(isObj).map(e => ({
      sequence: need(numOr(e.sequence), "events[].sequence"),
      timestampMs: numOr(e.timestamp_ms) ?? 0,
      event: strOr(e.event) ?? "UNKNOWN",
      clientId: numOr(e.client_id) ?? 0,
      simulated: boolOr(e.simulated) ?? false,
      rssiDbm: numOr(e.rssi_dbm),
    })),
  };
}

export interface GatewayConfig {
  deviceHttp: string;
  wsUrl: string;
  gatewayVersion: string;
}

export const DEFAULT_WS_URL = "ws://192.168.4.1:81/realtime";

export function parseGatewayConfig(value: unknown): GatewayConfig {
  if (!isObj(value)) throw new ApiShapeError("expected object");
  const wsUrl = need(strOr(value.wsUrl), "wsUrl");
  if (!/^wss?:\/\//.test(wsUrl)) throw new ApiShapeError("wsUrl must be ws:// or wss://");
  return {
    deviceHttp: strOr(value.deviceHttp) ?? "",
    wsUrl,
    gatewayVersion: strOr(value.gatewayVersion) ?? "desconocida",
  };
}

export interface GatewayStatus {
  state: "connected" | "connecting" | "disconnected";
  clockAuthority: boolean;
  lastSyncAt: string | null;
  lastError: string | null;
  reconnects: number;
  framesReceived: number;
  uptimeS: number;
}

export function parseGatewayStatus(value: unknown): GatewayStatus {
  if (!isObj(value)) throw new ApiShapeError("expected object");
  const state = strOr(value.state);
  if (state !== "connected" && state !== "connecting" && state !== "disconnected") throw new ApiShapeError("invalid state");
  return {
    state,
    clockAuthority: boolOr(value.clockAuthority) ?? false,
    lastSyncAt: strOr(value.lastSyncAt),
    lastError: strOr(value.lastError),
    reconnects: numOr(value.reconnects) ?? 0,
    framesReceived: numOr(value.framesReceived) ?? 0,
    uptimeS: numOr(value.uptimeS) ?? 0,
  };
}

export class HttpError extends Error {
  constructor(readonly status: number, message: string) {
    super(message);
  }
}

/** GET with a hard timeout. Throws HttpError on non-2xx, ApiShapeError on bad JSON. */
export async function getJson<T>(path: string, parse: (v: unknown) => T, options: { signal?: AbortSignal; timeoutMs?: number; fetchImpl?: typeof fetch } = {}): Promise<T> {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), options.timeoutMs ?? 4000);
  const onAbort = () => controller.abort();
  options.signal?.addEventListener("abort", onAbort, { once: true });
  try {
    const response = await (options.fetchImpl ?? fetch)(path, { signal: controller.signal, cache: "no-store", headers: { Accept: "application/json" } });
    if (!response.ok) throw new HttpError(response.status, `HTTP ${response.status}`);
    let body: unknown;
    try {
      body = await response.json();
    } catch {
      throw new ApiShapeError("response is not JSON");
    }
    return parse(body);
  } finally {
    clearTimeout(timer);
    options.signal?.removeEventListener("abort", onAbort);
  }
}

// ------------------------------------------------------------- lab API (POST)
// Write routes exist only on the ESP32 itself (the gateway proxy is read-only
// and answers 405). They are unauthenticated: laboratory AP only.

export interface LabExperiment {
  id: number;
  name: string;
  clientId: number;
  environment: number;
  calibrated: boolean;
  n: number | null;
  referenceRssi: number | null;
  currentEstimateM: number | null;
}

export interface LabMeasurement {
  experimentId: number;
  clientId: number;
  actualM: number | null;
  rssi: number | null;
  filtered: number | null;
  calibration: boolean;
  estimatedM: number | null;
  errorM: number | null;
}

export interface LabSnapshot {
  experiments: LabExperiment[];
  measurements: LabMeasurement[];
}

export function parseLab(value: unknown): LabSnapshot {
  if (!isObj(value)) throw new ApiShapeError("expected object");
  const experiments = Array.isArray(value.experiments) ? value.experiments : [];
  const measurements = Array.isArray(value.measurements) ? value.measurements : [];
  return {
    experiments: experiments.filter(isObj).map(e => ({
      id: need(numOr(e.id), "experiments.id"),
      name: strOr(e.name) ?? "",
      clientId: numOr(e.clientId) ?? 0,
      environment: numOr(e.environment) ?? 0,
      calibrated: boolOr(e.calibrated) ?? false,
      n: numOr(e.n),
      referenceRssi: numOr(e.referenceRssi),
      currentEstimateM: numOr(e.currentEstimateM),
    })),
    measurements: measurements.filter(isObj).map(m => ({
      experimentId: numOr(m.experimentId) ?? 0,
      clientId: numOr(m.clientId) ?? 0,
      actualM: numOr(m.actualM),
      rssi: numOr(m.rssiDbm),
      filtered: numOr(m.filteredDbm),
      calibration: boolOr(m.calibration) ?? false,
      estimatedM: numOr(m.estimatedM),
      errorM: numOr(m.errorM),
    })),
  };
}

export async function postJson(path: string, body: unknown, fetchImpl: typeof fetch = fetch): Promise<unknown> {
  const response = await fetchImpl(path, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  });
  let payload: unknown = null;
  try {
    payload = await response.json();
  } catch {
    payload = null;
  }
  if (!response.ok) {
    const message = isObj(payload) && typeof payload.error === "string" ? payload.error : `HTTP ${response.status}`;
    throw new HttpError(response.status, response.status === 405 ? "Escritura no disponible a través del gateway (solo lectura): abra la web desde http://192.168.4.1/" : message);
  }
  return payload;
}
