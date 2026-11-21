#!/usr/bin/env bash
# Authorized write operation ONLY. This script is never called by preflight.
# It writes the four explicit flash regions and never erases NVS or the full chip.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
KIT_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
ENVIRONMENT=""
PORT="/dev/ttyACM0"
CONFIRM=""
DIR=""

usage() {
  cat <<'EOF'
Uso:
  tools/p2_flash_authorized.sh \
    --environment sentinel_esp32s3_n16r8_realtime \
    --port /dev/ttyACM0 \
    --confirm 'FLASH sentinel_esp32s3_n16r8_realtime /dev/ttyACM0'

Esta operación ESCRIBE EN FLASH. No se ejecuta automáticamente.
No se aceptan opciones de borrado; NVS permanece intacto.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --environment)
      ENVIRONMENT="${2:-}"
      shift 2
      ;;
    --port)
      PORT="${2:-}"
      shift 2
      ;;
    --confirm)
      CONFIRM="${2:-}"
      shift 2
      ;;
    --kit-root)
      KIT_ROOT="$(cd -- "${2:-}" && pwd)"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --erase|--erase-all|--erase-flash)
      echo "FAIL: este kit no permite borrar flash ni NVS" >&2
      exit 2
      ;;
    *)
      echo "FAIL: opción desconocida: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

case "${ENVIRONMENT}" in
  sentinel_esp32s3_n16r8_realtime)
    DIR="${KIT_ROOT}/firmware/v0.4.0"
    ;;
  sentinel_esp32s3_n16r8_rf)
    DIR="${KIT_ROOT}/firmware/v0.3.1"
    ;;
  *)
    echo "FAIL: entorno no permitido: ${ENVIRONMENT:-<vacío>}" >&2
    exit 2
    ;;
esac

EXPECTED="FLASH ${ENVIRONMENT} ${PORT}"
if [[ "${CONFIRM}" != "${EXPECTED}" ]]; then
  echo "FAIL: confirmación inexacta." >&2
  echo "Se esperaba literalmente: ${EXPECTED}" >&2
  exit 2
fi

if [[ ! -d "${DIR}" ]]; then
  echo "FAIL: no existe el conjunto de artefactos: ${DIR}" >&2
  exit 2
fi

for artifact in bootloader.bin partitions.bin boot_app0.bin firmware.bin SHA256SUMS; do
  if [[ ! -s "${DIR}/${artifact}" ]]; then
    echo "FAIL: artefacto ausente o vacío: ${DIR}/${artifact}" >&2
    exit 2
  fi
done

if ! (cd "${DIR}" && sha256sum -c --status SHA256SUMS); then
  echo "FAIL: SHA-256 del conjunto seleccionado no coincide" >&2
  exit 2
fi

if [[ ! -e "${PORT}" ]]; then
  echo "FAIL: puerto serie no presente: ${PORT}" >&2
  exit 2
fi
if [[ ! -r "${PORT}" || ! -w "${PORT}" ]]; then
  echo "FAIL: sin permisos de lectura/escritura sobre ${PORT}" >&2
  echo "Use una sesión con el grupo uucp/dialout correspondiente." >&2
  exit 2
fi
if ! id -nG | tr ' ' '\n' | grep -Eq '^(uucp|dialout)$'; then
  echo "FAIL: el usuario actual no pertenece a uucp ni a dialout" >&2
  exit 2
fi

ESPTOOL=()
if command -v pio >/dev/null 2>&1; then
  ESPTOOL=(pio pkg exec --tool esptool@2.50400.0 -- esptool.py)
elif command -v esptool.py >/dev/null 2>&1; then
  ESPTOOL=(esptool.py)
else
  echo "FAIL: no se encontró esptool.py ni PlatformIO para invocarlo" >&2
  exit 2
fi

echo "ADVERTENCIA: operación de ESCRITURA en flash."
echo "Entorno:     ${ENVIRONMENT}"
echo "Puerto:      ${PORT}"
echo "Origen:      ${DIR}"
echo "NVS:         NO se borra"
echo "Confirmado:  ${CONFIRM}"

"${ESPTOOL[@]}" \
  --port "${PORT}" \
  --baud 460800 \
  write_flash \
  0x00000 "${DIR}/bootloader.bin" \
  0x08000 "${DIR}/partitions.bin" \
  0x0e000 "${DIR}/boot_app0.bin" \
  0x10000 "${DIR}/firmware.bin"

echo "OK: escritura finalizada. Verifique AP, HTTP y WebSocket antes de aceptar la imagen."
