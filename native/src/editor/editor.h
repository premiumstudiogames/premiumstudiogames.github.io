#pragma once
// Premium Engine editor. Menus and panels follow Unreal Engine 5's level editor:
// File, Edit, Window, Tools, Build, Select, Actor, Help · toolbar · Viewport · Outliner · Details · Content Browser · Output Log.
#include <string>
#include <vector>
#include <deque>
#include <memory>
#include <map>
#include <functional>
#include "app.h"
#include "project.h"
#include "renderer.h"
#include "game.h"
#include "navigation.h"
#include "bp_editor.h"

class TextEditor;
namespace pge {

struct ScriptTab { std::string id; std::unique_ptr<TextEditor> ed; std::string savedText, error; int errorLine = 0; bool focus = false; };
struct Prefs { bool autosave = true; int autosaveMinutes = 2; bool invertY = false; float camSpeed = 4; bool showFps = false; float uiScale = 1; int bpLang = 0; };   // bpLang: Blueprint names 0 English, 1 Arabic, 2 both
struct BpTab;

class Editor {
public:
  explicit Editor(AppWindow& w);
  ~Editor();
  bool init();
  void frame();                      // one UI frame
  void event(const SDL_Event& e);    // raw SDL events (before ImGui gets them)
  bool wantsQuit() const { return quit_; }
  bool console = false;              // the console version: full screen, run with the controller (on-screen keyboard), its own projects
  void requestQuit();
  bool openProjectFile(const std::string& file);

  // ---- state shared by the panels
  AppWindow& win; Renderer R;
  Project P; bool hasProject = false; std::string level; std::string sel;
  bool dirty = false;
  // viewport
  enum class View { Perspective, Top, Front, Right } view = View::Perspective;
  int viewMode = VM_Lit; bool showGrid = true, showIcons = true, showStats = false, showNav = false;   // showNav (P): where AI can walk
  size_t levelHash_ = 0, probeBump_ = 0; double levelHashAt_ = -1;   // reflection captures are made again when the level changes
  // Landscape mode: sculpt / paint with the mouse on the selected Landscape (Details > Landscape)
  int landTool = -1, landLayer = 0; float landRadius = 6, landStrength = 1, landFlat = 0, hillsH = 8, hillsScale = 40; bool landDown = false;
  bool landscapeBrush(const Camera& C, ImVec2 pos, ImVec2 size, ImDrawList* dl);   // true = the mouse is used by the brush
  NavGrid navPreview; GpuMesh navLines; size_t navKey_ = 0; double navAt_ = -1; void navShow(Frame& f);
  glm::vec3 camPos{8, 6, 14}; float camYaw = 0.5f, camPitch = -0.3f, orthoZoom = 20; glm::vec3 orthoCenter{0};
  int gizmoOp = 0; bool gizmoLocal = false; bool snapT = true, snapR = true, snapS = false; float snapTv = 0.5f, snapRv = 15, snapSv = 0.25f;
  RenderTarget vpRT; float vpX = 0, vpY = 0, vpW = 1, vpH = 1; bool vpHovered = false, vpFocused = false, flying = false;
  // play in editor
  std::unique_ptr<Game> game; bool playing = false, mouseFree = false, consolePreview = false;   // play as the console version (controller, on-screen keyboard, premium.console)
  // windows
  bool wPlaceActors = false, wOutliner = true, wDetails = true, wWorld = true, wContent = true, wLog = true, wScripts = false, wMaterial = false;
  bool wPrefs = false, wProjectSettings = false, wStats = false, wApi = false, wDocs = false, wShortcuts = false, wAbout = false, wMapCheck = false, wPackage = false;
  bool wBlueprints = false, wNewBlueprint = false; std::vector<std::unique_ptr<BpTab>> bpTabs;   // the Blueprint Editor (bp_editor.cpp)
  bool wNewLevel = false, wOpenLevel = false, wNewScript = false, wNewMaterial = false, wRename = false, wImport = false;
  std::string materialOpen; std::vector<ScriptTab> tabs; std::string rename; std::string newName;
  std::vector<std::string> mapCheck; std::string packageResult, packageDir; bool packageOk = false;
  Prefs prefs; std::string olFilter, cbFilter, logFilter, paFilter, cbFolder = "Levels"; char cmd[256] = {0};
  std::string toast; double toastUntil = 0; bool resetLayout = false; bool focusLog = false;
  std::vector<std::string> recent; bool codeFocused = false, codeFocusedNext_ = false;

  // ---- actions
  void newProject(const std::string& templ, const std::string& name, std::string* err = nullptr);
  bool save();
  void saveThumbnail();                           // Saved/thumbnail.bmp next to the project (shown in Premium Games)
  void closeProject();
  Level& L();
  Actor* actor(const std::string& id);
  void select(const std::string& id) { sel = id; }
  void touch();                                   // something in the project changed (one undo step per gesture)
  void undo(); void redo(); bool canUndo() const { return !undo_.empty() || pending_; } bool canRedo() const { return !redo_.empty(); }
  void run(const std::string& cmd);               // menu / shortcut commands
  std::string shotFile;                           // save the next frame as a .bmp (self-test)
  void queue(const std::string& cmd) { queued_.push_back(cmd); }   // run at the start of the next frame
  Actor& addActor(const std::string& type, const std::string& mesh = "", const glm::vec3* at = nullptr);
  void deleteSelected(); void duplicateSelected(); void copySelected(); void paste(); void focusSelected(); void frameAll();
  void snapToFloor(); void attach(const std::string& child, const std::string& parent);
  glm::mat4 worldOf(const Actor& a);
  glm::vec3 dropPoint(float mx, float my);        // where something dragged into the viewport lands
  void play(); void stopPlay(); void playStandalone();
  void openLevel(const std::string& name); void newLevel(const std::string& name, bool templ);
  void openScript(const std::string& id); std::string newScript(const std::string& name);
  void openBlueprint(const std::string& id); std::string newBlueprint(const std::string& name); bool compileBlueprint(const std::string& id, bool report);
  std::string newMaterial(const std::string& name);
  void compileScripts(bool quiet); void runMapCheck(); void packageProject(bool console = false);
  void notify(const std::string& t);
  void savePrefs(); void loadPrefs(); void addRecent(const std::string& f);
  std::string uniqueName(const std::string& base);
  // the project's own files (assets_ui.cpp): models and pictures in its folder, importing, picture slots, a model's animation
  std::vector<std::string> projModels, projTextures; double filesScanned_ = -1, assetsChecked_ = 0; float animClock = 0; bool animPaused = false;   // animClock: the viewport's animations (paused: tests)
  void scanFiles(bool now = false);
  std::vector<std::string> importFiles(const std::vector<std::string>& files);
  void importDialog();
  bool textureSlot(const char* id, std::string& file, bool srgb);
  bool modelDetails(Actor& a);

  // ---- UI (panels.cpp / dialogs.cpp)
  void drawMenuBar(); void drawToolbar(); void drawStatusBar(); void drawViewport();
  void drawOutliner(); void drawDetails(); void drawWorldSettings(); void drawContent(); void drawLog(); void drawPlaceActors();
  void drawScripts(); void drawBlueprints(); void drawMaterial(); void drawDialogs(); void drawProjectBrowser(); void addMenuItems(const glm::vec3* at);
  // ---- the console version (console.cpp)
  bool consoleEvent(const SDL_Event& e);   // true = used
  void consoleFrame();                     // before the UI: camera / moving with the sticks, which device drives the menus
  void drawConsole();                      // the controller hints and the on-screen keyboard
  bool oskOpen() const { return oskOn_; }
  bool focusNameField = false; std::string& projectNameField() { return browserName_; }   // tests
private:
  std::vector<std::string> queued_;
  bool quit_ = false, confirmQuit_ = false, init_ = false; std::string pendingOpen_;
  SDL_GameController* pad_ = nullptr; bool padNav_ = true, padFly_ = false, padMove_ = false, padMouse_ = false, oskOn_ = false, oskShut_ = false, oskShift_ = false;
  float pmX_ = 0, pmY_ = 0; int oskR_ = 1, oskC_ = 0, oskHold_ = -1; float oskHoldT_ = 0; std::vector<int> keyUps_;
  void oskPress(); void oskMove(int dir); void sendKey(int key);
  std::deque<std::string> undo_, redo_; std::string stable_; bool pending_ = false; std::string clip_;
  double lastAutosave_ = 0; Uint64 lastTick_ = 0; float fps_ = 0, dt_ = 0;
  bool gizmoWasUsing_ = false; std::string browserName_ = "MyProject"; int browserTemplate_ = 1; char pathBuf_[1024] = {0}; std::string browserErr_;
  void commit();
  void buildFrame(Frame& f, Camera& cam, float aspect);
  void cameraInput(float dt);
  void shortcuts();
  void defaultLayout(unsigned dockId);
  void setProject(Project&& p);
};
}
