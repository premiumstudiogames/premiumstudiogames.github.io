#pragma once
// Window + OpenGL 3.3 context + Dear ImGui, shared by the editor and the game player.
#include <SDL.h>
#include <string>
namespace pge {
struct AppWindow { SDL_Window* win = nullptr; SDL_GLContext gl = nullptr; float dpi = 1; };
extern bool g_quietErrors;   // self-tests: print errors instead of showing a message box that waits for a click
void showError(const std::string& title, const std::string& msg, SDL_Window* parent = nullptr);
bool openWindow(AppWindow& w, const std::string& title, int width, int height, bool maximized, bool docking);
void beginFrame();
void endFrame(AppWindow& w);
void closeWindow(AppWindow& w);
std::string assetsDir();
bool readPixelsBMP(const std::string& file, unsigned fbo, int w, int h);   // a framebuffer → .bmp
bool saveScreenshotBMP(const std::string& file, int w, int h);   // the window's back buffer (call before endFrame swaps)
}
