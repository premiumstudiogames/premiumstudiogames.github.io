#include "renderer.h"
#include "gl.h"
#include "log.h"
#include "arabic.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <string>
#include <algorithm>
#include <imgui.h>
#include <imgui_internal.h>   // ImTextCharFromUtf8

namespace pge {

glm::mat4 actorLocal(const Actor& a) {
  glm::mat4 m = glm::translate(glm::mat4(1), a.p);
  m = glm::rotate(m, glm::radians(a.r.y), {0, 1, 0}); m = glm::rotate(m, glm::radians(a.r.x), {1, 0, 0}); m = glm::rotate(m, glm::radians(a.r.z), {0, 0, 1});
  return glm::scale(m, glm::max(glm::abs(a.s), glm::vec3(1e-4f)) * glm::sign(a.s + glm::vec3(1e-9f)));
}

static const char* LIT_VS = R"(#version 330 core
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNor;
uniform mat4 uModel, uVP, uLightVP; out vec3 vPos; out vec3 vNor; out vec4 vLight;
void main(){ vec4 w = uModel * vec4(aPos,1.0); vPos = w.xyz; vNor = mat3(transpose(inverse(uModel))) * aNor; vLight = uLightVP * w; gl_Position = uVP * w; })";
// lighting shared by meshes and the block world (sun with shadow, point / spot lights, sky light, reflections, ACES + sRGB, fog)
static const char* LIGHT_LIB = R"(
uniform vec3 uCam, uSky, uGround, uFogColor; uniform float uAmbient, uExposure, uFogDensity; uniform int uFog, uMode, uHasShadow;
uniform int uNumLights; uniform int uLType[8]; uniform vec3 uLPos[8], uLDir[8], uLColor[8]; uniform float uLInt[8], uLRange[8], uLCos[8];
uniform int uHasSun; uniform vec3 uSunDir, uSunColor; uniform float uSunInt; uniform sampler2D uShadow;
const float PI = 3.14159265;
float D_GGX(float NoH, float a){ float a2=a*a; float d=NoH*NoH*(a2-1.0)+1.0; return a2/(PI*d*d); }
float V_Smith(float NoV, float NoL, float a){ float k=a*0.5; return 0.25/((NoV*(1.0-k)+k)*(NoL*(1.0-k)+k)); }
vec3 F_Schlick(vec3 f0, float VoH){ return f0 + (1.0-f0)*pow(1.0-VoH,5.0); }
vec3 brdf(vec3 N, vec3 V, vec3 L, vec3 base, float metal, float rough){
  vec3 H = normalize(V+L); float NoL=max(dot(N,L),0.0), NoV=max(dot(N,V),1e-3), NoH=max(dot(N,H),0.0), VoH=max(dot(V,H),0.0);
  float a = max(rough*rough, 0.002); vec3 f0 = mix(vec3(0.04), base, metal); vec3 F = F_Schlick(f0, VoH);
  vec3 spec = D_GGX(NoH,a)*V_Smith(NoV,NoL,a)*F; vec3 diff = (1.0-F)*(1.0-metal)*base/PI; return (diff+spec)*NoL; }
float shadowAt(vec4 lp){ if(uHasShadow==0) return 1.0; vec3 p = lp.xyz/lp.w*0.5+0.5; if(p.z>1.0||p.x<0.0||p.x>1.0||p.y<0.0||p.y>1.0) return 1.0;
  float s=0.0; vec2 ts = 1.0/vec2(textureSize(uShadow,0)); for(int x=-1;x<=1;x++) for(int y=-1;y<=1;y++){ float d=texture(uShadow,p.xy+vec2(x,y)*ts).r; s += (p.z-0.0015>d)?0.0:1.0; } return s/9.0; }
// three.js ACESFilmicToneMapping + sRGB output, so games look the same as in Premium Engine 0.1
vec3 rrt(vec3 v){ vec3 a = v*(v+0.0245786)-0.000090537; vec3 b = v*(0.983729*v+0.4329510)+0.238081; return a/b; }
vec3 aces(vec3 c){ const mat3 I = mat3(vec3(0.59719,0.07600,0.02840), vec3(0.35458,0.90834,0.13383), vec3(0.04823,0.01566,0.83777));
  const mat3 O = mat3(vec3(1.60475,-0.10208,-0.00327), vec3(-0.53108,1.10813,-0.07276), vec3(-0.07367,-0.00605,1.07602));
  c /= 0.6; c = I*c; c = rrt(c); c = O*c; return clamp(c,0.0,1.0); }
vec3 srgb(vec3 c){ return mix(c*12.92, 1.055*pow(c, vec3(1.0/2.4))-0.055, step(vec3(0.0031308), c)); }
vec3 lighting(vec3 N, vec3 P, vec3 base, float metal, float rough, float sh){
  vec3 V = normalize(uCam - P); vec3 col = vec3(0.0);
  if(uHasSun==1){ col += brdf(N,V,normalize(-uSunDir),base,metal,rough) * uSunColor * uSunInt * sh; }
  for(int i=0;i<uNumLights;i++){ vec3 Lv = uLPos[i]-P; float d = length(Lv); vec3 L = Lv/max(d,1e-4);
    float att = 1.0/max(d*d,0.01); if(uLRange[i]>0.0){ float r = clamp(1.0-pow(d/uLRange[i],4.0),0.0,1.0); att *= r*r; }
    if(uLType[i]==2){ float c = dot(-L, normalize(uLDir[i])); att *= smoothstep(uLCos[i], mix(uLCos[i],1.0,0.3), c); }
    col += brdf(N,V,L,base,metal,rough) * uLColor[i] * uLInt[i] * att; }
  vec3 hemi = mix(uGround, uSky, 0.5*N.y+0.5) * uAmbient;
  col += hemi * base * (1.0-metal) / PI;
  // sky reflection, so metals and glossy surfaces are not black without an environment map
  vec3 Rv = reflect(-V, N); vec3 env = mix(uGround, uSky, smoothstep(-0.3, 0.3, Rv.y)) * uAmbient;
  vec3 f0 = mix(vec3(0.04), base, metal); float NoV = max(dot(N,V), 0.0);
  col += env * (f0 + (max(vec3(1.0-rough), f0)-f0)*pow(1.0-NoV,5.0)) * (1.0 - rough*0.85) * 0.6;
  return col; }
vec3 finish(vec3 col, vec3 P){
  if(uMode!=1) col = aces(col * uExposure); col = srgb(col);
  if(uFog==1){ float d = length(uCam - P); float f = 1.0 - exp(-pow(uFogDensity*d,2.0)); col = mix(col, uFogColor, clamp(f,0.0,1.0)); }   // in display space, fades into the sky
  return col; }
)";
static const char* LIT_FS_HEAD = R"(#version 330 core
in vec3 vPos; in vec3 vNor; in vec4 vLight; out vec4 frag;
uniform vec3 uColor, uEmissive; uniform float uMetal, uRough, uOpacity; uniform int uUnlit;
)";
static const char* LIT_FS_MAIN = R"(
void main(){
  vec3 N = normalize(vNor); if(!gl_FrontFacing) N = -N;
  if(uMode==3){ frag = vec4(N*0.5+0.5,1.0); return; }
  vec3 base = uColor; vec3 col;
  if(uUnlit==1 || uMode==1){ col = base + uEmissive; if(uUnlit==1){ frag = vec4(srgb(col), uOpacity); return; } }
  else col = lighting(N, vPos, base, uMetal, uRough, shadowAt(vLight)) + uEmissive;
  frag = vec4(finish(col, vPos), uOpacity); })";
// the block world: chunk meshes with per-vertex colour, corner shadow (AO) and an optional texture atlas
static const char* VOX_VS = R"(#version 330 core
layout(location=0) in vec3 aPos; layout(location=1) in vec2 aUv; layout(location=2) in vec4 aCol; layout(location=3) in vec4 aNrm; layout(location=4) in uvec2 aInfo;
uniform mat4 uVP, uLightVP; uniform vec3 uOffset; uniform float uTime; uniform int uWater;
out vec3 vPos; out vec3 vNor; out vec4 vLight; out vec2 vUv; out vec4 vCol; flat out int vTile; flat out int vFlags;
void main(){ vec3 w = aPos + uOffset; vNor = normalize(aNrm.xyz);
  // liquid surfaces sit a little lower and move
  if(uWater==1 && vNor.y > 0.5) w.y += -0.05 + 0.018*sin(w.x*1.3 + uTime*1.4) + 0.014*sin(w.z*1.7 - uTime*1.1) + 0.008*sin((w.x+w.z)*3.1 + uTime*2.3);
  vPos = w; vLight = uLightVP * vec4(w,1.0); vUv = aUv; vCol = aCol; vTile = int(aInfo.x); vFlags = int(aInfo.y & 255u); gl_Position = uVP * vec4(w,1.0); })";
static const char* VOX_FS_HEAD = R"(#version 330 core
in vec3 vPos; in vec3 vNor; in vec4 vLight; in vec2 vUv; in vec4 vCol; flat in int vTile; flat in int vFlags; out vec4 frag;
uniform sampler2D uAtlas; uniform int uTiles; uniform float uTime;
vec3 toLin(vec3 c){ return mix(c/12.92, pow((c+0.055)/1.055, vec3(2.4)), step(vec3(0.04045), c)); }
vec3 atlasColor(vec3 base){
  if(uTiles > 0 && vTile != 65535){
    vec2 ts = vec2(textureSize(uAtlas, 0)); float tw = ts.x / float(uTiles); vec2 cell = vec2(tw) / ts;
    vec2 t = vec2(float(vTile % uTiles), float(vTile / uTiles)); vec2 f = fract(vUv); f.y = 1.0 - f.y;
    vec4 tx = texture(uAtlas, (t + f) * cell); base *= toLin(tx.rgb); }
  return base; }
)";
static const char* VOX_FS_MAIN = R"(
void main(){
  vec3 N = normalize(vNor);
  if(uMode==3){ frag = vec4(N*0.5+0.5,1.0); return; }
  vec3 base = atlasColor(toLin(vCol.rgb * 2.0));
  float ao = mix(0.42, 1.0, vCol.a);
  // faces of each axis get their own shade so terraces and walls that face away from the sun stay readable
  vec3 an = abs(N); ao *= N.y > 0.5 ? 1.0 : N.y < -0.5 ? 0.55 : mix(0.72, 0.86, an.x / max(an.x + an.z, 1e-4));
  vec3 glow = base * float(vFlags & 15) / 15.0 * 2.0;
  vec3 col = uMode==1 ? base * ao : lighting(N, vPos, base * ao, 0.0, 0.92, shadowAt(vLight));
  frag = vec4(finish(col + glow, vPos), 1.0); })";
// liquids: see-through, moving ripples, sky reflection that grows at low angles
static const char* WAT_FS_MAIN = R"(
void main(){
  vec3 N = normalize(vNor);
  if(N.y > 0.5){ vec2 p = vPos.xz; float t = uTime;
    vec3 dn = vec3(sin(p.x*3.1 + t*1.9)*0.07 + sin((p.x+p.y)*5.3 - t*2.7)*0.045, 0.0, cos(p.y*2.7 - t*1.5)*0.07 + sin((p.x-p.y)*4.1 + t*2.2)*0.045);
    N = normalize(N + dn); }
  if(!gl_FrontFacing) N = -N;
  vec3 base = atlasColor(toLin(vCol.rgb * 2.0));
  vec3 V = normalize(uCam - vPos); float fres = pow(1.0 - max(dot(N, V), 0.0), 3.0);
  vec3 col = uMode==1 ? base : lighting(N, vPos, base, 0.0, 0.07, shadowAt(vLight));
  float a = mix(vCol.a, 0.96, fres);
  frag = vec4(finish(col, vPos), a); })";
static const char* FLAT_VS = R"(#version 330 core
layout(location=0) in vec3 aPos; uniform mat4 uModel, uVP; out float vDist; uniform vec3 uCam;
void main(){ vec4 w = uModel*vec4(aPos,1.0); vDist = length(w.xyz - uCam); gl_Position = uVP*w; })";
static const char* FLAT_FS = R"(#version 330 core
uniform vec4 uColor; uniform float uFade; in float vDist; out vec4 frag;
void main(){ float a = uColor.a; if(uFade>0.0) a *= clamp(1.0 - vDist/uFade, 0.0, 1.0); frag = vec4(uColor.rgb, a); })";
static const char* DEPTH_VS = R"(#version 330 core
layout(location=0) in vec3 aPos; uniform mat4 uModel, uVP; void main(){ gl_Position = uVP*uModel*vec4(aPos,1.0); })";
static const char* DEPTH_FS = R"(#version 330 core
void main(){} )";
static const char* ID_FS = R"(#version 330 core
uniform vec4 uColor; out vec4 frag; void main(){ frag = uColor; })";

static const char* TEXT_VS = R"(#version 330 core
layout(location=0) in vec3 aPos; layout(location=1) in vec2 aUv; uniform mat4 uModel, uVP; out vec2 vUv;
void main(){ vUv = aUv; gl_Position = uVP*uModel*vec4(aPos,1.0); })";
static const char* TEXT_FS = R"(#version 330 core
in vec2 vUv; uniform sampler2D uTex; uniform vec4 uColor; uniform int uId; out vec4 frag;
void main(){ float a = texture(uTex, vUv).a; if(uId==1){ if(a<0.3) discard; frag = uColor; return; } frag = vec4(uColor.rgb, uColor.a*a); })";

static unsigned compile(const char* vs, const char* fs, const char* name) {
  auto sh = [&](GLenum t, const char* src) { unsigned s = glCreateShader(t); glShaderSource(s, 1, &src, nullptr); glCompileShader(s); int ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char b[2048]; glGetShaderInfoLog(s, 2048, nullptr, b); logMsg(std::string("error: shader ") + name + ": " + b, LogKind::Error); } return s; };
  unsigned p = glCreateProgram(), a = sh(GL_VERTEX_SHADER, vs), b = sh(GL_FRAGMENT_SHADER, fs); glAttachShader(p, a); glAttachShader(p, b); glLinkProgram(p);
  int ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok); if (!ok) { char buf[2048]; glGetProgramInfoLog(p, 2048, nullptr, buf); logMsg(std::string("error: program ") + name + ": " + buf, LogKind::Error); }
  glDeleteShader(a); glDeleteShader(b); return p;
}
static glm::vec3 lin(glm::vec3 c) { for (int i = 0; i < 3; i++) c[i] = c[i] <= 0.04045f ? c[i] / 12.92f : std::pow((c[i] + 0.055f) / 1.055f, 2.4f); return c; }
static int U(unsigned p, const char* n) { return glGetUniformLocation(p, n); }
static void M4(unsigned p, const char* n, const glm::mat4& m) { glUniformMatrix4fv(U(p, n), 1, GL_FALSE, glm::value_ptr(m)); }
static void V3(unsigned p, const char* n, const glm::vec3& v) { glUniform3f(U(p, n), v.x, v.y, v.z); }

bool Renderer::init() {
  std::string litFs = std::string(LIT_FS_HEAD) + LIGHT_LIB + LIT_FS_MAIN, voxFs = std::string(VOX_FS_HEAD) + LIGHT_LIB + VOX_FS_MAIN, watFs = std::string(VOX_FS_HEAD) + LIGHT_LIB + WAT_FS_MAIN;
  lit_ = compile(LIT_VS, litFs.c_str(), "lit"); vox_ = compile(VOX_VS, voxFs.c_str(), "voxels"); wat_ = compile(VOX_VS, watFs.c_str(), "water"); flat_ = compile(FLAT_VS, FLAT_FS, "flat"); depth_ = compile(DEPTH_VS, DEPTH_FS, "depth"); idp_ = compile(DEPTH_VS, ID_FS, "id"); text_ = compile(TEXT_VS, TEXT_FS, "text");
  glGenVertexArrays(1, &textVao_); glBindVertexArray(textVao_); glGenBuffers(1, &textVbo_); glBindBuffer(GL_ARRAY_BUFFER, textVbo_);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 20, (void*)0); glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 20, (void*)12); glBindVertexArray(0);
  // shadow map
  glGenFramebuffers(1, &shadowFbo_); glGenTextures(1, &shadowTex_); glBindTexture(GL_TEXTURE_2D, shadowTex_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, 2048, 2048, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER); float one[4] = {1, 1, 1, 1}; glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, one);
  glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTex_, 0); glDrawBuffer(GL_NONE); glReadBuffer(GL_NONE); glBindFramebuffer(GL_FRAMEBUFFER, 0);
  // helper line meshes
  std::vector<glm::vec3> L;
  { const float h = 0.5f; glm::vec3 c[8] = {{-h, -h, -h}, {h, -h, -h}, {h, h, -h}, {-h, h, -h}, {-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}};
    int e[24] = {0, 1, 1, 2, 2, 3, 3, 0, 4, 5, 5, 6, 6, 7, 7, 4, 0, 4, 1, 5, 2, 6, 3, 7}; for (int i : e) L.push_back(c[i]); wireBox_ = uploadLines(L); L.clear(); }
  { const float r = 0.35f, hh = 0.55f, PI = 3.14159265f; auto circ = [&](float y) { for (int i = 0; i < 24; i++) { float a = i / 24.f * 2 * PI, b = (i + 1) / 24.f * 2 * PI; L.push_back({r * std::cos(a), y, r * std::sin(a)}); L.push_back({r * std::cos(b), y, r * std::sin(b)}); } };
    circ(-hh); circ(hh); circ(0); for (int k = 0; k < 4; k++) { float a = k * PI / 2; L.push_back({r * std::cos(a), -hh, r * std::sin(a)}); L.push_back({r * std::cos(a), hh, r * std::sin(a)}); }
    for (int s = 0; s < 2; s++) for (int k = 0; k < 2; k++) for (int i = 0; i < 8; i++) { float a = i / 8.f * PI / 2, b = (i + 1) / 8.f * PI / 2, sg = s ? 1.f : -1.f; float ox = k ? 0 : 1, oz = k ? 1 : 0;
      L.push_back({ox * r * std::cos(a), sg * (hh + r * std::sin(a)), oz * r * std::cos(a)}); L.push_back({ox * r * std::cos(b), sg * (hh + r * std::sin(b)), oz * r * std::cos(b)});
      L.push_back({-ox * r * std::cos(a), sg * (hh + r * std::sin(a)), -oz * r * std::cos(a)}); L.push_back({-ox * r * std::cos(b), sg * (hh + r * std::sin(b)), -oz * r * std::cos(b)}); }
    wireCapsule_ = uploadLines(L); L.clear(); }
  { L = {{0, 0, 0}, {0, 0, -1}, {0, 0, -1}, {0.15f, 0, -0.8f}, {0, 0, -1}, {-0.15f, 0, -0.8f}, {0, 0, -1}, {0, 0.15f, -0.8f}}; arrow_ = uploadLines(L); L.clear(); }
  { L = {{0, 0, 0}, {1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {0, 0, 0}, {0, 0, 1}}; axes_ = uploadLines(L); L.clear(); }
  { const float w = 0.2f, h = 0.15f, d = 0.25f; glm::vec3 c[8] = {{-w, -h, -d}, {w, -h, -d}, {w, h, -d}, {-w, h, -d}, {-w, -h, d}, {w, -h, d}, {w, h, d}, {-w, h, d}};
    int e[24] = {0, 1, 1, 2, 2, 3, 3, 0, 4, 5, 5, 6, 6, 7, 7, 4, 0, 4, 1, 5, 2, 6, 3, 7}; for (int i : e) L.push_back(c[i]);
    glm::vec3 t(0, 0, -d), lens[4] = {{-0.15f, -0.12f, -0.5f}, {0.15f, -0.12f, -0.5f}, {0.15f, 0.12f, -0.5f}, {-0.15f, 0.12f, -0.5f}}; for (int i = 0; i < 4; i++) { L.push_back(t); L.push_back(lens[i]); L.push_back(lens[i]); L.push_back(lens[(i + 1) % 4]); }
    wireCam_ = uploadLines(L); L.clear(); }
  { for (int i = -50; i <= 50; i++) { L.push_back({(float)i, 0, -50}); L.push_back({(float)i, 0, 50}); L.push_back({-50, 0, (float)i}); L.push_back({50, 0, (float)i}); } grid_ = uploadLines(L); L.clear(); }
  return true;
}
void Renderer::shutdown() { glDeleteProgram(vox_); glDeleteProgram(wat_); glDeleteProgram(text_); glDeleteVertexArrays(1, &textVao_); glDeleteBuffers(1, &textVbo_); glDeleteProgram(lit_); glDeleteProgram(flat_); glDeleteProgram(depth_); glDeleteProgram(idp_); glDeleteFramebuffers(1, &shadowFbo_); glDeleteTextures(1, &shadowTex_);
  for (GpuMesh* m : {&wireBox_, &wireCapsule_, &arrow_, &axes_, &wireCam_, &grid_}) destroy(*m); releasePrimitives(); }

// quads for each glyph of the ImGui font atlas (the big font, index 1 when loaded), centred on the origin, 1 unit = text size
int Renderer::buildText(const TextItem& t) {
  ImGuiIO& io = ImGui::GetIO(); if (!io.Fonts || io.Fonts->Fonts.Size == 0) return 0;
  ImFont* font = io.Fonts->Fonts[io.Fonts->Fonts.Size > 1 ? 1 : 0]; float fs = font->FontSize;
  std::vector<float> v; float lineH = fs * 1.2f, k = t.size / fs;
  // split lines, measure each for centring
  std::vector<std::string> lines; { std::string cur; for (char c : visualText(t.text)) { if (c == '\n') { lines.push_back(cur); cur.clear(); } else cur += c; } lines.push_back(cur); }
  float total = lineH * lines.size();
  for (size_t li = 0; li < lines.size(); li++) {
    const std::string& L = lines[li]; float w = font->CalcTextSizeA(fs, 1e9f, 0, L.c_str()).x; float x = -w / 2, y0 = -total / 2 + li * lineH;
    const char* p = L.c_str(); const char* e = p + L.size();
    while (p < e) { unsigned int c = 0; int n = ImTextCharFromUtf8(&c, p, e); if (n <= 0) break; p += n;
      const ImFontGlyph* g = font->FindGlyph((ImWchar)c); if (!g) continue;
      if (g->Visible) { float ax = (x + g->X0) * k, bx = (x + g->X1) * k, ay = -(y0 + g->Y0) * k, by = -(y0 + g->Y1) * k;
        float q[6][5] = {{ax, by, 0, g->U0, g->V1}, {bx, by, 0, g->U1, g->V1}, {bx, ay, 0, g->U1, g->V0}, {ax, by, 0, g->U0, g->V1}, {bx, ay, 0, g->U1, g->V0}, {ax, ay, 0, g->U0, g->V0}};
        for (auto& r : q) v.insert(v.end(), r, r + 5); }
      x += g->AdvanceX; }
  }
  if (v.empty()) return 0;
  glBindBuffer(GL_ARRAY_BUFFER, textVbo_); glBufferData(GL_ARRAY_BUFFER, v.size() * 4, v.data(), GL_STREAM_DRAW); return (int)v.size() / 5;
}

void Renderer::resizeTarget(RenderTarget& rt, int w, int h) {
  w = std::max(w, 1); h = std::max(h, 1); if (rt.fbo && rt.w == w && rt.h == h) return; freeTarget(rt); rt.w = w; rt.h = h;
  auto make = [&](unsigned& fbo, unsigned& tex, unsigned& depth, GLint fmt, GLenum type) {
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex); glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, GL_RGBA, type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glGenRenderbuffers(1, &depth); glBindRenderbuffer(GL_RENDERBUFFER, depth); glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth); };
  make(rt.fbo, rt.color, rt.depth, GL_RGBA8, GL_UNSIGNED_BYTE); make(rt.idFbo, rt.idTex, rt.idDepth, GL_RGBA8, GL_UNSIGNED_BYTE);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
void Renderer::freeTarget(RenderTarget& rt) {
  if (rt.fbo) { glDeleteFramebuffers(1, &rt.fbo); glDeleteTextures(1, &rt.color); glDeleteRenderbuffers(1, &rt.depth); glDeleteFramebuffers(1, &rt.idFbo); glDeleteTextures(1, &rt.idTex); glDeleteRenderbuffers(1, &rt.idDepth); }
  rt = RenderTarget();
}

void Renderer::shadowPass(const Frame& f, glm::mat4& lightVP, bool& has) {
  has = false; const LightItem* sun = nullptr; for (auto& l : f.lights) if (l.type == 0) { sun = &l; break; }
  if (!sun || !sun->shadows || f.viewMode != VM_Lit) return;
  if (f.voxels.empty() && f.draws.empty()) return;
  glm::vec3 d = glm::normalize(sun->dir), c = f.shadowCenter; glm::vec3 up = std::abs(d.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
  glm::mat4 view = glm::lookAt(c - d * 80.f, c, up);
  // depth range: everything inside the 90 x 90 m light box that can cast a shadow, so tall terrain toward the sun is not clipped away
  float zNear = 1.f, zFar = 200.f;
  for (auto& v : f.voxels) { glm::vec3 lo(1e9f), hi(-1e9f);
    for (int i = 0; i < 8; i++) { glm::vec3 q = glm::vec3(view * glm::vec4((i & 1) ? v.mx.x : v.mn.x, (i & 2) ? v.mx.y : v.mn.y, (i & 4) ? v.mx.z : v.mn.z, 1)); lo = glm::min(lo, q); hi = glm::max(hi, q); }
    if (hi.x < -45.f || lo.x > 45.f || hi.y < -45.f || lo.y > 45.f) continue;
    zNear = std::min(zNear, -hi.z - 1.f); zFar = std::max(zFar, -lo.z + 1.f); }
  glm::mat4 proj = glm::ortho(-45.f, 45.f, -45.f, 45.f, zNear, zFar); lightVP = proj * view;
  glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_); glViewport(0, 0, 2048, 2048); glClear(GL_DEPTH_BUFFER_BIT); glEnable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
  glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(1.5f, 2.f);
  glUseProgram(depth_); M4(depth_, "uVP", lightVP);
  for (auto& it : f.draws) { if (!it.castShadow || !it.mesh || it.opacity < 0.5f || it.pickOnly || it.unlit) continue; M4(depth_, "uModel", it.model); glBindVertexArray(it.mesh->vao); glDrawElements(GL_TRIANGLES, it.mesh->count, GL_UNSIGNED_INT, nullptr); }
  for (auto& v : f.voxels) { M4(depth_, "uModel", glm::translate(glm::mat4(1), v.offset)); glBindVertexArray(v.vao); glDrawElements(GL_TRIANGLES, v.count, GL_UNSIGNED_INT, nullptr); }
  glDisable(GL_POLYGON_OFFSET_FILL); has = true;
}

void Renderer::render(const Frame& f, const Camera& cam, int w, int h, const RenderTarget* rt) {
  drawCalls = triangles = 0;
  glm::mat4 lightVP(1); bool hasShadow = false; shadowPass(f, lightVP, hasShadow);
  glBindFramebuffer(GL_FRAMEBUFFER, rt ? rt->fbo : 0); glViewport(0, 0, w, h);
  glm::vec3 bg = f.viewMode == VM_Wire || f.viewMode == VM_Normals ? glm::vec3(0.12f) : f.world.sky;   // the sky colour is a display colour (like three.js scene.background)
  glClearColor(bg.r, bg.g, bg.b, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDisable(GL_CULL_FACE); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glm::mat4 vp = cam.proj * cam.view;
  if (f.viewMode != VM_Wire) {
    auto common = [&](unsigned p) {
      glUseProgram(p); M4(p, "uVP", vp); M4(p, "uLightVP", lightVP); V3(p, "uCam", cam.pos);
      // linear-ish sky light colours from the display colours
      V3(p, "uSky", lin(f.world.skyLight)); V3(p, "uGround", lin(f.world.groundLight));
      glUniform1f(U(p, "uAmbient"), f.viewMode == VM_Unlit ? 1.f : f.world.ambient); glUniform1f(U(p, "uExposure"), f.world.exposure);
      glUniform1i(U(p, "uFog"), f.fog ? 1 : 0); V3(p, "uFogColor", f.world.fogColor); glUniform1f(U(p, "uFogDensity"), f.world.fogDensity);
      glUniform1i(U(p, "uMode"), f.viewMode); glUniform1i(U(p, "uHasShadow"), hasShadow ? 1 : 0);
      glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, shadowTex_); glUniform1i(U(p, "uShadow"), 0);
      int n = 0, sun = 0; int types[8] = {0}; glm::vec3 pos[8], dir[8], col[8]; float in[8], rg[8], cs[8];
      for (auto& l : f.lights) {
        if (l.type == 0 && !sun) { sun = 1; V3(p, "uSunDir", glm::normalize(l.dir)); V3(p, "uSunColor", lin(l.color)); glUniform1f(U(p, "uSunInt"), l.intensity); continue; }
        if (l.type == 0 || n >= 8) continue; types[n] = l.type; pos[n] = l.pos; dir[n] = l.dir; col[n] = lin(l.color); in[n] = l.intensity; rg[n] = l.range; cs[n] = l.cosOuter; n++;
      }
      glUniform1i(U(p, "uHasSun"), sun); glUniform1i(U(p, "uNumLights"), n);
      if (n) { glUniform1iv(U(p, "uLType"), n, types); glUniform3fv(U(p, "uLPos"), n, &pos[0].x); glUniform3fv(U(p, "uLDir"), n, &dir[0].x); glUniform3fv(U(p, "uLColor"), n, &col[0].x);
        glUniform1fv(U(p, "uLInt"), n, in); glUniform1fv(U(p, "uLRange"), n, rg); glUniform1fv(U(p, "uLCos"), n, cs); }
    };
    // the block world first (opaque, large), then meshes
    auto voxCommon = [&](unsigned p, int water) { common(p); glUniform1f(U(p, "uTime"), f.time); glUniform1i(U(p, "uWater"), water);
      glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, f.voxelAtlas); glUniform1i(U(p, "uAtlas"), 1); glUniform1i(U(p, "uTiles"), f.voxelAtlas ? f.voxelTiles : 0); glActiveTexture(GL_TEXTURE0); };
    if (!f.voxels.empty()) { unsigned p = vox_; voxCommon(p, 0);
      for (auto& v : f.voxels) { if (!v.visible) continue; V3(p, "uOffset", v.offset); glBindVertexArray(v.vao); glDrawElements(GL_TRIANGLES, v.count, GL_UNSIGNED_INT, nullptr); drawCalls++; triangles += v.count / 3; } }
    unsigned p = lit_;
    auto meshes = [&](int pass) {   // 0 opaque, 1 transparent
      common(p);
      for (auto& it : f.draws) {
        if (!it.mesh || it.pickOnly || (pass == 0) != (it.opacity >= 0.999f)) continue;
        M4(p, "uModel", it.model); V3(p, "uColor", lin(it.color)); V3(p, "uEmissive", lin(it.emissive));
        glUniform1f(U(p, "uMetal"), it.metal); glUniform1f(U(p, "uRough"), it.rough); glUniform1f(U(p, "uOpacity"), it.opacity); glUniform1i(U(p, "uUnlit"), it.unlit ? 1 : 0);
        if (pass == 1) glDepthMask(GL_FALSE);
        glBindVertexArray(it.mesh->vao); glDrawElements(GL_TRIANGLES, it.mesh->count, GL_UNSIGNED_INT, nullptr); drawCalls++; triangles += it.mesh->count / 3;
        glDepthMask(GL_TRUE);
      } };
    meshes(0);
    if (!f.voxelWater.empty()) {   // liquids of the block world: after everything opaque, far to near
      std::vector<const VoxelDraw*> ws; for (auto& v : f.voxelWater) ws.push_back(&v);
      std::sort(ws.begin(), ws.end(), [&](const VoxelDraw* a, const VoxelDraw* b) { return glm::length((a->mn + a->mx) * 0.5f - cam.pos) > glm::length((b->mn + b->mx) * 0.5f - cam.pos); });
      unsigned w = wat_; voxCommon(w, 1); glDepthMask(GL_FALSE);
      for (auto* v : ws) { V3(w, "uOffset", v->offset); glBindVertexArray(v->vao); glDrawElements(GL_TRIANGLES, v->count, GL_UNSIGNED_INT, nullptr); drawCalls++; triangles += v->count / 3; }
      glDepthMask(GL_TRUE); }
    meshes(1);
  }
  // 3D text (TextRender actors)
  if (!f.texts.empty()) { glUseProgram(text_); M4(text_, "uVP", vp); glUniform1i(U(text_, "uId"), 0); glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, (unsigned)(intptr_t)ImGui::GetIO().Fonts->TexID); glUniform1i(U(text_, "uTex"), 0); glDepthMask(GL_FALSE);
    for (auto& t : f.texts) { int n = buildText(t); if (!n) continue; M4(text_, "uModel", t.model); glUniform4f(U(text_, "uColor"), t.color.r, t.color.g, t.color.b, 1);
      glBindVertexArray(textVao_); glDrawArrays(GL_TRIANGLES, 0, n); drawCalls++; }
    glDepthMask(GL_TRUE); }
  // lines: grid, helpers, wireframe, selection
  unsigned q = flat_; glUseProgram(q); M4(q, "uVP", vp); V3(q, "uCam", cam.pos);
  if (f.grid) { glDepthMask(GL_FALSE); M4(q, "uModel", glm::translate(glm::mat4(1), {0, 0.003f, 0})); glUniform4f(U(q, "uColor"), 0.25f, 0.25f, 0.28f, 0.55f); glUniform1f(U(q, "uFade"), 60.f);
    glBindVertexArray(grid_.vao); glDrawArrays(GL_LINES, 0, grid_.count); glDepthMask(GL_TRUE); }
  glUniform1f(U(q, "uFade"), 0.f);
  if (f.viewMode == VM_Wire) for (auto& it : f.draws) { if (!it.mesh || it.pickOnly) continue; M4(q, "uModel", it.model); glUniform4f(U(q, "uColor"), 0.6f, 0.82f, 1.f, 1.f);
    glBindVertexArray(it.mesh->vao); glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); glDrawElements(GL_TRIANGLES, it.mesh->count, GL_UNSIGNED_INT, nullptr); glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); }
  for (int top = 0; top < 2; top++) { if (top) glDisable(GL_DEPTH_TEST);
    for (auto& l : f.lines) { if (l.onTop != (bool)top || !l.mesh) continue; M4(q, "uModel", l.model); glUniform4f(U(q, "uColor"), l.color.r, l.color.g, l.color.b, l.color.a);
      glBindVertexArray(l.mesh->vao); if (l.wire) { glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); glEnable(GL_POLYGON_OFFSET_LINE); glPolygonOffset(-1, -1); }
      if (l.mesh->lines) glDrawArrays(GL_LINES, 0, l.mesh->count); else glDrawElements(GL_TRIANGLES, l.mesh->count, GL_UNSIGNED_INT, nullptr);
      if (l.wire) { glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); glDisable(GL_POLYGON_OFFSET_LINE); } }
    glEnable(GL_DEPTH_TEST); }
  glBindVertexArray(0); glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

unsigned Renderer::pick(const Frame& f, const Camera& cam, RenderTarget& rt, int x, int y) {
  if (!rt.idFbo || x < 0 || y < 0 || x >= rt.w || y >= rt.h) return 0;
  glBindFramebuffer(GL_FRAMEBUFFER, rt.idFbo); glViewport(0, 0, rt.w, rt.h); glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
  glUseProgram(idp_); M4(idp_, "uVP", cam.proj * cam.view);
  auto idc = [&](unsigned id) { glUniform4f(U(idp_, "uColor"), (id & 255) / 255.f, ((id >> 8) & 255) / 255.f, ((id >> 16) & 255) / 255.f, 1); };
  for (auto& it : f.draws) { if (!it.mesh || !it.id) continue; idc(it.id); M4(idp_, "uModel", it.model); glBindVertexArray(it.mesh->vao); glDrawElements(GL_TRIANGLES, it.mesh->count, GL_UNSIGNED_INT, nullptr); }
  glLineWidth(1); for (auto& l : f.lines) { if (!l.mesh || !l.id) continue; idc(l.id); M4(idp_, "uModel", l.model); glBindVertexArray(l.mesh->vao);
    if (l.mesh->lines) glDrawArrays(GL_LINES, 0, l.mesh->count); else glDrawElements(GL_TRIANGLES, l.mesh->count, GL_UNSIGNED_INT, nullptr); }
  if (!f.texts.empty()) { glUseProgram(text_); M4(text_, "uVP", cam.proj * cam.view); glUniform1i(U(text_, "uId"), 1); glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, (unsigned)(intptr_t)ImGui::GetIO().Fonts->TexID); glUniform1i(U(text_, "uTex"), 0);
    for (auto& t : f.texts) { if (!t.id) continue; int n = buildText(t); if (!n) continue; M4(text_, "uModel", t.model);
      glUniform4f(U(text_, "uColor"), (t.id & 255) / 255.f, ((t.id >> 8) & 255) / 255.f, ((t.id >> 16) & 255) / 255.f, 1); glBindVertexArray(textVao_); glDrawArrays(GL_TRIANGLES, 0, n); } }
  unsigned char px[4] = {0}; glPixelStorei(GL_PACK_ALIGNMENT, 1); glReadPixels(x, rt.h - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  glBindFramebuffer(GL_FRAMEBUFFER, 0); glEnable(GL_BLEND);
  return px[0] | (px[1] << 8) | (px[2] << 16);
}
}
