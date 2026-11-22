# App iOS SENTINEL-LAB

App nativa para iPhone (SwiftUI, iOS 17+) que muestra el estado, la salud y la
telemetría RF del ESP32-S3 por HTTP v0.3.1 y WebSocket `sentinel.v1`.

## Estado verificado

| Elemento | Estado |
|---|---|
| Código fuente (`apps/ios/`) | Implementado |
| `SentinelKit` compilado y probado | **Sí, en CI** (2026-11-22): `swift test` con 32 tests, 0 fallos, en el contenedor `swift:6.0` (Linux) y en `macos-latest`. En el equipo Arch local no hay toolchain Swift |
| Compilación iOS (Xcode) | **Sí, en CI**: `xcodebuild build` → `BUILD SUCCEEDED` para `generic/platform=iOS Simulator` y `generic/platform=iOS` con `CODE_SIGNING_ALLOWED=NO`. Es una compilación **sin firmar y no instalable**. Los tests del target de la app solo se compilan |
| Contrato con el firmware real | Comprobado en el ESP32 físico con un cliente Python que imita a la app: rol `ios` con token aceptado y reloj sincronizado (`docs/validation/p3-integration.md`). La app en sí **no se ha ejecutado** |
| IPA firmada / instalación en iPhone | **No existe**: requiere macOS + Xcode + Apple ID (ver abajo) |
| Prueba en iPhone 12 físico | **Pendiente** |

Que compile en CI no implica que se instale ni que funcione en un iPhone. Eso
sigue pendiente hasta probarlo con Xcode en un dispositivo.

## Arquitectura

```
apps/ios/
├── project.yml                 # XcodeGen → SentinelLab.xcodeproj (no versionado)
├── scripts/sync-fixtures.sh    # copia ejemplos del contrato y tramas del firmware
├── SentinelKit/                # paquete Swift solo-Foundation (Linux + Apple)
│   ├── Sources/SentinelKit/
│   │   ├── ProtocolMessage.swift   # sobre v1, decodificador estricto, ClientSession
│   │   ├── Payloads.swift          # rf.telemetry, system.status, event, …
│   │   ├── StrictDecoding.swift    # claves desconocidas, duplicadas, patrones
│   │   ├── ProtocolTimestamp.swift # fecha UTC v1 (misma aritmética que el firmware)
│   │   ├── HTTPModels.swift        # /api/state, /api/rf, /api/events, /api/realtime/status
│   │   ├── Connection.swift        # backoff exponencial con jitter + máquina de estados
│   │   ├── History.swift           # RingBuffer y RssiHistory acotados
│   │   ├── Health.swift            # evaluación de salud (heap, pila, reloj, reinicios)
│   │   └── Evidence.swift          # evidencia JSON redactada (sin token)
│   └── Tests/SentinelKitTests/     # XCTest + Fixtures/{valid,invalid,firmware}
├── SentinelLab/                # target SwiftUI
│   ├── App/  Model/  Services/  Views/  Resources/
└── SentinelLabTests/
```

- **SentinelKit** concentra toda la lógica comprobable sin UI. El decodificador
  aplica las mismas reglas que `packages/protocol`: versión mayor 1, sin campos
  extra, sin claves duplicadas, timestamps estrictos, RSSI en −100…−1 dBm y
  `simulated: false`. Un `type` desconocido se conserva como `.unknown` y se
  cuenta, nunca se muestra como dato.
- **Transporte**: `URLSessionWebSocketTask` contra `ws://<host>:81/realtime`
  con la cabecera `Sec-WebSocket-Protocol: sentinel.v1` y, si hay token,
  `Authorization: Bearer …`. La app verifica que el subprotocolo negociado sea
  `sentinel.v1`.
- **Sincronización de reloj**: el firmware no emite tramas v1 hasta tener hora
  UTC válida. Con token, la app envía `connection.hello` (rol `ios`) con la hora
  del iPhone nada más abrir el socket; ese saludo autenticado sincroniza el
  ESP32. Sin token, la app lo explica usando `clockSynced` de
  `/api/realtime/status` y sigue mostrando datos HTTP.
- **Reconexión**: máquina de estados pura (`ConnectionStateMachine`) con
  backoff 1 → 30 s y jitter ±20 %; `NWPathMonitor` pasa a estado *Sin red* y
  reconecta al recuperar la ruta. Latido `connection.heartbeat` + ping cada
  15 s; 30 s sin tramas con el flujo activo se trata como conexión muerta.
- **HTTP**: sondeo de `/api/realtime/status` y `/api/rf` (cada 2 s por defecto)
  y de `/api/state` y `/api/events`. El historial RSSI se alimenta por HTTP solo
  mientras el WebSocket no transmite, para no duplicar muestras.
- **Memoria acotada**: historial de 8 estaciones × 600 muestras y 200 eventos.
- **Credenciales**: el token vive solo en el Keychain
  (`kSecAttrAccessibleWhenUnlockedThisDeviceOnly`), no en `UserDefaults`, logs
  ni evidencia.
- **Modo demo**: datos sintéticos locales (fuente `demo-generator`); todas las
  pantallas muestran *MODO DEMO* y la evidencia exportada lleva `"demo": true`.

Pantallas: **Panel** (conexión, firmware, uptime, heap, reloj, clientes, salud),
**Telemetría** (gráfica RSSI con Swift Charts, estaciones, eventos),
**Diagnóstico** (versiones, motivo de reinicio, memoria, contadores, protocolo,
exportación de evidencia con `ShareLink`) y **Ajustes** (dirección, token,
preferencias, demo).

## Compilar

### En Linux (solo SentinelKit)

```bash
apps/ios/scripts/sync-fixtures.sh          # tras regenerar build/firmware-*.json
cd apps/ios/SentinelKit && swift test      # requiere toolchain Swift 5.9+ (p. ej. 6.0)
# o, con Docker operativo y permisos:
docker run --rm -v "$PWD/apps/ios/SentinelKit":/pkg -w /pkg swift:6.0 swift test
```

La UI (SwiftUI, Charts, Security, Network) no compila en Linux.

### En macOS (app completa)

```bash
brew install xcodegen
cd apps/ios && xcodegen generate
open SentinelLab.xcodeproj
```

CI (`.github/workflows/ios.yml`) ejecuta `swift test` en un contenedor
`swift:6.0` y, en `macos-latest`, genera el proyecto y compila para simulador y
para dispositivo **sin firmar** (`CODE_SIGNING_ALLOWED=NO`). Un binario sin
firmar no se puede instalar en un iPhone.

> Desde Linux, sin Mac: ver [LINUX.md](LINUX.md) (xtool).

## Instalar en el iPhone 12 (proceso real)

Requisitos ineludibles para instalar una app propia en un iPhone sin jailbreak:

1. **macOS con Xcode** (Xcode 15 o posterior para iOS 17). Xcode no funciona en
   Linux; no hay alternativa oficial para firmar e instalar desde Arch.
2. **Un Apple ID** iniciado en Xcode → *Settings › Accounts*.
3. **Firma**:
   - *Personal Team* gratuito: perfil de aprovisionamiento válido **7 días**
     (hay que reinstalar desde Xcode al caducar), máximo 3 apps activas por
     dispositivo, sin TestFlight.
   - *Apple Developer Program* (99 USD/año): perfiles de 1 año, TestFlight y
     distribución ad hoc.
4. **Developer Mode** en el iPhone (iOS 16+): *Ajustes › Privacidad y
   seguridad › Modo de desarrollador*, reiniciar y confirmar.

Pasos en el Mac:

```bash
git clone https://github.com/Santy401/SENTINEL-LAB.git
cd SENTINEL-LAB/apps/ios && brew install xcodegen && xcodegen generate
open SentinelLab.xcodeproj
```

1. Target *SentinelLab* › *Signing & Capabilities*: marcar *Automatically
   manage signing*, elegir el *Team*. Si el bundle id `lab.sentinel.app` ya está
   tomado, cambiarlo por uno propio (p. ej. `com.<apellido>.sentinellab`).
2. Conectar el iPhone por USB, confiar en el Mac, elegirlo como destino y
   pulsar *Run* (⌘R).
3. Con cuenta gratuita, la primera vez: en el iPhone, *Ajustes › General › VPN
   y gestión de dispositivos* › confiar en el certificado del desarrollador.
4. Al abrir la app, aceptar el permiso de **red local** (sin él, iOS bloquea
   las conexiones a 192.168.4.1).
5. Unir el iPhone a la Wi-Fi `SENTINEL-LAB`. iOS puede avisar de que la red no
   tiene Internet: mantener la conexión.
6. En *Ajustes* de la app, pegar el valor de `SENTINEL_REALTIME_BEARER_TOKEN`
   del firmware (archivo local `include/secrets.h`, nunca versionado) y pulsar
   *Guardar token*. El Panel debe pasar a *Transmitiendo* y el reloj a
   *Sincronizado*.

Sin Mac propio, opciones legítimas: un Mac prestado (basta para cada
reinstalación semanal con cuenta gratuita), alquiler de Mac en la nube
(MacStadium, AWS EC2 Mac, etc.) con el iPhone conectado por USB solo si el
proveedor lo permite —en general no—, o pagar el Developer Program y distribuir
por **TestFlight** desde un Mac remoto o desde CI con certificado y perfil
cargados como secretos. CI sin certificado puede compilar, pero **no** produce
una IPA instalable. No se usa jailbreak ni herramientas de *sideloading* no
oficiales.

## Laboratorio físico (app 1.1, firmware ≥ 0.5.0)

### Movimiento (pestaña «Movimiento»)

- **Fuente:** CoreMotion `deviceMotion` a 100 Hz, con marco `xArbitraryZVertical`.
  El análisis corre en una cola de fondo (`MotionProcessor`) y la interfaz
  recibe instantáneas a unos 30 Hz.
- **Unidades:** CoreMotion entrega g; se convierte una sola vez a m/s² con
  g₀ = 9,80665. La aceleración lineal (`userAcceleration`) excluye la gravedad.
- **Agitación:**
  - |a| bruta (instantánea) y |a| filtrada (RMS en una ventana de 0,5 s), siempre
    rotuladas por separado.
  - Pico de ventana, pico de la sesión, máximo, mínimo y promedio del valor
    filtrado, y frecuencia de muestreo real.
- **Fuerza:** F = m·a con la masa nominal del iPhone 12 (0,164 kg). Es la fuerza
  neta que acelera el teléfono, no la fuerza de la mano.
- **No se integra** la aceleración a velocidad ni a posición: la deriva lo haría
  inválido.
- **Estados:** `STABLE` < 0,5 m/s², `MOVEMENT` < 4 m/s², `ALERT` ≥ 4 m/s²
  (valor filtrado).
- **Visualización:** modelo 3D (SceneKit) orientado con el cuaternión de
  `attitude`, ejes X/Y/Z, vector gravedad medido en el marco del dispositivo y
  gráficas por eje de los últimos 10 s. Tiene modo inmersivo y modo científico.

### Envío al ESP32 (`motion.sample`, Protocol v1)

- **Requisitos:** solo con el token configurado y la sesión WebSocket abierta
  (rol `ios` autenticado). El envío se puede desactivar en la propia pestaña.
- **Ritmo:** 5 Hz. Con el heartbeat de 15 s queda por debajo del límite de 8
  mensajes/s por cliente.
- **Selección de muestras (peak-hold):** en cada ventana de 200 ms se envía la
  muestra **real** con mayor |a lineal|, así una sacudida corta entre envíos no
  se pierde y nunca se envía un valor promediado o sintético.
- **Contenido:** `sourceType: CoreMotion` y vectores en m/s² en el marco del
  dispositivo (`acceleration` = lineal + gravedad).
  - `sampleIndex` arranca en los milisegundos Unix del arranque de la app, así
    que crece de forma estricta entre ejecuciones.
  - `deviceId` es el identificador estable de la instalación.
- **Eco:** el ESP32 retransmite `motion.sample`. La app muestra el eco de sus
  propias muestras como confirmación de ida y vuelta, y
  `/api/realtime/status.phoneMotion` como lo que recibe el ESP32.

### LED RGB

`VirtualLed` refleja `/api/realtime/status.rgb` (modo, GPIO y color). El color
físico se muestra escalado a brillo completo porque el LED real funciona
atenuado. Con un firmware sin motor RGB se indica «Firmware sin motor RGB».

### Distancia al ESP32

- **Origen del RSSI:** iOS no ofrece el RSSI Wi-Fi propio. Se usa el RSSI que
  el ESP32 mide para la estación de este iPhone. Si hay una sola estación se
  elige automáticamente; si hay varias, el usuario la identifica y la app la
  recuerda.
- **Modelo:** log-distancia, RSSI = A − 10·n·log₁₀(d).
  - **Orientativa (sin calibrar):** A = −40 dBm, n = 2,7, con un rango
    plausible para n de 2,0 a 3,5.
  - **Calibrada:** el modelo ajustado en el ESP32 (`/api/lab`) con mediciones
    reales a distancias conocidas. Siempre se rotula como estimación.
- **Alcance máximo teórico:** la distancia a la que el modelo llega a −90 dBm.
  Es teórico, no una medición.
- **Máxima distancia estimada en la sesión:** se guarda por separado para el
  modelo calibrado y para el orientativo.
- **Calibración guiada:**
  1. `POST /api/experiments` para la estación de este iPhone.
  2. En cada distancia (1, 2 y 4 m, editables): espera de 3 s y
     `POST /api/measurements` con `calibration: true`.
  3. `POST /api/calibration`.
  4. Se muestran A y n leídos de `/api/lab`.

  Los errores del firmware se traducen (RSSI no disponible, sesión RF caducada,
  máximo de 8 experimentos en RAM, etc.).

### Verificación (2026-11-22, Arch Linux)

- `swift test` (SentinelKit): 46 tests, 0 fallos. Incluyen física,
  peak-hold, envelope `motion.sample` validado con el decodificador estricto,
  modelo de distancia y ajuste, y modelos HTTP nuevos.
- `scripts/linux-install.sh --build-only` (xtool 1.24.0, SDK iOS 27.1):
  *Build complete!*. IPA arm64 con `NSMotionUsageDescription`.
- **No verificado en el iPhone:** sensores reales, escena 3D, envío al ESP32,
  calibración contra el firmware 0.5.0.

## Placa 3D, vibración, Actividad en vivo y alertas (app 1.2)

### Placa ESP32-S3 en 3D

- El Panel muestra como portada la placa ESP32-S3-DevKitC-1 en SceneKit. Se
  puede girar y acercar, y al tocar una pieza se abre su ficha técnica, con los
  mismos textos que la web.
- **Fuente de verdad:** la web. Los textos y las dimensiones vienen de
  `apps/web/src/lib/board-parts.ts`; la geometría es una transcripción en
  milímetros de `apps/web/src/three/board-model.ts`. La copia iOS está en
  `SentinelKit/Sources/SentinelKit/BoardModel.swift`, y
  `scripts/check-board-model.py` comprueba que los textos y las dimensiones
  coinciden con la web.
- El modelo es **original y aproximado**: dimensiones públicas de Espressif, no
  un CAD del fabricante.
- **LED virtual:** el domo del WS2812 brilla con el color de
  `/api/realtime/status.rgb`. El tono se amplifica para que se vea (el LED
  físico está limitado a 64/255), pero se conserva exacto.
- **Reacción al movimiento:** con la agitación, la placa se inclina como mucho
  4°. Desactivado con «Reducir movimiento».
- **Sin Metal:** se usa una vista superior 2D dibujada con las mismas piezas.

### Vibración háptica

- En la pestaña Movimiento, un háptico continuo (CoreHaptics) sigue la
  agitación filtrada con la misma escala que el LED: |a| / 8 m/s², con una zona
  muerta del 6 %.
- **Frecuencia de actualización:** como mucho 20 Hz.
- **Cuándo se apaga:** fuera de esa pestaña, con «Reducir movimiento», en bajo
  consumo, o si se desactiva en Ajustes.

### Actividad en vivo

- **Contenido:** estado de conexión, reloj, barra de agitación, color del LED y
  última alerta, en la pantalla bloqueada; en Dynamic Island cuando existe (el
  iPhone 12 no la tiene).
- **Activación:** desde Ajustes → Laboratorio.
- **Actualización:** solo la app, con cambios significativos y como mucho cada
  2 s.
- **Extensión:** `SentinelLabWidget`, declarada en `project.yml` y en
  `xtool.yml` (`extensions:`).
- **Cuenta gratuita:** xtool firma la extensión con un App ID adicional.
- **Si no se inicia:** la app muestra el error de ActivityKit en Ajustes y
  sigue funcionando. Esto ocurre si la extensión no llegó al bundle o si las
  Actividades en vivo están desactivadas en iOS.

### Alertas locales

Las calcula `AlertEngine` (SentinelKit, con tests) cada segundo a partir de
datos reales:

| Alerta | Condición | Severidad |
|---|---|---|
| Agitación fuerte | RMS ≥ 4 m/s² | crítica |
| iPhone fuera de alcance | RSSI filtrado < −85 dBm, o el ESP32 ya no lista este iPhone | aviso |
| ESP32 reiniciado | cambio de `bootId`; incluye `resetReason` | crítica |
| Memoria baja | heap libre < 40 KB | aviso |
| Reloj sin sincronizar | > 2 min sin UTC con el dispositivo accesible | información |

- **Disparo:** por flanco. Una condición que se mantiene genera una sola alerta,
  y cada tipo tiene un enfriamiento de 60 s (el reinicio siempre avisa).
- **Lista:** se muestran las últimas 50 en el Panel.
- **En segundo plano:** se envían como notificación local
  (UNUserNotificationCenter) si se autorizan en Ajustes.

## Seguridad y límites

- HTTP/WebSocket sin TLS: aceptable solo en la red aislada del laboratorio.
  `NSAllowsLocalNetworking` permite tráfico en claro únicamente a destinos
  locales.
- El token viaja en claro por la Wi-Fi WPA2 del laboratorio; protege la
  sincronización del reloj, no sustituye el aislamiento de la red.
- RSSI no es una distancia. La app solo muestra distancias que el firmware
  marca como calibradas.

## Relacionado

- [Contrato y configuración de Sensor Logger](../rf-ios.md)
- [Protocolo v1](../protocol-contract.md)
- [Núcleo realtime](../realtime-core.md)
- [Evidencia física](../rf-hardware-validation.md)
