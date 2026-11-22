import { useMemo, useState } from "react";
import { Activity, Radio, Signal, SignalLow, Unplug, Wifi } from "lucide-react";
import { EmptyState } from "@/components/arc/empty-state/empty-state";
import SegmentedControl from "@/components/arc/segmented-control/segmented-control";
import { Timeline, type TimelineEvent } from "@/components/arc/timeline/timeline";
import { formatDbm } from "@/lib/format";
import type { Snapshot, TimelineItem } from "@/lib/store";
import { Panel } from "@/ui/Panel";

const TITLES: Record<string, string> = {
  DEVICE_CONNECTED: "Cliente conectado",
  DEVICE_DISCONNECTED: "Cliente desconectado",
  SIGNAL_WEAK: "Señal débil",
  SIGNAL_RECOVERED: "Señal recuperada",
  COVERAGE_LIMIT_REACHED: "Límite de cobertura",
  MOVEMENT_DETECTED: "Movimiento detectado",
  MOTION_ALERT: "Alerta de movimiento",
  SENSOR_OFFLINE: "Sensor sin señal",
  SENSOR_RECOVERED: "Sensor recuperado",
  HEARTBEAT: "Latido",
};

function tone(name: string): TimelineEvent["tone"] {
  if (name === "SIGNAL_WEAK" || name === "DEVICE_DISCONNECTED" || name === "MOTION_ALERT" || name === "SENSOR_OFFLINE" || name === "COVERAGE_LIMIT_REACHED") return "danger";
  if (name === "DEVICE_CONNECTED" || name === "SIGNAL_RECOVERED" || name === "SENSOR_RECOVERED") return "success";
  return "neutral";
}

function icon(name: string) {
  switch (name) {
    case "DEVICE_CONNECTED":
      return <Wifi size={14} />;
    case "DEVICE_DISCONNECTED":
      return <Unplug size={14} />;
    case "SIGNAL_WEAK":
      return <SignalLow size={14} />;
    case "SIGNAL_RECOVERED":
      return <Signal size={14} />;
    case "HEARTBEAT":
      return <Radio size={14} />;
    default:
      return <Activity size={14} />;
  }
}

type Filter = "all" | "rf" | "motion";

export function Events({ snapshot }: { snapshot: Snapshot }) {
  const [filter, setFilter] = useState<Filter>("all");
  const events = useMemo(() => toTimeline(snapshot.timeline, filter), [snapshot.timeline, filter]);
  const heartbeats = snapshot.timeline.filter(item => item.name === "HEARTBEAT").length;

  return (
    <div className="view">
      <Panel
        title="Eventos recientes"
        subtitle={`${snapshot.timeline.length} en memoria (máx. 100) · ${heartbeats} latidos ocultos`}
        action={
          <SegmentedControl
            label="Filtrar eventos"
            value={filter}
            onValueChange={value => setFilter(value as Filter)}
            options={[
              { value: "all", label: "Todos" },
              { value: "rf", label: "RF" },
              { value: "motion", label: "Movimiento" },
            ]}
          />
        }
      >
        {events.length ? (
          <Timeline events={events} now={Date.now()} label="Eventos del ESP32" timeZone={Intl.DateTimeFormat().resolvedOptions().timeZone} locale="es" maxHeight={560} />
        ) : (
          <EmptyState icon={<Activity size={20} />} title="Sin eventos" description="Los eventos llegan por WebSocket (Protocol v1) o desde /api/events. Los eventos simulados se descartan." label="Sin eventos" />
        )}
      </Panel>
    </div>
  );
}

const RF_EVENTS = new Set(["DEVICE_CONNECTED", "DEVICE_DISCONNECTED", "SIGNAL_WEAK", "SIGNAL_RECOVERED", "COVERAGE_LIMIT_REACHED"]);

function toTimeline(items: TimelineItem[], filter: Filter): TimelineEvent[] {
  return items
    .filter(item => item.name !== "HEARTBEAT")
    .filter(item => (filter === "all" ? true : filter === "rf" ? RF_EVENTS.has(item.name) : !RF_EVENTS.has(item.name)))
    .slice()
    .sort((a, b) => b.at - a.at)
    .map(item => {
      const parts = [item.clientId !== null ? `cliente #${item.clientId}` : null, item.rssiDbm !== null ? formatDbm(item.rssiDbm) : null, item.origin === "http" ? "vía HTTP (hora aproximada)" : "vía WebSocket"].filter(Boolean);
      return {
        id: item.id,
        at: item.at,
        title: TITLES[item.name] ?? item.name,
        meta: parts.join(" · "),
        detail: item.message ?? undefined,
        tone: tone(item.name),
        icon: icon(item.name),
      };
    });
}
