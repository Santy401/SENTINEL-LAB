#!/usr/bin/env python3
"""Read-only WebSocket handshake probe for SENTINEL Core v0.4.0.

The probe opens a WebSocket connection and observes frames. It does not send a
gateway hello, does not synchronize the device clock, and does not change
configuration. A privileged token is not used unless explicitly requested.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))

import hw_common as hw  # noqa: E402


def sanitize_handshake(handshake: dict[str, Any]) -> dict[str, Any]:
    return {
        "status": handshake.get("status"),
        "upgrade": bool(handshake.get("upgrade")),
        "connectionUpgrade": bool(handshake.get("connectionUpgrade")),
        "acceptValid": bool(handshake.get("acceptValid")),
        "versionRequested": handshake.get("versionRequested"),
        "protocolRequested": handshake.get("protocolRequested"),
        "protocolAccepted": handshake.get("protocolAccepted"),
        "token_present": bool(handshake.get("token_present")),
    }


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="192.168.4.1")
    parser.add_argument("--port", type=int, default=hw.WS_PORT)
    parser.add_argument("--path", default=hw.WS_PATH)
    parser.add_argument("--seconds", type=float, default=5.0)
    parser.add_argument("--token-env", default="SENTINEL_HW_BEARER")
    parser.add_argument("--allow-gateway-token", action="store_true",
                        help="include the bearer token in the handshake; never send a gateway hello")
    parser.add_argument("--save-json", type=Path, help="optional host-side observation file")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if args.seconds <= 0 or args.seconds > 60:
        print("FAIL: --seconds debe estar entre 0 y 60")
        return 2

    token = os.environ.get(args.token_env) if args.allow_gateway_token else None
    token_text = "true" if token else "false"
    socket_client = hw.MinimalWebSocket(args.host, args.port, args.path, timeout=args.seconds)
    handshake: dict[str, Any] | None = None
    frames: list[dict[str, Any]] = []
    types: dict[str, int] = {}

    try:
        handshake = socket_client.connect(token)
        print(f"Handshake: {handshake['status']} accept={handshake['acceptValid']} "
              f"protocol={handshake.get('protocolAccepted')} token_present={token_text}")
        deadline = time.monotonic() + args.seconds
        while time.monotonic() < deadline:
            try:
                frame = socket_client.recv_frame()
            except TimeoutError:
                break
            frame_type = str(frame.get("type", "unknown"))
            types[frame_type] = types.get(frame_type, 0) + 1
            if frame_type == "text":
                payload = frame.get("payload", b"")
                try:
                    envelope = hw.parse_json(payload.decode("utf-8", errors="replace"))
                except (ValueError, TypeError):
                    envelope = None
                if isinstance(envelope, dict):
                    frames.append({
                        "type": envelope.get("type"),
                        "sequence": envelope.get("sequence"),
                        "source": envelope.get("source"),
                        "sessionId": envelope.get("sessionId"),
                    })
                    print(f"  frame: type={envelope.get('type')} seq={envelope.get('sequence')}")
            if frame_type == "close":
                break
    except hw.WebSocketHandshakeError as exc:
        detail = exc.args[0] if exc.args else {}
        if isinstance(detail, dict):
            print(f"FAIL handshake: status={detail.get('status')} "
                  f"accept={detail.get('acceptValid')} protocol={detail.get('protocolAccepted')}")
        else:
            print(f"FAIL handshake: {detail}")
        return 1
    except (hw.WebSocketError, OSError, TimeoutError) as exc:
        print(f"FAIL WebSocket: {type(exc).__name__}: {exc}")
        return 1
    finally:
        socket_client.close()

    total = sum(types.values())
    print(f"Frames observados: {total}" + (f" ({', '.join(f'{k}={v}' for k, v in sorted(types.items()))})"
                                           if types else ""))
    if not total:
        print("Sin frames: es esperado si el reloj UTC aún no está sincronizado.")

    if args.save_json and handshake is not None:
        payload = {
            "capturedAt": hw.format_utc_ms(datetime.now(timezone.utc)),
            "host": args.host,
            "port": args.port,
            "path": args.path,
            "handshake": sanitize_handshake(handshake),
            "frameCounts": types,
            "frames": frames,
        }
        args.save_json.parent.mkdir(parents=True, exist_ok=True)
        args.save_json.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n",
                                  encoding="utf-8")
        print(f"Observaciones guardadas en {args.save_json}")

    print("OK: handshake sentinel.v1 verificado; no se envió connection.hello")
    return 0


if __name__ == "__main__":
    sys.exit(main())
