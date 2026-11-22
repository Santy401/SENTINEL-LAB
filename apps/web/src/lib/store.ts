import {
  DEFAULT_WS_URL,
  HttpError,
  getJson,
  parseDeviceState,
  parseEvents,
  parseGatewayConfig,
  parseGatewayStatus,
  parseRealtimeStatus,
  parseRfSnapshot,
  type DeviceState,
  type EventsSnapshot,
  type GatewayStatus,
  type RealtimeStatus,
  type RfSnapshot,
} from "./api";
import { Backoff } from "./backoff";
import { demoFrames, demoStatus } from "./demo";
import { evaluateHealth, type HealthIndicator } from "./health";
import type {
  EventPayload,
  MotionStatusPayload,
  ProtocolMessage,
  RfTelemetryPayload,
  SystemStatusPayload,
  ErrorPayload,
} from "./protocol";
import { RealtimeClient, type ClientStats, type SocketState } from "./realtime-client";
import { Ring } from "./ring";

export interface Settings {
  refreshMs: number;
  demo: boolean;
}

export interface RssiPoint {
  at: number;
  rssi: number | null;
  filtered: number | null;
}

export interface TimelineItem {
  id: string;
  at: number;
  name: string;
  clientId: number | null;
  rssiDbm: number | null;
  message: string | null;
  origin: "websocket" | "http" | "local";
}

export interface FrameRecord {
  at: number;
  type: string;
  sequence: number;
  source: string;
}

export interface Snapshot {
  settings: Settings;
  demo: boolean;
  wsUrl: string;
  gatewayVersion: string | null;
  configResolved: boolean;
  socketState: SocketState | "demo";
  clientStats: ClientStats | null;
  realtimeStatus: RealtimeStatus | null;
  previousStatus: RealtimeStatus | null;
  deviceState: DeviceState | null;
  rfSnapshot: RfSnapshot | null;
  httpEvents: EventsSnapshot | null;
  gatewayStatus: GatewayStatus | null;
  httpError: string | null;
  lastPollAt: number | null;
  nextPollMs: number;
  rf: RfTelemetryPayload | null;
  rfAt: number | null;
  system: SystemStatusPayload | null;
  motion: MotionStatusPayload | null;
  lastServerError: ErrorPayload | null;
  rssiHistory: Record<string, RssiPoint[]>;
  timeline: TimelineItem[];
  frames: FrameRecord[];
  frameCounts: Record<string, number>;
  heapHistory: { at: number; free: number }[];
  health: HealthIndicator[];
  loading: boolean;
}

export type Notice = { type: "success" | "info" | "warning" | "error"; title: string; description?: string };

const HISTORY_POINTS = 120;
const TIMELINE_ITEMS = 100;
const FRAME_RECORDS = 40;
const HEAP_POINTS = 120;

function describe(error: unknown): string {
  if (error instanceof HttpError) return `HTTP ${error.status}`;
  if (error instanceof DOMException && error.name === "AbortError") return "tiempo de espera agotado";
  if (error instanceof Error) return error.message;
  return "error desconocido";
}

export class SentinelStore {
  private listeners = new Set<() => void>();
  private noticeListeners = new Set<(notice: Notice) => void>();
  private snapshot: Snapshot;
  private client: RealtimeClient | null = null;
  private pollTimer: ReturnType<typeof setTimeout> | null = null;
  private demoTimer: ReturnType<typeof setInterval> | null = null;
  private demoTick = 0;
  private abort: AbortController | null = null;
  private readonly pollBackoff = new Backoff({ baseMs: 2000, maxMs: 30000 });
  private readonly rssi = new Map<string, Ring<RssiPoint>>();
  private readonly timeline = new Ring<TimelineItem>(TIMELINE_ITEMS);
  private readonly frames = new Ring<FrameRecord>(FRAME_RECORDS);
  private readonly heap = new Ring<{ at: number; free: number }>(HEAP_POINTS);
  private readonly seenEvents = new Set<string>();
  private running = false;

  constructor(settings: Settings) {
    this.snapshot = {
      settings,
      demo: settings.demo,
      wsUrl: DEFAULT_WS_URL,
      gatewayVersion: null,
      configResolved: false,
      socketState: settings.demo ? "demo" : "idle",
      clientStats: null,
      realtimeStatus: null,
      previousStatus: null,
      deviceState: null,
      rfSnapshot: null,
      httpEvents: null,
      gatewayStatus: null,
      httpError: null,
      lastPollAt: null,
      nextPollMs: settings.refreshMs,
      rf: null,
      rfAt: null,
      system: null,
      motion: null,
      lastServerError: null,
      rssiHistory: {},
      timeline: [],
      frames: [],
      frameCounts: {},
      heapHistory: [],
      health: [],
      loading: true,
    };
  }

  subscribe = (listener: () => void): (() => void) => {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  };

  onNotice(listener: (notice: Notice) => void): () => void {
    this.noticeListeners.add(listener);
    return () => this.noticeListeners.delete(listener);
  }

  getSnapshot = (): Snapshot => this.snapshot;

  private set(patch: Partial<Snapshot>): void {
    const next = { ...this.snapshot, ...patch };
    next.health = evaluateHealth({
      status: next.realtimeStatus,
      previous: next.previousStatus,
      httpError: next.httpError,
      socketState: next.socketState,
    });
    this.snapshot = next;
    for (const listener of this.listeners) listener();
  }

  private notify(notice: Notice): void {
    for (const listener of this.noticeListeners) listener(notice);
  }

  start(): void {
    if (this.running) return;
    this.running = true;
    if (this.snapshot.settings.demo) this.startDemo();
    else void this.startLive();
  }

  stop(): void {
    this.running = false;
    this.client?.stop();
    this.client = null;
    if (this.pollTimer) clearTimeout(this.pollTimer);
    if (this.demoTimer) clearInterval(this.demoTimer);
    this.pollTimer = null;
    this.demoTimer = null;
    this.abort?.abort();
    this.abort = null;
  }

  updateSettings(settings: Settings): void {
    const modeChanged = settings.demo !== this.snapshot.settings.demo;
    if (modeChanged) {
      this.stop();
      this.resetData();
      this.set({ settings, demo: settings.demo, socketState: settings.demo ? "demo" : "idle", loading: true });
      this.start();
      this.notify({ type: "info", title: settings.demo ? "Modo DEMO activado" : "Modo dispositivo activado", description: settings.demo ? "Los datos son fixtures reproducidos, no mediciones." : undefined });
      return;
    }
    this.set({ settings, nextPollMs: settings.refreshMs });
  }

  private resetData(): void {
    this.rssi.clear();
    this.timeline.clear();
    this.frames.clear();
    this.heap.clear();
    this.seenEvents.clear();
    this.demoTick = 0;
    this.set({
      realtimeStatus: null,
      previousStatus: null,
      deviceState: null,
      rfSnapshot: null,
      httpEvents: null,
      gatewayStatus: null,
      httpError: null,
      rf: null,
      rfAt: null,
      system: null,
      motion: null,
      lastServerError: null,
      rssiHistory: {},
      timeline: [],
      frames: [],
      frameCounts: {},
      heapHistory: [],
      clientStats: null,
    });
  }

  // ---------------------------------------------------------------- live mode

  private async startLive(): Promise<void> {
    let wsUrl = DEFAULT_WS_URL;
    let gatewayVersion: string | null = null;
    try {
      const config = await getJson("gateway/config", parseGatewayConfig, { timeoutMs: 2500 });
      wsUrl = config.wsUrl;
      gatewayVersion = config.gatewayVersion;
    } catch {
      // No gateway (e.g. Vite dev server): use the laboratory AP default.
    }
    if (!this.running) return;
    this.set({ wsUrl, gatewayVersion, configResolved: true });
    this.client = new RealtimeClient({
      url: wsUrl,
      onMessage: message => this.ingest(message, Date.now()),
      onState: state => this.handleSocketState(state),
      onStats: stats => this.set({ clientStats: stats }),
    });
    this.client.start();
    void this.poll();
  }

  private handleSocketState(state: SocketState): void {
    const previous = this.snapshot.socketState;
    this.set({ socketState: state });
    if (state === "open" && previous !== "open") this.notify({ type: "success", title: "WebSocket conectado", description: "Sesión sentinel.v1 abierta" });
    if (state === "closed" && previous === "open") this.notify({ type: "warning", title: "WebSocket desconectado", description: "Reintentando con espera exponencial" });
  }

  private async poll(): Promise<void> {
    if (!this.running || this.snapshot.settings.demo) return;
    this.abort = new AbortController();
    const signal = this.abort.signal;
    const wasFailing = this.snapshot.httpError !== null;
    try {
      const status = await getJson("api/realtime/status", parseRealtimeStatus, { signal });
      const [device, rf, events, gateway] = await Promise.allSettled([
        getJson("api/state", parseDeviceState, { signal }),
        getJson("api/rf", parseRfSnapshot, { signal }),
        getJson("api/events", parseEvents, { signal }),
        this.snapshot.gatewayVersion ? getJson("gateway/status", parseGatewayStatus, { signal, timeoutMs: 2000 }) : Promise.reject(new Error("no gateway")),
      ]);
      if (!this.running) return;
      const previous = this.snapshot.realtimeStatus;
      if (previous && previous.bootId !== status.bootId) {
        this.notify({ type: "warning", title: "Reinicio del ESP32 detectado", description: `Nuevo bootId ${status.bootId}${status.resetReason ? ` (causa ${status.resetReason})` : ""}` });
        this.heap.clear();
      }
      const now = Date.now();
      this.heap.push({ at: now, free: status.freeHeapBytes });
      if (events.status === "fulfilled") this.ingestHttpEvents(events.value);
      this.client?.setExpectTraffic(status.clockSynced);
      this.pollBackoff.reset();
      this.set({
        realtimeStatus: status,
        previousStatus: previous,
        deviceState: device.status === "fulfilled" ? device.value : this.snapshot.deviceState,
        rfSnapshot: rf.status === "fulfilled" ? rf.value : this.snapshot.rfSnapshot,
        httpEvents: events.status === "fulfilled" ? events.value : this.snapshot.httpEvents,
        gatewayStatus: gateway.status === "fulfilled" ? gateway.value : null,
        httpError: null,
        lastPollAt: now,
        nextPollMs: this.snapshot.settings.refreshMs,
        heapHistory: this.heap.toArray(),
        timeline: this.timeline.toArray(),
        loading: false,
      });
      if (wasFailing) this.notify({ type: "success", title: "ESP32 accesible de nuevo" });
      this.schedulePoll(this.snapshot.settings.refreshMs);
    } catch (error) {
      if (!this.running) return;
      const delay = Math.max(this.snapshot.settings.refreshMs, this.pollBackoff.next());
      if (!wasFailing) this.notify({ type: "error", title: "No se puede leer el ESP32", description: describe(error) });
      this.set({ httpError: describe(error), nextPollMs: delay, loading: false });
      this.schedulePoll(delay);
    }
  }

  private schedulePoll(delay: number): void {
    if (!this.running) return;
    this.pollTimer = setTimeout(() => void this.poll(), delay);
  }

  private ingestHttpEvents(snapshot: EventsSnapshot): void {
    for (const event of snapshot.events) {
      if (event.simulated) continue;
      const key = `http-${snapshot.bootId}-${event.sequence}`;
      if (this.seenEvents.has(key)) continue;
      this.remember(key);
      // HTTP events carry firmware uptime, not UTC; place them relative to now.
      const status = this.snapshot.realtimeStatus;
      const at = status ? Date.now() - Math.max(0, status.uptimeMs - event.timestampMs) : Date.now();
      this.timeline.push({
        id: key,
        at,
        name: event.event,
        clientId: event.clientId || null,
        rssiDbm: event.rssiDbm,
        message: null,
        origin: "http",
      });
    }
  }

  // --------------------------------------------------------------- demo mode

  private startDemo(): void {
    this.set({ wsUrl: "demo://fixtures", configResolved: true, socketState: "demo", loading: false });
    const tick = () => {
      const now = Date.now();
      const status = demoStatus(this.demoTick);
      this.heap.push({ at: now, free: status.freeHeapBytes });
      for (const message of demoFrames(this.demoTick, new Date(now))) this.ingest(message, now, false);
      this.demoTick += 1;
      this.set({
        realtimeStatus: status,
        previousStatus: this.snapshot.realtimeStatus,
        heapHistory: this.heap.toArray(),
        lastPollAt: now,
        httpError: null,
      });
    };
    tick();
    this.demoTimer = setInterval(tick, 1000);
  }

  // ------------------------------------------------------------- ingestion

  private ingest(message: ProtocolMessage, now: number, publish = true): void {
    this.frames.push({ at: now, type: message.type, sequence: message.sequence, source: message.source });
    const frameCounts = { ...this.snapshot.frameCounts, [message.type]: (this.snapshot.frameCounts[message.type] ?? 0) + 1 };
    const patch: Partial<Snapshot> = { frames: this.frames.toArray(), frameCounts };
    switch (message.type) {
      case "rf.telemetry": {
        for (const client of message.payload.clients) {
          const key = String(client.clientId);
          let ring = this.rssi.get(key);
          if (!ring) {
            ring = new Ring<RssiPoint>(HISTORY_POINTS);
            this.rssi.set(key, ring);
          }
          ring.push({ at: now, rssi: client.rssiDbm, filtered: client.filteredRssiDbm ?? null });
        }
        const history: Record<string, RssiPoint[]> = {};
        for (const [key, ring] of this.rssi) history[key] = ring.toArray();
        patch.rf = message.payload;
        patch.rfAt = now;
        patch.rssiHistory = history;
        break;
      }
      case "system.status":
        patch.system = message.payload;
        break;
      case "motion.status":
        patch.motion = message.payload;
        break;
      case "event":
        this.ingestEvent(message.payload, now);
        patch.timeline = this.timeline.toArray();
        break;
      case "error":
        patch.lastServerError = message.payload;
        this.notify({ type: "error", title: `Error del firmware: ${message.payload.code}`, description: message.payload.message });
        break;
      default:
        break;
    }
    if (publish) this.set(patch);
    else this.snapshot = { ...this.snapshot, ...patch };
  }

  /** Keeps the dedup set bounded to the events still held in the timeline. */
  private remember(key: string): void {
    this.seenEvents.add(key);
    if (this.seenEvents.size > TIMELINE_ITEMS * 4) {
      const keep = new Set(this.timeline.toArray().map(item => item.id));
      keep.add(key);
      this.seenEvents.clear();
      for (const id of keep) this.seenEvents.add(id);
    }
  }

  private ingestEvent(payload: EventPayload, now: number): void {
    const key = `ws-${payload.eventId}`;
    if (this.seenEvents.has(key)) return;
    this.remember(key);
    const at = Date.parse(payload.occurredAt);
    this.timeline.push({
      id: key,
      at: Number.isNaN(at) ? now : at,
      name: payload.name,
      clientId: payload.details?.clientId ?? null,
      rssiDbm: payload.details?.rssiDbm ?? null,
      message: payload.details?.message ?? null,
      origin: "websocket",
    });
  }
}
