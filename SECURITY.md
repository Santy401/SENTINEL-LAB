# Seguridad

## Alcance

SENTINEL-LAB opera principalmente en una red Wi-Fi de laboratorio aislada. El
AP `SENTINEL-LAB` sirve HTTP en `192.168.4.1`; no está expuesto a internet.

## Riesgos conocidos de HTTP/WebSocket local

- El tráfico local no cifra contenido.
- Cualquier dispositivo asociado al AP puede intentar alcanzar los endpoints.
- No se debe replicar este modelo fuera de la red de laboratorio.
- El gateway remoto sí debe usar transporte protegido y autenticación.

## Controles actuales

- `include/secrets.h` y `.env` están excluidos de Git.
- Solo se versiona `include/secrets.example.h` con valores de ejemplo.
- `MotionIngest` limita tamaño de cuerpo, número de lecturas, intervalo,
  ejes finitos, longitud de identificadores y mensajes duplicados.
- La simulación se rechaza en el endpoint nativo.
- El emparejamiento de `deviceId` es explícito.
- n8n y Telegram permanecen sin desplegar.

## Controles planificados para v0.4.0

- Tokens por dispositivo.
- Pairing controlado y revocable.
- Separación entre telemetría y comandos administrativos.
- Validación de esquemas en gateway.
- Deduplicación y protección contra reproducción.
- Rate limiting y timeouts.
- Registro de errores sin secretos.
- Métricas de diagnóstico sin exponer administración a internet.

## Reporte de secretos

Si encuentras una credencial versionada, no la reenvíes en texto. Avisa al
propietario, rota la credencial afectada y elimina el archivo del historial solo
con autorización y procedimiento controlado.
