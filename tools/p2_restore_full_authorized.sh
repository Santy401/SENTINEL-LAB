#!/usr/bin/env bash
# FULL flash restore ONLY after explicit, unambiguous manual confirmation.
# This is a destructive operation: it replaces NVS and all persistent data.
# The backup image is opened read-only and is never copied or modified.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

# shellcheck source=p2_esptool.sh
source "${SCRIPT_DIR}/p2_esptool.sh"

BACKUP="${HOME}/sentinel-backups/original-v0.3.1/flash-full-16mb.bin"
PORT="/dev/ttyACM0"
CONFIRM=""
EXPECTED_SIZE=16777216
EXPECTED_SHA256="f6515d77b56cb7dbf27aec520269edd32147a62e3d2db9fe9fec421ee30a42ae"

usage() {
  cat <<'EOF'
Uso:
  tools/p2_restore_full_authorized.sh \
    --backup ~/sentinel-backups/original-v0.3.1/flash-full-16mb.bin \
    --port /dev/ttyACM0 \
    --confirm 'RESTORE-FULL f6515d77 /dev/ttyACM0 CONFIRM'

ADVERTENCIA: ESCRITURA COMPLETA DE FLASH. Reemplaza también NVS, calibraciones
y todos los datos persistentes. No es una recuperación parcial.

La imagen original se abre en modo lectura, nunca se copia ni se publica.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --backup)
      BACKUP="${2:-}"
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
    -h|--help)
      usage
      exit 0
      ;;
    --erase|--erase-all|--erase-flash|--partial|--skip-verify)
      echo "FAIL: opción no permitida en recuperación completa: $1" >&2
      exit 2
      ;;
    *)
      echo "FAIL: opción desconocida: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ ! -f "${BACKUP}" || ! -r "${BACKUP}" ]]; then
  echo "FAIL: imagen original ausente o no legible: ${BACKUP}" >&2
  exit 2
fi

ACTUAL_SIZE="$(stat -c %s "${BACKUP}")"
if [[ "${ACTUAL_SIZE}" != "${EXPECTED_SIZE}" ]]; then
  echo "FAIL: tamaño inesperado de la imagen: ${ACTUAL_SIZE} != ${EXPECTED_SIZE}" >&2
  exit 2
fi

ACTUAL_SHA256="$(sha256sum "${BACKUP}" | awk '{print $1}')"
if [[ "${ACTUAL_SHA256}" != "${EXPECTED_SHA256}" ]]; then
  echo "FAIL: SHA-256 de la imagen original no coincide" >&2
  exit 2
fi

HASH_PREFIX="${ACTUAL_SHA256:0:8}"
EXPECTED_CONFIRM="RESTORE-FULL ${HASH_PREFIX} ${PORT} CONFIRM"
if [[ "${CONFIRM}" != "${EXPECTED_CONFIRM}" ]]; then
  echo "FAIL: confirmación inexacta." >&2
  echo "Se esperaba literalmente: ${EXPECTED_CONFIRM}" >&2
  exit 2
fi

if [[ ! -c "${PORT}" ]]; then
  echo "FAIL: puerto serie no presente o no es dispositivo de carácter: ${PORT}" >&2
  exit 2
fi
if [[ ! -r "${PORT}" || ! -w "${PORT}" ]]; then
  echo "FAIL: sin permisos de lectura/escritura sobre ${PORT}" >&2
  exit 2
fi
if ! id -nG | tr ' ' '\n' | grep -Eq '^(uucp|dialout)$'; then
  echo "FAIL: el usuario actual no pertenece a uucp ni a dialout" >&2
  exit 2
fi

if ! p2_esptool_detect || ! p2_esptool_validate; then
  echo "FAIL: esptool no disponible o versión no soportada" >&2
  exit 2
fi

echo "ADVERTENCIA: RECUPERACIÓN COMPLETA DE FLASH (DESTRUCTIVA)."
echo "Destino:     ${PORT} (chip forzado: esp32s3, flash 16MB)"
echo "Imagen:      ${BACKUP} (solo lectura; NO se copia)"
echo "Tamaño:      ${ACTUAL_SIZE} bytes"
echo "SHA-256:     ${ACTUAL_SHA256}"
echo "Consecuencias: NVS, calibraciones y datos persistentes SERÁN REEMPLAZADOS."
echo "esptool:     ${P2_ESPTOOL_LABEL} v${P2_ESPTOOL_VERSION}"
echo "Confirmado:  ${CONFIRM}"
echo

echo "Identidad y capacidad observadas (solo lectura):"
p2_esptool_run --chip esp32s3 --port "${PORT}" --baud 460800 chip-id || true
p2_esptool_run --chip esp32s3 --port "${PORT}" --baud 460800 flash-id || true
echo

p2_esptool_run \
  --chip esp32s3 \
  --port "${PORT}" \
  --baud 460800 \
  "${P2_WRITE_SUBCOMMAND}" \
  --flash-size 16MB \
  0x00000 "${BACKUP}"

if [[ "$(sha256sum "${BACKUP}" | awk '{print $1}')" != "${ACTUAL_SHA256}" ]]; then
  echo "FAIL: la imagen original cambió durante la operación" >&2
  exit 2
fi

echo "OK: recuperación completa finalizada. La imagen original permanece inmutable."
echo "No se declaró PASS: verifique AP, HTTP y evidencia real antes de cerrar P2."
