#!/usr/bin/env python3
"""Automated checks for the SENTINEL-LAB v1 protocol contract."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

from jsonschema import FormatChecker
from jsonschema.exceptions import FormatError

PROTOCOL_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROTOCOL_ROOT))

from validate_protocol import (  # noqa: E402
    MAX_ENVELOPE_BYTES,
    SCHEMA_DIR,
    load_json,
    validate_file,
    validate_message,
)

VALID_DIR = PROTOCOL_ROOT / "examples" / "valid"
INVALID_DIR = PROTOCOL_ROOT / "examples" / "invalid"


class ProtocolContractTests(unittest.TestCase):
    def test_valid_examples(self) -> None:
        paths = sorted(VALID_DIR.glob("*.json"))
        self.assertTrue(paths)
        for path in paths:
            with self.subTest(example=path.name):
                self.assertEqual(validate_file(path), [])

    def test_negative_examples_are_rejected(self) -> None:
        paths = sorted(INVALID_DIR.glob("*.json"))
        self.assertTrue(paths)
        for path in paths:
            with self.subTest(example=path.name):
                self.assertTrue(validate_file(path), f"{path.name} was accepted")

    def test_all_message_types_have_schema_and_example(self) -> None:
        envelope = load_json(SCHEMA_DIR / "envelope.schema.json")
        types = set(envelope["properties"]["type"]["enum"])
        payload_schemas = {
            path.name.removesuffix(".schema.json")
            for path in SCHEMA_DIR.glob("*.schema.json")
            if path.name != "envelope.schema.json"
        }
        examples = {path.stem for path in VALID_DIR.glob("*.json")}
        self.assertEqual(types, payload_schemas)
        self.assertEqual(types, examples)

    def test_error_payloads_never_claim_real_measurements(self) -> None:
        for path in SCHEMA_DIR.glob("*.schema.json"):
            schema = load_json(path)
            simulated = schema.get("properties", {}).get("simulated")
            if simulated is not None:
                self.assertEqual(simulated.get("const"), False, path.name)

    def test_impossible_timestamp_is_rejected(self) -> None:
        message = load_json(VALID_DIR / "system.status.json")
        message["timestamp"] = "2026-02-30T16:10:03.000Z"
        errors = validate_message(message)
        self.assertTrue(any("invalid UTC timestamp" in error for error in errors))

    def test_sequence_bounds(self) -> None:
        message = load_json(VALID_DIR / "system.status.json")
        for value in (-1, 9007199254740992):
            with self.subTest(sequence=value):
                candidate = dict(message, sequence=value)
                self.assertTrue(validate_message(candidate))

    def test_hello_supported_version_matches_envelope(self) -> None:
        message = load_json(VALID_DIR / "connection.hello.json")
        message["payload"]["supportedVersion"] = "2.0.0"
        errors = validate_message(message)
        self.assertTrue(any("supportedVersion" in error for error in errors))

    def test_rf_client_count_matches_clients(self) -> None:
        message = load_json(VALID_DIR / "rf.telemetry.json")
        message["payload"]["clientCount"] = 8
        errors = validate_message(message)
        self.assertTrue(any("clientCount" in error for error in errors))

    def test_payload_size_limit(self) -> None:
        message = load_json(VALID_DIR / "system.status.json")
        message["payload"]["diagnostics"] = "x" * MAX_ENVELOPE_BYTES
        errors = validate_message(message)
        self.assertTrue(any("exceeds limit" in error for error in errors))

    def test_non_finite_numbers_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "nan.json"
            path.write_text('{"value": NaN}', encoding="utf-8")
            with self.assertRaises(ValueError):
                load_json(path)

    def test_validate_message_rejects_non_finite_values(self) -> None:
        message = load_json(VALID_DIR / "motion.sample.json")
        for value in (float("nan"), float("inf"), float("-inf")):
            with self.subTest(value=value):
                candidate = json.loads(json.dumps(message))
                candidate["payload"]["acceleration"]["x"] = value
                errors = validate_message(candidate)
                self.assertTrue(any("non-finite number" in error for error in errors))

    def test_duplicate_keys_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "duplicate.json"
            path.write_text('{"sequence": 1, "sequence": 2}', encoding="utf-8")
            with self.assertRaises(ValueError):
                load_json(path)

    def test_schema_ids_are_resolvable(self) -> None:
        schemas = {path.name: load_json(path) for path in SCHEMA_DIR.glob("*.schema.json")}
        ids = {schema["$id"] for schema in schemas.values()}
        self.assertEqual(len(ids), len(schemas))
        for schema in schemas.values():
            self.assertTrue(schema["$id"].startswith("https://"))
        envelope = schemas["envelope.schema.json"]
        refs = [
            branch["properties"]["payload"]["$ref"]
            for branch in envelope["oneOf"]
        ]
        self.assertTrue(refs)
        for ref in refs:
            self.assertIn(ref, ids)

    def test_format_checker_rejects_impossible_date(self) -> None:
        checker = FormatChecker()
        self.assertIn("date-time", checker.checkers)
        with self.assertRaises(FormatError):
            checker.check("2026-02-30T16:10:03Z", "date-time")

    def test_schema_files_are_valid_json(self) -> None:
        paths = sorted(SCHEMA_DIR.glob("*.schema.json"))
        self.assertEqual(len(paths), 10)
        for path in paths:
            with self.subTest(schema=path.name):
                self.assertIsInstance(load_json(path), dict)


if __name__ == "__main__":
    unittest.main(verbosity=2)
