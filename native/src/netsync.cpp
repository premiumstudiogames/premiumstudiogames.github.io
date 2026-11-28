// Online games kept the same on every computer (like Unreal's replication). The host decides; whoever "owns" an actor moves
// it (its physics runs there) and tells the others, who follow it smoothly (their copy is kinematic). Physics actors, cars,
// ragdolls and actors marked Replicates are kept the same; actors spawned with physics appear everywhere; each player is
// seen by the others as a character; damage to another player reaches them; decals show for everyone. A player who gets
// in a car (or picks something up) becomes its owner. Messages of the engine start with \x01: scripts never see them.
#include "game.h"
#include "log.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>

using json = nlohmann::json;
namespace pge {

static const char TAG = '\x01';
static json r3(const glm::vec3& v) { auto q = [](float x) { return std::round(x * 1000) / 1000; }; return json::array({q(v.x), q(v.y), q(v.z)}); }
static json r4(const glm::quat& v) { auto q = [](float x) { return std::round(x * 10000) / 10000; }; return json::array({q(v.x), q(v.y), q(v.z), q(v.w)}); }
static int inum(const json& m, const char* k, int d) { auto it = m.find(k); return it != m.end() && it->is_number() ? it->get<int>() : d; }
static float num(const json& x, float d = 0) { return x.is_number() ? x.get<float>() : d; }   // (a number that wasn't one - NaN - comes as null)
static glm::vec3 g3(const json& a, glm::vec3 d = glm::vec3(0)) { return a.is_array() && a.size() == 3 ? glm::vec3(num(a[0], d.x), num(a[1], d.y), num(a[2], d.z)) : d; }
static glm::quat g4(const json& a) { if (!(a.is_array() && a.size() == 4)) return glm::quat(1, 0, 0, 0); glm::quat q(num(a[3], 1), num(a[0]), num(a[1]), num(a[2])); return glm::length(q) > 1e-6f ? glm::normalize(q) : glm::quat(1, 0, 0, 0); }
static json m12(const glm::mat4& m) { json a = json::array(); for (int c = 0; c < 4; c++) for (int r = 0; r < 3; r++) a.push_back(std::round(m[c][r] * 10000) / 10000); return a; }
static glm::mat4 g12(const json& a) { glm::mat4 m(1); if (a.is_array() && a.size() == 12) for (int c = 0; c < 4; c++) for (int r = 0; r < 3; r++) m[c][r] = num(a[c * 3 + r], c == r ? 1.f : 0.f); return m; }

bool Game::netOn() const { return net_ && !net_->role().empty() && net_->connected() && net_->id() >= 0; }
int Game::netMe() const { return netOn() ? net_->id() : 0; }
bool Game::owns(const Obj& o) const { return !netOn() || o.owner == netMe(); }
bool Game::replicated(const Obj& o) const {
  if (o.remote >= 0 || o.welded || o.a.type != "StaticMesh") return false;
  return o.repl || o.a.physics.replicates || o.a.physics.simulate || o.a.vehicle.enabled || !o.rag.empty();
}
void Game::netSend(const json& m, int to, int except) {
  if (!netOn()) return; std::string d = std::string(1, TAG) + m.dump();
  if (net_->role() != "host") { net_->send(d, -1); return; }   // a player: to the host (it passes it on)
  if (to >= 0) { net_->send(d, to); return; } for (int p : net_->peers()) if (p != except) net_->send(d, p);
}

// what came: the engine's messages handled here, the rest kept for the scripts (net.receive())
void Game::netDrain() {
  if (!net_) return;
  for (auto& e : net_->take()) {
    if (e.type == "msg" && !e.data.empty() && e.data[0] == TAG) { json m = json::parse(e.data.substr(1), nullptr, false); if (m.is_object()) netHandle(m, inum(m, "f", e.id)); continue; }
    if (e.type == "join" && net_->role() == "host") netFull(e.id);
    if (e.type == "leave" && net_->role() == "host") {   // what they owned is the host's again
      json own = json::object(); for (auto& op : objs_) if (op->alive && op->owner == e.id) { op->owner = 0; own[op->a.id] = 0; } if (!own.empty()) netSend({{"k", "o"}, {"m", own}});
      netSend({{"k", "bye"}, {"id", e.id}}); }
    if (e.type == "leave") { auto it = avatars_.find(e.id); if (it != avatars_.end()) { std::string a = it->second; avatars_.erase(it); destroyActor(a); } }
    netInbox_.push_back(e); }
}
// a player who just joined: what was spawned, who owns what, where everything is
void Game::netFull(int to) {
  for (auto& op : objs_) { Obj& o = *op; if (!o.alive || !replicated(o)) continue;
    if (o.repl && o.a.id.rfind("net", 0) != 0) { json sp = {{"k", "sp"}, {"i", o.a.id}, {"mesh", o.a.mesh}, {"mat", o.a.material}, {"s", r3(o.s)}, {"n", o.a.name}, {"ph", o.a.physics.simulate}, {"veh", o.a.vehicle.enabled}, {"own", o.owner}};
      if (o.hasColor) sp["col"] = colorHex(o.color); netSend(sp, to); } }
  json own = json::object(); for (auto& op : objs_) if (op->alive && replicated(*op)) own[op->a.id] = op->owner; netSend({{"k", "o"}, {"m", own}}, to);
  json a = json::array(); for (auto& op : objs_) if (op->alive && replicated(*op) && owns(*op)) a.push_back(netSnap(*op)); if (!a.empty()) netSend({{"k", "s"}, {"a", a}}, to);
}
json Game::netSnap(const Obj& o) const {
  glm::quat q = quatYXZ(o.r); json a = {{"i", o.a.id}, {"p", r3(o.p)}, {"q", r4(q)}};
  if (o.body != NO_BODY && o.motion == Motion::Dynamic) a["v"] = r3(phys_->velocity(o.body));
  if (!o.visible) a["h"] = 1;
  if (!o.autoAnim && o.anim.anim >= 0) if (ModelRef m = model(o.a.mesh)) if (o.anim.anim < (int)m->anims.size()) { a["an"] = m->anims[o.anim.anim].name; a["at"] = std::round(o.anim.time * 100) / 100; }
  if (o.vehicle != NO_BODY) { auto st = phys_->vehicleState(o.vehicle, isModelFile(o.a.vehicle.wheelMesh)); json w = json::array(); for (auto& m : st.wheels) w.push_back(m12(m)); a["w"] = w; if (driving_ == o.a.id) a["d"] = 1; }
  if (!o.rag.empty()) { json r = json::array(); for (auto& f : o.anim.fixed) r.push_back(json::array({f.first, m12(f.second)})); a["rg"] = r; }
  return a;
}
void Game::netApply(Obj& o, const json& a) {
  o.repl = true; o.nt.p = g3(a.value("p", json()), o.p); o.nt.q = g4(a.value("q", json())); o.nt.v = g3(a.value("v", json())); o.nt.at = time_;
  o.visible = !a.contains("h"); o.netDriven = a.contains("d");
  if (a.contains("an") && a["an"].is_string()) if (ModelRef m = model(o.a.mesh)) { int ai = m->anim(a["an"]); if (ai >= 0) { o.autoAnim = false; if (ai != o.anim.anim) o.anim.play(ai, true, 1, 0.2f); float t = num(a.value("at", json(0))); if (std::abs(o.anim.time - t) > 0.3f) o.anim.time = t; } }
  if (a.contains("w") && a["w"].is_array()) { o.netWheels.clear(); for (auto& w : a["w"]) o.netWheels.push_back(g12(w)); }
  if (a.contains("rg") && a["rg"].is_array()) {   // a ragdoll moved by its owner: no physics here, its bones where they are there
    if (o.body != NO_BODY) { phys_->remove(o.body); o.body = NO_BODY; } o.walk.active = false; o.autoAnim = false;
    o.anim.fixed.clear(); for (auto& r : a["rg"]) if (r.is_array() && r.size() == 2) o.anim.fixed.push_back({(r[0].is_number() ? r[0].get<int>() : -1), g12(r[1])}); }
}
std::string Game::netAvatar(int id, const json& m) {
  auto it = avatars_.find(id); if (it != avatars_.end() && get(it->second)) return it->second;
  std::string mesh = m.value("m", std::string()); if (mesh.empty() || !model(mesh) || !model(mesh)->ok()) mesh = model("Characters/Mannequin.glb") && model("Characters/Mannequin.glb")->ok() ? "Characters/Mannequin.glb" : "Cylinder";
  Actor a = newActor("StaticMesh", "Player " + std::to_string(id + 1)); a.id = "netplayer" + std::to_string(id); a.mesh = mesh; if (isModelFile(mesh)) a.material.clear(); else a.s = {0.6f, 1.8f, 0.6f};
  a.tags.push_back("player"); a.physics.collision = true;
  auto ob = std::make_unique<Obj>(); ob->a = a; ob->p = a.p; ob->s = a.s; ob->remote = id; Obj& ref = *ob; byId_[a.id] = ob.get(); objs_.push_back(std::move(ob));
  size_t idx = objs_.size(); float k = 1; if (isModelFile(mesh)) { const MeshData& md = meshShape(mesh); float h = md.bmax.y - md.bmin.y; k = h > 1.0f && h < 2.6f ? 1.f : 1.8f / std::max(h, 1e-3f); ref.s = glm::vec3(k); }
  ref.body = phys_->addMesh(mesh, ref.s, ref.p, glm::quat(1, 0, 0, 0), Motion::Kinematic, 70, idx); ref.motion = Motion::Kinematic; ref.bodyScale = ref.s;
  avatars_[id] = a.id; return a.id;
}

void Game::netHandle(const json& m, int from) {
  std::string k = m.value("k", std::string()); bool host = net_->role() == "host";
  bool relay = host && inum(m, "f", -1) >= 0 && inum(m, "f", -1) != 0 && k != "req" && !(k == "dmg" && inum(m, "to", -1) == 0);
  if (relay) netSend(m, k == "dmg" ? inum(m, "to", -1) : -1, k == "dmg" ? -1 : from);   // a player's news goes on to the others
  fromNet_ = true;
  if (k == "s") { for (auto& a : m.value("a", json::array())) { Obj* o = get(a.value("i", std::string())); if (o && !owns(*o)) netApply(*o, a); } }
  else if (k == "p") { int id = inum(m, "id", -1); if (id >= 0 && id != netMe()) { Obj* o = get(netAvatar(id, m)); if (o) {
        o->nt.p = g3(m.value("p", json())) - glm::vec3(0, 0.9f, 0); o->nt.v = g3(m.value("v", json())); o->nt.q = glm::angleAxis(num(m.value("y", json(0))), glm::vec3(0, 1, 0)); o->nt.at = time_;
        o->visible = m.value("veh", std::string()).empty(); } } }
  else if (k == "o") { const json mm = m.value("m", json::object()); for (auto& [id, ow] : mm.items()) if (Obj* o = get(id)) o->owner = ow.is_number() ? ow.get<int>() : 0; }
  else if (k == "req" && host) { Obj* o = get(m.value("i", std::string())); int who = inum(m, "f", from);
    if (o && (o->owner == 0 || o->owner == who || std::find(net_->peers().begin(), net_->peers().end(), o->owner) == net_->peers().end()) && !(o->owner == 0 && driving_ == o->a.id) && !(o->owner != 0 && o->owner != who && o->netDriven)) {
      o->owner = who; netSend({{"k", "o"}, {"m", {{o->a.id, who}}}}); } }
  else if (k == "sp") { std::string id = m.value("i", std::string()); if (!id.empty() && !get(id)) { json o = {{"id", id}, {"name", m.value("n", std::string())}, {"scale", m.value("s", json::array({1, 1, 1}))}, {"physics", m.value("ph", false)}};
      if (m.contains("mat") && m["mat"].is_string() && !m["mat"].get<std::string>().empty()) o["material"] = m["mat"]; if (m.contains("col")) o["color"] = m["col"];
      if (m.contains("p")) o["position"] = m["p"]; if (m.value("veh", false)) o["vehicle"] = true;
      std::string nid = spawn(m.value("mesh", std::string("Cube")), o.dump()); if (Obj* x = get(nid)) { x->owner = inum(m, "own", from); x->repl = true; } } }
  else if (k == "x") { std::string id = m.value("i", std::string()); if (get(id)) destroyActor(id); }
  else if (k == "dmg") { if (inum(m, "to", -1) == netMe()) { json info = m.value("info", json::object()); damage("@player", num(m.value("amount", json(0))), info); } }
  else if (k == "dec") { json d = m.value("d", json::object()); addDecal(d); }
  else if (k == "bye") { int id = inum(m, "id", -1); auto it = avatars_.find(id); if (it != avatars_.end()) { std::string a = it->second; avatars_.erase(it); destroyActor(a); } }
  fromNet_ = false;
}

// who moves what: the owner's physics runs, the others' copy is kinematic and follows
void Game::netOwnership() {
  for (auto& op : objs_) { Obj& o = *op; if (!o.alive || !replicated(o) || o.body == NO_BODY) continue; bool mine = owns(o);
    bool natural = o.a.physics.simulate || o.a.vehicle.enabled;
    if (!mine && natural && !o.netKin) { o.netKin = true; if (o.vehicle != NO_BODY) { if (driving_ == o.a.id) exitVehicle(); phys_->removeVehicle(o.vehicle); o.vehicle = NO_BODY; }
      for (auto it = grabs_.begin(); it != grabs_.end();) { if (it->second.body == o.body) it = grabs_.erase(it); else ++it; }
      phys_->setMotion(o.body, Motion::Kinematic); o.motion = Motion::Kinematic; if (o.nt.at < 0) { o.nt.p = o.p; o.nt.q = quatYXZ(o.r); o.nt.at = time_; } }
    else if (mine && o.netKin) { o.netKin = false; phys_->setMotion(o.body, Motion::Dynamic); o.motion = Motion::Dynamic; phys_->setVelocity(o.body, o.nt.v); if (o.a.vehicle.enabled) addVehicle(o); } }
}

void Game::netFrame(float dt) {
  bool on = netOn();
  if (!on && netWas_) {   // the game ended: everything is ours again, the other players gone
    auto av = avatars_; avatars_.clear(); for (auto& [id, a] : av) destroyActor(a); for (auto& op : objs_) op->owner = 0; netOwnership(); }
  if (on && !netWas_ && net_->role() != "host") for (auto& op : objs_) op->owner = 0;   // joined: the host owns the level
  netWas_ = on; netDrain(); if (!on) return;
  netOwnership();
  if (!pendingEnter_.empty()) { Obj* v = get(pendingEnter_); if (!v) pendingEnter_.clear(); else if (owns(*v) && v->vehicle != NO_BODY) { pendingEnter_.clear(); enterVehicle(*v); } }
  // others' actors and characters follow what came, smoothly (a little ahead by their speed)
  float k = 1 - std::exp(-dt * 14);
  for (auto& op : objs_) { Obj& o = *op; if (!o.alive || o.nt.at < 0 || (o.remote < 0 && (owns(o) || !replicated(o)))) continue;
    glm::vec3 want = o.nt.p + o.nt.v * (float)std::min(0.25, time_ - o.nt.at); if (glm::length(want - o.p) > 8) o.p = want; else o.p = glm::mix(o.p, want, k);
    glm::quat q = glm::slerp(quatYXZ(o.r), o.nt.q, k); o.r = eulerYXZ(q);
    if (o.body != NO_BODY) phys_->moveKinematic(o.body, o.p, q); o.lastWorld = world(o); }
  // ours go out 20 times a second (what moved; everything once a second)
  if (time_ - netSent_ < 0.05) return; netSent_ = time_;
  json a = json::array(); bool all = std::fmod(time_, 1.0) < 0.05;
  for (auto& op : objs_) { Obj& o = *op; if (!o.alive || !replicated(o) || !owns(o)) continue; glm::quat q = quatYXZ(o.r);
    if (!all && glm::length(o.p - o.sentP) < 0.002f && std::abs(glm::dot(q, o.sentQ)) > 0.99999f && o.rag.empty() && o.vehicle == NO_BODY && time_ - o.sentAt < 1) continue;
    o.sentP = o.p; o.sentQ = q; o.sentAt = time_; a.push_back(netSnap(o)); }
  if (!a.empty()) netSend({{"k", "s"}, {"a", a}, {"f", netMe()}});
  if (player_ != NO_BODY || !driving_.empty()) { glm::vec3 p = playerPos(), v = player_ != NO_BODY ? phys_->velocity(player_) : glm::vec3(0); float y = mode_ == "ThirdPerson" && !pModel_.empty() ? charYaw_ : yaw_ + 3.14159265f;
    netSend({{"k", "p"}, {"id", netMe()}, {"f", netMe()}, {"p", r3(p)}, {"v", r3(v)}, {"y", y}, {"veh", driving_}, {"m", pModel_.empty() ? level_.world.playerModel : pModel_}}); }
}
}
