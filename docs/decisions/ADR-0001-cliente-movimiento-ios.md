# ADR-0001 · Cliente de movimiento para iOS

Estado: aceptado
Fecha: 2026-11-20

## Contexto

Safari sobre HTTP no puede acceder a `DeviceMotionEvent` sin contexto seguro.
El AP del ESP32 sirve HTTP local y no se autorizó HTTPS, certificados ni puente
TLS. Se necesitaban muestras reales de aceleración y gravedad desde un iPhone 12.

## Decisión

Usar Sensor Logger con HTTP Push directo a:

```text
http://192.168.4.1/api/motion/sensorlogger
```

El ESP32 empareja muestras `accelerometer` y `gravity` en `MotionIngest`. La
arquitectura final no dependerá de Sensor Logger: está prevista una app SwiftUI
privada con Core Motion.

## Consecuencias

- No se requiere HTTPS en el ESP32.
- El cliente debe autorizar explícitamente su `deviceId`.
- La simulación se rechaza en el endpoint nativo.
- La app futura deberá gestionar permisos, sesión, reconexión y firma legítima.
- No se desactivan restricciones de seguridad de Safari.

## Alternativas rechazadas

- Safari `DeviceMotion` sobre HTTP: bloqueado por contexto inseguro.
- HTTPS en ESP32 o puente TLS: fuera del alcance autorizado.
- Certificados no autorizados o jailbreak: rechazados.
