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
.msg{min-height:1.2em;color:#fc6;position:sticky;top:0;background:#111;z-index:5;padding:4px 0}.muted{color:#888;font-size:.85em}
.tw{overflow-x:auto}
button.primary,a.primary{display:block;width:100%;padding:12px;margin-top:8px;background:#1f5f3a;border:1px solid #3c9;color:#fff;font-weight:bold;text-align:center;text-decoration:none;border-radius:4px;box-sizing:border-box}
</style></head><body>
<h1>Claude Usage Monitor</h1>
<div class="msg" id="msg"></div>

<div id="main">
<h2>Device</h2>
<table id="dev"></table>
<button onclick="post('/api/restart',{}).then(()=>say('Restarting...'))">Restart</button>

<h2>Wi-Fi profiles</h2>
<div class="tw"><table><thead><tr><th>SSID</th><th>Priority</th><th>Enabled</th><th>Password</th><th></th></tr></thead><tbody id="wifiList"></tbody></table></div>
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
<div class="tw"><table><thead><tr><th>Name</th><th>Source</th><th>Organization ID</th><th>Enabled</th><th>Auth</th><th>Last update</th><th></th></tr></thead><tbody id="claudeList"></tbody></table></div>
<form id="claudeForm" onsubmit="return saveClaude(event)">
  <input type="hidden" name="idx" value="-1">
  <b id="claudeFormTitle">Add Claude profile</b>
  <label>Name (max 12, empty = Profile-XX)</label><input type="text" name="name" maxlength="12" placeholder="Profile-XX">
  <label>Source</label><select name="transport" onchange="onTransport()">
    <option value="oauth">api.anthropic.com (OAuth, recommended)</option>
    <option value="web-session">claude.ai web (sessionKey cookie)</option>
  </select>
  <div id="orgRow"><label>Organization ID (UUID, needed for claude.ai web)</label><input type="text" name="orgId" maxlength="36"></div>
  <div id="authRow"><label>sessionKey cookie value</label><input type="password" name="auth" maxlength="300" autocomplete="off">
    <div class="muted">The stored value is never shown again. When editing, leave empty to keep it.</div></div>
  <label><input type="checkbox" name="enabled" value="1" checked> enabled</label>
  <button type="submit">Save</button> <button type="button" onclick="resetClaudeForm()">New</button>
  <div id="oauthRow">
    <button type="button" class="primary" onclick="authNow()">Authenticate now</button>
    <div class="muted">Saves this profile and opens the Claude sign-in page in a new tab. Approve there, copy the code, paste it below.</div>
  </div>
</form>

<div id="loginFlow" style="display:none;background:#1b1b1b;padding:8px;border-radius:6px;margin-top:8px">
  <b>OAuth login</b>
  <div class="muted">1. Sign in to Claude and approve (if the tab did not open, use this button):</div>
  <a id="authUrl" class="primary" href="#" target="_blank" rel="noopener">Open Claude sign-in page</a>
  <div class="muted">2. After approving, the page shows a code. Paste it here (format <code>code#state</code>):</div>
  <input type="text" id="oauthCode" placeholder="code#state" autocomplete="off">
  <button type="button" class="primary" onclick="finishLogin()">Submit code</button>
  <button type="button" onclick="$('loginFlow').style.display='none'">Cancel</button>
</div>

<h2>Display &amp; refresh</h2>
<form onsubmit="return saveDisplay(event)">
  <label>Profile rotation interval (1-60 sec)</label><input type="number" id="rot" name="rotationSec" min="1" max="60" required>
  <label>Usage refresh interval per profile (60-3600 sec)</label><input type="number" id="refr" min="60" max="3600" required>
  <button type="submit">Save</button>
</form>

<h2>Time zone</h2>
<form onsubmit="return saveTz(event)">
  <div class="muted">Device local time: <span id="devTime">-</span></div>
  <label>Time zone (used for reset times on the display)</label>
  <select id="tzSel" onchange="onTzSel()">
    <option value="CET-1CEST,M3.5.0,M10.5.0/3">Europe/Budapest, Vienna, Berlin, Paris (CET/CEST)</option>
    <option value="GMT0BST,M3.5.0/1,M10.5.0">Europe/London (GMT/BST)</option>
    <option value="EET-2EEST,M3.5.0/3,M10.5.0/4">Europe/Helsinki, Athens, Bucharest (EET/EEST)</option>
    <option value="MSK-3">Europe/Moscow (MSK)</option>
    <option value="UTC0">UTC</option>
    <option value="EST5EDT,M3.2.0,M11.1.0">America/New_York (EST/EDT)</option>
    <option value="CST6CDT,M3.2.0,M11.1.0">America/Chicago (CST/CDT)</option>
    <option value="MST7MDT,M3.2.0,M11.1.0">America/Denver (MST/MDT)</option>
    <option value="PST8PDT,M3.2.0,M11.1.0">America/Los_Angeles (PST/PDT)</option>
    <option value="<+04>-4">Asia/Dubai (+04)</option>
    <option value="IST-5:30">Asia/Kolkata (IST)</option>
    <option value="<+08>-8">Asia/Singapore (+08)</option>
    <option value="JST-9">Asia/Tokyo (JST)</option>
    <option value="AEST-10AEDT,M10.1.0,M4.1.0/3">Australia/Sydney (AEST/AEDT)</option>
    <option value="">Custom POSIX TZ string...</option>
  </select>
  <label>POSIX TZ string</label><input type="text" id="tzStr" maxlength="47" autocomplete="off">
  <button type="submit">Save time zone</button>
</form>

<h2>Admin password</h2>
<form onsubmit="return saveAdmin(event)">
  <div class="muted" id="adminInfo"></div>
  <label>New admin password (8-64 chars, empty = remove protection)</label><input type="password" name="newPassword" maxlength="64" autocomplete="new-password">
  <button type="submit">Save</button> <button type="button" onclick="logout()">Logout</button>
  <div class="muted">Forgot it? Start setup mode with the BOOT button: in that mode no admin password is needed.
  Note: the device serves plain HTTP, the password travels unencrypted on the local network.</div>
</form>
</div>


<script>
const $=id=>document.getElementById(id);
function say(t){$('msg').textContent=t}
let token='';try{token=sessionStorage.getItem('sid')||''}catch(e){}
function setToken(t){token=t;try{t?sessionStorage.setItem('sid',t):sessionStorage.removeItem('sid')}catch(e){}}
function post(url,data){
  const body=new URLSearchParams();for(const k in data)body.append(k,data[k]);
  const h={'X-CMon':'1'};if(token)h['X-CMon-Token']=token;
  return fetch(url,{method:'POST',headers:h,body}).then(r=>r.json().then(j=>{
    if(r.status===401&&url!=='/api/login'){setToken('');lock()}
    if(!j.ok)throw new Error(j.error||'error');return j}));
}
// Admin-jelszo eseten belepes nelkul csak a login latszik; az olvaso API-k is tokent kernek (401).
// Lejart/ervenytelen munkamenet: vissza a semleges login-hejra (/). A felulet csak belepve toltodik le.
function lock(){setToken('');location.replace('/')}
function unlock(){}
function apiGet(url){const h={};if(token)h['X-CMon-Token']=token;
  return fetch(url,{headers:h,cache:'no-store'}).then(r=>{if(r.status===401){setToken('');lock();throw new Error('login required')}return r.json()})}
function logout(){lock()}
function saveAdmin(ev){ev.preventDefault();const f=ev.target;
  post('/api/admin',{newPassword:f.newPassword.value}).then(()=>{f.reset();setToken('');say('Admin password saved - log in again if set');loadStatus()}).catch(e=>say(e.message));return false}
function formData(f){const d={};for(const el of f.elements){if(!el.name)continue;if(el.type==='checkbox'){if(el.checked)d[el.name]='1'}else d[el.name]=el.value}return d}
function cell(tr,txt){const td=document.createElement('td');td.textContent=txt;tr.appendChild(td);return td}
function btn(td,label,fn,cls){const b=document.createElement('button');b.type='button';b.textContent=label;if(cls)b.className=cls;b.onclick=fn;td.appendChild(b)}
let cfg=null;

function loadStatus(){apiGet('/api/status').then(s=>{
  if(s.locked){lock();return}
  unlock();
  $('devTime').textContent=s.localTime?s.localTime+' ('+s.tz+')':'not synced yet';
  const t=$('dev');t.textContent='';
  const last=s.lastClaudeUpdate?new Date(s.lastClaudeUpdate*1000).toLocaleString():'never';
  const rows=[['Board',s.board],['Firmware',s.firmware],['Wi-Fi state',s.wifi.state],['SSID',s.wifi.ssid||s.wifi.apSsid],
    ['IP',s.wifi.ip],['RSSI',s.wifi.rssi?s.wifi.rssi+' dBm':''],['Time synced',s.timeSynced?'yes':'no'],
    ['Last Claude update',last],['Claude requests since boot',s.claudeFetchCount],['Free heap',s.freeHeap+' B'],['Uptime',s.uptimeS+' s']];
  for(const [k,v] of rows){const tr=document.createElement('tr');cell(tr,k);cell(tr,String(v));t.appendChild(tr)}
  $('adminInfo').textContent=s.adminSet?(s.adminRequired?'Admin password is set.':'Admin password is set, but not required in setup mode.'):'No admin password: anyone on this network can change the settings.';
  if(cfg)renderClaude(s.claude);
}).catch(()=>{})}

function load(tries){tries=tries||0;apiGet('/api/config').then(c=>{unlock();cfg=c;$('rot').value=c.rotationSec;$('refr').value=c.refreshSec;showTz(c.tz||'');renderWifi();onTransport();loadStatus()}).catch(e=>{if(e.message==='login required'){say('Log in to view and change the settings');loadStatus();return}if(tries<15){say('Device busy (Wi-Fi reconnect?), retrying...');setTimeout(()=>load(tries+1),2000)}else say('Device not reachable - reload the page')})}

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
function pollScan(){apiGet('/api/scan').then(s=>{
  if(s.running){setTimeout(pollScan,1000);return}
  const sel=$('scanSel');sel.textContent='';const o0=document.createElement('option');o0.value='';o0.textContent='- '+s.results.length+' networks -';sel.appendChild(o0);
  for(const n of s.results){const o=document.createElement('option');o.value=n.ssid;o.textContent=n.ssid+' ('+n.rssi+' dBm'+(n.secure?'':', open')+')';sel.appendChild(o)}
  say('Scan done: '+s.results.length+' networks');}).catch(e=>{say('Scan: connection lost, retrying...');setTimeout(pollScan,1500)})}
function pickScan(){const v=$('scanSel').value;if(v)$('wifiForm').ssid.value=v}

function renderClaude(st){
  const tb=$('claudeList');tb.textContent='';
  for(const c of cfg.claude){const tr=document.createElement('tr');
    const s=(st||[]).find(x=>x.idx===c.idx);
    const authState=c.transport==='oauth'?(c.hasRefresh?(c.expiresAt?'token, exp '+new Date(c.expiresAt*1000).toLocaleTimeString():'token'):'NOT LOGGED IN'):(c.hasAuth?'set':'missing');
    cell(tr,c.name);cell(tr,c.transport);cell(tr,c.orgId||'-');cell(tr,c.enabled?'yes':'no');cell(tr,authState);
    cell(tr,s?(s.hasData?s.lastOkAgoS+' s ago':'-')+(s.lastError?' / '+s.lastError+(s.httpStatus?' '+s.httpStatus:''):''):'-');
    const td=cell(tr,'');
    btn(td,'Edit',()=>editClaude(c));
    if(c.transport==='oauth')btn(td,'Authenticate',()=>{const w=window.open('about:blank','_blank');startLogin(c.idx,w).catch(e=>say(e.message))});
    btn(td,c.enabled?'Disable':'Enable',()=>post('/api/claude',{idx:c.idx,name:c.name,transport:c.transport,orgId:c.orgId,enabled:c.enabled?'0':'1'}).then(load).catch(e=>say(e.message)));
    btn(td,'Delete',()=>{if(confirm('Delete '+c.name+'?'))post('/api/claude/delete',{idx:c.idx}).then(load).catch(e=>say(e.message))},'danger');
    tb.appendChild(tr)}
}
function editClaude(c){const f=$('claudeForm');f.idx.value=c.idx;f.elements['name'].value=c.name;f.transport.value=c.transport;f.orgId.value=c.orgId;f.auth.value='';f.enabled.checked=c.enabled;
  onTransport();$('claudeFormTitle').textContent='Edit Claude profile';}
function onTransport(){const oauth=$('claudeForm').transport.value==='oauth';$('orgRow').style.display=oauth?'none':'block';$('authRow').style.display=oauth?'none':'block';$('oauthRow').style.display=oauth?'block':'none';}
let loginIdx=-1;
// A bongeszo csak kattintasra enged uj fulet: a hivo a kattintaskor nyit egy ures fulet (w), ide kerul az URL.
function startLogin(idx,w){loginIdx=idx;return post('/api/oauth/start',{idx}).then(j=>{$('authUrl').href=j.authorizeUrl;$('oauthCode').value='';
  $('loginFlow').style.display='block';$('loginFlow').scrollIntoView({behavior:'smooth'});
  if(w&&!w.closed){w.location.href=j.authorizeUrl;say('Claude sign-in opened in a new tab. Approve, copy the code, paste it below.')}
  else say('Tap "Open Claude sign-in page", approve, then paste the code below.');}).catch(e=>{if(w&&!w.closed)w.close();throw e});}
function authNow(){const f=$('claudeForm');const d=formData(f);if(!d.enabled)d.enabled='0';delete d.auth;delete d.orgId;d.transport='oauth';
  const w=window.open('about:blank','_blank');
  post('/api/claude',d).then(j=>{f.idx.value=j.idx;$('claudeFormTitle').textContent='Edit Claude profile';load();return startLogin(j.idx,w)})
    .catch(e=>{if(w&&!w.closed)w.close();say(e.message)})}
function finishLogin(){post('/api/oauth/finish',{idx:loginIdx,code:$('oauthCode').value}).then(()=>{$('loginFlow').style.display='none';$('oauthCode').value='';say('Logged in');load();}).catch(e=>say(e.message));}
function resetClaudeForm(){const f=$('claudeForm');f.reset();f.idx.value=-1;onTransport();$('claudeFormTitle').textContent='Add Claude profile'}
function saveClaude(ev){ev.preventDefault();const f=ev.target;const d=formData(f);if(!d.enabled)d.enabled='0';if(d.transport==='oauth'){delete d.auth;delete d.orgId}else if(d.auth)d.changeAuth='1';
  post('/api/claude',d).then(()=>{say('Claude profile saved');resetClaudeForm();load()}).catch(e=>say(e.message));return false}

function onTzSel(){const v=$('tzSel').value;if(v)$('tzStr').value=v;else $('tzStr').focus()}
function showTz(tz){$('tzStr').value=tz;const o=[...$('tzSel').options].find(x=>x.value===tz);$('tzSel').value=o?tz:''}
function saveTz(ev){ev.preventDefault();post('/api/timezone',{tz:$('tzStr').value.trim()}).then(j=>{say('Time zone saved, local time: '+j.localTime);$('devTime').textContent=j.localTime}).catch(e=>say(e.message));return false}
function saveDisplay(ev){ev.preventDefault();Promise.all([post('/api/display',{rotationSec:$('rot').value}),post('/api/refresh',{refreshSec:$('refr').value})]).then(()=>say('Saved')).catch(e=>say(e.message));return false}

load();setInterval(loadStatus,5000);
</script></body></html>)HTML";

// Belepes nelkul CSAK ez megy ki a "/"-re (projektgazda, 2026-09-17: idegen ne tudja meg, mi fut az eszkozon).
// Nincs benne termeknev, API-lista vagy felulet. Sikeres (vagy jelszo nelkuli) eleres utan a /api/ui adja a feluletet.
static const char LOGIN_SHELL_HTML[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Login</title>
<style>body{font-family:system-ui,sans-serif;margin:0 auto;max-width:360px;padding:24px;background:#111;color:#eee}
input,button{font-size:1em;padding:10px;margin:6px 0;width:100%;box-sizing:border-box;background:#222;color:#eee;border:1px solid #555;border-radius:4px}
#m{min-height:1.2em;color:#fc6}</style></head><body>
<div id="m"></div>
<form id="f" style="display:none" onsubmit="return go(event)">
<input type="password" id="p" autocomplete="current-password" placeholder="Password" required>
<button type="submit">Login</button>
</form>
<script>
let t='';try{t=sessionStorage.getItem('sid')||''}catch(e){}
const m=x=>document.getElementById('m').textContent=x;
function ui(){fetch('/api/ui',{headers:t?{'X-CMon-Token':t}:{},cache:'no-store'}).then(r=>{
  if(r.status===200)return r.text().then(h=>{document.open();document.write(h);document.close()});
  t='';try{sessionStorage.removeItem('sid')}catch(e){}
  document.getElementById('f').style.display='block';document.getElementById('p').focus()}).catch(()=>m('Not reachable'))}
function go(ev){ev.preventDefault();const b=new URLSearchParams();b.append('password',document.getElementById('p').value);
  fetch('/api/login',{method:'POST',headers:{'X-CMon':'1'},body:b}).then(r=>r.json()).then(j=>{
    if(!j.ok){m(j.error||'Login failed');return}
    t=j.token;try{sessionStorage.setItem('sid',t)}catch(e){};document.getElementById('p').value='';ui()}).catch(()=>m('Not reachable'));return false}
ui();
</script></body></html>)HTML";
