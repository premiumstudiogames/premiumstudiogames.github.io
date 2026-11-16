// Premium Engine editor entry point.
//   PremiumEngine [project.pge.json]
//   PremiumEngine --new <Blank|FirstPerson|ThirdPerson> <Name>
//   PremiumEngine --selftest <folder>   (CI: makes a project, plays it, packages it, saves a screenshot, exits 0 when all worked)
#include "editor.h"
#include "log.h"
#include "gl.h"
#include <SDL.h>
#include <cstring>
#include <fstream>
#include <filesystem>

int main(int argc, char** argv) {
  std::string selftest;
  for (int i = 1; i + 1 < argc; i++) if (!std::strcmp(argv[i], "--selftest")) selftest = argv[i + 1];
  pge::AppWindow W; pge::g_quietErrors = !selftest.empty();
  if (!pge::openWindow(W, "Premium Engine", 1600, 900, selftest.empty(), true)) return 1;
  int code = 0;
  {
    pge::Editor ed(W); ed.init();
    std::string newT, newN; for (int i = 1; i + 2 < argc; i++) if (!std::strcmp(argv[i], "--new")) { newT = argv[i + 1]; newN = argv[i + 2]; }   // from Premium Games: new project
    if (!newT.empty()) ed.queue("newProject:" + newT + ":" + newN);
    else if (selftest.empty() && argc > 1 && argv[1][0] != '-') ed.openProjectFile(argv[1]);
    if (!selftest.empty()) { std::error_code ec; std::filesystem::create_directories(std::filesystem::u8path(selftest), ec); ed.prefs.autosave = false;
      ed.queue("newProject:FirstPerson:SelfTest" + std::to_string(SDL_GetTicks())); }
    int frame = 0;
    while (!ed.wantsQuit()) {
      SDL_Event e;
      while (SDL_PollEvent(&e)) ed.event(e);
      if (SDL_GetWindowFlags(W.win) & SDL_WINDOW_MINIMIZED) { SDL_Delay(30); continue; }
      ed.frame();
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
