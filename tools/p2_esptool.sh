#!/usr/bin/env bash
# Shared esptool discovery for the guarded P2 write scripts.
# Source this file; it performs no device access by itself.
# Exports: P2_ESPTOOL_MODE, P2_ESPTOOL_CMD, P2_ESPTOOL_VERSION,
#          P2_ESPTOOL_LABEL, P2_WRITE_SUBCOMMAND.

P2_ESPTOOL_MODE=""
P2_ESPTOOL_CMD=()
P2_ESPTOOL_VERSION=""
P2_ESPTOOL_LABEL=""
P2_WRITE_SUBCOMMAND=""

p2_esptool_version_from_output() {
  printf '%s\n' "$1" | tr '\r' '\n' \
    | grep -oE 'esptool v?[0-9]+\.[0-9]+(\.[0-9]+)?' | head -1 \
    | sed -E 's/.*v//'
}

p2_esptool_run() {
  if [[ "${P2_ESPTOOL_MODE}" == "pio" ]]; then
    local arg quoted=""
    for arg in "$@"; do
      printf -v arg '%q' "${arg}"
      quoted+=" ${arg}"
    done
    pio pkg exec -p tool-esptool -c "esptool.py${quoted}"
  else
    "${P2_ESPTOOL_CMD[@]}" "$@"
  fi
}

_p2_esptool_try() {
  local label="$1" output version
  shift
  P2_ESPTOOL_MODE="direct"
  P2_ESPTOOL_CMD=("$@")
  output="$(p2_esptool_run version 2>&1 || true)"
  version="$(p2_esptool_version_from_output "${output}")"
  if [[ -z "${version}" ]]; then
    return 1
  fi
  P2_ESPTOOL_VERSION="${version}"
  P2_ESPTOOL_LABEL="${label}"
  return 0
}

p2_esptool_detect() {
  P2_ESPTOOL_MODE=""
  P2_ESPTOOL_CMD=()
  P2_ESPTOOL_VERSION=""
  P2_ESPTOOL_LABEL=""
  P2_WRITE_SUBCOMMAND=""

  if command -v esptool >/dev/null 2>&1; then
    if _p2_esptool_try "esptool (PATH)" esptool; then return 0; fi
  fi
  if command -v esptool.py >/dev/null 2>&1; then
    if _p2_esptool_try "esptool.py (PATH, obsoleto)" esptool.py; then return 0; fi
  fi
  if command -v python3 >/dev/null 2>&1; then
    if _p2_esptool_try "python3 -m esptool" python3 -m esptool; then return 0; fi
  fi
  if command -v pio >/dev/null 2>&1; then
    P2_ESPTOOL_MODE="pio"
    P2_ESPTOOL_CMD=()
    local output version
    output="$(p2_esptool_run version 2>&1 || true)"
    version="$(p2_esptool_version_from_output "${output}")"
    if [[ -n "${version}" ]]; then
      P2_ESPTOOL_VERSION="${version}"
      P2_ESPTOOL_LABEL="PlatformIO tool-esptool (sin fijar versión)"
      return 0
    fi
  fi
  return 1
}

p2_esptool_validate() {
  local major minor patch
  IFS='.' read -r major minor patch <<< "${P2_ESPTOOL_VERSION}"
  if [[ ! "${major}" =~ ^[0-9]+$ ]] || (( major < 4 )); then
    echo "FAIL: versión de esptool no soportada: ${P2_ESPTOOL_VERSION}" >&2
    return 1
  fi
  if (( major >= 5 )); then
    P2_WRITE_SUBCOMMAND="write-flash"
  else
    P2_WRITE_SUBCOMMAND="write_flash"
  fi
  return 0
}
