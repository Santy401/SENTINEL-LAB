# ADR-0003 · Compatibilidad HTTP v0.3.1 durante v0.4.0

Estado: aceptado
Fecha: 2026-11-21

## Contexto

La telemetría en tiempo real exigirá WebSocket y comunicación bidireccional, pero
el dashboard actual, los procedimientos de calibración y la evidencia física
dependen de la API HTTP v0.3.1.

## Decisión

Mantener la API HTTP v0.3.1 durante la migración a SENTINEL Core v0.4.0. El
transporte WebSocket será adicional, no sustituto inmediato.

## Consecuencias

- El dashboard actual seguirá funcionando durante el desarrollo.
- `MotionEngine` y `MotionIngest` no se sustituyen sin reemplazo probado.
- La nueva capa deberá probarse en paralelo.
- El firmware debe seguir operativo sin cliente web conectado.

## Alternativas rechazadas

- Cortar HTTP en v0.4.0: rompería evidencia y procedimientos existentes.
- Reescribir el motor de movimiento y RF al mismo tiempo: riesgo innecesario.
