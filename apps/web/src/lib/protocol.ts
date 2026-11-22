// Protocol v1 (sentinel.v1) client-side model and strict validation.
// The rules mirror packages/protocol/schemas/*.schema.json. Anything that does
// not match is rejected with a reason instead of being shown as data.

export const PROTOCOL_VERSION = "1.0.0";
export const SUBPROTOCOL = "sentinel.v1";
export const MAX_FRAME_BYTES = 1024;
export const MAX_SAFE_SEQUENCE = 9007199254740991;

const TIMESTAMP_RE = /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d{1,3})?Z$/;
const SOURCE_RE = /^[a-z0-9][a-z0-9._:-]{0,63}$/;
const ID_RE = /^[A-Za-z0-9][A-Za-z0-9._:-]{0,63}$/;
const VERSION_RE = /^\d+\.\d+\.\d+$/;

export type MessageType =
  | "motion.sample"
  | "motion.status"
  | "rf.telemetry"
  | "system.status"
  | "event"
  | "connection.hello"
  | "connection.heartbeat"
  | "connection.goodbye"
  | "error";

export const MESSAGE_TYPES: readonly MessageType[] = [
  "motion.sample",
  "motion.status",
  "rf.telemetry",
  "system.status",
  "event",
  "connection.hello",
  "connection.heartbeat",
  "connection.goodbye",
  "error",
];

export interface Envelope<T extends MessageType = MessageType, P = unknown> {
  protocolVersion: string;
  type: T;
  timestamp: string;
  sequence: number;
  source: string;
  sessionId: string;
  payload: P;
}

export interface EstimatedDistance {
  valueM: number;
  calibrated: boolean;
  method: "log-distance";
}

export interface RfClient {
  clientId: number;
  connected: boolean;
  rssiDbm: number | null;
  filteredRssiDbm?: number | null;
  lastSeen: string;
  estimatedDistance?: EstimatedDistance;
}

export interface RfTelemetryPayload {
  bootId: string;
  firmwareVersion: string;
  channel: number;
  clientCount: number;
  clients: RfClient[];
  simulated: false;
}

export type OperationalState = "BOOTING" | "IDLE" | "MONITORING" | "ERROR";

export interface SystemStatusPayload {
  firmwareVersion: string;
  bootId: string;
  uptimeMs: number;
  operationalState: OperationalState;
  transports: ("http" | "websocket")[];
  capabilities?: string[];
  freeHeapBytes?: number;
  simulated: false;
}

export interface MotionStatusPayload {
  deviceId: string;
  sourceType: "None" | "BrowserDeviceMotion" | "NativeClient";
  paired: boolean;
  acceptedSamples: number;
  lastSampleAgeMs: number;
  lastHttpCode?: number;
  signalLost: boolean;
  simulationRejected: number;
  simulated: false;
}

export interface Vector3 {
  x: number;
  y: number;
  z: number;
}

export type MotionState = "STABLE" | "MOVEMENT" | "ALERT" | "SENSOR_OFFLINE";

export interface MotionSamplePayload {
  deviceId: string;
  sourceType: "NativeClient" | "SensorLogger" | "CoreMotion";
  sampleIndex: number;
  sensorTimestamp?: string;
  acceleration: Vector3;
  gravity: Vector3;
  linearAcceleration: Vector3;
  state: MotionState;
  simulated: false;
}

export type EventName =
  | "DEVICE_CONNECTED"
  | "DEVICE_DISCONNECTED"
  | "SIGNAL_WEAK"
  | "SIGNAL_RECOVERED"
  | "COVERAGE_LIMIT_REACHED"
  | "MOVEMENT_DETECTED"
  | "MOTION_ALERT"
  | "SENSOR_OFFLINE"
  | "SENSOR_RECOVERED"
  | "HEARTBEAT";

export interface EventDetails {
  previousState?: MotionState;
  newState?: MotionState;
  rssiDbm?: number;
  clientId?: number;
  deviceId?: string;
  message?: string;
}

export interface EventPayload {
  eventId: string;
  name: EventName;
  occurredAt: string;
  details?: EventDetails;
  simulated: false;
}

export type PeerRole = "firmware" | "web" | "gateway" | "ios" | "sensor-client";

export interface HelloPayload {
  deviceId: string;
  role: PeerRole;
  supportedVersion: string;
  capabilities: string[];
}

export interface HeartbeatPayload {
  bootId: string;
  uptimeMs: number;
}

export type GoodbyeReason = "shutdown" | "reconnect" | "timeout" | "client-request";

export interface GoodbyePayload {
  reason: GoodbyeReason;
}

export type ErrorCode =
  | "INVALID_ENVELOPE"
  | "UNSUPPORTED_PROTOCOL_VERSION"
  | "UNKNOWN_MESSAGE_TYPE"
  | "PAYLOAD_TOO_LARGE"
  | "RATE_LIMITED"
  | "UNAUTHORIZED"
  | "INTERNAL_ERROR";

export interface ErrorPayload {
  code: ErrorCode;
  message: string;
  retryable: boolean;
  relatedSource?: string;
  relatedSequence?: number;
}

export type ProtocolMessage =
  | Envelope<"rf.telemetry", RfTelemetryPayload>
  | Envelope<"system.status", SystemStatusPayload>
  | Envelope<"motion.status", MotionStatusPayload>
  | Envelope<"motion.sample", MotionSamplePayload>
  | Envelope<"event", EventPayload>
  | Envelope<"connection.hello", HelloPayload>
  | Envelope<"connection.heartbeat", HeartbeatPayload>
  | Envelope<"connection.goodbye", GoodbyePayload>
  | Envelope<"error", ErrorPayload>;

export type ParseResult =
  | { ok: true; message: ProtocolMessage }
  | { ok: false; reason: string; unknownType?: string };

// ---------------------------------------------------------------------------
// Primitive checks

type Obj = Record<string, unknown>;

class Invalid extends Error {}

function fail(reason: string): never {
  throw new Invalid(reason);
}

function isObject(value: unknown): value is Obj {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function obj(value: unknown, where: string): Obj {
  if (!isObject(value)) fail(`${where}: expected object`);
  return value;
}

function keys(value: Obj, where: string, required: readonly string[], optional: readonly string[] = []): void {
  for (const key of required) if (!(key in value)) fail(`${where}: missing ${key}`);
  const allowed = new Set([...required, ...optional]);
  for (const key of Object.keys(value)) if (!allowed.has(key)) fail(`${where}: unexpected field ${key}`);
}

function str(value: unknown, where: string, pattern?: RegExp, max = 256): string {
  if (typeof value !== "string" || value.length < 1 || value.length > max) fail(`${where}: expected string`);
  if (pattern && !pattern.test(value)) fail(`${where}: bad format`);
  return value;
}

function oneOf<T extends string>(value: unknown, where: string, options: readonly T[]): T {
  if (typeof value !== "string" || !(options as readonly string[]).includes(value)) fail(`${where}: not allowed`);
  return value as T;
}

function int(value: unknown, where: string, min = 0, max = MAX_SAFE_SEQUENCE): number {
  if (typeof value !== "number" || !Number.isInteger(value) || value < min || value > max) fail(`${where}: expected integer in range`);
  return value;
}

function num(value: unknown, where: string, min: number, max: number): number {
  if (typeof value !== "number" || !Number.isFinite(value) || value < min || value > max) fail(`${where}: expected number in range`);
  return value;
}

function bool(value: unknown, where: string): boolean {
  if (typeof value !== "boolean") fail(`${where}: expected boolean`);
  return value;
}

function notSimulated(value: unknown, where: string): void {
  if (value !== false) fail(`${where}: simulated data is not accepted`);
}

function timestamp(value: unknown, where: string): string {
  const text = str(value, where, TIMESTAMP_RE, 24);
  if (Number.isNaN(Date.parse(text))) fail(`${where}: invalid date`);
  // Reject calendar overflow such as 2026-02-30 that Date.parse normalizes.
  const [datePart] = text.split("T");
  const [y, m, d] = (datePart ?? "").split("-").map(Number);
  const check = new Date(Date.UTC(y ?? 0, (m ?? 1) - 1, d ?? 0));
  if (check.getUTCFullYear() !== y || check.getUTCMonth() + 1 !== m || check.getUTCDate() !== d) fail(`${where}: invalid date`);
  return text;
}

function stringArray(value: unknown, where: string, max: number, pattern: RegExp, options?: readonly string[]): string[] {
  if (!Array.isArray(value) || value.length > max) fail(`${where}: expected array`);
  const seen = new Set<string>();
  for (const item of value) {
    const text = options ? oneOf(item, where, options) : str(item, where, pattern, 64);
    if (seen.has(text)) fail(`${where}: duplicate item`);
    seen.add(text);
  }
  return value as string[];
}

function rssi(value: unknown, where: string, nullable: boolean): number | null {
  if (value === null && nullable) return null;
  return int(value, where, -100, -1);
}

const MOTION_STATES = ["STABLE", "MOVEMENT", "ALERT", "SENSOR_OFFLINE"] as const;

// ---------------------------------------------------------------------------
// Payload validators

function rfTelemetry(p: Obj): void {
  keys(p, "rf.telemetry", ["bootId", "firmwareVersion", "channel", "clientCount", "clients", "simulated"]);
  str(p.bootId, "bootId", ID_RE, 64);
  str(p.firmwareVersion, "firmwareVersion", VERSION_RE, 32);
  int(p.channel, "channel", 1, 14);
  const count = int(p.clientCount, "clientCount", 0, 8);
  if (!Array.isArray(p.clients) || p.clients.length > 8) fail("clients: expected array (max 8)");
  if (p.clients.length !== count) fail("clientCount does not match clients");
  p.clients.forEach((raw, index) => {
    const where = `clients[${index}]`;
    const c = obj(raw, where);
    keys(c, where, ["clientId", "connected", "rssiDbm", "lastSeen"], ["filteredRssiDbm", "estimatedDistance"]);
    int(c.clientId, `${where}.clientId`);
    bool(c.connected, `${where}.connected`);
    rssi(c.rssiDbm, `${where}.rssiDbm`, true);
    if ("filteredRssiDbm" in c) rssi(c.filteredRssiDbm, `${where}.filteredRssiDbm`, true);
    timestamp(c.lastSeen, `${where}.lastSeen`);
    if ("estimatedDistance" in c) {
      const d = obj(c.estimatedDistance, `${where}.estimatedDistance`);
      keys(d, `${where}.estimatedDistance`, ["valueM", "calibrated", "method"]);
      num(d.valueM, `${where}.estimatedDistance.valueM`, 0, 10000);
      bool(d.calibrated, `${where}.estimatedDistance.calibrated`);
      if (d.method !== "log-distance") fail(`${where}.estimatedDistance.method: not allowed`);
    }
  });
  notSimulated(p.simulated, "rf.telemetry");
}

function systemStatus(p: Obj): void {
  keys(p, "system.status", ["firmwareVersion", "bootId", "uptimeMs", "operationalState", "transports", "simulated"], ["capabilities", "freeHeapBytes"]);
  str(p.firmwareVersion, "firmwareVersion", VERSION_RE, 32);
  str(p.bootId, "bootId", ID_RE, 64);
  int(p.uptimeMs, "uptimeMs");
  oneOf(p.operationalState, "operationalState", ["BOOTING", "IDLE", "MONITORING", "ERROR"] as const);
  const transports = stringArray(p.transports, "transports", 2, SOURCE_RE, ["http", "websocket"]);
  if (transports.length < 1) fail("transports: at least one required");
  if ("capabilities" in p) stringArray(p.capabilities, "capabilities", 8, SOURCE_RE, ["motion.ingest", "rf.monitor", "laboratory", "webhooks"]);
  if ("freeHeapBytes" in p) int(p.freeHeapBytes, "freeHeapBytes");
  notSimulated(p.simulated, "system.status");
}

function motionStatus(p: Obj): void {
  keys(p, "motion.status", ["deviceId", "sourceType", "paired", "acceptedSamples", "lastSampleAgeMs", "signalLost", "simulationRejected", "simulated"], ["lastHttpCode"]);
  str(p.deviceId, "deviceId", ID_RE, 64);
  oneOf(p.sourceType, "sourceType", ["None", "BrowserDeviceMotion", "NativeClient"] as const);
  bool(p.paired, "paired");
  int(p.acceptedSamples, "acceptedSamples");
  int(p.lastSampleAgeMs, "lastSampleAgeMs");
  if ("lastHttpCode" in p) int(p.lastHttpCode, "lastHttpCode", 100, 599);
  bool(p.signalLost, "signalLost");
  int(p.simulationRejected, "simulationRejected");
  notSimulated(p.simulated, "motion.status");
}

function vector(value: unknown, where: string): void {
  const v = obj(value, where);
  keys(v, where, ["x", "y", "z"]);
  num(v.x, `${where}.x`, -500, 500);
  num(v.y, `${where}.y`, -500, 500);
  num(v.z, `${where}.z`, -500, 500);
}

function motionSample(p: Obj): void {
  keys(p, "motion.sample", ["deviceId", "sourceType", "sampleIndex", "acceleration", "gravity", "linearAcceleration", "state", "simulated"], ["sensorTimestamp"]);
  str(p.deviceId, "deviceId", ID_RE, 64);
  oneOf(p.sourceType, "sourceType", ["NativeClient", "SensorLogger", "CoreMotion"] as const);
  int(p.sampleIndex, "sampleIndex");
  if ("sensorTimestamp" in p) timestamp(p.sensorTimestamp, "sensorTimestamp");
  vector(p.acceleration, "acceleration");
  vector(p.gravity, "gravity");
  vector(p.linearAcceleration, "linearAcceleration");
  oneOf(p.state, "state", MOTION_STATES);
  notSimulated(p.simulated, "motion.sample");
}

const EVENT_NAMES = [
  "DEVICE_CONNECTED",
  "DEVICE_DISCONNECTED",
  "SIGNAL_WEAK",
  "SIGNAL_RECOVERED",
  "COVERAGE_LIMIT_REACHED",
  "MOVEMENT_DETECTED",
  "MOTION_ALERT",
  "SENSOR_OFFLINE",
  "SENSOR_RECOVERED",
  "HEARTBEAT",
] as const;

function event(p: Obj): void {
  keys(p, "event", ["eventId", "name", "occurredAt", "simulated"], ["details"]);
  str(p.eventId, "eventId", ID_RE, 64);
  oneOf(p.name, "name", EVENT_NAMES);
  timestamp(p.occurredAt, "occurredAt");
  if ("details" in p) {
    const d = obj(p.details, "details");
    keys(d, "details", [], ["previousState", "newState", "rssiDbm", "clientId", "deviceId", "message"]);
    if ("previousState" in d) oneOf(d.previousState, "details.previousState", MOTION_STATES);
    if ("newState" in d) oneOf(d.newState, "details.newState", MOTION_STATES);
    if ("rssiDbm" in d) rssi(d.rssiDbm, "details.rssiDbm", false);
    if ("clientId" in d) int(d.clientId, "details.clientId");
    if ("deviceId" in d) str(d.deviceId, "details.deviceId", ID_RE, 64);
    if ("message" in d) str(d.message, "details.message", undefined, 256);
  }
  notSimulated(p.simulated, "event");
}

function hello(p: Obj): void {
  keys(p, "connection.hello", ["deviceId", "role", "supportedVersion", "capabilities"]);
  str(p.deviceId, "deviceId", ID_RE, 64);
  oneOf(p.role, "role", ["firmware", "web", "gateway", "ios", "sensor-client"] as const);
  const supported = str(p.supportedVersion, "supportedVersion", VERSION_RE, 32);
  if (majorOf(supported) !== majorOf(PROTOCOL_VERSION)) fail("supportedVersion: incompatible major version");
  stringArray(p.capabilities, "capabilities", 16, SOURCE_RE);
}

function heartbeat(p: Obj): void {
  keys(p, "connection.heartbeat", ["bootId", "uptimeMs"]);
  str(p.bootId, "bootId", ID_RE, 64);
  int(p.uptimeMs, "uptimeMs");
}

function goodbye(p: Obj): void {
  keys(p, "connection.goodbye", ["reason"]);
  oneOf(p.reason, "reason", ["shutdown", "reconnect", "timeout", "client-request"] as const);
}

function error(p: Obj): void {
  keys(p, "error", ["code", "message", "retryable"], ["relatedSource", "relatedSequence"]);
  oneOf(p.code, "code", [
    "INVALID_ENVELOPE",
    "UNSUPPORTED_PROTOCOL_VERSION",
    "UNKNOWN_MESSAGE_TYPE",
    "PAYLOAD_TOO_LARGE",
    "RATE_LIMITED",
    "UNAUTHORIZED",
    "INTERNAL_ERROR",
  ] as const);
  str(p.message, "message", undefined, 256);
  bool(p.retryable, "retryable");
  if ("relatedSource" in p) str(p.relatedSource, "relatedSource", SOURCE_RE, 64);
  if ("relatedSequence" in p) int(p.relatedSequence, "relatedSequence");
}

const PAYLOAD_VALIDATORS: Record<MessageType, (payload: Obj) => void> = {
  "rf.telemetry": rfTelemetry,
  "system.status": systemStatus,
  "motion.status": motionStatus,
  "motion.sample": motionSample,
  event,
  "connection.hello": hello,
  "connection.heartbeat": heartbeat,
  "connection.goodbye": goodbye,
  error,
};

function majorOf(version: string): string {
  return version.split(".")[0] ?? "";
}

// ---------------------------------------------------------------------------
// Duplicate-key detection. JSON.parse silently keeps the last duplicate, which
// would hide an ambiguous envelope (the contract rejects them).

export function findDuplicateKey(text: string): string | null {
  const stack: Set<string>[] = [];
  let i = 0;
  let expectKey = false;
  while (i < text.length) {
    const ch = text[i];
    if (ch === '"') {
      let j = i + 1;
      let value = "";
      while (j < text.length && text[j] !== '"') {
        if (text[j] === "\\") {
          value += text.slice(j, j + 2);
          j += 2;
        } else {
          value += text[j];
          j += 1;
        }
      }
      const top = stack[stack.length - 1];
      if (expectKey && top) {
        if (top.has(value)) return value;
        top.add(value);
        expectKey = false;
      }
      i = j + 1;
      continue;
    }
    if (ch === "{") {
      stack.push(new Set());
      expectKey = true;
    } else if (ch === "}") {
      stack.pop();
      expectKey = false;
    } else if (ch === "[") {
      stack.push(undefined as unknown as Set<string>);
      expectKey = false;
    } else if (ch === "]") {
      stack.pop();
    } else if (ch === ",") {
      expectKey = stack[stack.length - 1] instanceof Set;
    }
    i += 1;
  }
  return null;
}

// ---------------------------------------------------------------------------
// Public API

export function utf8Length(text: string): number {
  return new TextEncoder().encode(text).length;
}

export function validateEnvelope(value: unknown): ProtocolMessage {
  const e = obj(value, "envelope");
  keys(e, "envelope", ["protocolVersion", "type", "timestamp", "sequence", "source", "sessionId", "payload"]);
  const version = str(e.protocolVersion, "protocolVersion", VERSION_RE, 32);
  if (version !== PROTOCOL_VERSION) fail(`unsupported protocol version ${version}`);
  const type = e.type;
  if (typeof type !== "string" || !(MESSAGE_TYPES as readonly string[]).includes(type)) {
    throw Object.assign(new Invalid("unknown message type"), { unknownType: typeof type === "string" ? type : "" });
  }
  timestamp(e.timestamp, "timestamp");
  int(e.sequence, "sequence");
  str(e.source, "source", SOURCE_RE, 64);
  str(e.sessionId, "sessionId", ID_RE, 64);
  PAYLOAD_VALIDATORS[type as MessageType](obj(e.payload, "payload"));
  return e as unknown as ProtocolMessage;
}

/** Parses one text frame from the firmware. Never throws. */
export function parseServerFrame(text: string, maxBytes = MAX_FRAME_BYTES): ParseResult {
  if (utf8Length(text) > maxBytes) return { ok: false, reason: "frame exceeds size limit" };
  let value: unknown;
  try {
    value = JSON.parse(text);
  } catch {
    return { ok: false, reason: "invalid JSON" };
  }
  const duplicate = findDuplicateKey(text);
  if (duplicate !== null) return { ok: false, reason: `duplicate key ${duplicate}` };
  try {
    return { ok: true, message: validateEnvelope(value) };
  } catch (err) {
    if (err instanceof Invalid) {
      const unknownType = (err as Invalid & { unknownType?: string }).unknownType;
      return unknownType !== undefined ? { ok: false, reason: err.message, unknownType } : { ok: false, reason: err.message };
    }
    return { ok: false, reason: "validation error" };
  }
}

// ---------------------------------------------------------------------------
// Outbound envelopes (role "web" is read-only: hello, heartbeat, goodbye)

/** UTC with exactly three fractional digits, as Protocol v1 requires. */
export function formatUtc(date: Date): string {
  return date.toISOString();
}

export function randomId(prefix: string, random: () => number = Math.random): string {
  let out = "";
  const alphabet = "abcdefghijklmnopqrstuvwxyz0123456789";
  for (let i = 0; i < 10; i += 1) out += alphabet[Math.floor(random() * alphabet.length) % alphabet.length];
  return `${prefix}-${out}`;
}

export class OutboundSession {
  readonly source: string;
  readonly sessionId: string;
  private sequence = 0;

  constructor(source: string, sessionId: string) {
    if (!SOURCE_RE.test(source)) throw new Error("invalid source");
    if (!ID_RE.test(sessionId)) throw new Error("invalid sessionId");
    this.source = source;
    this.sessionId = sessionId;
  }

  get lastSequence(): number {
    return this.sequence;
  }

  private envelope<T extends MessageType, P>(type: T, payload: P, now: Date): Envelope<T, P> {
    if (this.sequence >= MAX_SAFE_SEQUENCE) throw new Error("sequence exhausted");
    this.sequence += 1;
    return {
      protocolVersion: PROTOCOL_VERSION,
      type,
      timestamp: formatUtc(now),
      sequence: this.sequence,
      source: this.source,
      sessionId: this.sessionId,
      payload,
    };
  }

  hello(deviceId: string, now: Date = new Date()): Envelope<"connection.hello", HelloPayload> {
    return this.envelope("connection.hello", { deviceId, role: "web", supportedVersion: PROTOCOL_VERSION, capabilities: ["telemetry.view"] }, now);
  }

  heartbeat(bootId: string, uptimeMs: number, now: Date = new Date()): Envelope<"connection.heartbeat", HeartbeatPayload> {
    return this.envelope("connection.heartbeat", { bootId, uptimeMs: Math.max(0, Math.floor(uptimeMs)) }, now);
  }

  goodbye(reason: GoodbyeReason, now: Date = new Date()): Envelope<"connection.goodbye", GoodbyePayload> {
    return this.envelope("connection.goodbye", { reason }, now);
  }
}
