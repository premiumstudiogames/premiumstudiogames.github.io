// Engine tests without a window: project files, physics, scripts, game rules.
// Run: PremiumEngineTests [--write-template <kind> <file>]
#include "game.h"
#include "log.h"
#include "script.h"
#include "voxel.h"
#include "physics.h"
#include "arabic.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <thread>
#include <filesystem>

using namespace pge;
static int fails = 0, passes = 0;
#define CHECK(c, msg) do { if (c) { passes++; std::printf("  ok   %s\n", msg); } else { fails++; std::printf("  FAIL %s  (%s:%d)\n", msg, __FILE__, __LINE__); } } while (0)

static SDL_Event keyEv(Uint32 type, SDL_Scancode sc) { SDL_Event e{}; e.type = type; e.key.keysym.scancode = sc; e.key.repeat = 0; return e; }
static void run(Game& g, float secs) { for (int i = 0; i < (int)(secs * 60); i++) g.update(1.f / 60); }
static bool hasLog(const char* s) { for (auto& l : logLines()) if (l.text.find(s) != std::string::npos) return true; return false; }

int main(int argc, char** argv) {
  if (argc == 4 && !std::strcmp(argv[1], "--write-template")) { Project p = makeTemplate(argv[2], argv[2]); std::string err; return saveProjectFile(p, argv[3], &err) ? 0 : 1; }

  std::printf("project files\n");
  for (const char* t : {"Blank", "FirstPerson", "ThirdPerson"}) {
    Project p = makeTemplate(t, "T"); std::string j = toJson(p), err; Project q;
    CHECK(fromJson(j, q, &err) && toJson(q) == j, (std::string("round trip: ") + t).c_str());
  }
  { Project q; std::string err; CHECK(!fromJson("{not json", q, &err) && !err.empty(), "bad file gives an error"); }

  std::printf("script checks\n");
  CHECK(ScriptHost::check("function onUpdate(dt) { self.rotation.y += dt; }").empty(), "valid script compiles");
  { std::string e = ScriptHost::check("function onUpdate(dt) {\n  self.rotation.y += ;\n}"); CHECK(!e.empty() && e.find("line 2") != std::string::npos, ("syntax error has line: " + e).c_str()); }
  CHECK(sanitizeName("My Game") == "My_Game" && sanitizeName("\xd9\x84\xd8\xb9\xd8\xa8\xd8\xa9 1") == "\xd9\x84\xd8\xb9\xd8\xa8\xd8\xa9_1", "project folder names keep Arabic letters");
  CHECK(keyAlias("w") == "KeyW" && keyAlias("space") == "Space" && keyAlias("5") == "Digit5", "key aliases");

  std::printf("first person template\n");
  Project P = makeTemplate("FirstPerson", "Test");
  {
    Game g(P, "Main"); CHECK(g.start(), "game starts");
    run(g, 1.0f);
    glm::vec3 p0 = g.playerPos(); CHECK(std::abs(p0.y - 1.0f) < 0.6f, ("player stands on the floor, y=" + std::to_string(p0.y)).c_str());
    g.event(keyEv(SDL_KEYDOWN, SDL_SCANCODE_W)); run(g, 1.0f); g.event(keyEv(SDL_KEYUP, SDL_SCANCODE_W));
    glm::vec3 p1 = g.playerPos(); CHECK(glm::length(glm::vec2(p1.x - p0.x, p1.z - p0.z)) > 3.5f, ("W walks forward " + std::to_string(glm::length(glm::vec2(p1.x - p0.x, p1.z - p0.z))) + " m").c_str());
    g.event(keyEv(SDL_KEYDOWN, SDL_SCANCODE_SPACE)); g.update(1.f / 60); g.event(keyEv(SDL_KEYUP, SDL_SCANCODE_SPACE)); run(g, 0.25f);
    CHECK(g.playerPos().y > p1.y + 0.5f, "Space jumps");
    run(g, 1.5f); CHECK(std::abs(g.playerPos().y - p1.y) < 0.2f, "lands again");
    // coins: walk into each by teleporting
    std::string coin = g.find("Coin"); CHECK(!coin.empty(), "a coin exists");
    glm::vec3 cp = g.worldPos(coin); g.playerTeleport(cp); g.update(1.f / 60);
    CHECK(!g.exists(coin), "touching a coin destroys it (onOverlap + self.destroy)");
    // jump pad
    std::string pad = g.find("JumpPad"); glm::vec3 pp = g.worldPos(pad); g.playerTeleport(pp + glm::vec3(0, 1.0f, 0)); run(g, 0.2f);
    CHECK(g.playerPos().y > pp.y + 2.0f, "jump pad launches the player");
    // physics cubes fall and rest
    auto cubes = g.findAll("PhysicsCube"); bool fell = true; for (auto& c : cubes) fell &= g.worldPos(c).y < 20; CHECK(fell, "physics boxes stay in the world");
    // kill Z
    g.playerTeleport({0, -100, 0}); g.update(1.f / 60); CHECK(g.playerPos().y > -10, "falling below Kill Z respawns");
  }
  std::printf("scripts API\n");
  {
    Project q = makeTemplate("Blank", "S");
    q.scripts["SC_T"] = {"SC_T", R"(
let n = 0;
function onStart() { world.vars.started = true; const b = world.spawn('Sphere', { name: 'Ball', position: [0, 6, 0], physics: true, color: '#ff0000' }); world.vars.ball = b.id; log('spawned', b.name); }
function onUpdate(dt) { n++; self.position.x = 2; self.rotation.y += dt; if (n === 30) world.hud('n', 'frames ' + n); if (n === 40) world.message('hi', 1); }
function onKey(k) { if (k === 'KeyR') self.position.z = 7; }
)"};
    q.scripts["SC_Bad"] = {"SC_Bad", "function onUpdate(dt) { undefinedThing.go(); }"};
    q.scripts["SC_Loop"] = {"SC_Loop", "function onStart() { while (true) {} }"};
    Actor a = newActor("Empty", "Thinker"); a.script = "SC_T"; q.levels[0].second.actors.push_back(a);
    Actor b = newActor("Empty", "Broken"); b.script = "SC_Bad"; q.levels[0].second.actors.push_back(b);
    Actor c = newActor("Empty", "Looper"); c.script = "SC_Loop"; q.levels[0].second.actors.push_back(c);
    Game g(q, "Main"); logClear(); CHECK(g.start(), "starts");
    CHECK(hasLog("[Thinker] spawned Ball"), "log() writes to the Output Log");
    CHECK(hasLog("interrupted"), "endless loop is stopped, not frozen");
    std::string ball = g.find("Ball"); CHECK(!ball.empty(), "world.spawn makes an actor");
    float y0 = g.worldPos(ball).y; run(g, 1.0f); CHECK(g.worldPos(ball).y < y0 - 1.0f, "spawned physics ball falls");
    CHECK(std::abs(g.worldPos(g.find("Thinker")).x - 2) < 1e-4f, "self.position.x = 2 moves the actor");
    CHECK(hasLog("SC_Bad.onUpdate on Broken") && hasLog("undefinedThing"), "runtime error names the script, event and actor");
    int errs = 0; for (auto& l : logLines()) errs += l.text.find("SC_Bad.onUpdate") != std::string::npos; CHECK(errs == 1, "a broken event is reported once, then switched off");
    g.event(keyEv(SDL_KEYDOWN, SDL_SCANCODE_R)); g.event(keyEv(SDL_KEYUP, SDL_SCANCODE_R));
    g.update(1.f / 60); CHECK(std::abs(g.worldPos(g.find("Thinker")).z - 7) < 1e-4f, "onKey gets the key code");
  }

  std::printf("block world\n");
  {
    Physics ph; ph.init(-9.81f);
    VoxelConfig cfg; cfg.minY = -16; cfg.maxY = 32; cfg.fineLo = -4; cfg.fineHi = 8; cfg.viewDistance = 2; cfg.threads = 2;
    VoxelWorld w(cfg, &ph);
    BlockType grass; grass.color[0] = glm::vec3(0.3f, 0.7f, 0.2f); w.defineBlock(1, grass); BlockType rock; w.defineBlock(3, rock); BlockType glassT; glassT.opaque = false; w.defineBlock(9, glassT);
    w.setBuiltinGenerator("flat", R"({"ground": 0, "top": 1, "dirt": 2, "stone": 3})");
    auto t0 = std::chrono::steady_clock::now(); w.update({8, 0, 8}); w.waitIdle();
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    auto st = w.stats(); CHECK(st.meshed >= 9, ("chunks around the focus are built: " + std::to_string(st.meshed) + " in " + std::to_string((int)ms) + " ms (gen " + std::to_string(st.genMs) + " ms, mesh " + std::to_string(st.meshMs) + " ms each)").c_str());
    CHECK(w.ready({8, 0, 8}), "ready() near the focus");
    CHECK(std::abs(w.topY(3.1f, 3.1f) - 0.f) < 1e-4f, "flat ground: top at y = 0");
    CHECK(w.getBlock({3.1f, -0.5f, 3.1f}).id == 1 && w.getBlock({3.1f, -1.5f, 3.1f}).id == 2 && w.getBlock({3.1f, -10.f, 3.1f}).id == 3, "layers: grass, dirt, stone (1 m below the near layer)");
    CHECK(w.getBlock({3.1f, -0.5f, 3.1f}).size == VC, "generated near-layer ground is 1 m blocks");
    CHECK(st.triangles < 9 * 600, ("hidden faces removed, flat faces merged: " + std::to_string(st.triangles) + " triangles").c_str());
    // 20 cm block
    CHECK(w.setBlock({3, 0, 3}, 5, 0.2f), "place a 20 cm block");
    BlockInfo b = w.getBlock({3.1f, 0.1f, 3.1f}); CHECK(b.id == 5 && b.size == 1 && b.origin == glm::ivec3(15, 0, 15), "it is there, 20 cm");
    CHECK(std::abs(w.topY(3.1f, 3.1f) - 0.2f) < 1e-4f, "topY sees it");
    CHECK(!w.canPlace({3, 0, 3}, 0.2f), "can't place into an occupied cell");
    // 1 m block in the near layer, any 20 cm position
    CHECK(w.setBlock({5.2f, 0, 5.4f}, 6, 1.f), "place a 1 m block at a 20 cm position");
    b = w.getBlock({6.1f, 0.9f, 6.3f}); CHECK(b.id == 6 && b.size == 5 && b.origin == glm::ivec3(26, 0, 27), "every cell of it points at the block");
    CHECK(w.removeBlock({5.5f, 0.5f, 5.5f}) == 6 && w.getBlock({6.1f, 0.9f, 6.3f}).id == 0, "remove takes the whole block");
    // a block across a chunk border
    CHECK(w.setBlock({15.6f, 0, 2}, 6, 0.8f), "a block across two chunks");
    b = w.getBlock({16.1f, 0.1f, 2.1f}); CHECK(b.id == 6 && b.origin.x == 78, "the part in the next chunk knows its block");
    CHECK(w.removeBlock({16.2f, 0.2f, 2.2f}) == 6 && w.getBlock({15.7f, 0.1f, 2.1f}).id == 0, "removing it from one side removes all of it");
    // placing over blocks replaces them
    w.setBlock({1, 0, 1}, 5, 0.2f); w.setBlock({1.2f, 0, 1}, 5, 0.2f); CHECK(w.setBlock({1, 0, 1}, 6, 0.4f) && w.getBlock({1.3f, 0.3f, 1.1f}).id == 6, "a bigger block replaces the small ones under it");
    // outside the near layer only 1 m blocks
    std::string why; CHECK(!w.setBlock({2, -10, 2}, 3, 0.2f, &why) && !why.empty(), ("20 cm blocks only in the near layer: " + why).c_str());
    CHECK(w.removeBlock({2.5f, -9.5f, 2.5f}) == 3 && w.getBlock({2.5f, -9.5f, 2.5f}).id == 0, "dig a 1 m block deep down");
    CHECK(w.setBlock({2.4f, -9.6f, 2.7f}, 3, 1.f) && w.getBlock({2.5f, -9.5f, 2.5f}).id == 3, "1 m blocks there snap to the 1 m grid");
    CHECK(!w.setBlock({2, -4.4f, 2}, 3, 1.f, &why), "a block can't cross the edge of the near layer");
    // ray cast
    VoxelHit h = w.raycast({9.1f, 5, 9.1f}, {0, -1, 0}, 10); CHECK(h.hit && h.id == 1 && h.normal == glm::ivec3(0, 1, 0) && std::abs(h.distance - 5.f) < 0.01f, "ray down hits the grass top");
    glm::vec3 pc = w.placeCorner(h, 0.4f); CHECK(std::abs(pc.y) < 1e-4f && std::abs(pc.x - 8.8f) < 0.21f, "place position sits on the face");
    h = w.raycast({9.1f, 0.5f, 9.1f}, {1, 0, 0}, 3); CHECK(!h.hit, "nothing in the air");
    w.setBlock({10, 0, 9}, 5, 0.4f); h = w.raycast({9.1f, 0.1f, 9.1f}, {1, 0, 0}, 3); CHECK(h.hit && h.normal == glm::ivec3(-1, 0, 0) && h.origin.x == 50, "ray sideways hits the side of a 40 cm block");
    // rebuilt after edits, and the player collides with blocks
    w.update({8, 0, 8}); w.waitIdle();
    uint32_t pl = ph.addPlayer({12.5f, 3, 12.5f}); for (int i = 0; i < 120; i++) { ph.step(1.f / 60); w.update({8, 0, 8}); }
    CHECK(std::abs(ph.position(pl).y - 0.9f) < 0.08f, ("the player stands on the blocks: y=" + std::to_string(ph.position(pl).y)).c_str());
    w.setBlock({12, 0, 12}, 6, 1.f); ph.teleport(pl, {12.5f, 3, 12.5f}, glm::quat(1, 0, 0, 0)); ph.setVelocity(pl, glm::vec3(0));
    for (int i = 0; i < 20; i++) { w.update({8, 0, 8}); std::this_thread::sleep_for(std::chrono::milliseconds(5)); } w.waitIdle();
    for (int i = 0; i < 120; i++) { ph.step(1.f / 60); w.update({8, 0, 8}); }
    CHECK(std::abs(ph.position(pl).y - 1.9f) < 0.08f, ("collision follows a placed block: y=" + std::to_string(ph.position(pl).y)).c_str());
    w.removeBlock({12.5f, 0.5f, 12.5f}); w.removeBlock({12.5f, -0.5f, 12.5f}); w.waitIdle();
    for (int i = 0; i < 90; i++) { ph.step(1.f / 60); w.update({8, 0, 8}); }
    CHECK(ph.position(pl).y < 0.5f, ("dig the blocks out and the player drops: y=" + std::to_string(ph.position(pl).y)).c_str());
    // transparent blocks don't hide faces
    CHECK(w.setBlock({4, 0, 12}, 9, 1.f), "transparent block type");
    // far chunks unload, edited ones keep their blocks
    w.setBlock({3.4f, 0, 3.4f}, 5, 0.2f); w.update({400, 0, 400}); w.waitIdle(); w.update({8, 0, 8}); w.waitIdle();
    CHECK(w.getBlock({3.5f, 0.1f, 3.5f}).id == 5, "edits survive going far away and back");
  }
  {
    Physics ph; ph.init(-9.81f); VoxelConfig cfg; cfg.minY = -16; cfg.maxY = 32; cfg.fineLo = -4; cfg.fineHi = 8; cfg.viewDistance = 2; cfg.threads = 3;
    VoxelWorld w(cfg, &ph); logClear();
    w.setScriptGenerator(R"(function (chunk, noise, p) {
      for (let x = 0; x < 16; x++) for (let z = 0; z < 16; z++) {
        const h = Math.floor(noise.fbm((chunk.x + x) * 0.05, (chunk.z + z) * 0.05, 3) * 3) + p.base;
        chunk.column(x, z, -16, h, 3);
        if (x === 4 && z === 4) chunk.set(x, h, z, 7, 0.4);
      }
    })", R"({"base": 2})");
    w.update({0, 0, 0}); w.waitIdle(); for (auto& l : w.takeLogs()) logMsg(l);
    CHECK(w.stats().meshed >= 9 && !hasLog("error"), "JavaScript generator runs on the background threads");
    float top = w.topY(4.1f, 4.1f); BlockInfo b = w.getBlock({4.1f, top - 0.1f, 4.1f}); CHECK(b.id == 7 && b.size == 2, ("chunk.set from the generator: id " + std::to_string(b.id)).c_str());
    VoxelWorld w2(cfg, &ph); w2.setScriptGenerator("function (chunk) { chunk.column(0, 0, 0, 1, 1); notDefined(); }", "{}"); w2.update({0, 0, 0}); w2.waitIdle();
    bool errLine = false; for (auto& l : w2.takeLogs()) errLine |= l.find("notDefined") != std::string::npos; CHECK(errLine, "a generator error is reported");
  }
  {
    Project q = makeTemplate("Blocks", "B"); Game g(q, "Main"); logClear(); CHECK(g.start(), "Block World template starts");
    for (int i = 0; i < 600 && !(g.voxels() && g.voxels()->ready(g.playerPos())); i++) { g.update(1.f / 60); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    run(g, 2.0f); glm::vec3 p = g.playerPos(); float top = g.voxels()->topY(p.x, p.z);
    CHECK(!std::isnan(top) && std::abs(p.y - 0.9f - top) < 0.15f, ("the player stands on the generated ground: feet " + std::to_string(p.y - 0.9f) + ", ground " + std::to_string(top)).c_str());
    int errs = 0; for (auto& l : logLines()) errs += l.kind == LogKind::Error; CHECK(errs == 0, "no script errors");
    // look down, right click places a block, left click breaks it
    g.event([] { SDL_Event e{}; e.type = SDL_MOUSEMOTION; e.motion.xrel = 0; e.motion.yrel = 600; return e; }()); g.update(1.f / 60);
    glm::vec3 cp = g.cameraPos(), cd = g.cameraDir(); VoxelHit h = g.voxels()->raycast(cp, cd, 7); CHECK(h.hit, "looking at the ground");
    auto click = [&](Uint8 btn) { SDL_Event e{}; e.type = SDL_MOUSEBUTTONDOWN; e.button.button = btn; g.event(e); g.update(1.f / 60); e.type = SDL_MOUSEBUTTONUP; g.event(e); g.update(1.f / 60); };
    glm::vec3 pc = g.voxels()->placeCorner(h, 0.2f); click(SDL_BUTTON_RIGHT);
    CHECK(g.voxels()->getBlock(pc + glm::vec3(0.1f)).id == 5, "right click places a block against the face you look at");
    h = g.voxels()->raycast(cp, cd, 7); CHECK(h.hit && h.id == 5, "now you look at it");
    click(SDL_BUTTON_LEFT); CHECK(g.voxels()->getBlock(pc + glm::vec3(0.1f)).id == 0, "left click breaks it");
    h = g.voxels()->raycast(cp, cd, 7);
    SDL_Event k{}; k.type = SDL_KEYDOWN; k.key.keysym.scancode = SDL_SCANCODE_5; g.event(k); g.update(1.f / 60); k.type = SDL_KEYUP; g.event(k);
    pc = g.voxels()->placeCorner(h, 1.f); click(SDL_BUTTON_RIGHT); BlockInfo bi = g.voxels()->getBlock(pc + glm::vec3(0.5f)); CHECK(bi.id == 5 && bi.size == 5, "key 5 then right click: a 1 m block");
  }
  std::printf("Arabic text\n");
  {
    std::u32string v = toU32(visualText(u8"في BLOCKFALL 3001! لا"));
    const std::u32string want = {0xFEFB, ' ', '!', 'B', 'L', 'O', 'C', 'K', 'F', 'A', 'L', 'L', ' ', '3', '0', '0', '1', ' ', 0xFEF2, 0xFED3};
    CHECK(v == want, "letters joined (lam-alef, initial / final forms) and drawn right to left, English and numbers kept");
    CHECK(visualText("Hello 123") == "Hello 123" && !hasRtl("Hello"), "English text is left as it is");
    std::u32string p = toU32(visualText(u8"الحجم (20 سم)")); CHECK(p.front() == '(' && p.find(U')') > p.find(U'2') && p.find(U'2') < p.find(U'0'), "brackets are mirrored in right-to-left text, numbers keep their order");
  }
  std::printf("BLOCKFALL noise in C++\n");
  {
    struct R { double x, y, s, h2, vn, fbm, hashf, vn3; };
    const R ref[] = {   // from the game's own JavaScript (world.js) run in node
      {0.37, -12.9, 2026, 0.98463987628929317, 0.87011355984723004, 0.68284872369359384, 0.34861448779702187, 0.47188525358733047},
      {523.25, 488.8, 2027, 0.53668088815174997, 0.32103243455971475, 0.22571885645487269, 0.11302765854634345, 0.73120256849741894},
      {-3.5, 100000, 2526, 0.89711623359471560, 0.84349026263225824, 0.29345982101710805, 0.45510072330944240, 0.69287925051368127},
      {7.0123, 0.0001, -5, 0.87638835818506777, 0.87622811781594723, 0.69816833821378665, 0.73874530824832618, 0.88697655238405659},
      {123456.7, -98765.4, 2101, 0.40773144038394094, 0.73230710432967816, 0.38763949966899752, 0.22923647123388946, 0.73580838217274092}};
    bool same = true; for (auto& r : ref) {
      same &= bf::h2(std::floor(r.x), std::floor(r.y), r.s) == r.h2 && bf::vn(r.x, r.y, r.s) == r.vn && bf::fbm(r.x * 0.012, r.y * 0.012, r.s) == r.fbm &&
              bf::hashf(std::floor(r.x), 11, std::floor(r.y)) == r.hashf && bf::vn3(r.x / 26, r.y / 17, (r.x + r.y) / 26, r.s + 611) == r.vn3; }
    CHECK(same, "h2, vn, fbm, hashf, vn3 give exactly the game's numbers");
  }

  std::printf("block world 2: writes into neighbour chunks, shapes, carving, liquids, saving\n");
  {
    Physics ph; ph.init(-9.81f); VoxelConfig cfg; cfg.minY = -16; cfg.maxY = 32; cfg.fineLo = -4; cfg.fineHi = 8; cfg.viewDistance = 2; cfg.threads = 3;
    const char* GEN = R"(function (chunk, noise) {
      for (let x = 0; x < 16; x++) for (let z = 0; z < 16; z++) { chunk.column(x, z, -16, -1, 3); chunk.column(x, z, -1, 0, 1); chunk.tint(x, z, [1.2, 1, 0.8]); }
      if (chunk.cx === 0 && chunk.cz === 0) {
        chunk.set(15.6, 0, 4, 5, 0.8);                    // across the border into chunk (1, 0)
        chunk.set(17, 0, 6, 6, 1);                        // all of it in chunk (1, 0)
        chunk.column(-1, 9, 0, 3, 7);                     // a 1 m column in chunk (-1, 0)
        chunk.place(3, -1, 3, 8, 1);                      // only into air: the ground is there, nothing happens
        chunk.set(5, 0, 5, 9, 1, { shape: 'stairs', rotation: 2 });
        chunk.set(-0.4, 0, 12, 6, 1, { onlyEmpty: true }); // into chunk (-1, 0), only if empty there
      }
      if (chunk.cx === 1 && chunk.cz === 0) chunk.set(3, 0, 3, 7, 0.4);
    })";
    VoxelWorld w(cfg, &ph); logClear(); w.setScriptGenerator(GEN, "{}");
    BlockType bt; w.defineBlock(1, bt); w.defineBlock(3, bt); w.defineBlock(5, bt); w.defineBlock(6, bt); w.defineBlock(7, bt); w.defineBlock(9, bt);
    w.update({0, 0, 0}); w.waitIdle(); for (auto& l : w.takeLogs()) logMsg(l);
    CHECK(!hasLog("error"), "generator runs");
    BlockInfo b = w.getBlock({16.1f, 0.1f, 4.1f}); CHECK(b.id == 5 && b.origin.x == 78 && w.getBlock({15.7f, 0.1f, 4.1f}).id == 5, "a block across the border is in both chunks");
    CHECK(w.getBlock({17.5f, 0.5f, 6.5f}).id == 6, "a block written into the next chunk is there");
    CHECK(w.getBlock({-0.5f, 2.5f, 9.5f}).id == 7 && w.getBlock({-0.5f, 3.5f, 9.5f}).id == 0, "a 1 m column written into the chunk on the other side");
    CHECK(w.getBlock({3.5f, -0.5f, 3.5f}).id == 1, "chunk.place leaves blocks that are there");
    CHECK(w.getBlock({-0.3f, 0.5f, 12.5f}).id == 6, "chunk.set onlyEmpty into the neighbour's air");
    b = w.getBlock({5.5f, 0.5f, 5.5f}); CHECK(b.id == 9 && b.extra.shape == SH_Stairs + 1 && b.extra.rot == 2, "generator blocks can have a shape and a turn");
    // go far away (chunks are dropped) and back (made again): the same blocks, also when the other chunk is made first
    w.update({400, 0, 400}); w.waitIdle(); w.update({40, 0, 0}); w.waitIdle(); w.update({0, 0, 0}); w.waitIdle();
    CHECK(w.getBlock({17.5f, 0.5f, 6.5f}).id == 6 && w.getBlock({16.1f, 0.1f, 4.1f}).id == 5 && w.getBlock({-0.5f, 2.5f, 9.5f}).id == 7, "made again: the neighbour writes come back");
    // a player's change in the target chunk stays when the source chunk is made again
    w.removeBlock({17.5f, 0.5f, 6.5f}); w.update({400, 0, 400}); w.waitIdle(); w.update({0, 0, 0}); w.waitIdle();
    CHECK(w.getBlock({17.5f, 0.5f, 6.5f}).id == 0, "a removed generator block does not come back");
    // shapes, heights, carving
    std::string why; BlockOpts o; CHECK(blockOptsFromJson(R"({"shape": "half", "rotation": 1})", o, &why) && o.extra.shape == SH_Half + 1 && o.extra.rot == 1, "block options from JSON");
    CHECK(!blockOptsFromJson(R"({"shape": "banana"})", o, &why) && why.find("shape") != std::string::npos, ("unknown shape is an error: " + why).c_str());
    BlockOpts half; half.extra.shape = SH_Half + 1; CHECK(w.setBlock({10, 0, 10}, 9, 1.f, half), "place a half block");
    BlockOpts low; low.sy = 2; CHECK(w.setBlock({12, 0, 10}, 9, 1.f, low) && w.getBlock({12.5f, 0.3f, 10.5f}).sizeY == 2 && w.getBlock({12.5f, 0.5f, 10.5f}).id == 0, "a 1 m wide, 40 cm tall block");
    CHECK(std::abs(w.topY(12.5f, 10.5f) - 0.4f) < 1e-4f, "topY of the low block");
    BlockOpts st; st.extra.shape = SH_Stairs + 1; st.extra.rot = 3; CHECK(w.setBlock({15.6f, 0, 10}, 9, 0.8f, st), "stairs across the chunk border");
    b = w.getBlock({16.1f, 0.1f, 10.1f}); CHECK(b.extra.shape == SH_Stairs + 1 && b.extra.rot == 3, "the other chunk knows its shape and turn");
    CHECK(w.setBlock({2, 0, 12}, 9, 1.f) && w.carve({2.5f, 0.5f, 12.5f}, 0, 4, 0, false), "carve a piece out");
    b = w.getBlock({2.5f, 0.5f, 12.5f}); CHECK(b.extra.carved && !b.extra.sub(0, 4, 0) && b.extra.sub(1, 4, 0) && b.extra.sub(0, 3, 0), "the block remembers its carved form");
    for (int k = 0; k < 5; k++) for (int j = 0; j < 5; j++) for (int i = 0; i < 5; i++) w.carve({2.5f, 0.5f, 12.5f}, i, j, k, false);
    CHECK(w.getBlock({2.5f, 0.5f, 12.5f}).id == 0, "carving every piece away removes the block");
    BlockType model; model.model.push_back(ModelPart()); model.model[0].a = glm::vec3(0.1f); model.model[0].b = glm::vec3(0.9f, 0.4f, 0.9f);
    ModelPart wheel; wheel.kind = 1; wheel.axis = 0; wheel.a = glm::vec3(0, 0.5f, 0.5f); wheel.b = glm::vec3(0.45f, 1, 0); model.model.push_back(wheel); w.defineBlock(20, model);
    CHECK(w.setBlock({8, 0, 12}, 20, 1.f), "place a model block");
    w.update({0, 0, 0}); w.waitIdle();
    uint32_t pl = ph.addPlayer({10.5f, 3, 10.5f}); for (int i = 0; i < 150; i++) { ph.step(1.f / 60); w.update({0, 0, 0}); }
    CHECK(std::abs(ph.position(pl).y - 1.4f) < 0.08f, ("the player stands on the half block: y=" + std::to_string(ph.position(pl).y)).c_str());
    ph.teleport(pl, {12.5f, 3, 10.5f}, glm::quat(1, 0, 0, 0)); ph.setVelocity(pl, glm::vec3(0)); for (int i = 0; i < 150; i++) { ph.step(1.f / 60); w.update({0, 0, 0}); }
    CHECK(std::abs(ph.position(pl).y - 1.3f) < 0.08f, ("and on the 40 cm tall block: y=" + std::to_string(ph.position(pl).y)).c_str());
    // saving the player's changes
    std::string file = (std::filesystem::temp_directory_path() / "pge-test-world.pgworld").string();
    CHECK(w.save(file, &why), ("save: " + why).c_str());
    VoxelWorld w2(cfg, &ph); w2.setScriptGenerator(GEN, "{}"); w2.update({0, 0, 0}); w2.waitIdle();
    CHECK(w2.getBlock({17.5f, 0.5f, 6.5f}).id == 6, "a new world has the generated block");
    CHECK(w2.load(file, &why), ("load: " + why).c_str()); w2.update({0, 0, 0}); w2.waitIdle();
    CHECK(w2.getBlock({17.5f, 0.5f, 6.5f}).id == 0 && w2.getBlock({10.5f, 0.2f, 10.5f}).extra.shape == SH_Half + 1 && w2.getBlock({16.1f, 0.1f, 10.1f}).extra.rot == 3,
          "loaded: removed blocks stay removed, shapes and turns come back");
    VoxelConfig other = cfg; other.maxY = 40; VoxelWorld w3(other, &ph); CHECK(!w3.load(file, &why) && !why.empty(), ("a save of another world size is refused: " + why).c_str());
    CHECK(!w2.load(file + ".none", &why), "a missing save is an error");
  }
  {   // liquids: no collision, ray casts look through, swimming
    Project q = makeTemplate("Blocks", "W");
    q.scripts.begin()->second.code = R"(
function onStart() {
  voxels.create({ bottom: -16, top: 32, nearLayer: [-4, 8], viewDistance: 2, seed: 1 });
  voxels.defineBlock(1, { color: '#5a9c3c', biome: 'top', vary: 0.1 }); voxels.defineBlock(3, { color: 'rgb(140,140,140)' });
  voxels.defineBlock(900, { color: 'rgb(52,112,186)', liquid: true, opacity: 0.7 });
  voxels.setGenerator(function (chunk) {
    for (let x = 0; x < 16; x++) for (let z = 0; z < 16; z++) {
      const lake = chunk.cx === 0 && chunk.cz === 0 && x >= 2 && x < 14 && z >= 2 && z < 14, g = lake ? -3 : 0;
      chunk.column(x, z, -16, g, 3); chunk.column(x, z, g - 1, g, 1); if (lake) chunk.column(x, z, g, 0, 900); chunk.tint(x, z, 1.1, 1, 0.9); }
  });
}
function onUpdate() { world.vars.swim = world.player.swimming; })";
    Game g(q, "Main"); logClear(); CHECK(g.start(), "liquid world starts");
    for (int i = 0; i < 600 && !(g.voxels() && g.voxels()->ready(g.playerPos())); i++) { g.update(1.f / 60); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    VoxelWorld* v = g.voxels(); CHECK(v && v->liquidAt({8, -1, 8}) == 900 && !v->solidAt({8, -1, 8}), "water is a liquid, not solid");
    VoxelHit h = v->raycast({8.1f, 5, 8.1f}, {0, -1, 0}, 20); CHECK(h.hit && h.id == 1 && std::abs(h.point.y + 3) < 0.01f, "ray casts look through water to the lake floor");
    h = v->raycast({8.1f, 5, 8.1f}, {0, -1, 0}, 20, true); CHECK(h.hit && h.id == 900 && std::abs(h.point.y) < 0.01f, "or stop at the surface when asked");
    CHECK(v->canPlace({8, -0.2f, 8}, 0.2f) && v->setBlock({8, -0.2f, 8}, 3, 0.2f) && v->getBlock({8.1f, -0.1f, 8.1f}).id == 3, "blocks go into water and replace it");
    g.playerTeleport({6.5f, 1.5f, 6.5f}); run(g, 2.0f);
    float y1 = g.playerPos().y; CHECK(y1 < -0.5f, ("the player falls into the water (no collision with it): y=" + std::to_string(y1)).c_str());
    CHECK(g.playerSwimming(), "and swims");
    g.event(keyEv(SDL_KEYDOWN, SDL_SCANCODE_SPACE)); run(g, 1.0f); float y2 = g.playerPos().y; g.event(keyEv(SDL_KEYUP, SDL_SCANCODE_SPACE));
    CHECK(y2 > y1 + 0.5f, ("Space swims up: " + std::to_string(y1) + " -> " + std::to_string(y2)).c_str());
    run(g, 0.2f); float y3 = g.playerPos().y; run(g, 1.0f); CHECK(g.playerPos().y < y3, "without Space the player sinks slowly");
    int errs = 0; for (auto& l : logLines()) errs += l.kind == LogKind::Error; CHECK(errs == 0, "no errors");
  }
  {   // Premium Games: trophies go to the file the store names, once each; premium.console / premium.store
    std::string ev = (std::filesystem::temp_directory_path() / "pge-test-events.txt").string(); std::filesystem::remove(ev);
#ifdef _WIN32
    _putenv_s("PREMIUM_GAMES_EVENTS", ev.c_str()); _putenv_s("PREMIUM_CONSOLE", "1"); _putenv_s("PREMIUM_GAMES", "1");
#else
    setenv("PREMIUM_GAMES_EVENTS", ev.c_str(), 1); setenv("PREMIUM_CONSOLE", "1", 1); setenv("PREMIUM_GAMES", "1", 1);
#endif
    Project q = makeTemplate("Blank", "T");
    q.scripts["SC_P"] = {"SC_P", "function onStart() { premium.trophy('first_block'); premium.trophy('first_block'); premium.trophy('bad id!'); if (premium.console && premium.store) premium.trophy('console'); }"};
    Actor a = newActor("Empty", "Trophies"); a.script = "SC_P"; q.levels[0].second.actors.push_back(a);
    Game g(q, q.startLevel); logClear(); CHECK(g.start(), "trophy game starts"); run(g, 0.1f);
    std::string got; if (FILE* f = std::fopen(ev.c_str(), "rb")) { char b[256]; size_t n = std::fread(b, 1, sizeof b, f); got.assign(b, n); std::fclose(f); }
    CHECK(got == "trophy first_block\ntrophy console\n", ("the trophy is written once, a bad id is not, premium.console works: '" + got + "'").c_str());
    bool warned = false; for (auto& l : logLines()) warned |= l.kind == LogKind::Warn && l.text.find("premium.trophy") != std::string::npos; CHECK(warned, "a bad trophy id is a warning");
#ifdef _WIN32
    _putenv_s("PREMIUM_GAMES_EVENTS", ""); _putenv_s("PREMIUM_CONSOLE", ""); _putenv_s("PREMIUM_GAMES", "");
#else
    unsetenv("PREMIUM_GAMES_EVENTS"); unsetenv("PREMIUM_CONSOLE"); unsetenv("PREMIUM_GAMES");
#endif
  }
  {   // BLOCKFALL's height: -98 .. +302 m
    Physics ph; ph.init(-9.81f); VoxelConfig cfg; cfg.minY = -98; cfg.maxY = 302; cfg.fineLo = -2; cfg.fineHi = 20; cfg.viewDistance = 3; cfg.threads = 3;
    VoxelWorld w(cfg, &ph); BlockType bt; w.defineBlock(1, bt); w.defineBlock(3, bt);
    w.setScriptGenerator(R"(function (chunk, noise) {
      for (let x = 0; x < 16; x++) for (let z = 0; z < 16; z++) { const m = 40 + Math.floor(noise.bf.fbm((chunk.x + x) * 0.02, (chunk.z + z) * 0.02, 7) * 200);
        chunk.column(x, z, -98, 5, 3); chunk.column(x, z, 5, 6, 1); if (x > 4 && x < 12 && z > 4 && z < 12) chunk.column(x, z, 20, m, 3); } })", "{}");
    auto t0 = std::chrono::steady_clock::now(); w.update({0, 0, 0}); w.waitIdle(60000);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); auto st = w.stats();
    CHECK(st.meshed >= 25 && w.getBlock({8.5f, 30.5f, 8.5f}).id == 3 && w.getBlock({8.5f, -97.5f, 8.5f}).id == 3,
          ("400 m tall world: " + std::to_string(st.meshed) + " chunks in " + std::to_string((int)ms) + " ms (gen " + std::to_string(st.genMs) + ", mesh " + std::to_string(st.meshMs) + " ms each, " + std::to_string((int)(st.memory >> 20)) + " MB)").c_str());
    CHECK(st.meshMs < 120, "a chunk of the tall world meshes fast enough");
  }
  std::printf("\n%d passed, %d failed\n", passes, fails);
  return fails ? 1 : 0;
}
