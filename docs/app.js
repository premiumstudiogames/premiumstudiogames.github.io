import { initializeApp } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-app.js";
import { getAuth, onAuthStateChanged, createUserWithEmailAndPassword, signInWithEmailAndPassword, signOut,
  sendPasswordResetEmail, sendEmailVerification, updateProfile, setPersistence, browserLocalPersistence } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-auth.js";
import { getFirestore, doc, getDoc, setDoc, updateDoc, deleteDoc, collection, collectionGroup, onSnapshot,
  serverTimestamp, writeBatch, Timestamp } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-firestore.js";

/* ---------------- config ---------------- */
const firebaseConfig = {
  apiKey: "AIzaSyBLZ4t7g5S03MFQS8ZF5jKDjKER-Te_Me0",
  authDomain: "premium-studio-f9bc9.firebaseapp.com",
  projectId: "premium-studio-f9bc9",
  storageBucket: "premium-studio-f9bc9.firebasestorage.app",
  messagingSenderId: "113306830657",
  appId: "1:113306830657:web:ac19723291cb10e7ca83c0",
  measurementId: "G-MXDZK8WSVN"
};
const ADMIN_EMAIL = "abdullra7manx@gmail.com";
const SITE_VERSION = "00.00.01";
const REPO = "premiumstudiogames/premiumstudiogames.github.io";
const APP_DL = `https://github.com/${REPO}/releases/latest/download/PremiumStudioSetup.exe`;

const fb = initializeApp(firebaseConfig);
const auth = getAuth(fb);
const fs = getFirestore(fb);
setPersistence(auth, browserLocalPersistence).catch(()=>{});

/* desktop app bridge (only present inside the Premium Studio app) */
const PS = window.premiumStudio || null;
const isMobile = /Android|iPhone|iPad|iPod|Mobile/i.test(navigator.userAgent);

/* ---------------- helpers ---------------- */
const $ = s => document.querySelector(s), $$ = s => [...document.querySelectorAll(s)];
const esc = s => String(s == null ? "" : s).replace(/[&<>"']/g, c => ({"&":"&amp;","<":"&lt;",">":"&gt;","\"":"&quot;","'":"&#39;"}[c]));
const money = p => p === 0 ? "مجاني" : "$" + p.toLocaleString("en-US", {minimumFractionDigits:2, maximumFractionDigits:2});
const fmtPid = p => p ? String(p) : "—";
const finalPrice = (base, pct) => base * (100 - pct) / 100;   // same arithmetic as the security rules
const tsMs = t => !t ? 0 : (typeof t === "number" ? t : (t.toMillis ? t.toMillis() : 0));
const fdate = t => { const ms = tsMs(t); if (!ms) return "—"; try { return new Date(ms).toLocaleDateString("ar-SA-u-ca-gregory-nu-latn", {year:"numeric", month:"short", day:"numeric"}); } catch(e) { return ""; } };
const mb = b => (b/1048576).toFixed(b > 10485760 ? 0 : 1) + " MB";
function toast(t){ const e = $("#toast"); e.textContent = t; e.classList.add("on"); clearTimeout(toast.t); toast.t = setTimeout(() => e.classList.remove("on"), 3000); }
$("#ver").textContent = "v" + SITE_VERSION + (PS ? " · App " + PS.version : "");

/* ---------------- catalogue ---------------- */
const GAMES = [
  {id:"blockfall", title:"BLOCKFALL 3001", ar:false, sub:"بناء · نجاة · عالم مفتوح",
   tagline:"عالم ما بعد الحرب. اجمع، اصنع، ابنِ مركبتك، وانجُ.",
   about:["سنة 3001. ثلاث سنوات مرّت على الحرب النووية والبيولوجية اللي بدأت في 2998 ودمّرت العالم. اللي بقى: أنقاض، مرتزقة، وزومبي، وأنت.",
          "اجمع بلوكات بأحجام مختلفة من 20 سم إلى متر كامل، اصنع أدواتك وأسلحتك، وابنِ مركباتك فوق منصة الرفع."],
   feat:["قصة من 5 فصول تغطي 2998 إلى 3001","بلوكات بأحجام مختلفة ومركبات تبنيها بنفسك","صناعة مثل ماينكرافت وأكثر من 100 سلاح واقعي","زومبي بستة أنواع ومرتزقة بالـ AK-47","دعم يد PS5 و Xbox"],
   tags:["بناء","نجاة","قصة","أكشن"],
   editions:[{id:"demo",name:"Demo",price:0,d:"جرّب أول العالم مجانًا"},{id:"std",name:"BLOCKFALL 3001",price:39.99,d:"اللعبة كاملة"},{id:"gold",name:"BLOCKFALL 3001 Gold",price:99.99,d:"الإصدار الذهبي",gold:true}]},
  {id:"last-recording", title:"التسجيل الأخير", en:"THE LAST RECORDING", ar:true, sub:"رعب نفسي · قصة · أونلاين",
   tagline:"شريط واحد. ليلة واحدة. مول ما يطلع منه أحد.",
   about:["ليلة 3 نوفمبر 1999، الساعة 3:33 الفجر. مول متعدد الطوابق بإضاءة نيون، وكاميرا فيديو قديمة في يدك ما زالت تسجّل.",
          "دوّر على أشرطة المراقبة الخمسة واكشف وش صار هنا. رعب نفسي بمنظور أول شخص مستوحى من Resident Evil و Silent Hill."],
   feat:["منظور كاميرا REC بالكامل","مول بإضاءة LED وانعكاسات على الأرضيات","مسدس وذخيرة ودم","العب أونلاين مع أصحابك"],
   tags:["رعب نفسي","قصة","أونلاين"],
   editions:[{id:"demo",name:"Demo",price:0,d:"جرّب أول جزء مجانًا"},{id:"std",name:"التسجيل الأخير",price:69.99,d:"اللعبة كاملة"},{id:"gold",name:"التسجيل الأخير · الذهبي",price:999999.99,d:"الإصدار الذهبي مع البيضة الذهبية",gold:true,egg:true}]}
];
const RANK = {demo:0, std:1, gold:2};
const byId = id => GAMES.find(g => g.id === id);
const edName = (g, id) => (g.editions.find(e => e.id === id) || {name:id}).name;
GAMES.forEach(g => { for (const sel of ["#fGame", "#cGame"]) { const o = document.createElement("option"); o.value = g.id; o.textContent = g.en || g.title; $(sel).appendChild(o); } });
let MANIFEST = {games:{}};
fetch("games.json", {cache:"no-store"}).then(r => r.json()).then(j => { MANIFEST = j; render(); }).catch(()=>{});

/* ---------------- procedural cover art ---------------- */
function rng(a){ return () => { a|=0; a=a+0x6D2B79F5|0; let t=Math.imul(a^a>>>15,1|a); t=t+Math.imul(t^t>>>7,61|t)^t; return ((t^t>>>14)>>>0)/4294967296; }; }
function drawBlockfall(g,w,h){
  const r=rng(3001);
  const sky=g.createLinearGradient(0,0,0,h); sky.addColorStop(0,"#1b1020"); sky.addColorStop(.45,"#5a2418"); sky.addColorStop(.72,"#d8662a"); sky.addColorStop(1,"#2a1a12");
  g.fillStyle=sky; g.fillRect(0,0,w,h);
  const sx=w*.68, sy=h*.6, sun=g.createRadialGradient(sx,sy,0,sx,sy,h*.5); sun.addColorStop(0,"rgba(255,214,140,.95)"); sun.addColorStop(.12,"rgba(255,150,70,.6)"); sun.addColorStop(1,"rgba(255,120,50,0)"); g.fillStyle=sun; g.fillRect(0,0,w,h);
  for(let i=0;i<60;i++){ g.fillStyle=`rgba(40,20,20,${.08+r()*.12})`; g.fillRect(0,r()*h*.5,w,1+r()*3); }
  const u=Math.max(4,Math.round(h/60));
  const skyline=(base,col,minH,maxH,gapP)=>{ g.fillStyle=col; let x=0; while(x<w){ const bw=u*(3+r()*9|0), bh=u*(minH+r()*(maxH-minH)|0); if(r()>gapP){ g.fillRect(x,base-bh,bw,bh); if(r()<.5) g.fillRect(x+bw*.2,base-bh-u*(1+r()*4|0),u*2,u*4); } x+=bw; } };
  skyline(h*.74,"#3a1d1a",6,26,.25); skyline(h*.8,"#22120f",4,16,.15);
  for(let y=h*.8;y<h;y+=u) for(let x=0;x<w;x+=u){ const d=(y-h*.8)/(h*.2), v=r(); g.fillStyle=v<.12?"#5a3a24":v<.3?"#3a2618":`rgb(${28+d*10|0},${20+d*6|0},16)`; g.fillRect(x,y,u,u); }
  for(let x=0;x<w;x+=u) if(r()<.5){ g.fillStyle=r()<.5?"#6b5a2a":"#4f4a22"; g.fillRect(x,h*.8-u,u,u); }
  const lx=w*.2, ly=h*.8; g.fillStyle="#111"; g.fillRect(lx,ly-u*5,u*12,u*5); g.fillStyle="#d8a23a"; for(let i=0;i<12;i+=2) g.fillRect(lx+i*u,ly-u*5,u,u);
  g.fillStyle="#0b0b0c"; g.fillRect(lx+u,ly-u*11,u*10,u*4); g.fillRect(lx+u*7,ly-u*14,u*3,u*3); g.fillRect(lx,ly-u*7,u*2,u*2); g.fillRect(lx+u*10,ly-u*7,u*2,u*2);
  for(let i=0;i<220;i++){ g.fillStyle=`rgba(255,220,180,${r()*.35})`; g.fillRect(r()*w,r()*h,r()*2+.5,r()*2+.5); }
  const vg=g.createRadialGradient(w/2,h/2,h*.3,w/2,h/2,w*.75); vg.addColorStop(0,"rgba(0,0,0,0)"); vg.addColorStop(1,"rgba(0,0,0,.6)"); g.fillStyle=vg; g.fillRect(0,0,w,h);
}
function drawHorror(g,w,h,t){
  const r=rng(1999); g.fillStyle="#04040a"; g.fillRect(0,0,w,h);
  const vx=w*.5, vy=h*.46, neon=["#3fe2ff","#ff2bd6","#ffb21a","#ff2a3a","#5dffa8"];
  for(let f=0;f<5;f++){ const y=h*(.1+f*.19);
    for(const s of [-1,1]){ const x0=s<0?0:w, x1=vx+s*w*.08;
      g.strokeStyle="rgba(180,190,220,.12)"; g.lineWidth=Math.max(1,h/300); g.beginPath(); g.moveTo(x0,y); g.lineTo(x1,vy+(y-vy)*.12); g.stroke();
      for(let k=0;k<6;k++){ const p=k/6, px=x0+(x1-x0)*p*p, py=y+(vy+(y-vy)*.12-y)*p*p, sz=(1-p*p)*h*.035, c=neon[(f*3+k+(s>0?2:0))%5];
        if(r()<.75){ g.shadowColor=c; g.shadowBlur=sz*2; g.fillStyle=c; g.globalAlpha=.55+r()*.4; g.fillRect(px-(s<0?0:sz*3),py+sz*.4,sz*3,Math.max(1,sz*.45)); g.globalAlpha=1; g.shadowBlur=0; } } } }
  const fl=g.createLinearGradient(0,vy,0,h); fl.addColorStop(0,"rgba(63,226,255,0)"); fl.addColorStop(1,"rgba(255,43,214,.18)"); g.fillStyle=fl; g.fillRect(0,vy,w,h-vy);
  for(let i=0;i<14;i++){ g.fillStyle=neon[i%5]; g.globalAlpha=.08; g.fillRect(vx+(r()-.5)*w*.9,vy+h*.05,Math.max(1,w*.004),h); g.globalAlpha=1; }
  const fx=vx+w*.02, fh=h*.12; g.fillStyle="#000"; g.beginPath(); g.ellipse(fx,vy-fh*.75,fh*.11,fh*.13,0,0,7); g.fill(); g.fillRect(fx-fh*.12,vy-fh*.62,fh*.24,fh*.62);
  const n=Math.round(w*h/180); for(let i=0;i<n;i++){ g.fillStyle=`rgba(255,255,255,${Math.random()*.06})`; g.fillRect(Math.random()*w,Math.random()*h,1.4,1.4); }
  g.fillStyle="rgba(0,0,0,.18)"; for(let y=0;y<h;y+=3) g.fillRect(0,y,w,1);
  const vg=g.createRadialGradient(w/2,h/2,h*.25,w/2,h/2,w*.7); vg.addColorStop(0,"rgba(0,0,0,0)"); vg.addColorStop(1,"rgba(0,0,0,.75)"); g.fillStyle=vg; g.fillRect(0,0,w,h);
  const m=Math.max(10,h*.06), fsz=Math.max(10,h*.07); g.font=`500 ${fsz}px "JetBrains Mono",monospace`; g.textBaseline="top";
  g.beginPath(); g.arc(m+fsz*.4,m+fsz*.5,fsz*.35,0,7); g.fillStyle=(Math.floor((t||0)/600)%2)?"#ff2a3a":"rgba(255,42,58,.25)"; g.fill();
  g.fillStyle="#fff"; g.fillText("REC",m+fsz,m); g.textAlign="right"; g.fillText("NOV.03 1999",w-m,h-m-fsz); g.textAlign="left"; g.fillText("03:33 AM",m,h-m-fsz);
  g.strokeStyle="rgba(255,255,255,.7)"; g.lineWidth=Math.max(1,h/200); const c=m*1.4;
  [[m*.5,m*.5,1,1],[w-m*.5,m*.5,-1,1],[m*.5,h-m*.5,1,-1],[w-m*.5,h-m*.5,-1,-1]].forEach(([x,y,a,b])=>{ g.beginPath(); g.moveTo(x,y+b*c); g.lineTo(x,y); g.lineTo(x+a*c,y); g.stroke(); });
}
const covers = new Set();
function cover(el, id){ const c = document.createElement("canvas"); el.prepend(c); const o = {c, id}; covers.add(o); paint(o); }
function paint(o, t){ const box = o.c.parentElement; if (!box || !box.isConnected) return false; const dpr = Math.min(devicePixelRatio || 1, 2);
  const w = Math.max(40, box.clientWidth*dpr|0), h = Math.max(30, box.clientHeight*dpr|0); if (o.c.width !== w || o.c.height !== h){ o.c.width = w; o.c.height = h; }
  const g = o.c.getContext("2d"); g.setTransform(1,0,0,1,0,0); g.shadowBlur = 0; g.textAlign = "left"; if (o.id === "blockfall") drawBlockfall(g,w,h); else drawHorror(g,w,h,t); return true; }
let rsT = 0; addEventListener("resize", () => { clearTimeout(rsT); rsT = setTimeout(() => { for (const o of [...covers]) if (!paint(o)) covers.delete(o); }, 150); });
const reduce = matchMedia("(prefers-reduced-motion: reduce)").matches;
if (!reduce) setInterval(() => { const t = performance.now(); for (const o of [...covers]) { if (o.id !== "last-recording") continue; if (!o.c.isConnected) { covers.delete(o); continue; } paint(o, t); } }, 600);

/* ---------------- state ---------------- */
let user = null, PROFILE = null, LIB = {}, isAdmin = false, guest = false, authReady = false;
let INSTALLED = {}, PROGRESS = {};
let unsubs = [];
/* entitlement: highest owned (free or paid) edition, plus any pending order */
function ent(gid){
  let best = null, pending = null;
  for (const [k, v] of Object.entries(LIB)) { if (v.game !== gid) continue;
    if (v.status === "pending") { if (!pending || RANK[v.edition] > RANK[pending.edition]) pending = v; }
    else if (!best || RANK[v.edition] > RANK[best.edition]) best = v; }
  return {best, pending};
}

/* ---------------- desktop bridge ---------------- */
async function refreshInstalled(){ if (!PS) return; try { INSTALLED = await PS.getInstalled() || {}; } catch(e) { INSTALLED = {}; } render(); }
if (PS) {
  $("#tabDl").hidden = true;
  PS.onProgress(p => { PROGRESS[p.id] = p; if (p.phase === "done" || p.phase === "error") { delete PROGRESS[p.id]; if (p.phase === "error") toast("ما تم التثبيت: " + (p.error || "خطأ")); else toast("تم تثبيت " + (byId(p.id) ? (byId(p.id).en || byId(p.id).title) : p.id) + " ✓ أيقونتها على سطح المكتب"); refreshInstalled(); } updateProgressUI(); });
  PS.onInstalledChanged && PS.onInstalledChanged(() => refreshInstalled());
  refreshInstalled();
}
function updateProgressUI(){
  const active = Object.values(PROGRESS);
  $("#dlChip").hidden = !active.length;
  if (active.length) { const p = active[0]; $("#dlChipT").textContent = (p.phase === "extract" ? "جاري التثبيت" : "جاري التحميل") + " " + pct(p) + "%"; }
  for (const p of active) { $$(`[data-prog="${p.id}"]`).forEach(el => { el.querySelector("i").style.width = pct(p) + "%"; const t = el.parentElement.querySelector("[data-progt]"); if (t) t.textContent = progText(p); }); }
}
const pct = p => p.total ? Math.min(100, Math.round(p.received / p.total * 100)) : (p.phase === "extract" ? 100 : 0);
const progText = p => p.phase === "extract" ? "جاري فك الملفات…" : `${mb(p.received||0)} / ${mb(p.total||0)}`;

/* the install / play block, Steam style */
function installBlock(g){
  const {best, pending} = ent(g.id), m = MANIFEST.games[g.id];
  if (!best) return "";
  const inst = INSTALLED[g.id], prog = PROGRESS[g.id];
  if (PS) {
    if (prog) return `<div class="inststate"><span data-progt>${progText(prog)}</span><span>${pct(prog)}%</span></div><div class="bar" data-prog="${g.id}"><i style="width:${pct(prog)}%"></i></div>`;
    if (inst) {
      const upd = m && inst.version !== m.version;
      return `<div class="inststate"><span class="st inst">مثبتة</span><span class="num">${esc(inst.version)} · ${mb(inst.size||0)}</span></div>
        <div class="row">${upd ? `<button class="btn inst" data-act="install" data-g="${g.id}">تحديث إلى ${esc(m.version)}</button>` : `<button class="btn play big" data-act="launch" data-g="${g.id}">تشغيل</button>`}
        <button class="btn ghost" data-act="folder" data-g="${g.id}">مجلد اللعبة</button><button class="btn danger" data-act="uninstall" data-g="${g.id}">حذف</button></div>`;
    }
    return `<div class="inststate"><span>غير مثبتة</span>${m ? `<span class="num">${mb(m.installSize)}</span>` : ""}</div><button class="btn inst big" data-act="install" data-g="${g.id}">تثبيت</button>`;
  }
  if (isMobile) return `<div class="inststate"><span>اللعبة للكمبيوتر. افتح الموقع من الكمبيوتر عشان تثبّتها.</span></div>`;
  return `<button class="btn inst big" data-act="getapp" data-g="${g.id}">تثبيت</button><span class="muted" style="font-size:13px">التثبيت يتم من تطبيق Premium Studio</span>`;
}
document.addEventListener("click", async e => {
  const b = e.target.closest("[data-act]"); if (!b) return;
  const id = b.dataset.g, g = byId(id), act = b.dataset.act;
  if (act === "getapp") return getAppModal(g);
  if (!PS) return;
  if (act === "install") { if (!ent(id).best) return; b.disabled = true; PROGRESS[id] = {id, phase:"download", received:0, total:(MANIFEST.games[id]||{}).zipSize||0}; render(); updateProgressUI(); try { await PS.install(id); } catch(err) { delete PROGRESS[id]; toast("ما تم التثبيت: " + (err && err.message || err)); render(); } }
  else if (act === "launch") { try { await PS.launch(id); } catch(err) { toast("ما اشتغلت اللعبة: " + (err && err.message || err)); } }
  else if (act === "folder") { PS.showFolder(id); }
  else if (act === "uninstall") confirmModal(`حذف ${g.en || g.title}؟`, "تنحذف ملفات اللعبة وأيقونتها من سطح المكتب. تقدر تثبّتها مرة ثانية من مكتبتك أي وقت.", "حذف", async () => { try { await PS.uninstall(id); toast("انحذفت اللعبة"); } catch(err) { toast("ما انحذفت: " + (err && err.message || err)); } refreshInstalled(); });
});
function getAppModal(g){
  openModal(`<div class="eyebrow">INSTALL</div><h1 class="mbox-t">ثبّت ${esc(g.en || g.title)}</h1>
    <p class="mbox-t">الألعاب تنثبت من تطبيق Premium Studio على الكمبيوتر، مثل ستيم. التطبيق يحط أيقونة اللعبة على سطح المكتب.</p>
    <a class="btn buy" href="${APP_DL}">تحميل تطبيق Premium Studio</a>
    <a class="btn" href="premiumstudio://game/${g.id}">عندي التطبيق، افتح فيه</a>
    <button class="btn ghost" data-close>إغلاق</button>`);
}

/* ---------------- views ---------------- */
let heroIdx = 0, heroT = 0;
function lowPrice(g){ return Math.min(...g.editions.filter(e => e.price > 0).map(e => e.price)); }
function renderHero(){
  const g = GAMES[heroIdx], {best} = ent(g.id);
  const h = $("#hero"); h.innerHTML = `<div class="art"></div><div class="side">
    <div class="eyebrow">FEATURED · ${esc(g.en || g.title)}</div>
    <h1 class="gtitle ${g.ar ? "ar" : ""}">${esc(g.title)}</h1>
    <div class="muted">${esc(g.tagline)}</div>
    <div class="tags">${g.tags.map(t => `<span class="tag">${esc(t)}</span>`).join("")}</div>
    <div class="price">${best ? '<span class="own">في مكتبتك ✓</span>' : `<small>يبدأ من </small>${money(lowPrice(g))}`}</div>
    <div class="row"><a class="btn buy" href="#${g.id}">صفحة اللعبة</a>${best ? "" : `<button class="btn ghost" data-demo="${g.id}">جرّب الديمو مجانًا</button>`}</div>
    <div class="dots">${GAMES.map((x, i) => `<button aria-label="${esc(x.en || x.title)}" aria-current="${i === heroIdx}" data-i="${i}"></button>`).join("")}</div></div>`;
  cover(h.querySelector(".art"), g.id);
  h.querySelectorAll(".dots button").forEach(b => b.onclick = () => { heroIdx = +b.dataset.i; renderHero(); restartHero(); });
  const d = h.querySelector("[data-demo]"); if (d) d.onclick = () => buy(g, g.editions[0]);
}
function restartHero(){ clearInterval(heroT); if (!reduce) heroT = setInterval(() => { if (curView !== "store" || !$("#modal").hidden) return; heroIdx = (heroIdx + 1) % GAMES.length; renderHero(); }, 8000); }
function renderGrid(){
  const box = $("#grid"); box.innerHTML = "";
  GAMES.forEach(g => { const {best} = ent(g.id);
    const a = document.createElement("a"); a.className = "card"; a.href = "#" + g.id;
    a.innerHTML = `<div class="art"></div><div class="body"><div class="t ${g.ar ? "ar" : ""}">${esc(g.title)}</div><div class="muted" style="font-size:13px">${esc(g.sub)}</div>
      <div class="ft">${best ? `<span class="own">في مكتبتك · ${esc(edName(g, best.edition))}${INSTALLED[g.id] ? " · مثبتة" : ""}</span>` : '<span class="tag">ديمو مجاني</span>'}<span class="price" style="font-size:16px">${money(lowPrice(g))}</span></div></div>`;
    box.appendChild(a); cover(a.querySelector(".art"), g.id); });
  const s = document.createElement("div"); s.className = "card soon"; s.innerHTML = "<b>COMING SOON</b><span>ألعاب جديدة من بريميوم ستوديو قريبًا</span>"; box.appendChild(s);
}
const ST = {free:["free","ديمو مجاني"], pending:["pending","بانتظار الدفع"], paid:["paid","مدفوع"], granted:["granted","مجاني من الاستوديو"]};
const stPill = s => { const [c, n] = ST[s] || ST.free; return `<span class="st ${c}">${n}</span>`; };
function renderGame(g){
  const v = $("#gameView"), {best, pending} = ent(g.id), m = MANIFEST.games[g.id];
  const mineRank = best ? RANK[best.edition] : -1;
  v.innerHTML = `<div class="banner"><div class="shade"></div><div class="cap"><div class="eyebrow">PREMIUM STUDIO${g.en ? " · " + esc(g.en) : ""}</div><h1 class="gtitle ${g.ar ? "ar" : ""}">${esc(g.title)}</h1><div class="muted">${esc(g.tagline)}</div></div></div>
  <div class="gp"><div class="about">
    <div>${g.about.map(p => `<p>${esc(p)}</p>`).join("")}</div>
    <div><div class="sec-h" style="margin-bottom:12px"><h2>المميزات</h2><small>FEATURES</small></div><ul class="feat">${g.feat.map(f => `<li>${esc(f)}</li>`).join("")}</ul></div>
    <dl class="specs"><dt>المطوّر</dt><dd>Premium Studio</dd><dt>المنصة</dt><dd>Windows 10 / 11</dd><dt>النوع</dt><dd>${g.tags.map(esc).join(" · ")}</dd>${m ? `<dt>الإصدار</dt><dd class="num">${esc(m.version)}</dd><dt>الحجم</dt><dd class="num">${mb(m.installSize)}</dd>` : ""}</dl>
  </div>
  <aside class="eds">
    ${best ? `<div class="ownbox"><div class="row" style="justify-content:space-between"><b>في مكتبتك</b>${stPill(best.status)}</div><div class="muted" style="font-size:13px">${esc(edName(g, best.edition))} · ${fdate(best.at)}</div>${installBlock(g)}</div>` : ""}
    ${pending ? `<div class="ownbox pend"><div class="row" style="justify-content:space-between"><b>${esc(edName(g, pending.edition))}</b>${stPill("pending")}</div><div class="muted" style="font-size:13px">طلبك وصل للاستوديو. تقدر تلعب بعد تأكيد الدفع.</div></div>` : ""}
    ${g.editions.map(e => { const has = RANK[e.id] <= mineRank, isPend = pending && pending.edition === e.id;
      return `<div class="ed ${e.gold ? "gold" : ""}"><div class="h"><span class="n">${esc(e.name)}${e.egg ? '<span class="egg" aria-hidden="true"></span>' : ""}</span><span class="price" style="font-size:18px">${money(e.price)}</span></div><div class="d">${esc(e.d)}</div>
      ${has ? '<button class="btn" disabled>عندك هذا الإصدار</button>' : isPend ? '<button class="btn" disabled>بانتظار الدفع</button>' : `<button class="btn ${e.price ? "buy" : ""}" data-ed="${e.id}">${e.price ? (best ? "ترقية إلى هذا الإصدار" : "شراء") : "أضف الديمو للمكتبة"}</button>`}</div>`; }).join("")}
  </aside></div>`;
  cover(v.querySelector(".banner"), g.id);
  v.querySelectorAll("[data-ed]").forEach(b => b.onclick = () => buy(g, g.editions.find(e => e.id === b.dataset.ed)));
}
function renderLib(){
  const box = $("#lib"), bar = $("#libBar"); box.innerHTML = ""; bar.innerHTML = "";
  if (!user) { box.innerHTML = '<div class="empty"><b style="color:var(--text)">المكتبة للمسجّلين فقط</b><span>سجّل دخولك عشان تضيف ألعاب لمكتبتك وتثبّتها.</span><button class="btn buy" type="button" data-login>تسجيل الدخول</button></div>'; return; }
  const mine = GAMES.filter(g => ent(g.id).best || ent(g.id).pending);
  if (PS) { const n = GAMES.filter(g => INSTALLED[g.id]).length, sz = Object.values(INSTALLED).reduce((a, x) => a + (x.size || 0), 0); bar.innerHTML = `<span class="tag">${mine.length} في المكتبة</span><span class="tag">${n} مثبتة · <span class="num">${mb(sz)}</span></span>`; }
  else if (!isMobile) bar.innerHTML = `<span>عشان تثبّت ألعابك على الكمبيوتر تحتاج تطبيق Premium Studio.</span><a class="btn mini buy" href="#download">حمّل التطبيق</a>`;
  if (!mine.length) { box.innerHTML = '<div class="empty"><b style="color:var(--text)">مكتبتك فاضية</b><span>أي لعبة تشتريها أو تضيف ديمو حقها تطلع هنا.</span><a class="btn buy" href="#store">تصفّح المتجر</a></div>'; return; }
  mine.forEach(g => { const {best, pending} = ent(g.id), o = best || pending; const d = document.createElement("div"); d.className = "li";
    d.innerHTML = `<a class="art" href="#${g.id}" aria-label="${esc(g.en || g.title)}"></a><div class="info"><b style="font:700 19px ${g.ar ? "var(--kufi)" : "var(--display)"}">${esc(g.title)}</b>
      <div class="row" style="gap:8px"><span class="muted" style="font-size:13px">${esc(edName(g, o.edition))}</span>${stPill(o.status)}${INSTALLED[g.id] ? '<span class="st inst">مثبتة</span>' : ""}</div>
      <span class="muted" style="font-size:12px">أضيفت ${fdate(o.at)}${pending && best ? " · ترقية بانتظار الدفع" : ""}</span></div>
      <div class="acts">${best ? installBlock(g) : '<span class="muted" style="font-size:13px">تقدر تلعب بعد تأكيد الدفع</span>'}</div>`;
    box.appendChild(d); cover(d.querySelector(".art"), g.id); });
}
function renderAccount(){
  const a = $("#acct");
  if (!user) { a.innerHTML = '<div class="empty" style="grid-column:1/-1"><b style="color:var(--text)">ما سجّلت دخول</b><button class="btn buy" type="button" data-login>تسجيل الدخول</button></div>'; return; }
  a.innerHTML = `<div class="panel"><h3>معلوماتك</h3><dl class="kv"><dt>رقم الحساب</dt><dd class="num ltr">${fmtPid(PROFILE && PROFILE.pid)}</dd><dt>الاسم</dt><dd id="aName"></dd><dt>الإيميل</dt><dd class="ltr" id="aMail"></dd><dt>التحقق</dt><dd>${user.emailVerified ? '<span class="st paid">الإيميل مؤكد</span>' : '<span class="st pending">غير مؤكد</span>'}</dd><dt>عضو من</dt><dd>${fdate(PROFILE && PROFILE.joined)}</dd></dl>
    ${user.emailVerified ? "" : '<div class="row"><button class="btn mini" id="aVerify" type="button">أرسل رابط التأكيد مرة ثانية</button><button class="btn mini ghost" id="aReload" type="button">أكدت، حدّث</button></div>'}</div>
    <div class="panel"><h3>الحساب</h3><p class="muted" style="font-size:14px">نفس الحساب يشتغل في الموقع وفي تطبيق Premium Studio.</p><div class="row"><button class="btn mini" id="aReset" type="button">تغيير كلمة السر</button><button class="btn mini danger" id="aOut" type="button">تسجيل الخروج</button></div></div>`;
  $("#aName").textContent = (PROFILE && PROFILE.name) || user.displayName || "—"; $("#aMail").textContent = user.email;
  $("#aOut").onclick = async () => { await signOut(auth); toast("سجّلت خروج"); location.hash = "#store"; };
  $("#aReset").onclick = async () => { try { await sendPasswordResetEmail(auth, user.email); toast("أرسلنا رابط تغيير كلمة السر لإيميلك"); } catch(e) { toast(authErr(e)); } };
  const v = $("#aVerify"); if (v) v.onclick = async () => { try { await sendEmailVerification(user); toast("أرسلنا رابط التأكيد لإيميلك"); } catch(e) { toast(authErr(e)); } };
  const rl = $("#aReload"); if (rl) rl.onclick = async () => { await user.reload(); await user.getIdToken(true); setAdmin(); render(); toast(user.emailVerified ? "تم تأكيد الإيميل ✓" : "لسا ما تأكد. افتح الرابط في إيميلك"); };
}

/* ---------------- studio dashboard (admin) ---------------- */
let USERS = {}, ORDERS = [];
function startAdmin(){
  unsubs.push(onSnapshot(collection(fs, "users"), s => { USERS = {}; s.forEach(d => USERS[d.id] = d.data()); if (curView === "studio") renderStudio(); }, e => { $("#studioNote").textContent = "ما قدرت أقرأ قائمة اللاعبين: " + e.code; }));
  unsubs.push(onSnapshot(collection(fs, "codes"), s => { CODES = {}; s.forEach(d => CODES[d.id] = d.data()); if (curView === "studio") renderCodes(); }, () => {}));
  unsubs.push(onSnapshot(collectionGroup(fs, "library"), s => { ORDERS = s.docs.map(d => ({ref:d.ref, uid:d.ref.parent.parent.id, ...d.data()})); if (curView === "studio") renderStudio(); }, e => { $("#studioNote").textContent = "ما قدرت أقرأ الطلبات: " + e.code; }));
}
function renderStudio(){
  const byUser = {}; ORDERS.forEach(o => (byUser[o.uid] = byUser[o.uid] || []).push(o));
  const paid = ORDERS.filter(o => o.status === "paid").reduce((a, o) => a + (o.price || 0), 0), pend = ORDERS.filter(o => o.status === "pending").length;
  $("#kpis").innerHTML = `<div class="kpi"><b>${Object.keys(USERS).length}</b><span>لاعب مسجّل</span></div><div class="kpi"><b>${ORDERS.length}</b><span>طلب ولعبة في المكتبات</span></div><div class="kpi"><b>${pend}</b><span>بانتظار الدفع</span></div><div class="kpi"><b>${money(paid)}</b><span>إيرادات مؤكدة</span></div>`;
  const q = $("#q").value.trim().toLowerCase().replace(/\s+/g, " "), qd = q.replace(/[\s#]/g, ""), fg = $("#fGame").value, fst = $("#fSt").value;
  const list = Object.entries(USERS).map(([id, u]) => ({id, ...u, orders: byUser[id] || []})).filter(p => {
    if (q && !((p.name || "") + " " + (p.email || "")).toLowerCase().includes(q) && !(qd && String(p.pid || "").includes(qd))) return false;
    if (fg && !p.orders.some(o => o.game === fg)) return false;
    if (fst && !p.orders.some(o => (!fg || o.game === fg) && o.status === fst)) return false; return true;
  }).sort((a, b) => tsMs(b.joined) - tsMs(a.joined));
  const tb = $("#rows"); tb.innerHTML = "";
  if (!list.length) tb.innerHTML = `<tr><td colspan="5" class="muted" style="text-align:center;padding:24px">${Object.keys(USERS).length ? "ما فيه نتائج تطابق البحث" : "ما سجّل أحد للحين."}</td></tr>`;
  list.forEach(p => { const tr = document.createElement("tr");
    tr.innerHTML = `<td class="pidtag">${fmtPid(p.pid)}</td><td><b></b></td><td class="ltr"></td><td>${fdate(p.joined)}</td><td><div class="gl">${p.orders.length ? p.orders.map((o, i) => { const g = byId(o.game);
      return `<div class="gi"><span>${esc(g ? (g.en || g.title) : o.game)} · ${esc(g ? edName(g, o.edition) : o.edition)}</span><span class="num muted">${money(o.price || 0)}</span>${o.code ? `<span class="tag ltr">${esc(o.code)}</span>` : ""}${stPill(o.status)}${o.status === "pending" ? `<button class="btn mini" data-pay="${i}">تأكيد الدفع</button><button class="btn mini" data-gift="${i}">منح مجاني</button><button class="btn mini danger" data-rej="${i}">رفض</button>` : ""}</div>`; }).join("") : '<span class="muted">ما عنده ألعاب</span>'}</div></td>`;
    tr.children[1].firstChild.textContent = p.name || "—"; tr.children[2].textContent = p.email || "—";
    const setSt = async (b, i, status, msg) => { b.disabled = true; try { await updateDoc(p.orders[i].ref, {status, paidAt:serverTimestamp()}); toast(msg); } catch(e) { b.disabled = false; toast("ما تحدّث الطلب: " + e.code); } };
    tr.querySelectorAll("[data-pay]").forEach(b => b.onclick = () => setSt(b, +b.dataset.pay, "paid", "تم تأكيد الدفع ✓"));
    tr.querySelectorAll("[data-gift]").forEach(b => b.onclick = () => setSt(b, +b.dataset.gift, "granted", "انمنحت اللعبة مجانًا ✓"));
    tr.querySelectorAll("[data-rej]").forEach(b => b.onclick = () => confirmModal("رفض الطلب؟", "ينحذف الطلب من مكتبة اللاعب.", "رفض", async () => { try { await deleteDoc(p.orders[+b.dataset.rej].ref); toast("انرفض الطلب"); } catch(e) { toast("ما انحذف: " + e.code); } }));
    tb.appendChild(tr); });
  renderCodes();
}
["#q", "#fGame", "#fSt"].forEach(s => $(s).addEventListener("input", renderStudio));

/* ---------------- discount codes (admin) ---------------- */
let CODES = {};
const normCode = c => String(c || "").trim().toUpperCase().replace(/[^A-Z0-9_-]/g, "");
function renderCodes(){
  const tb = $("#codeRows"); if (!tb) return; tb.innerHTML = "";
  const ids = Object.keys(CODES).sort();
  if (!ids.length) { tb.innerHTML = '<tr><td colspan="7" class="muted" style="text-align:center;padding:20px">ما فيه أكواد للحين. أضف أول كود من فوق.</td></tr>'; return; }
  for (const id of ids) { const c = CODES[id], g = c.game ? byId(c.game) : null, used = ORDERS.filter(o => o.code === id).length, expired = c.expires && tsMs(c.expires) < Date.now();
    const tr = document.createElement("tr");
    tr.innerHTML = `<td class="ltr num"><b>${esc(id)}</b></td><td class="num">${c.pct}%</td><td>${g ? esc(g.en || g.title) : "كل الألعاب"}</td><td>${c.expires ? fdate(c.expires) : "بدون"}</td><td class="num">${used}</td><td>${expired ? '<span class="st free">منتهي</span>' : c.active ? '<span class="st paid">شغّال</span>' : '<span class="st pending">موقوف</span>'}</td>
      <td><div class="row" style="gap:6px"><button class="btn mini" data-tog>${c.active ? "إيقاف" : "تشغيل"}</button><button class="btn mini danger" data-del>حذف</button></div></td>`;
    tr.querySelector("[data-tog]").onclick = async () => { try { await updateDoc(doc(fs, "codes", id), {active:!c.active}); } catch(e) { toast("ما تحدّث الكود: " + e.code); } };
    tr.querySelector("[data-del]").onclick = () => confirmModal(`حذف الكود ${id}؟`, "ما يقدر أحد يستخدمه بعدها. الطلبات القديمة اللي استخدمته تبقى.", "حذف", async () => { try { await deleteDoc(doc(fs, "codes", id)); toast("انحذف الكود"); } catch(e) { toast("ما انحذف: " + e.code); } });
    tb.appendChild(tr); }
}
$("#codeForm").addEventListener("submit", async ev => { ev.preventDefault();
  const err = $("#cErr"); err.textContent = "";
  const code = normCode($("#cCode").value), pct = Math.round(+$("#cPct").value), game = $("#cGame").value, exp = $("#cExp").value;
  if (code.length < 3) { err.textContent = "الكود لازم يكون 3 أحرف أو أكثر (إنجليزي وأرقام)"; return; }
  if (!(pct >= 1 && pct <= 100)) { err.textContent = "نسبة الخصم من 1 إلى 100"; return; }
  if (CODES[code]) { err.textContent = "هذا الكود موجود"; return; }
  const data = {pct, game, active:true, created:serverTimestamp(), expires: exp ? Timestamp.fromDate(new Date(exp + "T23:59:59")) : null};
  try { await setDoc(doc(fs, "codes", code), data); $("#cCode").value = ""; toast(`انضاف الكود ${code} ✓`); } catch(e) { err.textContent = "ما انحفظ الكود: " + e.code; }
});

/* ---------------- buy flow ---------------- */
function buy(g, e){
  if (!user) return showAuth("login", "سجّل دخولك عشان تضيف ألعاب لمكتبتك.");
  let applied = null;   // {id, pct}
  openModal(`<div class="eyebrow">${e.price ? "CHECKOUT" : "FREE DEMO"}</div><h1 class="mbox-t">${esc(e.name)}</h1>
    <div class="row" style="justify-content:space-between"><span class="muted">${esc(g.en || g.title)}</span><span class="price" id="bPrice">${money(e.price)}</span></div>
    ${e.price ? `<label class="fld">كود الخصم<div class="coderow"><input id="bCode" dir="ltr" maxlength="20" placeholder="اكتب الكود" autocomplete="off"><button class="btn" id="bApply" type="button">تطبيق</button></div></label><div class="okmsg" id="bCodeMsg"></div>` : ""}
    <p class="mbox-t" id="bNote">${e.price ? "الدفع الإلكتروني ما تفعّل للحين. لما تأكد الطلب يوصل للاستوديو بحالة «بانتظار الدفع»، وتقدر تلعب أول ما يتأكد الدفع." : "الديمو مجاني ويتضاف لمكتبتك على طول."}</p>
    <div class="err" id="bErr"></div>
    <div class="row"><button class="btn buy" id="bOk" type="button">${e.price ? "تأكيد الطلب" : "أضف للمكتبة"}</button><button class="btn ghost" data-close type="button">إلغاء</button></div>`);
  const showPrice = () => { const fp = applied ? finalPrice(e.price, applied.pct) : e.price;
    $("#bPrice").innerHTML = applied ? `<span class="strike">${money(e.price)}</span>${money(Math.round(fp * 100) / 100)}` : money(e.price);
    if (applied && fp === 0) { $("#bNote").textContent = "الكود يخلي اللعبة مجانية. تنضاف لمكتبتك وتقدر تلعبها على طول."; $("#bOk").textContent = "أضف للمكتبة"; } };
  const ap = $("#bApply"); if (ap) ap.onclick = async () => {
    const id = normCode($("#bCode").value), msg = $("#bCodeMsg"); msg.className = "okmsg"; msg.textContent = ""; applied = null; showPrice();
    if (!id) return;
    ap.disabled = true;
    try { const s = await getDoc(doc(fs, "codes", id)); const c = s.exists() ? s.data() : null;
      if (!c || !c.active) throw "الكود غلط أو موقوف";
      if (c.expires && tsMs(c.expires) < Date.now()) throw "الكود منتهي";
      if (c.game && c.game !== g.id) throw "الكود ما يشتغل على هذي اللعبة";
      applied = {id, pct:c.pct}; msg.textContent = `تم تطبيق خصم ${c.pct}% ✓`; showPrice(); }
    catch(x) { msg.className = "err"; msg.textContent = typeof x === "string" ? x : "ما قدرت أتحقق من الكود"; }
    ap.disabled = false; };
  $("#bOk").onclick = async () => { const btn = $("#bOk"); btn.disabled = true;
    const price = applied ? finalPrice(e.price, applied.pct) : e.price;
    const status = e.price === 0 ? "free" : price === 0 ? "granted" : "pending";
    const data = {game:g.id, edition:e.id, price, status, at:serverTimestamp()}; if (applied) data.code = applied.id;
    try { await setDoc(doc(fs, "users", user.uid, "library", g.id + "__" + e.id), data);
      closeModal(); toast(status === "pending" ? "وصل طلبك ✓ تلقاه في مكتبتك" : "انضافت لمكتبتك ✓"); }
    catch(err) { btn.disabled = false; $("#bErr").textContent = "ما انحفظ الطلب (" + (err.code || "خطأ") + "). جرّب مرة ثانية."; } };
}

/* ---------------- modal helpers ---------------- */
function openModal(html){ $("#mbox").innerHTML = html; $("#modal").hidden = false; }
function closeModal(){ $("#modal").hidden = true; $("#mbox").innerHTML = ""; }
$("#modal").addEventListener("click", e => { if (e.target.id === "modal" || e.target.closest("[data-close]")) closeModal(); });
addEventListener("keydown", e => { if (e.key === "Escape" && !$("#modal").hidden) closeModal(); });
function confirmModal(title, text, okLabel, fn){
  openModal(`<h1 class="mbox-t">${esc(title)}</h1><p class="mbox-t">${esc(text)}</p><div class="row"><button class="btn danger" id="cOk" type="button">${esc(okLabel)}</button><button class="btn ghost" data-close type="button">إلغاء</button></div>`);
  $("#cOk").onclick = () => { closeModal(); fn(); };
}
document.addEventListener("click", e => { if (e.target.closest("[data-login]")) showAuth("login"); });
$("#signIn").onclick = () => showAuth("login");

/* ---------------- routing ---------------- */
let curView = "store";
function route(){
  const h = (location.hash || "#store").slice(1), g = byId(h);
  let v = g ? "game" : (["store", "library", "studio", "download", "account"].includes(h) ? h : "store");
  if (v === "studio" && !isAdmin) v = "store";
  curView = v; $$(".view").forEach(s => s.hidden = s.dataset.view !== v);
  $$("#tabs a").forEach(a => a.setAttribute("aria-current", a.dataset.v === (g ? "store" : v) ? "page" : "false"));
  render(); scrollTo(0, 0);
}
function guestNote(){
  const v = $(".view:not([hidden])"); if (!v) return; let n = v.querySelector(".guestnote");
  const show = authReady && !user && curView !== "download";
  if (!show) { if (n) n.remove(); return; }
  if (!n) { n = document.createElement("div"); n.className = "guestnote"; n.innerHTML = '<span>أنت تتصفح كضيف. تقدر تتفرج بس، وللشراء والتثبيت لازم تسجّل دخولك.</span><button class="btn buy mini" type="button" data-login>تسجيل الدخول</button>'; v.prepend(n); }
}
function render(){
  if (!authReady) return;
  guestNote();
  if (curView === "store") { renderHero(); renderGrid(); }
  else if (curView === "game") renderGame(byId(location.hash.slice(1)));
  else if (curView === "library") renderLib();
  else if (curView === "studio") renderStudio();
  else if (curView === "account") renderAccount();
  else if (curView === "download") { $("#dlInApp").hidden = !PS; $("#dlBtn").hidden = !!PS; }
}
addEventListener("hashchange", route);

/* ---------------- auth ---------------- */
const AUTH_ERR = {
  "auth/email-already-in-use":"هذا الإيميل مسجّل من قبل. سجّل دخول بدل ما تنشئ حساب جديد.",
  "auth/invalid-email":"الإيميل مكتوب غلط.",
  "auth/weak-password":"كلمة السر ضعيفة. خلها 6 أحرف أو أكثر.",
  "auth/invalid-credential":"الإيميل أو كلمة السر غلط.",
  "auth/wrong-password":"كلمة السر غلط.",
  "auth/user-not-found":"ما فيه حساب بهذا الإيميل.",
  "auth/too-many-requests":"محاولات كثيرة. انتظر شوي وجرّب مرة ثانية.",
  "auth/network-request-failed":"ما فيه اتصال بالإنترنت.",
  "auth/operation-not-allowed":"التسجيل بالإيميل ما تفعّل في Firebase للحين.",
  "auth/unauthorized-domain":"هذا الموقع مو مضاف في Firebase (Authorized domains)."
};
const authErr = e => AUTH_ERR[e && e.code] || ("صار خطأ" + (e && e.code ? " (" + e.code + ")" : ""));
const brandHTML = '<div class="brand"><span class="mark">P</span><b>PREMIUM <span>STUDIO</span></b></div>';
function showAuth(mode, note){
  closeModal();
  const login = mode === "login";
  $("#gbox").innerHTML = `${brandHTML}<h1>${login ? "تسجيل الدخول" : "أنشئ حسابك"}</h1>${note ? `<p>${esc(note)}</p>` : '<p>حسابك يحفظ مكتبتك ومشترياتك، ويشتغل في الموقع والتطبيق.</p>'}
    <div class="seg"><button type="button" data-m="login" aria-pressed="${login}">تسجيل الدخول</button><button type="button" data-m="signup" aria-pressed="${!login}">حساب جديد</button></div>
    <form class="f" id="af" novalidate>
      ${login ? "" : '<label class="fld">اسم اللاعب<input id="aN" autocomplete="nickname" maxlength="32" required></label>'}
      <label class="fld">الإيميل<input id="aE" type="email" dir="ltr" autocomplete="email" maxlength="120" required placeholder="name@example.com"></label>
      <label class="fld">كلمة السر<input id="aP" type="password" dir="ltr" autocomplete="${login ? "current-password" : "new-password"}" minlength="6" required></label>
      ${login ? '<button class="link" type="button" id="aForgot">نسيت كلمة السر؟</button>' : '<label class="fld">أعد كلمة السر<input id="aP2" type="password" dir="ltr" autocomplete="new-password" required></label><label class="chk"><input type="checkbox" id="aAgree">أوافق إن بريميوم ستوديو يحفظ اسمي وإيميلي ومشترياتي</label>'}
      <div class="err" id="aErr"></div>
      <button class="btn buy" id="aGo" type="submit">${login ? "دخول" : "إنشاء الحساب"}</button>
    </form><div class="or">أو</div><button class="btn ghost" id="aGuest" type="button">تصفّح كضيف</button>`;
  $("#gate").hidden = false;
  $$("#gbox [data-m]").forEach(b => b.onclick = () => showAuth(b.dataset.m, note));
  $("#aGuest").onclick = () => { guest = true; try { sessionStorage.setItem("ps-guest", "1"); } catch(e) {} $("#gate").hidden = true; applyChrome(); render(); };
  const fg = $("#aForgot"); if (fg) fg.onclick = async () => { const m = $("#aE").value.trim(); if (!m) { $("#aErr").textContent = "اكتب إيميلك أول، بعدين اضغط نسيت كلمة السر."; return; }
    try { await sendPasswordResetEmail(auth, m); $("#aErr").className = "okmsg"; $("#aErr").textContent = "إذا الإيميل مسجّل، بيوصلك رابط تغيير كلمة السر."; } catch(e) { $("#aErr").textContent = authErr(e); } };
  setTimeout(() => { const f = $("#gbox input"); if (f && !isMobile) f.focus(); }, 30);
  $("#af").addEventListener("submit", async ev => { ev.preventDefault();
    const err = $("#aErr"); err.className = "err"; err.textContent = "";
    const m = $("#aE").value.trim().toLowerCase(), p = $("#aP").value;
    if (!/^[^\s@]+@[^\s@]+\.[^\s@]{2,}$/.test(m)) { err.textContent = "اكتب إيميل صحيح، مثل name@example.com"; return; }
    if (p.length < 6) { err.textContent = "كلمة السر لازم تكون 6 أحرف أو أكثر"; return; }
    let n = "";
    if (!login) { n = $("#aN").value.trim();
      if (n.length < 2) { err.textContent = "اكتب اسم لاعب من حرفين أو أكثر"; return; }
      if (p !== $("#aP2").value) { err.textContent = "كلمتين السر مو متطابقة"; return; }
      if (!$("#aAgree").checked) { err.textContent = "لازم توافق عشان نكمل التسجيل"; return; } }
    $("#aGo").disabled = true;
    try {
      if (login) await signInWithEmailAndPassword(auth, m, p);
      else { pendingName = n; const c = await createUserWithEmailAndPassword(auth, m, p); await updateProfile(c.user, {displayName:n}); await ensureProfile(c.user, n); sendEmailVerification(c.user).catch(()=>{}); toast("تم إنشاء حسابك ✓ أرسلنا رابط تأكيد لإيميلك"); }
    } catch(e) { $("#aGo").disabled = false; err.textContent = authErr(e); }
  });
}
let pendingName = "";
/* every account gets a random 10-digit account number (leading zeros allowed), reserved in playerIds/<pid> so no two accounts share one; it never changes */
const newPid = () => { const a = new Uint32Array(2); crypto.getRandomValues(a); return String((a[0] * 4294967296 + a[1]) % 10000000000).padStart(10, "0"); };
async function ensureProfile(u, name){
  const r = doc(fs, "users", u.uid);
  const s = await getDoc(r);
  if (s.exists() && s.data().pid) return;
  for (let i = 0; i < 5; i++) {
    const pid = newPid(), b = writeBatch(fs);
    b.set(doc(fs, "playerIds", pid), {uid:u.uid, at:serverTimestamp()});
    if (s.exists()) b.update(r, {pid});
    else b.set(r, {pid, name:(name || u.displayName || u.email.split("@")[0]).slice(0, 32), email:u.email.toLowerCase(), joined:serverTimestamp()});
    try { await b.commit(); return; } catch(e) { if (i === 4) throw e; }   // a taken number fails the batch, so try another
  }
}
function setAdmin(){ isAdmin = !!(user && user.email && user.email.toLowerCase() === ADMIN_EMAIL && user.emailVerified); $("#tabStudio").hidden = !isAdmin; }
function applyChrome(){
  $("#me").hidden = !user; $("#guestBar").hidden = !!user || !authReady;
  if (user) { const n = (PROFILE && PROFILE.name) || user.displayName || user.email; $("#meName").textContent = n; $("#meAv").textContent = (n || "?").trim().charAt(0).toUpperCase(); $("#mePid").textContent = PROFILE && PROFILE.pid ? "#" + PROFILE.pid : ""; }
}
onAuthStateChanged(auth, async u => {
  unsubs.forEach(f => { try { f(); } catch(e) {} }); unsubs = []; LIB = {}; PROFILE = null; USERS = {}; ORDERS = []; CODES = {};
  user = u; authReady = true; setAdmin();
  if (u) {
    $("#gate").hidden = true; guest = false;
    try { await ensureProfile(u, pendingName); } catch(e) { toast("ما قدرت أفتح حسابك في قاعدة البيانات: " + (e.code || e.message)); }
    unsubs.push(onSnapshot(doc(fs, "users", u.uid), s => { PROFILE = s.exists() ? s.data() : null; applyChrome(); if (curView === "account") renderAccount(); }));
    unsubs.push(onSnapshot(collection(fs, "users", u.uid, "library"), s => { LIB = {}; s.forEach(d => LIB[d.id] = d.data()); render(); }, e => toast("ما قدرت أقرأ مكتبتك: " + e.code)));
    if (isAdmin) startAdmin();
    if (PS && PS.setUser) PS.setUser({name:u.displayName || "", uid:u.uid});
  } else {
    let g = false; try { g = sessionStorage.getItem("ps-guest") === "1"; } catch(e) {}
    if (!g && !guest) showAuth("signup"); else $("#gate").hidden = true;
    if (PS && PS.setUser) PS.setUser(null);
  }
  applyChrome(); route();
});
restartHero();
