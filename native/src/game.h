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
#include "net.h"
#include "script.h"
#include "voxel.h"
#include "ui.h"
#include "audio.h"
#include "models.h"
#include "navigation.h"
#include "net.h"
#include "assets.h"
#include <nlohmann/json.hpp>
#include <SDL.h>

struct ImDrawList;
namespace pge {

glm::mat4 trsMatrix(const glm::vec3& p, const glm::vec3& rRadYXZ, const glm::vec3& s);
glm::vec3 eulerYXZ(const glm::quat& q);            // radians
glm::quat quatYXZ(const glm::vec3& r);             // radians
// adds what an actor looks like (mesh / light / text) to a frame; editor = also icons for lights, triggers, ...
void addActorVisual(Frame& f, const Project& p, const Actor& a, const glm::mat4& world, bool editor, unsigned pickId,
                    const glm::vec3* colorOverride = nullptr, const std::string* textOverride = nullptr, const Renderer* r = nullptr, const AnimPlay* anim = nullptr);
// a material's colours, numbers and pictures on a draw
void applyMaterial(DrawItem& d, const Material& m);
// a car's 4 wheels from its size and Vehicle settings (body space, m); one wheel's look (local: a Y cylinder → body space)
void vehicleWheels(const Actor& a, const glm::vec3& scale, std::vector<Physics::WheelDesc>& out);
void addWheelDraw(Frame& f, const Project& P, const Actor& a, const glm::mat4& bodyW, const glm::mat4& local, unsigned id);
// a Foliage actor's copies near the camera (instanced, in cells); cloth at rest (the editor)
void addFoliageDraws(Frame& f, const Project& P, const Actor& a, const glm::vec3& cam, unsigned id);
void clothRestMesh(const ClothProps& c, MeshData& m);
void fractureMesh(const MeshData& src, const glm::vec3& scale, int pieces, uint32_t seed, std::vector<MeshData>& out, std::vector<glm::vec3>& centers);
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
  bool wantsMouseLock() const { return mode_ != "Spectator" && !(ui_ && ui_->modal()); }   // a menu that is open frees the mouse
  bool ownsEscape() const { return ui_ && ui_->ownsEscape(); }   // the game's own menus use Escape (ui.escape = true)
  bool uiTyping() const { return ui_ && ui_->typing(); }
  bool paused() const { return ui_ && ui_->pausing(); }
  void setConsole(bool c) { console_ = c; if (ui_) ui_->setConsole(c); }   // the console version: text fields open the on-screen keyboard
  bool console() const { return console_; }
  const std::string& mode() const { return mode_; }
  const std::string& playerAnimation() const { return pAnimName_; }   // tests
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
  float axis(const std::string& name) override;
  bool padConnected() override { return pad_ != nullptr; }
  void padSet(const std::string& json) override;
  void rumble(float low, float high, float seconds) override;
  void log(const std::string& t) override;
  VoxelWorld* voxels() override { return vox_.get(); }
  std::string createVoxels(const std::string& optsJson) override;
  std::string assetPath(const std::string& p) override;
  glm::vec3 cameraPos() override;
  glm::vec3 cameraDir() override;
  bool playerSwimming() override { return swimming_; }
  std::string savePath(const std::string& name) override;
  void trophy(const std::string& id) override;
  Ui* ui() override { return ui_.get(); }
  Audio* audio() override;
  Models* models() override { return models_.get(); }
  void pauseSound(bool on) { if (audio_) audio_->pause(on); }   // the engine's pause menu / the window lost focus
  std::string playerGet() override;
  const std::vector<std::pair<std::string, std::string>>& hudLines() const { return hud_; }
  void playerSet(const std::string& json) override;
  std::string storage(const std::string& op, const std::string& key, const std::string& val) override;
  std::string animation(const std::string& id, const std::string& op, const std::string& arg) override;
  std::string nav(const std::string& id, const std::string& op, const std::string& arg) override;
  const NavGrid& navigation() const { return nav_; }
  void setSky(const std::string& json) override;
  std::string postGet() override;
  void setQuality(const std::string& json) override { quality_ = json; qualityVer_++; }
  void setCamera(const std::string& json) override;   // world.setCamera({ target, distance, height }): behind a point, turned by the mouse; null: back to the player
  glm::vec3 followCam() const;
  const std::string& quality(int& ver) const { ver = qualityVer_; return quality_; }
  std::string net(const std::string& op, const std::string& a, const std::string& b) override;   // net.*: playing together (net.h)
  std::string gameplay(const std::string& op, const std::string& a, const std::string& b) override;   // gameplay.cpp
  const std::string& driving() const { return driving_; }   // the car the player is in ("" = none)
  int decalCount() const { return (int)decals_.size(); }
  float playerHealth() const { return health_; }
  void netPump();   // the network keeps going while the game is paused (the player calls it every frame)   // world.setSky: sky and fog colours, light, the sun's direction and colour (time of day, weather)

private:
  struct Obj {
    Actor a; glm::vec3 p{0}, r{0}, s{1}; bool visible = true, alive = true, hasColor = false, inside = false;
    glm::vec3 color{1}; uint32_t body = NO_BODY; Motion motion = Motion::Static; int beh = -1;
    glm::mat4 lastWorld{1}; glm::vec3 bodyScale{1};
    AnimPlay anim;   // a model's animation
    std::string bone;                 // attached to a bone of its parent's model
    bool welded = false;              // its collision is part of its parent's moving body
    uint32_t vehicle = NO_BODY; float vin[4] = {0, 0, 0, 0}; float steer = 0; double flipSince = -1;   // a car: the physics' vehicle, the input (throttle, steer, brake, handbrake)
    struct RagPart { int node; uint32_t body; glm::mat4 offset; };
    std::vector<RagPart> rag; glm::vec3 ragFrom{0};   // a ragdoll: a body per bone; where the actor is from its first part
    uint32_t joint = NO_BODY;         // a Physics Constraint's joint
    // animation: idle / walk / run by itself from how fast it moves (off once a script plays an animation), feet on the ground (IK)
    bool autoAnim = true, footIK = true; glm::vec3 prevP{0}; float moveSpeed = 0; bool autoOn = false;
    struct IkReq { std::string bone, actor; glm::vec3 target{0}; float weight = 1, maxAngle = 70; bool aim = false; };
    std::vector<IkReq> ikReq;
    // online (netsync.cpp): who moves it (0 the host, else a player's id); what came for it from the one who does
    int owner = 0; bool repl = false, netKin = false, netDriven = false; int remote = -1;   // remote: the avatar of that player
    struct NetT { glm::vec3 p{0}, v{0}; glm::quat q{1, 0, 0, 0}; double at = -1; } nt; glm::vec3 sentP{1e9f}; glm::quat sentQ{1, 0, 0, 0}; double sentAt = -1;
    std::vector<glm::mat4> netWheels;
    double dieAt = -1; float dmgTaken = 0;   // goes at this time (pieces of something broken); damage so far (Destructible)
    struct Cloth { bool on = false; int n = 0; std::vector<glm::vec3> p, prev; std::vector<char> pin; struct L { int a, b; float rest; }; std::vector<L> links; int version = 0; } cloth;
    struct Walk { bool active = false; std::vector<glm::vec3> path; size_t i = 1; float speed = 3.5f, accept = 0.5f, foot = 0; bool turn = true, player = false; std::string goal, result; glm::vec3 goalAt{0}; double repath = 0; } walk;   // AI Move To
  };
  NavGrid nav_; bool navBuilt_ = false;
  // gameplay (gameplay.cpp)
  struct Decal { glm::mat4 m{1}; std::string actor; uint32_t body = NO_BODY; const Texture* tex = nullptr; glm::vec3 color{1}; float rough = 0.6f, born = 0, life = 0, opacity = 1; };
  std::vector<Decal> decals_;
  struct Grab { uint32_t body = NO_BODY; glm::vec3 local{0}, target{0}; float strength = 1; bool targetSet = false; };
  std::map<int, Grab> grabs_; int nextGrab_ = 1;
  std::map<uint32_t, std::string> jointOwner_;   // joints made by Physics Constraint actors (their break events)
  std::string driving_; bool driveInside_ = false; double driveIdle_ = 0;
  float health_ = 100, maxHealth_ = 100; bool dead_ = false; double respawnAt_ = -1; std::string vehicleKey_ = "KeyE";
  uint64_t frameNo_ = 0; mutable glm::mat4 camW_{1}; mutable bool camSet_ = false;
  glm::mat4 playerMatrix() const;                                  // where the player stands, facing where they face
  glm::mat4 boneWorld(const Obj& o, const std::string& bone) const;   // a bone of a model actor (no scale)
  void cameraCompute(glm::vec3& pos, glm::mat4& view) const;
  void startGameplay();                                            // joints and vehicles of the level
  void gameplayFrame(float dt, bool post);                         // vehicles, grabs (before the physics step); joints, ragdolls, the driver, decals (after)
  glm::mat4 playerBone(const std::string& bone) const;
  void rebuildBody(Obj& o);
  std::string hint_;                                               // "[E] Drive", the car's speed
  void addVehicle(Obj& o);
  void vehicleLayout(const Obj& o, std::vector<Physics::WheelDesc>& w) const;
  bool enterVehicle(Obj& v); void exitVehicle();
  bool ragdoll(Obj& o, const glm::vec3& impulse, const glm::vec3& point, bool hasPoint);
  void damage(const std::string& target, float amount, nlohmann::json info);
  void addDecal(const nlohmann::json& o);
  void drawGameplay(Frame& f) const;
  std::string attach(Obj& o, const nlohmann::json& j);
  void detach(Obj& o, const nlohmann::json& j);
  Obj* objOfBody(uint32_t body) const;
  // Third Person: the player's character, its animations chosen by how it moves (idle, walk, run, jump, fall), turning where it goes
  std::string pModel_, pAnimName_; AnimPlay pAnim_; float charYaw_ = 0; void playerAnimFrame(float dt);
  std::vector<Obj::IkReq> pIk_;
  bool fracture(Obj& o, uint32_t seed, const glm::vec3& point, float impulse, bool hasPoint);   // fracture.cpp
  void clothStart(Obj& o); void clothFrame(float dt); void clothDraws(Frame& f) const;         // cloth.cpp
  std::vector<uint32_t> foliageBodies_; void foliageStart();
  // online: actors kept the same on every player's computer, the other players as characters (netsync.cpp)
  std::vector<NetEvent> netInbox_; double netSent_ = -1; std::string pendingEnter_; std::map<int, std::string> avatars_; bool netWas_ = false; bool fromNet_ = false;
  bool netOn() const; int netMe() const; bool owns(const Obj& o) const; bool replicated(const Obj& o) const;
  void netSend(const nlohmann::json& m, int to = -1, int except = -1);
  void netFrame(float dt); void netDrain(); void netHandle(const nlohmann::json& m, int from); void netFull(int to);
  nlohmann::json netSnap(const Obj& o) const; void netApply(Obj& o, const nlohmann::json& a);
  void netOwnership(); std::string netAvatar(int id, const nlohmann::json& m);                                                // IK on the player's character
  std::string animOp(Obj& o, const Model& m, const std::string& op, const std::string& arg);
  void animFrame(float dt);                                                    // animation.cpp: every model's animation, auto locomotion, IK
  void solveIK(const Model& m, AnimPlay& an, const glm::mat4& W, const std::vector<Obj::IkReq>& req, bool feet, float floorY, std::vector<uint32_t> ignore);
  void ensureNav();
  bool walkPath(Obj& o, const glm::vec3& goalFeet);
  void walkFinish(Obj& o, bool ok);
  void walkFrame(float dt);
  Project project_; Level level_;
  std::vector<std::unique_ptr<Obj>> objs_; std::unordered_map<std::string, Obj*> byId_;
  std::unique_ptr<Physics> phys_; std::unique_ptr<ScriptHost> scripts_; std::unique_ptr<VoxelWorld> vox_; bool voxHold_ = false, swimming_ = false; glm::vec3 voxHoldPos_{0}; float gravity_ = -9.81f;
  std::string mode_; uint32_t player_ = NO_BODY; float yaw_ = 0, pitch_ = 0; glm::vec3 specPos_{0, 2, 5}, moveDir_{0};
  double time_ = 0; bool restartRequested_ = false, started_ = false;
  std::set<std::string> keys_, pressed_; glm::vec4 mouseState_{0}; float lookDx_ = 0, lookDy_ = 0;
  std::set<std::string> trophies_; std::unique_ptr<Ui> ui_; std::unique_ptr<Audio> audio_; std::unique_ptr<Models> models_; bool wasModal_ = false; std::unique_ptr<Net> net_;
  // player movement (scripts can change it: world.player.set); the defaults are the engine's usual ones
  std::string quality_; int qualityVer_ = 0; float mouseSens_ = 1;
  bool camOn_ = false; glm::vec3 camTarget_{0}; float camDist_ = 6, camHeight_ = 0;
  bool sunSet_ = false; glm::vec3 sunDir_{0, -1, 0}, sunColor_{1}; float sunInt_ = 1;   // world.setSky({ sun })
  struct Tune { bool fly = false, doubleTapRun = false, locked = false; float walk = 5, run = 9, sneak = 2, jump = 5.2f, flySpeed = 10, flyRun = 1.6f, flyClimb = 6, gravity = 0, accel = 0;
    std::string runKey = "Shift", sneakKey = ""; } tune_; bool grounded_ = false, running_ = false, sneaking_ = false, wHeld_ = false, tapRun_ = false; double wTap_ = -1;
  std::string msg_; double msgUntil_ = 0; std::vector<std::pair<std::string, std::string>> hud_;
  std::vector<std::pair<std::string, std::string>> pendingKeys_;

  void clear();
  Obj* get(const std::string& id) const;
  glm::mat4 world(const Obj& o) const;
  void addBody(Obj& o);
  void respawnPlayer();
  glm::vec3 playerStart(float* yawDeg) const;
  void destroyRec(const std::string& id);
  void soundFrame();
  // the controller (the first one connected): buttons are keys (PadA, PadB, PadX, PadY, PadLB, PadRB, PadLT, PadRT, PadStart, PadBack,
  // PadLS, PadRS, PadUp, PadDown, PadLeft, PadRight); with aliases on A = Space, RT = MouseLeft, LT = MouseRight, LB = WheelUp,
  // RB = WheelDown, Start = Escape, so PC games play on a controller as they are
  SDL_GameController* pad_ = nullptr; int padId_ = -1; float ax_[6] = {0, 0, 0, 0, 0, 0}; bool trig_[2] = {false, false};
  bool console_ = false, padAliases_ = true, padRun_ = false, invertY_ = false; float lookSpeed_ = 3.0f;
  void padOpen(int index); void padClose();
  void padKey(const std::string& name, bool down);
  void keyDown(const std::string& k, bool fire);
};
}
