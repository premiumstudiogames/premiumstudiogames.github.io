#include "landscape.h"
#include "mesh.h"
#include "gl.h"
#include <cmath>
#include <map>
#include <atomic>
#include <algorithm>
#include <cstring>

namespace pge {
using json = nlohmann::json;
static std::atomic<uint64_t> g_serial{1};
Landscape::Landscape() { serial = g_serial++; resize(128, 128); }
void Landscape::resize(int r, float s) {
  res = std::clamp(r, 8, 1024); size = std::max(1.f, s); h.assign((size_t)res * res, 0.f); w.assign((size_t)res * res * 4, 0); for (size_t i = 0; i < (size_t)res * res; i++) w[i * 4] = 255; changed();
}
float Landscape::heightAt(float x, float z) const {
  float fx = (x + size * 0.5f) / cell(), fz = (z + size * 0.5f) / cell(); fx = std::clamp(fx, 0.f, (float)res - 1.001f); fz = std::clamp(fz, 0.f, (float)res - 1.001f);
  int ix = (int)fx, iz = (int)fz; float u = fx - ix, v = fz - iz;
  auto H = [&](int a, int b) { return h[(size_t)b * res + a]; };
  // the same two triangles per cell as the mesh
  if (u + v <= 1) return H(ix, iz) + (H(ix + 1, iz) - H(ix, iz)) * u + (H(ix, iz + 1) - H(ix, iz)) * v;
  return H(ix + 1, iz + 1) + (H(ix, iz + 1) - H(ix + 1, iz + 1)) * (1 - u) + (H(ix + 1, iz) - H(ix + 1, iz + 1)) * (1 - v);
}
glm::vec3 Landscape::normalAt(float x, float z) const { float e = cell(); return glm::normalize(glm::vec3(heightAt(x - e, z) - heightAt(x + e, z), 2 * e, heightAt(x, z - e) - heightAt(x, z + e))); }
bool Landscape::raycast(const glm::vec3& o, const glm::vec3& d, float maxT, float& t) const {
  float step = cell() * 0.5f, prev = 0; float half = size * 0.5f;
  for (float s = 0; s <= maxT; s += step) { glm::vec3 p = o + d * s; if (std::fabs(p.x) > half + step || std::fabs(p.z) > half + step) { prev = s; continue; }
    if (p.y <= heightAt(p.x, p.z)) {   // crossed: halve the step to the surface
      float a = prev, b = s; for (int k = 0; k < 20; k++) { float m = (a + b) * 0.5f; glm::vec3 q = o + d * m; if (q.y <= heightAt(q.x, q.z)) b = m; else a = m; }
      t = b; return std::fabs((o + d * t).x) <= half && std::fabs((o + d * t).z) <= half; }
    prev = s; }
  return false;
}
void Landscape::brush(Tool tool, float cx, float cz, float radius, float strength, float dt, int li, float flat) {
  float c = cell(); int x0 = std::max(0, (int)std::floor((cx - radius + size * 0.5f) / c)), x1 = std::min(res - 1, (int)std::ceil((cx + radius + size * 0.5f) / c));
  int z0 = std::max(0, (int)std::floor((cz - radius + size * 0.5f) / c)), z1 = std::min(res - 1, (int)std::ceil((cz + radius + size * 0.5f) / c));
  if (x0 > x1 || z0 > z1) return; std::vector<float> old = h; li = std::clamp(li, 0, 3);
  for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) {
    float px = x * c - size * 0.5f, pz = z * c - size * 0.5f, d = std::sqrt((px - cx) * (px - cx) + (pz - cz) * (pz - cz)); if (d > radius) continue;
    float f = 0.5f + 0.5f * std::cos(d / radius * 3.14159265f);   // soft edge
    size_t i = (size_t)z * res + x; float k = f * strength * dt;
    switch (tool) {
      case Raise: h[i] += k * 4; break;
      case Lower: h[i] -= k * 4; break;
      case Smooth: { float s = 0; int n = 0; for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) { int a = x + dx, b = z + dz; if (a < 0 || b < 0 || a >= res || b >= res) continue; s += old[(size_t)b * res + a]; n++; }
        h[i] += (s / n - h[i]) * std::min(1.f, k * 6); break; }
      case Flatten: h[i] += (flat - h[i]) * std::min(1.f, k * 6); break;
      case Noise: { uint32_t s = (uint32_t)(x * 73856093u ^ z * 19349663u ^ (uint32_t)(version * 83492791u)); s ^= s >> 13; s *= 0x5bd1e995u; s ^= s >> 15; h[i] += ((s & 0xffff) / 65535.f - 0.5f) * k * 2; break; }
      case Paint: { float add = std::min(1.f, k * 4) * 255; int sum = 0; uint8_t* p = &w[i * 4];
        for (int l = 0; l < 4; l++) { float v = p[l]; v = l == li ? std::min(255.f, v + add) : std::max(0.f, v - add); p[l] = (uint8_t)std::lround(v); sum += p[l]; }
        if (sum == 0) p[li] = 255; break; }
    } }
  changed();
}
void Landscape::hills(float height, float scale, uint32_t seed) {
  auto hash = [&](int x, int z) { uint32_t s = (uint32_t)(x * 374761393u + z * 668265263u + seed * 2246822519u); s = (s ^ (s >> 13)) * 1274126177u; return (s ^ (s >> 16)) / 4294967295.f; };
  auto noise = [&](float x, float z) { int ix = (int)std::floor(x), iz = (int)std::floor(z); float u = x - ix, v = z - iz; u = u * u * (3 - 2 * u); v = v * v * (3 - 2 * v);
    return (hash(ix, iz) * (1 - u) + hash(ix + 1, iz) * u) * (1 - v) + (hash(ix, iz + 1) * (1 - u) + hash(ix + 1, iz + 1) * u) * v; };
  for (int z = 0; z < res; z++) for (int x = 0; x < res; x++) { float px = x * cell() / std::max(scale, 1.f), pz = z * cell() / std::max(scale, 1.f), a = 1, f = 1, s = 0, n = 0;
    for (int o = 0; o < 5; o++) { s += noise(px * f, pz * f) * a; n += a; a *= 0.5f; f *= 2; } h[(size_t)z * res + x] = (s / n - 0.5f) * 2 * height; }
  changed();
}
static const char* B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static std::string b64(const uint8_t* d, size_t n) { std::string o; o.reserve((n + 2) / 3 * 4);
  for (size_t i = 0; i < n; i += 3) { uint32_t v = d[i] << 16 | (i + 1 < n ? d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
    o += B64[(v >> 18) & 63]; o += B64[(v >> 12) & 63]; o += i + 1 < n ? B64[(v >> 6) & 63] : '='; o += i + 2 < n ? B64[v & 63] : '='; } return o; }
static std::vector<uint8_t> unb64(const std::string& s) { int T[256]; std::fill(T, T + 256, -1); for (int i = 0; i < 64; i++) T[(uint8_t)B64[i]] = i; std::vector<uint8_t> o; uint32_t v = 0; int b = 0;
  for (char c : s) { int x = T[(uint8_t)c]; if (x < 0) continue; v = v << 6 | x; b += 6; if (b >= 8) { b -= 8; o.push_back((uint8_t)(v >> b)); } } return o; }
json Landscape::toJson() const {
  std::vector<uint8_t> hb(h.size() * 4); std::memcpy(hb.data(), h.data(), hb.size());   // little-endian float32 (Windows, x86, ARM)
  json L = json::array(); for (int i = 0; i < 4; i++) L.push_back({{"material", layer[i]}, {"tiling", tiling[i]}});
  return {{"res", res}, {"size", size}, {"heights", b64(hb.data(), hb.size())}, {"weights", b64(w.data(), w.size())}, {"layers", L}};
}
bool Landscape::fromJson(const json& j) {
  if (!j.is_object()) return false; int r = j.value("res", 128); float s = j.value("size", 128.f); resize(r, s);
  std::vector<uint8_t> hb = unb64(j.value("heights", std::string())); if (hb.size() == h.size() * 4) std::memcpy(h.data(), hb.data(), hb.size());
  std::vector<uint8_t> wb = unb64(j.value("weights", std::string())); if (wb.size() == w.size()) w = wb;
  if (j.contains("layers") && j["layers"].is_array()) for (size_t i = 0; i < 4 && i < j["layers"].size(); i++) { layer[i] = j["layers"][i].value("material", std::string()); tiling[i] = j["layers"][i].value("tiling", 4.f); }
  changed(); return true;
}
void landscapeMesh(const Landscape& l, std::vector<float>& pos, std::vector<uint32_t>& idx) {
  pos.clear(); idx.clear(); float c = l.cell();
  for (int z = 0; z < l.res; z++) for (int x = 0; x < l.res; x++) { pos.push_back(x * c - l.size * 0.5f); pos.push_back(l.h[(size_t)z * l.res + x]); pos.push_back(z * c - l.size * 0.5f); }
  for (int z = 0; z + 1 < l.res; z++) for (int x = 0; x + 1 < l.res; x++) { uint32_t a = z * l.res + x, b = a + 1, d = a + l.res, e = d + 1; idx.insert(idx.end(), {a, d, b, b, d, e}); }
}
// drawn like a model (position, normal, uv, the 4 weights as the vertex colour)
struct LV { float p[3], n[3], uv[2]; uint8_t col[4]; uint16_t j[4]; float w[4]; };
static_assert(sizeof(LV) == 60, "same layout as models");
struct LGpu { GpuMesh g; uint64_t serial = 0; int version = -1; double used = 0; };
static std::map<uint64_t, LGpu> g_land; static double g_tick = 0;
const GpuMesh& landscapeGpu(const Landscape& l) {
  LGpu& e = g_land[l.serial]; e.used = ++g_tick;
  if (e.version == l.version) return e.g;
  if (g_land.size() > 32) for (auto it = g_land.begin(); it != g_land.end();) { if (it->second.used < g_tick - 600 && &it->second != &e) { destroy(it->second.g); it = g_land.erase(it); } else ++it; }
  std::vector<float> pos; std::vector<uint32_t> idx; landscapeMesh(l, pos, idx);
  std::vector<LV> v((size_t)l.res * l.res);
  for (int z = 0; z < l.res; z++) for (int x = 0; x < l.res; x++) { size_t i = (size_t)z * l.res + x; LV& q = v[i]; std::memset(&q, 0, sizeof q);
    q.p[0] = pos[i * 3]; q.p[1] = pos[i * 3 + 1]; q.p[2] = pos[i * 3 + 2]; glm::vec3 n = l.normalAt(q.p[0], q.p[2]); q.n[0] = n.x; q.n[1] = n.y; q.n[2] = n.z;
    q.uv[0] = (float)x / (l.res - 1); q.uv[1] = (float)z / (l.res - 1); for (int k = 0; k < 4; k++) q.col[k] = l.w[i * 4 + k]; q.w[0] = 1; }
  glm::vec3 mn(1e9f), mx(-1e9f); for (size_t i = 0; i < v.size(); i++) { glm::vec3 p(v[i].p[0], v[i].p[1], v[i].p[2]); mn = glm::min(mn, p); mx = glm::max(mx, p); }
  e.version = l.version; e.g.bmin = mn; e.g.bmax = mx; e.g.count = (int)idx.size();
  if (!glGenBuffers) return e.g;
  if (!e.g.vao) { glGenVertexArrays(1, &e.g.vao); glGenBuffers(1, &e.g.vbo); glGenBuffers(1, &e.g.ebo); glBindVertexArray(e.g.vao); glBindBuffer(GL_ARRAY_BUFFER, e.g.vbo);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LV), (void*)0); glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(LV), (void*)12);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(LV), (void*)24); glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(LV), (void*)32);
    glEnableVertexAttribArray(4); glVertexAttribIPointer(4, 4, GL_UNSIGNED_SHORT, sizeof(LV), (void*)36); glEnableVertexAttribArray(5); glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(LV), (void*)44);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, e.g.ebo); glBindVertexArray(0); }
  glBindVertexArray(e.g.vao); glBindBuffer(GL_ARRAY_BUFFER, e.g.vbo); glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(LV), v.data(), GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, e.g.ebo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * 4, idx.data(), GL_DYNAMIC_DRAW); glBindVertexArray(0);
  return e.g;
}
void releaseLandscapes() { for (auto& [k, e] : g_land) destroy(e.g); g_land.clear(); }
}
