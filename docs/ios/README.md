# App iOS SENTINEL-LAB

App nativa para iPhone (SwiftUI, iOS 17+) que muestra el estado, la salud y la
telemetría RF del ESP32-S3 por HTTP v0.3.1 y WebSocket `sentinel.v1`.

## Estado verificado

| Elemento | Estado |
|---|---|
| Código fuente (`apps/ios/`) | Implementado |
| `SentinelKit` compilado y probado en Linux | **No verificado**: en el equipo de desarrollo no hay toolchain Swift, Docker no está activo y requiere `sudo`; la descarga del toolchain oficial no se autorizó en esta sesión |
| Compilación iOS (Xcode) | **No verificada**: requiere macOS; el job `ios-build` de CI la ejecutará en `macos-latest` |
| IPA firmada / instalación en iPhone | **No existe**: requiere macOS + Xcode + Apple ID (ver abajo) |
| Prueba en iPhone 12 físico | **Pendiente** |

No se afirma que la app compile, se instale o funcione en un iPhone hasta que
CI o un Mac lo demuestren.

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
