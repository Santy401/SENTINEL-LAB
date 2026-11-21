# Verificación de offsets y particiones · SENTINEL-LAB P2

Documento de evidencia estática. No implica flasheo ni medición de hardware.
Comprobado en `omarchy` (Arch Linux) sobre el kit `~/p2-hardware-kit`, sin abrir
el puerto serie.

## Fuentes inspeccionadas

- `platformio.ini`: todos los entornos usan
  `board_upload.flash_size = 16MB`, `board_build.arduino.partitions = default_16MB.csv`
  y `board_build.arduino.memory_type = qio_opi`.
- CSV del framework:
  `~/.platformio/packages/framework-arduinoespressif32/tools/partitions/default_16MB.csv`.
- Tablas binarias compiladas: `firmware/v0.3.1/partitions.bin` y
  `firmware/v0.4.0/partitions.bin` (ambas de 3 072 B, idénticas).
- Imágenes `image-info` de `bootloader.bin` y `firmware.bin`: ambas declaran
  `Flash size: 16MB`, `Flash freq: 80m`, `Flash mode: DIO`, con hash de
  validación correcto.
- Herramienta de lectura: `tools/p2_layout_check.py` (solo lectura).

## Tabla de particiones real (decodificada del binario)

| Nombre | Tipo | Subtipo | Offset | Tamaño | Rango |
|---|---:|---:|---:|---:|---|
| `nvs` | 1 (data) | 2 (nvs) | `0x009000` | `0x005000` | `0x009000`–`0x00E000` |
| `otadata` | 1 (data) | 0 (ota) | `0x00E000` | `0x002000` | `0x00E000`–`0x010000` |
| `app0` | 0 (app) | 16 (ota_0) | `0x010000` | `0x640000` | `0x010000`–`0x650000` |
| `app1` | 0 (app) | 17 (ota_1) | `0x650000` | `0x640000` | `0x650000`–`0xC90000` |
| `spiffs` | 1 (data) | 130 (spiffs) | `0xC90000` | `0x360000` | `0xC90000`–`0xFF0000` |
| `coredump` | 1 (data) | 3 (coredump) | `0xFF0000` | `0x010000` | `0xFF0000`–`0x1000000` |

La tabla contiene además la entrada opcional de MD5 (`magic 0xEBEB`), que el
verificador reconoce como fin de las entradas.

## Regiones que escribe el script custodiado

| Archivo | Offset | Tamaño | Fin | Partición afectada |
|---|---:|---:|---:|---|
| `bootloader.bin` | `0x00000` | 19 968 B (`0x4E00`) | `0x04E00` | Bootloader (reservado) |
| `partitions.bin` | `0x08000` | 3 072 B (`0xC00`) | `0x08C00` | Tabla de particiones (reservado) |
| `boot_app0.bin` | `0x0E000` | 8 192 B (`0x2000`) | `0x10000` | `otadata` |
| `firmware.bin` | `0x10000` | 1 096 768 B (`0x10BC40`) | `0x11BC40` | `app0` |

## Análisis de superposición

| Comprobación | Resultado |
|---|---|
| ¿Alguna región escrita toca `nvs` (`0x9000`–`0xE000`)? | **No.** La tabla termina en `0x8C00` y el siguiente bloque empieza en `0xE000`. |
| ¿Alguna región escrita toca `spiffs` o `coredump`? | **No.** |
| `boot_app0.bin` y `otadata` | Coinciden exactamente (8 192 B). Es el comportamiento estándar de Arduino-ESP32: `boot_app0` inicializa `otadata`. |
| `firmware.bin` y `app0` | El offset `0x10000` coincide con `app0`; el binario cabe holgadamente (1 096 768 B de 6 553 600 B). |
| Ambos conjuntos de firmware | Usan tablas de particiones idénticas; no hay cambio de layout entre v0.3.1 y v0.4.0. |

**Conclusión:** NVS, calibraciones y datos persistentes quedan fuera de las cuatro
regiones de escritura. La única partición persistente sobrescrita es `otadata`, y se
sobrescribe deliberadamente con `boot_app0.bin` para inicializar el arranque de
`app0`. Esto debe tenerse en cuenta si en el futuro se activa un esquema OTA con
`app1`: la escritura de `boot_app0` fijaría el arranque en `app0`.

`write-flash` (esptool 5.x) sobrescribe solo los sectores de las regiones
indicadas; no se usa `--erase-all`, por lo que NVS no se borra.

## Evidencia reproducible

```bash
cd ~/p2-hardware-kit
python3 tools/p2_layout_check.py --kit . --save-json evidence/checksums/flash-layout.json
```

Resultado de la ejecución local: **21 PASS, 0 FAIL**. El JSON guardado registra
las seis particiones, los cuatro rangos de escritura y los 21 criterios.

## Compatibilidad de esptool observada

- `esptool v5.6.0` en `~/.local/bin/esptool` del equipo de preparación; el kit
  no depende de esa ruta y lo detecta en el `PATH`.
- `pio pkg exec --tool esptool@2.50400.0 -- esptool.py` **no válido**: esta
  PlatformIO 6.2.0 no acepta `--tool`. La forma correcta es
  `pio pkg exec -p tool-esptool -c 'esptool.py …'`.
- En esptool 5.x el subcomando canónico es `write-flash`; `write_flash` sigue
  aceptado pero avisa de obsolescencia. El script elige automáticamente según
  la versión detectada.
