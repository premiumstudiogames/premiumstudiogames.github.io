#include "game.h"
#include "log.h"
#include "arabic.h"
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <filesystem>
#include <fstream>
#include <set>

using json = nlohmann::json;
namespace pge {

static const glm::vec3 AX(1, 0, 0), AY(0, 1, 0), AZ(0, 0, 1);
glm::quat quatYXZ(const glm::vec3& r) { return glm::angleAxis(r.y, AY) * glm::angleAxis(r.x, AX) * glm::angleAxis(r.z, AZ); }
glm::mat4 trsMatrix(const glm::vec3& p, const glm::vec3& r, const glm::vec3& s) {
  glm::mat4 m = glm::translate(glm::mat4(1), p) * glm::mat4_cast(quatYXZ(r));
  glm::vec3 sc = s; for (int i = 0; i < 3; i++) if (std::abs(sc[i]) < 1e-4f) sc[i] = sc[i] < 0 ? -1e-4f : 1e-4f;
  return glm::scale(m, sc);
}
glm::vec3 eulerYXZ(const glm::quat& q) {   // same as three.js Euler.setFromRotationMatrix(m, 'YXZ')
  glm::mat3 m = glm::mat3_cast(q); float m11 = m[0][0], m12 = m[1][0], m13 = m[2][0], m21 = m[0][1], m22 = m[1][1], m23 = m[2][1], m31 = m[0][2], m33 = m[2][2];
  glm::vec3 e; e.x = std::asin(-glm::clamp(m23, -1.f, 1.f));
  if (std::abs(m23) < 0.9999999f) { e.y = std::atan2(m13, m33); e.z = std::atan2(m21, m22); } else { e.y = std::atan2(-m31, m11); e.z = 0; }
  return e;
}
static void decompose(const glm::mat4& W, glm::vec3& pos, glm::quat& rot, glm::vec3& scale) {
  pos = glm::vec3(W[3]); scale = {glm::length(glm::vec3(W[0])), glm::length(glm::vec3(W[1])), glm::length(glm::vec3(W[2]))};
  glm::mat3 R(glm::vec3(W[0]) / std::max(scale.x, 1e-6f), glm::vec3(W[1]) / std::max(scale.y, 1e-6f), glm::vec3(W[2]) / std::max(scale.z, 1e-6f));
  rot = glm::normalize(glm::quat_cast(R));
}
static void meshBounds(const std::string& mesh, glm::vec3& mn, glm::vec3& mx) { const MeshData& d = meshShape(mesh); mn = d.bmin; mx = d.bmax; }

std::string keyCode(SDL_Scancode sc) {
  if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return std::string("Key") + (char)('A' + (sc - SDL_SCANCODE_A));
  if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return std::string("Digit") + (char)('1' + (sc - SDL_SCANCODE_1));
  if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) return "F" + std::to_string(1 + sc - SDL_SCANCODE_F1);
  switch (sc) {
    case SDL_SCANCODE_0: return "Digit0"; case SDL_SCANCODE_SPACE: return "Space"; case SDL_SCANCODE_LSHIFT: return "ShiftLeft"; case SDL_SCANCODE_RSHIFT: return "ShiftRight";
    case SDL_SCANCODE_LCTRL: return "ControlLeft"; case SDL_SCANCODE_RCTRL: return "ControlRight"; case SDL_SCANCODE_LALT: return "AltLeft"; case SDL_SCANCODE_RALT: return "AltRight";
    case SDL_SCANCODE_UP: return "ArrowUp"; case SDL_SCANCODE_DOWN: return "ArrowDown"; case SDL_SCANCODE_LEFT: return "ArrowLeft"; case SDL_SCANCODE_RIGHT: return "ArrowRight";
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: return "Enter"; case SDL_SCANCODE_ESCAPE: return "Escape"; case SDL_SCANCODE_TAB: return "Tab"; case SDL_SCANCODE_BACKSPACE: return "Backspace";
    default: { const char* n = SDL_GetScancodeName(sc); return n && *n ? n : "Unknown"; }
  }
}

void applyMaterial(DrawItem& d, const Material& m) {
  d.color = m.color; d.emissive = m.emissive * m.emissiveIntensity; d.metal = m.metalness; d.rough = m.roughness; d.opacity = m.opacity;
  if (!m.hasMaps()) return;
  auto tx = [](const std::string& f, bool srgb) -> unsigned { const Texture* t = texture(f, srgb); return t ? t->id : 0; };
  d.tex[0] = tx(m.baseMap, true); d.tex[1] = tx(m.roughnessMap, false); d.tex[2] = tx(m.metalMap, false); d.tex[3] = tx(m.normalMap, false); d.tex[4] = tx(m.aoMap, false); d.tex[5] = tx(m.emissiveMap, true);
  if (d.tex[5] && glm::length(d.emissive) < 1e-4f) d.emissive = glm::vec3(m.emissiveIntensity);   // an emissive picture with a black colour: the picture's own colours
  d.tiling = m.tiling; d.normalScale = m.normalStrength; d.aoStrength = m.aoStrength; d.worldUV = m.worldAligned; d.flipGreen = m.directXNormal;
  if (m.masked && d.tex[0]) d.cutoff = m.cutoff;
}
// a model file: one draw per part (primitive), with its own materials (or the actor's), posed by its animation
static void addModelDraws(Frame& f, const Project& P, const Actor& a, const glm::mat4& W, unsigned id, const glm::vec3* colorOverride, const AnimPlay* anim) {
  ModelRef m = model(a.mesh); if (!m || !m->ok()) { DrawItem d; d.mesh = &primitive("Cube"); d.model = W; d.id = id; d.color = glm::vec3(1, 0, 1); d.emissive = glm::vec3(0.4f, 0, 0.4f); f.draws.push_back(d); return; }   // missing: a pink box, like other engines
  f.keep.push_back(m);
  auto pose = std::make_shared<std::vector<glm::mat4>>(); ModelPose mp; poseModel(*m, anim, mp);
  *pose = mp.joints; f.bones.push_back(pose);
  auto it = a.material.empty() ? P.materials.end() : P.materials.find(a.material); const Material* over = it != P.materials.end() ? &it->second : nullptr;
  for (const ModelPrim& pr : m->prims) { if (pr.node < 0) continue;
    DrawItem d; d.mesh = &pr.gpu; d.id = id;
    if (pr.skin >= 0 && pr.skin < (int)m->skins.size() && !m->skins[pr.skin].joints.empty()) { d.model = W; d.bones = pose->data() + mp.skinAt[pr.skin]; d.boneCount = (int)m->skins[pr.skin].joints.size(); }
    else d.model = W * mp.world[pr.node];
    if (over) applyMaterial(d, *over);
    else if (pr.material >= 0 && pr.material < (int)m->mats.size()) { const ModelMaterial& mm = m->mats[pr.material];
      d.color = glm::vec3(mm.base); d.opacity = mm.base.a; d.metal = mm.metal; d.rough = mm.rough; d.emissive = mm.emissive; d.normalScale = mm.normalScale; d.aoStrength = mm.occlusion;
      auto tid = [](const Texture* t) { return t && t->ok ? t->id : 0u; };
      d.tex[0] = tid(mm.baseTex); d.tex[1] = d.tex[2] = tid(mm.mrTex); d.tex[3] = tid(mm.normalTex); d.tex[4] = tid(mm.occTex); d.tex[5] = tid(mm.emissiveTex);
      if (mm.alphaMode == 1) { d.cutoff = mm.cutoff; d.opacity = 1; } else if (mm.alphaMode == 2) d.blend = true; else d.opacity = 1;
      d.vcolor = pr.colors; }
    else { d.color = glm::vec3(0.8f); d.metal = 0; d.rough = 0.6f; d.vcolor = pr.colors; }
    d.linear = !over;   // glTF colours are linear light already
    if (colorOverride) { d.color = *colorOverride; d.linear = false; }
    f.draws.push_back(d); }
}

void addActorVisual(Frame& f, const Project& P, const Actor& a, const glm::mat4& W, bool editor, unsigned id, const glm::vec3* colorOverride, const std::string* textOverride, const Renderer* r, const AnimPlay* anim) {
  glm::vec3 pos(W[3]); glm::vec3 fwd = glm::normalize(glm::mat3(W) * glm::vec3(0, 0, -1) + glm::vec3(1e-7f));
  glm::mat4 at = glm::translate(glm::mat4(1), pos);                       // icon placed at the actor, no scale
  glm::mat4 rotOnly = at * glm::mat4(glm::mat3(glm::normalize(glm::vec3(W[0])), glm::normalize(glm::vec3(W[1])), glm::normalize(glm::vec3(W[2]))));
  auto icon = [&](const char* mesh, glm::vec3 c, float s) { DrawItem d; d.mesh = &primitive(mesh); d.model = glm::scale(at, glm::vec3(s)); d.color = c; d.unlit = true; d.castShadow = false; d.id = id; f.draws.push_back(d); };
  auto proxy = [&](const char* mesh, glm::mat4 m) { DrawItem d; d.mesh = &primitive(mesh); d.model = m; d.pickOnly = true; d.id = id; f.draws.push_back(d); };
  auto line = [&](const GpuMesh& m, glm::mat4 model, glm::vec3 c, bool top) { LineItem l; l.mesh = &m; l.model = model; l.color = glm::vec4(c, 0.95f); l.onTop = top; l.id = id; f.lines.push_back(l); };
  if (editor && a.type == "StaticMesh" && a.vehicle.enabled) {   // a car: its wheels where they will be
    glm::vec3 sc(glm::length(glm::vec3(W[0])), glm::length(glm::vec3(W[1])), glm::length(glm::vec3(W[2]))); std::vector<Physics::WheelDesc> wd; vehicleWheels(a, sc, wd);
    glm::mat4 B = W; for (int i = 0; i < 3; i++) B[i] = W[i] / std::max(sc[i], 1e-6f);
    for (auto& w : wd) addWheelDraw(f, P, a, B, glm::translate(glm::mat4(1), w.pos - glm::vec3(0, 0.05f + w.travel * 0.6f, 0)) * glm::rotate(glm::mat4(1), 1.5707963f, AZ), id); }
  if (a.type == "StaticMesh" && isModelFile(a.mesh)) addModelDraws(f, P, a, W, id, colorOverride, anim);
  else if (a.type == "StaticMesh") {
    DrawItem d; d.mesh = &primitive(a.mesh); d.model = W; d.id = id;
    auto it = P.materials.find(a.material); Material m = it != P.materials.end() ? it->second : Material();
    applyMaterial(d, m); if (colorOverride) d.color = *colorOverride;
    f.draws.push_back(d);
  } else if (a.isLight()) {
    LightItem l; l.type = a.type == "DirectionalLight" ? 0 : a.type == "PointLight" ? 1 : 2; l.pos = pos; l.dir = fwd; l.color = a.light.color; l.intensity = a.light.intensity;
    l.range = a.light.range; l.cosOuter = std::cos(glm::radians(glm::clamp(a.light.angle, 1.f, 89.f))); l.shadows = a.light.shadows; f.lights.push_back(l);
    if (editor) { icon("Sphere", a.type == "DirectionalLight" ? glm::vec3(1, 0.91f, 0.66f) : a.light.color, 0.3f);
      if (r && a.type != "PointLight") line(r->arrow(), rotOnly, glm::vec3(1, 0.85f, 0.4f), true); }
  } else if (a.type == "Landscape" && a.land) {
    DrawItem d; d.mesh = &landscapeGpu(*a.land); d.model = W; d.id = id; d.splat = true; d.rough = 0.9f;
    for (int k = 0; k < 4; k++) { auto it = P.materials.find(a.land->layer[k]);
      if (it == P.materials.end()) { static const glm::vec3 DEF[4] = {{0.32f, 0.5f, 0.22f}, {0.48f, 0.47f, 0.45f}, {0.45f, 0.34f, 0.24f}, {0.82f, 0.74f, 0.55f}}; d.layerCol[k] = DEF[k]; continue; }   // grass, rock, dirt, sand
      const Material& m = it->second; d.layerCol[k] = m.color; d.layerRough[k] = m.roughness; d.layerTile[k] = std::max(0.05f, a.land->tiling[k]);
      if (!m.baseMap.empty()) { const Texture* t = texture(m.baseMap, true); d.layerTex[k] = t ? t->id : 0; } }
    f.draws.push_back(d);
  } else if (a.type == "ReflectionCapture") {
    ProbeItem pr; pr.pos = pos; pr.radius = std::max(0.5f, a.probe.radius); pr.intensity = a.probe.intensity; pr.id = id; f.probes.push_back(pr);
    if (editor) { icon("Sphere", glm::vec3(0.75f, 0.85f, 1.f), 0.35f); }
  } else if (a.type == "TextRender") {
    TextItem t; t.text = textOverride ? *textOverride : a.text.value; t.model = W; t.color = colorOverride ? *colorOverride : a.text.color; t.size = a.text.size; t.id = id; f.texts.push_back(t);
  } else if (editor && r) {
    if (a.type == "PlayerStart") { glm::vec3 c(0.31f, 0.76f, 0.97f); line(r->wireCapsule(), rotOnly, c, true); line(r->arrow(), glm::scale(glm::translate(rotOnly, {0, 0.2f, 0}), glm::vec3(1.2f)), c, true); proxy("Capsule", rotOnly); }
    else if (a.type == "CameraActor") { line(r->wireCam(), rotOnly, glm::vec3(0.78f, 0.57f, 0.92f), true); proxy("Cube", glm::scale(rotOnly, glm::vec3(0.4f, 0.3f, 0.5f))); }
    else if (a.type == "TriggerBox") { line(r->wireBox(), W, glm::vec3(1, 0.62f, 0.25f), false); proxy("Sphere", glm::scale(at, glm::vec3(0.35f))); }
    else if (a.type == "PhysicsConstraint") { glm::vec3 c(1, 0.45f, 0.75f); icon("Sphere", c, 0.18f); line(r->axes(), glm::scale(rotOnly, glm::vec3(0.6f)), c, true);   // the hinge turns about the green (Y) axis, a slider moves along the red (X)
      DrawItem d; d.mesh = &primitive("Cylinder"); d.model = glm::scale(rotOnly, glm::vec3(0.04f, 1.2f, 0.04f)); d.color = c; d.unlit = true; d.castShadow = false; d.id = id; if (a.joint.kind == "Slider") d.model = glm::scale(rotOnly * glm::rotate(glm::mat4(1), 1.5707963f, AZ), glm::vec3(0.04f, 1.2f, 0.04f)); f.draws.push_back(d);
      proxy("Sphere", glm::scale(at, glm::vec3(0.3f))); }
    else { line(r->axes(), glm::scale(rotOnly, glm::vec3(0.5f)), glm::vec3(0.9f), true); proxy("Sphere", glm::scale(at, glm::vec3(0.3f))); }
  }
}

/* ------------------------------------------------------------------ game */
Game::Game(const Project& p, const std::string& ln) : levelName(ln), project_(p) {}
Game::~Game() { clear(); padClose(); }

void Game::clear() {
  if (audio_) audio_->stopAll();
  if (models_) { models_->setBlocks(nullptr); models_->clear(); }
  if (scripts_ && started_) for (int b = 0; b < scripts_->count(); b++) scripts_->call(b, EV_Stop);   // last chance to save
  if (net_) { net_->stop(); net_->lanSearch(false); net_->take(); }
  if (ui_) { ui_->setVoxels(nullptr); ui_->reset(); }
  if (scripts_) scripts_->shutdown();
  scripts_.reset();
  vox_.reset();   // before the physics world: chunk bodies live there
  if (phys_) phys_->shutdown();
  phys_.reset(); objs_.clear(); byId_.clear(); hud_.clear(); msg_.clear(); player_ = NO_BODY; started_ = false;
}
Game::Obj* Game::get(const std::string& id) const { auto it = byId_.find(id); return it != byId_.end() && it->second->alive ? it->second : nullptr; }
glm::mat4 Game::world(const Obj& o) const {
  glm::mat4 m = trsMatrix(o.p, o.r, o.s); const Obj* cur = &o;
  for (int guard = 0; guard < 64; guard++) {   // up the parents: actors, a bone of a model, the player, the camera
    const std::string& pid = cur->a.parent; if (pid.empty()) break;
    if (pid == "@player") return (cur->bone.empty() ? playerMatrix() : playerBone(cur->bone)) * m;
    if (pid == "@camera") return camW_ * m;
    Obj* p = get(pid); if (!p) break;
    if (!cur->bone.empty()) return boneWorld(*p, cur->bone) * m;
    m = trsMatrix(p->p, p->r, p->s) * m; cur = p; }
  return m;
}

bool Game::start() {
  if (!pad_) for (int i = 0; i < SDL_NumJoysticks(); i++) padOpen(i);
  clear();
  const Level* L = project_.level(levelName); if (!L) { logMsg("error: level \"" + levelName + "\" not found", LogKind::Error); return false; }
  setAssetRoot(project_.path.empty() ? std::string() : std::filesystem::u8path(project_.path).parent_path().u8string());
  level_ = *L; time_ = 0; restartRequested_ = false; tune_ = Tune(); sunSet_ = false; camOn_ = false; mouseSens_ = 1; grounded_ = running_ = sneaking_ = tapRun_ = false;
  for (auto& a : level_.actors) { auto o = std::make_unique<Obj>(); o->a = a; o->p = a.p; o->r = glm::radians(a.r); o->s = a.s; o->visible = a.visible; byId_[a.id] = o.get(); objs_.push_back(std::move(o)); }
  phys_.reset(new Physics()); phys_->init(level_.world.gravity); gravity_ = level_.world.gravity; swimming_ = false; nav_.clear(); navBuilt_ = false;
  // collision meshes attached to a moving one are part of its body (welded): a car with a cabin, a crate with a lid
  for (auto& op : objs_) { Obj& o = *op; if (o.a.type != "StaticMesh" || !(o.a.physics.simulate || o.a.vehicle.enabled)) continue;
    std::function<void(const std::string&)> weld = [&](const std::string& id) { for (auto& c : objs_) if (c->a.parent == id && c->a.type == "StaticMesh") { if (c->a.physics.collision && !c->a.physics.simulate) c->welded = true; weld(c->a.id); } }; weld(o.a.id); }
  for (size_t i = 0; i < objs_.size(); i++) { Obj& o = *objs_[i]; if (o.a.type == "StaticMesh" && (o.a.physics.collision || o.a.physics.simulate || o.a.vehicle.enabled) && !o.welded) addBody(o);
    if (o.a.type == "Landscape" && o.a.land) {   // its triangles where it is in the world
      std::vector<float> pos; std::vector<uint32_t> idx; landscapeMesh(*o.a.land, pos, idx); glm::mat4 W = world(o); std::vector<glm::vec3> v(pos.size() / 3);
      for (size_t k = 0; k < v.size(); k++) v[k] = glm::vec3(W * glm::vec4(pos[k * 3], pos[k * 3 + 1], pos[k * 3 + 2], 1));
      if (ShapePtr sh = buildTriangleShape(v, idx)) { o.body = phys_->addStatic(sh, glm::vec3(0), i + 1); o.motion = Motion::Static; o.lastWorld = W; } } }
  // player
  mode_ = level_.world.gameMode; if (mode_ != "ThirdPerson" && mode_ != "Spectator") mode_ = "FirstPerson";
  float yawDeg = 0; glm::vec3 ps = playerStart(&yawDeg); yaw_ = glm::radians(yawDeg); pitch_ = 0;
  if (mode_ == "Spectator") specPos_ = ps + glm::vec3(0, 1.6f, 0); else player_ = phys_->addPlayer(ps);
  { const std::string& pm = level_.world.playerModel; pModel_ = mode_ != "ThirdPerson" || pm == "Capsule" ? "" : pm.empty() ? "Characters/Mannequin.glb" : pm; pAnim_ = AnimPlay(); pAnimName_.clear(); charYaw_ = yaw_ + 3.14159265f; }
  // scripts
  frameNo_ = 0; camSet_ = false; startGameplay();
  if (!models_) models_.reset(new Models()); models_->clear(); models_->setBlocks(vox_.get());
  if (!ui_) ui_.reset(new Ui()); ui_->setConsole(console_); ui_->reset(); ui_->setAssets([this](const std::string& p) { return assetPath(p); });
  scripts_.reset(new ScriptHost()); scripts_->init(this);
  for (auto& op : objs_) { Obj& o = *op; if (o.a.script.empty()) continue;
    auto it = project_.scripts.find(o.a.script); if (it == project_.scripts.end()) { logMsg("warning: " + o.a.name + ": script \"" + o.a.script + "\" not found", LogKind::Warn); continue; }
    o.beh = scripts_->attach(o.a.id, o.a.name, it->second.name, it->second.code); }
  for (auto& op : objs_) { Obj& o = *op; if (o.a.type != "StaticMesh" || !isModelFile(o.a.mesh) || o.a.anim.empty()) continue;   // animations that play from the start
    ModelRef m = model(o.a.mesh); int ai = m ? m->anim(o.a.anim) : -1;
    if (ai >= 0) o.anim.play(ai, o.a.animLoop, o.a.animSpeed, 0); else logMsg("warning: " + o.a.name + ": the model has no animation \"" + o.a.anim + "\"", LogKind::Warn); }
  started_ = true;
  for (int b = 0; b < scripts_->count(); b++) scripts_->call(b, EV_Start);
  return true;
}

glm::vec3 Game::playerStart(float* yawDeg) const {
  for (auto& o : objs_) if (o->alive && o->a.type == "PlayerStart") { if (yawDeg) *yawDeg = o->a.r.y; return glm::vec3(world(*o)[3]); }
  if (yawDeg) *yawDeg = 0; return {0, 2, 5};
}

static bool scriptedUp(const Game* g, const Actor& a, const std::function<const Actor*(const std::string&)>& find) {
  (void)g; int guard = 0; for (const Actor* x = &a; x && guard < 64; x = x->parent.empty() ? nullptr : find(x->parent), guard++) if (!x->script.empty()) return true;
  return false;
}
void Game::addBody(Obj& o) {
  glm::vec3 pos, sc; glm::quat q; decompose(world(o), pos, q, sc);
  bool scripted = scriptedUp(this, o.a, [&](const std::string& id) -> const Actor* { Obj* x = get(id); return x ? &x->a : nullptr; });
  o.motion = o.a.physics.simulate || o.a.vehicle.enabled ? Motion::Dynamic : scripted ? Motion::Kinematic : Motion::Static;
  if (o.motion == Motion::Dynamic && !o.a.parent.empty()) { o.a.parent.clear(); o.bone.clear(); o.p = pos; o.r = eulerYXZ(q); o.s = sc; }   // simulated actors leave their parent
  size_t idx = 0; for (; idx < objs_.size(); idx++) if (objs_[idx].get() == &o) break;
  float mass = o.a.vehicle.enabled && o.a.physics.mass < 100 ? 1300.f : o.a.physics.mass;   // a car of 1 kg flips at once
  std::vector<Physics::Part> parts; glm::mat4 R0 = glm::translate(glm::mat4(1), pos) * glm::mat4_cast(q), IR0 = glm::inverse(R0);
  std::function<void(const std::string&)> welded = [&](const std::string& id) { for (auto& c : objs_) if (c->alive && c->welded && c->a.parent == id) {
      glm::vec3 cp, cs; glm::quat cq; decompose(IR0 * world(*c), cp, cq, cs); parts.push_back({c->a.mesh, cs, cp, cq}); welded(c->a.id); } };
  welded(o.a.id);
  if (!parts.empty()) { parts.insert(parts.begin(), Physics::Part{o.a.mesh, sc, glm::vec3(0), glm::quat(1, 0, 0, 0)}); o.body = phys_->addCompound(parts, pos, q, o.motion, mass, idx + 1); }
  else o.body = phys_->addMesh(o.a.mesh, sc, pos, q, o.motion, mass, idx + 1);
  o.lastWorld = world(o); o.bodyScale = sc;
}

static const char* padName(int b);
void Game::event(const SDL_Event& e) {
  if (!started_) return;
  // the game's menus first: what they use, the game doesn't see (key ups always go through, so nothing stays held)
  if (ui_ && e.type != SDL_KEYUP && ui_->event(e)) return;
  if (ui_ && ui_->modal() && (e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEWHEEL)) return;
  switch (e.type) {
    case SDL_KEYDOWN: { std::string k = keyCode(e.key.keysym.scancode);
      if (!keys_.count(k)) pressed_.insert(k); keys_.insert(k);
      if (!e.key.repeat) for (int b = 0; b < scripts_->count(); b++) scripts_->call(b, EV_Key, 0, k);
      break; }
    case SDL_KEYUP: keys_.erase(keyCode(e.key.keysym.scancode)); break;
    case SDL_MOUSEWHEEL: { float y = e.wheel.preciseY != 0 ? e.wheel.preciseY : (float)e.wheel.y; if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) y = -y;   // input.pressed('WheelUp' / 'WheelDown'), onKey too
      if (y != 0) { const char* k = y > 0 ? "WheelUp" : "WheelDown"; pressed_.insert(k); for (int b = 0; b < scripts_->count(); b++) scripts_->call(b, EV_Key, 0, k); } break; }
    case SDL_MOUSEMOTION: if (SDL_GetRelativeMouseMode() || mouseState_.w > 0) { lookDx_ += e.motion.xrel; lookDy_ += e.motion.yrel; } break;
    case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: {
      bool down = e.type == SDL_MOUSEBUTTONDOWN; const char* k = e.button.button == SDL_BUTTON_LEFT ? "MouseLeft" : e.button.button == SDL_BUTTON_RIGHT ? "MouseRight" : e.button.button == SDL_BUTTON_MIDDLE ? "MouseMiddle" : nullptr;
      if (e.button.button == SDL_BUTTON_LEFT) mouseState_.z = down; if (e.button.button == SDL_BUTTON_RIGHT) mouseState_.w = down;
      if (k) { if (down) { if (!keys_.count(k)) pressed_.insert(k); keys_.insert(k); } else keys_.erase(k); }
      break; }
    // the controller
    case SDL_CONTROLLERDEVICEADDED: padOpen(e.cdevice.which); break;
    case SDL_CONTROLLERDEVICEREMOVED: if (e.cdevice.which == padId_) { padClose(); for (int i = 0; i < SDL_NumJoysticks(); i++) padOpen(i); } break;
    case SDL_CONTROLLERBUTTONDOWN: case SDL_CONTROLLERBUTTONUP: if (e.cbutton.which == padId_ || padId_ < 0) if (const char* n = padName(e.cbutton.button)) padKey(n, e.type == SDL_CONTROLLERBUTTONDOWN); break;
    case SDL_CONTROLLERAXISMOTION: { if (e.caxis.which != padId_ && padId_ >= 0) break; int a = e.caxis.axis; float v = e.caxis.value / 32767.f;
      int i = a == SDL_CONTROLLER_AXIS_LEFTX ? 0 : a == SDL_CONTROLLER_AXIS_LEFTY ? 1 : a == SDL_CONTROLLER_AXIS_RIGHTX ? 2 : a == SDL_CONTROLLER_AXIS_RIGHTY ? 3 : a == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? 4 : 5;
      ax_[i] = std::clamp(v, -1.f, 1.f);
      if (i >= 4) { bool on = ax_[i] > 0.5f, &was = trig_[i - 4]; if (on != was) { was = on; padKey(i == 4 ? "PadLT" : "PadRT", on); } }
      if (i <= 1 && ui_) ui_->padStick(ax_[0], ax_[1]);
      break; }
    default: break;
  }
}
void Game::releaseInput() { keys_.clear(); pressed_.clear(); mouseState_ = glm::vec4(0); padRun_ = false; }

/* ---- the controller */
void Game::keyDown(const std::string& k, bool fire) { if (!keys_.count(k)) pressed_.insert(k); keys_.insert(k); if (fire) for (int b = 0; b < scripts_->count(); b++) scripts_->call(b, EV_Key, 0, k); }
void Game::padOpen(int index) {
  if (pad_ || !SDL_IsGameController(index)) return; pad_ = SDL_GameControllerOpen(index); if (!pad_) return;
  padId_ = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad_)); const char* n = SDL_GameControllerName(pad_); logMsg(std::string("Controller: ") + (n ? n : "connected"));
}
void Game::padClose() { if (pad_) SDL_GameControllerClose(pad_); pad_ = nullptr; padId_ = -1; for (float& a : ax_) a = 0; trig_[0] = trig_[1] = false;
  for (auto it = keys_.begin(); it != keys_.end();) { if (it->rfind("Pad", 0) == 0) it = keys_.erase(it); else ++it; } padRun_ = false; }
static const char* padName(int b) {
  switch (b) { case SDL_CONTROLLER_BUTTON_A: return "PadA"; case SDL_CONTROLLER_BUTTON_B: return "PadB"; case SDL_CONTROLLER_BUTTON_X: return "PadX"; case SDL_CONTROLLER_BUTTON_Y: return "PadY";
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return "PadLB"; case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return "PadRB"; case SDL_CONTROLLER_BUTTON_START: return "PadStart"; case SDL_CONTROLLER_BUTTON_BACK: return "PadBack";
    case SDL_CONTROLLER_BUTTON_LEFTSTICK: return "PadLS"; case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return "PadRS"; case SDL_CONTROLLER_BUTTON_DPAD_UP: return "PadUp"; case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return "PadDown";
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return "PadLeft"; case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return "PadRight"; default: return nullptr; } }
void Game::padKey(const std::string& name, bool down) {
  // the game's menus first (moving between buttons, A, B, LB / RB for tabs ...)
  if (ui_ && ui_->pad(name, down)) return;
  bool menu = ui_ && ui_->modal();
  if (down) { keyDown(name, true); if (name == "PadLS") padRun_ = true; } else keys_.erase(name);
  if (!padAliases_) return;
  std::string al = name == "PadA" ? "Space" : name == "PadRT" ? "MouseLeft" : name == "PadLT" ? "MouseRight" : name == "PadStart" ? "Escape" : (menu && name == "PadB") ? "Escape" : "";
  if (name == "PadRT") mouseState_.z = down; if (name == "PadLT") mouseState_.w = down;
  if (!al.empty()) { if (down) keyDown(al, true); else keys_.erase(al); }
  if (down && (name == "PadLB" || name == "PadRB")) { const char* w = name == "PadLB" ? "WheelUp" : "WheelDown"; pressed_.insert(w); for (int b = 0; b < scripts_->count(); b++) scripts_->call(b, EV_Key, 0, w); }
}
float Game::axis(const std::string& n) {
  static const char* N[6] = {"LeftX", "LeftY", "RightX", "RightY", "LT", "RT"}; for (int i = 0; i < 6; i++) if (n == N[i]) return ax_[i]; return 0; }
void Game::padSet(const std::string& js) { json o = json::parse(js, nullptr, false); if (!o.is_object()) return;
  if (o.contains("aliases") && o["aliases"].is_boolean()) padAliases_ = o["aliases"].get<bool>();
  if (o.contains("invertY") && o["invertY"].is_boolean()) invertY_ = o["invertY"].get<bool>();
  if (o.contains("lookSpeed") && o["lookSpeed"].is_number()) lookSpeed_ = std::clamp(o["lookSpeed"].get<float>(), 0.2f, 12.f); }
void Game::rumble(float lo, float hi, float sec) { if (pad_) SDL_GameControllerRumble(pad_, (Uint16)(std::clamp(lo, 0.f, 1.f) * 65535), (Uint16)(std::clamp(hi, 0.f, 1.f) * 65535), (Uint32)(std::clamp(sec, 0.f, 5.f) * 1000)); }

void Game::update(float dt) {
  if (!started_) return;
  dt = std::min(dt, 0.05f);
  netPump();
  // the game's menus: animations, then what the player did in them goes to the scripts' handlers
  if (ui_) { ui_->update(dt); auto ev = ui_->takeEvents();
    if (!ev.empty()) { json a = json::array(); for (auto& e : ev) a.push_back({{"id", e.id}, {"type", e.type}, {"data", e.data}, {"path", e.path}}); scripts_->uiDispatch(a.dump()); scripts_->runJobs(); }
    bool m = ui_->modal(); if (m && !wasModal_) releaseInput(); wasModal_ = m;   // a menu opened: stop walking
    if (ui_->pausing()) { soundFrame(); if (vox_) vox_->update(playerPos()); pressed_.clear(); lookDx_ = lookDy_ = 0; if (restartRequested_) { logMsg("Restarting the level"); start(); } return; }   // the game stands still under the menu
  }
  time_ += dt;
  if (vox_) { vox_->update(playerPos()); for (auto& l : vox_->takeLogs()) logMsg(l, l.rfind("error", 0) == 0 ? LogKind::Error : l.rfind("warning", 0) == 0 ? LogKind::Warn : LogKind::Info); }
  // look
  if (ui_ && ui_->modal()) lookDx_ = lookDy_ = 0;
  const float sens = 0.0022f * mouseSens_; mouseState_.x = lookDx_; mouseState_.y = lookDy_;
  yaw_ -= lookDx_ * sens; pitch_ = glm::clamp(pitch_ - lookDy_ * sens, -1.45f, 1.45f); lookDx_ = lookDy_ = 0;
  glm::vec3 fwd(-std::sin(yaw_), 0, -std::cos(yaw_)), right(-fwd.z, 0, fwd.x), mv(0);
  auto k = [&](const char* c) { return keys_.count(c) > 0; };
  if (k("KeyW") || k("ArrowUp")) mv += fwd; if (k("KeyS") || k("ArrowDown")) mv -= fwd; if (k("KeyD") || k("ArrowRight")) mv += right; if (k("KeyA") || k("ArrowLeft")) mv -= right;
  if (glm::dot(mv, mv) > 0) mv = glm::normalize(mv);
  if (dead_ || !driving_.empty()) mv = glm::vec3(0);   // dead, or the keys drive a car
  // the controller: left stick moves (gently when pushed a little), right stick looks (faster the further it is pushed)
  if (!(ui_ && ui_->modal())) {
    auto dz = [](float x, float y, float d) { glm::vec2 v(x, y); float l = glm::length(v); if (l < d) return glm::vec2(0); return v / l * std::min(1.f, (l - d) / (1 - d)); };
    glm::vec2 ls = dz(ax_[0], ax_[1], 0.18f), rs = dz(ax_[2], ax_[3], 0.14f);
    if (glm::dot(mv, mv) == 0 && glm::dot(ls, ls) > 0 && !dead_ && driving_.empty()) mv = fwd * -ls.y + right * ls.x;
    if (glm::length(ls) < 0.3f) padRun_ = false;
    float rl = glm::length(rs); if (rl > 0) { glm::vec2 c = rs * rl;   // response curve: precise aim near the centre
      yaw_ -= c.x * lookSpeed_ * dt; pitch_ = glm::clamp(pitch_ - c.y * lookSpeed_ * 0.75f * dt * (invertY_ ? -1.f : 1.f), -1.45f, 1.45f); }
  }
  auto held = [&](const std::string& n) { return !n.empty() && (k((n + "Left").c_str()) || k((n + "Right").c_str()) || k(n.c_str())); };
  { bool w = k("KeyW"); if (tune_.doubleTapRun) { if (w && !wHeld_) { if (time_ - wTap_ < 0.3) tapRun_ = true; wTap_ = time_; } if (!w) tapRun_ = false; } wHeld_ = w; }
  bool padSneak = pad_ && k("PadB") && !(ui_ && ui_->modal());
  sneaking_ = !tune_.fly && (held(tune_.sneakKey) || padSneak); running_ = glm::dot(mv, mv) > 0 && !sneaking_ && (held(tune_.runKey) || tapRun_ || padRun_);
  float speed = tune_.fly ? tune_.flySpeed * (running_ ? tune_.flyRun : 1.f) : sneaking_ ? tune_.sneak : running_ ? tune_.run : tune_.walk; moveDir_ = mv;
  if (mode_ == "Spectator") { if (k("KeyE")) mv.y += 1; if (k("KeyQ")) mv.y -= 1; specPos_ += mv * speed * 1.6f * dt; }
  else if (player_ != NO_BODY && driving_.empty() && tune_.locked) { phys_->setVelocity(player_, glm::vec3(0)); grounded_ = true; swimming_ = false; running_ = sneaking_ = false; }   // riding a script vehicle: the scripts move the player
  else if (player_ != NO_BODY && driving_.empty()) {
    glm::vec3 c = phys_->position(player_), v = phys_->velocity(player_);
    if (tune_.accel > 0) { float a = tune_.accel * dt; v.x += glm::clamp(mv.x * speed - v.x, -a, a); v.z += glm::clamp(mv.z * speed - v.z, -a, a); } else { v.x = mv.x * speed; v.z = mv.z * speed; }
    glm::vec3 feet = c - glm::vec3(0, 0.9f, 0); bool grounded = false;
    const glm::vec2 offs[5] = {{0, 0}, {0.25f, 0}, {-0.25f, 0}, {0, 0.25f}, {0, -0.25f}};
    for (auto& o : offs) if (phys_->rayHit(feet + glm::vec3(o.x, 0.1f, o.y), feet + glm::vec3(o.x, -0.15f, o.y), player_)) { grounded = true; break; }
    // block world liquids: swim (slower, Space goes up, otherwise sink slowly; climb out at the edge)
    grounded_ = grounded; swimming_ = false;
    if (tune_.fly) { v.y = ((k("Space") || k("PadA") ? 1.f : 0.f) - (held(tune_.sneakKey.empty() ? "Shift" : tune_.sneakKey) || padSneak ? 1.f : 0.f)) * tune_.flyClimb - gravity_ * dt; }   // flying: Space up, sneak key down, no falling
    else if (tune_.gravity != 0) v.y += (tune_.gravity - gravity_) * dt;   // the player's own gravity
    if (!tune_.fly && vox_) { bool body = vox_->liquidAt(feet + glm::vec3(0, 0.45f, 0)) || vox_->liquidAt(c), head = vox_->liquidAt(c + glm::vec3(0, 0.7f, 0));
      if (body) { swimming_ = true; v.x *= 0.55f; v.z *= 0.55f;
        float target = k("Space") || k("PadA") ? (head ? 2.4f : 3.4f) : head ? -0.7f : -0.25f;
        v.y = v.y + (target - v.y) * std::min(1.f, dt * 5.f) - gravity_ * dt; } }
    if (!tune_.fly && grounded && !swimming_ && !sneaking_ && !dead_ && (pressed_.count("Space") || pressed_.count("PadA"))) v.y = tune_.jump;
    // block world: walk up 20 - 40 cm steps without jumping
    if (!tune_.fly && vox_ && grounded && glm::dot(mv, mv) > 0 && v.y <= 0.5f) {
      glm::vec3 ahead = feet + mv * 0.5f; auto solid = [&](float h) { return vox_->solidAt(ahead + glm::vec3(0, h, 0)); };
      if (solid(0.1f) && !solid(0.5f) && !solid(1.1f) && !solid(1.7f)) v.y = solid(0.3f) ? 3.6f : 2.6f;
    }
    // block world: hold the player until the ground under them is built
    if (vox_) { bool ok = vox_->ready(c, 1.5f);
      if (!ok) { if (!voxHold_) { voxHold_ = true; voxHoldPos_ = c; } phys_->teleport(player_, voxHoldPos_, glm::quat(1, 0, 0, 0)); v = glm::vec3(0); }
      else voxHold_ = false; }
    phys_->setVelocity(player_, v);
  }
  // physics
  gameplayFrame(dt, false);
  phys_->step(dt);
  gameplayFrame(dt, true);
  for (auto& [ua, ub] : phys_->takeContacts()) {
    auto side = [&](uint64_t self, uint64_t other) {
      if (self == PLAYER_TAG || self == 0 || (self >> 41) & 1 || self > objs_.size()) return; Obj& o = *objs_[self - 1]; if (!o.alive || o.beh < 0) return;   // a ragdoll's parts don't call onHit
      if (other != PLAYER_TAG) other &= 0xffffffffull;
      std::string oth = other == PLAYER_TAG ? "@player" : (other > 0 && other <= objs_.size() && objs_[other - 1]->alive) ? objs_[other - 1]->a.id : "";
      scripts_->call(o.beh, EV_Hit, 0, oth); };
    side(ua, ub); side(ub, ua);
  }
  for (auto& op : objs_) { Obj& o = *op; if (!o.alive || o.body == NO_BODY || o.motion != Motion::Dynamic) continue; glm::quat q; phys_->transform(o.body, o.p, q); o.r = eulerYXZ(q); o.lastWorld = world(o); }
  // scripts
  for (int b = 0; b < scripts_->count(); b++) scripts_->call(b, EV_Update, dt);
  // fell out of the world?
  if (player_ != NO_BODY && driving_.empty() && phys_->position(player_).y - 0.9f < level_.world.killZ) { logMsg("Player fell below Kill Z: respawn"); respawnPlayer(); }
  // the player walking into actors
  glm::vec3 pp = playerPos(), ph(0.35f, 0.9f, 0.35f);
  for (size_t i = 0; i < objs_.size(); i++) { Obj& o = *objs_[i]; if (!o.alive || !scripts_->has(o.beh, EV_Overlap)) continue;   // scripts may spawn actors: index loop
    glm::mat4 W = world(o); glm::vec3 mn, mx;
    if (o.a.type == "TriggerBox") { glm::vec3 c(W[3]), h = 0.5f * glm::vec3(glm::length(glm::vec3(W[0])), glm::length(glm::vec3(W[1])), glm::length(glm::vec3(W[2]))); mn = c - h; mx = c + h; }
    else if (o.a.type == "StaticMesh") { glm::vec3 a, b; meshBounds(o.a.mesh, a, b); mn = glm::vec3(1e9f); mx = glm::vec3(-1e9f);
      for (int i = 0; i < 8; i++) { glm::vec3 c((i & 1) ? b.x : a.x, (i & 2) ? b.y : a.y, (i & 4) ? b.z : a.z), w = glm::vec3(W * glm::vec4(c, 1)); mn = glm::min(mn, w); mx = glm::max(mx, w); }
      mn -= 0.05f; mx += 0.05f; }
    else continue;
    bool in = mn.x <= pp.x + ph.x && mx.x >= pp.x - ph.x && mn.y <= pp.y + ph.y && mx.y >= pp.y - ph.y && mn.z <= pp.z + ph.z && mx.z >= pp.z - ph.z;
    if (in && !o.inside) { o.inside = true; scripts_->call(o.beh, EV_Overlap, 0, "@player"); } else o.inside = in;
  }
  // bodies of actors moved by scripts follow them
  for (auto& op : objs_) { Obj& o = *op; if (!o.alive || o.body == NO_BODY || o.motion == Motion::Dynamic || !o.rag.empty()) continue;
    glm::mat4 W = world(o); if (W == o.lastWorld) continue; glm::vec3 pos, sc; glm::quat q; decompose(W, pos, q, sc);
    if (glm::any(glm::greaterThan(glm::abs(sc - o.bodyScale), glm::vec3(1e-4f)))) { phys_->setShape(o.body, o.a.mesh, sc); o.bodyScale = sc; }
    if (o.motion == Motion::Kinematic) phys_->moveKinematic(o.body, pos, q); else phys_->teleport(o.body, pos, q);
    o.lastWorld = W; }
  walkFrame(dt); playerAnimFrame(dt);
  animFrame(dt);   // models' animations move on (a script hears when one that doesn't loop reaches its end), feet on the ground
  soundFrame();
  if (models_) models_->update(dt);   // particles
  pressed_.clear(); scripts_->runJobs();
  { glm::vec3 cp; glm::mat4 cv; cameraCompute(cp, cv); camW_ = glm::inverse(cv); camSet_ = true; }   // actors on the camera follow it
  frameNo_++;
  if (restartRequested_) { logMsg("Restarting the level"); start(); }
}

void Game::respawnPlayer() { glm::vec3 p = playerStart(nullptr); if (!driving_.empty()) exitVehicle(); if (player_ != NO_BODY) { phys_->teleport(player_, p, glm::quat(1, 0, 0, 0)); phys_->setVelocity(player_, glm::vec3(0)); } else specPos_ = p; }

void Game::buildFrame(Frame& f, Camera& cam, float aspect) const {
  f = Frame(); f.world = level_.world; f.fog = level_.world.fog; f.probeVersion = 1;
  { glm::vec3 cp; glm::mat4 cv; cameraCompute(cp, cv); camW_ = glm::inverse(cv); camSet_ = true; }   // first: actors on the camera are drawn where it is now
  for (auto& op : objs_) { const Obj& o = *op; if (!o.alive) continue;
    bool vis = o.visible; int guard = 0; for (Obj* p = get(o.a.parent); p && vis && guard < 64; p = get(p->a.parent), guard++) vis = p->visible;
    if (!vis) continue;
    addActorVisual(f, project_, o.a, world(o), false, 0, o.hasColor ? &o.color : nullptr, nullptr, nullptr, o.anim.anim >= 0 || !o.anim.fixed.empty() || !o.anim.scaled.empty() ? &o.anim : nullptr); }
  if (sunSet_) { bool done = false; for (auto& l : f.lights) if (l.type == 0) { l.dir = sunDir_; l.color = sunColor_; l.intensity = sunInt_; done = true; break; }
    if (!done) { LightItem l; l.type = 0; l.dir = sunDir_; l.color = sunColor_; l.intensity = sunInt_; l.shadows = true; f.lights.insert(f.lights.begin(), l); } }
  glm::vec3 eye;
  if (mode_ == "Spectator") eye = specPos_; else eye = phys_->position(player_) + glm::vec3(0, 0.7f, 0);
  if (mode_ == "ThirdPerson" && driving_.empty()) {
    glm::vec3 c = phys_->position(player_); ModelRef pm = pModel_.empty() ? nullptr : model(pModel_);
    if (pm && pm->ok()) {   // the character: its feet on the capsule's bottom, a model far from 1.8 m tall is fitted to it
      Actor ch = newActor("StaticMesh", "Player"); ch.id = "@player"; ch.mesh = pModel_; ch.material.clear(); float hgt = pm->shape.bmax.y - pm->shape.bmin.y, k = hgt > 1.0f && hgt < 2.6f ? 1.f : 1.8f / std::max(hgt, 1e-3f);
      glm::mat4 W = glm::scale(glm::rotate(glm::translate(glm::mat4(1), c - glm::vec3(0, 0.9f, 0)), charYaw_, AY), glm::vec3(k)) * glm::translate(glm::mat4(1), glm::vec3(0, -pm->shape.bmin.y, 0));
      addActorVisual(f, project_, ch, W, false, 0, nullptr, nullptr, nullptr, pAnim_.anim >= 0 ? &pAnim_ : nullptr); }
    else { DrawItem d; d.mesh = &primitive("Capsule"); float face = glm::dot(moveDir_, moveDir_) > 0 ? std::atan2(-moveDir_.x, -moveDir_.z) : yaw_;
      d.model = glm::rotate(glm::translate(glm::mat4(1), c), face, AY); d.color = glm::vec3(0.227f, 0.482f, 1.f); d.rough = 0.5f; f.draws.push_back(d); }
  }
  cameraCompute(cam.pos, cam.view);
  drawGameplay(f);
  cam.proj = glm::perspective(glm::radians(glm::clamp(level_.world.fov, 20.f, 150.f)), std::max(aspect, 0.01f), 0.05f, 2000.f);
  { glm::vec3 fw = -glm::vec3(glm::inverse(cam.view)[2]); fw.y = 0; f.shadowCenter = eye + (glm::length(fw) > 1e-3f ? glm::normalize(fw) * 25.f : glm::vec3(0)); }   // the shadow box covers what is in front of the camera
  if (models_) models_->draw(f);
  if (vox_) { vox_->draw(f, cam);
    if (int lq = vox_->liquidAt(cam.pos)) { const BlockType& bt = vox_->block(lq); f.fog = true; f.world.fogColor = bt.color[0] * 0.5f; f.world.fogDensity = 0.09f; f.world.sky = f.world.fogColor; } }   // under water
  f.time = (float)time_;
}

void Game::drawHud(ImDrawList* dl, float x, float y, float w, float h) const {
  ImGuiIO& io = ImGui::GetIO(); ImFont* big = io.Fonts->Fonts.Size > 1 ? io.Fonts->Fonts[1] : io.Fonts->Fonts[0]; ImFont* ui = io.Fonts->Fonts[0];
  float yy = y + 14;
  // Arabic is joined and drawn right to left (visualText); HUD lines with Arabic sit on the right
  for (auto& [id, t0] : hud_) { std::string t = visualText(t0); ImVec2 s = ui->CalcTextSizeA(20, 1e9f, 0, t.c_str()); float bx = hasRtl(t0) ? x + w - 14 - s.x - 20 : x + 14;
    dl->AddRectFilled({bx, yy}, {bx + s.x + 20, yy + s.y + 12}, IM_COL32(0, 0, 0, 130), 8); dl->AddText(ui, 20, {bx + 10, yy + 6}, IM_COL32_WHITE, t.c_str()); yy += s.y + 18; }
  if (!msg_.empty() && time_ < msgUntil_) {
    float a = (float)std::min(1.0, (msgUntil_ - time_) / 0.3), maxW = w * 0.9f;
    // wrap in reading order, then turn each line for drawing
    std::vector<std::string> lines; { std::string cur, word; auto width = [&](const std::string& s) { std::string v = visualText(s); return big->CalcTextSizeA(34, 1e9f, 0, v.c_str()).x; };
      auto flushWord = [&] { if (word.empty()) return; std::string t = cur.empty() ? word : cur + " " + word; if (!cur.empty() && width(t) > maxW) { lines.push_back(cur); cur = word; } else cur = t; word.clear(); };
      for (char ch : msg_) { if (ch == '\n') { flushWord(); lines.push_back(cur); cur.clear(); } else if (ch == ' ') flushWord(); else word += ch; }
      flushWord(); lines.push_back(cur); }
    float lh = big->FontSize * 34.f / big->FontSize * 1.15f, tw = 0; std::vector<std::string> vis; for (auto& l : lines) { vis.push_back(visualText(l)); tw = std::max(tw, big->CalcTextSizeA(34, 1e9f, 0, vis.back().c_str()).x); }
    float th = lh * vis.size(); ImVec2 p(x + (w - tw) / 2, y + h * 0.16f);
    dl->AddRectFilled({p.x - 22, p.y - 12}, {p.x + tw + 22, p.y + th + 12}, IM_COL32(0, 0, 0, (int)(150 * a)), 12);
    for (size_t i = 0; i < vis.size(); i++) { float lw = big->CalcTextSizeA(34, 1e9f, 0, vis[i].c_str()).x; dl->AddText(big, 34, {x + (w - lw) / 2, p.y + lh * i}, IM_COL32(255, 255, 255, (int)(255 * a)), vis[i].c_str()); }
  }
  if (loading()) { const char* t = "Loading world..."; ImVec2 s = big->CalcTextSizeA(30, 1e9f, 0, t); ImVec2 p(x + (w - s.x) / 2, y + h * 0.45f);
    dl->AddRectFilled({p.x - 24, p.y - 14}, {p.x + s.x + 24, p.y + s.y + 14}, IM_COL32(0, 0, 0, 150), 12); dl->AddText(big, 30, p, IM_COL32_WHITE, t); }
  if (!hint_.empty() && !(ui_ && ui_->modal())) { ImVec2 s = ui->CalcTextSizeA(22, 1e9f, 0, hint_.c_str()); ImVec2 p(x + (w - s.x) / 2, y + h * 0.82f);
    dl->AddRectFilled({p.x - 14, p.y - 8}, {p.x + s.x + 14, p.y + s.y + 8}, IM_COL32(0, 0, 0, 140), 8); dl->AddText(ui, 22, p, IM_COL32_WHITE, hint_.c_str()); }
  if (mode_ == "FirstPerson" && driving_.empty() && !(ui_ && ui_->modal())) { ImVec2 c(x + w / 2, y + h / 2); dl->AddCircleFilled(c, 3.5f, IM_COL32(0, 0, 0, 120)); dl->AddCircleFilled(c, 2.2f, IM_COL32(255, 255, 255, 230)); }
  if (ui_) ui_->draw(dl, x, y, w, h);   // the game's own UI on top
}

int Game::actorCount() const { int n = 0; for (auto& o : objs_) n += o->alive; return n; }

/* ------------------------------------------------------------------ ScriptWorld */
bool Game::exists(const std::string& id) { return get(id) != nullptr; }
glm::vec3 Game::getVec(const std::string& id, char f) {
  Obj* o = get(id); if (!o) return glm::vec3(0);
  switch (f) { case 'r': return o->r; case 's': return o->s; case 'v': return o->body != NO_BODY && o->motion == Motion::Dynamic ? phys_->velocity(o->body) : glm::vec3(0); default: return o->p; }
}
void Game::setVec(const std::string& id, char f, const glm::vec3& v) {
  Obj* o = get(id); if (!o || !std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) return;
  bool dyn = o->body != NO_BODY && o->motion == Motion::Dynamic;
  switch (f) {
    case 'v': if (dyn) phys_->setVelocity(o->body, v); return;
    case 'r': o->r = v; break;
    case 's': o->s = v; if (dyn) { glm::vec3 a = glm::abs(v); phys_->setShape(o->body, o->a.mesh, a); o->bodyScale = a; } return;
    default: o->p = v; break;
  }
  if (dyn) { phys_->teleport(o->body, o->p, quatYXZ(o->r)); o->lastWorld = world(*o); }
}
bool Game::getVisible(const std::string& id) { Obj* o = get(id); return o && o->visible; }
void Game::setVisible(const std::string& id, bool v) { if (Obj* o = get(id)) o->visible = v; }
void Game::setColor(const std::string& id, const std::string& hex) {
  Obj* o = get(id); if (!o) return; std::string h = hex;
  if (h.size() == 4 && h[0] == '#') h = std::string("#") + h[1] + h[1] + h[2] + h[2] + h[3] + h[3];
  if (h.size() != 7 || h[0] != '#') { logMsg("warning: setColor wants a colour like '#ff8800', got '" + hex + "'", LogKind::Warn); return; }
  o->color = hexColor(h, o->color); o->hasColor = true;
}
void Game::setText(const std::string& id, const std::string& t) { if (Obj* o = get(id)) if (o->a.type == "TextRender") o->a.text.value = t; }
void Game::addForce(const std::string& id, const glm::vec3& f) { Obj* o = get(id); if (o && o->body != NO_BODY && o->motion == Motion::Dynamic) phys_->addImpulse(o->body, f); }
void Game::lookAt(const std::string& id, const glm::vec3& t) {   // like three.js Object3D.lookAt: +Z points at the target
  Obj* o = get(id); if (!o) return; glm::vec3 d = t - glm::vec3(world(*o)[3]); if (glm::dot(d, d) < 1e-10f) return; d = glm::normalize(d);
  o->r = {-std::asin(glm::clamp(d.y, -1.f, 1.f)), std::atan2(d.x, d.z), 0};
  if (o->body != NO_BODY && o->motion == Motion::Dynamic) phys_->teleport(o->body, o->p, quatYXZ(o->r));
}
glm::vec3 Game::worldPos(const std::string& id) { Obj* o = get(id); return o ? glm::vec3(world(*o)[3]) : glm::vec3(0); }
void Game::destroyRec(const std::string& id) {
  Obj* o = get(id); if (!o) return;
  if (driving_ == id) exitVehicle();
  o->alive = false;
  if (o->vehicle != NO_BODY) { phys_->removeVehicle(o->vehicle); o->vehicle = NO_BODY; }
  for (auto& r : o->rag) phys_->remove(r.body); o->rag.clear();
  if (o->joint != NO_BODY) { phys_->removeJoint(o->joint); jointOwner_.erase(o->joint); o->joint = NO_BODY; }
  for (auto it = grabs_.begin(); it != grabs_.end();) { if (objOfBody(it->second.body) == o) it = grabs_.erase(it); else ++it; }
  if (o->body != NO_BODY) { phys_->remove(o->body); o->body = NO_BODY; }
  if (o->welded) { o->welded = false; Obj* root = get(o->a.parent); while (root && !root->a.parent.empty() && get(root->a.parent)) root = get(root->a.parent); if (root) { o->a.parent.clear(); rebuildBody(*root); } }
  if (o->beh >= 0) scripts_->drop(o->beh);
  for (auto& c : objs_) if (c->alive && c->a.parent == id) destroyRec(c->a.id);
}
void Game::destroyActor(const std::string& id) { destroyRec(id); }
std::string Game::actorInfo(const std::string& id) { Obj* o = get(id); json j = {{"name", o ? o->a.name : ""}, {"type", o ? o->a.type : ""}, {"tags", o ? o->a.tags : std::vector<std::string>()}}; return j.dump(); }
std::string Game::find(const std::string& name) { for (auto& o : objs_) if (o->alive && o->a.name == name) return o->a.id; return ""; }
std::vector<std::string> Game::findAll(const std::string& tag) {
  std::vector<std::string> r; for (auto& o : objs_) if (o->alive && (o->a.name == tag || std::find(o->a.tags.begin(), o->a.tags.end(), tag) != o->a.tags.end())) r.push_back(o->a.id); return r;
}
static glm::vec3 jv(const json& j, glm::vec3 d) {
  try { if (j.is_array() && j.size() >= 3) return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
    if (j.is_object()) return {j.value("x", d.x), j.value("y", d.y), j.value("z", d.z)}; } catch (...) {}
  return d;
}
std::string Game::spawn(const std::string& mesh, const std::string& opts) {
  json o = json::object(); try { o = json::parse(opts); } catch (...) {}
  bool known = isModelFile(mesh); for (int i = 0; i < MESH_COUNT; i++) known |= mesh == MESHES[i];
  Actor a = newActor("StaticMesh", o.value("name", mesh)); a.mesh = known ? mesh : "Cube"; if (isModelFile(mesh)) a.material.clear();
  if (o.contains("animation") && o["animation"].is_string()) a.anim = o["animation"];
  a.p = jv(o.value("position", json()), {0, 1, 0}); a.s = jv(o.value("scale", json()), glm::vec3(1)); glm::vec3 rot = jv(o.value("rotation", json()), glm::vec3(0));
  if (o.contains("material") && o["material"].is_string()) a.material = o["material"];
  a.physics.simulate = o.value("physics", false); if (o.contains("tags") && o["tags"].is_array()) for (auto& t : o["tags"]) if (t.is_string()) a.tags.push_back(t);
  auto ob = std::make_unique<Obj>(); ob->a = a; ob->p = a.p; ob->r = rot; ob->s = a.s; Obj& ref = *ob; byId_[a.id] = ob.get(); objs_.push_back(std::move(ob));
  if (o.contains("color") && o["color"].is_string()) setColor(a.id, o["color"]);
  addBody(ref);
  if (!a.anim.empty()) { ModelRef m = model(a.mesh); int ai = m ? m->anim(a.anim) : -1; if (ai >= 0) ref.anim.play(ai, true, 1, 0); }
  if (o.contains("vehicle") && (o["vehicle"].is_object() || (o["vehicle"].is_boolean() && o["vehicle"].get<bool>()))) gameplay("vehicleMake", a.id, o["vehicle"].is_object() ? o["vehicle"].dump() : "{}");
  if (o.contains("parent") && o["parent"].is_string()) { json at = {{"to", o["parent"]}, {"keepWorld", true}}; if (o.contains("bone")) at["bone"] = o["bone"]; gameplay("attach", a.id, at.dump()); }
  if (o.contains("script") && o["script"].is_string()) { auto it = project_.scripts.find(o["script"]); if (it != project_.scripts.end()) { ref.a.script = it->first; ref.beh = scripts_->attach(a.id, a.name, it->second.name, it->second.code); scripts_->call(ref.beh, EV_Start); } }
  return a.id;
}
/* ------------------------------------------------------------------ walking (AI Move To) */
// the level's floors, made the first time something walks or asks for a way (levels without AI pay nothing)
void Game::ensureNav() {
  if (navBuilt_ || !phys_) return; navBuilt_ = true;
  glm::vec3 mn(1e9f), mx(-1e9f); int n = 0;
  for (auto& op : objs_) { const Obj& o = *op; if (!o.alive || o.body == NO_BODY || o.motion != Motion::Static) continue;
    glm::vec3 bmn, bmx; if (o.a.type == "Landscape" && o.a.land) { float hs = o.a.land->size * 0.5f; auto mm = std::minmax_element(o.a.land->h.begin(), o.a.land->h.end()); bmn = {-hs, *mm.first, -hs}; bmx = {hs, *mm.second, hs}; }
    else { const MeshData& md = meshShape(o.a.mesh); bmn = md.bmin; bmx = md.bmax; } glm::mat4 W = world(o);
    for (int k = 0; k < 8; k++) { glm::vec3 c((k & 1) ? bmx.x : bmn.x, (k & 2) ? bmx.y : bmn.y, (k & 4) ? bmx.z : bmn.z), w = glm::vec3(W * glm::vec4(c, 1)); mn = glm::min(mn, w); mx = glm::max(mx, w); } n++; }
  if (vox_) { glm::vec3 p = playerPos(); mn = glm::min(mn, p - glm::vec3(48, 40, 48)); mx = glm::max(mx, p + glm::vec3(48, 40, 48)); n++; }   // a block world: around the player
  if (!n) { logMsg("warning: navigation: the level has no still meshes with collision to walk on", LogKind::Warn); return; }
  mn = glm::max(mn, glm::vec3(-2000)); mx = glm::min(mx, glm::vec3(2000)); phys_->optimize();
  nav_.build(*phys_, mn, mx, level_.world.nav);
  char b[160]; std::snprintf(b, sizeof b, "Navigation: %d walkable cells of %.2f m (%.0f ms)", nav_.walkable(), nav_.cell(), nav_.buildMs()); logMsg(b);
}
void Game::playerAnimFrame(float dt) {
  if (pModel_.empty() || player_ == NO_BODY || !driving_.empty()) return; ModelRef m = model(pModel_); if (!m || !m->ok() || m->anims.empty()) return;
  glm::vec3 v = phys_->velocity(player_); float hs = glm::length(glm::vec2(v.x, v.z));
  auto find = [&](std::initializer_list<const char*> keys) { for (const char* k : keys) for (size_t i = 0; i < m->anims.size(); i++) { std::string n = m->anims[i].name; for (char& c : n) c = (char)std::tolower((unsigned char)c); if (n.find(k) != std::string::npos) return (int)i; } return -1; };
  std::string want; bool air = !grounded_ && !tune_.fly && !swimming_;
  if (air) want = v.y > 0.5f ? "jump" : "fall"; else if (hs > 0.25f) want = hs > (tune_.walk + tune_.run) * 0.5f ? "run" : "walk"; else want = "idle";
  int a = want == "run" ? find({"run", "sprint", "jog", "walk"}) : want == "walk" ? find({"walk", "jog", "run"}) : want == "jump" ? find({"jump", "air", "fall"}) : want == "fall" ? find({"fall", "air", "jump"}) : find({"idle", "stand", "breath"});
  if (a < 0) a = want == "idle" ? 0 : find({"idle"}); if (a < 0) a = 0;
  float speed = want == "walk" ? glm::clamp(hs / 1.6f, 0.5f, 2.5f) : want == "run" ? glm::clamp(hs / 5.5f, 0.6f, 2.f) : 1.f;
  // on the ground a blend space (idle, walk, run mixed by speed: no pop between them); in the air jump / fall
  int ai = find({"idle", "stand", "breath"}), wi = find({"walk", "jog"}), ri = find({"run", "sprint", "jog"});
  if (!air && wi >= 0 && want != "jump") { std::vector<std::pair<int, float>> sp; if (ai >= 0) sp.push_back({ai, 0.f}); sp.push_back({wi, std::max(0.5f, tune_.walk * 0.8f)}); if (ri >= 0 && ri != wi) sp.push_back({ri, std::max(tune_.walk + 0.5f, tune_.run * 0.9f)});
    pAnim_.setSpace(sp, hs, 0.15f); float top = sp.back().second; pAnim_.speed = hs > top ? glm::clamp(hs / top, 1.f, 2.f) : 1.f; pAnim_.advance(*m, dt); pAnimName_ = m->anims[a].name; }
  else { pAnim_.play(a, want != "jump", speed, 0.15f); pAnim_.speed = speed; pAnim_.advance(*m, dt); pAnimName_ = m->anims[a].name; }
  { float hgt = m->shape.bmax.y - m->shape.bmin.y, k = hgt > 1.0f && hgt < 2.6f ? 1.f : 1.8f / std::max(hgt, 1e-3f); glm::vec3 c = phys_->position(player_);
    glm::mat4 W = glm::scale(glm::rotate(glm::translate(glm::mat4(1), c - glm::vec3(0, 0.9f, 0)), charYaw_, glm::vec3(0, 1, 0)), glm::vec3(k)) * glm::translate(glm::mat4(1), glm::vec3(0, -m->shape.bmin.y, 0));
    solveIK(*m, pAnim_, W, pIk_, grounded_ && !air, c.y - 0.9f, {player_}); }
  if (hs > 0.2f) { float tgt = std::atan2(v.x, v.z), d = std::remainder(tgt - charYaw_, 6.2831853f), mx = 12.f * dt; charYaw_ += glm::clamp(d, -mx, mx); }
}
static float footOf(const Actor& a, const glm::vec3& scale) { if (a.type != "StaticMesh") return 0; return -meshShape(a.mesh).bmin.y * std::abs(scale.y); }   // the actor's origin above its feet
bool Game::walkPath(Obj& o, const glm::vec3& goal) {
  ensureNav(); auto& w = o.walk; glm::vec3 feet = o.p - glm::vec3(0, w.foot, 0); std::vector<glm::vec3> path;
  if (!nav_.ready() || !nav_.findPath(feet, goal, path)) return false;
  w.path = path; w.i = 1; w.goalAt = goal; return true;
}
void Game::walkFinish(Obj& o, bool ok) { o.walk.active = false; o.walk.result = ok ? "success" : "fail"; o.walk.path.clear(); if (scripts_) scripts_->call(o.beh, EV_MoveDone, ok ? 1 : 0); }
void Game::walkFrame(float dt) {
  for (size_t k = 0; k < objs_.size(); k++) { Obj& o = *objs_[k]; auto& w = o.walk; if (!o.alive || !w.active) continue;
    if (!o.rag.empty()) { w.active = false; continue; }
    glm::vec3 feet = o.p - glm::vec3(0, w.foot, 0);
    // following an actor or the player: a new way when it has moved
    if (w.player || !w.goal.empty()) { glm::vec3 g; if (w.player) g = playerPos() - glm::vec3(0, mode_ == "Spectator" ? 1.6f : 0.9f, 0); else { Obj* t = get(w.goal); if (!t) { walkFinish(o, false); continue; } g = worldPos(w.goal) - glm::vec3(0, footOf(t->a, t->s), 0); }
      if (glm::length(glm::vec2(g.x - feet.x, g.z - feet.z)) <= w.accept && std::abs(g.y - feet.y) < 2.f) { walkFinish(o, true); continue; }
      if (time_ >= w.repath && glm::length(g - w.goalAt) > 0.6f) { w.repath = time_ + 0.35; if (!walkPath(o, g) && w.path.empty()) { walkFinish(o, false); continue; } } }
    if (w.i >= w.path.size()) { walkFinish(o, w.player || !w.goal.empty() ? false : true); continue; }
    glm::vec3 t = w.path[w.i]; glm::vec2 d(t.x - feet.x, t.z - feet.z); float len = glm::length(d); bool last = w.i + 1 >= w.path.size();
    if (last && w.goal.empty() && !w.player && len <= w.accept) { walkFinish(o, true); continue; }
    if (len < 0.05f) { w.i++; continue; }
    float stepLen = std::min(w.speed * dt, len); glm::vec2 dir = d / len; feet.x += dir.x * stepLen; feet.z += dir.y * stepLen;
    // on the floor: a short ray down (stairs, ramps), else between the way's points
    float prevY = w.path[w.i - 1].y, along = glm::length(glm::vec2(t.x - w.path[w.i - 1].x, t.z - w.path[w.i - 1].z)); float u = along > 1e-4f ? 1 - glm::length(glm::vec2(t.x - feet.x, t.z - feet.z)) / along : 1;
    float y = glm::mix(prevY, t.y, glm::clamp(u, 0.f, 1.f)); Physics::Hit h;
    if (phys_->rayCast({feet.x, y + level_.world.nav.step + 0.3f, feet.z}, {feet.x, y - level_.world.nav.step - 0.5f, feet.z}, h, true)) y = h.point.y;
    feet.y = y; o.p = feet + glm::vec3(0, w.foot, 0);
    if (w.turn) { float want = std::atan2(dir.x, dir.y), cur = o.r.y, diff = std::remainder(want - cur, 6.2831853f); float mx = 10.f * dt; o.r.y = cur + glm::clamp(diff, -mx, mx); }
  }
}
std::string Game::nav(const std::string& id, const std::string& op, const std::string& arg) {
  json j = json::object(); if (!arg.empty()) { try { j = json::parse(arg); } catch (...) {} }
  auto v3 = [](const json& a, glm::vec3 d) { if (a.is_array() && a.size() == 3 && a[0].is_number() && a[1].is_number() && a[2].is_number()) return glm::vec3(a[0].get<float>(), a[1].get<float>(), a[2].get<float>()); return d; };
  if (id.empty()) {
    if (op == "rebuild") { navBuilt_ = false; ensureNav(); return "1"; }
    if (op == "info") { ensureNav(); return json{{"ready", nav_.ready()}, {"cells", nav_.walkable()}, {"cellSize", nav_.cell()}, {"ms", nav_.buildMs()}}.dump(); }
    if (op == "path") { ensureNav(); glm::vec3 a = v3(j.value("from", json()), glm::vec3(0)), b = v3(j.value("to", json()), glm::vec3(0));
      if (j.value("fromPlayer", false)) a.y -= 0.9f; if (j.value("toPlayer", false)) b.y -= 0.9f;
      std::vector<glm::vec3> p; if (!nav_.findPath(a, b, p)) return ""; json r = json::array(); for (auto& q : p) r.push_back({q.x, q.y, q.z}); return r.dump(); }
    return ""; }
  Obj* o = get(id); if (!o) return ""; auto& w = o->walk;
  if (op == "state") return json{{"moving", w.active}, {"result", w.result}}.dump();
  if (op == "stop") { if (w.active) walkFinish(*o, false); w.result = "stopped"; return "1"; }
  if (op == "move") {
    if (o->motion == Motion::Dynamic) { logMsg("warning: " + o->a.name + ": Move To moves actors without Simulate Physics", LogKind::Warn); return "0"; }
    // the same walk again (a Blueprint's Tick, a script's onUpdate): just its speed changes, no new way
    bool samePlayer = j.value("player", false) && w.active && w.player, sameActor = j.contains("actor") && j["actor"].is_string() && w.active && w.goal == j["actor"].get<std::string>();
    bool samePlace = w.active && !w.player && w.goal.empty() && !j.contains("actor") && !j.value("player", false) && glm::length(glm::vec3(j.value("x", 0.f), j.value("y", 0.f), j.value("z", 0.f)) - w.goalAt) < 0.1f;
    if (samePlayer || sameActor || samePlace) { w.speed = std::max(0.f, j.value("speed", w.speed)); w.accept = std::max(0.05f, j.value("acceptance", w.accept)); return "1"; }
    w = Obj::Walk(); w.foot = footOf(o->a, o->s); w.speed = std::max(0.f, j.value("speed", 3.5f)); w.accept = std::max(0.05f, j.value("acceptance", 0.5f)); w.turn = j.value("turn", true);
    glm::vec3 goal;
    if (j.value("player", false)) { w.player = true; goal = playerPos() - glm::vec3(0, 0.9f, 0); }
    else if (j.contains("actor") && j["actor"].is_string()) { w.goal = j["actor"]; Obj* t = get(w.goal); if (!t) return "0"; goal = worldPos(w.goal) - glm::vec3(0, footOf(t->a, t->s), 0); }
    else goal = {j.value("x", 0.f), j.value("y", 0.f), j.value("z", 0.f)};
    if (!walkPath(*o, goal)) { w.result = "fail"; logMsg("warning: " + o->a.name + ": no way to walk there" + (nav_.ready() ? "" : " (no navigation: the level needs still meshes with collision)"), LogKind::Warn); return "0"; }
    w.active = true; w.repath = time_ + 0.35; return "1"; }
  return "";
}
std::string Game::animation(const std::string& id, const std::string& op, const std::string& arg) {
  if (id == "@player") {   // the player's character: hands and head (its feet stand on the ground by themselves)
    if (op == "ikClear") { pIk_.clear(); return "1"; }
    if (op != "ik" && op != "look") return ""; json j = json::parse(arg.empty() ? "{}" : arg, nullptr, false); if (!j.is_object()) return "0";
    std::string bone = j.value("bone", std::string(op == "look" ? "Head" : "")); pIk_.erase(std::remove_if(pIk_.begin(), pIk_.end(), [&](auto& r) { return r.bone == bone; }), pIk_.end());
    if (j.value("off", false)) return "1"; Obj::IkReq r; r.bone = bone; r.aim = op == "look"; r.weight = j.value("weight", 1.f); r.maxAngle = j.value("maxAngle", 70.f);
    if (j.contains("actor") && j["actor"].is_string()) r.actor = j["actor"]; else if (j.contains("target") && j["target"].is_array() && j["target"].size() == 3) r.target = {j["target"][0].get<float>(), j["target"][1].get<float>(), j["target"][2].get<float>()}; else return "0";
    pIk_.push_back(r); return "1"; }
  Obj* o = get(id); if (!o || o->a.type != "StaticMesh" || !isModelFile(o->a.mesh)) return ""; ModelRef m = model(o->a.mesh); if (!m || !m->ok()) return "";
  AnimPlay& an = o->anim;
  if (op == "list") { json a = json::array(); for (auto& x : m->anims) a.push_back(x.name); return a.dump(); }
  if (op == "state") { json s = {{"name", an.anim >= 0 && an.anim < (int)m->anims.size() ? m->anims[an.anim].name : ""}, {"time", an.time}, {"playing", an.playing}, {"loop", an.loop}, {"speed", an.speed},
      {"duration", an.anim >= 0 && an.anim < (int)m->anims.size() ? m->anims[an.anim].duration : 0.f}}; return s.dump(); }
  if (op == "stop") { o->autoAnim = false; auto keepF = an.fixed; auto keepS = an.scaled; an = AnimPlay(); an.fixed = keepF; an.scaled = keepS; return "1"; }
  if (op == "speed") { an.speed = (float)std::atof(arg.c_str()); return "1"; }
  { std::string r = animOp(*o, *m, op, arg); if (!r.empty()) return r == "-" ? "" : r; }
  if (op == "play") { o->autoAnim = false; json j = json::object(); try { j = json::parse(arg); } catch (...) {}
    std::string n = j.value("name", ""); int ai = m->anim(n);
    if (ai < 0) { std::string names; for (auto& x : m->anims) names += (names.empty() ? "" : ", ") + x.name; logMsg("warning: " + o->a.name + ": no animation \"" + n + "\" in " + o->a.mesh + (names.empty() ? " (it has none)" : " - it has: " + names), LogKind::Warn); return "0"; }
    float blend = j.contains("blend") && j["blend"].is_number() ? j["blend"].get<float>() : 0.2f, speed = j.contains("speed") && j["speed"].is_number() ? j["speed"].get<float>() : 1.f;
    an.play(ai, !j.contains("loop") || !j["loop"].is_boolean() || j["loop"].get<bool>(), speed, blend); return "1"; }
  if (op == "boneScale") { json j = json::parse(arg, nullptr, false); if (!j.is_object()) return "0"; std::string bn = j.value("name", std::string()); int ni = -1; for (size_t i = 0; i < m->nodes.size(); i++) if (m->nodes[i].name == bn) { ni = (int)i; break; }
    if (ni < 0) return "0"; float k = j.value("scale", 1.f); an.scaled.erase(std::remove_if(an.scaled.begin(), an.scaled.end(), [&](auto& x) { return x.first == ni; }), an.scaled.end());
    if (std::abs(k - 1) > 1e-4f) an.scaled.push_back({ni, glm::vec3(k)}); return "1"; }
  if (op == "bone") { int ni = -1; for (size_t i = 0; i < m->nodes.size(); i++) if (m->nodes[i].name == arg) { ni = (int)i; break; } if (ni < 0) return "";
    ModelPose p; poseModel(*m, an.anim >= 0 || !an.fixed.empty() || !an.scaled.empty() ? &an : nullptr, p); glm::mat4 W = world(*o) * p.world[ni]; glm::vec3 pos, sc; glm::quat q; decompose(W, pos, q, sc); glm::vec3 e = eulerYXZ(q);
    return json{{"p", {pos.x, pos.y, pos.z}}, {"r", {e.x, e.y, e.z}}}.dump(); }
  return "";
}
void Game::message(const std::string& t, float sec) { msg_ = t; msgUntil_ = time_ + std::max(0.1f, sec); }
void Game::hud(const std::string& id, const std::string& t) {
  for (auto it = hud_.begin(); it != hud_.end(); ++it) if (it->first == id) { if (t.empty()) hud_.erase(it); else it->second = t; return; }
  if (!t.empty()) hud_.push_back({id, t});
}
void Game::setGravity(float y) { phys_->setGravity(y); gravity_ = y; }
glm::vec3 Game::playerPos() { return player_ != NO_BODY ? phys_->position(player_) : specPos_; }
void Game::playerTeleport(const glm::vec3& p) { if (player_ != NO_BODY) { phys_->teleport(player_, p, glm::quat(1, 0, 0, 0)); phys_->setVelocity(player_, glm::vec3(0)); } else specPos_ = p; }
void Game::playerLaunch(const glm::vec3& v) { if (player_ != NO_BODY) phys_->setVelocity(player_, v); }
std::string Game::playerGet() { const Tune& t = tune_; glm::vec3 v = player_ != NO_BODY && phys_ ? phys_->velocity(player_) : glm::vec3(0);
  return json{{"fly", t.fly}, {"locked", t.locked}, {"walkSpeed", t.walk}, {"runSpeed", t.run}, {"sneakSpeed", t.sneak}, {"jumpSpeed", t.jump}, {"flySpeed", t.flySpeed}, {"flyRun", t.flyRun}, {"flyClimb", t.flyClimb},
    {"gravity", t.gravity}, {"accel", t.accel}, {"runKey", t.runKey}, {"sneakKey", t.sneakKey}, {"doubleTapRun", t.doubleTapRun}, {"yaw", yaw_}, {"pitch", pitch_},
    {"grounded", grounded_}, {"running", running_}, {"sneaking", sneaking_}, {"velocity", {v.x, v.y, v.z}}, {"health", health_}, {"maxHealth", maxHealth_}, {"dead", dead_},
    {"vehicle", driving_}, {"vehicleKey", vehicleKey_}}.dump(); }
void Game::playerSet(const std::string& js) { json o = json::parse(js, nullptr, false); if (!o.is_object()) return; Tune& t = tune_;
  if (o.contains("mouseSensitivity") && o["mouseSensitivity"].is_number()) mouseSens_ = std::clamp(o["mouseSensitivity"].get<float>(), 0.05f, 6.f);
  auto f = [&](const char* k, float& d) { if (o.contains(k) && o[k].is_number()) d = o[k].get<float>(); };
  auto b = [&](const char* k, bool& d) { if (o.contains(k) && o[k].is_boolean()) d = o[k].get<bool>(); };
  auto s = [&](const char* k, std::string& d) { if (o.contains(k) && o[k].is_string()) d = o[k].get<std::string>(); };
  b("fly", t.fly); b("locked", t.locked); b("doubleTapRun", t.doubleTapRun); f("walkSpeed", t.walk); f("runSpeed", t.run); f("sneakSpeed", t.sneak); f("jumpSpeed", t.jump); f("flySpeed", t.flySpeed);
  f("flyRun", t.flyRun); f("flyClimb", t.flyClimb); f("gravity", t.gravity); f("accel", t.accel); s("runKey", t.runKey); s("sneakKey", t.sneakKey); s("vehicleKey", vehicleKey_);
  f("maxHealth", maxHealth_); maxHealth_ = std::max(1.f, maxHealth_);
  if (o.contains("health") && o["health"].is_number()) { health_ = glm::clamp(o["health"].get<float>(), 0.f, maxHealth_); if (health_ > 0 && dead_) { dead_ = false; respawnAt_ = -1; } }
  if (o.contains("model") && o["model"].is_string()) { std::string m = o["model"]; pModel_ = m == "Capsule" ? "" : m.empty() ? "Characters/Mannequin.glb" : m; pAnim_ = AnimPlay(); }   // Third Person: another character
  if (o.contains("yaw") && o["yaw"].is_number()) yaw_ = o["yaw"].get<float>(); if (o.contains("pitch") && o["pitch"].is_number()) pitch_ = glm::clamp(o["pitch"].get<float>(), -1.45f, 1.45f); }
// the game's own saved values: one file per name in Documents/PremiumEngine/Saves/<game>/store
std::string Game::storage(const std::string& op, const std::string& key, const std::string& val) {
  namespace fs = std::filesystem; fs::path dir = fs::u8path(savePath("x")).parent_path() / "store";
  auto file = [&](const std::string& k) { std::string n; for (unsigned char ch : k) { if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.') n += (char)ch; else { char h[4]; std::snprintf(h, sizeof h, "%%%02X", ch); n += h; } } return dir / fs::u8path(n + ".txt"); };
  auto unname = [](const std::string& n) { std::string k; for (size_t i = 0; i < n.size(); i++) { if (n[i] == '%' && i + 2 < n.size()) { k += (char)std::stoi(n.substr(i + 1, 2), nullptr, 16); i += 2; } else k += n[i]; } return k; };
  std::error_code ec;
  if (op == "get") { std::ifstream f(file(key), std::ios::binary); if (!f) return "\x01"; return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); }
  if (op == "set") { fs::create_directories(dir, ec); fs::path p = file(key), tmp = p; tmp += ".tmp"; { std::ofstream f(tmp, std::ios::binary | std::ios::trunc); f << val; } fs::rename(tmp, p, ec); if (ec) { fs::remove(p, ec); fs::rename(tmp, p, ec); } return ""; }
  if (op == "remove") { fs::remove(file(key), ec); return ""; }
  if (op == "keys") { json a = json::array(); for (auto& e : fs::directory_iterator(dir, ec)) { std::string n = e.path().filename().u8string(); if (n.size() < 5 || n.substr(n.size() - 4) != ".txt") continue; std::string k = unname(n.substr(0, n.size() - 4)); if (k.rfind(key, 0) == 0) a.push_back(k); } return a.dump(); }
  return ""; }
void Game::log(const std::string& t) { logMsg(t); }
static double wallClock() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
void Game::netPump() { if (net_) net_->update(wallClock()); }
// net.*: one player hosts a game (TCP), the others join by address or find it on the LAN; messages are text (JSON from scripts)
std::string Game::net(const std::string& op, const std::string& a, const std::string& b) {
  if (!net_) net_ = std::make_unique<Net>(); Net& n = *net_;
  if (op == "host") { json o = json::parse(a, nullptr, false); if (!o.is_object()) o = json::object();
    return n.host(o.value("port", 46600), o.value("name", project_.name), o.value("max", 8), o.value("info", std::string())) ? "1" : "0"; }
  if (op == "join") return n.join(a, b.empty() ? 46600 : std::atoi(b.c_str())) ? "1" : "0";
  if (op == "stop") { n.stop(); return ""; }
  if (op == "send") return n.send(a, b.empty() ? -1 : std::atoi(b.c_str())) ? "1" : "0";
  if (op == "kick") { n.kick(std::atoi(a.c_str())); return ""; }
  if (op == "take") { json r = json::array(); for (auto& e : n.take()) r.push_back({{"type", e.type}, {"id", e.id}, {"data", e.data}}); return r.dump(); }
  if (op == "lan") { n.lanSearch(a == "1"); return ""; }
  if (op == "list") { json r = json::array(); for (auto& g : n.lan(wallClock())) r.push_back({{"name", g.name}, {"address", g.address}, {"port", g.port}, {"players", g.players}, {"max", g.max}, {"info", g.info}}); return r.dump(); }
  if (op == "info") { n.setInfo(a, std::atoi(b.c_str())); return ""; }
  if (op == "state") { json p = json::array(); for (int id : n.peers()) p.push_back(id); return json{{"role", n.role()}, {"id", n.id()}, {"peers", p}, {"connected", n.connected()}, {"error", n.error}}.dump(); }
  return ""; }
// world.setSky({ sky, fogColor, fog, fogDensity, skyLight, groundLight, ambient, exposure, fov, sun: { dir: [x, y, z] (from the sun), color, intensity } })
// world.setCamera: behind and above a point, turned by the mouse (yaw / pitch); it comes in front of blocks between
glm::vec3 Game::followCam() const { glm::vec3 t = camTarget_ + glm::vec3(0, camHeight_, 0), dir(std::sin(yaw_) * std::cos(pitch_), -std::sin(pitch_), std::cos(yaw_) * std::cos(pitch_));
  float d = camDist_; if (vox_) for (float s = 0.3f; s < camDist_; s += 0.1f) if (vox_->solidAt(t + dir * s)) { d = std::max(0.3f, s - 0.25f); break; }
  return t + dir * d; }
void Game::setCamera(const std::string& js) { json o = json::parse(js, nullptr, false); if (!o.is_object()) { camOn_ = false; return; }
  auto v3 = [&](const char* k, glm::vec3& d) { if (o.contains(k) && o[k].is_array() && o[k].size() >= 3) for (int i = 0; i < 3; i++) d[i] = o[k][i].is_number() ? o[k][i].get<float>() : d[i]; };
  v3("target", camTarget_); if (o.contains("distance") && o["distance"].is_number()) camDist_ = std::clamp(o["distance"].get<float>(), 0.5f, 60.f);
  if (o.contains("height") && o["height"].is_number()) camHeight_ = o["height"].get<float>(); camOn_ = true; }
void Game::setSky(const std::string& js) {
  json j = json::parse(js, nullptr, false); if (!j.is_object()) return; WorldSettings& w = level_.world;
  auto col = [&](const json& o, const char* k, glm::vec3& v) { if (!o.contains(k)) return; const json& c = o[k];
    if (c.is_string()) v = hexColor(c.get<std::string>(), v); else if (c.is_array() && c.size() == 3 && c[0].is_number()) v = {c[0].get<float>(), c[1].get<float>(), c[2].get<float>()}; };
  auto num = [&](const json& o, const char* k, float& v) { if (o.contains(k) && o[k].is_number()) v = o[k].get<float>(); };
  col(j, "sky", w.sky); col(j, "skyTop", w.skyTop); col(j, "sunDisc", w.sunDisc); num(j, "moon", w.moon); num(j, "stars", w.stars); num(j, "sunSize", w.sunSize);
  col(j, "fogColor", w.fogColor); col(j, "skyLight", w.skyLight); col(j, "groundLight", w.groundLight);
  num(j, "fogDensity", w.fogDensity); num(j, "ambient", w.ambient); num(j, "exposure", w.exposure); num(j, "fov", w.fov); if (j.contains("fog") && j["fog"].is_boolean()) w.fog = j["fog"].get<bool>();
  if (j.contains("post")) { PostProcess& q = w.post; const json& o = j["post"];
    if (o.is_string() && o.get<std::string>() == "reset") q = PostProcess();
    else if (o.is_object()) { num(o, "vignette", q.vignette); num(o, "grain", q.grain); num(o, "chromatic", q.chromatic); num(o, "scanlines", q.scanlines); num(o, "vhs", q.vhs);
      num(o, "saturation", q.saturation); num(o, "contrast", q.contrast); num(o, "temperature", q.temperature); num(o, "bloom", q.bloom); col(o, "tint", q.tint);
      for (float* v : {&q.vignette, &q.grain, &q.chromatic, &q.scanlines, &q.vhs}) *v = glm::clamp(*v, 0.f, 1.f); q.saturation = glm::clamp(q.saturation, 0.f, 3.f); q.contrast = glm::clamp(q.contrast, 0.f, 3.f);
      q.temperature = glm::clamp(q.temperature, -1.f, 1.f); q.bloom = glm::clamp(q.bloom, 0.f, 10.f); } }
  if (j.contains("sun") && j["sun"].is_object()) { const json& o = j["sun"]; glm::vec3 d = sunDir_; col(o, "dir", d); if (glm::length(d) > 1e-4f) sunDir_ = glm::normalize(d);
    col(o, "color", sunColor_); num(o, "intensity", sunInt_); sunSet_ = true; w.sunDiscDir = -sunDir_; } }
std::string Game::postGet() { const PostProcess& q = level_.world.post;
  return json{{"vignette", q.vignette}, {"grain", q.grain}, {"chromatic", q.chromatic}, {"scanlines", q.scanlines}, {"vhs", q.vhs}, {"saturation", q.saturation}, {"contrast", q.contrast},
    {"temperature", q.temperature}, {"bloom", q.bloom}, {"tint", colorHex(q.tint)}}.dump(); }
std::string Game::createVoxels(const std::string& opts) {
  if (vox_) { if (ui_) ui_->setVoxels(nullptr); vox_.reset(); voxHold_ = false; }   // a new block world replaces the old one (another saved world)
  json o = json::object(); try { o = json::parse(opts); if (!o.is_object()) o = json::object(); } catch (...) { return "voxels.create: options are not valid"; }
  VoxelConfig c; c.minY = o.value("bottom", c.minY); c.maxY = o.value("top", c.maxY); c.viewDistance = o.value("viewDistance", c.viewDistance); c.seed = o.value("seed", c.seed); c.threads = o.value("threads", 0);
  if (o.contains("nearLayer") && o["nearLayer"].is_array() && o["nearLayer"].size() == 2) { c.fineLo = o["nearLayer"][0].get<int>(); c.fineHi = o["nearLayer"][1].get<int>(); }
  vox_.reset(new VoxelWorld(c, phys_.get())); if (ui_) ui_->setVoxels(vox_.get()); if (models_) models_->setBlocks(vox_.get()); return "";
}
// the ear is the camera; sounds that follow an actor move with it (and stop when it is gone)
void Game::soundFrame() {
  if (!audio_) return;
  audio_->update(cameraPos(), cameraDir(), [this](const std::string& id, glm::vec3& p) {
    if (id == "@player") { p = playerPos(); return true; } Obj* o = get(id); if (!o || !o->alive) return false; p = worldPos(id); return true; });
}
Audio* Game::audio() {
  if (!audio_) { audio_.reset(new Audio(g_headlessAudio)); audio_->setFiles([this](const std::string& p) { return assetPath(p); }); if (!audio_->ok()) { audio_.reset(); return nullptr; } }
  return audio_.get();
}
std::string Game::assetPath(const std::string& p) {
  namespace fs = std::filesystem; fs::path f = fs::u8path(p); if (f.is_absolute()) return p;
  std::vector<fs::path> roots; if (!project_.path.empty()) roots.push_back(fs::u8path(project_.path).parent_path()); roots.push_back(fs::u8path(exeDir()));
  for (auto& r : roots) { std::error_code ec; if (fs::exists(r / f, ec)) return (r / f).u8string(); if (fs::exists(r / "assets" / f, ec)) return (r / "assets" / f).u8string(); }
  return p;
}
glm::vec3 Game::cameraPos() { if (camOn_) return followCam(); if (camSet_) return glm::vec3(camW_[3]); if (mode_ == "Spectator") return specPos_; return player_ != NO_BODY ? phys_->position(player_) + glm::vec3(0, 0.7f, 0) : specPos_; }
std::string Game::savePath(const std::string& name) {
  std::string game = project_.name.empty() ? "Game" : project_.name, n = name.empty() ? "world" : name;
  for (auto* s : {&game, &n}) for (char& ch : *s) if (ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|') ch = '_';
  return (std::filesystem::u8path(documentsDir()) / "Saves" / std::filesystem::u8path(game) / std::filesystem::u8path(n + ".pgworld")).u8string();
}
glm::vec3 Game::cameraDir() { if (camOn_) { glm::vec3 d = camTarget_ + glm::vec3(0, camHeight_, 0) - followCam(); return glm::length(d) > 1e-4f ? glm::normalize(d) : glm::vec3(0, 0, -1); } if (camSet_) return glm::normalize(-glm::vec3(camW_[2])); return quatYXZ({pitch_, yaw_, 0}) * glm::vec3(0, 0, -1); }
// A trophy: Premium Games (which started this game) reads the events file it named and puts the trophy on the player's account
void Game::trophy(const std::string& id) {
  bool ok = !id.empty() && id.size() <= 40; for (char ch : id) ok = ok && (std::isalnum((unsigned char)ch) || ch == '_' || ch == '-');
  if (!ok) { logMsg("warning: premium.trophy: the id is letters, numbers, _ or - (up to 40)", LogKind::Warn); return; }
  if (!trophies_.insert(id).second) return;   // once per run is enough
  const char* f = std::getenv("PREMIUM_GAMES_EVENTS");
  if (!f || !*f) { logMsg("[trophy] " + id + " (goes on the account when played from Premium Games)"); return; }
  if (FILE* o = std::fopen(f, "ab")) { std::fprintf(o, "trophy %s\n", id.c_str()); std::fclose(o); }
}

}
