# Kit portable P2 para Arch Linux

Este documento acompaña al kit generado por `tools/p2_make_kit.py`. El kit es
autónomo: funciona desde `~/p2-hardware-kit` en `omarchy` sin depender de rutas
del servidor de compilación. El hardware es un ESP32-S3 N16R8 en `/dev/ttyACM0`.

**Estado del kit:** preparación de validación. No incluye evidencia falsa ni
resultados de hardware. No ejecuta flasheo automáticamente.

> **NO flashear sin autorización explícita.**  
> **NO borrar flash ni NVS.**  
> **NO fusionar PR #6 ni iniciar P3 desde este kit.**

## 1. Contenido

```text
p2-hardware-kit-YYYYMMDDTHHMMSSZ/
├── README.md                       # copia de este documento
├── KIT_MANIFEST.md                 # procedencia y hashes de alto nivel
├── SHA256SUMS                      # integridad de todo el kit
├── build-record.json               # commit, entorno, tamaños y hashes del build
├── firmware/
│   ├── v0.3.1/                     # respaldo verificado + SHA256SUMS + MANIFEST
│   └── v0.4.0/                     # imagen P2 + SHA256SUMS
├── tools/
│   ├── p2_preflight.py             # solo lectura
│   ├── p2_serial_check.py          # solo lectura
│   ├── p2_http_check.py            # solo lectura
│   ├── p2_ws_probe.py              # solo lectura, sin gateway hello
│   ├── p2_layout_check.py          # solo lectura, offsets y particiones
│   ├── p2_esptool.sh               # helper de detección compartido
│   ├── p2_flash_authorized.sh      # ESCRITURA parcial, confirmación literal
│   ├── p2_restore_full_authorized.sh  # ESCRITURA total, confirmación literal
│   ├── hw_common.py
│   ├── hw_capture.py
│   └── hw_report.py
├── packages/protocol/              # validador y esquemas v1 incluidos
├── docs/                           # procedimientos, matriz, actas y recuperación
├── build/                          # log de compilación v0.4.0
└── evidence/                       # directorios vacíos para evidencia real
```

La imagen original completa de 16 MiB **no se incluye**: permanece en
`~/sentinel-backups/original-v0.3.1/flash-full-16mb.bin` en el equipo de hardware.

Los scripts de preflight, puerto serie, HTTP, WebSocket y layout **nunca**
escriben en el dispositivo. Los dos scripts de escritura exigen confirmaciones
literales distintas y no aceptan opciones de borrado.

## 2. Verificación desde Arch

```bash
cd ~/p2-hardware-kit
sha256sum -c SHA256SUMS
cat KIT_MANIFEST.md
python3 tools/p2_preflight.py --root .
python3 tools/p2_serial_check.py --port /dev/ttyACM0
python3 tools/p2_layout_check.py --kit . --save-json evidence/checksums/flash-layout.json
```

`p2_preflight.py` verifica el manifiesto, la presencia de artefactos, la
ausencia de secretos, herramientas disponibles (incluido `esptool`), permisos
del puerto y, solo si está disponible, la visibilidad pasiva del AP y
`/api/state`. Los avisos de red no autorizan por sí solos una prueba; los fallos
de integridad sí detienen el procedimiento.

El validador de protocolo está incluido en `packages/protocol/`. Si falta
`jsonschema` en el Python del sistema, créese un venv local, sin tocar el
sistema:

```bash
cd ~/p2-hardware-kit
python3 -m venv .venv
.venv/bin/pip install -r packages/protocol/requirements.txt
.venv/bin/python tools/hw_report.py --help
```

## 3. Comprobaciones de solo lectura

Con el equipo conectado a la red de laboratorio:

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
  --fw 0.3.1 --realtime-version 0.4.0 --protocol 1.0.0 \
  --max-clock-skew-ms 5000
```

### Versiones: `--fw 0.3.1` no es un error

`--fw` designa la **versión de la API HTTP**, que v0.4.0 conserva
intencionalmente en `0.3.1` para no romper las rutas existentes. La versión del
núcleo realtime (`realtimeVersion`) y el campo `firmwareVersion` de los sobres v1
valen **`0.4.0`** y se comprueban con `--realtime-version 0.4.0`. Antes de la
corrección, `hw_report.py` comparaba `firmwareVersion` contra `--fw`, lo que
rechazaba telemetría v1 válida.

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

El script detecta esptool en este orden: `esptool`, `esptool.py`,
`python3 -m esptool`, y `pio pkg exec -p tool-esptool -c 'esptool.py …'` sin
fijar versión. Valida que la versión sea ≥ 4 y usa `write-flash` en 5.x o
`write_flash` en 4.x. No se usa la forma obsoleta
`pio pkg exec --tool esptool@…`, incompatible con PlatformIO 6.2.0.

Tras la carga, repetir solo lectura:

```bash
python3 tools/p2_http_check.py --host 192.168.4.1 \
  --save-json evidence/http/post-flash.json
python3 tools/p2_ws_probe.py --host 192.168.4.1 --seconds 10 \
  --save-json evidence/ws/post-flash.json
```

## 5. Recuperación autorizada

Recuperación **parcial** (conserva NVS), si el AP no reaparece o falla una
puerta crítica:

```bash
tools/p2_flash_authorized.sh \
  --environment sentinel_esp32s3_n16r8_rf \
  --port /dev/ttyACM0 \
  --confirm 'FLASH sentinel_esp32s3_n16r8_rf /dev/ttyACM0'
```

Después verificar `/api/rf` y `/api/state`. `/api/realtime/status` debe responder
`404` en `_rf`; eso confirma que se volvió a la imagen estable, no un fallo.
No use `esptool erase-flash`, `--erase-all` ni ninguna opción que borre NVS.
Véase `docs/hardware/p2-flash-layout.md` para el análisis de offsets.

La recuperación **completa** desde la imagen original de 16 MiB reemplaza
también NVS y todos los datos persistentes. Está preparada, no ejecutada, en
[`p2-full-recovery.md`](p2-full-recovery.md). Su imagen no se copia al kit ni se
publica.

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

**P2 READY FOR AUTHORIZED HARDWARE FLASH — NOT YET FLASHED**
