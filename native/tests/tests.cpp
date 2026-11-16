// Engine tests without a window: project files, physics, scripts, game rules.
// Run: PremiumEngineTests [--write-template <kind> <file>]
#include "game.h"
#include "log.h"
#include "script.h"
#include <cstdio>
#include <cstring>
#include <cmath>

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
  std::printf("\n%d passed, %d failed\n", passes, fails);
  return fails ? 1 : 0;
}
