// Setup oldal. Minden dinamikus adat textContent-tel kerul a DOM-ba (a scannelt SSID idegen adat — XSS ellen).
// Titok mezoi password tipusuak, es a szerver soha nem kuldi vissza oket.
#pragma once
#include <Arduino.h>

static const char WEB_PAGE_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Claude Monitor Setup</title>
<style>
body{font-family:system-ui,sans-serif;margin:0 auto;max-width:720px;padding:12px;background:#111;color:#eee}
h1{font-size:1.3em}h2{font-size:1.1em;border-bottom:1px solid #444;padding-bottom:4px;margin-top:24px}
table{width:100%;border-collapse:collapse}td,th{padding:4px;border-bottom:1px solid #333;text-align:left;font-size:.9em}
input,select,button{font-size:1em;padding:6px;margin:2px 0;box-sizing:border-box;background:#222;color:#eee;border:1px solid #555;border-radius:4px}
input[type=text],input[type=password],input[type=number]{width:100%}
button{cursor:pointer}button.danger{border-color:#a33}
form{background:#1b1b1b;padding:8px;border-radius:6px;margin-top:8px}
label{display:block;margin-top:6px;font-size:.85em;color:#aaa}
.msg{min-height:1.2em;color:#fc6}.muted{color:#888;font-size:.85em}
</style></head><body>
<h1>Claude Usage Monitor</h1>
<div class="msg" id="msg"></div>

<div id="loginBox" style="display:none">
<h2>Login</h2>
<form onsubmit="return doLogin(event)">
  <label>Admin password</label><input type="password" name="password" autocomplete="current-password" required>
  <button type="submit">Login</button>
</form>
</div>

<h2>Device</h2>
<table id="dev"></table>
<button onclick="post('/api/restart',{}).then(()=>say('Restarting...'))">Restart</button>

<h2>Wi-Fi profiles</h2>
<table><thead><tr><th>SSID</th><th>Priority</th><th>Enabled</th><th>Password</th><th></th></tr></thead><tbody id="wifiList"></tbody></table>
<form id="wifiForm" onsubmit="return saveWifi(event)">
  <input type="hidden" name="idx" value="-1">
  <b id="wifiFormTitle">Add Wi-Fi profile</b>
  <label>SSID</label><input type="text" name="ssid" maxlength="32" required>
  <button type="button" onclick="scan()">Scan</button> <select id="scanSel" onchange="pickScan()"><option value="">- scan results -</option></select>
  <label>Password (8-64 chars, empty = open network)</label><input type="password" name="password" maxlength="64" autocomplete="new-password">
  <label id="wifiKeepLbl" style="display:none"><input type="checkbox" name="changePassword" value="1"> clear stored password (open network)</label>
  <label>Priority (higher = preferred)</label><input type="number" name="priority" value="10" min="-1000" max="1000">
  <label><input type="checkbox" name="enabled" value="1" checked> enabled</label>
  <button type="submit">Save</button> <button type="button" onclick="resetWifiForm()">New</button>
</form>

<h2>Claude profiles</h2>
<table><thead><tr><th>Name</th><th>Source</th><th>Organization ID</th><th>Enabled</th><th>Auth</th><th>Last update</th><th></th></tr></thead><tbody id="claudeList"></tbody></table>
<form id="claudeForm" onsubmit="return saveClaude(event)">
  <input type="hidden" name="idx" value="-1">
  <b id="claudeFormTitle">Add Claude profile</b>
  <label>Name (max 12)</label><input type="text" name="name" maxlength="12" required>
  <label>Source</label><select name="transport">
    <option value="web-session">claude.ai web (sessionKey cookie)</option>
    <option value="oauth">api.anthropic.com (OAuth token)</option>
  </select>
  <label>Organization ID (UUID, needed for claude.ai web)</label><input type="text" name="orgId" maxlength="36">
  <label>Session / authentication value</label><input type="password" name="auth" maxlength="256" autocomplete="off">
  <label><input type="checkbox" name="enabled" value="1" checked> enabled</label>
  <button type="submit">Save</button> <button type="button" onclick="resetClaudeForm()">New</button>
  <div class="muted">The stored value is never shown again. When editing, leave empty to keep it.</div>
</form>

<h2>Display</h2>
<form onsubmit="return saveDisplay(event)">
  <label>Profile rotation interval (1-60 sec)</label><input type="number" id="rot" name="rotationSec" min="1" max="60" required>
  <button type="submit">Save</button>
</form>

<h2>Admin password</h2>
<form onsubmit="return saveAdmin(event)">
  <div class="muted" id="adminInfo"></div>
  <label>New admin password (8-64 chars, empty = remove protection)</label><input type="password" name="newPassword" maxlength="64" autocomplete="new-password">
  <button type="submit">Save</button> <button type="button" onclick="logout()">Logout</button>
  <div class="muted">Forgot it? Start setup mode with the BOOT button: in that mode no admin password is needed.
  Note: the device serves plain HTTP, the password travels unencrypted on the local network.</div>
</form>

<script>
const $=id=>document.getElementById(id);
function say(t){$('msg').textContent=t}
let token='';try{token=sessionStorage.getItem('cmonToken')||''}catch(e){}
function setToken(t){token=t;try{t?sessionStorage.setItem('cmonToken',t):sessionStorage.removeItem('cmonToken')}catch(e){}}
function post(url,data){
  const body=new URLSearchParams();for(const k in data)body.append(k,data[k]);
  const h={'X-CMon':'1'};if(token)h['X-CMon-Token']=token;
  return fetch(url,{method:'POST',headers:h,body}).then(r=>r.json().then(j=>{
    if(r.status===401&&url!=='/api/login'){setToken('');$('loginBox').style.display='block'}
    if(!j.ok)throw new Error(j.error||'error');return j}));
}
function doLogin(ev){ev.preventDefault();const f=ev.target;
  post('/api/login',{password:f.password.value}).then(j=>{setToken(j.token);f.reset();$('loginBox').style.display='none';say('Logged in')}).catch(e=>say(e.message));return false}
function logout(){setToken('');say('Logged out');loadStatus()}
function saveAdmin(ev){ev.preventDefault();const f=ev.target;
  post('/api/admin',{newPassword:f.newPassword.value}).then(()=>{f.reset();setToken('');say('Admin password saved - log in again if set');loadStatus()}).catch(e=>say(e.message));return false}
function formData(f){const d={};for(const el of f.elements){if(!el.name)continue;if(el.type==='checkbox'){if(el.checked)d[el.name]='1'}else d[el.name]=el.value}return d}
function cell(tr,txt){const td=document.createElement('td');td.textContent=txt;tr.appendChild(td);return td}
function btn(td,label,fn,cls){const b=document.createElement('button');b.type='button';b.textContent=label;if(cls)b.className=cls;b.onclick=fn;td.appendChild(b)}
let cfg=null;

function loadStatus(){fetch('/api/status').then(r=>r.json()).then(s=>{
  const t=$('dev');t.textContent='';
  const last=s.lastClaudeUpdate?new Date(s.lastClaudeUpdate*1000).toLocaleString():'never';
  const rows=[['Board',s.board],['Firmware',s.firmware],['Wi-Fi state',s.wifi.state],['SSID',s.wifi.ssid||s.wifi.apSsid],
    ['IP',s.wifi.ip],['RSSI',s.wifi.rssi?s.wifi.rssi+' dBm':''],['Time synced',s.timeSynced?'yes':'no'],
    ['Last Claude update',last],['Claude requests since boot',s.claudeFetchCount],['Free heap',s.freeHeap+' B'],['Uptime',s.uptimeS+' s']];
  for(const [k,v] of rows){const tr=document.createElement('tr');cell(tr,k);cell(tr,String(v));t.appendChild(tr)}
  $('adminInfo').textContent=s.adminSet?(s.adminRequired?'Admin password is set.':'Admin password is set, but not required in setup mode.'):'No admin password: anyone on this network can change the settings.';
  $('loginBox').style.display=(s.adminRequired&&!token)?'block':'none';
  if(cfg)renderClaude(s.claude);
}).catch(()=>{})}

function load(){fetch('/api/config').then(r=>r.json()).then(c=>{cfg=c;$('rot').value=c.rotationSec;renderWifi();loadStatus()})}

function renderWifi(){
  const tb=$('wifiList');tb.textContent='';
  const list=cfg.wifi.slice().sort((a,b)=>b.priority-a.priority);
  for(const w of list){const tr=document.createElement('tr');
    cell(tr,w.ssid);cell(tr,w.priority);cell(tr,w.enabled?'yes':'no');cell(tr,w.hasPassword?'set':'open');
    const td=cell(tr,'');
    btn(td,'Edit',()=>editWifi(w));
    btn(td,w.enabled?'Disable':'Enable',()=>post('/api/wifi',{idx:w.idx,ssid:w.ssid,priority:w.priority,enabled:w.enabled?'0':'1'}).then(load).catch(e=>say(e.message)));
    btn(td,'Delete',()=>{if(confirm('Delete '+w.ssid+'?'))post('/api/wifi/delete',{idx:w.idx}).then(load).catch(e=>say(e.message))},'danger');
    tb.appendChild(tr)}
}
function editWifi(w){const f=$('wifiForm');f.idx.value=w.idx;f.ssid.value=w.ssid;f.password.value='';f.priority.value=w.priority;f.enabled.checked=w.enabled;
  f.changePassword.checked=false;$('wifiKeepLbl').style.display='block';$('wifiFormTitle').textContent='Edit Wi-Fi profile';}
function resetWifiForm(){const f=$('wifiForm');f.reset();f.idx.value=-1;$('wifiKeepLbl').style.display='none';$('wifiFormTitle').textContent='Add Wi-Fi profile'}
function saveWifi(ev){ev.preventDefault();const f=ev.target;const d=formData(f);if(!d.enabled)d.enabled='0';if(d.password)d.changePassword='1';
  post('/api/wifi',d).then(()=>{say('Wi-Fi profile saved');resetWifiForm();load()}).catch(e=>say(e.message));return false}

function scan(){say('Scanning...');post('/api/scan',{}).then(()=>setTimeout(pollScan,1500)).catch(e=>say(e.message))}
function pollScan(){fetch('/api/scan').then(r=>r.json()).then(s=>{
  if(s.running){setTimeout(pollScan,1000);return}
  const sel=$('scanSel');sel.textContent='';const o0=document.createElement('option');o0.value='';o0.textContent='- '+s.results.length+' networks -';sel.appendChild(o0);
  for(const n of s.results){const o=document.createElement('option');o.value=n.ssid;o.textContent=n.ssid+' ('+n.rssi+' dBm'+(n.secure?'':', open')+')';sel.appendChild(o)}
  say('Scan done');})}
function pickScan(){const v=$('scanSel').value;if(v)$('wifiForm').ssid.value=v}

function renderClaude(st){
  const tb=$('claudeList');tb.textContent='';
  for(const c of cfg.claude){const tr=document.createElement('tr');
    const s=(st||[]).find(x=>x.idx===c.idx);
    cell(tr,c.name);cell(tr,c.transport);cell(tr,c.orgId||'-');cell(tr,c.enabled?'yes':'no');cell(tr,c.hasAuth?'set':'missing');
    cell(tr,s?(s.hasData?s.lastOkAgoS+' s ago':'-')+(s.lastError?' / '+s.lastError+(s.httpStatus?' '+s.httpStatus:''):''):'-');
    const td=cell(tr,'');
    btn(td,'Edit',()=>editClaude(c));
    btn(td,c.enabled?'Disable':'Enable',()=>post('/api/claude',{idx:c.idx,name:c.name,transport:c.transport,orgId:c.orgId,enabled:c.enabled?'0':'1'}).then(load).catch(e=>say(e.message)));
    btn(td,'Delete',()=>{if(confirm('Delete '+c.name+'?'))post('/api/claude/delete',{idx:c.idx}).then(load).catch(e=>say(e.message))},'danger');
    tb.appendChild(tr)}
}
function editClaude(c){const f=$('claudeForm');f.idx.value=c.idx;f.elements['name'].value=c.name;f.transport.value=c.transport;f.orgId.value=c.orgId;f.auth.value='';f.enabled.checked=c.enabled;
  $('claudeFormTitle').textContent='Edit Claude profile';}
function resetClaudeForm(){const f=$('claudeForm');f.reset();f.idx.value=-1;$('claudeFormTitle').textContent='Add Claude profile'}
function saveClaude(ev){ev.preventDefault();const f=ev.target;const d=formData(f);if(!d.enabled)d.enabled='0';if(d.auth)d.changeAuth='1';
  post('/api/claude',d).then(()=>{say('Claude profile saved');resetClaudeForm();load()}).catch(e=>say(e.message));return false}

function saveDisplay(ev){ev.preventDefault();post('/api/display',{rotationSec:$('rot').value}).then(()=>say('Display saved')).catch(e=>say(e.message));return false}

load();setInterval(loadStatus,5000);
</script></body></html>)HTML";
