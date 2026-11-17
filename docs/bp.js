/* Console mode, PlayStation 5 style: the PC as a games console. Works with a controller (Xbox / PlayStation),
   the keyboard or the mouse. Two spaces (الألعاب / وسائط), a row of tiles, a hub for the focused tile,
   a game library, a control center (Home/PS button) and notifications. Loaded by app.js through hooks. */
export function makeBP(H){
  const {esc, GAMES, ownedEds, instOf, ED_TAG, PS, toast, cover, money, ent, state} = H;
  let on = false, root = null, host = null, sheet = null, raf = 0, wallRaf = 0, hideT = 0, fresh = false, lastRun = {};
  let space = "games", sel = {games:-1, media:0}, pages = [], stack = [], toasts = [], unProg = null;
  const pad = {last:{}, held:{}}, ST = {glyphs:"auto", sounds:true, wall:"autumn"};
  try { Object.assign(ST, JSON.parse(localStorage.getItem("bp-ui") || "{}")); } catch(e) {}
  const saveUI = () => { try { localStorage.setItem("bp-ui", JSON.stringify({wall:ST.wall, sort:ST.sort, toasts:ST.toasts})); } catch(e) {} };
  const S = () => state();
  const onUsers = () => (pages[pages.length - 1] || {}).t === "users";
  let busy = "";
  const hue = id => Math.round([...String(id)].reduce((a, c) => (a * 33 + c.charCodeAt(0)) >>> 0, 5381) * 137.508) % 360;   // a steady colour per account
  const initial = a => esc(((a && (a.name || a.username)) || "?").trim().charAt(0).toUpperCase());
  async function waitUser(uid, ms = 12000){ const t0 = Date.now(); while (Date.now() - t0 < ms) { const u = S().user; if (u && u.uid === uid) return true; await new Promise(r => setTimeout(r, 120)); } return false; }
  const clock = () => new Date().toLocaleTimeString("ar-SA-u-nu-latn", {hour:"numeric", minute:"2-digit"});
  const mins = gid => ((S().PLAY[gid] || {}).minutes) || 0;
  const hrs = m => m >= 60 ? (m / 60).toFixed(1).replace(/\.0$/, "") + " ساعة" : m + " دقيقة";
  const last = gid => H.lastPlayed(gid);
  const mine = () => GAMES.filter(g => ownedEds(g.id).length).sort((a, b) => last(b.id) - last(a.id));
  const title = g => esc(g.en || g.title);
  const running = gid => Object.keys(S().RUNNING).some(k => k.split("--")[0] === gid || (S().RUNNING[k] || {}).game === gid);
  const GB = n => n >= 1e9 ? `<span class="num">${(n / 1e9).toFixed(2)}</span> غيغابايت` : `<span class="num">${(n / 1e6).toFixed(0)}</span> ميغابايت`;   // numbers only in .num (font features break Arabic joining)
  function ago(t){ const d = (Date.now() - t) / 60000; return d < 60 ? "منذ " + Math.max(1, Math.round(d)) + " دقيقة" : d < 1440 ? "منذ " + Math.round(d / 60) + " ساعة" : "منذ " + Math.round(d / 1440) + " يوم"; }

  /* ---------- game lists (like PS5 "قوائم الألعاب"), kept on this PC ---------- */
  const LISTS = { all(){ try { return JSON.parse(localStorage.getItem("bp-lists") || "[]"); } catch(e) { return []; } },
    save(l){ try { localStorage.setItem("bp-lists", JSON.stringify(l)); } catch(e) {} } };

  /* ---------- sounds (made on the fly) ---------- */
  let ac = null;
  function beep(kind){ if (!ST.sounds) return; try { ac = ac || new AudioContext(); const t = ac.currentTime, o = ac.createOscillator(), g = ac.createGain();
    const f = {move:[1250, .025, .025], ok:[760, .08, .05], back:[420, .07, .04], open:[560, .14, .045], err:[190, .15, .05], note:[990, .18, .04]}[kind] || [700, .04, .03];
    o.type = kind === "err" ? "square" : "sine"; o.frequency.setValueAtTime(f[0], t);
    if (kind === "ok" || kind === "open" || kind === "note") o.frequency.exponentialRampToValueAtTime(f[0] * 1.5, t + f[1]);
    if (kind === "back") o.frequency.exponentialRampToValueAtTime(f[0] * .65, t + f[1]);
    g.gain.setValueAtTime(f[2], t); g.gain.exponentialRampToValueAtTime(.0001, t + f[1]); o.connect(g).connect(ac.destination); o.start(t); o.stop(t + f[1] + .02); } catch(e) {} }

  /* ---------- button names follow the controller in your hands ---------- */
  let padKind = "xbox", padOn = false;
  const G = b => { const k = ST.glyphs === "auto" ? padKind : ST.glyphs;
    const m = k === "ps" ? {a:"✕", b:"○", x:"□", y:"△", lb:"L1", rb:"R1", start:"OPTIONS", guide:"PS"} : {a:"A", b:"B", x:"X", y:"Y", lb:"LB", rb:"RB", start:"☰", guide:"Xbox"};
    return `<kbd class="g g-${k}-${b}">${m[b]}</kbd>`; };

  /* ---------- small icons (drawn, no image files) ---------- */
  const I = {
    search:'<svg viewBox="0 0 24 24"><circle cx="10" cy="10" r="6.5"/><path d="M15 15l6 6"/></svg>',
    gear:'<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="3"/><path d="M10.3 2.8h3.4l.5 2.4 1.7.8 2.1-1.3 2.4 2.4-1.3 2.1.8 1.7 2.4.5v3.4l-2.4.5-.8 1.7 1.3 2.1-2.4 2.4-2.1-1.3-1.7.8-.5 2.4h-3.4l-.5-2.4-1.7-.8-2.1 1.3-2.4-2.4 1.3-2.1-.8-1.7-2.4-.5v-3.4l2.4-.5.8-1.7-1.3-2.1 2.4-2.4 2.1 1.3 1.7-.8z"/></svg>',
    store:'<svg viewBox="0 0 24 24"><path d="M5 8h14l-1.2 12H6.2z"/><path d="M9 8V6a3 3 0 0 1 6 0v2"/></svg>',
    lib:'<svg viewBox="0 0 24 24"><rect x="3" y="3" width="4" height="4"/><rect x="9" y="3" width="4" height="4"/><rect x="15" y="3" width="4" height="4"/><rect x="3" y="9" width="4" height="4"/><path d="M11 13h7a3 3 0 0 1 3 3v2a2 2 0 0 1-3.4 1.4L16 18h-3l-1.6 1.4A2 2 0 0 1 8 18v-2a3 3 0 0 1 3-3z"/></svg>',
    hub:'<svg viewBox="0 0 24 24"><rect x="3" y="3" width="7" height="7"/><path d="M17 3l4 7h-8z"/><path d="M3.5 14.5l6 6M9.5 14.5l-6 6"/><circle cx="17" cy="17.5" r="3.5"/></svg>',
    media:'<svg viewBox="0 0 24 24"><rect x="3" y="4" width="13" height="10" rx="1"/><path d="M6 4v10M13 4v10M3 8h3M3 11h3M13 8h3M13 11h3"/><path d="M18 9v9.5a2 2 0 1 1-1.5-2V10l4-1v7.5a2 2 0 1 1-1.5-2"/></svg>',
    shot:'<svg viewBox="0 0 24 24"><rect x="3" y="5" width="13" height="10" rx="1"/><path d="M5 19h4"/><rect x="13" y="12" width="8" height="6" rx="1"/><circle cx="17" cy="15" r="1.6"/></svg>',
    clip:'<svg viewBox="0 0 24 24"><rect x="3" y="5" width="13" height="12" rx="1.5"/><path d="M16 9l5-3v12l-5-3z"/></svg>',
    eng:'<svg viewBox="0 0 24 24"><path d="M12 2l9 5v10l-9 5-9-5V7z"/><path d="M12 22V12M21 7l-9 5-9-5"/></svg>',
    pad:'<svg viewBox="0 0 24 24"><path d="M7 7h10a4 4 0 0 1 4 4v4a2.5 2.5 0 0 1-4.3 1.7L15 15H9l-1.7 1.7A2.5 2.5 0 0 1 3 15v-4a4 4 0 0 1 4-4z"/><path d="M7.5 10v3M6 11.5h3"/><circle cx="16" cy="10.5" r=".8"/><circle cx="17.5" cy="12.5" r=".8"/></svg>',
    switcher:'<svg viewBox="0 0 24 24"><rect x="4" y="9" width="16" height="11" rx="1.5"/><path d="M6 6h12M8 3.5h8"/></svg>',
    base:'<svg viewBox="0 0 24 24"><circle cx="12" cy="8" r="3"/><path d="M6 15c1.5-2 3.5-3 6-3s4.5 1 6 3"/><ellipse cx="12" cy="18" rx="9" ry="3"/></svg>',
    bell:'<svg viewBox="0 0 24 24"><path d="M6 17V11a6 6 0 0 1 12 0v6l1.5 2h-15z"/><path d="M10 21h4"/></svg>',
    dl:'<svg viewBox="0 0 24 24"><path d="M12 3v11M7.5 9.5L12 14l4.5-4.5"/><path d="M8 20h8"/><circle cx="12" cy="17.5" r=".9"/></svg>',
    home:'<svg viewBox="0 0 24 24"><path d="M3 11.5L12 4l9 7.5"/><path d="M5.5 10v10h13V10"/><path d="M10 20v-5h4v5"/></svg>',
    sound:'<svg viewBox="0 0 24 24"><path d="M4 9.5h4L13 5v14l-5-4.5H4z"/><path d="M16 9a4 4 0 0 1 0 6M18.5 6.5a7.5 7.5 0 0 1 0 11"/></svg>',
    mute:'<svg viewBox="0 0 24 24"><path d="M4 9.5h4L13 5v14l-5-4.5H4z"/><path d="M16.5 9.5l5 5M21.5 9.5l-5 5"/></svg>',
    power:'<svg viewBox="0 0 24 24"><path d="M12 3v8"/><path d="M7 6.5a7.5 7.5 0 1 0 10 0"/></svg>',
    more:'<svg viewBox="0 0 24 24"><circle cx="6" cy="12" r="1.6"/><circle cx="12" cy="12" r="1.6"/><circle cx="18" cy="12" r="1.6"/></svg>',
    sort:'<svg viewBox="0 0 24 24"><path d="M7 4v15M3.5 15.5L7 19l3.5-3.5"/><path d="M13 6h8M13 11h6M13 16h4"/></svg>',
    addlist:'<svg viewBox="0 0 24 24"><path d="M4 16a3 3 0 0 1 3-3h7a3 3 0 0 1 3 3v2a2 2 0 0 1-3.4 1.4L12 18H9l-1.6 1.4A2 2 0 0 1 4 18z"/><path d="M18 3v6M15 6h6"/></svg>',
    trophy:'<svg viewBox="0 0 24 24"><path d="M7 4h10v5a5 5 0 0 1-10 0z"/><path d="M7 6H4a3 3 0 0 0 3 4M17 6h3a3 3 0 0 1-3 4M12 14v4M8 21h8M9 18h6"/></svg>',
    news:'<svg viewBox="0 0 24 24"><path d="M3 10v4h3l7 4V6l-7 4z"/><path d="M17 9.5a3 3 0 0 1 0 5M19 7l2-1M19 17l2 1"/></svg>',
    tip:'<svg viewBox="0 0 24 24"><path d="M5 19l9-9"/><path d="M14 4l1 2.5L17.5 7.5 15 8.5 14 11l-1-2.5L10.5 7.5 13 6.5z"/><circle cx="19" cy="14" r="1"/><circle cx="8" cy="5" r=".9"/></svg>',
    user:'<svg viewBox="0 0 24 24"><circle cx="12" cy="8" r="4"/><path d="M4 21c1-4.5 4.2-7 8-7s7 2.5 8 7"/></svg>',
    switchu:'<svg viewBox="0 0 24 24"><circle cx="9" cy="8" r="3.4"/><path d="M3 20c.8-3.6 3.2-5.6 6-5.6 1.4 0 2.6.4 3.6 1.2"/><path d="M15 13h6l-2-2M21 17h-6l2 2"/></svg>',
    logout:'<svg viewBox="0 0 24 24"><path d="M10 4H5v16h5"/><path d="M14 8l4 4-4 4M18 12H9"/></svg>',
    heart:'<svg viewBox="0 0 24 24"><path d="M12 20s-7.5-4.6-7.5-10A4.3 4.3 0 0 1 12 7.6 4.3 4.3 0 0 1 19.5 10C19.5 15.4 12 20 12 20z"/></svg>',
    disk:'<svg viewBox="0 0 24 24"><rect x="3" y="6" width="18" height="12" rx="2"/><path d="M7 14h.01M11 14h6"/></svg>',
    screen:'<svg viewBox="0 0 24 24"><rect x="3" y="4.5" width="18" height="12" rx="1.5"/><path d="M9 20h6M12 16.5V20"/></svg>',
    family:'<svg viewBox="0 0 24 24"><circle cx="8" cy="7" r="2.6"/><circle cx="16.5" cy="9" r="2"/><path d="M3.5 19c.6-3.6 2.4-5.6 4.5-5.6s3.9 2 4.5 5.6M13 15.2c.9-.8 2.1-1.2 3.5-1.2 2 0 3.5 1.6 4 5"/></svg>',
    system:'<svg viewBox="0 0 24 24"><path d="M7 3.5h10l1.5 17h-13z"/><path d="M9.5 7h5"/></svg>',
    capture:'<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="8"/><circle cx="12" cy="12" r="3"/></svg>',
    info:'<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="8.5"/><path d="M12 11v5.5M12 7.6v.2"/></svg>',
    friends:'<svg viewBox="0 0 24 24"><circle cx="9" cy="8.5" r="3"/><circle cx="16.5" cy="9.5" r="2.4"/><path d="M3.5 19c.8-3.2 3-5 5.5-5s4.7 1.8 5.5 5M14.5 14.3c2.6-.7 5 .9 6 4.7"/></svg>'
  };

  /* ---------- open / close ---------- */
  // the console lives in its own shadow root, so the store's CSS can't change it (and its CSS can't leak out)
  async function loadSheet(){ if (sheet) return sheet; sheet = new CSSStyleSheet();
    try { const u = new URL("bp.css?v=7", import.meta.url); sheet.replaceSync(await (await fetch(u)).text()); } catch(e) {} return sheet; }
  async function open(){ if (on) return; on = true; document.body.classList.add("bp-on"); pages = H.accounts ? [{t:"users"}] : []; stack = []; space = "games"; sel.games = -1; busy = "";
    host = document.createElement("div"); host.id = "bpHost"; host.className = "nomouse"; document.body.appendChild(host);
    root = host.attachShadow({mode:"open"}); root.adoptedStyleSheets = [await loadSheet()];
    if (PS && PS.fullscreen) PS.fullscreen(true).catch(() => {}); else { try { document.documentElement.requestFullscreen(); } catch(e) {} }
    document.addEventListener("keydown", key, true); root.addEventListener("mousemove", wake); root.addEventListener("mouseover", hover);
    root.addEventListener("wheel", wheel, {passive:false});
    raf = requestAnimationFrame(poll); lastRun = Object.assign({}, S().RUNNING);
    if (PS && PS.onProgress) unProg = PS.onProgress(p => { if (!on || !p) return;
      if (p.phase === "done") notify(I.dl, (H.keyTitle(p.id) || "") + " · اكتمل التحميل");
      else if (p.phase === "error") notify(I.dl, "ما اكتمل التحميل: " + (H.keyTitle(p.id) || ""));
      const d = root && root.querySelector(`[data-progk="${CSS.escape(p.id)}"]`); if (d && p.total) d.style.width = H.pct(p) + "%"; });
    try { if (PS && PS.getSettings) Object.assign(ST, await PS.getSettings()); } catch(e) {}
    fresh = true; draw(); beep("open"); }
  function close(){ if (!on) return; on = false; document.body.classList.remove("bp-on"); stopWall(); if (host) host.remove(); host = null; root = null; pages = []; stack = [];
    document.removeEventListener("keydown", key, true); cancelAnimationFrame(raf); if (unProg) { unProg(); unProg = null; }
    if (PS && PS.fullscreen) PS.fullscreen(false).catch(() => {}); else { try { if (document.fullscreenElement) document.exitFullscreen(); } catch(e) {} }
    if (location.hash === "#bigpicture") location.hash = "#library"; }
  function wake(){ if (!host) return; host.classList.remove("nomouse"); clearTimeout(hideT); hideT = setTimeout(() => host && host.classList.add("nomouse"), 3000); }
  function hover(e){ const t = e.target.closest("[data-bp]"); if (t && !t.disabled && root.activeElement !== t && !host.classList.contains("nomouse") && inScope(t)) focusEl(t, true); }
  function wheel(e){ const r = e.target.closest(".ps-row"); if (r && !stack.length && !pages.length) { e.preventDefault(); const it = rowItems(), i = cur(); const n = Math.max(0, Math.min(it.length - 1, i + (e.deltaY > 0 || e.deltaX > 0 ? 1 : -1))); focusTile(n); } }

  /* ---------- notifications (top corner, like the PS5) ---------- */
  function notify(icon, text){ if (!on || !root || ST.toasts === false) return; beep("note"); const id = Math.random().toString(36).slice(2);
    toasts.push({id, icon, text}); drawToasts(); setTimeout(() => { toasts = toasts.filter(t => t.id !== id); drawToasts(); }, 4500); }
  function drawToasts(){ const b = root && root.querySelector("#psToasts"); if (!b) return;
    b.innerHTML = toasts.slice(-3).map(t => `<div class="ps-toast"><span class="ic">${t.icon}</span><span class="tx">${esc(t.text)}</span></div>`).join(""); }

  /* ---------- state of a game ---------- */
  function gameState(g, prefer){ const s = S(), eds = ownedEds(g.id).slice().reverse();
    if (prefer && eds.includes(prefer)) eds.unshift(prefer);
    for (const ed of eds) { const [k, inst] = instOf(g.id, ed); if (inst && s.RUNNING[k]) return {k, ed, run:true}; }
    for (const ed of eds) { const [k] = instOf(g.id, ed); if (s.PROGRESS[k]) return {k, ed, prog:s.PROGRESS[k]}; }
    for (const ed of eds) { const [k, inst] = instOf(g.id, ed); if (inst) return {k, ed, inst:true, info:inst}; }
    return {k:instOf(g.id, eds[0] || "demo")[0], ed:eds[0], none:true}; }
  function actBtn(g, prefer){ const st = gameState(g, prefer);
    if (st.run) return `<button class="ps-btn stop" data-bp="act" data-a="stop" data-g="${g.id}" data-e="${st.ed}">إغلاق اللعبة</button>`;
    if (st.prog) return `<button class="ps-btn" data-bp="act" data-a="none" data-g="${g.id}">يحمّل <span class="num">${H.pct(st.prog)}%</span></button>`;
    if (st.inst) return `<button class="ps-btn play" data-bp="act" data-a="play" data-g="${g.id}" data-e="${st.ed}">العب اللعبة</button>`;
    if (st.ed) return `<button class="ps-btn" data-bp="act" data-a="install" data-g="${g.id}" data-e="${st.ed}">تنزيل</button>`;
    return `<button class="ps-btn" data-bp="buy" data-g="${g.id}">عرض في المتجر</button>`; }

  /* ---------- the home row ---------- */
  function rowItems(){
    if (space === "media") return [{k:"mhub", n:"اكتشف"}, {k:"gallery", n:"معرض الوسائط"}, {k:"clips", n:"المقاطع"}, ...(PS && PS.engine ? [{k:"engine", n:"Premium Engine"}] : [])];
    return [{k:"store", n:"المتجر"}, {k:"welcome", n:"الترحيب"}, ...mine().slice(0, 14).map(g => ({k:"game", g:g.id, n:g.en || g.title})), ...(PS && PS.screenshots ? [{k:"gallery", n:"معرض الوسائط"}] : []), {k:"library", n:"مكتبة الألعاب"}];
  }
  const cur = () => { const it = rowItems(); let i = sel[space]; if (i < 0) i = space === "games" ? (it.findIndex(x => x.k === "game") >= 0 && mine().some(g => last(g.id)) ? it.findIndex(x => x.k === "game") : 1) : 0; return Math.max(0, Math.min(it.length - 1, i)); };
  function tileHTML(x, i){ const c = i === cur() ? " sel" : "";
    if (x.k === "game") return `<button class="ps-t game${c}" data-bp="tile" data-i="${i}" data-g="${x.g}"><span class="art" data-cover="${x.g}"></span>${running(x.g) ? '<i class="live"></i>' : ""}</button>`;
    const ic = {store:I.store, welcome:I.hub, library:I.lib, mhub:I.media, gallery:I.shot, clips:I.clip, engine:I.eng}[x.k];
    return `<button class="ps-t sys ${x.k}${c}" data-bp="tile" data-i="${i}"><span class="art">${ic}</span></button>`; }
  function labelHTML(x){ if (!x) return "";
    if (x.k !== "game") return `<span class="ps-lbl sys">${esc(x.n)}</span>`;
    const g = H.byId(x.g), st = gameState(g), ed = st.ed && ED_TAG[st.ed];
    return `<span class="ps-lbl"><i class="plat">PC</i><b>${title(g)}</b>${ed ? `<span class="ed">${esc(ed)}</span>` : ""}${running(x.g) ? '<span class="ed run">شغّالة</span>' : ""}</span>`; }

  /* the area under the row changes with the focused tile */
  function hubHTML(x){
    if (!x) return "";
    if (x.k === "welcome") return widgetsHTML();
    if (x.k === "game") return gameHubHTML(H.byId(x.g));
    if (x.k === "store") { const vis = GAMES.filter(g => !g.hidden), sale = vis.filter(g => H.saleOf(g.id)), feat = sale[0] || vis.find(g => !ownedEds(g.id).length) || vis[0];
      const best = sale.reduce((m, g) => Math.max(m, H.saleOf(g.id).pct), 0), pick = [...sale, ...vis.filter(g => !sale.includes(g))].filter(g => g !== feat).slice(0, 4);
      return `<div class="ps-hub store">${feat ? `<button class="ps-promo" data-bp="page" data-p="store"><span class="cv" data-cover="${feat.id}"></span>
          <span class="cap"><small>${sale.length ? "تخفيضات Premium Store" : "جديد في المتجر"}</small><b>${sale.length ? `خصم حتى <span class="num">${best}%</span>` : title(feat)}</b><em>${sale.length ? `<bdi dir="ltr">${title(feat)}</bdi> · ${esc(H.saleLeft(feat.id))}` : esc(feat.tagline || "")}</em></span></button>` : ""}
        <p class="ps-note">${H.SOON.length ? "انضافت ألعاب جديدة إلى قائمة «قريبًا»" : "ألعاب جديدة كل فترة"}</p>
        <div class="ps-sec">لا يفوتك</div>
        <div class="ps-cards mini">${pick.map(g => { const sl = H.saleOf(g.id); return `<button class="ps-card art mini" data-bp="gpage" data-g="${g.id}"><span class="cv" data-cover="${g.id}"></span>${sl ? `<i class="off num">-${sl.pct}%</i>` : ""}</button>`; }).join("")}
          ${H.SOON.slice(0, 4).map(x => `<button class="ps-card art mini" data-bp="soon" data-s="${x.id}"><span class="cv">${H.soonArt(x)}</span></button>`).join("")}</div></div>`; }
    if (x.k === "library") { const own = mine(), inst = own.filter(g => { const st = gameState(g); return st.inst || st.run; }), show = inst.length ? inst : own;
      return `<div class="ps-hub lib"><div class="ps-count"><span>${inst.length ? "تخزين الجهاز" : "مكتبتك"}: <b class="num">${show.length}</b></span><span>${G("a")} افتح المكتبة</span></div>
        <div class="ps-grid mini">${show.slice(0, 10).map(g => coverTile(g)).join("") || '<p class="ps-empty">مكتبتك فاضية. روح للمتجر.</p>'}</div></div>`; }
    if (x.k === "mhub") return `<div class="ps-hub media"><div class="ps-banner" id="psBanner">${bannerHTML()}</div><div class="ps-sec">من أجلك</div><div class="ps-cards" id="psForYou">${forYouHTML()}</div></div>`;
    if (x.k === "gallery") return `<div class="ps-hub"><div class="ps-hubtitle">معرض الوسائط</div><p class="ps-sub">كل لقطات الشاشة من ألعابك. اضغط F12 وأنت تلعب.</p><div class="ps-acts"><button class="ps-btn play" data-bp="page" data-p="gallery">افتح</button></div></div>`;
    if (x.k === "clips") return `<div class="ps-hub"><div class="ps-hubtitle">المقاطع</div><p class="ps-sub">سجّل وأنت تلعب بـ F9، والمقاطع تنحفظ في مجلد الفيديوهات.</p><div class="ps-acts"><button class="ps-btn play" data-bp="clips">افتح المجلد</button></div></div>`;
    if (x.k === "engine") return `<div class="ps-hub"><div class="ps-hubtitle">Premium Engine</div><p class="ps-sub">اصنع ألعابك للكمبيوتر. يفتح المحرر على سطح المكتب.</p><div class="ps-acts"><button class="ps-btn play" data-bp="engine">افتح المحرك</button></div></div>`;
    return ""; }

  function troCounts(ach){ const t = {platinum:0, gold:0, silver:0, bronze:0}; ach.list.forEach(a => { if (a.got) t[a.tier] = (t[a.tier] || 0) + 1; });
    return `<span class="tcs">${["platinum","gold","silver","bronze"].map(k => `<span class="tc">${ach.icon(k, true)}<b class="num">${t[k]}</b></span>`).join("")}</span>`; }
  function gameHubHTML(g){ const st = gameState(g), m = mins(g.id), lp = last(g.id), ach = H.achOf(g.id), s = S();
    const fr = Object.entries(s.FRIENDS).filter(([id, f]) => f.status === "ok" && s.playing(id) && (s.playing(id) === (g.en || g.title) || s.playing(id) === g.title));
    const allFr = Object.values(s.FRIENDS).filter(f => f.status === "ok");
    return `<div class="ps-hub game"><div class="ps-logo">${title(g)}</div>
      <div class="ps-acts">${actBtn(g)}<button class="ps-btn icon" data-bp="menu" data-g="${g.id}" aria-label="خيارات">${I.more}</button>${m ? `<span class="ps-time">${I.pad}<b class="num">${hrs(m)}</b></span>` : ""}</div>
      <div class="ps-cards">
        ${ach.all ? `<button class="ps-card tro" data-bp="tpage" data-g="${g.id}">${troCounts(ach)}<span class="row"><b>الجوائز</b><b class="num">${Math.round(ach.got / ach.all * 100)}%</b></span><span class="bar"><i style="width:${Math.round(ach.got / ach.all * 100)}%"></i></span></button>` : ""}
        <button class="ps-card fr" data-bp="page" data-p="friends"><div class="avs">${(fr.length ? fr.map(([, f]) => f) : allFr).slice(0, 5).map(f => `<i style="--h:${hue(f.username || "")}">${esc((f.username || "?")[0].toUpperCase())}</i>`).join("") || '<i class="none">·</i>'}</div><b>${fr.length ? "أصدقاء يلعبون هذه اللعبة" : "الأصدقاء"}</b><small>${fr.length ? `<span class="num">${fr.length}</span> من أصدقائك يلعبونها الحين` : allFr.length ? "ما أحد منهم يلعبها الحين" : "أضف أصدقاء من المتجر"}</small></button>
        <button class="ps-card info" data-bp="gpage" data-g="${g.id}"><b>تفاصيل اللعبة</b><small>${lp ? "آخر مرة لعبت " + ago(lp) : esc(g.tagline || "")}</small></button>
        ${st.prog ? `<div class="ps-card info"><b>يحمّل <span class="num">${H.pct(st.prog)}%</span></b><span class="bar"><i data-progk="${st.k}" style="width:${H.pct(st.prog)}%"></i></span></div>` : ""}
      </div></div>`; }

  let SHOTS = null, DISK = null, rowSig = "", WAVES = "";
  // flowing white lines (the PS5 library / media backdrop), drawn once
  function wavesSVG(){ if (WAVES) return WAVES; let p = "";
    for (let i = 0; i < 54; i++) { const a = i / 54; let d = "";
      for (let x = 0; x <= 100; x += 2) { const y = 62 + Math.sin(x / 13 + a * 4.2) * (16 - a * 7) + Math.sin(x / 5.5 + a * 8) * 2.5 - a * 30 + (x / 100) * (-24 + a * 34); d += (x ? " L" : "M") + x + " " + y.toFixed(2); }
      p += `<path d="${d}" style="stroke:rgba(255,255,255,${(.05 + .3 * Math.sin(a * 3.1416)).toFixed(3)})"/>`; }
    return WAVES = `<svg class="waves" viewBox="0 0 100 100" preserveAspectRatio="none">${p}</svg>`; }
  const STOREBG = '<span class="sbg store"><i></i><i></i><i></i><b></b></span>';
  function bgHTML(x){ if (x && x.k === "game") return `<span class="gbg" data-cover="${x.g}"></span><i class="shade game"></i>`;
    if (!x || x.k === "welcome") return '<canvas id="psWall"></canvas><i class="shade"></i>';
    if (x.k === "store") return STOREBG + '<i class="shade soft"></i>';
    return `<span class="sbg waves">${wavesSVG()}</span><i class="shade soft"></i>`; }
  const bgKey = x => !x || x.k === "welcome" ? "w" : x.k === "game" ? "g" + x.g : x.k === "store" ? "s" : "v";
  function loadShots(){ if (SHOTS || !PS || !PS.screenshots) return; SHOTS = [];
    Promise.all(mine().map(g => PS.screenshots(g.id).then(l => l.map(x => Object.assign(x, {g:g.id}))).catch(() => []))).then(r => { SHOTS = r.flat().sort((a, b) => b.name.localeCompare(a.name)); if (on) softRefresh(); }); }
  function bannerHTML(){ loadShots(); const x = SHOTS && SHOTS[0];
    return x ? `<button class="ps-banner-in" data-bp="shot" data-i="0"><img src="${x.thumb}" alt=""><span class="cap"><b>آخر لقطة</b><small><bdi dir="ltr">${esc((H.byId(x.g) || {}).en || "")}</bdi> · <bdi dir="ltr">${esc(x.name)}</bdi></small></span></button>`
      : `<div class="ps-banner-in empty"><b>اكتشف</b><small>لقطاتك ومقاطعك تطلع هنا. اضغط F12 وأنت تلعب.</small></div>`; }
  function forYouHTML(){ loadShots(); return SHOTS && SHOTS.length ? SHOTS.slice(1, 9).map((x, i) => `<button class="ps-card art" data-bp="shot" data-i="${i + 1}"><img class="cv" src="${x.thumb}" alt=""></button>`).join("") : ""; }

  function widgetsHTML(){
    const s = S(), onl = Object.entries(s.FRIENDS).filter(([id, f]) => f.status === "ok" && s.online(id));
    const act = Object.entries(s.FRIENDS).filter(([id, f]) => f.status === "ok" && s.playing(id));
    const pads = navigator.getGamepads ? [...navigator.getGamepads()].filter(Boolean) : [];
    const inst = H.installed ? Object.values(H.installed()) : [], gamesBytes = inst.reduce((a, v) => a + (v.size || 0), 0);
    if (!DISK && PS && PS.disk) PS.disk().then(d => { DISK = d; if (on) softRefresh(); }).catch(() => { DISK = {}; });
    const tot = DISK && DISK.total, free = DISK && DISK.free, other = tot ? Math.max(0, tot - free - gamesBytes) : 0;
    const sale = GAMES.find(g => !g.hidden && H.saleOf(g.id)), feat = sale || GAMES.find(g => !g.hidden && !ownedEds(g.id).length) || GAMES[0];
    const soon = H.SOON[0];
    const prog = Object.entries(S().PROGRESS), pg = prog[0];
    return `${pg ? `<button class="ps-dlpill" data-bp="page" data-p="downloads"><span class="ic">${I.dl}</span><b class="num">${H.pct(pg[1])}%</b><span class="bar"><i data-progk="${pg[0]}" style="width:${H.pct(pg[1])}%"></i></span><small>${esc(H.keyTitle(pg[0]))}${prog.length > 1 ? ` +<span class="num">${prog.length - 1}</span>` : ""}</small></button>` : ""}<div class="ps-widgets">
      <div class="ps-col">
        <div class="ps-w pads">${[0, 1, 2].map(i => { const p = pads[i]; return `<span class="ring${p ? " on" : ""}">${p ? `<b class="num">${i + 1}</b>` : ""}${I.pad}</span>`; }).join("")}</div>
        <div class="ps-w store-w"><h4><span class="ic">${I.dl}</span> تخزين الجهاز</h4>
          ${tot ? `<div class="kv big"><span>المساحة الخالية</span><b>${GB(free)}</b></div><div class="sbar"><i class="a" style="width:${gamesBytes / tot * 100}%"></i><i class="c" style="width:${other / tot * 100}%"></i></div>` : ""}
          <div class="kv"><span><i class="dot a"></i>الألعاب</span><b>${GB(gamesBytes)}</b></div>${tot ? `<div class="kv"><span><i class="dot c"></i>أخرى</span><b>${GB(other)}</b></div>` : ""}
          <div class="kv"><span><i class="dot b"></i>ألعاب مثبتة</span><b class="num">${inst.length}</b></div></div>
      </div>
      <button class="ps-w news" data-bp="${sale ? "gpage" : "soon"}" ${sale ? `data-g="${feat.id}"` : `data-s="${soon ? soon.id : ""}"`}>
        <span class="cv"${sale ? ` data-cover="${feat.id}"` : ""}>${sale ? "" : soon ? H.soonArt(soon) : ""}</span>
        <h4><span class="ic">${I.news}</span> الأخبار</h4><span class="cap"><b class="ltr">${sale ? esc(feat.en || feat.title) : soon ? esc(soon.title) : ""}</b><small>${sale ? "خصم " + H.saleOf(feat.id).pct + "% · " + H.saleLeft(feat.id) : soon ? "قريبًا · " + esc(soon.when) : ""}</small></span></button>
      <div class="ps-col">
        <button class="ps-w frw" data-bp="page" data-p="friends"><h4><span class="ic">${I.friends}</span> الأصدقاء المتصلون</h4>${onl.length ? `<div class="avs">${onl.slice(0, 6).map(([, f]) => `<i>${esc((f.username || "?")[0].toUpperCase())}</i>`).join("")}</div><small>${onl.length} متصل</small>` : "<small>لا يوجد أصدقاء متصلون.</small>"}</button>
        <button class="ps-w act" data-bp="page" data-p="friends"><h4><span class="ic">${I.base}</span> نشاط الأصدقاء</h4>${act.length ? act.slice(0, 2).map(([id, f]) => `<div class="kv"><span><b>@${esc(f.username)}</b> يلعب</span><span>${esc(s.playing(id))}</span></div>`).join("") : "<small>ما أحد من أصدقائك يلعب الحين.</small>"}</button>
      </div>
      <button class="ps-w feat" data-bp="gpage" data-g="${feat.id}"><span class="cv" data-cover="${feat.id}"></span><h4><span class="ic">${I.store}</span> من المتجر</h4><span class="cap"><b>${title(feat)}</b><small>${ent(feat.id).best ? "في مكتبتك" : "من " + money(Math.round(H.lowPrice(feat) * 100) / 100)}</small></span></button>
    </div>`; }

  /* ---------- full pages ---------- */
  let libTab = "installed", q = "";
  function pageHTML(P){
    if (P.t === "users") return usersHTML();
    if (P.t === "library") return libraryHTML();
    if (P.t === "list") { const L = LISTS.all().find(l => l.id === P.id); if (!L) return `<div class="ps-page"><p>القائمة انحذفت.</p></div>`;
      const gs = L.games.map(H.byId).filter(Boolean);
      return `<div class="ps-page"><header class="ps-ph"><span class="pic">${I.lib}</span><h1>${esc(L.name)}</h1></header><div class="ps-count"><span>الألعاب: <b class="num">${gs.length}</b></span><span>${G("start")} خيارات القائمة</span></div>
        <div class="ps-grid">${gs.map(g => coverTile(g, `data-list="${L.id}"`)).join("") || '<p class="ps-empty">القائمة فاضية. أضف ألعاب من زر الخيارات ⋯ على أي لعبة.</p>'}</div></div>`; }
    if (P.t === "store") return storeHTML(P);
    if (P.t === "search") return searchHTML(P);
    if (P.t === "trophies") return trophiesHTML();
    if (P.t === "friends") { const s = S(), fr = Object.entries(s.FRIENDS).filter(([, f]) => f.status === "ok").sort((a, b) => s.online(b[0]) - s.online(a[0]));
      return `<div class="ps-page"><header class="ps-ph"><span class="pic">${I.base}</span><h1>قاعدة الألعاب</h1></header><div class="ps-count"><span>الأصدقاء: <b class="num">${fr.length}</b></span><span>متصل: <b class="num">${fr.filter(([id]) => s.online(id)).length}</b></span></div>
        <div class="ps-list">${fr.map(([id, f]) => friendRow(id, f)).join("") || '<p class="ps-empty">ما عندك أصدقاء للحين. أضفهم من المتجر.</p>'}</div></div>`; }
    if (P.t === "gallery") { loadShots();
      return `<div class="ps-page"><header class="ps-ph"><span class="pic">${I.shot}</span><h1>معرض الوسائط</h1></header>
        ${!PS || !PS.screenshots ? '<p class="ps-empty">المعرض في تطبيق Premium Games.</p>' : !SHOTS ? '<p class="ps-empty">يحمّل…</p>' : SHOTS.length ? `<div class="ps-count"><span>اللقطات: <b class="num">${SHOTS.length}</b></span></div><div class="ps-grid shots">${SHOTS.map((x, i) => `<button class="ps-cover shot" data-bp="shot" data-i="${i}"><img src="${x.thumb}" alt=""><span class="nm">${esc((H.byId(x.g) || {}).en || "")}</span></button>`).join("")}</div>` : '<p class="ps-empty">ما عندك لقطات. اضغط F12 وأنت تلعب.</p>'}</div>`; }
    if (P.t === "downloads") { const s = S(), act = Object.entries(s.PROGRESS), ups = H.pendingUpdates ? H.pendingUpdates() : [];
      return `<div class="ps-page"><header class="ps-ph"><span class="pic">${I.dl}</span><h1>التنزيلات</h1></header>
        ${!PS ? '<p class="ps-empty">التنزيلات في تطبيق Premium Games.</p>' : ""}
        ${act.length ? `<div class="ps-list">${act.map(([k, p]) => `<div class="ps-row-i" data-bp="item"><span class="cvs" data-cover="${(k.split("--")[0])}"></span><span class="t"><b>${esc(H.keyTitle(k))}</b><span class="bar"><i data-progk="${k}" style="width:${H.pct(p)}%"></i></span><small class="num">${H.progText(p)}</small></span></div>`).join("")}</div>` : ""}
        ${ups.length ? `<div class="ps-sec">تحديثات</div><div class="ps-list">${ups.map(([k]) => `<button class="ps-row-i" data-bp="update" data-k="${k}"><span class="t"><b>${esc(H.keyTitle(k))}</b><small>تحديث جاهز · ${G("a")} حدّث</small></span></button>`).join("")}</div>` : ""}
        ${PS && !act.length && !ups.length ? '<p class="ps-empty">ما فيه تنزيلات. كل ألعابك محدّثة ✓</p>' : ""}</div>`; }
    if (P.t === "settings") return settingsHTML(P);
    if (P.t === "profile") return profileHTML();
    if (P.t === "game") return gamePageHTML(P);
    if (P.t === "soon") { const x = H.SOON.find(s => s.id === P.s) || {};
      return `<div class="ps-page gp"><span class="gbg">${H.soonArt(x)}</span><div class="ginfo"><div class="eyebrow">قريبًا · ${esc(x.genreAr || "")}</div><h1>${esc(x.title || "")}</h1><p>${esc(x.tagline || "")}</p>
        <div class="ps-cards"><div class="ps-card info"><b>${esc(x.when || "")}</b><small>موعد الإصدار</small></div></div></div></div>`; }
    return ""; }
  /* store cards: cover with the name and price under it */
  function storeCard(g){ const e = ent(g.id), sl = H.saleOf(g.id), w = (S().WISH || {})[g.id];
    return `<button class="ps-sc" data-bp="gpage" data-g="${g.id}"><span class="art" data-cover="${g.id}"></span>${sl ? `<i class="off num">-${sl.pct}%</i>` : ""}${w && !e.best ? `<i class="wl">${I.heart}</i>` : ""}<span class="nm">${title(g)}</span><small>${e.best ? "في مكتبتك" : "من " + money(Math.round(H.lowPrice(g) * 100) / 100)}</small></button>`; }
  const soonCard = x => `<button class="ps-sc" data-bp="soon" data-s="${x.id}"><span class="art">${H.soonArt(x)}</span><span class="nm">${esc(x.title)}</span><small>قريبًا · ${esc(x.when)}</small></button>`;
  const STABS = [["new","الأحدث"],["deals","العروض"],["soon","قريبًا"],["browse","استعرض"]];
  function storeHTML(P){ const tab = P.tab || "new", W = S().WISH || {}, ql = q.trim().toLowerCase();
    const vis = GAMES.filter(x => !x.hidden || ownedEds(x.id).length), m = x => !ql || (x.title + " " + (x.en || "") + " " + (x.genreAr || "")).toLowerCase().includes(ql);
    const sale = vis.filter(g => H.saleOf(g.id)), top = vis.slice().sort((a, b) => !!H.saleOf(b.id) - !!H.saleOf(a.id));
    const row = (t, cards) => cards ? `<div class="ps-sec">${t}</div><div class="ps-hrow">${cards}</div>` : "";
    let body = "";
    if (tab === "new") body = row("الأكثر رواجًا", top.map(storeCard).join("")) + row("العروض", sale.map(storeCard).join("")) + row("قريبًا", H.SOON.slice(0, 14).map(soonCard).join(""));
    else if (tab === "deals") body = sale.length ? `<div class="ps-grid st">${sale.map(storeCard).join("")}</div>` : '<p class="ps-empty">ما فيه عروض الحين. ارجع لاحقًا.</p>';
    else if (tab === "soon") body = `<div class="ps-grid st">${H.SOON.map(soonCard).join("")}</div>`;
    else if (tab === "wish") { const wl = vis.filter(g => W[g.id]); body = `<div class="ps-count"><span>قائمة الأمنيات: <b class="num">${wl.length}</b></span></div>` + (wl.length ? `<div class="ps-grid st">${wl.map(storeCard).join("")}</div>` : '<p class="ps-empty">قائمة أمنياتك فاضية. افتح أي لعبة واضغط ♡.</p>'); }
    else { const gs = vis.filter(m), ss = H.SOON.filter(m);
      body = `<div class="ps-count"><span>${ql ? `نتائج «${esc(q)}»` : "كل الألعاب"}: <b class="num">${gs.length + ss.length}</b></span>${ql ? `<span>${G("y")} بحث جديد</span>` : ""}</div>` + (gs.length + ss.length ? `<div class="ps-grid st">${gs.map(storeCard).join("")}${ss.map(soonCard).join("")}</div>` : `<p class="ps-empty">ما لقينا شي باسم «${esc(q)}»</p>`); }
    return `<div class="ps-page store"><header class="ps-sh"><span class="pic">${I.store}</span><h1 class="ltr">Premium Store</h1>
        <nav class="ps-tabs st">${STABS.map(([k, n]) => `<button class="ps-tab${tab === k ? " on" : ""}" data-bp="stab" data-t="${k}">${n}</button>`).join("")}</nav>
        <span class="tools"><button class="ps-ib" data-bp="search" aria-label="بحث">${I.search}</button><button class="ps-ib${tab === "wish" ? " on" : ""}" data-bp="stab" data-t="wish" aria-label="قائمة الأمنيات">${I.heart}</button></span></header>
      ${body}</div>`; }
  const QTABS = [["games","الألعاب"],["media","وسائط"],["players","اللاعبون"]];
  function searchHTML(P){ const tab = P.tab || "games", ql = q.trim().toLowerCase(), s = S();
    const vis = GAMES.filter(x => !x.hidden || ownedEds(x.id).length), hit = x => (x.title + " " + (x.en || "") + " " + (x.genreAr || "")).toLowerCase().includes(ql);
    let body = "";
    if (tab === "games") { const gs = ql ? vis.filter(hit) : vis, ss = ql ? H.SOON.filter(hit) : [];
      body = `<div class="ps-sec">${ql ? "النتائج" : "الألعاب الرائجة"}</div>` + (gs.length + ss.length ? `<div class="ps-hrow">${gs.map(storeCard).join("")}${ss.slice(0, 20).map(soonCard).join("")}</div>` : `<p class="ps-empty">ما فيه ألعاب باسم «${esc(q)}»</p>`); }
    else if (tab === "media") { loadShots(); const l = (SHOTS || []).map((x, i) => [x, i]).filter(([x]) => !ql || (((H.byId(x.g) || {}).en || "") + " " + ((H.byId(x.g) || {}).title || "")).toLowerCase().includes(ql) || x.name.toLowerCase().includes(ql));
      body = `<div class="ps-sec">${ql ? "النتائج" : "آخر اللقطات"}</div>` + (!PS || !PS.screenshots ? '<p class="ps-empty">الوسائط في تطبيق Premium Games.</p>' : l.length ? `<div class="ps-hrow">${l.slice(0, 30).map(([x, i]) => `<button class="ps-sc shot" data-bp="shot" data-i="${i}"><span class="art"><img src="${x.thumb}" alt=""></span><span class="nm">${esc((H.byId(x.g) || {}).en || "")}</span><small class="ltr">${esc(x.name)}</small></button>`).join("")}</div>` : '<p class="ps-empty">ما فيه لقطات. اضغط F12 وأنت تلعب.</p>'); }
    else { const fr = Object.entries(s.FRIENDS).filter(([, f]) => f.status === "ok" && (!ql || (f.username || "").toLowerCase().includes(ql)));
      body = `<div class="ps-sec">${ql ? "النتائج" : "أصدقاؤك"}</div>` + (fr.length ? `<div class="ps-list">${fr.map(([id, f]) => friendRow(id, f)).join("")}</div>` : `<p class="ps-empty">${ql ? `ما فيه لاعب باسم «${esc(q)}» في أصدقائك` : "ما عندك أصدقاء للحين."}</p>`); }
    const ph = {games:"البحث عن ألعاب", media:"البحث في لقطاتك", players:"البحث عن لاعبين"}[tab];
    return `<div class="ps-page light search"><nav class="ps-qtabs">${QTABS.map(([k, n]) => `<button class="ps-qt${tab === k ? " on" : ""}" data-bp="qtab" data-t="${k}">${n}</button>`).join("")}</nav>
      <button class="ps-qbox" data-bp="search">${I.search}<span>${q ? esc(q) : ph}</span></button>${body}</div>`; }
  function trophiesHTML(){ const s = S(), own = mine().filter(g => H.achOf(g.id).all), tot = {platinum:0, gold:0, silver:0, bronze:0};
    let all = 0, got = 0; own.forEach(g => { const a = H.achOf(g.id); all += a.all; got += a.got; a.list.forEach(x => { if (x.got) tot[x.tier]++; }); });
    const icon = own.length ? H.achOf(own[0].id).icon : null;
    return `<div class="ps-page trop"><header class="ps-ph"><span class="pic">${I.trophy}</span><h1>الجوائز</h1></header>
      <div class="ps-trosum">${icon ? ["platinum","gold","silver","bronze"].map(k => `<span class="tc big">${icon(k, true)}<b class="num">${tot[k]}</b></span>`).join("") : ""}<span class="pc"><b class="num">${all ? Math.round(got / all * 100) : 0}%</b><small><span class="num">${got}/${all}</span> جائزة</small></span></div>
      <div class="ps-list wide">${own.map(g => { const a = H.achOf(g.id), p = Math.round(a.got / a.all * 100);
        return `<button class="ps-row-i trow" data-bp="tpage" data-g="${g.id}"><span class="cvs" data-cover="${g.id}"></span><span class="t"><b class="ltr">${title(g)}</b><span class="bar"><i style="width:${p}%"></i></span><small>${troCounts(a)}</small></span><span class="pc num">${p}%</span></button>`; }).join("") || '<p class="ps-empty">ما عندك ألعاب فيها جوائز.</p>'}</div></div>`; }
  const coverTile = (g, extra = "", sub = "") => `<button class="ps-cover" data-bp="gpage" data-g="${g.id}" ${extra}><span class="art" data-cover="${g.id}"></span>${running(g.id) ? '<i class="live"></i>' : ""}<span class="nm">${title(g)}${sub ? `<small>${esc(sub)}</small>` : ""}</span></button>`;
  function friendRow(id, f){ const s = S(), pl = s.playing(id), onl = s.online(id);
    return `<button class="ps-row-i fr${onl ? " on" : ""}" data-bp="friend" data-f="${id}"><span class="av">${esc((f.username || "?")[0].toUpperCase())}</span><span class="t"><b>@${esc(f.username)}</b><small>${pl ? "يلعب " + esc(pl) : onl ? "متصل" : "غير متصل"}</small></span></button>`; }
  function libraryHTML(){ const own = mine(), all = GAMES.filter(g => !g.hidden || ownedEds(g.id).length);
    let gs = libTab === "installed" ? own.filter(g => { const s = gameState(g); return s.inst || s.run; }) : libTab === "owned" ? own : all;
    if (ST.sort === "az") gs = gs.slice().sort((a, b) => (a.en || a.title).localeCompare(b.en || b.title));
    const lists = LISTS.all();
    return `<div class="ps-page lib"><aside class="ps-side"><button class="ps-ib" data-bp="search" aria-label="بحث">${I.search}</button><button class="ps-ib" data-bp="sort" aria-label="فرز">${I.sort}</button>${libTab === "owned" ? `<button class="ps-ib" data-bp="newlist" aria-label="قائمة جديدة">${I.addlist}</button>` : ""}</aside>
      <header class="ps-ph"><span class="pic">${I.lib}</span><h1>مكتبة الألعاب</h1></header>
      <nav class="ps-tabs">${[["installed","مثبّت"],["owned","مكتبتك"],["all","كل الألعاب"]].map(([k, n]) => `<button class="ps-tab${libTab === k ? " on" : ""}" data-bp="libtab" data-t="${k}">${n}</button>`).join("")}<span class="hint">${G("lb")}${G("rb")}</span></nav>
      ${libTab === "owned" && lists.length ? `<div class="ps-count"><span>قوائم الألعاب: <b class="num">${lists.length}</b></span></div><div class="ps-lists">${lists.map(l => `<button class="ps-listc" data-bp="list" data-id="${l.id}"><span class="cvs">${l.games.slice(0, 3).map(id => `<span data-cover="${id}"></span>`).join("")}</span><span class="nm"><b>${esc(l.name)}</b><small class="num">${l.games.length} ألعاب</small></span></button>`).join("")}</div>` : ""}
      <div class="ps-count"><span>${libTab === "installed" ? "تخزين الجهاز" : "الكل"}: <b class="num">${gs.length}</b></span><span>فرز حسب: ${ST.sort === "az" ? "الاسم (A - Z)" : "الأحدث"}</span></div>
      <div class="ps-grid">${gs.map(g => coverTile(g)).join("") || `<p class="ps-empty">${libTab === "installed" ? "ما فيه ألعاب مثبّتة." : "مكتبتك فاضية. روح للمتجر."}</p>`}</div></div>`; }
  function gamePageHTML(P){ const g = H.byId(P.g), eds = ownedEds(g.id), ach = H.achOf(g.id), ed = P.ed || gameState(g).ed, e = (g.editions || []).find(x => x.id === ed);
    return `<div class="ps-page gp"><span class="gbg" data-cover="${g.id}"></span><div class="ginfo">
      <div class="eyebrow">${eds.length ? "في مكتبتك" : "من المتجر"}${g.tags ? " · " + g.tags.map(esc).join(" · ") : ""}</div><h1>${title(g)}</h1>
      ${eds.length > 1 ? `<div class="eds">${eds.map(x => `<span class="${x === ed ? "on" : ""}">${esc(ED_TAG[x] || x)}</span>`).join("")}<small>${G("x")} غيّر النسخة</small></div>` : ""}
      <p>${esc(g.tagline || "")}</p>
      <div class="ps-acts">${actBtn(g, ed)}<button class="ps-btn icon" data-bp="menu" data-g="${g.id}" aria-label="خيارات">${I.more}</button>${eds.length ? "" : `<button class="ps-btn icon${(S().WISH || {})[g.id] ? " on" : ""}" data-bp="wishT" data-g="${g.id}" aria-label="قائمة الأمنيات">${I.heart}</button><button class="ps-btn ghost" data-bp="buy" data-g="${g.id}">الإصدارات والأسعار</button>`}</div>
      <div class="ps-cards">${eds.length ? `<div class="ps-card info"><b class="num">${mins(g.id) ? hrs(mins(g.id)) : "—"}</b><small>وقت اللعب</small></div><div class="ps-card info"><b>${last(g.id) ? ago(last(g.id)) : "—"}</b><small>آخر مرة</small></div>` : `<div class="ps-card info"><b class="num">${money(Math.round(H.lowPrice(g) * 100) / 100)}</b><small>يبدأ من${H.saleOf(g.id) ? " · خصم " + H.saleOf(g.id).pct + "%" : ""}</small></div>`}
        ${ach.all ? `<div class="ps-card info"><b class="num">${ach.got}/${ach.all}</b><small>التروفيات</small></div>` : ""}${e && e.d ? `<div class="ps-card info"><b>${esc(e.d)}</b><small>${esc(e.name)}</small></div>` : ""}</div>
      ${ach.all ? `<div class="ps-sec" id="psTro">الجوائز <small class="num">${ach.got}/${ach.all}</small></div><div class="achs">${ach.list.slice(0, 30).map(a => `<span class="${a.got ? "got" : ""}" title="${esc(a.desc)}">${ach.icon(a.tier, a.got)} ${esc(a.name)}</span>`).join("")}${ach.all > 30 ? `<span>+${ach.all - 30}</span>` : ""}</div>` : ""}
      </div></div>`; }
  const SETS = [["accounts","المستخدمون والحسابات","user"],["family","العائلة والرقابة الأبوية","family"],["system","النظام","system"],["storage","تخزين","disk"],["sound","الصوت","sound"],["screen","الشاشة والخلفية","screen"],["acc","الملحقات","pad"],["notes","الإشعارات","bell"],["capture","عمليات الالتقاط","capture"]];
  function settingsHTML(P){ const sw = (k, n, d, bp = "set") => `<button class="ps-set" data-bp="${bp}" data-k="${k}"><span><b>${n}</b><small>${d}</small></span><i class="sw${ST[k] !== false && ST[k] ? " on" : ""}"></i></button>`;
    const row = (n, d, val, attrs) => attrs ? `<button class="ps-set" ${attrs}><span><b>${n}</b><small>${d}</small></span><i class="val">${val}</i></button>` : `<div class="ps-set ro"><span><b>${n}</b><small>${d}</small></span><i class="val">${val}</i></div>`;
    const pick = (cur, list, bp) => list.map(([k, n]) => `<button class="ps-set" data-bp="${bp}" data-v="${k}"><span><b>${n}</b></span><i class="radio${cur === k ? " on" : ""}"></i></button>`).join("");
    if (!P.cat) return `<div class="ps-page light set"><h1 class="ps-stitle">الإعدادات</h1><div class="ps-slist">${SETS.map(([k, n, ic]) => `<button class="ps-si" data-bp="scat" data-c="${k}"><span class="ic">${I[ic]}</span><b>${n}</b></button>`).join("")}</div></div>`;
    const c = SETS.find(x => x[0] === P.cat) || SETS[0], s = S();
    let body = "";
    if (c[0] === "accounts") { const acc = H.accounts ? H.accounts() : [];
      body = acc.map(a => `<button class="ps-set" data-bp="user" data-uid="${esc(a.uid)}"><span class="who"><span class="uav" style="--h:${hue(a.uid)}">${initial(a)}</span><span><b>${esc(a.name || "")}</b><small class="ltr">${a.username ? "@" + esc(a.username) : ""}</small></span></span><i class="val">${a.cur ? "الحالي" : "بدّل"}</i></button>`).join("")
        + `<button class="ps-set" data-bp="uadd"><span><b>إضافة مستخدم</b><small>سجّل دخول بحساب ثاني على هذا الجهاز</small></span><i class="val">＋</i></button>`
        + `<button class="ps-set" data-bp="switchu"><span><b>شاشة اختيار المستخدم</b><small>نفس الشاشة اللي تطلع أول ما يفتح وضع الكونسل</small></span><i class="val">${G("a")}</i></button>`
        + (s.user ? `<button class="ps-set" data-bp="uforget" data-uid="${esc(s.user.uid)}"><span><b>إزالة حسابك من هذا الجهاز</b><small>يحتاج كلمة السر عشان ترجّعه</small></span><i class="val">✕</i></button>` : ""); }
    else if (c[0] === "family") { const pr = s.PARENTAL || ST.parental || {};
      body = row("الرقابة الأبوية", pr.on ? "شغّالة: الألعاب المسموحة فقط، ووقت لعب محدد" : "موقفة", pr.on ? "شغّالة" : "موقفة")
        + `<button class="ps-set" data-bp="acctpage"><span><b>تعديل الرقابة الأبوية</b><small>تنفتح صفحة الحساب في التطبيق. تحتاج الرمز السري لو كانت شغّالة</small></span><i class="val">${G("a")}</i></button>`; }
    else if (c[0] === "system") body = (PS ? sw("consoleBoot", "شغّل الكمبيوتر على وضع الكونسل", "أول ما يشتغل الويندوز يفتح هالشاشة، مثل جهاز ألعاب") + sw("startup", "يشتغل مع الويندوز", "التطبيق يجهز بالخلفية عشان ألعابك تفتح بسرعة") + sw("consolePad", "افتح وضع الكونسل لما توصل يد تحكم", "توصّل اليد وتطلع لك هالشاشة على طول") : "")
        + row("إصدار النظام", "Premium Games", `<span class="num ltr">${esc((PS && PS.version) || "—")}</span>`)
        + `<button class="ps-set" data-bp="exit"><span><b>اطلع من وضع الكونسل</b><small>ترجع لشاشة المتجر العادية</small></span><i class="val">⏏</i></button>`;
    else if (c[0] === "storage") { const inst = H.installed ? Object.entries(H.installed()) : [], used = inst.reduce((a, [, v]) => a + (v.size || 0), 0), tot = DISK && DISK.total, free = DISK && DISK.free;
      if (!DISK && PS && PS.disk) PS.disk().then(d => { DISK = d; if (on) draw(); }).catch(() => { DISK = {}; });
      body = `<div class="ps-set ro disk"><span><b>تخزين الجهاز</b><small>${tot ? `المساحة الخالية ${GB(free)} من ${GB(tot)}` : "…"}</small></span>${tot ? `<span class="sbar"><i class="a" style="width:${used / tot * 100}%"></i><i class="c" style="width:${Math.max(0, tot - free - used) / tot * 100}%"></i></span>` : ""}</div>`
        + `<div class="ps-sec">الألعاب <small>${GB(used)}</small></div>` + (inst.length ? inst.sort((a, b) => (b[1].size || 0) - (a[1].size || 0)).map(([k, v]) => `<button class="ps-set" data-bp="menu" data-g="${esc(v.game || k.split("--")[0])}"><span class="who"><span class="cvs" data-cover="${esc(v.game || k.split("--")[0])}"></span><span><b class="ltr">${esc(v.name || H.keyTitle(k))}</b><small class="num ltr">${esc(v.version || "")}</small></span></span><i class="val">${GB(v.size || 0)}</i></button>`).join("") : row("ما فيه ألعاب مثبّتة", "", "")); }
    else if (c[0] === "sound") body = sw("sounds", "أصوات الواجهة", "صوت خفيف لما تتنقل وتختار");
    else if (c[0] === "screen") body = `<div class="ps-sec">الخلفية</div>` + pick(ST.wall || "autumn", [["autumn","خريف"],["night","ليل"],["aurora","شفق"]], "wallset");
    else if (c[0] === "acc") { const pads = navigator.getGamepads ? [...navigator.getGamepads()].filter(Boolean) : [];
      body = `<div class="ps-sec">وحدات التحكم</div>` + (pads.map((p, i) => row(isPSid(p.id) ? "يد بلايستيشن" : "يد إكس بوكس / عامة", `<span class="ltr">${esc(p.id.slice(0, 50))}</span>`, `<span class="num">${i + 1}</span>`)).join("") || row("ما فيه يد متصلة", "وصّلها واضغط أي زر", ""))
        + `<button class="ps-set" data-bp="test"><span><b>اختبر يد التحكم</b><small>شوف كل زر وعصا تشتغل</small></span><i class="val">${G("a")}</i></button>`
        + `<div class="ps-sec">شكل أزرار اليد</div>` + pick(ST.glyphs || "auto", [["auto","تلقائي حسب اليد"],["xbox","إكس بوكس (A B X Y)"],["ps","بلايستيشن (✕ ○ □ △)"]], "glyphset"); }
    else if (c[0] === "notes") body = sw("toasts", "الإشعارات المنبثقة", "تطلع فوق يمين الشاشة: تنزيلات، أصدقاء، يد التحكم", "lset");
    else if (c[0] === "capture") body = row("لقطة شاشة", "وأنت تلعب", "<kbd class=\"g\">F12</kbd>") + row("سجّل مقطع", "اضغط مرة تبدأ ومرة توقف", "<kbd class=\"g\">F9</kbd>")
        + `<button class="ps-set" data-bp="page" data-p="gallery"><span><b>معرض الوسائط</b><small>كل لقطاتك</small></span><i class="val">${G("a")}</i></button><button class="ps-set" data-bp="clips"><span><b>مجلد المقاطع</b><small>ينفتح على سطح المكتب</small></span><i class="val">${G("a")}</i></button>`;
    return `<div class="ps-page light set sub"><header class="ps-shd"><span class="ic">${I[c[2]]}</span><h1>${c[1]}</h1></header><div class="ps-list set">${body}</div></div>`; }
  function profileHTML(){ const s = S(), p = s.PROFILE || {}, own = mine(), tot = own.reduce((a, g) => a + mins(g.id), 0), tro = own.reduce((a, g) => a + H.achOf(g.id).got, 0);
    const n = p.name || (s.user && s.user.displayName) || "ضيف";
    return `<div class="ps-page"><div class="ps-prof"><span class="bigav">${esc((p.username || n || "?")[0].toUpperCase())}</span><div><h1>${esc(n)}</h1><b class="ltr">${p.username ? "@" + esc(p.username) : ""}</b><small>${p.pid ? "PG ID " + esc(p.pid) : ""}</small></div></div>
      <div class="ps-cards"><div class="ps-card info"><b class="num">${own.length}</b><small>ألعاب</small></div><div class="ps-card info"><b class="num">${hrs(tot)}</b><small>وقت اللعب</small></div><div class="ps-card info"><b class="num">${tro}</b><small>تروفي</small></div><div class="ps-card info"><b class="num">${Object.values(s.FRIENDS).filter(f => f.status === "ok").length}</b><small>أصدقاء</small></div></div>
      <div class="ps-acts"><button class="ps-btn" data-bp="page" data-p="friends">الأصدقاء</button><button class="ps-btn ghost" data-bp="exit">اطلع من وضع الكونسل</button></div></div>`; }

  /* ---------- who's using this controller? (first screen, like the PS5) ---------- */
  function usersHTML(){ const acc = H.accounts ? H.accounts() : [];
    return `<div class="ps-users"><span class="clock num">${clock()}</span>
      <h1>مرحبًا بك في <span class="ltr">Premium Games</span></h1><p>من يستخدم وحدة التحكم هذه؟</p>
      <div class="ps-urow" id="psUrow"><button class="ps-u add" data-bp="uadd"><span class="ring">+</span><b>إضافة مستخدم</b></button>
        ${acc.map(a => `<button class="ps-u${a.cur ? " cur" : ""}" data-bp="user" data-uid="${esc(a.uid)}"><i class="padn">${I.pad}<b class="num">1</b></i><span class="ring" style="--h:${hue(a.uid)}">${initial(a)}</span><b>${esc(a.name || a.username || "")}</b><small>${G("start")} خيارات</small></button>`).join("")}</div>
      <button class="ps-upow" data-bp="upower" aria-label="الطاقة">${I.power}</button></div>`; }
  function enterHome(){ busy = ""; pages = []; stack = []; space = "games"; sel.games = -1; fresh = true; beep("open"); draw(); }
  async function signInAs(uid){ const a = (H.accounts() || []).find(x => x.uid === uid) || {};
    busy = "جاري الدخول…"; draw();
    let ok = false; try { ok = await H.switchUser(uid); } catch(e) {}
    if (ok && await waitUser(uid)) return enterHome();
    busy = ""; fresh = true; push({t:"login", email:a.email || "", pass:"", locked:true, uid}); }
  async function loginGo(L){ if (!/^[^\s@]+@[^\s@]+\.[^\s@]{2,}$/.test(L.email.trim())) { L.err = "اكتب إيميل صحيح"; beep("err"); return draw(); }
    if (L.pass.length < 6) { L.err = "كلمة السر 6 أحرف أو أكثر"; beep("err"); return draw(); }
    busy = "جاري تسجيل الدخول…"; L.err = ""; draw();
    try { const c = await H.signIn(L.email, L.pass); if (await waitUser(c.user.uid)) return enterHome(); L.err = "ما قدرنا نفتح الحساب. جرّب مرة ثانية"; }
    catch(e) { L.err = H.authErr(e); beep("err"); }
    busy = ""; draw(); }

  /* ---------- overlays ---------- */
  const CC = [["switcher","المبدل",I.switcher],["base","قاعدة الألعاب",I.base],["profile","الملف الشخصي",null],["notes","الإشعارات",I.bell],["downloads","التنزيلات",I.dl],["home","الرئيسية",I.home],["sound","الأصوات",null],["pads","وحدات التحكم",I.pad],["power","الطاقة",I.power]];
  function layerHTML(L){
    if (L.t === "cc") { const s = S(), prof = s.PROFILE || {}, n = H.notes().length, run = Object.entries(s.RUNNING);
      return `<div class="ps-cc">${ccPanelHTML(L)}
        <nav class="ps-ccbar">${CC.map(([k, nm, ic]) => `<button class="ps-cci${L.panel === k ? " act" : ""}" data-bp="cc" data-k="${k}" data-tip="${nm}">${k === "profile" ? `<span class="av">${esc((prof.username || "?")[0].toUpperCase())}</span>` : k === "sound" ? (ST.sounds ? I.sound : I.mute) : ic}${k === "notes" && n ? `<i class="badge num">${n}</i>` : ""}${k === "switcher" && run.length ? '<i class="dot"></i>' : ""}${k === "downloads" && Object.keys(s.PROGRESS).length ? '<i class="dot"></i>' : ""}</button>`).join("")}</nav>
        <div class="ps-cctip" id="psTip"></div></div>`; }
    if (L.t === "menu") { const g = H.byId(L.g), st = gameState(g), eds = ownedEds(g.id), lists = LISTS.all(), inList = L.list;
      return `<div class="ps-menu"><h3>${title(g)}</h3>
        <button class="ps-mi" data-bp="gpage" data-g="${g.id}">تفاصيل اللعبة</button>
        ${eds.length ? `<button class="ps-mi" data-bp="addto" data-g="${g.id}">أضف إلى قائمة ألعاب</button>` : ""}
        ${inList ? `<button class="ps-mi" data-bp="rmfrom" data-g="${g.id}" data-id="${inList}">احذف من هذي القائمة</button>` : ""}
        ${st.inst && PS ? `<button class="ps-mi" data-bp="mact" data-a="shortcut" data-k="${st.k}">أيقونة على سطح المكتب</button><button class="ps-mi" data-bp="mact" data-a="folder" data-k="${st.k}">افتح مكان الملفات</button><button class="ps-mi" data-bp="mact" data-a="verify" data-k="${st.k}">تحقّق من ملفات اللعبة</button><button class="ps-mi danger" data-bp="mact" data-a="uninstall" data-k="${st.k}">احذف</button>` : ""}
        ${st.run ? `<button class="ps-mi danger" data-bp="act" data-a="stop" data-g="${g.id}" data-e="${st.ed}">إغلاق اللعبة</button>` : ""}
        <button class="ps-mi" data-bp="buy" data-g="${g.id}">صفحة المتجر</button></div>`; }
    if (L.t === "lists") { const lists = LISTS.all();
      return `<div class="ps-dlg"><h2>أضف إلى قائمة</h2>${lists.map(l => `<button class="ps-mi${l.games.includes(L.g) ? " sel" : ""}" data-bp="toggleList" data-id="${l.id}" data-g="${L.g}">${esc(l.name)} <small class="num">(${l.games.length})</small></button>`).join("")}<button class="ps-mi" data-bp="newlist" data-g="${L.g}">＋ قائمة جديدة</button></div>`; }
    if (L.t === "listmenu") return `<div class="ps-menu"><h3>القائمة</h3><button class="ps-mi" data-bp="renlist" data-id="${L.id}">غيّر الاسم</button><button class="ps-mi danger" data-bp="dellist" data-id="${L.id}">احذف القائمة</button></div>`;
    if (L.t === "kbd") return kbdHTML(L);
    if (L.t === "power") return `<div class="ps-dlg"><h2>الطاقة</h2>${powerBtns()}</div>`;
    if (L.t === "me") { const s = S(), p = s.PROFILE || {}, n = p.name || (s.user && s.user.displayName) || "";
      return `<div class="ps-menu me"><div class="who"><span class="uav" style="--h:${hue(s.user ? s.user.uid : "")}">${initial({name:n, username:p.username})}</span><span><b>${esc(n)}</b><small class="ltr">${p.username ? "@" + esc(p.username) : ""}</small></span></div>
        <div class="ps-mi stat"><i class="dot"></i>متصل</div>
        <button class="ps-mi" data-bp="page" data-p="profile"><span class="mic">${I.user}</span>الملف الشخصي</button>
        <button class="ps-mi" data-bp="page" data-p="trophies"><span class="mic">${I.trophy}</span>الجوائز</button>
        <button class="ps-mi" data-bp="switchu"><span class="mic">${I.switchu}</span>تبديل المستخدم</button>
        <button class="ps-mi" data-bp="logout"><span class="mic">${I.logout}</span>تسجيل الخروج</button></div>`; }
    if (L.t === "umenu") { const a = (H.accounts() || []).find(x => x.uid === L.uid) || {};
      return `<div class="ps-menu"><h3>${esc(a.name || "")}</h3><button class="ps-mi danger" data-bp="uforget" data-uid="${esc(L.uid)}">إزالة المستخدم من هذا الجهاز</button></div>`; }
    if (L.t === "login") return `<div class="ps-dlg login"><h2>${L.email && L.locked ? "سجّل دخولك من جديد" : "إضافة مستخدم"}</h2><p>${L.locked ? "انتهت جلسة هذا الحساب على الجهاز. اكتب كلمة السر." : "سجّل دخولك بحساب Premium Games."}</p>
        <button class="ps-field" data-bp="lfield" data-f="email"><small>الإيميل</small><span class="ltr">${L.email ? esc(L.email) : '<i>name@example.com</i>'}</span></button>
        <button class="ps-field" data-bp="lfield" data-f="pass"><small>كلمة السر</small><span class="ltr">${L.pass ? "•".repeat(L.pass.length) : "<i>••••••</i>"}</span></button>
        ${L.err ? `<p class="err">${esc(L.err)}</p>` : ""}
        <button class="ps-mi ok" data-bp="lgo">تسجيل الدخول</button><button class="ps-mi" data-bp="lnew">ما عندك حساب؟ أنشئ حساب جديد</button></div>`;
    if (L.t === "confirm") return `<div class="ps-dlg"><h2>${esc(L.title)}</h2><p>${esc(L.text || "")}</p><button class="ps-mi danger" data-bp="yes">${esc(L.ok)}</button><button class="ps-mi" data-bp="back">إلغاء</button></div>`;
    if (L.t === "view") { const x = SHOTS[L.i]; return `<div class="ps-view"><img src="${x.thumb}" alt=""><div class="cap">${esc(x.name)} · <span class="num">${L.i + 1}/${SHOTS.length}</span><button class="ps-btn ghost" data-bp="showshot">افتح في المجلد</button></div></div>`; }
    if (L.t === "friend") { const f = S().FRIENDS[L.f] || {}, quick = ["يلا نلعب؟", "دقيقة وجاي", "أحسنت 👏", "باي، أشوفك بعدين"];
      return `<div class="ps-dlg"><h2>@${esc(f.username || "")}</h2><button class="ps-mi" data-bp="call" data-f="${L.f}">📞 مكالمة صوتية</button>
        ${quick.map(t => `<button class="ps-mi" data-bp="qmsg" data-f="${L.f}" data-m="${esc(t)}">💬 ${esc(t)}</button>`).join("")}<button class="ps-mi" data-bp="qtype" data-f="${L.f}">⌨ اكتب رسالة</button></div>`; }
    if (L.t === "test") return `<div class="ps-dlg wide"><h2>اختبار يد التحكم</h2><div id="bpTest" class="ps-test"></div><p class="muted">اضغط ${G("b")} مرتين ورا بعض للرجوع</p></div>`;
    if (L.t === "glyph") return `<div class="ps-dlg"><h2>شكل أزرار اليد</h2>${[["auto","تلقائي حسب اليد"],["xbox","إكس بوكس"],["ps","بلايستيشن"]].map(([k, n]) => `<button class="ps-mi${ST.glyphs === k ? " sel" : ""}" data-bp="glyphset" data-v="${k}">${n}</button>`).join("")}</div>`;
    if (L.t === "wall") return `<div class="ps-dlg"><h2>الخلفية</h2>${[["autumn","خريف"],["night","ليل"],["aurora","شفق"]].map(([k, n]) => `<button class="ps-mi${ST.wall === k ? " sel" : ""}" data-bp="wallset" data-v="${k}">${n}</button>`).join("")}</div>`;
    return ""; }
  function ccPanelHTML(L){ const s = S();
    const card = (ic, a, b, extra = "", attrs = "") => `<button class="ps-ccc" ${attrs}><span class="ic">${ic}</span>${extra}<span class="cap"><small>${a}</small><b>${b}</b></span></button>`;
    if (!L.panel) { loadShots(); const sh = SHOTS && SHOTS[0], sale = GAMES.find(g => !g.hidden && H.saleOf(g.id)), run = Object.entries(s.RUNNING)[0];
      return `<div class="ps-cccards">${run ? card(I.pad, "شغّالة الحين", esc(run[1].name || H.keyTitle(run[0])), "", `data-bp="qstop" data-k="${run[0]}"`) : ""}
        ${sh ? card(I.shot, "أنشئ مؤخرًا", "لقطة شاشة جديدة", `<img src="${sh.thumb}" alt="">`, `data-bp="shot" data-i="0"`) : ""}
        ${sale ? card(I.news, "خصم " + H.saleOf(sale.id).pct + "%", title(sale), `<span class="cv" data-cover="${sale.id}"></span>`, `data-bp="gpage" data-g="${sale.id}"`) : ""}
        ${card(I.tip, "اكتشف", "نصائح وضع الكونسل", "", `data-bp="cc" data-k="tips"`)}</div>`; }
    if (L.panel === "tips") return `<div class="ps-ccpanel"><h3>نصائح</h3><ul class="tips"><li>${G("guide")} أو F1: مركز التحكم في أي وقت</li><li>${G("start")}: خيارات اللعبة المحددة</li><li>${G("lb")}${G("rb")}: بين الألعاب والوسائط</li><li>${G("y")}: البحث</li><li>F12 وأنت تلعب: لقطة شاشة · F9: سجّل مقطع</li><li>Shift+Tab أو زر اليد الرئيسي داخل اللعبة: قائمة اللعبة</li></ul></div>`;
    if (L.panel === "switcher") { const run = Object.entries(s.RUNNING), rec = mine().filter(g => last(g.id)).slice(0, 4);
      return `<div class="ps-ccpanel"><h3>المبدل</h3><div class="ps-cccards">${run.map(([k, r]) => card(I.pad, "شغّالة · " + G("a") + " إغلاق", esc(r.name || H.keyTitle(k)), "", `data-bp="qstop" data-k="${k}"`)).join("")}
        ${rec.map(g => `<button class="ps-ccc art" data-bp="gpage" data-g="${g.id}"><span class="cv" data-cover="${g.id}"></span><span class="cap"><small>${ago(last(g.id))}</small><b>${title(g)}</b></span></button>`).join("")}${!run.length && !rec.length ? '<p class="ps-empty">ما فيه شي مفتوح.</p>' : ""}</div></div>`; }
    if (L.panel === "notes") { const n = H.notes();
      return `<div class="ps-ccpanel"><h3>الإشعارات</h3><div class="ps-list">${n.length ? n.slice(0, 8).map(x => `<div class="ps-row-i note" data-bp="item"><span class="em">${x.i}</span><span class="t"><b>${esc(x.t)}</b></span></div>`).join("") : '<p class="ps-empty">ما فيه إشعارات جديدة.</p>'}</div></div>`; }
    if (L.panel === "downloads") { const act = Object.entries(s.PROGRESS);
      return `<div class="ps-ccpanel"><h3>التنزيلات</h3><div class="ps-list">${act.map(([k, p]) => `<div class="ps-row-i" data-bp="item"><span class="t"><b>${esc(H.keyTitle(k))}</b><span class="bar"><i data-progk="${k}" style="width:${H.pct(p)}%"></i></span><small class="num">${H.progText(p)}</small></span></div>`).join("") || '<p class="ps-empty">ما فيه تنزيلات الحين.</p>'}</div><button class="ps-mi" data-bp="page" data-p="downloads">كل التنزيلات والتحديثات</button></div>`; }
    if (L.panel === "pads") { const pads = navigator.getGamepads ? [...navigator.getGamepads()].filter(Boolean) : [];
      return `<div class="ps-ccpanel"><h3>وحدات التحكم</h3><div class="ps-list">${pads.map((p, i) => `<div class="ps-row-i" data-bp="item"><span class="em num">${i + 1}</span><span class="t"><b>${/054c|playstation|dualsense|dualshock|wireless controller/i.test(p.id) ? "يد بلايستيشن" : "يد إكس بوكس / عامة"}</b><small class="ltr">${esc(p.id.slice(0, 60))}</small></span></div>`).join("") || '<p class="ps-empty">ما فيه يد متصلة. وصّلها وأضغط أي زر.</p>'}</div>
        <button class="ps-mi" data-bp="test">اختبر يد التحكم</button><button class="ps-mi" data-bp="glyph">شكل أزرار اليد</button></div>`; }
    if (L.panel === "power") return `<div class="ps-ccpanel"><h3>الطاقة</h3>${powerBtns()}</div>`;
    return ""; }
  function powerBtns(){ return `${PS && PS.power ? `<button class="ps-mi" data-bp="pw" data-w="sleep">🌙 وضع السكون</button><button class="ps-mi" data-bp="pw" data-w="restart">↻ إعادة تشغيل الكمبيوتر</button><button class="ps-mi" data-bp="pw" data-w="shutdown">⏻ إطفاء الكمبيوتر</button><button class="ps-mi" data-bp="pw" data-w="desktop">🖥 ارجع لسطح المكتب</button><button class="ps-mi" data-bp="pw" data-w="quit">✕ اقفل Premium Games</button>` : ""}
        <button class="ps-mi" data-bp="exit">⏏ اطلع من وضع الكونسل</button>`; }

  /* on-screen keyboard (Arabic / English) */
  const KB = {ar:["ض ص ث ق ف غ ع ه خ ح ج", "ش س ي ب ل ا ت ن م ك ط", "ئ ء ؤ ر ى ة و ز ظ د ذ", "1 2 3 4 5 6 7 8 9 0 ؟"], en:["q w e r t y u i o p", "a s d f g h j k l", "z x c v b n m", "1 2 3 4 5 6 7 8 9 0 ?"], mail:["@ . _ - + ! # $ % &"]};
  function kbdHTML(L){ const rows = L.lang === "en" && L.mail ? [...KB.en, ...KB.mail] : KB[L.lang], up = c => L.caps && L.lang === "en" ? c.toUpperCase() : c;
    return `<div class="ps-kbd"><div class="in" dir="${L.mail ? "ltr" : "auto"}">${L.v ? esc(L.secret ? "•".repeat(L.v.length) : L.v) : `<span>${esc(L.ph || "")}</span>`}<i class="caret"></i></div>
    ${rows.map(r => `<div class="kr" dir="${L.lang === "ar" ? "rtl" : "ltr"}">${r.split(" ").map(c => `<button class="k" data-bp="k" data-c="${esc(up(c))}">${esc(up(c))}</button>`).join("")}</div>`).join("")}
    <div class="kr">${L.lang === "en" ? `<button class="k w2${L.caps ? " on" : ""}" data-bp="kcaps">⇧</button>` : ""}${L.mail ? "" : `<button class="k w2" data-bp="klang">${L.lang === "ar" ? "EN" : "عربي"}</button>`}<button class="k w5" data-bp="k" data-c=" ">مسافة</button><button class="k w2" data-bp="kdel">⌫ ${G("x")}</button><button class="k w2 ok" data-bp="kok">تم ${G("start")}</button></div></div>`; }

  /* ---------- animated wallpaper (no image files) ---------- */
  let wall = null;
  function startWall(){ const c = root && root.querySelector("#psWall"); if (!c) return;
    if (wall && wall.c === c) return; stopWall();
    const g = c.getContext("2d"), W = c.width = Math.round(innerWidth / 2), Hh = c.height = Math.round(innerHeight / 2), theme = c.dataset.theme || ST.wall || "autumn";
    if (theme === "bokeh") return bokeh(c, g, W, Hh);
    const parts = Array.from({length:theme === "night" ? 140 : 70}, () => ({x:Math.random() * W, y:Math.random() * Hh, s:Math.random(), r:Math.random() * 6.28, v:.2 + Math.random() * .6}));
    const pal = {autumn:{top:"#3a1e0e", mid:"#a2541c", glow:"rgba(255,190,110,.55)", hill:["#4a2410", "#2a140a", "#160b06"], p:["#f08a24", "#c4401a", "#f6b73c", "#8a2c12"]},
      night:{top:"#04060f", mid:"#0d1d3d", glow:"rgba(120,160,255,.25)", hill:["#0c1426", "#080e1c", "#050811"], p:["#ffffff", "#bcd2ff"]},
      aurora:{top:"#020a12", mid:"#073040", glow:"rgba(60,255,190,.25)", hill:["#062126", "#04161a", "#020c0f"], p:["#7fffd4", "#9fd8ff"]}}[theme] || {};
    wall = {c, t:0}; const draw = () => { if (!wall || wall.c !== c) return; wall.t += 1;
      const t = wall.t / 60, sky = g.createLinearGradient(0, 0, 0, Hh); sky.addColorStop(0, pal.top); sky.addColorStop(.62, pal.mid); sky.addColorStop(1, pal.hill[2]);
      g.fillStyle = sky; g.fillRect(0, 0, W, Hh);
      const rg = g.createRadialGradient(W * .62, Hh * .42, 10, W * .62, Hh * .42, W * .55); rg.addColorStop(0, pal.glow); rg.addColorStop(1, "rgba(0,0,0,0)"); g.fillStyle = rg; g.fillRect(0, 0, W, Hh);
      if (theme === "aurora") for (let b = 0; b < 3; b++) { g.beginPath(); for (let x = 0; x <= W; x += 8) { const y = Hh * (.22 + b * .07) + Math.sin(x / 90 + t * .6 + b) * 18; x ? g.lineTo(x, y) : g.moveTo(x, y); }
        g.strokeStyle = `rgba(${b ? "120,200,255" : "60,255,180"},.18)`; g.lineWidth = 26 - b * 6; g.stroke(); }
      pal.hill.forEach((col, i) => { g.beginPath(); g.moveTo(0, Hh); for (let x = 0; x <= W; x += 10) g.lineTo(x, Hh * (.58 + i * .1) + Math.sin(x / (130 - i * 25) + i * 2) * (28 - i * 6) + Math.sin(x / 41 + i) * 6); g.lineTo(W, Hh); g.fillStyle = col; g.fill(); });
      for (const p of parts) { p.y += p.v * (theme === "night" ? .15 : 1); p.x += Math.sin(t + p.s * 9) * .5 - (theme === "night" ? 0 : .25); p.r += .02 + p.s * .03;
        if (p.y > Hh + 10) { p.y = -10; p.x = Math.random() * W; } if (p.x < -10) p.x = W + 10;
        g.save(); g.translate(p.x, p.y); g.rotate(p.r); g.fillStyle = pal.p[Math.floor(p.s * pal.p.length)];
        if (theme === "autumn") { g.globalAlpha = .75; g.beginPath(); g.ellipse(0, 0, 2.5 + p.s * 3, 1.3 + p.s * 1.6, 0, 0, 6.28); g.fill(); }
        else { g.globalAlpha = .35 + .5 * Math.abs(Math.sin(t * 2 + p.s * 20)); g.fillRect(0, 0, 1 + p.s, 1 + p.s); }
        g.restore(); }
      wallRaf = requestAnimationFrame(draw); };
    draw(); }
  // soft light and drifting golden dots (the PS5 user screen)
  function bokeh(c, g, W, Hh){ const dots = Array.from({length:150}, () => { const band = Math.random(); return {x:Math.random() * W, y:Hh * (.42 + band * .5) + (Math.random() - .5) * Hh * .25, r:.6 + Math.random() ** 3 * 9, a:Math.random() * 6.28, v:.05 + Math.random() * .25}; });
    wall = {c, t:0}; const draw = () => { if (!wall || wall.c !== c) return; const t = ++wall.t / 60;
      const bg = g.createLinearGradient(0, 0, W, Hh); bg.addColorStop(0, "#c9c3bb"); bg.addColorStop(.45, "#9aa4b8"); bg.addColorStop(1, "#6e7d99"); g.fillStyle = bg; g.fillRect(0, 0, W, Hh);
      const lg = g.createRadialGradient(W * .12, -Hh * .1, 10, W * .12, -Hh * .1, W * .7); lg.addColorStop(0, "rgba(255,236,214,.95)"); lg.addColorStop(.5, "rgba(255,226,200,.25)"); lg.addColorStop(1, "rgba(255,255,255,0)"); g.fillStyle = lg; g.fillRect(0, 0, W, Hh);
      for (const d of dots) { d.x += d.v; d.a += .01; if (d.x > W + 12) d.x = -12;
        const al = .35 + .4 * Math.abs(Math.sin(t * .7 + d.a)), rr = d.r * 2.2, gr = g.createRadialGradient(d.x, d.y + Math.sin(t * .4 + d.a) * 3, 0, d.x, d.y + Math.sin(t * .4 + d.a) * 3, rr);
        gr.addColorStop(0, `rgba(255,214,140,${al})`); gr.addColorStop(.45, `rgba(255,200,120,${al * .55})`); gr.addColorStop(1, "rgba(255,200,120,0)"); g.fillStyle = gr; g.fillRect(d.x - rr, d.y - rr + Math.sin(t * .4 + d.a) * 3, rr * 2, rr * 2); }
      wallRaf = requestAnimationFrame(draw); };
    draw(); }
  function stopWall(){ cancelAnimationFrame(wallRaf); wall = null; }

  /* ---------- drawing ---------- */
  function topHTML(){ const s = S(), prof = s.PROFILE || {};
    return `<header class="ps-top"><nav class="spaces"><button class="sp${space === "games" ? " on" : ""}" data-bp="space" data-v="games">الألعاب</button><button class="sp${space === "media" ? " on" : ""}" data-bp="space" data-v="media">وسائط</button></nav>
      <div class="right"><button class="ps-ib" data-bp="page" data-p="search" aria-label="بحث">${I.search}</button><button class="ps-ib" data-bp="page" data-p="settings" aria-label="الإعدادات">${I.gear}</button>
      <button class="ps-ib av" data-bp="me" aria-label="${esc(prof.name || prof.username || "الملف الشخصي")}" style="--h:${hue(s.user ? s.user.uid : "")}">${initial(prof)}${Object.keys(s.PROGRESS).length ? '<i class="spin"></i>' : ""}</button><span class="clock num">${clock()}</span></div></header>`; }
  function homeHTML(){ const it = rowItems(), i = cur(), x = it[i], bgGame = x && x.k === "game";
    rowSig = it.map(r => r.k + (r.g || "")).join();
    return `<div class="ps-bg" data-k="${bgKey(x)}">${bgHTML(x)}</div>
      ${topHTML()}<div class="ps-row"><div class="ps-strip" id="psStrip">${it.map(tileHTML).join("")}</div><div id="psLbl">${labelHTML(x)}</div></div>
      <main class="ps-main" id="psHub">${hubHTML(x)}</main>`; }
  function draw(){
    if (!on || !root) return;
    const ae = root.activeElement;
    const keep = !fresh && ae && ae.dataset.bp ? `[data-bp="${ae.dataset.bp}"]` + ["g","t","a","i","k","f","s","c","w","v","e","p","id","uid"].filter(k => ae.dataset[k] != null).map(k => `[data-${k}="${CSS.escape(ae.dataset[k])}"]`).join("") : "";
    const P = pages[pages.length - 1], sc0 = (root.querySelector(".ps-page") || {}).scrollTop || 0, hs0 = (root.querySelector(".ps-main") || {}).scrollLeft || 0;
    root.innerHTML = `<div id="bp">` + (P ? `<div class="ps-bg${P.t === "users" ? " users" : ""}">${pageBg(P)}</div>${pageHTML(P)}` : homeHTML())
      + stack.map((L, i) => `<div class="ps-layer t-${L.t}" data-li="${i}">${layerHTML(L)}</div>`).join("")
      + `<div id="psToasts" class="ps-toasts"></div><footer class="ps-help${P && (P.t === "search" || P.t === "settings") ? " light" : ""}"><span>${G("a")} ${P && P.t === "users" ? "تحديد" : "اختيار"}</span>${P && P.t === "users" ? "" : `<span>${G("b")} رجوع</span><span>${G("guide")} مركز التحكم</span>`}${!P ? `<span>${G("lb")}${G("rb")} الألعاب / الوسائط</span>` : ""}${P && P.t === "users" ? "" : `<span>${G("start")} خيارات</span>`}</footer>${busy ? `<div class="ps-busy"><i class="spin"></i><b>${esc(busy)}</b></div>` : ""}</div>`;
    paint(root); drawToasts();
    const pg = root.querySelector(".ps-page"); if (pg) pg.scrollTop = sc0; const mn = root.querySelector(".ps-main"); if (mn) mn.scrollLeft = hs0;
    if (root.querySelector("#psWall")) startWall(); else stopWall();
    const sc = scope();
    let f = keep && sc.querySelector(keep);
    const first = list => { for (const q of list) { const e = sc.querySelector(q); if (e) return e; } return null; };   // in this order of preference
    if (!f) f = stack.length ? first([".ps-field", ".k", ".ps-ccpanel [data-bp]", "button.ps-mi", "[data-bp]"]) : P && P.t === "users" ? first([".ps-u.cur", ".ps-u[data-bp=user]", ".ps-u"]) : P ? first([".ps-page .ps-qbox", ".ps-page .ps-si", ".ps-page .ps-btn.play", ".ps-page .ps-btn", ".ps-page .ps-sc", ".ps-page button.ps-set", ".ps-page .ps-cover", ".ps-page .ps-listc", ".ps-page .ps-row-i", ".ps-page .ps-set", ".ps-page .ps-search", ".ps-page [data-bp]"]) : sc.querySelector(`.ps-t[data-i="${cur()}"]`);
    if (stack.length && stack[stack.length - 1].t === "cc" && !keep) f = sc.querySelector(`.ps-cci[data-k="${stack[stack.length - 1].panel || "home"}"]`) || f;
    fresh = false;
    if (f) focusEl(f, true);
    if (!P) placeStrip();
    if (P && P.tro) { P.tro = false; const t = root.querySelector("#psTro"); if (t) t.scrollIntoView({block:"center"}); }
    if (stack.length && stack[stack.length - 1].t === "test") testLoop();
  }
  function pageBg(P){ const t = P.t;
    if (t === "game" || t === "soon") return "";
    if (t === "users") return '<canvas id="psWall" data-theme="bokeh"></canvas><i class="shade page"></i>';
    if (t === "search" || t === "settings") return '<span class="sbg light"></span>';
    if (t === "store") return STOREBG + '<i class="shade store"></i>';
    if (t === "library" || t === "list" || t === "trophies" || t === "gallery" || t === "downloads" || t === "friends" || t === "profile") return `<span class="sbg waves">${wavesSVG()}</span><i class="shade soft"></i>`;
    return '<canvas id="psWall"></canvas><i class="shade page"></i>'; }
  function paint(el){ el.querySelectorAll("[data-cover]").forEach(x => cover(x, x.dataset.cover)); el.querySelectorAll("[data-bp]").forEach(x => x.onclick = () => activate(x)); }
  const scope = () => { const l = root ? root.querySelectorAll(".ps-layer") : []; return l.length ? l[l.length - 1] : root; };
  const inScope = el => scope().contains(el);
  function focusEl(el, quiet){ el.focus({preventScroll:true});
    if (!el.closest(".ps-strip")) el.scrollIntoView({block:"nearest", inline:"nearest", behavior:quiet ? "auto" : "smooth"});
    if (!quiet) beep("move");
    if (el.classList.contains("ps-cci")) { const t = root.querySelector("#psTip"); if (t) { t.textContent = el.dataset.tip; const r = el.getBoundingClientRect(); t.style.left = (r.left + r.width / 2) + "px"; } }
    if (el.dataset.bp === "tile" && !pages.length) focusTile(+el.dataset.i, true);
    if (el.classList.contains("ps-u")) centerUser(el); }
  function centerUser(el){ const r = root.querySelector("#psUrow"); if (!r) return; r.style.transform = `translateX(${innerWidth / 2 - (el.offsetLeft + el.offsetWidth / 2)}px)`; }
  /* the focused tile stays at the same place on screen; the strip slides under it (like the PS5) */
  function placeStrip(){ const st = root && root.querySelector("#psStrip"), t = st && st.querySelector(".ps-t.sel"); if (!t) return;
    // measured from the final sizes, not offsetLeft/offsetWidth (those are mid-transition while tiles grow/shrink)
    const vw = innerWidth / 100, small = 5.6 * vw, gap = .6 * vw, big = 9.2 * vw, idx = [...st.children].indexOf(t);
    const anchor = Math.max(24, innerWidth * .09); st.style.transform = `translateX(${anchor - idx * (small + gap)}px)`;
    const lb = root.querySelector("#psLbl");
    // like the PS5: the other tiles stay right next to the focused one; its name sits under the small tiles, beside the big one
    if (lb) { lb.style.left = (anchor + big + gap * 2) + "px"; lb.style.top = (t.offsetTop + big - lb.offsetHeight) + "px"; } }
  function focusTile(i, fromFocus){ const it = rowItems(); if (it.map(r => r.k + (r.g || "")).join() !== rowSig) { sel[space] = i; fresh = true; return draw(); } i = Math.max(0, Math.min(it.length - 1, i));
    const changed = i !== cur(); sel[space] = i;
    const st = root.querySelector("#psStrip"); if (!st) return;
    st.querySelectorAll(".ps-t").forEach(t => t.classList.toggle("sel", +t.dataset.i === i));
    const t = st.querySelector(`.ps-t[data-i="${i}"]`); if (!fromFocus && t) t.focus({preventScroll:true});
    placeStrip();
    if (changed) { const x = it[i], lb = root.querySelector("#psLbl"), hub = root.querySelector("#psHub"), bg = root.querySelector(".ps-bg");
      if (lb) { lb.innerHTML = labelHTML(x); lb.classList.remove("in"); void lb.offsetWidth; lb.classList.add("in"); placeStrip(); }
      if (hub) { hub.innerHTML = hubHTML(x); hub.scrollLeft = 0; paint(hub); }
      if (bg && bg.dataset.k !== bgKey(x)) { bg.dataset.k = bgKey(x); stopWall(); bg.innerHTML = bgHTML(x); paint(bg); if (bg.querySelector("#psWall")) startWall(); }
      if (!fromFocus) beep("move"); } }
  function softRefresh(){ if (!on || !root || stack.some(L => L.t === "kbd")) return; const P = pages[pages.length - 1];
    if (!P && !stack.length && rowItems().map(r => r.k + (r.g || "")).join() === rowSig) { const hub = root.querySelector("#psHub"), ae = root.activeElement, tp = root.querySelector(".ps-top"); if (tp && !tp.contains(ae)) { tp.outerHTML = topHTML(); paint(root.querySelector(".ps-top")); } if (hub && !hub.contains(ae)) { hub.innerHTML = hubHTML(rowItems()[cur()]); paint(hub); return; } }
    draw(); }
  function push(L){ stack.push(L); fresh = true; beep("open"); draw(); }
  function pop(){ if (!stack.length) return false; stack.pop(); beep("back"); draw(); return true; }
  function openPage(P){ stack = []; pages.push(P); fresh = true; beep("open"); draw(); }
  function popPage(){ if (!pages.length || onUsers()) return false; pages.pop(); fresh = !pages.length ? false : true; beep("back"); draw(); return true; }
  function goHome(){ stack = []; pages = []; fresh = true; draw(); }

  /* ---------- actions ---------- */
  const fake = d => { const b = document.createElement("button"); for (const [k, v] of Object.entries(d)) b.dataset[k] = v; return b; };
  async function activate(el){
    const k = el.dataset.bp, top = stack[stack.length - 1]; if (!k) return; beep("ok");
    if (k === "tile") { const x = rowItems()[+el.dataset.i]; if (!x) return; if (+el.dataset.i !== cur()) return focusTile(+el.dataset.i);
      if (x.k === "game") { const g = H.byId(x.g), st = gameState(g); if (st.inst && !st.run) return activate(fake({bp:"act", a:"play", g:g.id, e:st.ed})); return openPage({t:"game", g:x.g}); }
      if (x.k === "store") { q = ""; return openPage({t:"store"}); } if (x.k === "library") return openPage({t:"library"}); if (x.k === "gallery" || x.k === "mhub") return openPage({t:"gallery"});
      if (x.k === "clips") return activate(fake({bp:"clips"})); if (x.k === "engine") return activate(fake({bp:"engine"}));
      if (x.k === "welcome") { const f = root.querySelector("#psHub [data-bp]"); if (f) focusEl(f); } return; }
    if (busy) return;
    if (k === "user") return signInAs(el.dataset.uid);
    if (k === "uadd") return push({t:"login", email:"", pass:""});
    if (k === "upower") return push({t:"power"});
    if (k === "uforget") { const uid = el.dataset.uid, a = (H.accounts() || []).find(x => x.uid === uid) || {}; stack = [];
      return push({t:"confirm", title:"تزيل " + (a.name || "") + " من هذا الجهاز؟", text:"الحساب نفسه ما ينحذف. تحتاج كلمة السر عشان ترجّعه.", ok:"إزالة", yes:async () => { const me = S().user && S().user.uid === uid; await H.forgetUser(uid); if (me || onUsers()) { pages = [{t:"users"}]; stack = []; fresh = true; } }}); }
    if (k === "lfield") { const L = top, f = el.dataset.f;
      return push({t:"kbd", v:L[f] || "", lang:"en", mail:f === "email", secret:f === "pass", ph:f === "email" ? "الإيميل" : "كلمة السر", done:v => { L[f] = f === "email" ? v.trim() : v; L.err = ""; }}); }
    if (k === "lgo") return loginGo(top);
    if (k === "lnew") { close(); H.signUp(); return; }
    if (k === "me") return push({t:"me"});
    if (k === "switchu") { pages = [{t:"users"}]; stack = []; fresh = true; beep("open"); return draw(); }
    if (k === "logout") { pages = [{t:"users"}]; stack = []; fresh = true; try { await H.logout(); } catch(e) {} beep("back"); return draw(); }
    if (k === "space") return setSpace(el.dataset.v);
    if (k === "page") { if (el.dataset.p === "downloads" || el.dataset.p === "friends" || el.dataset.p === "trophies" || el.dataset.p === "profile") stack = []; if (el.dataset.p === "search" || el.dataset.p === "store") q = ""; return openPage({t:el.dataset.p}); }
    if (k === "gpage") { stack = []; return openPage({t:"game", g:el.dataset.g}); }
    if (k === "soon") { if (!el.dataset.s) return; stack = []; return openPage({t:"soon", s:el.dataset.s}); }
    if (k === "back") return pop();
    if (k === "buy") { close(); location.hash = el.dataset.g; return; }
    if (k === "act") { const a = el.dataset.a, g = H.byId(el.dataset.g), st = gameState(g, el.dataset.e);
      if (a === "none") return; if (!PS) { beep("err"); return notify(I.pad, "التشغيل من تطبيق Premium Games على الكمبيوتر"); }
      if (el.isConnected) el.disabled = true; stack = stack.filter(L => L.t !== "menu");
      try { if (a === "play") { await PS.launch(st.k, st.ed); notify(I.pad, "يشغّل " + (g.en || g.title) + "…"); } else if (a === "stop") await PS.stop(st.k); else if (a === "install") { await PS.install(st.k, {edition:st.ed}); notify(I.dl, "بدأ التنزيل: " + (g.en || g.title)); } }
      catch(e) { beep("err"); notify(I.pad, String(e && e.message || e)); }
      setTimeout(softRefresh, 500); return; }
    if (k === "menu") return push({t:"menu", g:el.dataset.g, list:(pages[pages.length - 1] || {}).t === "list" ? pages[pages.length - 1].id : ""});
    if (k === "mact") { const a = el.dataset.a, key2 = el.dataset.k; stack = [];
      if (a === "uninstall") return push({t:"confirm", title:"تحذف " + H.keyTitle(key2) + "؟", text:"تقدر تنزّلها مرة ثانية من مكتبتك.", ok:"احذف", yes:async () => { await PS.uninstall(key2); notify(I.dl, "انحذفت " + H.keyTitle(key2)); }});
      try { if (a === "shortcut") { await PS.shortcut(key2); notify(I.home, "انحطت الأيقونة على سطح المكتب"); } else if (a === "folder") await PS.showFolder(key2); else if (a === "verify") { notify(I.dl, "يتحقق من الملفات…"); const r = await PS.verify(key2); notify(I.dl, r && r.fixed ? "تصلّحت " + r.fixed + " ملفات" : "الملفات سليمة ✓"); } }
      catch(e) { notify(I.pad, String(e && e.message || e)); } return draw(); }
    if (k === "addto") { stack = []; return push({t:"lists", g:el.dataset.g}); }
    if (k === "toggleList") { const l = LISTS.all(), L = l.find(x => x.id === el.dataset.id); if (L) { const i = L.games.indexOf(el.dataset.g); if (i >= 0) L.games.splice(i, 1); else L.games.unshift(el.dataset.g); LISTS.save(l); notify(I.lib, i >= 0 ? "انشالت من «" + L.name + "»" : "انضافت إلى «" + L.name + "»"); } return draw(); }
    if (k === "rmfrom") { const l = LISTS.all(), L = l.find(x => x.id === el.dataset.id); if (L) { L.games = L.games.filter(x => x !== el.dataset.g); LISTS.save(l); } stack = []; return draw(); }
    if (k === "newlist") { const g = el.dataset.g; stack = stack.filter(L => L.t !== "lists");
      return push({t:"kbd", v:"", lang:"ar", ph:"اسم القائمة", done:v => { v = v.trim().slice(0, 30); if (!v) return; const l = LISTS.all(); l.push({id:Date.now().toString(36), name:v, games:g ? [g] : []}); LISTS.save(l); notify(I.lib, "انعملت القائمة «" + v + "»"); if (g) libTab = "owned"; }}); }
    if (k === "list") return openPage({t:"list", id:el.dataset.id});
    if (k === "renlist") { const id = el.dataset.id, L = LISTS.all().find(x => x.id === id); stack = []; return push({t:"kbd", v:L ? L.name : "", lang:"ar", ph:"اسم القائمة", done:v => { const l = LISTS.all(), x = l.find(y => y.id === id); if (x && v.trim()) { x.name = v.trim().slice(0, 30); LISTS.save(l); } }}); }
    if (k === "dellist") { const id = el.dataset.id; stack = []; return push({t:"confirm", title:"تحذف القائمة؟", text:"الألعاب نفسها ما تنحذف.", ok:"احذف", yes:() => { LISTS.save(LISTS.all().filter(x => x.id !== id)); if ((pages[pages.length - 1] || {}).id === id) pages.pop(); }}); }
    if (k === "libtab") { libTab = el.dataset.t; fresh = true; return draw(); }
    if (k === "sort") { ST.sort = ST.sort === "az" ? "recent" : "az"; saveUI(); return draw(); }
    if (k === "search") return push({t:"kbd", v:q, lang:/[a-z]/i.test(q) ? "en" : "ar", ph:"ابحث…", done:v => { q = v; const P = pages[pages.length - 1]; if (P && P.t === "store" && v.trim()) { P.tab = "browse"; fresh = true; } }});
    if (k === "stab") { const P = pages[pages.length - 1]; P.tab = el.dataset.t; fresh = true; beep("move"); return draw(); }
    if (k === "qtab") { const P = pages[pages.length - 1]; P.tab = el.dataset.t; beep("move"); return draw(); }
    if (k === "scat") return openPage({t:"settings", cat:el.dataset.c});
    if (k === "tpage") { stack = []; return openPage({t:"game", g:el.dataset.g, tro:true}); }
    if (k === "lset") { ST[el.dataset.k] = ST[el.dataset.k] === false; saveUI(); return draw(); }
    if (k === "acctpage") { close(); location.hash = "#account"; return; }
    if (k === "wishT") { const g = el.dataset.g, w = !!(S().WISH || {})[g]; try { await H.wish(g, !w); notify(I.heart, w ? "انشالت من قائمة الأمنيات" : "انضافت لقائمة الأمنيات"); } catch(e) { notify(I.heart, String(e.message || e)); } return setTimeout(draw, 300); }
    if (k === "k") { top.v = (top.v + el.dataset.c).slice(0, 80); return draw(); }
    if (k === "kcaps") { top.caps = !top.caps; return draw(); }
    if (k === "kdel") { top.v = top.v.slice(0, -1); return draw(); }
    if (k === "klang") { top.lang = top.lang === "ar" ? "en" : "ar"; return draw(); }
    if (k === "kok") { stack.pop(); top.done(top.v); return draw(); }
    if (k === "friend") return push({t:"friend", f:el.dataset.f});
    if (k === "call") { stack = []; draw(); return H.call(el.dataset.f); }
    if (k === "qmsg") { try { await H.sendMsg(el.dataset.f, el.dataset.m); notify(I.friends, "انرسلت ✓"); } catch(e) { notify(I.friends, "ما انرسلت"); } return pop(); }
    if (k === "qtype") { const f = el.dataset.f; stack.pop(); return push({t:"kbd", v:"", lang:"ar", ph:"اكتب رسالتك…", done:async v => { if (v.trim()) { try { await H.sendMsg(f, v.trim()); notify(I.friends, "انرسلت ✓"); } catch(e) { notify(I.friends, "ما انرسلت"); } } }}); }
    if (k === "shot") { if (!SHOTS || !SHOTS[+el.dataset.i]) return; stack = stack.filter(L => L.t !== "cc"); return push({t:"view", i:+el.dataset.i}); }
    if (k === "showshot") { const x = SHOTS[top.i]; if (PS && PS.openShots) PS.openShots(x.file); return; }
    if (k === "clips") { if (PS && PS.openClips) PS.openClips().catch(() => {}); else notify(I.clip, "المقاطع في تطبيق Premium Games"); return; }
    if (k === "engine") { if (!PS || !PS.engine) return notify(I.eng, "المحرك في تطبيق Premium Games");
      try { const s = await PS.engine.status(); const v = s.installed[0]; if (!v) { notify(I.eng, "ثبّت Premium Engine من تبويب Engine في التطبيق"); return; } await PS.engine.launch(v.version); notify(I.eng, "يفتح Premium Engine على سطح المكتب…"); } catch(e) { notify(I.eng, String(e && e.message || e)); } return; }
    if (k === "set") { ST[el.dataset.k] = !ST[el.dataset.k]; if (PS && PS.setSettings) { try { Object.assign(ST, await PS.setSettings({[el.dataset.k]:ST[el.dataset.k]})); } catch(e) {} } return draw(); }
    if (k === "glyph") return push({t:"glyph"});
    if (k === "glyphset") { ST.glyphs = el.dataset.v; if (PS && PS.setSettings) PS.setSettings({glyphs:ST.glyphs}).catch(() => {}); return top && top.t === "glyph" ? pop() : draw(); }
    if (k === "wall") return push({t:"wall"});
    if (k === "wallset") { ST.wall = el.dataset.v; saveUI(); stopWall(); return top && top.t === "wall" ? pop() : draw(); }
    if (k === "test") return push({t:"test"});
    if (k === "exit") return close();
    if (k === "cc") { const c = el.dataset.k;
      if (c === "home") return goHome();
      if (c === "base") return openPage({t:"friends"});
      if (c === "profile") return openPage({t:"profile"});
      if (c === "sound") { ST.sounds = !ST.sounds; if (PS && PS.setSettings) PS.setSettings({sounds:ST.sounds}).catch(() => {}); beep("ok"); return draw(); }
      top.panel = top.panel === c ? null : c; fresh = true; return draw(); }
    if (k === "pw") { const w = el.dataset.w;
      if (w === "restart" || w === "shutdown") return push({t:"confirm", title:w === "restart" ? "إعادة تشغيل الكمبيوتر؟" : "إطفاء الكمبيوتر؟", text:Object.keys(S().RUNNING).length ? "اللعبة الشغالة بتنحفظ وتنقفل أول." : "", ok:w === "restart" ? "أعد التشغيل" : "أطفئ", yes:() => PS.power(w)});
      if (w === "quit") return push({t:"confirm", title:"تقفل Premium Games؟", text:"", ok:"اقفل", yes:() => PS.power("quit")});
      stack = []; draw(); try { await PS.power(w); } catch(e) { notify(I.power, String(e.message || e)); } return; }
    if (k === "yes") { const fn = top.yes; stack = []; draw(); try { await fn(); } catch(e) { notify(I.pad, String(e.message || e)); } return draw(); }
    if (k === "qstop") { try { await PS.stop(el.dataset.k); } catch(e) {} stack = []; return setTimeout(draw, 400); }
    if (k === "update") { try { await PS.install(el.dataset.k); notify(I.dl, "بدأ التحديث"); } catch(e) { notify(I.dl, String(e.message || e)); } return setTimeout(draw, 500); }
  }
  function setSpace(v){ if (v === space && !pages.length) return; space = v; stack = []; pages = []; fresh = true; beep("move"); draw(); }
  function context(btn){
    const top = stack[stack.length - 1], P = pages[pages.length - 1], a = root.activeElement;
    if (onUsers() && btn !== "start") return;
    if (btn === "guide") { if (top && top.t === "cc") return pop(); stack = []; return push({t:"cc", panel:null}); }
    if (btn === "x") { if (top && top.t === "kbd") { top.v = top.v.slice(0, -1); return draw(); }
      if (!top && P && P.t === "game") { const eds = ownedEds(P.g); if (eds.length > 1) { const c = P.ed || gameState(H.byId(P.g)).ed; P.ed = eds[(eds.indexOf(c) + 1) % eds.length]; beep("move"); draw(); } } return; }
    if (btn === "y") { if (top && top.t === "kbd") { top.v += " "; return draw(); } if (!top) { if (P && (P.t === "store" || P.t === "search" || P.t === "library")) return activate(root.querySelector('[data-bp="search"]')); return openPage({t:"search"}); } return; }
    if (btn === "start") { if (top && top.t === "kbd") { stack.pop(); top.done(top.v); beep("ok"); return draw(); }
      if (onUsers()) { if (top && top.t === "umenu") return pop(); if (!top && a && a.dataset.uid) return push({t:"umenu", uid:a.dataset.uid}); return; }
      if (top && top.t === "menu") return pop();
      const g = a && a.dataset && a.dataset.g; if (g && !top) return push({t:"menu", g, list:P && P.t === "list" ? P.id : ""});
      if (!top && P && P.t === "list") return push({t:"listmenu", id:P.id});
      if (!top && !P) { const x = rowItems()[cur()]; if (x && x.k === "game") return push({t:"menu", g:x.g}); }
      return; }
  }

  /* ---------- moving the focus ---------- */
  function move(dx, dy){
    const sc = scope(), items = [...sc.querySelectorAll("[data-bp]")].filter(e => !e.disabled && e.offsetParent !== null), c = root.activeElement;
    if (!items.length) return; if (!items.includes(c)) return focusEl(items[0]);
    // inside the home row: left / right walk the tiles
    if (c.dataset.bp === "tile" && dx) { const n = cur() + dx; if (n >= 0 && n < rowItems().length) focusTile(n); return; }
    const r0 = c.getBoundingClientRect(), c0 = {x:r0.left + r0.width / 2, y:r0.top + r0.height / 2};
    let best = null, bd = 1e9;
    for (const e of items) { if (e === c) continue; const r = e.getBoundingClientRect(); if (e.dataset.bp === "tile" && (r.right < 0 || r.left > innerWidth)) continue; if (dy && e.closest(".ps-side") && !c.closest(".ps-side")) continue;   // the side icons are reached with left / right only
      const p = {x:r.left + r.width / 2, y:r.top + r.height / 2}, vx = p.x - c0.x, vy = p.y - c0.y;
      if (dx && (Math.sign(vx) !== dx || Math.abs(vx) < 4)) continue; if (dy && (Math.sign(vy) !== dy || Math.abs(vy) < 4)) continue;
      // measured edge to edge, so a wide button right under you wins over a small one whose centre happens to be closer
      const gx = Math.max(0, r.left - r0.right, r0.left - r.right), gy = Math.max(0, r.top - r0.bottom, r0.top - r.bottom);
      const d = dx ? gx + Math.abs(vx) * .25 + gy * 3 : gy + Math.abs(vy) * .25 + gx * 2.2; if (d < bd) { bd = d; best = e; } }
    if (best && best.dataset.bp === "tile" && c.dataset.bp !== "tile") best = root.querySelector(`.ps-t[data-i="${cur()}"]`) || best;   // back into the row: the selected tile
    if (best) focusEl(best);
  }
  function back(){ const top = stack[stack.length - 1];
    if (top) return pop(); if (onUsers()) return beep("back"); if (popPage()) return;
    const a = root.activeElement;
    if (a && a.dataset.bp !== "tile") { beep("back"); const t = root.querySelector(`.ps-t[data-i="${cur()}"]`); if (t) t.focus({preventScroll:true}); return; }
    if (space === "media") return setSpace("games");
    if (cur() !== 1 && space === "games") { focusTile(1); return; }
    beep("back"); }
  function viewStep(d){ const top = stack[stack.length - 1]; if (top && top.t === "view") { top.i = (top.i + d + SHOTS.length) % SHOTS.length; draw(); return true; } return false; }
  function shoulder(d){ const P = pages[pages.length - 1]; if (onUsers()) return;
    if (P && P.t === "store") { const t = [...STABS.map(x => x[0]), "wish"]; P.tab = t[(t.indexOf(P.tab || "new") + d + t.length) % t.length]; fresh = true; beep("move"); return draw(); }
    if (P && P.t === "search") { const t = QTABS.map(x => x[0]); P.tab = t[(t.indexOf(P.tab || "games") + d + t.length) % t.length]; beep("move"); return draw(); }
    if (P && P.t === "library") { const t = ["installed","owned","all"]; libTab = t[(t.indexOf(libTab) + d + 3) % 3]; fresh = true; beep("move"); return draw(); }
    if (!P && !stack.length) setSpace(space === "games" ? "media" : "games"); }
  function key(e){ if (!on) return; const k = e.key, top = stack[stack.length - 1];
    if (document.activeElement !== host) { const ae = document.activeElement; if (ae && ae.blur) ae.blur(); if (!(top && top.t === "kbd")) { const f = scope().querySelector("[data-bp]"); if (f) f.focus({preventScroll:true}); } }
    const map = {ArrowLeft:[-1, 0], ArrowRight:[1, 0], ArrowUp:[0, -1], ArrowDown:[0, 1]};
    if (top && top.t === "kbd" && k.length === 1 && !e.ctrlKey && !e.altKey) { e.preventDefault(); top.v = (top.v + k).slice(0, 80); return draw(); }
    if (top && top.t === "kbd" && k === "Backspace") { e.preventDefault(); top.v = top.v.slice(0, -1); return draw(); }
    if (map[k]) { e.preventDefault(); host.classList.add("nomouse"); if (k === "ArrowLeft" || k === "ArrowRight") { if (viewStep(k === "ArrowLeft" ? -1 : 1)) return; } move(...map[k]); }
    else if (k === "Enter" || k === " ") { e.preventDefault(); if (top && top.t === "kbd" && k === "Enter") return context("start"); const a = root.activeElement; if (a && a.dataset && a.dataset.bp) activate(a); }
    else if (k === "Escape" || k === "Backspace") { e.preventDefault(); back(); }
    else if (k === "Tab") { e.preventDefault(); shoulder(e.shiftKey ? -1 : 1); }
    else if (k === "F1" || k === "Home" || k === "q" || k === "Q") { e.preventDefault(); context("guide"); }
    else if (k === "F2" || k === "o" || k === "O" || k === "ContextMenu") { e.preventDefault(); context("start"); }
    else if (k === "/") { e.preventDefault(); context("y"); }
    else if (k === "x" || k === "X") { e.preventDefault(); context("x"); } }

  /* ---------- game controllers (Xbox / PlayStation, standard mapping) ---------- */
  let testBack = 0;
  const isPS = id => /054c|playstation|dualsense|dualshock|wireless controller/i.test(id);
  const isPSid = id => /054c|playstation|dualsense|dualshock|wireless controller/i.test(id);
  addEventListener("gamepadconnected", e => { padOn = true; padKind = isPS(e.gamepad.id) ? "ps" : "xbox";
    if (on) { notify(I.pad, "انوصلت يد التحكم " + (e.gamepad.index + 1)); draw(); } else if (H.padConnected) H.padConnected(); });
  addEventListener("gamepaddisconnected", e => { padOn = [...(navigator.getGamepads ? navigator.getGamepads() : [])].some(Boolean); if (on) { notify(I.pad, "انفصلت يد التحكم " + (e.gamepad.index + 1)); draw(); } });
  function poll(){
    if (!on) return; const pads = navigator.getGamepads ? [...navigator.getGamepads()].filter(Boolean) : [];
    if (pads.length && !padOn) { padOn = true; padKind = isPS(pads[0].id) ? "ps" : "xbox"; draw(); }
    const top = stack[stack.length - 1];
    if (top && top.t === "test") { raf = requestAnimationFrame(poll); return; }
    for (const p of pads) {
      const b = i => p.buttons[i] && p.buttons[i].pressed, ax = p.axes || [];
      const st = {a:b(0), b:b(1), x:b(2), y:b(3), lb:b(4), rb:b(5), lt:b(6), rt:b(7), sel:b(8), start:b(9), guide:b(16), up:b(12) || ax[1] < -0.55, down:b(13) || ax[1] > 0.55, left:b(14) || ax[0] < -0.55, right:b(15) || ax[0] > 0.55};
      const now = performance.now();
      for (const [k, v] of Object.entries(st)) {
        const prev = pad.last[p.index + k]; pad.last[p.index + k] = v;
        const rep = ["up","down","left","right","lt","rt"].includes(k);
        if (v && !prev) pad.held[p.index + k] = now + 280;
        const fire = v && (!prev || (rep && now > pad.held[p.index + k]));
        if (!fire) continue; if (prev) pad.held[p.index + k] = now + 85;
        if (host) host.classList.add("nomouse");
        if (k === "up") move(0, -1); else if (k === "down") move(0, 1);
        else if (k === "left") { if (!viewStep(-1)) move(-1, 0); } else if (k === "right") { if (!viewStep(1)) move(1, 0); }
        else if (k === "a") { const a = root.activeElement; if (a && a.dataset && a.dataset.bp) activate(a); }
        else if (k === "b") back(); else if (k === "lb") shoulder(-1); else if (k === "rb") shoulder(1);
        else if (k === "lt" || k === "rt") { const m = root.querySelector(".ps-page") || root.querySelector(".ps-main"); if (m && !stack.length) m.scrollBy({top:(k === "rt" ? 1 : -1) * innerHeight * .6, behavior:"smooth"}); }
        else if (k === "x" || k === "y") context(k);
        else if (k === "start") context("start");
        else if (k === "guide") context("guide");
        else if (k === "sel") { if (!onUsers()) { stack = []; openPage({t:"downloads"}); } }
      }
    }
    raf = requestAnimationFrame(poll);
  }
  function testLoop(){
    const el = root && root.querySelector("#bpTest"); if (!el || !on) return;
    const p = [...(navigator.getGamepads ? navigator.getGamepads() : [])].filter(Boolean)[0];
    const names = ["A/✕","B/○","X/□","Y/△","LB","RB","LT","RT","Back","Start","L3","R3","↑","↓","←","→","Home"];
    if (!p) el.innerHTML = '<p>وصّل يد التحكم واضغط أي زر.</p>';
    else { el.innerHTML = `<p class="muted ltr">${esc(p.id)}</p><div class="tb">${names.map((n, i) => `<span class="${p.buttons[i] && p.buttons[i].pressed ? "on" : ""}">${n}</span>`).join("")}</div>
      <div class="ta">${[0, 2].map(i => `<span class="stick"><i style="transform:translate(${(p.axes[i] || 0) * 28}px,${(p.axes[i + 1] || 0) * 28}px)"></i></span>`).join("")}</div>`;
      const bb = p.buttons[1] && p.buttons[1].pressed; if (bb && !pad.last.testB) { const t = performance.now(); if (t - testBack < 700) { testBack = 0; pad.last.testB = bb; stack.pop(); return draw(); } testBack = t; } pad.last.testB = bb; }
    requestAnimationFrame(testLoop);
  }

  /* ---------- live updates ---------- */
  function refresh(){ if (!on) return;
    const run = S().RUNNING, ended = Object.keys(lastRun).filter(k => !run[k]), started = Object.keys(run).filter(k => !lastRun[k]); lastRun = Object.assign({}, run);
    if (ended.length && PS && PS.focusMain) { PS.focusMain().catch(() => {}); if (PS.fullscreen) PS.fullscreen(true).catch(() => {}); }   // back to the console screen when a game closes
    if (started.length) SHOTS = null; if (ended.length) SHOTS = null;
    softRefresh(); }
  setInterval(() => { if (on) { const c = root && root.querySelector(".ps-top .clock, .ps-users .clock"); if (c) c.textContent = clock(); } }, 15000);
  return {open, close, refresh, get on(){ return on; }};
}
