// Home: the physical ESP32-S3 as the protagonist, live instruments around it.
import { useEffect, useMemo, useRef, useState } from "react";
import { Activity, Cpu, Gauge as GaugeIcon, Radio, Clock3, Smartphone } from "lucide-react";
import { formatBytes, formatDuration } from "@/lib/format";
import { analyzeMotion } from "@/lib/motion-analysis";
import { radarClients } from "@/lib/radar";
import { shakeLabel, toG } from "@/lib/physics";
import type { Snapshot } from "@/lib/store";
import { BoardViewer } from "@/ui/BoardViewer";
import { useNow } from "@/ui/hooks";
import { RgbIndicator, RgbPanel } from "@/ui/RgbPanel";

export function Lab({ snapshot, onNavigate }: { snapshot: Snapshot; onNavigate(view: string): void }) {
  const now = useNow(1000);
  const status = snapshot.realtimeStatus;
  const connected = snapshot.socketState === "open" || snapshot.socketState === "demo";
  const motion = useMemo(() => analyzeMotion(snapshot.motionSamples, { filtered: true, now, windowMs: 10000 }), [snapshot.motionSamples, now]);
  const clients = radarClients(snapshot, now);
  const strongest = clients.filter(c => c.connected && c.filtered !== null).sort((a, b) => (b.filtered ?? -999) - (a.filtered ?? -999))[0];

  // One propagation pulse per new firmware event or connection change.
  const [pulseKey, setPulseKey] = useState(0);
  const lastEvent = snapshot.timeline.at(-1)?.id;
  const firstRender = useRef(true);
  useEffect(() => {
    if (firstRender.current) {
      firstRender.current = false;
      return;
    }
    setPulseKey(k => k + 1);
  }, [lastEvent, connected]);

  const phone = status?.phoneMotion;
  const phoneActive = (motion.ageMs !== null && motion.ageMs < 3000) || phone?.active === true;
  const intensity = motion.ageMs !== null && motion.ageMs < 3000 ? motion.intensity : phone?.intensity ?? 0;

  return (
    <div className="lab">
      <section className="lab-hero" aria-label="ESP32-S3 conectado">
        <div className="lab-hero-copy">
          <p className="eyebrow">
            <span className={`live-dot${connected ? " on" : ""}`} aria-hidden="true" />
            {snapshot.demo ? "DEMO · sin dispositivo" : connected ? "ESP32-S3 conectado en tiempo real" : status ? "ESP32-S3 accesible por HTTP" : "Buscando el ESP32-S3…"}
          </p>
          <h2>Laboratorio físico conectado</h2>
          <p className="lede">Una placa ESP32-S3 real mide la señal Wi-Fi y recibe el movimiento de un iPhone. Todo lo que ves aquí procede de ella; lo estimado lleva su etiqueta.</p>
        </div>
        <BoardViewer led={status?.rgb ?? null} ledMode={status?.rgb?.mode ?? null} connected={connected} pulseKey={pulseKey} />
      </section>

      <section className="instrument-strip" aria-label="Instrumentos">
        <Readout icon={<Cpu size={16} />} label="Firmware" value={status ? status.firmwareVersion ?? status.realtimeVersion : "—"} detail={status ? `API HTTP ${status.httpApiVersion ?? status.fw}` : "sin datos"} />
        <Readout icon={<Clock3 size={16} />} label="Tiempo activo" value={status ? formatDuration(status.uptimeMs) : "—"} detail={status ? (status.clockSynced ? `Reloj UTC · ${status.clockSource}` : "Reloj sin sincronizar") : "—"} tone={status && !status.clockSynced ? "warn" : undefined} />
        <Readout icon={<Activity size={16} />} label="Memoria libre" value={status ? formatBytes(status.freeHeapBytes) : "—"} detail={status ? `mínimo ${formatBytes(status.minFreeHeapBytes)}` : "—"} />
        <Readout icon={<Radio size={16} />} label="Señal más fuerte" value={strongest?.filtered != null ? `${strongest.filtered.toFixed(0)} dBm` : "—"} detail={strongest ? `cliente ${strongest.id} · ${strongest.band}` : `${clients.length} clientes RF`} onClick={() => onNavigate("radar")} />
        <Readout
          icon={<Smartphone size={16} />}
          label="Agitación del iPhone"
          value={phoneActive && motion.currentMs2 !== null ? `${toG(motion.currentMs2).toFixed(2)} g` : phoneActive && phone?.linearAccelMs2 != null ? `${toG(phone.linearAccelMs2).toFixed(2)} g` : "—"}
          detail={phoneActive ? shakeLabel(intensity) : "Abre la app SENTINEL-LAB con token"}
          meter={phoneActive ? intensity : undefined}
          onClick={() => onNavigate("motion")}
        />
        <Readout icon={<GaugeIcon size={16} />} label="Clientes WebSocket" value={status ? `${status.clientCount} / 4` : "—"} detail={status ? `${status.droppedFrames} tramas descartadas` : "—"} />
      </section>

      <div className="lab-grid">
        <section className="instrument" aria-label="LED RGB">
          <header>
            <h3>
              <RgbIndicator rgb={status?.rgb ?? null} /> LED RGB físico
            </h3>
          </header>
          <RgbPanel rgb={status?.rgb ?? null} firmware={status?.firmwareVersion ?? null} />
        </section>
        <section className="instrument" aria-label="Accesos">
          <header>
            <h3>Experiencias</h3>
          </header>
          <nav className="launchers">
            <button type="button" onClick={() => onNavigate("radar")}>
              <b>Radar de señal</b>
              <span>Intensidad Wi-Fi en vivo, alcance teórico y distancias calibradas.</span>
            </button>
            <button type="button" onClick={() => onNavigate("motion")}>
              <b>Física del movimiento</b>
              <span>Aceleración, gravedad, fuerza estimada y orientación del iPhone.</span>
            </button>
            <button type="button" onClick={() => onNavigate("experiments")}>
              <b>Experimentos</b>
              <span>Sesiones guiadas con datos exportables en CSV o JSON.</span>
            </button>
          </nav>
        </section>
      </div>
    </div>
  );
}

function Readout({ icon, label, value, detail, tone, meter, onClick }: { icon: React.ReactNode; label: string; value: string; detail: string; tone?: "warn"; meter?: number; onClick?: () => void }) {
  const body = (
    <>
      <span className="readout-label">
        {icon}
        {label}
      </span>
      <span className="readout-value">{value}</span>
      <span className="readout-detail">{detail}</span>
      {meter !== undefined && (
        <span className="readout-meter" aria-hidden="true">
          <i style={{ transform: `scaleX(${Math.max(0.02, meter)})` }} />
        </span>
      )}
    </>
  );
  return onClick ? (
    <button type="button" className={`readout${tone ? ` readout-${tone}` : ""}`} onClick={onClick}>
      {body}
    </button>
  ) : (
    <div className={`readout${tone ? ` readout-${tone}` : ""}`}>{body}</div>
  );
}
