# Arquitectura general

## Fronteras del sistema

SENTINEL-LAB separa cuatro dominios:

1. **Adquisición física**: acelerómetro, gravedad, RSSI y distancias medidas.
2. **Firmware**: adquisición, validación, motores de física y API local.
3. **Presentación**: dashboard embebido, futura web y futura app iOS.
4. **Automatización**: gateway, almacenamiento histórico, n8n y Telegram.

```mermaid
flowchart TB
  subgraph Physical
    ACC[Core Motion real]
    WIFI[Estaciones Wi-Fi]
    TAPE[Cinta métrica]
  end

  subgraph Firmware
    MI[MotionIngest]
    ME[MotionEngine]
    RF[RF monitor]
    CAL[Calibración]
    HTTP[HTTP v0.3.1]
    HEALTH[/api/realtime/status<br/>solo _realtime]
    WS[WebSocket v0.4<br/>probado en software]
  end

  subgraph Clients
    DASH[Dashboard embebido]
    WEB[Web experimental<br/>planeada]
    IOS[App SwiftUI privada<br/>planeada]
  end

  subgraph Backend
    GW[Gateway<br/>planeado]
    DB[(SQLite<br/>planeado)]
    N8N[n8n<br/>sin desplegar]
  end

  ACC --> MI --> ME
  WIFI --> RF --> CAL
  ME --> HTTP
  CAL --> HTTP
  ME --> WS
  WS --> HEALTH
  HTTP --> DASH
  WS --> WEB
  WS --> IOS
  HTTP --> GW
  GW --> DB
  GW --> N8N
  TAPE --> CAL
```

## Principios

- La fuente de movimiento actual es Sensor Logger sobre HTTP Push nativo.
- `accelerometer` se suma a la muestra `gravity` más cercana; si no hay pareja,
  la muestra se descarta.
- `MotionEngine` estima gravedad, magnitud dinámica y transiciones con
  histéresis.
- RSSI crudo y estimación filtrada son conceptualmente distintos.
- El ESP32 debe funcionar sin cliente web conectado.
- La API HTTP v0.3.1 permanece durante la migración.
- n8n no participa en el procesamiento de telemetría de alta frecuencia.

## Datos reales frente a simulación

Todo mensaje simulado está etiquetado en su contrato. El endpoint nativo rechaza
simulación. El dashboard mantiene la simulación separada de la medición real.
Ninguna pantalla principal debe presentar JSON técnico como evidencia física.

## Seguridad

HTTP y WebSocket locales son adecuados solo dentro del AP de laboratorio. El
gateway remoto debe usar TLS, autenticación por dispositivo y endpoints
administrativos no públicos.

## Protocolo compartido

El contrato v1.0.0 define un sobre JSON versionado y transport-independent para
movimiento, RF, estado, eventos, ciclo de conexión y errores. La terna
`source` + `sessionId` + `sequence` identifica cada mensaje; los clientes RF se
representan con `clientId` por sesión y nunca con MAC. Es la base de los
adaptadores futuros; no sustituye la API HTTP v0.3.1. El adaptador del firmware
existe en el entorno `_realtime` y está probado en software; la web, el gateway y
iOS siguen pendientes. Ver [contrato](../protocol-contract.md) y
[ADR-0004](../decisions/ADR-0004-protocolo-v1.md).

El núcleo `_realtime` añade `GET /api/realtime/status` con contadores de salud de
ejecución y el puerto WebSocket 81. El kit de evidencia
`tools/hw_capture.py` + `tools/hw_report.py` audita esa superficie, el reloj y la
recuperación. **Sin un informe P2 en PASS sobre captura real, el transporte y la
placa siguen siendo hardware untested**; ver
[validación física](validation/p2-hardware.md) y
[validación de software](validation/p2-software.md).

## Migración

La reorganización en monorepo es incremental. El firmware funcional no se mueve
ni se reemplaza hasta que el protocolo y las pruebas de la nueva capa estén
validados.
