#pragma once
// The engine's own character (like Unreal's mannequin), written as a .glb: a skinned figure 1.8 m tall facing +Z with 18 bones
// (Root, Hips, Spine, Chest, Neck, Head, arms, hands, legs, feet) and the animations a Third Person game needs: Idle, Walk,
// Run, Jump, Fall. Made by code (no outside files, no licences): PremiumEngineTests --write-mannequin assets/Characters/Mannequin.glb
#include <string>
#include <vector>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <functional>
#include <nlohmann/json.hpp>

namespace pge {
inline bool writeMannequin(const std::string& file) {
  using json = nlohmann::json; const float PI = 3.14159265f, D = PI / 180;
  struct Bone { const char* name; int parent; float x, y, z; };
  static const Bone B[] = {{"Root", -1, 0, 0, 0}, {"Hips", 0, 0, 0.95f, 0}, {"Spine", 1, 0, 0.12f, 0}, {"Chest", 2, 0, 0.2f, 0}, {"Neck", 3, 0, 0.22f, 0}, {"Head", 4, 0, 0.08f, 0},
    {"UpperArm_L", 3, 0.21f, 0.17f, 0}, {"LowerArm_L", 6, 0, -0.29f, 0}, {"Hand_L", 7, 0, -0.26f, 0}, {"UpperArm_R", 3, -0.21f, 0.17f, 0}, {"LowerArm_R", 9, 0, -0.29f, 0}, {"Hand_R", 10, 0, -0.26f, 0},
    {"UpperLeg_L", 1, 0.1f, -0.05f, 0}, {"LowerLeg_L", 12, 0, -0.43f, 0}, {"Foot_L", 13, 0, -0.41f, 0}, {"UpperLeg_R", 1, -0.1f, -0.05f, 0}, {"LowerLeg_R", 15, 0, -0.43f, 0}, {"Foot_R", 16, 0, -0.41f, 0}};
  const int NB = sizeof(B) / sizeof(B[0]);
  float wx[NB], wy[NB], wz[NB]; for (int i = 0; i < NB; i++) { wx[i] = B[i].x; wy[i] = B[i].y; wz[i] = B[i].z; if (B[i].parent >= 0) { wx[i] += wx[B[i].parent]; wy[i] += wy[B[i].parent]; wz[i] += wz[B[i].parent]; } }
  // the body: rounded boxes (8 x 6 rings) on their bones; a limb's end near its parent bends with both (50 / 50), so knees and elbows don't break
  std::vector<float> pos, nor, col; std::vector<uint16_t> jnt, idx; std::vector<float> wts;
  auto part = [&](float cx, float cy, float cz, float sx, float sy, float sz, int bone, int blendTop, float r, float g, float b) {
    const int SEG = 10, RING = 8; uint16_t base = (uint16_t)(pos.size() / 3);
    for (int i = 0; i <= RING; i++) { float v = (float)i / RING, th = v * PI; float yy = -std::cos(th), rr = std::sin(th);
      for (int j = 0; j <= SEG; j++) { float u = (float)j / SEG * 2 * PI; float nx = std::cos(u) * rr, nz = std::sin(u) * rr, ny = yy;
        // a superellipsoid (squarish), so arms and legs look like a mannequin's, not like balloons
        auto sq = [](float a) { return (a < 0 ? -1.f : 1.f) * std::pow(std::fabs(a), 0.55f); };
        float px = cx + sx * 0.5f * sq(nx), py = cy + sy * 0.5f * sq(ny), pz = cz + sz * 0.5f * sq(nz);
        pos.insert(pos.end(), {px, py, pz}); float ln = std::sqrt(nx * nx / (sx * sx) + ny * ny / (sy * sy) + nz * nz / (sz * sz)) + 1e-6f;
        nor.insert(nor.end(), {nx / sx / ln, ny / sy / ln, nz / sz / ln}); col.insert(col.end(), {r, g, b, 1});
        float top = (py - (cy - sy * 0.5f)) / sy;   // 0 at the bottom, 1 at the top
        if (blendTop >= 0 && top > 0.85f) { float w = 0.5f * (top - 0.85f) / 0.15f; jnt.insert(jnt.end(), {(uint16_t)bone, (uint16_t)blendTop, 0, 0}); wts.insert(wts.end(), {1 - w, w, 0, 0}); }
        else { jnt.insert(jnt.end(), {(uint16_t)bone, 0, 0, 0}); wts.insert(wts.end(), {1, 0, 0, 0}); } } }
    for (int i = 0; i < RING; i++) for (int j = 0; j < SEG; j++) { uint16_t a = base + i * (SEG + 1) + j, c = a + SEG + 1; idx.insert(idx.end(), {a, c, (uint16_t)(a + 1), (uint16_t)(a + 1), c, (uint16_t)(c + 1)}); }
  };
  const float BR = 0.78f, BG = 0.8f, BB = 0.84f, DR = 0.25f, DG = 0.42f, DB = 0.78f;
  part(0, wy[1], 0, 0.34f, 0.2f, 0.22f, 1, -1, DR, DG, DB);                       // pelvis
  part(0, wy[2] + 0.07f, 0, 0.3f, 0.2f, 0.19f, 2, 1, DR, DG, DB);                  // belly
  part(0, wy[3] + 0.11f, 0.005f, 0.4f, 0.32f, 0.23f, 3, 2, DR, DG, DB);            // chest
  part(0, wy[4] + 0.03f, 0, 0.1f, 0.1f, 0.1f, 4, 3, BR, BG, BB);                   // neck
  part(0, wy[5] + 0.12f, 0.01f, 0.21f, 0.26f, 0.24f, 5, -1, BR, BG, BB);           // head
  part(0, wy[5] + 0.13f, 0.12f, 0.12f, 0.05f, 0.02f, 5, -1, 0.1f, 0.12f, 0.15f);   // a visor: which way it faces
  for (int s = 0; s < 2; s++) { int ua = s ? 9 : 6; float x = wx[ua];
    part(x, wy[ua] - 0.14f, 0, 0.1f, 0.32f, 0.1f, ua, 3, BR, BG, BB); part(x, wy[ua + 1] - 0.13f, 0, 0.085f, 0.28f, 0.085f, ua + 1, ua, BR, BG, BB); part(x, wy[ua + 2] - 0.06f, 0, 0.07f, 0.13f, 0.045f, ua + 2, ua + 1, BR, BG, BB);
    int ul = s ? 15 : 12; float lx = wx[ul];
    part(lx, wy[ul] - 0.21f, 0, 0.15f, 0.46f, 0.16f, ul, 1, DR, DG, DB); part(lx, wy[ul + 1] - 0.2f, 0, 0.12f, 0.44f, 0.13f, ul + 1, ul, BR, BG, BB); part(lx, 0.045f, 0.05f, 0.11f, 0.09f, 0.26f, ul + 2, ul + 1, 0.2f, 0.22f, 0.25f); }
  int nv = (int)pos.size() / 3;
  // the animations: each bone's turn (degrees about X, Y, Z) over time, and the hips' height
  struct Key { std::vector<float> t; std::vector<std::vector<float>> q; std::vector<float> hipY; };
  auto quat = [&](float ax, float ay, float az) { float cx = std::cos(ax * D / 2), sx = std::sin(ax * D / 2), cy = std::cos(ay * D / 2), sy = std::sin(ay * D / 2), cz = std::cos(az * D / 2), sz = std::sin(az * D / 2);
    return std::vector<float>{sx * cy * cz - cx * sy * sz, cx * sy * cz + sx * cy * sz, cx * cy * sz - sx * sy * cz, cx * cy * cz + sx * sy * sz}; };   // X then Y then Z (applied Z·Y·X)
  struct Anim { std::string name; float len; int keys; std::function<void(float, std::vector<std::vector<float>>&, float&)> pose; };
  auto zero = [&](std::vector<std::vector<float>>& r) { r.assign(NB, {0, 0, 0}); };
  std::vector<Anim> anims = {
    {"Idle", 2.4f, 24, [&](float p, std::vector<std::vector<float>>& r, float& hy) { zero(r); float s = std::sin(p * 2 * PI); r[3] = {s * 1.5f, 0, 0}; r[5] = {-s * 2, std::sin(p * 2 * PI + 1) * 3, 0};
        r[6] = {0, 0, 4 + s * 2}; r[9] = {0, 0, -4 - s * 2}; r[7] = {-8, 0, 0}; r[10] = {-8, 0, 0}; hy = wy[1] - 0.005f + s * 0.006f; }},
    {"Walk", 1.0f, 24, [&](float p, std::vector<std::vector<float>>& r, float& hy) { zero(r); float s = std::sin(p * 2 * PI), c = std::cos(p * 2 * PI);
        r[12] = {-26 * s, 0, 0}; r[15] = {26 * s, 0, 0}; r[13] = {std::max(0.f, 45 * c) + 6, 0, 0}; r[16] = {std::max(0.f, -45 * c) + 6, 0, 0}; r[14] = {-10 * s, 0, 0}; r[17] = {10 * s, 0, 0};
        r[6] = {24 * s, 0, 5}; r[9] = {-24 * s, 0, -5}; r[7] = {-18 + 10 * s, 0, 0}; r[10] = {-18 - 10 * s, 0, 0}; r[2] = {3, 6 * s, 0}; r[3] = {0, -8 * s, 0}; r[5] = {0, 4 * s, 0};
        hy = wy[1] - 0.02f + 0.025f * std::fabs(std::cos(p * 2 * PI)); }},
    {"Run", 0.66f, 20, [&](float p, std::vector<std::vector<float>>& r, float& hy) { zero(r); float s = std::sin(p * 2 * PI), c = std::cos(p * 2 * PI);
        r[12] = {-48 * s - 10, 0, 0}; r[15] = {48 * s - 10, 0, 0}; r[13] = {std::max(0.f, 95 * c) + 15, 0, 0}; r[16] = {std::max(0.f, -95 * c) + 15, 0, 0}; r[14] = {-20 * s, 0, 0}; r[17] = {20 * s, 0, 0};
        r[6] = {45 * s, 0, 8}; r[9] = {-45 * s, 0, -8}; r[7] = {-75, 0, 0}; r[10] = {-75, 0, 0}; r[2] = {12, 10 * s, 0}; r[3] = {4, -14 * s, 0}; r[5] = {-10, 6 * s, 0};
        hy = wy[1] - 0.06f + 0.06f * std::fabs(std::cos(p * 2 * PI)); }},
    {"Jump", 0.4f, 8, [&](float p, std::vector<std::vector<float>>& r, float& hy) { zero(r); float k = std::min(1.f, p * 2.5f);
        r[12] = {-55 * k, 0, 0}; r[13] = {80 * k, 0, 0}; r[15] = {-20 * k, 0, 0}; r[16] = {45 * k, 0, 0}; r[6] = {-140 * k, 0, 15 * k}; r[9] = {-140 * k, 0, -15 * k}; r[7] = {-20 * k, 0, 0}; r[10] = {-20 * k, 0, 0};
        r[2] = {-5 * k, 0, 0}; r[5] = {-10 * k, 0, 0}; hy = wy[1]; }},
    {"Fall", 1.2f, 16, [&](float p, std::vector<std::vector<float>>& r, float& hy) { zero(r); float s = std::sin(p * 2 * PI);
        r[12] = {-25 + 8 * s, 0, 4}; r[15] = {-10 - 8 * s, 0, -4}; r[13] = {35, 0, 0}; r[16] = {25, 0, 0}; r[6] = {-20 + 10 * s, 0, 70}; r[9] = {-20 - 10 * s, 0, -70}; r[7] = {-25, 0, 0}; r[10] = {-25, 0, 0};
        r[3] = {-6, 0, 0}; r[5] = {-8, 0, 0}; hy = wy[1]; }}};
  // ---- the file
  std::vector<unsigned char> bin; json views = json::array(), acc = json::array();
  auto align = [&] { while (bin.size() % 4) bin.push_back(0); };
  auto put = [&](const void* p, size_t n) { size_t at = bin.size(); bin.insert(bin.end(), (const unsigned char*)p, (const unsigned char*)p + n); align(); return at; };
  auto view = [&](size_t off, size_t len, int target) { json v = {{"buffer", 0}, {"byteOffset", off}, {"byteLength", len}}; if (target) v["target"] = target; views.push_back(v); return (int)views.size() - 1; };
  auto accessor = [&](const void* p, size_t bytes, int target, int comp, int count, const char* type, json mn = nullptr, json mx = nullptr) {
    int v = view(put(p, bytes), bytes, target); json a = {{"bufferView", v}, {"componentType", comp}, {"count", count}, {"type", type}}; if (!mn.is_null()) { a["min"] = mn; a["max"] = mx; } acc.push_back(a); return (int)acc.size() - 1; };
  float mnP[3] = {1e9f, 1e9f, 1e9f}, mxP[3] = {-1e9f, -1e9f, -1e9f}; for (int i = 0; i < nv; i++) for (int k = 0; k < 3; k++) { mnP[k] = std::min(mnP[k], pos[i * 3 + k]); mxP[k] = std::max(mxP[k], pos[i * 3 + k]); }
  int aP = accessor(pos.data(), pos.size() * 4, 34962, 5126, nv, "VEC3", {mnP[0], mnP[1], mnP[2]}, {mxP[0], mxP[1], mxP[2]});
  int aN = accessor(nor.data(), nor.size() * 4, 34962, 5126, nv, "VEC3"), aC = accessor(col.data(), col.size() * 4, 34962, 5126, nv, "VEC4");
  int aJ = accessor(jnt.data(), jnt.size() * 2, 34962, 5123, nv, "VEC4"), aW = accessor(wts.data(), wts.size() * 4, 34962, 5126, nv, "VEC4");
  int aI = accessor(idx.data(), idx.size() * 2, 34963, 5123, (int)idx.size(), "SCALAR");
  std::vector<float> ibm; for (int i = 0; i < NB; i++) { float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -wx[i], -wy[i], -wz[i], 1}; ibm.insert(ibm.end(), m, m + 16); }
  int aB = accessor(ibm.data(), ibm.size() * 4, 0, 5126, NB, "MAT4");
  json janims = json::array();
  for (auto& an : anims) { std::vector<float> t; std::vector<std::vector<float>> qs(NB); std::vector<float> hips;
    bool loop = an.name != "Jump"; int n = an.keys + (loop ? 1 : 0);
    for (int k = 0; k < n; k++) { float p = (float)k / an.keys; if (!loop) p = (float)k / (an.keys - 1); t.push_back(p * an.len); std::vector<std::vector<float>> r; float hy = wy[1]; an.pose(loop ? std::fmod(p, 1.f) : p, r, hy);
      for (int b = 0; b < NB; b++) { auto q = quat(r[b][0], r[b][1], r[b][2]); qs[b].insert(qs[b].end(), q.begin(), q.end()); } hips.insert(hips.end(), {0, hy, 0}); }
    int at = accessor(t.data(), t.size() * 4, 0, 5126, (int)t.size(), "SCALAR", {t.front()}, {t.back()});
    json samplers = json::array(), channels = json::array();
    for (int b = 1; b < NB; b++) { int ao = accessor(qs[b].data(), qs[b].size() * 4, 0, 5126, (int)t.size(), "VEC4"); samplers.push_back({{"input", at}, {"output", ao}, {"interpolation", "LINEAR"}});
      channels.push_back({{"sampler", (int)samplers.size() - 1}, {"target", {{"node", b}, {"path", "rotation"}}}}); }
    int ah = accessor(hips.data(), hips.size() * 4, 0, 5126, (int)t.size(), "VEC3"); samplers.push_back({{"input", at}, {"output", ah}, {"interpolation", "LINEAR"}});
    channels.push_back({{"sampler", (int)samplers.size() - 1}, {"target", {{"node", 1}, {"path", "translation"}}}});
    janims.push_back({{"name", an.name}, {"samplers", samplers}, {"channels", channels}}); }
  json nodes = json::array(); for (int i = 0; i < NB; i++) { json o = {{"name", B[i].name}, {"translation", {B[i].x, B[i].y, B[i].z}}}; json ch = json::array(); for (int k = 0; k < NB; k++) if (B[k].parent == i) ch.push_back(k); if (!ch.empty()) o["children"] = ch; nodes.push_back(o); }
  nodes.push_back({{"name", "Mannequin"}, {"mesh", 0}, {"skin", 0}});
  json joints = json::array(); for (int i = 0; i < NB; i++) joints.push_back(i);
  json g = {{"asset", {{"version", "2.0"}, {"generator", "Premium Engine mannequin"}}}, {"scene", 0}, {"scenes", {{{"nodes", {0, NB}}}}}, {"nodes", nodes},
    {"meshes", {{{"name", "Mannequin"}, {"primitives", {{{"attributes", {{"POSITION", aP}, {"NORMAL", aN}, {"COLOR_0", aC}, {"JOINTS_0", aJ}, {"WEIGHTS_0", aW}}}, {"indices", aI}, {"material", 0}}}}}}},
    {"materials", {{{"name", "Mannequin"}, {"pbrMetallicRoughness", {{"baseColorFactor", {1, 1, 1, 1}}, {"metallicFactor", 0.1}, {"roughnessFactor", 0.45}}}}}},
    {"skins", {{{"joints", joints}, {"inverseBindMatrices", aB}, {"skeleton", 0}}}}, {"animations", janims}, {"accessors", acc}, {"bufferViews", views}, {"buffers", {{{"byteLength", bin.size()}}}}};
  std::string js = g.dump(); while (js.size() % 4) js += ' ';
  std::vector<unsigned char> out; auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) out.push_back((unsigned char)(v >> (8 * i))); };
  u32(0x46546C67); u32(2); u32((uint32_t)(12 + 8 + js.size() + 8 + bin.size())); u32((uint32_t)js.size()); u32(0x4E4F534A); out.insert(out.end(), js.begin(), js.end());
  u32((uint32_t)bin.size()); u32(0x004E4942); out.insert(out.end(), bin.begin(), bin.end());
  std::error_code ec; std::filesystem::create_directories(std::filesystem::u8path(file).parent_path(), ec);
  std::ofstream f(std::filesystem::u8path(file), std::ios::binary); f.write((const char*)out.data(), out.size()); return (bool)f;
}
}
