// Premium Engine's console version: the editor opened from Premium Games console mode.
// Full screen, its own projects (in the console's storage), and everything works from the controller:
//   the menus and panels with the D-pad / left stick (A press, B back, X menus, Y type), like Dear ImGui's gamepad navigation;
//   LT held: fly the camera (left stick move, right stick look, LB / RB down / up);
//   RT held: move the selected actor (left stick along the floor, LB / RB down / up, right stick turns it; snaps when let go);
//   View: play / stop · Menu: save · text fields open the on-screen keyboard. Home (Premium Games) opens the Control Center.
//   R3 (right stick click): a mouse pointer for things made for a mouse (the Blueprint graph): left stick moves it,
//   A clicks (hold to drag), X right-clicks, right stick zooms / scrolls, B or R3 goes back to the menus.
#include "editor.h"
#include "platform.h"
#include "log.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <glm/glm.hpp>
#include <cmath>
#include <cstring>
#include <string>

namespace pge {

// the on-screen keyboard: rows of keys, then the action row
static const char* OSK_ROWS[2][4] = {
  {"1234567890-=", "qwertyuiop[]", "asdfghjkl;'", "zxcvbnm,./_"},
  {"!@#$%^&*()_+", "QWERTYUIOP{}", "ASDFGHJKL:\"", "ZXCVBNM<>?-"}};
enum { OSK_SHIFT, OSK_SPACE, OSK_BACK, OSK_DONE, OSK_ACTIONS };
static const char* OSK_ACT[OSK_ACTIONS] = {"Shift (LB)", "Space (Y)", "Delete (X)", "Done (Menu)"};
static int oskRowLen(int r, bool shift) { return r < 4 ? (int)std::strlen(OSK_ROWS[shift][r]) : OSK_ACTIONS; }

static float axisOf(SDL_GameController* c, SDL_GameControllerAxis a, float dead = 0.18f) {
  if (!c) return 0; float v = SDL_GameControllerGetAxis(c, a) / 32767.f; v = glm::clamp(v, -1.f, 1.f);
  float m = std::fabs(v); if (m < dead) return 0; return (v > 0 ? 1 : -1) * (m - dead) / (1 - dead);
}

void Editor::sendKey(int key) { ImGui::GetIO().AddKeyEvent((ImGuiKey)key, true); keyUps_.push_back(key); }   // down now, up next frame

void Editor::oskMove(int dir) {   // 0 up, 1 down, 2 left, 3 right
  int rows = 5;
  if (dir == 0) oskR_ = (oskR_ + rows - 1) % rows; else if (dir == 1) oskR_ = (oskR_ + 1) % rows;
  else { int n = oskRowLen(oskR_, oskShift_); oskC_ = (oskC_ + (dir == 2 ? n - 1 : 1)) % n; }
  oskC_ = std::min(oskC_, oskRowLen(oskR_, oskShift_) - 1);
}

// the key under the cursor
void Editor::oskPress() {
  ImGuiIO& io = ImGui::GetIO();
  if (oskR_ < 4) { char c[2] = {OSK_ROWS[oskShift_][oskR_][oskC_], 0}; io.AddInputCharactersUTF8(c); if (oskShift_ && std::isalpha((unsigned char)c[0])) oskShift_ = false; return; }
  switch (oskC_) {
    case OSK_SHIFT: oskShift_ = !oskShift_; break;
    case OSK_SPACE: io.AddInputCharacter(' '); break;
    case OSK_BACK: sendKey(ImGuiKey_Backspace); break;
    case OSK_DONE: { ImGuiID id = ImGui::GetActiveID(); if (id && ImGui::GetInputTextState(id)) sendKey(ImGuiKey_Enter); else oskShut_ = true; break; }
  }
}

bool Editor::consoleEvent(const SDL_Event& e) {
  if (e.type == SDL_CONTROLLERDEVICEADDED && !pad_) pad_ = SDL_GameControllerOpen(e.cdevice.which);
  if (e.type == SDL_CONTROLLERDEVICEREMOVED && pad_ && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad_)) == e.cdevice.which) { SDL_GameControllerClose(pad_); pad_ = nullptr; }
  if (e.type != SDL_CONTROLLERBUTTONDOWN && e.type != SDL_CONTROLLERBUTTONUP) return false;
  bool down = e.type == SDL_CONTROLLERBUTTONDOWN; int b = e.cbutton.button;
  if (playing && game) {   // playing: the controller is the game's; View stops
    if (down && b == SDL_CONTROLLER_BUTTON_BACK) { stopPlay(); return true; }
    game->event(e); return true;
  }
  if (oskOn_) {   // the keyboard has the controller
    if (!down) { if (b >= SDL_CONTROLLER_BUTTON_DPAD_UP && b <= SDL_CONTROLLER_BUTTON_DPAD_RIGHT) oskHold_ = -1; return true; }
    switch (b) {
      case SDL_CONTROLLER_BUTTON_DPAD_UP: oskMove(0); oskHold_ = 0; oskHoldT_ = 0.4f; break;
      case SDL_CONTROLLER_BUTTON_DPAD_DOWN: oskMove(1); oskHold_ = 1; oskHoldT_ = 0.4f; break;
      case SDL_CONTROLLER_BUTTON_DPAD_LEFT: oskMove(2); oskHold_ = 2; oskHoldT_ = 0.4f; break;
      case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: oskMove(3); oskHold_ = 3; oskHoldT_ = 0.4f; break;
      case SDL_CONTROLLER_BUTTON_A: oskPress(); break;
      case SDL_CONTROLLER_BUTTON_X: sendKey(ImGuiKey_Backspace); break;
      case SDL_CONTROLLER_BUTTON_Y: ImGui::GetIO().AddInputCharacter(' '); break;
      case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: oskShift_ = !oskShift_; oskC_ = std::min(oskC_, oskRowLen(oskR_, oskShift_) - 1); break;
      case SDL_CONTROLLER_BUTTON_B: case SDL_CONTROLLER_BUTTON_START: { int keep = oskC_, keepR = oskR_; oskR_ = 4; oskC_ = OSK_DONE; oskPress(); oskR_ = keepR; oskC_ = keep; break; }
    }
    return true;
  }
  // the pointer: A / X are the mouse buttons (held = dragging a node or a wire)
  if (b == SDL_CONTROLLER_BUTTON_RIGHTSTICK && down) { padMouse_ = !padMouse_; if (padMouse_) { int mx, my; SDL_GetMouseState(&mx, &my); pmX_ = (float)mx; pmY_ = (float)my;
      if (pmX_ <= 0 && pmY_ <= 0) { int w, h; SDL_GetWindowSize(win.win, &w, &h); pmX_ = w * 0.5f; pmY_ = h * 0.5f; } } return true; }
  if (padMouse_) {
    ImGuiIO& io = ImGui::GetIO();
    if (b == SDL_CONTROLLER_BUTTON_A) { io.AddMouseButtonEvent(0, down); return true; }
    if (b == SDL_CONTROLLER_BUTTON_X) { io.AddMouseButtonEvent(1, down); return true; }
    if (b == SDL_CONTROLLER_BUTTON_B && down) { padMouse_ = false; return true; }
    if (b == SDL_CONTROLLER_BUTTON_Y && down) { io.AddKeyEvent(ImGuiMod_Alt, true); io.AddMouseButtonEvent(0, true); io.AddMouseButtonEvent(0, false); keyUps_.push_back(ImGuiMod_Alt); return true; }   // Y = Alt+click (break a pin's wires)
  }
  if (down && b == SDL_CONTROLLER_BUTTON_BACK && hasProject) { play(); return true; }
  if (down && b == SDL_CONTROLLER_BUTTON_START && hasProject) { if (save()) notify("Saved"); return true; }
  return false;   // the rest drives the menus (Dear ImGui reads the controller itself)
}

void Editor::consoleFrame() {
  ImGuiIO& io = ImGui::GetIO();
  for (int k : keyUps_) io.AddKeyEvent((ImGuiKey)k, false); keyUps_.clear();
  if (!pad_) for (int i = 0; i < SDL_NumJoysticks() && !pad_; i++) if (SDL_IsGameController(i)) pad_ = SDL_GameControllerOpen(i);
  // the keyboard opens with a text field (its own B / Done closes it; Y on the field opens it again)
  if (!io.WantTextInput) oskShut_ = false;
  if (oskShut_ && pad_ && SDL_GameControllerGetButton(pad_, SDL_CONTROLLER_BUTTON_Y)) oskShut_ = false;
  bool wantOsk = io.WantTextInput && !playing && !oskShut_;
  if (wantOsk && !oskOn_) { oskR_ = 1; oskC_ = 0; oskShift_ = false; sendKey(ImGuiKey_End); }   // add to the text (like a console keyboard), not over it
  oskOn_ = wantOsk;
  if (oskOn_ && oskHold_ >= 0) { oskHoldT_ -= dt_; if (oskHoldT_ <= 0) { oskMove(oskHold_); oskHoldT_ = 0.09f; } }
  if (oskOn_) { static float stT = 0; static int stD = -1; float x = axisOf(pad_, SDL_CONTROLLER_AXIS_LEFTX, 0.5f), y = axisOf(pad_, SDL_CONTROLLER_AXIS_LEFTY, 0.5f);
    int d = std::fabs(x) > std::fabs(y) ? (x < 0 ? 2 : x > 0 ? 3 : -1) : (y < 0 ? 0 : y > 0 ? 1 : -1);
    if (d != stD) { stD = d; stT = 0.4f; if (d >= 0) oskMove(d); } else if (d >= 0 && (stT -= dt_) <= 0) { oskMove(d); stT = 0.09f; } }

  // LT: fly the camera · RT: move the selection
  float lt = axisOf(pad_, SDL_CONTROLLER_AXIS_TRIGGERLEFT, 0.1f), rt = axisOf(pad_, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 0.1f);
  bool free = hasProject && !playing && !oskOn_ && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
  padFly_ = free && lt > 0.35f && view == View::Perspective;
  bool wasMoving = padMove_; padMove_ = free && !padFly_ && rt > 0.35f && actor(sel);
  float lx = axisOf(pad_, SDL_CONTROLLER_AXIS_LEFTX), ly = axisOf(pad_, SDL_CONTROLLER_AXIS_LEFTY), rx = axisOf(pad_, SDL_CONTROLLER_AXIS_RIGHTX), ry = axisOf(pad_, SDL_CONTROLLER_AXIS_RIGHTY);
  float ud = pad_ ? (float)SDL_GameControllerGetButton(pad_, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) - (float)SDL_GameControllerGetButton(pad_, SDL_CONTROLLER_BUTTON_LEFTSHOULDER) : 0;
  glm::vec3 f(-std::sin(camYaw) * std::cos(camPitch), std::sin(camPitch), -std::cos(camYaw) * std::cos(camPitch)), r(std::cos(camYaw), 0, -std::sin(camYaw));
  static const float SPEEDS[8] = {0.6f, 1.5f, 3, 6, 10, 16, 26, 45};
  if (padFly_) {
    float sp = SPEEDS[glm::clamp((int)prefs.camSpeed, 1, 8) - 1], iy = prefs.invertY ? -1.f : 1.f;
    camYaw -= rx * 2.4f * dt_; camPitch = glm::clamp(camPitch - ry * 1.8f * dt_ * iy, -1.55f, 1.55f);
    camPos += (f * -ly + r * lx + glm::vec3(0, ud, 0)) * sp * dt_ * (pad_ && SDL_GameControllerGetButton(pad_, SDL_CONTROLLER_BUTTON_LEFTSTICK) ? 3.f : 1.f);
  }
  if (padMove_) if (Actor* a = actor(sel)) {
    glm::vec3 fw = glm::normalize(glm::vec3(f.x, 0, f.z) + glm::vec3(1e-5f, 0, 0)); float d = glm::length(camPos - a->p), sp = glm::clamp(d * 0.6f, 1.5f, 30.f);
    glm::vec3 mv = (fw * -ly + r * lx + glm::vec3(0, ud, 0)) * sp * dt_;
    if (glm::dot(mv, mv) > 0 || rx != 0) { a->p += mv; a->r.y -= rx * 120 * dt_; touch(); }
  }
  if (wasMoving && !padMove_) if (Actor* a = actor(sel)) {   // let go: on the grid, one undo step
    if (snapT && snapTv > 0) a->p = glm::round(a->p / snapTv) * snapTv;
    if (snapR && snapRv > 0) a->r.y = std::round(a->r.y / snapRv) * snapRv;
    touch();
  }
  if (padMouse_ && !playing && !oskOn_) {   // the pointer moves with the left stick (faster the further it's pushed); the right stick scrolls / zooms
    int w, h; SDL_GetWindowSize(win.win, &w, &h); float sp = 1300 * dt_;
    pmX_ = glm::clamp(pmX_ + lx * std::fabs(lx) * sp, 0.f, (float)w - 1); pmY_ = glm::clamp(pmY_ + ly * std::fabs(ly) * sp, 0.f, (float)h - 1);
    if (lx != 0 || ly != 0) { SDL_WarpMouseInWindow(win.win, (int)pmX_, (int)pmY_); io.AddMousePosEvent(pmX_, pmY_); }
    if (ry != 0) io.AddMouseWheelEvent(0, -ry * dt_ * 6);
  } else if (playing || oskOn_) padMouse_ = false;
  // the menus get the controller only when nothing else has it
  bool nav = !playing && !oskOn_ && !padFly_ && !padMove_ && !padMouse_;
  if (nav != padNav_) {
    padNav_ = nav;
    if (nav) io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    else { io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad; for (int k = ImGuiKey_GamepadStart; k <= ImGuiKey_GamepadRStickDown; k++) io.AddKeyEvent((ImGuiKey)k, false); }
  }
}

void Editor::drawConsole() {
  ImGuiViewport* v = ImGui::GetMainViewport(); ImDrawList* dl = ImGui::GetForegroundDrawList(); ImFont* font = ImGui::GetFont();
  float fs = ImGui::GetFontSize(), s = fs / 15.f;
  auto pill = [&](ImVec2 p, const char* btn, const char* what) {   // [A] select
    ImVec2 bs = font->CalcTextSizeA(fs, 1e9f, 0, btn), ws = font->CalcTextSizeA(fs, 1e9f, 0, what);
    dl->AddRectFilled(p, {p.x + bs.x + 14 * s, p.y + fs + 8 * s}, IM_COL32(235, 235, 235, 255), 5 * s);
    dl->AddText(font, fs, {p.x + 7 * s, p.y + 4 * s}, IM_COL32(15, 15, 15, 255), btn);
    dl->AddText(font, fs, {p.x + bs.x + 22 * s, p.y + 4 * s}, IM_COL32(230, 230, 230, 255), what);
    return p.x + bs.x + ws.x + 44 * s; };
  if (oskOn_) {   // the keyboard, at the bottom
    float key = 54 * s, gap = 6 * s, w = 12 * (key + gap) + gap, h = 5 * (key + gap) + gap + 40 * s;
    ImVec2 o(v->Pos.x + (v->Size.x - w) / 2, v->Pos.y + v->Size.y - h - 30 * s);
    dl->AddRectFilled({o.x - 10 * s, o.y - 10 * s}, {o.x + w + 10 * s, o.y + h}, IM_COL32(18, 20, 26, 245), 12 * s);
    dl->AddRect({o.x - 10 * s, o.y - 10 * s}, {o.x + w + 10 * s, o.y + h}, IM_COL32(0, 112, 224, 255), 12 * s, 0, 2 * s);
    for (int r = 0; r < 5; r++) {
      int n = oskRowLen(r, oskShift_); float kw = r < 4 ? key : (w - gap * (OSK_ACTIONS + 1)) / OSK_ACTIONS, x0 = o.x + gap + (r < 4 ? (12 - n) * (key + gap) / 2 : 0);
      for (int c = 0; c < n; c++) {
        ImVec2 a(x0 + c * (kw + gap), o.y + gap + r * (key + gap)), b(a.x + kw, a.y + key); bool on = r == oskR_ && c == oskC_;
        bool lit = r == 4 && c == OSK_SHIFT && oskShift_;
        dl->AddRectFilled(a, b, on ? IM_COL32(0, 112, 224, 255) : lit ? IM_COL32(60, 90, 140, 255) : IM_COL32(44, 48, 58, 255), 6 * s);
        if (on) dl->AddRect({a.x - 2 * s, a.y - 2 * s}, {b.x + 2 * s, b.y + 2 * s}, IM_COL32_WHITE, 7 * s, 0, 2.5f * s);
        char t[2] = {r < 4 ? (char)OSK_ROWS[oskShift_][r][c] : (char)0, 0}; const char* label = r < 4 ? t : OSK_ACT[c];
        float ls = r < 4 ? fs * 1.5f : fs; ImVec2 ts = font->CalcTextSizeA(ls, 1e9f, 0, label);
        dl->AddText(font, ls, {(a.x + b.x - ts.x) / 2, (a.y + b.y - ts.y) / 2}, IM_COL32_WHITE, label);
      }
    }
    float x = o.x; float y = o.y + h - 34 * s;
    x = pill({x, y}, "A", "Type"); x = pill({x, y}, "X", "Delete"); x = pill({x, y}, "Y", "Space"); x = pill({x, y}, "LB", "Shift"); pill({x, y}, "B", "Close");
    return;
  }
  // what the buttons do, above the status bar
  float y = v->Pos.y + v->Size.y - fs - 8 * s - (hasProject ? ImGui::GetFrameHeight() + 12 * s : 10 * s), x = v->Pos.x + 14 * s;
  float bw = v->Size.x - 28 * s; dl->AddRectFilled({x - 6 * s, y - 4 * s}, {x + bw, y + fs + 12 * s}, IM_COL32(10, 10, 12, 200), 6 * s);
  if (playing) { x = pill({x, y}, "View", "Stop playing"); pill({x, y}, "Home", "Premium Games"); return; }
  if (padMouse_) {   // the pointer, and what its buttons do
    ImVec2 m = ImGui::GetIO().MousePos; ImVec2 tri[3] = {m, {m.x, m.y + 22 * s}, {m.x + 15 * s, m.y + 15 * s}};
    dl->AddTriangleFilled(tri[0], tri[1], tri[2], IM_COL32_WHITE); dl->AddTriangle(tri[0], tri[1], tri[2], IM_COL32(0, 0, 0, 255), 1.5f * s);
    x = pill({x, y}, "L", "Move pointer"); x = pill({x, y}, "A", "Click / drag"); x = pill({x, y}, "X", "Right-click"); x = pill({x, y}, "Y", "Alt+click"); x = pill({x, y}, "R", "Zoom / scroll");
    pill({x, y}, "B", "Back to menus"); return; }
  x = pill({x, y}, "A", "Select"); x = pill({x, y}, "B", "Back"); x = pill({x, y}, "X", "Menus (hold: windows)"); x = pill({x, y}, "Y", "Type");
  x = pill({x, y}, "R3", "Pointer");
  if (hasProject) { x = pill({x, y}, "LT", "Fly camera"); x = pill({x, y}, "RT", "Move selected"); x = pill({x, y}, "View", "Play"); x = pill({x, y}, "Menu", "Save"); }
  pill({x, y}, "Home", "Premium Games");
}
}
