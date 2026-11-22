# Changelog

Todos los cambios relevantes se registrarán aquí siguiendo
[Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/) y versiones
SemVer cuando exista una release oficial.

## [Unreleased]

### Added

- Fundación del repositorio SENTINEL-LAB.
- Gobernanza, plantillas GitHub y workflows base de CI.
- Auditoría P0 e inventario del firmware funcional.
- Contrato de protocolo v1.0.0 con esquemas estrictos y validador.
- Ejemplos válidos, casos negativos y workflow de validación de protocolo.
- Núcleo SENTINEL Core v0.4.0: WebSocket local con Protocol v1, telemetría RF,
  estado de movimiento, eventos, latidos y despedida ordenada.
- Entorno `sentinel_esp32s3_n16r8_realtime`, ADR-0005, guía `docs/realtime-core.md`
  e informe `docs/validation/p2-software.md`.
- Pruebas host del codec, reloj, colas, autenticación, handshake, adaptador,
  sesiones y ciclo de vida del servidor WebSocket.
- Kit reproducible de evidencia física P2: `tools/hw_common.py` (UTC, hashes,
  redacción, HTTP, Wi-Fi pasivo y WebSocket RFC 6455 mínimo),
  `tools/hw_capture.py` (fases, marcadores y manifiesto) y
  `tools/hw_report.py` (verificación e informe `INFORME.md`).
- Guías operatorias `docs/validation/p2-hardware.md` y
  `docs/hardware/recovery.md` con preflight, capturas, informe, flasheo
  autorizado, rollback y reglas anti-fabricación.
- Documentación del contrato de 13 campos de `GET /api/realtime/status`, solo
  presente en el entorno `_realtime`.
- Kit portable P2 para Arch Linux: `tools/p2_make_kit.py`, preflight de solo
  lectura, comprobación de puerto serie, HTTP y WebSocket, y script de escritura
  con confirmación literal sin borrado de NVS.
- Inventario de recuperabilidad del respaldo v0.3.1, registro de compilación del
  commit P2 y matriz de pruebas físicas con estados `PASS`/`FAIL`/`BLOCKED`/
  `NOT TESTED`.
- Verificador de solo lectura `tools/p2_layout_check.py` para offsets, tabla de
  particiones y no superposición con NVS.
- Helper `tools/p2_esptool.sh` con detección robusta de esptool y validación de
  versión; script `p2_restore_full_authorized.sh` para recuperación completa
  custodiada desde la imagen original de 16 MiB, sin copiar ni publicar la imagen.
- Documentación `docs/hardware/p2-flash-layout.md` y
  `docs/hardware/p2-full-recovery.md`.

### Changed

- Documentación de estado real del proyecto y migración por etapas.
- El sobre exige `sessionId`; la deduplicación usa `source`, `sessionId` y
  `sequence`.
- RF y eventos usan `clientId` por sesión sin publicar MAC ni inventar nombres.
- El contrato documenta el adaptador de firmware como implementado y probado en
  software; la web, el gateway y la app iOS siguen pendientes.
- La documentación del handshake refleja que el firmware valida la ruta exacta,
  los tokens `Upgrade`/`Connection`, la clave base64 canónica,
  `Sec-WebSocket-Version: 13` y el token `sentinel.v1`, con respuesta `426` para
  otras versiones; aclara que el reloj se sincroniza con el campo de sobre
  `timestamp`, no con un campo `epochMs`.
- Arquitectura y contrato reflejan el kit P2 y mantienen el estado de hardware
  como `untested` hasta un informe PASS sobre captura real.
- El kit elimina dependencias de rutas absolutas del servidor de compilación e
  incluye el validador y los esquemas de protocolo para funcionar desde Arch.
- `--fw 0.3.1` queda documentado como versión de la API HTTP; se añade
  `--realtime-version 0.4.0` para `hw_report.py` y `--expected-realtime` para
  `p2_http_check.py`.

### Fixed

- Los workflows de firmware copian la plantilla de Wi-Fi en el runner de CI sin
  publicar credenciales reales.
- El validador rechaza valores no finitos también al llamar directamente a
  `validate_message()`.
- `rf.telemetry` admite `clientId` y RSSI `null`, y exige coherencia entre
  `clientCount` y `clients`.
- `motion.status` deja de aceptar fuentes simuladas como medición real.
- Los esquemas usan `$id` HTTPS resolubles y la validación de fechas emplea
  `rfc3339-validator`.
- Los errores no devuelven el payload completo y el CI exige que cada caso
  negativo sea rechazado.
- `hw_report.py` ya no compara `firmwareVersion` de Protocol v1 con la versión
  HTTP `0.3.1`: usa `realtimeVersion`/`0.4.0`, evitando rechazar telemetría v1
  válida.
- `p2_flash_authorized.sh` ya no usa `pio pkg exec --tool esptool@…`, opción
  inexistente en PlatformIO 6.2.0; detecta `esptool`, `esptool.py`,
  `python3 -m esptool` o `pio pkg exec -p tool-esptool -c …`, y selecciona
  `write-flash` en esptool 5.x.
- La recuperación completa coloca `--flash-size 16MB` después del subcomando
  `write-flash`, como exige esptool 5.6.0.
- El servidor libera la ranura de un par que cierra localmente en lugar de
  retenerla hasta el timeout.
- La despedida del cliente se confirma antes de cerrar el socket y las tramas
  consecutivas en una sola lectura se procesan por separado.

### Security

- Verificación automática de secretos y rutas prohibidas antes de publicar.
- El contrato exige `simulated: false` en todos los mensajes de medición.
- Bearer token opcional para el rol `gateway`; sin token no se puede sincronizar
  el reloj por WebSocket. La plantilla pública no contiene credenciales.
- El firmware no inventa timestamps UTC: no emite sobres v1 hasta obtener una
  fecha válida de NTP local o de un gateway autenticado.
- El kit P2 lee el token solo desde `SENTINEL_HW_BEARER`, registra únicamente
  `token_present`, nunca imprime ni persiste `Authorization`, y excluye fixtures
  y payloads simulados de la evidencia.

### Validation

- 17 programas host, 18 pruebas de contrato, 9 ejemplos válidos, 16 casos
  negativos y 8 fixtures del firmware validados.
- Los cinco entornos PlatformIO compilan; RAM y flash estáticas se registran en
  `docs/validation/p2-software.md`.
- Sin pruebas de placa, heap en ejecución ni radio.
- Kit P2: `py_compile` de los tres módulos y `hw_report.py --help` ejecutados en
  host; no se capturó hardware ni se generó un informe PASS real.

## v0.3.1

### Added

- Receptor nativo de Sensor Logger mediante HTTP Push.
- `MotionIngest` con emparejamiento acelerómetro + gravedad.
- Endpoints `/motion/native`, `/api/motion/sensorlogger`, `/api/motion/status` y
  `/api/motion/config`.
- Regresión para impedir que el toast de Firefox iOS se presente como error del
  firmware.

### Hardware

- Firmware v0.3.1 flasheado y verificado.
- AP `SENTINEL-LAB` verificado tras el reset.
- Ingesta real con Sensor Logger reportada por el usuario.
