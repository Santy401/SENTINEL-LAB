# Changelog

Todos los cambios relevantes se registrarán aquí siguiendo
[Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/) y versiones
SemVer cuando exista una release oficial.

## [Unreleased]

### Added

- Fundación del repositorio SENTINEL-LAB.
- Gobernanza, plantillas GitHub y workflows base de CI.
- Auditoría P0 e inventario del firmware funcional.
- Contrato de protocolo v1.0.0 con esquemas estrictos y validador.
- Ejemplos válidos, casos negativos y workflow de validación de protocolo.

### Changed

- Documentación de estado real del proyecto y migración por etapas.

### Fixed

- Los workflows de firmware copian la plantilla de Wi-Fi en el runner de CI sin
  publicar credenciales reales.

### Security

- Verificación automática de secretos y rutas prohibidas antes de publicar.
- El contrato exige `simulated: false` en todos los mensajes de medición.

## v0.3.1

### Added

- Receptor nativo de Sensor Logger mediante HTTP Push.
- `MotionIngest` con emparejamiento acelerómetro + gravedad.
- Endpoints `/motion/native`, `/api/motion/sensorlogger`, `/api/motion/status` y
  `/api/motion/config`.
- Regresión para impedir que el toast de Firefox iOS se presente como error del
  firmware.

### Hardware

- Firmware v0.3.1 flasheado y verificado.
- AP `SENTINEL-LAB` verificado tras el reset.
- Ingesta real con Sensor Logger reportada por el usuario.
