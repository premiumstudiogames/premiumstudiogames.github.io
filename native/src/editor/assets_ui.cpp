// The project's own files in the editor: importing models and pictures (Content Browser > Import, File > Import,
// or dropping files on the window), the Models and Textures folders, picture slots of the Material Editor, a model's
// animations in the Details panel.
#include "editor.h"
#include "platform.h"
#include "log.h"
#include <imgui.h>
#include <imgui_stdlib.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <set>
#include <cstring>
#include <cctype>

namespace pge {
namespace fs = std::filesystem;
using json = nlohmann::json;

static std::string rel(const fs::path& root, const fs::path& f) { std::error_code ec; fs::path r = fs::relative(f, root, ec); return ec ? f.u8string() : r.generic_u8string(); }
static bool endsWith(const std::string& s, const char* e) { size_t n = std::strlen(e); if (s.size() < n) return false; for (size_t i = 0; i < n; i++) if (std::tolower((unsigned char)s[s.size() - n + i]) != e[i]) return false; return true; }
static bool sameFile(const fs::path& a, const fs::path& b) { std::error_code ec; return fs::equivalent(a, b, ec); }

void Editor::scanFiles(bool now) {
  double t = SDL_GetTicks() / 1000.0; if (!now && filesScanned_ >= 0 && t - filesScanned_ < 2.0) return; filesScanned_ = t;
  projModels.clear(); projTextures.clear(); if (P.path.empty()) return;
  fs::path root = fs::u8path(P.path).parent_path(); std::error_code ec;
  static const std::set<std::string> skip = {"Builds", "Saved", "dist", "reference", "node_modules", "Intermediate"};
  int seen = 0;
  for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator() && seen < 20000; it.increment(ec), seen++) {
    if (ec) break; std::string nm = it->path().filename().u8string();
    if (it->is_directory(ec)) { if (skip.count(nm) || (!nm.empty() && nm[0] == '.')) it.disable_recursion_pending(); continue; }
    std::string r = rel(root, it->path()); if (isModelFile(r)) projModels.push_back(r); else if (isImageFile(r)) projTextures.push_back(r); }
  std::sort(projModels.begin(), projModels.end()); std::sort(projTextures.begin(), projTextures.end());
}

// copies files into the project (Models/..., Textures/...); a .gltf brings its .bin and pictures with it. Returns the project paths.
std::vector<std::string> Editor::importFiles(const std::vector<std::string>& files) {
  std::vector<std::string> out; if (!hasProject) return out;
  if (P.path.empty() && !save()) return out;
  fs::path root = fs::u8path(P.path).parent_path(); std::error_code ec;
  for (auto& f : files) {
    fs::path src = fs::u8path(f); std::string name = src.filename().u8string();
    if (!fs::exists(src, ec)) { logMsg("error: import: " + f + " not found", LogKind::Error); continue; }
    bool model = isModelFile(name), image = isImageFile(name);
    if (!model && !image) { logMsg("warning: import: " + name + " is not a model (.glb, .gltf) or a picture (.png, .jpg, .tga, .bmp)", LogKind::Warn); notify("Can't import " + name + ": models are .glb / .gltf, pictures .png / .jpg / .tga / .bmp"); continue; }
    if (fs::u8path(rel(root, src)).is_relative() && rel(root, src).rfind("..", 0) != 0) { out.push_back(rel(root, src)); continue; }   // already in the project
    fs::path destDir = root / (model ? "Models" : "Textures");
    std::vector<std::pair<fs::path, fs::path>> copies;   // from, to
    if (endsWith(name, ".gltf")) {   // its own folder: the .gltf and the files it names, where it names them
      destDir /= src.stem(); copies.push_back({src, destDir / src.filename()});
      try { std::ifstream in(src, std::ios::binary); json j = json::parse(in);
        for (const char* k : {"buffers", "images"}) if (j.contains(k) && j[k].is_array()) for (auto& b : j[k]) if (b.contains("uri") && b["uri"].is_string()) {
          std::string u = b["uri"]; if (u.rfind("data:", 0) == 0) continue; std::string dec; for (size_t i = 0; i < u.size(); i++) { if (u[i] == '%' && i + 2 < u.size()) { dec += (char)std::strtol(u.substr(i + 1, 2).c_str(), nullptr, 16); i += 2; } else dec += u[i]; }
          fs::path rp = fs::u8path(dec); if (rp.is_absolute() || dec.find("..") != std::string::npos) continue; copies.push_back({src.parent_path() / rp, destDir / rp}); } }
      catch (...) { logMsg("warning: import: " + name + " is not valid JSON", LogKind::Warn); } }
    else copies.push_back({src, destDir / src.filename()});
    bool ok = true, again = fs::exists(copies[0].second, ec);
    for (auto& [a, b] : copies) { fs::create_directories(b.parent_path(), ec); if (sameFile(a, b)) continue; fs::copy_file(a, b, fs::copy_options::overwrite_existing, ec);
      if (ec) { logMsg("error: import: could not copy " + a.filename().u8string() + ": " + ec.message(), LogKind::Error); ok = false; ec.clear(); } }
    if (!ok) continue;
    std::string r = rel(root, copies[0].second); out.push_back(r);
    logMsg(std::string(again ? "Reimported " : "Imported ") + r, LogKind::Ok); }
  if (!out.empty()) { reloadChangedAssets(); scanFiles(true); }
  return out;
}

void Editor::importDialog() {
  if (!hasFileDialogs()) { notify("Put the files in the project's folder (Models, Textures): they show in the Content Browser."); return; }
  std::string f = openFileDialog("Import a model or a picture", "Models and pictures", "*.glb;*.gltf;*.png;*.jpg;*.jpeg;*.tga;*.bmp");
  if (f.empty()) return; auto r = importFiles({f});
  if (r.size() == 1) { cbFolder = isModelFile(r[0]) ? "Models" : "Textures"; wContent = true; }
}

// a picture slot of the Material Editor: a thumbnail, the project's pictures to pick from, drop a picture from the Content Browser
bool Editor::textureSlot(const char* id, std::string& file, bool srgb) {
  bool ch = false; ImGui::PushID(id); float h = ImGui::GetFrameHeight() * 2;
  const Texture* t = texture(file, srgb);
  ImVec2 p = ImGui::GetCursorScreenPos(); ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p, {p.x + h, p.y + h}, IM_COL32(20, 20, 20, 255), 3);
  if (t && t->id) dl->AddImage((ImTextureID)(intptr_t)t->id, {p.x + 1, p.y + 1}, {p.x + h - 1, p.y + h - 1});
  else if (!file.empty()) dl->AddText({p.x + 4, p.y + h / 2 - 7}, IM_COL32(255, 90, 90, 255), "?");
  ImGui::Dummy({h, h});
  if (ImGui::BeginDragDropTarget()) { if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("PGE_TEX")) { file.assign((const char*)pl->Data, pl->DataSize); ch = true; } ImGui::EndDragDropTarget(); }
  if (ImGui::IsItemHovered() && t && t->ok) ImGui::SetTooltip("%s\n%d x %d", file.c_str(), t->w, t->h);
  ImGui::SameLine(); ImGui::BeginGroup(); ImGui::SetNextItemWidth(-28);
  if (ImGui::BeginCombo("##f", file.empty() ? "None" : file.c_str(), ImGuiComboFlags_HeightLarge)) {
    if (ImGui::Selectable("None", file.empty())) { file.clear(); ch = true; }
    scanFiles(); for (auto& x : projTextures) if (ImGui::Selectable(x.c_str(), x == file)) { file = x; ch = true; }
    if (hasFileDialogs()) { ImGui::Separator(); if (ImGui::Selectable("Import...")) { std::string f = openFileDialog("Import a picture", "Pictures", "*.png;*.jpg;*.jpeg;*.tga;*.bmp"); if (!f.empty()) { auto r = importFiles({f}); if (!r.empty()) { file = r[0]; ch = true; } } } }
    ImGui::EndCombo(); }
  if (ImGui::BeginDragDropTarget()) { if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("PGE_TEX")) { file.assign((const char*)pl->Data, pl->DataSize); ch = true; } ImGui::EndDragDropTarget(); }
  ImGui::SameLine(); ImGui::BeginDisabled(file.empty()); if (ImGui::Button("x")) { file.clear(); ch = true; } ImGui::EndDisabled();
  if (t && !t->error.empty()) ImGui::TextColored({1, 0.45f, 0.4f, 1}, "%s", t->error.c_str());
  ImGui::EndGroup(); ImGui::PopID(); return ch;
}

// Details: what the model is made of, and the animation that plays from the start
bool Editor::modelDetails(Actor& a) {
  bool ch = false; ModelRef m = model(a.mesh);
  if (!m) return false;
  if (!m->ok()) { ImGui::TextColored({1, 0.45f, 0.4f, 1}, "%s", m->error.c_str()); return false; }
  ImGui::TextDisabled("%d triangles, %d material(s), %d joint(s), %d animation(s)", m->triangles, (int)m->mats.size(), m->joints, (int)m->anims.size());
  if (m->anims.empty()) return false;
  if (ImGui::BeginTable("anim", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) {
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, 110); ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
    auto row = [](const char* l) { ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(l); ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1); };
    row("Animation"); if (ImGui::BeginCombo("##an", a.anim.empty() ? "None" : a.anim.c_str())) {
      if (ImGui::Selectable("None", a.anim.empty())) { a.anim.clear(); ch = true; }
      for (auto& an : m->anims) { char lb[160]; std::snprintf(lb, sizeof lb, "%s  (%.2f s)", an.name.c_str(), an.duration); if (ImGui::Selectable(lb, a.anim == an.name)) { a.anim = an.name; ch = true; } }
      ImGui::EndCombo(); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Plays from the start of the game (and here in the viewport). Scripts and Blueprints change it: Play Animation.");
    if (!a.anim.empty()) { row("Loop"); ch |= ImGui::Checkbox("##al", &a.animLoop); row("Play Rate"); ch |= ImGui::DragFloat("##as", &a.animSpeed, 0.01f, -10, 10, "%.2f"); }
    ImGui::EndTable(); }
  return ch;
}
}
