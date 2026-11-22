# ADR-0006: Autoridad de tiempo, gateway local y clientes

- Estado: aceptado
- Fecha: 2026-11-22
- Relacionado: ADR-0003, ADR-0004, ADR-0005

## Contexto

La auditoría posterior al primer flasheo de v0.4.0 mostró que el ESP32 físico
**no podía emitir ningún mensaje Protocol v1**. El adaptador exige un reloj UTC
sincronizado, y las dos fuentes previstas no existían: ni había un token bearer
en `include/secrets.h` (así que ningún gateway autenticado era posible) ni había
un servidor NTP en la red del AP. El handshake funcionaba y no llegaba ningún
frame, igual que en la prueba de P2.

Además, el validador exigía que el `sessionId` entrante coincidiera con el del
firmware. El cliente solo puede conocer ese valor por el primer frame del
servidor, y ese frame necesita un reloj sincronizado. Un cliente conforme al
esquema (que exige `sessionId`) quedaba bloqueado.

Restricciones:

- Un navegador no puede enviar `Authorization` en un WebSocket.
- El firmware HTTP no envía cabeceras CORS y la API v0.3.1 no debe cambiar.
- La red del AP no tiene internet; la arquitectura no puede depender de la nube.
- El iPhone tiene una hora fiable y puede guardar un token en Keychain.

## Decisión

1. **El `sessionId` entrante pertenece al cliente.** El primero aceptado se
   liga a la conexión (igual que `source`); un cambio posterior, o retirarlo
   después de haberlo enviado, se rechaza con `UNAUTHORIZED`. Es coherente con
   la clave de deduplicación `source + sessionId + sequence` del emisor.
2. **La autoridad de tiempo es un rol autenticado, no un proceso.** Un `hello`
   con rol `gateway` **o** `ios` sobre un handshake con bearer válido sincroniza
   el reloj; los heartbeats de esa sesión lo re-sincronizan. Sin token, `ios` es
   un cliente de solo lectura y `gateway` recibe `UNAUTHORIZED`.
3. **Gateway separado, mínimo y opcional**: `tools/sentinel_gateway.py`, solo
   biblioteca estándar de Python. Hace tres cosas: es la autoridad de tiempo
   cuando no hay iPhone, sirve la web y actúa de proxy de solo lectura
   (`GET /api/*`) para resolver la falta de CORS. Escucha en `127.0.0.1` por
   defecto. Sin base de datos ni servicios de nube.
4. **Web de solo lectura** (`apps/web`, Arc Library). Conecta el WebSocket
   directamente al ESP32 y toma la hora del gateway o del iPhone.
5. **iOS es autónomo**: con token en Keychain, la app sincroniza el reloj sin
   gateway; sin token, muestra el estado HTTP y explica por qué no hay stream.

## Addendum v0.4.2 (2026-11-22)

- La web compilada (`apps/web`) se incrusta en la imagen realtime con
  `tools/embed_web.py` y `board_build.embed_files`, y se sirve en `/` (gzip,
  ~239 KB). El panel v0.3.1 pasa a `/legacy` en esa imagen; `_rf` no cambia.
- Un navegador puede ser autoridad de tiempo ofreciendo el subprotocolo
  `sentinel.auth.<token>`. Con ello, el gateway deja de ser necesario para el uso
  desde el móvil; sigue siendo útil para registrar evidencia y cuando ningún
  cliente tiene token.

## Consecuencias

- El ESP32 sigue funcionando sin ningún cliente: HTTP v0.3.1 intacto y el
  WebSocket acepta conexiones, aunque no emite v1 hasta la primera
  sincronización.
- El token protege la sincronización del reloj, no la confidencialidad: no hay
  TLS. Sirve únicamente en la red aislada del laboratorio.
- Los binarios `firmware.bin` incluyen la contraseña del AP y el token; no se
  publican (`tools/p2_stage_release.py` lo advierte).
- El reloj de la placa se re-ancla cada 15 s mientras haya una sesión
  privilegiada. Sin ella, deriva con el cristal del ESP32 hasta el reinicio.
- Cambio de comportamiento del protocolo sin cambio de esquema: Protocol v1
  sigue en `1.0.0`; firmware realtime pasa a `0.4.1`.
