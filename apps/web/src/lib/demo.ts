// DEMO mode: replays the frames the firmware host tests generate
// (src/fixtures/firmware). Timestamps and RSSI are varied so charts move.
// Everything produced here is labeled DEMO in the UI and in exported evidence;
// it is never a measurement.
import rfFixture from "@/fixtures/firmware/firmware-rf.json";
import systemFixture from "@/fixtures/firmware/firmware-system.json";
import heartbeatFixture from "@/fixtures/firmware/firmware-heartbeat.json";
import eventFixture from "@/fixtures/firmware/firmware-event.json";
import helloFixture from "@/fixtures/firmware/firmware-hello.json";
import motionFixture from "@/fixtures/firmware/firmware-motion.json";
import type { RealtimeStatus } from "./api";
import { parseServerFrame, type ProtocolMessage } from "./protocol";

export const DEMO_FIXTURES: readonly unknown[] = [helloFixture, rfFixture, systemFixture, heartbeatFixture, eventFixture, motionFixture];

function clone<T>(value: T): T {
  return JSON.parse(JSON.stringify(value)) as T;
}

/** Deterministic frame for tick n (one tick per second). Returns validated messages only. */
export function demoFrames(tick: number, now: Date): ProtocolMessage[] {
  const stamp = now.toISOString();
  const frames: Record<string, unknown>[] = [];
  const withEnvelope = (fixture: unknown, sequence: number) => {
    const frame = clone(fixture) as Record<string, unknown>;
    frame.timestamp = stamp;
    frame.sequence = sequence;
    return frame;
  };
  if (tick === 0) frames.push(withEnvelope(helloFixture, 1));
  const rf = withEnvelope(rfFixture, tick * 4 + 2);
  const payload = rf.payload as { clients: { rssiDbm: number | null; filteredRssiDbm?: number | null; lastSeen: string; connected: boolean }[] };
  payload.clients.forEach((client, index) => {
    if (!client.connected) return;
    const wave = Math.round(6 * Math.sin((tick + index * 7) / 5) + 2 * Math.sin(tick / 1.7));
    client.rssiDbm = Math.max(-100, Math.min(-1, -54 + wave - index * 9));
    client.filteredRssiDbm = Math.max(-100, Math.min(-1, -55 + Math.round(wave * 0.6) - index * 9));
    client.lastSeen = stamp;
  });
  frames.push(rf);
  if (tick % 5 === 0) frames.push(withEnvelope(heartbeatFixture, tick * 4 + 3));
  if (tick % 15 === 0) frames.push(withEnvelope(systemFixture, tick * 4 + 4));
  if (tick % 20 === 10) {
    const event = withEnvelope(eventFixture, tick * 4 + 5);
    const ev = event.payload as { eventId: string; occurredAt: string };
    ev.eventId = `demo-evt-${tick}`;
    ev.occurredAt = stamp;
    frames.push(event);
  }
  const out: ProtocolMessage[] = [];
  for (const frame of frames) {
    const result = parseServerFrame(JSON.stringify(frame));
    if (result.ok) out.push(result.message);
  }
  return out;
}

export function demoStatus(tick: number): RealtimeStatus {
  const heap = 214000 - (tick % 30) * 120;
  return {
    fw: "0.3.1",
    httpApiVersion: "0.3.1",
    firmwareVersion: "0.4.1",
    realtimeVersion: "0.4.1",
    bootId: "demo0000demo0000",
    resetReason: "POWERON",
    uptimeMs: 60000 + tick * 1000,
    clockSynced: true,
    clockSource: "peer",
    clockSyncAgeMs: (tick % 15) * 1000,
    clientCount: 1,
    freeHeapBytes: heap,
    minFreeHeapBytes: 210400,
    largestFreeBlockBytes: 110592,
    minLoopStackFreeBytes: 5120,
    heapSamples: 4 + Math.floor(tick / 15),
    droppedFrames: 0,
    rejectedMessages: 0,
    droppedTelemetry: 0,
    skippedSimulatedEvents: 0,
    handshakeTimeouts: 0,
    idleTimeouts: 0,
    remoteCloses: 0,
  };
}
