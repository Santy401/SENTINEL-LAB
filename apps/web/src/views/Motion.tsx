import { useMemo, useState } from "react";
import { Gauge } from "@/components/arc/gauge/gauge";
import { Switch } from "@/components/arc/switch/switch";
import { analyzeMotion, PEAK_THRESHOLD_MS2 } from "@/lib/motion-analysis";
import { IPHONE_12_MASS_KG, forceNewtons, shakeIntensity, shakeLabel, toG, STANDARD_GRAVITY } from "@/lib/physics";
import type { Snapshot } from "@/lib/store";
import { Panel } from "@/ui/Panel";
import { TraceChart } from "@/ui/TraceChart";
import { useNow, useReducedMotion } from "@/ui/hooks";

const AXIS = { x: "#f87171", y: "#34d399", z: "#60a5fa" } as const;

export function Motion({ snapshot }: { snapshot: Snapshot }) {
  const now = useNow(500);
  const reduced = useReducedMotion();
  const [filtered, setFiltered] = useState(true);
  const a = useMemo(() => analyzeMotion(snapshot.motionSamples, { filtered, now }), [snapshot.motionSamples, filtered, now]);
  const phone = snapshot.realtimeStatus?.phoneMotion ?? null;
  const live = a.ageMs !== null && a.ageMs < 3000;
  // Without relayed samples (clock unsynced), fall back to the firmware summary.
  const currentMs2 = live ? a.currentMs2 : phone?.active ? phone.linearAccelMs2 : null;
  const intensity = currentMs2 === null ? 0 : shakeIntensity(currentMs2);
  const peak = Math.max(a.peakMs2 ?? 0, phone?.peakMs2 ?? 0) || null;

  return (
    <div className="view motion-view">
      <div className="view-head">
        <div>
          <h2>Física del movimiento</h2>
          <p>El acelerómetro del iPhone, retransmitido por el ESP32. Agita el teléfono con la app SENTINEL-LAB abierta y conectada con token.</p>
        </div>
        <Switch label="Filtrado (EMA)" checked={filtered} onCheckedChange={setFiltered} />
      </div>

      {!live && !phone?.active && (
        <div className="callout" role="note">
          <b>Sin muestras de movimiento.</b> Abre la app del iPhone, guarda el token en Ajustes y mantén la pantalla encendida. Las muestras llegan a 5 Hz cuando el reloj del ESP32 está sincronizado.
        </div>
      )}

      <div className="motion-hero">
        <div className="shake-meter" style={{ ["--shake" as string]: String(intensity) }}>
          <Gauge
            value={Math.round(intensity * 100)}
            min={0}
            max={100}
            label="Intensidad de agitación"
            detail={currentMs2 === null ? "sin datos" : shakeLabel(intensity)}
            thresholds={[
              { from: 0, tone: "success", label: "suave" },
              { from: 50, tone: "warning", label: "fuerte" },
              { from: 85, tone: "danger", label: "muy fuerte" },
            ]}
          />
        </div>
        <dl className="physics-readouts">
          <div>
            <dt>Aceleración lineal |a|</dt>
            <dd>
              {currentMs2 === null ? "—" : currentMs2.toFixed(2)}
              <small>m/s²</small>
            </dd>
          </div>
          <div>
            <dt>En unidades de gravedad</dt>
            <dd>
              {currentMs2 === null ? "—" : toG(currentMs2).toFixed(3)}
              <small>g</small>
            </dd>
          </div>
          <div>
            <dt>Fuerza estimada F = m·a</dt>
            <dd>
              {currentMs2 === null ? "—" : forceNewtons(currentMs2).toFixed(3)}
              <small>N</small>
            </dd>
            <span className="estimate">estimación · m = {IPHONE_12_MASS_KG * 1000} g (iPhone 12)</span>
          </div>
          <div>
            <dt>Pico de la sesión</dt>
            <dd>
              {peak === null ? "—" : peak.toFixed(2)}
              <small>m/s²</small>
            </dd>
            <span className="estimate">{peak === null ? "" : `${toG(peak).toFixed(2)} g · ${forceNewtons(peak).toFixed(2)} N`}</span>
          </div>
        </dl>
        <PhoneTilt tilt={a.tilt} reduced={reduced} />
      </div>

      <div className="grid grid-2">
        <Panel title="Magnitud |a|" subtitle={`${filtered ? "Filtrada (EMA α=0,35) y cruda" : "Cruda"} · últimos 60 s`}>
          <TraceChart
            label="Magnitud de la aceleración lineal"
            unit="m/s²"
            min={0}
            guides={[{ value: PEAK_THRESHOLD_MS2, label: "umbral de pico" }]}
            traces={[
              ...(filtered ? [{ id: "raw", label: "cruda", color: "var(--trace-muted)", values: a.raw, dashed: true }] : []),
              { id: "mag", label: filtered ? "filtrada" : "cruda", color: "var(--accent-strong)", values: filtered ? a.filtered : a.raw },
            ]}
          />
        </Panel>
        <Panel title="Aceleración por eje" subtitle="Marco del dispositivo, sin gravedad">
          <TraceChart
            label="Aceleración por eje"
            unit="m/s²"
            traces={[
              { id: "x", label: "x", color: AXIS.x, values: a.axes.x },
              { id: "y", label: "y", color: AXIS.y, values: a.axes.y },
              { id: "z", label: "z", color: AXIS.z, values: a.axes.z },
            ]}
          />
        </Panel>
      </div>

      <div className="grid grid-2">
        <Panel title="Estadística de la sesión" subtitle={a.deviceId ? `Dispositivo ${a.deviceId}` : "Sin dispositivo"}>
          <dl className="kv">
            <dt>Muestras (60 s)</dt>
            <dd>{a.count}</dd>
            <dt>Frecuencia de muestreo</dt>
            <dd>{a.rateHz === null ? "—" : `${a.rateHz.toFixed(1)} Hz`}</dd>
            <dt>Mínimo / media / máximo</dt>
            <dd>{a.stats ? `${a.stats.min.toFixed(2)} / ${a.stats.mean.toFixed(2)} / ${a.stats.max.toFixed(2)} m/s²` : "—"}</dd>
            <dt>Picos ≥ {PEAK_THRESHOLD_MS2} m/s²</dt>
            <dd>{a.peaks.length}</dd>
            <dt>Antigüedad de la última muestra</dt>
            <dd>{a.ageMs === null ? "—" : `${(a.ageMs / 1000).toFixed(1)} s`}</dd>
            <dt>Muestras aceptadas por el ESP32</dt>
            <dd>{phone?.samples ?? "—"}</dd>
          </dl>
        </Panel>
        <Panel title="Qué se mide y qué no" subtitle="Rigor físico">
          <ul className="facts">
            <li>
              <b>Medido:</b> aceleración (con y sin gravedad) por CoreMotion, en m/s², a 5 Hz. Cada muestra es la más intensa de una ventana de 200 ms.
            </li>
            <li>
              <b>Derivado:</b> |a|, g (÷ {STANDARD_GRAVITY}), inclinación desde el vector gravedad.
            </li>
            <li>
              <b>Estimado:</b> fuerza F = m·a con la masa nominal del iPhone 12.
            </li>
            <li>
              <b>No calculado:</b> velocidad ni desplazamiento (integrar aceleración acumula error), ni rumbo (yaw), que la gravedad no determina.
            </li>
          </ul>
        </Panel>
      </div>
    </div>
  );
}

function PhoneTilt({ tilt, reduced }: { tilt: { pitchDeg: number; rollDeg: number } | null; reduced: boolean }) {
  return (
    <figure className="phone-tilt" aria-label={tilt ? `Inclinación: cabeceo ${tilt.pitchDeg.toFixed(0)}°, alabeo ${tilt.rollDeg.toFixed(0)}°` : "Inclinación sin datos"}>
      <div className="phone-stage">
        <div
          className={`phone-body${reduced ? " still" : ""}`}
          style={{ transform: tilt ? `rotateX(${(55 - tilt.pitchDeg).toFixed(1)}deg) rotateY(${(-tilt.rollDeg).toFixed(1)}deg)` : "rotateX(55deg)" }}
        >
          <div className="phone-screen">
            <span className="phone-notch" />
            <span className="phone-g" aria-hidden="true">g</span>
          </div>
        </div>
        <div className="phone-shadow" />
      </div>
      <figcaption>
        {tilt ? (
          <>
            Cabeceo <b>{tilt.pitchDeg.toFixed(0)}°</b> · Alabeo <b>{tilt.rollDeg.toFixed(0)}°</b>
            <small>Desde el vector gravedad. El rumbo no es observable.</small>
          </>
        ) : (
          "Inclinación: sin muestras"
        )}
      </figcaption>
    </figure>
  );
}
