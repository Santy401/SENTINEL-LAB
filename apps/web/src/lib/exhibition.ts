// Guided tour for live demonstrations. Plain language for visitors who do not
// program; every claim distinguishes measured data from estimates.

export type TourVisual = "board" | "radar" | "motion" | "rgb" | "experiments" | "stack" | "future";

export interface TourStep {
  id: string;
  title: string;
  lead: string;
  points: string[];
  visual: TourVisual;
  /** Optional "what is real" note shown in a distinct style. */
  reality?: string;
}

export const TOUR: readonly TourStep[] = [
  {
    id: "problem",
    title: "¿Qué problema resuelve?",
    lead: "Las ondas de radio y las fuerzas del movimiento son invisibles. SENTINEL-LAB las convierte en algo que se puede ver, medir y comparar.",
    points: [
      "Un laboratorio de bolsillo para experimentar con física real.",
      "Muestra en directo la señal Wi-Fi y el movimiento de un teléfono.",
      "Pensado para aulas, ferias de ciencia y prácticas de ingeniería.",
    ],
    visual: "board",
  },
  {
    id: "hardware",
    title: "El instrumento: un ESP32-S3 real",
    lead: "En la mesa hay una placa ESP32-S3 de 16 MB de flash y 8 MB de PSRAM. Crea su propia red Wi-Fi y mide a quien se conecta.",
    points: [
      "Doble núcleo a 240 MHz y radio Wi-Fi de 2,4 GHz.",
      "Un LED RGB en la placa muestra el estado del laboratorio.",
      "El modelo 3D de la pantalla es una representación aproximada de esa placa.",
    ],
    visual: "board",
    reality: "El modelo 3D es original y aproximado: no procede de planos de fabricación.",
  },
  {
    id: "radar",
    title: "Radar de señal",
    lead: "Cuanto más cerca del centro aparece un dispositivo, más fuerte llega su señal al ESP32.",
    points: [
      "La intensidad (RSSI, en dBm) la mide el ESP32 una vez por segundo.",
      "Los anillos de alcance siguen un modelo teórico de propagación.",
      "Con una cinta métrica se calibra el modelo y se estiman distancias.",
    ],
    visual: "radar",
    reality: "La intensidad es medida; la posición angular en el radar no lo es, y la distancia es siempre una estimación.",
  },
  {
    id: "motion",
    title: "Física del movimiento",
    lead: "La app del iPhone lee su acelerómetro y envía cada 200 ms la muestra más intensa al ESP32, que la reparte a esta pantalla.",
    points: [
      "Aceleración en tres ejes, en m/s² y en g.",
      "La gravedad indica cómo está inclinado el teléfono.",
      "Fuerza estimada con F = m·a usando la masa del iPhone 12 (164 g).",
    ],
    visual: "motion",
    reality: "La aceleración es medida; la fuerza es una estimación y no se calcula velocidad ni desplazamiento.",
  },
  {
    id: "rgb",
    title: "Luz que responde",
    lead: "El LED RGB físico cambia de color según el estado: en espera, con clientes, sincronizado o en movimiento.",
    points: [
      "Transiciones suaves, sin parpadeos rápidos.",
      "El LED virtual de la pantalla copia el color que reporta la placa.",
      "Un experimento mide cuánto tarda la luz en reaccionar a una agitación.",
    ],
    visual: "rgb",
  },
  {
    id: "stack",
    title: "Cómo funciona por dentro",
    lead: "Firmware en C++ en el ESP32, un protocolo propio con validación estricta y clientes web e iOS.",
    points: [
      "Protocolo sentinel.v1 sobre WebSocket con esquemas JSON verificados.",
      "Web en React + three.js servida por el propio ESP32.",
      "App nativa en SwiftUI compilada desde Linux.",
      "Pruebas automáticas en cada cambio y validación física en placa.",
    ],
    visual: "stack",
  },
  {
    id: "experiments",
    title: "Aprender midiendo",
    lead: "Cada experimento tiene objetivo, instrumentos, variables y exportación de datos.",
    points: [
      "Gravedad y orientación, movimiento, cobertura Wi-Fi, respuesta RGB y estabilidad.",
      "Los datos se descargan en CSV o JSON para analizarlos.",
      "Lo simulado lleva siempre la etiqueta DEMO.",
    ],
    visual: "experiments",
  },
  {
    id: "future",
    title: "Innovación y evolución",
    lead: "Un sistema abierto que une hardware real, protocolo verificable y visualización inmersiva.",
    points: [
      "Más sensores (temperatura, distancia por ultrasonidos) con el mismo protocolo.",
      "Varias placas para triangular la señal.",
      "Registro de sesiones para comparar entornos y grupos.",
    ],
    visual: "future",
  },
];

export function clampStep(index: number): number {
  return Math.min(TOUR.length - 1, Math.max(0, Math.trunc(index)));
}
