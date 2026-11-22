import { subprotocols } from "./token";
import { Backoff } from "./backoff";
import { OutboundSession, SUBPROTOCOL, parseServerFrame, randomId, type ProtocolMessage } from "./protocol";

export type SocketState = "idle" | "connecting" | "open" | "closed";

/** The subset of the browser WebSocket used here, so tests can supply a fake. */
export interface SocketLike {
  readonly readyState: number;
  readonly protocol: string;
  onopen: ((event: unknown) => void) | null;
  onclose: ((event: { code: number; reason: string; wasClean: boolean }) => void) | null;
  onerror: ((event: unknown) => void) | null;
  onmessage: ((event: { data: unknown }) => void) | null;
  send(data: string): void;
  close(code?: number, reason?: string): void;
}

export interface Scheduler {
  setTimeout(fn: () => void, ms: number): unknown;
  clearTimeout(handle: unknown): void;
  setInterval(fn: () => void, ms: number): unknown;
  clearInterval(handle: unknown): void;
  now(): number;
}

export const browserScheduler: Scheduler = {
  setTimeout: (fn, ms) => globalThis.setTimeout(fn, ms),
  clearTimeout: handle => globalThis.clearTimeout(handle as ReturnType<typeof setTimeout>),
  setInterval: (fn, ms) => globalThis.setInterval(fn, ms),
  clearInterval: handle => globalThis.clearInterval(handle as ReturnType<typeof setInterval>),
  now: () => Date.now(),
};

export interface ClientStats {
  framesReceived: number;
  framesInvalid: number;
  framesUnknownType: number;
  framesBinary: number;
  framesSent: number;
  sendsThrottled: number;
  reconnects: number;
  watchdogCloses: number;
  lastFrameAt: number | null;
  lastInvalidReason: string | null;
  lastCloseCode: number | null;
  nextRetryMs: number | null;
}

export interface RealtimeClientOptions {
  url: string;
  /** Returns the current realtime token (offered as a subprotocol) or null. */
  token?: () => string | null;
  createSocket?: (url: string, protocols: string[]) => SocketLike;
  scheduler?: Scheduler;
  random?: () => number;
  heartbeatMs?: number;
  /** Close and reconnect when the firmware should be streaming but is silent. */
  watchdogMs?: number;
  onMessage?: (message: ProtocolMessage) => void;
  onState?: (state: SocketState) => void;
  onStats?: (stats: ClientStats) => void;
}

const OPEN = 1;
const MAX_SENDS_PER_SECOND = 8;

export class RealtimeClient {
  private socket: SocketLike | null = null;
  private state: SocketState = "idle";
  private session: OutboundSession | null = null;
  private readonly backoff: Backoff;
  private readonly scheduler: Scheduler;
  private readonly createSocket: (url: string, protocols: string[]) => SocketLike;
  private readonly random: () => number;
  private readonly heartbeatMs: number;
  private readonly watchdogMs: number;
  private readonly deviceId: string;
  private readonly startedAt: number;
  private heartbeatTimer: unknown = null;
  private watchdogTimer: unknown = null;
  private retryTimer: unknown = null;
  private stopped = true;
  private expectTraffic = false;
  private sendWindowStart = 0;
  private sendsInWindow = 0;
  readonly stats: ClientStats = {
    framesReceived: 0,
    framesInvalid: 0,
    framesUnknownType: 0,
    framesBinary: 0,
    framesSent: 0,
    sendsThrottled: 0,
    reconnects: 0,
    watchdogCloses: 0,
    lastFrameAt: null,
    lastInvalidReason: null,
    lastCloseCode: null,
    nextRetryMs: null,
  };

  constructor(private readonly options: RealtimeClientOptions) {
    this.scheduler = options.scheduler ?? browserScheduler;
    this.random = options.random ?? Math.random;
    this.backoff = new Backoff({ baseMs: 1000, maxMs: 30000, random: this.random });
    this.createSocket = options.createSocket ?? ((url, protocols) => new WebSocket(url, protocols) as unknown as SocketLike);
    this.heartbeatMs = options.heartbeatMs ?? 15000;
    this.watchdogMs = options.watchdogMs ?? 20000;
    this.deviceId = randomId("web", this.random);
    this.startedAt = this.scheduler.now();
  }

  get currentState(): SocketState {
    return this.state;
  }

  start(): void {
    if (!this.stopped) return;
    this.stopped = false;
    this.connect();
  }

  stop(): void {
    this.stopped = true;
    this.clearTimers();
    if (this.retryTimer !== null) this.scheduler.clearTimeout(this.retryTimer);
    this.retryTimer = null;
    const socket = this.socket;
    this.socket = null;
    if (socket) {
      if (socket.readyState === OPEN && this.session) this.sendRaw(socket, this.session.goodbye("client-request"));
      detach(socket);
      try {
        socket.close(1000, "client-request");
      } catch {
        /* already closed */
      }
    }
    this.stats.nextRetryMs = null;
    this.setState("idle");
  }

  /**
   * Tell the client whether the firmware is expected to stream (its clock is
   * synchronized). Only then can silence be treated as a dead link.
   */
  setExpectTraffic(expect: boolean): void {
    if (this.expectTraffic === expect) return;
    this.expectTraffic = expect;
    this.armWatchdog();
  }

  private connect(): void {
    if (this.stopped) return;
    this.retryTimer = null;
    this.stats.nextRetryMs = null;
    this.setState("connecting");
    let socket: SocketLike;
    try {
      socket = this.createSocket(this.options.url, subprotocols(SUBPROTOCOL, this.options.token?.() ?? null));
    } catch {
      this.scheduleReconnect();
      return;
    }
    this.socket = socket;
    socket.onopen = () => this.handleOpen(socket);
    socket.onmessage = event => this.handleMessage(socket, event.data);
    socket.onerror = () => {
      /* onclose follows and drives the reconnect */
    };
    socket.onclose = event => this.handleClose(socket, event.code);
  }

  private handleOpen(socket: SocketLike): void {
    if (socket !== this.socket) return;
    if (socket.protocol !== SUBPROTOCOL) {
      // A server that did not negotiate sentinel.v1 is not this firmware.
      this.stats.lastInvalidReason = `subprotocolo negociado: "${socket.protocol}"`;
      socket.close(1002, "subprotocol");
      return;
    }
    this.backoff.reset();
    this.session = new OutboundSession("web-dashboard", randomId("web-session", this.random));
    this.setState("open");
    this.sendRaw(socket, this.session.hello(this.deviceId));
    this.heartbeatTimer = this.scheduler.setInterval(() => {
      if (this.socket === socket && socket.readyState === OPEN && this.session) {
        this.sendRaw(socket, this.session.heartbeat(this.deviceId, this.scheduler.now() - this.startedAt));
      }
    }, this.heartbeatMs);
    this.armWatchdog();
  }

  private handleMessage(socket: SocketLike, data: unknown): void {
    if (socket !== this.socket) return;
    if (typeof data !== "string") {
      this.stats.framesBinary += 1;
      this.emitStats();
      return;
    }
    const result = parseServerFrame(data);
    if (result.ok) {
      this.stats.framesReceived += 1;
      this.stats.lastFrameAt = this.scheduler.now();
      this.armWatchdog();
      this.emitStats();
      this.options.onMessage?.(result.message);
      return;
    }
    if (result.unknownType !== undefined) this.stats.framesUnknownType += 1;
    else this.stats.framesInvalid += 1;
    this.stats.lastInvalidReason = result.reason;
    this.emitStats();
  }

  private handleClose(socket: SocketLike, code: number): void {
    if (socket !== this.socket) return;
    detach(socket);
    this.socket = null;
    this.session = null;
    this.stats.lastCloseCode = code;
    this.clearTimers();
    this.setState("closed");
    this.scheduleReconnect();
  }

  private scheduleReconnect(): void {
    if (this.stopped) return;
    const delay = this.backoff.next();
    this.stats.reconnects += 1;
    this.stats.nextRetryMs = delay;
    this.emitStats();
    this.retryTimer = this.scheduler.setTimeout(() => this.connect(), delay);
  }

  private armWatchdog(): void {
    if (this.watchdogTimer !== null) this.scheduler.clearTimeout(this.watchdogTimer);
    this.watchdogTimer = null;
    if (!this.expectTraffic || this.state !== "open") return;
    const socket = this.socket;
    this.watchdogTimer = this.scheduler.setTimeout(() => {
      if (socket && socket === this.socket) {
        this.stats.watchdogCloses += 1;
        try {
          socket.close(4000, "watchdog");
        } catch {
          /* ignore */
        }
        // Do not wait for the browser's close event, which can take minutes on a dead link.
        this.handleClose(socket, 4000);
      }
    }, this.watchdogMs);
  }

  private sendRaw(socket: SocketLike, envelope: unknown): void {
    const now = this.scheduler.now();
    if (now - this.sendWindowStart >= 1000) {
      this.sendWindowStart = now;
      this.sendsInWindow = 0;
    }
    if (this.sendsInWindow >= MAX_SENDS_PER_SECOND) {
      this.stats.sendsThrottled += 1;
      return;
    }
    this.sendsInWindow += 1;
    try {
      socket.send(JSON.stringify(envelope));
      this.stats.framesSent += 1;
    } catch {
      /* close event follows */
    }
  }

  private clearTimers(): void {
    if (this.heartbeatTimer !== null) this.scheduler.clearInterval(this.heartbeatTimer);
    if (this.watchdogTimer !== null) this.scheduler.clearTimeout(this.watchdogTimer);
    this.heartbeatTimer = null;
    this.watchdogTimer = null;
  }

  private setState(state: SocketState): void {
    if (this.state === state) return;
    this.state = state;
    this.options.onState?.(state);
    this.emitStats();
  }

  private emitStats(): void {
    this.options.onStats?.({ ...this.stats });
  }
}

function detach(socket: SocketLike): void {
  socket.onopen = null;
  socket.onclose = null;
  socket.onerror = null;
  socket.onmessage = null;
}
