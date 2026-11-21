# Acelerómetro iOS nativo · SENTINEL RF v0.3.1

## Decisión

Se usa **Sensor Logger** (App Store, id `1531582925`) como cliente nativo iOS y se
rechaza la captura `DeviceMotion` de Safari sobre HTTP. La app lee Core Motion por
API nativa y publica lotes por **HTTP Push** directo a `192.168.4.1`. El ESP32 no
necesita HTTPS, certificados, CA, puente TLS ni internet.

Estado: **implementado, compilado y flasheado en v0.3.1, sin validación de
ingesta física**. El AP `SENTINEL-LAB` reapareció tras la carga, pero ninguna
muestra real ha llegado todavía desde el iPhone 12. Los datos de prueba del
repositorio son sintéticos y están etiquetados como tal.

## Por qué esta opción

| Opción | Resultado |
|---|---|
| Safari `DeviceMotion` sobre HTTP | Descartada: `/capture` exige `window.isSecureContext` y Safari bloquea el acelerómetro en HTTP. Requiere HTTPS, no autorizado. |
| HTTPS en ESP32 / puente TLS | Descartada para esta misión: certificado, SAN y CA de confianza; además el puente exigía cambiar la Wi‑Fi del host, lo que está prohibido. |
| App propia compilada en Linux | Descartada: firma, SDK y distribución iOS no disponibles aquí. |
| SensorLog | Descartada: de pago y la ficha actual indica iOS 18.0 o posterior; no garantiza el iPhone 12 con versiones anteriores. |
| Pythonista / Pyto | Descartadas: apps de pago y comportamiento ATS/HTTP local no verificado. |
| **Sensor Logger + HTTP Push** | **Elegida**: gratuita, cliente nativo Core Motion, HTTP Push JSON documentado, `deviceId` estable y lotes con marca de tiempo. |

Compatibilidad: el iPhone 12 dispone de Core Motion, que es la API que consume la
app. Antes de instalar, comprobar en la ficha de la App Store el requisito mínimo
de iOS de la versión disponible; no se afirma aquí un número de versión no
verificado en este entorno. Si la ficha indica una versión superior a la del
teléfono, no instalar una versión incompatible: detenerse y reportarlo.

## Instalación en el iPhone

1. Conectar el iPhone 12 a `SENTINEL-LAB` en Ajustes → Wi‑Fi. No desconectar el
   computador de `FAMILIACR`.
2. Instalar **Sensor Logger** desde la App Store. Si iOS solicita permiso de red
   local, concederlo. No instalar perfiles, no desactivar ATS y no ignorar avisos
   de seguridad de Safari.
3. Abrir la app y activar únicamente los sensores `Accelerometer` y `Gravity`.
4. En los ajustes de envío/streaming de la app configurar HTTP Push:
   - URL: `http://192.168.4.1/api/motion/sensorlogger`
   - Método: `POST`
   - Tipo de contenido: `application/json`
   - Periodo de lote: `200 ms` o mayor
   - Frecuencia de muestreo sugerida: `50 Hz` (20 ms entre muestras)
   - Pulsar **Test Pushing** antes de iniciar la grabación.
5. En el mismo iPhone abrir `http://192.168.4.1/motion/native` (Safari solo sirve
   para configurar; no se usa para leer el acelerómetro). Tras el envío de prueba
   aparecerá un `deviceId` pendiente. Copiarlo, pegarlo en el campo **ID de
   dispositivo permitido**, dejar `20` ms como intervalo mínimo y pulsar
   **Guardar autorización**.
6. Iniciar la grabación en Sensor Logger. Dejar la app en primer plano durante la
   demostración si el sistema limita muestreo en segundo plano.

## Contrato de datos

El ESP32 acepta el sobre de HTTP Push documentado por Sensor Logger:

```json
{
  "messageId": 12,
  "sessionId": "sesion-de-laboratorio",
  "deviceId": "identificador-devuelto-por-la-app",
  "payload": [
    {"name":"accelerometer","time":1698501144401773000,
     "values":{"x":0.1,"y":0.2,"z":0.3}},
    {"name":"gravity","time":1698501144401773000,
     "values":{"x":0.0,"y":0.0,"z":9.80665}}
  ]
}
```

- Unidades: `x`/`y`/`z` en **m/s²**. Sensor Logger usa `g = 9.80665`.
- `time`: epoch UTC en **nanosegundos**. El ESP32 usa su propio `millis()` como
  tiempo de recepción y solo resta el retardo derivado del lote; el reloj del
  teléfono nunca sustituye al reloj del servidor.
- En iOS, `accelerometer` es aceleración del usuario **sin gravedad**. El
  adaptador busca la muestra de `gravity` más cercana (skewness máximo 100 ms) y
  suma ambos vectores. Si no hay pareja, **descarta la muestra**, nunca inventa
  un vector de gravedad.
- Los pares se entregan a `MotionEngine`, que estima la gravedad con filtro de
  paso bajo y calcula la magnitud dinámica. No se calculan velocidad,
  desplazamiento ni fuerza.
- Fuente resultante: `NativeClient`. La simulación se rechaza en este endpoint y
  `/api/accel` devuelve `403` si recibe `source` nativo en la compilación RF.

## Límites y rechazos

| Parámetro | Valor |
|---|---|
| Cuerpo máximo | 16 KiB |
| Muestras máximas por lote | 64 lecturas |
| `deviceId` / `sessionId` | 1–64 caracteres imprimibles sin espacios |
| Intervalo mínimo configurables | 10–1000 ms (por defecto 20 ms) |
| Eje finito | `isfinite` y `abs <= 500 m/s²` |
| `messageId` | monótono dentro de una sesión; duplicados y mensajes fuera de orden → `409` |
| Sin autorizar → `403` | aparece como `pendingDeviceId` en el estado |
| Lote sin pares válidos → `422` | no se alimenta el motor |
| Simulación → `400` | nunca se etiqueta como real |

El receptor no bloquea el bucle HTTP: el cuerpo se limita antes de parsear, el
`JsonDocument` es dinámico pero acotado por esos límites y el emparejamiento es
`O(n*m)` con `n,m <= 64`.

## Verificación real (pendiente)

Tras iniciar la grabación, consultar desde el iPhone o desde un observador ya
conectado:

- `GET /api/motion/status`: `authorized=true`, `source="NativeClient"`,
  `acceptedSamples` creciente, `lastHttpCode=200`, `signalLost=false`,
  `sampleAgeMs` pequeño y `lastSampleEpochNs` cambiando.
- `GET /api/state`: `fw="0.3.1"`, `source="NativeClient"`, `fresh=true`,
  X/Y/Z y `accel.linear` en m/s².
- `GET /api/rf`: `clients` real, `available=true` y RSSI dBm de estaciones
  asociadas.
- `GET /api/events`: eventos de movimiento/RF con marcas de tiempo reales.

Si `pendingDeviceId` aparece vacío, revisar permiso de red local y el botón
**Test Pushing**. Si `acceptedSamples` no crece pero `skippedSamples` sí, revisar
que `Gravity` esté activado y que el periodo de lote no supere 64 lecturas. Hasta
observar estas señales, **no afirmar** que el acelerómetro funciona físicamente.

## Toast «Resumen no disponible» en Firefox iOS

El texto **no pertenece a SENTINEL**. No aparece en ningún HTML, JS, C++ ni
documentación del firmware; `grep` exacto sobre el repositorio da cero
coincidencias. Es un **toast nativo de Firefox iOS**, no una respuesta HTTP, ni
un evento de `MotionEngine`, ni un mensaje del dashboard.

Cadena verificada en el código fuente de Mozilla:

1. `GeneralBrowserActionType.shakeMotionEnded` llega a `SummarizerMiddleware` y
   ejecuta `handleShakeMotionEnded(...)`.
2. Ese handler llama a `makeConfiguration(from:)`. Si la página no es resumible
   (`nil`), dispara `dispatchShakeToSummarizeNotAvailable(...)`.
3. Se envía una acción `showToast` con `toastType = .shakeToSummarizeNotAvailable`.
4. `ToastType.title` devuelve `.Summarizer.ToastLabel`.
5. En `firefox-ios/Shared/Supporting Files/es-ES.lproj/Summarizer.strings`:
   `"Summarizer.ToastLabel.v149" = "Resumen no disponible";`.
6. El chequeo previo usa `window.__firefox__.Summarizer.checkSummarization(...)`;
   si `isProbablyReaderable(document)` falla, devuelve
   `reason = "documentNotReadable"` y `canSummarize = false`. Un panel de datos
   con tablas, `canvas` y texto disperso como el dashboard cumple ese caso.

Fuentes consultadas (rama `main` de `mozilla-mobile/firefox-ios`):
`BrowserKit/Sources/SummarizeKit/Backend/SummarizerTrigger.swift`,
`firefox-ios/Client/Frontend/Summarizer/SummarizeMiddleware.swift`,
`firefox-ios/Client/Frontend/Browser/ToastType.swift`,
`firefox-ios/Client/Frontend/UserContent/UserScripts/MainFrame/AtDocumentStart/Summarizer.js`
y el `Summarizer.strings` indicado.

### Cómo evitarlo en la demostración

No hace falta tocar el ESP32, Safari ni HTTPS. En el iPhone:

**Firefox → Ajustes → General → Resúmenes de páginas → Gestos → Agitar para
resumir → desactivado.** También se puede desactivar **Resumir páginas** por
completo. En la compilación actual de Firefox esa opción se llama
`Settings.Summarize.GesturesSection.ShakeGestureTitle.v142 = "Agitar para resumir"`.

Con esa opción desactivada, agitar el iPhone solo debe alimentar a Sensor Logger.
Si Firefox queda en segundo plano durante la grabación, tampoco recibe el gesto;
esa es la alternativa más sencilla para la prueba física.

### Cómo distinguirlo de un fallo real

| Observación | Origen |
|---|---|
| Aparece el toast «Resumen no disponible» al agitar con Firefox en primer plano | Gesto nativo de Firefox; no implica POST, rechazo HTTP ni pérdida de muestra. |
| `#status` muestra `SIN DATOS ACTUALES · HTTP 404` | Una API del dashboard devolvió 404; p. ej., placa con v0.3.0 sin rutas `/api/motion/*`. |
| `#distance` muestra `No disponible` | Texto del dashboard cuando falla `refresh()`; no es un toast del navegador. |
| `acceptedSamples` no crece en `/api/motion/status` | Problema real de ingesta: permiso `deviceId`, emparejamiento `gravity`, RED local o frecuencia. |

La regresión añadida en `tools/check_software.py` garantiza que el texto
«Resumen no disponible» no se cuele en ningún HTML servido y que los errores
reales de `refresh()` sigan siendo explícitos en `#status`.
