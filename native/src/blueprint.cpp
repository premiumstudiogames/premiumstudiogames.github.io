// Blueprints: the node library (English + Arabic), the graph's file format, and the compiler that turns a graph into
// the engine's script code (JavaScript). See blueprint.h.
#include "blueprint.h"
#include <set>
#include <sstream>
#include <functional>
#include <algorithm>
#include <cmath>
#include <cctype>

namespace pge {
using json = nlohmann::json;

const char* pinTypeId(PinType t) {
  switch (t) { case PinType::Exec: return "exec"; case PinType::Bool: return "bool"; case PinType::Number: return "number"; case PinType::String: return "string";
    case PinType::Vector: return "vector"; case PinType::Actor: return "actor"; case PinType::Array: return "array"; default: return "any"; }
}
PinType pinTypeFrom(const std::string& s) {
  if (s == "exec") return PinType::Exec; if (s == "bool") return PinType::Bool; if (s == "number" || s == "float" || s == "int") return PinType::Number;
  if (s == "string") return PinType::String; if (s == "vector") return PinType::Vector; if (s == "actor") return PinType::Actor; if (s == "array") return PinType::Array;
  return PinType::Any;
}
const char* pinTypeEn(PinType t) {
  switch (t) { case PinType::Exec: return "Exec"; case PinType::Bool: return "Boolean"; case PinType::Number: return "Float"; case PinType::String: return "String";
    case PinType::Vector: return "Vector"; case PinType::Actor: return "Actor"; case PinType::Array: return "Array"; default: return "Wildcard"; }
}
const char* pinTypeAr(PinType t) {
  switch (t) { case PinType::Exec: return "تنفيذ"; case PinType::Bool: return "منطقي (صح/خطأ)"; case PinType::Number: return "رقم"; case PinType::String: return "نص";
    case PinType::Vector: return "متجه (X Y Z)"; case PinType::Actor: return "ممثل"; case PinType::Array: return "قائمة"; default: return "أي نوع"; }
}
bool pinCanLink(PinType from, PinType to) {
  if (from == PinType::Exec || to == PinType::Exec) return from == to;
  if (from == to || from == PinType::Any || to == PinType::Any) return true;
  if (to == PinType::String) return true;                       // anything becomes text (like Unreal's automatic To String)
  return false;
}

/* ------------------------------------------------------------------ the node library */
static BpPinDef P(const std::string& spec) {
  BpPinDef p; p.out = spec[0] == '<'; std::string s = spec.substr(1);
  size_t at = s.find('@'); if (at != std::string::npos) { p.code = s.substr(at + 1); s = s.substr(0, at); }
  size_t eq = s.find('='); std::string def; bool hasDef = eq != std::string::npos; if (hasDef) { def = s.substr(eq + 1); s = s.substr(0, eq); }
  size_t c = s.find(':'); p.id = s.substr(0, c); p.type = c == std::string::npos ? PinType::Exec : pinTypeFrom(s.substr(c + 1));
  if (hasDef) { try { p.def = json::parse(def); } catch (...) { p.def = def; } }
  else switch (p.type) {
    case PinType::Bool: p.def = false; break; case PinType::Number: p.def = 0; break; case PinType::String: p.def = ""; break;
    case PinType::Vector: p.def = json::array({0, 0, 0}); break; default: break; }
  return p;
}
static std::vector<BpNodeDef> buildLibrary() {
  std::vector<BpNodeDef> L;
  auto add = [&](const std::string& type, const std::string& cat, BpNodeDef::Kind kind, const std::string& en, const std::string& ar, const std::string& descEn, const std::string& descAr,
                 std::vector<std::string> pins, std::string code = "", std::vector<BpFieldDef> fields = {}) {
    BpNodeDef d; d.type = type; d.cat = cat; d.kind = kind; d.en = en; d.ar = ar; d.descEn = descEn; d.descAr = descAr; d.code = code; d.fields = fields;
    for (auto& p : pins) d.pins.push_back(P(p)); L.push_back(d); };
  const auto E = BpNodeDef::Event, X = BpNodeDef::Exec, U = BpNodeDef::Pure;
  const std::string T = ">Target:actor=\"self\"";   // like Unreal: Target = self when nothing is wired

  // ---- events
  add("Event.BeginPlay", "Events", E, "Event BeginPlay", "حدث بدء اللعب", "Runs once, when the game starts.", "يشتغل مرة وحدة لما تبدأ اللعبة.", {"<then"});
  add("Event.Tick", "Events", E, "Event Tick", "حدث كل إطار", "Runs every frame. Delta Seconds = the time since the last frame (use it so speed doesn't depend on the frame rate).",
      "يشتغل كل إطار (عشرات المرات بالثانية). Delta Seconds = الوقت من الإطار اللي قبله، استخدمه عشان السرعة ما تتغير مع سرعة الجهاز.", {"<then", "<DeltaSeconds:number"});
  add("Event.ActorBeginOverlap", "Events", E, "Event ActorBeginOverlap", "حدث بداية التداخل", "The player walked into this actor (a Trigger Box or a mesh).",
      "اللاعب دخل في هذا الممثل (صندوق تفعيل أو مجسم).", {"<then", "<OtherActor:actor"});
  add("Event.Hit", "Events", E, "Event Hit", "حدث الاصطدام", "A physics body hit this actor (needs collision).", "جسم فيزيائي اصطدم بهذا الممثل (يحتاج التصادم شغال).", {"<then", "<OtherActor:actor"});
  add("Event.KeyPressed", "Events", E, "Key Pressed", "عند ضغط زر", "Runs when this key is pressed. Keys: KeyE, Space, Digit1, MouseLeft, WheelUp, PadA (controller)...",
      "يشتغل لما ينضغط هذا الزر. أمثلة: KeyE حرف E، Space المسافة، Digit1 الرقم 1، MouseLeft زر الماوس اليسار، PadA زر A في يد التحكم.", {"<then"}, "", {{"Key", "KeyE"}});
  add("Event.AnyKey", "Events", E, "Any Key Pressed", "عند ضغط أي زر", "Runs when any key is pressed. Key = which one.", "يشتغل مع أي زر ينضغط. Key = اسم الزر.", {"<then", "<Key:string"});
  add("Event.EndPlay", "Events", E, "Event EndPlay", "حدث نهاية اللعب", "Runs when the game stops or the level restarts (save your things here).", "يشتغل لما تتوقف اللعبة أو تعيد المرحلة (احفظ أشياءك هنا).", {"<then"});
  add("Event.Custom", "Events", E, "Custom Event", "حدث مخصص", "Your own event with a name. Run it with Call Custom Event or a timer.", "حدث تسميه أنت. تشغله بعقدة Call Custom Event أو بمؤقت.", {"<then"}, "", {{"Name", "MyEvent"}});
  add("Event.WidgetClicked", "Events", E, "On Widget Clicked", "عند الضغط على عنصر واجهة", "Runs when a game UI button (made with ui.create) with this id is clicked.",
      "يشتغل لما ينضغط زر في واجهة اللعبة بهذا المعرّف (id).", {"<then"}, "", {{"Widget", "playButton"}});

  // ---- flow control
  add("Flow.Branch", "Flow Control", X, "Branch", "تفرّع (إذا)", "If Condition is true, True runs; else False runs.", "إذا الشرط صحيح يشتغل True، وإذا خطأ يشتغل False.",
      {">exec", ">Condition:bool", "<True", "<False"}, "if ($in(Condition)) {\n$exec(True)\n} else {\n$exec(False)\n}");
  add("Flow.Sequence", "Flow Control", X, "Sequence", "تسلسل", "Runs Then 0, then Then 1, then Then 2, then Then 3, in order.", "يشغّل المخارج بالترتيب: Then 0 ثم Then 1 ثم Then 2 ثم Then 3.",
      {">exec", "<Then0", "<Then1", "<Then2", "<Then3"}, "$exec(Then0)\n$exec(Then1)\n$exec(Then2)\n$exec(Then3)");
  add("Flow.ForLoop", "Flow Control", X, "For Loop", "حلقة For", "Runs Loop Body once for every Index from First Index to Last Index, then Completed.",
      "يشغّل Loop Body مرة لكل رقم من First Index إلى Last Index، وبعدها Completed.",
      {">exec", ">FirstIndex:number=0", ">LastIndex:number=9", "<LoopBody", "<Index:number", "<Completed"},
      "for (let __i$id = Math.round($in(FirstIndex)), __e$id = Math.round($in(LastIndex)); __i$id <= __e$id; __i$id++) {\n$out(Index) = __i$id;\n$exec(LoopBody)\n}\n$exec(Completed)");
  add("Flow.ForEach", "Flow Control", X, "For Each Loop", "حلقة لكل عنصر", "Runs Loop Body for every element of the array, then Completed.", "يشغّل Loop Body لكل عنصر في القائمة، وبعدها Completed.",
      {">exec", ">Array:array", "<LoopBody", "<ArrayElement:any", "<ArrayIndex:number", "<Completed"},
      "{ const __a$id = $in(Array) || [];\nfor (let __i$id = 0; __i$id < __a$id.length; __i$id++) {\n$out(ArrayElement) = __a$id[__i$id]; $out(ArrayIndex) = __i$id;\n$exec(LoopBody)\n} }\n$exec(Completed)");
  add("Flow.While", "Flow Control", X, "While Loop", "حلقة While", "Runs Loop Body again and again while Condition is true, then Completed. (An endless loop is stopped by the engine.)",
      "يكرر Loop Body طول ما الشرط صحيح، وبعدها Completed. (المحرك يوقف الحلقة اللي ما تنتهي).", {">exec", ">Condition:bool", "<LoopBody", "<Completed"},
      "while ($in(Condition)) {\n$exec(LoopBody)\n}\n$exec(Completed)");
  add("Flow.DoOnce", "Flow Control", X, "Do Once", "مرة وحدة", "Lets the flow through only the first time (until Reset).", "يمرّر التنفيذ أول مرة بس (لين تشغّل Reset).",
      {">exec", ">Reset@__bp.once[$id] = 0;", "<Completed"}, "if (!__bp.once[$id]) { __bp.once[$id] = 1;\n$exec(Completed)\n}");
  add("Flow.FlipFlop", "Flow Control", X, "Flip Flop", "تبديل", "Runs A, then B the next time, then A again...", "يشغّل A، والمرة اللي بعدها B، وبعدها A وهكذا.",
      {">exec", "<A", "<B", "<IsA:bool"}, "__bp.flip[$id] = !__bp.flip[$id]; $out(IsA) = __bp.flip[$id];\nif ($out(IsA)) {\n$exec(A)\n} else {\n$exec(B)\n}");
  add("Flow.Gate", "Flow Control", X, "Gate", "بوابة", "Enter goes through to Exit only while the gate is open. Open, Close and Toggle change it (it starts open).",
      "التنفيذ من Enter يطلع من Exit بس إذا البوابة مفتوحة. Open يفتحها، Close يقفلها، Toggle يعكسها (تبدأ مفتوحة).",
      {">Enter@if (__bp.gate[$id] !== false) {\n$exec(Exit)\n}", ">Open@__bp.gate[$id] = true;", ">Close@__bp.gate[$id] = false;", ">Toggle@__bp.gate[$id] = __bp.gate[$id] === false;", "<Exit"});
  add("Flow.Delay", "Flow Control", X, "Delay", "تأخير", "Waits Duration seconds, then runs Completed. While it waits, more calls are ignored.",
      "ينتظر عدد الثواني (Duration) وبعدها يشغّل Completed. وهو ينتظر، أي تشغيل ثاني ينتجاهل.",
      {">exec", ">Duration:number=1", "<Completed"}, "__bp.delay($id, $in(Duration), () => {\n$exec(Completed)\n});");
  add("Flow.SetTimer", "Flow Control", X, "Set Timer by Event", "مؤقت لحدث", "Runs the custom event after Time seconds (again and again if Looping).", "يشغّل الحدث المخصص بعد Time ثانية (ويكرر إذا Looping صحيح).",
      {">exec", ">Time:number=1", ">Looping:bool=false", "<then"}, "__bp.timer($s(Event), $in(Time), $in(Looping), () => __ce_$f(Event)());\n$exec(then)", {{"Event", "MyEvent"}});
  add("Flow.ClearTimer", "Flow Control", X, "Clear Timer", "إلغاء المؤقت", "Stops the timer of this custom event.", "يوقف مؤقت هذا الحدث المخصص.", {">exec", "<then"}, "__bp.clear($s(Event));\n$exec(then)", {{"Event", "MyEvent"}});
  add("Flow.CallEvent", "Flow Control", X, "Call Custom Event", "تشغيل حدث مخصص", "Runs your custom event with this name.", "يشغّل الحدث المخصص اللي بهذا الاسم.", {">exec", "<then"}, "__ce_$f(Event)();\n$exec(then)", {{"Event", "MyEvent"}});

  // ---- utilities
  add("Utilities.PrintString", "Utilities", X, "Print String", "اطبع نص", "Shows the text in the corner of the screen for Duration seconds, and in the Output Log.",
      "يعرض النص في زاوية الشاشة لمدة Duration ثانية، ويكتبه في سجل المخرجات (Output Log).", {">exec", ">InString:string=\"Hello\"", ">Duration:number=2", "<then"},
      "__bp.print($in(InString), $in(Duration));\n$exec(then)");
  add("Utilities.IsValid", "Utilities", U, "Is Valid", "هل موجود؟", "True if the actor exists (not empty, not destroyed).", "صحيح إذا الممثل موجود (مو فاضي ومو محذوف).",
      {">Object:actor=null", "<ReturnValue:bool@__bp.valid($in(Object))"});
  add("Utilities.Self", "Utilities", U, "Get a reference to self", "مرجع لنفسي (Self)", "This actor (the one the Blueprint is on).", "هذا الممثل نفسه (اللي عليه البلوبرنت).", {"<Self:actor@self"});
  add("Utilities.SetGlobal", "Utilities", X, "Set Game Value", "احفظ قيمة للعبة", "Keeps a value under Name that every Blueprint and script can read (world.vars).",
      "يحفظ قيمة باسم Name تقدر كل البلوبرنتات والسكربتات تقراها (world.vars).", {">exec", ">Name:string=\"Score\"", ">Value:any", "<then"}, "world.vars[$in(Name)] = $in(Value);\n$exec(then)");
  add("Utilities.GetGlobal", "Utilities", U, "Get Game Value", "اقرأ قيمة اللعبة", "A value kept with Set Game Value (or Default if there is none).", "قيمة محفوظة بعقدة Set Game Value (أو Default إذا ما فيه).",
      {">Name:string=\"Score\"", ">Default:any=0", "<Value:any@__bp.global($in(Name), $in(Default))"});

  // ---- actors (Target = self)
  add("Actor.GetLocation", "Actor", U, "Get Actor Location", "موقع الممثل", "Where the actor is in the world (metres).", "مكان الممثل في العالم (بالمتر).", {T, "<ReturnValue:vector@__bp.pos($in(Target))"});
  add("Actor.SetLocation", "Actor", X, "Set Actor Location", "حدد موقع الممثل", "Moves the actor to New Location.", "ينقل الممثل للموقع الجديد.", {">exec", T, ">NewLocation:vector", "<then"},
      "{ const __t = $in(Target); if (__t) __bp.setPos(__t, $in(NewLocation)); }\n$exec(then)");
  add("Actor.AddOffset", "Actor", X, "Add Actor World Offset", "حرّك الممثل بمقدار", "Moves the actor by Delta Location (add to where it is).", "يحرك الممثل بمقدار Delta Location من مكانه الحالي.",
      {">exec", T, ">DeltaLocation:vector", "<then"}, "{ const __t = $in(Target); if (__t) __bp.setPos(__t, __bp.vadd(__bp.pos(__t), $in(DeltaLocation))); }\n$exec(then)");
  add("Actor.GetRotation", "Actor", U, "Get Actor Rotation", "دوران الممثل", "The actor's rotation in degrees (X, Y, Z).", "دوران الممثل بالدرجات (X و Y و Z).", {T, "<ReturnValue:vector@__bp.rot($in(Target))"});
  add("Actor.SetRotation", "Actor", X, "Set Actor Rotation", "حدد دوران الممثل", "Turns the actor to New Rotation (degrees).", "يدوّر الممثل للدوران الجديد (بالدرجات).", {">exec", T, ">NewRotation:vector", "<then"},
      "{ const __t = $in(Target); if (__t) __bp.setRot(__t, $in(NewRotation)); }\n$exec(then)");
  add("Actor.AddRotation", "Actor", X, "Add Actor Rotation", "دوّر الممثل بمقدار", "Turns the actor by Delta Rotation (degrees).", "يدوّر الممثل بمقدار Delta Rotation (بالدرجات).",
      {">exec", T, ">DeltaRotation:vector", "<then"}, "{ const __t = $in(Target); if (__t) __bp.setRot(__t, __bp.vadd(__bp.rot(__t), $in(DeltaRotation))); }\n$exec(then)");
  add("Actor.GetScale", "Actor", U, "Get Actor Scale", "حجم الممثل", "The actor's scale (1 = normal size).", "حجم الممثل (1 = الحجم العادي).", {T, "<ReturnValue:vector@__bp.scl($in(Target))"});
  add("Actor.SetScale", "Actor", X, "Set Actor Scale", "حدد حجم الممثل", "Changes the actor's scale.", "يغيّر حجم الممثل.", {">exec", T, ">NewScale:vector=[1,1,1]", "<then"},
      "{ const __t = $in(Target), __v = $in(NewScale); if (__t && __t.scale) __t.scale.set(__v.x, __v.y, __v.z); }\n$exec(then)");
  add("Actor.SetVisibility", "Actor", X, "Set Visibility", "إظهار / إخفاء", "Shows or hides the actor.", "يظهر الممثل أو يخفيه.", {">exec", T, ">NewVisibility:bool=true", "<then"},
      "{ const __t = $in(Target); if (__t) __t.visible = !!$in(NewVisibility); }\n$exec(then)");
  add("Actor.SetColor", "Actor", X, "Set Color", "غيّر اللون", "Changes the actor's colour (#rrggbb, like #ff8800).", "يغيّر لون الممثل (مثل #ff8800).", {">exec", T, ">Color:string=\"#ff8800\"", "<then"},
      "{ const __t = $in(Target); if (__t && __t.setColor) __t.setColor($in(Color)); }\n$exec(then)");
  add("Actor.SetText", "Actor", X, "Set Text", "غيّر النص", "Changes a Text Render actor's text.", "يغيّر نص ممثل النص (Text Render).", {">exec", T, ">Text:string=\"Hello\"", "<then"},
      "{ const __t = $in(Target); if (__t && __t.setText) __t.setText(__bp.str($in(Text))); }\n$exec(then)");
  add("Actor.AddForce", "Actor", X, "Add Force", "ادفع الممثل", "Pushes a physics actor (Simulate Physics on).", "يدفع ممثل فيزيائي (لازم Simulate Physics شغال).", {">exec", T, ">Force:vector=[0,500,0]", "<then"},
      "{ const __t = $in(Target), __v = $in(Force); if (__t && __t.addForce) __t.addForce(__v.x, __v.y, __v.z); }\n$exec(then)");
  add("Actor.SetVelocity", "Actor", X, "Set Velocity", "حدد السرعة", "Sets a physics actor's velocity (m/s).", "يحدد سرعة ممثل فيزيائي (متر بالثانية).", {">exec", T, ">Velocity:vector", "<then"},
      "{ const __t = $in(Target), __v = $in(Velocity); if (__t && __t.setVelocity) __t.setVelocity(__v.x, __v.y, __v.z); }\n$exec(then)");
  add("Actor.GetVelocity", "Actor", U, "Get Velocity", "السرعة", "The actor's velocity (m/s).", "سرعة الممثل (متر بالثانية).", {T, "<ReturnValue:vector@__bp.vel($in(Target))"});
  add("Actor.LookAt", "Actor", X, "Look At", "انظر إلى", "Turns the actor to face a point.", "يدوّر الممثل عشان يواجه نقطة.", {">exec", T, ">Point:vector", "<then"},
      "{ const __t = $in(Target), __v = $in(Point); if (__t && __t.lookAt) __t.lookAt(__v.x, __v.y, __v.z); }\n$exec(then)");
  add("Actor.Destroy", "Actor", X, "Destroy Actor", "احذف الممثل", "Removes the actor from the game.", "يحذف الممثل من اللعبة.", {">exec", T, "<then"}, "{ const __t = $in(Target); if (__bp.valid(__t) && __t.destroy) __t.destroy(); }\n$exec(then)");
  add("Actor.GetName", "Actor", U, "Get Actor Name", "اسم الممثل", "The actor's name.", "اسم الممثل.", {T, "<ReturnValue:string@__bp.name($in(Target))"});
  add("Actor.HasTag", "Actor", U, "Actor Has Tag", "عنده وسم؟", "True if the actor has this tag.", "صحيح إذا الممثل عنده هذا الوسم (Tag).", {T, ">Tag:string=\"Coin\"", "<ReturnValue:bool@__bp.hasTag($in(Target), $in(Tag))"});
  add("Actor.GetDistanceTo", "Actor", U, "Get Distance To", "المسافة إلى", "Metres between the two actors (or the player).", "المسافة بالمتر بين الممثلين (أو اللاعب).",
      {T, ">OtherActor:actor=null", "<ReturnValue:number@__bp.dist(__bp.pos($in(Target)), __bp.pos($in(OtherActor)))"});
  add("Actor.IsPlayer", "Actor", U, "Is Player", "هل هو اللاعب؟", "True if this is the player (for overlap and hit events).", "صحيح إذا هذا هو اللاعب (مفيد مع أحداث التداخل والاصطدام).",
      {">Object:actor=null", "<ReturnValue:bool@!!($in(Object) && $in(Object).isPlayer)"});

  // ---- world
  add("World.SpawnActor", "World", X, "Spawn Actor", "أنشئ ممثل", "Makes a new actor: a mesh (Cube, Sphere, Cylinder, Cone, Plane...), where, its colour, physics, a script or Blueprint.",
      "يسوي ممثل جديد: شكله (Cube مكعب، Sphere كرة، Cylinder أسطوانة...)، مكانه، لونه، فيزياء، وسكربت أو بلوبرنت عليه.",
      {">exec", ">Mesh:string=\"Cube\"", ">Location:vector=[0,2,0]", ">Color:string=\"#ffffff\"", ">Physics:bool=false", ">Script:string=\"\"", ">Name:string=\"\"", "<then", "<ReturnValue:actor"},
      "$out(ReturnValue) = __bp.spawn($in(Mesh), $in(Location), $in(Color), $in(Physics), $in(Script), $in(Name));\n$exec(then)");
  add("World.FindActor", "World", U, "Find Actor By Name", "ابحث عن ممثل بالاسم", "The actor with this name (or nothing).", "الممثل اللي بهذا الاسم (أو لا شيء).", {">Name:string=\"Door\"", "<ReturnValue:actor@world.find($in(Name))"});
  add("World.GetActorsWithTag", "World", U, "Get All Actors With Tag", "كل الممثلين بوسم", "Every actor that has this tag (an array).", "كل الممثلين اللي عندهم هذا الوسم (قائمة).", {">Tag:string=\"Coin\"", "<OutActors:array@world.findAll($in(Tag))"});
  add("World.GetTime", "World", U, "Get Game Time", "وقت اللعبة", "Seconds since the game started.", "عدد الثواني من بداية اللعبة.", {"<Seconds:number@world.time"});
  add("World.Message", "World", X, "Show Message", "اعرض رسالة", "A big message in the middle of the screen.", "رسالة كبيرة في وسط الشاشة.", {">exec", ">Text:string=\"Level complete!\"", ">Duration:number=3", "<then"},
      "world.message(__bp.str($in(Text)), $in(Duration));\n$exec(then)");
  add("World.Hud", "World", X, "Set HUD Line", "سطر في الشاشة", "A line of text in the corner that stays (same Id = replaced; empty text = removed).", "سطر نص في زاوية الشاشة يبقى (نفس الـId يستبدله، والنص الفاضي يشيله).",
      {">exec", ">Id:string=\"score\"", ">Text:string=\"Score: 0\"", "<then"}, "world.hud(__bp.str($in(Id)), __bp.str($in(Text)));\n$exec(then)");
  add("World.Restart", "World", X, "Restart Level", "أعد المرحلة", "Starts the level again.", "يعيد المرحلة من البداية.", {">exec"}, "world.restart();");
  add("World.SetGravity", "World", X, "Set Gravity", "الجاذبية", "The world's gravity (m/s², normal = -9.81).", "جاذبية العالم (العادية -9.81).", {">exec", ">Gravity:number=-9.81", "<then"}, "world.setGravity($in(Gravity));\n$exec(then)");
  add("World.Trophy", "World", X, "Unlock Trophy", "افتح جائزة", "Gives a trophy on the player's Premium Games account (its id from the store's list).", "يعطي اللاعب جائزة في حسابه على Premium Games (المعرّف من قائمة المتجر).",
      {">exec", ">Id:string=\"first_win\"", "<then"}, "premium.trophy($in(Id));\n$exec(then)");

  // ---- player and camera
  add("Player.Get", "Player", U, "Get Player", "اللاعب", "The player (for distances, Is Player, ...).", "اللاعب (للمسافات وغيرها).", {"<Player:actor@world.player"});
  add("Player.GetLocation", "Player", U, "Get Player Location", "موقع اللاعب", "Where the player is.", "مكان اللاعب.", {"<ReturnValue:vector@world.player.position"});
  add("Player.Teleport", "Player", X, "Teleport Player", "انقل اللاعب", "Moves the player to a place at once.", "ينقل اللاعب لمكان على طول.", {">exec", ">Location:vector=[0,2,0]", "<then"},
      "{ const __v = $in(Location); world.player.teleport(__v.x, __v.y, __v.z); }\n$exec(then)");
  add("Player.Launch", "Player", X, "Launch Player", "اقذف اللاعب", "Throws the player with this velocity (a jump pad).", "يقذف اللاعب بهذه السرعة (مثل منصة قفز).", {">exec", ">Velocity:vector=[0,12,0]", "<then"},
      "{ const __v = $in(Velocity); world.player.launch(__v.x, __v.y, __v.z); }\n$exec(then)");
  add("Player.SetFly", "Player", X, "Set Flying", "الطيران", "Lets the player fly (no falling).", "يخلي اللاعب يطير (ما يطيح).", {">exec", ">Fly:bool=true", "<then"}, "world.player.fly = !!$in(Fly);\n$exec(then)");
  add("Player.SetSpeed", "Player", X, "Set Walk Speed", "سرعة المشي", "How fast the player walks and runs (m/s).", "سرعة مشي اللاعب وركضه (متر بالثانية).", {">exec", ">WalkSpeed:number=4.3", ">RunSpeed:number=5.6", "<then"},
      "world.player.set({ walkSpeed: $in(WalkSpeed), runSpeed: $in(RunSpeed) });\n$exec(then)");
  add("Camera.GetForward", "Player", U, "Get Camera Forward", "اتجاه الكاميرا", "The direction the player looks.", "الاتجاه اللي يطالع فيه اللاعب.", {"<ReturnValue:vector@world.camera.forward"});
  add("Camera.GetLocation", "Player", U, "Get Camera Location", "موقع الكاميرا", "Where the camera is.", "مكان الكاميرا.", {"<ReturnValue:vector@world.camera.position"});

  // ---- input
  add("Input.IsKeyDown", "Input", U, "Is Key Down", "الزر مضغوط؟", "True while the key is held (w, space, shift, a..z, 0..9, MouseLeft, PadA...).", "صحيح طول ما الزر مضغوط (w، space، shift، الحروف، الأرقام، MouseLeft، PadA...).",
      {">Key:string=\"w\"", "<ReturnValue:bool@input.key($in(Key))"});
  add("Input.WasPressed", "Input", U, "Was Key Pressed", "انضغط الزر الحين؟", "True only in the frame the key was pressed.", "صحيح بس في الإطار اللي انضغط فيه الزر.", {">Key:string=\"space\"", "<ReturnValue:bool@input.pressed($in(Key))"});
  add("Input.MouseDelta", "Input", U, "Get Mouse Delta", "حركة الماوس", "How far the mouse moved this frame.", "كم تحرك الماوس في هذا الإطار.", {"<X:number@input.mouse.dx", "<Y:number@input.mouse.dy"});
  add("Input.Axis", "Input", U, "Get Controller Axis", "عصا يد التحكم", "A controller stick or trigger: LeftX, LeftY, RightX, RightY (-1..1), LT, RT (0..1).", "عصا أو زناد يد التحكم: LeftX و LeftY و RightX و RightY (من -1 إلى 1)، LT و RT (من 0 إلى 1).",
      {">Axis:string=\"LeftX\"", "<ReturnValue:number@(input.axis ? input.axis($in(Axis)) : 0)"});
  add("Input.Rumble", "Input", X, "Controller Rumble", "اهتزاز يد التحكم", "Vibrates the controller (0..1) for Duration seconds.", "يهز يد التحكم (من 0 إلى 1) لمدة Duration ثانية.", {">exec", ">Strength:number=0.6", ">Duration:number=0.3", "<then"},
      "if (input.rumble) input.rumble($in(Strength), $in(Strength), $in(Duration));\n$exec(then)");

  // ---- sound
  add("Audio.PlaySound", "Audio", X, "Play Sound 2D", "شغّل صوت", "Plays a sound file from the project (sounds/coin.ogg ...).", "يشغّل ملف صوت من المشروع (مثل sounds/coin.ogg).", {">exec", ">Sound:string=\"sounds/click.ogg\"", ">Volume:number=1", "<then"},
      "if (typeof sound !== 'undefined') sound.play($in(Sound), { volume: $in(Volume) });\n$exec(then)");
  add("Audio.PlaySoundAt", "Audio", X, "Play Sound at Location", "شغّل صوت في مكان", "Plays a sound at a place (quieter far away).", "يشغّل صوت في مكان معين (يوطى كل ما بعدت).", {">exec", ">Sound:string=\"sounds/click.ogg\"", ">Location:vector", ">Volume:number=1", "<then"},
      "if (typeof sound !== 'undefined') sound.play($in(Sound), { volume: $in(Volume), position: __bp.arr($in(Location)) });\n$exec(then)");

  // ---- saving
  add("Save.SetValue", "Save Game", X, "Save Value", "احفظ قيمة", "Saves a value under Key on the player's computer (kept after closing the game).", "يحفظ قيمة باسم Key في جهاز اللاعب (تبقى بعد ما يقفل اللعبة).",
      {">exec", ">Key:string=\"best\"", ">Value:any", "<then"}, "storage.setJSON(__bp.str($in(Key)), $in(Value));\n$exec(then)");
  add("Save.GetValue", "Save Game", U, "Load Value", "اقرأ قيمة محفوظة", "A saved value (or Default if it was never saved).", "قيمة محفوظة (أو Default إذا ما انحفظت قبل).", {">Key:string=\"best\"", ">Default:any=0", "<Value:any@storage.getJSON(__bp.str($in(Key)), $in(Default))"});

  // ---- math
  auto bin = [&](const char* type, const char* en, const char* ar, const char* dEn, const char* dAr, const char* expr, const char* bDef = "0") {
    add(type, "Math", U, en, ar, dEn, dAr, {">A:number=0", std::string(">B:number=") + bDef, std::string("<ReturnValue:number@") + expr}); };
  bin("Math.Add", "Add (+)", "جمع (+)", "A + B", "A زائد B", "($in(A) + $in(B))");
  bin("Math.Subtract", "Subtract (-)", "طرح (-)", "A - B", "A ناقص B", "($in(A) - $in(B))");
  bin("Math.Multiply", "Multiply (×)", "ضرب (×)", "A × B", "A ضرب B", "($in(A) * $in(B))", "1");
  bin("Math.Divide", "Divide (÷)", "قسمة (÷)", "A ÷ B (0 when B is 0)", "A تقسيم B (يطلع 0 إذا B صفر)", "__bp.div($in(A), $in(B))", "1");
  bin("Math.Modulo", "Modulo (%)", "باقي القسمة (%)", "What is left after A ÷ B", "الباقي بعد قسمة A على B", "__bp.mod($in(A), $in(B))", "2");
  bin("Math.Power", "Power", "أُس", "A to the power of B", "A أس B", "Math.pow($in(A), $in(B))", "2");
  bin("Math.Min", "Min", "الأصغر", "The smaller of A and B", "الأصغر بين A و B", "Math.min($in(A), $in(B))");
  bin("Math.Max", "Max", "الأكبر", "The bigger of A and B", "الأكبر بين A و B", "Math.max($in(A), $in(B))");
  auto un = [&](const char* type, const char* en, const char* ar, const char* dEn, const char* dAr, const char* expr) { add(type, "Math", U, en, ar, dEn, dAr, {">Value:number=0", std::string("<ReturnValue:number@") + expr}); };
  un("Math.Abs", "Absolute", "القيمة المطلقة", "The value without its minus sign", "القيمة بدون إشارة السالب", "Math.abs($in(Value))");
  un("Math.Round", "Round", "تقريب", "The nearest whole number", "أقرب رقم صحيح", "Math.round($in(Value))");
  un("Math.Floor", "Floor", "تقريب لتحت", "The whole number below", "الرقم الصحيح اللي تحته", "Math.floor($in(Value))");
  un("Math.Sqrt", "Square Root", "الجذر التربيعي", "√Value", "الجذر التربيعي", "Math.sqrt(Math.max(0, $in(Value)))");
  un("Math.Sin", "Sin (Degrees)", "جيب (درجات)", "Sine of an angle in degrees (waves, bobbing)", "جيب الزاوية بالدرجات (للحركة الموجية)", "Math.sin($in(Value) * Math.PI / 180)");
  un("Math.Cos", "Cos (Degrees)", "جيب التمام (درجات)", "Cosine of an angle in degrees", "جيب تمام الزاوية بالدرجات", "Math.cos($in(Value) * Math.PI / 180)");
  add("Math.Clamp", "Math", U, "Clamp", "حصر بين حدين", "Value kept between Min and Max.", "يخلي القيمة بين Min و Max.", {">Value:number=0", ">Min:number=0", ">Max:number=1", "<ReturnValue:number@Math.min(Math.max($in(Value), $in(Min)), $in(Max))"});
  add("Math.Lerp", "Math", U, "Lerp", "مزج بين قيمتين", "From A (Alpha 0) to B (Alpha 1).", "قيمة بين A (لما Alpha صفر) و B (لما Alpha واحد).", {">A:number=0", ">B:number=1", ">Alpha:number=0.5", "<ReturnValue:number@($in(A) + ($in(B) - $in(A)) * $in(Alpha))"});
  add("Math.RandomFloat", "Math", U, "Random Float in Range", "رقم عشوائي", "A random number between Min and Max.", "رقم عشوائي بين Min و Max.", {">Min:number=0", ">Max:number=1", "<ReturnValue:number@($in(Min) + Math.random() * ($in(Max) - $in(Min)))"});
  add("Math.RandomInt", "Math", U, "Random Integer in Range", "رقم صحيح عشوائي", "A random whole number from Min to Max.", "رقم صحيح عشوائي من Min إلى Max.", {">Min:number=1", ">Max:number=6", "<ReturnValue:number@__bp.randInt($in(Min), $in(Max))"});
  auto cmp = [&](const char* type, const char* en, const char* ar, const char* expr, PinType t) {
    std::string ti = pinTypeId(t); add(type, "Math", U, en, ar, std::string("True if ") + en, std::string("صحيح إذا ") + ar, {">A:" + ti, ">B:" + ti, std::string("<ReturnValue:bool@") + expr}); };
  cmp("Math.Equal", "Equal (==)", "يساوي (==)", "__bp.eq($in(A), $in(B))", PinType::Any);
  cmp("Math.NotEqual", "Not Equal (!=)", "لا يساوي (!=)", "!__bp.eq($in(A), $in(B))", PinType::Any);
  cmp("Math.Less", "Less (<)", "أصغر من (<)", "($in(A) < $in(B))", PinType::Number);
  cmp("Math.Greater", "Greater (>)", "أكبر من (>)", "($in(A) > $in(B))", PinType::Number);
  cmp("Math.LessEq", "Less or Equal (<=)", "أصغر أو يساوي (<=)", "($in(A) <= $in(B))", PinType::Number);
  cmp("Math.GreaterEq", "Greater or Equal (>=)", "أكبر أو يساوي (>=)", "($in(A) >= $in(B))", PinType::Number);
  add("Math.And", "Math", U, "AND", "و (AND)", "True if both are true.", "صحيح إذا الاثنين صح.", {">A:bool", ">B:bool", "<ReturnValue:bool@(!!$in(A) && !!$in(B))"});
  add("Math.Or", "Math", U, "OR", "أو (OR)", "True if one of them is true.", "صحيح إذا واحد منهم صح.", {">A:bool", ">B:bool", "<ReturnValue:bool@(!!$in(A) || !!$in(B))"});
  add("Math.Not", "Math", U, "NOT", "عكس (NOT)", "True becomes false, false becomes true.", "الصح يصير خطأ، والخطأ يصير صح.", {">A:bool", "<ReturnValue:bool@!$in(A)"});
  add("Math.Select", "Math", U, "Select", "اختيار", "A if Pick A is true, else B.", "يعطيك A إذا الشرط صح، وإلا B.", {">PickA:bool=true", ">A:any", ">B:any", "<ReturnValue:any@($in(PickA) ? $in(A) : $in(B))"});

  // ---- vectors
  add("Vector.Make", "Vector", U, "Make Vector", "سوِّ متجه", "A vector from X, Y and Z (Y is up).", "متجه من X و Y و Z (الـY لفوق).", {">X:number=0", ">Y:number=0", ">Z:number=0", "<ReturnValue:vector@__bp.v($in(X), $in(Y), $in(Z))"});
  add("Vector.Break", "Vector", U, "Break Vector", "فكّك المتجه", "X, Y and Z of a vector.", "يطلع X و Y و Z من المتجه.", {">InVec:vector", "<X:number@$in(InVec).x", "<Y:number@$in(InVec).y", "<Z:number@$in(InVec).z"});
  add("Vector.Add", "Vector", U, "Vector + Vector", "جمع متجهين", "A + B", "A زائد B", {">A:vector", ">B:vector", "<ReturnValue:vector@__bp.vadd($in(A), $in(B))"});
  add("Vector.Subtract", "Vector", U, "Vector - Vector", "طرح متجهين", "A - B (the way from B to A)", "A ناقص B (الاتجاه من B إلى A)", {">A:vector", ">B:vector", "<ReturnValue:vector@__bp.vsub($in(A), $in(B))"});
  add("Vector.Scale", "Vector", U, "Vector * Float", "ضرب متجه برقم", "Every part times Scale.", "كل جزء مضروب في Scale.", {">A:vector", ">Scale:number=1", "<ReturnValue:vector@__bp.vmul($in(A), $in(Scale))"});
  add("Vector.Length", "Vector", U, "Vector Length", "طول المتجه", "How long the vector is.", "طول المتجه.", {">A:vector", "<ReturnValue:number@__bp.vlen($in(A))"});
  add("Vector.Normalize", "Vector", U, "Normalize", "اتجاه بطول 1", "The same direction, length 1.", "نفس الاتجاه بطول 1.", {">A:vector", "<ReturnValue:vector@__bp.vnorm($in(A))"});
  add("Vector.Distance", "Vector", U, "Distance", "المسافة بين نقطتين", "Metres between two points.", "المسافة بالمتر بين نقطتين.", {">V1:vector", ">V2:vector", "<ReturnValue:number@__bp.dist($in(V1), $in(V2))"});

  // ---- text
  add("String.Append", "String", U, "Append", "ألصق نصين", "A then B as one text.", "النص A وبعده B كنص واحد.", {">A:string=\"Score: \"", ">B:string", "<ReturnValue:string@(__bp.str($in(A)) + __bp.str($in(B)))"});
  add("String.ToString", "String", U, "To String", "حوّل لنص", "Any value as text.", "أي قيمة كنص.", {">In:any", "<ReturnValue:string@__bp.str($in(In))"});
  add("String.Length", "String", U, "Length", "طول النص", "How many letters.", "عدد الحروف.", {">In:string", "<ReturnValue:number@__bp.str($in(In)).length"});
  add("String.Contains", "String", U, "Contains", "يحتوي على", "True if Text has Sub in it.", "صحيح إذا النص فيه الجزء Sub.", {">Text:string", ">Sub:string", "<ReturnValue:bool@__bp.str($in(Text)).includes(__bp.str($in(Sub)))"});

  // ---- arrays
  add("Array.Length", "Array", U, "Array Length", "عدد العناصر", "How many things are in the array.", "عدد العناصر في القائمة.", {">Array:array", "<ReturnValue:number@($in(Array) || []).length"});
  add("Array.Get", "Array", U, "Get (Array)", "عنصر من القائمة", "The element at Index (0 = the first).", "العنصر اللي في المكان Index (صفر = الأول).", {">Array:array", ">Index:number=0", "<ReturnValue:any@($in(Array) || [])[Math.round($in(Index))]"});

  // ---- variables (their pins take the variable's type)
  add("Var.Get", "Variables", U, "Get", "اقرأ متغير", "Reads one of this Blueprint's variables.", "يقرأ متغير من متغيرات هذا البلوبرنت.", {"<Value:any@v_$f(Var)"}, "", {{"Var", ""}});
  add("Var.Set", "Variables", X, "Set", "غيّر متغير", "Changes one of this Blueprint's variables.", "يغيّر قيمة متغير من متغيرات هذا البلوبرنت.", {">exec", ">Value:any", "<then", "<Output:any@v_$f(Var)"},
      "v_$f(Var) = $in(Value);\n$exec(then)", {{"Var", ""}});

  // ---- comment box
  add("Comment", "Utilities", BpNodeDef::Comment, "Comment", "تعليق", "A box with a note, to group nodes.", "صندوق فيه ملاحظة، لتجميع العقد.", {}, "", {{"Text", "Comment"}});
  return L;
}
const std::vector<BpNodeDef>& bpLibrary() { static std::vector<BpNodeDef> L = buildLibrary(); return L; }
const BpNodeDef* bpDef(const std::string& type) {
  static std::map<std::string, const BpNodeDef*> m; if (m.empty()) for (auto& d : bpLibrary()) m[d.type] = &d;
  auto it = m.find(type); return it == m.end() ? nullptr : it->second;
}
std::string bpCategoryAr(const std::string& c) {
  static const std::map<std::string, std::string> m = {{"Events", "الأحداث"}, {"Flow Control", "التحكم بالتنفيذ"}, {"Utilities", "أدوات"}, {"Actor", "الممثل"}, {"World", "العالم"},
    {"Player", "اللاعب والكاميرا"}, {"Input", "الإدخال"}, {"Audio", "الصوت"}, {"Save Game", "حفظ اللعبة"}, {"Math", "رياضيات ومنطق"}, {"Vector", "المتجهات"}, {"String", "النصوص"},
    {"Array", "القوائم"}, {"Variables", "المتغيرات"}};
  auto it = m.find(c); return it == m.end() ? c : it->second;
}
std::string bpPinEn(const std::string& id) {
  if (id == "exec" || id == "then") return ""; if (id == "ReturnValue") return "Return Value";
  std::string o; for (size_t i = 0; i < id.size(); i++) { char c = id[i]; if (i && ((std::isupper((unsigned char)c) && !std::isupper((unsigned char)id[i - 1])) || (std::isdigit((unsigned char)c) && !std::isdigit((unsigned char)id[i - 1])))) o += ' '; o += c; }
  return o;
}
std::string bpPinAr(const std::string& id) {
  static const std::map<std::string, std::string> m = {
    {"Target", "الهدف"}, {"ReturnValue", "النتيجة"}, {"DeltaSeconds", "زمن الإطار"}, {"OtherActor", "الممثل الثاني"}, {"Key", "الزر"}, {"Condition", "الشرط"},
    {"True", "صح"}, {"False", "خطأ"}, {"Then0", "أولاً"}, {"Then1", "ثانياً"}, {"Then2", "ثالثاً"}, {"Then3", "رابعاً"}, {"FirstIndex", "من"}, {"LastIndex", "إلى"},
    {"LoopBody", "كل مرة"}, {"Index", "الرقم"}, {"Completed", "بعد الانتهاء"}, {"Array", "القائمة"}, {"ArrayElement", "العنصر"}, {"ArrayIndex", "رقمه"}, {"Reset", "إعادة"},
    {"A", "A"}, {"B", "B"}, {"IsA", "هل A؟"}, {"Enter", "دخول"}, {"Open", "افتح"}, {"Close", "اقفل"}, {"Toggle", "اعكس"}, {"Exit", "خروج"}, {"Duration", "المدة (ثواني)"},
    {"Time", "الوقت"}, {"Looping", "يتكرر"}, {"InString", "النص"}, {"Object", "الشيء"}, {"Self", "نفسي"}, {"Name", "الاسم"}, {"Value", "القيمة"}, {"Default", "الافتراضي"},
    {"NewLocation", "الموقع الجديد"}, {"DeltaLocation", "مقدار الحركة"}, {"NewRotation", "الدوران الجديد"}, {"DeltaRotation", "مقدار الدوران"}, {"NewScale", "الحجم الجديد"},
    {"NewVisibility", "ظاهر"}, {"Color", "اللون"}, {"Text", "النص"}, {"Force", "القوة"}, {"Velocity", "السرعة"}, {"Point", "النقطة"}, {"Tag", "الوسم"}, {"Mesh", "الشكل"},
    {"Location", "الموقع"}, {"Physics", "فيزياء"}, {"Script", "السكربت"}, {"OutActors", "الممثلين"}, {"Seconds", "الثواني"}, {"Id", "المعرّف"}, {"Gravity", "الجاذبية"},
    {"Player", "اللاعب"}, {"Fly", "يطير"}, {"WalkSpeed", "سرعة المشي"}, {"RunSpeed", "سرعة الركض"}, {"X", "X"}, {"Y", "Y"}, {"Z", "Z"}, {"Axis", "المحور"},
    {"Strength", "القوة"}, {"Sound", "الصوت"}, {"Volume", "الارتفاع"}, {"Min", "الأقل"}, {"Max", "الأكثر"}, {"Alpha", "النسبة"}, {"PickA", "اختر A"}, {"InVec", "المتجه"},
    {"Scale", "المضاعف"}, {"V1", "النقطة 1"}, {"V2", "النقطة 2"}, {"In", "المدخل"}, {"Sub", "الجزء"}, {"Output", "القيمة"}};
  if (id == "exec" || id == "then") return ""; auto it = m.find(id); return it == m.end() ? bpPinEn(id) : it->second;
}

/* ------------------------------------------------------------------ graph */
BpNode* Blueprint::node(int id) { for (auto& n : nodes) if (n.id == id) return &n; return nullptr; }
const BpNode* Blueprint::node(int id) const { for (auto& n : nodes) if (n.id == id) return &n; return nullptr; }
const BpVar* Blueprint::var(const std::string& name) const { for (auto& v : vars) if (v.name == name) return &v; return nullptr; }

std::vector<BpPinDef> bpPins(const Blueprint& bp, const BpNode& n) {
  const BpNodeDef* d = bpDef(n.type); if (!d) return {};
  std::vector<BpPinDef> pins = d->pins;
  if (n.type == "Var.Get" || n.type == "Var.Set") {
    const BpVar* v = bp.var(n.values.value("Var", std::string())); PinType t = v ? v->type : PinType::Any;
    for (auto& p : pins) if (p.type == PinType::Any) { p.type = t; if (v) p.def = v->def; }
  }
  return pins;
}
const BpPinDef* bpPin(const Blueprint& bp, const BpNode& n, const std::string& pin, std::vector<BpPinDef>* store) {
  *store = bpPins(bp, n); for (auto& p : *store) if (p.id == pin) return &p; return nullptr;
}

json bpToJson(const Blueprint& bp) {
  json nodes = json::array(), links = json::array(), vars = json::array();
  for (auto& n : bp.nodes) { json o = {{"id", n.id}, {"type", n.type}, {"x", std::round(n.x)}, {"y", std::round(n.y)}}; if (!n.values.empty()) o["values"] = n.values; if (n.type == "Comment") { o["w"] = n.w; o["h"] = n.h; } nodes.push_back(o); }
  for (auto& l : bp.links) links.push_back({l.from, l.fromPin, l.to, l.toPin});
  for (auto& v : bp.vars) vars.push_back({{"name", v.name}, {"type", pinTypeId(v.type)}, {"default", v.def}});
  return {{"version", 1}, {"nodes", nodes}, {"links", links}, {"vars", vars}, {"nextId", bp.nextId}};
}
Blueprint bpFromJson(const json& j) {
  Blueprint bp; if (!j.is_object()) return bp;
  if (j.contains("nodes") && j["nodes"].is_array()) for (auto& o : j["nodes"]) {
    if (!o.is_object()) continue; BpNode n; n.id = o.value("id", 0); n.type = o.value("type", std::string()); n.x = o.value("x", 0.f); n.y = o.value("y", 0.f);
    if (o.contains("values") && o["values"].is_object()) n.values = o["values"]; n.w = o.value("w", 360.f); n.h = o.value("h", 200.f);
    if (n.id > 0 && !n.type.empty()) bp.nodes.push_back(n); }
  if (j.contains("links") && j["links"].is_array()) for (auto& o : j["links"]) if (o.is_array() && o.size() == 4) {
    try { BpLink l{o[0].get<int>(), o[1].get<std::string>(), o[2].get<int>(), o[3].get<std::string>()}; bp.links.push_back(l); } catch (...) {} }
  if (j.contains("vars") && j["vars"].is_array()) for (auto& o : j["vars"]) if (o.is_object()) {
    BpVar v; v.name = o.value("name", std::string()); v.type = pinTypeFrom(o.value("type", std::string("number"))); v.def = o.contains("default") ? o["default"] : json(); if (!v.name.empty()) bp.vars.push_back(v); }
  int mx = 0; for (auto& n : bp.nodes) mx = std::max(mx, n.id); bp.nextId = std::max(j.value("nextId", 1), mx + 1);
  return bp;
}
Blueprint bpStarter() {
  Blueprint bp; BpNode a; a.id = 1; a.type = "Event.BeginPlay"; a.x = 0; a.y = 0; BpNode b; b.id = 2; b.type = "Event.Tick"; b.x = 0; b.y = 220;
  bp.nodes = {a, b}; bp.nextId = 3; return bp;
}
std::string bpSanitize(const std::string& s) {
  std::string o; for (unsigned char c : s) o += (std::isalnum(c) || c == '_') ? (char)c : '_'; if (o.empty()) o = "_"; return o;
}

/* ------------------------------------------------------------------ the compiler */
// What every compiled Blueprint carries: vectors, timers, Delay / Do Once / Flip Flop state, printing, widget clicks
static const char* BP_RUNTIME = R"JS(const __bp = (() => {
  const D = Math.PI / 180, B = {
  v(x, y, z) { return new Vec3(+x || 0, +y || 0, +z || 0); },
  vec(a) { return a && typeof a === 'object' ? (Array.isArray(a) ? B.v(a[0], a[1], a[2]) : B.v(a.x, a.y, a.z)) : B.v(0, 0, 0); },
  arr(a) { a = B.vec(a); return [a.x, a.y, a.z]; },
  vadd(a, b) { a = B.vec(a); b = B.vec(b); return B.v(a.x + b.x, a.y + b.y, a.z + b.z); },
  vsub(a, b) { a = B.vec(a); b = B.vec(b); return B.v(a.x - b.x, a.y - b.y, a.z - b.z); },
  vmul(a, s) { a = B.vec(a); s = +s || 0; return B.v(a.x * s, a.y * s, a.z * s); },
  vlen(a) { a = B.vec(a); return Math.hypot(a.x, a.y, a.z); },
  vnorm(a) { a = B.vec(a); const l = Math.hypot(a.x, a.y, a.z) || 1; return B.v(a.x / l, a.y / l, a.z / l); },
  dist(a, b) { a = B.vec(a); b = B.vec(b); return Math.hypot(a.x - b.x, a.y - b.y, a.z - b.z); },
  valid(a) { return !!a && (a.isPlayer || a.alive !== false); },
  pos(a) { if (!a) return B.v(0, 0, 0); if (a.isPlayer) return a.position; return a.worldPosition ? a.worldPosition : B.vec(a); },
  setPos(a, v) { v = B.vec(v); if (a.isPlayer) a.teleport(v.x, v.y, v.z); else if (a.position) a.position.set(v.x, v.y, v.z); },
  rot(a) { if (!a || !a.rotation) return B.v(0, 0, 0); const r = a.rotation; return B.v(r.x / D, r.y / D, r.z / D); },
  setRot(a, v) { v = B.vec(v); if (a.rotation) a.rotation.set(v.x * D, v.y * D, v.z * D); },
  scl(a) { return a && a.scale ? B.vec(a.scale) : B.v(1, 1, 1); },
  vel(a) { return a && a.velocity ? B.vec(a.velocity) : B.v(0, 0, 0); },
  name(a) { return a ? String(a.name || '') : ''; },
  hasTag(a, t) { return !!a && Array.isArray(a.tags) && a.tags.includes(String(t)); },
  div(a, b) { return b ? a / b : 0; }, mod(a, b) { return b ? ((a % b) + b) % b : 0; },
  randInt(a, b) { a = Math.round(a); b = Math.round(b); if (b < a) [a, b] = [b, a]; return a + Math.floor(Math.random() * (b - a + 1)); },
  eq(a, b) { if (a && b && typeof a === 'object' && typeof b === 'object') { if (a.id !== undefined || b.id !== undefined || a.isPlayer || b.isPlayer) return a === b || (a.id !== undefined && a.id === b.id);
      if ('x' in a && 'x' in b) return a.x === b.x && a.y === b.y && a.z === b.z; } return a === b; },
  str(x) { if (x === null || x === undefined) return ''; if (typeof x === 'number') return String(Math.round(x * 1000) / 1000); if (typeof x === 'boolean') return x ? 'true' : 'false';
    if (typeof x === 'object') { if (x.isPlayer) return 'Player'; if (x.name !== undefined && x.id !== undefined) return String(x.name);
      if ('x' in x && 'y' in x && 'z' in x) return 'X=' + B.str(x.x) + ' Y=' + B.str(x.y) + ' Z=' + B.str(x.z);
      if (Array.isArray(x)) return x.map(B.str).join(', '); try { return JSON.stringify(x); } catch (e) { return String(x); } }
    return String(x); },
  global(k, d) { const v = world.vars[k]; return v === undefined ? d : v; },
  spawn(mesh, at, color, physics, script, name) { const o = { position: B.arr(at), color: String(color || '#ffffff'), physics: !!physics }; if (script) o.script = String(script); if (name) o.name = String(name); return world.spawn(String(mesh || 'Cube'), o); },
  once: {}, flip: {}, gate: {}, pend: {}, timers: [], uis: [], pn: 0,
  delay(id, s, f) { if (B.pend[id]) return; B.pend[id] = 1; B.timers.push({ t: Math.max(0, +s || 0), f: () => { delete B.pend[id]; f(); } }); },
  timer(name, s, loop, f) { B.clear(name); s = Math.max(0.001, +s || 0); B.timers.push({ name, t: s, every: loop ? s : 0, f }); },
  clear(name) { B.timers = B.timers.filter(t => t.name !== name); },
  print(t, s) { t = B.str(t); log(t); const id = '__bp' + (B.pn++ % 6); world.hud(id, t); B.timers.push({ t: Math.max(0.1, +s || 2), f: () => world.hud(id, '') }); },
  uiOn(id, ev, f) { B.uis.push({ id, ev, f, done: false }); },
  tick(dt) { const due = []; for (const t of B.timers) { t.t -= dt; if (t.t <= 0) due.push(t); }
    for (const t of due) { if (t.every) t.t += t.every; else { const i = B.timers.indexOf(t); if (i >= 0) B.timers.splice(i, 1); } t.f(); }
    if (B.uis.length && typeof ui !== 'undefined') for (const u of B.uis) if (!u.done) { const w = ui.get(u.id); if (w) { w.on(u.ev, u.f); u.done = true; } } } };
  return B; })();
)JS";

namespace {
struct Gen {
  const Blueprint& bp; std::vector<BpMessage>& msgs;
  std::map<int, const BpNode*> byId;
  std::map<std::pair<int, std::string>, std::pair<int, std::string>> inLink;              // an input ← its wire
  std::map<std::pair<int, std::string>, std::vector<std::pair<int, std::string>>> outLinks; // an output → its wires
  std::map<std::pair<int, std::string>, int> execIns;                                     // how many wires come into an exec input
  std::set<std::string> fnNeeded, fnDone; std::map<std::string, std::string> fnBody;       // exec entries made into functions
  std::set<std::string> topVars; std::set<int> reached; std::vector<std::pair<int, std::string>> stack; std::set<int> pureStack;
  std::set<std::string> customEvents;
  bool failed = false;
  Gen(const Blueprint& b, std::vector<BpMessage>& m) : bp(b), msgs(m) {}

  void err(int node, const std::string& en, const std::string& ar, bool error = true) { msgs.push_back({node, en, ar, error}); if (error) failed = true; }
  const BpNode* N(int id) { auto it = byId.find(id); return it == byId.end() ? nullptr : it->second; }
  std::vector<BpPinDef> pinsOf(const BpNode& n) { return bpPins(bp, n); }
  const BpPinDef* pinOf(const BpNode& n, const std::string& id, std::vector<BpPinDef>& store) { store = pinsOf(n); for (auto& p : store) if (p.id == id) return &p; return nullptr; }
  std::string outVar(int id, const std::string& pin) { std::string v = "__n" + std::to_string(id) + "_" + bpSanitize(pin); topVars.insert(v); return v; }

  std::string literal(const BpPinDef& p, const json& v) {
    switch (p.type) {
      case PinType::Bool: return (v.is_boolean() ? v.get<bool>() : v.is_number() ? v.get<double>() != 0 : v.is_string() ? (v.get<std::string>() == "true") : false) ? "true" : "false";
      case PinType::Number: { double d = 0; if (v.is_number()) d = v.get<double>(); else if (v.is_string()) { try { d = std::stod(v.get<std::string>()); } catch (...) {} } else if (v.is_boolean()) d = v.get<bool>();
        if (!std::isfinite(d)) d = 0; std::ostringstream o; o.precision(10); o << d; return o.str(); }
      case PinType::String: return json(v.is_string() ? v.get<std::string>() : v.is_null() ? std::string() : v.dump()).dump();
      case PinType::Vector: { double a[3] = {0, 0, 0}; if (v.is_array()) for (size_t i = 0; i < 3 && i < v.size(); i++) if (v[i].is_number()) a[i] = v[i].get<double>();
        std::ostringstream o; o.precision(10); o << "__bp.v(" << a[0] << ", " << a[1] << ", " << a[2] << ")"; return o.str(); }
      case PinType::Actor: return v.is_string() && v.get<std::string>() == "self" ? "self" : "null";
      case PinType::Array: return "[]";
      default: if (v.is_null()) return "null"; if (v.is_number() || v.is_boolean()) return v.dump(); if (v.is_string()) return json(v.get<std::string>()).dump(); return v.dump();
    }
  }
  // the value an input gets: its wire (a pure node's expression, or another node's output variable) or the value typed on the node
  std::string in(const BpNode& n, const std::string& pin) {
    std::vector<BpPinDef> st; const BpPinDef* p = pinOf(n, pin, st); if (!p) { err(n.id, "Unknown pin " + pin, "منفذ غير معروف " + pin); return "null"; }
    auto it = inLink.find({n.id, pin});
    if (it == inLink.end()) return literal(*p, n.values.contains(pin) ? n.values[pin] : p->def);
    const BpNode* src = N(it->second.first); if (!src) return literal(*p, p->def);
    std::vector<BpPinDef> st2; const BpPinDef* sp = pinOf(*src, it->second.second, st2); if (!sp) { err(n.id, "A wire comes from a pin that is gone", "فيه سلك جاي من منفذ مو موجود"); return "null"; }
    std::string e = out(*src, *sp);
    if (p->type == PinType::String && sp->type != PinType::String) e = "__bp.str(" + e + ")";
    else if (p->type == PinType::Vector && sp->type != PinType::Vector) e = "__bp.vec(" + e + ")";
    else if (p->type == PinType::Number && sp->type == PinType::Any) e = "(+(" + e + ") || 0)";
    return e;
  }
  // an output's value: an expression (pure outputs) or the variable the node wrote when it ran
  std::string out(const BpNode& n, const BpPinDef& p) {
    if (!p.code.empty()) {
      if (pureStack.count(n.id)) { err(n.id, "These nodes feed each other in a circle", "هذي العقد توصل لبعض في دائرة"); return "null"; }
      pureStack.insert(n.id); std::string e = expand(n, p.code, 0); pureStack.erase(n.id); reached.insert(n.id); return "(" + e + ")";
    }
    return outVar(n.id, p.id);
  }
  std::string field(const BpNode& n, const std::string& f) { const BpNodeDef* d = bpDef(n.type); std::string def; if (d) for (auto& x : d->fields) if (x.id == f) def = x.def;
    auto it = n.values.find(f); return it != n.values.end() && it->is_string() ? it->get<std::string>() : def; }
  std::string indent(const std::string& s, int lv) { if (s.empty()) return s; std::string pad(lv * 2, ' '), o; std::istringstream is(s); std::string ln; bool first = true;
    while (std::getline(is, ln)) { if (ln.empty()) continue; if (!first) o += "\n"; o += pad + ln; first = false; } return o; }

  // a template → code
  std::string expand(const BpNode& n, const std::string& t, int lv) {
    std::string o; size_t i = 0;
    while (i < t.size()) {
      if (t[i] == '$') {
        auto arg = [&](size_t s) -> std::pair<std::string, size_t> { size_t e = t.find(')', s); return {t.substr(s, e - s), e + 1}; };
        if (t.compare(i, 4, "$in(") == 0) { auto a = arg(i + 4); o += in(n, a.first); i = a.second; continue; }
        if (t.compare(i, 5, "$out(") == 0) { auto a = arg(i + 5); o += outVar(n.id, a.first); i = a.second; continue; }
        if (t.compare(i, 6, "$exec(") == 0) { auto a = arg(i + 6); o += execOut(n, a.first, lv); i = a.second; continue; }
        if (t.compare(i, 3, "$f(") == 0) { auto a = arg(i + 3); o += bpSanitize(field(n, a.first)); i = a.second; continue; }
        if (t.compare(i, 3, "$s(") == 0) { auto a = arg(i + 3); o += json(field(n, a.first)).dump(); i = a.second; continue; }
        if (t.compare(i, 3, "$id") == 0) { o += std::to_string(n.id); i += 3; continue; }
      }
      o += t[i++];
    }
    return o;
  }
  // what runs after an exec output: the next node's code, inline, or a call when it is reached from several places or in a loop
  std::string execOut(const BpNode& n, const std::string& pin, int lv) {
    auto it = outLinks.find({n.id, pin}); if (it == outLinks.end() || it->second.empty()) return "";
    std::string o;
    for (auto& [to, toPin] : it->second) {
      const BpNode* t = N(to); if (!t) continue;
      std::string fn = "__x" + std::to_string(to) + "_" + bpSanitize(toPin);
      bool onStack = std::find(stack.begin(), stack.end(), std::make_pair(to, toPin)) != stack.end();
      if (onStack || execIns[{to, toPin}] > 1) { fnNeeded.insert(fn); o += (o.empty() ? "" : "\n") + fn + "();"; }
      else o += (o.empty() ? "" : "\n") + execIn(*t, toPin, lv);
    }
    return o;
  }
  std::string execIn(const BpNode& n, const std::string& pin, int lv) {
    const BpNodeDef* d = bpDef(n.type); if (!d) return "";
    std::vector<BpPinDef> st; const BpPinDef* p = pinOf(n, pin, st); if (!p || p->type != PinType::Exec || p->out) { err(n.id, "A wire goes into a pin that is gone", "فيه سلك داخل لمنفذ مو موجود"); return ""; }
    std::string t = p->code.empty() ? d->code : p->code;
    reached.insert(n.id); stack.push_back({n.id, pin});
    std::string body = expand(n, t, lv);
    stack.pop_back();
    return body;
  }
};

// reformat: a brace opens a level, a closing one ends it (the templates are written flat)
std::string pretty(const std::string& code, int base) {
  std::istringstream is(code); std::string ln, o; int lv = base;
  while (std::getline(is, ln)) {
    size_t a = ln.find_first_not_of(" \t"); if (a == std::string::npos) continue; ln = ln.substr(a);
    int open = 0, close = 0; bool str = false; char q = 0;
    for (char c : ln) { if (str) { if (c == q) str = false; continue; } if (c == '\'' || c == '"' || c == '`') { str = true; q = c; } else if (c == '{' || c == '(' || c == '[') open++; else if (c == '}' || c == ')' || c == ']') close++; }
    int lead = 0; for (char c : ln) { if (c == '}' || c == ')' || c == ']') lead++; else break; }
    int net = std::max(-1, std::min(1, open - close));   // "fn(() => {" is one level, "});" closes it
    int here = std::max(0, lv - std::min(lead, 1)); o += std::string(here * 2, ' ') + ln + "\n"; lv = std::max(base, lv + net);
  }
  return o;
}
}

std::string bpCompile(const Blueprint& bp, const std::string& name, std::vector<BpMessage>& msgs) {
  msgs.clear(); Gen g(bp, msgs);
  for (auto& n : bp.nodes) {
    g.byId[n.id] = &n;
    if (!bpDef(n.type)) g.err(n.id, "Unknown node \"" + n.type + "\" (made by a newer engine?)", "عقدة غير معروفة \"" + n.type + "\" (يمكن من نسخة أحدث من المحرك؟)");
  }
  for (auto& l : bp.links) {
    const BpNode* a = g.N(l.from); const BpNode* b = g.N(l.to); if (!a || !b) continue;
    std::vector<BpPinDef> s1, s2; const BpPinDef* pa = g.pinOf(*a, l.fromPin, s1); const BpPinDef* pb = g.pinOf(*b, l.toPin, s2);
    if (!pa || !pb || !pa->out || pb->out) { g.err(b->id, "A wire is connected to a pin that is gone", "فيه سلك موصّل بمنفذ مو موجود"); continue; }
    if (!pinCanLink(pa->type, pb->type)) { g.err(b->id, std::string("Wrong wire: ") + pinTypeEn(pa->type) + " can't go into " + pinTypeEn(pb->type), std::string("سلك غلط: ") + pinTypeAr(pa->type) + " ما يدخل في " + pinTypeAr(pb->type)); continue; }
    if (pb->type != PinType::Exec) { if (g.inLink.count({l.to, l.toPin})) { g.err(b->id, "Two wires go into one input", "سلكين داخلين في مدخل واحد"); continue; } g.inLink[{l.to, l.toPin}] = {l.from, l.fromPin}; }
    else g.execIns[{l.to, l.toPin}]++;
    g.outLinks[{l.from, l.fromPin}].push_back({l.to, l.toPin});
  }
  for (auto& [k, v] : g.outLinks) { std::sort(v.begin(), v.end(), [&](auto& a, auto& b) { const BpNode* x = g.N(a.first); const BpNode* y = g.N(b.first); return x && y ? x->y < y->y : a < b; }); }
  for (auto& n : bp.nodes) if (n.type == "Var.Get" || n.type == "Var.Set") {
    std::string v = n.values.value("Var", std::string()); if (!bp.var(v)) g.err(n.id, "The variable \"" + v + "\" doesn't exist", "المتغير \"" + v + "\" مو موجود");
  }
  for (auto& n : bp.nodes) if (n.type == "Event.Custom") {
    std::string nm = bpSanitize(g.field(n, "Name")); if (g.customEvents.count(nm)) g.err(n.id, "Two custom events are called " + nm, "فيه حدثين مخصصين بنفس الاسم " + nm); g.customEvents.insert(nm); }
  for (auto& n : bp.nodes) if (n.type == "Flow.CallEvent" || n.type == "Flow.SetTimer" || n.type == "Flow.ClearTimer") {
    std::string nm = bpSanitize(g.field(n, "Event")); if (!g.customEvents.count(nm)) g.err(n.id, "There is no custom event called " + nm, "ما فيه حدث مخصص اسمه " + nm); }

  // the events → the engine's functions
  std::map<std::string, std::vector<const BpNode*>> ev;
  for (auto& n : bp.nodes) if (const BpNodeDef* d = bpDef(n.type)) if (d->kind == BpNodeDef::Event) ev[n.type].push_back(&n);
  for (const char* one : {"Event.BeginPlay", "Event.Tick", "Event.ActorBeginOverlap", "Event.Hit", "Event.EndPlay", "Event.AnyKey"})
    if (ev[one].size() > 1) for (size_t i = 1; i < ev[one].size(); i++) g.err(ev[one][i]->id, std::string("Only one ") + bpDef(one)->en + " can be in a Blueprint", std::string("ما يصير أكثر من ") + bpDef(one)->ar + " في البلوبرنت");
  auto body = [&](const BpNode* n) { return g.execOut(*n, "then", 1); };
  auto setOut = [&](const BpNode* n, const std::string& pin, const std::string& value) { return g.outVar(n->id, pin) + " = " + value + ";\n"; };
  std::string fns;
  auto fn = [&](const std::string& head, const std::string& pre, const std::string& b) { fns += head + " {\n" + pre + b + "\n}\n"; };
  { std::string b; for (auto* n : ev["Event.BeginPlay"]) b += body(n); fn("function onStart()", "", b); }
  { std::string b, pre = "__bp.tick(dt);\n"; for (auto* n : ev["Event.Tick"]) { pre += setOut(n, "DeltaSeconds", "dt"); b += body(n); } fn("function onUpdate(dt)", pre, b); }
  if (!ev["Event.ActorBeginOverlap"].empty()) { std::string b, pre; for (auto* n : ev["Event.ActorBeginOverlap"]) { pre += setOut(n, "OtherActor", "other"); b += body(n); } fn("function onOverlap(other)", pre, b); }
  if (!ev["Event.Hit"].empty()) { std::string b, pre; for (auto* n : ev["Event.Hit"]) { pre += setOut(n, "OtherActor", "other"); b += body(n); } fn("function onHit(other)", pre, b); }
  if (!ev["Event.KeyPressed"].empty() || !ev["Event.AnyKey"].empty()) {
    std::string b, pre;
    for (auto* n : ev["Event.AnyKey"]) { pre += setOut(n, "Key", "code"); b += body(n) + "\n"; }
    for (auto* n : ev["Event.KeyPressed"]) b += "if (code === " + json(g.field(*n, "Key")).dump() + ") {\n" + body(n) + "\n}\n";
    fn("function onKey(code)", pre, b);
  }
  if (!ev["Event.EndPlay"].empty()) { std::string b; for (auto* n : ev["Event.EndPlay"]) b += body(n); fn("function onStop()", "", b); }
  for (auto* n : ev["Event.Custom"]) fn("function __ce_" + bpSanitize(g.field(*n, "Name")) + "()", "", body(n));
  std::string uiBinds;
  for (auto* n : ev["Event.WidgetClicked"]) uiBinds += "__bp.uiOn(" + json(g.field(*n, "Widget")).dump() + ", 'click', () => {\n" + body(n) + "\n});\n";
  // exec entries reached from several places (or loops), as functions
  while (true) {
    std::string next; for (auto& f : g.fnNeeded) if (!g.fnDone.count(f)) { next = f; break; } if (next.empty()) break;
    g.fnDone.insert(next);
    size_t us = next.find('_', 3); int id = std::stoi(next.substr(3, us - 3)); std::string pin = next.substr(us + 1);
    const BpNode* n = g.N(id); std::string b; if (n) { g.stack.clear(); b = g.execIn(*n, pin, 1); }
    fn("function " + next + "()", "", b);
  }
  // warnings: nodes that never run, events with nothing on them
  for (auto& n : bp.nodes) {
    const BpNodeDef* d = bpDef(n.type); if (!d || d->kind == BpNodeDef::Comment) continue;
    if (d->kind == BpNodeDef::Exec && !g.reached.count(n.id)) g.err(n.id, "This node never runs: wire its white input (exec) from an event", "هذي العقدة ما راح تشتغل: وصّل مدخلها الأبيض (التنفيذ) من حدث", false);
    if (d->kind == BpNodeDef::Pure && !g.reached.count(n.id)) {
      bool used = false; for (auto& l : bp.links) if (l.from == n.id) used = true;
      if (!used) g.err(n.id, "This node's result isn't used", "نتيجة هذي العقدة ما تستخدمت", false);
    }
  }
  std::string vars;
  for (auto& v : bp.vars) { BpPinDef p; p.type = v.type; vars += "let v_" + bpSanitize(v.name) + " = " + g.literal(p, v.def) + ";\n"; }
  std::string tops; if (!g.topVars.empty()) { tops = "let "; bool first = true; for (auto& v : g.topVars) { tops += (first ? "" : ", ") + v; first = false; } tops += ";\n"; }
  return "// Blueprint " + name + " - made by the Blueprint editor from its nodes (changes here are replaced when it compiles)\n" + BP_RUNTIME +
         vars + tops + pretty(uiBinds + fns, 0);
}
}
