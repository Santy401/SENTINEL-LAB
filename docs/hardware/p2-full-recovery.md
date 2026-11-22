# Recuperación completa desde la imagen original de 16 MiB

Procedimiento **preparado pero no ejecutado**. Requiere autorización explícita
y manual para cada operación de escritura. No sustituye a la recuperación
parcial descrita en [`recovery.md`](recovery.md).

## Diferencia entre recuperación completa y parcial

| Modalidad | Qué escribe | NVS y datos persistentes | Script |
|---|---|---|---|
| **Parcial** (recomendada para P2) | Bootloader, tabla de particiones, `otadata` y `app0` en cuatro bloques concretos | **Conservados** | `tools/p2_flash_authorized.sh --environment sentinel_esp32s3_n16r8_rf` |
| **Completa** (último recurso) | Los 16 777 216 bytes de flash desde `0x0` | **Reemplazados por completo** | `tools/p2_restore_full_authorized.sh` |

La recuperación completa solo se considera si la parcial no restaura el equipo
o si se necesita recuperar el estado de fábrica de NVS. **Borra calibraciones,
configuración, estado de red y cualquier dato persistente capturado en flash.**

## Respaldo original (inmutable)

| Campo | Valor |
|---|---|
| Ruta en el equipo de hardware | `~/sentinel-backups/original-v0.3.1/flash-full-16mb.bin` |
| Tamaño | 16 777 216 bytes (`0x1000000`) |
| SHA-256 | `f6515d77b56cb7dbf27aec520269edd32147a62e3d2db9fe9fec421ee30a42ae` |
| Verificación local | `sha256sum -c SHA256SUMS` → `flash-full-16mb.bin: La suma coincide` |

Reglas de conservación:

- Verificar tamaño y SHA-256 **antes y después** de cualquier operación.
- Abrir la imagen solo en modo lectura. El script la pasa directamente a
  esptool; **no la copia**.
- **No incluir la imagen en Git, logs, artefactos del kit, informes ni mensajes.**
  Solo se publica su hash, nunca su contenido.
- No renombrar, mover ni reescribir el directorio original.
- Estado observado y **no modificado**: directorio `drwxr-xr-x` y archivo
  `-rw-r--r--`, propietario `santy401:uucp`. Si el propietario lo autoriza,
  puede restringirse a `700`/`600`, sin alterar el contenido ni los hashes.

## Comprobaciones previas (solo lectura)

```bash
cd ~/sentinel-backups/original-v0.3.1
stat -c '%n %s bytes' flash-full-16mb.bin
sha256sum -c SHA256SUMS

cd ~/p2-hardware-kit
python3 tools/p2_serial_check.py --port /dev/ttyACM0
python3 tools/p2_preflight.py --root .
```

`p2_serial_check.py` y `p2_preflight.py` no abren el puerto, no envían bytes y
no reinician el dispositivo. La identidad y la capacidad del chip se confirman
dentro del script de recuperación, ya autorizado, mediante `chip-id` y
`flash-id` de esptool con `--chip esp32s3` y `--flash-size 16MB`.

## Ejecución autorizada (NO ejecutar sin orden explícita)

```bash
~/p2-hardware-kit/tools/p2_restore_full_authorized.sh \
  --backup ~/sentinel-backups/original-v0.3.1/flash-full-16mb.bin \
  --port /dev/ttyACM0 \
  --confirm 'RESTORE-FULL f6515d77 /dev/ttyACM0 CONFIRM'
```

El script:

1. Exige la confirmación literal; la rechaza si el hash, el puerto o la palabra
   `CONFIRM` no coinciden exactamente. Es imposible ejecutarlo por accidente.
2. Verifica tamaño y SHA-256 de la imagen antes de escribir.
3. Comprueba que `/dev/ttyACM0` es un carácter accesible y que el usuario está
   en `uucp`/`dialout`.
4. Detecta esptool de forma robusta y usa el subcomando acorde a su versión
   (`write-flash` en 5.x, `write_flash` en 4.x).
5. Imprime `chip-id` y `flash-id` antes de escribir y fuerza `--chip esp32s3`
   con `--flash-size 16MB`.
6. Escribe la imagen completa en `0x0`, sin `--erase-all` ni opciones de borrado.
7. Recalcula el SHA-256 de la imagen al final para demostrar que no cambió.

**No se extraen datos desde el dispositivo ni se publica la imagen.** No se
ejecuta ninguna recuperación automática: siempre interviene una confirmación
humana.

## Después de cualquier recuperación

- Verificar AP `SENTINEL-LAB`, `/api/state` y las rutas HTTP v0.3.1.
- `/api/realtime/status` debe responder `404` si el equipo volvió a `_rf`.
- Registrar fecha UTC, commit, versión de esptool, hash de la imagen y
  resultado real en `evidence/`. No marcar `PASS` sin evidencia capturada.

Estado actual: **recuperación completa preparada, NO ejecutada**.
