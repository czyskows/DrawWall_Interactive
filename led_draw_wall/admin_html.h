#pragma once
// Admin page: http://192.168.4.1/admin?key=YOUR_ADMIN_KEY
const char ADMIN_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Wall Admin</title>
<style>
body{background:#101012;color:#eee;font-family:system-ui,sans-serif;margin:16px}
h2{margin:22px 0 10px;font-size:19px}
.g{display:grid;grid-template-columns:repeat(auto-fill,minmax(130px,1fr));gap:12px}
.c{background:#1d1d22;border-radius:10px;padding:8px;text-align:center;font-size:13px}
canvas{width:100%;image-rendering:pixelated;background:#000;border-radius:6px}
.b{display:flex;gap:6px;margin-top:6px}
button{flex:1;padding:9px 4px;border:0;border-radius:8px;color:#fff;font-size:14px}
.ok{background:#2f8f5b}.del{background:#c33}
.empty{color:#888;font-size:14px}
</style></head><body>
<h2>Waiting for approval <span id="np"></span></h2><div class="g" id="gp"></div>
<h2>In the slideshow <span id="na"></span></h2><div class="g" id="ga"></div>
<script>
const W=27,H=25,key=new URLSearchParams(location.search).get('key')||prompt('Admin key')||'';
const K=encodeURIComponent(key),$=s=>document.getElementById(s);
let lists={pending:[],approved:[]};
function counts(){$('np').textContent=`(${lists.pending.length})`;$('na').textContent=`(${lists.approved.length})`;
  for(const[g,l,t]of[['gp','pending','Nothing waiting 🎉'],['ga','approved','No approved drawings yet']])
    if(!lists[l].length)$(g).innerHTML=`<div class="empty">${t}</div>`}
function btn(cls,txt,fn){const b=document.createElement('button');b.className=cls;b.textContent=txt;b.onclick=fn;return b}
async function card(id,isP){
  const d=document.createElement('div');d.className='c';
  const c=document.createElement('canvas');c.width=W;c.height=H;
  const row=document.createElement('div');row.className='b';
  const q=`id=${id}&key=${K}`+(isP?'&p=1':'');
  const drop=l=>{lists[l]=lists[l].filter(x=>x!=id)};
  if(isP)row.append(btn('ok','Approve',async()=>{
    if((await fetch('/api/approve?'+q)).ok){drop('pending');lists.approved.push(id);d.remove();
      $('ga').querySelector('.empty')?.remove();$('ga').prepend(await card(id,false));counts()}}));
  row.append(btn('del','Delete',async()=>{if(!confirm('Delete drawing #'+id+'?'))return;
    if((await fetch('/api/del?'+q)).ok){drop(isP?'pending':'approved');d.remove();counts()}}));
  d.append(c,row);
  fetch('/api/img?'+q).then(r=>r.arrayBuffer()).then(buf=>{const px=new Uint8Array(buf),x=c.getContext('2d'),im=x.createImageData(W,H);
    for(let i=0;i<W*H;i++){im.data[i*4]=px[i*3];im.data[i*4+1]=px[i*3+1];im.data[i*4+2]=px[i*3+2];im.data[i*4+3]=255}
    x.putImageData(im,0,0)});
  return d}
async function load(){
  const r=await fetch('/api/list?key='+K);
  if(!r.ok){document.body.innerHTML='<h2>Wrong admin key</h2>';return}
  lists=await r.json();
  for(const id of [...lists.pending].reverse())$('gp').append(await card(id,true));
  for(const id of [...lists.approved].reverse())$('ga').append(await card(id,false));
  counts()}
load();
</script></body></html>)rawliteral";
