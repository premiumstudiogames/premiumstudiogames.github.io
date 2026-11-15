#pragma once
#include <vector>
#include <string>
#include <glm/glm.hpp>

namespace pge {
struct Vertex { glm::vec3 p, n; };
struct MeshData { std::vector<Vertex> v; std::vector<unsigned> i; glm::vec3 bmin{-0.5f}, bmax{0.5f}; };
struct GpuMesh { unsigned vao = 0, vbo = 0, ebo = 0; int count = 0; bool lines = false; glm::vec3 bmin{-0.5f}, bmax{0.5f}; };

MeshData makePrimitive(const std::string& name);   // Cube, Sphere, Cylinder, Cone, Plane, Capsule, Torus, Wedge (unit size)
GpuMesh upload(const MeshData& m, bool lines = false);
GpuMesh uploadLines(const std::vector<glm::vec3>& pts);   // pairs of points
void destroy(GpuMesh& m);
const GpuMesh& primitive(const std::string& name);       // cached GPU meshes
void releasePrimitives();
}
