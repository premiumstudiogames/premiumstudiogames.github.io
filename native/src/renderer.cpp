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

// meshes: primitives (position, normal, uv) and models (also vertex colour, joints and weights). Skinned models read their
// joints' matrices from a float texture (4 texels per joint), so a character can have any number of bones
static const char* SKIN_LIB = R"(#version 330 core
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNor; layout(location=2) in vec2 aUv; layout(location=3) in vec4 aCol; layout(location=4) in uvec4 aJoint; layout(location=5) in vec4 aWeight;
uniform mat4 uModel, uVP; uniform int uSkin; uniform sampler2D uBones;
mat4 bone(uint i){ int y = int(i); return mat4(texelFetch(uBones, ivec2(0, y), 0), texelFetch(uBones, ivec2(1, y), 0), texelFetch(uBones, ivec2(2, y), 0), texelFetch(uBones, ivec2(3, y), 0)); }
mat4 skinMatrix(){ if(uSkin == 0) return uModel; return uModel * (bone(aJoint.x) * aWeight.x + bone(aJoint.y) * aWeight.y + bone(aJoint.z) * aWeight.z + bone(aJoint.w) * aWeight.w); }
)";
static const char* LIT_VS_MAIN = R"(
uniform mat4 uLightVP; out vec3 vPos; out vec3 vNor; out vec4 vLight; out vec2 vUv; out vec4 vCol;
void main(){ mat4 M = skinMatrix(); vec4 w = M * vec4(aPos,1.0); vPos = w.xyz; vNor = mat3(transpose(inverse(M))) * aNor; vLight = uLightVP * w; vUv = aUv; vCol = aCol; gl_Position = uVP * w; })";
// lighting shared by meshes and the block world (sun with shadow, point / spot lights, sky light, reflections, ACES + sRGB, fog)
static const char* LIGHT_LIB = R"(
uniform vec3 uCam, uSky, uGround, uFogColor; uniform float uAmbient, uExposure, uFogDensity; uniform int uFog, uMode, uHasShadow;
uniform int uNumLights; uniform int uLType[8]; uniform vec3 uLPos[8], uLDir[8], uLColor[8]; uniform float uLInt[8], uLRange[8], uLCos[8];
uniform int uHasSun; uniform vec3 uSunDir, uSunColor; uniform float uSunInt; uniform sampler2DShadow uShadow;
uniform mat4 uCasc[4]; uniform vec4 uSplit; uniform vec4 uTexel; uniform vec3 uCamFwd, uFogLin; uniform int uCascades;
uniform samplerCube uEnv; uniform int uHasEnv; uniform vec3 uEnvPos; uniform float uEnvRadius, uEnvInt, uEnvMips;   // a reflection capture around the surface
const float PI = 3.14159265; float gAo = 1.0;   // ambient occlusion of the surface (maps), on the sky light only
float D_GGX(float NoH, float a){ float a2=a*a; float d=NoH*NoH*(a2-1.0)+1.0; return a2/(PI*d*d); }
float V_Smith(float NoV, float NoL, float a){ float k=a*0.5; return 0.25/((NoV*(1.0-k)+k)*(NoL*(1.0-k)+k)); }
vec3 F_Schlick(vec3 f0, float VoH){ return f0 + (1.0-f0)*pow(1.0-VoH,5.0); }
vec3 brdf(vec3 N, vec3 V, vec3 L, vec3 base, float metal, float rough){
  vec3 H = normalize(V+L); float NoL=max(dot(N,L),0.0), NoV=max(dot(N,V),1e-3), NoH=max(dot(N,H),0.0), VoH=max(dot(V,H),0.0);
  float a = max(rough*rough, 0.002); vec3 f0 = mix(vec3(0.04), base, metal); vec3 F = F_Schlick(f0, VoH);
  vec3 spec = D_GGX(NoH,a)*V_Smith(NoV,NoL,a)*F; vec3 diff = (1.0-F)*(1.0-metal)*base/PI; return (diff+spec)*NoL; }
// sun shadows: cascades (sharp near the camera, far ones cover 160 m) in a 2 x 2 atlas, soft edges (16 rotated taps,
// each a hardware 2 x 2 comparison), moved off the surface along its normal so flat ground has no stripes
const vec2 POIS[16] = vec2[](vec2(-0.94201624,-0.39906216), vec2(0.94558609,-0.76890725), vec2(-0.09418410,-0.92938870), vec2(0.34495938,0.29387760),
  vec2(-0.91588581,0.45771432), vec2(-0.81544232,-0.87912464), vec2(-0.38277543,0.27676845), vec2(0.97484398,0.75648379), vec2(0.44323325,-0.97511554),
  vec2(0.53742981,-0.47373420), vec2(-0.26496911,-0.41893023), vec2(0.79197514,0.19090188), vec2(-0.24188840,0.99706507), vec2(-0.81409955,0.91437590),
  vec2(0.19984126,0.78641367), vec2(0.14383161,-0.14100790));
float cascadeShadow(int i, vec3 P, vec3 N){
  vec3 L = normalize(-uSunDir); float nl = clamp(dot(N, L), 0.0, 1.0);
  vec3 q = P + N * uTexel[i] * (1.5 + 2.0*(1.0-nl)) + L * uTexel[i] * 0.5;
  vec4 lp = uCasc[i] * vec4(q, 1.0); vec3 c = lp.xyz / lp.w * 0.5 + 0.5; if(c.z >= 1.0) return 1.0;
  vec2 tile = vec2(float(i % 2), float(i / 2)) * 0.5; float ts = 1.0 / float(textureSize(uShadow, 0).x);
  float ang = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) * 6.2831853; mat2 R = mat2(cos(ang), sin(ang), -sin(ang), cos(ang));
  float s = 0.0, rad = 1.6;
  for(int k = 0; k < 16; k++){ vec2 uv = clamp(c.xy + R * POIS[k] * rad * ts * 2.0, vec2(ts), vec2(1.0 - ts)); s += texture(uShadow, vec3(tile + uv * 0.5, c.z - 0.0004)); }
  return s / 16.0; }
float shadowAt(vec3 P, vec3 N){
  if(uHasShadow==0) return 1.0; float d = dot(P - uCam, uCamFwd);
  for(int i = 0; i < 4; i++){ if(i >= uCascades) break; if(d < uSplit[i]){
      float s = cascadeShadow(i, P, N); float edge = uSplit[i] * 0.88;   // the last 12 % blends into the next cascade (or no shadow after the last)
      if(d > edge){ float t = (d - edge) / (uSplit[i] - edge); float n = i + 1 < uCascades ? cascadeShadow(i + 1, P, N) : 1.0; s = mix(s, n, t); }
      return s; } }
  return 1.0; }
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
  vec3 hemi = mix(uGround, uSky, 0.5*N.y+0.5) * uAmbient * gAo;
  col += hemi * base * (1.0-metal) / PI;
  // sky reflection, so metals and glossy surfaces are not black without an environment map
  vec3 Rv = reflect(-V, N); vec3 env = mix(uGround, uSky, smoothstep(-0.3, 0.3, Rv.y)) * uAmbient;
  vec3 f0 = mix(vec3(0.04), base, metal); float NoV = max(dot(N,V), 0.0);
  vec3 fr = f0 + (max(vec3(1.0-rough), f0)-f0)*pow(1.0-NoV,5.0); float k = (1.0 - rough*0.85) * 0.6;
  if(uHasEnv == 1){   // what the capture saw in that direction (parallax corrected on its sphere), blurrier for rough surfaces
    vec3 d = P - uEnvPos; float b = dot(d, Rv), c = dot(d, d) - uEnvRadius * uEnvRadius; float t = -b + sqrt(max(b * b - c, 0.0));
    vec3 cap = textureLod(uEnv, d + Rv * t, rough * uEnvMips).rgb; cap = mix(cap / 12.92, pow((cap + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), cap)) / max(uExposure, 1e-3) * uEnvInt;
    float w = 1.0 - smoothstep(uEnvRadius * 0.8, uEnvRadius, length(d)); env = mix(env, cap, w); k = mix(k, 1.0 - rough * 0.5, w); }
  col += env * fr * k * gAo;
  return col; }
// the scene is in linear HDR light: the post pass tonemaps (ACES) and adds bloom and ambient occlusion.
// Fog fades to the fog colour as it will look on screen (uFogLin is its value before the tonemapper), so it meets the sky.
vec3 finish(vec3 col, vec3 P){
  if(uFog==1){ float d = length(uCam - P); float f = 1.0 - exp(-pow(uFogDensity*d,2.0)); col = mix(col, uFogLin, clamp(f,0.0,1.0)); }
  return col; }
)";
static const char* LIT_FS_HEAD = R"(#version 330 core
in vec3 vPos; in vec3 vNor; in vec4 vLight; in vec2 vUv; in vec4 vCol; out vec4 frag;
uniform vec3 uColor, uEmissive; uniform float uMetal, uRough, uOpacity; uniform int uUnlit;
uniform sampler2D uBaseTex, uRoughTex, uMetalTex, uNormTex, uAoTex, uEmisTex; uniform int uMaps, uVColor, uWorldUV, uFlipG;
uniform vec2 uTiling; uniform float uNormalScale, uAoStr, uCutoff;
uniform int uSplat; uniform sampler2D uL0, uL1, uL2, uL3; uniform vec3 uLCol[4]; uniform float uLTile[4], uLRough[4]; uniform int uLHas[4];
)";
static const char* LIT_FS_MAIN = R"(
// normal maps without stored tangents: the surface's frame from how position and uv change across the pixel
vec3 perturb(vec3 N, vec3 p, vec2 uv, vec3 m){
  vec3 q0 = dFdx(p), q1 = dFdy(p); vec2 s0 = dFdx(uv), s1 = dFdy(uv); vec3 q1p = cross(q1, N), q0p = cross(N, q0);
  vec3 T = q1p * s0.x + q0p * s1.x, B = q1p * s0.y + q0p * s1.y; float det = max(dot(T, T), dot(B, B)); if(det <= 0.0) return N;
  float k = inversesqrt(det); return normalize(T * (m.x * k) + B * (m.y * k) + N * m.z); }
void main(){
  vec3 Ng = normalize(vNor); if(!gl_FrontFacing) Ng = -Ng; vec3 N = Ng;
  vec2 uv = vUv * uTiling;
  if(uWorldUV == 1){ vec3 a = abs(Ng); uv = (a.x >= a.y && a.x >= a.z ? vec2(vPos.z * sign(Ng.x), -vPos.y) : a.y >= a.z ? vec2(vPos.x, vPos.z) : vec2(vPos.x * -sign(Ng.z), -vPos.y)) * uTiling; }
  vec4 base = vec4(uColor, uOpacity);
  if((uMaps & 1) != 0) base *= texture(uBaseTex, uv);
  if(uVColor == 1) base *= vCol;
  if(uCutoff >= 0.0 && base.a < uCutoff) discard;
  float rough = uRough, metal = uMetal;
  if(uSplat == 1){   // landscape layers, blended by their painted weights, pictures laid on the world
    vec4 wv = vCol; float ws = wv.x + wv.y + wv.z + wv.w; wv = ws > 0.001 ? wv / ws : vec4(1.0, 0.0, 0.0, 0.0); vec2 wp = vPos.xz;
    vec3 c0 = uLCol[0] * (uLHas[0] == 1 ? texture(uL0, wp / uLTile[0]).rgb : vec3(1.0)), c1 = uLCol[1] * (uLHas[1] == 1 ? texture(uL1, wp / uLTile[1]).rgb : vec3(1.0));
    vec3 c2 = uLCol[2] * (uLHas[2] == 1 ? texture(uL2, wp / uLTile[2]).rgb : vec3(1.0)), c3 = uLCol[3] * (uLHas[3] == 1 ? texture(uL3, wp / uLTile[3]).rgb : vec3(1.0));
    base = vec4(c0 * wv.x + c1 * wv.y + c2 * wv.z + c3 * wv.w, 1.0); rough = dot(wv, vec4(uLRough[0], uLRough[1], uLRough[2], uLRough[3])); metal = 0.0; }
  if((uMaps & 2) != 0) rough *= texture(uRoughTex, uv).g;
  if((uMaps & 4) != 0) metal *= texture(uMetalTex, uv).b;
  if((uMaps & 8) != 0){ vec3 m = texture(uNormTex, uv).xyz * 2.0 - 1.0; if(uFlipG == 1) m.y = -m.y; m.xy *= uNormalScale; N = perturb(Ng, vPos, uv, normalize(m)); }
  if((uMaps & 16) != 0) gAo = mix(1.0, texture(uAoTex, uv).r, uAoStr);
  vec3 emis = uEmissive; if((uMaps & 32) != 0) emis *= texture(uEmisTex, uv).rgb;
  if(uMode==3){ frag = vec4(N*0.5+0.5,1.0); return; }
  vec3 col; float alpha = uCutoff >= 0.0 ? 1.0 : base.a;
  if(uUnlit==1 || uMode==1){ col = base.rgb + emis; if(uUnlit==1){ frag = vec4(col, alpha); return; } }
  else col = lighting(N, vPos, base.rgb, clamp(metal, 0.0, 1.0), clamp(rough, 0.03, 1.0), shadowAt(vPos, Ng)) + emis;
  frag = vec4(finish(col, vPos), alpha); })";
// the block world: chunk meshes with per-vertex colour, corner shadow (AO) and an optional texture atlas
static const char* VOX_VS = R"(#version 330 core
layout(location=0) in vec3 aPos; layout(location=1) in vec2 aUv; layout(location=2) in vec4 aCol; layout(location=3) in vec4 aNrm; layout(location=4) in uvec2 aInfo;
uniform mat4 uVP, uLightVP, uModel, uNrmM; uniform vec3 uOffset, uTint; uniform float uTime; uniform int uWater;
out vec3 vPos; out vec3 vNor; out vec4 vLight; out vec2 vUv; out vec4 vCol; flat out int vTile; flat out int vFlags;
void main(){ vec3 w = (uModel * vec4(aPos, 1.0)).xyz + uOffset; vNor = normalize(mat3(uNrmM) * aNrm.xyz);
  // liquid surfaces sit a little lower and move
  if(uWater==1 && vNor.y > 0.5) w.y += -0.05 + 0.018*sin(w.x*1.3 + uTime*1.4) + 0.014*sin(w.z*1.7 - uTime*1.1) + 0.008*sin((w.x+w.z)*3.1 + uTime*2.3);
  vPos = w; vLight = uLightVP * vec4(w,1.0); vUv = aUv; vCol = vec4(aCol.rgb * uTint, aCol.a); vTile = int(aInfo.x); vFlags = int(aInfo.y & 255u); gl_Position = uVP * vec4(w,1.0); })";
static const char* VOX_FS_HEAD = R"(#version 330 core
in vec3 vPos; in vec3 vNor; in vec4 vLight; in vec2 vUv; in vec4 vCol; flat in int vTile; flat in int vFlags; out vec4 frag;
uniform sampler2D uAtlas; uniform int uTiles, uA2C; uniform float uTime; float gCover = 1.0;
vec3 toLin(vec3 c){ return mix(c/12.92, pow((c+0.055)/1.055, vec3(2.4)), step(vec3(0.04045), c)); }
vec3 atlasColor(vec3 base){
  if(uTiles > 0 && vTile != 65535){
    vec2 ts = vec2(textureSize(uAtlas, 0)); float tw = ts.x / float(uTiles); vec2 cell = vec2(tw) / ts;
    vec2 t = vec2(float(vTile % uTiles), float(vTile / uTiles)); vec2 f = fract(vUv); f.y = 1.0 - f.y;
    vec4 tx = texture(uAtlas, (t + f) * cell);
    if((vFlags & 16) != 0){ gCover = clamp((tx.a - 0.5) / max(fwidth(tx.a), 1e-4) + 0.5, 0.0, 1.0); if(gCover <= (uA2C == 1 ? 0.01 : 0.5)) discard; if(uA2C == 0) gCover = 1.0; }
    base *= toLin(tx.rgb); }
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
  vec3 col = uMode==1 ? base * ao : lighting(N, vPos, base * ao, 0.0, 0.92, shadowAt(vPos, N));
  frag = vec4(finish(col + glow, vPos), gCover); })";
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
  vec3 col = uMode==1 ? base : lighting(N, vPos, base, 0.0, 0.07, shadowAt(vPos, normalize(vNor)));
  float a = mix(vCol.a, 0.96, fres);
  frag = vec4(finish(col, vPos), a); })";
static const char* FLAT_VS = R"(#version 330 core
layout(location=0) in vec3 aPos; uniform mat4 uModel, uVP; out float vDist; uniform vec3 uCam;
void main(){ vec4 w = uModel*vec4(aPos,1.0); vDist = length(w.xyz - uCam); gl_Position = uVP*w; })";
static const char* FLAT_FS = R"(#version 330 core
uniform vec4 uColor; uniform float uFade; in float vDist; out vec4 frag;
void main(){ float a = uColor.a; if(uFade>0.0) a *= clamp(1.0 - vDist/uFade, 0.0, 1.0); frag = vec4(uColor.rgb, a); })";
// shadows and clicking: skinned too; cut-out materials (leaves, fences) let the light through
static const char* DEPTH_VS_MAIN = R"(
out vec2 vUv; void main(){ vUv = aUv; gl_Position = uVP * skinMatrix() * vec4(aPos,1.0); })";
static const char* DEPTH_FS = R"(#version 330 core
in vec2 vUv; uniform sampler2D uBaseTex; uniform float uCutoff; uniform vec2 uTiling;
void main(){ if(uCutoff >= 0.0 && texture(uBaseTex, vUv * uTiling).a < uCutoff) discard; } )";
// shadows of the block world: cut-out pixels (leaves) let the light through
static const char* VDEPTH_VS = R"(#version 330 core
layout(location=0) in vec3 aPos; layout(location=1) in vec2 aUv; layout(location=4) in uvec2 aInfo; uniform mat4 uModel, uVP;
out vec2 vUv; flat out int vTile; flat out int vFlags;
void main(){ vUv = aUv; vTile = int(aInfo.x); vFlags = int(aInfo.y & 255u); gl_Position = uVP*uModel*vec4(aPos,1.0); })";
static const char* VDEPTH_FS = R"(#version 330 core
in vec2 vUv; flat in int vTile; flat in int vFlags; uniform sampler2D uAtlas; uniform int uTiles;
void main(){ if((vFlags & 16) != 0 && uTiles > 0 && vTile != 65535){
    vec2 ts = vec2(textureSize(uAtlas, 0)); float tw = ts.x / float(uTiles); vec2 cell = vec2(tw) / ts;
    vec2 t = vec2(float(vTile % uTiles), float(vTile / uTiles)); vec2 f = fract(vUv); f.y = 1.0 - f.y;
    if(texture(uAtlas, (t + f) * cell).a < 0.5) discard; } })";
static const char* ID_FS = R"(#version 330 core
in vec2 vUv; uniform vec4 uColor; out vec4 frag; void main(){ frag = uColor; })";


// ---- post passes (a triangle that covers the screen)
static const char* POST_VS = R"(#version 330 core
out vec2 vUv; void main(){ vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)); vUv = p; gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); })";
// the sky: from the horizon colour to the top colour, the sun's disc with a glow, the moon (opposite the sun), stars that come out at night
static const char* SKY_FS = R"(#version 330 core
in vec2 vUv; out vec4 frag; uniform mat4 uInvVP; uniform vec3 uTop, uHor, uSunC, uSunD; uniform float uSunR, uMoon, uStars;
float h3(vec3 p){ p = fract(p * 0.3183099 + 0.1); p *= 17.0; return fract(p.x * p.y * p.z * (p.x + p.y + p.z)); }
void main(){ vec4 a = uInvVP * vec4(vUv * 2.0 - 1.0, -1.0, 1.0), b = uInvVP * vec4(vUv * 2.0 - 1.0, 1.0, 1.0); vec3 d = normalize(b.xyz / b.w - a.xyz / a.w);
  float h = max(d.y, 0.0); vec3 c = mix(uHor, uTop, pow(h, 0.55));
  if (uStars > 0.0 && d.y > 0.05) { vec3 g = floor(d * 180.0); float r = h3(g); if (r > 0.985) { vec3 cc = (g + 0.5) / 180.0; float k = smoothstep(0.0035, 0.0, length(d - normalize(cc))); c += vec3(k * uStars * (0.6 + 0.4 * h3(g + 7.0))) * 2.0; } }
  float cs = dot(d, uSunD);
  if (dot(uSunC, uSunC) > 0.0) { float disc = smoothstep(cos(uSunR), cos(uSunR * 0.9), cs); float halo = pow(max(cs, 0.0), 220.0) * 0.6 + pow(max(cs, 0.0), 1400.0) * 0.8; c = mix(c, uSunC, disc) + uSunC * halo * 0.35; }
  if (uMoon > 0.0) { float m = smoothstep(cos(uSunR * 0.75), cos(uSunR * 0.68), -cs); c = mix(c, vec3(1.6, 1.7, 1.85), m * uMoon); }
  frag = vec4(c, 1.0); })";
// ambient occlusion from the depth buffer: 12 samples in the half sphere over each pixel (view space), turned per pixel
static const char* SSAO_FS = R"(#version 330 core
in vec2 vUv; out vec4 frag; uniform sampler2D uDepth; uniform mat4 uProj, uInvProj; uniform vec2 uTexel; uniform float uRadius;
vec3 vpos(vec2 uv){ float d = texture(uDepth, uv).r; vec4 p = uInvProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0); return p.xyz / p.w; }
const vec3 K[12] = vec3[](vec3(0.53,0.10,0.33), vec3(-0.31,0.47,0.25), vec3(0.11,-0.62,0.40), vec3(-0.44,-0.29,0.57), vec3(0.72,0.36,0.21), vec3(-0.15,0.18,0.86),
  vec3(0.27,-0.21,0.77), vec3(-0.71,0.09,0.42), vec3(0.06,0.81,0.37), vec3(0.42,-0.58,0.65), vec3(-0.52,-0.71,0.31), vec3(0.33,0.42,0.71));
void main(){
  vec2 uv0 = (floor(vUv / uTexel) + 0.5) * uTexel;   // on a full-size depth pixel, so its neighbours are really the next ones
  float d = texture(uDepth, uv0).r; if(d >= 1.0){ frag = vec4(1.0); return; }
  vec3 P = vpos(uv0);
  vec3 px = vpos(uv0 + vec2(uTexel.x, 0.0)) - P, nx = P - vpos(uv0 - vec2(uTexel.x, 0.0)), py = vpos(uv0 + vec2(0.0, uTexel.y)) - P, ny = P - vpos(uv0 - vec2(0.0, uTexel.y));
  vec3 N = normalize(cross(abs(px.z) < abs(nx.z) ? px : nx, abs(py.z) < abs(ny.z) ? py : ny)); if(dot(N, -P) < 0.0) N = -N;
  float a = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) * 6.2831853;
  vec3 r = vec3(cos(a), sin(a), 0.0); vec3 T = normalize(r - N * dot(r, N)); vec3 B = cross(N, T); mat3 TBN = mat3(T, B, N);
  float rad = uRadius * clamp(-P.z / 4.0, 0.35, 1.0), occ = 0.0, bias = 0.03 + 0.008 * -P.z;   // depth gets coarse far away: a bigger margin there
  for(int k = 0; k < 12; k++){ vec3 S = P + TBN * K[k] * rad * (0.3 + 0.7 * float(k) / 12.0);
    vec4 c = uProj * vec4(S, 1.0); vec2 uv = c.xy / c.w * 0.5 + 0.5; if(uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) continue;
    float z = vpos(uv).z; float range = smoothstep(0.0, 1.0, rad / max(abs(P.z - z), 1e-3));
    occ += (z >= S.z + bias ? 1.0 : 0.0) * range; }
  float ao = pow(clamp(1.0 - occ / 12.0, 0.0, 1.0), 1.4);
  frag = vec4(mix(ao, 1.0, smoothstep(25.0, 50.0, -P.z))); })";
static const char* AOBLUR_FS = R"(#version 330 core
in vec2 vUv; out vec4 frag; uniform sampler2D uAo, uDepth; uniform mat4 uInvProj; uniform vec2 uTexel;
float vz(vec2 uv){ float d = texture(uDepth, uv).r; vec4 p = uInvProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0); return p.z / p.w; }
void main(){ float z0 = vz(vUv), s = 0.0, w = 0.0, tol = 0.08 + 0.04 * abs(z0);   // depth-aware: edges stay sharp, flat ground and slopes are smoothed
  for(int x = -2; x <= 2; x++) for(int y = -2; y <= 2; y++){ vec2 uv = vUv + vec2(x, y) * uTexel; float k = exp(-abs(vz(uv) - z0) / tol); s += texture(uAo, uv).r * k; w += k; }
  frag = vec4(s / w); })";
static const char* BLOOMPRE_FS = R"(#version 330 core
in vec2 vUv; out vec4 frag; uniform sampler2D uSrc, uDepth; uniform vec2 uTexel; uniform float uExposure, uThreshold;
vec3 tap(vec2 o){ vec2 uv = vUv + uTexel * o; return texture(uDepth, uv).r < 1.0 ? texture(uSrc, uv).rgb : vec3(0.0); }   // the sky (its own colour, exactly) doesn't bloom
void main(){ vec3 c = (tap(vec2(-1,-1)) + tap(vec2(1,-1)) + tap(vec2(-1,1)) + tap(vec2(1,1))) * 0.25 * uExposure;
  float br = max(c.r, max(c.g, c.b)), knee = uThreshold * 0.5, soft = clamp(br - uThreshold + knee, 0.0, 2.0 * knee); soft = soft * soft / (4.0 * knee + 1e-4);
  c *= max(soft, br - uThreshold) / max(br, 1e-4); frag = vec4(min(c, vec3(64.0)), 1.0); })";
static const char* BLOOMDOWN_FS = R"(#version 330 core
in vec2 vUv; out vec4 frag; uniform sampler2D uSrc; uniform vec2 uTexel;
void main(){ vec3 c = texture(uSrc, vUv).rgb * 4.0;
  c += texture(uSrc, vUv + uTexel * vec2(-1,-1)).rgb + texture(uSrc, vUv + uTexel * vec2(1,-1)).rgb + texture(uSrc, vUv + uTexel * vec2(-1,1)).rgb + texture(uSrc, vUv + uTexel * vec2(1,1)).rgb;
  frag = vec4(c / 8.0, 1.0); })";
static const char* BLOOMUP_FS = R"(#version 330 core
in vec2 vUv; out vec4 frag; uniform sampler2D uSrc; uniform vec2 uTexel;
void main(){ vec3 c = texture(uSrc, vUv).rgb * 4.0;
  c += (texture(uSrc, vUv + uTexel * vec2(-1,0)).rgb + texture(uSrc, vUv + uTexel * vec2(1,0)).rgb + texture(uSrc, vUv + uTexel * vec2(0,-1)).rgb + texture(uSrc, vUv + uTexel * vec2(0,1)).rgb) * 2.0;
  c += texture(uSrc, vUv + uTexel * vec2(-1,-1)).rgb + texture(uSrc, vUv + uTexel * vec2(1,-1)).rgb + texture(uSrc, vUv + uTexel * vec2(-1,1)).rgb + texture(uSrc, vUv + uTexel * vec2(1,1)).rgb;
  frag = vec4(c / 16.0, 1.0); })";
// to the screen: AO, bloom, exposure, the same ACES tonemapper as before (games look as they did, with more depth), sRGB
// camera effects (World Settings > Post Process, world.setPostProcess): chromatic aberration, VHS tape wobble and colour bleed,
// temperature, tint, saturation, contrast, scanlines, film grain, vignette
static const char* COMPOSE_FS = R"(#version 330 core
in vec2 vUv; out vec4 frag; uniform sampler2D uHdr, uDepth, uAo, uBloom; uniform int uOut, uHasAo, uHasBloom, uPost; uniform float uExposure, uAoAmount, uBloomAmount;
uniform float uTime, uVignette, uGrain, uChroma, uScan, uVhs, uSat, uContrast, uTemp; uniform vec3 uTint; uniform vec2 uRes;
float hash(vec2 p){ return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
vec3 rrt(vec3 v){ vec3 a = v*(v+0.0245786)-0.000090537; vec3 b = v*(0.983729*v+0.4329510)+0.238081; return a/b; }
vec3 aces(vec3 c){ const mat3 I = mat3(vec3(0.59719,0.07600,0.02840), vec3(0.35458,0.90834,0.13383), vec3(0.04823,0.01566,0.83777));
  const mat3 O = mat3(vec3(1.60475,-0.10208,-0.00327), vec3(-0.53108,1.10813,-0.07276), vec3(-0.07367,-0.00605,1.07602));
  c /= 0.6; c = I*c; c = rrt(c); c = O*c; return clamp(c,0.0,1.0); }
vec3 srgb(vec3 c){ return mix(c*12.92, 1.055*pow(c, vec3(1.0/2.4))-0.055, step(vec3(0.0031308), c)); }
void main(){ float d = texture(uDepth, vUv).r; vec2 uv = vUv;
  if(uPost == 1 && uVhs > 0.0){   // a worn tape: lines wobble, a few jump, a tracking band rolls down
    float row = floor(uv.y * uRes.y / 2.0), fr = floor(uTime * 24.0), n = hash(vec2(row, fr));
    uv.x += (sin(uv.y * 38.0 + uTime * 2.7) * 0.0012 + sin(uv.y * 410.0 + uTime * 31.0) * 0.0004 + (n > 0.992 ? (n - 0.992) * 3.0 : 0.0)) * uVhs;
    float band = 1.0 - smoothstep(0.0, 0.035, abs(fract(uv.y + uTime * 0.06) - 0.5)); uv.x += band * 0.004 * uVhs * (hash(vec2(fr, 7.0)) - 0.5); }
  vec3 c = texture(uHdr, uv).rgb;
  if(uPost == 1 && (uChroma > 0.0 || uVhs > 0.0)){ vec2 off = (uv - 0.5) * uChroma * 0.012 + vec2(uVhs * 0.0028, 0.0);
    c.r = texture(uHdr, uv + off).r; c.b = texture(uHdr, uv - off).b; }
  if(uOut == 0){
    if(uHasAo == 1 && d < 1.0) c *= mix(1.0, texture(uAo, uv).r, uAoAmount);
    if(uHasBloom == 1) c += texture(uBloom, uv).rgb * uBloomAmount / max(uExposure, 1e-3);
    c = srgb(aces(c * uExposure));
    if(uPost == 1){
      c *= vec3(1.0 + 0.12 * uTemp, 1.0 + 0.02 * uTemp, 1.0 - 0.14 * uTemp) * uTint;
      float l = dot(c, vec3(0.2126, 0.7152, 0.0722)); c = mix(vec3(l), c, uSat); c = (c - 0.5) * uContrast + 0.5;
      if(uVhs > 0.0){ c = mix(c, c * vec3(1.04, 0.96, 1.07) * 0.92 + 0.045, uVhs); c += (hash(vec2(floor(uv.y * uRes.y), floor(uTime * 30.0))) - 0.5) * 0.05 * uVhs; }
      if(uScan > 0.0) c *= 1.0 - uScan * 0.32 * (0.5 + 0.5 * sin(gl_FragCoord.y * 3.14159));
      if(uGrain > 0.0){ float g = hash(gl_FragCoord.xy + fract(uTime * 7.31) * 517.0) - 0.5; c += g * uGrain * 0.22 * (1.0 - 0.6 * clamp(l, 0.0, 1.0)); }
      if(uVignette > 0.0){ vec2 q = (vUv - 0.5) * vec2(uRes.x / uRes.y, 1.0) / (0.5 * sqrt(uRes.x * uRes.x / (uRes.y * uRes.y) + 1.0)); c *= mix(1.0, smoothstep(1.15, 0.25, length(q)), uVignette); }
      c = clamp(c, 0.0, 1.0); } }
  else if(uOut == 1) c = srgb(c);
  c += (fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) / 255.0;   // dither: no bands in the sky
  frag = vec4(c, 1.0); gl_FragDepth = d; })";
static const char* TEXT_VS = R"(#version 330 core
layout(location=0) in vec3 aPos; layout(location=1) in vec2 aUv; uniform mat4 uModel, uVP; out vec2 vUv;
void main(){ vUv = aUv; gl_Position = uVP*uModel*vec4(aPos,1.0); })";
static const char* TEXT_FS = R"(#version 330 core
in vec2 vUv; uniform sampler2D uTex; uniform vec4 uColor; uniform int uId; out vec4 frag;
void main(){ float a = texture(uTex, vUv).a; if(uId==1){ if(a<0.3) discard; frag = uColor; return; } frag = vec4(uColor.rgb, uColor.a*a); })";

static bool inView(const glm::mat4& vp, const DrawItem& it);
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
  std::string litVs = std::string(SKIN_LIB) + LIT_VS_MAIN, depthVs = std::string(SKIN_LIB) + DEPTH_VS_MAIN;
  lit_ = compile(litVs.c_str(), litFs.c_str(), "lit"); vox_ = compile(VOX_VS, voxFs.c_str(), "voxels"); wat_ = compile(VOX_VS, watFs.c_str(), "water"); flat_ = compile(FLAT_VS, FLAT_FS, "flat"); depth_ = compile(depthVs.c_str(), DEPTH_FS, "depth"); vdepth_ = compile(VDEPTH_VS, VDEPTH_FS, "voxel depth"); idp_ = compile(depthVs.c_str(), ID_FS, "id");
  glGenTextures(1, &boneTex_); glBindTexture(GL_TEXTURE_2D, boneTex_); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  { float id[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 4, 1, 0, GL_RGBA, GL_FLOAT, id); } glBindTexture(GL_TEXTURE_2D, 0); text_ = compile(TEXT_VS, TEXT_FS, "text");
  ssao_ = compile(POST_VS, SSAO_FS, "ssao"); aoBlur_ = compile(POST_VS, AOBLUR_FS, "ao blur"); bloomPre_ = compile(POST_VS, BLOOMPRE_FS, "bloom"); bloomDown_ = compile(POST_VS, BLOOMDOWN_FS, "bloom down"); bloomUp_ = compile(POST_VS, BLOOMUP_FS, "bloom up"); compose_ = compile(POST_VS, COMPOSE_FS, "compose"); sky_ = compile(POST_VS, SKY_FS, "sky");
  glGenVertexArrays(1, &postVao_); glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
  glGenVertexArrays(1, &textVao_); glBindVertexArray(textVao_); glGenBuffers(1, &textVbo_); glBindBuffer(GL_ARRAY_BUFFER, textVbo_);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 20, (void*)0); glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 20, (void*)12); glBindVertexArray(0);
  // shadow map
  glGenFramebuffers(1, &shadowFbo_); glGenTextures(1, &shadowTex_); glBindTexture(GL_TEXTURE_2D, shadowTex_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, SHADOW_SIZE * 2, SHADOW_SIZE * 2, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);   // 2 x 2 cascades
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
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
void Renderer::shutdown() { glDeleteProgram(vox_); glDeleteProgram(wat_); glDeleteProgram(text_); glDeleteVertexArrays(1, &textVao_); glDeleteBuffers(1, &textVbo_); glDeleteProgram(lit_); glDeleteProgram(flat_); glDeleteProgram(depth_); glDeleteProgram(vdepth_); glDeleteProgram(idp_); glDeleteTextures(1, &boneTex_); for (auto& [k, pb] : probes_) glDeleteTextures(1, &pb.tex); probes_.clear(); if (capFbo_) glDeleteFramebuffers(1, &capFbo_); freeTarget(capRT_); glDeleteFramebuffers(1, &shadowFbo_); glDeleteTextures(1, &shadowTex_);
  for (unsigned q : {ssao_, aoBlur_, bloomPre_, bloomDown_, bloomUp_, compose_, sky_}) glDeleteProgram(q); glDeleteVertexArrays(1, &postVao_); freePost();
  for (GpuMesh* m : {&wireBox_, &wireCapsule_, &arrow_, &axes_, &wireCam_, &grid_}) destroy(*m); releasePrimitives(); releaseAssets(); releaseLandscapes(); }

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

/* ------------------------------------------------------------------ the frame
   1. sun shadows: up to 4 cascades fitted to slices of the view (texel-snapped, so they don't shimmer)
   2. the scene in linear HDR light, with MSAA (cut-out leaves get smooth edges through alpha to coverage)
   3. resolve, ambient occlusion from depth (half size, blurred), bloom (6 levels down and up)
   4. tonemap (ACES), exposure, sRGB, a little dither against banding; the depth goes with it
   5. on top: 3D text, the editor's lines and grid (display colours, tested against the scene's depth) */
static float acesC(float v) { float a = v * (v + 0.0245786f) - 0.000090537f, b = v * (0.983729f * v + 0.4329510f) + 0.238081f; return a / b; }
static glm::vec3 acesV(glm::vec3 c) {   // the shader's aces(), on the CPU
  const glm::mat3 I(glm::vec3(0.59719f, 0.07600f, 0.02840f), glm::vec3(0.35458f, 0.90834f, 0.13383f), glm::vec3(0.04823f, 0.01566f, 0.83777f));
  const glm::mat3 O(glm::vec3(1.60475f, -0.10208f, -0.00327f), glm::vec3(-0.53108f, 1.10813f, -0.07276f), glm::vec3(-0.07367f, -0.00605f, 1.07602f));
  c /= 0.6f; c = I * c; c = glm::vec3(acesC(c.x), acesC(c.y), acesC(c.z)); c = O * c; return glm::clamp(c, 0.f, 1.f); }
// the linear light that shows as a given screen colour after exposure and the tonemapper (the sky, the fog)
static glm::vec3 untonemap(glm::vec3 display, float exposure) {
  glm::vec3 t = lin(glm::clamp(display, 0.f, 0.995f)), L = t;
  for (int it = 0; it < 40; it++) { glm::vec3 d = acesV(L * std::max(exposure, 1e-3f)); for (int k = 0; k < 3; k++) { float r = t[k] / std::max(d[k], 1e-5f); L[k] = std::clamp(L[k] * std::pow(r, 0.85f), 0.f, 4096.f); } }
  return L;
}

void Renderer::shadowPass(const Frame& f, const Camera& cam, bool& has) {
  has = false; cascN_ = 0; const LightItem* sun = nullptr; for (auto& l : f.lights) if (l.type == 0) { sun = &l; break; }
  if (!sun || !sun->shadows || f.viewMode != VM_Lit || quality.shadowDistance <= 0) return;   // shadowDistance 0: no sun shadows (settings)
  if (f.voxels.empty() && f.draws.empty()) return;
  glm::vec3 d = glm::normalize(sun->dir); glm::vec3 up = std::abs(d.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
  glm::mat4 inv = glm::inverse(cam.proj * cam.view); glm::vec3 fwd = -glm::vec3(glm::inverse(cam.view)[2]);
  // the four corner rays of the view, scaled so that one unit = one metre along the view direction
  glm::vec3 ray[4]; { const float X[4] = {-1, 1, 1, -1}, Y[4] = {-1, -1, 1, 1};
    for (int k = 0; k < 4; k++) { glm::vec4 a = inv * glm::vec4(X[k], Y[k], -1, 1), b = inv * glm::vec4(X[k], Y[k], 1, 1); glm::vec3 r = glm::vec3(b) / b.w - glm::vec3(a) / a.w; ray[k] = r / std::max(1e-5f, glm::dot(r, fwd)); } }
  float farD = std::max(20.f, quality.shadowDistance); int n = 4;
  float splits[5] = {0.05f, 0, 0, 0, farD};
  for (int i = 1; i < n; i++) { float t = i / (float)n; splits[i] = 0.75f * (0.05f * std::pow(farD / 0.05f, t)) + 0.25f * (0.05f + (farD - 0.05f) * t); }   // between log and even slices
  glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_); glViewport(0, 0, SHADOW_SIZE * 2, SHADOW_SIZE * 2); glClear(GL_DEPTH_BUFFER_BIT); glEnable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
  glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(1.6f, 2.f); glEnable(GL_SCISSOR_TEST);
  for (int i = 0; i < n; i++) {
    float a = splits[i], b = splits[i + 1]; glm::vec3 pts[8]; glm::vec3 c(0);
    for (int k = 0; k < 4; k++) { pts[k] = cam.pos + ray[k] * a; pts[k + 4] = cam.pos + ray[k] * b; } for (auto& q : pts) c += q / 8.f;
    float r = 0; for (auto& q : pts) r = std::max(r, glm::length(q - c)); r = std::ceil(r * 8.f) / 8.f;
    glm::mat4 view = glm::lookAt(c - d * r, c, up);
    // the depth range: everything toward the sun that can throw a shadow into this slice
    float zNear = 0, zFar = 2 * r;
    for (auto& v : f.voxels) { if (!v.castShadow) continue; glm::vec3 lo(1e9f), hi(-1e9f);
      for (int k = 0; k < 8; k++) { glm::vec3 q = glm::vec3(view * glm::vec4((k & 1) ? v.mx.x : v.mn.x, (k & 2) ? v.mx.y : v.mn.y, (k & 4) ? v.mx.z : v.mn.z, 1)); lo = glm::min(lo, q); hi = glm::max(hi, q); }
      if (hi.x < -r || lo.x > r || hi.y < -r || lo.y > r) continue; zNear = std::min(zNear, -hi.z - 1.f); zFar = std::max(zFar, -lo.z + 1.f); }
    zNear = std::max(zNear, -400.f); zFar = std::min(zFar, 600.f);
    glm::mat4 proj = glm::ortho(-r, r, -r, r, zNear, zFar), vp = proj * view;
    // snap to whole shadow texels so moving the camera doesn't make the edges crawl
    glm::vec4 o = vp * glm::vec4(0, 0, 0, 1); o *= SHADOW_SIZE / 2.f; glm::vec4 ro = glm::round(o); glm::vec4 off = (ro - o) * (2.f / SHADOW_SIZE); off.z = 0; off.w = 0;
    proj[3] += off; vp = proj * view;
    casc_[i] = vp; split_[i] = b; texel_[i] = 2 * r / SHADOW_SIZE;
    int vx = (i % 2) * SHADOW_SIZE, vy = (i / 2) * SHADOW_SIZE; glViewport(vx, vy, SHADOW_SIZE, SHADOW_SIZE); glScissor(vx, vy, SHADOW_SIZE, SHADOW_SIZE);
    auto inBox = [&](const glm::vec3& mn, const glm::vec3& mx) { glm::vec3 lo(1e9f), hi(-1e9f);
      for (int k = 0; k < 8; k++) { glm::vec3 q = glm::vec3(view * glm::vec4((k & 1) ? mx.x : mn.x, (k & 2) ? mx.y : mn.y, (k & 4) ? mx.z : mn.z, 1)); lo = glm::min(lo, q); hi = glm::max(hi, q); }
      return !(hi.x < -r || lo.x > r || hi.y < -r || lo.y > r); };
    glUseProgram(depth_); M4(depth_, "uVP", vp); glUniform1i(U(depth_, "uBaseTex"), 1); glUniform1i(U(depth_, "uBones"), 7);
    for (auto& it : f.draws) { if (!it.castShadow || !it.mesh || !it.mesh->vao || it.opacity < 0.5f || it.pickOnly || it.unlit) continue;
      if (!it.bones) { glm::vec3 lo(1e9f), hi(-1e9f); const glm::vec3 &a = it.mesh->bmin, &b = it.mesh->bmax;   // only what this cascade covers
        for (int k = 0; k < 8; k++) { glm::vec3 w = glm::vec3(it.model * glm::vec4((k & 1) ? b.x : a.x, (k & 2) ? b.y : a.y, (k & 4) ? b.z : a.z, 1)); lo = glm::min(lo, w); hi = glm::max(hi, w); }
        if (a.x <= b.x && !inBox(lo, hi)) continue; }
      M4(depth_, "uModel", it.model); bindBones(depth_, it);
      bool cut = it.cutoff >= 0 && it.tex[0]; glUniform1f(U(depth_, "uCutoff"), cut ? it.cutoff : -1.f); glUniform2f(U(depth_, "uTiling"), it.tiling.x, it.tiling.y);
      if (cut) { glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, it.tex[0]); glActiveTexture(GL_TEXTURE0); }
      glBindVertexArray(it.mesh->vao); glDrawElements(GL_TRIANGLES, it.mesh->count, GL_UNSIGNED_INT, nullptr); }
    if (!f.voxels.empty()) { glUseProgram(vdepth_); M4(vdepth_, "uVP", vp);
      glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, f.voxelAtlas); glUniform1i(U(vdepth_, "uAtlas"), 1); glUniform1i(U(vdepth_, "uTiles"), f.voxelAtlas ? f.voxelTiles : 0); glActiveTexture(GL_TEXTURE0);
      for (auto& v : f.voxels) { if (!v.castShadow || !inBox(v.mn, v.mx)) continue; M4(vdepth_, "uModel", glm::translate(glm::mat4(1), v.offset) * v.model); glBindVertexArray(v.vao); glDrawElements(GL_TRIANGLES, v.count, GL_UNSIGNED_INT, nullptr); } }
  }
  glDisable(GL_SCISSOR_TEST); glDisable(GL_POLYGON_OFFSET_FILL); cascN_ = n; has = true;
}

void Renderer::freePost() {
  Post& P = post_;
  if (P.msFbo) { glDeleteFramebuffers(1, &P.msFbo); glDeleteRenderbuffers(1, &P.msColor); glDeleteRenderbuffers(1, &P.msDepth); }
  if (P.hdrFbo) { glDeleteFramebuffers(1, &P.hdrFbo); glDeleteTextures(1, &P.hdrTex); glDeleteTextures(1, &P.depthTex); }
  for (int i = 0; i < 2; i++) if (P.aoFbo[i]) { glDeleteFramebuffers(1, &P.aoFbo[i]); glDeleteTextures(1, &P.aoTex[i]); }
  for (int i = 0; i < P.bloomN; i++) { glDeleteFramebuffers(1, &P.bloomFbo[i]); glDeleteTextures(1, &P.bloomTex[i]); }
  P = Post();
}
void Renderer::ensurePost(int w, int h) {
  int want = std::clamp(quality.msaa, 0, 8); if (want == 1) want = 0;
  if (post_.hdrFbo && post_.w == w && post_.h == h && post_.want == want) return;
  freePost(); Post& P = post_; P.w = w; P.h = h; P.want = want;
  auto tex = [](unsigned& t, GLint fmt, GLenum f2, GLenum type, int tw, int th, GLint filter) { glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t); glTexImage2D(GL_TEXTURE_2D, 0, fmt, tw, th, 0, f2, type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); };
  // resolved HDR colour + depth (what the post passes read)
  glGenFramebuffers(1, &P.hdrFbo); glBindFramebuffer(GL_FRAMEBUFFER, P.hdrFbo);
  tex(P.hdrTex, GL_RGBA16F, GL_RGBA, GL_FLOAT, w, h, GL_LINEAR); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, P.hdrTex, 0);
  tex(P.depthTex, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_FLOAT, w, h, GL_NEAREST); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, P.depthTex, 0);
  // the multisampled scene (if the GPU can)
  int maxS = 0; glGetIntegerv(GL_MAX_SAMPLES, &maxS); P.samples = std::min(want, maxS);
  if (P.samples >= 2) {
    glGenFramebuffers(1, &P.msFbo); glBindFramebuffer(GL_FRAMEBUFFER, P.msFbo);
    glGenRenderbuffers(1, &P.msColor); glBindRenderbuffer(GL_RENDERBUFFER, P.msColor); glRenderbufferStorageMultisample(GL_RENDERBUFFER, P.samples, GL_RGBA16F, w, h); glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, P.msColor);
    glGenRenderbuffers(1, &P.msDepth); glBindRenderbuffer(GL_RENDERBUFFER, P.msDepth); glRenderbufferStorageMultisample(GL_RENDERBUFFER, P.samples, GL_DEPTH_COMPONENT24, w, h); glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, P.msDepth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { glDeleteFramebuffers(1, &P.msFbo); glDeleteRenderbuffers(1, &P.msColor); glDeleteRenderbuffers(1, &P.msDepth); P.msFbo = P.msColor = P.msDepth = 0; P.samples = 0; }
  } else P.samples = 0;
  // ambient occlusion (half size) and bloom levels
  int hw = std::max(1, w / 2), hh = std::max(1, h / 2);
  for (int i = 0; i < 2; i++) { glGenFramebuffers(1, &P.aoFbo[i]); glBindFramebuffer(GL_FRAMEBUFFER, P.aoFbo[i]); tex(P.aoTex[i], GL_R8, GL_RED, GL_UNSIGNED_BYTE, hw, hh, GL_LINEAR); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, P.aoTex[i], 0); }
  int bw = hw, bh = hh; P.bloomN = 0;
  for (int i = 0; i < 6 && bw >= 2 && bh >= 2; i++) { glGenFramebuffers(1, &P.bloomFbo[i]); glBindFramebuffer(GL_FRAMEBUFFER, P.bloomFbo[i]); tex(P.bloomTex[i], GL_RGBA16F, GL_RGBA, GL_FLOAT, bw, bh, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, P.bloomTex[i], 0); P.bw[i] = bw; P.bh[i] = bh; P.bloomN++; bw /= 2; bh /= 2; }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

std::string Renderer::probeKey(const ProbeItem& p) { char b[96]; std::snprintf(b, sizeof b, "%.2f,%.2f,%.2f,%.2f", p.pos.x, p.pos.y, p.pos.z, p.radius); return b; }
// one capture a frame: the scene from the capture's place in 6 directions into a cube picture (the editor's helpers left out)
void Renderer::captureProbes(const Frame& f) {
  const ProbeItem* todo = nullptr; for (auto& pr : f.probes) { auto it = probes_.find(probeKey(pr)); if (it == probes_.end() || !it->second.ready || it->second.version != f.probeVersion) { todo = &pr; break; } }
  if (!todo) return;
  if (probes_.size() > 64) { for (auto& [k, pb] : probes_) glDeleteTextures(1, &pb.tex); probes_.clear(); }
  const int S = 128; Probe& pb = probes_[probeKey(*todo)];
  if (!pb.tex) { glGenTextures(1, &pb.tex); glBindTexture(GL_TEXTURE_CUBE_MAP, pb.tex);
    for (int i = 0; i < 6; i++) glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA8, S, S, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    for (GLenum w : {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_WRAP_R}) glTexParameteri(GL_TEXTURE_CUBE_MAP, w, GL_CLAMP_TO_EDGE); }
  if (!capFbo_) glGenFramebuffers(1, &capFbo_);
  resizeTarget(capRT_, S, S);
  Frame g = f; g.probes.clear(); g.lines.clear(); g.texts.clear(); g.grid = false; g.viewMode = VM_Lit;
  g.draws.erase(std::remove_if(g.draws.begin(), g.draws.end(), [](const DrawItem& d) { return d.pickOnly || (d.unlit && !d.castShadow); }), g.draws.end());
  static const glm::vec3 DIR[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}, UP[6] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
  Quality keepQ = quality; quality.msaa = 0; quality.ssao = false; int keepDc = drawCalls, keepTr = triangles; capturing_ = true;
  for (int i = 0; i < 6; i++) {
    Camera c; c.pos = todo->pos; c.view = glm::lookAt(c.pos, c.pos + DIR[i], UP[i]); c.proj = glm::perspective(glm::radians(90.f), 1.f, 0.05f, 2000.f);
    render(g, c, S, S, &capRT_);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, capRT_.fbo); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, capFbo_);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, pb.tex, 0);
    glBlitFramebuffer(0, 0, S, S, 0, 0, S, S, GL_COLOR_BUFFER_BIT, GL_NEAREST); }
  glBindFramebuffer(GL_FRAMEBUFFER, 0); glBindTexture(GL_TEXTURE_CUBE_MAP, pb.tex); glGenerateMipmap(GL_TEXTURE_CUBE_MAP); glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
  capturing_ = false; quality = keepQ; drawCalls = keepDc; triangles = keepTr; pb.ready = true; pb.version = f.probeVersion;
}
static bool finiteM(const glm::mat4& m) { for (int c = 0; c < 4; c++) for (int r = 0; r < 4; r++) if (!std::isfinite(m[c][r])) return false; return true; }
void Renderer::render(const Frame& f, const Camera& cam, int w, int h, const RenderTarget* rt) {
  if (!finiteM(cam.view) || !finiteM(cam.proj) || !std::isfinite(cam.pos.x + cam.pos.y + cam.pos.z)) {   // a broken camera: no picture this frame (some drivers stop on NaN)
    static bool told = false; if (!told) { told = true; logMsg("warning: renderer: the camera's view isn't a number this frame (skipped)", LogKind::Warn); }
    glBindFramebuffer(GL_FRAMEBUFFER, rt ? rt->fbo : 0); glViewport(0, 0, std::max(w, 1), std::max(h, 1)); glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); return; }
  if (!capturing_ && !f.probes.empty() && f.viewMode == VM_Lit) captureProbes(f);
  drawCalls = triangles = 0; w = std::max(w, 1); h = std::max(h, 1);
  unsigned target = rt ? rt->fbo : 0;
  bool scene = f.viewMode != VM_Wire;
  bool hasShadow = false; if (scene) shadowPass(f, cam, hasShadow);
  glm::mat4 vp = cam.proj * cam.view;
  if (scene) {
    ensurePost(w, h); Post& P = post_;
    float expo = f.world.exposure;
    glm::vec3 bg = f.viewMode == VM_Normals ? glm::vec3(0.12f) : f.world.sky;   // the sky colour is a screen colour (like three.js scene.background)
    glm::vec3 bgLin = f.viewMode == VM_Lit ? untonemap(bg, expo) : f.viewMode == VM_Unlit ? lin(bg) : bg;
    glm::vec3 fogLin = f.viewMode == VM_Lit ? untonemap(f.world.fogColor, expo) : lin(f.world.fogColor);
    glBindFramebuffer(GL_FRAMEBUFFER, P.samples ? P.msFbo : P.hdrFbo); glViewport(0, 0, w, h);
    glClearColor(bgLin.r, bgLin.g, bgLin.b, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (f.viewMode == VM_Lit && f.world.skyTop.x >= 0 && sky_) {   // the sky (scripts): drawn first, everything else covers it
      glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_BLEND); glUseProgram(sky_); glBindVertexArray(postVao_);
      bool under = glm::distance(f.world.sky, f.world.fogColor) < 1e-4f && f.world.fogDensity > 0.05f;   // under water: just the water colour
      M4(sky_, "uInvVP", glm::inverse(vp)); V3(sky_, "uTop", under ? bgLin : untonemap(f.world.skyTop, expo)); V3(sky_, "uHor", bgLin);
      V3(sky_, "uSunC", under ? glm::vec3(0) : lin(f.world.sunDisc) * 4.f); V3(sky_, "uSunD", glm::normalize(f.world.sunDiscDir)); glUniform1f(U(sky_, "uSunR"), f.world.sunSize);
      glUniform1f(U(sky_, "uMoon"), under ? 0.f : f.world.moon); glUniform1f(U(sky_, "uStars"), under ? 0.f : f.world.stars);
      glDrawArrays(GL_TRIANGLES, 0, 3); glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST); }
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_TRUE); glDisable(GL_CULL_FACE); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glm::vec3 camFwd = -glm::vec3(glm::inverse(cam.view)[2]);
    auto common = [&](unsigned p) {
      glUseProgram(p); M4(p, "uVP", vp); V3(p, "uCam", cam.pos); V3(p, "uCamFwd", camFwd);
      // linear-ish sky light colours from the display colours
      V3(p, "uSky", lin(f.world.skyLight)); V3(p, "uGround", lin(f.world.groundLight));
      glUniform1f(U(p, "uAmbient"), f.viewMode == VM_Unlit ? 1.f : f.world.ambient); glUniform1f(U(p, "uExposure"), expo);
      glUniform1i(U(p, "uFog"), f.fog ? 1 : 0); V3(p, "uFogColor", f.world.fogColor); V3(p, "uFogLin", fogLin); glUniform1f(U(p, "uFogDensity"), f.world.fogDensity);
      glUniform1i(U(p, "uMode"), f.viewMode); glUniform1i(U(p, "uHasShadow"), hasShadow ? 1 : 0);
      glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, shadowTex_); glUniform1i(U(p, "uShadow"), 0); glUniform1i(U(p, "uEnv"), 8); glUniform1i(U(p, "uHasEnv"), 0);
      glUniform1i(U(p, "uCascades"), cascN_); if (cascN_) { glUniformMatrix4fv(U(p, "uCasc"), cascN_, GL_FALSE, glm::value_ptr(casc_[0]));
        glUniform4f(U(p, "uSplit"), split_[0], split_[1], split_[2], split_[3]); glUniform4f(U(p, "uTexel"), texel_[0], texel_[1], texel_[2], texel_[3]); }
      int n = 0, sun = 0; int types[8] = {0}; glm::vec3 pos[8], dir[8], col[8]; float in[8], rg[8], cs[8];
      for (auto& l : f.lights) {
        if (l.type == 0 && !sun) { sun = 1; V3(p, "uSunDir", glm::normalize(l.dir)); V3(p, "uSunColor", lin(l.color)); glUniform1f(U(p, "uSunInt"), l.intensity); continue; }
        if (l.type == 0 || n >= 8) continue; types[n] = l.type; pos[n] = l.pos; dir[n] = l.dir; col[n] = lin(l.color); in[n] = l.intensity; rg[n] = l.range; cs[n] = l.cosOuter; n++;
      }
      glUniform1i(U(p, "uHasSun"), sun); glUniform1i(U(p, "uNumLights"), n);
      if (n) { glUniform1iv(U(p, "uLType"), n, types); glUniform3fv(U(p, "uLPos"), n, &pos[0].x); glUniform3fv(U(p, "uLDir"), n, &dir[0].x); glUniform3fv(U(p, "uLColor"), n, &col[0].x);
        glUniform1fv(U(p, "uLInt"), n, in); glUniform1fv(U(p, "uLRange"), n, rg); glUniform1fv(U(p, "uLCos"), n, cs); }
    };
    // a model's part: its matrix (and the one for its normals) and tint; chunks: none
    auto voxModel = [&](unsigned p, const VoxelDraw& v) { M4(p, "uModel", v.model); M4(p, "uNrmM", glm::mat4(glm::transpose(glm::inverse(glm::mat3(v.model))))); V3(p, "uTint", v.tint); };
    auto voxCommon = [&](unsigned p, int water) { common(p); glUniform1f(U(p, "uTime"), f.time); glUniform1i(U(p, "uWater"), water); glUniform1i(U(p, "uA2C"), P.samples ? 1 : 0);
      glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, f.voxelAtlas); glUniform1i(U(p, "uAtlas"), 1); glUniform1i(U(p, "uTiles"), f.voxelAtlas ? f.voxelTiles : 0); glActiveTexture(GL_TEXTURE0); };
    // the block world first (opaque, large): no blending; with MSAA the cut-out edges cover part of each pixel
    if (!f.voxels.empty()) { unsigned p = vox_; voxCommon(p, 0); glDisable(GL_BLEND); if (P.samples) glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
      for (auto& v : f.voxels) { if (!v.visible) continue; V3(p, "uOffset", v.offset); voxModel(p, v); glBindVertexArray(v.vao); glDrawElements(GL_TRIANGLES, v.count, GL_UNSIGNED_INT, nullptr); drawCalls++; triangles += v.count / 3; }
      glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE); glEnable(GL_BLEND); }
    unsigned p = lit_;
    auto meshes = [&](int pass) {   // 0 opaque, 1 transparent
      common(p);
      static const char* TN[6] = {"uBaseTex", "uRoughTex", "uMetalTex", "uNormTex", "uAoTex", "uEmisTex"};
      for (int k = 0; k < 6; k++) glUniform1i(U(p, TN[k]), 1 + k); glUniform1i(U(p, "uBones"), 7);
      { const char* LN[4] = {"uL0", "uL1", "uL2", "uL3"}; for (int k = 0; k < 4; k++) glUniform1i(U(p, LN[k]), 9 + k); }
      for (auto& it : f.draws) {
        if (!it.mesh || !it.mesh->vao || it.pickOnly || (pass == 0) != (it.opacity >= 0.999f && !it.blend) || !inView(vp, it)) continue;
        M4(p, "uModel", it.model); V3(p, "uColor", it.linear ? it.color : lin(it.color)); V3(p, "uEmissive", it.linear ? it.emissive : lin(it.emissive)); bindBones(p, it);
        glUniform1f(U(p, "uMetal"), it.metal); glUniform1f(U(p, "uRough"), it.rough); glUniform1f(U(p, "uOpacity"), it.opacity); glUniform1i(U(p, "uUnlit"), it.unlit ? 1 : 0);
        int maps = 0; for (int k = 0; k < 6; k++) if (it.tex[k]) { maps |= 1 << k; glActiveTexture(GL_TEXTURE1 + k); glBindTexture(GL_TEXTURE_2D, it.tex[k]); } glActiveTexture(GL_TEXTURE0);
        glUniform1i(U(p, "uSplat"), it.splat ? 1 : 0);
        if (it.splat) { int has[4]; glm::vec3 lc[4]; for (int k = 0; k < 4; k++) { has[k] = it.layerTex[k] ? 1 : 0; lc[k] = lin(it.layerCol[k]); if (it.layerTex[k]) { glActiveTexture(GL_TEXTURE9 + k); glBindTexture(GL_TEXTURE_2D, it.layerTex[k]); } }
          glActiveTexture(GL_TEXTURE0); glUniform1iv(U(p, "uLHas"), 4, has); glUniform3fv(U(p, "uLCol"), 4, &lc[0].x); glUniform1fv(U(p, "uLTile"), 4, it.layerTile); glUniform1fv(U(p, "uLRough"), 4, it.layerRough); }
        glUniform1i(U(p, "uMaps"), maps); glUniform1i(U(p, "uVColor"), it.vcolor ? 1 : 0); glUniform1i(U(p, "uWorldUV"), it.worldUV ? 1 : 0); glUniform1i(U(p, "uFlipG"), it.flipGreen ? 1 : 0);
        glUniform2f(U(p, "uTiling"), it.tiling.x, it.tiling.y); glUniform1f(U(p, "uNormalScale"), it.normalScale); glUniform1f(U(p, "uAoStr"), it.aoStrength); glUniform1f(U(p, "uCutoff"), it.cutoff);
        { const ProbeItem* best = nullptr; const Probe* bp = nullptr; glm::vec3 c(it.model[3]);   // the smallest capture around it (like Unreal)
          if (!capturing_) for (auto& pr : f.probes) { auto q = probes_.find(probeKey(pr)); if (q == probes_.end() || !q->second.ready) continue;
            if (glm::length(c - pr.pos) < pr.radius && (!best || pr.radius < best->radius)) { best = &pr; bp = &q->second; } }
          glUniform1i(U(p, "uHasEnv"), best ? 1 : 0);
          if (best) { glActiveTexture(GL_TEXTURE8); glBindTexture(GL_TEXTURE_CUBE_MAP, bp->tex); glActiveTexture(GL_TEXTURE0); V3(p, "uEnvPos", best->pos); glUniform1f(U(p, "uEnvRadius"), best->radius);
            glUniform1f(U(p, "uEnvInt"), best->intensity); glUniform1f(U(p, "uEnvMips"), 7.f); } }
        if (pass == 1) glDepthMask(GL_FALSE);
        glBindVertexArray(it.mesh->vao); glDrawElements(GL_TRIANGLES, it.mesh->count, GL_UNSIGNED_INT, nullptr); drawCalls++; triangles += it.mesh->count / 3;
        glDepthMask(GL_TRUE);
      } };
    meshes(0);
    if (!f.voxelWater.empty()) {   // liquids of the block world: after everything opaque, far to near
      std::vector<const VoxelDraw*> ws; for (auto& v : f.voxelWater) ws.push_back(&v);
      std::sort(ws.begin(), ws.end(), [&](const VoxelDraw* a, const VoxelDraw* b) { return glm::length((a->mn + a->mx) * 0.5f - cam.pos) > glm::length((b->mn + b->mx) * 0.5f - cam.pos); });
      unsigned wp = wat_; voxCommon(wp, 1); glDepthMask(GL_FALSE);
      for (auto* v : ws) { V3(wp, "uOffset", v->offset); voxModel(wp, *v); glBindVertexArray(v->vao); glDrawElements(GL_TRIANGLES, v->count, GL_UNSIGNED_INT, nullptr); drawCalls++; triangles += v->count / 3; }
      glDepthMask(GL_TRUE); }
    meshes(1);
    // ---- resolve
    if (P.samples) { glBindFramebuffer(GL_READ_FRAMEBUFFER, P.msFbo); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, P.hdrFbo);
      glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST); glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_DEPTH_BUFFER_BIT, GL_NEAREST); }
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glBindVertexArray(postVao_);
    int hw = std::max(1, w / 2), hh = std::max(1, h / 2);
    // ---- ambient occlusion
    bool ao = quality.ssao && f.viewMode == VM_Lit;
    if (ao) {
      glBindFramebuffer(GL_FRAMEBUFFER, P.aoFbo[0]); glViewport(0, 0, hw, hh); glUseProgram(ssao_);
      glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, P.depthTex); glUniform1i(U(ssao_, "uDepth"), 0);
      M4(ssao_, "uProj", cam.proj); M4(ssao_, "uInvProj", glm::inverse(cam.proj)); glUniform2f(U(ssao_, "uTexel"), 1.f / w, 1.f / h); glUniform1f(U(ssao_, "uRadius"), quality.aoRadius);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glBindFramebuffer(GL_FRAMEBUFFER, P.aoFbo[1]); glUseProgram(aoBlur_); glBindTexture(GL_TEXTURE_2D, P.aoTex[0]); glUniform1i(U(aoBlur_, "uAo"), 0);
      glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, P.depthTex); glUniform1i(U(aoBlur_, "uDepth"), 1); glActiveTexture(GL_TEXTURE0);
      M4(aoBlur_, "uInvProj", glm::inverse(cam.proj)); glUniform2f(U(aoBlur_, "uTexel"), 1.f / hw, 1.f / hh); glDrawArrays(GL_TRIANGLES, 0, 3); }
    // ---- bloom: bright light spreads (lamps, glowing blocks, the sun on water)
    bool bloom = quality.bloom && f.viewMode == VM_Lit && P.bloomN > 0;
    if (bloom) {
      glUseProgram(bloomPre_); glBindFramebuffer(GL_FRAMEBUFFER, P.bloomFbo[0]); glViewport(0, 0, P.bw[0], P.bh[0]);
      glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, P.depthTex); glUniform1i(U(bloomPre_, "uDepth"), 1);
      glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, P.hdrTex); glUniform1i(U(bloomPre_, "uSrc"), 0); glUniform2f(U(bloomPre_, "uTexel"), 1.f / w, 1.f / h);
      glUniform1f(U(bloomPre_, "uExposure"), expo); glUniform1f(U(bloomPre_, "uThreshold"), quality.bloomThreshold); glDrawArrays(GL_TRIANGLES, 0, 3);
      glUseProgram(bloomDown_); glUniform1i(U(bloomDown_, "uSrc"), 0);
      for (int i = 1; i < P.bloomN; i++) { glBindFramebuffer(GL_FRAMEBUFFER, P.bloomFbo[i]); glViewport(0, 0, P.bw[i], P.bh[i]); glBindTexture(GL_TEXTURE_2D, P.bloomTex[i - 1]);
        glUniform2f(U(bloomDown_, "uTexel"), 1.f / P.bw[i - 1], 1.f / P.bh[i - 1]); glDrawArrays(GL_TRIANGLES, 0, 3); }
      glUseProgram(bloomUp_); glUniform1i(U(bloomUp_, "uSrc"), 0); glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE);
      for (int i = P.bloomN - 1; i > 0; i--) { glBindFramebuffer(GL_FRAMEBUFFER, P.bloomFbo[i - 1]); glViewport(0, 0, P.bw[i - 1], P.bh[i - 1]); glBindTexture(GL_TEXTURE_2D, P.bloomTex[i]);
        glUniform2f(U(bloomUp_, "uTexel"), 1.f / P.bw[i], 1.f / P.bh[i]); glDrawArrays(GL_TRIANGLES, 0, 3); }
      glDisable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
    // ---- to the screen: tonemap, with the scene's depth (for what is drawn on top)
    glBindFramebuffer(GL_FRAMEBUFFER, target); glViewport(0, 0, w, h);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_ALWAYS); glDepthMask(GL_TRUE); glUseProgram(compose_);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, P.hdrTex); glUniform1i(U(compose_, "uHdr"), 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, P.depthTex); glUniform1i(U(compose_, "uDepth"), 1);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, P.aoTex[1]); glUniform1i(U(compose_, "uAo"), 2);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, P.bloomN ? P.bloomTex[0] : 0); glUniform1i(U(compose_, "uBloom"), 3); glActiveTexture(GL_TEXTURE0);
    glUniform1i(U(compose_, "uOut"), f.viewMode == VM_Lit ? 0 : f.viewMode == VM_Unlit ? 1 : 2); glUniform1f(U(compose_, "uExposure"), expo);
    glUniform1i(U(compose_, "uHasAo"), ao ? 1 : 0); glUniform1f(U(compose_, "uAoAmount"), quality.aoAmount);
    glUniform1i(U(compose_, "uHasBloom"), bloom ? 1 : 0); glUniform1f(U(compose_, "uBloomAmount"), quality.bloomAmount * std::max(f.world.post.bloom, 0.f));
    { const PostProcess& q = f.world.post; bool on = f.viewMode == VM_Lit && !q.isDefault(); glUniform1i(U(compose_, "uPost"), on ? 1 : 0);
      glUniform1f(U(compose_, "uTime"), f.time); glUniform2f(U(compose_, "uRes"), (float)w, (float)h); glUniform1f(U(compose_, "uVignette"), q.vignette); glUniform1f(U(compose_, "uGrain"), q.grain);
      glUniform1f(U(compose_, "uChroma"), q.chromatic); glUniform1f(U(compose_, "uScan"), q.scanlines); glUniform1f(U(compose_, "uVhs"), q.vhs); glUniform1f(U(compose_, "uSat"), q.saturation);
      glUniform1f(U(compose_, "uContrast"), q.contrast); glUniform1f(U(compose_, "uTemp"), q.temperature); V3(compose_, "uTint", q.tint); }
    glDrawArrays(GL_TRIANGLES, 0, 3); drawCalls++;
    glDepthFunc(GL_LEQUAL); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  } else {
    glBindFramebuffer(GL_FRAMEBUFFER, target); glViewport(0, 0, w, h); glClearColor(0.12f, 0.12f, 0.12f, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_TRUE); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  }
  glDisable(GL_CULL_FACE);
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
  if (f.viewMode == VM_Wire) for (auto& it : f.draws) { if (!it.mesh || !it.mesh->vao || it.pickOnly || it.bones) continue; M4(q, "uModel", it.model); glUniform4f(U(q, "uColor"), 0.6f, 0.82f, 1.f, 1.f);
    glBindVertexArray(it.mesh->vao); glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); glDrawElements(GL_TRIANGLES, it.mesh->count, GL_UNSIGNED_INT, nullptr); glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); }
  for (int top = 0; top < 2; top++) { if (top) glDisable(GL_DEPTH_TEST);
    for (auto& l : f.lines) { if (l.onTop != (bool)top || !l.mesh) continue; M4(q, "uModel", l.model); glUniform4f(U(q, "uColor"), l.color.r, l.color.g, l.color.b, l.color.a);
      glBindVertexArray(l.mesh->vao); if (l.wire) { glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); glEnable(GL_POLYGON_OFFSET_LINE); glPolygonOffset(-1, -1); }
      if (l.mesh->lines) glDrawArrays(GL_LINES, 0, l.mesh->count); else glDrawElements(GL_TRIANGLES, l.mesh->count, GL_UNSIGNED_INT, nullptr);
      if (l.wire) { glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); glDisable(GL_POLYGON_OFFSET_LINE); } }
    glEnable(GL_DEPTH_TEST); }
  glBindVertexArray(0); glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// outside the view (all 8 corners of its box beyond one side): not drawn. Skinned models move away from their rest box, so they are kept.
static bool inView(const glm::mat4& vp, const DrawItem& it) {
  if (!it.mesh || it.bones) return true; const glm::vec3 &a = it.mesh->bmin, &b = it.mesh->bmax; if (a.x > b.x) return true;
  glm::mat4 m = vp * it.model; int out[6] = {0, 0, 0, 0, 0, 0};
  for (int k = 0; k < 8; k++) { glm::vec4 c = m * glm::vec4((k & 1) ? b.x : a.x, (k & 2) ? b.y : a.y, (k & 4) ? b.z : a.z, 1);
    out[0] += c.x < -c.w; out[1] += c.x > c.w; out[2] += c.y < -c.w; out[3] += c.y > c.w; out[4] += c.z < -c.w; out[5] += c.z > c.w; }
  for (int i = 0; i < 6; i++) if (out[i] == 8) return false; return true;
}
// a skinned model's joints into the bone texture (unit 7), or none
void Renderer::bindBones(unsigned prog, const DrawItem& it) {
  bool on = it.bones && it.boneCount > 0; glUniform1i(U(prog, "uSkin"), on ? 1 : 0); if (!on) return;
  glActiveTexture(GL_TEXTURE7); glBindTexture(GL_TEXTURE_2D, boneTex_); glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 4, it.boneCount, 0, GL_RGBA, GL_FLOAT, it.bones); glActiveTexture(GL_TEXTURE0);
}
unsigned Renderer::pick(const Frame& f, const Camera& cam, RenderTarget& rt, int x, int y) {
  if (!rt.idFbo || x < 0 || y < 0 || x >= rt.w || y >= rt.h) return 0;
  glBindFramebuffer(GL_FRAMEBUFFER, rt.idFbo); glViewport(0, 0, rt.w, rt.h); glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
  glUseProgram(idp_); M4(idp_, "uVP", cam.proj * cam.view);
  auto idc = [&](unsigned id) { glUniform4f(U(idp_, "uColor"), (id & 255) / 255.f, ((id >> 8) & 255) / 255.f, ((id >> 16) & 255) / 255.f, 1); };
  glUniform1i(U(idp_, "uBones"), 7);
  for (auto& it : f.draws) { if (!it.mesh || !it.mesh->vao || !it.id) continue; idc(it.id); M4(idp_, "uModel", it.model); bindBones(idp_, it); glBindVertexArray(it.mesh->vao); glDrawElements(GL_TRIANGLES, it.mesh->count, GL_UNSIGNED_INT, nullptr); }
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
