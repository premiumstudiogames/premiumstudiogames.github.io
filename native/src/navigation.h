#pragma once
// Where characters can walk, and the way from one place to another (like Unreal's Nav Mesh and AI Move To).
// The level's still bodies are sampled on a grid (several floors on top of each other); a cell is walkable when its floor
// is not too steep and a character (radius, height) fits above it. Ways are found with A* and straightened.
#include <vector>
#include <glm/glm.hpp>
#include "project.h"

namespace pge {
class Physics;
class NavGrid {
public:
  void clear();
  // samples the still bodies of a physics world between mn and mx; cell = how fine (it gets coarser for big areas)
  void build(const Physics& ph, glm::vec3 mn, glm::vec3 mx, const NavSettings& s);
  bool ready() const { return nx_ > 0; }
  int walkable() const { return (int)ys_.size(); }
  float buildMs() const { return ms_; }
  float cell() const { return cs_; }
  // the way from one place to another: points on the floor, the first is where you are, the last the goal (or the
  // nearest walkable place to it); false = no way there
  bool findPath(const glm::vec3& from, const glm::vec3& to, std::vector<glm::vec3>& out) const;
  bool nearest(const glm::vec3& p, glm::vec3& onFloor, float maxDist = 3) const;
  void lines(std::vector<glm::vec3>& out) const;   // the walkable cells as squares (pairs of points), for showing
private:
  int nx_ = 0, nz_ = 0; float cs_ = 0.5f, step_ = 0.45f, ms_ = 0; glm::vec3 org_{0};
  std::vector<int> col_;      // first node of each column (nx * nz + 1)
  std::vector<float> ys_;     // the floors (nodes), each column's from the top down
  int node(int ix, int iz, float y, float tol) const;   // the floor of a column near height y, -1 = none
  glm::vec3 pos(int n) const;
  int columnOf(int n) const;
  bool straight(const glm::vec3& a, const glm::vec3& b) const;   // walkable all the way along the line
};
}
