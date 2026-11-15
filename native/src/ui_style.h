#pragma once
#include <string>
// Look of the editor (dark, Unreal-like) and fonts. Font 0 = UI, font 1 = big (3D text, HUD), font 2 = monospace-ish for code.
namespace pge {
void applyEditorStyle(float scale = 1.0f);
void loadFonts(const std::string& assetsDir, float scale = 1.0f);
}
