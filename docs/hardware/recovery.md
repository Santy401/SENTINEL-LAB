# Recuperación de hardware · ESP32-S3 SENTINEL-LAB

## Estado y límites

Este procedimiento describe cómo **recuperar** la placa después de una prueba
del entorno experimental `sentinel_esp32s3_n16r8_realtime`. Está
**implementado como documento y kit de evidencia; no ejecutado sobre hardware
en P2**. La imagen estable es `sentinel_esp32s3_n16r8_rf` (v0.3.1).

> **NO flashear sin autorización explícita del propietario.**  
> **NO borrar NVS, NO usar `esptool.py erase_flash` y NO añadir `--erase-all`
> como paso estándar.**  
> **NO publicar credenciales, MAC, tokens ni rutas privadas en logs o informes.**  
> HTTP y WebSocket quedan confinados al AP de laboratorio `SENTINEL-LAB`.

Los comandos de solo lectura de la sección 1 pueden ejecutarse sin autorización
de flasheo. Los comandos de carga de la sección 3 **requieren autorización
explícita**.

## 1. Comprobaciones de solo lectura

Identidad USB y flash (no modifican la placa):

```bash
cd ~/Github/sentinel-firmware
esptool.py chip-id
esptool.py flash-id
```

Salida esperada, en términos generales: un ESP32-S3 con 16 MB de flash y 8 MB
de PSRAM. La identidad concreta observada en la sesión anterior quedó registrada
en `build/rf-validation.json`; **no copie esos valores como si fueran una
lectura nueva**. Si el `chip-id` o el `flash-id` no concuerdan con el acta, detenga
el procedimiento.

Monitor serie (solo lectura; `Ctrl+C` para salir):

```bash
pio device monitor -p /dev/ttyACM0 -b 115200
```

Es posible que el puerto aparezca como puente USB-serie y no como CDC nativo; en
ese caso los mensajes `Serial` de la aplicación pueden no verse y solo se observa
el bootloader. La reaparición del AP es la comprobación observable de que
`setup()` terminó.

Verificación desde el host sin asociarse a otra red:

```bash
nmcli -t -f IN-USE,SSID,BSSID,CHAN,SIGNAL,MODE device wifi list --rescan no
nmcli -t -f DEVICE,TYPE,STATE,CONNECTION device status
curl -fsS http://192.168.4.1/api/rf
curl -fsS http://192.168.4.1/api/state
curl -fsS http://192.168.4.1/api/realtime/status
```

La última ruta **solo existe en el build `_realtime`**. Un `404` tras un rollback
a `_rf` es esperado y no debe registrarse como fallo de hardware. Si la ruta
existe, debe exponer únicamente estos campos:

```text
fw, realtimeVersion, bootId, uptimeMs, clockSynced, clientCount,
freeHeapBytes, minFreeHeapBytes, heapSamples, droppedFrames,
rejectedMessages, droppedTelemetry, skippedSimulatedEvents
```

No hay credenciales en estas respuestas. No use `curl -v` si la salida pudiera
incluir cabeceras de autorización.

## 2. Criterio para decidir rollback

Se considera candidata a recuperación cualquiera de estas observaciones:

- El AP `SENTINEL-LAB` no reaparece tras el reset.
- `/api/rf` o `/api/state` dejan de responder en el AP local.
- El handshake en `ws://192.168.4.1:81/realtime` no devuelve `101` con
  `Sec-WebSocket-Protocol: sentinel.v1`, `Accept` válido y versión 13 solicitada.
  Una respuesta `426` indica versión rechazada; una `400`, petición incompleta.
- `/api/realtime/status` no existe en una imagen que debería ser `_realtime`, o
  muestra `clockSynced=false` sin fuente de reloj autorizada.
- El informe `docs/validation/p2-hardware.md` termina en `FAIL`.

Antes de cargar cualquier imagen, guarde las lecturas anteriores como evidencia:

```bash
python3 tools/hw_capture.py --dir evidence/recuperacion --host 192.168.4.1 \
  --phase disconnected --seconds 60 --note 'Estado previo a la recuperación'
```

Si no hay AP, la captura registrará errores; eso es evidencia de fallo, no un
informe PASS.

## 3. Carga autorizada de P2 y rollback

**Solo después de autorización explícita.** Compilar y cargar el entorno
experimental:

```bash
pio run -e sentinel_esp32s3_n16r8_realtime
newgrp uucp -c 'pio run -e sentinel_esp32s3_n16r8_realtime -t upload --upload-port /dev/ttyACM0'
```

Tras el reset, verificar solo lectura:

```bash
nmcli -t -f IN-USE,SSID,BSSID,CHAN,SIGNAL,MODE device wifi list --rescan no
curl -fsS http://192.168.4.1/api/rf
curl -fsS http://192.168.4.1/api/realtime/status
python3 tools/hw_capture.py --dir evidence/p2-intento --host 192.168.4.1 \
  --phase associated --seconds 120 --distance-m 1.0
python3 tools/hw_report.py --dir evidence/p2-intento \
  --ssid SENTINEL-LAB --fw 0.3.1 --protocol 1.0.0 --max-clock-skew-ms 5000
```

Si el token de gateway está autorizado para la sesión, léalo de forma segura
antes de la captura `associated` con `read -rs SENTINEL_HW_BEARER; export
SENTINEL_HW_BEARER`. No lo escriba en el historial ni en un archivo.

Rollback autorizado a la imagen funcional:

```bash
newgrp uucp -c 'pio run -e sentinel_esp32s3_n16r8_rf -t upload --upload-port /dev/ttyACM0'
pio device monitor -p /dev/ttyACM0 -b 115200
curl -fsS http://192.168.4.1/api/rf
```

Después del rollback, repetir la captura de recuperación y archivar el informe.
Un cambio de `bootId` es esperado después de un flasheo; el informe lo tratará
como reinicio, no como recuperación de la misma sesión.

## 4. Por qué `erase_flash` no es estándar

`include/secrets.h` se compila dentro de la imagen de aplicación, mientras que
la configuración persistente del laboratorio (por ejemplo `deviceId`,
emparejamientos y modelos de calibración) vive en NVS. Esa configuración no debe
borrarse salvo un incidente documentado y con autorización explícita.
`esptool.py erase_flash` y `--erase-all` borran NVS junto con la aplicación y
pueden destruir evidencia de identidad y calibraciones acumuladas. El flasheo
normal de PlatformIO ya verifica bootloader, tabla de particiones y aplicación
sin tocar el rango NVS.

Si existiera una autorización excepcional para borrar NVS, debe hacerse con
copia de seguridad verificada y registrarse por separado; **no** forma parte de
este procedimiento ni del kit P2.

## 5. Registro de la intervención

Toda intervención debe quedar descrita con:

- fecha y operador,
- entorno compilado (`_rf` o `_realtime`),
- puerto y resultados de `chip-id` / `flash-id` sin secretos,
- resultado del upload y del reinicio,
- rutas de la evidencia y del `INFORME.md`,
- decisión de aceptar el rollback o repetir la prueba.

En el kit portable de Arch, las dos cargas autorizadas pueden ejecutarse con
`tools/p2_flash_authorized.sh`, que exige una confirmación literal y prohíbe
opciones de borrado. Los scripts de preflight, puerto serie, HTTP y WebSocket
del kit son de solo lectura. El inventario y los límites del respaldo están en
[`v031-backup-inventory.md`](v031-backup-inventory.md) y el procedimiento de
transferencia en [`p2-kit.md`](p2-kit.md).

La aceptación final de hardware exige un `INFORME.md` con todas las puertas en
PASS generado desde evidencia real. Hasta entonces el estado documentado sigue
siendo **implemented / software tested, hardware untested**.
