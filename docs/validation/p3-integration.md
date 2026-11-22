# P3 — Integración ESP32 → Wi-Fi → HTTP/WebSocket → gateway / web / iOS

Fecha de la evidencia física: 2026-11-22 (UTC). Firmware realtime **0.4.1**,
API HTTP **0.3.1**, Protocol v1 **1.0.0**.

## Matriz de estado

| Componente | Implementado | Compilado | Probado en host | Probado en ESP32 físico | Probado en iPhone físico |
|---|---|---|---|---|---|
| Firmware v0.4.1 (`_realtime`) | Sí | Sí (local y CI, 5/5 entornos) | Sí (17 programas, ASan/UBSan en servidor y sesión) | **Sí** (18/18) | — |
| Imagen v0.3.1 (`_rf`) | Sin cambios | Sí | Sí | Sí (P2, flasheo previo) | — |
| Gateway (`tools/sentinel_gateway.py`) | Sí | n/a (Python) | Sí (12 tests, emulador con código real del firmware) | **Sí** | — |
| Web (`apps/web`, Arc) | Sí | Sí (Vite) | Sí (82 tests Vitest) | **Sí** (Chromium headless vía gateway contra el ESP32) | — |
| SentinelKit (Swift) | Sí | Sí (CI Linux `swift:6.0` y macOS) | Sí (32 tests XCTest en CI) | — | No |
| App iOS SwiftUI | Sí | Sí (CI macOS: `BUILD SUCCEEDED` en simulador y dispositivo, **sin firmar**) | Tests de la app solo compilados | Sin firmar, no se ha ejecutado | **No**: requiere macOS, Xcode y Apple ID (ver `docs/ios/README.md`) |

Nada de esto está listo para producción.

## Defectos encontrados y corregidos

| # | Hallazgo | Evidencia | Corrección |
|---|---|---|---|
| 1 | El ESP32 flasheado con v0.4.0 nunca podía sincronizar el reloj: sin token ni NTP, el WebSocket no emitía nada | Análisis del código y `secrets.h` (solo nombres de macros) | Token local generado (fuera de Git) y rol `ios` autenticado como autoridad de tiempo (ADR-0006) |
| 2 | Un `sessionId` entrante distinto del del firmware se rechazaba, y el cliente no puede conocer ese valor de antemano | `session.cpp` | El `sessionId` pertenece al cliente y se liga al primer frame |
| 3 | Un socket sin handshake ocupaba un slot sin límite de tiempo | `ws_server.cpp` | Plazo de 5 s → `408`. **En placa:** 5,01 s |
| 4 | Un FIN TCP sin Close retenía el slot hasta 60 s | Hallazgo P2 | `recv(MSG_PEEK)`. **En placa:** slot liberado en < 1 s |
| 5 | Un cliente pasivo era expulsado a los 60 s | `ws_server.cpp` | Ping cada 20 s. **En placa:** cliente pasivo vivo 75 s con 3 pings |
| 6 | Temporal `Peer{}` de ~3 KiB en la pila de 8 KiB, anidado sobre búferes locales | Análisis estático | Reset en sitio y búferes como miembros. **En placa:** mínimo 5 348 B de pila libres. Hipótesis compatible con el reinicio de P2, **no demostrada** |
| 7 | Primera prueba física: `droppedFrames=1` al sincronizar | `hw-integration-rc1-summary.json` | Fases escalonadas y cola de 3. **En placa:** 0 descartes en 411 frames |
| 8 | Inundación → un error por frame | Revisión | Un `RATE_LIMITED` por ventana |
| 9 | Sin diagnóstico de reset, stack ni fragmentación | Hallazgo P2 | `resetReason`, `minLoopStackFreeBytes`, `largestFreeBlockBytes`, `clockSource` y contadores |
| 10 | Web: medidor de pila con textos fijos en inglés | Captura de Chromium | Sustituido por `Progress` de Arc con etiquetas en español |

## Evidencia física (ESP32-S3 N16R8 rev 0.2, `/dev/ttyACM0`, 1a86:55d3)

Flasheo de v0.4.1 con `tools/p2_flash_authorized.sh --release v0.4.1`:

- Se escribieron 4 regiones y las 4 verificaron su hash.
- Bootloader, tabla de particiones y `boot_app0` son idénticos byte a byte a
  v0.3.1. Lo comprueba `tools/p2_stage_release.py`.
- NVS (0x9000–0xDFFF) no se escribió.
- El backup de 16 MiB se verificó por SHA-256 antes de flashear. No se restauró
  ni se borró la flash.

Resultado `docs/validation/p3-artifacts/hw-integration-summary.json`, generado
por `tools/p3_hw_integration.py` (**18/18 PASS**):

| Comprobación | Resultado |
|---|---|
| `firmwareVersion` 0.4.1, `fw`/`httpApiVersion` 0.3.1, `/api/state` intacto | PASS |
| Handshake sin petición → `408` | PASS, 5,01 s |
| Gateway con token sincroniza el reloj | PASS, `clockSource=peer` |
| Rol `ios` con token aceptado; `gateway` sin token → `UNAUTHORIZED` | PASS |
| FIN sin Close libera el slot | PASS (3 → 2 clientes, `remoteCloses` 0 → 1) |
| Cliente pasivo más allá del límite de inactividad | PASS, 75 s, 3 pings |
| Cliente pasivo recibe telemetría | PASS: 75 `rf.telemetry`, 16 heartbeats, 5 `system.status` |
| Proxy del gateway y web servida | PASS |
| Web renderiza datos del dispositivo (`0.4.1`, `bootId`) | PASS (`web-dashboard-hw.png`) |
| Soak 24 × 10 s, mismo `bootId`, uptime monótono, reloj sincronizado | PASS; heap 203 936–203 948 B, pila mínima 5 348 B |
| 411 frames validados contra los esquemas JSON | PASS, 0 errores |
| `droppedFrames` = `droppedTelemetry` = 0 | PASS (`rejectedMessages=1` es el `gateway` sin token, intencional) |

Duración total en placa: unos 6,5 min. **No** es un soak prolongado. Pendiente:
una prueba de horas, la pérdida de Wi-Fi con el iPhone y la reconexión desde la
app iOS.

La salida de la app por `Serial` va al USB CDC nativo, no al puente CH343, así
que por `/dev/ttyACM0` solo se ven los mensajes de la ROM. El arranque tras el
flasheo mostró `rst:0x1 (POWERON)` (reset por EN/RTS) y `entry 0x403c88b8`.

## Integración en host

- `python3 tools/check_software.py`: repositorio, Conventional Commits, 17
  programas C++, gateway (12 tests), sintaxis de los dashboards embebidos y
  workflow n8n.
- `build/host_device`: emulador del endpoint con el código real de
  `RealtimeServer` sobre TCP loopback.
- `cd apps/web && npm test`: 82 tests. Contrato: los 9 ejemplos válidos, los 16
  inválidos y las 8 tramas del firmware.
- CI `iOS app`: `swift test` (32 tests) en Linux y macOS, y `xcodebuild build`
  para simulador y dispositivo sin firmar.

## Reproducir la prueba física

Este equipo tiene una sola interfaz Wi-Fi: unirse al AP corta internet durante
la prueba. El envoltorio vuelve siempre a la red anterior.

```bash
python3 -m venv build/venv && build/venv/bin/pip install -r packages/protocol/requirements.txt
(cd apps/web && npm ci && npm run build)
EV=~/sentinel-p2-evidence/v041-$(date -u +%Y%m%dT%H%M%SZ); mkdir -p "$EV"
trap 'nmcli con up <red-habitual>' EXIT
nmcli -w 25 con up SENTINEL-LAB && sleep 3
build/venv/bin/python tools/p3_hw_integration.py "$EV"
```

## Riesgos abiertos

- La causa del reinicio no solicitado de P2 sigue sin identificarse. Desde ahora
  `resetReason` permite distinguir pánico o watchdog de un reset externo.
- La app iOS no se ha ejecutado nunca. No hay IPA firmada ni prueba en el
  iPhone 12. `Authorization` es una cabecera reservada de URLSession; funciona
  en la práctica con WebSocket, pero no está verificado en el dispositivo.
- La web no se ha probado en un navegador interactivo ni en Safari iOS; solo en
  Chromium headless.
- Sin TLS. Los `POST` de laboratorio de v0.3.1 siguen sin autenticación (red
  aislada).
- Los binarios contienen credenciales locales; `firmware.bin` nunca se publica.
- RSSI no es distancia; `estimatedDistance` exige calibración física.
