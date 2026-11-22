// Board hero: lazy three.js scene with a static SVG fallback that renders
// immediately and whenever WebGL is unavailable. Never blocks the dashboard.
import { Component, lazy, Suspense, useMemo, useState, type ReactNode } from "react";
import { boardPart, ledHex, ledLevel, type BoardPartId, BOARD_PARTS } from "@/lib/board-parts";
import { detectWebGL, useDarkTheme, useReducedMotion } from "./hooks";

const BoardScene = lazy(() => import("@/three/BoardScene"));

class SceneBoundary extends Component<{ fallback: ReactNode; children: ReactNode }, { failed: boolean }> {
  state = { failed: false };
  static getDerivedStateFromError() {
    return { failed: true };
  }
  render() {
    return this.state.failed ? this.props.fallback : this.props.children;
  }
}

export interface BoardViewerProps {
  led: { r: number; g: number; b: number } | null;
  ledMode: string | null;
  connected: boolean;
  pulseKey: number;
  /** Force the static fallback (tests, low-power preference). */
  forceFallback?: boolean;
}

export function BoardViewer({ led, ledMode, connected, pulseKey, forceFallback }: BoardViewerProps) {
  const webgl = useMemo(() => !forceFallback && detectWebGL(), [forceFallback]);
  const reducedMotion = useReducedMotion();
  const dark = useDarkTheme();
  const [hovered, setHovered] = useState<BoardPartId | null>(null);
  const [selected, setSelected] = useState<BoardPartId | null>(null);
  const focus = boardPart(selected ?? hovered);
  const fallback = <BoardFallback led={led} selected={selected} onSelect={setSelected} />;

  return (
    <div className="board-viewer" data-connected={connected} style={{ ["--led" as string]: ledHex(led), ["--led-level" as string]: String(ledLevel(led)) }}>
      <div className="board-stage">
        {webgl ? (
          <SceneBoundary fallback={fallback}>
            <Suspense fallback={<div className="board-loading">{fallback}<span>Cargando modelo 3D…</span></div>}>
              <BoardScene led={led} connected={connected} pulseKey={pulseKey} selected={selected} onHover={setHovered} onSelect={setSelected} reducedMotion={reducedMotion} dark={dark} />
            </Suspense>
          </SceneBoundary>
        ) : (
          fallback
        )}
        <div className="board-hint" aria-hidden="true">
          {webgl ? "Arrastra para girar · rueda o pellizco para acercar · toca un componente" : "Vista estática (WebGL no disponible)"}
        </div>
      </div>
      <div className="board-parts" role="group" aria-label="Componentes de la placa">
        {BOARD_PARTS.map(part => (
          <button key={part.id} type="button" className="chip" aria-pressed={selected === part.id} onClick={() => setSelected(selected === part.id ? null : part.id)}>
            {part.id === "rgb" && <i className="chip-led" aria-hidden="true" />}
            {part.name.split(" (")[0]}
          </button>
        ))}
      </div>
      <div className="board-info" aria-live="polite">
        {focus ? (
          <>
            <strong>{focus.name}</strong>
            <p>{focus.summary}</p>
            <ul>
              {focus.details.map(d => (
                <li key={d}>{d}</li>
              ))}
              {focus.id === "rgb" && <li>Estado actual: {ledMode ?? "sin telemetría RGB"} · {ledHex(led)}</li>}
            </ul>
          </>
        ) : (
          <p className="muted">Modelo original y aproximado de una ESP32-S3-DevKitC-1 (N16R8), construido con dimensiones públicas. Selecciona un componente para ver su función.</p>
        )}
      </div>
    </div>
  );
}

/** Top view of the board in SVG; also the WebGL-less experience. */
export function BoardFallback({ led, selected, onSelect }: { led: { r: number; g: number; b: number } | null; selected: BoardPartId | null; onSelect(part: BoardPartId | null): void }) {
  const zone = (id: BoardPartId, base: string) => ({
    className: `${base} zone${selected === id ? " zone-on" : ""}`,
    onClick: () => onSelect(selected === id ? null : id),
    role: "button" as const,
    tabIndex: 0,
    "aria-label": boardPart(id)?.name,
    onKeyDown: (e: { key: string }) => {
      if (e.key === "Enter" || e.key === " ") onSelect(selected === id ? null : id);
    },
  });
  const pins = Array.from({ length: 22 }, (_, i) => 7 + i * 2.54);
  return (
    <svg className="board-svg" viewBox="0 0 70 32" role="img" aria-label="Vista superior de la placa ESP32-S3-DevKitC-1">
      <defs>
        <radialGradient id="ledGlow">
          <stop offset="0%" stopColor={ledHex(led)} stopOpacity={0.9} />
          <stop offset="100%" stopColor={ledHex(led)} stopOpacity={0} />
        </radialGradient>
      </defs>
      <rect x="3.5" y="3.3" width="63" height="25.4" rx="1.2" className="svg-pcb" />
      {pins.map(x => (
        <g key={x}>
          <circle cx={x} cy={4.8} r={0.55} className="svg-pin" />
          <circle cx={x} cy={27.2} r={0.55} className="svg-pin" />
        </g>
      ))}
      <rect x="3.6" y="7" width="25.5" height="18" rx="0.4" {...zone("antenna", "svg-module-base")} />
      <rect x="10.2" y="8.1" width="17.6" height="15.8" rx="0.6" {...zone("module", "svg-shield")} />
      <text x="19" y="16.6" className="svg-label">ESP32-S3</text>
      {[0, 1, 2, 3, 4, 5].map(i => (
        <rect key={i} x={4.4 + i * 0.95} y={9} width={0.45} height={14} className="svg-trace" />
      ))}
      <circle cx="37" cy="9.5" r="4.2" fill="url(#ledGlow)" opacity={led ? 0.25 + ledLevel(led) : 0} />
      <rect x="34.6" y="7.1" width="5" height="5" rx="0.6" style={{ fill: ledHex(led) }} {...zone("rgb", "svg-led")} />
      <rect x="44" y="10" width="6.5" height="3.5" {...zone("ldo", "svg-chip")} />
      <rect x="45" y="18.5" width="4" height="4" {...zone("bridge", "svg-chip")} />
      <rect x="52" y="5.6" width="4.2" height="3.2" {...zone("boot", "svg-button")} />
      <rect x="52" y="23.2" width="4.2" height="3.2" {...zone("reset", "svg-button")} />
      <rect x="59.6" y="5.5" width="7.4" height="9" rx="1.2" {...zone("usb", "svg-usb")} />
      <rect x="59.6" y="17.5" width="7.4" height="9" rx="1.2" {...zone("uart", "svg-usb")} />
      <rect x="6" y="1.2" width="56" height="2" rx="0.3" {...zone("headers", "svg-header")} />
    </svg>
  );
}
