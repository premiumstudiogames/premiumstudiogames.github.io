#pragma once
// A small rigged model written as a .glb, for the engine's tests and the editor's self-test: two boxes on top of each other
// (Root: y 0..1, Arm: y 1..2), skinned to two joints, a 4 x 4 red picture inside the file, animations "Wave" (the arm turns
// 90° about Z in 1 s) and "Bob" (the root goes up 0.5 m and back in 1 s).
#include <string>
#include <vector>
#include <cstring>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace pge {
inline bool writeTestModel(const std::string& file) {
  using json = nlohmann::json; std::vector<unsigned char> bin;
  auto align = [&] { while (bin.size() % 4) bin.push_back(0); };
  auto put = [&](const void* p, size_t n) { size_t at = bin.size(); bin.insert(bin.end(), (const unsigned char*)p, (const unsigned char*)p + n); return at; };
  json views = json::array(), acc = json::array();
  auto view = [&](size_t off, size_t len, int target) { json v = {{"buffer", 0}, {"byteOffset", off}, {"byteLength", len}}; if (target) v["target"] = target; views.push_back(v); return (int)views.size() - 1; };
  auto accessor = [&](int v, int comp, int count, const char* type, json mn = nullptr, json mx = nullptr) { json a = {{"bufferView", v}, {"componentType", comp}, {"count", count}, {"type", type}}; if (!mn.is_null()) { a["min"] = mn; a["max"] = mx; } acc.push_back(a); return (int)acc.size() - 1; };
  // two boxes, 24 vertices each (flat faces)
  std::vector<float> pos, nor, uv, wts; std::vector<uint16_t> jnt, idx;
  auto box = [&](float y0, float y1, int joint) {
    const float h = 0.2f; float c[8][3] = {{-h, y0, -h}, {h, y0, -h}, {h, y1, -h}, {-h, y1, -h}, {-h, y0, h}, {h, y0, h}, {h, y1, h}, {-h, y1, h}};
    int f[6][4] = {{4, 5, 6, 7}, {1, 0, 3, 2}, {5, 1, 2, 6}, {0, 4, 7, 3}, {7, 6, 2, 3}, {0, 1, 5, 4}}; float n[6][3] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    for (int i = 0; i < 6; i++) { uint16_t b = (uint16_t)(pos.size() / 3); float U[4] = {0, 1, 1, 0}, V[4] = {1, 1, 0, 0};
      for (int k = 0; k < 4; k++) { for (int q = 0; q < 3; q++) { pos.push_back(c[f[i][k]][q]); nor.push_back(n[i][q]); } uv.push_back(U[k]); uv.push_back(V[k]);
        jnt.insert(jnt.end(), {(uint16_t)joint, 0, 0, 0}); wts.insert(wts.end(), {1.f, 0.f, 0.f, 0.f}); }
      idx.insert(idx.end(), {b, (uint16_t)(b + 1), (uint16_t)(b + 2), b, (uint16_t)(b + 2), (uint16_t)(b + 3)}); } };
  box(0, 1, 0); box(1, 2, 1); int nv = (int)pos.size() / 3;
  int aP = accessor(view(put(pos.data(), pos.size() * 4), pos.size() * 4, 34962), 5126, nv, "VEC3", {-0.2, 0, -0.2}, {0.2, 2, 0.2}); align();
  int aN = accessor(view(put(nor.data(), nor.size() * 4), nor.size() * 4, 34962), 5126, nv, "VEC3"); align();
  int aT = accessor(view(put(uv.data(), uv.size() * 4), uv.size() * 4, 34962), 5126, nv, "VEC2"); align();
  int aJ = accessor(view(put(jnt.data(), jnt.size() * 2), jnt.size() * 2, 34962), 5123, nv, "VEC4"); align();
  int aW = accessor(view(put(wts.data(), wts.size() * 4), wts.size() * 4, 34962), 5126, nv, "VEC4"); align();
  int aI = accessor(view(put(idx.data(), idx.size() * 2), idx.size() * 2, 34963), 5123, (int)idx.size(), "SCALAR"); align();
  float ibm[32] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1};   // joint 1 sits at y = 1
  int aB = accessor(view(put(ibm, sizeof ibm), sizeof ibm, 0), 5126, 2, "MAT4"); align();
  float t2[2] = {0, 1}, t3[3] = {0, 0.5f, 1}, s45 = std::sqrt(0.5f); float rot[8] = {0, 0, 0, 1, 0, 0, s45, s45}; float mv[9] = {0, 0, 0, 0, 0.5f, 0, 0, 0, 0};
  int aT2 = accessor(view(put(t2, sizeof t2), sizeof t2, 0), 5126, 2, "SCALAR", {0}, {1}); align();
  int aR = accessor(view(put(rot, sizeof rot), sizeof rot, 0), 5126, 2, "VEC4"); align();
  int aT3 = accessor(view(put(t3, sizeof t3), sizeof t3, 0), 5126, 3, "SCALAR", {0}, {1}); align();
  int aM = accessor(view(put(mv, sizeof mv), sizeof mv, 0), 5126, 3, "VEC3"); align();
  std::vector<unsigned char> png; { unsigned char px[4 * 4 * 4]; for (int i = 0; i < 16; i++) { px[i * 4] = 255; px[i * 4 + 1] = 0; px[i * 4 + 2] = 0; px[i * 4 + 3] = 255; }
    stbi_write_png_to_func([](void* ctx, void* d, int n) { auto* v = (std::vector<unsigned char>*)ctx; v->insert(v->end(), (unsigned char*)d, (unsigned char*)d + n); }, &png, 4, 4, 4, px, 16); }
  int vImg = view(put(png.data(), png.size()), png.size(), 0); align();
  json g = {{"asset", {{"version", "2.0"}, {"generator", "Premium Engine test"}}}, {"scene", 0}, {"scenes", {{{"nodes", {0, 2}}}}},
    {"nodes", {{{"name", "Root"}, {"children", {1}}}, {{"name", "Arm"}, {"translation", {0, 1, 0}}}, {{"name", "Body"}, {"mesh", 0}, {"skin", 0}}}},
    {"meshes", {{{"name", "Body"}, {"primitives", {{{"attributes", {{"POSITION", aP}, {"NORMAL", aN}, {"TEXCOORD_0", aT}, {"JOINTS_0", aJ}, {"WEIGHTS_0", aW}}}, {"indices", aI}, {"material", 0}}}}}}},
    {"materials", {{{"name", "Red"}, {"pbrMetallicRoughness", {{"baseColorTexture", {{"index", 0}}}, {"metallicFactor", 0}, {"roughnessFactor", 0.8}}}}}},
    {"textures", {{{"source", 0}}}}, {"images", {{{"bufferView", vImg}, {"mimeType", "image/png"}}}},
    {"skins", {{{"joints", {0, 1}}, {"inverseBindMatrices", aB}}}},
    {"animations", {{{"name", "Wave"}, {"samplers", {{{"input", aT2}, {"output", aR}, {"interpolation", "LINEAR"}}}}, {"channels", {{{"sampler", 0}, {"target", {{"node", 1}, {"path", "rotation"}}}}}}},
                    {{"name", "Bob"}, {"samplers", {{{"input", aT3}, {"output", aM}, {"interpolation", "LINEAR"}}}}, {"channels", {{{"sampler", 0}, {"target", {{"node", 0}, {"path", "translation"}}}}}}}}},
    {"accessors", acc}, {"bufferViews", views}, {"buffers", {{{"byteLength", bin.size()}}}}};
  std::string js = g.dump(); while (js.size() % 4) js += ' ';
  std::vector<unsigned char> out; auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) out.push_back((unsigned char)(v >> (8 * i))); };
  u32(0x46546C67); u32(2); u32((uint32_t)(12 + 8 + js.size() + 8 + bin.size()));
  u32((uint32_t)js.size()); u32(0x4E4F534A); out.insert(out.end(), js.begin(), js.end());
  u32((uint32_t)bin.size()); u32(0x004E4942); out.insert(out.end(), bin.begin(), bin.end());
  std::error_code ec; std::filesystem::create_directories(std::filesystem::u8path(file).parent_path(), ec);
  std::ofstream f(std::filesystem::u8path(file), std::ios::binary); f.write((const char*)out.data(), out.size()); return (bool)f;
}
}
