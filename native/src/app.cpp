#include "app.h"
#include "gl.h"
#include "log.h"
#include "project.h"
#include "ui_style.h"
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>
#include <filesystem>
#include <algorithm>
#include <vector>
#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#endif

namespace pge {
bool g_quietErrors = false;
void showError(const std::string& title, const std::string& msg, SDL_Window* parent) {
  std::fprintf(stderr, "%s: %s\n", title.c_str(), msg.c_str()); logMsg("error: " + msg, LogKind::Error);
  if (!g_quietErrors) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title.c_str(), msg.c_str(), parent);
}
std::string assetsDir() { return exeDir() + "/assets"; }

bool openWindow(AppWindow& w, const std::string& title, int width, int height, bool maximized, bool docking) {
  SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
  SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON, "1");        // window + taskbar icon from the .exe (resource 1)
  SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL, "1");
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) { showError(title, SDL_GetError()); return false; }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1); SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24); SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
  Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (maximized ? SDL_WINDOW_MAXIMIZED : 0);
  w.win = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, flags);
  if (!w.win) { showError(title, SDL_GetError()); return false; }
  w.gl = SDL_GL_CreateContext(w.win);
  if (!w.gl || !pgeLoadGL()) {
    std::string have; if (w.gl) { auto gs = (const unsigned char* (*)(unsigned))SDL_GL_GetProcAddress("glGetString"); if (gs && gs(0x1F02)) have = std::string("\n\nThis PC has OpenGL ") + (const char*)gs(0x1F02) + "."; }
    showError(title, "OpenGL 3.3 is not available. Please update your graphics driver." + have, w.win); return false; }
  SDL_GL_MakeCurrent(w.win, w.gl); SDL_GL_SetSwapInterval(1);
  float ddpi = 96; if (SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(w.win), &ddpi, nullptr, nullptr) == 0 && ddpi > 0) w.dpi = std::max(1.f, ddpi / 96.f);
  IMGUI_CHECKVERSION(); ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO(); io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; if (docking) io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  std::error_code ec; std::filesystem::create_directories(std::filesystem::u8path(documentsDir()), ec);
  static std::string ini; ini = documentsDir() + "/editor-layout.ini"; io.IniFilename = docking ? ini.c_str() : nullptr;
  applyEditorStyle(w.dpi); loadFonts(assetsDir(), w.dpi);
  ImGui_ImplSDL2_InitForOpenGL(w.win, w.gl); ImGui_ImplOpenGL3_Init("#version 330 core");
  return true;
}
void beginFrame() { ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplSDL2_NewFrame(); ImGui::NewFrame(); }
void endFrame(AppWindow& w) {
  ImGui::Render(); int dw, dh; SDL_GL_GetDrawableSize(w.win, &dw, &dh);
  glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, dw, dh);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData()); SDL_GL_SwapWindow(w.win);
}
void closeWindow(AppWindow& w) {
  ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplSDL2_Shutdown(); ImGui::DestroyContext();
  if (w.gl) SDL_GL_DeleteContext(w.gl); if (w.win) SDL_DestroyWindow(w.win); SDL_Quit(); w = AppWindow();
}
}

namespace pge {
bool writeBMP(const std::string& file, int w, int h, const unsigned char* bgrBottomUp) {
  int row = (w * 3 + 3) & ~3; unsigned size = 54 + row * h;
  unsigned char hd[54] = {'B', 'M'}; auto u32 = [&](int o, unsigned v) { for (int i = 0; i < 4; i++) hd[o + i] = (v >> (8 * i)) & 255; };
  u32(2, size); u32(10, 54); u32(14, 40); u32(18, w); u32(22, h); hd[26] = 1; hd[28] = 24; u32(34, row * h);
  std::string tmp = file + ".tmp"; FILE* f = nullptr;
#ifdef _WIN32
  int n = MultiByteToWideChar(CP_UTF8, 0, tmp.c_str(), -1, nullptr, 0); std::wstring wf(n, L'\0'); MultiByteToWideChar(CP_UTF8, 0, tmp.c_str(), -1, wf.data(), n); f = _wfopen(wf.c_str(), L"wb");
#else
  f = std::fopen(tmp.c_str(), "wb");
#endif
  if (!f) return false; std::fwrite(hd, 1, 54, f); std::vector<unsigned char> pad(row - w * 3, 0);
  for (int y = 0; y < h; y++) { std::fwrite(bgrBottomUp + (size_t)y * w * 3, 1, w * 3, f); if (!pad.empty()) std::fwrite(pad.data(), 1, pad.size(), f); }
  std::fclose(f);
  std::error_code ec; std::filesystem::rename(std::filesystem::u8path(tmp), std::filesystem::u8path(file), ec); return !ec;   // whole file or nothing
}
bool readPixelsBMP(const std::string& file, unsigned fbo, int w, int h) {
  std::vector<unsigned char> px((size_t)w * h * 3); glBindFramebuffer(GL_FRAMEBUFFER, fbo); glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, w, h, GL_BGR, GL_UNSIGNED_BYTE, px.data()); glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return writeBMP(file, w, h, px.data());
}
bool saveScreenshotBMP(const std::string& file, int w, int h) { return readPixelsBMP(file, 0, w, h); }
}
