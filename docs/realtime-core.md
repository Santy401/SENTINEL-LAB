# SENTINEL Core v0.4.0 — telemetría WebSocket local

## Estado

Implementado y probado en host. **No flasheado, no medido en hardware y no listo
para producción.** El firmware v0.3.1 y su API HTTP permanecen intactos.

## Compilación

```bash
pio run -e sentinel_esp32s3_n16r8_realtime
```

Este entorno extiende `_rf`, añade `src/realtime/*` y define
`SENTINEL_REALTIME=1`. Los otros entornos no compilan el núcleo en tiempo real.

## Superficie de red

| Transporte | Puerto | Ruta | Estado |
|---|---:|---|---|
| HTTP v0.3.1 | 80 | `/api/rf`, `/api/lab`, `/api/events`, etc. | Sin cambios |
| HTTP de salud | 80 | `/api/realtime/status` | Nueva, solo en `_realtime` |
| WebSocket | 81 | `/realtime` | Nuevo en v0.4.0 |

El endpoint solo escucha en el AP local de laboratorio. El handshake exige
`Upgrade: websocket`, `Connection: Upgrade`, `Sec-WebSocket-Key`,
`Sec-WebSocket-Version: 13` y `Sec-WebSocket-Protocol: sentinel.v1`. La
respuesta `101` devuelve `Sec-WebSocket-Accept` calculado sobre la clave y
`Sec-WebSocket-Protocol: sentinel.v1`. Un cliente debe verificar ambos valores;
no basta con recibir `101`.

### Ruta de salud de ejecución

`GET /api/realtime/status` existe únicamente en el entorno `_realtime` y no
requiere token. Devuelve exactamente estos campos:

| Campo | Tipo | Uso |
|---|---|---|
| `fw` | string | Versión del firmware HTTP v0.3.1 |
| `realtimeVersion` | string | Versión del núcleo en tiempo real |
| `bootId` | string | Identificador del arranque observado |
| `uptimeMs` | integer | Milisegundos monotónicos desde el arranque |
| `clockSynced` | boolean | Si existe una fuente UTC válida |
| `clientCount` | integer | Pares WebSocket activos |
| `freeHeapBytes` | integer | Heap libre observado |
| `minFreeHeapBytes` | integer | Mínimo de heap libre muestreado |
| `heapSamples` | integer | Número de muestras de heap |
| `droppedFrames` | integer | Tramas descartadas por retroceso |
| `rejectedMessages` | integer | Mensajes entrantes rechazados |
| `droppedTelemetry` | integer | Sobres v1 no publicados |
| `skippedSimulatedEvents` | integer | Eventos simulados omitidos |

Estos valores son observaciones de ejecución. No sustituyen la medición de heap
en placa: la puerta `runtime.heap_and_stability` del kit P2 los evalúa sobre
evidencia capturada.

## Reloj y arranque

El sobre v1 requiere ISO 8601 UTC. El ESP32 solo conoce `millis()`, por lo que
el adaptador **no emite ningún mensaje v1** hasta que una de estas fuentes
proporcione una fecha válida:

1. Un servidor NTP local configurado en el gitignored `include/rf/connected.h`
   mediante `SENTINEL_RF_NTP_SERVER`. El firmware llama a `configTime()` en
   `begin()` y reintenta la lectura cada 5 s; la SNTP es asíncrona, por lo que
   `time()` puede no devolver una fecha plausible hasta unos segundos después.
2. Un cliente que se identifique como `gateway` en `connection.hello`, con
   `Authorization: Bearer <token>` en el handshake. El timestamp de ese saludo
   sincroniza el reloj; el firmware no acepta un saludo `gateway` sin token.

El campo del sobre que aporta la fecha es **`timestamp`** (ISO 8601 UTC con
`Z`), no `epochMs`. `InboundValidator::validate()` parsea `timestamp` con
`UtcClock::parse()` y guarda internamente el valor como `epochMs`; el esquema
`connection.hello.schema.json` prohíbe campos adicionales, por lo que añadir
`epochMs` al sobre lo invalidaría. El kit de hardware registra `hostEpochMs`
solo en su evidencia local para auditar la marca enviada.

El token se define como `SENTINEL_REALTIME_BEARER_TOKEN` en `include/secrets.h`.
La plantilla `include/secrets.example.h` lo deja vacío: con token vacío nadie
puede sincronizar el reloj por WebSocket. No hay credenciales en el repositorio.

Un cliente normal puede conectarse antes de que exista reloj, pero no recibirá
mensajes hasta que la sincronización ocurra. El saludo queda pendiente y se envía
automáticamente cuando el reloj se sincroniza, incluso si la fecha la aportó
otro par. Después del handshake y con reloj válido, el servidor envía
`connection.hello`.

## Mensajes publicados

| Tipo | Cadencia | Contenido |
|---|---:|---|
| `connection.hello` | al conectar | versión, `bootId`, rol `firmware`, capacidades |
| `rf.telemetry` | 1 s | canal, clientes, RSSI, `lastSeen`, distancia estimada |
| `system.status` | 15 s | estado operativo, capacidades, `freeHeapBytes` |
| `motion.status` | 2 s | emparejamiento, muestras, pérdida de señal |
| `connection.heartbeat` | 5 s | `bootId` y `uptimeMs` |
| `event` | sondeo cada 100 ms | conexión, desconexión, señal débil y recuperación |
| `error` | cuando aplica | código estable, sin volcar payloads |
| `connection.goodbye` | coordinado por cliente | motivo explícito |

Todos los mensajes difundidos usan `source = "esp32-s3-realtime"` y una sesión
de sistema `<bootId>-s<generación>`. Los mensajes por conexión usan
`<bootId>-p<ranura>-<generación>`. El contador de generación es global, por lo
que una reconexión en la misma ranura nunca repite `sessionId`. La terna
`source` + `sessionId` + `sequence` es monótona y distinta entre reinicios.

`clientCount` siempre coincide con la longitud de `clients`. No se publica MAC.
`lastSeen` se deriva de `lastSeenMs` usando el anclaje del reloj sincronizado;
si el reloj no es válido, no se publica el sobre. `estimatedDistance` aparece
solo si existe una medición de laboratorio y su experimento tiene un modelo
calibrado.
La distancia sigue siendo una estimación didáctica, no una medición física
directa.

Un estado con fuente `Simulated` o `None` no genera `motion.status` v1. Tampoco
se publica si `deviceId` no cumple el patrón del esquema v1; HTTP v0.3.1 admite
un rango más amplio y el adaptador no reescribe ni inventa identidades. Los
eventos con `simulated=true` se descartan. El puente de eventos usa el número
de secuencia de `EventEngine`, por lo que no reenvía el anillo histórico al
reconectar y omite eventos ya vistos.

`system.status.operationalState` usa solo los valores del esquema aprobado:
`BOOTING`, `IDLE`, `MONITORING` y `ERROR`. Una alerta de movimiento se reporta
en `motion.status` y `event`, no como estado operativo inválido; sensor sin
señal se publica como `ERROR`. Las capacidades publicadas son
`rf.monitor`, `motion.ingest` y `laboratory`, todas permitidas por el contrato.

## Límites y retroceso

| Recurso | Límite |
|---|---|
| Pares WebSocket simultáneos | 4 |
| Trama entrante | 1024 bytes |
| Trama de control | 125 bytes |
| Cola de salida por par | 2 tramas |
| Mensajes entrantes por par | 8 por segundo |
| Inactividad | 60 s |
| Fragmentación | rechazada |
| Tramas sin máscara | rechazadas |
| Sobres generados | 1016 bytes para el sobre serializado completo por búfer fijo |

Si la cola se llena, se descarta la trama nueva y se incrementa un contador de
retroceso. No se reserva un búfer de 16 KiB por cliente. Las tramas mal
formadas, fragmentadas, sobredimensionadas o con opcode desconocido cierran la
conexión; los errores de protocolo v1 se responden con `error`.

## Cliente mínimo

```text
GET /realtime HTTP/1.1
Host: sentinel.local
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Key: <clave base64 aleatoria>
Sec-WebSocket-Version: 13
Sec-WebSocket-Protocol: sentinel.v1
Authorization: Bearer <token>   ; solo para rol gateway
```

Después de `101 Switching Protocols`, un `gateway` debe enviar:

```json
{
  "protocolVersion": "1.0.0",
  "type": "connection.hello",
  "timestamp": "2026-11-21T16:10:02.100Z",
  "sequence": 1,
  "source": "gateway-lab-01",
  "sessionId": "gateway-run-001",
  "payload": {
    "deviceId": "gateway-01",
    "role": "gateway",
    "supportedVersion": "1.0.0",
    "capabilities": ["telemetry"]
  }
}
```

La secuencia entrante debe aumentar. Un saludo `gateway` sin token válido se
responde con `UNAUTHORIZED`; no modifica el reloj. Los roles `web`,
`sensor-client` e `ios` no requieren token pero tampoco pueden sincronizarlo.
El firmware valida la línea de petición exacta, los tokens `websocket` y
`upgrade`, una clave base64 canónica de 16 bytes, `Sec-WebSocket-Version: 13` y
el token exacto `sentinel.v1` en `Sec-WebSocket-Protocol`. Una petición con otra
versión recibe `426 Upgrade Required`; una petición incompleta recibe `400`. No
se negocian subprotocolos alternativos: solo se anuncia `sentinel.v1`. El kit
`tools/hw_capture.py` envía y comprueba ambos campos en la respuesta.

## Verificación

```bash
python3 -m py_compile tools/hw_common.py tools/hw_capture.py tools/hw_report.py
python3 tools/check_software.py
python3 scripts/check-repository.py
python3 packages/protocol/validate_protocol.py build/firmware-*.json
pio run -e sentinel_esp32s3_n16r8_realtime
python3 tools/hw_report.py --help
```

Los resultados concretos y el tamaño del binario se registran en
`docs/validation/p2-software.md`. La secuencia completa de evidencia física, sus
puertas de aceptación y las advertencias de flasheo están en
`docs/validation/p2-hardware.md`. **No hay validación de hardware hasta que ese
informe devuelva PASS sobre evidencia real.**
