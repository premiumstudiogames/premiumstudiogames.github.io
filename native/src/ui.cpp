#include "ui.h"
#include "arabic.h"
#include "voxel.h"
#include "gl.h"
#include "log.h"
#include <imgui.h>
#include <stb_image.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <functional>

namespace pge {
using json = nlohmann::json;

static size_t utf8Len(const std::string& s);
/* ------------------------------------------------------------------ the node */
struct Len { float v = 0; int u = 2; };   // u: 0 units, 1 percent of the parent's content, 2 auto (fits its content), 3 fill (v = weight)
struct Ui::Node {
  std::string id, type; Node* parent = nullptr; std::vector<Node*> kids;
  Len x{0, 0}, y{0, 0}, w, h; float ax = 0, ay = 0, minW = 0, minH = 0, maxW = 0, maxH = 0;
  int layout = 0;   // 0 free (x, y from the anchor), 1 row, 2 column, 3 grid
  float gap = 0, pad[4] = {0, 0, 0, 0}; int align = 0, justify = 0, cols = 0, dir = -1; float cellW = 0, cellH = 0;
  unsigned bg = 0, bg2 = 0, border = 0, color = 0xffffffffu, hbg = 0, hcolor = 0, hborder = 0, pbg = 0, tint = 0xffffffffu;
  float radius = 0, borderW = 0, opacity = 1, shadow = 0;
  std::string text; float size = 20, lineH = 1.2f; int talign = -1, valign = -1; bool wrap = false, bold = false;
  std::string image; int block = -1, face = 1, fit = 0; bool cube = false;
  bool visible = true, scroll = false, clip = false, disabled = false, draggable = false, droppable = false, modal = false, pauseGame = false, pass = false;
  int z = 0; json data;
  std::string value, placeholder; double num = 0, mn = 0, mx = 1, step = 0; int maxLen = 200; bool password = false;
  std::vector<std::string> labels; int sel = 0; std::string title; bool closable = true, movable = false;
  std::set<std::string> listen;
  json anim = json::object();   // default show / hide animation
  int ak = 0, from = 3; float at = 1, dur = 0.25f; bool hiding = false;
  // worked out each frame (screen pixels)
  float lx = 0, ly = 0, lw = 0, lh = 0, sx = 0, sy = 0, sw = 0, sh = 0, cl[4] = {0, 0, 0, 0}, alpha = 1, s = 1;
  float contentH = 0, scrollY = 0, headH = 0; size_t caret = 0; double blink = 0;
  bool rd = false;   // worked-out direction (dir -1 inherits)
  bool rtl() const { return rd; }
};
struct Ui::Xf { float s = 1, tx = 0, ty = 0, a = 1; float X(float v) const { return v * s + tx; } float Y(float v) const { return v * s + ty; } };

Ui::Ui() {}
Ui::~Ui() { reset(); for (auto& [k, t] : tex_) if (t.id && glDeleteTextures) glDeleteTextures(1, &t.id); }
void Ui::reset() {
  if (focus_ && SDL_IsTextInputActive()) SDL_StopTextInput();
  nodes_.clear(); roots_.clear(); events_.clear(); drawn_.clear();
  hover_ = press_ = focus_ = drag_ = slide_ = thumb_ = dropOver_ = lastClickNode_ = nullptr; dragging_ = false;
  nav_ = osk_ = oskTarget_ = nullptr; padDrag_ = false; holdDir_ = stickDir_ = -1;
  userScale_ = 1; rtl_ = false; ownEscape_ = false; autoId_ = 0;
}

/* ------------------------------------------------------------------ props */
static unsigned rgba(float r, float g, float b, float a) {
  auto c = [](float v) { return (unsigned)std::lround(std::clamp(v, 0.f, 1.f) * 255); };
  return c(r) | (c(g) << 8) | (c(b) << 16) | (c(a) << 24);
}
static bool parseColor(const json& v, unsigned& out) {
  if (v.is_null()) { out = 0; return true; }
  if (v.is_array() && v.size() >= 3 && v[0].is_number()) { out = rgba(v[0].get<float>(), v[1].get<float>(), v[2].get<float>(), v.size() > 3 ? v[3].get<float>() : 1.f); return true; }
  if (!v.is_string()) return false;
  std::string s = v.get<std::string>(); std::string l; for (char ch : s) if (ch != ' ') l += (char)std::tolower((unsigned char)ch);
  if (l == "transparent" || l == "none" || l.empty()) { out = 0; return true; }
  if (l[0] == '#') { std::string h = l.substr(1); for (char ch : h) if (!std::isxdigit((unsigned char)ch)) return false;
    if (h.size() == 3 || h.size() == 4) { std::string e; for (char ch : h) { e += ch; e += ch; } h = e; }
    if (h.size() != 6 && h.size() != 8) return false;
    unsigned long n = std::strtoul(h.c_str(), nullptr, 16); if (h.size() == 6) n = (n << 8) | 0xff;
    out = rgba(((n >> 24) & 255) / 255.f, ((n >> 16) & 255) / 255.f, ((n >> 8) & 255) / 255.f, (n & 255) / 255.f); return true; }
  float r = 0, g = 0, b = 0, a = 1;
  if (std::sscanf(l.c_str(), "rgba(%f,%f,%f,%f)", &r, &g, &b, &a) == 4 || std::sscanf(l.c_str(), "rgb(%f,%f,%f)", &r, &g, &b) == 3) { out = rgba(r / 255, g / 255, b / 255, a); return true; }
  static const std::map<std::string, unsigned> named = {{"white", 0xffffffffu}, {"black", 0xff000000u}, {"red", 0xff3b3be8u}, {"green", 0xff5ec24au}, {"blue", 0xffff861fu}, {"yellow", 0xff4cc9f2u}, {"gray", 0xff8c8c8cu}, {"grey", 0xff8c8c8cu}};
  auto it = named.find(l); if (it != named.end()) { out = it->second; return true; }
  return false;
}
static Len parseLen(const json& v) {
  Len L; if (v.is_number()) { L.v = v.get<float>(); L.u = 0; return L; }
  if (v.is_string()) { std::string s = v.get<std::string>();
    if (s == "auto") return L;
    if (s.rfind("fill", 0) == 0) { L.u = 3; L.v = 1; if (s.size() > 5 && s[4] == ':') L.v = std::max(0.01f, (float)std::atof(s.c_str() + 5)); return L; }
    if (!s.empty() && s.back() == '%') { L.u = 1; L.v = (float)std::atof(s.c_str()); return L; }
    L.u = 0; L.v = (float)std::atof(s.c_str()); return L; }
  return L;
}
static void parseAnchor(const std::string& a, float& ax, float& ay) {
  ax = a.find("left") != std::string::npos ? 0.f : a.find("right") != std::string::npos ? 1.f : 0.5f;
  ay = a.find("top") != std::string::npos ? 0.f : a.find("bottom") != std::string::npos ? 1.f : 0.5f;
  if (a == "top-left" || a == "topleft") { ax = 0; ay = 0; }
}
static int alignOf(const std::string& s) { return s == "center" || s == "middle" ? 1 : s == "end" || s == "right" || s == "bottom" ? 2 : s == "stretch" ? 3 : s == "between" ? 3 : 0; }
static int animOf(const std::string& s) { return s == "fade" ? 1 : s == "slide" ? 2 : s == "pop" || s == "scale" ? 3 : s == "garage" ? 4 : s == "reveal" ? 5 : 0; }
static int sideOf(const std::string& s) { return s == "left" ? 0 : s == "right" ? 1 : s == "top" ? 2 : 3; }

void Ui::apply(Node& n, const json& p) {
  if (!p.is_object()) return;
  for (auto& [k, v] : p.items()) {
    try {
      if (k == "x") n.x = parseLen(v); else if (k == "y") n.y = parseLen(v);
      else if (k == "w" || k == "width") n.w = parseLen(v); else if (k == "h" || k == "height") n.h = parseLen(v);
      else if (k == "minW") n.minW = v.get<float>(); else if (k == "minH") n.minH = v.get<float>(); else if (k == "maxW") n.maxW = v.get<float>(); else if (k == "maxH") n.maxH = v.get<float>();
      else if (k == "anchor") parseAnchor(v.get<std::string>(), n.ax, n.ay);
      else if (k == "layout") { std::string s = v; n.layout = s == "row" ? 1 : s == "column" || s == "col" ? 2 : s == "grid" ? 3 : 0; }
      else if (k == "gap") n.gap = v.get<float>();
      else if (k == "pad" || k == "padding") {
        if (v.is_number()) { float a = v.get<float>(); for (float& q : n.pad) q = a; }
        else if (v.is_array() && v.size() == 2) { n.pad[0] = n.pad[2] = v[0].get<float>(); n.pad[1] = n.pad[3] = v[1].get<float>(); }
        else if (v.is_array() && v.size() == 4) for (int i = 0; i < 4; i++) n.pad[i] = v[i].get<float>(); }
      else if (k == "align") { std::string s = v; if (n.type == "text" || n.type == "button" || n.type == "input") n.talign = s == "center" ? 1 : s == "right" ? 2 : s == "left" ? 0 : -1; else n.align = alignOf(s); }
      else if (k == "textAlign") { std::string s = v; n.talign = s == "center" ? 1 : s == "right" ? 2 : s == "left" ? 0 : -1; }
      else if (k == "valign") { std::string s = v; n.valign = s == "top" ? 0 : s == "bottom" ? 2 : 1; }
      else if (k == "items") n.align = alignOf(v.get<std::string>());
      else if (k == "justify") n.justify = alignOf(v.get<std::string>());
      else if (k == "cols") n.cols = std::max(1, v.get<int>());
      else if (k == "cell") { if (v.is_number()) n.cellW = n.cellH = v.get<float>(); else if (v.is_array() && v.size() == 2) { n.cellW = v[0].get<float>(); n.cellH = v[1].get<float>(); } }
      else if (k == "dir") n.dir = v.get<std::string>() == "rtl" ? 1 : v.get<std::string>() == "ltr" ? 0 : -1;
      else if (k == "bg" || k == "background") { if (v.is_array() && v.size() == 2 && !v[0].is_number()) { parseColor(v[0], n.bg); parseColor(v[1], n.bg2); } else { parseColor(v, n.bg); n.bg2 = 0; } }
      else if (k == "color") parseColor(v, n.color);
      else if (k == "tint") parseColor(v, n.tint);
      else if (k == "border") { if (v.is_number()) n.borderW = v.get<float>(); else if (v.is_object()) { n.borderW = v.value("width", 1.f); if (v.contains("color")) parseColor(v["color"], n.border); } else { parseColor(v, n.border); if (!n.borderW) n.borderW = 1.5f; } }
      else if (k == "borderColor") { parseColor(v, n.border); if (!n.borderW) n.borderW = 1.5f; }
      else if (k == "radius") n.radius = v.get<float>(); else if (k == "opacity") n.opacity = std::clamp(v.get<float>(), 0.f, 1.f); else if (k == "shadow") n.shadow = v.get<float>();
      else if (k == "hover" && v.is_object()) { if (v.contains("bg")) parseColor(v["bg"], n.hbg); if (v.contains("color")) parseColor(v["color"], n.hcolor); if (v.contains("border")) parseColor(v["border"], n.hborder); }
      else if (k == "press" && v.is_object()) { if (v.contains("bg")) parseColor(v["bg"], n.pbg); }
      else if (k == "text" || k == "label") n.text = v.is_string() ? v.get<std::string>() : v.dump();
      else if (k == "size" || k == "fontSize") n.size = std::clamp(v.get<float>(), 4.f, 400.f);
      else if (k == "lineHeight") n.lineH = std::clamp(v.get<float>(), 0.6f, 4.f);
      else if (k == "wrap") n.wrap = v.get<bool>(); else if (k == "bold") n.bold = v.get<bool>();
      else if (k == "image") { n.image = v.is_string() ? v.get<std::string>() : ""; n.block = -1; }
      else if (k == "block") { n.image.clear(); if (v.is_number()) n.block = v.get<int>(); else if (v.is_object()) { n.block = v.value("id", -1); std::string f = v.value("face", std::string("side")); n.face = f == "top" ? 0 : f == "bottom" ? 2 : 1; n.cube = v.value("cube", false); } else n.block = -1; }
      else if (k == "cube") n.cube = v.get<bool>();
      else if (k == "fit") { std::string s = v; n.fit = s == "cover" ? 1 : s == "stretch" ? 2 : 0; }
      else if (k == "visible") { n.visible = v.get<bool>(); n.hiding = false; n.at = 1; }
      else if (k == "z") n.z = v.get<int>();
      else if (k == "scroll") n.scroll = v.get<bool>(); else if (k == "clip") n.clip = v.get<bool>();
      else if (k == "disabled") n.disabled = v.get<bool>(); else if (k == "draggable") n.draggable = v.get<bool>(); else if (k == "droppable") n.droppable = v.get<bool>();
      else if (k == "modal") n.modal = v.get<bool>(); else if (k == "pause") n.pauseGame = v.get<bool>(); else if (k == "pass") n.pass = v.get<bool>();
      else if (k == "data") n.data = v;
      else if (k == "value") { if (n.type == "input") { n.value = v.is_string() ? v.get<std::string>() : v.dump(); n.caret = n.value.size(); } else if (v.is_number()) setValue(&n, v.get<double>(), false); }
      else if (k == "placeholder") n.placeholder = v; else if (k == "maxLength") n.maxLen = std::max(1, v.get<int>()); else if (k == "password") n.password = v.get<bool>();
      else if (k == "min") n.mn = v.get<double>(); else if (k == "max") n.mx = v.get<double>(); else if (k == "step") n.step = std::max(0.0, v.get<double>());
      else if (k == "labels" && v.is_array()) { n.labels.clear(); for (auto& e : v) n.labels.push_back(e.is_string() ? e.get<std::string>() : e.dump()); n.sel = std::clamp(n.sel, 0, std::max(0, (int)n.labels.size() - 1)); }
      else if (k == "title") n.title = v; else if (k == "closable") n.closable = v.get<bool>(); else if (k == "movable") n.movable = v.get<bool>();
      else if (k == "anim") n.anim = v.is_string() ? json{{"anim", v}} : v;
      else if (k == "id" || k == "parent" || k == "on") {}
      else logMsg("warning: ui: '" + n.id + "' has no setting '" + k + "'", LogKind::Warn);
    } catch (...) { logMsg("warning: ui: '" + n.id + "': bad value for '" + k + "'", LogKind::Warn); }
  }
  if (n.type == "slider") setValue(&n, n.num, false);
}

static const std::set<std::string> TYPES = {"panel", "text", "image", "button", "grid", "list", "input", "slider", "tabs", "window"};
std::string Ui::create(const std::string& type, const std::string& id0, const json& p) {
  if (!TYPES.count(type)) return "unknown type '" + type + "' (panel, text, image, button, grid, list, input, slider, tabs, window)";
  std::string id = id0; if (id.empty()) { do id = "ui" + std::to_string(++autoId_); while (nodes_.count(id)); }
  if (nodes_.count(id)) return "'" + id + "' already exists";
  Node* parent = nullptr;
  if (p.is_object() && p.contains("parent") && p["parent"].is_string() && !p["parent"].get<std::string>().empty()) { parent = find(p["parent"]); if (!parent) return "no parent '" + p["parent"].get<std::string>() + "'"; }
  auto np = std::make_unique<Node>(); Node& n = *np; n.id = id; n.type = type; n.parent = parent;
  // the look each kind starts with (dark, like the console)
  if (type == "button") { n.bg = rgba(0.17f, 0.19f, 0.23f, 0.95f); n.hbg = rgba(0.24f, 0.27f, 0.33f, 1); n.pbg = rgba(0.12f, 0.53f, 1, 1); n.radius = 10; n.pad[0] = n.pad[2] = 12; n.pad[1] = n.pad[3] = 22; n.size = 22; n.talign = 1; }
  else if (type == "grid") { n.layout = 3; n.gap = 8; n.cols = 9; }
  else if (type == "list") { n.layout = 2; n.gap = 6; n.scroll = true; n.align = 3; }
  else if (type == "input") { n.bg = rgba(0.07f, 0.08f, 0.1f, 0.95f); n.border = rgba(0.23f, 0.26f, 0.31f, 1); n.hborder = rgba(0.35f, 0.4f, 0.48f, 1); n.borderW = 1.5f; n.radius = 8; n.w = {360, 0}; n.h = {52, 0}; n.pad[1] = n.pad[3] = 14; n.size = 22; }
  else if (type == "slider") { n.w = {300, 0}; n.h = {36, 0}; n.color = rgba(0.12f, 0.53f, 1, 1); }
  else if (type == "tabs") { n.layout = 2; n.align = 3; n.size = 22; n.color = rgba(0.12f, 0.53f, 1, 1); }
  else if (type == "window") { n.bg = rgba(0.086f, 0.098f, 0.125f, 0.96f); n.border = rgba(1, 1, 1, 0.08f); n.borderW = 1; n.radius = 14; for (float& q : n.pad) q = 20; n.layout = 2; n.gap = 10; n.align = 3; n.shadow = 24; n.size = 26; n.ax = n.ay = 0.5f; }
  apply(n, p);
  Node* raw = np.get(); nodes_[id] = std::move(np); lastId_ = id;
  if (parent) parent->kids.push_back(raw); else roots_.push_back(raw);
  if (p.is_object() && p.contains("visible") && !p["visible"].get<bool>()) raw->visible = false;
  return "";
}
Ui::Node* Ui::find(const std::string& id) const { auto it = nodes_.find(id); return it == nodes_.end() ? nullptr : it->second.get(); }
bool Ui::exists(const std::string& id) const { return find(id) != nullptr; }
std::vector<std::string> Ui::children(const std::string& id) const { std::vector<std::string> r; if (Node* n = find(id)) for (Node* k : n->kids) r.push_back(k->id); else if (id.empty()) for (Node* k : roots_) r.push_back(k->id); return r; }

std::string Ui::set(const std::string& id, const json& p) {
  Node* n = find(id); if (!n) return "no ui '" + id + "'";
  if (p.is_object() && p.contains("parent")) {   // move it
    Node* np = p["parent"].is_string() && !p["parent"].get<std::string>().empty() ? find(p["parent"]) : nullptr;
    if (p["parent"].is_string() && !p["parent"].get<std::string>().empty() && !np) return "no parent '" + p["parent"].get<std::string>() + "'";
    for (Node* q = np; q; q = q->parent) if (q == n) return "'" + id + "' can't go inside itself";
    if (np != n->parent) { unlink(n); n->parent = np; (np ? np->kids : roots_).push_back(n); }
  }
  apply(*n, p); return "";
}
void Ui::unlink(Node* n) { auto& v = n->parent ? n->parent->kids : roots_; v.erase(std::remove(v.begin(), v.end(), n), v.end()); }
void Ui::destroy(Node* n) {
  for (Node* k : std::vector<Node*>(n->kids)) destroy(k);
  for (Node** p : {&hover_, &press_, &focus_, &drag_, &slide_, &thumb_, &dropOver_, &lastClickNode_, &nav_, &osk_, &oskTarget_}) if (*p == n) *p = nullptr;
  if (!focus_ && SDL_IsTextInputActive() && n->type == "input") SDL_StopTextInput();
  if (drag_ == nullptr) dragging_ = false;
  drawn_.erase(std::remove(drawn_.begin(), drawn_.end(), n), drawn_.end());
  nodes_.erase(n->id);
}
void Ui::remove(const std::string& id) { Node* n = find(id); if (!n) return; unlink(n); destroy(n); }
void Ui::clear(const std::string& id) { Node* n = find(id); if (!n) return; for (Node* k : std::vector<Node*>(n->kids)) { unlink(k); destroy(k); } }

json Ui::get(const std::string& id) const {
  Node* n = find(id); if (!n) return nullptr;
  json r = {{"id", n->id}, {"type", n->type}, {"visible", shown(n)}, {"text", n->text}, {"parent", n->parent ? n->parent->id : ""},
            {"rect", {{"x", (n->lx - ox_) / scale_}, {"y", (n->ly - oy_) / scale_}, {"w", n->lw / scale_}, {"h", n->lh / scale_}}}, {"scroll", n->scrollY / scale_}};
  if (n->type == "input") r["value"] = n->value; else if (n->type == "slider") r["value"] = n->num; else if (n->type == "tabs") r["value"] = n->sel;
  if (!n->data.is_null()) r["data"] = n->data;
  r["focused"] = focus_ == n;
  return r;
}
void Ui::setValue(Node* n, double v, bool fire) {
  if (n->type == "tabs") { int s = std::clamp((int)std::lround(v), 0, std::max(0, (int)n->labels.size() - 1)); if (s != n->sel) { n->sel = s; if (fire) emit(n, "change", {{"value", s}}); } return; }
  double lo = std::min(n->mn, n->mx), hi = std::max(n->mn, n->mx); v = std::clamp(v, lo, hi);
  if (n->step > 0) v = lo + std::round((v - lo) / n->step) * n->step;
  v = std::clamp(v, lo, hi);
  if (v != n->num) { n->num = v; if (fire) emit(n, "input", {{"value", v}}); }
}

bool Ui::shown(const Node* n) const { for (const Node* q = n; q; q = q->parent) { if (!q->visible || q->hiding) return false;
    if (q->parent && q->parent->type == "tabs") { auto& K = q->parent->kids; auto it = std::find(K.begin(), K.end(), q); if (it - K.begin() != q->parent->sel) return false; } } return true; }
bool Ui::modal() const { for (auto& [k, n] : nodes_) if (n->modal && shown(n.get())) return true; return false; }
bool Ui::pausing() const { for (auto& [k, n] : nodes_) if (n->pauseGame && shown(n.get())) return true; return false; }
bool Ui::typing() const { return focus_ && shown(focus_); }

void Ui::show(const std::string& id, const json& a0) {
  Node* n = find(id); if (!n) return; json a = a0.is_object() && !a0.empty() ? a0 : n->anim;
  bool was = n->visible && !n->hiding;
  n->visible = true; n->hiding = false; if (was) return;
  n->ak = animOf(a.value("anim", std::string("none"))); n->from = sideOf(a.value("from", std::string(n->ak == 4 ? "top" : "bottom"))); n->dur = std::clamp(a.value("time", 0.25f), 0.01f, 5.f);
  n->at = n->ak ? 0 : 1;
}
void Ui::hide(const std::string& id, const json& a0) {
  Node* n = find(id); if (!n || !n->visible || n->hiding) return; json a = a0.is_object() && !a0.empty() ? a0 : n->anim;
  n->ak = animOf(a.value("anim", std::string("none"))); n->from = sideOf(a.value("from", std::string(n->ak == 4 ? "top" : "bottom"))); n->dur = std::clamp(a.value("time", 0.2f), 0.01f, 5.f);
  if (focus_) for (Node* q = focus_; q; q = q->parent) if (q == n) { focus_ = nullptr; SDL_StopTextInput(); break; }
  if (!n->ak) { n->visible = false; n->at = 1; return; }
  n->hiding = true; n->at = 0;
}
void Ui::focus(const std::string& id) {
  Node* n = id.empty() ? nullptr : find(id);
  if (n && n->type != "input") n = nullptr;
  if (n && console_) { oskOpen(n); return; }   // the console version: the on-screen keyboard
  if (focus_ == n) return;
  if (focus_) emit(focus_, "blur");
  focus_ = n; if (n) { n->caret = n->value.size(); n->blink = time_; emit(n, "focus"); SDL_StartTextInput(); } else SDL_StopTextInput();
}
void Ui::listen(const std::string& id, const std::string& ev, bool on) { if (Node* n = find(id)) { if (on) n->listen.insert(ev); else n->listen.erase(ev); } }
void Ui::emit(Node* n, const std::string& type, json data) {
  if (n->id.rfind("__osk", 0) == 0) { if (type == "click") oskKey(n); return; }   // the on-screen keyboard is the engine's own
  UiEvent e; e.id = n->id; e.type = type; e.data = std::move(data); for (Node* q = n->parent; q; q = q->parent) e.path.push_back(q->id);
  events_.push_back(std::move(e));
}

void Ui::update(float dt) {
  time_ += dt;
  if (osk_ && oskTarget_) if (Node* tx = find("__osk_text")) tx->text = (oskTarget_->password ? std::string(utf8Len(oskTarget_->value), '*') : oskTarget_->value) + "|";
  if (holdDir_ >= 0) { holdT_ -= dt; if (holdT_ <= 0) { holdT_ = 0.11f; navMove(holdDir_); } }   // held D-pad / stick: repeat
  for (auto& [k, n] : nodes_) if (n->at < 1) { n->at = std::min(1.f, n->at + dt / n->dur); if (n->at >= 1 && n->hiding) { n->hiding = false; n->visible = false; } }
}

/* ------------------------------------------------------------------ text */
static ImFont* fontFor(float px) { ImGuiIO& io = ImGui::GetIO(); if (io.Fonts->Fonts.Size > 1 && px >= 17) return io.Fonts->Fonts[1]; return io.Fonts->Fonts[0]; }
static float textW(float px, const std::string& visual) { ImFont* f = fontFor(px); return f->CalcTextSizeA(px, 1e9f, 0, visual.c_str()).x; }
// lines in reading order: '\n' breaks, and words go to the next line past maxW (maxW <= 0: no wrapping)
static std::vector<std::string> wrapLines(const std::string& text, float px, float maxW) {
  std::vector<std::string> lines; std::string cur, word;
  auto width = [&](const std::string& s) { return textW(px, visualText(s)); };
  auto flush = [&] { if (word.empty()) return; std::string t = cur.empty() ? word : cur + " " + word; if (maxW > 0 && !cur.empty() && width(t) > maxW) { lines.push_back(cur); cur = word; } else cur = t; word.clear(); };
  for (char ch : text) { if (ch == '\n') { flush(); lines.push_back(cur); cur.clear(); } else if (ch == ' ') flush(); else word += ch; }
  flush(); lines.push_back(cur); return lines;
}

/* ------------------------------------------------------------------ layout */
static float lenPx(const Len& L, float avail, float sc) { return L.u == 0 ? L.v * sc : L.u == 1 ? L.v / 100.f * avail : 0; }
void Ui::measure(Node& n, float availW, float& w, float& h) {
  float S = scale_, ph = (n.pad[1] + n.pad[3]) * S, pv = (n.pad[0] + n.pad[2]) * S, head = 0;
  if (n.type == "tabs") head = (n.size * 2.2f) * S; else if (n.type == "window" && !n.title.empty()) head = (n.size * 1.9f) * S;
  w = h = 0;
  auto own = [&](const Len& L, float avail, bool horiz, float natural) { return L.u == 0 || L.u == 1 ? lenPx(L, avail, S) : natural; (void)horiz; };
  float cw = 0, ch = 0;
  if (n.type == "text" || n.type == "button" || n.type == "input") {
    float px = n.size * S; float maxW = n.w.u == 0 || n.w.u == 1 ? lenPx(n.w, availW, S) - ph : (n.wrap ? availW - ph : 0);
    auto lines = wrapLines(n.type == "input" ? (n.value.empty() ? n.placeholder : n.value) : n.text, px, n.wrap ? maxW : 0);
    for (auto& l : lines) cw = std::max(cw, textW(px, visualText(l)));
    ch = lines.size() * px * n.lineH;
  } else if (n.type == "image") {
    if (n.block >= 0) cw = ch = 64 * S;
    else if (Tex* t = texture(n.image)) { cw = t->w * S; ch = t->h * S; }
  } else if (n.type == "slider") { cw = 300 * S; ch = 36 * S; }
  if (!n.kids.empty()) {
    std::vector<Node*> K; for (Node* k : n.kids) if (k->visible || k->hiding) K.push_back(k);
    if (n.type == "tabs") { K.clear(); if (n.sel < (int)n.kids.size()) K.push_back(n.kids[n.sel]); }
    float inner = std::max(0.f, (n.w.u == 0 || n.w.u == 1 ? lenPx(n.w, availW, S) : availW) - ph);
    std::vector<std::pair<float, float>> sz; for (Node* k : K) { float kw, kh; measure(*k, inner, kw, kh); sz.push_back({kw, kh}); }
    float g = n.gap * S;
    if (n.layout == 1) { for (size_t i = 0; i < K.size(); i++) { cw += sz[i].first; ch = std::max(ch, sz[i].second); } cw += g * std::max(0, (int)K.size() - 1); }
    else if (n.layout == 2) { for (size_t i = 0; i < K.size(); i++) { ch += sz[i].second; cw = std::max(cw, sz[i].first); } ch += g * std::max(0, (int)K.size() - 1); }
    else if (n.layout == 3) { int c = std::max(1, n.cols); float cellW = n.cellW > 0 ? n.cellW * S : 0, cellH = n.cellH > 0 ? n.cellH * S : 0;
      for (auto& s : sz) { if (!n.cellW) cellW = std::max(cellW, s.first); if (!n.cellH) cellH = std::max(cellH, s.second); }
      int rows = ((int)K.size() + c - 1) / c; int cc = std::min(c, (int)K.size());
      cw = cc * cellW + g * std::max(0, cc - 1); ch = rows * cellH + g * std::max(0, rows - 1); }
    else for (size_t i = 0; i < K.size(); i++) { cw = std::max(cw, lenPx(K[i]->x, inner, S) + sz[i].first); ch = std::max(ch, lenPx(K[i]->y, 0, S) + sz[i].second); }
  }
  w = own(n.w, availW, true, cw + ph); h = own(n.h, 0, false, ch + pv + head);
  if (n.type == "window" && !n.title.empty()) w = std::max(w, textW(n.size * S, visualText(n.title)) + ph + 80 * S);
  if (n.minW > 0) w = std::max(w, n.minW * S); if (n.maxW > 0) w = std::min(w, n.maxW * S);
  if (n.minH > 0) h = std::max(h, n.minH * S); if (n.maxH > 0) h = std::min(h, n.maxH * S);
}
void Ui::layout(Node& n, float x, float y, float w, float h) {
  float S = scale_; n.lx = x; n.ly = y; n.lw = w; n.lh = h;
  bool rtl = n.dir == 1 || (n.dir < 0 && (n.parent ? n.parent->rd : rtl_)); n.rd = rtl;
  n.headH = n.type == "tabs" ? n.size * 2.2f * S : n.type == "window" && !n.title.empty() ? n.size * 1.9f * S : 0;
  float cx = x + n.pad[3] * S, cy = y + n.pad[0] * S + n.headH, cw = std::max(0.f, w - (n.pad[1] + n.pad[3]) * S), ch = std::max(0.f, h - (n.pad[0] + n.pad[2]) * S - n.headH);
  if (n.type == "window" && !n.title.empty()) { cy = y + n.headH + n.pad[0] * S * 0.5f; ch = std::max(0.f, h - n.headH - (n.pad[0] * 0.5f + n.pad[2]) * S); }
  std::vector<Node*> K; for (Node* k : n.kids) if (k->visible || k->hiding) K.push_back(k);
  if (n.type == "tabs") { K.clear(); if (n.sel >= 0 && n.sel < (int)n.kids.size()) K.push_back(n.kids[n.sel]); }
  float g = n.gap * S, sy = n.scroll ? n.scrollY : 0;
  auto sizeOf = [&](Node& k, float availW, float availH, float& kw, float& kh) {
    float mw, mh; measure(k, availW, mw, mh);
    kw = k.w.u == 3 ? mw : mw; kh = k.h.u == 3 ? mh : mh;
    if (k.h.u == 1) kh = k.h.v / 100.f * availH; if (k.w.u == 1) kw = k.w.v / 100.f * availW;
    if (k.minH > 0) kh = std::max(kh, k.minH * S); if (k.maxH > 0) kh = std::min(kh, k.maxH * S);
  };
  float contentEnd = 0;
  if (n.layout == 1 || n.layout == 2) {
    bool row = n.layout == 1; float main = row ? cw : ch, cross = row ? ch : cw;
    std::vector<float> a(K.size()), b(K.size()); float fixed = 0, weight = 0;
    for (size_t i = 0; i < K.size(); i++) { Node& k = *K[i]; float kw, kh; sizeOf(k, cw, ch, kw, kh); a[i] = row ? kw : kh; b[i] = row ? kh : kw;
      const Len& ml = row ? k.w : k.h; const Len& cl = row ? k.h : k.w;
      if (ml.u == 3) { weight += ml.v; a[i] = 0; } else fixed += a[i];
      if (cl.u == 3 || (n.align == 3 && cl.u == 2)) b[i] = cross; }
    float gaps = g * std::max(0, (int)K.size() - 1), free = main - fixed - gaps;
    if (weight > 0) for (size_t i = 0; i < K.size(); i++) { const Len& ml = row ? K[i]->w : K[i]->h; if (ml.u == 3) a[i] = std::max(0.f, free * ml.v / weight); }
    float total = gaps; for (float v : a) total += v;
    float off = 0, extra = 0; if (weight <= 0 && free > 0 && !n.scroll) { if (n.justify == 1) off = free / 2; else if (n.justify == 2) off = free; else if (n.justify == 3 && K.size() > 1) extra = free / (K.size() - 1); }
    float p = off;
    for (size_t i = 0; i < K.size(); i++) { Node& k = *K[i];
      float cpos = n.align == 1 ? (cross - b[i]) / 2 : n.align == 2 ? cross - b[i] : 0;
      if (row) { float kx = rtl ? cx + cw - p - a[i] : cx + p; layout(k, kx, cy + cpos - sy, a[i], b[i]); }
      else layout(k, rtl && n.align != 3 ? cx + cw - cpos - b[i] : cx + cpos, cy + p - sy, b[i], a[i]);
      p += a[i] + g + extra; }
    contentEnd = row ? 0 : total;
    if (!row) contentEnd = std::max(0.f, p - g - extra);
  } else if (n.layout == 3) {
    int c = std::max(1, n.cols); float cellW = n.cellW > 0 ? n.cellW * S : (cw - g * (c - 1)) / c, cellH = n.cellH > 0 ? n.cellH * S : cellW;
    if (!n.cellH && n.cellW <= 0) { for (Node* k : K) if (k->h.u == 0) cellH = std::max(cellH, k->h.v * S); }
    int cols = c; float rowW = cols * cellW + g * (cols - 1), start = n.justify == 1 ? (cw - rowW) / 2 : n.justify == 2 ? cw - rowW : 0;
    for (size_t i = 0; i < K.size(); i++) { int col = (int)i % cols, r = (int)i / cols; float px = start + col * (cellW + g);
      float kx = rtl ? cx + cw - px - cellW : cx + px; layout(*K[i], kx, cy + r * (cellH + g) - sy, cellW, cellH); }
    int rows = ((int)K.size() + cols - 1) / cols; contentEnd = rows * cellH + g * std::max(0, rows - 1);
  } else {
    for (Node* kp : K) { Node& k = *kp; float kw, kh; sizeOf(k, cw, ch, kw, kh);
      if (k.w.u == 3) kw = cw; if (k.h.u == 3) kh = ch;
      float kx = cx + k.ax * (cw - kw) + lenPx(k.x, cw, S), ky = cy + k.ay * (ch - kh) + lenPx(k.y, ch, S) - sy;
      layout(k, kx, ky, kw, kh); contentEnd = std::max(contentEnd, ky + sy - cy + kh); }
  }
  n.contentH = contentEnd;
  if (n.scroll) { float maxS = std::max(0.f, n.contentH - ch); if (n.scrollY > maxS) { float d = n.scrollY - maxS; n.scrollY = maxS; if (d > 0.5f) layout(n, x, y, w, h); } }
}

/* ------------------------------------------------------------------ drawing */
static unsigned withA(unsigned c, float a) { unsigned al = (unsigned)std::lround(((c >> 24) & 255) * std::clamp(a, 0.f, 1.f)); return (c & 0x00ffffffu) | (al << 24); }
static float easeOut(float t) { return 1 - std::pow(1 - t, 3.f); }
static float easeIn(float t) { return t * t * t; }

Ui::Tex* Ui::texture(const std::string& path) {
  if (path.empty()) return nullptr;
  auto it = tex_.find(path); if (it != tex_.end()) return it->second.bad ? nullptr : &it->second;
  Tex t; std::string full = assets_ ? assets_(path) : path;
  int w = 0, h = 0, c = 0; unsigned char* px = stbi_load(full.c_str(), &w, &h, &c, 4);
  if (!px) { t.bad = true; logMsg("warning: ui: can't read the image " + path, LogKind::Warn); tex_[path] = t; return nullptr; }
  t.w = w; t.h = h;
  if (glGenTextures) { glGenTextures(1, &t.id); glBindTexture(GL_TEXTURE_2D, t.id); glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); }
  stbi_image_free(px); tex_[path] = t; return &tex_[path];
}

void Ui::drawText(ImDrawList* dl, Node& n, const Xf& t, float x, float y, float w, float h, unsigned col) {
  std::string src = n.text; if (src.empty()) return;
  float px = n.size * scale_; ImFont* f = fontFor(px);
  auto lines = wrapLines(src, px, n.wrap ? w : 0); float lh = px * n.lineH, total = lines.size() * lh;
  int va = n.valign >= 0 ? n.valign : (n.type == "button" ? 1 : 0);
  float yy = va == 1 ? y + (h - total) / 2 : va == 2 ? y + h - total : y;
  for (auto& l : lines) {
    std::string v = visualText(l); float lw = textW(px, v);
    int al = n.talign >= 0 ? n.talign : (hasRtl(l) ? 2 : 0);
    float xx = al == 1 ? x + (w - lw) / 2 : al == 2 ? x + w - lw : x;
    float oy = (lh - px) / 2;
    ImVec2 p(t.X(xx), t.Y(yy + oy));
    dl->AddText(f, px * t.s, p, col, v.c_str());
    if (n.bold) dl->AddText(f, px * t.s, {p.x + 0.7f * t.s, p.y}, col, v.c_str());
    yy += lh; }
}

void Ui::drawNode(ImDrawList* dl, Node& n, const Xf& P, float pclip[4]) {
  if (!n.visible) return;
  // animation of this node: scale s about its centre, offset, alpha, clipping to its own box
  float pr = n.at >= 1 ? 1 : n.hiding ? 1 - easeIn(n.at) : easeOut(n.at); if (n.hiding && n.at >= 1) pr = 0;
  float s = 1, dx = 0, dy = 0, a = n.opacity; bool ownClip = false; float revealH = 1;
  switch (n.ak) {
    case 1: a *= pr; break;
    case 2: { float d = (n.from <= 1 ? n.lw : n.lh) + 40 * scale_; float o = (1 - pr) * d; if (n.from == 0) dx = -o; else if (n.from == 1) dx = o; else if (n.from == 2) dy = -o; else dy = o; a *= 0.25f + 0.75f * pr; break; }
    case 3: s = 0.85f + 0.15f * pr; a *= pr; break;
    case 4: dy = -(1 - pr) * n.lh; ownClip = true; break;
    case 5: revealH = pr; ownClip = true; break;
  }
  if (n.ak == 0 && n.hiding) a = 0;
  float pcx = n.lx + n.lw / 2, pcy = n.ly + n.lh / 2;
  Xf L; L.s = s; L.tx = pcx - pcx * s + dx; L.ty = pcy - pcy * s + dy;
  Xf T; T.s = L.s * P.s; T.tx = L.tx * P.s + P.tx; T.ty = L.ty * P.s + P.ty; T.a = P.a * a;
  bool dragSrc = dragging_ && drag_ == &n; if (dragSrc) T.a *= 0.4f;
  n.sx = T.X(n.lx); n.sy = T.Y(n.ly); n.sw = n.lw * T.s; n.sh = n.lh * T.s; n.alpha = T.a; n.s = T.s;
  float clip[4] = {pclip[0], pclip[1], pclip[2], pclip[3]};
  if (ownClip) { float bx0 = P.X(n.lx), by0 = P.Y(n.ly), bx1 = P.X(n.lx + n.lw), by1 = P.Y(n.ly + n.lh * revealH);
    clip[0] = std::max(clip[0], bx0); clip[1] = std::max(clip[1], by0); clip[2] = std::min(clip[2], bx1); clip[3] = std::min(clip[3], by1); }
  for (int i = 0; i < 4; i++) n.cl[i] = clip[i];
  if (n.sx > clip[2] || n.sy > clip[3] || n.sx + n.sw < clip[0] || n.sy + n.sh < clip[1] || clip[2] <= clip[0] || clip[3] <= clip[1] || T.a <= 0.003f) return;
  drawn_.push_back(&n);
  dl->PushClipRect({clip[0], clip[1]}, {clip[2], clip[3]}, false);
  float x0 = n.sx, y0 = n.sy, x1 = n.sx + n.sw, y1 = n.sy + n.sh, rad = n.radius * scale_ * T.s;
  bool hov = false, prs = false; for (Node* q = hover_; q; q = q->parent) if (q == &n) hov = true; for (Node* q = press_; q; q = q->parent) if (q == &n) prs = true;
  bool ring = navOn_ && nav_ == &n; if (ring) hov = true;
  if (n.disabled) hov = prs = false;
  bool dropHere = dragging_ && dropOver_ == &n;
  if (n.shadow > 0) { float sh = n.shadow * scale_ * T.s; for (int i = 4; i >= 1; i--) { float e = sh * i / 4; dl->AddRectFilled({x0 - e * 0.5f, y0 - e * 0.25f}, {x1 + e * 0.5f, y1 + e * 0.75f}, withA(0x18000000u, T.a), rad + e); } }
  unsigned bg = prs && n.pbg ? n.pbg : hov && n.hbg ? n.hbg : n.bg; if (dropHere && n.droppable) bg = n.hbg ? n.hbg : rgba(1, 1, 1, 0.18f);
  if (n.disabled) bg = withA(bg, 0.5f);
  if (bg >> 24) {
    int v0 = dl->VtxBuffer.Size; dl->AddRectFilled({x0, y0}, {x1, y1}, withA(bg, T.a), rad);
    if (n.bg2 >> 24) { unsigned b2 = withA(n.bg2, T.a); ImVec4 c0 = ImGui::ColorConvertU32ToFloat4(withA(bg, T.a)), c1 = ImGui::ColorConvertU32ToFloat4(b2);   // top → bottom gradient
      for (int i = v0; i < dl->VtxBuffer.Size; i++) { float t = std::clamp((dl->VtxBuffer[i].pos.y - y0) / std::max(1.f, y1 - y0), 0.f, 1.f);
        ImVec4 c(c0.x + (c1.x - c0.x) * t, c0.y + (c1.y - c0.y) * t, c0.z + (c1.z - c0.z) * t, c0.w + (c1.w - c0.w) * t); dl->VtxBuffer[i].col = ImGui::ColorConvertFloat4ToU32(c); } }
  }
  unsigned bc = (hov || focus_ == &n) && n.hborder ? n.hborder : n.border; if (focus_ == &n) bc = rgba(0.12f, 0.53f, 1, 1);
  if ((bc >> 24) && n.borderW > 0) dl->AddRect({x0, y0}, {x1, y1}, withA(bc, T.a), rad, 0, n.borderW * scale_ * T.s);
  if (ring) { float g = 3 * scale_ * T.s; dl->AddRect({x0 - g, y0 - g}, {x1 + g, y1 + g}, withA(0xffffffffu, T.a), rad + g, 0, 2.5f * scale_ * T.s);
    dl->AddRect({x0 - g * 2.2f, y0 - g * 2.2f}, {x1 + g * 2.2f, y1 + g * 2.2f}, withA(0x40ffffffu, T.a), rad + g * 2.2f, 0, 3 * scale_ * T.s); }
  unsigned tc = withA(hov && n.hcolor ? n.hcolor : n.color, T.a * (n.disabled ? 0.5f : 1));
  float S = scale_, ix = n.lx + n.pad[3] * S, iy = n.ly + n.pad[0] * S + n.headH, iw = std::max(0.f, n.lw - (n.pad[1] + n.pad[3]) * S), ih = std::max(0.f, n.lh - (n.pad[0] + n.pad[2]) * S - n.headH);
  // ---- what each kind draws
  if (n.type == "text" || n.type == "button") drawText(dl, n, T, ix, iy, iw, ih, tc);
  else if (n.type == "image") {
    unsigned tint = withA(n.tint, T.a);
    if (n.block >= 0 && vox_ && vox_->atlasTexture()) {
      const BlockType& bt = vox_->block(n.block); int tiles = std::max(1, vox_->atlasTiles()); glm::ivec2 as = vox_->atlasSize();
      float cellU = 1.f / tiles, cellV = as.y > 0 ? (as.x / (float)tiles) / as.y : 1.f;
      auto uvOf = [&](int face, ImVec2& a0, ImVec2& a1) { int tl = bt.tile[face]; if (tl < 0) tl = bt.tile[1]; if (tl < 0) { a0 = a1 = ImVec2(0, 0); return false; } a0 = ImVec2((tl % tiles) * cellU, (tl / tiles) * cellV); a1 = ImVec2(a0.x + cellU, a0.y + cellV); return true; };
      auto col = [&](int face, float shade) { glm::vec3 c = bt.color[face] * shade; ImVec4 t4 = ImGui::ColorConvertU32ToFloat4(tint); return ImGui::ColorConvertFloat4ToU32(ImVec4(std::min(1.f, c.r * t4.x), std::min(1.f, c.g * t4.y), std::min(1.f, c.b * t4.z), t4.w)); };
      ImTextureID tex = (ImTextureID)(intptr_t)vox_->atlasTexture();
      float side = std::min(n.sw, n.sh), cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
      if (n.cube) {   // a little 3D block, like an inventory icon
        float hw = side * 0.46f, q = side * 0.25f, top = cy - side * 0.5f, dn = side * 0.5f; ImVec2 a0, a1;
        ImVec2 T0(cx, top), R0(cx + hw, top + q), C0(cx, top + 2 * q), L0(cx - hw, top + q);
        if (uvOf(0, a0, a1)) dl->AddImageQuad(tex, T0, R0, C0, L0, {a0.x, a0.y}, {a1.x, a0.y}, {a1.x, a1.y}, {a0.x, a1.y}, col(0, 1.f));
        if (uvOf(1, a0, a1)) { dl->AddImageQuad(tex, L0, C0, {C0.x, C0.y + dn}, {L0.x, L0.y + dn}, {a0.x, a0.y}, {a1.x, a0.y}, {a1.x, a1.y}, {a0.x, a1.y}, col(1, 0.78f));
          dl->AddImageQuad(tex, C0, R0, {R0.x, R0.y + dn}, {C0.x, C0.y + dn}, {a0.x, a0.y}, {a1.x, a0.y}, {a1.x, a1.y}, {a0.x, a1.y}, col(1, 0.6f)); }
      } else { ImVec2 a0, a1; if (uvOf(n.face, a0, a1)) dl->AddImage(tex, {cx - side / 2, cy - side / 2}, {cx + side / 2, cy + side / 2}, a0, a1, col(n.face, 1.f)); }
    } else if (Tex* t = texture(n.image); t && t->id) {
      float bw = n.sw, bh = n.sh, iw2 = (float)t->w, ih2 = (float)t->h; ImVec2 p0(x0, y0), p1(x1, y1), u0(0, 0), u1(1, 1);
      if (n.fit == 0) { float k = std::min(bw / iw2, bh / ih2); float w2 = iw2 * k, h2 = ih2 * k; p0 = {x0 + (bw - w2) / 2, y0 + (bh - h2) / 2}; p1 = {p0.x + w2, p0.y + h2}; }
      else if (n.fit == 1) { float k = std::max(bw / iw2, bh / ih2); float cu = bw / (iw2 * k), cv = bh / (ih2 * k); u0 = {(1 - cu) / 2, (1 - cv) / 2}; u1 = {u0.x + cu, u0.y + cv}; }
      if (rad > 0) dl->AddImageRounded((ImTextureID)(intptr_t)t->id, p0, p1, u0, u1, tint, rad); else dl->AddImage((ImTextureID)(intptr_t)t->id, p0, p1, u0, u1, tint);
    }
  } else if (n.type == "input") {
    float px = n.size * S * T.s; ImFont* f = fontFor(n.size * S);
    bool empty = n.value.empty(); std::string shownT = n.password ? std::string(std::count_if(n.value.begin(), n.value.end(), [](char ch) { return (ch & 0xC0) != 0x80; }) * 1, '*') : n.value;
    std::string vis = visualText(empty ? n.placeholder : shownT); bool rtl = hasRtl(empty ? n.placeholder : shownT) || (empty && n.rtl());
    float bx0 = T.X(ix), bx1 = T.X(ix + iw), tw = f->CalcTextSizeA(px, 1e9f, 0, vis.c_str()).x, ty = T.Y(iy + (ih - n.size * S) / 2);
    // caret position: LTR from the left, Arabic from the right
    float caretX;
    std::string pre = n.password ? std::string(std::count_if(n.value.begin(), n.value.begin() + std::min(n.caret, n.value.size()), [](char ch) { return (ch & 0xC0) != 0x80; }), '*') : n.value.substr(0, std::min(n.caret, n.value.size()));
    float preW = f->CalcTextSizeA(px, 1e9f, 0, visualText(pre).c_str()).x;
    float shift = 0, avail = bx1 - bx0;
    if (!rtl) { if (!empty && preW > avail - 4) shift = preW - (avail - 4); caretX = bx0 + preW - shift; }
    else { if (!empty && preW > avail - 4) shift = preW - (avail - 4); caretX = bx1 - preW + shift; }
    float tx = rtl ? bx1 - tw + shift : bx0 - shift;
    dl->PushClipRect({std::max(clip[0], bx0 - 1), clip[1]}, {std::min(clip[2], bx1 + 1), clip[3]}, true);
    dl->AddText(f, px, {tx, ty}, empty ? withA(tc, 0.45f) : tc, vis.c_str());
    if (focus_ == &n && std::fmod(time_ - n.blink, 1.0) < 0.55) dl->AddLine({caretX, ty}, {caretX, ty + px}, tc, 2 * T.s);
    dl->PopClipRect();
  } else if (n.type == "slider") {
    float lo = std::min(n.mn, n.mx), hi = std::max(n.mn, n.mx), t = hi > lo ? (float)((n.num - lo) / (hi - lo)) : 0;
    float tyc = (y0 + y1) / 2, th = 6 * S * T.s, kr = 11 * S * T.s, tx0 = x0 + kr, tx1 = x1 - kr; if (n.rtl()) t = 1 - t;
    dl->AddRectFilled({tx0, tyc - th / 2}, {tx1, tyc + th / 2}, withA(0x40ffffffu, T.a), th);
    float kx = tx0 + (tx1 - tx0) * t;
    if (n.rtl()) dl->AddRectFilled({kx, tyc - th / 2}, {tx1, tyc + th / 2}, withA(n.color, T.a), th); else dl->AddRectFilled({tx0, tyc - th / 2}, {kx, tyc + th / 2}, withA(n.color, T.a), th);
    dl->AddCircleFilled({kx, tyc}, kr * (slide_ == &n || hov ? 1.15f : 1.f), withA(0xffffffffu, T.a), 24);
  } else if (n.type == "tabs" && !n.labels.empty()) {
    float hx0 = x0, hy0 = y0, hy1 = T.Y(n.ly + n.headH), hw = (x1 - x0) / n.labels.size(); float px = n.size * S;
    dl->AddLine({x0, hy1}, {x1, hy1}, withA(0x30ffffffu, T.a), 1);
    for (size_t i = 0; i < n.labels.size(); i++) { size_t vi = n.rtl() ? n.labels.size() - 1 - i : i; float lx0 = hx0 + hw * vi;
      std::string v = visualText(n.labels[i]); float lw = textW(px, v) * T.s; bool on = (int)i == n.sel;
      dl->AddText(fontFor(px), px * T.s, {lx0 + (hw - lw) / 2, hy0 + (hy1 - hy0 - px * T.s) / 2}, withA(on ? 0xffffffffu : 0x9effffffu, T.a), v.c_str());
      if (on) dl->AddRectFilled({lx0 + hw * 0.2f, hy1 - 3 * S * T.s}, {lx0 + hw * 0.8f, hy1}, withA(n.color, T.a), 2); }
  } else if (n.type == "window" && !n.title.empty()) {
    float px = n.size * S, hy1 = T.Y(n.ly + n.headH); std::string v = visualText(n.title); bool r = hasRtl(n.title) || n.rtl();
    float lw = textW(px, v) * T.s, padX = n.pad[3] * S * T.s;
    dl->AddText(fontFor(px), px * T.s, {r ? x1 - padX - lw : x0 + padX, y0 + (hy1 - y0 - px * T.s) / 2}, tc, v.c_str());
    dl->AddLine({x0 + padX, hy1}, {x1 - padX, hy1}, withA(0x22ffffffu, T.a), 1);
    if (n.closable) { float cs = (hy1 - y0) * 0.36f, ccx = r ? x0 + padX + cs : x1 - padX - cs, ccy = (y0 + hy1) / 2; bool ch = hover_ == &n && std::abs(mx_ - ccx) < cs * 1.4f && std::abs(my_ - ccy) < cs * 1.4f;
      if (ch) dl->AddCircleFilled({ccx, ccy}, cs * 1.3f, withA(0x30ffffffu, T.a));
      dl->AddLine({ccx - cs * 0.55f, ccy - cs * 0.55f}, {ccx + cs * 0.55f, ccy + cs * 0.55f}, tc, 2 * T.s); dl->AddLine({ccx + cs * 0.55f, ccy - cs * 0.55f}, {ccx - cs * 0.55f, ccy + cs * 0.55f}, tc, 2 * T.s); }
  }
  // ---- children
  float kclip[4] = {clip[0], clip[1], clip[2], clip[3]};
  if (n.scroll || n.clip || n.type == "tabs" || n.type == "window") { float c0 = T.X(ix), c1 = T.Y(n.type == "window" || n.type == "tabs" ? n.ly + n.headH : iy), c2 = T.X(ix + iw), c3 = T.Y(iy + ih);
    if (n.scroll || n.clip) { kclip[0] = std::max(kclip[0], c0 - (n.scroll ? 0 : 0)); kclip[1] = std::max(kclip[1], c1); kclip[2] = std::min(kclip[2], c2); kclip[3] = std::min(kclip[3], c3); }
    else { kclip[1] = std::max(kclip[1], c1); } }
  if (!n.kids.empty()) {
    std::vector<Node*> K = n.kids; std::stable_sort(K.begin(), K.end(), [](Node* a, Node* b) { return a->z < b->z; });
    dl->PopClipRect();
    for (size_t i = 0; i < K.size(); i++) { if (n.type == "tabs" && K[i] != (n.sel < (int)n.kids.size() ? n.kids[n.sel] : nullptr)) continue; drawNode(dl, *K[i], T, kclip); }
    dl->PushClipRect({clip[0], clip[1]}, {clip[2], clip[3]}, false);
  }
  // scroll bar
  if (n.scroll) { float viewH = ih; if (n.contentH > viewH + 1) {
      float bw = 6 * S * T.s, trackY0 = T.Y(iy), trackY1 = T.Y(iy + ih), th = std::max(30 * S * T.s, (trackY1 - trackY0) * viewH / n.contentH);
      float t = n.scrollY / std::max(1.f, n.contentH - viewH), ty = trackY0 + (trackY1 - trackY0 - th) * t, bx = n.rtl() ? x0 + 3 * S * T.s : x1 - bw - 3 * S * T.s;
      dl->AddRectFilled({bx, ty}, {bx + bw, ty + th}, withA(thumb_ == &n ? 0xc0ffffffu : 0x60ffffffu, T.a), bw); } }
  dl->PopClipRect();
}

void Ui::draw(ImDrawList* dl, float x, float y, float w, float h) {
  sw_ = w; sh_ = h; ox_ = x; oy_ = y; scale_ = (h / 1080.f) * userScale_;
  float vw = w / scale_, vh = h / scale_;
  // the screen is the root: roots are laid out like children of a free panel the size of the screen
  for (Node* r : roots_) { if (!(r->visible || r->hiding)) continue; float rw, rh; measure(*r, w, rw, rh);
    if (r->w.u == 3) rw = w; if (r->h.u == 3) rh = h;
    float rx = x + r->ax * (w - rw) + lenPx(r->x, w, scale_), ry = y + r->ay * (h - rh) + lenPx(r->y, h, scale_); layout(*r, rx, ry, rw, rh); }
  (void)vw; (void)vh;
  drawn_.clear();
  float clip[4] = {x, y, x + w, y + h}; Xf I;
  std::vector<Node*> R = roots_; std::stable_sort(R.begin(), R.end(), [](Node* a, Node* b) { return a->z < b->z; });
  for (Node* r : R) drawNode(dl, *r, I, clip);
  // what's being dragged follows the mouse
  if (dragging_ && drag_ && padDrag_ && nav_) { mx_ = nav_->sx + nav_->sw * 0.5f + 8 * scale_; my_ = nav_->sy + nav_->sh * 0.5f + 8 * scale_; grabX_ = drag_->sw * 0.5f; grabY_ = drag_->sh * 0.5f; }
  if (dragging_ && drag_) { Node& d = *drag_; Xf G; G.tx = mx_ - grabX_ - d.lx; G.ty = my_ - grabY_ - d.ly; G.a = 0.9f;
    bool v = d.visible; bool ws = d.hiding; d.hiding = false; dragging_ = false; size_t keep = drawn_.size();
    float big[4] = {x, y, x + w, y + h}; drawNode(dl, d, G, big); drawn_.resize(keep); dragging_ = true; d.visible = v; d.hiding = ws;
    // the drawn rects of the dragged node are those of the copy now: put the real ones back next frame (layout runs again)
  }
}

/* ------------------------------------------------------------------ input */
static bool interactive(const Ui::Node& n);
bool interactive(const Ui::Node& n) {
  if (n.disabled) return false;
  if (n.type == "button" || n.type == "input" || n.type == "slider" || n.type == "tabs") return true;
  if (n.type == "window" && (n.closable || n.movable) && !n.title.empty()) return true;
  for (auto& e : n.listen) if (e == "click" || e == "rightclick" || e == "dblclick" || e == "down" || e == "up" || e == "enter" || e == "leave") return true;
  return n.draggable || n.droppable;
}
// the deepest node under the point that catches the mouse (has a background, an image, or reacts), then up to one that wants it
Ui::Node* Ui::hit(float x, float y, int want) const {
  for (auto it = drawn_.rbegin(); it != drawn_.rend(); ++it) { Node* n = *it;
    if (n->pass || n->alpha < 0.02f || n->hiding) continue;
    if (x < n->sx || y < n->sy || x >= n->sx + n->sw || y >= n->sy + n->sh) continue;
    if (x < n->cl[0] || y < n->cl[1] || x >= n->cl[2] || y >= n->cl[3]) continue;
    bool solid = (n->bg >> 24) || n->type == "image" || interactive(*n) || n->modal || n->scroll || !n->listen.empty();
    if (!solid) continue;
    if (want == 0) return n;
    for (Node* q = n; q; q = q->parent) {
      if (want == 1 && (interactive(*q) || !q->listen.empty())) return q;
      if (want == 4 && q->droppable && !q->disabled) return q;
      if (want == 8 && q->scroll) return q;
      if (want == 16) return n;   // the deepest: event target
    }
    return nullptr;
  }
  return nullptr;
}
void Ui::scrollBy(Node* n, float dy) { n->scrollY = std::max(0.f, n->scrollY + dy); }

static size_t prevChar(const std::string& s, size_t i) { if (i == 0) return 0; i--; while (i > 0 && (s[i] & 0xC0) == 0x80) i--; return i; }
static size_t nextChar(const std::string& s, size_t i) { if (i >= s.size()) return s.size(); i++; while (i < s.size() && (s[i] & 0xC0) == 0x80) i++; return i; }
static size_t utf8Len(const std::string& s) { size_t n = 0; for (char c : s) if ((c & 0xC0) != 0x80) n++; return n; }
void Ui::textKey(const SDL_Event& e) {
  Node& n = *focus_; SDL_Keycode k = e.key.keysym.sym; bool ctrl = e.key.keysym.mod & (KMOD_CTRL | KMOD_GUI); bool rtl = hasRtl(n.value);
  n.blink = time_; size_t c = std::min(n.caret, n.value.size());
  auto changed = [&] { emit(&n, "input", {{"value", n.value}}); };
  if (k == SDLK_BACKSPACE) { if (c > 0) { size_t p = ctrl ? 0 : prevChar(n.value, c); n.value.erase(p, c - p); n.caret = p; changed(); } }
  else if (k == SDLK_DELETE) { if (c < n.value.size()) { size_t q = nextChar(n.value, c); n.value.erase(c, q - c); changed(); } }
  else if (k == SDLK_LEFT) n.caret = rtl ? nextChar(n.value, c) : prevChar(n.value, c);
  else if (k == SDLK_RIGHT) n.caret = rtl ? prevChar(n.value, c) : nextChar(n.value, c);
  else if (k == SDLK_HOME) n.caret = 0; else if (k == SDLK_END) n.caret = n.value.size();
  else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) emit(&n, "submit", {{"value", n.value}});
  else if (k == SDLK_ESCAPE) focus("");
  else if (k == SDLK_TAB) {   // the next text field that is shown
    std::vector<Node*> ins; for (Node* d : drawn_) if (d->type == "input" && !d->disabled) ins.push_back(d);
    auto it = std::find(ins.begin(), ins.end(), &n); if (ins.size() > 1 && it != ins.end()) { size_t i = it - ins.begin(); i = (e.key.keysym.mod & KMOD_SHIFT) ? (i + ins.size() - 1) % ins.size() : (i + 1) % ins.size(); focus(ins[i]->id); } }
  else if (ctrl && k == SDLK_v) { if (char* t = SDL_GetClipboardText()) { std::string s = t; SDL_free(t); s.erase(std::remove(s.begin(), s.end(), '\n'), s.end()); s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
      while (!s.empty() && utf8Len(n.value) + utf8Len(s) > (size_t)n.maxLen) s.erase(prevChar(s, s.size()));
      n.value.insert(c, s); n.caret = c + s.size(); if (!s.empty()) changed(); } }
  else if (ctrl && k == SDLK_c && !n.password) SDL_SetClipboardText(n.value.c_str());
  else if (ctrl && k == SDLK_a) n.caret = n.value.size();
}

bool Ui::event(const SDL_Event& e) {
  bool rel = SDL_GetRelativeMouseMode();
  auto mods = [] { SDL_Keymod m = SDL_GetModState(); return json{{"shift", (m & KMOD_SHIFT) != 0}, {"ctrl", (m & KMOD_CTRL) != 0}, {"alt", (m & KMOD_ALT) != 0}}; };
  switch (e.type) {
    case SDL_MOUSEMOTION: {
      if (rel) return false;
      mx_ = (float)e.motion.x; my_ = (float)e.motion.y; if (navOn_ && (std::abs(e.motion.xrel) + std::abs(e.motion.yrel)) > 2) navOn_ = false;
      Node* top = hit(mx_, my_, 16); Node* nh = hit(mx_, my_, 1);
      if (nh != hover_) {   // enter / leave on the nodes that listen
        std::set<Node*> was, now; for (Node* q = hover_; q; q = q->parent) was.insert(q); for (Node* q = nh; q; q = q->parent) now.insert(q);
        for (Node* q : was) if (!now.count(q) && q->listen.count("leave")) emit(q, "leave");
        for (Node* q : now) if (!was.count(q) && q->listen.count("enter")) emit(q, "enter");
        hover_ = nh; }
      hoverSolid_ = top != nullptr;
      if (press_ && !dragging_ && !slide_ && !thumb_) {
        Node* src = nullptr; for (Node* q = press_; q; q = q->parent) if (q->draggable && !q->disabled) { src = q; break; }
        if (src && std::hypot(mx_ - pressX_, my_ - pressY_) > 6) { dragging_ = true; drag_ = src; grabX_ = pressX_ - src->sx; grabY_ = pressY_ - src->sy; emit(src, "dragstart", {{"data", src->data}}); } }
      if (dragging_) { Node* d = hit(mx_, my_, 4); for (Node* q = d; q; q = q->parent) if (q == drag_) { d = nullptr; break; } dropOver_ = d; }
      if (slide_) { float kr = 11 * scale_ * slide_->s, x0 = slide_->sx + kr, x1 = slide_->sx + slide_->sw - kr; float t = std::clamp((mx_ - x0) / std::max(1.f, x1 - x0), 0.f, 1.f); if (slide_->rtl()) t = 1 - t;
        setValue(slide_, std::min(slide_->mn, slide_->mx) + t * std::abs(slide_->mx - slide_->mn), true); }
      if (thumb_) { Node& n = *thumb_; float ih = std::max(1.f, n.sh - (n.pad[0] + n.pad[2]) * scale_ * n.s); float viewH = n.lh - (n.pad[0] + n.pad[2]) * scale_ - n.headH;
        float th = std::max(30 * scale_, viewH * viewH / std::max(1.f, n.contentH)); float t = (my_ - thumbGrab_ - n.sy - n.pad[0] * scale_ * n.s) / std::max(1.f, ih - th * n.s);
        n.scrollY = std::clamp(t, 0.f, 1.f) * std::max(0.f, n.contentH - viewH); }
      if (press_ && press_->type == "window" && press_->movable && !dragging_) { Node& w = *press_; float dx = (mx_ - pressX_) / scale_, dy = (my_ - pressY_) / scale_; pressX_ = mx_; pressY_ = my_;
        if (w.x.u != 0) w.x = {lenPx(w.x, sw_, scale_) / scale_, 0}; if (w.y.u != 0) w.y = {lenPx(w.y, sh_, scale_) / scale_, 0}; w.x.v += dx; w.y.v += dy; }
      return hoverSolid_ || dragging_ || slide_ || thumb_ || modal();
    }
    case SDL_MOUSEBUTTONDOWN: {
      if (rel) return false;
      mx_ = (float)e.button.x; my_ = (float)e.button.y;
      Node* top = hit(mx_, my_, 16); Node* ia = hit(mx_, my_, 1);
      if (!top) { if (focus_) focus(""); return modal(); }
      if (focus_ && ia != focus_) focus("");
      if (e.button.button == SDL_BUTTON_RIGHT) { if (ia || top) emit(top, "rightclick", mods()); return true; }
      if (e.button.button != SDL_BUTTON_LEFT) return true;
      press_ = top; pressX_ = mx_; pressY_ = my_;
      if (ia && ia->listen.count("down")) emit(ia, "down", mods());
      Node* sc = hit(mx_, my_, 8);
      if (sc && sc->contentH > 0) { Node& n = *sc; float bw = 14 * scale_ * n.s, bx = n.rtl() ? n.sx : n.sx + n.sw - bw;
        float viewH = n.lh - (n.pad[0] + n.pad[2]) * scale_ - n.headH; if (n.contentH > viewH + 1 && mx_ >= bx && mx_ <= bx + bw) { thumb_ = sc; float th = std::max(30 * scale_, viewH * viewH / n.contentH);
          float trackH = viewH - th, t = n.scrollY / std::max(1.f, n.contentH - viewH); float thumbY = n.sy + n.pad[0] * scale_ * n.s + trackH * t * n.s;
          thumbGrab_ = (my_ >= thumbY && my_ <= thumbY + th * n.s) ? my_ - thumbY : th * n.s / 2; return true; } }
      if (ia) {
        if (ia->type == "input") focus(ia->id);
        else if (ia->type == "slider") { slide_ = ia; SDL_Event m{}; m.type = SDL_MOUSEMOTION; m.motion.x = (int)mx_; m.motion.y = (int)my_; press_ = nullptr; event(m); press_ = top; }
        else if (ia->type == "tabs" && my_ < ia->sy + ia->headH * ia->s && !ia->labels.empty()) { int i = (int)((mx_ - ia->sx) / (ia->sw / ia->labels.size())); i = std::clamp(i, 0, (int)ia->labels.size() - 1); if (ia->rtl()) i = (int)ia->labels.size() - 1 - i; setValue(ia, i, true); }
        else if (ia->type == "window" && my_ < ia->sy + ia->headH * ia->s) {
          float cs = ia->headH * ia->s * 0.36f, padX = ia->pad[3] * scale_ * ia->s; bool r = hasRtl(ia->title) || ia->rtl(); float ccx = r ? ia->sx + padX + cs : ia->sx + ia->sw - padX - cs, ccy = ia->sy + ia->headH * ia->s / 2;
          if (ia->closable && std::abs(mx_ - ccx) < cs * 1.4f && std::abs(my_ - ccy) < cs * 1.4f) { emit(ia, "close"); hide(ia->id, json::object()); press_ = nullptr; }
          else if (ia->movable) press_ = ia; }
      }
      return true;
    }
    case SDL_MOUSEBUTTONUP: {
      if (e.button.button != SDL_BUTTON_LEFT) return !rel && (hoverSolid_ || modal());
      mx_ = (float)e.button.x; my_ = (float)e.button.y; bool used = press_ || dragging_ || slide_ || thumb_;
      if (dragging_ && drag_) { Node* t = dropOver_; json d = {{"from", drag_->id}, {"data", drag_->data}}; if (t) { d["to"] = t->id; emit(t, "drop", d); }
        emit(drag_, "dragend", {{"to", t ? t->id : ""}, {"data", drag_->data}}); }
      else if (slide_) emit(slide_, "change", {{"value", slide_->num}});
      else if (press_ && !thumb_) { Node* top = hit(mx_, my_, 16); bool same = false; for (Node* q = top; q; q = q->parent) if (q == press_) same = true;
        if (same && press_ && !press_->disabled && press_->type != "input" && press_->type != "slider") {
          json m = mods(); emit(press_, "click", m);
          if (lastClickNode_ == press_ && time_ - lastClick_ < 0.35) emit(press_, "dblclick", m);
          lastClickNode_ = press_; lastClick_ = time_; }
        Node* ia = hit(mx_, my_, 1); if (ia && ia->listen.count("up")) emit(ia, "up", mods()); }
      press_ = drag_ = slide_ = thumb_ = dropOver_ = nullptr; dragging_ = false;
      return used || (!rel && (hoverSolid_ || modal()));
    }
    case SDL_MOUSEWHEEL: {
      if (rel && !modal()) return false;
      mx_ = (float)e.wheel.mouseX; my_ = (float)e.wheel.mouseY;
      if (Node* sc = hit(mx_, my_, 8)) { scrollBy(sc, -e.wheel.preciseY * 60 * scale_); return true; }
      return hit(mx_, my_, 0) != nullptr || modal();
    }
    case SDL_TEXTINPUT: {
      if (!typing()) return false; Node& n = *focus_; std::string s = e.text.text;
      if (utf8Len(n.value) + utf8Len(s) > (size_t)n.maxLen) return true;
      size_t c = std::min(n.caret, n.value.size()); n.value.insert(c, s); n.caret = c + s.size(); n.blink = time_; emit(&n, "input", {{"value", n.value}}); return true;
    }
    case SDL_KEYDOWN: if (typing()) { textKey(e); return true; } return false;
    default: return false;
  }
}

/* ------------------------------------------------------------------ the controller */
Ui::Node* Ui::scopeRoot() const {   // the menu the controller moves in: the keyboard, else the top shown modal
  if (osk_) return osk_;
  for (auto it = drawn_.rbegin(); it != drawn_.rend(); ++it) if ((*it)->modal && shown(*it)) return *it;
  return nullptr;
}
std::vector<Ui::Node*> Ui::navCandidates() const {
  Node* root = scopeRoot(); std::vector<Node*> c;
  auto inside = [&](Node* n) { if (!root) return true; for (Node* q = n; q; q = q->parent) if (q == root) return true; return false; };
  for (Node* n : drawn_) { if (n->disabled || n->pass || n->alpha < 0.2f || !shown(n) || !inside(n) || n == root) continue;
    bool ok = interactive(*n) || (n->parent && n->parent->listen.count("click") && ((n->bg >> 24) || n->type == "image"));
    if (n->type == "window" || n->type == "text") ok = ok && n->listen.count("click");
    if (ok) c.push_back(n); }
  // the most specific: a node that holds another candidate isn't one itself
  std::vector<Node*> r; for (Node* n : c) { bool holds = false; for (Node* m : c) if (m != n) for (Node* q = m->parent; q; q = q->parent) if (q == n) { holds = true; break; } if (!holds) r.push_back(n); }
  return r;
}
void Ui::ensureVisible(Node* n) {
  for (Node* q = n->parent; q; q = q->parent) { if (!q->scroll) continue;
    float top = q->ly + q->pad[0] * scale_ + q->headH, bot = q->ly + q->lh - q->pad[2] * scale_;
    if (n->ly < top) q->scrollY = std::max(0.f, q->scrollY - (top - n->ly) - 8 * scale_);
    else if (n->ly + n->lh > bot) q->scrollY += (n->ly + n->lh - bot) + 8 * scale_; }
}
void Ui::navMove(int dir) {
  auto C = navCandidates(); if (C.empty()) return;
  if (!nav_ || std::find(C.begin(), C.end(), nav_) == C.end()) {   // first: the one at the top (left, or right in a right-to-left menu)
    Node* best = nullptr; for (Node* n : C) if (!best || n->sy < best->sy - 2 || (std::abs(n->sy - best->sy) <= 2 && (rtl_ ? n->sx > best->sx : n->sx < best->sx))) best = n;
    nav_ = best; ensureVisible(nav_); return; }
  // a slider takes left / right
  if (nav_->type == "slider" && dir <= 1) { double st = nav_->step > 0 ? nav_->step : std::abs(nav_->mx - nav_->mn) / 20; bool inc = (dir == 1) != nav_->rtl();
    setValue(nav_, nav_->num + (inc ? st : -st), true); emit(nav_, "change", {{"value", nav_->num}}); return; }
  float fx = nav_->sx + nav_->sw / 2, fy = nav_->sy + nav_->sh / 2; Node* best = nullptr; float bs = 1e30f;
  for (Node* n : C) { if (n == nav_) continue; float cx = n->sx + n->sw / 2, cy = n->sy + n->sh / 2, dx = cx - fx, dy = cy - fy;
    float along = dir == 0 ? -dx : dir == 1 ? dx : dir == 2 ? -dy : dy, perp = dir <= 1 ? std::abs(dy) : std::abs(dx);
    // the edges, so a wide button next to a small one is still "to the right"
    float gap = dir == 0 ? nav_->sx - (n->sx + n->sw) : dir == 1 ? n->sx - (nav_->sx + nav_->sw) : dir == 2 ? nav_->sy - (n->sy + n->sh) : n->sy - (nav_->sy + nav_->sh);
    if (along <= 1 || gap < -std::min(nav_->sw, nav_->sh) * 0.5f) continue;
    float overlap = dir <= 1 ? std::min(n->sy + n->sh, nav_->sy + nav_->sh) - std::max(n->sy, nav_->sy) : std::min(n->sx + n->sw, nav_->sx + nav_->sw) - std::max(n->sx, nav_->sx);
    float score = std::max(0.f, gap) + perp * (overlap > 0 ? 0.3f : 2.5f);
    if (score < bs) { bs = score; best = n; } }
  if (best) { nav_ = best; ensureVisible(nav_); }
}
void Ui::navActivate() {
  Node* n = nav_; if (!n) return;
  if (n->id.rfind("__osk", 0) == 0) { oskKey(n); return; }
  if (n->type == "input") { oskOpen(n); return; }
  if (n->type == "tabs") { setValue(n, (n->sel + 1) % std::max<size_t>(1, n->labels.size()), true); return; }
  if (n->type == "slider") return;
  Node* drag = nullptr; for (Node* q = n; q; q = q->parent) if (q->draggable && !q->disabled) { drag = q; break; }
  Node* drop = nullptr; for (Node* q = n; q; q = q->parent) if (q->droppable && !q->disabled) { drop = q; break; }
  if (padDrag_ && drag_) {   // putting it down
    json d = {{"from", drag_->id}, {"data", drag_->data}}; if (drop && drop != drag_) { d["to"] = drop->id; emit(drop, "drop", d); }
    emit(drag_, "dragend", {{"to", drop && drop != drag_ ? drop->id : ""}, {"data", drag_->data}}); drag_ = dropOver_ = nullptr; dragging_ = padDrag_ = false; return; }
  if (drag && n->type != "button" && !n->listen.count("click")) { drag_ = drag; dragging_ = padDrag_ = true; emit(drag, "dragstart", {{"data", drag->data}}); return; }
  emit(n, "click", {{"shift", false}, {"ctrl", false}, {"alt", false}, {"pad", true}});
}
bool Ui::pad(const std::string& b, bool down) {
  bool menu = osk_ || modal();
  if (!down) { if (b == "PadUp" || b == "PadDown" || b == "PadLeft" || b == "PadRight") { holdDir_ = -1; return menu; } return menu && (b == "PadA" || b == "PadB"); }
  if (!menu) return false;
  navOn_ = true;
  int dir = b == "PadLeft" ? 0 : b == "PadRight" ? 1 : b == "PadUp" ? 2 : b == "PadDown" ? 3 : -1;
  if (dir >= 0) { navMove(dir); holdDir_ = dir; holdT_ = 0.42f; return true; }
  if (b == "PadA") { auto C = navCandidates(); if (!nav_ || std::find(C.begin(), C.end(), nav_) == C.end()) navMove(3); else navActivate(); return true; }
  if (b == "PadY" && osk_) { oskClose(true); return true; }
  if (b == "PadX" && osk_) { for (Node* k : navCandidates()) if (k->id == "__osk_bs") { oskKey(k); break; } return true; }
  if (b == "PadB") {
    if (padDrag_ && drag_) { emit(drag_, "dragend", {{"to", ""}, {"data", drag_->data}}); drag_ = nullptr; dragging_ = padDrag_ = false; return true; }
    if (osk_) { oskClose(false); return true; }
    if (Node* r = scopeRoot()) emit(r, "back");
    return false;   // the game hears it too (as Escape), so its own "close the menu" works
  }
  if (b == "PadLB" || b == "PadRB") {
    Node* t = nullptr; for (Node* q = nav_; q; q = q->parent) if (q->type == "tabs") { t = q; break; }
    if (!t) for (Node* n : drawn_) if (n->type == "tabs" && shown(n)) { Node* r = scopeRoot(); bool in = !r; for (Node* q = n; q && !in; q = q->parent) in = q == r; if (in) { t = n; break; } }
    if (t && !t->labels.empty()) { int s = t->sel + ((b == "PadRB") != t->rtl() ? 1 : -1); s = (s + (int)t->labels.size()) % (int)t->labels.size(); setValue(t, s, true); nav_ = nullptr; }
    return true; }
  return false;
}
void Ui::padStick(float x, float y) {
  if (!(osk_ || modal())) { stickDir_ = -1; return; }
  int d = -1; if (std::max(std::abs(x), std::abs(y)) > 0.6f) d = std::abs(x) > std::abs(y) ? (x < 0 ? 0 : 1) : (y < 0 ? 2 : 3);
  if (d != stickDir_) { stickDir_ = d; if (d >= 0) { navOn_ = true; navMove(d); holdDir_ = d; holdT_ = 0.42f; } else holdDir_ = -1; }
}

/* ------------------------------------------------------------------ the on-screen keyboard (text fields with the controller) */
void Ui::oskOpen(Node* input) {
  if (osk_) oskClose(false);
  oskTarget_ = input; input->caret = input->value.size();
  create("window", "__osk", {{"title", input->placeholder.empty() ? (hasRtl(input->value) || input->rtl() ? std::string("\xd8\xa7\xd9\x83\xd8\xaa\xd8\xa8") : std::string("Type")) : input->placeholder},
    {"modal", true}, {"anchor", "bottom"}, {"y", -30}, {"w", 1180}, {"z", 1000}, {"closable", false}, {"dir", "ltr"}, {"anim", {{"anim", "slide"}, {"from", "bottom"}, {"time", 0.18}}}});
  osk_ = find("__osk"); osk_->at = 0; osk_->ak = 2; osk_->from = 3; osk_->dur = 0.18f;
  oskArabic_ = hasRtl(input->value) || input->rtl();
  oskBuild(); nav_ = nullptr; navOn_ = true; navMove(3);
  focus_ = input; input->blink = time_; SDL_StartTextInput();   // a real keyboard types too
}
void Ui::oskBuild() {
  clear("__osk");
  create("text", "__osk_text", {{"parent", "__osk"}, {"text", oskTarget_ ? (oskTarget_->password ? std::string(utf8Len(oskTarget_->value), '*') : oskTarget_->value) + "|" : "|"}, {"size", 30}, {"align", "center"}, {"w", "fill"}, {"bg", "#00000060"}, {"radius", 8}, {"pad", 10}});
  static const char* EN[4] = {"1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm_.@"};
  static const char* AR[4] = {"\xd9\xa1\xd9\xa2\xd9\xa3\xd9\xa4\xd9\xa5\xd9\xa6\xd9\xa7\xd9\xa8\xd9\xa9\xd9\xa0",
    "\xd8\xb6\xd8\xb5\xd8\xab\xd9\x82\xd9\x81\xd8\xba\xd8\xb9\xd9\x87\xd8\xae\xd8\xad\xd8\xac",
    "\xd8\xb4\xd8\xb3\xd9\x8a\xd8\xa8\xd9\x84\xd8\xa7\xd8\xaa\xd9\x86\xd9\x85\xd9\x83\xd8\xb7",
    "\xd8\xa6\xd8\xa1\xd8\xa4\xd8\xb1\xd9\x89\xd8\xa9\xd9\x88\xd8\xb2\xd8\xb8\xd8\xaf\xd8\xb0"};
  int k = 0;
  for (int r = 0; r < 4; r++) { std::string row = "__osk_r" + std::to_string(r); create("panel", row, {{"parent", "__osk"}, {"layout", "row"}, {"gap", 6}, {"justify", "center"}, {"w", "fill"}, {"dir", oskArabic_ ? "rtl" : "ltr"}});
    std::string s = oskArabic_ ? AR[r] : EN[r];
    for (size_t i = 0; i < s.size();) { size_t j = i + 1; while (j < s.size() && (s[j] & 0xC0) == 0x80) j++; std::string ch = s.substr(i, j - i); i = j;
      create("button", "__osk_k" + std::to_string(k++), {{"parent", row}, {"text", ch}, {"data", {{"k", ch}}}, {"w", 84}, {"h", 64}, {"size", 26}, {"pad", 0}}); } }
  create("panel", "__osk_r4", {{"parent", "__osk"}, {"layout", "row"}, {"gap", 6}, {"justify", "center"}, {"w", "fill"}});
  create("button", "__osk_lang", {{"parent", "__osk_r4"}, {"text", oskArabic_ ? "EN" : "\xd8\xb9\xd8\xb1\xd8\xa8\xd9\x8a"}, {"w", 140}, {"h", 64}, {"size", 24}});
  create("button", "__osk_space", {{"parent", "__osk_r4"}, {"text", " "}, {"data", {{"k", " "}}}, {"w", 420}, {"h", 64}});
  create("button", "__osk_bs", {{"parent", "__osk_r4"}, {"text", "\xe2\x8c\xab  X"}, {"w", 160}, {"h", 64}, {"size", 24}});
  create("button", "__osk_done", {{"parent", "__osk_r4"}, {"text", oskArabic_ ? "\xd8\xaa\xd9\x85  Y" : "Done  Y"}, {"w", 180}, {"h", 64}, {"size", 24}, {"bg", "#1f86ff"}, {"hover", {{"bg", "#4aa0ff"}}}});
}
void Ui::oskKey(Node* key) {
  if (!oskTarget_ || !nodes_.count(oskTarget_->id)) { oskClose(false); return; }
  Node& t = *oskTarget_; const std::string& id = key->id;
  if (id == "__osk_done") { oskClose(true); return; }
  if (id == "__osk_lang") { oskArabic_ = !oskArabic_; std::string keep = nav_ ? nav_->id : ""; oskBuild(); nav_ = find("__osk_lang"); return; }
  if (id == "__osk_bs") { if (!t.value.empty()) { t.value.erase(prevChar(t.value, t.value.size())); t.caret = t.value.size(); emit(&t, "input", {{"value", t.value}}); } }
  else if (key->data.is_object() && key->data.contains("k")) { std::string ch = key->data["k"]; if (utf8Len(t.value) + utf8Len(ch) <= (size_t)t.maxLen) { t.value += ch; t.caret = t.value.size(); emit(&t, "input", {{"value", t.value}}); } }
  if (Node* tx = find("__osk_text")) tx->text = (t.password ? std::string(utf8Len(t.value), '*') : t.value) + "|";
}
void Ui::oskClose(bool submit) {
  Node* t = oskTarget_ && nodes_.count(oskTarget_->id) ? oskTarget_ : nullptr;
  if (osk_) { std::string id = osk_->id; osk_ = nullptr; remove(id); }
  oskTarget_ = nullptr; nav_ = t; if (focus_ == t) { focus_ = nullptr; SDL_StopTextInput(); }
  if (t && submit) emit(t, "submit", {{"value", t->value}});
}
}
