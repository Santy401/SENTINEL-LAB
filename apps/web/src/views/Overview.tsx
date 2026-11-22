import { Progress } from "@/components/arc/progress/progress";
import { Clock, Cpu, Wifi } from "lucide-react";
import { Alert } from "@/components/arc/alert/alert";
import { EmptyState } from "@/components/arc/empty-state/empty-state";
import { Gauge } from "@/components/arc/gauge/gauge";
import { MetricCard } from "@/components/arc/metric-card/metric-card";
import { Skeleton } from "@/components/arc/skeleton/skeleton";
import { Sparkline } from "@/components/arc/sparkline/sparkline";
import { formatBytes, formatDuration } from "@/lib/format";
import { healthScore } from "@/lib/health";
import type { Snapshot } from "@/lib/store";
import { KeyValues, Panel } from "@/ui/Panel";
import { LevelBadge } from "@/ui/levels";

// CONFIG_ARDUINO_LOOP_STACK_SIZE of the Arduino-ESP32 core used by the firmware.
const LOOP_STACK_BYTES = 8192;

export function Overview({ snapshot }: { snapshot: Snapshot }) {
  const status = snapshot.realtimeStatus;
  const device = snapshot.deviceState;
  const rf = snapshot.rfSnapshot;
  const score = healthScore(snapshot.health);

  if (snapshot.loading && !status) {
    return (
      <div className="view">
        <div className="grid grid-4">
          {[0, 1, 2, 3].map(i => (
            <div className="panel" key={i}>
              <Skeleton label="Cargando estado del dispositivo" lines={3} />
            </div>
          ))}
        </div>
      </div>
    );
  }

  if (!status) {
    return (
      <div className="view">
        <Alert tone="danger" title="No se puede leer el ESP32">
          {snapshot.httpError ?? "Sin respuesta"}. Compruebe que este equipo está en la red SENTINEL-LAB y que el gateway o el proxy apuntan a http://192.168.4.1.
        </Alert>
        <EmptyState icon={<Wifi size={20} />} title="Sin datos del dispositivo" description="El panel mostrará el estado en cuanto /api/realtime/status responda. Reintentando automáticamente." label="Estado vacío" />
      </div>
    );
  }

  const heapPoints = snapshot.heapHistory.map(p => Math.round(p.free / 1024));
  const stackUsed = status.minLoopStackFreeBytes !== null ? LOOP_STACK_BYTES - status.minLoopStackFreeBytes : null;

  return (
    <div className="view">
      {snapshot.httpError && (
        <Alert tone="danger" title="Lectura HTTP fallida">
          {snapshot.httpError}. Se muestran los últimos valores recibidos.
        </Alert>
      )}
      {!status.clockSynced && (
        <Alert tone="warning" title="Reloj UTC del ESP32 sin sincronizar">
          Protocol v1 exige marcas de tiempo UTC, así que el firmware no emite ninguna trama WebSocket hasta que un gateway autenticado o la app iOS con token
          aporten la hora. La web es un cliente de solo lectura y no puede sincronizarlo. La información HTTP sigue disponible.
        </Alert>
      )}

      <div className="grid grid-4">
        <MetricCard label="Heap libre" value={Math.round(status.freeHeapBytes / 1024)} suffix=" KiB" context={`mínimo ${formatBytes(status.minFreeHeapBytes)}`} />
        <MetricCard label="Clientes WebSocket" value={status.clientCount} context="de 4 permitidos" />
        <MetricCard label="Clientes Wi-Fi" value={rf?.clients ?? device?.wifi.clients ?? 0} context={rf?.channel ? `canal ${rf.channel}` : "AP del laboratorio"} />
        <MetricCard label="Uptime" value={Math.floor(status.uptimeMs / 60000)} suffix=" min" context={formatDuration(status.uptimeMs)} />
      </div>

      <div className="grid grid-2">
        <Panel title="Salud del sistema" subtitle="Evaluación local de las lecturas del firmware" action={score !== null ? <LevelBadge level={score >= 90 ? "ok" : score >= 50 ? "warn" : "critical"} /> : undefined}>
          <div className="row" style={{ alignItems: "flex-start", gap: 24 }}>
            {score !== null && (
              <Gauge
                value={score}
                label="Índice de salud"
                detail={`${snapshot.health.filter(h => h.level === "ok").length}/${snapshot.health.length} correctos`}
                thresholds={[
                  { from: 0, tone: "danger", label: "Crítico" },
                  { from: 50, tone: "warning", label: "Atención" },
                  { from: 90, tone: "success", label: "Correcto" },
                ]}
              />
            )}
            <ul className="health-list" style={{ flex: 1, minWidth: 220 }}>
              {snapshot.health.map(item => (
                <li className="health-item" key={item.id}>
                  <LevelBadge level={item.level} label={item.label} />
                  <p>{item.detail}</p>
                </li>
              ))}
            </ul>
          </div>
        </Panel>

        <Panel title="Dispositivo" subtitle={snapshot.demo ? "DEMO · no es un dispositivo real" : "Lecturas HTTP del ESP32"} action={<Cpu size={18} aria-hidden="true" />}>
          <KeyValues
            items={[
              ["Firmware", `${status.firmwareVersion ?? status.realtimeVersion} (realtime ${status.realtimeVersion})`],
              ["API HTTP", status.httpApiVersion ?? status.fw],
              ["bootId", <code key="b">{status.bootId}</code>],
              ["Último reinicio", status.resetReason ?? "no informado por este firmware"],
              ["Reloj", status.clockSynced ? `sincronizado vía ${status.clockSource ?? "?"}${status.clockSyncAgeMs !== null ? `, hace ${formatDuration(status.clockSyncAgeMs)}` : ""}` : "sin sincronizar"],
              ["Red", rf ? `${rf.ssid ?? "—"} · ${rf.ip ?? "—"} · ${rf.mode ?? ""}` : device?.wifi.ssid ?? "—"],
              ["Movimiento", device ? `${device.motionState} · fuente ${device.accelSource}${device.simulation ? " (simulación)" : ""}` : "—"],
            ]}
          />
        </Panel>
      </div>

      <div className="grid grid-2">
        <Panel title="Memoria" subtitle={`${status.heapSamples} muestras en el firmware · cada 15 s`}>
          {heapPoints.length > 1 ? (
            <Sparkline data={heapPoints} label="Heap libre observado (KiB)" value={`${heapPoints[heapPoints.length - 1]} KiB`} change={`mín. ${Math.round(status.minFreeHeapBytes / 1024)} KiB`} tone={status.freeHeapBytes < 80 * 1024 ? "warning" : "accent"} width={420} height={72} formatValue={v => `${v} KiB`} />
          ) : (
            <p className="muted">Acumulando lecturas para la tendencia…</p>
          )}
          <p className="muted" style={{ marginTop: 10 }}>
            Bloque contiguo mayor: {formatBytes(status.largestFreeBlockBytes)}
          </p>
        </Panel>
        <Panel title="Pila del loop principal" subtitle="Marca de agua mínima de la tarea Arduino (8 KiB)" action={<Clock size={18} aria-hidden="true" />}>
          {stackUsed !== null ? (
            <>
              <Progress value={stackUsed} max={LOOP_STACK_BYTES} label="Pila usada en el peor caso" showValue />
              <p className="muted">
                {stackUsed} B usados de {LOOP_STACK_BYTES} B · {status.minLoopStackFreeBytes} B nunca usados
              </p>
            </>
          ) : (
            <p className="muted">Este firmware no expone la marca de agua de la pila (requiere v0.4.1 o superior).</p>
          )}
        </Panel>
      </div>
    </div>
  );
}
