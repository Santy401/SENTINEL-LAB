# SENTINEL v0.2.0 — Protocolo de comunicación

## 1. Alcance

Todo el tráfico es **local**. El ESP32-S3 crea un Access Point y sirve el
dashboard y la API desde su propia flash. No se usa n8n, Telegram, servicios
cloud ni conexión a internet.

| Parámetro | Valor |
|---|---|
| Modo Wi-Fi | Access Point (2,4 GHz, canal 1) |
| SSID / contraseña | `SENTINEL_WIFI_SSID` / `SENTINEL_WIFI_PASSWORD` en `include/secrets.h` (ignorado por Git) |
| IP del servidor | `192.168.4.1` (predeterminada en el AP de ESP32) |
| mDNS | `http://sentinel.local` |
| HTTP | Puerto 80, solo texto/JSON |
| Clientes AP | Máximo 4 |

El archivo `include/secrets.h` **no está versionado**. Parte de
`include/secrets.example.h`. Sin ese archivo el entorno v0.2.0 no compila
(`#error`). La contraseña debe tener al menos 8 caracteres (`static_assert`).

## 2. Endpoints

| Método | Ruta | Descripción |
|---|---|---|
| `GET` | `/` | Dashboard HTML autocontenido (CSS y JS inline, sin librerías externas). |
| `GET` | `/api/state` | Instantánea completa de estado, aceleración, LED e historial. |
| `POST` | `/api/accel` | Inyecta una muestra de aceleración. |
| `POST` | `/api/config` | Actualiza umbrales y el indicador de modo simulación. |
| — | cualquier otra | `404 {"ok":false,"error":"not found"}`. |

Todas las respuestas son `application/json` salvo `/`. `/api/state` envía
`Cache-Control: no-store`.

### 2.1 `GET /api/state`

```json
{
  "fw": "0.2.0",
  "uptimeMs": 12345,
  "state": "STABLE",
  "fresh": true,
  "source": "Simulated",
  "simulation": false,
  "wifi": { "mode": "AP", "ssid": "SENTINEL-...", "ip": "192.168.4.1", "clients": 1 },
  "accel": {
    "x": 0.12, "y": 4.31, "z": 9.76,
    "total": 10.61, "linear": 4.02,
    "gx": 0.01, "gy": 0.29, "gz": 9.81
  },
  "sampleCount": 42,
  "lastSampleMs": 12030,
  "thresholds": {
    "movement": 2.0, "alert": 6.0,
    "hysteresis": 0.8, "confirmations": 3, "timeoutMs": 2000
  },
  "leds": { "stable": false, "movement": true, "alert": false },
  "events": [
    { "t": 11800, "type": "StateChange", "from": "Stable", "to": "Movement", "mag": 4.02 }
  ]
}
```

- `fresh` es `false` cuando no hay muestra dentro del timeout. En ese caso
  `state` es `SENSOR_OFFLINE` y `source` es `None`.
- `events` contiene como máximo las 8 transiciones más recientes (el motor
  guarda 24 en un buffer circular). `t` es `millis()` del ESP32, no epoch.
- `linear` es la magnitud tras restar la gravedad estimada; es la que comparan
  los umbrales.

### 2.2 `POST /api/accel`

```json
{ "x": 0.1, "y": 4.0, "z": 9.81, "simulated": false, "source": "browser" }
```

| Campo | Tipo | Notas |
|---|---|---|
| `x`, `y`, `z` | número | m/s². Valores no finitos se convierten en `0`; se limita a ±500. |
| `simulated` | bool | Si es `true`, la muestra se etiqueta `Simulated`. |
| `source` | string | `browser` → `BrowserDeviceMotion`, `native` → `NativeClient`, `simulated` → `Simulated`. |

Respuesta: `200 {"ok":true}`. JSON inválido: `400`.

El **timestamp lo asigna el ESP32** con `millis()`; nunca se confía en el reloj
del teléfono. El origen siempre se conserva y se muestra en el dashboard: una
muestra simulada **jamás** se presenta como medición real.

### 2.3 `POST /api/config`

```json
{
  "movement": 2.0,
  "alert": 6.0,
  "hysteresis": 0.8,
  "confirmations": 3,
  "timeoutMs": 2000,
  "simulation": false
}
```

Solo se aplican las claves presentes. Respuesta: `200 {"ok":true}`. El motor
valida los rangos: movimiento 0,1–100; alerta por encima de movimiento;
histéresis ≥ 0 y < 90 % del umbral de movimiento; confirmaciones 1–10;
timeout ≥ 100 ms.

## 3. Motor de detección

- **Gravedad**: filtro paso bajo `g = α·g + (1−α)·a` sobre la aceleración que
  incluye gravedad (α = 0,92 por defecto).
- **Lineal**: `a − g`; magnitud `√(x²+y²+z²)`.
- **Histéresis**: para disparar movimiento hace falta superar `movement`; para
  liberarlo, bajar de `movement − hysteresis`. Igual para `alert`.
- **Confirmaciones**: 3 muestras consecutivas (configurable). Una lectura en
  la zona muerta rompe la racha.
- **Timeout**: sin muestras durante `timeoutMs` → `SENSOR_OFFLINE`. Solo una
  muestra **nueva** permite volver a `STABLE`.

```text
            primera muestra
  SENSOR_OFFLINE ──────────────> STABLE
        ^                         |  ^
        | timeout                 |  | 3 muestras < movement-hyst.
        |                         v  |
        +────── MOVEMENT <────── ALERT
                   3 > movement   3 > alert
```

## 4. Indicadores LED

| LED | Estado | GPIO |
|---|---|---|
| 1 | `STABLE` | Ninguno (virtual) |
| 2 | `MOVEMENT` | Ninguno (virtual) |
| 3 | `ALERT` | Ninguno (virtual) |

En `SENSOR_OFFLINE` los tres están apagados. No se asigna ningún pin: hacerlo
requiere verificar resistencias limitadoras o circuitos adecuados.

### 4.1 Controles del dashboard

El dashboard (`GET /`) incluye una sección **Prueba rápida** con tres botones:

| Botón | Ráfaga enviada | Estado esperado |
|---|---|---|
| `ESTABLE` | 10 muestras de ~0,2 m/s² | `STABLE` (LED 1) |
| `MOVIMIENTO` | 10 muestras de ~4 m/s² | `MOVEMENT` (LED 2) |
| `ALERTA` | 10 muestras de ~10 m/s² | `ALERT` (LED 3) |

El signo de la aceleración se alterna para mantener el filtro de gravedad
cerca de cero y que la magnitud lineal siga al valor solicitado. Todas las
muestras se envían con `simulated:true` y `source:"simulated"`; la interfaz
muestra **DATOS SIMULADOS** y el campo `source` del API es `Simulated`. Nunca
se presentan como lecturas reales.

También hay `Demo automática` (cicla los tres estados cada 4,5 s), entrada de
**magnitud manual**, edición de umbrales y un botón para solicitar el
acelerómetro real. Este último informa del bloqueo de iOS cuando la página se
sirve por HTTP.

## 5. Compatibilidad real con el acelerómetro del iPhone

**Estado: no disponible sobre este AP HTTP.**

- iOS Safari expone `DeviceMotionEvent`, pero desde iOS 13 exige
  `DeviceMotionEvent.requestPermission()` invocado desde un gesto del usuario.
- El acceso a movimiento/orientación requiere **contexto seguro (HTTPS)**.
  `http://192.168.4.1` (y `http://sentinel.local` servido por HTTP) **no** es
  un contexto seguro, por lo que Safari no entrega datos del acelerómetro.
- En algunos dispositivos iOS `event.acceleration` (sin gravedad) es `null`;
  el dashboard usa `event.accelerationIncludingGravity` y estima la gravedad
  en el ESP32.
- El dashboard comprueba `window.isSecureContext`. Si es falso, muestra un
  mensaje explícito y **no** intenta falsificar la captura. El botón de
  acelerómetro real solo actúa cuando el contexto lo permite.

Opciones futuras (no implementadas): servir HTTPS con certificado de CA local
instalado en el iPhone y nombre `sentinel.local` en el SAN; cliente nativo iOS
con CoreMotion; o sensor externo con un cliente local compatible. Ninguna se
presenta como funcionando hoy.

La simulación del dashboard está disponible como mecanismo de demostración y
siempre aparece etiquetada como **DATOS SIMULADOS**.


## Extensión RF v0.3.0

El contrato anterior se conserva en v0.2.0. Las rutas nuevas, límites de RF,
calibración, CSV y captura segura se documentan en [rf-protocol.md](rf-protocol.md).
