// Premium Engine editor entry point.
//   PremiumEngine [project.pge.json]
//   PremiumEngine --new <Blank|FirstPerson|ThirdPerson> <Name>
//   PremiumEngine --selftest <folder>   (CI: makes a project, plays it, packages it, saves a screenshot, exits 0 when all worked)
#include "editor.h"
#include "log.h"
#include "gl.h"
#include "platform.h"
#include "project.h"
#include <imgui.h>
#include <SDL.h>
#include <cstring>
#include <cmath>
#include <glm/glm.hpp>
#include <fstream>
#include <filesystem>

int main(int argc, char** argv) {
  std::string selftest, ctest; bool console = false;
  for (int i = 1; i + 1 < argc; i++) { if (!std::strcmp(argv[i], "--selftest")) selftest = argv[i + 1]; if (!std::strcmp(argv[i], "--consoletest")) ctest = argv[i + 1]; }
  if (!ctest.empty()) selftest.clear();
  // the console version (from Premium Games console mode): --console --home <the console's engine folder>
  for (int i = 1; i < argc; i++) { if (!std::strcmp(argv[i], "--console")) console = true; if (!std::strcmp(argv[i], "--home") && i + 1 < argc) pge::setDocumentsDir(argv[i + 1]); }
  pge::setConsoleSystem(console);
  pge::AppWindow W; pge::g_quietErrors = !selftest.empty();
  if (!pge::openWindow(W, "Premium Engine", 1600, 900, selftest.empty() && !console, true, console)) return 1;   // the console version fills the screen
  if (console) ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
  int code = 0;
  {
    pge::Editor ed(W); ed.console = console; ed.consolePreview = console; ed.init();
    std::string newT, newN; for (int i = 1; i + 2 < argc; i++) if (!std::strcmp(argv[i], "--new")) { newT = argv[i + 1]; newN = argv[i + 2]; }   // from Premium Games: new project
    if (!newT.empty()) ed.queue("newProject:" + newT + ":" + newN);
    else for (int i = 1; i < argc && selftest.empty(); i++) { const char* prev = i > 1 ? argv[i - 1] : ""; if (argv[i][0] != '-' && std::strcmp(prev, "--home") && std::strcmp(prev, "--consoletest")) { ed.openProjectFile(argv[i]); break; } }
    if (!selftest.empty()) { std::error_code ec; std::filesystem::create_directories(std::filesystem::u8path(selftest), ec); ed.prefs.autosave = false;
      ed.queue("newProject:FirstPerson:SelfTest" + std::to_string(SDL_GetTicks())); }
    int frame = 0;
    while (!ed.wantsQuit()) {
      SDL_Event e;
      while (SDL_PollEvent(&e)) ed.event(e);
      if (SDL_GetWindowFlags(W.win) & SDL_WINDOW_MINIMIZED) { SDL_Delay(30); continue; }
      ed.frame();
      if (!ctest.empty()) {   // CI: the console version - its folder, the keyboard typing with the controller, a project made with it
        frame++; auto pad = [](int b, bool down = true) { SDL_Event e{}; e.type = down ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP; e.cbutton.button = (Uint8)b; SDL_PushEvent(&e); };
        static std::string log; auto check = [&](bool c, const std::string& what) { log += (c ? "ok   " : "FAIL ") + what + "\n"; if (!c) code = 2; };
        if (frame == 10) { int ww, wh, dw, dh; SDL_GetWindowSize(W.win, &ww, &wh); SDL_GL_GetDrawableSize(W.win, &dw, &dh); check(ww == dw && wh == dh, "window " + std::to_string(ww) + "x" + std::to_string(wh) + ", drawable " + std::to_string(dw) + "x" + std::to_string(dh) + ", ui " + std::to_string((int)ImGui::GetIO().DisplaySize.x) + "x" + std::to_string((int)ImGui::GetIO().DisplaySize.y) + " vp " + std::to_string((int)ImGui::GetMainViewport()->Size.x)); std::error_code ec; std::filesystem::create_directories(std::filesystem::u8path(ctest), ec); ed.shotFile = ctest + "/console-browser.bmp"; }
        if (frame == 14) ed.focusNameField = true;
        if (frame == 18) { check(ed.oskOpen(), "a text field opens the on-screen keyboard"); pad(SDL_CONTROLLER_BUTTON_X); }
        if (frame == 20) pad(SDL_CONTROLLER_BUTTON_X);
        if (frame == 22) { pad(SDL_CONTROLLER_BUTTON_DPAD_RIGHT); pad(SDL_CONTROLLER_BUTTON_DPAD_RIGHT, false); }
        if (frame == 23) { pad(SDL_CONTROLLER_BUTTON_DPAD_RIGHT); pad(SDL_CONTROLLER_BUTTON_DPAD_RIGHT, false); }
        if (frame == 24) pad(SDL_CONTROLLER_BUTTON_A);
        if (frame == 26) pad(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
        if (frame == 27) pad(SDL_CONTROLLER_BUTTON_A);
        if (frame == 29) ed.shotFile = ctest + "/console-keyboard.bmp";
        if (frame == 31) pad(SDL_CONTROLLER_BUTTON_B);
        if (frame == 36) { check(ed.projectNameField() == "MyProjeeE", "typed with the controller: " + ed.projectNameField()); check(!ed.oskOpen(), "B closes the keyboard");
          ed.queue("newProject:FirstPerson:" + ed.projectNameField()); }
        static int vj = -1; static SDL_Joystick* vjoy = nullptr; static glm::vec3 p0, c0; static std::string moved;
        if (frame == 44) { vj = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, SDL_CONTROLLER_AXIS_MAX, SDL_CONTROLLER_BUTTON_MAX, 0); vjoy = vj >= 0 ? SDL_JoystickOpen(vj) : nullptr;
          check(vjoy != nullptr, "a (virtual) controller"); for (auto& a : ed.L().actors) if (a.type == "StaticMesh" && a.name != "Floor") { moved = a.id; break; } ed.sel = moved; c0 = ed.camPos; }
        if (frame == 46 && vjoy) { p0 = ed.actor(moved)->p; SDL_JoystickSetVirtualAxis(vjoy, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 32767); SDL_JoystickSetVirtualAxis(vjoy, SDL_CONTROLLER_AXIS_LEFTX, 32767); }
        if (frame == 52 && vjoy) { glm::vec3 p = ed.actor(moved)->p; check(glm::length(p - p0) > 0.05f, "RT + left stick moves the selected actor"); check(!(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NavEnableGamepad), "the menus don't take the sticks meanwhile");
          SDL_JoystickSetVirtualAxis(vjoy, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768); SDL_JoystickSetVirtualAxis(vjoy, SDL_CONTROLLER_AXIS_LEFTX, 0); }
        if (frame == 54 && vjoy) { glm::vec3 p = ed.actor(moved)->p; check(std::fabs(p.x / ed.snapTv - std::round(p.x / ed.snapTv)) < 1e-3f, "let go: on the grid"); check(ed.canUndo(), "one undo step");
          SDL_JoystickSetVirtualAxis(vjoy, SDL_CONTROLLER_AXIS_TRIGGERLEFT, 32767); SDL_JoystickSetVirtualAxis(vjoy, SDL_CONTROLLER_AXIS_LEFTY, -32767); }
        if (frame == 58 && vjoy) { check(glm::length(ed.camPos - c0) > 0.05f, "LT + left stick flies the camera"); SDL_JoystickSetVirtualAxis(vjoy, SDL_CONTROLLER_AXIS_TRIGGERLEFT, -32768); SDL_JoystickSetVirtualAxis(vjoy, SDL_CONTROLLER_AXIS_LEFTY, 0); }
        if (frame == 59) check(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NavEnableGamepad, "the menus have the controller again");
        if (frame == 60) { check(ed.hasProject && ed.P.path.rfind(pge::documentsDir(), 0) == 0, "the project is in the console's own folder: " + ed.P.path); ed.shotFile = ctest + "/console-editor.bmp"; }
        if (frame == 64) { pad(SDL_CONTROLLER_BUTTON_BACK); }
        if (frame == 90) { check(ed.playing, "View plays the level"); ed.shotFile = ctest + "/console-play.bmp"; }
        if (frame == 94) pad(SDL_CONTROLLER_BUTTON_BACK);
        if (frame == 100) { check(!ed.playing, "View stops playing");
          SDL_Event q{}; q.type = SDL_QUIT; ed.dirty = true; ed.event(q); check(ed.wantsQuit() && !ed.dirty, "closing saves by itself (no question)");
          std::ofstream o(std::filesystem::u8path(ctest + "/console-result.txt")); o << (code == 0 ? "PASS\n" : "FAIL\n") << log; break; }
      }
      if (!selftest.empty()) {
        frame++;
        if (frame == 20) { ed.sel = ed.L().actors.size() > 3 ? ed.L().actors[3].id : ""; ed.queue("duplicate"); ed.queue("undo"); }
        if (frame == 30) ed.queue("play");
        if (frame == 99) ed.shotFile = selftest + "/editor-play.bmp";
        if (frame == 110) ed.queue("stop");
        if (frame == 115) ed.queue("package");
        if (frame == 124) ed.shotFile = selftest + "/editor.bmp";
        if (frame == 130) {
          int errors = 0; for (auto& l : pge::logLines()) errors += l.kind == pge::LogKind::Error;
          bool thumb = std::filesystem::exists(std::filesystem::u8path(ed.P.path).parent_path() / "Saved" / "thumbnail.bmp");
          bool ok = thumb && ed.hasProject && ed.packageOk && errors == 0 && glGetError() == GL_NO_ERROR;
          std::ofstream o(std::filesystem::u8path(selftest + "/editor-result.txt"));
          o << (ok ? "PASS" : "FAIL") << "\nproject: " << ed.P.path << "\npackage: " << ed.packageDir << "\nthumbnail: " << thumb << "\nerrors: " << errors << "\n";
          for (auto& l : pge::logLines()) o << "[" << l.time << "] " << l.text << "\n";
          code = ok ? 0 : 2; break;
        }
      }
    }
    ed.savePrefs();
  }
  pge::closeWindow(W);
  return code;
}
