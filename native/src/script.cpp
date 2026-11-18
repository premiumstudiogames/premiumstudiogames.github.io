#include "script.h"
#include "log.h"
#include <quickjs.h>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <map>
#include <cmath>
#include "voxel.h"
#include "project.h"
#include <nlohmann/json.hpp>

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
const __xyz = (x, y, z) => (x !== null && typeof x === 'object') ? [+x.x || 0, +x.y || 0, +x.z || 0] : [+x || 0, +y || 0, +z || 0];
const __apis = new Map();
const __player = { name: 'Player', isPlayer: true, tags: [],
  get position() { const a = __n.playerPos(); return new Vec3(a[0], a[1], a[2]); },
  teleport(x, y, z) { __n.teleport(...__xyz(x, y, z)); },
  launch(x, y, z) { __n.launch(...__xyz(x, y, z)); },
  get swimming() { return __n.swimming(); } };
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
  spawn(mesh, o = {}) { return __api(__n.spawn(String(mesh), JSON.stringify(o || {}))); },
  destroy(x) { __n.destroy(x && x.id ? x.id : String(x)); },
  message(t, sec = 3) { __n.message(String(t), +sec || 3); },
  hud(id, t) { __n.hud(String(id), String(t)); },
  restart() { __n.restart(); },
  setGravity(y) { __n.setGravity(+y); } };
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
  raycast(from, dir, max = 8, o = {}) { const a = __xyz(from), b = __xyz(dir); const r = __n.voxRay(a[0], a[1], a[2], b[0], b[1], b[2], +max || 8, !!(o && o.liquids)); if (!r) return null;
    return { id: r[0], x: r[1], y: r[2], z: r[3], size: r[4], normal: new Vec3(r[5], r[6], r[7]), point: new Vec3(r[8], r[9], r[10]), distance: r[11], height: r[15],
      placePosition(size = 0.2, height = 0) { const p = __n.voxPlace(...r, +size, +height || 0); return new Vec3(p[0], p[1], p[2]); } }; },
  ready(x, y, z) { return __n.voxReady(...__xyz(x, y, z)); },
  topY(x, z) { return __n.voxTop(+x, +z); },
  // the player's changes, in Documents/PremiumEngine/Saves/<game>/<name>.pgworld
  save(name = 'world') { const r = __n.voxSave(String(name)); this.lastError = r === true ? '' : String(r); return r === true; },
  load(name = 'world') { const r = __n.voxLoad(String(name)); this.lastError = r === true ? '' : String(r); return r === true; },
  get stats() { return JSON.parse(__n.voxStats()); } };
const input = { key(k) { return __n.key(String(k)); }, pressed(k) { return __n.pressed(String(k)); },
  get mouse() { const m = __n.mouse(); return { dx: m[0], dy: m[1], left: !!m[2], right: !!m[3] }; } };
const math = { clamp: (v, a, b) => Math.max(a, Math.min(b, v)), lerp: (a, b, t) => a + (b - a) * t, random: (a = 0, b = 1) => a + Math.random() * (b - a), vec3: (x, y, z) => new Vec3(x, y, z) };
const THREE = { Vector3: Vec3, MathUtils: { clamp: math.clamp, lerp: math.lerp, degToRad: d => d * Math.PI / 180, radToDeg: r => r * 180 / Math.PI } };
globalThis.__make = function (fn, id, name) {
  const log = (...m) => __n.log('[' + name + '] ' + m.map(x => (x !== null && typeof x === 'object') ? JSON.stringify(x) : String(x)).join(' '));
  return fn(__api(id), world, input, log, THREE, math);
};
globalThis.__api = __api;
)JS";

static const char* EVENTS[EV_COUNT] = {"onStart", "onUpdate", "onOverlap", "onHit", "onKey"};

struct Behaviour { std::string actor, actorName, script; JSValue obj; bool ev[EV_COUNT]; bool alive = true; };
struct ScriptHost::Impl {
  JSRuntime* rt = nullptr; JSContext* ctx = nullptr; ScriptWorld* w = nullptr;
  std::vector<Behaviour> b; std::chrono::steady_clock::time_point deadline;
  JSValue make = JS_UNDEFINED, api = JS_UNDEFINED;
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
  FN("info", 1, { std::string s = W(c)->actorInfo(S(c, a[0])); return JS_NewStringLen(c, s.data(), s.size()); });
  FN("find", 1, { std::string s = W(c)->find(S(c, a[0])); return s.empty() ? JS_NULL : JS_NewStringLen(c, s.data(), s.size()); });
  FN("findAll", 1, { auto v = W(c)->findAll(S(c, a[0])); JSValue r = JS_NewArray(c); for (size_t i = 0; i < v.size(); i++) JS_SetPropertyUint32(c, r, (uint32_t)i, JS_NewStringLen(c, v[i].data(), v[i].size())); return r; });
  FN("spawn", 2, { std::string s = W(c)->spawn(S(c, a[0]), S(c, a[1])); return JS_NewStringLen(c, s.data(), s.size()); });
  FN("message", 2, W(c)->message(S(c, a[0]), (float)D(c, a[1])); return JS_UNDEFINED;);
  FN("hud", 2, W(c)->hud(S(c, a[0]), S(c, a[1])); return JS_UNDEFINED;);
  FN("restart", 0, W(c)->restart(); return JS_UNDEFINED;);
  FN("setGravity", 1, W(c)->setGravity((float)D(c, a[0])); return JS_UNDEFINED;);
  FN("time", 0, return JS_NewFloat64(c, W(c)->time()););
  FN("playerPos", 0, return A3(c, W(c)->playerPos()););
  FN("teleport", 3, W(c)->playerTeleport({(float)D(c, a[0]), (float)D(c, a[1]), (float)D(c, a[2])}); return JS_UNDEFINED;);
  FN("launch", 3, W(c)->playerLaunch({(float)D(c, a[0]), (float)D(c, a[1]), (float)D(c, a[2])}); return JS_UNDEFINED;);
  FN("key", 1, return JS_NewBool(c, W(c)->key(keyAlias(S(c, a[0]))));) ;
  FN("pressed", 1, return JS_NewBool(c, W(c)->pressed(keyAlias(S(c, a[0]))));) ;
  FN("mouse", 0, { glm::vec4 m = W(c)->mouse(); JSValue r = JS_NewArray(c); for (int i = 0; i < 4; i++) JS_SetPropertyUint32(c, r, i, JS_NewFloat64(c, m[i])); return r; });
  FN("log", 1, W(c)->log(S(c, a[0])); return JS_UNDEFINED;);
  FN("trophy", 1, W(c)->trophy(S(c, a[0])); return JS_UNDEFINED;);
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
  FN("swimming", 0, return JS_NewBool(c, W(c)->playerSwimming()););
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
  JSValue g = JS_GetGlobalObject(d->ctx); d->make = JS_GetPropertyStr(d->ctx, g, "__make"); d->api = JS_GetPropertyStr(d->ctx, g, "__api"); JS_FreeValue(d->ctx, g);
  return true;
}
void ScriptHost::shutdown() {
  if (!d || !d->ctx) return;
  for (auto& b : d->b) JS_FreeValue(d->ctx, b.obj);
  d->b.clear(); JS_FreeValue(d->ctx, d->make); JS_FreeValue(d->ctx, d->api); d->make = d->api = JS_UNDEFINED;
  JS_FreeContext(d->ctx); JS_FreeRuntime(d->rt); d->ctx = nullptr; d->rt = nullptr;
}

static std::string wrap(const std::string& code) {
  // first user line stays line 1 so error lines match the editor
  return "(function (self, world, input, log, THREE, math) { \"use strict\"; " + code +
         "\n;return { onStart: typeof onStart === 'function' ? onStart : null, onUpdate: typeof onUpdate === 'function' ? onUpdate : null,"
         " onOverlap: typeof onOverlap === 'function' ? onOverlap : null, onHit: typeof onHit === 'function' ? onHit : null, onKey: typeof onKey === 'function' ? onKey : null }; })";
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
  JSValue fn = JS_GetPropertyStr(c, b.obj, EVENTS[e]); JSValue av[1]; int n = 0;
  if (e == EV_Update) { av[0] = JS_NewFloat64(c, num); n = 1; }
  else if (e == EV_Overlap || e == EV_Hit) {
    if (arg.empty()) av[0] = JS_NULL; else { JSValue s = JS_NewStringLen(c, arg.data(), arg.size()); av[0] = JS_Call(c, d->api, JS_UNDEFINED, 1, &s); JS_FreeValue(c, s); if (JS_IsException(av[0])) { JS_FreeValue(c, JS_GetException(c)); av[0] = JS_NULL; } }
    n = 1;
  } else if (e == EV_Key) { av[0] = JS_NewStringLen(c, arg.data(), arg.size()); n = 1; }
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
void ScriptHost::runJobs() { if (!d->ctx) return; JSContext* c2; for (int k = 0; k < 64 && JS_ExecutePendingJob(d->rt, &c2) > 0; k++) {} }

std::string ScriptHost::check(const std::string& code) {
  JSRuntime* rt = JS_NewRuntime(); JSContext* c = JS_NewContext(rt); std::string src = wrap(code), out;
  JSValue r = JS_Eval(c, src.c_str(), src.size(), "script", JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
  if (JS_IsException(r)) out = errorText(c); else JS_FreeValue(c, r);
  JS_FreeContext(c); JS_FreeRuntime(rt); return out;
}
}
