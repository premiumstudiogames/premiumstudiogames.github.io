#include "project.h"
#include "blueprint.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <random>
#include <chrono>
#include <filesystem>
#include <cstdlib>
#include <SDL.h>
#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace pge {

const char* MESHES[] = {"Cube", "Sphere", "Cylinder", "Cone", "Plane", "Capsule", "Torus", "Wedge"};
const int MESH_COUNT = 8;
const char* ACTOR_TYPES[] = {"Empty", "PlayerStart", "TriggerBox", "CameraActor", "TextRender", "DirectionalLight", "PointLight", "SpotLight", "StaticMesh", "ReflectionCapture"};
const int ACTOR_TYPE_COUNT = 10;

const char* actorTypeLabel(const std::string& t) {
  if (t == "StaticMesh") return "Static Mesh"; if (t == "Empty") return "Empty Actor"; if (t == "PointLight") return "Point Light"; if (t == "SpotLight") return "Spot Light";
  if (t == "DirectionalLight") return "Directional Light"; if (t == "PlayerStart") return "Player Start"; if (t == "CameraActor") return "Camera Actor"; if (t == "TriggerBox") return "Trigger Box";
  if (t == "TextRender") return "Text Render"; if (t == "ReflectionCapture") return "Sphere Reflection Capture"; return "Actor";
}
const char* actorTypeIcon(const std::string& t) {
  if (t == "StaticMesh") return "[M]"; if (t == "PointLight" || t == "SpotLight" || t == "DirectionalLight") return "[L]"; if (t == "PlayerStart") return "[P]";
  if (t == "CameraActor") return "[C]"; if (t == "TriggerBox") return "[T]"; if (t == "TextRender") return "[A]"; if (t == "ReflectionCapture") return "[R]"; return "[ ]";
}

double nowMs() { return (double)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
std::string newId(const char* prefix) {
  static std::mt19937_64 rng{std::random_device{}() ^ (uint64_t)nowMs()};
  static const char* A = "abcdefghijklmnopqrstuvwxyz0123456789"; std::string s = prefix; for (int i = 0; i < 8; i++) s += A[rng() % 36]; return s;
}
std::string colorHex(const glm::vec3& c) {
  char b[8]; auto q = [](float v) { int i = (int)std::lround(glm::clamp(v, 0.f, 1.f) * 255); return i; };
  std::snprintf(b, sizeof b, "#%02x%02x%02x", q(c.r), q(c.g), q(c.b)); return b;
}
glm::vec3 hexColor(const std::string& s, glm::vec3 fb) {
  if (s.size() < 7 || s[0] != '#') return fb; unsigned v = 0; if (std::sscanf(s.c_str() + 1, "%06x", &v) != 1) return fb;
  return {((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f};
}
std::string sanitizeName(const std::string& s) {
  // letters of any language stay (UTF-8 bytes >= 0x80), ASCII symbols and spaces become '_'
  std::string o; for (char c : s) { unsigned char u = (unsigned char)c; o += (u >= 0x80 || std::isalnum(u) || c == '_' || c == '-') ? c : '_'; }
  if (o.size() > 40) { size_t n = 40; while (n > 0 && ((unsigned char)o[n] & 0xC0) == 0x80) n--; o = o.substr(0, n); }   // never cut a letter in half
  if (o.empty()) o = "MyProject"; return o;
}

Actor newActor(const std::string& type, const std::string& name) {
  Actor a; a.id = newId("a"); a.type = type; a.name = name.empty() ? type : name;
  if (type == "PointLight") a.light = {hexColor("#ffd9a0"), 30, 12, 35, true};
  if (type == "SpotLight") a.light = {glm::vec3(1), 60, 20, 35, true};
  if (type == "DirectionalLight") a.light = {hexColor("#fff4e0"), 2.2f, 0, 0, true};
  if (type == "TriggerBox") a.s = glm::vec3(2);
  return a;
}

const char* SCRIPT_TEMPLATE = R"(// Runs once when the game starts.
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
)";
static const char* SC_ROTATOR = R"(// Spins this actor. Change speed in degrees per second.
const speed = 90;

function onUpdate(dt) {
  self.rotation.y += speed * dt * Math.PI / 180;
}
)";
static const char* SC_PICKUP = R"(// A coin: spins, bobs, and adds to the score when the player touches it.
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
)";
static const char* SC_GOAL = R"(// Shows a message when the player reaches this trigger.
function onOverlap(other) {
  world.message('Goal reached in ' + world.time.toFixed(1) + 's');
}
)";
static const char* SC_LAUNCH = R"(// A jump pad: throws the player up when they step on it.
function onOverlap(other) {
  if (other.isPlayer) other.launch(0, 11, 0);
}
)";

static const char* SC_BLOCKS = R"(// A BLOCKFALL-style block world. The ground is made in 16 m chunks on background threads:
// 20 cm blocks near the surface (the near layer), 1 m blocks deeper down and in the mountains.
// Left click: break.  Right click: place.  1 - 5: block size 20 cm - 1 m.  Q / E: change block.
const BLOCKS = [
  [1, 'Grass', ['#62a544', '#6f8f3e', '#7a5a3a']],
  [2, 'Dirt', '#7a5a3a'],
  [3, 'Stone', '#8b9097'],
  [4, 'Sand', '#dcca92'],
  [5, 'Planks', '#b88a55'],
  [6, 'Brick', '#a8513f'],
  [7, 'Snow', '#f2f5f8'],
];
let size = 0.2, pick = 5, spawned = false;

function onStart() {
  voxels.create({ bottom: -32, top: 64, nearLayer: [-6, 14], viewDistance: 6, seed: 2026 });
  for (const [id, name, color] of BLOCKS) voxels.defineBlock(id, { name, color });
  // runs on background threads, once per chunk: (chunk, noise, params)
  voxels.setGenerator(function (chunk, noise) {
    for (let x = 0; x < 16; x++) for (let z = 0; z < 16; z++) {
      const wx = chunk.x + x, wz = chunk.z + z;
      const mountain = Math.max(0, noise.fbm(wx * 0.004, wz * 0.004, 3)) * 40;
      const h = noise.fbm(wx * 0.02, wz * 0.02, 4) * 6 + 3 + mountain;
      const top = Math.floor(h);
      chunk.column(x, z, -32, top - 3, 3);
      chunk.column(x, z, top - 3, top - 1, mountain > 12 ? 3 : 2);
      chunk.column(x, z, top - 1, top, h > 26 ? 7 : h < 0.5 ? 4 : 1);
      // the rest of the metre in 20 cm steps: smooth hills
      const rest = Math.floor((h - top) * 5);
      for (let k = 0; k < rest; k++) for (let a = 0; a < 5; a++) for (let c = 0; c < 5; c++)
        chunk.set(x + a / 5, top + k / 5, z + c / 5, h > 26 ? 7 : h < 0.5 ? 4 : 1, 0.2);
      if (noise.hash(wx, 7, wz) < 0.012) chunk.set(x, top + rest / 5, z, 3, 0.6);   // rocks
    }
  });
  world.hud('help', 'Left click: break   Right click: place   1-5: size   Q / E: block');
}

function onUpdate(dt) {
  if (!spawned && voxels.ready(0, 0, 0)) {   // put the player on the ground once it is built
    world.player.teleport(0.5, voxels.topY(0.5, 0.5) + 1.2, 0.5); spawned = true;
  }
  for (let i = 1; i <= 5; i++) if (input.pressed(String(i))) size = i / 5;
  if (input.pressed('e')) pick = pick % BLOCKS.length + 1;
  if (input.pressed('q')) pick = (pick + BLOCKS.length - 2) % BLOCKS.length + 1;
  const cam = world.camera, hit = voxels.raycast(cam.position, cam.forward, 7);
  if (hit && input.pressed('MouseLeft')) voxels.removeBlock(hit.point.clone().addScaledVector(hit.normal, -0.05));
  if (hit && input.pressed('MouseRight')) {
    const p = hit.placePosition(size);
    if (!voxels.setBlock(p, pick, size)) world.message(voxels.lastError, 1.5);
  }
  world.hud('block', BLOCKS[pick - 1][1] + '  ' + Math.round(size * 100) + ' cm' + (hit ? '   looking at: ' + BLOCKS[hit.id - 1][1] : ''));
}
)";

static Material mat(const char* n, const char* c, float m, float r, const char* e = "#000000", float op = 1) { Material x; x.name = n; x.color = hexColor(c); x.metalness = m; x.roughness = r; x.emissive = hexColor(e, glm::vec3(0)); x.opacity = op; return x; }
static Actor mesh(const char* name, const char* m, glm::vec3 p, glm::vec3 s, const char* material, glm::vec3 r = glm::vec3(0)) { Actor a = newActor("StaticMesh", name); a.mesh = m; a.p = p; a.s = s; a.material = material; a.r = r; return a; }

Project makeTemplate(const std::string& kind, const std::string& name) {
  Project P; P.id = newId("p"); P.name = name.empty() ? "MyProject" : name; P.templ = kind; P.engine = PGE_VERSION; P.created = P.updated = nowMs();
  P.materials["M_Default"] = Material();
  P.materials["M_Floor"] = mat("M_Floor", "#5d6168", 0, 0.92f); P.materials["M_Wall"] = mat("M_Wall", "#a9aeb6", 0, 0.75f);
  P.materials["M_Accent"] = mat("M_Accent", "#3a7bff", 0.1f, 0.4f); P.materials["M_Gold"] = mat("M_Gold", "#e3b341", 0.95f, 0.25f, "#3a2a00");
  P.materials["M_Red"] = mat("M_Red", "#e05252", 0, 0.5f); P.materials["M_Glass"] = mat("M_Glass", "#9fd3ff", 0.1f, 0.05f, "#000000", 0.35f);
  Level L; auto& A = L.actors;
  A.push_back(mesh("Floor", "Cube", {0, -0.5f, 0}, {60, 1, 60}, "M_Floor"));
  { Actor s = newActor("DirectionalLight", "Sun"); s.p = {0, 12, 0}; s.r = {-50, 35, 0}; A.push_back(s); }
  L.world.gameMode = "Spectator";
  if (kind == "Blank") { Actor ps = newActor("PlayerStart", "PlayerStart"); ps.p = {0, 1, 8}; A.push_back(ps); }
  if (kind == "Blocks") {
    A.erase(A.begin());   // no floor: the ground is blocks
    L.world.gameMode = "FirstPerson"; L.world.fog = true; L.world.fogDensity = 0.011f; L.world.killZ = -60;
    for (auto& a : A) if (a.type == "DirectionalLight") { a.r = {-55, 40, 0}; a.light.intensity = 2.4f; }
    { Actor ps = newActor("PlayerStart", "PlayerStart"); ps.p = {0.5f, 40, 0.5f}; A.push_back(ps); }
    { Actor w = newActor("Empty", "BlockWorld"); w.script = "SC_BlockWorld"; A.push_back(w); }
    P.scripts["SC_BlockWorld"] = {"SC_BlockWorld", SC_BLOCKS};
  }
  if (kind == "FirstPerson" || kind == "ThirdPerson") {
    L.world.gameMode = kind;
    { Actor ps = newActor("PlayerStart", "PlayerStart"); ps.p = {0, 1, 10}; A.push_back(ps); }
    float W[4][4] = {{0, -15, 30, 1}, {0, 15, 30, 1}, {-15, 0, 1, 30}, {15, 0, 1, 30}};
    for (auto& w : W) A.push_back(mesh("Wall", "Cube", {w[0], 1.5f, w[1]}, {w[2], 3, w[3]}, "M_Wall"));
    A.push_back(mesh("Ramp", "Wedge", {-7, 1, -4}, {4, 2, 4}, "M_Wall", {0, -90, 0}));   // its high end meets the platform
    A.push_back(mesh("Platform", "Cube", {-7, 1, -9}, {4, 2, 6}, "M_Wall"));
    for (int i = 0; i < 4; i++) A.push_back(mesh("Step", "Cube", {7, 0.25f + i * 0.5f, -2.f - i}, {3, 0.5f + i, 1}, "M_Wall"));
    for (int i = 0; i < 6; i++) { Actor c = mesh("PhysicsCube", "Cube", {-2 + (i % 3) * 1.1f, 0.5f + (i / 3) * 1.05f, -2}, glm::vec3(1), "M_Accent"); c.physics.simulate = true; c.physics.mass = 5; A.push_back(c); }
    { Actor b = mesh("PhysicsBall", "Sphere", {3, 3, 0}, glm::vec3(1.2f), "M_Red"); b.physics.simulate = true; A.push_back(b); }
    // a Blueprint to start from: Event Tick → Add Actor Rotation (Speed degrees a second, a variable you can change)
    { Blueprint bp; bp.vars.push_back({"Speed", PinType::Number, 90});
      auto node = [&](int id, const char* type, float x, float y, json v = json::object()) { BpNode n; n.id = id; n.type = type; n.x = x; n.y = y; n.values = v; bp.nodes.push_back(n); };
      node(1, "Event.Tick", 0, 0); node(2, "Actor.AddRotation", 448, 0); node(3, "Var.Get", 0, 176, {{"Var", "Speed"}}); node(4, "Math.Multiply", 160, 160); node(5, "Vector.Make", 304, 144);
      node(6, "Comment", -32, -64, {{"Text", "Spins this actor: Speed degrees a second"}}); bp.nodes.back().w = 720; bp.nodes.back().h = 320;
      bp.links = {{1, "then", 2, "exec"}, {1, "DeltaSeconds", 4, "A"}, {3, "Value", 4, "B"}, {4, "ReturnValue", 5, "Y"}, {5, "ReturnValue", 2, "DeltaRotation"}}; bp.nextId = 7;
      std::vector<BpMessage> m; Script sc{"BP_Rotator", bpCompile(bp, "BP_Rotator", m)}; sc.graph = bpToJson(bp); P.scripts["BP_Rotator"] = sc;
      Actor sp = mesh("Spinner", "Cube", {10, 1.5f, 0}, glm::vec3(1), "M_Accent"); sp.script = "BP_Rotator"; A.push_back(sp); }
    P.scripts["SC_Pickup"] = {"SC_Pickup", SC_PICKUP}; P.scripts["SC_Rotator"] = {"SC_Rotator", SC_ROTATOR}; P.scripts["SC_Goal"] = {"SC_Goal", SC_GOAL}; P.scripts["SC_JumpPad"] = {"SC_JumpPad", SC_LAUNCH};
    float C[5][3] = {{-7, 2.6f, -9}, {7, 2.6f, -5}, {0, 1, -8}, {10, 1, 8}, {-10, 1, 8}};
    for (auto& c : C) { Actor x = mesh("Coin", "Torus", {c[0], c[1], c[2]}, glm::vec3(0.8f), "M_Gold"); x.script = "SC_Pickup"; x.physics.collision = false; A.push_back(x); }
    { Actor pad = mesh("JumpPad", "Cylinder", {4, 0.05f, 6}, {2, 0.1f, 2}, "M_Accent"); pad.script = "SC_JumpPad"; A.push_back(pad); }
    { Actor t = newActor("TriggerBox", "GoalTrigger"); t.p = {-7, 3, -9}; t.s = {4, 2, 6}; t.script = "SC_Goal"; A.push_back(t); }
    { Actor t = newActor("TextRender", "Welcome"); t.p = {0, 2.6f, -14.4f}; t.text.value = P.name; A.push_back(t); }
    { Actor l = newActor("PointLight", "PointLight"); l.p = {7, 3, -5}; A.push_back(l); }
  }
  P.levels.push_back({"Main", L});
  return P;
}

/* ------------------------------------------------------------------ JSON */
static json v3(const glm::vec3& v) { return json::array({v.x, v.y, v.z}); }
static glm::vec3 v3(const json& j, glm::vec3 d) { if (!j.is_array() || j.size() < 3) return d; return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()}; }
template <class T> static T get(const json& j, const char* k, T d) { auto it = j.find(k); if (it == j.end() || it->is_null()) return d; try { return it->get<T>(); } catch (...) { return d; } }
static glm::vec3 col(const json& j, const char* k, glm::vec3 d) { auto it = j.find(k); if (it == j.end() || !it->is_string()) return d; return hexColor(it->get<std::string>(), d); }

static json actorJson(const Actor& a) {
  json j = {{"id", a.id}, {"name", a.name}, {"type", a.type}, {"parent", a.parent.empty() ? json() : json(a.parent)}, {"p", v3(a.p)}, {"r", v3(a.r)}, {"s", v3(a.s)}, {"visible", a.visible}, {"tags", a.tags}};
  if (a.type == "StaticMesh") { j["mesh"] = a.mesh; j["material"] = a.material; j["physics"] = {{"simulate", a.physics.simulate}, {"mass", a.physics.mass}, {"collision", a.physics.collision}};
    if (!a.anim.empty()) j["animation"] = {{"name", a.anim}, {"loop", a.animLoop}, {"speed", a.animSpeed}}; }
  if (a.type == "ReflectionCapture") j["probe"] = {{"radius", a.probe.radius}, {"intensity", a.probe.intensity}};
  if (a.isLight()) j["light"] = {{"color", colorHex(a.light.color)}, {"intensity", a.light.intensity}, {"range", a.light.range}, {"angle", a.light.angle}, {"shadows", a.light.shadows}};
  if (a.type == "TextRender") j["text"] = {{"value", a.text.value}, {"color", colorHex(a.text.color)}, {"size", a.text.size}};
  if (!a.script.empty()) j["script"] = a.script;
  return j;
}
static Actor actorFrom(const json& j) {
  Actor a = newActor(get<std::string>(j, "type", "Empty")); a.id = get<std::string>(j, "id", a.id); a.name = get<std::string>(j, "name", a.type);
  a.parent = j.contains("parent") && j["parent"].is_string() ? j["parent"].get<std::string>() : "";
  a.p = v3(j.value("p", json()), glm::vec3(0)); a.r = v3(j.value("r", json()), glm::vec3(0)); a.s = v3(j.value("s", json()), glm::vec3(1));
  a.visible = get<bool>(j, "visible", true); if (j.contains("tags") && j["tags"].is_array()) for (auto& t : j["tags"]) if (t.is_string()) a.tags.push_back(t);
  a.mesh = get<std::string>(j, "mesh", "Cube"); a.material = get<std::string>(j, "material", "M_Default");
  if (j.contains("animation") && j["animation"].is_object()) { const json& an = j["animation"]; a.anim = get<std::string>(an, "name", ""); a.animLoop = get<bool>(an, "loop", true); a.animSpeed = get<float>(an, "speed", 1.f); } a.script = get<std::string>(j, "script", "");
  if (j.contains("physics")) { auto& p = j["physics"]; a.physics.simulate = get<bool>(p, "simulate", false); a.physics.mass = get<float>(p, "mass", 1); a.physics.collision = get<bool>(p, "collision", true); }
  if (j.contains("probe") && j["probe"].is_object()) { auto& q = j["probe"]; a.probe.radius = get<float>(q, "radius", 10.f); a.probe.intensity = get<float>(q, "intensity", 1.f); }
  if (j.contains("light")) { auto& l = j["light"]; a.light.color = col(l, "color", a.light.color); a.light.intensity = get<float>(l, "intensity", a.light.intensity); a.light.range = get<float>(l, "range", a.light.range); a.light.angle = get<float>(l, "angle", a.light.angle); a.light.shadows = get<bool>(l, "shadows", true); }
  if (j.contains("text")) { auto& t = j["text"]; a.text.value = get<std::string>(t, "value", "Text"); a.text.color = col(t, "color", glm::vec3(1)); a.text.size = get<float>(t, "size", 0.6f); }
  return a;
}
static json worldJson(const WorldSettings& w) {
  json j = {{"gameMode", w.gameMode}, {"sky", colorHex(w.sky)}, {"skyLight", colorHex(w.skyLight)}, {"groundLight", colorHex(w.groundLight)}, {"ambient", w.ambient}, {"exposure", w.exposure},
          {"gravity", w.gravity}, {"killZ", w.killZ}, {"fog", w.fog}, {"fogColor", colorHex(w.fogColor)}, {"fogDensity", w.fogDensity}, {"fov", w.fov}};
  if (!w.post.isDefault()) { const PostProcess& q = w.post; j["post"] = {{"vignette", q.vignette}, {"grain", q.grain}, {"chromatic", q.chromatic}, {"scanlines", q.scanlines}, {"vhs", q.vhs},
      {"saturation", q.saturation}, {"contrast", q.contrast}, {"temperature", q.temperature}, {"bloom", q.bloom}, {"tint", colorHex(q.tint)}}; }
  if (!w.nav.isDefault()) j["navigation"] = {{"radius", w.nav.radius}, {"height", w.nav.height}, {"step", w.nav.step}, {"maxSlope", w.nav.maxSlope}, {"cell", w.nav.cell}};
  return j;
}
static WorldSettings worldFrom(const json& j) {
  WorldSettings w; if (!j.is_object()) return w;
  w.gameMode = get<std::string>(j, "gameMode", w.gameMode); w.sky = col(j, "sky", w.sky); w.skyLight = col(j, "skyLight", w.skyLight); w.groundLight = col(j, "groundLight", w.groundLight);
  w.ambient = get<float>(j, "ambient", w.ambient); w.exposure = get<float>(j, "exposure", w.exposure); w.gravity = get<float>(j, "gravity", w.gravity); w.killZ = get<float>(j, "killZ", w.killZ);
  w.fog = get<bool>(j, "fog", false); w.fogColor = col(j, "fogColor", w.sky); w.fogDensity = get<float>(j, "fogDensity", w.fogDensity); w.fov = get<float>(j, "fov", w.fov);
  if (j.contains("post") && j["post"].is_object()) { const json& q = j["post"]; PostProcess& o = w.post; o.vignette = get<float>(q, "vignette", 0); o.grain = get<float>(q, "grain", 0); o.chromatic = get<float>(q, "chromatic", 0);
    o.scanlines = get<float>(q, "scanlines", 0); o.vhs = get<float>(q, "vhs", 0); o.saturation = get<float>(q, "saturation", 1); o.contrast = get<float>(q, "contrast", 1); o.temperature = get<float>(q, "temperature", 0);
    o.bloom = get<float>(q, "bloom", 1); o.tint = col(q, "tint", glm::vec3(1)); }
  if (j.contains("navigation") && j["navigation"].is_object()) { const json& q = j["navigation"]; NavSettings& n = w.nav; n.radius = get<float>(q, "radius", n.radius); n.height = get<float>(q, "height", n.height);
    n.step = get<float>(q, "step", n.step); n.maxSlope = get<float>(q, "maxSlope", n.maxSlope); n.cell = get<float>(q, "cell", n.cell); }
  return w;
}
static json levelJ(const Level& l) { json a = json::array(); for (auto& x : l.actors) a.push_back(actorJson(x)); return {{"actors", a}, {"world", worldJson(l.world)}}; }
static Level levelF(const json& j) { Level l; if (j.contains("actors")) for (auto& a : j["actors"]) l.actors.push_back(actorFrom(a)); if (j.contains("world")) l.world = worldFrom(j["world"]); return l; }
std::string levelToJson(const Level& l) { return levelJ(l).dump(); }
bool levelFromJson(const std::string& t, Level& out) { try { out = levelF(json::parse(t)); return true; } catch (...) { return false; } }

std::string toJson(const Project& p, int indent) {
  json levels = json::object(); for (auto& [n, l] : p.levels) levels[n] = levelJ(l);
  json mats = json::object(); for (auto& [id, m] : p.materials) { json& j = mats[id]; j = {{"name", m.name}, {"color", colorHex(m.color)}, {"metalness", m.metalness}, {"roughness", m.roughness}, {"emissive", colorHex(m.emissive)}, {"emissiveIntensity", m.emissiveIntensity}, {"opacity", m.opacity}};
    const std::pair<const char*, const std::string*> maps[6] = {{"baseMap", &m.baseMap}, {"normalMap", &m.normalMap}, {"roughnessMap", &m.roughnessMap}, {"metalMap", &m.metalMap}, {"aoMap", &m.aoMap}, {"emissiveMap", &m.emissiveMap}};
    for (auto& [k, v] : maps) if (!v->empty()) j[k] = *v;
    if (m.hasMaps()) { j["tiling"] = {m.tiling.x, m.tiling.y}; j["normalStrength"] = m.normalStrength; j["aoStrength"] = m.aoStrength; }
    if (m.worldAligned) j["worldAligned"] = true; if (m.directXNormal) j["directXNormal"] = true; if (m.masked) { j["masked"] = true; j["cutoff"] = m.cutoff; } }
  json scr = json::object(); for (auto& [id, s] : p.scripts) { scr[id] = {{"name", s.name}, {"code", s.code}}; if (s.blueprint()) scr[id]["blueprint"] = s.graph; }
  json j = {{"id", p.id}, {"name", p.name}, {"template", p.templ}, {"engine", PGE_VERSION}, {"created", p.created}, {"updated", p.updated}, {"description", p.description},
            {"startLevel", p.startLevel}, {"levelOrder", json::array()}, {"levels", levels}, {"assets", {{"materials", mats}, {"scripts", scr}}}};
  for (auto& l : p.levels) j["levelOrder"].push_back(l.first);
  return j.dump(indent);
}
bool fromJson(const std::string& text, Project& P, std::string* err) {
  try {
    json j = json::parse(text); Project p;
    p.id = get<std::string>(j, "id", newId("p")); p.name = get<std::string>(j, "name", "MyProject"); p.templ = get<std::string>(j, "template", "Blank");
    p.description = get<std::string>(j, "description", ""); p.startLevel = get<std::string>(j, "startLevel", "Main"); p.created = get<double>(j, "created", nowMs()); p.updated = get<double>(j, "updated", nowMs());
    std::vector<std::string> order; if (j.contains("levelOrder")) for (auto& n : j["levelOrder"]) order.push_back(n);
    if (j.contains("levels")) for (auto& [n, l] : j["levels"].items()) if (std::find(order.begin(), order.end(), n) == order.end()) order.push_back(n);
    for (auto& n : order) if (j["levels"].contains(n)) p.levels.push_back({n, levelF(j["levels"][n])});
    if (p.levels.empty()) { if (err) *err = "The project has no levels"; return false; }
    if (!p.level(p.startLevel)) p.startLevel = p.levels[0].first;
    if (j.contains("assets")) {
      auto& as = j["assets"];
      if (as.contains("materials")) for (auto& [id, m] : as["materials"].items()) { Material x; x.name = get<std::string>(m, "name", id); x.color = col(m, "color", x.color); x.metalness = get<float>(m, "metalness", 0); x.roughness = get<float>(m, "roughness", 0.6f); x.emissive = col(m, "emissive", glm::vec3(0)); x.emissiveIntensity = get<float>(m, "emissiveIntensity", 1); x.opacity = get<float>(m, "opacity", 1);
        for (auto [k, v] : {std::make_pair("baseMap", &x.baseMap), std::make_pair("normalMap", &x.normalMap), std::make_pair("roughnessMap", &x.roughnessMap), std::make_pair("metalMap", &x.metalMap), std::make_pair("aoMap", &x.aoMap), std::make_pair("emissiveMap", &x.emissiveMap)}) *v = get<std::string>(m, k, "");
        if (m.contains("tiling") && m["tiling"].is_array() && m["tiling"].size() == 2 && m["tiling"][0].is_number() && m["tiling"][1].is_number()) x.tiling = {m["tiling"][0].get<float>(), m["tiling"][1].get<float>()};
        x.normalStrength = get<float>(m, "normalStrength", 1); x.aoStrength = get<float>(m, "aoStrength", 1); x.worldAligned = get<bool>(m, "worldAligned", false); x.directXNormal = get<bool>(m, "directXNormal", false);
        x.masked = get<bool>(m, "masked", false); x.cutoff = get<float>(m, "cutoff", 0.5f); p.materials[id] = x; }
      if (as.contains("scripts")) for (auto& [id, s] : as["scripts"].items()) { p.scripts[id] = {get<std::string>(s, "name", id), get<std::string>(s, "code", "")}; if (s.contains("blueprint") && s["blueprint"].is_object()) p.scripts[id].graph = s["blueprint"]; }
    }
    if (!p.materials.count("M_Default")) p.materials["M_Default"] = Material();
    P = std::move(p); return true;
  } catch (std::exception& e) { if (err) *err = e.what(); return false; }
}
bool saveProjectFile(Project& p, const std::string& file, std::string* err) {
  try {
    p.updated = nowMs(); fs::create_directories(fs::u8path(file).parent_path());
    std::string tmp = file + ".tmp";
    { std::ofstream o(fs::u8path(tmp), std::ios::binary); if (!o) { if (err) *err = "Can't write " + file; return false; } o << toJson(p); }
    fs::rename(fs::u8path(tmp), fs::u8path(file)); p.path = file; return true;
  } catch (std::exception& e) { if (err) *err = e.what(); return false; }
}
bool loadProjectFile(const std::string& file, Project& out, std::string* err) {
  std::ifstream i(fs::u8path(file), std::ios::binary); if (!i) { if (err) *err = "Can't open " + file; return false; }
  std::stringstream ss; ss << i.rdbuf(); if (!fromJson(ss.str(), out, err)) return false; out.path = file; return true;
}

static std::string g_docs;
void setDocumentsDir(const std::string& dir) { g_docs = dir; }
std::string documentsDir() {
  if (!g_docs.empty()) return g_docs;
#ifdef _WIN32
  // the real Documents folder (also when moved to OneDrive or another drive), as UTF-8 so non-English user names work
  std::string base = ".";
  PWSTR w = nullptr;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &w)) && w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n > 1) { base.assign(n - 1, '\0'); WideCharToMultiByte(CP_UTF8, 0, w, -1, base.data(), n, nullptr, nullptr); }
  }
  if (w) CoTaskMemFree(w);
  return base + "\\PremiumEngine";
#else
  const char* h = std::getenv("HOME"); return std::string(h ? h : ".") + "/Documents/PremiumEngine";
#endif
}
std::string projectsDir() { return (fs::u8path(documentsDir()) / "Projects").u8string(); }
std::string buildsDir() { return (fs::u8path(documentsDir()) / "Builds").u8string(); }
std::string exeDir() { char* b = SDL_GetBasePath(); std::string s = b ? b : "."; SDL_free(b); while (s.size() > 1 && (s.back() == '/' || s.back() == '\\')) s.pop_back(); return s; }
}
