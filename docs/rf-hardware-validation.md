# Validación · SENTINEL RF v0.3.1

Fecha: 2026-11-20
Estado: **v0.3.1 flasheada, AP `SENTINEL-LAB` verificado e ingesta real del
iPhone reportada por el usuario el 2026-11-21**. RSSI, distancia y calibración
siguen **pendientes de archivar con evidencia**.

Este informe separa deliberadamente dos clases de evidencia:

1. **Pruebas de software**: se ejecutan en el computador; no demuestran que la
   placa ni el teléfono funcionen.
2. **Comprobaciones físicas**: ejecutadas sobre el ESP32 o pendientes de hacer
   con el iPhone 12.

---

## 1. Pruebas de software (no acreditan hardware)

| Comprobación | Resultado |
|---|---|
| `python3 tools/check_software.py` | OK |
| `test_algorithm` | OK |
| `test_motion_engine` | OK |
| `test_rf` | OK |
| `test_rf_monitor` | OK (SDK simulado, cuatro clientes) |
| `test_event_json` | OK |
| `test_motion_ingest` | OK (incluye rechazo de simulación y emparejamiento accel + gravity) |
| `test_workflow` | OK, sin runtime n8n ni red |
| JS/DOM de los 4 HTML servidos | OK: sintaxis y referencias únicas |
| Sintaxis Python y workflow n8n inactivo | OK |
| Compilación `sentinel_esp32s3_n16r8` | OK |
| Compilación `sentinel_esp32s3_n16r8_diag` | OK |
| Compilación `sentinel_esp32s3_n16r8_v02` | OK |
| Compilación `sentinel_esp32s3_n16r8_rf` | OK |
| RAM estática RF | 72.092 B (22,0 %) |
| Flash de programa RF | 1.049.979 B (16,0 %) |
| SHA256 de `firmware.bin` | `918d9157f6308e84fb022faf0825ca37fb41ebc2edee28dcf70f22dce0f55799` |
| Regresión del toast de Firefox | OK: el texto «Resumen no disponible» no aparece en HTML servido; los errores de `refresh()` siguen siendo explícitos |

Estos resultados validan el código y el artefacto de compilación. **Ninguna
fila de esta sección sustituye una medición de aceleración, RSSI o distancia.**

---

## 2. Comprobaciones físicas ejecutadas

### 2.1 Pre-flight de identidad

| Comprobación | Evidencia | Resultado |
|---|---|---|
| Repo Git | El directorio no es repositorio Git | No aplica |
| Entorno seleccionado | `sentinel_esp32s3_n16r8_rf` | OK |
| Hash antes de cargar | Coincidente con el validado por software | `HASH_OK` |
| Puerto | `/dev/ttyACM0` | OK |
| Puente USB-serie | `1a86:55d3 QinHeng Electronics USB Single Serial` | OK |
| Chip detectado | ESP32-S3 QFN56, revisión v0.2, 8 MB PSRAM embebida | OK |
| MAC detectada | `ac:27:6e:aa:c8:4c` | Coincide con la autorizada |
| Flash | 16 MB, fabricante `c2`, dispositivo `2018`, quad, 3,3 V | OK |
| Herramienta de lectura | `esptool v5.6.0` (`chip-id`, `flash-id`) | OK |

Nota: el puerto aparece tras un puente USB-serie, no como USB CDC nativo
`303a:1001`. Los mensajes `Serial` de la aplicación no se ven ahí; sí se ve el
bootloader ROM. La reaparición del AP es la prueba observable de que `setup()`
llegó a ejecutarse.

### 2.2 Flasheo

Comando ejecutado:

```bash
cd ~/Github/sentinel-firmware
newgrp uucp -c 'pio run -e sentinel_esp32s3_n16r8_rf -t upload --upload-port /dev/ttyACM0'
```

Registro guardado en `build/flash-v031.log`.

| Comprobación | Evidencia |
|---|---|
| Resultado | `SUCCESS` en 25,37 s |
| MAC durante la carga | `ac:27:6e:aa:c8:4c` |
| Velocidad | 460800 baud |
| Bootloader | `Hash of data verified` |
| Tabla de particiones | `Hash of data verified` |
| `boot_app0.bin` | `Hash of data verified` |
| Aplicación RF | 1.065.344 B escritos en `0x00010000` (677.134 B comprimidos); `Hash of data verified` |
| Reset final | `Hard resetting via RTS pin...` |
| NVS | **No borrado**. El log solo muestra borrados en `0x00000000–0x00004fff`, `0x00008000–0x00008fff`, `0x0000e000–0x0000ffff` y `0x00010000–0x00114fff`; el rango NVS `0x00009000–0x0000dfff` no fue tocado |

### 2.3 Verificación posterior al flasheo

| Verificación | Evidencia | Resultado |
|---|---|---|
| Reaparición del AP | `SENTINEL-LAB` detectado tras el reset | OK |
| BSSID | `AE:27:6E:AA:C8:4C` | OK |
| Canal | 1 | OK |
| Señal observada | 100 | OK |
| Estabilidad | 3 lecturas pasivas separadas 2 s, idénticas | OK |
| Host | `wlp4s0:wifi:connected:FAMILIACR` antes y después | OK |
| Credenciales | Sin cambios | OK |
| n8n / Telegram | Sin despliegue | OK |

### 2.4 Evidencia reportada por el usuario el 2026-11-21

El propietario informó estas observaciones en el entorno físico:

- iPhone 12 reconocido mediante `libimobiledevice`.
- Sensor Logger transmite datos reales al ESP32.
- El emparejamiento del `deviceId` funciona.
- `acceptedSamples` incrementa durante la grabación.
- El dashboard HTTP está operativo.

Estas filas son **evidencia reportada por el usuario**, no una observación
directa de esta sesión. Queda pendiente copiar al acta las respuestas concretas
de `/api/motion/status`, `/api/state` y `/api/rf`, junto con marcas de tiempo.

---

## 3. Comprobaciones físicas pendientes con el iPhone 12

Estas filas **no están verificadas**. No se debe afirmar que funcionan hasta
copiar aquí observaciones reales del teléfono y del ESP32.

| Evidencia requerida | Estado |
|---|---|
| iPhone asociado a `SENTINEL-LAB` y cliente visible en `/api/rf` | Reportado por el usuario; evidencia por archivar |
| `fw="0.3.1"` y `bootId` estable por HTTP | Pendiente |
| Dashboard `http://192.168.4.1/` y `/motion/native` en el iPhone | Reportado por el usuario; evidencia por archivar |
| HTTP Push de Sensor Logger con `lastHttpCode=200` | Reportado por el usuario; evidencia por archivar |
| `deviceId` pendiente autorizado desde `/motion/native` | Reportado por el usuario; evidencia por archivar |
| `acceptedSamples` creciente y `sampleAgeMs` bajo | Crecimiento reportado; `sampleAgeMs` por archivar |
| `source="NativeClient"` y `fresh=true` en `/api/state` | Pendiente |
| X/Y/Z y `accel.linear` reales al mover el iPhone | Pendiente |
| Eventos de movimiento con marcas de tiempo reales | Pendiente |
| `signalLost=true` al detener la grabación | Pendiente |
| RSSI real del cliente a 1 m y otra distancia | Pendiente |
| Calibración RSSI → distancia y error independiente | Pendiente |
| Exportación CSV antes de reiniciar | Pendiente |
| Uptime monótono y ausencia de reinicios en ensayo prolongado | Pendiente |

### Criterios mínimos para aceptar la ingesta real

- `/api/motion/status`: `fw="0.3.1"`, `ready=true`, `authorized=true`,
  `lastHttpCode=200`, `lastError="ok"`, `acceptedSamples` creciente,
  `skippedSamples` estable o justificado, `signalLost=false`.
- `/api/state`: `fw="0.3.1"`, `source="NativeClient"`, `fresh=true`,
  `simulation=false`; los ejes cambian al mover el teléfono y vuelven a una
  magnitud cercana a 9,8 m/s² en reposo.
- `/api/rf`: `available=true`, al menos un cliente asociado y RSSI dBm real.
- `/api/events`: eventos con `timestamp_ms`, `client_id` y `rssi_dbm` coherentes
  con el `bootId` de `/api/rf`.
- La distancia estimada solo se acepta después de calibrar con distancias reales
  medidas; RSSI no es localización exacta.

---

## 4. Restricciones respetadas

- No se borró NVS ni se usó `--erase-all`.
- No se modificaron credenciales ni `include/secrets.h`.
- No se cambió la red del computador: permanece en `FAMILIACR`.
- No se activaron GPIO, HC-SR04, servo ni hardware externo.
- No se desplegó n8n ni Telegram.
- No se publicaron endpoints ni se abrieron puertos hacia internet.
- No se desactivaron restricciones de seguridad de Safari ni se usó HTTPS en el ESP32.

## 5. Historial

- v0.2.0: cerrada y validada 23/23.
- v0.3.0: flasheada el 2026-11-19; hash
  `09f2585213bc829963097766b69418d58972f9774572f1a9c6d401925ba0611b`. AP y
  estabilidad verificados; HTTP diferido por decisión del usuario.
- v0.3.1: compilada el 2026-11-19 y flasheada el 2026-11-20; hash
  `918d9157f6308e84fb022faf0825ca37fb41ebc2edee28dcf70f22dce0f55799`.
  AP verificado; validación con iPhone pendiente.
