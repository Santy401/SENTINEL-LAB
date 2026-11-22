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

## Historial, alertas e informes (gateway 0.2.0)

### Historial SQLite

Por defecto en `~/.local/share/sentinel-lab/history.db` (permisos 600; `--db`
para cambiarlo, `--no-history` para desactivarlo). Guarda:

| Tabla | Contenido | Frecuencia |
|---|---|---|
| `sessions` | cada ejecución del gateway | 1 por arranque |
| `status_samples` | subconjunto de `/api/realtime/status` (heap, pila, reloj, RGB, agitación, `bootId`, motivo de reinicio) | cada 10 s (`--status-interval`) |
| `events` | eventos RF del ESP32 | al llegar |
| `rf_samples` | RSSI por cliente | ≤ 1/s por cliente |
| `motion_summaries` | pico, media y estados de `motion.sample` | 1 por segundo |
| `lab_snapshots` | `/api/lab` (experimentos y calibración) | cuando cambia (sondeo cada 60 s) |
| `alerts` | alertas y estado de entrega | al generarse |

Retención: 30 días o 200 MB. Al superarse se borra el 10 % más antiguo de las
tablas de volumen. Las alertas pendientes de entrega no se borran nunca.

### Reglas de alerta

| Regla | Condición | Sostenida | Recuperación | Enfriamiento | Severidad |
|---|---|---|---|---|---|
| `strong_shake` | intensidad de agitación ≥ 0,85 o `motion.sample` en `ALERT` | 1 s | intensidad < 0,5 durante 5 s | 2 min | warning |
| `phone_out_of_range` | RSSI < −85 dBm, desconectado o ausente | 10 s | RSSI > −80 dBm durante 5 s | 5 min | warning |
| `esp_reboot` | cambio de `bootId` | inmediata | — | — | critical si `PANIC`/`*WDT`/`BROWNOUT`, si no warning |
| `heap_low` | heap < 40 KB o bloque mayor < 20 KB | 20 s | heap ≥ 50 KB y bloque ≥ 30 KB durante 20 s | 10 min | critical |
| `clock_unsynced` | reloj sin UTC con clientes conectados | 2 min | al sincronizar | 10 min | warning |
| `esp_unreachable` | sin respuesta HTTP | 60 s | primera respuesta | 5 min | critical |

Límite global: 20 alertas por hora; las que lo superan se cuentan como
suprimidas (`/gateway/history/summary`). Las recuperaciones se envían con
severidad `recovery`. La intensidad de agitación combina la del estado HTTP y
la de las muestras reenviadas: gana la mayor que siga siendo reciente.

### Entrega a n8n (almacenar y reenviar)

```bash
python3 tools/sentinel_gateway.py \
  --token-file ~/.config/sentinel-lab/realtime-token \
  --alert-webhook-url https://n8n.nexuscloud.sbs/webhook/sentinel-alerts \
  --alert-secret-file ~/.config/sentinel-lab/alert-secret     # chmod 600
```

- Cada alerta se guarda primero en SQLite y se envía cuando hay internet.
  Mientras el portátil esté solo en la Wi-Fi del ESP32, quedan en cola.
- Reintentos con espera exponencial: 5 s, 10 s, 20 s… hasta un máximo de
  10 min. Errores de red y 5xx se reintentan; 400, 401, 403, 404, 410, 413
  y 422 son definitivos y la alerta queda marcada `rechazada`.
- `POST` con JSON `{id, rule, kind, severity, title, message, occurredAt,
  device{bootId, firmwareVersion}, metrics}` sobre HTTPS. Cabeceras:
  - `X-Sentinel-Timestamp` (segundos Unix).
  - `X-Sentinel-Signature` = HMAC-SHA256 en hexadecimal de
    `timestamp + "." + cuerpo`, sobre los bytes exactos enviados.
  - `User-Agent: SentinelLabGateway/<versión>`: Cloudflare rechaza el agente
    por defecto de Python (error 1010).
- El secreto se lee de un archivo con permisos 600 y no aparece en registros
  ni en `/gateway/status`. `--alert-dry-run` imprime las alertas sin enviarlas.

### Endpoints nuevos

- `GET /gateway/alerts`: últimas 50 alertas, con estado de entrega.
- `GET /gateway/history/summary`: recuento por tabla, alertas pendientes y
  suprimidas, y tamaño del archivo.
- `GET /gateway/status`: incluye `alertDelivery` con entregadas, fallos,
  rechazadas y último error.

### Informe de sesión

```bash
python3 tools/sentinel_gateway.py report --list
python3 tools/sentinel_gateway.py report --session latest --out informe.html --pdf
```

Genera un HTML autocontenido e imprimible:
- Indicadores clave.
- Gráficas SVG de heap, RSSI por cliente y agitación.
- Línea de tiempo y tabla de alertas.
- Tabla de calibración de distancia (último `/api/lab` guardado).
- Eventos.

`--pdf` lo convierte con Chromium headless si está instalado. Todo sale de
mediciones registradas; no hay datos simulados.

### Servicio systemd (usuario)

```bash
deploy/install-gateway-service.sh      # crea ~/.config/sentinel-lab/gateway.env y arranca
journalctl --user -u sentinel-gateway -f
```

La unidad (`deploy/sentinel-gateway.service`) solo apunta a archivos con
permisos 600; `deploy/gateway.env.example` no contiene secretos.

## Pruebas

`python3 tools/tests/test_gateway.py -v` y
`python3 tools/tests/test_gateway_services.py` (ambos incluidos en
`tools/check_software.py`). Los servicios cubren:
- Reglas con datos sintéticos: sostenido, histéresis, enfriamiento y límite
  horario.
- HMAC frente a un cálculo independiente y permisos del secreto.
- Cola sin conexión → con conexión con espera exponencial, y 401 como rechazo
  definitivo.
- Retención por antigüedad y por tamaño.
- Informe HTML y CLI, endpoints nuevos y PDF (este último con
  `SENTINEL_TEST_PDF=1`).

`test_gateway.py`:

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
