import { Badge, type BadgeTone } from "@/components/arc/badge/badge";
import type { HealthLevel } from "@/lib/health";
import type { Snapshot } from "@/lib/store";

export const LEVEL_TONE: Record<HealthLevel, BadgeTone> = { ok: "success", warn: "warning", critical: "danger", unknown: "neutral" };
export const LEVEL_LABEL: Record<HealthLevel, string> = { ok: "Correcto", warn: "Atención", critical: "Crítico", unknown: "Sin datos" };

export function LevelBadge({ level, label }: { level: HealthLevel; label?: string }) {
  return (
    <Badge tone={LEVEL_TONE[level]} dot size="sm" pill>
      {label ?? LEVEL_LABEL[level]}
    </Badge>
  );
}

export function socketLabel(state: Snapshot["socketState"]): { tone: BadgeTone; text: string } {
  switch (state) {
    case "open":
      return { tone: "success", text: "Tiempo real conectado" };
    case "connecting":
      return { tone: "info", text: "Conectando…" };
    case "closed":
      return { tone: "warning", text: "Reconectando" };
    case "demo":
      return { tone: "warning", text: "DEMO" };
    default:
      return { tone: "neutral", text: "Inactivo" };
  }
}
