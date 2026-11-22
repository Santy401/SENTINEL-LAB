import type { Snapshot } from "./store";
import { radarRadius, signalBand, stableAngle, stdDev, type SignalBand } from "./physics";

export interface RadarClient {
  id: string;
  connected: boolean;
  rssi: number | null;
  filtered: number | null;
  history: number[];
  /** Firmware estimate; only meaningful when calibrated is true. */
  distance: { valueM: number; calibrated: boolean } | null;
  angle: number;
  radius: number;
  band: SignalBand | null;
  stabilityDb: number | null;
  origin: "websocket" | "http";
}

const WS_FRESH_MS = 5000;

/** Clients for the radar: WebSocket telemetry when fresh, HTTP /api/rf otherwise. */
export function radarClients(snapshot: Snapshot, now: number): RadarClient[] {
  const wsFresh = snapshot.rf && snapshot.rfAt !== null && now - snapshot.rfAt <= WS_FRESH_MS;
  if (wsFresh && snapshot.rf) {
    return snapshot.rf.clients.map(c => {
      const id = String(c.clientId);
      const history = (snapshot.rssiHistory[id] ?? []).map(p => p.filtered ?? p.rssi).filter((v): v is number => v !== null);
      const value = c.filteredRssiDbm ?? c.rssiDbm;
      return build(id, c.connected, c.rssiDbm, c.filteredRssiDbm ?? null, history, c.estimatedDistance ? { valueM: c.estimatedDistance.valueM, calibrated: c.estimatedDistance.calibrated } : null, value, "websocket");
    });
  }
  const http = snapshot.rfSnapshot;
  if (!http) return [];
  return http.devices.map(d => {
    const history = d.history.map(h => h.rssi).filter(Number.isFinite);
    return build(String(d.id), d.connected, d.rssiDbm, d.filteredDbm, history, null, d.filteredDbm ?? d.rssiDbm, "http");
  });
}

function build(id: string, connected: boolean, rssi: number | null, filtered: number | null, history: number[], distance: RadarClient["distance"], value: number | null, origin: RadarClient["origin"]): RadarClient {
  return {
    id,
    connected,
    rssi,
    filtered,
    history,
    distance,
    angle: stableAngle(id),
    radius: value === null ? 1 : radarRadius(value),
    band: value === null ? null : signalBand(value),
    stabilityDb: stdDev(history.slice(-30)),
    origin,
  };
}
