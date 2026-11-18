#pragma once
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include "mesh.h"
#include "project.h"

namespace pge {
struct Camera { glm::mat4 view{1}, proj{1}; glm::vec3 pos{0}; bool ortho = false; };
struct DrawItem { const GpuMesh* mesh = nullptr; glm::mat4 model{1}; glm::vec3 color{1}, emissive{0}; float metal = 0, rough = 0.6f, opacity = 1; bool castShadow = true, unlit = false, pickOnly = false; unsigned id = 0; };   // pickOnly: invisible, only for clicking (editor icons)
struct LightItem { int type = 1; glm::vec3 pos{0}, dir{0, -1, 0}, color{1}; float intensity = 1, range = 10, cosOuter = 0.8f; bool shadows = false; };
struct LineItem { const GpuMesh* mesh = nullptr; glm::mat4 model{1}; glm::vec4 color{1}; bool onTop = false, wire = false; unsigned id = 0; };   // wire: draw a triangle mesh as wireframe
struct TextItem { std::string text; glm::mat4 model{1}; glm::vec3 color{1}; float size = 0.6f; unsigned id = 0; };   // camera-independent text in the XY plane, centred
enum ViewMode { VM_Lit = 0, VM_Unlit, VM_Wire, VM_Normals };
struct VoxelDraw { unsigned vao = 0; int count = 0; glm::vec3 offset{0}, mn{0}, mx{0}; bool visible = true; };   // one chunk of the block world (world bounds; not visible = only casts shadows)
struct Frame {
  std::vector<VoxelDraw> voxels, voxelWater; unsigned voxelAtlas = 0; int voxelTiles = 0;   // block world: solid chunks, liquid surfaces
  float time = 0;   // seconds (moving water)
  std::vector<DrawItem> draws; std::vector<LightItem> lights; std::vector<LineItem> lines; std::vector<TextItem> texts;
  WorldSettings world; bool fog = false; int viewMode = VM_Lit; bool grid = false; glm::vec3 shadowCenter{0};
};
struct RenderTarget { unsigned fbo = 0, color = 0, depth = 0, idFbo = 0, idTex = 0, idDepth = 0; int w = 0, h = 0; };

class Renderer {
public:
  bool init();
  void shutdown();
  // draw the frame into a target (0 = window) of size w×h
  void render(const Frame& f, const Camera& cam, int w, int h, const RenderTarget* rt = nullptr);
  // which actor id is under pixel (x, y) (top-left origin); 0 = none
  unsigned pick(const Frame& f, const Camera& cam, RenderTarget& rt, int x, int y);
  void resizeTarget(RenderTarget& rt, int w, int h);
  void freeTarget(RenderTarget& rt);
  const GpuMesh& wireBox() const { return wireBox_; }
  const GpuMesh& wireCapsule() const { return wireCapsule_; }
  const GpuMesh& arrow() const { return arrow_; }
  const GpuMesh& axes() const { return axes_; }
  const GpuMesh& wireCam() const { return wireCam_; }
  const GpuMesh& gridMesh() const { return grid_; }
  int drawCalls = 0, triangles = 0;
private:
  unsigned lit_ = 0, vox_ = 0, wat_ = 0, flat_ = 0, depth_ = 0, idp_ = 0, text_ = 0, shadowFbo_ = 0, shadowTex_ = 0, textVao_ = 0, textVbo_ = 0;
  int buildText(const TextItem& t);   // fills textVbo_, returns vertex count
  GpuMesh wireBox_, wireCapsule_, arrow_, axes_, wireCam_, grid_, grid10_;
  void shadowPass(const Frame& f, glm::mat4& lightVP, bool& has);
};
glm::mat4 actorLocal(const Actor& a);
}
