import { useCallback, useEffect, useState, useSyncExternalStore } from "react";
import { Presentation, Radio } from "lucide-react";
import { Badge } from "@/components/arc/badge/badge";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/arc/tabs/tabs";
import { ThemeSwitch, type Theme } from "@/components/arc/theme-switch/theme-switch";
import { ToastStack, ToastStackProvider, useToastStack } from "@/components/arc/toast-stack/toast-stack";
import { Tooltip } from "@/components/arc/tooltip/tooltip";
import { overallLevel } from "@/lib/health";
import { SentinelStore, type Settings } from "@/lib/store";
import { LevelBadge, socketLabel } from "@/ui/levels";
import { Overview } from "@/views/Overview";
import { Lab } from "@/views/Lab";
import { Radar } from "@/views/Radar";
import { Motion } from "@/views/Motion";
import { Experiments } from "@/views/Experiments";
import { Exhibition } from "@/views/Exhibition";
import { Telemetry } from "@/views/Telemetry";
import { Events } from "@/views/Events";
import { Diagnostics } from "@/views/Diagnostics";
import { SettingsView } from "@/views/SettingsView";

export const APP_VERSION = "0.2.0";

export const VIEWS = [
  { value: "lab", label: "Laboratorio" },
  { value: "radar", label: "Radar" },
  { value: "motion", label: "Movimiento" },
  { value: "experiments", label: "Experimentos" },
  { value: "overview", label: "Estado" },
  { value: "telemetry", label: "Telemetría RF" },
  { value: "events", label: "Eventos" },
  { value: "diagnostics", label: "Diagnóstico" },
  { value: "settings", label: "Ajustes" },
] as const;

const SETTINGS_KEY = "sentinel.settings";
const THEME_KEY = "sentinel.theme";
const TAB_KEY = "sentinel.tab";
const DEFAULT_SETTINGS: Settings = { refreshMs: 3000, demo: false };

function readSettings(): Settings {
  try {
    const raw = JSON.parse(localStorage.getItem(SETTINGS_KEY) ?? "null") as Partial<Settings> | null;
    if (!raw) return DEFAULT_SETTINGS;
    const refreshMs = typeof raw.refreshMs === "number" && raw.refreshMs >= 2000 && raw.refreshMs <= 10000 ? raw.refreshMs : DEFAULT_SETTINGS.refreshMs;
    return { refreshMs, demo: raw.demo === true };
  } catch {
    return DEFAULT_SETTINGS;
  }
}

function store<T>(key: string, value: T): void {
  try {
    localStorage.setItem(key, typeof value === "string" ? value : JSON.stringify(value));
  } catch {
    /* private mode: settings stay per-session */
  }
}

function readTheme(): Theme {
  const current = document.documentElement.dataset.theme;
  return current === "dark" ? "dark" : "light";
}

function readTab(): string {
  try {
    const saved = localStorage.getItem(TAB_KEY);
    return VIEWS.some(v => v.value === saved) ? (saved as string) : "lab";
  } catch {
    return "lab";
  }
}

export function App() {
  return (
    <ToastStackProvider duration={5000} limit={8}>
      <Dashboard />
      <ToastStack label="Notificaciones" position="bottom-right" />
    </ToastStackProvider>
  );
}

function Dashboard() {
  const [settings, setSettings] = useState<Settings>(readSettings);
  const [theme, setTheme] = useState<Theme>(readTheme);
  const [tab, setTab] = useState<string>(readTab);
  const [exhibition, setExhibition] = useState(false);
  const navigate = useCallback((next: string) => {
    setTab(next);
    store(TAB_KEY, next);
    window.scrollTo?.({ top: 0 });
  }, []);
  const [sentinel] = useState(() => new SentinelStore(settings));
  const snapshot = useSyncExternalStore(sentinel.subscribe, sentinel.getSnapshot);
  const { toast } = useToastStack();

  useEffect(() => {
    sentinel.start();
    return () => sentinel.stop();
  }, [sentinel]);

  useEffect(() => sentinel.onNotice(notice => toast({ type: notice.type, title: notice.title, description: notice.description })), [sentinel, toast]);

  useEffect(() => {
    sentinel.updateSettings(settings);
    store(SETTINGS_KEY, settings);
  }, [sentinel, settings]);

  useEffect(() => {
    document.documentElement.dataset.theme = theme;
    store(THEME_KEY, theme);
  }, [theme]);

  const level = overallLevel(snapshot.health);
  const socket = socketLabel(snapshot.socketState);
  const firmware = snapshot.realtimeStatus?.firmwareVersion ?? snapshot.realtimeStatus?.realtimeVersion;

  return (
    <div className="shell">
      <header className="header">
        <div className="brand">
          <span className="brand-mark" aria-hidden="true">
            <Radio size={18} strokeWidth={1.75} />
          </span>
          <div>
            <h1>SENTINEL-LAB</h1>
            <small>Laboratorio físico · ESP32-S3</small>
          </div>
        </div>
        <div className="header-status">
          {snapshot.demo && (
            <Badge tone="warning" pill size="sm">
              DEMO
            </Badge>
          )}
          <Tooltip content={snapshot.demo ? "Reproducción de fixtures, sin dispositivo" : snapshot.wsUrl}>
            <span tabIndex={0}>
              <Badge tone={socket.tone} dot pill size="sm">
                {socket.text}
              </Badge>
            </span>
          </Tooltip>
          <LevelBadge level={level} label={`Salud: ${level === "ok" ? "correcta" : level === "warn" ? "atención" : level === "critical" ? "crítica" : "sin datos"}`} />
          {firmware && (
            <Badge tone="neutral" outline pill size="sm">
              fw {firmware}
            </Badge>
          )}
          <button type="button" className="exhibit-button" onClick={() => setExhibition(true)}>
            <Presentation size={16} aria-hidden="true" />
            <span>Exhibición</span>
          </button>
          <ThemeSwitch theme={theme} onThemeChange={next => setTheme(next)} label={theme === "light" ? "Cambiar a modo oscuro" : "Cambiar a modo claro"} iconOnly />
        </div>
      </header>

      {snapshot.demo && (
        <div className="demo-banner" role="note">
          <strong>DEMO</strong>
          <span>Los datos mostrados reproducen fixtures del firmware con variaciones sintéticas. No son mediciones del dispositivo.</span>
        </div>
      )}

      <Tabs
        className="tabs-wrap"
        value={tab}
        onValueChange={navigate}
      >
        <TabsList aria-label="Secciones">
          {VIEWS.map(v => (
            <TabsTrigger key={v.value} value={v.value}>
              {v.label}
            </TabsTrigger>
          ))}
        </TabsList>
        <TabsContent value="lab">
          <Lab snapshot={snapshot} onNavigate={navigate} />
        </TabsContent>
        <TabsContent value="radar">
          <Radar snapshot={snapshot} />
        </TabsContent>
        <TabsContent value="motion">
          <Motion snapshot={snapshot} />
        </TabsContent>
        <TabsContent value="experiments">
          <Experiments snapshot={snapshot} />
        </TabsContent>
        <TabsContent value="overview">
          <Overview snapshot={snapshot} />
        </TabsContent>
        <TabsContent value="telemetry">
          <Telemetry snapshot={snapshot} />
        </TabsContent>
        <TabsContent value="events">
          <Events snapshot={snapshot} />
        </TabsContent>
        <TabsContent value="diagnostics">
          <Diagnostics snapshot={snapshot} appVersion={APP_VERSION} />
        </TabsContent>
        <TabsContent value="settings">
          <SettingsView settings={settings} onChange={setSettings} snapshot={snapshot} onToken={token => sentinel.setToken(token)} />
        </TabsContent>
      </Tabs>

      {exhibition && <Exhibition snapshot={snapshot} onClose={() => setExhibition(false)} />}

      <footer className="footer">
        SENTINEL-LAB web {APP_VERSION} · Solo red de laboratorio · Componentes de interfaz:{" "}
        <a href="https://github.com/kuratlielia/arc-library" rel="noreferrer">
          Arc Library
        </a>{" "}
        (MIT) · Gráficos 3D:{" "}
        <a href="https://threejs.org" rel="noreferrer">
          three.js
        </a>{" "}
        (MIT) · Modelo de placa original y aproximado
      </footer>
    </div>
  );
}
