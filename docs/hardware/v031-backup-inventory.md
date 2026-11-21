# Inventario y recuperabilidad del respaldo v0.3.1

## Estado verificado

Directorio inspeccionado:

`/home/santy401/.sentinel-backups/v0.3.1-rf-20261121T194718Z`

Fecha declarada de creación: `2026-11-21T19:47:18Z`. Procedencia declarada:
`origin/main` en `485215d6f9b4986dbe6dd3b44278af2ac446c6df`, entorno
`sentinel_esp32s3_n16r8_rf`, compilación `SUCCESS` con
`include/secrets.example.h` copiado como plantilla, sin credenciales reales.

La comprobación local ejecutada fue:

```bash
sha256sum -c SHA256SUMS
```

Resultado: **4/4 sumas coinciden**.

## Archivos presentes

| Archivo | Tamaño | Offset de flasheo | SHA-256 |
|---|---:|---:|---|
| `bootloader.bin` | 19 968 B | `0x00000` | `84bb365666003b051f564d04380d0cf5d9d85c420ddafe9fc863071b49e49bfc` |
| `partitions.bin` | 3 072 B | `0x08000` | `bd0f7954aca2ef7d925ee21aaa1f3dc8822d1d6ce5cbbd26a135e5886bfff6ce` |
| `boot_app0.bin` | 8 192 B | `0x0e000` | `f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0` |
| `firmware.bin` | 1 065 360 B | `0x10000` | `d9b8c57d6f68300a49e3c116e0a69f023753f65bfd4fb6075d5c8b6589532ec8` |
| `SHA256SUMS` | 321 B | — | Manifiesto de los cuatro binarios |
| `MANIFEST.md` | 1 831 B | — | Procedencia y uso previsto |

Los offsets coinciden con el procedimiento de flasheo usado para v0.3.1 y con
los cuatro bloques escritos por PlatformIO/esptool: bootloader, tabla de
particiones, `boot_app0` y aplicación.

## Tres categorías que no deben confundirse

1. **Binario de aplicación compilado.** Es solo `firmware.bin`. Contiene la
   imagen de la aplicación, no el bootloader ni la tabla de particiones. El
   tamaño del archivo (1 065 360 B) no equivale al consumo declarado por el
   enlazador (1 049 995 B de programa), porque el binario incluye alineación y
   relleno.
2. **Conjunto completo de artefactos de flasheo.** Son los cuatro binarios
   anteriores con sus offsets. Este respaldo **sí** pertenece a esta categoría:
   permite restaurar bootloader, particiones, `boot_app0` y aplicación.
3. **Imagen completa de flash extraída del dispositivo.** Requeriría una lectura
   tipo `esptool.py read_flash 0x0 0x1000000 imagen.bin`, incluiría NVS y todo
   el contenido sin uso. **Este respaldo no la contiene.**

Por tanto, el respaldo es suficiente para una recuperación funcional de la
imagen de firmware, **pero no para una restauración bit a bit del estado del
dispositivo**.

## Evaluación de recuperabilidad

| Aspecto | Evaluación |
|---|---|
| Bootloader | Recuperable desde artefacto verificado |
| Tabla de particiones | Recuperable desde artefacto verificado |
| `boot_app0` | Recuperable desde artefacto verificado |
| Aplicación v0.3.1 | Recuperable desde artefacto verificado |
| NVS, emparejamientos y calibraciones | **No recuperables desde este respaldo** |
| Imagen completa de flash | **No disponible** |
| Credenciales Wi-Fi | No incluidas; deben existir solo en `include/secrets.h` local antes de compilar |
| Reproducibilidad byte a byte | No reclamada |

`build/rf-validation.json` registra el hash históricamente flasheado
`918d9157f6308e84fb022faf0825ca37fb41ebc2edee28dcf70f22dce0f55799`, distinto
del binario recompilado aquí. La diferencia se atribuye a la cadena de
herramientas local; no se usa el hash histórico como criterio de aceptación.
La compatibilidad se apoya en fuentes guardadas, mismos tamaños estáticos y
verificación de checksums del conjunto actual.

## Riesgo pendiente

El `MANIFEST.md` original menciona la ruta
`docs/hardware-validation/p2-hardware-validation.md`, que ya no existe en el
repositorio. Las rutas vigentes son:

- `docs/hardware/recovery.md`
- `docs/validation/p2-hardware.md`
- `docs/hardware/p2-kit.md`

No se modificó el manifiesto original para conservar su valor histórico. El kit
portable incluye este documento y la corrección de rutas.

Antes de cualquier recuperación real debe autorizarse por escrito la operación
de escritura, verificarse el puerto serie en el PC de Arch y conservarse la
evidencia previa. No se ha ejecutado ninguna escritura sobre el ESP32.
