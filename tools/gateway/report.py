"""Self-contained printable HTML session report with inline SVG charts."""

from __future__ import annotations

import html
import json
import shutil
import subprocess
from pathlib import Path
from typing import Any, Iterable

from .history import History

SEVERITY_COLOR = {"info": "#2b7bb9", "warning": "#c98a00", "critical": "#c0392b", "recovery": "#2e8b57"}


def _esc(value: Any) -> str:
    return html.escape("" if value is None else str(value))


def svg_line(points: list[tuple[float, float]], width: int = 720, height: int = 180,
             color: str = "#0e7c86", y_label: str = "", y_min: float | None = None,
             y_max: float | None = None) -> str:
    if len(points) < 2:
        return '<p class="empty">Sin datos suficientes para la gráfica.</p>'
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    x0, x1 = min(xs), max(xs)
    lo = min(ys) if y_min is None else y_min
    hi = max(ys) if y_max is None else y_max
    if hi == lo:
        hi = lo + 1
    pad = 36
    def sx(x: float) -> float:
        return pad + (x - x0) / ((x1 - x0) or 1) * (width - pad - 8)
    def sy(y: float) -> float:
        return height - 22 - (y - lo) / (hi - lo) * (height - 34)
    path = " ".join(f"{'M' if i == 0 else 'L'}{sx(x):.1f},{sy(y):.1f}" for i, (x, y) in enumerate(points))
    minutes = (x1 - x0) / 60000
    return (f'<svg viewBox="0 0 {width} {height}" role="img" aria-label="{_esc(y_label)}">'
            f'<line x1="{pad}" y1="{height-22}" x2="{width-8}" y2="{height-22}" stroke="#999"/>'
            f'<line x1="{pad}" y1="8" x2="{pad}" y2="{height-22}" stroke="#999"/>'
            f'<text x="2" y="14" font-size="10">{hi:.0f}</text>'
            f'<text x="2" y="{height-24}" font-size="10">{lo:.0f}</text>'
            f'<text x="{pad}" y="{height-6}" font-size="10">0 min</text>'
            f'<text x="{width-60}" y="{height-6}" font-size="10">{minutes:.1f} min</text>'
            f'<text x="{pad+6}" y="18" font-size="11" fill="#555">{_esc(y_label)}</text>'
            f'<path d="{path}" fill="none" stroke="{color}" stroke-width="1.6"/></svg>')


def svg_timeline(alerts: list[dict[str, Any]], start: int, end: int, width: int = 720) -> str:
    if not alerts:
        return '<p class="empty">No se generaron alertas en esta sesión.</p>'
    span = (end - start) or 1
    marks = []
    for a in alerts:
        x = 10 + (a["at_ms"] - start) / span * (width - 20)
        color = SEVERITY_COLOR.get(a["severity"], "#555")
        marks.append(f'<circle cx="{x:.1f}" cy="20" r="6" fill="{color}">'
                     f'<title>{_esc(a["at"])} {_esc(a["title"])}</title></circle>')
    return (f'<svg viewBox="0 0 {width} 40" role="img" aria-label="Línea de tiempo de alertas">'
            f'<line x1="10" y1="20" x2="{width-10}" y2="20" stroke="#bbb"/>{"".join(marks)}</svg>')


def _table(headers: Iterable[str], rows: Iterable[Iterable[Any]]) -> str:
    head = "".join(f"<th>{_esc(h)}</th>" for h in headers)
    body = "".join("<tr>" + "".join(f"<td>{_esc(c)}</td>" for c in r) + "</tr>" for r in rows)
    return f"<table><thead><tr>{head}</tr></thead><tbody>{body}</tbody></table>"


def build_report(history: History, session_spec: str | int = "latest") -> str:
    session_id = history.resolve_session(session_spec)
    if session_id is None:
        raise ValueError(f"no existe la sesión {session_spec}")
    session = next(s for s in history.sessions() if s["id"] == session_id)
    status = history.rows("status_samples", session_id)
    rf = history.rows("rf_samples", session_id)
    motion = history.rows("motion_summaries", session_id)
    events = history.rows("events", session_id)
    alerts = history.rows("alerts", session_id)
    labs = history.rows("lab_snapshots", session_id)
    times = [r["at_ms"] for r in status + rf + motion + alerts] or [session["started_ms"]]
    start, end = min(times), max(times)

    heap = svg_line([(r["at_ms"], (r["free_heap"] or 0) / 1024) for r in status if r["free_heap"]],
                    color="#0e7c86", y_label="Heap libre (KiB)")
    by_client: dict[int, list[tuple[float, float]]] = {}
    for r in rf:
        value = r["filtered"] if r["filtered"] is not None else r["rssi"]
        if value is not None:
            by_client.setdefault(r["client_id"], []).append((r["at_ms"], value))
    rssi_charts = "".join(
        f"<h3>Cliente {cid}</h3>" + svg_line(points, color="#6a4fb3", y_label="RSSI filtrado (dBm)",
                                             y_min=-100, y_max=-20)
        for cid, points in sorted(by_client.items())) or '<p class="empty">Sin telemetría RF.</p>'
    shake = svg_line([(r["at_ms"], r["peak"]) for r in motion], color="#c0392b",
                     y_label="Pico de aceleración lineal por segundo (m/s²)", y_min=0)

    peak = max((r["peak"] for r in motion), default=None)
    heap_min = min((r["free_heap"] for r in status if r["free_heap"]), default=None)
    boots = sorted({r["boot_id"] for r in status if r["boot_id"]})
    resets = sorted({r["reset_reason"] for r in status if r["reset_reason"]})
    firmware = next((r["firmware"] for r in reversed(status) if r["firmware"]), "desconocido")

    calibration = '<p class="empty">No se registró ninguna calibración en esta sesión.</p>'
    if labs:
        data = json.loads(labs[-1]["data"])
        exp_rows = [(e.get("id"), e.get("name"), e.get("clientId"), "sí" if e.get("calibrated") else "no",
                     e.get("n"), e.get("referenceRssi"), e.get("currentEstimateM"))
                    for e in data.get("experiments", [])]
        m_rows = [(m.get("experimentId"), m.get("actualM"), m.get("rssiDbm"), m.get("filteredDbm"),
                   m.get("estimatedM"), m.get("errorM")) for m in data.get("measurements", [])]
        calibration = (_table(["Exp.", "Nombre", "Cliente", "Calibrado", "n", "A (dBm @1 m)",
                               "Estimación actual (m)"], exp_rows)
                       + "<h3>Mediciones</h3>"
                       + _table(["Exp.", "Distancia real (m)", "RSSI (dBm)", "RSSI filtrado",
                                 "Estimada (m)", "Error (m)"], m_rows))

    alert_rows = [(a["at"], a["severity"], a["kind"], a["title"], a["message"],
                   a["delivered_at"] or "pendiente") for a in alerts]
    event_rows = [(e["at"], e["name"], e["client_id"], e["rssi"]) for e in events[-50:]]
    duration = (end - start) / 60000

    return f"""<!doctype html>
<html lang="es"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Informe de sesión SENTINEL-LAB #{session_id}</title>
<style>
 body{{font-family:system-ui,-apple-system,Segoe UI,sans-serif;margin:24px;color:#1b1f24;background:#fff;max-width:900px}}
 h1{{font-size:22px;margin:0 0 4px}} h2{{font-size:16px;margin:28px 0 8px;border-bottom:1px solid #ddd;padding-bottom:4px}}
 h3{{font-size:13px;margin:14px 0 4px;color:#444}}
 .meta{{color:#555;font-size:13px}} .kpis{{display:grid;grid-template-columns:repeat(4,1fr);gap:10px;margin-top:14px}}
 .kpi{{border:1px solid #ddd;border-radius:8px;padding:10px}} .kpi b{{display:block;font-size:18px}}
 .kpi span{{font-size:11px;color:#555}} svg{{width:100%;height:auto;background:#fafafa;border-radius:6px}}
 table{{border-collapse:collapse;width:100%;font-size:12px;margin:6px 0}} th,td{{border:1px solid #ddd;padding:4px 6px;text-align:left}}
 th{{background:#f3f3f3}} .empty{{color:#777;font-size:12px}} .note{{font-size:11px;color:#666}}
 @media print{{body{{margin:12mm}} h2{{break-after:avoid}} svg,table{{break-inside:avoid}}}}
</style></head><body>
<h1>Informe de sesión SENTINEL-LAB #{session_id}</h1>
<div class="meta">Inicio {_esc(session["started_at"])} · Fin {_esc(session["ended_at"] or "en curso")} ·
 Duración con datos {duration:.1f} min · Firmware {_esc(firmware)} · Gateway {_esc(session["gateway_version"])}</div>
<div class="kpis">
 <div class="kpi"><b>{len(status)}</b><span>muestras de estado</span></div>
 <div class="kpi"><b>{'—' if heap_min is None else f'{heap_min/1024:.1f} KiB'}</b><span>heap libre mínimo</span></div>
 <div class="kpi"><b>{'—' if peak is None else f'{peak:.1f} m/s²'}</b><span>pico de agitación</span></div>
 <div class="kpi"><b>{len(alerts)}</b><span>alertas</span></div>
</div>
<p class="meta">Arranques observados: {_esc(", ".join(boots) or "—")} · Motivos de reinicio: {_esc(", ".join(resets) or "—")}</p>
<h2>Memoria del ESP32</h2>{heap}
<h2>Intensidad de señal (RSSI)</h2>
<p class="note">El RSSI es una medida de intensidad, no una posición; la distancia solo se estima con un modelo calibrado.</p>
{rssi_charts}
<h2>Agitación del iPhone</h2>
<p class="note">Pico por segundo de la aceleración lineal reenviada por el ESP32 (CoreMotion, m/s²). No es velocidad ni desplazamiento.</p>
{shake}
<h2>Alertas</h2>{svg_timeline(alerts, start, end)}
{_table(["Hora (UTC)", "Severidad", "Tipo", "Título", "Mensaje", "Entrega"], alert_rows) if alerts else ""}
<h2>Calibración de distancia</h2>{calibration}
<h2>Eventos recientes del ESP32</h2>
{_table(["Hora (UTC)", "Evento", "Cliente", "RSSI"], event_rows) if events else '<p class="empty">Sin eventos.</p>'}
<p class="note">Generado por el gateway SENTINEL-LAB a partir de mediciones reales registradas en SQLite. Sin datos simulados.</p>
</body></html>
"""


def render_pdf(html_path: Path, pdf_path: Path, timeout: float = 90) -> bool:
    browser = next((shutil.which(b) for b in ("chromium", "chromium-browser", "google-chrome",
                                              "google-chrome-stable") if shutil.which(b)), None)
    if not browser:
        return False
    subprocess.run([browser, "--headless=new", "--disable-gpu", "--no-first-run",
                    "--no-pdf-header-footer", f"--print-to-pdf={pdf_path}",
                    html_path.resolve().as_uri()],
                   capture_output=True, timeout=timeout, check=False)
    return pdf_path.is_file() and pdf_path.stat().st_size > 0
