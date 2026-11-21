# SENTINEL RF v0.3.1 — API y datos

AP local: IP predeterminada 192.168.4.1, mDNS sentinel.local, SSID de secrets.h.
Este documento describe software compilado, no validación HTTP en la placa.

| Método | Ruta | Función |
|---|---|---|
| GET | / | Dashboard RF |
| GET | /motion | Dashboard v0.2 reutilizado |
| GET | /capture | Captura heredada que exige contexto seguro; no se usa en v0.3.1 |
| GET | /motion/native | Configuración del receptor nativo iOS |
| GET | /api/motion/status | Emparejamiento, contadores, intervalo, pérdida de señal y último código HTTP |
| POST | /api/motion/config | `deviceId` (vacío quita autorización) y `minIntervalMs` 10–1000 |
| POST | /api/motion/sensorlogger | Lote HTTP Push de Sensor Logger; fuente `NativeClient` |
| GET | /api/state | Estado de movimiento original; fw=0.3.1 |
| POST | /api/accel | x/y/z finitos en ±500 m/s², source y simulated; tiempo del ESP32. En RF rechaza `source` nativo (`403`) |
| POST | /api/config | Umbrales de movimiento, contrato v0.2 |
| GET | /api/rf | Disponibilidad, red, dispositivos, RSSI e historial |
| GET | /api/lab | Experimentos, modelos y registros |
| POST | /api/experiments | name (1–48 bytes), clientId existente, environment 0/1/2 |
| POST | /api/measurements | experimentId, actualM, calibration, notes, x/y opcionales |
| POST | /api/calibration | experimentId; ajusta modelo con puntos marcados de calibración |
| GET | /api/export.csv | Todos los registros con metadatos, streaming CSV |
| GET | /api/events | Eventos RF/movimiento, heartbeat y estado de notificaciones |

No hay endpoints para cambiar credenciales, encender GPIO, escanear redes o borrar datos.

## Semántica

- environment: 0 abierto, 1 interior, 2 obstáculos. Cada experimento pertenece a
  una sesión de cliente, con modelo independiente. No se transfiere calibración
  automáticamente a otro teléfono, reconexión o entorno.
- clientId es secuencial por arranque y asociación; bootId distingue reinicios.
  MAC no se publica. Alias solo en sessionStorage del navegador, no identidad real.
- lastSeenMs representa consulta de asociación; el SDK no informa fecha de cada
  paquete que produjo su RSSI promedio. Repetir consulta no garantiza paquete nuevo.
- rssiDbm es RSSI del cliente recibido por ESP32; filteredDbm es EMA. null es ausencia
  o lectura fuera del rango admitido [-100,-1], nunca cero ni medición inventada.
- available=false significa fallo de adquisición; no confirma desconexión. Un cliente
  asociado no demuestra HTTP exitoso ni internet. connected refleja asociación Wi-Fi.
- channel/frequencyMHz proceden del canal real; 1..13: 2407+5*canal MHz; 14: 2484 MHz.
- wavelengthM = 299792458 / frecuencia_Hz, longitud de onda en vacío aproximada para aire.
- actualM es distancia manual, 0..10000 m; para calibrar debe ser >0. x/y se suministran
  juntos, en metros, dentro de ±10000. Son coordenadas manuales, no GPS.
- notes máximo 96 bytes. El servidor captura RSSI y timestamp de la sesión elegida;
  no acepta RSSI desde este POST. calibration obligatorio booleano.
- Un registro desconectado requiere sesión aún retenida y adquisición RF disponible;
  RSSI, estimación y error quedan null. Si la sesión ya fue desalojada, responde 409.
- Dos o más distancias distintas con pendiente decreciente permiten ajustar modelo.
  n debe estar entre 0,5 y 8; calibración rechazada no destruye modelo anterior.
- estimatedM/errorM se guardan tal como eran al registrar. Calibrar después no modifica
  filas antiguas. Usar mediciones nuevas NO usadas para calibración para evaluar error.
- Timestamp relativo uint32 millis(), no fecha civil; se desborda aproximadamente cada
  49,7 días. No combinar sesiones por timestamp sin bootId.

## Límites y persistencia

4 clientes simultáneos del AP, 8 entradas de sesión, 32 muestras por sesión,
48 eventos del EventEngine, 8 experimentos, 96 mediciones. Memoria acotada en motores;
JSON/HTTP usa String y JsonDocument dinámicos. No se ha medido heap bajo carga real.
No se sobrescriben experimentos cuando se llenan: 409 y exportación manual.
RAM se pierde al reiniciar. CSV es la persistencia explícita en el dispositivo del
usuario; no existe recuperación NVS ni restauración automática. El CSV protege
comillas y prefijos de fórmulas en textos. Exportar antes de apagar.

Errores: 400 datos inválidos, 409 capacidad/estado incompatibles, 413 POST >1024
bytes en `/api/accel` y `/api/config` (>16 KiB en `/api/motion/sensorlogger`),
503 adquisición no disponible. El límite de cuerpo se comprueba después de que
WebServer lo recibe; no es una defensa contra clientes hostiles ni un límite
estricto de asignación de memoria del parser HTTP.

## Receptor de movimiento nativo (v0.3.1)

El sobre, unidades, parejas acelerómetro/gravedad, marcas de tiempo, límites,
emparejamiento y rechazos se describen en `docs/rf-ios.md`. El adaptador es C++
puro (`include/rf/motion_ingest.h`) y alimenta el mismo `MotionEngine` que
`/api/accel`. `deviceId` se autoriza explícitamente desde `/motion/native`; antes
de autorizar, el primer dispositivo aparece solo como `pendingDeviceId` y no se
acepta ninguna muestra. `lastSampleEpochNs` es tiempo UTC del teléfono en ns;
`lastAcceptedMs` y los campos de estado usan `millis()` del ESP32. La simulación
nunca entra por este endpoint. Sin evidencia física, estos campos son solo
contrato de software.

## Eventos

ID = boot_id + sequence. event, client_id, timestamp_ms, simulated y notify.
rssi_dbm y distance_estimated_m son null si faltan. Actualmente las alertas se basan
en señal/estado: no se calcula distancia para eventos ni se emite
COVERAGE_LIMIT_REACHED automáticamente. Ese nombre está reservado; pérdida de
asociación no demuestra un límite físico de alcance. El dashboard muestra el mayor
punto con asociación observada bajo las condiciones del experimento.

DEVICE_CONNECTED/DISCONNECTED se detectan por sondeo 1 Hz; pueden perderse
asociaciones más breves. SIGNAL_WEAK: tres lecturas <−80; recuperación: tres >−75;
lecturas fuera del criterio rompen la racha. Cooldown de avisos 30 s; el historial
conserva las transiciones. No hay identificación pasiva de redes/dispositivos ajenos.

## Fórmulas y alcance de inferencia

RSSI(d)=RSSI(d0)−10 n log10(d/d0); d=d0·10^((RSSI(d0)−RSSI)/(10n)).
El ajuste usa d0=1 m, regresión lineal respecto a log10(d/1m). El intercepto puede
ser extrapolado si no se midió a 1 m: incluir un punto de referencia a 1 m.
Filtro: rssi_f=0,75·rssi_f_anterior+0,25·rssi. Error absoluto: |estimada−real|.
Orientación, cuerpo humano, obstáculos, multitrayecto, tráfico y potencia del teléfono
alteran RSSI. No se ofrecen precisión fija ni intervalos de confianza.

Radar: anillo de posibles posiciones para una distancia, puntos solo con X/Y manual.
Interpolación IDW opcional dentro de la envolvente convexa de ≥3 posiciones no
alineadas, rotulada APROXIMACIÓN. Colores de calidad son categorías didácticas,
no rendimiento medido ni garantía de transmisión.

Movimiento: |a|=sqrt(x²+y²+z²), gravedad filtrada y dinámica aproximada a−g.
No integra velocidad/desplazamiento. No presenta F=m·a porque no hay masa/modelo.
