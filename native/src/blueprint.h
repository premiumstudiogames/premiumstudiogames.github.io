#pragma once
// Blueprints: visual scripts made of nodes and wires, like Unreal Engine's Blueprints. A Blueprint is turned into the
// engine's script code whenever it changes (compile), so it runs exactly like a script: in the editor's Play, in
// packaged games and on the console. Saved in the project as a script that also keeps its graph.
// Every node has an English name (the main one), an Arabic name, and an explanation in both.
#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>

namespace pge {

enum class PinType { Exec, Bool, Number, String, Vector, Actor, Array, Any };
const char* pinTypeId(PinType t);                 // "exec", "bool", "number", ...
PinType pinTypeFrom(const std::string& s);
const char* pinTypeEn(PinType t);                 // "Boolean", "Float", ...
const char* pinTypeAr(PinType t);
bool pinCanLink(PinType from, PinType to);        // an output of type `from` can feed an input of type `to`

struct BpPinDef {
  std::string id;           // also the English label ("NewLocation" → "New Location")
  PinType type = PinType::Exec;
  bool out = false;
  nlohmann::json def;       // default value of an input that has no wire
  std::string code;         // an output: its expression (pure); an exec input other than "exec": what it does
};
struct BpFieldDef { std::string id; std::string def; };   // a name set on the node itself (a key, an event's name, a variable)
struct BpNodeDef {
  enum Kind { Event, Exec, Pure, Comment };
  std::string type, cat, en, ar, descEn, descAr;
  Kind kind = Exec;
  std::vector<BpPinDef> pins;
  std::vector<BpFieldDef> fields;
  std::string code;         // what the node does when its "exec" input runs ($in(Pin), $out(Pin), $exec(Pin), $id, $f(Field), $s(Field))
};
const std::vector<BpNodeDef>& bpLibrary();
const BpNodeDef* bpDef(const std::string& type);
std::string bpCategoryAr(const std::string& cat);
std::string bpPinAr(const std::string& pinId);    // Arabic label of a pin
std::string bpPinEn(const std::string& pinId);    // "NewLocation" → "New Location"

struct BpNode {
  int id = 0; std::string type; float x = 0, y = 0;
  nlohmann::json values = nlohmann::json::object();   // pin defaults and fields
  float w = 360, h = 200;                             // comment boxes
};
struct BpLink { int from = 0; std::string fromPin; int to = 0; std::string toPin; };
struct BpVar { std::string name; PinType type = PinType::Number; nlohmann::json def; };
struct Blueprint {
  std::vector<BpNode> nodes; std::vector<BpLink> links; std::vector<BpVar> vars; int nextId = 1;
  BpNode* node(int id); const BpNode* node(int id) const;
  const BpVar* var(const std::string& name) const;
};

nlohmann::json bpToJson(const Blueprint& bp);
Blueprint bpFromJson(const nlohmann::json& j);
Blueprint bpStarter();                                           // a new Blueprint: BeginPlay and Tick, like Unreal
std::vector<BpPinDef> bpPins(const Blueprint& bp, const BpNode& n);   // the pins of a node (a variable's type, ...)
const BpPinDef* bpPin(const Blueprint& bp, const BpNode& n, const std::string& pin, std::vector<BpPinDef>* store);

struct BpMessage { int node = 0; std::string text, textAr; bool error = true; };
// the script code; errors and warnings by node (an error = the code can't be made)
std::string bpCompile(const Blueprint& bp, const std::string& name, std::vector<BpMessage>& msgs);
std::string bpSanitize(const std::string& s);                    // a name usable in code (letters, digits, _)
}
