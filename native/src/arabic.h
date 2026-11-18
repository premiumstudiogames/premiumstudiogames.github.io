#pragma once
// Arabic text for the screen: joined letters (initial / middle / final / alone forms, lam-alef) and right-to-left order,
// so text drawn left to right (Dear ImGui, 3D text) reads correctly. Mixed Arabic / English / numbers works.
#include <string>
#include <vector>

namespace pge {
bool hasRtl(const std::string& utf8);                 // any Arabic letters?
std::string visualText(const std::string& utf8);      // logical (as typed) -> visual (as drawn, left to right); each line on its own
std::vector<std::u32string> visualLines(const std::u32string& logical);   // the same for text already split into lines
std::u32string toU32(const std::string& s);
std::string toUtf8(const std::u32string& s);
}
