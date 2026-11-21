# ADR-0002 · Migración incremental a monorepo

Estado: aceptado
Fecha: 2026-11-21

## Contexto

El proyecto local tiene firmware funcional, pruebas, documentación e
integraciones en una única raíz. La estructura objetivo propone `apps/`,
`firmware/`, `services/`, `packages/` y dominios de documentación.

## Decisión

Migrar por etapas sin mover archivos ciegamente. P0 conserva la estructura actual
y añade gobernanza, CI y auditoría. Las siguientes etapas introducirán protocolo,
firmware, web, gateway e iOS con compatibilidad verificada.

## Consecuencias

- El firmware v0.3.1 sigue compilando con `platformio.ini` existente.
- Las rutas relativas de pruebas y scripts no se rompen.
- Cada movimiento deberá verificar imports, PlatformIO y CI.
- La estructura final no bloquea el trabajo actual.

## Alternativas rechazadas

- Mover todo en un solo commit: riesgo alto de romper compilación y pruebas.
- Crear directorios vacíos: no aporta valor verificable.
