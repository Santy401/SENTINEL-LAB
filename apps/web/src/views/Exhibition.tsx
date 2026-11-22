import { useEffect, useMemo, useRef, useState } from "react";
import { ChevronLeft, ChevronRight, X } from "lucide-react";
import { clampStep, TOUR, type TourVisual } from "@/lib/exhibition";
import { analyzeMotion } from "@/lib/motion-analysis";
import { forceNewtons, shakeLabel, toG } from "@/lib/physics";
import { radarClients } from "@/lib/radar";
import type { Snapshot } from "@/lib/store";
import { BoardViewer } from "@/ui/BoardViewer";
import { useNow } from "@/ui/hooks";
import { RgbIndicator } from "@/ui/RgbPanel";
import { RGB_MODE_LEGEND } from "@/lib/board-parts";

export function Exhibition({ snapshot, onClose }: { snapshot: Snapshot; onClose(): void }) {
  const [index, setIndex] = useState(0);
  const step = TOUR[index]!;
  const dialog = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (e.key === "Escape") onClose();
      if (e.key === "ArrowRight" || e.key === "PageDown") setIndex(i => clampStep(i + 1));
      if (e.key === "ArrowLeft" || e.key === "PageUp") setIndex(i => clampStep(i - 1));
    };
    window.addEventListener("keydown", onKey);
    dialog.current?.focus();
    return () => window.removeEventListener("keydown", onKey);
  }, [onClose]);

  return (
    <div className="exhibition" role="dialog" aria-modal="true" aria-label="Modo exhibición" ref={dialog} tabIndex={-1}>
      <header className="exhibition-top">
        <span className="exhibition-brand">SENTINEL-LAB</span>
        {snapshot.demo && <span className="demo-tag">DEMO · datos sintéticos</span>}
        <ol className="exhibition-dots" aria-label="Pasos">
          {TOUR.map((s, i) => (
            <li key={s.id}>
              <button type="button" aria-current={i === index ? "step" : undefined} aria-label={`Paso ${i + 1}: ${s.title}`} onClick={() => setIndex(i)} />
            </li>
          ))}
        </ol>
        <button type="button" className="icon-button" onClick={onClose} aria-label="Salir del modo exhibición">
          <X size={18} />
        </button>
      </header>
      <main className="exhibition-body">
        <section className="exhibition-copy" key={step.id}>
          <p className="eyebrow">
            Paso {index + 1} de {TOUR.length}
          </p>
          <h2>{step.title}</h2>
          <p className="lede">{step.lead}</p>
          <ul>
            {step.points.map(p => (
              <li key={p}>{p}</li>
            ))}
          </ul>
          {step.reality && <p className="reality">{step.reality}</p>}
        </section>
        <section className="exhibition-visual" aria-label="Datos en vivo">
          <LiveVisual visual={step.visual} snapshot={snapshot} />
        </section>
      </main>
      <footer className="exhibition-nav">
        <button type="button" onClick={() => setIndex(i => clampStep(i - 1))} disabled={index === 0}>
          <ChevronLeft size={18} /> Anterior
        </button>
        <span>Usa ← → o los puntos para navegar · Esc para salir</span>
        <button type="button" onClick={() => (index === TOUR.length - 1 ? onClose() : setIndex(i => clampStep(i + 1)))}>
          {index === TOUR.length - 1 ? "Terminar" : "Siguiente"} <ChevronRight size={18} />
        </button>
      </footer>
    </div>
  );
}

function LiveVisual({ visual, snapshot }: { visual: TourVisual; snapshot: Snapshot }) {
  const now = useNow(500);
  const status = snapshot.realtimeStatus;
  const motion = useMemo(() => analyzeMotion(snapshot.motionSamples, { filtered: true, now, windowMs: 5000 }), [snapshot.motionSamples, now]);
  const connected = snapshot.socketState === "open" || snapshot.socketState === "demo";
  switch (visual) {
    case "board":
      return <BoardViewer led={status?.rgb ?? null} ledMode={status?.rgb?.mode ?? null} connected={connected} pulseKey={0} />;
    case "radar": {
      const clients = radarClients(snapshot, now).filter(c => c.connected);
      return (
        <div className="big-readouts">
          {clients.length === 0 && <p className="muted">Conecta un teléfono a la Wi-Fi SENTINEL-LAB.</p>}
          {clients.slice(0, 3).map(c => (
            <div key={c.id} className="big-readout">
              <span>Dispositivo {c.id}</span>
              <b>{c.filtered?.toFixed(0) ?? c.rssi ?? "—"} dBm</b>
              <small>señal {c.band} · {c.distance?.calibrated ? `${c.distance.valueM.toFixed(1)} m calibrados` : "distancia sin calibrar"}</small>
            </div>
          ))}
        </div>
      );
    }
    case "motion":
      return (
        <div className="big-readouts">
          <div className="big-readout">
            <span>Agitación del iPhone</span>
            <b>{motion.currentMs2 === null ? "—" : `${toG(motion.currentMs2).toFixed(2)} g`}</b>
            <small>{motion.currentMs2 === null ? "Abre la app del iPhone" : shakeLabel(motion.intensity)}</small>
          </div>
          <div className="big-readout">
            <span>Fuerza estimada</span>
            <b>{motion.currentMs2 === null ? "—" : `${forceNewtons(motion.currentMs2).toFixed(2)} N`}</b>
            <small>F = m·a · 164 g</small>
          </div>
          <div className="big-readout">
            <span>Inclinación</span>
            <b>{motion.tilt ? `${motion.tilt.pitchDeg.toFixed(0)}° / ${motion.tilt.rollDeg.toFixed(0)}°` : "—"}</b>
            <small>cabeceo / alabeo</small>
          </div>
        </div>
      );
    case "rgb":
      return (
        <div className="big-readouts">
          <div className="big-readout">
            <RgbIndicator rgb={status?.rgb ?? null} size={72} />
            <b>{status?.rgb ? RGB_MODE_LEGEND[status.rgb.mode ?? ""]?.label ?? "—" : "Sin telemetría"}</b>
            <small>{status?.rgb ? RGB_MODE_LEGEND[status.rgb.mode ?? ""]?.meaning ?? "" : "Requiere firmware 0.5.0"}</small>
          </div>
        </div>
      );
    case "stack":
      return (
        <ul className="stack-list">
          <li><b>ESP32-S3</b> C++ · PlatformIO · WebSocket RFC 6455</li>
          <li><b>Protocolo</b> sentinel.v1 · JSON Schema · pruebas de contrato</li>
          <li><b>Web</b> React · three.js · servida desde el ESP32</li>
          <li><b>iPhone</b> SwiftUI · CoreMotion · Keychain</li>
          <li><b>Calidad</b> CI con pruebas host, compilación y validación física</li>
        </ul>
      );
    case "experiments":
      return (
        <div className="big-readouts">
          <div className="big-readout">
            <span>Muestras de movimiento (60 s)</span>
            <b>{snapshot.motionSamples.length}</b>
            <small>exportables en CSV/JSON</small>
          </div>
          <div className="big-readout">
            <span>Tiempo activo del ESP32</span>
            <b>{status ? `${Math.floor(status.uptimeMs / 60000)} min` : "—"}</b>
            <small>{status ? `heap libre ${Math.round(status.freeHeapBytes / 1024)} KiB` : ""}</small>
          </div>
        </div>
      );
    case "future":
      return (
        <ul className="stack-list">
          <li>Varias placas → triangulación de señal</li>
          <li>Nuevos sensores con el mismo protocolo</li>
          <li>Sesiones comparables entre grupos y entornos</li>
        </ul>
      );
  }
}
