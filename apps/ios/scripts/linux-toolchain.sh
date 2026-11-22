#!/usr/bin/env bash
# Reproducible, sudo-free iOS toolchain for Arch Linux / Omarchy (xtool).
#
#   apps/ios/scripts/linux-toolchain.sh                     # Swift + xtool
#   apps/ios/scripts/linux-toolchain.sh --ubuntu-host HOST  # libxml2/ICU from an Ubuntu 24.04 host
#   apps/ios/scripts/linux-toolchain.sh --sdk ~/Descargas/Xcode_27.1.xip
#
# Installs into ~/.local/share/sentinel-ios-toolchain (override with
# SENTINEL_IOS_TOOLCHAIN). Every download is verified before use:
#   - Swift: swift.org GPG signature (Swift 6.x Release Signing Key).
#   - xtool: SHA-256 published by GitHub for the release asset (pinned here).
# Nothing remote is executed without that check. No Apple credentials are
# requested or stored by this script.
set -euo pipefail

SWIFT_VERSION="6.4.3"
XTOOL_VERSION="1.24.0"
XTOOL_SHA256="dbfd9d7216f077c84c171b688c4173c37c23ca76bf7fd185d1a9ae8a53dab59f"
SWIFT_KEY_FPR="52BB7E3DE28A71BE22EC05FFEF80A866B47A981F"

T="${SENTINEL_IOS_TOOLCHAIN:-$HOME/.local/share/sentinel-ios-toolchain}"
UBUNTU_HOST=""
XIP=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --ubuntu-host) UBUNTU_HOST="${2:?}"; shift 2 ;;
    --sdk) XIP="${2:?}"; shift 2 ;;
    -h|--help) sed -n '2,15p' "$0"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

say() { printf '\033[1m==>\033[0m %s\n' "$*"; }
mkdir -p "$T"/{dl,bin,lib}

# 1. Swift toolchain (swift.org Ubuntu 24.04 build; runs on Arch with two libs).
if [[ ! -x "$T/swift/usr/bin/swift-frontend" ]]; then
  base="https://download.swift.org/swift-${SWIFT_VERSION}-release/ubuntu2404/swift-${SWIFT_VERSION}-RELEASE/swift-${SWIFT_VERSION}-RELEASE-ubuntu24.04.tar.gz"
  say "Downloading Swift ${SWIFT_VERSION}"
  curl -fL --retry 3 -o "$T/dl/swift.tar.gz" "$base"
  curl -fsSL -o "$T/dl/swift.tar.gz.sig" "$base.sig"
  curl -fsSL -o "$T/dl/swift-keys.asc" https://www.swift.org/keys/all-keys.asc
  export GNUPGHOME="$T/gnupg"; mkdir -p "$GNUPGHOME"; chmod 700 "$GNUPGHOME"
  gpg -q --import "$T/dl/swift-keys.asc" 2>/dev/null
  if ! gpg --status-fd 1 --verify "$T/dl/swift.tar.gz.sig" "$T/dl/swift.tar.gz" 2>/dev/null \
      | grep -q "VALIDSIG ${SWIFT_KEY_FPR}"; then
    echo "FAIL: Swift tarball signature is not from the Swift 6.x release key" >&2
    exit 1
  fi
  say "Swift signature OK; extracting"
  rm -rf "$T/swift"; mkdir -p "$T/swift"
  tar -xzf "$T/dl/swift.tar.gz" -C "$T/swift" --strip-components=1
fi

# 2. xtool AppImage, extracted (Arch ships fuse3, the AppImage runtime wants fuse2).
if [[ ! -x "$T/xtool/AppRun" ]]; then
  say "Downloading xtool ${XTOOL_VERSION}"
  curl -fL --retry 3 -o "$T/dl/xtool.AppImage" \
    "https://github.com/xtool-org/xtool/releases/download/${XTOOL_VERSION}/xtool-x86_64.AppImage"
  echo "${XTOOL_SHA256}  $T/dl/xtool.AppImage" | sha256sum -c -
  chmod +x "$T/dl/xtool.AppImage"
  (cd "$T/dl" && rm -rf squashfs-root && ./xtool.AppImage --appimage-extract >/dev/null)
  rm -rf "$T/xtool"; mv "$T/dl/squashfs-root" "$T/xtool"
fi
printf '#!/usr/bin/env bash\nexec "%s/xtool/AppRun" "$@"\n' "$T" > "$T/bin/xtool"
chmod +x "$T/bin/xtool"
ln -sf "$T/swift/usr/bin/swift" "$T/bin/swift"

# 3. Libraries the Ubuntu-built toolchain expects but Arch names differently.
ln -sf /usr/lib/libncursesw.so.6 "$T/lib/libncurses.so.6"
if [[ ! -e "$T/lib/libxml2.so.2" ]]; then
  if [[ -e /usr/lib/libxml2.so.2 ]]; then
    ln -sf /usr/lib/libxml2.so.2 "$T/lib/libxml2.so.2"
  elif [[ -n "$UBUNTU_HOST" ]]; then
    say "Copying libxml2 2.9 and ICU 74 from ${UBUNTU_HOST} (Ubuntu 24.04)"
    src=$(ssh "$UBUNTU_HOST" 'readlink -f /usr/lib/x86_64-linux-gnu/libxml2.so.2')
    scp -q "$UBUNTU_HOST:$src" "$T/lib/"
    ln -sf "$(basename "$src")" "$T/lib/libxml2.so.2"
    for l in libicuuc.so.74 libicudata.so.74; do
      real=$(ssh "$UBUNTU_HOST" "readlink -f /usr/lib/x86_64-linux-gnu/$l")
      scp -q "$UBUNTU_HOST:$real" "$T/lib/"
      ln -sf "$(basename "$real")" "$T/lib/$l"
    done
  else
    echo "FAIL: libxml2.so.2 missing. Either install the AUR package 'libxml2-legacy'" >&2
    echo "      (yay -S libxml2-legacy) or pass --ubuntu-host <ubuntu-24.04-host>." >&2
    exit 1
  fi
fi

# 4. Point the toolchain at $T/lib with RUNPATH (SwiftPM scrubs LD_LIBRARY_PATH).
if [[ ! -x "$T/venv/bin/patchelf" ]]; then
  python3 -m venv "$T/venv"
  "$T/venv/bin/pip" install -q patchelf
fi
PE="$T/venv/bin/patchelf"
for f in "$T"/lib/libxml2.so.2.* "$T"/lib/libicuuc.so.74.*; do
  [[ -f "$f" ]] && "$PE" --set-rpath '$ORIGIN' "$f"
done
patched=0
while IFS= read -r f; do
  if ldd "$f" 2>/dev/null | grep -qE '(libxml2\.so\.2|libncurses\.so\.6|libicu(uc|data)\.so\.74) => not found'; then
    "$PE" --add-rpath "$T/lib" "$f"; patched=$((patched + 1))
  fi
done < <(find "$T/swift/usr/bin" "$T/swift/usr/lib" -type f \( -perm -u+x -o -name '*.so*' \) \
          ! -name 'lldb*' ! -name '*.py' 2>/dev/null)
say "Patched RUNPATH of ${patched} toolchain files"
missing=$(for b in swift-driver swift-frontend swift-build swift-package lld; do
  ldd "$T/swift/usr/bin/$b" 2>/dev/null | grep 'not found' || true; done)
if [[ -n "$missing" ]]; then echo "FAIL: unresolved libraries:"; echo "$missing"; exit 1; fi

export PATH="$T/bin:$PATH"
say "$(swift --version 2>&1 | head -1)"
say "xtool $(xtool --version 2>/dev/null || echo "${XTOOL_VERSION}")"

# 5. Darwin SDK from Apple's Xcode.xip (downloaded by you, signed by Apple).
if [[ -n "$XIP" ]]; then
  [[ -f "$XIP" ]] || { echo "FAIL: $XIP not found" >&2; exit 1; }
  say "Installing the Darwin SDK from $(basename "$XIP") (this takes a while)"
  xtool sdk install "$XIP"
fi
say "SDK: $(xtool sdk status 2>&1 | head -1)"

cat <<EOF

Add the toolchain to your shell (once):
  echo 'export PATH="$T/bin:\$PATH"' >> ~/.bashrc

Next steps (docs/ios/LINUX.md):
  1. Download Xcode 27.1 (.xip) at https://developer.apple.com/download/all/?q=Xcode
     and run: $0 --sdk /path/to/Xcode_27.1.xip
  2. Log in once (interactive; never pass the password as an argument):
     xtool auth login --mode password
  3. apps/ios/scripts/linux-install.sh [--via home-server]
EOF
