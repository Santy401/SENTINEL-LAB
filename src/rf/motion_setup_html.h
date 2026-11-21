#pragma once
#include <Arduino.h>
namespace sentinel { namespace rf {
static const char kMotionSetupHtml[] PROGMEM=R"MOTION(<!doctype html><html lang="es"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Acelerómetro nativo · SENTINEL</title><style>body{font:17px system-ui;max-width:760px;margin:26px auto;padding:20px;background:#091323;color:#e5edf8}button,input{font:inherit;padding:10px;margin:5px 0}button{cursor:pointer;background:#174c50;color:inherit;border:1px solid #45617d;border-radius:7px}input{background:#0c192b;color:inherit;border:1px solid #45617d;border-radius:7px}pre{white-space:pre-wrap;background:#112238;padding:12px;border-radius:8px}a{color:#71ddc9}.ok{color:#7fe1b6}.warn{color:#f8bb63}</style>
<h1>Acelerómetro nativo</h1><p>Esta página no usa Safari para leer sensores. Usa una app iOS nativa que envíe datos reales por HTTP local al ESP32. Sin HTTPS, sin internet y sin simulación.</p>
<h2>1 · iPhone</h2><ol><li>Conecta el iPhone a <b>SENTINEL-LAB</b>.</li><li>Abre <b>Sensor Logger</b> y activa solo <b>Accelerometer</b> y <b>Gravity</b>.</li><li>En ajustes de streaming activa <b>HTTP Push</b> con URL <code>http://192.168.4.1/api/motion/sensorlogger</code>.</li><li>Usa un periodo de lote de <b>200 ms</b> o mayor y pulsa <b>Test Pushing</b> antes de grabar.</li></ol>
<h2>2 · Emparejamiento</h2><p id="pair">Consultando estado…</p><label>ID de dispositivo permitido<br><input id="deviceId" maxlength="64" placeholder="Pega aquí el deviceId pendiente"></label><label>Intervalo mínimo entre muestras (ms)<br><input id="interval" type="number" min="10" max="1000" value="20"></label><button id="save">Guardar autorización</button> <button id="clear">Quitar autorización</button><pre id="detail">—</pre>
<h2>3 · Verificación</h2><p>Tras iniciar la grabación, comprueba <code>/api/motion/status</code> y <code>/api/state</code>. La fuente debe ser <b>NativeClient</b>, nunca <b>Simulated</b>. Si aparece <b>SENSOR_OFFLINE</b>, revisa el emparejamiento o la señal.</p>
<p><a href="/motion">Volver a movimiento y simulación</a> · <a href="/">Volver al laboratorio</a></p>
<script>
'use strict';const $=id=>document.getElementById(id);
async function api(path,data){const r=await fetch(path,data?{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)}:{cache:'no-store'});const j=await r.json();if(!r.ok)throw Error(j.error||'HTTP '+r.status);return j}
function render(s){$('detail').textContent=JSON.stringify(s,null,2);$('deviceId').value=s.allowedDeviceId||s.pendingDeviceId||'';$('interval').value=s.minIntervalMs;
$('pair').className=s.authorized?'ok':'warn';$('pair').textContent=s.authorized?'Dispositivo autorizado: '+s.allowedDeviceId:(s.pendingDeviceId?'Pendiente de autorización: '+s.pendingDeviceId:'Aún no hay dispositivo pendiente. Inicia HTTP Push y vuelve a cargar.');}
async function refresh(){try{render(await api('/api/motion/status'))}catch(e){$('pair').textContent='Error: '+e.message}}
$('save').onclick=async()=>{try{const r=await api('/api/motion/config',{deviceId:$('deviceId').value,minIntervalMs:Number($('interval').value)});if(!r.ok)throw Error(r.error||'No se guardó');refresh()}catch(e){$('pair').textContent='No se guardó: '+e.message}};
$('clear').onclick=async()=>{try{await api('/api/motion/config',{deviceId:'',minIntervalMs:Number($('interval').value)});refresh()}catch(e){$('pair').textContent='No se pudo quitar: '+e.message}};
refresh();setInterval(refresh,3000);
</script></html>)MOTION";
} }
