// Characters' animation in the running game (like Unreal's Animation Blueprint): every model's animation moves on, a
// character walking (moveTo, or moved by a script) blends idle / walk / run by its speed, layers play on part of the
// body (shooting while walking), feet stand on uneven ground (foot IK), hands reach for places and heads look at things.
#include "game.h"
#include "log.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cctype>
#include <map>

using json = nlohmann::json;
namespace pge {

static std::string low(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }
static int findAnim(const Model& m, std::initializer_list<const char*> keys) {
  for (const char* k : keys) for (size_t i = 0; i < m.anims.size(); i++) if (low(m.anims[i].name).find(k) != std::string::npos) return (int)i; return -1;
}
static int findNode(const Model& m, const std::string& name) {
  for (size_t i = 0; i < m.nodes.size(); i++) if (m.nodes[i].name == name) return (int)i;
  std::string n = low(name); for (size_t i = 0; i < m.nodes.size(); i++) if (low(m.nodes[i].name) == n) return (int)i; return -1;
}
static bool isJoint(const Model& m, int n) { for (auto& s : m.skins) for (int j : s.joints) if (j == n) return true; return false; }
static int jointParent(const Model& m, int n) { int p = n >= 0 ? m.nodes[n].parent : -1; while (p >= 0 && !isJoint(m, p)) p = m.nodes[p].parent; return p; }

// a character's legs: the two feet (left: +X of the model, which faces +Z), their knees and hips; found once per model
struct LegRig { int foot[2] = {-1, -1}, knee[2] = {-1, -1}, thigh[2] = {-1, -1}, hip = -1; bool ok = false; };
static const LegRig& legsOf(const Model& m) {
  static std::map<const Model*, LegRig> cache; auto it = cache.find(&m); if (it != cache.end()) return it->second; LegRig r;
  if (!m.skins.empty()) { ModelPose rest; poseModel(m, nullptr, rest); std::vector<int> feet;
    for (size_t i = 0; i < m.nodes.size(); i++) { std::string n = low(m.nodes[i].name); if ((n.find("foot") != std::string::npos || n.find("ankle") != std::string::npos) && n.find("toe") == std::string::npos && n.find("end") == std::string::npos && isJoint(m, (int)i)) feet.push_back((int)i); }
    if (feet.size() >= 2) { int a = feet[0], b = feet[0]; for (int f : feet) { if (rest.world[f][3].x > rest.world[a][3].x) a = f; if (rest.world[f][3].x < rest.world[b][3].x) b = f; }
      if (a != b) { r.foot[0] = a; r.foot[1] = b; for (int s = 0; s < 2; s++) { r.knee[s] = jointParent(m, r.foot[s]); r.thigh[s] = jointParent(m, r.knee[s]); }
        if (r.thigh[0] >= 0 && r.thigh[1] >= 0) { // the hips: where both legs meet
          for (int p = jointParent(m, r.thigh[0]); p >= 0; p = jointParent(m, p)) { bool both = false; for (int q = r.thigh[1]; q >= 0; q = jointParent(m, q)) if (q == p) both = true; if (both) { r.hip = p; break; } }
          r.ok = r.hip >= 0; } } } }
  return cache[&m] = r;
}

void Game::solveIK(const Model& m, AnimPlay& an, const glm::mat4& W, const std::vector<Obj::IkReq>& req, bool feet, float floorY, std::vector<uint32_t> ignore) {
  an.ik.clear(); an.hip = -1; an.hipDrop = 0; if (!an.fixed.empty() || m.skins.empty()) return;
  glm::mat4 IW = glm::inverse(W); bool needPose = feet || !req.empty(); if (!needPose) return;
  ModelPose p; poseModel(m, &an, p);
  // feet: each foot as high above the ground under it as it is above the floor in the animation; the hips go down for the lower one
  const LegRig& L = legsOf(m);
  if (feet && L.ok) { float off[2] = {0, 0}; glm::vec3 fw[2]; bool hit[2] = {false, false};
    for (int s = 0; s < 2; s++) { fw[s] = glm::vec3(W * p.world[L.foot[s]][3]); Physics::Hit h;
      if (phys_->rayCastIgnoring(fw[s] + glm::vec3(0, 0.55f, 0), fw[s] - glm::vec3(0, 0.6f, 0), h, ignore) && h.normal.y > 0.5f) { off[s] = glm::clamp(h.point.y - floorY, -0.45f, 0.45f); hit[s] = true; } }
    if (hit[0] || hit[1]) { float drop = std::min({off[0], off[1], 0.f}); glm::vec3 down = glm::mat3(IW) * glm::vec3(0, drop, 0); an.hip = L.hip; an.hipDrop = down.y;
      glm::vec3 fwd = glm::normalize(glm::mat3(W) * glm::vec3(0, 0, 1) + glm::vec3(1e-6f));
      for (int s = 0; s < 2; s++) if (std::abs(off[s] - drop) > 0.005f || drop != 0) { AnimPlay::Ik k; k.end = L.foot[s]; k.mid = L.knee[s]; k.root = L.thigh[s];
          k.target = glm::vec3(IW * glm::vec4(fw[s] + glm::vec3(0, off[s], 0), 1)); k.pole = glm::vec3(IW * glm::vec4(glm::vec3(W * p.world[L.knee[s]][3]) + fwd, 1)); k.hasPole = true; an.ik.push_back(k); } } }
  // hands and heads the scripts asked for
  for (auto& r : req) { int e = findNode(m, r.bone); if (e < 0) continue; glm::vec3 t = r.target; if (!r.actor.empty()) { if (r.actor == "@player") t = playerPos() + glm::vec3(0, 0.6f, 0); else if (Obj* x = get(r.actor)) t = glm::vec3(world(*x)[3]); else continue; }
    AnimPlay::Ik k; k.end = e; k.weight = r.weight; k.aim = r.aim; k.maxAngle = r.maxAngle; k.target = glm::vec3(IW * glm::vec4(t, 1));
    if (!r.aim) { k.mid = jointParent(m, e); k.root = jointParent(m, k.mid); if (k.mid < 0 || k.root < 0) continue;
      k.pole = glm::vec3(IW * glm::vec4(glm::vec3(W * p.world[k.mid][3]) - glm::mat3(W) * glm::vec3(0, 0, 1) * 0.5f - glm::vec3(0, 0.3f, 0), 1)); k.hasPole = true; }   // elbows back and down
    an.ik.push_back(k); }
}

void Game::animFrame(float dt) {
  for (size_t i = 0; i < objs_.size(); i++) { Obj& o = *objs_[i]; if (!o.alive || o.a.type != "StaticMesh" || !isModelFile(o.a.mesh)) continue; ModelRef m = model(o.a.mesh); if (!m || !m->ok()) continue;
    // how fast it moves (walking, or moved by a script)
    glm::vec3 d = o.p - o.prevP; d.y = 0; float sp = dt > 0 ? glm::length(d) / dt : 0; if (sp > 40) sp = 0; o.prevP = o.p; o.moveSpeed += (sp - o.moveSpeed) * std::min(1.f, dt * 8);
    // idle / walk / run by itself (a script that plays an animation turns this off; self.autoAnimate = true turns it on)
    if (o.autoAnim && o.rag.empty() && !m->anims.empty()) { int wi = findAnim(*m, {"walk", "jog"}), ri = findAnim(*m, {"run", "sprint"}), ii = findAnim(*m, {"idle", "stand", "breath"});
      if (wi >= 0) { bool moving = o.moveSpeed > 0.15f || o.walk.active;
        if (moving || o.autoOn) { std::vector<std::pair<int, float>> s; int rest = !o.a.anim.empty() ? m->anim(o.a.anim) : ii; if (rest < 0) rest = ii;
          if (rest >= 0) s.push_back({rest, 0.f}); s.push_back({wi, 1.5f}); if (ri >= 0 && ri != wi) s.push_back({ri, 4.5f});
          if (!moving && o.moveSpeed < 0.05f) { o.autoOn = false; if (rest >= 0) o.anim.play(rest, true, o.a.animSpeed, 0.25f); }
          else { o.autoOn = true; o.anim.setSpace(s, o.moveSpeed, 0.2f); float top = s.back().second; o.anim.speed = o.moveSpeed > top ? glm::clamp(o.moveSpeed / top, 1.f, 2.f) : 1.f; } } } }
    if (o.anim.anim >= 0 || o.anim.layer >= 0 || !o.anim.space.empty()) {
      bool end = o.anim.advance(*m, dt);
      if (end && o.anim.anim >= 0 && o.anim.anim < (int)m->anims.size()) scripts_->call(o.beh, EV_AnimEnd, 0, m->anims[o.anim.anim].name);
      if (o.anim.layerEnded) { o.anim.layerEnded = false; if (o.anim.layer >= 0 && o.anim.layer < (int)m->anims.size()) scripts_->call(o.beh, EV_AnimEnd, 0, m->anims[o.anim.layer].name); } }
    // feet on the ground, hands and heads where asked
    bool feet = o.footIK && o.rag.empty() && o.a.parent.empty() && o.visible;
    if ((feet && legsOf(*m).ok) || !o.ikReq.empty()) { std::vector<uint32_t> ig; if (o.body != NO_BODY) ig.push_back(o.body); if (player_ != NO_BODY) ig.push_back(player_);
      float floorY = glm::vec3(world(o) * glm::vec4(0, m->shape.bmin.y, 0, 1)).y; solveIK(*m, o.anim, world(o), o.ikReq, feet, floorY, ig); }
    else { o.anim.ik.clear(); o.anim.hip = -1; } }
}

// scripts: self.setBlendSpace, self.playLayer, self.setIK, self.lookAt, self.autoAnimate, self.footIK
std::string Game::animOp(Obj& o, const Model& m, const std::string& op, const std::string& arg) {
  json j = json::parse(arg.empty() ? "{}" : arg, nullptr, false); if (j.is_discarded()) j = json::object();
  auto missing = [&](const std::string& n) { std::string names; for (auto& x : m.anims) names += (names.empty() ? "" : ", ") + x.name; logMsg("warning: " + o.a.name + ": no animation \"" + n + "\" in " + o.a.mesh + (names.empty() ? "" : " - it has: " + names), LogKind::Warn); };
  if (op == "space") { std::vector<std::pair<int, float>> s; if (j.contains("anims") && j["anims"].is_array()) for (auto& e : j["anims"]) if (e.is_array() && e.size() == 2 && e[0].is_string() && e[1].is_number()) { int ai = m.anim(e[0]); if (ai < 0) { missing(e[0]); return "0"; } s.push_back({ai, e[1].get<float>()}); }
    if (j.contains("anims") && j["anims"].is_object()) for (auto& [k, v] : j["anims"].items()) if (v.is_number()) { int ai = m.anim(k); if (ai < 0) { missing(k); return "0"; } s.push_back({ai, v.get<float>()}); }   // { Idle: 0, Walk: 1.5 }
    if (s.empty()) return "0"; o.autoAnim = false; o.anim.setSpace(s, j.value("value", 0.f), j.value("blend", 0.2f)); if (j.contains("speed")) o.anim.speed = j.value("speed", 1.f); return "1"; }
  if (op == "spaceValue") { o.anim.spaceValue = (float)std::atof(arg.c_str()); return "1"; }
  if (op == "layer") { std::string n = j.value("name", std::string()); if (n.empty()) { o.anim.stopLayer(j.value("blend", 0.2f)); return "1"; } int ai = m.anim(n); if (ai < 0) { missing(n); return "0"; }
    std::string bone = j.value("bone", std::string()); int bi = bone.empty() ? -1 : findNode(m, bone); if (!bone.empty() && bi < 0) { logMsg("warning: " + o.a.name + ": no bone \"" + bone + "\"", LogKind::Warn); return "0"; }
    o.anim.playLayer(ai, bi, j.value("loop", false), j.value("speed", 1.f), j.value("weight", 1.f), j.value("blend", 0.15f)); return "1"; }
  if (op == "layerStop") { o.anim.stopLayer(j.value("blend", 0.2f)); return "1"; }
  if (op == "ik" || op == "look") { std::string bone = j.value("bone", std::string(op == "look" ? "Head" : "")); if (findNode(m, bone) < 0) { if (op == "look") { for (const char* h : {"head", "Head", "neck"}) if (findNode(m, h) >= 0) { bone = h; break; } } if (findNode(m, bone) < 0) return "0"; }
    o.ikReq.erase(std::remove_if(o.ikReq.begin(), o.ikReq.end(), [&](auto& r) { return low(r.bone) == low(bone); }), o.ikReq.end());
    if (j.value("off", false)) return "1";
    Obj::IkReq r; r.bone = bone; r.aim = op == "look"; r.weight = j.value("weight", 1.f); r.maxAngle = j.value("maxAngle", 70.f);
    if (j.contains("actor") && j["actor"].is_string()) r.actor = j["actor"]; else if (j.contains("target") && j["target"].is_array() && j["target"].size() == 3) r.target = {j["target"][0].get<float>(), j["target"][1].get<float>(), j["target"][2].get<float>()}; else return "0";
    o.ikReq.push_back(r); return "1"; }
  if (op == "ikClear") { o.ikReq.clear(); return "1"; }
  if (op == "auto") { if (arg == "1" || arg == "0") { o.autoAnim = arg == "1"; o.autoOn = false; } return o.autoAnim ? "1" : "0"; }
  if (op == "footIK") { if (arg == "1" || arg == "0") o.footIK = arg == "1"; return o.footIK ? "1" : "0"; }
  if (op == "spaceState") { int a, b; float w; o.anim.spaceAt(m, a, b, w); return json{{"a", a >= 0 ? m.anims[a].name : ""}, {"b", b >= 0 ? m.anims[b].name : ""}, {"w", w}, {"value", o.anim.spaceValue}, {"layer", o.anim.layer >= 0 ? m.anims[o.anim.layer].name : ""}, {"layerWeight", o.anim.layerWeight}, {"ik", o.anim.ik.size()}, {"auto", o.autoOn}}.dump(); }
  return "";
}
}
