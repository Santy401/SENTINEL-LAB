# Panel web de SENTINEL-LAB

Panel de laboratorio de nueva generación para el ESP32-S3, en `apps/web/`.
Sustituye como interfaz principal de diagnóstico a las páginas HTML embebidas
en el firmware; estas siguen intactas en `http://192.168.4.1/` (API v0.3.1).

**Estado:** implementado, compilado y probado en host (typecheck, 82 tests de
Vitest, build de producción). **No se ha probado en un navegador real contra el
ESP32 físico** ni con el gateway: es la siguiente validación pendiente.

## Arquitectura

```
Navegador ──HTTP same-origin──> gateway (o proxy de Vite) ──> ESP32 /api/*
    │
    └──WebSocket ws://192.168.4.1:81/realtime (sentinel.v1, solo lectura)──> ESP32
```

- **HTTP**: la página pide rutas relativas (`api/state`, `api/rf`,
  `api/events`, `api/realtime/status`, `gateway/config`, `gateway/status`).
  El gateway (`tools/sentinel_gateway.py`) sirve `apps/web/dist` y hace de
  proxy hacia el ESP32; en desarrollo lo hace Vite. El firmware no envía
  cabeceras CORS, por eso la página no llama a `http://192.168.4.1` directamente.
- **WebSocket**: el navegador se conecta directamente al ESP32 (el WebSocket no
  está sujeto a CORS). La URL sale de `GET gateway/config`; si no existe, se usa
  `ws://192.168.4.1:81/realtime`.
- **Rol `web`, solo lectura**: los navegadores no pueden enviar `Authorization`
  en un WebSocket, así que la web nunca es autoridad de reloj. Envía
  `connection.hello` (rol `web`), un `connection.heartbeat` cada 15 s y
  `connection.goodbye` al cerrar; `sessionId` y `sequence` son propios y
  crecientes.
- **Reloj**: el firmware no emite tramas Protocol v1 hasta que un gateway
  autenticado o la app iOS con token sincronizan su reloj UTC. El panel lo
  indica con `clockSynced` de `/api/realtime/status` y, mientras tanto, usa el
  historial HTTP de `/api/rf` para la telemetría RF.

### Lógica (`src/lib`, sin React)

| Módulo | Función |
|---|---|
| `protocol.ts` | Validación estricta Protocol v1 (los mismos campos, rangos y `additionalProperties` que `packages/protocol/schemas`), detección de claves duplicadas, límite de 1024 B por trama, constructores de sobres del cliente |
| `api.ts` | Modelos HTTP v0.3.1 y de salud realtime; rechaza respuestas incompletas en lugar de inventar valores; acepta tanto la ruta de v0.4.0 (13 campos) como la de v0.4.1 |
| `realtime-client.ts` | WebSocket con subprotocolo obligatorio, reconexión exponencial con jitter (1 s → 30 s), heartbeat, watchdog de silencio cuando el reloj está sincronizado, límite de 8 envíos/s |
| `store.ts` | Sondeo HTTP con aborto y retroceso, ingesta de tramas, buffers acotados (120 puntos RSSI por cliente, 100 eventos, 40 tramas) |
| `health.ts` | Indicadores de salud (heap, fragmentación, pila, causa de reinicio, reinicios detectados, contadores crecientes, reloj, socket) |
| `evidence.ts` | Exportación JSON no sensible; elimina claves con nombre de credencial y marca DEMO |
| `demo.ts` | Modo DEMO: reproduce las tramas generadas por los tests del firmware (`src/fixtures/firmware`) |

### Interfaz

Vistas: **Panel**, **Telemetría RF**, **Eventos**, **Diagnóstico** y **Ajustes**.
Todos los componentes visuales son de [Arc Library](https://github.com/kuratlielia/arc-library):
tokens `foundation.css` (modo claro/oscuro con `data-theme`), `theme-switch`,
`tabs`, `segmented-control`, `metric-card`, `gauge`, `progress` (pila del
loop de 8 KiB), `sparkline` (heap y RSSI por cliente), `line-chart` (RSSI),
`sortable-data-table` (clientes), `timeline` (eventos), `donut-chart` (tramas por
tipo), `toast-stack` (cambios de conexión y reinicios), `skeleton`,
`empty-state`, `alert`, `badge`, `tooltip`, `select`, `switch`,
`json-viewer` y `copy-button`. Arc respeta `prefers-reduced-motion` en todas sus
animaciones; el panel no añade bucles de animación propios.

**DEMO**: se activa en Ajustes. Reproduce fixtures del firmware con RSSI y heap
variados de forma sintética. La cabecera, un aviso fijo, la vista Dispositivo y
la evidencia exportada quedan marcados como DEMO; nunca se presentan como
medición.

## Desarrollo

Requisitos: Node.js ≥ 20.19 (CI usa 22).

```bash
cd apps/web
npm ci
npm run dev          # http://localhost:5173, proxy /api -> http://192.168.4.1
SENTINEL_DEVICE=http://192.168.4.1 npm run dev   # otra dirección del ESP32
npm run typecheck
npm test             # Vitest: contrato, cliente WS, salud, render SSR de las vistas
npm run build        # genera apps/web/dist con base relativa "./"
npm run sync-fixtures  # copia build/firmware-*.json tras python3 tools/check_software.py
```

Para usarlo con el ESP32, el equipo debe estar conectado al AP `SENTINEL-LAB`.

## Servida desde el ESP32 (v0.4.2, uso normal)

El firmware realtime incluye esta web y la sirve en `http://192.168.4.1/`. Basta
con unir el móvil o el portátil a la Wi-Fi `SENTINEL-LAB` y abrir esa URL.

1. *Ajustes → Token de tiempo real*: pega el token
   (`~/.config/sentinel-lab/realtime-token`) y pulsa *Guardar token*. Se guarda
   solo en ese navegador.
2. La web reconecta, pone en hora el ESP32 y empieza a recibir el stream.
3. Sin token también funciona, pero en solo lectura: estado y RF por HTTP, y
   stream solo si otro cliente con token (gateway o iPhone) ya puso el reloj
   en hora.

Para actualizar la web incrustada:

```bash
(cd apps/web && npm run build)
python3 tools/embed_web.py            # regenera web_assets/*.gz (determinista)
pio run -e sentinel_esp32s3_n16r8_realtime
```

El CI comprueba con `tools/embed_web.py --check` que `web_assets/` corresponde
al código de `apps/web`. El panel v0.3.1 sigue disponible en `/legacy`.

## Ejecución con el gateway

`npm run build` y después el gateway sirve `apps/web/dist` en su puerto HTTP local
junto con el proxy `/api/*` y los endpoints `/gateway/config` y `/gateway/status`.
Consulte la documentación del gateway para el comando exacto.

## Pruebas

- `test/protocol.test.ts`: acepta los 9 ejemplos válidos de
  `packages/protocol/examples/valid`, rechaza los 16 inválidos (cada uno por su
  motivo: clave duplicada, campo extra, RSSI 0, `simulated`, versión, etc.),
  acepta las 8 tramas generadas por el firmware dentro del límite de 1024 B y
  valida los sobres que envía el cliente.
- `test/fixtures-sync.test.ts`: si existe `build/`, las copias de
  `src/fixtures/firmware` deben coincidir byte a byte.
- `test/api.test.ts`: cuerpo exacto de `writeRealtimeHealth()` (v0.4.1) y de la
  ruta v0.4.0 ya flasheada; formatos de `/api/rf` y `/api/events`; timeouts.
- `test/realtime-client.test.ts`: negociación del subprotocolo, hello y
  heartbeats, tramas inválidas/desconocidas/binarias, backoff, watchdog, cierre.
- `test/logic.test.ts` y `test/render.test.tsx`: salud, evidencia, DEMO y
  render en servidor de las cinco vistas con datos y en estados vacíos.

## Límites y seguridad

- Solo para la red de laboratorio aislada. Sin autenticación, sin TLS: no
  exponer el gateway ni el ESP32 a internet.
- La web es de solo lectura: no configura el firmware ni sincroniza el reloj.
- Sin dependencias en la nube; las fuentes Geist/Inter no se descargan y se usa
  la fuente sans-serif del sistema.
- RSSI no determina una distancia exacta. La distancia solo se muestra si el
  firmware la marca como calibrada, y siempre como estimación.
- Tamaño del build: JS 703 KB (228 KB gzip), CSS 95 KB (17 KB gzip). Aceptable
  en red local; no se ha dividido en chunks.

## Atribución

Los componentes de `apps/web/src/components/arc/` son copia sin modificar de
Arc Library, © 2026 Elia Kuratli, licencia MIT (ver `LICENSE` en esa carpeta y
el commit de origen en `ARC_COMMIT`).
