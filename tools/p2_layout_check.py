#!/usr/bin/env python3
"""Read-only flash layout verifier for the P2 firmware sets.

Parses the compiled partition tables, computes the exact regions written by
the guarded flash script and proves that NVS is not overlapped. It never opens
the serial port and never writes to any file unless --save-json is supplied.
"""

from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

PARTITION_MAGIC = 0x50AA
END_MARKERS = (0xFFFF, 0xAA55, 0xEBEB)  # 0xEBEB marks the optional MD5 entry
EXPECTED_PARTITIONS: dict[str, tuple[int, int, int, int]] = {
    "nvs": (1, 2, 0x9000, 0x5000),
    "otadata": (1, 0, 0xE000, 0x2000),
    "app0": (0, 16, 0x10000, 0x640000),
    "app1": (0, 17, 0x650000, 0x640000),
    "spiffs": (1, 130, 0xC90000, 0x360000),
    "coredump": (1, 3, 0xFF0000, 0x10000),
}
WRITE_OFFSETS = {
    "bootloader.bin": 0x00000,
    "partitions.bin": 0x08000,
    "boot_app0.bin": 0x0E000,
    "firmware.bin": 0x10000,
}
BINARY_NAMES = tuple(WRITE_OFFSETS)


def parse_partition_table(path: Path) -> list[dict[str, Any]]:
    data = path.read_bytes()
    if len(data) < 32 or len(data) % 32 != 0:
        raise ValueError(f"partition table has invalid size: {len(data)}")
    entries: list[dict[str, Any]] = []
    for position in range(0, len(data), 32):
        raw = data[position:position + 32]
        magic = int.from_bytes(raw[0:2], "little")
        if magic in END_MARKERS:
            break
        if magic != PARTITION_MAGIC:
            raise ValueError(f"bad partition magic at {position:#x}: {magic:#x}")
        entries.append({
            "name": raw[12:28].split(b"\0", 1)[0].decode("ascii", errors="replace"),
            "type": raw[2],
            "subtype": raw[3],
            "offset": int.from_bytes(raw[4:8], "little"),
            "size": int.from_bytes(raw[8:12], "little"),
        })
    return entries


def overlaps(a_start: int, a_end: int, b_start: int, b_end: int) -> bool:
    return a_start < b_end and b_start < a_end


class Checks:
    def __init__(self) -> None:
        self.items: list[dict[str, Any]] = []

    def add(self, identifier: str, passed: bool, detail: str, **observed: Any) -> None:
        self.items.append({
            "id": identifier,
            "status": "PASS" if passed else "FAIL",
            "detail": detail,
            "observed": observed,
        })

    @property
    def failures(self) -> int:
        return sum(item["status"] == "FAIL" for item in self.items)


def verify_set(root: Path, label: str, checks: Checks) -> dict[str, Any]:
    binary_paths = {name: root / name for name in BINARY_NAMES}
    missing = [name for name, path in binary_paths.items()
               if not path.is_file() or path.stat().st_size == 0]
    checks.add(f"{label}.artifacts", not missing,
               "all four binaries present and non-empty" if not missing
               else "missing: " + ", ".join(missing))
    if missing:
        return {}

    entries = parse_partition_table(binary_paths["partitions.bin"])
    by_name = {entry["name"]: entry for entry in entries}
    actual = {name: (item["type"], item["subtype"], item["offset"], item["size"])
              for name, item in by_name.items()}
    checks.add(f"{label}.partition_table", actual == EXPECTED_PARTITIONS,
               "partition table matches default_16MB.csv"
               if actual == EXPECTED_PARTITIONS else "partition table differs from expected",
               entries=entries)

    nvs = by_name.get("nvs")
    otadata = by_name.get("otadata")
    app0 = by_name.get("app0")
    if not nvs or not otadata or not app0:
        checks.add(f"{label}.required_partitions", False, "nvs/otadata/app0 missing")
        return {}

    nvs_start, nvs_end = nvs["offset"], nvs["offset"] + nvs["size"]
    ota_start, ota_end = otadata["offset"], otadata["offset"] + otadata["size"]

    regions: dict[str, dict[str, int]] = {}
    for name, path in binary_paths.items():
        start = WRITE_OFFSETS[name]
        end = start + path.stat().st_size
        regions[name] = {"start": start, "end": end, "bytes": path.stat().st_size}

    nvs_hits = [name for name, region in regions.items()
                if overlaps(region["start"], region["end"], nvs_start, nvs_end)]
    checks.add(f"{label}.nvs_not_overlapped", not nvs_hits,
               "no written region overlaps NVS"
               if not nvs_hits else "NVS overlap: " + ", ".join(nvs_hits),
               nvsRange={"start": nvs_start, "end": nvs_end}, regions=regions)

    ota_hits = [name for name, region in regions.items()
                if overlaps(region["start"], region["end"], ota_start, ota_end)]
    expected_ota_hits = ota_hits == ["boot_app0.bin"]
    checks.add(f"{label}.otadata_only_boot_app0", expected_ota_hits,
               "boot_app0.bin initializes otadata and no other image touches it"
               if expected_ota_hits else "unexpected regions overlap otadata: " + ", ".join(ota_hits),
               otadataRange={"start": ota_start, "end": ota_end}, overlaps=ota_hits)

    checks.add(f"{label}.boot_app0_size",
               regions["boot_app0.bin"]["bytes"] == otadata["size"],
               "boot_app0.bin exactly fills the otadata partition",
               bytes=regions["boot_app0.bin"]["bytes"], otadataBytes=otadata["size"])

    checks.add(f"{label}.app0_offset",
               WRITE_OFFSETS["firmware.bin"] == app0["offset"],
               "firmware.bin is written at the app0 offset",
               writeOffset=WRITE_OFFSETS["firmware.bin"], app0Offset=app0["offset"])

    checks.add(f"{label}.app0_capacity",
               regions["firmware.bin"]["bytes"] <= app0["size"],
               "firmware.bin fits inside app0",
               bytes=regions["firmware.bin"]["bytes"], app0Bytes=app0["size"])

    checks.add(f"{label}.bootloader_before_partitions",
               regions["bootloader.bin"]["end"] <= WRITE_OFFSETS["partitions.bin"],
               "bootloader ends before the partition table",
               bootloaderEnd=regions["bootloader.bin"]["end"])

    checks.add(f"{label}.partitions_before_nvs",
               regions["partitions.bin"]["end"] <= nvs_start,
               "partition table ends before NVS",
               partitionsEnd=regions["partitions.bin"]["end"], nvsStart=nvs_start)

    header_ok = True
    header_detail: dict[str, dict[str, Any]] = {}
    for name in ("bootloader.bin", "firmware.bin"):
        head = binary_paths[name].read_bytes()[:4]
        magic = head[0]
        segments = head[1]
        ok = magic == 0xE9 and segments > 0
        header_ok = header_ok and ok
        header_detail[name] = {"magic": hex(magic), "segments": segments, "ok": ok}
    checks.add(f"{label}.image_headers", header_ok,
               "bootloader and application have valid ESP image headers",
               headers=header_detail)

    return {"partitions": entries, "regions": regions,
            "nvsRange": {"start": nvs_start, "end": nvs_end}}


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kit", type=Path, default=Path(__file__).resolve().parents[1],
                        help="kit root (defaults to the parent of tools/)")
    parser.add_argument("--save-json", type=Path, help="optional host-side evidence file")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    kit = args.kit.resolve()
    checks = Checks()
    result: dict[str, Any] = {}
    for label, version in (("v031", "v0.3.1"), ("v040", "v0.4.0")):
        result[version] = verify_set(kit / "firmware" / version, label, checks)

    for item in checks.items:
        print(f"{item['status']:4} {item['id']:42} {item['detail']}")

    same_table = result.get("v0.3.1", {}).get("partitions") == result.get("v0.4.0", {}).get("partitions")
    checks.add("sets.partition_tables_identical", same_table,
               "both firmware sets use the same partition table")
    print(f"{checks.items[-1]['status']:4} {checks.items[-1]['id']:42} {checks.items[-1]['detail']}")

    if args.save_json:
        payload = {
            "capturedAt": datetime.now(timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z"),
            "kit": ".",
            "checks": checks.items,
            "sets": result,
        }
        args.save_json.parent.mkdir(parents=True, exist_ok=True)
        args.save_json.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n",
                                  encoding="utf-8")
        print(f"Evidencia guardada en {args.save_json}")

    print()
    print(f"Resultado: {len(checks.items) - checks.failures} PASS, {checks.failures} FAIL")
    if checks.failures:
        print("FAIL: no continuar con ninguna escritura.")
    else:
        print("OK: NVS queda fuera de todas las regiones de escritura documentadas.")
    return 1 if checks.failures else 0


if __name__ == "__main__":
    sys.exit(main())
