#include "navigation.h"
#include "physics.h"
#include <queue>
#include <cmath>
#include <chrono>
#include <algorithm>
#include <limits>

namespace pge {

void NavGrid::clear() { nx_ = nz_ = 0; col_.clear(); ys_.clear(); }

void NavGrid::build(const Physics& ph, glm::vec3 mn, glm::vec3 mx, const NavSettings& s) {
  auto t0 = std::chrono::steady_clock::now(); clear();
  mn -= glm::vec3(1); mx += glm::vec3(1);
  float cs = std::max(s.cell, 0.1f); const int MAXC = 640;   // big levels get coarser cells, not slow ones
  cs = std::max({cs, (mx.x - mn.x) / MAXC, (mx.z - mn.z) / MAXC});
  nx_ = std::max(1, (int)std::ceil((mx.x - mn.x) / cs)); nz_ = std::max(1, (int)std::ceil((mx.z - mn.z) / cs)); cs_ = cs; org_ = mn; step_ = s.step;
  float minNy = std::cos(glm::radians(glm::clamp(s.maxSlope, 1.f, 89.f))), r = std::max(s.radius, 0.05f), hb = std::max(s.height - s.step, 0.2f);
  float top = mx.y + 2, bottom = mn.y - 2;
  col_.assign((size_t)nx_ * nz_ + 1, 0);
  for (int iz = 0; iz < nz_; iz++) for (int ix = 0; ix < nx_; ix++) {
    col_[(size_t)iz * nx_ + ix] = (int)ys_.size();
    float x = org_.x + (ix + 0.5f) * cs, z = org_.z + (iz + 0.5f) * cs, y = top;
    for (int layer = 0; layer < 8 && y > bottom; layer++) {
      Physics::Hit h; if (!ph.rayCast({x, y, z}, {x, bottom, z}, h, true)) break;
      // a floor: not too steep, and a character fits above it (a box from a step above the floor to its head)
      if (h.normal.y >= minNy && !ph.boxOverlaps({x, h.point.y + s.step + hb * 0.5f + 0.02f, z}, {r, hb * 0.5f, r}, true)) ys_.push_back(h.point.y);
      y = h.point.y - 0.05f; }
  }
  col_[(size_t)nx_ * nz_] = (int)ys_.size();
  if (ys_.empty()) clear();
  ms_ = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

int NavGrid::columnOf(int n) const { return (int)(std::upper_bound(col_.begin(), col_.end(), n) - col_.begin()) - 1; }
glm::vec3 NavGrid::pos(int n) const { int c = columnOf(n); return {org_.x + (c % nx_ + 0.5f) * cs_, ys_[n], org_.z + (c / nx_ + 0.5f) * cs_}; }
int NavGrid::node(int ix, int iz, float y, float tol) const {
  if (ix < 0 || iz < 0 || ix >= nx_ || iz >= nz_) return -1; size_t c = (size_t)iz * nx_ + ix; int best = -1; float bd = tol;
  for (int n = col_[c]; n < col_[c + 1]; n++) { float d = std::abs(ys_[n] - y); if (d <= bd) { bd = d; best = n; } }
  return best;
}
bool NavGrid::nearest(const glm::vec3& p, glm::vec3& out, float maxDist) const {
  if (!ready()) return false; int cx = (int)std::floor((p.x - org_.x) / cs_), cz = (int)std::floor((p.z - org_.z) / cs_);
  int R = std::max(1, (int)std::ceil(maxDist / cs_)); float best = 1e30f; int bn = -1;
  for (int r = 0; r <= R && bn < 0; r++) for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++) {
    if (std::max(std::abs(dx), std::abs(dz)) != r) continue; int ix = cx + dx, iz = cz + dz; if (ix < 0 || iz < 0 || ix >= nx_ || iz >= nz_) continue;
    size_t c = (size_t)iz * nx_ + ix;
    for (int n = col_[c]; n < col_[c + 1]; n++) { float dy = p.y - ys_[n]; if (dy < -step_ - 0.3f || dy > 3.f) continue;   // the floor under the feet, not one far above or below
      glm::vec3 q = pos(n); float d = glm::length(glm::vec2(q.x - p.x, q.z - p.z)) + std::abs(dy) * 0.5f; if (d < best) { best = d; bn = n; } } }
  if (bn < 0 || best > maxDist + cs_) return false; out = pos(bn); return true;
}
bool NavGrid::straight(const glm::vec3& a, const glm::vec3& b) const {
  glm::vec3 d = b - a; float len = glm::length(glm::vec2(d.x, d.z)); int n = std::max(1, (int)std::ceil(len / (cs_ * 0.35f))); float y = a.y;
  for (int i = 1; i <= n; i++) { glm::vec3 p = a + d * ((float)i / n); int ix = (int)std::floor((p.x - org_.x) / cs_), iz = (int)std::floor((p.z - org_.z) / cs_);
    int k = node(ix, iz, y, step_); if (k < 0) return false; y = ys_[k]; }
  return std::abs(y - b.y) <= step_ + 0.05f;
}
bool NavGrid::findPath(const glm::vec3& from, const glm::vec3& to, std::vector<glm::vec3>& out) const {
  out.clear(); glm::vec3 a, b; if (!nearest(from, a) || !nearest(to, b, 6)) return false;
  auto idx = [&](const glm::vec3& p) { return node((int)std::floor((p.x - org_.x) / cs_), (int)std::floor((p.z - org_.z) / cs_), p.y, 0.01f); };
  int s = idx(a), g = idx(b); if (s < 0 || g < 0) return false;
  size_t N = ys_.size(); std::vector<float> cost(N, std::numeric_limits<float>::max()); std::vector<int> from_(N, -1); std::vector<char> closed(N, 0);
  using Q = std::pair<float, int>; std::priority_queue<Q, std::vector<Q>, std::greater<Q>> open;
  glm::vec3 gp = pos(g); cost[s] = 0; open.push({glm::length(pos(s) - gp), s});
  static const int DX[8] = {1, -1, 0, 0, 1, 1, -1, -1}, DZ[8] = {0, 0, 1, -1, 1, -1, 1, -1};
  bool found = false; int visited = 0;
  while (!open.empty()) {
    int n = open.top().second; open.pop(); if (closed[n]) continue; closed[n] = 1; if (n == g) { found = true; break; } if (++visited > 400000) break;
    int c = columnOf(n), ix = c % nx_, iz = c / nx_; float y = ys_[n]; glm::vec3 p = pos(n);
    for (int k = 0; k < 8; k++) { int m = node(ix + DX[k], iz + DZ[k], y, step_); if (m < 0 || closed[m]) continue;
      if (k >= 4 && (node(ix + DX[k], iz, y, step_) < 0 || node(ix, iz + DZ[k], y, step_) < 0)) continue;   // no cutting corners past walls
      glm::vec3 q = pos(m); float nc = cost[n] + glm::length(q - p); if (nc < cost[m]) { cost[m] = nc; from_[m] = n; open.push({nc + glm::length(q - gp), m}); } }
  }
  if (!found) return false;
  std::vector<glm::vec3> raw; for (int n = g; n >= 0; n = from_[n]) raw.push_back(pos(n)); std::reverse(raw.begin(), raw.end());
  // straighten: from each point, on to the farthest one it can walk to in a line
  glm::vec3 start(from.x, a.y, from.z), end(to.x, b.y, to.z); if (glm::length(glm::vec2(to.x - b.x, to.z - b.z)) > cs_) end = b;   // the goal is off the floor: its nearest walkable place
  raw.front() = straight(start, raw.size() > 1 ? raw[1] : raw[0]) ? start : raw.front(); raw.back() = straight(raw.size() > 1 ? raw[raw.size() - 2] : raw.back(), end) ? end : raw.back();
  out.push_back(raw.front()); size_t i = 0;
  while (i + 1 < raw.size()) { size_t j = raw.size() - 1; while (j > i + 1 && !straight(raw[i], raw[j])) j--; out.push_back(raw[j]); i = j; }
  return true;
}
void NavGrid::lines(std::vector<glm::vec3>& L) const {
  L.clear(); if (!ready()) return; float h = cs_ * 0.42f; int stride = ys_.size() > 120000 ? 2 : 1;
  for (int c = 0; c < nx_ * nz_; c++) { if (stride > 1 && ((c % nx_) + (c / nx_)) % 2) continue;
    for (int n = col_[c]; n < col_[c + 1]; n++) { glm::vec3 p = pos(n) + glm::vec3(0, 0.04f, 0);
      glm::vec3 q[4] = {p + glm::vec3(-h, 0, -h), p + glm::vec3(h, 0, -h), p + glm::vec3(h, 0, h), p + glm::vec3(-h, 0, h)};
      for (int k = 0; k < 4; k++) { L.push_back(q[k]); L.push_back(q[(k + 1) % 4]); } } }
}
}
