#include <filesystem>
#include <fstream>
#include "script.h"
#include "log.h"
#include <quickjs.h>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <map>
#include <cmath>
#include "voxel.h"
#include "ui.h"
#include "audio.h"
#include "models.h"
#include "project.h"
#include <nlohmann/json.hpp>

// text form of binary data (a saved block world sent over the network)
static std::string base64Encode(const std::string& in) { static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; std::string out; out.reserve((in.size() + 2) / 3 * 4);
  size_t i = 0; for (; i + 2 < in.size(); i += 3) { uint32_t n = ((uint8_t)in[i] << 16) | ((uint8_t)in[i + 1] << 8) | (uint8_t)in[i + 2]; out += T[n >> 18]; out += T[(n >> 12) & 63]; out += T[(n >> 6) & 63]; out += T[n & 63]; }
  if (i < in.size()) { uint32_t n = (uint8_t)in[i] << 16; if (i + 1 < in.size()) n |= (uint8_t)in[i + 1] << 8; out += T[n >> 18]; out += T[(n >> 12) & 63]; out += i + 1 < in.size() ? T[(n >> 6) & 63] : '='; out += '='; }
  return out; }
static bool base64Decode(const std::string& in, std::string& out) { int v[256]; for (int& x : v) x = -1; const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; for (int i = 0; i < 64; i++) v[(uint8_t)T[i]] = i;
  out.clear(); uint32_t acc = 0; int bits = 0; for (char ch : in) { if (ch == '=' || ch == '\n' || ch == '\r') continue; int x = v[(uint8_t)ch]; if (x < 0) return false; acc = (acc << 6) | x; bits += 6; if (bits >= 8) { bits -= 8; out += (char)((acc >> bits) & 255); } }
  return true; }
namespace pge {

std::string keyAlias(const std::string& k0) {
  std::string k = k0;
  if (k.size() == 1 && std::isalpha((unsigned char)k[0])) return std::string("Key") + (char)std::toupper((unsigned char)k[0]);
  if (k.size() == 1 && std::isdigit((unsigned char)k[0])) return "Digit" + k;
  std::string l; for (char c : k) l += (char)std::tolower((unsigned char)c);
  static const std::map<std::string, std::string> M = {{"space", "Space"}, {"shift", "ShiftLeft"}, {"ctrl", "ControlLeft"}, {"control", "ControlLeft"}, {"alt", "AltLeft"},
    {"up", "ArrowUp"}, {"down", "ArrowDown"}, {"left", "ArrowLeft"}, {"right", "ArrowRight"}, {"enter", "Enter"}, {"escape", "Escape"}, {"esc", "Escape"}, {"tab", "Tab"}};
  auto it = M.find(l); return it != M.end() ? it->second : k;
}

static const char* PRELUDE = R"JS(
"use strict";
const __n = globalThis.__n;
class Vec3 {
  constructor(x = 0, y = 0, z = 0) { this.x = +x; this.y = +y; this.z = +z; }
  set(x, y, z) { this.x = +x; this.y = +y; this.z = +z; return this; }
  copy(v) { return this.set(v.x, v.y, v.z); }
  clone() { return new Vec3(this.x, this.y, this.z); }
  add(v) { return this.set(this.x + v.x, this.y + v.y, this.z + v.z); }
  sub(v) { return this.set(this.x - v.x, this.y - v.y, this.z - v.z); }
  multiplyScalar(s) { return this.set(this.x * s, this.y * s, this.z * s); }
  addScaledVector(v, s) { return this.set(this.x + v.x * s, this.y + v.y * s, this.z + v.z * s); }
  lerp(v, t) { return this.set(this.x + (v.x - this.x) * t, this.y + (v.y - this.y) * t, this.z + (v.z - this.z) * t); }
  dot(v) { return this.x * v.x + this.y * v.y + this.z * v.z; }
  lengthSq() { return this.x * this.x + this.y * this.y + this.z * this.z; }
  length() { return Math.sqrt(this.lengthSq()); }
  normalize() { const l = this.length() || 1; return this.multiplyScalar(1 / l); }
  distanceTo(v) { return Math.hypot(this.x - v.x, this.y - v.y, this.z - v.z); }
  toArray() { return [this.x, this.y, this.z]; }
  toString() { return `(${this.x.toFixed(2)}, ${this.y.toFixed(2)}, ${this.z.toFixed(2)})`; }
}
// a live vector: reading/writing x, y, z goes straight to the actor
class Vec3Ref {
  constructor(id, f) { this._id = id; this._f = f; }
  get x() { return __n.getc(this._id, this._f, 0); } set x(v) { __n.setc(this._id, this._f, 0, +v); }
  get y() { return __n.getc(this._id, this._f, 1); } set y(v) { __n.setc(this._id, this._f, 1, +v); }
  get z() { return __n.getc(this._id, this._f, 2); } set z(v) { __n.setc(this._id, this._f, 2, +v); }
  set(x, y, z) { __n.setv(this._id, this._f, +x, +y, +z); return this; }
  copy(v) { return this.set(v.x, v.y, v.z); }
  clone() { const a = __n.getv(this._id, this._f); return new Vec3(a[0], a[1], a[2]); }
  add(v) { return this.copy(this.clone().add(v)); }
  sub(v) { return this.copy(this.clone().sub(v)); }
  multiplyScalar(s) { return this.copy(this.clone().multiplyScalar(s)); }
  addScaledVector(v, s) { return this.copy(this.clone().addScaledVector(v, s)); }
  lerp(v, t) { return this.copy(this.clone().lerp(v, t)); }
  length() { return this.clone().length(); }
  distanceTo(v) { return this.clone().distanceTo(v); }
  toArray() { return this.clone().toArray(); }
  toString() { return this.clone().toString(); }
  toJSON() { return this.toArray(); }
}
const __xyz = (x, y, z) => Array.isArray(x) ? [+x[0] || 0, +x[1] || 0, +x[2] || 0] : (x !== null && typeof x === 'object') ? [+x.x || 0, +x.y || 0, +x.z || 0] : [+x || 0, +y || 0, +z || 0];   // a Vec3, { x, y, z }, [x, y, z] or x, y, z
const __apis = new Map();
const __player = { name: 'Player', isPlayer: true, tags: [],
  get position() { const a = __n.playerPos(); return new Vec3(a[0], a[1], a[2]); },
  teleport(x, y, z) { __n.teleport(...__xyz(x, y, z)); },
  launch(x, y, z) { __n.launch(...__xyz(x, y, z)); },
  get swimming() { return __n.swimming(); },
  // movement: fly, walkSpeed, runSpeed, sneakSpeed, jumpSpeed, flySpeed, flyRun, flyClimb, gravity, accel, runKey, sneakKey, doubleTapRun; and yaw, pitch, grounded, running, sneaking
  get settings() { return JSON.parse(__n.playerGet()); }, set(o) { __n.playerSet(JSON.stringify(o || {})); },
  get fly() { return !!this.settings.fly; }, set fly(v) { this.set({ fly: !!v }); },
  get yaw() { return this.settings.yaw; }, set yaw(v) { this.set({ yaw: +v }); }, get pitch() { return this.settings.pitch; }, set pitch(v) { this.set({ pitch: +v }); },
  get grounded() { return !!this.settings.grounded; }, get running() { return !!this.settings.running; }, get sneaking() { return !!this.settings.sneaking; },
  get velocity() { const v = this.settings.velocity || [0, 0, 0]; return new Vec3(v[0], v[1], v[2]); },
  // health (100 at the start): damage() / heal(); onPlayerDamage(amount, info) and onPlayerDeath() in every script; with no onPlayerDeath the player starts again at the Player Start
  get health() { return this.settings.health; }, set health(v) { this.set({ health: +v }); },
  get maxHealth() { return this.settings.maxHealth; }, set maxHealth(v) { this.set({ maxHealth: +v }); },
  get dead() { return !!this.settings.dead; },
  damage(amount, o) { world.applyDamage(this, amount, o); }, heal(amount) { this.set({ health: this.settings.health + (+amount || 0) }); },
  // cars: the one the player drives (or null); enterVehicle(car), exitVehicle(); E (or the controller's Y) gets in and out by itself
  get vehicle() { const v = this.settings.vehicle; return v ? __api(v) : null; },
  enterVehicle(v) { return !!v && __n.gp('vehicle', v.id, JSON.stringify({ enter: true })) !== ''; }, exitVehicle() { const v = this.settings.vehicle; if (v) __n.gp('vehicle', v, JSON.stringify({ exit: true })); } };
// values for the gameplay calls: vectors → [x, y, z], actors → their id, the player → '@player'
const __gpv = v => { if (v === null || v === undefined) return v; if (Array.isArray(v)) return v.map(__gpv);
  if (typeof v === 'object') { if (v.isPlayer) return '@player'; if (typeof v.id === 'string' && 'position' in v) return v.id; if ('x' in v && 'y' in v && 'z' in v) return [+v.x || 0, +v.y || 0, +v.z || 0];
    const r = {}; for (const k in v) r[k] = __gpv(v[k]); return r; } return v; };
globalThis.__info = s => { let o = {}; try { o = JSON.parse(s); } catch (e) {} const V = a => Array.isArray(a) ? new Vec3(a[0], a[1], a[2]) : a;
  if (o.point) o.point = V(o.point); if (o.direction) o.direction = V(o.direction); o.from = o.from ? __api(o.from) : null; return o; };
const __joint = j => ({ id: j, get alive() { return __n.gp('jointGet', j, '') !== ''; }, get value() { const r = __n.gp('jointGet', j, ''); return r ? JSON.parse(r).value : 0; },
  setMotor(speed, force) { __n.gp('jointSet', j, JSON.stringify({ motorSpeed: +speed || 0, motorForce: force === undefined ? 1e5 : +force })); },
  setTarget(v, force) { __n.gp('jointSet', j, JSON.stringify({ target: +v || 0, motorForce: force === undefined ? 1e5 : +force })); },
  remove() { __n.gp('jointSet', j, JSON.stringify({ remove: true })); } });
const __grab = g => ({ id: g, get active() { return __n.gp('grabGet', g, '') === '1'; },
  moveTo(p, strength) { __n.gp('grabMove', g, JSON.stringify({ target: __gpv(p), strength: strength === undefined ? 1 : +strength })); },
  release(vel) { __n.gp('release', g, JSON.stringify({ velocity: vel ? __gpv(vel) : null })); } });
const __veh = (id, s) => ({ speed: s.speed * 3.6, speedMs: s.speed, rpm: s.rpm, gear: s.gear, grounded: s.grounded, driver: s.driver ? __player : null,
  setInput(throttle, steer, brake, handbrake) { __n.gp('vehicle', id, JSON.stringify({ input: [+throttle || 0, +steer || 0, +brake || 0, +handbrake || 0] })); },
  enter() { return __n.gp('vehicle', id, JSON.stringify({ enter: true })) !== ''; }, exit() { __n.gp('vehicle', id, JSON.stringify({ exit: true })); } });
// the game's own saved values (Documents/PremiumEngine/Saves/<game>/store): text or JSON by name
const storage = { get(k) { const r = __n.storage('get', String(k), ''); return r === '\x01' ? null : r; }, set(k, v) { __n.storage('set', String(k), String(v)); },
  remove(k) { __n.storage('remove', String(k), ''); }, keys(prefix) { return JSON.parse(__n.storage('keys', String(prefix || ''), '')); },
  getJSON(k, d) { const r = this.get(k); if (r === null) return d === undefined ? null : d; try { return JSON.parse(r); } catch (e) { return d === undefined ? null : d; } },
  setJSON(k, v) { this.set(k, JSON.stringify(v)); } };
globalThis.storage = storage;
// playing together: one hosts, the others join (address or LAN); messages are any JSON value (or text)
const net = { host(o) { return __n.net('host', JSON.stringify(o || {}), '') === '1'; }, join(address, port) { return __n.net('join', String(address), String(port || 46600)) === '1'; },
  stop() { __n.net('stop', '', ''); }, send(data, to) { return __n.net('send', typeof data === 'string' ? data : JSON.stringify(data), String(to == null ? -1 : to)) === '1'; },
  kick(id) { __n.net('kick', String(id), ''); }, receive() { return JSON.parse(__n.net('take', '', '')); }, searchLan(on) { __n.net('lan', on === false ? '0' : '1', ''); },
  get lan() { return JSON.parse(__n.net('list', '', '')); }, setInfo(info, players) { __n.net('info', String(info || ''), String(players || 0)); },
  get state() { return JSON.parse(__n.net('state', '', '')); }, get role() { return this.state.role; }, get id() { return this.state.id; }, get peers() { return this.state.peers; },
  get connected() { return this.state.connected; }, get error() { return this.state.error; } };
globalThis.net = net;
function __api(id) {
  if (!id) return null; if (id === '@player') return __player;
  let a = __apis.get(id); if (a) return a;
  const info = JSON.parse(__n.info(id));
  a = { id, name: info.name, type: info.type, tags: info.tags,
    position: new Vec3Ref(id, 'p'), rotation: new Vec3Ref(id, 'r'), scale: new Vec3Ref(id, 's'),
    get worldPosition() { const v = __n.worldPos(id); return new Vec3(v[0], v[1], v[2]); },
    get visible() { return __n.getVisible(id); }, set visible(v) { __n.setVisible(id, !!v); },
    get alive() { return __n.exists(id); },
    setColor(c) { __n.setColor(id, String(c)); },
    setText(t) { __n.setText(id, String(t)); },
    addForce(x, y, z) { __n.addForce(id, ...__xyz(x, y, z)); },
    setVelocity(x, y, z) { __n.setv(id, 'v', ...__xyz(x, y, z)); },
    get velocity() { const v = __n.getv(id, 'v'); return new Vec3(v[0], v[1], v[2]); },
    distanceTo(o) { const p = o && o.isPlayer ? o.position : (o && o.worldPosition) ? o.worldPosition : o; return this.worldPosition.distanceTo(p); },
    lookAt(x, y, z) { __n.lookAt(id, ...__xyz(x, y, z)); },
    // models: playAnimation('Run', { loop: true, speed: 1, blend: 0.2 }), stopAnimation(), animation (what plays), animations (names), bone('Hand_R')
    playAnimation(name, o) { return __n.anim(id, 'play', JSON.stringify(Object.assign({ name: String(name) }, o || {}))) === '1'; },
    stopAnimation() { __n.anim(id, 'stop', ''); },
    get animation() { const r = __n.anim(id, 'state', ''); return r ? JSON.parse(r) : null; },
    get animations() { const r = __n.anim(id, 'list', ''); return r ? JSON.parse(r) : []; },
    set animationSpeed(v) { __n.anim(id, 'speed', String(+v)); },
    setBoneScale(name, s) { return __n.anim(id, 'boneScale', JSON.stringify({ name: String(name), scale: +s })) === '1'; },   // 0 = gone with what hangs on it (gore: a limb shot off)
    bone(name) { const r = __n.anim(id, 'bone', String(name)); if (!r) return null; const b = JSON.parse(r);
      return { position: new Vec3(b.p[0], b.p[1], b.p[2]), rotation: new Vec3(b.r[0], b.r[1], b.r[2]) }; },
    // walking on the level's floors (AI Move To): moveTo(place | actor | world.player, { speed: 3.5, acceptance: 0.5, turn: true }) → false if there's no way
    moveTo(t, o) { let g; if (t && t.isPlayer) g = { player: true }; else if (t && t.id) g = { actor: t.id }; else { const v = Array.isArray(t) ? t : __xyz(t); g = { x: +v[0] || 0, y: +v[1] || 0, z: +v[2] || 0 }; }
      return __n.nav(id, 'move', JSON.stringify(Object.assign(g, o || {}))) === '1'; },
    stopMoving() { __n.nav(id, 'stop', ''); },
    get isMoving() { const r = __n.nav(id, 'state', ''); return r ? JSON.parse(r).moving : false; },
    get moveResult() { const r = __n.nav(id, 'state', ''); return r ? JSON.parse(r).result : ''; },
    // attach to another actor (a bone of its model), the player or the camera ('camera'): { bone: 'Hand_R', keepWorld: false, offset: [x, y, z], rotation: [deg x, y, z] }
    attachTo(parent, o) { const to = parent === 'camera' ? '@camera' : parent && parent.isPlayer ? '@player' : parent && parent.id ? parent.id : String(parent || '');
      return __n.gp('attach', id, JSON.stringify(Object.assign({ to }, __gpv(o || {})))) === '1'; },
    detach(o) { __n.gp('detach', id, JSON.stringify(__gpv(o || {}))); },
    get parent() { const r = __n.gp('parent', id, ''); return r === '@player' ? __player : r === '@camera' ? 'camera' : r ? __api(r) : null; },
    applyDamage(amount, o) { world.applyDamage(this, amount, o); },
    addImpulse(v, at) { __n.gp('impulse', id, JSON.stringify({ impulse: __gpv(v), point: at ? __gpv(at) : null })); },
    // a character model goes limp and falls (its bones become physics bodies); { impulse: [x, y, z], point: [x, y, z] } pushes where it was hit
    ragdoll(o) { return __n.gp('ragdoll', id, JSON.stringify(__gpv(o || {}))) === '1'; },
    get isRagdoll() { return __n.gp('ragdollState', id, '') === '1'; },
    // a car (Vehicle in the Details, or makeVehicle({ torque, maxSteer, wheelRadius, drive: 'all' | 'rear' | 'front' ... }))
    get vehicle() { const r = __n.gp('vehicle', id, ''); return r ? __veh(id, JSON.parse(r)) : null; },
    makeVehicle(o) { return __n.gp('vehicleMake', id, JSON.stringify(o || {})) === '1'; },
    // a Physics Constraint actor's joint (or null)
    get joint() { const r = __n.gp('actorJoint', id, ''); return r ? __joint(r) : null; },
    destroy() { __n.destroy(id); } };
  __apis.set(id, a); return a;
}
// Premium Games: trophies on the player's account, and whether the game runs on the console (from Premium Games' console mode)
const premium = { trophy(id) { __n.trophy(String(id)); }, get store() { return __n.psEnv('PREMIUM_GAMES'); }, get console() { return __n.psEnv('PREMIUM_CONSOLE'); } };
const world = { vars: {}, get voxels() { return voxels; },
  get camera() { const c = __n.camera(); return { position: new Vec3(c[0], c[1], c[2]), forward: new Vec3(c[3], c[4], c[5]) }; },
  get time() { return __n.time(); }, get player() { return __player; },
  find(name) { const id = __n.find(String(name)); return id ? __api(id) : null; },
  findAll(tag) { return __n.findAll(String(tag)).map(__api); },
  spawn(mesh, o = {}) { return __api(__n.spawn(String(mesh), JSON.stringify(__gpv(o || {})))); },
  destroy(x) { __n.destroy(x && x.id ? x.id : String(x)); },
  message(t, sec = 3) { __n.message(String(t), +sec || 3); },
  hud(id, t) { __n.hud(String(id), String(t)); },
  restart() { __n.restart(); },
  setGravity(y) { __n.setGravity(+y); },
  setSky(o) { __n.setSky(JSON.stringify(o || {})); },
  // the way between two places on the level's floors: [Vec3, ...] or null; navigation.rebuild() after the level changed
  findPath(a, b) { const P = x => x && x.isPlayer ? x.position : (x && x.worldPosition) ? x.worldPosition : x; const v = x => Array.isArray(x) ? x : __xyz(P(x));
    const r = __n.nav('', 'path', JSON.stringify({ from: v(a), to: v(b), fromPlayer: !!(a && a.isPlayer), toPlayer: !!(b && b.isPlayer) })); return r ? JSON.parse(r).map(p => new Vec3(p[0], p[1], p[2])) : null; },
  navigation: { rebuild() { __n.nav('', 'rebuild', ''); }, get info() { return JSON.parse(__n.nav('', 'info', '')); } },
  // camera effects: { vignette, grain, chromatic, scanlines, vhs (0..1), saturation, contrast, bloom (1 = as is), temperature (-1 cold .. 1 warm), tint: '#rrggbb' }; null = all off
  setPostProcess(o) { __n.setSky(JSON.stringify({ post: o === null ? 'reset' : (o || {}) })); },
  get postProcess() { return JSON.parse(__n.postGet()); },
  setQuality(o) { __n.setQuality(JSON.stringify(o || {})); },
  // the camera behind a point (a vehicle the scripts drive), turned by the mouse: { target: [x, y, z], distance, height }; null: the player's eyes again
  setCamera(o) { __n.setCamera(o ? JSON.stringify(o) : 'null'); },
  // the first thing on a line (Unreal's Line Trace): { position, normal, distance, actor, player, body, bone } or null; { ignore: [actors], ignorePlayer: true }
  raycast(from, to, o) { const r = __n.gp('raycast', JSON.stringify(__gpv(Object.assign({ from, to }, o || {}))), ''); if (!r) return null; const h = JSON.parse(r);
    return { position: new Vec3(h.p[0], h.p[1], h.p[2]), normal: new Vec3(h.n[0], h.n[1], h.n[2]), distance: h.d, actor: h.a ? __api(h.a) : null, player: !!h.pl, body: h.b, bone: h.bone || '' }; },
  lineTrace(from, to, o) { return this.raycast(from, to, o); },
  // damage: the target's onDamage(amount, { type, from, point, direction }); the player loses health; impulse pushes bodies and ragdolls
  applyDamage(target, amount, o) { __n.gp('damage', JSON.stringify(__gpv(target)), JSON.stringify(__gpv(Object.assign({ amount: +amount || 0 }, o || {})))); },
  applyRadialDamage(center, radius, amount, o) { return JSON.parse(__n.gp('radial', JSON.stringify(__gpv(Object.assign({ center, radius: +radius || 0, amount: +amount || 0 }, o || {}))), '') || '[]').map(__api); },
  // a picture laid on a surface: { type: 'blood' | 'bullet' | 'scorch' | 'splat', texture, size, color, angle, life (s), actor, body }
  spawnDecal(position, normal, o) { __n.gp('decal', JSON.stringify(__gpv(Object.assign({ position, normal }, o || {}))), ''); },
  clearDecals() { __n.gp('decalsClear', '', ''); },
  // two actors held together (or one to the world): { kind: 'Hinge' | 'Fixed' | 'BallSocket' | 'Slider' | 'Distance', a, b, pivot, axis, limits, min, max, motorSpeed, motorForce, breakForce }
  addJoint(o) { const r = __n.gp('joint', JSON.stringify(__gpv(o || {})), ''); return r ? __joint(r) : null; },
  // pick up a body (a box, a ragdoll's arm): grab(hit) or grab(actor, point); then handle.moveTo(place) every frame, release(velocity) to drop or throw it
  grab(target, point, o) { const t = target && target.body !== undefined && target.normal ? { body: target.body, point: __gpv(target.position) } : { actor: __gpv(target), point: point ? __gpv(point) : null };
    const r = __n.gp('grab', JSON.stringify(Object.assign(t, __gpv(o || {}))), ''); return r ? __grab(r) : null; } };
// the block world (BLOCKFALL style): 16 m chunks, 20 cm blocks in the near layer, 1 m blocks above and below
const voxels = { lastError: '',
  create(o = {}) { const e = __n.voxCreate(JSON.stringify(o || {})); this.lastError = e; if (e) __n.log('[voxels] ' + e); return !e; },
  get enabled() { return __n.voxOn(); },
  defineBlock(id, o = {}) { __n.voxDefine(+id, JSON.stringify(o || {})); },
  setGenerator(g, params = {}) { if (typeof g === 'function') __n.voxGenScript(g.toString(), JSON.stringify(params || {})); else __n.voxGenBuiltin(String(g), JSON.stringify(params || {})); },
  // a PNG file, or pixels made by the game: { width, height, pixels (RGBA bytes), tilesPerRow } or { tileSize, tiles: [RGBA bytes of each tile] }
  setAtlas(a, tilesPerRow = 16) { let r;
    if (typeof a === 'string') r = __n.voxAtlas(a, +tilesPerRow || 16);
    else if (a && a.tiles) r = __n.voxAtlasTiles(a.tiles, +a.tileSize || 16, +a.tilesPerRow || 0);
    else if (a && a.pixels) r = __n.voxAtlasPx(a.pixels, +a.width, +a.height, +a.tilesPerRow || +tilesPerRow || 16);
    else r = 'setAtlas wants a file name or { pixels } / { tiles }';
    this.lastError = r === true ? '' : String(r); if (r !== true) __n.log('[voxels] setAtlas: ' + r); return r === true; },
  getBlock(x, y, z) { const r = __n.voxGet(...__xyz(x, y, z)); if (!r) return null;
    const b = { id: r[0], x: r[1], y: r[2], z: r[3], size: r[4], height: r[5], rotation: r[6], shape: r[7] };
    if (r[8]) b.carve = r[8].split('').map(Number); return b; },
  // options: { height: 0.2 .. 1, rotation: 0..11, shape: 'stairs' | 'half' | ..., carve: [125 x 0/1], onlyEmpty: true }
  setBlock(x, y, z, id, size, opts) {
    if (x !== null && typeof x === 'object') { opts = id; size = z; id = y; [x, y, z] = __xyz(x); }
    const r = __n.voxSet(+x, +y, +z, +id, size === undefined ? 0.2 : +size, opts ? JSON.stringify(opts) : ''); this.lastError = r === true ? '' : String(r); return r === true; },
  canPlace(x, y, z, size, height) { if (x !== null && typeof x === 'object') { height = z; size = y; [x, y, z] = __xyz(x); } return __n.voxCanPlace(+x, +y, +z, size === undefined ? 0.2 : +size, +height || 0); },
  removeBlock(x, y, z) { return __n.voxRemove(...__xyz(x, y, z)); },
  carve(x, y, z, i, j, k, solid) { if (x !== null && typeof x === 'object') { solid = k; k = j; j = i; i = z; [x, y, z] = __xyz(x); } return __n.voxCarve(+x, +y, +z, i | 0, j | 0, k | 0, !!solid); },
  liquidAt(x, y, z) { return __n.voxLiquid(...__xyz(x, y, z)); },
  solidAt(x, y, z) { return __n.voxSolid(...__xyz(x, y, z)); },
  boxSolid(min, max) { const a = __xyz(min), b = __xyz(max); return __n.voxBoxSolid(a[0], a[1], a[2], b[0], b[1], b[2]); },
  raycast(from, dir, max = 8, o = {}) { const a = __xyz(from), b = __xyz(dir); const r = __n.voxRay(a[0], a[1], a[2], b[0], b[1], b[2], +max || 8, !!(o && o.liquids)); if (!r) return null;
    return { id: r[0], x: r[1], y: r[2], z: r[3], size: r[4], normal: new Vec3(r[5], r[6], r[7]), point: new Vec3(r[8], r[9], r[10]), distance: r[11], height: r[15],
      placePosition(size = 0.2, height = 0) { const p = __n.voxPlace(...r, +size, +height || 0); return new Vec3(p[0], p[1], p[2]); } }; },
  ready(x, y, z) { return __n.voxReady(...__xyz(x, y, z)); },
  topY(x, z) { return __n.voxTop(+x, +z); },
  // the player's changes, in Documents/PremiumEngine/Saves/<game>/<name>.pgworld
  save(name = 'world') { const r = __n.voxSave(String(name)); this.lastError = r === true ? '' : String(r); return r === true; },
  deleteSave(name = 'world') { return __n.voxDelete(String(name)); },
  setViewDistance(n) { __n.voxView(+n); },
  load(name = 'world') { const r = __n.voxLoad(String(name)); this.lastError = r === true ? '' : String(r); return r === true; },
  saveData() { const r = __n.voxSaveData(); if (r[0] === '!') { this.lastError = r.slice(1); return null; } this.lastError = ''; return r; },
  loadData(text) { const r = __n.voxLoadData(String(text || '')); this.lastError = r === true ? '' : String(r); return r === true; },
  items(list) { __n.voxItems(list instanceof Float32Array ? list : new Float32Array(list || [])); },
  get stats() { return JSON.parse(__n.voxStats()); } };
const input = { key(k) { return __n.key(String(k)); }, pressed(k) { return __n.pressed(String(k)); },
  get mouse() { const m = __n.mouse(); return { dx: m[0], dy: m[1], left: !!m[2], right: !!m[3] }; },
  // the controller: input.axis('LeftX' | 'LeftY' | 'RightX' | 'RightY' | 'LT' | 'RT'), buttons are keys ('PadA' ...)
  axis(n) { return __n.axis(String(n)); },
  get pad() { return __n.padOn(); },
  padSettings(o) { __n.padSet(JSON.stringify(o || {})); },
  rumble(low, high, seconds) { __n.rumble(+low || 0, +high || 0, seconds === undefined ? 0.2 : +seconds); } };
const math = { clamp: (v, a, b) => Math.max(a, Math.min(b, v)), lerp: (a, b, t) => a + (b - a) * t, random: (a = 0, b = 1) => a + Math.random() * (b - a), vec3: (x, y, z) => new Vec3(x, y, z) };
const THREE = { Vector3: Vec3, MathUtils: { clamp: math.clamp, lerp: math.lerp, degToRad: d => d * Math.PI / 180, radToDeg: r => r * 180 / Math.PI } };
globalThis.__make = function (fn, id, name) {
  const log = (...m) => __n.log('[' + name + '] ' + m.map(x => (x !== null && typeof x === 'object') ? JSON.stringify(x) : String(x)).join(' '));
  return fn(__api(id), world, input, log, THREE, math);
};
globalThis.__api = __api;
// ---- game UI: ui.create(type, props) → a node; node.on('click', e => ...); node.show({anim: 'slide'}) ...
const __uiH = new Map();
const __uiJ = p => JSON.stringify(p === undefined ? {} : p);
class UiNode {
  constructor(id) { this.id = id; }
  set(p) { const e = __n.uiSet(this.id, __uiJ(p)); if (e) __n.log('warning: ui: ' + e); if (p && p.on) for (const ev in p.on) this.on(ev, p.on[ev]); return this; }
  add(type, p) { return ui.create(type, Object.assign({}, p || {}, { parent: this.id })); }
  on(ev, fn) { if (typeof fn !== 'function') return this; let h = __uiH.get(this.id); if (!h) __uiH.set(this.id, h = {}); (h[ev] = h[ev] || []).push(fn); __n.uiListen(this.id, String(ev), true); return this; }
  off(ev, fn) { const h = __uiH.get(this.id); if (!h || !h[ev]) return this; h[ev] = fn ? h[ev].filter(f => f !== fn) : []; if (!h[ev].length) { delete h[ev]; __n.uiListen(this.id, String(ev), false); } return this; }
  show(a) { __n.uiShow(this.id, __uiJ(a)); return this; }
  hide(a) { __n.uiHide(this.id, __uiJ(a)); return this; }
  toggle(a) { return this.visible ? this.hide(a) : this.show(a); }
  focus() { __n.uiFocus(this.id); return this; }
  blur() { __n.uiFocus(''); return this; }
  remove() { __uiForget(this.id); __n.uiRemove(this.id); }
  clear() { for (const k of JSON.parse(__n.uiKids(this.id))) __uiForget(k); __n.uiClear(this.id); return this; }
  get info() { return JSON.parse(__n.uiGet(this.id)); }
  get exists() { return __n.uiExists(this.id); }
  get visible() { const i = this.info; return !!i && i.visible; }
  get focused() { const i = this.info; return !!i && i.focused; }
  get value() { const i = this.info; return i ? i.value : undefined; } set value(v) { this.set({ value: v }); }
  get text() { const i = this.info; return i ? i.text : ''; } set text(v) { this.set({ text: String(v) }); }
  get data() { const i = this.info; return i ? i.data : undefined; } set data(v) { this.set({ data: v }); }
  get rect() { const i = this.info; return i ? i.rect : null; }
  get parent() { const i = this.info; return i && i.parent ? new UiNode(i.parent) : null; }
  get children() { return JSON.parse(__n.uiKids(this.id)).map(id => new UiNode(id)); }
}
function __uiForget(id) { for (const k of JSON.parse(__n.uiKids(id))) __uiForget(k); __uiH.delete(id); }
const ui = {
  create(type, p) { p = p || {}; const r = __n.uiCreate(String(type), __uiJ(Object.assign({}, p, { on: undefined }))); if (r === undefined) throw new Error('ui: not available here');
    if (r[0] === '!') throw new Error('ui.create: ' + r.slice(1)); const n = new UiNode(r); if (p.on) for (const ev in p.on) n.on(ev, p.on[ev]); return n; },
  get(id) { return __n.uiExists(String(id)) ? new UiNode(String(id)) : null; },
  get roots() { return JSON.parse(__n.uiKids('')).map(id => new UiNode(id)); },
  set scale(v) { __n.uiScale(+v); },
  set rtl(v) { __n.uiDir(!!v); },
  set escape(v) { __n.uiEscape(!!v); },
  get open() { return __n.uiState()[0]; },
  get paused() { return __n.uiState()[1]; },
  get typing() { return __n.uiState()[2]; },
  get mouse() { const m = __n.uiMouse(); return { x: m[0], y: m[1], w: m[2], h: m[3] }; },
  blur() { __n.uiFocus(''); }
};
globalThis.ui = ui;
// ---- sound: sound.play('sounds/step.ogg', { volume, pitch, position, loop, bus }) → a handle; sounds made by code
const __sndErr = r => { if (typeof r === 'string') { sound.lastError = r; __n.log('warning: sound: ' + r); return 0; } sound.lastError = ''; return r; };
const sound = {
  lastError: '',
  make(name, def) { const r = (def instanceof Float32Array) ? __n.sndSamples(String(name), def, 1, 44100) : __n.sndMake(String(name), JSON.stringify(def)); if (r) { sound.lastError = r; __n.log('warning: sound.make: ' + r); return false; } return true; },
  samples(name, data, opts) { opts = opts || {}; const r = __n.sndSamples(String(name), data, opts.channels || 1, opts.sampleRate || 44100); if (r) { sound.lastError = r; __n.log('warning: sound.samples: ' + r); return false; } return true; },
  play(src, opts) {
    if (opts && opts.follow && typeof opts.follow === 'object') opts = Object.assign({}, opts, { follow: opts.follow.id });
    if (opts && opts.position && typeof opts.position === 'object' && !Array.isArray(opts.position)) opts = Object.assign({}, opts, { position: [opts.position.x, opts.position.y, opts.position.z] });
    if (src instanceof Float32Array) { const h = __n.sndSamples('', src, (opts && opts.channels) || 1, (opts && opts.sampleRate) || 44100); if (typeof h === 'number' && opts) __n.sndSet(h, JSON.stringify(opts)); return __sndErr(h); }
    if (src !== null && typeof src === 'object') return __sndErr(__n.sndPlay(JSON.stringify(src), true, JSON.stringify(opts || {})));
    return __sndErr(__n.sndPlay(String(src), false, JSON.stringify(opts || {}))); },
  set(h, opts) { if (opts && opts.position && !Array.isArray(opts.position) && typeof opts.position === 'object') opts = Object.assign({}, opts, { position: [opts.position.x, opts.position.y, opts.position.z] }); __n.sndSet(+h || 0, JSON.stringify(opts || {})); },
  stop(h, fade) { __n.sndStop(+h || 0, +fade || 0); },
  stopAll(bus) { __n.sndStopAll(bus ? String(bus) : ''); },
  playing(h) { return __n.sndPlaying(+h || 0); },
  has(name) { return __n.sndHas(String(name)); },
  get volume() { return __n.sndVolume('master', undefined); }, set volume(v) { __n.sndVolume('master', +v); },
  setVolume(bus, v) { __n.sndVolume(String(bus), +v); },
  getVolume(bus) { return __n.sndVolume(String(bus), undefined); }
};
globalThis.sound = sound;
// ---- models made by scripts (boxes / vertices, parts that turn about their pivots) and particles
const __v3 = v => (v && typeof v === 'object' && !Array.isArray(v) && 'x' in v) ? [v.x, v.y, v.z] : v;
function __pose(o) { if (!o || typeof o !== 'object') return o; const r = Object.assign({}, o);
  for (const k of ['position', 'rotation', 'scale']) if (k in r) r[k] = __v3(r[k]);
  if (r.parts) { const p = {}; for (const n in r.parts) p[n] = __pose(r.parts[n]); r.parts = p; } return r; }
class Model {
  constructor(id) { this.id = id; }
  set(o) { const e = __n.mdlSet(this.id, JSON.stringify(__pose(o) || {})); if (e) __n.log('warning: model: ' + e); return this; }
  pose(parts) { return this.set({ parts }); }
  get info() { return JSON.parse(__n.mdlGet(this.id)); }
  get exists() { return __n.mdlExists(this.id); }
  get position() { const i = this.info; return i && i.position; } set position(v) { this.set({ position: v }); }
  get rotation() { const i = this.info; return i && i.rotation; } set rotation(v) { this.set({ rotation: v }); }
  get visible() { const i = this.info; return !!i && i.visible; } set visible(v) { this.set({ visible: !!v }); }
  part(name) { const m = __n.mdlPart(this.id, String(name)); if (!m) return null;
    return { matrix: m, position: { x: m[12], y: m[13], z: m[14] }, forward: { x: -m[8], y: -m[9], z: -m[10] }, up: { x: m[4], y: m[5], z: m[6] } }; }
  remove() { __n.mdlRemove(this.id); }
}
const models = {
  lastError: '',
  define(name, def) { const e = __n.mdlDefine(String(name), JSON.stringify(def)); models.lastError = e || ''; if (e) { __n.log('warning: models.define: ' + e); return false; } return true; },
  spawn(name, o) { const r = __n.mdlSpawn(String(name), JSON.stringify(__pose(o) || {})); if (r[0] === '!') { models.lastError = r.slice(1); __n.log('warning: models.spawn: ' + r.slice(1)); return null; } models.lastError = ''; return new Model(r); },
  get(id) { return __n.mdlExists(String(id)) ? new Model(String(id)) : null; }
};
const particles = {
  emit(o) { o = Object.assign({}, o); for (const k of ['position', 'direction', 'velocity', 'area']) if (k in o) o[k] = __v3(o[k]);
    const e = __n.ptEmit(JSON.stringify(o)); if (e) { __n.log('warning: particles.emit: ' + e); return false; } return true; },
  clear() { __n.ptClear(); },
  get count() { return __n.ptCount(); }
};
// files.read('name.json'): a text file shipped with the game (next to the game file), or null
const files = { read(name) { const t = __n.fileRead(String(name)); return t == null ? null : t; }, readJSON(name, d = null) { const t = files.read(name); if (t == null) return d; try { return JSON.parse(t); } catch (e) { return d; } } };
globalThis.files = files;
globalThis.models = models; globalThis.particles = particles; globalThis.Model = Model;
globalThis.__uiDispatch = function (json) {
  for (const e of JSON.parse(json)) {
    let stopped = false; const target = new UiNode(e.id);
    const ev = Object.assign({}, e.data, { type: e.type, target, stop() { stopped = true; } });
    const own = e.type === 'enter' || e.type === 'leave' || e.type === 'focus' || e.type === 'blur';
    for (const id of own ? [e.id] : [e.id, ...e.path]) {
      const h = __uiH.get(id), fs = h && h[e.type];
      if (fs) { ev.current = new UiNode(id);
        for (const f of fs.slice()) { try { f(ev); } catch (err) { const st = err && err.stack ? String(err.stack).split('\n').find(l => l.indexOf(':') > 0) : '';
          __n.uiError('ui "' + e.type + '" on ' + id + ': ' + (err && err.message || err) + (st ? ' (' + st.trim() + ')' : '')); } } }
      if (stopped) break;
    }
  }
};
)JS";

static const char* EVENTS[EV_COUNT] = {"onStart", "onUpdate", "onOverlap", "onHit", "onKey", "onStop", "onAnimationEnd", "onMoveFinished",
                                      "onDamage", "onPlayerDamage", "onPlayerDeath", "onJointBreak", "onVehicleEnter", "onVehicleExit"};

struct Behaviour { std::string actor, actorName, script; JSValue obj; bool ev[EV_COUNT]; bool alive = true; };
struct ScriptHost::Impl {
  JSRuntime* rt = nullptr; JSContext* ctx = nullptr; ScriptWorld* w = nullptr;
  std::vector<Behaviour> b; std::chrono::steady_clock::time_point deadline;
  JSValue make = JS_UNDEFINED, api = JS_UNDEFINED, info = JS_UNDEFINED;
};

static ScriptWorld* W(JSContext* c) { return (ScriptWorld*)JS_GetContextOpaque(c); }
static std::string S(JSContext* c, JSValueConst v) { size_t n = 0; const char* s = JS_ToCStringLen(c, &n, v); if (!s) return {}; std::string r(s, n); JS_FreeCString(c, s); return r; }
static double D(JSContext* c, JSValueConst v) { double d = 0; if (JS_ToFloat64(c, &d, v)) return 0; return d; }
static JSValue A3(JSContext* c, glm::vec3 v) { JSValue a = JS_NewArray(c); for (int i = 0; i < 3; i++) JS_SetPropertyUint32(c, a, i, JS_NewFloat64(c, v[i])); return a; }
static char F(JSContext* c, JSValueConst v) { std::string s = S(c, v); return s.empty() ? 'p' : s[0]; }
static std::string errorText(JSContext* c) {
  JSValue e = JS_GetException(c); std::string msg = S(c, e);
  JSValue st = JS_GetPropertyStr(c, e, "stack"); std::string stack = JS_IsUndefined(st) ? "" : S(c, st); JS_FreeValue(c, st); JS_FreeValue(c, e);
  // "at fn (SC_Name:12:5)" → append " (line 12)"
  auto p = stack.find(':'); if (p != std::string::npos) { size_t q = p + 1; std::string num; while (q < stack.size() && std::isdigit((unsigned char)stack[q])) num += stack[q++]; if (!num.empty()) msg += " (line " + num + ")"; }
  return msg;
}

#define FN(name, argc, ...) JS_SetPropertyStr(c, n, name, JS_NewCFunction(c, +[](JSContext* c, JSValueConst, int, JSValueConst* a) -> JSValue { (void)a; __VA_ARGS__ }, name, argc))
static void bindNatives(JSContext* c) {
  JSValue g = JS_GetGlobalObject(c), n = JS_NewObject(c);
  FN("exists", 1, return JS_NewBool(c, W(c)->exists(S(c, a[0])));) ;
  FN("getc", 3, { int i = (int)D(c, a[2]); return JS_NewFloat64(c, W(c)->getVec(S(c, a[0]), F(c, a[1]))[i < 0 || i > 2 ? 0 : i]); });
  FN("setc", 4, { std::string id = S(c, a[0]); char f = F(c, a[1]); int i = (int)D(c, a[2]); glm::vec3 v = W(c)->getVec(id, f); if (i >= 0 && i < 3) v[i] = (float)D(c, a[3]); W(c)->setVec(id, f, v); return JS_UNDEFINED; });
  FN("getv", 2, return A3(c, W(c)->getVec(S(c, a[0]), F(c, a[1]))););
  FN("setv", 5, W(c)->setVec(S(c, a[0]), F(c, a[1]), {(float)D(c, a[2]), (float)D(c, a[3]), (float)D(c, a[4])}); return JS_UNDEFINED;);
  FN("worldPos", 1, return A3(c, W(c)->worldPos(S(c, a[0]))););
  FN("getVisible", 1, return JS_NewBool(c, W(c)->getVisible(S(c, a[0]))););
  FN("setVisible", 2, W(c)->setVisible(S(c, a[0]), JS_ToBool(c, a[1])); return JS_UNDEFINED;);
  FN("setColor", 2, W(c)->setColor(S(c, a[0]), S(c, a[1])); return JS_UNDEFINED;);
  FN("setText", 2, W(c)->setText(S(c, a[0]), S(c, a[1])); return JS_UNDEFINED;);
  FN("addForce", 4, W(c)->addForce(S(c, a[0]), {(float)D(c, a[1]), (float)D(c, a[2]), (float)D(c, a[3])}); return JS_UNDEFINED;);
  FN("lookAt", 4, W(c)->lookAt(S(c, a[0]), {(float)D(c, a[1]), (float)D(c, a[2]), (float)D(c, a[3])}); return JS_UNDEFINED;);
  FN("destroy", 1, W(c)->destroyActor(S(c, a[0])); return JS_UNDEFINED;);
  FN("nav", 3, { std::string s = W(c)->nav(S(c, a[0]), S(c, a[1]), S(c, a[2])); return JS_NewStringLen(c, s.data(), s.size()); });
  FN("anim", 3, { std::string s = W(c)->animation(S(c, a[0]), S(c, a[1]), S(c, a[2])); return JS_NewStringLen(c, s.data(), s.size()); });
  FN("info", 1, { std::string s = W(c)->actorInfo(S(c, a[0])); return JS_NewStringLen(c, s.data(), s.size()); });
  FN("find", 1, { std::string s = W(c)->find(S(c, a[0])); return s.empty() ? JS_NULL : JS_NewStringLen(c, s.data(), s.size()); });
  FN("findAll", 1, { auto v = W(c)->findAll(S(c, a[0])); JSValue r = JS_NewArray(c); for (size_t i = 0; i < v.size(); i++) JS_SetPropertyUint32(c, r, (uint32_t)i, JS_NewStringLen(c, v[i].data(), v[i].size())); return r; });
  FN("spawn", 2, { std::string s = W(c)->spawn(S(c, a[0]), S(c, a[1])); return JS_NewStringLen(c, s.data(), s.size()); });
  FN("message", 2, W(c)->message(S(c, a[0]), (float)D(c, a[1])); return JS_UNDEFINED;);
  FN("hud", 2, W(c)->hud(S(c, a[0]), S(c, a[1])); return JS_UNDEFINED;);
  FN("restart", 0, W(c)->restart(); return JS_UNDEFINED;);
  FN("net", 3, { std::string r = W(c)->net(S(c, a[0]), S(c, a[1]), S(c, a[2])); return JS_NewStringLen(c, r.data(), r.size()); });
  FN("setQuality", 1, W(c)->setQuality(S(c, a[0])); return JS_UNDEFINED;);
  FN("fileRead", 1, { std::string n = S(c, a[0]); if (n.empty() || n.find("..") != std::string::npos || n[0] == '/' || n[0] == '\\' || n.find(':') != std::string::npos) return JS_NULL;
    std::ifstream f(std::filesystem::u8path(W(c)->assetPath(n)), std::ios::binary); if (!f) return JS_NULL; std::string t((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); return JS_NewStringLen(c, t.data(), t.size()); });
  FN("setSky", 1, W(c)->setSky(S(c, a[0])); return JS_UNDEFINED;);
  FN("setCamera", 1, W(c)->setCamera(S(c, a[0])); return JS_UNDEFINED;);
  FN("gp", 3, { std::string s = W(c)->gameplay(S(c, a[0]), S(c, a[1]), S(c, a[2])); return JS_NewStringLen(c, s.data(), s.size()); });
  FN("postGet", 0, { std::string s = W(c)->postGet(); return JS_NewStringLen(c, s.data(), s.size()); });
  FN("setGravity", 1, W(c)->setGravity((float)D(c, a[0])); return JS_UNDEFINED;);
  FN("time", 0, return JS_NewFloat64(c, W(c)->time()););
  FN("playerPos", 0, return A3(c, W(c)->playerPos()););
  FN("teleport", 3, W(c)->playerTeleport({(float)D(c, a[0]), (float)D(c, a[1]), (float)D(c, a[2])}); return JS_UNDEFINED;);
  FN("launch", 3, W(c)->playerLaunch({(float)D(c, a[0]), (float)D(c, a[1]), (float)D(c, a[2])}); return JS_UNDEFINED;);
  FN("key", 1, return JS_NewBool(c, W(c)->key(keyAlias(S(c, a[0]))));) ;
  FN("pressed", 1, return JS_NewBool(c, W(c)->pressed(keyAlias(S(c, a[0]))));) ;
  FN("axis", 1, return JS_NewFloat64(c, W(c)->axis(S(c, a[0])));) ;
  FN("padOn", 0, return JS_NewBool(c, W(c)->padConnected());) ;
  FN("padSet", 1, W(c)->padSet(S(c, a[0])); return JS_UNDEFINED;) ;
  FN("rumble", 3, W(c)->rumble((float)D(c, a[0]), (float)D(c, a[1]), (float)D(c, a[2])); return JS_UNDEFINED;) ;
  FN("mouse", 0, { glm::vec4 m = W(c)->mouse(); JSValue r = JS_NewArray(c); for (int i = 0; i < 4; i++) JS_SetPropertyUint32(c, r, i, JS_NewFloat64(c, m[i])); return r; });
  FN("log", 1, W(c)->log(S(c, a[0])); return JS_UNDEFINED;);
  FN("trophy", 1, W(c)->trophy(S(c, a[0])); return JS_UNDEFINED;);
  // game UI
#define UIP(c) Ui* U = W(c)->ui(); if (!U) return JS_UNDEFINED;
  auto js = [](JSContext* c, JSValueConst v) { nlohmann::json j = nlohmann::json::object(); try { j = nlohmann::json::parse(S(c, v)); } catch (...) {} return j; };
  (void)js;
  FN("uiCreate", 2, { UIP(c) nlohmann::json p = nlohmann::json::object(); try { p = nlohmann::json::parse(S(c, a[1])); } catch (...) {}
    std::string id = p.is_object() && p.contains("id") && p["id"].is_string() ? p["id"].get<std::string>() : "";
    std::string e = U->create(S(c, a[0]), id, p); if (!e.empty()) { std::string r = "!" + e; return JS_NewStringLen(c, r.data(), r.size()); }
    id = U->lastId();
    return JS_NewStringLen(c, id.data(), id.size()); });
  FN("uiSet", 2, { UIP(c) nlohmann::json p = nlohmann::json::object(); try { p = nlohmann::json::parse(S(c, a[1])); } catch (...) {} std::string e = U->set(S(c, a[0]), p); return JS_NewStringLen(c, e.data(), e.size()); });
  FN("uiGet", 1, { UIP(c) std::string r = U->get(S(c, a[0])).dump(); return JS_NewStringLen(c, r.data(), r.size()); });
  FN("uiExists", 1, { UIP(c) return JS_NewBool(c, U->exists(S(c, a[0]))); });
  FN("uiKids", 1, { UIP(c) std::string r = nlohmann::json(U->children(S(c, a[0]))).dump(); return JS_NewStringLen(c, r.data(), r.size()); });
  FN("uiRemove", 1, { UIP(c) U->remove(S(c, a[0])); return JS_UNDEFINED; });
  FN("uiClear", 1, { UIP(c) U->clear(S(c, a[0])); return JS_UNDEFINED; });
  FN("uiShow", 2, { UIP(c) nlohmann::json p = nlohmann::json::object(); try { p = nlohmann::json::parse(S(c, a[1])); } catch (...) {} U->show(S(c, a[0]), p); return JS_UNDEFINED; });
  FN("uiHide", 2, { UIP(c) nlohmann::json p = nlohmann::json::object(); try { p = nlohmann::json::parse(S(c, a[1])); } catch (...) {} U->hide(S(c, a[0]), p); return JS_UNDEFINED; });
  FN("uiFocus", 1, { UIP(c) U->focus(S(c, a[0])); return JS_UNDEFINED; });
  FN("uiListen", 3, { UIP(c) U->listen(S(c, a[0]), S(c, a[1]), JS_ToBool(c, a[2])); return JS_UNDEFINED; });
  FN("uiScale", 1, { UIP(c) U->setScale((float)D(c, a[0])); return JS_UNDEFINED; });
  FN("uiDir", 1, { UIP(c) U->setDir(JS_ToBool(c, a[0])); return JS_UNDEFINED; });
  FN("uiEscape", 1, { UIP(c) U->setEscape(JS_ToBool(c, a[0])); return JS_UNDEFINED; });
  FN("uiMouse", 0, { UIP(c) float m[4]; U->mouse(m); JSValue r = JS_NewArray(c); for (int i = 0; i < 4; i++) JS_SetPropertyUint32(c, r, i, JS_NewFloat64(c, m[i])); return r; });
  FN("uiState", 0, { UIP(c) JSValue r = JS_NewArray(c); JS_SetPropertyUint32(c, r, 0, JS_NewBool(c, U->modal())); JS_SetPropertyUint32(c, r, 1, JS_NewBool(c, U->pausing())); JS_SetPropertyUint32(c, r, 2, JS_NewBool(c, U->typing())); return r; });
  // sound
#define SNDP(c) Audio* A = W(c)->audio(); if (!A) return JS_NewString(c, "no sound here");
  FN("sndMake", 2, { SNDP(c) nlohmann::json d; try { d = nlohmann::json::parse(S(c, a[1])); } catch (...) { return JS_NewString(c, "the description is not valid"); }
    std::string e = A->make(S(c, a[0]), d); return JS_NewStringLen(c, e.data(), e.size()); });
  FN("sndSamples", 4, { SNDP(c) size_t off = 0, bl = 0, bpe = 0; JSValue buf = JS_GetTypedArrayBuffer(c, a[1], &off, &bl, &bpe);
    if (JS_IsException(buf) || bpe != 4) { if (JS_IsException(buf)) JS_FreeValue(c, JS_GetException(c)); else JS_FreeValue(c, buf); return JS_NewString(c, "samples must be a Float32Array (-1 .. 1)"); }
    size_t sz = 0; uint8_t* base = JS_GetArrayBuffer(c, &sz, buf); JS_FreeValue(c, buf); if (!base) return JS_NewString(c, "samples must be a Float32Array (-1 .. 1)");
    std::vector<float> v((const float*)(base + off), (const float*)(base + off) + bl / 4);
    std::string name = S(c, a[0]); int ch = (int)D(c, a[2]), rate = (int)D(c, a[3]);
    if (name.empty()) { std::string why; int h = A->playSamples(std::move(v), ch, rate, nlohmann::json::object(), &why); if (!h) return JS_NewStringLen(c, why.data(), why.size()); return JS_NewInt32(c, h); }
    std::string e = A->makeSamples(name, std::move(v), ch, rate); return JS_NewStringLen(c, e.data(), e.size()); });
  FN("sndPlay", 3, { SNDP(c) nlohmann::json o = nlohmann::json::object(); try { o = nlohmann::json::parse(S(c, a[2])); } catch (...) {} std::string why; int h;
    if (JS_ToBool(c, a[1])) { nlohmann::json d; try { d = nlohmann::json::parse(S(c, a[0])); } catch (...) { return JS_NewString(c, "the description is not valid"); } h = A->playDef(d, o, &why); }
    else h = A->play(S(c, a[0]), o, &why);
    if (!h) return JS_NewStringLen(c, why.data(), why.size()); return JS_NewInt32(c, h); });
  FN("sndSet", 2, { SNDP(c) nlohmann::json o = nlohmann::json::object(); try { o = nlohmann::json::parse(S(c, a[1])); } catch (...) {} A->set((int)D(c, a[0]), o); return JS_UNDEFINED; });
  FN("sndStop", 2, { SNDP(c) A->stop((int)D(c, a[0]), (float)D(c, a[1])); return JS_UNDEFINED; });
  FN("sndStopAll", 1, { SNDP(c) A->stopAll(S(c, a[0])); return JS_UNDEFINED; });
  FN("sndPlaying", 1, { SNDP(c) return JS_NewBool(c, A->playing((int)D(c, a[0]))); });
  FN("sndHas", 1, { SNDP(c) return JS_NewBool(c, A->has(S(c, a[0]))); });
  FN("sndVolume", 2, { SNDP(c) std::string b = S(c, a[0]); if (JS_IsNumber(a[1])) A->setVolume(b, (float)D(c, a[1])); return JS_NewFloat64(c, A->volume(b)); });
#undef SNDP
  // models and particles
#define MDP(c) Models* M = W(c)->models(); if (!M) return JS_NewString(c, "no models here");
  auto pj = [](JSContext* c, JSValueConst v) { nlohmann::json j = nlohmann::json::object(); try { j = nlohmann::json::parse(S(c, v)); } catch (...) {} return j; };
  (void)pj;
  FN("mdlDefine", 2, { MDP(c) nlohmann::json d; try { d = nlohmann::json::parse(S(c, a[1])); } catch (...) { return JS_NewString(c, "the model is not valid"); }
    std::string e = M->define(S(c, a[0]), d); return JS_NewStringLen(c, e.data(), e.size()); });
  FN("mdlSpawn", 2, { MDP(c) nlohmann::json o = nlohmann::json::object(); try { o = nlohmann::json::parse(S(c, a[1])); } catch (...) {}
    std::string id, e = M->spawn(S(c, a[0]), o, id); std::string r = e.empty() ? id : "!" + e; return JS_NewStringLen(c, r.data(), r.size()); });
  FN("mdlSet", 2, { MDP(c) nlohmann::json o = nlohmann::json::object(); try { o = nlohmann::json::parse(S(c, a[1])); } catch (...) { return JS_NewString(c, "not valid"); }
    std::string e = M->set(S(c, a[0]), o); return JS_NewStringLen(c, e.data(), e.size()); });
  FN("mdlGet", 1, { MDP(c) std::string r = M->get(S(c, a[0])).dump(); return JS_NewStringLen(c, r.data(), r.size()); });
  FN("mdlRemove", 1, { MDP(c) M->remove(S(c, a[0])); return JS_UNDEFINED; });
  FN("mdlExists", 1, { MDP(c) return JS_NewBool(c, M->exists(S(c, a[0]))); });
  FN("mdlPart", 2, { MDP(c) bool ok = false; glm::mat4 m = M->partWorld(S(c, a[0]), S(c, a[1]), &ok); if (!ok) return JS_NULL;
    JSValue r = JS_NewArray(c); for (int i = 0; i < 16; i++) JS_SetPropertyUint32(c, r, i, JS_NewFloat64(c, (&m[0][0])[i])); return r; });
  FN("ptEmit", 1, { MDP(c) nlohmann::json o; try { o = nlohmann::json::parse(S(c, a[0])); } catch (...) { return JS_NewString(c, "not valid"); } std::string e = M->emit(o); return JS_NewStringLen(c, e.data(), e.size()); });
  FN("ptClear", 0, { MDP(c) M->clearParticles(); return JS_UNDEFINED; });
  FN("ptCount", 0, { MDP(c) return JS_NewInt32(c, M->particleCount()); });
#undef MDP
  FN("uiError", 1, { logMsg("error: " + S(c, a[0]), LogKind::Error); return JS_UNDEFINED; });
#undef UIP
  FN("psEnv", 1, { const char* v = std::getenv(S(c, a[0]).c_str()); return JS_NewBool(c, v && *v && std::string(v) != "0"); });
  FN("camera", 0, { glm::vec3 p = W(c)->cameraPos(), f = W(c)->cameraDir(); JSValue r = JS_NewArray(c); float v[6] = {p.x, p.y, p.z, f.x, f.y, f.z}; for (int i = 0; i < 6; i++) JS_SetPropertyUint32(c, r, i, JS_NewFloat64(c, v[i])); return r; });
  // block world
  FN("voxCreate", 1, { std::string e = W(c)->createVoxels(S(c, a[0])); return JS_NewStringLen(c, e.data(), e.size()); });
  FN("voxOn", 0, return JS_NewBool(c, W(c)->voxels() != nullptr););
  FN("voxDefine", 2, { VoxelWorld* v = W(c)->voxels(); if (!v) { W(c)->log("warning: voxels.defineBlock: call voxels.create() first"); return JS_UNDEFINED; }
    BlockType t; nlohmann::json o = nlohmann::json::object(); try { o = nlohmann::json::parse(S(c, a[1])); } catch (...) {}
    auto col = [](const nlohmann::json& e, glm::vec3 d) -> glm::vec3 {   // '#rrggbb', 'rgb(r,g,b)' or [r, g, b] (0..1)
      if (e.is_string()) { std::string s = e; if (s.rfind("rgb", 0) == 0) { int r = 0, g = 0, b = 0; if (std::sscanf(s.c_str(), "rgb(%d,%d,%d)", &r, &g, &b) == 3 || std::sscanf(s.c_str(), "rgb( %d , %d , %d )", &r, &g, &b) == 3) return glm::vec3(r, g, b) / 255.f; return d; } return hexColor(s, d); }
      if (e.is_array() && e.size() >= 3 && e[0].is_number()) return {e[0].get<float>(), e[1].get<float>(), e[2].get<float>()};
      return d; };
    t.name = o.value("name", std::string());
    auto colors = o.contains("color") ? o["color"] : nlohmann::json("#ffffff");
    if (colors.is_string() || (colors.is_array() && colors.size() == 3 && colors[0].is_number())) t.color[0] = t.color[1] = t.color[2] = col(colors, glm::vec3(1));
    else if (colors.is_array() && !colors.empty()) for (int i = 0; i < 3; i++) t.color[i] = col(colors.size() > (size_t)i ? colors[i] : colors.back(), glm::vec3(1));
    else if (colors.is_object()) { t.color[0] = col(colors.value("top", nlohmann::json("#ffffff")), glm::vec3(1)); t.color[1] = col(colors.value("side", colors.value("top", nlohmann::json("#ffffff"))), t.color[0]); t.color[2] = col(colors.value("bottom", colors.value("side", nlohmann::json("#ffffff"))), t.color[1]); }
    if (o.contains("tile")) { auto& tl = o["tile"]; if (tl.is_number()) t.tile[0] = t.tile[1] = t.tile[2] = tl.get<int>();
      else if (tl.is_array()) for (int i = 0; i < 3; i++) { const auto& e = tl.size() > (size_t)i ? tl[i] : tl.back(); if (e.is_number()) t.tile[i] = e.get<int>(); }
      else if (tl.is_object()) { t.tile[0] = tl.value("top", -1); t.tile[1] = tl.value("side", t.tile[0]); t.tile[2] = tl.value("bottom", t.tile[1]); } }
    t.solid = o.value("solid", true); t.opaque = !o.value("transparent", false); t.liquid = o.value("liquid", false);
    t.cutout = o.value("cutout", !t.opaque && !t.liquid && (t.tile[0] >= 0 || t.tile[1] >= 0 || t.tile[2] >= 0));
    t.vary = glm::clamp(o.value("vary", 0.f), 0.f, 0.5f); t.opacity = glm::clamp(o.value("opacity", 0.75f), 0.05f, 1.f);
    if (o.contains("glow")) { auto& gl = o["glow"]; t.glow = gl.is_boolean() ? (gl.get<bool>() ? 1.f : 0.f) : gl.is_number() ? glm::clamp(gl.get<float>(), 0.f, 2.f) : 0.f; }
    if (o.contains("biome")) { auto& bi = o["biome"]; auto bit = [](const std::string& s) -> uint8_t { return s == "top" ? 1 : s == "side" || s == "sides" ? 2 : s == "bottom" ? 4 : s == "all" ? 7 : 0; };
      if (bi.is_boolean()) t.biome = bi.get<bool>() ? 7 : 0; else if (bi.is_string()) t.biome = bit(bi.get<std::string>()); else if (bi.is_array()) for (auto& e : bi) if (e.is_string()) t.biome |= bit(e.get<std::string>()); }
    if (o.contains("shape")) { auto& sh = o["shape"]; int x = sh.is_number() ? sh.get<int>() : sh.is_string() ? shapeFromName(sh.get<std::string>()) : -1;
      if (x < 0 || x >= SH_Count) W(c)->log("warning: voxels.defineBlock: unknown shape (cube, slope, half, hex, arch, wedge, cylinder, pyramid, roof, stairs, cross)"); else t.shape = x;
      if (t.shape == SH_Cross && !o.contains("solid")) t.solid = false; }
    if (o.contains("model") && o["model"].is_array()) for (auto& p : o["model"]) { if (!p.is_object()) continue; ModelPart m;
      if (p.contains("box") && p["box"].is_array() && p["box"].size() == 6) { m.kind = 0; m.a = {p["box"][0].get<float>(), p["box"][1].get<float>(), p["box"][2].get<float>()}; m.b = {p["box"][3].get<float>(), p["box"][4].get<float>(), p["box"][5].get<float>()}; }
      else if (p.contains("cylinder") && p["cylinder"].is_array() && p["cylinder"].size() == 5) { m.kind = 1; auto& q = p["cylinder"]; m.a = {q[0].get<float>(), q[1].get<float>(), q[2].get<float>()}; m.b = {q[3].get<float>(), q[4].get<float>(), 0};
        std::string ax = p.value("axis", std::string("y")); m.axis = ax == "x" ? 0 : ax == "z" ? 2 : 1; m.sides = p.value("sides", 16); }
      else { W(c)->log("warning: voxels.defineBlock: a model part needs box: [x0, y0, z0, x1, y1, z1] or cylinder: [x, y, z, radius, length]"); continue; }
      if (p.contains("color")) { m.color = col(p["color"], glm::vec3(1)); m.hasColor = true; } m.tile = p.value("tile", -1); t.model.push_back(m); }
    v->defineBlock((int)D(c, a[0]), t); return JS_UNDEFINED; });
  FN("voxGenBuiltin", 2, { if (VoxelWorld* v = W(c)->voxels()) v->setBuiltinGenerator(S(c, a[0]), S(c, a[1])); return JS_UNDEFINED; });
  FN("voxGenScript", 2, { if (VoxelWorld* v = W(c)->voxels()) v->setScriptGenerator(S(c, a[0]), S(c, a[1])); return JS_UNDEFINED; });
  FN("voxAtlas", 2, { VoxelWorld* v = W(c)->voxels(); if (!v) return JS_NewString(c, "call voxels.create() first"); std::string f = W(c)->assetPath(S(c, a[0]));
    return v->setAtlas(f, (int)D(c, a[1])) ? JS_TRUE : JS_NewString(c, ("can't read " + f).c_str()); });
  FN("voxAtlasPx", 4, { VoxelWorld* v = W(c)->voxels(); if (!v) return JS_NewString(c, "call voxels.create() first");
    size_t n = 0; uint8_t* px = JS_GetUint8Array(c, &n, a[0]); if (!px) { JS_FreeValue(c, JS_GetException(c)); size_t off = 0, bpe = 0; JSValue buf = JS_GetTypedArrayBuffer(c, a[0], &off, &n, &bpe);
      if (JS_IsException(buf)) { JS_FreeValue(c, JS_GetException(c)); return JS_NewString(c, "pixels must be a Uint8Array / Uint8ClampedArray"); } size_t bl = 0; uint8_t* base = JS_GetArrayBuffer(c, &bl, buf); JS_FreeValue(c, buf); px = base ? base + off : nullptr; }
    int w = (int)D(c, a[1]), h = (int)D(c, a[2]); if (!px || (size_t)w * h * 4 > n) return JS_NewString(c, "pixels must hold width x height x 4 bytes (RGBA)");
    std::string why; return v->setAtlasPixels(px, w, h, (int)D(c, a[3]), &why) ? JS_TRUE : JS_NewStringLen(c, why.data(), why.size()); });
  FN("voxAtlasTiles", 3, { VoxelWorld* v = W(c)->voxels(); if (!v) return JS_NewString(c, "call voxels.create() first");
    int ts = (int)D(c, a[1]); JSValue len = JS_GetPropertyStr(c, a[0], "length"); int n = (int)D(c, len); JS_FreeValue(c, len);
    if (ts < 1 || ts > 512 || n < 1) return JS_NewString(c, "tiles wants a list of tiles and tileSize 1..512");
    int per = (int)D(c, a[2]); if (per <= 0) { per = 1; while (per * per < n) per++; } int rows = (n + per - 1) / per;
    std::vector<uint8_t> img((size_t)per * ts * rows * ts * 4, 0);
    for (int i = 0; i < n; i++) { JSValue e = JS_GetPropertyUint32(c, a[0], (uint32_t)i); size_t off = 0, bl = 0, bpe = 0; JSValue buf = JS_GetTypedArrayBuffer(c, e, &off, &bl, &bpe); JS_FreeValue(c, e);
      if (JS_IsException(buf)) { JS_FreeValue(c, JS_GetException(c)); return JS_NewString(c, ("tile " + std::to_string(i) + " is not a Uint8Array").c_str()); }
      size_t sz = 0; uint8_t* base = JS_GetArrayBuffer(c, &sz, buf); JS_FreeValue(c, buf); if (!base || bl < (size_t)ts * ts * 4) return JS_NewString(c, ("tile " + std::to_string(i) + " needs tileSize x tileSize x 4 bytes").c_str());
      int tx = i % per, ty = i / per; for (int y = 0; y < ts; y++) std::memcpy(&img[(((size_t)ty * ts + y) * per * ts + (size_t)tx * ts) * 4], base + off + (size_t)y * ts * 4, (size_t)ts * 4); }
    std::string why; return v->setAtlasPixels(img.data(), per * ts, rows * ts, per, &why) ? JS_TRUE : JS_NewStringLen(c, why.data(), why.size()); });
  FN("voxGet", 3, { VoxelWorld* v = W(c)->voxels(); if (!v) return JS_NULL; BlockInfo b = v->getBlock({(float)D(c, a[0]), (float)D(c, a[1]), (float)D(c, a[2])}); if (!b.id) return JS_NULL;
    JSValue r = JS_NewArray(c); int sh = b.extra.shape ? b.extra.shape - 1 : v->block(b.id).shape;
    double x[7] = {(double)b.id, b.origin.x / (double)VC, b.origin.y / (double)VC, b.origin.z / (double)VC, b.size / (double)VC, b.sizeY / (double)VC, (double)b.extra.rot}; for (int i = 0; i < 7; i++) JS_SetPropertyUint32(c, r, i, JS_NewFloat64(c, x[i]));
    JS_SetPropertyUint32(c, r, 7, JS_NewString(c, shapeName(sh)));
    if (b.extra.carved) { std::string m(125, '0'); for (int k = 0; k < 125; k++) if (b.extra.carve[k >> 6] >> (k & 63) & 1) m[k] = '1'; JS_SetPropertyUint32(c, r, 8, JS_NewStringLen(c, m.data(), m.size())); }
    return r; });
  FN("voxSet", 6, { VoxelWorld* v = W(c)->voxels(); if (!v) return JS_NewString(c, "call voxels.create() first"); std::string why; BlockOpts o; std::string oj = S(c, a[5]);
    if (!oj.empty() && !blockOptsFromJson(oj, o, &why)) return JS_NewStringLen(c, why.data(), why.size());
    bool ok = v->setBlock({(float)D(c, a[0]), (float)D(c, a[1]), (float)D(c, a[2])}, (int)D(c, a[3]), (float)D(c, a[4]), o, &why); return ok ? JS_TRUE : JS_NewStringLen(c, why.data(), why.size()); });
  FN("voxCanPlace", 5, { VoxelWorld* v = W(c)->voxels(); return JS_NewBool(c, v && v->canPlace({(float)D(c, a[0]), (float)D(c, a[1]), (float)D(c, a[2])}, (float)D(c, a[3]), (float)D(c, a[4]))); });
  FN("voxRemove", 3, { VoxelWorld* v = W(c)->voxels(); return JS_NewInt32(c, v ? v->removeBlock({(float)D(c, a[0]), (float)D(c, a[1]), (float)D(c, a[2])}) : 0); });
  FN("voxCarve", 7, { VoxelWorld* v = W(c)->voxels(); return JS_NewBool(c, v && v->carve({(float)D(c, a[0]), (float)D(c, a[1]), (float)D(c, a[2])}, (int)D(c, a[3]), (int)D(c, a[4]), (int)D(c, a[5]), JS_ToBool(c, a[6]))); });
  FN("voxSolid", 3, { VoxelWorld* v = W(c)->voxels(); return JS_NewBool(c, v && v->solidAt({(float)D(c, a[0]), (float)D(c, a[1]), (float)D(c, a[2])})); });
  // any solid block in a box (its cells every 20 cm, the edges included): vehicles and moving things test where they go
  FN("voxBoxSolid", 6, { VoxelWorld* v = W(c)->voxels(); if (!v) return JS_FALSE; glm::vec3 a0((float)D(c, a[0]), (float)D(c, a[1]), (float)D(c, a[2])), a1((float)D(c, a[3]), (float)D(c, a[4]), (float)D(c, a[5]));
    glm::vec3 mn = glm::min(a0, a1), mx = glm::max(a0, a1), sz = mx - mn; if (sz.x * sz.y * sz.z > 4000) return JS_FALSE; int n[3]; for (int k = 0; k < 3; k++) n[k] = std::max(1, (int)std::ceil(sz[k] / 0.19f));
    for (int i = 0; i <= n[0]; i++) for (int j = 0; j <= n[1]; j++) for (int k = 0; k <= n[2]; k++) if (v->solidAt(mn + sz * glm::vec3((float)i / n[0], (float)j / n[1], (float)k / n[2]))) return JS_TRUE;
    return JS_FALSE; });
  FN("voxLiquid", 3, { VoxelWorld* v = W(c)->voxels(); return JS_NewInt32(c, v ? v->liquidAt({(float)D(c, a[0]), (float)D(c, a[1]), (float)D(c, a[2])}) : 0); });
  FN("voxRay", 8, { VoxelWorld* v = W(c)->voxels(); if (!v) return JS_NULL;
    VoxelHit h = v->raycast({(float)D(c, a[0]), (float)D(c, a[1]), (float)D(c, a[2])}, {(float)D(c, a[3]), (float)D(c, a[4]), (float)D(c, a[5])}, (float)D(c, a[6]), JS_ToBool(c, a[7])); if (!h.hit) return JS_NULL;
    double x[16] = {(double)h.id, h.origin.x / (double)VC, h.origin.y / (double)VC, h.origin.z / (double)VC, h.size / (double)VC, (double)h.normal.x, (double)h.normal.y, (double)h.normal.z,
      h.point.x, h.point.y, h.point.z, h.distance, (double)h.cell.x, (double)h.cell.y, (double)h.cell.z, h.sizeY / (double)VC};
    JSValue r = JS_NewArray(c); for (int i = 0; i < 16; i++) JS_SetPropertyUint32(c, r, i, JS_NewFloat64(c, x[i])); return r; });
  FN("voxPlace", 18, { VoxelWorld* v = W(c)->voxels(); VoxelHit h; h.hit = true; h.id = (int)D(c, a[0]);
    h.origin = glm::ivec3((int)std::lround(D(c, a[1]) * VC), (int)std::lround(D(c, a[2]) * VC), (int)std::lround(D(c, a[3]) * VC)); h.size = (int)std::lround(D(c, a[4]) * VC);
    h.normal = glm::ivec3((int)D(c, a[5]), (int)D(c, a[6]), (int)D(c, a[7])); h.point = {(float)D(c, a[8]), (float)D(c, a[9]), (float)D(c, a[10])}; h.distance = (float)D(c, a[11]);
    h.cell = glm::ivec3((int)D(c, a[12]), (int)D(c, a[13]), (int)D(c, a[14])); h.sizeY = (int)std::lround(D(c, a[15]) * VC);
    glm::vec3 p = v ? v->placeCorner(h, (float)D(c, a[16]), (float)D(c, a[17])) : glm::vec3(0); return A3(c, p); });
  FN("voxSave", 1, { VoxelWorld* v = W(c)->voxels(); if (!v) return JS_NewString(c, "call voxels.create() first"); std::string why;
    return v->save(W(c)->savePath(S(c, a[0])), &why) ? JS_TRUE : JS_NewStringLen(c, why.data(), why.size()); });
  FN("voxLoad", 1, { VoxelWorld* v = W(c)->voxels(); if (!v) return JS_NewString(c, "call voxels.create() first"); std::string why;
    return v->load(W(c)->savePath(S(c, a[0])), &why) ? JS_TRUE : JS_NewStringLen(c, why.data(), why.size()); });
  FN("voxSaveData", 0, { VoxelWorld* v = W(c)->voxels(); if (!v) return JS_NewString(c, "!call voxels.create() first"); std::string why, path = W(c)->savePath("__share_out");
    if (!v->save(path, &why)) { why = "!" + why; return JS_NewStringLen(c, why.data(), why.size()); }
    std::ifstream in(std::filesystem::u8path(path), std::ios::binary); std::string bin((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()); in.close(); std::error_code ec; std::filesystem::remove(std::filesystem::u8path(path), ec);
    std::string r = base64Encode(bin); return JS_NewStringLen(c, r.data(), r.size()); });
  FN("voxLoadData", 1, { VoxelWorld* v = W(c)->voxels(); if (!v) return JS_NewString(c, "call voxels.create() first"); std::string why, path = W(c)->savePath("__share_in"), bin;
    if (!base64Decode(S(c, a[0]), bin)) return JS_NewString(c, "not a saved block world");
    { std::error_code ec; std::filesystem::create_directories(std::filesystem::u8path(path).parent_path(), ec); std::ofstream o(std::filesystem::u8path(path), std::ios::binary); o.write(bin.data(), bin.size()); }
    bool ok = v->load(path, &why); std::error_code ec; std::filesystem::remove(std::filesystem::u8path(path), ec); return ok ? JS_TRUE : JS_NewStringLen(c, why.data(), why.size()); });
  FN("swimming", 0, return JS_NewBool(c, W(c)->playerSwimming()););
  FN("voxView", 1, { if (VoxelWorld* v = W(c)->voxels()) v->setViewDistance((int)D(c, a[0])); return JS_UNDEFINED; });
  FN("voxDelete", 1, { std::error_code ec; return JS_NewBool(c, std::filesystem::remove(std::filesystem::u8path(W(c)->savePath(S(c, a[0]))), ec)); });
  FN("playerGet", 0, { std::string s = W(c)->playerGet(); return JS_NewStringLen(c, s.data(), s.size()); });
  FN("playerSet", 1, { W(c)->playerSet(S(c, a[0])); return JS_UNDEFINED; });
  FN("storage", 3, { std::string s = W(c)->storage(S(c, a[0]), S(c, a[1]), S(c, a[2])); return JS_NewStringLen(c, s.data(), s.size()); });
  FN("voxItems", 1, { VoxelWorld* v = W(c)->voxels(); if (!v) return JS_UNDEFINED; size_t off = 0, len = 0, bpe = 0; JSValue buf = JS_GetTypedArrayBuffer(c, a[0], &off, &len, &bpe);
    if (JS_IsException(buf)) return JS_EXCEPTION; size_t sz = 0; uint8_t* p = JS_GetArrayBuffer(c, &sz, buf); JS_FreeValue(c, buf); std::vector<float> r;
    if (p && bpe == 4) { r.resize(len / 4); std::memcpy(r.data(), p + off, r.size() * 4); } v->setItems(r); return JS_UNDEFINED; });
  FN("voxReady", 3, { VoxelWorld* v = W(c)->voxels(); return JS_NewBool(c, v && v->ready({(float)D(c, a[0]), (float)D(c, a[1]), (float)D(c, a[2])})); });
  FN("voxTop", 2, { VoxelWorld* v = W(c)->voxels(); return JS_NewFloat64(c, v ? v->topY((float)D(c, a[0]), (float)D(c, a[1])) : NAN); });
  FN("voxStats", 0, { VoxelWorld* v = W(c)->voxels(); std::string s = "{}";
    if (v) { auto st = v->stats(); s = nlohmann::json{{"chunks", st.chunks}, {"meshed", st.meshed}, {"generating", st.pendingGen}, {"meshing", st.pendingMesh}, {"triangles", st.triangles}, {"bricks", st.bricks}, {"generateMs", st.genMs}, {"meshMs", st.meshMs}, {"memoryMB", st.memory / 1048576.0}, {"changedChunks", v->changedChunks()}}.dump(); }
    return JS_NewStringLen(c, s.data(), s.size()); });
  JS_SetPropertyStr(c, g, "__n", n); JS_FreeValue(c, g);
}
#undef FN

static int interrupt(JSRuntime*, void* op) { auto* d = (ScriptHost::Impl*)op; return std::chrono::steady_clock::now() > d->deadline ? 1 : 0; }

ScriptHost::ScriptHost() : d(new Impl) {}
ScriptHost::~ScriptHost() { shutdown(); }

bool ScriptHost::init(ScriptWorld* w) {
  shutdown(); d->w = w;
  d->rt = JS_NewRuntime(); JS_SetMemoryLimit(d->rt, 256u << 20); JS_SetMaxStackSize(d->rt, 1u << 20);
  d->ctx = JS_NewContext(d->rt); JS_SetContextOpaque(d->ctx, w);
  JS_SetInterruptHandler(d->rt, interrupt, d.get());
  bindNatives(d->ctx);
  d->deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  JSValue r = JS_Eval(d->ctx, PRELUDE, std::char_traits<char>::length(PRELUDE), "<prelude>", JS_EVAL_TYPE_GLOBAL);
  if (JS_IsException(r)) { logMsg("error: script prelude: " + errorText(d->ctx), LogKind::Error); return false; }
  JS_FreeValue(d->ctx, r);
  JSValue g = JS_GetGlobalObject(d->ctx); d->make = JS_GetPropertyStr(d->ctx, g, "__make"); d->api = JS_GetPropertyStr(d->ctx, g, "__api"); d->info = JS_GetPropertyStr(d->ctx, g, "__info"); JS_FreeValue(d->ctx, g);
  return true;
}
void ScriptHost::shutdown() {
  if (!d || !d->ctx) return;
  for (auto& b : d->b) JS_FreeValue(d->ctx, b.obj);
  d->b.clear(); JS_FreeValue(d->ctx, d->make); JS_FreeValue(d->ctx, d->api); JS_FreeValue(d->ctx, d->info); d->make = d->api = d->info = JS_UNDEFINED;
  JS_FreeContext(d->ctx); JS_FreeRuntime(d->rt); d->ctx = nullptr; d->rt = nullptr;
}

static std::string wrap(const std::string& code) {
  // first user line stays line 1 so error lines match the editor
  return "(function (self, world, input, log, THREE, math) { \"use strict\"; " + code +
         "\n;return { onStart: typeof onStart === 'function' ? onStart : null, onUpdate: typeof onUpdate === 'function' ? onUpdate : null,"
         " onOverlap: typeof onOverlap === 'function' ? onOverlap : null, onHit: typeof onHit === 'function' ? onHit : null, onKey: typeof onKey === 'function' ? onKey : null, onStop: typeof onStop === 'function' ? onStop : null,"
         " onAnimationEnd: typeof onAnimationEnd === 'function' ? onAnimationEnd : null, onMoveFinished: typeof onMoveFinished === 'function' ? onMoveFinished : null,"
         " onDamage: typeof onDamage === 'function' ? onDamage : null, onPlayerDamage: typeof onPlayerDamage === 'function' ? onPlayerDamage : null, onPlayerDeath: typeof onPlayerDeath === 'function' ? onPlayerDeath : null,"
         " onJointBreak: typeof onJointBreak === 'function' ? onJointBreak : null, onVehicleEnter: typeof onVehicleEnter === 'function' ? onVehicleEnter : null, onVehicleExit: typeof onVehicleExit === 'function' ? onVehicleExit : null }; })";
}

int ScriptHost::attach(const std::string& actorId, const std::string& actorName, const std::string& scriptName, const std::string& code) {
  if (!d->ctx) return -1;
  JSContext* c = d->ctx; d->deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  std::string src = wrap(code);
  JSValue fn = JS_Eval(c, src.c_str(), src.size(), scriptName.c_str(), JS_EVAL_TYPE_GLOBAL);
  if (JS_IsException(fn)) { logMsg("error: " + scriptName + ": " + errorText(c), LogKind::Error); return -1; }
  JSValue args[3] = {fn, JS_NewStringLen(c, actorId.data(), actorId.size()), JS_NewStringLen(c, actorName.data(), actorName.size())};
  JSValue obj = JS_Call(c, d->make, JS_UNDEFINED, 3, args);
  for (auto& v : args) JS_FreeValue(c, v);
  if (JS_IsException(obj)) { logMsg("error: " + scriptName + " on " + actorName + ": " + errorText(c), LogKind::Error); return -1; }
  Behaviour b; b.actor = actorId; b.actorName = actorName; b.script = scriptName; b.obj = obj;
  for (int e = 0; e < EV_COUNT; e++) { JSValue f = JS_GetPropertyStr(c, obj, EVENTS[e]); b.ev[e] = JS_IsFunction(c, f); JS_FreeValue(c, f); }
  d->b.push_back(b); return (int)d->b.size() - 1;
}
bool ScriptHost::has(int i, ScriptEvent e) const { return i >= 0 && i < (int)d->b.size() && d->b[i].alive && d->b[i].ev[e]; }
void ScriptHost::call(int i, ScriptEvent e, double num, const std::string& arg) {
  if (!has(i, e)) return;
  JSContext* c = d->ctx; Behaviour& b = d->b[i];
  JSValue fn = JS_GetPropertyStr(c, b.obj, EVENTS[e]); JSValue av[2]; int n = 0;
  auto actorArg = [&](const std::string& id) -> JSValue { if (id.empty()) return JS_NULL; JSValue s = JS_NewStringLen(c, id.data(), id.size()); JSValue r = JS_Call(c, d->api, JS_UNDEFINED, 1, &s); JS_FreeValue(c, s);
    if (JS_IsException(r)) { JS_FreeValue(c, JS_GetException(c)); return JS_NULL; } return r; };
  if (e == EV_Update) { av[0] = JS_NewFloat64(c, num); n = 1; }
  else if (e == EV_Overlap || e == EV_Hit) {
    if (arg.empty()) av[0] = JS_NULL; else { JSValue s = JS_NewStringLen(c, arg.data(), arg.size()); av[0] = JS_Call(c, d->api, JS_UNDEFINED, 1, &s); JS_FreeValue(c, s); if (JS_IsException(av[0])) { JS_FreeValue(c, JS_GetException(c)); av[0] = JS_NULL; } }
    n = 1;
  } else if (e == EV_Key || e == EV_AnimEnd) { av[0] = JS_NewStringLen(c, arg.data(), arg.size()); n = 1; }
  else if (e == EV_MoveDone) { av[0] = JS_NewBool(c, num != 0); n = 1; }
  else if (e == EV_Damage || e == EV_PlayerDamage) { av[0] = JS_NewFloat64(c, num); JSValue s = JS_NewStringLen(c, arg.data(), arg.size()); av[1] = JS_Call(c, d->info, JS_UNDEFINED, 1, &s); JS_FreeValue(c, s);
    if (JS_IsException(av[1])) { JS_FreeValue(c, JS_GetException(c)); av[1] = JS_NewObject(c); } n = 2; }
  else if (e == EV_JointBreak) { av[0] = JS_NewStringLen(c, arg.data(), arg.size()); n = 1; }
  else if (e == EV_VehicleEnter || e == EV_VehicleExit) { av[0] = actorArg(arg); n = 1; }
  d->deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  JSValue r = JS_Call(c, fn, b.obj, n, av);
  for (int k = 0; k < n; k++) JS_FreeValue(c, av[k]);
  JS_FreeValue(c, fn);
  if (JS_IsException(r)) {
    std::string msg = errorText(c); if (msg.find("interrupted") != std::string::npos) msg += " - the script ran for more than 2 seconds (endless loop?)";
    logMsg("error: " + d->b[i].script + "." + EVENTS[e] + " on " + d->b[i].actorName + ": " + msg, LogKind::Error);
    d->b[i].ev[e] = false;   // stop calling the broken event, like 0.1
  }
  JS_FreeValue(c, r);
}
void ScriptHost::drop(int i) { if (i >= 0 && i < (int)d->b.size()) d->b[i].alive = false; }
const std::string& ScriptHost::actorOf(int i) const { return d->b[i].actor; }
int ScriptHost::count() const { return (int)d->b.size(); }
void ScriptHost::uiDispatch(const std::string& events) {
  if (!d->ctx) return; JSContext* c = d->ctx; JSValue g = JS_GetGlobalObject(c), fn = JS_GetPropertyStr(c, g, "__uiDispatch");
  JSValue arg = JS_NewStringLen(c, events.data(), events.size()); d->deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  JSValue r = JS_Call(c, fn, JS_UNDEFINED, 1, &arg);
  if (JS_IsException(r)) logMsg("error: ui: " + errorText(c), LogKind::Error);
  JS_FreeValue(c, r); JS_FreeValue(c, arg); JS_FreeValue(c, fn); JS_FreeValue(c, g);
}
void ScriptHost::runJobs() { if (!d->ctx) return; JSContext* c2; for (int k = 0; k < 64 && JS_ExecutePendingJob(d->rt, &c2) > 0; k++) {} }

std::string ScriptHost::check(const std::string& code) {
  JSRuntime* rt = JS_NewRuntime(); JSContext* c = JS_NewContext(rt); std::string src = wrap(code), out;
  JSValue r = JS_Eval(c, src.c_str(), src.size(), "script", JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
  if (JS_IsException(r)) out = errorText(c); else JS_FreeValue(c, r);
  JS_FreeContext(c); JS_FreeRuntime(rt); return out;
}
}
