#pragma once
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <memory>
#include <map>
#include "mesh.h"
#include "project.h"
#include "assets.h"

namespace pge {
struct Camera { glm::mat4 view{1}, proj{1}; glm::vec3 pos{0}; bool ortho = false; };
struct DrawItem { const GpuMesh* mesh = nullptr; glm::mat4 model{1}; glm::vec3 color{1}, emissive{0}; float metal = 0, rough = 0.6f, opacity = 1; bool castShadow = true, unlit = false, pickOnly = false; unsigned id = 0;   // pickOnly: invisible, only for clicking (editor icons)
  // textures (GL names, 0 = none): base colour (sRGB, alpha), roughness (green), metal (blue), normal, ambient occlusion (red), emissive (sRGB)
  unsigned tex[6] = {0, 0, 0, 0, 0, 0}; glm::vec2 tiling{1}; float normalScale = 1, aoStrength = 1, cutoff = -1;   // cutoff >= 0: cut out where alpha is below it
  bool vcolor = false, worldUV = false, flipGreen = false, blend = false, linear = false, decal = false;   // decal: drawn over the surface under it (no z-fighting)   // linear: color / emissive are linear light (glTF), not screen colours;   // vertex colours (models), textures laid on the world (no stretching), DirectX normal map, see-through
  const glm::mat4* bones = nullptr; int boneCount = 0;
  // a landscape: 4 painted layers blended by the vertex colour (their colour, picture laid on the world every `tile` m, roughness)
  bool splat = false; unsigned layerTex[4] = {0, 0, 0, 0}; glm::vec3 layerCol[4] = {glm::vec3(1), glm::vec3(1), glm::vec3(1), glm::vec3(1)}; float layerTile[4] = {4, 4, 4, 4}, layerRough[4] = {0.9f, 0.9f, 0.9f, 0.9f}; };   // a skinned model: its joints (model space), model = the actor
struct LightItem { int type = 1; glm::vec3 pos{0}, dir{0, -1, 0}, color{1}; float intensity = 1, range = 10, cosOuter = 0.8f; bool shadows = false; };
struct LineItem { const GpuMesh* mesh = nullptr; glm::mat4 model{1}; glm::vec4 color{1}; bool onTop = false, wire = false; unsigned id = 0; };   // wire: draw a triangle mesh as wireframe
struct ProbeItem { glm::vec3 pos{0}; float radius = 10, intensity = 1; unsigned id = 0; };   // a Sphere Reflection Capture (id: for the editor's clicks)
struct TextItem { std::string text; glm::mat4 model{1}; glm::vec3 color{1}; float size = 0.6f; unsigned id = 0; };   // camera-independent text in the XY plane, centred
enum ViewMode { VM_Lit = 0, VM_Unlit, VM_Wire, VM_Normals };
struct VoxelDraw { unsigned vao = 0; int count = 0; glm::vec3 offset{0}, mn{0}, mx{0}; bool visible = true, castShadow = true; glm::mat4 model{1}; glm::vec3 tint{1}; };   // one chunk of the block world, or a part of a model (model matrix, colour tint); world bounds; not visible = only casts shadows
struct Frame {
  std::vector<VoxelDraw> voxels, voxelWater; unsigned voxelAtlas = 0; int voxelTiles = 0;   // block world: solid chunks, liquid surfaces
  float time = 0;   // seconds (moving water)
  std::vector<DrawItem> draws; std::vector<LightItem> lights; std::vector<LineItem> lines; std::vector<TextItem> texts;
  WorldSettings world; bool fog = false; int viewMode = VM_Lit; bool grid = false; glm::vec3 shadowCenter{0};
  std::vector<ProbeItem> probes; size_t probeVersion = 0;   // reflection captures; a new version captures them again (the editor: the level changed)
  std::vector<ModelRef> keep; std::vector<std::shared_ptr<std::vector<glm::mat4>>> bones;   // models drawn this frame and their joints (DrawItem points into them)
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
  unsigned sky_ = 0;   // the sky behind everything (world.setSky skyTop / sun / moon / stars)
  unsigned postVao_ = 0, ssao_ = 0, aoBlur_ = 0, bloomPre_ = 0, bloomDown_ = 0, bloomUp_ = 0, compose_ = 0;
  glm::mat4 casc_[4]; float split_[4] = {0, 0, 0, 0}, texel_[4] = {0, 0, 0, 0}; int cascN_ = 0;
  void ensurePost(int w, int h);
  void freePost();
  unsigned boneTex_ = 0; void bindBones(unsigned prog, const DrawItem& it);
  // reflection captures: a cube picture of the scene around each one (mip levels = rougher), made once (one a frame)
  struct Probe { unsigned tex = 0; bool ready = false; size_t version = 0; }; std::map<std::string, Probe> probes_; RenderTarget capRT_; unsigned capFbo_ = 0; bool capturing_ = false;
  void captureProbes(const Frame& f);
  static std::string probeKey(const ProbeItem& p);
public:
  int probesReady() const { int n = 0; for (auto& [k, p] : probes_) n += p.ready; return n; }
private:
  unsigned lit_ = 0, vox_ = 0, wat_ = 0, flat_ = 0, depth_ = 0, vdepth_ = 0, idp_ = 0, text_ = 0, shadowFbo_ = 0, shadowTex_ = 0, textVao_ = 0, textVbo_ = 0;
  int buildText(const TextItem& t);   // fills textVbo_, returns vertex count
  GpuMesh wireBox_, wireCapsule_, arrow_, axes_, wireCam_, grid_, grid10_;
  void shadowPass(const Frame& f, const Camera& cam, bool& has);
};
glm::mat4 actorLocal(const Actor& a);
}
