#include "ui_style.h"
#include <imgui.h>
#include <filesystem>

namespace pge {
void applyEditorStyle(float scale) {
  ImGuiStyle& s = ImGui::GetStyle(); ImGui::StyleColorsDark(&s);
  s.WindowRounding = 4; s.FrameRounding = 3; s.TabRounding = 3; s.ScrollbarRounding = 6; s.GrabRounding = 3; s.PopupRounding = 4; s.ChildRounding = 3;
  s.WindowBorderSize = 1; s.FrameBorderSize = 0; s.TabBorderSize = 0; s.WindowPadding = {8, 8}; s.FramePadding = {6, 4}; s.ItemSpacing = {6, 5}; s.IndentSpacing = 14;
  s.WindowMenuButtonPosition = ImGuiDir_None; s.DockingSeparatorSize = 3;
  ImVec4* c = s.Colors;
  auto rgb = [](int r, int g, int b, float a = 1) { return ImVec4(r / 255.f, g / 255.f, b / 255.f, a); };
  c[ImGuiCol_Text] = rgb(220, 220, 220); c[ImGuiCol_TextDisabled] = rgb(120, 120, 120);
  c[ImGuiCol_WindowBg] = rgb(36, 36, 36); c[ImGuiCol_ChildBg] = rgb(30, 30, 30); c[ImGuiCol_PopupBg] = rgb(26, 26, 26, 0.98f);
  c[ImGuiCol_Border] = rgb(14, 14, 14); c[ImGuiCol_BorderShadow] = rgb(0, 0, 0, 0);
  c[ImGuiCol_FrameBg] = rgb(15, 15, 15); c[ImGuiCol_FrameBgHovered] = rgb(24, 24, 24); c[ImGuiCol_FrameBgActive] = rgb(30, 30, 30);
  c[ImGuiCol_TitleBg] = rgb(21, 21, 21); c[ImGuiCol_TitleBgActive] = rgb(21, 21, 21); c[ImGuiCol_TitleBgCollapsed] = rgb(21, 21, 21);
  c[ImGuiCol_MenuBarBg] = rgb(21, 21, 21); c[ImGuiCol_ScrollbarBg] = rgb(26, 26, 26); c[ImGuiCol_ScrollbarGrab] = rgb(70, 70, 70);
  c[ImGuiCol_CheckMark] = rgb(0, 112, 224); c[ImGuiCol_SliderGrab] = rgb(0, 112, 224); c[ImGuiCol_SliderGrabActive] = rgb(30, 140, 250);
  c[ImGuiCol_Button] = rgb(56, 56, 56); c[ImGuiCol_ButtonHovered] = rgb(76, 76, 76); c[ImGuiCol_ButtonActive] = rgb(0, 112, 224);
  c[ImGuiCol_Header] = rgb(0, 82, 160, 0.75f); c[ImGuiCol_HeaderHovered] = rgb(60, 60, 60); c[ImGuiCol_HeaderActive] = rgb(0, 112, 224);
  c[ImGuiCol_Separator] = rgb(14, 14, 14); c[ImGuiCol_SeparatorHovered] = rgb(0, 112, 224); c[ImGuiCol_SeparatorActive] = rgb(0, 112, 224);
  c[ImGuiCol_ResizeGrip] = rgb(0, 0, 0, 0); c[ImGuiCol_ResizeGripHovered] = rgb(0, 112, 224); c[ImGuiCol_ResizeGripActive] = rgb(0, 112, 224);
  c[ImGuiCol_Tab] = rgb(21, 21, 21); c[ImGuiCol_TabHovered] = rgb(56, 56, 56); c[ImGuiCol_TabSelected] = rgb(36, 36, 36); c[ImGuiCol_TabSelectedOverline] = rgb(0, 112, 224);
  c[ImGuiCol_TabDimmed] = rgb(21, 21, 21); c[ImGuiCol_TabDimmedSelected] = rgb(36, 36, 36); c[ImGuiCol_TabDimmedSelectedOverline] = rgb(60, 60, 60);
  c[ImGuiCol_DockingPreview] = rgb(0, 112, 224, 0.6f); c[ImGuiCol_DockingEmptyBg] = rgb(16, 16, 16);
  c[ImGuiCol_TextSelectedBg] = rgb(0, 112, 224, 0.45f); c[ImGuiCol_NavHighlight] = rgb(0, 112, 224); c[ImGuiCol_ModalWindowDimBg] = rgb(0, 0, 0, 0.55f);
  c[ImGuiCol_TableHeaderBg] = rgb(26, 26, 26); c[ImGuiCol_TableRowBgAlt] = rgb(255, 255, 255, 0.03f);
  s.ScaleAllSizes(scale);
}
void loadFonts(const std::string& assets, float scale) {
  ImGuiIO& io = ImGui::GetIO(); io.Fonts->Clear();
  std::string f = assets + "/Roboto-Medium.ttf";
  static const ImWchar ranges[] = {0x0020, 0x00FF, 0x2010, 0x205E, 0x2190, 0x21FF, 0x25A0, 0x25FF, 0};
  if (std::filesystem::exists(f)) {
    ImFontConfig c; c.OversampleH = 2;
    io.Fonts->AddFontFromFileTTF(f.c_str(), 15.f * scale, &c, ranges);
    ImFontConfig b; b.OversampleH = 2; io.Fonts->AddFontFromFileTTF(f.c_str(), 72.f, &b, ranges);   // 3D text and HUD (scaled down when drawn)
  } else { io.Fonts->AddFontDefault(); ImFontConfig b; b.SizePixels = 39; io.Fonts->AddFontDefault(&b); }
  std::string mono = assets + "/Cousine-Regular.ttf";   // code editor (monospace)
  if (std::filesystem::exists(mono)) { ImFontConfig m; m.OversampleH = 2; io.Fonts->AddFontFromFileTTF(mono.c_str(), 15.f * scale, &m, ranges); }
  else { ImFontConfig m; m.SizePixels = 13.f * scale; io.Fonts->AddFontDefault(&m); }
}
}
