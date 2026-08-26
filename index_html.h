const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="ko">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#151515">
<title>TESLA UNLOCK</title>
<style>
:root{--bg:#f2f2f0;--card:#fff;--ink:#171717;--muted:#777;--line:#dededb;--green:#2b8a55;--amber:#b27a19;--red:#c9403a;--shadow:0 8px 30px rgba(0,0,0,.07)}
@media(prefers-color-scheme:dark){:root{--bg:#151515;--card:#202020;--ink:#f2f2f2;--muted:#999;--line:#343434;--shadow:none}}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}html,body{margin:0;background:var(--bg);color:var(--ink);font-family:-apple-system,BlinkMacSystemFont,"Helvetica Neue",Arial,sans-serif}button,input{font:inherit}button{cursor:pointer}.wrap{max-width:520px;margin:auto;min-height:100vh;padding:calc(12px + env(safe-area-inset-top)) 14px calc(26px + env(safe-area-inset-bottom))}.top{height:48px;display:flex;align-items:center;justify-content:space-between}.logo{font-size:15px;font-weight:700;letter-spacing:-.02em}.live{display:flex;align-items:center;gap:7px;font-size:11px;color:var(--muted)}.live:before{content:"";width:7px;height:7px;border-radius:50%;background:var(--red)}.live.ok:before{background:var(--green)}
.drive{padding:24px 8px 18px;text-align:center}.small{font-size:10px;color:var(--muted);font-weight:650;letter-spacing:.06em}.mode{font-size:46px;line-height:1.03;font-weight:510;letter-spacing:-.055em;margin:8px 0 20px}.mode.good{color:var(--green)}.mode.warn{color:var(--amber)}.mode.bad{color:var(--red)}.mode.compact{font-size:30px;letter-spacing:-.04em}
.metrics{display:grid;grid-template-columns:repeat(1,1fr);gap:1px;background:var(--line);border:1px solid var(--line);border-radius:18px;overflow:hidden;box-shadow:var(--shadow)}.metric{background:var(--card);padding:14px 7px;text-align:center}.metric .k{font-size:9px;color:var(--muted);font-weight:650}.metric .v{font-size:17px;margin-top:5px;font-weight:580;letter-spacing:-.03em}.good{color:var(--green)!important}.warn{color:var(--amber)!important}.bad{color:var(--red)!important}
.quick{margin-top:14px;background:var(--card);border-radius:20px;overflow:hidden;border:1px solid var(--line);box-shadow:var(--shadow)}.qrow{min-height:60px;padding:0 16px;display:flex;align-items:center;justify-content:space-between;gap:10px}.qrow+.qrow{border-top:1px solid var(--line)}.ql{display:flex;align-items:center;gap:12px;min-width:0}.ico{width:28px;height:28px;border-radius:50%;background:var(--bg);display:grid;place-items:center;font-size:12px;font-weight:700;flex:0 0 auto}.qt{font-size:14px;font-weight:560}.qs{font-size:10px;color:var(--muted);margin-top:2px;white-space:nowrap}.rightctl{display:flex;align-items:center;gap:9px;min-width:0}.prio{font-size:9.5px;font-weight:680;color:var(--green);white-space:nowrap;text-align:right}.prio b{display:block;font-size:7.5px;color:var(--muted);font-weight:620;margin-bottom:2px;letter-spacing:.05em}.toggle{position:relative;width:43px;height:25px;flex:0 0 auto}.toggle input{display:none}.track{position:absolute;inset:0;border-radius:99px;background:#b8b8b3;transition:.18s}.track:after{content:"";position:absolute;width:19px;height:19px;left:3px;top:3px;border-radius:50%;background:#fff;transition:.18s;box-shadow:0 1px 3px rgba(0,0,0,.25)}.toggle input:checked+.track{background:var(--green)}.toggle input:checked+.track:after{transform:translateX(18px)}
.gatecard{margin-top:14px;background:var(--card);border:1px solid var(--line);border-radius:20px;padding:15px;box-shadow:var(--shadow)}.gatehead{display:flex;align-items:center;justify-content:space-between;gap:12px}.gatetitle{font-size:13px;font-weight:590}.gatepill{border:1px solid var(--green);color:var(--green);border-radius:999px;padding:7px 11px;font-size:11px;font-weight:700;white-space:nowrap}.gatepill.closed{border-color:var(--muted);color:var(--muted)}.gategrid{display:grid;grid-template-columns:repeat(4,1fr);gap:1px;background:var(--line);border:1px solid var(--line);border-radius:14px;overflow:hidden;margin-top:12px}.gateitem{background:var(--card);padding:11px 5px;text-align:center}.gk{font-size:8px;color:var(--muted);font-weight:650;letter-spacing:.04em}.gv{font-size:11px;margin-top:4px;font-weight:650;white-space:nowrap}
.canline{margin-top:14px;display:grid;grid-template-columns:1fr 1fr;gap:8px}.can{background:var(--card);border:1px solid var(--line);border-radius:18px;padding:15px;box-shadow:var(--shadow)}.canTop{display:flex;align-items:center;justify-content:space-between}.canName{font-size:12px;font-weight:590}.badge{font-size:9px;color:var(--green);font-weight:700}.canVal{font-size:22px;margin-top:10px;font-weight:520;letter-spacing:-.04em}.canSub{font-size:9px;color:var(--muted);margin-top:4px}
.drawer{margin-top:14px;background:var(--card);border:1px solid var(--line);border-radius:20px;overflow:hidden;box-shadow:var(--shadow)}details+details{border-top:1px solid var(--line)}summary{list-style:none;cursor:pointer;padding:17px 16px;display:flex;justify-content:space-between;align-items:center;font-size:13px;font-weight:570}summary::-webkit-details-marker{display:none}.arrow{font-size:15px;color:var(--muted);transition:.18s}details[open] .arrow{transform:rotate(90deg)}.body{padding:0 16px 15px}.r{display:flex;justify-content:space-between;gap:16px;padding:10px 0;font-size:11px;border-top:1px solid var(--line)}.rk{color:var(--muted)}.rv{text-align:right;font-variant-numeric:tabular-nums;overflow-wrap:anywhere}.subhead{font-size:9px;color:var(--muted);font-weight:700;letter-spacing:.08em;margin:13px 0 7px}.controlgrid{display:grid;grid-template-columns:1fr 1fr;gap:8px;padding-top:6px}.btn{border:0;border-radius:13px;padding:12px;background:var(--bg);color:var(--ink);font-size:11px;font-weight:600}.btn.primary{background:var(--ink);color:var(--bg)}.btn.danger{color:var(--red)}.fieldgrid{display:grid;grid-template-columns:1fr 1fr;gap:8px}.field{background:var(--bg);border-radius:13px;padding:9px 10px}.field label{display:block;font-size:8px;color:var(--muted);font-weight:650;margin-bottom:5px}.field input{width:100%;border:0;outline:0;background:transparent;color:var(--ink);font-size:13px}.field.full{grid-column:1/-1}.ota{margin-top:8px}.ota input[type=file]{width:100%;font-size:10px;color:var(--muted);margin:6px 0 8px}.progress{height:4px;background:var(--line);border-radius:99px;overflow:hidden;display:none}.progress.show{display:block}.progress>i{display:block;height:100%;width:0;background:var(--green)}.otaMsg{font-size:9px;color:var(--muted);margin-top:7px}.linkbtn{display:flex;align-items:center;justify-content:center;text-decoration:none}
.toast{position:fixed;left:50%;bottom:calc(24px + env(safe-area-inset-bottom));transform:translate(-50%,20px);background:#111;color:#fff;padding:10px 14px;border-radius:999px;font-size:11px;opacity:0;pointer-events:none;transition:.2s;z-index:20}.toast.show{opacity:.94;transform:translate(-50%,0)}.bottom{text-align:center;margin-top:18px;font-size:9px;color:var(--muted)}
@media(max-width:380px){.mode{font-size:40px}.mode.compact{font-size:27px}.qrow{padding:0 13px}.qs{max-width:140px;overflow:hidden;text-overflow:ellipsis}.prio{font-size:8.5px}.gategrid{grid-template-columns:1fr 1fr}.canline{grid-template-columns:1fr}}
</style>
</head>
<body>
<div class="wrap">
  <div class="top"><div class="logo">TESLA UNLOCK</div><div class="live" id="conn">Connecting</div></div>

  <section class="drive">
    <div class="small">AUTOPILOT</div>
    <div class="mode" id="apMode">—</div>
    <div class="metrics">
      <div class="metric"><div class="k">BLINKER</div><div class="v" id="blinkerState">—</div></div>
    </div>
  </section>

  <section class="quick">
    <div class="qrow"><div class="ql"><div class="ico">↔</div><div><div class="qt">Auto Blinker</div></div></div><label class="toggle"><input id="blinkToggle" type="checkbox"><span class="track"></span></label></div>
    <div class="qrow"><div class="ql"><div class="ico">S</div><div><div class="qt">EU Unlock</div><div class="qs">Summon TX Priority</div></div></div><div class="rightctl"><div class="prio"><b>PRIORITY</b><span id="priorityState">—</span></div><label class="toggle"><input id="summonToggle" type="checkbox"><span class="track"></span></label></div></div>
    <div class="qrow"><div class="ql"><div class="ico">T</div><div><div class="qt">TLSSC</div><div class="qs">AP active only</div></div></div><label class="toggle"><input id="tlsscToggle" type="checkbox"><span class="track"></span></label></div>
  </section>

  <section class="gatecard">
    <div class="gatehead"><div class="gatetitle">SUMMON GATE</div><div class="gatepill closed" id="gatePill">CLOSED</div></div>
    <div class="gategrid">
      <div class="gateitem"><div class="gk">PARKED</div><div class="gv" id="gPark">OFF</div></div>
      <div class="gateitem"><div class="gk">SUMMONING</div><div class="gv" id="gSummon">OFF</div></div>
      <div class="gateitem"><div class="gk">ACA</div><div class="gv" id="gAca">INACTIVE</div></div>
      <div class="gateitem"><div class="gk">SPR</div><div class="gv" id="gSpr">NOT SEEN</div></div>
    </div>
  </section>

  <div class="canline">
    <section class="can"><div class="canTop"><div class="canName">CAN A</div><div class="badge" id="canABadge">—</div></div><div class="canVal">Body</div><div class="canSub">MCP2515</div></section>
    <section class="can"><div class="canTop"><div class="canName">CAN B</div><div class="badge" id="canBBadge">—</div></div><div class="canVal">Chassis</div><div class="canSub">TWAI</div></section>
  </div>

  <section class="drawer">
    <details>
      <summary><span>Live details</span><span class="arrow">›</span></summary>
      <div class="body">
        <div class="r"><span class="rk">HandsOn real</span><span class="rv" id="liveHo">—</span></div>
        <div class="r"><span class="rk">Last injected</span><span class="rv" id="liveInj">—</span></div>
                <div class="r"><span class="rk">0x249 RX</span><span class="rv" id="live249">—</span></div>
        <div class="r"><span class="rk">Summon TX OK / FAIL</span><span class="rv" id="liveSumTx">—</span></div>
      </div>
    </details>



    <details>
      <summary><span>Auto Blinker</span><span class="arrow">›</span></summary>
      <div class="body">
        <div class="subhead">AUTO BLINKER MODE</div>
        <div class="controlgrid" style="padding-top:0">
          <button class="btn" id="blinkModeNoa">NOA only · state 5</button>
          <button class="btn" id="blinkModeApNoa">AP & NOA · state 3 + 5</button>
        </div>
        <div class="r"><span class="rk">Selected mode</span><span class="rv" id="blinkModeState">NOA only</span></div>
        <div class="fieldgrid"><div class="field full"><label>DELAY BEFORE TRIGGER (S)</label><input id="blinkDelay" type="number" min="0" max="30" step="0.1" inputmode="decimal"></div></div>
        <button class="btn primary" id="blinkApply" style="width:100%;margin-top:8px">Apply Delay</button>
        <div class="r"><span class="rk">Gate</span><span class="rv" id="blinkGate">—</span></div>
        <div class="r"><span class="rk">Auto armed</span><span class="rv" id="blinkArmed">—</span></div>
        <div class="r"><span class="rk">Countdown</span><span class="rv" id="blinkRemain">—</span></div>
        <div class="r"><span class="rk">Behavior</span><span class="rv" id="blinkBehavior">—</span></div>
        <div class="r"><span class="rk">Active turn</span><span class="rv" id="blinkTurn">—</span></div>
        <div class="r"><span class="rk">0x24A RX</span><span class="rv" id="blink24a">—</span></div>
        <div class="r"><span class="rk">0x249 checksum</span><span class="rv" id="blinkCk">—</span></div>
        <div class="r"><span class="rk">0x249 raw</span><span class="rv" id="blinkRaw">—</span></div>
      </div>
    </details>

    <details>
      <summary><span>Summon transport</span><span class="arrow">›</span></summary>
      <div class="body">
        <div class="r"><span class="rk">Priority state</span><span class="rv" id="sumPriorityDetail">—</span></div>
        <div class="r"><span class="rk">Fresh parked</span><span class="rv" id="sumFreshPark">—</span></div>
        <div class="r"><span class="rk">TX queue now / max</span><span class="rv" id="sumTxQueue">—</span></div>
        <div class="r"><span class="rk">Non-Summon shed</span><span class="rv" id="sumShed">—</span></div>
        <div class="r"><span class="rk">Queue flush</span><span class="rv" id="sumFlush">—</span></div>
        <div class="r"><span class="rk">Full enter / exit</span><span class="rv" id="sumFull">—</span></div>
        <div class="r"><span class="rk">RX 0x118 / 0x186 / 0x399 / 0x3F8</span><span class="rv" id="sumRxIds">—</span></div>
      </div>
    </details>

    <details>
      <summary><span>System</span><span class="arrow">›</span></summary>
      <div class="body">
        <div class="r"><span class="rk">Firmware</span><span class="rv" id="sysFw">—</span></div>
        <div class="r"><span class="rk">Uptime</span><span class="rv" id="sysUptime">—</span></div>
        <div class="r"><span class="rk">Free heap</span><span class="rv" id="sysHeap">—</span></div>
        <div class="r"><span class="rk">Hard Reinitialize</span><span class="rv" id="sysReinit">—</span></div>
        <div class="r"><span class="rk">Last hard reason</span><span class="rv" id="sysReason">—</span></div>
        <div class="controlgrid">
          <button class="btn" id="btnResetStats">Reset Stats</button>
          <button class="btn" id="btnReinit">Hard Reinitialize</button>
          <a class="btn linkbtn" href="/api/system/boot-capture.csv">Boot Capture CSV</a>
          <button class="btn danger" id="btnReboot">Reboot T-2CAN</button>
        </div>
        <div class="subhead">FIRMWARE UPDATE</div>
        <div class="ota"><input id="otaFile" type="file" accept=".bin,application/octet-stream"><button class="btn primary" id="otaUpload" style="width:100%">Upload Firmware</button><div class="progress" id="otaProgress"><i id="otaBar"></i></div><div class="otaMsg" id="otaMsg"></div></div>
      </div>
    </details>
  </section>
  <div class="bottom">T2CAN UNLOCK</div>
</div>
<div class="toast" id="toast"></div>
<script>
const $=id=>document.getElementById(id);let otaUploading=false,lastOk=0,lastBlink=null,lastSum=null,lastSys=null,lastDas=null;
function ok(){lastOk=Date.now();$('conn').textContent='Connected';$('conn').className='live ok'}function toast(t){const x=$('toast');x.textContent=t;x.classList.add('show');clearTimeout(toast.h);toast.h=setTimeout(()=>x.classList.remove('show'),1500)}
setInterval(()=>{if(Date.now()-lastOk>2500){$('conn').textContent='Disconnected';$('conn').className='live'}},700);
function fmtUptime(s){s=Number(s)||0;const h=Math.floor(s/3600),m=Math.floor((s%3600)/60),ss=s%60;return h?`${h}h ${m}m ${ss}s`:m?`${m}m ${ss}s`:`${ss}s`}
function modeInfo(ds,noa){switch(Number(ds)){case 0:case 1:case 2:return['OFF',''];case 3:return['AUTOSTEER','good'];case 4:return['AUTOSTEER · RESTRICTED','warn compact'];case 5:return[noa?'NOA':'NOA · STALE',noa?'good':'warn'];case 6:return['FSD','good'];case 8:return['ABORTING','warn'];case 9:return['ABORTED','warn'];case 14:return['FAULT','bad'];case 15:return['SNA','warn'];default:return['STATE '+ds,'warn']}}
const TURN={0:'IDLE',2:'RIGHT',4:'UP_2',6:'LEFT',7:'LEFT legacy',8:'DOWN_2'};
async function jget(url){const r=await fetch(url,{cache:'no-store'});if(!r.ok)throw Error(r.status);ok();return r.json()}async function post(url){const r=await fetch(url,{method:'POST'});if(!r.ok&&!([202].includes(r.status)))throw Error(r.status);ok();return r}

async function fetchBlink(){try{const s=lastBlink=await jget('/api/blinkA/stats');const [m,c]=modeInfo(s.dasState,s.noaActive);$('apMode').textContent=m;$('apMode').className='mode '+c;$('blinkToggle').checked=!!s.enabled;$('blinkModeState').textContent=s.autoMode===1?'AP & NOA · state 3 + 5':'NOA only · state 5';$('blinkModeNoa').className='btn '+(s.autoMode===0?'primary':'');$('blinkModeApNoa').className='btn '+(s.autoMode===1?'primary':'');$('blinkSub').textContent=(s.autoMode===1?'AP & NOA':'NOA only')+' · '+(Number(s.delayMs)/1000).toFixed(1)+' s';$('blinkerState').textContent=!s.enabled?'OFF':s.noaActive?'READY':'STANDBY';$('blinkerState').className='v '+(s.enabled&&s.noaActive?'good':'');if(document.activeElement!==$('blinkDelay'))$('blinkDelay').value=(Number(s.delayMs)/1000).toFixed(1);$('blinkGate').textContent=s.noaActive?'NOA · FRESH':(s.noaRawActive?'NOA · STALE':'BLOCKED');$('blinkArmed').textContent=s.autoArmed?'ARMED':'IDLE';$('blinkRemain').textContent=s.autoArmed?s.autoRemainMs+' ms':'—';$('blinkBehavior').textContent=s.behaviorType;$('blinkTurn').textContent=TURN[s.activeTurn]??s.activeTurn;$('live249').textContent=s.rx249;$('blinkCk').textContent=!s.seen249?'—':(s.cksumSelfTest?'OK':'MISMATCH');$('blinkRaw').textContent=s.realRaw||'—'}catch(e){}}
async function fetchDas(){try{const s=lastDas=await jget('/api/das/stats');$('blink24a').textContent=s.visualDebugRx}catch(e){}}
async function fetchSum(){try{const s=lastSum=await jget('/api/summon/stats');$('summonToggle').checked=!!s.enabled;$('tlsscToggle').checked=!!s.tlssc;$('priorityState').textContent=s.priorityStateName||s.priorityState;$('sumPriorityDetail').textContent=s.priorityStateName||s.priorityState;$('sumFreshPark').textContent=s.priorityFreshParked?'YES':'NO';$('sumTxQueue').textContent=s.txQueueNow+' / '+s.txQueueMax;$('sumShed').textContent=s.nonSummonShed+' · S '+s.standbyShed+' · F '+s.fullShed;$('sumFlush').textContent=s.summonQueueFlush;$('sumFull').textContent=s.priorityFullEnter+' / '+s.priorityFullExit;$('sumRxIds').textContent=[s.rx280,s.rx390,s.rx921,s.rx1016].join(' / ');$('liveSumTx').textContent=s.txOk+' / '+s.txFail;const gp=$('gatePill');gp.textContent=s.gate?'OPEN':'CLOSED';gp.className='gatepill '+(s.gate?'':'closed');[['gPark',s.parked,'ON','OFF'],['gSummon',s.summon,'ON','OFF'],['gAca',s.aca,'ACTIVE','INACTIVE'],['gSpr',s.spr,'SEEN','NOT SEEN']].forEach(([id,v,a,b])=>{const e=$(id);e.textContent=v?a:b;e.className='gv '+(v?'good':'')});const bState=s.canStateName||['STOPPED','RUNNING','BUS OFF','RECOVERING'][s.canState]||String(s.canState);$('canBBadge').textContent=bState;$('canBBadge').className='badge '+(s.canState===1?'good':s.canState===2?'bad':'warn')}catch(e){}}
async function fetchSys(){try{const s=lastSys=await jget('/api/system/stats');$('sysFw').textContent=s.fwVersion||'—';$('sysUptime').textContent=fmtUptime(s.uptimeS);$('sysHeap').textContent=Math.round((s.freeHeap||0)/1024)+' KB';$('sysReinit').textContent=s.canHardReinit+' / fail '+s.canHardReinitFail;$('sysReason').textContent=s.canLastHardReason;$('canABadge').textContent=s.mcpReady?'READY':'NOT READY';if(!lastSum){$('canBBadge').textContent=s.twaiReady?'RUNNING':'NOT READY'}}catch(e){}}

$('blinkToggle').onchange=async e=>{try{await post(e.target.checked?'/api/blinkA/enable':'/api/blinkA/disable');fetchBlink()}catch(x){}};
$('summonToggle').onchange=async e=>{try{await post(e.target.checked?'/api/summon/enable':'/api/summon/disable');fetchSum()}catch(x){}};
$('tlsscToggle').onchange=async e=>{try{await post(e.target.checked?'/api/summon/tlssc-enable':'/api/summon/tlssc-disable');fetchSum()}catch(x){}};

async function setBlinkMode(mode){
  try{
    await post('/api/blinkA/mode?mode='+mode);
    await fetchBlink();
    toast(mode===1?'Auto Blinker: AP & NOA':'Auto Blinker: NOA only');
  }catch(e){toast('Mode update failed')}
}
$('blinkModeNoa').onclick=()=>setBlinkMode(0);
$('blinkModeApNoa').onclick=()=>setBlinkMode(1);
$('blinkApply').onclick=async()=>{let sec=parseFloat($('blinkDelay').value);if(!Number.isFinite(sec))return toast('Invalid delay');sec=Math.max(0,Math.min(30,sec));$('blinkDelay').value=sec.toFixed(1);const v=Math.round(sec*1000);try{const r=await post('/api/blinkA/delay?ms='+v);if(r.ok){await fetchBlink();toast('Delay '+(v/1000).toFixed(1)+' s')}}catch(e){toast('Delay update failed')}};
$('btnResetStats').onclick=async()=>{if(!confirm('Reset runtime diagnostic counters?'))return;try{await post('/api/system/reset-stats');toast('Stats reset')}catch(e){}};$('btnReinit').onclick=async()=>{if(!confirm('Hard reinitialize CAN A and CAN B?'))return;try{await post('/api/system/reinit-can');toast('Reinitialize requested')}catch(e){}};$('btnReboot').onclick=async()=>{if(!confirm('Reboot T-2CAN now?'))return;try{await post('/api/system/reboot')}catch(e){}toast('Rebooting…');setTimeout(()=>location.reload(),5000)};
$('otaUpload').onclick=()=>{const f=$('otaFile').files[0],msg=$('otaMsg'),pr=$('otaProgress'),bar=$('otaBar');if(!f)return toast('Choose .bin file');const fd=new FormData();fd.append('update',f,f.name);const x=new XMLHttpRequest();otaUploading=true;pr.classList.add('show');msg.textContent='Uploading…';x.open('POST','/update',true);x.upload.onprogress=e=>{if(e.lengthComputable){const p=Math.round(e.loaded/e.total*100);bar.style.width=p+'%';msg.textContent='Uploading '+p+'%'}};x.onload=()=>{let good=x.status===200;try{good=good&&JSON.parse(x.responseText).ok}catch(e){}if(good){bar.style.width='100%';msg.textContent='Flash successful · rebooting';setTimeout(()=>location.reload(),6000)}else{msg.textContent='OTA failed';otaUploading=false}};x.onerror=()=>{msg.textContent='Upload error';otaUploading=false};x.send(fd)};

fetchBlink();fetchDas();fetchSum();fetchSys();setInterval(()=>{if(!otaUploading){fetchBlink()}},500);setInterval(()=>{if(!otaUploading){fetchDas();fetchSum()}},800);setInterval(()=>{if(!otaUploading)fetchSys()},3000);
</script>
</body></html>
)HTML";
