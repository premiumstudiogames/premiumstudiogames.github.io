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
struct VoxelDraw { unsigned vao = 0; int count = 0; glm::vec3 offset{0}, mn{0}, mx{0}; bool visible = true, castShadow = true; glm::mat4 model{1}; glm::vec3 tint{1}; };   // one chunk of the block world, or a part of a model (model matrix, colour tint); world bounds; not visible = only casts shadows
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
  // picture quality (the console can lower it): MSAA samples (0 = off), ambient occlusion, bloom, how far sun shadows reach
  struct Quality { int msaa = 4; bool ssao = true, bloom = true; float aoAmount = 0.8f, aoRadius = 0.6f, bloomAmount = 0.07f, bloomThreshold = 1.0f, shadowDistance = 160.f; } quality;
private:
  static constexpr int SHADOW_SIZE = 2048;   // one cascade; the shadow texture holds 2 x 2
  struct Post { int w = 0, h = 0, samples = 0, want = -1; unsigned msFbo = 0, msColor = 0, msDepth = 0, hdrFbo = 0, hdrTex = 0, depthTex = 0, aoFbo[2] = {0, 0}, aoTex[2] = {0, 0};
    unsigned bloomFbo[6] = {0}, bloomTex[6] = {0}; int bloomN = 0, bw[6] = {0}, bh[6] = {0}; } post_;
  unsigned postVao_ = 0, ssao_ = 0, aoBlur_ = 0, bloomPre_ = 0, bloomDown_ = 0, bloomUp_ = 0, compose_ = 0;
  glm::mat4 casc_[4]; float split_[4] = {0, 0, 0, 0}, texel_[4] = {0, 0, 0, 0}; int cascN_ = 0;
  void ensurePost(int w, int h);
  void freePost();
  unsigned lit_ = 0, vox_ = 0, wat_ = 0, flat_ = 0, depth_ = 0, vdepth_ = 0, idp_ = 0, text_ = 0, shadowFbo_ = 0, shadowTex_ = 0, textVao_ = 0, textVbo_ = 0;
  int buildText(const TextItem& t);   // fills textVbo_, returns vertex count
  GpuMesh wireBox_, wireCapsule_, arrow_, axes_, wireCam_, grid_, grid10_;
  void shadowPass(const Frame& f, const Camera& cam, bool& has);
};
glm::mat4 actorLocal(const Actor& a);
}
