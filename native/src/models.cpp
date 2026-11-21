#include "models.h"
#include "voxel.h"
#include "renderer.h"
#include "gl.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>

namespace pge {
using json = nlohmann::json;

// the block world's vertex (same layout as its chunks, so the same shader draws both)
struct MV { float x, y, z, u, v; uint8_t r, g, b, ao; int8_t nx, ny, nz, pad; uint16_t tile; uint8_t flags, pad2; };
static_assert(sizeof(MV) == 32, "vertex layout");
struct MMesh { std::vector<MV> v; std::vector<uint32_t> i; glm::vec3 mn{1e9f}, mx{-1e9f}; };

static void upload(Models::Gpu& g, const MMesh& m, bool stream) {
  g.count = (int)m.i.size(); g.mn = m.mn; g.mx = m.mx;
  if (!glGenBuffers) return;
  if (m.i.empty()) { if (g.vao) { glDeleteBuffers(1, &g.vbo); glDeleteBuffers(1, &g.ebo); glDeleteVertexArrays(1, &g.vao); } g.vao = g.vbo = g.ebo = 0; g.count = 0; return; }
  if (!g.vao) { glGenVertexArrays(1, &g.vao); glGenBuffers(1, &g.vbo); glGenBuffers(1, &g.ebo);
    glBindVertexArray(g.vao); glBindBuffer(GL_ARRAY_BUFFER, g.vbo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ebo);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MV), (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(MV), (void*)12);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(MV), (void*)20);
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_BYTE, GL_TRUE, sizeof(MV), (void*)24);
    glEnableVertexAttribArray(4); glVertexAttribIPointer(4, 2, GL_UNSIGNED_SHORT, sizeof(MV), (void*)28);
    glBindVertexArray(0); }
  glBindVertexArray(g.vao); glBindBuffer(GL_ARRAY_BUFFER, g.vbo); glBufferData(GL_ARRAY_BUFFER, m.v.size() * sizeof(MV), m.v.data(), stream ? GL_STREAM_DRAW : GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ebo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, m.i.size() * 4, m.i.data(), stream ? GL_STREAM_DRAW : GL_STATIC_DRAW); glBindVertexArray(0);
}
static void freeGpu(Models::Gpu& g) { if (g.vao && glDeleteVertexArrays) { glDeleteBuffers(1, &g.vbo); glDeleteBuffers(1, &g.ebo); glDeleteVertexArrays(1, &g.vao); } g = Models::Gpu(); }
Models::Def::~Def() { for (auto& p : parts) freeGpu(p.gpu); }
Models::Models() {}
Models::~Models() { clear(); }
void Models::clear() { inst_.clear(); defs_.clear(); ps_.clear(); freeGpu(pgpu_); nextId_ = 0; }

glm::mat4 eulerXYZ(const glm::vec3& r) {
  return glm::rotate(glm::mat4(1), r.x, glm::vec3(1, 0, 0)) * glm::rotate(glm::mat4(1), r.y, glm::vec3(0, 1, 0)) * glm::rotate(glm::mat4(1), r.z, glm::vec3(0, 0, 1));
}
static bool vec3Of(const json& j, glm::vec3& out) {
  if (j.is_array() && j.size() >= 3 && j[0].is_number() && j[1].is_number() && j[2].is_number()) { out = {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()}; return std::isfinite(out.x + out.y + out.z); }
  if (j.is_object() && j.contains("x")) { out = {j.value("x", 0.f), j.value("y", 0.f), j.value("z", 0.f)}; return std::isfinite(out.x + out.y + out.z); }
  if (j.is_number()) { float s = j.get<float>(); out = glm::vec3(s); return std::isfinite(s); }
  return false;
}
static bool colorOf(const json& j, glm::vec3& c) {
  if (j.is_array() && j.size() >= 3 && j[0].is_number()) { c = {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()}; return true; }
  if (!j.is_string()) return false; std::string s = j.get<std::string>(); if (s.empty() || s[0] != '#') return false; s = s.substr(1);
  if (s.size() == 3) { std::string e; for (char ch : s) { e += ch; e += ch; } s = e; }
  if (s.size() != 6 && s.size() != 8) return false;
  unsigned long n = std::strtoul(s.substr(0, 6).c_str(), nullptr, 16); c = {((n >> 16) & 255) / 255.f, ((n >> 8) & 255) / 255.f, (n & 255) / 255.f}; return true;
}
static MV vert(const glm::vec3& P, float u, float v, const glm::vec3& col, const glm::vec3& n, int tile, int flags) {
  MV q{}; q.x = P.x; q.y = P.y; q.z = P.z; q.u = u; q.v = v; glm::vec3 cc = glm::clamp(col * 0.5f, 0.f, 1.f);
  q.r = (uint8_t)std::lround(cc.r * 255); q.g = (uint8_t)std::lround(cc.g * 255); q.b = (uint8_t)std::lround(cc.b * 255); q.ao = 255;
  glm::vec3 nn = glm::length(n) > 1e-6f ? glm::normalize(n) : glm::vec3(0, 1, 0); q.nx = (int8_t)std::lround(nn.x * 127); q.ny = (int8_t)std::lround(nn.y * 127); q.nz = (int8_t)std::lround(nn.z * 127);
  q.tile = tile < 0 ? 0xffff : (uint16_t)tile; q.flags = (uint8_t)flags; return q;
}
static const glm::ivec3 FD[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
static void axesOf(int d, int& ua, int& va) { int ax = d / 2; if (ax == 0) { ua = 2; va = 1; } else if (ax == 1) { ua = 0; va = 2; } else { ua = 0; va = 1; } }
// one box: centre c, size s, turned by R (about its centre); faces from the block world's order (+x, -x, +y, -y, +z, -z)
static void addBox(MMesh& m, const glm::vec3& c, const glm::vec3& s, const glm::mat3& R, const glm::vec3 col[6], const int tile[6], int flags, bool repeat) {
  for (int d = 0; d < 6; d++) { glm::ivec3 n = FD[d]; int ua, va; axesOf(d, ua, va);
    glm::vec3 p0(-0.5f); if (n.x > 0) p0.x = 0.5f; if (n.y > 0) p0.y = 0.5f; if (n.z > 0) p0.z = 0.5f;
    glm::vec3 eu(0), ev(0); eu[ua] = 1; ev[va] = 1; float su = repeat ? s[ua] : 1, sv = repeat ? s[va] : 1;
    uint32_t b = (uint32_t)m.v.size(); const float U[4] = {0, 1, 0, 1}, V[4] = {0, 0, 1, 1};
    glm::vec3 wn = R * glm::vec3(n);
    for (int k = 0; k < 4; k++) { glm::vec3 P = c + R * ((p0 + eu * U[k] + ev * V[k]) * s); m.v.push_back(vert(P, U[k] * su, V[k] * sv, col[d], wn, tile[d], flags | (tile[d] >= 0 ? 16 : 0))); m.mn = glm::min(m.mn, P); m.mx = glm::max(m.mx, P); }
    bool flip = glm::dot(glm::cross(eu, ev), glm::vec3(n)) < 0; uint32_t tri[6] = {0, 1, 3, 0, 3, 2}; if (flip) { std::swap(tri[1], tri[2]); std::swap(tri[4], tri[5]); }
    for (uint32_t t : tri) m.i.push_back(b + t); }
}
// colours and tiles of a box's six faces from: color, tile (n or {top, bottom, side, px, nx, py, ny, pz, nz}), block (its faces)
static std::string facesOf(const json& b, VoxelWorld* vox, glm::vec3 col[6], int tile[6], int& glow) {
  glm::vec3 base(1); for (int i = 0; i < 6; i++) { col[i] = base; tile[i] = -1; }
  if (b.contains("block") && b["block"].is_number()) { int id = b["block"].get<int>(); if (!vox) return "block faces need the block world (voxels.create)";
    const BlockType& bt = vox->block(id); for (int d = 0; d < 6; d++) { int f = d == 2 ? 0 : d == 3 ? 2 : 1; col[d] = bt.color[f]; tile[d] = bt.tile[f]; } glow = (int)std::lround(glm::clamp(bt.glow / 2.f, 0.f, 1.f) * 15); }
  if (b.contains("color")) { glm::vec3 c; if (!colorOf(b["color"], c)) return "color: '#rrggbb' or [r, g, b]"; for (int i = 0; i < 6; i++) col[i] = c; }
  if (b.contains("colors") && b["colors"].is_object()) { static const char* K[6] = {"px", "nx", "py", "ny", "pz", "nz"};
    for (int i = 0; i < 6; i++) { glm::vec3 c; const char* alt = i == 2 ? "top" : i == 3 ? "bottom" : "side"; if (b["colors"].contains(K[i]) && colorOf(b["colors"][K[i]], c)) col[i] = c; else if (b["colors"].contains(alt) && colorOf(b["colors"][alt], c)) col[i] = c; } }
  if (b.contains("tile")) { auto& t = b["tile"];
    if (t.is_number()) for (int i = 0; i < 6; i++) tile[i] = t.get<int>();
    else if (t.is_object()) { static const char* K[6] = {"px", "nx", "py", "ny", "pz", "nz"};
      for (int i = 0; i < 6; i++) { const char* alt = i == 2 ? "top" : i == 3 ? "bottom" : "side"; if (t.contains(K[i]) && t[K[i]].is_number()) tile[i] = t[K[i]].get<int>(); else if (t.contains(alt) && t[alt].is_number()) tile[i] = t[alt].get<int>(); } }
    else if (!t.is_null()) return "tile: a number or { top, side, bottom } (or px, nx, py, ny, pz, nz)"; }
  if (b.contains("glow") && b["glow"].is_number()) glow = (int)std::lround(glm::clamp(b["glow"].get<float>() / 2.f, 0.f, 1.f) * 15);
  return "";
}

std::string Models::define(const std::string& name, const json& def) {
  if (name.empty()) return "a model needs a name";
  if (!def.is_object()) return "a model is { parts: { name: { boxes: [...], pivot, parent }, ... } } or { boxes: [...] }";
  json parts = def.contains("parts") ? def["parts"] : json{{"body", def}};
  if (!parts.is_object() || parts.empty()) return "parts: { name: { ... } }";
  if (parts.size() > 128) return "at most 128 parts";
  auto D = std::make_shared<Def>();
  for (auto& [pn, pj] : parts.items()) { Part p; p.name = pn; D->byName[pn] = (int)D->parts.size(); D->parts.push_back(p); }
  for (auto& [pn, pj] : parts.items()) {
    Part& p = D->parts[D->byName[pn]]; MMesh m;
    if (!pj.is_object()) return "part '" + pn + "' is an object";
    if (pj.contains("parent") && pj["parent"].is_string() && !pj["parent"].get<std::string>().empty()) { auto it = D->byName.find(pj["parent"]); if (it == D->byName.end()) return "part '" + pn + "': no part '" + pj["parent"].get<std::string>() + "'"; p.parent = it->second; }
    if (pj.contains("pivot") && !vec3Of(pj["pivot"], p.pivot)) return "part '" + pn + "': pivot is [x, y, z]";
    if (pj.contains("rotation") && !vec3Of(pj["rotation"], p.rot)) return "part '" + pn + "': rotation is [x, y, z] (radians)";
    int glow = 0;
    if (pj.contains("boxes")) { if (!pj["boxes"].is_array()) return "part '" + pn + "': boxes is a list";
      for (auto& b : pj["boxes"]) { if (!b.is_object()) return "part '" + pn + "': each box is an object";
        glm::vec3 s(1), c(0), r(0); if (b.contains("size") && !vec3Of(b["size"], s)) return "part '" + pn + "': size is [x, y, z] (metres)";
        if (b.contains("pos") && !vec3Of(b["pos"], c)) return "part '" + pn + "': pos is [x, y, z]"; if (b.contains("position") && !vec3Of(b["position"], c)) return "part '" + pn + "': position is [x, y, z]";
        if (b.contains("rotation") && !vec3Of(b["rotation"], r)) return "part '" + pn + "': a box's rotation is [x, y, z] (radians)";
        glm::vec3 col[6]; int tile[6]; int gl = glow; std::string e = facesOf(b, vox_, col, tile, gl); if (!e.empty()) return "part '" + pn + "': " + e;
        addBox(m, c, glm::max(s, glm::vec3(0.001f)), glm::mat3(eulerXYZ(r)), col, tile, gl, b.value("repeat", false)); } }
    if (pj.contains("mesh")) { auto& M = pj["mesh"]; if (!M.is_object() || !M.contains("positions") || !M["positions"].is_array()) return "part '" + pn + "': mesh is { positions, indices?, normals?, colors?, uvs?, tile? }";
      auto& P = M["positions"]; size_t nv = P.size() / 3; if (nv < 3 || nv > 200000) return "part '" + pn + "': 3 .. 200000 vertices";
      std::vector<uint32_t> idx; if (M.contains("indices") && M["indices"].is_array()) for (auto& i : M["indices"]) { uint32_t k = i.get<uint32_t>(); if (k >= nv) return "part '" + pn + "': an index past the vertices"; idx.push_back(k); }
      else for (uint32_t i = 0; i < nv; i++) idx.push_back(i);
      if (idx.size() % 3) return "part '" + pn + "': indices make triangles (a multiple of 3)";
      int tile = M.value("tile", -1); glm::vec3 base(1); if (M.contains("color")) colorOf(M["color"], base);
      auto num = [&](const char* k, size_t i, float d) { return M.contains(k) && M[k].is_array() && i < M[k].size() && M[k][i].is_number() ? M[k][i].get<float>() : d; };
      bool hasN = M.contains("normals") && M["normals"].is_array() && M["normals"].size() >= nv * 3; int gl = (int)std::lround(glm::clamp(M.value("glow", 0.f) / 2.f, 0.f, 1.f) * 15);
      // flat normals (each triangle its own corners) unless normals are given
      for (size_t t = 0; t < idx.size(); t += 3) { glm::vec3 q[3]; for (int k = 0; k < 3; k++) { size_t i = idx[t + k]; q[k] = {P[i * 3].get<float>(), P[i * 3 + 1].get<float>(), P[i * 3 + 2].get<float>()}; }
        glm::vec3 fn = glm::cross(q[1] - q[0], q[2] - q[0]);
        for (int k = 0; k < 3; k++) { size_t i = idx[t + k]; glm::vec3 n = hasN ? glm::vec3(num("normals", i * 3, 0), num("normals", i * 3 + 1, 1), num("normals", i * 3 + 2, 0)) : fn;
          glm::vec3 c = base * glm::vec3(num("colors", i * 3, 1), num("colors", i * 3 + 1, 1), num("colors", i * 3 + 2, 1));
          m.v.push_back(vert(q[k], num("uvs", i * 2, 0), num("uvs", i * 2 + 1, 0), c, n, tile, gl | (tile >= 0 ? 16 : 0))); m.i.push_back((uint32_t)m.v.size() - 1); m.mn = glm::min(m.mn, q[k]); m.mx = glm::max(m.mx, q[k]); } } }
    upload(p.gpu, m, false);
  }
  // a parent before its children, and no loops
  for (size_t i = 0; i < D->parts.size(); i++) { int g = 0; for (int q = D->parts[i].parent; q >= 0; q = D->parts[q].parent) if (++g > 128 || q == (int)i) return "part '" + D->parts[i].name + "' is its own parent"; }
  defs_[name] = D;
  for (auto& [id, in] : inst_) if (in.model == name) { in.def = D; in.pose.resize(D->parts.size()); }   // instances take the new look
  return "";
}

static void poseFrom(const json& o, glm::vec3& p, glm::vec3& r, glm::vec3& s, bool& vis, std::string& why) {
  if (o.contains("position") && !vec3Of(o["position"], p)) why = "position is [x, y, z]";
  if (o.contains("rotation") && !vec3Of(o["rotation"], r)) why = "rotation is [x, y, z] (radians)";
  if (o.contains("scale") && !vec3Of(o["scale"], s)) why = "scale is a number or [x, y, z]";
  if (o.contains("visible") && o["visible"].is_boolean()) vis = o["visible"].get<bool>();
}
std::string Models::spawn(const std::string& model, const json& o, std::string& id) {
  auto it = defs_.find(model); if (it == defs_.end()) return "no model '" + model + "' (models.define first)";
  if (inst_.size() >= 5000) return "too many models (5000)";
  Inst in; in.model = model; in.def = it->second; in.pose.resize(in.def->parts.size());
  id = "m" + std::to_string(++nextId_);
  inst_[id] = in; std::string e = set(id, o); if (!e.empty()) { inst_.erase(id); return e; }
  return "";
}
std::string Models::set(const std::string& id, const json& o) {
  auto it = inst_.find(id); if (it == inst_.end()) return "no model '" + id + "'"; Inst& in = it->second;
  if (!o.is_object()) return ""; std::string why;
  poseFrom(o, in.p, in.r, in.s, in.visible, why);
  if (o.contains("tint")) { glm::vec3 c; if (colorOf(o["tint"], c)) in.tint = c; else why = "tint: '#rrggbb' or [r, g, b]"; }
  if (o.contains("shadow") && o["shadow"].is_boolean()) in.shadow = o["shadow"].get<bool>();
  if (o.contains("parts") && o["parts"].is_object()) for (auto& [pn, pj] : o["parts"].items()) {
    auto pi = in.def->byName.find(pn); if (pi == in.def->byName.end()) { why = "no part '" + pn + "' in '" + in.model + "'"; continue; }
    PartPose& pp = in.pose[pi->second]; if (pj.is_object()) poseFrom(pj, pp.p, pp.r, pp.s, pp.visible, why); }
  return why;
}
json Models::get(const std::string& id) const {
  auto it = inst_.find(id); if (it == inst_.end()) return nullptr; const Inst& in = it->second;
  json parts = json::object(); for (size_t i = 0; i < in.pose.size(); i++) { auto& p = in.pose[i]; parts[in.def->parts[i].name] = {{"position", {p.p.x, p.p.y, p.p.z}}, {"rotation", {p.r.x, p.r.y, p.r.z}}, {"scale", {p.s.x, p.s.y, p.s.z}}, {"visible", p.visible}}; }
  return {{"model", in.model}, {"position", {in.p.x, in.p.y, in.p.z}}, {"rotation", {in.r.x, in.r.y, in.r.z}}, {"scale", {in.s.x, in.s.y, in.s.z}}, {"visible", in.visible}, {"parts", parts}};
}
void Models::remove(const std::string& id) { inst_.erase(id); }
std::vector<std::string> Models::parts(const std::string& id) const { std::vector<std::string> r; auto it = inst_.find(id); if (it != inst_.end()) for (auto& p : it->second.def->parts) r.push_back(p.name); return r; }

// a part: its pivot (from its parent's), its rest rotation, then the pose (moved, turned, scaled about the pivot)
glm::mat4 Models::partMatrix(const Inst& in, int i) const {
  const Part& P = in.def->parts[i]; const PartPose& Q = in.pose[i];
  glm::mat4 L = glm::translate(glm::mat4(1), P.pivot + Q.p) * eulerXYZ(P.rot + Q.r) * glm::scale(glm::mat4(1), Q.s);
  return P.parent >= 0 ? partMatrix(in, P.parent) * L : glm::translate(glm::mat4(1), in.p) * eulerXYZ(in.r) * glm::scale(glm::mat4(1), in.s) * L;
}
glm::mat4 Models::partWorld(const std::string& id, const std::string& part, bool* ok) const {
  if (ok) *ok = false; auto it = inst_.find(id); if (it == inst_.end()) return glm::mat4(1);
  auto pi = it->second.def->byName.find(part); if (pi == it->second.def->byName.end()) return glm::mat4(1);
  if (ok) *ok = true; return partMatrix(it->second, pi->second);
}
int Models::drawCount() const { int n = 0;
  for (auto& [id, in] : inst_) if (in.visible) for (size_t i = 0; i < in.pose.size(); i++) { if (!in.def->parts[i].gpu.count) continue; bool h = false;
      for (int q = (int)i; q >= 0; q = in.def->parts[q].parent) if (!in.pose[q].visible) { h = true; break; } if (!h) n++; }
  return n; }

void Models::draw(Frame& f) const {
  for (auto& [id, in] : inst_) { if (!in.visible) continue;
    std::vector<char> hidden(in.pose.size(), 0);
    for (size_t i = 0; i < in.pose.size(); i++) { for (int q = (int)i; q >= 0; q = in.def->parts[q].parent) if (!in.pose[q].visible) { hidden[i] = 1; break; } }
    for (size_t i = 0; i < in.pose.size(); i++) { const Gpu& g = in.def->parts[i].gpu; if (hidden[i] || !g.count) continue;
      VoxelDraw v; v.vao = g.vao; v.count = g.count; v.model = partMatrix(in, (int)i); v.tint = in.tint; v.castShadow = in.shadow;
      glm::vec3 lo(1e9f), hi(-1e9f); for (int k = 0; k < 8; k++) { glm::vec3 c((k & 1) ? g.mx.x : g.mn.x, (k & 2) ? g.mx.y : g.mn.y, (k & 4) ? g.mx.z : g.mn.z); glm::vec3 w = glm::vec3(v.model * glm::vec4(c, 1)); lo = glm::min(lo, w); hi = glm::max(hi, w); }
      v.mn = lo; v.mx = hi; f.voxels.push_back(v); } }
  if (pgpu_.count) { VoxelDraw v; v.vao = pgpu_.vao; v.count = pgpu_.count; v.mn = pgpu_.mn; v.mx = pgpu_.mx; f.voxels.push_back(v); }
}

/* ------------------------------------------------------------------ particles */
float Models::rnd() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return (rng_ & 0xffffff) / 16777215.f; }
std::string Models::emit(const json& o) {
  if (!o.is_object()) return "particles.emit({ position, count, ... })";
  glm::vec3 pos(0); if (!o.contains("position") || !vec3Of(o["position"], pos)) return "position is [x, y, z]";
  int count = std::clamp(o.value("count", 10), 0, 2000);
  auto range = [&](const char* k, float d0, float d1, float& a, float& b) { a = d0; b = d1; if (!o.contains(k)) return; auto& j = o[k];
    if (j.is_number()) a = b = j.get<float>(); else if (j.is_array() && j.size() == 2 && j[0].is_number()) { a = j[0].get<float>(); b = j[1].get<float>(); } };
  float s0, s1, l0, l1, z0, z1, sp0, sp1; range("speed", 1, 3, s0, s1); range("life", 0.6f, 1.2f, l0, l1); range("size", 0.08f, 0.14f, z0, z1); range("spin", 0, 6, sp0, sp1);
  glm::vec3 dir(0, 1, 0); if (o.contains("direction")) vec3Of(o["direction"], dir); if (glm::length(dir) < 1e-5f) dir = {0, 1, 0}; dir = glm::normalize(dir);
  glm::vec3 base(0); if (o.contains("velocity")) vec3Of(o["velocity"], base);
  glm::vec3 area(0); if (o.contains("area")) vec3Of(o["area"], area);
  float spread = std::clamp(o.value("spread", 1.f), 0.f, 1.f), grav = o.value("gravity", -9.81f), drag = std::max(0.f, o.value("drag", 0.4f)), bounce = std::clamp(o.value("bounce", 0.25f), 0.f, 1.f);
  bool collide = o.value("collide", true), shrink = o.value("shrink", true);
  int glow = (int)std::lround(glm::clamp(o.value("glow", 0.f) / 2.f, 0.f, 1.f) * 15);
  // what they look like: a block's faces, an atlas tile, a colour or one of several
  std::vector<glm::vec3> cols; int tile = o.value("tile", -1);
  if (o.contains("block") && o["block"].is_number()) { if (!vox_) return "block needs the block world (voxels.create)"; const BlockType& bt = vox_->block(o["block"].get<int>()); cols.push_back(bt.color[1]); if (tile < 0) tile = bt.tile[1]; if (!glow) glow = (int)std::lround(glm::clamp(bt.glow / 2.f, 0.f, 1.f) * 15); }
  if (o.contains("color")) { glm::vec3 c; if (!colorOf(o["color"], c)) return "color: '#rrggbb' or [r, g, b]"; cols = {cols.empty() ? c : cols[0] * c}; }
  if (o.contains("colors") && o["colors"].is_array()) { cols.clear(); for (auto& c : o["colors"]) { glm::vec3 v; if (colorOf(c, v)) cols.push_back(v); } }
  if (cols.empty()) cols.push_back(glm::vec3(1));
  if (ps_.size() + count > 6000) { size_t drop = std::min(ps_.size(), ps_.size() + count - 6000); ps_.erase(ps_.begin(), ps_.begin() + drop); }   // the oldest go
  for (int i = 0; i < count; i++) {
    Particle p{}; p.p = pos + (glm::vec3(rnd(), rnd(), rnd()) - 0.5f) * area;
    glm::vec3 r(rnd() * 2 - 1, rnd() * 2 - 1, rnd() * 2 - 1); if (glm::length(r) < 1e-4f) r = {0, 1, 0}; r = glm::normalize(r);
    glm::vec3 d = glm::normalize(glm::mix(dir, r, spread) + glm::vec3(1e-5f)); p.v = base + d * (s0 + (s1 - s0) * rnd());
    p.col = cols[(size_t)(rnd() * cols.size()) % cols.size()] * (0.9f + 0.2f * rnd()); p.size = z0 + (z1 - z0) * rnd(); p.life = std::max(0.02f, l0 + (l1 - l0) * rnd());
    p.spin = sp0 + (sp1 - sp0) * rnd(); p.ang = rnd() * 6.283f; p.axis = glm::normalize(glm::vec3(rnd() - 0.5f, rnd() - 0.5f, rnd() - 0.5f) + glm::vec3(0, 1e-4f, 0));
    p.bounce = bounce; p.drag = drag; p.grav = grav; p.tile = tile; p.glow = glow; p.collide = collide; p.shrink = shrink;
    ps_.push_back(p); }
  return "";
}
void Models::clearParticles() { ps_.clear(); MMesh m; upload(pgpu_, m, true); }
void Models::update(float dt) {
  dt = std::min(dt, 0.05f);
  auto solid = [&](const glm::vec3& q) { if (!vox_) return false; BlockInfo b = vox_->getBlock(q); return b.id > 0 && vox_->block(b.id).solid; };
  for (auto& p : ps_) { p.age += dt; if (p.age >= p.life) continue;
    if (!p.rest) { p.v.y += p.grav * dt; p.v *= std::max(0.f, 1 - p.drag * dt); p.ang += p.spin * dt;
      glm::vec3 np = p.p + p.v * dt;
      if (p.collide && vox_) {
        float h = p.size * 0.5f;
        if (solid({np.x, np.y - h, np.z}) && p.v.y < 0) { np.y = p.p.y; p.v.y = -p.v.y * p.bounce; p.v.x *= 0.7f; p.v.z *= 0.7f; p.spin *= 0.6f; if (std::abs(p.v.y) < 0.4f) { p.v.y = 0; if (glm::length(p.v) < 0.15f) p.rest = true; } }
        if (solid({np.x + (p.v.x > 0 ? h : -h), np.y, p.p.z})) { np.x = p.p.x; p.v.x = -p.v.x * p.bounce; }
        if (solid({p.p.x, np.y, np.z + (p.v.z > 0 ? h : -h)})) { np.z = p.p.z; p.v.z = -p.v.z * p.bounce; }
      }
      p.p = np; } }
  ps_.erase(std::remove_if(ps_.begin(), ps_.end(), [](const Particle& p) { return p.age >= p.life; }), ps_.end());
  MMesh m; m.v.reserve(ps_.size() * 24); m.i.reserve(ps_.size() * 36);
  for (auto& p : ps_) { float t = p.age / p.life, s = p.shrink ? p.size * std::min(1.f, (1 - t) * 3) : p.size;   // shrink in the last third
    glm::vec3 col[6]; int tl[6]; for (int i = 0; i < 6; i++) { col[i] = p.col * (i == 2 ? 1.f : i == 3 ? 0.6f : 0.82f); tl[i] = p.tile; }
    glm::mat3 R = glm::mat3(glm::rotate(glm::mat4(1), p.ang, p.axis));
    addBox(m, p.p, glm::vec3(std::max(0.002f, s)), R, col, tl, p.glow, false); }
  upload(pgpu_, m, true);
}
}
