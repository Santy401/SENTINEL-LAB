#!/usr/bin/env bash
# Build, sign (free or paid Apple ID logged in with `xtool auth login`) and
# install SENTINEL-LAB on a USB-connected iPhone, from Linux, with xtool.
#
#   apps/ios/scripts/linux-install.sh                    # iPhone on this machine
#   apps/ios/scripts/linux-install.sh --via home-server  # iPhone on another host
#   apps/ios/scripts/linux-install.sh --build-only       # .ipa only, no device
#
# --via forwards that host's usbmuxd socket over SSH (no root needed here),
# so the iPhone can stay plugged into the other machine. The host must already
# be paired with the iPhone ("Trust this computer").
set -euo pipefail

T="${SENTINEL_IOS_TOOLCHAIN:-$HOME/.local/share/sentinel-ios-toolchain}"
export PATH="$T/bin:$T/swift/usr/bin:$PATH"
APP_DIR="$(cd "$(dirname "$0")/.." && pwd)"
VIA=""
BUILD_ONLY=0
CONFIG="release"
PORT=27015

while [[ $# -gt 0 ]]; do
  case "$1" in
    --via) VIA="${2:?}"; shift 2 ;;
    --build-only) BUILD_ONLY=1; shift ;;
    --debug) CONFIG="debug"; shift ;;
    -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

say() { printf '\033[1m==>\033[0m %s\n' "$*"; }
command -v xtool >/dev/null || { echo "FAIL: run scripts/linux-toolchain.sh first" >&2; exit 1; }
if xtool sdk status 2>&1 | grep -qi "not installed"; then
  echo "FAIL: Darwin SDK missing. Run scripts/linux-toolchain.sh --sdk auto" >&2
  exit 1
fi

cd "$APP_DIR"
if [[ "$BUILD_ONLY" == 1 ]]; then
  say "Building ${CONFIG} .ipa (unsigned)"
  xtool dev build --configuration "$CONFIG" --ipa
  ls -la xtool/*.ipa
  exit 0
fi

# `xtool auth status` prints the session token after "Logged in."; only test
# the first line and never echo it.
if [[ "$(xtool auth status 2>/dev/null | head -1)" != "Logged in." ]]; then
  echo "Not logged in to Apple Developer Services. Run once, interactively:" >&2
  echo "  xtool auth login --mode password" >&2
  exit 1
fi

TUNNEL_PID=""
cleanup() { [[ -n "$TUNNEL_PID" ]] && kill "$TUNNEL_PID" 2>/dev/null || true; }
trap cleanup EXIT
if [[ -n "$VIA" ]]; then
  say "Forwarding usbmuxd from ${VIA}"
  ssh -o ExitOnForwardFailure=yes -N -L "127.0.0.1:${PORT}:/var/run/usbmuxd" "$VIA" &
  TUNNEL_PID=$!
  sleep 2
  export USBMUXD_SOCKET_ADDRESS="127.0.0.1:${PORT}"
fi

say "Devices visible to xtool:"
xtool devices
UDID="$(xtool devices 2>/dev/null | sed -n 's/.*\[usb\]: \([0-9A-Fa-f-]\{20,\}\).*/\1/p' | head -1)"
[[ -n "$UDID" ]] || { echo "FAIL: no USB iPhone found (unlock it and tap Trust)" >&2; exit 1; }

say "Building, signing and installing (${CONFIG}) on ${UDID:0:8}…"
xtool dev run --configuration "$CONFIG" --udid "$UDID" --usb

say "Done. On the iPhone: Settings > General > VPN & Device Management > trust your"
say "developer certificate if asked, then open SENTINEL-LAB on the SENTINEL-LAB Wi-Fi."
