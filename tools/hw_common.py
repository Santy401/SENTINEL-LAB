#!/usr/bin/env python3
"""Shared helpers for reproducible P2 hardware evidence capture and review.

The module uses the Python standard library only. Protocol validation is loaded
from packages/protocol/validate_protocol.py by hw_report.py, which already owns
its jsonschema dependency.
"""

from __future__ import annotations

import base64
import hashlib
import json
import os
import re
import socket
import subprocess
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterator

ROOT = Path(__file__).resolve().parents[1]
PROTOCOL_ROOT = ROOT / "packages" / "protocol"

PROTOCOL_VERSION = "1.0.0"
SUBPROTOCOL = "sentinel.v1"
WS_PORT = 81
WS_PATH = "/realtime"
REALTIME_STATUS_PATH = "/api/realtime/status"
HTTP_READ_PATHS = (
    "/",
    "/api/state",
    "/api/rf",
    "/api/lab",
    "/api/events",
    "/api/export.csv",
    "/motion/native",
    "/motion/status",
)
# The firmware exposes the motion status under /api/motion/status. The literal
# /motion/status path from the capture contract is still observed so an operator
# can see that the legacy path is absent; either one may satisfy the gate.
HTTP_ALTERNATE_PATHS = ("/api/motion/status",)
HTTP_OBSERVED_PATHS = HTTP_READ_PATHS + HTTP_ALTERNATE_PATHS + (REALTIME_STATUS_PATH,)
CLOCK_SYNC_PHASE = "associated"
PHASES = ("pre", "associated", "disconnected", "reconnected", "soak")

EVIDENCE_FILE = "evidence.jsonl"
MANIFEST_FILE = "manifest.json"
REPORT_FILE = "report.json"
REPORT_MD_FILE = "INFORME.md"

MAX_HTTP_BYTES = 64 * 1024
MAX_FRAME_BYTES = 16 * 1024
MAX_NMCLI_LINES = 50

REDACTED_KEYS = frozenset(
    {
        "authorization",
        "proxy-authorization",
        "token",
        "bearer",
        "password",
        "secret",
        "apikey",
        "api_key",
        "cookie",
        "set-cookie",
    }
)

_UTC_PATTERN = re.compile(
    r"^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})(?:\.(\d{1,3}))?Z$"
)
_HOST_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9.-]{0,253}$")
_SECRET_TEXT_PATTERN = re.compile(r"\b(authorization\s*:\s*)?bearer\s+\S+", re.IGNORECASE)
_WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
_EXCLUDED = object()


class HardwareKitError(Exception):
    """Base error for evidence tooling failures."""


class WebSocketError(HardwareKitError):
    """Minimal WebSocket transport failure."""


class WebSocketHandshakeError(WebSocketError):
    """Handshake completed but did not meet the expected contract."""

    def __init__(self, handshake: dict[str, Any]) -> None:
        self.handshake = handshake
        super().__init__("websocket handshake rejected")


def utc_now() -> datetime:
    return datetime.now(timezone.utc)


def format_utc_ms(moment: datetime) -> str:
    """Format UTC with exactly three fractional digits, as Protocol v1 expects."""
    moment = moment.astimezone(timezone.utc)
    return moment.strftime("%Y-%m-%dT%H:%M:%S.") + f"{moment.microsecond // 1000:03d}Z"


def epoch_ms(moment: datetime) -> int:
    return int(moment.timestamp() * 1000)


def parse_utc_ms(text: Any) -> int | None:
    """Parse a Protocol v1 UTC timestamp into epoch milliseconds."""
    if not isinstance(text, str):
        return None
    match = _UTC_PATTERN.fullmatch(text)
    if not match:
        return None
    year, month, day, hour, minute, second = (int(value) for value in match.groups()[:6])
    milliseconds = int((match.group(7) or "0").ljust(3, "0"))
    try:
        moment = datetime(year, month, day, hour, minute, second, milliseconds * 1000, timezone.utc)
    except ValueError:
        return None
    return epoch_ms(moment)


def canonical_json(value: Any) -> bytes:
    return json.dumps(
        value,
        sort_keys=True,
        separators=(",", ":"),
        ensure_ascii=False,
        allow_nan=False,
    ).encode("utf-8")


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_json(value: Any) -> str:
    return sha256_bytes(canonical_json(value))


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _reject_constant(value: str) -> None:
    raise ValueError(f"non-finite number is not allowed: {value}")


def _reject_duplicates(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, item in pairs:
        if key in result:
            raise ValueError(f"duplicate object key: {key}")
        result[key] = item
    return result


def parse_json(text: str) -> Any:
    return json.loads(
        text,
        parse_constant=_reject_constant,
        object_pairs_hook=_reject_duplicates,
    )


def redact(value: Any) -> tuple[Any, bool]:
    """Remove secret-looking keys and values before evidence is persisted."""
    changed = [False]

    def walk(node: Any) -> Any:
        if isinstance(node, dict):
            output: dict[str, Any] = {}
            for key, item in node.items():
                normalized = str(key).lower()
                if normalized in REDACTED_KEYS:
                    output[key] = "[REDACTED]"
                    changed[0] = True
                else:
                    output[key] = walk(item)
            return output
        if isinstance(node, (list, tuple)):
            return [walk(item) for item in node]
        if isinstance(node, str):
            def replace(match: re.Match[str]) -> str:
                changed[0] = True
                prefix = match.group(1) or ""
                return f"{prefix}Bearer [REDACTED]"

            return _SECRET_TEXT_PATTERN.sub(replace, node)
        return node

    return walk(value), changed[0]


def hash_of_stored_record(record: dict[str, Any]) -> str:
    payload = dict(record)
    payload.pop("recordHash", None)
    return sha256_json(payload)


def finalize_record(record: dict[str, Any]) -> dict[str, Any]:
    """Stamp UTC time, redact secrets and attach the per-record SHA256."""
    payload = dict(record)
    payload.setdefault("capturedAt", format_utc_ms(utc_now()))
    clean, was_redacted = redact(payload)
    if was_redacted:
        clean["redacted"] = True
    clean.pop("recordHash", None)
    clean["recordHash"] = hash_of_stored_record(clean)
    return clean


def append_record(path: Path, record: dict[str, Any]) -> dict[str, Any]:
    final = finalize_record(record)
    line = json.dumps(final, sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False)
    with path.open("a", encoding="utf-8") as handle:
        handle.write(line + "\n")
    return final


def read_jsonl(path: Path) -> tuple[list[dict[str, Any]], list[str]]:
    """Read evidence records; unparsable lines are returned as explicit issues."""
    records: list[dict[str, Any]] = []
    issues: list[str] = []
    if not path.exists():
        return records, [f"missing evidence file: {path.name}"]
    with path.open("r", encoding="utf-8") as handle:
        for number, line in enumerate(handle, 1):
            if not line.strip():
                continue
            try:
                value = parse_json(line)
            except (ValueError, TypeError) as exc:
                issues.append(f"invalid evidence JSON at line {number}: {exc}")
                continue
            if not isinstance(value, dict):
                issues.append(f"evidence line {number} is not an object")
                continue
            records.append(value)
    return records, issues


def verify_evidence(
    evidence_path: Path, manifest_path: Path
) -> dict[str, Any]:
    """Verify the manifest hash, record hashes and the evidence file hash."""
    result: dict[str, Any] = {
        "ok": False,
        "records": [],
        "issues": [],
        "manifestPresent": manifest_path.exists(),
        "evidencePresent": evidence_path.exists(),
    }
    records, issues = read_jsonl(evidence_path)
    result["records"] = records
    result["issues"].extend(issues)

    manifest: dict[str, Any] = {}
    if manifest_path.exists():
        try:
            value = parse_json(manifest_path.read_text(encoding="utf-8"))
            if isinstance(value, dict):
                manifest = value
            else:
                result["issues"].append("manifest is not an object")
        except (OSError, ValueError) as exc:
            result["issues"].append(f"invalid manifest JSON: {exc}")
    else:
        result["issues"].append("missing manifest.json")

    for index, record in enumerate(records, 1):
        declared = record.get("recordHash")
        computed = hash_of_stored_record(record)
        if declared != computed:
            result["issues"].append(f"record hash mismatch at line {index}")

    if evidence_path.exists():
        file_hash = sha256_file(evidence_path)
        result["evidenceSha256Computed"] = file_hash
        if manifest.get("evidenceSha256") != file_hash:
            result["issues"].append("manifest evidenceSha256 does not match evidence.jsonl")

    declared_hashes = manifest.get("perRecordSha256")
    if not isinstance(declared_hashes, list):
        result["issues"].append("manifest perRecordSha256 is missing")
    else:
        actual_hashes = [record.get("recordHash") for record in records]
        if declared_hashes != actual_hashes:
            result["issues"].append("manifest record hash list does not match evidence.jsonl")
        if manifest.get("recordCount") != len(records):
            result["issues"].append("manifest recordCount does not match evidence.jsonl")

    result["manifest"] = manifest
    result["ok"] = not result["issues"]
    return result


def write_manifest(path: Path, manifest: dict[str, Any]) -> dict[str, Any]:
    clean, was_redacted = redact(manifest)
    if was_redacted:
        raise HardwareKitError("manifest contained secret-looking data")
    clean["updatedAt"] = format_utc_ms(utc_now())
    path.write_text(
        json.dumps(clean, sort_keys=True, indent=2, ensure_ascii=False, allow_nan=False) + "\n",
        encoding="utf-8",
    )
    return clean


def valid_host(host: str) -> bool:
    return bool(_HOST_PATTERN.fullmatch(host))


def has_simulation_marker(value: Any) -> bool:
    """True when a payload contains simulated=true or a simulated source."""
    if isinstance(value, dict):
        for key, item in value.items():
            normalized = str(key).lower()
            if normalized in ("simulated", "simulation") and (
                item is True or (isinstance(item, str) and item.lower() == "true")
            ):
                return True
            if normalized in ("sourcetype", "source") and (
                isinstance(item, str) and item.lower() == "simulated"
            ):
                return True
            if has_simulation_marker(item):
                return True
    elif isinstance(value, (list, tuple)):
        return any(has_simulation_marker(item) for item in value)
    return False


def strip_simulated(value: Any) -> Any:
    """Drop any record carrying a simulation marker."""
    if isinstance(value, dict):
        if has_simulation_marker(value):
            return _EXCLUDED
        output: dict[str, Any] = {}
        for key, item in value.items():
            cleaned = strip_simulated(item)
            if cleaned is not _EXCLUDED:
                output[key] = cleaned
        return output
    if isinstance(value, (list, tuple)):
        output_list = []
        for item in value:
            cleaned = strip_simulated(item)
            if cleaned is not _EXCLUDED:
                output_list.append(cleaned)
        return output_list
    return value


def clean_evidence_value(value: Any) -> tuple[Any, bool]:
    cleaned = strip_simulated(value)
    if cleaned is _EXCLUDED:
        return None, True
    return cleaned, False


def stale_fixture_refs(value: Any) -> list[str]:
    """Find references to build fixtures or protocol examples inside evidence."""
    found: list[str] = []

    def walk(node: Any, path: str) -> None:
        if isinstance(node, dict):
            for key, item in node.items():
                walk(item, f"{path}.{key}" if path else str(key))
        elif isinstance(node, (list, tuple)):
            for index, item in enumerate(node):
                walk(item, f"{path}[{index}]")
        elif isinstance(node, str):
            normalized = node.replace("\\", "/")
            if "build/firmware-" in normalized or "packages/protocol/examples/" in normalized:
                found.append(path or "<root>")

    walk(value, "")
    return found


def stale_fixture_files(evidence_dir: Path) -> list[str]:
    found: list[str] = []
    for path in sorted(evidence_dir.rglob("*")):
        relative = path.relative_to(evidence_dir).as_posix()
        if re.fullmatch(r"build/firmware-[^/]*\.json", relative) or relative.startswith(
            "packages/protocol/examples/"
        ):
            found.append(relative)
    return found


def http_get(host: str, path: str, port: int = 80, timeout: float = 4.0) -> dict[str, Any]:
    """Perform a read-only HTTP GET without credentials; never store headers."""
    url = f"http://{host}{path}" if port == 80 else f"http://{host}:{port}{path}"
    observation: dict[str, Any] = {
        "kind": "http",
        "path": path,
        "url": url,
        "status": None,
        "contentType": None,
        "bodyJson": None,
        "bodyText": None,
        "truncated": False,
        "error": None,
    }
    request = urllib.request.Request(
        url,
        method="GET",
        headers={"Accept": "*/*", "Cache-Control": "no-store"},
    )
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            observation["status"] = int(response.status)
            observation["contentType"] = response.headers.get("Content-Type")
            raw = response.read(MAX_HTTP_BYTES + 1)
    except urllib.error.HTTPError as exc:
        observation["status"] = int(exc.code)
        raw = exc.read(MAX_HTTP_BYTES + 1)
        observation["error"] = f"HTTP {exc.code}"
    except (OSError, TimeoutError, ValueError) as exc:
        observation["error"] = f"{type(exc).__name__}: {exc}"
        return observation

    observation["truncated"] = len(raw) > MAX_HTTP_BYTES
    text = raw[:MAX_HTTP_BYTES].decode("utf-8", errors="replace")
    observation["bodyText"] = text
    if observation["contentType"] and "json" in observation["contentType"].lower():
        try:
            body = parse_json(text)
            if isinstance(body, dict):
                observation["bodyJson"] = body
        except (ValueError, TypeError):
            observation["error"] = "invalid JSON response"
    return observation


def _nmcli(fields: str, arguments: list[str], timeout: float = 4.0) -> dict[str, Any]:
    command = ["nmcli", "-t", "-f", fields, *arguments]
    try:
        completed = subprocess.run(
            command,
            capture_output=True,
            text=True,
            timeout=timeout,
            check=False,
        )
    except FileNotFoundError:
        return {"available": False, "command": " ".join(command), "error": "nmcli not found", "lines": []}
    except (OSError, subprocess.SubprocessError) as exc:
        return {"available": False, "command": " ".join(command), "error": str(exc), "lines": []}
    lines = [line for line in completed.stdout.splitlines() if line][:MAX_NMCLI_LINES]
    return {
        "available": completed.returncode == 0,
        "command": " ".join(command),
        "exitCode": completed.returncode,
        "lines": lines,
    }


def wifi_info() -> dict[str, Any]:
    """Collect passive Wi-Fi information with nmcli; never changes networking."""
    scan = _nmcli(
        "IN-USE,SSID,BSSID,CHAN,SIGNAL,MODE",
        ["device", "wifi", "list", "--rescan", "no"],
    )
    devices = _nmcli("DEVICE,TYPE,STATE,CONNECTION", ["device", "status"])
    networks = []
    for line in scan.get("lines", []):
        parts = line.split(":", 5)
        if len(parts) < 6:
            continue
        networks.append(
            {
                "inUse": parts[0],
                "ssid": parts[1],
                "bssid": parts[2],
                "channel": parts[3],
                "signal": parts[4],
                "mode": parts[5],
            }
        )
    return {
        "kind": "wifi",
        "available": bool(scan.get("available") or devices.get("available")),
        "scan": scan,
        "devices": devices,
        "networks": networks,
    }


class MinimalWebSocket:
    """Tiny RFC 6455 client: handshake, masked sends, unmasked server frames."""

    def __init__(self, host: str, port: int, path: str, timeout: float = 5.0) -> None:
        self.host = host
        self.port = port
        self.path = path
        self.timeout = timeout
        self.socket: socket.socket | None = None
        self.buffer = bytearray()
        self.handshake: dict[str, Any] | None = None
        self.closed = False

    def connect(self, token: str | None = None) -> dict[str, Any]:
        key = base64.b64encode(os.urandom(16)).decode("ascii")
        request_headers = {
            "Host": f"{self.host}:{self.port}",
            "Upgrade": "websocket",
            "Connection": "Upgrade",
            "Sec-WebSocket-Key": key,
            "Sec-WebSocket-Version": "13",
            "Sec-WebSocket-Protocol": SUBPROTOCOL,
        }
        lines = [f"GET {self.path} HTTP/1.1"]
        lines.extend(f"{name}: {value}" for name, value in request_headers.items())
        authorization_present = False
        if token:
            clean_token = token.replace("\r", "").replace("\n", "")
            if any(ord(character) < 32 or ord(character) > 126 for character in clean_token):
                raise WebSocketError("invalid characters in bearer token")
            lines.append("Authorization: Bearer " + clean_token)
            authorization_present = True

        self.socket = socket.create_connection((self.host, self.port), timeout=self.timeout)
        self.socket.settimeout(self.timeout)
        self.socket.sendall(("\r\n".join(lines) + "\r\n\r\n").encode("ascii"))

        raw_headers = self._read_headers()
        status, headers = self._parse_headers(raw_headers)
        expected_accept = base64.b64encode(
            hashlib.sha1((key + _WS_GUID).encode("ascii")).digest()
        ).decode("ascii")
        accept = headers.get("sec-websocket-accept")
        handshake: dict[str, Any] = {
            "kind": "wsHandshake",
            "connected": False,
            "status": status,
            "upgrade": headers.get("upgrade", "").lower() == "websocket",
            "connectionUpgrade": "upgrade" in headers.get("connection", "").lower(),
            "acceptValid": accept == expected_accept,
            "versionRequested": "13",
            "protocolRequested": SUBPROTOCOL,
            "protocolAccepted": headers.get("sec-websocket-protocol"),
            "cacheControl": headers.get("cache-control"),
            "authorizationPresent": authorization_present,
            "token_present": bool(token),
            "requestHeaders": request_headers,
        }
        handshake["ok"] = (
            handshake["status"] == 101
            and handshake["upgrade"]
            and handshake["connectionUpgrade"]
            and handshake["acceptValid"]
            and handshake["protocolAccepted"] == SUBPROTOCOL
        )
        handshake["connected"] = handshake["ok"]
        self.handshake = handshake
        if not handshake["ok"]:
            raise WebSocketHandshakeError(handshake)
        return handshake

    def _read_headers(self) -> bytes:
        while b"\r\n\r\n" not in self.buffer:
            chunk = self._read_socket(4096)
            if not chunk:
                raise WebSocketError("connection closed during handshake")
            self.buffer.extend(chunk)
            if len(self.buffer) > 8192:
                raise WebSocketError("handshake headers too large")
        index = self.buffer.index(b"\r\n\r\n") + 4
        headers = bytes(self.buffer[:index])
        del self.buffer[:index]
        return headers

    @staticmethod
    def _parse_headers(raw: bytes) -> tuple[int | None, dict[str, str]]:
        text = raw.decode("iso-8859-1")
        lines = text.split("\r\n")
        status = None
        headers: dict[str, str] = {}
        for line in lines:
            if not line:
                continue
            if line.startswith("HTTP/"):
                parts = line.split(" ", 2)
                if len(parts) >= 2 and parts[1].isdigit():
                    status = int(parts[1])
                continue
            if ":" in line:
                name, value = line.split(":", 1)
                headers[name.strip().lower()] = value.strip()
        return status, headers

    def _read_socket(self, count: int) -> bytes:
        if self.socket is None:
            raise WebSocketError("socket is not connected")
        try:
            return self.socket.recv(count)
        except socket.timeout as exc:
            raise TimeoutError("websocket read timeout") from exc

    def _recv_exact(self, count: int) -> bytes:
        while len(self.buffer) < count:
            chunk = self._read_socket(max(1, count - len(self.buffer)))
            if not chunk:
                raise WebSocketError("connection closed during frame")
            self.buffer.extend(chunk)
        data = bytes(self.buffer[:count])
        del self.buffer[:count]
        return data

    def send_frame(self, opcode: int, payload: bytes) -> None:
        if self.socket is None or self.closed:
            raise WebSocketError("socket is not connected")
        header = bytearray([0x80 | opcode])
        length = len(payload)
        if length < 126:
            header.append(0x80 | length)
        elif length < 65536:
            header.append(0x80 | 126)
            header.extend(length.to_bytes(2, "big"))
        else:
            header.append(0x80 | 127)
            header.extend(length.to_bytes(8, "big"))
        mask = os.urandom(4)
        header.extend(mask)
        masked = bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))
        self.socket.sendall(bytes(header) + masked)

    def send_text(self, text: str) -> None:
        self.send_frame(0x1, text.encode("utf-8"))

    def recv_frame(self) -> dict[str, Any]:
        first, second = self._recv_exact(2)
        fin = bool(first & 0x80)
        opcode = first & 0x0F
        server_masked = bool(second & 0x80)
        length = second & 0x7F
        if length == 126:
            length = int.from_bytes(self._recv_exact(2), "big")
        elif length == 127:
            length = int.from_bytes(self._recv_exact(8), "big")
        if length > MAX_FRAME_BYTES:
            raise WebSocketError("frame exceeds evidence size limit")
        mask = self._recv_exact(4) if server_masked else None
        payload = self._recv_exact(length) if length else b""
        if mask:
            payload = bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))
        frame: dict[str, Any] = {
            "opcode": opcode,
            "fin": fin,
            "serverMasked": server_masked,
            "length": length,
            "payload": payload,
        }
        if opcode == 0x9:
            self.send_frame(0xA, payload)
            frame["type"] = "ping"
        elif opcode == 0xA:
            frame["type"] = "pong"
        elif opcode == 0x8:
            self.closed = True
            frame["type"] = "close"
        elif opcode in (0x0, 0x1, 0x2):
            frame["type"] = "continuation" if opcode == 0x0 else ("text" if opcode == 0x1 else "binary")
        else:
            raise WebSocketError(f"unsupported websocket opcode {opcode}")
        return frame

    def settimeout(self, timeout: float) -> None:
        if self.socket is not None:
            self.socket.settimeout(timeout)

    def close(self) -> None:
        if self.socket is not None and not self.closed:
            try:
                self.send_frame(0x8, b"")
            except (OSError, WebSocketError):
                pass
        if self.socket is not None:
            try:
                self.socket.close()
            except OSError:
                pass
        self.closed = True


def iter_records(records: list[dict[str, Any]]) -> Iterator[dict[str, Any]]:
    """Yield records that are usable as hardware evidence."""
    for record in records:
        clean, excluded = clean_evidence_value(record)
        if excluded:
            continue
        yield clean
