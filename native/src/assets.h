#pragma once
// The game's own files: models (glTF 2.0: .glb / .gltf, with their materials, textures, node tree, skins and animations)
// and textures (.png / .jpg / .tga / .bmp). Paths are relative to the project's folder (Models/Robot.glb); a packaged game
// finds them next to its program. Loaded once and shared; the editor reloads a file when it changes on disk (reimport).
#include <string>
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "mesh.h"

namespace pge {
struct Texture { unsigned id = 0; int w = 0, h = 0; bool alpha = false, ok = false; std::string error; };

void setAssetRoot(const std::string& projectDir);    // the open project's folder ("" = none)
const std::string& assetRoot();
std::string resolveAsset(const std::string& rel);    // the project's folder, next to the program, its assets folder; "" if not found
bool isModelFile(const std::string& mesh);           // ends with .glb / .gltf
bool isImageFile(const std::string& file);
// a texture from a file of the project (cached; sRGB for colours, linear for normal / roughness maps); nullptr = no file name
const Texture* texture(const std::string& file, bool srgb);
const Texture* pixelTexture(const std::string& name, int w, int h, const std::vector<unsigned char>& rgba, bool srgb);   // made by the engine (decals), cached by name

struct ModelMaterial {
  std::string name; glm::vec4 base{1}; float metal = 1, rough = 1, normalScale = 1, occlusion = 1, cutoff = 0.5f; glm::vec3 emissive{0};
  int alphaMode = 0;   // 0 opaque, 1 mask (cut out), 2 blend
  bool doubleSided = false;
  const Texture *baseTex = nullptr, *mrTex = nullptr, *normalTex = nullptr, *emissiveTex = nullptr, *occTex = nullptr;
};
struct ModelPrim { GpuMesh gpu; int material = -1, node = -1, skin = -1; bool colors = false; };
struct ModelNode { std::string name; int parent = -1, mesh = -1, skin = -1; glm::vec3 t{0}, s{1}; glm::quat r{1, 0, 0, 0}; std::vector<int> children; };
struct ModelSkin { std::vector<int> joints; std::vector<glm::mat4> inverseBind; };
struct AnimChannel { int node = -1, path = 0, interp = 1; std::vector<float> times; std::vector<glm::vec4> values; };   // path 0 move, 1 turn, 2 scale; interp 0 step, 1 linear, 2 cubic
struct ModelAnim { std::string name; float duration = 0; std::vector<AnimChannel> ch; };

struct Model {
  std::string file, error;                    // error: why it could not be loaded (the model draws as nothing)
  std::vector<ModelNode> nodes; std::vector<int> roots, meshPrims0, meshPrimsN;   // meshPrims0 / N: each glTF mesh's first primitive and count
  std::vector<ModelPrim> prims; std::vector<ModelMaterial> mats; std::vector<ModelSkin> skins; std::vector<ModelAnim> anims;
  std::vector<glm::mat4> rest;                // each node's place in the model at rest
  MeshData shape;                             // every triangle at rest, in the model's space (bounds, physics, snapping)
  int triangles = 0, vertices = 0, joints = 0;
  bool ok() const { return error.empty(); }
  int anim(const std::string& name) const;    // -1 = none
  ~Model();
};
using ModelRef = std::shared_ptr<const Model>;
ModelRef model(const std::string& file);      // cached; nullptr only when file is empty
bool reloadChangedAssets();                   // the editor: files changed on disk are loaded again (true = something changed)
void releaseAssets();                         // before the GL context goes away

// playing an animation (with a blend from the one before)
struct AnimPlay {
  int anim = -1; float time = 0, speed = 1; bool loop = true, playing = false;
  int from = -1; float fromTime = 0, blend = 0, blendTime = 0; bool fromLoop = true;
  void play(int a, bool loopIt, float spd, float blendSeconds);   // blendSeconds 0 = at once
  bool advance(const Model& m, float dt);     // true when it reached the end this step (not looping)
  std::vector<std::pair<int, glm::mat4>> fixed;   // nodes held where something else puts them (ragdoll parts): node → its place in the model
  std::vector<std::pair<int, glm::vec3>> scaled;  // nodes made bigger / smaller with what hangs on them (0 = gone: a limb shot off)
};
struct ModelPose { std::vector<glm::mat4> world; std::vector<glm::mat4> joints; std::vector<int> skinAt; };
void poseModel(const Model& m, const AnimPlay* a, ModelPose& out);   // a = nullptr: at rest

// what a mesh name is made of: a primitive (Cube, Sphere, ...) or a model file; cached
const MeshData& meshShape(const std::string& mesh);
}
