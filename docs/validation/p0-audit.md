# Auditoría P0 · Fundación del repositorio

Fecha: 2026-11-21
Alcance: `~/Github/sentinel-firmware` antes de la reorganización a monorepo.
Método: inspección de solo lectura, ejecución de pruebas y comprobación remota.
No se flasheó hardware durante la auditoría.

## 1. Estado remoto

| Comprobación | Resultado |
|---|---|
| Directorio local con `.git` antes de P0 | No |
| Remoto indicado | `https://github.com/Santy401/SENTINEL-LAB.git` |
| `git ls-remote` | Sin refs; remoto accesible y vacío |
| `HTTP info/refs` | 200 |
| Usuario GitHub confirmado | `Santy401`, tipo `User` |
| Historial previo a preservar | No existe en el remoto |

## 2. Hashes funcionales

| Artefacto | SHA256 |
|---|---|
| `firmware.bin` RF v0.3.1 | `918d9157f6308e84fb022faf0825ca37fb41ebc2edee28dcf70f22dce0f55799` |
| Binario v0.3.0 flasheado previamente | `09f2585213bc829963097766b69418d58972f9774572f1a9c6d401925ba0611b` |
| `build/rf-validation.json` | `84639ee9c21bd10f79fbe74ea911ee138c0977c6b9ede2ea9f4c774c3c353622` |
| `docs/rf-hardware-validation.md` | `6c5e2b766f95d25062303493ef4a789ae4ace209836e6c8c2b27d9defd8531b3` |

Los archivos de validación se actualizaron posteriormente para incorporar la
evidencia reportada por el usuario el 2026-11-21; el hash del firmware no cambió.

## 3. Pruebas ejecutadas en la auditoría

```bash
python3 tools/check_software.py
for env in sentinel_esp32s3_n16r8 sentinel_esp32s3_n16r8_diag \
           sentinel_esp32s3_n16r8_v02 sentinel_esp32s3_n16r8_rf; do
  pio run -e "$env"
done
```

Resultado: todas las pruebas host, comprobaciones JS/DOM, validación del workflow
y los cuatro entornos PlatformIO pasaron.

| Entorno | Uso |
|---|---|
| `sentinel_esp32s3_n16r8` | línea heredada v0.1.0 |
| `sentinel_esp32s3_n16r8_diag` | diagnóstico v0.1.0 |
| `sentinel_esp32s3_n16r8_v02` | dashboard y motor v0.2.0 |
| `sentinel_esp32s3_n16r8_rf` | firmware funcional v0.3.1 |

## 4. Inventario de funcionalidades

### Firmware heredado v0.1.0

- Máquina de estados de intrusión con histéresis.
- Sensor HC-SR04 no bloqueante con filtro mediano.
- Servo controlado con seguridad desactivada por defecto.
- Modo diagnóstico por flag de compilación.

### v0.2.0

- AP WPA2 y mDNS.
- Dashboard HTTP embebido.
- `MotionEngine` puro con gravedad, magnitud lineal, histéresis, confirmaciones,
  timeout y anillo de 24 eventos.
- LEDs virtuales sin GPIO.
- API `/api/state`, `/api/accel` y `/api/config`.

### RF v0.3.1

- Monitor RF a 1 Hz con hasta 8 clientes e historiales RSSI.
- Filtro EMA y estimación logarítmica de distancia.
- Eventos RF con anillo acotado y histerésis de señal débil.
- Laboratorio con 8 experimentos y 96 mediciones en RAM.
- Exportación CSV con protección contra inyección de fórmulas.
- `MotionIngest` para HTTP Push de Sensor Logger:
  - emparejamiento explícito por `deviceId`;
  - 403 si no autorizado, 409 duplicados, 422 sin pares;
  - límite de 16 KiB y 64 lecturas;
  - rechazo de simulación;
  - emparejamiento aceleración + gravedad con desfase máximo de 100 ms.
- Rutas `/motion/native`, `/api/motion/sensorlogger`, `/api/motion/status` y
  `/api/motion/config`.
- Notificador HTTPS opcional y desactivado, con plantilla pública
  `connected.example.h`.

## 5. Componentes reutilizables

- `v02::MotionEngine`: sin Arduino/Wi-Fi/Serial, tiempo inyectado.
- `rf::MotionIngest`: mismo patrón puro y time-injected.
- `Ring`, `RssiFilter`, `DistanceEstimator`, `EventEngine`, `Laboratory` y
  `eventJson`: cabeceras puras, verificables en host.
- `AccelSource` y etiquetado de simulación.
- `tools/check_software.py`: puerta reproducible de pruebas.
- `test/stubs/esp_wifi.h`: simulacro del SDK para pruebas RF.

## 6. Secretos y configuración privada

| Ruta | Estado |
|---|---|
| `include/secrets.h` | Presente, con credenciales reales; **ignorado y nunca versionado** |
| `include/secrets.example.h` | Plantilla pública; valores de ejemplo |
| `include/rf/connected.example.h` | Plantilla pública vacía |
| `include/rf/connected.h` | Ausente; se añadió patrón de ignorado para futura creación |
| n8n credential schema | Solo nombres `botToken` y `chatId`, sin valores |
| workflow n8n | Inactivo; sin token incrustado |
| `.env`, certificados, llaves | No presentes; excluidos por Git |

No se imprimió ningún valor secreto.

## 7. Deuda técnica detectada

1. El README histórico indicaba v0.3.0 mientras el código ya reportaba v0.3.1.
2. `src/v02/web_dashboard.cpp` mezcla comportamiento v0.2 y RF bajo
   `#ifdef SENTINEL_RF`.
3. La detección RF usa sondeo a 1 Hz; eventos muy cortos pueden perderse.
4. RSSI del SDK es un promedio con época desconocida; `lastSeenMs` es tiempo de
   sondeo, no de paquete.
5. `COVERAGE_LIMIT_REACHED` está reservado y nunca se emite.
6. Experimentos, calibraciones y estado de ingesta viven en RAM.
7. No hay pruebas host de handlers HTTP, notificador ni `wifi_ap.cpp`.
8. El pico de heap y la latencia bajo carga no están medidos.
9. Los binarios compilados contienen configuración de red; no publicar artefactos.
10. `build/` mezcla evidencia, logs, bins y caché npm; por ahora solo se versiona
    `build/rf-validation.json`.
11. n8n/Telegram están implementados pero sin desplegar ni validar en producción.
12. La app iOS no existe todavía; cualquier code signing requiere macOS/Xcode.

## 8. Conclusión

El firmware funcional v0.3.1 fue preservado. La base P0 puede crearse sin mover
fuentes ni romper PlatformIO. Las siguientes etapas deben introducir primero el
protocolo compartido y después migrar componentes con compatibilidad verificada.
