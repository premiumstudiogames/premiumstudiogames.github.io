/* The Control Center over a game (console mode), like the PS5's: cards for the game you're in and captures above,
   a bar of icons below. Driven by the controller (D-pad / stick, A / ✕, B / ○, Home) or the keyboard / mouse. */
'use strict';
const $ = s => document.querySelector(s);
const esc = s => String(s == null ? '' : s).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const I = {
  home: '<svg viewBox="0 0 24 24"><path d="M3 11.5L12 4l9 7.5"/><path d="M5.5 10v10h13V10"/><path d="M10 20v-5h4v5"/></svg>',
  switcher: '<svg viewBox="0 0 24 24"><rect x="4" y="9" width="16" height="11" rx="1.5"/><path d="M6 6h12M8 3.5h8"/></svg>',
  bell: '<svg viewBox="0 0 24 24"><path d="M6 17V11a6 6 0 0 1 12 0v6l1.5 2h-15z"/><path d="M10 21h4"/></svg>',
  base: '<svg viewBox="0 0 24 24"><circle cx="12" cy="8" r="3"/><path d="M6 15c1.5-2 3.5-3 6-3s4.5 1 6 3"/><ellipse cx="12" cy="18" rx="9" ry="3"/></svg>',
  sound: '<svg viewBox="0 0 24 24"><path d="M4 9.5h4L13 5v14l-5-4.5H4z"/><path d="M16 9a4 4 0 0 1 0 6M18.5 6.5a7.5 7.5 0 0 1 0 11"/></svg>',
  mute: '<svg viewBox="0 0 24 24"><path d="M4 9.5h4L13 5v14l-5-4.5H4z"/><path d="M16.5 9.5l5 5M21.5 9.5l-5 5"/></svg>',
  power: '<svg viewBox="0 0 24 24"><path d="M12 3v8"/><path d="M7 6.5a7.5 7.5 0 1 0 10 0"/></svg>',
  pad: '<svg viewBox="0 0 24 24"><path d="M7 7h10a4 4 0 0 1 4 4v4a2.5 2.5 0 0 1-4.3 1.7L15 15H9l-1.7 1.7A2.5 2.5 0 0 1 3 15v-4a4 4 0 0 1 4-4z"/><path d="M7.5 10v3M6 11.5h3"/><circle cx="16" cy="10.5" r=".8"/><circle cx="17.5" cy="12.5" r=".8"/></svg>',
  shot: '<svg viewBox="0 0 24 24"><rect x="3" y="5" width="13" height="10" rx="1"/><path d="M5 19h4"/><rect x="13" y="12" width="8" height="6" rx="1"/><circle cx="17" cy="15" r="1.6"/></svg>',
  clip: '<svg viewBox="0 0 24 24"><rect x="3" y="5" width="13" height="12" rx="1.5"/><path d="M16 9l5-3v12l-5-3z"/></svg>',
  trophy: '<svg viewBox="0 0 24 24"><path d="M7 4h10v5a5 5 0 0 1-10 0z"/><path d="M7 6H4a3 3 0 0 0 3 4M17 6h3a3 3 0 0 1-3 4M12 14v4M8 21h8M9 18h6"/></svg>',
  stop: '<svg viewBox="0 0 24 24"><rect x="6" y="6" width="12" height="12" rx="1.5"/></svg>',
  moon: '<svg viewBox="0 0 24 24"><path d="M19 14.5A8 8 0 0 1 9.5 5a8 8 0 1 0 9.5 9.5z"/></svg>',
  restart: '<svg viewBox="0 0 24 24"><path d="M4 12a8 8 0 1 0 2.4-5.7"/><path d="M4 4v4h4"/></svg>',
  exit: '<svg viewBox="0 0 24 24"><path d="M10 4H5v16h5"/><path d="M14 8l4 4-4 4M18 12H9"/></svg>'
};
I.notes = I.bell;
let D = null, panel = '', confirm = null, kind = 'xbox';
const G = b => kind === 'ps' ? `<kbd class="p${b}">${{ a: '✕', b: '○' }[b]}</kbd>` : `<kbd class="x${b}">${b.toUpperCase()}</kbd>`;
const mins = since => Math.max(1, Math.round((Date.now() - since) / 60000));
const playing = since => { const m = mins(since), h = Math.floor(m / 60), r = m % 60;
  const hs = h === 1 ? 'ساعة' : h === 2 ? 'ساعتين' : h + ' ساعات', ms = r === 1 ? 'دقيقة' : r === 2 ? 'دقيقتين' : r <= 10 ? r + ' دقائق' : r + ' دقيقة';
  return m < 1 ? 'بدأت الحين' : h ? `لك ${hs}${r ? ' و' + ms : ''} تلعب` : `لك ${ms} تلعب`; };
const BAR = [['home', 'الرئيسية'], ['switcher', 'المبدل'], ['notes', 'الإشعارات'], ['base', 'قاعدة الألعاب'], ['sound', 'الصوت'], ['profile', 'الملف الشخصي'], ['power', 'الطاقة']];

function cards() {
  const t = D.trophies || {}, p = t.all ? Math.round(t.got / t.all * 100) : 0, n = (D.con.notes || [])[0];
  return `<button class="c big" data-a="close"><span class="art${D.cover ? '' : ' none'}" style="${D.cover ? `background-image:url('${D.cover}')` : ''}"></span>
      <span class="ic">${I.pad}</span><span class="cap"><small>${playing(D.since)} · ${G('a')} العودة إلى اللعبة</small><b>${esc(D.name)}</b></span></button>
    ${t.all ? `<button class="c" data-a="page" data-v="trophies"><span class="ic">${I.trophy}</span><span class="cap"><small>الجوائز</small><b class="num">${t.got}/${t.all} · ${p}%</b>
      <span class="bar"><i style="width:${p}%"></i></span><span class="tros">${['platinum', 'gold', 'silver', 'bronze'].map(k => `<span class="tro"><i class="t-${k}"></i><span class="num">${(t.list || []).filter(x => x.got && x.tier === k).length}</span></span>`).join('')}</span></span></button>` : ''}
    <button class="c" data-a="shot"><span class="ic">${I.shot}</span><span class="cap"><small>التقاط</small><b>لقطة شاشة</b><small>F12</small></span></button>
    <button class="c" data-a="rec"><span class="ic">${I.clip}</span><span class="cap"><small>${D.recording ? '<span class="rec"><i></i> يسجّل الحين</span>' : 'التقاط'}</small><b>${D.recording ? 'أوقف التسجيل' : 'سجّل مقطع'}</b><small>F9</small></span></button>
    ${n ? `<button class="c" data-a="panel" data-v="notes"><span class="ic">${I.bell}</span><span class="cap"><small>آخر إشعار</small><b>${esc(n.t)}</b></span></button>` : ''}`;
}
function bar() {
  const n = (D.con.notes || []).length, pr = D.con.profile || {};
  return BAR.map(([k, t]) => `<button class="i${panel === k ? ' act' : ''}" data-a="${k === 'home' ? 'home' : k === 'base' ? 'page' : k === 'profile' ? 'page' : 'panel'}" data-v="${k === 'base' ? 'friends' : k === 'profile' ? 'profile' : k}" data-tip="${t}">${
    k === 'profile' ? `<span class="av" style="--h:${+pr.hue || 210}">${esc(pr.initial || '؟')}</span>` : k === 'sound' ? (D.muted ? I.mute : I.sound) : I[k]}${k === 'notes' && n ? `<span class="badge num">${n}</span>` : ''}</button>`).join('');
}
function panelHTML() {
  if (confirm) return `<h3>${esc(confirm.title)}</h3><p>${esc(confirm.text || '')}</p><button class="m danger" data-a="yes">${esc(confirm.ok)}</button><button class="m" data-a="no">إلغاء</button>`;
  if (panel === 'switcher') return `<h3>المبدل</h3><button class="m" data-a="switch" data-v="home"><span class="mi">${I.home}</span><span>الشاشة الرئيسية<small>الألعاب تبقى شغّالة</small></span></button>`
    + D.running.map(g => `<button class="m" data-a="switch" data-v="${esc(g.key)}"><span class="cv" style="${g.cover ? `background-image:url('${g.cover}')` : ''}"></span><span>${esc(g.name)}<small>${g.key === D.key ? 'أنت فيها الحين' : playing(g.since)}</small></span></button>`).join('')
    + `<button class="m danger" data-a="ask" data-v="stop"><span class="mi">${I.stop}</span><span>إغلاق اللعبة<small>${esc(D.name)}</small></span></button>`;
  if (panel === 'notes') { const l = D.con.notes || [];
    return `<h3>الإشعارات</h3>${l.length ? l.map(x => `<button class="m" data-a="page" data-v="notes"><span class="mi" style="font-size:1.6vw;text-align:center">${esc(x.i || '•')}</span><span>${esc(x.t)}</span></button>`).join('') : '<p>ما فيه إشعارات جديدة.</p>'}`; }
  if (panel === 'sound') return `<h3>الصوت</h3>${D.canMute ? `<button class="m" data-a="mute"><span class="mi">${D.muted ? I.mute : I.sound}</span><span>صوت اللعبة<small>${D.muted ? 'مكتوم' : 'شغّال'}</small></span><i class="sw${D.muted ? '' : ' on'}"></i></button>` : '<p>صوت هذي اللعبة من داخلها.</p>'}`;
  if (panel === 'power') return `<h3>الطاقة</h3>
    <button class="m danger" data-a="ask" data-v="stop"><span class="mi">${I.stop}</span><span>إغلاق اللعبة<small>${esc(D.name)}</small></span></button>
    <button class="m" data-a="power" data-v="sleep"><span class="mi">${I.moon}</span><span>وضع السكون</span></button>
    <button class="m" data-a="ask" data-v="restart"><span class="mi">${I.restart}</span><span>إعادة تشغيل الكمبيوتر</span></button>
    <button class="m" data-a="ask" data-v="shutdown"><span class="mi">${I.power}</span><span>إيقاف تشغيل الكمبيوتر</span></button>
    <button class="m" data-a="ask" data-v="quit"><span class="mi">${I.exit}</span><span>اقفل Premium Games</span></button>`;
  return '';
}
function draw(focusSel) {
  const ae = document.activeElement, keep = !focusSel && ae && ae.dataset && ae.dataset.a ? `[data-a="${ae.dataset.a}"]${ae.dataset.v ? `[data-v="${CSS.escape(ae.dataset.v)}"]` : ''}` : '';
  $('#cards').innerHTML = cards(); $('#bar').innerHTML = bar();
  const p = $('#panel'), ph = panelHTML(); p.innerHTML = ph; p.classList.toggle('on', !!ph);
  $('#cards').style.display = ph ? 'none' : '';
  $('#help').innerHTML = `<span>${G('a')} اختيار</span><span>${G('b')} رجوع</span><span><kbd>⌂</kbd> العودة إلى اللعبة · مطوّل: الشاشة الرئيسية</span>`;
  document.querySelectorAll('[data-a]').forEach(b => { b.onclick = () => act(b); b.onmouseenter = () => { if (!document.body.classList.contains('nomouse')) focus(b, true); }; });
  const vis = sel => sel && [...document.querySelectorAll(sel)].find(e => e.offsetParent !== null);
  const f = vis(focusSel) || (ph ? p.querySelector('[data-a]') : vis(keep)) || document.querySelector('.c');
  if (f) focus(f, true);
}
function focus(el, quiet) {
  el.focus({ preventScroll: true }); el.scrollIntoView({ block: 'nearest' });
  const tip = $('#tip');
  if (el.classList.contains('i')) { tip.textContent = el.dataset.tip; const r = el.getBoundingClientRect(); tip.style.left = (r.left + r.width / 2) + 'px'; tip.style.display = ''; } else tip.style.display = 'none';
  if (!quiet) beep(); }
let ac = null;
function beep(f = 1250, d = .025) { try { ac = ac || new AudioContext(); const t = ac.currentTime, o = ac.createOscillator(), g = ac.createGain(); o.frequency.setValueAtTime(f, t); g.gain.setValueAtTime(.025, t); g.gain.exponentialRampToValueAtTime(.0001, t + d); o.connect(g).connect(ac.destination); o.start(t); o.stop(t + d + .02); } catch (e) {} }

const ASK = { stop: () => ({ title: 'إغلاق اللعبة؟', text: D.name + ' تنقفل وترجع للشاشة الرئيسية. اللي ما حفظته ممكن يضيع.', ok: 'إغلاق اللعبة', go: () => CC.act('stop') }),
  restart: () => ({ title: 'إعادة تشغيل الكمبيوتر؟', text: 'اللعبة تنحفظ وتنقفل أول.', ok: 'أعد التشغيل', go: () => CC.act('power', 'restart') }),
  shutdown: () => ({ title: 'إيقاف تشغيل الكمبيوتر؟', text: 'اللعبة تنحفظ وتنقفل أول.', ok: 'أطفئ', go: () => CC.act('power', 'shutdown') }),
  quit: () => ({ title: 'تقفل Premium Games؟', text: 'اللعبة تنقفل أول.', ok: 'اقفل', go: () => CC.act('power', 'quit') }) };
async function act(el) {
  const a = el.dataset.a, v = el.dataset.v; beep(760, .08);
  if (a === 'close') return CC.act('close');
  if (a === 'home') return CC.act('home');
  if (a === 'page') return CC.act('home', v);
  if (a === 'switch') return CC.act('switch', v);
  if (a === 'shot' || a === 'rec') return CC.act(a);
  if (a === 'panel') { panel = panel === v ? '' : v; confirm = null; return draw(panel ? '#panel [data-a]' : `.i[data-v="${v}"]`); }
  if (a === 'mute') { D.muted = await CC.act('mute'); return draw('[data-a="mute"]'); }
  if (a === 'power') return CC.act('power', v);
  if (a === 'ask') { confirm = ASK[v](); return draw('[data-a="no"]'); }
  if (a === 'no') { confirm = null; return draw(); }
  if (a === 'yes') { const c = confirm; confirm = null; return c.go(); }
}
function back() {
  if (confirm) { confirm = null; beep(420, .07); return draw(); }
  if (panel) { const v = panel; panel = ''; beep(420, .07); return draw(`.i[data-v="${v}"]`); }
  CC.act('close');
}
/* moving: rows are the cards, the bar, a panel's list */
function move(dx, dy) {
  const all = [...document.querySelectorAll('[data-a]')].filter(e => e.offsetParent !== null), c = document.activeElement;
  if (!all.includes(c)) return all[0] && focus(all[0]);
  const r0 = c.getBoundingClientRect(); let best = null, bd = 1e9;
  for (const e of all) { if (e === c) continue; const r = e.getBoundingClientRect(), vx = (r.left + r.width / 2) - (r0.left + r0.width / 2), vy = (r.top + r.height / 2) - (r0.top + r0.height / 2);
    const same = r.bottom > r0.top + 2 && r.top < r0.bottom - 2;
    if (dx) { if (!same || Math.sign(vx) !== dx || Math.abs(vx) < 4) continue; const d = Math.abs(vx); if (d < bd) { bd = d; best = e; } }
    else { if (same || Math.sign(vy) !== dy || Math.abs(vy) < 4) continue; const d = Math.abs(vy) * 3 + Math.abs(vx); if (d < bd) { bd = d; best = e; } } }
  if (best) focus(best);
}
addEventListener('keydown', e => {
  document.body.classList.add('nomouse');
  const k = e.key, m = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1] }[k];
  if (m) { e.preventDefault(); move(...m); }
  else if (k === 'Enter' || k === ' ') { e.preventDefault(); const a = document.activeElement; if (a && a.dataset.a) act(a); }
  else if (k === 'Escape' || k === 'Backspace') { e.preventDefault(); back(); }
  else if (k === 'Tab' && e.shiftKey) { e.preventDefault(); CC.act('close'); }
  else if (k === 'Home' || k === 'F1') { e.preventDefault(); CC.act('close'); }
});
addEventListener('mousemove', () => document.body.classList.remove('nomouse'));
/* controller */
const last = {}, held = {};
function poll() {
  const pads = navigator.getGamepads ? [...navigator.getGamepads()].filter(Boolean) : [];
  const now = performance.now();
  for (const p of pads) {
    kind = /054c|playstation|dualsense|dualshock|wireless controller/i.test(p.id) ? 'ps' : 'xbox';
    const b = i => !!(p.buttons[i] && p.buttons[i].pressed), ax = p.axes || [];
    const st = { a: b(0), b: b(1), up: b(12) || ax[1] < -.55, down: b(13) || ax[1] > .55, left: b(14) || ax[0] < -.55, right: b(15) || ax[0] > .55 };
    for (const [k, v] of Object.entries(st)) {
      const id = p.index + k, prev = last[id]; last[id] = v; if (!v) continue;
      const rep = k === 'up' || k === 'down' || k === 'left' || k === 'right';
      if (prev && !(rep && now > held[id])) continue;
      held[id] = now + (prev ? 85 : 300); document.body.classList.add('nomouse');
      if (k === 'a') { const a = document.activeElement; if (a && a.dataset.a) act(a); } else if (k === 'b') back();
      else move(k === 'left' ? -1 : k === 'right' ? 1 : 0, k === 'up' ? -1 : k === 'down' ? 1 : 0);
    }
  }
  requestAnimationFrame(poll);
}
// the Home button itself is read by the app (it closes this, or goes to the home screen when held)
(async () => {
  try { D = await CC.data(); } catch (e) {}
  if (!D) D = { key: '', name: '', since: Date.now(), running: [], con: {}, trophies: {} };
  D.con = D.con || {};
  draw(); requestAnimationFrame(poll);
  setInterval(() => { if (!panel && !confirm) { const s = document.querySelector('.c.big small'); if (s) s.innerHTML = `${playing(D.since)} · ${G('a')} العودة إلى اللعبة`; } }, 30000);
})();
