import { useMemo, useState } from "react";
import { Antenna } from "lucide-react";
import { Badge } from "@/components/arc/badge/badge";
import { EmptyState } from "@/components/arc/empty-state/empty-state";
import { LineChart, type LineChartDatum, type LineChartSeries } from "@/components/arc/line-chart/line-chart";
import SegmentedControl from "@/components/arc/segmented-control/segmented-control";
import { SortableDataTable, type DataColumn } from "@/components/arc/sortable-data-table/sortable-data-table";
import { Sparkline } from "@/components/arc/sparkline/sparkline";
import { formatClock, formatDbm, formatDuration } from "@/lib/format";
import type { Snapshot } from "@/lib/store";
import { Panel } from "@/ui/Panel";

type Source = "websocket" | "http";

interface ClientRow extends Record<string, unknown> {
  id: string;
  clientId: number;
  connected: boolean;
  rssi: number | null;
  filtered: number | null;
  lastSeen: string;
  distance: string;
}

const SERIES_COLORS = ["var(--series-1)", "var(--series-2)", "var(--series-3)", "var(--series-4)"];

export function Telemetry({ snapshot }: { snapshot: Snapshot }) {
  const hasWs = snapshot.rf !== null;
  const hasHttp = (snapshot.rfSnapshot?.devices.length ?? 0) > 0;
  const [preferred, setPreferred] = useState<Source>("websocket");
  const source: Source = preferred === "websocket" && hasWs ? "websocket" : hasHttp ? "http" : hasWs ? "websocket" : preferred;

  const { data, series } = useMemo(() => buildChart(snapshot, source), [snapshot, source]);
  const rows = useMemo(() => buildRows(snapshot, source), [snapshot, source]);

  const columns: DataColumn<ClientRow>[] = [
    { key: "clientId", label: "Cliente", sortable: true, numeric: true, render: v => <code>#{String(v)}</code> },
    {
      key: "connected",
      label: "Estado",
      sortable: true,
      render: v => (
        <Badge tone={v ? "success" : "neutral"} dot size="sm" pill>
          {v ? "Conectado" : "Desconectado"}
        </Badge>
      ),
    },
    { key: "rssi", label: "RSSI", sortable: true, numeric: true, render: v => formatDbm(v as number | null) },
    { key: "filtered", label: "RSSI filtrado", sortable: true, numeric: true, render: v => formatDbm(v as number | null) },
    { key: "lastSeen", label: "Visto por última vez", sortable: true },
    { key: "distance", label: "Distancia estimada" },
  ];

  if (!hasWs && !hasHttp) {
    return (
      <div className="view">
        <EmptyState
          icon={<Antenna size={20} />}
          title="Sin telemetría RF todavía"
          description={
            snapshot.realtimeStatus && !snapshot.realtimeStatus.clockSynced
              ? "El WebSocket no emite hasta que se sincronice el reloj UTC, y /api/rf aún no lista clientes asociados al AP."
              : "Ningún cliente asociado al AP SENTINEL-LAB. Conecte un dispositivo autorizado para observar su RSSI."
          }
          label="Telemetría vacía"
        />
      </div>
    );
  }

  return (
    <div className="view">
      <Panel
        title="RSSI por cliente"
        subtitle={
          source === "websocket"
            ? `Protocol v1 rf.telemetry · ${snapshot.demo ? "DEMO" : "tiempo real"} · canal ${snapshot.rf?.channel ?? "—"}`
            : "Historial HTTP /api/rf (tiempos relativos al arranque del ESP32)"
        }
        action={
          <SegmentedControl
            label="Fuente de datos"
            value={source}
            onValueChange={value => setPreferred(value as Source)}
            options={[
              { value: "websocket", label: "WebSocket" },
              { value: "http", label: "HTTP" },
            ]}
          />
        }
      >
        <LineChart
          data={data}
          series={series}
          label="RSSI en dBm"
          unit=" dBm"
          height={260}
          curve="linear"
          categoryLabel="Hora"
          emptyLabel="Sin muestras en esta fuente"
          formatValue={value => `${Math.round(value)} dBm`}
          formatTick={value => `${Math.round(value)}`}
        />
        <p className="muted" style={{ marginTop: 10 }}>
          El RSSI no determina una distancia exacta. Las distancias solo se muestran cuando el firmware indica un modelo calibrado con medición física real.
        </p>
      </Panel>

      <Panel title="Clientes asociados" subtitle={`${rows.length} en la última lectura`}>
        <SortableDataTable<ClientRow>
          rows={rows}
          columns={columns}
          rowKey="id"
          caption="Clientes Wi-Fi observados por el ESP32"
          emptyMessage="Sin clientes"
          defaultSort={{ key: "clientId", direction: "asc" }}
          itemName={{ one: "cliente", other: "clientes" }}
        />
      </Panel>

      {source === "websocket" && (
        <Panel title="Tendencia individual" subtitle="Últimas muestras por cliente">
          <div className="sparks">
            {Object.entries(snapshot.rssiHistory).map(([clientId, points], index) => {
              const values = points.map(p => p.rssi).filter((v): v is number => v !== null);
              if (values.length < 2) return null;
              const last = values[values.length - 1] ?? 0;
              return (
                <Sparkline
                  key={clientId}
                  data={values}
                  label={`Cliente #${clientId}`}
                  value={formatDbm(last)}
                  change={`${values.length} muestras`}
                  tone={last < -80 ? "warning" : index % 2 ? "success" : "accent"}
                  width={220}
                  height={56}
                  formatValue={v => formatDbm(v)}
                />
              );
            })}
          </div>
        </Panel>
      )}
    </div>
  );
}

function buildChart(snapshot: Snapshot, source: Source): { data: LineChartDatum[]; series: LineChartSeries[] } {
  const byTime = new Map<number, Record<string, number | undefined>>();
  const ids: string[] = [];
  if (source === "websocket") {
    for (const [clientId, points] of Object.entries(snapshot.rssiHistory)) {
      ids.push(clientId);
      for (const point of points) {
        if (point.rssi === null) continue;
        const row = byTime.get(point.at) ?? {};
        row[clientId] = point.rssi;
        byTime.set(point.at, row);
      }
    }
  } else {
    for (const device of snapshot.rfSnapshot?.devices ?? []) {
      const clientId = String(device.id);
      ids.push(clientId);
      for (const point of device.history) {
        const row = byTime.get(point.t) ?? {};
        row[clientId] = point.rssi;
        byTime.set(point.t, row);
      }
    }
  }
  const uptime = snapshot.rfSnapshot?.uptimeMs ?? 0;
  const data = [...byTime.entries()]
    .sort(([a], [b]) => a - b)
    .slice(-120)
    .map(([at, values]) => {
      const label = source === "websocket" ? new Date(at).toLocaleTimeString("es", { hour12: false }) : `hace ${formatDuration(Math.max(0, uptime - at))}`;
      return { key: String(at), label, axisLabel: label, values };
    });
  const series = ids.map((id, index) => ({ key: id, label: `Cliente #${id}`, color: SERIES_COLORS[index % SERIES_COLORS.length] }));
  return { data, series };
}

function buildRows(snapshot: Snapshot, source: Source): ClientRow[] {
  if (source === "websocket" && snapshot.rf) {
    return snapshot.rf.clients.map(client => ({
      id: String(client.clientId),
      clientId: client.clientId,
      connected: client.connected,
      rssi: client.rssiDbm,
      filtered: client.filteredRssiDbm ?? null,
      lastSeen: formatClock(client.lastSeen),
      distance: client.estimatedDistance
        ? client.estimatedDistance.calibrated
          ? `≈ ${client.estimatedDistance.valueM.toFixed(2)} m (estimación)`
          : "Modelo sin calibrar"
        : "No disponible",
    }));
  }
  const uptime = snapshot.rfSnapshot?.uptimeMs ?? 0;
  return (snapshot.rfSnapshot?.devices ?? []).map(device => ({
    id: String(device.id),
    clientId: device.id,
    connected: device.connected,
    rssi: device.rssiDbm,
    filtered: device.filteredDbm,
    lastSeen: `hace ${formatDuration(Math.max(0, uptime - device.lastSeenMs))}`,
    distance: "Solo vía Protocol v1",
  }));
}
