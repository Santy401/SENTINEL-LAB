#!/usr/bin/env python3
"""Validate SENTINEL-LAB protocol messages against versioned JSON Schemas."""

from __future__ import annotations

import argparse
import json
import math
import re
import sys
from datetime import datetime
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator, FormatChecker
from referencing import Registry, Resource

PROTOCOL_ROOT = Path(__file__).resolve().parent
SCHEMA_DIR = PROTOCOL_ROOT / "schemas"
SUPPORTED_VERSION = "1.0.0"
MAX_ENVELOPE_BYTES = 16 * 1024
TIMESTAMP_KEYS = {"timestamp", "sensorTimestamp", "occurredAt", "lastSeen"}


def _reject_constant(value: str) -> None:
    raise ValueError(f"non-finite number is not allowed: {value}")


def _reject_duplicates(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate object key: {key}")
        result[key] = value
    return result


def load_json(path: Path) -> Any:
    return json.loads(
        path.read_text(encoding="utf-8"),
        parse_constant=_reject_constant,
        object_pairs_hook=_reject_duplicates,
    )


def _build_registry() -> Registry:
    resources = []
    for path in sorted(SCHEMA_DIR.glob("*.schema.json")):
        schema = json.loads(path.read_text(encoding="utf-8"))
        resources.append((schema["$id"], Resource.from_contents(schema)))
    return Registry().with_resources(resources)


REGISTRY = _build_registry()
ENVELOPE_SCHEMA = json.loads((SCHEMA_DIR / "envelope.schema.json").read_text(encoding="utf-8"))
VALIDATOR = Draft202012Validator(
    ENVELOPE_SCHEMA,
    registry=REGISTRY,
    format_checker=FormatChecker(),
)


def _is_utc_timestamp(value: str) -> bool:
    if not re.fullmatch(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d{1,3})?Z", value):
        return False
    try:
        datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return False
    return True


def _check_timestamps(value: Any, path: str, errors: list[str]) -> None:
    if isinstance(value, dict):
        for key, item in value.items():
            child = f"{path}.{key}" if path else key
            if key in TIMESTAMP_KEYS and isinstance(item, str) and not _is_utc_timestamp(item):
                errors.append(f"{child}: invalid UTC timestamp {item!r}")
            else:
                _check_timestamps(item, child, errors)
    elif isinstance(value, list):
        for index, item in enumerate(value):
            _check_timestamps(item, f"{path}[{index}]", errors)


def _check_finite_numbers(value: Any, path: str, errors: list[str]) -> None:
    if isinstance(value, float) and not math.isfinite(value):
        errors.append(f"{path or '<root>'}: non-finite number {value!r}")
    elif isinstance(value, dict):
        for key, item in value.items():
            child = f"{path}.{key}" if path else key
            _check_finite_numbers(item, child, errors)
    elif isinstance(value, list):
        for index, item in enumerate(value):
            _check_finite_numbers(item, f"{path}[{index}]", errors)


def validate_message(message: Any) -> list[str]:
    errors: list[str] = []
    if not isinstance(message, dict):
        return ["message: must be a JSON object"]

    version = message.get("protocolVersion")
    if version != SUPPORTED_VERSION:
        errors.append(f"protocolVersion: unsupported version {version!r}")

    encoded = json.dumps(message, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    if len(encoded) > MAX_ENVELOPE_BYTES:
        errors.append(
            f"envelope: {len(encoded)} bytes exceeds limit of {MAX_ENVELOPE_BYTES}"
        )

    if message.get("type") == "connection.hello" and isinstance(message.get("payload"), dict):
        supported = message["payload"].get("supportedVersion")
        if supported != SUPPORTED_VERSION:
            errors.append(
                f"payload.supportedVersion: unsupported version {supported!r}"
            )

    if message.get("type") == "rf.telemetry" and isinstance(message.get("payload"), dict):
        payload = message["payload"]
        clients = payload.get("clients")
        count = payload.get("clientCount")
        if isinstance(clients, list) and isinstance(count, int) and count != len(clients):
            errors.append(
                f"payload.clientCount: declared {count} but found {len(clients)} clients"
            )

    _check_finite_numbers(message, "", errors)
    _check_timestamps(message, "", errors)
    for error in VALIDATOR.iter_errors(message):
        location = ".".join(str(part) for part in error.absolute_path) or "<root>"
        errors.append(f"{location}: {error.message}")
    return errors


def validate_file(path: Path) -> list[str]:
    try:
        message = load_json(path)
    except (OSError, ValueError) as exc:
        return [f"invalid JSON: {exc}"]
    return validate_message(message)


def _iter_json_files(path: Path) -> list[Path]:
    if path.is_dir():
        return sorted(path.glob("*.json"))
    return [path]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="+", type=Path, help="JSON files or directories")
    args = parser.parse_args(argv)

    failures = 0
    checked = 0
    for raw_path in args.paths:
        for path in _iter_json_files(raw_path):
            checked += 1
            errors = validate_file(path)
            if errors:
                failures += 1
                print(f"FAIL {path}")
                for error in errors:
                    print(f"  - {error}")
            else:
                print(f"OK   {path}")

    print(f"Protocol validation: {checked - failures} valid, {failures} invalid")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
