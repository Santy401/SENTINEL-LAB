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

- `protocolVersion`, `type`, `timestamp`, `sequence`, `source` y `payload`.
- Nueve tipos de mensaje cubriendo movimiento, RF, estado, eventos, ciclo de
  conexión y errores.
- JSON Schema Draft 2020-12 con validación estricta.
- Versionado SemVer con aceptación exacta de la versión declarada.
- Límite de 16 KiB por sobre y rechazo de simulación en mediciones.

El contrato se publica en `packages/protocol/` y se documenta en
`docs/protocol-contract.md`. El firmware v0.3.1 no se modifica.

## Consecuencias

- Los adaptadores futuros pueden implementarse por separado en cada componente.
- Las pruebas de esquema detectan cambios accidentales antes de integration.
- Los clientes deben declarar y respetar una versión exacta.
- Se añade `jsonschema` solo como dependencia de la herramienta de validación.
- La compatibilidad HTTP v0.3.1 se preserva sin reescribir el firmware.
- La compatibilidad en ejecución queda pendiente de P2.

## Alternativas rechazadas

- Reutilizar los payloads HTTP tal cual: acoplaría el contrato a una ruta y a un
  transporte concretos.
- Definir el contrato solo en documentación: no detectaría regresiones.
- Implementar un validador propio: más código y mayor riesgo que usar JSON
  Schema con una biblioteca establecida.
- Cambiar el firmware durante P1: rompería la estabilidad de v0.3.1.
