# Validación física P2 · SENTINEL Core v0.4.0

## Estado real

El kit de evidencia (`tools/hw_common.py`, `tools/hw_capture.py` y
`tools/hw_report.py`) está **implementado y probado en host** (sintaxis y flujo
del informador). **No se ha flasheado la imagen `_realtime`, no se han capturado
mediciones de la placa y no existe un informe P2 en PASS.** El hardware queda
**sin validar** hasta que un `INFORME.md` generado por `hw_report.py` sobre
evidencia real devuelva `PASS` en las nueve puertas.

La imagen funcional `sentinel_esp32s3_n16r8_rf` (v0.3.1) sigue siendo la de
producción. La API HTTP v0.3.1 no se modifica.

> **ADVERTENCIA — NO flashear sin autorización explícita del propietario.**  
> **ADVERTENCIA — NO borrar NVS ni usar `erase_flash`/`--erase-all` como paso
> habitual.**  
> **ADVERTENCIA — HTTP y WebSocket solo en la red de laboratorio; no exponer
> estos endpoints a internet.**  
> **ADVERTENCIA — RSSI no determina una distancia exacta; no se usa para
> calibración ni localización sin medición física.**  
> **ADVERTENCIA — los sobres Protocol v1 requieren un reloj UTC válido; sin
> reloj no hay telemetría v1 aceptable.**

## 1. Requisitos previos y preflight

En el computador de laboratorio, dentro del repositorio:

```bash
cd ~/Github/sentinel-firmware
git status --short
git branch --show-current
python3 --version
python3 -m py_compile tools/hw_common.py tools/hw_capture.py tools/hw_report.py
python3 scripts/check-repository.py
python3 tools/hw_report.py --help
python3 tools/check_software.py
python3 tools/p2_preflight.py --root .
python3 tools/p2_serial_check.py --port /dev/ttyACM0
python3 tools/p2_http_check.py --host 192.168.4.1
python3 tools/p2_ws_probe.py --host 192.168.4.1 --seconds 8
```

Los cuatro scripts añadidos son de solo lectura. El kit portable para Arch, su
procedimiento de transferencia y el script de escritura con confirmación
literal están descritos en [el kit P2](../hardware/p2-kit.md). La matriz
completa de estados físicos está en
[la matriz P2](p2-physical-matrix.md) y el acta de compilación en
[el registro P2](p2-build-record.md).

El validador canónico usa las dependencias ya presentes en el repositorio. Para
que `protocol.all_envelopes_valid` pueda evaluarse, créese un entorno aislado:

```bash
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install -r packages/protocol/requirements.txt
python3 packages/protocol/validate_protocol.py packages/protocol/examples/valid
python3 packages/protocol/validate_protocol.py --expect-invalid packages/protocol/examples/invalid
```

`jsonschema` y `rfc3339-validator` solo se usan en la herramienta de validación;
el firmware no necesita dependencias nuevas. `.venv*/` está ignorado por Git.

Comprobaciones de red **de solo lectura** (no cambian la red del host):

```bash
nmcli -t -f IN-USE,SSID,BSSID,CHAN,SIGNAL,MODE device wifi list --rescan no
nmcli -t -f DEVICE,TYPE,STATE,CONNECTION device status
```

Se espera ver `SENTINEL-LAB` en la lista pasiva. Si `nmcli` no está disponible,
la puerta `wifi.ap_visibility` queda `NOT_VERIFIED`; no se inventa el SSID.

El token de gateway **solo** se lee de la variable de entorno
`SENTINEL_HW_BEARER`. Para no dejarlo en el historial:

```bash
read -rs -p 'Bearer token del gateway: ' SENTINEL_HW_BEARER; export SENTINEL_HW_BEARER; echo
```

Si no se va a sincronizar el reloj, no defina la variable. El kit registra
únicamente `token_present: true|false`; nunca imprime ni guarda el valor ni la
cabecera `Authorization`. Al terminar:

```bash
unset SENTINEL_HW_BEARER
```

## 2. Superficie observada

Cada captura consulta, con GET y sin credenciales:

- `/`
- `/api/state`
- `/api/rf`
- `/api/lab`
- `/api/events`
- `/api/export.csv`
- `/motion/native`
- `/motion/status` (ruta exigida por el contrato de captura; en el firmware
  actual suele responder 404)
- `/api/motion/status` (alias real del firmware; una de las dos rutas debe
  responder 200 con JSON y `fw` para aprobar la puerta HTTP)
- `/api/realtime/status` (solo existe en el build `_realtime`; si responde 404
  se registra `present: false` y no se cuenta como evidencia de ejecución)

Además abre un WebSocket mínimo RFC 6455 en `ws://192.168.4.1:81/realtime` con:

```text
Sec-WebSocket-Version: 13
Sec-WebSocket-Protocol: sentinel.v1
```

La respuesta debe ser `101 Switching Protocols`, con `Sec-WebSocket-Accept`
válido y `Sec-WebSocket-Protocol: sentinel.v1`. El firmware valida la ruta
exacta, los tokens `Upgrade`/`Connection`, la clave base64 canónica, la versión
13 y el token `sentinel.v1`; una versión distinta recibe `426 Upgrade Required`.
No se negocian subprotocolos alternativos. El kit verifica esos valores; no
acepta un handshake solo por recibir un `101`.

### Sincronización de reloj

Solo en la fase designada `associated`, y solo si `SENTINEL_HW_BEARER` está
definido, el kit envía `Authorization: Bearer <token>` en el handshake y un
`connection.hello` con `payload.role = "gateway"`.

El campo exacto del sobre es **`timestamp`** (ISO 8601 UTC con `Z`), según
`packages/protocol/schemas` y `InboundValidator::validate()` +
`UtcClock::parse()`. **`epochMs` no existe como campo del sobre**: es la
variable interna que el firmware deriva de `timestamp` para anclar el reloj. El
registro de evidencia `clockSync` guarda `hostTimestamp` y `hostEpochMs` para
auditar esa correspondencia, sin inventar un campo nuevo en el protocolo. El sobre no
incluye `epochMs` porque el esquema tiene `additionalProperties: false`.

Sin este saludo autorizado (y sin NTP local válido), el firmware no publica
sobres v1. Eso es intencional: no se generan timestamps falsos.

## 3. Secuencia de captura

Use **un mismo directorio** para la secuencia completa. Cada invocación añade
registros al JSONL y reescribe el manifiesto con todos los hashes.

```bash
mkdir -p evidence/p2-20261121

python3 tools/hw_capture.py --dir evidence/p2-20261121 --host 192.168.4.1 \
  --phase pre --seconds 60

python3 tools/hw_capture.py --dir evidence/p2-20261121 --host 192.168.4.1 \
  --phase associated --seconds 120 --distance-m 1.0 \
  --note 'Cliente asociado a 1 m; distancia declarada por el operador'

python3 tools/hw_capture.py --dir evidence/p2-20261121 --host 192.168.4.1 \
  --phase disconnected --seconds 90 \
  --note 'Cliente desconectado manualmente del AP'

python3 tools/hw_capture.py --dir evidence/p2-20261121 --host 192.168.4.1 \
  --phase reconnected --seconds 120 --distance-m 1.0 \
  --note 'Cliente vuelto a asociar al AP'

python3 tools/hw_capture.py --dir evidence/p2-20261121 --host 192.168.4.1 \
  --phase soak --seconds 300 \
  --note 'Observación prolongada sin intervención'
```

Acciones del operador: antes de `pre` el cliente no debe estar asociado; entre
`associated` y `disconnected` desconecte el cliente; entre `disconnected` y
`reconnected` vuelva a asociarlo; durante `soak` no intervenga. Las marcas
`operatorAction` son **declaraciones del operador**, no pruebas por sí solas:
deben concordar con `/api/rf`, `/api/events` y los frames WebSocket.

`--seconds` debe ser mayor que 0 y como máximo 3600. `--distance-m` es una declaración
humana, no una medición automática. `--note` y `--action` son opcionales y se
limpian antes de persistirse; no incluyen credenciales.

Salida esperada de cada captura (sin valores numéricos fijos):

```text
Evidencia: evidence/p2-20261121/evidence.jsonl
Manifiesto: evidence/p2-20261121/manifest.json
Fase: <fase>; registros añadidos: <N>; token_present: true|false
Sincronización de reloj: intentada como gateway | no intentada (sin token)
```

## 4. Diseño de la evidencia

```text
evidence/p2-20261121/
├── evidence.jsonl   # una observación por línea, con capturedAt UTC y recordHash
├── manifest.json    # hashes de cada registro + SHA256 del JSONL + resumen de ejecuciones
├── report.json      # salida estructurada del informador
└── INFORME.md       # informe legible en español
```

Tipos de registro: `operatorAction`, `wifi`, `http`, `wsHandshake`, `wsFrame`,
`wsError`, `wsClose` y `clockSync`. Cada línea lleva `capturedAt` en UTC real y
un `recordHash` SHA256 calculado sobre el registró canónico. El manifiesto
contiene `perRecordSha256`, `evidenceSha256`, `recordCount`, `runs`,
`clockSyncPhase` y `token_present`. No contiene tokens ni cabeceras.

## 5. Informe

```bash
python3 tools/hw_report.py --dir evidence/p2-20261121 \
  --ssid SENTINEL-LAB --fw 0.3.1 --protocol 1.0.0 \
  --max-clock-skew-ms 5000
```

El comando escribe `report.json` e `INFORME.md`. Imprime el resultado global y
el estado de cada puerta. Sale con código `0` solo si el global es `PASS`; con
cualquier `FAIL` o `NOT_VERIFIED`, sale con código `1`.

**Ejemplo de forma, no de resultado ni de medición:**

```text
Resultado global: PASS|FAIL
- Visibilidad del AP Wi-Fi: PASS|FAIL|NOT_VERIFIED
- Rutas HTTP v0.3.1 de lectura: PASS|FAIL|NOT_VERIFIED
- Handshake WebSocket sentinel.v1: PASS|FAIL|NOT_VERIFIED
- Validez de todos los sobres v1: PASS|FAIL|NOT_VERIFIED
- Sincronización de reloj por gateway y plausibilidad: PASS|FAIL|NOT_VERIFIED
- RSSI de clientes asociados: PASS|FAIL|NOT_VERIFIED
- Eventos de conexión y desconexión: PASS|FAIL|NOT_VERIFIED
- Recuperación WebSocket y Wi-Fi: PASS|FAIL|NOT_VERIFIED
- Heap y estabilidad en ejecución: PASS|FAIL|NOT_VERIFIED
```

No se muestran aquí cifras de RSSI, heap ni uptime porque no hay medición real
archivada. Los valores mostrados por el informe proceden solo de la evidencia.

## 6. Puertas de aceptación

| Puerta | PASS exige |
|---|---|
| `wifi.ap_visibility` | `nmcli` disponible y SSID `--ssid` observado en modo pasivo |
| `http.v031_read_routes` | Las ocho rutas de lectura responden 200 con cuerpo y `fw` coherente; `/motion/status` admite el alias `/api/motion/status` |
| `ws.sentinel_v1_handshake` | 101, `Accept` válido, versión 13 y subprotocolo `sentinel.v1` aceptado |
| `protocol.all_envelopes_valid` | Validador canónico disponible y **todos** los sobres recibidos válidos para `--protocol` |
| `clock.gateway_sync_and_plausible` | Intento `gateway` en fase `associated`, `token_present=true`, `clockSynced=true`, saludo con marca UTC cercana y sobres plausibles |
| `rf.rssi_from_associated_clients` | `/api/rf` y `rf.telemetry` muestran cliente asociado con RSSI entero en `[-100, -1]` |
| `events.connect_disconnect` | `DEVICE_CONNECTED` y `DEVICE_DISCONNECTED` en HTTP y en v1, con `bootId` coherente |
| `recovery.ws_and_wifi` | Fases `pre`, `associated`, `disconnected`, `reconnected`, `soak`; AP y WS visibles tras reconectar; `bootId` sin cambio |
| `runtime.heap_and_stability` | `/api/realtime/status` con los 13 campos del contrato, `clockSynced=true`, heap positivo, `uptimeMs` monótono y contadores de pérdida en cero |

La ruta de salud, cuando existe, expone exactamente:

```text
fw, realtimeVersion, bootId, uptimeMs, clockSynced, clientCount,
freeHeapBytes, minFreeHeapBytes, heapSamples, droppedFrames,
rejectedMessages, droppedTelemetry, skippedSimulatedEvents
```

**Falta de evidencia = `NOT_VERIFIED` y global `FAIL`.** Un `FAIL` indica una
observación contradictoria o inválida. No se aprueba una puerta «porque el
código compila».

## 7. Reglas anti-fabricación

1. Nunca usar `build/firmware-*.json` ni `packages/protocol/examples/**` como
   evidencia: son fixtures de software y el informador los rechaza si aparecen
   en el directorio o dentro de un registro.
2. Nunca contar un payload con `simulated=true`, `simulation=true`,
   `sourceType: "Simulated"` o `source: "Simulated"` como medición de hardware;
   el registro se excluye y el informe queda en `FAIL`.
3. Nunca presentar compilación, prueba host o simulación como medición física.
4. Nunca rellenar valores ausentes con `0`, estimaciones ni valores de ejemplo.
5. Nunca guardar tokens, `Authorization`, contraseñas ni credenciales Wi-Fi en
   evidencia, informes, logs o mensajes de pantalla.
6. Nunca derivar distancia de RSSI sin medición física y calibración real.
7. Nunca afirmar que el reloj está sincronizado solo porque el WebSocket abrió;
   hace falta el saludo `gateway` autorizado y `clockSynced=true`.
8. Nunca cambiar el resultado del informe a mano: regenerelo desde la evidencia.

## 8. Flasheo y recuperación (solo con autorización)

**NO ejecutar los siguientes comandos sin autorización explícita.** Si el
propietario autoriza probar el entorno experimental:

```bash
cd ~/Github/sentinel-firmware
pio run -e sentinel_esp32s3_n16r8_realtime
esptool.py chip-id
esptool.py flash-id
newgrp uucp -c 'pio run -e sentinel_esp32s3_n16r8_realtime -t upload --upload-port /dev/ttyACM0'
pio device monitor -p /dev/ttyACM0 -b 115200
```

Si la placa no vuelve a presentar `SENTINEL-LAB` o el informe falla, la
recuperación autorizada es volver a la imagen estable:

```bash
newgrp uucp -c 'pio run -e sentinel_esp32s3_n16r8_rf -t upload --upload-port /dev/ttyACM0'
```

No añadir `--erase-all`, no usar `esptool.py erase_flash` y no borrar NVS: la
configuración y la calibración del laboratorio viven ahí. El procedimiento
completo de lecturas previas, cargas autorizadas y rollback está en
[recuperación de hardware](../hardware/recovery.md). Tras un rollback, la ruta
`/api/realtime/status` no existe: la puerta `runtime.heap_and_stability` debe
quedar `NOT_VERIFIED`, no inventarse como PASS.

## 9. Cierre del acta

Solo cuando el informe sobre evidencia real indique `PASS` en las nueve puertas
se podrá cambiar el estado de P2 a **hardware tested** en la documentación.
Hasta entonces el estado es **implemented / software tested, hardware untested**.
No se propone fusión, despliegue ni P3 sin autorización explícita.
