// Premium Engine editor.
import * as THREE from 'three';
import { TransformControls } from 'three/addons/TransformControls.js';
import { ENGINE_VERSION, MESHES, DEG, ACTOR_TYPES, newActor, buildLevel, applyWorld, applyTransform, makeRenderer, Game, SCRIPT_TEMPLATE, checkScript, DEFAULT_MATERIAL } from './runtime.js';

const $ = s => document.querySelector(s), $$ = s => [...document.querySelectorAll(s)];
const esc = s => String(s ?? '').replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const clone = o => JSON.parse(JSON.stringify(o));
const r4 = v => Math.round(v * 1e4) / 1e4;
const uid = p => p + Math.random().toString(36).slice(2, 9);
const store = { get(k, d) { try { const v = localStorage.getItem(k); return v == null ? d : JSON.parse(v); } catch (e) { return d; } }, set(k, v) { try { localStorage.setItem(k, JSON.stringify(v)); return true; } catch (e) { return false; } } };

/* =================================================================== state */
const S = {
  project: null, level: 'Main', sel: null, clip: null, dirty: false, undo: [], redo: [],
  tool: 'translate', space: 'world', snap: { t: true, r: true, s: false, tv: 0.5, rv: 15, sv: 0.25 },
  camSpeed: 4, view: 'Perspective', mode: 'Lit', show: { grid: true, icons: true, stats: false }, pie: null, cbFolder: 'Content', cbSel: null,
  prefs: store.get('pge:prefs', { sensitivity: 1, autosave: 0 })
};
const L = () => S.project.levels[S.level];
const actor = id => L().actors.find(a => a.id === id);

/* =================================================================== log & toast */
const logBox = () => $('#log');
function log(msg, kind = '') {
  const t = new Date().toTimeString().slice(0, 8), el = document.createElement('div');
  if (!kind) kind = /^error/i.test(msg) ? 'error' : /^warning/i.test(msg) ? 'warn' : '';
  el.className = 'l ' + kind; el.innerHTML = `<span class="t">${t}</span>${esc(msg)}`; logBox().appendChild(el);
  const f = $('#logFilter').value.trim().toLowerCase(); if (f && !msg.toLowerCase().includes(f)) el.classList.add('hidden');
  logBox().scrollTop = 1e9;
  if (kind === 'error') { const sb = $('[data-cmd=showLog]'); sb.style.color = 'var(--err)'; }
}
let toastT = 0;
function toast(t) { const el = $('#toast'); el.textContent = t; el.classList.add('on'); clearTimeout(toastT); toastT = setTimeout(() => el.classList.remove('on'), 2200); }

/* =================================================================== projects */
const MATS = {
  M_Default: { ...DEFAULT_MATERIAL },
  M_Floor: { name: 'M_Floor', color: '#5d6168', metalness: 0, roughness: 0.92, emissive: '#000000', emissiveIntensity: 1, opacity: 1 },
  M_Wall: { name: 'M_Wall', color: '#a9aeb6', metalness: 0, roughness: 0.75, emissive: '#000000', emissiveIntensity: 1, opacity: 1 },
  M_Accent: { name: 'M_Accent', color: '#3a7bff', metalness: 0.1, roughness: 0.4, emissive: '#000000', emissiveIntensity: 1, opacity: 1 },
  M_Gold: { name: 'M_Gold', color: '#e3b341', metalness: 0.95, roughness: 0.25, emissive: '#3a2a00', emissiveIntensity: 1, opacity: 1 },
  M_Red: { name: 'M_Red', color: '#e05252', metalness: 0, roughness: 0.5, emissive: '#000000', emissiveIntensity: 1, opacity: 1 },
  M_Glass: { name: 'M_Glass', color: '#9fd3ff', metalness: 0.1, roughness: 0.05, emissive: '#000000', emissiveIntensity: 1, opacity: 0.35 }
};
const SC_ROTATOR = `// Spins this actor. Change speed in degrees per second.
const speed = 90;

function onUpdate(dt) {
  self.rotation.y += speed * dt * Math.PI / 180;
}
`;
const SC_PICKUP = `// A coin: spins, bobs, and adds to the score when the player touches it.
let baseY = 0;

function onStart() {
  baseY = self.position.y;
  world.vars.score = world.vars.score || 0;
  world.vars.total = (world.vars.total || 0) + 1;
  world.hud('score', 'Coins: ' + world.vars.score + ' / ' + world.vars.total);
}

function onUpdate(dt) {
  self.rotation.y += 2 * dt;
  self.position.y = baseY + Math.sin(world.time * 3) * 0.15;
}

function onOverlap(other) {
  world.vars.score++;
  world.hud('score', 'Coins: ' + world.vars.score + ' / ' + world.vars.total);
  if (world.vars.score === world.vars.total) world.message('All coins collected!');
  self.destroy();
}
`;
const SC_GOAL = `// Shows a message when the player reaches this trigger.
function onOverlap(other) {
  world.message('Goal reached in ' + world.time.toFixed(1) + 's');
}
`;
const SC_LAUNCH = `// A jump pad: throws the player up when they step on it.
function onOverlap(other) {
  if (other.isPlayer) other.launch(0, 11, 0);
}
`;
function floorAndSun(acts) {
  acts.push(newActor('StaticMesh', { name: 'Floor', mesh: 'Cube', p: [0, -0.5, 0], s: [60, 1, 60], material: 'M_Floor' }));
  acts.push(newActor('DirectionalLight', { name: 'Sun', p: [0, 12, 0], r: [-50, 35, 0] }));
}
function template(kind, name) {
  const acts = [], scripts = {}, mats = clone(MATS);
  floorAndSun(acts);
  let gameMode = 'Spectator';
  if (kind === 'Blank') {
    acts.push(newActor('PlayerStart', { name: 'PlayerStart', p: [0, 1, 8] }));
  }
  if (kind === 'FirstPerson' || kind === 'ThirdPerson') {
    gameMode = kind;
    acts.push(newActor('PlayerStart', { name: 'PlayerStart', p: [0, 1, 10] }));
    for (const [x, z, sx, sz] of [[0, -15, 30, 1], [0, 15, 30, 1], [-15, 0, 1, 30], [15, 0, 1, 30]]) acts.push(newActor('StaticMesh', { name: 'Wall', mesh: 'Cube', p: [x, 1.5, z], s: [sx, 3, sz], material: 'M_Wall' }));
    acts.push(newActor('StaticMesh', { name: 'Ramp', mesh: 'Wedge', p: [-7, 1, -4], s: [4, 2, 4], r: [0, 90, 0], material: 'M_Wall' }));
    acts.push(newActor('StaticMesh', { name: 'Platform', mesh: 'Cube', p: [-7, 1, -9], s: [4, 2, 6], material: 'M_Wall' }));
    for (let i = 0; i < 4; i++) acts.push(newActor('StaticMesh', { name: 'Step', mesh: 'Cube', p: [7, 0.25 + i * 0.5, -2 - i], s: [3, 0.5 + i, 1], material: 'M_Wall' }));
    for (let i = 0; i < 6; i++) { const c = newActor('StaticMesh', { name: 'PhysicsCube', mesh: 'Cube', p: [-2 + (i % 3) * 1.1, 0.5 + Math.floor(i / 3) * 1.05, -2], material: 'M_Accent' }); c.physics.simulate = true; c.physics.mass = 5; acts.push(c); }
    const sphere = newActor('StaticMesh', { name: 'PhysicsBall', mesh: 'Sphere', p: [3, 3, 0], s: [1.2, 1.2, 1.2], material: 'M_Red' }); sphere.physics.simulate = true; acts.push(sphere);
    scripts.SC_Pickup = { name: 'SC_Pickup', code: SC_PICKUP }; scripts.SC_Rotator = { name: 'SC_Rotator', code: SC_ROTATOR }; scripts.SC_Goal = { name: 'SC_Goal', code: SC_GOAL }; scripts.SC_JumpPad = { name: 'SC_JumpPad', code: SC_LAUNCH };
    for (const [x, y, z] of [[-7, 2.6, -9], [7, 2.6, -5], [0, 1, -8], [10, 1, 8], [-10, 1, 8]]) { const c = newActor('StaticMesh', { name: 'Coin', mesh: 'Torus', p: [x, y, z], s: [0.8, 0.8, 0.8], material: 'M_Gold', script: 'SC_Pickup' }); c.physics.collision = false; acts.push(c); }
    const pad = newActor('StaticMesh', { name: 'JumpPad', mesh: 'Cylinder', p: [4, 0.05, 6], s: [2, 0.1, 2], material: 'M_Accent', script: 'SC_JumpPad' }); acts.push(pad);
    acts.push(newActor('TriggerBox', { name: 'GoalTrigger', p: [-7, 3, -9], s: [4, 2, 6], script: 'SC_Goal' }));
    acts.push(newActor('TextRender', { name: 'Welcome', p: [0, 2.6, -14.4], text: name || 'Premium Engine' }));
    acts.push(newActor('PointLight', { name: 'PointLight', p: [7, 3, -5] }));
  }
  const level = { actors: acts, world: { gameMode, sky: '#8fb7e0', skyLight: '#dfeaff', groundLight: '#3a3f4a', ambient: 0.8, exposure: 1, gravity: -9.81, killZ: -30, fog: false, fogColor: '#8fb7e0', fogDensity: 0.01, fov: 75 } };
  return { id: uid('p'), name: name || 'MyProject', template: kind, engine: ENGINE_VERSION, created: Date.now(), updated: Date.now(), description: '',
    startLevel: 'Main', levels: { Main: level }, assets: { materials: mats, scripts } };
}
function listProjects() { return store.get('pge:projects', []); }
function saveProject(quiet) {
  const p = S.project; p.updated = Date.now();
  if (!store.set('pge:p:' + p.id, p)) { log('error: Could not save: browser storage is full. Use File > Export Project.', 'error'); return false; }
  const list = listProjects().filter(x => x.id !== p.id); list.unshift({ id: p.id, name: p.name, template: p.template, updated: p.updated, actors: Object.values(p.levels).reduce((n, l) => n + l.actors.length, 0) });
  store.set('pge:projects', list.slice(0, 50)); store.set('pge:last', p.id);
  S.dirty = false; status(); if (!quiet) { log('Saved project ' + p.name, 'ok'); toast('Saved'); }
  return true;
}
function openProject(p, level) {
  if (S.pie) stopPlay();
  S.project = p; S.level = level && p.levels[level] ? level : (p.startLevel in p.levels ? p.startLevel : Object.keys(p.levels)[0]);
  S.sel = null; S.undo = []; S.redo = []; S.dirty = false; S.cbFolder = 'Content';
  p.assets.materials = p.assets.materials || {}; p.assets.scripts = p.assets.scripts || {}; if (!p.assets.materials.M_Default) p.assets.materials.M_Default = { ...DEFAULT_MATERIAL };
  store.set('pge:last', p.id); $('#projName').textContent = p.name + ' · ' + S.level; document.title = p.name + ' - Premium Engine';
  rebuild(); renderAll(); frameAll(); log(`Opened project ${p.name} (level ${S.level})`);
}

/* =================================================================== undo */
function snapshot() { return JSON.stringify({ level: S.level, levels: S.project.levels, assets: S.project.assets, sel: S.sel }); }
function pushUndo() { S.undo.push(snapshot()); if (S.undo.length > 100) S.undo.shift(); S.redo = []; }
function restore(json) { const o = JSON.parse(json); S.project.levels = o.levels; S.project.assets = o.assets; S.level = o.level in o.levels ? o.level : S.level; S.sel = o.sel && actor(o.sel) ? o.sel : null; dirty(); rebuild(); renderAll(); }
function undo() { if (!S.undo.length) return toast('Nothing to undo'); S.redo.push(snapshot()); restore(S.undo.pop()); toast('Undo'); }
function redo() { if (!S.redo.length) return toast('Nothing to redo'); S.undo.push(snapshot()); restore(S.redo.pop()); toast('Redo'); }
function dirty() { S.dirty = true; status(); }
function status() { const s = $('#saveState'); s.textContent = S.dirty ? '● Unsaved changes' : 'All Saved'; s.classList.toggle('dirty', S.dirty); }
function edit(fn, opts = {}) { pushUndo(); fn(); dirty(); if (opts.rebuild !== false) rebuild(); if (opts.ui !== false) renderAll(opts); }

/* =================================================================== 3D viewport */
const canvas = $('#viewport');
const renderer = makeRenderer(canvas);
const scene = new THREE.Scene();
const levelRoot = new THREE.Group(); levelRoot.name = '__level'; scene.add(levelRoot);
const persp = new THREE.PerspectiveCamera(60, 1, 0.05, 5000);
const ortho = new THREE.OrthographicCamera(-10, 10, 10, -10, -1000, 1000);
let cam = persp, orthoSize = 20;
const grid = new THREE.Group(); grid.userData.editorOnly = true; scene.add(grid);
{ const g1 = new THREE.GridHelper(100, 100, 0x3b3b40, 0x2a2a2e); g1.material.transparent = true; g1.material.opacity = 0.28; const g2 = new THREE.GridHelper(100, 10, 0x55555c, 0x55555c); g2.material.transparent = true; g2.material.opacity = 0.5; g1.position.y = g2.position.y = 0.003; grid.add(g1, g2);
  const ax = new THREE.AxesHelper(1.2); ax.position.y = 0.002; grid.add(ax); }
const selBox = new THREE.BoxHelper(undefined, 0xffa726); selBox.visible = false; selBox.material.depthTest = false; selBox.material.transparent = true; scene.add(selBox);
const tc = new TransformControls(cam, canvas); tc.setSize(0.85); scene.add(tc);
let objects = {};
const camState = { yaw: -0.6, pitch: -0.35, pos: new THREE.Vector3(12, 8, 14), focus: 10 };
function applyCam() {
  if (cam === persp) { persp.position.copy(camState.pos); persp.rotation.set(camState.pitch, camState.yaw, 0, 'YXZ'); }
}
function setView(v) {
  S.view = v; $('#vpCamLabel').textContent = v;
  if (v === 'Perspective') { cam = persp; applyCam(); }
  else { cam = ortho; const d = 200; ortho.position.set(v === 'Right' ? d : 0, v === 'Top' ? d : 0, v === 'Front' ? d : 0); ortho.up.set(0, v === 'Top' ? 0 : 1, v === 'Top' ? -1 : 0); ortho.lookAt(0, 0, 0); }
  tc.camera = cam; resize();
}
function resize() {
  const w = canvas.clientWidth, h = canvas.clientHeight; if (!w || !h) return;
  renderer.setSize(w, h, false); persp.aspect = w / h; persp.updateProjectionMatrix();
  const a = w / h; Object.assign(ortho, { left: -orthoSize * a / 2, right: orthoSize * a / 2, top: orthoSize / 2, bottom: -orthoSize / 2 }); ortho.updateProjectionMatrix();
}
new ResizeObserver(resize).observe(canvas);
function rebuild() {
  if (!S.project) return;
  tc.detach(); levelRoot.clear();
  applyWorld(scene, renderer, L().world);
  objects = buildLevel(S.project, L(), levelRoot, true);
  levelRoot.traverse(o => { if (o.userData.editorOnly) o.visible = S.show.icons; });
  scene.fog = null;   // fog only in game, so editing stays clear
  attachSel(); viewMode();
}
function attachSel() {
  const o = S.sel && objects[S.sel];
  if (o && !S.pie) { tc.attach(o); selBox.setFromObject(o); selBox.visible = true; } else { tc.detach(); selBox.visible = false; }
}
function viewMode() {
  const m = S.mode; $('#vpModeLabel').textContent = m;
  scene.overrideMaterial = m === 'Wireframe' ? new THREE.MeshBasicMaterial({ color: 0x9ad0ff, wireframe: true }) : m === 'Normals' ? new THREE.MeshNormalMaterial() : m === 'Unlit' ? null : null;
  levelRoot.traverse(o => { if (o.isLight) o.visible = m !== 'Unlit'; });
  const amb = scene.getObjectByName('__ambient'); if (amb) amb.intensity = m === 'Unlit' ? 3 : (L().world.ambient ?? 0.8);
}
function applySnap() { tc.setTranslationSnap(S.snap.t ? S.snap.tv : null); tc.setRotationSnap(S.snap.r ? S.snap.rv * DEG : null); tc.setScaleSnap(S.snap.s ? S.snap.sv : null); }
applySnap();
let dragStart = false;
tc.addEventListener('dragging-changed', e => { if (e.value) { pushUndo(); dragStart = true; } else if (dragStart) { dragStart = false; dirty(); renderOutliner(); } });
tc.addEventListener('objectChange', () => {
  const o = tc.object, a = o && actor(o.userData.actorId); if (!a) return;
  a.p = [r4(o.position.x), r4(o.position.y), r4(o.position.z)];
  const e = new THREE.Euler().setFromQuaternion(o.quaternion, 'YXZ'); a.r = [r4(e.x / DEG), r4(e.y / DEG), r4(e.z / DEG)];
  a.s = [r4(o.scale.x), r4(o.scale.y), r4(o.scale.z)];
  updateTransformFields(a);
});
// render loop
let fps = 0, frames = 0, fpsT = performance.now(), lastT = performance.now();
function loop() {
  requestAnimationFrame(loop);
  const now = performance.now(), dt = Math.min(0.1, (now - lastT) / 1000); lastT = now;
  frames++; if (now - fpsT > 500) { fps = Math.round(frames * 1000 / (now - fpsT)); frames = 0; fpsT = now; }
  if (S.pie) return;
  flyStep(dt);
  if (selBox.visible && tc.object) selBox.setFromObject(tc.object);
  grid.visible = S.show.grid;
  renderer.render(scene, cam);
  if (S.project) $('#vpInfo').textContent = `${S.project.name} · ${S.level}  |  ${L().actors.length} actors${S.show.stats ? `  |  ${fps} FPS  |  ${renderer.info.render.triangles} tris  |  ${renderer.info.render.calls} draws` : ''}`;
}
requestAnimationFrame(loop);

/* -------------------------------------------------- editor camera: RMB look + WASD, MMB pan, Alt+LMB orbit, wheel */
const held = new Set(); let rmb = false, mmb = false, orbit = false, downAt = null, moved = 0;
canvas.addEventListener('contextmenu', e => e.preventDefault());
canvas.addEventListener('mousedown', e => {
  canvas.focus(); downAt = { x: e.clientX, y: e.clientY }; moved = 0;
  if (e.button === 2) { rmb = true; canvas.requestPointerLock && cam === persp && canvas.requestPointerLock(); }
  if (e.button === 1) { mmb = true; e.preventDefault(); }
  if (e.button === 0 && e.altKey && cam === persp) orbit = true;
});
addEventListener('mouseup', e => {
  if (e.button === 2) { rmb = false; if (document.pointerLockElement === canvas) document.exitPointerLock(); }
  if (e.button === 1) mmb = false;
  if (e.button === 0) { if (orbit) { orbit = false; return; } if (downAt && moved < 4 && e.target === canvas && !tc.dragging) pick(e); }
  downAt = null;
});
addEventListener('mousemove', e => {
  if (!downAt && !rmb) return; moved += Math.abs(e.movementX) + Math.abs(e.movementY);
  const sens = 0.0028 * (S.prefs.sensitivity || 1);
  if (rmb && cam === persp) { camState.yaw -= e.movementX * sens; camState.pitch = Math.max(-1.55, Math.min(1.55, camState.pitch - e.movementY * sens)); applyCam(); }
  else if (rmb && cam === ortho) panBy(e.movementX, e.movementY);
  else if (mmb) panBy(e.movementX, e.movementY);
  else if (orbit) { const f = focusPoint(); camState.yaw -= e.movementX * sens; camState.pitch = Math.max(-1.55, Math.min(1.55, camState.pitch - e.movementY * sens));
    const dir = new THREE.Vector3(0, 0, -1).applyEuler(new THREE.Euler(camState.pitch, camState.yaw, 0, 'YXZ')); camState.pos.copy(f).addScaledVector(dir, -camState.focus); applyCam(); }
});
function focusPoint() { const dir = new THREE.Vector3(0, 0, -1).applyEuler(new THREE.Euler(camState.pitch, camState.yaw, 0, 'YXZ')); return camState.pos.clone().addScaledVector(dir, camState.focus); }
function panBy(dx, dy) {
  if (cam === ortho) { const k = orthoSize / canvas.clientHeight; const r = new THREE.Vector3(1, 0, 0).applyQuaternion(ortho.quaternion), u = new THREE.Vector3(0, 1, 0).applyQuaternion(ortho.quaternion); ortho.position.addScaledVector(r, -dx * k).addScaledVector(u, dy * k); return; }
  const k = 0.002 * Math.max(2, camState.focus) * (S.camSpeed / 4); const q = new THREE.Quaternion().setFromEuler(new THREE.Euler(camState.pitch, camState.yaw, 0, 'YXZ'));
  camState.pos.addScaledVector(new THREE.Vector3(1, 0, 0).applyQuaternion(q), -dx * k).addScaledVector(new THREE.Vector3(0, 1, 0).applyQuaternion(q), dy * k); applyCam();
}
canvas.addEventListener('wheel', e => {
  e.preventDefault();
  if (cam === ortho) { orthoSize = Math.max(2, Math.min(400, orthoSize * (e.deltaY > 0 ? 1.12 : 0.89))); resize(); return; }
  if (rmb) { setCamSpeed(Math.max(1, Math.min(8, S.camSpeed + (e.deltaY < 0 ? 1 : -1)))); return; }
  const dir = new THREE.Vector3(0, 0, -1).applyEuler(new THREE.Euler(camState.pitch, camState.yaw, 0, 'YXZ'));
  const d = (e.deltaY < 0 ? 1 : -1) * Math.max(0.5, camState.focus * 0.12); camState.pos.addScaledVector(dir, d); camState.focus = Math.max(1, camState.focus - d); applyCam();
}, { passive: false });
canvas.addEventListener('keydown', e => { if (rmb) { held.add(e.code); e.preventDefault(); e.stopPropagation(); } }, true);
addEventListener('keyup', e => held.delete(e.code));
addEventListener('blur', () => { held.clear(); rmb = false; });
function flyStep(dt) {
  if (!rmb || cam !== persp || !held.size) return;
  const sp = [0.5, 1, 2, 4, 8, 16, 32, 64][S.camSpeed - 1] * (held.has('ShiftLeft') ? 3 : 1);
  const q = new THREE.Quaternion().setFromEuler(new THREE.Euler(camState.pitch, camState.yaw, 0, 'YXZ')), v = new THREE.Vector3();
  if (held.has('KeyW')) v.z -= 1; if (held.has('KeyS')) v.z += 1; if (held.has('KeyA')) v.x -= 1; if (held.has('KeyD')) v.x += 1;
  v.applyQuaternion(q); if (held.has('KeyE')) v.y += 1; if (held.has('KeyQ')) v.y -= 1;
  if (v.lengthSq()) camState.pos.addScaledVector(v.normalize(), sp * dt); applyCam();
}
function setCamSpeed(n) { S.camSpeed = n; $('#camSpeedBtn').textContent = '🎥 ' + n; }
const ray = new THREE.Raycaster();
function rayAt(e) { const r = canvas.getBoundingClientRect(); ray.setFromCamera(new THREE.Vector2(((e.clientX - r.left) / r.width) * 2 - 1, -((e.clientY - r.top) / r.height) * 2 + 1), cam); return ray; }
function hitActor(e) {
  const hits = rayAt(e).intersectObjects(levelRoot.children, true).filter(h => h.object.visible && !(h.object.isLine && !h.object.parent?.userData?.editorOnly && false));
  for (const h of hits) { let o = h.object; while (o && !o.userData.actorId) o = o.parent; if (o) return { id: o.userData.actorId, point: h.point }; }
  return null;
}
function pick(e) { const h = hitActor(e); select(h ? h.id : null); }
function groundPoint(e) { const h = hitActor(e); if (h) return h.point; const p = new THREE.Vector3(); rayAt(e).ray.intersectPlane(new THREE.Plane(new THREE.Vector3(0, 1, 0), 0), p); return p.lengthSq() ? p : focusPoint(); }
function frameAll() {
  const box = new THREE.Box3(); levelRoot.children.forEach(o => { if (o.isMesh) box.expandByObject(o); });
  if (box.isEmpty()) box.set(new THREE.Vector3(-5, 0, -5), new THREE.Vector3(5, 3, 5));
  const s = box.getBoundingSphere(new THREE.Sphere()); const r = Math.min(s.radius, 30);
  camState.yaw = -0.7; camState.pitch = -0.42; camState.focus = r * 1.6; const dir = new THREE.Vector3(0, 0, -1).applyEuler(new THREE.Euler(camState.pitch, camState.yaw, 0, 'YXZ'));
  camState.pos.copy(s.center).addScaledVector(dir, -camState.focus); applyCam();
}
function focusSel() {
  const o = S.sel && objects[S.sel]; if (!o) return frameAll();
  const s = new THREE.Box3().setFromObject(o).getBoundingSphere(new THREE.Sphere()); const r = Math.max(0.6, s.radius || 0.6);
  camState.focus = r * 3; const dir = new THREE.Vector3(0, 0, -1).applyEuler(new THREE.Euler(camState.pitch, camState.yaw, 0, 'YXZ')); camState.pos.copy(s.center).addScaledVector(dir, -camState.focus); applyCam();
}
// drag & drop from the Content Drawer / Place Actors
canvas.addEventListener('dragover', e => { if (e.dataTransfer.types.includes('pge/asset')) { e.preventDefault(); e.dataTransfer.dropEffect = 'copy'; } });
canvas.addEventListener('drop', e => {
  e.preventDefault(); let d; try { d = JSON.parse(e.dataTransfer.getData('pge/asset')); } catch (er) { return; }
  const p = groundPoint(e), h = hitActor(e);
  if (d.kind === 'shape' || d.kind === 'actor') { spawn(d.kind === 'shape' ? 'StaticMesh' : d.id, d.kind === 'shape' ? { mesh: d.id } : {}, p); }
  else if (d.kind === 'material') { if (!h) return toast('Drop a material on a mesh'); const a = actor(h.id); if (a.type !== 'StaticMesh') return toast('Materials go on Static Mesh actors'); edit(() => a.material = d.id); select(a.id); log(`Applied ${d.id} to ${a.name}`); }
  else if (d.kind === 'script') { if (!h) return toast('Drop a script on an actor'); const a = actor(h.id); edit(() => a.script = d.id); select(a.id); log(`Attached ${d.id} to ${a.name}`); }
});

/* =================================================================== actor operations */
function select(id) { S.sel = id && actor(id) ? id : null; attachSel(); renderOutliner(); renderDetails(); }
function spawn(type, o = {}, at) {
  const a = newActor(type, o); a.name = uniqueName(o.name || (type === 'StaticMesh' ? (o.mesh || 'Cube') : ACTOR_TYPES[type].label.replace(/\s/g, '')));
  if (at) { a.p = [r4(S.snap.t ? Math.round(at.x / S.snap.tv) * S.snap.tv : at.x), r4(at.y + (type === 'StaticMesh' ? 0.5 : type === 'PlayerStart' ? 1 : type.includes('Light') ? 2 : 0.5)), r4(S.snap.t ? Math.round(at.z / S.snap.tv) * S.snap.tv : at.z)]; }
  else { const f = focusPoint(); a.p = [r4(f.x), r4(Math.max(0.5, f.y)), r4(f.z)]; }
  if (type === 'DirectionalLight') a.r = [-50, 35, 0];
  edit(() => L().actors.push(a), { rebuild: true }); select(a.id); log(`Placed ${a.name}`);
  return a;
}
function uniqueName(base) { const names = new Set(L().actors.map(a => a.name)); if (!names.has(base)) return base; let i = 2; while (names.has(base + i)) i++; return base + i; }
function descendants(id) { const out = []; const walk = p => L().actors.filter(a => a.parent === p).forEach(a => { out.push(a.id); walk(a.id); }); walk(id); return out; }
function del() { if (!S.sel) return; const a = actor(S.sel), ids = [S.sel, ...descendants(S.sel)]; edit(() => { L().actors = L().actors.filter(x => !ids.includes(x.id)); }); S.sel = null; attachSel(); renderAll(); log(`Deleted ${a.name}${ids.length > 1 ? ` and ${ids.length - 1} attached actor(s)` : ''}`); }
function copySel() { if (!S.sel) return; const ids = [S.sel, ...descendants(S.sel)]; S.clip = L().actors.filter(a => ids.includes(a.id)).map(clone); toast('Copied ' + actor(S.sel).name); }
function paste(offset = true) {
  if (!S.clip) return toast('Clipboard is empty');
  const map = {}; const copies = S.clip.map(a => { const c = clone(a); map[a.id] = c.id = uid('a'); return c; });
  for (const c of copies) { c.parent = map[c.parent] || (copies.some(x => x.id === c.parent) ? c.parent : (L().actors.some(x => x.id === c.parent) ? c.parent : null)); }
  copies[0].name = uniqueName(copies[0].name.replace(/\d+$/, '')); if (offset) copies[0].p = [r4(copies[0].p[0] + (S.snap.t ? S.snap.tv * 2 : 1)), copies[0].p[1], copies[0].p[2]];
  edit(() => L().actors.push(...copies)); select(copies[0].id); log('Pasted ' + copies[0].name);
}
function duplicate() { if (!S.sel) return; const keep = S.clip; copySel(); paste(true); S.clip = keep; }
function snapToFloor() {
  const a = S.sel && actor(S.sel), o = a && objects[a.id]; if (!o) return;
  const box = new THREE.Box3().setFromObject(o), others = levelRoot.children.filter(x => x !== o && x.isMesh);
  const rc = new THREE.Raycaster(new THREE.Vector3((box.min.x + box.max.x) / 2, box.min.y - 0.001, (box.min.z + box.max.z) / 2), new THREE.Vector3(0, -1, 0));
  const h = rc.intersectObjects(others, true)[0]; const floor = h ? h.point.y : 0;
  edit(() => { a.p[1] = r4(a.p[1] - (box.min.y - floor)); }); log(`Snapped ${a.name} to the floor`);
}
function attachTo(childId, parentId) {
  const c = actor(childId); if (!c || childId === parentId || descendants(childId).includes(parentId)) return toast("Can't attach there");
  const co = objects[childId], po = parentId ? objects[parentId] : levelRoot;
  edit(() => {
    const m = co.matrixWorld.clone(); const pm = po.matrixWorld.clone().invert(); m.premultiply(pm);
    const pos = new THREE.Vector3(), q = new THREE.Quaternion(), s = new THREE.Vector3(); m.decompose(pos, q, s); const e = new THREE.Euler().setFromQuaternion(q, 'YXZ');
    c.parent = parentId || null; c.p = [r4(pos.x), r4(pos.y), r4(pos.z)]; c.r = [r4(e.x / DEG), r4(e.y / DEG), r4(e.z / DEG)]; c.s = [r4(s.x), r4(s.y), r4(s.z)];
  }); log(parentId ? `Attached ${c.name} to ${actor(parentId).name}` : `Detached ${c.name}`);
}

/* =================================================================== menus */
const SC = navigator.platform.includes('Mac') ? '⌘' : 'Ctrl+';
const MENUS = {
  File: () => [
    ['sec', 'Open'], ['New Level...', 'newLevel', SC + 'N', '🗋'], ['Open Level...', 'openLevel', SC + 'O', '🗁'],
    ['sec', 'Save'], ['Save Current Level', 'saveLevel', SC + 'S', '💾'], ['Save All', 'saveAll', SC + 'Shift+S', '💾'],
    ['sec', 'Import / Export'], ['Import Into Level...', 'importLevel', '', '⤓'], ['Export Project...', 'exportProject', '', '⤒'],
    ['sec', 'Project'], ['New Project...', 'newProject', '', '✚'], ['Open Project...', 'openProject', '', '🗁'], ['Package Project', 'package', '', '📦'],
    ['hr'], ['Exit', 'exit', '', '⏏']],
  Edit: () => [
    ['sec', 'History'], ['Undo', 'undo', SC + 'Z', '↶', !S.undo.length], ['Redo', 'redo', SC + 'Y', '↷', !S.redo.length],
    ['sec', 'Edit'], ['Cut', 'cut', SC + 'X', '✂', !S.sel], ['Copy', 'copy', SC + 'C', '⎘', !S.sel], ['Paste', 'paste', SC + 'V', '📋', !S.clip], ['Duplicate', 'duplicate', SC + 'D', '⧉', !S.sel], ['Delete', 'delete', 'Delete', '🗑', !S.sel],
    ['sec', 'Configuration'], ['Editor Preferences...', 'prefs', '', '⚙'], ['Project Settings...', 'projectSettings', '', '⚙']],
  Window: () => [
    ['sec', 'Level Editor'], ['Place Actors', 'win:placeActors', '', '', false, !$('#placeActors').classList.contains('hidden')], ['Outliner', 'win:outliner', '', '', false, !$('#outliner').classList.contains('hidden')],
    ['Details', 'win:details', '', '', false, !$('#details').classList.contains('hidden')], ['World Settings', 'worldSettings', '', '🌐'],
    ['sec', 'General'], ['Content Drawer', 'toggleDrawer', SC + 'Space', '🗀'], ['Output Log', 'showLog', '', '≣'],
    ['sec', 'Layout'], ['Load Layout: Default', 'resetLayout', '', '▦']],
  Tools: () => [
    ['sec', 'Scripting'], ['New Script...', 'newScript', '', '{ }'], ['Find in Scripts...', 'findScripts', '', '🔍'], ['Script API Reference', 'apiRef', '', '📘'],
    ['sec', 'Debug'], ['Statistics', 'stats', '', '📊'], ['Toggle FPS Counter', 'toggleStats', '', '⏱']],
  Build: () => [
    ['sec', 'Build'], ['Build All Levels', 'buildAll', '', '⚒'], ['Compile Scripts', 'compileAll', 'F7', '{ }'],
    ['sec', 'Verification'], ['Map Check', 'mapCheck', '', '✔']],
  Select: () => [
    ['sec', 'Selection'], ['Select All Actors', 'selectAll', SC + 'A', ''], ['Unselect All', 'deselect', 'Esc', '', !S.sel], ['Select Parent', 'selectParent', '', '', !(S.sel && actor(S.sel).parent)],
    ['sec', 'By Type'], ['Select Next: Same Mesh', 'selSameMesh', '', '', !(S.sel && actor(S.sel).mesh)], ['Select Next: Same Material', 'selSameMat', '', '', !(S.sel && actor(S.sel).material)], ['Select Next: Light', 'selLight', '', '']],
  Actor: () => [
    ['sec', 'Transform'], ['Snap to Floor', 'snapFloor', 'End', '⤓', !S.sel], ['Reset Location', 'resetP', '', '', !S.sel], ['Reset Rotation', 'resetR', '', '', !S.sel], ['Reset Scale', 'resetS', '', '', !S.sel],
    ['sec', 'Attachment'], ['Detach', 'detach', '', '', !(S.sel && actor(S.sel).parent)],
    ['sec', 'Visibility'], ['Hide Selected', 'hide', 'H', '', !S.sel], ['Show All', 'showAll', SC + 'H', ''],
    ['sec', 'View'], ['Focus Selected', 'focus', 'F', '◎', !S.sel], ['Pilot Camera To Selection', 'focus', '', '', !S.sel]],
  Help: () => [['sec', 'Help'], ['Documentation', 'docs', 'F1', '📘'], ['Keyboard Shortcuts', 'shortcuts', '', '⌨'], ['Script API Reference', 'apiRef', '', '{ }'], ['hr'], ['About Premium Engine', 'about', '', 'ℹ']]
};
const POPS = {
  mode: () => [['sec', 'Modes'], ['Selection', 'noop', 'Shift+1', '⬚', false, true]],
  quickAdd: () => [['sec', 'Place Actor'], ...Object.entries({ 'Empty Actor': 'Empty', 'Player Start': 'PlayerStart', 'Trigger Box': 'TriggerBox', 'Camera Actor': 'CameraActor', 'Text Render': 'TextRender' }).map(([n, t]) => [n, 'add:' + t, '', ACTOR_TYPES[t].icon]),
    ['sec', 'Shapes'], ...MESHES.map(m => [m, 'shape:' + m, '', '▣']), ['sec', 'Lights'], ...['DirectionalLight', 'PointLight', 'SpotLight'].map(t => [ACTOR_TYPES[t].label, 'add:' + t, '', ACTOR_TYPES[t].icon]),
    ['sec', 'Content'], ['New Material', 'newMaterial', '', '◍'], ['New Script', 'newScript', '', '{ }'], ['New Level', 'newLevel', '', '🗋']],
  scripts: () => [['sec', 'Scripts'], ['New Script...', 'newScript', '', '✚'], ['Open Level Script', 'levelScript', '', '{ }'], ...(S.sel ? [['Open Script of Selected Actor', 'editSelScript', '', '✎', !actor(S.sel).script]] : []), ['sec', 'Project Scripts'], ...Object.entries(S.project.assets.scripts).map(([id, s]) => [s.name, 'openScript:' + id, '', '{ }'])],
  playOpts: () => [['sec', 'Play'], ['Selected Viewport', 'play', 'Alt+P', '▶'], ['New Window (packaged game)', 'playWindow', '', '⧉'], ['Simulate', 'simulate', 'Alt+S', '⏵⏵'], ['sec', 'Game Mode'],
    ...[['First Person', 'FirstPerson'], ['Third Person', 'ThirdPerson'], ['Spectator', 'Spectator']].map(([n, m]) => [n, 'gm:' + m, '', '', false, (L().world.gameMode || 'FirstPerson') === m])],
  platforms: () => [['sec', 'Package'], ['Web (single HTML file)', 'package', '', '🌐'], ['Run Packaged Game In New Window', 'playWindow', '', '⧉'], ['sec', 'Settings'], ['Packaging Settings...', 'projectSettings', '', '⚙']],
  settings: () => [['sec', 'Settings'], ['Project Settings...', 'projectSettings', '', '⚙'], ['World Settings', 'worldSettings', '', '🌐'], ['Editor Preferences...', 'prefs', '', '⚙']],
  vpView: () => [['sec', 'Viewport'], ['Frame All', 'frameAll', 'Home', '▣'], ['Focus Selected', 'focus', 'F', '◎', !S.sel], ['Take Screenshot', 'screenshot', 'F9', '📷'], ['sec', 'Game View'], ['Toggle Editor Icons', 'toggleIcons', 'G', '']],
  vpCam: () => [['sec', 'Perspective'], ['Perspective', 'view:Perspective', '', '', false, S.view === 'Perspective'], ['sec', 'Orthographic'], ...['Top', 'Front', 'Right'].map(v => [v, 'view:' + v, '', '', false, S.view === v])],
  vpMode: () => [['sec', 'View Mode'], ...['Lit', 'Unlit', 'Wireframe', 'Normals'].map(m => [m, 'mode:' + m, '', '', false, S.mode === m])],
  vpShow: () => [['sec', 'Show'], ['Grid', 'show:grid', '', '', false, S.show.grid], ['Editor Icons', 'show:icons', '', '', false, S.show.icons], ['Stats', 'show:stats', '', '', false, S.show.stats]],
  snapT: () => [0.01, 0.1, 0.25, 0.5, 1, 2, 5, 10].map(v => [String(v), 'snapT:' + v, '', '', false, S.snap.tv === v]),
  snapR: () => [5, 10, 15, 30, 45, 90].map(v => [v + '°', 'snapR:' + v, '', '', false, S.snap.rv === v]),
  snapS: () => [0.1, 0.25, 0.5, 1].map(v => [String(v), 'snapS:' + v, '', '', false, S.snap.sv === v]),
  camSpeed: () => [1, 2, 3, 4, 5, 6, 7, 8].map(v => ['Speed ' + v, 'camSpeed:' + v, '', '', false, S.camSpeed === v]),
  olAdd: () => POPS.quickAdd().filter(x => x[1] && (x[1].startsWith('add:') || x[1].startsWith('shape:') || x[0] === 'sec'))
};
let popOwner = null;
function showPop(items, x, y, owner) {
  const p = $('#menuPop');
  p.innerHTML = items.map(it => {
    if (it[0] === 'sec') return `<div class="sec">${esc(it[1])}</div>`; if (it[0] === 'hr') return '<div class="hr"></div>';
    const [n, cmd, sc, ic, dis, chk] = it;
    return `<div class="it${dis ? ' dis' : ''}${chk ? ' chk' : ''}" data-c="${esc(cmd)}"><span class="ic">${chk ? '' : esc(ic || '')}</span>${esc(n)}${sc ? `<span class="sc">${esc(sc)}</span>` : ''}</div>`;
  }).join('');
  p.classList.remove('hidden'); const r = p.getBoundingClientRect();
  p.style.left = Math.min(x, innerWidth - r.width - 6) + 'px'; p.style.top = Math.min(y, innerHeight - r.height - 6) + 'px'; popOwner = owner;
  p.querySelectorAll('.it:not(.dis)').forEach(el => el.onclick = () => { hidePop(); runAll(el.dataset.c); });
}
function hidePop() { $('#menuPop').classList.add('hidden'); $$('.mbtn.open').forEach(b => b.classList.remove('open')); popOwner = null; }
addEventListener('mousedown', e => { if (!e.target.closest('#menuPop') && !e.target.closest('.mbtn') && !e.target.closest('.tbmenu')) hidePop(); });
$('#menus').innerHTML = Object.keys(MENUS).map(m => `<button class="mbtn" data-m="${m}">${m}</button>`).join('');
$$('.mbtn').forEach(b => {
  const open = () => { if (!S.project) return; $$('.mbtn.open').forEach(x => x.classList.remove('open')); b.classList.add('open'); const r = b.getBoundingClientRect(); showPop(MENUS[b.dataset.m](), r.left, r.bottom + 2, b); };
  b.onclick = () => popOwner === b ? hidePop() : open();
  b.onmouseenter = () => { if (popOwner && popOwner.classList && popOwner.classList.contains('mbtn') && popOwner !== b) open(); };
});
$$('.tbmenu').forEach(m => m.querySelector('button').onclick = () => { if (!S.project) return; if (popOwner === m) return hidePop(); const r = m.getBoundingClientRect(); showPop(POPS[m.dataset.menu](), r.left, r.bottom + 4, m); });
document.addEventListener('click', e => { const b = e.target.closest('[data-cmd]'); if (b && !b.disabled) run(b.dataset.cmd); });

/* =================================================================== commands */
function run(c) {
  const [k, v] = c.split(/:(.*)/s);
  const A = S.sel && actor(S.sel);
  switch (k) {
    case 'noop': return;
    case 'saveAll': case 'saveLevel': return saveProject();
    case 'newLevel': return newLevel(); case 'openLevel': return openLevelDlg();
    case 'importLevel': return importLevel(); case 'exportProject': return exportProject();
    case 'newProject': case 'openProject': return projectBrowser();
    case 'package': return packageProject(); case 'playWindow': return packageProject(true);
    case 'exit': if (S.dirty && !confirm('You have unsaved changes. Leave without saving?')) return; location.href = '../#library'; return;
    case 'undo': return undo(); case 'redo': return redo();
    case 'cut': copySel(); return del(); case 'copy': return copySel(); case 'paste': return paste(); case 'duplicate': return duplicate(); case 'delete': return del();
    case 'prefs': return prefsDlg(); case 'projectSettings': return projectSettingsDlg();
    case 'win': return togglePanel(v); case 'worldSettings': showPanel('details'); return setDTab('world');
    case 'toggleDrawer': return toggleDrawer(); case 'showLog': return showDrawer('log'); case 'clearLog': logBox().innerHTML = ''; return;
    case 'resetLayout': ['outliner', 'details', 'rightDock'].forEach(showPanel); $('#placeActors').classList.add('hidden'); $('#rightDock').style.width = ''; $('#outliner').style.height = ''; return toast('Default layout');
    case 'newScript': return newScript(); case 'findScripts': return findScripts(); case 'apiRef': return apiRef();
    case 'levelScript': { let id = Object.keys(S.project.assets.scripts).find(i => S.project.assets.scripts[i].name === 'SC_Level_' + S.level);
      if (!id) { id = 'SC_Level_' + S.level; edit(() => { S.project.assets.scripts[id] = { name: id, code: `// Level script for ${S.level}. Attach it to any actor (e.g. an Empty Actor) to run it.\nfunction onStart() {\n  log('Level ${S.level} started');\n}\n` }; const e = newActor('Empty', { name: 'LevelScript', script: id }); L().actors.push(e); }); }
      return openScript(id); }
    case 'editSelScript': return A && A.script && openScript(A.script);
    case 'openScript': return openScript(v);
    case 'stats': { const t = {}; L().actors.forEach(a => t[a.type] = (t[a.type] || 0) + 1); log('Statistics — ' + Object.entries(t).map(([k2, n]) => `${k2}: ${n}`).join(', ') + ` | triangles: ${renderer.info.render.triangles} | draw calls: ${renderer.info.render.calls}`); return showDrawer('log'); }
    case 'toggleStats': case 'show': { const key = v || 'stats'; S.show[key] = !S.show[key]; if (key === 'icons') rebuild(); return; }
    case 'toggleIcons': S.show.icons = !S.show.icons; S.show.grid = S.show.icons; rebuild(); return;
    case 'buildAll': return buildAll(); case 'compileAll': return compileAll(); case 'mapCheck': return mapCheck();
    case 'selectAll': { const first = L().actors.find(a => a.type === 'StaticMesh'); log(`Level has ${L().actors.length} actors (multi-selection comes in a later version)`); return first && select(first.id); }
    case 'deselect': return select(null);
    case 'selectParent': return A && A.parent && select(A.parent);
    case 'selSameMesh': case 'selSameMat': case 'selLight': { const list = L().actors.filter(a => k === 'selLight' ? a.type.includes('Light') : k === 'selSameMesh' ? a.mesh === A.mesh : a.material === A.material);
      if (!list.length) return; const i = list.findIndex(a => a.id === S.sel); return select(list[(i + 1) % list.length].id); }
    case 'snapFloor': return snapToFloor();
    case 'resetP': return A && edit(() => A.p = [0, 0, 0]); case 'resetR': return A && edit(() => A.r = [0, 0, 0]); case 'resetS': return A && edit(() => A.s = [1, 1, 1]);
    case 'detach': return A && attachTo(A.id, null);
    case 'hide': return A && edit(() => A.visible = false); case 'showAll': return edit(() => L().actors.forEach(a => a.visible = true));
    case 'focus': return focusSel(); case 'frameAll': return frameAll();
    case 'screenshot': { renderer.render(scene, cam); const a = document.createElement('a'); a.href = canvas.toDataURL('image/png'); a.download = `${S.project.name}_${S.level}.png`; a.click(); return log('Saved screenshot'); }
    case 'docs': return docs(); case 'shortcuts': return shortcuts(); case 'about': return about();
    case 'add': return spawn(v); case 'shape': return spawn('StaticMesh', { mesh: v });
    case 'newMaterial': return newMaterial();
    case 'play': return startPlay(false); case 'simulate': return startPlay(true); case 'stop': return stopPlay();
    case 'gm': return edit(() => L().world.gameMode = v, { rebuild: false });
    case 'view': return setView(v); case 'mode': S.mode = v; return viewMode();
    case 'snapT': S.snap.tv = +v; $('#snapT').textContent = v; return applySnap(); case 'snapR': S.snap.rv = +v; $('#snapR').textContent = v + '°'; return applySnap(); case 'snapS': S.snap.sv = +v; $('#snapS').textContent = v; return applySnap();
    case 'camSpeed': return setCamSpeed(+v);
  }
}
// viewport toolbar buttons
$$('[data-tool]').forEach(b => b.onclick = () => setTool(b.dataset.tool));
function setTool(t) { S.tool = t; tc.setMode(t); $$('[data-tool]').forEach(b => b.classList.toggle('on', b.dataset.tool === t)); }
$('#spaceBtn').onclick = () => { S.space = S.space === 'world' ? 'local' : 'world'; tc.setSpace(S.space); $('#spaceBtn').textContent = S.space === 'world' ? '🌐' : '⬚'; toast(S.space === 'world' ? 'World coordinates' : 'Local coordinates'); };
$$('[data-snap]').forEach(b => b.onclick = () => { const k = b.dataset.snap; S.snap[k] = !S.snap[k]; b.classList.toggle('on', S.snap[k]); applySnap(); });

/* =================================================================== panels */
function togglePanel(id) { const el = $('#' + id); el.classList.toggle('hidden'); if (id === 'placeActors') renderPlaceActors(); syncDock(); resize(); }
function showPanel(id) { $('#' + id).classList.remove('hidden'); syncDock(); }
function syncDock() { const d = $('#rightDock'); d.classList.toggle('hidden', $('#outliner').classList.contains('hidden') && $('#details').classList.contains('hidden')); $('.vsplit').classList.toggle('hidden', d.classList.contains('hidden')); }
$$('[data-close]').forEach(b => b.onclick = () => { $('#' + b.dataset.close).classList.add('hidden'); syncDock(); resize(); });
function toggleDrawer() { const d = $('#drawer'); if (d.classList.contains('hidden') || !$('#logBody').classList.contains('hidden')) showDrawer('content'); else d.classList.add('hidden'); resize(); }
function showDrawer(tab) { const d = $('#drawer'); d.classList.remove('hidden'); $$('[data-btab]').forEach(b => b.classList.toggle('on', b.dataset.btab === tab)); $('#cbBody').classList.toggle('hidden', tab !== 'content'); $('#logBody').classList.toggle('hidden', tab !== 'log'); if (tab === 'log') { $('[data-cmd=showLog]').style.color = ''; logBox().scrollTop = 1e9; } else renderContent(); resize(); }
$$('[data-btab]').forEach(b => b.onclick = () => showDrawer(b.dataset.btab));
$('#logFilter').oninput = () => { const f = $('#logFilter').value.trim().toLowerCase(); $$('#log .l').forEach(l => l.classList.toggle('hidden', !!f && !l.textContent.toLowerCase().includes(f))); };
// resizers
$$('[data-split]').forEach(sp => sp.onmousedown = e => {
  e.preventDefault(); const kind = sp.dataset.split, start = { x: e.clientX, y: e.clientY, w: $('#rightDock').offsetWidth, h: $('#outliner').offsetHeight };
  const mv = ev => { if (kind === 'right') $('#rightDock').style.width = Math.max(220, Math.min(innerWidth * 0.6, start.w - (ev.clientX - start.x))) + 'px'; else $('#outliner').style.height = Math.max(90, start.h + ev.clientY - start.y) + 'px'; resize(); };
  const up = () => { removeEventListener('mousemove', mv); removeEventListener('mouseup', up); }; addEventListener('mousemove', mv); addEventListener('mouseup', up);
});
function renderAll() { renderOutliner(); renderDetails(); if (!$('#drawer').classList.contains('hidden') && !$('#cbBody').classList.contains('hidden')) renderContent(); if (!$('#placeActors').classList.contains('hidden')) renderPlaceActors(); $('#projName').textContent = S.project.name + ' · ' + S.level; }

/* -------------------------------------------------- Outliner */
let renaming = null;
function renderOutliner() {
  if (!S.project) return;
  const q = $('#olSearch').value.trim().toLowerCase(), acts = L().actors, rows = [];
  const walk = (parent, depth) => acts.filter(a => (a.parent || null) === parent).sort((a, b) => a.name.localeCompare(b.name)).forEach(a => { rows.push([a, depth]); walk(a.id, depth + 1); });
  walk(null, 0); acts.filter(a => a.parent && !acts.some(p => p.id === a.parent)).forEach(a => rows.push([a, 0]));
  const shown = q ? rows.filter(([a]) => (a.name + ' ' + a.type).toLowerCase().includes(q)) : rows;
  $('#olBody').innerHTML = `<div class="olrow lvl" data-root="1"><span></span><span class="lbl"><i>🌐</i>${esc(S.level)} (Editor)</span><span class="ty">World</span></div>` +
    shown.map(([a, d]) => `<div class="olrow${a.id === S.sel ? ' sel' : ''}" data-id="${a.id}" draggable="true"><span class="eye${a.visible === false ? ' off' : ''}" data-eye="${a.id}">👁</span>
      <span class="lbl" style="padding-left:${(q ? 0 : d) * 14 + 10}px">${renaming === a.id ? `<input value="${esc(a.name)}" data-ren="${a.id}">` : `<i>${ACTOR_TYPES[a.type]?.icon || '◇'}</i>${esc(a.name)}`}</span><span class="ty">${esc(a.type === 'StaticMesh' ? 'StaticMeshActor' : a.type)}${a.script ? ' {}' : ''}</span></div>`).join('');
  $('#olFoot').textContent = `${acts.length} actors${S.sel ? ' (1 selected)' : ''}`;
  $$('#olBody .olrow[data-id]').forEach(r => {
    r.onclick = e => { if (e.target.dataset.eye || e.target.tagName === 'INPUT') return; select(r.dataset.id); };
    r.ondblclick = e => { if (e.target.dataset.eye) return; select(r.dataset.id); focusSel(); };
    r.ondragstart = e => { e.dataTransfer.setData('pge/actor', r.dataset.id); e.dataTransfer.effectAllowed = 'move'; };
    r.ondragover = e => { if (e.dataTransfer.types.includes('pge/actor') || e.dataTransfer.types.includes('pge/asset')) { e.preventDefault(); r.classList.add('drop'); } };
    r.ondragleave = () => r.classList.remove('drop');
    r.ondrop = e => { e.preventDefault(); r.classList.remove('drop'); const c = e.dataTransfer.getData('pge/actor'); if (c) return attachTo(c, r.dataset.id);
      let d; try { d = JSON.parse(e.dataTransfer.getData('pge/asset')); } catch (er) { return; } const a = actor(r.dataset.id);
      if (d.kind === 'material' && a.type === 'StaticMesh') { edit(() => a.material = d.id); select(a.id); } else if (d.kind === 'script') { edit(() => a.script = d.id); select(a.id); } };
  });
  const root = $('#olBody .olrow[data-root]'); root.ondragover = e => { if (e.dataTransfer.types.includes('pge/actor')) { e.preventDefault(); root.classList.add('drop'); } }; root.ondragleave = () => root.classList.remove('drop');
  root.ondrop = e => { e.preventDefault(); root.classList.remove('drop'); const c = e.dataTransfer.getData('pge/actor'); if (c) attachTo(c, null); }; root.onclick = () => select(null);
  $$('[data-eye]').forEach(b => b.onclick = () => { const a = actor(b.dataset.eye); edit(() => a.visible = a.visible === false); });
  const ri = $('[data-ren]'); if (ri) { ri.focus(); ri.select(); const done = ok => { const a = actor(ri.dataset.ren); renaming = null; if (ok && ri.value.trim() && ri.value.trim() !== a.name) edit(() => a.name = ri.value.trim().slice(0, 60)); else renderOutliner(); };
    ri.onkeydown = e => { e.stopPropagation(); if (e.key === 'Enter') done(true); if (e.key === 'Escape') done(false); }; ri.onblur = () => done(true); }
}
$('#olSearch').oninput = renderOutliner;

/* -------------------------------------------------- Details & World Settings */
let dtab = 'details';
function setDTab(t) { dtab = t; $$('[data-dtab]').forEach(b => b.classList.toggle('on', b.dataset.dtab === t)); renderDetails(); }
$$('[data-dtab]').forEach(b => b.onclick = () => setDTab(b.dataset.dtab));
const vecHTML = (k, v, step) => `<div class="vec" data-vec="${k}" data-step="${step}">${['x', 'y', 'z'].map((ax, i) => `<label class="f ${ax}"><i></i><input data-i="${i}" value="${r4(v[i])}"></label>`).join('')}</div>`;
const cat = (title, body, id) => `<div class="cat" data-cat="${id || title}"><div class="cath">${esc(title)}</div><div class="catb">${body}</div></div>`;
const prop = (label, html) => `<div class="prop"><label title="${esc(label)}">${esc(label)}</label><div>${html}</div></div>`;
const selectHTML = (key, val, opts) => `<select class="inp" data-k="${key}">${opts.map(([v, n]) => `<option value="${esc(v)}"${String(v) === String(val) ? ' selected' : ''}>${esc(n)}</option>`).join('')}</select>`;
const numHTML = (key, val, step = 0.1) => `<input class="inp" type="number" step="${step}" data-k="${key}" value="${val ?? ''}">`;
const chkHTML = (key, val) => `<input type="checkbox" class="chk" data-k="${key}"${val ? ' checked' : ''}>`;
const colorHTML = (key, val) => `<div class="color"><input type="color" data-k="${key}" value="${esc(val || '#ffffff')}"><input class="inp" data-k="${key}" value="${esc(val || '#ffffff')}" style="width:90px"></div>`;
const closedCats = new Set(store.get('pge:closedCats', []));
function renderDetails() {
  if (!S.project) return; const box = $('#dtBody');
  if (dtab === 'world') return renderWorld(box);
  const a = S.sel && actor(S.sel);
  if (!a) { box.innerHTML = '<div class="empty">Select an object to view details.</div>'; return; }
  const P = S.project, mats = Object.entries(P.assets.materials), scripts = Object.entries(P.assets.scripts);
  let h = `<div class="dname"><span class="ic">${ACTOR_TYPES[a.type]?.icon || '◇'}</span><input class="inp" data-k="name" value="${esc(a.name)}"></div>`;
  h += cat('Transform', prop('Location', vecHTML('p', a.p, 0.05)) + prop('Rotation', vecHTML('r', a.r, 1)) + prop('Scale', vecHTML('s', a.s, 0.02)));
  if (a.type === 'StaticMesh') {
    const m = P.assets.materials[a.material] || DEFAULT_MATERIAL;
    h += cat('Static Mesh', prop('Static Mesh', selectHTML('mesh', a.mesh, MESHES.map(x => [x, 'SM_' + x]))));
    h += cat('Materials', prop('Element 0', `<div class="btnrow">${selectHTML('material', a.material, mats.map(([id, mm]) => [id, mm.name]))}</div>`) + prop('Base Color', colorHTML('mat.color', m.color)) +
      prop('Metallic', `<div class="slider"><input type="range" min="0" max="1" step="0.01" data-k="mat.metalness" value="${m.metalness}"><input class="inp" data-k="mat.metalness" value="${m.metalness}"></div>`) +
      prop('Roughness', `<div class="slider"><input type="range" min="0" max="1" step="0.01" data-k="mat.roughness" value="${m.roughness}"><input class="inp" data-k="mat.roughness" value="${m.roughness}"></div>`) +
      prop('', `<div class="btnrow"><button class="btn" data-act="matEdit">Edit Material…</button><button class="btn" data-act="matNew">New Material</button></div>`));
    h += cat('Physics', prop('Simulate Physics', chkHTML('physics.simulate', a.physics.simulate)) + prop('Mass (kg)', numHTML('physics.mass', a.physics.mass, 0.1)) + prop('Collision Enabled', chkHTML('physics.collision', a.physics.collision !== false)));
  }
  if (a.light) {
    h += cat('Light', prop('Light Color', colorHTML('light.color', a.light.color)) + prop('Intensity', numHTML('light.intensity', a.light.intensity, a.type === 'DirectionalLight' ? 0.1 : 1)) +
      (a.type !== 'DirectionalLight' ? prop('Attenuation Radius', numHTML('light.range', a.light.range, 0.5)) : '') + (a.type === 'SpotLight' ? prop('Outer Cone Angle', numHTML('light.angle', a.light.angle, 1)) : '') + prop('Cast Shadows', chkHTML('light.shadows', a.light.shadows)));
  }
  if (a.text) h += cat('Text', prop('Text', `<input class="inp" data-k="text.value" value="${esc(a.text.value)}">`) + prop('Text Color', colorHTML('text.color', a.text.color)) + prop('World Size', numHTML('text.size', a.text.size, 0.05)));
  h += cat('Script', prop('Script', selectHTML('script', a.script || '', [['', 'None'], ...scripts.map(([id, s]) => [id, s.name])])) + prop('', `<div class="btnrow">${a.script ? '<button class="btn primary" data-act="scriptEdit">Open Script</button>' : ''}<button class="btn" data-act="scriptNew">New Script</button></div>`));
  h += cat('Rendering', prop('Visible', chkHTML('visible', a.visible !== false)));
  h += cat('Actor', prop('Tags', `<input class="inp" data-k="tags" value="${esc((a.tags || []).join(', '))}" placeholder="enemy, door">`) + prop('Attach Parent', selectHTML('parent', a.parent || '', [['', 'None'], ...L().actors.filter(x => x.id !== a.id && !descendants(a.id).includes(x.id)).map(x => [x.id, x.name])])) + prop('Actor ID', `<span style="color:var(--dim);font:11px var(--mono)">${a.id}</span>`));
  box.innerHTML = h; wireDetails(box, a);
}
function setPath(o, path, v) { const ks = path.split('.'); let t = o; for (let i = 0; i < ks.length - 1; i++) t = t[ks[i]] = t[ks[i]] || {}; t[ks[ks.length - 1]] = v; }
function getPath(o, path) { return path.split('.').reduce((t, k) => t == null ? t : t[k], o); }
function wireCats(box) { box.querySelectorAll('.cat').forEach(c => { if (closedCats.has(c.dataset.cat)) c.classList.add('closed'); c.querySelector('.cath').onclick = () => { c.classList.toggle('closed'); c.classList.contains('closed') ? closedCats.add(c.dataset.cat) : closedCats.delete(c.dataset.cat); store.set('pge:closedCats', [...closedCats]); }; }); }
function wireDetails(box, a) {
  wireCats(box);
  const apply = (k, raw, el) => {
    let v = raw;
    if (el.type === 'checkbox') v = el.checked; else if (el.type === 'number' || el.type === 'range' || /metalness|roughness|mass|intensity|range|angle|size/.test(k)) v = parseFloat(raw); if (Number.isNaN(v)) return;
    if (k.startsWith('mat.')) { const m = S.project.assets.materials[a.material]; if (!m) return; edit(() => setPath(m, k.slice(4), v), { ui: false }); syncTwins(box, k, v, el); return; }
    if (k === 'tags') v = String(raw).split(',').map(s => s.trim()).filter(Boolean);
    if (k === 'name') { v = String(raw).trim().slice(0, 60) || a.name; edit(() => a.name = v, { rebuild: false, ui: false }); renderOutliner(); return; }
    if (k === 'parent') return attachTo(a.id, v || null);
    if (k === 'script') v = v || undefined;
    edit(() => setPath(a, k, v), { ui: false }); syncTwins(box, k, v, el);
    if (['mesh', 'material', 'script', 'visible'].includes(k)) renderOutliner(); if (k === 'material' || k === 'script') renderDetails();
  };
  box.querySelectorAll('[data-k]').forEach(el => {
    const ev = el.type === 'range' || el.type === 'color' ? 'input' : 'change';
    el.addEventListener(ev, () => apply(el.dataset.k, el.value, el));
    if (el.type === 'range' || el.type === 'color') el.addEventListener('change', () => {});
    el.addEventListener('keydown', e => e.stopPropagation());
  });
  box.querySelectorAll('.vec').forEach(v => v.querySelectorAll('input').forEach(inp => {
    const set = val => { const k = v.dataset.vec, i = +inp.dataset.i; if (Number.isNaN(val)) return; a[k][i] = r4(val); applyTransform(objects[a.id], a); if (tc.object) selBox.setFromObject(tc.object); };
    inp.addEventListener('keydown', e => { e.stopPropagation(); if (e.key === 'Enter') inp.blur(); });
    inp.addEventListener('change', () => { pushUndo(); set(parseFloat(inp.value)); dirty(); });
    // drag to scrub the value, like a slider
    inp.addEventListener('mousedown', e => { if (document.activeElement === inp) return; e.preventDefault(); let mv = 0, start = parseFloat(inp.value) || 0, sx = e.clientX, began = false;
      const m = ev => { mv = ev.clientX - sx; if (!began && Math.abs(mv) > 2) { began = true; pushUndo(); } if (began) { const val = r4(start + mv * +v.dataset.step); inp.value = val; set(val); } };
      const u = () => { removeEventListener('mousemove', m); removeEventListener('mouseup', u); if (began) dirty(); else { inp.focus(); inp.select(); } }; addEventListener('mousemove', m); addEventListener('mouseup', u); });
  }));
  box.querySelectorAll('[data-act]').forEach(b => b.onclick = () => {
    const act = b.dataset.act;
    if (act === 'matEdit') materialDlg(a.material); if (act === 'matNew') { const id = newMaterial(true); edit(() => a.material = id); renderDetails(); }
    if (act === 'scriptEdit') openScript(a.script); if (act === 'scriptNew') { const id = newScript(true, a.name); edit(() => a.script = id); renderDetails(); openScript(id); }
  });
}
function syncTwins(box, k, v, el) { box.querySelectorAll(`[data-k="${k}"]`).forEach(x => { if (x !== el && x.type !== 'checkbox') x.value = v; }); }
function updateTransformFields(a) { if (dtab !== 'details' || S.sel !== a.id) return; for (const k of ['p', 'r', 's']) $$(`#dtBody [data-vec="${k}"] input`).forEach(i => { if (document.activeElement !== i) i.value = r4(a[k][+i.dataset.i]); }); }
function renderWorld(box) {
  const w = L().world;
  box.innerHTML = `<div class="dname"><span class="ic">🌐</span><b>World Settings · ${esc(S.level)}</b></div>` +
    cat('Game Mode', prop('Default Game Mode', selectHTML('gameMode', w.gameMode || 'FirstPerson', [['FirstPerson', 'First Person'], ['ThirdPerson', 'Third Person'], ['Spectator', 'Spectator (fly)']])) + prop('Field of View', numHTML('fov', w.fov ?? 75, 1)), 'wGame') +
    cat('Physics', prop('Global Gravity Z', numHTML('gravity', w.gravity ?? -9.81, 0.1)) + prop('Kill Z', numHTML('killZ', w.killZ ?? -30, 1)), 'wPhys') +
    cat('Sky & Lighting', prop('Sky Color', colorHTML('sky', w.sky)) + prop('Sky Light (upper)', colorHTML('skyLight', w.skyLight)) + prop('Sky Light (lower)', colorHTML('groundLight', w.groundLight)) + prop('Sky Light Intensity', numHTML('ambient', w.ambient ?? 0.8, 0.05)) + prop('Exposure', numHTML('exposure', w.exposure ?? 1, 0.05)), 'wSky') +
    cat('Fog', prop('Exponential Height Fog', chkHTML('fog', w.fog)) + prop('Fog Color', colorHTML('fogColor', w.fogColor || w.sky)) + prop('Fog Density', numHTML('fogDensity', w.fogDensity ?? 0.01, 0.002)), 'wFog');
  wireCats(box);
  box.querySelectorAll('[data-k]').forEach(el => el.addEventListener(el.type === 'color' ? 'input' : 'change', () => {
    let v = el.type === 'checkbox' ? el.checked : el.type === 'number' ? parseFloat(el.value) : el.value; if (Number.isNaN(v)) return;
    edit(() => w[el.dataset.k] = v, { ui: false }); syncTwins(box, el.dataset.k, v, el); if (el.dataset.k === 'fog') log('Fog shows when you Play');
  }));
}

/* -------------------------------------------------- Place Actors */
const PA = [['Basic', ['Empty', 'PlayerStart', 'TriggerBox', 'CameraActor', 'TextRender']], ['Lights', ['DirectionalLight', 'PointLight', 'SpotLight']], ['Shapes', MESHES.map(m => 'shape:' + m)]];
function renderPlaceActors() {
  const q = $('#paSearch').value.trim().toLowerCase();
  $('#paBody').innerHTML = PA.map(([c, list]) => { const items = list.map(t => t.startsWith('shape:') ? { k: 'shape', id: t.slice(6), n: t.slice(6), ic: '▣' } : { k: 'actor', id: t, n: ACTOR_TYPES[t].label, ic: ACTOR_TYPES[t].icon }).filter(x => !q || x.n.toLowerCase().includes(q));
    return items.length ? `<div class="pacat">${c}</div>` + items.map(x => `<div class="paitem" draggable="true" data-k="${x.k}" data-id="${x.id}"><span class="ic">${x.ic}</span>${esc(x.n)}</div>`).join('') : ''; }).join('');
  $$('#paBody .paitem').forEach(el => { el.ondragstart = e => e.dataTransfer.setData('pge/asset', JSON.stringify({ kind: el.dataset.k, id: el.dataset.id })); el.ondblclick = () => el.dataset.k === 'shape' ? spawn('StaticMesh', { mesh: el.dataset.id }) : spawn(el.dataset.id); });
}
$('#paSearch').oninput = renderPlaceActors;

/* -------------------------------------------------- Content Drawer */
const FOLDERS = [['Content', 0], ['Levels', 1], ['Materials', 1], ['Scripts', 1], ['Engine', 0], ['Shapes', 1]];
function assetsIn(f) {
  const P = S.project, out = [];
  const lv = () => Object.keys(P.levels).map(n => ({ kind: 'level', id: n, name: n, ic: '🗺', color: '#e8a33d', sub: 'Level' }));
  const ma = () => Object.entries(P.assets.materials).map(([id, m]) => ({ kind: 'material', id, name: m.name, color: '#5fb04c', sub: 'Material', mat: m }));
  const sc = () => Object.entries(P.assets.scripts).map(([id, s]) => ({ kind: 'script', id, name: s.name, ic: '{ }', color: '#3d8ef0', sub: 'Script' }));
  const sh = () => MESHES.map(m => ({ kind: 'shape', id: m, name: 'SM_' + m, ic: '▣', color: '#3dc1e8', sub: 'Static Mesh' }));
  if (f === 'Content') out.push(...lv(), ...ma(), ...sc()); if (f === 'Levels') out.push(...lv()); if (f === 'Materials') out.push(...ma()); if (f === 'Scripts') out.push(...sc()); if (f === 'Shapes' || f === 'Engine') out.push(...sh());
  return out;
}
function renderContent() {
  if (!S.project) return; const q = (($('#cbSearch') || {}).value || '').trim().toLowerCase();
  const list = assetsIn(S.cbFolder).filter(x => !q || x.name.toLowerCase().includes(q));
  $('#cbBody').innerHTML = `<div class="cbtree">${FOLDERS.map(([n, d]) => `<div class="tn${S.cbFolder === n ? ' sel' : ''}" style="--d:${d}" data-f="${n}">${d ? '📁' : '🗀'} ${n}</div>`).join('')}</div>
    <div class="cbmain"><div class="cbbar"><div class="tbmenu" data-menu="cbAdd"><button class="btn primary">＋ Add</button></div><button class="btn" data-act="import">⤓ Import</button><button class="btn" data-act="save">💾 Save All</button>
      <span class="cbpath">${S.cbFolder === 'Shapes' ? 'Engine › Shapes' : S.cbFolder === 'Content' || S.cbFolder === 'Engine' ? S.cbFolder : 'Content › ' + S.cbFolder}</span><span class="grow"></span><input class="search" id="cbSearch" style="width:220px" placeholder="Search ${S.cbFolder}" value="${esc(q)}"></div>
      <div class="cbgrid">${list.map(x => `<div class="asset${S.cbSel === x.kind + x.id ? ' sel' : ''}" draggable="${x.kind !== 'level'}" data-kind="${x.kind}" data-id="${esc(x.id)}" title="${esc(x.name)}">
        <div class="th" style="${x.mat ? `background:radial-gradient(circle at 35% 30%, rgba(255,255,255,${0.15 + (1 - x.mat.roughness) * 0.5}), ${x.mat.color} 45%, #000 105%)` : ''}">${x.mat ? '' : x.ic}<span class="k" style="background:${x.color}"></span></div><b>${esc(x.name)}</b><small>${x.sub}</small></div>`).join('') || '<div class="empty" style="grid-column:1/-1">This folder is empty. Use + Add.</div>'}</div></div>`;
  $$('#cbBody .tn').forEach(t => t.onclick = () => { S.cbFolder = t.dataset.f; renderContent(); });
  const s = $('#cbSearch'); s.oninput = () => { renderContent(); const n = $('#cbSearch'); n.focus(); n.setSelectionRange(n.value.length, n.value.length); }; s.onkeydown = e => e.stopPropagation();
  $$('#cbBody .asset').forEach(el => {
    const k = el.dataset.kind, id = el.dataset.id;
    el.onclick = () => { S.cbSel = k + id; $$('#cbBody .asset').forEach(x => x.classList.toggle('sel', x === el)); };
    el.ondblclick = () => { if (k === 'level') switchLevel(id); if (k === 'material') materialDlg(id); if (k === 'script') openScript(id); if (k === 'shape') spawn('StaticMesh', { mesh: id }); };
    el.ondragstart = e => e.dataTransfer.setData('pge/asset', JSON.stringify({ kind: k, id }));
    el.oncontextmenu = e => { e.preventDefault(); S.cbSel = k + id; if (k === 'shape') return;
      showPop([['sec', k[0].toUpperCase() + k.slice(1)], ['Edit / Open', `assetOpen:${k}|${id}`, '', '✎'], ['Rename', `assetRen:${k}|${id}`, 'F2', '✎'], ['Duplicate', `assetDup:${k}|${id}`, '', '⧉'], ['Delete', `assetDel:${k}|${id}`, 'Delete', '🗑', k === 'material' && id === 'M_Default']], e.clientX, e.clientY, el); };
  });
  $('#cbBody [data-menu=cbAdd] button').onclick = e => { const r = e.target.getBoundingClientRect(); showPop([['sec', 'Create Basic Asset'], ['Level', 'newLevel', '', '🗺'], ['Material', 'newMaterial', '', '◍'], ['Script', 'newScript', '', '{ }']], r.left, r.bottom + 4, e.target); };
  $('#cbBody [data-act=import]').onclick = importLevel; $('#cbBody [data-act=save]').onclick = () => saveProject();
}
// asset context commands
const assetCmd = c => { const [op, rest] = c.split(/:(.*)/s), [k, id] = rest.split('|'); const P = S.project;
  if (op === 'assetOpen') return k === 'level' ? switchLevel(id) : k === 'material' ? materialDlg(id) : openScript(id);
  if (op === 'assetRen') { const cur = k === 'level' ? id : (k === 'material' ? P.assets.materials[id].name : P.assets.scripts[id].name); const n = prompt('New name', cur); if (!n || n === cur) return;
    if (k === 'level') { if (P.levels[n]) return toast('A level with that name exists'); return edit(() => { P.levels[n] = P.levels[id]; delete P.levels[id]; if (P.startLevel === id) P.startLevel = n; if (S.level === id) S.level = n; }); }
    return edit(() => (k === 'material' ? P.assets.materials : P.assets.scripts)[id].name = n.slice(0, 60)); }
  if (op === 'assetDup') { if (k === 'level') { let n = id + '_Copy'; while (P.levels[n]) n += '2'; return edit(() => P.levels[n] = clone(P.levels[id])); }
    const bag = k === 'material' ? P.assets.materials : P.assets.scripts, nid = uid(k === 'material' ? 'M_' : 'SC_'); return edit(() => bag[nid] = { ...clone(bag[id]), name: bag[id].name + '_Copy' }); }
  if (op === 'assetDel') { if (k === 'level') { if (Object.keys(P.levels).length < 2) return toast("A project needs at least one level"); if (!confirm(`Delete level ${id}?`)) return; return edit(() => { delete P.levels[id]; if (P.startLevel === id) P.startLevel = Object.keys(P.levels)[0]; if (S.level === id) S.level = Object.keys(P.levels)[0]; }); }
    const used = Object.values(P.levels).some(l => l.actors.some(a => (k === 'material' ? a.material : a.script) === id)); if (used && !confirm(`${id} is used by actors in your levels. Delete anyway?`)) return;
    return edit(() => { delete (k === 'material' ? P.assets.materials : P.assets.scripts)[id]; Object.values(P.levels).forEach(l => l.actors.forEach(a => { if (k === 'material' && a.material === id) a.material = 'M_Default'; if (k === 'script' && a.script === id) delete a.script; })); }); }
};
function runAll(c) { if (/^asset(Open|Ren|Dup|Del):/.test(c)) return assetCmd(c); return run(c); }
window.__pgeRun = runAll;

/* =================================================================== dialogs */
function modal(html, onReady) { const m = $('#modal'); m.innerHTML = html; m.classList.remove('hidden'); m.querySelectorAll('[data-close-modal]').forEach(b => b.onclick = closeModal); m.onkeydown = e => { e.stopPropagation(); if (e.key === 'Escape' && !m.querySelector('.se')) closeModal(); }; if (onReady) onReady(m); }
function closeModal() { $('#modal').classList.add('hidden'); $('#modal').innerHTML = ''; }
function win(title, body, foot, cls = '') { return `<div class="win ${cls}"><div class="wh"><b>${esc(title)}</b><span class="grow"></span><button class="x" data-close-modal>✕</button></div><div class="wb">${body}</div>${foot ? `<div class="wf">${foot}</div>` : ''}</div>`; }
function newLevel() {
  const P = S.project; let n = 'NewLevel', i = 1; while (P.levels[n]) n = 'NewLevel' + (++i);
  modal(win('New Level', `<div class="form"><label>Name</label><input class="inp" id="nlName" value="${n}"><label>Template</label><select class="inp" id="nlTpl"><option value="Basic">Basic (floor, sun, player start)</option><option value="Empty">Empty Level</option><option value="Copy">Copy of ${esc(S.level)}</option></select></div>`,
    '<button class="btn" data-close-modal>Cancel</button><button class="btn primary" id="nlOk">Create</button>'), m => {
    m.querySelector('#nlName').select();
    m.querySelector('#nlOk').onclick = () => { const name = m.querySelector('#nlName').value.trim().replace(/[^\w-]/g, '_').slice(0, 40); if (!name) return; if (P.levels[name]) return toast('That level already exists');
      const t = m.querySelector('#nlTpl').value; let lv;
      if (t === 'Copy') lv = clone(L()); else { const acts = []; if (t === 'Basic') { floorAndSun(acts); acts.push(newActor('PlayerStart', { name: 'PlayerStart', p: [0, 1, 6] })); } lv = { actors: acts, world: clone(L().world) }; }
      edit(() => P.levels[name] = lv); closeModal(); switchLevel(name); log('Created level ' + name); };
  });
}
function switchLevel(n) { if (!S.project.levels[n]) return; S.level = n; S.sel = null; rebuild(); renderAll(); frameAll(); log('Opened level ' + n); }
function openLevelDlg() {
  modal(win('Open Level', `<div class="cbgrid" style="min-width:480px">${Object.keys(S.project.levels).map(n => `<div class="asset${n === S.level ? ' sel' : ''}" data-l="${esc(n)}"><div class="th">🗺<span class="k" style="background:#e8a33d"></span></div><b>${esc(n)}</b><small>${S.project.levels[n].actors.length} actors${n === S.project.startLevel ? ' · start' : ''}</small></div>`).join('')}</div>`, '<button class="btn" data-close-modal>Cancel</button>'),
    m => m.querySelectorAll('[data-l]').forEach(el => el.ondblclick = el.onclick = () => { closeModal(); switchLevel(el.dataset.l); }));
}
function newMaterial(silent) {
  const P = S.project; let n = 'M_NewMaterial', i = 1; const names = new Set(Object.values(P.assets.materials).map(m => m.name)); while (names.has(n)) n = 'M_NewMaterial' + (++i);
  const id = uid('M_'); edit(() => P.assets.materials[id] = { ...DEFAULT_MATERIAL, name: n, color: '#' + Math.floor(Math.random() * 0xffffff).toString(16).padStart(6, '0') }, { ui: false });
  log('Created material ' + n); if (!$('#drawer').classList.contains('hidden')) renderContent(); if (!silent) materialDlg(id); return id;
}
function materialDlg(id) {
  const P = S.project, m = P.assets.materials[id]; if (!m) return;
  const row = (l, h) => `<label>${l}</label><div>${h}</div>`;
  modal(win('Material Editor · ' + m.name, `<div style="display:grid;grid-template-columns:200px 1fr;gap:18px"><div id="mPrev" style="width:200px;height:200px;border-radius:6px;background:#0f0f10"></div><div class="form">
    ${row('Name', `<input class="inp" data-m="name" value="${esc(m.name)}">`)}${row('Base Color', `<input type="color" data-m="color" value="${m.color}">`)}
    ${row('Metallic', `<input type="range" min="0" max="1" step="0.01" data-m="metalness" value="${m.metalness}">`)}${row('Roughness', `<input type="range" min="0" max="1" step="0.01" data-m="roughness" value="${m.roughness}">`)}
    ${row('Emissive Color', `<input type="color" data-m="emissive" value="${m.emissive || '#000000'}">`)}${row('Emissive Strength', `<input type="range" min="0" max="10" step="0.1" data-m="emissiveIntensity" value="${m.emissiveIntensity ?? 1}">`)}
    ${row('Opacity', `<input type="range" min="0.05" max="1" step="0.01" data-m="opacity" value="${m.opacity ?? 1}">`)}</div></div>`, '<button class="btn primary" data-close-modal>Done</button>'), md => {
    // live preview sphere
    const pc = document.createElement('canvas'); pc.width = pc.height = 400; pc.style.width = pc.style.height = '200px'; md.querySelector('#mPrev').appendChild(pc);
    const r = new THREE.WebGLRenderer({ canvas: pc, antialias: true, alpha: true }); r.toneMapping = THREE.ACESFilmicToneMapping; r.outputColorSpace = THREE.SRGBColorSpace;
    const sc = new THREE.Scene(), c = new THREE.PerspectiveCamera(35, 1, 0.1, 10); c.position.set(0, 0, 3.2);
    sc.add(new THREE.HemisphereLight('#dfeaff', '#2a2d35', 1)); const dl = new THREE.DirectionalLight('#fff', 3); dl.position.set(2, 2, 3); sc.add(dl);
    const mesh = new THREE.Mesh(new THREE.SphereGeometry(1, 64, 32), new THREE.MeshStandardMaterial()); sc.add(mesh);
    const paint = () => { const mm = P.assets.materials[id]; Object.assign(mesh.material, { metalness: +mm.metalness, roughness: +mm.roughness, transparent: (mm.opacity ?? 1) < 1, opacity: +(mm.opacity ?? 1), emissiveIntensity: +(mm.emissiveIntensity ?? 1) }); mesh.material.color.set(mm.color); mesh.material.emissive.set(mm.emissive || '#000'); mesh.material.needsUpdate = true; r.render(sc, c); };
    paint(); let first = true;
    md.querySelectorAll('[data-m]').forEach(el => el.addEventListener('input', () => { const k = el.dataset.m, v = el.type === 'range' ? parseFloat(el.value) : el.value;
      if (first) { pushUndo(); first = false; } m[k] = k === 'name' ? (v.trim() || m.name).slice(0, 60) : v; dirty(); paint(); rebuild(); }));
    md.querySelectorAll('input').forEach(i => i.addEventListener('keydown', e => e.stopPropagation()));
    const done = md.querySelector('[data-close-modal].primary'); done.addEventListener('click', () => { r.dispose(); renderAll(); });
  });
}
function prefsDlg() {
  modal(win('Editor Preferences', `<div class="form"><label>Mouse sensitivity</label><input type="range" min="0.3" max="2.5" step="0.1" id="pfSens" value="${S.prefs.sensitivity}"><label>Camera speed</label><select class="inp" id="pfSpeed">${[1, 2, 3, 4, 5, 6, 7, 8].map(v => `<option${v === S.camSpeed ? ' selected' : ''}>${v}</option>`).join('')}</select>
    <label>Auto save</label><select class="inp" id="pfAuto">${[[0, 'Off'], [2, 'Every 2 minutes'], [5, 'Every 5 minutes'], [10, 'Every 10 minutes']].map(([v, n]) => `<option value="${v}"${v === +S.prefs.autosave ? ' selected' : ''}>${n}</option>`).join('')}</select></div>`, '<button class="btn primary" id="pfOk">Save</button>'), m => {
    m.querySelector('#pfOk').onclick = () => { S.prefs.sensitivity = +m.querySelector('#pfSens').value; S.prefs.autosave = +m.querySelector('#pfAuto').value; setCamSpeed(+m.querySelector('#pfSpeed').value); store.set('pge:prefs', S.prefs); closeModal(); toast('Preferences saved'); };
  });
}
function projectSettingsDlg() {
  const P = S.project;
  modal(win('Project Settings', `<div class="form"><label>Project Name</label><input class="inp" id="psName" value="${esc(P.name)}"><label>Description</label><input class="inp" id="psDesc" value="${esc(P.description || '')}"><label>Game Default Map</label><select class="inp" id="psStart">${Object.keys(P.levels).map(n => `<option${n === P.startLevel ? ' selected' : ''}>${esc(n)}</option>`).join('')}</select>
    <label>Template</label><span style="color:var(--muted)">${esc(P.template)}</span><label>Engine Version</label><span style="color:var(--muted)">${ENGINE_VERSION}</span></div>`, '<button class="btn" data-close-modal>Cancel</button><button class="btn primary" id="psOk">Save</button>'), m => {
    m.querySelector('#psOk').onclick = () => { edit(() => { P.name = m.querySelector('#psName').value.trim().slice(0, 60) || P.name; P.description = m.querySelector('#psDesc').value.slice(0, 300); P.startLevel = m.querySelector('#psStart').value; }, { rebuild: false }); closeModal(); saveProject(true); toast('Project settings saved'); };
  });
}
function docs() {
  modal(win('Premium Engine · Documentation', `<div style="max-width:720px;line-height:1.6">
    <h3 style="margin-top:0">Getting started</h3><p>Pick a template in the Project Browser, then build your level in the <b>Viewport</b>. Every object in a level is an <b>Actor</b>: meshes, lights, the Player Start, trigger boxes and text. The <b>Outliner</b> lists them; the <b>Details</b> panel edits the selected one.</p>
    <h3>Moving around</h3><p>Hold the <b>right mouse button</b> and use <b>W A S D Q E</b> to fly (mouse wheel while holding changes the speed). <b>Middle mouse</b> pans, <b>Alt + left drag</b> orbits, <b>F</b> focuses the selection.</p>
    <h3>Building</h3><p>Add actors from <b>+ Add</b>, from <b>Window › Place Actors</b>, or drag shapes from the <b>Content Drawer</b> (Ctrl+Space). Use <b>W / E / R</b> to move, rotate and scale. Drag materials and scripts from the Content Drawer onto actors.</p>
    <h3>Gameplay</h3><p>Scripts are written in JavaScript. A script attached to an actor gets <code>onStart()</code>, <code>onUpdate(dt)</code>, <code>onOverlap(other)</code>, <code>onHit(other)</code> and <code>onKey(code)</code>. See <b>Help › Script API Reference</b>.</p>
    <h3>Playing & shipping</h3><p><b>Play</b> runs the level with the game mode from World Settings. <b>Simulate</b> runs physics and scripts while you keep the editor camera. <b>Platforms › Web</b> packages the project into one HTML file that runs anywhere.</p></div>`, '<button class="btn primary" data-close-modal>Close</button>'));
}
function shortcuts() {
  const k = [['W / E / R', 'Translate / Rotate / Scale'], ['RMB + WASDQE', 'Fly the camera'], ['Alt + LMB', 'Orbit'], ['MMB drag', 'Pan'], ['F', 'Focus selection'], ['Home', 'Frame the whole level'], ['End', 'Snap to floor'], ['Delete', 'Delete'], ['Ctrl+D', 'Duplicate'],
    ['Ctrl+C / Ctrl+V / Ctrl+X', 'Copy / Paste / Cut'], ['Ctrl+Z / Ctrl+Y', 'Undo / Redo'], ['Ctrl+S', 'Save'], ['Ctrl+Space', 'Content Drawer'], ['F2', 'Rename'], ['H', 'Hide selected'], ['G', 'Game view (hide icons)'], ['Alt+P / Alt+S', 'Play / Simulate'], ['Esc', 'Stop / Unselect'], ['F7', 'Compile scripts'], ['F9', 'Screenshot']];
  modal(win('Keyboard Shortcuts', `<div class="kbd">${k.map(([a, b]) => `<span>${a}</span><span>${b}</span>`).join('')}</div>`, '<button class="btn primary" data-close-modal>Close</button>'));
}
function about() { modal(win('About Premium Engine', `<div style="text-align:center;padding:10px 40px"><div class="logo" style="font-size:28px;margin:0 auto 12px;display:inline-flex"><i>P</i><i>E</i></div><h2 style="margin:4px 0">Premium Engine</h2><p style="color:var(--muted)">Version ${ENGINE_VERSION} · experimental<br>Part of Premium Games<br>Rendering: three.js (MIT) · Physics: cannon-es (MIT)</p></div>`, '<button class="btn primary" data-close-modal>Close</button>')); }

/* -------------------------------------------------- scripts */
function newScript(silent, forName) {
  const P = S.project, names = new Set(Object.values(P.assets.scripts).map(s => s.name)); let n = 'SC_' + (forName ? String(forName).replace(/\W/g, '') : 'NewScript'), i = 1, base = n; while (names.has(n)) n = base + (++i);
  const id = uid('SC_'); edit(() => P.assets.scripts[id] = { name: n, code: SCRIPT_TEMPLATE }, { ui: false }); log('Created script ' + n);
  if (!$('#drawer').classList.contains('hidden')) renderContent(); if (!silent) openScript(id); return id;
}
const KW = /\b(function|const|let|var|if|else|for|while|return|new|true|false|null|undefined|this|of|in|break|continue|typeof|switch|case|default|class|extends|try|catch|throw)\b/g;
function highlight(code) {
  let h = esc(code);
  // comments and strings are set aside first (as private-use characters, so later rules can't touch them)
  const toks = []; const keep = (re, cls) => { h = h.replace(re, m => { toks.push(`<span class="${cls}">${m}</span>`); return String.fromCharCode(0xE000 + toks.length - 1); }); };
  keep(/\/\/[^\n]*/g, 'k-com'); keep(/\/\*[\s\S]*?\*\//g, 'k-com'); keep(/(&#39;|&quot;|`)(?:(?!\1).)*?\1/g, 'k-str');
  h = h.replace(KW, '<span class="k-kw">$1</span>').replace(/\b(\d+(?:\.\d+)?)\b/g, '<span class="k-num">$1</span>').replace(/\b(self|world|input|log|math|THREE)\b/g, '<span class="k-api">$1</span>').replace(/\b(on[A-Z]\w*)\b/g, '<span class="k-fn">$1</span>');
  return h.replace(/[\uE000-\uF8FF]/g, c => toks[c.charCodeAt(0) - 0xE000]) + '\n';
}
const API = `<h4>Events (define the ones you need)</h4><code>function onStart() {}
function onUpdate(dt) {}
function onOverlap(other) {}
function onHit(other) {}
function onKey(code) {}</code>
<h4>self — this actor</h4><code>self.name, self.tags
self.position  (x, y, z)
self.rotation  (x, y, z radians)
self.scale
self.visible = false
self.setColor('#ff0000')
self.setText('Hello')   // Text Render
self.addForce(x, y, z)
self.setVelocity(x, y, z)
self.distanceTo(world.player)
self.lookAt(x, y, z)
self.destroy()</code>
<h4>world</h4><code>world.time        // seconds
world.player      // .position .teleport() .launch()
world.find('Door')
world.findAll('enemy')  // by tag
world.spawn('Sphere', { position: [0, 5, 0], physics: true, color: '#f00' })
world.message('You win!', 3)
world.hud('score', 'Score: 10')
world.vars        // shared between scripts
world.setGravity(-9.81)
world.restart()</code>
<h4>input</h4><code>input.key('W')      // held
input.pressed('Space')  // this frame
input.mouse.left</code>
<h4>helpers</h4><code>log('text')
math.clamp(v, a, b)
math.lerp(a, b, t)
math.random(a, b)
math.vec3(x, y, z)</code>`;
function openScript(id) {
  const P = S.project, s = P.assets.scripts[id]; if (!s) return toast('Script not found');
  const users = Object.entries(P.levels).flatMap(([ln, l]) => l.actors.filter(a => a.script === id).map(a => ln + ' › ' + a.name));
  modal(win('Script Editor · ' + s.name, `<div class="code"><div class="gut" id="seGut"></div><div class="ed" id="seEd"><pre id="sePre"></pre><textarea id="seTa" spellcheck="false" autocapitalize="off" autocomplete="off"></textarea></div></div><div class="api"><b>Used by</b><p>${users.length ? users.map(esc).join('<br>') : 'No actors yet: drag the script onto an actor.'}</p>${API}</div>`,
    '<span id="seErr" class="serr" style="flex:1;border:0;padding:0 6px"></span><button class="btn" id="seCompile">Compile (F7)</button><button class="btn" data-close-modal>Close</button><button class="btn primary" id="seSave">Save</button>', 'se'), m => {
    const ta = m.querySelector('#seTa'), pre = m.querySelector('#sePre'), gut = m.querySelector('#seGut'), ed = m.querySelector('#seEd'), err = m.querySelector('#seErr');
    ta.value = s.code;
    const paint = () => { pre.innerHTML = highlight(ta.value); const n = ta.value.split('\n').length; gut.innerHTML = Array.from({ length: n }, (_, i) => i + 1).join('<br>'); ta.style.height = pre.offsetHeight + 'px'; ta.style.width = Math.max(ed.clientWidth, pre.scrollWidth) + 'px'; };
    ed.onscroll = () => { gut.scrollTop = ed.scrollTop; };
    const compile = () => { const e = checkScript(ta.value); err.className = 'serr' + (e ? ' bad' : ''); err.textContent = e ? '✖ ' + e : '✔ Compiled'; return !e; };
    const save = () => { const ok = compile(); if (ta.value !== s.code) { pushUndo(); s.code = ta.value; dirty(); } log(ok ? `Compiled and saved ${s.name}` : `error: ${s.name}: ${err.textContent.slice(2)}`, ok ? 'ok' : 'error'); return ok; };
    ta.addEventListener('input', paint);
    ta.addEventListener('keydown', e => {
      e.stopPropagation();
      if (e.key === 'Tab') { e.preventDefault(); const a = ta.selectionStart; ta.setRangeText('  ', a, ta.selectionEnd, 'end'); paint(); }
      if (e.key === 'Enter') { const a = ta.selectionStart, line = ta.value.slice(0, a).split('\n').pop(), ind = line.match(/^\s*/)[0] + (/[{(\[]\s*$/.test(line) ? '  ' : ''); e.preventDefault(); ta.setRangeText('\n' + ind, a, ta.selectionEnd, 'end'); paint(); }
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's') { e.preventDefault(); save(); saveProject(true); }
      if (e.key === 'F7') { e.preventDefault(); compile(); }
      if (e.key === 'Escape') { e.preventDefault(); if (ta.value !== s.code && !confirm('Close without saving the script?')) return; closeModal(); }
    });
    m.querySelector('#seCompile').onclick = compile; m.querySelector('#seSave').onclick = () => { if (save()) { saveProject(true); toast('Script saved'); } };
    m.querySelector('[data-close-modal]').onclick = () => { if (ta.value !== s.code && !confirm('Close without saving the script?')) return; closeModal(); };
    requestAnimationFrame(() => { paint(); compile(); ta.focus(); });
  });
}
function findScripts() {
  modal(win('Find in Scripts', `<input class="inp" id="fsQ" placeholder="Search text in all scripts" style="width:520px"><div id="fsRes" style="margin-top:10px;max-height:50vh;overflow:auto;font:12px var(--mono)"></div>`, '<button class="btn" data-close-modal>Close</button>'), m => {
    const q = m.querySelector('#fsQ'); q.focus();
    q.oninput = () => { const t = q.value.trim().toLowerCase(); const res = []; if (t) for (const [id, s] of Object.entries(S.project.assets.scripts)) s.code.split('\n').forEach((l, i) => { if (l.toLowerCase().includes(t)) res.push(`<div data-s="${id}" style="padding:3px 0;cursor:default"><span style="color:var(--gold)">${esc(s.name)}:${i + 1}</span> ${esc(l.trim())}</div>`); });
      m.querySelector('#fsRes').innerHTML = res.join('') || (t ? '<span style="color:var(--dim)">No results</span>' : ''); m.querySelectorAll('[data-s]').forEach(el => el.onclick = () => openScript(el.dataset.s)); };
  });
}
function apiRef() { modal(win('Script API Reference', `<div class="api" style="border:0;max-width:640px;background:none">${API}</div>`, '<button class="btn primary" data-close-modal>Close</button>')); }
function compileAll() { let bad = 0; for (const s of Object.values(S.project.assets.scripts)) { const e = checkScript(s.code); if (e) { bad++; log(`error: ${s.name}: ${e}`, 'error'); } } log(bad ? `Compile finished: ${bad} script(s) with errors` : `Compile finished: ${Object.keys(S.project.assets.scripts).length} script(s) OK`, bad ? 'error' : 'ok'); showDrawer('log'); return !bad; }
function mapCheck(level = S.level, quiet) {
  const P = S.project, l = P.levels[level], issues = [];
  const gm = (l.world || {}).gameMode || 'FirstPerson';
  if (gm !== 'Spectator' && !l.actors.some(a => a.type === 'PlayerStart')) issues.push(['warn', `${level}: no Player Start — the player spawns at (0, 2, 5)`]);
  if (l.actors.filter(a => a.type === 'PlayerStart').length > 1) issues.push(['warn', `${level}: more than one Player Start, the first one is used`]);
  if (!l.actors.some(a => a.type.includes('Light'))) issues.push(['warn', `${level}: no lights besides the sky light`]);
  for (const a of l.actors) { if (a.material && !P.assets.materials[a.material]) issues.push(['error', `${level}: ${a.name} uses a missing material`]); if (a.script && !P.assets.scripts[a.script]) issues.push(['error', `${level}: ${a.name} uses a missing script`]); if (a.parent && !l.actors.some(x => x.id === a.parent)) issues.push(['warn', `${level}: ${a.name} is attached to a missing actor`]); }
  issues.forEach(([k, t]) => log((k === 'error' ? 'error: ' : 'warning: ') + t, k === 'error' ? 'error' : 'warn'));
  if (!quiet) { log(`Map Check ${level}: ${issues.length ? issues.length + ' issue(s)' : 'no problems'}`, issues.some(i => i[0] === 'error') ? 'error' : 'ok'); showDrawer('log'); }
  return issues;
}
function buildAll() { log('Build started'); const ok = compileAll(); let errs = 0; for (const n of Object.keys(S.project.levels)) errs += mapCheck(n, true).filter(i => i[0] === 'error').length; log(ok && !errs ? 'Build succeeded' : 'Build finished with errors', ok && !errs ? 'ok' : 'error'); showDrawer('log'); return ok && !errs; }

/* =================================================================== play in editor */
function startPlay(simulate) {
  if (S.pie) return; if (!S.project) return;
  const pc = $('#playCanvas'); pc.classList.remove('hidden'); canvas.classList.add('hidden');
  const proj = clone(S.project); const lvl = proj.levels[S.level];
  if (simulate) { lvl.world = lvl.world || {}; lvl.world.gameMode = 'Spectator'; lvl.actors = lvl.actors.filter(a => a.type !== 'PlayerStart'); lvl.actors.push(newActor('PlayerStart', { name: '__cam', p: [camState.pos.x, camState.pos.y - 1.6, camState.pos.z], r: [0, camState.yaw / DEG, 0] })); }
  try {
    S.pie = new Game({ project: proj, levelName: S.level, canvas: pc, hud: $('#hud'), log: (t, k) => log(t, k) }).start();
    if (simulate) S.pie.pitch = camState.pitch;
  } catch (e) { log('error: Play failed: ' + e.message, 'error'); pc.classList.add('hidden'); canvas.classList.remove('hidden'); S.pie = null; return; }
  tc.detach(); selBox.visible = false;
  $('#viewportWrap').classList.add('playing'); $('#pieBanner').classList.toggle('hidden', !!simulate);
  $('[data-cmd=play]').disabled = $('[data-cmd=simulate]').disabled = true; $('[data-cmd=stop]').disabled = false;
  log(simulate ? 'Simulate started (hold right mouse + WASD to look around)' : `Play In Editor: ${S.level} (${lvl.world.gameMode || 'FirstPerson'})`, 'ok'); pc.focus();
}
function stopPlay() {
  if (!S.pie) return; S.pie.stop(); S.pie.renderer.dispose(); S.pie = null;
  const pc = $('#playCanvas'); pc.classList.add('hidden'); canvas.classList.remove('hidden'); $('#hud').innerHTML = '';
  $('#viewportWrap').classList.remove('playing'); $('#pieBanner').classList.add('hidden');
  $('[data-cmd=play]').disabled = $('[data-cmd=simulate]').disabled = false; $('[data-cmd=stop]').disabled = true;
  attachSel(); log('Play session ended'); canvas.focus();
}

/* =================================================================== packaging */
async function b64(url) { const buf = new Uint8Array(await (await fetch(url)).arrayBuffer()); let s = ''; for (let i = 0; i < buf.length; i += 0x8000) s += String.fromCharCode.apply(null, buf.subarray(i, i + 0x8000)); return btoa(s); }
async function packageProject(openWindow) {
  if (!buildAll()) { if (!confirm('The build has errors. Package anyway?')) return; }
  log('Packaging ' + S.project.name + ' for Web...');
  try {
    const [three, cannon, rt] = await Promise.all([b64('./vendor/three.module.js'), b64('./vendor/cannon-es.js'), b64('./runtime.js')]);
    const proj = clone(S.project); const html = `<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>${esc(proj.name)}</title>
<style>html,body{margin:0;height:100%;background:#000;overflow:hidden;font-family:system-ui,sans-serif}canvas{width:100%;height:100%;display:block}#hud{position:fixed;inset:0;pointer-events:none}.pge-hud{display:block;margin:10px 14px;font:700 18px system-ui;color:#fff;text-shadow:0 2px 6px #000;width:max-content}.pge-msg{position:absolute;left:50%;top:38%;transform:translate(-50%,-50%);font:800 34px system-ui;color:#fff;text-shadow:0 3px 16px #000;transition:opacity .4s;text-align:center}
#start{position:fixed;inset:0;display:grid;place-items:center;background:radial-gradient(circle,#1b2338,#05070c);color:#fff;cursor:pointer;text-align:center}#start h1{margin:0 0 8px;font-size:42px}#start p{color:#9aa4b8}</style>
<script type="importmap">${JSON.stringify({ imports: { three: 'data:text/javascript;base64,' + three, 'cannon-es': 'data:text/javascript;base64,' + cannon, 'pge-runtime': 'data:text/javascript;base64,' + rt } })}</script></head>
<body><canvas id="c"></canvas><div id="hud"></div><div id="start"><div><h1>${esc(proj.name)}</h1><p>Click to play · WASD move · Space jump · Mouse look · Esc release mouse</p><p style="font-size:12px;margin-top:30px">Made with Premium Engine ${ENGINE_VERSION}</p></div></div>
<script type="module">import { Game } from 'pge-runtime'; const project = ${JSON.stringify(proj).replace(/</g, '\\u003c')};
document.getElementById('start').onclick = e => { e.currentTarget.remove(); const g = new Game({ project, canvas: document.getElementById('c'), hud: document.getElementById('hud') }).start(); try { g.canvas.requestPointerLock(); } catch (er) {} window.game = g; };</script></body></html>`;
    const blob = new Blob([html], { type: 'text/html' }), url = URL.createObjectURL(blob);
    if (openWindow) { window.open(url, '_blank'); log('Opened the packaged game in a new window', 'ok'); return url; }
    const a = document.createElement('a'); a.href = url; a.download = proj.name.replace(/[^\w-]+/g, '_') + '.html'; a.click(); setTimeout(() => URL.revokeObjectURL(url), 60000);
    log(`Packaged ${a.download} (${(blob.size / 1048576).toFixed(1)} MB) — open it in any browser`, 'ok'); toast('Package ready'); return url;
  } catch (e) { log('error: Packaging failed: ' + e.message, 'error'); }
}
function download(name, text, type = 'application/json') { const a = document.createElement('a'); a.href = URL.createObjectURL(new Blob([text], { type })); a.download = name; a.click(); setTimeout(() => URL.revokeObjectURL(a.href), 30000); }
function exportProject() { download(S.project.name.replace(/[^\w-]+/g, '_') + '.pge.json', JSON.stringify(S.project, null, 1)); log('Exported project file'); }
function pickFile() { return new Promise(res => { const i = document.createElement('input'); i.type = 'file'; i.accept = '.json,.pge.json,application/json'; i.onchange = () => { const f = i.files[0]; if (!f) return res(null); f.text().then(t => { try { res(JSON.parse(t)); } catch (e) { toast('Not a valid project file'); res(null); } }); }; i.click(); }); }
async function importLevel() {
  const p = await pickFile(); if (!p || !p.levels) return;
  const src = p.levels[p.startLevel] || Object.values(p.levels)[0]; const map = {};
  const copies = src.actors.map(a => { const c = clone(a); map[a.id] = c.id = uid('a'); return c; }); copies.forEach(c => { c.parent = c.parent ? map[c.parent] || null : null; });
  edit(() => { L().actors.push(...copies); for (const [id, m] of Object.entries(p.assets.materials || {})) if (!S.project.assets.materials[id]) S.project.assets.materials[id] = m; for (const [id, s] of Object.entries(p.assets.scripts || {})) if (!S.project.assets.scripts[id]) S.project.assets.scripts[id] = s; });
  log(`Imported ${copies.length} actors from ${p.name}`, 'ok');
}

/* =================================================================== project browser */
const TPL = [['Blank', '⬜', 'An empty level with a floor, a sun and a free-flying camera.'], ['FirstPerson', '🎯', 'Walk, jump and collect coins in first person. Physics boxes, a jump pad and a goal.'], ['ThirdPerson', '🧍', 'The same arena with a third-person character and a follow camera.']];
function projectBrowser() {
  let tab = listProjects().length ? 'recent' : 'games', pick = null;
  const draw = m => {
    const list = listProjects();
    m.querySelector('.pbside').innerHTML = `<div class="tn${tab === 'recent' ? ' sel' : ''}" data-t="recent">🕘 Recent Projects</div><div class="tn${tab === 'games' ? ' sel' : ''}" data-t="games">🎮 Games</div>`;
    m.querySelector('.pbgrid').innerHTML = tab === 'recent' ? (list.length ? list.map(p => `<div class="tpl${pick === 'p:' + p.id ? ' sel' : ''}" data-p="${p.id}"><div class="art">${(TPL.find(t => t[0] === p.template) || TPL[0])[1]}</div><b>${esc(p.name)}</b><small>${new Date(p.updated).toLocaleString('en-GB')} · ${p.actors || 0} actors</small></div>`).join('') : '<div class="empty" style="grid-column:1/-1">No projects yet. Pick a template in Games.</div>')
      : TPL.map(([k, ic, d]) => `<div class="tpl${pick === 't:' + k ? ' sel' : ''}" data-tp="${k}"><div class="art">${ic}</div><b>${k.replace(/([a-z])([A-Z])/g, '$1 $2')}</b><small>${d}</small></div>`).join('');
    m.querySelectorAll('[data-t]').forEach(el => el.onclick = () => { tab = el.dataset.t; pick = null; draw(m); });
    m.querySelectorAll('[data-tp]').forEach(el => { el.onclick = () => { pick = 't:' + el.dataset.tp; draw(m); }; el.ondblclick = () => { pick = 't:' + el.dataset.tp; go(m); }; });
    m.querySelectorAll('[data-p]').forEach(el => { el.onclick = () => { pick = 'p:' + el.dataset.p; draw(m); }; el.ondblclick = () => { pick = 'p:' + el.dataset.p; go(m); };
      el.oncontextmenu = e => { e.preventDefault(); if (confirm('Delete this project from this computer?')) { localStorage.removeItem('pge:p:' + el.dataset.p); store.set('pge:projects', listProjects().filter(x => x.id !== el.dataset.p)); draw(m); } }; });
    const t = pick && pick.startsWith('t:') ? TPL.find(x => x[0] === pick.slice(2)) : null, pr = pick && pick.startsWith('p:') ? list.find(x => x.id === pick.slice(2)) : null;
    m.querySelector('#pbInfo').innerHTML = t ? `<b>${t[0].replace(/([a-z])([A-Z])/g, '$1 $2')}</b><p>${t[2]}</p>` : pr ? `<b>${esc(pr.name)}</b><p>Last saved ${new Date(pr.updated).toLocaleString('en-GB')}. Right-click a project to delete it.</p>` : '<b>Select a template or a project</b><p>Projects are saved in this browser. Use File › Export Project to keep a copy.</p>';
    m.querySelector('#pbNameRow').classList.toggle('hidden', !t); m.querySelector('#pbGo').textContent = pr ? 'Open' : 'Create'; m.querySelector('#pbGo').disabled = !pick;
  };
  const go = m => {
    if (!pick) return;
    if (pick.startsWith('p:')) { const p = store.get('pge:p:' + pick.slice(2)); if (!p) return toast('Project not found'); closeModal(); return openProject(p); }
    const name = (m.querySelector('#pbName').value.trim() || 'MyProject').replace(/[^\w -]/g, '').slice(0, 40); const p = template(pick.slice(2), name);
    closeModal(); openProject(p); saveProject(true); log(`Created project ${name} from the ${pick.slice(2)} template`, 'ok');
  };
  modal(`<div class="win pb"><div class="wh"><span class="logo"><i>P</i><i>E</i></span><b>Premium Engine Project Browser</b><span class="grow"></span>${S.project ? '<button class="x" data-close-modal>✕</button>' : ''}</div>
    <div class="wb"><div class="pbside"></div><div class="pbmain"><div class="pbgrid"></div>
    <div class="pbfoot"><div id="pbInfo"></div><div class="f"><div id="pbNameRow"><label style="color:var(--muted)">Project Name</label><input class="inp" id="pbName" value="MyProject"></div>
    <div class="btnrow" style="justify-content:flex-end"><button class="btn" id="pbFile">Open File…</button>${S.project ? '<button class="btn" data-close-modal>Cancel</button>' : ''}<button class="btn primary" id="pbGo" disabled>Create</button></div></div></div></div></div></div>`, m => {
    draw(m); m.querySelector('#pbGo').onclick = () => go(m);
    m.querySelector('#pbName').onkeydown = e => { e.stopPropagation(); if (e.key === 'Enter') go(m); };
    m.querySelector('#pbFile').onclick = async () => { const p = await pickFile(); if (!p || !p.levels) return; p.id = p.id || uid('p'); closeModal(); openProject(p); saveProject(true); };
  });
}

/* =================================================================== keyboard */
addEventListener('keydown', e => {
  if (!S.project) return; const t = e.target, typing = t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.tagName === 'SELECT');
  if (!$('#modal').classList.contains('hidden')) return; if (typing) return;
  const ctrl = e.ctrlKey || e.metaKey, k = e.key.toLowerCase();
  if (S.pie) { if (e.key === 'Escape' || (e.shiftKey && e.key === 'F1')) { if (e.shiftKey && document.pointerLockElement) { document.exitPointerLock(); return; } e.preventDefault(); stopPlay(); } return; }
  if (rmb) return;
  if (ctrl && k === 's') { e.preventDefault(); return saveProject(); }
  if (ctrl && k === 'z') { e.preventDefault(); return undo(); } if (ctrl && (k === 'y' || (e.shiftKey && k === 'z'))) { e.preventDefault(); return redo(); }
  if (ctrl && k === 'c') return copySel(); if (ctrl && k === 'v') return paste(); if (ctrl && k === 'x') { copySel(); return del(); }
  if (ctrl && k === 'd') { e.preventDefault(); return duplicate(); }
  if (ctrl && e.code === 'Space') { e.preventDefault(); return toggleDrawer(); }
  if (ctrl && k === 'n') { e.preventDefault(); return newLevel(); } if (ctrl && k === 'o') { e.preventDefault(); return openLevelDlg(); }
  if (ctrl && k === 'h') { e.preventDefault(); return run('showAll'); } if (ctrl && k === 'a') { e.preventDefault(); return run('selectAll'); }
  if (e.altKey && k === 'p') { e.preventDefault(); return startPlay(false); } if (e.altKey && k === 's') { e.preventDefault(); return startPlay(true); }
  if (ctrl || e.altKey) return;
  if (k === 'w') setTool('translate'); else if (k === 'e') setTool('rotate'); else if (k === 'r') setTool('scale');
  else if (k === 'f') focusSel(); else if (e.key === 'Home') frameAll(); else if (e.key === 'End') snapToFloor();
  else if (e.key === 'Delete' || e.key === 'Backspace') del(); else if (e.key === 'Escape') select(null);
  else if (e.key === 'F2' && S.sel) { renaming = S.sel; renderOutliner(); } else if (k === 'h' && S.sel) run('hide'); else if (k === 'g') run('toggleIcons');
  else if (e.key === 'F7') { e.preventDefault(); compileAll(); } else if (e.key === 'F9') { e.preventDefault(); run('screenshot'); } else if (e.key === 'F1') { e.preventDefault(); docs(); }
});
// console commands
$('#cmdInput').addEventListener('keydown', e => {
  e.stopPropagation(); if (e.key !== 'Enter') return; const c = e.target.value.trim(); e.target.value = ''; if (!c) return; log('> ' + c);
  const [w, ...a] = c.split(/\s+/); const lw = w.toLowerCase();
  if (lw === 'help') return log('Commands: stat fps | spawn <Shape> | play | simulate | stop | save | gravity <value> | level <name> | clear');
  if (lw === 'stat') { S.show.stats = !S.show.stats; return; } if (lw === 'play') return startPlay(false); if (lw === 'simulate') return startPlay(true); if (lw === 'stop') return stopPlay(); if (lw === 'save') return saveProject();
  if (lw === 'clear') { logBox().innerHTML = ''; return; } if (lw === 'spawn') { const m = MESHES.find(x => x.toLowerCase() === (a[0] || 'cube').toLowerCase()); return m ? spawn('StaticMesh', { mesh: m }) : log('warning: unknown shape ' + a[0]); }
  if (lw === 'gravity') { const v = parseFloat(a[0]); if (Number.isNaN(v)) return log('warning: gravity needs a number'); return edit(() => L().world.gravity = v, { rebuild: false }); }
  if (lw === 'level') return switchLevel(a.join(' '));
  log('warning: Unknown command. Type help.');
});

/* =================================================================== start */
$('#engVer').textContent = 'Premium Engine ' + ENGINE_VERSION;
window.__pge = { S, run: runAll, spawn, select, startPlay, stopPlay, saveProject, openProject, template, packageProject, actor, L };
setInterval(() => { if (S.project && S.dirty && +S.prefs.autosave && Date.now() - (S._lastAuto || 0) > S.prefs.autosave * 60000) { S._lastAuto = Date.now(); saveProject(true); log('Auto saved'); } }, 15000);
addEventListener('beforeunload', e => { if (S.dirty) { e.preventDefault(); e.returnValue = ''; } });
setView('Perspective'); setCamSpeed(4); renderPlaceActors();
const last = store.get('pge:last'), lp = last && store.get('pge:p:' + last);
const q = new URLSearchParams(location.search);
if (q.get('new')) { openProject(template(q.get('new'), q.get('name') || 'MyProject')); saveProject(true); history.replaceState(null, '', location.pathname); }
else if (lp && !q.has('browser')) openProject(lp); else projectBrowser();
log('Premium Engine ' + ENGINE_VERSION + ' ready. Press F1 for help.');
