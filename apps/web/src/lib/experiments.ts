// Reproducible lab sessions. A session records real samples taken from the
// store snapshot while it runs; nothing is synthesized here. DEMO sessions are
// flagged in the record and in every export.

import type { MotionPoint, RgbChange, Snapshot } from "./store";
import { seriesStats, toG, tiltFromGravity, type SeriesStats } from "./physics";

export type ExperimentId = "gravity" | "motion" | "coverage" | "rgb" | "stability";

export interface ExperimentDefinition {
  id: ExperimentId;
  name: string;
  objective: string;
  instruments: string[];
  variables: string[];
  procedure: string[];
  /** What is measured vs. derived, shown verbatim to the user. */
  honesty: string;
}

export const EXPERIMENTS: readonly ExperimentDefinition[] = [
  {
    id: "gravity",
    name: "Gravedad y orientación",
    objective: "Observar cómo el vector de gravedad medido por el iPhone cambia al inclinarlo.",
    instruments: ["Acelerómetro del iPhone (CoreMotion)", "Relé WebSocket del ESP32"],
    variables: ["Gravedad x, y, z (m/s²)", "Cabeceo y alabeo (°)", "|g| (m/s²)"],
    procedure: ["Apoya el iPhone plano y pulsa Iniciar.", "Inclínalo despacio hacia delante, atrás y a los lados.", "Detén la sesión y compara |g| con 9,81 m/s²."],
    honesty: "Gravedad medida por CoreMotion. Cabeceo y alabeo se derivan de ella; el rumbo (yaw) no es observable solo con la gravedad.",
  },
  {
    id: "motion",
    name: "Movimiento",
    objective: "Registrar la aceleración lineal y la intensidad de agitación del iPhone.",
    instruments: ["Acelerómetro del iPhone (CoreMotion)", "Relé WebSocket del ESP32"],
    variables: ["Aceleración lineal x, y, z (m/s²)", "|a| (m/s², g)", "Fuerza estimada F = m·a (N)"],
    procedure: ["Pulsa Iniciar con el iPhone quieto.", "Agítalo en ráfagas cortas de distinta fuerza.", "Detén y revisa picos, media y máximo."],
    honesty: "Aceleración medida. La fuerza es una estimación con la masa nominal del iPhone 12 (164 g); no se mide velocidad ni desplazamiento.",
  },
  {
    id: "coverage",
    name: "Cobertura Wi-Fi",
    objective: "Comparar el RSSI del iPhone en posiciones a distancia medida con cinta métrica.",
    instruments: ["Monitor RF del ESP32 (RSSI del cliente)", "API de laboratorio del firmware", "Cinta métrica"],
    variables: ["Distancia real (m)", "RSSI y RSSI filtrado (dBm)", "Distancia estimada por el modelo (m)"],
    procedure: ["Crea un experimento para tu cliente.", "Mide al menos dos distancias distintas como puntos de calibración.", "Calibra y añade mediciones de validación."],
    honesty: "RSSI medido por el ESP32. La distancia estimada procede de un modelo log-distancia ajustado con tus puntos; no es una posición.",
  },
  {
    id: "rgb",
    name: "Respuesta RGB",
    objective: "Estimar la latencia entre una agitación del iPhone y el cambio del LED RGB físico.",
    instruments: ["Acelerómetro del iPhone", "LED WS2812 del ESP32 (estado por HTTP)"],
    variables: ["Instante del pico de movimiento", "Instante del cambio de LED observado", "Latencia (ms)"],
    procedure: ["Pon el intervalo de lectura en 2 s.", "Deja el iPhone quieto, luego agítalo con fuerza.", "Repite varias veces y revisa la tabla de latencias."],
    honesty: "Aproximación: el estado del LED se observa por sondeo HTTP, así que la resolución es el intervalo de lectura. Es una cota superior, no la latencia real.",
  },
  {
    id: "stability",
    name: "Estabilidad",
    objective: "Registrar uptime, memoria y latencia del enlace durante una sesión.",
    instruments: ["Ruta /api/realtime/status", "Cliente WebSocket"],
    variables: ["Uptime (s)", "Heap libre (B)", "Bloque libre mayor (B)", "Tramas descartadas", "Antigüedad de la última trama (ms)"],
    procedure: ["Inicia la sesión y deja el sistema funcionando.", "Opcional: conecta y desconecta clientes.", "Detén y exporta."],
    honesty: "Todas las magnitudes las reporta el firmware o el navegador.",
  },
];

export interface SessionRow {
  t: number;
  values: Record<string, number | string | null>;
}

export interface SessionRecord {
  experiment: ExperimentId;
  startedAt: number;
  stoppedAt: number | null;
  demo: boolean;
  bootId: string | null;
  rows: SessionRow[];
}

export const MAX_SESSION_ROWS = 3000;

export function startSession(experiment: ExperimentId, snapshot: Snapshot, now: number): SessionRecord {
  return { experiment, startedAt: now, stoppedAt: null, demo: snapshot.demo, bootId: snapshot.realtimeStatus?.bootId ?? null, rows: [] };
}

/** Rows to append for this snapshot: only samples newer than the last row. */
export function sampleSession(record: SessionRecord, snapshot: Snapshot, now: number): SessionRow[] {
  const lastT = record.rows.at(-1)?.t ?? record.startedAt - 1;
  switch (record.experiment) {
    case "gravity":
      return snapshot.motionSamples.filter(m => m.at > lastT).map(m => {
        const tilt = tiltFromGravity(m.gravity);
        return {
          t: m.at,
          values: {
            gx: m.gravity.x,
            gy: m.gravity.y,
            gz: m.gravity.z,
            gMag: Math.hypot(m.gravity.x, m.gravity.y, m.gravity.z),
            pitchDeg: tilt ? round(tilt.pitchDeg, 1) : null,
            rollDeg: tilt ? round(tilt.rollDeg, 1) : null,
          },
        };
      });
    case "motion":
      return snapshot.motionSamples.filter(m => m.at > lastT).map(m => ({
        t: m.at,
        values: { ax: m.linear.x, ay: m.linear.y, az: m.linear.z, aMag: round(m.linearMs2, 3), aG: round(toG(m.linearMs2), 3), state: m.state },
      }));
    case "stability": {
      const s = snapshot.realtimeStatus;
      if (!s || snapshot.lastPollAt === null || snapshot.lastPollAt <= lastT) return [];
      const lastFrame = snapshot.clientStats?.lastFrameAt ?? null;
      return [{
        t: snapshot.lastPollAt,
        values: {
          uptimeS: round(s.uptimeMs / 1000, 1),
          freeHeap: s.freeHeapBytes,
          largestBlock: s.largestFreeBlockBytes,
          droppedFrames: s.droppedFrames,
          clients: s.clientCount,
          frameAgeMs: lastFrame === null ? null : Math.max(0, now - lastFrame),
        },
      }];
    }
    case "rgb": {
      const rows: SessionRow[] = [];
      for (const m of snapshot.motionSamples) if (m.at > lastT) rows.push({ t: m.at, values: { kind: "motion", aMag: round(m.linearMs2, 3) } });
      for (const c of snapshot.rgbLog) if (c.at > lastT) rows.push({ t: c.at, values: { kind: "rgb", mode: c.mode, r: c.r, g: c.g, b: c.b } });
      return rows.sort((a, b) => a.t - b.t);
    }
    case "coverage":
      // Coverage uses the firmware lab API; the view records its responses.
      return [];
  }
}

export function appendRows(record: SessionRecord, rows: SessionRow[]): SessionRecord {
  if (rows.length === 0) return record;
  const merged = record.rows.concat(rows);
  return { ...record, rows: merged.length > MAX_SESSION_ROWS ? merged.slice(merged.length - MAX_SESSION_ROWS) : merged };
}

export function stopSession(record: SessionRecord, now: number): SessionRecord {
  return { ...record, stoppedAt: now };
}

export function numericColumn(record: SessionRecord, key: string): number[] {
  return record.rows.map(r => r.values[key]).filter((v): v is number => typeof v === "number" && Number.isFinite(v));
}

export function columnStats(record: SessionRecord, key: string): SeriesStats | null {
  return seriesStats(numericColumn(record, key));
}

export interface RgbLatency {
  motionAt: number;
  ledAt: number;
  latencyMs: number;
}

/**
 * Pairs each motion onset (|a| crossing the threshold after a quiet sample)
 * with the first later LED change into "motion" mode within the window.
 * Resolution is bounded by the HTTP polling interval.
 */
export function rgbLatencies(motion: readonly Pick<MotionPoint, "at" | "linearMs2">[], rgb: readonly Pick<RgbChange, "at" | "mode">[], threshold = 2.5, windowMs = 8000): RgbLatency[] {
  const out: RgbLatency[] = [];
  let quiet = true;
  for (const m of motion) {
    const above = m.linearMs2 >= threshold;
    if (above && quiet) {
      const change = rgb.find(c => c.mode === "motion" && c.at >= m.at && c.at - m.at <= windowMs);
      if (change && !out.some(l => l.ledAt === change.at)) out.push({ motionAt: m.at, ledAt: change.at, latencyMs: change.at - m.at });
    }
    quiet = !above;
  }
  return out;
}

function csvCell(value: unknown): string {
  if (value === null || value === undefined) return "";
  const text = String(value);
  return /[",\n\r]/.test(text) ? `"${text.replace(/"/g, '""')}"` : text;
}

export function sessionToCsv(record: SessionRecord): string {
  const keys: string[] = [];
  for (const row of record.rows) for (const k of Object.keys(row.values)) if (!keys.includes(k)) keys.push(k);
  const header = ["iso_time", "t_ms", "demo", ...keys];
  const lines = [header.join(",")];
  for (const row of record.rows) {
    lines.push([new Date(row.t).toISOString(), row.t - record.startedAt, record.demo ? 1 : 0, ...keys.map(k => row.values[k])].map(csvCell).join(","));
  }
  return lines.join("\r\n") + "\r\n";
}

export function sessionToJson(record: SessionRecord): string {
  const definition = EXPERIMENTS.find(e => e.id === record.experiment);
  return JSON.stringify(
    {
      kind: "sentinel-lab.experiment",
      schemaVersion: 1,
      demo: record.demo,
      experiment: definition ? { id: definition.id, name: definition.name, objective: definition.objective, honesty: definition.honesty } : record.experiment,
      startedAt: new Date(record.startedAt).toISOString(),
      stoppedAt: record.stoppedAt === null ? null : new Date(record.stoppedAt).toISOString(),
      bootId: record.bootId,
      rows: record.rows.map(r => ({ t: new Date(r.t).toISOString(), ...r.values })),
    },
    null,
    2,
  );
}

function round(value: number, digits: number): number {
  const f = 10 ** digits;
  return Math.round(value * f) / f;
}
