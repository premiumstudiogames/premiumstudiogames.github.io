#include "assets.h"
#include "gl.h"
#include "log.h"
#include "project.h"
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <stb_image.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <filesystem>
#include <fstream>
#include <map>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace pge {
namespace fs = std::filesystem;

static std::string g_root;
static std::map<std::string, const struct Texture*> g_texFront;   // root|file|srgb → the texture: no disk look-ups while drawing
void setAssetRoot(const std::string& d) { if (d != g_root) g_texFront.clear(); g_root = d; }
const std::string& assetRoot() { return g_root; }
static std::string lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }
static bool endsWith(const std::string& s, const char* e) { size_t n = std::strlen(e); return s.size() >= n && lower(s.substr(s.size() - n)) == e; }
bool isModelFile(const std::string& m) { return endsWith(m, ".glb") || endsWith(m, ".gltf"); }
bool isImageFile(const std::string& f) { for (const char* e : {".png", ".jpg", ".jpeg", ".tga", ".bmp"}) if (endsWith(f, e)) return true; return false; }
std::string resolveAsset(const std::string& rel) {
  if (rel.empty()) return ""; std::error_code ec; fs::path f = fs::u8path(rel);
  if (f.is_absolute()) return fs::exists(f, ec) ? rel : "";
  std::vector<fs::path> roots; if (!g_root.empty()) roots.push_back(fs::u8path(g_root)); roots.push_back(fs::u8path(exeDir()));
  for (auto& r : roots) { if (fs::exists(r / f, ec)) return (r / f).u8string(); if (fs::exists(r / "assets" / f, ec)) return (r / "assets" / f).u8string(); }
  return "";
}
static bool readFile(const fs::path& p, std::vector<unsigned char>& out) {
  std::ifstream f(p, std::ios::binary); if (!f) return false; f.seekg(0, std::ios::end); auto n = f.tellg(); if (n < 0) return false; f.seekg(0);
  out.resize((size_t)n); if (n > 0) f.read((char*)out.data(), n); return (bool)f;
}
static double mtimeOf(const std::string& file) { std::error_code ec; auto t = fs::last_write_time(fs::u8path(file), ec); return ec ? 0 : (double)t.time_since_epoch().count(); }

/* ------------------------------------------------------------------ textures */
struct TexEntry { std::unique_ptr<Texture> t; double mtime = 0; std::string file; };
static const Texture* textureLoad(const std::string& file, bool srgb);
static std::map<std::string, TexEntry> g_tex;   // key: resolved file (+ "|s" for sRGB) or "model#image"
static bool glReady() { return glGenTextures != nullptr && glGetString != nullptr; }
static void uploadTexture(Texture& t, const unsigned char* px, int w, int h, bool srgb) {
  t.w = w; t.h = h; t.ok = true; if (!glReady()) return;
  glGenTextures(1, &t.id); glBindTexture(GL_TEXTURE_2D, t.id); glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  static float aniso = -1; if (aniso < 0) { aniso = 0; float mx = 0; glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &mx); while (glGetError() != GL_NO_ERROR) {} aniso = std::min(mx, 8.f); }
  if (aniso > 1) { glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, aniso); while (glGetError() != GL_NO_ERROR) {} }
  glBindTexture(GL_TEXTURE_2D, 0);
}
static void decodeTexture(Texture& t, const unsigned char* data, size_t size, bool srgb) {
  int w = 0, h = 0, n = 0; unsigned char* px = stbi_load_from_memory(data, (int)size, &w, &h, &n, 4);
  if (!px) { t.error = std::string("can't read the picture: ") + (stbi_failure_reason() ? stbi_failure_reason() : "unknown format"); return; }
  t.alpha = n == 2 || n == 4; if (t.alpha) { bool any = false; for (int i = 0; i < w * h && !any; i++) any = px[i * 4 + 3] < 250; t.alpha = any; }
  uploadTexture(t, px, w, h, srgb); stbi_image_free(px);
}
static void freeTex(Texture& t) { if (t.id && glReady()) glDeleteTextures(1, &t.id); t.id = 0; }
const Texture* texture(const std::string& file, bool srgb) {
  if (file.empty()) return nullptr;
  std::string front = g_root + "|" + file + (srgb ? "|s" : "|l"); auto fi = g_texFront.find(front); if (fi != g_texFront.end()) return fi->second;
  const Texture* r = textureLoad(file, srgb); g_texFront[front] = r; return r;
}
const Texture* pixelTexture(const std::string& name, int w, int h, const std::vector<unsigned char>& rgba, bool srgb) {
  std::string key = "@" + name + (srgb ? "|s" : "|l"); auto it = g_tex.find(key); if (it != g_tex.end()) return it->second.t.get();
  if (w <= 0 || h <= 0 || rgba.size() < (size_t)w * h * 4) return nullptr;
  TexEntry e; e.t.reset(new Texture()); e.t->alpha = true; uploadTexture(*e.t, rgba.data(), w, h, srgb); const Texture* r = e.t.get(); g_tex[key] = std::move(e); return r;
}
static const Texture* textureLoad(const std::string& file, bool srgb) {
  std::string path = resolveAsset(file), key = (path.empty() ? "?" + file : path) + (srgb ? "|s" : "|l");
  auto it = g_tex.find(key); if (it != g_tex.end()) return it->second.t.get();
  TexEntry e; e.t.reset(new Texture()); e.file = path;
  if (path.empty()) e.t->error = "file not found: " + file;
  else { std::vector<unsigned char> b; if (!readFile(fs::u8path(path), b)) e.t->error = "can't open " + file; else decodeTexture(*e.t, b.data(), b.size(), srgb); e.mtime = mtimeOf(path); }
  if (!e.t->error.empty()) logMsg("warning: texture " + file + ": " + e.t->error, LogKind::Warn);
  const Texture* r = e.t.get(); g_tex[key] = std::move(e); return r;
}

/* ------------------------------------------------------------------ models */
Model::~Model() { for (auto& p : prims) if (glReady() && p.gpu.vao) destroy(p.gpu); }
int Model::anim(const std::string& n) const { for (size_t i = 0; i < anims.size(); i++) if (anims[i].name == n) return (int)i; return -1; }

struct MVert { glm::vec3 p, n; glm::vec2 uv; uint8_t col[4]; uint16_t j[4]; float w[4]; };
static_assert(sizeof(MVert) == 60, "model vertex");
static GpuMesh uploadModel(const std::vector<MVert>& v, const std::vector<unsigned>& idx, glm::vec3 mn, glm::vec3 mx) {
  GpuMesh g; g.bmin = mn; g.bmax = mx; g.count = (int)idx.size(); if (!glReady() || v.empty()) return g;
  glGenVertexArrays(1, &g.vao); glBindVertexArray(g.vao);
  glGenBuffers(1, &g.vbo); glBindBuffer(GL_ARRAY_BUFFER, g.vbo); glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(MVert), v.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MVert), (void*)0);
  glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(MVert), (void*)12);
  glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(MVert), (void*)24);
  glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(MVert), (void*)32);
  glEnableVertexAttribArray(4); glVertexAttribIPointer(4, 4, GL_UNSIGNED_SHORT, sizeof(MVert), (void*)36);
  glEnableVertexAttribArray(5); glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(MVert), (void*)44);
  glGenBuffers(1, &g.ebo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ebo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * 4, idx.data(), GL_STATIC_DRAW);
  glBindVertexArray(0); return g;
}

// glTF files: external .bin / pictures are read through std::filesystem, so folders with Arabic names work on Windows
static cgltf_result fileRead(const cgltf_memory_options*, const cgltf_file_options*, const char* path, cgltf_size* size, void** data) {
  std::vector<unsigned char> b; if (!readFile(fs::u8path(path), b)) return cgltf_result_file_not_found;
  void* p = std::malloc(std::max<size_t>(b.size(), 1)); if (!p) return cgltf_result_out_of_memory; if (!b.empty()) std::memcpy(p, b.data(), b.size());
  *size = b.size(); *data = p; return cgltf_result_success;
}
static void fileRelease(const cgltf_memory_options*, const cgltf_file_options*, void* data) { std::free(data); }
static const char* resultText(cgltf_result r) {
  switch (r) { case cgltf_result_data_too_short: return "the file is cut short"; case cgltf_result_unknown_format: return "not a glTF file"; case cgltf_result_invalid_json: return "broken JSON";
    case cgltf_result_invalid_gltf: return "not valid glTF 2.0"; case cgltf_result_file_not_found: return "a file it uses is missing (.bin or a picture)"; case cgltf_result_io_error: return "can't read the file";
    case cgltf_result_out_of_memory: return "out of memory"; case cgltf_result_legacy_gltf: return "glTF 1.0 (export it as glTF 2.0)"; default: return "can't read it"; }
}

static std::shared_ptr<Model> loadModel(const std::string& rel, const std::string& path) {
  auto M = std::make_shared<Model>(); M->file = rel;
  if (path.empty()) { M->error = "file not found: " + rel; return M; }
  std::vector<unsigned char> bytes; if (!readFile(fs::u8path(path), bytes)) { M->error = "can't open " + rel; return M; }
  cgltf_options opt{}; opt.file.read = fileRead; opt.file.release = fileRelease; cgltf_data* D = nullptr;
  cgltf_result r = cgltf_parse(&opt, bytes.data(), bytes.size(), &D);
  if (r == cgltf_result_success) r = cgltf_load_buffers(&opt, D, path.c_str());
  if (r == cgltf_result_success) r = cgltf_validate(D);
  if (r != cgltf_result_success) { M->error = resultText(r); if (D) cgltf_free(D); return M; }
  for (cgltf_size i = 0; i < D->extensions_required_count; i++) { std::string e = D->extensions_required[i];
    if (e == "KHR_draco_mesh_compression" || e == "EXT_meshopt_compression" || e == "KHR_mesh_quantization" || e == "KHR_texture_basisu" || e == "EXT_texture_webp") {
      M->error = "it uses " + e + " - export it again without compression (glTF / GLB with PNG or JPG pictures)"; cgltf_free(D); return M; } }
  fs::path dir = fs::u8path(path).parent_path();
  // pictures: inside the .glb, a data: URI, or a file next to the .gltf
  std::vector<const Texture*> imgS(D->images_count * 2, nullptr);
  auto image = [&](const cgltf_texture_view& tv, bool srgb) -> const Texture* {
    if (!tv.texture || !tv.texture->image) return nullptr; const cgltf_image* im = tv.texture->image; size_t ii = cgltf_image_index(D, im);
    const Texture*& slot = imgS[ii * 2 + (srgb ? 1 : 0)]; if (slot) return slot;
    std::string key = path + "#" + std::to_string(ii) + (srgb ? "|s" : "|l");
    auto it = g_tex.find(key); if (it != g_tex.end()) return slot = it->second.t.get();
    TexEntry e; e.t.reset(new Texture()); e.file = path;
    if (im->buffer_view) decodeTexture(*e.t, cgltf_buffer_view_data(im->buffer_view), im->buffer_view->size, srgb);
    else if (im->uri && std::strncmp(im->uri, "data:", 5) == 0) { const char* comma = std::strchr(im->uri, ','); void* out = nullptr;
      if (comma && std::strstr(im->uri, ";base64,")) { size_t n = std::strlen(comma + 1); size_t sz = n / 4 * 3; if (n > 1 && comma[n] == '=') sz--; if (n > 2 && comma[n - 1] == '=') sz--;
        if (cgltf_load_buffer_base64(&opt, sz, comma + 1, &out) == cgltf_result_success) { decodeTexture(*e.t, (unsigned char*)out, sz, srgb); std::free(out); } else e.t->error = "broken picture data"; }
      else e.t->error = "picture data it can't read"; }
    else if (im->uri) { std::string u = im->uri; std::vector<char> c(u.begin(), u.end()); c.push_back(0); cgltf_decode_uri(c.data());
      std::vector<unsigned char> b; fs::path f = dir / fs::u8path(c.data()); if (readFile(f, b)) decodeTexture(*e.t, b.data(), b.size(), srgb); else e.t->error = std::string("missing picture ") + c.data(); }
    if (!e.t->error.empty()) logMsg("warning: " + rel + ": " + e.t->error, LogKind::Warn);
    slot = e.t.get(); g_tex[key] = std::move(e); return slot; };
  for (cgltf_size i = 0; i < D->materials_count; i++) { const cgltf_material& m = D->materials[i]; ModelMaterial mm; mm.name = m.name ? m.name : "Material" + std::to_string(i);
    if (m.has_pbr_metallic_roughness) { const auto& pb = m.pbr_metallic_roughness; mm.base = glm::make_vec4(pb.base_color_factor); mm.metal = pb.metallic_factor; mm.rough = pb.roughness_factor;
      mm.baseTex = image(pb.base_color_texture, true); mm.mrTex = image(pb.metallic_roughness_texture, false); }
    else if (m.has_pbr_specular_glossiness) { const auto& sg = m.pbr_specular_glossiness; mm.base = glm::make_vec4(sg.diffuse_factor); mm.metal = 0; mm.rough = 1 - sg.glossiness_factor; mm.baseTex = image(sg.diffuse_texture, true); }
    mm.normalTex = image(m.normal_texture, false); mm.normalScale = m.normal_texture.texture ? m.normal_texture.scale : 1;
    mm.occTex = image(m.occlusion_texture, false); mm.occlusion = m.occlusion_texture.texture ? m.occlusion_texture.scale : 1;
    mm.emissive = glm::make_vec3(m.emissive_factor) * (m.has_emissive_strength ? m.emissive_strength.emissive_strength : 1.f); mm.emissiveTex = image(m.emissive_texture, true);
    if (m.unlit) { mm.emissive = glm::vec3(mm.base); mm.metal = 0; mm.rough = 1; }
    mm.alphaMode = m.alpha_mode == cgltf_alpha_mode_mask ? 1 : m.alpha_mode == cgltf_alpha_mode_blend ? 2 : 0; mm.cutoff = m.alpha_cutoff; mm.doubleSided = m.double_sided;
    M->mats.push_back(mm); }
  // nodes
  M->nodes.resize(D->nodes_count);
  for (cgltf_size i = 0; i < D->nodes_count; i++) { const cgltf_node& n = D->nodes[i]; ModelNode& o = M->nodes[i]; o.name = n.name ? n.name : "Node" + std::to_string(i);
    o.parent = n.parent ? (int)cgltf_node_index(D, n.parent) : -1; for (cgltf_size c = 0; c < n.children_count; c++) o.children.push_back((int)cgltf_node_index(D, n.children[c]));
    if (n.has_matrix) { glm::mat4 m = glm::make_mat4(n.matrix); o.t = glm::vec3(m[3]); o.s = {glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2]))};
      glm::mat3 R(glm::vec3(m[0]) / std::max(o.s.x, 1e-8f), glm::vec3(m[1]) / std::max(o.s.y, 1e-8f), glm::vec3(m[2]) / std::max(o.s.z, 1e-8f)); if (glm::determinant(R) < 0) { o.s.x = -o.s.x; R[0] = -R[0]; } o.r = glm::normalize(glm::quat_cast(R)); }
    else { if (n.has_translation) o.t = glm::make_vec3(n.translation); if (n.has_rotation) o.r = glm::normalize(glm::quat(n.rotation[3], n.rotation[0], n.rotation[1], n.rotation[2])); if (n.has_scale) o.s = glm::make_vec3(n.scale); }
    o.mesh = n.mesh ? (int)cgltf_mesh_index(D, n.mesh) : -1; o.skin = n.skin ? (int)cgltf_skin_index(D, n.skin) : -1; }
  const cgltf_scene* sc = D->scene ? D->scene : D->scenes_count ? &D->scenes[0] : nullptr;
  if (sc) for (cgltf_size i = 0; i < sc->nodes_count; i++) M->roots.push_back((int)cgltf_node_index(D, sc->nodes[i]));
  else for (size_t i = 0; i < M->nodes.size(); i++) if (M->nodes[i].parent < 0) M->roots.push_back((int)i);
  // skins
  for (cgltf_size i = 0; i < D->skins_count; i++) { const cgltf_skin& s = D->skins[i]; ModelSkin k;
    for (cgltf_size j = 0; j < s.joints_count; j++) { k.joints.push_back((int)cgltf_node_index(D, s.joints[j])); glm::mat4 ib(1);
      if (s.inverse_bind_matrices) cgltf_accessor_read_float(s.inverse_bind_matrices, j, glm::value_ptr(ib), 16); k.inverseBind.push_back(ib); }
    M->joints += (int)k.joints.size(); M->skins.push_back(k); }
  // animations
  for (cgltf_size i = 0; i < D->animations_count; i++) { const cgltf_animation& a = D->animations[i]; ModelAnim an; an.name = a.name && *a.name ? a.name : "Animation" + std::to_string(i);
    for (cgltf_size c = 0; c < a.channels_count; c++) { const cgltf_animation_channel& ch = a.channels[c]; if (!ch.target_node || !ch.sampler) continue;
      int path = ch.target_path == cgltf_animation_path_type_translation ? 0 : ch.target_path == cgltf_animation_path_type_rotation ? 1 : ch.target_path == cgltf_animation_path_type_scale ? 2 : -1; if (path < 0) continue;
      AnimChannel o; o.node = (int)cgltf_node_index(D, ch.target_node); o.path = path; o.interp = ch.sampler->interpolation == cgltf_interpolation_type_step ? 0 : ch.sampler->interpolation == cgltf_interpolation_type_cubic_spline ? 2 : 1;
      const cgltf_accessor* in = ch.sampler->input; const cgltf_accessor* out = ch.sampler->output; o.times.resize(in->count);
      for (cgltf_size k = 0; k < in->count; k++) cgltf_accessor_read_float(in, k, &o.times[k], 1);
      int comps = path == 1 ? 4 : 3; o.values.resize(out->count, glm::vec4(0, 0, 0, 1));
      for (cgltf_size k = 0; k < out->count; k++) { float v[4] = {0, 0, 0, 1}; cgltf_accessor_read_float(out, k, v, comps); o.values[k] = glm::make_vec4(v); }
      size_t need = o.times.size() * (o.interp == 2 ? 3 : 1); if (o.times.empty() || o.values.size() < need) continue;
      an.duration = std::max(an.duration, o.times.back()); an.ch.push_back(std::move(o)); }
    M->anims.push_back(std::move(an)); }
  // meshes: one draw per primitive (triangles), vertices with their joints
  M->meshPrims0.resize(D->meshes_count); M->meshPrimsN.resize(D->meshes_count);
  struct Cpu { std::vector<MVert> v; std::vector<unsigned> i; };
  std::vector<Cpu> cpu;
  for (cgltf_size mi = 0; mi < D->meshes_count; mi++) { const cgltf_mesh& me = D->meshes[mi]; M->meshPrims0[mi] = (int)M->prims.size();
    for (cgltf_size pi = 0; pi < me.primitives_count; pi++) { const cgltf_primitive& pr = me.primitives[pi];
      if (pr.type != cgltf_primitive_type_triangles && pr.type != cgltf_primitive_type_triangle_strip && pr.type != cgltf_primitive_type_triangle_fan) continue;
      const cgltf_accessor *P = nullptr, *N = nullptr, *T = nullptr, *C = nullptr, *J = nullptr, *W = nullptr;
      for (cgltf_size k = 0; k < pr.attributes_count; k++) { const cgltf_attribute& at = pr.attributes[k];
        if (at.type == cgltf_attribute_type_position) P = at.data; else if (at.type == cgltf_attribute_type_normal) N = at.data;
        else if (at.type == cgltf_attribute_type_texcoord && at.index == 0) T = at.data; else if (at.type == cgltf_attribute_type_color && at.index == 0) C = at.data;
        else if (at.type == cgltf_attribute_type_joints && at.index == 0) J = at.data; else if (at.type == cgltf_attribute_type_weights && at.index == 0) W = at.data; }
      if (!P || P->count == 0) continue;
      Cpu c; c.v.resize(P->count); glm::vec3 mn(1e30f), mx(-1e30f);
      for (cgltf_size k = 0; k < P->count; k++) { MVert& v = c.v[k]; v = MVert{}; cgltf_accessor_read_float(P, k, &v.p.x, 3); mn = glm::min(mn, v.p); mx = glm::max(mx, v.p);
        if (N) cgltf_accessor_read_float(N, k, &v.n.x, 3); if (T) cgltf_accessor_read_float(T, k, &v.uv.x, 2);
        float col[4] = {1, 1, 1, 1}; if (C) cgltf_accessor_read_float(C, k, col, cgltf_num_components(C->type)); for (int q = 0; q < 4; q++) v.col[q] = (uint8_t)std::lround(std::clamp(col[q], 0.f, 1.f) * 255);
        if (J && W) { cgltf_uint jj[4] = {0, 0, 0, 0}; cgltf_accessor_read_uint(J, k, jj, 4); float ww[4] = {0, 0, 0, 0}; cgltf_accessor_read_float(W, k, ww, 4);
          float s = ww[0] + ww[1] + ww[2] + ww[3]; for (int q = 0; q < 4; q++) { v.j[q] = (uint16_t)jj[q]; v.w[q] = s > 1e-6f ? ww[q] / s : (q == 0 ? 1.f : 0.f); } }
        else v.w[0] = 1; }
      std::vector<unsigned> raw; if (pr.indices) { raw.resize(pr.indices->count); for (cgltf_size k = 0; k < pr.indices->count; k++) raw[k] = (unsigned)cgltf_accessor_read_index(pr.indices, k); }
      else { raw.resize(P->count); for (cgltf_size k = 0; k < P->count; k++) raw[k] = (unsigned)k; }
      if (pr.type == cgltf_primitive_type_triangles) c.i = raw;
      else for (size_t k = 2; k < raw.size(); k++) { if (pr.type == cgltf_primitive_type_triangle_fan) c.i.insert(c.i.end(), {raw[0], raw[k - 1], raw[k]}); else if (k % 2) c.i.insert(c.i.end(), {raw[k - 1], raw[k - 2], raw[k]}); else c.i.insert(c.i.end(), {raw[k - 2], raw[k - 1], raw[k]}); }
      c.i.resize(c.i.size() / 3 * 3); for (unsigned& x : c.i) if (x >= c.v.size()) x = 0;
      if (!N) {   // no normals: flat ones from the triangles (each corner its own vertex)
        Cpu f; for (size_t k = 0; k + 2 < c.i.size(); k += 3) { MVert a = c.v[c.i[k]], b = c.v[c.i[k + 1]], d = c.v[c.i[k + 2]]; glm::vec3 n = glm::cross(b.p - a.p, d.p - a.p); n = glm::length(n) > 1e-12f ? glm::normalize(n) : glm::vec3(0, 1, 0);
          for (MVert q : {a, b, d}) { q.n = n; f.i.push_back((unsigned)f.v.size()); f.v.push_back(q); } } c = std::move(f); }
      else for (auto& v : c.v) v.n = glm::length(v.n) > 1e-12f ? glm::normalize(v.n) : glm::vec3(0, 1, 0);
      ModelPrim mp; mp.material = pr.material ? (int)cgltf_material_index(D, pr.material) : -1; mp.colors = C != nullptr;
      mp.gpu = uploadModel(c.v, c.i, mn, mx); M->triangles += (int)c.i.size() / 3; M->vertices += (int)c.v.size();
      M->prims.push_back(mp); cpu.push_back(std::move(c)); }
    M->meshPrimsN[mi] = (int)M->prims.size() - M->meshPrims0[mi]; }
  cgltf_free(D);
  // the model at rest: where each node is, every triangle where it is drawn (skinned meshes by their joints)
  ModelPose pose; poseModel(*M, nullptr, pose); M->rest = pose.world;
  MeshData& S = M->shape; S.bmin = glm::vec3(1e30f); S.bmax = glm::vec3(-1e30f);
  std::vector<int> primNode(M->prims.size(), -1), primSkin(M->prims.size(), -1);
  for (size_t ni = 0; ni < M->nodes.size(); ni++) { const ModelNode& n = M->nodes[ni]; if (n.mesh < 0) continue;
    for (int k = 0; k < M->meshPrimsN[n.mesh]; k++) { size_t pi = M->meshPrims0[n.mesh] + k; if (primNode[pi] < 0) { primNode[pi] = (int)ni; primSkin[pi] = n.skin; } } }
  for (size_t pi = 0; pi < M->prims.size(); pi++) { M->prims[pi].node = primNode[pi]; M->prims[pi].skin = primSkin[pi]; if (primNode[pi] < 0) continue;
    const Cpu& c = cpu[pi]; glm::mat4 W = M->rest[primNode[pi]]; int sk = primSkin[pi]; unsigned base = (unsigned)S.v.size();
    for (auto& v : c.v) { glm::vec4 p(v.p, 1), q(0);
      if (sk >= 0 && sk < (int)M->skins.size()) { const ModelSkin& s = M->skins[sk]; int at = pose.skinAt[sk];
        for (int k = 0; k < 4; k++) if (v.w[k] > 0 && v.j[k] < s.joints.size()) q += pose.joints[at + v.j[k]] * p * v.w[k]; }
      else q = W * p;
      glm::vec3 w(q); if (!std::isfinite(w.x + w.y + w.z)) w = glm::vec3(0); S.v.push_back({w, v.n}); S.bmin = glm::min(S.bmin, w); S.bmax = glm::max(S.bmax, w); }
    for (unsigned i : c.i) S.i.push_back(base + i); }
  if (S.v.empty()) { S.bmin = glm::vec3(-0.5f); S.bmax = glm::vec3(0.5f); if (M->prims.empty()) M->error = "it has no meshes (triangles)"; }
  if (!M->ok()) logMsg("warning: model " + rel + ": " + M->error, LogKind::Warn);
  else logMsg("Model " + rel + ": " + std::to_string(M->triangles) + " triangles, " + std::to_string(M->mats.size()) + " material(s), " + std::to_string(M->joints) + " joint(s), " + std::to_string(M->anims.size()) + " animation(s)");
  return M;
}

struct ModelEntry { std::shared_ptr<Model> m; std::string path; double mtime = 0; };
static std::map<std::string, ModelEntry> g_models;   // key: the name as written (Models/Robot.glb) + the project folder
ModelRef model(const std::string& file) {
  if (file.empty()) return nullptr;
  std::string key = g_root + "|" + file; auto it = g_models.find(key); if (it != g_models.end()) return it->second.m;
  ModelEntry e; e.path = resolveAsset(file); e.m = loadModel(file, e.path); e.mtime = e.path.empty() ? 0 : mtimeOf(e.path);
  g_models[key] = e; return e.m;
}
bool reloadChangedAssets() {
  bool any = false; g_texFront.clear();
  for (auto& [k, e] : g_models) { std::string p = resolveAsset(e.m->file); double t = p.empty() ? 0 : mtimeOf(p);
    if (p != e.path || t != e.mtime) { // a picture of the model may be the one cached: drop them so they load again
      for (auto it = g_tex.begin(); it != g_tex.end();) { if (!e.path.empty() && it->first.rfind(e.path + "#", 0) == 0) { freeTex(*it->second.t); it = g_tex.erase(it); } else ++it; }
      e.path = p; e.mtime = t; e.m = loadModel(e.m->file, p); any = true; } }
  for (auto it = g_tex.begin(); it != g_tex.end();) { const std::string& key = it->first;
    if (key.find('#') == std::string::npos && !it->second.file.empty() && mtimeOf(it->second.file) != it->second.mtime) { freeTex(*it->second.t); it = g_tex.erase(it); any = true; } else ++it; }
  for (auto it = g_tex.begin(); it != g_tex.end();) { if (it->first[0] == '?' && !resolveAsset(it->first.substr(1, it->first.size() - 3)).empty()) { it = g_tex.erase(it); any = true; } else ++it; }   // a missing picture that is there now
  return any;
}
static std::map<std::string, ModelRef> g_shapeKeep;   // meshShape() hands out references into these
void releaseAssets() { g_texFront.clear(); g_shapeKeep.clear(); g_models.clear(); for (auto& [k, e] : g_tex) freeTex(*e.t); g_tex.clear(); }

/* ------------------------------------------------------------------ animation */
void AnimPlay::play(int a, bool loopIt, float spd, float blendSeconds) {
  if (a == anim && playing && loopIt == loop) { speed = spd; return; }   // already playing it
  if (blendSeconds > 0 && anim >= 0 && playing) { from = anim; fromTime = time; fromLoop = loop; blendTime = blendSeconds; blend = 0; } else { from = -1; blend = 1; }
  anim = a; time = spd < 0 ? 1e30f : 0; loop = loopIt; speed = spd; playing = a >= 0;
}
bool AnimPlay::advance(const Model& m, float dt) {
  bool ended = false;
  if (anim >= (int)m.anims.size()) { anim = -1; playing = false; } if (from >= (int)m.anims.size()) from = -1;
  if (anim >= 0 && playing) { float d = m.anims[anim].duration; time += dt * speed;
    if (d <= 0) time = 0;
    else if (loop) { time = std::fmod(time, d); if (time < 0) time += d; }
    else if (time >= d || time <= 0) { time = std::clamp(time, 0.f, d); playing = false; ended = true; } }   // stays on its last frame
  if (from >= 0) { float d = m.anims[from].duration; fromTime += dt * speed; if (d > 0) fromTime = fromLoop ? std::fmod(fromTime, d) : std::min(fromTime, d);
    blend += blendTime > 0 ? dt / blendTime : 1; if (blend >= 1) { from = -1; blend = 1; } }
  return ended;
}
static glm::vec4 sample(const AnimChannel& c, float t) {
  const auto& T = c.times; size_t n = T.size();
  auto val = [&](size_t k) { return c.interp == 2 ? c.values[k * 3 + 1] : c.values[k]; };
  if (t <= T[0]) return val(0); if (t >= T[n - 1]) return val(n - 1);
  size_t k = std::upper_bound(T.begin(), T.end(), t) - T.begin() - 1; float dt = T[k + 1] - T[k], u = dt > 0 ? (t - T[k]) / dt : 0;
  if (c.interp == 0) return val(k);
  if (c.interp == 2) { glm::vec4 p0 = c.values[k * 3 + 1], m0 = c.values[k * 3 + 2] * dt, p1 = c.values[(k + 1) * 3 + 1], m1 = c.values[(k + 1) * 3] * dt; float u2 = u * u, u3 = u2 * u;
    glm::vec4 r = (2 * u3 - 3 * u2 + 1) * p0 + (u3 - 2 * u2 + u) * m0 + (-2 * u3 + 3 * u2) * p1 + (u3 - u2) * m1; if (c.path == 1) r = glm::normalize(r); return r; }
  if (c.path == 1) { glm::quat a(val(k).w, val(k).x, val(k).y, val(k).z), b(val(k + 1).w, val(k + 1).x, val(k + 1).y, val(k + 1).z); glm::quat q = glm::slerp(a, b, u); return {q.x, q.y, q.z, q.w}; }
  return glm::mix(val(k), val(k + 1), u);
}
struct Trs { glm::vec3 t, s; glm::quat r; };
static void applyAnim(const ModelAnim& a, float t, std::vector<Trs>& L) {
  for (auto& c : a.ch) { if (c.node < 0 || c.node >= (int)L.size()) continue; glm::vec4 v = sample(c, t);
    if (c.path == 0) L[c.node].t = glm::vec3(v); else if (c.path == 1) L[c.node].r = glm::normalize(glm::quat(v.w, v.x, v.y, v.z)); else L[c.node].s = glm::vec3(v); }
}
void poseModel(const Model& m, const AnimPlay* a, ModelPose& out) {
  size_t n = m.nodes.size(); std::vector<Trs> L(n); for (size_t i = 0; i < n; i++) L[i] = {m.nodes[i].t, m.nodes[i].s, m.nodes[i].r};
  if (a && a->anim >= 0 && a->anim < (int)m.anims.size()) {
    if (a->from >= 0 && a->from < (int)m.anims.size() && a->blend < 1) { std::vector<Trs> F = L; applyAnim(m.anims[a->from], a->fromTime, F); applyAnim(m.anims[a->anim], a->time, L);
      float w = glm::clamp(a->blend, 0.f, 1.f); w = w * w * (3 - 2 * w);
      for (size_t i = 0; i < n; i++) { L[i].t = glm::mix(F[i].t, L[i].t, w); L[i].s = glm::mix(F[i].s, L[i].s, w); L[i].r = glm::slerp(F[i].r, L[i].r, w); } }
    else applyAnim(m.anims[a->anim], a->time, L); }
  out.world.assign(n, glm::mat4(1)); std::vector<char> done(n, 0);
  std::vector<const glm::mat4*> held; if (a && !a->fixed.empty()) { held.assign(n, nullptr); for (auto& f : a->fixed) if (f.first >= 0 && f.first < (int)n) held[f.first] = &f.second; }
  // parents before children (any order in the file)
  std::vector<int> stack; for (int r : m.roots) stack.push_back(r); for (size_t i = 0; i < n; i++) if (m.nodes[i].parent < 0) stack.push_back((int)i);
  while (!stack.empty()) { int i = stack.back(); stack.pop_back(); if (i < 0 || i >= (int)n || done[i]) continue; done[i] = 1;
    glm::mat4 local = glm::translate(glm::mat4(1), L[i].t) * glm::mat4_cast(L[i].r) * glm::scale(glm::mat4(1), L[i].s);
    int p = m.nodes[i].parent; out.world[i] = !held.empty() && held[i] ? *held[i] : p >= 0 && done[p] ? out.world[p] * local : local; for (int c : m.nodes[i].children) stack.push_back(c); }
  out.joints.clear(); out.skinAt.clear();
  for (auto& s : m.skins) { out.skinAt.push_back((int)out.joints.size()); for (size_t j = 0; j < s.joints.size(); j++) out.joints.push_back((s.joints[j] >= 0 && s.joints[j] < (int)n ? out.world[s.joints[j]] : glm::mat4(1)) * s.inverseBind[j]); }
}

/* ------------------------------------------------------------------ shapes of meshes */
const MeshData& meshShape(const std::string& mesh) {
  static std::map<std::string, MeshData> prims; static MeshData empty;
  if (isModelFile(mesh)) { ModelRef m = model(mesh); g_shapeKeep[g_root + "|" + mesh] = m; return m ? m->shape : empty; }
  auto it = prims.find(mesh); if (it != prims.end()) return it->second;
  return prims[mesh] = makePrimitive(mesh);
}
}
