// Project Browser and the editor's dialogs (preferences, settings, help, packaging, ...).
#include "editor.h"
#include "platform.h"
#include "log.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <filesystem>
#include <algorithm>
#include <cstring>

namespace fs = std::filesystem;
namespace pge {

static const char* API_TEXT = R"(SCRIPTS
Scripts are JavaScript. Add one to an actor in Details > Script. These functions are called by the engine:

  function onStart() { }          once, when the game starts
  function onUpdate(dt) { }       every frame (dt = seconds since the last frame)
  function onOverlap(other) { }   the player walked into this actor (Trigger Box or mesh)
  function onHit(other) { }       a physics body hit this actor (needs collision)
  function onKey(code) { }        a key was pressed, e.g. 'KeyE', 'Space', 'Digit1'

self  - the actor the script is on
  self.name, self.tags, self.id
  self.position.x / .y / .z      metres (also .set(x, y, z), .add(v), .copy(v))
  self.rotation.x / .y / .z      radians
  self.scale.x / .y / .z
  self.visible = false           hide / show
  self.worldPosition             position in the world (read only)
  self.setColor('#ff8800')       change the colour
  self.setText('Hello')          Text Render actors
  self.addForce(x, y, z)         push a physics actor (Simulate Physics on)
  self.setVelocity(x, y, z)      self.velocity
  self.lookAt(x, y, z)           turn towards a point
  self.distanceTo(other)         metres to another actor or the player
  self.destroy()                 remove from the game

world
  world.time                     seconds since the game started
  world.vars                     an object to keep your own values (score, ...)
  world.player                   .position, .teleport(x, y, z), .launch(x, y, z)
  world.find('Name')             an actor by name (or null)
  world.findAll('tag')           actors with a tag (or that name)
  world.spawn('Cube', { position: [0, 5, 0], scale: [1, 1, 1], color: '#ff0000', physics: true, name: 'Box', script: 'SC_Name' })
  world.destroy(actor)
  world.message('Text', seconds) big message in the middle of the screen
  world.hud('id', 'Text')        a line in the corner (empty text removes it)
  world.restart()                restart the level
  world.setGravity(-9.81)

input
  input.key('w')                 held down?  ('space', 'shift', 'up', 'a'..'z', '0'..'9')
  input.pressed('space')         pressed this frame?
  input.mouse                    .dx, .dy, .left, .right
  input.pressed('MouseLeft')     mouse buttons work like keys: 'MouseLeft', 'MouseRight', 'MouseMiddle'

world.camera                     .position, .forward (the direction the player looks)

BLOCK WORLD (voxels)
The ground is split into 16 x 16 m chunks. Near the ground (the near layer) blocks can be 20, 40, 60, 80 or 100 cm;
above and below it everything is 1 m blocks. Chunks are generated and built on background threads, hidden faces
are removed, corners get ambient occlusion, and the player and physics bodies collide with the blocks directly.
Positions are metres. A block of size s sits on the 20 cm grid; (x, y, z) is its lowest corner.

  voxels.create({ bottom: -32, top: 64, nearLayer: [-6, 14], viewDistance: 6, seed: 2026, threads: 0 })
                                 start a block world (removes the old one). false + voxels.lastError on a mistake
  voxels.enabled                 is there a block world?
  voxels.defineBlock(1, { name: 'Grass', color: '#5a9c3c', solid: true, transparent: false, tile: 0 })
      color: '#rrggbb', [r, g, b] (0..1) or { top, side, bottom }     tile: number or { top, side, bottom } (atlas)
  voxels.setAtlas('textures/blocks.png', 16)   block textures: a PNG of square tiles, 16 per row
  voxels.setGenerator('flat' | 'hills', { ground: 0, top: 1, dirt: 2, stone: 3, amplitude: 9, scale: 0.012, octaves: 4, smooth: true })
                                 built-in ground made of block ids top / dirt / stone
  voxels.setGenerator(function (chunk, noise, params) { ... }, { ... })  your own ground (runs on background threads,
                                 cannot use world / self; only chunk, noise and params)
      chunk.cx, chunk.cz         chunk number;  chunk.x, chunk.z = its corner in metres
      chunk.column(x, z, yBottom, yTop, id)   1 m blocks from yBottom up to yTop (x, z = 0..15 inside the chunk)
      chunk.fill(x0, y0, z0, x1, y1, z1, id)  1 m blocks in a box (max not included)
      chunk.set(x, y, z, id, size)            one block (size 0.2 .. 1, near layer only for small sizes)
      chunk.get(x, y, z)                      block id there (0 = air)
      noise.simplex2(x, z)  noise.simplex3(x, y, z)  noise.fbm(x, z, octaves, lacunarity, gain)  noise.hash(x, y, z)
      (all noise uses the seed from voxels.create, so every thread and every run makes the same world)
  voxels.getBlock(x, y, z)       { id, x, y, z, size } of the block at a point, or null for air
  voxels.setBlock(x, y, z, id, size)   place (replaces blocks it overlaps). false + voxels.lastError if not allowed
  voxels.canPlace(x, y, z, size)
  voxels.removeBlock(x, y, z)    removes the block at a point, returns its id (0 = nothing)
  voxels.raycast(from, dir, max) the first block along a ray, or null:
      { id, x, y, z, size, normal, point, distance, placePosition(size) }   placePosition = corner for a new block on that face
  voxels.ready(x, y, z)          the chunks around a point are built and collide
  voxels.topY(x, z)              top of the highest block (NaN while not loaded)
  voxels.stats                   { chunks, meshed, pendingGen, pendingMesh, triangles, bricks, genMs, meshMs }
  world.voxels                   the same object

  Example - break and build:
    const hit = voxels.raycast(world.camera.position, world.camera.forward, 8);
    if (hit && input.pressed('MouseLeft')) voxels.removeBlock(hit.point.clone().addScaledVector(hit.normal, -0.05));
    if (hit && input.pressed('MouseRight')) voxels.setBlock(hit.placePosition(0.2), 2, 0.2);

log('text', value)               writes to the Output Log
math.clamp(v, a, b)  math.lerp(a, b, t)  math.random(a, b)  math.vec3(x, y, z)
)";

static const char* DOCS_TEXT = R"(PREMIUM ENGINE - QUICK START

1. Create a project from a template in the Project Browser (Blank, First Person, Third Person).
2. Place actors: toolbar + (Quick Add), Actor > Place Actor, Window > Place Actors, or drag shapes from the Content Browser into the viewport.
3. Move them with the gizmo: W move, E rotate, R scale. Hold the right mouse button in the viewport and use W A S D Q E to fly.
4. Change an actor in the Details panel: mesh, material, physics, light, text, script.
5. Materials: Content Browser > Materials. Double-click to edit. Drag a material onto an actor in the viewport to apply it.
6. Scripts: Tools > New Script, or Details > Script > New Script. Press F7 to compile. See Help > Script API Reference.
7. Press Play (Alt+P) to play in the viewport. Esc stops. Shift+F1 frees the mouse.
8. Platforms > Windows packages the game as a Windows program (.exe) in Documents\PremiumEngine\Builds.

Projects are saved in Documents\PremiumEngine\Projects as .pge.json files.
They also open in Premium Engine 0.1 (web) and the other way round.
)";

static const char* SHORTCUTS[][2] = {
  {"Ctrl+S", "Save"}, {"Ctrl+Z / Ctrl+Y", "Undo / Redo"}, {"Ctrl+C / X / V", "Copy / Cut / Paste actors"}, {"Ctrl+D", "Duplicate"}, {"Delete", "Delete"}, {"F2", "Rename"},
  {"W / E / R", "Move / Rotate / Scale"}, {"F", "Focus selected"}, {"Home", "Frame all"}, {"End", "Snap to floor"}, {"H / Ctrl+H", "Hide selected / Show all"}, {"G", "Game view (hide icons)"},
  {"Esc", "Unselect / stop playing"}, {"Right mouse + W A S D Q E", "Fly the camera (wheel = speed, Shift = fast)"}, {"Middle mouse drag", "Pan"}, {"Alt + left mouse", "Orbit"},
  {"Mouse wheel", "Move forward / back"}, {"Alt+P", "Play / Stop"}, {"Shift+F1", "Free the mouse while playing"}, {"F7", "Compile scripts"}, {"Ctrl+Space", "Content Browser"},
  {"Ctrl+N / Ctrl+O", "New level / Open level"}, {"F1", "Documentation"}};

static bool beginDialog(const char* title, bool* open, ImVec2 size) {
  if (!*open) return false;
  ImGuiViewport* v = ImGui::GetMainViewport(); ImGui::SetNextWindowPos({v->Pos.x + v->Size.x / 2, v->Pos.y + v->Size.y / 2}, ImGuiCond_Appearing, {0.5f, 0.5f});
  ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);
  if (!ImGui::Begin(title, open, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse)) { ImGui::End(); return false; }
  return true;
}

void Editor::drawDialogs() {
  float s = win.dpi;
  if (beginDialog("New Level", &wNewLevel, {380 * s, 0})) {
    ImGui::Text("Level name"); ImGui::SetNextItemWidth(-1); if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    bool go = ImGui::InputText("##nl", &newName, ImGuiInputTextFlags_EnterReturnsTrue); static bool templ = false; ImGui::Checkbox("Start with a floor, sky and light", &templ);
    if (ImGui::Button("Create", {120, 0}) || go) { newLevel(newName, templ); wNewLevel = false; } ImGui::SameLine(); if (ImGui::Button("Cancel")) wNewLevel = false;
    ImGui::End(); }
  if (beginDialog("Open Level", &wOpenLevel, {360 * s, 300 * s})) {
    for (auto& [ln, lv] : P.levels) { std::string label = ln + "  (" + std::to_string(lv.actors.size()) + " actors)" + (ln == P.startLevel ? "  - start level" : "");
      if (ImGui::Selectable(label.c_str(), ln == level, ImGuiSelectableFlags_AllowDoubleClick) && ImGui::IsMouseDoubleClicked(0)) { openLevel(ln); wOpenLevel = false; } }
    ImGui::TextDisabled("Double-click to open"); ImGui::End(); }
  if (beginDialog("New Script", &wNewScript, {380 * s, 0})) {
    ImGui::Text("Script name"); ImGui::SetNextItemWidth(-1); if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    bool go = ImGui::InputText("##ns", &newName, ImGuiInputTextFlags_EnterReturnsTrue); Actor* a = actor(sel); static bool assign = true;
    if (a) ImGui::Checkbox(("Add it to " + a->name).c_str(), &assign);
    if (ImGui::Button("Create", {120, 0}) || go) { std::string id = newScript(newName); if (a && assign) { a->script = id; touch(); } openScript(id); wNewScript = false; }
    ImGui::SameLine(); if (ImGui::Button("Cancel")) wNewScript = false; ImGui::End(); }
  if (beginDialog("New Material", &wNewMaterial, {380 * s, 0})) {
    ImGui::Text("Material name"); ImGui::SetNextItemWidth(-1); if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    bool go = ImGui::InputText("##nm", &newName, ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::Button("Create", {120, 0}) || go) { materialOpen = newMaterial(newName); wMaterial = true; Actor* a = actor(sel); if (a && a->type == "StaticMesh") { a->material = materialOpen; touch(); } wNewMaterial = false; }
    ImGui::SameLine(); if (ImGui::Button("Cancel")) wNewMaterial = false; ImGui::End(); }
  if (beginDialog("Rename Actor", &wRename, {340 * s, 0})) {
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere(); ImGui::SetNextItemWidth(-1); bool go = ImGui::InputText("##rn", &rename, ImGuiInputTextFlags_EnterReturnsTrue);
    if ((ImGui::Button("Rename", {120, 0}) || go) && !rename.empty()) { if (Actor* a = actor(sel)) { a->name = rename; touch(); } wRename = false; } ImGui::SameLine(); if (ImGui::Button("Cancel")) wRename = false; ImGui::End(); }
  if (beginDialog("Import Into Level", &wImport, {520 * s, 0})) {
    ImGui::TextWrapped("Adds every actor of a level from another Premium Engine project (.pge.json, also from 0.1 web) to the current level, with its materials and scripts.");
    static std::string path; ImGui::SetNextItemWidth(-90); ImGui::InputTextWithHint("##ip", "C:\\path\\to\\Project.pge.json", &path); ImGui::SameLine();
    if (ImGui::Button("Browse...")) { std::string f = openFileDialog("Import level", "Premium Engine project", "*.pge.json;*.json"); if (!f.empty()) path = f; }
    if (ImGui::Button("Import", {120, 0})) { Project q; std::string err;
      if (!loadProjectFile(path, q, &err)) notify("Could not read: " + err);
      else { const Level* lv = q.level(q.startLevel); int n = 0; std::map<std::string, std::string> ids; for (auto& a : lv->actors) ids[a.id] = newId("a");
        for (auto a : lv->actors) { a.id = ids[a.id]; if (!a.parent.empty()) a.parent = ids.count(a.parent) ? ids[a.parent] : ""; a.name = uniqueName(a.name); L().actors.push_back(a); n++; }
        for (auto& [id, m] : q.materials) if (!P.materials.count(id)) P.materials[id] = m; for (auto& [id, sc] : q.scripts) if (!P.scripts.count(id)) P.scripts[id] = sc;
        touch(); logMsg("Imported " + std::to_string(n) + " actors from " + q.name, LogKind::Ok); wImport = false; } }
    ImGui::SameLine(); if (ImGui::Button("Cancel")) wImport = false; ImGui::End(); }
  if (beginDialog("Editor Preferences", &wPrefs, {440 * s, 0})) {
    bool ch = false;
    ch |= ImGui::Checkbox("Autosave", &prefs.autosave); ImGui::SameLine(); ImGui::SetNextItemWidth(120); ch |= ImGui::SliderInt("minutes", &prefs.autosaveMinutes, 1, 30);
    ch |= ImGui::Checkbox("Invert mouse Y when flying", &prefs.invertY);
    ImGui::SetNextItemWidth(160); ch |= ImGui::SliderFloat("Camera speed", &prefs.camSpeed, 1, 8, "%.0f");
    ImGui::SetNextItemWidth(160); if (ImGui::SliderFloat("UI scale", &prefs.uiScale, 0.75f, 2.0f, "%.2f")) { ImGui::GetIO().FontGlobalScale = prefs.uiScale; ch = true; }
    if (ImGui::Checkbox("Show FPS in the viewport", &prefs.showFps)) { showStats = prefs.showFps; ch = true; }
    if (ch) savePrefs();
    ImGui::Separator(); ImGui::TextDisabled("Projects: %s", projectsDir().c_str()); ImGui::End(); }
  if (beginDialog("Project Settings", &wProjectSettings, {480 * s, 0})) {
    bool ch = false; ImGui::SeparatorText("Description");
    ImGui::Text("Project Name"); ImGui::SetNextItemWidth(-1); ch |= ImGui::InputText("##pn", &P.name);
    ImGui::Text("Description"); ImGui::SetNextItemWidth(-1); ch |= ImGui::InputTextMultiline("##pd", &P.description, {-1, 70});
    ImGui::SeparatorText("Maps & Modes");
    ImGui::Text("Game Default Map"); ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##sl", P.startLevel.c_str())) { for (auto& [ln, lv] : P.levels) if (ImGui::Selectable(ln.c_str(), ln == P.startLevel)) { P.startLevel = ln; ch = true; } ImGui::EndCombo(); }
    ImGui::SeparatorText("Packaging"); ImGui::TextWrapped("Windows: %s\\%s\\Windows\\%s.exe", buildsDir().c_str(), sanitizeName(P.name).c_str(), sanitizeName(P.name).c_str());
    ImGui::TextDisabled("Template: %s   Engine: %s   File: %s", P.templ.c_str(), PGE_VERSION, P.path.c_str());
    if (ch) { touch(); SDL_SetWindowTitle(win.win, (P.name + " - Premium Engine").c_str()); }
    ImGui::End(); }
  if (beginDialog("Statistics", &wStats, {420 * s, 0})) {
    std::map<std::string, int> types; int tris = 0; for (auto& a : L().actors) { types[actorTypeLabel(a.type)]++; if (a.type == "StaticMesh") tris += (int)makePrimitive(a.mesh).i.size() / 3; }
    if (ImGui::BeginTable("st", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
      auto row = [](const std::string& k, const std::string& v) { ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(k.c_str()); ImGui::TableNextColumn(); ImGui::TextUnformatted(v.c_str()); };
      row("Level", level); row("Actors", std::to_string(L().actors.size())); for (auto& [t, n] : types) row("  " + t, std::to_string(n));
      row("Triangles (meshes)", std::to_string(tris)); row("Draw calls (last frame)", std::to_string(R.drawCalls)); row("Levels", std::to_string(P.levels.size()));
      row("Materials", std::to_string(P.materials.size())); row("Scripts", std::to_string(P.scripts.size()));
      if (playing && game) { row("Game: actors", std::to_string(game->actorCount())); row("Game: physics bodies", std::to_string(game->physicsBodies())); }
      ImGui::EndTable(); }
    ImGui::End(); }
  if (beginDialog("Map Check", &wMapCheck, {620 * s, 320 * s})) {
    for (auto& m : mapCheck) { ImVec4 c = m.rfind("error", 0) == 0 ? ImVec4(1, 0.42f, 0.42f, 1) : m.rfind("warning", 0) == 0 ? ImVec4(1, 0.8f, 0.35f, 1) : ImVec4(0.6f, 0.9f, 0.6f, 1); ImGui::TextColored(c, "%s", m.c_str()); }
    ImGui::End(); }
  if (beginDialog("Package Project", &wPackage, {560 * s, 0})) {
    ImGui::TextWrapped("%s", packageResult.c_str()); ImGui::Spacing();
    if (packageOk) { if (ImGui::Button("Open Folder", {130, 0})) openInExplorer(packageDir); ImGui::SameLine();
      if (ImGui::Button("Run Game", {130, 0})) { std::string err; std::string exe = (fs::u8path(packageDir) / fs::u8path(sanitizeName(P.name) + exeSuffix())).u8string(); if (!launchProgram(exe, {"--windowed"}, &err)) notify(err); } ImGui::SameLine(); }
    if (ImGui::Button("Close", {100, 0})) wPackage = false; ImGui::End(); }
  if (beginDialog("Script API Reference", &wApi, {720 * s, 620 * s})) { ImGui::PushFont(ImGui::GetIO().Fonts->Fonts.Size > 2 ? ImGui::GetIO().Fonts->Fonts[2] : ImGui::GetFont()); ImGui::TextUnformatted(API_TEXT); ImGui::PopFont(); ImGui::End(); }
  if (beginDialog("Documentation", &wDocs, {720 * s, 420 * s})) { ImGui::TextWrapped("%s", DOCS_TEXT); ImGui::End(); }
  if (beginDialog("Keyboard Shortcuts", &wShortcuts, {560 * s, 560 * s})) {
    if (ImGui::BeginTable("sc", 2, ImGuiTableFlags_RowBg)) { for (auto& r : SHORTCUTS) { ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextColored(ImVec4(0.5f, 0.75f, 1, 1), "%s", r[0]); ImGui::TableNextColumn(); ImGui::TextUnformatted(r[1]); } ImGui::EndTable(); }
    ImGui::End(); }
  if (beginDialog("About Premium Engine", &wAbout, {460 * s, 0})) {
    ImGui::TextColored(ImVec4(0.0f, 0.44f, 0.88f, 1), "PREMIUM ENGINE"); ImGui::Text("Version %s (experimental)", PGE_VERSION); ImGui::Text("Made for Premium Games");
    ImGui::Separator(); ImGui::TextWrapped("C++17 - OpenGL 3.3 - SDL2 - Dear ImGui + ImGuizmo - Jolt Physics - QuickJS - nlohmann/json - glm - ImGuiColorTextEdit - Boost.Regex. All open source (MIT / zlib / Boost licences).");
    ImGui::End(); }
}

/* ------------------------------------------------------------------ Project Browser (shown when no project is open) */
void Editor::drawProjectBrowser() {
  ImGuiViewport* v = ImGui::GetMainViewport(); ImGui::SetNextWindowPos(v->WorkPos); ImGui::SetNextWindowSize(v->WorkSize);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {28, 22});
  ImGui::Begin("Project Browser", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleVar();
  ImFont* big = ImGui::GetIO().Fonts->Fonts.Size > 1 ? ImGui::GetIO().Fonts->Fonts[1] : ImGui::GetFont();
  ImGui::PushFont(big); ImGui::SetWindowFontScale(30.f / big->FontSize); ImGui::TextColored(ImVec4(0.0f, 0.5f, 1, 1), "PREMIUM ENGINE"); ImGui::SetWindowFontScale(1); ImGui::PopFont();
  ImGui::SameLine(); ImGui::TextDisabled("  %s  -  Unreal-style editor, C++ engine, games run as Windows programs", PGE_VERSION);
  ImGui::Separator(); ImGui::Spacing();
  float leftW = ImGui::GetContentRegionAvail().x * 0.5f, s = win.dpi;
  ImGui::BeginChild("recent", {leftW, 0}, ImGuiChildFlags_Border);
  ImGui::SeparatorText("RECENT PROJECTS");
  // projects in Documents/PremiumEngine/Projects + recent list
  struct Item { std::string path, name; fs::file_time_type t; };
  static std::vector<Item> items; static double scanned = -10; double now = SDL_GetTicks() / 1000.0;
  if (now - scanned > 3) { scanned = now; items.clear(); std::error_code ec; std::set<std::string> seen;
    auto add = [&](const fs::path& p) { std::string k = fs::weakly_canonical(p, ec).u8string(); if (seen.count(k) || !fs::exists(p, ec)) return; seen.insert(k); items.push_back({p.u8string(), p.stem().stem().u8string(), fs::last_write_time(p, ec)}); };
    for (auto& r : recent) add(fs::u8path(r));
    for (auto& d : fs::directory_iterator(fs::u8path(projectsDir()), ec)) if (d.is_directory()) for (auto& f : fs::directory_iterator(d.path(), ec)) { std::string n = f.path().filename().u8string(); if (n.size() > 9 && n.substr(n.size() - 9) == ".pge.json") add(f.path()); }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.t > b.t; }); }
  if (items.empty()) ImGui::TextDisabled("No projects yet. Create one on the right.");
  float tile = 150 * s; int cols = std::max(1, (int)(ImGui::GetContentRegionAvail().x / (tile + 12))), n = 0;
  for (auto& it : items) { if (n % cols) ImGui::SameLine(); n++; ImGui::PushID(it.path.c_str()); ImVec2 p = ImGui::GetCursorScreenPos();
    if (ImGui::Selectable("##p", false, ImGuiSelectableFlags_AllowDoubleClick, {tile, tile * 0.85f}) && ImGui::IsMouseDoubleClicked(0)) pendingOpen_ = it.path;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nDouble-click to open", it.path.c_str());
    ImDrawList* dl = ImGui::GetWindowDrawList(); dl->AddRectFilled({p.x + 4, p.y + 4}, {p.x + tile - 4, p.y + tile * 0.6f}, IM_COL32(16, 40, 70, 255), 4);
    dl->AddText(big, 34, {p.x + 14, p.y + 14}, IM_COL32(0, 140, 255, 255), it.name.substr(0, 2).c_str());
    ImGui::PushClipRect(p, {p.x + tile, p.y + tile}, true); dl->AddText({p.x + 6, p.y + tile * 0.63f}, IM_COL32_WHITE, it.name.c_str()); ImGui::PopClipRect();
    ImGui::PopID(); }
  ImGui::Spacing(); ImGui::SeparatorText("OPEN");
  if (hasFileDialogs()) { if (ImGui::Button("Browse...", {140, 0})) { std::string f = openFileDialog("Open project", "Premium Engine project", "*.pge.json;*.json"); if (!f.empty()) pendingOpen_ = f; } }
  else { ImGui::SetNextItemWidth(-90); ImGui::InputTextWithHint("##op", "/path/to/Project.pge.json", pathBuf_, sizeof pathBuf_); ImGui::SameLine(); if (ImGui::Button("Open")) pendingOpen_ = pathBuf_; }
  ImGui::TextDisabled("Projects from Premium Engine 0.1 (web) open here too."); ImGui::TextDisabled("You can also drop a .pge.json file on this window.");
  ImGui::EndChild(); ImGui::SameLine();
  ImGui::BeginChild("new", {0, 0}, ImGuiChildFlags_Border);
  ImGui::SeparatorText("NEW PROJECT - GAMES");
  const char* names[] = {"Blank", "First Person", "Third Person", "Block World"}; const char* ids[] = {"Blank", "FirstPerson", "ThirdPerson", "Blocks"};
  const char* desc[] = {"An empty level with a floor, a sun and a Player Start. Spectator camera.",
    "Walk and jump in first person: ramp, stairs, physics boxes, coins to collect with a score, a jump pad and a goal. Scripts included.",
    "The same playground with a third person camera behind the player character.",
    "A BLOCKFALL-style world made of blocks: 20 cm blocks near the surface, 1 m blocks below and in the mountains, built in chunks on background threads. Break and place blocks."};
  for (int i = 0; i < 4; i++) { ImGui::PushID(i); ImVec2 p = ImGui::GetCursorScreenPos(); float w = ImGui::GetContentRegionAvail().x, h = 64 * s;
    if (ImGui::Selectable("##t", browserTemplate_ == i, ImGuiSelectableFlags_AllowDoubleClick, {w, h})) browserTemplate_ = i;
    ImDrawList* dl = ImGui::GetWindowDrawList(); dl->AddRectFilled({p.x + 4, p.y + 4}, {p.x + h - 4, p.y + h - 4}, IM_COL32(20, 50, 90, 255), 4);
    dl->AddText(big, 26, {p.x + 14, p.y + h / 2 - 14}, IM_COL32(120, 190, 255, 255), i == 0 ? "B" : i == 1 ? "1P" : i == 2 ? "3P" : "BW");
    dl->AddText({p.x + h + 6, p.y + 8}, IM_COL32_WHITE, names[i]);
    ImGui::PushClipRect({p.x + h, p.y}, {p.x + w, p.y + h}, true); dl->AddText(nullptr, 0, {p.x + h + 6, p.y + 28}, ImGui::GetColorU32(ImGuiCol_TextDisabled), desc[i], nullptr, w - h - 12); ImGui::PopClipRect();
    ImGui::PopID(); }
  ImGui::Spacing(); ImGui::SeparatorText("PROJECT");
  ImGui::Text("Name"); ImGui::SetNextItemWidth(-1); ImGui::InputText("##bn", &browserName_);
  ImGui::TextDisabled("Folder: %s", (fs::u8path(projectsDir()) / fs::u8path(sanitizeName(browserName_))).u8string().c_str());
  ImGui::Spacing();
  if (ImGui::Button("Create", {160 * s, 34 * s})) { browserErr_.clear(); newProject(ids[browserTemplate_], browserName_, &browserErr_); }
  if (!browserErr_.empty()) ImGui::TextColored(ImVec4(1, 0.45f, 0.45f, 1), "%s", browserErr_.c_str());
  ImGui::EndChild();
  ImGui::End();
  if (!pendingOpen_.empty()) { std::string f = pendingOpen_; pendingOpen_.clear(); if (!openProjectFile(f)) browserErr_ = "Could not open " + f; }
}
}
