// The gameplay toolkit of the running game: line traces, attaching actors (to bones, the player, the camera), damage and
// radial damage, decals (blood, bullet holes, scorch marks), physics joints (Unreal's Physics Constraint), picking up and
// throwing bodies, ragdolls for character models, and cars (Jolt's wheeled vehicles) the player gets in and drives.
#include "game.h"
#include "log.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <random>
#include <set>
#include <functional>

using json = nlohmann::json;
namespace pge {

static const glm::vec3 AY(0, 1, 0);
constexpr uint64_t RAG_TAG = 1ull << 41;   // userData of a ragdoll's parts: RAG_TAG | (actor index + 1)
constexpr uint64_t IDX_MASK = 0xffffffffull;

static glm::mat4 rigid(const glm::mat4& m) {   // without scale
  glm::mat4 r = m; for (int i = 0; i < 3; i++) { float l = glm::length(glm::vec3(m[i])); if (l > 1e-8f) r[i] = m[i] / l; } return r;
}
static bool v3(const json& j, glm::vec3& out) {
  if (j.is_array() && j.size() >= 3 && j[0].is_number() && j[1].is_number() && j[2].is_number()) { out = {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()}; return true; }
  if (j.is_object() && j.contains("x")) { out = {j.value("x", 0.f), j.value("y", 0.f), j.value("z", 0.f)}; return true; }
  return false;
}
static json ja(const glm::vec3& v) { return json::array({v.x, v.y, v.z}); }
static std::string lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }
static bool has(const std::string& s, std::initializer_list<const char*> keys) { for (const char* k : keys) if (s.find(k) != std::string::npos) return true; return false; }
static glm::quat fromTo(glm::vec3 a, glm::vec3 b) {
  float c = glm::dot(a, b); if (c > 0.99999f) return glm::quat(1, 0, 0, 0);
  if (c < -0.99999f) { glm::vec3 ax = std::abs(a.x) < 0.9f ? glm::cross(a, glm::vec3(1, 0, 0)) : glm::cross(a, glm::vec3(0, 1, 0)); return glm::angleAxis(3.14159265f, glm::normalize(ax)); }
  glm::vec3 x = glm::cross(a, b); return glm::normalize(glm::quat(1 + c, x.x, x.y, x.z));
}

/* ------------------------------------------------------------------ vehicles: where the wheels go, how they look */
void vehicleWheels(const Actor& a, const glm::vec3& scale, std::vector<Physics::WheelDesc>& out) {
  out.clear(); const MeshData& md = meshShape(a.mesh); glm::vec3 s = glm::abs(scale), mn = md.bmin * s, mx = md.bmax * s;
  if (md.v.empty()) { mn = -0.5f * s; mx = 0.5f * s; }
  const VehicleProps& v = a.vehicle; glm::vec3 c = (mn + mx) * 0.5f, h = (mx - mn) * 0.5f;
  float r = std::max(0.05f, v.wheelRadius), w = std::max(0.02f, v.wheelWidth), x = std::max(0.05f, h.x - w * 0.5f), z = std::max(0.1f, h.z * (1 - 2 * glm::clamp(v.axleInset, 0.f, 0.45f)));
  float y = mn.y + 0.05f;   // the suspension's top at the body's floor: the wheels hang under it
  for (int i = 0; i < 4; i++) { Physics::WheelDesc d; bool front = i < 2, left = (i & 1) == 0;
    d.pos = {c.x + (left ? -x : x), y, c.z + (front ? -z : z)}; d.radius = r; d.width = w; d.travel = std::max(0.05f, v.travel); d.stiffness = std::max(0.3f, v.stiffness);
    d.steer = front ? v.maxSteer : 0; d.handbrake = front ? 0 : 4000; d.brake = 1500 + 2 * (a.physics.mass > 100 ? a.physics.mass : 1300) * r * 0.5f;
    d.drive = v.drive == 2 || (v.drive == 1 && front) || (v.drive == 0 && !front); out.push_back(d); }
}
void addWheelDraw(Frame& f, const Project& P, const Actor& a, const glm::mat4& bodyW, const glm::mat4& local, unsigned id) {
  const VehicleProps& v = a.vehicle; if (v.wheelMesh.empty()) return; float r = std::max(0.05f, v.wheelRadius), w = std::max(0.02f, v.wheelWidth);
  auto it = P.materials.find(v.wheelMaterial); Material m; if (it != P.materials.end()) m = it->second; else { m.color = glm::vec3(0.07f); m.roughness = 0.85f; }
  if (isModelFile(v.wheelMesh)) {   // a wheel model (its axle along X): fitted to the wheel's size
    ModelRef mo = model(v.wheelMesh); if (!mo || !mo->ok()) return; glm::vec3 sz = mo->shape.bmax - mo->shape.bmin, cen = (mo->shape.bmax + mo->shape.bmin) * 0.5f; float k = 2 * r / std::max(1e-3f, std::max(sz.y, sz.z));
    Actor wa = newActor("StaticMesh", "Wheel"); wa.mesh = v.wheelMesh; wa.material = it != P.materials.end() ? v.wheelMaterial : "";
    addActorVisual(f, P, wa, bodyW * local * glm::scale(glm::mat4(1), glm::vec3(k)) * glm::translate(glm::mat4(1), -cen), false, id); return; }
  DrawItem d; d.mesh = &primitive(v.wheelMesh); d.model = bodyW * local * glm::scale(glm::mat4(1), glm::vec3(2 * r, w, 2 * r)); d.id = id; applyMaterial(d, m); f.draws.push_back(d);
}

/* ------------------------------------------------------------------ the player, bones, the camera */
glm::mat4 Game::playerMatrix() const {   // feet, facing -Z where the player looks (First Person) or the character faces (Third Person)
  glm::vec3 c = player_ != NO_BODY ? phys_->position(player_) : specPos_; float yaw = mode_ == "ThirdPerson" && !pModel_.empty() ? charYaw_ + 3.14159265f : yaw_;
  return glm::rotate(glm::translate(glm::mat4(1), c - glm::vec3(0, player_ != NO_BODY ? 0.9f : 1.6f, 0)), yaw, AY);
}
glm::mat4 Game::boneWorld(const Obj& o, const std::string& bone) const {
  glm::mat4 W = world(o); if (o.a.type != "StaticMesh" || !isModelFile(o.a.mesh)) return rigid(W);
  ModelRef m = model(o.a.mesh); if (!m || !m->ok()) return rigid(W);
  int ni = -1; for (size_t i = 0; i < m->nodes.size(); i++) if (m->nodes[i].name == bone) { ni = (int)i; break; }
  if (ni < 0) { std::string b = lower(bone); for (size_t i = 0; i < m->nodes.size(); i++) if (lower(m->nodes[i].name) == b) { ni = (int)i; break; } }
  if (ni < 0) return rigid(W);
  ModelPose p; poseModel(*m, o.anim.anim >= 0 || !o.anim.fixed.empty() ? &o.anim : nullptr, p); return rigid(W * p.world[ni]);
}
glm::mat4 Game::playerBone(const std::string& bone) const {
  ModelRef pm = pModel_.empty() || mode_ != "ThirdPerson" ? nullptr : model(pModel_); if (!pm || !pm->ok()) return playerMatrix();
  int ni = -1; std::string b = lower(bone); for (size_t i = 0; i < pm->nodes.size(); i++) if (lower(pm->nodes[i].name) == b) { ni = (int)i; break; }
  if (ni < 0) return playerMatrix();
  glm::vec3 c = phys_->position(player_); float hgt = pm->shape.bmax.y - pm->shape.bmin.y, k = hgt > 1.0f && hgt < 2.6f ? 1.f : 1.8f / std::max(hgt, 1e-3f);
  glm::mat4 W = glm::scale(glm::rotate(glm::translate(glm::mat4(1), c - glm::vec3(0, 0.9f, 0)), charYaw_, AY), glm::vec3(k)) * glm::translate(glm::mat4(1), glm::vec3(0, -pm->shape.bmin.y, 0));
  ModelPose p; poseModel(*pm, pAnim_.anim >= 0 ? &pAnim_ : nullptr, p); return rigid(W * p.world[ni]);
}
void Game::cameraCompute(glm::vec3& camPos, glm::mat4& view) const {
  glm::vec3 eye = mode_ == "Spectator" || player_ == NO_BODY ? specPos_ : phys_->position(player_) + glm::vec3(0, 0.7f, 0);
  if (Obj* v = driving_.empty() ? nullptr : get(driving_)) {   // in a car: behind it (the mouse looks around it), or in the driver's seat (V)
    glm::mat4 VW = rigid(world(*v)); const MeshData& md = meshShape(v->a.mesh); glm::vec3 s = glm::abs(v->s), mn = md.bmin * s, mx = md.bmax * s, h = (mx - mn) * 0.5f, c = (mn + mx) * 0.5f;
    if (driveInside_) { glm::vec3 seat = glm::vec3(VW * glm::vec4(c.x - h.x * 0.38f, c.y + h.y * 0.55f, c.z - h.z * 0.05f, 1));
      glm::mat4 cw = glm::translate(glm::mat4(1), seat) * glm::mat4(glm::mat3(VW)) * glm::mat4_cast(quatYXZ({pitch_, yaw_, 0})); view = glm::inverse(cw); camPos = seat; return; }
    glm::vec3 target = glm::vec3(VW * glm::vec4(c, 1)) + glm::vec3(0, h.y + 0.6f, 0);
    glm::vec3 back = glm::vec3(std::sin(yaw_) * std::cos(pitch_), -std::sin(pitch_), std::cos(yaw_) * std::cos(pitch_)) * std::max(2.f, v->a.vehicle.camDistance + h.z * 0.5f);
    glm::vec3 cp = target + back; Physics::Hit hit; float len = glm::length(cp - target);
    if (phys_->rayCastIgnoring(target, cp, hit, {v->body, player_})) cp = target + (cp - target) / len * std::max(0.6f, len * hit.fraction - 0.3f);
    view = glm::lookAt(cp, target, AY); camPos = cp; return; }
  if (mode_ == "ThirdPerson" && player_ != NO_BODY) {
    glm::vec3 back = glm::vec3(std::sin(yaw_) * std::cos(pitch_), -std::sin(pitch_), std::cos(yaw_) * std::cos(pitch_)) * 4.5f;
    glm::vec3 cp = eye + back + glm::vec3(0, 0.3f, 0);
    { Physics::Hit h; float len = glm::length(cp - eye);   // like a spring arm: the camera comes in front of walls (never onto the player's head: the view needs a direction)
      if (phys_->rayCast(eye, cp, h, true)) cp = eye + (cp - eye) / len * std::max(0.35f, len * h.fraction - 0.25f); }
    view = glm::lookAt(cp, eye, AY); camPos = cp; return; }
  glm::mat4 camW = glm::translate(glm::mat4(1), eye) * glm::mat4_cast(quatYXZ({pitch_, yaw_, 0})); view = glm::inverse(camW); camPos = eye;
}
Game::Obj* Game::objOfBody(uint32_t body) const {
  if (body == NO_BODY || !phys_) return nullptr; uint64_t ud = phys_->userData(body); if (ud == PLAYER_TAG || ud == 0) return nullptr;
  uint64_t i = ud & IDX_MASK; if (i == 0 || i > objs_.size()) return nullptr; Obj* o = objs_[i - 1].get(); return o->alive ? o : nullptr;
}

/* ------------------------------------------------------------------ attach / detach */
std::string Game::attach(Obj& o, const json& j) {
  std::string to = j.value("to", std::string()); if (to == o.a.id) return "0";
  if (to != "@player" && to != "@camera") { Obj* p = get(to); if (!p) return "0"; for (Obj* x = p; x; x = get(x->a.parent)) if (x == &o) return "0"; }   // no loops
  if (!o.a.parent.empty()) detach(o, json{{"keepWorld", true}, {"quiet", true}});
  glm::mat4 Wold = world(o);
  if (!o.rag.empty()) return "0";
  if (o.body != NO_BODY) { phys_->remove(o.body); o.body = NO_BODY; }   // carried: its body leaves the world (back when it's let go)
  o.a.parent = to; o.bone = j.value("bone", std::string());
  if (j.value("keepWorld", false)) {   // stays where it is, now moving with the parent
    glm::mat4 P = world(o) * glm::inverse(trsMatrix(o.p, o.r, o.s)); glm::mat4 L = glm::inverse(P) * Wold;
    glm::vec3 sc(glm::length(glm::vec3(L[0])), glm::length(glm::vec3(L[1])), glm::length(glm::vec3(L[2]))); glm::mat3 R(glm::vec3(L[0]) / sc.x, glm::vec3(L[1]) / sc.y, glm::vec3(L[2]) / sc.z);
    o.p = glm::vec3(L[3]); o.r = eulerYXZ(glm::quat_cast(R)); o.s = sc;
  } else { glm::vec3 off(0), rot(0); v3(j.value("offset", json()), off); v3(j.value("rotation", json()), rot); o.p = off; o.r = glm::radians(rot); }
  // a collision mesh on a moving body becomes part of it (welded, like Unreal): a car built from blocks
  Obj* root = get(to); while (root && !root->a.parent.empty() && get(root->a.parent)) root = get(root->a.parent);
  if (root && root->body != NO_BODY && root->motion == Motion::Dynamic && o.a.type == "StaticMesh" && o.a.physics.collision && !o.a.physics.simulate && j.value("weld", true)) { o.welded = true; rebuildBody(*root); }
  o.lastWorld = world(o); return "1";
}
void Game::detach(Obj& o, const json& j) {
  if (o.a.parent.empty()) return; glm::mat4 W = world(o); Obj* root = get(o.a.parent); while (root && !root->a.parent.empty() && get(root->a.parent)) root = get(root->a.parent);
  glm::vec3 vel(0); if (root && root->body != NO_BODY) vel = phys_->velocity(root->body); else if (o.a.parent == "@player" && player_ != NO_BODY) vel = phys_->velocity(player_);
  o.a.parent.clear(); o.bone.clear();
  if (j.value("keepWorld", true)) { glm::vec3 sc(glm::length(glm::vec3(W[0])), glm::length(glm::vec3(W[1])), glm::length(glm::vec3(W[2]))); glm::mat3 R(glm::vec3(W[0]) / sc.x, glm::vec3(W[1]) / sc.y, glm::vec3(W[2]) / sc.z);
    o.p = glm::vec3(W[3]); o.r = eulerYXZ(glm::quat_cast(R)); o.s = sc; }
  bool wasWelded = o.welded; o.welded = false;
  if (wasWelded && root) rebuildBody(*root);
  if (o.a.type == "StaticMesh" && (o.a.physics.collision || o.a.physics.simulate) && o.body == NO_BODY) { addBody(o);
    glm::vec3 v = vel; v3(j.value("velocity", json()), v); if (o.motion == Motion::Dynamic) phys_->setVelocity(o.body, v); }
  o.lastWorld = world(o);
}
// a moving body again with what is welded to it now (its speed, and its car, kept)
void Game::rebuildBody(Obj& o) {
  if (o.body == NO_BODY) return; glm::vec3 v = phys_->velocity(o.body), w = phys_->angularVelocity(o.body); bool car = o.vehicle != NO_BODY;
  if (car) { phys_->removeVehicle(o.vehicle); o.vehicle = NO_BODY; }
  phys_->remove(o.body); o.body = NO_BODY; addBody(o);
  if (o.body != NO_BODY) { phys_->setVelocity(o.body, v); phys_->setAngularVelocity(o.body, w); if (car) addVehicle(o); }
}

/* ------------------------------------------------------------------ decals */
static uint32_t hash32(uint32_t x) { x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16; return x; }
static float vnoise(float x, float y, uint32_t seed) {
  int xi = (int)std::floor(x), yi = (int)std::floor(y); float fx = x - xi, fy = y - yi; auto h = [&](int a, int b) { return (hash32((uint32_t)a * 73856093u ^ (uint32_t)b * 19349663u ^ seed) & 0xffff) / 65535.f; };
  float sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
  return glm::mix(glm::mix(h(xi, yi), h(xi + 1, yi), sx), glm::mix(h(xi, yi + 1), h(xi + 1, yi + 1), sx), sy);
}
static const Texture* decalTexture(const std::string& kind, int variant) {
  std::string name = "decal_" + kind + "_" + std::to_string(variant); const int N = kind == "bullet" ? 64 : 128; std::vector<unsigned char> px((size_t)N * N * 4, 0);
  uint32_t seed = hash32((uint32_t)variant * 977u + (uint32_t)kind.size() * 131u);
  auto fbm = [&](float x, float y) { return vnoise(x, y, seed) * 0.55f + vnoise(x * 2.1f, y * 2.1f, seed + 7) * 0.3f + vnoise(x * 4.3f, y * 4.3f, seed + 13) * 0.15f; };
  std::mt19937 rng(seed); std::uniform_real_distribution<float> U(0, 1);
  struct Drop { float x, y, r; }; std::vector<Drop> drops; if (kind == "blood") for (int i = 0; i < 14; i++) { float a = U(rng) * 6.283f, d = 0.42f + U(rng) * 0.5f; drops.push_back({std::cos(a) * d, std::sin(a) * d, 0.02f + U(rng) * 0.05f}); }
  for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
    float u = (x + 0.5f) / N * 2 - 1, v = (y + 0.5f) / N * 2 - 1, r = std::sqrt(u * u + v * v), ang = std::atan2(v, u); float a = 0, shade = 1;
    if (kind == "bullet") {   // a dark hole, a broken ring around it
      float n = fbm(std::cos(ang) * 3 + 5, std::sin(ang) * 3 + 5); float hole = 0.16f + n * 0.05f, ring = 0.42f + n * 0.25f;
      if (r < hole) { a = 1; shade = 0.02f; } else if (r < ring) { float t = (r - hole) / (ring - hole); a = (1 - t) * 0.85f * (0.6f + 0.4f * fbm(u * 9, v * 9)); shade = 0.12f + 0.25f * t; }
      else if (r < ring + 0.25f) { float crack = std::abs(std::sin(ang * 5 + n * 6)); a = crack > 0.96f ? (1 - (r - ring) / 0.25f) * 0.5f : 0; shade = 0.15f; }
    } else if (kind == "scorch") {   // black, soft, smoky edges
      float n = fbm(u * 3 + 11, v * 3 + 11); float edge = 0.55f + n * 0.4f; a = glm::clamp((edge - r) / 0.35f, 0.f, 1.f) * 0.95f; shade = 0.03f + 0.1f * fbm(u * 8, v * 8);
    } else {   // blood: a splat with a ragged edge and drops around it
      float n = fbm(std::cos(ang) * 2.5f + 3, std::sin(ang) * 2.5f + 3); float edge = 0.34f + n * 0.3f; float body = glm::clamp((edge - r) / 0.05f, 0.f, 1.f);
      float drop = 0; for (auto& d : drops) { float dd = std::hypot(u - d.x, v - d.y); drop = std::max(drop, glm::clamp((d.r - dd) / 0.012f, 0.f, 1.f)); }
      a = std::max(body, drop); shade = 0.55f + 0.45f * (1 - r) * (0.7f + 0.3f * fbm(u * 6, v * 6));
    }
    if (r > 0.98f) a = 0;   // nothing at the border (the picture repeats)
    unsigned char* p = &px[((size_t)y * N + x) * 4]; unsigned char g = (unsigned char)std::round(glm::clamp(shade, 0.f, 1.f) * 255); p[0] = p[1] = p[2] = g; p[3] = (unsigned char)std::round(glm::clamp(a, 0.f, 1.f) * 255); }
  return pixelTexture(name, N, N, px, true);
}
void Game::addDecal(const json& o) {
  glm::vec3 p(0), n(0, 1, 0); if (!v3(o.value("position", json()), p)) return; v3(o.value("normal", json()), n); if (glm::length(n) < 1e-5f) n = AY; n = glm::normalize(n);
  std::string kind = lower(o.value("type", std::string("blood"))); if (kind == "splat") kind = "blood"; if (kind == "hole" || kind == "bullethole") kind = "bullet"; if (kind == "burn" || kind == "explosion") kind = "scorch";
  Decal d; float size = o.contains("size") && o["size"].is_number() ? o["size"].get<float>() : kind == "bullet" ? 0.12f : kind == "scorch" ? 2.5f : 0.9f; size = glm::clamp(size, 0.01f, 50.f);
  static std::mt19937 rng(1234); float ang = o.contains("angle") && o["angle"].is_number() ? glm::radians(o["angle"].get<float>()) : std::uniform_real_distribution<float>(0, 6.2831853f)(rng);
  std::string file = o.value("texture", std::string());
  if (!file.empty()) { d.tex = texture(file, true); if (!d.tex || !d.tex->ok) { logMsg("warning: decal: can't use the picture " + file, LogKind::Warn); return; } }
  else d.tex = decalTexture(kind == "bullet" || kind == "scorch" ? kind : "blood", std::uniform_int_distribution<int>(0, 3)(rng));
  d.color = kind == "blood" ? glm::vec3(0.42f, 0.015f, 0.02f) : glm::vec3(1); if (o.contains("color") && o["color"].is_string()) d.color = hexColor(o["color"].get<std::string>(), d.color);
  d.rough = kind == "blood" ? 0.18f : 0.9f; d.born = (float)time_; d.life = o.value("life", 0.f);
  glm::mat4 m = glm::translate(glm::mat4(1), p + n * 0.006f) * glm::mat4_cast(fromTo(AY, n)) * glm::rotate(glm::mat4(1), ang, AY) * glm::scale(glm::mat4(1), glm::vec3(size, 1, size));
  // on something that moves: it moves with it (a body, or an actor)
  uint32_t body = o.contains("body") && o["body"].is_number() ? o["body"].get<uint32_t>() : NO_BODY; std::string actor = o.value("actor", std::string());
  if (body != NO_BODY && phys_->dynamic(body)) { glm::vec3 bp; glm::quat bq; phys_->transform(body, bp, bq); d.body = body; d.m = glm::inverse(glm::translate(glm::mat4(1), bp) * glm::mat4_cast(bq)) * m; if (Obj* x = objOfBody(body)) d.actor = x->a.id; }
  else if (Obj* x = actor.empty() ? nullptr : get(actor)) { d.actor = actor; d.m = glm::inverse(rigid(world(*x))) * m; }
  else d.m = m;
  decals_.push_back(d); if (decals_.size() > 512) decals_.erase(decals_.begin());   // the oldest go first
}

/* ------------------------------------------------------------------ damage */
void Game::damage(const std::string& target, float amount, json info) {
  info["amount"] = amount; glm::vec3 point(0), dir(0); bool hasPoint = v3(info.value("point", json()), point); v3(info.value("direction", json()), dir);
  if (!info.contains("type")) info["type"] = "damage";
  if (target == "@player") {
    if (dead_) return; health_ = std::max(0.f, health_ - amount); if (amount < 0) health_ = std::min(maxHealth_, health_);
    if (player_ != NO_BODY && info.contains("impulse") && info["impulse"].is_number() && glm::length(dir) > 1e-5f) phys_->setVelocity(player_, phys_->velocity(player_) + glm::normalize(dir) * info["impulse"].get<float>() / 70.f);
    std::string s = info.dump(); for (int b = 0; b < scripts_->count(); b++) scripts_->call(b, EV_PlayerDamage, amount, s);
    if (health_ <= 0 && !dead_) { dead_ = true; bool handled = false; for (int b = 0; b < scripts_->count(); b++) { handled |= scripts_->has(b, EV_PlayerDeath); scripts_->call(b, EV_PlayerDeath); }
      if (!handled) { message("You died", 2.5f); respawnAt_ = time_ + 3; } }   // no script decides: start again at the Player Start
    return; }
  Obj* o = get(target); if (!o) return;
  // a push where it was hit
  glm::vec3 imp(0); if (info.contains("impulse")) { if (info["impulse"].is_number() && glm::length(dir) > 1e-5f) imp = glm::normalize(dir) * info["impulse"].get<float>(); else v3(info["impulse"], imp); }
  if (glm::length(imp) > 0) {
    if (!o->rag.empty()) { uint32_t best = o->rag[0].body; float bd = 1e9f; for (auto& r : o->rag) { float dd = glm::length(phys_->position(r.body) - point); if (hasPoint && dd < bd) { bd = dd; best = r.body; } }
      if (hasPoint) phys_->addImpulseAt(best, imp, point); else phys_->addImpulse(best, imp); }
    else if (o->body != NO_BODY && o->motion == Motion::Dynamic) { if (hasPoint) phys_->addImpulseAt(o->body, imp, point); else phys_->addImpulse(o->body, imp); } }
  if (o->beh >= 0) scripts_->call(o->beh, EV_Damage, amount, info.dump());
}

/* ------------------------------------------------------------------ ragdolls */
bool Game::ragdoll(Obj& o, const glm::vec3& impulse, const glm::vec3& point, bool hasPoint) {
  if (!o.rag.empty()) { if (glm::length(impulse) > 0) { uint32_t best = o.rag[0].body; float bd = 1e9f; for (auto& r : o.rag) { float dd = glm::length(phys_->position(r.body) - point); if (dd < bd) { bd = dd; best = r.body; } }
      if (hasPoint) phys_->addImpulseAt(best, impulse, point); else phys_->addImpulse(o.rag[0].body, impulse); } return true; }
  if (o.a.type != "StaticMesh" || !isModelFile(o.a.mesh)) return false; ModelRef m = model(o.a.mesh); if (!m || !m->ok() || m->skins.empty()) return false;
  if (!o.a.parent.empty()) detach(o, json{{"keepWorld", true}});
  glm::vec3 walkVel(0); if (o.walk.active && o.walk.i < o.walk.path.size()) { glm::vec3 d = o.walk.path[o.walk.i] - (o.p - glm::vec3(0, o.walk.foot, 0)); d.y = 0; if (glm::length(d) > 1e-3f) walkVel = glm::normalize(d) * o.walk.speed; }
  o.walk = Obj::Walk();
  ModelPose mp; poseModel(*m, o.anim.anim >= 0 ? &o.anim : nullptr, mp); glm::mat4 W = world(o);
  size_t n = m->nodes.size(); std::vector<char> isJ(n, 0); for (auto& s : m->skins) for (int j : s.joints) if (j >= 0 && j < (int)n) isJ[j] = 1;
  auto jparent = [&](int i) { int p = m->nodes[i].parent; while (p >= 0 && !isJ[p]) p = m->nodes[p].parent; return p; };
  std::vector<glm::vec3> head(n); for (size_t i = 0; i < n; i++) head[i] = glm::vec3(W * mp.world[i][3]);
  glm::vec3 up = glm::normalize(glm::mat3(W) * AY + glm::vec3(1e-7f)), fwd = glm::normalize(glm::mat3(W) * glm::vec3(0, 0, 1) + glm::vec3(1e-7f));
  float height = (m->shape.bmax.y - m->shape.bmin.y) * std::abs(o.s.y); if (height < 0.05f) height = 1.8f;
  // skip a "root" bone at the feet: its children start the body
  std::vector<char> skip(n, 0); for (size_t i = 0; i < n; i++) if (isJ[i] && jparent((int)i) < 0 && has(lower(m->nodes[i].name), {"root", "armature"})) skip[i] = 1;
  struct Seg { int node, parentPart = -1; glm::vec3 a, b; float r, mass; std::string name; };
  std::vector<Seg> segs; std::vector<int> partOf(n, -1);
  // parents before children
  std::vector<int> order; { std::vector<char> done(n, 0); std::function<void(int)> visit = [&](int i) { if (done[i]) return; done[i] = 1; int p = m->nodes[i].parent; if (p >= 0) visit(p); order.push_back(i); }; for (size_t i = 0; i < n; i++) visit((int)i); }
  for (int i : order) { if (!isJ[i] || skip[i]) continue; std::string nm = lower(m->nodes[i].name);
    int jp = jparent(i); while (jp >= 0 && skip[jp]) jp = jparent(jp);
    std::vector<int> kids; for (size_t k = 0; k < n; k++) if (isJ[k] && jparent((int)k) == i) kids.push_back((int)k);
    glm::vec3 dir = jp >= 0 && glm::length(head[i] - head[jp]) > 1e-4f ? glm::normalize(head[i] - head[jp]) : up; glm::vec3 tail;
    if (!kids.empty()) { int best = kids[0]; float bs = -1e9f; for (int k : kids) { glm::vec3 d = head[k] - head[i]; float l = glm::length(d); float sc = l > 1e-5f ? glm::dot(d / l, dir) * l : -1e9f; if (sc > bs) { bs = sc; best = k; } }
      tail = head[best]; if (jp < 0 && glm::length(tail - head[i]) < height * 0.05f) tail = head[i] + up * height * 0.1f; }
    else { if (!has(nm, {"head", "hand", "foot", "toe"}) || has(nm, {"end", "twist", "roll"})) continue;   // a finger's tip, a twist bone: no body
      if (has(nm, {"foot", "toe"})) dir = glm::normalize(fwd - up * 0.3f); tail = head[i] + dir * height * (has(nm, {"head"}) ? 0.12f : 0.08f); }
    float len = glm::length(tail - head[i]); bool root = jp < 0 || partOf[jp] < 0;
    if (!root && len < height * 0.045f) continue;   // too small to matter (fingers): it moves with its parent
    if (has(nm, {"finger", "thumb", "index", "middle", "ring", "pinky"}) && !root) continue;
    Seg s; s.node = i; s.a = head[i]; s.b = tail; s.name = nm;
    s.r = has(nm, {"hip", "pelvis", "spine", "chest", "torso", "body"}) ? height * 0.085f : has(nm, {"head"}) ? height * 0.06f : has(nm, {"thigh", "upleg", "upperleg"}) ? height * 0.05f : glm::clamp(len * 0.2f, height * 0.022f, height * 0.04f);
    int pp = jp; while (pp >= 0 && partOf[pp] < 0) pp = jparent(pp); s.parentPart = pp >= 0 ? partOf[pp] : -1;
    if (s.parentPart < 0 && !segs.empty()) s.parentPart = 0;   // another root: held to the first part
    partOf[i] = (int)segs.size(); segs.push_back(s); }
  // bones without a body move with the nearest part above them
  if (segs.empty()) return false;
  float total = o.a.physics.mass >= 20 ? o.a.physics.mass : 70, sum = 0; for (auto& s : segs) { s.mass = std::max(0.02f, glm::length(s.b - s.a)) * s.r * s.r; sum += s.mass; } for (auto& s : segs) s.mass = std::max(0.5f, total * s.mass / sum);
  if (o.body != NO_BODY) { phys_->remove(o.body); o.body = NO_BODY; }
  size_t idx = 0; for (; idx < objs_.size(); idx++) if (objs_[idx].get() == &o) break;
  uint32_t group = phys_->newGroup(); o.rag.clear();
  for (size_t k = 0; k < segs.size() && k < 64; k++) { Seg& s = segs[k];
    uint32_t b = phys_->addCapsule(s.a, s.b, s.r, s.mass, RAG_TAG | (idx + 1), group, (int)k); if (b == NO_BODY) continue;
    glm::vec3 bp; glm::quat bq; phys_->transform(b, bp, bq); glm::mat4 T = glm::translate(glm::mat4(1), bp) * glm::mat4_cast(bq);
    o.rag.push_back({s.node, b, glm::inverse(T) * (W * mp.world[s.node])}); phys_->setVelocity(b, walkVel); }
  // joints: each part to its parent, swinging and twisting within a body's limits
  for (size_t k = 1; k < o.rag.size(); k++) { Seg& s = segs[k]; if (s.parentPart < 0 || s.parentPart >= (int)o.rag.size()) continue;
    Physics::JointDesc jd; jd.type = Physics::JCone; jd.a = o.rag[s.parentPart].body; jd.b = o.rag[k].body; jd.pivot = s.a; jd.axis = glm::length(s.b - s.a) > 1e-5f ? glm::normalize(s.b - s.a) : up; jd.normal = fwd;
    const std::string& nm = s.name; jd.friction = 0.5f;
    if (has(nm, {"spine", "chest", "torso", "pelvis", "hip"})) { jd.swing = 22; jd.twistMin = -15; jd.twistMax = 15; }
    else if (has(nm, {"neck", "head"})) { jd.swing = 40; jd.twistMin = -45; jd.twistMax = 45; }
    else if (has(nm, {"upperarm", "arm_", "shoulder", "clavicle"}) && !has(nm, {"fore", "lower"})) { jd.swing = 80; jd.twistMin = -45; jd.twistMax = 45; }
    else if (has(nm, {"thigh", "upleg", "upperleg"})) { jd.swing = 60; jd.twistMin = -25; jd.twistMax = 25; }
    else if (has(nm, {"calf", "shin", "lowerleg", "leg", "knee", "forearm", "lowerarm", "elbow"})) { jd.swing = 70; jd.twistMin = -8; jd.twistMax = 8; }
    else if (has(nm, {"hand", "foot", "toe", "wrist", "ankle"})) { jd.swing = 35; jd.twistMin = -15; jd.twistMax = 15; }
    phys_->addJoint(jd); }
  o.ragFrom = o.p - phys_->position(o.rag[0].body); o.anim = AnimPlay(); o.motion = Motion::Dynamic; o.inside = false;
  if (glm::length(impulse) > 0) { uint32_t best = o.rag[0].body; float bd = 1e9f; if (hasPoint) for (auto& r : o.rag) { float dd = glm::length(phys_->position(r.body) - point); if (dd < bd) { bd = dd; best = r.body; } }
    if (hasPoint) phys_->addImpulseAt(best, impulse, point); else for (auto& r : o.rag) phys_->addImpulse(r.body, impulse * (1.f / o.rag.size())); }
  return true;
}

/* ------------------------------------------------------------------ vehicles */
void Game::addVehicle(Obj& o) {
  if (o.body == NO_BODY || o.motion != Motion::Dynamic || o.vehicle != NO_BODY) return;
  Physics::VehicleDesc d; vehicleWheels(o.a, o.bodyScale, d.wheels); d.torque = o.a.vehicle.torque; d.maxRpm = o.a.vehicle.maxRpm;
  const MeshData& md = meshShape(o.a.mesh); float hh = md.v.empty() ? 0.5f * std::abs(o.bodyScale.y) : (md.bmax.y - md.bmin.y) * 0.5f * std::abs(o.bodyScale.y); d.centerOfMass = {0, -hh, 0};   // low, like a real car's (engine and floor): it doesn't roll over in turns
  o.vehicle = phys_->addVehicle(o.body, d); o.vin[0] = o.vin[1] = o.vin[2] = 0; o.vin[3] = 0;
}
bool Game::enterVehicle(Obj& v) {
  if (v.vehicle == NO_BODY || player_ == NO_BODY || !driving_.empty() || dead_) return false;
  driving_ = v.a.id; driveInside_ = false; phys_->setEnabled(player_, false);
  glm::vec3 f = glm::mat3(rigid(world(v))) * glm::vec3(0, 0, -1); yaw_ = std::atan2(-f.x, -f.z); pitch_ = -0.22f; v.vin[3] = 0;
  for (int b = 0; b < scripts_->count(); b++) scripts_->call(b, EV_VehicleEnter, 0, v.a.id);
  return true;
}
void Game::exitVehicle() {
  Obj* v = get(driving_); std::string id = driving_; driving_.clear(); driveInside_ = false; if (player_ == NO_BODY) return;
  glm::vec3 at = phys_->position(player_), vel(0);
  if (v) { glm::mat4 VW = rigid(world(*v)); const MeshData& md = meshShape(v->a.mesh); glm::vec3 s = glm::abs(v->s), mn = md.bmin * s, mx = md.bmax * s, h = (mx - mn) * 0.5f, c = (mn + mx) * 0.5f;
    if (v->body != NO_BODY) vel = phys_->velocity(v->body) * 0.6f;
    const glm::vec3 spots[5] = {{c.x - h.x - 0.75f, 0, c.z}, {c.x + h.x + 0.75f, 0, c.z}, {c.x, 0, c.z + h.z + 0.9f}, {c.x, 0, c.z - h.z - 0.9f}, {c.x, mx.y + 1.0f, c.z}};
    bool found = false; for (auto& sp : spots) { glm::vec3 w = glm::vec3(VW * glm::vec4(sp, 1)); w.y = std::max(w.y, glm::vec3(VW * glm::vec4(c, 1)).y + (sp.y > 0 ? sp.y - c.y : 0.15f));
      if (!phys_->boxOverlaps(w, glm::vec3(0.34f, 0.88f, 0.34f), false)) { at = w; found = true; break; } }
    if (!found) at = glm::vec3(VW * glm::vec4(c.x, mx.y + 1.2f, c.z, 1));
    v->vin[0] = v->vin[1] = v->vin[2] = 0; v->vin[3] = 1; }   // parked: the hand brake on
  phys_->setEnabled(player_, true); phys_->teleport(player_, at, glm::quat(1, 0, 0, 0)); phys_->setVelocity(player_, vel); pitch_ = 0;
  for (int b = 0; b < scripts_->count(); b++) scripts_->call(b, EV_VehicleExit, 0, id);
}

/* ------------------------------------------------------------------ the level's joints and cars */
void Game::startGameplay() {
  decals_.clear(); grabs_.clear(); jointOwner_.clear(); driving_.clear(); driveInside_ = false; health_ = maxHealth_ = 100; dead_ = false; respawnAt_ = -1; vehicleKey_ = "KeyE"; hint_.clear();
  for (auto& op : objs_) { Obj& o = *op; if (o.alive && o.a.type == "StaticMesh" && o.a.vehicle.enabled) addVehicle(o); }
  for (auto& op : objs_) { Obj& o = *op; if (!o.alive || o.a.type != "PhysicsConstraint") continue; const JointProps& c = o.a.joint;
    Obj* A = get(c.a); Obj* B = c.b.empty() ? nullptr : get(c.b);
    if (!A || A->body == NO_BODY) { logMsg("warning: " + o.a.name + ": Physics Constraint needs Actor 1 with collision (a Static Mesh)", LogKind::Warn); continue; }
    if (!c.b.empty() && (!B || B->body == NO_BODY)) { logMsg("warning: " + o.a.name + ": Actor 2 has no collision; holding Actor 1 to the world", LogKind::Warn); B = nullptr; }
    glm::mat4 W = rigid(world(o)); Physics::JointDesc jd; std::string k = lower(c.kind);
    jd.type = k == "fixed" ? Physics::JFixed : k == "ballsocket" || k == "ball" || k == "point" ? Physics::JBall : k == "slider" || k == "prismatic" ? Physics::JSlider : k == "distance" || k == "rope" ? Physics::JDistance : Physics::JHinge;
    jd.a = A->body; jd.b = B ? B->body : NO_BODY; jd.pivot = glm::vec3(W[3]); jd.axis = jd.type == Physics::JSlider ? glm::vec3(W[0]) : glm::vec3(W[1]); jd.normal = jd.type == Physics::JSlider ? glm::vec3(W[1]) : glm::vec3(W[0]);
    jd.pivotB = B ? glm::vec3(world(*B)[3]) : jd.pivot; if (jd.type == Physics::JDistance && !B) jd.pivotB = jd.pivot, jd.pivot = glm::vec3(world(*A)[3]);
    jd.limits = c.limits; jd.min = c.min; jd.max = c.max; jd.motorSpeed = c.motorSpeed; jd.motorForce = c.motorForce; jd.friction = c.friction; jd.breakForce = c.breakForce;
    o.joint = phys_->addJoint(jd); if (o.joint != NO_BODY) jointOwner_[o.joint] = o.a.id; }
}

/* ------------------------------------------------------------------ every frame */
void Game::gameplayFrame(float dt, bool post) {
  auto k = [&](const char* c) { return keys_.count(c) > 0; };
  bool menu = ui_ && ui_->modal();
  if (!post) {
    // getting in and out of cars (E, or the controller's Y)
    hint_.clear();
    if (!menu && !dead_ && player_ != NO_BODY && !vehicleKey_.empty()) {
      bool press = pressed_.count(vehicleKey_) || pressed_.count("PadY");
      if (!driving_.empty()) { if (press) exitVehicle(); }
      else { Obj* best = nullptr; float bd = 1e9f; glm::vec3 pp = phys_->position(player_);
        for (auto& op : objs_) { Obj& o = *op; if (!o.alive || o.vehicle == NO_BODY || !o.a.vehicle.enterable) continue; const MeshData& md = meshShape(o.a.mesh); glm::vec3 h = (md.bmax - md.bmin) * 0.5f * glm::abs(o.s);
          float d = glm::length(glm::vec3(rigid(world(o))[3]) - pp) - std::max(h.x, h.z); if (d < 2.2f && d < bd) { bd = d; best = &o; } }
        if (best) { hint_ = "[" + std::string(vehicleKey_ == "KeyE" ? "E" : vehicleKey_) + "] Drive " + best->a.name; if (press) enterVehicle(*best); } } }
    // the car the player drives: W / S (or RT / LT), A / D (or the left stick), Space hand brake, V the view
    if (Obj* v = driving_.empty() ? nullptr : get(driving_)) {
      if (v->vehicle == NO_BODY) exitVehicle();
      else { float thr = 0, st = 0, hb = 0;
        if (!menu) { thr = (k("KeyW") || k("ArrowUp") ? 1.f : 0.f) - (k("KeyS") || k("ArrowDown") ? 1.f : 0.f) + ax_[5] - ax_[4]; st = (k("KeyD") || k("ArrowRight") ? 1.f : 0.f) - (k("KeyA") || k("ArrowLeft") ? 1.f : 0.f);
          if (std::abs(ax_[0]) > 0.15f && std::abs(ax_[0]) > std::abs(st)) st = (ax_[0] - (ax_[0] > 0 ? 0.15f : -0.15f)) / 0.85f; hb = k("Space") || k("PadA") ? 1.f : 0.f;
          if (pressed_.count("KeyV") || pressed_.count("PadRS")) { driveInside_ = !driveInside_; if (driveInside_) { yaw_ = 0; pitch_ = 0; } else { glm::vec3 f = glm::mat3(rigid(world(*v))) * glm::vec3(0, 0, -1); yaw_ = std::atan2(-f.x, -f.z); pitch_ = -0.22f; } } }
        thr = glm::clamp(thr, -1.f, 1.f); st = glm::clamp(st, -1.f, 1.f);
        float rate = (std::abs(st) < std::abs(v->steer) || st * v->steer < 0 ? 5.f : 2.5f) * dt; v->steer += glm::clamp(st - v->steer, -rate, rate);
        float speed = phys_->vehicleState(v->vehicle).speed, br = 0;
        if ((thr > 0 && speed < -1.f) || (thr < 0 && speed > 1.f)) { br = std::abs(thr); thr = 0; }   // the other way while moving: brake first
        v->vin[0] = thr; v->vin[1] = v->steer; v->vin[2] = br; v->vin[3] = hb;
        // the chase camera comes back behind the car when the mouse rests
        if (!driveInside_) { if (mouseState_.x != 0 || mouseState_.y != 0 || std::abs(ax_[2]) > 0.15f || std::abs(ax_[3]) > 0.15f) driveIdle_ = time_;
          if (time_ - driveIdle_ > 1.2 && std::abs(speed) > 2) { glm::vec3 f = glm::mat3(rigid(world(*v))) * glm::vec3(0, 0, speed < 0 ? 1 : -1); float want = std::atan2(-f.x, -f.z), d = std::remainder(want - yaw_, 6.2831853f); yaw_ += d * std::min(1.f, dt * 2.f); } }
        // on its side or its roof and stopped for 2 s: back on its wheels (like most driving games)
        { glm::vec3 up = glm::mat3(rigid(world(*v))) * AY; if (up.y < 0.35f && std::abs(speed) < 1.5f) { if (v->flipSince < 0) v->flipSince = time_; else if (time_ - v->flipSince > 2) { glm::vec3 f = glm::mat3(rigid(world(*v))) * glm::vec3(0, 0, -1); f.y = 0;
              float yaw = glm::length(f) > 1e-3f ? std::atan2(-f.x, -f.z) : 0; phys_->teleport(v->body, v->p + glm::vec3(0, 1.2f, 0), quatYXZ({0, yaw, 0})); phys_->setVelocity(v->body, glm::vec3(0)); phys_->setAngularVelocity(v->body, glm::vec3(0)); v->flipSince = -1; } }
          else v->flipSince = -1; }
        char b[96]; std::snprintf(b, sizeof b, "%d km/h   [%s] Exit   [V] View", (int)std::round(std::abs(speed) * 3.6f), vehicleKey_ == "KeyE" ? "E" : vehicleKey_.c_str()); hint_ = b; } }
    for (auto& op : objs_) { Obj& o = *op; if (o.alive && o.vehicle != NO_BODY) phys_->vehicleInput(o.vehicle, o.vin[0], o.vin[1], o.vin[2], o.vin[3]); }
    // carried bodies follow their handle
    for (auto it = grabs_.begin(); it != grabs_.end();) { Grab& g = it->second;
      if (!phys_->dynamic(g.body)) { it = grabs_.erase(it); continue; }
      if (g.targetSet) { glm::vec3 bp; glm::quat bq; phys_->transform(g.body, bp, bq); glm::vec3 at = bp + bq * g.local, want = (g.target - at) * 12.f * g.strength;
        float mx = 30.f; if (glm::length(want) > mx) want = glm::normalize(want) * mx; phys_->setVelocity(g.body, want); phys_->setAngularVelocity(g.body, phys_->angularVelocity(g.body) * 0.85f); }
      ++it; }
    return;
  }
  // ---- after the physics step
  for (uint32_t j : phys_->takeBrokenJoints()) { auto it = jointOwner_.find(j); std::string jid = std::to_string(j);
    if (it != jointOwner_.end()) { Obj* c = get(it->second); if (c) { c->joint = NO_BODY; for (const std::string& who : {c->a.id, c->a.joint.a, c->a.joint.b}) if (Obj* x = get(who)) scripts_->call(x->beh, EV_JointBreak, 0, jid); } jointOwner_.erase(it); }
    else for (int b = 0; b < scripts_->count(); b++) scripts_->call(b, EV_JointBreak, 0, jid); }
  // ragdolls: the model's bones where their bodies are
  for (auto& op : objs_) { Obj& o = *op; if (!o.alive || o.rag.empty()) continue;
    o.p = phys_->position(o.rag[0].body) + o.ragFrom; glm::mat4 IW = glm::inverse(world(o)); o.anim.fixed.clear();
    for (auto& r : o.rag) { glm::vec3 bp; glm::quat bq; phys_->transform(r.body, bp, bq); o.anim.fixed.push_back({r.node, IW * glm::translate(glm::mat4(1), bp) * glm::mat4_cast(bq) * r.offset}); }
    o.lastWorld = world(o); }
  // the driver sits in the car (the ear and world.player.position go with it)
  if (Obj* v = driving_.empty() ? nullptr : get(driving_)) { glm::mat4 VW = rigid(world(*v)); phys_->teleport(player_, glm::vec3(VW[3]) + glm::vec3(0, 0.3f, 0), glm::quat(1, 0, 0, 0)); phys_->setVelocity(player_, v->body != NO_BODY ? phys_->velocity(v->body) : glm::vec3(0));
    if (glm::vec3(VW[3]).y < level_.world.killZ) { exitVehicle(); respawnPlayer(); } }
  else if (!driving_.empty()) exitVehicle();
  if (dead_ && respawnAt_ >= 0 && time_ >= respawnAt_) { respawnAt_ = -1; dead_ = false; health_ = maxHealth_; respawnPlayer(); }
  // decals: gone with their actor, fading at the end of their life
  decals_.erase(std::remove_if(decals_.begin(), decals_.end(), [&](const Decal& d) { return (!d.actor.empty() && !get(d.actor)) || (d.life > 0 && time_ - d.born > d.life); }), decals_.end());
}
void Game::drawGameplay(Frame& f) const {
  for (auto& op : objs_) { const Obj& o = *op; if (!o.alive || o.vehicle == NO_BODY || !o.visible || o.a.vehicle.wheelMesh.empty()) continue;
    auto st = phys_->vehicleState(o.vehicle, isModelFile(o.a.vehicle.wheelMesh)); glm::mat4 B = rigid(world(o)); for (auto& w : st.wheels) addWheelDraw(f, project_, o.a, B, w, 0); }
  const GpuMesh& plane = primitive("Plane");
  for (auto& d : decals_) { DrawItem it; it.mesh = &plane; glm::mat4 base(1);
    if (d.body != NO_BODY) { glm::vec3 bp; glm::quat bq; phys_->transform(d.body, bp, bq); base = glm::translate(glm::mat4(1), bp) * glm::mat4_cast(bq); }
    else if (!d.actor.empty()) { const Obj* a = get(d.actor); if (!a) continue; base = rigid(world(*a)); }
    it.model = base * d.m; it.tex[0] = d.tex ? d.tex->id : 0; it.color = d.color; it.rough = d.rough; it.metal = 0; it.blend = true; it.castShadow = false; it.decal = true;
    it.opacity = d.life > 0 ? glm::clamp((float)(d.life - (time_ - d.born)) / std::min(1.5f, d.life * 0.3f), 0.f, 1.f) : 1.f; if (it.opacity <= 0.001f) continue;
    it.opacity = std::min(it.opacity, 0.998f); f.draws.push_back(it); }
}

/* ------------------------------------------------------------------ scripts: world.raycast, actor.attachTo, ... */
std::string Game::gameplay(const std::string& op, const std::string& a, const std::string& b) {
  if (!phys_) return "";
  auto J = [](const std::string& s) { json j = json::parse(s, nullptr, false); return j.is_discarded() ? json::object() : j; };
  if (op == "raycast") { json j = J(a); glm::vec3 from(0), to(0); if (!v3(j.value("from", json()), from) || !v3(j.value("to", json()), to)) return "";
    std::vector<uint32_t> ig; if (j.value("ignorePlayer", false) && player_ != NO_BODY) ig.push_back(player_);
    if (j.contains("ignore") && j["ignore"].is_array()) for (auto& x : j["ignore"]) if (x.is_string()) { std::string id = x; if (id == "@player") { if (player_ != NO_BODY) ig.push_back(player_); continue; }
        if (Obj* o = get(id)) { if (o->body != NO_BODY) ig.push_back(o->body); for (auto& r : o->rag) ig.push_back(r.body); } }
    if (!driving_.empty() && player_ != NO_BODY) ig.push_back(player_);
    Physics::Hit h; if (!phys_->rayCastIgnoring(from, to, h, ig)) return "";
    json r = {{"p", ja(h.point)}, {"n", ja(h.normal)}, {"d", glm::length(to - from) * h.fraction}, {"b", h.body}, {"pl", h.userData == PLAYER_TAG}};
    if (Obj* o = objOfBody(h.body)) { r["a"] = o->a.id; if (h.userData & RAG_TAG) for (auto& p : o->rag) if (p.body == h.body) { ModelRef m = model(o->a.mesh); if (m && p.node < (int)m->nodes.size()) r["bone"] = m->nodes[p.node].name; } }
    return r.dump(); }
  if (op == "damage") { json t = J(a); std::string id = t.is_string() ? t.get<std::string>() : ""; json info = J(b); float amount = info.value("amount", 0.f); info.erase("amount"); if (!id.empty()) damage(id, amount, info); return ""; }
  if (op == "radial") { json j = J(a); glm::vec3 c(0); if (!v3(j.value("center", json()), c)) return "[]"; float R = std::max(0.01f, j.value("radius", 0.f)), amount = j.value("amount", 0.f), imp = j.value("impulse", 0.f); bool fall = j.value("falloff", true);
    std::string from = j.value("from", std::string()), type = j.value("type", std::string("explosion")); json hit = json::array(); std::set<Obj*> done;
    auto reach = [&](const glm::vec3& p) { float d = glm::length(p - c); if (d > R) return -1.f; Physics::Hit h; if (phys_->rayCast(c, p, h, true) && h.fraction < 0.98f && glm::length(h.point - p) > 0.3f) return -1.f; return fall ? 1 - d / R : 1.f; };   // walls shield
    for (uint32_t body : phys_->sphereBodies(c, R)) { if (body == player_) continue; Obj* o = objOfBody(body); glm::vec3 bp = phys_->position(body); float k = reach(bp); if (k < 0) continue;
      if (imp > 0 && phys_->dynamic(body)) { glm::vec3 d = bp - c; d = glm::length(d) > 1e-3f ? glm::normalize(d + glm::vec3(0, 0.3f, 0)) : AY; float m = phys_->mass(body), f = imp * k * (o && !o->rag.empty() ? m / std::max(1.f, o->a.physics.mass >= 20 ? o->a.physics.mass : 70.f) : 1.f); phys_->addImpulse(body, d * f); }
      if (!o || done.count(o)) continue; done.insert(o); if (amount != 0) { json info = {{"type", type}, {"point", ja(bp)}, {"direction", ja(glm::normalize(bp - c + glm::vec3(1e-5f)))}}; if (!from.empty()) info["from"] = from; damage(o->a.id, amount * k, info); } hit.push_back(o->a.id); }
    if (player_ != NO_BODY && driving_.empty()) { glm::vec3 pp = phys_->position(player_); float k = reach(pp); if (k >= 0) { glm::vec3 d = glm::normalize(pp - c + glm::vec3(0, 0.5f, 0));
        if (imp > 0) phys_->setVelocity(player_, phys_->velocity(player_) + d * imp * k / 70.f);
        if (amount != 0) { json info = {{"type", type}, {"point", ja(pp)}, {"direction", ja(d)}}; if (!from.empty()) info["from"] = from; damage("@player", amount * k, info); } hit.push_back("@player"); } }
    return hit.dump(); }
  if (op == "decal") { addDecal(J(a)); return ""; }
  if (op == "decalsClear") { decals_.clear(); return ""; }
  if (op == "joint") { json j = J(a); Obj* A = get(j.value("a", std::string())); if (!A) return ""; uint32_t ba = A->body; if (!A->rag.empty()) ba = A->rag[0].body; if (ba == NO_BODY) return "";
    std::string bid = j.value("b", std::string()); Obj* B = bid.empty() || bid == "@world" ? nullptr : get(bid); uint32_t bb = B ? (B->rag.empty() ? B->body : B->rag[0].body) : NO_BODY;
    Physics::JointDesc jd; std::string k = lower(j.value("kind", std::string("Hinge")));
    jd.type = k == "fixed" ? Physics::JFixed : k == "ballsocket" || k == "ball" || k == "point" ? Physics::JBall : k == "slider" || k == "prismatic" ? Physics::JSlider : k == "distance" || k == "rope" ? Physics::JDistance : k == "cone" ? Physics::JCone : Physics::JHinge;
    jd.a = ba; jd.b = bb; jd.pivot = glm::vec3(rigid(world(*A))[3]); v3(j.value("pivot", json()), jd.pivot); v3(j.value("axis", json()), jd.axis); v3(j.value("normal", json()), jd.normal);
    jd.pivotB = B ? glm::vec3(rigid(world(*B))[3]) : jd.pivot; v3(j.value("pivotB", json()), jd.pivotB);
    jd.limits = j.contains("min") || j.contains("max") || j.value("limits", false); jd.min = j.value("min", jd.type == Physics::JHinge ? -90.f : 0.f); jd.max = j.value("max", jd.type == Physics::JHinge ? 90.f : 1.f);
    jd.motorSpeed = j.value("motorSpeed", 0.f); jd.motorForce = j.value("motorForce", 0.f); jd.friction = j.value("friction", 0.f); jd.breakForce = j.value("breakForce", 0.f); jd.spring = j.value("spring", 0.f);
    jd.swing = j.value("swing", 45.f); jd.twistMin = j.value("twistMin", -30.f); jd.twistMax = j.value("twistMax", 30.f);
    uint32_t id = phys_->addJoint(jd); return id == NO_BODY ? "" : std::to_string(id); }
  if (op == "jointGet") { uint32_t id = (uint32_t)std::strtoul(a.c_str(), nullptr, 10); if (!phys_->jointAlive(id)) return ""; return json{{"value", phys_->jointValue(id)}}.dump(); }
  if (op == "jointSet") { uint32_t id = (uint32_t)std::strtoul(a.c_str(), nullptr, 10); json j = J(b);
    if (j.value("remove", false)) { phys_->removeJoint(id); jointOwner_.erase(id); return ""; }
    if (j.contains("target")) phys_->jointTarget(id, j.value("target", 0.f), j.value("motorForce", 0.f)); else phys_->jointMotor(id, j.value("motorSpeed", 0.f), j.value("motorForce", 0.f)); return ""; }
  if (op == "actorJoint") { Obj* o = get(a); return o && o->joint != NO_BODY && phys_->jointAlive(o->joint) ? std::to_string(o->joint) : ""; }
  if (op == "grab") { json j = J(a); uint32_t body = NO_BODY; glm::vec3 p(0); bool hasP = v3(j.value("point", json()), p);
    if (j.contains("body") && j["body"].is_number()) body = j["body"].get<uint32_t>();
    else if (Obj* o = get(j.value("actor", std::string()))) { if (!o->rag.empty()) { body = o->rag[0].body; float bd = 1e9f; if (hasP) for (auto& r : o->rag) { float dd = glm::length(phys_->position(r.body) - p); if (dd < bd) { bd = dd; body = r.body; } } } else body = o->body; }
    if (!phys_->dynamic(body)) return "";
    glm::vec3 bp; glm::quat bq; phys_->transform(body, bp, bq); if (!hasP) p = bp;
    Grab g; g.body = body; g.local = glm::inverse(bq) * (p - bp); g.strength = j.value("strength", 1.f); int id = nextGrab_++; grabs_[id] = g; return std::to_string(id); }
  if (op == "grabMove") { auto it = grabs_.find(std::atoi(a.c_str())); if (it == grabs_.end()) return ""; json j = J(b); glm::vec3 t; if (v3(j.value("target", json()), t)) { it->second.target = t; it->second.targetSet = true; }
    if (j.contains("strength") && j["strength"].is_number()) it->second.strength = glm::clamp(j["strength"].get<float>(), 0.05f, 10.f); return ""; }
  if (op == "grabGet") return grabs_.count(std::atoi(a.c_str())) ? "1" : "";
  if (op == "release") { auto it = grabs_.find(std::atoi(a.c_str())); if (it == grabs_.end()) return ""; json j = J(b); glm::vec3 v;
    if (v3(j.value("velocity", json()), v)) { Obj* o = objOfBody(it->second.body); if (o && !o->rag.empty()) for (auto& r : o->rag) phys_->setVelocity(r.body, v); else phys_->setVelocity(it->second.body, v); }
    grabs_.erase(it); return ""; }
  // ---- one actor
  Obj* o = get(a); if (!o) return "";
  if (op == "attach") return attach(*o, J(b));
  if (op == "detach") { detach(*o, J(b)); return ""; }
  if (op == "parent") return o->a.parent;
  if (op == "impulse") { json j = J(b); glm::vec3 imp(0), p(0); if (!v3(j.value("impulse", json()), imp)) return ""; bool hp = v3(j.value("point", json()), p);
    if (!o->rag.empty()) { ragdoll(*o, imp, p, hp); return ""; } if (o->body != NO_BODY && o->motion == Motion::Dynamic) { if (hp) phys_->addImpulseAt(o->body, imp, p); else phys_->addImpulse(o->body, imp); } return ""; }
  if (op == "ragdoll") { json j = J(b); glm::vec3 imp(0), p(0); v3(j.value("impulse", json()), imp); bool hp = v3(j.value("point", json()), p); return ragdoll(*o, imp, p, hp) ? "1" : "0"; }
  if (op == "ragdollState") return o->rag.empty() ? "0" : "1";
  if (op == "vehicleMake") { json j = J(b); VehicleProps& v = o->a.vehicle; v.enabled = true;
    auto f = [&](const char* k, float& d) { if (j.contains(k) && j[k].is_number()) d = j[k].get<float>(); };
    f("torque", v.torque); f("maxRpm", v.maxRpm); f("maxSteer", v.maxSteer); f("wheelRadius", v.wheelRadius); f("wheelWidth", v.wheelWidth); f("travel", v.travel); f("stiffness", v.stiffness); f("axleInset", v.axleInset); f("camDistance", v.camDistance);
    if (j.contains("drive") && j["drive"].is_string()) { std::string d = lower(j["drive"]); v.drive = d == "rear" ? 0 : d == "front" ? 1 : 2; }
    if (j.contains("wheelMesh") && j["wheelMesh"].is_string()) v.wheelMesh = j["wheelMesh"]; if (j.contains("wheelMaterial") && j["wheelMaterial"].is_string()) v.wheelMaterial = j["wheelMaterial"];
    if (j.contains("enterable") && j["enterable"].is_boolean()) v.enterable = j["enterable"];
    if (j.contains("mass") && j["mass"].is_number()) o->a.physics.mass = j["mass"].get<float>();
    if (o->vehicle != NO_BODY) { phys_->removeVehicle(o->vehicle); o->vehicle = NO_BODY; }
    o->a.physics.simulate = true; if (o->body != NO_BODY) { phys_->remove(o->body); o->body = NO_BODY; } addBody(*o); addVehicle(*o); return o->vehicle != NO_BODY ? "1" : "0"; }
  if (op == "vehicle") { if (o->vehicle == NO_BODY) return ""; json j = J(b);
    if (j.value("enter", false)) return enterVehicle(*o) ? "1" : "";
    if (j.value("exit", false)) { if (driving_ == o->a.id) exitVehicle(); return "1"; }
    if (j.contains("input") && j["input"].is_array() && j["input"].size() == 4) { if (driving_ != o->a.id) for (int i = 0; i < 4; i++) o->vin[i] = j["input"][i].is_number() ? j["input"][i].get<float>() : 0.f; return "1"; }
    auto st = phys_->vehicleState(o->vehicle); return json{{"speed", st.speed}, {"rpm", st.rpm}, {"gear", st.gear}, {"grounded", st.grounded}, {"driver", driving_ == o->a.id}}.dump(); }
  return "";
}
}
