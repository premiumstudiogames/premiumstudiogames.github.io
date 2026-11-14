/* Console mode: the PC as a games console. Full screen, works entirely with a game controller
   (Xbox or PlayStation), the keyboard or the mouse. Loaded by app.js; uses its data through the hooks it passes in. */
export function makeBP(H){
  const {esc, GAMES, ownedEds, instOf, ED_TAG, PS, toast, cover, money, ent, state} = H;
  let fresh = false, on = false, tab = "home", root = null, raf = 0, stack = [], heroG = "", lastRun = {}, hideT = 0;
  const pad = {last:{}, held:{}}, ST = {glyphs:"auto", sounds:true};
  const TABS = [["home","الرئيسية"],["library","المكتبة"],["store","المتجر"],["friends","الأصدقاء"],["downloads","التحميلات"],["shots","اللقطات"],["settings","الإعدادات"]];
  const clock = () => new Date().toLocaleTimeString("ar-SA-u-nu-latn", {hour:"numeric", minute:"2-digit"});
  const S = () => state();
  const mins = gid => ((S().PLAY[gid] || {}).minutes) || 0;
  const hrs = m => m >= 60 ? (m / 60).toFixed(1).replace(/\.0$/, "") + " ساعة" : m + " دقيقة";
  const last = gid => H.lastPlayed(gid);
  const mine = () => GAMES.filter(g => ownedEds(g.id).length).sort((a, b) => last(b.id) - last(a.id));

  /* ---------- sounds (made on the fly, no files) ---------- */
  let ac = null;
  function beep(kind){ if (!ST.sounds) return; try { ac = ac || new AudioContext(); const t = ac.currentTime, o = ac.createOscillator(), g = ac.createGain();
    const f = {move:[880, 0.035, .035], ok:[660, 0.09, .06], back:[330, 0.08, .05], open:[520, 0.12, .05], err:[180, 0.15, .06]}[kind] || [700, .04, .03];
    o.type = kind === "err" ? "square" : "sine"; o.frequency.setValueAtTime(f[0], t); if (kind === "ok" || kind === "open") o.frequency.exponentialRampToValueAtTime(f[0] * 1.5, t + f[1]);
    if (kind === "back") o.frequency.exponentialRampToValueAtTime(f[0] * .6, t + f[1]);
    g.gain.setValueAtTime(f[2], t); g.gain.exponentialRampToValueAtTime(.0001, t + f[1]); o.connect(g).connect(ac.destination); o.start(t); o.stop(t + f[1] + .02); } catch(e) {} }

  /* ---------- button names follow the controller you hold ---------- */
  let padKind = "xbox";
  const G = b => { const k = ST.glyphs === "auto" ? padKind : ST.glyphs;
    const m = k === "ps" ? {a:"✕", b:"○", x:"□", y:"△", lb:"L1", rb:"R1", start:"OPTIONS", guide:"PS"} : {a:"A", b:"B", x:"X", y:"Y", lb:"LB", rb:"RB", start:"☰", guide:"⊙"};
    return `<kbd class="g-${k}-${b}">${m[b]}</kbd>`; };

  /* ---------- open / close ---------- */
  async function open(){ if (on) return; on = true; document.body.classList.add("bp-on"); tab = "home"; stack = [];
    root = document.createElement("div"); root.id = "bp"; document.body.appendChild(root);
    if (PS && PS.fullscreen) PS.fullscreen(true).catch(() => {}); else { try { document.documentElement.requestFullscreen(); } catch(e) {} }
    document.addEventListener("keydown", key, true); root.addEventListener("mousemove", wake); root.addEventListener("mouseover", hover);
    raf = requestAnimationFrame(poll); lastRun = Object.assign({}, S().RUNNING);
    try { if (PS && PS.getSettings) Object.assign(ST, await PS.getSettings()); } catch(e) {}
    draw(); beep("open"); }
  function close(){ if (!on) return; on = false; document.body.classList.remove("bp-on"); if (root) root.remove(); root = null; stack = [];
    document.removeEventListener("keydown", key, true); cancelAnimationFrame(raf);
    if (PS && PS.fullscreen) PS.fullscreen(false).catch(() => {}); else { try { if (document.fullscreenElement) document.exitFullscreen(); } catch(e) {} }
    if (location.hash === "#bigpicture") location.hash = "#library"; }
  function wake(){ if (!root) return; root.classList.remove("nomouse"); clearTimeout(hideT); hideT = setTimeout(() => root && root.classList.add("nomouse"), 3000); }
  function hover(e){ const t = e.target.closest("[data-bp]"); if (t && !t.disabled && document.activeElement !== t && !root.classList.contains("nomouse")) t.focus({preventScroll:true}); }

  /* ---------- state of a game ---------- */
  function gameState(g, prefer){ const s = S(), eds = ownedEds(g.id).slice().reverse();
    if (prefer && eds.includes(prefer)) eds.unshift(prefer);
    for (const ed of eds) { const [k, inst] = instOf(g.id, ed); if (inst && s.RUNNING[k]) return {k, ed, run:true}; }
    for (const ed of eds) { const [k] = instOf(g.id, ed); if (s.PROGRESS[k]) return {k, ed, prog:s.PROGRESS[k]}; }
    for (const ed of eds) { const [k, inst] = instOf(g.id, ed); if (inst) return {k, ed, inst:true}; }
    return {k:instOf(g.id, eds[0] || "demo")[0], ed:eds[0], none:true}; }
  function actBtn(g, prefer, big){ const st = gameState(g, prefer), c = big ? " big" : "";
    if (st.run) return `<button class="bp-btn stop${c}" data-bp="act" data-a="stop" data-g="${g.id}" data-e="${st.ed}">■ إغلاق</button>`;
    if (st.prog) return `<button class="bp-btn${c}" data-bp="act" data-a="none" data-g="${g.id}">يحمّل ${H.pct(st.prog)}%</button>`;
    if (st.inst) return `<button class="bp-btn play${c}" data-bp="act" data-a="play" data-g="${g.id}" data-e="${st.ed}">▶ لعب</button>`;
    if (st.ed) return `<button class="bp-btn inst${c}" data-bp="act" data-a="install" data-g="${g.id}" data-e="${st.ed}">⬇ تثبيت</button>`;
    return `<button class="bp-btn buy${c}" data-bp="buy" data-g="${g.id}">🛒 صفحة الشراء</button>`; }
  const tile = (g, extra = "") => `<button class="bp-tile" data-bp="game" data-g="${g.id}"${extra}><span class="art" data-cover="${g.id}">${S().RUNNING && Object.keys(S().RUNNING).some(k => k.split("--")[0] === g.id) ? '<i class="bp-live">● شغالة</i>' : ""}</span><b>${esc(g.en || g.title)}</b></button>`;
  const soonTile = x => `<button class="bp-tile soon" data-bp="soon" data-s="${x.id}"><span class="art">${H.soonArt(x)}<i class="bp-soon">قريبًا</i></span><b>${esc(x.title)}</b></button>`;

  /* ---------- screens ---------- */
  function vHome(){
    const s = S(), l = mine(), g = H.byId(heroG) || l[0] || GAMES[0]; heroG = g.id;
    const on2 = Object.entries(s.FRIENDS).filter(([id, f]) => f.status === "ok" && s.online(id));
    const notOwned = GAMES.filter(x => !ownedEds(x.id).length);
    return `<section class="bp-hero" id="bpHero">${heroHTML(g)}</section>
      ${l.length ? `<h3 class="bp-h">كمّل اللعب</h3><div class="bp-row">${l.map(x => tile(x, ' data-hero="1"')).join("")}</div>` : `<div class="bp-note">مكتبتك فاضية. اختر لعبة من المتجر.</div>`}
      ${on2.length ? `<h3 class="bp-h">أصدقاء متصلين</h3><div class="bp-row">${on2.map(([id, f]) => friendChip(id, f)).join("")}</div>` : ""}
      <h3 class="bp-h">من المتجر</h3><div class="bp-row">${notOwned.map(x => tile(x, ' data-hero="1"')).join("")}${H.SOON.slice(0, 10).map(soonTile).join("")}</div>`; }
  function heroHTML(g){ const m = mins(g.id), lp = last(g.id), own = ownedEds(g.id).length;
    return `<span class="bp-bg" data-cover="${g.id}"></span><div class="bp-info"><div class="bp-eyebrow">${own ? (lp ? "آخر مرة لعبت: " + ago(lp) : "في مكتبتك") : "من المتجر"}</div><h1>${esc(g.en || g.title)}</h1>
      <div class="bp-meta">${own ? `<span>${m ? hrs(m) + " لعب" : "ما لعبتها للحين"}</span>` : `<span>${esc(g.tagline || "")}</span>`}</div>
      <div class="bp-acts">${actBtn(g)}<button class="bp-btn ghost" data-bp="game" data-g="${g.id}">التفاصيل</button></div></div>`; }
  function ago(t){ const d = (Date.now() - t) / 60000; return d < 60 ? "قبل " + Math.max(1, Math.round(d)) + " دقيقة" : d < 1440 ? "قبل " + Math.round(d / 60) + " ساعة" : "قبل " + Math.round(d / 1440) + " يوم"; }
  function vLibrary(){ const l = mine();
    return l.length ? `<div class="bp-grid lib">${l.map(x => tile(x)).join("")}</div>` : `<div class="bp-empty">مكتبتك فاضية.<button class="bp-btn" data-bp="tab" data-t="store">روح للمتجر</button></div>`; }
  let q = "";
  function vStore(){ const ql = q.trim().toLowerCase(), m = x => !ql || (x.title + " " + (x.en || "") + " " + (x.genreAr || "")).toLowerCase().includes(ql);
    const gs = GAMES.filter(m), ss = H.SOON.filter(m);
    return `<div class="bp-search"><button class="bp-input" data-bp="search">${q ? esc(q) : '<span>ابحث عن لعبة…</span>'}</button><span class="bp-hint">${G("y")} بحث</span></div>
      ${gs.length ? `<h3 class="bp-h">الألعاب</h3><div class="bp-grid">${gs.map(g => { const e = ent(g.id), s = H.saleOf(g.id);
        return `<button class="bp-card" data-bp="game" data-g="${g.id}"><span class="art" data-cover="${g.id}">${s && !e.best ? `<i class="bp-soon sale">-${s.pct}%</i>` : ""}</span><b>${esc(g.en || g.title)}</b><small>${e.best ? "في مكتبتك ✓" : "من " + money(Math.round(H.lowPrice(g) * 100) / 100) + (s ? " · تخفيض" : " · ديمو مجاني")}</small></button>`; }).join("")}</div>` : ""}
      ${ss.length ? `<h3 class="bp-h">قريبًا · ${ss.length}</h3><div class="bp-grid">${ss.slice(0, 48).map(x => `<button class="bp-card" data-bp="soon" data-s="${x.id}"><span class="art">${H.soonArt(x)}<i class="bp-soon">قريبًا</i></span><b>${esc(x.title)}</b><small>${esc(x.genreAr)} · ${esc(x.when)}</small></button>`).join("")}</div>` : ""}
      ${!gs.length && !ss.length ? `<div class="bp-empty">ما لقينا شي باسم «${esc(q)}»</div>` : ""}`; }
  function friendChip(id, f){ const s = S(), pl = s.playing(id), onl = s.online(id);
    return `<button class="bp-friend${onl ? " on" : ""}" data-bp="friend" data-f="${id}"><span class="bp-av">${esc((f.username || "?")[0].toUpperCase())}</span><span><b>@${esc(f.username)}</b><small>${pl ? "يلعب " + esc(pl) : onl ? "متصل" : "غير متصل"}</small></span></button>`; }
  function vFriends(){ const s = S(), fr = Object.entries(s.FRIENDS).filter(([, f]) => f.status === "ok").sort((a, b) => s.online(b[0]) - s.online(a[0]));
    return fr.length ? `<div class="bp-grid fr">${fr.map(([id, f]) => friendChip(id, f)).join("")}</div>` : `<div class="bp-empty">ما عندك أصدقاء للحين. أضفهم من صفحة الأصدقاء في المتجر.</div>`; }
  function vDownloads(){ const s = S(), act = Object.entries(s.PROGRESS), ups = H.pendingUpdates ? H.pendingUpdates() : [];
    if (!PS) return `<div class="bp-empty">التحميلات تشتغل في تطبيق Premium Games على الكمبيوتر.</div>`;
    return (act.length ? `<h3 class="bp-h">يحمّل الحين</h3><div class="bp-list">${act.map(([k, p]) => `<div class="bp-dl" data-bp="item"><b>${esc(H.keyTitle(k))}</b><div class="bp-bar"><i style="width:${H.pct(p)}%"></i></div><small class="num">${H.progText(p)}</small></div>`).join("")}</div>` : "")
      + (ups.length ? `<h3 class="bp-h">تحديثات</h3><div class="bp-list">${ups.map(([k]) => `<button class="bp-dl" data-bp="update" data-k="${k}"><b>${esc(H.keyTitle(k))}</b><small>تحديث جاهز · ${G("a")} حدّث</small></button>`).join("")}</div>` : "")
      + (!act.length && !ups.length ? `<div class="bp-empty">كل ألعابك محدّثة ✓</div>` : ""); }
  let shots = null;
  function vShots(){
    if (!PS || !PS.screenshots) return `<div class="bp-empty">اللقطات في تطبيق Premium Games.</div>`;
    if (!shots) { shots = []; Promise.all(mine().map(g => PS.screenshots(g.id).then(l => l.map(x => Object.assign(x, {g:g.id}))).catch(() => []))).then(r => { shots = r.flat().sort((a, b) => b.name.localeCompare(a.name)); draw(); }); return `<div class="bp-empty">يحمّل اللقطات…</div>`; }
    return shots.length ? `<div class="bp-grid shots">${shots.map((x, i) => `<button class="bp-shot" data-bp="shot" data-i="${i}"><img src="${x.thumb}" alt=""><small>${esc(H.byId(x.g) ? (H.byId(x.g).en || H.byId(x.g).title) : "")} · ${esc(x.name)}</small></button>`).join("")}</div>`
      : `<div class="bp-empty">ما عندك لقطات. اضغط F12 وأنت تلعب.</div>`; }
  function vSettings(){ const t = (k, n, d) => `<button class="bp-set" data-bp="set" data-k="${k}"><span><b>${n}</b><small>${d}</small></span><i class="sw${ST[k] ? " on" : ""}"></i></button>`;
    const gl = {auto:"تلقائي حسب اليد", xbox:"إكس بوكس (A B X Y)", ps:"بلايستيشن (✕ ○ □ △)"};
    return `<div class="bp-list set">
      ${PS ? t("consoleBoot", "شغّل الكمبيوتر على وضع الكونسل", "أول ما يشتغل الويندوز يفتح Premium Games على هالشاشة، مثل جهاز ألعاب") : ""}
      ${PS ? t("startup", "يشتغل مع الويندوز", "التطبيق يجهز بالخلفية عشان ألعابك تفتح بسرعة") : ""}
      ${PS ? t("consolePad", "افتح وضع الكونسل لما توصل يد تحكم", "توصّل اليد وتطلع لك هالشاشة على طول") : ""}
      ${t("sounds", "أصوات الواجهة", "صوت خفيف لما تتنقل وتختار")}
      <button class="bp-set" data-bp="glyph"><span><b>شكل أزرار اليد</b><small>${gl[ST.glyphs] || gl.auto}</small></span><i class="bp-val">تغيير</i></button>
      <button class="bp-set" data-bp="test"><span><b>اختبر يد التحكم</b><small>شوف كل زر وعصا تشتغل</small></span><i class="bp-val">${G("a")}</i></button>
      <button class="bp-set" data-bp="exit"><span><b>اطلع من وضع الكونسل</b><small>ترجع لشاشة المتجر العادية</small></span><i class="bp-val">⏏</i></button></div>`; }

  /* ---------- layers (game page, keyboard, quick menu, power, viewer) ---------- */
  function layerHTML(L){
    if (L.t === "game") { const g = H.byId(L.g), eds = ownedEds(g.id), m = mins(g.id), lp = last(g.id), ach = H.achOf(g.id);
      const ed = L.ed || gameState(g).ed, e = g.editions.find(x => x.id === ed);
      return `<div class="bp-page"><span class="bp-bg" data-cover="${g.id}"></span><div class="bp-pinfo">
        <div class="bp-eyebrow">${eds.length ? "في مكتبتك" : "من المتجر"}${g.tags ? " · " + g.tags.map(esc).join(" · ") : ""}</div><h1>${esc(g.en || g.title)}</h1>
        ${eds.length > 1 ? `<div class="bp-eds">${eds.map(x => `<span class="${x === ed ? "on" : ""}">${esc(ED_TAG[x] || x)}</span>`).join("")}<small>${G("x")} غيّر النسخة</small></div>` : ed ? `<div class="bp-eds"><span class="on">${esc(ED_TAG[ed] || ed)}</span></div>` : ""}
        <p class="bp-desc">${esc(g.tagline || "")}</p>
        <div class="bp-acts">${actBtn(g, ed, true)}${eds.length ? "" : `<button class="bp-btn ghost" data-bp="buy" data-g="${g.id}">الإصدارات والأسعار</button>`}</div>
        <div class="bp-stats">${eds.length ? `<div><b>${m ? hrs(m) : "—"}</b><small>وقت اللعب</small></div><div><b>${lp ? ago(lp) : "—"}</b><small>آخر مرة</small></div>` : `<div><b>${money(Math.round(H.lowPrice(g) * 100) / 100)}</b><small>يبدأ من${H.saleOf(g.id) ? " · خصم " + H.saleOf(g.id).pct + "%" : ""}</small></div><div><b>مجاني</b><small>الديمو</small></div>`}
          ${ach.all ? `<div><b>${ach.got}/${ach.all}</b><small>الإنجازات</small></div>` : ""}${e && e.d ? `<div><b>${esc(e.d)}</b><small>${esc(e.name)}</small></div>` : ""}</div>
        ${ach.all ? `<div class="bp-achs">${ach.list.map(a => `<span class="${a.got ? "got" : ""}" title="${esc(a.desc)}">${a.got ? "🏆" : "🔒"} ${esc(a.name)}</span>`).join("")}</div>` : ""}
        </div></div>`; }
    if (L.t === "soon") { const x = H.SOON.find(s => s.id === L.s);
      return `<div class="bp-page soonp"><span class="bp-bg">${H.soonArt(x)}</span><div class="bp-pinfo"><div class="bp-eyebrow">قريبًا · ${esc(x.genreAr)}</div><h1>${esc(x.title)}</h1><p class="bp-desc">${esc(x.tagline)}</p>
        <div class="bp-stats"><div><b>${esc(x.when)}</b><small>موعد الإصدار</small></div></div><div class="bp-acts"><button class="bp-btn ghost" data-bp="back">رجوع</button></div></div></div>`; }
    if (L.t === "kbd") return kbdHTML(L);
    if (L.t === "quick") { const s = S(), run = Object.entries(s.RUNNING), n = H.notes();
      return `<aside class="bp-quick"><h2>القائمة السريعة</h2>
        ${run.map(([k, r]) => `<div class="bp-qrun"><b>${esc(r.name || H.keyTitle(k))}</b><small>شغالة الحين</small><button class="bp-btn stop" data-bp="qstop" data-k="${k}">■ أقفل اللعبة</button></div>`).join("")}
        <h3>الإشعارات${n.length ? ` · ${n.length}` : ""}</h3>${n.length ? n.slice(0, 6).map(x => `<div class="bp-note2">${x.i} ${esc(x.t)}</div>`).join("") : '<div class="bp-note2 muted">ما فيه شي جديد</div>'}
        <button class="bp-q" data-bp="tab" data-t="friends">👥 الأصدقاء</button><button class="bp-q" data-bp="tab" data-t="downloads">⬇ التحميلات</button>
        <button class="bp-q" data-bp="set" data-k="sounds">${ST.sounds ? "🔊 الأصوات شغالة" : "🔇 الأصوات مقفلة"}</button>
        <button class="bp-q" data-bp="power">⏻ الطاقة</button></aside>`; }
    if (L.t === "power") return `<div class="bp-dlg"><h2>الطاقة</h2>
        ${PS && PS.power ? `<button class="bp-q" data-bp="pw" data-w="sleep">🌙 وضع السكون</button><button class="bp-q" data-bp="pw" data-w="restart">↻ إعادة تشغيل الكمبيوتر</button><button class="bp-q" data-bp="pw" data-w="shutdown">⏻ إطفاء الكمبيوتر</button><button class="bp-q" data-bp="pw" data-w="desktop">🖥 ارجع لسطح المكتب</button><button class="bp-q" data-bp="pw" data-w="quit">✕ اقفل Premium Games</button>` : ""}
        <button class="bp-q" data-bp="exit">⏏ اطلع من وضع الكونسل</button></div>`;
    if (L.t === "confirm") return `<div class="bp-dlg"><h2>${esc(L.title)}</h2><p>${esc(L.text)}</p><button class="bp-q danger" data-bp="yes">${esc(L.ok)}</button><button class="bp-q" data-bp="back">إلغاء</button></div>`;
    if (L.t === "view") { const x = shots[L.i]; return `<div class="bp-view"><img src="${x.thumb}" alt=""><div class="bp-vcap">${esc(x.name)} · ${L.i + 1}/${shots.length}<button class="bp-btn ghost" data-bp="showshot">افتح في المجلد</button></div></div>`; }
    if (L.t === "friend") { const f = S().FRIENDS[L.f] || {}, quick = ["يلا نلعب؟", "دقيقة وجاي", "أحسنت 👏", "باي، أشوفك بعدين"];
      return `<div class="bp-dlg"><h2>@${esc(f.username || "")}</h2>
        <button class="bp-q" data-bp="call" data-f="${L.f}">📞 مكالمة صوتية</button>
        <h3>رسالة سريعة</h3>${quick.map(t => `<button class="bp-q" data-bp="qmsg" data-f="${L.f}" data-m="${esc(t)}">💬 ${esc(t)}</button>`).join("")}
        <button class="bp-q" data-bp="qtype" data-f="${L.f}">⌨ اكتب رسالة</button></div>`; }
    if (L.t === "test") return `<div class="bp-dlg wide"><h2>اختبار يد التحكم</h2><div id="bpTest" class="bp-test"></div><p class="muted">اضغط ${G("b")} مرتين ورا بعض للرجوع</p></div>`;
    if (L.t === "glyph") return `<div class="bp-dlg"><h2>شكل أزرار اليد</h2>${[["auto","تلقائي حسب اليد"],["xbox","إكس بوكس"],["ps","بلايستيشن"]].map(([k, n]) => `<button class="bp-q${ST.glyphs === k ? " sel" : ""}" data-bp="glyphset" data-v="${k}">${n}</button>`).join("")}</div>`;
    return ""; }

  /* on-screen keyboard (Arabic / English), for the controller */
  const KB = {ar:["ض ص ث ق ف غ ع ه خ ح ج", "ش س ي ب ل ا ت ن م ك ط", "ئ ء ؤ ر ى ة و ز ظ د ذ", "1 2 3 4 5 6 7 8 9 0 ؟"], en:["q w e r t y u i o p", "a s d f g h j k l", "z x c v b n m", "1 2 3 4 5 6 7 8 9 0 ?"]};
  function kbdHTML(L){ return `<div class="bp-kbd"><div class="bp-kin">${esc(L.v) || `<span>${esc(L.ph || "")}</span>`}<i class="caret"></i></div>
    ${KB[L.lang].map(r => `<div class="bp-krow">${r.split(" ").map(c => `<button class="bp-k" data-bp="k" data-c="${esc(c)}">${esc(c)}</button>`).join("")}</div>`).join("")}
    <div class="bp-krow"><button class="bp-k w2" data-bp="klang">${L.lang === "ar" ? "EN" : "عربي"}</button><button class="bp-k w5" data-bp="k" data-c=" ">مسافة</button><button class="bp-k w2" data-bp="kdel">⌫ ${G("x")}</button><button class="bp-k w2 ok" data-bp="kok">تم ${G("start")}</button></div></div>`; }

  /* ---------- drawing ---------- */
  function draw(){
    if (!on || !root) return;
    const s = S(), prof = s.PROFILE || {}, u = s.user;
    const online = Object.keys(s.FRIENDS).filter(id => s.FRIENDS[id].status === "ok" && s.online(id)).length, n = H.notes().length;
    const ae = root.contains(document.activeElement) ? document.activeElement : null;
    const keep = !fresh && ae && ae.dataset.bp ? `[data-bp="${ae.dataset.bp}"]` + ["g","t","a","i","k","f","s","c","w","v","e"].filter(k => ae.dataset[k] != null).map(k => `[data-${k}="${CSS.escape(ae.dataset[k])}"]`).join("") : "";
    const scrollTop = (root.querySelector(".bp-main") || {}).scrollTop || 0;
    const views = {home:vHome, library:vLibrary, store:vStore, friends:vFriends, downloads:vDownloads, shots:vShots, settings:vSettings};
    root.innerHTML = `<header class="bp-top"><span class="bp-logo"><i>P</i><i>G</i></span>
        <nav>${TABS.map(([id, nm]) => `<button class="bp-tab${tab === id ? " on" : ""}" data-bp="tab" data-t="${id}">${nm}${id === "downloads" && Object.keys(s.PROGRESS).length ? ' <i class="dot"></i>' : ""}</button>`).join("")}</nav>
        <span class="bp-me">${n ? `<span class="bp-badge">🔔 ${n}</span>` : ""}${online ? `<span class="bp-fr">● ${online}</span>` : ""}<span class="bp-pad" title="يد التحكم">${padOn ? "🎮" : ""}</span><b>${esc(prof.username ? "@" + prof.username : (u ? "" : "ضيف"))}</b><span class="num bp-clock">${clock()}</span></span></header>
      <main class="bp-main">${views[tab]()}</main>
      ${stack.map((L, i) => `<div class="bp-layer t-${L.t}" data-li="${i}">${layerHTML(L)}</div>`).join("")}
      <footer class="bp-help"><span>${G("a")} اختيار</span><span>${G("b")} رجوع</span><span>${G("lb")}${G("rb")} الأقسام</span>${tab === "store" && !stack.length ? `<span>${G("y")} بحث</span>` : ""}<span>${G("start")} القائمة السريعة</span></footer>`;
    root.querySelectorAll("[data-cover]").forEach(el => cover(el, el.dataset.cover));
    root.querySelectorAll("[data-bp]").forEach(el => el.onclick = () => activate(el));
    const m = root.querySelector(".bp-main"); if (m) m.scrollTop = scrollTop;
    fresh = false;
    const sc = scope(), f = (keep && sc.querySelector(keep)) || sc.querySelector(".bp-btn.play, .bp-btn.stop, .bp-btn.inst, .bp-k") || (stack.length ? sc.querySelector("[data-bp]") : sc.querySelector(".bp-main [data-bp]")) || sc.querySelector(".bp-tab.on");
    if (f) focusEl(f, true);
    if (stack.length && stack[stack.length - 1].t === "test") testLoop();
  }
  const scope = () => (root && root.querySelector(".bp-layer:last-of-type")) || root;
  function focusEl(el, quiet){ el.focus({preventScroll:true}); el.scrollIntoView({block:"nearest", inline:"nearest", behavior:quiet ? "auto" : "smooth"});
    if (!quiet) beep("move");
    if (el.dataset.hero && el.dataset.g && el.dataset.g !== heroG) { heroG = el.dataset.g; const h = root.querySelector("#bpHero"); if (h) { h.innerHTML = heroHTML(H.byId(heroG)); h.querySelectorAll("[data-cover]").forEach(x => cover(x, x.dataset.cover)); h.querySelectorAll("[data-bp]").forEach(x => x.onclick = () => activate(x)); } } }
  function push(L){ stack.push(L); fresh = true; beep("open"); draw(); }
  function pop(){ if (!stack.length) return false; stack.pop(); fresh = !!stack.length; beep("back"); draw(); return true; }

  /* ---------- actions ---------- */
  async function activate(el){
    const k = el.dataset.bp, top = stack[stack.length - 1]; beep("ok");
    if (k === "tab") { stack = []; if (el.dataset.t === "exit") return close(); tab = el.dataset.t; fresh = true; if (tab === "shots") shots = null; draw(); return; }
    if (k === "game") return push({t:"game", g:el.dataset.g});
    if (k === "soon") return push({t:"soon", s:el.dataset.s});
    if (k === "back") return pop();
    if (k === "buy") { close(); location.hash = el.dataset.g; return; }
    if (k === "act") { const a = el.dataset.a, g = H.byId(el.dataset.g), st = gameState(g, el.dataset.e);
      if (a === "none") return;
      if (!PS) { beep("err"); return toast("التشغيل من تطبيق Premium Games على الكمبيوتر"); }
      el.disabled = true;
      try { if (a === "play") { await PS.launch(st.k, st.ed); toast("يشغّل " + (g.en || g.title) + "…"); } else if (a === "stop") await PS.stop(st.k); else if (a === "install") { await PS.install(st.k, {edition:st.ed}); toast("بدأ التثبيت"); } }
      catch(e) { beep("err"); toast(String(e && e.message || e)); }
      setTimeout(draw, 500); return; }
    if (k === "update") { try { await PS.install(el.dataset.k); toast("بدأ التحديث"); } catch(e) { toast(String(e.message || e)); } return setTimeout(draw, 500); }
    if (k === "search") return push({t:"kbd", v:q, lang:/[a-z]/i.test(q) ? "en" : "ar", ph:"ابحث عن لعبة…", done:v => { q = v; }});
    if (k === "k") { top.v = (top.v + el.dataset.c).slice(0, 60); return draw(); }
    if (k === "kdel") { top.v = top.v.slice(0, -1); return draw(); }
    if (k === "klang") { top.lang = top.lang === "ar" ? "en" : "ar"; return draw(); }
    if (k === "kok") { stack.pop(); top.done(top.v); return draw(); }
    if (k === "friend") return push({t:"friend", f:el.dataset.f});
    if (k === "call") { stack = []; draw(); return H.call(el.dataset.f); }
    if (k === "qmsg") { try { await H.sendMsg(el.dataset.f, el.dataset.m); toast("انرسلت ✓"); } catch(e) { toast("ما انرسلت"); } return pop(); }
    if (k === "qtype") { const f = el.dataset.f; stack.pop(); return push({t:"kbd", v:"", lang:"ar", ph:"اكتب رسالتك…", done:async v => { if (v.trim()) { try { await H.sendMsg(f, v.trim()); toast("انرسلت ✓"); } catch(e) { toast("ما انرسلت"); } } }}); }
    if (k === "shot") return push({t:"view", i:+el.dataset.i});
    if (k === "showshot") { const x = shots[top.i]; if (PS && PS.openShots) PS.openShots(x.file); return; }
    if (k === "set") { ST[el.dataset.k] = !ST[el.dataset.k]; if (PS && PS.setSettings) { try { Object.assign(ST, await PS.setSettings({[el.dataset.k]:ST[el.dataset.k]})); } catch(e) {} } return draw(); }
    if (k === "glyph") return push({t:"glyph"});
    if (k === "glyphset") { ST.glyphs = el.dataset.v; if (PS && PS.setSettings) PS.setSettings({glyphs:ST.glyphs}).catch(() => {}); return pop(); }
    if (k === "test") return push({t:"test"});
    if (k === "exit") return close();
    if (k === "power") { stack = stack.filter(L => L.t !== "quick"); return push({t:"power"}); }
    if (k === "pw") { const w = el.dataset.w;
      if (w === "restart" || w === "shutdown") return push({t:"confirm", title:w === "restart" ? "إعادة تشغيل الكمبيوتر؟" : "إطفاء الكمبيوتر؟", text:Object.keys(S().RUNNING).length ? "اللعبة الشغالة بتنحفظ وتنقفل أول." : "", ok:w === "restart" ? "أعد التشغيل" : "أطفئ", yes:() => PS.power(w)});
      if (w === "quit") return push({t:"confirm", title:"تقفل Premium Games؟", text:"", ok:"اقفل", yes:() => PS.power("quit")});
      stack = []; draw(); try { await PS.power(w); } catch(e) { toast(String(e.message || e)); } return; }
    if (k === "yes") { const fn = top.yes; stack = []; draw(); try { await fn(); } catch(e) { toast(String(e.message || e)); } return; }
    if (k === "qstop") { try { await PS.stop(el.dataset.k); } catch(e) {} stack = []; return setTimeout(draw, 400); }
  }
  function context(btn){   // X / Y / Start inside a layer or screen
    const top = stack[stack.length - 1];
    if (btn === "x") { if (top && top.t === "kbd") { top.v = top.v.slice(0, -1); return draw(); }
      if (top && top.t === "game") { const eds = ownedEds(top.g); if (eds.length > 1) { const cur = top.ed || gameState(H.byId(top.g)).ed; top.ed = eds[(eds.indexOf(cur) + 1) % eds.length]; beep("move"); draw(); } } return; }
    if (btn === "y") { if (top && top.t === "kbd") { top.v += " "; return draw(); } if (!stack.length && tab === "store") return activate(root.querySelector('[data-bp="search"]')); return; }
    if (btn === "start") { if (top && top.t === "kbd") { stack.pop(); top.done(top.v); beep("ok"); return draw(); }
      if (top && top.t === "quick") return pop(); return push({t:"quick"}); }
  }

  /* ---------- moving the focus ---------- */
  function move(dx, dy){
    const sc = scope(), items = [...sc.querySelectorAll("[data-bp]")].filter(e => !e.disabled && e.offsetParent !== null), cur = document.activeElement;
    if (!items.length) return; if (!items.includes(cur)) return focusEl(items[0]);
    const r0 = cur.getBoundingClientRect(), c0 = {x:r0.left + r0.width / 2, y:r0.top + r0.height / 2};
    let best = null, bd = 1e9;
    for (const e of items) { if (e === cur) continue; const r = e.getBoundingClientRect(), c = {x:r.left + r.width / 2, y:r.top + r.height / 2};
      const vx = c.x - c0.x, vy = c.y - c0.y;
      if (dx && (Math.sign(vx) !== dx || Math.abs(vx) < 4)) continue; if (dy && (Math.sign(vy) !== dy || Math.abs(vy) < 4)) continue;
      const d = dx ? Math.abs(vx) + Math.abs(vy) * 3 : Math.abs(vy) + Math.abs(vx) * 3; if (d < bd) { bd = d; best = e; } }
    if (best) focusEl(best);
  }
  function cycleTab(d){ stack = []; fresh = true; const ids = TABS.map(t => t[0]); tab = ids[(ids.indexOf(tab) + d + ids.length) % ids.length]; if (tab === "shots") shots = null; beep("move"); draw(); }
  function back(){ if (pop()) return; const cur = document.activeElement;
    if (cur && cur.dataset.bp !== "tab") { beep("back"); root.querySelector(".bp-tab.on")?.focus(); } else if (tab !== "home") { tab = "home"; beep("back"); draw(); } else { beep("back"); close(); } }
  function viewStep(d){ const top = stack[stack.length - 1]; if (top && top.t === "view") { top.i = (top.i + d + shots.length) % shots.length; draw(); return true; } return false; }
  function key(e){ if (!on) return; const k = e.key, top = stack[stack.length - 1];
    const map = {ArrowLeft:[1, 0], ArrowRight:[-1, 0], ArrowUp:[0, -1], ArrowDown:[0, 1]};
    if (top && top.t === "kbd" && k.length === 1 && !e.ctrlKey && !e.altKey) { e.preventDefault(); top.v = (top.v + k).slice(0, 60); return draw(); }
    if (top && top.t === "kbd" && k === "Backspace") { e.preventDefault(); top.v = top.v.slice(0, -1); return draw(); }
    if (map[k]) { e.preventDefault(); if (k === "ArrowLeft" || k === "ArrowRight") { if (viewStep(k === "ArrowLeft" ? 1 : -1)) return; } move(...map[k]); }
    else if (k === "Enter" || k === " ") { e.preventDefault(); if (top && top.t === "kbd" && k === "Enter") return context("start"); const a = document.activeElement; if (a && a.dataset && a.dataset.bp) activate(a); }
    else if (k === "Escape" || k === "Backspace") { e.preventDefault(); back(); }
    else if (k === "Tab") { e.preventDefault(); cycleTab(e.shiftKey ? -1 : 1); }
    else if (k === "q" || k === "Q" || k === "F1") { e.preventDefault(); context("start"); }
    else if (k === "/" ) { e.preventDefault(); if (tab !== "store") { tab = "store"; stack = []; draw(); } context("y"); }
    else if (k === "x" || k === "X") { e.preventDefault(); context("x"); } }

  /* ---------- game controllers (Xbox / PlayStation, standard mapping) ---------- */
  let padOn = false, testBack = 0;
  addEventListener("gamepadconnected", e => { padOn = true; padKind = /054c|playstation|dualsense|dualshock|wireless controller/i.test(e.gamepad.id) ? "ps" : "xbox";
    if (on) { toast("🎮 انوصلت يد التحكم"); draw(); } else if (H.padConnected) H.padConnected(); });
  addEventListener("gamepaddisconnected", () => { padOn = [...(navigator.getGamepads ? navigator.getGamepads() : [])].some(Boolean); if (on) { toast("انفصلت يد التحكم"); draw(); } });
  function poll(){
    if (!on) return; const pads = navigator.getGamepads ? [...navigator.getGamepads()].filter(Boolean) : [];
    if (pads.length && !padOn) { padOn = true; padKind = /054c|playstation|dualsense|dualshock|wireless controller/i.test(pads[0].id) ? "ps" : "xbox"; draw(); }
    const top = stack[stack.length - 1];
    if (top && top.t === "test") { raf = requestAnimationFrame(poll); return; }
    for (const p of pads) {
      const b = i => p.buttons[i] && p.buttons[i].pressed, ax = p.axes || [];
      const st = {a:b(0), b:b(1), x:b(2), y:b(3), lb:b(4), rb:b(5), lt:b(6), rt:b(7), sel:b(8), start:b(9), guide:b(16), up:b(12) || ax[1] < -0.55, down:b(13) || ax[1] > 0.55, left:b(14) || ax[0] < -0.55, right:b(15) || ax[0] > 0.55};
      const now = performance.now();
      for (const [k, v] of Object.entries(st)) {
        const prev = pad.last[p.index + k]; pad.last[p.index + k] = v;
        const rep = ["up","down","left","right","lt","rt"].includes(k);
        if (v && !prev) pad.held[p.index + k] = now + 260;
        const fire = v && (!prev || (rep && now > pad.held[p.index + k]));
        if (!fire) continue; if (prev) pad.held[p.index + k] = now + 90;
        if (root && root.classList.contains("nomouse") === false) root.classList.add("nomouse");
        if (k === "up") move(0, -1); else if (k === "down") move(0, 1);
        else if (k === "left") { if (!viewStep(1)) move(1, 0); } else if (k === "right") { if (!viewStep(-1)) move(-1, 0); }
        else if (k === "a") { const a = document.activeElement; if (a && a.dataset && a.dataset.bp) activate(a); }
        else if (k === "b") back(); else if (k === "lb") cycleTab(-1); else if (k === "rb") cycleTab(1);
        else if (k === "lt" || k === "rt") { const m = root.querySelector(".bp-main"); if (m && !stack.length) m.scrollBy({top:(k === "rt" ? 1 : -1) * innerHeight * .6, behavior:"smooth"}); }
        else if (k === "x" || k === "y") context(k);
        else if (k === "start" || k === "guide") context("start");
        else if (k === "sel") { if (!stack.length) { tab = "downloads"; draw(); } }
      }
    }
    raf = requestAnimationFrame(poll);
  }
  function testLoop(){
    const el = root && root.querySelector("#bpTest"); if (!el || !on) return;
    const p = [...(navigator.getGamepads ? navigator.getGamepads() : [])].filter(Boolean)[0];
    const names = ["A/✕","B/○","X/□","Y/△","LB","RB","LT","RT","Back","Start","L3","R3","↑","↓","←","→","Home"];
    if (!p) el.innerHTML = '<p>وصّل يد التحكم واضغط أي زر.</p>';
    else { el.innerHTML = `<p class="muted">${esc(p.id)}</p><div class="bp-tb">${names.map((n, i) => `<span class="${p.buttons[i] && p.buttons[i].pressed ? "on" : ""}">${n}</span>`).join("")}</div>
      <div class="bp-ta">${[0, 2].map(i => `<span class="stick"><i style="transform:translate(${(p.axes[i] || 0) * 28}px,${(p.axes[i + 1] || 0) * 28}px)"></i></span>`).join("")}</div>`;
      const bb = p.buttons[1] && p.buttons[1].pressed; if (bb && !pad.last.testB) { const t = performance.now(); if (t - testBack < 700) { testBack = 0; pad.last.testB = bb; stack.pop(); return draw(); } testBack = t; } pad.last.testB = bb; }
    requestAnimationFrame(testLoop);
  }

  /* ---------- live updates ---------- */
  function refresh(){ if (!on) return;
    const run = S().RUNNING, ended = Object.keys(lastRun).filter(k => !run[k]); lastRun = Object.assign({}, run);
    if (ended.length && PS && PS.focusMain) { PS.focusMain().catch(() => {}); if (PS.fullscreen) PS.fullscreen(true).catch(() => {}); }   // back to the console screen when a game closes
    const top = stack[stack.length - 1]; if (top && top.t === "kbd") return;   // don't disturb typing
    draw(); }
  setInterval(() => { if (on) { const c = root && root.querySelector(".bp-clock"); if (c) c.textContent = clock(); } }, 15000);
  return {open, close, refresh, get on(){ return on; }};
}
