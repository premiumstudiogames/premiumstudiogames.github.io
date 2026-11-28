#include "foliage.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <map>
#include <cmath>
#include <algorithm>

namespace pge {
uint32_t foliageRand(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s ? s : (s = 0x9e3779b9u); }
static uint64_t g_serial = 1;
Foliage::Foliage() : serial(g_serial++) {}
int Foliage::count() const { int n = 0; for (auto& t : types) n += (int)t.inst.size(); return n; }
glm::mat4 FoliageType::matrix(const Inst& i) const {
  glm::quat tilt(1, 0, 0, 0); glm::vec3 n = glm::normalize(i.n);
  if (alignToNormal && n.y < 0.9999f) { glm::vec3 ax = glm::cross(glm::vec3(0, 1, 0), n); float l = glm::length(ax); if (l > 1e-5f) tilt = glm::angleAxis(std::acos(glm::clamp(n.y, -1.f, 1.f)), ax / l); }
  return glm::translate(glm::mat4(1), i.p) * glm::mat4_cast(tilt * glm::angleAxis(i.yaw, glm::vec3(0, 1, 0))) * glm::scale(glm::mat4(1), glm::vec3(i.scale));
}
int Foliage::erase(int type, const glm::vec3& c, float r) {
  int n = 0; for (int k = 0; k < (int)types.size(); k++) { if (type >= 0 && k != type) continue; auto& v = types[k].inst; size_t before = v.size();
    v.erase(std::remove_if(v.begin(), v.end(), [&](const FoliageType::Inst& i) { float dx = i.p.x - c.x, dz = i.p.z - c.z; return dx * dx + dz * dz < r * r; }), v.end()); n += (int)(before - v.size()); }
  if (n) changed(); return n;
}
nlohmann::json Foliage::toJson() const {
  nlohmann::json a = nlohmann::json::array();
  for (auto& t : types) { nlohmann::json in = nlohmann::json::array(); auto q = [](float x) { return std::round(x * 1000) / 1000; };
    for (auto& i : t.inst) { in.push_back(q(i.p.x)); in.push_back(q(i.p.y)); in.push_back(q(i.p.z)); in.push_back(q(i.yaw)); in.push_back(q(i.scale)); in.push_back(q(i.n.x)); in.push_back(q(i.n.z)); }
    a.push_back({{"mesh", t.mesh}, {"material", t.material}, {"density", t.density}, {"scaleMin", t.scaleMin}, {"scaleMax", t.scaleMax}, {"cullDistance", t.cullDistance}, {"wind", t.wind}, {"sink", t.sink},
                 {"align", t.alignToNormal}, {"randomYaw", t.randomYaw}, {"collision", t.collision}, {"shadows", t.castShadow}, {"inst", in}}); }
  return {{"types", a}};
}
bool Foliage::fromJson(const nlohmann::json& j) {
  types.clear(); if (!j.is_object() || !j.contains("types") || !j["types"].is_array()) return false;
  for (auto& x : j["types"]) { if (!x.is_object()) continue; FoliageType t;
    t.mesh = x.value("mesh", t.mesh); t.material = x.value("material", std::string()); t.density = x.value("density", t.density); t.scaleMin = x.value("scaleMin", t.scaleMin); t.scaleMax = x.value("scaleMax", t.scaleMax);
    t.cullDistance = x.value("cullDistance", t.cullDistance); t.wind = x.value("wind", 0.f); t.sink = x.value("sink", 0.f); t.alignToNormal = x.value("align", false); t.randomYaw = x.value("randomYaw", true);
    t.collision = x.value("collision", false); t.castShadow = x.value("shadows", true);
    if (x.contains("inst") && x["inst"].is_array()) { auto& in = x["inst"]; for (size_t k = 0; k + 6 < in.size() + 0 && k + 7 <= in.size(); k += 7) {
        auto f = [&](size_t o) { return in[k + o].is_number() ? in[k + o].get<float>() : 0.f; }; FoliageType::Inst i; i.p = {f(0), f(1), f(2)}; i.yaw = f(3); i.scale = f(4);
        float nx = f(5), nz = f(6); i.n = {nx, std::sqrt(std::max(0.f, 1 - nx * nx - nz * nz)), nz}; t.inst.push_back(i); } }
    types.push_back(t); }
  changed(); return true;
}
const std::vector<FoliageCell>& foliageCells(const Foliage& f) {
  struct C { int version = -1; std::vector<FoliageCell> cells; }; static std::map<uint64_t, C> cache;
  C& c = cache[f.serial]; if (c.version == f.version) return c.cells; c.version = f.version; c.cells.clear();
  const float S = 32;
  for (int t = 0; t < (int)f.types.size(); t++) { std::map<std::pair<int, int>, size_t> at; const FoliageType& ty = f.types[t];
    for (auto& i : ty.inst) { std::pair<int, int> key((int)std::floor(i.p.x / S), (int)std::floor(i.p.z / S)); auto it = at.find(key);
      if (it == at.end()) { FoliageCell cell; cell.type = t; cell.mats = std::make_shared<std::vector<glm::mat4>>(); cell.mn = glm::vec3(1e9f); cell.mx = glm::vec3(-1e9f); at[key] = c.cells.size(); c.cells.push_back(cell); it = at.find(key); }
      FoliageCell& cell = c.cells[it->second]; if (cell.mats->size() >= 8192) { FoliageCell more; more.type = t; more.mats = std::make_shared<std::vector<glm::mat4>>(); more.mn = glm::vec3(1e9f); more.mx = glm::vec3(-1e9f); it->second = c.cells.size(); c.cells.push_back(more); }
      FoliageCell& cc = c.cells[it->second]; cc.mats->push_back(ty.matrix(i)); float r = 3 * i.scale; cc.mn = glm::min(cc.mn, i.p - glm::vec3(r)); cc.mx = glm::max(cc.mx, i.p + glm::vec3(r)); } }
  for (auto& cell : c.cells) cell.center = (cell.mn + cell.mx) * 0.5f;
  return c.cells;
}
}
