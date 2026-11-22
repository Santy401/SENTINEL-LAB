import { useMemo } from "react";
import { Download } from "lucide-react";
import { Alert } from "@/components/arc/alert/alert";
import { Badge } from "@/components/arc/badge/badge";
import { Button } from "@/components/arc/button/button";
import { CopyButton } from "@/components/arc/copy-button/copy-button";
import { DonutChart } from "@/components/arc/donut-chart/donut-chart";
import { JsonViewer } from "@/components/arc/json-viewer/json-viewer";
import { Progress } from "@/components/arc/progress/progress";
import { useToastStack } from "@/components/arc/toast-stack/toast-stack";
import { buildEvidence } from "@/lib/evidence";
import { formatBytes, formatDuration } from "@/lib/format";
import type { Snapshot } from "@/lib/store";
import { KeyValues, Panel } from "@/ui/Panel";

export function Diagnostics({ snapshot, appVersion }: { snapshot: Snapshot; appVersion: string }) {
  const status = snapshot.realtimeStatus;
  const stats = snapshot.clientStats;
  const { toast } = useToastStack();

  const evidence = useMemo(
    () =>
      buildEvidence({
        appVersion,
        demo: snapshot.demo,
        wsUrl: snapshot.wsUrl,
        generatedAt: new Date(snapshot.lastPollAt ?? Date.now()),
        realtimeStatus: status,
        deviceState: snapshot.deviceState,
        gatewayStatus: snapshot.gatewayStatus,
        health: snapshot.health,
        counters: { ...(stats ?? {}), ...snapshot.frameCounts } as unknown as Record<string, number>,
        recentFrames: snapshot.frames,
        recentEvents: snapshot.timeline.slice(-30),
      }),
    [appVersion, snapshot, status, stats],
  );
  const evidenceText = useMemo(() => JSON.stringify(evidence, null, 2), [evidence]);

  const download = () => {
    const blob = new Blob([evidenceText], { type: "application/json" });
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    const stamp = new Date().toISOString().replace(/[:.]/g, "-");
    link.href = url;
    link.download = `sentinel-evidence-${snapshot.demo ? "DEMO-" : ""}${stamp}.json`;
    link.click();
    URL.revokeObjectURL(url);
    toast({ type: "success", title: "Evidencia exportada", description: snapshot.demo ? "Marcada como DEMO" : "Sin credenciales ni secretos" });
  };

  const donut = Object.entries(snapshot.frameCounts).map(([key, value]) => ({ key, label: key, value }));

  const firmwareCounters: [string, number | null][] = status
    ? [
        ["Tramas descartadas", status.droppedFrames],
        ["Mensajes rechazados", status.rejectedMessages],
        ["Telemetría descartada", status.droppedTelemetry],
        ["Eventos simulados omitidos", status.skippedSimulatedEvents],
        ["Timeouts de handshake", status.handshakeTimeouts],
        ["Timeouts por inactividad", status.idleTimeouts],
        ["Cierres remotos (FIN)", status.remoteCloses],
      ]
    : [];
  const clientCounters: [string, number][] = stats
    ? [
        ["Tramas válidas recibidas", stats.framesReceived],
        ["Tramas inválidas", stats.framesInvalid],
        ["Tipos desconocidos", stats.framesUnknownType],
        ["Tramas binarias", stats.framesBinary],
        ["Tramas enviadas", stats.framesSent],
        ["Reconexiones", stats.reconnects],
        ["Cierres por watchdog", stats.watchdogCloses],
      ]
    : [];

  return (
    <div className="view">
      {snapshot.lastServerError && (
        <Alert tone="warning" title={`Último error del firmware: ${snapshot.lastServerError.code}`}>
          {snapshot.lastServerError.message}
        </Alert>
      )}
      <div className="grid grid-2">
        <Panel title="Firmware y memoria" subtitle={snapshot.demo ? "DEMO" : "GET /api/realtime/status"}>
          {status ? (
            <KeyValues
              items={[
                ["Versión del firmware", status.firmwareVersion ?? "—"],
                ["Versión realtime", status.realtimeVersion],
                ["API HTTP (fw)", `${status.httpApiVersion ?? status.fw} · compatibilidad v0.3.1`],
                ["Causa del último reinicio", status.resetReason ?? "no informada"],
                ["Uptime", formatDuration(status.uptimeMs)],
                ["Heap libre / mínimo", `${formatBytes(status.freeHeapBytes)} / ${formatBytes(status.minFreeHeapBytes)}`],
                ["Bloque contiguo mayor", formatBytes(status.largestFreeBlockBytes)],
                ["Pila libre mínima (loop)", formatBytes(status.minLoopStackFreeBytes)],
                ["Muestras de heap", String(status.heapSamples)],
                ["Reloj", status.clockSynced ? `sincronizado (${status.clockSource ?? "?"})` : "sin sincronizar"],
              ]}
            />
          ) : (
            <p className="muted">Sin lectura del firmware.</p>
          )}
          {status && (
            <div style={{ marginTop: 16 }}>
              <Progress value={status.clientCount} max={4} label="Ranuras WebSocket ocupadas" showValue />
            </div>
          )}
        </Panel>

        <Panel title="Comunicaciones" subtitle={`WebSocket ${snapshot.wsUrl}`}>
          <div className="counter-grid">
            {[...firmwareCounters, ...clientCounters].map(([label, value]) => (
              <div className="counter" key={label}>
                <span>{label}</span>
                <strong>{value ?? "—"}</strong>
              </div>
            ))}
          </div>
          {stats?.lastInvalidReason && <p className="muted" style={{ marginTop: 12 }}>Último rechazo local: {stats.lastInvalidReason}</p>}
          {stats?.nextRetryMs !== null && stats?.nextRetryMs !== undefined && snapshot.socketState !== "open" && (
            <p className="muted" style={{ marginTop: 4 }}>Próximo intento en {Math.round(stats.nextRetryMs / 1000)} s (último cierre {stats.lastCloseCode ?? "—"})</p>
          )}
        </Panel>
      </div>

      <div className="grid grid-2">
        <Panel title="Tramas Protocol v1 por tipo" subtitle="Acumulado en esta sesión del navegador">
          <DonutChart data={donut} label="Tramas recibidas" totalLabel="Tramas" otherLabel="Otras" emptyLabel="Ninguna trama recibida todavía" size={196} />
        </Panel>
        <Panel
          title="Gateway"
          subtitle={snapshot.gatewayVersion ? `sentinel_gateway ${snapshot.gatewayVersion}` : "No detectado"}
          action={
            snapshot.gatewayStatus ? (
              <Badge tone={snapshot.gatewayStatus.state === "connected" ? "success" : "warning"} dot pill size="sm">
                {snapshot.gatewayStatus.state}
              </Badge>
            ) : undefined
          }
        >
          {snapshot.gatewayStatus ? (
            <KeyValues
              items={[
                ["Autoridad de reloj", snapshot.gatewayStatus.clockAuthority ? "sí (token configurado)" : "no"],
                ["Última sincronización", snapshot.gatewayStatus.lastSyncAt ?? "—"],
                ["Último error", snapshot.gatewayStatus.lastError ?? "—"],
                ["Reconexiones", String(snapshot.gatewayStatus.reconnects)],
                ["Tramas recibidas", String(snapshot.gatewayStatus.framesReceived)],
                ["Uptime", formatDuration(snapshot.gatewayStatus.uptimeS * 1000)],
              ]}
            />
          ) : (
            <p className="muted">
              {snapshot.demo
                ? "No aplica en modo DEMO."
                : "La página no se sirve desde el gateway o /gateway/status no responde. Sin gateway autenticado ni app iOS con token, el reloj del ESP32 no se sincroniza."}
            </p>
          )}
        </Panel>
      </div>

      <Panel
        title="Evidencia no sensible"
        subtitle="Instantánea en JSON para informes. Se eliminan los campos con nombres de credenciales."
        action={
          <div className="row">
            <CopyButton value={evidenceText} label="Copiar" />
            <Button size="sm" onClick={download}>
              <Download size={14} aria-hidden="true" /> Exportar
            </Button>
          </div>
        }
      >
        <JsonViewer data={evidence} rootName="evidencia" defaultExpandDepth={1} searchable maxHeight={420} label="Evidencia exportable" />
      </Panel>
    </div>
  );
}
