#!/usr/bin/env python3
"""Evaluate P2 hardware evidence and emit a Spanish operator report."""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from hw_common import (
    CLOCK_SYNC_PHASE,
    EVIDENCE_FILE,
    HTTP_ALTERNATE_PATHS,
    HTTP_READ_PATHS,
    MANIFEST_FILE,
    PROTOCOL_ROOT,
    REDACTED_KEYS,
    REPORT_FILE,
    REPORT_MD_FILE,
    REALTIME_STATUS_PATH,
    SUBPROTOCOL,
    WS_PORT,
    clean_evidence_value,
    parse_utc_ms,
    stale_fixture_files,
    stale_fixture_refs,
    verify_evidence,
)

GATE_NAMES = (
    "wifi.ap_visibility",
    "http.v031_read_routes",
    "ws.sentinel_v1_handshake",
    "protocol.all_envelopes_valid",
    "clock.gateway_sync_and_plausible",
    "rf.rssi_from_associated_clients",
    "events.connect_disconnect",
    "recovery.ws_and_wifi",
    "runtime.heap_and_stability",
)

GATE_LABELS = {
    "wifi.ap_visibility": "Visibilidad del AP Wi-Fi",
    "http.v031_read_routes": "Rutas HTTP v0.3.1 de lectura",
    "ws.sentinel_v1_handshake": "Handshake WebSocket sentinel.v1",
    "protocol.all_envelopes_valid": "Validez de todos los sobres v1",
    "clock.gateway_sync_and_plausible": "Sincronización de reloj por gateway y plausibilidad",
    "rf.rssi_from_associated_clients": "RSSI de clientes asociados",
    "events.connect_disconnect": "Eventos de conexión y desconexión",
    "recovery.ws_and_wifi": "Recuperación WebSocket y Wi-Fi",
    "runtime.heap_and_stability": "Heap y estabilidad en ejecución",
}

STATUS_FIELDS = {
    "fw": str,
    "realtimeVersion": str,
    "bootId": str,
    "uptimeMs": int,
    "clockSynced": bool,
    "clientCount": int,
    "freeHeapBytes": int,
    "minFreeHeapBytes": int,
    "heapSamples": int,
    "droppedFrames": int,
    "rejectedMessages": int,
    "droppedTelemetry": int,
    "skippedSimulatedEvents": int,
}

WARNINGS = (
    "RSSI no determina una distancia exacta; ninguna puerta usa RSSI como medición de distancia.",
    "Los sobres Protocol v1 requieren reloj UTC válido; sin sincronización no se acepta telemetría.",
    "build/firmware-*.json y packages/protocol/examples/** son fixtures: nunca constituyen evidencia de hardware.",
    "Cualquier payload con simulated=true, simulation=true o fuente Simulated se excluye y fuerza FAIL.",
    "HTTP y WebSocket se usan solo en la red de laboratorio; no se exponen a internet.",
    "El token se lee únicamente de SENTINEL_HW_BEARER; nunca se imprime ni se persiste.",
    "Una puerta sin evidencia se marca NOT_VERIFIED y el resultado global es FAIL.",
    "Este informe no acredita hardware hasta que todas las puertas estén en PASS.",
)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Verifica la evidencia P2 y emite report.json e INFORME.md en español."
    )
    parser.add_argument("--dir", required=True, type=Path, help="Directorio de evidencia")
    parser.add_argument("--ssid", required=True, help="SSID esperado del AP")
    parser.add_argument("--fw", required=True, help="Versión de firmware esperada")
    parser.add_argument("--protocol", required=True, help="Versión de Protocol esperada")
    parser.add_argument(
        "--max-clock-skew-ms",
        type=int,
        default=5000,
        help="Desviación máxima admitida entre reloj del host y sobres (ms)",
    )
    args = parser.parse_args(argv)
    if args.max_clock_skew_ms < 0:
        parser.error("--max-clock-skew-ms debe ser mayor o igual que cero")
    return args


def gate(status: str, reason: str, observed: dict[str, Any] | None = None) -> dict[str, Any]:
    return {"status": status, "reason": reason, "observed": observed or {}}


def load_validator() -> tuple[Any | None, str | None]:
    path = PROTOCOL_ROOT / "validate_protocol.py"
    try:
        spec = importlib.util.spec_from_file_location("sentinel_protocol_validator", path)
        if spec is None or spec.loader is None:
            return None, "no se pudo cargar validate_protocol.py"
        module = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = module
        spec.loader.exec_module(module)
        validate_message = getattr(module, "validate_message", None)
        if not callable(validate_message):
            return None, "validate_message no está disponible"
        return validate_message, None
    except Exception as exc:  # dependency failures must become NOT_VERIFIED, not a silent pass
        return None, f"{type(exc).__name__}: {exc}"


def is_int(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def body_of(record: dict[str, Any]) -> dict[str, Any]:
    body = record.get("bodyJson")
    return body if isinstance(body, dict) else {}


def payload_of(record: dict[str, Any]) -> dict[str, Any]:
    envelope = record.get("envelope")
    payload = envelope.get("payload") if isinstance(envelope, dict) else None
    return payload if isinstance(payload, dict) else {}


def _contains_secret(node: Any, secret: str) -> bool:
    if isinstance(node, dict):
        return any(_contains_secret(value, secret) for value in node.values())
    if isinstance(node, (list, tuple)):
        return any(_contains_secret(value, secret) for value in node)
    return isinstance(node, str) and bool(secret) and secret in node


def secret_issues(record: dict[str, Any], secret: str, line: int) -> list[str]:
    issues: list[str] = []

    def walk(node: Any, path: str) -> None:
        if isinstance(node, dict):
            for key, value in node.items():
                normalized = str(key).lower()
                if normalized in REDACTED_KEYS and value not in (None, "[REDACTED]", True, False):
                    issues.append(f"registro {line}: clave sensible sin redactar en {path}.{key}")
                walk(value, f"{path}.{key}" if path else str(key))
        elif isinstance(node, (list, tuple)):
            for index, value in enumerate(node):
                walk(value, f"{path}[{index}]")

    walk(record, "")
    if secret and _contains_secret(record, secret):
        issues.append(f"registro {line}: posible token de la sesión presente en la evidencia")
    return issues


def prepare_evidence(
    evidence_dir: Path, integrity: dict[str, Any], secret: str
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    usable: list[dict[str, Any]] = []
    excluded: list[dict[str, Any]] = []
    for line, record in enumerate(integrity.get("records", []), 1):
        integrity["issues"].extend(secret_issues(record, secret, line))
        refs = stale_fixture_refs(record)
        if refs:
            integrity["issues"].append(f"registro {line}: referencia a fixture obsoleto en {', '.join(refs)}")
            continue
        clean, was_excluded = clean_evidence_value(record)
        if was_excluded:
            integrity["issues"].append(
                f"registro {line}: marcador de simulación; no es evidencia de hardware"
            )
            excluded.append({"line": line, "reason": "marcador de simulación en el payload"})
            continue
        usable.append(clean)
    for relative in stale_fixture_files(evidence_dir):
        integrity["issues"].append(f"archivo de fixture obsoleto en el directorio: {relative}")
    integrity["excludedRecords"] = excluded
    integrity["ok"] = not integrity["issues"]
    return usable, excluded


def by_kind(records: list[dict[str, Any]], kind: str) -> list[dict[str, Any]]:
    return [record for record in records if record.get("kind") == kind]


def http_records(records: list[dict[str, Any]], path: str) -> list[dict[str, Any]]:
    return [record for record in by_kind(records, "http") if record.get("path") == path]


def http_ok(records: list[dict[str, Any]], path: str) -> list[dict[str, Any]]:
    return [record for record in http_records(records, path) if record.get("status") == 200]


def accepted_http(records: list[dict[str, Any]], path: str) -> list[dict[str, Any]]:
    """Return successful observations for a required path, allowing documented aliases."""
    if path == "/motion/status":
        direct = http_ok(records, path)
        if direct:
            return direct
        return [record for alias in HTTP_ALTERNATE_PATHS for record in http_ok(records, alias)]
    return http_ok(records, path)


def envelope_records(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [record for record in by_kind(records, "wsFrame") if record.get("isProtocolEnvelope")]


def envelopes_of_type(records: list[dict[str, Any]], message_type: str) -> list[dict[str, Any]]:
    return [record for record in envelope_records(records) if record.get("envelopeType") == message_type]


def wifi_gate(records: list[dict[str, Any]], args: argparse.Namespace) -> dict[str, Any]:
    wifi = by_kind(records, "wifi")
    if not wifi or not any(record.get("available") for record in wifi):
        return gate("NOT_VERIFIED", "No hay observación nmcli disponible; no se confirma visibilidad del AP.")
    networks = [network for record in wifi for network in record.get("networks", [])]
    matches = [
        network
        for network in networks
        if str(network.get("ssid", "")).upper() == args.ssid.upper()
    ]
    observed = {
        "ssidObservado": args.ssid if matches else None,
        "redesObservadas": len(networks),
        "coincidencias": len(matches),
        "bssids": sorted({network.get("bssid") for network in matches if network.get("bssid")}),
        "canales": sorted({network.get("channel") for network in matches if network.get("channel")}),
    }
    if not matches:
        return gate("FAIL", "nmcli estuvo disponible pero no observó el SSID esperado.", observed)
    observed["senalesNmcli"] = sorted({network.get("signal") for network in matches if network.get("signal")})
    return gate("PASS", "Se observó el SSID esperado mediante nmcli en modo pasivo.", observed)


def http_gate(records: list[dict[str, Any]], args: argparse.Namespace) -> dict[str, Any]:
    missing = [path for path in HTTP_READ_PATHS if not accepted_http(records, path)]
    if missing:
        return gate("NOT_VERIFIED", "Faltan observaciones HTTP para: " + ", ".join(missing))
    failures: list[str] = []
    observed: dict[str, Any] = {}
    for path in HTTP_READ_PATHS:
        successes = accepted_http(records, path)
        observed[path] = {
            "respuestas200": len(successes),
            "rutas": sorted({str(record.get("path")) for record in successes}),
        }
        if not successes:
            failures.append(f"{path}: sin respuesta HTTP 200")
            continue
        if path in ("/", "/motion/native", "/api/export.csv") and not any(
            record.get("bodyText") for record in successes
        ):
            failures.append(f"{path}: cuerpo vacío")
        if path in ("/api/state", "/api/rf", "/api/lab", "/api/events", "/motion/status"):
            bodies = [record.get("bodyJson") for record in successes]
            if not any(isinstance(body, dict) for body in bodies):
                failures.append(f"{path}: sin cuerpo JSON válido")

    firmware_versions: dict[str, Any] = {}
    for path in ("/api/rf", "/api/state", "/motion/status"):
        versions = [
            body_of(record).get("fw")
            for record in accepted_http(records, path)
            if isinstance(record.get("bodyJson"), dict)
        ]
        firmware_versions[path] = sorted({str(value) for value in versions})
        if not versions:
            failures.append(f"{path}: campo fw no observado")
        elif any(value != args.fw for value in versions):
            failures.append(f"{path}: fw distinto del esperado {args.fw}")
    observed["fwPorRuta"] = firmware_versions

    if failures:
        return gate("FAIL", "; ".join(failures), observed)
    return gate(
        "PASS",
        "Todas las rutas v0.3.1 respondieron 200 con cuerpo y fw coherente; "
        "el estado de movimiento acepta el alias /api/motion/status.",
        observed,
    )


def ws_gate(records: list[dict[str, Any]]) -> dict[str, Any]:
    handshakes = by_kind(records, "wsHandshake")
    if not handshakes:
        return gate("NOT_VERIFIED", "No hay observación del handshake WebSocket.")
    successful = [
        record
        for record in handshakes
        if record.get("connected") is True and record.get("ok") is True
    ]
    observed = {
        "intentos": len(handshakes),
        "correctos": len(successful),
        "puerto": WS_PORT,
        "subprotocoloSolicitado": SUBPROTOCOL,
        "versionSolicitada": "13",
        "subprotocoloAceptado": [record.get("protocolAccepted") for record in successful],
    }
    if not successful:
        return gate("FAIL", "El handshake no cumplió 101/Accept/Upgrade/Subprotocolo.", observed)
    return gate("PASS", "Handshake 101 con Accept válido y subprotocolo sentinel.v1.", observed)


def protocol_gate(
    records: list[dict[str, Any]], args: argparse.Namespace
) -> tuple[dict[str, Any], dict[str, Any]]:
    validation: dict[str, Any] = {
        "validatorAvailable": False,
        "validatorError": None,
        "envelopesChecked": 0,
        "envelopeErrors": [],
    }
    validate_message, error = load_validator()
    validation["validatorError"] = error
    validation["validatorAvailable"] = validate_message is not None
    if validate_message is None:
        return (
            gate(
                "NOT_VERIFIED",
                "El validador canónico no está disponible; instale packages/protocol/requirements.txt en un venv.",
            ),
            validation,
        )

    envelopes = envelope_records(records)
    if not envelopes:
        return gate("NOT_VERIFIED", "No se recibieron sobres Protocol v1 por WebSocket."), validation

    malformed = [
        record
        for record in by_kind(records, "wsFrame")
        if record.get("looksLikeEnvelope") and not record.get("isProtocolEnvelope")
    ]
    errors: list[str] = []
    types: set[str] = set()
    for record in envelopes:
        envelope = record.get("envelope")
        types.add(str(record.get("envelopeType")))
        validation["envelopesChecked"] += 1
        if not isinstance(envelope, dict):
            errors.append(f"trama {record.get('frameIndex')}: sobre no es objeto")
            continue
        if envelope.get("protocolVersion") != args.protocol:
            errors.append(
                f"trama {record.get('frameIndex')}: protocolVersion {envelope.get('protocolVersion')!r} "
                f"distinto de {args.protocol}"
            )
        try:
            found = validate_message(envelope)
        except Exception as exc:  # validator must be explicit about failures
            found = [f"{type(exc).__name__}: {exc}"]
        for message in found[:3]:
            errors.append(f"trama {record.get('frameIndex')}: {message}")
    for record in malformed:
        errors.append(f"trama {record.get('frameIndex')}: JSON con aspecto de sobre no válido")

    validation["envelopeErrors"] = errors
    observed = {
        "sobresRecibidos": len(envelopes),
        "tipos": sorted(types),
        "tramasSospechosas": len(malformed),
        "errores": len(errors),
    }
    if errors:
        return gate("FAIL", "Al menos un sobre recibido no supera la validación canónica.", observed), validation
    return gate("PASS", "Todos los sobres recibidos superan el validador canónico.", observed), validation


def clock_gate(
    records: list[dict[str, Any]], args: argparse.Namespace
) -> dict[str, Any]:
    attempts = [record for record in by_kind(records, "clockSync") if record.get("attempted")]
    if not attempts:
        return gate("NOT_VERIFIED", "No hay intento registrado de sincronización como gateway.")
    attempt = attempts[0]
    failures: list[str] = []
    if attempt.get("phase") != CLOCK_SYNC_PHASE:
        failures.append(f"fase de sincronización {attempt.get('phase')!r} no es {CLOCK_SYNC_PHASE}")
    if attempt.get("token_present") is not True:
        failures.append("el intento no registra token_present=true")
    if attempt.get("role") != "gateway":
        failures.append("el intento no registra el rol gateway")
    if attempt.get("wireField") != "timestamp":
        failures.append("el sobre de sincronización no usa el campo wire timestamp")
    host_epoch = attempt.get("hostEpochMs")
    host_parsed = parse_utc_ms(attempt.get("hostTimestamp"))
    if not is_int(host_epoch) or host_parsed is None or host_epoch != host_parsed:
        failures.append("marca de tiempo UTC del host ausente, inválida o incoherente")

    statuses = http_ok(records, REALTIME_STATUS_PATH)
    if not statuses:
        return gate(
            "NOT_VERIFIED",
            "Hay intento de sincronización pero no hay observación válida de /api/realtime/status.",
            {"token_present": attempt.get("token_present"), "fase": attempt.get("phase")},
        )
    if not any(
        isinstance(record.get("bodyJson"), dict) and body_of(record).get("clockSynced") is True
        for record in statuses
    ):
        failures.append("/api/realtime/status no observó clockSynced=true")
    for record in statuses:
        body = record.get("bodyJson")
        if not isinstance(body, dict):
            failures.append("/api/realtime/status sin cuerpo JSON")
            continue
        if body.get("fw") != args.fw:
            failures.append(f"fw {body.get('fw')!r} distinto del esperado")
        if not isinstance(body.get("realtimeVersion"), str) or not body["realtimeVersion"]:
            failures.append("realtimeVersion ausente o no textual")

    hellos = envelopes_of_type(records, "connection.hello")
    matching: list[int] = []
    for record in hellos:
        envelope = record.get("envelope") or {}
        payload = payload_of(record)
        if payload.get("role") != "gateway":
            failures.append(f"trama {record.get('frameIndex')}: connection.hello sin rol gateway")
            continue
        if not isinstance(payload.get("deviceId"), str) or not payload["deviceId"]:
            failures.append(f"trama {record.get('frameIndex')}: connection.hello sin deviceId")
        if payload.get("supportedVersion") != args.protocol:
            failures.append(
                f"trama {record.get('frameIndex')}: supportedVersion distinto de {args.protocol}"
            )
        if not isinstance(payload.get("capabilities"), list):
            failures.append(f"trama {record.get('frameIndex')}: capabilities no es lista")
        observed_epoch = parse_utc_ms(envelope.get("timestamp"))
        if observed_epoch is None or not is_int(host_epoch):
            continue
        if abs(observed_epoch - host_epoch) <= args.max_clock_skew_ms:
            matching.append(observed_epoch - host_epoch)
    if not matching:
        failures.append("ningún connection.hello concuerda con la marca UTC del host dentro de la tolerancia")

    implausible = 0
    for record in envelope_records(records):
        envelope = record.get("envelope") or {}
        envelope_epoch = parse_utc_ms(envelope.get("timestamp"))
        capture_epoch = parse_utc_ms(record.get("capturedAt"))
        if envelope_epoch is None or capture_epoch is None:
            failures.append("sobre o registro sin marca UTC analizable")
            continue
        if abs(envelope_epoch - capture_epoch) > args.max_clock_skew_ms:
            implausible += 1
    if implausible:
        failures.append(f"{implausible} sobres con desviación superior a {args.max_clock_skew_ms} ms")

    observed = {
        "fase": attempt.get("phase"),
        "token_present": attempt.get("token_present"),
        "campoEnSobre": attempt.get("wireField"),
        "epochMsHost": host_epoch,
        "desviacionesHelloMs": matching,
        "sobresConDesviacionExcesiva": implausible,
        "clockSyncedObservado": [
            body_of(record).get("clockSynced") for record in statuses
        ],
    }
    if failures:
        return gate("FAIL", "; ".join(sorted(set(failures))), observed)
    return gate("PASS", "Reloj sincronizado por gateway y sobres plausibles respecto al host.", observed)


def rf_gate(records: list[dict[str, Any]], args: argparse.Namespace) -> dict[str, Any]:
    http_observations = http_ok(records, "/api/rf")
    v1_observations = envelopes_of_type(records, "rf.telemetry")
    if not http_observations or not v1_observations:
        return gate(
            "NOT_VERIFIED",
            "Se requieren simultáneamente observaciones HTTP /api/rf y sobres v1 rf.telemetry.",
            {"http": len(http_observations), "v1": len(v1_observations)},
        )
    failures: list[str] = []
    http_rssi: list[int] = []
    for record in http_observations:
        body = record.get("bodyJson")
        if not isinstance(body, dict) or body.get("available") is not True:
            failures.append("/api/rf no observó available=true")
            continue
        devices = body.get("devices")
        if not isinstance(devices, list):
            failures.append("/api/rf sin lista devices")
            continue
        values = [
            device.get("rssiDbm")
            for device in devices
            if isinstance(device, dict) and device.get("connected") is True
        ]
        valid = [value for value in values if is_int(value) and -100 <= value <= -1]
        if not valid:
            failures.append("/api/rf sin RSSI entero de cliente asociado")
        http_rssi.extend(valid)

    v1_rssi: list[int] = []
    for record in v1_observations:
        payload = payload_of(record)
        clients = payload.get("clients")
        if not isinstance(clients, list):
            failures.append("rf.telemetry sin lista clients")
            continue
        if payload.get("clientCount") != len(clients):
            failures.append("rf.telemetry con clientCount incoherente")
        if payload.get("simulated") is not False:
            failures.append("rf.telemetry no declara simulated=false")
        if payload.get("firmwareVersion") != args.fw:
            failures.append("rf.telemetry con firmwareVersion distinto del esperado")
        values = [
            client.get("rssiDbm")
            for client in clients
            if isinstance(client, dict) and client.get("connected") is True
        ]
        valid = [value for value in values if is_int(value) and -100 <= value <= -1]
        if not valid:
            failures.append("rf.telemetry sin RSSI de cliente asociado")
        v1_rssi.extend(valid)

    observed = {
        "httpRssiDbm": http_rssi,
        "v1RssiDbm": v1_rssi,
        "clientesHttp": [body_of(record).get("devices") for record in http_observations],
        "clientesV1": [payload_of(record).get("clientCount") for record in v1_observations],
    }
    if failures:
        return gate("FAIL", "; ".join(sorted(set(failures))), observed)
    return gate("PASS", "RSSI real observado en clientes asociados por HTTP y por v1.", observed)


def events_gate(records: list[dict[str, Any]]) -> dict[str, Any]:
    http_observations = http_ok(records, "/api/events")
    v1_observations = envelopes_of_type(records, "event")
    if not http_observations or not v1_observations:
        return gate(
            "NOT_VERIFIED",
            "Se requieren eventos en /api/events y sobres v1 event.",
            {"http": len(http_observations), "v1": len(v1_observations)},
        )
    required = {"DEVICE_CONNECTED", "DEVICE_DISCONNECTED"}
    http_names: set[str] = set()
    boot_ids: set[str] = set()
    for record in http_observations:
        body = record.get("bodyJson")
        if isinstance(body, dict):
            if isinstance(body.get("bootId"), str):
                boot_ids.add(body["bootId"])
            for event in body.get("events", []):
                if isinstance(event, dict) and event.get("simulated") is not True:
                    if isinstance(event.get("event"), str):
                        http_names.add(event["event"])
    v1_names: set[str] = set()
    for record in v1_observations:
        payload = payload_of(record)
        if payload.get("simulated") is not False:
            continue
        if isinstance(payload.get("name"), str):
            v1_names.add(payload["name"])
    rf_boot_ids = {
        body_of(record).get("bootId")
        for record in http_ok(records, "/api/rf")
        if isinstance(record.get("bodyJson"), dict)
    }
    observed = {
        "eventosHttp": sorted(http_names),
        "eventosV1": sorted(v1_names),
        "bootIds": sorted(value for value in boot_ids if value),
    }
    failures: list[str] = []
    if not required <= http_names:
        failures.append("faltan eventos HTTP de conexión o desconexión")
    if not required <= v1_names:
        failures.append("faltan sobres v1 de conexión o desconexión")
    if len(boot_ids | rf_boot_ids) > 1:
        failures.append("bootId inconsistente entre eventos y /api/rf")
    if failures:
        return gate("FAIL", "; ".join(failures), observed)
    return gate("PASS", "Conexión y desconexión observadas por HTTP y por Protocol v1.", observed)


def recovery_gate(records: list[dict[str, Any]], args: argparse.Namespace) -> dict[str, Any]:
    required_phases = {"pre", "associated", "disconnected", "reconnected", "soak"}
    phases = {record.get("phase") for record in records}
    marker_phases = {record.get("phase") for record in by_kind(records, "operatorAction")}
    if not required_phases <= phases or not {"disconnected", "reconnected"} <= marker_phases:
        return gate(
            "NOT_VERIFIED",
            "Faltan fases o marcadores de operador para desconexión y reconexión.",
            {"fases": sorted(str(value) for value in phases if value)},
        )
    ws_after = [
        record
        for record in by_kind(records, "wsHandshake")
        if record.get("connected") is True and record.get("phase") in ("reconnected", "soak")
    ]
    wifi_after = [
        network
        for record in by_kind(records, "wifi")
        if record.get("phase") in ("reconnected", "soak")
        for network in record.get("networks", [])
        if str(network.get("ssid", "")).upper() == args.ssid.upper()
    ]
    boot_ids = {
        body_of(record).get("bootId")
        for record in http_ok(records, "/api/rf")
        if isinstance(record.get("bodyJson"), dict)
    }
    observed = {
        "handshakesTrasReconexion": len(ws_after),
        "apVisibleTrasReconexion": len(wifi_after),
        "bootIds": sorted(str(value) for value in boot_ids if value),
    }
    failures: list[str] = []
    if not ws_after:
        failures.append("no hay handshake correcto tras la reconexión")
    if not wifi_after:
        failures.append("no se observó el AP tras la reconexión")
    if len(boot_ids) > 1:
        failures.append("bootId cambió: hubo reinicio, no recuperación de sesión")
    if failures:
        return gate("FAIL", "; ".join(failures), observed)
    return gate("PASS", "AP y WebSocket recuperados tras desconexión sin cambio de bootId.", observed)


def runtime_gate(records: list[dict[str, Any]], args: argparse.Namespace) -> dict[str, Any]:
    statuses = http_ok(records, REALTIME_STATUS_PATH)
    if not statuses:
        return gate("NOT_VERIFIED", "No hay observación 200 de /api/realtime/status (solo build realtime).")
    if len(statuses) < 2:
        return gate("NOT_VERIFIED", "Se requieren al menos dos observaciones de salud para medir estabilidad.")
    failures: list[str] = []
    observed: dict[str, Any] = {"observaciones": len(statuses), "rutas": REALTIME_STATUS_PATH}
    for record in statuses:
        body = record.get("bodyJson")
        if not isinstance(body, dict):
            failures.append("/api/realtime/status sin cuerpo JSON")
            continue
        missing = [field for field in STATUS_FIELDS if field not in body]
        if missing:
            failures.append("campos ausentes: " + ", ".join(missing))
            continue
        for field, expected_type in STATUS_FIELDS.items():
            value = body[field]
            valid = is_int(value) if expected_type is int else type(value) is expected_type
            if not valid:
                failures.append(f"{field} con tipo inesperado")
        if body["fw"] != args.fw:
            failures.append("fw distinto del esperado")
        if not body["realtimeVersion"]:
            failures.append("realtimeVersion vacío")
        if not body["clockSynced"]:
            failures.append("clockSynced=false en la ruta de salud")
        if body["freeHeapBytes"] <= 0 or body["minFreeHeapBytes"] <= 0:
            failures.append("heap libre o mínimo no positivo")
        if body["minFreeHeapBytes"] > body["freeHeapBytes"]:
            failures.append("minFreeHeapBytes mayor que freeHeapBytes")
        if body["heapSamples"] < 2:
            failures.append("heapSamples menor que dos muestras")
        for field in (
            "uptimeMs",
            "clientCount",
            "freeHeapBytes",
            "minFreeHeapBytes",
            "heapSamples",
            "droppedFrames",
            "rejectedMessages",
            "droppedTelemetry",
            "skippedSimulatedEvents",
        ):
            if not is_int(body[field]) or body[field] < 0:
                failures.append(f"{field} no es entero no negativo")
        for field in ("droppedFrames", "rejectedMessages", "droppedTelemetry"):
            if body[field] != 0:
                failures.append(f"{field}={body[field]} (se espera cero durante la validación)")

    ordered = sorted(statuses, key=lambda record: record.get("capturedAt") or "")
    uptimes = [
        body_of(record).get("uptimeMs")
        for record in ordered
        if isinstance(record.get("bodyJson"), dict)
    ]
    boot_ids = {
        body_of(record).get("bootId")
        for record in statuses
        if isinstance(record.get("bodyJson"), dict)
    }
    if len(uptimes) >= 2 and any(
        not is_int(current) or not is_int(previous) or current < previous
        for previous, current in zip(uptimes, uptimes[1:])
    ):
        failures.append("uptimeMs no es monótono")
    if len(boot_ids) > 1:
        failures.append("bootId cambió entre observaciones de salud")
    observed.update(
        {
            "uptimeMs": uptimes,
            "bootIds": sorted(str(value) for value in boot_ids if value),
            "freeHeapBytes": [
                body_of(record).get("freeHeapBytes") for record in ordered
            ],
            "minFreeHeapBytes": [
                body_of(record).get("minFreeHeapBytes") for record in ordered
            ],
            "heapSamples": [body_of(record).get("heapSamples") for record in ordered],
            "droppedFrames": [body_of(record).get("droppedFrames") for record in ordered],
            "rejectedMessages": [body_of(record).get("rejectedMessages") for record in ordered],
            "droppedTelemetry": [body_of(record).get("droppedTelemetry") for record in ordered],
            "skippedSimulatedEvents": [
                body_of(record).get("skippedSimulatedEvents") for record in ordered
            ],
        }
    )
    if failures:
        return gate("FAIL", "; ".join(sorted(set(failures))), observed)
    return gate("PASS", "Ruta de salud completa, heap positivo, uptime monótono y sin pérdidas.", observed)


def render_markdown(report: dict[str, Any]) -> str:
    lines = [
        "# Informe P2 de validación de hardware",
        "",
        f"**Resultado global: {report['overall']}**",
        "",
        f"- Generado: {report['generatedAt']}",
        f"- Directorio de evidencia: `{report['inputs']['evidenceDir']}`",
        f"- SSID esperado: `{report['inputs']['ssid']}`",
        f"- Firmware esperado: `{report['inputs']['fw']}`",
        f"- Protocol esperado: `{report['inputs']['protocol']}`",
        f"- Tolerancia de reloj: {report['inputs']['maxClockSkewMs']} ms",
        "",
        "## Integridad de la evidencia",
        "",
        f"- Manifiesto presente: `{str(report['integrity']['manifestPresent']).lower()}`",
        f"- SHA256 de evidence.jsonl: `{report['integrity'].get('evidenceSha256Computed')}`",
        f"- Registros leídos: {report['integrity']['recordCount']}",
        f"- Registros excluidos por simulación: {len(report['integrity']['excludedRecords'])}",
        f"- Problemas: {len(report['integrity']['issues'])}",
        "",
    ]
    for issue in report["integrity"]["issues"][:20]:
        lines.append(f"  - {issue}")
    lines.extend(
        [
            "",
            "## Puertas de aceptación",
            "",
            "| Puerta | Estado | Detalle |",
            "|---|---|---|",
        ]
    )
    for name in GATE_NAMES:
        result = report["gates"][name]
        lines.append(f"| `{name}` | {result['status']} | {result['reason']} |")
    lines.extend(["", "## Observaciones por puerta", ""])
    for name in GATE_NAMES:
        result = report["gates"][name]
        lines.extend(
            [
                f"### {GATE_LABELS[name]} (`{name}`)",
                "",
                f"- Estado: **{result['status']}**",
                f"- Motivo: {result['reason']}",
                "",
                "```json",
                json.dumps(result["observed"], ensure_ascii=False, indent=2, sort_keys=True),
                "```",
                "",
            ]
        )
    validation = report["validation"]
    lines.extend(
        [
            "## Validación del protocolo",
            "",
            f"- Validador disponible: `{str(validation['validatorAvailable']).lower()}`",
            f"- Sobres comprobados: {validation['envelopesChecked']}",
            f"- Errores de sobre: {len(validation['envelopeErrors'])}",
            "",
            "## Reglas anti-fabricación",
            "",
        ]
    )
    lines.extend(f"- {warning}" for warning in WARNINGS)
    if report["overall"] == "PASS":
        conclusion = "La evidencia observada cumple todas las puertas. El hardware queda documentado como validado solo para esta sesión y esta imagen."
    else:
        conclusion = "No se puede afirmar que el hardware esté validado. Una puerta FAIL o NOT_VERIFIED invalida la aceptación; repita la captura con evidencia real."
    lines.extend(["", "## Conclusión", "", conclusion, ""])
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    evidence_path = args.dir / EVIDENCE_FILE
    manifest_path = args.dir / MANIFEST_FILE
    integrity = verify_evidence(evidence_path, manifest_path)
    records, _ = prepare_evidence(args.dir, integrity, os.environ.get("SENTINEL_HW_BEARER", ""))

    if not integrity["ok"]:
        gates = {
            name: gate("NOT_VERIFIED", "Integridad de evidencia inválida; no se evalúa la puerta.")
            for name in GATE_NAMES
        }
        validation = {
            "validatorAvailable": False,
            "validatorError": None,
            "envelopesChecked": 0,
            "envelopeErrors": [],
        }
    else:
        gates = {}
        gates["wifi.ap_visibility"] = wifi_gate(records, args)
        gates["http.v031_read_routes"] = http_gate(records, args)
        gates["ws.sentinel_v1_handshake"] = ws_gate(records)
        gates["protocol.all_envelopes_valid"], validation = protocol_gate(records, args)
        gates["clock.gateway_sync_and_plausible"] = clock_gate(records, args)
        gates["rf.rssi_from_associated_clients"] = rf_gate(records, args)
        gates["events.connect_disconnect"] = events_gate(records)
        gates["recovery.ws_and_wifi"] = recovery_gate(records, args)
        gates["runtime.heap_and_stability"] = runtime_gate(records, args)

    overall = (
        "PASS"
        if integrity["ok"] and all(gates[name]["status"] == "PASS" for name in GATE_NAMES)
        else "FAIL"
    )
    report: dict[str, Any] = {
        "generatedAt": datetime.now(timezone.utc).isoformat(),
        "tool": "hw_report.py",
        "inputs": {
            "evidenceDir": str(args.dir),
            "ssid": args.ssid,
            "fw": args.fw,
            "protocol": args.protocol,
            "maxClockSkewMs": args.max_clock_skew_ms,
        },
        "overall": overall,
        "integrity": {
            "ok": integrity["ok"],
            "issues": integrity["issues"],
            "manifestPresent": integrity["manifestPresent"],
            "evidencePresent": integrity["evidencePresent"],
            "evidenceSha256Computed": integrity.get("evidenceSha256Computed"),
            "recordCount": len(integrity["records"]),
            "excludedRecords": integrity["excludedRecords"],
        },
        "validation": validation,
        "gates": gates,
        "warnings": list(WARNINGS),
    }

    report_path = args.dir / REPORT_FILE
    markdown_path = args.dir / REPORT_MD_FILE
    args.dir.mkdir(parents=True, exist_ok=True)
    report_path.write_text(
        json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True, allow_nan=False) + "\n",
        encoding="utf-8",
    )
    markdown_path.write_text(render_markdown(report), encoding="utf-8")

    print(f"Resultado global: {overall}")
    for name in GATE_NAMES:
        print(f"- {GATE_LABELS[name]}: {gates[name]['status']}")
    print(f"Informe: {markdown_path}")
    print(f"Datos: {report_path}")
    return 0 if overall == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
