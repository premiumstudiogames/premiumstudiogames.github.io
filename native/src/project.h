#pragma once
// Project data: levels, actors, materials and scripts. Saved as JSON (.pge.json), the same format as
// Premium Engine 0.1 (web), so projects move between the two.
#include <string>
#include <vector>
#include <map>
#include <glm/glm.hpp>

namespace pge {

struct Material { std::string name = "M_Default"; glm::vec3 color{0.784f, 0.8f, 0.83f}; float metalness = 0, roughness = 0.6f; glm::vec3 emissive{0}; float emissiveIntensity = 1, opacity = 1; };
struct Script { std::string name, code; };
struct LightProps { glm::vec3 color{1}; float intensity = 1, range = 12, angle = 35; bool shadows = true; };
struct TextProps { std::string value = "Text"; glm::vec3 color{1}; float size = 0.6f; };
struct PhysicsProps { bool simulate = false, collision = true; float mass = 1; };

struct Actor {
  std::string id, name, type, parent;           // type: StaticMesh, Empty, PointLight, SpotLight, DirectionalLight, PlayerStart, CameraActor, TriggerBox, TextRender
  glm::vec3 p{0}, r{0}, s{1};                   // location (m), rotation (degrees, applied Y then X then Z), scale
  bool visible = true;
  std::vector<std::string> tags;
  std::string mesh = "Cube", material = "M_Default", script;
  PhysicsProps physics; LightProps light; TextProps text;
  bool isLight() const { return type == "PointLight" || type == "SpotLight" || type == "DirectionalLight"; }
};

struct WorldSettings {
  std::string gameMode = "FirstPerson";
  glm::vec3 sky{0.56f, 0.72f, 0.88f}, skyLight{0.87f, 0.92f, 1.0f}, groundLight{0.23f, 0.25f, 0.29f}, fogColor{0.56f, 0.72f, 0.88f};
  float ambient = 0.8f, exposure = 1, gravity = -9.81f, killZ = -30, fogDensity = 0.01f, fov = 75;
  bool fog = false;
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
