// PremiumEnginePlayer: runs a packaged game. A packaged game is this program (renamed to the game's name)
// next to game.pge.json and the assets folder.
#include "app.h"
#include "game.h"
#include "gl.h"
#include "log.h"
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>
#include <filesystem>
#include <cstring>

using namespace pge;

int main(int argc, char** argv) {
  std::string file = exeDir() + "/game.pge.json";
  for (int i = 1; i < argc; i++) if (std::strcmp(argv[i], "--game") == 0 && i + 1 < argc) file = argv[++i];
  bool windowed = false; for (int i = 1; i < argc; i++) if (std::strcmp(argv[i], "--windowed") == 0) windowed = true;
  std::string selftest; for (int i = 1; i + 1 < argc; i++) if (std::strcmp(argv[i], "--selftest") == 0) { selftest = argv[i + 1]; windowed = true; }   // CI check
  Project P; std::string err; g_quietErrors = !selftest.empty();
  AppWindow W; if (!openWindow(W, "Premium Engine", 1280, 720, false, false)) return 1;
  if (!loadProjectFile(file, P, &err)) {
    showError("Premium Engine", "This game's data is missing or damaged.\n\n" + err, W.win); closeWindow(W); return 1; }
  SDL_SetWindowTitle(W.win, P.name.c_str());
  if (!windowed) SDL_SetWindowFullscreen(W.win, SDL_WINDOW_FULLSCREEN_DESKTOP);
  Renderer R; R.init();
  Game G(P, P.startLevel); if (!G.start()) { showError(P.name, "The start level could not be loaded.", W.win); closeWindow(W); return 1; }
  bool running = true, paused = false, focused = true; Uint64 last = SDL_GetPerformanceCounter(); size_t seenLog = 0; std::string lastError; double errorUntil = 0;
  auto lock = [&](bool on) { SDL_SetRelativeMouseMode(on && G.wantsMouseLock() ? SDL_TRUE : SDL_FALSE); };
  auto toggleFull = [&]() { bool full = SDL_GetWindowFlags(W.win) & SDL_WINDOW_FULLSCREEN_DESKTOP; SDL_SetWindowFullscreen(W.win, full ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP); };
  if (selftest.empty()) lock(true);
  int frame = 0; glm::vec3 startPos(0); bool finishSelftest = false;
  while (running) {
    if (!selftest.empty()) {   // walk forward for a second, then check the player moved and save a screenshot
      auto key = [&](Uint32 t) { SDL_Event k{}; k.type = t; k.key.keysym.scancode = SDL_SCANCODE_W; G.event(k); };
      if (!G.loading() || SDL_GetTicks() > 120000) {   // a block world is built first
        frame++;
        if (frame == 30) { startPos = G.playerPos(); key(SDL_KEYDOWN); } if (frame == 90) key(SDL_KEYUP);
        if (frame == 120) finishSelftest = true;
      }
    }
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      ImGui_ImplSDL2_ProcessEvent(&e);
      if (e.type == SDL_QUIT) running = false;
      else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) { focused = false; G.releaseInput(); if (!paused) { paused = true; lock(false); } }
      else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) focused = true;
      // Escape: the engine's pause menu, unless the game's own menus use it (ui.escape = true; the pause menu is then the game's)
      else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE && !e.key.repeat && (paused || !G.ownsEscape())) { paused = !paused; G.releaseInput(); lock(!paused); }
      else if (e.type == SDL_KEYDOWN && !e.key.repeat && (e.key.keysym.sym == SDLK_F11 || (e.key.keysym.sym == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT)))) toggleFull();
      else if (!paused) { if (e.type == SDL_MOUSEBUTTONDOWN && !SDL_GetRelativeMouseMode()) lock(true); G.event(e); }
    }
    Uint64 now = SDL_GetPerformanceCounter(); float dt = (float)((now - last) / (double)SDL_GetPerformanceFrequency()); last = now;
    if (!selftest.empty()) dt = 1.f / 60;
    if (!paused) G.update(dt);
    G.pauseSound(paused);   // the pause menu (or another window in front) stops the game's sounds where they are
    // the game's menus free the mouse while they are open, and take it back when they close
    if (selftest.empty()) { bool want = !paused && focused && G.wantsMouseLock(); if (want != (bool)SDL_GetRelativeMouseMode()) {
        SDL_SetRelativeMouseMode(want ? SDL_TRUE : SDL_FALSE); if (!want) { int ww, wh; SDL_GetWindowSize(W.win, &ww, &wh); SDL_WarpMouseInWindow(W.win, ww / 2, wh / 2); } } }
    auto& L = logLines(); if (seenLog > L.size()) seenLog = L.size(); for (; seenLog < L.size(); seenLog++) if (L[seenLog].kind == LogKind::Error) { lastError = L[seenLog].text; errorUntil = SDL_GetTicks() / 1000.0 + 6; }
    int dw, dh; SDL_GL_GetDrawableSize(W.win, &dw, &dh);
    Frame F; Camera C; G.buildFrame(F, C, dh ? (float)dw / dh : 1.f); R.render(F, C, dw, dh, nullptr);
    beginFrame();
    ImGuiIO& io = ImGui::GetIO();
    G.drawHud(ImGui::GetBackgroundDrawList(), 0, 0, io.DisplaySize.x, io.DisplaySize.y);
    if (SDL_GetTicks() / 1000.0 < errorUntil) ImGui::GetForegroundDrawList()->AddText({12, io.DisplaySize.y - 28}, IM_COL32(255, 110, 110, 255), lastError.c_str());
    if (paused) {
      ImGui::GetBackgroundDrawList()->AddRectFilled({0, 0}, io.DisplaySize, IM_COL32(0, 0, 0, 140));
      ImGui::SetNextWindowPos({io.DisplaySize.x / 2, io.DisplaySize.y / 2}, ImGuiCond_Always, {0.5f, 0.5f}); ImGui::SetNextWindowSize({300 * W.dpi, 0});
      ImGui::Begin("Paused", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
      ImGui::TextUnformatted(P.name.c_str()); ImGui::Separator();
      ImVec2 bw(-1, 34 * W.dpi);
      if (ImGui::Button("Resume", bw)) { paused = false; lock(true); }
      if (ImGui::Button("Restart Level", bw)) { G.start(); paused = false; lock(true); }
      if (ImGui::Button((SDL_GetWindowFlags(W.win) & SDL_WINDOW_FULLSCREEN_DESKTOP) ? "Windowed (F11)" : "Full Screen (F11)", bw)) toggleFull();
      if (ImGui::Button("Quit Game", bw)) running = false;
      ImGui::End();
    }
    if (finishSelftest) {   // screenshot of this frame (before it is shown), then the result
      ImGui::Render(); int w, h; SDL_GL_GetDrawableSize(W.win, &w, &h); glViewport(0, 0, w, h); ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
      saveScreenshotBMP(selftest + "/player.bmp", w, h);
      float moved = glm::length(G.playerPos() - startPos); int errors = 0; for (auto& l : logLines()) errors += l.kind == LogKind::Error;
      bool ok = moved > 1.0f && errors == 0 && glGetError() == GL_NO_ERROR;
      if (FILE* f = std::fopen((selftest + "/player-result.txt").c_str(), "w")) { std::fprintf(f, "%s\nmoved %.2f m, %d errors\n", ok ? "PASS" : "FAIL", moved, errors);
        for (auto& l : logLines()) std::fprintf(f, "%s\n", l.text.c_str()); std::fclose(f); }   // the Output Log, for finding what went wrong
      R.shutdown(); closeWindow(W); return ok ? 0 : 2;
    }
    endFrame(W);
  }
  R.shutdown(); closeWindow(W);
  return 0;
}
