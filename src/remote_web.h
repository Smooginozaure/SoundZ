// Page web de la télécommande, servie au téléphone par remote.cpp : trois onglets (Musique, Voix, Soundboard),
// faits pour le doigt. Elle interroge SoundZ toutes les ~0,5 s (/api/state) et lui envoie les commandes (/api/cmd).
#pragma once

namespace remote {

constexpr char kPageHtml[] = R"SZPAGE(<!doctype html>
<html lang="fr">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no,viewport-fit=cover">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
<meta name="apple-mobile-web-app-title" content="SoundZ">
<meta name="theme-color" content="#1e1f22">
<meta name="format-detection" content="telephone=no">
<link rel="apple-touch-icon" href="/icon.png">
<link rel="icon" type="image/png" href="/icon.png">
<link rel="manifest" id="manifest" href="/manifest.webmanifest">
<title>SoundZ</title>
<style>
:root{--bg:#1e1f22;--bar:#232428;--surface:#2b2d31;--press:#3f4147;--btn:#383a40;--track:#4e5058;--text:#dbdee1;--text2:#b5bac1;
--muted:#949ba4;--faint:#80848e;--accent:#3e5068;--accent2:#4a5e79;--bright:#607999;--light:#a0b4cd;--soft:#323a46;
--danger:#f23f43;--amber:#e3b45f;--ok:#3ba55d}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
[hidden]{display:none!important}
html,body{margin:0;background:var(--bg);color:var(--text);font:16px/1.35 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,system-ui,sans-serif;
-webkit-text-size-adjust:100%}
body{padding-top:env(safe-area-inset-top);min-height:100vh;-webkit-user-select:none;user-select:none;-webkit-touch-callout:none;overscroll-behavior-y:none}
header{position:sticky;top:0;z-index:5;display:flex;align-items:center;gap:10px;padding:12px 16px 10px;background:var(--bg)}
.logo{width:28px;height:28px;border-radius:8px;background:var(--accent);color:#fff;display:grid;place-items:center;font-weight:700;font-size:12px}
header b{font-size:18px}
#conn{margin-left:auto;display:flex;align-items:center;gap:6px;font-size:13px;color:var(--muted)}
#conn i{width:8px;height:8px;border-radius:50%;background:var(--faint)}
#conn.on i{background:var(--ok)}#conn.off i{background:var(--danger)}
#banner{margin:0 16px 8px;padding:12px 14px;border-radius:12px;background:#3a2426;color:#ffc9ca;font-size:14px}
main{padding:0 16px calc(92px + env(safe-area-inset-bottom))}
.tab{display:none}.tab.show{display:block}
nav{position:fixed;left:0;right:0;bottom:0;z-index:6;display:flex;gap:6px;background:var(--bar);border-top:1px solid #2e3035;
padding:6px 8px calc(6px + env(safe-area-inset-bottom))}
nav button{flex:1;border:0;background:none;color:var(--muted);font-size:12px;padding:6px 0 5px;border-radius:12px;display:flex;flex-direction:column;align-items:center;gap:1px}
nav .ic{font-size:22px;line-height:26px;filter:grayscale(1);opacity:.65}
nav button.sel{background:var(--accent);color:#fff}nav button.sel .ic{filter:none;opacity:1}
button{font:inherit;color:var(--text);touch-action:manipulation}
.btn{border:0;border-radius:12px;background:var(--btn);padding:12px;min-height:50px;font-size:16px;font-weight:600}
.btn:active{background:var(--press)}
.btn.primary{background:var(--accent);color:#fff}.btn.primary:active{background:var(--accent2)}
.btn.stop{background:#47292c;color:#ffc2c4}.btn.stop:active{background:#5a3236}
.btn.wide{width:100%}
.row{display:flex;gap:10px;margin:12px 0}.row>*{flex:1;min-width:0}
.card{background:var(--surface);border-radius:16px;padding:14px 16px;margin:12px 0}
.center{text-align:center}
.status{margin:10px 2px;font-size:14px;color:var(--muted)}.status.err{color:var(--danger)}
h2{margin:22px 2px 8px;font-size:12px;letter-spacing:.7px;text-transform:uppercase;color:var(--faint)}
.lbl{font-size:14px;color:var(--text2);margin-bottom:2px}
.slider+.slider{margin-top:10px}
input[type=range]{-webkit-appearance:none;appearance:none;display:block;width:100%;height:38px;margin:0;background:transparent;--fill:var(--track)}
input[type=range]::-webkit-slider-runnable-track{height:6px;border-radius:3px;background:var(--fill)}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:26px;height:26px;margin-top:-10px;border-radius:50%;background:#fff;box-shadow:0 1px 4px rgba(0,0,0,.5)}
input[type=range]::-moz-range-track{height:6px;border-radius:3px;background:var(--fill)}
input[type=range]::-moz-range-thumb{width:26px;height:26px;border:0;border-radius:50%;background:#fff}
.big{font-size:64px;font-weight:700;line-height:1.1;margin-top:6px}
.sub{color:var(--muted);font-size:15px;margin-bottom:10px}
.pm{margin:6px 0 10px}.pm .btn{font-size:34px;font-weight:500;min-height:64px;border-radius:16px}
.ticks{display:flex;justify-content:space-between;padding:0 6px;font-size:12px;color:var(--faint)}
.ticks span{width:20px;text-align:center}
.seg{display:flex;gap:6px;background:var(--surface);border-radius:14px;padding:5px;margin:12px 0}
.seg button{flex:1;border:0;border-radius:10px;background:none;color:var(--text2);padding:11px 4px;font-size:15px;font-weight:600}
.seg button.sel{background:var(--accent);color:#fff}
.note{font-size:30px;font-weight:700;min-height:40px}
.cap{font-size:13px;color:var(--faint);margin-bottom:10px}
.meter{height:6px;border-radius:3px;background:var(--track);overflow:hidden}.meter i{display:block;height:100%;width:0;background:var(--bright);transition:width .15s}
.presets{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.switch{display:flex;align-items:center;gap:12px;width:100%;border:0;background:none;padding:10px 0;text-align:left;font-size:15px;color:var(--text)}
.switch span{flex:1}
.switch i{flex:none;position:relative;width:46px;height:28px;border-radius:14px;background:var(--track);transition:background .15s}
.switch i::after{content:"";position:absolute;top:3px;left:3px;width:22px;height:22px;border-radius:50%;background:#fff;transition:transform .15s}
.switch.on i{background:var(--bright)}.switch.on i::after{transform:translateX(18px)}
.switch.dis{opacity:.4}
.warn{display:flex;align-items:center;gap:10px;margin:4px 0 12px;padding:10px 12px;border-radius:12px;background:#3a3428;color:var(--amber);font-size:14px}
.warn span{flex:1}.warn .btn{flex:none;min-height:40px;padding:8px 12px;font-size:14px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(98px,1fr));gap:10px;margin:12px 0}
.tile{position:relative;aspect-ratio:1/1.08;border:2px solid transparent;border-radius:18px;background:var(--surface);padding:10px 6px 8px;
display:flex;flex-direction:column;align-items:center;justify-content:center;gap:6px;transition:transform .08s}
.tile:active{background:var(--press);transform:scale(.95)}
.tile img{width:54%;max-width:76px;aspect-ratio:1;pointer-events:none}
.tile span{max-width:100%;font-size:13px;line-height:1.2;color:var(--text2);text-align:center;overflow:hidden;display:-webkit-box;
-webkit-line-clamp:2;-webkit-box-orient:vertical;word-break:break-word}
.tile.unnamed span{color:var(--amber)}
.tile.playing{background:var(--soft);border-color:var(--bright)}
.tile.playing::after{content:"";position:absolute;inset:-2px;border-radius:18px;border:2px solid var(--light);animation:pulse 1s ease-out infinite}
@keyframes pulse{from{opacity:.9;transform:scale(1)}to{opacity:0;transform:scale(1.07)}}
.empty{padding:28px 12px;text-align:center;color:var(--muted);font-size:15px}
#toast{position:fixed;left:16px;right:16px;bottom:calc(84px + env(safe-area-inset-bottom));z-index:7;padding:12px 14px;border-radius:12px;
background:#2f3a49;color:#fff;font-size:14px;box-shadow:0 6px 20px rgba(0,0,0,.4);opacity:0;transform:translateY(10px);transition:all .2s;pointer-events:none}
#toast.show{opacity:1;transform:none}#toast.err{background:#4a2b2e}
</style>
</head>
<body>
<header><div class="logo">SZ</div><b>SoundZ</b><div id="conn"><i></i><span id="conn-text">Connexion…</span></div></header>
<div id="banner" hidden></div>
<main>
<section class="tab" id="tab-music">
  <div class="card center">
    <div class="big" id="m-big">0</div>
    <div class="sub" id="m-sub">son d'origine</div>
    <div class="row pm"><button class="btn" id="m-down" aria-label="Plus grave">−</button><button class="btn" id="m-up" aria-label="Plus aigu">+</button></div>
    <input type="range" id="m-range" min="-4" max="4" step="1" value="0">
    <div class="ticks"><span>−4</span><span>−3</span><span>−2</span><span>−1</span><span>0</span><span>+1</span><span>+2</span><span>+3</span><span>+4</span></div>
  </div>
  <div class="row"><button class="btn" id="m-reset">Remettre à 0</button><button class="btn" id="m-toggle">Désactiver</button></div>
  <p class="status" id="m-status"></p>
</section>
<section class="tab" id="tab-voice">
  <button class="btn primary wide" id="v-toggle">Activer la voix</button>
  <p class="status" id="v-status"></p>
  <div class="seg" id="v-modes"><button data-m="0">Sans effet</button><button data-m="1">Autotune</button><button data-m="2">Vocodeur</button></div>
  <div class="card center"><div class="note" id="v-note">—</div><div class="cap">note chantée → note envoyée</div><div class="meter"><i id="v-level"></i></div></div>
  <h2>Préréglages</h2>
  <div class="presets"><button class="btn" data-p="0">Naturel</button><button class="btn" data-p="1">T-Pain</button><button class="btn" data-p="2">Robot</button><button class="btn" data-p="3">Chœur</button></div>
  <h2>Réglages</h2>
  <div class="card" id="v-rows"></div>
  <div class="card">
    <button class="switch" id="v-chords"><span>Accords (chœur de robots, vocodeur)</span><i></i></button>
    <button class="switch" id="v-monitor"><span id="v-monitor-label">Entendre ma voix (écho)</span><i></i></button>
  </div>
</section>
<section class="tab" id="tab-board">
  <div class="warn" id="b-warn" hidden><span>Voix désactivée : les sons ne partent pas dans ton micro.</span><button class="btn primary" id="b-voice">Activer</button></div>
  <div class="row"><button class="btn stop" id="b-stop">■ Stop</button><button class="btn primary" id="b-replay">✂ 10 dernières s</button></div>
  <div class="grid" id="b-grid"></div>
  <div class="empty" id="b-empty" hidden>Aucun son pour l'instant. Crée tes sons dans SoundZ, sur le PC (onglet Soundboard).</div>
  <p class="status" id="b-captures" hidden></p>
  <div class="card"><div class="lbl" id="b-vol-label">Volume des sons</div><input type="range" id="b-vol" min="0" max="100" step="1" value="80"></div>
</section>
</main>
<nav>
  <button data-tab="music"><span class="ic">🎵</span>Musique</button>
  <button data-tab="voice"><span class="ic">🎤</span>Voix</button>
  <button data-tab="board"><span class="ic">🔊</span>Soundboard</button>
</nav>
<div id="toast"></div>
<script>
(function () {
  var $ = function (id) { return document.getElementById(id); };
  var K = new URLSearchParams(location.search).get('k') || '';
  try { if (K) localStorage.setItem('soundz.k', K); else K = localStorage.getItem('soundz.k') || ''; } catch (e) {}
  $('manifest').href = '/manifest.webmanifest?k=' + encodeURIComponent(K);
  var tab = 'board';
  try { tab = localStorage.getItem('soundz.tab') || 'board'; } catch (e) {}
  if (location.hash.length > 1) tab = location.hash.slice(1);  // #music, #voice, #board
  var version = -1, noticeId = null, timer = 0, polling = false, failures = 0, hold = {}, semi = 0, voice = null, toastTimer = 0;

  function vibrate() { try { if (navigator.vibrate) navigator.vibrate(12); } catch (e) {} }
  function withKey(path) { return path + (path.indexOf('?') < 0 ? '?' : '&') + 'k=' + encodeURIComponent(K); }
  function call(path, body) {
    var options = body ? { method: 'POST', body: body, cache: 'no-store', headers: { 'Content-Type': 'application/x-www-form-urlencoded' } }
                       : { cache: 'no-store' };
    return fetch(withKey(path), options).then(function (r) {
      if (!r.ok) throw { code: r.status };
      return r.json();
    });
  }
  function banner(text) { var b = $('banner'); b.hidden = !text; b.textContent = text || ''; }
  function connected(on) { $('conn').className = on ? 'on' : 'off'; $('conn-text').textContent = on ? 'Relié' : 'Hors ligne'; }
  function ok(state) { failures = 0; connected(true); banner(''); render(state); }
  function fail(e) {
    if (e && e.code === 503) return;  // SoundZ est occupé : la prochaine fois
    if (e && e.code === 403) {
      connected(false);
      banner('Ce lien n\'est plus valable. Dans SoundZ, sur le PC : ⚙ → Télécommande sur téléphone, puis scanne le QR code.');
      return;
    }
    if (++failures >= 2) {
      connected(false);
      banner('SoundZ ne répond pas. Vérifie qu\'il est ouvert sur le PC et que le téléphone est sur le même Wi-Fi.');
    }
  }
  function send(action, a, b) {
    return call('/api/cmd', 'a=' + encodeURIComponent(action) + '&v=' + (Number(a) | 0) + '&w=' + (Number(b) | 0)).then(ok, fail);
  }
  function poll() {
    if (polling || document.hidden) return;
    clearTimeout(timer);
    if (!K) { banner('Scanne le QR code affiché dans SoundZ, sur le PC : ⚙ → Télécommande sur téléphone.'); return; }
    polling = true;
    call('/api/state').then(ok, fail).catch(function (e) { console.error(e); }).then(function () {
      polling = false;
      clearTimeout(timer);
      timer = setTimeout(poll, failures ? 1500 : tab === 'voice' ? 250 : 600);
    });
  }
  document.addEventListener('visibilitychange', function () { if (!document.hidden) poll(); });
  window.addEventListener('pageshow', poll);

  function showTab(name) {
    tab = name;
    try { localStorage.setItem('soundz.tab', name); } catch (e) {}
    document.querySelectorAll('nav button').forEach(function (b) { b.classList.toggle('sel', b.dataset.tab === name); });
    document.querySelectorAll('.tab').forEach(function (s) { s.classList.toggle('show', s.id === 'tab-' + name); });
    poll();
  }
  document.querySelectorAll('nav button').forEach(function (b) { b.onclick = function () { showTab(b.dataset.tab); }; });

  // Curseurs : envoyés pendant le geste (au plus toutes les 120 ms) ; l'état reçu ne les fait pas sauter sous le doigt.
  function fill(el, bipolar) {
    var min = +el.min, max = +el.max, at = (+el.value - min) / (max - min) * 100, from = 0, to = at;
    if (bipolar) { var zero = (0 - min) / (max - min) * 100; from = Math.min(zero, at); to = Math.max(zero, at); }
    el.style.setProperty('--fill', 'linear-gradient(90deg,var(--track) ' + from + '%,var(--bright) ' + from + '%,var(--bright) ' + to +
                         '%,var(--track) ' + to + '%)');
  }
  function setRange(el, value, bipolar) {
    if ((hold[el.id] || 0) > Date.now()) return;
    if (+el.value !== value) el.value = value;
    fill(el, bipolar);
  }
  function bindRange(el, bipolar, onValue) {
    var last = 0, later = 0;
    function go() { last = Date.now(); onValue(+el.value); }
    el.addEventListener('input', function () {
      hold[el.id] = Date.now() + 900;
      fill(el, bipolar);
      clearTimeout(later);
      if (Date.now() - last > 120) go(); else later = setTimeout(go, 120);
    });
    el.addEventListener('change', function () { hold[el.id] = Date.now() + 900; clearTimeout(later); go(); });
  }
  function toast(text, error) {
    var t = $('toast');
    t.textContent = text;
    t.className = 'show' + (error ? ' err' : '');
    clearTimeout(toastTimer);
    toastTimer = setTimeout(function () { t.className = error ? 'err' : ''; }, 3200);
  }

  // ---- Musique
  bindRange($('m-range'), true, function (v) { send('music.semi', v); });
  $('m-down').onclick = function () { vibrate(); send('music.semi', semi - 1); };
  $('m-up').onclick = function () { vibrate(); send('music.semi', semi + 1); };
  $('m-reset').onclick = function () { vibrate(); send('music.semi', 0); };
  $('m-toggle').onclick = function () { vibrate(); send('music.toggle'); };
  function renderMusic(m) {
    semi = m.semi;
    var n = Math.abs(m.semi);
    $('m-big').textContent = m.semi === 0 ? '0' : (m.semi > 0 ? '+' : '−') + n;
    $('m-sub').textContent = m.semi === 0 ? 'son d\'origine' : n === 1 ? 'demi-ton' : 'demi-tons';
    setRange($('m-range'), m.semi, true);
    $('m-toggle').textContent = m.on ? 'Désactiver' : 'Activer';
    $('m-toggle').className = 'btn' + (m.on ? '' : ' primary');
    $('m-status').textContent = m.status;
    $('m-status').className = 'status' + (m.err ? ' err' : '');
  }

  // ---- Voix
  var rows = [];
  $('v-toggle').onclick = function () { vibrate(); send('voice.toggle'); };
  $('b-voice').onclick = function () { vibrate(); send('voice.toggle'); };
  document.querySelectorAll('#v-modes button').forEach(function (b) {
    b.onclick = function () { vibrate(); send('voice.mode', +b.dataset.m); };
  });
  document.querySelectorAll('.presets button').forEach(function (b) {
    b.onclick = function () { vibrate(); send('voice.preset', +b.dataset.p); };
  });
  $('v-chords').onclick = function () {
    if (!voice || voice.mode !== 2) return;
    vibrate();
    send('voice.chords', voice.chords ? 0 : 1);
  };
  $('v-monitor').onclick = function () { if (!voice) return; vibrate(); send('voice.monitor', voice.monitor ? 0 : 1); };
  function renderVoice(v) {
    voice = v;
    $('v-toggle').textContent = v.on ? 'Désactiver la voix' : 'Activer la voix';
    $('v-toggle').className = 'btn wide' + (v.on ? '' : ' primary');
    $('v-status').textContent = v.status;
    $('v-status').className = 'status' + (v.err ? ' err' : '');
    document.querySelectorAll('#v-modes button').forEach(function (b) { b.classList.toggle('sel', +b.dataset.m === v.mode); });
    $('v-note').textContent = v.note;
    $('v-level').style.width = v.level + '%';
    if (!rows.length) {
      v.rows.forEach(function (r, i) {
        var box = document.createElement('div'), label = document.createElement('div'), input = document.createElement('input');
        box.className = 'slider';
        label.className = 'lbl';
        input.type = 'range';
        input.id = 'v-row' + i;
        input.min = r.min;
        input.max = r.max;
        input.step = 1;
        box.appendChild(label);
        box.appendChild(input);
        $('v-rows').appendChild(box);
        bindRange(input, i === 4, function (value) { send('voice.row', i, value); });
        rows.push({ label: label, input: input });
      });
    }
    v.rows.forEach(function (r, i) {
      if (!rows[i]) return;
      rows[i].label.textContent = r.label;
      setRange(rows[i].input, r.value, i === 4);
    });
    $('v-chords').className = 'switch' + (v.chords ? ' on' : '') + (v.mode === 2 ? '' : ' dis');
    $('v-monitor').className = 'switch' + (v.monitor ? ' on' : '');
    $('v-monitor-label').textContent = v.monitorLabel;
  }

  // ---- Soundboard
  $('b-stop').onclick = function () { vibrate(); send('sb.stop'); };
  $('b-replay').onclick = function () { vibrate(); send('sb.replay'); };
  bindRange($('b-vol'), false, function (v) { send('sb.volume', v); });
  function renderBoard(b) {
    $('b-warn').hidden = b.mic;
    var grid = $('b-grid');
    if (b.version !== version) {
      version = b.version;
      grid.textContent = '';
      b.sounds.forEach(function (s) {
        var tile = document.createElement('button'), img = document.createElement('img'), name = document.createElement('span');
        tile.className = 'tile' + (s.unnamed ? ' unnamed' : '');
        tile.dataset.uid = s.uid;
        img.alt = '';
        img.src = withKey('/img/' + s.uid + '?v=' + b.version);
        name.textContent = (s.unnamed ? '✎ ' : '') + s.name;
        tile.appendChild(img);
        tile.appendChild(name);
        tile.onclick = function () { vibrate(); tile.classList.add('playing'); send('sb.play', s.uid); };
        grid.appendChild(tile);
      });
      $('b-empty').hidden = b.sounds.length > 0;
    }
    var playing = {};
    b.sounds.forEach(function (s) { playing[s.uid] = s.playing; });
    Array.prototype.forEach.call(grid.children, function (tile) { tile.classList.toggle('playing', !!playing[tile.dataset.uid]); });
    var c = $('b-captures');
    c.hidden = !b.captures;
    c.textContent = b.captures === 1 ? '✂ 1 capture à trier : découpe-la dans l\'overlay en jeu ou dans SoundZ, sur le PC.'
                                     : '✂ ' + b.captures + ' captures à trier : découpe-les dans l\'overlay en jeu ou dans SoundZ, sur le PC.';
    setRange($('b-vol'), b.volume, false);
    $('b-vol-label').textContent = 'Volume des sons : ' + $('b-vol').value + ' %';
    if (noticeId === null) noticeId = b.noticeId;  // pas de vieux message à l'ouverture
    else if (b.noticeId && b.noticeId !== noticeId) { noticeId = b.noticeId; toast(b.notice, b.noticeErr); }
  }
  $('b-vol').addEventListener('input', function () { $('b-vol-label').textContent = 'Volume des sons : ' + $('b-vol').value + ' %'; });

  function render(s) {
    if (s.music) renderMusic(s.music);
    if (s.voice) renderVoice(s.voice);
    if (s.board) renderBoard(s.board);
  }
  showTab(tab === 'music' || tab === 'voice' ? tab : 'board');
})();
</script>
</body>
</html>
)SZPAGE";

}  // namespace remote
