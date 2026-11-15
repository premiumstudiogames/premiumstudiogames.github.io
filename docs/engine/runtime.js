// Premium Engine runtime: builds a level from project data and runs it (used by Play In Editor and by packaged games).
import * as THREE from 'three';
import * as CANNON from 'cannon-es';

export const ENGINE_VERSION = '0.1.0';
export const MESHES = ['Cube', 'Sphere', 'Cylinder', 'Cone', 'Plane', 'Capsule', 'Torus', 'Wedge'];
export const DEG = Math.PI / 180;

/* ------------------------------------------------------------------ geometry & materials */
const geoCache = {};
export function geometry(mesh) {
  if (geoCache[mesh]) return geoCache[mesh];
  let g;
  switch (mesh) {
    case 'Sphere': g = new THREE.SphereGeometry(0.5, 32, 16); break;
    case 'Cylinder': g = new THREE.CylinderGeometry(0.5, 0.5, 1, 32); break;
    case 'Cone': g = new THREE.ConeGeometry(0.5, 1, 32); break;
    case 'Plane': g = new THREE.PlaneGeometry(1, 1); g.rotateX(-Math.PI / 2); break;
    case 'Capsule': g = new THREE.CapsuleGeometry(0.35, 1.1, 8, 16); break;
    case 'Torus': g = new THREE.TorusGeometry(0.4, 0.12, 16, 48); break;
    case 'Wedge': {
      const s = new THREE.Shape(); s.moveTo(-0.5, -0.5); s.lineTo(0.5, -0.5); s.lineTo(-0.5, 0.5); s.lineTo(-0.5, -0.5);
      g = new THREE.ExtrudeGeometry(s, { depth: 1, bevelEnabled: false }); g.translate(0, 0, -0.5); break; }
    default: g = new THREE.BoxGeometry(1, 1, 1);
  }
  return (geoCache[mesh] = g);
}
export const DEFAULT_MATERIAL = { name: 'M_Default', color: '#c8ccd4', metalness: 0, roughness: 0.6, emissive: '#000000', emissiveIntensity: 1, opacity: 1 };
export function makeMaterial(project, matId) {
  const m = (project.assets.materials || {})[matId] || DEFAULT_MATERIAL;
  const mat = new THREE.MeshStandardMaterial({ color: m.color, metalness: +m.metalness || 0, roughness: m.roughness == null ? 0.6 : +m.roughness,
    emissive: m.emissive || '#000000', emissiveIntensity: m.emissiveIntensity == null ? 1 : +m.emissiveIntensity });
  if ((m.opacity ?? 1) < 1) { mat.transparent = true; mat.opacity = +m.opacity; }
  mat.userData.matId = matId;
  return mat;
}

/* ------------------------------------------------------------------ actors */
export const ACTOR_TYPES = {
  StaticMesh: { label: 'Static Mesh', icon: '▣' },
  Empty: { label: 'Empty Actor', icon: '◇' },
  PointLight: { label: 'Point Light', icon: '✹' },
  SpotLight: { label: 'Spot Light', icon: '◭' },
  DirectionalLight: { label: 'Directional Light', icon: '☀' },
  PlayerStart: { label: 'Player Start', icon: '⚑' },
  CameraActor: { label: 'Camera Actor', icon: '◉' },
  TriggerBox: { label: 'Trigger Box', icon: '⬚' },
  TextRender: { label: 'Text Render', icon: 'T' }
};
export function newActor(type, o = {}) {
  const a = { id: 'a' + Math.random().toString(36).slice(2, 10), name: o.name || type, type, parent: o.parent || null,
    p: o.p || [0, 0, 0], r: o.r || [0, 0, 0], s: o.s || [1, 1, 1], visible: true, tags: [] };
  if (type === 'StaticMesh') Object.assign(a, { mesh: o.mesh || 'Cube', material: o.material || 'M_Default', physics: { simulate: false, mass: 1, collision: true } });
  if (type === 'PointLight') a.light = { color: '#ffd9a0', intensity: 30, range: 12, shadows: true };
  if (type === 'SpotLight') a.light = { color: '#ffffff', intensity: 60, range: 20, angle: 35, shadows: true };
  if (type === 'DirectionalLight') a.light = { color: '#fff4e0', intensity: 2.2, shadows: true };
  if (type === 'TriggerBox') a.s = o.s || [2, 2, 2];
  if (type === 'TextRender') a.text = { value: o.text || 'Text', color: '#ffffff', size: 0.6 };
  if (o.script) a.script = o.script;
  return a;
}
function textSprite(t) {
  const c = document.createElement('canvas'), x = c.getContext('2d'), fs = 96;
  x.font = `700 ${fs}px system-ui, sans-serif`; const w = Math.ceil(x.measureText(t.value || ' ').width) + 24;
  c.width = w; c.height = fs * 1.4; x.font = `700 ${fs}px system-ui, sans-serif`; x.fillStyle = t.color || '#fff'; x.textBaseline = 'middle'; x.fillText(t.value || '', 12, c.height / 2);
  const tex = new THREE.CanvasTexture(c); tex.colorSpace = THREE.SRGBColorSpace;
  const m = new THREE.Mesh(new THREE.PlaneGeometry(w / c.height * (t.size || 0.6) * 1.4, (t.size || 0.6) * 1.4), new THREE.MeshBasicMaterial({ map: tex, transparent: true, side: THREE.DoubleSide, depthWrite: false }));
  return m;
}
// editor-only helper visuals (icons for lights, player start, triggers…)
function helper(kind, color) {
  const g = new THREE.Group(); g.userData.editorOnly = true;
  const lm = new THREE.LineBasicMaterial({ color, depthTest: false, transparent: true, opacity: 0.9 });
  if (kind === 'light') { g.add(new THREE.Mesh(new THREE.SphereGeometry(0.15, 12, 8), new THREE.MeshBasicMaterial({ color, depthTest: false }))); }
  if (kind === 'player') {
    g.add(new THREE.LineSegments(new THREE.EdgesGeometry(new THREE.CapsuleGeometry(0.35, 1.1, 2, 8)), lm)).position.y = 0;
    const arrow = new THREE.ArrowHelper(new THREE.Vector3(0, 0, -1), new THREE.Vector3(0, 0.2, 0), 1.2, color, 0.3, 0.2); g.add(arrow);
  }
  if (kind === 'camera') { const b = new THREE.Mesh(new THREE.BoxGeometry(0.4, 0.3, 0.5), new THREE.MeshBasicMaterial({ color, wireframe: true })); g.add(b);
    const c = new THREE.Mesh(new THREE.ConeGeometry(0.2, 0.3, 4, 1, true), new THREE.MeshBasicMaterial({ color, wireframe: true })); c.rotation.x = Math.PI / 2; c.position.z = -0.4; g.add(c); }
  if (kind === 'trigger') g.add(new THREE.LineSegments(new THREE.EdgesGeometry(new THREE.BoxGeometry(1, 1, 1)), new THREE.LineBasicMaterial({ color })));
  if (kind === 'empty') g.add(new THREE.AxesHelper(0.5));
  return g;
}
export function buildActor(project, a, editor) {
  let o;
  if (a.type === 'StaticMesh') { o = new THREE.Mesh(geometry(a.mesh), makeMaterial(project, a.material)); o.castShadow = o.receiveShadow = true; }
  else if (a.type === 'PointLight') { o = new THREE.Group(); const l = new THREE.PointLight(a.light.color, a.light.intensity, a.light.range, 2); l.castShadow = !!a.light.shadows; l.shadow.mapSize.set(512, 512); o.add(l); if (editor) o.add(helper('light', a.light.color)); }
  else if (a.type === 'SpotLight') { o = new THREE.Group(); const l = new THREE.SpotLight(a.light.color, a.light.intensity, a.light.range, (a.light.angle || 35) * DEG, 0.3, 2); l.castShadow = !!a.light.shadows; l.position.set(0, 0, 0); l.target.position.set(0, 0, -1); o.add(l, l.target); if (editor) o.add(helper('light', a.light.color)); }
  else if (a.type === 'DirectionalLight') { o = new THREE.Group(); const l = new THREE.DirectionalLight(a.light.color, a.light.intensity); l.castShadow = !!a.light.shadows;
    l.shadow.mapSize.set(2048, 2048); Object.assign(l.shadow.camera, { left: -40, right: 40, top: 40, bottom: -40, near: 0.5, far: 200 }); l.shadow.bias = -0.0005; l.position.set(0, 0, 0); l.target.position.set(0, 0, -1); o.add(l, l.target); o.userData.sun = l; if (editor) o.add(helper('light', '#ffe9a8')); }
  else if (a.type === 'PlayerStart') { o = new THREE.Group(); if (editor) o.add(helper('player', '#4fc3f7')); }
  else if (a.type === 'CameraActor') { o = new THREE.Group(); if (editor) o.add(helper('camera', '#c792ea')); }
  else if (a.type === 'TriggerBox') { o = new THREE.Group(); if (editor) o.add(helper('trigger', '#ff9e40')); }
  else if (a.type === 'TextRender') { o = new THREE.Group(); o.add(textSprite(a.text || {})); }
  else { o = new THREE.Group(); if (editor) o.add(helper('empty')); }
  o.name = a.name; o.userData.actorId = a.id; o.visible = a.visible !== false;
  applyTransform(o, a);
  return o;
}
export function applyTransform(o, a) {
  o.position.set(+a.p[0] || 0, +a.p[1] || 0, +a.p[2] || 0);
  o.rotation.set((+a.r[0] || 0) * DEG, (+a.r[1] || 0) * DEG, (+a.r[2] || 0) * DEG, 'YXZ');
  o.scale.set(+a.s[0] || 1e-4, +a.s[1] || 1e-4, +a.s[2] || 1e-4);
}
// one level → a THREE scene (actors nested by parent)
export function buildLevel(project, level, scene, editor) {
  const map = {};
  for (const a of level.actors) map[a.id] = buildActor(project, a, editor);
  for (const a of level.actors) (a.parent && map[a.parent] ? map[a.parent] : scene).add(map[a.id]);
  return map;
}
export function applyWorld(scene, renderer, w) {
  w = w || {};
  scene.background = new THREE.Color(w.sky || '#8fb7e0');
  scene.fog = w.fog ? new THREE.FogExp2(w.fogColor || w.sky || '#8fb7e0', +w.fogDensity || 0.01) : null;
  let amb = scene.getObjectByName('__ambient');
  if (!amb) { amb = new THREE.HemisphereLight('#ffffff', '#445566', 1); amb.name = '__ambient'; amb.userData.editorOnly = false; scene.add(amb); }
  amb.color.set(w.skyLight || '#dfeaff'); amb.groundColor.set(w.groundLight || '#3a3f4a'); amb.intensity = w.ambient == null ? 0.8 : +w.ambient;
  if (renderer) renderer.toneMappingExposure = w.exposure == null ? 1 : +w.exposure;
}
export function makeRenderer(canvas) {
  const r = new THREE.WebGLRenderer({ canvas, antialias: true, preserveDrawingBuffer: true });
  r.setPixelRatio(Math.min(devicePixelRatio, 2)); r.shadowMap.enabled = true; r.shadowMap.type = THREE.PCFSoftShadowMap;
  r.toneMapping = THREE.ACESFilmicToneMapping; r.outputColorSpace = THREE.SRGBColorSpace;
  return r;
}

/* ------------------------------------------------------------------ scripts */
export const SCRIPT_TEMPLATE = `// Runs once when the game starts.
function onStart() {
  log('Hello from ' + self.name);
}

// Runs every frame. dt = seconds since the last frame.
function onUpdate(dt) {
  self.rotation.y += dt;            // spin slowly
}

// Runs when the player walks into this actor (Trigger Boxes and meshes).
function onOverlap(other) {
  // world.message('You found ' + self.name + '!');
}
`;
export function compileScript(code) {
  // eslint-disable-next-line no-new-func
  return new Function('self', 'world', 'input', 'log', 'THREE', 'math',
    `"use strict";\n${code}\n;return { onStart: typeof onStart === 'function' ? onStart : null, onUpdate: typeof onUpdate === 'function' ? onUpdate : null,
      onOverlap: typeof onOverlap === 'function' ? onOverlap : null, onHit: typeof onHit === 'function' ? onHit : null, onKey: typeof onKey === 'function' ? onKey : null };`);
}
export function checkScript(code) { try { compileScript(code); return null; } catch (e) { return e.message; } }

/* ------------------------------------------------------------------ the game */
const KEYALIAS = k => { k = String(k); if (/^[a-z]$/i.test(k)) return 'Key' + k.toUpperCase(); if (/^[0-9]$/.test(k)) return 'Digit' + k;
  return ({ space: 'Space', shift: 'ShiftLeft', ctrl: 'ControlLeft', up: 'ArrowUp', down: 'ArrowDown', left: 'ArrowLeft', right: 'ArrowRight', enter: 'Enter', escape: 'Escape' })[k.toLowerCase()] || k; };

export class Game {
  constructor(opts) {
    this.o = opts; this.project = opts.project; this.level = JSON.parse(JSON.stringify(opts.project.levels[opts.levelName || opts.project.startLevel]));
    this.canvas = opts.canvas; this.log = opts.log || ((...a) => console.log(...a));
    this.keys = new Set(); this.pressed = new Set(); this.mouse = { dx: 0, dy: 0, left: false, right: false };
    this.running = false; this.time = 0; this.hud = opts.hud;
  }
  start() {
    const P = this.project, W = this.level.world || {};
    this.renderer = this.o.renderer || makeRenderer(this.canvas);
    this.scene = new THREE.Scene(); applyWorld(this.scene, this.renderer, W);
    this.camera = new THREE.PerspectiveCamera(W.fov || 75, 1, 0.05, 2000);
    this.objects = buildLevel(P, this.level, this.scene, false);
    this.byId = {}; for (const a of this.level.actors) this.byId[a.id] = a;
    // physics
    this.world = new CANNON.World({ gravity: new CANNON.Vec3(0, W.gravity == null ? -9.81 : +W.gravity, 0) });
    this.world.broadphase = new CANNON.SAPBroadphase(this.world); this.world.allowSleep = true;
    this.world.defaultContactMaterial.friction = 0.4; this.world.defaultContactMaterial.restitution = 0.1;
    this.bodies = new Map();
    this.scene.updateMatrixWorld(true);
    for (const a of this.level.actors) if (a.type === 'StaticMesh' && a.physics && (a.physics.collision !== false || a.physics.simulate)) this.addBody(a);
    // player
    this.setupPlayer(W.gameMode || P.gameMode || 'FirstPerson');
    // scripts
    this.behaviours = [];
    for (const a of this.level.actors) if (a.script) this.attachScript(a, a.script);
    // input
    this._kd = e => { if (!this.running) return; if (['Space', 'ArrowUp', 'ArrowDown'].includes(e.code)) e.preventDefault(); if (!this.keys.has(e.code)) this.pressed.add(e.code); this.keys.add(e.code);
      for (const b of this.behaviours) if (b.fn.onKey) this.safe(b, 'onKey', e.code); };
    this._ku = e => this.keys.delete(e.code);
    this._mm = e => { if (document.pointerLockElement === this.canvas || this.mouse.right) { this.mouse.dx += e.movementX; this.mouse.dy += e.movementY; } };
    this._md = e => { if (e.button === 0) this.mouse.left = true; if (e.button === 2) this.mouse.right = true;
      if (this.mode !== 'Spectator' && document.pointerLockElement !== this.canvas && this.canvas.requestPointerLock) { try { this.canvas.requestPointerLock(); } catch (er) {} } };
    this._mu = e => { if (e.button === 0) this.mouse.left = false; if (e.button === 2) this.mouse.right = false; };
    this._cm = e => e.preventDefault();
    addEventListener('keydown', this._kd); addEventListener('keyup', this._ku); addEventListener('mousemove', this._mm);
    this.canvas.addEventListener('mousedown', this._md); addEventListener('mouseup', this._mu); this.canvas.addEventListener('contextmenu', this._cm);
    this.running = true; this.last = performance.now();
    for (const b of this.behaviours) this.safe(b, 'onStart');
    const loop = () => { if (!this.running) return; this.raf = requestAnimationFrame(loop); this.frame(); };
    this.raf = requestAnimationFrame(loop);
    return this;
  }
  stop() {
    this.running = false; cancelAnimationFrame(this.raf);
    removeEventListener('keydown', this._kd); removeEventListener('keyup', this._ku); removeEventListener('mousemove', this._mm);
    this.canvas.removeEventListener('mousedown', this._md); removeEventListener('mouseup', this._mu); this.canvas.removeEventListener('contextmenu', this._cm);
    if (document.pointerLockElement === this.canvas) document.exitPointerLock();
    this.scene.traverse(o => { if (o.isMesh && o.material && o.material.map) o.material.map.dispose(); });
    if (this.hud) this.hud.innerHTML = '';
  }
  shapeFor(a, o) {
    const s = new THREE.Vector3(); o.getWorldScale(s);
    switch (a.mesh) {
      case 'Sphere': return new CANNON.Sphere(0.5 * Math.max(s.x, s.y, s.z));
      case 'Cylinder': case 'Cone': case 'Capsule': return new CANNON.Cylinder(0.45 * Math.max(s.x, s.z) * (a.mesh === 'Cone' ? 0.6 : 1), 0.45 * Math.max(s.x, s.z), (a.mesh === 'Capsule' ? 1.8 : 1) * s.y, 16);
      case 'Plane': return new CANNON.Box(new CANNON.Vec3(0.5 * s.x, 0.02, 0.5 * s.z));
      case 'Torus': return new CANNON.Box(new CANNON.Vec3(0.5 * s.x, 0.5 * s.y, 0.12 * s.z));
      default: return new CANNON.Box(new CANNON.Vec3(0.5 * s.x, 0.5 * s.y, 0.5 * s.z));
    }
  }
  addBody(a) {
    const o = this.objects[a.id]; if (!o) return;
    const dyn = !!(a.physics && a.physics.simulate);
    const b = new CANNON.Body({ mass: dyn ? Math.max(0.01, +a.physics.mass || 1) : 0, shape: this.shapeFor(a, o) });
    const wp = new THREE.Vector3(), wq = new THREE.Quaternion(); o.getWorldPosition(wp); o.getWorldQuaternion(wq);
    b.position.set(wp.x, wp.y, wp.z); b.quaternion.set(wq.x, wq.y, wq.z, wq.w);
    if (dyn && o.parent !== this.scene) { this.scene.attach(o); }   // simulated actors detach from their parent
    b.userData = { actor: a.id }; b.addEventListener('collide', e => { const other = e.body.userData && e.body.userData.actor; for (const bh of this.behaviours) if (bh.actor.id === a.id && bh.fn.onHit) this.safe(bh, 'onHit', other ? this.api(this.byId[other]) : (e.body === this.playerBody ? this.playerApi : null)); });
    this.world.addBody(b); this.bodies.set(a.id, { body: b, dyn });
  }
  setupPlayer(mode) {
    this.mode = mode; const ps = this.level.actors.find(a => a.type === 'PlayerStart');
    const p = ps ? this.objects[ps.id].getWorldPosition(new THREE.Vector3()) : new THREE.Vector3(0, 2, 5);
    this.yaw = ps ? (+ps.r[1] || 0) * DEG : 0; this.pitch = 0;
    if (mode === 'Spectator') { this.camera.position.copy(p).add(new THREE.Vector3(0, 1.6, 0)); this.playerBody = null; }
    else {
      const b = new CANNON.Body({ mass: 70, fixedRotation: true, linearDamping: 0.05, material: new CANNON.Material({ friction: 0 }) });
      b.addShape(new CANNON.Sphere(0.35), new CANNON.Vec3(0, 0.35, 0)); b.addShape(new CANNON.Sphere(0.35), new CANNON.Vec3(0, 1.25, 0)); b.addShape(new CANNON.Cylinder(0.35, 0.35, 0.9, 12), new CANNON.Vec3(0, 0.8, 0));
      b.position.set(p.x, p.y - 0.9, p.z); b.userData = { player: true }; this.world.addBody(b); this.playerBody = b;
      if (mode === 'ThirdPerson') { const m = new THREE.Mesh(geometry('Capsule'), new THREE.MeshStandardMaterial({ color: '#3a7bff', roughness: 0.5 })); m.castShadow = true; this.scene.add(m); this.playerMesh = m; }
    }
    const self = this;
    this.playerApi = { name: 'Player', isPlayer: true,
      get position() { return self.playerBody ? new THREE.Vector3(self.playerBody.position.x, self.playerBody.position.y + 0.9, self.playerBody.position.z) : self.camera.position.clone(); },
      teleport(x, y, z) { if (self.playerBody) { self.playerBody.position.set(x, y - 0.9, z); self.playerBody.velocity.set(0, 0, 0); } else self.camera.position.set(x, y, z); },
      launch(x, y, z) { if (self.playerBody) self.playerBody.velocity.set(x, y, z); } };
  }
  attachScript(a, sid) {
    const s = (this.project.assets.scripts || {})[sid]; if (!s) { this.log(`warning: ${a.name}: script "${sid}" not found`, 'warn'); return; }
    let fn; try { fn = compileScript(s.code)(this.api(a), this.worldApi(), this.inputApi(), (...m) => this.log(`[${a.name}] ` + m.map(x => typeof x === 'object' ? JSON.stringify(x) : String(x)).join(' ')), THREE, MATH); }
    catch (e) { this.log(`error: ${s.name}: ${e.message}`, 'error'); return; }
    this.behaviours.push({ actor: a, script: s, fn, inside: false });
  }
  safe(b, k, ...args) { if (typeof b.fn[k] !== 'function') return; try { b.fn[k](...args); } catch (e) { this.log(`error: ${b.script.name}.${k} on ${b.actor.name}: ${e.message}`, 'error'); b.fn[k] = null; } }
  api(a) {
    if (!a) return null; if (a._api) return a._api;
    const self = this, o = () => self.objects[a.id];
    return (a._api = { id: a.id, name: a.name, tags: a.tags || [], get object3D() { return o(); },
      get position() { return o().position; }, get rotation() { return o().rotation; }, get scale() { return o().scale; },
      get visible() { return o().visible; }, set visible(v) { o().visible = !!v; },
      setColor(c) { o().traverse(x => { if (x.isMesh && x.material.color) { x.material = x.material.clone(); x.material.color.set(c); } }); },
      setText(t) { const g = o(); if (a.type !== 'TextRender') return; g.clear(); g.add(textSprite({ ...(a.text || {}), value: String(t) })); },
      addForce(x, y, z) { const b = self.bodies.get(a.id); if (b) b.body.applyImpulse(new CANNON.Vec3(x, y, z)); },
      setVelocity(x, y, z) { const b = self.bodies.get(a.id); if (b) b.body.velocity.set(x, y, z); },
      get velocity() { const b = self.bodies.get(a.id); return b ? new THREE.Vector3(b.body.velocity.x, b.body.velocity.y, b.body.velocity.z) : new THREE.Vector3(); },
      distanceTo(other) { const p = other && other.position ? other.position : other; return o().getWorldPosition(new THREE.Vector3()).distanceTo(p); },
      lookAt(x, y, z) { const p = typeof x === 'object' ? x : new THREE.Vector3(x, y, z); o().lookAt(p); },
      destroy() { self.destroy(a.id); } });
  }
  worldApi() {
    const self = this;
    return this._w || (this._w = {
      vars: {}, get time() { return self.time; }, get player() { return self.playerApi; },
      find(name) { const a = self.level.actors.find(x => x.name === name); return a ? self.api(a) : null; },
      findAll(tag) { return self.level.actors.filter(x => (x.tags || []).includes(tag) || x.name === tag).map(x => self.api(x)); },
      spawn(mesh, o = {}) { const a = newActor('StaticMesh', { mesh: MESHES.includes(mesh) ? mesh : 'Cube', name: o.name || mesh, p: o.position || [0, 1, 0], s: o.scale || [1, 1, 1], material: o.material || 'M_Default' });
        if (o.physics) a.physics.simulate = true; self.level.actors.push(a); self.byId[a.id] = a; const ob = buildActor(self.project, a, false); self.scene.add(ob); self.objects[a.id] = ob;
        if (o.color) self.api(a).setColor(o.color); if (a.physics.collision !== false) self.addBody(a); return self.api(a); },
      destroy(x) { self.destroy(x && x.id ? x.id : x); },
      message(t, sec = 3) { if (!self.hud) return; let m = self.hud.querySelector('.pge-msg'); if (!m) { m = document.createElement('div'); m.className = 'pge-msg'; self.hud.appendChild(m); } m.textContent = t; m.style.opacity = 1; clearTimeout(self._mt); self._mt = setTimeout(() => m.style.opacity = 0, sec * 1000); },
      hud(id, t) { if (!self.hud) return; let m = self.hud.querySelector(`[data-hud="${id}"]`); if (!m) { m = document.createElement('div'); m.className = 'pge-hud'; m.dataset.hud = id; self.hud.appendChild(m); } m.textContent = t; },
      restart() { self.restartRequested = true; },
      setGravity(y) { self.world.gravity.set(0, y, 0); }
    });
  }
  inputApi() { const self = this; return this._i || (this._i = { key(k) { return self.keys.has(KEYALIAS(k)); }, pressed(k) { return self.pressed.has(KEYALIAS(k)); }, get mouse() { return self.mouse; } }); }
  destroy(id) { const o = this.objects[id]; if (o) o.removeFromParent(); const b = this.bodies.get(id); if (b) { this.world.removeBody(b.body); this.bodies.delete(id); }
    this.behaviours = this.behaviours.filter(x => x.actor.id !== id); delete this.objects[id]; this.level.actors = this.level.actors.filter(a => a.id !== id); }
  resize() { const w = this.canvas.clientWidth, h = this.canvas.clientHeight; if (!w || !h) return; this.renderer.setSize(w, h, false); this.camera.aspect = w / h; this.camera.updateProjectionMatrix(); }
  frame() {
    const now = performance.now(), dt = Math.min(0.05, (now - this.last) / 1000); this.last = now; this.time += dt;
    this.resize();
    // look
    const sens = 0.0022; this.yaw -= this.mouse.dx * sens; this.pitch = Math.max(-1.45, Math.min(1.45, this.pitch - this.mouse.dy * sens)); this.mouse.dx = this.mouse.dy = 0;
    const fwd = new THREE.Vector3(-Math.sin(this.yaw), 0, -Math.cos(this.yaw)), right = new THREE.Vector3(-fwd.z, 0, fwd.x);
    const k = c => this.keys.has(c), mv = new THREE.Vector3();
    if (k('KeyW') || k('ArrowUp')) mv.add(fwd); if (k('KeyS') || k('ArrowDown')) mv.sub(fwd); if (k('KeyD') || k('ArrowRight')) mv.add(right); if (k('KeyA') || k('ArrowLeft')) mv.sub(right);
    if (mv.lengthSq()) mv.normalize();
    const speed = (k('ShiftLeft') || k('ShiftRight')) ? 9 : 5;
    if (this.mode === 'Spectator') {
      if (k('KeyE')) mv.y += 1; if (k('KeyQ')) mv.y -= 1;
      this.camera.position.addScaledVector(mv, speed * 1.6 * dt);
      this.camera.rotation.set(this.pitch, this.yaw, 0, 'YXZ');
    } else {
      const b = this.playerBody;
      b.velocity.x = mv.x * speed; b.velocity.z = mv.z * speed;
      // on the ground? cast a short ray down
      const from = new CANNON.Vec3(b.position.x, b.position.y + 0.1, b.position.z), to = new CANNON.Vec3(b.position.x, b.position.y - 0.15, b.position.z), r = new CANNON.RaycastResult();
      const grounded = this.world.raycastClosest(from, to, { skipBackfaces: true, collisionFilterMask: -1 }, r) && r.body !== b;
      if (grounded && this.pressed.has('Space')) b.velocity.y = 5.2;
      const eye = new THREE.Vector3(b.position.x, b.position.y + 1.6, b.position.z);
      if (this.mode === 'ThirdPerson') {
        this.playerMesh.position.set(b.position.x, b.position.y + 0.9, b.position.z); if (mv.lengthSq()) this.playerMesh.rotation.y = Math.atan2(-mv.x, -mv.z);
        const back = new THREE.Vector3(Math.sin(this.yaw) * Math.cos(this.pitch), -Math.sin(this.pitch), Math.cos(this.yaw) * Math.cos(this.pitch)).multiplyScalar(4.5);
        this.camera.position.copy(eye).add(back).add(new THREE.Vector3(0, 0.3, 0)); this.camera.lookAt(eye);
      } else { this.camera.position.copy(eye); this.camera.rotation.set(this.pitch, this.yaw, 0, 'YXZ'); }
      const killZ = (this.level.world || {}).killZ; if (killZ != null && b.position.y < +killZ) { this.log('Player fell below Kill Z: respawn'); const ps = this.level.actors.find(a => a.type === 'PlayerStart'); const p = ps ? this.objects[ps.id].getWorldPosition(new THREE.Vector3()) : new THREE.Vector3(0, 2, 0); b.position.set(p.x, p.y - 0.9, p.z); b.velocity.set(0, 0, 0); }
    }
    // physics
    this.world.step(1 / 60, dt, 4);
    for (const [id, { body, dyn }] of this.bodies) if (dyn) { const o = this.objects[id]; if (o) { o.position.set(body.position.x, body.position.y, body.position.z); o.quaternion.set(body.quaternion.x, body.quaternion.y, body.quaternion.z, body.quaternion.w); } }
    // scripts
    for (const b of this.behaviours) if (b.fn.onUpdate) this.safe(b, 'onUpdate', dt);
    // overlaps with the player
    const pp = this.playerApi.position, pbox = new THREE.Box3().setFromCenterAndSize(pp, new THREE.Vector3(0.7, 1.8, 0.7));
    for (const b of this.behaviours) { if (!b.fn.onOverlap) continue; const o = this.objects[b.actor.id]; if (!o) continue;
      const box = b.actor.type === 'TriggerBox' ? new THREE.Box3().setFromCenterAndSize(o.getWorldPosition(new THREE.Vector3()), o.getWorldScale(new THREE.Vector3())) : new THREE.Box3().setFromObject(o).expandByScalar(0.05);
      const inside = box.intersectsBox(pbox); if (inside && !b.inside) this.safe(b, 'onOverlap', this.playerApi); b.inside = inside; }
    this.pressed.clear();
    if (this.restartRequested) { this.restartRequested = false; this.stop(); Object.assign(this, new Game(this.o)); this.start(); return; }
    this.renderer.render(this.scene, this.camera);
  }
}
export const MATH = { clamp: (v, a, b) => Math.max(a, Math.min(b, v)), lerp: (a, b, t) => a + (b - a) * t, random: (a = 0, b = 1) => a + Math.random() * (b - a), vec3: (x, y, z) => new THREE.Vector3(x, y, z) };
