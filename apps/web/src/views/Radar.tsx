import { useMemo, useState } from "react";
import SegmentedControl from "@/components/arc/segmented-control/segmented-control";
import { Select } from "@/components/arc/select/select";
import { DEFAULT_RSSI_AT_1M, DEFAULT_SENSITIVITY_DBM, PATH_LOSS_PRESETS, distanceFromRssi, rssiAtDistance, theoreticalRangeM } from "@/lib/physics";
import { radarClients, type RadarClient } from "@/lib/radar";
import type { Snapshot } from "@/lib/store";
import { Panel } from "@/ui/Panel";
import { TraceChart } from "@/ui/TraceChart";
import { useNow, useReducedMotion } from "@/ui/hooks";

const RINGS = [-40, -55, -65, -75, -85];
const CLIENT_COLORS = ["#38bdf8", "#f59e0b", "#a78bfa", "#34d399", "#f472b6", "#fb7185", "#facc15", "#60a5fa"];

/** Radar geometry: maps a 0..1 radius to SVG units around (0,0). */
export function radarPoint(angle: number, radius: number, size = 100): { x: number; y: number } {
  return { x: Math.cos(angle) * radius * size, y: Math.sin(angle) * radius * size };
}

export function Radar({ snapshot }: { snapshot: Snapshot }) {
  const now = useNow(1000);
  const reduced = useReducedMotion();
  const [mode, setMode] = useState("immersive");
  const [preset, setPreset] = useState<string>("open");
  const [focus, setFocus] = useState<string | null>(null);
  const n = PATH_LOSS_PRESETS.find(p => p.id === preset)?.n ?? 2.7;
  const clients = radarClients(snapshot, now);
  const range = theoreticalRangeM(n);
  const selected = clients.find(c => c.id === focus) ?? clients.find(c => c.connected) ?? null;
  const colors = useMemo(() => new Map(clients.map((c, i) => [c.id, CLIENT_COLORS[i % CLIENT_COLORS.length]!])), [clients]);
  const scientific = mode === "scientific";

  return (
    <div className="view radar-view">
      <div className="view-head">
        <div>
          <h2>Radar de señal</h2>
          <p>El ESP32 en el centro. Cuanto más fuerte llega la señal de un dispositivo, más cerca del centro aparece.</p>
        </div>
        <SegmentedControl
          label="Modo de visualización"
          value={mode}
          onValueChange={setMode}
          options={[
            { value: "immersive", label: "Inmersivo" },
            { value: "scientific", label: "Científico" },
          ]}
        />
      </div>

      <div className="radar-layout">
        <figure className={`radar${scientific ? " radar-scientific" : ""}${reduced ? " radar-still" : ""}`} aria-label="Radar de intensidad de señal">
          <svg viewBox="-110 -110 220 220" role="img" aria-label={`${clients.length} dispositivos en el radar`}>
            <defs>
              <radialGradient id="radarGlow">
                <stop offset="0%" stopColor="var(--radar-core)" stopOpacity="0.55" />
                <stop offset="100%" stopColor="var(--radar-core)" stopOpacity="0" />
              </radialGradient>
            </defs>
            <circle r="104" className="radar-disc" />
            {!scientific && !reduced && [0, 1, 2].map(i => <circle key={i} r="100" className="radar-wave" style={{ animationDelay: `${i * 1.2}s` }} />)}
            {RINGS.map(rssi => {
              const r = Math.min(1, Math.max(0.08, (-30 - rssi) / 65)) * 100;
              const d = distanceFromRssi(rssi, n);
              return (
                <g key={rssi}>
                  <circle r={r} className="radar-ring" />
                  <text x={3} y={-r + 7} className="radar-ring-label">
                    {rssi} dBm{scientific && d !== null ? ` · ≈${d < 10 ? d.toFixed(1) : d.toFixed(0)} m*` : ""}
                  </text>
                </g>
              );
            })}
            <line x1="-104" y1="0" x2="104" y2="0" className="radar-axis" />
            <line x1="0" y1="-104" x2="0" y2="104" className="radar-axis" />
            {!scientific && !reduced && <path d="M0,0 L104,0 A104,104 0 0,0 73.5,-73.5 Z" className="radar-sweep" />}
            <circle r="16" fill="url(#radarGlow)" />
            <rect x="-6" y="-4" width="12" height="8" rx="1.4" className="radar-esp" />
            <text y="15" textAnchor="middle" className="radar-esp-label">ESP32-S3</text>
            {clients.map(c => {
              const p = radarPoint(c.angle, c.radius);
              const color = colors.get(c.id)!;
              return (
                <g key={c.id} className={`radar-client${c.connected ? "" : " offline"}${selected?.id === c.id ? " focus" : ""}`} transform={`translate(${p.x.toFixed(2)} ${p.y.toFixed(2)})`} onClick={() => setFocus(c.id)} role="button" tabIndex={0} aria-label={`Cliente ${c.id}, ${c.filtered ?? c.rssi ?? "sin"} dBm`} onKeyDown={e => (e.key === "Enter" || e.key === " ") && setFocus(c.id)}>
                  {!scientific && c.connected && !reduced && <circle r="6" className="radar-ping" style={{ stroke: color }} />}
                  <circle r="3.4" style={{ fill: color }} />
                  <text x="5" y="-4" className="radar-client-label">
                    {c.id}
                  </text>
                </g>
              );
            })}
          </svg>
          <figcaption>
            Radio = intensidad medida (RSSI). <b>La posición angular no se mide</b>: cada dispositivo ocupa un ángulo fijo derivado de su identificador.
            {scientific && " * Distancias de los anillos según el modelo teórico seleccionado."}
          </figcaption>
        </figure>

        <div className="radar-side">
          <Panel title="Modelo de cobertura" subtitle="Conceptual: no sustituye a una medición">
            <Select label="Entorno (exponente n)" value={preset} onValueChange={setPreset} options={PATH_LOSS_PRESETS.map(p => ({ value: p.id, label: p.label }))} />
            <dl className="kv">
              <dt>Potencia a 1 m (A)</dt>
              <dd>{DEFAULT_RSSI_AT_1M} dBm (supuesta)</dd>
              <dt>Umbral de enlace</dt>
              <dd>{DEFAULT_SENSITIVITY_DBM} dBm</dd>
              <dt>Alcance teórico</dt>
              <dd>
                <b>≈ {range < 10 ? range.toFixed(1) : range.toFixed(0)} m</b> (orientativo)
              </dd>
              <dt>RSSI esperado a 5 m</dt>
              <dd>{rssiAtDistance(5, n).toFixed(0)} dBm</dd>
            </dl>
            <p className="note">Modelo log-distancia RSSI = A − 10·n·log₁₀(d). Paredes, cuerpos y la orientación de la antena cambian el resultado; las distancias reales solo salen de un experimento calibrado.</p>
          </Panel>
          <ClientDetail client={selected} n={n} color={selected ? colors.get(selected.id) ?? "#38bdf8" : "#38bdf8"} />
        </div>
      </div>

      <Panel title="Historial de intensidad" subtitle={clients[0]?.origin === "websocket" ? "Telemetría WebSocket (1 Hz)" : "Lectura HTTP /api/rf"}>
        <TraceChart
          label="RSSI por cliente"
          unit="dBm"
          min={-100}
          max={-20}
          guides={[{ value: -75, label: "aceptable" }]}
          traces={clients.map(c => ({ id: c.id, label: `cliente ${c.id}`, color: colors.get(c.id)!, values: c.history.slice(-120) }))}
        />
      </Panel>
    </div>
  );
}

function ClientDetail({ client, n, color }: { client: RadarClient | null; n: number; color: string }) {
  if (!client) {
    return (
      <Panel title="Dispositivo" subtitle="Sin clientes asociados">
        <p className="muted">Conecta un teléfono o portátil a la red SENTINEL-LAB para verlo en el radar.</p>
      </Panel>
    );
  }
  const value = client.filtered ?? client.rssi;
  const modelled = value === null ? null : distanceFromRssi(value, n);
  return (
    <Panel title={`Cliente ${client.id}`} subtitle={client.connected ? "Asociado al punto de acceso" : "Desconectado"}>
      <div className="client-swatch" style={{ background: color }} aria-hidden="true" />
      <dl className="kv">
        <dt>RSSI medido</dt>
        <dd>{client.rssi !== null ? `${client.rssi} dBm` : "—"}</dd>
        <dt>RSSI filtrado</dt>
        <dd>{client.filtered !== null ? `${client.filtered.toFixed(1)} dBm` : "—"}</dd>
        <dt>Calidad</dt>
        <dd>{client.band ?? "—"}</dd>
        <dt>Estabilidad (σ)</dt>
        <dd>{client.stabilityDb !== null ? `${client.stabilityDb.toFixed(1)} dB` : "—"}</dd>
        <dt>Distancia calibrada</dt>
        <dd>{client.distance?.calibrated ? <b>{client.distance.valueM.toFixed(2)} m (experimento)</b> : "Sin calibrar: usa Experimentos → Cobertura"}</dd>
        <dt>Distancia del modelo</dt>
        <dd>{modelled !== null ? `≈ ${modelled.toFixed(1)} m (teórica)` : "—"}</dd>
      </dl>
    </Panel>
  );
}
