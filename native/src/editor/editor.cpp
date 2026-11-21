#include "editor.h"
#include "platform.h"
#include "gl.h"
#include "log.h"
#include "script.h"
#include <TextEditor.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>
#include <ImGuizmo.h>
#include <nlohmann/json.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <set>

namespace fs = std::filesystem;
using json = nlohmann::json;
namespace pge {

Editor::Editor(AppWindow& w) : win(w) {}
Editor::~Editor() { if (game) stopPlay(); R.freeTarget(vpRT); R.shutdown(); }

bool Editor::init() {
  R.init(); loadPrefs();
  std::error_code ec; fs::create_directories(fs::u8path(projectsDir()), ec);
  logMsg(std::string("Premium Engine ") + PGE_VERSION + " - OpenGL " + (const char*)glGetString(GL_VERSION));
  lastTick_ = SDL_GetPerformanceCounter(); init_ = true; return true;
}

/* ------------------------------------------------------------------ project */
void Editor::setProject(Project&& p) {
  if (game) stopPlay();
  P = std::move(p); hasProject = true; level = P.level(P.startLevel) ? P.startLevel : P.levels[0].first; sel.clear();
  undo_.clear(); redo_.clear(); stable_ = toJson(P, -1); pending_ = false; dirty = false; tabs.clear(); materialOpen.clear(); wScripts = wMaterial = false;
  frameAll(); SDL_SetWindowTitle(win.win, (P.name + " - Premium Engine").c_str());
  addRecent(P.path); lastAutosave_ = SDL_GetTicks() / 1000.0;
}
void Editor::newProject(const std::string& templ, const std::string& name, std::string* err) {
  std::string n = sanitizeName(name); fs::path dir = fs::u8path(projectsDir()) / fs::u8path(n), file = dir / fs::u8path(n + ".pge.json");
  if (fs::exists(file)) { if (err) *err = "A project called " + n + " already exists."; return; }
  std::error_code ec; fs::create_directories(dir, ec);
  Project p = makeTemplate(templ, name); std::string e;
  if (!saveProjectFile(p, file.u8string(), &e)) { if (err) *err = e; return; }
  p.path = file.u8string(); setProject(std::move(p)); saveThumbnail(); logMsg("Created project " + P.name + " (" + templ + ") at " + P.path, LogKind::Ok);
}
bool Editor::openProjectFile(const std::string& file) {
  Project p; std::string err;
  if (!loadProjectFile(file, p, &err)) { logMsg("error: could not open " + file + ": " + err, LogKind::Error); notify("Could not open the project: " + err); return false; }
  setProject(std::move(p)); logMsg("Opened " + file, LogKind::Ok); return true;
}
void Editor::closeProject() { if (game) stopPlay(); hasProject = false; P = Project(); sel.clear(); tabs.clear(); SDL_SetWindowTitle(win.win, "Premium Engine"); }
bool Editor::save() {
  if (!hasProject) return false;
  for (auto& t : tabs) { auto it = P.scripts.find(t.id); std::string txt = t.ed->GetText(); if (it != P.scripts.end() && it->second.code != txt) { it->second.code = txt; t.savedText = txt; touch(); } }
  if (pending_) commit();
  P.updated = nowMs(); std::string err;
  if (P.path.empty()) P.path = (fs::u8path(projectsDir()) / fs::u8path(sanitizeName(P.name)) / fs::u8path(sanitizeName(P.name) + ".pge.json")).u8string();
  std::error_code ec; fs::create_directories(fs::u8path(P.path).parent_path(), ec);
  if (!saveProjectFile(P, P.path, &err)) { logMsg("error: save failed: " + err, LogKind::Error); notify("Save failed: " + err); return false; }
  dirty = false; saveThumbnail(); logMsg("Saved " + P.path, LogKind::Ok); notify("Saved"); lastAutosave_ = SDL_GetTicks() / 1000.0; return true;
}
void Editor::saveThumbnail() {
  if (!hasProject || P.path.empty()) return;
  static RenderTarget rt; const int w = 480, h = 270; R.resizeTarget(rt, w, h);
  bool icons = showIcons, grid = showGrid; std::string s = sel; showIcons = false; showGrid = false; sel.clear();
  Frame f; Camera c; buildFrame(f, c, (float)w / h); showIcons = icons; showGrid = grid; sel = s;
  R.render(f, c, w, h, &rt);
  fs::path dir = fs::u8path(P.path).parent_path() / "Saved"; std::error_code ec; fs::create_directories(dir, ec);
  readPixelsBMP((dir / "thumbnail.bmp").u8string(), rt.fbo, w, h);
}
Level& Editor::L() { Level* l = P.level(level); if (!l) { level = P.levels[0].first; l = &P.levels[0].second; } return *l; }
Actor* Editor::actor(const std::string& id) { if (id.empty() || !hasProject) return nullptr; for (auto& a : L().actors) if (a.id == id) return &a; return nullptr; }
std::string Editor::uniqueName(const std::string& base) {
  auto taken = [&](const std::string& n) { for (auto& a : L().actors) if (a.name == n) return true; return false; };
  if (!taken(base)) return base; std::string b = base; while (!b.empty() && std::isdigit((unsigned char)b.back())) b.pop_back(); if (!b.empty() && b.back() == '_') b.pop_back();
  for (int i = 2;; i++) { std::string n = b + "_" + std::to_string(i); if (!taken(n)) return n; }
}

/* ------------------------------------------------------------------ undo */
void Editor::touch() { pending_ = true; dirty = true; }
void Editor::commit() { undo_.push_back(stable_); while (undo_.size() > 200) undo_.pop_front(); redo_.clear(); stable_ = toJson(P, -1); pending_ = false; }
static void restoreInto(Project& P, const std::string& j) { std::string path = P.path; Project q; if (fromJson(j, q)) { P = std::move(q); P.path = path; } }
void Editor::undo() {
  if (pending_) commit(); if (undo_.empty()) return;
  redo_.push_back(stable_); stable_ = undo_.back(); undo_.pop_back(); restoreInto(P, stable_); if (!P.level(level)) level = P.levels[0].first; if (!actor(sel)) sel.clear(); dirty = true;
}
void Editor::redo() {
  if (redo_.empty()) return; undo_.push_back(stable_); stable_ = redo_.back(); redo_.pop_back(); restoreInto(P, stable_); if (!P.level(level)) level = P.levels[0].first; if (!actor(sel)) sel.clear(); dirty = true;
}

/* ------------------------------------------------------------------ actors */
glm::mat4 Editor::worldOf(const Actor& a) {
  glm::mat4 m = trsMatrix(a.p, glm::radians(a.r), a.s); int guard = 0;
  for (Actor* p = actor(a.parent); p && guard < 64; p = actor(p->parent), guard++) m = trsMatrix(p->p, glm::radians(p->r), p->s) * m;
  return m;
}
glm::vec3 Editor::dropPoint(float mx, float my) {
  // ray from the camera through the mouse onto the ground plane (y = 0), else 6 m in front
  Frame f; Camera c; buildFrame(f, c, vpW / std::max(vpH, 1.f));
  float nx = (mx - vpX) / vpW * 2 - 1, ny = 1 - (my - vpY) / vpH * 2; glm::mat4 inv = glm::inverse(c.proj * c.view);
  glm::vec4 a = inv * glm::vec4(nx, ny, -1, 1), b = inv * glm::vec4(nx, ny, 1, 1); glm::vec3 o = glm::vec3(a) / a.w, d = glm::normalize(glm::vec3(b) / b.w - o);
  if (view == View::Perspective && d.y < -0.01f) { float t = -o.y / d.y; if (t > 0 && t < 400) return o + d * t; }
  if (view != View::Perspective) { glm::vec3 p = o; if (view == View::Top) p.y = 0; else if (view == View::Front) p.z = 0; else p.x = 0; return p; }
  return o + d * 6.f;
}
Actor& Editor::addActor(const std::string& type, const std::string& mesh, const glm::vec3* at) {
  Actor a = newActor(type, type == "StaticMesh" ? mesh : actorTypeLabel(type)); if (type == "StaticMesh") a.mesh = mesh.empty() ? "Cube" : mesh;
  a.name = uniqueName(type == "StaticMesh" ? a.mesh : type);
  if (at) a.p = *at;
  else { glm::vec3 f(-std::sin(camYaw) * std::cos(camPitch), std::sin(camPitch), -std::cos(camYaw) * std::cos(camPitch)); a.p = view == View::Perspective ? camPos + f * 6.f : orthoCenter; }
  if (type == "StaticMesh" && a.mesh != "Plane") a.p.y = std::max(a.p.y, 0.5f * a.s.y);
  if (type == "DirectionalLight") a.r = {-50, 35, 0};
  if (snapT) a.p = glm::round(a.p / snapTv) * snapTv;
  L().actors.push_back(a); sel = a.id; touch(); logMsg("Placed " + a.name);
  return L().actors.back();
}
void Editor::deleteSelected() {
  Actor* a = actor(sel); if (!a) return; std::string name = a->name, id = sel;
  std::function<void(const std::string&)> del = [&](const std::string& x) {
    std::vector<std::string> kids; for (auto& c : L().actors) if (c.parent == x) kids.push_back(c.id);
    for (auto& k : kids) del(k);
    auto& v = L().actors; v.erase(std::remove_if(v.begin(), v.end(), [&](const Actor& q) { return q.id == x; }), v.end()); };
  del(id); sel.clear(); touch(); logMsg("Deleted " + name);
}
static json actorTreeJson(Editor& E, const Actor& root) {
  json arr = json::array(); std::function<void(const Actor&)> add = [&](const Actor& a) {
    Level tmp; tmp.actors.push_back(a); json one = json::parse(levelToJson(tmp))["actors"][0]; arr.push_back(one);
    for (auto& c : E.L().actors) if (c.parent == a.id) add(c); };
  add(root); return arr;
}
static void pasteTree(Editor& E, const json& arr, const glm::vec3& offset) {
  Level tmp; try { tmp.actors.clear(); json lj = {{"actors", arr}}; levelFromJson(lj.dump(), tmp); } catch (...) { return; }
  if (tmp.actors.empty()) return; std::map<std::string, std::string> ids;
  for (auto& a : tmp.actors) ids[a.id] = newId("a");
  std::string first;
  for (size_t i = 0; i < tmp.actors.size(); i++) { Actor a = tmp.actors[i]; a.id = ids[a.id];
    if (!a.parent.empty() && ids.count(a.parent)) a.parent = ids[a.parent]; else if (i == 0) { a.parent = E.actor(a.parent) ? a.parent : ""; a.p += offset; }
    a.name = E.uniqueName(a.name); if (i == 0) first = a.id; E.L().actors.push_back(a); }
  E.sel = first; E.touch();
}
void Editor::duplicateSelected() { Actor* a = actor(sel); if (!a) return; json t = actorTreeJson(*this, *a); pasteTree(*this, t, glm::vec3(snapT ? std::max(snapTv, 0.5f) : 0.5f, 0, 0)); logMsg("Duplicated"); }
void Editor::copySelected() { Actor* a = actor(sel); if (!a) return; clip_ = actorTreeJson(*this, *a).dump(); SDL_SetClipboardText(("PGE_ACTORS:" + clip_).c_str()); }
void Editor::paste() {
  std::string c = clip_; if (char* t = SDL_GetClipboardText()) { std::string s = t; SDL_free(t); if (s.rfind("PGE_ACTORS:", 0) == 0) c = s.substr(11); }
  if (c.empty()) return; try { pasteTree(*this, json::parse(c), glm::vec3(0.5f, 0, 0.5f)); logMsg("Pasted"); } catch (...) {}
}
void Editor::focusSelected() {
  Actor* a = actor(sel); if (!a) return; glm::mat4 W = worldOf(*a); glm::vec3 c(W[3]);
  float r = std::max({glm::length(glm::vec3(W[0])), glm::length(glm::vec3(W[1])), glm::length(glm::vec3(W[2])), 1.f});
  glm::vec3 f(-std::sin(camYaw) * std::cos(camPitch), std::sin(camPitch), -std::cos(camYaw) * std::cos(camPitch)); camPos = c - f * (r * 2.2f + 2.f); orthoCenter = c;
}
void Editor::frameAll() {
  if (!hasProject) return; glm::vec3 mn(1e9f), mx(-1e9f); int n = 0;
  for (auto& a : L().actors) { if (a.type == "DirectionalLight") continue; glm::vec3 p(worldOf(a)[3]); if (a.type == "StaticMesh" && glm::length(a.s) > 40) continue; mn = glm::min(mn, p); mx = glm::max(mx, p); n++; }
  if (!n) { mn = glm::vec3(-5); mx = glm::vec3(5); }
  glm::vec3 c = (mn + mx) * 0.5f; float r = std::max(glm::length(mx - mn) * 0.5f, 4.f);
  camYaw = 0.6f; camPitch = -0.45f; glm::vec3 f(-std::sin(camYaw) * std::cos(camPitch), std::sin(camPitch), -std::cos(camYaw) * std::cos(camPitch));
  camPos = c - f * (r * 1.6f + 3.f); orthoCenter = c; orthoZoom = r * 1.3f;
}
void Editor::snapToFloor() {
  Actor* a = actor(sel); if (!a) return; glm::mat4 W = worldOf(*a);
  float bottom = 0; if (a->type == "StaticMesh") { MeshData m = makePrimitive(a->mesh); float mn = 1e9f; for (auto& v : m.v) mn = std::min(mn, (W * glm::vec4(v.p, 1)).y); bottom = mn - W[3].y; }
  // highest surface below: other meshes' tops under the actor's x/z, else y = 0
  glm::vec3 c(W[3]); float floorY = 0;
  for (auto& o : L().actors) { if (o.id == a->id || o.type != "StaticMesh") continue; glm::mat4 OW = worldOf(o); MeshData m = makePrimitive(o.mesh); glm::vec3 mn(1e9f), mx(-1e9f);
    for (auto& v : m.v) { glm::vec3 w = OW * glm::vec4(v.p, 1); mn = glm::min(mn, w); mx = glm::max(mx, w); }
    if (c.x >= mn.x && c.x <= mx.x && c.z >= mn.z && c.z <= mx.z && mx.y <= c.y + bottom + 0.01f && mx.y > floorY) floorY = mx.y; }
  float dy = floorY - (c.y + bottom); a->p.y += dy; touch();
}
void Editor::attach(const std::string& child, const std::string& parent) {
  Actor* c = actor(child); if (!c || child == parent) return;
  for (Actor* p = actor(parent); p; p = actor(p->parent)) if (p->id == child) { notify("Can't attach an actor to its own child"); return; }
  glm::mat4 W = worldOf(*c), PW = parent.empty() ? glm::mat4(1) : worldOf(*actor(parent)); glm::mat4 local = glm::inverse(PW) * W;
  glm::vec3 p(local[3]), s(glm::length(glm::vec3(local[0])), glm::length(glm::vec3(local[1])), glm::length(glm::vec3(local[2])));
  glm::mat3 R(glm::vec3(local[0]) / s.x, glm::vec3(local[1]) / s.y, glm::vec3(local[2]) / s.z);
  c->parent = parent; c->p = p; c->r = glm::degrees(eulerYXZ(glm::quat_cast(R))); c->s = s; touch();
  logMsg(parent.empty() ? "Detached " + c->name : "Attached " + c->name + " to " + actor(parent)->name);
}

/* ------------------------------------------------------------------ levels, scripts, materials */
void Editor::openLevel(const std::string& n) { if (!P.level(n)) return; if (playing) stopPlay(); level = n; sel.clear(); frameAll(); logMsg("Opened level " + n); }
void Editor::newLevel(const std::string& name, bool templ) {
  std::string n = sanitizeName(name); if (P.level(n)) { notify("A level called " + n + " already exists"); return; }
  Level l; if (templ) { Project t = makeTemplate("Blank", "x"); l = t.levels[0].second; } else { Actor s = newActor("DirectionalLight", "Sun"); s.r = {-50, 35, 0}; s.p = {0, 10, 0}; l.actors.push_back(s); Actor ps = newActor("PlayerStart", "PlayerStart"); ps.p = {0, 1, 0}; l.actors.push_back(ps); }
  P.levels.push_back({n, l}); touch(); openLevel(n);
}
std::string Editor::newScript(const std::string& name) {
  std::string id = sanitizeName(name.rfind("SC_", 0) == 0 ? name : "SC_" + name); std::string b = id; for (int i = 2; P.scripts.count(id); i++) id = b + "_" + std::to_string(i);
  P.scripts[id] = {id, SCRIPT_TEMPLATE}; touch(); logMsg("Created script " + id); return id;
}
std::string Editor::newMaterial(const std::string& name) {
  std::string id = sanitizeName(name.rfind("M_", 0) == 0 ? name : "M_" + name); std::string b = id; for (int i = 2; P.materials.count(id); i++) id = b + "_" + std::to_string(i);
  Material m; m.name = id; P.materials[id] = m; touch(); logMsg("Created material " + id); return id;
}
void Editor::openScript(const std::string& id) {
  if (!P.scripts.count(id)) return; wScripts = true;
  for (auto& t : tabs) if (t.id == id) { t.focus = true; return; }
  ScriptTab t; t.id = id; t.ed.reset(new TextEditor()); t.ed->SetLanguageDefinition(TextEditor::LanguageDefinitionId::JavaScript); t.ed->SetPalette(TextEditor::PaletteId::Dark);
  t.ed->SetTabSize(2); t.ed->SetShowWhitespacesEnabled(false); t.ed->SetText(P.scripts[id].code); t.savedText = t.ed->GetText(); t.focus = true; tabs.push_back(std::move(t));
}
void Editor::compileScripts(bool quiet) {
  for (auto& t : tabs) { auto it = P.scripts.find(t.id); if (it == P.scripts.end()) continue; std::string txt = t.ed->GetText(); if (it->second.code != txt) { it->second.code = txt; touch(); } t.savedText = txt; }
  int bad = 0;
  for (auto& [id, s] : P.scripts) {
    std::string e = ScriptHost::check(s.code);
    for (auto& t : tabs) if (t.id == id) { t.error = e; t.errorLine = 0; auto p = e.find("(line "); if (p != std::string::npos) t.errorLine = std::atoi(e.c_str() + p + 6); }
    if (!e.empty()) { bad++; logMsg("error: " + s.name + ": " + e, LogKind::Error); }
  }
  if (!quiet || bad) logMsg(bad ? "Compile: " + std::to_string(bad) + " script(s) have errors" : "Compile: " + std::to_string(P.scripts.size()) + " script(s) OK", bad ? LogKind::Error : LogKind::Ok);
  if (bad) { wLog = true; focusLog = true; }
}
void Editor::runMapCheck() {
  mapCheck.clear(); int errs = 0, warns = 0;
  for (auto& [ln, lv] : P.levels) {
    bool ps = false; std::set<std::string> ids; for (auto& a : lv.actors) ids.insert(a.id);
    for (auto& a : lv.actors) {
      if (a.type == "PlayerStart") ps = true;
      if (a.type == "StaticMesh" && !P.materials.count(a.material)) { mapCheck.push_back("warning: " + ln + ": " + a.name + " uses a missing material " + a.material); warns++; }
      if (!a.script.empty() && !P.scripts.count(a.script)) { mapCheck.push_back("error: " + ln + ": " + a.name + " uses a missing script " + a.script); errs++; }
      if (!a.parent.empty() && !ids.count(a.parent)) { mapCheck.push_back("warning: " + ln + ": " + a.name + " is attached to an actor that doesn't exist"); warns++; }
      if (a.type == "StaticMesh" && a.physics.simulate && a.p.y < lv.world.killZ) { mapCheck.push_back("warning: " + ln + ": " + a.name + " starts below Kill Z"); warns++; }
    }
    if (!ps && lv.world.gameMode != "Spectator") { mapCheck.push_back("warning: " + ln + ": no Player Start, the player will start at (0, 2, 5)"); warns++; }
  }
  for (auto& [id, s] : P.scripts) { std::string e = ScriptHost::check(s.code); if (!e.empty()) { mapCheck.push_back("error: script " + s.name + ": " + e); errs++; } }
  mapCheck.insert(mapCheck.begin(), "Map check: " + std::to_string(errs) + " error(s), " + std::to_string(warns) + " warning(s)");
  for (auto& m : mapCheck) logMsg(m); wMapCheck = true;
}
// the console version has a size limit (the whole game folder); the PC version has none
static const uintmax_t CONSOLE_LIMIT_MB = 2048;
void Editor::packageProject(bool console) {
  compileScripts(true); runMapCheck(); wMapCheck = false;
  packageOk = false; packageDir.clear();
  for (auto& m : mapCheck) if (m.rfind("error", 0) == 0) { packageResult = "Packaging stopped: fix the errors in the Output Log first.\n\n" + m; wPackage = true; wLog = true; return; }
  std::string name = sanitizeName(P.name); fs::path out = fs::u8path(buildsDir()) / fs::u8path(name) / (console ? "Console" : "Windows");
  fs::path player = fs::u8path(exeDir()) / fs::u8path("PremiumEnginePlayer" + exeSuffix());
  std::error_code ec;
  if (!fs::exists(player)) { packageResult = "The game player program is missing next to the editor:\n" + player.u8string(); wPackage = true; return; }
  fs::remove_all(out, ec); fs::create_directories(out, ec);
  fs::path exe = out / fs::u8path(name + exeSuffix());
  fs::copy_file(player, exe, fs::copy_options::overwrite_existing, ec);
  if (ec) { packageResult = "Could not copy the game program: " + ec.message(); wPackage = true; return; }
  fs::copy(fs::u8path(exeDir()) / "assets", out / "assets", fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
  // the game's own files next to its project (textures, sounds, fonts, models, data): the packaged game finds them the same way
  int own = 0;
  if (!P.path.empty()) { fs::path root = fs::u8path(P.path).parent_path(), builds = fs::u8path(buildsDir());
    static const std::set<std::string> media = {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".wav", ".ogg", ".mp3", ".flac", ".ttf", ".otf", ".glb", ".gltf", ".bin", ".json", ".txt", ".csv"};
    static const std::set<std::string> skipDirs = {"Builds", "Saved", "dist", "reference", "node_modules", "Intermediate"};
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (ec) break; const fs::path& f = it->path(); std::string nm = f.filename().u8string();
      if (it->is_directory(ec)) { if (skipDirs.count(nm) || (!nm.empty() && nm[0] == '.') || f == builds) it.disable_recursion_pending(); continue; }
      std::string ext = f.extension().u8string(); for (char& ch : ext) ch = (char)std::tolower((unsigned char)ch);
      if (!media.count(ext) || nm.size() > 9 && nm.substr(nm.size() - 9) == ".pge.json" || it->file_size(ec) > (512ull << 20)) continue;
      fs::path rel = fs::relative(f, root, ec); if (ec || rel.empty() || rel.u8string().rfind("..", 0) == 0) continue;
      fs::create_directories((out / rel).parent_path(), ec); fs::copy_file(f, out / rel, fs::copy_options::overwrite_existing, ec); if (!ec) own++; }
    if (own) logMsg("Package: " + std::to_string(own) + " file(s) of the game copied (textures, sounds, ...)"); }
  Project q = P; q.path.clear(); std::string err;
  if (!saveProjectFile(q, (out / "game.pge.json").u8string(), &err)) { packageResult = "Could not write the game data: " + err; wPackage = true; return; }
#ifndef _WIN32
  fs::permissions(exe, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
#endif
  if (console) {   // full screen, controller first, the console's picture settings
    std::ofstream cf(out / "console.json"); cf << json{{"console", true}, {"limitMB", CONSOLE_LIMIT_MB}, {"quality", {{"msaa", 2}, {"ssao", true}, {"bloom", true}, {"shadowDistance", 120}}}}.dump(2); }
  uintmax_t size = 0; for (auto& e : fs::recursive_directory_iterator(out, ec)) if (e.is_regular_file()) size += e.file_size();
  char mb[64]; std::snprintf(mb, sizeof mb, "%.1f MB", size / 1048576.0);
  if (console && size > CONSOLE_LIMIT_MB * 1048576ull) {
    fs::remove_all(out, ec); char lim[96]; std::snprintf(lim, sizeof lim, "%.1f MB of %llu MB", size / 1048576.0, (unsigned long long)CONSOLE_LIMIT_MB);
    packageResult = std::string("The console version is too big: ") + lim + ".\n\nMake the game's files smaller (pictures, sounds, music) and package again."; wPackage = true;
    logMsg(std::string("error: Package (Console): too big, ") + lim, LogKind::Error); return; }
  packageOk = true; packageDir = out.u8string();
  if (console) { char lim[96]; std::snprintf(lim, sizeof lim, "%s of %llu MB", mb, (unsigned long long)CONSOLE_LIMIT_MB); std::snprintf(mb, sizeof mb, "%s", lim); }
  packageResult = "Packaged " + P.name + (console ? " for the console (" : " for Windows (") + mb + ").\n\n" + exe.u8string() +
    (console ? "\n\nThe console version opens full screen, is played with a controller and has the on-screen keyboard. Upload it next to the PC version on Premium Games."
             : "\n\nThe folder is the whole game: zip it and share it, or upload it to Premium Games.");
  logMsg("Package: " + exe.u8string(), LogKind::Ok); wPackage = true;
}

/* ------------------------------------------------------------------ play */
void Editor::play() {
  if (!hasProject) return; if (playing) stopPlay();
  compileScripts(true);
  logMsg("PIE: Play in editor - " + level + " (" + L().world.gameMode + ")", LogKind::Ok);
#ifdef _WIN32
  _putenv_s("PREMIUM_CONSOLE", consolePreview ? "1" : "0");
#else
  setenv("PREMIUM_CONSOLE", consolePreview ? "1" : "0", 1);
#endif
  game.reset(new Game(P, level)); game->setConsole(consolePreview); if (!game->start()) { game.reset(); return; }
  playing = true; mouseFree = !game->wantsMouseLock(); ImGui::SetWindowFocus("Viewport");
  if (game->wantsMouseLock()) SDL_SetRelativeMouseMode(SDL_TRUE);
}
void Editor::stopPlay() {
  if (!game) return; SDL_SetRelativeMouseMode(SDL_FALSE); game.reset(); playing = false; logMsg("PIE: stopped");
}
void Editor::playStandalone() {
  compileScripts(true); fs::path dir = fs::u8path(documentsDir()) / "Saved"; std::error_code ec; fs::create_directories(dir, ec);
  Project q = P; q.startLevel = level; fs::path f = dir / "standalone.pge.json"; std::string err;
  if (!saveProjectFile(q, f.u8string(), &err)) { notify(err); return; }
  fs::path player = fs::u8path(exeDir()) / fs::u8path("PremiumEnginePlayer" + exeSuffix());
#ifdef _WIN32
  _putenv_s("PREMIUM_CONSOLE", consolePreview ? "1" : "0");
#else
  setenv("PREMIUM_CONSOLE", consolePreview ? "1" : "0", 1);
#endif
  if (!launchProgram(player.u8string(), {"--game", f.u8string(), "--windowed"}, &err)) { logMsg("error: " + err, LogKind::Error); notify(err); }
  else logMsg("Started " + P.name + " in a new window");
}

/* ------------------------------------------------------------------ prefs */
void Editor::loadPrefs() {
  std::ifstream i(fs::u8path(documentsDir()) / "editor-settings.json"); if (!i) return;
  try { json j; i >> j; prefs.autosave = j.value("autosave", true); prefs.autosaveMinutes = j.value("autosaveMinutes", 2); prefs.invertY = j.value("invertY", false);
    prefs.camSpeed = j.value("camSpeed", 4.f); prefs.showFps = j.value("showFps", false); prefs.uiScale = j.value("uiScale", 1.f);
    if (j.contains("recent")) for (auto& r : j["recent"]) recent.push_back(r); } catch (...) {}
  showStats = prefs.showFps; ImGui::GetIO().FontGlobalScale = prefs.uiScale;
}
void Editor::savePrefs() {
  json j = {{"autosave", prefs.autosave}, {"autosaveMinutes", prefs.autosaveMinutes}, {"invertY", prefs.invertY}, {"camSpeed", prefs.camSpeed}, {"showFps", prefs.showFps}, {"uiScale", prefs.uiScale}, {"recent", recent}};
  std::error_code ec; fs::create_directories(fs::u8path(documentsDir()), ec); std::ofstream o(fs::u8path(documentsDir()) / "editor-settings.json"); o << j.dump(1);
}
void Editor::addRecent(const std::string& f) {
  if (f.empty()) return; recent.erase(std::remove(recent.begin(), recent.end(), f), recent.end()); recent.insert(recent.begin(), f); if (recent.size() > 12) recent.resize(12); savePrefs();
}
void Editor::notify(const std::string& t) { toast = t; toastUntil = SDL_GetTicks() / 1000.0 + 2.5; }
void Editor::requestQuit() { if (dirty && hasProject) confirmQuit_ = true; else quit_ = true; }

/* ------------------------------------------------------------------ commands */
void Editor::run(const std::string& c) {
  auto is = [&](const char* p) { return c.rfind(p, 0) == 0; };
  if (c == "save" || c == "saveAll") save();
  else if (c == "undo") undo(); else if (c == "redo") redo();
  else if (c == "cut") { copySelected(); deleteSelected(); } else if (c == "copy") copySelected(); else if (c == "paste") paste();
  else if (c == "duplicate") duplicateSelected(); else if (c == "delete") deleteSelected();
  else if (c == "newLevel") { newName = "NewLevel"; wNewLevel = true; } else if (c == "openLevel") wOpenLevel = true;
  else if (c == "newProject" || c == "openProject") { if (dirty) save(); closeProject(); }
  else if (c == "importLevel") wImport = true;
  else if (c == "package") packageProject(); else if (c == "exit") requestQuit();
  else if (c == "prefs") wPrefs = true; else if (c == "projectSettings") wProjectSettings = true;
  else if (c == "worldSettings") { wWorld = true; ImGui::SetWindowFocus("World Settings"); }
  else if (c == "toggleContent") { wContent = !wContent; if (wContent) ImGui::SetWindowFocus("Content Browser"); }
  else if (c == "showLog") { wLog = true; focusLog = true; } else if (c == "resetLayout") resetLayout = true;
  else if (c == "newScript") { newName = "SC_NewScript"; wNewScript = true; } else if (c == "newMaterial") { newName = "M_NewMaterial"; wNewMaterial = true; }
  else if (c == "apiRef") wApi = true; else if (c == "stats") wStats = true; else if (c == "toggleFps") { showStats = !showStats; prefs.showFps = showStats; savePrefs(); }
  else if (c == "buildAll" || c == "mapCheck") runMapCheck(); else if (c == "compile") compileScripts(false);
  else if (c == "deselect") sel.clear();
  else if (c == "selectParent") { Actor* a = actor(sel); if (a && actor(a->parent)) sel = a->parent; }
  else if (c == "selSameMesh" || c == "selSameMat" || c == "selLight") {
    Actor* a = actor(sel); auto& v = L().actors; size_t start = 0; for (size_t i = 0; i < v.size(); i++) if (v[i].id == sel) start = i + 1;
    for (size_t k = 0; k < v.size(); k++) { Actor& b = v[(start + k) % v.size()];
      bool ok = c == "selLight" ? b.isLight() : (a && b.type == "StaticMesh" && (c == "selSameMesh" ? b.mesh == a->mesh : b.material == a->material));
      if (ok && b.id != sel) { sel = b.id; focusSelected(); break; } } }
  else if (c == "snapFloor") snapToFloor();
  else if (c == "resetP" || c == "resetR" || c == "resetS") { if (Actor* a = actor(sel)) { if (c == "resetP") a->p = glm::vec3(0); if (c == "resetR") a->r = glm::vec3(0); if (c == "resetS") a->s = glm::vec3(1); touch(); } }
  else if (c == "detach") { Actor* a = actor(sel); if (a) attach(a->id, ""); }
  else if (c == "hide") { if (Actor* a = actor(sel)) { a->visible = !a->visible; touch(); } }
  else if (c == "showAll") { for (auto& a : L().actors) a.visible = true; touch(); }
  else if (c == "focus") focusSelected(); else if (c == "frameAll") frameAll();
  else if (c == "rename") { if (Actor* a = actor(sel)) { rename = a->name; wRename = true; } }
  else if (c == "docs") wDocs = true; else if (c == "shortcuts") wShortcuts = true; else if (c == "about") wAbout = true;
  else if (c == "play") play(); else if (c == "stop") stopPlay(); else if (c == "playWindow") playStandalone();
  else if (c == "toggleIcons") showIcons = !showIcons;
  else if (is("gm:")) { L().world.gameMode = c.substr(3); touch(); }
  else if (is("view:")) { std::string v = c.substr(5); view = v == "Top" ? View::Top : v == "Front" ? View::Front : v == "Right" ? View::Right : View::Perspective; }
  else if (is("mode:")) viewMode = std::atoi(c.c_str() + 5);
  else if (is("add:")) addActor(c.substr(4)); else if (is("shape:")) addActor("StaticMesh", c.substr(6));
  else if (is("level:")) openLevel(c.substr(6)); else if (is("script:")) openScript(c.substr(7));
  else if (is("material:")) { materialOpen = c.substr(9); wMaterial = true; }
  else if (is("open:")) openProjectFile(c.substr(5));
  else if (is("newProject:")) { auto p = c.find(':', 11); std::string err; newProject(c.substr(11, p - 11), c.substr(p + 1), &err); if (!err.empty()) logMsg("error: " + err, LogKind::Error); }
  else logMsg("warning: unknown command " + c, LogKind::Warn);
}

void Editor::shortcuts() {
  ImGuiIO& io = ImGui::GetIO(); if (io.WantTextInput) return;
  bool ctrl = io.KeyCtrl, shift = io.KeyShift, alt = io.KeyAlt;
  auto K = [&](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
  if (ctrl && K(ImGuiKey_S)) run("save");
  if (ctrl && K(ImGuiKey_Space)) run("toggleContent");
  if (alt && K(ImGuiKey_P)) { if (playing) stopPlay(); else play(); }
  if (K(ImGuiKey_F7)) run("compile");
  if (K(ImGuiKey_F1)) wDocs = true;
  if (playing || codeFocused) return;   // the code editor has its own undo, copy, paste, ...
  if (ctrl && K(ImGuiKey_Z)) run("undo"); if (ctrl && (K(ImGuiKey_Y) || (shift && K(ImGuiKey_Z)))) run("redo");
  if (ctrl && K(ImGuiKey_N)) run("newLevel"); if (ctrl && K(ImGuiKey_O)) run("openLevel");
  if (ctrl && K(ImGuiKey_C)) run("copy"); if (ctrl && K(ImGuiKey_X)) run("cut"); if (ctrl && K(ImGuiKey_V)) run("paste"); if (ctrl && K(ImGuiKey_D)) run("duplicate");
  if (K(ImGuiKey_Delete)) run("delete"); if (K(ImGuiKey_F2)) run("rename"); if (K(ImGuiKey_End)) run("snapFloor");
  if (ctrl && K(ImGuiKey_H)) run("showAll"); else if (!ctrl && K(ImGuiKey_H)) run("hide");
  if (K(ImGuiKey_Escape)) run("deselect"); if (K(ImGuiKey_Home)) run("frameAll");
  if (!flying && !ctrl && !alt) {
    if (K(ImGuiKey_F)) run("focus"); if (K(ImGuiKey_G)) run("toggleIcons");
    if (K(ImGuiKey_W)) gizmoOp = 0; if (K(ImGuiKey_E)) gizmoOp = 1; if (K(ImGuiKey_R)) gizmoOp = 2;
  }
}

/* ------------------------------------------------------------------ events and frame */
static float g_flyDx = 0, g_flyDy = 0;
void Editor::event(const SDL_Event& e) {
  bool captured = playing && game && !mouseFree && SDL_GetRelativeMouseMode();
  if (e.type == SDL_QUIT) { requestQuit(); return; }
  if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE && e.window.windowID == SDL_GetWindowID(win.win)) { requestQuit(); return; }
  if (e.type == SDL_DROPFILE) { std::string f = e.drop.file; SDL_free(e.drop.file); if (f.size() > 9 && f.substr(f.size() - 9) == ".pge.json") { if (hasProject && dirty) save(); openProjectFile(f); } return; }
  if (e.type == SDL_MOUSEMOTION) { g_flyDx += e.motion.xrel; g_flyDy += e.motion.yrel; }
  if (playing && game) {
    // Escape stops playing; when the game's own menus use Escape (ui.escape = true), Shift+Escape does
    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE && (!game->ownsEscape() || (e.key.keysym.mod & KMOD_SHIFT))) { stopPlay(); return; }
    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F1 && (e.key.keysym.mod & KMOD_SHIFT)) { mouseFree = true; SDL_SetRelativeMouseMode(SDL_FALSE); game->releaseInput(); return; }
    if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) { game->releaseInput(); mouseFree = true; SDL_SetRelativeMouseMode(SDL_FALSE); }
    if (captured) { game->event(e); return; }   // the game owns keyboard and mouse
    ImGui_ImplSDL2_ProcessEvent(&e);
    bool typing = ImGui::GetIO().WantTextInput;
    if (vpFocused && !typing && (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP)) game->event(e);
    if (vpFocused && !typing && e.type == SDL_TEXTINPUT && game->uiTyping()) game->event(e);   // typing in the game's text fields
    if (vpHovered && e.type == SDL_MOUSEWHEEL) game->event(e);
    if (e.type == SDL_KEYUP) game->event(e);   // never leave a key stuck
    if (vpHovered && (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION)) {
      if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && game->wantsMouseLock()) { mouseFree = false; SDL_SetRelativeMouseMode(SDL_TRUE); }
      game->event(e);
    }
    return;
  }
  ImGui_ImplSDL2_ProcessEvent(&e);
}

void Editor::frame() {
  Uint64 now = SDL_GetPerformanceCounter(); dt_ = (float)((now - lastTick_) / (double)SDL_GetPerformanceFrequency()); lastTick_ = now; dt_ = std::min(dt_, 0.25f);
  fps_ = fps_ * 0.95f + (dt_ > 0 ? 1.f / dt_ : 0) * 0.05f;
  if (playing && game) { game->update(dt_); if (!game) playing = false; }
  if (playing && game && !mouseFree && SDL_GetRelativeMouseMode() && !game->wantsMouseLock()) { mouseFree = true; SDL_SetRelativeMouseMode(SDL_FALSE); }   // the game opened a menu
  beginFrame(); ImGuizmo::BeginFrame();
  { auto q = std::move(queued_); queued_.clear(); for (auto& c : q) run(c); }
  if (!hasProject) { drawProjectBrowser(); drawDialogs(); }
  else {
    shortcuts(); drawMenuBar(); drawToolbar(); drawStatusBar();
    ImGuiID dock = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_None);
    static bool firstFrame = true;   // no saved layout yet (first start): use the default one
    if (firstFrame) { firstFrame = false; if (!ImGui::FindWindowSettingsByID(ImHashStr("Viewport"))) resetLayout = true; }
    if (resetLayout) { defaultLayout(dock); resetLayout = false; }
    codeFocusedNext_ = false; drawViewport();
    if (wOutliner) drawOutliner(); if (wDetails) drawDetails(); if (wWorld) drawWorldSettings(); if (wContent) drawContent(); if (wLog) drawLog();
    if (wPlaceActors) drawPlaceActors(); if (wScripts) drawScripts(); if (wMaterial) drawMaterial();
    drawDialogs(); codeFocused = codeFocusedNext_;
    if (pending_ && !ImGui::IsAnyItemActive() && !ImGuizmo::IsUsing() && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) commit();
    double t = SDL_GetTicks() / 1000.0;
    if (prefs.autosave && dirty && !playing && t - lastAutosave_ > prefs.autosaveMinutes * 60.0 && !ImGui::IsAnyItemActive()) { logMsg("Autosave"); save(); }
  }
  if (confirmQuit_) { ImGui::OpenPopup("Save changes?"); confirmQuit_ = false; }
  if (ImGui::BeginPopupModal("Save changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("%s has unsaved changes.", P.name.c_str()); ImGui::Spacing();
    if (ImGui::Button("Save and Exit", {140, 0})) { if (save()) quit_ = true; ImGui::CloseCurrentPopup(); } ImGui::SameLine();
    if (ImGui::Button("Exit Without Saving", {160, 0})) { quit_ = true; ImGui::CloseCurrentPopup(); } ImGui::SameLine();
    if (ImGui::Button("Cancel", {90, 0})) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  if (!toast.empty() && SDL_GetTicks() / 1000.0 < toastUntil) {
    ImGuiViewport* v = ImGui::GetMainViewport(); ImVec2 s = ImGui::CalcTextSize(toast.c_str()); ImVec2 p(v->Pos.x + v->Size.x - s.x - 40, v->Pos.y + v->Size.y - s.y - 60);
    ImDrawList* dl = ImGui::GetForegroundDrawList(); dl->AddRectFilled(p, {p.x + s.x + 24, p.y + s.y + 16}, IM_COL32(20, 20, 20, 235), 6); dl->AddRect(p, {p.x + s.x + 24, p.y + s.y + 16}, IM_COL32(0, 112, 224, 255), 6);
    dl->AddText({p.x + 12, p.y + 8}, IM_COL32_WHITE, toast.c_str());
  }
  if (!shotFile.empty()) { ImGui::Render(); int w, h; SDL_GL_GetDrawableSize(win.win, &w, &h); glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, w, h);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData()); saveScreenshotBMP(shotFile, w, h); shotFile.clear(); SDL_GL_SwapWindow(win.win); return; }
  endFrame(win);
}

void Editor::defaultLayout(unsigned dockId) {
  ImGui::DockBuilderRemoveNode(dockId); ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace); ImGui::DockBuilderSetNodeSize(dockId, ImGui::GetMainViewport()->WorkSize);
  ImGuiID main = dockId, right, bottom, rightBottom, left;
  right = ImGui::DockBuilderSplitNode(main, ImGuiDir_Right, 0.22f, nullptr, &main);
  rightBottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.62f, nullptr, &right);
  bottom = ImGui::DockBuilderSplitNode(main, ImGuiDir_Down, 0.27f, nullptr, &main);
  left = ImGui::DockBuilderSplitNode(main, ImGuiDir_Left, 0.16f, nullptr, &main);
  ImGui::DockBuilderDockWindow("Viewport", main); ImGui::DockBuilderDockWindow("Script Editor", main); ImGui::DockBuilderDockWindow("Material Editor", main);
  ImGui::DockBuilderDockWindow("Outliner", right);
  ImGui::DockBuilderDockWindow("Details", rightBottom); ImGui::DockBuilderDockWindow("World Settings", rightBottom);
  ImGui::DockBuilderDockWindow("Content Browser", bottom); ImGui::DockBuilderDockWindow("Output Log", bottom);
  ImGui::DockBuilderDockWindow("Place Actors", left);
  ImGui::DockBuilderFinish(dockId);
  wOutliner = wDetails = wWorld = wContent = wLog = true;
}

/* ------------------------------------------------------------------ viewport */
void Editor::buildFrame(Frame& f, Camera& cam, float aspect) {
  f = Frame(); f.world = L().world; f.fog = L().world.fog; f.viewMode = viewMode; f.grid = showGrid;
  unsigned idx = 0;
  for (auto& a : L().actors) { idx++;
    bool vis = a.visible; for (Actor* p = actor(a.parent); p && vis; p = actor(p->parent)) vis = p->visible;
    if (!vis) continue; addActorVisual(f, P, a, worldOf(a), showIcons, idx, nullptr, nullptr, &R); }
  if (Actor* s = actor(sel)) {   // selection outline
    glm::mat4 W = worldOf(*s); LineItem l; l.color = glm::vec4(1, 0.55f, 0.1f, 1); l.onTop = false;
    if (s->type == "StaticMesh") { l.mesh = &primitive(s->mesh); l.model = W; l.wire = true; f.lines.push_back(l); }
    else if (s->type == "TriggerBox") { l.mesh = &R.wireBox(); l.model = W; l.onTop = true; f.lines.push_back(l); }
  }
  float asp = std::max(aspect, 0.01f);
  if (view == View::Perspective) {
    glm::mat4 cw = glm::translate(glm::mat4(1), camPos) * glm::mat4_cast(quatYXZ({camPitch, camYaw, 0})); cam.view = glm::inverse(cw); cam.pos = camPos;
    cam.proj = glm::perspective(glm::radians(70.f), asp, 0.05f, 4000.f); f.shadowCenter = camPos;
  } else {
    glm::vec3 c = orthoCenter, eye, up(0, 1, 0);
    if (view == View::Top) { eye = c + glm::vec3(0, 500, 0); up = {0, 0, -1}; } else if (view == View::Front) eye = c + glm::vec3(0, 0, 500); else eye = c + glm::vec3(500, 0, 0);
    cam.view = glm::lookAt(eye, c, up); cam.pos = eye; cam.ortho = true; cam.proj = glm::ortho(-orthoZoom * asp, orthoZoom * asp, -orthoZoom, orthoZoom, 0.1f, 2000.f); f.shadowCenter = c;
    if (view != View::Top) f.grid = false;
  }
}

void Editor::cameraInput(float dt) {
  ImGuiIO& io = ImGui::GetIO(); float dx = g_flyDx, dy = g_flyDy; g_flyDx = g_flyDy = 0;
  static const float SPEEDS[8] = {0.6f, 1.5f, 3, 6, 10, 16, 26, 45};
  float speed = SPEEDS[glm::clamp((int)prefs.camSpeed, 1, 8) - 1];
  glm::vec3 f(-std::sin(camYaw) * std::cos(camPitch), std::sin(camPitch), -std::cos(camYaw) * std::cos(camPitch)), r(std::cos(camYaw), 0, -std::sin(camYaw)), u = glm::cross(r, f);
  bool rmb = ImGui::IsMouseDown(ImGuiMouseButton_Right), mmb = ImGui::IsMouseDown(ImGuiMouseButton_Middle), lmb = ImGui::IsMouseDown(ImGuiMouseButton_Left);
  if (vpHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && view == View::Perspective && !io.KeyAlt) { flying = true; SDL_SetRelativeMouseMode(SDL_TRUE); }
  if (flying && !rmb) { flying = false; SDL_SetRelativeMouseMode(SDL_FALSE); }
  float iy = prefs.invertY ? -1.f : 1.f;
  if (flying) {
    camYaw -= dx * 0.0035f; camPitch = glm::clamp(camPitch - dy * 0.0035f * iy, -1.55f, 1.55f);
    glm::vec3 mv(0); auto k = [&](ImGuiKey key) { return ImGui::IsKeyDown(key); };
    if (k(ImGuiKey_W)) mv += f; if (k(ImGuiKey_S)) mv -= f; if (k(ImGuiKey_D)) mv += r; if (k(ImGuiKey_A)) mv -= r; if (k(ImGuiKey_E)) mv.y += 1; if (k(ImGuiKey_Q)) mv.y -= 1;
    if (io.MouseWheel != 0) { prefs.camSpeed = glm::clamp(prefs.camSpeed + (io.MouseWheel > 0 ? 1.f : -1.f), 1.f, 8.f); notify("Camera speed " + std::to_string((int)prefs.camSpeed)); }
    if (glm::dot(mv, mv) > 0) camPos += glm::normalize(mv) * speed * dt * (io.KeyShift ? 3.f : 1.f);
    return;
  }
  if (!vpHovered) return;
  if (view == View::Perspective) {
    if (io.MouseWheel != 0) camPos += f * io.MouseWheel * std::max(1.f, speed * 0.4f);
    if (mmb) { camPos -= r * io.MouseDelta.x * 0.01f * std::max(1.f, speed * 0.3f); camPos += u * io.MouseDelta.y * 0.01f * std::max(1.f, speed * 0.3f); }
    if (io.KeyAlt && lmb) {   // orbit around the point in front / the selection
      glm::vec3 pivot = camPos + f * 10.f; if (Actor* a = actor(sel)) pivot = glm::vec3(worldOf(*a)[3]); float d = glm::length(camPos - pivot);
      camYaw -= io.MouseDelta.x * 0.005f; camPitch = glm::clamp(camPitch - io.MouseDelta.y * 0.005f, -1.55f, 1.55f);
      glm::vec3 nf(-std::sin(camYaw) * std::cos(camPitch), std::sin(camPitch), -std::cos(camYaw) * std::cos(camPitch)); camPos = pivot - nf * d;
    }
    if (io.KeyAlt && rmb) camPos += f * (io.MouseDelta.x + io.MouseDelta.y) * 0.03f;
  } else {
    if (io.MouseWheel != 0) orthoZoom = glm::clamp(orthoZoom * (io.MouseWheel > 0 ? 0.85f : 1.18f), 0.5f, 500.f);
    if (rmb || mmb) { float k = orthoZoom * 2 / std::max(vpH, 1.f);
      glm::vec3 R2 = view == View::Top ? glm::vec3(1, 0, 0) : view == View::Front ? glm::vec3(1, 0, 0) : glm::vec3(0, 0, -1), U2 = view == View::Top ? glm::vec3(0, 0, -1) : glm::vec3(0, 1, 0);
      orthoCenter -= R2 * io.MouseDelta.x * k; orthoCenter += U2 * io.MouseDelta.y * k; }
  }
}
}
