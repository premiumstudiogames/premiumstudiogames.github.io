#include "mesh.h"
#include "gl.h"
#include <map>
#include <unordered_map>
#include <cstring>
#include <algorithm>
#include <cmath>

namespace pge {
static const float PI = 3.14159265358979f;

static void quad(MeshData& m, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d) {
  glm::vec3 n = glm::normalize(glm::cross(b - a, c - a)); unsigned s = (unsigned)m.v.size();
  m.v.push_back({a, n, {0, 1}}); m.v.push_back({b, n, {1, 1}}); m.v.push_back({c, n, {1, 0}}); m.v.push_back({d, n, {0, 0}});
  m.i.insert(m.i.end(), {s, s + 1, s + 2, s, s + 2, s + 3});
}
static void tri(MeshData& m, glm::vec3 a, glm::vec3 b, glm::vec3 c) {
  glm::vec3 n = glm::normalize(glm::cross(b - a, c - a)); unsigned s = (unsigned)m.v.size();
  m.v.push_back({a, n, {0, 1}}); m.v.push_back({b, n, {1, 1}}); m.v.push_back({c, n, {0, 0}}); m.i.insert(m.i.end(), {s, s + 1, s + 2});
}
static void bounds(MeshData& m) { m.bmin = glm::vec3(1e9f); m.bmax = glm::vec3(-1e9f); for (auto& v : m.v) { m.bmin = glm::min(m.bmin, v.p); m.bmax = glm::max(m.bmax, v.p); } }

// a solid of revolution around Y: profile = (radius, y) points from bottom to top, smooth normals
static void lathe(MeshData& m, const std::vector<glm::vec2>& prof, int seg, bool capBottom, bool capTop) {
  unsigned base = (unsigned)m.v.size(); int rows = (int)prof.size();
  for (int r = 0; r < rows; r++) {
    glm::vec2 a = prof[std::max(0, r - 1)], b = prof[std::min(rows - 1, r + 1)]; glm::vec2 t = glm::normalize(b - a + glm::vec2(1e-6f)); glm::vec2 n2(t.y, -t.x);
    for (int s = 0; s <= seg; s++) { float u = (float)s / seg * 2 * PI, c = std::cos(u), sn = std::sin(u);
      m.v.push_back({{prof[r].x * sn, prof[r].y, prof[r].x * c}, glm::normalize(glm::vec3(n2.x * sn, n2.y, n2.x * c) + glm::vec3(1e-6f)), {(float)s / seg, 0.5f - prof[r].y}}); }
  }
  for (int r = 0; r < rows - 1; r++) for (int s = 0; s < seg; s++) { unsigned a = base + r * (seg + 1) + s, b = a + seg + 1; m.i.insert(m.i.end(), {a, a + 1, b, b, a + 1, b + 1}); }
  auto cap = [&](float y, float rad, bool up) { unsigned c = (unsigned)m.v.size(); glm::vec3 n(0, up ? 1.f : -1.f, 0); m.v.push_back({{0, y, 0}, n, {0.5f, 0.5f}});
    for (int s = 0; s <= seg; s++) { float u = (float)s / seg * 2 * PI; m.v.push_back({{rad * std::sin(u), y, rad * std::cos(u)}, n, {0.5f + rad * std::sin(u), 0.5f - rad * std::cos(u)}}); }
    for (int s = 0; s < seg; s++) { if (up) m.i.insert(m.i.end(), {c, c + 1 + s, c + 2 + s}); else m.i.insert(m.i.end(), {c, c + 2 + s, c + 1 + s}); } };
  if (capBottom && prof.front().x > 0) cap(prof.front().y, prof.front().x, false);
  if (capTop && prof.back().x > 0) cap(prof.back().y, prof.back().x, true);
}

MeshData makePrimitive(const std::string& name) {
  MeshData m; const float h = 0.5f;
  if (name == "Sphere") {
    int seg = 32, rings = 16; for (int r = 0; r <= rings; r++) { float v = (float)r / rings * PI; for (int s = 0; s <= seg; s++) { float u = (float)s / seg * 2 * PI;
      glm::vec3 n(std::sin(v) * std::sin(u), -std::cos(v), std::sin(v) * std::cos(u)); m.v.push_back({n * 0.5f, n, {(float)s / seg, 1.f - (float)r / rings}}); } }
    for (int r = 0; r < rings; r++) for (int s = 0; s < seg; s++) { unsigned a = r * (seg + 1) + s, b = a + seg + 1; m.i.insert(m.i.end(), {a, a + 1, b, b, a + 1, b + 1}); }
  } else if (name == "Cylinder") { lathe(m, {{h, -h}, {h, h}}, 32, true, true);
  } else if (name == "Cone") { lathe(m, {{h, -h}, {0.0001f, h}}, 32, true, false);
  } else if (name == "Capsule") { std::vector<glm::vec2> p; float r = 0.35f, hh = 0.55f; for (int i = 0; i <= 8; i++) { float a = -PI / 2 + (float)i / 8 * PI / 2; p.push_back({r * std::cos(a) + 1e-4f, -hh + r * std::sin(a)}); }
    for (int i = 0; i <= 8; i++) { float a = (float)i / 8 * PI / 2; p.push_back({r * std::cos(a) + 1e-4f, hh + r * std::sin(a)}); } lathe(m, p, 24, false, false);
  } else if (name == "Torus") { int seg = 48, ts = 16; float R = 0.4f, r = 0.12f;
    for (int i = 0; i <= seg; i++) { float u = (float)i / seg * 2 * PI; for (int j = 0; j <= ts; j++) { float v = (float)j / ts * 2 * PI;
      glm::vec3 c(R * std::cos(u), R * std::sin(u), 0), n(std::cos(v) * std::cos(u), std::cos(v) * std::sin(u), std::sin(v)); m.v.push_back({c + n * r, n, {(float)i / seg, (float)j / ts}}); } }
    for (int i = 0; i < seg; i++) for (int j = 0; j < ts; j++) { unsigned a = i * (ts + 1) + j, b = a + ts + 1; m.i.insert(m.i.end(), {a, b, a + 1, b, b + 1, a + 1}); }
  } else if (name == "Plane") { quad(m, {-h, 0, h}, {h, 0, h}, {h, 0, -h}, {-h, 0, -h});
  } else if (name == "Wedge") {   // right triangle (x,y) extruded along z: slope faces +x/+y
    glm::vec3 A(-h, -h, h), B(h, -h, h), C(-h, h, h), A2(-h, -h, -h), B2(h, -h, -h), C2(-h, h, -h);
    tri(m, A, B, C); tri(m, A2, C2, B2); quad(m, A2, B2, B, A); quad(m, A, C, C2, A2); quad(m, B, B2, C2, C);
  } else {   // Cube
    quad(m, {-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}); quad(m, {h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h});
    quad(m, {h, -h, h}, {h, -h, -h}, {h, h, -h}, {h, h, h}); quad(m, {-h, -h, -h}, {-h, -h, h}, {-h, h, h}, {-h, h, -h});
    quad(m, {-h, h, h}, {h, h, h}, {h, h, -h}, {-h, h, -h}); quad(m, {-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h});
  }
  bounds(m); return m;
}

std::vector<unsigned> simplifyIndices(const void* verts, size_t stride, size_t posOff, size_t nrmOff, size_t nv, const std::vector<unsigned>& idx, float keep, glm::vec3 bmin, glm::vec3 bmax) {
  const char* base = (const char*)verts; size_t nt = idx.size() / 3; if (nt < 4 || nv == 0) return idx;
  auto P = [&](size_t v) { glm::vec3 p; std::memcpy(&p, base + v * stride + posOff, 12); return p; };
  auto N = [&](size_t v) { glm::vec3 n; std::memcpy(&n, base + v * stride + nrmOff, 12); return n; };
  std::vector<uint8_t> face(nv); for (size_t v = 0; v < nv; v++) { glm::vec3 n = N(v), a = glm::abs(n); int ax = a.x >= a.y && a.x >= a.z ? 0 : a.y >= a.z ? 1 : 2; face[v] = (uint8_t)(ax * 2 + (n[ax] < 0)); }
  glm::vec3 ext = glm::max(bmax - bmin, glm::vec3(1e-4f)); float size = std::max({ext.x, ext.y, ext.z});
  std::vector<unsigned> best = idx, out, map(nv); std::unordered_map<uint64_t, unsigned> rep; rep.reserve(nv);
  auto run = [&](float cell) { rep.clear(); out.clear();
    for (size_t v = 0; v < nv; v++) { glm::vec3 q = glm::floor((P(v) - bmin) / cell); uint64_t key = ((uint64_t)(uint32_t)(int)q.x * 73856093ull) ^ ((uint64_t)(uint32_t)(int)q.y * 19349663ull << 20) ^ ((uint64_t)(uint32_t)(int)q.z * 83492791ull << 40) ^ ((uint64_t)face[v] << 61);
      auto it = rep.find(key); if (it == rep.end()) { rep.emplace(key, (unsigned)v); map[v] = (unsigned)v; } else map[v] = it->second; }
    for (size_t t = 0; t + 2 < idx.size(); t += 3) { unsigned a = map[idx[t]], b = map[idx[t + 1]], c = map[idx[t + 2]]; if (a == b || b == c || a == c) continue; out.push_back(a); out.push_back(b); out.push_back(c); } };
  // the cell size for about `keep` of the triangles: halve the range a few times
  float lo = size / 1024, hi = size / 2; size_t want = (size_t)(nt * keep);
  for (int it = 0; it < 10; it++) { float mid = std::sqrt(lo * hi); run(mid); if (out.size() / 3 > want) lo = mid; else { hi = mid; if (out.size() >= 12) best = out; } }
  return best;
}
std::vector<unsigned> withLods(GpuMesh& g, const void* verts, size_t stride, size_t posOff, size_t nrmOff, size_t nv, const std::vector<unsigned>& idx) {
  g.lodCount[0] = g.lodCount[1] = 0; std::vector<unsigned> all = idx; size_t nt = idx.size() / 3; if (nt < 300) return all;
  glm::vec3 mn(1e9f), mx(-1e9f); for (size_t v = 0; v < nv; v++) { glm::vec3 p; std::memcpy(&p, (const char*)verts + v * stride + posOff, 12); mn = glm::min(mn, p); mx = glm::max(mx, p); }
  const float keep[2] = {0.45f, 0.15f}; size_t prev = nt;
  for (int l = 0; l < 2; l++) { std::vector<unsigned> s = simplifyIndices(verts, stride, posOff, nrmOff, nv, idx, keep[l], mn, mx); size_t st = s.size() / 3;
    if (st == 0 || st > prev * 0.8) break;   // it didn't get much simpler (already low poly)
    g.lodFirst[l] = (unsigned)all.size(); g.lodCount[l] = (int)s.size(); all.insert(all.end(), s.begin(), s.end()); prev = st; }
  if (g.lodCount[0] && !g.lodCount[1]) { g.lodCount[1] = g.lodCount[0]; g.lodFirst[1] = g.lodFirst[0]; }
  return all;
}
GpuMesh upload(const MeshData& d, bool lines, bool lods) {
  GpuMesh g; g.lines = lines; g.bmin = d.bmin; g.bmax = d.bmax;
  glGenVertexArrays(1, &g.vao); glBindVertexArray(g.vao);
  glGenBuffers(1, &g.vbo); glBindBuffer(GL_ARRAY_BUFFER, g.vbo); glBufferData(GL_ARRAY_BUFFER, d.v.size() * sizeof(Vertex), d.v.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
  glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)12);
  glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)24);
  if (!d.i.empty()) { std::vector<unsigned> all = lods && !lines ? withLods(g, d.v.data(), sizeof(Vertex), 0, 12, d.v.size(), d.i) : d.i;
    glGenBuffers(1, &g.ebo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ebo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, all.size() * 4, all.data(), GL_STATIC_DRAW); g.count = (int)d.i.size(); }
  else g.count = (int)d.v.size();
  glBindVertexArray(0); return g;
}
GpuMesh uploadLines(const std::vector<glm::vec3>& pts) { MeshData d; for (auto& p : pts) d.v.push_back({p, glm::vec3(0, 1, 0)}); if (!pts.empty()) { d.bmin = d.bmax = pts[0]; for (auto& p : pts) { d.bmin = glm::min(d.bmin, p); d.bmax = glm::max(d.bmax, p); } } return upload(d, true); }
void destroy(GpuMesh& m) { if (m.ebo) glDeleteBuffers(1, &m.ebo); if (m.vbo) glDeleteBuffers(1, &m.vbo); if (m.vao) glDeleteVertexArrays(1, &m.vao); m = GpuMesh(); }
static std::map<std::string, GpuMesh> g_prims;
static std::map<std::string, MeshData> g_made;
void registerMesh(const std::string& name, const MeshData& m) { g_made[name] = m; auto it = g_prims.find(name); if (it != g_prims.end()) { destroy(it->second); g_prims.erase(it); } }
const MeshData* registeredMesh(const std::string& name) { auto it = g_made.find(name); return it == g_made.end() ? nullptr : &it->second; }
void forgetMeshes(const std::string& prefix) {
  for (auto it = g_made.begin(); it != g_made.end();) { if (it->first.rfind(prefix, 0) == 0) it = g_made.erase(it); else ++it; }
  for (auto it = g_prims.begin(); it != g_prims.end();) { if (it->first.rfind(prefix, 0) == 0) { destroy(it->second); it = g_prims.erase(it); } else ++it; }
}
const GpuMesh& primitive(const std::string& name) {
  auto it = g_prims.find(name); if (it != g_prims.end()) return it->second;
  if (auto m = registeredMesh(name)) return g_prims[name] = upload(*m);
  return g_prims[name] = upload(makePrimitive(name));
}
GpuMesh& dynamicMesh(const std::string& name, const MeshData& m) {
  auto it = g_prims.find(name);
  if (it != g_prims.end() && it->second.vbo && it->second.count == (int)m.i.size()) {   // the same shape: only the points move
    glBindBuffer(GL_ARRAY_BUFFER, it->second.vbo); glBufferSubData(GL_ARRAY_BUFFER, 0, m.v.size() * sizeof(Vertex), m.v.data()); glBindBuffer(GL_ARRAY_BUFFER, 0);
    it->second.bmin = m.bmin; it->second.bmax = m.bmax; return it->second; }
  if (it != g_prims.end()) destroy(it->second);
  return g_prims[name] = upload(m, false, false);
}
void releasePrimitives() { for (auto& [n, m] : g_prims) destroy(m); g_prims.clear(); }
}
