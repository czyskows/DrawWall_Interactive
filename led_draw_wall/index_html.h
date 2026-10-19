#pragma once
// Drawing page served at http://192.168.4.1/
const char INDEX_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>LED Draw Wall</title>
<style>
:root{--bg:#101012;--panel:#1d1d22;--fg:#f2f2f2;--muted:#9a9aa3;--accent:#ff3d7f}
*{box-sizing:border-box}
html,body{margin:0;background:var(--bg);color:var(--fg);font-family:system-ui,-apple-system,sans-serif;overscroll-behavior:none}
body{display:flex;flex-direction:column;align-items:center;gap:12px;padding:14px 12px 24px;user-select:none;-webkit-user-select:none}
header,.row,#wrap{width:100%;max-width:520px}
header{display:flex;justify-content:space-between;align-items:baseline}
h1{font-size:20px;margin:0}
#status{font-size:13px;color:var(--muted)}
canvas{width:100%;display:block;touch-action:none;border-radius:10px;background:#000}
.row{display:flex;gap:8px;flex-wrap:wrap;justify-content:center}
.sw{width:34px;height:34px;border-radius:50%;border:3px solid #2a2a30;cursor:pointer;padding:0}
.sw.on{border-color:#fff;transform:scale(1.12)}
#custom{position:relative;overflow:hidden;background:conic-gradient(red,yellow,lime,cyan,blue,magenta,red)}
#custom input{position:absolute;inset:0;opacity:0;width:100%;height:100%;cursor:pointer}
button{flex:1;min-width:64px;padding:11px 6px;border:0;border-radius:12px;background:var(--panel);color:var(--fg);font-size:15px;cursor:pointer}
button.on{background:var(--accent);color:#fff}
#save{background:#2f8f5b}
#scrollrow{align-items:center}
#scroll{flex:none;min-width:110px}
#speedwrap{flex:1;display:flex;align-items:center;gap:8px;color:var(--muted);font-size:14px}
#speed{flex:1;accent-color:var(--accent)}
#toast{position:fixed;bottom:22px;left:50%;transform:translateX(-50%);background:#fff;color:#111;padding:10px 18px;border-radius:22px;font-size:15px;opacity:0;transition:opacity .3s;pointer-events:none;width:max-content;max-width:90vw;text-align:center}
#toast.show{opacity:1}
#overlay{position:fixed;inset:0;background:rgba(0,0,0,.9);display:none;flex-direction:column;align-items:center;justify-content:center;gap:16px;text-align:center;padding:28px;font-size:18px}
#overlay button{flex:none;padding:12px 26px;background:var(--accent)}
</style></head><body>
<header><h1>Draw on the wall!</h1><span id="status">Connecting…</span></header>
<div id="wrap"><canvas id="cv"></canvas></div>
<div class="row" id="pal"></div>
<div class="row">
  <button data-tool="pen" class="on">✏️ Pen</button>
  <button data-tool="fill">🪣 Fill</button>
  <button data-tool="eraser">🧽 Erase</button>
</div>
<div class="row">
  <button data-size="1" class="on">Small</button>
  <button data-size="2">Medium</button>
  <button data-size="3">Big</button>
</div>
<div class="row" id="scrollrow">
  <button id="scroll">↔ Scroll</button>
  <label id="speedwrap">🐢<input id="speed" type="range" min="1" max="20" value="6">🐇</label>
</div>
<div class="row">
  <button id="clear">Clear</button>
  <button id="save">Save to the show ⭐</button>
</div>
<div id="toast"></div>
<div id="overlay"><div id="omsg"></div><button onclick="location.reload()">Try again</button></div>
<script>
const W=27,H=25,C=20;
const cv=document.getElementById('cv'),ctx=cv.getContext('2d');
cv.width=W*C;cv.height=H*C;
const px=new Uint8Array(W*H*3);
const PAL=['#ff0000','#ff6a00','#ffd000','#7dff00','#00ff40','#00ffd0','#0090ff','#2a2aff','#9b00ff','#ff00c8','#ff6b9d','#ffffff'];
let color=[255,0,0],tool='pen',size=1,ws=null,pend=[],pc=null,last=null,full=false,off=0,scrolling=false;
const wrap=v=>((v%W)+W)%W; // screen column <-> canvas column while scrolling

const $=s=>document.querySelector(s);
function hex2rgb(h){const n=parseInt(h.slice(1),16);return[n>>16&255,n>>8&255,n&255]}
function status(t){$('#status').textContent=t}
let tt;function toast(t){const e=$('#toast');e.textContent=t;e.classList.add('show');clearTimeout(tt);tt=setTimeout(()=>e.classList.remove('show'),2600)}
function overlay(t){$('#omsg').textContent=t;$('#overlay').style.display='flex'}

// ---- palette
function pick(el,rgb){document.querySelectorAll('.sw').forEach(s=>s.classList.remove('on'));el.classList.add('on');color=rgb;if(tool=='eraser')setTool('pen')}
PAL.forEach((h,i)=>{const b=document.createElement('button');b.className='sw'+(i?'':' on');b.style.background=h;b.onclick=()=>pick(b,hex2rgb(h));$('#pal').append(b)});
const cu=document.createElement('label');cu.className='sw';cu.id='custom';cu.innerHTML='<input type="color" value="#ff00ff">';
cu.querySelector('input').oninput=e=>{cu.style.background=e.target.value;pick(cu,hex2rgb(e.target.value))};
$('#pal').append(cu);

// ---- tools
function setTool(t){tool=t;document.querySelectorAll('[data-tool]').forEach(b=>b.classList.toggle('on',b.dataset.tool==t))}
document.querySelectorAll('[data-tool]').forEach(b=>b.onclick=()=>setTool(b.dataset.tool));
document.querySelectorAll('[data-size]').forEach(b=>b.onclick=()=>{size=+b.dataset.size;document.querySelectorAll('[data-size]').forEach(x=>x.classList.toggle('on',x==b))});
$('#clear').onclick=()=>{if(confirm('Clear the whole wall? (This clears it for everyone drawing.)')&&ws&&ws.readyState==1)ws.send(new Uint8Array([2]))};
function sendScroll(){if(ws&&ws.readyState==1)ws.send(new Uint8Array([4,scrolling?1:0,+$('#speed').value]))}
$('#scroll').onclick=()=>{scrolling=!scrolling;$('#scroll').classList.toggle('on',scrolling);sendScroll()};
$('#speed').oninput=()=>{if(scrolling)sendScroll()};
$('#save').onclick=()=>{flush();if(ws&&ws.readyState==1)ws.send(new Uint8Array([3]))};

// ---- rendering
function drawCell(x,y){const i=(y*W+x)*3,r=px[i],g=px[i+1],b=px[i+2],sx=wrap(x-off);
  ctx.fillStyle='#000';ctx.fillRect(sx*C,y*C,C,C);
  ctx.fillStyle=(r|g|b)?`rgb(${r},${g},${b})`:'#202024';
  ctx.beginPath();ctx.arc(sx*C+C/2,y*C+C/2,C*0.42,0,7);ctx.fill()}
function redraw(){for(let y=0;y<H;y++)for(let x=0;x<W;x++)drawCell(x,y)}
function setPx(x,y,c){if(x<0||y<0||x>=W||y>=H)return false;const i=(y*W+x)*3;
  if(px[i]==c[0]&&px[i+1]==c[1]&&px[i+2]==c[2])return false;
  px[i]=c[0];px[i+1]=c[1];px[i+2]=c[2];drawCell(x,y);return true}

// ---- sending (batched)
function same(a,b){return a&&b&&a[0]==b[0]&&a[1]==b[1]&&a[2]==b[2]}
function queue(x,y,c){if(pc&&!same(pc,c))flush();pc=c;pend.push(x,y);if(pend.length>=400)flush()}
function flush(){if(!pend.length)return;if(ws&&ws.readyState==1){const b=new Uint8Array(4+pend.length);b[0]=1;b.set(pc,1);b.set(pend,4);ws.send(b)}pend=[]}
setInterval(flush,40);

// ---- drawing
function paint(x,y){const c=tool=='eraser'?[0,0,0]:color,lo=-Math.floor((size-1)/2); // x,y = screen cell
  for(let dy=lo;dy<lo+size;dy++)for(let dx=lo;dx<lo+size;dx++){const X=x+dx,Y=y+dy;if(X<0||X>=W)continue;
    const cx=wrap(X+off);if(setPx(cx,Y,c))queue(cx,Y,c)}}
function line(x0,y0,x1,y1){let dx=Math.abs(x1-x0),dy=-Math.abs(y1-y0),sx=x0<x1?1:-1,sy=y0<y1?1:-1,e=dx+dy;
  for(;;){paint(x0,y0);if(x0==x1&&y0==y1)break;const e2=2*e;if(e2>=dy){e+=dy;x0+=sx}if(e2<=dx){e+=dx;y0+=sy}}}
function fill(x,y){if(x<0||y<0||x>=W||y>=H)return;const i=(y*W+x)*3,t=[px[i],px[i+1],px[i+2]],c=color;
  if(same(t,c))return;const st=[[x,y]],seen=new Uint8Array(W*H);
  while(st.length){const[a,b]=st.pop();if(a<0||b<0||a>=W||b>=H)continue;const k=b*W+a;if(seen[k])continue;seen[k]=1;
    const j=k*3;if(px[j]!=t[0]||px[j+1]!=t[1]||px[j+2]!=t[2])continue;
    setPx(a,b,c);queue(a,b,c);st.push([a+1,b],[a-1,b],[a,b+1],[a,b-1])}
  flush()}
function cell(e){const r=cv.getBoundingClientRect();return[Math.floor((e.clientX-r.left)/r.width*W),Math.floor((e.clientY-r.top)/r.height*H)]}
cv.addEventListener('pointerdown',e=>{e.preventDefault();cv.setPointerCapture(e.pointerId);const[x,y]=cell(e);
  if(tool=='fill'){fill(wrap(x+off),y);return}last=[x,y];paint(x,y)});
cv.addEventListener('pointermove',e=>{if(!last)return;const[x,y]=cell(e);line(last[0],last[1],x,y);last=[x,y]});
['pointerup','pointercancel'].forEach(t=>cv.addEventListener(t,()=>{last=null;flush()}));

// ---- connection
function connect(){
  ws=new WebSocket(`ws://${location.host}/ws`);ws.binaryType='arraybuffer';
  ws.onopen=()=>status('Connected');
  ws.onmessage=e=>{
    if(typeof e.data=='string'){const m=JSON.parse(e.data);
      if(m.t=='users')status(m.n<=1?'Just you drawing':`${m.n} people drawing together`);
      else if(m.t=='full'){full=true;overlay('Two people are already drawing on the wall. Give it a minute and try again!')}
      else if(m.t=='saved')toast(m.pending?'Saved! It\'ll join the show once it\'s approved ✨':'Saved! It\'ll show up when the wall is resting ✨');
      else if(m.t=='msg')toast(m.m);
      else if(m.t=='scroll'){scrolling=!!m.on;$('#scroll').classList.toggle('on',scrolling);$('#speed').value=m.sp;if(!scrolling){off=0;redraw()}}
      return}
    const b=new Uint8Array(e.data);
    if(b[0]==0x10){px.set(b.subarray(1,1+W*H*3));redraw()}
    else if(b[0]==0x11){off=b[1];redraw()}
    else if(b[0]==1){const c=[b[1],b[2],b[3]];for(let i=4;i+1<b.length;i+=2)setPx(b[i],b[i+1],c)}
  };
  ws.onclose=()=>{if(full)return;status('Reconnecting…');setTimeout(connect,1500)};
}
redraw();connect();
</script></body></html>)rawliteral";
