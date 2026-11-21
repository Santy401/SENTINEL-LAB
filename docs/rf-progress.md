# Avance SENTINEL RF v0.3.0

## Inspección inicial

Los tres entornos originales compilaron antes de cambios. Ambas pruebas host
originales pasaron. No hay directorio .git en esta copia; el inventario de hashes
originales está en build/baseline-sha256.json (sin secrets.h).
No existe /dev/ttyACM0 en esta sesión: no se ha verificado hardware.

## M1

Entorno sentinel_esp32s3_n16r8_rf separado. Reutiliza MotionEngine, LedIndicator,
WiFiAp y WebDashboard. Dashboard original conservado en /motion. Monitor consulta
esp_wifi_ap_get_sta_list cada segundo; el SDK instalado expone wifi_sta_info_t.rssi
como promedio. No se conoce la fecha del paquete que produjo ese promedio:
lastSeenMs es la fecha de consulta de asociación, no de recepción RF.
RSSI válido para este laboratorio: [-100,-1] dBm; fuera del rango se publica null.
Ocho entradas de sesión (cuatro conexiones AP simultáneas), 32 lecturas por cliente,
32 cambios de asociación. Las MAC permanecen internas, sin exposición HTTP.
Reconciliación por sondeo: una conexión y desconexión entre consultas puede perderse.
Los eventos Wi-Fi existen en el SDK; integración por cola pendiente.

## M2

DistanceEstimator puro: regresión lineal RSSI frente a log10(distancia/1m).
Requiere dos o más distancias positivas distintas y pendiente física decreciente.
Rango admitido n=0,5..8, restricción del laboratorio, no ley universal.
Sin calibración o RSSI válido, devuelve NaN (API lo representa como null).
Filtro EMA 0,75 previo + 0,25 nuevo, reiniciado ante lectura inválida/desconexión.
No hay intervalos de confianza ni precisión declarada. Tests usan datos sintéticos.


## M3 — Experimentos y mapa

Implementados modelos por experimento, registro de observaciones capturadas por
ESP32, distancias/coordenadas manuales, tabla de error y exportación CSV.
Radar representa anillo, nunca dirección inferida. Cobertura: puntos y aproximación
IDW opcional dentro de envolvente convexa, con al menos tres posiciones no alineadas.
Gráficas RSSI/tiempo, RSSI/distancia y estimada/real. RAM conserva hasta 96 mediciones;
no hay persistencia automática tras reinicio. Pruebas de capacidad/calibración pasan.

## M4 — Movimiento e iOS

MotionEngine y LedIndicator originales se reutilizan sin cambios. API RF rechaza
XYZ ausente/no finito; fuente y timestamp de recepción se conservan. /capture
exige HTTPS/permiso y espera muestras reales antes de informar captura efectiva.
Puente HTTPS local preparado, no iniciado; requiere certificado/CA y autorización
para cambiar red/iniciar servicio. Sintaxis Python/JS verificada. iPhone no probado.

## M5 — Eventos y automatización

EventEngine puro con 48 eventos circulares, histéresis, rachas consecutivas, IDs
por arranque y cooldown. Transiciones RF y movimiento, marca de simulación,
heartbeat 15 s. Pruebas host de eventos, recuperación, buffer y JSON pasan.
COVERAGE_LIMIT_REACHED está reservado, no se infiere automáticamente alcance máximo.
Asociación por sondeo: eventos rápidos entre sondeos pueden perderse; no hay cola
de eventos Wi-Fi del SDK. Los contadores de señal aportan confirmación temporal,
pero conexiones/desconexiones no tienen debounce temporal adicional.

Adaptador HTTPS en tarea separada, cola/reintentos acotados, CA y autenticación;
AP+STA optativo mediante archivo privado que no existe en esta sesión. NTP local
optativo; sin reloj válido no envía. No se desplegaron servicios ni enviaron mensajes.
Workflow n8n inactivo: autenticación, validación, filtro, deduplicación acotada,
watchdog y nodo Telegram que guarda token y chat ID en credenciales. TypeScript
compila contra dependencias fijadas en package-lock. Tests de lógica del workflow
pasan sin runtime n8n. Importación y funcionamiento real siguen pendientes.

## M6 — Evidencia de software y límites físicos

- Compilación de los cuatro entornos: SUCCESS.
- v0.2: RAM 49.900 bytes, flash 968.899 bytes, iguales al estado inicial.
- Cinco ejecutables host: algoritmo original, movimiento original, RF/modelo/lab,
  monitor con cuatro clientes del SDK simulado y serialización de eventos: pasan.
- Tests JS del workflow: validación, duplicados, watchdog y recuperación: pasan.
- Sintaxis JS y referencias DOM de los tres HTML: correctas.
- Python del puente y del verificador: sintaxis correcta; puente no arrancado.
- Paquete del nodo n8n: tsc correcto; no equivale a importación en n8n.
- Advertencias de los entornos antiguos: MCPWM obsoleto/ESP32Servo; no se cambió
  esa dependencia y no aparece en el entorno RF.

La memoria publicada por PlatformIO es estática, no el pico de heap de JSON/TLS.
Los valores del modo AP no dimensionan la tarea TLS cuando se habilite configuración
privada. Carga con múltiples clientes físicos, latencia y estabilidad quedan pendientes.
No existe /dev/ttyACM0 aquí. No se flasheó, cambió Wi-Fi del host, conectó hardware,
configuró credenciales, desplegó n8n ni se enviaron notificaciones Telegram.

Ver rf-demo.md para la lista de evidencia física que debe recogerse tras autorización.

## Cierre de compilación

RF final: RAM estática **71.676 bytes (21,9 %)**; programa en flash
**1.034.463 bytes (15,8 % de la partición de aplicación)**.
Ver build/rf-validation.json para el hash del binario y el resumen de pruebas.
El binario contiene configuración de red compilada: no distribuirlo públicamente.

Comprobación de credenciales sin imprimir valores: SSID coincide con SENTINEL-LAB;
macro de contraseña presente y longitud mínima válida. secrets.h no fue modificado.
La IP 192.168.4.1 es la configuración predeterminada, no una nueva verificación física.
