import { renderToString } from "react-dom/server";
import { afterAll, beforeAll, describe, expect, it, vi } from "vitest";
import { ToastStackProvider } from "@/components/arc/toast-stack/toast-stack";
import { SentinelStore, type Snapshot } from "@/lib/store";
import { Diagnostics } from "@/views/Diagnostics";
import { Events } from "@/views/Events";
import { Overview } from "@/views/Overview";
import { SettingsView } from "@/views/SettingsView";
import { Telemetry } from "@/views/Telemetry";
import { Lab } from "@/views/Lab";
import { Radar } from "@/views/Radar";
import { Motion } from "@/views/Motion";
import { Experiments } from "@/views/Experiments";
import { Exhibition } from "@/views/Exhibition";
import { BoardViewer } from "@/ui/BoardViewer";

// Server-side render smoke test: every view renders real DEMO data and the
// empty/loading states without throwing. It does not replace a browser check.
let demo: Snapshot;
let empty: Snapshot;

beforeAll(() => {
  vi.useFakeTimers();
  vi.setSystemTime(new Date("2026-11-21T16:10:00.000Z"));
  const store = new SentinelStore({ refreshMs: 3000, demo: true });
  store.start();
  vi.advanceTimersByTime(30000);
  demo = store.getSnapshot();
  store.stop();
  empty = new SentinelStore({ refreshMs: 3000, demo: false }).getSnapshot();
});

afterAll(() => vi.useRealTimers());

const render = (node: React.ReactNode) => renderToString(<ToastStackProvider>{node}</ToastStackProvider>);

describe("views", () => {
  it("collects DEMO data through the store", () => {
    expect(demo.demo).toBe(true);
    expect(demo.rf?.clients.length).toBeGreaterThan(0);
    expect(Object.keys(demo.rssiHistory).length).toBeGreaterThan(0);
    expect(demo.timeline.length).toBeGreaterThan(0);
    expect(demo.frameCounts["rf.telemetry"]).toBe(31);
  });

  it("renders the overview with health and device data", () => {
    const html = render(<Overview snapshot={demo} />);
    expect(html).toContain("Salud del sistema");
    expect(html).toContain("DEMO");
  });

  it("renders telemetry with the client table and calibrated distance label", () => {
    const html = render(<Telemetry snapshot={demo} />);
    expect(html).toContain("Clientes asociados");
    expect(html).toContain("estimación");
  });

  it("renders events, diagnostics and settings", () => {
    expect(render(<Events snapshot={demo} />)).toContain("Eventos recientes");
    const diagnostics = render(<Diagnostics snapshot={demo} appVersion="0.1.0" />);
    expect(diagnostics).toContain("Evidencia no sensible");
    expect(render(<SettingsView snapshot={demo} settings={demo.settings} onChange={() => {}} onToken={() => {}} />)).toContain("Modo DEMO");
  });

  it("renders loading and empty states before any data", () => {
    expect(render(<Overview snapshot={empty} />)).toContain("Cargando estado");
    expect(render(<Telemetry snapshot={empty} />)).toContain("Sin telemetría RF");
    expect(render(<Events snapshot={empty} />)).toContain("Sin eventos");
  });
});

describe("physical lab views (no WebGL: SVG fallback)", () => {
  it("collects DEMO motion samples and RGB state", () => {
    expect(demo.motionSamples.length).toBeGreaterThan(50);
    expect(demo.realtimeStatus?.rgb?.enabled).toBe(true);
    expect(demo.rgbLog.length).toBeGreaterThan(0);
  });

  it("renders the lab hero with the static board and instruments", () => {
    const html = render(<Lab snapshot={demo} onNavigate={() => {}} />);
    expect(html).toContain("Laboratorio físico conectado");
    expect(html).toContain("Vista superior de la placa ESP32-S3-DevKitC-1");
    expect(html).toContain("Vista estática (WebGL no disponible)");
    expect(html).toContain("Agitación del iPhone");
    expect(html).toContain("DEMO");
  });

  it("renders the board viewer without a snapshot", () => {
    const html = render(<BoardViewer led={null} ledMode={null} connected={false} pulseKey={0} forceFallback />);
    expect(html).toContain("LED RGB WS2812");
  });

  it("renders the radar with the honesty note and theoretical range", () => {
    const html = render(<Radar snapshot={demo} />);
    expect(html).toContain("La posición angular no se mide");
    expect(html).toContain("Alcance teórico");
    expect(render(<Radar snapshot={empty} />)).toContain("Sin clientes asociados");
  });

  it("renders the motion analyzer with force as an estimate", () => {
    const html = render(<Motion snapshot={demo} />);
    expect(html).toContain("Fuerza estimada F = m·a");
    expect(html).toContain("estimación");
    expect(html).toContain("No calculado");
    expect(render(<Motion snapshot={empty} />)).toContain("Sin muestras de movimiento");
  });

  it("renders experiments and the exhibition tour", () => {
    expect(render(<Experiments snapshot={demo} />)).toContain("Gravedad y orientación");
    const tour = render(<Exhibition snapshot={demo} onClose={() => {}} />);
    expect(tour).toContain("¿Qué problema resuelve?");
    expect(tour).toContain("DEMO");
  });
});
