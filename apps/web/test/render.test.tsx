import { renderToString } from "react-dom/server";
import { afterAll, beforeAll, describe, expect, it, vi } from "vitest";
import { ToastStackProvider } from "@/components/arc/toast-stack/toast-stack";
import { SentinelStore, type Snapshot } from "@/lib/store";
import { Diagnostics } from "@/views/Diagnostics";
import { Events } from "@/views/Events";
import { Overview } from "@/views/Overview";
import { SettingsView } from "@/views/SettingsView";
import { Telemetry } from "@/views/Telemetry";

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
