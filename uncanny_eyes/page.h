// Uncanny Eyes – https://www.youtube.com/@fluxandfur – https://github.com/fluxfur/uncannyeyes
// Copyright (c) 2026 @fluxandfur – MIT License (see LICENSE)
//
// page.h – the web page (one page: eyes, camera, calibration, behavior, vision behavior).
// Served by web.cpp. Plain HTML/CSS/JS, dark theme.
#pragma once
#include "config.h"           // UE_VERSION

static const char PAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Uncanny Eyes</title>
<style>
  :root { color-scheme: dark; --bg: #121212; --card: #1c1c1c; --pad: #262626; --line: #3a3a3a;
          --field: #2a2a2a; --text: #e4e4e4; --dim: #9a9a9a; --accent: #1db584; --warn: #f0a040; --bad: #e5534b; }
  * { box-sizing: border-box; }
  body { font-family: system-ui, sans-serif; background: var(--bg); color: var(--text);
         max-width: 520px; margin: 0 auto; padding: 0 1rem 5rem; }

  header { position: sticky; top: 0; z-index: 2; background: var(--bg); padding: 0.8rem 0 0.6rem;
           border-bottom: 1px solid var(--line); margin-bottom: 1rem; }
  .top { display: flex; align-items: center; gap: 0.6rem; flex-wrap: wrap; }
  .top b { font-size: 1.15rem; }
  #dot { width: 10px; height: 10px; border-radius: 50%; background: #666; display: inline-block; }
  #dot.ok { background: var(--accent); } #dot.bad { background: var(--bad); }
  .pill { margin-left: auto; background: var(--card); border: 1px solid var(--line); border-radius: 999px;
          padding: 0.15rem 0.7rem; color: var(--accent); font-size: 0.9rem; }
  #tstat { font-size: 0.8rem; color: var(--dim); margin-top: 0.35rem; font-variant-numeric: tabular-nums; }
  .sub2 { font-size: 0.72rem; color: var(--dim); opacity: 0.8; font-variant-numeric: tabular-nums; }
  .face { color: var(--accent); font-weight: bold; } .head { color: var(--warn); font-weight: bold; }

  .pad { position: relative; width: 100%; max-width: 300px; aspect-ratio: 1; margin: 0 auto 0.75rem;
         background: var(--pad); border-radius: 16px; touch-action: none; user-select: none;
         -webkit-user-select: none; border: 2px solid var(--line); }
  .pad::before, .pad::after { content: ""; position: absolute; background: var(--line); }
  .pad::before { left: 50%; top: 0; width: 1px; height: 100%; }
  .pad::after  { top: 50%; left: 0; height: 1px; width: 100%; }
  .knob { position: absolute; width: 56px; height: 56px; border-radius: 50%; background: var(--accent);
          opacity: 0.9; pointer-events: none; transform: translate(-50%, -50%); box-shadow: 0 2px 10px #000a; }
  .ell { position: absolute; border: 2px dashed var(--accent); border-radius: 50%; pointer-events: none; opacity: 0.6; }
  .controls { display: flex; justify-content: center; gap: 1.2rem; align-items: center; flex-wrap: wrap; margin-bottom: 1.2rem; }

  details.card { background: var(--card); border: 1px solid var(--line); border-radius: 12px;
                 padding: 0 1rem; margin-bottom: 0.8rem; }
  details.card[open] { padding-bottom: 0.9rem; }
  summary { cursor: pointer; padding: 0.8rem 0; font-weight: 600; list-style: none;
            display: flex; gap: 0.5rem; align-items: baseline; }
  summary::-webkit-details-marker { display: none; }
  summary::before { content: "\25B8"; color: var(--dim); display: inline-block; transition: transform 0.15s; }
  details[open] > summary::before { transform: rotate(90deg); }
  summary small { font-weight: normal; color: var(--dim); }
  details.sub { border-top: 1px solid var(--line); margin-top: 0.8rem; }
  details.sub > summary { font-weight: normal; color: var(--dim); padding: 0.6rem 0 0.2rem; }
  h4.servo { margin-top: 0.8rem; font-size: 0.95rem; }
  h4 { margin: 1.1rem 0 0.2rem; font-weight: 600; display: flex; gap: 0.5rem; align-items: baseline; }
  h4 .val { color: var(--accent); } h4 small { font-weight: normal; color: var(--dim); }
  .tag { color: var(--warn); font-weight: bold; font-size: 0.72rem; }

  input, select, button { font: inherit; color: var(--text); background: var(--field);
                          border: 1px solid var(--line); border-radius: 6px; }
  input[type=number] { width: 3.7rem; padding: 0.3rem; font-size: 16px; }   /* 16px: no zoom on iPhone */
  select { padding: 0.3rem; font-size: 16px; max-width: 100%; }
  input[type=checkbox] { accent-color: var(--accent); width: 1.2rem; height: 1.2rem; vertical-align: -4px; }
  input[type=range] { width: 100%; height: 2rem; accent-color: var(--accent); background: none; border: none; }
  button { padding: 0.4rem 0.8rem; cursor: pointer; min-height: 2.4rem; }
  button:disabled { opacity: 0.35; cursor: default; }
  button.small { font-size: 0.85rem; padding: 0.3rem 0.7rem; min-height: 2.2rem; }
  label { padding: 0.15rem 0; }
  button.primary { background: var(--accent); border-color: var(--accent); color: #fff; }
  .cfg { display: flex; gap: 0.5rem 0.8rem; align-items: center; flex-wrap: wrap; margin-top: 0.5rem; }
  .sets { display: flex; gap: 0.5rem; margin-top: 0.5rem; }
  .sets button.on { background: var(--accent); color: #fff; border-color: var(--accent); }
  .hint { font-size: 0.82rem; color: var(--dim); margin: 0.5rem 0 0; line-height: 1.45; }
  /* Advanced off: hide everything marked .adv */
  body:not(.advanced) .adv { display: none !important; }
  .ver { color: var(--dim); font-weight: normal; }
  .advtgl { margin-left: auto; font-size: 0.85rem; color: var(--dim); white-space: nowrap; }

  .acts { width: 100%; border-collapse: collapse; margin-top: 0.6rem; }
  .acts td { padding: 0.18rem 0.3rem 0.18rem 0; }
  .acts td:nth-child(2) { white-space: nowrap; }
  .acts input { width: 3.4rem; }
  .acts tr.na { opacity: 0.35; }
  .acts small { color: var(--dim); }
  .acts .d { display: block; font-size: 0.78rem; color: var(--dim); line-height: 1.3; }
  .acts tr.grp td { padding-top: 0.7rem; font-size: 0.72rem; color: var(--dim);
                    text-transform: uppercase; letter-spacing: 0.05em; }
  .acts tr.grp ~ tr.grp td { padding-top: 1.3rem; }

  .grid { display: grid; grid-template-columns: 1fr auto; gap: 0.4rem 0.8rem; align-items: center; margin-top: 0.6rem; }
  .grid input[type=number] { width: 5rem; }
  #cam { width: 100%; border-radius: 8px; background: #000; display: block; aspect-ratio: 4 / 3; }

  .savebar { position: fixed; bottom: 0; left: 0; right: 0; z-index: 2; background: #121212f2;
             border-top: 1px solid var(--line); padding: 0.6rem 1rem; }
  .savebar > div { max-width: 520px; margin: 0 auto; display: flex; gap: 0.6rem; align-items: center; }
  #status { font-size: 0.85rem; color: var(--dim); }
  #status.dirty { color: var(--warn); font-weight: bold; }
</style>
</head>
<body>
  <header>
    <div class="top">
      <b>Uncanny Eyes</b> <small class="adv ver">v)rawliteral" UE_VERSION R"rawliteral(</small>
      <span id="dot" class="bad" title="connection"></span>
      <span class="pill" id="bstat">-</span>
      <button class="small" id="resume" hidden>Resume</button>
      <button class="small" id="relearn2" title="Learn the empty room again">Relearn room</button>
    </div>
    <div id="tstat">camera: -</div>
    <div id="ttime" class="sub2 adv"></div>
  </header>

  <div class="pad" id="pad"><div class="ell" id="ell"></div><div class="knob" id="knob"></div></div>
  <div class="controls">
    <button id="center">Center</button>
    <label><input type="checkbox" id="spring"> Spring back</label>
    <label><input type="checkbox" id="free"> Free (no ellipse)</label>
  </div>

  <details class="card" id="c-cam" open>
    <summary>Camera <small>live view and tracking</small></summary>
    <img id="cam" alt="">
    <p class="hint">Magenta person, white head, blue face search, green face, red target</p>
    <div class="cfg">
      <button id="relearn">Relearn empty room</button>
      <label class="adv"><input type="checkbox" id="ftest"> Test: fast tracking</label>
    </div>
    <div class="grid" id="cam-b"></div>
    <div class="grid adv" id="cam-a"></div>
    <details class="sub adv">
      <summary>Detection tuning</summary>
      <div class="grid" id="cam-d"></div>
      <p class="hint">Person not found or patchy: lower the foreground threshold. Noise or shadows counted as
        people: raise it, or the minimum person size / height. Objects that never move turn blue and fade out.
        A person standing still turning blue: lower the movement threshold or raise "Still &rarr; object".</p>
    </details>
    <details class="sub adv">
      <summary>Exposure and image</summary>
      <div class="grid" id="cam-e"></div>
    </details>
    <p class="hint">Camera settings are saved automatically. The live view only runs while this section is open.
      <span class="adv">Test: fast tracking = follow only, as fast as possible (not saved).</span></p>
  </details>

  <details class="card" id="c-cal" open>
    <summary>Calibration <small>eye sets and servos</small></summary>

    <h4>1. Servo range</h4>
    <p class="hint">Tick Free. Move each servo to where the mechanics stop and set Min/Max just inside.
      Reverse if it moves the wrong way.</p>
    <h4 class="servo">Servo 1 <small>left/right</small> <span class="val"><span id="v1">90</span>&deg;</span> <span class="tag" id="r1"></span></h4>
    <input type="range" id="s1" min="0" max="180" value="90">
    <div class="cfg">
      <label><input type="checkbox" id="inv1"> Reverse</label>
      <label>Min <input type="number" id="min1" min="0" max="180"></label>
      <label>Max <input type="number" id="max1" min="0" max="180"></label>
    </div>
    <h4 class="servo">Servo 2 <small>up/down</small> <span class="val"><span id="v2">90</span>&deg;</span> <span class="tag" id="r2"></span></h4>
    <input type="range" id="s2" min="0" max="180" value="90">
    <div class="cfg">
      <label><input type="checkbox" id="inv2"> Reverse</label>
      <label>Min <input type="number" id="min2" min="0" max="180"></label>
      <label>Max <input type="number" id="max2" min="0" max="180"></label>
    </div>

    <h4>2. Lids</h4>
    <div class="cfg">
      <label><input type="checkbox" id="lids"> This painting has lids</label>
    </div>
    <div class="sets" id="setbtns">
      <button data-set="0">Main</button>
      <button data-set="1">Lids</button>
    </div>
    <p class="hint" id="setshint">Main: steps 3 and 4. Lids: only step 3 (a fixed position, Min/Max don't apply).</p>

    <h4>3. Center <small>eye set: <span class="val" id="setname">Main</span></small></h4>
    <p class="hint" id="centerhint">Move the eyes until this set sits centered.</p>
    <div class="cfg">
      <button class="small" id="here">Center = current</button>
      <label>X <input type="number" id="cx" min="0" max="180"></label>
      <label>Y <input type="number" id="cy" min="0" max="180"></label>
    </div>

    <div id="step4">
    <h4>4. Size</h4>
    <p class="hint">Move to the left or right edge &rarr; Radius X. To the top or bottom edge &rarr; Radius Y.</p>
    <div class="cfg">
      <button class="small" id="edgeX">Radius X = current</button>
      <label>X <input type="number" id="rx" min="0" max="90"></label>
    </div>
    <div class="cfg">
      <button class="small" id="edgeY">Radius Y = current</button>
      <label>Y <input type="number" id="ry" min="0" max="90"></label>
    </div>
    </div>

    <h4>5. Check</h4>
    <p class="hint">Untick Free: the joystick now stays inside the dashed circle.</p>

    <h4>6. Save</h4>
    <p class="hint">Press Save at the bottom.</p>

    <div class="adv">
      <h4>Idle</h4>
      <div class="cfg">
        <label><input type="checkbox" id="relax"> Relax servos when idle</label>
        <label>after <input type="number" id="relaxms" min="200" max="60000" step="100"> ms</label>
      </div>
      <p class="hint">Stops the pulses when nothing moves: no buzzing, no current. The next move wakes them.</p>
    </div>
  </details>

  <details class="card" id="c-beh">
    <summary>Behavior <small>on its own</small></summary>
    <div class="cfg">
      <label><input type="checkbox" id="bon"> Autonomous</label>
    </div>
    <table class="acts" id="acts"></table>
    <div class="cfg" id="sleeprow">
      <label>Sleep after <input type="number" id="awake" min="0" max="3600"> s</label>
      <label>for <input type="number" id="sleep" min="1" max="3600"> s</label>
      <button class="small" id="sleepnow">Sleep now</button>
      <button class="small" id="wakenow">Wake up</button>
    </div>
    <p class="hint">Only while nobody is watched. Each time one action by chance, the rest = nothing.
      Sleep after 0 = never. Lid actions and sleep need lids</p>
    <details class="sub adv">
      <summary>Timing</summary>
      <div class="cfg">
        <label>Every <input type="number" id="ivmin" min="1" max="600"> to
               <input type="number" id="ivmax" min="1" max="600"> s</label>
      </div>
      <div class="cfg">
        <label>Max speed <input type="number" id="spd" min="10" max="600"> &deg;/s</label>
      </div>
      <div class="cfg">
        <label>Glance at least <input type="number" id="glmin" min="0" max="100"> % off-center</label>
      </div>
      <div class="cfg">
        <label>Glance hold <input type="number" id="hmin" min="0" max="20" step="0.1"> to
               <input type="number" id="hmax" min="0" max="20" step="0.1"> s</label>
      </div>
    </details>
  </details>

  <details class="card" id="c-vis">
    <summary>Vision behavior <small>reacting to people</small></summary>
    <div class="cfg">
      <label><input type="checkbox" id="won"> Active</label>
      <label><input type="checkbox" id="wmulti"> Switch between people</label>
      <label><input type="checkbox" id="wwake"> Wake up when someone comes</label>
    </div>
    <p class="hint">Active: the camera steers the eyes, overrides other behavior.</p>
    <table class="acts" id="vacts"></table>
    <details class="sub adv">
      <summary>Aim</summary>
      <div class="cfg">
        <label style="flex:1">Offset X <span class="val" id="woxv">0</span> %
          <input type="range" id="wox" min="-50" max="50" step="1"></label>
      </div>
      <div class="cfg">
        <label style="flex:1">Offset Y <span class="val" id="woyv">0</span> %
          <input type="range" id="woy" min="-50" max="50" step="1"></label>
      </div>
      <div class="cfg">
        <label style="flex:1">Gain X <span class="val" id="wgxv">1.0</span>&times;
          <input type="range" id="wgx" min="30" max="300" step="10"></label>
      </div>
      <div class="cfg">
        <label style="flex:1">Gain Y <span class="val" id="wgyv">1.0</span>&times;
          <input type="range" id="wgy" min="30" max="300" step="10"></label>
      </div>
      <p class="hint">Stand where people usually stand. Offset moves the eyes onto you (+ = right / up).
        Gain: how far they move when you move.</p>
    </details>
    <details class="sub adv">
      <summary>Timing</summary>
      <div class="cfg">
        <label>Ignore flickers shorter than <input type="number" id="wnot" min="0" max="10" step="0.1"> s</label>
      </div>
      <div class="cfg">
        <label>Delayed follow lags behind by <input type="number" id="wdms" min="0.1" max="10" step="0.1"> s</label>
      </div>
      <div class="cfg">
        <label>Standing still after <input type="number" id="wstill" min="1" max="60"> s</label>
      </div>
      <div class="cfg">
        <label>Close = face wider than <input type="number" id="wclose" min="2" max="60"> % of the view</label>
      </div>
      <div class="cfg">
        <label>Stare after you for <input type="number" id="wlost" min="0" max="60"> s</label>
      </div>
    </details>
    <details class="sub adv">
      <summary>Fine tuning</summary>
      <div class="cfg">
        <label>Follow speed <input type="number" id="wspd" min="10" max="600"> &deg;/s</label>
        <label>Smoothing <input type="number" id="wsm" min="0" max="2" step="0.05"> s</label>
      </div>
      <div class="cfg">
        <label>Deadband <input type="number" id="wdb" min="0" max="10"> &deg;</label>
      </div>
      <p class="hint">Speed: max °/s (600 = servo limit). Smoothing: 0 = snappy, 0.3 = smooth.
        Deadband: ignore wobble smaller than this.</p>
    </details>
  </details>

  <div class="savebar"><div>
    <button id="save">Save</button>
    <button id="reset">Defaults</button>
    <span id="status"></span>
    <label class="advtgl"><input type="checkbox" id="adv"> Advanced</label>
  </div></div>

<script>
  const $ = id => document.getElementById(id);
  const pad = $('pad'), knob = $('knob'), ell = $('ell');
  const NAMES = ['Main', 'Lids'];
  const ACTS = ['Glance', 'Look around', 'Blink', 'Slow blink', 'Drowsy', 'Twitch', 'Eye roll'];
  const ACT_DESC = ['looks somewhere, holds, looks back', 'slow sweep left and right', 'a quick blink',
                    'a long cat blink', 'lids droop, reopen', 'a few small nervous jumps', 'one slow circle'];
  const NEEDS = [0, 0, 1, 1, 1, 0, 0];                     // 1 = needs lids
  const VACTS = ['Fast follow', 'Delayed follow', 'Double take', 'Startle', 'Lose interest',
                 'Freeze', 'Shy', 'Twitch', 'Stare after', 'Follow you out'];
  const VACT_DESC = ['follows you right away', 'follows a moment behind',
                     'looks away and back; then follows', 'big jump past you; then follows',
                     'goes idle; follows again when you move',
                     'stops; looks at you when you look away', 'hides behind lids or looks away',
                     'big nervous jumps', 'stares where you were',
                     'follows your way out, then back'];
  const VEVENT = [0, 0, 0, 0, 1, 2, 2, 2, 3, 3];
  const EVENTS = ['When you arrive', 'When you stand still', 'When you come close and look', 'When you leave'];
  const OTHERWISE = ['otherwise: ignore you', 'otherwise: keep watching', 'otherwise: hold eye contact', 'otherwise: drift back'];
  const VACT_BASE = 20;
  // Aim sliders: id, key in cfg.wat, label text
  const AIM = [['wgx', 'gx', v => (v / 100).toFixed(1)], ['wgy', 'gy', v => (v / 100).toFixed(1)],
               ['wox', 'ox', v => (v > 0 ? '+' : '') + v], ['woy', 'oy', v => (v > 0 ? '+' : '') + v]];
  const DEFAULT_SETS = [{cx: 90, cy: 90, rx: 30, ry: 20}, {cx: 90, cy: 130, rx: 0, ry: 0}];
  const DEFAULT_BEH = { on: true, ivmin: 5, ivmax: 20, spd: 90, glmin: 40, hmin: 800, hmax: 2500,
                        awake: 120, sleep: 30, w: [35, 15, 25, 10, 5, 5, 5] };
  const DEFAULT_WAT = { on: true, multi: false, dms: 1800, spd: 120, db: 1, sm: 300, not: 300, still: 5,
                        close: 12, lost: 3, wake: true, ch: [40, 30, 15, 15, 70, 40, 30, 15, 60, 40],
                        gx: 100, gy: 100, ox: 0, oy: 0 };
  const defaults = () => ({ min1: 0, max1: 180, inv1: true, min2: 0, max2: 180, inv2: true,
                            sets: DEFAULT_SETS.map(s => ({...s})), lids: true,
                            relax: true, relaxms: 1500,
                            beh: {...DEFAULT_BEH, w: [...DEFAULT_BEH.w]},
                            wat: {...DEFAULT_WAT, ch: [...DEFAULT_WAT.ch]} });
  const cfg = defaults();
  let saved = JSON.stringify(cfg);
  let a1 = 90, a2 = 90, lx = 0, ly = 0, cur = 0, dragging = false;

  // slider / pad range: Min..Max for the Main eyes; the Lids position may lie outside it: 0..180
  const lo      = n => cur === 1 ? 0 : cfg['min' + n];
  const hi      = n => cur === 1 ? 180 : cfg['max' + n];
  const span    = n => Math.max(1, hi(n) - lo(n));
  const toPos   = (n, a) => cfg['inv' + n] ? lo(n) + hi(n) - a : a;
  const toAngle = (n, p) => cfg['inv' + n] ? lo(n) + hi(n) - p : p;
  const clampN  = (n, v) => Math.min(hi(n), Math.max(lo(n), v));
  const clamp180 = v => Math.min(180, Math.max(0, Math.round(v) || 0));
  const frac    = (n, a) => (toPos(n, a) - lo(n)) / span(n);   // angle -> 0..1 on the pad
  const free    = () => $('free').checked;
  const net = ok => { $('dot').className = ok ? 'ok' : 'bad'; return ok; };
  const set = () => cfg.sets[cur];
  // don't overwrite a field while it's being typed in
  const put = (id, v) => { if (document.activeElement !== $(id)) $(id).value = v; };
  const num = (id, lo, hi, scale = 1) => Math.round(Math.min(hi, Math.max(lo, +$(id).value || 0)) * scale);

  // look direction limited to the unit circle (= the set's ellipse)
  function limited() { const r = Math.hypot(lx, ly); return r > 1 ? [lx / r, ly / r] : [lx, ly]; }

  // ---- action tables ----
  $('acts').innerHTML = '<tr class="grp"><td>Each time <small>· otherwise: nothing</small></td><td>Chance</td><td></td></tr>' + ACTS.map((n, i) =>
    `<tr id="act${i}"><td>${n}${NEEDS[i] ? ' <small>(lids)</small>' : ''}<span class="d">${ACT_DESC[i]}</span></td>` +
    `<td><input type="number" id="w${i}" min="0" max="100"> %</td>` +
    `<td><button class="small" data-act="${i}">Run</button></td></tr>`).join('');
  $('vacts').innerHTML = EVENTS.map((ev, e) =>
    `<tr class="grp"><td>${ev} <small>· ${OTHERWISE[e]}</small></td><td>${e ? '' : 'Chance'}</td><td></td></tr>` +
    VACTS.map((n, i) => VEVENT[i] !== e ? '' :
      `<tr id="vact${i}"><td>${n}<span class="d">${VACT_DESC[i]}</span></td>` +
      `<td><input type="number" id="c${i}" min="0" max="100"> %</td>` +
      `<td><button class="small" data-act="${VACT_BASE + i}">Run</button></td></tr>`).join('')).join('');

  function render() {
    [1, 2].forEach(n => {
      const a = n === 1 ? a1 : a2;
      const s = $('s' + n);
      s.min = lo(n); s.max = hi(n); s.value = toPos(n, a);
      $('v' + n).textContent = Math.round(a);
      $('r' + n).textContent = cfg['inv' + n] ? 'REVERSED' : '';
      $('inv' + n).checked = cfg['inv' + n];
      put('min' + n, cfg['min' + n]);
      put('max' + n, cfg['max' + n]);
    });
    if (free()) {
      // pad = full servo range, no limit shown
      knob.style.left = (frac(1, a1) * 100) + '%';
      knob.style.top  = ((1 - frac(2, a2)) * 100) + '%';
      ell.style.display = 'none';
    } else {
      // pad = look direction; dashed circle = the set's ellipse
      const [x, y] = limited();
      knob.style.left = ((x + 1) / 2 * 100) + '%';
      knob.style.top  = ((1 - y) / 2 * 100) + '%';
      ell.style.display = cur === 1 ? 'none' : '';      // Lids: a fixed position, no ellipse
      ell.style.left = ell.style.top = '0'; ell.style.width = ell.style.height = '100%';
    }
    $('lids').checked = cfg.lids;
    $('setbtns').style.display = $('setshint').style.display = cfg.lids ? '' : 'none';
    document.querySelectorAll('[data-set]').forEach(b => b.classList.toggle('on', +b.dataset.set === cur));
    $('setname').textContent = NAMES[cur];
    $('step4').style.display = cur === 1 ? 'none' : '';  // Lids have no size
    $('centerhint').textContent = cur === 1 ? 'Move the eyes until the lids cover them.'
                                            : 'Move the eyes until this set sits centered.';
    ['cx', 'cy', 'rx', 'ry'].forEach(k => put(k, set()[k]));
    $('relax').checked = cfg.relax;
    put('relaxms', cfg.relaxms);
    // behavior
    const b = cfg.beh;
    $('bon').checked = b.on;
    put('ivmin', b.ivmin); put('ivmax', b.ivmax); put('spd', b.spd); put('glmin', b.glmin);
    put('hmin', b.hmin / 1000); put('hmax', b.hmax / 1000); put('awake', b.awake); put('sleep', b.sleep);
    ACTS.forEach((n, i) => {
      put('w' + i, b.w[i]);
      const ok = !NEEDS[i] || cfg.lids;
      $('act' + i).className = ok ? '' : 'na';
      document.querySelector(`[data-act="${i}"]`).disabled = !ok;
    });
    $('sleeprow').style.opacity = cfg.lids ? '' : '0.35';
    $('sleepnow').disabled = $('wakenow').disabled = !cfg.lids;
    // vision behavior
    const w = cfg.wat;
    $('won').checked = w.on; $('wmulti').checked = w.multi;
    $('wwake').checked = w.wake; put('wdms', w.dms / 1000); put('wsm', w.sm / 1000);
    AIM.forEach(([id, k, f]) => { put(id, w[k]); $(id + 'v').textContent = f(w[k]); });
    put('wspd', w.spd); put('wdb', w.db); put('wnot', w.not / 1000); put('wstill', w.still);
    put('wclose', w.close); put('wlost', w.lost);
    VACTS.forEach((n, i) => put('c' + i, w.ch[i]));
    const unsaved = JSON.stringify(cfg) !== saved;
    $('status').textContent = unsaved ? 'Unsaved changes' : 'Saved';
    $('status').className = unsaved ? 'dirty' : '';
    $('save').classList.toggle('primary', unsaved);         // Save only stands out when needed
  }

  // ---- requests: strictly one after another, so replies can't overtake each other ----
  let chain = Promise.resolve(), moveUrl = null, moveQueued = false, pending = 0;
  // 2 s timeout: one lost request must not block everything behind it.
  // body given = POST (the settings are too long for a URL)
  const get = (url, body) => {
    pending++;
    const ctl = new AbortController(), t = setTimeout(() => ctl.abort(), 2000);
    const opt = body === undefined ? { signal: ctl.signal } : { method: 'POST', body, signal: ctl.signal };
    return fetch(url, opt).then(r => net(r.ok) ? r.json() : null)
      .catch(() => { net(false); return null; })
      .finally(() => { clearTimeout(t); pending--; });
  };
  function req(url, body) { const p = chain.then(() => get(url, body)); chain = p; return p; }
  // movements: only the newest one is sent; the reply holds the target angles
  function move(url) {
    moveUrl = url;
    if (moveQueued) return;
    moveQueued = true;
    chain = chain.then(() => { moveQueued = false; return get(moveUrl); })
                 .then(d => {
                   if (!d) return;
                   showStatus(d.st);
                   if (!moveQueued) { a1 = d.a1; a2 = d.a2; render(); }
                 });
  }

  function cfgQuery(save) {
    let q = `min1=${cfg.min1}&max1=${cfg.max1}&inv1=${cfg.inv1 ? 1 : 0}` +
            `&min2=${cfg.min2}&max2=${cfg.max2}&inv2=${cfg.inv2 ? 1 : 0}&save=${save ? 1 : 0}`;
    cfg.sets.forEach((s, i) => q += `&s${i}cx=${s.cx}&s${i}cy=${s.cy}&s${i}rx=${s.rx}&s${i}ry=${s.ry}`);
    q += `&s1on=${cfg.lids ? 1 : 0}&relax=${cfg.relax ? 1 : 0}&relaxms=${cfg.relaxms}`;
    const b = cfg.beh;
    q += `&bon=${b.on ? 1 : 0}&ivmin=${b.ivmin}&ivmax=${b.ivmax}&spd=${b.spd}&glmin=${b.glmin}` +
         `&hmin=${b.hmin}&hmax=${b.hmax}&awake=${b.awake}&sleep=${b.sleep}`;
    b.w.forEach((w, i) => q += `&w${i}=${w}`);
    const w = cfg.wat;
    q += `&won=${w.on ? 1 : 0}&wmulti=${w.multi ? 1 : 0}` +
         `&wdms=${w.dms}&wspd=${w.spd}&wdb=${w.db}&wsm=${w.sm}` +
         `&wnot=${w.not}&wstill=${w.still}&wclose=${w.close}&wlost=${w.lost}&wwake=${w.wake ? 1 : 0}`;
    w.ch.forEach((c, i) => q += `&c${i}=${c}`);
    q += `&wgx=${w.gx}&wgy=${w.gy}&wox=${w.ox}&woy=${w.oy}`;
    return q;
  }
  function sendCfg(save) {
    return req('/cfg', cfgQuery(save)).then(d => {
      if (d) { a1 = d.a1; a2 = d.a2; showStatus(d.st); render(); }
      return !!d;
    });
  }
  function applyCfg() { render(); sendCfg(false); }

  // angles -> look direction in the selected set (same as the board), so the
  // joystick knob follows the sliders
  function lookFrom(a, b) {
    if (cur === 1) return;                      // Lids: fixed position, no look direction
    const s = set();
    let x = s.rx ? (a - s.cx) / s.rx : 0, y = s.ry ? (b - s.cy) / s.ry : 0;
    if (cfg.inv1) x = -x;
    if (cfg.inv2) y = -y;
    const r = Math.hypot(x, y);
    if (r > 1) { x /= r; y /= r; }
    lx = x; ly = y;
  }
  // absolute angles (sliders, free joystick)
  function setAngles(x, y) {
    touchedAt = performance.now();
    a1 = clampN(1, x); a2 = clampN(2, y);
    lookFrom(a1, a2);
    render();
    move(`/both?a=${Math.round(a1)}&b=${Math.round(a2)}&n=${cur}`);
  }
  // look direction (normal joystick)
  function setLook(x, y) {
    touchedAt = performance.now();
    lx = x; ly = y;
    render();
    move(`/look?x=${lx.toFixed(3)}&y=${ly.toFixed(3)}&n=${cur}`);
  }

  // ---- live status ----
  // Live position from the board. Not while the user is moving something (the
  // servos lag behind the target, which would pull a slider back under the finger).
  // 'cur' stays the set selected on the page, so calibration never jumps sets.
  let touchedAt = 0;
  const touched = () => performance.now() - touchedAt < 1500;
  function showStatus(st, s) {
    $('bstat').textContent = st + (s ? ` (${NAMES[s]})` : '');
    $('resume').hidden = !st.startsWith('paused');
  }
  function camText(d) {
    if (d.bg < 2) return 'camera: ' + (d.bg === 0 ? 'settling exposure…' :
                         d.wait ? 'waiting for the room to be still…' : 'learning the empty room…');
    let t = d.src === 1 ? '<span class="face">FACE</span>' : d.src === 2 ? '<span class="head">HEAD</span>' : 'nobody';
    if (d.people > 1) t += ` · ${d.people} people`;
    // the rest only with Advanced
    let a = '';
    if (d.src) a += ` · ${d.x.toFixed(2)}, ${d.y.toFixed(2)}`;
    if (d.src === 1) a += ` · face ${Math.round(d.size * 100)} %`;     // compare with "Close = face wider than"
    const tc = d.temp >= 75 ? 'var(--bad)' : d.temp >= 60 ? 'var(--warn)' : 'inherit';
    a += ` · ${d.fps.toFixed(1)} fps` + (d.det ? ` · face ${d.det} ms` : '') +
         (d.temp > 0 ? ` · <span style="color:${tc}">${d.temp.toFixed(0)} °C</span>` : '');
    return t + `<span class="adv">${a}</span>`;
  }
  function applyStatus(d) {
    if (!d) return;
    showStatus(d.st, d.set);
    if (d.fps !== undefined) $('tstat').innerHTML = camText(d);
    if (d.tm) $('ttime').textContent = `ms per frame: camera ${d.tm[0]} · convert ${d.tm[1]} · room ${d.tm[2]}` +
                                       ` · face ${d.tm[3]} · stream ${d.tm[4]}`;
    if (d.ftest !== undefined && document.activeElement !== $('ftest')) $('ftest').checked = !!d.ftest;
    if (dragging || moveQueued || touched()) return;
    a1 = d.a1; a2 = d.a2;
    if (!free()) { lx = d.lx; ly = d.ly; }
    render();
  }
  // while Free is ticked, ask the board to keep the behavior paused (calibrating)
  setInterval(() => {
    // no polling while steering, so moves never wait behind a status request
    if (!pending && !dragging && !moveQueued && !touched()) req('/status' + (free() ? '?hold=1' : '')).then(applyStatus);
  }, 600);

  // ---- joystick ----
  function fromPointer(e) {
    const r = pad.getBoundingClientRect();
    const fx = Math.min(1, Math.max(0, (e.clientX - r.left) / r.width));
    const fy = Math.min(1, Math.max(0, (e.clientY - r.top) / r.height));
    if (free()) {
      const p1 = Math.round(lo(1) + fx * (hi(1) - lo(1)));
      const p2 = Math.round(lo(2) + (1 - fy) * (hi(2) - lo(2)));  // up = higher
      setAngles(toAngle(1, p1), toAngle(2, p2));
    } else {
      setLook(fx * 2 - 1, 1 - fy * 2);
    }
  }
  function center() {
    if (free()) setAngles(Math.round((lo(1) + hi(1)) / 2), Math.round((lo(2) + hi(2)) / 2));
    else setLook(0, 0);
  }
  pad.addEventListener('pointerdown', e => { dragging = true; pad.setPointerCapture(e.pointerId); fromPointer(e); });
  pad.addEventListener('pointermove', e => { if (dragging) fromPointer(e); });
  const endDrag = () => { if (!dragging) return; dragging = false; if ($('spring').checked) center(); };
  pad.addEventListener('pointerup', endDrag);
  pad.addEventListener('pointercancel', endDrag);
  $('center').addEventListener('click', center);
  $('free').addEventListener('change', () => {
    if (!free()) {                                   // calibration done: back to look mode
      setLook(lx, ly);
      req('/resume').then(applyStatus);              // also resumes following people
    } else {
      render(); req('/status?hold=1').then(applyStatus);   // pause the behavior right away
    }
  });
  $('resume').addEventListener('click', () => req('/resume').then(applyStatus));

  // ---- sliders ----
  $('s1').addEventListener('input', e => setAngles(toAngle(1, +e.target.value), a2));
  $('s2').addEventListener('input', e => setAngles(a1, toAngle(2, +e.target.value)));

  // ---- eye sets ----
  document.querySelectorAll('[data-set]').forEach(b => b.addEventListener('click', () => {
    cur = +b.dataset.set;
    if (free()) { lx = 0; ly = 0; }          // calibrating: go to the set's center
    render();
    move(`/set?n=${cur}&x=${lx.toFixed(3)}&y=${ly.toFixed(3)}`);
  }));
  ['cx', 'cy'].forEach(k => $(k).addEventListener('change', e => { set()[k] = clamp180(e.target.value); applyCfg(); }));
  ['rx', 'ry'].forEach(k => $(k).addEventListener('change', e => { set()[k] = Math.min(90, clamp180(e.target.value)); applyCfg(); }));
  $('here').addEventListener('click',  () => { set().cx = Math.round(a1); set().cy = Math.round(a2); applyCfg(); });
  $('edgeX').addEventListener('click', () => { set().rx = Math.min(90, Math.round(Math.abs(a1 - set().cx))); applyCfg(); });
  $('edgeY').addEventListener('click', () => { set().ry = Math.min(90, Math.round(Math.abs(a2 - set().cy))); applyCfg(); });
  $('lids').addEventListener('change', e => { cfg.lids = e.target.checked; if (!cfg.lids) cur = 0; applyCfg(); });

  // ---- behavior ----
  $('bon').addEventListener('change', e => {
    cfg.beh.on = e.target.checked;
    if (cfg.beh.on && free()) $('free').checked = false;   // Free would keep it paused
    applyCfg();
    if (cfg.beh.on) req('/resume').then(applyStatus);      // start now, even after manual control
  });
  [['ivmin', 'ivmin', 1, 600], ['ivmax', 'ivmax', 1, 600], ['spd', 'spd', 10, 600], ['glmin', 'glmin', 0, 100],
   ['hmin', 'hmin', 0, 20, 1000], ['hmax', 'hmax', 0, 20, 1000], ['awake', 'awake', 0, 3600], ['sleep', 'sleep', 1, 3600]]
    .forEach(([id, k, lo, hi, sc]) => $(id).addEventListener('change', () => {
      cfg.beh[k] = num(id, lo, hi, sc || 1);
      if (cfg.beh.ivmax < cfg.beh.ivmin) cfg.beh.ivmax = cfg.beh.ivmin;
      if (cfg.beh.hmax < cfg.beh.hmin) cfg.beh.hmax = cfg.beh.hmin;
      applyCfg();
    }));
  ACTS.forEach((n, i) => $('w' + i).addEventListener('change', () => { cfg.beh.w[i] = num('w' + i, 0, 100); applyCfg(); }));
  document.querySelectorAll('[data-act]').forEach(b => b.addEventListener('click', () =>
    req(`/act?n=${b.dataset.act}`).then(applyStatus)));
  $('sleepnow').addEventListener('click', () => req('/act?n=100').then(applyStatus));
  $('wakenow').addEventListener('click',  () => req('/act?n=101').then(applyStatus));

  // ---- vision behavior ----
  $('won').addEventListener('change', e => { cfg.wat.on = e.target.checked; applyCfg(); });
  $('wwake').addEventListener('change', e => { cfg.wat.wake = e.target.checked; applyCfg(); });
  $('wmulti').addEventListener('change', e => { cfg.wat.multi = e.target.checked; applyCfg(); });
  AIM.forEach(([id, k, f]) => {
    $(id).addEventListener('input', e => { $(id + 'v').textContent = f(+e.target.value); });
    $(id).addEventListener('change', e => { cfg.wat[k] = +e.target.value; applyCfg(); });
  });
  $('ftest').addEventListener('change', e => req('/test?follow=' + (e.target.checked ? 1 : 0)).then(applyStatus));
  [['wspd', 'spd', 10, 600], ['wdb', 'db', 0, 10], ['wnot', 'not', 0, 10, 1000], ['wstill', 'still', 1, 60],
   ['wdms', 'dms', 0.1, 10, 1000], ['wsm', 'sm', 0, 2, 1000],
   ['wclose', 'close', 2, 60], ['wlost', 'lost', 0, 60]]
    .forEach(([id, k, lo, hi, sc]) => $(id).addEventListener('change', () => { cfg.wat[k] = num(id, lo, hi, sc || 1); applyCfg(); }));
  VACTS.forEach((n, i) => $('c' + i).addEventListener('change', () => { cfg.wat.ch[i] = num('c' + i, 0, 100); applyCfg(); }));

  // ---- idle ----
  $('relax').addEventListener('change', e => { cfg.relax = e.target.checked; applyCfg(); });
  $('relaxms').addEventListener('change', e => {
    cfg.relaxms = Math.min(60000, Math.max(200, Math.round(+e.target.value) || 1500));
    applyCfg();
  });

  // ---- axis settings ----
  [1, 2].forEach(n => {
    $('inv' + n).addEventListener('change', e => { cfg['inv' + n] = e.target.checked; applyCfg(); });
    $('min' + n).addEventListener('change', e => {
      const v = clamp180(e.target.value);
      cfg['min' + n] = v; if (v > cfg['max' + n]) cfg['max' + n] = v;
      applyCfg();
    });
    $('max' + n).addEventListener('change', e => {
      const v = clamp180(e.target.value);
      cfg['max' + n] = v; if (v < cfg['min' + n]) cfg['min' + n] = v;
      applyCfg();
    });
  });

  $('reset').addEventListener('click', () => {
    Object.assign(cfg, defaults());
    if (!cfg.lids) cur = 0;
    applyCfg();
  });
  $('save').addEventListener('click', () => {
    sendCfg(true).then(ok => {
      if (ok) { saved = JSON.stringify(cfg); render(); }
      else { $('status').textContent = 'Save failed'; $('status').className = 'dirty'; }
    });
  });

  // ---- camera settings (saved on the board right away) ----
  // [key, label, group (b basic, a advanced, d detection, e exposure), type, ...]
  const CF = [
    ['mode', 'Mode', 'b', 'sel', [[0, 'Silhouette + face'], [1, 'Silhouette only'], [2, 'Faces only (slow)']]],
    ['fs', 'Frame size', 'a', 'sel', [[0, 'QVGA 320×240'], [1, 'VGA 640×480']]],
    ['gray', 'Image (reboots)', 'a', 'sel', [[1, 'Grayscale'], [0, 'Color']]],
    ['ae', 'Exposure level', 'b', 'num', -2, 2, 1],
    ['lock', 'Lock exposure', 'a', 'chk'],
    ['mask', 'Show foreground', 'b', 'chk'],
    ['crop', 'Show detector view', 'a', 'chk'],
    ['fevery', 'Face check every (frames)', 'a', 'num', 1, 10, 1],
    ['fgthr', 'Foreground threshold', 'd', 'num', 5, 80, 1],
    ['minblob', 'Min person size (cells)', 'd', 'num', 10, 1000, 5],
    ['minh', 'Min person height (cells)', 'd', 'num', 2, 50, 1],
    ['still', 'Still → object after (s)', 'd', 'num', 2, 120, 1],
    ['stillabs', 'Object fades in (s)', 'd', 'num', 1, 60, 1],
    ['mthr', 'Movement threshold', 'd', 'num', 2, 40, 1],
    ['absorb', 'Person fades in (s)', 'd', 'num', 5, 900, 5],
    ['boost', 'Face contrast boost', 'd', 'chk'],
    ['roi', 'Face search size (px)', 'd', 'num', 96, 240, 16],
    ['s1scale', 'Stage 1 resize scale', 'd', 'num', 0.1, 1, 0.05],
    ['s1score', 'Stage 1 score', 'd', 'num', 0.05, 0.9, 0.05],
    ['s2score', 'Stage 2 score', 'd', 'num', 0.1, 0.9, 0.05],
    ['confirm', 'Confirm frames', 'd', 'num', 1, 5, 1],
    ['hold', 'Hold frames', 'd', 'num', 0, 30, 1],
    ['gain', 'Gain ceiling', 'e', 'sel', [[0, '2×'], [1, '4×'], [2, '8×'], [3, '16×'], [4, '32×'], [5, '64×'], [6, '128×']]],
    ['aec2', 'Night exposure (blurrier)', 'e', 'chk'],
    ['mexp', 'Manual exposure (0 = auto)', 'e', 'num', 0, 1200, 10],
    ['mgain', 'Manual gain (0–30)', 'e', 'num', 0, 30, 1],
    ['bright', 'Brightness', 'e', 'num', -2, 2, 1],
    ['contrast', 'Contrast', 'e', 'num', -2, 2, 1],
    ['vflip', 'Flip vertical', 'e', 'chk'],
    ['hmirror', 'Mirror image', 'e', 'chk']];
  for (const [k, label, g, type, a, b, c] of CF) {
    const l = document.createElement('label'); l.textContent = label; l.htmlFor = 'cf_' + k;
    let el;
    if (type === 'sel') {
      el = document.createElement('select');
      for (const [v, t] of a) { const o = document.createElement('option'); o.value = v; o.textContent = t; el.appendChild(o); }
    } else if (type === 'chk') { el = document.createElement('input'); el.type = 'checkbox'; }
    else { el = document.createElement('input'); el.type = 'number'; el.min = a; el.max = b; el.step = c; }
    el.id = 'cf_' + k;
    el.onchange = () => {
      const v = el.type === 'checkbox' ? (el.checked ? 1 : 0) : el.value;
      req('/cam/set?' + k + '=' + encodeURIComponent(v)).then(fillCam);
    };
    $('cam-' + g).appendChild(l); $('cam-' + g).appendChild(el);
  }
  function fillCam(s) {
    if (!s) return;
    for (const k in s) {
      const el = $('cf_' + k);
      if (!el) continue;
      if (el.type === 'checkbox') el.checked = !!s[k]; else el.value = s[k];
    }
  }
  $('relearn').addEventListener('click', () => req('/cam/relearn'));
  $('relearn2').addEventListener('click', () => req('/cam/relearn'));

  // the stream (and the board's work to annotate + encode it) only runs while Camera is open
  const camCard = $('c-cam'), camImg = $('cam');
  const startStream = () => { camImg.src = 'http://' + location.hostname + ':81/stream?' + Date.now(); };
  const camToggle = () => {
    if (!camCard.open) camImg.removeAttribute('src');
    else if (!camImg.getAttribute('src')) startStream();
  };
  camCard.addEventListener('toggle', camToggle);
  // the board ends the stream after 5 s without frames (reboot, camera restart, WiFi blip): reconnect
  camImg.onerror = () => { camImg.removeAttribute('src'); setTimeout(camToggle, 2000); };

  // remember which sections are open
  document.querySelectorAll('details[id]').forEach(d => {
    try { const v = localStorage.getItem('open_' + d.id); if (v !== null) d.open = v === '1'; } catch (e) {}
    d.addEventListener('toggle', () => { try { localStorage.setItem('open_' + d.id, d.open ? '1' : '0'); } catch (e) {} });
  });
  camToggle();                                       // Camera open at load: start the stream

  // Advanced: shows the fine settings (remembered in this browser)
  const showAdv = on => document.body.classList.toggle('advanced', on);
  try { $('adv').checked = localStorage.getItem('advanced') === '1'; } catch (e) {}
  showAdv($('adv').checked);
  $('adv').addEventListener('change', e => {
    showAdv(e.target.checked);
    try { localStorage.setItem('advanced', e.target.checked ? '1' : '0'); } catch (e2) {}
  });

  render();
  req('/state').then(d => {
    if (!d) return;
    cfg.min1 = d.min1; cfg.max1 = d.max1; cfg.inv1 = !!d.inv1;
    cfg.min2 = d.min2; cfg.max2 = d.max2; cfg.inv2 = !!d.inv2;
    cfg.sets = d.sets.map(s => ({ cx: s[0], cy: s[1], rx: s[2], ry: s[3] }));
    cfg.lids = !!d.lids;
    cfg.relax = !!d.relax; cfg.relaxms = d.relaxms;
    cfg.beh = { on: !!d.beh.on, ivmin: d.beh.ivmin, ivmax: d.beh.ivmax, spd: d.beh.spd, glmin: d.beh.glmin,
                hmin: d.beh.hmin, hmax: d.beh.hmax, awake: d.beh.awake, sleep: d.beh.sleep, w: d.beh.w };
    cfg.wat = { on: !!d.wat.on, multi: !!d.wat.multi, dms: d.wat.dms,
                spd: d.wat.spd, db: d.wat.db, sm: d.wat.sm,
                not: d.wat.not, still: d.wat.still, close: d.wat.close, lost: d.wat.lost,
                wake: !!d.wat.wake, ch: d.wat.ch,
                gx: d.wat.gx, gy: d.wat.gy, ox: d.wat.ox, oy: d.wat.oy };
    $('ftest').checked = !!d.ftest;
    a1 = d.a1; a2 = d.a2; cur = d.set; lx = d.lx; ly = d.ly;
    showStatus(d.st);
    saved = JSON.stringify(cfg);
    render();
  });
  req('/cam/settings').then(fillCam);
</script>
</body>
</html>
)rawliteral";
