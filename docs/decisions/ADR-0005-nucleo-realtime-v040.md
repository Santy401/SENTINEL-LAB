# ADR-0005 · Núcleo SENTINEL Core v0.4.0 sobre WebSocket local

Estado: implementado en software; pendiente de validación en hardware

Fecha: 2026-11-21

## Contexto

El firmware v0.3.1 ya publica estado RF, movimiento y eventos por HTTP en el AP
local. El contrato v1 aprobado en P1 define un sobre versionado, pero todavía no
existía un adaptador en el ESP32. P2 debe añadir telemetría en tiempo real sin
romper las rutas HTTP v0.3.1 ni aumentar el consumo de memoria de las imágenes
de producción existentes.

También existen tres restricciones concretas:

- El SDK Wi-Fi solo informa tiempo relativo `millis()`, no fecha civil UTC.
- `EventEngine` puede contener eventos simulados, mientras que v1 exige
  `simulated: false` en las mediciones.
- El AP admite como máximo cuatro clientes y el heap del ESP32 es limitado.

## Decisión

Añadir un entorno PlatformIO nuevo,
`sentinel_esp32s3_n16r8_realtime`, que extiende `_rf` y define
`SENTINEL_REALTIME=1`. Los entornos `sentinel_esp32s3_n16r8`, `_diag`, `_v02` y
`_rf` conservan sus filtros de fuentes y su comportamiento. Las rutas HTTP
v0.3.1 no cambian de forma; el nuevo valor `simulationRejected` solo existe
dentro de la guarda de compilación P2.

El transporte es un endpoint WebSocket mínimo y autocontenido en
`ws://<AP-IP>:81/realtime`, con subprotocolo `sentinel.v1`. Se implementa RFC
6455 de forma acotada en lugar de incorporar una biblioteca nueva: tramas de
hasta 1024 bytes, sin fragmentación, cola de dos tramas por par, cuatro pares,
límite de ocho mensajes entrantes por segundo y cierre por inactividad. Los
buffers son estáticos y el adaptador serializa cada sobre en un búfer fijo de
1016 bytes antes de copiarlo a la cola.

La identidad de cada emisor es `source` + `sessionId` + `sequence`. Los
mensajes difundidos usan una sesión de sistema derivada de `bootId`; los
saludos y errores por conexión usan la sesión de ese par. El validador
entrante rechaza versiones desconocidas, tipos no aceptados desde un cliente,
secuencias no crecientes, timestamps inválidos y roles no registrados.

El firmware **no inventa tiempo UTC**. Hasta que no haya un servidor NTP local
configurado en `include/rf/connected.h` o un cliente `gateway` autenticado
envíe un `connection.hello` con timestamp válido, el adaptador no emite ningún
sobre v1. El rol `gateway` exige un bearer token definido en el gitignored
`include/secrets.h`; si el token está vacío, las operaciones privilegiadas
quedan deshabilitadas. La telemetría de laboratorio sigue siendo legible desde
la red local sin credenciales.

La telemetría RF publica `clientId`, RSSI crudo y filtrado, `lastSeen` derivado
del reloj sincronizado y `estimatedDistance` solo cuando existe una medición de
laboratorio con modelo calibrado. No se publica MAC. Los eventos simulados se
descartan y el puente de eventos omite secuencias anteriores al inicio de la
conexión para no reenviar el anillo histórico.

## Consecuencias

- v0.3.1 sigue siendo la imagen estable; v0.4.0 se prueba por separado.
- El incremento de RAM y Flash medido por el enlazador es acotado y se registra en
  `docs/validation/p2-software.md`.
- Los clientes deben esperar el primer `connection.hello` antes de interpretar
  que el sistema no responde; puede no llegar si no hay reloj sincronizado.
- La autenticación protege la sincronización del reloj, no sustituye a una red
  de laboratorio aislada.
- Los límites están diseñados para el AP local; no son una defensa frente a
  clientes remotos o Internet.
- No se han realizado pruebas en placa, medición de heap en ejecución ni
  pruebas de radio; el estado es **software tested**, no **hardware tested**.

## Alternativas rechazadas

- Modificar el entorno `_rf`: habría cambiado el firmware v0.3.1 ya validado.
- Reservar 16 KiB por cliente: innecesario y arriesgado para el heap del ESP32.
- Usar `millis()` como timestamp UTC: produciría fechas falsas.
- Publicar eventos simulados con `simulated: true`: contradice el contrato v1.
- Exponer el WebSocket en el puerto 80: WebServer ya lo ocupa y mixes de
  protocolos dificultarían el diagnóstico.
