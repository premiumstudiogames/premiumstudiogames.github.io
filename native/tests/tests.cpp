// Engine tests without a window: project files, physics, scripts, game rules.
// Run: PremiumEngineTests [--write-template <kind> <file>]
#include "game.h"
#include "log.h"
#include "script.h"
#include "voxel.h"
#include "physics.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <thread>

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
  std::printf("\n%d passed, %d failed\n", passes, fails);
  return fails ? 1 : 0;
}
