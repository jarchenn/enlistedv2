#include "render.h"
#include "nature.h"
#include "textures.h"
#include <chrono>
// L2 lighting engine hooks (src/lighting.cpp, docs/lighting.md)
void lightingInit(Renderer& R, const char* kCommonSrc, const char* kMainFSSrc);
void lightingBegin(Renderer& R, const RenderView& v);
void lightingPrepass(Renderer& R, const RenderView& v, const std::vector<DrawItem>& opaque, const Terrain& t);
void lightingEnd(Renderer& R);
void lightingTodColors(const Renderer& R, vec3& sun, vec3& sky, vec3& ground, vec3& fog, vec3& fill, vec3& zen, vec3& hor, vec3& skySun, vec3& skySunCol);
void lightingUniforms(Renderer& R, GLuint p);
void lightingExposure(const Renderer& R, GLuint p);
float lightingFixedExposure(const Renderer& R);

// ============================================================== shaders
static const char* kCommon = R"GLSL(
const float PI = 3.14159265;
float hash12(vec2 p){ vec3 p3 = fract(vec3(p.xyx)*0.1031); p3 += dot(p3, p3.yzx+33.33); return fract((p3.x+p3.y)*p3.z); }
float hash13(vec3 p){ p = fract(p*0.1031); p += dot(p, p.zyx+31.32); return fract((p.x+p.y)*p.z); }
float vnoise(vec3 p){
  vec3 i = floor(p), f = fract(p); f = f*f*(3.0-2.0*f);
  float a=hash13(i), b=hash13(i+vec3(1,0,0)), c=hash13(i+vec3(0,1,0)), d=hash13(i+vec3(1,1,0));
  float e=hash13(i+vec3(0,0,1)), g=hash13(i+vec3(1,0,1)), h=hash13(i+vec3(0,1,1)), k=hash13(i+vec3(1,1,1));
  return mix(mix(mix(a,b,f.x),mix(c,d,f.x),f.y), mix(mix(e,g,f.x),mix(h,k,f.x),f.y), f.z);
}
float fbm(vec3 p){ float s=0.0, a=0.5; for(int i=0;i<4;i++){ s+=a*vnoise(p); p*=2.03; a*=0.5; } return s; }

uniform vec3 uSunDir, uSunCol, uSkyCol, uGroundCol, uCamPos, uFogCol, uFill;
uniform float uFogDens;
uniform float uUnder;   // 1: the camera is under water (swim.cpp): applyFog / sky become a dense uniform water fog
uniform sampler2DShadow uShadow; uniform mat4 uLightVP; uniform int uShadowOn;
uniform sampler2D uOcc; uniform mat4 uOccVP; uniform float uOccTop, uOccRange; uniform int uOccOn;
uniform vec2 uShadowTexel;
// ---- lighting model (docs/rendering.md). Original implementation; BSL v10 (Capt Tatsu) was the visual reference.
uniform sampler2DShadow uShadowFar; uniform mat4 uLightVPFar; uniform int uShadowFarOn;
uniform vec2 uShadowTexelFar;
uniform vec2 uShadowWorldTexel;   // metres per shadow texel: near, far cascade
uniform float uShadowDepthRange;  // metres covered by the [0,1] light-space depth
uniform float uShadowSoft;        // PCF radius in near-cascade texels (0 = legacy 3x3 grid)
uniform int uFrame;               // frame counter (noise rotation; TAA decorrelation)
uniform int uCloudShadowOn; uniform float uCloudCov, uCloudT;
uniform float uAoIndirectOnly;    // 1: material AO / sky occlusion darken only indirect light
// ---- L2 lighting engine (src/lighting.cpp, docs/lighting.md): time of day, clustered local lights, probe GI
uniform vec3 uSkyZen, uSkyHor;    // sky model zenith / horizon colour (time of day; Day = the BSL-pass constants)
uniform vec3 uSkySun, uSkySunCol; // true sun direction and colour for the sky dome / haze glow (the key light may be the moon)
uniform vec3 uSkyK;               // horizon colour relative to Day (exactly 1 by Day): scales the ground band and the legacy dome

// interleaved gradient noise (Jimenez 2014), rotated per frame
float ign(vec2 p){ return fract(52.9829189*fract(dot(p, vec2(0.06711056, 0.00583715))) + float(uFrame % 64)*0.618034); }
#ifdef NMK_FS
float pixelNoise(){ return ign(gl_FragCoord.xy); }
#else
float pixelNoise(){ return 0.5; }
#endif
// one cascade: Vogel-disk PCF in light space. Normal offset grows with the slope to the sun (acne), a small
// constant depth bias keeps contact (no peter-panning where walls meet the ground).
float cascadePCF(sampler2DShadow sm, mat4 vp, vec2 texel, float worldTexel, vec3 w, vec3 n, float radiusTex, int taps, out float edge){
  float ndl = clamp(dot(n, uSunDir), 0.0, 1.0), sinT = sqrt(1.0 - ndl*ndl);
  vec3 off = n*worldTexel*(0.6 + 1.6*sinT);
  vec4 lp = vp*vec4(w + off, 1.0);
  vec3 s = lp.xyz/lp.w*0.5 + 0.5;
  vec2 e = abs(s.xy*2.0 - 1.0); edge = max(e.x, e.y);
  if (edge >= 1.0 || s.z >= 1.0) { edge = 2.0; return 1.0; }
  float bias = worldTexel*(0.35 + 0.9*min(sinT/max(ndl, 0.08), 6.0))/uShadowDepthRange;
  float rot = pixelNoise()*6.2831853, sum = 0.0;
  for (int i = 0; i < taps; ++i) {
    float r = sqrt((float(i) + 0.5)/float(taps)), a = float(i)*2.3999632 + rot;
    sum += texture(sm, vec3(s.xy + vec2(cos(a), sin(a))*r*radiusTex*texel, s.z - bias));
  }
  return sum/float(taps);
}
float sunShadowT(vec3 w, vec3 n, float soft, int taps){
  if (uShadowOn == 0) return 1.0;
  if (uShadowSoft <= 0.0) {   // legacy fallback: 3x3 grid, fixed offsets
    float ndl = clamp(dot(n, uSunDir), 0.0, 1.0);
    vec4 lp = uLightVP * vec4(w + n*(0.02 + 0.05*(1.0-ndl)), 1.0);
    vec3 s = lp.xyz/lp.w*0.5+0.5;
    if (s.x<0.0||s.x>1.0||s.y<0.0||s.y>1.0||s.z>1.0) return 1.0;
    float sum = 0.0;
    for (int y=-1;y<=1;y++) for (int x=-1;x<=1;x++) sum += texture(uShadow, vec3(s.xy + vec2(x,y)*uShadowTexel*1.2, s.z - 0.0006));
    return sum/9.0;
  }
  float eN, eF;
  float sN = cascadePCF(uShadow, uLightVP, uShadowTexel, uShadowWorldTexel.x, w, n, uShadowSoft*(1.0 + 2.5*soft), taps, eN);
  if (uShadowFarOn == 0) return sN;
  float tB = smoothstep(0.78, 0.96, eN);
  if (tB <= 0.0) return sN;
  float sF = cascadePCF(uShadowFar, uLightVPFar, uShadowTexelFar, uShadowWorldTexel.y, w, n, 1.25 + soft, max(taps/2, 4), eF);
  sF = mix(sF, 1.0, smoothstep(0.85, 0.98, eF));   // fade out at the far edge instead of a hard line
  return mix(sN, sF, tB);
}
float shadowPCF(vec3 w, vec3 n){ return sunShadowT(w, n, 0.0, 8); }   // 8 rotated taps; TAA integrates the rotation over frames
// cloud deck at 1500 m: the same field the sky draws, in world xz (so the shadows on the ground match the sky)
float cloudField(vec2 xz){
  vec3 q = vec3(xz/600.0 + uCloudT*vec2(0.012, 0.0075), uCloudT*0.002);
  float n = 0.0, a = 0.5;
  for (int i = 0; i < 5; ++i) { n += a*vnoise(q); q = q*2.07 + vec3(1.7, 9.2, 3.1); a *= 0.5; }
  return n;
}
float cloudThreshold(){ return mix(0.72, 0.38, uCloudCov); }
// low-octave version for the clouds' self-shadowing (only the broad optical depth matters)
float cloudFieldLo(vec2 xz){
  vec3 q = vec3(xz/600.0 + uCloudT*vec2(0.012, 0.0075), uCloudT*0.002);
  float n = 0.0, a = 0.5;
  for (int i = 0; i < 3; ++i) { n += a*vnoise(q); q = q*2.07 + vec3(1.7, 9.2, 3.1); a *= 0.5; }
  return n + 0.0625;   // the two missing octaves average ~a/2 each
}
// cloud shadow on the ground: the field varies over ~600 m, so it is baked once per frame into a 512^2 map
// (+-1.5 km around the camera, ~6 m texels) and only sampled per fragment
uniform sampler2D uCloudShadowMap; uniform vec3 uCloudShadowBox;   // centre x, centre z, half extent
float cloudShadowAt(vec2 xz, float y){
  vec2 q = xz + uSunDir.xz/uSunDir.y*(1500.0 - y);
  float th = cloudThreshold();
  return 1.0 - 0.62*smoothstep(th - 0.02, th + 0.30, cloudField(q));
}
float cloudShadow(vec3 w){
  if (uCloudShadowOn == 0 || uSunDir.y < 0.05) return 1.0;
  vec2 uv = (w.xz - uCloudShadowBox.xy)/(2.0*uCloudShadowBox.z) + 0.5;
  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return cloudShadowAt(w.xz, w.y);
  return texture(uCloudShadowMap, uv).r;
}
float skyVis(vec3 w, vec3 n){
  if (uOccOn == 0) return 1.0;
  vec3 q = w + vec3(n.x, 0.0, n.z)*0.65;
  vec4 lp = uOccVP * vec4(q, 1.0);
  vec2 uv = lp.xy*0.5+0.5;
  if (uv.x<0.0||uv.x>1.0||uv.y<0.0||uv.y>1.0) return 1.0;
  float occ = 0.0;
  vec2 t = 1.0/vec2(textureSize(uOcc,0));
  for (int y=-1;y<=1;y++) for (int x=-1;x<=1;x++) {
    float d = texture(uOcc, uv + vec2(x,y)*t*1.5).r;
    float oy = uOccTop - d*uOccRange;
    occ += smoothstep(0.15, 0.6, oy - w.y);
  }
  return 1.0 - occ/9.0;
}
uniform int uLook;          // 0 legacy look, 1 current
// Indirect (hemisphere) light of the current fragment and the haze transmittance in front of it. World shaders
// write gInd*gFogT to the AO attachment, so screen-space AO can remove occluded *indirect* light only.
vec3 gInd = vec3(0.0); float gFogT = 1.0;
// Relief inputs from the wall materials (kMainFS, docs/rendering.md#rough-walls): gNg = the geometric normal when the
// material bumped N (zero otherwise), gCavity = how much of the sky a relief recess still sees (mortar, hollows).
vec3 gNg = vec3(0.0); float gCavity = 1.0;
vec3 gEyeOv = vec3(1e30);   // L2 probe captures: the probe is the eye (kMainFS sets it from the capture vertex shader)
uniform float uHazeH;       // aerial-perspective scale height (m)
vec3 skyColor(vec3 d){
  float h = clamp(d.y, -1.0, 1.0);
  float sd = max(dot(d, uSkySun), 0.0);
  if (uLook == 0) {
    vec3 tk = uSkyK;   // time-of-day scale of the legacy dome (1 by day)
    vec3 zen = vec3(0.33,0.44,0.60)*1.25*tk, hor = vec3(0.74,0.77,0.79)*1.25*tk, gnd = vec3(0.40,0.41,0.38)*tk;
    vec3 c = h > 0.0 ? mix(hor, zen, pow(h, 0.55)) : mix(hor, gnd, clamp(-h*4.0,0.0,1.0));
    return c + uSkySunCol * (pow(sd, 400.0)*6.0 + pow(sd, 12.0)*0.12);
  }
  // Exponential gradient: the horizon is brightest and greyest (long air path), the zenith a soft Channel blue.
  // The sun side of the sky is lifted and warmed (forward scattering by haze), the opposite side stays cool.
  // Day: zen (0.28, 0.40, 0.62), hor (0.80, 0.84, 0.87); the time of day moves both (lighting.cpp todCompute)
  vec3 zen = uSkyZen, hor = uSkyHor, gnd = vec3(0.36, 0.37, 0.34)*uSkyK;
  float up = max(h, 0.0);
  float g = exp(-up*3.2);                                       // 1 at the horizon -> ~0.04 at the zenith
  vec3 c = mix(zen, hor, g);
  float halo = pow(sd, 6.0)*0.22 + pow(sd, 48.0)*0.55;
  c += uSkySunCol*vec3(1.0, 0.93, 0.80)*halo*(0.35 + 0.65*g)*0.32;
  c *= 1.0 - 0.06*(1.0 - sd)*(1.0 - g);                         // anti-solar sky a touch deeper
  if (h < 0.0) c = mix(hor*0.92, gnd, clamp(-h*5.0, 0.0, 1.0));   // below the horizon (reflections only)
  c += uSkySunCol*pow(sd, 900.0)*7.0*step(0.0, h);               // sun disc
  return c;
}
// Aerial perspective: height-falloff haze integrated along the view ray, density rho(y) = rho0 exp(-y / Hh).
// Optical depth from the eye at e to the point: rho0 * L * exp(-e.y/Hh) * (1 - exp(-dy/Hh)) / (dy/Hh).
// The in-scattered colour is the horizon sky in that direction plus a sun glow, so the land fades into the sky.
vec3 applyFog(vec3 col, vec3 w){
  if (uUnder > 0.5) { float f = 1.0 - exp(-length(w - uCamPos)*uFogDens); gFogT = 1.0 - f; return mix(col, uFogCol, f); }
  vec3 dir = normalize(w - uCamPos);
  if (uLook == 0) {
    float d = max(length(w - uCamPos) - 35.0, 0.0);
    float f = 1.0 - exp(-d*uFogDens);
    vec3 fc = mix(uFogCol, skyColor(vec3(dir.x, max(dir.y,0.02), dir.z)), 0.5);
    gFogT = 1.0 - f;
    return mix(col, fc, f);
  }
  float L = max(length(w - uCamPos) - 12.0, 0.0);          // a clear zone for close combat
  float dy = w.y - uCamPos.y, k = dy/uHazeH;
  float hf = abs(k) > 1e-3 ? (1.0 - exp(-k))/k : 1.0 - 0.5*k;
  float tau = uFogDens*L*exp(-max(uCamPos.y, 0.0)/uHazeH)*hf;
  float f = 1.0 - exp(-tau);
  vec3 fd = normalize(vec3(dir.x, max(dir.y, 0.015), dir.z));
  vec3 fc = mix(uFogCol, skyColor(fd), 0.65);
  float sd = max(dot(dir, uSkySun), 0.0);
  fc += uSkySunCol*vec3(1.0, 0.92, 0.78)*pow(sd, 8.0)*0.10;
  gFogT = 1.0 - f;
  return mix(col, fc, f);
}
float D_GGX(float NdH, float a){ float a2=a*a; float d=NdH*NdH*(a2-1.0)+1.0; return a2/(PI*d*d); }
float G_Smith(float NdV, float NdL, float r){ float k=(r+1.0)*(r+1.0)/8.0; return (NdV/(NdV*(1.0-k)+k))*(NdL/(NdL*(1.0-k)+k)); }
vec3 F_Schlick(float c, vec3 F0){ return F0 + (1.0-F0)*pow(1.0-c, 5.0); }
// ---- local lights: clustered forward (lighting.cpp builds the froxel lists in a compute pass)
struct LLight { vec4 posR; vec4 colI; vec4 dirC; vec4 misc; };   // pos, radius | rgb x intensity (flicker applied) | spot dir, cos outer | cos inner, shadow slot, spot, -
layout(std430, binding = 5) readonly buffer NmkLights { LLight gLights[]; };
layout(std430, binding = 6) readonly buffer NmkClusters { uint gCluster[]; };      // per froxel: count, then max-per indices
layout(std430, binding = 7) readonly buffer NmkLShadowVP { mat4 gLShadowVP[]; };   // 6 per shadow slot (cube faces; spot: face 0)
#ifdef NMK_PROBE_RW
layout(std430, binding = 8) buffer NmkProbes { vec4 gProbe[]; };                   // the probe projection pass writes them
#else
layout(std430, binding = 8) readonly buffer NmkProbes { vec4 gProbe[]; };          // 4 per probe: SH R, G, B (irradiance / pi), pos + valid
#endif
uniform int uLightCount, uLightMode;   // mode 0: froxel clusters (camera passes), 1: every light (probe captures)
uniform vec4 uClusterP;                // unjittered projection [0][0], [1][1], [2][0], [2][1]
uniform vec4 uClusterZ;                // near (m), slices / ln(far / near)
uniform ivec4 uClusterDim;             // nx, ny, nz, max lights per froxel
uniform vec2 uScreen;                  // camera target size (px)
uniform mat4 uViewM;                   // camera view matrix
uniform sampler2DShadow uLShadow;      // unit 16: light shadow atlas (standard Z)
uniform vec4 uLShadowTile;             // tile px, atlas w, atlas h
uniform float uLampUnit;               // irradiance at 1 m of intensity 1 (scene units; the Day sun is 3.3)
float lightFalloff(float d2, float r){ float x = d2/(r*r); float w = clamp(1.0 - x*x, 0.0, 1.0); return w*w/max(d2, 0.09); }
float lightShadow(int slot, bool spot, vec3 w, vec3 Ng, vec3 lp, vec3 L){
  vec3 d = w - lp; int face = 0;
  if (!spot) { vec3 a = abs(d); face = (a.x >= a.y && a.x >= a.z) ? (d.x > 0.0 ? 0 : 1) : (a.y >= a.z ? (d.y > 0.0 ? 2 : 3) : (d.z > 0.0 ? 4 : 5)); }
  float dist = length(d);
  vec3 q = w + Ng*(0.02 + 0.012*dist) + L*(0.01 + 0.01*dist);   // normal offset and a pull toward the light (acne)
  vec4 c = gLShadowVP[slot*6 + face]*vec4(q, 1.0);
  if (c.w <= 1e-4) return 1.0;
  vec3 s = c.xyz/c.w*0.5 + 0.5;
  if (s.z >= 1.0 || s.x < 0.0 || s.x > 1.0 || s.y < 0.0 || s.y > 1.0) return 1.0;
  float T = uLShadowTile.x; vec2 inset = vec2(1.5/T);
  vec2 base = vec2(float(face), float(slot))*T;
  float sum = 0.0;
  for (int i = 0; i < 4; ++i) {
    vec2 o = (vec2(float(i & 1), float(i >> 1)) - 0.5)*1.2/T;
    vec2 uv = (base + clamp(s.xy + o, inset, 1.0 - inset)*T)/uLShadowTile.yz;
    sum += texture(uLShadow, vec3(uv, s.z - 0.0004));
  }
  return sum*0.25;
}
void lightTerm(int i, vec3 w, vec3 N, vec3 Ng, vec3 V, float a, float rough, vec3 F0, inout vec3 dif, inout vec3 spe){
  LLight l = gLights[i];
  vec3 dl = l.posR.xyz - w; float d2 = dot(dl, dl);
  if (d2 >= l.posR.w*l.posR.w) return;
  float d = sqrt(d2); vec3 L = dl/max(d, 1e-4);
  float NdL = dot(N, L);
  if (NdL <= 0.0) return;
  float att = lightFalloff(d2, l.posR.w);
  if (l.dirC.w > -1.0) att *= smoothstep(l.dirC.w, max(l.misc.x, l.dirC.w + 1e-3), dot(-L, l.dirC.xyz));
  if (att <= 0.0) return;
  if (l.misc.y >= 0.0) att *= lightShadow(int(l.misc.y + 0.5), l.misc.z > 0.5, w, Ng, l.posR.xyz, L);
  vec3 E = l.colI.rgb*(uLampUnit*att*NdL);
  dif += E;
  if (a > 0.0) {
    vec3 H = normalize(V + L); float NdV = max(dot(N, V), 1e-3), NdH = max(dot(N, H), 0.0);
    spe += E*D_GGX(NdH, a)*G_Smith(NdV, NdL, rough)*F_Schlick(max(dot(H, V), 0.0), F0)/(4.0*NdV*max(NdL, 1e-3));
  }
}
// Sum of the local lights at w: dif = irradiance (times NdL, shadowed), spe = GGX specular radiance. rough < 0: diffuse only.
void localLights(vec3 w, vec3 N, vec3 Ng, vec3 V, float rough, vec3 F0, out vec3 dif, out vec3 spe){
  dif = vec3(0.0); spe = vec3(0.0);
  if (uLightCount <= 0) return;
  float a = rough < 0.0 ? 0.0 : max(rough*rough, 0.02);
#ifdef NMK_FS
  if (uLightMode == 0) {
    float vz = -(uViewM*vec4(w, 1.0)).z;
    int sz = int(floor(log(max(vz, uClusterZ.x)/uClusterZ.x)*uClusterZ.y));
    if (sz >= uClusterDim.z) return;
    ivec2 t = clamp(ivec2(gl_FragCoord.xy/uScreen*vec2(uClusterDim.xy)), ivec2(0), uClusterDim.xy - 1);
    int base = ((sz*uClusterDim.y + t.y)*uClusterDim.x + t.x)*(uClusterDim.w + 1);
    int n = int(gCluster[base]);
    for (int k = 0; k < n; ++k) lightTerm(int(gCluster[base + 1 + k]), w, N, Ng, V, a, rough, F0, dif, spe);
    return;
  }
#endif
  for (int i = 0; i < uLightCount; ++i) lightTerm(i, w, N, Ng, V, a, rough, F0, dif, spe);
}
// ---- indirect light: DDGI-lite probe volumes around the camera (lighting.cpp). Each probe holds L1 SH of its captured
// radiance (stored as irradiance / pi: E(n)/pi = c.x + dot(c.yzw, n)) and an 8x8 octahedral tile of the distance to
// the nearest surface (mean, mean^2) for a Chebyshev visibility test, so light does not leak through walls.
uniform int uGiOn, uGiCascades;
uniform vec4 uGiGrid[2];               // spacing (m), capture range (m)
uniform ivec4 uGiOrg[2];               // origin cell (world cell index), first probe index
uniform ivec4 uGiDim[2];               // probes per axis
uniform sampler2D uGiVis;              // unit 17
uniform int uGiVisCols;                // tiles per atlas row
uniform float uGiOpenK;                // share of the analytic sky model where the sky is open (1 by day)
vec2 octEncode(vec3 n){
  n /= (abs(n.x) + abs(n.y) + abs(n.z));
  vec2 p = n.xz;
  if (n.y < 0.0) p = (1.0 - abs(p.yx))*vec2(n.x >= 0.0 ? 1.0 : -1.0, n.z >= 0.0 ? 1.0 : -1.0);
  return p;
}
vec3 octDecode(vec2 p){
  vec3 n = vec3(p.x, 1.0 - abs(p.x) - abs(p.y), p.y);
  if (n.y < 0.0) { vec2 q = (1.0 - abs(n.zx))*vec2(n.x >= 0.0 ? 1.0 : -1.0, n.z >= 0.0 ? 1.0 : -1.0); n.x = q.x; n.z = q.y; }
  return normalize(n);
}
int giIndex(int c, ivec3 cell){ ivec3 n = uGiDim[c].xyz; ivec3 m = ((cell % n) + n) % n; return uGiOrg[c].w + (m.y*n.z + m.z)*n.x + m.x; }
vec2 giVisUV(int idx, vec3 d){
  vec2 o = octEncode(d)*0.5 + 0.5;
  vec2 tile = vec2(float(idx % uGiVisCols), float(idx / uGiVisCols));
  return (tile*8.0 + 0.5 + o*7.0)/vec2(textureSize(uGiVis, 0));
}
// Probe irradiance / pi at w for normal N (and for direction R in env). Returns the probe coverage 0..1: the caller
// keeps its analytic term for the rest (outside the volumes, no visible probe).
float giSample(vec3 w, vec3 Ng, vec3 N, vec3 R, out vec3 irr, out vec3 env){
  irr = vec3(0.0); env = vec3(0.0);
  if (uGiOn == 0) return 0.0;
  float rem = 1.0;
  for (int c = 0; c < uGiCascades; ++c) {
    float s = uGiGrid[c].x;
    ivec3 n = uGiDim[c].xyz;
    vec3 g = w/s - vec3(uGiOrg[c].xyz);
    if (any(lessThan(g, vec3(0.0))) || any(greaterThanEqual(g, vec3(n - 1)))) continue;
    vec3 edge = min(g, vec3(n - 1) - g);
    float cw = c + 1 < uGiCascades ? clamp(min(min(edge.x, edge.y), edge.z), 0.0, 1.0) : 1.0;   // fade to the next volume over the outer cell
    ivec3 b = ivec3(floor(g)); vec3 f = g - vec3(b);
    vec3 q = w + Ng*(0.18*s);   // visibility bias along the geometric normal
    vec4 sR = vec4(0.0), sG = vec4(0.0), sB = vec4(0.0); float sw = 0.0, tw = 0.0;
    for (int k = 0; k < 8; ++k) {
      ivec3 o = ivec3(k & 1, (k >> 1) & 1, k >> 2);
      int idx = giIndex(c, uGiOrg[c].xyz + b + o);
      vec3 tri = mix(1.0 - f, f, vec3(o));
      float wt = tri.x*tri.y*tri.z;
      vec4 pp = gProbe[idx*4 + 3];
      if (pp.w < 0.5) continue;
      tw += wt;
      vec3 toP = pp.xyz - w; float dp = length(toP);
      float bf = (dot(toP/max(dp, 1e-4), Ng) + 1.0)*0.5; wt *= bf*bf + 0.05;   // probes behind the surface count less
      vec3 pd = q - pp.xyz; float r = length(pd);
      vec2 m = textureLod(uGiVis, giVisUV(idx, pd/max(r, 1e-4)), 0.0).rg;
      if (r > m.x) { float var = max(m.y - m.x*m.x, 0.0004); float dd = r - m.x; float ch = var/(var + dd*dd); wt *= ch*ch*ch; }
      if (wt < 1e-6) continue;
      sR += gProbe[idx*4]*wt; sG += gProbe[idx*4 + 1]*wt; sB += gProbe[idx*4 + 2]*wt; sw += wt;
    }
    if (sw <= 1e-5) continue;
    float conf = clamp(tw*4.0, 0.0, 1.0)*clamp(sw/max(tw, 1e-4)*8.0, 0.0, 1.0);
    sR /= sw; sG /= sw; sB /= sw;
    vec3 e = max(vec3(sR.x + dot(sR.yzw, N), sG.x + dot(sG.yzw, N), sB.x + dot(sB.yzw, N)), vec3(0.0));
    vec3 v = max(vec3(sR.x + dot(sR.yzw, R), sG.x + dot(sG.yzw, R), sB.x + dot(sB.yzw, R)), vec3(0.0));
    float take = rem*cw*conf;
    irr += e*take; env += v*take; rem -= take;
    if (rem <= 1e-3) break;
  }
  return 1.0 - rem;
}
// The hemisphere term of the cheaper copies (leaf, grass, water): probe light where the sky is hidden or by night.
vec3 giAmbient(vec3 w, vec3 N, float sv, vec3 analytic){
  if (uGiOn == 0) return analytic;
  float k = clamp(sv*uGiOpenK, 0.0, 1.0);
  if (k >= 0.98) return analytic;
  vec3 gi, ge; float cov = giSample(w, N, N, N, gi, ge);
  return mix(max(gi + analytic*(1.0 - cov), analytic), analytic, k);   // probe light only adds (as in shade())
}
vec3 shade(vec3 w, vec3 N, vec3 alb, float rough, float metal, float ao){
  vec3 V = normalize((gEyeOv.x < 1e29 ? gEyeOv : uCamPos) - w);
  vec3 L = uSunDir; vec3 H = normalize(V+L);
  float NdL = max(dot(N,L),0.0), NdV = max(dot(N,V),1e-3), NdH = max(dot(N,H),0.0);
  float a = max(rough*rough, 0.02);
  vec3 F0 = mix(vec3(0.04), alb, metal);
  vec3 F = F_Schlick(max(dot(H,V),0.0), F0);
  vec3 spec = D_GGX(NdH,a)*G_Smith(NdV,NdL,rough)*F/(4.0*NdV*max(NdL,1e-3));
  vec3 kd = (1.0-F)*(1.0-metal);
  // shadow and sky-occlusion lookups offset along the *geometric* normal when the material bumped N (gNg): the bumped
  // normal moved the receiver offset / slope bias per pixel, which the per-frame PCF rotation turned into flicker
  vec3 Ng = dot(gNg, gNg) > 0.5 ? gNg : N;
  float sh = NdL > 0.0 ? shadowPCF(w, Ng) * cloudShadow(w) : 0.0;
  vec3 direct = (kd*alb/PI + spec) * uSunCol * NdL * sh;
  float sv = skyVis(w, Ng);
  float amb = mix(0.06, 1.0, sv);
  vec3 irr = mix(uGroundCol, uSkyCol, 0.5+0.5*N.y) * amb + uFill*(1.0-sv);
  float cav = 1.0, relK = 1.0;
  if (uLook == 1 && dot(gNg, gNg) > 0.5) {
    // relief under sky light (bent normal): the sky toward the sun and overhead is brighter than the far side and the
    // horizon band, so a relief facet turned toward the sun's azimuth or up gathers more of it and one turned away
    // less. Only the relief's deviation from the geometric normal counts (flat walls are unchanged), and recesses
    // see less sky (gCavity): mortar, pillowed stones and render bulges stay legible on walls in shadow.
    vec3 dn = N - gNg;
    vec2 az = uSunDir.xz/max(length(uSunDir.xz), 1e-3);
    float rel = dot(dn.xz, az)*0.9 + dn.y*0.6;
    cav = clamp(gCavity, 0.3, 1.0);
    relK = clamp(1.0 + rel, 0.55, 1.5)*cav;
    irr *= relK;
  }
  vec3 R = reflect(-V, N);
  vec3 Fr = F0 + (max(vec3(1.0-rough), F0)-F0)*pow(1.0-NdV, 5.0);
  vec3 envOcc = uFill * 2.4 + uGroundCol * 0.25;
  if (uGiOn == 1) {   // probe GI replaces the fill term where the sky is hidden (and outdoors by night): docs/lighting.md
    float k = clamp(sv*uGiOpenK, 0.0, 1.0);
    if (k < 0.98) {
      vec3 gi, ge; float cov = giSample(w, Ng, N, R, gi, ge);
      if (cov > 0.0) {
        // probe light only ever adds to the analytic term (the old fill is the floor): a sealed room keeps today's look
        // by Day and the analytic term fades out by night, where lamps and window spill take over
        vec3 giIrr = gi*relK + irr*(1.0 - cov);
        irr = mix(max(giIrr, irr), irr, k);
        envOcc = max(ge*0.9 + envOcc*(1.0 - cov), envOcc);
      }
    }
  }
  vec3 envL = mix(envOcc, skyColor(R) * amb, sv);
  vec3 envSpec = envL * Fr * (1.0-rough*0.6) * 0.6 * cav;
  // material AO darkens the light arriving from the hemisphere, not the direct sun (legacy: everything)
  float aoD = mix(ao, 1.0, uAoIndirectOnly);
  vec3 r = direct*aoD + (irr*alb*kd + envSpec*mix(1.0, ao, 0.7)) * ao;
  if (uLightCount > 0) { vec3 ld, ls; localLights(w, N, Ng, V, rough, F0, ld, ls); r += (kd*alb/PI*ld + ls)*aoD; }
  gInd = irr*alb*kd*ao;
  return (any(isnan(r)) || any(isinf(r))) ? vec3(0.0) : r;   // never write a non-finite HDR value
}
)GLSL";

static const char* kMainVS = R"GLSL(
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNrm; layout(location=2) in vec3 aAlb;
layout(location=3) in vec4 aPar; layout(location=4) in vec3 aPc; layout(location=5) in vec3 aPn;
layout(location=6) in vec3 aEmi;   // material emission (L2); VAOs without the attribute read 0
layout(location=7) in vec4 aInst;  // PF2 HLOD cells: per-vertex building seed / wear / palette (w = 2); disabled elsewhere (w 0 or 1: uInst)
layout(location=8) in vec3 aLp; layout(location=9) in vec3 aLn;   // PF2 HLOD cells: the asset-local position / normal (patterns)
uniform mat4 uModel, uVP; uniform vec4 uSnapAp;   // V6: w = 1 for snap chains, xz = the chain's texture origin
uniform vec4 uInst;
out vec3 vW, vN, vAlb, vPc, vPn, vAp, vAn, vEmi; out vec4 vPar; flat out vec3 vEye; flat out vec4 vInstV;
invariant gl_Position;   // R1 zprepass: kDepthVS computes the same depth
void main(){
  vInstV = aInst.w > 1.5 ? vec4(aInst.xyz, 0.0) : uInst;
  vec4 w = uModel*vec4(aPos,1.0);
  vW = w.xyz; vN = mat3(uModel)*aNrm; vAlb = aAlb; vPar = aPar; vPc = aPc; vPn = aPn; vAp = aPos; vAn = aNrm; vEmi = aEmi; vEye = uCamPos;
  if (uSnapAp.w > 0.5) { vAp = vec3(w.x - uSnapAp.x, aPos.y, w.z - uSnapAp.z); vAn = vN; }   // V6: patterns continue across snapped joints
  if (aInst.w > 1.5) { vAp = aLp; vAn = aLn; }   // PF2 HLOD: world-space vertices, the copy's own pattern space
  gl_Position = uVP*w;
}
)GLSL";

static const char* kMainFS = R"GLSL(
in vec3 vW, vN, vAlb, vPc, vPn, vAp, vAn, vEmi; in vec4 vPar; flat in vec3 vEye;
uniform vec4 uTint; uniform float uTime; uniform int uWeather;   // 1: world structures get localized weathering
uniform float uGlow, uEmissiveK;   // L2 emission: per draw (DrawItem::glow) and global multipliers
uniform int uCapture;              // L2 probe capture: radiance + front-facing flag, no emission
uniform int uNoWallRelief;   // A/B: 1 = rough-wall relief off (Renderer::wallRelief = 0)
// uMatLayer (GX5 layered materials: 0 off (Low, probe captures), 1 on, 2 High) is declared with the L4 hooks (textures.cpp)
struct SDec { vec4 a, b, c; };   // GX5 static decals (static_decals.h): centre + type, normal + seed, half size + strength + variant
layout(std430, binding = 10) readonly buffer NmkStaticDecals { SDec gSD[]; };
uniform vec4 uRoomDec[16]; uniform vec4 uExtDec; uniform int uSDecOn;
uniform int uGxDebug;   // NMK_GX5_DEBUG=1: red = interior room found (green = past 16), blue = decals in its list
uniform ivec2 uRoomX;   // GX5: rooms past the 16 in uniforms, as records in gSD (a = room A, b = room B, c = decal range): first, count
layout(location=0) out vec4 oColor;
layout(location=1) out float oDist;   // eye distance, only stored during the first-person pass (depth of field)
layout(location=2) out vec4 oAmb;     // indirect light (screen-space AO), world pass only
layout(location=3) out vec4 oNR;      // world normal + roughness (screen-space reflections), world pass only
layout(location=4) out vec4 oVel;     // motion: static geometry leaves a = 0 (TAA reprojects it from depth)
vec2 faceUV(vec3 p, vec3 n){ vec3 a=abs(n); if (a.y>a.x && a.y>a.z) return p.xz; if (a.x>a.z) return vec2(p.z,p.y); return vec2(p.x,p.y); }
int domAxis(vec3 n){ vec3 a=abs(n); if (a.x>=a.y && a.x>=a.z) return 0; if (a.y>=a.z) return 1; return 2; }
// ---- detail filtering (docs/rendering.md#facade-shimmer). gPix = metres covered by this pixel. Procedural detail finer
// than about two pixels is faded toward its mean instead of being point-sampled: unfiltered 85-230 cycles/m grain and
// sub-pixel joints made facades crawl at 20-100 m, and the derivative bump amplified it.
float gPix = 0.001;
float res(float freq){ return 1.0 - smoothstep(0.18, 0.5, freq*gPix); }                       // 1 resolved .. 0 sub-pixel
float vnF(vec3 p, float freq){ float w = res(freq); return w <= 0.0 ? 0.5 : mix(0.5, vnoise(p), w); }
float fbmF(vec3 p, float f0){ float s = 0.0, a = 0.5; for (int i = 0; i < 4; i++){ float w = res(f0); s += a*(w <= 0.0 ? 0.5 : mix(0.5, vnoise(p), w)); p *= 2.03; f0 *= 2.03; a *= 0.5; } return s; }
uniform int uMatLod;   // R1 matlod: 1 = base pattern noise fades with the footprint of its world frequency (0: as before)
float vnL(vec3 p, float freq){ return uMatLod == 1 ? vnF(p, freq) : vnoise(p); }
float fbmL(vec3 p, float f0){ return uMatLod == 1 ? fbmF(p, f0) : fbm(p); }
float lineAA(float mask, float w, float mean){ return mix(mask, mean, smoothstep(0.6*w, 2.5*w, gPix)); }   // mask of feature width w (m)
// ---- walls, interiors and furniture (docs/interiors.md): per building copy parameters (SurfaceParams)
flat in vec4 vInstV;                   // seed (0..1), wear (0..2), palette variation (0..1), room count: uInst, or per vertex (PF2 HLOD)
#define uInst vInstV
uniform vec4 uRoomA[16], uRoomB[16];   // (min x, min z, max x, max z) / (floor y, ceiling y, wall finish, floor finish)
uniform vec4 uSillA[12], uSillB[12];   // (centre x, sill y, centre z, half width) / (outward normal x, z, -, -)
uniform int uSills;
int roomAt(vec3 p){
  int n = int(uInst.w + 0.5);
  for (int k = 0; k < 16; ++k) {
    if (k >= n) break;
    vec4 a = uRoomA[k], b = uRoomB[k];
    if (p.x >= a.x && p.x <= a.z && p.z >= a.y && p.z <= a.w && p.y >= b.x - 0.08 && p.y <= b.y) return k;
  }
  for (int j = 0; j < 96; ++j) {   // GX5: large buildings (the chateau): the rest from the decal buffer
    if (j >= uRoomX.y) break;
    vec4 a = gSD[uRoomX.x + j].a, b = gSD[uRoomX.x + j].b;
    if (p.x >= a.x && p.x <= a.z && p.z >= a.y && p.z <= a.w && p.y >= b.x - 0.08 && p.y <= b.y) return 16 + j;
  }
  return -1;
}
vec4 roomA(int k){ return k < 16 ? uRoomA[k] : gSD[uRoomX.x + k - 16].a; }
vec4 roomB(int k){ return k < 16 ? uRoomB[k] : gSD[uRoomX.x + k - 16].b; }
vec4 roomDec(int k){ return k < 16 ? uRoomDec[k] : gSD[uRoomX.x + k - 16].c; }
// pattern 1's course / block layout (wallfield.cpp stoneId mirrors it, so breaches follow these joints)
void stoneLayout(vec2 uv, out vec2 id, out float e, out float u, out float vr){
  float band = floor(uv.y / 0.62), fy = uv.y - band * 0.62;
  float s1 = 0.62 * (0.36 + 0.28 * hash12(vec2(band, 17.0)));
  float three = step(0.68, hash12(vec2(band, 29.0)));
  float s2 = mix(0.62, s1 + (0.62 - s1) * (0.45 + 0.1 * hash12(vec2(band, 31.0))), three);
  float cy0 = 0.0, cy1 = s1, row = band * 3.0;
  if (fy >= s1 && fy < s2) { cy0 = s1; cy1 = s2; row += 1.0; } else if (fy >= s2) { cy0 = s2; cy1 = 0.62; row += 2.0; }
  float rowH = cy1 - cy0;
  float L = max(rowH * 1.15, 0.30 + 0.55 * hash12(vec2(row, 3.0)));
  float x = uv.x + hash12(vec2(row, 7.0)) * 5.0;
  float k = floor(x / L);
  float jl = (hash12(vec2(k, row)) - 0.5) * 0.45 * L, jr = (hash12(vec2(k + 1.0, row)) - 0.5) * 0.45 * L;
  float left = k * L + jl, right = (k + 1.0) * L + jr;
  if (x < left) { right = left; k -= 1.0; left = k * L + (hash12(vec2(k, row)) - 0.5) * 0.45 * L; }
  else if (x > right) { left = right; k += 1.0; right = (k + 1.0) * L + (hash12(vec2(k + 1.0, row)) - 0.5) * 0.45 * L; }
  id = vec2(k, row);
  e = min(min(x - left, right - x), min(fy - cy0, cy1 - fy));
  u = clamp((x - left) / max(right - left, 1e-3), 0.0, 1.0);
  vr = clamp((fy - cy0) / max(rowH, 1e-3), 0.0, 1.0);
}
// sparse hairline cracks: thin ridges of a domain-warped noise, gated by a broad mask
float hairline(vec2 uv, float density){
  vec2 w = uv * 2.2 + vec2(fbm(vec3(uv * 1.1, 8.0)), fbm(vec3(uv * 1.1, 9.0))) * 1.6;
  float cn = abs(vnoise(vec3(w, 3.0)) - 0.5);
  float fw = max(0.006, fwidth(cn) * 1.2);   // under a pixel the line widens to the pixel but keeps its integrated darkness
  return (1.0 - smoothstep(0.0, fw, cn)) * (0.006 / fw) * smoothstep(0.7 - 0.06 * density, 0.84 - 0.06 * density, fbm(vec3(uv * 0.55, 12.0)));
}
// run-off below window sills (asset space): streaks hanging from the sill ends, fading with drop
float sillRunoff(vec3 p){
  float r = 0.0;
  for (int k = 0; k < 12; ++k) {
    if (k >= uSills) break;
    vec4 a = uSillA[k], b = uSillB[k];
    vec2 n = b.xy, t = vec2(-n.y, n.x);
    vec2 d = p.xz - a.xz;
    float off = dot(d, n), along = dot(d, t), drop = a.y - p.y;
    if (off < -0.2 || off > 0.9 || drop < 0.0 || drop > 2.4) continue;
    float w = a.w * (1.0 + 0.12 * drop);
    if (abs(along) > w) continue;
    float ends = 0.45 + 0.55 * smoothstep(a.w * 0.45, a.w * 0.95, abs(along));
    float st = smoothstep(0.35, 0.8, vnoise(vec3(along * 13.0, p.y * 0.6, float(k))));
    r = max(r, ends * st * (1.0 - smoothstep(0.1, 2.4, drop)) * (1.0 - smoothstep(0.85, 1.0, abs(along) / w)));
  }
  return r;
}
// GX5: 1 at a window's interior jamb / head / sill, fading over 0.5 m (asset space; sills: centre, half width, top)
float openingNear(vec3 p, vec3 an){
  float r = 0.0;
  for (int k = 0; k < 12; ++k) {
    if (k >= uSills) break;
    vec4 a = uSillA[k], b = uSillB[k];
    vec2 n = b.xy, t = vec2(-n.y, n.x);
    if (abs(dot(an.xz, n)) < 0.7) continue;
    vec2 d = p.xz - a.xz;
    if (abs(dot(d, n)) > 1.2) continue;
    float top = b.z > a.y ? b.z : a.y + 1.3;
    vec2 e = vec2(abs(dot(d, t)) - a.w, max(a.y - p.y, p.y - top));
    float dist = length(max(e, vec2(0.0))) + min(max(e.x, e.y), 0.0);
    r = max(r, 1.0 - smoothstep(0.0, 0.5, abs(dist)));
  }
  return r;
}
// herringbone: blocks N x 1 (in block widths); returns block-local coords (along, across) and an id
// (tiling: H_k = [k, k+N) x [k, k+1), V_k = [N+k, N+k+1) x [k+1-N, k+1), translated by m (N, -N); the strips
// overlap in x - y, so the neighbouring strip is tried too -- verified exact by sampling)
vec2 herring(vec2 p, float N, out vec3 id){
  float m0 = floor((p.x - p.y + 1.0) / (2.0 * N));
  for (int t = 0; t < 3; ++t) {
    float m = m0 + (t == 1 ? -1.0 : t == 2 ? 1.0 : 0.0);
    vec2 q = p + vec2(-m * N, m * N);
    float k = floor(q.y);
    if (q.x >= k && q.x < k + N) { id = vec3(k, m, 0.0); return vec2(q.x - k, q.y - k); }
    float j = floor(q.x) - N;
    if (q.y >= j + 1.0 - N && q.y < j + 1.0) { id = vec3(j, m, 1.0); return vec2(q.y - (j + 1.0 - N), q.x - (N + j)); }
  }
  id = vec3(0.0); return vec2(0.5);
}
// hexagonal tiles (inradius 0.5 per unit): xy offset from the tile centre, zw tile id
vec4 hexCell(vec2 p){
  const vec2 s = vec2(1.0, 1.7320508);
  vec4 hc = floor(vec4(p, p - vec2(0.5, 1.0)) / s.xyxy) + 0.5;
  vec4 h = vec4(p - hc.xy * s, p - (hc.zw + 0.5) * s);
  return dot(h.xy, h.xy) < dot(h.zw, h.zw) ? vec4(h.xy, hc.xy) : vec4(h.zw, hc.zw + 0.5);
}
float hexEdge(vec2 h){ h = abs(h); return 0.5 - max(dot(h, vec2(0.5, 0.8660254)), h.x); }
// wood grain along `along`, rings across: value 0..1
float woodGrain(float along, float across, float seed){
  return vnoise(vec3(along * 1.4, across * 52.0, seed)) * 0.55 + vnoise(vec3(along * 0.35, across * 150.0, seed + 3.0)) * 0.3
       + 0.15 * smoothstep(0.3, 0.7, fract(across * 18.0 + vnoise(vec3(along * 0.8, across * 6.0, seed)) * 2.5));
}
// ---- GX5 layered materials: inputs the room finishes set (pattern 20 walls, 21 floors / ceilings), read by gxLayer.
// gIn 1 = an interior surface with a known room: height above its floor, distance along the surface to the nearest room
// corner (walls) or room edge (floors, ceilings), and to the ceiling.
float fbm2(vec3 p){ return 0.66*vnoise(p) + 0.34*vnoise(p*2.03); }   // GX5: two octaves where four were too dear for a per-pixel mask
int gIn = 0; float gFloorY = 9.0, gCornerD = 9.0, gCeilD = 9.0;
float gWear = -1.0;   // per-room wear (pattern 20 sets it: rooms of one house range from kept to ruined); < 0 = the copy's
float gSeam = 1.0;   // seam / corner occlusion (also dims the direct lamp light a little there)
float gDetH = 0.0;   // detail relief (m), added to the derivative bump
// Mask-driven wear and grime over the base material (brief section 1): grime gathers at the floor, in room corners and
// under the ceiling (smoke), with world-space blotches, scaled by the building's wear, so one finish reads clean in one
// house and grimy in the next; roughness breakup on every surface; a high-frequency detail relief on walls; ~0.5 m seam
// occlusion at wall-wall, wall-floor and wall-ceiling joints (the flat lighting there made rooms read as boxes).
vec3 gDbg = vec3(0.0);   // NMK_GX5_DEBUG=2: decal tests passed (facing, plane, box)
int gRoom = -1;      // the room whose finish this fragment took (pattern 20 / 21), for its decal list
float gDecH = 0.0;   // decal relief (m)
// ---- GX5 static decals, evaluated in the material: each changes albedo, roughness, relief and occlusion of the surface
// under it (asset space, box tested against the decal's plane; only surfaces facing the decal's normal take it)
float crackPath(vec2 q, float seed, float w){   // q: x across (m), y along the crack (m); a wandering hairline with a branch
  float x = (fbm(vec3(q.y*2.2, seed*7.0, 1.0)) - 0.5)*0.22 + (vnoise(vec3(q.y*9.0, seed*3.0, 2.0)) - 0.5)*0.035;
  float d = abs(q.x - x);
  float bx = x + (q.y - 0.35)*0.5*(seed - 0.5)*2.0 + (vnoise(vec3(q.y*11.0, seed*5.0, 4.0)) - 0.5)*0.03;
  float br = (q.y > 0.35 + 0.3*seed) ? abs(q.x - bx) : 9.0;
  float fw = max(w, gPix*0.7);
  return max(1.0 - smoothstep(0.0, fw, d), (1.0 - smoothstep(0.0, fw*0.7, br))*0.8)*(w/fw);
}
void sdecals(vec3 an, inout vec3 alb, inout float rough, inout float ao){
  vec4 r = gRoom >= 0 && gIn == 1 ? roomDec(gRoom) : uExtDec;
  int first = int(r.x + 0.5), n = int(r.y + 0.5);
  n = min(n, uMatLayer == 2 ? 40 : 16);   // the list is priority sorted: Medium keeps the most telling 24
  for (int i = 0; i < 40; ++i) {
    if (i >= n) break;
    vec4 Db = gSD[first + i].b;                    // facing first, so the other two records are fetched only by decals on this wall
    vec3 nd = Db.xyz;
    gDbg.x = max(gDbg.x, 0.3);
    if (dot(an, nd) < 0.6) continue;
    gDbg.x = 1.0;
    vec4 Da = gSD[first + i].a;
    vec3 d = vAp - Da.xyz;
    if (abs(dot(d, nd)) > 0.25) continue;
    gDbg.y = 1.0;
    SDec D; D.a = Da; D.b = Db; D.c = gSD[first + i].c;
    vec3 t = abs(nd.y) > 0.7 ? vec3(1.0, 0.0, 0.0) : normalize(cross(vec3(0.0, 1.0, 0.0), nd));
    vec3 bt = cross(nd, t);
    vec2 hs = D.c.xy;
    vec2 q = vec2(dot(d, t), dot(d, bt));   // metres from the centre
    if (abs(q.x) > hs.x || abs(q.y) > hs.y) continue;
    gDbg.z = 1.0;
    vec2 u = q/hs;                           // -1..1
    int type = int(D.a.w + 0.5); float sd = D.b.w, k = D.c.z;
    float edgeF = (1.0 - smoothstep(0.75, 1.0, abs(u.x)))*(1.0 - smoothstep(0.75, 1.0, abs(u.y)));
    if (type == 0) {        // crack: a hairline down from its top end, plaster slightly lifted along it
      vec2 c = vec2(q.x, hs.y - q.y);
      float w = 0.0018*(1.0 - 0.55*smoothstep(0.0, 2.0*hs.y, c.y));
      float along = smoothstep(0.0, 0.05, c.y)*(1.0 - smoothstep(1.6*hs.y, 2.0*hs.y, c.y));
      float m = crackPath(c, sd, w)*along;
      float halo = crackPath(c, sd, 0.007)*along*(1.0 - m);   // grime caught along the crack: reads at range
      alb *= 1.0 - 0.65*m*k - 0.14*halo*k;
      gDecH -= 0.002*m*k; gDecH += 0.0003*halo*k; ao *= 1.0 - 0.35*m*k;
    } else if (type == 1) { // water stain: a blot with dark tide lines, yellowed inside, drips below on walls
      float wall = 1.0 - step(0.7, abs(nd.y));
      vec2 p = u*vec2(1.0, 1.0) + vec2(0.0, -0.25*wall);
      float rr = length(p*vec2(1.0, 0.85 + 0.3*wall)) + (fbm(vec3(u*2.2, sd*13.0)) - 0.5)*0.55;
      float body = 1.0 - smoothstep(0.62, 0.7, rr);
      float tide = (1.0 - smoothstep(0.0, 0.045, abs(rr - 0.66)))*0.9 + (1.0 - smoothstep(0.0, 0.03, abs(rr - 0.45 - 0.08*sd)))*0.45 + (1.0 - smoothstep(0.0, 0.025, abs(rr - 0.27)))*0.25;
      float drip = wall*(1.0 - smoothstep(0.0, 0.035 + 0.02*vnoise(vec3(q.x*7.0, sd, 1.0)), abs(fract(q.x*2.3 + sd*5.0) - 0.5)*0.43))
                 *step(fract(floor(q.x*2.3 + sd*5.0)*0.618 + sd), 0.45)*smoothstep(0.2, -0.95, u.y)*(1.0 - smoothstep(-0.95, -1.0, u.y))*step(u.y, 0.1);
      float m = clamp(max(body*0.7 + tide*body*0.7, drip*0.6)*edgeF, 0.0, 1.0);
      alb = mix(alb, alb*vec3(0.6, 0.5, 0.32), m*k); rough = mix(rough, min(1.0, rough + 0.08), m*k);
      gDecH += 0.0004*tide*body*k;
    } else if (type == 2) { // soot over a hearth: a plume widening upward, streaky, densest at the mantel and the ceiling
      float y = u.y*0.5 + 0.5;                // 0 bottom .. 1 top
      float wdt = 0.35 + 0.6*y + (fbm(vec3(q*3.0, sd*9.0)) - 0.5)*0.35;
      float plume = 1.0 - smoothstep(wdt*0.55, wdt, abs(u.x));
      float streaks = 0.75 + 0.25*vnoise(vec3(q.x*14.0, q.y*1.2, sd*3.0));
      float m = plume*streaks*(0.55 + 0.45*smoothstep(0.75, 1.0, y) + 0.4*(1.0 - smoothstep(0.0, 0.18, y)))*(1.0 - smoothstep(0.92, 1.0, abs(u.x)));
      m *= smoothstep(-1.0, -0.8, u.y);
      alb = mix(alb, alb*vec3(0.24, 0.22, 0.2), clamp(m*k, 0.0, 0.9)); rough = mix(rough, 0.97, m*k);
    } else if (type == 3) { // scuffs: horizontal rubs and knocks above the skirting (furniture, boots, mops)
      float rub = smoothstep(0.55, 0.85, vnoise(vec3(q.x*3.0, q.y*28.0, sd*11.0)))*smoothstep(0.4, 0.8, vnoise(vec3(q.x*1.2, sd*5.0, 3.0)));
      float knock = smoothstep(0.78, 0.9, vnoise(vec3(q*vec2(9.0, 14.0), sd*7.0)));
      float m = max(rub, knock)*edgeF;
      alb = mix(alb, alb*vec3(0.72, 0.7, 0.66), m*0.8*k); rough = mix(rough, rough*0.8, rub*k*0.6);
      gDecH -= 0.0006*knock*k;
    } else if (type == 4) { // bullet pocks: craters with a spalled ring showing the coat underneath
      float m = 0.0, ring = 0.0, hole = 0.0;
      for (int j = 0; j < 6; ++j) {
        vec2 c = (vec2(hash12(vec2(sd*31.0, float(j))), hash12(vec2(float(j), sd*17.0)))*2.0 - 1.0)*hs*0.75;
        float rad = 0.012 + 0.018*hash12(vec2(float(j)*3.0, sd));
        float dd = length(q - c) + (vnoise(vec3((q - c)*90.0, float(j))) - 0.5)*rad*0.6;
        hole = max(hole, 1.0 - smoothstep(rad*0.35, rad*0.6, dd));
        m = max(m, 1.0 - smoothstep(rad*0.6, rad, dd));
        ring = max(ring, (1.0 - smoothstep(rad, rad*2.6, dd))*smoothstep(0.35, 0.7, vnoise(vec3(q*60.0, float(j)))));
      }
      alb = mix(alb, vec3(0.62, 0.6, 0.55)*mix(1.0, 0.85, sd), max(m, ring*0.5)*k);
      alb *= 1.0 - 0.75*hole*k;
      gDecH -= (0.006*m + 0.001*ring)*k; ao *= 1.0 - 0.5*hole*k; rough = mix(rough, 0.98, m*k);
    } else if (type == 5) { // rain streaks below a sill
      float y = 1.0 - (u.y*0.5 + 0.5);        // 0 at the sill .. 1 at the bottom
      float s = smoothstep(0.45, 0.8, vnoise(vec3(q.x*16.0, y*0.6, sd*9.0)))*(0.6 + 0.4*vnoise(vec3(q.x*40.0, y*3.0, sd)));
      float m = s*(1.0 - smoothstep(0.35 + 0.6*vnoise(vec3(q.x*6.0, sd, 2.0)), 1.0, y))*(1.0 - smoothstep(0.85, 1.0, abs(u.x)));
      alb = mix(alb, alb*vec3(0.5, 0.5, 0.46), m*k); rough = mix(rough, rough*0.8, m*k);
    } else if (type == 6) { // picture outline: the paper under a frame kept its colour (cleaner, less faded) + the nail
      float frameM = (1.0 - smoothstep(0.88, 0.96, abs(u.x)))*(1.0 - smoothstep(0.88, 0.96, abs(u.y)));
      vec2 nail = q - vec2(0.0, hs.y*1.18);
      float nailH = 1.0 - smoothstep(0.0025, 0.004, length(nail));
      alb *= 1.0 + 0.09*frameM*k; alb = mix(alb, alb*vec3(1.0, 0.98, 0.94), frameM*0.5*k);
      alb *= 1.0 - 0.8*nailH; gDecH -= 0.002*nailH;
    } else {                // damp: a mottled grey-green tide rising from the floor in a corner, flaking at its rim
      float y = u.y*0.5 + 0.5;
      float edge = 1.0 - y + (fbm(vec3(q*3.0, sd*11.0)) - 0.5)*0.6;
      float m = smoothstep(0.15, 0.4, edge)*(1.0 - smoothstep(0.7, 1.0, abs(u.x)));
      float rim = (1.0 - smoothstep(0.0, 0.06, abs(edge - 0.2)))*m;
      float mould = smoothstep(0.62, 0.8, vnoise(vec3(q*45.0, sd)))*m;
      alb = mix(alb, alb*vec3(0.66, 0.7, 0.6), m*0.75*k); alb *= 1.0 - 0.35*mould*k - 0.12*rim*k;
      rough = mix(rough, 0.95, m*k); gDecH += 0.0005*rim*k;
    }
  }
}
void gxLayer(int pat, vec3 an, inout vec3 alb, inout float rough, inout float ao){
  float vert = 1.0 - smoothstep(0.45, 0.75, abs(an.y));
  float wearK = clamp(gWear >= 0.0 ? gWear : uInst.y, 0.0, 2.0);
  float rb = uMatLayer == 2 ? fbm2(vW*2.3 + 31.0) : vnoise(vW*2.3 + 31.0), rb2 = uMatLayer == 2 ? vnF(vW*19.0 + 7.0, 19.0) : 0.5;   // High: a finer octave
  rough = clamp(rough*(0.8 + 0.34*rb + 0.12*(rb2 - 0.5)), 0.05, 1.0);   // specular breakup: nothing has uniform roughness
  bool wall = pat == 20 || pat == 17 || pat == 3 || pat == 18 || pat == 19 || pat == 22 || pat == 1 || pat == 9 || pat == 11 || pat == 15;
  if (uMatLayer == 2 && wall && vert > 0.0) gDetH = ((vnF(vW*170.0, 170.0) - 0.5)*0.00022 + (vnF(vW*52.0 + 3.0, 52.0) - 0.5)*0.0004)*vert;
  if (gIn != 1) return;
  float n1 = vnoise(vW*1.7 + 2.0), n2 = uMatLayer == 2 ? fbm2(vW*0.9 + 5.0) : vnoise(vW*0.9 + 5.0);
  float y = gFloorY, cd = gCornerD, cl = gCeilD;
  float blot = smoothstep(0.55, 0.85, uMatLayer == 2 ? fbm2(vW*0.45 + uInst.x*9.0) : vnoise(vW*0.45 + uInst.x*9.0));
  float g;
  if (pat == 21) {   // floors: dirt along the skirting and into the corners; ceilings: smoke at the edges
    float edge = 1.0 - smoothstep(0.0, 0.18 + 0.3*n1, cd);
    g = clamp(edge*0.8 + blot*0.25, 0.0, 1.0)*(0.3 + 0.4*wearK);
    gSeam = 0.62 + 0.38*smoothstep(0.0, 0.45, cd);
  } else {
    float foot = 1.0 - smoothstep(0.0, 0.22 + 0.35*n1, y);
    float corner = 1.0 - smoothstep(0.0, 0.25 + 0.45*n2, cd);
    float top = 1.0 - smoothstep(0.0, 0.25 + 0.35*n1, cl);
    g = clamp(max(max(foot*0.85, corner*0.75), top*0.8) + blot*0.3, 0.0, 1.0)*(0.3 + 0.42*wearK)*vert;
    float hand = uMatLayer == 2 ? smoothstep(0.85, 1.0, y)*(1.0 - smoothstep(1.4, 1.7, y))*smoothstep(0.62, 0.85, vnoise(vW*vec3(3.0, 1.4, 3.0) + 11.0))*vert : 0.0;   // High
    rough = mix(rough, rough*0.72, hand*0.5*wearK); alb *= 1.0 - 0.05*hand*wearK;   // greasy hand-height smudges
    gSeam = (0.52 + 0.48*smoothstep(0.0, 0.5, cd))*(0.55 + 0.45*smoothstep(0.0, 0.45, y))*(0.6 + 0.4*smoothstep(0.0, 0.45, cl));
  }
  alb = mix(alb, alb*vec3(0.6, 0.55, 0.46), g*0.75);
  rough = mix(rough, min(1.0, rough + 0.14), g);
  ao *= gSeam;
}
void main(){
  if (uCapture == 1) gEyeOv = vEye;
  gPix = max(length(dFdx(vW)), length(dFdy(vW)));
  int pat = int(vPar.x + 0.5) % 100, tslot = texSlot(vPar.x, pat);   // L4: x = pattern + 100 * texture code (textures.cpp)
  vec3 alb = vAlb; float rough = vPar.y, metal = vPar.z; int grain = int(vPar.w+0.5);
  float h = 0.0, bump = 0.0, ao = 1.0;
  if (texReplace(tslot)) { alb *= 0.94 + 0.08*vnL(vAp*7.0, 7.0); }   // L4: the set replaces the pattern (textures.json mode "replace")
  else if (pat == 1) { // coursed limestone: courses of varying height, irregular joints, pillowed faces
    vec2 uv = faceUV(vAp, vAn);
    float band = floor(uv.y / 0.62), fy = uv.y - band * 0.62;
    float s1 = 0.62 * (0.36 + 0.28 * hash12(vec2(band, 17.0)));
    float three = step(0.68, hash12(vec2(band, 29.0)));
    float s2 = mix(0.62, s1 + (0.62 - s1) * (0.45 + 0.1 * hash12(vec2(band, 31.0))), three);
    float cy0 = 0.0, cy1 = s1, row = band * 3.0;
    if (fy >= s1 && fy < s2) { cy0 = s1; cy1 = s2; row += 1.0; } else if (fy >= s2) { cy0 = s2; cy1 = 0.62; row += 2.0; }
    float rowH = cy1 - cy0;
    float L = max(rowH * 1.15, 0.30 + 0.55 * hash12(vec2(row, 3.0)));
    float x = uv.x + hash12(vec2(row, 7.0)) * 5.0;
    float k = floor(x / L);
    float jl = (hash12(vec2(k, row)) - 0.5) * 0.45 * L, jr = (hash12(vec2(k + 1.0, row)) - 0.5) * 0.45 * L;
    float left = k * L + jl, right = (k + 1.0) * L + jr;
    if (x < left) { right = left; k -= 1.0; left = k * L + (hash12(vec2(k, row)) - 0.5) * 0.45 * L; }
    else if (x > right) { left = right; k += 1.0; right = (k + 1.0) * L + (hash12(vec2(k + 1.0, row)) - 0.5) * 0.45 * L; }
    vec2 id = vec2(k, row);
    float ex = min(x - left, right - x), ey = min(fy - cy0, cy1 - fy);
    float e = min(ex, ey) + (vnL(vec3(uv * 9.0, 3.0), 9.0) - 0.5) * 0.018;
    float mortar = 1.0 - smoothstep(0.005, 0.02, e);
    float pillow = smoothstep(0.0, 0.06, e);
    float u = clamp((x - left) / max(right - left, 1e-3), 0.0, 1.0);
    float tilt = (hash12(id + 3.0) - 0.5) * (u - 0.5) + (hash12(id + 9.0) - 0.5) * ((fy - cy0) / max(rowH, 1e-3) - 0.5);
    float pit = smoothstep(0.62, 0.8, vnL(vec3(uv * 22.0, id.x), 22.0));
    vec3 sc = alb * (0.74 + 0.4 * hash12(id));
    sc = mix(sc, sc * vec3(1.1, 0.98, 0.8), hash12(id + 11.0) * 0.85);          // ochre Caen limestone
    sc = mix(sc, sc * vec3(0.84, 0.88, 0.93), step(0.86, hash12(id + 5.0)) * 0.7);   // occasional grey stone
    sc *= 0.8 + 0.38 * fbmL(vec3(uv * 0.3, 9.0), 0.3);                                     // macro variation along the wall
    sc *= 0.9 + 0.16 * fbmL(vec3(uv * 16.0, 5.0), 16.0);
    sc *= 1.0 - 0.18 * pit;
    vec3 mcol = vec3(0.50, 0.48, 0.43) * (0.85 + 0.22 * fbmL(vec3(uv * 11.0, 2.0), 11.0));
    alb = mix(sc, mcol, mortar);
    float damp = 1.0 - smoothstep(-0.2, 1.0, vAp.y);
    alb *= mix(1.0, 0.7, damp);
    rough = mix(clamp(rough * (0.88 + 0.22 * hash12(id + 7.0)) - 0.1 * damp, 0.3, 1.0), 0.98, mortar);
    h = (1.0 - mortar) * (0.55 + 0.45 * pillow) + tilt * 0.35 - pit * 0.15;
    bump = 0.016; ao = 1.0 - mortar * 0.4;
  } else if (pat == 2 || pat == 10) { // wood planks / painted wood
    int na = domAxis(vPn); bool endGrain = grain == na; int g = grain; if (g == na) g = (na+1)%3;
    int ac = 3 - na - g;
    float along = vPc[g], across = vPc[ac];
    float pw = (pat == 10) ? 0.14 : 0.17;
    float pid = floor(across/pw); float fa = fract(across/pw);
    float seam = 1.0 - smoothstep(0.0, 0.05, min(fa, 1.0-fa));
    float gr = vnoise(vec3(along*1.2, across*55.0, pid*7.0))*0.6 + vnoise(vec3(along*0.3, across*140.0, pid))*0.4;
    vec3 wc = alb * (0.82 + 0.28*hash12(vec2(pid, float(g)))) * (0.8 + 0.35*gr);
    if (pat == 10) {
      float wear = smoothstep(0.58, 0.72, fbm(vPc*9.0 + pid));
      vec3 paint = alb * (0.9 + 0.1*fbmL(vPc*3.0, 3.0));
      vec3 bare = vec3(0.42,0.37,0.30)*(0.8+0.35*gr);
      wc = mix(paint, bare, wear);
      rough = mix(rough, 0.85, wear);
    }
    if (endGrain) {   // cut end: growth rings, darker, more absorbent
      vec2 q = vec2(vPc[(na+1)%3], vPc[(na+2)%3]) + vec2(hash12(vec2(pid, 1.0)), hash12(vec2(pid, 2.0))) * 0.3;
      float ring = fract(length(q) * 55.0 + vnL(vec3(q * 30.0, 1.0), 30.0) * 1.5);
      wc = alb * (0.62 + 0.18 * smoothstep(0.3, 0.6, ring));
      seam = 0.0; rough = min(1.0, rough + 0.15);
    }
    alb = mix(wc, wc*0.4, seam);
    rough = clamp(rough + (gr-0.5)*0.12, 0.2, 1.0);
    h = (1.0-seam) + gr*0.1; bump = 0.0025; ao = 1.0 - seam*0.3;
  } else if (pat == 3) { // lime plaster / whitewash
    float n = fbmL(vAp*1.7, 1.7);
    float trowel = vnoise(vec3(faceUV(vAp, vAn) * vec2(3.0, 9.0), 7.0));
    alb *= 0.9 + 0.12*n + 0.05*trowel;
    alb = mix(alb, alb*1.06, smoothstep(0.55, 0.7, fbmL(vAp*0.6 + 3.0, 0.6)));
    float dirt = 1.0 - smoothstep(0.0, 0.5, fract(vAp.y) * 0.0 + (vAp.y - floor(vAp.y/3.0)*3.0));
    alb *= mix(1.0, 0.86, dirt*0.0);
    h = fbmL(vAp*14.0, 14.0); bump = 0.0012;
    if (uMatLayer > 0 && abs(normalize(vAn).y) < 0.5) {   // GX5 two materials: the render over half-timber infill falls off
      // to the brick nogging behind (more on worn copies, at the foot and under the beams), with a broken rim
      vec2 uv = faceUV(vAp, vAn);
      float lm = fbmL(vec3(uv * 0.8, uInst.x * 23.0 + 61.0), 0.8) + 0.14 * (1.0 - smoothstep(0.0, 1.2, vAp.y)) + 0.05 * (vnL(vec3(uv * 8.0, 62.0), 8.0) - 0.5);
      float thr = 0.86 - 0.12 * uInst.y, fw = max(0.006, fwidth(lm) * 1.5);
      float lost = smoothstep(thr, thr + fw, lm), rimP = smoothstep(thr - 0.02, thr, lm) * (1.0 - lost);
      if (lost + rimP > 0.0) {
        float row = floor(uv.y / 0.075), xo = uv.x + mod(row, 2.0) * 0.1175, bi = floor(xo / 0.235);
        vec2 f = vec2(fract(xo / 0.235) * 0.235, fract(uv.y / 0.075) * 0.075);
        float e = min(min(f.x, 0.235 - f.x), min(f.y, 0.075 - f.y));
        float mort = lineAA(1.0 - smoothstep(0.004, 0.01, e), 0.008, 0.2);
        vec3 brick = mix(vec3(0.44, 0.22, 0.14) * (0.72 + 0.42 * hash12(vec2(bi, row) + uInst.x * 9.0)), vec3(0.52, 0.5, 0.45), mort);
        alb = mix(mix(alb, alb * 1.05, rimP), brick, lost); rough = mix(rough, 0.95, max(lost, rimP));
        h = mix(h, 0.0, lost); bump = 1.0; h = h * 0.0012 - 0.012 * lost - 0.003 * mort * lost + 0.001 * rimP; ao *= 1.0 - 0.25 * lost * mort;
      }
    }
  } else if (pat == 4) { // slate courses (part-local; grain axis = slope)
    int na = domAxis(vPn);
    if (na == 1) {
      float s = vPc[grain]; int ac = (grain == 0) ? 2 : 0; float x = vPc[ac];
      float course = floor(s/0.17); float fs = fract(s/0.17);
      float off = mod(course, 2.0)*0.13; float tid = floor((x+off)/0.26); float fx = fract((x+off)/0.26);
      float edge = min(fx, 1.0-fx)*0.26;
      float gap = 1.0 - smoothstep(0.004, 0.012, edge);
      float v = hash12(vec2(tid, course));
      alb *= (0.75 + 0.45*v) * (0.9+0.15*vnL(vPc*12.0, 12.0));
      alb = mix(alb, alb*0.35, gap);
      h = fs*0.6 + (1.0-gap)*0.4; bump = 0.01;
      rough = clamp(rough - 0.15*v + 0.1, 0.3, 1.0);
      ao = mix(0.75, 1.0, fs);
    }
  } else if (pat == 5) { // terracotta floor tiles
    vec2 uv = faceUV(vAp, vAn)/0.3;
    vec2 id = floor(uv), f = fract(uv);
    float e = min(min(f.x,1.0-f.x), min(f.y,1.0-f.y))*0.3;
    float grout = 1.0 - smoothstep(0.004, 0.01, e);
    alb *= (0.8 + 0.35*hash12(id)) * (0.9 + 0.15*fbmL(vec3(uv*3.0,1.0), 10.0));
    alb = mix(alb, vec3(0.45,0.42,0.38), grout);
    rough = mix(rough - 0.15*hash12(id+3.0), 0.95, grout);
    h = 1.0-grout; bump = 0.002; ao = 1.0 - grout*0.3;
  } else if (pat == 6) { // iron / steel
    float n = fbmL(vAp*6.0, 6.0);
    alb *= 0.8 + 0.3*n; rough = clamp(rough + (n-0.5)*0.3, 0.2, 1.0);
    h = n; bump = 0.0008;
  } else if (pat == 8) { // fabric
    float n = vnL(vAp*140.0, 140.0)*0.5 + vnL(vAp*9.0, 9.0)*0.5;
    alb *= 0.86 + 0.18*n; h = n; bump = 0.0006;
  } else if (pat == 9) { // brick
    vec2 uv = faceUV(vAp, vAn);
    float row = floor(uv.y/0.075); float off = mod(row,2.0)*0.115;
    float id = floor((uv.x+off)/0.23);
    vec2 f = vec2(fract((uv.x+off)/0.23)*0.23, fract(uv.y/0.075)*0.075);
    float e = min(min(f.x,0.23-f.x), min(f.y,0.075-f.y));
    float mortar = 1.0 - smoothstep(0.004, 0.009, e);
    alb *= 0.75 + 0.4*hash12(vec2(id,row));
    alb = mix(alb, vec3(0.55,0.53,0.5), mortar);
    h = 1.0-mortar; bump = 0.006;
  } else if (pat == 11) { // poured concrete: board formwork, aggregate, damp foot
    float n = fbmL(vAp*1.6, 1.6);
    float bj = abs(fract(vAp.y/0.22 + hash12(floor(vAp.xz*0.5))*0.0)-0.5)*0.22;
    float board = 1.0 - smoothstep(0.004, 0.012, 0.11 - bj);
    float grainB = vnoise(vec3(faceUV(vAp, vAn).x*2.0, vAp.y*40.0, 3.0));
    float agg = smoothstep(0.72, 0.85, vnL(vAp*70.0, 70.0));
    alb *= 0.8 + 0.28*n; alb *= 1.0 - board*0.12 - grainB*0.05;
    alb = mix(alb, alb*vec3(1.15,1.12,1.05), agg*0.5);
    float damp = 1.0 - smoothstep(-0.3, 0.9, vAp.y);
    alb *= mix(1.0, 0.72, damp); rough = mix(rough, rough*0.8, damp);
    h = n*0.4 - board*0.6 + agg*0.2 + grainB*0.1; bump = 0.0045;
  } else if (pat == 12 || pat == 13 || pat == 14) {
    // path surfaces in path coordinates (x across, z along, metres): fixed texel density at any length / width.
    // vPar.w = edge factor (0 at the border, 1 from 0.45 m in): soil / verge transition at the margins.
    vec2 q = vAp.xz; float edge = clamp(vPar.w, 0.0, 1.0);
    vec3 soil = vec3(0.17, 0.135, 0.095) * (0.8 + 0.4 * fbmL(vec3(q * 0.9, 4.0), 0.9));
    vec3 verge = vec3(0.16, 0.21, 0.09) * (0.8 + 0.4 * vnL(vec3(q * 3.0, 8.0), 3.0));
    if (pat == 12) {
      // gravel: Voronoi stones 1.5-4 cm (two scales), grey / ochre / flint, soil showing between, pillowed tops
      float best = 9.0, second = 9.0; vec2 id = vec2(0.0);
      vec2 g = q / 0.028;
      vec2 gi = floor(g);
      for (int j = -1; j <= 1; ++j) for (int i = -1; i <= 1; ++i) {
        vec2 c = gi + vec2(i, j);
        vec2 o = c + vec2(hash12(c), hash12(c + 17.0));
        float d = length(g - o);
        if (d < best) { second = best; best = d; id = c; } else if (d < second) second = d;
      }
      float gap = second - best;                       // 0 on cell borders
      float stone = smoothstep(0.05, 0.28, gap);
      float tint = hash12(id + 3.0);
      vec3 sc = tint < 0.45 ? vec3(0.40, 0.39, 0.37) : tint < 0.8 ? vec3(0.44, 0.39, 0.31) : vec3(0.27, 0.27, 0.29);
      sc *= 0.75 + 0.45 * hash12(id + 9.0);
      float coarse = smoothstep(0.55, 0.75, vnL(vec3(q * 1.3, 2.0), 1.3));   // patches of coarser / thinner stone
      vec3 between = mix(vec3(0.22, 0.2, 0.17), soil, 0.5);
      alb = mix(between, sc, stone * (0.7 + 0.3 * coarse));
      alb *= 0.88 + 0.22 * fbmL(vec3(q * 0.35, 6.0), 0.35);
      rough = mix(0.95, 0.78 + 0.12 * hash12(id), stone);
      h = stone * (0.6 + 0.4 * hash12(id + 5.0)) + 0.25 * coarse; bump = 0.012;
      ao = mix(0.75, 1.0, stone);
      alb = mix(mix(soil, verge, smoothstep(0.35, 0.0, edge) * vnL(vec3(q * 2.0, 1.0), 2.0)), alb, smoothstep(0.0, 0.55, edge + 0.25 * (vnL(vec3(q * 4.0, 3.0), 4.0) - 0.5)));
    } else if (pat == 13) {
      // dirt: compacted soil, two worn wheel ruts 1.5 m apart, fine crumb, damp patches, patchy grass at the margin
      float rutA = exp(-pow((abs(q.x) - 0.75) / 0.18, 2.0));
      float crown = exp(-pow(q.x / 0.35, 2.0));
      float crumb = vnL(vec3(q * 38.0, 2.0), 38.0), clod = smoothstep(0.62, 0.8, vnL(vec3(q * 9.0, 5.0), 9.0));
      float damp = smoothstep(0.55, 0.78, fbmL(vec3(q * 0.25, 7.0), 0.25)) * (0.4 + 0.6 * rutA);
      alb = soil * (0.82 + 0.3 * crumb) * (1.0 - 0.22 * rutA) * (1.0 + 0.1 * crown) * (0.85 + 0.3 * fbmL(vec3(q * 0.18, 3.0), 0.18));
      alb = mix(alb, alb * vec3(0.62, 0.6, 0.58), damp);
      vec3 grassTint = mix(alb, verge, smoothstep(0.5, 0.8, vnL(vec3(q * 1.6, 9.0), 1.6)) * 0.6);
      alb = mix(alb, grassTint, crown * 0.6);   // grass in the centre strip between the ruts
      rough = mix(0.97, 0.8, damp);
      h = crumb * 0.25 + clod * 0.4 - rutA * 0.6 + crown * 0.2; bump = 0.008;
      alb = mix(mix(soil, verge, smoothstep(0.5, 0.0, edge)), alb, smoothstep(0.0, 0.7, edge + 0.35 * (vnL(vec3(q * 3.0, 4.0), 3.0) - 0.5)));
    } else {
      // concrete slabs (2 m along the path): expansion seams, exposed aggregate, chipped seam edges, staining
      float sz = q.y / 2.0; float seamD = abs(fract(sz + 0.5) - 0.5) * 2.0;
      float seam = 1.0 - smoothstep(0.006, 0.014, seamD);
      float slabId = floor(sz + 0.5);
      float n = fbmL(vec3(q * 0.8, slabId), 0.8);
      float agg = smoothstep(0.7, 0.86, vnL(vec3(q * 55.0, 1.0), 55.0));
      float chip = smoothstep(0.62, 0.8, vnL(vec3(q * 7.0, slabId + 3.0), 7.0)) * (1.0 - smoothstep(0.01, 0.06, seamD));
      float stain = smoothstep(0.58, 0.8, fbmL(vec3(q * 0.4, 11.0), 0.4));
      alb = vec3(0.40, 0.39, 0.37) * (0.86 + 0.22 * n) * (0.92 + 0.12 * hash12(vec2(slabId, 7.0)));
      alb = mix(alb, alb * vec3(1.12, 1.08, 1.0), agg * 0.5);
      alb = mix(alb, alb * vec3(0.78, 0.76, 0.7), stain * 0.6);
      alb = mix(alb, vec3(0.3, 0.28, 0.25), max(seam, chip * 0.7));
      rough = mix(0.86, 0.95, max(seam, chip)) - 0.06 * stain;
      h = n * 0.3 + agg * 0.15 - seam * 0.8 - chip * 0.5; bump = 0.005;
      ao = 1.0 - 0.35 * seam;
      alb = mix(soil, alb, smoothstep(0.0, 0.12, edge));   // a thin soil line where the slab meets the ground
    }
  } else if (pat == 15) { // bunker concrete (docs/bunkers.md#material): horizontal board formwork with board-to-board
    // tone, joint fins and grain imprint, pour lifts, form-tie holes, mottling; darker foot and run-off below the roof
    // lip (grain = lip height in dm, asset space with the origin on the ground)
    vec3 an = normalize(vAn);
    float vert = 1.0 - smoothstep(0.45, 0.75, abs(an.y));
    float along = faceUV(vAp, vAn).x;
    float bi = floor(vAp.y / 0.24), fy = fract(vAp.y / 0.24);
    float seg = floor((along + hash12(vec2(bi, 3.0)) * 3.0) / 3.0);
    float tone = 0.9 + 0.17 * hash12(vec2(bi, seg));
    float ej = min(fy, 1.0 - fy) * 0.24;
    float joint = (1.0 - smoothstep(0.002, 0.009, ej)) * vert;
    float grainB = vnoise(vec3(along * 1.3, vAp.y * 34.0, bi)) * vert;
    float lift = floor(vAp.y / 1.2);
    float liftLine = (1.0 - smoothstep(0.004, 0.02, abs(fract(vAp.y / 1.2 + 0.5) - 0.5) * 1.2)) * vert;
    vec2 tp = vec2(fract(along / 0.9 + 0.5) - 0.5, fract(vAp.y / 0.72 + 0.5) - 0.5) * vec2(0.9, 0.72);
    float tie = (1.0 - smoothstep(0.009, 0.016, length(tp))) * vert;
    float mott = fbmL(vAp * 0.45, 0.45);
    alb *= mix(1.0, tone, vert) * (0.93 + 0.1 * hash12(vec2(lift, 9.0))) * (0.68 + 0.6 * mott);
    alb *= 1.0 - joint * 0.22 - grainB * 0.06 - liftLine * 0.18 - tie * 0.5;
    float nn = vnL(vAp * 1.1, 1.1);
    float foot = (1.0 - smoothstep(0.0, 0.7 + 0.5 * nn, vAp.y)) * vert;
    alb = mix(alb, alb * vec3(0.4, 0.4, 0.31), foot);
    float lip = float(grain) * 0.1;
    if (lip > 0.5) {
      float d = lip - vAp.y;
      float band = d > 0.0 ? 1.0 - smoothstep(0.0, 0.45, d) : 0.0;
      float streak = d > 0.0 ? smoothstep(0.52, 0.86, vnoise(vec3(along * 2.6, vAp.y * 0.18, 5.0))) * (1.0 - smoothstep(0.0, 1.8 + 0.8 * nn, d)) : 0.0;
      alb *= 1.0 - vert * (0.5 * band + 0.55 * streak);
    }
    float top = smoothstep(0.6, 0.9, an.y);
    float lichen = top * smoothstep(0.45, 0.75, vnL(vAp * 1.7 + 3.0, 1.7));
    alb = mix(alb, vec3(0.2, 0.21, 0.15), lichen * 0.45);
    rough = clamp(rough + joint * 0.06 - foot * 0.08, 0.3, 1.0);
    h = mott * 0.3 - joint * 0.6 + grainB * 0.15 - tie * 0.8 - liftLine * 0.3; bump = 0.004;
    ao = 1.0 - joint * 0.15 - tie * 0.4;
  } else if (pat == 16) { // camouflage net (docs/bunkers.md#camouflage): disruptive olive / brown / dark blotches,
    // leafy scrim lumps with darker gaps, garnish tufts; part-local so the pattern follows each draped sheet
    vec2 uv = faceUV(vPc, vPn);
    float b = fbmL(vec3(uv * 0.7, 7.0), 0.7);
    vec3 c = mix(vec3(0.075, 0.085, 0.035), vec3(0.15, 0.125, 0.06), smoothstep(0.42, 0.55, b));
    c = mix(c, vec3(0.045, 0.045, 0.03), smoothstep(0.62, 0.7, b));
    float strand = smoothstep(0.3, 0.62, vnL(vec3(uv * 16.0, 5.0), 16.0));   // leafy scrim lumps, no regular grid (aliases)
    float garn = smoothstep(0.55, 0.8, vnL(vec3(uv * 5.0, 2.0), 5.0));
    alb = c * (0.45 + 0.55 * max(strand, garn)) * (0.85 + 0.3 * vnL(vec3(uv * 40.0, 1.0), 40.0));
    rough = 0.95; h = max(strand, garn); bump = 0.003; ao = 0.75 + 0.25 * h;
  } else if (pat == 17) { // lime render over rubble (docs/interiors.md, docs/rendering.md#rough-walls): broad drift, mineral
    // grain, pits, hairline cracks, uneven coats; mid-scale relief: the render bulging over the rubble behind it, trowel
    // lumps, repair patches standing proud with a lip, spalled patches showing pillowed rubble behind a render edge, grime
    // in the hollows and run-off streaks; damp foot, run-off below sills. Detail under a pixel fades to its mean.
    vec2 uv = faceUV(vAp, vAn);
    vec3 an = normalize(vAn);
    float vert = 1.0 - smoothstep(0.45, 0.75, abs(an.y));
    float pv = uInst.z, wear = uInst.y, sd = uInst.x * 17.0;
    float broad = fbmL(vec3(uv * 0.33, sd), 0.33);
    float coat = fbmF(vec3(uv * 1.3, 4.0 + sd), 1.3);
    float grn = vnF(vec3(uv * 85.0, 1.0), 85.0) * 0.6 + vnF(vec3(uv * 230.0, 2.0), 230.0) * 0.4;
    float pit = lineAA(smoothstep(0.72, 0.88, vnL(vec3(uv * 38.0, 5.0), 38.0)), 0.008, 0.08);
    float crack = hairline(uv + sd, wear) * vert;
    float bulge = fbm(vec3(uv * vec2(0.9, 1.2), 40.0 + sd));
    float lump = fbmF(vec3(uv * 4.0, 41.0 + sd), 4.0);
    float pm = fbmL(vec3(uv * 0.6, 33.0 + sd), 0.6) + 0.04 * (lump - 0.5);
    float patchM = smoothstep(0.62, 0.62 + max(0.008, fwidth(pm) * 1.5), pm) * vert;
    float patchLip = smoothstep(0.595, 0.62, pm) * (1.0 - patchM) * vert;
    float sp = fbmL(vec3(uv * 0.85, 21.0 + sd), 0.85) + 0.16 * (1.0 - smoothstep(0.0, 1.3, vAp.y)) + 0.05 * vnL(vec3(uv * 9.0, 6.0), 9.0);
    float thr = 0.83 - 0.06 * wear;
    float spall = smoothstep(thr, thr + max(0.012, fwidth(sp) * 1.5), sp) * vert;
    float rim = smoothstep(thr - 0.035, thr, sp) * (1.0 - spall) * vert;
    vec3 lime = alb * (0.9 + 0.2 * (broad - 0.5) * (0.5 + pv)) * (0.96 + 0.06 * coat) * (0.95 + 0.07 * grn);
    lime *= 1.0 - 0.1 * pit;
    lime = mix(lime, lime * vec3(0.92, 0.92, 0.9), smoothstep(0.55, 0.78, coat) * 0.6);
    lime = mix(lime, alb * vec3(1.05, 1.05, 1.07) * (0.97 + 0.04 * grn), patchM * 0.75);   // newer lime: cleaner, cooler
    vec2 id; float e, u, vr; stoneLayout(uv, id, e, u, vr);
    float mortar = lineAA(1.0 - smoothstep(0.006, 0.026, e), 0.016, 0.16);
    float dome = sin(u * PI) * sin(vr * PI);
    vec3 stoneC = vec3(0.29, 0.26, 0.205) * (0.82 + 0.3 * hash12(id)) * (0.9 + 0.15 * fbmF(vec3(uv * 9.0, 2.0), 9.0));
    stoneC = mix(stoneC, vec3(0.36, 0.34, 0.3), mortar * 0.8);
    alb = mix(lime, stoneC, spall);
    float hollow = clamp((0.5 - bulge) * 2.2, 0.0, 1.0) * (1.0 - patchM);
    float streak = smoothstep(0.55, 0.86, vnF(vec3(uv.x * 7.0, uv.y * 0.35, 13.0 + sd), 7.0)) * vert * (1.0 - spall);
    alb *= 1.0 - 0.16 * rim - 0.2 * crack - 0.08 * hollow - 0.1 * streak * (0.5 + 0.5 * wear) - 0.05 * patchLip;
    float nn = vnL(vAp * 1.3, 1.3);
    float damp = (1.0 - smoothstep(0.0, 0.45 + 0.5 * nn, vAp.y)) * vert;
    alb = mix(alb, alb * vec3(0.7, 0.71, 0.64), damp * 0.85);
    float ro = sillRunoff(vAp) * vert;
    alb = mix(alb, alb * vec3(0.74, 0.75, 0.7), ro * (0.4 + 0.3 * wear));
    rough = clamp(mix(rough, 0.97, spall) - 0.06 * damp - 0.04 * patchM, 0.4, 1.0);
    // relief (m): bulges +-1.2 cm, trowel lumps 2 mm, patches 3 mm proud with a 1.5 mm lip, the render 1.6 cm over the
    // rubble face, spalled stones pillowed 2 cm behind 8 mm recessed mortar, cracks 3 mm deep
    float hR = 0.024 * (bulge - 0.5) + 0.002 * (lump - 0.47) + 0.003 * patchM + 0.0015 * patchLip + 0.0006 * coat + 0.0003 * grn - 0.0008 * pit - 0.003 * crack + 0.002 * rim;
    float hS = -0.016 + mix(0.02 * smoothstep(0.0, 0.12, e) * (0.7 + 0.3 * dome) - lineAA(1.0 - smoothstep(0.006, 0.026, e), 0.008, 0.0) * 0.008, 0.012, smoothstep(0.04, 0.12, gPix));
    h = mix(hR, mix(hS, hR - 0.004, smoothstep(0.004, 0.016, gPix)), spall); bump = 1.0;   // the 1.6 cm spall step fades by ~15 m
    ao = 1.0 - 0.22 * rim - 0.25 * spall * mortar;
    gCavity = 1.0 - 0.22 * hollow - 0.12 * crack - spall * (0.15 + 0.3 * mortar) * (1.0 - 0.5 * dome);
  } else if (pat == 18) { // brickwork, Flemish bond (docs/interiors.md): cohesive palette, neighbouring-value lime
    // mortar recessed, a few over-burnt headers, chipped arrises, grime and streaks; corners continue through faceUV.
    // Joints fade to the blend under a pixel and the courses to the wall's mean beyond ~50 m (no moire).
    vec2 uv = faceUV(vAp, vAn);
    float bh = 0.075, bl = 0.235, hl = 0.115, unit = bl + hl;
    float row = floor(uv.y / bh);
    float xo = uv.x + mod(row, 2.0) * unit * 0.5;
    float ui = floor(xo / unit), fu = xo - ui * unit;
    float hdr = step(bl, fu);
    float x0 = mix(0.0, bl, hdr), x1 = mix(bl, unit, hdr);
    vec2 id = vec2(ui * 2.0 + hdr, row);
    float e = min(min(fu - x0, x1 - fu), min(uv.y - row * bh, (row + 1.0) * bh - uv.y)) + (vnF(vec3(uv * 40.0, 2.0), 40.0) - 0.5) * 0.003;
    float aa = smoothstep(0.0025, 0.008, gPix);    // joints under a pixel
    float aaB = smoothstep(0.02, 0.05, gPix);      // courses under ~1.5 px
    float mortarS = 1.0 - smoothstep(0.0035, 0.0075, e);
    float mortar = mix(mortarS, 0.12, aa);
    float pv = uInst.z, v = mix(hash12(id + uInst.x * 31.0), 0.5, aaB);
    vec3 bc = alb * (1.0 + 0.17 * pv * (v - 0.5));
    bc = mix(bc, bc * vec3(0.72, 0.68, 0.72), hdr * step(0.62, hash12(id + 5.0)) * 0.75 * (1.0 - aaB));
    bc = mix(bc, bc * vec3(1.08, 1.03, 0.92), step(0.86, hash12(id + 9.0)) * 0.5 * pv * (1.0 - aaB));
    bc *= 0.9 + 0.14 * fbmF(vec3(uv * 18.0, id.x), 18.0);
    bc *= 0.9 + 0.12 * fbmL(vec3(uv * 0.4, 3.0 + uInst.x * 7.0), 0.4);
    vec3 mcol = mix(alb * 0.95, vec3(0.6, 0.57, 0.51), 0.55) * (0.9 + 0.1 * vnF(vec3(uv * 60.0, 1.0), 60.0));
    float chip = smoothstep(0.74, 0.88, vnL(vec3(uv * 30.0, v * 7.0), 30.0)) * (1.0 - smoothstep(0.0, 0.012, e)) * (1.0 - aa);
    alb = mix(bc, mcol, max(mortar, chip * 0.55));
    float streak = smoothstep(0.58, 0.86, vnF(vec3(uv.x * 6.0, uv.y * 0.3, 19.0), 6.0)) * smoothstep(0.5, 2.5, vAp.y);
    float soot = smoothstep(0.55, 0.8, fbmL(vec3(uv * 0.7, 23.0 + uInst.x * 3.0), 0.7));
    alb *= (1.0 - 0.12 * streak) * (1.0 - 0.1 * soot * uInst.y) * (1.0 - 0.06 * mortar);
    rough = mix(clamp(rough + (v - 0.5) * 0.1, 0.5, 1.0), 0.95, mortar);
    // relief (m): mortar recessed 6 mm, brick faces pillowed 2 mm with 4 mm arris rounding, chips 3 mm, fired texture
    float arr = mix(1.0 - smoothstep(0.0, 0.012, e), 0.25, aa);
    float hb = 0.002 * (1.0 - arr) - 0.004 * arr * arr + 0.0012 * (fbmF(vec3(uv * 25.0, 4.0), 25.0) - 0.47) - 0.003 * chip;
    h = mix(hb, -0.006, mix(mortarS, 0.12, smoothstep(0.0015, 0.005, gPix))); bump = 1.0;   // relief fades before the tone
    ao = 1.0 - mortar * 0.22;
    gCavity = 1.0 - 0.3 * mortar - 0.08 * arr;
  } else if (pat == 19) { // dressed limestone (docs/interiors.md): pattern 1's layout, fine lime joints close in
    // value to the stone, low block-to-block contrast, tooling striations, worn arrises; blocks gently pillowed and
    // tilted, joints recessed, chips, grime in the joints and streaks. Joints / tooling fade under a pixel.
    vec2 uv = faceUV(vAp, vAn);
    vec2 id; float e, u, vr; stoneLayout(uv, id, e, u, vr);
    e += (vnF(vec3(uv * 14.0, 3.0), 14.0) - 0.5) * 0.004;
    float joint = lineAA(1.0 - smoothstep(0.002, 0.007, e), 0.004, 0.03);
    float arris = lineAA(1.0 - smoothstep(0.004, 0.022, e), 0.012, 0.08);
    float pv = uInst.z;
    vec3 sc = alb * (1.0 + 0.12 * pv * (hash12(id) - 0.5)) * (0.92 + 0.12 * fbmL(vec3(uv * 0.5, 4.0), 0.5));
    sc = mix(sc, sc * vec3(1.05, 1.0, 0.9), hash12(id + 11.0) * 0.35 * pv);
    float tool = vnF(vec3((uv.x + uv.y * 0.35) * 180.0, uv.y * 4.0, id.x), 180.0);
    float patina = fbmF(vec3(uv * 3.0, id.y), 3.0);
    sc *= (0.94 + 0.06 * tool) * (0.9 + 0.12 * patina);
    float chip = smoothstep(0.7, 0.86, vnF(vec3(uv * 16.0, id.x + id.y), 32.0)) * arris;
    vec3 jc = mix(sc, vec3(0.56, 0.54, 0.48), 0.4) * 0.93;
    alb = mix(sc, jc, max(joint, chip * 0.5));
    alb *= 1.0 - 0.08 * arris;
    float streak = smoothstep(0.6, 0.88, vnF(vec3(uv.x * 5.0, uv.y * 0.28, 29.0), 5.0)) * smoothstep(0.4, 2.2, vAp.y);
    alb *= (1.0 - 0.1 * streak) * (1.0 - 0.1 * joint);
    float damp = 1.0 - smoothstep(-0.2, 0.9, vAp.y);
    alb *= mix(1.0, 0.76, damp);
    rough = clamp(rough + 0.05 * (tool - 0.5) + 0.06 * joint, 0.4, 1.0);
    // relief (m): blocks pillowed 6 mm and tilted +-3 mm, arrises worn 6 mm down into 5 mm recessed joints, chips 4 mm,
    // tooling 0.4 mm
    float dome = sin(u * PI) * sin(vr * PI);
    float tilt = (hash12(id + 3.0) - 0.5) * (u - 0.5) + (hash12(id + 9.0) - 0.5) * (vr - 0.5);
    // per-block pillow and tilt reach zero at the worn arris (continuous across joints); joint and arris relief fade
    // under the pixel (as for rubble, 9.5)
    float blockH = mix(smoothstep(0.0, 0.03, e) * (0.006 * dome + 0.012 * tilt), 0.003, smoothstep(0.04, 0.12, gPix));
    float jointH = lineAA(1.0 - smoothstep(0.002, 0.007, e), 0.004, 0.0), arrisH = lineAA(1.0 - smoothstep(0.004, 0.022, e), 0.006, 0.0);
    float hs = blockH - 0.006 * arrisH * arrisH + 0.0004 * tool - 0.004 * chip + 0.001 * (patina - 0.47);
    h = mix(hs, -0.005, jointH); bump = 1.0;
    ao = 1.0 - joint * 0.2 - arris * 0.08;
    gCavity = 1.0 - 0.3 * joint - 0.1 * arris;
  } else if (pat == 22) { // rubble masonry, muted (docs/interiors.md): pattern 1's layout; mortar a recessed
    // neighbouring value (never black), cohesive palette, softer stone-to-stone alternation; stones pillowed 2-4 cm and
    // tilted with knobbly quarry faces, mortar recessed 1.5 cm, grime in the joints, streaks. Fine terms fade to the mean.
    vec2 uv = faceUV(vAp, vAn);
    vec2 id; float e, u, vr; stoneLayout(uv, id, e, u, vr);
    e += (vnF(vec3(uv * 9.0, 3.0), 9.0) - 0.5) * 0.02;
    float mortar = lineAA(1.0 - smoothstep(0.005, 0.024, e), 0.015, 0.18);
    float pillow = lineAA(smoothstep(0.0, 0.07, e), 0.05, 0.75);
    float dome = sin(u * PI) * sin(vr * PI);
    float tilt = (hash12(id + 3.0) - 0.5) * (u - 0.5) + (hash12(id + 9.0) - 0.5) * (vr - 0.5);
    float pit = lineAA(smoothstep(0.72, 0.86, vnL(vec3(uv * 55.0, id.x), 55.0)), 0.006, 0.06);
    float face = fbmF(vec3(uv * 6.0, id.x * 1.3 + id.y), 6.0);
    float pv = uInst.z;
    vec3 sc = alb * 0.84 * (1.0 + 0.24 * pv * (hash12(id + uInst.x * 13.0) - 0.5));
    sc = mix(sc, sc * vec3(1.06, 0.99, 0.86), hash12(id + 11.0) * 0.55 * pv);
    sc = mix(sc, sc * vec3(0.9, 0.93, 0.97), step(0.88, hash12(id + 5.0)) * 0.45);
    sc *= 0.86 + 0.26 * fbmL(vec3(uv * 0.3, 9.0 + uInst.x * 5.0), 0.3);
    sc *= 0.92 + 0.12 * fbmF(vec3(uv * 16.0, 5.0), 16.0);
    sc *= (1.0 - 0.08 * pit) * (0.95 + 0.1 * face);
    vec3 mcol = mix(alb * 0.78, vec3(0.44, 0.42, 0.37), 0.5) * (0.88 + 0.18 * fbmF(vec3(uv * 11.0, 2.0), 11.0));
    alb = mix(sc, mcol, mortar);
    alb *= 1.0 - 0.1 * (1.0 - pillow) * (1.0 - mortar);
    float streak = smoothstep(0.6, 0.88, vnF(vec3(uv.x * 5.0, uv.y * 0.3, 17.0), 5.0)) * smoothstep(0.4, 2.0, vAp.y);
    alb *= (1.0 - 0.12 * streak) * (1.0 - 0.1 * mortar);
    float damp = 1.0 - smoothstep(-0.2, 1.0, vAp.y);
    alb *= mix(1.0, 0.78, damp);
    rough = mix(clamp(rough * (0.9 + 0.2 * hash12(id + 7.0)) - 0.08 * damp, 0.3, 1.0), 0.97, mortar);
    // relief (m): stones pillowed 2-4 cm (per stone) and tilted +-1.5 cm, knobbly faces 6 mm, pits 2 mm; mortar 1.5 cm back
    // Every per-stone term goes to zero at the joint through the rounded arris (14 cm), so the height is continuous
    // across joints; beyond ~70 m the per-stone relief fades to its mean, and the 2 cm groove itself by ~20 m (it is a
    // one-pixel feature there: TAA jitter turns it into shimmer; the joint still reads by its tone and the cavity term)
    float pil = 0.02 + 0.02 * hash12(id + 21.0);
    float edge = smoothstep(0.0, 0.14, e);
    float stoneH = mix(edge * (pil * (0.7 + 0.3 * dome) + 0.03 * tilt), 0.7 * 0.03, smoothstep(0.04, 0.12, gPix));
    float hs = stoneH + 0.012 * (face - 0.47) - 0.002 * pit;
    float mortarH = lineAA(1.0 - smoothstep(0.005, 0.024, e), 0.008, 0.0);
    h = mix(hs, -0.015, mortarH); bump = 1.0;
    ao = 1.0 - mortar * 0.3;
    gCavity = 1.0 - 0.35 * mortar - 0.15 * (1.0 - dome) * (1.0 - mortar);
  } else if (pat == 20) { // interior wall finish by room (docs/interiors.md): painted limewash, sprig or striped paper,
    // wainscot under paper or paint, bare whitewash, smoke-darkened plaster; skirting, dado rail, seams, fading,
    // peeling near the foot and damp; finish codes per room (SurfaceParams), material grain = code without a room
    vec3 an = normalize(vAn);
    float vert = 1.0 - smoothstep(0.45, 0.75, abs(an.y));
    int ri = roomAt(vAp + an * 0.12);
    vec4 rb = ri >= 0 ? roomB(ri) : vec4(-100.0, 100.0, float(grain % 16), 0.0);
    int code = int(rb.z + 0.5);
    float y = vAp.y - rb.x;
    vec2 uv = faceUV(vAp, vAn);
    float s = uInst.x * 13.0 + float(ri) * 1.7;
    if (ri >= 0) { vec4 ra = roomA(ri); gIn = 1; gRoom = ri; gFloorY = y; gCeilD = rb.y - vAp.y;
      gCornerD = abs(an.x) > abs(an.z) ? min(vAp.z - ra.y, ra.w - vAp.z) : min(vAp.x - ra.x, ra.z - vAp.x); }   // GX5 layer inputs
    float rwf = ri >= 0 && uMatLayer > 0 ? roomDec(ri).z : 0.0;
    float wear = uInst.y * (rwf > 0.0 ? rwf : 1.0), pv = uInst.z;   // GX5: per-room wear (static_decals.cpp: 0.4 .. 1.6)
    gWear = wear;
    vec3 tints[5] = vec3[5](vec3(0.71, 0.68, 0.6), vec3(0.68, 0.61, 0.46), vec3(0.56, 0.61, 0.62), vec3(0.6, 0.63, 0.53), vec3(0.69, 0.58, 0.52));
    vec3 tint = tints[int(hash12(vec2(s, 3.0)) * 4.99)];
    float n = fbmL(vec3(uv * 1.4, s), 1.4);
    float grn = vnF(vec3(uv * 70.0, 2.0), 70.0);
    float bulge = fbm(vec3(uv * vec2(1.0, 1.3), s + 40.0));                     // hand-plastered wall: gentle bulges
    float pm = fbmL(vec3(uv * 0.8, s + 33.0), 0.8);
    float patchM = smoothstep(0.64, 0.64 + max(0.008, fwidth(pm) * 1.5), pm) * vert;   // replastered patches
    vec3 plaster = alb * (0.92 + 0.1 * n) * (0.96 + 0.05 * grn);
    float mott = fbmL(vec3(uv * 0.7, s + 3.0), 0.7), trow = vnoise(vec3(uv * vec2(2.5, 7.0), s + 5.0));
    vec3 paint = mix(alb, tint, 0.65 * pv + 0.2) * (0.84 + 0.2 * n + 0.1 * (mott - 0.5)) * (0.96 + 0.05 * grn) * (0.97 + 0.04 * trow);
    vec3 c = paint;
    float hh = n * 0.3 + grn * 0.12 + trow * 0.25;
    float aoW = 1.0;
    if (code == 5) { c = plaster * vec3(1.02, 1.01, 0.98) * (0.9 + 0.14 * fbmL(vec3(uv * 3.0, s + 2.0), 3.0)); }
    if (code == 6) { c = plaster * mix(vec3(0.62, 0.58, 0.52), vec3(0.85, 0.82, 0.76), smoothstep(0.0, 1.6, vAp.y - 1.2 + n)); }
    if ((code == 1 || code == 2 || code == 3) && vert > 0.5) {   // wallpaper
      vec3 ground = mix(vec3(0.74, 0.69, 0.56), tint * 1.05, 0.45 * pv + 0.15);
      float m = 0.0;
      if (code == 2) {
        float x = fract(uv.x / 0.106);
        m = clamp((smoothstep(0.0, 0.02, x) * (1.0 - smoothstep(0.36, 0.38, x))) * 0.55 + (1.0 - smoothstep(0.0, 0.012, abs(x - 0.7))) * 0.6, 0.0, 1.0);
      } else {
        vec2 cell = vec2(0.1325, 0.11);
        vec2 g = uv / cell; float r = floor(g.y); g.x += mod(r, 2.0) * 0.5;
        vec2 f = (fract(g) - 0.5) * vec2(1.2, 1.0);
        float d = length(f - vec2(0.0, 0.08));
        float a = atan(f.y - 0.08, f.x);
        float pr = 0.1 + 0.035 * cos(a * 5.0);
        float flower = 1.0 - smoothstep(pr - 0.02, pr, d);
        float stem = (1.0 - smoothstep(0.01, 0.022, abs(f.x + 0.18 * (f.y + 0.3)))) * step(-0.36, f.y) * step(f.y, 0.0);
        float leaf = 1.0 - smoothstep(0.03, 0.05, length((f - vec2(-0.07, -0.2)) * vec2(1.0, 2.2)));
        m = max(flower, max(stem * 0.8, leaf * 0.7));
        if (uMatLayer > 0 && hash12(vec2(s, 21.0)) > 0.5) {   // GX5: damask (target_02): a mirrored medallion, side scrolls and
          // leaves on a half-drop lattice, printed tone on tone and embossed
          vec2 dc = vec2(0.21, 0.3), dg = uv / dc; float dr = floor(dg.y); dg.x += mod(dr, 2.0) * 0.5;
          vec2 p = fract(dg) - 0.5; float ax = abs(p.x) * 0.7;   // metres-like aspect (cell 0.21 x 0.3)
          // palmette: petals fanning up from a low centre, an outline ring cut through them, a bud above, a stem below
          vec2 cc = vec2(ax, p.y + 0.1);
          float th = atan(cc.x, cc.y), rr = length(cc);
          float petal = (1.0 - smoothstep(-0.012, 0.0, rr - (0.15 + 0.06 * abs(cos(th * 3.5))))) * (1.0 - smoothstep(1.35, 1.55, abs(th)));
          float ring = 1.0 - smoothstep(0.004, 0.011, abs(rr - 0.075));
          float eye = 1.0 - smoothstep(0.025, 0.032, rr);
          float palm = max(petal * (1.0 - ring), eye);
          float bud = 1.0 - smoothstep(0.022, 0.03, length(vec2(ax, (p.y - 0.29) * 0.75)));
          float stem = (1.0 - smoothstep(0.006, 0.011, ax)) * step(-0.42, p.y) * step(p.y, -0.15);
          // ogee lattice: two wavy scrolls meeting at the cell edges, with leaves where they cross
          float vine = 1.0 - smoothstep(0.007, 0.014, abs(ax - 0.21 - 0.075 * cos(p.y * 6.2832)));
          float lf = 1.0 - smoothstep(0.024, 0.036, length((vec2(ax, abs(p.y)) - vec2(0.3, 0.44)) * vec2(1.0, 1.6)));
          m = max(max(palm, bud), max(max(vine * 0.9, lf), stem * 0.8));
        }
      }
      m = mix(m, 0.18, 1.0 - res(40.0));   // motif strokes under a pixel: the paper's mean tone
      float fib = uMatLayer == 2 ? vnF(vec3(uv * vec2(260.0, 90.0), s + 6.0), 260.0) : 0.5;   // GX5: paper fibre (High)
      vec3 motif = ground * mix(vec3(0.62, 0.55, 0.52), vec3(0.55, 0.6, 0.58), hash12(vec2(s, 9.0)));
      bool damask = code != 2 && uMatLayer > 0 && hash12(vec2(s, 21.0)) > 0.5;
      vec3 paper = mix(ground, damask ? ground * vec3(0.84, 0.82, 0.8) : motif, m * (damask ? 0.55 : 0.6));   // damask: printed tone on tone
      float fade = smoothstep(0.35, 0.8, fbmL(vec3(uv * 0.22, s + 4.0), 0.22));
      paper = mix(paper, mix(paper, vec3(dot(paper, vec3(0.333))), 0.5) * 1.08, fade * 0.5);
      float sx = abs(fract(uv.x / 0.53 + 0.5) - 0.5) * 0.53;
      float seam = lineAA(1.0 - smoothstep(0.0007, 0.0018, sx), 0.0015, 0.004);
      paper *= 1.0 - 0.18 * seam;
      float footD = 1.0 - smoothstep(0.0, 0.7, y);
      float peelM = fbmL(vec3(uv * 1.9, s + 11.0), 1.9) + 0.22 * footD + 0.18 * seam * smoothstep(0.4, 0.7, fbm(vec3(uv.y * 3.0, uv.x * 0.5, s)));
      float pthr = 0.86 - 0.08 * wear;
      float peel = smoothstep(pthr, pthr + 0.01, peelM);
      float curl = smoothstep(pthr - 0.025, pthr, peelM) * (1.0 - peel);
      float top = code == 3 ? 1.0 : 0.0;
      paper *= 0.97 + 0.05 * fib;
      if (y > 0.98 * top) { c = mix(paper * (1.0 + 0.12 * curl), plaster * vec3(0.95, 0.94, 0.9), peel); hh = 0.4 + 0.3 * (1.0 - peel) + 0.25 * curl + seam * 0.2; aoW -= 0.2 * peel * curl;
        if (uMatLayer > 0) {   // GX5: embossed print (0.5 mm) with its edges in shadow, fibre relief; the print a touch satin
          hh += (0.16 * m + 0.03 * fib) * (1.0 - peel); aoW -= 0.06 * m * (1.0 - m) * 4.0 * (1.0 - peel); rough = mix(rough, rough * 0.82, m * (1.0 - peel)); } }
    }
    if ((code == 3 || code == 4) && vert > 0.5 && y < 1.03) {   // wainscot: vertical boards, dado rail, skirting
      float bx = uv.x / 0.105, bi = floor(bx), fb = fract(bx);
      float vj = lineAA(1.0 - smoothstep(0.0, 0.06, min(fb, 1.0 - fb)), 0.006, 0.08);
      float gr = woodGrain(y, uv.x, bi);
      vec3 wood = vec3(0.2, 0.125, 0.07) * (0.86 + 0.25 * hash12(vec2(bi, s))) * (0.8 + 0.32 * gr);
      if (code == 4) {
        vec3 wp = mix(vec3(0.33, 0.37, 0.31), vec3(0.42, 0.37, 0.3), hash12(vec2(s, 5.0))) * (0.92 + 0.08 * n);
        float thru = smoothstep(0.66 - 0.06 * wear, 0.74 - 0.06 * wear, fbmL(vec3(uv * 6.0, bi), 6.0) + 0.25 * (1.0 - smoothstep(0.0, 0.25, y)));
        wood = mix(wp, wood, thru);
      }
      float cap = smoothstep(0.955, 0.96, y), skirt = 1.0 - smoothstep(0.15, 0.155, y);
      float panel = (1.0 - cap) * (1.0 - skirt);
      c = mix(wood, wood * 0.42, vj * panel);
      c *= 1.0 + 0.1 * cap - 0.06 * skirt;
      float scuff = smoothstep(0.6, 0.85, vnoise(vec3(uv * vec2(4.0, 12.0), s))) * (1.0 - smoothstep(0.1, 0.5, y)) * 0.15 * wear;
      c *= 1.0 - scuff;
      hh = panel * (0.55 + gr * 0.1 - vj * 0.5) + cap * (0.9 + 0.1 * sin((y - 0.955) / 0.08 * 3.14)) + skirt * (0.8 - 0.2 * smoothstep(0.12, 0.15, y));
      aoW -= 0.25 * vj * panel + 0.15 * (1.0 - smoothstep(0.0, 0.04, abs(y - 0.955))) + 0.15 * (1.0 - smoothstep(0.0, 0.03, abs(y - 0.155)));
      rough = mix(0.55, 0.7, vj);
    } else if (ri >= 0 && vert > 0.5 && y < 0.13 && code != 5) {   // painted skirting board on plaster / paper walls
      c = mix(vec3(0.16, 0.12, 0.08), tint * 0.5, 0.4) * (0.9 + 0.12 * vnoise(vec3(uv * vec2(30.0, 4.0), s)));
      hh = 0.9 - 0.3 * smoothstep(0.1, 0.13, y); rough = 0.6;
      aoW -= 0.12 * (1.0 - smoothstep(0.0, 0.03, abs(y - 0.13)));
    }
    float hLoss = 0.0;
    bool trimHere = ((code == 3 || code == 4) && y < 1.03) || (ri >= 0 && y < 0.13 && code != 5);
    if (uMatLayer > 0 && vert > 0.5 && !trimHere) {
      // GX5 two materials (target_08): plaster and its paper or paint fall away to the brick behind, more with the wear,
      // at the foot and where the noise mask says; a torn ring of bare plaster round each loss and a broken rim
      // where plaster really fails: the lower wall, corners and round windows; mid-wall at eye height only cracks and
      // damp. Kept rooms (wear < 0.85) lose none; each 6 m of wall hosts at most one loss region (half the cells none),
      // seeded per wall plane so opposite walls never mirror.
      float plane = dot(vAp, abs(an)) * 1.37 + dot(an, vec3(3.1, 5.3, 7.7)) * 4.1;
      vec2 lu = uv + vec2(plane, 0.0);
      float wearL = clamp((wear - 0.6) / 0.8, 0.0, 1.0);
      float lm = -1.0;
      if (wearL > 0.0) {
        float cellX = floor(lu.x / 6.0), hc = hash12(vec2(cellX, s + plane + 71.0));
        float cx = (cellX + 0.2 + 0.6 * hash12(vec2(cellX, s + plane + 72.0))) * 6.0;
        float region = step(hc, 0.5) * (1.0 - smoothstep(0.5, 1.4, abs(lu.x - cx)));
        float broad = fbm2(vec3(lu * 1.15, s + 51.0)) * region;
        float foot = 1.0 - smoothstep(0.0, 1.0, y), corner = 1.0 - smoothstep(0.0, 0.8, gCornerD);
        lm = 0.6 * broad + 0.32 * foot + 0.28 * corner + 0.3 * openingNear(vAp, an)
           + 0.1 * (vnL(vec3(lu * 6.0, s + 52.0), 6.0) - 0.5) + (uMatLayer == 2 ? 0.05 * (vnL(vec3(lu * 19.0, s + 53.0), 19.0) - 0.5) : 0.0);
      }
      float thr = 0.74 - 0.12 * wearL, fw = max(0.006, fwidth(lm) * 1.5);
      float lost = smoothstep(thr, thr + fw, lm);
      float rimP = smoothstep(thr - 0.022, thr, lm) * (1.0 - lost);
      float tear = smoothstep(thr - 0.06, thr - 0.022, lm) * (1.0 - smoothstep(thr - 0.022, thr, lm));
      if (lost + rimP + tear > 0.0) {
        float row = floor(uv.y / 0.075), xo = uv.x + mod(row, 2.0) * 0.1175, bi = floor(xo / 0.235);
        vec2 f = vec2(fract(xo / 0.235) * 0.235, fract(uv.y / 0.075) * 0.075);
        float e = min(min(f.x, 0.235 - f.x), min(f.y, 0.075 - f.y));
        float mort = lineAA(1.0 - smoothstep(0.004, 0.01, e), 0.008, 0.2);
        vec3 bc = vec3(0.42, 0.21, 0.13) * (0.72 + 0.42 * hash12(vec2(bi, row) + s)) * (0.88 + 0.2 * vnF(vec3(uv * 40.0, 3.0), 40.0));
        vec3 brick = mix(bc, vec3(0.5, 0.47, 0.42) * (0.85 + 0.2 * n), mort);
        brick = mix(brick, plaster * 0.9, smoothstep(0.55, 0.8, vnL(vec3(uv * 9.0, s + 53.0), 9.0)) * 0.45);   // plaster left in the joints
        vec3 bare = plaster * vec3(0.96, 0.94, 0.88) * (0.9 + 0.1 * grn);
        c = mix(c, bare, tear); c = mix(c, bare * 1.04, rimP); c = mix(c, brick, lost);
        hLoss = -0.014 * lost - 0.0006 * tear + 0.0012 * rimP - 0.004 * mort * lost + 0.002 * (vnF(vec3(uv * 25.0, 4.0), 25.0) - 0.5) * lost;
        aoW -= 0.28 * lost * mort + 0.12 * rimP;
        rough = mix(rough, 0.95, max(lost, rimP));
      }
    }
    if (ri >= 0 && vert > 0.5) {   // contact shading at the floor and ceiling, damp band, restrained cracks
      float footD = 1.0 - smoothstep(0.0, 0.32 + 0.25 * vnL(vAp * 2.0, 2.0), y);
      c = mix(c, c * vec3(0.78, 0.76, 0.7), footD * 0.3 * wear * (code == 4 || code == 3 ? 0.4 : 1.0));
      aoW *= 0.82 + 0.18 * smoothstep(0.0, 0.3, y);
      aoW *= 0.88 + 0.12 * smoothstep(0.0, 0.35, rb.y - vAp.y);
    }
    float crack = hairline(uv + s, wear * 0.7) * vert * (y > 1.0 || code < 3 ? 1.0 : 0.0);
    c *= 1.0 - 0.25 * crack;
    float plasterLike = (code == 3 || code == 4) && y < 1.03 ? 0.25 : 1.0;   // wainscot boards barely follow the wall
    c = mix(c, c * vec3(1.03, 1.03, 1.04), patchM * 0.5 * plasterLike);
    c *= 1.0 - 0.06 * clamp((0.5 - bulge) * 2.0, 0.0, 1.0) * plasterLike;   // dust and smoke settle in the hollows
    // relief (m): the finish's own texture (3.2 mm scale), bulges +-6 mm, patches 2 mm proud, cracks
    alb = c; h = (hh - crack * 0.4) * 0.0032 + (0.012 * (bulge - 0.5) + 0.002 * patchM) * plasterLike + hLoss; bump = 1.0; ao = clamp(aoW, 0.4, 1.0);
    gCavity = 1.0 - 0.15 * clamp((0.5 - bulge) * 2.0, 0.0, 1.0) * plasterLike - 0.1 * crack;
  } else if (pat == 21) { // floor finish by room (docs/interiors.md): worn boards, herringbone parquet, hexagonal
    // terracotta tomettes, stone flags, beaten earth; dirt at the room's edges, traffic polish in the middle
    vec3 an = normalize(vAn);
    vec2 q = vAp.xz;
    if (an.y < -0.5 || abs(an.y) < 0.5) {   // ceiling of the room below / cut faces: plain boards along x
      int rc = an.y < -0.5 ? roomAt(vAp - vec3(0.0, 0.12, 0.0)) : -1;
      if (rc >= 0) { vec4 ra = roomA(rc); gIn = 1; gRoom = rc; gCornerD = min(min(q.x - ra.x, ra.z - q.x), min(q.y - ra.y, ra.w - q.y)); }   // GX5 layer inputs
      float bw = 0.16, bi = floor(q.y / bw), fb = fract(q.y / bw);
      float seam = 1.0 - smoothstep(0.0, 0.04, min(fb, 1.0 - fb));
      float gr = woodGrain(q.x, q.y, bi);
      alb = vec3(0.21, 0.14, 0.085) * (0.85 + 0.25 * hash12(vec2(bi, 4.0))) * (0.8 + 0.3 * gr);
      alb = mix(alb, alb * 0.45, seam * step(0.5, -an.y));
      h = 1.0 - seam; bump = 0.002; ao = 1.0 - 0.25 * seam;
    } else {
      int ri = roomAt(vAp + vec3(0.0, 0.06, 0.0));
      int code = ri >= 0 ? int(roomB(ri).w + 0.5) : grain % 16;
      vec4 ra = ri >= 0 ? roomA(ri) : vec4(q - 50.0, q + 50.0);
      float edgeD = min(min(q.x - ra.x, ra.z - q.x), min(q.y - ra.y, ra.w - q.y));
      if (ri >= 0) { gIn = 1; gCornerD = edgeD; }   // GX5 layer inputs
      float edgeDirt = ri >= 0 ? (1.0 - smoothstep(0.0, 0.3 + 0.2 * vnL(vec3(q * 3.0, 1.0), 3.0), edgeD)) : 0.0;
      vec2 rc = (ra.xy + ra.zw) * 0.5, rh = max((ra.zw - ra.xy) * 0.5, vec2(0.6));
      float traffic = ri >= 0 ? 1.0 - smoothstep(0.15, 0.85, length((q - rc) / rh)) : 0.3;
      float wear = uInst.y, s = uInst.x * 11.0 + float(ri);
      vec3 c; float hh = 0.5, r = rough, aoF = 1.0;
      if (code == 1) {   // herringbone parquet: 7 x 35 cm oak blocks
        vec3 bid; vec2 l = herring(q / 0.07, 5.0, bid);
        float e = min(min(l.x, 5.0 - l.x) * 0.07, min(l.y, 1.0 - l.y) * 0.07);
        float joint = 1.0 - smoothstep(0.0008, 0.0025, e);
        float gr = woodGrain(l.x * 0.07, l.y * 0.07 + bid.x * 0.37, bid.x + bid.y * 7.0 + bid.z * 3.0);
        c = vec3(0.27, 0.17, 0.09) * (0.84 + 0.3 * hash13(bid + s)) * (0.8 + 0.32 * gr);
        c = mix(c, c * 0.5, joint);
        hh = (1.0 - joint) * 0.8 + gr * 0.08; r = mix(0.5, 0.75, joint); aoF = 1.0 - 0.3 * joint;
      } else if (code == 2) {   // hexagonal terracotta tomettes, 16.5 cm
        vec4 hx = hexCell(q / 0.165);
        float e = hexEdge(hx.xy) * 0.165;
        float grout = 1.0 - smoothstep(0.002, 0.005, e);
        float v = hash12(hx.zw + s);
        c = vec3(0.36, 0.15, 0.08) * (0.82 + 0.3 * v) * (0.9 + 0.14 * fbmL(vec3(q * 9.0, v * 5.0), 9.0));
        c = mix(c, c * vec3(0.8, 0.84, 0.9), step(0.85, hash12(hx.zw + 7.0)) * 0.5);
        float chip = smoothstep(0.72, 0.88, vnL(vec3(q * 25.0, v * 3.0), 25.0)) * (1.0 - smoothstep(0.0, 0.012, e));
        c = mix(c, vec3(0.34, 0.31, 0.27), max(grout, chip * 0.5));
        hh = (1.0 - grout) * (0.8 + 0.2 * smoothstep(0.0, 0.01, e)) - chip * 0.3; r = mix(0.62 - 0.12 * traffic, 0.95, grout); aoF = 1.0 - 0.3 * grout;
      } else if (code == 3) {   // stone flags (local limestone), worn smooth
        vec2 id; float e, u, vr; stoneLayout(q * 1.2, id, e, u, vr);
        float joint = 1.0 - smoothstep(0.003, 0.012, e);
        c = vec3(0.26, 0.245, 0.215) * (0.86 + 0.22 * hash12(id + s)) * (0.84 + 0.24 * fbmL(vec3(q * 2.5, id.x), 2.5));
        c *= 0.92 + 0.12 * vnL(vec3(q * 40.0, id.y), 40.0);
        c = mix(c, c * 0.72 + vec3(0.02), joint);
        hh = (1.0 - joint) * 0.9 + 0.1 * fbmL(vec3(q * 6.0, 2.0), 6.0); r = mix(0.6 - 0.15 * traffic, 0.95, joint); aoF = 1.0 - 0.3 * joint;
      } else if (code == 4) {   // beaten earth / lime screed (workshop, store)
        float n1 = fbmL(vec3(q * 1.5, s), 1.5), n2 = vnL(vec3(q * 30.0, 3.0), 30.0);
        c = vec3(0.3, 0.27, 0.22) * (0.8 + 0.3 * n1) * (0.92 + 0.12 * n2);
        hh = n1 * 0.6 + n2 * 0.2; r = 0.92;
      } else {   // boards along x: 16 cm, staggered butt joints, nail heads, grain, gaps
        float bw = 0.16, bi = floor(q.y / bw), fb = fract(q.y / bw);
        float L = 1.6 + 1.2 * hash12(vec2(bi, 3.0));
        float x = q.x + hash12(vec2(bi, 7.0)) * L, li = floor(x / L), fx = fract(x / L) * L;
        float gap = 1.0 - smoothstep(0.0, 0.035, min(fb, 1.0 - fb));
        float butt = 1.0 - smoothstep(0.0, 0.004, min(fx, L - fx));
        float nail = 1.0 - smoothstep(0.004, 0.007, length(vec2(fract(q.x / 0.6 + 0.5) - 0.5, (fb - 0.5) * 0.4) * vec2(0.6, bw)));
        float gr = woodGrain(q.x, q.y, bi * 3.0 + li);
        c = vec3(0.24, 0.16, 0.095) * (0.82 + 0.3 * hash12(vec2(bi, li) + s)) * (0.8 + 0.32 * gr);
        c = mix(c, c * 0.35, max(gap, butt));
        c = mix(c, vec3(0.05), nail * 0.7);
        hh = (1.0 - max(gap, butt)) * 0.9 + gr * 0.08; r = mix(0.62, 0.85, max(gap, butt)); aoF = 1.0 - 0.3 * max(gap, butt);
      }
      // traffic polish and edge dirt (restrained, scaled by the building's wear)
      c = mix(c, c * 1.08, traffic * 0.35 * min(wear, 1.5));
      r = clamp(r - traffic * 0.12 * wear, 0.3, 1.0);
      vec3 dirt = vec3(0.17, 0.15, 0.12) * (0.8 + 0.3 * vnL(vec3(q * 6.0, 2.0), 6.0));
      c = mix(c, dirt, edgeDirt * (0.25 + 0.2 * wear));
      aoF *= 0.85 + 0.15 * smoothstep(0.0, 0.12, edgeD);
      alb = c; rough = mix(r, 0.95, edgeDirt * 0.5); h = hh; bump = 0.0025; ao = aoF;
    }
  } else if (pat == 23) { // woven rug (part-local): border bands, medallion or kilim field, weave, wear; grain packs
    // the variant and the rug's half size: variant + 4 * (half x cm + 256 * half z cm)
    int gv = grain % 4; int sz = grain / 4;
    vec2 hs = max(vec2(float(sz % 256), float(sz / 256)) * 0.01, vec2(0.2));
    vec2 p = vec2(vPc.x, vPc.z);
    vec2 d = hs - abs(p);
    float edge = min(d.x, d.y);
    vec3 base = alb, acc = mix(alb * vec3(0.5, 0.62, 0.75), vec3(0.62, 0.55, 0.38), float(gv == 1));
    vec3 c = base;
    float b1 = step(0.05, edge) * (1.0 - step(0.09, edge)), b2 = step(0.13, edge) * (1.0 - step(0.15, edge));
    if (gv == 1) {   // kilim stripes and lozenges
      float st = step(0.5, fract(p.y * 3.3)) * 0.5 + 0.5 * step(0.8, fract(p.y * 9.0));
      float lz = 1.0 - step(0.12, abs(fract(p.x * 2.4) - 0.5) + abs(fract(p.y * 3.3) - 0.5));
      c = mix(base, acc, clamp(st * 0.5 + lz * 0.7, 0.0, 1.0));
    } else {         // medallion field
      vec2 r = p / max(hs - vec2(0.2), vec2(0.1));
      float med = 1.0 - smoothstep(0.42, 0.46, abs(r.x) + abs(r.y));
      float ring = 1.0 - smoothstep(0.0, 0.03, abs(abs(r.x) + abs(r.y) - 0.55));
      float flor = smoothstep(0.55, 0.75, vnL(vec3(p * 9.0, 2.0), 9.0));
      c = mix(base, acc, max(med * 0.6, ring * 0.8)) * (0.92 + 0.12 * flor);
    }
    c = mix(c, acc * 0.8, max(b1, b2));
    c = mix(c, base * 0.6, step(edge, 0.05) * step(0.02, edge));
    float weave = vnL(vec3(p * 260.0, 1.0), 260.0) * 0.5 + 0.5 * step(0.5, fract(p.x * 180.0 + step(0.5, fract(p.y * 180.0)) * 0.5));
    float worn = smoothstep(0.55, 0.8, fbmL(vec3(p * 2.0, 4.0), 2.0)) * (1.0 - smoothstep(0.1, 0.3, edge) * 0.3) * uInst.y;
    c = mix(c, mix(c, vec3(0.42, 0.38, 0.3), 0.45), worn * 0.6);
    alb = c * (0.88 + 0.16 * weave); rough = 0.97; h = weave * 0.5 - worn * 0.3; bump = 0.0012;
  } else if (pat == 24) { // glazed faience: smooth glaze, colour band (grain variant), fine crackle, chipped rims
    float n = vnL(vAp * 40.0, 40.0);
    vec2 uv = faceUV(vPc, vPn);
    vec3 band = grain == 1 ? vec3(0.12, 0.2, 0.42) : grain == 2 ? vec3(0.42, 0.12, 0.08) : vec3(0.22, 0.32, 0.18);
    float stripe = step(0.82, fract(vAp.y * 32.0 + 0.3)) * float(grain > 0);
    float dots = (1.0 - smoothstep(0.25, 0.35, length(fract(uv * 28.0) - 0.5))) * float(grain == 3);
    alb = mix(alb * (0.95 + 0.06 * n), band, max(stripe, dots * 0.8));
    float cr = 1.0 - smoothstep(0.0, 0.03, abs(vnL(vec3(uv * 26.0, 7.0), 26.0) - 0.5));
    alb *= 1.0 - 0.06 * cr;
    rough = 0.18 + 0.12 * n; h = n * 0.2 - cr * 0.3; bump = 0.0006;
  } else if (pat == 25 || pat == 26) { // furniture wood / painted wood (docs/interiors.md): continuous grain along the
    // part's own axis (grain -1 = longest axis), figure and pores, end grain, restrained dents, dirt on the feet,
    // hand polish on tops; painted: paint over the same grain, worn through on high spots and the foot
    int na = domAxis(vPn); bool endGrain = grain == na; int g = grain; if (g == na) g = (na + 1) % 3;
    int ac = 3 - na - g;
    float along = vPc[g], across = vPc[ac];
    float seed = floor(dot(vAp - vPc, vec3(3.1, 7.7, 5.3)) * 4.0);
    float gr = woodGrain(along, across, seed);
    float pores = smoothstep(0.62, 0.8, vnoise(vec3(along * 9.0, across * 400.0, seed)));
    vec3 wc = alb * (0.88 + 0.2 * hash12(vec2(seed, 1.0))) * (0.78 + 0.34 * gr) * (1.0 - 0.12 * pores);
    float dent = smoothstep(0.75, 0.92, vnoise(vec3(vAp * 18.0 + seed))) * 0.5 * uInst.y;
    float scratch = (1.0 - smoothstep(0.0, 0.02, abs(vnoise(vec3(along * 3.0, across * 70.0, seed + 9.0)) - 0.5))) * 0.4 * uInst.y;
    wc *= 1.0 - 0.1 * dent - 0.08 * scratch;
    if (endGrain) {
      vec2 qq = vec2(vPc[(na + 1) % 3], vPc[(na + 2) % 3]) + vec2(hash12(vec2(seed, 1.0)), hash12(vec2(seed, 2.0))) * 0.3;
      float ring = fract(length(qq) * 60.0 + vnL(vec3(qq * 30.0, 1.0), 30.0) * 1.5);
      wc = alb * (0.6 + 0.16 * smoothstep(0.3, 0.6, ring)); rough = min(1.0, rough + 0.15);
    }
    vec3 an = normalize(vAn);
    float feet = 1.0 - smoothstep(0.0, 0.1, vAp.y);
    float top = smoothstep(0.85, 0.95, an.y);
    if (pat == 26) {
      vec3 paint = mix(alb, vec3(dot(alb, vec3(0.33))), 0.1) * (0.94 + 0.06 * fbmL(vPc * 3.0, 3.0));
      vec3 bare = vec3(0.3, 0.22, 0.14) * (0.8 + 0.3 * gr);
      float thru = smoothstep(0.72 - 0.06 * uInst.y, 0.8 - 0.06 * uInst.y, fbm(vPc * 7.0 + seed) + 0.25 * feet);
      wc = mix(paint * (1.0 - 0.06 * dent), bare, thru);
      rough = mix(rough, 0.82, thru);
      h = (1.0 - thru) * 0.4 + gr * 0.05 * thru - dent * 0.3;
    } else {
      rough = clamp(rough + (gr - 0.5) * 0.1 - top * 0.1, 0.25, 1.0);
      h = gr * 0.25 + pores * -0.2 - dent * 0.5 - scratch * 0.3;
    }
    wc = mix(wc, wc * vec3(0.62, 0.6, 0.56), feet * 0.6);
    alb = wc; bump = 0.0012;
  } else if (pat == 27) { // paper: off-white sheets / page edges, faint printed rows, foxing
    vec2 uv = faceUV(vPc, vPn);
    float rows = step(0.55, fract(uv.y * 160.0)) * step(0.3, vnoise(vec3(uv * vec2(90.0, 160.0), 1.0))) * float(abs(vPn.y) > 0.5);
    float fox = smoothstep(0.7, 0.85, vnL(vec3(uv * 40.0, 3.0), 40.0));
    alb *= (1.0 - 0.18 * rows) * (1.0 - 0.12 * fox) * (0.95 + 0.06 * vnL(vec3(uv * 300.0, 2.0), 300.0));
    rough = 0.9; h = vnL(vec3(uv * 120.0, 5.0), 120.0); bump = 0.0004;
  } else if (pat == 28) { // textiles (grain variant): 0 plain weave, 1 ticking stripes, 2 woven check, 3 blanket bands
    vec2 uv = faceUV(vPc, vPn);
    vec3 c = alb;
    if (grain == 1) c = mix(alb, alb * vec3(0.35, 0.4, 0.55), step(0.82, fract(uv.x * 22.0)));
    else if (grain == 2) { float a = step(0.5, fract(uv.x * 12.0)), b = step(0.5, fract(uv.y * 12.0)); c = alb * (0.62 + 0.19 * (a + b)); }
    else if (grain == 3) c = mix(alb, alb * vec3(0.55, 0.45, 0.4), step(0.88, fract(uv.y * 4.0)));
    float weave = vnL(vec3(uv * 300.0, 1.0), 300.0) * 0.5 + 0.5 * step(0.5, fract(uv.x * 220.0 + step(0.5, fract(uv.y * 220.0)) * 0.5));
    float fold = fbmL(vec3(uv * 4.0, 3.0), 4.0);
    alb = c * (0.86 + 0.14 * weave) * (0.88 + 0.16 * fold); rough = 0.95; h = weave * 0.3 + fold * 0.7; bump = 0.0015;
  } else if (pat == 29) { // natural rock (docs/nature.md): kTerrainFS's slope-rock lithologies, in world space so boulders
    // share the cliffs' bed tones, bedding lines, joints, iron staining and lichen; grain 0 granite, 1 limestone, 2 schist.
    // Vertex albedo is a per-rock tint (~1). Moss and damp at the foot (asset y ~ 0 is the ground line).
    vec3 p = vW; float up = normalize(vN).y;
    float grn = vnL(p*3.1 + 44.0, 3.1), mott = fbmL(p*0.6 + 45.0, 0.6);
    vec3 rock; float crack = 0.0, bedLine = 0.0;
    if (grain == 0) {   // granite: grey-pink, feldspar flecks, dark mica specks, rounded weathering, no beds
      float speck = vnL(p*23.0 + 3.0, 23.0), feld = smoothstep(0.62, 0.75, vnL(p*9.0 + 5.0, 9.0));
      rock = mix(vec3(0.40, 0.39, 0.38), vec3(0.53, 0.49, 0.46), mott) * (0.86 + 0.24*speck);
      rock = mix(rock, vec3(0.60, 0.50, 0.46), feld*0.45);
      rock *= 1.0 - 0.4*smoothstep(0.72, 0.9, vnL(p*31.0 + 9.0, 31.0));
      h = speck*0.35 + mott*0.65; bump = 0.010;
    } else {
      float lith = grain == 2 ? 1.0 : 0.0;
      float warp = fbmL(vec3(p.xz*0.06, p.y*0.02 + 42.0), 0.06)*5.0;
      float bedY = p.y*0.42*2.0 + warp;                                                         // boulder scale: ~1.2 m beds
      float bedV = fract(sin(floor(bedY)*91.7)*4375.85);
      bedLine = 1.0 - smoothstep(0.0, 0.07, min(fract(bedY), 1.0 - fract(bedY)));
      float thin = smoothstep(0.82, 0.97, sin((p.y + warp*0.4)*7.5)*0.5 + 0.5);
      float jx = (p.x*0.62 + p.z*0.78)*0.58 + (vnL(vec3(p.xz*0.1, p.y*0.14 + 43.0), 0.1) - 0.5)*0.7;
      crack = (1.0 - smoothstep(0.012, 0.04, abs(fract(jx) - 0.5))) * step(0.45, vnoise(vec3(floor(jx)*1.7, floor(bedY*0.5)*1.3, 52.0)));
      vec3 lime = mix(vec3(0.44, 0.40, 0.33), vec3(0.57, 0.52, 0.43), bedV) * (0.74 + 0.42*mott);
      vec3 schist = mix(vec3(0.24, 0.23, 0.21), vec3(0.34, 0.31, 0.27), bedV) * (0.78 + 0.36*mott) * (1.0 - 0.25*thin*lith);
      rock = mix(lime, schist, lith) * (0.9 + 0.2*grn);
      rock *= 1.0 - 0.4*bedLine - 0.42*crack;
      rock = mix(rock, vec3(0.30, 0.22, 0.15), 0.25*smoothstep(0.55, 0.8, fbmL(p*0.25 + 46.0, 0.25)));   // iron staining
      h = grn*0.4 + mott*0.4 - bedLine*0.5 - crack*0.6 + thin*lith*0.3; bump = 0.018;
    }
    // weathering at hand scale: blotchy tone, solution pits / vesicles, fine grit relief
    float blot = vnL(p*1.7 + 70.0, 1.7), pit = smoothstep(0.72, 0.88, vnL(p*7.0 + 71.0, 7.0)), grit = vnL(p*11.0 + 72.0, 11.0);
    rock *= (0.80 + 0.40*blot) * (1.0 - 0.32*pit) * (0.92 + 0.16*grit);
    h += grit*0.35 - pit*0.6; bump = max(bump, 0.035);
    float lichen = smoothstep(0.62, 0.8, vnL(p*0.9 + 47.0, 0.9)) * smoothstep(0.1, 0.7, up + 0.25);
    rock = mix(rock, mix(vec3(0.16, 0.19, 0.09), vec3(0.42, 0.41, 0.30), grn), lichen*0.55);     // moss / grey lichen
    rock = mix(rock, vec3(0.55, 0.50, 0.26), smoothstep(0.78, 0.86, vnL(p*5.5 + 61.0, 5.5))*smoothstep(0.0, 0.6, up)*0.45);   // crusty yellow lichen
    float foot = 1.0 - smoothstep(-0.05, 0.25 + 0.25*vnL(vAp*1.7, 1.7), vAp.y);
    rock = mix(rock, vec3(0.12, 0.15, 0.06), foot*smoothstep(0.45, 0.7, vnL(vAp*2.3 + 7.0, 2.3))*0.6) * mix(1.0, 0.75, foot);   // damp foot
    alb = rock * vAlb * 0.8; rough = 0.86 + 0.08*grn; ao *= 1.0 - 0.3*crack;   // 0.8: matches the cliff faces' in-game brightness
  } else {
    alb *= 0.94 + 0.08*vnL(vAp*7.0, 7.0);
  }
  texMaterial(tslot, vW, vN, vAp, vAn, vPc, vPn, grain, gPix, alb, rough, ao);   // L4: ambientCG layer under the weathering (textures.cpp)
  if (uMatLayer > 0) gxLayer(pat, normalize(vAn), alb, rough, ao);   // GX5 wear / grime layer, roughness breakup, seam occlusion
  if (uSDecOn == 1 && (pat == 20 || (pat == 21 && gRoom >= 0) || (gIn == 0 && pat != 21 && pat != 23 && pat < 25 && uInst.w > 0.5))) sdecals(normalize(vAn), alb, rough, ao);   // GX5 static decals
  if (uWeather == 1 && (pat == 1 || pat == 2 || pat == 3 || pat == 9 || pat == 11 || pat == 17 || pat == 18 || pat == 19 || pat == 22)) {
    // localized weathering in asset space (origin at the ground): rising damp and splash at the foot of vertical
    // faces, moss patches in that damp band, faint run-off streaks higher up. Not a uniform dirt overlay.
    float vert = 1.0 - smoothstep(0.4, 0.7, abs(normalize(vN).y));
    float nn = vnL(vAp*1.3, 1.3);
    float foot = (1.0 - smoothstep(0.0, 0.45 + 0.4*nn, vAp.y)) * vert * step(-0.05, vAp.y);   // GX1: nothing below the asset's ground line (dug-in trench timber)
    alb *= mix(1.0, 0.7, foot); rough = mix(rough, rough*0.75, foot);
    // GX4: the dirt gradient reaches ~1 m (warm earthy tint, densest at the ground), rain splash speckle in the lowest
    // 0.35 m, moss patches climbing from the ground to ~0.6 m (greener on lime render and rubble, less on brick)
    float g1 = (1.0 - smoothstep(0.1, 1.15 + 0.3*nn, vAp.y)) * vert;
    alb = mix(alb, alb*vec3(0.55, 0.5, 0.42), g1*0.75);
    float splash = (1.0 - smoothstep(0.05, 0.5 + 0.15*nn, vAp.y)) * vert * smoothstep(0.5, 0.72, vnL(vAp*23.0 + 3.0, 23.0));
    alb *= 1.0 - 0.35*splash;
    float moss = vert * (1.0 - smoothstep(0.0, 0.5 + 0.3*vnL(vAp*1.9 + 5.0, 1.9), vAp.y)) * smoothstep(0.36, 0.62, vnL(vAp*2.3 + 7.0, 2.3)) * (pat == 9 || pat == 18 ? 0.6 : 1.0);
    alb = mix(alb, mix(vec3(0.05, 0.085, 0.025), vec3(0.13, 0.16, 0.06), vnL(vAp*9.0, 9.0)), moss*0.8);
    rough = mix(rough, 0.95, moss*0.5);
    float streak = smoothstep(0.66, 0.92, vnoise(vec3(dot(vAp.xz, vec2(3.1, 2.7)), vAp.y*0.22, 2.0))) * vert * smoothstep(0.6, 2.0, vAp.y);
    alb *= 1.0 - 0.14*streak;
  }
  vec3 N = dot(vN, vN) > 1e-12 ? normalize(vN) : vec3(0.0, 1.0, 0.0);
  if (!gl_FrontFacing) N = -N;
  if (pat >= 17 && pat <= 22 && pat != 21) gNg = N;   // rough walls: relief ambient term in shade()
  if (uNoWallRelief == 1 && pat >= 17 && pat <= 22 && pat != 21) { bump = 0.0; gCavity = 1.0; gTexH = 0.0; gTexBump = false; gTexDN = vec3(0.0); }
  else gCavity = 1.0;
  if (bump > 0.0 || gTexBump || gDetH != 0.0 || gDecH != 0.0) {
    vec3 dpdx = dFdxFine(vW), dpdy = dFdyFine(vW);
    float hb = h*bump + gTexH + gDetH + gDecH;   // L4: + the set's height (m); GX5: + the detail relief and the decals
    float dhx = dFdxFine(hb), dhy = dFdyFine(hb);
    vec3 r1 = cross(dpdy, N), r2 = cross(N, dpdx);
    float det = dot(dpdx, r1);
    if (abs(det) > 1e-10) {
      vec3 grad = sign(det)*(dhx*r1 + dhy*r2);
      vec3 nb = abs(det)*N - grad;
      // grazing slivers / helper pixels can cancel to zero: keep the geometric normal (a NaN here once
      // poisoned the exposure meter through the HDR mip chain)
      if (dot(nb, nb) > 1e-24) N = normalize(nb);
    }
  }
  if (gl_FrontFacing) N = normalize(N + gTexDN);   // L4: the set's normal map
  vec3 c = shade(vW, N, alb, rough, metal, ao);
  if (uGxDebug == 2 && uCapture == 0) c = gDbg*0.8 + 0.05;
  if (uGxDebug == 1 && uCapture == 0) {
    vec4 rr = gRoom >= 0 && gIn == 1 ? roomDec(gRoom) : uExtDec;
    c = vec3(gIn == 1 ? 0.6 : 0.05, gRoom >= 16 ? 0.6 : 0.05, clamp(rr.y/8.0, 0.0, 1.0))*(0.5 + 0.5*max(dot(N, vec3(0.3, 0.8, 0.5)), 0.0));
  }
  c *= mix(1.0, gSeam, 0.45);   // GX5: corners and seams also see less of the lamps and the window light
  if (uCapture == 1) { oColor = vec4(c*uTint.rgb, gl_FrontFacing ? 1.0 : 0.0); return; }
  c += vEmi*(uGlow*uEmissiveK);   // emission: into the HDR target before bloom, never into the indirect (AO) buffer
  c = applyFog(c, vW);
  oDist = length(vW - uCamPos);
  if (uTint.a < 0.999) { c = mix(c, uTint.rgb, 0.55); oColor = vec4(c, uTint.a); oAmb = vec4(0.0); oNR = vec4(0.0, 1.0, 0.0, 1.0); oVel = vec4(0.0); return; }
  oColor = vec4(c * uTint.rgb, 1.0);   // opaque tint multiplies (white = unchanged; wrecks are blackened)
  oAmb = vec4(gInd*gFogT*uTint.rgb, 1.0);
  oNR = vec4(N, rough + (1.0 - gFogT));   // hazed-out surfaces never reflect
  oVel = vec4(0.0);
}
)GLSL";

static const char* kGlassFS = R"GLSL(
in vec3 vW, vN, vAlb, vPc, vPn, vAp, vAn, vEmi; in vec4 vPar;
uniform float uGlow, uEmissiveK;
out vec4 oColor;
void main(){
  vec3 N = normalize(vN); vec3 V = normalize(uCamPos - vW);
  if (dot(N, V) < 0.0) N = -N;
  // old cylinder glass: slight waviness
  N = normalize(N + 0.025*vec3(vnoise(vAp*18.0)-0.5, vnoise(vAp*18.0+7.0)-0.5, vnoise(vAp*18.0+3.0)-0.5));
  float NdV = max(dot(N, V), 0.0);
  float F = 0.04 + 0.96*pow(1.0-NdV, 5.0);
  vec3 R = reflect(-V, N);
  float sv = skyVis(vW, N);
  vec3 refl = skyColor(R) * mix(0.25, 1.0, sv);
  if (uGiOn == 1 && sv*uGiOpenK < 0.98) {   // reflections of a lit room / street at night (probe GI)
    vec3 gi, ge; float cov = giSample(vW, N, R, R, gi, ge);
    refl = mix(max(mix(refl, ge*1.2, cov), refl), refl, clamp(sv*uGiOpenK, 0.0, 1.0));
  }
  vec3 H = normalize(V + uSunDir);
  float spec = pow(max(dot(N,H),0.0), 600.0) * 30.0 * shadowPCF(vW, N);
  float dirt = fbm(vAp*6.0);
  vec3 tint = vec3(0.82,0.88,0.84);
  vec3 c = refl*F + uSunCol*spec*F + tint*0.03*dirt;
  if (uLightCount > 0) { vec3 ld, ls; localLights(vW, N, N, V, 0.05, vec3(0.04), ld, ls); c += ls + tint*ld*0.02; }   // lamp highlights on the panes
  float a = clamp(0.10 + F*0.85 + dirt*0.08, 0.0, 0.95);
  vec3 emi = vEmi*(uGlow*uEmissiveK);   // lit lamp glass: added in full whatever the pane alpha
  c = applyFog(c, vW);
  oColor = vec4((c + emi) / max(a, 0.05), a);
}
)GLSL";

// ---- imported textured models (first-person weapons): metal-rough PBR with normal and AO maps
static const char* kModelVS = R"GLSL(
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNrm; layout(location=2) in vec4 aTan; layout(location=3) in vec2 aUV;
uniform mat4 uModel, uVP;
uniform float uThin;   // radians to widen by (thin geometry), 0 = off
out vec3 vW, vN; out vec4 vT; out vec2 vUV;
void main(){
  vec4 w = uModel*vec4(aPos,1.0);
  mat3 m = mat3(uModel);
  if (uThin > 0.0) { vec3 n = m*aNrm; float l = length(n); if (l > 1e-6) w.xyz += n / l * min(length(w.xyz - uCamPos) * uThin, 0.03); }
  vW = w.xyz; vN = m*aNrm; vT = vec4(m*aTan.xyz, aTan.w); vUV = aUV;
  gl_Position = uVP*w;
}
)GLSL";
static const char* kModelFS = R"GLSL(
in vec3 vW, vN; in vec4 vT; in vec2 vUV;
uniform sampler2D uAlb, uMR, uNrm, uAO;
uniform int uHasAlb, uHasMR, uHasNrm, uHasAO, uAOinMR;
uniform vec4 uBase, uTint; uniform float uMetal, uRough;
layout(location=0) out vec4 oColor;
layout(location=1) out float oDist;
layout(location=2) out vec4 oAmb;
layout(location=3) out vec4 oNR;
layout(location=4) out vec4 oVel;
void main(){
  vec4 base = uBase;
  if (uHasAlb == 1) base *= texture(uAlb, vUV);
  float rough = uRough, metal = uMetal, ao = 1.0;
  if (uHasMR == 1) { vec4 mr = texture(uMR, vUV); rough *= mr.g; metal *= mr.b; if (uAOinMR == 1) ao = mr.r; }
  if (uHasAO == 1) ao = texture(uAO, vUV).r;
  vec3 N = normalize(vN);
  if (!gl_FrontFacing) N = -N;                         // glTF models are double sided
  if (uHasNrm == 1) {
    vec3 T = vT.xyz - N*dot(N, vT.xyz);
    if (dot(T, T) > 1e-12) {
      T = normalize(T); vec3 B = cross(N, T) * (vT.w < 0.0 ? -1.0 : 1.0);
      vec3 t = texture(uNrm, vUV).xyz*2.0 - 1.0;
      vec3 n = T*t.x + B*t.y + N*max(t.z, 0.05);
      if (dot(n, n) > 1e-12) N = normalize(n);
    }
  }
  vec3 c = shade(vW, N, base.rgb, clamp(rough, 0.04, 1.0), clamp(metal, 0.0, 1.0), mix(1.0, ao, 0.85));
  if (uTint.a >= 0.999) c *= uTint.rgb;   // opaque tint multiplies (white = unchanged; tank wrecks are blackened)
  c = applyFog(c, vW);
  if (uTint.a < 0.999) c = mix(c, uTint.rgb, 0.55);
  oColor = vec4(c, 1.0);
  oDist = length(vW - uCamPos);
  oAmb = vec4(gInd*gFogT, 1.0);
  oNR = vec4(N, clamp(rough, 0.04, 1.0) + (1.0 - gFogT));
  oVel = vec4(0.0);
}
)GLSL";

// ---- near-field depth of field for aiming down the sights: only first-person weapon pixels nearer than the
// focus get a circle of confusion; scatter-as-gather so the blurred rear sight bleeds over the background.
//   CoC(d) = amount * clamp((focus - d) / (0.6 * focus), 0, 1) * maxR      (d = eye distance, 0 = world pixel)
static const char* kDofFS = R"GLSL(
#version 450 core
in vec2 vUV;
uniform sampler2D uHdr, uDist; uniform float uFocus, uAmount, uMaxR; uniform vec2 uTexel;
out vec4 oColor;
float coc(vec2 uv){ float d = texture(uDist, uv).r; if (d <= 0.0) return 0.0; return uAmount*clamp((uFocus - d)/(0.6*uFocus), 0.0, 1.0)*uMaxR; }
void main(){
  vec3 c0 = textureLod(uHdr, vUV, 0.0).rgb;
  float r0 = coc(vUV);
  vec3 sum = c0; float wsum = 1.0;
  const float GA = 2.39996323;
  for (int i = 0; i < 24; ++i) {
    float rr = sqrt((float(i) + 0.5)/24.0) * uMaxR;
    vec2 o = vec2(cos(float(i)*GA), sin(float(i)*GA)) * rr;
    vec2 uv = vUV + o*uTexel;
    float r = coc(uv);
    float w = smoothstep(rr - 1.0, rr + 1.0, max(r, min(r0, r + 0.5)));   // a tap contributes if its blur disc reaches here
    sum += textureLod(uHdr, uv, 0.0).rgb * w; wsum += w;
  }
  vec3 c = sum/wsum;
  oColor = vec4((any(isnan(c)) || any(isinf(c))) ? c0 : c, 1.0);
}
)GLSL";

// ---- optic eyepiece (W2: sniper scopes, AT gun sight). Pixels q from the frame centre (y up), circle radius R.
// uMode 0 (after the world, into dofTex): inside the circle the magnified world, an edge falloff and the reticle ink;
//   outside it the world blurred heavily (16 taps on a coarse hdrColor mip) and darkened, with a black tube rim.
// uMode 1 (after the weapon, blended ONE / SRC_ALPHA into hdrColor): the inside comes back from the composite, so the
//   weapon shows only around the eyepiece; outside, the weapon is shaded by uShade (out of focus, in the eye's shadow).
static const char* kEyepieceFS = R"GLSL(
#version 450 core
in vec2 vUV;
uniform sampler2D uSrc; uniform vec2 uRes; uniform float uR, uDark, uShade; uniform int uReticle, uMode;
out vec4 oColor;
float segD(vec2 p, vec2 a, vec2 b){ vec2 pa = p - a, ba = b - a; float h = clamp(dot(pa, ba)/dot(ba, ba), 0.0, 1.0); return length(pa - ba*h); }
float cov(float d){ return clamp(0.5 - d, 0.0, 1.0); }   // ink coverage at signed distance d (px, < 0 inside)
float reticleInk(vec2 q, float R){
  float ink = 0.0;
  if (uReticle == 1) {          // German no. 1 (ZF39): pointed post from below, its tip the aim point; heavy side bars, open centre
    float hw = max(1.6, R*0.011), tip = R*0.07;
    if (q.y <= 0.0) ink = cov(abs(q.x) - hw*min(1.0, -q.y/tip));
    ink = max(ink, cov(max(abs(q.y) - max(1.0, R*0.007), R*0.18 - abs(q.x))));
  } else if (uReticle == 2) {   // US M81 / M82 (M1C): fine crosshair, tapered lower post
    float t = max(0.8, R*0.002);
    ink = cov(abs(q.y) - t);
    ink = max(ink, cov(abs(q.x) - (q.y > 0.0 ? t : max(t, 0.012*(-q.y)))));
  } else if (uReticle == 3) {   // TZF: big chevron (aim point at its tip), a row of small range chevrons, a post, dotted range arcs
    float u = R*0.25, lw = max(1.0, R*0.0035), d = 1e9;
    d = min(d, segD(q, vec2(0.0), vec2(-0.45*u, -0.63*u)));
    d = min(d, segD(q, vec2(0.0), vec2( 0.45*u, -0.63*u)));
    for (int k = 1; k <= 3; ++k) for (int s = -1; s <= 1; s += 2) {
      vec2 a = vec2(float(s*k)*u, 0.0);
      d = min(d, segD(q, a, a + vec2(-0.2*u, -0.28*u)));
      d = min(d, segD(q, a, a + vec2( 0.2*u, -0.28*u)));
    }
    ink = cov(d - lw);
    if (q.y < -0.9*u) ink = max(ink, cov(abs(q.x) - 1.8*lw));   // vertical post under the big chevron
    float th = degrees(atan(q.y, q.x));
    if (th > 28.0 && th < 152.0) for (int i = 0; i < 3; ++i) {   // range arcs over the upper field, a dot every 5 deg
      float ri = R*(0.42 + 0.17*float(i)), tk = clamp(round(th/5.0)*5.0, 35.0, 145.0);
      ink = max(ink, cov(length(q - ri*vec2(cos(radians(tk)), sin(radians(tk)))) - 1.4*lw));
    }
  }
  return ink;
}
void main(){
  vec2 q = (vUV - 0.5)*uRes; float R = uR*uRes.y, r = length(q);
  float f = clamp(R + 0.5 - r, 0.0, 1.0);   // inside the eyepiece (1 px antialiased edge)
  float outer = smoothstep(R, 2.2*R, r);
  if (uMode == 1) { vec3 c = textureLod(uSrc, vUV, 0.0).rgb; oColor = vec4(c*f, (1.0 - f)*uShade*mix(1.0, 0.55, outer)); return; }
  vec3 sharp = textureLod(uSrc, vUV, 0.0).rgb * mix(1.0, 0.6, smoothstep(0.8*R, R, r));
  sharp = mix(sharp, vec3(0.0015), reticleInk(q, R));
  float bl = R*0.09, lod = log2(max(1.0, bl/3.0));
  vec3 s = vec3(0.0);
  for (int i = 0; i < 16; ++i) {
    float rr = sqrt((float(i) + 0.5)/16.0)*bl, a = float(i)*2.39996323;
    s += textureLod(uSrc, vUV + vec2(cos(a), sin(a))*rr/uRes, lod).rgb;
  }
  vec3 outside = s/16.0*uDark*smoothstep(R, 1.1*R, r)*mix(1.0, 0.5, outer);
  vec3 c = mix(outside, sharp, f);
  oColor = vec4((any(isnan(c)) || any(isinf(c))) ? vec3(0.0) : c, 1.0);
}
)GLSL";

// ---- volumetric light shafts (half resolution): march the shadow cascades along the view ray up to the world
// depth (never the first-person weapon: it is drawn after this pass), accumulate lit haze with a forward-biased
// Henyey-Greenstein phase. Restrained: it adds light only where the haze is sunlit between occluders.
static const char* kShaftFS = R"GLSL(
in vec2 vUV;
uniform sampler2D uDepth; uniform mat4 uInvVP; uniform float uMaxDist, uDensity;
out vec4 oColor;
float hgPhase(float c, float g){ float g2 = g*g; return (1.0 - g2)/(4.0*PI*pow(1.0 + g2 - 2.0*g*c, 1.5)); }
void main(){
  float z = texture(uDepth, vUV).r;
  vec4 p = uInvVP*vec4(vUV*2.0 - 1.0, z, 1.0); vec3 w = p.xyz/p.w;   // reverse-Z: ndc z = depth (sky 0 = far plane)
  vec3 rd = w - uCamPos; float L = length(rd); rd /= max(L, 1e-4);
  L = min(L, uMaxDist);
  const int N = 14;
  float dt = L/float(N), jit = pixelNoise(), lit = 0.0, T = 1.0;
  for (int i = 0; i < N; ++i) {
    vec3 q = uCamPos + rd*(dt*(float(i) + jit));
    float e; float s = cascadePCF(uShadow, uLightVP, uShadowTexel, uShadowWorldTexel.x, q, uSunDir, 0.0, 1, e);
    if (e > 0.95 && uShadowFarOn == 1) s = cascadePCF(uShadowFar, uLightVPFar, uShadowTexelFar, uShadowWorldTexel.y, q, uSunDir, 0.0, 1, e);
    float rho = uDensity*exp(-max(q.y, 0.0)/uHazeH);
    lit += T*rho*s*dt; T *= exp(-rho*dt);
  }
  float c = dot(rd, uSunDir);
  float ph = hgPhase(c, 0.72);                 // forward lobe only: beams show toward the sun, not as uniform haze
  oColor = vec4(uSunCol*vec3(1.0, 0.94, 0.84)*lit*ph*4.0*PI, 1.0);
}
)GLSL";
// ---- screen-space ambient occlusion (half resolution) from the world depth only. View-space positions are
// reconstructed from depth with the jitter-consistent inverse projection; the normal comes from the smaller of
// the two neighbour differences per axis (no smearing across silhouettes). 8 samples on a rotated spiral within
// a world-space radius; each counts by how far it rises above the tangent plane, faded by distance (no halos
// around depth discontinuities). Sky (depth 1) is unoccluded.
static const char* kSsaoFS = R"GLSL(
in vec2 vUV;
uniform sampler2D uDepth; uniform mat4 uInvProj, uProj; uniform float uRadius, uPower; uniform vec2 uTexel;
out vec4 oColor;
vec3 vpos(vec2 uv){ float z = texture(uDepth, uv).r; vec4 p = uInvProj*vec4(uv*2.0 - 1.0, z, 1.0); return p.xyz/p.w; }   // reverse-Z
void main(){
  float z = texture(uDepth, vUV).r;
  if (z <= 0.0) { oColor = vec4(1.0); return; }   // sky
  vec3 P = vpos(vUV);
  vec3 px = vpos(vUV + vec2(uTexel.x, 0.0)) - P, nx = P - vpos(vUV - vec2(uTexel.x, 0.0));
  vec3 py = vpos(vUV + vec2(0.0, uTexel.y)) - P, ny = P - vpos(vUV - vec2(0.0, uTexel.y));
  vec3 dx = dot(px, px) < dot(nx, nx) ? px : nx, dy = dot(py, py) < dot(ny, ny) ? py : ny;
  vec3 N = normalize(cross(dx, dy));
  if (dot(N, P) > 0.0) N = -N;
  float rad = uRadius*clamp(1.0 + (-P.z - 20.0)/60.0, 1.0, 3.0);   // a little wider far away (keeps contact)
  float occ = 0.0, rot = pixelNoise()*6.2831853;
  const int S = 8;
  for (int i = 0; i < S; ++i) {
    float t = (float(i) + 0.5)/float(S);
    float a = float(i)*2.3999632 + rot;
    vec3 T = normalize(cross(N, abs(N.y) < 0.9 ? vec3(0, 1, 0) : vec3(1, 0, 0))), B = cross(N, T);
    vec3 dir = normalize((T*cos(a) + B*sin(a))*sqrt(1.0 - t*0.6) + N*(0.25 + 0.75*t));
    vec3 Q = P + dir*rad*mix(0.2, 1.0, t*t);
    vec4 c = uProj*vec4(Q, 1.0); vec2 uv = c.xy/c.w*0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) continue;
    vec3 S3 = vpos(uv);
    float above = S3.z - Q.z;                                     // sample buried behind the surface seen there
    float range = 1.0 - smoothstep(rad*0.6, rad*2.2, abs(S3.z - P.z));
    occ += step(0.02*rad, above)*range;
  }
  float ao = pow(clamp(1.0 - occ/float(S), 0.0, 1.0), uPower);
  oColor = vec4(ao, -P.z, 0.0, 1.0);
}
)GLSL";
// Composite of the lit world: screen-space reflections, then AO on the indirect light, then light shafts.
// SSR (smooth surfaces only: puddles, wet ruts, polished metal): march the reflected ray in view space against
// the world depth with a thickness test, refine by bisection. A miss keeps the sky / environment term already
// in the lit colour; a hit replaces it, weighted by Fresnel, roughness, screen edges and ray length. Only world
// depth is read: the first-person weapon is drawn after this pass.
static const char* kCloudShadowFS = R"GLSL(
in vec2 vUV;
out vec4 oColor;
void main(){
  vec2 xz = uCloudShadowBox.xy + (vUV*2.0 - 1.0)*uCloudShadowBox.z;
  oColor = vec4(cloudShadowAt(xz, 0.0), 0.0, 0.0, 1.0);
}
)GLSL";
static const char* kCompositeFS = R"GLSL(
in vec2 vUV;
uniform sampler2D uScene, uShafts, uAmb, uAO, uDepth, uNR; uniform float uShaftAmt, uAoAmt, uSsrAmt;
uniform mat4 uInvProj, uProj, uView; uniform vec2 uAoTexel;
out vec4 oColor;
vec3 viewPosAt(vec2 uv){ float z = texture(uDepth, uv).r; vec4 p = uInvProj*vec4(uv*2.0 - 1.0, z, 1.0); return p.xyz/p.w; }   // reverse-Z
vec2 toUV(vec3 q){ vec4 c = uProj*vec4(q, 1.0); return c.xy/c.w*0.5 + 0.5; }
vec4 traceSSR(vec3 Pv, vec3 Rv){
  float stepL = 0.22, t = 0.10, jit = pixelNoise();
  vec3 prev = Pv;
  for (int i = 0; i < 28; ++i) {
    vec3 q = Pv + Rv*(t + stepL*jit);
    if (q.z > -0.05) return vec4(0.0);
    vec2 uv = toUV(q);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return vec4(0.0);
    float dz = viewPosAt(uv).z - q.z;              // > 0: the ray passed behind the visible surface
    if (dz > 0.0 && dz < max(0.35, stepL*1.5)) {
      vec3 a = prev, b = q;
      for (int k = 0; k < 5; ++k) { vec3 m = 0.5*(a + b); if (viewPosAt(toUV(m)).z - m.z > 0.0) b = m; else a = m; }
      uv = toUV(b);
      vec2 e = smoothstep(vec2(0.0), vec2(0.08), uv)*smoothstep(vec2(0.0), vec2(0.08), 1.0 - uv);
      return vec4(uv, e.x*e.y*(1.0 - smoothstep(20.0, 60.0, t)), 1.0);
    }
    prev = q; t += stepL; stepL *= 1.18;
  }
  return vec4(0.0);
}
void main(){
  vec3 c = texture(uScene, vUV).rgb;
  if (uSsrAmt > 0.0) {
    vec4 nr = texture(uNR, vUV);
    if (nr.w < 0.4 && texture(uDepth, vUV).r > 0.0) {   // not sky
      vec3 Pv = viewPosAt(vUV);
      vec3 Nv = normalize(mat3(uView)*nr.xyz);
      vec3 Vv = normalize(-Pv), Rv = reflect(-Vv, Nv);
      float F = 0.02 + 0.98*pow(1.0 - max(dot(Nv, Vv), 0.0), 5.0);
      vec4 hit = traceSSR(Pv, Rv);
      if (hit.w > 0.0) {
        vec3 Rw = normalize(transpose(mat3(uView))*Rv);
        vec3 refl = textureLod(uScene, hit.xy, 0.0).rgb;
        float w = hit.z*F*(1.0 - smoothstep(0.1, 0.4, nr.w))*uSsrAmt;
        c = max(c + (refl - skyColor(Rw))*w, vec3(0.0));   // geometry seen in the reflection replaces the sky estimate
      }
    }
  }
  if (uAoAmt > 0.0) {
    // depth-aware 4x4 blur of the half-resolution AO, then remove the occluded share of the indirect light
    float z = texture(uDepth, vUV).r;
    vec4 p = uInvProj*vec4(vUV*2.0 - 1.0, z, 1.0); float d0 = -p.z/p.w;
    float s = 0.0, w = 0.0;
    for (int y = -2; y < 2; ++y) for (int x = -2; x < 2; ++x) {
      vec2 o = (vec2(x, y) + 0.5)*uAoTexel;
      vec2 a = texture(uAO, vUV + o).rg;
      float k = exp(-abs(a.g - d0)/(0.04*d0 + 0.05));
      s += a.r*k; w += k;
    }
    float ao = w > 1e-4 ? s/w : 1.0;
    vec3 amb = texture(uAmb, vUV).rgb;
    c -= amb*(1.0 - ao)*uAoAmt;
  }
  if (uShaftAmt > 0.0) c += texture(uShafts, vUV).rgb*uShaftAmt;
  c = max(c, vec3(0.0));
  oColor = vec4((any(isnan(c)) || any(isinf(c))) ? vec3(0.0) : c, 1.0);
}
)GLSL";
// ---- temporal anti-aliasing of the *world only* (weapon, sights, muzzle flash and tracers are drawn after it,
// unjittered, so they cannot ghost). History is reprojected with the previous unjittered view-projection from
// the world depth; it is clipped toward the 3x3 neighbourhood (YCoCg mean +- 1.1 sigma) so moving foliage and
// disocclusions do not smear; the current frame weighs more under motion; invalid history (off-screen,
// reset after a camera cut / resize) takes the current frame outright.
static const char* kTaaFS = R"GLSL(
in vec2 vUV;
uniform sampler2D uCur, uHist, uDepth, uVel; uniform mat4 uInvVP, uPrevVP; uniform vec2 uTexel; uniform int uValid; uniform float uSharpen;
layout(location=0) out vec4 oColor;
layout(location=1) out vec4 oHist;
vec3 toYC(vec3 c){ return vec3(0.25*c.r + 0.5*c.g + 0.25*c.b, 0.5*c.r - 0.5*c.b, -0.25*c.r + 0.5*c.g - 0.25*c.b); }
vec3 fromYC(vec3 y){ return vec3(y.x + y.y - y.z, y.x + y.z, y.x - y.y - y.z); }
vec3 tm(vec3 c){ return c/(1.0 + dot(c, vec3(0.2126, 0.7152, 0.0722))); }          // weigh in a compressed space
vec3 itm(vec3 c){ return c/max(1.0 - dot(c, vec3(0.2126, 0.7152, 0.0722)), 1e-3); }
// 5-tap Catmull-Rom history fetch: bilinear resampling every frame would blur the image under camera motion
vec3 histCR(vec2 uv){
  vec2 sz = 1.0/uTexel, p = uv*sz - 0.5, f = fract(p), c = (floor(p) + 0.5)*uTexel;
  vec2 w0 = f*(-0.5 + f*(1.0 - 0.5*f)), w1 = 1.0 + f*f*(-2.5 + 1.5*f), w2 = f*(0.5 + f*(2.0 - 1.5*f)), w3 = f*f*(-0.5 + 0.5*f);
  vec2 w12 = w1 + w2, o12 = c + (w2/w12)*uTexel, o0 = c - uTexel, o3 = c + 2.0*uTexel;
  vec3 r = texture(uHist, vec2(o12.x, o0.y)).rgb*w12.x*w0.y + texture(uHist, vec2(o0.x, o12.y)).rgb*w0.x*w12.y
         + texture(uHist, o12).rgb*w12.x*w12.y + texture(uHist, vec2(o3.x, o12.y)).rgb*w3.x*w12.y + texture(uHist, vec2(o12.x, o3.y)).rgb*w12.x*w3.y;
  float wsum = w12.x*w0.y + w0.x*w12.y + w12.x*w12.y + w3.x*w12.y + w12.x*w3.y;
  return max(r/wsum, vec3(0.0));
}
void main(){
  vec3 cur = tm(texture(uCur, vUV).rgb);
  vec3 m1 = vec3(0.0), m2 = vec3(0.0), sum = vec3(0.0);
  for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) {
    vec3 s = tm(texture(uCur, vUV + vec2(x, y)*uTexel).rgb);
    vec3 yc = toYC(s); m1 += yc; m2 += yc*yc;
    if (x != 0 || y != 0) sum += s;
  }
  m1 /= 9.0; vec3 sig = sqrt(max(m2/9.0 - m1*m1, 0.0));
  float z = texture(uDepth, vUV).r;
  vec4 wp = uInvVP*vec4(vUV*2.0 - 1.0, z, 1.0);   // reverse-Z: ndc z = depth; the sky (0) lands on the far plane
  vec3 w = wp.xyz/wp.w;
  vec4 pc = uPrevVP*vec4(w, 1.0); vec2 puv = pc.xy/pc.w*0.5 + 0.5;
  vec4 mv = texture(uVel, vUV);
  if (mv.a > 0.5) puv = vUV + mv.xy;   // animated foliage / grass: their own motion, not the depth reprojection
  vec3 res = cur;
  if (uValid == 1 && puv.x > 0.0 && puv.x < 1.0 && puv.y > 0.0 && puv.y < 1.0) {
    vec3 h = toYC(tm(histCR(puv)));
    vec3 lo = m1 - 1.1*sig, hi = m1 + 1.1*sig;
    vec3 ctr = 0.5*(lo + hi), ext = max(0.5*(hi - lo), vec3(1e-4));   // clip toward the box centre
    vec3 d = h - ctr; vec3 a = abs(d/ext); float mx = max(a.x, max(a.y, a.z));
    if (mx > 1.0) h = ctr + d/mx;
    float motion = length((puv - vUV)/uTexel);
    float alpha = mix(0.08, 0.30, clamp(motion/16.0, 0.0, 1.0));
    res = mix(fromYC(h), cur, alpha);
  }
  vec3 hst = itm(res);
  hst = (any(isnan(hst)) || any(isinf(hst))) ? itm(cur) : hst;
  // unsharp mask on the displayed image only (never fed back into the history, which would ring and drift)
  vec3 o = itm(max(res + (res - sum/8.0)*uSharpen, vec3(0.0)));
  o = (any(isnan(o)) || any(isinf(o))) ? hst : o;
  oColor = vec4(o, 1.0); oHist = vec4(hst, 1.0);
}
)GLSL";
// ---- bloom: a downsample chain (first level de-fireflied by a luminance-weighted average) and a tent upsample
// accumulated back up; the tonemap adds a small fraction. Wide and faint: a soft glow, no threshold streaks.
static const char* kBloomDownFS = R"GLSL(
in vec2 vUV;
uniform sampler2D uSrc; uniform vec2 uSrcTexel; uniform int uFirst;
out vec4 oColor;
void main(){
  vec3 s[4]; vec2 o = uSrcTexel;
  s[0] = texture(uSrc, vUV + vec2(-o.x, -o.y)).rgb; s[1] = texture(uSrc, vUV + vec2(o.x, -o.y)).rgb;
  s[2] = texture(uSrc, vUV + vec2(-o.x, o.y)).rgb;  s[3] = texture(uSrc, vUV + vec2(o.x, o.y)).rgb;
  vec3 c = vec3(0.0); float wsum = 0.0;
  for (int i = 0; i < 4; ++i) {
    float w = uFirst == 1 ? 1.0/(1.0 + dot(s[i], vec3(0.2126, 0.7152, 0.0722))) : 1.0;
    c += s[i]*w; wsum += w;
  }
  c /= wsum;
  oColor = vec4((any(isnan(c)) || any(isinf(c))) ? vec3(0.0) : min(c, vec3(64.0)), 1.0);
}
)GLSL";
static const char* kBloomUpFS = R"GLSL(
in vec2 vUV;
uniform sampler2D uSrc; uniform vec2 uSrcTexel;
out vec4 oColor;
void main(){
  vec2 o = uSrcTexel;
  vec3 c = texture(uSrc, vUV).rgb*4.0;
  c += (texture(uSrc, vUV + vec2(o.x, 0)).rgb + texture(uSrc, vUV - vec2(o.x, 0)).rgb + texture(uSrc, vUV + vec2(0, o.y)).rgb + texture(uSrc, vUV - vec2(0, o.y)).rgb)*2.0;
  c += texture(uSrc, vUV + o).rgb + texture(uSrc, vUV - o).rgb + texture(uSrc, vUV + vec2(o.x, -o.y)).rgb + texture(uSrc, vUV + vec2(-o.x, o.y)).rgb;
  oColor = vec4(c/16.0, 1.0);
}
)GLSL";

// ---- tree leaf cards: alpha-tested clusters, sway  V_offset = D_wind * M * A * sin(f t + k_x X + k_z Z)
static const char* kLeafVS = R"GLSL(
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNrm; layout(location=2) in vec2 aUV; layout(location=3) in float aMask; layout(location=4) in float aShade;
uniform mat4 uModel, uVP; uniform float uTime, uWind;
uniform mat4 uVPU, uPrevVP; uniform float uPrevTime;   // motion vectors (TAA): unjittered current / previous
out vec3 vW, vN; out vec2 vUV; out float vM, vShade; flat out float vSeed;
flat out float vLayer; out vec3 vLN;   // L1: leaf texture layer (-1: the procedural ovals), the leaf plane's normal
out vec4 vCC, vPC;
invariant gl_Position;
vec3 leafSway(vec3 w, float t, float m){
  const vec3 D = vec3(0.8, 0.0, 0.6); const float A = 0.10, f = 1.3, kx = 0.21, kz = 0.17;
  float s = sin(f*t + kx*w.x + kz*w.z) + 0.3*sin(2.9*t + 0.8*w.x - 0.5*w.y);
  return (D*A*s + vec3(0.0, 0.25*A*s, 0.0)) * m * uWind;
}
void main(){
  vec4 w0 = uModel*vec4(aPos, 1.0), w = w0;
  w.xyz += leafSway(w0.xyz, uTime, aMask);
  vW = w.xyz; vN = normalize(mat3(uModel)*aNrm); vUV = aUV; vM = aMask; vShade = aShade;
  vSeed = floor(dot(aPos, vec3(13.1, 7.3, 11.7)));
  vLayer = -1.0; vLN = vN;
  vCC = uVPU*w; vPC = uPrevVP*vec4(w0.xyz + leafSway(w0.xyz, uPrevTime, aMask), 1.0);
  gl_Position = uVP*w;   // invariant: the PF2 canopy depth prepass (leaf_shadow) and the GL_EQUAL shading pass must agree
}
)GLSL";
// L1 species leaves (foliage.cpp, docs/leaves.md): instanced sprig meshes. Per vertex: card-space position (x, y in
// [-1, 1] over the card's T / B half extents, z along its normal), leaf normal in the same frame, layer uv, layer.
// Per instance (one canopy card): centre, T*s, B*s, cluster direction, mask / shade / seed. Same sway and outputs as kLeafVS.
static const char* kLeafInstVS = R"GLSL(
layout(location=0) in vec3 aC; layout(location=1) in vec3 aN; layout(location=2) in vec2 aUV; layout(location=3) in float aLayer;
layout(location=4) in vec3 iCtr; layout(location=5) in vec3 iT; layout(location=6) in vec3 iB; layout(location=7) in vec3 iDir; layout(location=8) in vec3 iMSS;
uniform mat4 uModel, uVP; uniform float uTime, uWind;
uniform mat4 uVPU, uPrevVP; uniform float uPrevTime;
out vec3 vW, vN; out vec2 vUV; out float vM, vShade; flat out float vSeed;
flat out float vLayer; out vec3 vLN;
out vec4 vCC, vPC;
invariant gl_Position;
vec3 leafSway(vec3 w, float t, float m){
  const vec3 D = vec3(0.8, 0.0, 0.6); const float A = 0.10, f = 1.3, kx = 0.21, kz = 0.17;
  float s = sin(f*t + kx*w.x + kz*w.z) + 0.3*sin(2.9*t + 0.8*w.x - 0.5*w.y);
  return (D*A*s + vec3(0.0, 0.25*A*s, 0.0)) * m * uWind;
}
void main(){
  vec3 n = cross(iT, iB)/max(length(iT), 1e-6);   // card normal, same length as iT / iB
  vec4 w0 = uModel*vec4(iCtr + iT*aC.x + iB*aC.y + n*aC.z, 1.0), w = w0;
  w.xyz += leafSway(w0.xyz, uTime, iMSS.x);
  vec3 ln = normalize(iT*aN.x + iB*aN.y + n*aN.z);
  if (dot(ln, iDir) < 0.0) ln = -ln;               // two-sided: the face toward the cluster's outside
  vW = w.xyz; vLN = normalize(mat3(uModel)*ln);
  vN = normalize(mat3(uModel)*normalize(iDir*0.7 + ln*0.3));   // the crown's volume first, the leaf's own tilt second
  vUV = aUV; vM = iMSS.x; vShade = iMSS.y; vSeed = iMSS.z; vLayer = aLayer;
  vCC = uVPU*w; vPC = uPrevVP*vec4(w0.xyz + leafSway(w0.xyz, uPrevTime, iMSS.x), 1.0);
  gl_Position = uVP*w;   // invariant: the canopy depth prepass and the GL_EQUAL shading pass must agree
}
)GLSL";
static const char* kLeafAlpha = R"GLSL(
uniform sampler2DArray uLeaves;   // L1: unit kLeafUnit (foliage.cpp); mips keep the alpha-tested coverage
float leafAlpha(vec2 uv, float seed);
float leafCut(vec2 uv, float seed, float layer){ return layer < 0.0 ? leafAlpha(uv, seed) : texture(uLeaves, vec3(uv, layer)).a; }
float leafAlpha(vec2 uv, float seed){
  // leaves grow slightly once a card covers few pixels (fwidth of the uv), so distant canopies keep their
  // coverage instead of breaking into single-pixel sparkles that crawl as the camera moves
  float grow = 0.0;
#ifdef NMK_FS
  grow = clamp(length(fwidth(uv))*0.35, 0.0, 0.03);
#endif
  if (uv.x > 1.5) {   // styled cards (foliage.cpp): [2,4) fern frond, [4,6) nettle leaf, [6,8) stem strip
    float st = floor(uv.x*0.5); vec2 q = vec2(uv.x - st*2.0, uv.y);
    float d = abs(q.x - 0.5)*2.0;
    if (st < 1.5) {   // rachis and slanted pinnae under a tapering envelope
      float env = sin(3.14159*min(1.0, q.y*1.08))*(1.0 - 0.35*q.y);
      if (d < 0.07 + grow) return 1.0;
      if (d > env + grow) return 0.0;
      return fract(q.y*16.0 - d*1.3) < 0.6 + grow*8.0 ? 1.0 : 0.0;
    }
    if (st < 2.5) return d < sin(3.14159*pow(q.y, 0.75))*0.92 + 0.06*sin(q.y*42.0) + grow ? 1.0 : 0.0;   // serrated ovate leaf
    return 1.0;
  }
  float a = 0.0;
  for (int i = 0; i < 6; ++i) {
    vec2 c = vec2(hash12(vec2(seed, float(i))), hash12(vec2(float(i), seed + 3.1)))*0.64 + 0.18;
    float ang = hash12(vec2(seed + float(i), 7.0))*6.2832;
    vec2 d = uv - c; d = mat2(cos(ang), -sin(ang), sin(ang), cos(ang))*d; d.x *= 2.3;
    if (dot(d, d) < 0.028 + grow) a = 1.0;
  }
  return a;
}
)GLSL";
static const char* kLeafFS = R"GLSL(
in vec3 vW, vN; in vec2 vUV; in float vM, vShade; flat in float vSeed;
flat in float vLayer; in vec3 vLN;
in vec4 vCC, vPC;
uniform vec3 uLeafCol;
layout(location=4) out vec4 oVel;     // screen motion (previous uv - current uv), a = 1: valid
layout(location=0) out vec4 oColor;
layout(location=2) out vec4 oAmb;
layout(location=3) out vec4 oNR;
float leafAlpha(vec2 uv, float seed);
void main(){
  vec4 tx = vLayer < 0.0 ? vec4(1.0) : texture(uLeaves, vec3(vUV, vLayer));
  if ((vLayer < 0.0 ? leafAlpha(vUV, vSeed) : tx.a) < 0.5) discard;
  vec3 N = normalize(vN);
  float var = hash12(vec2(vSeed, 2.0));
  // textured: uLeafCol is the asset's leafColor / the species mean, so the crown keeps its authored colour
  vec3 alb = vLayer < 0.0 ? uLeafCol*(0.72 + 0.56*var)*vShade : tx.rgb*uLeafCol*(0.86 + 0.28*var)*vShade;
  alb = mix(alb, alb*vec3(1.08, 0.95, 0.7), hash12(vec2(vSeed, 5.0))*0.35);   // some yellowed / olive leaves
  // seasonal variation: whole crowns drift fresher or yellower-olive on a slow world-space noise (neighbouring trees
  // differ) and clusters inside a crown vary a little, so a row of one species never reads as copies
  float sv = vnoise(vec3(vW.xz*0.045, 3.7))*0.7 + vnoise(vW*0.4)*0.3;
  alb *= mix(vec3(0.9, 1.04, 0.92), vec3(1.1, 1.0, 0.78), smoothstep(0.25, 0.75, sv));
  float sh = 1.0;
  if (uShadowSoft > 0.0) sh = sunShadowT(vW + N*0.06, N, 1.0, 8) * cloudShadow(vW);   // soft: thin leaves blur the map
  else if (uShadowOn == 1) { vec4 lp = uLightVP*vec4(vW + N*0.12, 1.0); vec3 sc = lp.xyz/lp.w*0.5 + 0.5;
    if (sc.x > 0.0 && sc.x < 1.0 && sc.y > 0.0 && sc.y < 1.0 && sc.z < 1.0) sh = texture(uShadow, vec3(sc.xy, sc.z - 0.002)); }
  float ndl = max(dot(N, uSunDir), 0.0)*0.7 + 0.3;
  vec3 irr = mix(uGroundCol, uSkyCol, 0.5 + 0.5*N.y)*mix(0.45, 1.0, 0.5 + 0.5*N.y);
  float lsv = uShadowSoft > 0.0 ? skyVis(vW, N) : 1.0;
  if (uShadowSoft > 0.0) irr *= mix(0.4, 1.0, lsv);   // the inner canopy sees less sky
  irr = giAmbient(vW, N, lsv, irr);                   // L2: probe light by night / under cover
  vec3 V = normalize(uCamPos - vW);
  // subsurface: light through the leaf toward the viewer, tinted by the leaf (stronger looking into the sun)
  float VoL = max(dot(-V, uSunDir), 0.0);
  vec3 sss = alb*normalize(alb + 1e-4)*uSunCol*(0.05 + 0.22*pow(VoL, 6.0))*sh;
  vec3 c = alb/PI*uSunCol*ndl*sh + irr*alb*0.7*vShade + (uShadowSoft > 0.0 ? sss : alb*uSunCol*0.08*pow(VoL, 4.0)*sh);
  if (vLayer >= 0.0) {   // thin-leaf backlight: sun on the far face of the leaf as seen from here (warmer, still shadowed)
    vec3 LN = normalize(vLN); if (dot(LN, V) < 0.0) LN = -LN;
    c += alb*vec3(1.0, 1.05, 0.6)*uSunCol*0.12*max(dot(-LN, uSunDir), 0.0)*sh;
  }
  if (uLightCount > 0) {   // lamps: wrapped diffuse, both faces of the card (thin leaves)
    vec3 ld, ls, lb; localLights(vW, N, N, V, -1.0, vec3(0.04), ld, ls); localLights(vW, -N, -N, V, -1.0, vec3(0.04), lb, ls);
    c += alb/PI*(ld + lb*0.6)*vShade;
  }
  c = applyFog(c, vW);
  oColor = vec4((any(isnan(c)) || any(isinf(c))) ? vec3(0.0) : c, 1.0);
  oAmb = vec4(irr*alb*0.7*vShade*gFogT*0.5, 1.0);   // canopies: half weight (their own sky occlusion already darkens them)
  oNR = vec4(N, 1.0);
  oVel = vec4(vPC.xy/vPC.w*0.5 - vCC.xy/vCC.w*0.5, 0.0, 1.0);
}
)GLSL";
static const char* kLeafShadowFS = R"GLSL(
in vec3 vW, vN; in vec2 vUV; in float vM, vShade; flat in float vSeed;
flat in float vLayer;
float leafCut(vec2 uv, float seed, float layer);
void main(){ if (leafCut(vUV, vSeed, vLayer) < 0.5) discard; }   // the same cutout as the shaded leaves
)GLSL";

static const char* kShadowVS = R"GLSL(
#version 450 core
layout(location=0) in vec3 aPos;
uniform mat4 uModel, uVP;
void main(){ gl_Position = uVP*uModel*vec4(aPos,1.0); }
)GLSL";
static const char* kDepthVS = R"GLSL(   // R1 zprepass experiment: kMainVS's exact transform
layout(location=0) in vec3 aPos;
uniform mat4 uModel, uVP;
invariant gl_Position;
void main(){ vec4 w = uModel*vec4(aPos,1.0); gl_Position = uVP*w; }
)GLSL";
static const char* kShadowFS = R"GLSL(
#version 450 core
void main(){}
)GLSL";

static const char* kTerrainVS = R"GLSL(
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNrm; layout(location=2) in float aDirt; layout(location=3) in vec3 aEw;
uniform mat4 uVP;
out vec3 vW, vN; out float vDirt; out vec3 vEw;
void main(){ vW = aPos; vN = aNrm; vDirt = aDirt; vEw = aEw; gl_Position = uVP*vec4(aPos,1.0); }
)GLSL";
static const char* kTerrainFS = R"GLSL(
in vec3 vW, vN; in float vDirt; in vec3 vEw;   // vEw: GX1 earthworks (terrain_cuts.cpp): packed mud, freshly turned earth, wet
uniform int uGrid; uniform vec2 uBuildHalf;
uniform int uDiscard; uniform sampler2D uTileMask; uniform float uTileSize; uniform vec2 uTerrainOrg, uTerrainExt;
uniform sampler2D uSurf;   // R wear, G wet, B route kind, A rut (surface.cpp)
uniform int uTCOn; uniform sampler2DArray uTC; uniform vec4 uTCMap;   // R1 terrain cache: on, texture (unit 24), xy = origin, zw = 1 / extent
float vn2(vec2 q, float z){   // == vnoise(vec3(q, z)) for a whole-number z (f.z = 0): the 4 hashes that matter
  vec2 i = floor(q), f = fract(q); f = f*f*(3.0-2.0*f);
  return mix(mix(hash13(vec3(i, z)), hash13(vec3(i + vec2(1,0), z)), f.x), mix(hash13(vec3(i + vec2(0,1), z)), hash13(vec3(i + vec2(1,1), z)), f.x), f.y);
}
layout(location=0) out vec4 oColor;
layout(location=2) out vec4 oAmb;
layout(location=3) out vec4 oNR;
layout(location=4) out vec4 oVel;
void main(){
  vec3 p = vW;
  vec3 tdx = dFdx(p), tdy = dFdy(p), tn0 = normalize(vN); float tDist = length(p - uCamPos);   // L4 terrain layers (before any discard)
  if (uDiscard == 1) {   // base mesh hidden under high-resolution deformation tiles
    ivec2 tc = ivec2(floor((p.xz - uTerrainOrg)/uTileSize));
    if (texelFetch(uTileMask, tc, 0).r > 0.5) discard;
  }
  vec4 S = texture(uSurf, (p.xz - uTerrainOrg)/uTerrainExt);
  float wear = S.r, wet = S.g, kind = S.b, rut = S.a;
  // four scales so nothing tiles: broad fields (80 m), patches (14 m), clumps (1 m), grit (0.2 m)
  float dCam = length(p - uCamPos), nearK = 1.0 - smoothstep(25.0, 70.0, dCam);
  vec2 rq = mat2(0.8, -0.6, 0.6, 0.8)*p.xz;                                         // rotated: no lattice alignment
  float nB, nM, nF, nX, nClov, clump, streak, litN, flatN, bareN, stoneN, pebN, trkN, nZ, swN;
  if (uTCOn == 1) {   // R1 terrain cache (rendering.md 13): the stable xz-only fields come baked, the rest live and 2D
    vec2 tuv = (p.xz - uTCMap.xy)*uTCMap.zw, gx = tdx.xz*uTCMap.zw, gy = tdy.xz*uTCMap.zw;
    vec4 c0 = textureGrad(uTC, vec3(tuv, 0.0), gx, gy), c1 = textureGrad(uTC, vec3(tuv, 1.0), gx, gy);
    nB = c0.r; nM = c0.g; nF = c0.b; nClov = c0.a;
    vec3 fq = vec3(p.xz*0.35, 16.0)*2.03*2.03, bq = vec3(rq*0.8, 17.0)*2.03;          // the fbm octaves finer than the cache
    flatN = c1.r + 0.125*vnoise(fq) + 0.0625*vnoise(fq*2.03);
    bareN = c1.g + 0.25*vnoise(bq) + 0.125*vnoise(bq*2.03) + 0.0625*vnoise(bq*2.03*2.03);
    nX = vn2(p.xz*4.3, 4.0); clump = vn2(rq*3.1, 19.0); stoneN = vn2(p.xz*9.0, 7.0); pebN = vn2(p.xz*6.5, 11.0); trkN = vn2(p.xz*1.7, 12.0); nZ = vn2(p.xz*4.3, 5.0);
    streak = 0.5; litN = 0.0; swN = 0.5;   // near-only sward detail: every use is scaled by nearK (sand takes the mean)
    if (nearK > 0.0) {
      vec2 sw = p.xz*vec2(13.0, 38.0) + vec2(vn2(p.xz*1.3, 13.0)*6.0, 0.0);
      vec3 lq = vec3(rq*7.0, 15.0)*2.03;
      streak = vn2(sw, 14.0); swN = vn2(sw*0.7, 18.0); litN = 0.5*vn2(rq*7.0, 15.0) + 0.25*vnoise(lq) + 0.125*vnoise(lq*2.03) + 0.0625*vnoise(lq*2.03*2.03);
    }
  } else {   // the live pre-R1 fields (terraincache off)
    nB = fbm(vec3(p.xz*0.012, 1.0)); nM = fbm(vec3(p.xz*0.07, 2.0)); nF = vnoise(vec3(p.xz*0.9, 3.0)); nX = vnoise(vec3(p.xz*4.3, 4.0));
    nClov = vnoise(vec3(p.xz*0.9, 9.0)); clump = vnoise(vec3(mat2(0.8, -0.6, 0.6, 0.8)*p.xz*3.1, 19.0));
    vec2 sw = p.xz*vec2(13.0, 38.0) + vec2(vnoise(vec3(p.xz*1.3, 13.0))*6.0, 0.0);   // streaks along a meandering lay
    streak = vnoise(vec3(sw, 14.0)); litN = fbm(vec3(rq*7.0, 15.0)); flatN = fbm(vec3(p.xz*0.35, 16.0)); bareN = fbm(vec3(rq*0.8, 17.0));
    stoneN = vnoise(vec3(p.xz*9.0, 7.0)); pebN = vnoise(vec3(p.xz*6.5, 11.0)); trkN = vnoise(vec3(p.xz*1.7, 12.0));
    nZ = vnoise(vec3(p.xz*4.3, 5.0)); swN = vnoise(vec3(sw*0.7, 18.0));
  }
  vec3 g1 = vec3(0.12,0.19,0.06), g2 = vec3(0.21,0.26,0.085), dry = vec3(0.32,0.30,0.165);
  vec3 grass = mix(g1, g2, smoothstep(0.3, 0.7, nB));
  grass = mix(grass, dry, smoothstep(0.6, 0.82, nM)*0.5);
  grass *= 0.78 + 0.32*nF;
  grass *= 1.0 - 0.16*smoothstep(0.65, 0.8, nClov);   // clover / darker tufts
  grass *= mix(1.0, mix(0.5, 0.85, clump), 0.5 + 0.5*nearK);                                             // shadowed sward depth between plants
  // near-field sward texture (fades with distance so it never shimmers): blade-scale streaks, flattened grass,
  // organic litter and exposed soil between plants - the ground reads as turf even where grass is culled
  float litter = smoothstep(0.62, 0.8, litN);
  float flat_ = smoothstep(0.6, 0.78, flatN);                  // flattened, lighter patches
  float bare = smoothstep(0.62, 0.8, bareN);
  grass *= mix(1.0, 0.88 + 0.24*streak, nearK);
  grass = mix(grass, grass*vec3(1.18, 1.12, 0.95) + vec3(0.015, 0.012, 0.0), flat_*0.6);
  grass = mix(grass, vec3(0.11, 0.085, 0.055), litter*0.25*nearK);
  grass = mix(grass, mix(vec3(0.20,0.155,0.10), vec3(0.28,0.22,0.145), nF), bare*0.65);   // GX3: bare-earth patches (grass.cpp bareField thins the clumps there)
  vec3 soil = mix(vec3(0.20,0.155,0.10), vec3(0.28,0.22,0.145), nF) * (0.85 + 0.3*nX);
  vec3 mud = mix(vec3(0.12,0.095,0.07), vec3(0.17,0.135,0.10), nX);
  float stone = step(0.6, stoneN);
  vec3 grav = mix(vec3(0.34,0.32,0.28), vec3(0.46,0.43,0.38), stone) * (0.8 + 0.3*nX);
  vec3 track = mix(soil, grav, smoothstep(0.75, 1.0, kind));
  // compacted earth: trodden patches, small stones and wheel-polished strips break the track up at 0.2-2 m
  float pebble = smoothstep(0.78, 0.9, pebN);
  track = mix(track * (0.82 + 0.3*trkN), track*1.25 + vec3(0.03), pebble*0.45);
  track = mix(track, mix(vec3(0.40,0.345,0.24), vec3(0.47,0.41,0.29), nF)*(0.9 + 0.2*nX) + vec3(0.02,0.018,0.012)*streak, 1.0 - smoothstep(0.07, 0.16, abs(kind - 0.251)));   // W5: beach sand (route kind "sand", surface.cpp)
  // grass breaks up into the track along a noisy edge; ruts and damp lows turn to mud
  float tw = smoothstep(0.25, 0.75, wear + (nF - 0.5)*0.35 + (nX - 0.5)*0.15);
  // L4 (textures.cpp, docs/textures.md): world-space planar ambientCG detail on each ground layer, multiplied against the
  // set's mean so the procedural palette and its broad variation stay; layers whose weight is ~0 take no samples
  TerrT tI = TerrT(vec3(1.0), 1.0, 1.0, vec3(0.0)), tG = tI, tS = tI, tV = tI, tA = tI;
  float wGrav = smoothstep(0.75, 1.0, kind), wSand = 1.0 - smoothstep(0.07, 0.16, abs(kind - 0.251)), wSoil = (1.0 - wGrav)*(1.0 - wSand);
  if (tw < 0.995) tG = terrTex(0, p, tn0, tdx, tdy, tDist);
  if (tw > 0.005 && wSoil > 0.01) tS = terrTex(1, p, tn0, tdx, tdy, tDist);
  if (tw > 0.005 && wGrav > 0.01) tV = terrTex(3, p, tn0, tdx, tdy, tDist);
  if (tw > 0.005 && wSand > 0.01) tA = terrTex(4, p, tn0, tdx, tdy, tDist);
  float wT = max(wSoil + wGrav*(1.0 - wSand) + wSand, 1e-4);
  TerrT tTr = TerrT((tS.alb*wSoil + tV.alb*wGrav*(1.0 - wSand) + tA.alb*wSand)/wT, (tS.r*wSoil + tV.r*wGrav*(1.0 - wSand) + tA.r*wSand)/wT,
                    (tS.ao*wSoil + tV.ao*wGrav*(1.0 - wSand) + tA.ao*wSand)/wT, (tS.dn*wSoil + tV.dn*wGrav*(1.0 - wSand) + tA.dn*wSand)/wT);
  grass *= tG.alb; track *= tTr.alb;
  vec3 tdn = mix(tG.dn, tTr.dn, tw); float trr = mix(tG.r, tTr.r, tw), tao = mix(tG.ao, tTr.ao, tw);
  vec3 alb = mix(grass, track, tw);
  // W5b: slope material. Soil and scree from ~28 to ~42 deg, bare rock above ~45 deg (cliffs, quarry and ravine
  // walls): grey-brown limestone and darker schist in beds, bedding lines, vertical joints, lichen on ledges.
  // World-space 3D noise, so vertical faces are textured, not stretched. Wear / route / rut / wet still apply below.
  float rockK = 0.0, screeK = 0.0, rCrack = 0.0; vec3 rBump = vec3(0.0);
  float sny = normalize(vN).y + (nM - 0.5)*0.07;                       // band edges wander
  float ewK = clamp(vEw.x + vEw.y, 0.0, 1.0);                           // GX1: dug earth is never rock or scree
  float ewRock = 1.0 - smoothstep(0.0, 0.12, ewK);                       // (any earthworks weight at all suppresses it)
  screeK = smoothstep(0.89, 0.75, sny)*ewRock;
  if (screeK > 0.001) {
    float rBreak = fbm(vec3(p.xz*0.21, p.y*0.21 + 40.0));
    rockK = smoothstep(0.75, 0.62, sny + (rBreak - 0.5)*0.16)*ewRock;
    float lith = smoothstep(0.35, 0.65, fbm(vec3(p.xz*0.018, p.y*0.03 + 41.0)));              // limestone <-> schist
    float warp = fbm(vec3(p.xz*0.06, p.y*0.02 + 42.0))*5.0;
    float bedY = p.y*0.42 + warp;                                                              // ~2.4 m beds
    float bedV = fract(sin(floor(bedY)*91.7)*4375.85);                                         // per-bed tone
    float bedLine = 1.0 - smoothstep(0.0, 0.07, min(fract(bedY), 1.0 - fract(bedY)));          // bedding planes
    float thin = smoothstep(0.82, 0.97, sin((p.y + warp*0.4)*7.5)*0.5 + 0.5);                  // fine laminae (schist)
    float jx = (p.x*0.62 + p.z*0.78)*0.29 + (vnoise(vec3(p.xz*0.05, p.y*0.07 + 43.0)) - 0.5)*0.7;      // sub-vertical joints ~3.4 m apart
    rCrack = (1.0 - smoothstep(0.012, 0.04, abs(fract(jx) - 0.5))) * step(0.45, vnoise(vec3(floor(jx)*1.7, floor(bedY*0.5)*1.3, 52.0)));   // some run, some stop at a bed
    float grain = vnoise(vec3(p*3.1 + 44.0)), mott = fbm(vec3(p*0.6 + 45.0));
    vec3 lime = mix(vec3(0.44,0.40,0.33), vec3(0.57,0.52,0.43), bedV) * (0.74 + 0.42*mott);
    vec3 schist = mix(vec3(0.24,0.23,0.21), vec3(0.34,0.31,0.27), bedV) * (0.78 + 0.36*mott) * (1.0 - 0.25*thin*lith);
    vec3 rock = mix(lime, schist, lith) * (0.9 + 0.2*grain);
    rock *= 1.0 - 0.4*bedLine - 0.42*rCrack;
    rock = mix(rock, vec3(0.30,0.22,0.15), 0.25*smoothstep(0.55, 0.8, fbm(vec3(p*0.25 + 46.0))));      // iron staining
    float lichen = smoothstep(0.62, 0.8, vnoise(vec3(p*0.45 + 47.0))) * smoothstep(0.35, 0.65, sny + 0.25);
    rock = mix(rock, mix(vec3(0.16,0.19,0.09), vec3(0.42,0.41,0.30), grain), lichen*0.55);             // moss / lichen
    // scree: brown soil with loose grey stones, collecting toward the foot of the face
    float pebs = smoothstep(0.55, 0.72, vnoise(vec3(p*4.2 + 48.0))) + 0.5*smoothstep(0.6, 0.8, vnoise(vec3(p*1.4 + 49.0)));
    vec3 scr = mix(soil*0.95, mix(lime, schist, lith)*(0.7 + 0.25*grain), clamp(pebs, 0.0, 1.0)*0.6);
    TerrT tSc = terrTex(6, p, tn0, tdx, tdy, tDist), tRk = rockK > 0.01 ? terrTex(5, p, tn0, tdx, tdy, tDist) : tI;   // L4
    scr *= tSc.alb; rock *= tRk.alb;
    float sK = screeK*(1.0 - 0.6*tw);
    tdn = mix(mix(tdn, tSc.dn, sK), tRk.dn, rockK); trr = mix(mix(trr, tSc.r, sK), tRk.r, rockK); tao = mix(mix(tao, tSc.ao, sK), tRk.ao, rockK);
    alb = mix(alb, scr, screeK*(1.0 - 0.6*tw));
    alb = mix(alb, rock, rockK);
    rBump = vec3(vnoise(vec3(p*1.7 + 50.0)) - 0.5, (bedLine - 0.5)*0.6, vnoise(vec3(p*1.7 + 51.0)) - 0.5)*1.1;
  }
  float mudK = clamp(rut*0.85 + wet*0.6, 0.0, 1.0)*tw;
  if (mudK > 0.01) { TerrT tM = terrTex(2, p, tn0, tdx, tdy, tDist); mud *= tM.alb; tdn = mix(tdn, tM.dn, mudK); trr = mix(trr, tM.r, mudK); tao = mix(tao, tM.ao, mudK); }   // L4
  alb = mix(alb, mud, mudK);
  // crater and disturbed soil
  float dirt = clamp(vDirt, 0.0, 1.0);
  float dirtK = smoothstep(0.15, 0.75, dirt + (nM - 0.5)*0.35);
  if (dirtK > 0.01) { TerrT tC = terrTex(7, p, tn0, tdx, tdy, tDist); soil *= tC.alb; tdn = mix(tdn, tC.dn, dirtK); trr = mix(trr, tC.r, dirtK); tao = mix(tao, tC.ao, dirtK); }   // L4
  alb = mix(alb, soil, dirtK);
  if (ewK > 0.001) {   // GX1 trench cuts and spoil: dark packed mud (wetter low down), freshly turned crumbly earth
    TerrT tM = terrTex(2, p, tn0, tdx, tdy, tDist), tF = terrTex(7, p, tn0, tdx, tdy, tDist);
    float cl = fbm(vec3(p*2.4 + 60.0)), cg = vnoise(vec3(p*9.0 + 61.0));
    // the ground-projected texture sets streak on the near-vertical cut walls: there, procedural earth with spade marks
    float flatK = smoothstep(0.35, 0.75, tn0.y);
    float spade = smoothstep(0.55, 0.75, vnoise(vec3(p.xz*1.1, p.y*7.0 + 62.0)))*(1.0 - flatK);   // horizontal cut lines
    vec3 pmud = mix(vec3(0.085,0.068,0.052), vec3(0.135,0.105,0.078), cl*0.7 + cg*0.3) * mix(vec3(1.0), tM.alb, flatK) * (1.0 - 0.25*spade);
    vec3 turned = mix(vec3(0.07,0.054,0.04), vec3(0.155,0.118,0.085), smoothstep(0.3, 0.75, cl)) * (0.85 + 0.3*cg) * mix(vec3(1.0), tF.alb, flatK);
    float mk = vEw.x/max(vEw.x + vEw.y, 1e-3);
    tM.dn *= flatK; tF.dn *= flatK;
    alb = mix(alb, mix(turned, pmud, mk), max(ewK, (1.0 - ewRock)*(1.0 - flatK)));   // steep earthworks faces fully earth
    alb *= mix(1.0, 0.72, vEw.z*ewK);
    tdn = mix(tdn, mix(tF.dn, tM.dn, mk), ewK); trr = mix(trr, mix(tF.r, tM.r*0.7, mk), ewK); tao = mix(tao, mix(tF.ao, tM.ao, mk), ewK);
  }
  PaintOut po = paintApply(p, normalize(vN), alb, dirt, dFdx(p), dFdy(p), length(p - uCamPos));   // lasso ground paint: albedo (ground_paint.cpp)
  // localized wetness: darker, and glossy only where water stands (puddles follow ruts / lows)
  float puddle = smoothstep(0.55, 0.8, wet + (nX - 0.5)*0.2);
  alb *= mix(1.0, 0.62, clamp(wet*1.2, 0.0, 1.0));
  float rough = mix(mix(0.96, 0.86, tw), 0.10, puddle);
  rough = mix(rough, 0.78, vEw.z*ewK);   // GX1: wet mud low in the cut: a dull damp sheen, not a mirror
  vec3 N = normalize(vN + vec3(nX - 0.5, 0.0, nZ - 0.5)*mix(0.22, 0.42, max(tw, dirt))*(1.0 - puddle)
                     + vec3(streak - 0.5, 0.0, swN - 0.5)*0.35*nearK*(1.0 - tw)*(1.0 - puddle));   // sward relief (shading only)
  float ao = (1.0 - 0.25*dirt) * (1.0 - 0.18*rut*tw);
  if (screeK > 0.001) {   // W5b: rock relief and joint shadow (shading only)
    N = normalize(mix(N, normalize(normalize(vN) + rBump), max(rockK, screeK*0.4)));
    rough = mix(rough, 0.9, rockK);
    ao *= 1.0 - 0.35*rCrack*rockK;
  }
  N = normalize(N + tdn*(1.0 - puddle)); rough = clamp(rough*mix(trr, 1.0, puddle), 0.04, 1.0); ao *= tao;   // L4 terrain layers
  paintFinish(po, rough, N, ao);   // lasso ground paint: roughness, AO, relief over every other layer (after L4)
  vec3 c = shade(vW, N, alb, rough, 0.0, ao);
  if (uGrid == 1 && abs(p.x) <= uBuildHalf.x && abs(p.z) <= uBuildHalf.y) {
    vec2 g = abs(fract(p.xz/5.0 + 0.5) - 0.5)*5.0;
    vec2 fw = fwidth(p.xz)*1.2;
    float line = 1.0 - min(smoothstep(0.0, fw.x, g.x), smoothstep(0.0, fw.y, g.y));
    float d = length(p - uCamPos);
    c = mix(c, vec3(0.85,0.85,0.7), line*0.35*(1.0 - smoothstep(60.0, 220.0, d)));
  }
  c = applyFog(c, vW);
  oColor = vec4((any(isnan(c)) || any(isinf(c))) ? vec3(0.0) : c, 1.0);
  oAmb = vec4(gInd*gFogT, 1.0);
  oNR = vec4(N, rough + (1.0 - gFogT));   // puddles (rough 0.1) are the main mirror-like surface outdoors
  oVel = vec4(0.0);
}
)GLSL";

// R1 terrain material cache bake (rendering.md 13): kTerrainFS's xz-only noise fields at texel centres, the same
// expressions as its live path. Layer 0: nB, nM, nF, clover; layer 1: the cache-resolved octaves of flat_ and bare.
static const char* kTerrainCacheCS = R"GLSL(
layout(local_size_x = 8, local_size_y = 8) in;
layout(rgba8, binding = 0) uniform writeonly image2DArray uOut;
uniform vec2 uOrg; uniform float uTexel;
void main(){
  ivec2 id = ivec2(gl_GlobalInvocationID.xy);
  if (any(greaterThanEqual(id, imageSize(uOut).xy))) return;
  vec2 xz = uOrg + (vec2(id) + 0.5)*uTexel, rq = mat2(0.8, -0.6, 0.6, 0.8)*xz;
  vec3 fq = vec3(xz*0.35, 16.0);
  imageStore(uOut, ivec3(id, 0), vec4(fbm(vec3(xz*0.012, 1.0)), fbm(vec3(xz*0.07, 2.0)), vnoise(vec3(xz*0.9, 3.0)), vnoise(vec3(xz*0.9, 9.0))));
  imageStore(uOut, ivec3(id, 1), vec4(0.5*vnoise(fq) + 0.25*vnoise(fq*2.03), 0.5*vnoise(vec3(rq*0.8, 17.0)), 0.0, 0.0));
}
)GLSL";

// ---- instanced grass: pasture clumps of up to 12 cards (GX3, grass.cpp) and nature-patch plants (nature.cpp);
// alpha-tested procedural silhouettes, rooted wind bend
static const char* kGrassVS = R"GLSL(
layout(location=0) in vec3 aB; layout(location=1) in vec4 iP; layout(location=2) in vec4 iS;
uniform mat4 uVP; uniform float uTime, uRadius, uWidthScale, uWind;
uniform mat4 uVPU, uPrevVP; uniform float uPrevTime;
uniform float uCount, uShadowPass;   // pasture: clumps in this chunk (0 = nature patch plants); sun-shadow pass (radius)
uniform vec4 uLod, uLod2;            // pasture LOD: drawn share, near r, mid r, falloff | floor, -, -, wind r
uniform vec4 uCards;                 // G1: this species group's near / mid / far cards | shadow-pass cards (0 = main pass)
out vec3 vW, vAlb, vF; out vec2 vUV; out float vWid, vTap;   // vTap: G1 blade card width / full card width (trimmed padding)
out vec4 vCC, vPC;
flat out int vSp; flat out int vPart; flat out float vSeed;   // part: 0 blade / stem, 1 flower head (disc), 2 basal leaf; pasture 10+
// GX3 pasture clump: cards pre-arranged around the root (tall centre, short outward-leaning rim), leaning with the
// slope, the base blended into the ground colour, base AO, wind only near the eye; LOD fades by rank (no pop)
void pastureClump(){
  vec3 base = iP.xyz;
  float d = length(base - uCamPos);
  float bl = aB.z, v = aB.y, k = bl/11.0;
  vec4 pk = unpackUnorm4x8(floatBitsToUint(iS.z)), py = unpackUnorm4x8(floatBitsToUint(iS.y)), pg = unpackUnorm4x8(floatBitsToUint(iS.w));
  int sp = clamp(int(pk.a*255.0 + 0.5) - 56, 0, 23);
  float ph = fract(sin(dot(base.xz, vec2(12.9898, 78.233)))*43758.5453);
  float r1 = fract(sin(ph*91.7 + bl*78.233)*43758.5453), r2 = fract(sin(ph*39.35 + bl*11.135)*24634.6345), r3 = fract(sin(ph*17.13 + bl*53.71)*9631.77);
  float dens = d <= uLod.y ? 1.0 : max(uLod2.x, pow(uLod.y/d, uLod.w));
  float keep = uLod.x*dens, rank = (float(gl_InstanceID) + 0.5)/uCount;
  float vis = 1.0 - smoothstep(keep*0.8, keep, rank);
  float nb = d <= uLod.y ? uCards.x : d <= uLod.z ? mix(uCards.x, uCards.y, smoothstep(uLod.y, uLod.z, d)) : mix(uCards.y, uCards.z, smoothstep(uLod.z, uRadius, d));
  float fade = (1.0 - smoothstep(uRadius*0.72, uRadius, d))*vis*clamp((uShadowPass > 0.0 ? uCards.w : nb) - bl, 0.0, 1.0);
  if (uShadowPass > 0.0) fade *= 1.0 - smoothstep(uShadowPass*0.7, uShadowPass, d);   // G1: casters shrink away toward the shadow radius (no ring)
  if (fade <= 0.0) {   // clump / card past its LOD cut: the whole card leaves the clip volume (no shading, no raster)
    gl_Position = vec4(0.0, 0.0, -2.0, 1.0); vW = vAlb = vF = vec3(0.0); vUV = vec2(0.0); vWid = 0.0; vTap = 1.0; vCC = vPC = vec4(0.0, 0.0, 0.0, 1.0);
    vSp = 0; vPart = 10; vSeed = 0.0; return;
  }
  float widen = min(3.2, pow(clamp(dens*nb/12.0, 0.05, 1.0), -0.6));   // fewer clumps / cards: the rest widen
  float H = iS.x, W = py.x*0.1;
  // card roles: 10 grass blade, 11 clover leaflet, 12 seed-head stem, 13 shrub twig, 14 broad leaf, 15 plantain spike
  int part = 10;
  float spread = 0.15, hf = mix(1.0, 0.5, k)*(0.75 + 0.45*r2), w = W*(0.8 + 0.4*r1), curve = 0.2 + 0.4*r1 + 0.7*k, lift = 1.0, windAmp = 1.0;
  float ang = iP.w + bl*2.39996 + r1*0.6, taper = 1.0 - v*0.65;
  if (sp == 0) { curve = 0.3 + 0.4*r1 + 0.6*k; spread = 0.12; windAmp = 0.35; }
  else if (sp == 4) { curve = 0.5 + 0.5*r1; windAmp = 0.8; }
  else if (sp == 2) {
    if (bl < 2.5) { part = 12; hf = 0.88 + 0.14*r2; w = v > 0.7 ? 0.011 + 0.004*r1 : 0.004; curve = 0.04 + 0.10*r1; spread = 0.03; windAmp = 1.8; taper = 1.0; }
    else { hf = mix(0.62, 0.36, k)*(0.8 + 0.4*r2); curve = 0.4 + 0.4*r1; spread = 0.06; windAmp = 1.2; }
  } else if (sp == 3) {
    if (bl < 8.5) { part = 11; float leaf = floor(bl/3.0); ang = iP.w + leaf*2.1 + (bl - leaf*3.0 - 1.0)*0.95 + r3*0.2; windAmp = 0.15; taper = 1.0; }
    else { hf = 1.5*(0.8 + 0.4*r2); w = 0.007; windAmp = 0.35; }
  } else if (sp == 17) {
    if (bl < 6.5) { part = 14; hf = 0.8 + 0.4*r2; lift = 0.65; curve = 0.6 + 0.4*r1; spread = 0.02; windAmp = 0.2; taper = 1.0; }
    else if (bl < 8.5) { part = 15; hf = 2.2*(0.85 + 0.3*r2); w = v > 0.6 ? 0.010 : 0.0035; curve = 0.08 + 0.1*r1; spread = 0.02; windAmp = 1.2; taper = 1.0; }
    else { hf = 1.8*(0.8 + 0.4*r2); w = 0.007; windAmp = 0.5; }
  } else if (sp == 16) {
    part = 13; spread = 0.14; hf = mix(1.0, 0.6, k)*(0.8 + 0.4*r2); curve = 0.12 + 0.55*k; windAmp = 0.25; taper = 1.0 - v*0.25; ang += r3*1.2;
  } else if (sp == 18) { curve = 0.08 + 0.22*r1 + 0.45*k; spread = 0.10; windAmp = 1.1; }   // marram: stiff, upright tussock
  else curve = 0.12 + 0.4*r1 + 0.8*k;   // medium meadow grass: the rim droops
  float h = H*hf*fade;
  float tap = part == 10 ? max(1.0 - 0.95*v*v*v, 0.1) : 1.0;   // G1: blade cards follow the pointed silhouette (no transparent padding)
  w *= widen*taper*tap;
  vec3 fwd = vec3(sin(ang), 0.0, cos(ang)), side = vec3(cos(ang), 0.0, -sin(ang));
  if (part == 13) side = normalize(side + fwd*(r3 - 0.5)*1.4);
  vec2 nxz = py.yz*2.0 - 1.0; float ny = sqrt(max(0.1, 1.0 - dot(nxz, nxz)));
  float rad = spread*sqrt((bl + 0.5)/12.0)*(0.6 + 0.8*r2);
  vec3 root = base + fwd*rad; root.y -= dot(nxz, fwd.xz*rad)/ny;   // on the slope plane
  vec2 lean = nxz/ny*0.55;                                         // and leaning with it
  float windK = uLod2.w > 0.0 ? 1.0 - smoothstep(uLod2.w*0.75, uLod2.w, d) : 0.0;
  int nk = (windK > 0.0 && uShadowPass == 0.0) ? 2 : 1;
  vec3 pos[2];
  for (int f = 0; f < nk; ++f) {   // f 0: this frame, 1: previous frame (motion vectors)
    float t = f == 0 ? uTime : uPrevTime;
    float gust = sin(t*1.5 + ph*6.28 + dot(base.xz, vec2(0.13, 0.07)))*0.55 + sin(t*0.63 + dot(base.xz, vec2(0.045, 0.11)))*0.45;
    vec3 wv = vec3(0.8, 0.0, 0.6)*(0.08 + gust*0.22*windK)*uWind*windAmp;
    vec3 p;
    if (part == 11) {   // clover leaflet: out from the petiole top, nearly flat
      float L = W*(0.85 + 0.3*r1)*fade;
      p = root + vec3(0.0, h*(0.55 + 0.45*r2), 0.0) + fwd*(v*L*1.1) + side*(aB.x*W*widen*0.5*fade) + vec3(0.0, v*L*0.25, 0.0) + wv*0.3*h;
    } else {
      vec3 bend = fwd*curve + wv;
      p = root + side*(aB.x*w*0.5) + vec3(0.0, v*h*lift, 0.0) + bend*(v*v)*h;
      p.y -= (v*v)*h*dot(bend, bend)*0.35*lift;
    }
    p.xz += lean*(p.y - root.y);
    pos[f] = p;
  }
  if (nk == 1) pos[1] = pos[0];
  vec3 p = pos[0];
  vW = p; vUV = vec2(aB.x*0.5 + 0.5, v); vSp = sp; vPart = part; vWid = w; vTap = tap; vSeed = r1; vF = fwd;
  gl_Position = uVP*vec4(p, 1.0);
  if (uShadowPass > 0.0) { vAlb = vec3(0.0); vCC = vPC = vec4(0.0, 0.0, 0.0, 1.0); return; }
  vCC = uVPU*vec4(p, 1.0); vPC = uPrevVP*vec4(pos[1], 1.0);
  // colour: per-card value, darker saturated base -> lighter drier tip, the lower 30 % takes the ground colour,
  // base AO darker inside the clump and under the canopy
  float r4 = fract(r3*57.3 + r1*13.1);
  vec3 alb = pk.rgb*0.5*(0.74 + 0.5*r3)*mix(vec3(1.0), vec3(1.12, 1.04, 0.78), step(0.82, r4)*0.8), gnd = pg.rgb*0.5;   // per card: value, some yellower
  float dryTip = (sp == 4 ? 0.7 : sp == 2 ? 0.45 : sp == 18 ? 0.35 : sp == 1 ? 0.14 : sp == 0 ? 0.08 : 0.05)*(0.4 + 1.2*r1*r1);
  vec3 col = mix(alb*vec3(0.78, 0.88, 0.72), mix(alb*vec3(1.12, 1.14, 0.9), vec3(0.42, 0.37, 0.21), dryTip), smoothstep(0.2, 1.0, v));
  bool flatLeaf = part == 11 || part == 14;
  if (flatLeaf) col = alb*(0.9 + 0.2*v);
  else if (part != 13) col = mix(gnd, col, smoothstep(0.0, 0.3, v));
  float above = p.y - base.y;
  float ao = mix(0.40 + 0.25*k, 1.0, smoothstep(0.0, flatLeaf ? 0.2 : 0.6, v))*mix(0.68, 1.0, smoothstep(0.0, 0.45, above));   // base and canopy depth (G1: lighter floor, roots are not black)
  if (part == 13) ao = mix(0.45, 1.0, smoothstep(0.0, 0.8, v))*(0.75 + 0.25*k);   // shrub: dark interior, lit dome
  vAlb = col*ao;
}
void plantMain();
void main(){ if (uCount > 0.0) pastureClump(); else plantMain(); }
void plantMain(){
  float d = length(iP.xyz - uCamPos);
  float fade = 1.0 - smoothstep(uRadius*0.72, uRadius, d);
  bool head = aB.z > 9.5;                                  // patch plants (nature.cpp) carry a head quad per blade
  float bl = head ? aB.z - 10.0 : aB.z;
  vec4 pk = unpackUnorm4x8(floatBitsToUint(iS.z));
  // species (grass.h 0 turf 1 medium 2 tall 3 clover 4 straw; nature.h 5 tall meadow, 6 poppy, 7 ox-eye daisy,
  // 8 buttercup, 9 cornflower, 10 cow parsley, 11 foxglove, 12 clover in flower; V9 13 wheat, 14 brush, 15 stubble)
  int sp = clamp(int(pk.a*255.0 + 0.5) - 56, 0, 15);
  // per-blade variation inside the tuft (deterministic from the instance phase)
  float r1 = fract(sin(iS.w*12.9898 + bl*78.233)*43758.5453), r2 = fract(sin(iS.w*39.346 + bl*11.135)*24634.6345);
  float ang = iP.w + bl*2.39996 + r1*0.6;
  vec3 side = vec3(cos(ang), 0.0, -sin(ang));
  vec3 fwd = vec3(sin(ang), 0.0, cos(ang));
  bool fl = sp >= 6 && sp <= 12;                          // flowers (13-15 are stems only)
  int ib = int(bl + 0.5);
  int nL = sp == 5 ? 2 : sp == 12 ? 4 : fl ? 3 : 0;        // basal leaves
  int nS = sp == 5 ? 5 : sp == 6 ? 2 : sp == 7 ? 3 : sp == 8 ? 4 : sp == 9 ? 3 : sp == 10 ? 2 : sp == 11 ? 1 : sp == 12 ? 2 : 7;
  bool isLeaf = sp >= 5 && ib < nL, isStem = sp < 5 || (!isLeaf && ib < nL + nS);
  bool disc = head && isStem && fl && sp != 11;            // flat flower heads (foxglove bells are on its stem)
  float spread = sp == 3 ? 0.05 : sp == 0 ? 0.085 : sp == 2 ? 0.04 : sp == 14 ? 0.12 : sp >= 5 ? (isLeaf ? 0.03 : 0.05) : 0.065;
  vec3 root = iP.xyz + fwd*(spread*(0.4 + r2));
  float h = iS.x * (0.6 + 0.45*r2) * fade;
  if (sp == 2 && bl > 2.5) h = 0.0;                        // stems: three per clump (the rest collapse to nothing)
  if (sp >= 5) h = isLeaf ? iS.x*(sp == 12 ? 0.8 : sp == 5 ? 0.55 : 0.26)*(0.7 + 0.5*r2)*fade : isStem ? iS.x*(0.82 + 0.25*r2)*fade : 0.0;
  if (sp == 13 || sp == 15) h = iS.x*(0.92 + 0.12*r2)*fade;   // an even crop
  if (head && !disc) h = 0.0;
  float v = head ? 1.0 : aB.y;
  float taper = (sp == 3 || (sp == 12 && isLeaf)) ? 1.0 : (1.0 - v*0.8);
  float w;
  if (sp < 5) { w = iS.y * uWidthScale * taper; if (sp == 2 && v > 0.78) w *= 2.6; }
  else if (isLeaf) w = (sp == 5 ? 0.006 : sp == 12 ? 0.026 : 0.016) * uWidthScale * taper * (0.8 + 0.4*r1);
  else if (sp == 5) w = (v > 0.6 ? 0.022*(1.25 - 0.6*v) : iS.y) * uWidthScale;       // panicle on the upper stem
  else if (sp == 11) w = (v > 0.49 ? iS.y*(1.3 - 0.8*v) : 0.007) * uWidthScale;      // foxglove: bell spike
  else if (sp == 13) w = (v > 0.6 ? 0.014 : 0.0055) * uWidthScale;                   // wheat: straw, the ear on the top quad
  else if (sp == 14) w = 0.034 * uWidthScale * (1.0 - 0.45*v);                       // brush: twig with leaf clusters
  else if (sp == 15) w = 0.0065 * uWidthScale;                                       // stubble
  else w = 0.0045 * uWidthScale;
  float lift = (sp == 3 || (sp == 12 && isLeaf)) ? 0.35 : 1.0;   // clover leaves lie low and spread
  float curve = sp == 0 ? 0.30 + 0.35*r1 : sp == 2 ? 0.04 + 0.10*r1 : sp == 3 ? 1.6 + 0.6*r1 : sp == 4 ? 0.55 + 0.45*r1 : sp == 1 ? 0.12 + 0.30*r1
              : sp == 13 ? 0.03 + 0.07*r1 : sp == 14 ? 0.35 + 0.30*r1 : sp == 15 ? 0.03
              : isLeaf ? (sp == 12 ? 1.6 + 0.6*r1 : 0.9 + 0.6*r1) : 0.05 + 0.10*r1;
  float windAmp = sp == 13 ? 1.15 : sp == 14 ? 0.35 : sp == 15 ? 0.05 : sp == 2 ? 1.8 : sp == 0 ? 0.35 : sp == 3 ? 0.15 : sp == 5 ? (isLeaf ? 1.0 : 1.9) : sp == 12 ? 0.2 : fl ? (isLeaf ? 0.5 : 0.9) : 1.0;   // tall stems sway most, turf barely
  float freq = sp == 13 ? 1.25 : sp == 2 ? 1.1 : sp == 5 ? 1.0 : fl ? 1.4 : 1.7;
  float hr = iS.y * (0.85 + 0.3*r2) * min(1.0, fade*3.0);   // flower head radius
  float tilt = sp == 6 ? 0.45 : sp == 7 ? 0.35 : sp == 8 ? 0.30 : sp == 9 ? 0.50 : sp == 10 ? 0.12 : 1.1;
  // wheat / stubble laid flat (nature.cpp: tanks, blasts, trampled crop): shape.y = amount x 63 + direction fraction
  float flat_ = 0.0; vec3 fD = vec3(0.0, 0.0, 1.0);
  if (sp == 13 || sp == 15) { flat_ = floor(iS.y)/63.0; float fa = fract(iS.y)/0.98*6.2831853 + (r1 - 0.5)*0.8; fD = vec3(sin(fa), 0.0, cos(fa)); }
  float tiltA = flat_*1.42*(0.86 + 0.14*r2);
  vec3 axis = vec3(0.0, cos(tiltA), 0.0) + fD*sin(tiltA);
  vec3 sideF = normalize(mix(side, cross(vec3(0.0, 1.0, 0.0), fD), flat_) + vec3(1e-5));
  vec3 pos[2]; vec3 nH = vec3(0.0, 1.0, 0.0);
  for (int k = 0; k < 2; ++k) {   // k 0: this frame, 1: previous frame (motion vectors)
    float t = k == 0 ? uTime : uPrevTime;
    float gust = sin(t*freq + iS.w + dot(iP.xz, vec2(0.13, 0.07)))*0.55 + sin(t*0.63 + dot(iP.xz, vec2(0.045, 0.11)))*0.45;
    if (sp == 13) gust = sin(t*freq - dot(iP.xz, vec2(0.8, 0.6))*0.34)*0.6 + sin(t*0.57 - dot(iP.xz, vec2(0.8, 0.6))*0.11 + iS.w*0.15)*0.4;   // waves rolling across the field
    vec3 bend = fwd*curve*((sp == 3 || (sp == 12 && isLeaf)) ? 0.5 : 1.0) + vec3(0.8, 0.0, 0.6)*(0.08 + gust*0.20)*uWind*windAmp;
    vec3 p;
    if (head) {
      vec3 top = root + vec3(0.0, h*lift, 0.0) + bend*h; top.y -= h*dot(bend, bend)*0.35*lift;
      vec3 n = normalize(vec3(0.0, 1.0, 0.0) + fwd*tilt + bend*0.6);
      vec3 T1 = normalize(side - n*dot(side, n)), T2 = cross(n, T1);
      p = top + (T1*aB.x + T2*(aB.y*2.0 - 1.0))*(disc ? hr : 0.0);
      if (k == 0) nH = n;
    } else if (flat_ > 0.0) {
      p = root + sideF*(aB.x*w*0.5) + axis*(v*h) + bend*(v*v)*h*(1.0 - 0.85*flat_);
    } else {
      p = root + side*(aB.x*w*0.5) + vec3(0.0, v*h*lift, 0.0);
      p += bend*(v*v)*h;            // root (v = 0) never moves
      p.y -= (v*v)*h*dot(bend, bend)*0.35*lift;
    }
    pos[k] = p;
  }
  vec3 p = pos[0];
  vCC = uVPU*vec4(p, 1.0); vPC = uPrevVP*vec4(pos[1], 1.0);
  vec3 alb = pk.rgb*0.5;   // meadow palette baked per tuft on the CPU (cluster colour + species tint)
  if (sp == 2) alb = mix(alb, vec3(0.36, 0.32, 0.18), smoothstep(0.72, 0.9, v));   // straw-coloured seed heads
  vAlb = alb; vW = p; vF = head ? nH : fwd; vUV = head ? vec2(aB.x, aB.y*2.0 - 1.0) : vec2(aB.x*0.5 + 0.5, v); vSp = sp;
  vPart = head ? 1 : isLeaf ? 2 : 0; vWid = w; vTap = 1.0; vSeed = r1;
  gl_Position = uVP*vec4(p, 1.0);
}
)GLSL";
static const char* kGrassFS = R"GLSL(
in vec3 vW, vAlb, vF; in vec2 vUV; in float vWid, vTap;
in vec4 vCC, vPC;
flat in int vSp; flat in int vPart; flat in float vSeed;
uniform float uWidthScale;
layout(location=4) out vec4 oVel;
layout(location=0) out vec4 oColor;
layout(location=2) out vec4 oAmb;
layout(location=3) out vec4 oNR;
void main(){
  float e = abs(vUV.x*2.0 - 1.0)*vTap, v = vUV.y, lv = v;   // e across the full (untrimmed) card
  vec3 F = gl_FrontFacing ? vF : -vF;
  bool past = vPart >= 10;
  vec3 N = normalize(vec3(0.0, 1.0, 0.0) + F*((vPart == 11 || vPart == 14) ? 0.15 : past ? 0.8 : 0.45));
  vec3 alb;
  if (past) {   // GX3 pasture cards: each species' procedural texture (detail fades where a card is under a pixel wide)
    float dk = 1.0 - smoothstep(0.3, 0.9, fwidth(e));
    float s = vUV.x*2.0 - 1.0;
    alb = vAlb;
    if (vPart == 10) {          // grass blade: pointed tip, light midrib, darker margins, fine veins, some browned tips
      if (e > 1.0 - pow(v, 3.0)*0.95) discard;
      alb *= 1.0 + dk*(0.12*(1.0 - smoothstep(0.0, 0.2, e)) - 0.18*smoothstep(0.55, 1.0, e) - 0.05*step(0.5, fract(e*2.5 + 0.25)));
      if (vSeed > 0.8) alb = mix(alb, vec3(0.30, 0.24, 0.13), smoothstep(0.78, 1.0, v)*0.75);
    } else if (vPart == 11) {   // clover leaflet: obcordate (notched tip), pale chevron
      float y = v*2.0 - 1.0;
      if (e > sqrt(max(0.0, 1.0 - y*y)) || (v > 0.86 && e < (v - 0.86)*2.5)) discard;
      float chev = 1.0 - smoothstep(0.03, 0.09, abs(v - 0.32 - e*0.3));
      alb = mix(alb, alb*1.7 + vec3(0.03, 0.04, 0.02), chev*0.5*dk*step(e, 0.75));
      alb *= 1.0 + 0.12*dk*(1.0 - smoothstep(0.0, 0.08, e));
    } else if (vPart == 12) {   // seed-head stem: a dense spike or an open panicle above the culm
      if (v > 0.74) {
        float y = (v - 0.74)/0.26;
        float lobe = 0.72 + 0.28*smoothstep(0.3, 0.7, fract(y*9.0 + vSeed*5.0));
        if (e > pow(max(0.0, 1.0 - (2.0*y - 1.0)*(2.0*y - 1.0)), 0.6)*lobe) discard;
        alb = mix(vec3(0.46, 0.42, 0.27), vec3(0.38, 0.32, 0.21), vSeed)*(0.9 + 0.2*fract(y*9.0 + vSeed*5.0));
      } else if (e > 0.85) discard;
    } else if (vPart == 13) {   // shrub twig: woody stem with alternate oval leaves and a leafy tip
      float yy = v*6.0 + vSeed*3.0, cell = fract(yy), idx = floor(yy);
      float sd = mod(idx, 2.0)*2.0 - 1.0;
      vec2 q = vec2((s - sd*0.46)/0.54, (cell - 0.5)/0.46);
      bool leaf = dot(q, q) < 1.0 && v > 0.12;
      vec2 qt = vec2(s/0.6, (v - 0.9)/0.12);
      leaf = leaf || dot(qt, qt) < 1.0;
      bool twig = abs(s) < 0.08 && v < 0.9;
      if (!leaf && !twig) discard;
      float lh = fract(sin(idx*12.9898 + vSeed*78.233)*43758.5453);
      alb = leaf ? alb*(0.75 + 0.5*lh)*(1.0 + 0.25*dk*(1.0 - dot(q, q))) : vec3(0.16, 0.12, 0.08)*(0.6 + 0.6*v);
    } else if (vPart == 14) {   // plantain leaf: lanceolate, parallel veins
      if (e > sin(3.14159*pow(v, 0.75))*0.95) discard;
      alb *= 1.0 + dk*(0.16*(1.0 - smoothstep(0.0, 0.07, e)) + 0.08*(1.0 - smoothstep(0.0, 0.05, abs(e - 0.45))) - 0.15*smoothstep(0.7, 1.0, e));
    } else {                    // plantain spike: brown seed head over a bare stem
      if (v > 0.62) { if (e > 0.9) discard; alb = vec3(0.24, 0.19, 0.11)*(0.75 + 0.5*step(0.55, fract(v*45.0 + e)) ); }
      else if (e > 0.8) discard;
    }
  } else if (vPart == 1) {   // flower head: disc in [-1, 1]^2 (nature.cpp patches)
    vec2 q = vUV; float r = length(q), th = atan(q.y, q.x);
    float aa = fwidth(r)*1.5;   // distant heads fill in instead of sparkling
    vec3 petal, ctr; float rim, cr;
    if (vSp == 6) { rim = 0.80 + 0.20*abs(cos(2.0*th)); cr = 0.22; petal = mix(vec3(0.62, 0.05, 0.03), vec3(0.72, 0.10, 0.04), vSeed); ctr = vec3(0.05, 0.04, 0.04);
      petal = mix(ctr, petal, smoothstep(cr, cr + 0.16, r)); }                                                                  // poppy: four broad petals, black eye
    else if (vSp == 7) { rim = (0.5 + 0.5*cos(18.0*th + vSeed*6.0)) > 0.22 + aa*4.0 ? 1.0 : 0.30; cr = 0.30; petal = vec3(0.86, 0.86, 0.80); ctr = vec3(0.78, 0.56, 0.05); }   // ox-eye daisy: white rays, yellow disc
    else if (vSp == 8) { rim = 0.68 + 0.32*sqrt(abs(cos(2.5*th + vSeed*3.0))); cr = 0.20; petal = vec3(0.86, 0.66, 0.03); ctr = vec3(0.62, 0.55, 0.08); }   // buttercup: five glossy petals
    else if (vSp == 9) { rim = 0.72 + 0.28*abs(fract(th*1.27324 + vSeed) - 0.5)*2.0; cr = 0.28; petal = mix(vec3(0.13, 0.26, 0.74), vec3(0.20, 0.33, 0.80), vSeed); ctr = vec3(0.24, 0.10, 0.36); }   // cornflower: ragged blue florets
    else if (vSp == 10) {   // cow parsley: an umbel of tiny white florets
      vec2 g = q*5.0 + vSeed*3.0; vec2 cl = fract(g) - 0.5;
      float dot_ = 1.0 - smoothstep(0.30, 0.36 + fwidth(g.x)*0.5, length(cl));
      rim = 1.0; cr = 0.0; petal = vec3(0.86, 0.86, 0.80); ctr = petal;
      if (dot_ < 0.5 && fwidth(g.x) < 0.6) discard;
    }
    else { rim = 0.95; cr = 0.0; petal = mix(vec3(0.62, 0.28, 0.44), vec3(0.82, 0.80, 0.72), step(0.7, vSeed)); ctr = petal;     // clover head (red / white)
      petal *= 0.8 + 0.25*vnoise(vec3(q*9.0, vSeed*10.0)); }
    if (r > rim) discard;
    alb = r < cr ? ctr : petal;
    N = normalize(F); lv = 1.0;
  } else if (vSp < 5) {
    if (vSp == 3) { float y = v*2.0 - 1.0; if (e > sqrt(max(0.0, 1.0 - y*y))) discard; }   // clover / weed leaf: rounded
    else if (vSp == 2 && v > 0.78) { float y = (v - 0.78)/0.22*2.0 - 1.0; if (e > sqrt(max(0.0, 1.0 - y*y))*0.9) discard; }   // oval seed head
    else if (e > 1.0 - pow(v, 1.3)) discard;               // narrow tapered blade, pointed tip
    alb = vAlb;
  } else if (vPart == 2) {   // basal leaves: lanceolate (clover: rounded leaflets)
    if (vSp == 12) { float y = v*2.0 - 1.0; if (e > sqrt(max(0.0, 1.0 - y*y))) discard; }
    else if (e > sin(3.14159*pow(v, 0.8))*0.95) discard;
    alb = vAlb;
  } else if (vSp == 5) {   // tall meadow grass: thin stem, then a feathery panicle
    if (v < 0.62) { if (e*vWid > 0.0058*uWidthScale) discard; alb = vAlb; }
    else { float y = (v - 0.62)/0.38; float lobe = 0.55 + 0.45*step(0.45, fract(y*9.0 + vSeed*3.0));
      if (e > sqrt(max(0.0, 1.0 - (2.0*y - 1.0)*(2.0*y - 1.0)))*lobe) discard;
      alb = mix(vec3(0.40, 0.36, 0.22), vec3(0.34, 0.27, 0.28), vSeed); }   // straw / purplish heads
  } else if (vSp == 11) {   // foxglove: bare stem, then nodding bells along the spike
    if (v < 0.49) { if (e*vWid > 0.0075*uWidthScale) discard; alb = vAlb; }
    else { float t = fract((v - 0.49)*14.0); if (e > (0.45 + 0.55*sin(3.14159*t))) discard;
      alb = mix(vec3(0.62, 0.24, 0.50), vec3(0.80, 0.62, 0.72), (1.0 - e)*0.35*(1.0 - t)); }
  } else if (vSp == 13) {   // wheat: straw, then a plump ear of paired spikelets
    if (v < 0.88) { if (e*vWid > 0.0055*uWidthScale) discard; alb = vAlb; }
    else { float y = (v - 0.88)/0.12; float spk = 0.72 + 0.28*step(0.4, fract(y*7.0 + vSeed*2.0));
      if (e > pow(max(0.0, 1.0 - (2.0*y - 1.0)*(2.0*y - 1.0)), 0.35)*spk) discard;
      alb = vAlb*vec3(1.08, 0.97, 0.80); }
  } else if (vSp == 14) {   // brush: woody twig with leaf clusters along it
    float lobe = 0.22 + 0.78*smoothstep(0.25, 0.7, fract(v*5.0 + vSeed*3.0))*step(0.15, v);
    if (e > lobe) discard;
    alb = e < 0.2 ? vec3(0.15, 0.11, 0.075) : vAlb;
  } else if (vSp == 15) {   // stubble: cut straw stems, paler cut tops
    if (e > 0.9) discard;
    alb = vAlb*(v > 0.9 ? 1.15 : 1.0);
  } else alb = vAlb;   // flower stems
  if (!past) alb *= mix(0.62, 1.12, lv);   // shaded roots, sunlit (not bleached) tips (pasture: gradient + AO per vertex)
  if (vPart == 1) alb *= 0.85;
  // cheap lighting: one hardware-PCF shadow tap, one sky-occlusion tap, wrapped diffuse, no specular
  float sh = 1.0;
  vec3 shP = past ? vW + uSunDir*0.05 + vec3(0.0, 0.02, 0.0) : vW + vec3(0.0, 0.04, 0.0);   // pasture cards cast: step off their own shadow
  if (uShadowSoft > 0.0) sh = sunShadowT(shP, vec3(0.0, 1.0, 0.0), 0.6, 4) * cloudShadow(vW);
  else if (uShadowOn == 1) {
    vec4 lp = uLightVP*vec4(vW + vec3(0.0, 0.06, 0.0), 1.0); vec3 sc = lp.xyz/lp.w*0.5 + 0.5;
    if (sc.x > 0.0 && sc.x < 1.0 && sc.y > 0.0 && sc.y < 1.0 && sc.z < 1.0) sh = texture(uShadow, vec3(sc.xy, sc.z - 0.0015));
  }
  float sv = 1.0;
  if (uOccOn == 1) {
    vec4 lp = uOccVP*vec4(vW, 1.0); vec2 uv = lp.xy*0.5 + 0.5;
    if (uv.x > 0.0 && uv.x < 1.0 && uv.y > 0.0 && uv.y < 1.0) sv = 1.0 - smoothstep(0.15, 0.6, (uOccTop - texture(uOcc, uv).r*uOccRange) - vW.y);
  }
  float ndl = max(dot(N, uSunDir), 0.0)*0.75 + 0.25;
  vec3 irr = mix(uGroundCol, uSkyCol, 0.5 + 0.5*N.y)*mix(0.06, 1.0, sv) + uFill*(1.0 - sv);
  irr = giAmbient(vW, N, sv, irr);   // L2: probe light by night / under cover
  float lk = past ? 1.0 : mix(0.72, 1.0, lv);
  vec3 c = (alb/PI*uSunCol*ndl*sh + irr*alb) * lk;
  if (uLightCount > 0) { vec3 ld, ls; localLights(vW, vec3(0.0, 1.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 1.0, 0.0), -1.0, vec3(0.04), ld, ls); c += alb/PI*ld*lk; }   // lamps: blades lit as the ground under them
  vec3 V = normalize(uCamPos - vW);
  if (past) c += alb * uSunCol * vec3(1.0, 1.08, 0.62) * (0.05 + 0.34*pow(max(dot(-V, uSunDir), 0.0), 2.0)) * (vPart == 13 ? 0.4 : 0.4 + 0.6*v) * sh * sv;   // backlit cards glow (translucency)
  else c += alb * uSunCol * 0.10 * pow(max(dot(-V, uSunDir), 0.0), 3.0) * lv * sh;   // thin-leaf transmission
  c = applyFog(c, vW);
  oColor = vec4((any(isnan(c)) || any(isinf(c))) ? vec3(0.0) : c, 1.0);
  oAmb = vec4(irr*alb*lk*gFogT*0.6, 1.0);   // blades: reduced weight (thin, sparse occluders)
  oNR = vec4(N, 1.0);
  oVel = vec4(vPC.xy/vPC.w*0.5 - vCC.xy/vCC.w*0.5, 0.0, 1.0);
}
)GLSL";
// GX3: near pasture clumps into the sun shadow map (Grass::drawShadow); coarse silhouettes of the cards
static const char* kGrassShadowFS = R"GLSL(
in vec2 vUV; in float vWid, vTap;
flat in int vSp; flat in int vPart; flat in float vSeed;
void main(){
  float e = abs(vUV.x*2.0 - 1.0)*vTap, v = vUV.y;
  if (vPart == 11 || vPart == 14) { float y = v*2.0 - 1.0; if (e > sqrt(max(0.0, 1.0 - y*y))) discard; }
  else if (vPart == 13) { if (e > 0.12 && fract(v*6.0 + vSeed*3.0 + e*0.4) > 0.75) discard; }
  else if (e > 1.0 - pow(v, 3.0)*0.95) discard;
}
)GLSL";

static const char* kFullVS = R"GLSL(
#version 450 core
out vec2 vUV;
void main(){ vec2 p = vec2((gl_VertexID<<1)&2, gl_VertexID&2); vUV = p; gl_Position = vec4(p*2.0-1.0, 1.0, 1.0); }
)GLSL";
// the sky's triangle sits on the far plane of the reverse-Z world pass (clip z 0 = depth 0), so drawn after the opaque
// world (PF2 skyLast) it passes GL_GEQUAL only where nothing was drawn and early-Z skips its shading everywhere else.
// Thumbnails (standard Z, sky first, no depth write) are unaffected.
static const char* kSkyVS = R"GLSL(
#version 450 core
out vec2 vUV;
void main(){ vec2 p = vec2((gl_VertexID<<1)&2, gl_VertexID&2); vUV = p; gl_Position = vec4(p*2.0-1.0, 0.0, 1.0); }
)GLSL";
static const char* kSkyFS = R"GLSL(
in vec2 vUV;
uniform mat4 uInvVP; uniform int uCloudOn; uniform float uCloudCover, uTime;
uniform vec3 uMoonDir, uMoonCol, uCloudLit; uniform float uStars;   // L2 time of day (Day: no moon / stars, clouds x1)
out vec4 oColor;
void main(){
  vec4 a = uInvVP*vec4(vUV*2.0-1.0, -1.0, 1.0), b = uInvVP*vec4(vUV*2.0-1.0, 1.0, 1.0);
  vec3 d = normalize(b.xyz/b.w - a.xyz/a.w);
  vec3 c = skyColor(d);
  if (uStars > 0.0 && d.y > 0.0) {   // stars: a fixed hashed field on the sphere, faint twinkle
    vec3 q = d*190.0; vec3 id = floor(q); float h = hash13(id);
    if (h > 0.9955) {
      vec3 cc = id + 0.5 + (vec3(hash13(id + 1.7), hash13(id + 4.3), hash13(id + 9.1)) - 0.5)*0.6;
      float r = length(q - cc), tw = 0.7 + 0.3*sin(uTime*(1.5 + 4.0*hash13(id + 3.1)) + h*60.0);
      c += vec3(0.85, 0.9, 1.0)*(1.0 - smoothstep(0.0, 0.45, r))*tw*uStars*0.05*((h - 0.9955)/0.0045 + 0.25)*smoothstep(0.0, 0.2, d.y);
    }
  }
  if (dot(uMoonCol, uMoonCol) > 0.0) {   // moon disc and a small halo
    float md = dot(d, uMoonDir);
    c += uMoonCol*(smoothstep(0.99985, 0.99992, md)*55.0 + pow(max(md, 0.0), 300.0)*0.8);
  }
  if (uCloudOn == 1 && d.y > 0.0) {
    // ray to the cloud plane (1500 m), fBm (5 octaves) scrolling with the wind; coverage shifts the threshold
    vec2 cpw = uCamPos.xz + d.xz/(d.y + 0.06)*(1500.0 - uCamPos.y);   // deck hit point in world xz (~600 m cells)
    float n = cloudField(cpw);
    float th = mix(0.72, 0.38, uCloudCover);
    float cl = smoothstep(th, th + 0.22, n);
    float sd = max(dot(d, uSunDir), 0.0);
    float thick = smoothstep(th + 0.05, th + 0.45, n);
    vec3 lit = vec3(0.95, 0.95, 0.96)*1.35*mix(1.0, 0.5, thick) + uSunCol*0.35*pow(sd, 6.0)*(1.0 - thick);   // grey bases, silver lining at thin edges
    if (uLook == 1) {
      // self-shadowing: optical depth toward the sun from three samples of the same field (Beer), plus a
      // forward-scattering lobe that brightens thin edges near the sun; bases are blue-grey, never black
      vec2 toSun = normalize(uSunDir.xz + 1e-4)*160.0;
      float od = 0.0;
      for (int i = 1; i <= 3; ++i) od += max(cloudFieldLo(cpw + toSun*float(i)) - th, 0.0);
      float lightT = exp(-od*3.2);
      vec3 base = mix(vec3(0.52, 0.56, 0.62), vec3(0.98, 0.98, 0.97), lightT)*1.45;
      float hg = 0.25 + 0.75*pow(sd, 5.0);
      lit = base*mix(1.0, 0.72, thick) + uSunCol*vec3(1.0, 0.95, 0.85)*0.45*hg*(1.0 - thick)*lightT;
      float far = 1.0 - smoothstep(0.02, 0.25, d.y);           // distant cloud fades into the horizon haze
      lit = mix(lit, skyColor(d), far*0.65);
    }
    c = mix(c, lit*uCloudLit, cl*0.9*smoothstep(0.0, 0.12, d.y));
  }
  if (uUnder > 0.5) c = uFogCol*(0.75 + 0.45*max(d.y, 0.0));   // under water: the fog, a little lighter toward the surface
  oColor = vec4((any(isnan(c)) || any(isinf(c))) ? skyColor(d) : c, 1.0);
}
)GLSL";
static const char* kToneFS = R"GLSL(
#version 450 core
in vec2 vUV;
uniform sampler2D uHdr, uExpo, uBloom; uniform float uFixedExpo, uBloomAmt, uEyeR, uAspect; uniform int uTonemap;
uniform float uUnderTone;   // 1: under water (swim.cpp): blue-green tint, stronger vignette
// GX5 grade (content/settings/grade.json, docs/graphics-overhaul.md#gx5-grade): saturation, cool shadows / warm
// highlights, a gentle S-curve, vignette, chromatic aberration (0 = off), and an edge-aware sharpen of the image
uniform int uGradeOn; uniform float uGradeSat, uGradeContrast, uGradeVig, uGradeSharpen, uGradeCA; uniform vec3 uGradeShadow, uGradeHigh;
out vec4 oColor;
vec3 aces(vec3 x){ return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14), 0.0, 1.0); }
vec3 toSRGB(vec3 c){ return mix(c*12.92, 1.055*pow(c, vec3(1.0/2.4))-0.055, step(0.0031308, c)); }
// Scene tonemap (the only display transform): a rational shoulder x / (1 + x^p)^(1/p), applied partly per channel
// (highlights desaturate toward white like film) and partly on luminance (keeps hue: no orange / yellow shift),
// with a gentle toe. Linear in, display-referred linear out; sRGB encoding happens once, below.
vec3 shoulder(vec3 x, float p){ return x/pow(1.0 + pow(x, vec3(p)), vec3(1.0/p)); }
vec3 filmic(vec3 x){
  x = max(x, vec3(0.0));
  float L = dot(x, vec3(0.2126, 0.7152, 0.0722));
  float Lt = shoulder(vec3(L), 1.7).x;
  vec3 byLum = x*(Lt/max(L, 1e-5));
  vec3 c = mix(shoulder(x, 1.7), byLum, 0.45);
  c = pow(c, vec3(1.10));                 // toe: a little more depth in the shadows without crushing them
  return clamp(c, 0.0, 1.0);
}
vec3 hdrAt(vec2 uv){
  if (uGradeOn == 0 || uGradeCA <= 0.0) return texture(uHdr, uv).rgb;
  vec2 d = (uv - 0.5)*uGradeCA*0.004;   // lateral colour fringe toward the corners
  return vec3(texture(uHdr, uv + d).r, texture(uHdr, uv).g, texture(uHdr, uv - d).b);
}
void main(){
  vec3 c = hdrAt(vUV);
  if (uGradeOn == 1 && uGradeSharpen > 0.0) {   // luminance unsharp mask in a compressed space, held back on strong edges (no halos)
    vec2 tx = 1.0/vec2(textureSize(uHdr, 0));
    vec3 nb = texture(uHdr, vUV + vec2(tx.x, 0.0)).rgb + texture(uHdr, vUV - vec2(tx.x, 0.0)).rgb + texture(uHdr, vUV + vec2(0.0, tx.y)).rgb + texture(uHdr, vUV - vec2(0.0, tx.y)).rgb;
    float L = dot(c, vec3(0.2126, 0.7152, 0.0722)), Lb = dot(nb*0.25, vec3(0.2126, 0.7152, 0.0722));
    float y = L/(1.0 + L), yb = Lb/(1.0 + Lb), dy = y - yb;
    float ys = clamp(y + dy*uGradeSharpen*(1.0 - smoothstep(0.03, 0.18, abs(dy))), 0.0, 0.999);
    c *= (ys/(1.0 - ys))/max(L, 1e-5);
  }
  float bAmt = uBloomAmt;   // optic eyepiece: no bloom over its dark surround (the bloom is taken from the whole lit view)
  if (uEyeR > 0.0) bAmt *= 1.0 - smoothstep(0.98*uEyeR, 1.02*uEyeR, length((vUV - 0.5)*vec2(uAspect, 1.0)));
  if (bAmt > 0.0) c = mix(c, texture(uBloom, vUV).rgb/6.0, bAmt);   // chain sums 6 levels; energy-preserving blend, not an add
  float e = uFixedExpo > 0.0 ? uFixedExpo : texelFetch(uExpo, ivec2(0), 0).r;
  c = uTonemap == 0 ? aces(c*e*0.8) : filmic(c*e*1.22);
  // subdued, slightly desaturated period grade (grade.json): saturation, cool shadows / warm highlights, vignette
  float l = dot(c, vec3(0.2126,0.7152,0.0722));
  vec2 q = vUV - 0.5;
  if (uGradeOn == 1) {   // GX5: the muted, cool-shadow / warm-light grading of the brief's targets
    c = mix(vec3(l), c, uGradeSat);
    float sh = 1.0 - smoothstep(0.0, 0.42, l), hi = smoothstep(0.3, 0.95, l);
    c *= mix(vec3(1.0), uGradeShadow, sh)*mix(vec3(1.0), uGradeHigh, hi);
    c = clamp(c, 0.0, 1.0); c = mix(c, c*c*(3.0 - 2.0*c), uGradeContrast);
    c *= 1.0 - dot(q,q)*uGradeVig;
  } else {
    c = mix(vec3(l), c, 0.93);
    c *= 1.0 - dot(q,q)*0.16;
  }
  if (uUnderTone > 0.0) { c = mix(c, c*vec3(0.55, 0.92, 0.98) + vec3(0.0, 0.025, 0.035), uUnderTone); c *= 1.0 - dot(q,q)*1.4*uUnderTone; }
  oColor = vec4(toSRGB(c), 1.0);
}
)GLSL";
static const char* kExpoFS = R"GLSL(
#version 450 core
uniform sampler2D uHdr, uPrev; uniform float uLod, uDt, uReset;
uniform float uExpoKey, uExpoMin, uExpoMax;   // L2 time of day: Day 0.24, 0.9 .. 1.45; night opens the range (lighting.cpp)
out float oE;
void main(){
  vec3 avg = textureLod(uHdr, vec2(0.5), uLod).rgb;
  float prev = texelFetch(uPrev, ivec2(0), 0).r;
  if (uReset > 0.0) { oE = uReset; return; }
  float lr = dot(avg, vec3(0.2126,0.7152,0.0722));
  if (isnan(lr) || isinf(lr)) { oE = prev; return; }   // one non-finite pixel poisons the whole mip average
  float l = max(lr, 1e-4);
  float target = clamp(uExpoKey/l, uExpoMin, uExpoMax);
  float k = 1.0 - exp(-uDt*(target > prev ? 0.6 : 1.2));
  oE = mix(prev, target, k);
}
)GLSL";
static const char* kFxVS = R"GLSL(
#version 450 core
layout(location=0) in vec3 aPos; layout(location=1) in vec4 aCol; layout(location=2) in vec2 aUV;
uniform mat4 uVP; out vec4 vC; out vec2 vUV;
void main(){ vC = aCol; vUV = aUV; gl_Position = uVP*vec4(aPos,1.0); }
)GLSL";
static const char* kFxFS = R"GLSL(
#version 450 core
in vec4 vC; in vec2 vUV; uniform int uAdd; out vec4 o;
void main(){
  float d = length(vUV*2.0-1.0);
  float a = smoothstep(1.0, 0.15, d);
  if (uAdd == 1) o = vec4(vC.rgb*a, 1.0);
  else o = vec4(vC.rgb, vC.a*a*a);
}
)GLSL";
static const char* kLineVS = R"GLSL(
#version 450 core
layout(location=0) in vec3 aPos; layout(location=1) in vec4 aCol;
uniform mat4 uVP; out vec4 vC;
void main(){ vC = aCol; gl_Position = uVP*vec4(aPos,1.0); }
)GLSL";
static const char* kLineFS = R"GLSL(
#version 450 core
in vec4 vC; out vec4 o; void main(){ o = vC; }
)GLSL";

// ============================================================== overlay helpers
void Overlay::box(const mat4& m, vec3 a, vec3 b, vec4 c, bool top) {
  vec3 p[8];
  for (int k = 0; k < 8; ++k) p[k] = vec3(m * vec4(k & 1 ? b.x : a.x, k & 2 ? b.y : a.y, k & 4 ? b.z : a.z, 1));
  int e[12][2] = {{0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7}};
  for (auto& x : e) line(p[x[0]], p[x[1]], c, top);
}
void Overlay::capsule(vec3 f, float h, float r, vec4 c) {
  const int S = 16;
  for (int ring = 0; ring < 3; ++ring) {
    float y = ring == 0 ? 0.05f : ring == 1 ? h * 0.5f : h;
    for (int k = 0; k < S; ++k) {
      float a0 = k * 6.2831853f / S, a1 = (k + 1) * 6.2831853f / S;
      line(f + vec3(std::cos(a0) * r, y, std::sin(a0) * r), f + vec3(std::cos(a1) * r, y, std::sin(a1) * r), c);
    }
  }
  for (int k = 0; k < 4; ++k) {
    float a = k * 1.5707963f;
    line(f + vec3(std::cos(a) * r, 0.05f, std::sin(a) * r), f + vec3(std::cos(a) * r, h, std::sin(a) * r), c);
  }
}

// ============================================================== renderer
GLuint Renderer::compile(const char* vs, const char* fs, const char* name) {
  locCache.clear();   // a new program may reuse a deleted one's id
  auto sh = [&](GLenum type, const char* const* srcs, int n) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, n, srcs, nullptr); glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[4096]; glGetShaderInfoLog(s, sizeof log, nullptr, log); logf("shader %s (%s) error:\n%s", name, type == GL_VERTEX_SHADER ? "vs" : "fs", log); ++shaderErrors; }
    return s;
  };
  bool vsHasVersion = std::strncmp(vs, "\n#version", 9) == 0 || std::strncmp(vs, "#version", 8) == 0;
  bool fsHasVersion = std::strncmp(fs, "\n#version", 9) == 0 || std::strncmp(fs, "#version", 8) == 0;
  // kCommon has no #version: the stage define lets it hold fragment-only helpers (gl_FragCoord noise)
  const char* vsrc[3] = {"#version 450 core\n#define NMK_VS 1\n", kCommon, vs}; const char* fsrc[3] = {"#version 450 core\n#define NMK_FS 1\n", kCommon, fs};
  GLuint v = vsHasVersion ? sh(GL_VERTEX_SHADER, &vs, 1) : sh(GL_VERTEX_SHADER, vsrc, 3);
  GLuint f = fsHasVersion ? sh(GL_FRAGMENT_SHADER, &fs, 1) : sh(GL_FRAGMENT_SHADER, fsrc, 3);
  GLuint p = glCreateProgram(); glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
  GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) { char log[4096]; glGetProgramInfoLog(p, sizeof log, nullptr, log); logf("link %s error:\n%s", name, log); }
  glDeleteShader(v); glDeleteShader(f);
  return p;
}

static GLint U(GLuint p, const char* n) { return glGetUniformLocation(p, n); }

// R1 terrain material cache. Its content is a pure function of world xz (fixed seeds), so craters, paint, trenches,
// wear and editor sculpting never invalidate it: those inputs (vDirt, vEw, uSurf, ground paint, height, normal) stay
// live in kTerrainFS. Only a new terrain extent (map load / resize) re-bakes, once, in one dispatch.
void Renderer::terrainCacheBind(const Terrain& t, GLuint prog) {
  bool on = perf.terrainCache && !t.h.empty();
  if (on && (!tcTex || tcOrg != t.org || tcExt != vec2(t.size, t.sizeZ))) {
    if (!tcProg) {   // compute program: kCommon (vnoise / fbm) + the bake
      GLuint s = glCreateShader(GL_COMPUTE_SHADER);
      const char* parts[3] = {"#version 450 core\n#define NMK_CS 1\n", kCommon, kTerrainCacheCS};
      glShaderSource(s, 3, parts, nullptr); glCompileShader(s);
      GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
      if (!ok) { char log[4096]; glGetShaderInfoLog(s, sizeof log, nullptr, log); logf("shader terrain cache (cs) error:\n%s", log); ++shaderErrors; }
      tcProg = glCreateProgram(); glAttachShader(tcProg, s); glLinkProgram(tcProg); glDeleteShader(s);
    }
    if (tcTex) glDeleteTextures(1, &tcTex);
    tcTexel = std::max(0.5f, std::max(t.size, t.sizeZ) / 4096.0f);   // 2 texels / m; past 2 km a side, coarser (4096 cap)
    tcW = (int)std::ceil(t.size / tcTexel); tcH = (int)std::ceil(t.sizeZ / tcTexel);
    int lv = 1 + (int)std::floor(std::log2((float)std::max(tcW, tcH)));
    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &tcTex);
    glTextureStorage3D(tcTex, lv, GL_RGBA8, tcW, tcH, 2);
    glTextureParameteri(tcTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); glTextureParameteri(tcTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(tcTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(tcTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameterf(tcTex, 0x84FE /* GL_TEXTURE_MAX_ANISOTROPY */, 8.0f);
    auto t0 = std::chrono::steady_clock::now();
    glUseProgram(tcProg);
    glUniform2f(U(tcProg, "uOrg"), t.org.x, t.org.y); glUniform1f(U(tcProg, "uTexel"), tcTexel);
    glBindImageTexture(0, tcTex, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA8);
    glDispatchCompute((tcW + 7) / 8, (tcH + 7) / 8, 1);
    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
    glGenerateTextureMipmap(tcTex);
    glFinish();   // one stall per bake (map load / resize) so the cost is measured; never per frame
    tcBakeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    tcOrg = t.org; tcExt = vec2(t.size, t.sizeZ); tcMB = tcW * (double)tcH * 8.0 * 4.0 / 3.0 / 1e6;
    logf("terrain cache: %d x %d x 2 RGBA8 + mips, %.1f MB, %.3f m texels, baked in %.2f ms", tcW, tcH, tcMB, tcTexel, tcBakeMs);
    glUseProgram(prog);
  }
  glUniform1i(U(prog, "uTCOn"), on ? 1 : 0);
  if (!on) return;
  glUniform1i(U(prog, "uTC"), 24); glBindTextureUnit(24, tcTex);
  glUniform4f(U(prog, "uTCMap"), tcOrg.x, tcOrg.y, 1.0f / (tcW * tcTexel), 1.0f / (tcH * tcTexel));
}

bool Renderer::init(int w, int h) {
  progMain = compile(kMainVS, (std::string(tex::glslCommon()) + tex::glslMain() + kMainFS).c_str(), "main");   // L4 texture hooks first
  progGlass = compile(kMainVS, kGlassFS, "glass");
  progShadow = compile(kShadowVS, kShadowFS, "shadow");
  progDepth = compile(kDepthVS, kShadowFS, "depth prepass");
  { extern const char* groundPaintGlsl(); progTerrain = compile(kTerrainVS, (std::string(tex::glslCommon()) + groundPaintGlsl() + kTerrainFS).c_str(), "terrain"); }   // L4 defines first, then lasso ground paint
  tex::init(contentDir() / "textures"); tex::attach(progMain); tex::attach(progTerrain);   // L4: ambientCG sets, units 20-22
  progSky = compile(kSkyVS, kSkyFS, "sky");
  progTone = compile(kFullVS, kToneFS, "tone");
  {   // GX5 grade parameters (content/settings/grade.json; defaults when missing)
    json j; try { j = json::parse(readTextFile(contentDir() / "settings" / "grade.json")); } catch (...) { logf("grade: content/settings/grade.json missing or invalid (defaults)"); }
    if (j.is_object()) {
      grade.on = j.value("enabled", grade.on); grade.saturation = j.value("saturation", grade.saturation); grade.contrast = j.value("contrast", grade.contrast);
      grade.vignette = j.value("vignette", grade.vignette); grade.sharpen = j.value("sharpen", grade.sharpen); grade.chromatic = j.value("chromaticAberration", grade.chromatic);
      grade.shadowTint = jvec3(j.value("shadowTint", json()), grade.shadowTint); grade.highlightTint = jvec3(j.value("highlightTint", json()), grade.highlightTint);
    }
  }
  progExpo = compile(kFullVS, kExpoFS, "expo");
  progLine = compile(kLineVS, kLineFS, "line");
  progFx = compile(kFxVS, kFxFS, "fx");
  progGrass = compile(kGrassVS, kGrassFS, "grass");
  progGrassShadow = compile(kGrassVS, kGrassShadowFS, "grass_shadow");
  progModel = compile(kModelVS, kModelFS, "model");
  progDof = compile(kFullVS, kDofFS, "dof");
  progEyepiece = compile(kFullVS, kEyepieceFS, "eyepiece");
  progShaft = compile(kFullVS, kShaftFS, "shafts");
  progSsao = compile(kFullVS, kSsaoFS, "ssao");
  progBloomDown = compile(kFullVS, kBloomDownFS, "bloom_down");
  progBloomUp = compile(kFullVS, kBloomUpFS, "bloom_up");
  progTaa = compile(kFullVS, kTaaFS, "taa");
  progCloudSh = compile(kFullVS, kCloudShadowFS, "cloud_shadow");
  glCreateTextures(GL_TEXTURE_2D, 1, &cloudShTex); glTextureStorage2D(cloudShTex, 1, GL_R16F, 512, 512);
  glTextureParameteri(cloudShTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(cloudShTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameteri(cloudShTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(cloudShTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glCreateFramebuffers(1, &cloudShFbo); glNamedFramebufferTexture(cloudShFbo, GL_COLOR_ATTACHMENT0, cloudShTex, 0);
  if (glCheckNamedFramebufferStatus(cloudShFbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { logf("cloud shadow framebuffer incomplete"); ++fboErrors; }
  progComposite = compile(kFullVS, kCompositeFS, "composite");
  {
    std::string fs1 = std::string(kLeafAlpha) + kLeafFS, fs2 = std::string(kLeafAlpha) + kLeafShadowFS;
    progLeaf = compile(kLeafVS, fs1.c_str(), "leaf");
    progLeafShadow = compile(kLeafVS, fs2.c_str(), "leaf_shadow");
    progLeafI = compile(kLeafInstVS, fs1.c_str(), "leaf_i");
    progLeafIShadow = compile(kLeafInstVS, fs2.c_str(), "leaf_i_shadow");
    for (GLuint p : {progLeaf, progLeafShadow, progLeafI, progLeafIShadow}) glProgramUniform1i(p, glGetUniformLocation(p, "uLeaves"), kLeafUnit);
  }
  glCreateVertexArrays(1, &fxVao); glCreateBuffers(1, &fxVbo);
  glVertexArrayVertexBuffer(fxVao, 0, fxVbo, 0, sizeof(FxVert));
  for (int k = 0; k < 3; ++k) { glEnableVertexArrayAttrib(fxVao, k); glVertexArrayAttribBinding(fxVao, k, 0); }
  glVertexArrayAttribFormat(fxVao, 0, 3, GL_FLOAT, GL_FALSE, offsetof(FxVert, p));
  glVertexArrayAttribFormat(fxVao, 1, 4, GL_FLOAT, GL_FALSE, offsetof(FxVert, c));
  glVertexArrayAttribFormat(fxVao, 2, 2, GL_FLOAT, GL_FALSE, offsetof(FxVert, uv));
  glCreateVertexArrays(1, &emptyVao);
  glCreateVertexArrays(1, &lineVao); glCreateBuffers(1, &lineVbo);
  glVertexArrayVertexBuffer(lineVao, 0, lineVbo, 0, sizeof(OverlayVert));
  glEnableVertexArrayAttrib(lineVao, 0); glVertexArrayAttribFormat(lineVao, 0, 3, GL_FLOAT, GL_FALSE, 0); glVertexArrayAttribBinding(lineVao, 0, 0);
  glEnableVertexArrayAttrib(lineVao, 1); glVertexArrayAttribFormat(lineVao, 1, 4, GL_FLOAT, GL_FALSE, 12); glVertexArrayAttribBinding(lineVao, 1, 0);

  createShadowTargets();
  // sky occlusion (top-down depth)
  glCreateTextures(GL_TEXTURE_2D, 1, &occTex);
  glTextureStorage2D(occTex, 1, GL_DEPTH_COMPONENT32F, occSize, occSize);
  glTextureParameteri(occTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(occTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameteri(occTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(occTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glCreateFramebuffers(1, &occFbo); glNamedFramebufferTexture(occFbo, GL_DEPTH_ATTACHMENT, occTex, 0);
  glNamedFramebufferDrawBuffer(occFbo, GL_NONE); glNamedFramebufferReadBuffer(occFbo, GL_NONE);
  // exposure ping-pong
  for (int k = 0; k < 2; ++k) {
    glCreateTextures(GL_TEXTURE_2D, 1, &expoTex[k]); glTextureStorage2D(expoTex[k], 1, GL_R32F, 1, 1);
    float one = 1.0f; glTextureSubImage2D(expoTex[k], 0, 0, 0, 1, 1, GL_RED, GL_FLOAT, &one);
    glCreateFramebuffers(1, &expoFbo[k]); glNamedFramebufferTexture(expoFbo[k], GL_COLOR_ATTACHMENT0, expoTex[k], 0);
  }
  glCreateQueries(GL_TIME_ELAPSED, 3, queries);
  // thumbnails
  glCreateTextures(GL_TEXTURE_2D, 1, &thumbHdr); glTextureStorage2D(thumbHdr, 1, GL_RGBA16F, 256, 256);
  glCreateTextures(GL_TEXTURE_2D, 1, &thumbDepth); glTextureStorage2D(thumbDepth, 1, GL_DEPTH_COMPONENT32F, 256, 256);
  glCreateFramebuffers(1, &thumbHdrFbo);
  glNamedFramebufferTexture(thumbHdrFbo, GL_COLOR_ATTACHMENT0, thumbHdr, 0);
  glNamedFramebufferTexture(thumbHdrFbo, GL_DEPTH_ATTACHMENT, thumbDepth, 0);
  resize(w, h);
  { static const std::string mainFS = std::string(tex::glslCommon()) + tex::glslMain() + kMainFS; lightingInit(*this, kCommon, mainFS.c_str()); }   // the capture shader is progMain's FS, L4 texture hooks included   // L2: clusters, light shadows, probe volumes
  return true;
}

// Shadow maps (near cascade and far cascade): sizes come from the settings presets (setShadowSizes).
void Renderer::createShadowTargets() {
  farFrame = occFrame = -1000;   // PF2 caches: the maps are new
  if (shadowFbo) { glDeleteFramebuffers(1, &shadowFbo); glDeleteFramebuffers(1, &shadowFarFbo); GLuint t[2] = {shadowTex, shadowFarTex}; glDeleteTextures(2, t); }
  glCreateTextures(GL_TEXTURE_2D, 1, &shadowTex);
  glTextureStorage2D(shadowTex, 1, GL_DEPTH_COMPONENT32F, shadowSize, shadowSize);
  glTextureParameteri(shadowTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(shadowTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameteri(shadowTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(shadowTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTextureParameteri(shadowTex, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE); glTextureParameteri(shadowTex, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
  glCreateFramebuffers(1, &shadowFbo); glNamedFramebufferTexture(shadowFbo, GL_DEPTH_ATTACHMENT, shadowTex, 0);
  glNamedFramebufferDrawBuffer(shadowFbo, GL_NONE); glNamedFramebufferReadBuffer(shadowFbo, GL_NONE);
  // far cascade (same comparison sampler setup)
  glCreateTextures(GL_TEXTURE_2D, 1, &shadowFarTex);
  glTextureStorage2D(shadowFarTex, 1, GL_DEPTH_COMPONENT32F, shadowFarSize, shadowFarSize);
  glTextureParameteri(shadowFarTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(shadowFarTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameteri(shadowFarTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(shadowFarTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTextureParameteri(shadowFarTex, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE); glTextureParameteri(shadowFarTex, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
  glCreateFramebuffers(1, &shadowFarFbo); glNamedFramebufferTexture(shadowFarFbo, GL_DEPTH_ATTACHMENT, shadowFarTex, 0);
  glNamedFramebufferDrawBuffer(shadowFarFbo, GL_NONE); glNamedFramebufferReadBuffer(shadowFarFbo, GL_NONE);
  if (glCheckNamedFramebufferStatus(shadowFbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE || glCheckNamedFramebufferStatus(shadowFarFbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { logf("shadow framebuffer incomplete"); ++fboErrors; }
}

void Renderer::setShadowSizes(int nearSize, int farSize) {
  nearSize = std::clamp(nearSize, 512, 8192); farSize = std::clamp(farSize, 512, 8192);
  if (nearSize == shadowSize && farSize == shadowFarSize) return;
  shadowSize = nearSize; shadowFarSize = farSize;
  if (shadowFbo) createShadowTargets();
}
void Renderer::setResolutionScale(float sc) {
  sc = std::clamp(sc, 0.5f, 1.0f);
  if (sc == resScale) return;
  resScale = sc;
  if (displayW > 0) { int w = displayW, h = displayH; displayW = 0; resize(w, h); }
}
void Renderer::setSsaoFull(bool full) {
  int d = full ? 1 : 2;
  if (d == aoDiv) return;
  aoDiv = d;
  if (hdrFbo) createTargets();
}

void Renderer::createTargets() {
  if (hdrFbo) { glDeleteFramebuffers(1, &hdrFbo); glDeleteFramebuffers(1, &ldrFbo); glDeleteFramebuffers(1, &dofFbo); GLuint t[5] = {hdrColor, depthTex, ldrColor, vmDistTex, dofTex}; glDeleteTextures(5, t); }
  int levels = 1 + (int)std::floor(std::log2((float)std::max(width, height)));
  glCreateTextures(GL_TEXTURE_2D, 1, &hdrColor); glTextureStorage2D(hdrColor, levels, GL_RGBA16F, width, height);
  glTextureParameteri(hdrColor, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTextureParameteri(hdrColor, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(hdrColor, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glCreateTextures(GL_TEXTURE_2D, 1, &depthTex); glTextureStorage2D(depthTex, 1, GL_DEPTH_COMPONENT32F, width, height);
  glCreateTextures(GL_TEXTURE_2D, 1, &ldrColor); glTextureStorage2D(ldrColor, 1, GL_RGBA8, width, height);
  glCreateFramebuffers(1, &hdrFbo);
  glNamedFramebufferTexture(hdrFbo, GL_COLOR_ATTACHMENT0, hdrColor, 0);
  glNamedFramebufferTexture(hdrFbo, GL_DEPTH_ATTACHMENT, depthTex, 0);
  // first-person eye distance (depth of field), written only during the weapon pass
  glCreateTextures(GL_TEXTURE_2D, 1, &vmDistTex); glTextureStorage2D(vmDistTex, 1, GL_R16F, width, height);
  glTextureParameteri(vmDistTex, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTextureParameteri(vmDistTex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTextureParameteri(vmDistTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(vmDistTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glNamedFramebufferTexture(hdrFbo, GL_COLOR_ATTACHMENT1, vmDistTex, 0);
  glNamedFramebufferDrawBuffer(hdrFbo, GL_COLOR_ATTACHMENT0);
  glCreateTextures(GL_TEXTURE_2D, 1, &dofTex); glTextureStorage2D(dofTex, 1, GL_RGBA16F, width, height);
  glTextureParameteri(dofTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);   // one level: sampled by the eyepiece pass
  glCreateFramebuffers(1, &dofFbo); glNamedFramebufferTexture(dofFbo, GL_COLOR_ATTACHMENT0, dofTex, 0);
  glCreateFramebuffers(1, &ldrFbo);
  glNamedFramebufferTexture(ldrFbo, GL_COLOR_ATTACHMENT0, ldrColor, 0);
  glNamedFramebufferTexture(ldrFbo, GL_DEPTH_ATTACHMENT, depthTex, 0);
  // world composite: a copy of the lit world to read while writing hdrColor (colour-only target, so the world
  // depth can be sampled), and the half-resolution light-shaft buffer
  if (sceneCopy) { glDeleteTextures(1, &sceneCopy); glDeleteTextures(1, &shaftTex); glDeleteFramebuffers(1, &compFbo); glDeleteFramebuffers(1, &shaftFbo); }
  glCreateTextures(GL_TEXTURE_2D, 1, &sceneCopy); glTextureStorage2D(sceneCopy, 1, GL_RGBA16F, width, height);
  glTextureParameteri(sceneCopy, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(sceneCopy, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(sceneCopy, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glCreateFramebuffers(1, &compFbo); glNamedFramebufferTexture(compFbo, GL_COLOR_ATTACHMENT0, hdrColor, 0);
  glCreateTextures(GL_TEXTURE_2D, 1, &shaftTex); glTextureStorage2D(shaftTex, 1, GL_RGBA16F, std::max(1, width / 2), std::max(1, height / 2));
  glTextureParameteri(shaftTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(shaftTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameteri(shaftTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(shaftTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glCreateFramebuffers(1, &shaftFbo); glNamedFramebufferTexture(shaftFbo, GL_COLOR_ATTACHMENT0, shaftTex, 0);
  // indirect light (attachment 2 of the HDR target, world pass only) and half-resolution AO (ao, view depth)
  if (ambTex) { glDeleteTextures(1, &ambTex); glDeleteTextures(1, &aoTex); glDeleteFramebuffers(1, &aoFbo); }
  glCreateTextures(GL_TEXTURE_2D, 1, &ambTex); glTextureStorage2D(ambTex, 1, GL_R11F_G11F_B10F, width, height);   // PF2: rgb only, never negative (half the bytes of RGBA16F)
  glTextureParameteri(ambTex, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTextureParameteri(ambTex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glNamedFramebufferTexture(hdrFbo, GL_COLOR_ATTACHMENT2, ambTex, 0);
  glCreateTextures(GL_TEXTURE_2D, 1, &aoTex); glTextureStorage2D(aoTex, 1, GL_RG16F, std::max(1, width / aoDiv), std::max(1, height / aoDiv));
  glTextureParameteri(aoTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(aoTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameteri(aoTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(aoTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glCreateFramebuffers(1, &aoFbo); glNamedFramebufferTexture(aoFbo, GL_COLOR_ATTACHMENT0, aoTex, 0);
  // world normal + roughness (attachment 3, world pass only) and the TAA history ping-pong
  if (nrTex) { glDeleteTextures(1, &nrTex); glDeleteTextures(2, histTex); glDeleteFramebuffers(2, taaFbo); }
  glCreateTextures(GL_TEXTURE_2D, 1, &nrTex); glTextureStorage2D(nrTex, 1, GL_RGBA16F, width, height);
  glTextureParameteri(nrTex, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTextureParameteri(nrTex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glNamedFramebufferTexture(hdrFbo, GL_COLOR_ATTACHMENT3, nrTex, 0);
  if (velTex) glDeleteTextures(1, &velTex);
  glCreateTextures(GL_TEXTURE_2D, 1, &velTex); glTextureStorage2D(velTex, 1, GL_RGBA16F, width, height);
  glTextureParameteri(velTex, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTextureParameteri(velTex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glNamedFramebufferTexture(hdrFbo, GL_COLOR_ATTACHMENT4, velTex, 0);
  for (int k = 0; k < 2; ++k) {
    glCreateTextures(GL_TEXTURE_2D, 1, &histTex[k]); glTextureStorage2D(histTex[k], 1, GL_RGBA16F, width, height);
    glTextureParameteri(histTex[k], GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(histTex[k], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(histTex[k], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(histTex[k], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glCreateFramebuffers(1, &taaFbo[k]);
    glNamedFramebufferTexture(taaFbo[k], GL_COLOR_ATTACHMENT0, hdrColor, 0);
    glNamedFramebufferTexture(taaFbo[k], GL_COLOR_ATTACHMENT1, histTex[k], 0);
    GLenum b2[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1}; glNamedFramebufferDrawBuffers(taaFbo[k], 2, b2);
    if (glCheckNamedFramebufferStatus(taaFbo[k], GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { logf("taa framebuffer incomplete"); ++fboErrors; }
  }
  taaValid = false;   // resize: no valid history
  // bloom chain from half resolution down
  for (int i = 0; i < kBloomLevels; ++i) {
    if (bloomTex[i]) { glDeleteTextures(1, &bloomTex[i]); glDeleteFramebuffers(1, &bloomFbo[i]); }
    bloomW[i] = std::max(1, width >> (i + 1)); bloomH[i] = std::max(1, height >> (i + 1));
    glCreateTextures(GL_TEXTURE_2D, 1, &bloomTex[i]); glTextureStorage2D(bloomTex[i], 1, GL_R11F_G11F_B10F, bloomW[i], bloomH[i]);
    glTextureParameteri(bloomTex[i], GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(bloomTex[i], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(bloomTex[i], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(bloomTex[i], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glCreateFramebuffers(1, &bloomFbo[i]); glNamedFramebufferTexture(bloomFbo[i], GL_COLOR_ATTACHMENT0, bloomTex[i], 0);
    if (glCheckNamedFramebufferStatus(bloomFbo[i], GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { logf("bloom framebuffer %d incomplete", i); ++fboErrors; }
  }
  for (GLuint f : {hdrFbo, ldrFbo, compFbo, shaftFbo, dofFbo, aoFbo})
    if (glCheckNamedFramebufferStatus(f, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { logf("framebuffer %u incomplete after resize to %dx%d", f, width, height); ++fboErrors; }
  createMsaa();
}

// B1 MSAA (settings: High): the opaque world (sky, terrain, solids, models, foliage, grass) renders into a
// multisampled copy of the HDR target's world attachments (0 colour, 2 indirect light, 3 normal + roughness,
// 4 velocity, depth), resolved into the HDR target before water, decals and TAA (resolveMsaa). Renderbuffers: only
// blits read them. 0 / 1 samples: no MSAA target, the world renders straight into hdrFbo as before.
void Renderer::createMsaa() {
  if (msFbo) { glDeleteFramebuffers(1, &msFbo); glDeleteRenderbuffers(5, msRb); msFbo = 0; for (auto& r : msRb) r = 0; }
  if (msaaSamples < 2) return;
  GLint maxS = 0; glGetIntegerv(GL_MAX_SAMPLES, &maxS);
  int s = std::min(msaaSamples, (int)maxS);
  if (s < 2) return;
  glCreateFramebuffers(1, &msFbo); glCreateRenderbuffers(5, msRb);
  const GLenum fmt[5] = {GL_RGBA16F, GL_R11F_G11F_B10F, GL_RGBA16F, GL_RGBA16F, GL_DEPTH_COMPONENT32F};   // indirect light as ambTex (the resolve blit needs equal formats)
  const GLenum att[5] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3, GL_COLOR_ATTACHMENT4, GL_DEPTH_ATTACHMENT};
  for (int k = 0; k < 5; ++k) {
    glNamedRenderbufferStorageMultisample(msRb[k], s, fmt[k], width, height);
    glNamedFramebufferRenderbuffer(msFbo, att[k], GL_RENDERBUFFER, msRb[k]);
  }
  glNamedFramebufferDrawBuffer(msFbo, GL_COLOR_ATTACHMENT0);
  if (glCheckNamedFramebufferStatus(msFbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    logf("MSAA x%d framebuffer incomplete: MSAA off", s); ++fboErrors;
    glDeleteFramebuffers(1, &msFbo); glDeleteRenderbuffers(5, msRb); msFbo = 0; for (auto& r : msRb) r = 0;
  }
}
void Renderer::setMsaa(int samples) {
  samples = samples >= 2 ? 4 : 0;
  if (samples == msaaSamples) return;
  msaaSamples = samples;
  if (hdrFbo) createMsaa();
  taaValid = false;
}
// colour attachments average their samples; depth takes one sample per pixel (the resolved depth stays a real surface)
void Renderer::resolveMsaa() {
  for (GLenum a : {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3, GL_COLOR_ATTACHMENT4}) {
    glNamedFramebufferReadBuffer(msFbo, a); glNamedFramebufferDrawBuffer(hdrFbo, a);
    glBlitNamedFramebuffer(msFbo, hdrFbo, 0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
  }
  glBlitNamedFramebuffer(msFbo, hdrFbo, 0, 0, width, height, 0, 0, width, height, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
  glNamedFramebufferReadBuffer(msFbo, GL_COLOR_ATTACHMENT0);
  const GLenum all[5] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3, GL_COLOR_ATTACHMENT4, GL_DEPTH_ATTACHMENT};
  glInvalidateNamedFramebufferData(msFbo, 5, all);   // resolved: the multisampled copy is dead until next frame's clear
}

void Renderer::resize(int w, int h) {
  // w, h: the window framebuffer. The world renders at that size x the resolution scale (settings) and the final
  // blit upscales it; at scale 1 (the default) internal and display sizes are the same.
  w = std::max(w, 16); h = std::max(h, 16);
  if (w == displayW && h == displayH) return;
  displayW = w; displayH = h;
  int iw = std::max(16, (int)std::lround(w * resScale)), ih = std::max(16, (int)std::lround(h * resScale));
  if (iw == width && ih == height) return;
  width = iw; height = ih; createTargets();
}

void Renderer::resetExposure(float e) { expoReset = e; }
std::string Renderer::hdrProbe() const {
  int levels = (int)std::floor(std::log2((float)std::max(width, height)));
  float top[4] = {0}; glGetTextureImage(hdrColor, levels, GL_RGBA, GL_FLOAT, sizeof top, top);
  std::vector<float> px((size_t)width * height * 4);
  glGetTextureImage(hdrColor, 0, GL_RGBA, GL_FLOAT, (GLsizei)(px.size() * 4), px.data());
  size_t bad = 0, firstX = 0, firstY = 0;
  for (size_t k = 0; k < px.size(); k += 4)
    if (!std::isfinite(px[k]) || !std::isfinite(px[k + 1]) || !std::isfinite(px[k + 2])) { if (!bad) { firstX = (k / 4) % width; firstY = (k / 4) / width; } ++bad; }
  char b[200]; std::snprintf(b, sizeof b, "top-mip (%.3f %.3f %.3f) non-finite pixels %zu (first at %zu,%zu)", top[0], top[1], top[2], bad, firstX, firstY);
  return b;
}

void Renderer::setLighting(GLuint p, const RenderView& v) {
  glUseProgram(p);
  glUniform3fv(U(p, "uSunDir"), 1, glm::value_ptr(sunDir));
  // sun : sky balance. Legacy ~1.75:1 on lit diffuse (flat, milky shadows); now ~3:1 with a cooler sky fill so
  // shadowed sides read blue-grey and lit faces warm, while shadows stay open (no crushed blacks).
  vec3 sun(2.65f, 2.52f, 2.32f), sky(0.42f, 0.49f, 0.60f), ground(0.20f, 0.19f, 0.155f), fog(0.64f, 0.68f, 0.72f), fill(0.10f, 0.095f, 0.08f);
  if (!q.legacy) { sun = vec3(3.30f, 3.05f, 2.62f); sky = vec3(0.30f, 0.36f, 0.47f); ground = vec3(0.17f, 0.155f, 0.12f); fill = vec3(0.075f, 0.072f, 0.065f); }
  vec3 zen(0.28f, 0.40f, 0.62f), hor(0.80f, 0.84f, 0.87f), skySun = sunDir, skySunCol = sun;
  lightingTodColors(*this, sun, sky, ground, fog, fill, zen, hor, skySun, skySunCol);   // L2 time of day (no change by Day / in thumbnails)
  glUniform3fv(U(p, "uSkyZen"), 1, glm::value_ptr(zen)); glUniform3fv(U(p, "uSkyHor"), 1, glm::value_ptr(hor));
  { vec3 sk = hor / vec3(0.80f, 0.84f, 0.87f); glUniform3fv(U(p, "uSkyK"), 1, glm::value_ptr(sk)); }
  glUniform3fv(U(p, "uSkySun"), 1, glm::value_ptr(skySun)); glUniform3fv(U(p, "uSkySunCol"), 1, glm::value_ptr(skySunCol));
  if (v.underwater && !thumbMode) { sun *= vec3(0.22f, 0.38f, 0.40f); sky *= vec3(0.45f, 0.75f, 0.80f); ground *= vec3(0.45f, 0.75f, 0.80f); }   // swim.cpp: light filtered by the water above
  glUniform3fv(U(p, "uSunCol"), 1, glm::value_ptr(sun));
  glUniform3fv(U(p, "uSkyCol"), 1, glm::value_ptr(sky));
  glUniform3fv(U(p, "uGroundCol"), 1, glm::value_ptr(ground));
  glUniform3fv(U(p, "uFogCol"), 1, glm::value_ptr(fog));
  glUniform3fv(U(p, "uFill"), 1, glm::value_ptr(fill));
  glUniform3fv(U(p, "uCamPos"), 1, glm::value_ptr(v.camPos));
  glUniform1f(U(p, "uFogDens"), thumbMode ? 0.0f : (q.legacy ? 0.0016f : q.hazeDensity));
  if (v.underwater && !thumbMode) { fog = v.underFogCol; glUniform3fv(U(p, "uFogCol"), 1, glm::value_ptr(fog)); glUniform1f(U(p, "uFogDens"), v.underFogDens); }   // swim.cpp
  glUniform1f(U(p, "uUnder"), v.underwater && !thumbMode ? 1.0f : 0.0f);
  if (p == progMain) fogNow = vec4(fog, v.underwater && !thumbMode ? v.underFogDens : (thumbMode ? 0.0f : (q.legacy ? 0.0016f : q.hazeDensity)));
  glUniformMatrix4fv(U(p, "uLightVP"), 1, GL_FALSE, glm::value_ptr(lightVP));
  glUniformMatrix4fv(U(p, "uOccVP"), 1, GL_FALSE, glm::value_ptr(occVP));
  glUniform1f(U(p, "uOccTop"), occTop); glUniform1f(U(p, "uOccRange"), occRange);
  glUniform1i(U(p, "uShadowOn"), thumbMode ? 0 : 1);
  glUniform1i(U(p, "uOccOn"), thumbMode ? 0 : 1);
  glUniform2f(U(p, "uShadowTexel"), 1.0f / shadowSize, 1.0f / shadowSize);
  glUniform1i(U(p, "uShadow"), 0); glUniform1i(U(p, "uOcc"), 1);
  glBindTextureUnit(0, shadowTex); glBindTextureUnit(1, occTex);
  bool far = !q.legacy && q.farCascade && !thumbMode;
  glUniform1i(U(p, "uShadowFar"), 6); glBindTextureUnit(6, shadowFarTex);
  glUniform1i(U(p, "uShadowFarOn"), far ? 1 : 0);
  glUniformMatrix4fv(U(p, "uLightVPFar"), 1, GL_FALSE, glm::value_ptr(lightVPFar));
  glUniform2f(U(p, "uShadowTexelFar"), 1.0f / shadowFarSize, 1.0f / shadowFarSize);
  glUniform2f(U(p, "uShadowWorldTexel"), 2 * nearR / shadowSize, 2 * farR / shadowFarSize);
  glUniform1f(U(p, "uShadowDepthRange"), 800.0f);
  glUniform1f(U(p, "uShadowSoft"), q.legacy ? 0.0f : q.shadowSoft);
  glUniform1i(U(p, "uFrame"), frame);
  glUniform1i(U(p, "uCloudShadowOn"), !q.legacy && q.cloudShadows && clouds && !thumbMode ? 1 : 0);
  glUniform1f(U(p, "uCloudCov"), cloudCover); glUniform1f(U(p, "uCloudT"), time);
  glUniform1i(U(p, "uCloudShadowMap"), 13); glBindTextureUnit(13, cloudShTex);
  glUniform3fv(U(p, "uCloudShadowBox"), 1, glm::value_ptr(cloudShBox));
  glUniform1f(U(p, "uAoIndirectOnly"), q.legacy ? 0.0f : 1.0f);
  glUniform1i(U(p, "uLook"), q.legacy ? 0 : 1);
  glUniform1f(U(p, "uHazeH"), q.hazeHeight);
  glUniform1f(U(p, "uTime"), time);
  lightingUniforms(*this, p);   // L2: local lights, light shadows, probe GI, sky extras
}

Renderer::DrawLocs Renderer::drawLocs(GLuint p) {
  for (auto& [k, l] : locCache) if (k == p) return l;
  DrawLocs l;
  l.model = U(p, "uModel"); l.tint = U(p, "uTint"); l.inst = U(p, "uInst"); l.snapAp = U(p, "uSnapAp"); l.glow = U(p, "uGlow");   // snapAp: V6 snap-chain texture origin
  l.roomA = U(p, "uRoomA"); l.roomB = U(p, "uRoomB"); l.sills = U(p, "uSills"); l.sillA = U(p, "uSillA"); l.sillB = U(p, "uSillB");
  l.roomDec = U(p, "uRoomDec"); l.extDec = U(p, "uExtDec"); l.roomX = U(p, "uRoomX");   // GX5 static decal ranges, rooms past 16
  l.vp = U(p, "uVP"); l.time = U(p, "uTime"); l.wind = U(p, "uWind"); l.leafCol = U(p, "uLeafCol");
  l.base = U(p, "uBase"); l.metal = U(p, "uMetal"); l.rough = U(p, "uRough"); l.thin = U(p, "uThin");
  l.alb = U(p, "uAlb"); l.mr = U(p, "uMR"); l.nrm = U(p, "uNrm"); l.ao = U(p, "uAO");
  l.hasAlb = U(p, "uHasAlb"); l.hasMR = U(p, "uHasMR"); l.hasNrm = U(p, "uHasNrm"); l.hasAO = U(p, "uHasAO"); l.aoInMR = U(p, "uAOinMR");
  locCache.push_back({p, l});
  return l;
}

// Frustum culling (docs/rendering.md#frustum-culling). Margins in world metres cover vertex-shader displacement beyond
// the uploaded bounds.
static constexpr float kCullEps = 0.01f;        // box-kit meshes: no displacement (rounding only)
static constexpr float kModelCullPad = 0.05f;   // imported models: thin-wire widening (<= 3 cm) and small part anims
// world-space diagonal of a mesh-space box (largest axis scale of the model matrix): the small-caster test of the coarse depth passes
static float worldDiag(vec3 lo, vec3 hi, const mat4& m) {
  float sc = std::max({glm::length(vec3(m[0])), glm::length(vec3(m[1])), glm::length(vec3(m[2]))});
  return glm::length(hi - lo) * sc;
}

// GX5 static decals and the rooms past 16 (static_decals.cpp): the whole list again when a building's block changed
// (edits, map loads), bound at 10 for every main-shader program (camera pass and probe captures)
void Renderer::uploadStaticDecals() {
  if (sdecDirty) {
    sdecDirty = false;
    if (!sdecBuf) glCreateBuffers(1, &sdecBuf);
    glNamedBufferData(sdecBuf, std::max<size_t>(1, sdec.size()) * sizeof(StaticDecal), sdec.empty() ? nullptr : sdec.data(), GL_STATIC_DRAW);
  }
  if (sdecBuf) glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 10, sdecBuf);
}
void Renderer::drawItems(GLuint p, const std::vector<DrawItem>& items, bool shadowPass, const Frustum* fr, bool sortSurf) {
  const DrawLocs L = drawLocs(p);
  GLint lm = L.model, lt = L.tint;
  GLint li = shadowPass ? -1 : L.inst, la = shadowPass ? -1 : L.snapAp;
  GLint lg = shadowPass ? -1 : L.glow;   // L2: per-item emission multiplier
  static const SurfaceParams kDefaultSurf;
  // visible set: the mesh's bounds under its model matrix against this pass's frustum
  drawIdx.clear();
  for (uint32_t k = 0; k < (uint32_t)items.size(); ++k) {
    const DrawItem& it = items[k];
    if (!it.mesh || !it.mesh->count) continue;
    if (fr && !fr->visible(it.mesh->bmin, it.mesh->bmax, it.model, kCullEps)) { stats.culled++; continue; }
    if (shadowPass && minCaster > 0 && worldDiag(it.mesh->bmin, it.mesh->bmax, it.model) < minCaster) { stats.culled++; continue; }
    if (shadowPass && castCull && it.castR > 0) {   // R2 castercull: a small prop's sun shadow only near the eye
      vec3 c = vec3(it.model * vec4((it.mesh->bmin + it.mesh->bmax) * 0.5f, 1.0f)) - castEye;
      if (glm::dot(c, c) > it.castR * it.castR) { stats.casterCulled++; continue; }
    }
    if (!shadowPass && it.mainHidden) { stats.roomCulled++; continue; }   // R2 roomcull (only ever set for the camera pass's lists)
    if (depthSurfOnly && !it.surf) continue;
    drawIdx.push_back(k);
  }
  // main pass: copies sharing finish parameters are grouped so the parameter block uploads once per building. Stable,
  // so the order inside a building (and any coplanar tie) is unchanged and never depends on the camera.
  auto surfOf = [&](uint32_t k) -> const SurfaceParams* { return items[k].surf ? items[k].surf : &kDefaultSurf; };
  if (sortSurf && li >= 0)
    std::stable_sort(drawIdx.begin(), drawIdx.end(), [&](uint32_t a, uint32_t b) { return std::less<const SurfaceParams*>()(surfOf(a), surfOf(b)); });
  if (sortSurf && li >= 0 && perf.opqSort && drawIdx.size() > 1) {
    // R1 opqsort: each building's run (one finish block) moves front to back by its nearest part, so early-Z rejects the
    // walls, rooms and props it hides. A run keeps its inner order (coplanar ties never depend on the camera).
    struct Run { float d; uint32_t a, b; };
    std::vector<Run> runs; runs.reserve(256);
    for (uint32_t a = 0; a < drawIdx.size();) {
      const SurfaceParams* s = surfOf(drawIdx[a]); uint32_t b = a; float d = 1e30f;
      for (; b < drawIdx.size() && surfOf(drawIdx[b]) == s; ++b) {
        const DrawItem& it = items[drawIdx[b]];
        d = std::min(d, glm::distance(vec3(it.model * vec4(0.5f * (it.mesh->bmin + it.mesh->bmax), 1.0f)), sortEye));
      }
      runs.push_back({d, a, b}); a = b;
    }
    std::stable_sort(runs.begin(), runs.end(), [](const Run& x, const Run& y) { return x.d < y.d; });
    std::vector<uint32_t> sorted; sorted.reserve(drawIdx.size());
    for (const Run& r : runs) sorted.insert(sorted.end(), drawIdx.begin() + r.a, drawIdx.begin() + r.b);
    drawIdx.swap(sorted);
  }
  const SurfaceParams* lastSurf = nullptr;
  GLuint boundVao = 0;
  for (uint32_t k : drawIdx) {
    const DrawItem& it = items[k];
    glUniformMatrix4fv(lm, 1, GL_FALSE, glm::value_ptr(it.model));
    if (!shadowPass && lt >= 0) glUniform4fv(lt, 1, glm::value_ptr(it.tint));
    if (la >= 0) glUniform4fv(la, 1, glm::value_ptr(it.apWorld));
    if (lg >= 0) glUniform1f(lg, it.glow);
    if (li >= 0) {   // building finish parameters (docs/interiors.md): uploaded only when the copy changes
      const SurfaceParams* sp = surfOf(k);
      if (sp != lastSurf) {
        lastSurf = sp;
        glUniform4fv(li, 1, glm::value_ptr(sp->inst));
        int nr = std::clamp((int)(sp->inst.w + 0.5f), 0, 16);
        if (nr) { glUniform4fv(L.roomA, nr, glm::value_ptr(sp->roomA[0])); glUniform4fv(L.roomB, nr, glm::value_ptr(sp->roomB[0])); }
        if (L.roomDec >= 0) { glUniform4fv(L.roomDec, 16, glm::value_ptr(sp->roomDec[0])); glUniform4fv(L.extDec, 1, glm::value_ptr(sp->extDec)); }
        if (L.roomX >= 0) glUniform2i(L.roomX, sp->roomX.x, sdecBuf ? sp->roomX.y : 0);
        int ns = std::clamp(sp->sills, 0, 12);
        glUniform1i(L.sills, ns);
        if (ns) { glUniform4fv(L.sillA, ns, glm::value_ptr(sp->sillA[0])); glUniform4fv(L.sillB, ns, glm::value_ptr(sp->sillB[0])); }
      }
    }
    if (it.mesh->vao != boundVao) { boundVao = it.mesh->vao; glBindVertexArray(boundVao); }
    glDrawElements(GL_TRIANGLES, it.mesh->count, GL_UNSIGNED_INT, nullptr);
    stats.draws++; stats.tris += it.mesh->count / 3;
  }
}

// Trees with L1 species leaves (perf.leafTex) draw their cards as instances of the shared sprig meshes with the
// instanced twin of p (progLeafI / progLeafIShadow): LOD f.cur in the main view (view), the sprig cards elsewhere
// (sun / sky-occlusion shadows: same layers, same alpha). Everything else (ivy, ferns, oval A/B) draws the card VBO.
void Renderer::drawFoliage(GLuint p, const mat4& vp, const Frustum* fr, bool view, float lodFrom) {
  float sway = 0.14f * std::max(0.0f, wind) + kCullEps;   // kLeafVS leafSway: |offset| <= 0.10 x 1.3 x |(0.8, 0.25, 0.6)| x uWind
  bool tex = perf.leafTex && leafLibReady();
  for (int pass = 0; pass < 2; ++pass) {
    auto mine = [&](const FoliageDraw& f) { bool inst = tex && f.tree && f.tree->leafVao; return f.tree && inst == (pass == 1) && (inst || f.tree->cardVao); };
    if (std::none_of(foliage.begin(), foliage.end(), mine)) continue;
    GLuint q = pass == 0 ? p : p == progLeaf ? progLeafI : progLeafIShadow;
    glUseProgram(q);
    const DrawLocs L = drawLocs(q);
    glUniformMatrix4fv(L.vp, 1, GL_FALSE, glm::value_ptr(vp));
    glUniform1f(L.time, time); glUniform1f(L.wind, wind);
    GLint lm = L.model, lc = L.leafCol;
    for (auto& f : foliage) {
      if (!mine(f)) continue;
      if (fr && !fr->visible(f.tree->cardMin, f.tree->cardMax, f.m, sway + (pass ? 0.05f : 0.0f))) { stats.culled++; continue; }   // leaf fold: a few cm off the card
      glUniformMatrix4fv(lm, 1, GL_FALSE, glm::value_ptr(f.m));
      // R2 leafshadowlod: a depth pass past lodFrom metres from the eye casts from the shadow LOD (foliage.cpp): half the
      // cards / sprig instances, grown 1.25x, same alpha
      const bool slod = !view && lodFrom >= 0 && (lodFrom == 0 || glm::length(vec3(f.m[3]) - castEye) > lodFrom);
      if (pass == 0) {
        if (lc >= 0) glUniform3fv(lc, 1, glm::value_ptr(f.tree->leafCol));
        glBindVertexArray(f.tree->cardVao);
        bool cl = slod && f.tree->shadowVerts > 0;
        int first = cl ? f.tree->shadowFirst : 0, n = cl ? f.tree->shadowVerts : f.tree->cardVerts;
        glDrawArrays(GL_TRIANGLES, first, n);
        stats.draws++; stats.tris += n / 3;
        continue;
      }
      int lod = view ? f.cur : 2;
      glBindVertexArray(f.tree->leafVao);
      for (auto& g : slod && !f.tree->leafShadowGroups.empty() ? f.tree->leafShadowGroups : f.tree->leafGroups) {
        const LeafMeshRange& r = gLeafSprigMesh[g.sprig * 3 + lod];
        if (lc >= 0) glUniform3fv(lc, 1, glm::value_ptr(g.tint));
        glDrawElementsInstancedBaseVertexBaseInstance(GL_TRIANGLES, r.count, GL_UNSIGNED_INT, (void*)(uintptr_t)(r.firstIndex * 4u), g.count, r.baseVertex, g.first);
        stats.draws++; stats.tris += (size_t)r.count / 3 * g.count;
      }
    }
  }
}

// L1 leaf LOD per tree for the main view: near (fitted folded leaves) / mid (2-triangle leaves) / far (sprig cards) by
// the eye's distance to the canopy box, scaled by the zoom (scopes keep detail further out), with hysteresis.
void Renderer::selectLeafLods(const RenderView& v) {
  const float zoom = 1.43f / std::max(0.1f, v.proj[1][1]);   // 1 at a 70 degree vertical FOV
  for (auto& f : foliage) {
    if (!f.tree || !f.tree->leafVao) continue;
    vec3 lc = vec3(glm::inverse(f.m) * vec4(v.camPos, 1.0f));
    float d = glm::length(lc - glm::clamp(lc, f.tree->cardMin, f.tree->cardMax)) * zoom / std::max(0.1f, leafLodScale);
    uint8_t cur = f.lod ? *f.lod : 2, want = cur;
    if (cur == 0) { if (d > kLeafNearOut) want = d > kLeafMidOut ? 2 : 1; }
    else if (cur == 1) { if (d < kLeafNearIn) want = 0; else if (d > kLeafMidOut) want = 2; }
    else { if (d < kLeafNearIn) want = 0; else if (d < kLeafMidIn) want = 1; }
    f.cur = want; if (f.lod) *f.lod = want;
  }
}

// Imported model parts are tested with their import bounds (model.cpp, vertex space). Parts without bounds are always
// drawn: skinned soldiers (the skinning pass rewrites their vertex buffer every frame) and older bakes.
static bool partCulled(const Frustum* fr, const ModelPart& pt, const mat4& m) {
  if (!fr) return false;
  vec3 e = pt.bmax - pt.bmin;
  if (e.x < 0 || e.y < 0 || e.z < 0 || e.x + e.y + e.z <= 0) return false;
  return !fr->visible(pt.bmin, pt.bmax, m, kModelCullPad);
}

void Renderer::drawModelsDepth(GLuint p, const std::vector<ModelDraw>& items, const Frustum* fr) {
  GLint lm = drawLocs(p).model;
  const GpuModel* bound = nullptr;
  for (auto& it : items) {
    const GpuModel* m = it.model;
    if (!m || !m->vao || it.part < 0 || it.part >= (int)m->parts.size()) continue;
    const ModelPart& pt = m->parts[it.part];
    if (partCulled(fr, pt, it.m)) { stats.culled++; continue; }
    if (minCaster > 0 && pt.bmax.x >= pt.bmin.x && worldDiag(pt.bmin, pt.bmax, it.m) < minCaster) { stats.culled++; continue; }
    if (m != bound) { bound = m; glBindVertexArray(m->vao); }
    glUniformMatrix4fv(lm, 1, GL_FALSE, glm::value_ptr(it.m));
    glDrawElements(GL_TRIANGLES, (GLsizei)pt.count, GL_UNSIGNED_INT, (void*)(uintptr_t)(pt.first * 4u));
  }
}

void Renderer::drawModels(const std::vector<ModelDraw>& items, const Frustum* fr) {
  GLuint p = progModel;
  const DrawLocs L = drawLocs(p);
  glUniform1i(L.alb, 2); glUniform1i(L.mr, 3); glUniform1i(L.nrm, 4); glUniform1i(L.ao, 5);
  GLint lm = L.model, lb = L.base, lme = L.metal, lr = L.rough, lt = L.tint, lth = L.thin;
  const GpuModel* bound = nullptr; int boundMat = -2;
  for (auto& it : items) {
    const GpuModel* m = it.model;
    if (!m || !m->vao || it.part < 0 || it.part >= (int)m->parts.size()) continue;
    const ModelPart& pt = m->parts[it.part];
    if (partCulled(fr, pt, it.m)) { stats.culled++; continue; }
    if (m != bound) { bound = m; boundMat = -2; glBindVertexArray(m->vao); }
    int mi = pt.mat >= 0 && pt.mat < (int)m->mats.size() ? pt.mat : -1;
    if (mi != boundMat) {
      boundMat = mi;
      GpuModel::MatTex none; const GpuModel::MatTex& t = mi >= 0 ? m->mats[mi] : none;
      glBindTextureUnit(2, t.alb); glBindTextureUnit(3, t.mr); glBindTextureUnit(4, t.n); glBindTextureUnit(5, t.ao);
      glUniform1i(L.hasAlb, t.alb ? 1 : 0); glUniform1i(L.hasMR, t.mr ? 1 : 0); glUniform1i(L.hasNrm, t.n ? 1 : 0);
      glUniform1i(L.hasAO, t.ao ? 1 : 0); glUniform1i(L.aoInMR, t.aoInMR ? 1 : 0);
    }
    glUniformMatrix4fv(lm, 1, GL_FALSE, glm::value_ptr(it.m));
    glUniform4fv(lb, 1, glm::value_ptr(pt.base)); glUniform1f(lme, pt.metal); glUniform1f(lr, pt.rough);
    glUniform4fv(lt, 1, glm::value_ptr(it.tint));
    glUniform1f(lth, it.thinPx * pxAngle);
    glDrawElements(GL_TRIANGLES, (GLsizei)pt.count, GL_UNSIGNED_INT, (void*)(uintptr_t)(pt.first * 4u));
    stats.draws++; stats.tris += (int)pt.count / 3;
  }
}

bool Renderer::renderModelSheet(const std::vector<ModelDraw>& items, const std::vector<DrawItem>& boxes, const mat4& view, const mat4& proj, vec3 camPos, int w, int h, std::vector<uint8_t>& rgba) {
  GLuint col, dep, fbo;
  glCreateTextures(GL_TEXTURE_2D, 1, &col); glTextureStorage2D(col, 1, GL_RGBA16F, w, h);
  glCreateTextures(GL_TEXTURE_2D, 1, &dep); glTextureStorage2D(dep, 1, GL_DEPTH_COMPONENT32F, w, h);
  glCreateFramebuffers(1, &fbo); glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0, col, 0); glNamedFramebufferTexture(fbo, GL_DEPTH_ATTACHMENT, dep, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo); glViewport(0, 0, w, h);
  glClearColor(0.62f, 0.64f, 0.66f, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
  RenderView v; v.view = view; v.proj = proj; v.camPos = camPos;
  thumbMode = true;
  mat4 vp = proj * view;
  setLighting(progMain, v); glUniformMatrix4fv(U(progMain, "uVP"), 1, GL_FALSE, glm::value_ptr(vp)); drawItems(progMain, boxes, false);
  setLighting(progModel, v); glUniformMatrix4fv(U(progModel, "uVP"), 1, GL_FALSE, glm::value_ptr(vp)); drawModels(items);
  thumbMode = false;
  std::vector<float> px((size_t)w * h * 4);
  glGetTextureImage(col, 0, GL_RGBA, GL_FLOAT, (GLsizei)(px.size() * 4), px.data());
  rgba.resize((size_t)w * h * 4);
  for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
    for (int c = 0; c < 4; ++c) {
      float f = px[((size_t)(h - 1 - y) * w + x) * 4 + c];
      if (c < 3) { f = std::clamp(f * 0.9f, 0.0f, 4.0f); f = f / (1.0f + f * 0.25f) * 1.25f; f = std::pow(std::clamp(f, 0.0f, 1.0f), 1.0f / 2.2f); } else f = 1;
      rgba[((size_t)y * w + x) * 4 + c] = (uint8_t)std::lround(f * 255);
    }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &fbo); glDeleteTextures(1, &col); glDeleteTextures(1, &dep);
  return true;
}

static mat4 stableOrtho(vec3 focus, vec3 dir, float radius, float depth, int res, mat4& viewOut) {
  vec3 up = std::fabs(dir.y) > 0.99f ? vec3(0, 0, 1) : vec3(0, 1, 0);
  mat4 view = glm::lookAt(vec3(0), -dir, up);
  // snap focus to texel grid in light space to stop shimmering
  vec3 ls = vec3(view * vec4(focus, 1));
  float texel = 2 * radius / res;
  ls.x = std::floor(ls.x / texel) * texel; ls.y = std::floor(ls.y / texel) * texel;
  mat4 proj = glm::ortho(ls.x - radius, ls.x + radius, ls.y - radius, ls.y + radius, -ls.z - depth, -ls.z + depth);
  viewOut = view;
  return proj * view;
}

// Standard GL projection (clip z -1..1) -> reverse-Z for glClipControl(GL_ZERO_TO_ONE): z' = (w - z) / 2, so the near
// plane maps to 1 and the far plane to 0. Float depth then keeps ~constant relative precision with distance (layers
// millimetres apart no longer fight at 100 m, docs/rendering.md#depth-convention). x / y / w rows are unchanged, so jitter,
// motion vectors and screen projection are unaffected. Built in double.
// PF2 / Mac: glInvalidateNamedFramebufferData before a pass that rewrites every pixel of its target (and on targets that
// are dead until the next frame clears them). A hint on desktop GL; on tile GPUs (the Metal shim) the old contents are
// neither loaded nor stored.
static const GLenum kInvC0[1] = {GL_COLOR_ATTACHMENT0};
static mat4 reverseZ(const mat4& p) {
  glm::dmat4 d(p);
  for (int c = 0; c < 4; ++c) d[c][2] = 0.5 * d[c][3] - 0.5 * d[c][2];
  return mat4(d);
}

const char* gpuPassName(int p) {
  static const char* n[GP_Count] = {"cloud_shadow", "shadow_near", "shadow_far", "sky_occlusion", "light_shadows", "probes", "clusters",
                                    "clears", "sky", "terrain", "opaque", "models", "trees", "grass", "nature", "msaa_resolve", "water", "gore",
                                    "glass", "composite", "taa", "bloom", "fx", "weapon", "post", "ui"};
  return p >= 0 && p < GP_Count ? n[p] : "end";
}
// one slot of GP_Count + 1 timestamps per frame, three slots in flight: a slot is read (only when every result is ready,
// so the CPU never waits) just before it is reused
void Renderer::gpuFrameBegin() {
  if (!gpuTimers) return;
  if (!gq[0][0]) {
    if (!glQueryCounter || !glGetQueryObjectui64v) { gpuTimers = false; return; }   // e.g. a GL subset without timestamps
    for (auto& s : gq) glGenQueries(GP_Count + 1, s);
  }
  gqSlot = (gqSlot + 1) % 3;
  bool* set = gqSet[gqSlot]; GLuint* q = gq[gqSlot];
  bool ready = set[0];
  for (int i = 0; i <= GP_Count && ready; ++i) if (set[i]) { GLint a = 0; glGetQueryObjectiv(q[i], GL_QUERY_RESULT_AVAILABLE, &a); ready = a != 0; }
  if (ready) {
    GLuint64 ts[GP_Count + 1] = {};
    for (int i = 0; i <= GP_Count; ++i) if (set[i]) glGetQueryObjectui64v(q[i], GL_QUERY_RESULT, &ts[i]);
    int ord[GP_Count + 1], n = 0;   // passes in GPU time order (the pass order may change with the PF2 switches)
    for (int i = 0; i <= GP_Count; ++i) if (set[i]) ord[n++] = i;
    std::sort(ord, ord + n, [&](int a, int b) { return ts[a] < ts[b]; });
    std::fill(gpuPassMs, gpuPassMs + GP_Count, 0.0);
    for (int k = 0; k + 1 < n; ++k) if (ord[k] < GP_Count) gpuPassMs[ord[k]] = (ts[ord[k + 1]] - ts[ord[k]]) / 1e6;
    if (!perf.syncGpuQuery && set[GP_UI]) stats.gpuMs = (ts[GP_UI] - ts[0]) / 1e6;   // render() only, as GL_TIME_ELAPSED measured it
  }
  std::fill(set, set + GP_Count + 1, false);
}
void Renderer::gpuMark(int p) {
  if (!gpuTimers || !gq[0][0] || p < 0 || p > GP_Count) return;
  glQueryCounter(gq[gqSlot][p], GL_TIMESTAMP); gqSet[gqSlot][p] = true;
}

void Renderer::render(const RenderView& v0, const Terrain& t, const std::vector<DrawItem>& opaque,
                      const std::vector<DrawItem>& glass, const std::vector<DrawItem>& ghosts, const Overlay& ov) {
  auto c0 = std::chrono::high_resolution_clock::now();
  // shadow cascades and sky occlusion: standard depth; the camera passes below switch to reverse-Z
  glClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE); glClearDepth(1.0);
  RenderView v = v0;
  v.proj = reverseZ(v0.proj);
  if (v0.vmProj[2][3] != 0.0f) v.vmProj = reverseZ(v0.vmProj);
  stats.draws = 0; stats.tris = 0; stats.culled = 0; stats.casterCulled = 0; stats.shadowDraws = 0;   // roomCulled: counted from here too
  stats.roomCulled = 0; stats.probePending = stats.probeDirty = stats.probeStaleMax = 0;
  time += v.dt;
  uploadStaticDecals();      // GX5: before the probe captures (they run the main shader too)
  lightingBegin(*this, v);   // L2: time of day (sunDir = the key light until lightingEnd)
  pxAngle = 2.0f / (std::max(1e-3f, v.proj[1][1]) * std::max(1, height));   // vertical radians per pixel
  gpuFrameBegin(); gpuMark(GP_CloudShadow);   // stats.gpuMs from the timestamps: read 2-3 frames late, never waits
  const bool elapsedQ = !gpuTimers || perf.syncGpuQuery;   // no timestamps: a GL_TIME_ELAPSED ring (never waits); asyncq A/B: the old waiting read-back
  if (elapsedQ) {
    if (perf.syncGpuQuery) {   // the pre-PF2 read-back (A/B only): waits for last frame's GPU work
      int prev = (qidx + 2) % 3;
      if (qvalid && qUsed[prev]) { GLuint64 ns = 0; glGetQueryObjectui64v(queries[prev], GL_QUERY_RESULT, &ns); stats.gpuMs = ns / 1e6; }
    } else {                   // three in flight: the oldest is read only when its result is there (the last value stays otherwise)
      int old = (qidx + 1) % 3; GLint av = 0;
      if (qUsed[old]) glGetQueryObjectiv(queries[old], GL_QUERY_RESULT_AVAILABLE, &av);
      if (av) { GLuint64 ns = 0; glGetQueryObjectui64v(queries[old], GL_QUERY_RESULT, &ns); stats.gpuMs = ns / 1e6; }
    }
    glBeginQuery(GL_TIME_ELAPSED, queries[qidx]);
  }

  // ---- cloud shadow map (once per frame, texel-snapped so it does not swim as the camera moves)
  if (!thumbMode && !q.legacy && q.cloudShadows && clouds) {
    float tex = 2 * cloudShBox.z / 512;
    cloudShBox.x = std::floor(v.camPos.x / tex) * tex; cloudShBox.y = std::floor(v.camPos.z / tex) * tex;
    glBindFramebuffer(GL_FRAMEBUFFER, cloudShFbo); glViewport(0, 0, 512, 512);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND);
    setLighting(progCloudSh, v);
    glBindTextureUnit(13, 0);   // the map is the render target here: never bound for sampling at the same time
    glBindVertexArray(emptyVao); glDrawArrays(GL_TRIANGLES, 0, 3);
    glEnable(GL_DEPTH_TEST);
  }
  gpuMark(GP_ShadowNear);
  // ---- shadow map: tighter around the camera when walking, wider while flying
  float camH = std::max(0.0f, v.camPos.y - t.height(v.camPos.x, v.camPos.z));
  float radius = v.editor ? std::clamp(60.0f + camH * 1.2f, 60.0f, 260.0f) : (q.legacy ? 55.0f : 40.0f);
  {   // GX2: inside a room the near cascade tightens to 24 m (about 1.2 cm texels at 4096) so window bars cast crisp shadows
    bool indoor = false;
    for (auto& b : rooms) { vec3 l = vec3(glm::inverse(b.m) * vec4(v.camPos, 1)); if (std::fabs(l.x) < b.half.x && std::fabs(l.y) < b.half.y && std::fabs(l.z) < b.half.z) { indoor = true; break; } }
    if (indoor && !q.legacy) radius = 24.0f;
  }
  ++frame;
  vec3 fwd = -vec3(glm::inverse(v.view)[2]);
  vec3 focus = v.camPos + vec3(fwd.x, 0, fwd.z) * radius * 0.5f;
  focus.y = t.height(focus.x, focus.z);
  mat4 lv;
  lightVP = stableOrtho(focus, sunDir, radius, 400.0f, shadowSize, lv);
  nearR = radius;
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo);
  glViewport(0, 0, shadowSize, shadowSize);
  glClear(GL_DEPTH_BUFFER_BIT);
  glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(1.5f, 2.0f);
  glDisable(GL_CULL_FACE);
  glUseProgram(progShadow);
  glUniformMatrix4fv(U(progShadow, "uVP"), 1, GL_FALSE, glm::value_ptr(lightVP));
  // every depth / colour pass culls against its own view-projection (docs/rendering.md#frustum-culling): a cascade keeps
  // the casters inside its ortho box even when they are behind the camera
  Frustum frNear = Frustum::fromVP(lightVP);
  castCull = perf.casterCull && !thumbMode; castEye = v.camPos;   // R2: on for the two sun cascades only
  const float leafLod = perf.leafShadowLod && !thumbMode ? 12.0f : -1.0f;   // R2: full cards under / beside the eye, the LOD past 12 m
  const int shDraws0 = stats.draws;
  drawItems(progShadow, opaque, true, &frNear);
  if (!foliage.empty() && !thumbMode) drawFoliage(progLeafShadow, lightVP, &frNear, false, leafLod);
  if (grass && !thumbMode && perf.grassShadow) {   // GX3: near pasture clumps cast (within the preset's shadow radius, in the eye's view)
    glUseProgram(progGrassShadow);
    glUniform3fv(U(progGrassShadow, "uCamPos"), 1, glm::value_ptr(v.camPos)); glUniform1f(U(progGrassShadow, "uTime"), time);
    grass->drawShadow(progGrassShadow, lightVP, v0.proj * v.view, v.camPos);
  }
  glUseProgram(progShadow);
  if (!worldModels.empty()) drawModelsDepth(progShadow, worldModels, &frNear);
  gpuMark(GP_ShadowFar);
  // far cascade: the whole visible village / fields (texel ~0.23 m), blended in where the near one ends
  // PF2 farcache: redrawn every 2nd frame, or at once when its focus moves > 8 m, its radius or the key light changes;
  // in between the shaders keep sampling the last map with the matrix it was drawn with (moving casters lag <= 1 frame)
  const float fR = std::max(240.0f, radius * 2.5f);
  const vec3 ffw = v.camPos + vec3(fwd.x, 0, fwd.z) * fR * 0.6f;
  const bool farRedo = !perf.farCache || frame - farFrame >= 2 || fR != farR || glm::distance(ffw, farFocus) > 8.0f || sunDir != farSun;
  if (!q.legacy && q.farCascade && !thumbMode && farRedo) {
    farR = fR; farFrame = frame; farFocus = ffw; farSun = sunDir;
    vec3 ff = ffw; ff.y = t.height(ff.x, ff.z);
    mat4 lv2; lightVPFar = stableOrtho(ff, sunDir, farR, 400.0f, shadowFarSize, lv2);
    minCaster = perf.smallCasters ? 2.0f * (2 * farR / shadowFarSize) : 0.0f;   // PF2: casters under ~1 texel
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFarFbo);
    glViewport(0, 0, shadowFarSize, shadowFarSize);
    glClear(GL_DEPTH_BUFFER_BIT);
    glUseProgram(progShadow);
    glUniformMatrix4fv(U(progShadow, "uVP"), 1, GL_FALSE, glm::value_ptr(lightVPFar));
    Frustum frFar = Frustum::fromVP(lightVPFar);
    drawItems(progShadow, opaque, true, &frFar);
    if (!foliage.empty()) drawFoliage(progLeafShadow, lightVPFar, &frFar, false, perf.leafShadowLod ? 0.0f : -1.0f);
    glUseProgram(progShadow); glUniformMatrix4fv(U(progShadow, "uVP"), 1, GL_FALSE, glm::value_ptr(lightVPFar));
    if (!worldModels.empty()) drawModelsDepth(progShadow, worldModels, &frFar);
    minCaster = 0;
  }
  castCull = false; stats.shadowDraws = stats.draws - shDraws0;
  gpuMark(GP_SkyOcc);
  // ---- sky occlusion (top down). PF2 occcache: redrawn every 16 frames, or at once when the camera moves 4 m across
  // or 8 m up / down (the map covers +-90 m, so the camera stays >= 86 m inside it); thumbnails always redraw.
  float occR = 90.0f;
  const float occTopW = std::max(v.camPos.y, 0.0f) + 60.0f;
  const bool occRedo = !perf.occCache || thumbMode || frame - occFrame >= 16 || glm::length(vec2(v.camPos.x - occCam.x, v.camPos.z - occCam.z)) > 4.0f || std::fabs(occTopW - occTop) > 8.0f;
  if (occRedo) {
  occFrame = thumbMode ? -1000 : frame; occCam = v.camPos;
  vec3 ofocus = v.camPos; occTop = occTopW; occRange = 200.0f;
  {
    float texel = 2 * occR / occSize;
    ofocus.x = std::floor(ofocus.x / texel) * texel; ofocus.z = std::floor(ofocus.z / texel) * texel;
    mat4 view = glm::lookAt(vec3(ofocus.x, occTop, ofocus.z), vec3(ofocus.x, occTop - 1, ofocus.z), vec3(0, 0, -1));
    mat4 proj = glm::ortho(-occR, occR, -occR, occR, 0.0f, occRange);
    occVP = proj * view;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, occFbo);
  glViewport(0, 0, occSize, occSize);
  glClear(GL_DEPTH_BUFFER_BIT);
  glUseProgram(progShadow);
  glUniformMatrix4fv(U(progShadow, "uVP"), 1, GL_FALSE, glm::value_ptr(occVP));
  Frustum frOcc = Frustum::fromVP(occVP);
  minCaster = perf.smallCasters ? std::max(0.3f, 2.0f * (2 * occR / occSize)) : 0.0f;   // PF2: props too small to shade the sky
  drawItems(progShadow, opaque, true, &frOcc);
  if (!foliage.empty() && !thumbMode) drawFoliage(progLeafShadow, occVP, &frOcc, false, perf.leafShadowLod ? 0.0f : -1.0f);
  glUseProgram(progShadow); glUniformMatrix4fv(U(progShadow, "uVP"), 1, GL_FALSE, glm::value_ptr(occVP));
  if (!worldModels.empty()) drawModelsDepth(progShadow, worldModels, &frOcc);
  minCaster = 0;
  }
  glDisable(GL_POLYGON_OFFSET_FILL);
  lightingPrepass(*this, v, opaque, t);   // L2: light upload, light shadow atlas, probe captures, froxel clusters (standard Z)

  gpuMark(GP_Clears);
  // ---- main HDR pass. With TAA the world is rendered with a sub-pixel jitter (Halton 2,3, 8 phases); effects,
  // the weapon and overlays use the unjittered matrices.
  bool taaOn = q.taa && !q.legacy && !thumbMode;
  mat4 vpU = v.proj * v.view;
  mat4 projJ = v.proj;
  if (taaOn) {
    auto halton = [](int i, int b) { float f = 1, r = 0; while (i > 0) { f /= b; r += f * (i % b); i /= b; } return r; };
    int ph = frame % std::clamp(q.taaPhases, 1, 64) + 1;
    jitterPx = vec2(halton(ph, 2) - 0.5f, halton(ph, 3) - 0.5f) * q.taaJitter;   // +-0.25 px default: edges stay crisp
    projJ[2][0] += jitterPx.x * 2.0f / width; projJ[2][1] += jitterPx.y * 2.0f / height;
    // camera cut: teleport / mode switch / respawn (a fast flick is fine; reprojection handles it)
    if (glm::length(v.camPos - prevCam) > 4.0f) taaValid = false;
  } else { taaValid = false; jitterPx = vec2(0); }
  mat4 vp = projJ * v.view;
  // main-view planes from the standard-Z projection (reverse-Z loses the far plane); the TAA jitter is sub-pixel
  Frustum frMain = Frustum::fromProjView(v0.proj, v.view);
  const bool ms = msFbo && !thumbMode && !q.legacy;   // B1 MSAA: the opaque world goes to msFbo, resolved before water
  const GLuint worldFbo = ms ? msFbo : hdrFbo;
  glBindFramebuffer(GL_FRAMEBUFFER, worldFbo);
  glViewport(0, 0, width, height);
  revZ = true; glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE); glClearDepth(0.0);   // reverse-Z from here to the end
  glClearColor(0.7f, 0.73f, 0.75f, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  if (!thumbMode) { float z4[4] = {0, 0, 0, 0}, n4[4] = {0, 1, 0, 1}; glClearNamedFramebufferfv(worldFbo, GL_COLOR, 2, z4); glClearNamedFramebufferfv(worldFbo, GL_COLOR, 3, n4); glClearNamedFramebufferfv(worldFbo, GL_COLOR, 4, z4); }
  // sky: colour attachment 0 only (the cleared 2 / 3 / 4 are the sky's values); first, or last (PF2 skyLast, see kSkyVS)
  const GLenum b5[5] = {GL_COLOR_ATTACHMENT0, GL_NONE, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3, GL_COLOR_ATTACHMENT4};
  auto drawSky = [&]() {
    gpuMark(GP_Sky);
    glNamedFramebufferDrawBuffer(worldFbo, GL_COLOR_ATTACHMENT0);
    setLighting(progSky, v);
    glUniformMatrix4fv(U(progSky, "uInvVP"), 1, GL_FALSE, glm::value_ptr(glm::inverse(vp)));
    glUniform1i(U(progSky, "uCloudOn"), clouds ? 1 : 0); glUniform1f(U(progSky, "uCloudCover"), cloudCover);
    glDepthFunc(zLEqual()); glDepthMask(GL_FALSE);   // a front-facing triangle: the cull state does not matter
    glBindVertexArray(emptyVao); glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthMask(GL_TRUE); glDepthFunc(zLess());
    if (!thumbMode) glNamedFramebufferDrawBuffers(worldFbo, 5, b5);   // terrain .. grass also write attachments 2-4
  };
  if (!perf.skyLast) drawSky();
  else if (!thumbMode) glNamedFramebufferDrawBuffers(worldFbo, 5, b5);
  glDepthMask(GL_TRUE); glDepthFunc(zLess());   // the world's depth state (drawSky leaves it too; sky-last must set it here)
  auto drawTerrain = [&]() {
    gpuMark(GP_Terrain);
    setLighting(progTerrain, v);
    glUniformMatrix4fv(U(progTerrain, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
    glUniform1i(U(progTerrain, "uGrid"), v.grid ? 1 : 0);
    terrainCacheBind(t, progTerrain);
    glEnable(GL_CULL_FACE); glCullFace(GL_BACK);
    t.draw(progTerrain, v.camPos, vp);   // streamed LOD chunks + deformation tiles (terrain_chunks.cpp)
  };
  if (!perf.terrainLast) drawTerrain();
  gpuMark(GP_Opaque);
  // opaque
  glEnable(GL_CULL_FACE); // all authored solids are closed with outward winding
  setLighting(progMain, v);
  glUniform1i(U(progMain, "uWeather"), thumbMode ? 0 : 1);
  glUniform1i(U(progMain, "uNoWallRelief"), wallRelief ? 0 : 1);
  glUniform1i(U(progMain, "uMatLod"), !thumbMode && perf.matLod ? 1 : 0);
  glUniform1i(U(progMain, "uMatLayer"), thumbMode || q.legacy || !perf.matLayer ? 0 : q.matLayer);
  uploadStaticDecals();
  { static const int dbg = std::getenv("NMK_GX5_DEBUG") ? std::atoi(std::getenv("NMK_GX5_DEBUG")) : 0; glUniform1i(U(progMain, "uGxDebug"), dbg); }
  glUniform1i(U(progMain, "uSDecOn"), thumbMode || q.legacy || q.matLayer == 0 || !perf.staticDecals || !sdecBuf || sdec.empty() ? 0 : 1);
  glUniformMatrix4fv(U(progMain, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
  sortEye = v.camPos;
  bool zpre = perf.zPrepass && !thumbMode;
  if (zpre) {   // R1 zprepass experiment: building copies (items with finish parameters) depth-only first
    glNamedFramebufferDrawBuffer(worldFbo, GL_NONE);
    glUseProgram(progDepth); glUniformMatrix4fv(U(progDepth, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
    depthSurfOnly = true; drawItems(progDepth, opaque, true, &frMain); depthSurfOnly = false;
    glNamedFramebufferDrawBuffers(worldFbo, 5, b5);
    glUseProgram(progMain); glDepthFunc(zLEqual());
  }
  drawItems(progMain, opaque, false, &frMain, true);
  if (zpre) glDepthFunc(zLess());
  gpuMark(GP_Models);
  if (!worldModels.empty()) {   // imported models placed in the world
    glDisable(GL_CULL_FACE);
    setLighting(progModel, v);
    glUniformMatrix4fv(U(progModel, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
    drawModels(worldModels, &frMain);
    glEnable(GL_CULL_FACE);
  }
  gpuMark(GP_Trees);
  // tree canopies (double sided, alpha tested)
  if (!foliage.empty()) {
    glDisable(GL_CULL_FACE);
    selectLeafLods(v);
    for (GLuint lp : {progLeaf, progLeafI}) {
      setLighting(lp, v);
      glUniformMatrix4fv(U(lp, "uVPU"), 1, GL_FALSE, glm::value_ptr(vpU));
      glUniformMatrix4fv(U(lp, "uPrevVP"), 1, GL_FALSE, glm::value_ptr(taaValid ? prevVP : vpU));
      glUniform1f(U(lp, "uPrevTime"), time - v.dt);
    }
    if (perf.leafPrepass) {   // PF2: canopy depth first (alpha test only), then each covered pixel is shaded once at GL_EQUAL
      glNamedFramebufferDrawBuffer(worldFbo, GL_NONE);
      drawFoliage(progLeafShadow, vp, &frMain, true);
      if (thumbMode) glNamedFramebufferDrawBuffer(worldFbo, GL_COLOR_ATTACHMENT0); else glNamedFramebufferDrawBuffers(worldFbo, 5, b5);
      glDepthFunc(GL_EQUAL); glDepthMask(GL_FALSE);
      drawFoliage(progLeaf, vp, &frMain, true);
      glDepthFunc(zLess()); glDepthMask(GL_TRUE);
    } else drawFoliage(progLeaf, vp, &frMain, true);
    glEnable(GL_CULL_FACE);
  }
  if (perf.terrainLast) drawTerrain();   // PF2: after solids / models / canopies, so the ground they hide is never shaded
  gpuMark(GP_Grass);
  // grass after opaque so buildings reject hidden blades early; double sided
  if (grass && !thumbMode) {
    glDisable(GL_CULL_FACE);
    setLighting(progGrass, v);
    glUniformMatrix4fv(U(progGrass, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
    glUniformMatrix4fv(U(progGrass, "uVPU"), 1, GL_FALSE, glm::value_ptr(vpU));
    glUniformMatrix4fv(U(progGrass, "uPrevVP"), 1, GL_FALSE, glm::value_ptr(taaValid ? prevVP : vpU));
    glUniform1f(U(progGrass, "uPrevTime"), time - v.dt);
    grass->draw(progGrass, vp, v.camPos);
    stats.draws += grass->lastDrawnChunks; stats.tris += (int)grass->lastDrawnBlades * 8;
    glEnable(GL_CULL_FACE);
  }
  gpuMark(GP_Nature);
  natDraws = 0; natTufts = 0;
  if (!natPatches.empty() && grass && !thumbMode) {   // flower / tall-grass patches (nature.cpp): same program, wind and fade
    glDisable(GL_CULL_FACE);
    setLighting(progGrass, v);
    glUniformMatrix4fv(U(progGrass, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
    glUniformMatrix4fv(U(progGrass, "uVPU"), 1, GL_FALSE, glm::value_ptr(vpU));
    glUniformMatrix4fv(U(progGrass, "uPrevVP"), 1, GL_FALSE, glm::value_ptr(taaValid ? prevVP : vpU));
    glUniform1f(U(progGrass, "uPrevTime"), time - v.dt);
    // Low / Medium / High share of each patch and reach (the pre-GX3 grass radii: patches keep their own LOD)
    int gq = std::clamp((int)grass->quality, 0, 4);
    const float natFrac[5] = {0.0f, 0.6f, 0.8f, 1.0f, 1.0f}, natR[5] = {0.0f, 38.0f, 58.0f, 85.0f, 85.0f};   // G1 Ultra: patches as High
    if (natFrac[gq] > 0) natureDrawPatches(progGrass, natPatches, vp, v.camPos, natR[gq] * 1.1f, natFrac[gq], wind, natDraws, natTufts);
    stats.draws += natDraws; stats.tris += (int)natTufts * 30;
    glEnable(GL_CULL_FACE);
  }
  if (perf.skyLast) drawSky();
  gpuMark(GP_MsaaResolve);
  if (ms) {   // B1 MSAA resolve into the HDR target; water and everything after draw there, as without MSAA
    resolveMsaa();
    glBindFramebuffer(GL_FRAMEBUFFER, hdrFbo);
    GLenum b5[5] = {GL_COLOR_ATTACHMENT0, GL_NONE, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3, GL_COLOR_ATTACHMENT4}; glNamedFramebufferDrawBuffers(hdrFbo, 5, b5);
  }
  gpuMark(GP_Water);
  if (!water.empty() && !thumbMode) { extern void waterRenderPass(Renderer&, const RenderView&, const mat4&); waterRenderPass(*this, v, vp); }   // V1 water (water.cpp)
  glNamedFramebufferDrawBuffer(hdrFbo, GL_COLOR_ATTACHMENT0);   // transparent / effects / weapon never touch attachment 2
  gpuMark(GP_Gore);
  if (!thumbMode) { extern void goreRenderPass(Renderer&, const RenderView&, const mat4&, unsigned); goreRenderPass(*this, v, vp, depthTex); }   // W7 gore: blood decals + droplets
  gpuMark(GP_Glass);
  // transparent: glass then ghosts
  glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);
  glDisable(GL_CULL_FACE);
  if (!glass.empty()) {   // culled first, then sorted far to near (the sort only sees what is drawn)
    std::vector<DrawItem> sorted; sorted.reserve(glass.size());
    for (auto& g : glass) {
      if (!g.mesh || !g.mesh->count) continue;
      if (!frMain.visible(g.mesh->bmin, g.mesh->bmax, g.model, 0.01f)) { stats.culled++; continue; }
      sorted.push_back(g);
    }
    std::sort(sorted.begin(), sorted.end(), [&](const DrawItem& a, const DrawItem& b) {
      return glm::length(vec3(a.model[3]) - v.camPos) > glm::length(vec3(b.model[3]) - v.camPos); });
    setLighting(progGlass, v);
    glUniformMatrix4fv(U(progGlass, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
    drawItems(progGlass, sorted, false);
  }
  if (!ghosts.empty()) {
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    setLighting(progMain, v);
    glUniformMatrix4fv(U(progMain, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
    drawItems(progMain, ghosts, false, &frMain);
  }
  // world-only composite (light shafts; later AO / reflections / TAA): runs before effects and the first-person
  // pass, so nothing here ever reads the weapon's depth
  gpuMark(GP_Composite);
  if (!thumbMode && !q.legacy) { RenderView vj = v; vj.proj = projJ; worldComposite(vj, vp); }
  gpuMark(GP_Taa);
  if (taaOn) taaResolve(vp, vpU, v);
  // bloom is taken from the lit world *before* effects are drawn, so muzzle flashes, tracers and impact sparks
  // look as they did before this pass (no glow)
  gpuMark(GP_Bloom);
  if (!thumbMode && !q.legacy && q.bloom && q.bloomStrength > 0) bloomPass();
  gpuMark(GP_Fx);
  // effects: smoke (alpha) then flashes / tracers / streaks (additive), depth-tested, no depth writes
  if (!fxAlpha.empty() || !fxAdd.empty()) {
    glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND); glEnable(GL_DEPTH_TEST);   // own state: the world composite / TAA passes leave blending off
    glUseProgram(progFx);
    glUniformMatrix4fv(U(progFx, "uVP"), 1, GL_FALSE, glm::value_ptr(vpU));   // after the TAA resolve: unjittered
    glBindVertexArray(fxVao);
    if (!fxAlpha.empty()) {
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glUniform1i(U(progFx, "uAdd"), 0);
      glNamedBufferData(fxVbo, fxAlpha.size() * sizeof(FxVert), fxAlpha.data(), GL_STREAM_DRAW);
      glDrawArrays(GL_TRIANGLES, 0, (GLsizei)fxAlpha.size());
    }
    if (!fxAdd.empty()) {
      glBlendFunc(GL_ONE, GL_ONE); glUniform1i(U(progFx, "uAdd"), 1);
      glNamedBufferData(fxVbo, fxAdd.size() * sizeof(FxVert), fxAdd.data(), GL_STREAM_DRAW);
      glDrawArrays(GL_TRIANGLES, 0, (GLsizei)fxAdd.size());
    }
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  }
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  gpuMark(GP_Weapon);
  // optic eyepiece (W2): world -> eyepiece composite (kEyepieceFS mode 0) -> weapon -> eyepiece restored over it (mode 1)
  const bool eyepiece = v.eyepieceR > 0 && !thumbMode;
  auto eyepiecePass = [&](int mode) {
    glDisable(GL_DEPTH_TEST);
    glUseProgram(progEyepiece);
    glBindTextureUnit(0, mode == 0 ? hdrColor : dofTex); glUniform1i(U(progEyepiece, "uSrc"), 0);
    glUniform2f(U(progEyepiece, "uRes"), (float)width, (float)height);
    glUniform1f(U(progEyepiece, "uR"), v.eyepieceR);
    glUniform1f(U(progEyepiece, "uDark"), v.reticle == 3 ? 0.006f : 0.035f);   // linear: gunner sight soft black; scopes the world dimly (~20 % on screen)
    glUniform1f(U(progEyepiece, "uShade"), 0.6f);
    glUniform1i(U(progEyepiece, "uReticle"), v.reticle); glUniform1i(U(progEyepiece, "uMode"), mode);
    glBindVertexArray(emptyVao); glDrawArrays(GL_TRIANGLES, 0, 3);
    glEnable(GL_DEPTH_TEST);
  };
  if (eyepiece) {
    glGenerateTextureMipmap(hdrColor);   // the blur source; also the exposure's (below), so the dark surround does not brighten the view
    glBindFramebuffer(GL_FRAMEBUFFER, dofFbo);
    eyepiecePass(0);
    glCopyImageSubData(dofTex, GL_TEXTURE_2D, 0, 0, 0, 0, hdrColor, GL_TEXTURE_2D, 0, 0, 0, 0, width, height, 1);
    glBindFramebuffer(GL_FRAMEBUFFER, hdrFbo);
  }
  if (!viewModel.empty() || !viewModelTex.empty()) {
    // First-person weapon (and arms): drawn over a cleared depth buffer so it never clips into walls, lit at its
    // world position, with its own fixed-FOV projection so the world FOV can zoom while the weapon keeps its size.
    glClear(GL_DEPTH_BUFFER_BIT);
    GLenum bufs[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glNamedFramebufferDrawBuffers(hdrFbo, 2, bufs);
    float zero[4] = {0, 0, 0, 0}; glClearNamedFramebufferfv(hdrFbo, GL_COLOR, 1, zero);
    mat4 vvp = (v.vmProj[2][3] != 0.0f ? v.vmProj : v.proj) * v.view;
    glEnable(GL_CULL_FACE);
    setLighting(progMain, v);
    glUniform1i(U(progMain, "uWeather"), 0);
    glUniformMatrix4fv(U(progMain, "uVP"), 1, GL_FALSE, glm::value_ptr(vvp));
    drawItems(progMain, viewModel, false);
    if (!viewModelTex.empty()) {
      glDisable(GL_CULL_FACE);
      setLighting(progModel, v);
      glUniformMatrix4fv(U(progModel, "uVP"), 1, GL_FALSE, glm::value_ptr(vvp));
      drawModels(viewModelTex);
      glEnable(GL_CULL_FACE);
    }
    glNamedFramebufferDrawBuffer(hdrFbo, GL_COLOR_ATTACHMENT0);
    if (dofEnabled && v.dofAmount > 0.001f && v.dofFocus > 0.0f) {
      glBindFramebuffer(GL_FRAMEBUFFER, dofFbo);
      glDisable(GL_DEPTH_TEST);
      glUseProgram(progDof);
      glBindTextureUnit(0, hdrColor); glBindTextureUnit(1, vmDistTex);
      glUniform1i(U(progDof, "uHdr"), 0); glUniform1i(U(progDof, "uDist"), 1);
      glUniform1f(U(progDof, "uFocus"), v.dofFocus); glUniform1f(U(progDof, "uAmount"), v.dofAmount);
      glUniform1f(U(progDof, "uMaxR"), 6.0f * height / 1080.0f);
      glUniform2f(U(progDof, "uTexel"), 1.0f / width, 1.0f / height);
      glBindVertexArray(emptyVao); glDrawArrays(GL_TRIANGLES, 0, 3);
      glCopyImageSubData(dofTex, GL_TEXTURE_2D, 0, 0, 0, 0, hdrColor, GL_TEXTURE_2D, 0, 0, 0, 0, width, height, 1);
      glEnable(GL_DEPTH_TEST);
      glBindFramebuffer(GL_FRAMEBUFFER, hdrFbo);
    }
    if (eyepiece) {   // dofTex still holds the eyepiece composite
      glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_SRC_ALPHA);
      eyepiecePass(1);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDisable(GL_BLEND);
    }
  }

  gpuMark(GP_Post);
  // ---- exposure
  if (!eyepiece) glGenerateTextureMipmap(hdrColor);
  int levels = (int)std::floor(std::log2((float)std::max(width, height)));
  glBindFramebuffer(GL_FRAMEBUFFER, expoFbo[expoIdx ^ 1]);
  glViewport(0, 0, 1, 1);
  glDisable(GL_DEPTH_TEST);
  glUseProgram(progExpo);
  glBindTextureUnit(0, hdrColor); glBindTextureUnit(1, expoTex[expoIdx]);
  glUniform1i(U(progExpo, "uHdr"), 0); glUniform1i(U(progExpo, "uPrev"), 1);
  glUniform1f(U(progExpo, "uLod"), (float)levels); glUniform1f(U(progExpo, "uDt"), v.dt);
  glUniform1f(U(progExpo, "uReset"), expoReset); expoReset = -1;
  lightingExposure(*this, progExpo);   // L2: exposure range of the time of day
  glBindVertexArray(emptyVao); glDrawArrays(GL_TRIANGLES, 0, 3);
  expoIdx ^= 1;

  // ---- tonemap into LDR (shares depth for overlays)
  glBindFramebuffer(GL_FRAMEBUFFER, ldrFbo); glInvalidateNamedFramebufferData(ldrFbo, 1, kInvC0);   // the tonemap writes every pixel (depth is shared: kept)
  glViewport(0, 0, width, height);
  glUseProgram(progTone);
  glBindTextureUnit(0, hdrColor); glBindTextureUnit(1, expoTex[expoIdx]);
  glUniform1i(U(progTone, "uHdr"), 0); glUniform1i(U(progTone, "uExpo"), 1);
  glUniform1f(U(progTone, "uFixedExpo"), v.autoExposure ? -1.0f : lightingFixedExposure(*this));   // 1 by Day
  glUniform1f(U(progTone, "uUnderTone"), v.underwater ? 1.0f : 0.0f);   // swim.cpp
  {   // colour grade (content/settings/grade.json, read once; the defaults match the shipped file)
    static vec4 g(0.91f, 0.21f, 1.0f, 0.18f); static vec3 lo(0.965f, 0.995f, 1.045f), hi(1.035f, 1.0f, 0.955f); static bool read = false;
    if (!read) {
      read = true;
      try {
        json j = json::parse(readTextFile(contentDir() / "settings" / "grade.json"));
        auto v3 = [&](const char* k, vec3 d) { return j.contains(k) ? vec3(j[k][0].get<float>(), j[k][1].get<float>(), j[k][2].get<float>()) : d; };
        g = vec4(j.value("saturation", g.x), j.value("vignette", g.y), j.value("splitTone", g.z), j.value("pivot", g.w));
        lo = v3("shadowTint", lo); hi = v3("highlightTint", hi);
      } catch (std::exception& e) { logf("render: settings/grade.json: %s (defaults)", e.what()); }
    }
    glUniform4f(U(progTone, "uGrade"), g.x, g.y, g.z, g.w); glUniform3f(U(progTone, "uGradeLo"), lo.x, lo.y, lo.z); glUniform3f(U(progTone, "uGradeHi"), hi.x, hi.y, hi.z);
  }
  glUniform1i(U(progTone, "uTonemap"), q.legacy ? 0 : 1);
  glBindTextureUnit(2, bloomTex[0]); glUniform1i(U(progTone, "uBloom"), 2);
  glUniform1f(U(progTone, "uBloomAmt"), !q.legacy && q.bloom ? q.bloomStrength : 0.0f);
  glUniform1f(U(progTone, "uEyeR"), eyepiece ? v.eyepieceR : 0.0f); glUniform1f(U(progTone, "uAspect"), (float)width / std::max(1, height));
  {   // GX5 grade (content/settings/grade.json)
    bool on = grade.on && perf.grade && !q.legacy && !thumbMode;
    glUniform1i(U(progTone, "uGradeOn"), on ? 1 : 0);
    glUniform1f(U(progTone, "uGradeSat"), grade.saturation); glUniform1f(U(progTone, "uGradeContrast"), grade.contrast);
    glUniform1f(U(progTone, "uGradeVig"), grade.vignette); glUniform1f(U(progTone, "uGradeCA"), grade.chromatic);
    glUniform1f(U(progTone, "uGradeSharpen"), q.taa ? grade.sharpen : 0.0f);
    glUniform3fv(U(progTone, "uGradeShadow"), 1, glm::value_ptr(grade.shadowTint)); glUniform3fv(U(progTone, "uGradeHigh"), 1, glm::value_ptr(grade.highlightTint));
  }
  glDrawArrays(GL_TRIANGLES, 0, 3);

  // ---- editor overlays (separate pass; never drawn in Explore)
  if (v.editor && (!ov.lines.empty() || !ov.tris.empty() || !ov.linesTop.empty())) {
    glUseProgram(progLine);
    glUniformMatrix4fv(U(progLine, "uVP"), 1, GL_FALSE, glm::value_ptr(vpU));
    glBindVertexArray(lineVao);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    auto drawV = [&](const std::vector<OverlayVert>& vv, GLenum mode, bool depth) {
      if (vv.empty()) return;
      if (depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
      glNamedBufferData(lineVbo, vv.size() * sizeof(OverlayVert), vv.data(), GL_STREAM_DRAW);
      glDrawArrays(mode, 0, (GLsizei)vv.size());
    };
    glDepthMask(GL_FALSE);
    drawV(ov.tris, GL_TRIANGLES, true);
    drawV(ov.lines, GL_LINES, true);
    drawV(ov.linesTop, GL_LINES, false);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
  }
  glEnable(GL_DEPTH_TEST);
  {   // to the window: 1:1, or upscaled (bilinear) from the reduced internal resolution
    int dw = displayW > 0 ? displayW : width, dh = displayH > 0 ? displayH : height;
    glBlitNamedFramebuffer(ldrFbo, 0, 0, 0, width, height, 0, 0, dw, dh, GL_COLOR_BUFFER_BIT, (dw == width && dh == height) ? GL_NEAREST : GL_LINEAR);
  }
  if (!thumbMode) {   // indirect light, normal + roughness, motion and the world depth: dead until next frame's clears (the
    // LDR colour and the first-person distance stay: screenshots and the sights tests read them after render())
    const GLenum dead[4] = {GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3, GL_COLOR_ATTACHMENT4, GL_DEPTH_ATTACHMENT};
    glInvalidateNamedFramebufferData(hdrFbo, 4, dead);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE); glClearDepth(1.0); glDepthFunc(GL_LESS);   // back to standard GL
  water.clear();
  lightingEnd(*this);
  gpuMark(GP_UI);   // ends at the app's GP_Count mark after ImGui
  if (elapsedQ) { glEndQuery(GL_TIME_ELAPSED); qUsed[qidx] = true; qidx = (qidx + 1) % 3; qvalid = true; } else qvalid = false;
  stats.cpuMs = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - c0).count();
}

void Renderer::worldComposite(const RenderView& v, const mat4& vp) {
  bool shafts = q.shafts && q.shaftStrength > 0, ao = q.ssao && q.ssaoStrength > 0, ssr = q.ssr && q.ssrStrength > 0;
  if (!shafts && !ao && !ssr) return;
  glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
  glBindVertexArray(emptyVao);
  mat4 inv = glm::inverse(vp), invP = glm::inverse(v.proj);
  if (ao) {
    glBindFramebuffer(GL_FRAMEBUFFER, aoFbo); glInvalidateNamedFramebufferData(aoFbo, 1, kInvC0);
    glViewport(0, 0, std::max(1, width / aoDiv), std::max(1, height / aoDiv));
    glUseProgram(progSsao);
    glUniform1i(U(progSsao, "uFrame"), frame);
    glBindTextureUnit(7, depthTex); glUniform1i(U(progSsao, "uDepth"), 7);
    glUniformMatrix4fv(U(progSsao, "uInvProj"), 1, GL_FALSE, glm::value_ptr(invP));
    glUniformMatrix4fv(U(progSsao, "uProj"), 1, GL_FALSE, glm::value_ptr(v.proj));
    glUniform1f(U(progSsao, "uRadius"), q.ssaoRadius); glUniform1f(U(progSsao, "uPower"), 1.4f);
    glUniform2f(U(progSsao, "uTexel"), (float)aoDiv / width, (float)aoDiv / height);
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  // light shafts at half resolution
  if (shafts) {
  glBindFramebuffer(GL_FRAMEBUFFER, shaftFbo); glInvalidateNamedFramebufferData(shaftFbo, 1, kInvC0);
  glViewport(0, 0, std::max(1, width / 2), std::max(1, height / 2));
  setLighting(progShaft, v);
  glBindTextureUnit(7, depthTex);
  glUniform1i(U(progShaft, "uDepth"), 7);
  glUniformMatrix4fv(U(progShaft, "uInvVP"), 1, GL_FALSE, glm::value_ptr(inv));
  glUniform1f(U(progShaft, "uMaxDist"), 150.0f);
  glUniform1f(U(progShaft, "uDensity"), q.hazeDensity * 1.6f);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  // composite into hdrColor from a copy of it
  glCopyImageSubData(hdrColor, GL_TEXTURE_2D, 0, 0, 0, 0, sceneCopy, GL_TEXTURE_2D, 0, 0, 0, 0, width, height, 1);
  glBindFramebuffer(GL_FRAMEBUFFER, compFbo); glInvalidateNamedFramebufferData(compFbo, 1, kInvC0);   // rebuilt from sceneCopy
  glViewport(0, 0, width, height);
  glUseProgram(progComposite);
  glBindTextureUnit(8, sceneCopy); glBindTextureUnit(9, shaftTex); glBindTextureUnit(10, ambTex); glBindTextureUnit(11, aoTex); glBindTextureUnit(7, depthTex);
  glUniform1i(U(progComposite, "uScene"), 8); glUniform1i(U(progComposite, "uShafts"), 9);
  glUniform1i(U(progComposite, "uAmb"), 10); glUniform1i(U(progComposite, "uAO"), 11); glUniform1i(U(progComposite, "uDepth"), 7);
  glUniform1f(U(progComposite, "uShaftAmt"), shafts ? q.shaftStrength : 0.0f);
  glUniform1f(U(progComposite, "uAoAmt"), ao ? q.ssaoStrength : 0.0f);
  setLighting(progComposite, v);   // sky model + noise for the reflection estimate
  glUseProgram(progComposite);
  glBindTextureUnit(12, nrTex); glUniform1i(U(progComposite, "uNR"), 12);
  glUniform1f(U(progComposite, "uSsrAmt"), ssr ? q.ssrStrength : 0.0f);
  glUniformMatrix4fv(U(progComposite, "uProj"), 1, GL_FALSE, glm::value_ptr(v.proj));
  glUniformMatrix4fv(U(progComposite, "uView"), 1, GL_FALSE, glm::value_ptr(v.view));
  glUniformMatrix4fv(U(progComposite, "uInvProj"), 1, GL_FALSE, glm::value_ptr(invP));
  glUniform2f(U(progComposite, "uAoTexel"), (float)aoDiv / width, (float)aoDiv / height);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindFramebuffer(GL_FRAMEBUFFER, hdrFbo);
  glViewport(0, 0, width, height);
  glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
}

void Renderer::taaResolve(const mat4& vpJ, const mat4& vpU, const RenderView& v) {
  glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
  glBindVertexArray(emptyVao);
  glCopyImageSubData(hdrColor, GL_TEXTURE_2D, 0, 0, 0, 0, sceneCopy, GL_TEXTURE_2D, 0, 0, 0, 0, width, height, 1);
  int cur = histIdx ^ 1;
  glBindFramebuffer(GL_FRAMEBUFFER, taaFbo[cur]);
  { const GLenum b2[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1}; glInvalidateNamedFramebufferData(taaFbo[cur], 2, b2); }   // colour (from sceneCopy) and the new history
  glViewport(0, 0, width, height);
  glUseProgram(progTaa);
  glBindTextureUnit(8, sceneCopy); glBindTextureUnit(9, histTex[histIdx]); glBindTextureUnit(7, depthTex);
  glUniform1i(U(progTaa, "uCur"), 8); glUniform1i(U(progTaa, "uHist"), 9); glUniform1i(U(progTaa, "uDepth"), 7);
  glBindTextureUnit(10, velTex); glUniform1i(U(progTaa, "uVel"), 10);
  // reproject with the *unjittered* matrices: a still camera then fetches history at exactly the same pixel (the
  // jittered inverse would resample history half a pixel off every frame and accumulate blur)
  (void)vpJ; glUniformMatrix4fv(U(progTaa, "uInvVP"), 1, GL_FALSE, glm::value_ptr(glm::inverse(vpU)));
  glUniformMatrix4fv(U(progTaa, "uPrevVP"), 1, GL_FALSE, glm::value_ptr(prevVP));
  glUniform2f(U(progTaa, "uTexel"), 1.0f / width, 1.0f / height);
  glUniform1i(U(progTaa, "uValid"), taaValid ? 1 : 0);
  glUniform1f(U(progTaa, "uSharpen"), q.taaSharpen);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  histIdx = cur; taaValid = true;
  prevVP = vpU; prevCam = v.camPos;
  glBindFramebuffer(GL_FRAMEBUFFER, hdrFbo); glViewport(0, 0, width, height);
  glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
}

void Renderer::bloomPass() {
  glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
  glBindVertexArray(emptyVao);
  glUseProgram(progBloomDown);
  glUniform1i(U(progBloomDown, "uSrc"), 0);
  for (int i = 0; i < kBloomLevels; ++i) {
    GLuint src = i == 0 ? hdrColor : bloomTex[i - 1];
    int sw = i == 0 ? width : bloomW[i - 1], sh = i == 0 ? height : bloomH[i - 1];
    glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo[i]); glViewport(0, 0, bloomW[i], bloomH[i]); glInvalidateNamedFramebufferData(bloomFbo[i], 1, kInvC0);
    glBindTextureUnit(0, src);
    if (i == 0) glTextureParameteri(hdrColor, GL_TEXTURE_MIN_FILTER, GL_LINEAR);   // read level 0 only
    glUniform2f(U(progBloomDown, "uSrcTexel"), 1.0f / sw, 1.0f / sh);
    glUniform1i(U(progBloomDown, "uFirst"), i == 0 ? 1 : 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (i == 0) glTextureParameteri(hdrColor, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  }
  glUseProgram(progBloomUp);
  glUniform1i(U(progBloomUp, "uSrc"), 0);
  glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE);
  for (int i = kBloomLevels - 1; i > 0; --i) {   // accumulate each smaller level into the next larger one
    glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo[i - 1]); glViewport(0, 0, bloomW[i - 1], bloomH[i - 1]);
    glBindTextureUnit(0, bloomTex[i]);
    glUniform2f(U(progBloomUp, "uSrcTexel"), 1.0f / bloomW[i], 1.0f / bloomH[i]);
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  glDisable(GL_BLEND);
  glBindFramebuffer(GL_FRAMEBUFFER, hdrFbo); glViewport(0, 0, width, height);
  glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
}

bool Renderer::readPixels(std::vector<uint8_t>& rgba, int& w, int& h) {
  w = width; h = height; rgba.resize((size_t)w * h * 4);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, ldrFbo);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  return true;
}

bool Renderer::readVmDist(std::vector<float>& d, int& w, int& h) {
  w = width; h = height; d.assign((size_t)w * h, 0.0f);
  if (!vmDistTex) return false;
  glGetTextureImage(vmDistTex, 0, GL_RED, GL_FLOAT, (GLsizei)(d.size() * sizeof(float)), d.data());
  return true;
}

// Renderer::renderThumbnail moved to thumbs.cpp (W2: build-menu thumbnails)
