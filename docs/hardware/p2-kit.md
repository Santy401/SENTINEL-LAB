# Kit portable P2 para Arch Linux

Este documento acompaña al kit generado por `tools/p2_make_kit.py`. El kit se
prepara en Ubuntu y se transfiere al PC principal con Arch Linux, donde está el
ESP32-S3 N16R8 conectado a `/dev/ttyACM0`.

**Estado del kit:** preparación de validación. No incluye evidencia falsa ni
resultados de hardware. No ejecuta flasheo automáticamente.

> **NO flashear sin autorización explícita.**  
> **NO borrar flash ni NVS.**  
> **NO fusionar PR #6 ni iniciar P3 desde este kit.**

## 1. Contenido

```text
p2-hardware-kit-YYYYMMDDTHHMMSSZ/
├── README.md                    # copia de este documento
├── KIT_MANIFEST.md              # procedencia y hashes de alto nivel
├── SHA256SUMS                   # integridad de todo el kit
├── build-record.json            # commit, entorno, tamaños y hashes del build
├── firmware/
│   ├── v0.3.1/                  # respaldo verificado + SHA256SUMS + MANIFEST
│   └── v0.4.0/                  # imagen P2 + SHA256SUMS
├── tools/
│   ├── p2_preflight.py          # solo lectura
│   ├── p2_serial_check.py       # solo lectura
│   ├── p2_http_check.py         # solo lectura
│   ├── p2_ws_probe.py           # solo lectura, sin gateway hello
│   ├── p2_flash_authorized.sh   # ESCRITURA, confirmación manual exacta
│   ├── hw_common.py
│   ├── hw_capture.py
│   └── hw_report.py
├── docs/                        # procedimientos, matriz, actas y recuperación
├── build/                       # log de compilación v0.4.0 sin identificadores privados
└── evidence/                    # directorios vacíos para evidencia real
```

Los scripts de preflight, puerto serie, HTTP y WebSocket **nunca** escriben en
el dispositivo. El único script de escritura es `p2_flash_authorized.sh`; exige
una confirmación literal y no acepta opciones de borrado.

## 2. Transferencia Ubuntu → Arch

Desde Ubuntu, sustituyendo la ruta por la generada:

```bash
KIT=~/Github/sentinel-firmware/dist/p2-hardware-kit-YYYYMMDDTHHMMSSZ
rsync -a --human-readable --progress "$KIT/" usuario@arch-lab:~/p2-hardware-kit/
```

En Arch:

```bash
cd ~/p2-hardware-kit
sha256sum -c SHA256SUMS
cat KIT_MANIFEST.md
python3 tools/p2_preflight.py --root .
python3 tools/p2_serial_check.py --port /dev/ttyACM0
```

`p2_preflight.py` verifica el manifiesto, la presencia de artefactos, la
ausencia de secretos, herramientas disponibles, permisos del puerto y, solo si
está disponible, la visibilidad pasiva del AP y `/api/state`. Los avisos de red
no autorizan por sí solos una prueba; los fallos de integridad sí detienen el
procedimiento.

No se instalan dependencias globales. Si falta `jsonschema` para el validador
canónico, créese un venv local dentro del kit y use
`packages/protocol/requirements.txt` desde el repositorio; no modifique el
sistema.

## 3. Comprobaciones de solo lectura

Con el AP `SENTINEL-LAB` activo:

```bash
python3 tools/p2_http_check.py --host 192.168.4.1 \
  --save-json evidence/http/pre-v031.json

python3 tools/p2_ws_probe.py --host 192.168.4.1 --seconds 8 \
  --save-json evidence/ws/pre-probe.json
```

`p2_ws_probe.py` no envía `connection.hello` y, por defecto, no usa token. Sin
reloj UTC válido es normal observar cero frames v1; eso no es un fallo del
handshake.

Para capturar la secuencia completa use los scripts originales:

```bash
python3 tools/hw_capture.py --dir evidence/run --host 192.168.4.1 \
  --phase pre --seconds 60
python3 tools/hw_capture.py --dir evidence/run --host 192.168.4.1 \
  --phase associated --seconds 120 --distance-m 1.0
python3 tools/hw_capture.py --dir evidence/run --host 192.168.4.1 \
  --phase disconnected --seconds 90
python3 tools/hw_capture.py --dir evidence/run --host 192.168.4.1 \
  --phase reconnected --seconds 120 --distance-m 1.0
python3 tools/hw_capture.py --dir evidence/run --host 192.168.4.1 \
  --phase soak --seconds 300
python3 tools/hw_report.py --dir evidence/run --ssid SENTINEL-LAB \
  --fw 0.3.1 --protocol 1.0.0 --max-clock-skew-ms 5000
```

El informe solo puede dar `PASS` con evidencia real. No edite `INFORME.md` a
mano.

## 4. Carga autorizada de P2

**Solo con autorización explícita y después de guardar la evidencia previa.**
El comando escribe cuatro regiones concretas y no borra NVS:

```bash
tools/p2_flash_authorized.sh \
  --environment sentinel_esp32s3_n16r8_realtime \
  --port /dev/ttyACM0 \
  --confirm 'FLASH sentinel_esp32s3_n16r8_realtime /dev/ttyACM0'
```

El confirmador debe coincidir literalmente con entorno y puerto. Si pertenece a
`uucp` o `dialout` en una sesión distinta, use esa sesión; el script no cambia
grupos ni permisos del sistema.

Tras la carga, repetir solo lectura:

```bash
python3 tools/p2_http_check.py --host 192.168.4.1 \
  --save-json evidence/http/post-flash.json
python3 tools/p2_ws_probe.py --host 192.168.4.1 --seconds 10 \
  --save-json evidence/ws/post-flash.json
```

## 5. Recuperación autorizada

Si el AP no reaparece, falla una puerta crítica o el operador decide detener la
prueba:

```bash
tools/p2_flash_authorized.sh \
  --environment sentinel_esp32s3_n16r8_rf \
  --port /dev/ttyACM0 \
  --confirm 'FLASH sentinel_esp32s3_n16r8_rf /dev/ttyACM0'
```

Después verificar `/api/rf` y `/api/state`. `/api/realtime/status` debe responder
`404` en `_rf`; eso confirma que se volvió a la imagen estable, no un fallo.
No use `esptool.py erase_flash`, `--erase-all` ni ninguna opción que borre NVS.

El respaldo no conserva NVS, calibraciones ni una imagen completa de flash; la
recuperación es funcional, no una restauración bit a bit del dispositivo.
Véase `docs/hardware/v031-backup-inventory.md` y `docs/hardware/recovery.md`.

## 6. Estructura de evidencia

Cada archivo debe identificar fecha UTC, entorno, commit/firmware, procedimiento
y resultado. Use nombres con fecha y fase, por ejemplo:

```text
evidence/serial/20261121T210000Z-post-flash.log
evidence/http/20261121T210100Z-routes.json
evidence/ws/20261121T210200Z-frames.jsonl
evidence/heap/20261121T210300Z-status-samples.json
evidence/results/20261121T210400Z-matrix.md
evidence/checksums/20261121T210500Z-SHA256SUMS
evidence/final/20261121T210600Z-INFORME.md
```

No guarden tokens, `Authorization`, contraseñas, claves privadas ni números de
serie. `hw_common.py` ya redacta cabeceras sensibles y excluye fixtures y
payloads simulados.

## 7. Criterio de cierre

La validación física solo se acepta cuando `hw_report.py` genera un
`INFORME.md` con las nueve puertas en `PASS` a partir de evidencia real. Hasta
entonces el estado es:

**IMPLEMENTED / SOFTWARE TESTED / HARDWARE VALIDATION PENDING**
