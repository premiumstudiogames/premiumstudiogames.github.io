// Menu bar, toolbar, viewport and the docked panels (Outliner, Details, World Settings, Content Browser, Output Log, ...).
#include "editor.h"
#include "platform.h"
#include "log.h"
#include "script.h"
#include <TextEditor.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>
#include <algorithm>
#include <cstring>

namespace fs = std::filesystem;
namespace pge {

static const ImU32 BLUE = IM_COL32(0, 112, 224, 255), GREEN = IM_COL32(60, 200, 90, 255), RED = IM_COL32(230, 70, 70, 255);
enum Icon { IC_Save, IC_Play, IC_Stop, IC_Add, IC_Cube, IC_Script, IC_Box, IC_Gear, IC_Window, IC_Select, IC_Down };
static void drawIcon(ImDrawList* dl, ImVec2 c, float s, int ic, ImU32 col) {
  float h = s * 0.5f;
  switch (ic) {
    case IC_Save: dl->AddRect({c.x - h, c.y - h}, {c.x + h, c.y + h}, col, 2, 0, 1.6f); dl->AddRectFilled({c.x - h * 0.5f, c.y - h}, {c.x + h * 0.5f, c.y - h * 0.3f}, col); dl->AddRect({c.x - h * 0.55f, c.y + h * 0.1f}, {c.x + h * 0.55f, c.y + h}, col); break;
    case IC_Play: dl->AddTriangleFilled({c.x - h * 0.7f, c.y - h}, {c.x - h * 0.7f, c.y + h}, {c.x + h, c.y}, col); break;
    case IC_Stop: dl->AddRectFilled({c.x - h * 0.8f, c.y - h * 0.8f}, {c.x + h * 0.8f, c.y + h * 0.8f}, col, 2); break;
    case IC_Add: dl->AddCircle(c, h, col, 20, 1.6f); dl->AddLine({c.x - h * 0.5f, c.y}, {c.x + h * 0.5f, c.y}, col, 1.8f); dl->AddLine({c.x, c.y - h * 0.5f}, {c.x, c.y + h * 0.5f}, col, 1.8f); break;
    case IC_Cube: { ImVec2 t(c.x, c.y - h), l(c.x - h, c.y - h * 0.45f), r(c.x + h, c.y - h * 0.45f), m(c.x, c.y + h * 0.1f), bl(c.x - h, c.y + h * 0.55f), br(c.x + h, c.y + h * 0.55f), b(c.x, c.y + h);
      dl->AddQuad(t, r, m, l, col, 1.5f); dl->AddLine(l, bl, col, 1.5f); dl->AddLine(r, br, col, 1.5f); dl->AddLine(m, b, col, 1.5f); dl->AddLine(bl, b, col, 1.5f); dl->AddLine(br, b, col, 1.5f); break; }
    case IC_Script: { ImFont* f = ImGui::GetFont(); float fs = s * 0.95f; ImVec2 ts = f->CalcTextSizeA(fs, 1e9f, 0, "{ }"); dl->AddText(f, fs, {c.x - ts.x / 2, c.y - ts.y / 2}, col, "{ }"); break; }
    case IC_Box: dl->AddRect({c.x - h, c.y - h * 0.5f}, {c.x + h, c.y + h}, col, 1, 0, 1.6f); dl->AddLine({c.x - h, c.y - h * 0.5f}, {c.x - h * 0.6f, c.y - h}, col, 1.5f); dl->AddLine({c.x + h, c.y - h * 0.5f}, {c.x + h * 0.6f, c.y - h}, col, 1.5f);
      dl->AddLine({c.x - h * 0.6f, c.y - h}, {c.x + h * 0.6f, c.y - h}, col, 1.5f); dl->AddLine({c.x - h * 0.25f, c.y - h * 0.5f}, {c.x + h * 0.25f, c.y - h * 0.5f}, col, 3); break;
    case IC_Gear: dl->AddCircle(c, h * 0.45f, col, 16, 1.8f); for (int i = 0; i < 8; i++) { float a = i * 3.14159f / 4; dl->AddLine({c.x + std::cos(a) * h * 0.6f, c.y + std::sin(a) * h * 0.6f}, {c.x + std::cos(a) * h, c.y + std::sin(a) * h}, col, 2.2f); } break;
    case IC_Window: dl->AddRect({c.x - h, c.y - h * 0.8f}, {c.x + h, c.y + h * 0.8f}, col, 2, 0, 1.6f); dl->AddLine({c.x - h, c.y - h * 0.4f}, {c.x + h, c.y - h * 0.4f}, col, 1.6f); break;
    case IC_Select: dl->AddTriangleFilled({c.x - h * 0.6f, c.y - h}, {c.x - h * 0.6f, c.y + h * 0.6f}, {c.x + h * 0.6f, c.y + h * 0.15f}, col); break;
    case IC_Down: dl->AddTriangleFilled({c.x - h * 0.5f, c.y - h * 0.25f}, {c.x + h * 0.5f, c.y - h * 0.25f}, {c.x, c.y + h * 0.3f}, col); break;
  }
}
// toolbar button: icon + optional label, returns clicked
static bool toolButton(const char* id, int icon, const char* label, ImU32 col, const char* tip, bool enabled = true) {
  ImGuiStyle& st = ImGui::GetStyle(); float h = ImGui::GetFrameHeight() + 6; float lw = label ? ImGui::CalcTextSize(label).x + 8 : 0;
  ImVec2 size(h + lw, h); ImGui::BeginDisabled(!enabled); bool r = ImGui::Button(id, size); ImGui::EndDisabled();
  ImVec2 mn = ImGui::GetItemRectMin(); ImDrawList* dl = ImGui::GetWindowDrawList(); ImU32 c = enabled ? col : IM_COL32(110, 110, 110, 255);
  drawIcon(dl, {mn.x + h / 2, mn.y + h / 2}, h * 0.45f, icon, c);
  if (label) dl->AddText({mn.x + h, mn.y + (h - ImGui::GetFontSize()) / 2}, ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled), label);
  if (tip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", tip);
  (void)st; return r;
}
static bool item(const char* label, const char* sc = nullptr, bool enabled = true, bool checked = false) { return ImGui::MenuItem(label, sc, checked, enabled); }
static void section(const char* t) { ImGui::Spacing(); ImGui::TextDisabled("%s", t); ImGui::Separator(); }

/* ------------------------------------------------------------------ menu bar (Unreal order) */
void Editor::drawMenuBar() {
  if (!ImGui::BeginMainMenuBar()) return;
  ImGui::TextColored(ImVec4(0.0f, 0.44f, 0.88f, 1), "PE"); ImGui::Spacing();
  Actor* s = actor(sel);
  if (ImGui::BeginMenu("File")) {
    section("OPEN"); if (item("New Level...", "Ctrl+N")) run("newLevel"); if (item("Open Level...", "Ctrl+O")) run("openLevel");
    section("SAVE"); if (item("Save Current Level", "Ctrl+S")) run("save"); if (item("Save All", "Ctrl+Shift+S")) run("saveAll");
    if (!console) { section("IMPORT / EXPORT"); if (item("Import Model or Picture...")) run("importAsset"); if (item("Import Into Level...")) run("importLevel"); if (item("Open Project Folder")) openInExplorer(fs::u8path(P.path).parent_path().u8string()); }
    section("PROJECT"); if (item("New Project...")) run("newProject"); if (item("Open Project...")) run("openProject"); if (item("Package Project")) run("package");
    if (ImGui::BeginMenu("Recent Projects")) { for (auto& r : recent) if (item(fs::u8path(r).stem().stem().u8string().c_str())) { if (dirty) save(); openProjectFile(r); break; } ImGui::EndMenu(); }
    ImGui::Separator(); if (item("Exit")) run("exit");
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Edit")) {
    section("HISTORY"); if (item("Undo", "Ctrl+Z", canUndo())) run("undo"); if (item("Redo", "Ctrl+Y", canRedo())) run("redo");
    section("EDIT"); if (item("Cut", "Ctrl+X", s)) run("cut"); if (item("Copy", "Ctrl+C", s)) run("copy"); if (item("Paste", "Ctrl+V")) run("paste");
    if (item("Duplicate", "Ctrl+D", s)) run("duplicate"); if (item("Delete", "Delete", s)) run("delete"); if (item("Rename", "F2", s)) run("rename");
    section("CONFIGURATION"); if (item("Editor Preferences...")) run("prefs"); if (item("Project Settings...")) run("projectSettings");
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Window")) {
    section("LEVEL EDITOR"); ImGui::MenuItem("Place Actors", nullptr, &wPlaceActors); ImGui::MenuItem("Outliner", nullptr, &wOutliner); ImGui::MenuItem("Details", nullptr, &wDetails); ImGui::MenuItem("World Settings", nullptr, &wWorld);
    section("GENERAL"); ImGui::MenuItem("Content Browser", "Ctrl+Space", &wContent); ImGui::MenuItem("Output Log", nullptr, &wLog); ImGui::MenuItem("Script Editor", nullptr, &wScripts); ImGui::MenuItem("Blueprint Editor", nullptr, &wBlueprints);
    section("LAYOUT"); if (item("Load Layout: Default")) run("resetLayout");
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Tools")) {
    section("SCRIPTING"); if (item("New Blueprint...")) run("newBlueprint"); if (item("New Script...")) run("newScript"); if (item("New Material...")) run("newMaterial"); if (item("Script API Reference")) run("apiRef");
    section("DEBUG"); if (item("Statistics")) run("stats"); if (item("Toggle FPS Counter", nullptr, true, showStats)) run("toggleFps");
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Build")) {
    section("BUILD"); if (item("Build All Levels")) run("buildAll"); if (item("Compile Scripts", "F7")) run("compile");
    section("VERIFICATION"); if (item("Map Check")) run("mapCheck");
    section("PACKAGE"); if (item("Package Project (Windows)")) run("package");
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Select")) {
    section("SELECTION"); if (item("Unselect All", "Esc", s)) run("deselect"); if (item("Select Parent", nullptr, s && actor(s->parent))) run("selectParent");
    section("BY TYPE"); if (item("Select Next: Same Mesh", nullptr, s && s->type == "StaticMesh")) run("selSameMesh"); if (item("Select Next: Same Material", nullptr, s && s->type == "StaticMesh")) run("selSameMat");
    if (item("Select Next: Light")) run("selLight");
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Actor")) {
    section("PLACE"); if (ImGui::BeginMenu("Place Actor")) { addMenuItems(nullptr); ImGui::EndMenu(); }
    section("TRANSFORM"); if (item("Snap to Floor", "End", s)) run("snapFloor"); if (item("Reset Location", nullptr, s)) run("resetP"); if (item("Reset Rotation", nullptr, s)) run("resetR"); if (item("Reset Scale", nullptr, s)) run("resetS");
    section("ATTACHMENT"); if (item("Detach", nullptr, s && !s->parent.empty())) run("detach");
    section("VISIBILITY"); if (item(s && !s->visible ? "Show Selected" : "Hide Selected", "H", s)) run("hide"); if (item("Show All", "Ctrl+H")) run("showAll");
    section("VIEW"); if (item("Focus Selected", "F", s)) run("focus"); if (item("Frame All", "Home")) run("frameAll");
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Help")) {
    section("HELP"); if (item("Documentation", "F1")) run("docs"); if (item("Keyboard Shortcuts")) run("shortcuts"); if (item("Script API Reference")) run("apiRef");
    ImGui::Separator(); if (item("About Premium Engine")) run("about");
    ImGui::EndMenu();
  }
  // project name on the right, like Unreal
  std::string pn = P.name + (dirty ? " *" : ""); float w = ImGui::CalcTextSize(pn.c_str()).x;
  ImGui::SameLine(ImGui::GetWindowWidth() - w - 20); ImGui::TextDisabled("%s", pn.c_str());
  ImGui::EndMainMenuBar();
}

void Editor::addMenuItems(const glm::vec3* at) {
  section("BASIC");
  const char* basic[][2] = {{"Empty Actor", "Empty"}, {"Player Start", "PlayerStart"}, {"Trigger Box", "TriggerBox"}, {"Camera Actor", "CameraActor"}, {"Text Render", "TextRender"}};
  for (auto& b : basic) if (ImGui::MenuItem(b[0])) addActor(b[1], "", at);
  section("SHAPES"); for (int i = 0; i < MESH_COUNT; i++) if (ImGui::MenuItem(MESHES[i])) addActor("StaticMesh", MESHES[i], at);
  section("LIGHTS"); for (const char* t : {"DirectionalLight", "PointLight", "SpotLight"}) if (ImGui::MenuItem(actorTypeLabel(t))) addActor(t, "", at);
}

/* ------------------------------------------------------------------ toolbar */
void Editor::drawToolbar() {
  ImGuiViewport* vp = ImGui::GetMainViewport(); float h = ImGui::GetFrameHeight() + 18;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8, 6}); ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(26, 26, 26, 255));
  if (ImGui::BeginViewportSideBar("##Toolbar", vp, ImGuiDir_Up, h, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar)) {
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
    if (toolButton("##save", IC_Save, nullptr, IM_COL32(210, 210, 210, 255), "Save (Ctrl+S)")) run("save");
    ImGui::SameLine(); ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical); ImGui::SameLine();
    if (toolButton("##mode", IC_Select, "Selection Mode", IM_COL32(210, 210, 210, 255), "Editing mode")) {}
    ImGui::SameLine(); ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical); ImGui::SameLine();
    if (toolButton("##add", IC_Add, nullptr, GREEN, "Quick Add to the Project")) ImGui::OpenPopup("quickAdd");
    if (ImGui::BeginPopup("quickAdd")) { addMenuItems(nullptr); section("CONTENT"); if (ImGui::MenuItem("New Material")) run("newMaterial"); if (ImGui::MenuItem("New Blueprint")) run("newBlueprint"); if (ImGui::MenuItem("New Script")) run("newScript"); if (ImGui::MenuItem("New Level")) run("newLevel"); ImGui::EndPopup(); }
    ImGui::SameLine();
    if (toolButton("##scripts", IC_Script, nullptr, IM_COL32(100, 170, 255, 255), "Scripts")) ImGui::OpenPopup("scriptsPop");
    if (ImGui::BeginPopup("scriptsPop")) {
      section("SCRIPTS"); if (ImGui::MenuItem("New Blueprint...")) run("newBlueprint"); if (ImGui::MenuItem("New Script...")) run("newScript");
      if (Actor* a = actor(sel)) if (ImGui::MenuItem("Open Script of Selected Actor", nullptr, false, !a->script.empty())) openScript(a->script);
      section("PROJECT SCRIPTS"); for (auto& [id, s] : P.scripts) if (ImGui::MenuItem(s.name.c_str())) openScript(id);
      ImGui::EndPopup(); }
    ImGui::SameLine(); ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical); ImGui::SameLine();
    // play controls
    if (!playing) { if (toolButton("##play", IC_Play, nullptr, GREEN, "Play in the viewport (Alt+P)")) play(); }
    else if (toolButton("##stop", IC_Stop, nullptr, RED, "Stop (Esc)")) stopPlay();
    ImGui::SameLine(0, 0); if (toolButton("##playopts", IC_Down, nullptr, IM_COL32(200, 200, 200, 255), "Play options")) ImGui::OpenPopup("playOpts");
    if (ImGui::BeginPopup("playOpts")) {
      section("PLAY"); if (ImGui::MenuItem("Selected Viewport", "Alt+P")) play(); if (ImGui::MenuItem("New Window (standalone game)")) playStandalone(); ImGui::MenuItem("Play as the Console Version", nullptr, &consolePreview);
      section("GAME MODE"); for (const char* m : {"FirstPerson", "ThirdPerson", "Spectator"}) if (ImGui::MenuItem(m, nullptr, L().world.gameMode == m)) run(std::string("gm:") + m);
      ImGui::EndPopup(); }
    ImGui::SameLine(); ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical); ImGui::SameLine();
    if (toolButton("##platforms", IC_Box, "Platforms", IM_COL32(230, 180, 80, 255), "Package the game")) ImGui::OpenPopup("platforms");
    if (ImGui::BeginPopup("platforms")) {
      section("PACKAGE"); if (!console && ImGui::MenuItem("Windows (.exe)")) packageProject(); if (ImGui::MenuItem("Console (controller, up to 2 GB)")) packageProject(true); if (!console && ImGui::MenuItem("Open Builds Folder")) { std::error_code ec; fs::create_directories(fs::u8path(buildsDir()), ec); openInExplorer(buildsDir()); }
      section("SETTINGS"); if (ImGui::MenuItem("Packaging Settings...")) run("projectSettings");
      ImGui::EndPopup(); }
    float right = ImGui::GetWindowWidth() - 130; ImGui::SameLine(std::max(right, ImGui::GetCursorPosX()));
    if (toolButton("##settings", IC_Gear, "Settings", IM_COL32(200, 200, 200, 255), "Settings")) ImGui::OpenPopup("settingsPop");
    if (ImGui::BeginPopup("settingsPop")) { if (ImGui::MenuItem("Project Settings...")) run("projectSettings"); if (ImGui::MenuItem("World Settings")) run("worldSettings"); if (ImGui::MenuItem("Editor Preferences...")) run("prefs"); ImGui::EndPopup(); }
    ImGui::PopStyleColor();
  }
  ImGui::End(); ImGui::PopStyleColor(); ImGui::PopStyleVar();
}

void Editor::drawStatusBar() {
  ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(21, 21, 21, 255));
  if (ImGui::BeginViewportSideBar("##Status", vp, ImGuiDir_Down, ImGui::GetFrameHeight() + 6, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_MenuBar)) {
    if (ImGui::BeginMenuBar()) {
      if (ImGui::Button("Content Browser")) run("toggleContent"); ImGui::SameLine(); if (ImGui::Button("Output Log")) run("showLog");
      ImGui::SameLine(); ImGui::TextDisabled("Cmd"); ImGui::SameLine(); ImGui::SetNextItemWidth(260);
      if (ImGui::InputTextWithHint("##cmd", "Enter Console Command", cmd, sizeof cmd, ImGuiInputTextFlags_EnterReturnsTrue)) {
        std::string c = cmd; cmd[0] = 0; logMsg("> " + c);
        if (c == "help") logMsg("Commands: play, stop, save, stat fps, compile, mapcheck, clear, frame, viewmode lit|unlit|wireframe|normals, open <level>");
        else if (c == "play") play(); else if (c == "stop") stopPlay(); else if (c == "save") save(); else if (c == "stat fps") run("toggleFps");
        else if (c == "compile") compileScripts(false); else if (c == "mapcheck") runMapCheck(); else if (c == "clear") logClear(); else if (c == "frame") frameAll();
        else if (c.rfind("viewmode ", 0) == 0) { std::string m = c.substr(9); viewMode = m == "unlit" ? VM_Unlit : m == "wireframe" ? VM_Wire : m == "normals" ? VM_Normals : VM_Lit; }
        else if (c.rfind("open ", 0) == 0) openLevel(c.substr(5));
        else if (!c.empty()) logMsg("warning: unknown command '" + c + "' (type help)", LogKind::Warn);
      }
      std::string st = dirty ? "Unsaved changes" : "All Saved"; float w = ImGui::CalcTextSize(st.c_str()).x + ImGui::CalcTextSize("Premium Engine " PGE_VERSION).x + 40;
      ImGui::SameLine(ImGui::GetWindowWidth() - w); ImGui::TextColored(dirty ? ImVec4(1, 0.75f, 0.3f, 1) : ImVec4(0.5f, 0.8f, 0.5f, 1), "%s", st.c_str());
      ImGui::SameLine(); ImGui::TextDisabled("Premium Engine " PGE_VERSION);
      ImGui::EndMenuBar();
    }
  }
  ImGui::End(); ImGui::PopStyleColor();
}

/* ------------------------------------------------------------------ viewport */
static bool vpMenuButton(const char* label, const char* popup) {
  ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(20, 20, 20, 200)); bool r = ImGui::Button(label); ImGui::PopStyleColor();
  if (r) ImGui::OpenPopup(popup); return r;
}
void Editor::drawViewport() {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
  bool open = ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse);
  ImGui::PopStyleVar();
  if (!open) { ImGui::End(); return; }
  ImVec2 pos = ImGui::GetCursorScreenPos(), size = ImGui::GetContentRegionAvail(); size.x = std::max(size.x, 8.f); size.y = std::max(size.y, 8.f);
  vpX = pos.x; vpY = pos.y; vpW = size.x; vpH = size.y;
  float sc = ImGui::GetIO().DisplayFramebufferScale.x; int pw = (int)(size.x * sc), ph = (int)(size.y * sc);
  R.resizeTarget(vpRT, pw, ph);
  vpFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
  Frame F; Camera C;
  if (playing && game) { game->buildFrame(F, C, size.x / size.y); }
  else buildFrame(F, C, size.x / size.y);
  R.render(F, C, pw, ph, &vpRT);
  ImGui::Image((ImTextureID)(intptr_t)vpRT.color, size, {0, 1}, {1, 0});
  bool imgHovered = ImGui::IsItemHovered();
  // drag & drop from Place Actors / Content Browser
  if (!playing && ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("PGE_ADD")) { std::string s((const char*)p->Data, p->DataSize); auto bar = s.find('|'); glm::vec3 at = dropPoint(ImGui::GetMousePos().x, ImGui::GetMousePos().y);
      Actor& a = addActor(s.substr(0, bar), bar == std::string::npos ? "" : s.substr(bar + 1), &at); (void)a; }
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("PGE_MAT")) { std::string m((const char*)p->Data, p->DataSize);
      unsigned id = R.pick(F, C, vpRT, (int)((ImGui::GetMousePos().x - vpX) * sc), (int)((ImGui::GetMousePos().y - vpY) * sc));
      if (id >= 1 && id <= L().actors.size() && L().actors[id - 1].type == "StaticMesh") { L().actors[id - 1].material = m; sel = L().actors[id - 1].id; touch(); logMsg("Applied " + m + " to " + L().actors[id - 1].name); } }
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("PGE_SCRIPT")) { std::string m((const char*)p->Data, p->DataSize);
      unsigned id = R.pick(F, C, vpRT, (int)((ImGui::GetMousePos().x - vpX) * sc), (int)((ImGui::GetMousePos().y - vpY) * sc));
      if (id >= 1 && id <= L().actors.size()) { L().actors[id - 1].script = m; sel = L().actors[id - 1].id; touch(); logMsg("Added " + m + " to " + L().actors[id - 1].name); } }
    ImGui::EndDragDropTarget();
  }
  ImDrawList* dl = ImGui::GetWindowDrawList();
  if (playing && game) {
    dl->PushClipRect(pos, {pos.x + size.x, pos.y + size.y}); game->drawHud(dl, pos.x, pos.y, size.x, size.y);
    const char* msg = mouseFree ? "Playing in editor - click the viewport to control the game - Esc stops" : "Playing in editor - Esc stops - Shift+F1 frees the mouse";
    ImVec2 ts = ImGui::CalcTextSize(msg); ImVec2 bp(pos.x + (size.x - ts.x) / 2 - 12, pos.y + size.y - ts.y - 26);
    dl->AddRectFilled(bp, {bp.x + ts.x + 24, bp.y + ts.y + 12}, IM_COL32(0, 0, 0, 160), 6); dl->AddText({bp.x + 12, bp.y + 6}, IM_COL32(255, 255, 255, 220), msg);
    dl->PopClipRect();
    vpHovered = imgHovered;
  } else {
    // gizmo
    Actor* s = actor(sel); bool gizmoOver = false;
    if (s) {
      ImGuizmo::SetOrthographic(view != View::Perspective); ImGuizmo::SetDrawlist(dl); ImGuizmo::SetRect(pos.x, pos.y, size.x, size.y);
      glm::mat4 W = worldOf(*s); float snap[3]; float* snapPtr = nullptr;
      ImGuizmo::OPERATION op = gizmoOp == 0 ? ImGuizmo::TRANSLATE : gizmoOp == 1 ? ImGuizmo::ROTATE : ImGuizmo::SCALE;
      if (gizmoOp == 0 && snapT) { snap[0] = snap[1] = snap[2] = snapTv; snapPtr = snap; } if (gizmoOp == 1 && snapR) { snap[0] = snapRv; snapPtr = snap; } if (gizmoOp == 2 && snapS) { snap[0] = snap[1] = snap[2] = snapSv; snapPtr = snap; }
      if (ImGuizmo::Manipulate(glm::value_ptr(C.view), glm::value_ptr(C.proj), op, gizmoLocal || gizmoOp == 2 ? ImGuizmo::LOCAL : ImGuizmo::WORLD, glm::value_ptr(W), nullptr, snapPtr)) {
        glm::mat4 PW = actor(s->parent) ? worldOf(*actor(s->parent)) : glm::mat4(1); glm::mat4 local = glm::inverse(PW) * W;
        glm::vec3 sc3(glm::length(glm::vec3(local[0])), glm::length(glm::vec3(local[1])), glm::length(glm::vec3(local[2])));
        glm::mat3 Rm(glm::vec3(local[0]) / sc3.x, glm::vec3(local[1]) / sc3.y, glm::vec3(local[2]) / sc3.z);
        if (gizmoOp == 0) s->p = glm::vec3(local[3]);
        else if (gizmoOp == 1) s->r = glm::degrees(eulerYXZ(glm::normalize(glm::quat_cast(Rm))));
        else s->s = sc3 * glm::sign(s->s + glm::vec3(1e-9f));
        touch();
      }
      gizmoOver = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
    }
    vpHovered = imgHovered && !ImGuizmo::IsUsing();
    // click to select
    static ImVec2 downAt; static bool downIn = false;
    if (imgHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !gizmoOver && !ImGui::GetIO().KeyAlt) { downAt = ImGui::GetMousePos(); downIn = true; }
    if (downIn && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) { downIn = false; ImVec2 m = ImGui::GetMousePos();
      if (std::abs(m.x - downAt.x) + std::abs(m.y - downAt.y) < 5 && imgHovered) {
        unsigned id = R.pick(F, C, vpRT, (int)((m.x - pos.x) * sc), (int)((m.y - pos.y) * sc));
        sel = id >= 1 && id <= L().actors.size() ? L().actors[id - 1].id : ""; } }
    // right click without moving: context menu
    static ImVec2 rdown; if (imgHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) rdown = ImGui::GetMousePos();
    if (imgHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && std::abs(ImGui::GetMousePos().x - rdown.x) + std::abs(ImGui::GetMousePos().y - rdown.y) < 4 && !flying) ImGui::OpenPopup("vpContext");
    if (ImGui::BeginPopup("vpContext")) {
      static glm::vec3 at; if (ImGui::IsWindowAppearing()) at = dropPoint(rdown.x, rdown.y);
      if (Actor* a = actor(sel)) { ImGui::TextDisabled("%s", a->name.c_str()); if (ImGui::MenuItem("Focus", "F")) focusSelected(); if (ImGui::MenuItem("Duplicate", "Ctrl+D")) duplicateSelected();
        if (ImGui::MenuItem("Snap to Floor", "End")) snapToFloor(); if (ImGui::MenuItem("Delete", "Delete")) deleteSelected(); ImGui::Separator(); }
      if (ImGui::BeginMenu("Place Actor")) { addMenuItems(&at); ImGui::EndMenu(); }
      if (ImGui::MenuItem("Paste", "Ctrl+V")) paste();
      ImGui::EndPopup();
    }
    cameraInput(dt_ > 0 ? dt_ : 1.f / 60);
  }
  // overlay toolbar (top of the viewport, like Unreal)
  ImGui::SetCursorScreenPos({pos.x + 6, pos.y + 6});
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {7, 3});
  if (!playing) {
    vpMenuButton("=", "vpView"); ImGui::SameLine();
    const char* views[] = {"Perspective", "Top", "Front", "Right"}; vpMenuButton(views[(int)view], "vpCam"); ImGui::SameLine();
    const char* modes[] = {"Lit", "Unlit", "Wireframe", "Normals"}; vpMenuButton(modes[viewMode], "vpMode"); ImGui::SameLine();
    vpMenuButton("Show", "vpShow");
    if (ImGui::BeginPopup("vpView")) { if (ImGui::MenuItem("Frame All", "Home")) frameAll(); if (ImGui::MenuItem("Focus Selected", "F", false, actor(sel))) focusSelected(); ImGui::Separator();
      if (ImGui::MenuItem("Game View (hide editor icons)", "G", !showIcons)) showIcons = !showIcons; ImGui::EndPopup(); }
    if (ImGui::BeginPopup("vpCam")) { section("PERSPECTIVE"); if (ImGui::MenuItem("Perspective", nullptr, view == View::Perspective)) view = View::Perspective; section("ORTHOGRAPHIC");
      for (int i = 1; i < 4; i++) if (ImGui::MenuItem(views[i], nullptr, (int)view == i)) { view = (View)i; if (Actor* a = actor(sel)) orthoCenter = glm::vec3(worldOf(*a)[3]); } ImGui::EndPopup(); }
    if (ImGui::BeginPopup("vpMode")) { section("VIEW MODE"); for (int i = 0; i < 4; i++) if (ImGui::MenuItem(modes[i], nullptr, viewMode == i)) viewMode = i; ImGui::EndPopup(); }
    if (ImGui::BeginPopup("vpShow")) { ImGui::MenuItem("Grid", nullptr, &showGrid); ImGui::MenuItem("Editor Icons", nullptr, &showIcons); ImGui::MenuItem("Stats (FPS)", nullptr, &showStats); ImGui::EndPopup(); }
    // right side: transform tools and snapping
    float rw = 430; ImGui::SetCursorScreenPos({pos.x + size.x - rw, pos.y + 6});
    auto tog = [&](const char* l, bool on, const char* tip) { ImGui::PushStyleColor(ImGuiCol_Button, on ? IM_COL32(0, 112, 224, 255) : IM_COL32(20, 20, 20, 200)); bool r = ImGui::Button(l); ImGui::PopStyleColor(); if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip); return r; };
    if (tog("Move", gizmoOp == 0, "Select and translate (W)")) gizmoOp = 0; ImGui::SameLine(0, 2); if (tog("Rotate", gizmoOp == 1, "Select and rotate (E)")) gizmoOp = 1; ImGui::SameLine(0, 2); if (tog("Scale", gizmoOp == 2, "Select and scale (R)")) gizmoOp = 2;
    ImGui::SameLine(); if (tog(gizmoLocal ? "Local" : "World", false, "Coordinate system")) gizmoLocal = !gizmoLocal;
    ImGui::SameLine(); if (tog("#", snapT, "Grid snap")) snapT = !snapT; ImGui::SameLine(0, 1); char b[16]; std::snprintf(b, sizeof b, "%g##st", snapTv); vpMenuButton(b, "snapT");
    ImGui::SameLine(); if (tog("<", snapR, "Rotation snap")) snapR = !snapR; ImGui::SameLine(0, 1); std::snprintf(b, sizeof b, "%g%s##sr", snapRv, "deg"); vpMenuButton(b, "snapR");
    ImGui::SameLine(); std::snprintf(b, sizeof b, "Cam %d##cs", (int)prefs.camSpeed); vpMenuButton(b, "camSpeed");
    if (ImGui::BeginPopup("snapT")) { for (float v : {0.01f, 0.1f, 0.25f, 0.5f, 1.f, 2.f, 5.f, 10.f}) { char t[16]; std::snprintf(t, sizeof t, "%g", v); if (ImGui::MenuItem(t, nullptr, snapTv == v)) { snapTv = v; snapT = true; } } ImGui::EndPopup(); }
    if (ImGui::BeginPopup("snapR")) { for (float v : {5.f, 10.f, 15.f, 30.f, 45.f, 90.f}) { char t[16]; std::snprintf(t, sizeof t, "%g deg", v); if (ImGui::MenuItem(t, nullptr, snapRv == v)) { snapRv = v; snapR = true; } } ImGui::EndPopup(); }
    if (ImGui::BeginPopup("camSpeed")) { for (int v = 1; v <= 8; v++) if (ImGui::MenuItem(("Speed " + std::to_string(v)).c_str(), nullptr, (int)prefs.camSpeed == v)) { prefs.camSpeed = (float)v; savePrefs(); } ImGui::EndPopup(); }
  }
  ImGui::PopStyleVar();
  if (showStats) {
    char st[160]; std::snprintf(st, sizeof st, "%.0f FPS  %.1f ms\n%d draws  %d tris", fps_, 1000.f / std::max(fps_, 1.f), R.drawCalls, R.triangles);
    ImVec2 ts = ImGui::CalcTextSize(st); dl->AddText({pos.x + size.x - ts.x - 12, pos.y + 40}, IM_COL32(120, 255, 120, 255), st);
  }
  if (!playing) { std::string lv = "Level: " + level; dl->AddText({pos.x + 10, pos.y + size.y - 24}, IM_COL32(255, 255, 255, 120), lv.c_str()); }
  ImGui::End();
}

/* ------------------------------------------------------------------ Outliner */
void Editor::drawOutliner() {
  if (!ImGui::Begin("Outliner", &wOutliner)) { ImGui::End(); return; }
  ImGui::SetNextItemWidth(-34); ImGui::InputTextWithHint("##olf", "Search...", &olFilter); ImGui::SameLine();
  if (ImGui::Button("+", {26, 0})) ImGui::OpenPopup("olAdd");
  if (ImGui::BeginPopup("olAdd")) { addMenuItems(nullptr); ImGui::EndPopup(); }
  auto& acts = L().actors; std::string lf = olFilter; std::transform(lf.begin(), lf.end(), lf.begin(), ::tolower);
  auto match = [&](const Actor& a) { if (lf.empty()) return true; std::string n = a.name; std::transform(n.begin(), n.end(), n.begin(), ::tolower); return n.find(lf) != std::string::npos; };
  std::string dropOn, dragId; bool dropRoot = false;
  if (ImGui::BeginTable("ol", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, ImVec2(0, -ImGui::GetFrameHeightWithSpacing()))) {
    ImGui::TableSetupScrollFreeze(0, 1); ImGui::TableSetupColumn("Item Label", ImGuiTableColumnFlags_WidthStretch); ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 90); ImGui::TableSetupColumn("##vis", ImGuiTableColumnFlags_WidthFixed, 24);
    ImGui::TableHeadersRow();
    // level root row
    ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    bool rootOpen = ImGui::TreeNodeEx("##root", ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_DefaultOpen, "%s (Editor)", level.c_str());
    if (ImGui::BeginDragDropTarget()) { if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("PGE_ACTOR")) { dragId.assign((const char*)p->Data, p->DataSize); dropRoot = true; } ImGui::EndDragDropTarget(); }
    ImGui::TableNextColumn(); ImGui::TextDisabled("World");
    if (rootOpen) {
      std::function<void(const std::string&)> rows = [&](const std::string& parent) {
        for (size_t i = 0; i < acts.size(); i++) { Actor& a = acts[i]; if (a.parent != parent && !(parent.empty() && !actor(a.parent) && !a.parent.empty())) continue;
          bool hasKids = false; for (auto& c : acts) if (c.parent == a.id) { hasKids = true; break; }
          if (!lf.empty() && !match(a) && !hasKids) continue;
          ImGui::PushID(a.id.c_str()); ImGui::TableNextRow(); ImGui::TableNextColumn();
          ImGuiTreeNodeFlags fl = ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen | (hasKids ? 0 : ImGuiTreeNodeFlags_Leaf) | (a.id == sel ? ImGuiTreeNodeFlags_Selected : 0);
          if (!a.visible) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
          bool open = ImGui::TreeNodeEx("##n", fl, "%s %s", actorTypeIcon(a.type), a.name.c_str());
          if (!a.visible) ImGui::PopStyleColor();
          if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) sel = a.id;
          if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) { sel = a.id; focusSelected(); }
          if (ImGui::BeginPopupContextItem("ctx")) { sel = a.id; ImGui::TextDisabled("%s", a.name.c_str());
            if (ImGui::MenuItem("Rename", "F2")) run("rename"); if (ImGui::MenuItem("Duplicate", "Ctrl+D")) duplicateSelected(); if (ImGui::MenuItem("Focus", "F")) focusSelected();
            if (ImGui::MenuItem("Detach", nullptr, false, !a.parent.empty())) attach(a.id, ""); if (ImGui::MenuItem("Delete", "Delete")) deleteSelected(); ImGui::EndPopup(); }
          if (ImGui::BeginDragDropSource()) { ImGui::SetDragDropPayload("PGE_ACTOR", a.id.data(), a.id.size()); ImGui::Text("Attach %s to...", a.name.c_str()); ImGui::EndDragDropSource(); }
          if (ImGui::BeginDragDropTarget()) { if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("PGE_ACTOR")) { dragId.assign((const char*)p->Data, p->DataSize); dropOn = a.id; } ImGui::EndDragDropTarget(); }
          ImGui::TableNextColumn(); ImGui::TextDisabled("%s", actorTypeLabel(a.type));
          ImGui::TableNextColumn(); bool v = a.visible; if (ImGui::Checkbox("##v", &v)) { a.visible = v; touch(); } if (ImGui::IsItemHovered()) ImGui::SetTooltip("Visible");
          if (open) { std::string id = a.id; ImGui::PopID(); rows(id); ImGui::TreePop(); } else ImGui::PopID();
        }
      };
      rows(""); ImGui::TreePop();
    }
    ImGui::EndTable();
  }
  if (!dragId.empty()) { if (dropRoot) attach(dragId, ""); else if (!dropOn.empty()) attach(dragId, dropOn); }
  ImGui::TextDisabled("%d actors%s", (int)acts.size(), actor(sel) ? " (1 selected)" : "");
  ImGui::End();
}

/* ------------------------------------------------------------------ Details */
static bool vec3Row(const char* label, glm::vec3& v, float speed, const glm::vec3& reset, const char* fmt = "%.2f") {
  bool ch = false; ImGui::PushID(label); ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(label);
  ImGui::TableNextColumn(); float w = (ImGui::GetContentRegionAvail().x - 26) / 3 - 4; const ImU32 cols[3] = {IM_COL32(200, 60, 60, 255), IM_COL32(80, 170, 60, 255), IM_COL32(50, 110, 220, 255)};
  for (int i = 0; i < 3; i++) { if (i) ImGui::SameLine(0, 4); ImVec2 p = ImGui::GetCursorScreenPos(); ImGui::SetNextItemWidth(w); ImGui::PushID(i);
    ch |= ImGui::DragFloat("##c", &v[i], speed, 0, 0, fmt); ImGui::PopID(); ImGui::GetWindowDrawList()->AddRectFilled(p, {p.x + 3, p.y + ImGui::GetFrameHeight()}, cols[i]); }
  ImGui::SameLine(0, 4); if (ImGui::Button("R", {22, 0})) { v = reset; ch = true; } if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset to default");
  ImGui::PopID(); return ch;
}
static void rowLabel(const char* l) { ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(l); ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1); }
static bool beginProps(const char* id) { return ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX); }
static void propCols() { ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, 110); ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch); }

void Editor::drawDetails() {
  if (!ImGui::Begin("Details", &wDetails)) { ImGui::End(); return; }
  Actor* a = actor(sel);
  if (!a) { ImGui::Spacing(); ImGui::TextDisabled("Select an object to view details."); ImGui::End(); return; }
  bool ch = false;
  ImGui::SetNextItemWidth(-1); std::string nm = a->name; if (ImGui::InputText("##name", &nm, ImGuiInputTextFlags_EnterReturnsTrue) && !nm.empty()) { a->name = nm; ch = true; }
  ImGui::TextDisabled("%s  -  %s", actorTypeLabel(a->type), a->id.c_str());
  if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen) && beginProps("tr")) { propCols();
    ch |= vec3Row("Location", a->p, 0.05f, glm::vec3(0)); ch |= vec3Row("Rotation", a->r, 0.5f, glm::vec3(0), "%.1f"); ch |= vec3Row("Scale", a->s, 0.02f, glm::vec3(1), "%.3f");
    ImGui::EndTable(); }
  if (a->type == "StaticMesh") {
    if (ImGui::CollapsingHeader("Static Mesh", ImGuiTreeNodeFlags_DefaultOpen) && beginProps("sm")) { propCols();
      rowLabel("Static Mesh"); if (ImGui::BeginCombo("##mesh", a->mesh.c_str(), ImGuiComboFlags_HeightLarge)) {
        for (int i = 0; i < MESH_COUNT; i++) if (ImGui::Selectable(MESHES[i], a->mesh == MESHES[i])) { if (isModelFile(a->mesh) && a->material.empty()) a->material = "M_Default"; a->mesh = MESHES[i]; a->anim.clear(); ch = true; }
        scanFiles(); if (!projModels.empty()) { ImGui::Separator(); ImGui::TextDisabled("Models"); }
        for (auto& m : projModels) if (ImGui::Selectable(m.c_str(), a->mesh == m)) { if (!isModelFile(a->mesh) && a->material == "M_Default") a->material.clear(); a->mesh = m; a->anim.clear(); ch = true; }
        if (hasFileDialogs()) { ImGui::Separator(); if (ImGui::Selectable("Import a model...")) { std::string f = openFileDialog("Import a model", "glTF models", "*.glb;*.gltf"); if (!f.empty()) { auto r = importFiles({f}); if (!r.empty()) { if (a->material == "M_Default") a->material.clear(); a->mesh = r[0]; a->anim.clear(); ch = true; } } } }
        ImGui::EndCombo(); }
      if (ImGui::BeginDragDropTarget()) { if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("PGE_ADD")) { std::string s((const char*)p->Data, p->DataSize); auto bar = s.find('|');
          if (bar != std::string::npos && s.substr(0, bar) == "StaticMesh") { std::string m = s.substr(bar + 1); if (isModelFile(m) && a->material == "M_Default") a->material.clear(); if (!isModelFile(m) && a->material.empty()) a->material = "M_Default"; a->mesh = m; a->anim.clear(); ch = true; } } ImGui::EndDragDropTarget(); }
      ImGui::EndTable();
      if (isModelFile(a->mesh)) ch |= modelDetails(*a); }
    if (ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen) && beginProps("mt")) { propCols();
      rowLabel("Element 0"); ImGui::SetNextItemWidth(-60);
      bool mdl = isModelFile(a->mesh); const char* shown = a->material.empty() ? (mdl ? "The model's own" : "M_Default") : a->material.c_str();
      if (ImGui::BeginCombo("##mat", shown)) { if (mdl && ImGui::Selectable("The model's own", a->material.empty())) { a->material.clear(); ch = true; }
        for (auto& [id, m] : P.materials) if (ImGui::Selectable(id.c_str(), a->material == id)) { a->material = id; ch = true; } ImGui::EndCombo(); }
      if (mdl && ImGui::IsItemHovered()) ImGui::SetTooltip("A model draws with the materials in its file, or one material of the project on all of it.");
      if (ImGui::BeginDragDropTarget()) { if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("PGE_MAT")) { a->material.assign((const char*)p->Data, p->DataSize); ch = true; } ImGui::EndDragDropTarget(); }
      ImGui::SameLine(); ImGui::BeginDisabled(a->material.empty()); if (ImGui::Button("Edit")) { materialOpen = a->material; wMaterial = true; ImGui::SetWindowFocus("Material Editor"); } ImGui::EndDisabled();
      ImGui::EndTable(); }
    if (ImGui::CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen) && beginProps("ph")) { propCols();
      rowLabel("Simulate Physics"); ch |= ImGui::Checkbox("##sim", &a->physics.simulate);
      rowLabel("Mass (kg)"); ImGui::BeginDisabled(!a->physics.simulate); ch |= ImGui::DragFloat("##mass", &a->physics.mass, 0.1f, 0.01f, 10000.f, "%.2f"); ImGui::EndDisabled();
      rowLabel("Collision"); ch |= ImGui::Checkbox("##col", &a->physics.collision);
      ImGui::EndTable(); }
  }
  if (a->isLight() && ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen) && beginProps("lt")) { propCols();
    rowLabel("Light Color"); ch |= ImGui::ColorEdit3("##lc", &a->light.color.x);
    rowLabel(a->type == "DirectionalLight" ? "Intensity (lux)" : "Intensity (cd)"); ch |= ImGui::DragFloat("##li", &a->light.intensity, a->type == "DirectionalLight" ? 0.02f : 0.5f, 0, 100000, "%.2f");
    if (a->type != "DirectionalLight") { rowLabel("Attenuation Radius"); ch |= ImGui::DragFloat("##lr", &a->light.range, 0.1f, 0.1f, 1000, "%.1f m"); }
    if (a->type == "SpotLight") { rowLabel("Outer Cone Angle"); ch |= ImGui::SliderFloat("##la", &a->light.angle, 1, 89, "%.0f deg"); }
    rowLabel("Cast Shadows"); ch |= ImGui::Checkbox("##ls", &a->light.shadows);
    if (a->type != "DirectionalLight" && a->light.shadows) { rowLabel(""); ImGui::TextDisabled("Shadows come from the Directional Light."); }
    ImGui::EndTable(); }
  if (a->type == "TextRender" && ImGui::CollapsingHeader("Text", ImGuiTreeNodeFlags_DefaultOpen) && beginProps("tx")) { propCols();
    rowLabel("Text"); ch |= ImGui::InputTextMultiline("##tv", &a->text.value, {-1, ImGui::GetTextLineHeight() * 3});
    rowLabel("Text Color"); ch |= ImGui::ColorEdit3("##tc", &a->text.color.x);
    rowLabel("World Size"); ch |= ImGui::DragFloat("##ts", &a->text.size, 0.01f, 0.05f, 50, "%.2f m");
    ImGui::EndTable(); }
  if (ImGui::CollapsingHeader("Script", ImGuiTreeNodeFlags_DefaultOpen) && beginProps("sc")) { propCols();
    rowLabel("Script"); ImGui::SetNextItemWidth(-60);
    if (ImGui::BeginCombo("##scr", a->script.empty() ? "None" : a->script.c_str())) {
      if (ImGui::Selectable("None", a->script.empty())) { a->script.clear(); ch = true; }
      for (auto& [id, s] : P.scripts) if (ImGui::Selectable(id.c_str(), a->script == id)) { a->script = id; ch = true; }
      ImGui::Separator(); if (ImGui::Selectable("+ New Blueprint...")) { std::string id = newBlueprint("BP_" + sanitizeName(a->name)); a->script = id; ch = true; openBlueprint(id); }
      if (ImGui::Selectable("+ New Script...")) { std::string id = newScript("SC_" + sanitizeName(a->name)); a->script = id; ch = true; openScript(id); }
      ImGui::EndCombo(); }
    if (ImGui::BeginDragDropTarget()) { if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("PGE_SCRIPT")) { a->script.assign((const char*)p->Data, p->DataSize); ch = true; } ImGui::EndDragDropTarget(); }
    ImGui::SameLine(); ImGui::BeginDisabled(a->script.empty()); if (ImGui::Button("Edit##s")) openScript(a->script); ImGui::EndDisabled();
    ImGui::EndTable(); }
  if (ImGui::CollapsingHeader("Rendering") && beginProps("rd")) { propCols(); rowLabel("Visible"); ch |= ImGui::Checkbox("##vis", &a->visible); ImGui::EndTable(); }
  if (ImGui::CollapsingHeader("Actor", ImGuiTreeNodeFlags_DefaultOpen) && beginProps("ac")) { propCols();
    rowLabel("Tags"); std::string tags; for (size_t i = 0; i < a->tags.size(); i++) tags += (i ? ", " : "") + a->tags[i];
    if (ImGui::InputTextWithHint("##tags", "comma, separated", &tags, ImGuiInputTextFlags_EnterReturnsTrue)) { a->tags.clear(); std::string cur;
      for (char c : tags + ",") { if (c == ',') { while (!cur.empty() && cur.front() == ' ') cur.erase(0, 1); while (!cur.empty() && cur.back() == ' ') cur.pop_back(); if (!cur.empty()) a->tags.push_back(cur); cur.clear(); } else cur += c; } ch = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Press Enter to apply. Scripts find tagged actors with world.findAll(tag).");
    rowLabel("Attached To"); { std::string pn = actor(a->parent) ? actor(a->parent)->name : "None"; if (ImGui::BeginCombo("##par", pn.c_str())) {
      if (ImGui::Selectable("None", a->parent.empty())) attach(a->id, "");
      for (auto& o : L().actors) if (o.id != a->id && ImGui::Selectable(o.name.c_str(), a->parent == o.id)) { std::string oid = o.id; attach(a->id, oid); break; }
      ImGui::EndCombo(); } }
    ImGui::EndTable(); }
  if (ch) touch();
  ImGui::End();
}

/* ------------------------------------------------------------------ World Settings */
void Editor::drawWorldSettings() {
  if (!ImGui::Begin("World Settings", &wWorld)) { ImGui::End(); return; }
  WorldSettings& w = L().world; bool ch = false;
  if (ImGui::CollapsingHeader("Game Mode", ImGuiTreeNodeFlags_DefaultOpen) && beginProps("gm")) { propCols();
    rowLabel("GameMode Override"); if (ImGui::BeginCombo("##gm", w.gameMode.c_str())) { for (const char* m : {"FirstPerson", "ThirdPerson", "Spectator"}) if (ImGui::Selectable(m, w.gameMode == m)) { w.gameMode = m; ch = true; } ImGui::EndCombo(); }
    rowLabel("Field of View"); ch |= ImGui::SliderFloat("##fov", &w.fov, 40, 120, "%.0f deg");
    ImGui::EndTable(); }
  if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen) && beginProps("li")) { propCols();
    rowLabel("Sky Color"); ch |= ImGui::ColorEdit3("##sky", &w.sky.x);
    rowLabel("Sky Light"); ch |= ImGui::ColorEdit3("##skl", &w.skyLight.x);
    rowLabel("Ground Light"); ch |= ImGui::ColorEdit3("##gl", &w.groundLight.x);
    rowLabel("Ambient Intensity"); ch |= ImGui::SliderFloat("##amb", &w.ambient, 0, 5, "%.2f");
    rowLabel("Exposure"); ch |= ImGui::SliderFloat("##exp", &w.exposure, 0.1f, 4, "%.2f");
    ImGui::EndTable(); }
  if (ImGui::CollapsingHeader("Fog", ImGuiTreeNodeFlags_DefaultOpen) && beginProps("fg")) { propCols();
    rowLabel("Exponential Fog"); ch |= ImGui::Checkbox("##fog", &w.fog);
    ImGui::BeginDisabled(!w.fog); rowLabel("Fog Color"); ch |= ImGui::ColorEdit3("##fc", &w.fogColor.x); rowLabel("Fog Density"); ch |= ImGui::SliderFloat("##fd", &w.fogDensity, 0.001f, 0.2f, "%.3f", ImGuiSliderFlags_Logarithmic); ImGui::EndDisabled();
    ImGui::EndTable(); }
  if (ImGui::CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen) && beginProps("wp")) { propCols();
    rowLabel("Gravity Z"); ch |= ImGui::DragFloat("##grav", &w.gravity, 0.05f, -100, 100, "%.2f m/s2");
    rowLabel("Kill Z"); ch |= ImGui::DragFloat("##kz", &w.killZ, 0.5f, -10000, 1000, "%.1f m");
    ImGui::EndTable(); }
  if (ch) touch();
  ImGui::End();
}

/* ------------------------------------------------------------------ Place Actors */
void Editor::drawPlaceActors() {
  if (!ImGui::Begin("Place Actors", &wPlaceActors)) { ImGui::End(); return; }
  ImGui::SetNextItemWidth(-1); ImGui::InputTextWithHint("##paf", "Search Classes", &paFilter);
  std::string f = paFilter; std::transform(f.begin(), f.end(), f.begin(), ::tolower);
  auto entry = [&](const char* label, const std::string& payload, int icon) {
    std::string l = label; std::transform(l.begin(), l.end(), l.begin(), ::tolower); if (!f.empty() && l.find(f) == std::string::npos) return;
    ImGui::PushID(payload.c_str()); ImVec2 p = ImGui::GetCursorScreenPos(); float h = ImGui::GetFrameHeight() + 8;
    if (ImGui::Selectable("##e", false, 0, {0, h})) { auto bar = payload.find('|'); addActor(payload.substr(0, bar), bar == std::string::npos ? "" : payload.substr(bar + 1)); }
    if (ImGui::BeginDragDropSource()) { ImGui::SetDragDropPayload("PGE_ADD", payload.data(), payload.size()); ImGui::Text("Place %s", label); ImGui::EndDragDropSource(); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to place in front of the camera, or drag into the viewport");
    ImDrawList* dl = ImGui::GetWindowDrawList(); dl->AddRectFilled({p.x + 2, p.y + 2}, {p.x + h - 2, p.y + h - 2}, IM_COL32(15, 15, 15, 255), 3);
    drawIcon(dl, {p.x + h / 2, p.y + h / 2}, h * 0.32f, icon, IM_COL32(190, 190, 190, 255)); dl->AddText({p.x + h + 8, p.y + (h - ImGui::GetFontSize()) / 2}, ImGui::GetColorU32(ImGuiCol_Text), label);
    ImGui::PopID(); };
  if (ImGui::CollapsingHeader("Basic", ImGuiTreeNodeFlags_DefaultOpen)) { entry("Empty Actor", "Empty", IC_Select); entry("Player Start", "PlayerStart", IC_Play); entry("Trigger Box", "TriggerBox", IC_Window); entry("Camera Actor", "CameraActor", IC_Window); entry("Text Render", "TextRender", IC_Script); }
  if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen)) for (const char* t : {"DirectionalLight", "PointLight", "SpotLight"}) entry(actorTypeLabel(t), t, IC_Gear);
  if (ImGui::CollapsingHeader("Shapes", ImGuiTreeNodeFlags_DefaultOpen)) for (int i = 0; i < MESH_COUNT; i++) entry(MESHES[i], std::string("StaticMesh|") + MESHES[i], IC_Cube);
  ImGui::End();
}

/* ------------------------------------------------------------------ Content Browser */
void Editor::drawContent() {
  if (!ImGui::Begin("Content Browser", &wContent)) { ImGui::End(); return; }
  if (ImGui::Button("+ Add")) ImGui::OpenPopup("cbAdd"); ImGui::SameLine();
  if (!console) { if (ImGui::Button("Import")) importDialog(); if (ImGui::IsItemHovered()) ImGui::SetTooltip("Bring a model (.glb, .gltf) or a picture (.png, .jpg, .tga) into the project.\nOr drop the files on this window."); ImGui::SameLine(); }
  if (ImGui::Button("Save All")) save(); ImGui::SameLine();
  ImGui::SetNextItemWidth(220); ImGui::InputTextWithHint("##cbf", "Search Content", &cbFilter);
  ImGui::SameLine(); ImGui::TextDisabled("Content > %s", cbFolder.c_str());
  if (ImGui::BeginPopup("cbAdd")) { if (ImGui::MenuItem("Level")) run("newLevel"); if (ImGui::MenuItem("Material")) run("newMaterial"); if (ImGui::MenuItem("Blueprint")) run("newBlueprint"); if (ImGui::MenuItem("Script")) run("newScript"); ImGui::EndPopup(); }
  ImGui::Separator();
  ImGui::BeginChild("folders", {170, 0}, ImGuiChildFlags_ResizeX);
  if (ImGui::TreeNodeEx("Content", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
    for (const char* f : {"Levels", "Materials", "Scripts", "Models", "Textures", "Shapes"}) { if (ImGui::TreeNodeEx(f, ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth | (cbFolder == f ? ImGuiTreeNodeFlags_Selected : 0))) ImGui::TreePop(); if (ImGui::IsItemClicked()) cbFolder = f; }
    ImGui::TreePop(); }
  ImGui::EndChild(); ImGui::SameLine();
  ImGui::BeginChild("assets", {0, 0});
  if (cbFolder == "Models" || cbFolder == "Textures") scanFiles();
  std::string flt = cbFilter; std::transform(flt.begin(), flt.end(), flt.begin(), ::tolower);
  float tile = 96 * ImGui::GetIO().FontGlobalScale * win.dpi, avail = ImGui::GetContentRegionAvail().x; int cols = std::max(1, (int)(avail / (tile + 10))); int n = 0;
  std::string delMat, delScript, delLevel, dupMat;
  auto tileItem = [&](const std::string& id, const std::string& label, const char* kind, ImU32 color, const char* payloadType, std::function<void()> open, std::function<void()> ctx) {
    std::string l = label; std::transform(l.begin(), l.end(), l.begin(), ::tolower); if (!flt.empty() && l.find(flt) == std::string::npos) return;
    if (n % cols) ImGui::SameLine(); n++;
    ImGui::PushID(id.c_str()); ImGui::BeginGroup(); ImVec2 p = ImGui::GetCursorScreenPos();
    bool selected = (cbFolder == "Levels" && id == level) || (cbFolder == "Materials" && id == materialOpen);
    ImGui::Selectable("##t", selected, ImGuiSelectableFlags_AllowDoubleClick, {tile, tile + 34});
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) open();
    if (payloadType && ImGui::BeginDragDropSource()) { ImGui::SetDragDropPayload(payloadType, id.data(), id.size()); ImGui::Text("%s", label.c_str()); ImGui::EndDragDropSource(); }
    if (ImGui::BeginPopupContextItem("ctx")) { ImGui::TextDisabled("%s", label.c_str()); if (ImGui::MenuItem("Open")) open(); ctx(); ImGui::EndPopup(); }
    ImDrawList* dl = ImGui::GetWindowDrawList(); dl->AddRectFilled({p.x + 4, p.y + 4}, {p.x + tile - 4, p.y + tile - 8}, IM_COL32(18, 18, 18, 255), 4);
    dl->AddRectFilled({p.x + 4, p.y + tile - 11}, {p.x + tile - 4, p.y + tile - 8}, color);
    if (std::strcmp(kind, "Material") == 0) { auto it = P.materials.find(id); if (it != P.materials.end()) { glm::vec3 c = it->second.color; dl->AddCircleFilled({p.x + tile / 2, p.y + tile / 2 - 4}, tile * 0.3f, ImGui::ColorConvertFloat4ToU32({c.r, c.g, c.b, 1}), 32);
        dl->AddCircleFilled({p.x + tile / 2 - tile * 0.09f, p.y + tile / 2 - 4 - tile * 0.09f}, tile * 0.07f, IM_COL32(255, 255, 255, (int)(150 * (1 - it->second.roughness))), 16); } }
    else if (std::strcmp(kind, "Texture") == 0) { const Texture* t = texture(id, true); if (t && t->id) dl->AddImage((ImTextureID)(intptr_t)t->id, {p.x + 10, p.y + 10}, {p.x + tile - 10, p.y + tile - 16}); else dl->AddText({p.x + tile / 2 - 4, p.y + tile / 2 - 10}, IM_COL32(255, 90, 90, 255), "?"); }
    else drawIcon(dl, {p.x + tile / 2, p.y + tile / 2 - 4}, tile * 0.26f, std::strcmp(kind, "Script") == 0 ? IC_Script : std::strcmp(kind, "Level") == 0 ? IC_Window : IC_Cube, IM_COL32(200, 200, 200, 255));
    ImGui::PushClipRect(p, {p.x + tile, p.y + tile + 40}, true);
    dl->AddText({p.x + 6, p.y + tile - 4}, ImGui::GetColorU32(ImGuiCol_Text), label.c_str()); dl->AddText({p.x + 6, p.y + tile + 12}, ImGui::GetColorU32(ImGuiCol_TextDisabled), kind);
    ImGui::PopClipRect(); ImGui::EndGroup(); ImGui::PopID(); };
  if (cbFolder == "Levels") for (auto& [ln, lv] : P.levels) { std::string name = ln;
    tileItem(ln, ln + (ln == P.startLevel ? " (start)" : ""), "Level", IM_COL32(230, 140, 40, 255), nullptr, [&, name] { openLevel(name); },
      [&, name] { if (ImGui::MenuItem("Set as Start Level")) { P.startLevel = name; touch(); } if (ImGui::MenuItem("Delete", nullptr, false, P.levels.size() > 1)) delLevel = name; }); }
  if (cbFolder == "Materials") for (auto& [id, m] : P.materials) { std::string mid = id;
    tileItem(id, id, "Material", IM_COL32(60, 180, 90, 255), "PGE_MAT", [&, mid] { materialOpen = mid; wMaterial = true; ImGui::SetWindowFocus("Material Editor"); },
      [&, mid] { if (ImGui::MenuItem("Duplicate")) dupMat = mid; if (ImGui::MenuItem("Delete", nullptr, false, mid != "M_Default")) delMat = mid; }); }
  if (cbFolder == "Scripts") for (auto& [id, s] : P.scripts) { std::string sid = id;
    tileItem(id, s.name, s.blueprint() ? "Blueprint" : "Script", s.blueprint() ? IM_COL32(60, 120, 255, 255) : IM_COL32(60, 130, 230, 255), "PGE_SCRIPT", [&, sid] { openScript(sid); }, [&, sid] { if (ImGui::MenuItem("Delete")) delScript = sid; }); }
  if (cbFolder == "Shapes") for (int i = 0; i < MESH_COUNT; i++) { std::string m = MESHES[i];
    tileItem("StaticMesh|" + m, m, "Static Mesh", IM_COL32(40, 180, 220, 255), "PGE_ADD", [&, m] { addActor("StaticMesh", m); }, [] {}); }
  if (cbFolder == "Models") for (auto& m : projModels) { std::string mm = m;
    tileItem("StaticMesh|" + m, fs::u8path(m).stem().u8string(), "Model", IM_COL32(230, 90, 170, 255), "PGE_ADD", [&, mm] { addActor("StaticMesh", mm); },
      [&, mm] { ImGui::TextDisabled("%s", mm.c_str()); if (ImGui::MenuItem("Place in Level")) addActor("StaticMesh", mm); if (!console && ImGui::MenuItem("Show in Folder")) openInExplorer((fs::u8path(P.path).parent_path() / fs::u8path(mm)).parent_path().u8string()); }); }
  if (cbFolder == "Textures") for (auto& t : projTextures) { std::string tt = t;
    tileItem(t, fs::u8path(t).filename().u8string(), "Texture", IM_COL32(200, 60, 60, 255), "PGE_TEX", [&, tt] { if (P.materials.count(materialOpen)) { P.materials[materialOpen].baseMap = tt; touch(); wMaterial = true; } else notify("Drag the picture into a slot of the Material Editor"); },
      [&, tt] { ImGui::TextDisabled("%s", tt.c_str()); if (!console && ImGui::MenuItem("Show in Folder")) openInExplorer((fs::u8path(P.path).parent_path() / fs::u8path(tt)).parent_path().u8string()); }); }
  if (!n) ImGui::TextDisabled(cbFolder == "Models" || cbFolder == "Textures" ? (console ? "Nothing here yet: put files in the project's Models / Textures folders." : "Nothing here yet: Import, or drop .glb / .gltf models and .png / .jpg pictures on the window.") : flt.empty() ? "This folder is empty. Use + Add." : "Nothing matches the search.");
  ImGui::EndChild();
  auto inUse = [&](auto pred) { int k = 0; for (auto& [ln, lv] : P.levels) for (auto& a : lv.actors) k += pred(a); return k; };
  if (!dupMat.empty()) { std::string id = newMaterial(dupMat + "_Copy"); Material m = P.materials[dupMat]; m.name = id; P.materials[id] = m; }
  if (!delMat.empty()) { int k = inUse([&](const Actor& a) { return a.type == "StaticMesh" && a.material == delMat; });
    for (auto& [ln, lv] : P.levels) for (auto& a : lv.actors) if (a.material == delMat) a.material = "M_Default";
    P.materials.erase(delMat); if (materialOpen == delMat) { materialOpen.clear(); wMaterial = false; } touch(); logMsg("Deleted " + delMat + (k ? " (" + std::to_string(k) + " actor(s) now use M_Default)" : "")); }
  if (!delScript.empty()) { int k = inUse([&](const Actor& a) { return a.script == delScript; });
    for (auto& [ln, lv] : P.levels) for (auto& a : lv.actors) if (a.script == delScript) a.script.clear();
    P.scripts.erase(delScript); bpTabs.erase(std::remove_if(bpTabs.begin(), bpTabs.end(), [&](auto& t) { return t->id == delScript; }), bpTabs.end()); tabs.erase(std::remove_if(tabs.begin(), tabs.end(), [&](const ScriptTab& t) { return t.id == delScript; }), tabs.end()); touch();
    logMsg("Deleted " + delScript + (k ? " (removed from " + std::to_string(k) + " actor(s))" : "")); }
  if (!delLevel.empty()) { auto& v = P.levels; v.erase(std::remove_if(v.begin(), v.end(), [&](auto& x) { return x.first == delLevel; }), v.end());
    if (P.startLevel == delLevel) P.startLevel = v[0].first; if (level == delLevel) openLevel(P.startLevel); touch(); logMsg("Deleted level " + delLevel); }
  ImGui::End();
}

/* ------------------------------------------------------------------ Output Log */
void Editor::drawLog() {
  if (focusLog) { ImGui::SetNextWindowFocus(); focusLog = false; }
  if (!ImGui::Begin("Output Log", &wLog)) { ImGui::End(); return; }
  ImGui::SetNextItemWidth(260); ImGui::InputTextWithHint("##lf", "Search Log", &logFilter); ImGui::SameLine(); if (ImGui::Button("Clear")) logClear();
  ImGui::Separator(); ImGui::BeginChild("lines", {0, 0}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
  std::string f = logFilter; std::transform(f.begin(), f.end(), f.begin(), ::tolower);
  for (auto& l : logLines()) {
    if (!f.empty()) { std::string t = l.text; std::transform(t.begin(), t.end(), t.begin(), ::tolower); if (t.find(f) == std::string::npos) continue; }
    ImVec4 c = l.kind == LogKind::Error ? ImVec4(1, 0.42f, 0.42f, 1) : l.kind == LogKind::Warn ? ImVec4(1, 0.8f, 0.35f, 1) : l.kind == LogKind::Ok ? ImVec4(0.5f, 0.85f, 0.5f, 1) : ImVec4(0.8f, 0.8f, 0.8f, 1);
    ImGui::TextDisabled("[%s]", l.time.c_str()); ImGui::SameLine(); ImGui::TextColored(c, "%s", l.text.c_str());
  }
  if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20) ImGui::SetScrollHereY(1.0f);
  ImGui::EndChild(); ImGui::End();
}

/* ------------------------------------------------------------------ Script Editor */
void Editor::drawScripts() {
  if (!ImGui::Begin("Script Editor", &wScripts)) { ImGui::End(); return; }
  if (tabs.empty()) { ImGui::TextDisabled("No script open. Double-click a script in the Content Browser, or use Tools > New Script."); ImGui::End(); return; }
  if (ImGui::Button("Compile (F7)")) compileScripts(false); ImGui::SameLine(); if (ImGui::Button("Save")) save(); ImGui::SameLine();
  if (ImGui::Button("API Reference")) wApi = true; ImGui::SameLine(); ImGui::TextDisabled("JavaScript - Ctrl+/ comments - hover a name for help");
  int closeIdx = -1;
  if (ImGui::BeginTabBar("scripttabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs)) {
    for (size_t i = 0; i < tabs.size(); i++) { ScriptTab& t = tabs[i]; bool open = true; bool changed = t.ed->GetText() != t.savedText;
      ImGuiTabItemFlags fl = (changed ? ImGuiTabItemFlags_UnsavedDocument : 0) | (t.focus ? ImGuiTabItemFlags_SetSelected : 0); t.focus = false;
      std::string label = t.id + ".js###" + t.id;
      if (ImGui::BeginTabItem(label.c_str(), &open, fl)) {
        if (!t.error.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 110, 110, 255)); ImGui::TextWrapped("%s", t.error.c_str()); ImGui::PopStyleColor();
          if (t.errorLine > 0) { ImGui::SameLine(); if (ImGui::SmallButton("Go to line")) { t.ed->SetCursorPosition(t.errorLine - 1, 0); t.ed->SelectLine(t.errorLine - 1); } } }
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts.Size > 2 ? ImGui::GetIO().Fonts->Fonts[2] : ImGui::GetFont());
        t.ed->Render(("##code" + t.id).c_str(), ImGui::IsWindowFocused(), ImVec2(0, 0), false);
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) codeFocusedNext_ = true;
        ImGui::PopFont();
        ImGui::EndTabItem();
      }
      if (!open) closeIdx = (int)i;
    }
    ImGui::EndTabBar();
  }
  if (closeIdx >= 0) { ScriptTab& t = tabs[closeIdx]; auto it = P.scripts.find(t.id); std::string txt = t.ed->GetText();
    if (it != P.scripts.end() && it->second.code != txt) { it->second.code = txt; touch(); }   // keep the edits (Ctrl+Z undoes)
    tabs.erase(tabs.begin() + closeIdx); }
  ImGui::End();
}

/* ------------------------------------------------------------------ Material Editor */
void Editor::drawMaterial() {
  if (!ImGui::Begin("Material Editor", &wMaterial)) { ImGui::End(); return; }
  auto it = P.materials.find(materialOpen);
  if (it == P.materials.end()) { ImGui::TextDisabled("Open a material from the Content Browser or the Details panel."); ImGui::End(); return; }
  Material& m = it->second; bool ch = false;
  // preview: a sphere lit like the level, rendered into its own target
  static RenderTarget prt; float ps = std::min(ImGui::GetContentRegionAvail().x * 0.45f, 320.f); int px = (int)(ps * ImGui::GetIO().DisplayFramebufferScale.x);
  R.resizeTarget(prt, px, px); Frame F; F.world = L().world; F.world.sky = glm::vec3(0.1f);
  { DrawItem d; d.mesh = &primitive(m.worldAligned ? "Cube" : "Sphere"); d.model = glm::scale(glm::mat4(1), glm::vec3(m.worldAligned ? 1.1f : 1.6f)); if (m.worldAligned) d.model = glm::rotate(d.model, 0.6f, glm::vec3(0.3f, 1, 0)); applyMaterial(d, m); F.draws.push_back(d); }
  { LightItem l; l.type = 0; l.dir = glm::normalize(glm::vec3(-0.5f, -0.7f, -0.6f)); l.color = glm::vec3(1); l.intensity = 2.5f; F.lights.push_back(l); }
  Camera C; C.pos = {0, 0, 3}; C.view = glm::lookAt(C.pos, glm::vec3(0), glm::vec3(0, 1, 0)); C.proj = glm::perspective(glm::radians(40.f), 1.f, 0.1f, 50.f);
  R.render(F, C, px, px, &prt);
  ImGui::Image((ImTextureID)(intptr_t)prt.color, {ps, ps}, {0, 1}, {1, 0}); ImGui::SameLine();
  ImGui::BeginGroup(); ImGui::Text("%s", materialOpen.c_str()); ImGui::TextDisabled("Used by %d actor(s) in this level", (int)std::count_if(L().actors.begin(), L().actors.end(), [&](const Actor& a) { return a.type == "StaticMesh" && a.material == materialOpen; }));
  ImGui::Spacing();
  if (beginProps("mat")) { propCols();
    rowLabel("Base Color"); ch |= ImGui::ColorEdit3("##bc", &m.color.x);
    rowLabel("Metallic"); ch |= ImGui::SliderFloat("##me", &m.metalness, 0, 1);
    rowLabel("Roughness"); ch |= ImGui::SliderFloat("##ro", &m.roughness, 0, 1);
    rowLabel("Emissive Color"); ch |= ImGui::ColorEdit3("##em", &m.emissive.x);
    rowLabel("Emissive Strength"); ch |= ImGui::DragFloat("##ei", &m.emissiveIntensity, 0.05f, 0, 100);
    rowLabel("Opacity"); ch |= ImGui::SliderFloat("##op", &m.opacity, 0, 1);
    ImGui::EndTable(); }
  ImGui::EndGroup();
  // pictures: each one multiplies its value above (roughness reads green, metal blue, occlusion red: a packed ORM picture fits all three)
  ImGui::SeparatorText("Textures"); ImGui::BeginGroup();
  if (beginProps("maps")) { propCols(); auto slot = [&](const char* l, std::string& f, bool srgb, const char* tip) { rowLabel(l); if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip); bool c = textureSlot(l, f, srgb); ch |= c; return c; };
    if (slot("Base Color", m.baseMap, true, "The surface's colours (and see-through parts in its alpha). Multiplies Base Color.") && !m.baseMap.empty() && glm::length(m.color - Material().color) < 1e-3f) m.color = glm::vec3(1);
    slot("Normal", m.normalMap, false, "Bumps and grooves (a blue-purple picture).");
    if (slot("Roughness", m.roughnessMap, false, "Rough (white) or shiny (black): the green channel. Multiplies Roughness.") && !m.roughnessMap.empty()) m.roughness = 1;
    if (slot("Metallic", m.metalMap, false, "Metal (white) or not (black): the blue channel. Multiplies Metallic.") && !m.metalMap.empty()) m.metalness = 1;
    slot("Ambient Occlusion", m.aoMap, false, "Dark creases: the red channel.");
    if (slot("Emissive", m.emissiveMap, true, "Glowing parts. Multiplies Emissive Color.") && !m.emissiveMap.empty() && glm::length(m.emissive) < 1e-4f) m.emissive = glm::vec3(1);
    if (m.hasMaps()) {
      rowLabel("Tiling"); ch |= ImGui::DragFloat2("##til", &m.tiling.x, 0.01f, 0.001f, 1000, "%.2f");
      rowLabel("World Aligned"); ch |= ImGui::Checkbox("##wa", &m.worldAligned); if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lay the pictures on the world (Tiling per metre): they don't stretch when an actor is scaled. For walls, floors, level blocks.");
      if (!m.normalMap.empty()) { rowLabel("Normal Strength"); ch |= ImGui::SliderFloat("##ns", &m.normalStrength, 0, 4); rowLabel("DirectX Normal"); ch |= ImGui::Checkbox("##dx", &m.directXNormal); if (ImGui::IsItemHovered()) ImGui::SetTooltip("Normal maps made for DirectX / Unreal have green flipped: tick this if the bumps look upside down."); }
      if (!m.aoMap.empty()) { rowLabel("AO Strength"); ch |= ImGui::SliderFloat("##aos", &m.aoStrength, 0, 1); }
      if (!m.baseMap.empty()) { rowLabel("Masked"); ch |= ImGui::Checkbox("##msk", &m.masked); if (ImGui::IsItemHovered()) ImGui::SetTooltip("Cut out where the picture's alpha is low: leaves, fences, grass. Shadows are cut too.");
        if (m.masked) { rowLabel("Opacity Clip"); ch |= ImGui::SliderFloat("##cut", &m.cutoff, 0.01f, 0.99f); } } }
    ImGui::EndTable(); }
  ImGui::Spacing(); ImGui::TextDisabled("Drag pictures from the Content Browser (Textures) into the slots. Drag the material onto an actor in the viewport to apply it.");
  ImGui::EndGroup();
  if (ch) touch();
  ImGui::End();
}
}
