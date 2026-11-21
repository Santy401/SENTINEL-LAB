#!/usr/bin/env bash
# Copies the shared Protocol v1 examples and firmware-generated frames into the
# SentinelKit test resources so the Swift decoder is tested against the same
# fixtures as the Python validator and the firmware host tests.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
dest="$here/../SentinelKit/Tests/SentinelKitTests/Fixtures"
rm -rf "$dest"
mkdir -p "$dest/valid" "$dest/invalid" "$dest/firmware"
cp "$repo"/packages/protocol/examples/valid/*.json "$dest/valid/"
cp "$repo"/packages/protocol/examples/invalid/*.json "$dest/invalid/"
# build/firmware-*.json is produced by tools/check_software.py; it is optional.
if compgen -G "$repo/build/firmware-*.json" >/dev/null; then
  cp "$repo"/build/firmware-*.json "$dest/firmware/"
else
  echo "warning: build/firmware-*.json missing; run tools/check_software.py" >&2
fi
echo "fixtures: $(ls "$dest/valid" | wc -l) valid, $(ls "$dest/invalid" | wc -l) invalid, $(ls "$dest/firmware" | wc -l) firmware"
