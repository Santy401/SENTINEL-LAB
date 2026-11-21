#!/usr/bin/env python3
"""Read-only HTTP compatibility check for SENTINEL-LAB v0.3.1/v0.4.0.

Only GET requests without credentials are sent. The script never changes
device state and never stores response headers or tokens.
"""

from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))

import hw_common as hw  # noqa: E402

REQUIRED_PATHS = (
    "/",
    "/api/state",
    "/api/rf",
    "/api/lab",
    "/api/events",
    "/api/export.csv",
    "/api/motion/status",
)
ALTERNATE_PATHS = ("/motion/status",)
HEALTH_PATH = hw.REALTIME_STATUS_PATH
HEALTH_FIELDS = (
    "fw", "realtimeVersion", "bootId", "uptimeMs", "clockSynced", "clientCount",
    "freeHeapBytes", "minFreeHeapBytes", "heapSamples", "droppedFrames",
    "rejectedMessages", "droppedTelemetry", "skippedSimulatedEvents",
)


def summarize(observation: dict[str, Any]) -> str:
    status = observation.get("status")
    error = observation.get("error")
    if status == 200:
        return "OK"
    if status is not None:
        return f"HTTP {status}"
    return f"sin respuesta ({error or 'error desconocido'})"


def check_health(observation: dict[str, Any], expected_fw: str) -> tuple[bool, str]:
    body = observation.get("bodyJson")
    if not isinstance(body, dict):
        return False, "cuerpo no JSON"
    missing = [field for field in HEALTH_FIELDS if field not in body]
    if missing:
        return False, "campos ausentes: " + ", ".join(missing)
    if body.get("fw") not in (None, expected_fw):
        return False, f"fw={body.get('fw')}"
    return True, "13 campos"


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="192.168.4.1")
    parser.add_argument("--timeout", type=float, default=4.0)
    parser.add_argument("--expected-fw", default="0.3.1")
    parser.add_argument("--save-json", type=Path, help="optional host-side observation file")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    observations: list[dict[str, Any]] = []
    failures = 0

    for path in (*REQUIRED_PATHS, *ALTERNATE_PATHS, HEALTH_PATH):
        observation = hw.http_get(args.host, path, timeout=args.timeout)
        observations.append(observation)
        result = summarize(observation)
        note = ""
        body = observation.get("bodyJson")
        fw = body.get("fw") if isinstance(body, dict) else None

        if path in REQUIRED_PATHS:
            if observation.get("status") != 200:
                failures += 1
                result = "FAIL " + result
            elif fw and fw != args.expected_fw:
                failures += 1
                result = "FAIL fw inesperado"
                note = f"fw={fw}"
            else:
                note = f"fw={fw or 'no declarado'}"
        elif path in ALTERNATE_PATHS:
            note = "alias informativo; /api/motion/status es la ruta canónica"
        elif path == HEALTH_PATH:
            if observation.get("status") == 404:
                note = "ausente en _rf (esperado)"
            elif observation.get("status") == 200:
                valid, detail = check_health(observation, args.expected_fw)
                note = detail if valid else "FAIL " + detail
                if not valid:
                    failures += 1
            else:
                note = "NOT_VERIFIED; la ruta solo existe en _realtime"

        print(f"{path:28} {result:22} {note}".rstrip())

    if args.save_json:
        payload = {
            "capturedAt": hw.format_utc_ms(datetime.now(timezone.utc)),
            "host": args.host,
            "expectedFw": args.expected_fw,
            "observations": observations,
        }
        args.save_json.parent.mkdir(parents=True, exist_ok=True)
        args.save_json.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n",
                                  encoding="utf-8")
        print(f"\nObservaciones guardadas en {args.save_json}")

    print()
    if failures:
        print(f"FAIL: {failures} comprobaciones HTTP obligatorias no se cumplieron")
    else:
        print("OK: rutas HTTP obligatorias respondieron con coherencia de versión")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
