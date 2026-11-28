// Engine tests without a window: project files, physics, scripts, game rules.
// Run: PremiumEngineTests [--write-template <kind> <file>]
#include "game.h"
#include "log.h"
#include "script.h"
#include "voxel.h"
#include "physics.h"
#include "arabic.h"
#include "ui.h"
#include "audio.h"
#include "models.h"
#include "blueprint.h"
#include "assets.h"
#include "testmodel.h"
#include "mannequin.h"
#include <imgui.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <thread>
#include <array>
#include <filesystem>

using namespace pge;
using json = nlohmann::json;
static int fails = 0, passes = 0;
#define CHECK(c, msg) do { if (c) { passes++; std::printf("  ok   %s\n", msg); } else { fails++; std::printf("  FAIL %s  (%s:%d)\n", msg, __FILE__, __LINE__); } } while (0)

static SDL_Event keyEv(Uint32 type, SDL_Scancode sc) { SDL_Event e{}; e.type = type; e.key.keysym.scancode = sc; e.key.repeat = 0; return e; }
static void run(Game& g, float secs) { for (int i = 0; i < (int)(secs * 60); i++) g.update(1.f / 60); }
static bool hasLog(const char* s) { for (auto& l : logLines()) if (l.text.find(s) != std::string::npos) return true; return false; }

int main(int argc, char** argv) {
  if (argc == 3 && !std::strcmp(argv[1], "--write-mannequin")) return writeMannequin(argv[2]) ? 0 : 1;
  if (argc == 4 && !std::strcmp(argv[1], "--write-template")) { Project p = makeTemplate(argv[2], argv[2]); std::string err; return saveProjectFile(p, argv[3], &err) ? 0 : 1; }
  {   // Blueprints: nodes and wires compiled to the script code, run in a real game
    std::printf("blueprints\n");
    Blueprint bp; int id = 1;
    auto node = [&](const std::string& type, float x, float y, json v = json::object()) { BpNode n; n.id = id++; n.type = type; n.x = x; n.y = y; n.values = v; bp.nodes.push_back(n); return n.id; };
    auto wire = [&](int a, const std::string& ap, int b, const std::string& bpn) { bp.links.push_back({a, ap, b, bpn}); };
    bp.vars.push_back({"Score", PinType::Number, 5});
    bp.vars.push_back({"Spins", PinType::Number, 0});
    int begin = node("Event.BeginPlay", 0, 0), tick = node("Event.Tick", 0, 300);
    int setS = node("Var.Set", 300, 0, {{"Var", "Score"}}), getS = node("Var.Get", 100, 100, {{"Var", "Score"}}), add = node("Math.Add", 200, 100, {{"B", 37}});
    int app = node("String.Append", 400, 100, {{"A", "Score: "}}), print = node("Utilities.PrintString", 600, 0);
    wire(begin, "then", setS, "exec"); wire(getS, "Value", add, "A"); wire(add, "ReturnValue", setS, "Value"); wire(setS, "then", print, "exec");
    wire(setS, "Output", app, "B"); wire(app, "ReturnValue", print, "InString");
    // a For Loop that spawns three boxes named Box0..Box2 on a line
    int loop = node("Flow.ForLoop", 800, 0, {{"FirstIndex", 0}, {"LastIndex", 2}}), spawn = node("World.SpawnActor", 1000, 0, {{"Mesh", "Cube"}});
    int mk = node("Vector.Make", 900, 200, {{"Y", 1}}), mul = node("Math.Multiply", 850, 250, {{"B", 3}}), nm = node("String.Append", 900, 300, {{"A", "Box"}});
    wire(print, "then", loop, "exec"); wire(loop, "LoopBody", spawn, "exec"); wire(loop, "Index", mul, "A"); wire(mul, "ReturnValue", mk, "X"); wire(mk, "ReturnValue", spawn, "Location");
    wire(loop, "Index", nm, "B"); wire(nm, "ReturnValue", spawn, "Name");
    // Tick: move up 2 m/s while below 3 m (Branch), then a Delay of 0.5 s and Do Once → Show Message, and Set Game Value
    int getLoc = node("Actor.GetLocation", 100, 400), brk = node("Vector.Break", 200, 400), less = node("Math.Less", 300, 400, {{"B", 3}});
    int br = node("Flow.Branch", 400, 300), off = node("Actor.AddOffset", 600, 300), up = node("Vector.Make", 500, 450), dtm = node("Math.Multiply", 450, 500, {{"B", 2}});
    int dly = node("Flow.Delay", 600, 600, {{"Duration", 0.5}}), once = node("Flow.DoOnce", 800, 600), msg = node("Utilities.SetGlobal", 1000, 600, {{"Name", "done"}, {"Value", 1}});
    wire(tick, "then", br, "exec"); wire(getLoc, "ReturnValue", brk, "InVec"); wire(brk, "Y", less, "A"); wire(less, "ReturnValue", br, "Condition");
    wire(br, "True", off, "exec"); wire(tick, "DeltaSeconds", dtm, "A"); wire(dtm, "ReturnValue", up, "Y"); wire(up, "ReturnValue", off, "DeltaLocation");
    wire(br, "False", dly, "exec"); wire(dly, "Completed", once, "exec"); wire(once, "Completed", msg, "exec");
    // a custom event on a looping timer counts spins; key R puts the actor at z = 7
    int ce = node("Event.Custom", 0, 800, {{"Name", "Spin"}}), getSp = node("Var.Get", 100, 900, {{"Var", "Spins"}}), inc = node("Math.Add", 200, 900, {{"B", 1}});
    int setSp = node("Var.Set", 300, 800, {{"Var", "Spins"}}), hud = node("World.Hud", 500, 800, {{"Id", "spins"}});
    wire(ce, "then", setSp, "exec"); wire(getSp, "Value", inc, "A"); wire(inc, "ReturnValue", setSp, "Value"); wire(setSp, "then", hud, "exec"); wire(setSp, "Output", hud, "Text");
    int timer = node("Flow.SetTimer", 1200, 0, {{"Event", "Spin"}, {"Time", 0.25}, {"Looping", true}}); wire(loop, "Completed", timer, "exec");
    int key = node("Event.KeyPressed", 0, 1000, {{"Key", "KeyR"}}), setL = node("Actor.SetLocation", 300, 1000, {{"NewLocation", {0, 3, 7}}}); wire(key, "then", setL, "exec");
    std::vector<BpMessage> msgs; std::string code = bpCompile(bp, "BP_Test", msgs);
    int errs = 0; std::string all; for (auto& m : msgs) { errs += m.error; all += m.text + "; "; }
    CHECK(errs == 0, ("a Blueprint compiles (" + all + ")").c_str());
    CHECK(ScriptHost::check(code).empty(), ("its code is valid: " + ScriptHost::check(code)).c_str());
    CHECK(bpFromJson(bpToJson(bp)).nodes.size() == bp.nodes.size() && bpFromJson(bpToJson(bp)).links.size() == bp.links.size(), "a Blueprint saves and loads");
    Project q = makeTemplate("Blank", "BPT");
    q.scripts["BP_Test"] = {"BP_Test", code}; q.scripts["BP_Test"].graph = bpToJson(bp);
    Actor a = newActor("Empty", "Thinker"); a.script = "BP_Test"; q.levels[0].second.actors.push_back(a);
    { std::string file = (std::filesystem::temp_directory_path() / "bp-test.pge.json").string(), e; CHECK(saveProjectFile(q, file, &e), "a project with a Blueprint saves");
      Project r; CHECK(loadProjectFile(file, r, &e) && r.scripts["BP_Test"].blueprint() && r.scripts["BP_Test"].code == code, "…and loads with its graph"); }
    Game g(q, "Main"); logClear(); CHECK(g.start(), "a game with a Blueprint starts");
    CHECK(hasLog("Score: 42"), "BeginPlay → Set (5 + 37) → Print String");
    CHECK(!g.find("Box0").empty() && !g.find("Box2").empty() && g.find("Box3").empty(), "For Loop spawned Box0..Box2");
    CHECK(std::abs(g.worldPos(g.find("Box2")).x - 6) < 1e-3f, "…at Index × 3");
    run(g, 1.0f); float y1 = g.worldPos(g.find("Thinker")).y; CHECK(y1 > 1.7f && y1 < 2.3f, ("Tick moves it up 2 m/s: " + std::to_string(y1)).c_str());
    run(g, 1.5f); float y2 = g.worldPos(g.find("Thinker")).y; CHECK(y2 >= 3 && y2 < 3.1f, ("Branch stops it at 3 m: " + std::to_string(y2)).c_str());
    int spins = 0; for (auto& l : logLines()) (void)l; (void)spins;
    SDL_Event kd = keyEv(SDL_KEYDOWN, SDL_SCANCODE_R); g.event(kd); run(g, 0.05f);
    CHECK(std::abs(g.worldPos(g.find("Thinker")).z - 7) < 1e-3f, "Key Pressed (KeyR) → Set Actor Location");
    // the HUD line counts timer ticks: about 4 a second
    std::string hudSpins; for (auto& h : g.hudLines()) if (h.first == "spins") hudSpins = h.second; int n = hudSpins.empty() ? 0 : std::atoi(hudSpins.c_str());
    CHECK(n >= 9 && n <= 12, ("a looping timer runs the custom event (" + hudSpins + " spins in ~2.6 s)").c_str());
    // mistakes are found and named on their node
    Blueprint bad = bp; bad.links.push_back({getS, "Value", br, "Condition"});
    std::vector<BpMessage> m2; bpCompile(bad, "Bad", m2); bool twoIn = false; for (auto& m : m2) twoIn = twoIn || (m.error && m.node == br);
    CHECK(twoIn, "two wires into one input is an error on that node");
    Blueprint bad2 = bp; for (auto& nn : bad2.nodes) if (nn.id == getSp) nn.values["Var"] = "Gone";
    bpCompile(bad2, "Bad2", m2); bool gone = false; for (auto& m : m2) gone = gone || (m.error && m.node == getSp && !m.textAr.empty()); CHECK(gone, "a missing variable is an error (with Arabic text)");
    Blueprint loopy; loopy.nodes = {BpNode{1, "Math.Add"}, BpNode{2, "Math.Add"}}; loopy.links = {{1, "ReturnValue", 2, "A"}, {2, "ReturnValue", 1, "A"}};
    int pr = 3; BpNode pn; pn.id = pr; pn.type = "Utilities.PrintString"; loopy.nodes.push_back(pn); BpNode ev; ev.id = 4; ev.type = "Event.BeginPlay"; loopy.nodes.push_back(ev);
    loopy.links.push_back({4, "then", 3, "exec"}); loopy.links.push_back({1, "ReturnValue", 3, "InString"});
    bpCompile(loopy, "Loopy", m2); bool circ = false; for (auto& m : m2) circ = circ || m.error; CHECK(circ, "pure nodes feeding each other in a circle is an error, not a hang");
    {   // functions: inputs and outputs, called with exec pins (AddTwice) and pure (Square), from the Event Graph and from each other
      Blueprint fb; int nid = 1; auto nd = [&](BpGraph& g, const std::string& type, json v = json::object()) { BpNode n; n.id = nid++; n.type = type; n.values = v; g.nodes.push_back(n); return n.id; };
      BpFunction sq; sq.name = "Square"; sq.pure = true; sq.inputs = {{"X", PinType::Number}}; sq.outputs = {{"Y", PinType::Number}};
      { int e = nd(sq, "Function.Entry", {{"Function", "Square"}}), m = nd(sq, "Math.Multiply"), r = nd(sq, "Function.Return", {{"Function", "Square"}});
        sq.links = {{e, "then", r, "exec"}, {e, "X", m, "A"}, {e, "X", m, "B"}, {m, "ReturnValue", r, "Y"}}; }
      BpFunction at; at.name = "AddTwice"; at.inputs = {{"A", PinType::Number}, {"B", PinType::Number}}; at.outputs = {{"Sum", PinType::Number}, {"Label", PinType::String}};
      fb.funcs = {sq, at};
      { BpFunction& f = fb.funcs[1]; int e = nd(f, "Function.Entry", {{"Function", "AddTwice"}}), a1 = nd(f, "Math.Add"), a2 = nd(f, "Math.Add"), r = nd(f, "Function.Return", {{"Function", "AddTwice"}}), cs = nd(f, "Function.Call", {{"Function", "Square"}});
        f.links = {{e, "then", r, "exec"}, {e, "A", a1, "A"}, {e, "B", a1, "B"}, {a1, "ReturnValue", a2, "A"}, {a1, "ReturnValue", a2, "B"}, {a2, "ReturnValue", r, "Sum"}, {e, "A", cs, "X"}, {cs, "Y", r, "Label"}}; }
      int bpb = nd(fb, "Event.BeginPlay"), call = nd(fb, "Function.Call", {{"Function", "AddTwice"}, {"A", 3}, {"B", 4}}), h1 = nd(fb, "World.Hud", {{"Id", "sum"}}), h2 = nd(fb, "World.Hud", {{"Id", "label"}});
      int sq5 = nd(fb, "Function.Call", {{"Function", "Square"}, {"X", 5}}), h3 = nd(fb, "World.Hud", {{"Id", "sq"}});
      fb.links = {{bpb, "then", call, "exec"}, {call, "then", h1, "exec"}, {call, "Sum", h1, "Text"}, {h1, "then", h2, "exec"}, {call, "Label", h2, "Text"}, {h2, "then", h3, "exec"}, {sq5, "Y", h3, "Text"}};
      fb.nextId = nid; std::vector<BpMessage> fm; std::string code = bpCompile(fb, "BP_Fn", fm); int ferr = 0; std::string all; for (auto& m : fm) { ferr += m.error; all += m.text + "; "; }
      CHECK(ferr == 0, ("a Blueprint with functions compiles " + all).c_str());
      Blueprint back = bpFromJson(bpToJson(fb)); CHECK(back.funcs.size() == 2 && back.funcs[1].outputs.size() == 2 && back.funcs[0].pure && back.funcs[1].nodes.size() == 5 && back.nextId == nid, "functions save and load");
      Project fp = makeTemplate("Blank", "FnT"); fp.scripts["BP_Fn"] = {"BP_Fn", code}; Actor fa = newActor("Empty", "FnActor"); fa.script = "BP_Fn"; fp.levels[0].second.actors.push_back(fa);
      Game fg(fp, "Main"); fg.start(); auto hudOf = [&](const char* id) { for (auto& h : fg.hudLines()) if (h.first == id) return h.second; return std::string(); };
      CHECK(hudOf("sum") == "14" && hudOf("label") == "9" && hudOf("sq") == "25", ("AddTwice(3, 4) = 14 (and calls Square(3) = 9), Square(5) = 25: " + hudOf("sum") + " " + hudOf("label") + " " + hudOf("sq")).c_str());
      Blueprint bad = fb; bad.funcs[0].nodes.erase(bad.funcs[0].nodes.begin()); bpCompile(bad, "Bad", fm); bool noEntry = false; for (auto& m : fm) noEntry = noEntry || m.error; CHECK(noEntry, "a function without its entry is an error");
    }
    int ar = 0; for (auto& d : bpLibrary()) ar += !d.ar.empty() && !d.descAr.empty() && !d.descEn.empty(); CHECK(ar == (int)bpLibrary().size() && ar > 90, ("every node has English and Arabic names and explanations (" + std::to_string(ar) + ")").c_str());
  }
  {   // models: a rigged .glb with a picture inside and two animations, loaded, posed, played in a game, collided with, saved
    std::printf("models\n");
    namespace fs = std::filesystem; fs::path dir = fs::temp_directory_path() / "pge-model-test"; std::error_code ec; fs::remove_all(dir, ec);
    CHECK(writeTestModel((dir / "Models" / "TestBot.glb").u8string()), "a test model is written (.glb)"); fs::create_directories(dir / "Textures", ec);
    setAssetRoot(dir.u8string()); logClear();
    ModelRef m = model("Models/TestBot.glb");
    CHECK(m && m->ok(), ("it loads: " + (m ? m->error : std::string("null"))).c_str());
    CHECK(m && m->triangles == 24 && m->vertices == 48 && m->prims.size() == 1, "its triangles and vertices");
    CHECK(m && m->skins.size() == 1 && m->joints == 2 && m->nodes.size() == 3, "its skin: 2 joints, 3 nodes");
    CHECK(m && m->anims.size() == 2 && m->anim("Wave") == 0 && m->anim("Bob") == 1 && std::abs(m->anims[0].duration - 1) < 1e-4f, "its animations: Wave, Bob (1 s)");
    CHECK(m && m->mats.size() == 1 && m->mats[0].baseTex && m->mats[0].baseTex->ok && m->mats[0].baseTex->w == 4 && std::abs(m->mats[0].rough - 0.8f) < 1e-4f, "its material and the picture inside the file");
    CHECK(m && std::abs(m->shape.bmin.y) < 1e-4f && std::abs(m->shape.bmax.y - 2) < 1e-4f && std::abs(m->shape.bmax.x - 0.2f) < 1e-4f, "its bounds at rest (skinned): y 0..2");
    CHECK(hasLog("Model Models/TestBot.glb: 24 triangles"), "the log says what it loaded");
    if (m && m->ok()) {
      AnimPlay a; a.play(0, false, 1, 0); a.advance(*m, 1.0f); ModelPose p; poseModel(*m, &a, p);
      glm::vec3 top = glm::vec3(p.joints[p.skinAt[0] + 1] * glm::vec4(0.2f, 2, 0.2f, 1));
      CHECK(glm::length(top - glm::vec3(-1, 1.2f, 0.2f)) < 1e-3f, ("Wave at 1 s: the arm's corner turned 90° about Z: " + std::to_string(top.x) + ", " + std::to_string(top.y)).c_str());
      CHECK(!a.playing, "…and a Wave that doesn't loop stops at its end");
      a.play(0, true, 1, 0); a.advance(*m, 0.5f); poseModel(*m, &a, p); top = glm::vec3(p.joints[1] * glm::vec4(0.2f, 2, 0.2f, 1));
      CHECK(std::abs(top.x - (0.2f * std::cos(0.7854f) - std::sin(0.7854f))) < 2e-3f, "half way: 45° (slerp)");
      a.advance(*m, 1.0f); CHECK(a.playing && std::abs(a.time - 0.5f) < 1e-4f, "looping: 1.5 s is 0.5 s into the second time");
      a.play(1, true, 1, 0.5f); a.advance(*m, 0.25f); CHECK(a.from == 0 && std::abs(a.blend - 0.5f) < 1e-4f, "Bob blends in from Wave over 0.5 s");
      a.advance(*m, 0.3f); CHECK(a.from < 0, "…and then plays alone");
      AnimPlay b; b.play(1, true, 1, 0); b.advance(*m, 0.5f); poseModel(*m, &b, p); CHECK(std::abs(p.world[0][3].y - 0.5f) < 1e-4f, "Bob at 0.5 s: the root is 0.5 m up");
      { unsigned char px[16 * 16 * 3]; for (int i = 0; i < 256; i++) { px[i * 3] = 40; px[i * 3 + 1] = 200; px[i * 3 + 2] = 90; }   // pictures as people have them: JPEG, TGA, BMP
        stbi_write_jpg((dir / "Textures" / "a.jpg").u8string().c_str(), 16, 16, 3, px, 90); stbi_write_tga((dir / "Textures" / "a.tga").u8string().c_str(), 16, 16, 3, px); stbi_write_bmp((dir / "Textures" / "a.bmp").u8string().c_str(), 16, 16, 3, px);
        for (const char* f : {"Textures/a.jpg", "Textures/a.tga", "Textures/a.bmp"}) { const Texture* t = texture(f, true); CHECK(t && t->ok && t->w == 16 && t->error.empty(), (std::string("a texture from ") + f + (t ? " " + t->error : "")).c_str()); } }
      ModelRef again = model("Models/TestBot.glb"); CHECK(again.get() == m.get(), "the same file is loaded once");
      ModelRef none = model("Models/Nope.glb"); CHECK(none && !none->ok() && none->error.find("not found") != std::string::npos, "a missing model says so (and draws as a pink box)");
      { std::ofstream f(dir / "Models" / "Broken.glb", std::ios::binary); f << "glTF garbage"; } ModelRef broken = model("Models/Broken.glb"); CHECK(broken && !broken->ok(), ("a broken file says why: " + (broken ? broken->error : "")).c_str());
    }
    // in a game: a script plays and hears the end; a Blueprint too; the body collides with the model's triangles
    Project q = makeTemplate("Blank", "ModelTest"); q.path = (dir / "ModelTest.pge.json").u8string();
    Actor bot = newActor("StaticMesh", "Bot"); bot.mesh = "Models/TestBot.glb"; bot.material.clear(); bot.anim = "Wave"; bot.animLoop = false; bot.p = {0, 0, 0}; bot.s = {5, 1, 5};
    bot.script = "SC_Bot"; q.scripts["SC_Bot"] = {"SC_Bot", "function onStart() { world.hud('list', self.animations.join(',')); }\n"
      "function onAnimationEnd(name) { world.hud('end', name + ':' + self.animation.playing); const ok = self.playAnimation('Bob', { blend: 0 }); world.hud('bob', String(ok) + ':' + self.animation.name); world.hud('bad', String(self.playAnimation('Nope'))); }\n"
      "function onUpdate(dt) { if (self.animation && self.animation.name === 'Bob') { const b = self.bone('Arm'); world.hud('arm', b ? b.position.y.toFixed(2) : 'none'); } }\n"};
    q.levels[0].second.actors.push_back(bot);
    { Blueprint bp; BpNode ev; ev.id = 1; ev.type = "Event.BeginPlay"; BpNode pl; pl.id = 2; pl.type = "Animation.Play"; pl.values = {{"Animation", "Bob"}, {"Loop", false}, {"BlendTime", 0}};
      BpNode end; end.id = 3; end.type = "Event.AnimationEnd"; BpNode set; set.id = 4; set.type = "World.Hud"; set.values = {{"Id", "bp"}};
      BpNode fx; fx.id = 5; fx.type = "World.SetCameraEffect"; fx.values = {{"Effect", "Film Grain"}, {"Amount", 0.3}};
      bp.nodes = {ev, pl, end, set, fx}; bp.links = {{1, "then", 2, "exec"}, {3, "then", 4, "exec"}, {3, "Animation", 4, "Text"}, {2, "then", 5, "exec"}};
      std::vector<BpMessage> ms; std::string code = bpCompile(bp, "BP_Anim", ms); int errs = 0; for (auto& x : ms) errs += x.error; CHECK(errs == 0, "a Blueprint with Play Animation and On Animation End compiles");
      q.scripts["BP_Anim"] = {"BP_Anim", code}; q.scripts["BP_Anim"].graph = bpToJson(bp); }
    Actor bot2 = newActor("StaticMesh", "Bot2"); bot2.mesh = "Models/TestBot.glb"; bot2.material.clear(); bot2.p = {10, 0, 0}; bot2.script = "BP_Anim"; q.levels[0].second.actors.push_back(bot2);
    Material mt; mt.name = "M_Bricks"; mt.baseMap = "Textures/bricks.png"; mt.normalMap = "Textures/bricks_n.png"; mt.tiling = {2, 3}; mt.worldAligned = true; mt.masked = true; mt.cutoff = 0.3f; q.materials["M_Bricks"] = mt;
    { std::string e; CHECK(saveProjectFile(q, q.path, &e), "a project with a model saves"); Project r; CHECK(loadProjectFile(q.path, r, &e), "…and loads");
      const Actor* ra = nullptr; for (auto& x : r.levels[0].second.actors) if (x.name == "Bot") ra = &x;
      CHECK(ra && ra->mesh == "Models/TestBot.glb" && ra->material.empty() && ra->anim == "Wave" && !ra->animLoop, "the model actor keeps its file, own materials and animation");
      const Material& rm = r.materials["M_Bricks"]; CHECK(rm.baseMap == mt.baseMap && rm.normalMap == mt.normalMap && rm.tiling == mt.tiling && rm.worldAligned && rm.masked && std::abs(rm.cutoff - 0.3f) < 1e-4f, "a material keeps its pictures, tiling, world aligned, masked"); }
    Game g(q, "Main"); CHECK(g.start(), "the game starts");
    auto hudOf = [&](const char* id) { for (auto& h : g.hudLines()) if (h.first == id) return h.second; return std::string(); };
    CHECK(hudOf("list") == "Wave,Bob", ("self.animations: " + hudOf("list")).c_str());
    CHECK(std::abs(json::parse(g.postGet())["grain"].get<float>() - 0.3f) < 1e-4f, "Blueprint: Set Camera Effect (Film Grain 0.3)");
    run(g, 0.5f); CHECK(hudOf("end").empty(), "Wave is still playing at 0.5 s");
    run(g, 0.7f); CHECK(hudOf("end") == "Wave:false", ("onAnimationEnd('Wave') when it ends: " + hudOf("end")).c_str());
    CHECK(hudOf("bob") == "true:Bob", ("self.playAnimation('Bob'): " + hudOf("bob")).c_str());
    CHECK(hudOf("bad") == "false" && hasLog("no animation \"Nope\""), "a name it doesn't have: false, and the log says which it has");
    run(g, 0.3f); std::string arm = hudOf("arm"); CHECK(!arm.empty() && arm != "none" && std::atof(arm.c_str()) > 1.05f, ("self.bone('Arm') follows the animation: y = " + arm).c_str());
    CHECK(hudOf("bp") == "Bob", ("Blueprint: Play Animation (Bob, no loop) → On Animation End: " + hudOf("bp")).c_str());
    std::string box = g.spawn("Cube", "{\"position\":[0,5,0],\"physics\":true}"); run(g, 2.5f);
    float y = g.worldPos(box).y; CHECK(y > 2.3f && y < 2.7f, ("a falling box lands on the model's top (y 2 + 0.5): " + std::to_string(y)).c_str());
    g.setSky("{\"post\":{\"vignette\":0.5,\"vhs\":2,\"tint\":\"#ff0000\"}}"); { json pp = json::parse(g.postGet()); CHECK(std::abs(pp["vignette"].get<float>() - 0.5f) < 1e-4f && pp["vhs"] == 1 && pp["tint"] == "#ff0000" && std::abs(pp["grain"].get<float>() - 0.3f) < 1e-4f, "world.setPostProcess: given values change (clamped), the rest stay"); }
    g.setSky("{\"post\":\"reset\"}"); CHECK(json::parse(g.postGet())["vignette"] == 0, "world.setPostProcess(null) turns them off");
    { WorldSettings w; w.post.vignette = 0.4f; w.post.vhs = 0.7f; w.post.tint = {1, 0.5f, 0.25f}; Level lv; lv.world = w; Level back; CHECK(levelFromJson(levelToJson(lv), back) && std::abs(back.world.post.vhs - 0.7f) < 1e-4f && std::abs(back.world.post.vignette - 0.4f) < 1e-4f && std::abs(back.world.post.tint.g - 0.5f) < 0.01f, "a level saves its camera effects"); }
    std::string sp = g.spawn("Models/TestBot.glb", "{\"position\":[20,0,0],\"animation\":\"Bob\"}"); CHECK(!sp.empty() && g.animation(sp, "state", "").find("\"Bob\"") != std::string::npos, "world.spawn of a model with an animation");
    setAssetRoot(""); fs::remove_all(dir, ec);
  }
  {   // walking (AI Move To): around a wall, up a ramp onto a platform, following the player; a Blueprint's AI Move To; no way to a closed room
    std::printf("navigation\n");
    Project q = makeTemplate("Blank", "NavTest"); Level& L = q.levels[0].second; L.actors.clear();
    auto box = [&](const char* n, glm::vec3 p, glm::vec3 s, const char* mesh = "Cube", glm::vec3 r = glm::vec3(0)) { Actor a = newActor("StaticMesh", n); a.mesh = mesh; a.p = p; a.s = s; a.r = r; L.actors.push_back(a); return a.id; };
    box("Floor", {0, -0.5f, 0}, {40, 1, 40});
    box("Wall", {0, 1.5f, -2}, {12, 3, 0.4f});                       // between the walker (z = 4) and the goal (z = -8): go round its ends (x = ±6)
    box("Platform", {12, 1, -10}, {6, 2, 6});                         // 2 m up, a ramp to it
    box("Ramp", {12, 1, -4}, {6, 2, 6}, "Wedge", {0, -90, 0});
    for (int k = 0; k < 4; k++) box("Room", {-12.f + (k % 2 ? 0 : (k / 2 ? 2.2f : -2.2f)), 1.5f, -10.f + (k % 2 ? (k / 2 ? 2.2f : -2.2f) : 0)}, k % 2 ? glm::vec3(4.8f, 3, 0.4f) : glm::vec3(0.4f, 3, 4.8f));   // a closed room at (-12, -10)
    Actor w = newActor("StaticMesh", "Walker"); w.p = {0, 0.5f, 4}; w.physics.collision = false; w.script = "SC_Walk";
    q.scripts["SC_Walk"] = {"SC_Walk", "function onStart() { world.hud('start', String(self.moveTo([0, 0, -8], { speed: 6 }))); }\n"
      "function onMoveFinished(ok) { world.vars.n = (world.vars.n || 0) + 1; world.hud('done' + world.vars.n, String(ok) + ':' + self.position.x.toFixed(1) + ',' + self.position.y.toFixed(2) + ',' + self.position.z.toFixed(1));\n"
      "  if (world.vars.n === 1) self.moveTo([12, 2, -10], { speed: 6 }); }\n"
      "function onUpdate() { world.vars.maxX = Math.max(world.vars.maxX || 0, Math.abs(self.position.x)); world.hud('maxx', world.vars.maxX.toFixed(1)); }\n"};
    L.actors.push_back(w);
    { Blueprint bp; BpNode ev; ev.id = 1; ev.type = "Event.BeginPlay"; BpNode mv; mv.id = 2; mv.type = "AI.MoveToActor"; mv.values = {{"Speed", 8}, {"AcceptanceRadius", 1.5}};
      BpNode ok; ok.id = 3; ok.type = "World.Hud"; ok.values = {{"Id", "chase"}, {"Text", "caught"}}; BpNode no; no.id = 4; no.type = "World.Hud"; no.values = {{"Id", "chase"}, {"Text", "lost"}};
      bp.nodes = {ev, mv, ok, no}; bp.links = {{1, "then", 2, "exec"}, {2, "OnSuccess", 3, "exec"}, {2, "OnFail", 4, "exec"}};
      std::vector<BpMessage> ms; std::string code = bpCompile(bp, "BP_Chase", ms); int errs = 0; std::string all; for (auto& x : ms) { errs += x.error; all += x.text + "; "; } CHECK(errs == 0, ("a Blueprint with AI Move To Actor compiles " + all).c_str());
      q.scripts["BP_Chase"] = {"BP_Chase", code}; q.scripts["BP_Chase"].graph = bpToJson(bp); }
    Actor ch = newActor("StaticMesh", "Chaser"); ch.p = {8, 0.5f, 8}; ch.s = glm::vec3(0.6f); ch.physics.collision = false; ch.script = "BP_Chase"; L.actors.push_back(ch);
    Actor ps = newActor("PlayerStart", "Start"); ps.p = {-6, 0.1f, 10}; L.actors.push_back(ps);
    Game g(q, "Main"); logClear(); CHECK(g.start(), "a level with walkers starts");
    auto hudOf = [&](const std::string& id) { for (auto& h : g.hudLines()) if (h.first == id) return h.second; return std::string(); };
    CHECK(hudOf("start") == "true", "self.moveTo: there is a way");
    CHECK(hasLog("Navigation: ") && g.navigation().walkable() > 2000, ("the floors are found (" + std::to_string(g.navigation().walkable()) + " cells)").c_str());
    run(g, 6.0f); std::string d1 = hudOf("done1");
    CHECK(d1.rfind("true:", 0) == 0 && std::abs(std::atof(d1.substr(d1.rfind(',') + 1).c_str()) + 8) < 0.7f, ("onMoveFinished(true) at the goal behind the wall: " + d1).c_str());
    CHECK(std::atof(hudOf("maxx").c_str()) > 5.8f, ("it went round the wall's end (|x| up to " + hudOf("maxx") + ")").c_str());
    run(g, 6.0f); std::string d2 = hudOf("done2");
    CHECK(d2.rfind("true:", 0) == 0 && d2.find(",2.") != std::string::npos, ("up the ramp onto the platform (y 2): " + d2).c_str());
    CHECK(hudOf("chase") == "caught", ("Blueprint AI Move To Actor (the player): " + hudOf("chase")).c_str());
    std::vector<glm::vec3> path; CHECK(!g.navigation().findPath({0, 0, 4}, {-12, 0, -10}, path), "no way into a closed room");
    CHECK(g.nav("", "path", "{\"from\":[0,0,4],\"to\":[0,0,-8]}").size() > 10, "world.findPath gives the way");
    { Project fp = makeTemplate("FirstPerson", "FP"); Game f(fp, "Main"); f.start(); std::string r = f.nav("", "path", "{\"from\":[0,0,0],\"to\":[-7,2,-9]}");
      CHECK(!r.empty(), "First Person template: the ramp leads up onto the platform"); }
    { Level back; Level lv; lv.world.nav.radius = 0.6f; lv.world.nav.cell = 0.25f; CHECK(levelFromJson(levelToJson(lv), back) && back.world.nav.radius == 0.6f && back.world.nav.cell == 0.25f, "a level saves its navigation settings"); }
  }
  {   // Third Person: the engine's mannequin plays Idle, Walk, Run, Jump by how the player moves
    std::printf("third person character\n");
    ModelRef mq = model("Characters/Mannequin.glb"); CHECK(mq && mq->ok() && mq->anims.size() == 5 && mq->joints == 18, ("the mannequin: 18 bones, Idle / Walk / Run / Jump / Fall" + (mq ? " " + mq->error : std::string())).c_str());
    Project tp = makeTemplate("ThirdPerson", "TP"); Game g(tp, "Main"); CHECK(g.start(), "a Third Person game starts");
    run(g, 1.0f); CHECK(g.playerAnimation() == "Idle", ("standing: " + g.playerAnimation()).c_str());
    SDL_Event w = keyEv(SDL_KEYDOWN, SDL_SCANCODE_W); g.event(w); run(g, 0.8f); CHECK(g.playerAnimation() == "Walk", ("W: " + g.playerAnimation()).c_str());
    SDL_Event sh = keyEv(SDL_KEYDOWN, SDL_SCANCODE_LSHIFT); g.event(sh); run(g, 0.8f); CHECK(g.playerAnimation() == "Run", ("W + Shift: " + g.playerAnimation()).c_str());
    SDL_Event sp = keyEv(SDL_KEYDOWN, SDL_SCANCODE_SPACE); g.event(sp); run(g, 0.15f); CHECK(g.playerAnimation() == "Jump", ("Space: " + g.playerAnimation()).c_str());
    g.event(keyEv(SDL_KEYUP, SDL_SCANCODE_SPACE)); g.event(keyEv(SDL_KEYUP, SDL_SCANCODE_W)); g.event(keyEv(SDL_KEYUP, SDL_SCANCODE_LSHIFT)); run(g, 2.0f);
    CHECK(g.playerAnimation() == "Idle", ("let go: back to " + g.playerAnimation()).c_str());
  }
  {   // Landscape: heights, brushes, paint, saved; the player stands on it, characters walk over it
    std::printf("landscape\n");
    Landscape L; L.resize(64, 64); L.hills(6, 20, 7); float h0 = L.heightAt(3.3f, -7.1f);
    CHECK(std::abs(h0) <= 6.01f && std::abs(L.heightAt(0, 0) - L.h[32 * 64 + 32]) < 1.5f, "hills made");
    float before = L.heightAt(10, 10); L.brush(Landscape::Raise, 10, 10, 4, 1, 0.5f); CHECK(L.heightAt(10, 10) > before + 1.5f && std::abs(L.heightAt(-20, -20) - Landscape(L).heightAt(-20, -20)) < 1e-6f, "Raise brush lifts the ground under it");
    L.brush(Landscape::Flatten, -10, -10, 5, 5, 1, 0, 2.5f); CHECK(std::abs(L.heightAt(-10, -10) - 2.5f) < 0.3f, "Flatten brings it to a height");
    L.brush(Landscape::Paint, 0, 0, 6, 5, 1, 2); size_t ci = (size_t)32 * 64 + 32; CHECK(L.w[ci * 4 + 2] > 200 && L.w[ci * 4] < 50, "Paint puts layer 3 where it paints");
    float t = 0; CHECK(L.raycast({10, 50, 10}, {0, -1, 0}, 100, t) && std::abs(50 - t - L.heightAt(10, 10)) < 0.05f, "a ray finds the ground");
    Landscape B; CHECK(B.fromJson(json::parse(L.toJson().dump())) && B.res == 64 && std::abs(B.heightAt(10, 10) - L.heightAt(10, 10)) < 1e-5f && B.w == L.w, "a landscape saves and loads (heights, paint)");
    Project q = makeTemplate("Blank", "LandT"); Level& lv = q.levels[0].second; lv.actors.clear(); lv.world.gameMode = "FirstPerson";
    Actor la = newActor("Landscape", "Land"); la.land->resize(64, 64); la.land->hills(5, 18, 3); lv.actors.push_back(la);
    Actor ps = newActor("PlayerStart", "Start"); ps.p = {6, 20, 6}; lv.actors.push_back(ps);
    Actor wk = newActor("StaticMesh", "Walker"); wk.p = {-20, la.land->heightAt(-20, -20) + 0.5f, -20}; wk.physics.collision = false; wk.script = "SC_W"; lv.actors.push_back(wk);
    q.scripts["SC_W"] = {"SC_W", "function onStart() { world.hud('w', String(self.moveTo([20, 0, 20], { speed: 8 }))); } function onMoveFinished(ok) { world.hud('wd', String(ok)); }"};
    { std::string e; std::string f = (std::filesystem::temp_directory_path() / "land.pge.json").u8string(); saveProjectFile(q, f, &e); Project r; CHECK(loadProjectFile(f, r, &e) && r.levels[0].second.actors[0].land && std::abs(r.levels[0].second.actors[0].land->heightAt(6, 6) - la.land->heightAt(6, 6)) < 1e-5f, "a project with a landscape saves and loads"); }
    Game g(q, "Main"); CHECK(g.start(), "a game on a landscape starts"); run(g, 3.0f);
    glm::vec3 pp = g.playerPos(); CHECK(std::abs(pp.y - 0.9f - la.land->heightAt(pp.x, pp.z)) < 0.25f, ("the player stands on it (feet " + std::to_string(pp.y - 0.9f) + ", ground " + std::to_string(la.land->heightAt(pp.x, pp.z)) + ")").c_str());
    auto hudOf = [&](const char* id) { for (auto& h : g.hudLines()) if (h.first == id) return h.second; return std::string(); };
    run(g, 8.0f); CHECK(hudOf("w") == "true" && hudOf("wd") == "true", ("a character walks over the hills (AI Move To): " + hudOf("w") + " " + hudOf("wd")).c_str());
    glm::vec3 wp = g.worldPos(g.find("Walker")); CHECK(std::abs(wp.y - 0.5f - la.land->heightAt(wp.x, wp.z)) < 0.3f, "…on the ground");
  }
  {   // the gameplay toolkit: line traces, attaching, damage, explosions, decals, joints, carrying, ragdolls, cars
    std::printf("gameplay\n");
    Project q = makeTemplate("Blank", "Gameplay"); Level& L = q.levels[0].second; L.actors.clear(); L.world.gameMode = "FirstPerson";
    auto box = [&](const char* n, glm::vec3 p, glm::vec3 s, bool sim = false, float mass = 1) { Actor a = newActor("StaticMesh", n); a.p = p; a.s = s; a.physics.simulate = sim; a.physics.mass = mass; L.actors.push_back(a); return a.id; };
    box("Floor", {0, -0.5f, 0}, {200, 1, 200});
    std::string target = box("Target", {0, 1, -10}, {1, 2, 1});
    std::string crate = box("Crate", {6, 0.5f, -6}, {1, 1, 1}, true, 20);
    std::string door = box("Door", {-6, 1.1f, -10.5f}, {1, 2, 0.1f}, true, 15);
    Actor hinge = newActor("PhysicsConstraint", "DoorHinge"); hinge.p = {-6.5f, 1.1f, -10.5f}; hinge.joint.kind = "Hinge"; hinge.joint.a = door; hinge.joint.min = -100; hinge.joint.max = 100; L.actors.push_back(hinge);
    std::string weak = box("Weak", {12, 3, -12}, {0.5f, 0.5f, 0.5f}, true, 50);
    Actor rope = newActor("PhysicsConstraint", "WeakLink"); rope.p = {12, 3.25f, -12}; rope.joint.kind = "Fixed"; rope.joint.a = weak; rope.joint.breakForce = 1500; rope.script = "SC_Link"; L.actors.push_back(rope);
    std::string lift = box("Lift", {-12, 0.3f, 6}, {2, 0.2f, 2}, true, 100);
    Actor sl = newActor("PhysicsConstraint", "LiftRail"); sl.p = {-12, 0.3f, 6}; sl.r = {0, 0, 90}; sl.joint.kind = "Slider"; sl.joint.a = lift; sl.joint.min = 0; sl.joint.max = 4; sl.joint.motorSpeed = 2; sl.joint.motorForce = 1e5f; L.actors.push_back(sl);
    Actor car = newActor("StaticMesh", "Car"); car.p = {20, 1.2f, 0}; car.s = {1.9f, 0.75f, 4.2f}; car.physics.simulate = true; car.physics.mass = 1300; car.vehicle.enabled = true; L.actors.push_back(car);
    Actor cab = newActor("StaticMesh", "Cabin"); cab.parent = car.id; cab.p = {0, 0.95f, 0.08f}; cab.s = {0.92f, 0.9f, 0.5f}; L.actors.push_back(cab);
    Actor man = newActor("StaticMesh", "Enemy"); man.mesh = "Characters/Mannequin.glb"; man.material.clear(); man.p = {-3, 0, -4}; man.physics.collision = false; man.anim = "Idle"; man.script = "SC_Enemy"; L.actors.push_back(man);
    Actor gun = newActor("StaticMesh", "Gun"); gun.p = {0, 5, 0}; gun.s = {0.1f, 0.1f, 0.4f}; gun.physics.collision = false; L.actors.push_back(gun);
    Actor torch = newActor("SpotLight", "Torch"); L.actors.push_back(torch);
    Actor ps = newActor("PlayerStart", "Start"); ps.p = {0, 0.1f, 0}; L.actors.push_back(ps);
    q.scripts["SC_Enemy"] = {"SC_Enemy", "let hp = 100; function onDamage(n, info) { hp -= n; world.hud('enemy', hp + ':' + info.type + ':' + (info.from ? info.from.name : 'none') + ':' + (info.point ? info.point.y.toFixed(1) : '?'));\n"
      "  if (hp <= 0 && !self.isRagdoll) self.ragdoll({ impulse: [0, 50, -300], point: info.point }); }"};
    q.scripts["SC_Link"] = {"SC_Link", "function onJointBreak(j) { world.hud('broke', 'yes ' + j); }"};
    q.scripts["SC_Player"] = {"SC_Player", "function onPlayerDamage(n, info) { world.hud('pd', n + ':' + info.type); } function onVehicleEnter(v) { world.hud('in', v.name); } function onVehicleExit(v) { world.hud('out', v.name); }"};
    Actor gm = newActor("Empty", "Rules"); gm.script = "SC_Player"; L.actors.push_back(gm);
    { Project back; CHECK(fromJson(toJson(q), back) && back.levels[0].second.actors[9].vehicle.enabled && back.levels[0].second.actors[4].joint.a == door && back.levels[0].second.actors[6].joint.breakForce == 1500,
        "cars and constraints save and load"); }
    Game g(q, "Main"); logClear(); CHECK(g.start(), "a level with cars, constraints and a character starts");
    auto hudOf = [&](const std::string& id) { for (auto& h : g.hudLines()) if (h.first == id) return h.second; return std::string(); };
    auto J = [](const std::string& s) { return json::parse(s.empty() ? "null" : s); };
    run(g, 0.5f);
    // line traces
    { json h = J(g.gameplay("raycast", "{\"from\":[0,1,0],\"to\":[0,1,-30],\"ignorePlayer\":true}", ""));
      CHECK(h.is_object() && h.value("a", "") == target && std::abs(h["p"][2].get<float>() + 9.5f) < 0.06f && h["n"][2].get<float>() > 0.9f, ("a line trace hits the target's face: " + h.dump()).c_str());
      json h2 = J(g.gameplay("raycast", json{{"from", {0, 1, 0}}, {"to", {0, 1, -30}}, {"ignorePlayer", true}, {"ignore", {target}}}.dump(), ""));
      CHECK(h2.is_null() || h2.value("a", "") != target, "…and goes through what it ignores");
      json h3 = J(g.gameplay("raycast", "{\"from\":[0,1,5],\"to\":[0,1,-5]}", "")); CHECK(h3.is_object() && h3.value("pl", false), "…and hits the player unless told not to"); }
    // attaching: a gun to the character's hand, a light to the camera, a box to the player
    { CHECK(g.gameplay("attach", g.find("Gun"), json{{"to", g.find("Enemy")}, {"bone", "Hand_R"}}.dump()) == "1", "attach a gun to a bone");
      json b = J(g.animation(g.find("Enemy"), "bone", "Hand_R")); glm::vec3 hand(b["p"][0].get<float>(), b["p"][1].get<float>(), b["p"][2].get<float>());
      CHECK(glm::length(g.worldPos(g.find("Gun")) - hand) < 0.01f, "…the gun is in the hand");
      run(g, 0.4f); b = J(g.animation(g.find("Enemy"), "bone", "Hand_R")); hand = {b["p"][0].get<float>(), b["p"][1].get<float>(), b["p"][2].get<float>()};
      CHECK(glm::length(g.worldPos(g.find("Gun")) - hand) < 0.01f, "…and stays in it while the animation plays");
      CHECK(g.gameplay("attach", g.find("Torch"), json{{"to", "@camera"}, {"offset", {0.2, -0.2, -0.3}}}.dump()) == "1", "attach a light to the camera");
      run(g, 0.1f); glm::vec3 tp = g.worldPos(g.find("Torch")), want = g.cameraPos() + g.cameraDir() * 0.3f; CHECK(glm::length(tp - want) < 0.45f, "…it is in front of the camera");
      g.gameplay("detach", g.find("Gun"), "{}"); glm::vec3 at = g.worldPos(g.find("Gun")); run(g, 0.2f); CHECK(glm::length(g.worldPos(g.find("Gun")) - at) < 0.01f && g.gameplay("parent", g.find("Gun"), "").empty(), "detach: it stays where it was"); }
    // damage: the character's onDamage, the player's health
    { g.gameplay("damage", json(g.find("Enemy")).dump(), json{{"amount", 30}, {"type", "bullet"}, {"from", "@player"}, {"point", {-3, 1.5, -4}}}.dump());
      CHECK(hudOf("enemy") == "70:bullet:Player:1.5", ("onDamage(amount, { type, from, point }): " + hudOf("enemy")).c_str());
      g.gameplay("damage", "\"@player\"", json{{"amount", 25}, {"type", "fall"}}.dump()); CHECK(std::abs(g.playerHealth() - 75) < 0.01f && hudOf("pd") == "25:fall", ("the player loses health, onPlayerDamage: " + hudOf("pd")).c_str());
      g.gameplay("damage", "\"@player\"", json{{"amount", 500}}.dump()); CHECK(g.playerHealth() == 0 && J(g.playerGet()).value("dead", false), "health 0: dead");
      run(g, 3.3f); CHECK(std::abs(g.playerHealth() - 100) < 0.01f && !J(g.playerGet()).value("dead", true), "no onPlayerDeath: the player starts again with full health"); }
    // decals
    { int n0 = g.decalCount(); g.gameplay("decal", json{{"position", {0, 0, -3}}, {"normal", {0, 1, 0}}, {"type", "blood"}}.dump(), "");
      g.gameplay("decal", json{{"position", {0, 1, -9.5}}, {"normal", {0, 0, 1}}, {"type", "bullet"}, {"actor", target}}.dump(), "");
      g.gameplay("decal", json{{"position", {1, 0, -3}}, {"normal", {0, 1, 0}}, {"type", "scorch"}, {"life", 0.5}}.dump(), "");
      CHECK(g.decalCount() == n0 + 3, "decals: blood, a bullet hole, a scorch mark"); run(g, 0.8f); CHECK(g.decalCount() == n0 + 2, "…one with a life span fades away");
      g.destroyActor(target); run(g, 0.05f); CHECK(g.decalCount() == n0 + 1, "…one on an actor goes with it");
      for (int i = 0; i < 600; i++) g.gameplay("decal", json{{"position", {i * 0.01, 0, 3}}, {"normal", {0, 1, 0}}}.dump(), ""); CHECK(g.decalCount() == 512, "…at most 512 (the oldest go)"); }
    // joints: a door on a hinge, a breakable link, a lift on a powered slider
    { glm::vec3 d0 = g.worldPos(door); g.addForce(door, {0, 0, -60}); run(g, 1.0f); glm::vec3 d1 = g.worldPos(door); glm::vec3 r1 = g.getVec(door, 'r');
      CHECK(std::abs(r1.y) > 0.2f && std::abs(d1.y - d0.y) < 0.1f && glm::length(glm::vec2(d1.x + 6.5f, d1.z + 10.5f)) < 0.6f, ("the door swings on its hinge (turned " + std::to_string(glm::degrees(r1.y)) + " deg)").c_str());
      run(g, 1.0f); glm::vec3 w = g.worldPos(weak); CHECK(hudOf("broke").empty() && std::abs(w.y - 3) < 0.2f, "a Fixed link holds a 50 kg box up");
      g.addForce(weak, {0, -2000, 0}); run(g, 0.5f); CHECK(hudOf("broke").rfind("yes", 0) == 0, "…pulled too hard it breaks: onJointBreak");
      run(g, 1.0f); CHECK(g.worldPos(weak).y < 0.5f, "…and the box falls");
      CHECK(g.worldPos(lift).y > 2.5f, ("a powered slider lifts the platform (" + std::to_string(g.worldPos(lift).y) + " m)").c_str());
      std::string jid = g.gameplay("joint", json{{"kind", "BallSocket"}, {"a", crate}, {"pivot", {6, 3, -6}}}.dump(), ""); CHECK(!jid.empty() && !g.gameplay("jointGet", jid, "").empty(), "world.addJoint: a ball socket made by a script");
      g.gameplay("jointSet", jid, "{\"remove\":true}"); CHECK(g.gameplay("jointGet", jid, "").empty(), "…and removed"); }
    // carrying: grab the crate, lift it, throw it
    { std::string h = g.gameplay("grab", json{{"actor", crate}}.dump(), ""); CHECK(!h.empty(), "grab a physics box");
      for (int i = 0; i < 90; i++) { g.gameplay("grabMove", h, json{{"target", {6, 3, -6}}}.dump()); g.update(1.f / 60); }
      glm::vec3 c = g.worldPos(crate); CHECK(glm::length(c - glm::vec3(6, 3, -6)) < 0.3f, ("…it is carried where the handle is (" + std::to_string(c.y) + " m up)").c_str());
      g.gameplay("release", h, json{{"velocity", {0, 0, -15}}}.dump()); run(g, 0.3f); CHECK(g.worldPos(crate).z < -8, "…and thrown"); }
    // ragdoll: enough damage kills the character (its script makes it a ragdoll), it falls; its bones follow the bodies
    { g.gameplay("damage", json(g.find("Enemy")).dump(), json{{"amount", 80}, {"type", "bullet"}, {"point", {-3, 1.6, -4}}}.dump());
      CHECK(g.gameplay("ragdollState", g.find("Enemy"), "") == "1", "a character becomes a ragdoll");
      CHECK(g.animation(g.find("Enemy"), "boneScale", "{\"name\":\"UpperArm_L\",\"scale\":0}") == "1" && g.animation(g.find("Enemy"), "boneScale", "{\"name\":\"Nope\",\"scale\":0}") == "0", "setBoneScale: a limb shot off");
      { ModelRef mq = model("Characters/Mannequin.glb"); AnimPlay ap; int ua = -1, hand = -1; for (size_t i = 0; i < mq->nodes.size(); i++) { if (mq->nodes[i].name == "UpperArm_L") ua = (int)i; if (mq->nodes[i].name == "Hand_L") hand = (int)i; }
        ap.scaled.push_back({ua, glm::vec3(0)}); ap.fixed.push_back({hand, glm::mat4(1)}); ModelPose mp; poseModel(*mq, &ap, mp);
        CHECK(glm::length(glm::vec3(mp.world[hand][0])) < 0.01f && glm::length(glm::vec3(mp.world[ua][0])) < 0.01f, "…what hangs on it goes too (a ragdoll's hand as well)"); }
      run(g, 2.5f); json h = J(g.gameplay("raycast", "{\"from\":[-3,3,-4.3],\"to\":[-3,-1,-4.3]}", "")); glm::vec3 ep = g.worldPos(g.find("Enemy"));
      json hb = J(g.animation(g.find("Enemy"), "bone", "Head")); float headY = hb.is_object() ? hb["p"][1].get<float>() : 9;
      CHECK(headY < 0.7f, ("…it falls to the ground (head at " + std::to_string(headY) + " m)").c_str());
      bool boneHit = false; for (float x = -5; x <= -1 && !boneHit; x += 0.25f) for (float z = -8; z <= 0 && !boneHit; z += 0.25f) { json r = J(g.gameplay("raycast", json{{"from", {x, 2, z}}, {"to", {x, -1, z}}}.dump(), "")); if (r.is_object() && r.value("a", "") == g.find("Enemy") && !r.value("bone", "").empty()) boneHit = true; }
      CHECK(boneHit, "…a line trace hits its body parts (with the bone's name)");
      std::string hd = g.gameplay("grab", json{{"actor", g.find("Enemy")}, {"point", {ep.x, 0.2, ep.z}}}.dump(), "");
      for (int i = 0; i < 120; i++) { g.gameplay("grabMove", hd, json{{"target", {ep.x, 2.2, ep.z}}}.dump()); g.update(1.f / 60); }
      float maxY = -9; for (int i = 0; i < 18; i++) { json bb = J(g.animation(g.find("Enemy"), "bone", std::vector<std::string>{"Hips", "Spine", "Chest", "Head", "Hand_L", "Hand_R", "Foot_L", "Foot_R"}[i % 8])); if (bb.is_object()) maxY = std::max(maxY, bb["p"][1].get<float>()); }
      CHECK(maxY > 1.2f, ("…a body can be picked up and dragged (highest bone " + std::to_string(maxY) + " m)").c_str()); g.gameplay("release", hd, "{}"); }
    // radial damage: pushes the crate, hurts the player near it
    { glm::vec3 c0 = g.worldPos(crate); g.playerTeleport(c0 + glm::vec3(2, 1, 0)); run(g, 0.3f); c0 = g.worldPos(crate); float hp = g.playerHealth();
      json hit = J(g.gameplay("radial", json{{"center", {c0.x, c0.y, c0.z + 1.0f}}, {"radius", 6}, {"amount", 40}, {"impulse", 400}}.dump(), ""));
      run(g, 0.4f); CHECK(glm::length(g.worldPos(crate) - c0) > 0.5f, "an explosion throws the box"); CHECK(g.playerHealth() < hp, ("…and hurts the player near it (" + std::to_string(g.playerHealth()) + ")").c_str()); }
    // the car: the player gets in with E, drives with W, steers with D, gets out with E
    { std::string cid = g.find("Car"); json st = J(g.gameplay("vehicle", cid, "{}")); CHECK(st.is_object(), "a Static Mesh with Vehicle on is a car");
      run(g, 1.5f); CHECK(g.worldPos(cid).y > 0.4f && g.worldPos(cid).y < 1.8f, ("…it stands on its wheels (" + std::to_string(g.worldPos(cid).y) + " m)").c_str());
      glm::vec3 cabin = g.worldPos(g.find("Cabin")) - g.worldPos(cid); CHECK(cabin.y > 0.5f, "…with its cabin on top (welded)");
      g.playerTeleport(g.worldPos(cid) + glm::vec3(-2.2f, 0.3f, 0)); run(g, 0.3f);
      g.event(keyEv(SDL_KEYDOWN, SDL_SCANCODE_E)); g.update(1.f / 60); g.event(keyEv(SDL_KEYUP, SDL_SCANCODE_E));
      CHECK(g.driving() == cid && hudOf("in") == "Car", "E next to the car: the player drives it (onVehicleEnter)");
      glm::vec3 p0 = g.worldPos(cid); g.event(keyEv(SDL_KEYDOWN, SDL_SCANCODE_W)); run(g, 4.0f);
      st = J(g.gameplay("vehicle", cid, "{}")); float kmh = st["speed"].get<float>() * 3.6f; glm::vec3 p1 = g.worldPos(cid);
      CHECK(kmh > 40 && p1.z < p0.z - 15, ("W: it drives forward (" + std::to_string((int)kmh) + " km/h, " + std::to_string(p0.z - p1.z) + " m)").c_str());
      CHECK(glm::length(g.playerPos() - p1) < 2.5f, "…the player goes with it");
      glm::vec3 r0 = g.getVec(cid, 'r'); g.event(keyEv(SDL_KEYDOWN, SDL_SCANCODE_D)); run(g, 1.5f); g.event(keyEv(SDL_KEYUP, SDL_SCANCODE_D)); glm::vec3 r1 = g.getVec(cid, 'r');
      float turned = glm::degrees(std::remainder(r1.y - r0.y, 6.2831853f)); CHECK(turned < -15, ("D: it turns right (" + std::to_string((int)turned) + " deg)").c_str());
      CHECK(std::abs(r1.x) < 0.35f && std::abs(r1.z) < 0.35f, ("…without rolling over (lean " + std::to_string(glm::degrees(r1.z)) + " deg)").c_str());
      g.event(keyEv(SDL_KEYUP, SDL_SCANCODE_W)); g.event(keyEv(SDL_KEYDOWN, SDL_SCANCODE_S)); float slowest = 1e9f, back = 0; double stopT = -1;
      for (int i = 0; i < 600; i++) { g.update(1.f / 60); float sp = J(g.gameplay("vehicle", cid, "{}"))["speed"].get<float>(); if (std::abs(sp) < slowest) slowest = std::abs(sp); if (sp > 0.3f) stopT = i / 60.0; back = std::min(back, sp); if (back < -3) break; }
      g.event(keyEv(SDL_KEYUP, SDL_SCANCODE_S));
      CHECK(slowest < 0.5f && stopT > 0 && stopT < 6, ("S: it brakes to a stop (" + std::to_string(stopT) + " s)").c_str()); CHECK(back < -3, "…then S drives it backwards");
      g.event(keyEv(SDL_KEYDOWN, SDL_SCANCODE_SPACE)); run(g, 2.5f); g.event(keyEv(SDL_KEYUP, SDL_SCANCODE_SPACE));
      st = J(g.gameplay("vehicle", cid, "{}")); CHECK(std::abs(st["speed"].get<float>()) < 0.5f, "Space: the hand brake stops it");
      g.event(keyEv(SDL_KEYDOWN, SDL_SCANCODE_E)); g.update(1.f / 60); g.event(keyEv(SDL_KEYUP, SDL_SCANCODE_E)); run(g, 0.5f);
      glm::vec3 pp = g.playerPos(), cp = g.worldPos(cid); CHECK(g.driving().empty() && hudOf("out") == "Car" && glm::length(glm::vec2(pp.x - cp.x, pp.z - cp.z)) < 4 && pp.y > cp.y - 1, "E again: the player gets out beside it (onVehicleExit)");
      // a car driven by a script (AI)
      g.gameplay("vehicle", cid, "{\"input\":[1,0,0,0]}"); run(g, 2.0f); st = J(g.gameplay("vehicle", cid, "{}")); CHECK(std::abs(st["speed"].get<float>()) > 4, "a script drives the car (setInput)");
      g.gameplay("vehicle", cid, "{\"input\":[0,0,1,1]}"); run(g, 3.0f);
      // a car built by a script: blocks attached to a body, then made a car
      std::string b = g.spawn("Cube", "{\"position\":[30,1.2,20],\"scale\":[2,0.5,4],\"physics\":true}"); std::string top = g.spawn("Cube", "{\"position\":[30,2,20],\"scale\":[1.5,0.8,1.5]}");
      CHECK(g.gameplay("attach", top, json{{"to", b}, {"keepWorld", true}}.dump()) == "1" && g.gameplay("vehicleMake", b, "{\"mass\":900,\"drive\":\"rear\"}") == "1", "a car built from blocks (attach, makeVehicle)");
      run(g, 1.0f); CHECK(g.worldPos(top).y > g.worldPos(b).y + 0.4f, "…its parts ride with it");
      g.gameplay("vehicle", b, "{\"input\":[1,0.3,0,0]}"); run(g, 2.0f); st = J(g.gameplay("vehicle", b, "{}")); CHECK(std::abs(st["speed"].get<float>()) > 3, "…and it drives"); }
    // a heavier car is slower to speed up; the mass set is the mass it has
    { std::string light = g.spawn("Cube", json{{"position", {60, 1.2, 0}}, {"scale", {1.8, 0.5, 4}}, {"physics", true}}.dump()), heavy = g.spawn("Cube", json{{"position", {75, 1.2, 0}}, {"scale", {1.8, 0.5, 4}}, {"physics", true}}.dump());
      g.gameplay("vehicleMake", light, "{\"mass\":900}"); g.gameplay("vehicleMake", heavy, "{\"mass\":4000}"); run(g, 1.0f);
      g.gameplay("vehicle", light, "{\"input\":[1,0,0,0]}"); g.gameplay("vehicle", heavy, "{\"input\":[1,0,0,0]}"); run(g, 2.0f);
      float vl = J(g.gameplay("vehicle", light, "{}"))["speed"].get<float>(), vh = J(g.gameplay("vehicle", heavy, "{}"))["speed"].get<float>();
      CHECK(vl > vh * 1.4f && vh > 1, ("a 900 kg car outruns a 4000 kg one (" + std::to_string(vl) + " / " + std::to_string(vh) + " m/s)").c_str()); }
  }
  {   // the gameplay API from a script, and the Blueprint nodes
    Project q = makeTemplate("Blank", "Api"); Level& L = q.levels[0].second; L.actors.clear();
    Actor f = newActor("StaticMesh", "Floor"); f.p = {0, -0.5f, 0}; f.s = {60, 1, 60}; L.actors.push_back(f);
    Actor t = newActor("StaticMesh", "Wall"); t.p = {0, 1, -6}; t.s = {4, 2, 0.5f}; L.actors.push_back(t);
    Actor b = newActor("StaticMesh", "Ball"); b.mesh = "Sphere"; b.p = {3, 0.5f, 0}; b.physics.simulate = true; b.physics.mass = 5; L.actors.push_back(b);
    Actor ps = newActor("PlayerStart", "Start"); ps.p = {0, 0.1f, 4}; L.actors.push_back(ps);
    Actor s = newActor("Empty", "Logic"); s.script = "SC_Api"; L.actors.push_back(s);
    q.scripts["SC_Api"] = {"SC_Api", "function onStart() { const h = world.raycast([0, 1, 0], [0, 1, -20]); world.hud('ray', h ? h.actor.name + ':' + h.position.z.toFixed(2) + ':' + h.normal.z.toFixed(0) : 'none');\n"
      " const w = world.find('Wall'); world.spawnDecal(h.position, h.normal, { type: 'bullet', actor: w }); w.applyDamage(5, { type: 'test' });\n"
      " const ball = world.find('Ball'); const j = world.addJoint({ kind: 'Distance', a: ball, pivot: [3, 3, 0], min: 0, max: 2 }); world.hud('joint', j ? 'yes' : 'no');\n"
      " const g = world.grab(ball); world.vars.g = g; world.hud('grab', g ? 'yes' : 'no');\n"
      " const hp = world.player.health; world.player.damage(10, { type: 'x' }); world.hud('hp', hp + '>' + world.player.health); world.player.heal(5); world.hud('hp2', String(world.player.health));\n"
      " const c = world.spawn('Cube', { position: [10, 1.2, 0], scale: [1.9, 0.7, 4], physics: true, vehicle: { torque: 900 } }); world.hud('car', c.vehicle ? 'yes' : 'no'); world.vars.car = c; }\n"
      "function onUpdate() { if (world.vars.g) world.vars.g.moveTo([3, 2, 0]); }\n"};
    Game g(q, "Main"); CHECK(g.start(), "a game using the gameplay API starts");
    auto hudOf = [&](const std::string& id) { for (auto& h : g.hudLines()) if (h.first == id) return h.second; return std::string(); };
    CHECK(hudOf("ray") == "Wall:-5.75:1", ("world.raycast: " + hudOf("ray")).c_str());
    CHECK(hudOf("joint") == "yes" && hudOf("grab") == "yes" && hudOf("car") == "yes", "world.addJoint, world.grab, world.spawn({ vehicle })");
    CHECK(hudOf("hp") == "100>90" && hudOf("hp2") == "95", ("world.player.damage / heal: " + hudOf("hp") + " " + hudOf("hp2")).c_str());
    run(g, 1.5f); glm::vec3 bp = g.worldPos(g.find("Ball")); CHECK(glm::length(bp - glm::vec3(3, 2, 0)) < 0.4f, "grab.moveTo every frame carries the ball");
    CHECK(g.decalCount() == 1, "world.spawnDecal");
    { Project q2 = q; q2.levels[0].second.world.gameMode = "FirstPerson"; q2.scripts["SC_Api"].code = "function onStart() { const c = world.spawn('Cube', { position: [6, 1.2, 0], scale: [1.9, 0.7, 4], physics: true, vehicle: true }); world.hud('in', String(world.player.enterVehicle(c))); }";
      Game g2(q2, "Main"); g2.start(); run(g2, 0.5f); bool in = false; for (auto& h : g2.hudLines()) if (h.first == "in") in = h.second == "true";
      CHECK(in && !g2.driving().empty(), "world.player.enterVehicle"); }   // the game ends with the player in the car: no crash
    CHECK(true, "a game that ends with the player in a car closes cleanly");
    // the Blueprint nodes compile
    Blueprint gbp; int id = 1; auto node = [&](const std::string& type, json v = json::object()) { BpNode n; n.id = id++; n.type = type; n.values = v; gbp.nodes.push_back(n); return n.id; };
    int ev = node("Event.BeginPlay"), tr = node("Physics.TraceFromCamera"), dm = node("Damage.Apply"), dc = node("Decal.Spawn"), rad = node("Damage.ApplyRadial"), at = node("Attach.ToCamera"), jn = node("Physics.AddJoint", {{"Kind", "Hinge"}}), mo = node("Physics.JointMotor"), en = node("Vehicle.Enter"), rg = node("Physics.Ragdoll");
    int ad = node("Event.AnyDamage"), pr = node("Utilities.PrintString"), de = node("Event.PlayerDeath"), hl = node("Player.SetHealth"), ve = node("Event.VehicleEnter"), jb = node("Event.JointBreak"), pd = node("Event.PlayerDamage"), vx = node("Event.VehicleExit");
    gbp.links = {{ev, "then", tr, "exec"}, {tr, "then", dm, "exec"}, {tr, "HitActor", dm, "DamagedActor"}, {tr, "Location", dm, "HitLocation"}, {dm, "then", dc, "exec"}, {tr, "Location", dc, "Location"}, {tr, "Normal", dc, "Normal"},
                {dc, "then", rad, "exec"}, {rad, "then", at, "exec"}, {at, "then", jn, "exec"}, {jn, "then", mo, "exec"}, {jn, "Joint", mo, "Joint"}, {mo, "then", en, "exec"}, {en, "then", rg, "exec"},
                {ad, "then", pr, "exec"}, {ad, "DamageType", pr, "InString"}, {de, "then", hl, "exec"}};
    (void)ve; (void)jb; (void)pd; (void)vx;
    std::vector<BpMessage> ms; std::string code = bpCompile(gbp, "BP_Gameplay", ms); int errs = 0; std::string all; for (auto& x : ms) { errs += x.error; if (x.error) all += x.text + "; "; }
    CHECK(errs == 0 && code.find("function onDamage(amount, info)") != std::string::npos && code.find("function onPlayerDeath()") != std::string::npos, ("the gameplay Blueprint nodes compile " + all).c_str());
    CHECK(ScriptHost::check(code).empty(), ("…to code that runs: " + ScriptHost::check(code)).c_str());
  }
  {   // animation: idle / walk / run by itself, blend spaces, layers, IK (hands, head, feet)
    std::printf("animation\n");
    Project q = makeTemplate("Blank", "Anim"); Level& L = q.levels[0].second; L.actors.clear(); L.world.gameMode = "Spectator";
    Actor fl = newActor("StaticMesh", "Floor"); fl.p = {0, -0.5f, 0}; fl.s = {80, 1, 80}; L.actors.push_back(fl);
    Actor step = newActor("StaticMesh", "Step"); step.p = {8.12f, 0.1f, 0}; step.s = {0.25f, 0.2f, 1}; L.actors.push_back(step);
    auto man = [&](const char* n, glm::vec3 p) { Actor a = newActor("StaticMesh", n); a.mesh = "Characters/Mannequin.glb"; a.material.clear(); a.p = p; a.physics.collision = false; a.anim = "Idle"; L.actors.push_back(a); };
    man("Walker", {0, 0, 0}); man("Mixer", {4, 0, 0}); man("OnStep", {8, 0, 0}); man("Flat", {12, 0, 0});
    Game g(q, "Main"); CHECK(g.start(), "a level with characters starts");
    auto J = [](const std::string& s) { return json::parse(s.empty() ? "null" : s); };
    auto bone = [&](const char* a, const char* b) { json j = J(g.animation(g.find(a), "bone", b)); return j.is_object() ? glm::vec3(j["p"][0].get<float>(), j["p"][1].get<float>(), j["p"][2].get<float>()) : glm::vec3(-99); };
    auto st = [&](const char* a) { return J(g.animation(g.find(a), "spaceState", "")); };
    // a character that walks (moveTo) walks by itself, then stands
    g.nav(g.find("Walker"), "move", json{{"x", 0}, {"y", 0}, {"z", -8}, {"speed", 1.4}}.dump()); run(g, 1.0f);
    json s = st("Walker"); CHECK(s["auto"].get<bool>() && (s["a"] == "Walk" || s["b"] == "Walk"), ("moving: it walks by itself (" + s.dump() + ")").c_str());
    g.nav(g.find("Walker"), "move", json{{"x", 0}, {"y", 0}, {"z", 8}, {"speed", 4.5}}.dump()); run(g, 1.0f); s = st("Walker"); CHECK(s["a"] == "Run" || s["b"] == "Run", ("faster: it runs (" + s.dump() + ")").c_str());
    run(g, 6.0f); s = st("Walker"); CHECK(!s["auto"].get<bool>() && J(g.animation(g.find("Walker"), "state", ""))["name"] == "Idle", "there: it stands (Idle)");
    // a blend space set by a script
    CHECK(g.animation(g.find("Mixer"), "space", json{{"anims", json::array({json::array({"Idle", 0}), json::array({"Walk", 1.5}), json::array({"Run", 5})})}, {"value", 3.25}}.dump()) == "1", "setBlendSpace");
    run(g, 0.2f); s = st("Mixer"); CHECK(s["a"] == "Walk" && s["b"] == "Run" && std::abs(s["w"].get<float>() - 0.5f) < 0.01f, ("…the value mixes the two either side (" + s.dump() + ")").c_str());
    // a layer: the upper body plays another animation, the legs don't change
    glm::vec3 foot0 = bone("Mixer", "Foot_L"), hand0 = bone("Mixer", "Hand_R");
    CHECK(g.animation(g.find("Mixer"), "layer", json{{"name", "Jump"}, {"bone", "Spine"}, {"blend", 0}}.dump()) == "1", "playLayer on the Spine");
    glm::vec3 foot1 = bone("Mixer", "Foot_L"), hand1 = bone("Mixer", "Hand_R");
    CHECK(glm::length(foot1 - foot0) < 0.02f && glm::length(hand1 - hand0) > 0.02f, ("…the arms move, the legs don't (hand " + std::to_string(glm::length(hand1 - hand0)) + ")").c_str());
    g.animation(g.find("Mixer"), "layerStop", "{\"blend\":0}"); run(g, 0.1f);
    // IK: the right hand reaches for a place; the head looks at one
    glm::vec3 sh = bone("Mixer", "UpperArm_R"), want = sh + glm::vec3(0.1f, 0.15f, 0.35f);
    CHECK(g.animation(g.find("Mixer"), "ik", json{{"bone", "Hand_R"}, {"target", {want.x, want.y, want.z}}}.dump()) == "1", "setIK on a hand"); run(g, 0.05f);
    CHECK(glm::length(bone("Mixer", "Hand_R") - want) < 0.03f, ("…the hand is there (" + std::to_string(glm::length(bone("Mixer", "Hand_R") - want)) + " m off)").c_str());
    glm::vec3 far = sh + glm::vec3(0, 0, 5); g.animation(g.find("Mixer"), "ik", json{{"bone", "Hand_R"}, {"target", {far.x, far.y, far.z}}}.dump()); run(g, 0.05f);
    glm::vec3 h = bone("Mixer", "Hand_R"); CHECK(glm::length(h - sh) < 0.75f && h.z > sh.z + 0.4f, "…out of reach: the arm points there, straight");
    glm::vec3 headA = bone("Mixer", "Head"); json r0 = J(g.animation(g.find("Mixer"), "bone", "Head"));
    g.animation(g.find("Mixer"), "look", json{{"target", {headA.x + 5, headA.y, headA.z + 2}}}.dump()); run(g, 0.05f); json r1 = J(g.animation(g.find("Mixer"), "bone", "Head"));
    CHECK(std::abs(r1["r"][1].get<float>() - r0["r"][1].get<float>()) > 0.5f, ("lookAt: the head turns (" + std::to_string(r1["r"][1].get<float>() - r0["r"][1].get<float>()) + " rad)").c_str());
    g.animation(g.find("Mixer"), "ikClear", ""); run(g, 0.05f); CHECK(std::abs(J(g.animation(g.find("Mixer"), "bone", "Head"))["r"][1].get<float>() - r0["r"][1].get<float>()) < 0.05f, "clearIK: as the animation has it");
    // feet: one on a step, one on the floor
    glm::vec3 fl1 = bone("OnStep", "Foot_L"), fr1 = bone("OnStep", "Foot_R"), fl2 = bone("Flat", "Foot_L"), fr2 = bone("Flat", "Foot_R");
    CHECK(fl1.y - fl2.y > 0.15f && std::abs(fr1.y - fr2.y) < 0.03f, ("foot IK: the foot on the step stands on it (" + std::to_string(fl1.y - fl2.y) + " m up), the other on the floor").c_str());
    g.animation(g.find("OnStep"), "footIK", "0"); run(g, 0.05f); CHECK(std::abs(bone("OnStep", "Foot_L").y - fl2.y) < 0.02f, "…footIK off: as the animation has it");
    // the script API and the Blueprint nodes
    Blueprint ab; int id = 1; auto node = [&](const std::string& type, json v = json::object()) { BpNode n; n.id = id++; n.type = type; n.values = v; ab.nodes.push_back(n); return n.id; };
    int ev = node("Event.BeginPlay"), n1 = node("Animation.SetBlendSpace"), n2 = node("Animation.SetBlendValue"), n3 = node("Animation.PlayLayer", {{"Animation", "Jump"}}), n4 = node("Animation.StopLayer"), n5 = node("Animation.SetIK"), n6 = node("Animation.LookAt"), n7 = node("Animation.AutoAnimate");
    ab.links = {{ev, "then", n1, "exec"}, {n1, "then", n2, "exec"}, {n2, "then", n3, "exec"}, {n3, "then", n4, "exec"}, {n4, "then", n5, "exec"}, {n5, "then", n6, "exec"}, {n6, "then", n7, "exec"}};
    std::vector<BpMessage> ms; std::string code = bpCompile(ab, "BP_Anim", ms); int errs = 0; for (auto& x : ms) errs += x.error;
    CHECK(errs == 0 && ScriptHost::check(code).empty(), "the animation Blueprint nodes compile to code that runs");
    q.scripts["SC_AnimApi"] = {"SC_AnimApi", "function onStart() { world.hud('a', [self.setBlendSpace([['Idle', 0], ['Walk', 1.5]], 1), self.playLayer('Jump', { bone: 'Spine' }), self.setIK('Hand_L', [0, 1.5, 1]), self.lookAt(world.player), self.autoAnimate, self.footIK].join(',')); self.blendValue = 0.5; }"};
    for (auto& a : q.levels[0].second.actors) if (a.name == "Flat") a.script = "SC_AnimApi";
    Game g2(q, "Main"); g2.start(); std::string hv; for (auto& x : g2.hudLines()) if (x.first == "a") hv = x.second; CHECK(hv == "true,true,true,true,false,true", ("the script API: " + hv).c_str());
  }
  {   // playing together: a host and a player on this computer, messages both ways, the LAN beacon, leaving
    pge::Net h, c; double t = 0; CHECK(h.host(46655, "Test world", 4, "info"), "net: host a game");
    c.lanSearch(true); CHECK(c.join("127.0.0.1", 46655), "net: join it");
    std::vector<pge::NetEvent> he, ce; auto pump = [&](int n) { for (int i = 0; i < n; i++) { t += 0.05; h.update(t); c.update(t); for (auto& e : h.take()) he.push_back(e); for (auto& e : c.take()) ce.push_back(e); std::this_thread::sleep_for(std::chrono::milliseconds(5)); } };
    pump(40); auto has = [](const std::vector<pge::NetEvent>& v, const std::string& ty, const std::string& d = "") { for (auto& e : v) if (e.type == ty && (d.empty() || e.data == d)) return true; return false; };
    CHECK(has(he, "join") && has(ce, "connected") && c.id() == 1 && c.connected(), "net: the host sees the player join, the player gets its id");
    std::string big(300000, 'x'); c.send("{\"hi\":1}", -1); h.send(big, 1); h.send("{\"all\":2}", -1); pump(60);
    CHECK(has(he, "msg", "{\"hi\":1}") && has(ce, "msg", big) && has(ce, "msg", "{\"all\":2}"), "net: messages both ways, a big one whole, in order");
    bool lan = false; for (auto& g : c.lan(t)) lan = lan || (g.name == "Test world" && g.port == 46655 && g.players == 2); CHECK(lan, "net: the game shows on the LAN with its players");
    c.stop(); pump(20); CHECK(has(he, "leave"), "net: the host sees the player leave"); h.stop(); }

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
  std::printf("cut-out blocks (leaves, flowers)\n");
  {
    Physics ph; ph.init(-9.81f);
    VoxelConfig cfg; cfg.minY = -8; cfg.maxY = 24; cfg.fineLo = -4; cfg.fineHi = 8; cfg.viewDistance = 1; cfg.threads = 2;
    VoxelWorld w(cfg, &ph); BlockType rock; w.defineBlock(3, rock);
    BlockType leaves; leaves.opaque = false; leaves.cutout = true; leaves.tile[0] = leaves.tile[1] = leaves.tile[2] = 0; w.defineBlock(6, leaves);
    BlockType glass; glass.opaque = false; w.defineBlock(9, glass);
    w.setBuiltinGenerator("flat", R"({"ground": 0, "top": 3, "dirt": 3, "stone": 3})"); w.update({8, 0, 8}); w.waitIdle();
    int t0 = w.stats().triangles;
    w.setBlock({4, 10, 4}, 6, 1.f); w.setBlock({5, 10, 4}, 6, 1.f); w.update({8, 0, 8}); w.waitIdle(); int t1 = w.stats().triangles;
    CHECK(t1 - t0 == 16, ("two leaves side by side keep the faces between them, like the game: " + std::to_string(t1 - t0) + " triangles").c_str());
    w.removeBlock({4.5f, 10.5f, 4.5f}); w.removeBlock({5.5f, 10.5f, 4.5f});
    w.setBlock({4, 10, 4}, 9, 1.f); w.setBlock({5, 10, 4}, 9, 1.f); w.update({8, 0, 8}); w.waitIdle(); int t2 = w.stats().triangles;
    CHECK(t2 - t0 == 12, ("two glass blocks hide the face between them: " + std::to_string(t2 - t0) + " triangles").c_str());
  }

  std::printf("game UI\n");
  {
    ImGui::CreateContext(); ImGuiIO& io = ImGui::GetIO(); io.Fonts->AddFontDefault(); { ImFontConfig b; b.SizePixels = 39; io.Fonts->AddFontDefault(&b); }
    unsigned char* px; int fw, fh; io.Fonts->GetTexDataAsRGBA32(&px, &fw, &fh); io.DisplaySize = ImVec2(1920, 1080); io.DeltaTime = 1.f / 60;
    Project q = makeTemplate("Blank", "T");
    q.scripts["SC_UI"] = {"SC_UI", R"JS(
let clicks = 0;
function onStart() {
  ui.rtl = true;
  const win = ui.create('window', { id: 'inv', title: 'المخزون', w: 600, h: 500, modal: true, pause: true, visible: false, anim: { anim: 'garage', time: 0.2 } });
  const grid = win.add('grid', { id: 'slots', cols: 3, cell: 80, gap: 10 });
  for (let i = 0; i < 6; i++) grid.add('panel', { id: 's' + i, bg: '#ffffff20', radius: 6, draggable: true, droppable: true, data: { slot: i } });
  grid.on('drop', e => { ui.get('label').text = 'drop ' + e.from + ' > ' + e.target.id + ' ' + e.data.slot; });
  grid.on('click', e => { ui.get('label').text = 'slot ' + e.target.id + (e.shift ? ' shift' : ''); });
  win.add('text', { id: 'label', text: '-' });
  win.add('input', { id: 'name', placeholder: 'اسم العالم', on: { submit: e => { ui.get('label').text = 'name ' + e.value; } } });
  win.add('slider', { id: 'vol', min: 0, max: 10, step: 1, value: 5, on: { change: e => { ui.get('label').text = 'vol ' + e.value; } } });
  const tabs = win.add('tabs', { id: 'tabs', labels: ['أ', 'ب'], h: 120, on: { change: e => { ui.get('label').text = 'tab ' + e.value; } } });
  tabs.add('text', { id: 'page0', text: 'one' }); tabs.add('text', { id: 'page1', text: 'two' });
  const btn = ui.create('button', { id: 'open', text: 'افتح', anchor: 'bottom', y: -40, on: { click: () => { clicks++; win.show(); } } });
  ui.create('button', { id: 'bad', text: 'x', anchor: 'top-left', x: 10, y: 10, on: { click: () => { null.x = 1; } } });
  const list = ui.create('list', { id: 'list', w: 200, h: 100, anchor: 'top-right', y: 10, x: -10 });
  for (let i = 0; i < 20; i++) list.add('text', { text: 'row ' + i });
  world.vars.ok = true;
}
)JS"};
    Actor a = newActor("Empty", "UI"); a.script = "SC_UI"; q.levels[0].second.actors.push_back(a);
    Game g(q, q.startLevel); logClear(); CHECK(g.start(), "a game with UI starts");
    Ui& U = *g.ui();
    auto frame = [&] { ImGui::NewFrame(); g.drawHud(ImGui::GetBackgroundDrawList(), 0, 0, 1920, 1080); ImGui::EndFrame(); };
    auto rect = [&](const char* id) { auto r = U.get(id)["rect"]; return std::array<float, 4>{r["x"].get<float>(), r["y"].get<float>(), r["w"].get<float>(), r["h"].get<float>()}; };
    auto mouse = [&](float x, float y, int what, Uint16 mod = 0) { SDL_Event e{}; if (what == 0) { e.type = SDL_MOUSEMOTION; e.motion.x = (int)x; e.motion.y = (int)y; }
      else { e.type = what == 1 ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP; e.button.button = SDL_BUTTON_LEFT; e.button.x = (int)x; e.button.y = (int)y; } SDL_SetModState((SDL_Keymod)mod); g.event(e); SDL_SetModState(KMOD_NONE); };
    auto clickAt = [&](const char* id, Uint16 mod = 0) { frame(); auto r = rect(id); float x = r[0] + r[2] / 2, y = r[1] + r[3] / 2; mouse(x, y, 0); mouse(x, y, 1, mod); mouse(x, y, 2, mod); g.update(1.f / 60); frame(); };
    auto label = [&] { return U.get("label")["text"].get<std::string>(); };
    frame(); frame();
    CHECK(!g.paused() && !U.modal(), "no menu open: the game runs and has the mouse");
    auto br = rect("open"); CHECK(std::abs(br[0] + br[2] / 2 - 960) < 1 && std::abs(br[1] + br[3] - 1040) < 1, "button anchored at the bottom centre, 40 units up");
    clickAt("open"); CHECK(U.get("inv")["visible"].get<bool>(), "clicking the button opens the window (handler ran)");
    run(g, 0.3f); frame();
    CHECK(g.paused() && !g.wantsMouseLock(), "an open menu pauses the game and frees the mouse");
    auto s0 = rect("s0"), s1 = rect("s1"), s3 = rect("s3");
    CHECK(s1[0] < s0[0] && std::abs(s0[0] - s1[0] - 90) < 0.5f && std::abs(s3[1] - s0[1] - 90) < 0.5f && std::abs(s0[2] - 80) < 0.5f, "grid: 80 cells, 10 gap, right to left (ui.rtl)");
    { mouse(480, 270, 0); float m[4]; U.mouse(m); CHECK(std::abs(m[0] - 480) < 0.5f && std::abs(m[1] - 270) < 0.5f && std::abs(m[2] - 1920) < 0.5f && std::abs(m[3] - 1080) < 0.5f, "ui.mouse: the mouse and the screen in UI units"); }
    clickAt("s4", KMOD_LSHIFT); CHECK(label() == "slot s4 shift", ("click bubbles to the grid with the slot and Shift: " + label()).c_str());
    { frame(); auto a0 = rect("s0"), a2 = rect("s2"); mouse(a0[0] + 20, a0[1] + 20, 0); mouse(a0[0] + 20, a0[1] + 20, 1);
      for (int i = 1; i <= 5; i++) { mouse(a0[0] + 20 + (a2[0] - a0[0]) * i / 5, a0[1] + 20, 0); frame(); }
      mouse(a2[0] + 20, a2[1] + 20, 2); g.update(1.f / 60); frame(); }
    CHECK(label() == "drop s0 > s2 0", ("drag and drop: " + label()).c_str());
    clickAt("name"); CHECK(g.uiTyping(), "clicking a text field gives it the keyboard");
    { SDL_Event t{}; t.type = SDL_TEXTINPUT; std::strcpy(t.text.text, "\xd8\xb9\xd8\xa7\xd9\x84\xd9\x85x"); g.event(t);
      SDL_Event k{}; k.type = SDL_KEYDOWN; k.key.keysym.sym = SDLK_BACKSPACE; g.event(k); k.key.keysym.sym = SDLK_RETURN; g.event(k);
      SDL_Event w = keyEv(SDL_KEYDOWN, SDL_SCANCODE_W); w.key.keysym.sym = SDLK_w; g.event(w); g.update(1.f / 60); }
    CHECK(label() == "name \xd8\xb9\xd8\xa7\xd9\x84\xd9\x85", ("typing Arabic, backspace, Enter: " + label()).c_str());
    CHECK(!g.key("KeyW"), "keys typed in a text field don't reach the game");
    { frame(); auto r = rect("vol"); mouse(r[0] + 5, r[1] + r[3] / 2, 0); mouse(r[0] + 5, r[1] + r[3] / 2, 1); mouse(r[0] + r[2] - 2, r[1] + r[3] / 2, 0); mouse(r[0] + r[2] - 2, r[1] + r[3] / 2, 2); g.update(1.f / 60); }
    CHECK(label() == "vol 10" || label() == "vol 0", ("slider dragged to an end: " + label()).c_str());
    { frame(); auto r = rect("tabs"); float hx = r[0] + r[2] * 0.25f, hy = r[1] + 10; mouse(hx, hy, 0); mouse(hx, hy, 1); mouse(hx, hy, 2); g.update(1.f / 60); frame(); }
    CHECK(label() == "tab 1" && U.get("page1")["visible"].get<bool>() && !U.get("page0")["visible"].get<bool>(), ("tabs (right to left: the left tab is the second): " + label()).c_str());
    U.hide("inv", nlohmann::json::object()); run(g, 0.3f);
    { frame(); auto r = rect("list"); SDL_Event wv{}; wv.type = SDL_MOUSEWHEEL; wv.wheel.y = -2; wv.wheel.preciseY = -2; wv.wheel.mouseX = (int)(r[0] + 50); wv.wheel.mouseY = (int)(r[1] + 50); g.event(wv);
      for (int i = 0; i < 3; i++) frame(); float sc = U.get("list")["scroll"].get<float>(); CHECK(sc > 50 && sc < 2000, ("a list scrolls with the wheel: " + std::to_string(sc)).c_str()); }
    U.show("inv", nlohmann::json::object()); run(g, 0.3f); frame();
    U.hide("inv", nlohmann::json::object()); run(g, 0.3f); frame();
    CHECK(!g.paused() && !U.modal() && !U.get("inv")["visible"].get<bool>(), "closing the menu (garage animation) gives the game back");
    clickAt("bad"); CHECK(hasLog("ui \"click\" on bad"), "an error in a UI handler is in the Output Log");
    ImGui::DestroyContext();
  }

  {   // player movement settings (fly, speeds), the game's saved values (storage) and onStop
    Project q = makeTemplate("Blocks", "StorageTest"); q.scripts["SC_BlockWorld"].code =
      "function onStart() { voxels.create({ bottom: -8, top: 16, nearLayer: [-2, 8], viewDistance: 2, seed: 3 }); voxels.defineBlock(1, { color: '#888' }); voxels.setGenerator('flat', { ground: 0, top: 1, dirt: 1, stone: 1 });\n"
      "  storage.remove('t'); storage.setJSON('t', { a: [1, 2], s: '\\u0639\\u0627\\u0644\\u0645' }); world.vars.st = storage.getJSON('t').a[1] === 2 && storage.getJSON('t').s.length === 4 && storage.keys('t').indexOf('t') >= 0 && storage.get('nope') === null;\n"
      "  world.player.set({ walkSpeed: 4.317, sneakSpeed: 1.31, runKey: 'Control', sneakKey: 'Shift', jumpSpeed: 8.4, gravity: -32 }); world.player.fly = true; }\n"
      "function onUpdate() { world.hud('st', String(world.vars.st) + ' ' + world.player.fly + ' ' + world.player.settings.walkSpeed); }\n"
      "function onStop() { storage.set('stopped', 'yes'); }";
    Game g(q, "Main"); logClear(); CHECK(g.start(), "storage test starts");
    for (int i = 0; i < 600 && !(g.voxels() && g.voxels()->ready(g.playerPos())); i++) { g.update(1.f / 60); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    run(g, 0.5f); glm::vec3 a = g.playerPos(); run(g, 1.0f); glm::vec3 b = g.playerPos();
    CHECK(std::abs(b.y - a.y) < 0.05f, ("flying: the player doesn't fall (" + std::to_string(a.y) + " -> " + std::to_string(b.y) + ")").c_str());
    { SDL_Event k{}; k.type = SDL_KEYDOWN; k.key.keysym.scancode = SDL_SCANCODE_SPACE; g.event(k); run(g, 0.5f); k.type = SDL_KEYUP; g.event(k); g.update(1.f / 60); }
    glm::vec3 c = g.playerPos(); CHECK(c.y > b.y + 2.f, ("flying: Space goes up (" + std::to_string(c.y - b.y) + " m)").c_str());
    std::string hud = g.playerGet(); CHECK(hud.find("\"fly\":true") != std::string::npos && hud.find("4.31") != std::string::npos, "world.player.set and .fly");
    CHECK(g.storage("get", "t", "").find("[1,2]") != std::string::npos, "storage.setJSON writes the game's file");
    g.playerSet("{\"fly\":false}"); run(g, 1.5f); CHECK(g.playerPos().y < c.y - 1.f, "fly off: the player falls again");
    int errs = 0; for (auto& l : logLines()) errs += l.kind == LogKind::Error; CHECK(errs == 0, "no script errors");
    std::string st; for (auto& [id, t] : g.hudLines()) if (id == "st") st = t; CHECK(st.rfind("true ", 0) == 0, ("storage round trip, keys, missing = null: " + st).c_str());
    g.storage("remove", "stopped", ""); g.start(); CHECK(g.storage("get", "stopped", "") == "yes", "onStop runs when the game stops or restarts");
    { g.voxels()->defineBlock(9, [] { BlockType t; t.defined = true; t.shape = SH_Cross; return t; }());
      g.voxels()->setItems({1, 0, 1, 0, .2f, .2f, .2f, .5f, 0, -1, 1, 1, 1,   9, 1, 1, 0, .3f, .3f, .3f, 0, 0, -1, 1, 1, 1,   1, 0, 1, 0, .4f, .4f, .4f, 0, 0, 5, 1, 1, 1});
      CHECK(g.voxels()->itemIndices() == 36 + 24 + 36, ("voxels.items: two boxes and a crossed picture (" + std::to_string(g.voxels()->itemIndices()) + " indices)").c_str());
      g.voxels()->setItems({}); CHECK(g.voxels()->itemIndices() == 0, "voxels.items([]) clears them"); }
    g.storage("remove", "stopped", ""); g.storage("remove", "t", "");
  }

  std::printf("sound\n");
  {
    auto rms = [](const std::vector<float>& v, size_t a = 0, size_t b = 0) { if (!b) b = v.size(); double s = 0; for (size_t i = a; i < b; i++) s += v[i] * v[i]; return std::sqrt(s / std::max<size_t>(1, b - a)); };
    std::vector<float> out; std::string e = synthRender({{"type", "sine"}, {"freq", 440}, {"duration", 0.5}}, out);
    int zc = 0; for (size_t i = 1; i < out.size(); i++) zc += (out[i - 1] < 0) != (out[i] < 0);
    CHECK(e.empty() && out.size() == 22050 && std::abs(rms(out) - 0.7071) < 0.01 && std::abs(zc - 440) <= 2, ("a 440 Hz sine made by code: " + std::to_string(zc) + " crossings, rms " + std::to_string(rms(out))).c_str());
    synthRender({{"type", "square"}, {"freq", json::array({880, 110})}, {"duration", 0.4}, {"gain", {{"attack", 0.01}, {"decay", 0.1}, {"sustain", 0.3}, {"release", 0.1}}}}, out);
    CHECK(rms(out, 0, 400) < rms(out, 2000, 3000) && rms(out, 17000, 17400) < rms(out, 9000, 9400) + 1e-6 && std::abs(out.back()) < 0.02, "envelope: attack, decay, sustain, release");
    std::vector<float> raw, low; synthRender({{"type", "noise"}, {"duration", 0.3}, {"seed", 7}}, raw);
    synthRender({{"type", "noise"}, {"duration", 0.3}, {"seed", 7}, {"filter", {{"type", "lowpass"}, {"freq", 400}}}}, low);
    CHECK(rms(low) < rms(raw) * 0.4, ("noise through a 400 Hz lowpass is much quieter: " + std::to_string(rms(low) / rms(raw))).c_str());
    std::vector<float> mixd; synthRender({{"voices", json::array({json{{"type", "sine"}, {"freq", 220}, {"length", 0.2}}, json{{"type", "triangle"}, {"freq", 330}, {"start", 0.2}, {"length", 0.2}}})}, {"echo", {{"time", 0.1}}}}, mixd);
    CHECK(mixd.size() > (size_t)(0.4 * 44100) && rms(mixd, 0, 8000) > 0.3 && rms(mixd, 9500, 17000) > 0.3, "voices one after the other, with an echo tail");
    CHECK(!synthRender({{"type", "kazoo"}}, out).empty() && !synthRender({{"type", "sine"}, {"duration", 99}}, out).empty(), "a bad sound description says what is wrong");

    Audio au(true); CHECK(au.ok(), "the mixer starts without a sound card (tests)");
    CHECK(au.make("beep", {{"type", "sine"}, {"freq", 500}, {"duration", 1}}).empty(), "sound.make keeps a sound by name");
    std::vector<float> st(2 * 4800); int h = au.play("beep", json::object()); au.mix(st.data(), 4800);
    auto chan = [&](int c) { double s = 0; for (size_t i = c; i < st.size(); i += 2) s += st[i] * st[i]; return std::sqrt(s / (st.size() / 2)); };
    CHECK(h > 0 && chan(0) > 0.3 && std::abs(chan(0) - chan(1)) < 0.05, "it plays (both ears the same)");
    au.setVolume("sfx", 0); au.mix(st.data(), 4800); CHECK(chan(0) < 1e-4, "a group's volume (sfx = 0) mutes it"); au.setVolume("sfx", 1);
    au.setVolume("master", 0.5f); au.mix(st.data(), 4800); double half = chan(0); au.setVolume("master", 1); au.mix(st.data(), 4800);
    CHECK(std::abs(half / chan(0) - 0.5) < 0.05, "the game's volume");
    au.pause(true); au.mix(st.data(), 4800); double ps = chan(0); au.pause(false); au.mix(st.data(), 4800);
    CHECK(ps < 0.01 && chan(0) > 0.3, ("pause stops it where it is, resume goes on: " + std::to_string(ps) + " / " + std::to_string(chan(0))).c_str());
    au.stop(h); CHECK(!au.playing(h) && au.count() == 0, "stop");
    // 3D: the ear at the origin looking down -Z, the sound 4 m to the right
    au.update({0, 0, 0}, {0, 0, -1}, nullptr); int h3 = au.play("beep", {{"position", {4, 0, 0}}}); au.mix(st.data(), 4800);
    double l = chan(0), r = chan(1); CHECK(h3 > 0 && r > l * 1.5, ("a sound on the right is louder in the right ear: " + std::to_string(l) + " / " + std::to_string(r)).c_str());
    au.set(h3, {{"position", {40, 0, 0}}}); au.mix(st.data(), 4800); CHECK(chan(1) < r * 0.5, "and quieter far away");
    au.stop(h3, 0.05f); for (int i = 0; i < 4; i++) { au.mix(st.data(), 4800); au.update({0, 0, 0}, {0, 0, -1}, nullptr); } CHECK(au.count() == 0, "stop with a fade, then it is gone");
    // a .wav file
    std::string wav = (std::filesystem::temp_directory_path() / "pge-test.wav").string();
    { FILE* f = std::fopen(wav.c_str(), "wb"); int n = 22050, rate = 22050; auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); }; auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
      std::fwrite("RIFF", 1, 4, f); w32(36 + n * 2); std::fwrite("WAVEfmt ", 1, 8, f); w32(16); w16(1); w16(1); w32(rate); w32(rate * 2); w16(2); w16(16); std::fwrite("data", 1, 4, f); w32(n * 2);
      for (int i = 0; i < n; i++) { int16_t v = (int16_t)(std::sin(i * 0.1) * 12000); std::fwrite(&v, 2, 1, f); } std::fclose(f); }
    int hw = au.play(wav, json::object()); au.mix(st.data(), 4800); CHECK(hw > 0 && chan(0) > 0.15, "plays a .wav file");
    { int ho = au.play(std::string(PGE_TEST_DATA) + "/tone.ogg", json::object()); au.mix(st.data(), 4800); CHECK(ho > 0 && chan(0) > 0.2, ("plays an .ogg file (Vorbis): " + std::to_string(chan(0))).c_str()); au.stop(ho); }
    std::string why; CHECK(au.play("nope.ogg", json::object(), &why) == 0 && !why.empty(), ("a missing file: " + why).c_str());
    au.stopAll(); std::filesystem::remove(wav);

    g_headlessAudio = true;
    Project q = makeTemplate("Blank", "T");
    q.scripts["SC_SND"] = {"SC_SND", R"JS(
function onStart() {
  world.vars.made = sound.make('jump', { type: 'square', freq: [300, 900], duration: 0.2, gain: { attack: 0.005, release: 0.1 }, filter: { type: 'lowpass', freq: 3000 } });
  world.vars.h = sound.play('jump', { volume: 0.8, follow: self });
  world.vars.loop = sound.play({ voices: [{ type: 'brown', duration: 1 }] }, { loop: true, bus: 'music', position: { x: 1, y: 0, z: 0 } });
  const s = new Float32Array(4410); for (let i = 0; i < s.length; i++) s[i] = Math.sin(i * 0.2) * 0.5;
  world.vars.raw = sound.play(s);
  world.vars.bad = sound.play('missing.ogg');
  sound.setVolume('music', 0.3); world.vars.vol = sound.getVolume('music');
}
)JS"};
    Actor sa = newActor("Empty", "Speaker"); sa.script = "SC_SND"; q.levels[0].second.actors.push_back(sa);
    Game g(q, q.startLevel); logClear(); CHECK(g.start(), "a game with sound starts");
    Audio* A = g.audio(); run(g, 0.05f);
    CHECK(A && A->count() == 3, ("sound.play: made, follows its actor, loops, samples from a Float32Array: " + std::to_string(A ? A->count() : -1) + " playing").c_str());
    CHECK(hasLog("sound: can't play 'missing.ogg'"), "a sound that isn't there is a warning");
    { std::vector<float> buf(2 * 800); for (int i = 0; i < 40; i++) { A->mix(buf.data(), 800); g.update(1.f / 60); } }
    CHECK(A->count() == 1, ("finished sounds go; the loop stays: " + std::to_string(A->count())).c_str());
    g.start(); run(g, 0.02f); CHECK(g.audio()->count() == 3, "restarting the level stops the old sounds (the new ones play)");
    g_headlessAudio = false;
  }

  std::printf("models and particles\n");
  {
    Models M;
    json pig = {{"parts", {
      {"body", {{"pivot", {0, 0.7, 0}}, {"boxes", json::array({json{{"size", {0.6, 0.5, 1.0}}, {"color", "#f0a0a0"}}})}}},
      {"head", {{"parent", "body"}, {"pivot", {0, 0.15, -0.5}}, {"boxes", json::array({json{{"size", {0.5, 0.5, 0.4}}, {"pos", {0, 0, -0.2}}, {"tile", {{"side", 3}, {"top", 4}}}}})}}},
      {"legFL", {{"parent", "body"}, {"pivot", {-0.18, -0.25, -0.35}}, {"boxes", json::array({json{{"size", {0.18, 0.45, 0.18}}, {"pos", {0, -0.225, 0}}}})}}},
      {"legFR", {{"parent", "body"}, {"pivot", {0.18, -0.25, -0.35}}, {"boxes", json::array({json{{"size", {0.18, 0.45, 0.18}}, {"pos", {0, -0.225, 0}}}})}}},
      {"tri", {{"parent", "head"}, {"mesh", {{"positions", {0, 0, 0, 0.1, 0, 0, 0, 0.1, 0}}, {"color", "#ffffff"}}}}}}}};
    CHECK(M.define("pig", pig).empty(), "a model of parts: boxes with colours and tiles, a part of vertices");
    std::string id; CHECK(M.spawn("pig", {{"position", {10, 0, 5}}}, id).empty() && M.drawCount() == 5, "spawned: each part drawn");
    bool ok; glm::vec3 h = glm::vec3(M.partWorld(id, "head", &ok)[3]);
    CHECK(ok && glm::distance(h, glm::vec3(10, 0.85f, 4.5f)) < 1e-4f, "the head sits at its pivot on the body");
    M.set(id, {{"rotation", {0, 1.5707963, 0}}}); h = glm::vec3(M.partWorld(id, "head")[3]);
    CHECK(glm::distance(h, glm::vec3(9.5f, 0.85f, 5.f)) < 1e-3f, "turning the model turns its parts with it (like three.js: radians)");
    M.set(id, {{"rotation", {0, 0, 0}}, {"parts", {{"legFL", {{"rotation", {0.5, 0, 0}}}}}}});
    glm::vec3 foot = glm::vec3(M.partWorld(id, "legFL") * glm::vec4(0, -0.45f, 0, 1)), still = glm::vec3(M.partWorld(id, "legFR") * glm::vec4(0, -0.45f, 0, 1));
    CHECK(foot.z < still.z - 0.1f && std::abs(still.z - (5 - 0.35f)) < 1e-4f, "a leg turns about its hip, the other stays");
    CHECK(!M.set(id, {{"parts", {{"tail", {{"rotation", {1, 0, 0}}}}}}}).empty(), "a part that isn't there says so");
    M.set(id, {{"parts", {{"head", {{"visible", false}}}}}}); CHECK(M.drawCount() == 3, "hiding a part hides its children");
    CHECK(!M.define("bad", {{"parts", {{"a", {{"parent", "nope"}}}}}}).empty() && !M.spawn("nope", json::object(), id).empty(), "bad models say what is wrong");
    M.remove(id); CHECK(M.instanceCount() == 0 && M.drawCount() == 0, "removed");

    Physics ph; ph.init(-9.81f);
    VoxelConfig cfg; cfg.minY = -8; cfg.maxY = 24; cfg.fineLo = -4; cfg.fineHi = 8; cfg.viewDistance = 1; cfg.threads = 2;
    VoxelWorld w(cfg, &ph); BlockType rock; w.defineBlock(3, rock);
    w.setBuiltinGenerator("flat", R"({"ground": 0, "top": 3, "dirt": 3, "stone": 3})"); w.update({8, 0, 8}); w.waitIdle();
    M.setBlocks(&w);
    CHECK(M.emit({{"position", {8, 3, 8}}, {"count", 40}, {"block", 3}, {"life", {3, 3}}, {"speed", {1, 2}}}).empty() && M.particleCount() == 40, "particles from a block");
    for (int i = 0; i < 120; i++) M.update(1.f / 60);
    int low = 0, above = 0; (void)above;
    M.emit({{"position", {8, 3, 8}}, {"count", 1}, {"speed", 0}, {"life", 5}, {"size", 0.1}});
    for (int i = 0; i < 180; i++) M.update(1.f / 60);
    CHECK(M.particleCount() == 1, "the others are gone when their life is over");
    { json dummy; (void)dummy; }
    Frame f; M.draw(f); CHECK(!f.voxels.empty(), "particles are drawn with the block world");
    (void)low;
    M.clearParticles(); CHECK(M.particleCount() == 0, "particles.clear");

    Project q = makeTemplate("Blank", "T");
    q.scripts["SC_M"] = {"SC_M", R"JS(
let m, t = 0;
function onStart() {
  models.define('box', { parts: { base: { boxes: [{ size: [1, 1, 1], color: '#44aa66' }] }, lid: { parent: 'base', pivot: [0, 0.5, 0.5], boxes: [{ size: [1, 0.1, 1], pos: [0, 0.05, -0.5] }] } } });
  m = models.spawn('box', { position: { x: 1, y: 2, z: 3 } });
  world.vars.bad = models.spawn('nothing') === null;
  particles.emit({ position: [0, 5, 0], count: 5, color: '#ffaa00', glow: 1.5, collide: false });
}
function onUpdate(dt) { t += dt; m.pose({ lid: { rotation: [t, 0, 0] } }); const p = m.part('lid'); world.vars.lidY = p.position.y; }
)JS"};
    Actor ma = newActor("Empty", "Chest"); ma.script = "SC_M"; q.levels[0].second.actors.push_back(ma);
    Game g(q, q.startLevel); logClear(); CHECK(g.start(), "a game with models starts"); run(g, 0.5f);
    Models* GM = g.models(); bool ok2 = false; glm::mat4 lid = GM ? GM->partWorld("m1", "lid", &ok2) : glm::mat4(1);
    CHECK(GM && GM->instanceCount() == 1 && ok2 && glm::distance(glm::vec3(lid[3]), glm::vec3(1, 2.5f, 3.5f)) < 1e-4f, "models.spawn from a script; the lid's hinge stays at its pivot");
    CHECK(glm::vec3(lid * glm::vec4(0, 0, -1, 1)).y > 2.7f, "the lid opens (turned every frame by the script)");
    CHECK(hasLog("models.spawn: no model 'nothing'"), "a model that isn't there is a warning");
    CHECK(GM->particleCount() == 5, "particles.emit from a script");
  }

  std::printf("controller and console menus\n");
  {
    ImGui::CreateContext(); ImGuiIO& io = ImGui::GetIO(); io.Fonts->AddFontDefault(); { ImFontConfig b; b.SizePixels = 39; io.Fonts->AddFontDefault(&b); }
    unsigned char* px; int fw, fh; io.Fonts->GetTexDataAsRGBA32(&px, &fw, &fh); io.DisplaySize = ImVec2(1920, 1080); io.DeltaTime = 1.f / 60;
    Project q = makeTemplate("FirstPerson", "T");
    q.scripts["SC_P"] = {"SC_P", R"JS(
const S = { keys: '' };
function put(k, v) { S[k] = v; ui.get('out').text = JSON.stringify(S); }
function onKey(k) { if (k === 'PadA' || k === 'Space' || k === 'Escape') put('keys', S.keys + k + ','); }
function onStart() {
  ui.create('text', { id: 'out', text: '{}', anchor: 'top-left', size: 12 });
  const w = ui.create('window', { id: 'menu', title: 'Menu', w: 700, h: 640, modal: true, visible: false, layout: 'column', gap: 12 });
  const row = w.add('panel', { layout: 'row', gap: 10 });
  row.add('button', { id: 'b1', text: 'One', on: { click: () => put('clicked', 'one') } });
  row.add('button', { id: 'b2', text: 'Two', on: { click: e => put('clicked', 'two' + (e.pad ? ' pad' : '')) } });
  const slots = w.add('grid', { id: 'slots', cols: 4, cell: 70, gap: 8 });
  for (let i = 0; i < 4; i++) slots.add('panel', { id: 's' + i, bg: '#ffffff20', draggable: true, droppable: true, data: { i } });
  slots.on('drop', e => put('drop', e.from + '>' + e.target.id));
  w.add('input', { id: 'name', placeholder: 'Name', on: { submit: e => put('name', e.value) } });
  w.on('back', () => put('back', true));
}
)JS"};
    Actor pa = newActor("Empty", "PadTest"); pa.script = "SC_P"; q.levels[0].second.actors.push_back(pa);
    Game g(q, q.startLevel); logClear(); CHECK(g.start(), "a game for the controller starts");
    Ui& U = *g.ui();
    auto frame = [&] { ImGui::NewFrame(); g.drawHud(ImGui::GetBackgroundDrawList(), 0, 0, 1920, 1080); ImGui::EndFrame(); };
    auto btn = [&](int b, bool down) { SDL_Event e{}; e.type = down ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP; e.cbutton.button = (Uint8)b; g.event(e); };
    auto tap = [&](int b) { btn(b, true); g.update(1.f / 60); frame(); btn(b, false); g.update(1.f / 60); frame(); };
    auto stick = [&](int a, float v) { SDL_Event e{}; e.type = SDL_CONTROLLERAXISMOTION; e.caxis.axis = (Uint8)a; e.caxis.value = (Sint16)(v * 32767); g.event(e); };
    auto out = [&] { json o = json::parse(U.get("out")["text"].get<std::string>(), nullptr, false); return o.is_object() ? o : json::object(); };
    run(g, 0.5f);
    glm::vec3 p0 = g.playerPos(); stick(SDL_CONTROLLER_AXIS_LEFTY, -1); run(g, 1.f); stick(SDL_CONTROLLER_AXIS_LEFTY, 0);
    CHECK(glm::length(glm::vec2(g.playerPos().x - p0.x, g.playerPos().z - p0.z)) > 3.f, "the left stick walks");
    float yaw0 = json::parse(g.playerGet())["yaw"].get<float>(); stick(SDL_CONTROLLER_AXIS_RIGHTX, 1); run(g, 0.5f); stick(SDL_CONTROLLER_AXIS_RIGHTX, 0);
    CHECK(json::parse(g.playerGet())["yaw"].get<float>() < yaw0 - 0.5f, "the right stick turns");
    run(g, 0.4f); float y0 = g.playerPos().y; btn(SDL_CONTROLLER_BUTTON_A, true); run(g, 0.25f); btn(SDL_CONTROLLER_BUTTON_A, false);
    CHECK(g.playerPos().y > y0 + 0.4f, "A jumps");
    CHECK(out().value("keys", "").find("PadA,Space,") != std::string::npos, ("onKey hears PadA and Space (the alias): " + out().value("keys", std::string())).c_str());
    stick(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 0.95f);
    CHECK(g.key("PadRT") && g.key("MouseLeft") && g.mouse().z > 0, "RT is the left mouse button too (PC games play on a controller)");
    stick(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 0); run(g, 0.05f); CHECK(!g.key("MouseLeft"), "and lets go");
    U.show("menu", json::object()); run(g, 0.1f); frame(); frame();
    tap(SDL_CONTROLLER_BUTTON_DPAD_DOWN);   // the first press puts the focus on the first button
    tap(SDL_CONTROLLER_BUTTON_DPAD_RIGHT); tap(SDL_CONTROLLER_BUTTON_A);
    CHECK(out().value("clicked", "") == "two pad", ("D-pad right, A: " + out().value("clicked", std::string())).c_str());
    tap(SDL_CONTROLLER_BUTTON_DPAD_DOWN); tap(SDL_CONTROLLER_BUTTON_A);   // pick up a slot
    tap(SDL_CONTROLLER_BUTTON_DPAD_RIGHT); tap(SDL_CONTROLLER_BUTTON_A);   // put it on the next one
    CHECK(out().value("drop", "").size() > 3 && out().value("drop", "").find('>') != std::string::npos && out()["drop"].get<std::string>().substr(0, 2) != out()["drop"].get<std::string>().substr(3), ("pick up with A, put down with A: " + out().value("drop", std::string())).c_str());
    for (int i = 0; i < 3; i++) tap(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    tap(SDL_CONTROLLER_BUTTON_A);
    CHECK(U.keyboardOpen() && U.exists("__osk"), "A on a text field opens the on-screen keyboard");
    tap(SDL_CONTROLLER_BUTTON_DPAD_DOWN); tap(SDL_CONTROLLER_BUTTON_A); tap(SDL_CONTROLLER_BUTTON_DPAD_RIGHT); tap(SDL_CONTROLLER_BUTTON_A);
    std::string typed = U.get("name")["value"].get<std::string>();
    CHECK(typed.size() == 2, ("typing with the controller: '" + typed + "'").c_str());
    tap(SDL_CONTROLLER_BUTTON_Y);
    CHECK(!U.keyboardOpen() && out().value("name", "") == typed, "Y: done, the keyboard closes and the field is submitted");
    tap(SDL_CONTROLLER_BUTTON_B);
    CHECK(out().value("back", false) && out().value("keys", "").find("Escape") != std::string::npos, "B: the menu hears 'back' and the game hears Escape (its own close)");
    ImGui::DestroyContext();
  }

  std::printf("\n%d passed, %d failed\n", passes, fails);
  return fails ? 1 : 0;
}
