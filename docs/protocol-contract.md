# Contrato de protocolo SENTINEL v1.0.0

## 1. Alcance y estado

Este documento define el contrato compartido de mensajería de SENTINEL-LAB.
La versión actual es **1.0.0**. El contrato es **transport-independent**: el
mismo sobre JSON puede viajar en una respuesta HTTP, en un frame WebSocket o en
un archivo de evidencia.

Estado real:

- **Implemented**: esquemas, validador, ejemplos, pruebas automáticas y
  adaptador en el firmware v0.4.0.
- **Software tested**: 18 pruebas de contrato, 9 ejemplos válidos, 16 casos
  negativos, 8 fixtures generados por el firmware y 14 programas host.
- **Planned**: adaptadores en web, gateway e iOS.
- **Hardware tested**: no. La imagen v0.3.1 y sus endpoints HTTP no cambian;
  el transporte WebSocket existe solo en el entorno experimental `_realtime`.

## 2. Principios

- Toda medición publicada es real; los mensajes de medición exigen
  `simulated: false`.
- Un estado con fuente simulada no se publica como `motion.status` de v1; la
  simulación queda fuera de la superficie de medición del contrato.
- El sobre es estricto: no se aceptan campos desconocidos.
- Los esquemas son la fuente de verdad de la forma de cada mensaje y tienen
  identificadores HTTPS resolubles.
- La versión del protocolo se declara en cada mensaje; `connection.hello` además
  declara la versión que soporta y debe coincidir con el sobre.
- Los errores son legibles por máquina, no contienen secretos y asocian la
  respuesta con el `source` del mensaje rechazado.
- No se publica MAC como identidad de dispositivo. `clientId` es secuencial por
  arranque y asociación; `bootId` distingue reinicios. No se inventan nombres de
  dispositivo ni se afirma descubrir equipos fuera del AP del ESP32.
- RSSI no determina una distancia exacta; `estimatedDistance` es una estimación
  condicionada a calibración física.

## 3. Sobre compartido

Todo mensaje contiene exactamente estos campos:

| Campo | Tipo | Regla |
|---|---|---|
| `protocolVersion` | string | SemVer `MAJOR.MINOR.PATCH`; en v1 debe ser `1.0.0` |
| `type` | string | Tipo de mensaje del registro cerrado |
| `timestamp` | string | ISO 8601 UTC con `Z`; milisegundos opcionales |
| `sequence` | integer | Monótono dentro de una sesión, de `0` a `2^53 - 1` |
| `source` | string | Identificador del emisor, 1–64 caracteres |
| `sessionId` | string | Identificador de sesión del emisor, 1–64 caracteres |
| `payload` | object | Contenido validado según `type` |

La terna `source` + `sessionId` + `sequence` permite deduplicación y detección
de pérdida de mensajes sin depender del transporte. `sessionId` cambia al
reiniciar el productor o al restablecer una conexión; evita colisiones cuando
`sequence` vuelve a cero. El validador comprueba la forma de la terna; el
seguimiento de monotonía es estadoful y está implementado en el adaptador P2 del
firmware.

## 4. Tipos de mensaje

| `type` | Payload | Uso |
|---|---|---|
| `motion.sample` | `motion.sample.schema.json` | Muestra real de aceleración y gravedad |
| `motion.status` | `motion.status.schema.json` | Estado del ingestor nativo de movimiento |
| `rf.telemetry` | `rf.telemetry.schema.json` | RSSI real, clientes y estimación opcional |
| `system.status` | `system.status.schema.json` | Estado operativo y capacidades del nodo |
| `event` | `event.schema.json` | Evento acotado del laboratorio |
| `connection.hello` | `connection.hello.schema.json` | Saludo inicial del par |
| `connection.heartbeat` | `connection.heartbeat.schema.json` | Latido de vida independiente del transporte |
| `connection.goodbye` | `connection.goodbye.schema.json` | Cierre ordenado con motivo explícito |
| `error` | `error.schema.json` | Error estructurado, reintentable o no |

Los tipos `motion.*` conservan la semántica de `NativeClient`, `SensorLogger` y
los estados `STABLE`, `MOVEMENT`, `ALERT` y `SENSOR_OFFLINE` ya presentes en el
firmware. `motion.status` no acepta `sourceType: "Simulated"`.

El enum de `event.name` replica los nombres emitidos por `EventEngine`:
`DEVICE_CONNECTED`, `DEVICE_DISCONNECTED`, `SIGNAL_WEAK`, `SIGNAL_RECOVERED`,
`COVERAGE_LIMIT_REACHED`, `MOVEMENT_DETECTED`, `MOTION_ALERT`, `SENSOR_OFFLINE`,
`SENSOR_RECOVERED` y `HEARTBEAT`. Los eventos incluyen `clientId`, nunca MAC.

## 5. Validación

Los esquemas usan JSON Schema Draft 2020-12 y publican `$id` HTTPS bajo la ruta
del repositorio. `validate_protocol.build_registry()` expone el registro local
para consumidores que trabajen sin red. La validación canónica incluye:

- `additionalProperties: false` en sobre y payloads.
- `required` explícito por tipo de mensaje y `$ref` absolutos resueltos.
- Enums cerrados para tipos, estados, roles, eventos y códigos de error.
- Rangos numéricos, longitudes y coherencia de `clientCount` con `clients`.
- Timestamps UTC con forma y fecha válidas mediante `rfc3339-validator`.
- Rechazo de `NaN`, `Infinity` y claves duplicadas tanto al cargar JSON como al
  llamar directamente a `validate_message()`.
- Comprobación cruzada de `connection.hello.supportedVersion`.
- Límite de tamaño del sobre serializado y mensajes de error sin volcados del
  payload.

### Límites principales

| Elemento | Límite |
|---|---|
| Sobre serializado | 16 KiB en UTF-8 compacto |
| `sequence` | `0` a `9007199254740991` |
| `deviceId`, `bootId`, `source`, `sessionId`, `eventId` | 1–64 caracteres |
| Clientes RF por mensaje | 8 |
| Capacidades por saludo | 16 |
| Vector de movimiento | `-500` a `500` m/s² por eje |
| RSSI | `-100` a `-1` dBm o `null` si no hay lectura |
| Distancia estimada | `0` a `10000` m |
| Canal Wi-Fi | `1` a `14` |
| Mensaje de error | 1–256 caracteres |

Las dependencias `jsonschema` y `rfc3339-validator` existen solo en la
herramienta de validación. El firmware no las utiliza y no se le exige ninguna
biblioteca nueva.

### Implicaciones de memoria

El límite de 16 KiB se define sobre bytes de transporte canónicos, no sobre el
objeto en memoria. Un gateway debe rechazar el mensaje mientras lee el frame o
el cuerpo HTTP, antes de construir un objeto completo. El adaptador P2 usa
buffers fijos de 1016 bytes para el sobre, colas de dos tramas por par y
retroceso explícito con contador de descartes; no reserva 16 KiB por cliente.
El heap pico, la latencia y el comportamiento bajo carga real siguen sin medir;
la comparación de tamaños estáticos está en `docs/validation/p2-software.md`.

### Comandos

```bash
python3 -m venv .venv
. .venv/bin/activate
pip install -r packages/protocol/requirements.txt

python3 packages/protocol/validate_protocol.py packages/protocol/examples/valid
python3 packages/protocol/validate_protocol.py --expect-invalid packages/protocol/examples/invalid
python3 -m unittest discover -s packages/protocol/tests -v
```

El workflow `.github/workflows/protocol-validation.yml` ejecuta los mismos
controles en GitHub Actions y también se dispara ante cambios en firmware que
puedan afectar la compatibilidad.

## 6. Versionado

- **MAJOR**: cambio incompatible del sobre o de un payload existente.
- **MINOR**: nuevos tipos de mensaje o campos opcionales, con esquema publicado.
- **PATCH**: corrección de documentación, ejemplos o descripción sin cambiar la
  forma validada.

Reglas de interoperabilidad:

1. Cada mensaje declara exactamente la versión que respeta su productor.
2. El validador canónico acepta solo la versión que declara soportar.
3. `connection.hello.supportedVersion` debe coincidir con `protocolVersion`.
4. Un consumidor que no soporta la versión debe responder
   `UNSUPPORTED_PROTOCOL_VERSION`.
5. No se aceptan campos extra «por tolerancia» en validación canónica.
6. Toda versión nueva publica su propio conjunto de esquemas versionado.

Esto evita interpretaciones ambiguas entre HTTP, WebSocket y registro histórico.

## 7. Manejo de errores

| Código | Significado | Reintentable |
|---|---|---|
| `INVALID_ENVELOPE` | Sobre o payload no válido | No |
| `UNSUPPORTED_PROTOCOL_VERSION` | Versión no soportada por el consumidor | No |
| `UNKNOWN_MESSAGE_TYPE` | Tipo no registrado para esa versión | No |
| `PAYLOAD_TOO_LARGE` | Sobre por encima de 16 KiB | No |
| `RATE_LIMITED` | Límite de frecuencia excedido | Sí |
| `UNAUTHORIZED` | Dispositivo o par no autorizado | No |
| `INTERNAL_ERROR` | Fallo interno del receptor | Sí |

Cuando el error se asocia a un mensaje recibido, incluye `relatedSource` y
`relatedSequence` para identificarlo sin exponer credenciales ni datos privados.
Si no existe un mensaje relacionado, ambos campos se omiten. Los mensajes del
validador no devuelven el payload completo.

## 8. Compatibilidad con firmware v0.3.1

La API HTTP v0.3.1 permanece intacta. La siguiente tabla describe la
correspondencia implementada por el adaptador WebSocket del entorno
`sentinel_esp32s3_n16r8_realtime`. El entorno `_rf` sigue siendo la imagen de
producción y no emite estos mensajes.

| Superficie v0.3.1 | Mensaje v1 correspondiente | Garantía en P2 |
|---|---|---|
| `POST /api/motion/sensorlogger` | `motion.sample` | Mismo emparejamiento aceleración + gravedad y rechazo de simulación |
| `GET /api/motion/status` | `motion.status` | Mismos conceptos; la fuente simulada se excluye de v1 |
| `GET /api/state` | `system.status` | Estado operativo y capacidades sin cambiar la ruta |
| `GET /api/rf` | `rf.telemetry` | `clientId` por sesión, RSSI crudo y filtrado, `null` si no hay lectura |
| `GET /api/events` | `event` | Un sobre por evento del anillo acotado; nombres alineados con `EventEngine` |
| `GET /motion/native` | Sin cambio | La autorización de `deviceId` sigue en la UI actual |
| `GET /api/export.csv` | Sin cambio | La exportación CSV no se sustituye |
| `POST /api/experiments`, `/api/measurements`, `/api/calibration` | Fuera de v1 | Operaciones del laboratorio, no telemetría |

`clientId` es secuencial por arranque y asociación, y `bootId` distingue
reinicios. No es una dirección MAC ni una identidad permanente. El protocolo no
admite `mac` en RF ni en eventos; el firmware v0.3.1 ya evita publicarla.

Garantías concretas de P2:

- No se renombró, movió ni eliminó ningún archivo de `src/` o `include/`.
- `platformio.ini` añade un quinto entorno; los cuatro originales conservan sus
  filtros y todos compilan.
- No se flasheó hardware.
- Los cambios en fuentes compartidas están dentro de `#ifdef SENTINEL_REALTIME`.
- La compatibilidad está verificada en host y por compilación; la compatibilidad
  en ejecución sobre la placa sigue pendiente.
- Los detalles y tamaños se registran en `docs/validation/p2-software.md`.

## 9. Transporte

- **HTTP**: un sobre por petición o respuesta; el código HTTP transporta el
  resultado, el payload `error` transporta el detalle.
- **WebSocket**: un sobre por frame de texto en `ws://<AP-IP>:81/realtime`;
  `hello` y `heartbeat` son mensajes de control, no telemetría. El endpoint está
  acotado a la red local y se documenta en `docs/realtime-core.md`.
- **Sin transporte**: el mismo JSON es válido como evidencia archivada.

El contrato no asume TLS, autenticación ni topología. Esas decisiones pertenecen
al despliegue: HTTP local solo en la red de laboratorio; gateway remoto con
transporte protegido.

## 10. Riesgos pendientes

- Todavía no existe un adaptador que produzca estos mensajes desde el firmware.
- La estimación de distancia depende de calibración física real.
- El seguimiento de monotonía de `sequence` requiere estado en cada sesión.
- El historial RAM-only y la exportación CSV no se sustituyen en P1.
- La latencia, el pico de heap y el tamaño máximo de cola en el ESP32 son
  mediciones obligatorias de P2.
