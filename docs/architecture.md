# SENTINEL — Arquitectura del firmware

## Módulos

| Archivo | Responsabilidad |
|---|---|
| `include/config.h` | Pines, umbrales y tiempos. Única fuente de configuración. |
| `include/distance_sensor.h` / `src/distance_sensor.cpp` | Driver no bloqueante del HC-SR04 con ISR, timeout y filtro mediano. |
| `include/servo_controller.h` / `src/servo_controller.cpp` | Envoltorio de `ESP32Servo` con límites y movimiento progresivo. |
| `src/main.cpp` | Arranque, información de placa y máquina de estados. |

## Máquina de estados

```text
BOOTING -> IDLE -> MONITORING
                     |     ^
                     |     | (clear confirmado x3)
                     v     |
             INTRUSION_DETECTED -> ACTUATING
                     |                |
                     +----------------+
                            ERROR
```

- **BOOTING**: Serial, identificación de chip/flash/PSRAM, init de periféricos.
- **IDLE**: espera corta de estabilización.
- **MONITORING**: lectura de distancia cada 250 ms.
- **INTRUSION_DETECTED**: confirmación de 3 lecturas válidas por debajo de 20 cm.
- **ACTUATING**: servo en posición de protección; espera 3 lecturas válidas por
  encima de 30 cm para volver a casa.
- **ERROR**: exceso de lecturas inválidas; recuperación automática al recibir una
  lectura válida.

## Reglas de decisión

- Una lectura inválida **nunca** se interpreta como "sin objeto".
- Histéresis: 20 cm para disparar, 30 cm para liberar.
- El servo permanece deshabilitado por defecto (`kServoEnabledByDefault=false`)
  hasta verificar alimentación externa de 5 V.

## Dependencias verificadas

- `platformio/espressif32@8.0.0`
- `framework-arduinoespressif32@4.30312.0` (Arduino-ESP32 3.x)
- `madhephaestus/ESP32Servo@3.2.1` (compatible con ESP32-S3, LEDC/MCPWM)

Se eligió `esp32-s3-devkitc-1` por coincidir en `hwids 303A:1001` y en la
variante `esp32s3`. Los overrides de `platformio.ini` ajustan flash a 16 MB,
particiones `default_16MB.csv` y PSRAM OPI (`qio_opi`).

## Preparación para fases futuras (estado en v0.1.0; véase v0.2.0 al final)

Los puntos de extensión previstos son:

- Conectividad Wi-Fi y API HTTP: añadir módulo `net_service.*`.
- Eventos JSON: centralizar `logEvent()` en un `event_bus.*`.
- Autenticación del dispositivo: derivar de MAC + efuse, sin secretos en el repo.
- LCD y notificaciones: nuevos drivers detrás de interfaces simples.

Se evita introducir esas dependencias hasta que la fase actual esté validada.

## Verificación sin hardware

Posibles pruebas de lógica (futuras): simulación de secuencias de distancias para
comprobar contadores de confirmación e histéresis mediante `test/`.

---

# SENTINEL v0.2.0 — Dashboard local y detección de movimiento

## Arquitectura implementada

```text
iPhone (Safari)
   |  HTTP local / API JSON
   v
ESP32-S3 (AP 2,4 GHz + WebServer puerto 80)
   |                    |                    |
   v                    v                    v
AccelSample        MotionEngine        LedIndicator
(origen:           (gravedad,          (3 salidas
 browser /          umbrales,           virtuales)
 simulado /         histéresis,
 nativo)            timeout)
```

El origen de datos está desacoplado del motor: el HTTP convierte cada petición
en un `AccelSample` con `timestampMs` asignado por el ESP32 y una etiqueta de
origen. El motor no conoce Arduino, WiFi ni Serial, por lo que se prueba en host.

| Archivo | Responsabilidad |
|---|---|
| `include/v02/settings.h` | Constantes de red, detección y límites. |
| `include/secrets.example.h` → `include/secrets.h` | Credenciales Wi-Fi (fuera del repo). |
| `include/v02/accel_source.h` | `AccelSample` y etiquetado de origen. |
| `include/v02/motion_engine.h` / `src/v02/motion_engine.cpp` | Filtro de gravedad, magnitudes, histéresis, estados, eventos y timeout. Lógica pura. |
| `include/v02/led_indicator.h` / `src/v02/led_indicator.cpp` | Tres indicadores virtuales; sin GPIO. |
| `include/v02/wifi_ap.h` / `src/v02/wifi_ap.cpp` | AP WPA2, mDNS y dirección IP por Serial. |
| `include/v02/dashboard_html.h` | Dashboard HTML/CSS/JS embebido en PROGMEM. |
| `include/v02/web_dashboard.h` / `src/v02/web_dashboard.cpp` | Rutas HTTP, JSON de estado, ingesta y configuración. |
| `src/v02/main_v02.cpp` | Arranque, heartbeat y bucle. |
| `test/test_motion_engine.cpp` | Pruebas host de transiciones, timeouts e histéresis. |
| `docs/protocol.md` | Contrato HTTP, semántica de estados y compatibilidad iOS. |

## Máquina de estados (v0.2.0)

```text
  SENSOR_OFFLINE --primera muestra--> STABLE
        ^                              |  ^
        | timeout                      |  | 3 muestras < movement - hyst.
        |                              v  |
        +------------- MOVEMENT <---- ALERT
                        3 > movement   3 > alert
```

- `STABLE`, `MOVEMENT`, `ALERT`, `SENSOR_OFFLINE`.
- El evento `Timeout` se registra al perder la comunicación; `Recovery` al
  recibir una muestra nueva. `update()` nunca recupera por sí solo.
- Una muestra simulada se etiqueta como tal en `source` y en la interfaz.

## Aislamiento entre versiones

`platformio.ini` define tres entornos:

| Entorno | Fuentes | Uso |
|---|---|---|
| `sentinel_esp32s3_n16r8` | `+<*> -<v02/*>` | v0.1.0 original (HC-SR04 + servo deshabilitado). |
| `sentinel_esp32s3_n16r8_diag` | `+<*> -<v02/*>` | Diagnóstico HC-SR04 sin servo. |
| `sentinel_esp32s3_n16r8_v02` | `+<v02/*>` | v0.2.0 (Wi-Fi + dashboard). |

El entorno por defecto sigue siendo v0.1.0. El HC-SR04 y el servo **no** se
compilan ni se activan en v0.2.0. No se asignan GPIO a los LED.

## Decisiones de diseño

- Dashboard embebido en PROGMEM: evita un paso extra de subida de sistema de
  archivos y reduce el riesgo de corrupción; el HTML ocupa flash, no RAM.
- ArduinoJson 7 (dinámico) y `WebServer` del framework; sin frameworks JS.
- Sondeo (`polling`) de 700 ms en lugar de WebSocket: suficiente para 2 Hz y
  mucho más simple y estable en un MCU.
- Timestamps de servidor: inmune a relojes de teléfono desincronizados.
- Buffer circular de 24 eventos en el motor, 8 en la telemetría.



# SENTINEL RF v0.3.0

| Capa | Implementación |
|---|---|
| Adquisición | rf/rf_monitor.cpp: API de asociaciones ESP-IDF y RSSI promedio |
| Procesamiento | DistanceEstimator/RssiFilter, EventEngine, MotionEngine reutilizado |
| Experimentos | Laboratory: modelos por experimento, 96 registros, capacidad explícita |
| Visualización | web_rf.cpp y dashboard_html.h; dashboard v0.2 en /motion |
| Persistencia | CSV descargable; estado operativo en RAM, sin NVS |
| Notificación | notifier.cpp: cola FreeRTOS y HTTPS opcional, sin bloqueo en loop |
| iOS | /capture y puente TLS Linux optativo, pendiente de validación real |

El entorno RF compila src/rf/* y solo motion_engine.cpp, led_indicator.cpp,
wifi_ap.cpp y web_dashboard.cpp de v0.2. Los entornos antiguos excluyen rf/*;
v0.2 mantiene su filtro original. Las rutas adicionales y validación de entrada
se habilitan en WebDashboard exclusivamente con SENTINEL_RF. No cambia el motor
original ni los pines. WiFiAp inicia AP; solo configuración privada optativa amplía
a AP+STA. Consultar rf-protocol.md para límites, unidades y semántica científica.
