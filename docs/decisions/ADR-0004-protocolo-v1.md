# ADR-0004 · Contrato de protocolo compartido v1.0.0

Estado: propuesto

Fecha: 2026-11-21

## Contexto

El firmware v0.3.1 expone HTTP local para movimiento, RF, eventos y estado. La
evolución hacia v0.4.0 añadirá WebSocket, y el gateway, la web y la app iOS
necesitarán interpretar los mismos datos. Sin un contrato común, cada cliente
podría inventar nombres, unidades o reglas de error incompatibles.

## Decisión

Definir un sobre JSON versionado y transport-independent con:

- `protocolVersion`, `type`, `timestamp`, `sequence`, `source`, `sessionId` y
  `payload`. La terna `source` + `sessionId` + `sequence` evita colisiones entre
  reinicios y reconexiones.
- Nueve tipos de mensaje cubriendo movimiento, RF, estado, eventos, ciclo de
  conexión y errores.
- JSON Schema Draft 2020-12 con `$id` HTTPS resolubles, registro local publicado
  y validación estricta.
- Versionado SemVer con aceptación exacta de la versión declarada y comprobación
  cruzada en `connection.hello`.
- Límite de 16 KiB sobre bytes de transporte, rechazo de simulación en
  mediciones y exclusión de fuentes simuladas en `motion.status`.
- Identificación RF por `clientId` por sesión y `bootId`; sin MAC ni nombres de
  dispositivo inventados.
- Nombres de evento alineados con `EventEngine` y errores correlacionados por
  `relatedSource` + `relatedSequence`.

El contrato se publica en `packages/protocol/` y se documenta en
`docs/protocol-contract.md`. El firmware v0.3.1 no se modifica.

## Consecuencias

- Los adaptadores futuros pueden implementarse por separado en cada componente.
- Las pruebas de esquema detectan cambios accidentales antes de integration.
- Los clientes deben declarar y respetar una versión exacta.
- Se añaden `jsonschema` y `rfc3339-validator` solo como dependencias de la
  herramienta de validación.
- La compatibilidad HTTP v0.3.1 se preserva sin reescribir el firmware.
- La compatibilidad en ejecución queda pendiente de P2, incluidos heap pico,
  latencia, colas acotadas y retroceso en el ESP32.
- La revisión de P1 corrigió NaN en la API directa, MAC obligatoria en RF,
  deduplicación sin sesión, simulación mal etiquetada, esquemas no resolubles y
  comprobaciones de formato inactivas.

## Alternativas rechazadas

- Reutilizar los payloads HTTP tal cual: acoplaría el contrato a una ruta y a un
  transporte concretos.
- Definir el contrato solo en documentación: no detectaría regresiones.
- Implementar un validador propio: más código y mayor riesgo que usar JSON
  Schema con una biblioteca establecida.
- Cambiar el firmware durante P1: rompería la estabilidad de v0.3.1.
