# Validación de software · SENTINEL Core v0.4.0 (P2)

Fecha: 2026-11-21  
Alcance: implementación local del núcleo en tiempo real sobre el firmware RF
v0.3.1. **No se flasheó hardware, no se midió heap en ejecución y no se realizaron
pruebas de radio.** Los resultados son de compilación y pruebas host.

## 1. Estado

| Elemento | Estado |
|---|---|
| Contrato Protocol v1.0.0 | Implementado y probado en host |
| Adaptador y transporte en firmware | Implementado y probado en host |
| API HTTP v0.3.1 | Sin cambios; compila en su entorno original |
| Imagen de producción v0.3.1 | Sigue siendo `_rf`; `_realtime` es experimental |
| Placa, radio y calibración física | Sin validar |

## 2. Comandos ejecutados

```bash
python3 tools/check_software.py
python3 scripts/check-repository.py
python3 scripts/check-conventional-commit.py --self-test
python3 -c 'import yaml,glob; [yaml.safe_load(open(p)) for p in glob.glob(".github/workflows/*.yml")]'
python3 packages/protocol/validate_protocol.py packages/protocol/examples/valid
python3 packages/protocol/validate_protocol.py --expect-invalid packages/protocol/examples/invalid
python3 -m unittest discover -s packages/protocol/tests -v
python3 packages/protocol/validate_protocol.py build/firmware-*.json
for env in sentinel_esp32s3_n16r8 sentinel_esp32s3_n16r8_diag \
           sentinel_esp32s3_n16r8_v02 sentinel_esp32s3_n16r8_rf \
           sentinel_esp32s3_n16r8_realtime; do
  pio run -e "$env"
done
```

Las dependencias del validador se instalaron en un entorno virtual aislado;
no se versionaron ni imprimieron credenciales.

## 3. Resultados

| Comprobación | Resultado |
|---|---|
| `tools/check_software.py` | 14 programas host OK, comprobaciones JS/DOM y sintaxis Python OK |
| Pruebas unitarias del contrato | 18 OK |
| Ejemplos válidos del contrato | 9 OK |
| Casos negativos del contrato | 16 rechazados |
| Fixtures generados por el firmware | 8 OK |
| `scripts/check-repository.py` | OK (174 archivos) |
| Conventional Commits self-test | OK (7 casos) |
| Parse YAML de workflows | OK |
| Entornos PlatformIO | 5/5 SUCCESS |

La cobertura host añadida por P2 incluye codec RFC 6455 (incluidas dos tramas en
una sola lectura y tramas sobredimensionadas), origen de tiempo y bloqueo hasta
sincronización, cola acotada con contador de descartes, autenticación bearer,
handshake, adaptador y validador de sobres entrantes, sesiones monótonas y
únicas por reconexión, estados/capacidades generados por la misma política del
productor, `deviceId` conforme al esquema y el ciclo de vida completo del
servidor WebSocket: saludo, `gateway` sin token, texto malformado, trama
oversized, límite de cuatro pares, saludo pendiente hasta la sincronización,
`goodbye` y cierre.

## 4. Tamaños de compilación

Valores del enlazador de PlatformIO; **no son mediciones de heap en ejecución**.

| Entorno | RAM usada | Flash usada | Estado |
|---|---:|---:|---|
| `sentinel_esp32s3_n16r8` | 24 052 B / 327 680 B (7,3 %) | 307 495 B / 6 553 600 B (4,7 %) | OK |
| `sentinel_esp32s3_n16r8_diag` | 24 084 B / 327 680 B (7,3 %) | 308 115 B / 6 553 600 B (4,7 %) | OK |
| `sentinel_esp32s3_n16r8_v02` | 49 900 B / 327 680 B (15,2 %) | 968 899 B / 6 553 600 B (14,8 %) | OK |
| `sentinel_esp32s3_n16r8_rf` | 72 092 B / 327 680 B (22,0 %) | 1 049 979 B / 6 553 600 B (16,0 %) | OK |
| `sentinel_esp32s3_n16r8_realtime` | 85 748 B / 327 680 B (26,2 %) | 1 077 263 B / 6 553 600 B (16,4 %) | OK |

Incremento estático de `_realtime` frente a `_rf`: **+13 656 B de RAM** y
**+27 284 B de Flash**. Quedan 241 932 B de RAM declarados por el enlazador,
pero parte se consume en ejecución por el stack FreeRTOS, el heap dynamic y las
asignaciones HTTP. No se ha medido el pico real.

No se afirma reproducibilidad byte a byte entre compilaciones separadas: el
hash P0/P1 de `_rf` quedó registrado en la evidencia de hardware, y la
recompilación actual conserva el mismo tamaño y la misma superficie de fuentes,
pero no se usa el hash como criterio de compatibilidad.

## 5. Compatibilidad HTTP v0.3.1

- Ninguna ruta HTTP existente se renombró, movió ni eliminó.
- Todos los cambios en archivos compartidos están dentro de
  `#ifdef SENTINEL_REALTIME`; el entorno `_rf` no define esa macro.
- `_realtime` extiende `_rf` y añade `src/realtime/*` más `-DSENTINEL_REALTIME=1`.
- Los cuatro entornos previos siguen compilando con sus filtros originales.
- `/api/rf` continúa exponiendo `lastSeenMs` relativo. El campo `lastSeen` de v1
  solo se deriva del reloj sincronizado; no se inventa fecha civil.
- `clientCount` de v1 coincide con la longitud de `clients` emitida; no se
  publica MAC.
- Los eventos simulados y los estados de movimiento simulados quedan fuera de
  la superficie v1.

## 6. Seguridad y límites verificados en software

- Handshake en `ws://<AP-IP>:81/realtime`, subprotocolo `sentinel.v1`.
- Cuatro pares, 1024 B por trama, dos tramas por cola de salida, ocho mensajes
  entrantes por segundo y par, 60 s de inactividad.
- Tramas fragmentadas, sin máscara, con opcode desconocido o sobredimensionadas
  se rechazan; los pares desconectados liberan su ranura.
- El bearer token se lee de `include/secrets.h` y la plantilla lo deja vacío.
  Sin token, el rol `gateway` no puede sincronizar el reloj. Con token vacío no
  existen operaciones privilegiadas; la telemetría local sigue legible.
- El firmware no inicia emisiones v1 hasta recibir una fecha válida de NTP local
  o de un `gateway` autenticado. Sin reloj no hay sobres con timestamps falsos.
- Estos límites protegen el AP local de laboratorio; no son una defensa frente a
  Internet. No se probaron con tráfico hostil real.

## 7. Riesgos pendientes

1. **Heap real:** no medido; el incremento del enlazador no sustituye una prueba
   de pico bajo carga.
2. **Hardware:** no se probó el AP, el WebSocket sobre Wi-Fi real ni la
   coexistencia con WebServer en placa.
3. **Reloj:** sin NTP local ni gateway autorizado, un cliente no recibe datos v1;
   esto es intencional y debe verificarse en una integración posterior.
4. **Calibración:** `estimatedDistance` se publica solo con modelo calibrado
   registrado por el usuario; no hay medición física nueva en P2.
5. **Cortes de eventos:** el sondeo RF a 1 Hz heredado puede perder asociaciones
   muy breves; P2 no cambia ese muestreo.
6. **Autenticación:** el token protege la sincronización del reloj, no sustituye
   una red de laboratorio aislada. No se probó con clientes remotos.
7. **Handshake:** se validan ruta, `Upgrade`, `Connection` y clave; no se exige
   `Sec-WebSocket-Version: 13` y el subprotocolo se anuncia de forma
   incondicional. Aceptable para el AP local, pendiente de endurecer.
8. **NTP en modo AP:** si se configura un servidor NTP sin credenciales STA, la
   SNTP se inicia pero no tiene ruta; el gateway autenticado sigue siendo la
   fuente de reloj usable en ese caso.

## 8. Conclusión

P2 está **implemented / software tested** y listo para revisión. No es
**production ready** ni **hardware tested**. No se solicita fusión del PR ni se
inicia P3 sin autorización explícita.
