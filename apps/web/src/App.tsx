import { useEffect, useState, useSyncExternalStore } from "react";
import { Radio } from "lucide-react";
import { Badge } from "@/components/arc/badge/badge";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/arc/tabs/tabs";
import { ThemeSwitch, type Theme } from "@/components/arc/theme-switch/theme-switch";
import { ToastStack, ToastStackProvider, useToastStack } from "@/components/arc/toast-stack/toast-stack";
import { Tooltip } from "@/components/arc/tooltip/tooltip";
import { overallLevel } from "@/lib/health";
import { SentinelStore, type Settings } from "@/lib/store";
import { LevelBadge, socketLabel } from "@/ui/levels";
import { Overview } from "@/views/Overview";
import { Telemetry } from "@/views/Telemetry";
import { Events } from "@/views/Events";
import { Diagnostics } from "@/views/Diagnostics";
import { SettingsView } from "@/views/SettingsView";

export const APP_VERSION = "0.1.0";

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
    return localStorage.getItem(TAB_KEY) ?? "overview";
  } catch {
    return "overview";
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
            <small>Panel de laboratorio · ESP32-S3</small>
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
        onValueChange={next => {
          setTab(next);
          store(TAB_KEY, next);
        }}
      >
        <TabsList aria-label="Secciones">
          <TabsTrigger value="overview">Panel</TabsTrigger>
          <TabsTrigger value="telemetry">Telemetría RF</TabsTrigger>
          <TabsTrigger value="events">Eventos</TabsTrigger>
          <TabsTrigger value="diagnostics">Diagnóstico</TabsTrigger>
          <TabsTrigger value="settings">Ajustes</TabsTrigger>
        </TabsList>
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

      <footer className="footer">
        SENTINEL-LAB web {APP_VERSION} · Solo red de laboratorio · Componentes de interfaz:{" "}
        <a href="https://github.com/kuratlielia/arc-library" rel="noreferrer">
          Arc Library
        </a>{" "}
        (MIT)
      </footer>
    </div>
  );
}
