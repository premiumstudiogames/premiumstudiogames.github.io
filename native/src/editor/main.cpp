// Premium Engine editor entry point.
//   PremiumEngine [project.pge.json]
//   PremiumEngine --new <Blank|FirstPerson|ThirdPerson> <Name>
//   PremiumEngine --selftest <folder>   (CI: makes a project, plays it, packages it, saves a screenshot, exits 0 when all worked)
#include "editor.h"
#include "log.h"
#include "gl.h"
#include "platform.h"
#include "project.h"
#include "assets.h"
#include "testmodel.h"
#include <imgui.h>
#include <SDL.h>
#include <cstring>
#include <cmath>
#include <glm/glm.hpp>
#include <fstream>
#include <filesystem>
#include <atomic>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#include <cstdlib>
// self-tests: if the editor stops moving for 60 s, where its main thread is (function, file, line) goes to stderr and the
// progress file, and it ends - a test machine has no one to look
static std::atomic<int> g_beat{0}; static std::string g_watchFile;
static void watchdog(HANDLE mainThread) {
  int last = -1; int still = 0;
  while (true) { Sleep(1000); int b = g_beat.load(); if (b != last) { last = b; still = 0; continue; } if (++still < 60) continue;
    SuspendThread(mainThread); CONTEXT c{}; c.ContextFlags = CONTEXT_FULL; GetThreadContext(mainThread, &c);
    HANDLE proc = GetCurrentProcess(); SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME); SymInitialize(proc, nullptr, TRUE);
    STACKFRAME64 f{}; f.AddrPC.Offset = c.Rip; f.AddrPC.Mode = AddrModeFlat; f.AddrFrame.Offset = c.Rbp; f.AddrFrame.Mode = AddrModeFlat; f.AddrStack.Offset = c.Rsp; f.AddrStack.Mode = AddrModeFlat;
    std::string out = "THE EDITOR STOPPED (no frame for 60 s). Main thread:\n";
    for (int i = 0; i < 48 && StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, mainThread, &f, &c, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr); i++) {
      char buf[sizeof(SYMBOL_INFO) + 256]; SYMBOL_INFO* sym = (SYMBOL_INFO*)buf; sym->SizeOfStruct = sizeof(SYMBOL_INFO); sym->MaxNameLen = 255; DWORD64 d = 0;
      std::string name = SymFromAddr(proc, f.AddrPC.Offset, &d, sym) ? sym->Name : "?"; IMAGEHLP_LINE64 ln{}; ln.SizeOfStruct = sizeof ln; DWORD dl = 0;
      out += "  " + name + (SymGetLineFromAddr64(proc, f.AddrPC.Offset, &dl, &ln) ? std::string("  ") + ln.FileName + ":" + std::to_string(ln.LineNumber) : std::string()) + "\n"; }
    std::fprintf(stderr, "%s", out.c_str()); std::fflush(stderr); if (!g_watchFile.empty()) { std::ofstream o(std::filesystem::u8path(g_watchFile), std::ios::app); o << out; }
    TerminateProcess(proc, 3); }
}
#endif

int main(int argc, char** argv) {
  std::string selftest, ctest, bptest, mtest; bool console = false;
  for (int i = 1; i + 1 < argc; i++) { if (!std::strcmp(argv[i], "--selftest")) selftest = argv[i + 1]; if (!std::strcmp(argv[i], "--consoletest")) ctest = argv[i + 1]; if (!std::strcmp(argv[i], "--bptest")) bptest = argv[i + 1]; if (!std::strcmp(argv[i], "--modeltest")) mtest = argv[i + 1]; }
  if (!bptest.empty() || !mtest.empty()) selftest.clear();
#ifdef _WIN32
  if (!selftest.empty() || !bptest.empty() || !mtest.empty() || !ctest.empty()) {   // tests: a crash ends the program (no dialog waiting for a click on CI)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX); _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    HANDLE me = nullptr; DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &me, 0, FALSE, DUPLICATE_SAME_ACCESS);
    g_watchFile = (!mtest.empty() ? mtest : !bptest.empty() ? bptest : !ctest.empty() ? ctest : selftest) + "/stopped.txt"; std::thread(watchdog, me).detach(); }
#endif
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
    else for (int i = 1; i < argc && selftest.empty(); i++) { const char* prev = i > 1 ? argv[i - 1] : ""; if (argv[i][0] != '-' && std::strcmp(prev, "--home") && std::strcmp(prev, "--consoletest") && std::strcmp(prev, "--bptest") && std::strcmp(prev, "--modeltest")) { ed.openProjectFile(argv[i]); break; } }
    if (!selftest.empty()) { std::error_code ec; std::filesystem::create_directories(std::filesystem::u8path(selftest), ec); ed.prefs.autosave = false;
      ed.queue("newProject:FirstPerson:SelfTest" + std::to_string(SDL_GetTicks())); }
    int frame = 0;
    while (!ed.wantsQuit()) {
      SDL_Event e;
      while (SDL_PollEvent(&e)) ed.event(e);
      if (SDL_GetWindowFlags(W.win) & SDL_WINDOW_MINIMIZED) { SDL_Delay(30); continue; }
      ed.frame();
#ifdef _WIN32
      g_beat++;
#endif
      if (!bptest.empty()) {   // CI: the Blueprint Editor used like a person would - a wire dragged with the mouse, a node found by searching, Play
        frame++; static std::string log; auto check = [&](bool c, const std::string& what) { log += (c ? "ok   " : "FAIL ") + what + "\n"; if (!c) code = 2; };
        Uint32 wid = SDL_GetWindowID(W.win); SDL_Window* sw = W.win; auto mouse = [wid, sw](float x, float y, int btn = 0, bool down = false, bool up = false) { SDL_WarpMouseInWindow(sw, (int)x, (int)y); SDL_Event e{}; e.type = SDL_MOUSEMOTION; e.motion.windowID = wid; e.motion.x = (int)x; e.motion.y = (int)y; SDL_PushEvent(&e);
          if (down || up) { SDL_Event b{}; b.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP; b.button.button = (Uint8)btn; b.button.windowID = wid; b.button.x = (int)x; b.button.y = (int)y; b.button.clicks = 1; SDL_PushEvent(&b); } };
        static std::string bpId, cube; static int addN = 0, mkN = 0; static ImVec2 p1, p2; static float y0 = 0;
        pge::BpTab* t = ed.bpTabs.empty() ? nullptr : ed.bpTabs.back().get();
        auto pinScreen = [&](int node, const char* pin) { for (auto& p : t->pins_) if (p.node == node && p.pin == pin) return t->toScreen(p.pos); return ImVec2(-1, -1); };
        if (frame == 5) { std::error_code ec; std::filesystem::create_directories(std::filesystem::u8path(bptest), ec); ed.queue("newProject:FirstPerson:BPTest" + std::to_string(SDL_GetTicks())); }
        if (frame == 40) { check(ed.hasProject, "a project");
          for (auto& a : ed.L().actors) if (a.type == "StaticMesh" && a.name != "Floor" && a.physics.simulate == false && a.p.y > 0.2f) { cube = a.id; break; }
          bpId = ed.newBlueprint("Mover"); if (pge::Actor* a = ed.actor(cube)) a->script = bpId; ed.openBlueprint(bpId); }
        if (frame == 46 && t) { pge::BpNode a; a.id = t->bp.nextId++; a.type = "Actor.AddOffset"; a.x = 400; a.y = 220; t->bp.nodes.push_back(a); addN = a.id;
          pge::BpNode m; m.id = t->bp.nextId++; m.type = "Vector.Make"; m.x = 128; m.y = 400; t->bp.nodes.push_back(m); mkN = m.id;
          t->bp.links.push_back({2, "DeltaSeconds", mkN, "Y"}); t->bp.links.push_back({mkN, "ReturnValue", addN, "DeltaLocation"}); t->commit(ed); t->frameNext = true; }
        if (frame == 52 && t) { p1 = pinScreen(2, "then"); p2 = pinScreen(addN, "exec"); check(p1.x > 0 && p2.x > 0, "the pins are on screen"); mouse(p1.x, p1.y); }
        if (frame == 53) mouse(p1.x, p1.y, SDL_BUTTON_LEFT, true);
        if (frame == 54) mouse((p1.x + p2.x) / 2, (p1.y + p2.y) / 2 + 30);
        if (frame == 55) mouse(p2.x, p2.y);
        if (frame == 56) mouse(p2.x, p2.y, SDL_BUTTON_LEFT, false, true);
        if (frame == 60 && t) { bool w = false; for (auto& l : t->bp.links) w = w || (l.from == 2 && l.fromPin == "then" && l.to == addN && l.toPin == "exec"); check(w, "a wire dragged from Tick's exec pin to Add Actor World Offset");
          ImVec2 c(t->origin_.x + t->size_.x - 60, t->origin_.y + t->size_.y - 60); mouse(c.x, c.y); }
        if (frame == 61 && t) { ImVec2 c(t->origin_.x + t->size_.x - 60, t->origin_.y + t->size_.y - 60); mouse(c.x, c.y, SDL_BUTTON_RIGHT, true); }
        if (frame == 62 && t) { ImVec2 c(t->origin_.x + t->size_.x - 60, t->origin_.y + t->size_.y - 60); mouse(c.x, c.y, SDL_BUTTON_RIGHT, false, true); }
        if (frame == 66) { SDL_Event e{}; e.type = SDL_TEXTINPUT; e.text.windowID = wid; std::strcpy(e.text.text, "print"); SDL_PushEvent(&e); }
        if (frame == 68) ed.shotFile = bptest + "/bp-menu.bmp";
        if (frame == 70) { SDL_Event e{}; e.type = SDL_KEYDOWN; e.key.windowID = wid; e.key.keysym.scancode = SDL_SCANCODE_RETURN; e.key.keysym.sym = SDLK_RETURN; SDL_PushEvent(&e); SDL_Event u = e; u.type = SDL_KEYUP; SDL_PushEvent(&u); }
        if (frame == 76 && t) { bool pr = false; for (auto& n : t->bp.nodes) pr = pr || n.type == "Utilities.PrintString"; check(pr, "right-click → search \"print\" → Enter adds Print String");
          int errs = 0; for (auto& m : t->msgs) errs += m.error; check(errs == 0, "the Blueprint compiles"); t->frameNext = true; ed.prefs.bpLang = 0; }
        if (frame == 80) ed.shotFile = bptest + "/bp-english.bmp";
        if (frame == 84) { ed.prefs.bpLang = 1; } if (frame == 88) ed.shotFile = bptest + "/bp-arabic.bmp";
        if (frame == 92) { ed.prefs.bpLang = 2; } if (frame == 96) ed.shotFile = bptest + "/bp-both.bmp";
        if (frame == 100) { ed.prefs.bpLang = 0; ed.play(); }
        if (frame == 104 && ed.game) { y0 = ed.game->worldPos(ed.game->find(ed.actor(cube) ? ed.actor(cube)->name : "")).y; }
        if (frame == 170) { check(ed.playing && ed.game, "Play");
          float y1 = ed.game ? ed.game->worldPos(ed.game->find(ed.actor(cube) ? ed.actor(cube)->name : "")).y : 0; check(y1 > y0 + 0.2f, "the Blueprint moves the cube up while playing (" + std::to_string(y0) + " → " + std::to_string(y1) + ")");
          ed.stopPlay(); ed.undo(); check(t && t->bp.nodes.size() >= 4, "undo keeps the editor in step"); }
        if (frame == 172 && t) {   // a function with an input and an output, opened in its own graph, called from the Event Graph
          pge::BpFunction f; f.name = "Double"; f.inputs = {{"Value", pge::PinType::Number}}; f.outputs = {{"Result", pge::PinType::Number}};
          pge::BpNode en; en.id = t->bp.nextId++; en.type = "Function.Entry"; en.values["Function"] = "Double"; pge::BpNode m; m.id = t->bp.nextId++; m.type = "Math.Multiply"; m.x = 260; m.y = 120; m.values["B"] = 2;
          pge::BpNode rt; rt.id = t->bp.nextId++; rt.type = "Function.Return"; rt.x = 560; rt.values["Function"] = "Double"; f.nodes = {en, m, rt};
          f.links = {{en.id, "then", rt.id, "exec"}, {en.id, "Value", m.id, "A"}, {m.id, "ReturnValue", rt.id, "Result"}}; t->bp.funcs.push_back(f);
          pge::BpNode c; c.id = t->bp.nextId++; c.type = "Function.Call"; c.x = 700; c.y = 500; c.values["Function"] = "Double"; t->bp.nodes.push_back(c);
          t->commit(ed); t->graph = 0; t->frameNext = true; ed.prefs.bpLang = 2; ImGui::SetWindowFocus("Blueprint Editor"); }
        if (frame == 174) ed.shotFile = bptest + "/bp-function.bmp";
        if (frame == 175 && t) { int errs = 0; for (auto& m : t->msgs) errs += m.error; check(errs == 0 && ed.P.scripts.count(bpId) && ed.P.scripts[bpId].code.find("function __fn_Double(__a0)") != std::string::npos, "a function (Double: Value → Result) compiles into the Blueprint's code"); ed.prefs.bpLang = 0; }
        if (frame == 176) { std::ofstream o(std::filesystem::u8path(bptest + "/bp-result.txt")); o << (code == 0 ? "PASS\n" : "FAIL\n") << log; break; }
      }
      if (!mtest.empty()) {   // CI: models and textures used like a person would - a .glb dropped on the viewport, its animation, a picture on a material, Play, Package
        frame++; static std::string log;
        { std::ofstream pr(std::filesystem::u8path(mtest + "/model-progress.txt"), std::ios::trunc); pr << "frame " << frame << " at " << SDL_GetTicks() << " ms\n" << log; for (auto& l : pge::logLines()) pr << "[" << l.time << "] " << l.text << "\n"; }   // CI: where it was if it stops
        auto check = [&](bool c, const std::string& what) { log += (c ? "ok   " : "FAIL ") + what + "\n"; if (!c) code = 2; };
        namespace fs = std::filesystem; static fs::path src; static std::string botId, cubeId;
        // what a screenshot shows: how many pixels are mostly red / green, and how wide the red ones spread
        struct Px { int red = 0, green = 0, rx0 = 1 << 30, rx1 = -1; };
        int rx = (int)ed.vpX, ry = (int)ed.vpY, rw = (int)ed.vpW, rh = (int)ed.vpH;   // only the viewport (the panels have red X fields)
        auto look = [&](const std::string& f) { Px r; SDL_Surface* s0 = SDL_LoadBMP(f.c_str()); if (!s0) return r; SDL_Surface* s = SDL_ConvertSurfaceFormat(s0, SDL_PIXELFORMAT_RGBA32, 0); SDL_FreeSurface(s0); if (!s) return r;
          for (int y = std::max(ry, 0); y < std::min(ry + rh, s->h); y++) for (int x = std::max(rx, 0); x < std::min(rx + rw, s->w); x++) { const Uint8* p = (const Uint8*)s->pixels + y * s->pitch + x * 4;
            if (p[0] > 50 && p[0] > 3 * p[1] && p[0] > 3 * p[2]) { r.red++; r.rx0 = std::min(r.rx0, x); r.rx1 = std::max(r.rx1, x); } if (p[1] > 50 && p[1] > 2 * p[0] && p[1] > 2 * p[2]) r.green++; }
          SDL_FreeSurface(s); return r; };
        if (frame == 5) { std::error_code ec; fs::create_directories(fs::u8path(mtest), ec); src = fs::u8path(mtest) / "source"; fs::create_directories(src, ec);
          check(pge::writeTestModel((src / "TestBot.glb").u8string()), "a model file outside the project (TestBot.glb)");
          unsigned char g[8 * 8 * 4]; for (int i = 0; i < 64; i++) { g[i * 4] = 20; g[i * 4 + 1] = 230; g[i * 4 + 2] = 30; g[i * 4 + 3] = 255; } stbi_write_png((src / "Grass.png").u8string().c_str(), 8, 8, 4, g, 32);
          ed.queue("newProject:Blank:ModelTest" + std::to_string(SDL_GetTicks())); ed.prefs.autosave = false; }
        if (frame == 30) { check(ed.hasProject, "a project"); ed.camYaw = 0; ed.camPitch = 0; ed.camPos = {0, 1, 4}; ed.animPaused = true; ed.animClock = 0; ed.shotFile = mtest + "/model-before.bmp"; }
        if (frame == 34) { SDL_WarpMouseInWindow(W.win, (int)(ed.vpX + ed.vpW / 2), (int)(ed.vpY + ed.vpH * 0.8f)); }
        if (frame == 36) { SDL_Event e{}; e.type = SDL_DROPFILE; e.drop.file = SDL_strdup((src / "TestBot.glb").u8string().c_str()); e.drop.windowID = SDL_GetWindowID(W.win); SDL_PushEvent(&e); }
        if (frame == 40) { fs::path inProj = fs::u8path(ed.P.path).parent_path() / "Models" / "TestBot.glb"; check(fs::exists(inProj), "dropped on the window: copied into the project's Models folder");
          for (auto& a : ed.L().actors) if (a.mesh == "Models/TestBot.glb") botId = a.id; pge::Actor* b = ed.actor(botId);
          check(b != nullptr, "dropped on the viewport: placed in the level"); check(b && b->material.empty() && b->name == "TestBot", "…with its own materials, named after the file");
          if (b) { check(b->p.y > -0.01f && b->p.y < 0.6f, "…standing on the ground (y = " + std::to_string(b->p.y) + ")"); b->p = {0, 0, 0}; b->anim = "Wave"; b->animLoop = true; ed.touch(); ed.sel = botId; }
          ed.scanFiles(true); check(ed.projModels.size() == 1 && ed.projModels[0] == "Models/TestBot.glb", "the Content Browser's Models folder shows it"); }
        if (frame == 44) ed.shotFile = mtest + "/model-rest.bmp";
        if (frame == 48) { ed.animClock = 0.5f; ed.shotFile = mtest + "/model-wave.bmp"; }
        if (frame == 52) { Px a = look(mtest + "/model-before.bmp"), b = look(mtest + "/model-rest.bmp"), c = look(mtest + "/model-wave.bmp");
          check(b.red > a.red + 2000, "the model is drawn with the red picture inside its file (" + std::to_string(a.red) + " → " + std::to_string(b.red) + " red pixels)");
          check(c.rx1 - c.rx0 > (b.rx1 - b.rx0) + 20, "its animation bends it on the GPU (red width " + std::to_string(b.rx1 - b.rx0) + " → " + std::to_string(c.rx1 - c.rx0) + " px)");
          ed.animPaused = false;
          auto r = ed.importFiles({(src / "Grass.png").u8string()}); check(r.size() == 1 && r[0] == "Textures/Grass.png", "a picture imported into Textures");
          std::string mid = ed.newMaterial("M_Grass"); ed.P.materials[mid].baseMap = r.empty() ? "" : r[0]; glm::vec3 at(1.2f, 0, 0); pge::Actor& cb = ed.addActor("StaticMesh", "Cube", &at); cb.material = mid; cubeId = cb.id; ed.touch(); ed.shotFile = mtest + "/model-texture.bmp"; }
        if (frame == 54) { ed.materialOpen = "M_Grass"; ed.wMaterial = true; ed.cbFolder = "Textures"; ed.wContent = true; ImGui::SetWindowFocus("Content Browser"); }
        if (frame == 55) ed.shotFile = mtest + "/model-ui.bmp";
        if (frame == 56) { Px a = look(mtest + "/model-wave.bmp"), d = look(mtest + "/model-texture.bmp"); check(d.green > a.green + 2000, "a material with a Base Color picture draws it (" + std::to_string(a.green) + " → " + std::to_string(d.green) + " green pixels)");
          ed.runMapCheck(); bool bad = false; for (auto& m : ed.mapCheck) bad = bad || m.rfind("error", 0) == 0; check(!bad, "Map Check: no errors"); ed.play(); }
        if (frame == 90) { check(ed.playing && ed.game, "Play"); std::string st = ed.game ? ed.game->animation(ed.game->find("TestBot"), "state", "") : "";
          check(st.find("\"Wave\"") != std::string::npos && st.find("\"playing\":true") != std::string::npos, "in the game the model plays Wave from the start: " + st); ed.shotFile = mtest + "/model-play.bmp"; }
        if (frame == 94) { ed.stopPlay(); ed.packageProject(); }
        // more models to look at (a folder of .glb / .gltf): placed in a row, each with its first animation, a screenshot (not checked)
        if (const char* extra = std::getenv("PGE_MODELTEST_EXTRA")) {
          static std::vector<std::string> ids;
          if (frame == 96) { std::vector<std::string> fl; std::error_code ec; for (auto& e : fs::directory_iterator(fs::u8path(extra), ec)) if (pge::isModelFile(e.path().u8string())) fl.push_back(e.path().u8string());
            std::sort(fl.begin(), fl.end()); auto r = ed.importFiles(fl); float x = -3; for (auto& m : r) { glm::vec3 at(x, 0, 1.5f); pge::Actor& a = ed.addActor("StaticMesh", m, &at); pge::ModelRef md = pge::model(m);
              if (md && md->ok()) { glm::vec3 sz = md->shape.bmax - md->shape.bmin; float k = 1.8f / std::max({sz.x, sz.y, sz.z, 1e-3f}); a.s = glm::vec3(k); a.p.y = -md->shape.bmin.y * k; if (!md->anims.empty()) a.anim = md->anims[0].name; }
              log += "extra: " + m + (md ? (md->ok() ? " " + std::to_string(md->triangles) + " tris, " + std::to_string(md->anims.size()) + " anims" : " ERROR " + md->error) : "") + "\n"; x += 2.5f; }
            ed.sel.clear(); ed.wPackage = false; ed.camPos = {-0.5f, 1.4f, 5.5f}; ed.camYaw = 0; ed.camPitch = -0.08f; ed.animPaused = false; ed.touch(); }
          if (frame == 99) ed.shotFile = mtest + "/model-extra.bmp";
          if (frame == 100) ed.shotFile.clear(); }
        // camera effects: the vignette darkens the corners, the VHS look changes the picture (in the editor's viewport, as in the game)
        static float corner0 = 0, corner1 = 0; static double diff = 0;
        auto cornerLum = [&](const std::string& f, double* d2 = nullptr, const std::string& other = "") { SDL_Surface* s0 = SDL_LoadBMP(f.c_str()); if (!s0) return 0.f; SDL_Surface* s = SDL_ConvertSurfaceFormat(s0, SDL_PIXELFORMAT_RGBA32, 0); SDL_FreeSurface(s0);
          SDL_Surface* o = nullptr; if (d2 && !other.empty()) { SDL_Surface* o0 = SDL_LoadBMP(other.c_str()); if (o0) { o = SDL_ConvertSurfaceFormat(o0, SDL_PIXELFORMAT_RGBA32, 0); SDL_FreeSurface(o0); } }
          double sum = 0; int n = 0; double dd = 0; int dn = 0;
          for (int y = ry + 30; y < ry + rh - 4; y += 2) for (int x = rx + 4; x < rx + rw - 4; x += 2) { const Uint8* p = (const Uint8*)s->pixels + y * s->pitch + x * 4;
            bool corner = (x < rx + rw / 8 || x > rx + rw * 7 / 8) && (y < ry + rh / 6 || y > ry + rh * 5 / 6); if (corner) { sum += p[0] + p[1] + p[2]; n++; }
            if (o) { const Uint8* q = (const Uint8*)o->pixels + y * o->pitch + x * 4; dd += std::abs(p[0] - q[0]) + std::abs(p[1] - q[1]) + std::abs(p[2] - q[2]); dn++; } }
          if (o) SDL_FreeSurface(o); SDL_FreeSurface(s); if (d2) *d2 = dn ? dd / dn : 0; return n ? (float)(sum / n) : 0.f; };
        if (frame == 104) { ed.sel.clear(); ed.wPackage = false; ed.L().world.post = pge::PostProcess(); ed.shotFile = mtest + "/post-off.bmp"; }
        if (frame == 106) { ed.L().world.post.vignette = 1; ed.shotFile = mtest + "/post-vignette.bmp"; }
        if (frame == 108) { ed.L().world.post = pge::PostProcess(); ed.L().world.post.vhs = 0.8f; ed.L().world.post.grain = 0.4f; ed.L().world.post.scanlines = 0.4f; ed.shotFile = mtest + "/post-vhs.bmp"; }
        if (frame == 110) { corner0 = cornerLum(mtest + "/post-off.bmp"); corner1 = cornerLum(mtest + "/post-vignette.bmp"); cornerLum(mtest + "/post-vhs.bmp", &diff, mtest + "/post-off.bmp");
          check(corner1 < corner0 * 0.75f, "Post Process: the vignette darkens the corners (" + std::to_string((int)corner0) + " → " + std::to_string((int)corner1) + ")");
          check(diff > 6, "Post Process: VHS + grain + scanlines change the picture (" + std::to_string(diff) + " per pixel)"); ed.L().world.post = pge::PostProcess(); }
        if (frame == 111) ed.showNav = true;
        if (frame == 114) { check(ed.navPreview.walkable() > 1000, "Show > Navigation: the walkable floor (" + std::to_string(ed.navPreview.walkable()) + " cells)"); ed.shotFile = mtest + "/navigation.bmp"; }
        // a reflection capture: a mirror cube shows what is around it (the red model, the green cube) instead of the sky colours
        if (frame == 116) { ed.showNav = false; std::string mid = ed.newMaterial("M_Mirror"); ed.P.materials[mid].metalness = 1; ed.P.materials[mid].roughness = 0.05f; ed.P.materials[mid].color = glm::vec3(0.95f);
          glm::vec3 at(0.6f, 0.45f, 1.6f); pge::Actor& m = ed.addActor("StaticMesh", "Sphere", &at); m.material = mid; m.s = glm::vec3(0.9f); m.p = at; ed.touch(); ed.sel.clear(); ed.shotFile = mtest + "/refl-off.bmp"; }
        if (frame == 118) { glm::vec3 at(0.4f, 1.5f, 2.6f); pge::Actor& c = ed.addActor("ReflectionCapture", "", &at); c.probe.radius = 8; ed.touch(); ed.sel.clear(); }
        if (frame == 126) { check(ed.R.probesReady() >= 1, "a Sphere Reflection Capture is made"); ed.shotFile = mtest + "/refl-on.bmp"; }
        if (frame == 128) { double dd = 0; cornerLum(mtest + "/refl-on.bmp", &dd, mtest + "/refl-off.bmp"); check(dd > 0.3, "…and shiny surfaces reflect what it sees (" + std::to_string(dd) + " per pixel)"); }
        // Third Person: the mannequin runs (W + Shift) in Play, the camera behind it
        if (frame == 132) { ed.L().world.gameMode = "ThirdPerson"; ed.touch(); ed.play(); }
        if (frame == 140 && ed.game) { SDL_Event k{}; k.type = SDL_KEYDOWN; k.key.keysym.scancode = SDL_SCANCODE_W; ed.game->event(k); k.key.keysym.scancode = SDL_SCANCODE_LSHIFT; ed.game->event(k); }
        if (frame == 170) { check(ed.game && ed.game->playerAnimation() == "Run", "Third Person: the mannequin runs (" + std::string(ed.game ? ed.game->playerAnimation() : "no game") + ")"); ed.shotFile = mtest + "/third-person.bmp"; }
        if (frame == 174) { ed.stopPlay(); ed.L().world.gameMode = "FirstPerson"; }
        // Landscape: placed, hills made, then sculpted with the mouse like a person would (left button held on it)
        static std::string landId; static float hBefore = 0; static glm::vec3 brushAt(0);
        Uint32 wid2 = SDL_GetWindowID(W.win); auto mouse2 = [&](float x, float y, int btn = 0, bool down = false, bool up = false) { SDL_WarpMouseInWindow(W.win, (int)x, (int)y); SDL_Event e{}; e.type = SDL_MOUSEMOTION; e.motion.windowID = wid2; e.motion.x = (int)x; e.motion.y = (int)y; SDL_PushEvent(&e);
          if (down || up) { SDL_Event b{}; b.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP; b.button.button = (Uint8)btn; b.button.windowID = wid2; b.button.x = (int)x; b.button.y = (int)y; b.button.clicks = 1; SDL_PushEvent(&b); } };
        if (frame == 178) { for (auto it = ed.L().actors.begin(); it != ed.L().actors.end();) { if (it->name == "Floor") it = ed.L().actors.erase(it); else ++it; }
          glm::vec3 at(0); pge::Actor& la = ed.addActor("Landscape", "", &at); landId = la.id; la.land->resize(128, 96); la.land->hills(4, 25, 11); ed.landTool = 0; ed.landRadius = 6; ed.landStrength = 3; ed.sel = landId; ed.touch();
          ed.camPos = {0, 18, 30}; ed.camYaw = 0; ed.camPitch = -0.5f; ImGui::SetWindowFocus("Viewport"); }
        if (frame == 182) mouse2(ed.vpX + ed.vpW / 2, ed.vpY + ed.vpH / 2);
        if (frame == 184) { // where the brush is: the landscape under the viewport's middle
          pge::Actor* la = ed.actor(landId); if (la) { float px = ed.camPos.x, py = ed.camPos.y, pz = ed.camPos.z; glm::vec3 d(-std::sin(ed.camYaw) * std::cos(ed.camPitch), std::sin(ed.camPitch), -std::cos(ed.camYaw) * std::cos(ed.camPitch)); float tt = 0;
            if (la->land->raycast({px, py, pz}, d, 500, tt)) { brushAt = glm::vec3(px, py, pz) + d * tt; hBefore = la->land->heightAt(brushAt.x, brushAt.z); } }
          mouse2(ed.vpX + ed.vpW / 2, ed.vpY + ed.vpH / 2, SDL_BUTTON_LEFT, true); }
        if (frame == 196) mouse2(ed.vpX + ed.vpW / 2, ed.vpY + ed.vpH / 2, SDL_BUTTON_LEFT, false, true);
        if (frame == 199) { pge::Actor* la = ed.actor(landId); float h1 = la ? la->land->heightAt(brushAt.x, brushAt.z) : 0;
          check(la && h1 > hBefore + 0.3f, "Landscape: holding the mouse on it raises the ground under the brush (" + std::to_string(hBefore) + " → " + std::to_string(h1) + ")"); check(ed.canUndo(), "…one undo step");
          ed.landTool = -1; ed.shotFile = mtest + "/landscape.bmp"; }
        if (frame == 203) { if (const char* v = std::getenv("PGE_MT_VARIANT")) { std::string vv = v;   // CI: which part of a Play on a landscape stops a driver
            if (vv.find("noprobe") != std::string::npos) for (auto it = ed.L().actors.begin(); it != ed.L().actors.end();) { if (it->type == "ReflectionCapture") it = ed.L().actors.erase(it); else ++it; }
            if (vv.find("nomsaa") != std::string::npos) ed.R.quality.msaa = 0;
            if (vv.find("noshadow") != std::string::npos) ed.R.quality.shadowDistance = 0;
            if (vv.find("nopost") != std::string::npos) { ed.R.quality.ssao = false; ed.R.quality.bloom = false; }
            if (vv.find("noland") != std::string::npos) for (auto it = ed.L().actors.begin(); it != ed.L().actors.end();) { if (it->type == "Landscape") it = ed.L().actors.erase(it); else ++it; }
            std::fprintf(stderr, "variant %s\n", v); }
          ed.L().world.gameMode = std::getenv("PGE_MT_FP") ? "FirstPerson" : "ThirdPerson"; for (auto& a : ed.L().actors) if (a.type == "PlayerStart") a.p = {0, 12, 8}; ed.touch(); ed.play(); }
        if (frame > 203 && frame < 240 && ed.game && std::getenv("PGE_DEBUG_PLAY")) { glm::vec3 pp = ed.game->playerPos(), cp = ed.game->cameraPos(); std::fprintf(stderr, "f%d player %.3f %.3f %.3f cam %.3f %.3f %.3f t=%u\n", frame, pp.x, pp.y, pp.z, cp.x, cp.y, cp.z, SDL_GetTicks()); }
        if (std::getenv("PGE_DEBUG_PLAY") || (std::getenv("PGE_MT_VARIANT") && frame > 200)) { std::fprintf(stderr, "frame %d t=%u draws=%d tris=%d\n", frame, SDL_GetTicks(), ed.R.drawCalls, ed.R.triangles); std::fflush(stderr); }
        if (frame == 240) { glm::vec3 pp = ed.game ? ed.game->playerPos() : glm::vec3(0); pge::Actor* la = ed.actor(landId); float gy = la ? la->land->heightAt(pp.x, pp.z) : -99;
          check(ed.game && std::abs(pp.y - 0.9f - gy) < 0.3f, "Play: the mannequin stands on the landscape (feet " + std::to_string(pp.y - 0.9f) + ", ground " + std::to_string(gy) + ")"); ed.shotFile = mtest + "/landscape-play.bmp"; }
        if (frame == 244) { ed.stopPlay(); }
        if (frame == 248) { check(ed.packageOk, "Package"); check(fs::exists(fs::u8path(ed.packageDir) / "Models" / "TestBot.glb") && fs::exists(fs::u8path(ed.packageDir) / "Textures" / "Grass.png"), "the packaged game has the model and the picture");
          check(glGetError() == GL_NO_ERROR, "no OpenGL errors");
          std::ofstream o(fs::u8path(mtest + "/model-result.txt")); o << (code == 0 ? "PASS\n" : "FAIL\n") << log; for (auto& l : pge::logLines()) o << "[" << l.time << "] " << l.text << "\n"; break; }
      }
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
