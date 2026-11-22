import { Select } from "@/components/arc/select/select";
import { Switch } from "@/components/arc/switch/switch";
import type { Settings, Snapshot } from "@/lib/store";
import { KeyValues, Panel } from "@/ui/Panel";

const INTERVALS = [
  { value: "2000", label: "Cada 2 s" },
  { value: "3000", label: "Cada 3 s" },
  { value: "5000", label: "Cada 5 s" },
  { value: "10000", label: "Cada 10 s" },
];

export function SettingsView({ settings, onChange, snapshot }: { settings: Settings; onChange: (next: Settings) => void; snapshot: Snapshot }) {
  return (
    <div className="view">
      <div className="grid grid-2">
        <Panel title="Preferencias" subtitle="Se guardan solo en este navegador">
          <div className="settings-list">
            <Select
              label="Intervalo de lectura HTTP"
              description="Ante errores se espera más (retroceso exponencial hasta 30 s)."
              value={String(settings.refreshMs)}
              onValueChange={value => onChange({ ...settings, refreshMs: Number(value) })}
              options={INTERVALS}
            />
            <Switch
              label="Modo DEMO (reproducir fixtures del firmware)"
              checked={settings.demo}
              onCheckedChange={checked => onChange({ ...settings, demo: checked })}
            />
            <p className="muted">
              En modo DEMO no se contacta con el ESP32. Toda la interfaz y la evidencia exportada quedan marcadas como DEMO.
            </p>
          </div>
        </Panel>
        <Panel title="Conexión" subtitle="Resuelta desde /gateway/config, o valores por defecto del AP">
          <KeyValues
            items={[
              ["WebSocket", <code key="ws">{snapshot.wsUrl}</code>],
              ["Subprotocolo", <code key="p">sentinel.v1</code>],
              ["API HTTP", <code key="h">{snapshot.gatewayVersion ? "proxy del gateway (mismo origen)" : "mismo origen (/api)"}</code>],
              ["Rol de este cliente", "web · solo lectura"],
              ["Autenticación", "ninguna: los navegadores no pueden enviar Authorization en un WebSocket"],
            ]}
          />
          <p className="muted" style={{ marginTop: 12 }}>
            Uso exclusivo en la red de laboratorio SENTINEL-LAB. No exponga el ESP32 ni el gateway a internet.
          </p>
        </Panel>
      </div>
    </div>
  );
}
