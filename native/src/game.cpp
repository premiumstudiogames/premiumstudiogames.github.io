#include "game.h"
#include "log.h"
#include "arabic.h"
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <filesystem>

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
static void meshBounds(const std::string& mesh, glm::vec3& mn, glm::vec3& mx) {
  static std::map<std::string, std::pair<glm::vec3, glm::vec3>> cache;
  auto it = cache.find(mesh); if (it == cache.end()) { MeshData d = makePrimitive(mesh); it = cache.emplace(mesh, std::make_pair(d.bmin, d.bmax)).first; }
  mn = it->second.first; mx = it->second.second;
}

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

void addActorVisual(Frame& f, const Project& P, const Actor& a, const glm::mat4& W, bool editor, unsigned id, const glm::vec3* colorOverride, const std::string* textOverride, const Renderer* r) {
  glm::vec3 pos(W[3]); glm::vec3 fwd = glm::normalize(glm::mat3(W) * glm::vec3(0, 0, -1) + glm::vec3(1e-7f));
  glm::mat4 at = glm::translate(glm::mat4(1), pos);                       // icon placed at the actor, no scale
  glm::mat4 rotOnly = at * glm::mat4(glm::mat3(glm::normalize(glm::vec3(W[0])), glm::normalize(glm::vec3(W[1])), glm::normalize(glm::vec3(W[2]))));
  auto icon = [&](const char* mesh, glm::vec3 c, float s) { DrawItem d; d.mesh = &primitive(mesh); d.model = glm::scale(at, glm::vec3(s)); d.color = c; d.unlit = true; d.castShadow = false; d.id = id; f.draws.push_back(d); };
  auto proxy = [&](const char* mesh, glm::mat4 m) { DrawItem d; d.mesh = &primitive(mesh); d.model = m; d.pickOnly = true; d.id = id; f.draws.push_back(d); };
  auto line = [&](const GpuMesh& m, glm::mat4 model, glm::vec3 c, bool top) { LineItem l; l.mesh = &m; l.model = model; l.color = glm::vec4(c, 0.95f); l.onTop = top; l.id = id; f.lines.push_back(l); };
  if (a.type == "StaticMesh") {
    DrawItem d; d.mesh = &primitive(a.mesh); d.model = W; d.id = id;
    auto it = P.materials.find(a.material); Material m = it != P.materials.end() ? it->second : Material();
    d.color = colorOverride ? *colorOverride : m.color; d.emissive = m.emissive * m.emissiveIntensity; d.metal = m.metalness; d.rough = m.roughness; d.opacity = m.opacity;
    f.draws.push_back(d);
  } else if (a.isLight()) {
    LightItem l; l.type = a.type == "DirectionalLight" ? 0 : a.type == "PointLight" ? 1 : 2; l.pos = pos; l.dir = fwd; l.color = a.light.color; l.intensity = a.light.intensity;
    l.range = a.light.range; l.cosOuter = std::cos(glm::radians(glm::clamp(a.light.angle, 1.f, 89.f))); l.shadows = a.light.shadows; f.lights.push_back(l);
    if (editor) { icon("Sphere", a.type == "DirectionalLight" ? glm::vec3(1, 0.91f, 0.66f) : a.light.color, 0.3f);
      if (r && a.type != "PointLight") line(r->arrow(), rotOnly, glm::vec3(1, 0.85f, 0.4f), true); }
  } else if (a.type == "TextRender") {
    TextItem t; t.text = textOverride ? *textOverride : a.text.value; t.model = W; t.color = colorOverride ? *colorOverride : a.text.color; t.size = a.text.size; t.id = id; f.texts.push_back(t);
  } else if (editor && r) {
    if (a.type == "PlayerStart") { glm::vec3 c(0.31f, 0.76f, 0.97f); line(r->wireCapsule(), rotOnly, c, true); line(r->arrow(), glm::scale(glm::translate(rotOnly, {0, 0.2f, 0}), glm::vec3(1.2f)), c, true); proxy("Capsule", rotOnly); }
    else if (a.type == "CameraActor") { line(r->wireCam(), rotOnly, glm::vec3(0.78f, 0.57f, 0.92f), true); proxy("Cube", glm::scale(rotOnly, glm::vec3(0.4f, 0.3f, 0.5f))); }
    else if (a.type == "TriggerBox") { line(r->wireBox(), W, glm::vec3(1, 0.62f, 0.25f), false); proxy("Sphere", glm::scale(at, glm::vec3(0.35f))); }
    else { line(r->axes(), glm::scale(rotOnly, glm::vec3(0.5f)), glm::vec3(0.9f), true); proxy("Sphere", glm::scale(at, glm::vec3(0.3f))); }
  }
}

/* ------------------------------------------------------------------ game */
Game::Game(const Project& p, const std::string& ln) : levelName(ln), project_(p) {}
Game::~Game() { clear(); }

void Game::clear() {
  if (scripts_) scripts_->shutdown();
  scripts_.reset();
  vox_.reset();   // before the physics world: chunk bodies live there
  if (phys_) phys_->shutdown();
  phys_.reset(); objs_.clear(); byId_.clear(); hud_.clear(); msg_.clear(); player_ = NO_BODY; started_ = false;
}
Game::Obj* Game::get(const std::string& id) const { auto it = byId_.find(id); return it != byId_.end() && it->second->alive ? it->second : nullptr; }
glm::mat4 Game::world(const Obj& o) const {
  glm::mat4 m = trsMatrix(o.p, o.r, o.s); int guard = 0;
  for (Obj* p = get(o.a.parent); p && guard < 64; p = get(p->a.parent), guard++) m = trsMatrix(p->p, p->r, p->s) * m;
  return m;
}

bool Game::start() {
  clear();
  const Level* L = project_.level(levelName); if (!L) { logMsg("error: level \"" + levelName + "\" not found", LogKind::Error); return false; }
  level_ = *L; time_ = 0; restartRequested_ = false;
  for (auto& a : level_.actors) { auto o = std::make_unique<Obj>(); o->a = a; o->p = a.p; o->r = glm::radians(a.r); o->s = a.s; o->visible = a.visible; byId_[a.id] = o.get(); objs_.push_back(std::move(o)); }
  phys_.reset(new Physics()); phys_->init(level_.world.gravity); gravity_ = level_.world.gravity; swimming_ = false;
  for (size_t i = 0; i < objs_.size(); i++) { Obj& o = *objs_[i]; if (o.a.type == "StaticMesh" && (o.a.physics.collision || o.a.physics.simulate)) addBody(o); }
  // player
  mode_ = level_.world.gameMode; if (mode_ != "ThirdPerson" && mode_ != "Spectator") mode_ = "FirstPerson";
  float yawDeg = 0; glm::vec3 ps = playerStart(&yawDeg); yaw_ = glm::radians(yawDeg); pitch_ = 0;
  if (mode_ == "Spectator") specPos_ = ps + glm::vec3(0, 1.6f, 0); else player_ = phys_->addPlayer(ps);
  // scripts
  scripts_.reset(new ScriptHost()); scripts_->init(this);
  for (auto& op : objs_) { Obj& o = *op; if (o.a.script.empty()) continue;
    auto it = project_.scripts.find(o.a.script); if (it == project_.scripts.end()) { logMsg("warning: " + o.a.name + ": script \"" + o.a.script + "\" not found", LogKind::Warn); continue; }
    o.beh = scripts_->attach(o.a.id, o.a.name, it->second.name, it->second.code); }
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
  o.motion = o.a.physics.simulate ? Motion::Dynamic : scripted ? Motion::Kinematic : Motion::Static;
  if (o.motion == Motion::Dynamic && !o.a.parent.empty()) { o.a.parent.clear(); o.p = pos; o.r = eulerYXZ(q); o.s = sc; }   // simulated actors leave their parent
  size_t idx = 0; for (; idx < objs_.size(); idx++) if (objs_[idx].get() == &o) break;
  o.body = phys_->addMesh(o.a.mesh, sc, pos, q, o.motion, o.a.physics.mass, idx + 1);
  o.lastWorld = world(o); o.bodyScale = sc;
}

void Game::event(const SDL_Event& e) {
  if (!started_) return;
  switch (e.type) {
    case SDL_KEYDOWN: { std::string k = keyCode(e.key.keysym.scancode);
      if (!keys_.count(k)) pressed_.insert(k); keys_.insert(k);
      if (!e.key.repeat) for (int b = 0; b < scripts_->count(); b++) scripts_->call(b, EV_Key, 0, k);
      break; }
    case SDL_KEYUP: keys_.erase(keyCode(e.key.keysym.scancode)); break;
    case SDL_MOUSEMOTION: if (SDL_GetRelativeMouseMode() || mouseState_.w > 0) { lookDx_ += e.motion.xrel; lookDy_ += e.motion.yrel; } break;
    case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: {
      bool down = e.type == SDL_MOUSEBUTTONDOWN; const char* k = e.button.button == SDL_BUTTON_LEFT ? "MouseLeft" : e.button.button == SDL_BUTTON_RIGHT ? "MouseRight" : e.button.button == SDL_BUTTON_MIDDLE ? "MouseMiddle" : nullptr;
      if (e.button.button == SDL_BUTTON_LEFT) mouseState_.z = down; if (e.button.button == SDL_BUTTON_RIGHT) mouseState_.w = down;
      if (k) { if (down) { if (!keys_.count(k)) pressed_.insert(k); keys_.insert(k); } else keys_.erase(k); }
      break; }
    default: break;
  }
}
void Game::releaseInput() { keys_.clear(); pressed_.clear(); mouseState_ = glm::vec4(0); }

void Game::update(float dt) {
  if (!started_) return;
  dt = std::min(dt, 0.05f); time_ += dt;
  if (vox_) { vox_->update(playerPos()); for (auto& l : vox_->takeLogs()) logMsg(l, l.rfind("error", 0) == 0 ? LogKind::Error : l.rfind("warning", 0) == 0 ? LogKind::Warn : LogKind::Info); }
  // look
  const float sens = 0.0022f; mouseState_.x = lookDx_; mouseState_.y = lookDy_;
  yaw_ -= lookDx_ * sens; pitch_ = glm::clamp(pitch_ - lookDy_ * sens, -1.45f, 1.45f); lookDx_ = lookDy_ = 0;
  glm::vec3 fwd(-std::sin(yaw_), 0, -std::cos(yaw_)), right(-fwd.z, 0, fwd.x), mv(0);
  auto k = [&](const char* c) { return keys_.count(c) > 0; };
  if (k("KeyW") || k("ArrowUp")) mv += fwd; if (k("KeyS") || k("ArrowDown")) mv -= fwd; if (k("KeyD") || k("ArrowRight")) mv += right; if (k("KeyA") || k("ArrowLeft")) mv -= right;
  if (glm::dot(mv, mv) > 0) mv = glm::normalize(mv);
  float speed = (k("ShiftLeft") || k("ShiftRight")) ? 9.f : 5.f; moveDir_ = mv;
  if (mode_ == "Spectator") { if (k("KeyE")) mv.y += 1; if (k("KeyQ")) mv.y -= 1; specPos_ += mv * speed * 1.6f * dt; }
  else if (player_ != NO_BODY) {
    glm::vec3 c = phys_->position(player_), v = phys_->velocity(player_); v.x = mv.x * speed; v.z = mv.z * speed;
    glm::vec3 feet = c - glm::vec3(0, 0.9f, 0); bool grounded = false;
    const glm::vec2 offs[5] = {{0, 0}, {0.25f, 0}, {-0.25f, 0}, {0, 0.25f}, {0, -0.25f}};
    for (auto& o : offs) if (phys_->rayHit(feet + glm::vec3(o.x, 0.1f, o.y), feet + glm::vec3(o.x, -0.15f, o.y), player_)) { grounded = true; break; }
    // block world liquids: swim (slower, Space goes up, otherwise sink slowly; climb out at the edge)
    swimming_ = false;
    if (vox_) { bool body = vox_->liquidAt(feet + glm::vec3(0, 0.45f, 0)) || vox_->liquidAt(c), head = vox_->liquidAt(c + glm::vec3(0, 0.7f, 0));
      if (body) { swimming_ = true; v.x *= 0.55f; v.z *= 0.55f;
        float target = k("Space") ? (head ? 2.4f : 3.4f) : head ? -0.7f : -0.25f;
        v.y = v.y + (target - v.y) * std::min(1.f, dt * 5.f) - gravity_ * dt; } }
    if (grounded && !swimming_ && pressed_.count("Space")) v.y = 5.2f;
    // block world: walk up 20 - 40 cm steps without jumping
    if (vox_ && grounded && glm::dot(mv, mv) > 0 && v.y <= 0.5f) {
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
  phys_->step(dt);
  for (auto& [ua, ub] : phys_->takeContacts()) {
    auto side = [&](uint64_t self, uint64_t other) {
      if (self == PLAYER_TAG || self == 0 || self > objs_.size()) return; Obj& o = *objs_[self - 1]; if (!o.alive || o.beh < 0) return;
      std::string oth = other == PLAYER_TAG ? "@player" : (other > 0 && other <= objs_.size() && objs_[other - 1]->alive) ? objs_[other - 1]->a.id : "";
      scripts_->call(o.beh, EV_Hit, 0, oth); };
    side(ua, ub); side(ub, ua);
  }
  for (auto& op : objs_) { Obj& o = *op; if (!o.alive || o.body == NO_BODY || o.motion != Motion::Dynamic) continue; glm::quat q; phys_->transform(o.body, o.p, q); o.r = eulerYXZ(q); o.lastWorld = world(o); }
  // scripts
  for (int b = 0; b < scripts_->count(); b++) scripts_->call(b, EV_Update, dt);
  // fell out of the world?
  if (player_ != NO_BODY && phys_->position(player_).y - 0.9f < level_.world.killZ) { logMsg("Player fell below Kill Z: respawn"); respawnPlayer(); }
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
  for (auto& op : objs_) { Obj& o = *op; if (!o.alive || o.body == NO_BODY || o.motion == Motion::Dynamic) continue;
    glm::mat4 W = world(o); if (W == o.lastWorld) continue; glm::vec3 pos, sc; glm::quat q; decompose(W, pos, q, sc);
    if (glm::any(glm::greaterThan(glm::abs(sc - o.bodyScale), glm::vec3(1e-4f)))) { phys_->setShape(o.body, o.a.mesh, sc); o.bodyScale = sc; }
    if (o.motion == Motion::Kinematic) phys_->moveKinematic(o.body, pos, q); else phys_->teleport(o.body, pos, q);
    o.lastWorld = W; }
  pressed_.clear(); scripts_->runJobs();
  if (restartRequested_) { logMsg("Restarting the level"); start(); }
}

void Game::respawnPlayer() { glm::vec3 p = playerStart(nullptr); if (player_ != NO_BODY) { phys_->teleport(player_, p, glm::quat(1, 0, 0, 0)); phys_->setVelocity(player_, glm::vec3(0)); } else specPos_ = p; }

void Game::buildFrame(Frame& f, Camera& cam, float aspect) const {
  f = Frame(); f.world = level_.world; f.fog = level_.world.fog;
  for (auto& op : objs_) { const Obj& o = *op; if (!o.alive) continue;
    bool vis = o.visible; int guard = 0; for (Obj* p = get(o.a.parent); p && vis && guard < 64; p = get(p->a.parent), guard++) vis = p->visible;
    if (!vis) continue;
    addActorVisual(f, project_, o.a, world(o), false, 0, o.hasColor ? &o.color : nullptr); }
  glm::vec3 eye;
  if (mode_ == "Spectator") eye = specPos_; else eye = phys_->position(player_) + glm::vec3(0, 0.7f, 0);
  glm::mat4 camW;
  if (mode_ == "ThirdPerson") {
    glm::vec3 c = phys_->position(player_);
    DrawItem d; d.mesh = &primitive("Capsule"); float face = glm::dot(moveDir_, moveDir_) > 0 ? std::atan2(-moveDir_.x, -moveDir_.z) : yaw_;
    d.model = glm::rotate(glm::translate(glm::mat4(1), c), face, AY); d.color = glm::vec3(0.227f, 0.482f, 1.f); d.rough = 0.5f; f.draws.push_back(d);
    glm::vec3 back = glm::vec3(std::sin(yaw_) * std::cos(pitch_), -std::sin(pitch_), std::cos(yaw_) * std::cos(pitch_)) * 4.5f;
    glm::vec3 cp = eye + back + glm::vec3(0, 0.3f, 0);
    cam.view = glm::lookAt(cp, eye, AY); cam.pos = cp;
  } else { camW = glm::translate(glm::mat4(1), eye) * glm::mat4_cast(quatYXZ({pitch_, yaw_, 0})); cam.view = glm::inverse(camW); cam.pos = eye; }
  cam.proj = glm::perspective(glm::radians(glm::clamp(level_.world.fov, 20.f, 150.f)), std::max(aspect, 0.01f), 0.05f, 2000.f);
  { glm::vec3 fw = -glm::vec3(glm::inverse(cam.view)[2]); fw.y = 0; f.shadowCenter = eye + (glm::length(fw) > 1e-3f ? glm::normalize(fw) * 25.f : glm::vec3(0)); }   // the shadow box covers what is in front of the camera
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
  if (mode_ == "FirstPerson") { ImVec2 c(x + w / 2, y + h / 2); dl->AddCircleFilled(c, 3.5f, IM_COL32(0, 0, 0, 120)); dl->AddCircleFilled(c, 2.2f, IM_COL32(255, 255, 255, 230)); }
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
  Obj* o = get(id); if (!o) return; o->alive = false;
  if (o->body != NO_BODY) { phys_->remove(o->body); o->body = NO_BODY; }
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
  bool known = false; for (int i = 0; i < MESH_COUNT; i++) known |= mesh == MESHES[i];
  Actor a = newActor("StaticMesh", o.value("name", mesh)); a.mesh = known ? mesh : "Cube";
  a.p = jv(o.value("position", json()), {0, 1, 0}); a.s = jv(o.value("scale", json()), glm::vec3(1)); glm::vec3 rot = jv(o.value("rotation", json()), glm::vec3(0));
  if (o.contains("material") && o["material"].is_string()) a.material = o["material"];
  a.physics.simulate = o.value("physics", false); if (o.contains("tags") && o["tags"].is_array()) for (auto& t : o["tags"]) if (t.is_string()) a.tags.push_back(t);
  auto ob = std::make_unique<Obj>(); ob->a = a; ob->p = a.p; ob->r = rot; ob->s = a.s; Obj& ref = *ob; byId_[a.id] = ob.get(); objs_.push_back(std::move(ob));
  if (o.contains("color") && o["color"].is_string()) setColor(a.id, o["color"]);
  addBody(ref);
  if (o.contains("script") && o["script"].is_string()) { auto it = project_.scripts.find(o["script"]); if (it != project_.scripts.end()) { ref.a.script = it->first; ref.beh = scripts_->attach(a.id, a.name, it->second.name, it->second.code); scripts_->call(ref.beh, EV_Start); } }
  return a.id;
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
void Game::log(const std::string& t) { logMsg(t); }
std::string Game::createVoxels(const std::string& opts) {
  if (vox_) return "the block world already exists";
  json o = json::object(); try { o = json::parse(opts); if (!o.is_object()) o = json::object(); } catch (...) { return "voxels.create: options are not valid"; }
  VoxelConfig c; c.minY = o.value("bottom", c.minY); c.maxY = o.value("top", c.maxY); c.viewDistance = o.value("viewDistance", c.viewDistance); c.seed = o.value("seed", c.seed); c.threads = o.value("threads", 0);
  if (o.contains("nearLayer") && o["nearLayer"].is_array() && o["nearLayer"].size() == 2) { c.fineLo = o["nearLayer"][0].get<int>(); c.fineHi = o["nearLayer"][1].get<int>(); }
  vox_.reset(new VoxelWorld(c, phys_.get())); return "";
}
std::string Game::assetPath(const std::string& p) {
  namespace fs = std::filesystem; fs::path f = fs::u8path(p); if (f.is_absolute()) return p;
  std::vector<fs::path> roots; if (!project_.path.empty()) roots.push_back(fs::u8path(project_.path).parent_path()); roots.push_back(fs::u8path(exeDir()));
  for (auto& r : roots) { std::error_code ec; if (fs::exists(r / f, ec)) return (r / f).u8string(); if (fs::exists(r / "assets" / f, ec)) return (r / "assets" / f).u8string(); }
  return p;
}
glm::vec3 Game::cameraPos() { if (mode_ == "Spectator") return specPos_; return player_ != NO_BODY ? phys_->position(player_) + glm::vec3(0, 0.7f, 0) : specPos_; }
std::string Game::savePath(const std::string& name) {
  std::string game = project_.name.empty() ? "Game" : project_.name, n = name.empty() ? "world" : name;
  for (auto* s : {&game, &n}) for (char& ch : *s) if (ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|') ch = '_';
  return (std::filesystem::u8path(documentsDir()) / "Saves" / std::filesystem::u8path(game) / std::filesystem::u8path(n + ".pgworld")).u8string();
}
glm::vec3 Game::cameraDir() { return quatYXZ({pitch_, yaw_, 0}) * glm::vec3(0, 0, -1); }
}
