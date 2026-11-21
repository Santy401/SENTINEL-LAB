#pragma once

#include <Arduino.h>

namespace sentinel {
namespace v02 {

// Self-contained dashboard stored in flash. No filesystem, no CDN, no frameworks.
static const char kDashboardHtml[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<meta name="color-scheme" content="dark">
<title>SENTINEL · Monitor local</title>
<style>
:root{--bg:#070b16;--card:#101a2e;--card2:#0c1425;--line:#22304f;--fg:#eaf0ff;--mut:#8fa0c8;
--green:#37e0a0;--amber:#ffc857;--red:#ff6073;--blue:#6aa9ff}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
body{margin:0 auto;max-width:680px;padding:12px 12px 28px;min-height:100vh;color:var(--fg);
background:radial-gradient(1100px 520px at 50% -12%,#16264a 0%,var(--bg) 62%);
font:15px/1.45 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif}
header{display:flex;align-items:center;justify-content:space-between;gap:10px;padding:6px 2px 14px}
.brand{font-size:19px;font-weight:800;letter-spacing:.02em}
.brand span{color:var(--mut);font-weight:600;font-size:12px;display:block;letter-spacing:.14em}
.pill{font-size:11px;font-weight:800;padding:6px 11px;border-radius:99px;background:#1e2b49;color:var(--mut);white-space:nowrap}
.pill.on{background:rgba(55,224,160,.16);color:var(--green)}
.pill.off{background:rgba(143,160,200,.12);color:var(--mut)}
.card{background:linear-gradient(180deg,rgba(255,255,255,.035),rgba(255,255,255,0)),var(--card);
border:1px solid var(--line);border-radius:16px;padding:15px;margin-bottom:11px;box-shadow:0 8px 26px rgba(0,0,0,.22)}
h2{margin:0 0 11px;font-size:11px;letter-spacing:.13em;text-transform:uppercase;color:var(--mut);font-weight:800}
.hero{text-align:center;padding:20px 15px 16px}
.state{display:inline-block;font-size:27px;font-weight:850;letter-spacing:.06em;padding:9px 24px;border-radius:14px;
background:rgba(143,160,200,.12);color:var(--mut);animation:glow 2s ease-in-out infinite}
.state.stable{background:rgba(55,224,160,.14);color:var(--green)}
.state.movement{background:rgba(255,200,87,.14);color:var(--amber)}
.state.alert{background:rgba(255,96,115,.16);color:var(--red)}
@keyframes glow{0%,100%{box-shadow:0 0 0 rgba(255,255,255,0)}50%{box-shadow:0 0 22px rgba(106,169,255,.22)}}
.subline{margin-top:10px;font-size:12px;color:var(--mut)}
.banner{display:none;align-items:center;gap:8px;margin-top:12px;padding:9px 11px;border-radius:10px;
font-size:12px;font-weight:650;background:rgba(255,200,87,.12);border:1px solid rgba(255,200,87,.42);color:var(--amber)}
.banner.show{display:flex}
.banner.nodata{background:rgba(143,160,200,.09);border-color:var(--line);color:var(--mut)}
.stats{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin-top:14px}
.stat{background:var(--card2);border:1px solid var(--line);border-radius:11px;padding:9px 6px;text-align:center}
.stat b{display:block;font-size:18px;font-variant-numeric:tabular-nums}
.stat small{color:var(--mut);font-size:10px;text-transform:uppercase;letter-spacing:.07em}
.leds{display:flex;gap:8px}
.ledwrap{flex:1;text-align:center;background:var(--card2);border:1px solid var(--line);border-radius:12px;padding:10px 4px}
.led{width:42px;height:42px;margin:0 auto 7px;border-radius:50%;background:#1b2540;border:2px solid #334166;transition:.2s}
.led.on.l1{background:var(--green);border-color:var(--green);box-shadow:0 0 20px rgba(55,224,160,.85)}
.led.on.l2{background:var(--amber);border-color:var(--amber);box-shadow:0 0 20px rgba(255,200,87,.85)}
.led.on.l3{background:var(--red);border-color:var(--red);box-shadow:0 0 20px rgba(255,96,115,.85)}
.ledwrap small{font-size:10px;color:var(--mut)}
canvas{width:100%;height:150px;display:block;background:var(--card2);border:1px solid var(--line);border-radius:11px}
.grid3{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}
.grid2{display:grid;grid-template-columns:repeat(2,1fr);gap:8px}
.metric{background:var(--card2);border:1px solid var(--line);border-radius:11px;padding:10px}
.metric small{display:block;color:var(--mut);font-size:10px;text-transform:uppercase;letter-spacing:.07em}
.metric b{font-size:19px;font-variant-numeric:tabular-nums}
.unit{font-size:10px;color:var(--mut)}
.actions{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}
button{border:1px solid #34456f;background:#1b2847;color:var(--fg);border-radius:12px;padding:13px 8px;
font-size:13px;font-weight:800;cursor:pointer;transition:.12s;min-height:52px}
button:active{transform:translateY(2px)}
button.s{background:rgba(55,224,160,.14);border-color:rgba(55,224,160,.5);color:var(--green)}
button.m{background:rgba(255,200,87,.14);border-color:rgba(255,200,87,.5);color:var(--amber)}
button.a{background:rgba(255,96,115,.15);border-color:rgba(255,96,115,.5);color:var(--red)}
button.wide{width:100%;grid-column:1/-1;background:#1d5fc4;border-color:#3b83ea}
button.ghost{background:transparent}
input[type=number]{width:100%;background:var(--card2);color:var(--fg);border:1px solid var(--line);
border-radius:10px;padding:10px;font-size:16px;font-variant-numeric:tabular-nums}
label{display:block;color:var(--mut);font-size:10px;text-transform:uppercase;letter-spacing:.07em;margin-bottom:5px}
.rowgap{margin-top:10px}
.switch{display:flex;align-items:center;gap:10px;font-size:14px;font-weight:700;margin:2px 0 10px}
details{border-top:1px solid var(--line);margin-top:12px;padding-top:11px}
summary{cursor:pointer;color:var(--mut);font-size:12px;font-weight:750;list-style:none}
summary::-webkit-details-marker{display:none}
summary:before{content:'▸ ';color:var(--blue)}
details[open] summary:before{content:'▾ '}
.note{font-size:11px;color:var(--mut);margin:9px 0 0}
ul.events{list-style:none;margin:0;padding:0;max-height:200px;overflow:auto}
ul.events li{display:flex;align-items:center;gap:8px;padding:8px 0;border-bottom:1px solid var(--line);font-size:12px}
ul.events li:last-child{border:0}
.tag{font-size:10px;font-weight:850;padding:3px 8px;border-radius:99px;background:#22304f;color:var(--mut)}
.tag.stable{background:rgba(55,224,160,.16);color:var(--green)}
.tag.movement{background:rgba(255,200,87,.16);color:var(--amber)}
.tag.alert{background:rgba(255,96,115,.18);color:var(--red)}
footer{text-align:center;color:var(--mut);font-size:10px;padding:6px 0 0}
</style>
</head>
<body>
<header>
  <div class="brand">SENTINEL<span>v0.2.0 · MONITOR LOCAL</span></div>
  <div id="conn" class="pill">CONECTANDO</div>
</header>

<section class="card hero">
  <div id="state" class="state">SENSOR_OFFLINE</div>
  <div class="subline"><span id="source" class="pill">SIN DATOS</span>
    &nbsp; <span id="demoPill" class="pill">demo: off</span></div>
  <div id="simBanner" class="banner">⚠ DATOS SIMULADOS — no son mediciones reales del acelerómetro.</div>
  <div id="noData" class="banner nodata">Sin muestras recientes. El sistema está en SENSOR_OFFLINE.</div>
  <div class="stats">
    <div class="stat"><b id="lin">0.00</b><small>movimiento m/s²</small></div>
    <div class="stat"><b id="samples">0</b><small>muestras</small></div>
    <div class="stat"><b id="uptime">0</b><small>uptime s</small></div>
  </div>
</section>

<section class="card">
  <h2>Indicadores (virtuales)</h2>
  <div class="leds">
    <div class="ledwrap"><div id="led1" class="led l1"></div><small>LED 1 · estable</small></div>
    <div class="ledwrap"><div id="led2" class="led l2"></div><small>LED 2 · movimiento</small></div>
    <div class="ledwrap"><div id="led3" class="led l3"></div><small>LED 3 · alerta</small></div>
  </div>
</section>

<section class="card">
  <h2>Prueba rápida — toca para cambiar el estado</h2>
  <div class="actions">
    <button id="btnStable" class="s">ESTABLE</button>
    <button id="btnMove" class="m">MOVIMIENTO</button>
    <button id="btnAlert" class="a">ALERTA</button>
  </div>
  <div class="rowgap">
    <label class="switch"><input id="demoToggle" type="checkbox"> Demo automática (cicla los 3 estados)</label>
    <div class="pill" id="demoPhase">fase: —</div>
  </div>
  <div class="rowgap">
    <label for="mag">Magnitud manual (m/s²) — umbrales: 2 / 6</label>
    <div style="display:flex;gap:8px;align-items:end">
      <input id="mag" type="number" min="0" max="15" step="0.5" value="4">
      <button id="btnMag" style="flex:0 0 130px">APLICAR</button>
    </div>
  </div>
  <p class="note">Cada botón enví­a una ráfaga de muestras simuladas al ESP32. El estado cambia solo con 3 muestras consecutivas por encima del umbral.</p>
</section>

<section class="card">
  <h2>Aceleración</h2>
  <div class="grid3">
    <div class="metric"><small>Eje X</small><b id="ax">0.00</b> <span class="unit">m/s²</span></div>
    <div class="metric"><small>Eje Y</small><b id="ay">0.00</b> <span class="unit">m/s²</span></div>
    <div class="metric"><small>Eje Z</small><b id="az">0.00</b> <span class="unit">m/s²</span></div>
  </div>
  <div class="grid2 rowgap">
    <div class="metric"><small>Total</small><b id="amag">0.00</b> <span class="unit">m/s²</span></div>
    <div class="metric"><small>Sin gravedad</small><b id="alin">0.00</b> <span class="unit">m/s²</span></div>
  </div>
  <p class="note">Gráfica: aceleración lineal. Líneas discontinuas = umbrales 2 y 6 m/s².</p>
  <canvas id="chart"></canvas>
</section>

<section class="card">
  <h2>Historial de eventos</h2>
  <ul id="events" class="events"><li><span class="muted">Sin eventos todavía</span></li></ul>
</section>

<section class="card">
  <details>
    <summary>Configuración de umbrales</summary>
    <div class="grid2 rowgap">
      <div><label>Movimiento</label><input id="cfgMove" type="number" step="0.1" min="0.1" value="2"></div>
      <div><label>Alerta</label><input id="cfgAlert" type="number" step="0.1" min="0.2" value="6"></div>
      <div><label>Histéresis</label><input id="cfgHyst" type="number" step="0.1" min="0" value="0.8"></div>
      <div><label>Confirmaciones</label><input id="cfgConf" type="number" step="1" min="1" max="10" value="3"></div>
    </div>
    <p><button id="btnCfg" class="wide" style="margin-top:10px">GUARDAR UMBRALES</button></p>
    <p id="cfgNote" class="note"></p>
  </details>
  <details>
    <summary>Acelerómetro real del iPhone (limitación de iOS)</summary>
    <p class="note">iOS Safari solo entrega <code>DeviceMotionEvent</code> en contexto seguro (HTTPS) y tras <code>requestPermission()</code> desde un toque. Este AP sirve HTTP, por lo que <b>no puede haber datos reales del acelerómetro aquí</b>. El botón inferior lo detecta y lo indica; nunca se inventan lecturas reales.</p>
    <p><button id="btnMotion" class="ghost wide" style="margin-top:8px">SOLICITAR ACELERÓMETRO REAL</button></p>
    <p id="motionNote" class="note">En iPhone con HTTP verás el motivo. En un navegador con HTTPS y permiso concedido, los datos entrarán como <code>BrowserDeviceMotion</code>.</p>
  </details>
</section>

<footer>SENTINEL · servidor local en ESP32-S3 · sin internet · sin servicios externos</footer>

<script>
const $=id=>document.getElementById(id);
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const fmt=(v,d=2)=>(Number(v)||0).toFixed(d);
const MAXH=64;
let history=[],cfgLoaded=false,demoOn=false,demoTimer=null,demoPhase=0,simReset=null;

async function api(path,opts){
  const r=await fetch(path,Object.assign({cache:'no-store'},opts||{}));
  if(!r.ok)throw new Error('HTTP '+r.status);
  return r.json();
}
async function post(path,obj){
  return api(path,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(obj)});
}
function setConn(ok,msg){
  $('conn').textContent=ok?'● CONECTADO':'○ SIN CONEXIÓN';
  $('conn').className='pill '+(ok?'on':'off');
  if(!ok)$('conn').title=msg||'';
}
function markSim(){
  post('/api/config',{simulation:true}).catch(()=>{});
  clearTimeout(simReset);
  simReset=setTimeout(()=>{if(!demoOn)post('/api/config',{simulation:false}).catch(()=>{});},6000);
}
// Alternating sign keeps the gravity low-pass near zero, so the linear
// magnitude reliably tracks the requested value. All samples are labelled.
async function sendBurst(mag,count=9){
  markSim();
  for(let i=0;i<count;i++){
    const s=(i%2===0)?1:-1;
    try{await post('/api/accel',{x:0.15*s,y:mag*s,z:9.81,simulated:true,source:'simulated'});}catch(e){}
    await sleep(45);
  }
  await refresh();
}
function renderEvents(events){
  const box=$('events');
  if(!events||!events.length){box.innerHTML='<li><span class="muted">Sin eventos todavía</span></li>';return;}
  box.innerHTML=events.slice().reverse().map(e=>{
    const cls=String(e.to||'').toLowerCase().replace('_','-');
    const t=(e.t/1000).toFixed(1)+' s';
    return '<li><span class="tag '+cls+'">'+e.to+'</span><span><b>'+e.type+'</b> '+
      '· '+fmt(e.mag)+' m/s² · <span class="muted">'+t+'</span></span></li>';
  }).join('');
}
function draw(s){
  const c=$('chart'),ctx=c.getContext('2d'),dpr=window.devicePixelRatio||1,w=c.clientWidth,h=c.clientHeight;
  if(c.width!==Math.round(w*dpr)||c.height!==Math.round(h*dpr)){c.width=Math.round(w*dpr);c.height=Math.round(h*dpr);}
  ctx.setTransform(dpr,0,0,dpr,0,0);ctx.clearRect(0,0,w,h);
  const max=Math.max(8,...history.map(Math.abs))*1.1,yOf=v=>h/2-(v/max)*(h/2-8);
  [2,-2,6,-6].forEach((v,i)=>{ctx.setLineDash([4,5]);ctx.strokeStyle=i<2?'rgba(55,224,160,.28)':'rgba(255,96,115,.3)';
    ctx.beginPath();ctx.moveTo(0,yOf(v));ctx.lineTo(w,yOf(v));ctx.stroke();});
  ctx.setLineDash([]);ctx.strokeStyle='rgba(143,160,200,.18)';
  ctx.beginPath();ctx.moveTo(0,yOf(0));ctx.lineTo(w,yOf(0));ctx.stroke();
  if(history.length<2)return;
  const grad=ctx.createLinearGradient(0,0,0,h);grad.addColorStop(0,'rgba(106,169,255,.35)');grad.addColorStop(1,'rgba(106,169,255,0)');
  ctx.beginPath();ctx.moveTo(0,h/2);
  history.forEach((v,i)=>ctx.lineTo(i*(w/(MAXH-1)),yOf(v)));
  ctx.lineTo((history.length-1)*(w/(MAXH-1)),h/2);ctx.closePath();ctx.fillStyle=grad;ctx.fill();
  ctx.beginPath();history.forEach((v,i)=>{const x=i*(w/(MAXH-1)),y=yOf(v);i?ctx.lineTo(x,y):ctx.moveTo(x,y);});
  ctx.strokeStyle='#6aa9ff';ctx.lineWidth=2;ctx.stroke();
}
function render(s){
  setConn(true);
  const cls=String(s.state).toLowerCase().replace('_','-');
  $('state').textContent=s.state;$('state').className='state '+cls;
  $('source').textContent=s.fresh?s.source:'SIN DATOS';
  $('source').className='pill '+(s.fresh?(s.source==='Simulated'?'on':''):'off');
  $('demoPill').textContent='demo: '+(s.simulation?'on':'off');
  $('demoPill').className='pill '+(s.simulation?'on':'off');
  $('simBanner').className='banner'+((s.fresh&&s.source==='Simulated')?' show':'');
  $('noData').className='banner nodata'+(s.fresh?'':' show');
  $('ax').textContent=fmt(s.accel.x);$('ay').textContent=fmt(s.accel.y);$('az').textContent=fmt(s.accel.z);
  $('amag').textContent=fmt(s.accel.total);$('alin').textContent=fmt(s.accel.linear);
  $('lin').textContent=fmt(s.accel.linear);$('samples').textContent=s.sampleCount;
  $('uptime').textContent=(s.uptimeMs/1000).toFixed(0);
  $('led1').className='led l1'+(s.leds.stable?' on':'');
  $('led2').className='led l2'+(s.leds.movement?' on':'');
  $('led3').className='led l3'+(s.leds.alert?' on':'');
  renderEvents(s.events);
  history.push(s.accel.linear);if(history.length>MAXH)history.shift();draw(s);
  if(!cfgLoaded&&s.thresholds){
    $('cfgMove').value=s.thresholds.movement;$('cfgAlert').value=s.thresholds.alert;
    $('cfgHyst').value=s.thresholds.hysteresis;$('cfgConf').value=s.thresholds.confirmations;cfgLoaded=true;
  }
}
async function refresh(){
  try{render(await api('/api/state'));}catch(e){setConn(false,e.message);}
}

$('btnStable').onclick=()=>sendBurst(0.2,10);
$('btnMove').onclick=()=>sendBurst(4,9);
$('btnAlert').onclick=()=>sendBurst(10,9);
$('btnMag').onclick=()=>sendBurst(Math.max(0,Math.min(15,Number($('mag').value)||0)),9);

const phases=[{name:'ESTABLE',mag:0.2},{name:'MOVIMIENTO',mag:4},{name:'ALERTA',mag:10}];
$('demoToggle').onchange=async e=>{
  demoOn=e.target.checked;
  if(demoOn){
    demoPhase=0;$('demoPhase').textContent='fase: ESTABLE';await sendBurst(phases[0].mag,9);
    demoTimer=setInterval(async()=>{demoPhase=(demoPhase+1)%3;
      $('demoPhase').textContent='fase: '+phases[demoPhase].name;
      await sendBurst(phases[demoPhase].mag,9);},4500);
  }else{
    clearInterval(demoTimer);demoTimer=null;$('demoPhase').textContent='fase: —';
    post('/api/config',{simulation:false}).catch(()=>{});
  }
};
$('btnCfg').onclick=async()=>{
  await post('/api/config',{movement:Number($('cfgMove').value),alert:Number($('cfgAlert').value),
    hysteresis:Number($('cfgHyst').value),confirmations:Number($('cfgConf').value)}).catch(()=>{});
  $('cfgNote').textContent='Umbrales enviados al ESP32.';
};

let motionOn=false,lastPost=0;
function onMotion(e){
  const a=e.accelerationIncludingGravity||e.acceleration;
  if(!a){$('motionNote').textContent='El navegador entregó aceleración nula.';return;}
  const t=Date.now();if(t-lastPost<50)return;lastPost=t;
  post('/api/accel',{x:a.x||0,y:a.y||0,z:a.z||0,simulated:false,source:'browser'}).catch(()=>{});
}
$('btnMotion').onclick=async()=>{
  const note=$('motionNote');
  if(!window.isSecureContext){
    note.textContent='BLOQUEADO POR iOS: esta página se sirve por HTTP. Safari exige HTTPS para el acelerómetro. Usa los botones de prueba rápida.';return;
  }
  if(!('DeviceMotionEvent' in window)){note.textContent='Este navegador no expone DeviceMotionEvent.';return;}
  try{
    if(typeof DeviceMotionEvent.requestPermission==='function'){
      const p=await DeviceMotionEvent.requestPermission();
      if(p!=='granted'){note.textContent='Permiso denegado ('+p+').';return;}
    }
    if(!motionOn){window.addEventListener('devicemotion',onMotion,true);motionOn=true;
      $('btnMotion').textContent='ACELERÓMETRO REAL ACTIVO';note.textContent='Capturando aceleración real.';}
  }catch(err){note.textContent='Error de permiso: '+err;}
};

setInterval(refresh,500);refresh();window.addEventListener('resize',()=>draw());
</script>
</body>
</html>)rawliteral";

}  // namespace v02
}  // namespace sentinel
