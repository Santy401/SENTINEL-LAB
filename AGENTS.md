# Instrucciones para agentes

SENTINEL-LAB es un proyecto de experimentación física, telemetría en tiempo real
y automatización. Estas reglas aplican a personas y agentes automatizados.

## Idioma

- Código, identificadores, comentarios técnicos, mensajes de commit, ramas y
  GitHub Actions: **inglés**.
- ADR, guías de usuario, arquitectura, física e informes: **español**.
- No mezclar identificadores en español e inglés.

## Seguridad

- Nunca publicar credenciales, tokens, contraseñas, certificados ni archivos
  privados.
- `include/secrets.h`, `.env` y equivalentes están prohibidos en Git.
- Solo se versionan plantillas `*.example`.
- No imprimir valores secretos en logs, respuestas ni informes.
- No exponer endpoints administrativos a internet.
- HTTP/WebSocket local son aceptables solo en la red de laboratorio; documentar
  sus límites.

## Flujo de trabajo

- `main` es estable; desarrollar en ramas.
- Usar Conventional Commits.
- Cada cambio coherente lleva pruebas y documentación cuando aplique.
- No hacer force push, borrar historial ni merge automático de PR.
- No flashear hardware sin autorización explícita.
- No desplegar n8n, Telegram ni servicios de producción sin autorización.
- No presentar código sin compilar como compilado, ni simulación como medición
  real.

## Estado de la plataforma

Antes de cambiar código, consultar `README.md`, `docs/validation/` y
`build/rf-validation.json`. Diferenciar siempre:

- Planned.
- Implemented.
- Software tested.
- Hardware tested.
- Production ready.

## Arquitectura

- El firmware v0.3.1 es funcional y debe permanecer intacto hasta que exista un
  reemplazo probado.
- La API HTTP v0.3.1 se mantiene durante la migración a v0.4.0.
- `MotionEngine` y `MotionIngest` no se eliminan sin reemplazo validado.
- El ESP32 debe seguir operando aunque no haya cliente web conectado.
- RSSI no determina una distancia exacta; toda calibración necesita medición
  física real.
