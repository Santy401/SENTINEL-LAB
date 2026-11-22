# Matriz de pruebas físicas P2 · SENTINEL Core v0.4.0

## Leyenda y regla de estado

| Estado | Significado |
|---|---|
| `PASS` | Existe evidencia real archivada y cumple el criterio. Ninguna prueba física P2 está en este estado. |
| `FAIL` | Existe evidencia real y contradice el criterio. |
| `BLOCKED` | Requiere autorización de flasheo, token o intervención física aún no concedida. |
| `NOT TESTED` | No existe captura real de la prueba. |

**Estado global actual:** `P2 READY FOR AUTHORIZED HARDWARE FLASH — NOT YET FLASHED`.

No se han ejecutado pruebas sobre el ESP32-S3 desde este flujo. Las filas
marcadas `PASS` en la sección de preparación son verificaciones de software o
artefactos, no mediciones de hardware.

## 0. Preparación verificada en Ubuntu

| ID | Comprobación | Criterio | Resultado | Evidencia |
|---|---|---|---|---|
| P-01 | Git y PR | Rama P2 sincronizada; PR #6 abierto y checks verdes | `PASS` | `git status`, `gh pr checks 6` |
| P-02 | Respaldo v0.3.1 | Cuatro SHA-256 verificados; procedencia documentada | `PASS` | `docs/hardware/v031-backup-inventory.md` |
| P-03 | Compilación v0.4.0 | `pio run -e ..._realtime` = SUCCESS | `PASS` | `docs/validation/p2-build-record.md` |
| P-04 | Pruebas host | `tools/check_software.py` y contrato v1 OK | `PASS` | `docs/validation/p2-software.md` |
| P-05 | Kit portable | Manifiesto y scripts generados sin credenciales | `PASS` | `KIT_MANIFEST.md`, `SHA256SUMS` |
| P-06 | Offsets y particiones | Tabla decodificada; NVS fuera de las cuatro regiones | `PASS` | `docs/hardware/p2-flash-layout.md`, `p2_layout_check.py` |
| P-07 | Compatibilidad esptool | Detección y versión validadas con esptool v5.6.0 | `PASS` | `tools/p2_esptool.sh`, `docs/hardware/p2-flash-layout.md` |
| P-08 | Respaldo completo original | 16 777 216 B y SHA-256 verificados; imagen no publicada | `PASS` | `docs/hardware/p2-full-recovery.md` |
| P-09 | Recuperación real | Rollback ejecutado sobre el dispositivo | `BLOCKED` | Autorización pendiente |

## 1. Arranque

| ID | Prueba | Procedimiento | Criterio de PASS | Estado | Evidencia prevista |
|---|---|---|---|---|---|
| A-01 | Identificación de versión | Tras carga autorizada de `_realtime`, consultar `/api/state` y `/api/realtime/status` | `fw=0.3.1`, `realtimeVersion` presente, sin `404` en la ruta de salud | `BLOCKED` | `evidence/results/boot.json` |
| A-02 | Inicio estable | Observar AP y monitor serie durante 120 s | AP visible, servicio HTTP activo, sin reinicios | `NOT TESTED` | `evidence/serial/boot.log`, `evidence/http/boot.json` |
| A-03 | Reinicios inesperados | Comparar `bootId` y `uptimeMs` en dos lecturas | `bootId` constante y `uptimeMs` monótono | `NOT TESTED` | `evidence/results/stability.json` |
| A-04 | Estado de servicios | GET a rutas obligatorias tras arranque | Rutas v0.3.1 responden 200 | `NOT TESTED` | `evidence/http/routes.json` |

## 2. Compatibilidad HTTP

| ID | Endpoint | Procedimiento | Criterio de PASS | Estado | Evidencia prevista |
|---|---|---|---|---|---|
| H-01 | `/api/state` | GET sin credenciales | 200, JSON, `fw` coherente | `NOT TESTED` | `evidence/http/api-state.json` |
| H-02 | `/api/rf` | GET sin credenciales | 200, JSON con `clients`, `ssid`, `ip` | `NOT TESTED` | `evidence/http/api-rf.json` |
| H-03 | `/api/events` | GET sin credenciales | 200, JSON de eventos | `NOT TESTED` | `evidence/http/api-events.json` |
| H-04 | `/api/lab` | GET sin credenciales | 200, JSON de laboratorio | `NOT TESTED` | `evidence/http/api-lab.json` |
| H-05 | `/api/export.csv` | GET sin credenciales | 200, cabecera CSV válida | `NOT TESTED` | `evidence/http/export.csv` |
| H-06 | `/api/motion/status` | GET sin credenciales durante y después de recording | 200, JSON con estado de movimiento real | `NOT TESTED` | `evidence/http/motion-status.json` |
| H-07 | `/api/motion/sensorlogger` | Verificar si el alias existe en el build; si no, documentar `404` | 200 con JSON si existe; `404` documentado si no es ruta del firmware | `NOT TESTED` | `evidence/http/motion-sensorlogger.json` |
| H-08 | `/api/realtime/status` | GET sin token en build `_realtime` | 200 con los 13 campos y tipos correctos | `BLOCKED` | `evidence/http/realtime-status.json` |

La ruta `/api/realtime/status` debe quedar ausente (`404`) tras rollback a
`_rf`; ese `404` no es un fallo.

## 3. WebSocket

| ID | Prueba | Procedimiento | Criterio de PASS | Estado | Evidencia prevista |
|---|---|---|---|---|---|
| W-01 | Conexión | `p2_ws_probe.py` a `ws://192.168.4.1:81/realtime` | Handshake 101 | `BLOCKED` | `evidence/ws/handshake.json` |
| W-02 | Subprotocolo | Verificar cabecera de respuesta | `Sec-WebSocket-Protocol: sentinel.v1` | `BLOCKED` | `evidence/ws/handshake.json` |
| W-03 | Handshake estricto | Verificar `Accept`, versión 13 y upgrade | Todas las comprobaciones verdaderas | `BLOCKED` | `evidence/ws/handshake.json` |
| W-04 | Recepción | Observar frames durante fase `associated` | Al menos un sobre v1 válido tras reloj válido | `BLOCKED` | `evidence/ws/frames.jsonl` |
| W-05 | Envelope v1 | Validar cada sobre con el validador canónico | 100 % válidos | `BLOCKED` | `evidence/ws/validation.json` |
| W-06 | Secuencia y sesión | Comparar `source`, `sessionId` y `sequence` | Monotonía por sesión; sin reutilización | `BLOCKED` | `evidence/ws/frames.jsonl` |
| W-07 | Desconexión/reconexión | Fases capturadas por `hw_capture.py` | AP y WS disponibles tras reconectar; `bootId` estable | `BLOCKED` | `evidence/ws/reconnect.json` |
| W-08 | Límite de clientes | Cuarto y quinto cliente concurrentes | Cuatro aceptados; exceso rechazado sin reinicio | `NOT TESTED` | `evidence/ws/limits.json` |
| W-09 | Tamaño de mensaje | Enviar trama mayor a 1024 B desde cliente de prueba | Rechazo sin reinicio; contador de descartes | `NOT TESTED` | `evidence/ws/limits.json` |
| W-10 | Entradas inválidas | Versión distinta, subprotocolo ausente, clave inválida | `426`/`400` según caso; servicio sigue activo | `NOT TESTED` | `evidence/ws/invalid.json` |

El kit original no envía un saludo `gateway` por defecto. La sincronización de
reloj requiere autorización explícita y token seguro.

## 4. Tiempo

| ID | Prueba | Procedimiento | Criterio de PASS | Estado | Evidencia prevista |
|---|---|---|---|---|---|
| T-01 | Estado inicial | Handshake sin reloj válido | `clockSynced=false` y sin sobres v1 falsos | `BLOCKED` | `evidence/ws/no-clock.json` |
| T-02 | Sincronización | Saludo `gateway` autorizado en fase `associated` | `clockSynced=true` y timestamps plausibles | `BLOCKED` | `evidence/ws/clock-sync.json` |
| T-03 | Validez de timestamps | Comparar `timestamp` con marca del host | Desviación dentro del umbral acordado | `BLOCKED` | `evidence/ws/clock-sync.json` |
| T-04 | Pérdida de sincronización | Reiniciar AP o gateway con reloj inválido | No se inventan fechas; comportamiento documentado | `NOT TESTED` | `evidence/ws/clock-loss.json` |

## 5. Telemetría

| ID | Prueba | Procedimiento | Criterio de PASS | Estado | Evidencia prevista |
|---|---|---|---|---|---|
| D-01 | RSSI | Comparar `/api/rf` con `rf.telemetry` | Cliente asociado con RSSI entero en `[-100,-1]` | `NOT TESTED` | `evidence/results/rssi.json` |
| D-02 | Eventos | Desconectar y asociar cliente real | `DEVICE_CONNECTED` y `DEVICE_DISCONNECTED` en HTTP y v1 | `BLOCKED` | `evidence/results/events.json` |
| D-03 | Datos del iPhone | Sensor Logger real en fase `associated` | `motion.status` con fuente real y muestras crecientes | `NOT TESTED` | `evidence/results/motion.json` |
| D-04 | Frecuencia | Medir cadencia de telemetría durante 120 s | 1 Hz nominal para RF; sin avalanchas | `NOT TESTED` | `evidence/results/cadence.json` |
| D-05 | Integridad | Validar todos los frames v1 capturados | Sin duplicados ni secuencias retrógradas | `BLOCKED` | `evidence/ws/validation.json` |
| D-06 | Pérdida de datos | Concurrencia HTTP/WS y contadores de salud | `droppedFrames`, `rejectedMessages` y `droppedTelemetry` sin incrementos injustificados | `NOT TESTED` | `evidence/heap/status-samples.json` |

## 6. Estabilidad

| ID | Prueba | Procedimiento | Criterio de PASS | Estado | Evidencia prevista |
|---|---|---|---|---|---|
| S-01 | Heap libre real | Muestrear `/api/realtime/status` durante `soak` | `freeHeapBytes > 0` en todas las muestras | `BLOCKED` | `evidence/heap/status-samples.json` |
| S-02 | Heap mínimo | Comparar `minFreeHeapBytes` contra muestras | Monótonamente no mayor que el mínimo observado | `BLOCKED` | `evidence/heap/status-samples.json` |
| S-03 | Uptime | Dos o más muestras separadas en el tiempo | `uptimeMs` monótono y sin reinicio | `BLOCKED` | `evidence/heap/status-samples.json` |
| S-04 | Reconexión Wi-Fi | Fases `disconnected`/`reconnected` | Servicios restaurados tras reasociación | `BLOCKED` | `evidence/ws/reconnect.json` |
| S-05 | Concurrencia | HTTP + WS + cliente móvil simultáneos | Sin `FAIL`, sin watchdog, contadores estables | `NOT TESTED` | `evidence/results/concurrency.json` |
| S-06 | Reinicios inesperados | Revisar serie y `bootId` durante `soak` | Sin cambios de `bootId` durante la sesión PASS | `NOT TESTED` | `evidence/serial/soak.log` |

## 7. Recuperación

| ID | Prueba | Procedimiento | Criterio de PASS | Estado | Evidencia prevista |
|---|---|---|---|---|---|
| R-01 | Disponibilidad del respaldo | Verificar presencia del directorio | Cuatro binarios y manifiestos presentes | `PASS` | `docs/hardware/v031-backup-inventory.md` |
| R-02 | Integridad del respaldo | `sha256sum -c SHA256SUMS` | 4/4 coincidencias | `PASS` | `KIT_MANIFEST.md` |
| R-03 | Compatibilidad de artefactos | Comparar offsets y particiones | `0x0`, `0x8000`, `0xe000`, `0x10000` documentados | `PASS` | `docs/hardware/v031-backup-inventory.md` |
| R-04 | Procedimiento documentado | Revisar guía de recuperación | Comandos, advertencias y criterios presentes | `PASS` | `docs/hardware/recovery.md` |
| R-05 | Recuperación real | Rollback autorizado a `_rf` | AP y rutas v0.3.1 operativas; evidencia archivada | `BLOCKED` | `evidence/results/rollback.json` |
| R-06 | Preservación de NVS | Confirmar que no se ejecutó `erase-flash` | Log sin borrado; configuración conservada | `BLOCKED` | `evidence/checksums/flash-command.log` |
| R-07 | Recuperación completa real | Restauración autorizada desde imagen de 16 MiB | Imagen íntegra, NVS reemplazado y evidencia archivada | `BLOCKED` | `evidence/results/full-restore.json` |

## Cierre

El informador `tools/hw_report.py` solo puede marcar `PASS` global cuando las
nueve puertas definidas estén respaldadas por evidencia real íntegra. Hasta que
eso ocurra, cualquier reporte sin capturas devuelve `FAIL`/`NOT_VERIFIED` y el
estado del proyecto permanece `HARDWARE VALIDATION PENDING`.
