#pragma once
// The running game: used by Play In Editor and by packaged games (PremiumEnginePlayer).
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <set>
#include <unordered_map>
#include <SDL.h>
#include "project.h"
#include "renderer.h"
#include "physics.h"
#include "script.h"
#include "voxel.h"

struct ImDrawList;
namespace pge {

glm::mat4 trsMatrix(const glm::vec3& p, const glm::vec3& rRadYXZ, const glm::vec3& s);
glm::vec3 eulerYXZ(const glm::quat& q);            // radians
glm::quat quatYXZ(const glm::vec3& r);             // radians
// adds what an actor looks like (mesh / light / text) to a frame; editor = also icons for lights, triggers, ...
void addActorVisual(Frame& f, const Project& p, const Actor& a, const glm::mat4& world, bool editor, unsigned pickId,
                    const glm::vec3* colorOverride = nullptr, const std::string* textOverride = nullptr, const Renderer* r = nullptr);
std::string keyCode(SDL_Scancode sc);              // SDL scancode → "KeyW", "Space", ...

class Game : public ScriptWorld {
public:
  Game(const Project& p, const std::string& levelName);
  ~Game() override;
  bool start();
  void event(const SDL_Event& e);                  // keyboard + mouse for the game
  void update(float dt);
  void buildFrame(Frame& f, Camera& cam, float aspect) const;
  void drawHud(ImDrawList* dl, float x, float y, float w, float h) const;
  void releaseInput();                              // forget held keys (focus lost)
  bool wantsMouseLock() const { return mode_ != "Spectator"; }
  const std::string& mode() const { return mode_; }
  double gameTime() const { return time_; }
  int actorCount() const;
  bool loading() const { return vox_ && !vox_->ready(const_cast<Game*>(this)->playerPos(), 1.5f); }   // the block world around the player is still being built
  int physicsBodies() const { return phys_ ? phys_->bodyCount() : 0; }
  std::string levelName;

  // ScriptWorld
  bool exists(const std::string& id) override;
  glm::vec3 getVec(const std::string& id, char f) override;
  void setVec(const std::string& id, char f, const glm::vec3& v) override;
  bool getVisible(const std::string& id) override;
  void setVisible(const std::string& id, bool v) override;
  void setColor(const std::string& id, const std::string& hex) override;
  void setText(const std::string& id, const std::string& t) override;
  void addForce(const std::string& id, const glm::vec3& f) override;
  void lookAt(const std::string& id, const glm::vec3& t) override;
  glm::vec3 worldPos(const std::string& id) override;
  void destroyActor(const std::string& id) override;
  std::string actorInfo(const std::string& id) override;
  std::string find(const std::string& name) override;
  std::vector<std::string> findAll(const std::string& tag) override;
  std::string spawn(const std::string& mesh, const std::string& opts) override;
  void message(const std::string& t, float sec) override;
  void hud(const std::string& id, const std::string& t) override;
  void restart() override { restartRequested_ = true; }
  void setGravity(float y) override;
  double time() override { return time_; }
  glm::vec3 playerPos() override;
  void playerTeleport(const glm::vec3& p) override;
  void playerLaunch(const glm::vec3& v) override;
  bool key(const std::string& k) override { return keys_.count(k) > 0; }
  bool pressed(const std::string& k) override { return pressed_.count(k) > 0; }
  glm::vec4 mouse() override { return mouseState_; }
  void log(const std::string& t) override;
  VoxelWorld* voxels() override { return vox_.get(); }
  std::string createVoxels(const std::string& optsJson) override;
  std::string assetPath(const std::string& p) override;
  glm::vec3 cameraPos() override;
  glm::vec3 cameraDir() override;
  bool playerSwimming() override { return swimming_; }
  std::string savePath(const std::string& name) override;

private:
  struct Obj {
    Actor a; glm::vec3 p{0}, r{0}, s{1}; bool visible = true, alive = true, hasColor = false, inside = false;
    glm::vec3 color{1}; uint32_t body = NO_BODY; Motion motion = Motion::Static; int beh = -1;
    glm::mat4 lastWorld{1}; glm::vec3 bodyScale{1};
  };
  Project project_; Level level_;
  std::vector<std::unique_ptr<Obj>> objs_; std::unordered_map<std::string, Obj*> byId_;
  std::unique_ptr<Physics> phys_; std::unique_ptr<ScriptHost> scripts_; std::unique_ptr<VoxelWorld> vox_; bool voxHold_ = false, swimming_ = false; glm::vec3 voxHoldPos_{0}; float gravity_ = -9.81f;
  std::string mode_; uint32_t player_ = NO_BODY; float yaw_ = 0, pitch_ = 0; glm::vec3 specPos_{0, 2, 5}, moveDir_{0};
  double time_ = 0; bool restartRequested_ = false, started_ = false;
  std::set<std::string> keys_, pressed_; glm::vec4 mouseState_{0}; float lookDx_ = 0, lookDy_ = 0;
  std::string msg_; double msgUntil_ = 0; std::vector<std::pair<std::string, std::string>> hud_;
  std::vector<std::pair<std::string, std::string>> pendingKeys_;

  void clear();
  Obj* get(const std::string& id) const;
  glm::mat4 world(const Obj& o) const;
  void addBody(Obj& o);
  void respawnPlayer();
  glm::vec3 playerStart(float* yawDeg) const;
  void destroyRec(const std::string& id);
};
}
