# Guía de demostración y validación · SENTINEL RF v0.3.1

## Antes de tocar hardware

```bash
cd ~/Github/sentinel-firmware
python3 tools/check_software.py
for env in sentinel_esp32s3_n16r8 sentinel_esp32s3_n16r8_diag \
           sentinel_esp32s3_n16r8_v02 sentinel_esp32s3_n16r8_rf; do
  pio run -e "$env" || exit 1
done
```

**Estado físico:** la placa ya ejecuta v0.3.1 (flasheada el 2026-11-20) y el AP
`SENTINEL-LAB` fue verificado. Las rutas `/api/motion/*` existen en el dispositivo,
pero **todavía no hay evidencia real** de ingesta desde el iPhone. Esta guía no
autoriza cambiar la Wi‑Fi del host ni desplegar n8n/Telegram.

Resultados del build de software de esta revisión (no implican flasheo):

| Entorno | RAM | Flash | Estado |
|---|---|---|---|
| `sentinel_esp32s3_n16r8` | 24.052 B (7,3 %) | 307.495 B (4,7 %) | OK |
| `sentinel_esp32s3_n16r8_diag` | 24.084 B (7,3 %) | 308.115 B (4,7 %) | OK |
| `sentinel_esp32s3_n16r8_v02` | 49.900 B (15,2 %) | 968.899 B (14,8 %) | OK |
| `sentinel_esp32s3_n16r8_rf` | 72.092 B (22,0 %) | 1.049.979 B (16,0 %) | OK |

Hash del binario RF v0.3.1:
`918d9157f6308e84fb022faf0825ca37fb41ebc2edee28dcf70f22dce0f55799`.

## Misión 1 · Prueba desde el iPhone conectado a SENTINEL-LAB

No desconectar el computador de `FAMILIACR`. Conectar solo el iPhone a
`SENTINEL-LAB`.

1. En Safari del iPhone abrir `http://192.168.4.1/`. Comprobar visualmente:
   cabecera v0.3.1, IP/canal, número de clientes, RSSI del cliente seleccionado,
   uptime y `bootId`. Copiar el `bootId` a la tabla de evidencia.
2. Abrir `http://192.168.4.1/api/state`. Registrar `fw`, `uptimeMs`, `state`,
   `fresh`, `source`, `simulation`, `wifi.clients`, `sampleCount`,
   `lastSampleMs`, X/Y/Z y `accel.linear`. Con v0.3.0, `fw` será `0.3.0`.
3. Abrir `http://192.168.4.1/api/rf`. Registrar `available`, `clients`,
   `rssiDbm`/`filteredDbm` reales del cliente asociado, `ssid`, `ip`, `channel`,
   `frequencyMHz`, `wavelengthM`, `bootId` y `uptimeMs`.
4. Abrir `http://192.168.4.1/api/events`. Registrar `bootId`, eventos con sus
   `timestamp_ms`, `client_id` y `rssi_dbm`; verificar que `bootId` coincide con
   el de `/api/rf`.
5. Abrir `http://192.168.4.1/api/lab`. Registrar experimentos y modelos; si está
   vacío, anotar «sin experimentos», no «fallo».
6. Abrir `http://192.168.4.1/motion`. Probar la simulación **solo** con
   `simulation: true` y verificar que el state muestre `Simulated`. No presentar
   esos valores como mediciones del iPhone.
7. Esperar 30 s y repetir `/api/state` y `/api/rf`. `uptimeMs` debe aumentar y
   `bootId` debe permanecer igual; si cambia, hubo un reinicio.

Hasta que estas observaciones se copien a la tabla final, **ninguno** de estos
endpoints se declara funcional en hardware.

## Misión 2/3 · Acelerómetro nativo (solo con v0.3.1 flasheada)

Seguir `docs/rf-ios.md` al pie de la letra:

1. Sensor Logger con `Accelerometer` + `Gravity`, HTTP Push a
   `http://192.168.4.1/api/motion/sensorlogger`, lote ≥200 ms, 50 Hz y
   **Test Pushing**.
2. Abrir `http://192.168.4.1/motion/native`, autorizar el `deviceId` pendiente y
   dejar 20 ms de intervalo mínimo.
3. Iniciar grabación y comprobar `/api/motion/status`: `authorized=true`,
   `source="NativeClient"`, `acceptedSamples` creciente, `lastHttpCode=200`,
   `lastError="ok"`, `signalLost=false`, `sampleAgeMs` bajo.
4. Comprobar `/api/state`: `source="NativeClient"`, `fresh=true`, X/Y/Z en m/s².
5. Mover el iPhone de forma controlada y observar cambios de `state` y
   `accel.linear` en `/api/state` y nuevos eventos en `/api/events`.
6. Detener la grabación en la app; tras el timeout configurado,
   `/api/motion/status` debe marcar `signalLost=true`. No es un fallo de RF.

Si la placa sigue en v0.3.0, estas rutas devolverán 404: registrar ese hecho,
no un error de la app.

## Misión 4 · Calibración de distancia con RSSI real

La distancia se estima solo con muestras RSSI de estaciones **asociadas**; RSSI
no es localización exacta. La medición manual `actualM` es la referencia.

1. Elegir un experimento nuevo con nombre y entorno (abierto/interior/obstáculos)
   y el `clientId` correcto. El alias del navegador no identifica el dispositivo.
2. Mantener fijos canal, orientación del iPhone, altura del ESP32, obstáculos y
   posición del cuerpo. Usar cinta métrica u otra referencia conocida.
3. Para cada distancia real (incluir **1 m** y al menos otra distancia mayor):
   esperar estabilización del filtro EMA, registrar `actualM` y marcar la casilla
   de calibración. Repetir varias veces por punto y anotar la dispersión.
4. Llamar a `/api/calibration` con ese `experimentId`. Revisar `n` y los
   parámetros ajustados. Si hay menos de dos distancias distintas o la pendiente
   no decrece, la calibración se rechaza sin destruir el modelo anterior.
5. Desmarcar calibración y tomar distancias nuevas, **no usadas** para ajustar.
   Comparar `estimatedM` contra `actualM` y registrar el error absoluto.
6. Repetir en otro entorno con otro experimento: la calibración no se transfiere
   entre teléfonos, reconexiones (`bootId` nuevo) ni entornos.
7. Exportar `/api/export.csv` antes de apagar o reiniciar: todo reside en RAM.
8. Observar la pérdida de asociación alejando el teléfono y anotar el último
   punto con RSSI válido. Una desconexión HTTP no demuestra un alcance máximo;
   `COVERAGE_LIMIT_REACHED` está reservado y no se emite automáticamente.

El dashboard distingue RSSI crudo (`rssiDbm`) del valor filtrado
(`filteredDbm`/`currentEstimateM`); no se debe sustituir uno por otro en el acta.
No se ofrecen precisión fija ni intervalos de confianza.

## Tabla para rellenar con resultados reales

| Evidencia | Resultado |
|---|---|
| Identidad/flash/PSRAM de la carga v0.3.1 | OK: flasheada y verificada el 2026-11-20 |
| Reaparición del AP `SENTINEL-LAB` | OK: BSSID `AE:27:6E:AA:C8:4C`, canal 1, señal 100 |
| Un arranque / reinicios inesperados | Pendiente de ensayo prolongado |
| `fw`, `uptimeMs` y `bootId` por HTTP | Pendiente |
| Dashboard y `/motion` en iPhone | Pendiente |
| `/api/state`, `/api/rf`, `/api/lab`, `/api/events` | Pendiente |
| Clientes y RSSI real con 1 y varios clientes | Pendiente |
| Sensor Logger: permiso de red local y lotes aceptados | Pendiente |
| `source=NativeClient` y muestras X/Y/Z reales | Pendiente |
| Pérdida de señal al detener la app | Pendiente |
| Heap mínimo y latencia HTTP en ensayo prolongado | Pendiente |
| Distancias, calibración y CSV exportado | Pendiente |
| Webhook/Telegram/watchdog | Pendiente (sin despliegue) |

Ningún resultado sintético de test sustituye estas filas. LEDs físicos, HC-SR04,
ECHO y servo permanecen sin conectar/activar por esta versión. Safari sigue sin
ser la fuente del acelerómetro; no se desactivan sus restricciones de seguridad.
