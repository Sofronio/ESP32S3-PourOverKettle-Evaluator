#pragma once

// ============================================================
// Embedded Web UI — 内嵌 Web UI (零外部依赖, AP 模式无外网也能用)
// Bilingual: 中文 / English toggle, persisted in localStorage.
// Layout: left 40% (temp + controls) / right 60% (chart), viewport adaptive.
// ============================================================
static const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32-S3 Pour-Over Kettle Evaluator</title>
<style>
  :root { --bg:#0d1117; --card:#161b22; --line:#30363d; --txt:#e6edf3; --mut:#8b949e; --acc:#4fc3f7; }
  [hidden]{ display:none !important; }   /* 防止被下方 display:flex 类覆盖 / avoid display:flex overriding hidden */
  * { box-sizing:border-box; }
  html,body { height:100%; }
  body { margin:0; font-family:-apple-system,"PingFang SC","Microsoft YaHei",sans-serif; background:var(--bg); color:var(--txt);
         display:flex; flex-direction:column; overflow:hidden; }
  header { flex:0 0 auto; padding:12px 20px; background:#010409; border-bottom:1px solid var(--line);
           display:flex; align-items:center; gap:12px; flex-wrap:wrap; }
  header h1 { font-size:17px; margin:0; }
  #langBtn { margin-left:auto; }
  .badge { font-size:12px; padding:3px 10px; border-radius:20px; background:#21262d; color:var(--mut); border:1px solid var(--line); }
  .badge.green { color:#69f0ae; border-color:#2ea043; }
  .badge.blue  { color:#40c4ff; border-color:#1f6feb; }
  .badge.yellow{ color:#ffd740; border-color:#d29922; }
  .badge.red   { color:#ff5252; border-color:#f85149; }
  .badge.white { color:#e6edf3; border-color:#8b949e; }
  main { flex:1 1 auto; min-height:0; max-width:1200px; width:100%; margin:0 auto; padding:14px;
         display:grid; grid-template-columns:4fr 6fr; gap:14px; }
  .col-left { min-height:0; display:flex; flex-direction:column; gap:14px; overflow-y:auto; }
  .col-right { min-height:0; display:flex; flex-direction:column; }
  .card { background:var(--card); border:1px solid var(--line); border-radius:10px; padding:14px; }
  .card h3 { margin:0 0 10px; font-size:14px; color:var(--mut); font-weight:600; }
  .temprow { display:flex; align-items:baseline; gap:10px; }
  #tempNow { font-size:52px; font-weight:700; font-variant-numeric:tabular-nums; line-height:1.1; }
  .unit { font-size:20px; color:var(--mut); }
  #tempStatus { color:var(--mut); font-size:13px; margin:2px 0 8px; }
  .btns { display:flex; gap:10px; flex-wrap:wrap; margin:10px 0; align-items:center; }
  button { background:#21262d; color:var(--txt); border:1px solid var(--line); border-radius:8px; padding:7px 14px; font-size:14px; cursor:pointer; }
  button:hover { border-color:#8b949e; }
  button.primary { background:#1f6feb; border-color:#1f6feb; color:#fff; }
  button.danger { background:#da3633; border-color:#da3633; color:#fff; }
  button.rec-on { background:#f85149; border-color:#f85149; color:#fff; animation:pulse 1.2s infinite; }
  @keyframes pulse { 50% { opacity:.65; } }
  .chart-card { flex:1 1 auto; min-height:0; display:flex; flex-direction:column; overflow-y:auto; }
  .chart-tabs { flex:0 0 auto; display:flex; gap:6px; padding-bottom:8px; }
  .chart-tabs button { padding:4px 14px; font-size:13px; }
  .ana-row { flex:0 0 auto; display:flex; gap:8px; align-items:center; padding-bottom:8px; flex-wrap:wrap; }
  .ana-row select { flex:1 1 160px; min-width:140px; }
  .ana-err { color:#ff5252; font-size:13px; padding:10px 4px; }
  .ana-scorecard { display:flex; align-items:center; gap:14px; padding:10px 4px 2px; }
  .ana-num { font-size:46px; font-weight:700; line-height:1; }
  .ana-g { font-size:16px; font-weight:600; margin-bottom:4px; }
  .ana-meta { color:#8b949e; font-size:12px; }
  .ana-table { width:100%; border-collapse:collapse; margin-top:8px; }
  .ana-table td { padding:6px 8px; border-bottom:1px solid #21262d; font-size:13px; }
  .ana-table td:last-child { text-align:right; white-space:nowrap; }
  .chart-tools { flex:0 0 auto; display:flex; gap:8px; flex-wrap:wrap; align-items:center; padding-bottom:10px; }
  select, input { background:#0d1117; color:var(--txt); border:1px solid var(--line); border-radius:6px; padding:7px 9px; font-size:13px; }
  select:focus, input:focus { outline:none; border-color:var(--acc); }
  input[readonly] { color:var(--mut); }
  .chart-tools select { flex:1 1 90px; min-width:90px; }
  .chart-tools input { flex:1 1 80px; min-width:80px; }
  .lid { display:flex; align-items:center; gap:4px; font-size:13px; white-space:nowrap; color:var(--txt); }
  .lid input { flex:0 0 auto; min-width:0; accent-color:var(--acc); }
  .tg { display:flex; align-items:center; gap:4px; font-size:13px; white-space:nowrap; color:var(--txt); }
  .tg input { width:64px; flex:0 0 auto; min-width:0; }
  #wInfo { color:var(--mut); font-size:12px; }
  canvas { flex:1 1 auto; min-height:0; width:100%; background:#0d1117; border:1px solid var(--line);
           border-radius:8px; display:block; }
  ul { list-style:none; margin:0; padding:0; }
  li { display:flex; align-items:center; gap:10px; padding:7px 4px; border-bottom:1px solid #21262d; font-size:13px; }
  li:last-child { border-bottom:none; }
  .fname { flex:1; color:var(--acc); word-break:break-all; font-variant-numeric:tabular-nums; }
  .fsize { color:var(--mut); font-size:12px; }
  a.dl { color:var(--acc); text-decoration:none; font-size:13px; }
  form { display:flex; gap:8px; flex-wrap:wrap; align-items:center; }
  form input { flex:1; min-width:90px; }
  .hint { color:var(--mut); font-size:12px; margin-top:8px; }
  .row { display:flex; gap:10px; align-items:center; flex-wrap:wrap; }
  #recInfo { font-size:13px; color:var(--mut); }
  @media (max-width:700px) {
    main { grid-template-columns:1fr; overflow-y:auto; }
    body { overflow:auto; }
    .col-left, .col-right { overflow:visible; }
    .chart-card { min-height:340px; }
  }
</style>
</head>
<body>
<header>
  <h1 data-i18n="appTitle">&#127777; ESP32-S3 手冲壶评测工具</h1>
  <span id="netBadge" class="badge">…</span>
  <span id="timeBadge" class="badge">--:--:--</span>
  <span id="devBadge" class="badge"></span>
  <button id="langBtn" onclick="toggleLang()">EN</button>
</header>
<main>
  <!-- 左列 40%: 置顶温度 + 全部功能 / left column: temp + controls -->
  <div class="col-left">
    <div class="card">
      <div class="temprow"><span id="tempNow">--.-</span><span class="unit">°C</span></div>
      <div id="tempStatus"></div>
      <div class="btns">
        <button id="recBtn" class="primary" onclick="toggleRec()">▶</button>
        <span id="recInfo"></span>
      </div>
    </div>

    <div class="card">
      <h3 data-i18n="kettleTitle">手冲壶管理</h3>
      <form onsubmit="saveKettle(event)">
        <input id="kName" data-i18n-ph="kettleName" required maxlength="20">
        <input id="kDry" type="number" step="0.1" min="0" data-i18n-ph="kettleDry" required>
        <input id="kLid" type="number" step="0.1" min="0" data-i18n-ph="kettleLid">
        <input id="kCap" type="number" step="1" min="0" data-i18n-ph="kettleCap">
        <button type="submit" class="primary" data-i18n="kettleSave">保存手冲壶</button>
      </form>
      <ul id="kettleList"><li class="hint">…</li></ul>
    </div>

    <div class="card">
      <h3 data-i18n="filesTitle">已存储温度记录 (CSV)</h3>
      <div class="btns">
        <button onclick="loadFiles()" data-i18n="refresh">刷新列表</button>
        <button class="danger" onclick="clearAll()" data-i18n="clearAll">清空全部</button>
      </div>
      <ul id="fileList"><li class="hint">…</li></ul>
    </div>

    <div class="card">
      <h3 data-i18n="wifiTitle">WiFi 设置</h3>
      <form onsubmit="saveConfig(event)">
        <input id="setSsid" data-i18n-ph="ssidPh" required maxlength="63">
        <input id="setPass" type="password" data-i18n-ph="passPh" required maxlength="63">
        <button type="submit" class="primary" data-i18n="wifiSave">保存并重连</button>
      </form>
      <div class="hint" data-i18n="wifiHint">保存后设备立即用新 SSID 连接;Host 模式下此连接用于联网/同步时间,不影响 4.4.4.1 管理页。</div>
    </div>

    <div class="card">
      <h3 data-i18n="netTitle">网络模式</h3>
      <div class="row">
        <button id="hostBtn" onclick="toggleHost()">…</button>
        <span id="hostInfo" class="hint"></span>
      </div>
    </div>
  </div>

  <!-- 右列 60%: 实时/分析 tab + 图表 / right column: tabs + chart -->
  <div class="col-right">
    <div class="card chart-card">
      <div class="chart-tabs">
        <button id="tabLive" class="primary" onclick="setMode('live')" data-i18n="tabLive">实时</button>
        <button id="tabAna" onclick="setMode('ana')" data-i18n="tabAna">CSV 分析</button>
      </div>
      <div id="anaPanel" class="ana-row" hidden>
        <select id="anaFile" onchange="runAnalysis()"><option value="" data-i18n="anaSelect">选择已保存的记录, 自动分析…</option></select>
      </div>
      <div id="anaResult" hidden></div>
      <div class="chart-tools" id="liveTools">
        <select id="kettleSel" onchange="selectKettle()"><option value="" data-i18n="noKettle">未选择壶</option></select>
        <label class="lid"><input type="radio" name="lid" value="on" checked onchange="setLid(true)"><span data-i18n="lidOn">有盖</span></label>
        <label class="lid"><input type="radio" name="lid" value="off" onchange="setLid(false)"><span data-i18n="lidOff">无盖</span></label>
        <input id="wTotal" type="number" step="0.1" min="0" data-i18n-ph="totalPh" oninput="calcWater()">
        <input id="wWater" type="number" step="0.1" min="0" data-i18n-ph="waterPh" readonly>
        <label class="tg"><span data-i18n="initTempLbl">初始温</span><input id="initIn" type="number" step="0.1" min="0" max="60" value="25" oninput="setTarget()"></label>
        <label class="tg"><span data-i18n="targetTempLbl">目标温</span><input id="targetIn" type="number" step="0.1" min="15" max="110" value="93" oninput="setTarget()"></label>
        <span id="wInfo"></span>
      </div>
      <canvas id="chart"></canvas>
    </div>
  </div>
</main>
<script>
// ==================== i18n (中文 / English) ====================
const I18N={
  zh:{
    appTitle:'&#127777; ESP32-S3 手冲壶评测工具',
    tabLive:'实时', tabAna:'CSV 分析', anaSelect:'选择已保存的记录, 自动分析…', noKettle:'未选择壶',
    kettleTitle:'手冲壶管理', kettleName:'名称, 如 fellow', kettleDry:'空壶重 g', kettleLid:'盖子重 g', kettleCap:'标称容量 ml', kettleSave:'保存手冲壶',
    kettleEmpty:'(空) 新建后 CSV 以 壶名_起始温-结束温_时间 命名', edit:'编辑', del:'删除', cur:' (当前)',
    kettleInfo:(d,l,c)=>'空'+d+'g / 盖'+l+'g'+(c?' / '+c+'ml':''),
    delKettle:(n)=>'删除手冲壶 '+n+' ?',
    filesTitle:'已存储温度记录 (CSV)', refresh:'刷新列表', clearAll:'清空全部', filesEmpty:'(空) 记录停止后会自动保存为 CSV 文件',
    download:'下载', delFile:(n)=>'删除 '+n+' ?', clearAllConfirm:'确定清空全部已存储记录?',
    wifiTitle:'WiFi 设置', ssidPh:'SSID', passPh:'密码', wifiSave:'保存并重连',
    wifiHint:'保存后设备立即用新 SSID 连接;Host 模式下此连接用于联网/同步时间,不影响 4.4.4.1 管理页。',
    wifiSaved:'已保存,正在重新连接 WiFi',
    netTitle:'网络模式', toSta:'切换到 STA 模式', toHost:'切换到 Host 模式',
    hostInfoHost:(ssid)=>'开放网络 '+ssid+'(无密码), 手机/电脑连接后自动跳转 4.4.4.1',
    hostInfoSta:(ip,conn)=>'STA 模式:连接路由器 WiFi, 管理页 '+ip+(conn?'':' (连接失败时将自动开启 AP 兜底)'),
    lidOn:'有盖', lidOff:'无盖', totalPh:'当前总重 g', waterPh:'水量 g', initTempLbl:'初始温', targetTempLbl:'目标温',
    recStart:'▶ 开始记录', recStop:'⏹ 停止记录',
    recIdle:'未记录', recRec:(c,s)=>'记录中 · 已采集 '+c+' 条 · 开始于 '+s,
    recCount:(n)=>'记录 '+n+' 条', timeNotSync:'时间未同步',
    tempOk:(t)=>'正常 · 设备时间 '+t,
    netHostBadge:(ssid,n,ip)=>'Host(AP) '+ssid+(n?(' · '+n+' 台设备'):'')+' · 管理页 '+ip,
    netStaBadge:(ssid,ip,r)=>'WiFi '+ssid+' '+ip+' ('+r+'dBm)', netNone:'WiFi 未连接',
    fault:{0xff:'传感器未连接',0x04:'热电偶开路',0x02:'热电偶短路GND',0x01:'热电偶短路VCC'},
    chartWait:'等待数据…',
    chartTarget:(t)=>'目标 '+t+'°', chartInit:(t)=>'初始 '+t+'°', chartPeak:(p,o)=>'峰值 '+p+'° (+'+o+'°)',
    anaUnheated:(a,b)=>'未加热 '+a+' → '+b, anaRise:(a)=>'升温段 0:00 → '+a, anaReach:(a)=>'到达目标 '+a,
    anaErrEmpty:'该文件没有可解析的数据(可能是更早期格式或已损坏)', anaErr:(e)=>'⚠ 分析失败: '+e,
    anaRows:{rise:'到达时间', over:'超调量', bias:'稳态偏差', osc:'稳态振荡'},
    anaRiseVal:(r,t,eff)=>'('+'理论 '+t+', 效率 '+eff+'%)',
    anaOverVal:(p,o)=>'峰值 '+p+'°, 超调 '+o+'°',
    anaBiasVal:(d,m,b)=>'稳态 '+d+' 内均值 '+m+'°, 偏差 '+b+'°',
    anaOscVal:(a,r)=>'±'+a+'° (波动范围 '+r+'°)',
    anaPower:'反推加热功率', anaPowerVal:(p)=>p+' W (由温升斜率×水量热容)', anaNoWater:'无水量数据, 无法反推',
    anaMeta:(k,w,t,i,dur,n)=>'壶: '+k+' · 水量 '+w+'g · 目标 '+t+'° · 初始基准 '+i+'° · 时长 '+dur+' · 样本 '+n+' 条',
    grade:{excellent:'优秀',good:'良好',fair:'及格',poor:'需优化'},
  },
  en:{
    appTitle:'&#127777; ESP32-S3 Pour-Over Kettle Evaluator',
    tabLive:'Live', tabAna:'CSV Analysis', anaSelect:'Pick a saved session, analyzes automatically…', noKettle:'No kettle',
    kettleTitle:'Kettles', kettleName:'Name, e.g. fellow', kettleDry:'Dry weight g', kettleLid:'Lid weight g', kettleCap:'Capacity ml', kettleSave:'Save kettle',
    kettleEmpty:'(empty) New sessions are named  kettle_start-end_temp_time',
    edit:'Edit', del:'Delete', cur:' (current)',
    kettleInfo:(d,l,c)=>'dry '+d+'g / lid '+l+'g'+(c?' / cap '+c+'ml':''),
    delKettle:(n)=>'Delete kettle '+n+' ?',
    filesTitle:'Saved sessions (CSV)', refresh:'Refresh', clearAll:'Clear all', filesEmpty:'(empty) Sessions are saved as CSV on stop',
    download:'Download', delFile:(n)=>'Delete '+n+' ?', clearAllConfirm:'Clear ALL saved sessions?',
    wifiTitle:'Wi-Fi settings', ssidPh:'SSID', passPh:'Password', wifiSave:'Save & reconnect',
    wifiHint:'The device reconnects with the new SSID immediately; in Host mode this link is for internet/NTP only — the 4.4.4.1 page is unaffected.',
    wifiSaved:'Saved, reconnecting Wi-Fi…',
    netTitle:'Network mode', toSta:'Switch to STA mode', toHost:'Switch to Host mode',
    hostInfoHost:(ssid)=>'Open network '+ssid+' (no password); phones auto-redirect to 4.4.4.1',
    hostInfoSta:(ip,conn)=>'STA mode: connects to your router, page at '+ip+(conn?'':' (AP fallback starts if connect fails)'),
    lidOn:'Lid on', lidOff:'Lid off', totalPh:'Total weight g', waterPh:'Water g', initTempLbl:'Init temp', targetTempLbl:'Target temp',
    recStart:'▶ Start', recStop:'⏹ Stop',
    recIdle:'Not recording', recRec:(c,s)=>'Recording · '+c+' samples · since '+s,
    recCount:(n)=>n+' samples', timeNotSync:'time not synced',
    tempOk:(t)=>'OK · device time '+t,
    netHostBadge:(ssid,n,ip)=>'Host(AP) '+ssid+(n?(' · '+n+' device'+(n>1?'s':'')):'')+' · page '+ip,
    netStaBadge:(ssid,ip,r)=>'WiFi '+ssid+' '+ip+' ('+r+'dBm)', netNone:'WiFi not connected',
    fault:{0xff:'Sensor missing',0x04:'Thermocouple open',0x02:'Short to GND',0x01:'Short to VCC'},
    chartWait:'Waiting for data…',
    chartTarget:(t)=>'Target '+t+'°', chartInit:(t)=>'Init '+t+'°', chartPeak:(p,o)=>'Peak '+p+'° (+'+o+'°)',
    anaUnheated:(a,b)=>'Unheated '+a+' → '+b, anaRise:(a)=>'Rise 0:00 → '+a, anaReach:(a)=>'Target reached '+a,
    anaErrEmpty:'No parseable data in this file (older format or corrupted)', anaErr:(e)=>'⚠ Analysis failed: '+e,
    anaRows:{rise:'Rise time', over:'Overshoot', bias:'Steady bias', osc:'Steady oscillation'},
    anaRiseVal:(r,t,eff)=>'('+'theoretical '+t+', efficiency '+eff+'%)',
    anaOverVal:(p,o)=>'peak '+p+'°, overshoot '+o+'°',
    anaBiasVal:(d,m,b)=>'mean '+m+'° over '+d+', bias '+b+'°',
    anaOscVal:(a,r)=>'±'+a+'° (range '+r+'°)',
    anaPower:'Inferred heating power', anaPowerVal:(p)=>p+' W (from rise slope × water heat capacity)', anaNoWater:'No water data, cannot infer',
    anaMeta:(k,w,t,i,dur,n)=>'Kettle: '+k+' · water '+w+'g · target '+t+'° · init ref '+i+'° · duration '+dur+' · samples '+n,
    grade:{excellent:'Excellent',good:'Good',fair:'Fair',poor:'Poor'},
  }
};
let lang=(localStorage.getItem('lang')==='en')?'en':'zh';
const T=(k)=>I18N[lang][k];

// ==================== 状态 / state ====================
const Y_MIN=15, Y_MAX=110, WINDOW=300;   // live chart Y range & X window (s)
let temps=[];       // {t:客户端epoch秒, v:温度} / client-epoch seconds
let rec=false, recSince=0;
let cur=null;       // 最近一次 /api/state
let kettles=[];     // 手冲壶列表 / kettle list
let lidOn=true;     // 有盖 / lid on
let target=93;      // 目标温度 ℃ / target temp
let initTemp=25;    // 初始设定温度 ℃ / init reference
let mode='live';    // live | ana
let anaCur=null;    // 分析模式当前结果 / analysis result
let anaFilesLoaded=false;

const $=id=>document.getElementById(id);
const p2=n=>String(n).padStart(2,'0');
const fmtTime=epoch=>{const d=new Date(epoch*1000);return p2(d.getHours())+':'+p2(d.getMinutes())+':'+p2(d.getSeconds());};
const fmtTick=s=>(s<0?'-':'')+Math.floor(Math.abs(s)/60)+':'+String(Math.floor(Math.abs(s)%60)).padStart(2,'0');
const fmtDur=s=>{ if(s==null)return '--'; const m=Math.floor(s/60); return m+'m'+String(Math.floor(s%60)).padStart(2,'0')+'s'; };
const fmtDurZh=s=>{ if(s==null)return '未到达'; const m=Math.floor(s/60); return m+'分'+String(Math.floor(s%60)).padStart(2,'0')+'秒'; };
const faultTxt=code=>T('fault')[code]||'';

// ==================== 语言切换 / language toggle ====================
function applyLang(){
  document.documentElement.lang=lang;
  document.querySelectorAll('[data-i18n]').forEach(el=>{ el.textContent=T(el.getAttribute('data-i18n')); });
  document.querySelectorAll('[data-i18n-ph]').forEach(el=>{ el.placeholder=T(el.getAttribute('data-i18n-ph')); });
  $('langBtn').textContent=lang==='zh'?'EN':'中';
  // 动态文本立即刷新 / refresh dynamic texts
  $('recBtn').textContent=rec?T('recStop'):T('recStart');
  if(anaCur) runAnalysis();
  draw();
}
function toggleLang(){ lang=lang==='zh'?'en':'zh'; localStorage.setItem('lang',lang); applyLang(); }

// ==================== 轮询 / polling ====================
async function poll(){
  try{ const r=await fetch('/api/state'); cur=await r.json(); }catch(e){ return; }
  // 顶部状态徽章 / header badges
  const nb=$('netBadge');
  if(cur.host){ nb.textContent=T('netHostBadge')(cur.apSsid,cur.staNum,cur.ip); nb.className='badge white'; }
  else if(cur.conn){ nb.textContent=T('netStaBadge')(cur.ssid,cur.ip,cur.rssi); nb.className='badge blue'; }
  else { nb.textContent=T('netNone'); nb.className='badge yellow'; }
  $('timeBadge').textContent = cur.synced ? fmtTime(cur.time) : T('timeNotSync');
  $('devBadge').textContent = T('recCount')(cur.recCount);
  // 称量状态回填 / restore weighing state
  if(typeof cur.lid!=='undefined'){
    lidOn=cur.lid;
    document.querySelectorAll('input[name="lid"]').forEach(r=>{ r.checked=(r.value==='on')===lidOn; });
    if(cur.total>0 && !$('wTotal').value) $('wTotal').value=cur.total.toFixed(1);
    if(cur.water>0 && !$('wWater').value) $('wWater').value=cur.water.toFixed(1);
    if(cur.target>0 && !$('targetIn').value){ target=cur.target; $('targetIn').value=target.toFixed(1); }
    if(cur.init>0 && !$('initIn').value){ initTemp=cur.init; $('initIn').value=initTemp.toFixed(1); }
  }
  // 温度 / temperature
  if(cur.t!==null && !isNaN(cur.t)){
    $('tempNow').textContent=cur.t.toFixed(2);
    $('tempStatus').textContent= cur.faultStr ? ('⚠ '+faultTxt(cur.fault)) : T('tempOk')(fmtTime(cur.time));
    temps.push({t:Date.now()/1000, v:cur.t});
    const cut=Date.now()/1000-WINDOW;
    while(temps.length && temps[0].t<cut) temps.shift();
  } else {
    $('tempStatus').textContent='⚠ '+(cur.faultStr?faultTxt(cur.fault):'-');
  }
  // 记录状态 / recording state
  const wasRec=rec;
  rec=cur.rec;
  if(rec!==wasRec){
    recSince=Date.now()/1000;
    if(wasRec) loadFiles();   // 刚停止 → 刷新文件列表 / refresh file list after stop
  }
  const rb=$('recBtn');
  if(rec){ rb.textContent=T('recStop'); rb.className='rec-on'; }
  else  { rb.textContent=T('recStart'); rb.className='primary'; }
  $('recInfo').textContent= rec ? T('recRec')(cur.recCount, fmtTime(recSince)) : T('recIdle');
  // 网络模式按钮 / network mode button
  $('hostBtn').textContent = cur.host ? T('toSta') : T('toHost');
  $('hostInfo').textContent = cur.host
    ? T('hostInfoHost')(cur.apSsid)
    : T('hostInfoSta')(cur.ip, cur.conn);
  draw();
}
setInterval(poll,1000); poll(); loadFiles(); loadKettles(); applyLang();

// ==================== 手冲壶 / kettles ====================
function curKettle(){ return kettles.find(k=>k.name===$('kettleSel').value)||null; }
function showKettleInfo(){
  const k=curKettle();
  $('wInfo').textContent = k ? T('kettleInfo')(k.dry,k.lid,k.cap) : '';
}
async function loadKettles(){
  try{
    const r=await fetch('/api/kettles'); const d=await r.json();
    kettles=d.kettles;
    const sel=$('kettleSel');
    sel.innerHTML='<option value="">'+T('noKettle')+'</option>'+kettles.map(k=>`<option value="${k.name.replace(/"/g,'&quot;')}">${k.name}</option>`).join('');
    sel.value=d.cur||'';
    const ul=$('kettleList'); ul.innerHTML='';
    if(!kettles.length){ ul.innerHTML='<li class="hint">'+T('kettleEmpty')+'</li>'; }
    for(const k of kettles){
      const li=document.createElement('li');
      const nm=document.createElement('span'); nm.className='fname'; nm.textContent=k.name+(k.name===d.cur?T('cur'):'');
      const info=document.createElement('span'); info.className='fsize'; info.textContent=T('kettleInfo')(k.dry,k.lid,k.cap);
      const edit=document.createElement('button'); edit.textContent=T('edit');
      edit.onclick=()=>{ $('kName').value=k.name; $('kDry').value=k.dry; $('kLid').value=k.lid; $('kCap').value=k.cap||''; $('kName').focus(); };
      const del=document.createElement('button'); del.textContent=T('del'); del.className='danger';
      del.onclick=async()=>{ if(!confirm(T('delKettle')(k.name)))return; await post('/api/kettles/delete','name='+encodeURIComponent(k.name)); loadKettles(); };
      li.append(nm,info,edit,del); ul.appendChild(li);
    }
    showKettleInfo();
  }catch(e){}
}
async function selectKettle(){
  const name=$('kettleSel').value;
  if(name) await post('/api/kettles/select','name='+encodeURIComponent(name));
  showKettleInfo();
  calcWater();
}
function setLid(v){ lidOn=v; calcWater(); }
function calcWater(){
  const k=curKettle(); if(!k) return;
  const total=parseFloat($('wTotal').value);
  if(isNaN(total)) return;
  const water=Math.max(0,total-k.dry-(lidOn?k.lid:0));
  $('wWater').value=water.toFixed(1);
  post('/api/kettle/state','kettle='+encodeURIComponent(k.name)+'&lid='+(lidOn?'on':'off')+'&total='+total.toFixed(1));
}
function setTarget(){
  const v=parseFloat($('targetIn').value);
  const iv=parseFloat($('initIn').value);
  if(isNaN(v)||v<15||v>110){ $('targetIn').value=target.toFixed(1); return; }
  target=v;
  if(isNaN(iv)||iv<0||iv>60){ $('initIn').value=initTemp.toFixed(1); }
  else initTemp=iv;
  post('/api/target','t='+target.toFixed(1)+'&init='+initTemp.toFixed(1));
  draw();
}
async function saveKettle(ev){
  ev.preventDefault();
  const name=$('kName').value.trim();
  const dry=parseFloat($('kDry').value);
  const lid=parseFloat($('kLid').value)||0;
  const cap=parseFloat($('kCap').value)||0;
  if(!name||isNaN(dry)) return;
  await post('/api/kettles','name='+encodeURIComponent(name)+'&dry='+dry+'&lid='+lid+'&cap='+cap);
  $('kName').value=''; $('kDry').value=''; $('kLid').value=''; $('kCap').value='';
  loadKettles();
}

// ==================== 图表 / charts ====================
function setMode(m){
  mode=m;
  $('tabLive').className=mode==='live'?'primary':'';
  $('tabAna').className=mode==='ana'?'primary':'';
  $('anaPanel').hidden=mode!=='ana';
  $('anaResult').hidden=mode!=='ana';
  $('liveTools').hidden=mode!=='live';
  $('chart').style.flex = (m==='ana') ? '0 0 320px' : '';
  if(mode==='ana'&&!anaFilesLoaded) fillAnaFiles();
  draw();
}
function draw(){ if(mode==='ana'){ if(anaCur) drawAnalysis(); } else drawLive(); }
function drawLive(){
  const c=$('chart'); const dpr=window.devicePixelRatio||1;
  const W=c.clientWidth, H=c.clientHeight;
  if(!W||!H) return;
  if(c.width!==W*dpr||c.height!==H*dpr){ c.width=W*dpr; c.height=H*dpr; }
  const g=c.getContext('2d'); g.setTransform(dpr,0,0,dpr,0,0);
  g.clearRect(0,0,W,H);
  const padL=36, padR=10, padT=10, padB=22;
  const x0=padL, x1=W-padR, y0=padT, y1=H-padB;
  const now=Date.now()/1000, t0=now-WINDOW;
  const X=t=>x0+(x1-x0)*(t-t0)/WINDOW;
  const Y=v=>y1-(y1-y0)*(v-Y_MIN)/(Y_MAX-Y_MIN);
  g.strokeStyle='#21262d'; g.fillStyle='#8b949e'; g.font='11px monospace';
  for(let v=Y_MIN;v<=Y_MAX;v+=10){
    const y=Y(v);
    g.beginPath(); g.moveTo(x0,y); g.lineTo(x1,y); g.stroke();
    g.textAlign='right'; g.fillText(v+'°', x0-6, y+4);
  }
  g.textAlign='center';
  for(let t=Math.ceil(t0/60)*60; t<=now; t+=60) g.fillText(fmtTime(t), X(t), H-6);
  if(rec){
    const from=Math.max(t0,recSince);
    g.fillStyle='rgba(248,81,73,.08)';
    g.fillRect(X(from), y0, X(now)-X(from), y1-y0);
  }
  if(target>=Y_MIN && target<=Y_MAX){
    const yt=Y(target);
    g.save();
    g.setLineDash([6,4]);
    g.strokeStyle='rgba(240,136,62,.75)'; g.lineWidth=1.2;
    g.beginPath(); g.moveTo(x0,yt); g.lineTo(x1,yt); g.stroke();
    g.setLineDash([]);
    g.fillStyle='#f0883e'; g.font='11px sans-serif';
    g.textAlign='left'; g.fillText(T('chartTarget')(target.toFixed(1)), x0+6, yt-5);
    g.restore();
  }
  if(temps.length>1){
    g.strokeStyle='#4fc3f7'; g.lineWidth=1.8; g.beginPath();
    let started=false;
    for(const pt of temps){
      const x=X(pt.t), y=Y(pt.v);
      if(x<x0||x>x1) continue;
      if(!started){ g.moveTo(x,y); started=true; } else g.lineTo(x,y);
    }
    g.stroke();
    const lp=temps[temps.length-1];
    g.fillStyle='#4fc3f7'; g.beginPath(); g.arc(X(lp.t),Y(lp.v),3,0,7); g.fill();
  } else {
    g.fillStyle='#8b949e'; g.textAlign='center';
    g.fillText(T('chartWait'), W/2, (y0+y1)/2);
  }
}

// ==================== CSV 分析 / CSV analysis (browser-side, ported from tools/analyze.py) ====================
function parseCsvText(text){
  const meta={}; const rows=[];
  for(const line of text.split('\n')){
    const l=line.trim(); if(!l) continue;
    if(l[0]==='#'){
      let m=l.match(/init:([\d.]+)c/); if(m) meta.init=parseFloat(m[1]);
      m=l.match(/kettle:([^,]+)/); if(m) meta.kettle=m[1].trim();
      m=l.match(/water:([\d.]+)g/); if(m) meta.water=parseFloat(m[1]);
      continue;
    }
    if(/^time/i.test(l)) continue;
    const p=l.split(',');
    if(p.length<2) continue;   // 兼容 2/3/4 列旧格式 / tolerate 2/3/4-col old formats
    let sec;
    if(p[0].startsWith('uptime:')) sec=parseFloat(p[0].slice(7));
    else{ const [d,ti]=p[0].split(' '); const [y,mo,da]=d.split('-').map(Number); const [h,mi,s]=ti.split(':').map(Number); sec=Date.UTC(y,mo-1,da,h,mi,s)/1000; }
    rows.push([sec, parseFloat(p[1]),
               p.length>2?parseFloat(p[2])||0:0,
               p.length>3?parseFloat(p[3])||0:93]);
  }
  if(!rows.length) return {meta, data:[], init:0, target:93, empty:true};
  rows.sort((a,b)=>a[0]-b[0]);
  const t0=rows[0][0];
  const data=rows.map(r=>[r[0]-t0, r[1], r[2], r[3]]);
  const init=meta.init||data[0][1];
  const targets=[...new Set(data.map(r=>r[3]).filter(v=>v>0))];
  const target=targets[Math.floor(targets.length/2)]||93;
  return {meta, data, init, target, empty:false};
}
function analyzeData(data, init, target, water){
  const n=data.length, pv=data.map(r=>r[1]);
  // 升温起点(差分法) / rise start via differential detection
  let rise0=0;
  for(let i=0;i<n-3;i++){ if(pv[i+1]-pv[i]>=0.25 && pv[i+3]-pv[i]>=0.5){ rise0=i+1; break; } }
  const rise0t=data[rise0][0];
  const D=data.map(r=>[r[0]-rise0t, r[1], r[2], r[3]]);
  const t=D.map(r=>r[0]), v=D.map(r=>r[1]), nn=D.length;
  let riseIdx=null;
  for(let i=0;i<nn;i++){ if(v[i]>=target-0.5){ riseIdx=i; break; } }
  const riseT=riseIdx===null?null:t[riseIdx];
  // 稳态段 / steady segment: from first entry ±0.5℃, never leaving ±1℃ afterwards
  let sIdx=riseIdx===null?nn-1:riseIdx;
  if(riseIdx!==null){
    outer: for(let i=riseIdx;i<nn;i++){ for(let j=i;j<nn;j++){ if(Math.abs(v[j]-target)>1.0) continue outer; } sIdx=i; break; }
  }
  const steady=v.slice(sIdx);
  const steadyT=t[sIdx], steadyDur=t[nn-1]-steadyT;
  const steadyMean=steady.reduce((a,b)=>a+b,0)/steady.length;
  const steadyBias=steadyMean-target;
  const steadyOsc=Math.max(...steady)-Math.min(...steady);
  const peak=Math.max(...v), overshoot=peak-target;
  // 反推功率 / inferred power from rise slope × water heat capacity
  const lo=v[Math.floor(nn*0.1)], hi=v[Math.floor(nn*0.9)];
  const seg=D.filter(r=>r[1]>=lo&&r[1]<=hi);
  let slope=0;
  if(seg.length>2){
    const mx=seg.reduce((a,r)=>a+r[0],0)/seg.length;
    const my=seg.reduce((a,r)=>a+r[1],0)/seg.length;
    let sxy=0,sxx=0;
    for(const r of seg){ sxy+=(r[0]-mx)*(r[1]-my); sxx+=(r[0]-mx)**2; }
    slope=sxx?sxy/sxx:0;
  }
  const power=4.18*water*slope;
  const tTheory=(power>0&&riseT)?4.18*water*(target-init)/power:0;
  const ratio=(tTheory>0&&riseT)?tTheory/riseT:0;
  return {riseT,steadyT,steadyDur,steadyMean,steadyBias,steadyOsc,peak,overshoot,power,tTheory,ratio,span:t[nn-1]-t[0],data:D};
}
function scoreOf(ms){
  const rise= ms.riseT==null?0:Math.max(0,30-Math.max(0,0.8-ms.ratio)/0.05*4);
  const over= Math.max(0,25-Math.max(0,ms.overshoot-0.5)/0.5*10);
  const bias= Math.max(0,25-Math.max(0,Math.abs(ms.steadyBias)-0.3)/0.3*10);
  const osc=  Math.max(0,20-Math.max(0,(ms.steadyOsc/2)-0.5)/2.5*20);
  const total=rise+over+bias+osc;
  const gradeKey= total>=90?'excellent':total>=70?'good':total>=50?'fair':'poor';
  const color= total>=90?'#4fd6a8':total>=70?'#4fc3f7':total>=50?'#ffd740':'#ff5252';
  return {rise,over,bias,osc,total,grade:gradeKey,color};
}
async function fillAnaFiles(){
  try{
    const r=await fetch('/api/files'); const list=await r.json();
    $('anaFile').innerHTML='<option value="">'+T('anaSelect')+'</option>'+
      list.map(f=>`<option value="${encodeURIComponent(f.name)}">${f.name}</option>`).join('');
    anaFilesLoaded=true;
  }catch(e){}
}
function showAnaError(msg){
  anaCur=null;
  $('anaResult').innerHTML='<div class="ana-err">'+msg+'</div>';
  $('anaResult').hidden=false;
}
async function runAnalysis(){
  const v=$('anaFile').value; if(!v) return;
  try{
    const text=await (await fetch('/download?name='+v)).text();
    const res=parseCsvText(text);
    if(res.empty){ showAnaError(T('anaErrEmpty')); return; }
    const {meta,data,init,target}=res;
    const ms=analyzeData(data,init,target,parseFloat(meta.water)||0);
    const sc=scoreOf(ms);
    anaCur={data:ms.data,init,target,ms,sc,meta};
    const durFmt=lang==='zh'?fmtDurZh:fmtDur;
    const rows=[
      [T('anaRows').rise, durFmt(ms.riseT)+(ms.tTheory?T('anaRiseVal')(durFmt(ms.tTheory),(ms.ratio*100).toFixed(0)):''), sc.rise,30],
      [T('anaRows').over, T('anaOverVal')(ms.peak.toFixed(2),ms.overshoot.toFixed(2)), sc.over,25],
      [T('anaRows').bias, T('anaBiasVal')(durFmt(ms.steadyDur),ms.steadyMean.toFixed(2),(ms.steadyBias>=0?'+':'')+ms.steadyBias.toFixed(2)), sc.bias,25],
      [T('anaRows').osc, T('anaOscVal')((ms.steadyOsc/2).toFixed(2),ms.steadyOsc.toFixed(2)), sc.osc,20],
    ];
    $('anaResult').innerHTML=
      '<div class="ana-scorecard">'+
        '<div class="ana-num" style="color:'+sc.color+'">'+sc.total.toFixed(0)+'</div>'+
        '<div>'+
          '<div class="ana-g" style="color:'+sc.color+'">'+T('grade')[sc.grade]+' / 100</div>'+
          '<div class="ana-meta">'+T('anaMeta')(meta.kettle||'-',meta.water||'0',target.toFixed(1),init.toFixed(1),durFmt(ms.span),data.length)+'</div>'+
        '</div>'+
      '</div>'+
      '<table class="ana-table">'+
        rows.map(r=>`<tr><td>${r[0]}</td><td>${r[1]}</td><td><b>${r[2].toFixed(0)}</b> / ${r[3]}</td></tr>`).join('')+
        '<tr><td style="color:#8b949e">'+T('anaPower')+'</td><td style="color:#8b949e">'+(ms.power>0?T('anaPowerVal')(ms.power.toFixed(0)):T('anaNoWater'))+'</td><td></td></tr>'+
      '</table>';
    $('anaResult').hidden=false;
    draw();
  }catch(e){ showAnaError(T('anaErr')(e.message||String(e))); }
}
function drawAnalysis(){
  const {data,init,target,ms}=anaCur;
  const c=$('chart'); const dpr=window.devicePixelRatio||1;
  const W=c.clientWidth, H=c.clientHeight;
  if(!W||!H) return;
  if(c.width!==W*dpr||c.height!==H*dpr){ c.width=W*dpr; c.height=H*dpr; }
  const g=c.getContext('2d'); g.setTransform(dpr,0,0,dpr,0,0);
  g.clearRect(0,0,W,H);
  const padL=36,padR=10,padT=10,padB=22;
  const x0=padL,x1=W-padR,y0=padT,y1=H-padB;
  const xs=data.map(r=>r[0]), ys=data.map(r=>r[1]);
  // y 轴范围: 跨度上下各 5% 余量 / y-range: 5% padding each side of the data span
  const dmin=Math.min(...ys,init), dmax=Math.max(...ys,target);
  const ypad=Math.max(0.5,(dmax-dmin)*0.05);
  const ymin=Math.floor(dmin-ypad), ymax=Math.ceil(dmax+ypad);
  const xmin=xs[0], xmax=xs[xs.length-1]||1;
  const X=x=>x0+(x1-x0)*(x-xmin)/(xmax-xmin);
  const Y=v=>y1-(y1-y0)*(v-ymin)/(ymax-ymin);
  g.strokeStyle='#21262d'; g.fillStyle='#8b949e'; g.font='11px monospace';
  const step=Math.max(1,Math.round((ymax-ymin)/8));
  for(let v=ymin;v<=ymax;v+=step){ const y=Y(v); g.beginPath(); g.moveTo(x0,y); g.lineTo(x1,y); g.stroke(); g.textAlign='right'; g.fillText(v+'°',x0-6,y+4); }
  g.textAlign='center';
  for(let s=Math.floor(xmin/60)*60;s<=xmax+60;s+=60) g.fillText(fmtTick(s),X(s),H-6);
  // 三段背景: 蓝=未加热, 红=升温, 绿=稳态 / segments: blue=unheated, red=rise, green=steady
  const fitText=(txt,cx,maxW,color)=>{ let fs=11; g.font=fs+'px sans-serif';
    while(g.measureText(txt).width>maxW && fs>8){ fs-=0.5; g.font=fs+'px sans-serif'; }
    g.fillStyle=color; g.textAlign='center'; g.fillText(txt,cx,y1-4); };
  const zx=X(0);
  g.strokeStyle='#ff5252'; g.setLineDash([2,3]); g.lineWidth=1;
  g.beginPath(); g.moveTo(zx,y0); g.lineTo(zx,y1); g.stroke(); g.setLineDash([]);
  if(zx>x0+2){ g.fillStyle='rgba(79,195,247,.07)'; g.fillRect(x0,y0,zx-x0,y1-y0);
    g.font='11px sans-serif'; g.fillStyle='#4fc3f7'; g.textAlign='left';
    g.fillText(T('anaUnheated')(fmtTick(xmin),'0:00'),x0,y1-4); }
  if(ms.riseT!=null){
    const rx=X(ms.riseT);
    g.fillStyle='rgba(255,82,82,.08)'; g.fillRect(zx,y0,rx-zx,y1-y0);
    fitText(T('anaRise')(fmtTick(ms.riseT)),(zx+rx)/2,rx-zx-6,'#ff5252');
    g.strokeStyle='#4fd6a8'; g.setLineDash([2,3]); g.lineWidth=1; g.beginPath(); g.moveTo(rx,y0); g.lineTo(rx,y1); g.stroke(); g.setLineDash([]);
    g.font='11px sans-serif'; g.fillStyle='#4fd6a8'; g.textAlign='right'; g.fillText(T('anaReach')(fmtTick(ms.riseT)),x1-6,y1-4);
  }
  if(ms.steadyT!=null){ const sx0=X(ms.steadyT); g.fillStyle='rgba(79,214,168,.07)'; g.fillRect(sx0,y0,x1-sx0,y1-y0); }
  // 目标/初始/峰值虚线 + 顶部居中标注 / dashed lines + top-center labels
  g.setLineDash([6,4]); g.strokeStyle='#f0883e'; g.lineWidth=1.2;
  g.beginPath(); g.moveTo(x0,Y(target)); g.lineTo(x1,Y(target)); g.stroke(); g.setLineDash([]);
  g.strokeStyle='#8b949e'; g.setLineDash([3,4]);
  g.beginPath(); g.moveTo(x0,Y(init)); g.lineTo(x1,Y(init)); g.stroke(); g.setLineDash([]);
  if(ms.overshoot>0){ const py=Y(ms.peak); g.strokeStyle='#ff5252'; g.setLineDash([2,3]); g.beginPath(); g.moveTo(x0,py); g.lineTo(x1,py); g.stroke(); g.setLineDash([]); }
  const topLabels=[
    [T('chartTarget')(target.toFixed(1)),'#f0883e'],
    [T('chartInit')(init.toFixed(1)),'#8b949e'],
  ];
  if(ms.overshoot>0) topLabels.push([T('chartPeak')(ms.peak.toFixed(1),ms.overshoot.toFixed(1)),'#ff5252']);
  g.font='11px sans-serif'; g.textAlign='left';
  const totalW=topLabels.reduce((a,l)=>a+g.measureText(l[0]).width,0)+topLabels.length*20;
  let lx=(x0+x1)/2-totalW/2;
  for(const l of topLabels){ g.fillStyle=l[1]; g.fillText(l[0],lx,y0+2); lx+=g.measureText(l[0]).width+20; }
  // 温度曲线 / temperature curve
  g.strokeStyle='#4fc3f7'; g.lineWidth=1.8; g.beginPath();
  data.forEach((r,i)=>{ const x=X(r[0]), y=Y(r[1]); i?g.lineTo(x,y):g.moveTo(x,y); });
  g.stroke();
}

// ==================== 通用 / common ====================
async function post(u,b){
  try{ await fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b}); }catch(e){}
}
async function toggleRec(){
  await post('/api/record','action='+(rec?'stop':'start'));
  await poll();
}
async function loadFiles(){
  try{
    const r=await fetch('/api/files'); const list=await r.json();
    const ul=$('fileList'); ul.innerHTML='';
    if(!list.length){ ul.innerHTML='<li class="hint">'+T('filesEmpty')+'</li>'; return; }
    for(const f of list){
      const li=document.createElement('li');
      const nm=document.createElement('span'); nm.className='fname'; nm.textContent=f.name;
      const sz=document.createElement('span'); sz.className='fsize'; sz.textContent=(f.size/1024).toFixed(1)+' KB';
      const dl=document.createElement('a'); dl.className='dl'; dl.href='/download?name='+encodeURIComponent(f.name); dl.textContent=T('download');
      const del=document.createElement('button'); del.textContent=T('del'); del.className='danger';
      del.onclick=async()=>{ if(!confirm(T('delFile')(f.name)))return; await post('/api/delete','name='+encodeURIComponent(f.name)); loadFiles(); };
      li.append(nm,sz,dl,del); ul.appendChild(li);
    }
  }catch(e){}
}
async function clearAll(){
  if(!confirm(T('clearAllConfirm')))return;
  await post('/api/clear',''); loadFiles();
}
async function saveConfig(ev){
  ev.preventDefault();
  const ssid=$('setSsid').value.trim(), pass=$('setPass').value;
  if(!ssid)return;
  await post('/api/config','ssid='+encodeURIComponent(ssid)+'&pass='+encodeURIComponent(pass));
  alert(T('wifiSaved'));
}
async function toggleHost(){
  const mode=cur&&cur.host?'sta':'host';
  await post('/api/host','mode='+mode);
  setTimeout(poll, 800);
}
</script>
</body>
</html>)rawliteral";
