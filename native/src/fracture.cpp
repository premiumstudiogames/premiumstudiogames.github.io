// Breaking meshes into pieces: the mesh is cut by planes through its middle again and again (the biggest piece next), each
// cut closed with a cap, until there are enough pieces. The same seed gives the same pieces (every computer of an online game
// breaks it the same way). Then in the game: the actor goes, its pieces fly from the hit and fall (physics), and go later.
#include "game.h"
#include "log.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

using json = nlohmann::json;
namespace pge {

struct Tri { Vertex v[3]; };
static uint32_t rnd(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s ? s : (s = 0x2545F491u); }
static float rndf(uint32_t& s) { return (rnd(s) & 0xffffff) / float(0xffffff); }
static Vertex lerpV(const Vertex& a, const Vertex& b, float t) { Vertex v; v.p = glm::mix(a.p, b.p, t); v.n = glm::normalize(glm::mix(a.n, b.n, t) + glm::vec3(1e-7f)); v.uv = glm::mix(a.uv, b.uv, t); return v; }

// one piece cut in two by the plane (n, d): triangles on each side; the points where it was cut (for the caps)
static void cut(const std::vector<Tri>& in, const glm::vec3& n, float d, std::vector<Tri>& A, std::vector<Tri>& B, std::vector<glm::vec3>& seam) {
  for (auto& t : in) { float s[3]; int pos = 0; for (int k = 0; k < 3; k++) { s[k] = glm::dot(n, t.v[k].p) - d; pos += s[k] >= 0; }
    if (pos == 3) { A.push_back(t); continue; } if (pos == 0) { B.push_back(t); continue; }
    // one point alone on its side: it makes one triangle there, the other two make two on the other side
    int lone = -1; for (int k = 0; k < 3; k++) if ((s[k] >= 0) == (pos == 1)) { lone = k; break; }
    const Vertex& a = t.v[lone]; const Vertex& b = t.v[(lone + 1) % 3]; const Vertex& c = t.v[(lone + 2) % 3];
    float sa = s[lone], sb = s[(lone + 1) % 3], sc = s[(lone + 2) % 3];
    Vertex ab = lerpV(a, b, sa / (sa - sb)), ac = lerpV(a, c, sa / (sa - sc)); seam.push_back(ab.p); seam.push_back(ac.p);
    Tri t1{{a, ab, ac}}, t2{{ab, b, c}}, t3{{ab, c, ac}};
    std::vector<Tri>& lonely = (pos == 1) ? A : B; std::vector<Tri>& other = (pos == 1) ? B : A; lonely.push_back(t1); other.push_back(t2); other.push_back(t3); }
}
// the cut closed: a fan from the middle of the seam, round in order (pieces of the usual meshes are convex)
static void cap(std::vector<Tri>& piece, const std::vector<glm::vec3>& seam, const glm::vec3& out) {
  if (seam.size() < 3) return; glm::vec3 c(0); for (auto& p : seam) c += p; c /= (float)seam.size();
  glm::vec3 u = glm::abs(out.y) < 0.9f ? glm::normalize(glm::cross(out, glm::vec3(0, 1, 0))) : glm::normalize(glm::cross(out, glm::vec3(1, 0, 0))), w = glm::cross(out, u);
  std::vector<std::pair<float, glm::vec3>> ring; for (auto& p : seam) { glm::vec3 q = p - c; ring.push_back({std::atan2(glm::dot(q, w), glm::dot(q, u)), p}); }
  std::sort(ring.begin(), ring.end(), [](auto& x, auto& y) { return x.first < y.first; });
  auto vx = [&](const glm::vec3& p) { Vertex v; v.p = p; v.n = out; v.uv = {glm::dot(p, u), glm::dot(p, w)}; return v; };
  for (size_t k = 0; k < ring.size(); k++) { glm::vec3 a = ring[k].second, b = ring[(k + 1) % ring.size()].second; if (glm::length(b - a) < 1e-6f) continue;
    Tri t{{vx(c), vx(a), vx(b)}}; if (glm::dot(glm::cross(a - c, b - c), out) < 0) std::swap(t.v[1], t.v[2]); piece.push_back(t); }
}
static void bounds(const std::vector<Tri>& p, glm::vec3& mn, glm::vec3& mx) { mn = glm::vec3(1e9f); mx = glm::vec3(-1e9f); for (auto& t : p) for (auto& v : t.v) { mn = glm::min(mn, v.p); mx = glm::max(mx, v.p); } }

void fractureMesh(const MeshData& src, const glm::vec3& scale, int pieces, uint32_t seed, std::vector<MeshData>& out, std::vector<glm::vec3>& centers) {
  out.clear(); centers.clear(); std::vector<std::vector<Tri>> P(1); uint32_t s = seed * 2654435761u + 12345u;
  for (size_t k = 0; k + 2 < src.i.size(); k += 3) { Tri t; for (int j = 0; j < 3; j++) { t.v[j] = src.v[src.i[k + j]]; t.v[j].p *= scale; t.v[j].n = glm::normalize(t.v[j].n / scale); } P[0].push_back(t); }
  if (src.i.empty()) for (size_t k = 0; k + 2 < src.v.size(); k += 3) { Tri t{{src.v[k], src.v[k + 1], src.v[k + 2]}}; for (auto& v : t.v) v.p *= scale; P[0].push_back(t); }
  pieces = std::clamp(pieces, 2, 64);
  for (int tries = 0; (int)P.size() < pieces && tries < pieces * 6; tries++) {
    size_t big = 0; float bv = -1; for (size_t k = 0; k < P.size(); k++) { glm::vec3 mn, mx; bounds(P[k], mn, mx); glm::vec3 e = mx - mn; float v = e.x * e.y * e.z + 1e-6f * (e.x + e.y + e.z); if (v > bv) { bv = v; big = k; } }
    glm::vec3 mn, mx; bounds(P[big], mn, mx); glm::vec3 e = mx - mn;
    glm::vec3 c = (mn + mx) * 0.5f + (glm::vec3(rndf(s), rndf(s), rndf(s)) - 0.5f) * e * 0.3f;
    glm::vec3 n = glm::normalize(glm::vec3(rndf(s) - 0.5f, rndf(s) - 0.5f, rndf(s) - 0.5f) + glm::vec3(1e-4f)); n = glm::normalize(n * (glm::vec3(1e-3f) + e));   // across the long side
    std::vector<Tri> A, B; std::vector<glm::vec3> seam; cut(P[big], n, glm::dot(n, c), A, B, seam);
    if (A.size() < 4 || B.size() < 4) continue;
    cap(A, seam, -n); cap(B, seam, n); P[big] = std::move(A); P.push_back(std::move(B)); }
  for (auto& p : P) { glm::vec3 mn, mx; bounds(p, mn, mx); glm::vec3 c = (mn + mx) * 0.5f; MeshData m; m.bmin = mn - c; m.bmax = mx - c;
    for (auto& t : p) { glm::vec3 fn = glm::cross(t.v[1].p - t.v[0].p, t.v[2].p - t.v[0].p); for (auto v : t.v) { v.p -= c; if (glm::dot(v.n, fn) < 0 && glm::length(fn) > 1e-9f) v.n = glm::normalize(fn); m.i.push_back((unsigned)m.v.size()); m.v.push_back(v); } }
    out.push_back(std::move(m)); centers.push_back(c); }
}

bool Game::fracture(Obj& o, uint32_t seed, const glm::vec3& point, float impulse, bool hasPoint) {
  if (!o.alive || o.a.type != "StaticMesh" || o.remote >= 0 || o.a.mesh.empty() || o.a.mesh[0] == '@') return false;
  const MeshData& src = meshShape(o.a.mesh); if (src.v.empty()) return false;
  glm::mat4 W = world(o); glm::vec3 sc(glm::length(glm::vec3(W[0])), glm::length(glm::vec3(W[1])), glm::length(glm::vec3(W[2]))); glm::mat3 R(glm::vec3(W[0]) / sc.x, glm::vec3(W[1]) / sc.y, glm::vec3(W[2]) / sc.z);
  std::vector<MeshData> parts; std::vector<glm::vec3> centers; fractureMesh(src, sc, o.a.destructible.enabled ? o.a.destructible.pieces : 10, seed, parts, centers); if (parts.size() < 2) return false;
  if (netOn() && !fromNet_) netSend({{"k", "fr"}, {"i", o.a.id}, {"seed", seed}, {"pt", {point.x, point.y, point.z}}, {"hp", hasPoint}, {"imp", impulse}, {"f", netMe()}});
  glm::vec3 vel = o.body != NO_BODY && o.motion == Motion::Dynamic ? phys_->velocity(o.body) : glm::vec3(0); glm::vec3 color = o.color; bool hasColor = o.hasColor;
  float total = o.a.physics.simulate ? o.a.physics.mass : std::max(5.f, sc.x * sc.y * sc.z * 600), vsum = 0; std::vector<float> vol; for (auto& m : parts) { glm::vec3 e = m.bmax - m.bmin; vol.push_back(std::max(1e-5f, e.x * e.y * e.z)); vsum += vol.back(); }
  std::string id = o.a.id, material = o.a.material, name = o.a.name; float life = o.a.destructible.enabled ? o.a.destructible.debrisLife : 10; int owner = netOn() ? (fromNet_ ? o.owner : netMe()) : 0; glm::quat q = glm::quat_cast(R);
  { bool was = fromNet_; fromNet_ = true; destroyActor(id); fromNet_ = was; }   // the whole goes (on every computer each breaks its own)
  for (size_t k = 0; k < parts.size(); k++) { std::string mesh = "@frag:" + id + ":" + std::to_string(k); registerMesh(mesh, parts[k]);
    Actor a = newActor("StaticMesh", name + " piece"); a.id = id + "_f" + std::to_string(k); a.mesh = mesh; a.material = material; a.physics.simulate = true; a.physics.mass = std::max(0.2f, total * vol[k] / vsum);
    glm::vec3 wp = glm::vec3(W[3]) + R * centers[k]; a.p = wp;
    auto ob = std::make_unique<Obj>(); ob->a = a; ob->p = wp; ob->r = eulerYXZ(q); ob->s = glm::vec3(1); ob->color = color; ob->hasColor = hasColor; ob->owner = owner; ob->repl = true; ob->dieAt = life > 0 ? time_ + life + (k % 5) * 0.2 : -1;
    Obj& ref = *ob; byId_[a.id] = ob.get(); objs_.push_back(std::move(ob)); addBody(ref);
    if (ref.body != NO_BODY && owns(ref)) { glm::vec3 dir = hasPoint ? wp - point : R * centers[k]; dir = glm::length(dir) > 1e-4f ? glm::normalize(dir) : glm::vec3(0, 1, 0);
      phys_->setVelocity(ref.body, vel + dir * (impulse / std::max(1.f, total)) * (0.6f + 0.8f * ((k * 7919) % 100) / 100.f)); phys_->setAngularVelocity(ref.body, dir * 3.f); } }
  return true;
}
}
