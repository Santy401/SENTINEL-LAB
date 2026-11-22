#!/usr/bin/env python3
"""Stage a realtime firmware release into a P2 hardware kit (read-only on the device).

Copies the four PlatformIO artifacts to <kit>/firmware/<release>/, writes
SHA256SUMS and refuses to stage when the bootloader, partition table or
boot_app0 differ from the v0.3.1 set already in the kit: an unchanged layout
is what guarantees that writing these regions keeps NVS intact.

The staged firmware.bin embeds the AP password and the realtime bearer token
from include/secrets.h. Never publish it, attach it to a PR or upload it.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ENVIRONMENT = "sentinel_esp32s3_n16r8_realtime"
FIXED = ("bootloader.bin", "partitions.bin", "boot_app0.bin")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kit", type=Path, default=Path.home() / "p2-hardware-kit")
    parser.add_argument("--release", required=True, help="e.g. v0.4.1")
    parser.add_argument("--build-dir", type=Path,
                        default=ROOT / ".pio" / "build" / ENVIRONMENT)
    args = parser.parse_args(argv)
    if not re.fullmatch(r"v\d+\.\d+\.\d+", args.release):
        print("FAIL: --release must look like v0.4.1", file=sys.stderr)
        return 2

    reference = args.kit / "firmware" / "v0.3.1"
    sources = {
        "bootloader.bin": args.build_dir / "bootloader.bin",
        "partitions.bin": args.build_dir / "partitions.bin",
        "boot_app0.bin": reference / "boot_app0.bin",
        "firmware.bin": args.build_dir / "firmware.bin",
    }
    for name, path in sources.items():
        if not path.is_file() or path.stat().st_size == 0:
            print(f"FAIL: missing artifact {name}: {path}", file=sys.stderr)
            return 2
    for name in FIXED:
        expected = reference / name
        if not expected.is_file():
            print(f"FAIL: reference v0.3.1 {name} missing in kit", file=sys.stderr)
            return 2
        if sha256(sources[name]) != sha256(expected):
            print(f"FAIL: {name} differs from v0.3.1; layout change needs review",
                  file=sys.stderr)
            return 1
        print(f"OK   {name} identical to v0.3.1 ({sha256(expected)[:16]}…)")

    destination = args.kit / "firmware" / args.release
    destination.mkdir(parents=True, exist_ok=True)
    lines = []
    for name, path in sources.items():
        target = destination / name
        shutil.copyfile(path, target)
        target.chmod(0o600)
        lines.append(f"{sha256(target)}  {name}")
    (destination / "SHA256SUMS").write_text("\n".join(lines) + "\n")
    (destination / "SENSITIVE.txt").write_text(
        "firmware.bin embeds local credentials (AP password, bearer token).\n"
        "Do not publish, upload or attach it anywhere.\n")
    print(f"OK   staged {args.release} at {destination}")
    print(f"     firmware.bin sha256 {sha256(destination / 'firmware.bin')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
