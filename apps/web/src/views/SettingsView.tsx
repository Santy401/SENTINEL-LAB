import { Select } from "@/components/arc/select/select";
import { Switch } from "@/components/arc/switch/switch";
import { useState } from "react";
import { Button } from "@/components/arc/button/button";
import type { Settings, Snapshot } from "@/lib/store";
import { validToken } from "@/lib/token";
import { KeyValues, Panel } from "@/ui/Panel";

const INTERVALS = [
  { value: "2000", label: "Cada 2 s" },
  { value: "3000", label: "Cada 3 s" },
  { value: "5000", label: "Cada 5 s" },
  { value: "10000", label: "Cada 10 s" },
];

export function SettingsView({ settings, onChange, snapshot, onToken }: { settings: Settings; onChange: (next: Settings) => void; snapshot: Snapshot; onToken: (token: string | null) => void }) {
  const [draft, setDraft] = useState("");
  const draftOk = validToken(draft.trim());
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
        <Panel title="Conexión" subtitle="Resuelta desde /gateway/config o desde el host que sirve esta página">
          <KeyValues
            items={[
              ["WebSocket", <code key="ws">{snapshot.wsUrl}</code>],
              ["Subprotocolo", <code key="p">sentinel.v1</code>],
              ["API HTTP", <code key="h">{snapshot.gatewayVersion ? "proxy del gateway (mismo origen)" : "mismo origen (/api)"}</code>],
              ["Rol de este cliente", snapshot.tokenConfigured ? "web · autoridad de tiempo (con token)" : "web · solo lectura"],
              ["Autenticación", snapshot.tokenConfigured ? "token como subprotocolo sentinel.auth" : "ninguna"],
            ]}
          />
          <form
            className="settings-list"
            style={{ marginTop: 16 }}
            onSubmit={event => {
              event.preventDefault();
              if (!draftOk) return;
              onToken(draft.trim());
              setDraft("");
            }}
          >
            <label className="muted" htmlFor="realtime-token">
              Token de tiempo real (SENTINEL_REALTIME_BEARER_TOKEN). Con él, este navegador pone en hora el ESP32 y recibe el stream sin gateway ni iPhone.
            </label>
            <input
              id="realtime-token"
              type="password"
              autoComplete="off"
              spellCheck={false}
              value={draft}
              placeholder={snapshot.tokenConfigured ? "Token guardado · pegue otro para reemplazarlo" : "Pegue el token"}
              onChange={event => setDraft(event.target.value)}
              aria-invalid={draft.length > 0 && !draftOk}
              className="token-input"
            />
            <div style={{ display: "flex", gap: 8, flexWrap: "wrap" }}>
              <Button type="submit" disabled={!draftOk}>Guardar token</Button>
              {snapshot.tokenConfigured ? (
                <Button type="button" variant="secondary" onClick={() => onToken(null)}>Eliminar token</Button>
              ) : null}
            </div>
            <p className="muted">Se guarda solo en este navegador. Viaja sin cifrar por la Wi-Fi del laboratorio; no lo use fuera de SENTINEL-LAB.</p>
          </form>
          <p className="muted" style={{ marginTop: 12 }}>
            Uso exclusivo en la red de laboratorio SENTINEL-LAB. No exponga el ESP32 ni el gateway a internet.
          </p>
        </Panel>
      </div>
    </div>
  );
}
