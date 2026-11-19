#pragma once
// Game UI (like Unreal's UMG, made from scripts): panels, text, images (also blocks from the atlas), buttons, grids,
// scrolling lists, text fields, sliders, tabs and windows; drag and drop; show / hide animations; menus that pause the
// game and free the mouse. Sizes are in UI units: the screen is 1080 units tall (1920 wide at 16:9), at any resolution.
// Arabic text is joined and right to left; dir: 'rtl' lays rows and grids out from the right.
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <unordered_map>
#include <functional>
#include <SDL.h>
#include <nlohmann/json.hpp>

struct ImDrawList; struct ImFont;
namespace pge {
class VoxelWorld;

struct UiEvent { std::string id, type; nlohmann::json data; std::vector<std::string> path; };   // path: the node's parents (for bubbling)

class Ui {
public:
  Ui(); ~Ui();
  void reset();                                                // a new level: everything goes
  // types: panel, text, image, button, grid, list, input, slider, tabs, window. "" = ok, else why not
  std::string create(const std::string& type, const std::string& id, const nlohmann::json& props);
  std::string set(const std::string& id, const nlohmann::json& props);
  const std::string& lastId() const { return lastId_; }        // the id create() gave
  nlohmann::json get(const std::string& id) const;             // value, text, visible, rect (UI units) ...
  bool exists(const std::string& id) const;
  void remove(const std::string& id);
  void clear(const std::string& id);                           // its children
  void show(const std::string& id, const nlohmann::json& anim);   // {anim: fade|slide|pop|garage|reveal|none, from, time}
  void hide(const std::string& id, const nlohmann::json& anim);
  void focus(const std::string& id);
  void listen(const std::string& id, const std::string& ev, bool on);
  std::vector<std::string> children(const std::string& id) const;
  void setScale(float s) { userScale_ = s > 0.2f && s < 5 ? s : 1; }
  void setDir(bool rtl) { rtl_ = rtl; }
  void setEscape(bool own) { ownEscape_ = own; }
  void setVoxels(VoxelWorld* v) { vox_ = v; }
  void setAssets(std::function<std::string(const std::string&)> find) { assets_ = std::move(find); }   // image name → file

  // input: returns true when the UI used the event (the game should not see it)
  bool event(const SDL_Event& e);
  void update(float dt);
  void draw(ImDrawList* dl, float x, float y, float w, float h);
  std::vector<UiEvent> takeEvents() { auto r = std::move(events_); events_.clear(); return r; }

  bool modal() const;          // a shown node asks for the mouse (menus): the mouse is free, the game doesn't look around
  bool pausing() const;        // a shown node pauses the game
  bool typing() const;         // a text field has the keyboard
  bool ownsEscape() const { return ownEscape_; }
  bool hovering() const { return hoverSolid_; }

  struct Node;
private:
  std::unordered_map<std::string, std::unique_ptr<Node>> nodes_;
  std::vector<Node*> roots_;
  std::vector<UiEvent> events_;
  VoxelWorld* vox_ = nullptr; std::function<std::string(const std::string&)> assets_;
  float userScale_ = 1, scale_ = 1, ox_ = 0, oy_ = 0, sw_ = 1920, sh_ = 1080; bool rtl_ = false, ownEscape_ = false;
  float mx_ = -1e9f, my_ = -1e9f; bool hoverSolid_ = false; double time_ = 0; int autoId_ = 0; std::string lastId_;
  Node *hover_ = nullptr, *press_ = nullptr, *focus_ = nullptr, *drag_ = nullptr, *slide_ = nullptr, *thumb_ = nullptr, *dropOver_ = nullptr;
  float pressX_ = 0, pressY_ = 0, grabX_ = 0, grabY_ = 0, thumbGrab_ = 0; bool dragging_ = false; double lastClick_ = 0; Node* lastClickNode_ = nullptr;
  std::vector<Node*> drawn_;   // this frame, in drawing order (hit testing goes backwards)
  struct Tex { unsigned id = 0; int w = 0, h = 0; bool bad = false; };
  std::map<std::string, Tex> tex_;

  Node* find(const std::string& id) const;
  void emit(Node* n, const std::string& type, nlohmann::json data = nlohmann::json::object());
  void apply(Node& n, const nlohmann::json& p);
  void unlink(Node* n);
  void destroy(Node* n);
  bool shown(const Node* n) const;      // it and its parents are visible
  Node* hit(float x, float y, int want) const;
  void measure(Node& n, float availW, float& w, float& h);
  void layout(Node& n, float x, float y, float w, float h);
  struct Xf;
  void drawNode(ImDrawList* dl, Node& n, const Xf& parent, float clip[4]);
  void drawText(ImDrawList* dl, Node& n, const Xf& t, float x, float y, float w, float h, unsigned col);
  void scrollBy(Node* n, float dy);
  void setValue(Node* n, double v, bool fire);
  void textKey(const SDL_Event& e);
  Tex* texture(const std::string& path);
  friend struct UiTest;
};
}
