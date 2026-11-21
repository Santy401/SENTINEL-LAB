# Automatización opcional (sin desplegar)

El modo predeterminado sigue siendo AP local sin internet. Solo el archivo privado
include/rf/connected.h habilita STA; se proporciona connected.example.h con valores
vacíos. No se ha creado connected.h ni modificado secrets.h. No se configuraron
n8n, Telegram, redes, certificados ni tokens.

## Firmware

Cola acotada a ocho eventos; tarea FreeRTOS separada, nunca espera en loop().
HTTPS con CA del receptor, cabecera X-Sentinel-Token, mínimo 16 caracteres.
Tres intentos como máximo, conexión/lectura 1500 ms y handshake TLS 3 s.
Sin redirects ni TLS inseguro. Cola llena descarta el evento nuevo; sin conexión STA
se descarta el evento. No es una cola durable ni entrega garantizada.
Heartbeat cada 15 s. ID de evento: boot_id aleatorio de 64 bits + sequence.
RF: tres confirmaciones consecutivas, umbral débil -80 dBm, recuperación -75 dBm,
ventana de notificación 30 s. Eventos locales se conservan aunque se suprima aviso.
Movimiento reutiliza confirmaciones, histéresis y timeout de MotionEngine;
conserva la etiqueta de simulación, incluso al pasar a SENSOR_OFFLINE.

AP+STA comparte una radio; el AP sigue el canal de STA. El RSSI de clientes AP
no es el RSSI de la conexión ascendente. El canal real se consulta al SDK.
No hay escaneo de redes ajenas ni VPN; una VPN no amplifica RF.
Antes de activar STA debe revisarse acceso al dashboard HTTP desde la LAN: el
laboratorio usa WPA2 como frontera, no autenticación HTTP por usuario.

## n8n importable y credenciales

integrations/n8n/sentinel-rf.workflow.json está inactivo. Requiere n8n y el nodo
local de integrations/n8n; no se asume que estén instalados. El nodo adicional es
necesario porque Telegram estándar guarda el token en credenciales, pero suele
poner chatId en parámetros: aquí AMBOS se guardan en la credencial sentinelTelegram.
No requiere servicios de secretos comerciales ni comprar componentes.

Después de autorización de despliegue:
1. Revisar versión de n8n y compatibilidad de su API; instalar/compilar el paquete
   de nodo local con las herramientas del servidor. El paquete se verifica con TypeScript y n8n-workflow 1.x; la carga en un
   servidor n8n real sigue pendiente.
2. Importar el workflow inactivo; elegir credencial Header Auth del Webhook
   (nombre X-Sentinel-Token, valor secreto configurado fuera del workflow).
3. Crear credencial SENTINEL Telegram con bot token y chat ID; seleccionarla en
   el nodo SENTINEL Telegram. No pegar esos valores en código ni exportaciones.
4. Configurar TLS del receptor, acceso de red y hora válida del ESP32 para TLS.
5. Ajustar watchdog (por defecto 60 s), activar workflow y probar entrega.

El receptor valida esquema/tipos, filtra simulados y tipos de alerta y conserva
hasta 512 identificadores mediante workflow static data. Configurar ejecución
serial para evitar carreras entre webhook/watchdog. El estado persistente de n8n
solo debe evaluarse con workflow activo, no mediante ejecución manual aislada.
Deduplicación es acotada, no exactamente-una-vez: caída después de enviar y antes
de guardar puede duplicar. Para producción haría falta almacén transaccional.
Webhook responde al recibir; un 2xx no acredita entrega Telegram. Revisar ejecuciones.
Reintentos de Telegram limitados a tres. El token no se propaga en mensajes de error.

El watchdog empieza tras la primera recepción válida. No puede detectar una placa
que nunca haya enviado heartbeat. Avisa de ausencia, no atribuye la causa a la placa.
Un ESP32 desconectado no necesita enviar su propia alerta de pérdida de enlace.

Pendiente: importación real, compilación del nodo, configuración de credenciales,
NTP local y reloj TLS, pruebas de caída/reintentos, duplicados concurrentes y Telegram real.

SENTINEL_RF_NTP_SERVER permite indicar un servidor NTP local autorizado; por defecto
está vacío. Sin reloj posterior a 2023 se descartan envíos, sin desactivar validación TLS.
