import { ledHex, RGB_MODE_LEGEND } from "@/lib/board-parts";
import type { RgbStatus } from "@/lib/api";

export function RgbIndicator({ rgb, size = 14 }: { rgb: RgbStatus | null; size?: number }) {
  const color = rgb && rgb.enabled ? ledHex(rgb) : null;
  return (
    <span
      className="rgb-dot"
      style={{ width: size, height: size, background: color ?? "transparent", boxShadow: color ? `0 0 ${size}px ${color}` : undefined }}
      aria-label={color ? `LED ${color}` : "LED sin telemetría"}
      role="img"
    />
  );
}

export function RgbPanel({ rgb, firmware }: { rgb: RgbStatus | null; firmware: string | null }) {
  const legend = Object.entries(RGB_MODE_LEGEND);
  return (
    <div className="rgb-panel">
      <div className="rgb-now">
        <RgbIndicator rgb={rgb} size={28} />
        <div>
          <strong>{rgb ? (rgb.enabled ? RGB_MODE_LEGEND[rgb.mode ?? ""]?.label ?? "Modo desconocido" : "LED desactivado") : "Sin telemetría RGB"}</strong>
          <small>
            {rgb
              ? `${ledHex(rgb)} · R${rgb.r} G${rgb.g} B${rgb.b}${rgb.pin !== null ? ` · GPIO${rgb.pin}` : ""}`
              : `El firmware ${firmware ?? ""} no reporta el LED (requiere 0.5.0 o superior).`}
          </small>
        </div>
      </div>
      <ul className="rgb-legend">
        {legend.map(([mode, info]) => (
          <li key={mode} data-active={rgb?.mode === mode}>
            <b>{info.label}</b>
            <span>{info.meaning}</span>
          </li>
        ))}
      </ul>
      <p className="note">El color mostrado es el que el firmware dice haber escrito en el LED; se lee por HTTP cada pocos segundos.</p>
    </div>
  );
}
