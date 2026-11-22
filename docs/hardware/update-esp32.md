# Actualizar el ESP32-S3 a una release realtime

Procedimiento usado para v0.4.2 el 2026-11-22. Solo escribe bootloader, tabla
de particiones, `boot_app0` y la aplicación; **NVS no se toca**. No borra la
flash ni restaura la imagen completa; eso exige el procedimiento separado y una
autorización explícita (`docs/hardware/p2-full-recovery.md`).

## 1. Credenciales locales (fuera de Git)

`include/secrets.h` (ignorado) debe definir `SENTINEL_WIFI_SSID`,
`SENTINEL_WIFI_PASSWORD` y `SENTINEL_REALTIME_BEARER_TOKEN`. Para crear un token
sin imprimirlo:

```bash
mkdir -p ~/.config/sentinel-lab && chmod 700 ~/.config/sentinel-lab
( umask 077; python3 -c "import secrets;print(secrets.token_hex(24))" > ~/.config/sentinel-lab/realtime-token )
printf '#define SENTINEL_REALTIME_BEARER_TOKEN "%s"\n' "$(cat ~/.config/sentinel-lab/realtime-token)" >> include/secrets.h
```

El mismo token se introduce en la app iOS (Ajustes → token, se guarda en
Keychain) y se pasa al gateway con `--token-file`.

## 2. Compilar y preparar la release

```bash
python3 tools/check_software.py
pio run -e sentinel_esp32s3_n16r8_realtime
python3 tools/p2_stage_release.py --release v0.4.2   # escribe ~/p2-hardware-kit/firmware/v0.4.2
```

`p2_stage_release.py` falla si el bootloader, la tabla de particiones o
`boot_app0` difieren de v0.3.1. Un layout distinto requiere revisión, porque
podría mover NVS.

## 3. Verificaciones previas

```bash
sha256sum -c <<< "f6515d77b56cb7dbf27aec520269edd32147a62e3d2db9fe9fec421ee30a42ae  $HOME/sentinel-backups/original-v0.3.1/flash-full-16mb.bin"
(cd ~/p2-hardware-kit/firmware/v0.4.2 && sha256sum -c SHA256SUMS)
esptool --chip esp32s3 --port /dev/ttyACM0 flash-id   # ESP32-S3 rev v0.2, 16MB, PSRAM 8MB
```

Si la sesión no tiene el grupo `uucp` activo, aunque el usuario pertenezca a
él, ejecuta los comandos dentro de `newgrp uucp`.

## 4. Flashear

```bash
tools/p2_flash_authorized.sh \
  --environment sentinel_esp32s3_n16r8_realtime --port /dev/ttyACM0 \
  --kit-root ~/p2-hardware-kit --release v0.4.2 \
  --confirm 'FLASH sentinel_esp32s3_n16r8_realtime /dev/ttyACM0'
```

Debe terminar con cuatro `Hash of data verified`.

## 5. Aceptar o revertir

Une el equipo al AP y ejecuta `tools/p3_hw_integration.py` (ver
`docs/validation/p3-integration.md`). Acepta la imagen solo si todas las
comprobaciones dan PASS.

Rollback a v0.3.1, también sin tocar NVS:

```bash
tools/p2_flash_authorized.sh --environment sentinel_esp32s3_n16r8_rf \
  --port /dev/ttyACM0 --kit-root ~/p2-hardware-kit \
  --confirm 'FLASH sentinel_esp32s3_n16r8_rf /dev/ttyACM0'
```

`~/p2-hardware-kit/firmware/v0.4.2/firmware.bin` contiene credenciales: no lo
subas ni lo adjuntes a ningún PR o release.
