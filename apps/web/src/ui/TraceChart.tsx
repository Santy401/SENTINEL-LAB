// Lightweight instrument trace: several series on a shared time axis, drawn as
// SVG paths. Cheap enough to re-render at 5 Hz on a phone.

export interface Trace {
  id: string;
  label: string;
  color: string;
  values: readonly (number | null)[];
  dashed?: boolean;
}

export interface TraceChartProps {
  traces: Trace[];
  /** Fixed y-range; computed from data when omitted. */
  min?: number;
  max?: number;
  unit: string;
  height?: number;
  label: string;
  /** Horizontal reference lines, e.g. a threshold. */
  guides?: { value: number; label: string }[];
}

export function chartRange(traces: readonly Trace[], min?: number, max?: number): [number, number] {
  let lo = Infinity;
  let hi = -Infinity;
  for (const t of traces) for (const v of t.values) if (v !== null && Number.isFinite(v)) {
    lo = Math.min(lo, v);
    hi = Math.max(hi, v);
  }
  if (!Number.isFinite(lo)) return [min ?? 0, max ?? 1];
  const pad = (hi - lo) * 0.1 || 1;
  return [min ?? lo - pad, max ?? hi + pad];
}

export function tracePath(values: readonly (number | null)[], width: number, height: number, lo: number, hi: number): string {
  const n = values.length;
  if (n === 0 || hi === lo) return "";
  const dx = n > 1 ? width / (n - 1) : 0;
  let d = "";
  let pen = false;
  values.forEach((v, i) => {
    if (v === null || !Number.isFinite(v)) {
      pen = false;
      return;
    }
    const x = i * dx;
    const y = height - ((Math.min(hi, Math.max(lo, v)) - lo) / (hi - lo)) * height;
    d += `${pen ? "L" : "M"}${x.toFixed(1)},${y.toFixed(1)}`;
    pen = true;
  });
  return d;
}

export function TraceChart({ traces, min, max, unit, height = 140, label, guides = [] }: TraceChartProps) {
  const width = 600;
  const [lo, hi] = chartRange(traces, min, max);
  const hasData = traces.some(t => t.values.some(v => v !== null));
  const y = (v: number) => height - ((v - lo) / (hi - lo)) * height;
  return (
    <figure className="trace" aria-label={label}>
      <div className="trace-legend">
        {traces.map(t => (
          <span key={t.id}>
            <i style={{ background: t.color }} aria-hidden="true" />
            {t.label}
          </span>
        ))}
        <span className="trace-unit">{unit}</span>
      </div>
      <svg viewBox={`0 0 ${width} ${height}`} preserveAspectRatio="none" role="img" aria-label={`${label}, rango ${lo.toFixed(1)} a ${hi.toFixed(1)} ${unit}`}>
        {[0.25, 0.5, 0.75].map(f => (
          <line key={f} x1={0} x2={width} y1={height * f} y2={height * f} className="trace-grid" />
        ))}
        {lo < 0 && hi > 0 && <line x1={0} x2={width} y1={y(0)} y2={y(0)} className="trace-zero" />}
        {guides.map(g => (
          <g key={g.label}>
            <line x1={0} x2={width} y1={y(g.value)} y2={y(g.value)} className="trace-guide" />
            <text x={width - 4} y={y(g.value) - 4} textAnchor="end" className="trace-guide-label">
              {g.label}
            </text>
          </g>
        ))}
        {traces.map(t => (
          <path key={t.id} d={tracePath(t.values, width, height, lo, hi)} stroke={t.color} strokeDasharray={t.dashed ? "6 5" : undefined} fill="none" vectorEffect="non-scaling-stroke" strokeWidth={1.8} strokeLinejoin="round" />
        ))}
      </svg>
      {!hasData && <figcaption className="trace-empty">Sin muestras todavía</figcaption>}
      <div className="trace-scale" aria-hidden="true">
        <span>{hi.toFixed(1)}</span>
        <span>{lo.toFixed(1)}</span>
      </div>
    </figure>
  );
}
