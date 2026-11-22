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
