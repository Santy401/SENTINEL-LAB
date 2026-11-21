# Registro de compilación · SENTINEL Core v0.4.0

Este acta describe una compilación de software ejecutada en Ubuntu. **No acredita
ejecución en hardware ni mide heap en tiempo de ejecución.**

## Identificación

| Campo | Valor |
|---|---|
| Fecha | `2026-11-21` |
| Repositorio | `https://github.com/Santy401/SENTINEL-LAB` |
| Commit exacto | `7ef61e1603ef503fefcf7e1fc7fd3af77aced489` |
| Rama | `feat/firmware-realtime-core` |
| Entorno PlatformIO | `sentinel_esp32s3_n16r8_realtime` |
| Comando | `pio run -e sentinel_esp32s3_n16r8_realtime` |
| Resultado | `SUCCESS` |
| Duración | `00:00:02.762` |
| Log completo | `build/build-sentinel_esp32s3_n16r8_realtime.log` |

## Herramientas

| Herramienta | Versión observada |
|---|---|
| PlatformIO Core | `6.2.0` |
| Python | `3.14.8` |
| `espressif32` | `8.0.0` |
| Arduino-ESP32 framework | `4.30312.261116` (3.3.12) |
| `framework-arduinoespressif32-esp32s3-libs` | `3.3.12` |
| ArduinoJson | `7.4.3` |
| `tool-esptool` | `2.50400.0` (esptool 5.4.0) |
| Toolchain Xtensa | `14.2.0+20260121` |
| Toolchain RISC-V | `14.2.0+20260121` |
| Compilador host usado en pruebas | GCC 16.2.1 |

No se instalaron dependencias globales nuevas para preparar este kit. El
validador canónico usa un entorno virtual aislado con
`packages/protocol/requirements.txt`.

## Tamaños del enlazador

| Métrica | Uso | Total | Porcentaje |
|---|---:|---:|---:|
| RAM estática | 86 084 B | 327 680 B | 26,3 % |
| Flash de programa | 1 079 603 B | 6 553 600 B | 16,5 % |

Estos valores son estimaciones estáticas del enlazador. **No son heap libre,
heap mínimo ni pico de uso en ejecución.** La medición real debe obtenerse de
`/api/realtime/status` sobre la placa y registrarse como evidencia.

## Artefactos generados

| Archivo | Tamaño | SHA-256 |
|---|---:|---|
| `bootloader.bin` | 19 968 B | `84bb365666003b051f564d04380d0cf5d9d85c420ddafe9fc863071b49e49bfc` |
| `partitions.bin` | 3 072 B | `bd0f7954aca2ef7d925ee21aaa1f3dc8822d1d6ce5cbbd26a135e5886bfff6ce` |
| `boot_app0.bin` | 8 192 B | `f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0` |
| `firmware.bin` | 1 096 768 B | `4c4ff77649576fb45a501065a0680b2d90329c6e15a74cf956fd2c69c30e2321` |

El tamaño de `firmware.bin` incluye alineación y relleno; por eso difiere del
consumo de programa informado por el enlazador.

## Advertencias

La salida de compilación no registró líneas `warning:` ni `error:`. La única
observación relevante es informativa: la board genérica se identifica como N8
sin PSRAM, mientras que `platformio.ini` fuerza explícitamente 16 MB de flash,
`qio_opi` y `BOARD_HAS_PSRAM` para el hardware N16R8 verificado.

## Verificación de software acompañante

- `python3 tools/check_software.py`: OK.
- Pruebas host del núcleo, handshake, sesión, reloj, salud y ruta HTTP: OK.
- Contrato v1 y fixtures: OK desde entorno virtual con `jsonschema`.
- Cinco entornos PlatformIO: compilación verificada en la sesión de trabajo.

No se realizó flasheo, reinicio remoto, captura de radio, captura WebSocket
sobre Wi-Fi real ni medición de heap en placa.
