# Validación de software · SENTINEL Core v0.4.0 (P2)

Fecha: 2026-11-21  
Alcance: implementación local del núcleo en tiempo real sobre el firmware RF
v0.3.1. **No se flasheó hardware, no se midió heap en ejecución y no se realizaron
pruebas de radio.** Los resultados son de compilación y pruebas host.

## 1. Estado

| Elemento | Estado |
|---|---|
| Contrato Protocol v1.0.0 | Implementado y probado en host |
| Adaptador y transporte en firmware | Implementado y probado en host |
| API HTTP v0.3.1 | Sin cambios; compila en su entorno original |
| Imagen de producción v0.3.1 | Sigue siendo `_rf`; `_realtime` es experimental |
| Ruta de salud `GET /api/realtime/status` | Implementada en el entorno `_realtime`; sin medición en placa |
| Kit de evidencia física P2 | Implementado y probado en host (sintaxis, manifiestos e informador) |
| Placa, radio y calibración física | Sin validar; no existe informe P2 en PASS |

## 2. Comandos ejecutados

```bash
python3 tools/check_software.py
python3 scripts/check-repository.py
python3 scripts/check-conventional-commit.py --self-test
python3 -c 'import yaml,glob; [yaml.safe_load(open(p)) for p in glob.glob(".github/workflows/*.yml")]'
python3 packages/protocol/validate_protocol.py packages/protocol/examples/valid
python3 packages/protocol/validate_protocol.py --expect-invalid packages/protocol/examples/invalid
python3 -m unittest discover -s packages/protocol/tests -v
python3 packages/protocol/validate_protocol.py build/firmware-*.json
python3 -m py_compile tools/hw_common.py tools/hw_capture.py tools/hw_report.py \
  tools/p2_preflight.py tools/p2_serial_check.py tools/p2_http_check.py \
  tools/p2_ws_probe.py tools/p2_make_kit.py tools/p2_layout_check.py
bash -n tools/p2_esptool.sh tools/p2_flash_authorized.sh \
  tools/p2_restore_full_authorized.sh
python3 tools/hw_report.py --help
python3 tools/p2_layout_check.py --kit ~/p2-hardware-kit
for env in sentinel_esp32s3_n16r8 sentinel_esp32s3_n16r8_diag \
           sentinel_esp32s3_n16r8_v02 sentinel_esp32s3_n16r8_rf \
           sentinel_esp32s3_n16r8_realtime; do
  pio run -e "$env"
done
```

Las comprobaciones del validador se ejecutaron desde un entorno virtual aislado
ya instalado; el intérprete del sistema no trae `jsonschema`. En una máquina
limpia, créese un venv e instale `packages/protocol/requirements.txt`. No se
versionaron ni imprimieron credenciales.

## 3. Resultados

| Comprobación | Resultado |
|---|---|
| `tools/check_software.py` | 17 programas host OK (incluye salud heap y ruta HTTP con/sin `_realtime`), comprobaciones JS/DOM y sintaxis Python OK |
| Pruebas unitarias del contrato | 18 OK |
| Ejemplos válidos del contrato | 9 OK |
| Casos negativos del contrato | 16 rechazados |
| Fixtures generados por el firmware | 8 OK |
| `scripts/check-repository.py` | OK (201 archivos) |
| Conventional Commits self-test | OK (7 casos) |
| Parse YAML de workflows | OK |
| Entornos PlatformIO | 5/5 SUCCESS |
| Sintaxis del kit P2 (`py_compile`) | 9/9 OK |
| Sintaxis de scripts Bash del kit | 3/3 OK |
| `python3 tools/hw_report.py --help` | OK |
| `p2_layout_check.py` sobre el kit | 21 PASS, 0 FAIL |
| Detección de esptool | `esptool (PATH)` v5.6.0 → `write-flash` |

La cobertura host añadida por P2 incluye codec RFC 6455 (incluidas dos tramas en
una sola lectura y tramas sobredimensionadas), origen de tiempo y bloqueo hasta
sincronización, cola acotada con contador de descartes, autenticación bearer,
handshake, adaptador y validador de sobres entrantes, sesiones monótonas y
únicas por reconexión, estados/capacidades generados por la misma política del
productor, `deviceId` conforme al esquema y el ciclo de vida completo del
servidor WebSocket: saludo, `gateway` sin token, texto malformado, trama
oversized, límite de cuatro pares, saludo pendiente hasta la sincronización,
`goodbye` y cierre.

## 4. Tamaños de compilación

Valores del enlazador de PlatformIO; **no son mediciones de heap en ejecución**.

| Entorno | RAM usada | Flash usada | Estado |
|---|---:|---:|---|
| `sentinel_esp32s3_n16r8` | 24 052 B / 327 680 B (7,3 %) | 307 495 B / 6 553 600 B (4,7 %) | OK |
| `sentinel_esp32s3_n16r8_diag` | 24 084 B / 327 680 B (7,3 %) | 308 115 B / 6 553 600 B (4,7 %) | OK |
| `sentinel_esp32s3_n16r8_v02` | 49 900 B / 327 680 B (15,2 %) | 968 899 B / 6 553 600 B (14,8 %) | OK |
| `sentinel_esp32s3_n16r8_rf` | 72 092 B / 327 680 B (22,0 %) | 1 049 979 B / 6 553 600 B (16,0 %) | OK |
| `sentinel_esp32s3_n16r8_realtime` | 86 084 B / 327 680 B (26,3 %) | 1 079 603 B / 6 553 600 B (16,5 %) | OK |

Incremento estático de `_realtime` frente a `_rf`: **+13 992 B de RAM** y
**+29 624 B de Flash**. Quedan 241 596 B de RAM declarados por el enlazador,
pero parte se consume en ejecución por el stack FreeRTOS, el heap dynamic y las
asignaciones HTTP. No se ha medido el pico real.

No se afirma reproducibilidad byte a byte entre compilaciones separadas: el
hash P0/P1 de `_rf` quedó registrado en la evidencia de hardware, y la
recompilación actual conserva el mismo tamaño y la misma superficie de fuentes,
pero no se usa el hash como criterio de compatibilidad.

## 5. Compatibilidad HTTP v0.3.1

- Ninguna ruta HTTP existente se renombró, movió ni eliminó.
- Todos los cambios en archivos compartidos están dentro de
  `#ifdef SENTINEL_REALTIME`; el entorno `_rf` no define esa macro.
- `_realtime` extiende `_rf` y añade `src/realtime/*` más `-DSENTINEL_REALTIME=1`.
- Los cuatro entornos previos siguen compilando con sus filtros originales.
- `/api/rf` continúa exponiendo `lastSeenMs` relativo. El campo `lastSeen` de v1
  solo se deriva del reloj sincronizado; no se inventa fecha civil.
- `clientCount` de v1 coincide con la longitud de `clients` emitida; no se
  publica MAC.
- Los eventos simulados y los estados de movimiento simulados quedan fuera de
  la superficie v1.

## 6. Seguridad y límites verificados en software

- Handshake en `ws://<AP-IP>:81/realtime`, subprotocolo `sentinel.v1`.
- Cuatro pares, 1024 B por trama, dos tramas por cola de salida, ocho mensajes
  entrantes por segundo y par, 60 s de inactividad.
- Tramas fragmentadas, sin máscara, con opcode desconocido o sobredimensionadas
  se rechazan; los pares desconectados liberan su ranura.
- El bearer token se lee de `include/secrets.h` y la plantilla lo deja vacío.
  Sin token, el rol `gateway` no puede sincronizar el reloj. Con token vacío no
  existen operaciones privilegiadas; la telemetría local sigue legible.
- El firmware no inicia emisiones v1 hasta recibir una fecha válida de NTP local
  o de un `gateway` autenticado. Sin reloj no hay sobres con timestamps falsos.
- El muestreo de heap y la ruta de salud se prueban en host con y sin
  `SENTINEL_REALTIME`: la ruta existe solo en `_realtime` y expone 13 campos;
  el muestreo ocurre inclusive antes de la sincronización del reloj.
- Estos límites protegen el AP local de laboratorio; no son una defensa frente a
  Internet. No se probaron con tráfico hostil real.

## 7. Kit de validación de hardware (P2)

Se añadió un kit reproducible, sin dependencias nuevas:

- `tools/hw_common.py`: UTC real, hashes SHA256 por registro y del archivo,
  redacción de credenciales, cliente WebSocket RFC 6455 mínimo con
  `socket`/`base64`/`hashlib`/`os.urandom`, lecturas HTTP y Wi-Fi pasivo con
  `nmcli`.
- `tools/hw_capture.py`: captura por fases `pre`, `associated`, `disconnected`,
  `reconnected` y `soak`; escribe `evidence.jsonl` y `manifest.json`. Solo en la
  fase `associated`, y solo si `SENTINEL_HW_BEARER` está definido, envía un
  saludo `gateway` con `Authorization` y la marca UTC del host en el campo
  `timestamp` del sobre; en evidencia local registra `hostEpochMs`.
- `tools/hw_report.py`: verifica hashes, excluye fixtures y datos simulados,
  importa `validate_message` del validador canónico cuando sus dependencias
  están instaladas y emite `report.json` e `INFORME.md` con nueve puertas.

El token nunca se imprime ni se persiste: el manifiesto guarda solo
`token_present`. `build/firmware-*.json`, `packages/protocol/examples/**` y
cualquier payload con `simulated=true`, `simulation=true`,
`sourceType: "Simulated"` o `source: "Simulated"` quedan excluidos y fuerzan
`FAIL`. La ruta `/api/realtime/status` se documenta contra el
contrato de 13 campos del entorno `_realtime`; no se ejecutó contra una placa.

Se añadió además un kit portable para Arch Linux. `tools/p2_preflight.py`,
`p2_serial_check.py`, `p2_http_check.py`, `p2_ws_probe.py` y
`p2_layout_check.py` son de solo lectura; `tools/p2_make_kit.py` ensambla los
artefactos y genera `SHA256SUMS`; y hay dos scripts de escritura con
confirmaciones literales distintas y sin opciones de borrado:
`p2_flash_authorized.sh` (recuperación parcial) y
`p2_restore_full_authorized.sh` (recuperación completa desde la imagen original,
que reemplaza NVS). La detección de esptool se centralizó en `p2_esptool.sh`,
sin fijar versiones de PlatformIO. El kit incluye el respaldo v0.3.1, la imagen
v0.4.0, el validador y esquemas de protocolo, manifiestos, actas, matriz física
y directorios de evidencia vacía. **La imagen completa de 16 MiB no se copia al
kit ni se publica.**

Se corrigió una inconsistencia de versiones: `--fw 0.3.1` designa la API HTTP,
que v0.4.0 conserva por compatibilidad, mientras `realtimeVersion` y
`firmwareVersion` de Protocol v1 valen `0.4.0`. `hw_report.py` y
`p2_http_check.py` aceptan ahora `--realtime-version` / `--expected-realtime`.
Las rutas absolutas del servidor de compilación se sustituyeron por rutas
relativas al usuario.

El flujo de verificación del kit se ejecutó solo en host: compilación Python,
ayuda del informador, preflight, comprobación de layout, detección de esptool y
comprobación de integridad. **No se capturó hardware, no se flasheó la placa y
no se generó un informe PASS real.** El procedimiento operatorio está en
`docs/validation/p2-hardware.md`, la matriz en
`docs/validation/p2-physical-matrix.md`, el kit en `docs/hardware/p2-kit.md`, los
offsets en `docs/hardware/p2-flash-layout.md` y la recuperación en
`docs/hardware/recovery.md` y `docs/hardware/p2-full-recovery.md`.

## 8. Riesgos pendientes

1. **Heap real:** no medido; el incremento del enlazador no sustituye una prueba
   de pico bajo carga.
2. **Hardware:** no se probó el AP, el WebSocket sobre Wi-Fi real ni la
   coexistencia con WebServer en placa.
3. **Reloj:** sin NTP local ni gateway autorizado, un cliente no recibe datos v1;
   esto es intencional y debe verificarse en una integración posterior.
4. **Calibración:** `estimatedDistance` se publica solo con modelo calibrado
   registrado por el usuario; no hay medición física nueva en P2.
5. **Cortes de eventos:** el sondeo RF a 1 Hz heredado puede perder asociaciones
   muy breves; P2 no cambia ese muestreo.
6. **Autenticación:** el token protege la sincronización del reloj, no sustituye
   una red de laboratorio aislada. No se probó con clientes remotos.
7. **Handshake:** el firmware valida la ruta exacta, los tokens `Upgrade` y
   `Connection`, la clave base64 canónica, `Sec-WebSocket-Version: 13` y el
   token `sentinel.v1`; responde `426` a otras versiones. No negocia
   alternativas. El kit envía y verifica ambos campos, pero la interoperabilidad
   real sigue pendiente de prueba en placa.
8. **NTP en modo AP:** si se configura un servidor NTP sin credenciales STA, la
   SNTP se inicia pero no tiene ruta; el gateway autenticado sigue siendo la
   fuente de reloj usable en ese caso.
9. **Kit P2:** sin captura real; el informador solo puede devolver PASS cuando
   exista evidencia íntegra y el validador canónico esté instalado en el venv.

## 9. Conclusión

P2 está **implemented / software tested** y listo para revisión. El kit de
evidencia física está implementado, pero no hay capturas de hardware ni informe
PASS: el estado de hardware sigue siendo **untested**. No es **production
ready** ni **hardware tested**. No se solicita fusión del PR ni se inicia P3 sin
autorización explícita.
