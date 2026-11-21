# Guía de desarrollo

## Requisitos locales

- Python 3
- Node.js
- g++ con soporte C++17
- PlatformIO Core 6
- `esptool` solo para verificación de hardware

## Comandos

```bash
python3 tools/check_software.py
python3 scripts/check-repository.py
pio run -e sentinel_esp32s3_n16r8_rf
```

Ver todos los entornos:

```bash
for env in sentinel_esp32s3_n16r8 sentinel_esp32s3_n16r8_diag \
           sentinel_esp32s3_n16r8_v02 sentinel_esp32s3_n16r8_rf; do
  pio run -e "$env" || exit 1
done
```

## Hardware

No flashear sin autorización explícita. Antes de cargar:

1. Confirmar puerto y puente USB-serie.
2. Confirmar MAC del ESP32.
3. Confirmar entorno PlatformIO.
4. Confirmar SHA256 del binario.
5. No usar borrado de NVS salvo autorización específica.

## Idioma

Código en inglés. Documentación e informes en español.
