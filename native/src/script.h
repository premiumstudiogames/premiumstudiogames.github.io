#pragma once
// Game scripts are JavaScript (QuickJS). Same API as Premium Engine 0.1:
//   self (the actor), world, input, log(...), math — events onStart, onUpdate(dt), onOverlap(other), onHit(other), onKey(code)
#include <string>
#include <vector>
#include <memory>
#include <glm/glm.hpp>

namespace pge {
class VoxelWorld;
// what scripts can do to the game (implemented by Game)
class ScriptWorld {
public:
  virtual ~ScriptWorld() = default;
  virtual bool exists(const std::string& id) = 0;
  virtual glm::vec3 getVec(const std::string& id, char field) = 0;              // 'p' position, 'r' rotation (radians), 's' scale, 'v' velocity
  virtual void setVec(const std::string& id, char field, const glm::vec3& v) = 0;
  virtual bool getVisible(const std::string& id) = 0;
  virtual void setVisible(const std::string& id, bool v) = 0;
  virtual void setColor(const std::string& id, const std::string& hex) = 0;
  virtual void setText(const std::string& id, const std::string& t) = 0;
  virtual void addForce(const std::string& id, const glm::vec3& f) = 0;
  virtual void lookAt(const std::string& id, const glm::vec3& target) = 0;
  virtual glm::vec3 worldPos(const std::string& id) = 0;
  virtual void destroyActor(const std::string& id) = 0;
  virtual std::string actorInfo(const std::string& id) = 0;                  // JSON {name, type, tags}
  virtual std::string find(const std::string& name) = 0;                      // id or ""
  virtual std::vector<std::string> findAll(const std::string& tag) = 0;
  virtual std::string spawn(const std::string& mesh, const std::string& optsJson) = 0;
  virtual void message(const std::string& t, float sec) = 0;
  virtual void hud(const std::string& id, const std::string& t) = 0;
  virtual void restart() = 0;
  virtual void setGravity(float y) = 0;
  virtual double time() = 0;
  virtual glm::vec3 playerPos() = 0;
  virtual void playerTeleport(const glm::vec3& p) = 0;
  virtual void playerLaunch(const glm::vec3& v) = 0;
  virtual bool key(const std::string& k) = 0;
  virtual bool pressed(const std::string& k) = 0;
  virtual glm::vec4 mouse() = 0;                                               // dx, dy, left, right
  virtual void log(const std::string& t) = 0;
  // block world (BLOCKFALL style); nullptr until a script calls voxels.create()
  virtual VoxelWorld* voxels() { return nullptr; }
  virtual std::string createVoxels(const std::string& optsJson) { (void)optsJson; return "no block world in this game"; }   // "" = ok
  virtual std::string assetPath(const std::string& p) { return p; }
  virtual glm::vec3 cameraPos() { return glm::vec3(0); }
  virtual glm::vec3 cameraDir() { return glm::vec3(0, 0, -1); }
  virtual bool playerSwimming() { return false; }
  virtual std::string savePath(const std::string& name) { return name; }   // Documents/PremiumEngine/Saves/<game>/<name>
  // Premium Games: a trophy for the player's account (only when the game was started from the Premium Games store)
  virtual void trophy(const std::string& id) { log("[trophy] " + id); }
};

enum ScriptEvent { EV_Start = 0, EV_Update, EV_Overlap, EV_Hit, EV_Key, EV_COUNT };

class ScriptHost {
public:
  ScriptHost();
  ~ScriptHost();
  bool init(ScriptWorld* w);
  void shutdown();
  // compile a script for an actor; returns a behaviour index or -1 (the error is logged)
  int attach(const std::string& actorId, const std::string& actorName, const std::string& scriptName, const std::string& code);
  bool has(int b, ScriptEvent e) const;
  // arg: for onUpdate a number, for onOverlap/onHit an actor id ("" = nothing, "@player" = the player), for onKey a key code
  void call(int b, ScriptEvent e, double num = 0, const std::string& arg = "");
  void drop(int b);                                     // actor destroyed
  const std::string& actorOf(int b) const;
  int count() const;
  void runJobs();
  // syntax check for the editor; returns "" when fine
  static std::string check(const std::string& code);
  struct Impl;
private:
  std::unique_ptr<Impl> d;
};
std::string keyAlias(const std::string& k);   // "w" → "KeyW", "space" → "Space", ...
}
