# SENTINEL — Conexionado y seguridad eléctrica

Este documento describe la **propuesta** de cableado. No asume que el hardware
ya esté conectado. Verifica físicamente antes de alimentar.

## Placa detectada

- ESP32-S3 QFN56, revisión v0.2, 240 MHz, Wi-Fi + BT LE.
- Flash 16 MB (`c2:2018`, quad, 3,3 V).
- PSRAM embebida 8 MB (OPI).
- USB nativo Serial/JTAG: `/dev/ttyACM0`, VID:PID `303a:1001`.
- MAC: `ac:27:6e:aa:c8:4c`.

## Mapa de pines

| Elemento | Señal | ESP32-S3 | Módulo | Notas |
|---|---|---|---|---|
| HC-SR04 | TRIG | GPIO4 | Trigger | Salida 3,3 V; aceptada por HC-SR04 |
| HC-SR04 | ECHO | GPIO5 | Echo | **5 V → divisor → 3,3 V obligatorio** |
| SG90 | PWM | GPIO6 | Signal | LEDC/MCPWM, 50 Hz, 500–2400 µs |
| — | — | GPIO7 | — | Reservado para futuro LCD/LED |
| Fuente 5 V | VCC | — | HC-SR04 VCC + Servo VCC | Fuente externa **≥ 2 A** |
| Común | GND | GND | Todos los GND | **Masa común obligatoria** |
| Servo | — | — | 100–470 µF entre 5 V y GND | Cerca del servo |

## Divisor para ECHO (no negociable)

```text
HC-SR04 ECHO (5 V)
        │
      R1 = 1 kΩ
        │
        ├────────── GPIO5 (ESP32-S3)
        │
      R2 = 2 kΩ
        │
       GND

V_GPIO5 = 5 V × 2/(1+2) = 3,33 V
```

Alternativa: `R1=4,7 kΩ`, `R2=10 kΩ` → 3,40 V (aceptable).

## Esquema textual

```text
     Fuente 5 V (≥2 A)
        ├── HC-SR04 VCC
        ├── Servo VCC ──┬── 100 µF ── GND
        └── ESP 5V (si no usas USB)

     GND común ──┬── ESP GND
                 ├── HC-SR04 GND
                 ├── Servo GND
                 └── R2 (2 kΩ) del divisor

     HC-SR04 TRIG ───────────── GPIO4
     HC-SR04 ECHO ──1 kΩ──┬──── GPIO5
                         2 kΩ
                          │
                         GND

     Servo SIGNAL ──220 Ω────── GPIO6
```

## Pines prohibidos

- GPIO19 / GPIO20 → USB nativo D−/D+.
- GPIO0, GPIO3, GPIO45, GPIO46 → strapping / boot.
- GPIO26…GPIO37 → flash y PSRAM del módulo N16R8.
- GPIO43 / GPIO44 → UART0 de arranque.

## Checklist antes de probar

- [ ] Divisor ECHO montado y medido con multímetro (≤ 3,3 V en GPIO5).
- [ ] Fuente externa de 5 V con ≥ 2 A disponible.
- [ ] Masa común entre ESP, HC-SR04, servo y fuente.
- [ ] Capacitor 100–470 µF en la línea de alimentación del servo.
- [ ] Confirmado que el servo **no** se alimenta desde GPIO ni desde 3,3 V.
- [ ] `kServoEnabledByDefault = true` solo **después** de verificar lo anterior.

## Estado de pruebas físicas

No se ha conectado ni alimentado ningún sensor/servo durante el desarrollo de
esta fase. Las pruebas físicas quedan pendientes de autorización.

## Modo diagnóstico (sin servo)

Existe un entorno de compilación separado dentro del mismo proyecto:

```bash
pio run -e sentinel_esp32s3_n16r8_diag
pio run -e sentinel_esp32s3_n16r8_diag -t upload --upload-port /dev/ttyACM0
```

Imprime por Serial a 115200 baudios:

- duración del pulso ECHO en microsegundos,
- distancia filtrada en centímetros,
- estado `VALID`, `NO_ECHO` u `OUT_OF_RANGE`,
- contadores de lecturas válidas, sin eco y fuera de rango,
- mínimo, máximo y promedio,
- resumen al entrar en `ERROR`.

El servo permanece deshabilitado porque ambos entornos comparten
`kServoEnabledByDefault = false`.

**Este modo no sustituye la adaptación de nivel.** Si ECHO entrega 5 V, no se
debe conectar a GPIO5 aunque el firmware muestre mediciones.
