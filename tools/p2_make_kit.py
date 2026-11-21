#!/usr/bin/env python3
"""Assemble a portable, read-only-first P2 hardware validation kit.

Only allowlisted binaries, scripts, and documents are copied. Credentials and
private device logs are never included. Root SHA256SUMS is generated last.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import re
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
DEFAULT_BACKUP = Path("/home/santy401/.sentinel-backups/v0.3.1-rf-20261121T194718Z")
DEFAULT_BOOT_APP0 = (
    Path.home() / ".platformio" / "packages" / "framework-arduinoespressif32"
    / "tools" / "partitions" / "boot_app0.bin"
)
BUILD_ENV = "sentinel_esp32s3_n16r8_realtime"
BINARIES = ("bootloader.bin", "partitions.bin", "boot_app0.bin", "firmware.bin")
TOOLS = (
    "hw_common.py", "hw_capture.py", "hw_report.py", "p2_preflight.py",
    "p2_serial_check.py", "p2_http_check.py", "p2_ws_probe.py",
    "p2_flash_authorized.sh",
)
DOCS = (
    "hardware/p2-kit.md",
    "hardware/recovery.md",
    "hardware/v031-backup-inventory.md",
    "validation/p2-hardware.md",
    "validation/p2-software.md",
    "validation/p2-physical-matrix.md",
    "validation/p2-build-record.md",
    "realtime-core.md",
)
EVIDENCE_DIRS = ("serial", "http", "ws", "heap", "results", "versions", "checksums", "incidents", "final")
FORBIDDEN_NAMES = {"secrets.h", ".env"}
FORBIDDEN_SUFFIXES = (".pem", ".key", ".p12")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_sha256sums(path: Path) -> dict[str, str]:
    entries: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        text = line.strip()
        if not text or text.startswith("#"):
            continue
        parts = text.split(None, 1)
        if len(parts) != 2:
            raise ValueError(f"invalid SHA256SUMS line in {path}")
        entries[parts[1].strip().lstrip("*")] = parts[0].lower()
    return entries


def verify_directory(root: Path, manifest: Path) -> dict[str, str]:
    entries = parse_sha256sums(manifest)
    for name, expected in entries.items():
        target = root / name
        if not target.is_file():
            raise FileNotFoundError(target)
        if sha256(target) != expected:
            raise ValueError(f"hash mismatch: {name}")
    return entries


def command_text(command: list[str]) -> str:
    try:
        completed = subprocess.run(command, capture_output=True, text=True, check=False, timeout=20)
    except (OSError, subprocess.SubprocessError):
        return "NOT_AVAILABLE"
    text = (completed.stdout or completed.stderr).strip().splitlines()
    return text[0] if text else "NOT_AVAILABLE"


def parse_build_log(log: Path) -> dict[str, int]:
    text = log.read_text(encoding="utf-8", errors="replace")
    ram = re.search(r"RAM:\s+.*?used\s+(\d+)\s+bytes from\s+(\d+)", text)
    flash = re.search(r"Flash:\s+.*?used\s+(\d+)\s+bytes from\s+(\d+)", text)
    if not ram or not flash:
        raise ValueError(f"build log lacks RAM/Flash lines: {log}")
    return {
        "ramUsedBytes": int(ram.group(1)),
        "ramTotalBytes": int(ram.group(2)),
        "flashUsedBytes": int(flash.group(1)),
        "flashTotalBytes": int(flash.group(2)),
    }


def copy_file(source: Path, destination: Path) -> None:
    if not source.is_file():
        raise FileNotFoundError(source)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def write_sha256sums(root: Path) -> int:
    lines: list[str] = []
    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.name == "SHA256SUMS":
            continue
        relative = path.relative_to(root).as_posix()
        lines.append(f"{sha256(path)}  {relative}\n")
    (root / "SHA256SUMS").write_text("".join(lines), encoding="utf-8")
    return len(lines)


def ensure_no_secrets(root: Path) -> None:
    found: list[str] = []
    for path in root.rglob("*"):
        if path.name in FORBIDDEN_NAMES or path.name.lower().endswith(FORBIDDEN_SUFFIXES):
            found.append(path.relative_to(root).as_posix())
    if found:
        raise RuntimeError("forbidden files in kit: " + ", ".join(sorted(found)))


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", type=Path, required=True,
                        help="new or empty destination directory for the kit")
    parser.add_argument("--backup", type=Path, default=DEFAULT_BACKUP)
    parser.add_argument("--boot-app0", type=Path, default=DEFAULT_BOOT_APP0)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    destination = args.destination.resolve()
    backup = args.backup.resolve()
    boot_app0 = args.boot_app0.resolve()

    if destination.exists() and any(destination.iterdir()):
        print(f"FAIL: destination is not empty: {destination}", file=sys.stderr)
        return 2

    build_dir = REPO / ".pio" / "build" / BUILD_ENV
    build_log = REPO / "build" / f"build-{BUILD_ENV}.log"
    v040_sources = {
        "bootloader.bin": build_dir / "bootloader.bin",
        "partitions.bin": build_dir / "partitions.bin",
        "boot_app0.bin": boot_app0,
        "firmware.bin": build_dir / "firmware.bin",
    }
    for name, path in v040_sources.items():
        if not path.is_file() or path.stat().st_size == 0:
            print(f"FAIL: missing v0.4.0 artifact {name}: {path}", file=sys.stderr)
            return 2
    if not backup.is_dir() or not (backup / "SHA256SUMS").is_file():
        print(f"FAIL: invalid v0.3.1 backup: {backup}", file=sys.stderr)
        return 2
    backup_hashes = verify_directory(backup, backup / "SHA256SUMS")
    if not build_log.is_file():
        print(f"FAIL: missing build log: {build_log}", file=sys.stderr)
        return 2

    destination.mkdir(parents=True, exist_ok=True)

    backup_dir = destination / "firmware" / "v0.3.1"
    for name in (*BINARIES, "SHA256SUMS", "MANIFEST.md"):
        copy_file(backup / name, backup_dir / name)

    release_dir = destination / "firmware" / "v0.4.0"
    for name in BINARIES:
        copy_file(v040_sources[name], release_dir / name)
    release_hashes = {name: sha256(release_dir / name) for name in BINARIES}
    (release_dir / "SHA256SUMS").write_text(
        "".join(f"{release_hashes[name]}  {name}\n" for name in BINARIES), encoding="utf-8"
    )

    for name in TOOLS:
        source = REPO / "tools" / name
        copy_file(source, destination / "tools" / name)
    for relative in DOCS:
        source = REPO / "docs" / relative
        destination_name = "README.md" if relative == "hardware/p2-kit.md" else relative
        copy_file(source, destination / "docs" / destination_name)

    copy_file(build_log, destination / "build" / f"{BUILD_ENV}.log")

    evidence_root = destination / "evidence"
    for name in EVIDENCE_DIRS:
        folder = evidence_root / name
        folder.mkdir(parents=True, exist_ok=True)
        (folder / "README.md").write_text(
            f"# Evidencia · {name}\n\n"
            "Directorio preparado para evidencia real. No se han guardado capturas sintéticas.\n",
            encoding="utf-8",
        )

    commit = command_text(["git", "-C", str(REPO), "rev-parse", "HEAD"])
    sizes = parse_build_log(build_log)
    build_record = {
        "generatedAt": datetime.now(timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z"),
        "gitCommit": commit,
        "environment": BUILD_ENV,
        "platformio": command_text(["pio", "--version"]),
        "python": platform.python_version(),
        "build": sizes,
        "artifacts": {
            name: {"sha256": release_hashes[name], "bytes": (release_dir / name).stat().st_size}
            for name in BINARIES
        },
        "note": "Static linker sizes are not runtime heap measurements.",
    }
    (destination / "build-record.json").write_text(
        json.dumps(build_record, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )

    manifest_lines = [
        "# Manifiesto del kit P2 para Arch Linux",
        "",
        f"- Generado UTC: `{build_record['generatedAt']}`",
        f"- Commit del repositorio: `{commit}`",
        f"- Entorno P2: `{BUILD_ENV}`",
        f"- Respaldo v0.3.1: `{backup}`",
        "- Credenciales incluidas: **ninguna**.",
        "- Estado: **IMPLEMENTED / SOFTWARE TESTED / HARDWARE VALIDATION PENDING**.",
        "",
        "## Artefactos v0.3.1",
        "",
    ]
    manifest_lines.extend(f"- `{name}`: `{backup_hashes[name]}`" for name in BINARIES)
    manifest_lines.extend(["", "## Artefactos v0.4.0", ""])
    manifest_lines.extend(f"- `{name}`: `{release_hashes[name]}`" for name in BINARIES)
    manifest_lines.extend([
        "",
        "## Verificación",
        "",
        "Desde la raíz del kit:",
        "",
        "```bash",
        "sha256sum -c SHA256SUMS",
        "python3 tools/p2_preflight.py --root .",
        "```",
        "",
        "La verificación completa de integridad se documenta en `docs/README.md`.",
    ])
    (destination / "KIT_MANIFEST.md").write_text("\n".join(manifest_lines) + "\n", encoding="utf-8")

    ensure_no_secrets(destination)
    count = write_sha256sums(destination)

    print(f"Kit creado: {destination}")
    print(f"Archivos verificables: {count}")
    print("Siguiente paso: transferir el directorio a Arch y ejecutar sha256sum -c SHA256SUMS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
