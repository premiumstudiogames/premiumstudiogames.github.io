// The Blueprint Editor, like Unreal Engine's: an event graph of nodes and wires on a grid.
//   Right-click (or drag a wire into empty space): add a node, searching in English or Arabic.
//   Drag from a pin to a pin: a wire. Alt+click a pin: break its wires. Drag nodes; drag on empty space to select.
//   Right-drag or middle-drag: pan. Wheel: zoom. Delete, Ctrl+C / Ctrl+V / Ctrl+D / Ctrl+A, F: frame everything.
//   My Blueprint (left): variables (drag one in: Get, Alt: Set), events, compiler results, the selected node explained.
//   The names are English (like Unreal); "عربي" shows every node and pin in Arabic, "Both" shows both. Hovering a node
//   explains it in both languages. Every change compiles the graph into the script that runs.
#include "editor.h"
#include "bp_editor.h"
#include "blueprint.h"
#include "arabic.h"
#include "log.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

namespace pge {
using json = nlohmann::json;

static ImU32 pinColor(PinType t, float a = 1) {
  ImVec4 c;
  switch (t) {
    case PinType::Exec: c = {1, 1, 1, 1}; break; case PinType::Bool: c = {0.58f, 0.f, 0.f, 1}; break; case PinType::Number: c = {0.62f, 1.f, 0.27f, 1}; break;
    case PinType::String: c = {0.98f, 0.f, 0.83f, 1}; break; case PinType::Vector: c = {1.f, 0.78f, 0.f, 1}; break; case PinType::Actor: c = {0.f, 0.66f, 0.96f, 1}; break;
    case PinType::Array: c = {0.25f, 0.85f, 0.8f, 1}; break; default: c = {0.66f, 0.66f, 0.66f, 1}; }
  c.w = a; return ImGui::GetColorU32(c);
}
static ImU32 headerColor(const BpNodeDef* d) {
  if (!d) return IM_COL32(90, 30, 30, 255);
  if (d->kind == BpNodeDef::Event) return IM_COL32(140, 22, 22, 255);
  if (d->kind == BpNodeDef::Pure) return IM_COL32(52, 105, 42, 255);
  if (d->cat == "Flow Control") return IM_COL32(78, 78, 78, 255);
  if (d->cat == "Variables") return IM_COL32(40, 40, 40, 255);
  return IM_COL32(28, 72, 130, 255);
}
// a label in the chosen language (0 English, 1 Arabic, 2 both: English, the Arabic is shown under titles and in tips)
static std::string ar(const std::string& s) { return hasRtl(s) ? visualText(s) : s; }
// Arabic text wrapped to a width: split into lines in reading order first, then each line is joined / reversed and
// put on the right (wrapping already-reversed text would put the end of the sentence on the first line)
static void arWrapped(const std::string& logical, float width = 0, ImU32 col = 0) {
  if (!hasRtl(logical)) { ImGui::PushTextWrapPos(width > 0 ? ImGui::GetCursorPosX() + width : 0); if (col) ImGui::PushStyleColor(ImGuiCol_Text, col); ImGui::TextUnformatted(logical.c_str()); if (col) ImGui::PopStyleColor(); ImGui::PopTextWrapPos(); return; }
  if (width <= 0) width = ImGui::GetContentRegionAvail().x; if (width < 40) width = 40;
  std::vector<std::string> words; { std::string w; for (char c : logical) { if (c == ' ') { if (!w.empty()) words.push_back(w); w.clear(); } else w += c; } if (!w.empty()) words.push_back(w); }
  std::vector<std::string> lines; std::string line;
  for (auto& w : words) { std::string t = line.empty() ? w : line + " " + w; if (!line.empty() && ImGui::CalcTextSize(visualText(t).c_str()).x > width) { lines.push_back(line); line = w; } else line = t; }
  if (!line.empty()) lines.push_back(line);
  float x0 = ImGui::GetCursorPosX(); if (col) ImGui::PushStyleColor(ImGuiCol_Text, col);
  for (auto& l : lines) { std::string v = visualText(l); float w = ImGui::CalcTextSize(v.c_str()).x; ImGui::SetCursorPosX(x0 + std::max(0.f, width - w)); ImGui::TextUnformatted(v.c_str()); }
  if (col) ImGui::PopStyleColor();
}
std::string BpTab::title(const BpNodeDef* d, const BpNode& n, int lang) const {
  if (!d) return n.type;
  std::string en = d->en, a = d->ar;
  if (n.type == "Var.Get" || n.type == "Var.Set") { std::string v = n.values.value("Var", std::string()); en = (n.type == "Var.Get" ? "Get " : "Set ") + v; a = (n.type == "Var.Get" ? "اقرأ " : "غيّر ") + v; }
  if (n.type == "Event.Custom") { en = n.values.value("Name", std::string("MyEvent")); a = "حدث: " + en; }
  if (n.type == "Flow.CallEvent") { en = "Call " + n.values.value("Event", std::string("MyEvent")); a = "شغّل " + n.values.value("Event", std::string("MyEvent")); }
  if (n.type == "Event.KeyPressed") { en = n.values.value("Key", std::string("KeyE")) + " Pressed"; a = "عند ضغط " + n.values.value("Key", std::string("KeyE")); }
  return lang == 1 ? ar(a) : en;
}

/* ------------------------------------------------------------------ open / new / compile */
std::string Editor::newBlueprint(const std::string& name) {
  std::string base = name; for (char& c : base) if (!std::isalnum((unsigned char)c) && c != '_') c = '_';
  std::string id = base.rfind("BP_", 0) == 0 ? base : "BP_" + base; std::string b = id; for (int i = 2; P.scripts.count(id); i++) id = b + "_" + std::to_string(i);
  Blueprint bp = bpStarter(); std::vector<BpMessage> m; Script s{id, bpCompile(bp, id, m)}; s.graph = bpToJson(bp); P.scripts[id] = s;
  touch(); logMsg("Created Blueprint " + id); return id;
}
void Editor::openBlueprint(const std::string& id) {
  auto it = P.scripts.find(id); if (it == P.scripts.end() || !it->second.blueprint()) return;
  wBlueprints = true; for (auto& t : bpTabs) if (t->id == id) { t->focus = true; return; }
  auto t = std::make_unique<BpTab>(); t->id = id; t->load(it->second.graph); t->focus = true; t->frameNext = true; bpTabs.push_back(std::move(t));
}
// the graph → the script code. With errors the last good code stays (the game never runs a broken Blueprint)
bool Editor::compileBlueprint(const std::string& id, bool report) {
  auto it = P.scripts.find(id); if (it == P.scripts.end() || !it->second.blueprint()) return true;
  Blueprint bp = bpFromJson(it->second.graph); std::vector<BpMessage> msgs; std::string code = bpCompile(bp, id, msgs);
  int errs = 0; for (auto& m : msgs) errs += m.error;
  if (!errs) it->second.code = code;
  for (auto& t : bpTabs) if (t->id == id) t->msgs = msgs;
  if (report) for (auto& m : msgs) { const BpNode* n = bp.node(m.node); const BpNodeDef* d = n ? bpDef(n->type) : nullptr;
    logMsg(std::string(m.error ? "error: " : "warning: ") + id + ": " + (d ? d->en : std::string("node")) + ": " + m.text, m.error ? LogKind::Error : LogKind::Warn); }
  return errs == 0;
}

void BpTab::load(const json& g) { bp = bpFromJson(g); saved = g; }
// a change made in the editor → the project (one undo step), compiled at once
void BpTab::commit(Editor& ed) {
  auto it = ed.P.scripts.find(id); if (it == ed.P.scripts.end()) return;
  json g = bpToJson(bp); it->second.graph = g; saved = g; ed.compileBlueprint(id, false); ed.touch();
}

/* ------------------------------------------------------------------ the window */
void Editor::drawBlueprints() {
  ImGui::SetNextWindowSize({1100, 700}, ImGuiCond_FirstUseEver);
  if (ImGuiWindow* vp = ImGui::FindWindowByName("Viewport")) if (vp->DockId) ImGui::SetNextWindowDockID(vp->DockId, ImGuiCond_FirstUseEver);   // a tab next to the Viewport, like Unreal's
  if (!ImGui::Begin("Blueprint Editor", &wBlueprints)) { ImGui::End(); return; }
  // undo / redo / a file loaded: the tabs follow the project
  for (size_t i = 0; i < bpTabs.size();) { auto it = P.scripts.find(bpTabs[i]->id);
    if (it == P.scripts.end() || !it->second.blueprint()) { bpTabs.erase(bpTabs.begin() + i); continue; }
    if (it->second.graph != bpTabs[i]->saved) { bpTabs[i]->load(it->second.graph); bpTabs[i]->sel.clear(); compileBlueprint(bpTabs[i]->id, false); }
    i++; }
  if (bpTabs.empty()) { ImGui::TextDisabled("No Blueprint open. Double-click one in the Content Browser (Scripts), or use + Add > Blueprint.");
    ImGui::TextDisabled("%s", visualText("ما فيه بلوبرنت مفتوح. افتح واحد من متصفح المحتوى (Scripts) أو سوِّ واحد جديد من + Add > Blueprint.").c_str()); ImGui::End(); return; }
  int close = -1;
  if (ImGui::BeginTabBar("bptabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs)) {
    for (size_t i = 0; i < bpTabs.size(); i++) { BpTab& t = *bpTabs[i]; bool open = true;
      ImGuiTabItemFlags fl = t.focus ? ImGuiTabItemFlags_SetSelected : 0; t.focus = false;
      if (ImGui::BeginTabItem((t.id + "###bp" + t.id).c_str(), &open, fl)) { t.draw(*this); ImGui::EndTabItem(); }
      if (!open) close = (int)i; }
    ImGui::EndTabBar();
  }
  if (close >= 0) bpTabs.erase(bpTabs.begin() + close);
  ImGui::End();
}

void BpTab::draw(Editor& ed) {
  if (msgs.empty() && !compiledOnce) { ed.compileBlueprint(id, false); compiledOnce = true; }
  int& lang = ed.prefs.bpLang;
  // ---- toolbar
  int errs = 0, warns = 0; for (auto& m : msgs) (m.error ? errs : warns)++;
  ImU32 sc = errs ? IM_COL32(230, 70, 60, 255) : warns ? IM_COL32(240, 190, 60, 255) : IM_COL32(80, 200, 110, 255);
  ImGui::PushStyleColor(ImGuiCol_Button, sc); ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(15, 15, 15, 255));
  if (ImGui::Button(errs ? "Compile  (errors)" : warns ? "Compile  (warnings)" : "Compile  (good)")) { ed.compileBlueprint(id, true); }
  ImGui::PopStyleColor(2);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s", "The graph is compiled into the script that runs (it also happens on every change).", visualText("البلوبرنت يتحول لسكربت يشتغل (يصير تلقائياً مع كل تعديل).").c_str());
  ImGui::SameLine(); if (ImGui::Button("Save")) ed.save();
  ImGui::SameLine(); ImGui::SetNextItemWidth(170 * ImGui::GetFontSize() / 15);
  const char* langs[] = {"English", "", "English + Arabic"}; std::string arl = visualText("العربية"); langs[1] = arl.c_str();
  ImGui::Combo("##bplang", &lang, langs, 3); if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", visualText("لغة أسماء العقد: الإنجليزي أساسي، والعربي يشرح لك كل عقدة").c_str());
  if (ImGui::IsItemDeactivatedAfterEdit()) ed.savePrefs();
  ImGui::SameLine(); if (ImGui::Button("Frame All (F)")) frameNext = true;
  ImGui::SameLine(); ImGui::Checkbox("Show Code", &showCode);
  ImGui::SameLine(); ImGui::TextDisabled("Right-click: add node   ·   Drag pins: wire   ·   Alt+click pin: break   ·   Right-drag: pan   ·   Wheel: zoom");

  float left = 270 * ImGui::GetFontSize() / 15;
  ImGui::BeginChild("##mybp", {left, 0}, ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
  drawSide(ed, lang);
  ImGui::EndChild(); ImGui::SameLine();
  if (showCode) {
    float w = ImGui::GetContentRegionAvail().x * 0.38f;
    ImGui::BeginChild("##canvasWrap", {ImGui::GetContentRegionAvail().x - w - 6, 0}); drawCanvas(ed, lang); ImGui::EndChild(); ImGui::SameLine();
    ImGui::BeginChild("##code", {0, 0}, ImGuiChildFlags_Borders);
    auto it = ed.P.scripts.find(id); ImGui::TextDisabled("The script this Blueprint runs as (read only)");
    if (it != ed.P.scripts.end()) { std::string c = it->second.code; size_t p = c.find("\n})();\n"); if (p != std::string::npos) c = c.substr(p + 7);
      ImGui::PushFont(ImGui::GetIO().Fonts->Fonts.Size > 2 ? ImGui::GetIO().Fonts->Fonts[2] : ImGui::GetFont()); ImGui::TextUnformatted(c.c_str()); ImGui::PopFont(); }
    ImGui::EndChild();
  } else { ImGui::BeginChild("##canvasWrap", {0, 0}); drawCanvas(ed, lang); ImGui::EndChild(); }
}

/* ------------------------------------------------------------------ My Blueprint: variables, events, results, the selected node */
void BpTab::drawSide(Editor& ed, int lang) {
  auto H = [&](const char* en, const char* a) { std::string s = lang == 1 ? visualText(a) : lang == 2 ? std::string(en) + "  " + visualText(a) : std::string(en); ImGui::SeparatorText(s.c_str()); };
  H("VARIABLES", "المتغيرات");
  int del = -1; bool ch = false;
  for (size_t i = 0; i < bp.vars.size(); i++) { BpVar& v = bp.vars[i]; ImGui::PushID((int)i);
    ImGui::PushStyleColor(ImGuiCol_Button, pinColor(v.type, 0.9f)); ImGui::Button("##ty", {10, ImGui::GetFrameHeight()}); ImGui::PopStyleColor();
    if (ImGui::BeginDragDropSource()) { ImGui::SetDragDropPayload("BPVAR", v.name.c_str(), v.name.size() + 1); ImGui::Text("%s  (drop: Get · Alt: Set)", v.name.c_str()); ImGui::EndDragDropSource(); }
    ImGui::SameLine(); ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
    std::string old = v.name; ImGui::InputText("##nm", &v.name, ImGuiInputTextFlags_CharsNoBlank);
    if (ImGui::IsItemDeactivatedAfterEdit()) { v.name = bpSanitize(v.name); bool dup = false; for (size_t k = 0; k < bp.vars.size(); k++) dup = dup || (k != i && bp.vars[k].name == v.name);
      if (dup || v.name.empty()) v.name = renameFrom.empty() ? old : renameFrom; else { for (auto& n : bp.nodes) if ((n.type == "Var.Get" || n.type == "Var.Set") && n.values.value("Var", std::string()) == renameFrom) n.values["Var"] = v.name; ch = true; } renameFrom.clear(); }
    else if (ImGui::IsItemActivated()) renameFrom = old;
    ImGui::SameLine(); ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - 4);
    const PinType types[] = {PinType::Bool, PinType::Number, PinType::String, PinType::Vector, PinType::Actor, PinType::Array};
    if (ImGui::BeginCombo("##ty2", lang == 1 ? visualText(pinTypeAr(v.type)).c_str() : pinTypeEn(v.type))) {
      for (auto t : types) if (ImGui::Selectable(lang == 1 ? visualText(pinTypeAr(t)).c_str() : pinTypeEn(t), t == v.type)) { v.type = t; v.def = json(); ch = true; }
      ImGui::EndCombo(); }
    ImGui::SameLine(); if (ImGui::Button("x", {ImGui::GetFrameHeight(), 0})) del = (int)i;
    // its starting value
    ImGui::Indent(14); BpPinDef p; p.type = v.type; p.id = "Default";
    if (v.def.is_null()) { BpPinDef d = p; v.def = v.type == PinType::Bool ? json(false) : v.type == PinType::Number ? json(0) : v.type == PinType::String ? json("") : v.type == PinType::Vector ? json::array({0, 0, 0}) : json(); }
    if (v.type != PinType::Actor && v.type != PinType::Array) { ImGui::TextDisabled("%s", lang == 1 ? visualText("البداية").c_str() : "Default"); ImGui::SameLine(); if (valueWidget(p, v.def, ImGui::GetContentRegionAvail().x)) ch = true; }
    ImGui::Unindent(14); ImGui::PopID(); }
  if (del >= 0) { bp.vars.erase(bp.vars.begin() + del); ch = true; }
  if (ImGui::Button(lang == 1 ? visualText("+ متغير").c_str() : "+ Variable")) { std::string n = "NewVar"; for (int k = 2; bp.var(n); k++) n = "NewVar" + std::to_string(k); bp.vars.push_back({n, PinType::Number, 0}); ch = true; }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s", "A value this Blueprint keeps (score, health...). Drag it into the graph.", visualText("قيمة يحفظها البلوبرنت (النقاط، الصحة...). اسحبها للرسم.").c_str());

  H("EVENTS", "الأحداث");
  for (auto& n : bp.nodes) { const BpNodeDef* d = bpDef(n.type); if (!d || d->kind != BpNodeDef::Event) continue;
    if (ImGui::Selectable((title(d, n, lang) + "##ev" + std::to_string(n.id)).c_str(), sel.count(n.id) > 0)) { sel = {n.id}; centerOn = n.id; } }
  H("COMPILER RESULTS", "نتائج الترجمة");
  if (msgs.empty()) ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.5f, 1), "%s", lang == 1 ? visualText("كل شي سليم").c_str() : "Good - no errors");
  for (size_t i = 0; i < msgs.size(); i++) { auto& m = msgs[i];
    ImGui::PushStyleColor(ImGuiCol_Text, m.error ? IM_COL32(255, 110, 100, 255) : IM_COL32(240, 200, 90, 255));
    std::string t = (m.error ? "error: " : "warning: ") + (lang == 1 ? visualText(m.textAr) : m.text) + "##m" + std::to_string(i);   // one line; hover for both in full
    if (ImGui::Selectable(t.c_str(), false, ImGuiSelectableFlags_None)) { if (m.node) { sel = {m.node}; centerOn = m.node; } }
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) { ImGui::BeginTooltip(); ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24); ImGui::TextUnformatted(m.text.c_str()); ImGui::PopTextWrapPos(); arWrapped(m.textAr, ImGui::GetFontSize() * 24); ImGui::EndTooltip(); } }
  // the selected node, explained
  if (sel.size() == 1) if (const BpNode* n = bp.node(*sel.begin())) if (const BpNodeDef* d = bpDef(n->type)) {
    H("SELECTED NODE", "العقدة المحددة");
    ImGui::TextColored(ImVec4(1, 1, 1, 1), "%s", d->en.c_str()); ImGui::TextColored(ImVec4(0.75f, 0.85f, 1, 1), "%s", visualText(d->ar).c_str());
    ImGui::PushTextWrapPos(0); ImGui::TextDisabled("%s", d->descEn.c_str()); ImGui::PopTextWrapPos(); arWrapped(d->descAr);
    std::vector<BpPinDef> pins = bpPins(bp, *n);
    for (auto& p : pins) { if (p.type == PinType::Exec) continue; ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(pinColor(p.type)), "%s %s", p.out ? "out" : "in ", bpPinEn(p.id).c_str());
      ImGui::SameLine(); ImGui::TextDisabled("%s  ·  %s", visualText(bpPinAr(p.id)).c_str(), visualText(pinTypeAr(p.type)).c_str()); }
  }
  if (ch) commit(ed);
}

// an input's value typed on the node (or a variable's default): true when it changed (on letting go)
bool BpTab::valueWidget(const BpPinDef& p, json& v, float w) {
  bool ch = false; ImGui::PushID(p.id.c_str());
  switch (p.type) {
    case PinType::Bool: { bool b = v.is_boolean() ? v.get<bool>() : false; if (ImGui::Checkbox("##b", &b)) { v = b; ch = true; } break; }
    case PinType::Number: { float f = v.is_number() ? v.get<float>() : 0; ImGui::SetNextItemWidth(w); ImGui::DragFloat("##n", &f, 0.05f, 0, 0, "%.3g"); if (ImGui::IsItemEdited()) v = f; ch = ImGui::IsItemDeactivatedAfterEdit(); break; }
    case PinType::String: case PinType::Any: { std::string s = v.is_string() ? v.get<std::string>() : v.is_null() ? "" : v.dump(); ImGui::SetNextItemWidth(w);
      if (ImGui::InputText("##s", &s)) { if (p.type == PinType::Any) { try { v = json::parse(s); } catch (...) { v = s; } } else v = s; } ch = ImGui::IsItemDeactivatedAfterEdit(); break; }
    case PinType::Vector: { float a[3] = {0, 0, 0}; if (v.is_array()) for (size_t i = 0; i < 3 && i < v.size(); i++) if (v[i].is_number()) a[i] = v[i].get<float>();
      ImGui::SetNextItemWidth(w); ImGui::DragFloat3("##v", a, 0.05f, 0, 0, "%.2g"); if (ImGui::IsItemEdited()) v = json::array({a[0], a[1], a[2]}); ch = ImGui::IsItemDeactivatedAfterEdit(); break; }
    default: break;
  }
  ImGui::PopID(); return ch;
}

/* ------------------------------------------------------------------ the graph */
void BpTab::drawCanvas(Editor& ed, int lang) {
  ImGuiIO& io = ImGui::GetIO(); ImDrawList* dl = ImGui::GetWindowDrawList(); ImFont* font = ImGui::GetFont();
  const float fs = ImGui::GetFontSize();
  ImVec2 O = ImGui::GetCursorScreenPos(), size = ImGui::GetContentRegionAvail(); if (size.x < 50 || size.y < 50) return;
  ImVec2 E(O.x + size.x, O.y + size.y); origin_ = O; size_ = size;
  ImGui::SetNextItemAllowOverlap(); ImGui::InvisibleButton("##canvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
  ImGuiID canvasId = ImGui::GetItemID();
  bool canvasHover = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlappedByItem), canvasFocus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
  // variables dropped in
  if (ImGui::BeginDragDropTarget()) { if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("BPVAR")) { std::string v((const char*)pl->Data);
      ImVec2 w = toWorld(io.MousePos, O); BpNode n; n.id = bp.nextId++; n.type = io.KeyAlt ? "Var.Set" : "Var.Get"; n.x = w.x; n.y = w.y; n.values["Var"] = v; bp.nodes.push_back(n); sel = {n.id}; commit(ed); }
    ImGui::EndDragDropTarget(); }
  dl->PushClipRect(O, E, true);
  dl->AddRectFilled(O, E, IM_COL32(38, 38, 40, 255));
  // grid like Unreal's: small lines every 16, big every 128
  for (int big = 0; big < 2; big++) { float step = (big ? 128.f : 16.f) * zoom; if (step < 6) continue; ImU32 c = big ? IM_COL32(20, 20, 22, 255) : IM_COL32(48, 48, 51, 255);
    float sx = std::fmod(pan.x * zoom, step), sy = std::fmod(pan.y * zoom, step);
    for (float x = O.x + sx; x < E.x; x += step) dl->AddLine({x, O.y}, {x, E.y}, c, big ? 1.5f : 1);
    for (float y = O.y + sy; y < E.y; y += step) dl->AddLine({O.x, y}, {E.x, y}, c, big ? 1.5f : 1); }
  if (frameNext) { frameAll(size); frameNext = false; }
  if (centerOn) { if (BpNode* n = bp.node(centerOn)) { pan = ImVec2(size.x / 2 / zoom - n->x - 100, size.y / 2 / zoom - n->y - 40); } centerOn = 0; }

  // ---- layout
  std::map<int, std::vector<BpMessage*>> nodeMsgs; for (auto& m : msgs) if (m.node) nodeMsgs[m.node].push_back(&m);
  std::set<std::pair<int, std::string>> linked; for (auto& l : bp.links) { linked.insert({l.from, l.fromPin}); linked.insert({l.to, l.toPin}); }
  struct Lay { ImVec2 a, b; float hh; std::vector<BpPinDef> pins; };
  std::map<int, Lay> lay; pins_.clear();
  const float rh = fs * 1.5f, pad = fs * 0.6f;
  for (auto& n : bp.nodes) {
    const BpNodeDef* d = bpDef(n.type); Lay L; L.pins = bpPins(bp, n);
    if (d && d->kind == BpNodeDef::Comment) { L.a = {n.x, n.y}; L.b = {n.x + n.w, n.y + n.h}; L.hh = fs * 1.6f; lay[n.id] = L; continue; }
    bool compact = n.type == "Var.Get";
    std::string tt = title(d, n, lang); float tw = font->CalcTextSizeA(fs, 1e9f, 0, tt.c_str()).x + fs * 2.2f;
    if (lang == 2 && d) tw = std::max(tw, font->CalcTextSizeA(fs * 0.85f, 1e9f, 0, ar(d->ar).c_str()).x + fs * 2.2f);
    float hh = compact ? 0 : fs * (lang == 2 ? 2.45f : 1.65f);
    int nf = d ? (int)d->fields.size() : 0; if (n.type == "Event.Custom" || n.type == "Flow.CallEvent" || n.type == "Event.KeyPressed") nf = 1;
    float fieldsH = nf * rh;
    float inW = 0, outW = 0; int nin = 0, nout = 0;
    for (auto& p : L.pins) { std::string lbl = lang == 1 ? ar(bpPinAr(p.id)) : bpPinEn(p.id); float w = font->CalcTextSizeA(fs, 1e9f, 0, lbl.c_str()).x + fs * 1.6f;
      if (!p.out && !linked.count({n.id, p.id})) w += inlineWidth(p.type, fs);
      if (p.out) { outW = std::max(outW, w); nout++; } else { inW = std::max(inW, w); nin++; } }
    float w = std::max({tw, inW + outW + fs * 1.2f, fs * 8}); if (compact) w = std::max(font->CalcTextSizeA(fs, 1e9f, 0, tt.c_str()).x + fs * 3, fs * 6);
    float h = hh + fieldsH + std::max(nin, nout) * rh + pad; if (compact) h = rh + pad * 0.6f;
    L.a = {n.x, n.y}; L.b = {n.x + w, n.y + h}; L.hh = hh + fieldsH; lay[n.id] = L;
    // pins' places
    int ri = 0, ro = 0;
    for (auto& p : L.pins) { float y = n.y + L.hh + (p.out ? ro++ : ri++) * rh + rh * 0.5f + (compact ? pad * 0.3f : 0);
      ImVec2 c = p.out ? ImVec2(n.x + w - fs * 0.75f, y) : ImVec2(n.x + fs * 0.75f, y); pins_.push_back({n.id, p.id, c, p.type, p.out}); }
  }
  auto S = [&](ImVec2 w) { return ImVec2(O.x + (w.x + pan.x) * zoom, O.y + (w.y + pan.y) * zoom); };
  auto pinAt = [&](int node, const std::string& pin) -> const PinPos* { for (auto& p : pins_) if (p.node == node && p.pin == pin) return &p; return nullptr; };

  // ---- comments (behind)
  float z = zoom; ImGui::SetWindowFontScale(std::max(0.3f, z));
  for (auto& n : bp.nodes) if (n.type == "Comment") { Lay& L = lay[n.id]; ImVec2 a = S(L.a), b = S(L.b);
    bool s = sel.count(n.id) > 0; dl->AddRectFilled(a, b, IM_COL32(120, 120, 120, 40), 6 * z); dl->AddRectFilled(a, {b.x, a.y + L.hh * z}, IM_COL32(120, 120, 120, 90), 6 * z, ImDrawFlags_RoundCornersTop);
    dl->AddRect(a, b, s ? IM_COL32(245, 166, 35, 255) : IM_COL32(170, 170, 170, 120), 6 * z, 0, s ? 2.f : 1.f);
    std::string t = n.values.value("Text", std::string("Comment")); dl->AddText(font, fs * 1.1f * z, {a.x + 8 * z, a.y + 4 * z}, IM_COL32_WHITE, ar(t).c_str());
    dl->AddTriangleFilled({b.x - 12 * z, b.y}, {b.x, b.y - 12 * z}, b, IM_COL32(200, 200, 200, 160)); }

  // ---- wires
  for (auto& l : bp.links) { const PinPos* a = pinAt(l.from, l.fromPin); const PinPos* b = pinAt(l.to, l.toPin); if (!a || !b) continue;
    ImVec2 p1 = S(a->pos), p4 = S(b->pos); float dx = std::max(40.f * z, std::fabs(p4.x - p1.x) * 0.5f);
    dl->AddBezierCubic(p1, {p1.x + dx, p1.y}, {p4.x - dx, p4.y}, p4, pinColor(a->type == PinType::Any ? b->type : a->type), (a->type == PinType::Exec ? 3.2f : 2.4f) * std::max(0.5f, z)); }
  if (dragLink) { const PinPos* a = pinAt(linkNode, linkPin); if (a) { ImVec2 p1 = S(a->pos), p4 = io.MousePos; if (!a->out) std::swap(p1, p4);
      float dx = std::max(40.f * z, std::fabs(p4.x - p1.x) * 0.5f); dl->AddBezierCubic(p1, {p1.x + dx, p1.y}, {p4.x - dx, p4.y}, p4, pinColor(a->type), 2.6f * std::max(0.5f, z)); } }

  // ---- nodes
  bool widgetHover = false, widgetActive = false; std::vector<int> hoverOrder;
  for (auto& n : bp.nodes) { if (n.type == "Comment") continue;
    const BpNodeDef* d = bpDef(n.type); Lay& L = lay[n.id]; ImVec2 a = S(L.a), b = S(L.b); if (b.x < O.x || a.x > E.x || b.y < O.y || a.y > E.y) continue;
    bool compact = n.type == "Var.Get"; bool s = sel.count(n.id) > 0; bool bad = false, warn = false; for (auto* m : nodeMsgs[n.id]) (m->error ? bad : warn) = true;
    float r = 7 * z;
    dl->AddRectFilled({a.x + 3 * z, a.y + 4 * z}, {b.x + 3 * z, b.y + 4 * z}, IM_COL32(0, 0, 0, 90), r);
    if (compact) { const BpVar* v = bp.var(n.values.value("Var", std::string()));
      dl->AddRectFilled(a, b, IM_COL32(28, 28, 30, 240), r * 1.8f); dl->AddRectFilled(a, {a.x + 5 * z, b.y}, pinColor(v ? v->type : PinType::Any), r, ImDrawFlags_RoundCornersLeft); }
    else {
      dl->AddRectFilled(a, b, IM_COL32(22, 22, 24, 238), r);
      ImU32 hc = headerColor(d); dl->AddRectFilledMultiColor({a.x + 1, a.y + 1}, {b.x - 1, a.y + (L.hh - (d && !d->fields.empty() ? 0 : 0)) * z}, hc, IM_COL32(28, 28, 30, 230), IM_COL32(28, 28, 30, 230), hc);
      float hy = a.y + fs * 0.25f * z;
      if (d && d->kind == BpNodeDef::Event) dl->AddCircleFilled({a.x + fs * 0.85f * z, hy + fs * 0.5f * z}, fs * 0.32f * z, IM_COL32(255, 255, 255, 220));
      else if (d && d->kind == BpNodeDef::Pure) dl->AddText(font, fs * z, {a.x + fs * 0.5f * z, hy}, IM_COL32(170, 255, 140, 255), "f");
      else dl->AddText(font, fs * z, {a.x + fs * 0.45f * z, hy}, IM_COL32(150, 200, 255, 255), "f");
      dl->AddText(font, fs * z, {a.x + fs * 1.5f * z, hy}, IM_COL32_WHITE, title(d, n, lang).c_str());
      if (lang == 2 && d) dl->AddText(font, fs * 0.85f * z, {a.x + fs * 1.5f * z, hy + fs * 1.1f * z}, IM_COL32(200, 215, 255, 230), ar(d->ar).c_str());
    }
    dl->AddRect(a, b, bad ? IM_COL32(255, 60, 50, 255) : s ? IM_COL32(245, 166, 35, 255) : warn ? IM_COL32(240, 190, 60, 200) : IM_COL32(10, 10, 10, 200), compact ? r * 1.8f : r, 0, (s || bad) ? 2.2f : 1.f);
    if (bad || warn) { ImVec2 c(b.x - fs * 0.4f * z, a.y - fs * 0.1f * z); dl->AddCircleFilled(c, fs * 0.55f * z, bad ? IM_COL32(220, 40, 30, 255) : IM_COL32(230, 170, 30, 255)); dl->AddText(font, fs * 0.9f * z, {c.x - fs * 0.13f * z, c.y - fs * 0.48f * z}, IM_COL32_WHITE, "!"); }
    // fields (an event's name, a key, a variable)
    float fy = (compact ? 0 : (lang == 2 ? 2.45f : 1.65f)) * fs;
    if (d && !compact && z > 0.45f) for (auto& f : d->fields) { if (n.type == "Var.Set" && f.id == "Var") continue;
      ImGui::SetCursorScreenPos(S({n.x + fs * 0.6f, n.y + fy + rh * 0.12f})); ImGui::PushID(n.id); ImGui::PushID(f.id.c_str());
      std::string v = n.values.value(f.id, f.def); ImGui::SetNextItemWidth((L.b.x - L.a.x - fs * 1.2f) * z);
      if (f.id == "Event" ) { if (ImGui::BeginCombo("##f", v.c_str())) { for (auto& m : bp.nodes) if (m.type == "Event.Custom") { std::string nm = m.values.value("Name", std::string("MyEvent")); if (ImGui::Selectable(nm.c_str(), nm == v)) { n.values[f.id] = nm; commit(ed); } } ImGui::EndCombo(); } }
      else { ImGui::InputText("##f", &v); if (ImGui::IsItemEdited()) n.values[f.id] = v; if (ImGui::IsItemDeactivatedAfterEdit()) commit(ed); }
      widgetHover |= ImGui::IsItemHovered(); widgetActive |= ImGui::IsItemActive();
      ImGui::PopID(); ImGui::PopID(); fy += rh; }
    if (n.type == "Var.Set" && !compact && z > 0.45f) fy += 0;   // the variable is in the title
    // pins
    for (auto& p : L.pins) { const PinPos* pp = pinAt(n.id, p.id); if (!pp) continue; ImVec2 c = S(pp->pos); bool on = linked.count({n.id, p.id}) > 0; float pr = fs * 0.36f * z;
      if (p.type == PinType::Exec) { ImVec2 q[5] = {{c.x - pr, c.y - pr}, {c.x + pr * 0.2f, c.y - pr}, {c.x + pr * 1.1f, c.y}, {c.x + pr * 0.2f, c.y + pr}, {c.x - pr, c.y + pr}};
        if (on) dl->AddConvexPolyFilled(q, 5, IM_COL32_WHITE); else dl->AddPolyline(q, 5, IM_COL32_WHITE, ImDrawFlags_Closed, 1.6f * std::max(0.6f, z)); }
      else { if (on) dl->AddCircleFilled(c, pr, pinColor(p.type)); else dl->AddCircle(c, pr, pinColor(p.type), 0, 1.8f * std::max(0.6f, z));
        if (p.type == PinType::Array) dl->AddRect({c.x - pr * 0.5f, c.y - pr * 0.5f}, {c.x + pr * 0.5f, c.y + pr * 0.5f}, pinColor(p.type)); }
      if (compact) continue;
      std::string lbl = lang == 1 ? ar(bpPinAr(p.id)) : bpPinEn(p.id);
      ImVec2 ts = font->CalcTextSizeA(fs * z, 1e9f, 0, lbl.c_str());
      float lx = p.out ? c.x - fs * 0.7f * z - ts.x : c.x + fs * 0.7f * z;
      if (!lbl.empty()) dl->AddText(font, fs * z, {lx, c.y - ts.y * 0.5f}, IM_COL32(220, 220, 220, 255), lbl.c_str());
      // the value typed on an input with no wire
      if (!p.out && !on && p.type != PinType::Exec && p.type != PinType::Actor && p.type != PinType::Array && z > 0.45f) {
        float ww = inlineWidth(p.type, fs) - fs * 0.6f; ImGui::SetCursorScreenPos({lx + ts.x + fs * 0.35f * z, c.y - ImGui::GetFrameHeight() * 0.5f});
        ImGui::PushID(n.id); json v = n.values.contains(p.id) ? n.values[p.id] : p.def;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {3 * z, 1 * z}); bool done = valueWidget(p, v, ww * z); ImGui::PopStyleVar();
        if (!(n.values.contains(p.id) && n.values[p.id] == v) && !(v == p.def && !n.values.contains(p.id))) n.values[p.id] = v;
        widgetHover |= ImGui::IsItemHovered(); widgetActive |= ImGui::IsItemActive(); if (done) commit(ed);
        ImGui::PopID(); }
      else if (!p.out && !on && p.type == PinType::Actor && p.def.is_string() && (p.def.get<std::string>() == "self" || p.def.get<std::string>() == "player")) {
        bool pl = p.def.get<std::string>() == "player";
        dl->AddText(font, fs * 0.9f * z, {lx + ts.x + fs * 0.35f * z, c.y - ts.y * 0.45f}, IM_COL32(150, 150, 150, 255), lang == 1 ? visualText(pl ? "اللاعب" : "نفسي").c_str() : (pl ? "player" : "self")); }
    }
    if (ImGui::IsMouseHoveringRect(a, b)) hoverOrder.push_back(n.id);
  }
  ImGui::SetWindowFontScale(1);
  if (boxSel) { ImVec2 a = boxA, b = io.MousePos; dl->AddRectFilled(ImMin(a, b), ImMax(a, b), IM_COL32(245, 166, 35, 30)); dl->AddRect(ImMin(a, b), ImMax(a, b), IM_COL32(245, 166, 35, 200)); }
  dl->PopClipRect();
  // zoom level, like Unreal
  { char zb[32]; std::snprintf(zb, sizeof zb, "Zoom 1:%.2g", 1 / zoom); dl->AddText({E.x - fs * 6, O.y + 6}, IM_COL32(200, 200, 200, 120), zb); }
  if (bp.nodes.empty()) { const char* t1 = "Right-click to add a node (Event BeginPlay to start)"; std::string t2 = visualText("اضغط زر الماوس اليمين عشان تضيف عقدة (ابدأ بحدث بدء اللعب)");
    dl->AddText({O.x + size.x / 2 - ImGui::CalcTextSize(t1).x / 2, O.y + size.y / 2 - fs}, IM_COL32(200, 200, 200, 150), t1);
    dl->AddText({O.x + size.x / 2 - ImGui::CalcTextSize(t2.c_str()).x / 2, O.y + size.y / 2 + fs * 0.4f}, IM_COL32(200, 200, 200, 150), t2.c_str()); }

  // ---- what is under the mouse
  ImVec2 mw = toWorld(io.MousePos, O);
  const PinPos* hp = nullptr; float best = fs * 0.75f;
  for (auto& p : pins_) { float dx = (p.pos.x - mw.x) * zoom, dy = (p.pos.y - mw.y) * zoom; float dd = std::sqrt(dx * dx + dy * dy); if (dd < best) { best = dd; hp = &p; } }
  int hn = hoverOrder.empty() ? 0 : hoverOrder.back();
  int hc = 0; bool hcResize = false;
  if (!hn) for (auto it = bp.nodes.rbegin(); it != bp.nodes.rend(); ++it) if (it->type == "Comment") { Lay& L = lay[it->id];
    if (mw.x >= L.a.x && mw.x <= L.b.x && mw.y >= L.a.y && mw.y <= L.a.y + L.hh) { hc = it->id; break; }
    if (std::fabs(mw.x - L.b.x) * zoom < 12 && std::fabs(mw.y - L.b.y) * zoom < 12) { hc = it->id; hcResize = true; break; } }
  bool otherActive = ImGui::IsAnyItemActive() && ImGui::GetActiveID() != canvasId;   // clicking the canvas makes it the active item
  bool free = canvasHover && !widgetHover && !widgetActive && !otherActive;
  // tips: a node explained in both languages; a pin's type
  if (free && !dragLink && !dragNodes && !boxSel) {
    if (hp) { std::vector<BpPinDef> st; const BpNode* n = bp.node(hp->node); const BpPinDef* p = n ? bpPin(bp, *n, hp->pin, &st) : nullptr;
      if (p) ImGui::SetTooltip("%s  (%s)\n%s  ·  %s", bpPinEn(p->id).empty() ? (p->out ? "Out" : "In") : bpPinEn(p->id).c_str(), pinTypeEn(p->type), visualText(bpPinAr(p->id)).c_str(), visualText(pinTypeAr(p->type)).c_str()); }
    else if (hn && ImGui::GetCurrentContext()->HoveredIdTimer > 0.45f) { const BpNode* n = bp.node(hn); const BpNodeDef* d = n ? bpDef(n->type) : nullptr;
      if (d) { ImGui::BeginTooltip(); ImGui::PushTextWrapPos(fs * 26); ImGui::TextColored(ImVec4(1, 1, 1, 1), "%s", d->en.c_str()); ImGui::TextColored(ImVec4(0.75f, 0.85f, 1, 1), "%s", visualText(d->ar).c_str());
        ImGui::Separator(); ImGui::TextUnformatted(d->descEn.c_str()); arWrapped(d->descAr, fs * 26);
        for (auto* m : nodeMsgs[hn]) { ImGui::Separator(); ImGui::TextColored(m->error ? ImVec4(1, 0.45f, 0.4f, 1) : ImVec4(1, 0.8f, 0.35f, 1), "%s", m->text.c_str()); arWrapped(m->textAr, fs * 26); }
        ImGui::PopTextWrapPos(); ImGui::EndTooltip(); } }
  }

  // ---- mouse
  if (free && io.MouseWheel != 0) { float nz = std::clamp(zoom * (io.MouseWheel > 0 ? 1.12f : 1 / 1.12f), 0.2f, 2.2f); ImVec2 before = toWorld(io.MousePos, O); zoom = nz; ImVec2 after = toWorld(io.MousePos, O); pan.x += after.x - before.x; pan.y += after.y - before.y; }
  if (free && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    if (hp && io.KeyAlt) { breakPin(hp->node, hp->pin); commit(ed); }
    else if (hp) { dragLink = true; linkNode = hp->node; linkPin = hp->pin; }
    else if (hn || hc) { int id = hn ? hn : hc; if (io.KeyCtrl) { if (sel.count(id)) sel.erase(id); else sel.insert(id); } else if (!sel.count(id)) { if (!io.KeyShift) sel.clear(); sel.insert(id); }
      if (hcResize) { resizeComment = id; } else { dragNodes = true; dragMoved = false;
        // a comment carries the nodes inside it
        std::set<int> carry = sel; for (int s : sel) if (const BpNode* c = bp.node(s)) if (c->type == "Comment") { Lay& L = lay[s]; for (auto& m : bp.nodes) if (m.id != s && m.x >= L.a.x && m.y >= L.a.y && lay[m.id].b.x <= L.b.x && lay[m.id].b.y <= L.b.y) carry.insert(m.id); }
        carryIds = carry; } }
    else { boxSel = true; boxA = io.MousePos; if (!io.KeyCtrl && !io.KeyShift) sel.clear(); }
  }
  if (dragNodes && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2)) { ImVec2 d = io.MouseDelta; for (int id : carryIds) if (BpNode* n = bp.node(id)) { n->x += d.x / zoom; n->y += d.y / zoom; } dragMoved = dragMoved || d.x != 0 || d.y != 0; }
  if (resizeComment && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1)) if (BpNode* n = bp.node(resizeComment)) { n->w = std::max(120.f, n->w + io.MouseDelta.x / zoom); n->h = std::max(80.f, n->h + io.MouseDelta.y / zoom); dragMoved = true; }
  if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    if (dragNodes || resizeComment) { if (dragMoved) { for (int id : carryIds) if (BpNode* n = bp.node(id)) { n->x = std::round(n->x / 16) * 16; n->y = std::round(n->y / 16) * 16; } commit(ed); } dragNodes = false; resizeComment = 0; carryIds.clear(); }
    if (boxSel) { ImVec2 a = toWorld(ImMin(boxA, io.MousePos), O), b = toWorld(ImMax(boxA, io.MousePos), O);
      for (auto& n : bp.nodes) { Lay& L = lay[n.id]; if (L.b.x >= a.x && L.a.x <= b.x && L.b.y >= a.y && L.a.y <= b.y) sel.insert(n.id); } boxSel = false; }
    if (dragLink) { dragLink = false;
      if (hp && !(hp->node == linkNode && hp->pin == linkPin)) { if (connect(linkNode, linkPin, hp->node, hp->pin)) commit(ed); else ed.notify("These pins can't be wired together"); }
      else if (!hp && canvasHover) { menuFromNode = linkNode; menuFromPin = linkPin; menuAt = mw; menuOpen = true; } }
  }
  // right / middle: pan, or (a right click without moving) the menus
  if (canvasHover && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle))) { panning = true; panMoved = 0; }
  if (panning && (ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle))) { pan.x += io.MouseDelta.x / zoom; pan.y += io.MouseDelta.y / zoom; panMoved += std::fabs(io.MouseDelta.x) + std::fabs(io.MouseDelta.y); }
  if (panning && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) { panning = false;
    if (panMoved < 4 && canvasHover) { if (hp) { ctxPinNode = hp->node; ctxPin = hp->pin; ImGui::OpenPopup("bpPinCtx"); } else if (hn || hc) { int id = hn ? hn : hc; if (!sel.count(id)) sel = {id}; ImGui::OpenPopup("bpNodeCtx"); }
      else { menuFromNode = 0; menuFromPin.clear(); menuAt = mw; menuOpen = true; } } }
  if (panning && ImGui::IsMouseReleased(ImGuiMouseButton_Middle)) panning = false;
  if (menuOpen) { ImGui::OpenPopup("bpAdd"); search.clear(); menuOpen = false; menuFocus = true; }

  // ---- keys
  if (canvasFocus && !otherActive) {
    bool ctrl = io.KeyCtrl;
    if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !sel.empty()) { deleteSel(); commit(ed); }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_A)) { sel.clear(); for (auto& n : bp.nodes) sel.insert(n.id); }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C) && !sel.empty()) copySel();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_X) && !sel.empty()) { copySel(); deleteSel(); commit(ed); }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V)) { if (pasteAt(canvasHover ? mw : toWorld({O.x + size.x / 2, O.y + size.y / 2}, O))) commit(ed); }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D) && !sel.empty()) { copySel(); ImVec2 at = mw; if (BpNode* f = bp.node(*sel.begin())) at = {f->x + 32, f->y + 32}; if (pasteAt(at)) commit(ed); }
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_F)) frameAll(size);
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_C) && canvasHover) { BpNode n; n.id = bp.nextId++; n.type = "Comment"; n.x = mw.x; n.y = mw.y; n.values["Text"] = "Comment";
      if (!sel.empty()) { float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f; for (int s : sel) { Lay& L = lay[s]; x0 = std::min(x0, L.a.x); y0 = std::min(y0, L.a.y); x1 = std::max(x1, L.b.x); y1 = std::max(y1, L.b.y); }
        n.x = x0 - 24; n.y = y0 - fs * 2.6f; n.w = x1 - x0 + 48; n.h = y1 - y0 + fs * 2.6f + 24; }
      bp.nodes.insert(bp.nodes.begin(), n); sel = {n.id}; commit(ed); }
  }

  // ---- menus
  if (ImGui::BeginPopup("bpNodeCtx")) {
    bool one = sel.size() == 1; BpNode* n = one ? bp.node(*sel.begin()) : nullptr;
    if (n && n->type == "Comment") { std::string t = n->values.value("Text", std::string()); ImGui::SetNextItemWidth(fs * 16); if (ImGui::InputText("##ct", &t)) n->values["Text"] = t; if (ImGui::IsItemDeactivatedAfterEdit()) commit(ed); }
    if (ImGui::MenuItem("Delete", "Del")) { deleteSel(); commit(ed); }
    if (ImGui::MenuItem("Duplicate", "Ctrl+D")) { copySel(); if (n) { if (pasteAt({n->x + 32, n->y + 32})) commit(ed); } else if (pasteAt(mw)) commit(ed); }
    if (ImGui::MenuItem("Copy", "Ctrl+C")) copySel();
    if (ImGui::MenuItem("Break All Links")) { bp.links.erase(std::remove_if(bp.links.begin(), bp.links.end(), [&](const BpLink& l) { return sel.count(l.from) || sel.count(l.to); }), bp.links.end()); commit(ed); }
    if (ImGui::MenuItem("Comment Selection", "C")) { ImGui::CloseCurrentPopup(); }
    ImGui::EndPopup(); }
  if (ImGui::BeginPopup("bpPinCtx")) { if (ImGui::MenuItem("Break Link(s)", "Alt+Click")) { breakPin(ctxPinNode, ctxPin); commit(ed); } ImGui::EndPopup(); }
  drawAddMenu(ed, lang);
}

float BpTab::inlineWidth(PinType t, float fs) const {
  switch (t) { case PinType::Bool: return fs * 1.9f; case PinType::Number: return fs * 4.4f; case PinType::String: case PinType::Any: return fs * 7.5f; case PinType::Vector: return fs * 10.5f; default: return 0; }
}
ImVec2 BpTab::toWorld(ImVec2 s, ImVec2 O) const { return ImVec2((s.x - O.x) / zoom - pan.x, (s.y - O.y) / zoom - pan.y); }
void BpTab::frameAll(ImVec2 size) {
  if (bp.nodes.empty()) { pan = {size.x / 2 / zoom, size.y / 2 / zoom}; return; }
  float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f; for (auto& n : bp.nodes) { x0 = std::min(x0, n.x); y0 = std::min(y0, n.y); x1 = std::max(x1, n.x + 260); y1 = std::max(y1, n.y + 140); }
  zoom = std::clamp(std::min(size.x / (x1 - x0 + 120), size.y / (y1 - y0 + 120)), 0.7f, 1.0f);   // readable; pan for the rest
  pan = {size.x / 2 / zoom - (x0 + x1) / 2, size.y / 2 / zoom - (y0 + y1) / 2};
}
void BpTab::breakPin(int node, const std::string& pin) {
  bp.links.erase(std::remove_if(bp.links.begin(), bp.links.end(), [&](const BpLink& l) { return (l.from == node && l.fromPin == pin) || (l.to == node && l.toPin == pin); }), bp.links.end());
}
// a wire between two pins (either way round): an input takes one wire, an exec output one too
bool BpTab::connect(int na, const std::string& pa, int nb, const std::string& pb) {
  const BpNode* A = bp.node(na); const BpNode* B = bp.node(nb); if (!A || !B || na == nb) return false;
  std::vector<BpPinDef> s1, s2; const BpPinDef* a = bpPin(bp, *A, pa, &s1); const BpPinDef* b = bpPin(bp, *B, pb, &s2); if (!a || !b || a->out == b->out) return false;
  if (!a->out) { std::swap(na, nb); std::swap(a, b); }
  const std::string& fromPin = a->id; const std::string& toPin = b->id;
  if (!pinCanLink(a->type, b->type)) return false;
  if (b->type != PinType::Exec) bp.links.erase(std::remove_if(bp.links.begin(), bp.links.end(), [&](const BpLink& l) { return l.to == nb && l.toPin == toPin; }), bp.links.end());
  else bp.links.erase(std::remove_if(bp.links.begin(), bp.links.end(), [&](const BpLink& l) { return l.from == na && l.fromPin == fromPin; }), bp.links.end());
  bp.links.push_back({na, fromPin, nb, toPin}); return true;
}
void BpTab::deleteSel() {
  bp.links.erase(std::remove_if(bp.links.begin(), bp.links.end(), [&](const BpLink& l) { return sel.count(l.from) || sel.count(l.to); }), bp.links.end());
  bp.nodes.erase(std::remove_if(bp.nodes.begin(), bp.nodes.end(), [&](const BpNode& n) { return sel.count(n.id) > 0; }), bp.nodes.end()); sel.clear();
}
void BpTab::copySel() {
  Blueprint c; for (auto& n : bp.nodes) if (sel.count(n.id)) c.nodes.push_back(n);
  for (auto& l : bp.links) if (sel.count(l.from) && sel.count(l.to)) c.links.push_back(l);
  json j = {{"premiumBlueprintNodes", bpToJson(c)}}; SDL_SetClipboardText(j.dump().c_str());
}
bool BpTab::pasteAt(ImVec2 at) {
  char* t = SDL_GetClipboardText(); std::string s = t ? t : ""; SDL_free(t);
  json j; try { j = json::parse(s); } catch (...) { return false; } if (!j.is_object() || !j.contains("premiumBlueprintNodes")) return false;
  Blueprint c = bpFromJson(j["premiumBlueprintNodes"]); if (c.nodes.empty()) return false;
  float x0 = 1e9f, y0 = 1e9f; for (auto& n : c.nodes) { x0 = std::min(x0, n.x); y0 = std::min(y0, n.y); }
  std::map<int, int> ids; sel.clear();
  for (auto& n : c.nodes) { BpNode m = n; m.id = bp.nextId++; ids[n.id] = m.id; m.x = std::round((at.x + n.x - x0) / 16) * 16; m.y = std::round((at.y + n.y - y0) / 16) * 16; bp.nodes.push_back(m); sel.insert(m.id); }
  for (auto& l : c.links) bp.links.push_back({ids[l.from], l.fromPin, ids[l.to], l.toPin});
  return true;
}

// the Add Node menu: every node by category, searched in English and Arabic; from a wire, only the ones that fit it
void BpTab::drawAddMenu(Editor& ed, int lang) {
  ImGui::SetNextWindowSizeConstraints({ImGui::GetFontSize() * 22, 0}, {ImGui::GetFontSize() * 30, ImGui::GetFontSize() * 34});
  if (!ImGui::BeginPopup("bpAdd")) return;
  const BpPinDef* from = nullptr; std::vector<BpPinDef> st; const BpNode* fn = menuFromNode ? bp.node(menuFromNode) : nullptr; if (fn) from = bpPin(bp, *fn, menuFromPin, &st);
  ImGui::TextDisabled("%s", from ? "All Actions for this pin" : "All Actions for this Blueprint"); ImGui::SameLine(); ImGui::TextDisabled("%s", visualText(from ? "كل اللي يناسب هذا المنفذ" : "كل العقد").c_str());
  if (menuFocus) { ImGui::SetKeyboardFocusHere(); menuFocus = false; }
  ImGui::SetNextItemWidth(-1); bool enter = ImGui::InputTextWithHint("##q", "Search (English or عربي)", &search, ImGuiInputTextFlags_EnterReturnsTrue);
  std::string q = search; std::transform(q.begin(), q.end(), q.begin(), [](unsigned char c) { return (char)std::tolower(c); });
  struct Item { std::string type, label, cat, tip, tipAr; json values; };
  std::vector<Item> items;
  auto fits = [&](const std::string& type, const json& values) -> std::string {   // the pin to wire to ("" = doesn't fit)
    if (!from) return "*"; BpNode t; t.type = type; t.values = values; for (auto& p : bpPins(bp, t)) if (p.out != from->out && (from->out ? pinCanLink(from->type, p.type) : pinCanLink(p.type, from->type))) return p.id; return ""; };
  auto label = [&](const std::string& en, const std::string& a) { return lang == 1 ? visualText(a) + "   ·   " + en : lang == 2 ? en + "   ·   " + visualText(a) : en + "   ·   " + visualText(a); };
  for (auto& v : bp.vars) { json vals = {{"Var", v.name}};
    if (fits("Var.Get", vals).size()) items.push_back({"Var.Get", label("Get " + v.name, "اقرأ " + v.name), "Variables", "", "", vals});
    if (fits("Var.Set", vals).size()) items.push_back({"Var.Set", label("Set " + v.name, "غيّر " + v.name), "Variables", "", "", vals}); }
  for (auto& n : bp.nodes) if (n.type == "Event.Custom") { std::string nm = n.values.value("Name", std::string("MyEvent")); json vals = {{"Event", nm}};
    if (fits("Flow.CallEvent", vals).size()) items.push_back({"Flow.CallEvent", label("Call " + nm, "شغّل " + nm), "Events", "", "", vals}); }
  for (auto& d : bpLibrary()) { if (d.type == "Var.Get" || d.type == "Var.Set" || d.type == "Flow.CallEvent") continue; if (from && d.kind == BpNodeDef::Comment) continue;
    if (fits(d.type, json::object()).empty()) continue; items.push_back({d.type, label(d.en, d.ar), d.cat, d.descEn, d.descAr, json::object()}); }
  auto match = [&](const Item& it) { if (q.empty()) return true; const BpNodeDef* d = bpDef(it.type); std::string hay = it.label + " " + it.type + " " + it.cat + " " + (d ? d->ar + " " + d->descEn + " " + d->descAr + " " + bpCategoryAr(d->cat) : "");
    std::transform(hay.begin(), hay.end(), hay.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    std::string low = q; return hay.find(low) != std::string::npos; };
  const Item* pick = nullptr; const Item* first = nullptr;
  ImGui::BeginChild("##list", {0, ImGui::GetFontSize() * 26});
  std::vector<std::string> cats; for (auto& it : items) if (std::find(cats.begin(), cats.end(), it.cat) == cats.end()) cats.push_back(it.cat);
  for (auto& c : cats) {
    int k = 0; for (auto& it : items) k += it.cat == c && match(it); if (!k) continue;
    std::string head = (lang == 1 ? visualText(bpCategoryAr(c)) + "   ·   " + c : c + "   ·   " + visualText(bpCategoryAr(c)));
    if (!q.empty()) ImGui::SetNextItemOpen(true); else if (from || c == "Events") ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
    if (ImGui::TreeNodeEx(head.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) {
      for (auto& it : items) if (it.cat == c && match(it)) { if (!first) first = &it;
        const BpNodeDef* d = bpDef(it.type); ImGui::PushStyleColor(ImGuiCol_Text, d && d->kind == BpNodeDef::Event ? IM_COL32(255, 140, 130, 255) : d && d->kind == BpNodeDef::Pure ? IM_COL32(170, 240, 150, 255) : IM_COL32(230, 230, 230, 255));
        if (ImGui::Selectable((it.label + "##" + it.type + it.values.dump()).c_str())) pick = &it; ImGui::PopStyleColor();
        if (ImGui::IsItemHovered() && !it.tip.empty()) { ImGui::BeginTooltip(); ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24); ImGui::TextUnformatted(it.tip.c_str()); ImGui::PopTextWrapPos(); arWrapped(it.tipAr, ImGui::GetFontSize() * 24); ImGui::EndTooltip(); } }
      ImGui::TreePop(); }
  }
  ImGui::EndChild();
  if (enter && first) pick = first;
  if (pick) {
    BpNode n; n.id = bp.nextId++; n.type = pick->type; n.values = pick->values; n.x = std::round(menuAt.x / 16) * 16; n.y = std::round(menuAt.y / 16) * 16;
    if (n.type == "Event.Custom") { std::string nm = "MyEvent"; for (int k = 2; ; k++) { bool used = false; for (auto& m : bp.nodes) used = used || (m.type == "Event.Custom" && m.values.value("Name", std::string("MyEvent")) == nm); if (!used) break; nm = "MyEvent" + std::to_string(k); } n.values["Name"] = nm; }
    if (from && from->out == false) n.x -= 260;   // dragged out of an input: the new node goes to its left
    bp.nodes.push_back(n); sel = {n.id};
    if (from) { std::string p = fits(n.type, n.values); if (!p.empty() && p != "*") connect(menuFromNode, menuFromPin, n.id, p); }
    commit(ed); ImGui::CloseCurrentPopup();
  }
  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}
}
