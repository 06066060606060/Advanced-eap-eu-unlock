const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>T2CAN Unified</title>
<style>
  :root {
    --bg: #0d0d0d;
    --panel: #161618;
    --card: #1c1c1e;
    --line: #2c2c2e;
    --txt: #f5f5f7;
    --muted: #8e8e93;
    --accent: #0a84ff;
    --ok: #30d158;
    --warn: #ff9f0a;
    --bad: #ff453a;
  }
  * { box-sizing: border-box; -webkit-tap-highlight-color: transparent; }
  body {
    margin: 0;
    background: var(--bg);
    color: var(--txt);
    font: 15px/1.45 -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
    -webkit-font-smoothing: antialiased;
  }
  header {
    padding: 18px 20px;
    display: flex;
    justify-content: space-between;
    align-items: center;
    flex-wrap: wrap;
    gap: 10px;
  }
  header h1 {
    margin: 0;
    font-size: 18px;
    font-weight: 700;
    color: var(--txt);
    display: flex;
    align-items: center;
    gap: 8px;
  }
  header h1::before {
    content: "⚡";
    font-size: 14px;
    opacity: 0.7;
  }
  header .pill {
    font-size: 12px;
    padding: 5px 12px;
    background: var(--card);
    border: 1px solid var(--line);
    border-radius: 20px;
    color: var(--muted);
    display: flex;
    align-items: center;
    gap: 6px;
    font-weight: 500;
  }
  header .pill::before {
    content: "";
    width: 8px;
    height: 8px;
    border-radius: 50%;
    background: var(--warn);
    display: inline-block;
  }
  header .pill.ok::before { background: var(--ok); }
  header .pill.bad::before { background: var(--bad); }
  .tabs {
    display: flex;
    gap: 8px;
    padding: 0 20px 14px;
    max-width: 460px;
    margin: 0 auto;
    flex-wrap: wrap;
  }
  .tab-btn {
    background: var(--card);
    border: 1px solid var(--line);
    color: var(--muted);
    padding: 9px 16px;
    border-radius: 12px;
    cursor: pointer;
    font: inherit;
    font-size: 13px;
    font-weight: 600;
    transition: all 0.15s ease;
  }
  .tab-btn:hover { filter: brightness(1.2); }
  .tab-btn.active { background: var(--txt); color: var(--bg); border-color: transparent; }
  main {
    max-width: 460px;
    margin: 0 auto;
    padding: 0 16px 24px;
    display: none;
    gap: 16px;
  }
  main.active { display: grid; }
  .panel {
    background: var(--panel);
    border: 1px solid var(--line);
    border-radius: 20px;
    padding: 18px;
  }
  .panel h2 {
    margin: 0 0 14px;
    font-size: 14px;
    font-weight: 700;
    color: var(--txt);
  }
  .panel h2 .iface-note {
    font-size: 10px;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: .06em;
    color: var(--muted);
    margin-left: 8px;
  }
  .row {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 10px;
  }
  .row.row3 { grid-template-columns: 1fr 1fr 1fr; }
  .stat {
    background: var(--card);
    border: 1px solid var(--line);
    border-radius: 14px;
    padding: 14px 12px;
    min-height: 80px;
    display: flex;
    flex-direction: column;
    justify-content: center;
  }
  .stat .k {
    font-size: 10px;
    text-transform: uppercase;
    letter-spacing: 0.08em;
    color: var(--muted);
    font-weight: 600;
    line-height: 1.3;
  }
  .stat .v {
    font-size: 20px;
    font-weight: 700;
    margin-top: 6px;
    color: var(--txt);
    letter-spacing: -0.02em;
    overflow-wrap: anywhere;
  }
  .stat.full { grid-column: 1 / -1; }
  .big-state {
    background: var(--card);
    border: 1px solid var(--line);
    border-radius: 14px;
    padding: 18px 14px;
    font-size: 28px;
    font-weight: 700;
    letter-spacing: -0.02em;
    text-align: center;
  }
  .big-state.off  { color: var(--txt); }
  .big-state.on   { color: var(--ok); }
  .big-state.warn { color: var(--warn); }
  .big-state.bad  { color: var(--bad); }
  .tbar {
    display: flex;
    gap: 10px;
    margin-top: 14px;
    flex-wrap: wrap;
  }
  button {
    font: inherit;
    cursor: pointer;
    background: var(--card);
    color: var(--txt);
    border: 1px solid var(--line);
    border-radius: 12px;
    padding: 10px 20px;
    font-size: 13px;
    font-weight: 600;
    transition: all 0.15s ease;
  }
  button:hover { filter: brightness(1.2); }
  button:active { transform: scale(0.97); }
  button[disabled] { opacity: .5; cursor: not-allowed; }
  button.primary {
    background: var(--txt);
    color: var(--bg);
    border-color: transparent;
  }
  button.danger {
    background: var(--card);
    color: var(--bad);
    border-color: #3a1f23;
  }
  button.warn {
    background: var(--warn);
    color: #000;
    border-color: transparent;
  }
  .desc {
    font-size: 12px;
    color: var(--muted);
    line-height: 1.6;
    margin-top: 8px;
  }
  .desc b { color: var(--txt); font-weight: 600; }
  .ok { color: var(--ok); }
  .warn { color: var(--warn); }
  .bad { color: var(--bad); }
  .footer {
    color: var(--muted);
    font-size: 11px;
    text-align: center;
    padding: 14px 0;
    line-height: 1.6;
  }
  .footer a { color: var(--muted); text-decoration: none; }
  label {
    display: flex;
    flex-direction: column;
    gap: 5px;
    color: var(--muted);
    font-size: 11px;
    text-transform: uppercase;
    letter-spacing: 0.05em;
    font-weight: 600;
  }
  input[type=text], input[type=number] {
    background: var(--card);
    border: 1px solid var(--line);
    color: var(--txt);
    border-radius: 10px;
    padding: 9px 10px;
    font: 13px/1.4 -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
    width: 100%;
  }
  input[type=file] {
    width: 100%;
    font-size: 12px;
    color: var(--muted);
    background: var(--card);
    border: 1px solid var(--line);
    border-radius: 12px;
    padding: 10px 12px;
  }
  table { width: 100%; border-collapse: collapse; margin-top: 8px; }
  th, td { text-align: left; padding: 7px 6px; border-bottom: 1px solid var(--line); font-size: 12px; }
  th { color: var(--muted); font-weight: 600; text-transform: uppercase; letter-spacing: .05em; font-size: 10px; }
  td input { width: 90%; }
  details { margin-top: 14px; }
  summary { cursor: pointer; color: var(--muted); font-size: 12px; font-weight: 600; }
  .progress {
    width: 100%;
    height: 8px;
    background: var(--card);
    border: 1px solid var(--line);
    border-radius: 6px;
    overflow: hidden;
    margin-top: 12px;
    display: none;
  }
  .progress.show { display: block; }
  .progress-bar {
    height: 100%;
    width: 0%;
    background: var(--accent);
    transition: width 0.15s ease;
  }
  .ota-msg {
    font-size: 12px;
    margin-top: 10px;
    color: var(--muted);
  }
  .ota-msg.ok  { color: var(--ok); }
  .ota-msg.bad { color: var(--bad); }
  .gate-status {
    text-align: center;
    font-size: 13px;
    font-weight: 700;
    padding: 10px;
    border-radius: 12px;
    border: 1px solid var(--line);
    background: var(--card);
  }
  .gate-status.open   { color: var(--ok);  border-color: var(--ok); }
  .gate-status.closed { color: var(--bad); border-color: #3a1f23; }
  .gbox.active .v { color: var(--ok); }
  .toast {
    position: fixed;
    bottom: 18px;
    left: 50%;
    transform: translateX(-50%);
    background: var(--txt);
    color: var(--bg);
    padding: 8px 16px;
    border-radius: 14px;
    font-size: 12px;
    font-weight: 700;
    opacity: 0;
    transition: opacity .25s;
    z-index: 10;
  }
  .toast.show { opacity: 1; }
  .toggle-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    margin-top: 4px;
  }
  .toggle-row .lbl { font-size: 14px; font-weight: 600; color: var(--txt); text-transform: none; letter-spacing: 0; }
  .switch { position: relative; width: 50px; height: 30px; flex-shrink: 0; }
  .switch input { opacity: 0; width: 0; height: 0; }
  .slider {
    position: absolute;
    inset: 0;
    cursor: pointer;
    background: var(--card);
    border: 1px solid var(--line);
    border-radius: 30px;
    transition: 0.2s;
  }
  .slider::before {
    content: "";
    position: absolute;
    height: 22px;
    width: 22px;
    left: 3px;
    top: 3px;
    background: var(--txt);
    border-radius: 50%;
    transition: 0.2s;
  }
  .switch input:checked + .slider { background: var(--ok); border-color: transparent; }
  .switch input:checked + .slider::before { transform: translateX(20px); background: #000; }
</style>
</head>
<body>
<header>
  <h1>T2CAN Advanced EAP & EU unlock</h1>
  <span class="pill" id="conn">connecting…</span>
</header>
<div class="tabs">
  <button class="tab-btn active" onclick="showTab('eap',this)">Advanced EAP</button>
  <button class="tab-btn" onclick="showTab('summon',this)">Summon Unlock</button>
  <button class="tab-btn" onclick="showTab('fw',this)">Firmware</button>
</div>

<!-- ADVANCED EAP TAB -->
<main id="main-eap" class="active">

  <section class="panel">
    <h2>Auto Blinker <span class="iface-note">TX 0x249 — CAN A</span></h2>
    <div class="toggle-row">
      <span class="lbl">Enable auto blinker</span>
      <label class="switch" style="margin:0">
        <input type="checkbox" id="blinkAToggle">
        <span class="slider"></span>
      </label>
    </div>
    <div class="desc">
      Triggered when <b>DAS_behaviorType</b> (0x24A · <b>DAS_visualDebug</b>) becomes <b>LANE_CHANGE_LEFT (2)</b> or <b>LANE_CHANGE_RIGHT (3)</b>, while <b>Autopilot</b> is active or <b>Force Mode</b> is enabled. The trigger is delayed by <b>N ms</b> after detection; the delay is configurable below.<br>
      Sends <b>SCCM_turnIndicatorStalkStatus</b> (0x249) as a <b>single pulse</b> (~350 ms) through <b>CAN A (MCP2515)</b>. One lane-change event produces one pulse and the native three-blink behavior.<br>
      <b>Soft</b> behavior: left → DOWN_1, right → UP_1.<br>
      Checksum and rolling counter are calculated from the reverse-engineered SCCM model. UP values are log-verified; DOWN values are extrapolated. Test while stationary / in Park first.
    </div>
    <div class="row" style="margin-top:12px;align-items:flex-end;">
      <div style="flex:1">
        <div class="k" style="margin-bottom:6px">Delay before trigger (ms) · 0–30000</div>
        <input type="number" id="blkA_delay_in" min="0" max="30000" step="100" value="3000" style="width:100%;padding:10px 12px;border-radius:8px;border:1px solid #2a3550;background:#0e1524;color:#e8eefc;font-size:15px;">
      </div>
      <button class="primary" id="blkA_delay_apply" style="width:120px">Apply</button>
    </div>
    <div class="row" style="margin-top:12px">
      <div class="stat"><div class="k">Auto armed</div><div class="v" id="blkA_armed">—</div></div>
      <div class="stat"><div class="k">Countdown</div><div class="v" id="blkA_remain">—</div></div>
      <div class="stat full"><div class="k">Active delay</div><div class="v" id="blkA_delay_cur">—</div></div>
    </div>
    <div class="row" style="margin-top:12px">
      <div class="stat"><div class="k">Autopilot active</div><div class="v" id="blkA_ap">—</div></div>
      <div class="stat"><div class="k">Force Mode</div><div class="v" id="blkA_fm">—</div></div>
      <div class="stat"><div class="k">Active turn</div><div class="v" id="blkA_turn">—</div></div>
      <div class="stat"><div class="k">Tx ok / fail</div><div class="v" id="blkA_tx">0/0</div></div>
      <div class="stat"><div class="k">CAN A state</div><div class="v" id="blkA_cs">—</div></div>
      <div class="stat full"><div class="k">Uptime</div><div class="v" id="blkA_up">—</div></div>
    </div>
  </section>

  <section class="panel">
    <h2>Diagnostics 0x249 <span class="iface-note">RX SCCM_leftStalk — CAN A</span></h2>
    <div class="row" style="margin-top:0">
      <div class="stat"><div class="k">Seen on bus</div><div class="v" id="blkA_seen">—</div></div>
      <div class="stat"><div class="k">Rx count 0x249</div><div class="v" id="blkA_rx249">—</div></div>
      <div class="stat"><div class="k">Checksum self-test</div><div class="v" id="blkA_st">—</div></div>
      <div class="stat"><div class="k">Real SCCM counter</div><div class="v" id="blkA_rcnt">—</div></div>
      <div class="stat"><div class="k">Real SCCM turn</div><div class="v" id="blkA_rturn">—</div></div>
      <div class="stat"><div class="k">Real checksum (byte 0)</div><div class="v" id="blkA_rck">—</div></div>
    </div>
    <div class="desc">
      Reads the real SCCM frame on CAN A to align our counter before injection. The checksum self-test compares our formula with the real byte 0 and should remain OK. If the RX count stays at 0, the MCP2515 is not receiving frames: check CAN A wiring and termination.
    </div>
  </section>
</main>

<!-- SUMMON UNLOCK TAB -->
<main id="main-summon">
  <section class="panel">
    <h2>Summon Unlock <span class="iface-note">CAN B — TWAI</span></h2>
    <div class="stat full" style="min-height:auto;padding:16px 14px;">
      <div class="k">State</div>
      <div class="big-state off" id="sum_big">—</div>
    </div>
    <div class="tbar">
      <button class="primary" onclick="postSummon('/api/summon/enable')">Enable</button>
      <button class="danger" onclick="postSummon('/api/summon/disable')">Disable</button>
      <button class="warn" id="btnForceMode">AP injection</button>
    </div>
  </section>

  <section class="panel">
    <h2>Traffic Light &amp; Stop Sign Control <span class="iface-note">CAN B — TWAI</span></h2>
    <div class="toggle-row">
      <span class="lbl">Enable TLSSC</span>
      <label class="switch" style="margin:0">
        <input type="checkbox" id="tlsscToggle">
        <span class="slider"></span>
      </label>
    </div>
    <div class="desc">
      Injects <b>UI_fsdStopsControlEnabled = 1</b> on <b>0x3FD</b> mux0 bit38.<br>
	  and <b>UI_fsdContinueOnGreenWithCIPV = 1</b> on <b>0x3FD</b> mux0 bit39.<br>
      Off by default. Applied only while the injection gate is open.
    </div>
  </section>

  <section class="panel">
    <h2>Injection Gate</h2>
    <div class="gate-status" id="sum_gate_status">—</div>
    <div class="row" style="margin-top:12px">
      <div class="stat" id="sum_g_pk"><div class="k">Parked</div><div class="v" id="sum_g_pk_v">—</div></div>
      <div class="stat" id="sum_g_su"><div class="k">Summoning</div><div class="v" id="sum_g_su_v">—</div></div>
      <div class="stat"><div class="k">ACA</div><div class="v" id="sum_d_aca">—</div></div>
      <div class="stat"><div class="k">SPR</div><div class="v" id="sum_d_spr">—</div></div>
    </div>
    <div class="desc">
      Gate open if Parked OR Summoning only.<br>
      APActive : <span id="sum_g_ap_v" style="font-weight:600">—</span> (info only, doesn't start injection).
    </div>
  </section>

  <section class="panel">
    <h2>Frames CAN</h2>
    <div class="row">
      <div class="stat"><div class="k">280 (gear/ACA)</div><div class="v" id="sum_s_280">—</div></div>
      <div class="stat"><div class="k">390 (DIF gear)</div><div class="v" id="sum_s_390">—</div></div>
      <div class="stat"><div class="k">921 (AP status)</div><div class="v" id="sum_s_921">—</div></div>
      <div class="stat"><div class="k">1016 (SPR)</div><div class="v" id="sum_s_1016">—</div></div>
      <div class="stat"><div class="k">1021 mux1 rx</div><div class="v" id="sum_s_rx">—</div></div>
      <div class="stat"><div class="k">TX ok</div><div class="v ok" id="sum_s_ok">—</div></div>
      <div class="stat"><div class="k">TX fail</div><div class="v" id="sum_s_fail">—</div></div>
      <div class="stat"><div class="k">CAN B state</div><div class="v" id="sum_s_can">—</div></div>
      <div class="stat full"><div class="k">Uptime</div><div class="v" id="sum_s_up">—</div></div>
    </div>
  </section>
</main>

<!-- FIRMWARE / OTA TAB -->
<main id="main-fw">
  <section class="panel">
    <h2>Firmware / OTA Update</h2>
    <div class="row" style="margin-bottom:12px;">
      <div class="stat"><div class="k">Version</div><div class="v" id="fw_ver">—</div></div>
      <div class="stat"><div class="k">Free heap</div><div class="v" id="fw_free">—</div></div>
      <div class="stat"><div class="k">MCP2515 (CAN A)</div><div class="v" id="fw_mcp">—</div></div>
      <div class="stat"><div class="k">TWAI (CAN B)</div><div class="v" id="fw_twai">—</div></div>
      <div class="stat full"><div class="k">Boot count</div><div class="v" id="fw_boot">—</div></div>
    </div>
    <input type="file" id="otaFile" accept=".bin">
    <div class="tbar">
      <button class="primary" id="btnOtaUpload" onclick="uploadOta()">Upload &amp; Flash</button>
    </div>
    <div class="progress" id="otaProgressWrap">
      <div class="progress-bar" id="otaProgressBar"></div>
    </div>
    <div class="ota-msg" id="otaMsg">Select a compiled .bin firmware file, then upload. The device reboots automatically after a successful flash.</div>
  </section>
</main>

<div class="footer">
  T2CAN Unified ·
  <a href="/api/blinkA/stats" target="_blank">/api/blinkA/stats (EAP)</a> ·
  <a href="/api/summon/stats" target="_blank">/api/summon/stats</a> ·
  <a href="/api/system/stats" target="_blank">/api/system/stats</a><br>
  research / educational only · not for use on public roads
</div>
<div class="toast" id="toast">saved</div>

<script>
const $ = id => document.getElementById(id);
let otaUploading = false;

function showTab(tab, btn) {
  document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
  document.querySelectorAll('main').forEach(m => m.classList.remove('active'));
  btn.classList.add('active');
  $('main-' + tab).classList.add('active');
}

function showToast(msg) {
  const t = $('toast');
  t.textContent = msg;
  t.classList.add('show');
  clearTimeout(showToast._h);
  showToast._h = setTimeout(() => t.classList.remove('show'), 1500);
}

// ===== ADVANCED EAP JS (Auto Blinker + Manual) =====
const BLINKA_CAN_STATES = ['running','recovering','bus-off','stopped'];
const BLINKA_REQ = ['NONE','LEFT','RIGHT'];
const TURN_NAMES = {0:'IDLE',2:'UP_1',4:'UP_2',7:'DOWN_1',8:'DOWN_2',5:'SNA'};

async function fetchBlinkAStats() {
  try {
    const s = await fetch('/api/blinkA/stats').then(r => r.json());
    $('blkA_ap').textContent  = s.apActive ? 'YES' : 'no';
    $('blkA_fm').textContent  = s.forceMode ? 'YES' : 'no';

    if (s.activeTurn !== undefined)
      $('blkA_turn').textContent = (TURN_NAMES[s.activeTurn] ?? String(s.activeTurn)) + ' (' + s.activeTurn + ')';

    $('blkA_tx').textContent = s.txOk + '/' + s.txFail;

    const cs = BLINKA_CAN_STATES[s.canAState] ?? String(s.canAState);
    $('blkA_cs').textContent = cs;
    $('blkA_cs').className   = 'v ' + (s.canAState === 0 ? 'ok' : s.canAState === 2 ? 'bad' : 'warn');

    const u = s.uptimeS;
    $('blkA_up').textContent = u < 60 ? u + 's' : Math.floor(u/60) + 'm' + (u%60) + 's';

    const tg = $('blinkAToggle');
    if (tg && document.activeElement !== tg) tg.checked = !!s.enabled;

    // Delayed trigger: delay, armed state, and countdown
    if (s.delayMs !== undefined) {
      $('blkA_delay_cur').textContent = s.delayMs + ' ms';
      const din = $('blkA_delay_in');
      if (din && document.activeElement !== din) din.value = s.delayMs;
    }
    if (s.autoArmed !== undefined) {
      const ar = $('blkA_armed');
      ar.textContent = s.autoArmed ? ('ARMED ' + (BLINKA_REQ[s.autoPending] ?? s.autoPending)) : 'idle';
      ar.className   = 'v ' + (s.autoArmed ? 'warn' : '');
      $('blkA_remain').textContent = s.autoArmed ? ((s.autoRemainMs ?? 0) + ' ms') : '—';
    }

    // Diagnostics 0x249 (CAN A): confirms reception and checksum calculation
    if (s.rx249 !== undefined) {
      const seen = $('blkA_seen');
      seen.textContent = s.seen249 ? 'YES' : 'no';
      seen.className   = 'v ' + (s.seen249 ? 'ok' : 'bad');
      $('blkA_rx249').textContent = s.rx249;
      const st = $('blkA_st');
      if (!s.seen249) { st.textContent = '—'; st.className = 'v'; }
      else { st.textContent = s.cksumSelfTest ? 'OK' : 'MISMATCH'; st.className = 'v ' + (s.cksumSelfTest ? 'ok' : 'bad'); }
      $('blkA_rcnt').textContent  = s.realCounter;
      $('blkA_rturn').textContent = (TURN_NAMES[s.realTurn] ?? String(s.realTurn)) + ' (' + s.realTurn + ')';
      $('blkA_rck').textContent   = '0x' + Number(s.realCksum).toString(16).toUpperCase().padStart(2,'0');
    }
  } catch {}
}

async function postBlinkA(url) {
  await fetch(url, { method: 'POST' });
  fetchBlinkAStats();
}

$('blinkAToggle').addEventListener('change', (e) => {
  postBlinkA(e.target.checked ? '/api/blinkA/enable' : '/api/blinkA/disable');
  showToast(e.target.checked ? 'Auto blinker enabled' : 'Auto blinker disabled');
});


function applyBlinkADelay() {
  let v = parseInt($('blkA_delay_in').value, 10);
  if (isNaN(v)) v = 3000;
  v = Math.max(0, Math.min(30000, v));
  $('blkA_delay_in').value = v;
  postBlinkA('/api/blinkA/delay?ms=' + v);
  showToast('Auto delay = ' + v + ' ms');
}
$('blkA_delay_apply').addEventListener('click', applyBlinkADelay);
$('blkA_delay_in').addEventListener('keydown', (e) => { if (e.key === 'Enter') applyBlinkADelay(); });

// ===== SUMMON UNLOCK JS =====
const SUMMON_CAN_STATES = ['running','recovering','bus-off','stopped'];

async function fetchSummonStats() {
  try {
    const s = await fetch('/api/summon/stats').then(r => r.json());
    const big = $('sum_big');
    if (s.forceMode) {
      big.textContent = 'FORCE';
      big.className   = 'big-state warn';
    } else {
      big.textContent = s.enabled ? 'ON' : 'OFF';
      big.className   = 'big-state ' + (s.enabled ? 'on' : 'off');
    }

    const btnForceMode = $('btnForceMode');
    if (btnForceMode) {
      btnForceMode.textContent = s.forceMode ? 'AP Injection: ON' : 'AP Injection: OFF';
      btnForceMode.style.opacity = s.forceMode ? '1' : '0.7';
    }

    const gate = s.gate;
    const gs = $('sum_gate_status');
    gs.textContent = gate ? 'OPEN — injection allowed' : 'CLOSED — injection blocked';
    gs.className   = 'gate-status ' + (gate ? 'open' : 'closed');

    $('sum_g_ap_v').textContent = s.ap ? 'ON' : 'OFF';
    $('sum_g_ap_v').style.color = s.ap ? 'var(--ok)' : 'var(--muted)';
    $('sum_g_pk').classList.toggle('active', !!s.parked);
    $('sum_g_pk_v').textContent = s.parked ? 'ON' : 'OFF';
    $('sum_g_su').classList.toggle('active', !!s.summon);
    $('sum_g_su_v').textContent = s.summon ? 'ON' : 'OFF';

    $('sum_d_aca').textContent = s.aca ? 'ACTIVE' : 'inactive';
    $('sum_d_aca').className = 'v ' + (s.aca ? 'ok' : '');
    $('sum_d_spr').textContent = s.spr ? 'SEEN' : 'not seen';
    $('sum_d_spr').className = 'v ' + (s.spr ? 'ok' : '');

    $('sum_s_280').textContent  = s.rx280;
    $('sum_s_390').textContent  = s.rx390;
    $('sum_s_921').textContent  = s.rx921;
    $('sum_s_1016').textContent = s.rx1016;
    $('sum_s_rx').textContent   = s.rxMux1;
    $('sum_s_ok').textContent   = s.txOk;
    $('sum_s_fail').textContent = s.txFail;
    $('sum_s_fail').className   = 'v ' + (s.txFail > 0 ? 'warn' : '');

    const cs = SUMMON_CAN_STATES[s.canState] ?? String(s.canState);
    $('sum_s_can').textContent = cs;
    $('sum_s_can').className   = 'v ' + (s.canState === 0 ? 'ok' : s.canState === 2 ? 'bad' : 'warn');

    const u = s.uptimeS;
    $('sum_s_up').textContent = u < 60 ? u + 's' : Math.floor(u/60) + 'm' + (u%60) + 's';

    const tg = $('tlsscToggle');
    if (tg && document.activeElement !== tg) tg.checked = !!s.tlssc;

    $('conn').textContent = 'connected';
    $('conn').className   = 'pill ok';
  } catch {
    $('conn').textContent = 'lost';
    $('conn').className   = 'pill bad';
  }
}

async function postSummon(url) {
  await fetch(url, { method: 'POST' });
  fetchSummonStats();
}

$('tlsscToggle').addEventListener('change', (e) => {
  postSummon(e.target.checked ? '/api/summon/tlssc-enable' : '/api/summon/tlssc-disable');
  showToast(e.target.checked ? 'TLSSC enabled' : 'TLSSC disabled');
});

// ===== FIRMWARE / SYSTEM JS =====
async function fetchSystemStats() {
  try {
    const s = await fetch('/api/system/stats').then(r => r.json());
    if (s.fwVersion) {
      $('fw_ver').textContent = s.fwVersion;
      $('hdr_ver').textContent = s.fwVersion;
    }
    if (s.freeHeap !== undefined) $('fw_free').textContent = Math.round(s.freeHeap/1024) + ' KB';
    $('fw_mcp').textContent  = s.mcpReady  ? 'ready' : 'not ready';
    $('fw_mcp').className    = 'v ' + (s.mcpReady  ? 'ok' : 'bad');
    $('fw_twai').textContent = s.twaiReady ? 'ready' : 'not ready';
    $('fw_twai').className   = 'v ' + (s.twaiReady ? 'ok' : 'bad');
    if (s.rtcBootCount !== undefined) $('fw_boot').textContent = s.rtcBootCount;
  } catch(e) {
    // keep last known values
  }
}

// ── OTA upload ───────────────────────────────────────────────
function uploadOta() {
  const input = $('otaFile');
  const file = input.files[0];
  const msg = $('otaMsg');
  const wrap = $('otaProgressWrap');
  const bar = $('otaProgressBar');
  const btn = $('btnOtaUpload');

  if (!file) {
    msg.textContent = 'Please choose a .bin file first.';
    msg.className = 'ota-msg bad';
    return;
  }

  const form = new FormData();
  form.append('update', file, file.name);

  otaUploading = true;
  btn.disabled = true;
  input.disabled = true;
  wrap.className = 'progress show';
  bar.style.width = '0%';
  msg.textContent = 'Uploading ' + file.name + '…';
  msg.className = 'ota-msg';

  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/update', true);

  xhr.upload.onprogress = (e) => {
    if (e.lengthComputable) {
      const pct = Math.round((e.loaded / e.total) * 100);
      bar.style.width = pct + '%';
      msg.textContent = 'Uploading… ' + pct + '%';
    }
  };

  xhr.onload = () => {
    let ok = xhr.status === 200;
    let errText = '';
    try {
      const r = JSON.parse(xhr.responseText);
      ok = ok && r.ok;
      errText = r.error || '';
    } catch {}

    if (ok) {
      bar.style.width = '100%';
      msg.textContent = 'Flash successful — rebooting…';
      msg.className = 'ota-msg ok';
      setTimeout(() => location.reload(), 6000);
    } else {
      msg.textContent = 'OTA failed' + (errText ? ': ' + errText : '');
      msg.className = 'ota-msg bad';
      btn.disabled = false;
      input.disabled = false;
      otaUploading = false;
    }
  };

  xhr.onerror = () => {
    msg.textContent = 'Upload error — device likely rebooted or connection lost.';
    msg.className = 'ota-msg bad';
    btn.disabled = false;
    input.disabled = false;
    otaUploading = false;
  };

  xhr.send(form);
}

// ===== STARTUP =====
fetchBlinkAStats();
setInterval(() => { if (!otaUploading) fetchBlinkAStats(); }, 500);
fetchSummonStats();
setInterval(() => { if (!otaUploading) fetchSummonStats(); }, 800);
fetchSystemStats();
setInterval(() => { if (!otaUploading) fetchSystemStats(); }, 3000);
</script>
</body>
</html>
)HTML";
