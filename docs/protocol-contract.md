# Contrato de protocolo SENTINEL v1.0.0

## 1. Alcance y estado

Este documento define el contrato compartido de mensajería de SENTINEL-LAB.
La versión actual es **1.0.0**. El contrato es **transport-independent**: el
mismo sobre JSON puede viajar en una respuesta HTTP, en un frame WebSocket o en
un archivo de evidencia.

Estado real:

- **Implemented**: esquemas, validador, ejemplos y pruebas automáticas.
- **Planned**: adaptadores en firmware, web, gateway e iOS.
- Este trabajo **no modifica** el firmware v0.3.1 ni sus endpoints HTTP.

## 2. Principios

- Toda medición publicada es real; los mensajes de medición exigen
  `simulated: false`.
- El sobre es estricto: no se aceptan campos desconocidos.
- Los esquemas son la fuente de verdad de la forma de cada mensaje.
- La versión del protocolo se declara en cada mensaje.
- Los errores son legibles por máquina y no contienen secretos.
- RSSI no determina una distancia exacta; `estimatedDistance` es una estimación
  condicionada a calibración física.

## 3. Sobre compartido

Todo mensaje contiene exactamente estos campos:

| Campo | Tipo | Regla |
|---|---|---|
| `protocolVersion` | string | SemVer `MAJOR.MINOR.PATCH`; en v1 debe ser `1.0.0` |
| `type` | string | Tipo de mensaje del registro cerrado |
| `timestamp` | string | ISO 8601 UTC con `Z`; milisegundos opcionales |
| `sequence` | integer | Monótono por `source`, de `0` a `2^53 - 1` |
| `source` | string | Identificador del emisor, 1–64 caracteres |
| `payload` | object | Contenido validado según `type` |

El par `source` + `sequence` permite deduplicación y detección de pérdida de
mensajes sin depender del transporte.

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
firmware.

## 5. Validación

Los esquemas usan JSON Schema Draft 2020-12. La validación canónica incluye:

- `additionalProperties: false` en sobre y payloads.
- `required` explícito por tipo de mensaje.
- Enums cerrados para tipos, estados, roles y códigos de error.
- Rangos numéricos y límites de longitud.
- Timestamps UTC con forma y fecha válidas.
- Rechazo de `NaN`, `Infinity` y claves duplicadas antes de JSON Schema.
- Límite de tamaño del sobre serialized.

### Límites principales

| Elemento | Límite |
|---|---|
| Sobre serializado | 16 KiB |
| `sequence` | `0` a `9007199254740991` |
| `deviceId`, `bootId`, `source`, `eventId` | 1–64 caracteres |
| Clientes RF por mensaje | 8 |
| Capacidades por saludo | 16 |
| Vector de movimiento | `-500` a `500` m/s² por eje |
| RSSI | `-100` a `0` dBm |
| Canal Wi-Fi | `1` a `14` |
| Mensaje de error | 1–256 caracteres |

La dependencia `jsonschema` existe solo en la herramienta de validación. El
firmware no la utiliza y no se le exige ninguna biblioteca nueva.

### Comandos

```bash
python3 -m venv .venv
. .venv/bin/activate
pip install -r packages/protocol/requirements.txt

python3 packages/protocol/validate_protocol.py packages/protocol/examples/valid
python3 -m unittest discover -s packages/protocol/tests -v
```

El workflow `.github/workflows/protocol-validation.yml` ejecuta los mismos
controles en GitHub Actions.

## 6. Versionado

- **MAJOR**: cambio incompatible del sobre o de un payload existente.
- **MINOR**: nuevos tipos de mensaje o campos opcionales, con esquema publicado.
- **PATCH**: corrección de documentación, ejemplos o descripción sin cambiar la
  forma validada.

Reglas de interoperabilidad:

1. Cada mensaje declara exactamente la versión que respeta su productor.
2. El validador canónico acepta solo la versión que declara soportar.
3. Un consumidor que no soporta la versión debe responder
   `UNSUPPORTED_PROTOCOL_VERSION`.
4. No se aceptan campos extra «por tolerancia» en validación canónica.
5. Toda versión nueva publica su propio conjunto de esquemas versionado.

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

El campo `relatedSequence` permite asociar el error con un mensaje recibido sin
incluir credenciales ni datos privados.

## 8. Compatibilidad con firmware v0.3.1

La API HTTP v0.3.1 permanece intacta. La siguiente tabla describe la
correspondencia conceptual que usarán los adaptadores futuros; **no implica que
el firmware ya emita estos mensajes**.

| Superficie v0.3.1 | Mensaje v1 correspondiente | Garantía en P1 |
|---|---|---|
| `POST /api/motion/sensorlogger` | `motion.sample` | Mismo emparejamiento aceleración + gravedad y rechazo de simulación |
| `GET /api/motion/status` | `motion.status` | Mismos conceptos: `deviceId`, `acceptedSamples`, `signalLost` |
| `GET /api/state` | `system.status` | Estado operativo y capacidades sin cambiar la ruta |
| `GET /api/rf` | `rf.telemetry` | RSSI crudo y filtrado diferenciados; distancia opcional |
| `GET /api/events` | `event` | Un sobre por evento del anillo acotado |
| `GET /motion/native` | Sin cambio | La autorización de `deviceId` sigue en la UI actual |
| `GET /api/export.csv` | Sin cambio | La exportación CSV no se sustituye |
| `POST /api/experiments`, `/api/measurements`, `/api/calibration` | Fuera de v1 | Operaciones del laboratorio, no telemetría |

Garantías concretas de esta etapa:

- No se renombró, movió ni eliminó ningún archivo de `src/` o `include/`.
- No se cambió `platformio.ini`.
- No se flasheó hardware.
- Los cuatro entornos PlatformIO siguen compilando.
- La compatibilidad está documentada y es estructural; la compatibilidad en
  ejecución se verificará en P2 con adaptadores y pruebas.

## 9. Transporte

- **HTTP**: un sobre por petición o respuesta; el código HTTP transporta el
  resultado, el payload `error` transporta el detalle.
- **WebSocket**: un sobre por frame de texto; `hello` y `heartbeat` son
  mensajes de control, no telemetría.
- **Sin transporte**: el mismo JSON es válido como evidencia archivada.

El contrato no asume TLS, autenticación ni topología. Esas decisiones pertenecen
al despliegue: HTTP local solo en la red de laboratorio; gateway remoto con
transporte protegido.

## 10. Riesgos pendientes

- Todavía no existe un adaptador que produzca estos mensajes desde el firmware.
- La estimación de distancia depende de calibración física real.
- `COVERAGE_LIMIT_REACHED` sigue reservado y no forma parte del enum de v1.
- El historial RAM-only y la exportación CSV no se sustituyen en P1.
- La latencia y el pico de heap con la nueva capa no están medidos.
