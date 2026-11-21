# Hardware

## Dispositivo validado

- ESP32-S3 N16R8.
- 16 MB flash.
- 8 MB PSRAM.
- AP `SENTINEL-LAB`.

## Documentos

- [Mapa de pines y precauciones eléctricas](../../hardware.md)
- [Flasheo y validación física](../../rf-hardware-validation.md)
- [Procedimiento de medición y calibración](../../rf-demo.md)

## Reglas

- No flashear sin autorización.
- No borrar NVS salvo autorización explícita.
- No conectar HC-SR04 ni servo sin verificación eléctrica.
- Los LED del firmware son virtuales.
- RSSI no determina una distancia exacta.
