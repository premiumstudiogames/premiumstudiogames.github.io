/* Big Picture: the PC as a console. Full screen, big covers, works with a game controller, arrows or the mouse.
   Loaded by app.js; uses its data through the hooks it passes in. */
export function makeBP(H){
  const {$, esc, GAMES, ownedEds, instOf, ED_TAG, PS, toast, cover, money, ent, state} = H;
  let on = false, tab = "library", sel = 0, root = null, raf = 0, lastPad = {}, padHeld = {};
  const TABS = [["library","المكتبة"],["store","المتجر"],["friends","الأصدقاء"],["downloads","التحميلات"],["exit","خروج"]];
  const clock = () => new Date().toLocaleTimeString("ar-SA-u-nu-latn", {hour:"numeric", minute:"2-digit"});
  function open(){ if (on) return; on = true; document.body.classList.add("bp-on");
    root = document.createElement("div"); root.id = "bp"; document.body.appendChild(root);
    if (PS && PS.fullscreen) PS.fullscreen(true).catch(() => {}); else { try { document.documentElement.requestFullscreen(); } catch(e) {} }
    document.addEventListener("keydown", key, true); raf = requestAnimationFrame(poll); draw(); setTimeout(() => root && root.querySelector(".bp-tile.on, .bp-main [data-bp]")?.focus(), 50); }
  function close(){ if (!on) return; on = false; document.body.classList.remove("bp-on"); if (root) root.remove(); root = null;
    document.removeEventListener("keydown", key, true); cancelAnimationFrame(raf);
    if (PS && PS.fullscreen) PS.fullscreen(false).catch(() => {}); else { try { if (document.fullscreenElement) document.exitFullscreen(); } catch(e) {} }
    if (location.hash === "#bigpicture") location.hash = "#library"; }
  const mine = () => GAMES.filter(g => ownedEds(g.id).length);
  function gameState(g){ const S = state(), eds = ownedEds(g.id).slice().reverse();
    for (const ed of eds) { const [k, inst] = instOf(g.id, ed); if (inst && S.RUNNING[k]) return {k, ed, run:true}; }
    for (const ed of eds) { const [k, inst] = instOf(g.id, ed); if (inst) return {k, ed, inst:true}; }
    for (const ed of eds) { const [k] = instOf(g.id, ed); if (S.PROGRESS[k]) return {k, ed, prog:S.PROGRESS[k]}; }
    return {k:instOf(g.id, eds[0] || "demo")[0], ed:eds[0], none:true}; }
  function draw(){
    if (!on || !root) return;
    const S = state(), u = S.user, prof = S.PROFILE || {};
    const online = Object.keys(S.FRIENDS).filter(id => S.FRIENDS[id].status === "ok" && S.online(id)).length;
    const ae = root.contains(document.activeElement) ? document.activeElement : null;
    const keep = ae && ae.dataset.bp ? `[data-bp="${ae.dataset.bp}"]` + ["i","t","a","g"].map(k => ae.dataset[k] != null ? `[data-${k}="${ae.dataset[k]}"]` : "").join("") : "";
    let body = "";
    if (tab === "library") {
      const l = mine();
      if (!l.length) body = `<div class="bp-empty">مكتبتك فاضية. روح للمتجر وأضف لعبة.</div>`;
      else { sel = Math.min(sel, l.length - 1); const g = l[sel], st = gameState(g), mins = (S.PLAY[g.id] && S.PLAY[g.id].minutes) || 0;
        const btn = st.run ? `<button class="bp-btn stop" data-bp="act" data-a="stop">■ إغلاق</button>` : st.inst ? `<button class="bp-btn play" data-bp="act" data-a="play">▶ لعب</button>` : st.prog ? `<button class="bp-btn" disabled>يحمّل…</button>` : `<button class="bp-btn inst" data-bp="act" data-a="install">⬇ تثبيت</button>`;
        body = `<div class="bp-hero"><span class="bp-bg" data-cover="${g.id}"></span><div class="bp-info"><h1>${esc(g.en || g.title)}</h1>
          <div class="bp-meta"><span>${ED_TAG[st.ed] || ""}</span><span>${mins >= 60 ? (mins / 60).toFixed(1) + " ساعة لعب" : mins + " دقيقة لعب"}</span>${st.run ? '<span class="bp-run">● شغالة</span>' : ""}</div>${btn}</div></div>
          <div class="bp-row">${l.map((x, i) => `<button class="bp-tile${i === sel ? " on" : ""}" data-bp="tile" data-i="${i}"><span data-cover="${x.id}"></span><b>${esc(x.en || x.title)}</b></button>`).join("")}</div>`; }
    } else if (tab === "store") {
      body = `<div class="bp-grid">${GAMES.map((g, i) => { const e = ent(g.id), low = g.editions.filter(x => x.price > 0).map(x => x.price);
        return `<button class="bp-card" data-bp="store" data-g="${g.id}"><span data-cover="${g.id}"></span><b>${esc(g.en || g.title)}</b><small>${e.best ? "في مكتبتك" : low.length ? "من " + money(Math.min(...low)) : "مجانية"}</small></button>`; }).join("")}</div>`;
    } else if (tab === "friends") {
      const fr = Object.entries(S.FRIENDS).filter(([, f]) => f.status === "ok").sort((a, b) => S.online(b[0]) - S.online(a[0]));
      body = fr.length ? `<div class="bp-list">${fr.map(([id, f]) => { const pl = S.playing(id), onl = S.online(id);
        return `<div class="bp-friend${onl ? " on" : ""}" data-bp="item"><span class="bp-av">${esc(f.username[0].toUpperCase())}</span><div><b>@${esc(f.username)}</b><small>${pl ? "يلعب " + esc(pl) : onl ? "متصل" : "غير متصل"}</small></div></div>`; }).join("")}</div>` : `<div class="bp-empty">ما عندك أصدقاء للحين.</div>`;
    } else if (tab === "downloads") {
      const act = Object.values(S.PROGRESS);
      body = act.length ? `<div class="bp-list">${act.map(p => `<div class="bp-dl" data-bp="item"><b>${esc(H.keyTitle(p.id))}</b><div class="bp-bar"><i style="width:${H.pct(p)}%"></i></div><small class="num">${H.progText(p)}</small></div>`).join("")}</div>` : `<div class="bp-empty">ما فيه تحميلات الحين.</div>`;
    }
    root.innerHTML = `<header class="bp-top"><span class="bp-logo"><i>P</i><i>S</i></span>
        <nav>${TABS.map(([id, n]) => `<button class="bp-tab${tab === id ? " on" : ""}" data-bp="tab" data-t="${id}">${n}</button>`).join("")}</nav>
        <span class="bp-me">${online ? `<span class="bp-fr">● ${online}</span>` : ""}<b>${esc(prof.username ? "@" + prof.username : (u ? "" : "ضيف"))}</b><span class="num">${clock()}</span></span></header>
      <main class="bp-main">${body}</main>
      <footer class="bp-help"><span><kbd>A</kbd> / Enter اختيار</span><span><kbd>B</kbd> / Esc رجوع</span><span><kbd>LB</kbd><kbd>RB</kbd> / Tab الأقسام</span></footer>`;
    root.querySelectorAll("[data-cover]").forEach(el => cover(el, el.dataset.cover));
    root.querySelectorAll("[data-bp]").forEach(el => el.onclick = () => activate(el));
    const f = (keep && root.querySelector(keep)) || root.querySelector(".bp-tile.on") || root.querySelector(".bp-main [data-bp]") || root.querySelector(".bp-tab.on");
    if (f) f.focus({preventScroll:true});
    const t = root.querySelector(".bp-tile.on"); if (t) t.scrollIntoView({inline:"center", block:"nearest", behavior:"smooth"});
  }
  async function activate(el){
    const k = el.dataset.bp;
    if (k === "tab") { if (el.dataset.t === "exit") return close(); tab = el.dataset.t; sel = 0; draw(); return; }
    if (k === "tile") { const i = +el.dataset.i; if (i === sel) { const a = root.querySelector('[data-a]'); if (a) a.focus(); } sel = i; draw(); root.querySelector(`.bp-tile[data-i="${i}"]`)?.focus(); return; }
    if (k === "store") { close(); location.hash = el.dataset.g; return; }
    if (k === "act") { const g = mine()[sel], st = gameState(g), a = el.dataset.a;
      if (!PS) return toast("التشغيل من تطبيق Premium Studio");
      el.disabled = true;
      try { if (a === "play") await PS.launch(st.k, st.ed); else if (a === "stop") await PS.stop(st.k); else if (a === "install") await PS.install(st.k, {edition:st.ed}); }
      catch(e) { toast(String(e && e.message || e)); }
      setTimeout(draw, 400); }
  }
  // move focus to the nearest focusable thing in a direction (works for any layout)
  function move(dx, dy){
    const items = [...root.querySelectorAll("[data-bp]")].filter(e => !e.disabled), cur = document.activeElement;
    if (!items.length) return; if (!items.includes(cur)) return items[0].focus();
    const r0 = cur.getBoundingClientRect(), c0 = {x:r0.left + r0.width / 2, y:r0.top + r0.height / 2};
    let best = null, bd = 1e9;
    for (const e of items) { if (e === cur) continue; const r = e.getBoundingClientRect(), c = {x:r.left + r.width / 2, y:r.top + r.height / 2};
      const vx = c.x - c0.x, vy = c.y - c0.y; if (dx && Math.sign(vx) !== dx) continue; if (dy && Math.sign(vy) !== dy) continue;
      const d = dx ? Math.abs(vx) + Math.abs(vy) * 2.5 : Math.abs(vy) + Math.abs(vx) * 2.5; if (d < bd) { bd = d; best = e; } }
    if (best) { best.focus(); if (best.dataset.bp === "tile") { sel = +best.dataset.i; draw(); root.querySelector(`.bp-tile[data-i="${sel}"]`)?.focus(); } }
  }
  function cycleTab(d){ const ids = TABS.map(t => t[0]).filter(t => t !== "exit"); tab = ids[(ids.indexOf(tab) + d + ids.length) % ids.length]; sel = 0; draw(); }
  function back(){ const cur = document.activeElement; if (cur && cur.dataset.bp !== "tab") { root.querySelector(".bp-tab.on")?.focus(); } else close(); }
  function key(e){ if (!on) return; const k = e.key, rtl = true;
    const map = {ArrowLeft:[rtl ? 1 : -1, 0], ArrowRight:[rtl ? -1 : 1, 0], ArrowUp:[0, -1], ArrowDown:[0, 1]};
    if (map[k]) { e.preventDefault(); move(...map[k]); }
    else if (k === "Enter" || k === " ") { e.preventDefault(); const a = document.activeElement; if (a && a.dataset && a.dataset.bp) activate(a); }
    else if (k === "Escape" || k === "Backspace") { e.preventDefault(); back(); }
    else if (k === "Tab") { e.preventDefault(); cycleTab(e.shiftKey ? -1 : 1); } }
  // game controllers (Xbox / PlayStation layouts through the standard mapping)
  function poll(){
    if (!on) return; const pads = navigator.getGamepads ? [...navigator.getGamepads()].filter(Boolean) : [];
    for (const p of pads) {
      const b = i => p.buttons[i] && p.buttons[i].pressed, ax = p.axes || [];
      const st = {a:b(0), b:b(1), lb:b(4), rb:b(5), up:b(12) || ax[1] < -0.6, down:b(13) || ax[1] > 0.6, left:b(14) || ax[0] < -0.6, right:b(15) || ax[0] > 0.6, start:b(9)};
      const now = performance.now();
      for (const [k, v] of Object.entries(st)) {
        const prev = lastPad[k]; lastPad[k] = v;
        const fire = v && (!prev || (["up","down","left","right"].includes(k) && now - (padHeld[k] || 0) > 220));
        if (v && !prev) padHeld[k] = now + 200;
        if (!fire) continue; if (prev) padHeld[k] = now;
        if (k === "up") move(0, -1); else if (k === "down") move(0, 1); else if (k === "left") move(1, 0); else if (k === "right") move(-1, 0);
        else if (k === "a") { const a = document.activeElement; if (a && a.dataset && a.dataset.bp) activate(a); }
        else if (k === "b") back(); else if (k === "lb") cycleTab(-1); else if (k === "rb") cycleTab(1); else if (k === "start") close();
      }
    }
    raf = requestAnimationFrame(poll);
  }
  setInterval(() => { if (on) { const c = root && root.querySelector(".bp-me .num"); if (c) c.textContent = clock(); } }, 15000);
  return {open, close, refresh(){ if (on) draw(); }, get on(){ return on; }};
}
