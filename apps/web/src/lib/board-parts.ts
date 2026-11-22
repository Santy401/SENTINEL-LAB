// Interactive zones of the ESP32-S3-DevKitC-1 (N16R8) model. Dimensions are
// approximations from Espressif's public DevKitC-1 user guide and the
// ESP32-S3-WROOM-1 datasheet (module 18 × 25.5 × 3.1 mm); positions on the
// PCB are approximate, not taken from manufacturing drawings. Units: mm.

export type BoardPartId = "module" | "antenna" | "usb" | "uart" | "rgb" | "boot" | "reset" | "ldo" | "bridge" | "headers";

export interface BoardPart {
  id: BoardPartId;
  name: string;
  summary: string;
  details: string[];
}

export const BOARD_DIMENSIONS_MM = { length: 63, width: 25.4, thickness: 1.6 } as const;

export const BOARD_PARTS: readonly BoardPart[] = [
  {
    id: "module",
    name: "Módulo ESP32-S3-WROOM-1 (N16R8)",
    summary: "SoC Xtensa LX7 de doble núcleo a 240 MHz bajo blindaje metálico.",
    details: ["Flash 16 MB (quad) · PSRAM 8 MB (octal)", "Wi-Fi 802.11 b/g/n 2,4 GHz y Bluetooth LE 5", "Aquí corren el firmware SENTINEL y el servidor WebSocket"],
  },
  {
    id: "antenna",
    name: "Antena PCB",
    summary: "Antena impresa en el extremo del módulo, sin plano de masa debajo.",
    details: ["Emite el punto de acceso SENTINEL-LAB", "Su orientación y los obstáculos cambian el RSSI medido", "No cubrirla con la mano durante experimentos de cobertura"],
  },
  {
    id: "usb",
    name: "USB-C nativo (USB)",
    summary: "USB OTG del propio ESP32-S3 (GPIO19/20).",
    details: ["Consola serie CDC del firmware", "Alimentación 5 V"],
  },
  {
    id: "uart",
    name: "USB-C UART",
    summary: "Puerto de programación a través del puente USB-serie.",
    details: ["Usado para flashear (/dev/ttyACM0)", "Las líneas DTR/RTS pueden reiniciar la placa"],
  },
  {
    id: "rgb",
    name: "LED RGB WS2812 (GPIO48)",
    summary: "LED direccionable de la placa; el firmware ≥ 0.5.0 lo usa como indicador de estado.",
    details: ["El LED virtual de esta escena copia el color reportado por /api/realtime/status", "Brillo limitado y transiciones suaves, sin estroboscopio", "En algunas revisiones de placa el LED está en GPIO38"],
  },
  {
    id: "boot",
    name: "Botón BOOT (GPIO0)",
    summary: "Mantenido al reiniciar, entra en modo descarga.",
    details: ["No necesario con esptool y auto-reset"],
  },
  {
    id: "reset",
    name: "Botón RST (EN)",
    summary: "Reinicia el ESP32-S3.",
    details: ["Un reinicio por EN se reporta como POWERON en resetReason"],
  },
  {
    id: "ldo",
    name: "Regulador 3,3 V",
    summary: "LDO que baja los 5 V del USB a 3,3 V.",
    details: ["Limita la corriente disponible para periféricos externos"],
  },
  {
    id: "bridge",
    name: "Puente USB-serie",
    summary: "Convierte USB en UART para programar (CP2102N en la placa oficial; CH343 en este ejemplar, 1a86:55d3).",
    details: ["Representación genérica: el encapsulado real puede variar"],
  },
  {
    id: "headers",
    name: "Pines 2 × 22",
    summary: "GPIO, alimentación y tierra con paso de 2,54 mm.",
    details: ["Lógica a 3,3 V: no conectar 5 V a los GPIO", "Ningún pin está conectado en esta configuración del laboratorio"],
  },
];

export function boardPart(id: string | null | undefined): BoardPart | null {
  return BOARD_PARTS.find(p => p.id === id) ?? null;
}

/** Hex colour for an LED state; dark grey when the LED is off or unknown. */
export function ledHex(rgb: { r: number; g: number; b: number } | null | undefined): string {
  if (!rgb) return "#2a2f36";
  const hex = (v: number) => Math.max(0, Math.min(255, Math.round(v))).toString(16).padStart(2, "0");
  return `#${hex(rgb.r)}${hex(rgb.g)}${hex(rgb.b)}`;
}

/** Perceived brightness 0..1 (Rec. 709 luma), used to scale the glow. */
export function ledLevel(rgb: { r: number; g: number; b: number } | null | undefined): number {
  if (!rgb) return 0;
  return (0.2126 * rgb.r + 0.7152 * rgb.g + 0.0722 * rgb.b) / 255;
}

export const RGB_MODE_LEGEND: Record<string, { label: string; meaning: string }> = {
  boot: { label: "Arranque", meaning: "El firmware acaba de iniciar: fundido cian." },
  offline: { label: "En espera", meaning: "Sin clientes WebSocket: respiración azul tenue y lenta." },
  client: { label: "Cliente conectado", meaning: "Hay clientes pero el reloj aún no está sincronizado: verde azulado fijo." },
  synced: { label: "Sincronizado", meaning: "Reloj UTC válido y clientes activos: verde suave." },
  motion: { label: "Movimiento", meaning: "El iPhone se mueve: el color pasa de turquesa a ámbar y rojo según la intensidad." },
  event: { label: "Evento", meaning: "Evento RF (conexión, desconexión, señal débil): un pulso blanco suave." },
};
