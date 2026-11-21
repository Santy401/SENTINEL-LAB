#!/usr/bin/env python3
"""Read-only preflight for the portable P2 hardware kit.

The script never opens the serial port, flashes firmware, changes Wi-Fi state,
or writes to the device. It only inspects local artifacts and performs passive
network observations.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import stat
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import hw_common as hw  # noqa: E402

EXPECTED_SSID = "SENTINEL-LAB"
EXPECTED_VID_PID = ("1a86", "55d3")
REQUIRED_TOOLS = ("sha256sum", "curl")
OPTIONAL_TOOLS = ("pio", "nmcli", "lsusb")
FORBIDDEN_NAMES = {"secrets.h", ".env"}
FORBIDDEN_SUFFIXES = (".pem", ".key", ".p12")


class Report:
    def __init__(self) -> None:
        self.failures: list[str] = []
        self.warnings: list[str] = []
        self.checks: list[str] = []

    def ok(self, message: str) -> None:
        self.checks.append(f"OK   {message}")

    def warn(self, message: str) -> None:
        self.warnings.append(message)
        self.checks.append(f"WARN {message}")

    def fail(self, message: str) -> None:
        self.failures.append(message)
        self.checks.append(f"FAIL {message}")

    def emit(self) -> None:
        for line in self.checks:
            print(line)
        print()
        print(f"Resumen: {len(self.checks) - len(self.warnings) - len(self.failures)} OK, "
              f"{len(self.warnings)} advertencias, {len(self.failures)} fallos")
        if self.failures:
            print("El preflight NO autoriza continuar. Corrija los fallos antes de cualquier prueba física.")
        else:
            print("Preflight solo lectura completado. No se ha escrito en el dispositivo.")


def sha256_file(path: Path) -> str:
    digest = hw.sha256_file(path)
    return digest


def verify_sha256sums(root: Path, manifest: Path, report: Report) -> None:
    lines = manifest.read_text(encoding="utf-8").splitlines()
    checked = 0
    for number, line in enumerate(lines, 1):
        text = line.strip()
        if not text or text.startswith("#"):
            continue
        parts = text.split(None, 1)
        if len(parts) != 2:
            report.fail(f"{manifest.name}:{number} formato inválido")
            continue
        expected, relative = parts
        target = root / relative.strip().lstrip("*")
        if not target.is_file():
            report.fail(f"{manifest.name}: falta {relative}")
            continue
        actual = sha256_file(target)
        if actual.lower() != expected.lower():
            report.fail(f"{manifest.name}: hash distinto para {relative}")
            continue
        checked += 1
    if checked:
        report.ok(f"SHA-256 verificados: {checked} archivos desde {manifest.name}")


def artifact_map(root: Path, backup: Path) -> dict[str, Path]:
    kit_v031 = root / "firmware" / "v0.3.1"
    kit_v040 = root / "firmware" / "v0.4.0"
    if (kit_v031 / "firmware.bin").is_file() and (kit_v040 / "firmware.bin").is_file():
        images = {
            "v0.3.1 bootloader": kit_v031 / "bootloader.bin",
            "v0.3.1 partitions": kit_v031 / "partitions.bin",
            "v0.3.1 boot_app0": kit_v031 / "boot_app0.bin",
            "v0.3.1 firmware": kit_v031 / "firmware.bin",
            "v0.4.0 bootloader": kit_v040 / "bootloader.bin",
            "v0.4.0 partitions": kit_v040 / "partitions.bin",
            "v0.4.0 boot_app0": kit_v040 / "boot_app0.bin",
            "v0.4.0 firmware": kit_v040 / "firmware.bin",
        }
    else:
        build = root / ".pio" / "build" / "sentinel_esp32s3_n16r8_realtime"
        images = {
            "v0.4.0 bootloader": build / "bootloader.bin",
            "v0.4.0 partitions": build / "partitions.bin",
            "v0.4.0 firmware": build / "firmware.bin",
            "v0.3.1 bootloader": backup / "bootloader.bin",
            "v0.3.1 partitions": backup / "partitions.bin",
            "v0.3.1 boot_app0": backup / "boot_app0.bin",
            "v0.3.1 firmware": backup / "firmware.bin",
        }
    return images


def check_artifacts(images: dict[str, Path], report: Report) -> None:
    missing = [name for name, path in images.items() if not path.is_file() or path.stat().st_size == 0]
    if missing:
        report.fail("artefactos ausentes o vacíos: " + ", ".join(sorted(missing)))
        return
    report.ok(f"artefactos de firmware presentes: {len(images)}")


def check_secret_files(root: Path, report: Report, strict: bool) -> None:
    skip = {".git", ".pio", "node_modules", "build", "evidence", "__pycache__"}
    found: list[str] = []
    for path in root.rglob("*"):
        if not path.is_file():
            continue
        if any(part in skip for part in path.parts):
            continue
        if path.name in FORBIDDEN_NAMES or path.name.lower().endswith(FORBIDDEN_SUFFIXES):
            found.append(path.relative_to(root).as_posix())
    if not found:
        report.ok("sin secrets.h, .env ni material de clave en el kit")
    elif strict:
        report.fail("archivos privados en el kit: " + ", ".join(sorted(found)))
    else:
        report.warn("archivos privados locales ignorados por Git: " + ", ".join(sorted(found)))


def check_tools(report: Report) -> None:
    for name in REQUIRED_TOOLS:
        if shutil.which(name):
            report.ok(f"herramienta disponible: {name}")
        else:
            report.fail(f"herramienta requerida no encontrada: {name}")
    for name in OPTIONAL_TOOLS:
        if shutil.which(name):
            report.ok(f"herramienta opcional disponible: {name}")
        else:
            report.warn(f"herramienta opcional no encontrada: {name}")


def check_serial(port: Path, report: Report) -> None:
    if not port.exists():
        report.warn(f"puerto serie no presente: {port}")
        return
    mode = stat.S_IMODE(port.stat().st_mode)
    if not stat.S_ISCHR(port.stat().st_mode):
        report.warn(f"{port} no es un dispositivo de carácter")
        return
    if os.access(port, os.R_OK | os.W_OK):
        report.ok(f"puerto serie accesible: {port} (modo {mode:04o})")
    else:
        report.warn(f"puerto serie sin acceso de lectura/escritura: {port} (modo {mode:04o})")

    executable = shutil.which("lsusb")
    if not executable:
        report.warn("lsusb no disponible; no se pudo confirmar VID:PID del adaptador")
        return
    try:
        output = subprocess.run(
            [executable], capture_output=True, text=True, timeout=5, check=False
        ).stdout
    except (OSError, subprocess.SubprocessError):
        report.warn("lsusb no pudo ejecutarse")
        return
    ids = set(re.findall(r"ID\s+([0-9a-fA-F]{4}):([0-9a-fA-F]{4})", output))
    normalized = {(low.lower(), pid.lower()) for low, pid in ids}
    if EXPECTED_VID_PID in normalized:
        report.ok("adaptador USB-serie esperado observado: " + ":".join(EXPECTED_VID_PID))
    else:
        report.warn("no se observó el VID:PID esperado; verifique el cable y el puerto en el PC de Arch")


def check_wifi(host: str, report: Report) -> None:
    try:
        info = hw.wifi_info()
    except Exception as exc:  # passive check must never abort preflight
        report.warn(f"Wi-Fi pasivo no disponible: {type(exc).__name__}")
        return
    if not info.get("available"):
        report.warn("nmcli no disponible; la visibilidad del AP queda NOT_VERIFIED")
        return
    ssids = {item.get("ssid") for item in info.get("networks", [])}
    if EXPECTED_SSID in ssids:
        report.ok(f"AP observado pasivamente: {EXPECTED_SSID}")
    else:
        report.warn(f"AP {EXPECTED_SSID} no observado; conéctese al AP antes de la validación")


def check_http(host: str, expected_fw: str, report: Report) -> None:
    try:
        observation = hw.http_get(host, "/api/state", timeout=3.0)
    except Exception as exc:
        report.warn(f"HTTP /api/state no disponible: {type(exc).__name__}")
        return
    status = observation.get("status")
    if status != 200:
        report.warn(f"HTTP /api/state respondió {status or 'sin respuesta'}")
        return
    body = observation.get("bodyJson")
    fw = body.get("fw") if isinstance(body, dict) else None
    if fw and fw != expected_fw:
        report.warn(f"HTTP /api/state informa fw={fw}; se esperaba {expected_fw}")
        return
    report.ok(f"HTTP /api/state 200 (fw={fw or 'no declarado'})")


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1],
                        help="kit root or repository root")
    parser.add_argument("--backup", type=Path,
                        default=Path("/home/santy401/.sentinel-backups/v0.3.1-rf-20261121T194718Z"),
                        help="verified v0.3.1 backup used only in repository mode")
    parser.add_argument("--port", type=Path, default=Path("/dev/ttyACM0"),
                        help="serial port to inspect without opening it")
    parser.add_argument("--host", default="192.168.4.1", help="lab AP address")
    parser.add_argument("--expected-fw", default="0.3.1", help="expected HTTP firmware version")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    root = args.root.resolve()
    report = Report()

    root_manifest = root / "SHA256SUMS"
    kit_mode = root_manifest.is_file()
    if kit_mode:
        verify_sha256sums(root, root_manifest, report)
    else:
        report.warn("sin SHA256SUMS en la raíz: modo repositorio, no modo kit")

    for manifest in sorted((root / "firmware").glob("*/SHA256SUMS")) if (root / "firmware").is_dir() else []:
        verify_sha256sums(manifest.parent, manifest, report)

    check_artifacts(artifact_map(root, args.backup.resolve()), report)
    check_secret_files(root, report, strict=kit_mode)
    check_tools(report)
    check_serial(args.port, report)
    check_wifi(args.host, report)
    check_http(args.host, args.expected_fw, report)
    report.emit()
    return 1 if report.failures else 0


if __name__ == "__main__":
    sys.exit(main())
