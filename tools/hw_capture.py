#!/usr/bin/env python3
"""Capture live P2 hardware evidence from the SENTINEL-LAB access point."""

from __future__ import annotations

import argparse
import json
import os
import time
from datetime import datetime
from pathlib import Path
from typing import Any

from hw_common import (
    CLOCK_SYNC_PHASE,
    EVIDENCE_FILE,
    HTTP_OBSERVED_PATHS,
    MANIFEST_FILE,
    PHASES,
    PROTOCOL_VERSION,
    REALTIME_STATUS_PATH,
    SUBPROTOCOL,
    WS_PATH,
    WS_PORT,
    HardwareKitError,
    MinimalWebSocket,
    WebSocketHandshakeError,
    append_record,
    epoch_ms,
    format_utc_ms,
    hash_of_stored_record,
    http_get,
    parse_json,
    read_jsonl,
    sha256_bytes,
    sha256_file,
    utc_now,
    valid_host,
    wifi_info,
    write_manifest,
)

PHASE_ACTIONS = {
    "pre": "verify-client-not-associated",
    "associated": "associate-client",
    "disconnected": "disconnect-client",
    "reconnected": "reassociate-client",
    "soak": "leave-system-idle",
}


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Captura evidencia real de hardware P2 en un directorio de evidencia."
    )
    parser.add_argument("--dir", required=True, type=Path, help="Directorio de evidencia")
    parser.add_argument("--host", required=True, help="IP del AP, por ejemplo 192.168.4.1")
    parser.add_argument("--phase", required=True, choices=PHASES, help="Fase de la captura")
    parser.add_argument("--seconds", required=True, type=float, help="Duración de la ventana WebSocket")
    parser.add_argument("--distance-m", type=float, help="Distancia física declarada por el operador")
    parser.add_argument("--action", help="Acción adicional declarada por el operador")
    parser.add_argument("--note", help="Nota declarada por el operador (sin credenciales)")
    args = parser.parse_args(argv)
    if args.seconds <= 0 or args.seconds > 3600:
        parser.error("--seconds debe estar entre 0 y 3600")
    if args.distance_m is not None and (args.distance_m < 0 or args.distance_m > 10000):
        parser.error("--distance-m debe estar entre 0 y 10000")
    if not valid_host(args.host):
        parser.error("--host no es una IP o nombre de host válido")
    return args


def clean_note(note: str | None, token: str) -> str | None:
    if not note:
        return None
    cleaned = " ".join(note.split())[:200]
    if token and token in cleaned:
        return "[REDACTED]"
    return cleaned


def marker_base(args: argparse.Namespace, action: str) -> dict[str, Any]:
    record: dict[str, Any] = {
        "kind": "operatorAction",
        "phase": args.phase,
        "action": action,
        "operatorDeclared": True,
        "host": args.host,
    }
    if args.distance_m is not None:
        record["distanceM"] = args.distance_m
    note = clean_note(args.note, os.environ.get("SENTINEL_HW_BEARER", ""))
    if note:
        record["note"] = note
    if args.action:
        record["additionalAction"] = clean_note(args.action, os.environ.get("SENTINEL_HW_BEARER", ""))
    return record


def capture_http(evidence_path: Path, args: argparse.Namespace, step: str) -> None:
    for path in HTTP_OBSERVED_PATHS:
        observation = http_get(args.host, path)
        observation["phase"] = args.phase
        observation["step"] = step
        if path in ("/motion/status", "/api/motion/status", REALTIME_STATUS_PATH):
            observation["present"] = observation["status"] == 200
        append_record(evidence_path, observation)


def build_gateway_hello(moment: datetime) -> dict[str, Any]:
    """Build the wire hello; epochMs is internal, the schema field is timestamp."""
    wire_timestamp = format_utc_ms(moment)
    envelope = {
        "protocolVersion": PROTOCOL_VERSION,
        "type": "connection.hello",
        "timestamp": wire_timestamp,
        "sequence": 1,
        "source": "gateway-hw-capture",
        "sessionId": "hwcap-" + os.urandom(4).hex(),
        "payload": {
            "deviceId": "gateway-hw-capture-01",
            "role": "gateway",
            "supportedVersion": PROTOCOL_VERSION,
            "capabilities": ["clock.sync"],
        },
    }
    return envelope


def record_frame(
    evidence_path: Path,
    args: argparse.Namespace,
    index: int,
    frame: dict[str, Any],
) -> None:
    record: dict[str, Any] = {
        "kind": "wsFrame",
        "phase": args.phase,
        "frameIndex": index,
        "frameType": frame.get("type"),
        "opcode": frame["opcode"],
        "fin": frame["fin"],
        "serverMasked": frame["serverMasked"],
        "length": frame["length"],
        "frameSha256": sha256_bytes(frame["payload"]),
        "isProtocolEnvelope": False,
    }
    if frame.get("type") == "text":
        text = frame["payload"].decode("utf-8", errors="replace")
        try:
            envelope = parse_json(text)
        except (ValueError, TypeError) as exc:
            record["parseError"] = f"{type(exc).__name__}: {exc}"
            record["looksLikeEnvelope"] = '"protocolVersion"' in text
            record["text"] = text[:2048]
        else:
            if isinstance(envelope, dict) and "protocolVersion" in envelope:
                record["isProtocolEnvelope"] = True
                record["envelopeType"] = envelope.get("type")
                record["envelope"] = envelope
            else:
                record["text"] = text[:2048]
    append_record(evidence_path, record)


def capture_websocket(
    evidence_path: Path,
    args: argparse.Namespace,
    token: str,
    token_present: bool,
) -> None:
    designated = args.phase == CLOCK_SYNC_PHASE
    ws_token = token if designated and token_present else None
    socket_client = MinimalWebSocket(args.host, WS_PORT, WS_PATH)
    handshake: dict[str, Any] | None = None

    try:
        handshake = socket_client.connect(ws_token)
    except WebSocketHandshakeError as exc:
        handshake = dict(exc.handshake)
        handshake["error"] = "handshake-rejected"
        append_record(evidence_path, {**handshake, "phase": args.phase})
        if designated:
            append_clock_sync_marker(evidence_path, args, False, token_present, "handshake-failed")
        if socket_client.socket is not None:
            socket_client.socket.close()
        return
    except (OSError, TimeoutError, HardwareKitError) as exc:
        append_record(
            evidence_path,
            {
                "kind": "wsHandshake",
                "phase": args.phase,
                "connected": False,
                "token_present": token_present,
                "authorizationPresent": False,
                "protocolRequested": SUBPROTOCOL,
                "versionRequested": "13",
                "error": f"{type(exc).__name__}: {exc}",
            },
        )
        if designated:
            append_clock_sync_marker(evidence_path, args, False, token_present, "handshake-failed")
        if socket_client.socket is not None:
            socket_client.socket.close()
        return

    append_record(evidence_path, {**handshake, "phase": args.phase})

    sent = False
    if designated:
        if token_present and handshake and handshake.get("connected"):
            moment = utc_now()
            envelope = build_gateway_hello(moment)
            socket_client.send_text(
                json.dumps(envelope, separators=(",", ":"), ensure_ascii=False, allow_nan=False)
            )
            append_clock_sync_marker(evidence_path, args, True, token_present, "sent", envelope, moment)
            sent = True
        else:
            reason = "token-missing" if not token_present else "handshake-failed"
            append_clock_sync_marker(evidence_path, args, False, token_present, reason)
    elif not token_present:
        append_clock_sync_marker(evidence_path, args, False, False, "no-token-in-env")
    else:
        append_clock_sync_marker(evidence_path, args, False, True, "phase-not-designated")

    frame_index = 0
    deadline = time.monotonic() + args.seconds
    while time.monotonic() < deadline and not socket_client.closed:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break
        socket_client.settimeout(min(0.5, remaining))
        try:
            frame = socket_client.recv_frame()
        except TimeoutError:
            continue
        except (OSError, HardwareKitError) as exc:
            append_record(
                evidence_path,
                {
                    "kind": "wsError",
                    "phase": args.phase,
                    "framesObserved": frame_index,
                    "error": f"{type(exc).__name__}: {exc}",
                },
            )
            break
        frame_index += 1
        record_frame(evidence_path, args, frame_index, frame)

    append_record(
        evidence_path,
        {
            "kind": "wsClose",
            "phase": args.phase,
            "framesObserved": frame_index,
            "clockSyncSent": sent,
            "closeSent": True,
        },
    )
    socket_client.close()


def append_clock_sync_marker(
    evidence_path: Path,
    args: argparse.Namespace,
    attempted: bool,
    token_present: bool,
    reason: str,
    envelope: dict[str, Any] | None = None,
    moment: datetime | None = None,
) -> None:
    marker: dict[str, Any] = {
        "kind": "clockSync",
        "phase": args.phase,
        "designatedPhase": CLOCK_SYNC_PHASE,
        "attempted": attempted,
        "sent": attempted,
        "reason": reason,
        "token_present": token_present,
        "headerField": "Authorization" if attempted else None,
        "wireField": "timestamp" if attempted else None,
    }
    if attempted and envelope is not None and moment is not None:
        marker["hostTimestamp"] = envelope["timestamp"]
        marker["hostEpochMs"] = epoch_ms(moment)
        marker["role"] = envelope["payload"]["role"]
        marker["sessionId"] = envelope["sessionId"]
        marker["envelopeSha256"] = sha256_bytes(
            json.dumps(envelope, separators=(",", ":"), ensure_ascii=False, allow_nan=False).encode("utf-8")
        )
    append_record(evidence_path, marker)


def build_manifest(
    evidence_dir: Path,
    evidence_path: Path,
    args: argparse.Namespace,
    runs: list[dict[str, Any]],
) -> dict[str, Any]:
    records, issues = read_jsonl(evidence_path)
    if issues:
        raise HardwareKitError("evidencia corrupta: " + "; ".join(issues))
    manifest: dict[str, Any] = {
        "schemaVersion": 1,
        "tool": "hw_capture.py",
        "host": args.host,
        "token_present": any(bool(run.get("token_present")) for run in runs),
        "clockSyncPhase": CLOCK_SYNC_PHASE,
        "protocolVersion": PROTOCOL_VERSION,
        "subprotocol": SUBPROTOCOL,
        "wsPort": WS_PORT,
        "evidenceFile": EVIDENCE_FILE,
        "recordCount": len(records),
        "perRecordSha256": [record.get("recordHash") for record in records],
        "evidenceSha256": sha256_file(evidence_path),
        "runs": runs,
        "createdAt": format_utc_ms(utc_now()),
    }
    manifest_path = evidence_dir / MANIFEST_FILE
    return write_manifest(manifest_path, manifest)


def capture(args: argparse.Namespace) -> int:
    args.dir.mkdir(parents=True, exist_ok=True)
    evidence_path = args.dir / EVIDENCE_FILE
    manifest_path = args.dir / MANIFEST_FILE

    existing, issues = read_jsonl(evidence_path)
    if issues:
        raise HardwareKitError("evidencia previa inválida: " + "; ".join(issues))

    for index, record in enumerate(existing, 1):
        if hash_of_stored_record(record) != record.get("recordHash"):
            raise HardwareKitError(f"hash de registro previo inválido en línea {index}")

    token = os.environ.get("SENTINEL_HW_BEARER", "")
    token_present = bool(token)
    started_at = format_utc_ms(utc_now())
    append_record(evidence_path, marker_base(args, "capture-start"))
    append_record(evidence_path, marker_base(args, PHASE_ACTIONS[args.phase]))

    wifi = wifi_info()
    wifi["phase"] = args.phase
    append_record(evidence_path, wifi)

    capture_http(evidence_path, args, "before-ws")
    capture_websocket(evidence_path, args, token, token_present)
    capture_http(evidence_path, args, "after-ws")
    append_record(evidence_path, marker_base(args, "capture-end"))

    run_summary = {
        "phase": args.phase,
        "seconds": args.seconds,
        "distanceM": args.distance_m,
        "token_present": token_present,
        "startedAt": started_at,
        "endedAt": format_utc_ms(utc_now()),
        "recordsAdded": len(read_jsonl(evidence_path)[0]) - len(existing),
    }
    if manifest_path.exists():
        try:
            previous = parse_json(manifest_path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            previous = {}
        runs = previous.get("runs", []) if isinstance(previous, dict) else []
        if not isinstance(runs, list):
            runs = []
    else:
        runs = []
    runs.append(run_summary)
    build_manifest(args.dir, evidence_path, args, runs)

    print(f"Evidencia: {evidence_path}")
    print(f"Manifiesto: {manifest_path}")
    print(f"Fase: {args.phase}; registros añadidos: {run_summary['recordsAdded']}; token_present: {str(token_present).lower()}")
    if args.phase == CLOCK_SYNC_PHASE:
        print("Sincronización de reloj: " + ("intentada como gateway" if token_present else "no intentada (sin token)"))
    return 0


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        return capture(args)
    except HardwareKitError as exc:
        print(f"ERROR: {exc}")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
