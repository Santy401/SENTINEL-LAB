# Gateway local SENTINEL-LAB

`tools/sentinel_gateway.py` es un proceso Python de un solo archivo, sin
dependencias externas, que se ejecuta en el portátil del laboratorio conectado
al AP `SENTINEL-LAB`. Justificación en ADR-0006.

## Funciones

| Función | Detalle |
|---|---|
| Autoridad de tiempo | Sesión `gateway` autenticada; `hello` sincroniza el reloj del ESP32, heartbeat cada 15 s lo re-sincroniza |
| Reconexión | Backoff exponencial con jitter completo, máximo 30 s |
| Web | Sirve `apps/web/dist` con fallback SPA |
| Proxy | `GET`/`HEAD /api/*` → `http://<device>/api/*`, 4 s de timeout, 64 KiB máximo; cualquier otro método → `405` |
| Estado | `GET /gateway/config`, `GET /gateway/status` (nunca contiene el token) |
| Evidencia | `--log-jsonl` guarda los frames recibidos, redactados |

## Uso

```bash
# Token: archivo con permisos 600 (el gateway rechaza permisos más abiertos).
ls -l ~/.config/sentinel-lab/realtime-token

# Web (una vez)
cd apps/web && npm ci && npm run build && cd ../..

# Conectado al AP SENTINEL-LAB:
python3 tools/sentinel_gateway.py \
  --token-file ~/.config/sentinel-lab/realtime-token \
  --log-jsonl ~/sentinel-p2-evidence/gateway.jsonl
# Abrir http://127.0.0.1:8080
```

Opciones: `--device 192.168.4.1`, `--port 8080`, `--bind 127.0.0.1` (no
expongas el gateway fuera del equipo), `--web-root`, `--no-link` (solo web y
proxy, sin sesión de tiempo).

El token debe coincidir con `SENTINEL_REALTIME_BEARER_TOKEN` de
`include/secrets.h` con el que se compiló el firmware.

## Pruebas

`python3 tools/tests/test_gateway.py -v` (incluido en
`tools/check_software.py`):

- Unitarias: backoff, envelopes válidos según los esquemas, permisos del token y
  ausencia del token en el estado.
- HTTP: proxy, rechazo de escrituras, estáticos, fallback SPA y path traversal.
- Integración con `build/host_device`, que ejecuta el `RealtimeServer` **real**
  del firmware sobre TCP loopback: sincronización con token, sin frames con un
  token incorrecto y reconexión tras reiniciar el dispositivo. Es emulación en
  host, no evidencia de hardware.

## Límites

- HTTP y WebSocket sin TLS: solo en la red del laboratorio.
- Un único dispositivo por proceso.
- Si el gateway y el iPhone están conectados a la vez, ambos re-sincronizan el
  reloj; la última muestra gana. Ambos usan hora del sistema sincronizada por
  NTP de su sistema operativo.
