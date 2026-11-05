import { makeBP } from "./bp.js?v=1";
import { makeRTC } from "./rtc.js?v=1";
import { initializeApp } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-app.js";
import { getAuth, onAuthStateChanged, createUserWithEmailAndPassword, signInWithEmailAndPassword, signOut,
  sendPasswordResetEmail, sendEmailVerification, updateProfile, setPersistence, browserLocalPersistence } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-auth.js";
import { getFirestore, doc, getDoc, setDoc, updateDoc, deleteDoc, collection, collectionGroup, onSnapshot,
  serverTimestamp, writeBatch, Timestamp, increment, addDoc, query, orderBy, limit, where, arrayUnion, arrayRemove } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-firestore.js";

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
/* PS COIN balances are stored as whole ten-thousandths (1 = 0.0001 PS COIN) so sums stay exact. 1 PS COIN = $100,000. */
const COIN_UNIT = 10000;
const COIN_USD = 100000;
const SIGNUP_BONUS = 1;     // 0.0001 PS COIN for every new account
const PACKS = [             // must match coinPacks() in firestore.rules (coins in ten-thousandths)
  {id:"u1",     coins:1,      price:10},
  {id:"u100",   coins:100,    price:1000},
  {id:"u1000",  coins:1000,   price:10000},
  {id:"u10000", coins:10000,  price:100000},
  {id:"u100000",coins:100000, price:1000000}
];
const coins = n => ((n || 0) / COIN_UNIT).toLocaleString("en-US", {minimumFractionDigits:0, maximumFractionDigits:4});
const SITE_VERSION = "00.00.25";
const APP_DL = "downloads/PremiumStudioSetup.exe";

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
   editions:[{id:"demo",name:"Demo",price:0,d:"جرّب أول العالم مجانًا"},{id:"std",name:"BLOCKFALL 3001",price:39.99,d:"اللعبة كاملة"},{id:"gold",name:"BLOCKFALL 3001 Gold",price:99.99,d:"الإصدار الذهبي",gold:true},{id:"dev",name:"BLOCKFALL 3001 المبرمج",price:null,studio:true,d:"فيه «المبرمج»: تعديل خلفية القائمة ومشهدها"}]},
  {id:"last-recording", title:"التسجيل الأخير", en:"THE LAST RECORDING", ar:true, sub:"رعب نفسي · قصة · أونلاين",
   tagline:"شريط واحد. ليلة واحدة. مول ما يطلع منه أحد.",
   about:["ليلة 3 نوفمبر 1999، الساعة 3:33 الفجر. مول متعدد الطوابق بإضاءة نيون، وكاميرا فيديو قديمة في يدك ما زالت تسجّل.",
          "دوّر على أشرطة المراقبة الخمسة واكشف وش صار هنا. رعب نفسي بمنظور أول شخص مستوحى من Resident Evil و Silent Hill."],
   feat:["منظور كاميرا REC بالكامل","مول بإضاءة LED وانعكاسات على الأرضيات","مسدس وذخيرة ودم","العب أونلاين مع أصحابك"],
   tags:["رعب نفسي","قصة","أونلاين"],
   editions:[{id:"demo",name:"Demo",price:0,d:"جرّب أول جزء مجانًا"},{id:"std",name:"التسجيل الأخير",price:69.99,d:"اللعبة كاملة"},{id:"gold",name:"التسجيل الأخير · الذهبي",price:999999.99,d:"الإصدار الذهبي مع البيضة الذهبية",gold:true,egg:true}]}
];
const RANK = {demo:0, std:1, gold:2, dev:3};
const byId = id => GAMES.find(g => g.id === id);
const edName = (g, id) => (g.editions.find(e => e.id === id) || {name:id}).name;
GAMES.forEach(g => { for (const sel of ["#fGame", "#cGame"]) { const o = document.createElement("option"); o.value = g.id; o.textContent = g.en || g.title; $(sel).appendChild(o); } });
let MANIFEST = {games:{}};
/* latest versions straight from each game's own update source (the same one the game updates from) */
let LATEST = {};
const noCache = u => u + (u.includes("?") ? "&" : "?") + "t=" + Date.now();
async function loadLatest(){
  for (const [id, m] of Object.entries(MANIFEST.games || {})) {
    try {
      if (m.type === "bf-launcher") { const [p, g] = await Promise.all([fetch(noCache(m.parts)).then(r => r.json()), fetch(noCache(m.gameManifest)).then(r => r.json())]); LATEST[id] = {version:p.version, game:g.latest && g.latest.release, size:p.size}; }
      else if (m.type === "manifest") { const j = await fetch(noCache(m.base + "manifest.json")).then(r => r.json()); LATEST[id] = {version:j.version}; }
    } catch(e) {}
  }
  render();
}
fetch("games.json", {cache:"no-store"}).then(r => r.json()).then(j => { MANIFEST = j; render(); loadLatest(); }).catch(()=>{});
setInterval(() => { if (document.visibilityState === "visible") loadLatest(); }, 10 * 60 * 1000);   // pick up new releases while the store stays open
/* the store's access code for BLOCKFALL's launcher download; only owners can read it */
async function bfCode(){ try { const s = await getDoc(doc(fs, "keys", "blockfall")); return s.exists() ? s.data().code : ""; } catch(e) { return ""; } }

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
let SHARED = {}, FAMILY = null, CARDS = {}, DROPS = {}, CBADGES = {}, LISTINGS = [], GIFTS = [], ALLGIFTS = [];
let INSTALLED = {}, PROGRESS = {}, RUNNING = {}, PLAY = {}, PRES = {}, REFUNDS = {}, ACH = {}, PROFS = {}, WISH = {}, PREFS = {fav:[], hidden:[]}, REVIEWS = {}, NEWS = {};
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
async function refreshInstalled(){ if (!PS) return; try { INSTALLED = await PS.getInstalled() || {}; } catch(e) { INSTALLED = {}; } render(); autoInstall(); }
let AUTO = (() => { try { return new URLSearchParams(location.search).get("install") || ""; } catch(e) { return ""; } })();
function autoInstall(){
  if (!AUTO || !PS || !user) return;
  const [gid, ed] = AUTO.split("--"), it = LIB[`${gid}__${ed}`];
  if (!it) return;   // library not loaded yet
  AUTO = ""; try { history.replaceState(null, "", location.pathname + location.hash); } catch(e) {}
  if (it.status === "pending") return toast("هذا الإصدار بانتظار الدفع");
  if (instOf(gid, ed)[1]) return toast("هذي النسخة مثبتة من قبل");
  const b = document.querySelector(`[data-act="install"][data-gid="${gid}"][data-ed="${ed}"]`); if (b) b.click();
}
if (PS) {
  $("#tabDl").hidden = true; $("#tabDls").hidden = false;
  PS.onProgress(p => { trackSpeed(p); PROGRESS[p.id] = p;
    if (p.phase === "done" || p.phase === "error") { delete PROGRESS[p.id];
      if (p.phase === "error") toast("ما تم التثبيت: " + (p.error || "خطأ")); else { toast("تم التثبيت ✓ أيقونتها على سطح المكتب"); addDone(p.id); }
      refreshInstalled(); if (curView === "downloads") renderDownloads(); }
    updateProgressUI(); });
  PS.onInstalledChanged && PS.onInstalledChanged(() => refreshInstalled());
  // give the app each game's cover for its "Starting game" window
  if (PS.setCover) setTimeout(() => { for (const g of GAMES) { try { const box = document.createElement("div"); box.style.cssText = "position:fixed;left:-9999px;top:0;width:300px;height:450px";
    document.body.appendChild(box); const o = {c:document.createElement("canvas"), id:g.id}; box.appendChild(o.c); paint(o, 0); PS.setCover(g.id, o.c.toDataURL("image/png")).catch(() => {}); box.remove(); } catch(e) {} } }, 1500);
  // very old app versions only update when fully closed: tell them once how
  fetch("https://api.github.com/repos/premiumstudiogames/premiumstudiogames.github.io/releases/latest").then(r => r.json()).then(j => {
    const lv = String(j.tag_name || "").replace(/^v/, "").split(".").map(n => n.padStart(2, "0")).join(".");
    if (lv && PS.version && PS.version < lv && !PS.updatesItself) { const b = document.createElement("div"); b.className = "updbar";
      b.innerHTML = `نسخة جديدة من Premium Studio (${lv}) جاهزة. اضغط بالزر اليمين على أيقونة PS جنب الساعة ← «خروج»، وافتحه، ويتحدّث. بعدها التحديثات تنزل لحالها. <a href="downloads/PremiumStudioSetup.exe" download>أو حمّلها من هنا</a>`;
      document.body.prepend(b); } }).catch(() => {});
  if (PS.onRunning) { PS.onRunning(r => { RUNNING = r || {}; render(); }); PS.getRunning().then(r => { RUNNING = r || {}; render(); }).catch(() => {}); }
  refreshInstalled();
}
/* download speed (like Steam's network graph) and the list of finished downloads */
const SPEED = {}; let PEAK = 0, SETS = null;
function trackSpeed(p){
  const now = performance.now(), s = SPEED[p.id] || (SPEED[p.id] = {t:now, r:p.received || 0, v:0});
  if (p.phase !== "download") { s.v = 0; return; }
  const dt = (now - s.t) / 1000; if (dt >= 0.5) { const v = Math.max(0, ((p.received || 0) - s.r) / dt); s.v = s.v ? s.v * 0.6 + v * 0.4 : v; s.t = now; s.r = p.received || 0; PEAK = Math.max(PEAK, s.v); }
}
const totalSpeed = () => Object.keys(PROGRESS).reduce((a, k) => a + ((SPEED[k] && SPEED[k].v) || 0), 0);
const fmtSpeed = b => b >= 1048576 ? (b / 1048576).toFixed(1) + " MB/s" : Math.round(b / 1024) + " KB/s";
const DONE_KEY = "ps-downloads-done";
const loadDone = () => { try { return JSON.parse(localStorage.getItem(DONE_KEY) || "[]"); } catch(e) { return []; } };
function addDone(key){ const l = loadDone().filter(d => d.key !== key); l.unshift({key, at:Date.now(), size:(SPEED[key] && SPEED[key].r) || 0}); try { localStorage.setItem(DONE_KEY, JSON.stringify(l.slice(0, 20))); } catch(e) {} delete SPEED[key]; }
function clearDone(){ try { localStorage.removeItem(DONE_KEY); } catch(e) {} renderDownloads(); updateProgressUI(); }
const keyTitle = key => { const v = INSTALLED[key], [gid, ed] = String(key).split("--"), g = byId(v ? v.game : gid), e = v ? v.edition : ed; return g ? (g.en || g.title) + (ED_TAG[e] ? " " + ED_TAG[e] : "") : key; };
// updates waiting for an installed game (newer version on the server)
const pendingUpdates = () => Object.entries(INSTALLED).filter(([k, v]) => { const L = LATEST[v.game], m = MANIFEST.games[v.game]; return m && m.type !== "bf-launcher" && L && L.version && v.version !== L.version && !PROGRESS[k]; });
function updateProgressUI(){
  renderDlBar();
  const active = Object.values(PROGRESS);
  $("#dlChip").hidden = !active.length;
  if (active.length) { const p = active[0]; $("#dlChipT").textContent = (p.phase === "extract" ? "جاري التثبيت" : "جاري التحميل") + " " + pct(p) + "%"; }
  for (const p of active) { $$(`[data-prog="${p.id}"]`).forEach(el => { el.querySelector("i").style.width = pct(p) + "%"; const t = el.parentElement.querySelector("[data-progt]"); if (t) t.textContent = progText(p); }); }
  if (curView === "downloads") renderDownloads(true);
  if (curView === "library") renderLibSide();
}
// the bar at the bottom of the app (Steam's "Downloads - 2 of 2 items complete")
function renderDlBar(){
  const bar = $("#dlBar"); if (!bar) return;
  if (!PS || !user) { bar.hidden = true; document.body.classList.remove("hasbar"); return; }
  bar.hidden = false; document.body.classList.add("hasbar");
  const act = Object.values(PROGRESS), done = loadDone().length, sp = totalSpeed();
  const mid = act.length
    ? `<span class="spin"></span><b>${esc(keyTitle(act[0].id))}</b><span class="num">${pct(act[0])}%</span><span class="dlbar-p"><i style="width:${pct(act[0])}%"></i></span>${sp ? `<span class="num muted">${fmtSpeed(sp)}</span>` : ""}${act.length > 1 ? `<span class="muted">+${act.length - 1}</span>` : ""}`
    : `<span class="muted">التحميلات · ${done ? `${done} من ${done} مكتملة` : "ما فيه تحميلات"}</span>`;
  bar.innerHTML = `<a class="dlbar-l" href="#store">＋ المتجر</a><a class="dlbar-m" href="#downloads">${mid}</a><a class="dlbar-r" href="#friends">الأصدقاء والدردشة</a>`;
}
function renderDownloads(light){
  const box = $("#downloads"); if (!box) return;
  if (!PS) { box.innerHTML = '<div class="empty"><b style="color:var(--text)">التحميلات تشتغل في تطبيق Premium Studio</b><a class="btn buy" href="#download">حمّل التطبيق</a></div>'; return; }
  const act = Object.values(PROGRESS), sp = totalSpeed();
  if (light && box.querySelector(".dl-net")) {   // just refresh numbers while downloading
    box.querySelector("[data-net]").textContent = fmtSpeed(sp); box.querySelector("[data-peak]").textContent = fmtSpeed(PEAK);
    for (const p of act) { const r = box.querySelector(`[data-dl="${p.id}"]`); if (!r) return renderDownloads(); r.querySelector(".bar i").style.width = pct(p) + "%"; r.querySelector("[data-progt]").textContent = progText(p);
      const s = SPEED[p.id] && SPEED[p.id].v, eta = s && p.total ? Math.max(0, (p.total - p.received) / s) : 0; r.querySelector("[data-eta]").textContent = s ? `${fmtSpeed(s)}${eta ? " · باقي " + (eta > 90 ? Math.round(eta / 60) + " د" : Math.round(eta) + " ث") : ""}` : (p.phase === "extract" ? "يثبّت…" : "…"); }
    if (box.querySelectorAll("[data-dl]").length !== act.length) renderDownloads();
    return;
  }
  const done = loadDone(), ups = pendingUpdates();
  const art = id => `<span class="dl-art" data-cover="${id}"></span>`;
  if (PS.getSettings && !SETS) PS.getSettings().then(x => { SETS = x || {}; renderDownloads(); }).catch(() => {});
  box.innerHTML = `<div class="dl-net"><div><small>الشبكة</small><b class="num" data-net>${fmtSpeed(sp)}</b></div><div><small>الأعلى</small><b class="num" data-peak>${fmtSpeed(PEAK)}</b></div><div><small>التحديث التلقائي</small><b>شغال</b></div><label class="dl-lim"><small>حد السرعة</small><select id="dlLim">${[[0,"بدون حد"],[524288,"0.5 MB/s"],[1048576,"1 MB/s"],[2097152,"2 MB/s"],[5242880,"5 MB/s"],[10485760,"10 MB/s"],[26214400,"25 MB/s"]].map(([v, n]) => `<option value="${v}"${(+(SETS && SETS.limit) || 0) === v ? " selected" : ""}>${n}</option>`).join("")}</select></label></div>
    <div class="dl-sec"><h3>قيد التحميل <span class="num muted">(${act.length})</span></h3>${act.length ? act.map(p => { const v = String(p.id).split("--")[0];
      return `<div class="dl-row" data-dl="${p.id}">${art(v)}<div class="dl-info"><b>${esc(keyTitle(p.id))}</b><span class="num muted" data-progt>${progText(p)}</span><div class="bar"><i style="width:${pct(p)}%"></i></div></div><span class="num muted" data-eta>…</span></div>`; }).join("") : '<p class="muted">ما فيه تحميلات الحين.</p>'}</div>
    ${ups.length ? `<div class="dl-sec"><h3>تحديثات متوفرة <span class="num muted">(${ups.length})</span></h3>${ups.map(([k, v]) => `<div class="dl-row">${art(v.game)}<div class="dl-info"><b>${esc(keyTitle(k))}</b><span class="muted num">${esc(v.version)} ← ${esc(LATEST[v.game].version)}</span></div><span class="muted" style="font-size:12px">يتحدّث تلقائيًا لما تشغّلها</span><button class="btn inst mini" data-act="install" data-g="${k}" data-gid="${v.game}" data-ed="${v.edition}">حدّث الحين</button></div>`).join("")}</div>` : ""}
    <div class="dl-sec"><h3>مكتملة <span class="num muted">(${done.length})</span>${done.length ? '<button class="btn ghost mini" id="dlClear" type="button">مسح الكل</button>' : ""}</h3>${done.length ? done.map(d => { const inst = INSTALLED[d.key], gid = inst ? inst.game : String(d.key).split("--")[0];
      return `<div class="dl-row">${art(gid)}<div class="dl-info"><b>${esc(keyTitle(d.key))}</b><span class="muted num" style="font-size:12px">${inst ? mb(inst.size || 0) + " · " : ""}اكتمل ${new Date(d.at).toLocaleString("ar-SA-u-nu-latn", {weekday:"short", hour:"numeric", minute:"2-digit"})}</span></div>${inst ? (RUNNING[d.key] ? '<span class="st run">● شغالة</span>' : `<button class="btn play" data-act="launch" data-g="${d.key}" data-gid="${gid}" data-ed="${inst.edition}">▶ لعب</button>`) : '<span class="muted">انحذفت</span>'}</div>`; }).join("") : '<p class="muted">ما فيه شي.</p>'}</div>`;
  box.querySelectorAll("[data-cover]").forEach(el => cover(el, el.dataset.cover));
  const c = $("#dlClear"); if (c) c.onclick = clearDone;
  const lim = $("#dlLim"); if (lim) lim.onchange = async () => { try { SETS = await PS.setSettings({limit:+lim.value}); toast("انحفظ حد السرعة"); } catch(e) {} };
}
const pct = p => p.total ? Math.min(100, Math.round(p.received / p.total * 100)) : (p.phase === "extract" ? 100 : 0);
const progText = p => p.phase === "extract" ? "جاري فك الملفات…" : `${mb(p.received||0)} / ${mb(p.total||0)}`;

/* the install / play block, Steam style. Every owned edition installs and plays as its own game. */
const ED_TAG = {demo:"Demo", std:"Standard", gold:"Gold", dev:"المبرمج"};
const sharedItems = () => Object.entries(SHARED).flatMap(([owner, o]) => Object.values(o.lib || {}).map(v => ({...v, owner, ownerName:o.name})));
const ownedEds = gid => [...new Set(Object.values(LIB).filter(v => v.game === gid && v.status !== "pending").map(v => v.edition)
  .concat(sharedItems().filter(v => v.game === gid && v.status !== "pending").map(v => v.edition)))].sort((a, b) => RANK[a] - RANK[b]);
const sharedFrom = gid => { const own = Object.values(LIB).some(v => v.game === gid && v.status !== "pending"); if (own) return ""; const x = sharedItems().find(v => v.game === gid && v.status !== "pending"); return x ? x.ownerName : ""; };
const instOf = (gid, ed) => { for (const [k, v] of Object.entries(INSTALLED)) if (v.game === gid && v.edition === ed) return [k, v]; return [`${gid}--${ed}`, null]; };
const anyInstalled = gid => Object.values(INSTALLED).some(v => v.game === gid);
const keyGame = k => byId(INSTALLED[k] ? INSTALLED[k].game : String(k).split("--")[0]);
function fmtPlay(gid){
  const p = PLAY[gid]; if (!p || !p.minutes) return "";
  const h = p.minutes / 60, t = h >= 1 ? `${h.toFixed(h < 10 ? 1 : 0)} ساعة` : `${p.minutes} دقيقة`;
  let last = ""; if (p.last && p.last.toDate) { const d = p.last.toDate(), days = Math.floor((Date.now() - d) / 864e5); last = days <= 0 ? "اليوم" : days === 1 ? "أمس" : d.toLocaleDateString("ar-SA-u-nu-latn", {day:"numeric", month:"short"}); }
  return `<div class="muted playtime" style="font-size:12px">وقت اللعب: <b class="num">${t}</b>${last ? ` · آخر تشغيل: ${last}` : ""}</div>`;
}
function installBlock(g){
  const eds = ownedEds(g.id);
  if (!eds.length) return "";
  if (!PS && isMobile) return `<div class="inststate"><span>اللعبة للكمبيوتر. افتح الموقع من الكمبيوتر عشان تثبّتها.</span></div>`;
  return `<div class="edinsts">${fmtPlay(g.id)}${eds.map(ed => edInstall(g, ed)).join("")}</div>`;
}
function edInstall(g, ed){
  const m = MANIFEST.games[g.id], [key, inst] = instOf(g.id, ed), prog = PROGRESS[key];
  const a = `data-g="${key}" data-gid="${g.id}" data-ed="${ed}"`;
  const head = `<div class="edinst-h"><b>${esc(g.en || g.title)}${ED_TAG[ed] ? ` <span class="tag">${ED_TAG[ed]}</span>` : ""}</b>`;
  if (!PS) return `<div class="edinst">${head}</div><button class="btn inst" data-act="getapp" ${a}>تثبيت</button></div>`;
  if (!PS.multiEdition) return `<div class="edinst">${head}</div><span class="muted" style="font-size:13px">حدّث تطبيق Premium Studio: اقفله وافتحه.</span></div>`;
  if (prog) return `<div class="edinst">${head}<span class="num" data-progt>${progText(prog)}</span></div><div class="bar" data-prog="${key}"><i style="width:${pct(prog)}%"></i></div></div>`;
  if (inst && RUNNING[key]) {
    const mins = Math.max(0, Math.floor((Date.now() - RUNNING[key].since) / 60000));
    const cl = RUNNING[key].closing;
    return `<div class="edinst running">${head}<span class="st run">● ${cl ? "يتقفل…" : "شغالة"}</span><span class="num muted" style="font-size:12px">${mins ? mins + " د" : "الحين"}</span></div>
      <div class="row"><button class="btn danger" data-act="stop" ${a} ${cl ? "disabled" : ""}>${cl ? "جاري الإغلاق…" : "إغلاق"}</button></div></div>`;
  }
  if (inst) {
    const L = LATEST[g.id], bf = m && m.type === "bf-launcher";
    const upd = !bf && L && L.version && inst.version !== L.version;
    const verTxt = bf ? (L && L.game ? esc(L.game) : "") : `${esc(inst.version)} · ${mb(inst.size||0)}`;
    return `<div class="edinst">${head}<span class="st inst">مثبتة</span><span class="num muted" style="font-size:12px">${verTxt}</span></div>
      <div class="row">${upd ? `<button class="btn inst" data-act="install" ${a}>تحديث إلى ${esc(L.version)}</button>` : `<button class="btn play" data-act="launch" ${a}>لعب</button>`}
      <button class="btn ghost mini" data-act="folder" ${a}>المجلد</button><button class="btn danger mini" data-act="uninstall" ${a}>حذف</button></div></div>`;
  }
  return `<div class="edinst">${head}<span class="muted" style="font-size:12px">غير مثبتة${m && m.installSize ? ` · <span class="num">~${mb(m.installSize)}</span>` : ""}</span></div><button class="btn inst" data-act="install" ${a}>تثبيت</button></div>`;
}
document.addEventListener("click", async e => {
  const b = e.target.closest("[data-act]"); if (!b) return;
  const key = b.dataset.g, gid = b.dataset.gid, ed = b.dataset.ed, g = byId(gid), act = b.dataset.act;
  if (act === "getapp") return getAppModal(g, ed, key);
  if (!PS) return;
  const owns = ownedEds(gid).includes(ed);
  if (act === "install") { if (!owns) return; b.disabled = true;
    const m = MANIFEST.games[gid] || {}, opts = {edition:ed};
    if (m.type === "bf-launcher") opts.code = await bfCode();   // only needed if the launcher download is ever locked again
    PROGRESS[key] = {id:key, phase:"download", received:0, total:(LATEST[gid] && LATEST[gid].size) || m.installSize || 0}; render(); updateProgressUI();
    try { await PS.install(key, opts); } catch(err) { delete PROGRESS[key]; toast("ما تم التثبيت: " + (err && err.message || err)); render(); } }
  else if (act === "launch") { if (!owns) return toast("ما تملك هذا الإصدار"); b.disabled = true; const t = b.textContent; b.textContent = "جاري التشغيل…";
    try { await PS.launch(key, ed); } catch(err) { toast("ما اشتغلت اللعبة: " + (err && err.message || err)); }
    b.disabled = false; b.textContent = t; }
  else if (act === "folder") { PS.showFolder(key); }
  else if (act === "stop") { b.disabled = true; b.textContent = "جاري الإغلاق…"; try { await PS.stop(key); } catch(err) { toast("ما انقفلت: " + (err && err.message || err)); b.disabled = false; b.textContent = "إغلاق"; } }
  else if (act === "uninstall") { const nm = (g.en || g.title) + (ED_TAG[ed] ? " " + ED_TAG[ed] : "");
    confirmModal(`حذف ${nm}؟`, "تنحذف اللعبة وأيقونتها من سطح المكتب. تقدر تثبّتها مرة ثانية من مكتبتك أي وقت.", "حذف", async () => { try { await PS.uninstall(key); toast("انحذفت " + nm); } catch(err) { toast("ما انحذفت: " + (err && err.message || err)); } refreshInstalled(); }); }
});
function getAppModal(g, ed, key){
  openModal(`<div class="eyebrow">INSTALL</div><h1 class="mbox-t">ثبّت ${esc(g.en || g.title)}${ED_TAG[ed] ? " " + ED_TAG[ed] : ""}</h1>
    <p class="mbox-t">الألعاب تنثبت من تطبيق Premium Studio على الكمبيوتر، مثل ستيم. التطبيق يحط أيقونة اللعبة على سطح المكتب.</p>
    <a class="btn buy" href="${APP_DL}" download>تحميل تطبيق Premium Studio</a>
    <a class="btn" href="premiumstudio://${key ? "install/" + key : "game/" + g.id}">عندي التطبيق، ثبّتها فيه</a>
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
      <div class="ft">${best ? `<span class="own">في مكتبتك · ${esc(edName(g, best.edition))}${anyInstalled(g.id) ? " · مثبتة" : ""}</span>` : '<span class="tag">ديمو مجاني</span>'}<span class="price" style="font-size:16px">${money(lowPrice(g))}</span></div></div>`;
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
    <dl class="specs"><dt>المطوّر</dt><dd>Premium Studio</dd><dt>المنصة</dt><dd>Windows 10 / 11</dd><dt>النوع</dt><dd>${g.tags.map(esc).join(" · ")}</dd>${LATEST[g.id] ? `<dt>آخر إصدار</dt><dd class="num">${esc(LATEST[g.id].game || LATEST[g.id].version)}</dd>` : ""}</dl>
  </div>
  <aside class="eds">
    ${best ? `<div class="ownbox"><div class="row" style="justify-content:space-between"><b>في مكتبتك</b>${stPill(best.status)}</div><div class="muted" style="font-size:13px">${esc(edName(g, best.edition))} · ${fdate(best.at)}</div>${installBlock(g)}</div>` : ""}
    ${pending ? `<div class="ownbox pend"><div class="row" style="justify-content:space-between"><b>${esc(edName(g, pending.edition))}</b>${stPill("pending")}</div><div class="muted" style="font-size:13px">طلبك وصل للاستوديو. تقدر تلعب بعد تأكيد الدفع.</div></div>` : ""}
    ${g.editions.map(e => { const has = (LIB[`${g.id}__${e.id}`] && LIB[`${g.id}__${e.id}`].status !== "pending") || (e.price > 0 && RANK[e.id] <= mineRank), isPend = pending && pending.edition === e.id;
      return `<div class="ed ${e.gold ? "gold" : ""}${e.studio ? " studio" : ""}"><div class="h"><span class="n">${esc(e.name)}${e.egg ? '<span class="egg" aria-hidden="true"></span>' : ""}</span><span class="price" style="font-size:${e.studio ? 13 : 18}px">${e.studio ? "من الاستوديو" : money(e.price)}</span></div><div class="d">${esc(e.d)}</div>
      ${e.price > 0 && user ? `<button class="btn mini ghost giftbtn" type="button" data-gift-ed="${e.id}">🎁 أهدِها</button>` : ""}
      ${has ? '<button class="btn" disabled>عندك هذا الإصدار</button>' : e.studio ? '<button class="btn" disabled>يعطيها الاستوديو للمطورين</button>' : isPend ? '<button class="btn" disabled>بانتظار الدفع</button>' : `<button class="btn ${e.price ? "buy" : ""}" data-ed="${e.id}">${e.price ? (best ? "ترقية إلى هذا الإصدار" : "شراء") : "أضف الديمو للمكتبة"}</button>`}</div>`; }).join("")}
  </aside></div>`;
  cover(v.querySelector(".banner"), g.id);
  v.querySelectorAll("[data-ed]:not([data-act])").forEach(b => b.onclick = () => buy(g, g.editions.find(e => e.id === b.dataset.ed)));
  gameExtras(g, v, best);
  v.querySelectorAll("[data-gift-ed]").forEach(b => b.onclick = () => giftModal(g, g.editions.find(e => e.id === b.dataset.giftEd)));
}
/* ---------------- library, Steam style: game list on the side, shelves and a game page on the right ---------------- */
let LIBSEL = "", LIBQ = "", LIBF = "", LIBSORT = "az";
function libGames(all){
  return GAMES.filter(g => (ent(g.id).best || ent(g.id).pending || sharedFrom(g.id)) && (all || LIBF === "hidden" || !(PREFS.hidden || []).includes(g.id)));
}
const lastPlayed = gid => PLAY[gid] && PLAY[gid].last ? tsMs(PLAY[gid].last) : 0;
const runningGame = gid => Object.entries(RUNNING).some(([k, r]) => (r.game || k.split("--")[0]) === gid);
const progGame = gid => Object.values(PROGRESS).find(p => String(p.id).split("--")[0] === gid);
function renderLibSide(){
  const side = $("#libSide"); if (!side) return;
  const q = LIBQ.trim().toLowerCase();
  const list = libGames().filter(g => (!q || (g.title + " " + (g.en || "")).toLowerCase().includes(q))
    && (!LIBF || (LIBF === "inst" ? anyInstalled(g.id) : LIBF === "fav" ? (PREFS.fav || []).includes(g.id) : LIBF === "hidden" ? (PREFS.hidden || []).includes(g.id) : !anyInstalled(g.id)))).sort((a, b) => (a.en || a.title).localeCompare(b.en || b.title));
  side.querySelector(".ls-list").innerHTML = list.length ? list.map(g => { const pr = progGame(g.id), run = runningGame(g.id), inst = anyInstalled(g.id);
    const sh = sharedFrom(g.id);
    const tag = run ? '<small class="ls-run">● شغالة</small>' : pr ? `<small class="ls-dl num">يحمّل ${pct(pr)}%</small>` : ent(g.id).best ? "" : sh ? `<small class="muted">من @${esc(sh)}</small>` : '<small class="muted">بانتظار الدفع</small>';
    return `<button class="ls-item${LIBSEL === g.id ? " on" : ""}${inst ? " inst" : ""}" data-sel="${g.id}" type="button"><span class="ls-ic" data-cover="${g.id}"></span><span class="ls-n">${(PREFS.fav || []).includes(g.id) ? "★ " : ""}${esc(g.en || g.title)}</span>${tag}</button>`; }).join("") : '<p class="muted" style="padding:10px">ما فيه نتائج</p>';
  side.querySelectorAll("[data-cover]").forEach(el => cover(el, el.dataset.cover));
  side.querySelectorAll("[data-sel]").forEach(b => b.onclick = () => { LIBSEL = b.dataset.sel; renderLib(); });
}
function libTile(g, big){
  const mins = (PLAY[g.id] && PLAY[g.id].minutes) || 0, run = runningGame(g.id);
  return `<button class="lt${big ? " big" : ""}${run ? " running" : ""}" data-sel="${g.id}" type="button"><span class="lt-art" data-cover="${g.id}"></span>
    ${big ? `<span class="lt-info"><span class="lt-play">${run ? "■" : "▶"}</span><span><b>${run ? "شغالة الحين" : "وقت اللعب"}</b><small class="num">${mins >= 60 ? (mins / 60).toFixed(1) + " ساعة" : mins + " دقيقة"}</small></span></span>` : ""}
    <span class="lt-t">${esc(g.en || g.title)}</span>${anyInstalled(g.id) ? "" : '<span class="lt-badge">غير مثبتة</span>'}</button>`;
}
function renderLibHome(main){
  const mine = libGames();
  if (!mine.length) { main.innerHTML = '<div class="empty"><b style="color:var(--text)">مكتبتك فاضية</b><span>أي لعبة تشتريها أو تضيف ديمو حقها تطلع هنا.</span><a class="btn buy" href="#store">تصفّح المتجر</a></div>'; return; }
  const recent = mine.filter(g => lastPlayed(g.id)).sort((a, b) => lastPlayed(b.id) - lastPlayed(a.id));
  const sorters = {az:(a, b) => (a.en || a.title).localeCompare(b.en || b.title), recent:(a, b) => lastPlayed(b.id) - lastPlayed(a.id), time:(a, b) => ((PLAY[b.id] || {}).minutes || 0) - ((PLAY[a.id] || {}).minutes || 0)};
  const all = mine.slice().sort(sorters[LIBSORT] || sorters.az);
  main.innerHTML = `${recent.length ? `<div class="shelf"><div class="shelf-h"><h3>آخر الألعاب</h3></div><div class="shelf-row">${recent.map((g, i) => libTile(g, i === 0)).join("")}</div></div>` : ""}
    <div class="shelf"><div class="shelf-h"><h3>كل الألعاب <span class="num muted">(${all.length})</span></h3><label class="muted" style="font-size:12px">ترتيب
      <select id="libSort"><option value="az">أبجدي</option><option value="recent">آخر تشغيل</option><option value="time">وقت اللعب</option></select></label></div>
      <div class="shelf-grid">${all.map(g => libTile(g)).join("")}</div></div>`;
  $("#libSort").value = LIBSORT; $("#libSort").onchange = e => { LIBSORT = e.target.value; renderLib(); };
}
function renderLibGame(main, g){
  const sh = sharedFrom(g.id), shv = sh && sharedItems().find(v => v.game === g.id);
  const {best, pending} = ent(g.id), o = best || pending || shv, mins = (PLAY[g.id] && PLAY[g.id].minutes) || 0, last = lastPlayed(g.id);
  const ach = Object.values(ACH).filter(a => a.game === g.id).length, achAll = Object.keys((MANIFEST.games[g.id] || {}).achievements || {}).length;
  const days = last ? Math.floor((Date.now() - last) / 864e5) : -1;
  main.innerHTML = `<div class="lg-hero"><span class="lg-art" data-cover="${g.id}"></span><div class="lg-title"><div class="row" style="gap:6px"><button class="btn ghost mini" id="libBack" type="button">→ المكتبة</button>
      <button class="btn ghost mini" id="libFav" type="button">${(PREFS.fav || []).includes(g.id) ? "★ مفضلة" : "☆ أضف للمفضلة"}</button><button class="btn ghost mini" id="libHide" type="button">${(PREFS.hidden || []).includes(g.id) ? "إظهار" : "إخفاء"}</button><button class="btn ghost mini" id="libProps" type="button">⚙ خصائص</button></div><h1>${esc(g.title)}</h1></div></div>
    <div class="lg-bar">${best || sh ? installBlock(g) : '<span class="muted">تقدر تلعب بعد تأكيد الدفع</span>'}
      <div class="lg-stats"><div><small>آخر تشغيل</small><b>${days < 0 ? "—" : days === 0 ? "اليوم" : days === 1 ? "أمس" : new Date(last).toLocaleDateString("ar-SA-u-nu-latn", {day:"numeric", month:"short"})}</b></div>
        <div><small>وقت اللعب</small><b class="num">${mins >= 60 ? (mins / 60).toFixed(1) + " ساعة" : mins + " دقيقة"}</b></div>
        ${achAll ? `<div><small>الإنجازات</small><b class="num">${ach}/${achAll}</b><span class="lg-ach"><i style="width:${Math.round(ach / achAll * 100)}%"></i></span></div>` : ""}</div></div>
    <div class="lg-body"><div class="panel"><h3>نسختك</h3><div class="row" style="gap:8px"><span>${esc(edName(g, o.edition))}</span>${sh ? `<span class="tag">مشاركة من @${esc(sh)}</span>` : stPill(o.status)}${o.from ? `<span class="tag">🎁 هدية من @${esc(o.from)}</span>` : ""}</div><span class="muted" style="font-size:12px">أضيفت ${fdate(o.at)}${pending && best ? " · ترقية بانتظار الدفع" : ""}</span>${refundLine(g)}<a class="linkbtn" href="#${g.id}">صفحة المتجر</a></div>
      ${achAll ? `<div class="panel"><h3>الإنجازات</h3><div class="lg-achs">${Object.entries(MANIFEST.games[g.id].achievements).map(([id, d]) => { const got = ACH[g.id + "__" + id];
        return `<div class="badge${got ? " got" : ""}" title="${esc(d.desc || "")}"><span>🏆</span><b>${esc(d.name)}</b><small>${esc(d.desc || "")}</small></div>`; }).join("")}</div></div>` : ""}</div>`;
  $("#libBack").onclick = () => { LIBSEL = ""; renderLib(); };
  $("#libFav").onclick = () => setPref("fav", g.id, !(PREFS.fav || []).includes(g.id));
  $("#libHide").onclick = () => { const h = (PREFS.hidden || []).includes(g.id); setPref("hidden", g.id, !h); if (!h) { LIBSEL = ""; toast("انخفت من مكتبتك. تلقاها في «المخفية»"); } };
  $("#libProps").onclick = () => gameProps(g);
  if (PS && PS.screenshots) {   // screenshots taken with F12 while playing
    const box = document.createElement("div"); box.className = "panel lg-shots"; box.innerHTML = `<h3>لقطات الشاشة والمقاطع <span><button class="linkbtn" type="button" data-shots>اللقطات</button> · <button class="linkbtn" type="button" data-clips>المقاطع</button></span></h3><div class="shots"><span class="muted">اضغط F12 وانت تلعب عشان تصوّر، و F9 عشان تسجّل مقطع.</span></div>`;
    main.querySelector(".lg-body").appendChild(box);
    box.querySelector("[data-shots]").onclick = () => PS.openShots("");
    box.querySelector("[data-clips]").onclick = () => PS.openClips && PS.openClips();
    PS.screenshots(g.id).then(l => { if (!l.length) return; box.querySelector(".shots").innerHTML = l.map((x, i) => `<button class="shot" type="button" data-i="${i}" title="${esc(x.name)}"><img src="${x.thumb}" alt=""></button>`).join("");
      box.querySelectorAll(".shot").forEach(b => b.onclick = () => PS.openShots(l[+b.dataset.i].file)); }).catch(() => {});
  }
}
function renderLib(){
  const box = $("#lib"), bar = $("#libBar"); bar.innerHTML = "";
  if (!user) { box.innerHTML = '<div class="empty"><b style="color:var(--text)">المكتبة للمسجّلين فقط</b><span>سجّل دخولك عشان تضيف ألعاب لمكتبتك وتثبّتها.</span><button class="btn buy" type="button" data-login>تسجيل الدخول</button></div>'; return; }
  if (!PS && !isMobile) bar.innerHTML = `<span>عشان تثبّت ألعابك على الكمبيوتر تحتاج تطبيق Premium Studio.</span><a class="btn mini buy" href="#download">حمّل التطبيق</a>`;
  if (!box.querySelector("#libSide")) {
    box.innerHTML = `<div class="libwrap"><aside class="libside" id="libSide"><button class="ls-home" id="libHome" type="button">الرئيسية</button>
      <div class="ls-tools"><input id="libQ" type="search" placeholder="ابحث في مكتبتك" autocomplete="off"><select id="libF" aria-label="فلتر"><option value="">الكل</option><option value="fav">المفضلة ★</option><option value="inst">مثبتة</option><option value="no">غير مثبتة</option><option value="hidden">المخفية</option></select></div>
      <div class="ls-list"></div></aside><section class="libmain" id="libMain"></section></div>`;
    $("#libQ").oninput = e => { LIBQ = e.target.value; renderLibSide(); };
    $("#libF").onchange = e => { LIBF = e.target.value; renderLibSide(); };
    $("#libHome").onclick = () => { LIBSEL = ""; renderLib(); };
  }
  $("#libHome").classList.toggle("on", !LIBSEL);
  renderLibSide();
  const main = $("#libMain"), g = LIBSEL && byId(LIBSEL);
  if (g && (ent(g.id).best || ent(g.id).pending || sharedFrom(g.id))) renderLibGame(main, g); else { LIBSEL = ""; renderLibHome(main); }
  main.querySelectorAll("[data-cover]").forEach(el => cover(el, el.dataset.cover));
  main.querySelectorAll(".lt[data-sel]").forEach(b => b.onclick = () => { LIBSEL = b.dataset.sel; renderLib(); main.scrollIntoView({block:"start"}); });
}

/* ---------------- profile: level, XP, badges, game achievements ----------------
   XP: 10 per hour played, 100 per game owned, +50 per Gold edition, 25 per friend. Level n needs 25·n² XP. */
const BADGES = [
  {id:"founder", icon:"★", name:"مؤسس", desc:"من أوائل اللاعبين في 2026", ok:s => s.joinedYear && s.joinedYear <= 2026},
  {id:"verified", icon:"✓", name:"موثّق", desc:"أكدت إيميلك", ok:s => s.verified},
  {id:"first", icon:"🎮", name:"أول لعبة", desc:"عندك لعبة في مكتبتك", ok:s => s.games >= 1},
  {id:"collector", icon:"📚", name:"جامع", desc:"تملك كل ألعاب الاستوديو", ok:s => s.games >= GAMES.length && GAMES.length > 0},
  {id:"gold", icon:"👑", name:"ذهبي", desc:"تملك نسخة Gold", ok:s => s.gold >= 1},
  {id:"h1", icon:"⏱", name:"ساعة لعب", desc:"لعبت ساعة", ok:s => s.minutes >= 60},
  {id:"h10", icon:"🔥", name:"10 ساعات", desc:"لعبت 10 ساعات", ok:s => s.minutes >= 600},
  {id:"h100", icon:"💎", name:"100 ساعة", desc:"لعبت 100 ساعة", ok:s => s.minutes >= 6000},
  {id:"social", icon:"🤝", name:"اجتماعي", desc:"عندك صديق", ok:s => s.friends >= 1},
  {id:"popular", icon:"🌟", name:"مشهور", desc:"عندك 10 أصدقاء", ok:s => s.friends >= 10},
  {id:"coin", icon:"🪙", name:"صاحب عملات", desc:"عندك 1 PS COIN أو أكثر", ok:s => s.coins >= 10000}
];
function myStats(){
  const owned = GAMES.filter(g => ownedEds(g.id).length);
  return {minutes: Object.values(PLAY).reduce((a, p) => a + (p.minutes || 0), 0), games: owned.length,
    gold: Object.values(LIB).filter(v => v.edition === "gold" && v.status !== "pending").length,
    friends: Object.values(FRIENDS).filter(f => f.status === "ok").length, coins: (PROFILE && PROFILE.coins) || 0,
    verified: !!(user && user.emailVerified), joinedYear: PROFILE && PROFILE.joined && PROFILE.joined.toDate ? PROFILE.joined.toDate().getFullYear() : 0,
    ach: Object.keys(ACH).length};
}
const xpOf = s => Math.floor(s.minutes / 6) + 100 * s.games + 50 * s.gold + 25 * s.friends + 10 * s.ach;
const levelOf = xp => Math.floor(Math.sqrt(xp / 25));
function myProfile(){ const st = myStats(), xp = xpOf(st), lv = levelOf(xp); return {st, xp, lv, badges: BADGES.filter(b => b.ok(st)).map(b => b.id)}; }
// the public part (friends see your level and badges)
let lastPub = "";
function publishProfile(){
  if (!user || !PROFILE || !PROFILE.username) return;
  const p = myProfile(), pub = {username:PROFILE.username, level:p.lv, xp:p.xp, hours:Math.floor(p.st.minutes / 60), badges:p.badges};
  const k = JSON.stringify(pub); if (k === lastPub) return; lastPub = k;
  setDoc(doc(fs, "profiles", user.uid), {...pub, at:serverTimestamp()}).catch(() => { lastPub = ""; });
}
function renderProfile(){
  const box = $("#prof"); if (!box) return;
  if (!user) { box.innerHTML = ""; return; }
  const {st, xp, lv, badges} = myProfile(), cur = 25 * lv * lv, next = 25 * (lv + 1) * (lv + 1), pc = Math.round((xp - cur) / (next - cur) * 100);
  const un = (PROFILE && PROFILE.username) || "", hrs = st.minutes / 60;
  const achList = Object.values(ACH).sort((a, b) => tsMs(b.at) - tsMs(a.at));
  box.innerHTML = `<div class="prof-h"><span class="prof-av">${esc((un || (PROFILE && PROFILE.name) || "?").charAt(0).toUpperCase())}</span>
      <div class="prof-who"><b class="ltr">@${esc(un || "—")}</b><span class="muted">${esc((PROFILE && PROFILE.name) || "")}</span>
        <div class="xpbar" title="${xp} XP"><i style="width:${pc}%"></i></div><small class="muted num">${xp} / ${next} XP للمستوى ${lv + 1}</small></div>
      <div class="lvl"><small>المستوى</small><b class="num">${lv}</b></div></div>
    <div class="prof-stats"><div><b class="num">${hrs >= 10 ? Math.round(hrs) : hrs.toFixed(1)}</b><span>ساعة لعب</span></div><div><b class="num">${st.games}</b><span>لعبة</span></div><div><b class="num">${st.friends}</b><span>صديق</span></div><div><b class="num">${achList.length}</b><span>إنجاز</span></div></div>
    <h3 class="prof-sub">الشارات <span class="muted num">${badges.length}/${BADGES.length}</span></h3>
    <div class="badges">${BADGES.map(b => `<div class="badge${badges.includes(b.id) ? " got" : ""}" title="${esc(b.desc)}"><span>${b.icon}</span><b>${esc(b.name)}</b><small>${esc(b.desc)}</small></div>`).join("")}</div>
    ${achList.length ? `<h3 class="prof-sub">إنجازات الألعاب</h3><div class="badges">${achList.map(a => { const g = byId(a.game), def = (MANIFEST.games[a.game] && MANIFEST.games[a.game].achievements || {})[a.id] || {};
      return `<div class="badge got"><span>🏆</span><b>${esc(def.name || a.id)}</b><small>${esc(g ? (g.en || g.title) : a.game)}${def.desc ? " · " + esc(def.desc) : ""}</small></div>`; }).join("")}</div>` : ""}`;
}

/* ---------------- wishlist, reviews, news (store game page) ---------------- */
async function toggleWish(gid){
  if (!user) return showAuth("login");
  try { if (WISH[gid]) await deleteDoc(doc(fs, "users", user.uid, "wishlist", gid)); else { await setDoc(doc(fs, "users", user.uid, "wishlist", gid), {at:serverTimestamp()}); toast("انضافت لقائمة أمنياتك ♥"); } }
  catch(e) { toast("ما تم: " + (e.code || e.message)); }
}
const revUnsubs = {};
function watchReviews(gid){ if (revUnsubs[gid]) return; revUnsubs[gid] = onSnapshot(collection(fs, "reviews", gid, "r"), s => { REVIEWS[gid] = s.docs.map(d => ({id:d.id, ...d.data()})); if (curView === "game" && location.hash.slice(1) === gid) renderGame(byId(gid)); }, () => {}); }
async function loadNews(g){
  if (NEWS[g.id]) return; NEWS[g.id] = [];
  const m = MANIFEST.games[g.id]; if (!m || !m.gameManifest) return;
  try { const j = await (await fetch(m.gameManifest + "?t=" + Date.now())).json(); NEWS[g.id] = (j.versions || []).slice(0, 6).map(v => ({id:v.id, title:v.title || "", notes:v.notes || [], at:v.releaseTime || v.time}));
    if (curView === "game" && location.hash.slice(1) === g.id) renderGame(g); } catch(e) {}
}
function gameExtras(g, v, best){
  watchReviews(g.id); loadNews(g);
  const aside = v.querySelector(".eds");
  if (aside && !best) { const w = document.createElement("button"); w.type = "button"; w.className = "btn wish" + (WISH[g.id] ? " on" : ""); w.textContent = WISH[g.id] ? "♥ في قائمة أمنياتك" : "♡ أضف لقائمة الأمنيات"; w.onclick = () => toggleWish(g.id); aside.appendChild(w); }
  const rs = REVIEWS[g.id] || [], up = rs.filter(r => r.up).length, pct = rs.length ? Math.round(up / rs.length * 100) : 0;
  const label = !rs.length ? "ما فيه تقييمات للحين" : pct >= 80 ? "إيجابية جدًا" : pct >= 70 ? "إيجابية في الغالب" : pct >= 40 ? "مختلطة" : "سلبية في الغالب";
  const mine = user && rs.find(r => r.id === user.uid);
  const sec = document.createElement("section"); sec.className = "gx";
  sec.innerHTML = `${(NEWS[g.id] || []).length ? `<div class="gx-news"><div class="sec-h"><h2>ما الجديد</h2><small>NEWS</small></div>${NEWS[g.id].map(n => `<article><b>${esc(n.title || "تحديث")}</b> <span class="num muted">${esc(n.id)}</span><small class="muted">${n.at ? new Date(n.at).toLocaleDateString("ar-SA-u-nu-latn", {day:"numeric", month:"short", year:"numeric"}) : ""}</small>${n.notes.length ? `<ul>${n.notes.slice(0, 4).map(x => `<li>${esc(x)}</li>`).join("")}</ul>` : ""}</article>`).join("")}</div>` : ""}
    <div class="gx-rev"><div class="sec-h"><h2>تقييمات اللاعبين</h2><small>REVIEWS</small></div>
      <div class="rev-sum"><b class="${pct >= 70 ? "pos" : pct >= 40 ? "mix" : rs.length ? "neg" : ""}">${label}</b>${rs.length ? `<span class="muted num">${pct}% من ${rs.length} تقييم</span>` : ""}</div>
      ${best ? `<form class="rev-form" id="revF"><div class="row" style="gap:8px"><b>${mine ? "تقييمك" : "قيّم اللعبة"}</b><button type="button" class="btn mini${mine && mine.up ? " buy" : ""}" data-up="1">👍 أنصح فيها</button><button type="button" class="btn mini${mine && !mine.up ? " danger" : ""}" data-up="0">👎 ما أنصح</button></div>
        <textarea id="revT" maxlength="800" placeholder="اكتب رأيك (اختياري)">${esc(mine ? mine.text || "" : "")}</textarea><div class="err" id="revE"></div></form>` : `<p class="muted" style="font-size:13px">يقدر يقيّم اللي عنده اللعبة.</p>`}
      <div class="rev-list">${rs.slice().sort((a, b) => tsMs(b.at) - tsMs(a.at)).slice(0, 20).map(r => `<div class="rev"><div class="rev-h"><span class="${r.up ? "pos" : "neg"}">${r.up ? "👍 ينصح فيها" : "👎 ما ينصح"}</span><b class="ltr">@${esc(r.username || "")}</b><small class="muted num">${r.hours || 0} ساعة لعب</small></div>${r.text ? `<p>${esc(r.text)}</p>` : ""}</div>`).join("")}</div></div>`;
  v.appendChild(sec);
  const f = $("#revF");
  if (f) f.querySelectorAll("[data-up]").forEach(b => b.onclick = async () => {
    if (!PROFILE || !PROFILE.username) return toast("اختر اسم مستخدم أول");
    const mins = (PLAY[g.id] && PLAY[g.id].minutes) || 0;
    try { await setDoc(doc(fs, "reviews", g.id, "r", user.uid), {username:PROFILE.username, up:b.dataset.up === "1", text:$("#revT").value.trim().slice(0, 800), hours:Math.floor(mins / 60), at:serverTimestamp()}); toast("انحفظ تقييمك ✓"); }
    catch(e) { $("#revE").textContent = "ما انحفظ: " + (e.code || e.message); } });
}

/* ---------------- game properties (like Steam's) ---------------- */
function gameProps(g){
  const eds = ownedEds(g.id).map(ed => [ed, ...instOf(g.id, ed)]).filter(x => x[2]);
  openModal(`<div class="eyebrow">PROPERTIES</div><h1 class="mbox-t">خصائص ${esc(g.en || g.title)}</h1>
    ${!PS ? '<p class="muted">الخصائص في تطبيق Premium Studio.</p>' : !eds.length ? '<p class="muted">ما فيه نسخة مثبتة.</p>' : eds.map(([ed, key, inst]) => `<div class="prop"><b>${esc(edName(g, ed))}</b><span class="muted num" style="font-size:12px">${esc(inst.version || "")} · ${mb(inst.size || 0)}</span>
      <div class="row" style="gap:6px;flex-wrap:wrap"><button class="btn mini" data-pv="${key}">تحقق من سلامة الملفات</button><button class="btn mini" data-ps="${key}">أيقونة على سطح المكتب</button><button class="btn mini" data-act="folder" data-g="${key}" data-gid="${g.id}" data-ed="${ed}">المجلد</button><button class="btn mini danger" data-act="uninstall" data-g="${key}" data-gid="${g.id}" data-ed="${ed}">حذف</button></div></div>`).join("")}
    <div class="row" style="justify-content:flex-end;margin-top:14px"><button class="btn" type="button" data-close>إغلاق</button></div>`);
  const box = $("#mbox");
  box.querySelectorAll("[data-pv]").forEach(b => b.onclick = async () => { b.disabled = true; b.textContent = "يتحقق ويصلّح…"; try { await PS.verify(b.dataset.pv); b.textContent = "✓ الملفات سليمة"; } catch(e) { b.textContent = "ما تم: " + (e && e.message || e); b.disabled = false; } });
  box.querySelectorAll("[data-ps]").forEach(b => b.onclick = async () => { try { await PS.shortcut(b.dataset.ps); b.textContent = "✓ انحطت"; } catch(e) { toast(String(e && e.message || e)); } });
  box.querySelector("[data-close]").onclick = closeModal;
}

/* ---------------- favorites and hidden games (library), kept on the account ---------------- */
async function setPref(kind, gid, on){
  if (!user) return; const l = new Set(PREFS[kind] || []); on ? l.add(gid) : l.delete(gid);
  PREFS = {...PREFS, [kind]:[...l]}; render();
  try { await setDoc(doc(fs, "users", user.uid, "meta", "prefs"), {fav:PREFS.fav || [], hidden:PREFS.hidden || []}); } catch(e) {}
}

/* ---------------- your status (online / away / invisible), like Steam ---------------- */
const STATUS_KEY = "ps-status";
const myStatus = () => { try { return localStorage.getItem(STATUS_KEY) || "online"; } catch(e) { return "online"; } };
function beat(){ if (!user) return; setDoc(doc(fs, "presence", user.uid), {state:myStatus(), at:serverTimestamp()}, {merge:true}).catch(() => {}); }
setInterval(() => { if (user && !document.hidden) beat(); else if (user && myStatus() === "online") beat(); }, 3 * 60000);
function setStatus(st){ try { localStorage.setItem(STATUS_KEY, st); } catch(e) {} beat(); renderAccount(); toast(st === "invisible" ? "صرت تبان غير متصل" : st === "away" ? "حالتك: بعيد" : "حالتك: متصل"); }

/* ---------------- notifications (the bell) ---------------- */
function notes(){
  const out = [];
  for (const [id, f] of Object.entries(FRIENDS)) if (f.status === "in") out.push({t:`@${f.username} يبي يضيفك صديق`, href:"#friends", i:"👤"});
  for (const id of Object.keys(LASTMSG)) if (unread(id)) { const m = LASTMSG[id], f = FRIENDS[id]; out.push({t:m.kind === "invite" ? `@${f ? f.username : ""} يدعوك تلعب ${gameName(m.game)}` : `رسالة من @${f ? f.username : ""}`, href:"#friends", i:m.kind === "invite" ? "🎮" : "💬"}); }
  for (const a of Object.values(ACH)) if (Date.now() - tsMs(a.at) < 864e5) { const d = ((MANIFEST.games[a.game] || {}).achievements || {})[a.id]; out.push({t:`إنجاز جديد: ${d ? d.name : a.id}`, href:"#account", i:"🏆"}); }
  for (const [k, v] of pendingUpdates()) out.push({t:`تحديث متوفر: ${keyTitle(k)}`, href:"#downloads", i:"⬇"});
  for (const [id, d] of Object.entries(DEVICES)) if (id !== deviceId() && Date.now() - tsMs(d.first) < 864e5) out.push({t:`دخول من جهاز جديد: ${d.name || ""}`, href:"#account", i:"🔐"});
  for (const [id, r] of Object.entries(REFUNDS)) if (r.status !== "pending" && Date.now() - tsMs(r.doneAt || r.at) < 7 * 864e5) out.push({t:r.status === "done" ? "تمت الموافقة على الاسترجاع" : "انرفض طلب الاسترجاع", href:"#library", i:"↩"});
  for (const gid of Object.keys(WISH)) { const g = byId(gid); if (g && ent(gid).best) continue; }
  return out;
}
document.addEventListener("click", e => { const l = $("#bellList"); if (!l) return;
  if (e.target.closest("#bellBtn")) { l.hidden = !l.hidden; renderBell(); } else if (!e.target.closest("#bellList")) l.hidden = true; else if (e.target.closest("a")) l.hidden = true; });
function renderBell(){
  const b = $("#bell"); if (!b) return; b.hidden = !user;
  const n = notes(); b.querySelector(".nb").textContent = n.length; b.querySelector(".nb").hidden = !n.length;
  const l = $("#bellList"); if (l && !l.hidden) l.innerHTML = n.length ? n.map(x => `<a href="${x.href}"><span>${x.i}</span>${esc(x.t)}</a>`).join("") : '<p class="muted">ما فيه إشعارات</p>';
}

/* ---------------- voice chat, parties and Remote Play Together ---------------- */
let RTC = null;
function rtc(){
  if (RTC) return RTC;
  RTC = makeRTC({fs, toast, esc, PS, me:() => user, myName:() => (PROFILE && PROFILE.username) || "",
    F:{doc, collection, query, where, onSnapshot, setDoc, getDoc, updateDoc, addDoc, serverTimestamp, arrayUnion, arrayRemove},
    gameStream:async key => { try { await PS.shareGame(key); return await navigator.mediaDevices.getDisplayMedia({video:{frameRate:30}, audio:false}); } catch(e) { toast("شغّل اللعبة من بريميوم أول: " + (e && e.message || e)); return null; } }});
  RTC.onChange = renderCall; return RTC;
}
const callName = (c, uid) => (c.names && c.names[uid]) || (FRIENDS[uid] && FRIENDS[uid].username) || "";
function renderCall(){
  const bar = $("#callBar"), ring = $("#ringBox"); if (!bar || !ring) return;
  const R = RTC, c = R && R.cur;
  // ringing / invited
  const inc = R ? Object.entries(R.incoming) : [];
  ring.hidden = !inc.length || !!(c && inc.every(([id]) => id === c.id));
  ring.innerHTML = inc.map(([id, x]) => `<div class="ring"><b>${x.kind === "party" ? "🎉 دعوة لحفلة صوتية" : x.kind === "remote" ? "🎮 دعوة تلعب عن بعد" : "📞 مكالمة صوتية"}</b><span>من @${esc(callName(x, x.host))}${x.game ? " · " + esc(gameName(x.game)) : ""}</span>
    <div class="row" style="gap:6px"><button class="btn mini buy" type="button" data-rj="${id}">${x.kind === "voice" ? "رد" : "انضم"}</button><button class="btn mini danger" type="button" data-rd="${id}">رفض</button></div></div>`).join("");
  ring.querySelectorAll("[data-rj]").forEach(b => b.onclick = () => rtc().join(b.dataset.rj).catch(e => toast(String(e && e.message || e))));
  ring.querySelectorAll("[data-rd]").forEach(b => b.onclick = () => rtc().decline(b.dataset.rd));
  // in a call
  bar.hidden = !c;
  if (!c) { const v = $("#remoteView"); if (v) v.remove(); return; }
  const others = (c.data.in || []).filter(x => x !== user.uid);
  bar.innerHTML = `<b>${c.data.kind === "party" ? "🎉 حفلة صوتية" : c.data.kind === "remote" ? "🎮 تلعب عن بعد" : "📞 مكالمة"}</b>
    <span class="muted">${others.length ? others.map(x => "@" + esc(callName(c.data, x))).join("، ") : "بانتظار الباقين…"}</span>
    ${c.local && c.data.kind !== "remote" ? `<button class="btn mini" type="button" id="cMute">${c.muted ? "🔇 مكتوم" : "🎙 كتم"}</button>` : ""}<button class="btn mini danger" type="button" id="cLeave">إنهاء</button>`;
  const m = $("#cMute"); if (m) m.onclick = () => rtc().mute();
  $("#cLeave").onclick = () => rtc().leave();
  // Remote Play viewer: the host's game, and your keys/mouse go back to it
  if (c.data.kind === "remote" && !c.isHost) {
    let v = $("#remoteView");
    if (!v) { v = document.createElement("div"); v.id = "remoteView"; v.innerHTML = `<video autoplay playsinline></video><div class="rv-bar"><span>تلعب على جهاز @${esc(callName(c.data, c.data.host))} · اضغط على الشاشة وابدأ</span><button class="btn mini danger" type="button">خروج</button></div>`;
      document.body.appendChild(v); v.querySelector("button").onclick = () => rtc().leave();
      const vid = v.querySelector("video"), pos = e => { const r = vid.getBoundingClientRect(); return {x:(e.clientX - r.left) / r.width, y:(e.clientY - r.top) / r.height}; };
      vid.tabIndex = 0;
      vid.onmousedown = e => { vid.focus(); rtc().input({type:"mouseDown", button:e.button, ...pos(e)}); };
      vid.onmouseup = e => rtc().input({type:"mouseUp", button:e.button, ...pos(e)});
      let mv = 0; vid.onmousemove = e => { if (performance.now() - mv < 30) return; mv = performance.now(); rtc().input({type:"mouseMove", ...pos(e)}); };
      vid.onwheel = e => { e.preventDefault(); rtc().input({type:"mouseWheel", dx:e.deltaX, dy:e.deltaY, ...pos(e)}); };
      vid.oncontextmenu = e => e.preventDefault();
      vid.onkeydown = e => { e.preventDefault(); rtc().input({type:"keyDown", key:e.key.length === 1 ? e.key : e.key}); if (e.key.length === 1) rtc().input({type:"char", key:e.key}); };
      vid.onkeyup = e => { e.preventDefault(); rtc().input({type:"keyUp", key:e.key}); }; }
    const vid = v.querySelector("video"); if (c.remoteVideo && vid.srcObject !== c.remoteVideo) vid.srcObject = c.remoteVideo;
  }
}
function partyModal(){
  const fr = Object.entries(FRIENDS).filter(([, f]) => f.status === "ok"); if (!fr.length) return toast("أضف أصدقاء أول");
  openModal(`<div class="eyebrow">PARTY</div><h1 class="mbox-t">🎉 حفلة صوتية</h1><p class="muted">اختر لين 5 أصدقاء. يتكلمون معك وانتم تلعبون.</p>
    <div class="giftlist">${fr.map(([id, f]) => `<label><input type="checkbox" value="${id}"> @${esc(f.username)}${isOnline(id) ? ' <small class="plg">متصل</small>' : ""}</label>`).join("")}</div>
    <div class="row" style="justify-content:flex-end;gap:8px;margin-top:12px"><button class="btn" type="button" data-close>إلغاء</button><button class="btn buy" type="button" id="pGo">ابدأ الحفلة</button></div>`);
  $("#mbox [data-close]").onclick = closeModal;
  $("#pGo").onclick = () => { const ids = [...document.querySelectorAll("#mbox input:checked")].map(x => x.value).slice(0, 5); if (!ids.length) return toast("اختر صديق");
    const names = {}; ids.forEach(id => names[id] = FRIENDS[id].username); closeModal(); rtc().start("party", ids, names).catch(() => {}); };
}
// Remote Play Together: share a game that runs inside Premium Studio
function remoteModal(fid){
  if (!PS || !PS.shareGame) return toast("العب عن بعد من تطبيق Premium Studio");
  const run = Object.entries(RUNNING).filter(([k]) => (MANIFEST.games[(INSTALLED[k] || {}).game] || {}).type === "manifest");
  if (!run.length) return toast("شغّل «التسجيل الأخير» أول، وبعدين ادعُ صديقك يلعب معك");
  const [key] = run[0]; rtc().start("remote", [fid], {[fid]:FRIENDS[fid].username}, {key, game:(INSTALLED[key] || {}).game}).catch(() => {});
}

/* ---------------- account security: your devices, new-device alerts, sign out everywhere ---------------- */
let DEVICES = {}, SECURITY = {};
const DEV_KEY = "ps-device", LOGIN_KEY = "ps-login-at";
const deviceId = () => { try { let d = localStorage.getItem(DEV_KEY); if (!d) { d = Math.random().toString(36).slice(2) + Date.now().toString(36); localStorage.setItem(DEV_KEY, d); } return d; } catch(e) { return "web"; } };
function deviceName(){ const ua = navigator.userAgent; const os = /Windows/.test(ua) ? "Windows" : /Android/.test(ua) ? "Android" : /iPhone|iPad/.test(ua) ? "iPhone/iPad" : /Mac/.test(ua) ? "Mac" : "جهاز"; return (PS ? "تطبيق Premium Studio · " : "المتصفح · ") + os; }
function registerDevice(){
  if (!user) return; const id = deviceId();
  try { if (!localStorage.getItem(LOGIN_KEY + user.uid)) localStorage.setItem(LOGIN_KEY + user.uid, String(Date.now())); } catch(e) {}
  setDoc(doc(fs, "users", user.uid, "devices", id), {name:deviceName(), last:serverTimestamp(), first:(DEVICES[id] && DEVICES[id].first) || serverTimestamp()}, {merge:true}).catch(() => {});
}
function checkSignOutAll(){
  if (!user || !SECURITY.signOutBefore) return; let at = 0; try { at = +localStorage.getItem(LOGIN_KEY + user.uid) || 0; } catch(e) {}
  if (at && at < tsMs(SECURITY.signOutBefore)) { try { localStorage.removeItem(LOGIN_KEY + user.uid); } catch(e) {} toast("طلعت من كل الأجهزة. سجّل دخولك من جديد"); signOut(auth); }
}
function securityBox(){
  const me = deviceId(), list = Object.entries(DEVICES).sort((a, b) => tsMs(b[1].last) - tsMs(a[1].last));
  return `<h3 style="margin-top:16px">الأمان</h3><p class="muted" style="font-size:13px;margin:0 0 8px">الأجهزة اللي دخلت منها. إذا شفت جهاز ما تعرفه، اطلع من كل الأجهزة وغيّر كلمة السر.</p>
    <div class="devs">${list.map(([id, d]) => `<div class="dev"><b>${esc(d.name || "جهاز")}${id === me ? ' <span class="tag">هذا الجهاز</span>' : ""}</b><small class="muted">آخر دخول ${fdate(d.last)}</small>${id !== me ? `<button class="linkbtn" type="button" data-devx="${id}">إزالة</button>` : ""}</div>`).join("")}</div>
    <button class="btn mini danger" type="button" id="signAll" style="margin-top:8px">اطلع من كل الأجهزة</button>`;
}
async function signOutEverywhere(){
  confirmModal("تطلع من كل الأجهزة؟", "كل جهاز داخل بحسابك يطلع، وتحتاج تسجّل دخولك فيها من جديد (حتى هذا الجهاز).", "اطلع من الكل", async () => {
    try { await setDoc(doc(fs, "users", user.uid, "meta", "security"), {signOutBefore:serverTimestamp()}); } catch(e) { toast("ما تم: " + e.code); } });
}
// parental controls (in the app): PIN, allowed games, minutes per day
let PARENTAL = null;
function parentalBox(){
  if (!PS || !PS.setParental) return "";
  const p = PARENTAL || {on:false, hasPin:false, allowed:[], daily:0, used:0};
  return `<h3 style="margin-top:16px">الرقابة الأبوية</h3><p class="muted" style="font-size:13px;margin:0 0 8px">على هذا الكمبيوتر: اختر الألعاب المسموحة ووقت اللعب باليوم. ${p.on ? `<b class="plg">شغالة</b> · لعب اليوم <span class="num">${p.used}</span> دقيقة` : ""}</p>
    <div class="pc"><label>الرقم السري<input id="pcPin" type="password" inputmode="numeric" maxlength="8" placeholder="${p.hasPin ? "اكتب الرقم السري" : "اختر رقم من 4 لـ 8 أرقام"}" autocomplete="off"></label>
      <label>دقائق اللعب باليوم<input id="pcDaily" type="number" min="0" max="1440" value="${p.daily || 0}"><small class="muted">0 = بدون حد</small></label>
      <div><span class="muted" style="font-size:13px">الألعاب المسموحة (إذا ما اخترت شي، كلها مسموحة)</span><div class="row" style="gap:8px;flex-wrap:wrap">${GAMES.map(g => `<label class="tag"><input type="checkbox" data-pcg="${g.id}"${p.allowed.includes(g.id) ? " checked" : ""}> ${esc(g.en || g.title)}</label>`).join("")}</div></div>
      <div class="row" style="gap:8px"><button class="btn mini buy" type="button" id="pcSave">${p.on ? "احفظ" : "شغّل الرقابة"}</button>${p.on ? '<button class="btn mini" type="button" id="pcOff">وقّفها</button>' : ""}</div><div class="err" id="pcErr"></div></div>`;
}
async function parentalSave(on){
  const pin = $("#pcPin").value.trim(), p = PARENTAL || {};
  const o = {pin, daily:+$("#pcDaily").value || 0, allowed:[...document.querySelectorAll("[data-pcg]:checked")].map(x => x.dataset.pcg), on};
  if (!p.hasPin) o.newPin = pin;
  try { PARENTAL = await PS.setParental(o); document.body.classList.toggle("kidmode", !!PARENTAL.on); toast(PARENTAL.on ? "الرقابة الأبوية شغالة ✓" : "انوقفت الرقابة الأبوية"); renderAccount(); }
  catch(e) { $("#pcErr").textContent = String(e && e.message || e).replace(/^Error invoking remote method '[^']+': (Error: )?/, ""); }
}

/* ---------------- Workshop: worlds made by players ---------------- */
let WORKSHOP = [], wsUnsub = null, MYWORLDS = null, WSUBS = {};
function watchWorkshop(){ if (wsUnsub || !user) return; wsUnsub = onSnapshot(collection(fs, "workshop"), s => { WORKSHOP = s.docs.map(d => ({id:d.id, ...d.data()})).sort((a, b) => (b.subs || 0) - (a.subs || 0) || tsMs(b.at) - tsMs(a.at)); if (curView === "workshop") renderWorkshop(); }, () => {}); unsubs.push(() => { if (wsUnsub) wsUnsub(); wsUnsub = null; }); }
function renderWorkshop(){
  const box = $("#workshop"); if (!box) return;
  if (!user) { box.innerHTML = '<div class="empty"><b style="color:var(--text)">الورشة للمسجّلين</b><button class="btn buy" type="button" data-login>تسجيل الدخول</button></div>'; return; }
  watchWorkshop();
  const canPlay = ownedEds("blockfall").length > 0;
  if (PS && PS.myWorlds && MYWORLDS === null) { MYWORLDS = []; PS.myWorlds().then(l => { MYWORLDS = l || []; renderWorkshop(); }).catch(() => {}); }
  box.innerHTML = `<p class="muted">عوالم BLOCKFALL 3001 سواها اللاعبين. اضغط «اشترك» وتلقاه في عوالمك أول ما تشغّل اللعبة.</p>
    ${PS && PS.workshopUpload ? `<div class="panel ws-up"><h3>ارفع عالم من عوالمك</h3>${MYWORLDS && MYWORLDS.length ? `<div class="row" style="gap:8px;flex-wrap:wrap"><select id="wsW">${MYWORLDS.map(w => `<option value="${esc(w.id)}">${esc(w.name)} (${mb(w.size)})</option>`).join("")}</select><input id="wsT" maxlength="60" placeholder="اسم العالم في الورشة"><input id="wsD" maxlength="600" placeholder="وصف (اختياري)"><button class="btn buy" type="button" id="wsGo">ارفع</button></div><div class="err" id="wsE"></div>` : '<span class="muted">العب BLOCKFALL مرة من بريميوم عشان تطلع عوالمك هنا.</span>'}</div>` : ""}
    <div class="ws-grid">${WORKSHOP.length ? WORKSHOP.map((w, i) => `<div class="ws"><span class="ws-art" data-cover="blockfall"></span><div class="ws-b"><b>${esc(w.title)}</b><small class="muted">@${esc(w.authorName || "")} · ${mb(w.size || 0)} · <span class="num">${w.subs || 0}</span> مشترك</small>${w.desc ? `<p>${esc(w.desc)}</p>` : ""}
      <div class="row" style="gap:6px">${!PS ? '<span class="muted" style="font-size:12px">الاشتراك من التطبيق</span>' : !canPlay ? '<span class="muted" style="font-size:12px">تحتاج BLOCKFALL 3001</span>' : WSUBS[w.id] ? '<span class="st inst">✓ مشترك · يدخل عوالمك</span>' : `<button class="btn mini buy" type="button" data-wss="${i}">اشترك</button>`}${w.author === user.uid ? `<button class="btn mini danger" type="button" data-wsx="${i}">احذف</button>` : ""}</div></div></div>`).join("") : '<p class="muted">ما فيه عوالم للحين. كن أول واحد يرفع.</p>'}</div>`;
  box.querySelectorAll("[data-cover]").forEach(el => cover(el, el.dataset.cover));
  const go = $("#wsGo"); if (go) go.onclick = async () => { go.disabled = true; go.textContent = "يرفع…"; const sel = $("#wsW");
    try { await PS.workshopUpload(sel.value, $("#wsT").value.trim() || sel.options[sel.selectedIndex].text.replace(/ \(.*$/, ""), $("#wsD").value.trim()); toast("ارتفع عالمك للورشة ✓"); }
    catch(e) { $("#wsE").textContent = String(e && e.message || e).replace(/^Error invoking remote method '[^']+': (Error: )?/, ""); } go.disabled = false; go.textContent = "ارفع"; };
  box.querySelectorAll("[data-wss]").forEach(b => b.onclick = async () => { const w = WORKSHOP[+b.dataset.wss]; b.disabled = true; b.textContent = "ينزّل…";
    try { await PS.workshopGet(w.id, w.title, w.mode); WSUBS[w.id] = 1; updateDoc(doc(fs, "workshop", w.id), {subs:increment(1)}).catch(() => {}); toast("✓ يدخل عوالمك أول ما تشغّل BLOCKFALL"); renderWorkshop(); }
    catch(e) { toast("ما تم: " + String(e && e.message || e).replace(/^Error invoking remote method '[^']+': (Error: )?/, "")); b.disabled = false; b.textContent = "اشترك"; } });
  box.querySelectorAll("[data-wsx]").forEach(b => b.onclick = () => deleteDoc(doc(fs, "workshop", WORKSHOP[+b.dataset.wsx].id)).catch(e => toast(e.code)));
}

/* ---------------- gifts: buy an edition for a friend ---------------- */
function giftModal(g, e){
  if (!user) return showAuth("login");
  const fr = Object.entries(FRIENDS).filter(([, f]) => f.status === "ok");
  if (!fr.length) return toast("أضف أصدقاء أول عشان تهديهم");
  openModal(`<div class="eyebrow">GIFT</div><h1 class="mbox-t">🎁 أهدِ ${esc(e.name)}</h1><p class="muted">اختر صديقك. بعد ما يأكد الاستوديو الدفع (${money(e.price)})، توصله اللعبة في مكتبته.</p>
    <div class="giftlist">${fr.map(([id, f]) => `<label><input type="radio" name="gto" value="${id}"> @${esc(f.username)}</label>`).join("")}</div>
    <div class="err" id="gErr"></div><div class="row" style="justify-content:flex-end;gap:8px;margin-top:12px"><button class="btn" type="button" data-close>إلغاء</button><button class="btn buy" type="button" id="gGo">أرسل الهدية</button></div>`);
  $("#mbox [data-close]").onclick = closeModal;
  $("#gGo").onclick = async () => { const to = (document.querySelector('input[name="gto"]:checked') || {}).value; if (!to) return $("#gErr").textContent = "اختر صديق";
    try { await addDoc(collection(fs, "users", user.uid, "gifts"), {to, toName:FRIENDS[to].username, game:g.id, edition:e.id, price:e.price, status:"pending", at:serverTimestamp()}); closeModal(); toast("انرسل طلب الهدية ✓ بانتظار تأكيد الدفع"); }
    catch(err) { $("#gErr").textContent = "ما انرسلت: " + (err.code || err.message); } };
}

/* ---------------- family sharing: let up to 5 friends play your games ---------------- */
function familyBox(){
  const fr = Object.entries(FRIENDS).filter(([, f]) => f.status === "ok"), mem = (FAMILY && FAMILY.members) || [];
  const shared = Object.entries(SHARED);
  return `<h3 style="margin-top:16px">مشاركة العائلة</h3><p class="muted" style="font-size:13px;margin:0 0 8px">اختر لين 5 أصدقاء يقدرون يلعبون ألعاب مكتبتك.</p>
    ${fr.length ? `<div class="row famrow" style="gap:8px;flex-wrap:wrap">${fr.map(([id, f]) => `<label class="tag"><input type="checkbox" data-fam="${id}"${mem.includes(id) ? " checked" : ""}> @${esc(f.username)}</label>`).join("")}</div>` : '<span class="muted">أضف أصدقاء أول.</span>'}
    ${shared.length ? `<p style="margin:10px 0 0">مكتبات مشاركة معك: ${shared.map(([, o]) => `<span class="tag">@${esc(o.name)}</span>`).join(" ")}</p>` : ""}`;
}
async function saveFamily(){
  const ids = [...document.querySelectorAll("[data-fam]:checked")].map(x => x.dataset.fam).slice(0, 5);
  if (document.querySelectorAll("[data-fam]:checked").length > 5) toast("الحد 5 أشخاص");
  const names = {}; ids.forEach(id => names[id] = FRIENDS[id].username);
  try { await setDoc(doc(fs, "families", user.uid), {members:ids, names}); toast("انحفظت مشاركة العائلة ✓"); } catch(e) { toast("ما انحفظت: " + (e.code || e.message)); }
}
const famUnsubs = {};
function watchShared(){
  if (!user) return;
  unsubs.push(onSnapshot(query(collection(fs, "families"), where("members", "array-contains", user.uid)), s => {
    const owners = s.docs.map(d => d.id);
    for (const o of Object.keys(famUnsubs)) if (!owners.includes(o)) { famUnsubs[o](); delete famUnsubs[o]; delete SHARED[o]; }
    for (const d of s.docs) { const o = d.id; if (famUnsubs[o]) continue;
      SHARED[o] = {name:"", lib:{}};
      famUnsubs[o] = onSnapshot(collection(fs, "users", o, "library"), ls => { SHARED[o] = {name:(PROFS[o] && PROFS[o].username) || (FRIENDS[o] && FRIENDS[o].username) || "صديق", lib:Object.fromEntries(ls.docs.map(x => [x.id, x.data()]))}; render(); sendOwned(); }, () => {}); }
    render(); }, () => {}));
}

/* ---------------- trading cards, badges and the market (PS COIN) ---------------- */
const CARDSET = {
  "blockfall": [["الناجي","🧍"],["الحفّار","⛏"],["البنّاء","🧱"],["السائق","🚙"],["الرادار","📡"]],
  "last-recording": [["الكاميرا","📹"],["الشريط","📼"],["الحارس","🔦"],["المول","🏬"],["الظل","👤"]]
};
const cardName = id => { const [g, i] = [id.replace(/_\d$/, ""), +id.slice(-1)]; const c = (CARDSET[g] || [])[i - 1]; return c ? c[0] : id; };
const cardIcon = id => { const g = id.replace(/_\d$/, ""), c = (CARDSET[g] || [])[+id.slice(-1) - 1]; return c ? c[1] : "🃏"; };
// a card drops for every 30 minutes played, up to 3 per game
let dropping = false;
async function maybeDrop(){
  if (!user || dropping) return;
  for (const g of GAMES) { const mins = (PLAY[g.id] && PLAY[g.id].minutes) || 0, n = (DROPS[g.id] && DROPS[g.id].n) || 0;
    if (n >= 3 || (n + 1) * 30 > mins || !CARDSET[g.id]) continue;
    dropping = true; const i = 1 + Math.floor(Math.random() * 5), cid = `${g.id}_${i}`;
    try { const b = writeBatch(fs); b.set(doc(fs, "users", user.uid, "cardDrops", g.id), {n:n + 1}); b.set(doc(fs, "users", user.uid, "cards", cid), {game:g.id, count:((CARDS[cid] && CARDS[cid].count) || 0) + 1, via:"drop"});
      await b.commit(); toast(`🃏 حصلت بطاقة: ${cardName(cid)} (${g.en || g.title})`); } catch(e) {}
    dropping = false; return; }
}
async function craftBadge(gid){
  const lv = (CBADGES[gid] && CBADGES[gid].level) || 0, b = writeBatch(fs);
  for (let i = 1; i <= 5; i++) { const cid = `${gid}_${i}`; b.update(doc(fs, "users", user.uid, "cards", cid), {count:CARDS[cid].count - 1, via:"craft"}); }
  b.set(doc(fs, "users", user.uid, "badges", gid), {level:lv + 1, at:serverTimestamp()});
  try { await b.commit(); toast(`🏅 صنعت شارة ${byId(gid).en || byId(gid).title} مستوى ${lv + 1}`); } catch(e) { toast("ما تم: " + (e.code || e.message)); }
}
function cardsBox(){
  return `<h3 style="margin-top:16px">بطاقاتي</h3><p class="muted" style="font-size:13px;margin:0 0 8px">كل نص ساعة لعب تعطيك بطاقة (لين 3 لكل لعبة). جمّع الخمس واصنع شارة، أو بِع وشترِ في <a href="#market">السوق</a>.</p>
    ${GAMES.filter(g => CARDSET[g.id]).map(g => { const have = CARDSET[g.id].map((c, i) => (CARDS[`${g.id}_${i + 1}`] || {}).count || 0), full = have.every(x => x > 0), lv = (CBADGES[g.id] && CBADGES[g.id].level) || 0;
      return `<div class="cset"><div class="row" style="justify-content:space-between"><b>${esc(g.en || g.title)}${lv ? ` <span class="tag">🏅 شارة مستوى ${lv}</span>` : ""}</b>${full && lv < 5 ? `<button class="btn mini buy" type="button" data-craft="${g.id}">اصنع الشارة</button>` : `<small class="muted num">${have.filter(x => x).length}/5</small>`}</div>
        <div class="cards">${CARDSET[g.id].map((c, i) => `<div class="tcard${have[i] ? " got" : ""}"><span>${c[1]}</span><b>${esc(c[0])}</b>${have[i] ? `<small class="num">×${have[i]}</small>` : ""}</div>`).join("")}</div></div>`; }).join("")}`;
}
async function listCard(cid){
  const v = prompt(`سعر «${cardName(cid)}» بـ PS COIN (مثلاً 0.0005)`); if (!v) return;
  const price = Math.round(parseFloat(v) * COIN_UNIT); if (!(price >= 1)) return toast("اكتب سعر صحيح (أقل شي 0.0001)");
  const ref = doc(collection(fs, "listings")), b = writeBatch(fs);
  b.set(ref, {seller:user.uid, sellerName:(PROFILE && PROFILE.username) || "", card:cid, game:cid.replace(/_\d$/, ""), price, status:"open", at:serverTimestamp()});
  b.update(doc(fs, "users", user.uid, "cards", cid), {count:CARDS[cid].count - 1, via:"list", lastTrade:ref.id});
  try { await b.commit(); toast("انعرضت في السوق ✓"); } catch(e) { toast("ما انعرضت: " + (e.code || e.message)); }
}
async function buyListing(l){
  if (!user) return showAuth("login");
  const mine = (PROFILE && PROFILE.coins) || 0; if (mine < l.price) return toast("رصيدك من PS COIN ما يكفي");
  const b = writeBatch(fs);
  b.update(doc(fs, "listings", l.id), {status:"sold", buyer:user.uid, soldAt:serverTimestamp()});
  b.update(doc(fs, "users", user.uid), {coins:mine - l.price, lastTrade:l.id});
  b.update(doc(fs, "users", l.seller), {coins:increment(l.price), lastSale:l.id});
  b.set(doc(fs, "users", user.uid, "cards", l.card), {game:l.game, count:((CARDS[l.card] && CARDS[l.card].count) || 0) + 1, via:"buy", lastTrade:l.id});
  try { await b.commit(); toast(`اشتريت «${cardName(l.card)}» ✓`); } catch(e) { toast("ما تم الشراء: " + (e.code || e.message)); }
}
async function cancelListing(l){
  const b = writeBatch(fs); b.update(doc(fs, "listings", l.id), {status:"cancelled"});
  b.set(doc(fs, "users", user.uid, "cards", l.card), {game:l.game, count:((CARDS[l.card] && CARDS[l.card].count) || 0) + 1, via:"cancel", lastTrade:l.id});
  try { await b.commit(); toast("رجعت البطاقة لك"); } catch(e) { toast("ما تم: " + (e.code || e.message)); }
}
function renderMarket(){
  const box = $("#market"); if (!box) return;
  if (!user) { box.innerHTML = '<div class="empty"><b style="color:var(--text)">السوق للمسجّلين</b><button class="btn buy" type="button" data-login>تسجيل الدخول</button></div>'; return; }
  const mineCards = Object.entries(CARDS).filter(([, c]) => c.count > 0);
  box.innerHTML = `<div class="mk-bal">رصيدك: <b class="num">${coins((PROFILE && PROFILE.coins) || 0)}</b> PS COIN</div>
    <div class="sec-h"><h2>معروض للبيع</h2><small>LISTINGS</small></div>
    <div class="mk-grid">${LISTINGS.length ? LISTINGS.map((l, i) => `<div class="tcard got big"><span>${cardIcon(l.card)}</span><b>${esc(cardName(l.card))}</b><small>${esc((byId(l.game) || {}).en || (byId(l.game) || {}).title || l.game)} · @${esc(l.sellerName || "")}</small><b class="num">${coins(l.price)} PS COIN</b>
      ${l.seller === user.uid ? `<button class="btn mini ghost" type="button" data-mkc="${i}">اسحبها</button>` : `<button class="btn mini buy" type="button" data-mkb="${i}">شراء</button>`}</div>`).join("") : '<p class="muted">ما فيه شي معروض الحين.</p>'}</div>
    <div class="sec-h"><h2>بطاقاتك</h2><small>YOUR CARDS</small></div>
    <div class="mk-grid">${mineCards.length ? mineCards.map(([cid, c]) => `<div class="tcard got"><span>${cardIcon(cid)}</span><b>${esc(cardName(cid))}</b><small class="num">×${c.count}</small><button class="btn mini" type="button" data-mks="${cid}">اعرضها للبيع</button></div>`).join("") : '<p class="muted">ما عندك بطاقات للحين. العب وتطيح لك.</p>'}</div>`;
  box.querySelectorAll("[data-mkb]").forEach(b => b.onclick = () => { b.disabled = true; buyListing(LISTINGS[+b.dataset.mkb]); });
  box.querySelectorAll("[data-mkc]").forEach(b => b.onclick = () => { b.disabled = true; cancelListing(LISTINGS[+b.dataset.mkc]); });
  box.querySelectorAll("[data-mks]").forEach(b => b.onclick = () => listCard(b.dataset.mks));
}

/* refunds: a paid edition can be returned within 14 days of purchase if played under 2 hours */
const REFUND_DAYS = 14, REFUND_MIN = 120;
function refundable(g){
  const mins = (PLAY[g.id] && PLAY[g.id].minutes) || 0;
  return Object.entries(LIB).filter(([, v]) => v.game === g.id && v.status === "paid" && v.paidAt && Date.now() - tsMs(v.paidAt) < REFUND_DAYS * 864e5 && mins < REFUND_MIN);
}
function refundLine(g){
  const req = Object.entries(REFUNDS).filter(([id]) => id.startsWith(g.id + "__"));
  const st = req.map(([id, r]) => { const ed = id.split("__")[1];
    return r.status === "pending" ? `<span class="st pending">طلب استرجاع ${esc(edName(g, ed))} قيد المراجعة</span>` : r.status === "rejected" ? `<span class="st">انرفض طلب الاسترجاع</span>` : ""; }).join("");
  const can = refundable(g).filter(([id]) => !REFUNDS[id]);
  return (st ? `<div class="row" style="gap:6px">${st}</div>` : "") + can.map(([id, v]) => `<button class="linkbtn" type="button" data-refund="${id}">استرجاع ${esc(edName(g, v.edition))} (${money(v.price)})</button>`).join("");
}
document.addEventListener("click", e => {
  const b = e.target.closest("[data-refund]"); if (!b || !user) return;
  const id = b.dataset.refund, v = LIB[id], g = v && byId(v.game); if (!v) return;
  const mins = (PLAY[v.game] && PLAY[v.game].minutes) || 0;
  confirmModal(`استرجاع ${g ? (g.en || g.title) : ""} ${g ? edName(g, v.edition) : ""}؟`, `لعبت ${mins} دقيقة. بعد موافقة الاستوديو ترجع لك ${money(v.price)} وتنشال النسخة من مكتبتك.`, "اطلب الاسترجاع", async () => {
    try { await setDoc(doc(fs, "users", user.uid, "refunds", id), {game:v.game, edition:v.edition, price:v.price, minutes:mins, status:"pending", at:serverTimestamp()}); toast("انرسل طلب الاسترجاع ✓"); }
    catch(err) { toast("ما انرسل: " + (err.code || err.message)); } });
});
function renderAccount(){
  renderProfile();
  const sb = $("#statusBox");
  if (sb && user) { const st = myStatus(); sb.innerHTML = `<h3>حالتك</h3><div class="row" style="gap:8px">${[["online","● متصل"],["away","◐ بعيد"],["invisible","○ أبان غير متصل"]].map(([k, n]) => `<button class="btn mini${st === k ? " buy" : ""}" type="button" data-st="${k}">${n}</button>`).join("")}</div>
    ${Object.keys(WISH).length ? `<h3 style="margin-top:16px">قائمة أمنياتك</h3><div class="row" style="gap:8px;flex-wrap:wrap">${Object.keys(WISH).map(id => { const g = byId(id); return g ? `<a class="btn mini" href="#${id}">♥ ${esc(g.en || g.title)}</a>` : ""; }).join("")}</div>` : ""}`;
    sb.insertAdjacentHTML("beforeend", familyBox() + cardsBox() + securityBox() + parentalBox());
    sb.querySelectorAll("[data-devx]").forEach(b => b.onclick = () => deleteDoc(doc(fs, "users", user.uid, "devices", b.dataset.devx)).catch(() => {}));
    const sa = $("#signAll"); if (sa) sa.onclick = signOutEverywhere;
    const ps = $("#pcSave"); if (ps) ps.onclick = () => parentalSave(true);
    const po = $("#pcOff"); if (po) po.onclick = () => parentalSave(false);
    sb.querySelectorAll("[data-fam]").forEach(c => c.onchange = saveFamily);
    sb.querySelectorAll("[data-craft]").forEach(b => b.onclick = () => { b.disabled = true; craftBadge(b.dataset.craft); });
    sb.querySelectorAll("[data-st]").forEach(b => b.onclick = () => setStatus(b.dataset.st)); }
  const a = $("#acct");
  if (!user) { a.innerHTML = '<div class="empty" style="grid-column:1/-1"><b style="color:var(--text)">ما سجّلت دخول</b><button class="btn buy" type="button" data-login>تسجيل الدخول</button></div>'; return; }
  a.innerHTML = `<div class="panel"><h3>معلوماتك</h3><dl class="kv"><dt>PS ID</dt><dd class="num ltr">${fmtPid(PROFILE && PROFILE.pid)}</dd><dt>PS COIN</dt><dd class="num ltr">${coins(PROFILE && PROFILE.coins)}</dd><dt>اسم المستخدم</dt><dd class="ltr" id="aUser"></dd><dt>الاسم</dt><dd id="aName"></dd><dt>الإيميل</dt><dd class="ltr" id="aMail"></dd><dt>التحقق</dt><dd>${user.emailVerified ? '<span class="st paid">الإيميل مؤكد</span>' : '<span class="st pending">غير مؤكد</span>'}</dd><dt>عضو من</dt><dd>${fdate(PROFILE && PROFILE.joined)}</dd></dl>
    ${user.emailVerified ? "" : '<div class="row"><button class="btn mini" id="aVerify" type="button">أرسل رابط التأكيد مرة ثانية</button><button class="btn mini ghost" id="aReload" type="button">أكدت، حدّث</button></div>'}</div>
    <div class="panel"><h3>الحساب</h3><p class="muted" style="font-size:14px">نفس الحساب يشتغل في الموقع وفي تطبيق Premium Studio.</p><div class="row"><button class="btn mini" id="aReset" type="button">تغيير كلمة السر</button><button class="btn mini danger" id="aOut" type="button">تسجيل الخروج</button></div></div>`;
  $("#aUser").textContent = PROFILE && PROFILE.username ? "@" + PROFILE.username : "—"; $("#aName").textContent = (PROFILE && PROFILE.name) || user.displayName || "—"; $("#aMail").textContent = user.email;
  $("#aOut").onclick = async () => { await signOut(auth); toast("سجّلت خروج"); location.hash = "#store"; };
  $("#aReset").onclick = async () => { try { await sendPasswordResetEmail(auth, user.email); toast("أرسلنا رابط تغيير كلمة السر لإيميلك"); } catch(e) { toast(authErr(e)); } };
  const v = $("#aVerify"); if (v) v.onclick = async () => { try { await sendEmailVerification(user); toast("أرسلنا رابط التأكيد لإيميلك"); } catch(e) { toast(authErr(e)); } };
  const rl = $("#aReload"); if (rl) rl.onclick = async () => { await user.reload(); await user.getIdToken(true); setAdmin(); render(); toast(user.emailVerified ? "تم تأكيد الإيميل ✓" : "لسا ما تأكد. افتح الرابط في إيميلك"); };
}

/* ---------------- studio dashboard (admin) ---------------- */
/* ---------------- friends ----------------
   users/<me>/friends/<them> = {username: their @name, status}. "out" = I asked, "in" = they asked, "ok" = friends.
   Both sides are written together, so each player sees the request. */
let FRIENDS = {};
const frRef = (a, b) => doc(fs, "users", a, "friends", b);
function frBadge(){ const n = Object.values(FRIENDS).filter(f => f.status === "in").length + (user ? Object.keys(LASTMSG).filter(unread).length : 0), b = $("#frBadge"); b.hidden = !n; b.textContent = n; }
async function addFriend(raw){
  const un = normUname(raw), me = PROFILE && PROFILE.username;
  if (!me) return "اختر اسم مستخدم لحسابك أول";
  if (!UNAME_RE.test(un)) return "اكتب اسم مستخدم صحيح";
  if (un === me) return "هذا اسمك أنت 🙂";
  let s; try { s = await getDoc(doc(fs, "usernames", un)); } catch(e) { return "ما قدرت أبحث: " + (e.code || e.message); }
  if (!s.exists()) return `ما فيه لاعب باسم @${un}`;
  const fid = s.data().uid, cur = FRIENDS[fid];
  if (cur && cur.status === "ok") return `@${un} صاحبك من قبل`;
  if (cur && cur.status === "out") return `أرسلت طلب لـ @${un} من قبل`;
  if (cur && cur.status === "in") { await acceptFriend(fid); return ""; }
  const b = writeBatch(fs);
  b.set(frRef(user.uid, fid), {username:un, status:"out", at:serverTimestamp()});
  b.set(frRef(fid, user.uid), {username:me, status:"in", at:serverTimestamp()});
  try { await b.commit(); toast(`أرسلت طلب صداقة لـ @${un}`); return ""; } catch(e) { return "ما انرسل الطلب: " + (e.code || e.message); }
}
async function acceptFriend(fid){
  const b = writeBatch(fs); b.update(frRef(user.uid, fid), {status:"ok"}); b.update(frRef(fid, user.uid), {status:"ok"});
  try { await b.commit(); toast(`صرت أنت و @${FRIENDS[fid] ? FRIENDS[fid].username : ""} أصدقاء ✓`); } catch(e) { toast("ما تم: " + (e.code || e.message)); }
}
async function dropFriend(fid){
  const b = writeBatch(fs); b.delete(frRef(user.uid, fid)); b.delete(frRef(fid, user.uid));
  try { await b.commit(); } catch(e) { toast("ما تم: " + (e.code || e.message)); }
}
const presUnsubs = {};
function watchPresence(){
  const ok = Object.keys(FRIENDS).filter(id => FRIENDS[id].status === "ok");
  for (const k of Object.keys(presUnsubs)) { const id = k.startsWith("prof:") ? k.slice(5) : k; if (!ok.includes(id)) { presUnsubs[k](); delete presUnsubs[k]; delete PRES[id]; delete PROFS[id]; } }
  for (const id of ok) if (!presUnsubs["prof:" + id]) presUnsubs["prof:" + id] = onSnapshot(doc(fs, "profiles", id), s => { PROFS[id] = s.exists() ? s.data() : null; if (curView === "friends") renderFriendList(); }, () => {});
  for (const id of ok) if (!presUnsubs[id]) presUnsubs[id] = onSnapshot(doc(fs, "presence", id), s => { PRES[id] = s.exists() ? s.data() : null; if (curView === "friends") renderFriendList(); }, () => {});
}
// online = the app or the store is open (it says so every few minutes), unless they chose to look offline
function isOnline(id){ const p = PRES[id]; return !!(p && p.at && p.at.toDate && Date.now() - p.at.toDate() < 6 * 60000 && p.state !== "invisible"); }
function playingNow(id){ const p = PRES[id]; return p && p.game && p.at && p.at.toDate && Date.now() - p.at.toDate() < 10 * 60000 ? p.game : ""; }
/* ---------------- chat with friends ----------------
   chats/<uidA_uidB>/msgs/<id> = {from, text, at, kind:"text"|"invite", game?}. Only friends can write. */
let CHAT_WITH = "", CHAT_MSGS = [], chatUnsub = null;
const LASTMSG = {}, lastUnsubs = {};
const cidOf = fid => [user.uid, fid].sort().join("_");
const readKey = fid => `ps-read-${user.uid}-${fid}`;
const readAt = fid => { try { return +localStorage.getItem(readKey(fid)) || 0; } catch(e) { return 0; } };
const msMs = t => t && t.toMillis ? t.toMillis() : Date.now();
function markRead(fid){ try { localStorage.setItem(readKey(fid), String(Date.now())); } catch(e) {} }
const unread = fid => { const m = LASTMSG[fid]; return !!(m && m.from !== user.uid && msMs(m.at) > readAt(fid)); };
function watchChats(){
  const ok = user ? Object.keys(FRIENDS).filter(id => FRIENDS[id].status === "ok") : [];
  for (const id of Object.keys(lastUnsubs)) if (!ok.includes(id)) { lastUnsubs[id](); delete lastUnsubs[id]; delete LASTMSG[id]; }
  for (const id of ok) if (!lastUnsubs[id]) {
    let first = true;
    lastUnsubs[id] = onSnapshot(query(collection(fs, "chats", cidOf(id), "msgs"), orderBy("at", "desc"), limit(1)), s => {
      const m = s.docs[0] ? s.docs[0].data() : null; LASTMSG[id] = m;
      if (m && !first && m.from !== user.uid && !(curView === "friends" && CHAT_WITH === id && !document.hidden)) notifyMsg(id, m);
      first = false; frBadge(); if (curView === "friends") renderFriendList();
    }, () => {});
  }
  if (CHAT_WITH && !ok.includes(CHAT_WITH)) openChat("");
}
function notifyMsg(fid, m){
  const who = "@" + (FRIENDS[fid] ? FRIENDS[fid].username : ""), body = m.kind === "invite" ? `يدعوك تلعب ${gameName(m.game)}` : m.text;
  toast(`${who}: ${body}`);
  try { if (document.hidden && "Notification" in window) { const go = () => new Notification(who, {body, icon:"img/icon.png"}); if (Notification.permission === "granted") go(); else if (Notification.permission !== "denied") Notification.requestPermission().then(p => p === "granted" && go()); } } catch(e) {}
}
const gameName = gid => { const g = byId(gid); return g ? (g.en || g.title) : gid; };
function openChat(fid){
  if (chatUnsub) { chatUnsub(); chatUnsub = null; }
  CHAT_WITH = fid; CHAT_MSGS = [];
  if (fid) { markRead(fid); chatUnsub = onSnapshot(query(collection(fs, "chats", cidOf(fid), "msgs"), orderBy("at", "desc"), limit(60)), s => {
    CHAT_MSGS = s.docs.map(d => ({id:d.id, ...d.data()})).reverse(); if (CHAT_WITH === fid && curView === "friends") { markRead(fid); frBadge(); renderChat(); renderFriendList(); } }, e => { toast("ما قدرت أفتح المحادثة: " + e.code); }); }
  if (curView === "friends") { renderFriendList(); renderChat(); }
}
async function sendMsg(fid, text, kind = "text", game){
  text = String(text || "").trim().slice(0, 1000); if (!text) return;
  const m = {from:user.uid, text, at:serverTimestamp(), kind}; if (game) m.game = game;
  try { await addDoc(collection(fs, "chats", cidOf(fid), "msgs"), m); } catch(e) { toast("ما انرسلت: " + (e.code || e.message)); }
}
function renderChat(){
  const box = $("#chat"); if (!box) return;
  const f = FRIENDS[CHAT_WITH];
  if (!CHAT_WITH || !f) { box.innerHTML = '<div class="chat-empty muted">اختر صديق من القائمة عشان تكلمه أو تدعوه يلعب معك.</div>'; return; }
  const owned = GAMES.filter(g => ownedEds(g.id).length);
  const keepDraft = $("#chatIn") ? $("#chatIn").value : "";
  const pl = playingNow(CHAT_WITH);
  box.innerHTML = `<div class="chat-h"><span class="av">${esc(f.username.charAt(0).toUpperCase())}</span><div class="who"><b class="ltr">@${esc(f.username)}</b>${pl ? `<small class="plg">يلعب ${esc(pl)}</small>` : ""}</div>
      <button class="btn mini" id="callB" type="button" title="مكالمة صوتية">📞</button><button class="btn mini" id="rpB" type="button" title="العب معي عن بعد">🎮 عن بعد</button>
      ${owned.length ? `<select id="invG" aria-label="اللعبة">${owned.map(g => `<option value="${g.id}">${esc(g.en || g.title)}</option>`).join("")}</select><button class="btn mini buy" id="invB" type="button">ادعه يلعب</button>` : ""}</div>
    <div class="chat-msgs" id="chatMsgs">${CHAT_MSGS.length ? CHAT_MSGS.map(m => {
      const me = m.from === user.uid, t = m.at && m.at.toDate ? m.at.toDate().toLocaleTimeString("ar-SA-u-nu-latn", {hour:"numeric", minute:"2-digit"}) : "";
      const body = m.kind === "invite"
        ? `<div class="inv">🎮 ${me ? "دعيته يلعب" : "يدعوك تلعب"} <b>${esc(gameName(m.game))}</b>${me ? "" : `<button class="btn mini play" data-inv="${esc(m.game)}" type="button">العب</button>`}</div>`
        : esc(m.text);
      return `<div class="msg${me ? " me" : ""}"><div class="bub">${body}</div><small class="num">${t}</small></div>`; }).join("") : '<div class="chat-empty muted">ما فيه رسائل. قل له هلا 👋</div>'}</div>
    <form class="chat-in" id="chatF"><input id="chatIn" maxlength="1000" autocomplete="off" placeholder="اكتب رسالة…"><button class="btn buy" type="submit">إرسال</button></form>`;
  const ml = $("#chatMsgs"); ml.scrollTop = ml.scrollHeight;
  const inp = $("#chatIn"); inp.value = keepDraft; inp.focus();
  $("#chatF").onsubmit = ev => { ev.preventDefault(); const v = inp.value; inp.value = ""; sendMsg(CHAT_WITH, v); };
  $("#callB").onclick = () => rtc().start("voice", [CHAT_WITH], {[CHAT_WITH]:f.username}).catch(() => {});
  $("#rpB").onclick = () => remoteModal(CHAT_WITH);
  if ($("#invB")) $("#invB").onclick = () => { const g = $("#invG").value; sendMsg(CHAT_WITH, `دعوة للعب ${gameName(g)}`, "invite", g); };
  box.querySelectorAll("[data-inv]").forEach(b => b.onclick = () => playInvite(b.dataset.inv));
}
// an invite: play the best edition you have installed, otherwise open the game page
async function playInvite(gid){
  const eds = ownedEds(gid).slice().reverse();
  if (PS && PS.multiEdition) for (const ed of eds) { const [key, inst] = instOf(gid, ed); if (inst) { try { await PS.launch(key, ed); toast("جاري التشغيل…"); } catch(e) { toast("ما اشتغلت: " + (e && e.message || e)); } return; } }
  location.hash = gid;
  toast(eds.length ? "ثبّت اللعبة أول" : "ما تملك هذي اللعبة");
}
function renderFriendList(){
  const box = $("#frList"); if (!box || !user) return;
  const all = Object.entries(FRIENDS).map(([id, f]) => ({id, ...f})).sort((a, b) => a.username.localeCompare(b.username));
  const ok = all.filter(f => f.status === "ok").sort((a, b) => (unread(b.id) - unread(a.id)) || (!!playingNow(b.id) - !!playingNow(a.id)) || (msMs(LASTMSG[b.id] && LASTMSG[b.id].at) * !!LASTMSG[b.id] - msMs(LASTMSG[a.id] && LASTMSG[a.id].at) * !!LASTMSG[a.id]));
  const inc = all.filter(f => f.status === "in"), out = all.filter(f => f.status === "out");
  const row = (f, acts, chat) => { const pl = f.status === "ok" ? playingNow(f.id) : "", lm = chat && LASTMSG[f.id];
    const sub = pl ? `<small class="plg">يلعب ${esc(pl)}</small>` : f.status === "ok" && isOnline(f.id) && !lm ? `<small class="plg">${PRES[f.id] && PRES[f.id].state === "away" ? "بعيد" : "متصل"}</small>` : lm ? `<small class="muted last">${lm.from === user.uid ? "أنت: " : ""}${esc(lm.kind === "invite" ? "🎮 دعوة للعب" : lm.text)}</small>` : "";
    return `<div class="fr${pl ? " playing" : ""}${f.status === "ok" && isOnline(f.id) ? " online" : ""}${chat ? " chatable" : ""}${CHAT_WITH === f.id ? " on" : ""}" ${chat ? `data-chat="${f.id}"` : ""}><span class="av">${esc(f.username.charAt(0).toUpperCase())}</span><div class="who"><b class="ltr">@${esc(f.username)}${PROFS[f.id] ? ` <span class="lvchip num" title="المستوى">${PROFS[f.id].level}</span>` : ""}</b>${sub}</div>${chat && unread(f.id) ? '<span class="dot"></span>' : ""}<div class="row">${acts}</div></div>`; };
  box.innerHTML = `<h3>أصدقائي <span class="tag num">${ok.length}</span></h3><div class="frlist">${ok.length ? ok.map(f => row(f, `<button class="btn mini ghost" data-fr-del="${f.id}" title="إزالة">✕</button>`, true)).join("") : '<span class="muted">ما عندك أصدقاء للحين. أضفهم باسم المستخدم حقهم.</span>'}</div>
    ${inc.length ? `<h3 style="margin-top:12px">طلبات الصداقة</h3><div class="frlist">${inc.map(f => row(f, `<button class="btn mini buy" data-fr-ok="${f.id}">قبول</button><button class="btn mini ghost" data-fr-del="${f.id}">رفض</button>`)).join("")}</div>` : ""}
    ${out.length ? `<h3 style="margin-top:12px">طلبات أرسلتها</h3><div class="frlist">${out.map(f => row(f, `<button class="btn mini ghost" data-fr-del="${f.id}">إلغاء</button>`)).join("")}</div>` : ""}`;
  box.querySelectorAll("[data-chat]").forEach(r => r.onclick = e => { if (e.target.closest("button")) return; openChat(r.dataset.chat); });
  box.querySelectorAll("[data-fr-ok]").forEach(b => b.onclick = () => { b.disabled = true; acceptFriend(b.dataset.frOk); });
  box.querySelectorAll("[data-fr-del]").forEach(b => b.onclick = () => { b.disabled = true; dropFriend(b.dataset.frDel); });
}
function renderFriends(){
  const box = $("#friends");
  if (!user) { box.innerHTML = '<div class="empty"><b style="color:var(--text)">الأصدقاء للمسجّلين فقط</b><span>سجّل دخولك عشان تضيف أصدقاءك باسم المستخدم.</span><button class="btn buy" type="button" data-login>تسجيل الدخول</button></div>'; return; }
  if (!$("#frList")) {
    box.innerHTML = `<div class="row" style="justify-content:flex-end;margin-bottom:10px"><button class="btn" type="button" id="partyB">🎉 حفلة صوتية</button></div><form class="fradd" id="frForm" novalidate><label class="fld">أضف صديق باسم المستخدم<input id="frU" dir="ltr" maxlength="21" autocomplete="off" placeholder="@username"></label><button class="btn buy" type="submit">إضافة</button></form>
      <div class="err" id="frErr" style="margin:-10px 0 12px"></div>
      <div class="frcols"><div class="panel" id="frList"></div><div class="panel chat" id="chat"></div></div>`;
    $("#partyB").onclick = partyModal;
    $("#frForm").onsubmit = async ev => { ev.preventDefault(); const btn = ev.target.querySelector("button"); btn.disabled = true; const m = await addFriend($("#frU").value); btn.disabled = false; if (m) $("#frErr").textContent = m; else { $("#frU").value = ""; $("#frErr").textContent = ""; } };
    renderChat();
  }
  renderFriendList();
  if (CHAT_WITH) { markRead(CHAT_WITH); frBadge(); }
}

let USERS = {}, ORDERS = [], COINORDERS = [], MYCO = [], ALLREFUNDS = {};
function startAdmin(){
  unsubs.push(onSnapshot(collection(fs, "users"), s => { USERS = {}; s.forEach(d => USERS[d.id] = d.data()); if (curView === "studio") renderStudio(); }, e => { $("#studioNote").textContent = "ما قدرت أقرأ قائمة اللاعبين: " + e.code; }));
  unsubs.push(onSnapshot(collectionGroup(fs, "coinOrders"), s => { COINORDERS = s.docs.map(d => ({ref:d.ref, uid:d.ref.parent.parent.id, ...d.data()})); if (curView === "studio") renderStudio(); }, () => {}));
  unsubs.push(onSnapshot(collection(fs, "codes"), s => { CODES = {}; s.forEach(d => CODES[d.id] = d.data()); if (curView === "studio") renderCodes(); }, () => {}));
  unsubs.push(onSnapshot(collectionGroup(fs, "gifts"), s => { ALLGIFTS = s.docs.map(d => ({ref:d.ref, from:d.ref.parent.parent.id, ...d.data()})); if (curView === "studio") renderStudio(); }, () => {}));
  unsubs.push(onSnapshot(collectionGroup(fs, "refunds"), s => { ALLREFUNDS = {}; s.forEach(d => ALLREFUNDS[d.ref.parent.parent.id + "/" + d.id] = {ref:d.ref, ...d.data()}); if (curView === "studio") renderStudio(); }, () => {}));
  unsubs.push(onSnapshot(collectionGroup(fs, "library"), s => { ORDERS = s.docs.map(d => ({ref:d.ref, uid:d.ref.parent.parent.id, ...d.data()})); if (curView === "studio") renderStudio(); }, e => { $("#studioNote").textContent = "ما قدرت أقرأ الطلبات: " + e.code; }));
}
function renderGifts(){
  const box = $("#giftRows"); if (!box) return;
  const l = ALLGIFTS.filter(g => g.status === "pending");
  box.innerHTML = l.length ? l.map((g, i) => `<div class="prop"><b>🎁 ${esc(edName(byId(g.game), g.edition))} · ${money(g.price)}</b><span class="muted">من ${esc((USERS[g.from] || {}).username ? "@" + USERS[g.from].username : g.from)} إلى @${esc(g.toName)}</span>
    <div class="row" style="gap:6px"><button class="btn mini buy" type="button" data-gs="${i}">تأكيد الدفع وإرسال الهدية</button><button class="btn mini danger" type="button" data-gx="${i}">رفض</button></div></div>`).join("") : '<p class="muted">ما فيه هدايا تنتظر.</p>';
  box.querySelectorAll("[data-gs]").forEach(b => b.onclick = async () => { const g = l[+b.dataset.gs]; b.disabled = true;
    try { const bt = writeBatch(fs); bt.set(doc(fs, "users", g.to, "library", `${g.game}__${g.edition}`), {game:g.game, edition:g.edition, price:0, status:"granted", at:serverTimestamp(), from:(USERS[g.from] || {}).username || "صديق"});
      bt.update(g.ref, {status:"sent", sentAt:serverTimestamp()}); await bt.commit(); toast("وصلت الهدية ✓"); } catch(e) { b.disabled = false; toast("ما تم: " + e.code); } });
  box.querySelectorAll("[data-gx]").forEach(b => b.onclick = async () => { const g = l[+b.dataset.gx]; try { await updateDoc(g.ref, {status:"rejected", sentAt:serverTimestamp()}); } catch(e) { toast("ما تم: " + e.code); } });
}
function renderStudio(){
  renderGifts();
  const byUser = {}; ORDERS.forEach(o => (byUser[o.uid] = byUser[o.uid] || []).push(o));
  const paid = ORDERS.filter(o => o.status === "paid").reduce((a, o) => a + (o.price || 0), 0), pend = ORDERS.filter(o => o.status === "pending").length;
  const coPaid = COINORDERS.filter(o => o.status === "paid").reduce((a, o) => a + (o.price || 0), 0), coPend = COINORDERS.filter(o => o.status === "pending").length;
  $("#kpis").innerHTML = `<div class="kpi"><b>${Object.keys(USERS).length}</b><span>لاعب مسجّل</span></div><div class="kpi"><b>${ORDERS.length}</b><span>طلب ولعبة في المكتبات</span></div><div class="kpi"><b>${pend + coPend}</b><span>بانتظار الدفع</span></div><div class="kpi"><b>${money(paid + coPaid)}</b><span>إيرادات مؤكدة</span></div>`;
  const q = $("#q").value.trim().toLowerCase().replace(/\s+/g, " "), qd = q.replace(/[\s#]/g, ""), fg = $("#fGame").value, fst = $("#fSt").value;
  const list = Object.entries(USERS).map(([id, u]) => ({id, ...u, orders: byUser[id] || []})).filter(p => {
    if (q && !((p.name || "") + " " + (p.email || "") + " @" + (p.username || "")).toLowerCase().includes(q) && !(qd && String(p.pid || "").includes(qd))) return false;
    if (fg && !p.orders.some(o => o.game === fg)) return false;
    if (fst && !p.orders.some(o => (!fg || o.game === fg) && o.status === fst)) return false; return true;
  }).sort((a, b) => tsMs(b.joined) - tsMs(a.joined));
  const tb = $("#rows"); tb.innerHTML = "";
  const coBy = {}; COINORDERS.forEach(o => (coBy[o.uid] = coBy[o.uid] || []).push(o));
  if (!list.length) tb.innerHTML = `<tr><td colspan="6" class="muted" style="text-align:center;padding:24px">${Object.keys(USERS).length ? "ما فيه نتائج تطابق البحث" : "ما سجّل أحد للحين."}</td></tr>`;
  list.forEach(p => { const tr = document.createElement("tr");
    tr.innerHTML = `<td class="pidtag">${fmtPid(p.pid)}</td><td><b></b></td><td class="ltr"></td><td><div class="row" style="gap:6px;flex-wrap:nowrap"><span class="coin" aria-hidden="true"></span><b class="num">${coins(p.coins)}</b><button class="btn mini" data-coins>تعديل</button></div><div class="row" style="gap:4px;margin-top:6px;flex-wrap:nowrap"><select data-gsel aria-label="نسخة">${GAMES.map(g => g.editions.map(e => `<option value="${g.id}__${e.id}">${esc(e.name)}</option>`).join("")).join("")}</select><button class="btn mini" data-grant>منح</button></div></td><td>${fdate(p.joined)}</td><td><div class="gl">${(coBy[p.id] || []).filter(o => o.status === "pending").map(o => `<div class="gi"><span class="num">${coins(o.coins)} PS COIN</span><span class="num muted">${money(o.price)}</span>${stPill("pending")}<button class="btn mini" data-copay="${esc(o.ref.id)}">تأكيد الدفع</button><button class="btn mini danger" data-corej="${esc(o.ref.id)}">رفض</button></div>`).join("")}${p.orders.length ? p.orders.map((o, i) => { const g = byId(o.game);
      return `<div class="gi"><span>${esc(g ? (g.en || g.title) : o.game)} · ${esc(g ? edName(g, o.edition) : o.edition)}</span><span class="num muted">${money(o.price || 0)}</span>${o.code ? `<span class="tag ltr">${esc(o.code)}</span>` : ""}${stPill(o.status)}${o.status === "pending" ? `<button class="btn mini" data-pay="${i}">تأكيد الدفع</button><button class="btn mini" data-gift="${i}">منح مجاني</button><button class="btn mini danger" data-rej="${i}">رفض</button>` : ""}${(() => { const r = ALLREFUNDS[p.id + "/" + o.ref.id]; return r && r.status === "pending" ? `<span class="st pending">يطلب استرجاع · لعب ${r.minutes} د</span><button class="btn mini" data-rfok="${i}">وافق وارجع ${money(r.price)}</button><button class="btn mini danger" data-rfno="${i}">ارفض</button>` : ""; })()}</div>`; }).join("") : '<span class="muted">ما عنده ألعاب</span>'}</div></td>`;
    tr.children[1].firstChild.textContent = p.name || "—"; if (p.username) { const sm = document.createElement("div"); sm.className = "muted ltr"; sm.style.fontSize = "12px"; sm.textContent = "@" + p.username; tr.children[1].appendChild(sm); } tr.children[2].textContent = p.email || "—";
    tr.querySelector("[data-coins]").onclick = () => coinAdjust(p);
    tr.querySelector("[data-grant]").onclick = async ev => { const v = tr.querySelector("[data-gsel]").value, [game, edition] = v.split("__"), b = ev.target; b.disabled = true;
      try { await setDoc(doc(fs, "users", p.id, "library", v), {game, edition, price:0, status:"granted", at:serverTimestamp()}); toast(`انمنحت ${edName(byId(game), edition)} لـ ${p.name || "اللاعب"} ✓`); }
      catch(e) { toast("ما انمنحت: " + (e.code || e.message)); } b.disabled = false; };
    tr.querySelectorAll("[data-copay]").forEach(b => b.onclick = async () => { const o = (coBy[p.id] || []).find(x => x.ref.id === b.dataset.copay); if (!o) return; b.disabled = true;
      try { const bt = writeBatch(fs); bt.update(o.ref, {status:"paid", paidAt:serverTimestamp()}); bt.update(doc(fs, "users", p.id), {coins:increment(o.coins)}); await bt.commit(); toast(`انضاف ${coins(o.coins)} PS COIN لـ ${p.name || "اللاعب"} ✓`); }
      catch(e) { b.disabled = false; toast("ما تحدّث الطلب: " + e.code); } });
    tr.querySelectorAll("[data-corej]").forEach(b => b.onclick = () => confirmModal("رفض طلب العملات؟", "ينحذف الطلب وما تنضاف عملات.", "رفض", async () => { const o = (coBy[p.id] || []).find(x => x.ref.id === b.dataset.corej); try { await deleteDoc(o.ref); toast("انرفض الطلب"); } catch(e) { toast("ما انحذف: " + e.code); } }));
    const setSt = async (b, i, status, msg) => { b.disabled = true; try { await updateDoc(p.orders[i].ref, {status, paidAt:serverTimestamp()}); toast(msg); } catch(e) { b.disabled = false; toast("ما تحدّث الطلب: " + e.code); } };
    tr.querySelectorAll("[data-pay]").forEach(b => b.onclick = () => setSt(b, +b.dataset.pay, "paid", "تم تأكيد الدفع ✓"));
    tr.querySelectorAll("[data-gift]").forEach(b => b.onclick = () => setSt(b, +b.dataset.gift, "granted", "انمنحت اللعبة مجانًا ✓"));
    tr.querySelectorAll("[data-rej]").forEach(b => b.onclick = () => confirmModal("رفض الطلب؟", "ينحذف الطلب من مكتبة اللاعب.", "رفض", async () => { try { await deleteDoc(p.orders[+b.dataset.rej].ref); toast("انرفض الطلب"); } catch(e) { toast("ما انحذف: " + e.code); } }));
    tr.querySelectorAll("[data-rfok]").forEach(b => b.onclick = () => { const o = p.orders[+b.dataset.rfok], r = ALLREFUNDS[p.id + "/" + o.ref.id];
      confirmModal("الموافقة على الاسترجاع؟", `رجّع للاعب ${money(r.price)} بنفس طريقة الدفع، وتنشال النسخة من مكتبته.`, "موافقة", async () => { try { const bt = writeBatch(fs); bt.delete(o.ref); bt.update(r.ref, {status:"done", doneAt:serverTimestamp()}); await bt.commit(); toast("تم الاسترجاع ✓"); } catch(e) { toast("ما تم: " + e.code); } }); });
    tr.querySelectorAll("[data-rfno]").forEach(b => b.onclick = async () => { const o = p.orders[+b.dataset.rfno], r = ALLREFUNDS[p.id + "/" + o.ref.id]; b.disabled = true; try { await updateDoc(r.ref, {status:"rejected", doneAt:serverTimestamp()}); toast("انرفض الاسترجاع"); } catch(e) { b.disabled = false; toast("ما تم: " + e.code); } });
    tb.appendChild(tr); });
  renderCodes();
}
["#q", "#fGame", "#fSt"].forEach(s => $(s).addEventListener("input", renderStudio));

function coinAdjust(p){
  openModal(`<div class="eyebrow">PS COIN</div><h1 class="mbox-t">عملات ${esc(p.name || "")}</h1><p class="mbox-t">الرصيد الحالي: <b class="num">${coins(p.coins)}</b> · PS ID <span class="num">${fmtPid(p.pid)}</span></p>
    <label class="fld">كم PS COIN تضيف؟ (بالسالب للخصم، مثل -0.0001)<input id="caN" type="number" step="0.0001" dir="ltr" value="0.0001"></label><div class="err" id="caErr"></div>
    <div class="row"><button class="btn buy" id="caOk" type="button">تطبيق</button><button class="btn ghost" data-close type="button">إلغاء</button></div>`);
  $("#caOk").onclick = async () => { const n = Math.round(+$("#caN").value * COIN_UNIT);
    if (!n) { $("#caErr").textContent = "اكتب رقم غير صفر"; return; }
    if ((p.coins || 0) + n < 0) { $("#caErr").textContent = "الرصيد ما يصير أقل من صفر"; return; }
    $("#caOk").disabled = true;
    try { await updateDoc(doc(fs, "users", p.id), {coins:increment(n)}); closeModal(); toast(n > 0 ? `انضاف ${coins(n)} PS COIN ✓` : `انخصم ${coins(-n)} PS COIN`); }
    catch(e) { $("#caOk").disabled = false; $("#caErr").textContent = "ما تحدّث الرصيد: " + e.code; } };
}

/* ---------------- PS COIN wallet ---------------- */
function renderCoins(){
  const w = $("#wallet"), pk = $("#packs"), ol = $("#coinOrders");
  if (!user) { w.innerHTML = '<div class="empty" style="flex:1"><b style="color:var(--text)">سجّل دخولك عشان تشوف رصيدك</b><button class="btn buy" type="button" data-login>تسجيل الدخول</button></div>'; pk.innerHTML = ""; ol.innerHTML = ""; return; }
  w.innerHTML = `<span class="coin big" aria-hidden="true"></span><div><div class="lbl">PS COIN</div><div class="bal">${coins(PROFILE && PROFILE.coins)}</div></div><div class="muted" style="margin-inline-start:auto;font-size:14px;max-width:40ch">عملات بريميوم ستوديو. العملة الوحدة بـ $100,000. كل حساب جديد ياخذ ${coins(SIGNUP_BONUS)} PS COIN هدية.</div>`;
  pk.innerHTML = PACKS.map(x => `<div class="pack"><div class="amt"><span class="coin" aria-hidden="true"></span>${coins(x.coins)}</div><div class="price" style="font-size:18px">${money(x.price)}</div><button class="btn buy" type="button" data-pack="${x.id}">شراء</button></div>`).join("");
  pk.querySelectorAll("[data-pack]").forEach(b => b.onclick = () => buyPack(PACKS.find(x => x.id === b.dataset.pack)));
  const list = MYCO.slice().sort((a, b) => tsMs(b.at) - tsMs(a.at));
  ol.innerHTML = list.length ? list.map(o => `<div class="li" style="grid-template-columns:minmax(0,1fr) auto"><div class="info"><b class="num"><span class="coin" style="display:inline-block;vertical-align:-3px;margin-inline-end:6px"></span>${coins(o.coins)} PS COIN</b><span class="muted" style="font-size:12px">${fdate(o.at)} · <span class="num">${money(o.price)}</span></span></div><div>${stPill(o.status)}</div></div>`).join("") : '<div class="empty">ما عندك طلبات عملات.</div>';
}
function buyPack(x){
  openModal(`<div class="eyebrow">PS COIN</div><h1 class="mbox-t">${coins(x.coins)} PS COIN</h1><div class="row" style="justify-content:space-between"><span class="muted">باقة عملات</span><span class="price">${money(x.price)}</span></div>
    <p class="mbox-t">الدفع الإلكتروني ما تفعّل للحين. لما تأكد الطلب يوصل للاستوديو بحالة «بانتظار الدفع»، وتنضاف العملات لرصيدك أول ما يتأكد الدفع.</p><div class="err" id="pkErr"></div>
    <div class="row"><button class="btn buy" id="pkOk" type="button">تأكيد الطلب</button><button class="btn ghost" data-close type="button">إلغاء</button></div>`);
  $("#pkOk").onclick = async () => { $("#pkOk").disabled = true;
    try { await setDoc(doc(collection(fs, "users", user.uid, "coinOrders")), {pack:x.id, coins:x.coins, price:x.price, status:"pending", at:serverTimestamp()}); closeModal(); toast("وصل طلبك ✓"); }
    catch(e) { $("#pkOk").disabled = false; $("#pkErr").textContent = "ما انحفظ الطلب (" + (e.code || "خطأ") + ")"; } };
}

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
  const h0 = (location.hash || "#store").slice(1);
  if (h0 === "bigpicture") { bigPicture().open(); return; }
  const h = h0, g = byId(h);
  let v = g ? "game" : (["store", "library", "downloads", "friends", "market", "workshop", "studio", "download", "account", "coins"].includes(h) ? h : "store");
  if (v === "studio" && !isAdmin) v = "store";
  curView = v; $$(".view").forEach(s => s.hidden = s.dataset.view !== v);
  $$("#tabs a").forEach(a => a.setAttribute("aria-current", a.dataset.v === (g ? "store" : v) ? "page" : "false"));
  render(); scrollTo(0, 0);
}
function guestNote(){
  const v = $(".view:not([hidden])"); if (!v) return; let n = v.querySelector(".guestnote");
  const show = authReady && !user && curView !== "download" && curView !== "coins";
  if (!show) { if (n) n.remove(); return; }
  if (!n) { n = document.createElement("div"); n.className = "guestnote"; n.innerHTML = '<span>أنت تتصفح كضيف. تقدر تتفرج بس، وللشراء والتثبيت لازم تسجّل دخولك.</span><button class="btn buy mini" type="button" data-login>تسجيل الدخول</button>'; v.prepend(n); }
}
let BP = null;
function bigPicture(){ if (!BP) BP = makeBP({$, esc, GAMES, ownedEds, instOf, ED_TAG, PS, toast, cover, money, ent, keyTitle, pct, progText,
  state:() => ({user, PROFILE, FRIENDS, PLAY, RUNNING, PROGRESS, online:isOnline, playing:playingNow})}); return BP; }
function render(){
  try { publishProfile(); } catch(e) {}
  try { renderBell(); } catch(e) {}
  try { if (BP && BP.on) BP.refresh(); } catch(e) {}
  try { renderDlBar(); } catch(e) {}
  if (!authReady) return;
  guestNote();
  if (curView === "store") { renderHero(); renderGrid(); }
  else if (curView === "game") renderGame(byId(location.hash.slice(1)));
  else if (curView === "library") renderLib();
  else if (curView === "studio") renderStudio();
  else if (curView === "account") renderAccount();
  else if (curView === "coins") renderCoins();
  else if (curView === "friends") renderFriends();
  else if (curView === "downloads") renderDownloads();
  else if (curView === "market") renderMarket();
  else if (curView === "workshop") renderWorkshop();
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
const brandHTML = '<div class="brand"><span class="mark"><i>P</i><i>S</i></span><b>PREMIUM <span>STUDIO</span></b></div>';
function showAuth(mode, note){
  closeModal();
  const login = mode === "login";
  $("#gbox").innerHTML = `${brandHTML}<h1>${login ? "تسجيل الدخول" : "أنشئ حسابك"}</h1>${note ? `<p>${esc(note)}</p>` : '<p>حسابك يحفظ مكتبتك ومشترياتك، ويشتغل في الموقع والتطبيق.</p>'}
    <div class="seg"><button type="button" data-m="login" aria-pressed="${login}">تسجيل الدخول</button><button type="button" data-m="signup" aria-pressed="${!login}">حساب جديد</button></div>
    <form class="f" id="af" novalidate>
      ${login ? "" : '<label class="fld">اسم المستخدم (ما يتكرر، إنجليزي وأرقام و _ و .)<input id="aU" dir="ltr" autocomplete="username" maxlength="20" required placeholder="xox_gamer"></label><div class="muted" id="aUmsg" style="font-size:12px;min-height:1em"></div><label class="fld">اسم اللاعب<input id="aN" autocomplete="nickname" maxlength="32" required></label>'}
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
  if (!login) wireUnameCheck($("#aU"), $("#aUmsg"));
  setTimeout(() => { const f = $("#gbox input"); if (f && !isMobile) f.focus(); }, 30);
  $("#af").addEventListener("submit", async ev => { ev.preventDefault();
    const err = $("#aErr"); err.className = "err"; err.textContent = "";
    const m = $("#aE").value.trim().toLowerCase(), p = $("#aP").value;
    if (!/^[^\s@]+@[^\s@]+\.[^\s@]{2,}$/.test(m)) { err.textContent = "اكتب إيميل صحيح، مثل name@example.com"; return; }
    if (p.length < 6) { err.textContent = "كلمة السر لازم تكون 6 أحرف أو أكثر"; return; }
    let n = "";
    let un = "";
    if (!login) { un = normUname($("#aU").value);
      if (!UNAME_RE.test(un)) { err.textContent = "اسم المستخدم من 3 إلى 20 حرف: إنجليزي صغير وأرقام و _ و ."; return; }
      $("#aGo").disabled = true;
      const free = await unameFree(un); $("#aGo").disabled = false;
      if (!free) { err.textContent = "اسم المستخدم هذا مأخوذ، اختر غيره"; return; }
      n = $("#aN").value.trim();
      if (n.length < 2) { err.textContent = "اكتب اسم لاعب من حرفين أو أكثر"; return; }
      if (p !== $("#aP2").value) { err.textContent = "كلمتين السر مو متطابقة"; return; }
      if (!$("#aAgree").checked) { err.textContent = "لازم توافق عشان نكمل التسجيل"; return; } }
    $("#aGo").disabled = true;
    try {
      if (login) await signInWithEmailAndPassword(auth, m, p);
      else { pendingName = n; pendingUname = un; const c = await createUserWithEmailAndPassword(auth, m, p); await updateProfile(c.user, {displayName:n}); await ensureProfile(c.user, n, un); sendEmailVerification(c.user).catch(()=>{}); toast("تم إنشاء حسابك ✓ أرسلنا رابط تأكيد لإيميلك"); }
    } catch(e) { $("#aGo").disabled = false; err.textContent = authErr(e); }
  });
}
let pendingName = "", pendingUname = "";
/* usernames: unique, reserved in usernames/<name> so no two accounts share one */
const UNAME_RE = /^[a-z0-9_.]{3,20}$/;
const normUname = v => String(v || "").trim().toLowerCase().replace(/^@/, "");
async function unameFree(un){ try { return !(await getDoc(doc(fs, "usernames", un))).exists(); } catch(e) { return false; } }
function wireUnameCheck(inp, msg){
  let t = 0; inp.addEventListener("input", () => { clearTimeout(t); const un = normUname(inp.value); msg.className = "muted"; msg.textContent = "";
    if (!un) return; if (!UNAME_RE.test(un)) { msg.className = "err"; msg.textContent = "3 إلى 20 حرف: a-z و 0-9 و _ و ."; return; }
    t = setTimeout(async () => { const ok = await unameFree(un); if (normUname(inp.value) !== un) return; msg.className = ok ? "okmsg" : "err"; msg.textContent = ok ? `@${un} متاح ✓` : `@${un} مأخوذ`; }, 350); });
}
async function claimUname(u, un){
  const b = writeBatch(fs);
  b.set(doc(fs, "usernames", un), {uid:u.uid, at:serverTimestamp()});
  b.update(doc(fs, "users", u.uid), {username:un});
  await b.commit();
}
function askUsername(u){
  closeModal();
  $("#gbox").innerHTML = `${brandHTML}<h1>اختر اسم المستخدم</h1><p>كل حساب لازم يكون له اسم مستخدم ما يتكرر. ما تقدر تغيّره بعدين.</p>
    <form class="f" id="uf" novalidate><label class="fld">اسم المستخدم<input id="uU" dir="ltr" autocomplete="username" maxlength="20" required placeholder="xox_gamer"></label><div class="muted" id="uUmsg" style="font-size:12px;min-height:1em"></div><div class="err" id="uErr"></div><button class="btn buy" id="uGo" type="submit">حفظ</button></form>`;
  $("#gate").hidden = false; wireUnameCheck($("#uU"), $("#uUmsg"));
  $("#uf").addEventListener("submit", async ev => { ev.preventDefault(); const un = normUname($("#uU").value), err = $("#uErr"); err.textContent = "";
    if (!UNAME_RE.test(un)) { err.textContent = "3 إلى 20 حرف: إنجليزي صغير وأرقام و _ و ."; return; }
    $("#uGo").disabled = true;
    try { if (!(await unameFree(un))) throw {code:"taken"}; await claimUname(u, un); $("#gate").hidden = true; toast(`صار اسمك @${un} ✓`); }
    catch(e) { $("#uGo").disabled = false; err.textContent = e.code === "taken" || e.code === "permission-denied" ? "اسم المستخدم هذا مأخوذ، اختر غيره" : "ما انحفظ: " + (e.code || e.message); } });
}
/* every account gets a random 10-digit PS ID (leading zeros allowed), reserved in playerIds/<pid> so no two accounts share one; it never changes */
const newPid = () => { const a = new Uint32Array(2); crypto.getRandomValues(a); return String((a[0] * 4294967296 + a[1]) % 10000000000).padStart(10, "0"); };
const profileJobs = {};
function ensureProfile(u, name, un){ return profileJobs[u.uid] || (profileJobs[u.uid] = ensureProfileOnce(u, name, un).finally(() => { delete profileJobs[u.uid]; })); }
async function ensureProfileOnce(u, name, un){
  const r = doc(fs, "users", u.uid);
  const s = await getDoc(r);
  const cur = s.exists() ? s.data() : null;
  if (cur && cur.pid && "coins" in cur) return;
  if (cur && cur.pid) { await updateDoc(r, {coins:SIGNUP_BONUS}); return; }   // older account: signup bonus once
  for (let i = 0; i < 5; i++) {
    const pid = newPid(), b = writeBatch(fs);
    b.set(doc(fs, "playerIds", pid), {uid:u.uid, at:serverTimestamp()});
    if (cur) b.update(r, "coins" in cur ? {pid} : {pid, coins:SIGNUP_BONUS});
    else { const d = {pid, coins:SIGNUP_BONUS, name:(name || u.displayName || u.email.split("@")[0]).slice(0, 32), email:u.email.toLowerCase(), joined:serverTimestamp()};
      if (un && UNAME_RE.test(un)) { b.set(doc(fs, "usernames", un), {uid:u.uid, at:serverTimestamp()}); d.username = un; }
      b.set(r, d); }
    try { await b.commit(); return; } catch(e) { if (i === 4) throw e; }   // a taken number fails the batch, so try another
  }
}
function sendOwned(){ if (!PS || !PS.setOwned || !user) return; const o = {}; for (const g of GAMES) o[g.id] = ownedEds(g.id); PS.setOwned(o).catch(() => {}); }
function sendUserToApp(){ if (!PS || !PS.setUser || !user) return;
  PS.setUser({username:(PROFILE && PROFILE.username) || "", name:(PROFILE && PROFILE.name) || user.displayName || "", pid:(PROFILE && PROFILE.pid) || "", email:user.email || "", uid:user.uid, refreshToken:user.refreshToken || ""}).then(sendOwned, () => {}); }
function setAdmin(){ isAdmin = !!(user && user.email && user.email.toLowerCase() === ADMIN_EMAIL && user.emailVerified); $("#tabStudio").hidden = !isAdmin; }
function applyChrome(){
  $("#me").hidden = !user; $("#guestBar").hidden = !!user || !authReady;
  if (user) { const n = (PROFILE && PROFILE.name) || user.displayName || user.email; $("#meName").textContent = n; $("#meAv").textContent = (n || "?").trim().charAt(0).toUpperCase(); $("#mePid").textContent = PROFILE && PROFILE.username ? "@" + PROFILE.username : (PROFILE && PROFILE.pid ? "PS ID " + PROFILE.pid : ""); }
  $("#coinChip").hidden = !user; if (user) $("#coinBal").textContent = coins(PROFILE && PROFILE.coins);
}
onAuthStateChanged(auth, async u => {
  unsubs.forEach(f => { try { f(); } catch(e) {} }); unsubs = []; LIB = {}; PROFILE = null; FRIENDS = {}; watchPresence(); watchChats(); PLAY = {}; REFUNDS = {}; ACH = {}; WISH = {}; PREFS = {fav:[], hidden:[]}; lastPub = ""; frBadge();
    SHARED = {}; for (const k of Object.keys(famUnsubs)) { famUnsubs[k](); delete famUnsubs[k]; } CARDS = {}; DROPS = {}; CBADGES = {}; LISTINGS = []; FAMILY = null; GIFTS = []; USERS = {}; ORDERS = []; CODES = {}; MYCO = []; COINORDERS = [];
  user = u; authReady = true; setAdmin();
  if (u) {
    $("#gate").hidden = true; guest = false;
    try { await ensureProfile(u, pendingName, pendingUname); } catch(e) { toast("ما قدرت أفتح حسابك في قاعدة البيانات: " + (e.code || e.message)); }
    unsubs.push(onSnapshot(doc(fs, "users", u.uid), s => { PROFILE = s.exists() ? s.data() : null; applyChrome(); sendUserToApp();
      if (PROFILE && PROFILE.pid && !PROFILE.username && !$("#uf")) askUsername(u); if (curView === "account") renderAccount(); if (curView === "coins") renderCoins(); }));
    unsubs.push(onSnapshot(collection(fs, "users", u.uid, "library"), s => { LIB = {}; s.forEach(d => LIB[d.id] = d.data()); render(); autoInstall(); sendOwned(); }, e => toast("ما قدرت أقرأ مكتبتك: " + e.code)));
    unsubs.push(onSnapshot(collection(fs, "users", u.uid, "friends"), s => { FRIENDS = {}; s.forEach(d => FRIENDS[d.id] = d.data()); watchChats(); frBadge(); watchPresence(); if (curView === "friends") renderFriends(); }, () => {}));
    unsubs.push(onSnapshot(collection(fs, "users", u.uid, "achievements"), s => { ACH = {}; s.forEach(d => ACH[d.id] = d.data()); publishProfile(); if (curView === "account") renderProfile(); }, () => {}));
    unsubs.push(onSnapshot(collection(fs, "users", u.uid, "cards"), s => { CARDS = {}; s.forEach(d => CARDS[d.id] = d.data()); if (curView === "account") renderAccount(); if (curView === "market") renderMarket(); }, () => {}));
    unsubs.push(onSnapshot(collection(fs, "users", u.uid, "cardDrops"), s => { DROPS = {}; s.forEach(d => DROPS[d.id] = d.data()); maybeDrop(); }, () => {}));
    unsubs.push(onSnapshot(collection(fs, "users", u.uid, "badges"), s => { CBADGES = {}; s.forEach(d => CBADGES[d.id] = d.data()); if (curView === "account") renderAccount(); }, () => {}));
    unsubs.push(onSnapshot(query(collection(fs, "listings"), where("status", "==", "open")), s => { LISTINGS = s.docs.map(d => ({id:d.id, ...d.data()})).sort((a, b) => a.price - b.price); if (curView === "market") renderMarket(); }, () => {}));
    unsubs.push(onSnapshot(doc(fs, "families", u.uid), s => { FAMILY = s.exists() ? s.data() : null; if (curView === "account") renderAccount(); }, () => {}));
    unsubs.push(onSnapshot(collection(fs, "users", u.uid, "gifts"), s => { GIFTS = s.docs.map(d => ({id:d.id, ...d.data()})); }, () => {}));
    watchShared();
    rtc().watchIncoming();
    unsubs.push(onSnapshot(collection(fs, "users", u.uid, "devices"), s => { const first = !Object.keys(DEVICES).length; DEVICES = {}; s.forEach(d => DEVICES[d.id] = d.data()); if (first) registerDevice(); if (curView === "account") renderAccount(); renderBell(); }, () => {}));
    unsubs.push(onSnapshot(doc(fs, "users", u.uid, "meta", "security"), s => { SECURITY = s.exists() ? s.data() : {}; checkSignOutAll(); }, () => {}));
    if (PS && PS.getSettings) PS.getSettings().then(x => { PARENTAL = x && x.parental; document.body.classList.toggle("kidmode", !!(PARENTAL && PARENTAL.on)); }).catch(() => {});
    unsubs.push(onSnapshot(collection(fs, "users", u.uid, "wishlist"), s => { WISH = {}; s.forEach(d => WISH[d.id] = d.data()); render(); }, () => {}));
    unsubs.push(onSnapshot(doc(fs, "users", u.uid, "meta", "prefs"), s => { PREFS = s.exists() ? {fav:[], hidden:[], ...s.data()} : {fav:[], hidden:[]}; render(); }, () => {}));
    beat();
    unsubs.push(onSnapshot(collection(fs, "users", u.uid, "refunds"), s => { REFUNDS = {}; s.forEach(d => REFUNDS[d.id] = d.data()); render(); }, () => {}));
    unsubs.push(onSnapshot(collection(fs, "users", u.uid, "play"), s => { PLAY = {}; s.forEach(d => PLAY[d.id] = d.data()); render(); maybeDrop(); }, () => {}));
    unsubs.push(onSnapshot(collection(fs, "users", u.uid, "coinOrders"), s => { MYCO = s.docs.map(d => ({id:d.id, ...d.data()})); if (curView === "coins") renderCoins(); }, () => {}));
    if (isAdmin) startAdmin();
  } else {
    let g = false; try { g = sessionStorage.getItem("ps-guest") === "1"; } catch(e) {}
    if (!g && !guest) showAuth("signup"); else $("#gate").hidden = true;
    if (PS && PS.setUser) PS.setUser(null);
  }
  applyChrome(); route();
});
restartHero();
