import { useEffect, useMemo, useState } from "react";
import { Download, Play, Square } from "lucide-react";
import { Button } from "@/components/arc/button/button";
import { getJson, parseLab, postJson, type LabSnapshot } from "@/lib/api";
import { appendRows, columnStats, EXPERIMENTS, numericColumn, rgbLatencies, sampleSession, sessionToCsv, sessionToJson, startSession, stopSession, type ExperimentId, type SessionRecord } from "@/lib/experiments";
import { seriesStats } from "@/lib/physics";
import { radarClients } from "@/lib/radar";
import type { Snapshot } from "@/lib/store";
import { Panel } from "@/ui/Panel";
import { TraceChart } from "@/ui/TraceChart";

function download(name: string, text: string, type: string): void {
  const url = URL.createObjectURL(new Blob([text], { type }));
  const a = document.createElement("a");
  a.href = url;
  a.download = name;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

export function Experiments({ snapshot }: { snapshot: Snapshot }) {
  const [active, setActive] = useState<ExperimentId>("motion");
  const [record, setRecord] = useState<SessionRecord | null>(null);
  const definition = EXPERIMENTS.find(e => e.id === active)!;
  const running = record !== null && record.stoppedAt === null;

  // Append real samples from each store update while a session runs.
  useEffect(() => {
    if (!record || record.stoppedAt !== null) return;
    const rows = sampleSession(record, snapshot, Date.now());
    if (rows.length) setRecord(r => (r ? appendRows(r, rows) : r));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [snapshot]);

  const select = (id: ExperimentId) => {
    if (running) return;
    setActive(id);
    setRecord(null);
  };

  return (
    <div className="view experiments-view">
      <div className="view-head">
        <div>
          <h2>Experimentos</h2>
          <p>Sesiones reproducibles con objetivo, instrumentos, variables y datos exportables.</p>
        </div>
      </div>
      <div className="experiment-picker" role="tablist" aria-label="Experimentos">
        {EXPERIMENTS.map(e => (
          <button key={e.id} type="button" role="tab" aria-selected={active === e.id} disabled={running && active !== e.id} onClick={() => select(e.id)}>
            {e.name}
          </button>
        ))}
      </div>

      <div className="grid grid-2">
        <Panel title={definition.name} subtitle={definition.objective}>
          <dl className="kv">
            <dt>Instrumentos</dt>
            <dd>{definition.instruments.join(" · ")}</dd>
            <dt>Variables</dt>
            <dd>{definition.variables.join(" · ")}</dd>
          </dl>
          <ol className="procedure">
            {definition.procedure.map(step => (
              <li key={step}>{step}</li>
            ))}
          </ol>
          <p className="note">{definition.honesty}</p>
        </Panel>
        {active === "coverage" ? (
          <CoveragePanel snapshot={snapshot} />
        ) : (
          <SessionPanel snapshot={snapshot} record={record} running={running} onStart={() => setRecord(startSession(active, snapshot, Date.now()))} onStop={() => setRecord(r => (r ? stopSession(r, Date.now()) : r))} />
        )}
      </div>
      {record && active !== "coverage" && <SessionResults record={record} />}
    </div>
  );
}

function SessionPanel({ snapshot, record, running, onStart, onStop }: { snapshot: Snapshot; record: SessionRecord | null; running: boolean; onStart(): void; onStop(): void }) {
  const elapsed = record ? ((record.stoppedAt ?? Date.now()) - record.startedAt) / 1000 : 0;
  return (
    <Panel title="Sesión" subtitle={snapshot.demo ? "DEMO: los datos registrados serán sintéticos" : "Datos reales del dispositivo"}>
      <div className="session-controls">
        {running ? (
          <Button variant="danger" onClick={onStop}>
            <Square size={14} /> Detener
          </Button>
        ) : (
          <Button onClick={onStart}>
            <Play size={14} /> Iniciar
          </Button>
        )}
        {record && (
          <>
            <Button variant="secondary" onClick={() => download(`sentinel-${record.experiment}-${record.startedAt}.csv`, sessionToCsv(record), "text/csv")}>
              <Download size={14} /> CSV
            </Button>
            <Button variant="secondary" onClick={() => download(`sentinel-${record.experiment}-${record.startedAt}.json`, sessionToJson(record), "application/json")}>
              <Download size={14} /> JSON
            </Button>
          </>
        )}
      </div>
      <dl className="kv">
        <dt>Estado</dt>
        <dd>{running ? "Registrando…" : record ? "Finalizada" : "Lista"}</dd>
        <dt>Duración</dt>
        <dd>{elapsed.toFixed(1)} s</dd>
        <dt>Filas</dt>
        <dd>{record?.rows.length ?? 0}</dd>
        <dt>Arranque del ESP32</dt>
        <dd>{record?.bootId ?? snapshot.realtimeStatus?.bootId ?? "—"}</dd>
      </dl>
    </Panel>
  );
}

function SessionResults({ record }: { record: SessionRecord }) {
  const chart = useMemo(() => {
    switch (record.experiment) {
      case "gravity":
        return { unit: "°", traces: [{ id: "pitch", label: "cabeceo", color: "#60a5fa", values: numericColumn(record, "pitchDeg") }, { id: "roll", label: "alabeo", color: "#f59e0b", values: numericColumn(record, "rollDeg") }], stats: [["|g| (m/s²)", columnStats(record, "gMag")], ["Cabeceo (°)", columnStats(record, "pitchDeg")], ["Alabeo (°)", columnStats(record, "rollDeg")]] as const };
      case "motion":
        return { unit: "m/s²", traces: [{ id: "a", label: "|a|", color: "#38bdf8", values: numericColumn(record, "aMag") }], stats: [["|a| (m/s²)", columnStats(record, "aMag")], ["|a| (g)", columnStats(record, "aG")]] as const };
      case "stability":
        return { unit: "B", traces: [{ id: "heap", label: "heap libre", color: "#34d399", values: numericColumn(record, "freeHeap") }], stats: [["Heap libre (B)", columnStats(record, "freeHeap")], ["Antigüedad de trama (ms)", columnStats(record, "frameAgeMs")]] as const };
      default:
        return null;
    }
  }, [record]);

  if (record.experiment === "rgb") {
    const motion = record.rows.filter(r => r.values.kind === "motion").map(r => ({ at: r.t, linearMs2: Number(r.values.aMag) }));
    const rgb = record.rows.filter(r => r.values.kind === "rgb").map(r => ({ at: r.t, mode: (r.values.mode as string | null) ?? null }));
    const latencies = rgbLatencies(motion, rgb);
    const stats = seriesStats(latencies.map(l => l.latencyMs));
    return (
      <Panel title="Resultados: latencia RGB" subtitle="Cota superior limitada por el intervalo de lectura HTTP">
        {latencies.length === 0 ? (
          <p className="muted">Aún no hay pares agitación → cambio de LED. Agita el iPhone con fuerza tras unos segundos de reposo.</p>
        ) : (
          <>
            <dl className="kv">
              <dt>Pares detectados</dt>
              <dd>{latencies.length}</dd>
              <dt>Latencia mín / media / máx</dt>
              <dd>{stats ? `${stats.min.toFixed(0)} / ${stats.mean.toFixed(0)} / ${stats.max.toFixed(0)} ms` : "—"}</dd>
            </dl>
            <TraceChart label="Latencia por ensayo" unit="ms" min={0} traces={[{ id: "lat", label: "latencia", color: "#f59e0b", values: latencies.map(l => l.latencyMs) }]} />
          </>
        )}
      </Panel>
    );
  }
  if (!chart) return null;
  return (
    <Panel title="Resultados" subtitle={record.demo ? "DEMO · datos sintéticos" : "Datos medidos"}>
      <TraceChart label="Serie registrada" unit={chart.unit} traces={chart.traces} />
      <table className="stats-table">
        <thead>
          <tr>
            <th>Variable</th>
            <th>n</th>
            <th>Mínimo</th>
            <th>Media</th>
            <th>Máximo</th>
          </tr>
        </thead>
        <tbody>
          {chart.stats.map(([label, s]) => (
            <tr key={label}>
              <td>{label}</td>
              <td>{s?.count ?? 0}</td>
              <td>{s ? s.min.toFixed(2) : "—"}</td>
              <td>{s ? s.mean.toFixed(2) : "—"}</td>
              <td>{s ? s.max.toFixed(2) : "—"}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </Panel>
  );
}

function CoveragePanel({ snapshot }: { snapshot: Snapshot }) {
  const clients = radarClients(snapshot, Date.now()).filter(c => c.connected);
  const [lab, setLab] = useState<LabSnapshot | null>(null);
  const [clientId, setClientId] = useState<string>("");
  const [experimentId, setExperimentId] = useState<number | null>(null);
  const [name, setName] = useState("Cobertura · ensayo 1");
  const [environment, setEnvironment] = useState(1);
  const [distance, setDistance] = useState("1");
  const [calibration, setCalibration] = useState(true);
  const [busy, setBusy] = useState(false);
  const [message, setMessage] = useState<string | null>(null);
  const viaGateway = snapshot.gatewayVersion !== null;

  const refresh = async () => setLab(await getJson("api/lab", parseLab));
  useEffect(() => {
    if (!snapshot.demo) void refresh().catch(() => undefined);
  }, [snapshot.demo]);
  useEffect(() => {
    if (!clientId && clients[0]) setClientId(clients[0].id);
  }, [clientId, clients]);

  const run = async (action: () => Promise<unknown>, ok: string) => {
    setBusy(true);
    setMessage(null);
    try {
      await action();
      await refresh();
      setMessage(ok);
    } catch (error) {
      setMessage(error instanceof Error ? error.message : "Error desconocido");
    } finally {
      setBusy(false);
    }
  };

  const experiment = lab?.experiments.find(e => e.id === experimentId) ?? null;
  const rows = lab?.measurements.filter(m => m.experimentId === experimentId) ?? [];
  if (snapshot.demo) {
    return (
      <Panel title="Cobertura Wi-Fi" subtitle="No disponible en DEMO">
        <p className="muted">Este experimento usa la API de laboratorio del ESP32 y mediciones con cinta métrica; no se simula.</p>
      </Panel>
    );
  }
  return (
    <Panel title="Cobertura Wi-Fi" subtitle={viaGateway ? "Solo lectura a través del gateway: abre http://192.168.4.1/ para registrar" : "API de laboratorio del ESP32"}>
      <div className="form-grid">
        <label>
          Cliente
          <select value={clientId} onChange={e => setClientId(e.target.value)}>
            {clients.length === 0 && <option value="">Sin clientes conectados</option>}
            {clients.map(c => (
              <option key={c.id} value={c.id}>
                {c.id} · {c.filtered?.toFixed(0) ?? c.rssi ?? "—"} dBm
              </option>
            ))}
          </select>
        </label>
        <label>
          Nombre
          <input value={name} maxLength={48} onChange={e => setName(e.target.value)} />
        </label>
        <label>
          Entorno
          <select value={environment} onChange={e => setEnvironment(Number(e.target.value))}>
            <option value={0}>Abierto</option>
            <option value={1}>Interior</option>
            <option value={2}>Con obstáculos</option>
          </select>
        </label>
        <Button disabled={busy || !clientId || viaGateway} onClick={() => void run(async () => {
          const reply = (await postJson("api/experiments", { name, clientId: Number(clientId), environment })) as { id?: number };
          if (typeof reply?.id === "number") setExperimentId(reply.id);
        }, "Experimento creado")}>
          Crear experimento
        </Button>
      </div>
      <div className="form-grid">
        <label>
          Experimento
          <select value={experimentId ?? ""} onChange={e => setExperimentId(e.target.value ? Number(e.target.value) : null)}>
            <option value="">—</option>
            {lab?.experiments.map(e => (
              <option key={e.id} value={e.id}>
                #{e.id} {e.name} {e.calibrated ? "· calibrado" : ""}
              </option>
            ))}
          </select>
        </label>
        <label>
          Distancia real (m, medida con cinta)
          <input type="number" min="0.1" max="100" step="0.1" value={distance} onChange={e => setDistance(e.target.value)} />
        </label>
        <label className="check">
          <input type="checkbox" checked={calibration} onChange={e => setCalibration(e.target.checked)} /> Punto de calibración
        </label>
        <Button disabled={busy || experimentId === null || viaGateway || !(Number(distance) > 0)} onClick={() => void run(() => postJson("api/measurements", { experimentId, actualM: Number(distance), calibration, notes: "web" }), "RSSI registrado")}>
          Registrar RSSI
        </Button>
        <Button variant="secondary" disabled={busy || experimentId === null || viaGateway} onClick={() => void run(() => postJson("api/calibration", { experimentId }), "Modelo calibrado")}>
          Calibrar
        </Button>
      </div>
      {message && <p className="form-message" role="status">{message}</p>}
      {experiment && (
        <dl className="kv">
          <dt>Modelo</dt>
          <dd>{experiment.calibrated ? `n = ${experiment.n?.toFixed(2)}, RSSI a 1 m = ${experiment.referenceRssi?.toFixed(1)} dBm` : "Sin calibrar (≥ 2 distancias distintas)"}</dd>
          <dt>Distancia estimada ahora</dt>
          <dd>{experiment.currentEstimateM !== null ? `${experiment.currentEstimateM.toFixed(2)} m (estimación)` : "—"}</dd>
        </dl>
      )}
      {rows.length > 0 && (
        <table className="stats-table">
          <thead>
            <tr>
              <th>Real (m)</th>
              <th>RSSI</th>
              <th>Estimada (m)</th>
              <th>Error (m)</th>
              <th>Uso</th>
            </tr>
          </thead>
          <tbody>
            {rows.map((m, i) => (
              <tr key={i}>
                <td>{m.actualM?.toFixed(2) ?? "—"}</td>
                <td>{m.rssi ?? "—"}</td>
                <td>{m.estimatedM?.toFixed(2) ?? "—"}</td>
                <td>{m.errorM?.toFixed(2) ?? "—"}</td>
                <td>{m.calibration ? "calibración" : "validación"}</td>
              </tr>
            ))}
          </tbody>
        </table>
      )}
      <a className="link" href="api/export.csv">
        Descargar CSV de laboratorio del ESP32
      </a>
    </Panel>
  );
}
