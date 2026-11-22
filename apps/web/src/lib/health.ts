import type { RealtimeStatus } from "./api";

export type HealthLevel = "ok" | "warn" | "critical" | "unknown";

export interface HealthIndicator {
  id: string;
  label: string;
  level: HealthLevel;
  detail: string;
}

export interface HealthInput {
  status: RealtimeStatus | null;
  previous: RealtimeStatus | null;
  httpError: string | null;
  socketState: "idle" | "connecting" | "open" | "closed" | "demo";
}

// Thresholds are conservative for the ESP32-S3 internal heap (~320 KiB) and
// the 8 KiB Arduino loop stack. They flag trends; they are not limits measured
// on hardware.
export const HEAP_WARN_BYTES = 80 * 1024;
export const HEAP_CRITICAL_BYTES = 40 * 1024;
export const BLOCK_WARN_BYTES = 16 * 1024;
export const STACK_WARN_BYTES = 2048;
export const STACK_CRITICAL_BYTES = 1024;

const ABNORMAL_RESETS = new Set(["PANIC", "INT_WDT", "TASK_WDT", "WDT", "BROWNOUT"]);

function increased(current: number | null, previous: number | null | undefined): number {
  if (current === null || previous === null || previous === undefined) return 0;
  return current > previous ? current - previous : 0;
}

export function evaluateHealth({ status, previous, httpError, socketState }: HealthInput): HealthIndicator[] {
  const out: HealthIndicator[] = [];
  if (!status) {
    out.push({
      id: "http",
      label: "API HTTP",
      level: httpError ? "critical" : "unknown",
      detail: httpError ? `Sin respuesta de /api/realtime/status: ${httpError}` : "Esperando la primera lectura",
    });
    return out;
  }
  out.push({ id: "http", label: "API HTTP", level: "ok", detail: "El firmware responde" });

  out.push(
    status.clockSynced
      ? { id: "clock", label: "Reloj UTC", level: "ok", detail: `Sincronizado (fuente: ${status.clockSource ?? "desconocida"})` }
      : {
          id: "clock",
          label: "Reloj UTC",
          level: "warn",
          detail: "No sincronizado: el WebSocket no emite datos hasta que un gateway o la app iOS autenticada aporte la hora",
        },
  );

  const heap = status.freeHeapBytes;
  out.push({
    id: "heap",
    label: "Heap libre",
    level: heap < HEAP_CRITICAL_BYTES ? "critical" : heap < HEAP_WARN_BYTES ? "warn" : "ok",
    detail: `${heap} B libres, mínimo observado ${status.minFreeHeapBytes} B`,
  });

  if (status.largestFreeBlockBytes !== null) {
    out.push({
      id: "fragmentation",
      label: "Bloque libre mayor",
      level: status.largestFreeBlockBytes < BLOCK_WARN_BYTES ? "warn" : "ok",
      detail: `${status.largestFreeBlockBytes} B contiguos`,
    });
  }

  if (status.minLoopStackFreeBytes !== null) {
    const stack = status.minLoopStackFreeBytes;
    out.push({
      id: "stack",
      label: "Pila del loop",
      level: stack < STACK_CRITICAL_BYTES ? "critical" : stack < STACK_WARN_BYTES ? "warn" : "ok",
      detail: `${stack} B nunca usados (marca de agua mínima)`,
    });
  }

  if (status.resetReason) {
    out.push({
      id: "reset",
      label: "Último reinicio",
      level: ABNORMAL_RESETS.has(status.resetReason) ? "warn" : "ok",
      detail: ABNORMAL_RESETS.has(status.resetReason) ? `Causa anómala: ${status.resetReason}` : `Causa: ${status.resetReason}`,
    });
  }

  if (previous && previous.bootId !== status.bootId) {
    out.push({ id: "reboot", label: "Reinicio detectado", level: "warn", detail: `bootId cambió de ${previous.bootId} a ${status.bootId}` });
  }

  const drops =
    increased(status.droppedFrames, previous?.droppedFrames) + increased(status.droppedTelemetry, previous?.droppedTelemetry);
  const rejected = increased(status.rejectedMessages, previous?.rejectedMessages);
  out.push({
    id: "transport",
    label: "Transporte",
    level: drops || rejected ? "warn" : "ok",
    detail:
      drops || rejected
        ? `Desde la lectura anterior: +${drops} descartes, +${rejected} rechazos`
        : `Totales: ${status.droppedFrames} tramas descartadas, ${status.rejectedMessages} rechazadas`,
  });

  out.push({
    id: "socket",
    label: "WebSocket",
    level: socketState === "open" ? "ok" : socketState === "demo" ? "unknown" : "warn",
    detail:
      socketState === "open"
        ? "Conectado a sentinel.v1"
        : socketState === "demo"
          ? "Modo demostración"
          : socketState === "connecting"
            ? "Conectando…"
            : "Sin conexión en tiempo real",
  });
  return out;
}

export function overallLevel(indicators: HealthIndicator[]): HealthLevel {
  if (indicators.some(i => i.level === "critical")) return "critical";
  if (indicators.some(i => i.level === "warn")) return "warn";
  if (indicators.length && indicators.every(i => i.level === "ok")) return "ok";
  return "unknown";
}

/** 0–100 score for the health gauge: ok=1, warn=0.5, critical=0; unknown ignored. */
export function healthScore(indicators: HealthIndicator[]): number | null {
  const scored = indicators.filter(i => i.level !== "unknown");
  if (!scored.length) return null;
  const sum = scored.reduce((acc, i) => acc + (i.level === "ok" ? 1 : i.level === "warn" ? 0.5 : 0), 0);
  return Math.round((sum / scored.length) * 100);
}
