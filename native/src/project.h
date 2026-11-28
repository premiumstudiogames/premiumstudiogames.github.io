#pragma once
// Project data: levels, actors, materials and scripts. Saved as JSON (.pge.json), the same format as
// Premium Engine 0.1 (web), so projects move between the two.
#include <string>
#include <vector>
#include <map>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <memory>
#include "landscape.h"
#include "foliage.h"

namespace pge {

// a material: colours and numbers, and pictures (files of the project, e.g. Textures/Bricks_Color.png) that it multiplies them by.
// Roughness reads the green channel, metal the blue, ambient occlusion the red: one packed ORM picture can go in all three.
struct Material { std::string name = "M_Default"; glm::vec3 color{0.784f, 0.8f, 0.83f}; float metalness = 0, roughness = 0.6f; glm::vec3 emissive{0}; float emissiveIntensity = 1, opacity = 1;
  std::string baseMap, normalMap, roughnessMap, metalMap, aoMap, emissiveMap;
  glm::vec2 tiling{1}; float normalStrength = 1, aoStrength = 1, cutoff = 0.5f;
  bool worldAligned = false, directXNormal = false, masked = false;   // world aligned: the pictures are laid on the world (1 tile = 1 m / tiling), not stretched with the mesh
  bool hasMaps() const { return !baseMap.empty() || !normalMap.empty() || !roughnessMap.empty() || !metalMap.empty() || !aoMap.empty() || !emissiveMap.empty(); } };
// a script: code, or a Blueprint (its graph is kept; code = what the graph compiled to, so it runs like any script)
struct Script { std::string name, code; nlohmann::json graph; bool blueprint() const { return graph.is_object(); } };
struct LightProps { glm::vec3 color{1}; float intensity = 1, range = 12, angle = 35; bool shadows = true; };
struct TextProps { std::string value = "Text"; glm::vec3 color{1}; float size = 0.6f; };
struct PhysicsProps { bool simulate = false, collision = true; float mass = 1; bool replicates = false; };   // replicates: kept the same for every player of an online game (physics actors and cars always are)
struct ProbeProps { float radius = 10, intensity = 1; };   // Sphere Reflection Capture: what it reflects, within radius m
// a car (Unreal's Wheeled Vehicle): a Static Mesh with Simulate Physics drives on 4 wheels (placed from its size, or by wheel list)
struct VehicleProps {
  bool enabled = false, enterable = true;
  float torque = 650, maxRpm = 6000, maxSteer = 35, wheelRadius = 0.4f, wheelWidth = 0.3f, travel = 0.3f, stiffness = 1.5f, axleInset = 0.18f, camDistance = 7;
  int drive = 2;                                  // 0 rear, 1 front, 2 all wheels
  std::string wheelMesh = "Cylinder", wheelMaterial = "M_Tire";   // drawn at each wheel ("" = none: the model has its own)
  bool isDefault() const { return !enabled; }
};
// breaks into pieces (Unreal's Geometry Collection / Chaos Destruction): enough damage (or a hard hit, an explosion) and the
// mesh cracks into pieces that fly and fall; they go after debrisLife seconds (0 = stay)
struct DestructibleProps { bool enabled = false; int pieces = 10; float health = 50, debrisLife = 10; bool isDefault() const { return !enabled; } };
// cloth (a flag, a curtain, a cape): a sheet of points held at its top (or corners, left edge) that falls, sways in the wind and
// rests on what it touches; it moves with its actor (attach it to a character's bone: a cape)
struct ClothProps { float width = 2, height = 2; int res = 16; std::string pin = "top"; float stiffness = 1, wind = 0.5f, mass = 1; std::string material; glm::vec3 windDir{1, 0, 0.3f}; };
// Physics Constraint (Unreal's): holds two actors together (or one to the world): a door's hinge, a chain, a drawer, a rope
struct JointProps {
  std::string kind = "Hinge";                     // Hinge, Fixed, BallSocket, Slider, Distance
  std::string a, b;                               // actor ids (b "" = the world)
  bool limits = true; float min = -90, max = 90;  // Hinge degrees, Slider / Distance m
  float motorSpeed = 0, motorForce = 0, friction = 0, breakForce = 0;
};

struct Actor {
  std::string id, name, type, parent;           // type: StaticMesh, Empty, PointLight, SpotLight, DirectionalLight, PlayerStart, CameraActor, TriggerBox, TextRender, ReflectionCapture
  glm::vec3 p{0}, r{0}, s{1};                   // location (m), rotation (degrees, applied Y then X then Z), scale
  bool visible = true;
  std::vector<std::string> tags;
  std::string mesh = "Cube", material = "M_Default", script;   // mesh: a shape (Cube, Sphere, ...) or a model file of the project (Models/Robot.glb); material "" = the model's own
  std::string anim; bool animLoop = true; float animSpeed = 1;   // a model's animation that plays from the start
  PhysicsProps physics; LightProps light; TextProps text; ProbeProps probe; VehicleProps vehicle; JointProps joint; DestructibleProps destructible; ClothProps cloth;
  LandscapeRef land;   // a Landscape actor's heights and paint
  FoliageRef fol;      // a Foliage actor's painted copies
  bool isLight() const { return type == "PointLight" || type == "SpotLight" || type == "DirectionalLight"; }
};

// camera effects over the whole picture (like Unreal's Post Process Volume): 0 = off; saturation / contrast / bloom 1 = as is
struct PostProcess {
  float vignette = 0, grain = 0, chromatic = 0, scanlines = 0, vhs = 0, saturation = 1, contrast = 1, temperature = 0, bloom = 1; glm::vec3 tint{1};
  bool isDefault() const { return vignette == 0 && grain == 0 && chromatic == 0 && scanlines == 0 && vhs == 0 && saturation == 1 && contrast == 1 && temperature == 0 && bloom == 1 && tint == glm::vec3(1); }
};
// the characters that walk with AI Move To: their size, the highest step, the steepest slope; how fine the grid is (m)
struct NavSettings { float radius = 0.35f, height = 1.8f, step = 0.45f, maxSlope = 45, cell = 0.5f;
  bool isDefault() const { return radius == 0.35f && height == 1.8f && step == 0.45f && maxSlope == 45 && cell == 0.5f; } };
struct WorldSettings {
  std::string gameMode = "FirstPerson";
  glm::vec3 sky{0.56f, 0.72f, 0.88f}, skyLight{0.87f, 0.92f, 1.0f}, groundLight{0.23f, 0.25f, 0.29f}, fogColor{0.56f, 0.72f, 0.88f};
  float ambient = 0.8f, exposure = 1, gravity = -9.81f, killZ = -30, fogDensity = 0.01f, fov = 75;
  bool fog = false; PostProcess post; NavSettings nav;
  std::string playerModel;   // Third Person: the player's character ("" = the engine's mannequin, "Capsule", or a model of the project)
  // a sky drawn behind everything (scripts, world.setSky): colour at the top (sky is the horizon), the sun's disc and glow,
  // the moon, stars. Not saved with the level; skyTop.x < 0 = a flat sky of the sky colour (as before)
  glm::vec3 skyTop{-1, -1, -1}, sunDisc{0}, sunDiscDir{0, 1, 0}; float sunSize = 0.035f, moon = 0, stars = 0;
};

struct Level { std::vector<Actor> actors; WorldSettings world; };

struct Project {
  std::string id, name = "MyProject", templ = "Blank", description, startLevel = "Main", engine;
  std::vector<std::pair<std::string, Level>> levels;   // keeps order
  std::map<std::string, Material> materials;
  std::map<std::string, Script> scripts;
  std::string path;   // where it's saved on disk
  double created = 0, updated = 0;

  Level* level(const std::string& n) { for (auto& l : levels) if (l.first == n) return &l.second; return nullptr; }
  const Level* level(const std::string& n) const { for (auto& l : levels) if (l.first == n) return &l.second; return nullptr; }
};

extern const char* MESHES[];
extern const int MESH_COUNT;
extern const char* ACTOR_TYPES[];
extern const int ACTOR_TYPE_COUNT;
const char* actorTypeLabel(const std::string& type);
const char* actorTypeIcon(const std::string& type);

std::string newId(const char* prefix);
Actor newActor(const std::string& type, const std::string& name = "");
Project makeTemplate(const std::string& kind, const std::string& name);
extern const char* SCRIPT_TEMPLATE;

std::string toJson(const Project& p, int indent = 1);
bool fromJson(const std::string& text, Project& out, std::string* err = nullptr);
bool saveProjectFile(Project& p, const std::string& file, std::string* err = nullptr);
bool loadProjectFile(const std::string& file, Project& out, std::string* err = nullptr);
std::string levelToJson(const Level& l);
bool levelFromJson(const std::string& text, Level& out);

// folders
std::string documentsDir();              // ~/Documents/PremiumEngine
void setDocumentsDir(const std::string& dir);   // the console version keeps its projects, builds, saves and settings in the console's own storage
std::string projectsDir();
std::string buildsDir();
std::string exeDir();
std::string sanitizeName(const std::string& s);
std::string colorHex(const glm::vec3& c);
glm::vec3 hexColor(const std::string& s, glm::vec3 fallback = glm::vec3(1));
double nowMs();
}
