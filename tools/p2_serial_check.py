#!/usr/bin/env python3
"""Read-only serial-port identification for SENTINEL-LAB hardware.

This script does not open the port, send bytes, reset the device, or flash.
It reports only the properties needed to decide whether an authorized write
operation may be attempted from the Arch Linux computer.
"""

from __future__ import annotations

import argparse
import grp
import os
import pwd
import re
import shutil
import stat
import subprocess
import sys
from pathlib import Path


def run(command: list[str], timeout: float = 5.0) -> tuple[int, str, str] | None:
    if not shutil.which(command[0]):
        return None
    try:
        completed = subprocess.run(
            command, capture_output=True, text=True, timeout=timeout, check=False
        )
    except (OSError, subprocess.SubprocessError):
        return None
    return completed.returncode, completed.stdout, completed.stderr


def usb_ids_from_lsusb(expected: tuple[str, str]) -> bool | None:
    result = run(["lsusb"])
    if result is None:
        return None
    _, stdout, _ = result
    pairs = {(vid.lower(), pid.lower())
             for vid, pid in re.findall(r"ID\s+([0-9a-fA-F]{4}):([0-9a-fA-F]{4})", stdout)}
    return expected in pairs


def udev_ids(port: Path) -> dict[str, str]:
    result = run(["udevadm", "info", "--query=property", f"--name={port}"])
    if result is None:
        return {}
    _, stdout, _ = result
    wanted = {"ID_VENDOR_ID", "ID_MODEL_ID", "ID_USB_DRIVER"}
    output: dict[str, str] = {}
    for line in stdout.splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        if key in wanted:
            output[key] = value
    return output


def group_names() -> list[str]:
    names = set()
    for gid in os.getgroups():
        try:
            names.add(grp.getgrgid(gid).gr_name)
        except KeyError:
            continue
    return sorted(names)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=Path, default=Path("/dev/ttyACM0"))
    parser.add_argument("--expected-vid-pid", default="1A86:55D3")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    port = args.port
    try:
        expected_vid, expected_pid = (part.lower() for part in args.expected_vid_pid.split(":", 1))
    except ValueError:
        print("ERROR: --expected-vid-pid debe tener el formato VID:PID")
        return 2

    failures = 0
    if not port.exists():
        print(f"FAIL: {port} no existe en este equipo")
        return 1
    info = port.stat()
    if not stat.S_ISCHR(info.st_mode):
        print(f"FAIL: {port} no es un dispositivo de carácter")
        return 1

    owner = pwd.getpwuid(info.st_uid).pw_name
    group = grp.getgrgid(info.st_gid).gr_name
    mode = stat.S_IMODE(info.st_mode)
    print(f"Puerto:      {port}")
    print(f"Tipo:        dispositivo de carácter ({os.major(info.st_rdev)}:{os.minor(info.st_rdev)})")
    print(f"Permisos:    {owner}:{group} {mode:04o}")
    print(f"Grupos:      {', '.join(group_names())}")

    if os.access(port, os.R_OK | os.W_OK):
        print("Acceso RW:   OK")
    else:
        print("Acceso RW:   FAIL; pertenezca a uucp/dialout o use la sesión del grupo correcto")
        failures += 1

    usb_match = usb_ids_from_lsusb((expected_vid, expected_pid))
    if usb_match is True:
        print(f"USB:         VID:PID {expected_vid}:{expected_pid} observado")
    elif usb_match is False:
        print(f"USB:         FAIL; no se observó {expected_vid}:{expected_pid}")
        failures += 1
    else:
        print("USB:         NOT_VERIFIED; lsusb no disponible")

    ids = udev_ids(port)
    if ids:
        print("udev:        " + ", ".join(f"{key}={value}" for key, value in sorted(ids.items())))
        vid = ids.get("ID_VENDOR_ID", "").lower()
        pid = ids.get("ID_MODEL_ID", "").lower()
        if (vid or pid) and (vid != expected_vid or pid != expected_pid):
            print("udev:        FAIL; VID:PID no coincide con el esperado")
            failures += 1
    else:
        print("udev:        NOT_VERIFIED; udevadm no disponible o sin datos para el puerto")

    print()
    print("Solo lectura: el puerto NO se ha abierto y NO se han enviado bytes.")
    print("Si necesita ver el monitor, ejecute manualmente:")
    print(f"  pio device monitor -p {port} -b 115200")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
