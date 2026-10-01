// =============================================================================
//  page.h  -  Baseball Timer web page
// =============================================================================
// This file holds the web page that the ESP32 sends to the phone. It lives in
// its own file (a separate tab in the Arduino IDE) because the Arduino IDE
// scans .ino files for functions, and it mistakes the JavaScript functions in
// the page for C++ code, which causes compile errors. .h files aren't scanned.
//
// Keep this file in the same folder as baseball_timer.ino.
//
// This is the complete page sent to the phone's browser. It's stored in flash
// (PROGMEM) as one big text string. Everything between R"rawliteral( and
// )rawliteral" is sent exactly as written.
//
// How the page talks to the ESP32:
//   GET  /state   -> current settings (used once when the page loads)
//   GET  /level   -> live status: mic level, timer state, etc. (every 100 ms)
//   POST /set?k=<setting>&v=<value>  -> change a setting (see handleSet())
//   POST /arm?on=1 or 0              -> arm / disarm
//   POST /cancel                     -> cancel a running countdown
//   POST /test                       -> play the selected end sound
//
// To add a new setting to the page:
//   1. Add an input here with onchange="set('mykey', this.value)"
//   2. Fill it in from the /state data in init() in the <script> below
//   3. In baseball_timer.ino: add a variable in section 5, load it in loadSettings(),
//      handle 'mykey' in handleSet(), and include it in sendState()

#pragma once
#include <Arduino.h>

const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Baseball Timer</title>
  <style>
    /* ---- General look ---- */
    body { font-family: -apple-system, sans-serif; text-align: center;
           background: #111; color: #eee; margin: 20px 12px; }
    h2 { margin: 26px 0 8px; font-weight: 500; color: #aaa; font-size: 1.05em; }

    /* ---- Big status panel; its color changes with the device state ---- */
    #panel { max-width: 420px; margin: 0 auto; padding: 22px; border-radius: 16px;
             background: #1e2a3a; transition: background .2s; }   /* listening */
    #panel.counting { background: #1f5130; }                     /* counting down */
    #panel.done     { background: #7a5a00; }                     /* time's up */
    #panel.disarmed { background: #262626; }                     /* disarmed */
    #label { color: #bbb; font-size: 1.1em; }
    #big   { font-size: 4.2em; font-weight: 700; font-variant-numeric: tabular-nums; }
    #cancel { display: none; }

    /* ---- Arm / disarm button ---- */
    #arm { display: block; width: 100%; max-width: 464px; margin: 0 auto 14px;
           font-size: 1.4em; font-weight: 600; padding: 18px; }
    #arm.armed    { background: #2e9d4f; }
    #arm.disarmed { background: #555; }

    /* ---- Controls ---- */
    .row { margin: 10px 0; font-size: 1.05em; }
    input[type=number] { width: 90px; font-size: 1.2em; padding: 6px; border-radius: 8px;
                         border: none; text-align: center; }
    select { font-size: 1em; padding: 6px; border-radius: 8px; }
    input[type=range] { width: 80%; max-width: 320px; }
    button { font-size: 1.05em; padding: 10px 18px; margin: 4px; border: none;
             border-radius: 10px; color: #fff; background: #3a5fcd; cursor: pointer; }
    button.red { background: #c62828; }

    /* ---- Mic level graph ---- */
    canvas { width: 100%; max-width: 600px; background: #1b1b1b; border-radius: 10px; }
    .legend { font-size: .8em; color: #aaa; margin: 4px 0; }
    .legend span { margin: 0 6px; }
    .small { color: #888; font-size: .9em; }
  </style>
</head>
<body>
  <h1>&#9918; Baseball Timer</h1>

  <!-- Arm / disarm toggle -->
  <button id="arm" class="disarmed" onclick="toggleArm()">...</button>

  <!-- Status panel: shows Disarmed / Listening / countdown / Time's up -->
  <div id="panel">
    <div id="label">Listening for a hit...</div>
    <div id="big">--</div>
    <button id="cancel" class="red" onclick="post('/cancel')">Cancel</button>
  </div>
  <p class="small">Hits detected: <span id="hits">0</span></p>

  <!-- Timer length (setting key: dur) and start chirp (setting key: beep) -->
  <h2>Timer</h2>
  <div class="row">
    <input type="number" id="dur" min="0.1" max="3600" step="0.1" value="5"
           onchange="set('dur', this.value)"> seconds
  </div>
  <div class="row">
    <label><input type="checkbox" id="beep" onchange="set('beep', this.checked ? 1 : 0)">
      Short beep when timer starts</label>
  </div>

  <!-- Tick mode (key: ticks; values match the TICKS_ enum)
       and tick interval (key: tickint; value in milliseconds) -->
  <h2>Countdown ticks</h2>
  <div class="row">
    <select id="ticks" onchange="set('ticks', this.value)">
      <option value="0">Off</option>
      <option value="1">Whole countdown</option>
      <option value="2">Last 3 seconds</option>
      <option value="3">Last 5 seconds</option>
    </select>
    every
    <select id="tickint" onchange="set('tickint', this.value)">
      <option value="150">0.15 s</option>
      <option value="250">0.25 s</option>
      <option value="500">0.5 s</option>
      <option value="1000">1 s</option>
    </select>
  </div>

  <!-- End sound (key: endsnd; values match the SND_ enum) -->
  <h2>End sound</h2>
  <div class="row">
    <select id="endsnd" onchange="set('endsnd', this.value)">
      <option value="6">"Go" tone (high)</option>
      <option value="3">Triple beep</option>
      <option value="4">Ding-dong</option>
      <option value="5">Long tone</option>
      <option value="2">Single beep</option>
    </select>
    <button onclick="post('/test')">Test</button>
  </div>

  <!-- LED options (keys: redhold, bright) -->
  <h2>Lights</h2>
  <div class="row">
    Red light after timer ends:
    <select id="redhold" onchange="set('redhold', this.value)">
      <option value="0">Until next hit</option>
      <option value="1">3 seconds</option>
    </select>
  </div>
  <div class="row">Brightness: <span id="brightv">60</span>%</div>
  <input type="range" id="bright" min="10" max="100"
         oninput="brightv.textContent=this.value" onchange="set('bright', this.value)">

  <!-- Power-up behavior (key: armboot) -->
  <h2>When powered on</h2>
  <div class="row">
    <select id="armboot" onchange="set('armboot', this.value)">
      <option value="0">Start disarmed</option>
      <option value="1">Start armed</option>
    </select>
  </div>

  <!-- Volume (key: vol) and sensitivity (key: sens).
       oninput updates the number while dragging; onchange sends it when released. -->
  <h2>Volume: <span id="volv">50</span>%</h2>
  <input type="range" id="vol" min="0" max="100"
         oninput="volv.textContent=this.value" onchange="set('vol', this.value)">

  <h2>Sensitivity: <span id="sensv">50</span></h2>
  <input type="range" id="sens" min="0" max="100"
         oninput="sensv.textContent=this.value" onchange="set('sens', this.value)">

  <!-- Live mic graph, for tuning sensitivity -->
  <h2>Mic level</h2>
  <canvas id="graph" width="600" height="180"></canvas>
  <div class="legend">
    <span style="color:#4fc3f7">&#9632; level</span>
    <span style="color:#888">&#9632; background</span>
    <span style="color:#ff5252">&#9632; trigger line</span>
  </div>

<script>
  // Shortcut: $('id') finds the page element with that id
  const $ = id => document.getElementById(id);

  const c = $('graph'), g = c.getContext('2d');   // graph canvas and its drawing tool
  const hist = [], N = 100;       // last N status readings (100 x 100 ms = 10 s of graph)
  let lastDone = null;            // last seen doneCount, to detect a finished countdown
  let doneUntil = 0;              // show "Time's up!" until this time
  let isArmed = false;            // what the arm button currently shows

  // Send a command to the ESP32 (no reply needed)
  function post(url) { fetch(url, { method: 'POST' }); }

  // Change a setting on the ESP32; it saves it to flash
  function set(k, v) { post('/set?k=' + k + '&v=' + encodeURIComponent(v)); }

  // Update the arm button's color and text
  function renderArm(a) {
    isArmed = a;
    $('arm').className = a ? 'armed' : 'disarmed';
    $('arm').textContent = a ? 'ARMED \u2014 tap to disarm' : 'DISARMED \u2014 tap to arm';
  }

  // Arm button tapped: update the button right away, then tell the ESP32
  function toggleArm() {
    renderArm(!isArmed);
    post('/arm?on=' + (isArmed ? 1 : 0));
  }

  // ---- Graph drawing ----
  // Converts a dB value (0 at top, -100 at bottom) to a vertical pixel position
  const y = db => c.height * (Math.min(0, Math.max(-100, db)) / -100);

  // Draws one line across the graph. fn picks which value to plot from each reading.
  function drawLine(fn, color, dash) {
    g.strokeStyle = color; g.lineWidth = 2; g.setLineDash(dash || []);
    g.beginPath();
    hist.forEach((d, i) => {
      const x = (i + N - hist.length) * c.width / (N - 1);
      i ? g.lineTo(x, y(fn(d))) : g.moveTo(x, y(fn(d)));
    });
    g.stroke(); g.setLineDash([]);
  }

  // Redraws the whole graph: grid, background, trigger line, live level
  function draw() {
    g.clearRect(0, 0, c.width, c.height);
    g.fillStyle = '#555'; g.font = '12px sans-serif'; g.strokeStyle = '#2a2a2a'; g.lineWidth = 1;
    for (let db = -20; db > -100; db -= 20) {
      g.beginPath(); g.moveTo(0, y(db)); g.lineTo(c.width, y(db)); g.stroke();
      g.fillText(db + ' dB', 4, y(db) - 3);
    }
    drawLine(d => d.bg, '#888');                     // background level
    drawLine(d => d.bg + d.thr, '#ff5252', [6, 4]);  // trigger line = background + threshold
    drawLine(d => d.lvl, '#4fc3f7');                 // live level
  }

  // ---- Status panel ----
  // d is one reading from /level (see handleLevel() in the sketch)
  function showStatus(d) {
    const panel = $('panel');

    // If the finished-countdown counter went up, show "Time's up!" for 2 seconds
    if (lastDone !== null && d.done > lastDone) doneUntil = Date.now() + 2000;
    lastDone = d.done;

    renderArm(d.armed);
    if (!d.armed) {
      panel.className = 'disarmed';
      $('label').textContent = 'Disarmed \u2014 not listening';
      $('big').textContent = parseFloat($('dur').value).toFixed(1);
      $('cancel').style.display = 'none';
    } else if (d.st === 1) {                    // 1 = COUNTING
      panel.className = 'counting';
      $('label').textContent = 'Counting down';
      $('big').textContent = (d.rem / 1000).toFixed(1);
      $('cancel').style.display = 'inline-block';
    } else if (Date.now() < doneUntil) {
      panel.className = 'done';
      $('label').textContent = "Time's up!";
      $('big').textContent = '0.0';
      $('cancel').style.display = 'none';
    } else {
      panel.className = '';
      $('label').textContent = 'Listening for a hit...';
      $('big').textContent = parseFloat($('dur').value).toFixed(1);
      $('cancel').style.display = 'none';
    }
    $('hits').textContent = d.hits;
  }

  // Asks the ESP32 for live status every 100 ms, forever.
  // Waits for each answer before asking again, so requests never pile up.
  async function poll() {
    try {
      const d = await (await fetch('/level')).json();
      hist.push(d); if (hist.length > N) hist.shift();
      draw();
      showStatus(d);
    } catch (e) {}          // ignore a missed reading; just try again
    setTimeout(poll, 100);
  }

  // Runs once when the page opens: load saved settings into the controls, then start polling
  async function init() {
    try {
      const s = await (await fetch('/state')).json();
      $('dur').value = s.dur;
      $('endsnd').value = s.endsnd;
      $('ticks').value = s.ticks;
      $('tickint').value = s.tickint;
      $('beep').checked = s.beep;
      $('vol').value = s.vol;   $('volv').textContent = s.vol;
      $('sens').value = s.sens; $('sensv').textContent = s.sens;
      $('armboot').value = s.armboot ? 1 : 0;
      $('redhold').value = s.redhold ? 1 : 0;
      $('bright').value = s.bright; $('brightv').textContent = s.bright;
    } catch (e) {}
    poll();
  }
  init();
</script>
</body>
</html>
)rawliteral";
