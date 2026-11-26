#pragma once
// One open Blueprint in the Blueprint Editor (bp_editor.cpp): its graph, the view, what is selected, what is being dragged.
#include <string>
#include <set>
#include <vector>
#include <imgui.h>
#include "blueprint.h"

namespace pge {
class Editor;
struct BpTab {
  std::string id; Blueprint bp; nlohmann::json saved;
  int graph = -1;   // -1 the Event Graph, else a function's graph
  BpGraph& G() { if (graph >= (int)bp.funcs.size()) graph = -1; return graph >= 0 ? (BpGraph&)bp.funcs[graph] : (BpGraph&)bp; }      // saved = what the project has (undo / redo bring a new one)
  std::vector<BpMessage> msgs; bool compiledOnce = false, focus = false, frameNext = false, showCode = false;
  ImVec2 pan{200, 150}; float zoom = 1; std::set<int> sel; int centerOn = 0;
  // dragging
  bool dragLink = false, dragNodes = false, dragMoved = false, boxSel = false, panning = false; int linkNode = 0, resizeComment = 0; std::string linkPin; ImVec2 boxA; float panMoved = 0;
  std::set<int> carryIds;
  // menus
  bool menuOpen = false, menuFocus = false; int menuFromNode = 0, ctxPinNode = 0; std::string menuFromPin, ctxPin, search, renameFrom; ImVec2 menuAt;
  struct PinPos { int node; std::string pin; ImVec2 pos; PinType type; bool out; };
  std::vector<PinPos> pins_; ImVec2 origin_{0, 0}, size_{0, 0};   // the canvas on screen (tests)
  ImVec2 toScreen(ImVec2 w) const { return ImVec2(origin_.x + (w.x + pan.x) * zoom, origin_.y + (w.y + pan.y) * zoom); }

  void load(const nlohmann::json& g);
  void commit(Editor& ed);
  void draw(Editor& ed);
  void drawSide(Editor& ed, int lang);
  void drawCanvas(Editor& ed, int lang);
  void drawAddMenu(Editor& ed, int lang);
  bool valueWidget(const BpPinDef& p, nlohmann::json& v, float w);
  std::string title(const BpNodeDef* d, const BpNode& n, int lang) const;
  float inlineWidth(PinType t, float fs) const;
  ImVec2 toWorld(ImVec2 s, ImVec2 O) const;
  void frameAll(ImVec2 size);
  void breakPin(int node, const std::string& pin);
  bool connect(int na, const std::string& pa, int nb, const std::string& pb);
  void deleteSel(); void copySel(); bool pasteAt(ImVec2 at);
};
}
