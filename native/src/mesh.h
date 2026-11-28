#pragma once
#include <vector>
#include <string>
#include <glm/glm.hpp>

namespace pge {
struct Vertex { glm::vec3 p, n; glm::vec2 uv{0}; };   // uv: texture coordinates (1 = the texture once across a face)
struct MeshData { std::vector<Vertex> v; std::vector<unsigned> i; glm::vec3 bmin{-0.5f}, bmax{0.5f}; };
struct GpuMesh { unsigned vao = 0, vbo = 0, ebo = 0; int count = 0; bool lines = false; glm::vec3 bmin{-0.5f}, bmax{0.5f}; };

MeshData makePrimitive(const std::string& name);   // Cube, Sphere, Cylinder, Cone, Plane, Capsule, Torus, Wedge (unit size)
GpuMesh upload(const MeshData& m, bool lines = false);
GpuMesh uploadLines(const std::vector<glm::vec3>& pts);   // pairs of points
void destroy(GpuMesh& m);
const GpuMesh& primitive(const std::string& name);       // cached GPU meshes (also meshes made at run time, below)
// meshes made by the engine while a game runs (the pieces of something broken, cloth): "@name"; drawn, collided, and gone with forgetMeshes
void registerMesh(const std::string& name, const MeshData& m);
const MeshData* registeredMesh(const std::string& name);
void forgetMeshes(const std::string& prefix);
GpuMesh& dynamicMesh(const std::string& name, const MeshData& m);   // made or updated in place (cloth moves every frame)
void releasePrimitives();
}
