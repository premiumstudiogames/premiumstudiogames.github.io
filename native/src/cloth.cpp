// Cloth: a sheet of points (res × res) joined to their neighbours (along, across, and two apart: it bends but keeps its
// shape), moved by gravity and wind (Verlet), held where it is pinned to its actor, pushed out of characters and bodies,
// and stopped by the level's surfaces. Drawn from both sides.
#include "game.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>

using json = nlohmann::json;
namespace pge {

static bool pinnedAt(const ClothProps& c, int x, int y, int n) {
  if (c.pin == "none") return false; if (c.pin == "left") return x == 0; if (c.pin == "corners") return y == 0 && (x == 0 || x == n - 1); if (c.pin == "all") return true; return y == 0;
}
static glm::vec3 restLocal(const ClothProps& c, int x, int y, int n) { return {(x / float(n - 1) - 0.5f) * c.width, -y / float(n - 1) * c.height, 0}; }

// the mesh of a sheet (both sides) from its points
static void clothMesh(const std::vector<glm::vec3>& P, int n, MeshData& m) {
  m.v.resize((size_t)n * n * 2); m.i.clear(); m.bmin = glm::vec3(1e9f); m.bmax = glm::vec3(-1e9f);
  for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) { int k = y * n + x; glm::vec3 p = P[k];
    glm::vec3 dx = P[y * n + std::min(x + 1, n - 1)] - P[y * n + std::max(x - 1, 0)], dy = P[std::min(y + 1, n - 1) * n + x] - P[std::max(y - 1, 0) * n + x];
    glm::vec3 nr = glm::cross(dx, dy); nr = glm::length(nr) > 1e-9f ? glm::normalize(nr) : glm::vec3(0, 0, 1); glm::vec2 uv(x / float(n - 1), y / float(n - 1));
    m.v[k] = {p, nr, uv}; m.v[k + n * n] = {p, -nr, uv}; m.bmin = glm::min(m.bmin, p); m.bmax = glm::max(m.bmax, p); }
  for (int y = 0; y + 1 < n; y++) for (int x = 0; x + 1 < n; x++) { unsigned a = y * n + x, b = a + 1, c = a + n, d = c + 1, o = n * n;
    for (unsigned i : {a, c, b, b, c, d}) m.i.push_back(i); for (unsigned i : {a + o, b + o, c + o, b + o, d + o, c + o}) m.i.push_back(i); }
}
void clothRestMesh(const ClothProps& c, MeshData& m) { int n = std::clamp(c.res, 3, 64); std::vector<glm::vec3> P; for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) P.push_back(restLocal(c, x, y, n)); clothMesh(P, n, m); }

void Game::clothStart(Obj& o) {
  auto& s = o.cloth; const ClothProps& c = o.a.cloth; int n = std::clamp(c.res, 3, 64); s.n = n; s.p.clear(); s.pin.clear(); s.links.clear(); glm::mat4 W = world(o);
  for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) { glm::vec3 w = glm::vec3(W * glm::vec4(restLocal(c, x, y, n), 1)); s.p.push_back(w); s.pin.push_back(pinnedAt(c, x, y, n)); }
  s.prev = s.p; auto link = [&](int a, int b) { s.links.push_back({a, b, glm::length(s.p[a] - s.p[b])}); };
  for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) { int k = y * n + x;
    if (x + 1 < n) link(k, k + 1); if (y + 1 < n) link(k, k + n); if (x + 1 < n && y + 1 < n) { link(k, k + n + 1); link(k + 1, k + n); }   // along, across
    if (x + 2 < n) link(k, k + 2); if (y + 2 < n) link(k, k + 2 * n); }   // two apart: it bends softly
  s.on = true;
}
void Game::clothFrame(float dt) {
  for (auto& op : objs_) { Obj& o = *op; if (!o.alive || o.a.type != "Cloth") continue; if (!o.cloth.on) clothStart(o); auto& s = o.cloth; const ClothProps& c = o.a.cloth; int n = s.n;
    glm::mat4 W = world(o);
    // what it can touch: the player, characters, moving bodies (as spheres)
    std::vector<glm::vec4> balls; if (player_ != NO_BODY && driving_.empty()) { glm::vec3 pp = phys_->position(player_); balls.push_back(glm::vec4(pp + glm::vec3(0, 0.45f, 0), 0.38f)); balls.push_back(glm::vec4(pp - glm::vec3(0, 0.45f, 0), 0.38f)); }
    for (auto& q : objs_) { Obj& b = *q; if (!b.alive || &b == &o || b.a.type != "StaticMesh") continue; bool mover = b.motion == Motion::Dynamic || b.remote >= 0 || isModelFile(b.a.mesh); if (!mover || b.body == NO_BODY && !isModelFile(b.a.mesh)) continue;
      glm::vec3 bp = glm::vec3(world(b)[3]); if (glm::length(bp - s.p[n * n / 2]) > std::max(c.width, c.height) + 4) continue; const MeshData& md = meshShape(b.a.mesh); glm::vec3 e = (md.bmax - md.bmin) * glm::abs(b.s) * 0.5f;
      if (isModelFile(b.a.mesh)) { float r = std::max(0.2f, std::min(e.x, e.z) * 0.8f); glm::vec3 base = glm::vec3(world(b) * glm::vec4((md.bmin + md.bmax) * 0.5f, 1)); for (float h : {-0.5f, 0.f, 0.5f}) balls.push_back(glm::vec4(base + glm::vec3(0, h * e.y, 0), r)); }
      else balls.push_back(glm::vec4(glm::vec3(world(b) * glm::vec4((md.bmin + md.bmax) * 0.5f, 1)), std::max({e.x, e.y, e.z}) * 0.9f)); }
    glm::vec3 wind = glm::length(c.windDir) > 1e-4f ? glm::normalize(c.windDir) * c.wind * 6.f : glm::vec3(0); float g = gravity_ != 0 ? gravity_ : -9.81f;
    int steps = 2; float h = std::min(dt, 0.05f) / steps; int iters = std::clamp((int)std::round(4 + 8 * c.stiffness), 2, 20);
    for (int st = 0; st < steps; st++) {
      float t = (float)time_ + st * h;
      for (int k = 0; k < n * n; k++) { if (s.pin[k]) { int x = k % n, y = k / n; s.prev[k] = s.p[k]; s.p[k] = glm::vec3(W * glm::vec4(restLocal(c, x, y, n), 1)); continue; }
        glm::vec3 v = (s.p[k] - s.prev[k]) * 0.985f; float gust = 0.55f + 0.45f * std::sin(t * 2.1f + k * 0.37f) * std::sin(t * 0.7f + k * 0.11f);
        s.prev[k] = s.p[k]; s.p[k] += v + (glm::vec3(0, g, 0) + wind * gust / std::max(0.1f, c.mass)) * h * h; }
      for (int it = 0; it < iters; it++) for (auto& l : s.links) { glm::vec3 d = s.p[l.b] - s.p[l.a]; float len = glm::length(d); if (len < 1e-7f) continue;
          float wa = s.pin[l.a] ? 0.f : 1.f, wb = s.pin[l.b] ? 0.f : 1.f; if (wa + wb == 0) continue; glm::vec3 corr = d * ((len - l.rest) / len / (wa + wb));
          s.p[l.a] += corr * wa; s.p[l.b] -= corr * wb; }
      for (int k = 0; k < n * n; k++) { if (s.pin[k]) continue;
        for (auto& b : balls) { glm::vec3 d = s.p[k] - glm::vec3(b); float l = glm::length(d); if (l < b.w + 0.02f && l > 1e-6f) s.p[k] = glm::vec3(b) + d / l * (b.w + 0.02f); }
        if (st == steps - 1 && glm::length(s.p[k] - s.prev[k]) > 1e-4f) { Physics::Hit hit; glm::vec3 from = s.prev[k] + glm::vec3(0, 0.02f, 0);
          if (phys_->rayCast(from, s.p[k], hit, true)) { s.p[k] = hit.point + hit.normal * 0.015f; s.prev[k] = glm::mix(s.prev[k], s.p[k], 0.6f); } } } }   // it rests on surfaces (with friction)
    s.version++; }
}
// foliage that collides (trees, rocks): a still body for each copy
void Game::foliageStart() {
  foliageBodies_.clear();
  for (auto& op : objs_) { Obj& o = *op; if (!o.alive || o.a.type != "Foliage" || !o.a.fol) continue;
    for (auto& t : o.a.fol->types) { if (!t.collision) continue; for (auto& i : t.inst) { glm::mat4 m = t.matrix(i); glm::vec3 p(m[3]), sc(i.scale); glm::quat q = glm::quat_cast(glm::mat3(m) / std::max(i.scale, 1e-4f));
        uint32_t b = phys_->addMesh(t.mesh, sc, p, q, Motion::Static, 1, 0); if (b != NO_BODY) foliageBodies_.push_back(b); } } }
  if (!foliageBodies_.empty()) phys_->optimize();
}
void Game::clothDraws(Frame& f) const {
  for (auto& op : objs_) { const Obj& o = *op; if (!o.alive || o.a.type != "Cloth" || !o.cloth.on || !o.visible) continue; MeshData m; clothMesh(o.cloth.p, o.cloth.n, m);
    const GpuMesh& g = dynamicMesh("@cloth:" + o.a.id, m); DrawItem d; d.mesh = &g; d.model = glm::mat4(1);
    auto it = project_.materials.find(o.a.cloth.material); if (it != project_.materials.end()) applyMaterial(d, it->second); else { d.color = glm::vec3(0.62f, 0.12f, 0.12f); d.rough = 0.85f; }
    if (o.hasColor) d.color = o.color; f.draws.push_back(d); }
}
}
