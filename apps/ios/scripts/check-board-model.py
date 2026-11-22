#!/usr/bin/env python3
"""Verify that the iOS board model matches the web definition.

Source of truth: apps/web/src/lib/board-parts.ts (part ids, names, summaries,
details and BOARD_DIMENSIONS_MM). The iOS copy lives in
apps/ios/SentinelKit/Sources/SentinelKit/BoardModel.swift. Geometry is a
millimetre transcription of apps/web/src/three/board-model.ts; this script
checks the texts and the board dimensions exactly.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
WEB = ROOT / "apps/web/src/lib/board-parts.ts"
IOS = ROOT / "apps/ios/SentinelKit/Sources/SentinelKit/BoardModel.swift"


def strings(text: str) -> list[str]:
    return re.findall(r'"((?:[^"\\]|\\.)*)"', text)


def web_parts(src: str) -> list[tuple[str, str, str, list[str]]]:
    body = src.split("BOARD_PARTS", 1)[1].split("];", 1)[0]
    parts = []
    for block in re.findall(r"\{(.*?)\n  \}", body, re.S):
        pid = re.search(r'id:\s*"([^"]+)"', block).group(1)
        name = re.search(r'name:\s*"((?:[^"\\]|\\.)*)"', block).group(1)
        summary = re.search(r'summary:\s*"((?:[^"\\]|\\.)*)"', block).group(1)
        details = strings(re.search(r"details:\s*\[(.*?)\]", block, re.S).group(1))
        parts.append((pid, name, summary, details))
    return parts


def ios_parts(src: str) -> list[tuple[str, str, str, list[str]]]:
    body = src.split("public static let parts", 1)[1].split("\n    ]\n", 1)[0]
    parts = []
    for block in re.findall(r"BoardPartInfo\((.*?)\]\)", body, re.S):
        pid = re.search(r'id:\s*"([^"]+)"', block).group(1)
        name = re.search(r'name:\s*"((?:[^"\\]|\\.)*)"', block).group(1)
        summary = re.search(r'summary:\s*"((?:[^"\\]|\\.)*)"', block).group(1)
        details = strings(block.split("details:", 1)[1])
        parts.append((pid, name, summary, details))
    return parts


def main() -> int:
    if not WEB.is_file():
        print(f"SKIP: {WEB} not found")
        return 0
    web_src, ios_src = WEB.read_text(), IOS.read_text()
    errors = []
    w, i = web_parts(web_src), ios_parts(ios_src)
    if not w:
        errors.append("could not parse web BOARD_PARTS")
    if w != i:
        for a, b in zip(w, i):
            if a != b:
                errors.append(f"part differs: web={a[0]} ios={b[0]}")
        if len(w) != len(i):
            errors.append(f"part count differs: web={len(w)} ios={len(i)}")
    dims = re.search(r"BOARD_DIMENSIONS_MM\s*=\s*\{\s*length:\s*([\d.]+),\s*width:\s*([\d.]+),\s*thickness:\s*([\d.]+)", web_src)
    for key, value in zip(("lengthMm", "widthMm", "thicknessMm"), dims.groups() if dims else ()):
        m = re.search(rf"{key}\s*=\s*([\d.]+)", ios_src)
        if not m or float(m.group(1)) != float(value):
            errors.append(f"{key} differs from web ({value})")
    if errors:
        print("\n".join("FAIL: " + e for e in errors))
        return 1
    print(f"board model consistent with the web ({len(w)} parts, {dims.groups() if dims else ''} mm)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
