#include "render.h"
#include <cstring>
#include <stb_dxt.h>   // CP3c impostor atlases (the implementation is in textures.cpp)
#include <fstream>
#include "user_cache.h"
#include "crumb.h"
#include "ground_paint.h"   // NT4: groundPaintArid
#include "lighting.h"
#include "snow.h"
#include "settings.h"
#include "nature.h"
#include "textures.h"
#include <cfloat>
#include <chrono>
#include <tuple>
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
// BF B4 contact AO decals (spec 4.6, reference 04 s4.8): one screen-space rectangle per object, the bounds of its
// footprint box projected; the fragment darkens up-facing ground within w of the footprint, multiplied into the AO target
static const char* kContactAOVS = R"GLSL(#version 450 core
layout(location=0) in vec4 aA; layout(location=1) in vec4 aB; layout(location=2) in vec4 aC;
uniform mat4 uVP;
flat out vec4 vA, vB, vC;
void main(){
  vA = aA; vB = aB; vC = aC;
  float cs = cos(aA.w), sn = sin(aA.w); vec2 h = aB.xy + aB.z;
  vec2 lo = vec2(1e9), hi = vec2(-1e9); bool behind = false;
  for (int k = 0; k < 8; ++k) {
    vec2 l = vec2((k & 1) == 0 ? -h.x : h.x, (k & 2) == 0 ? -h.y : h.y);
    vec4 p = uVP*vec4(aA.x + cs*l.x + sn*l.y, (k & 4) == 0 ? aC.x : aC.y, aA.z - sn*l.x + cs*l.y, 1.0);
    if (p.w < 0.05) { behind = true; break; }
    lo = min(lo, p.xy/p.w); hi = max(hi, p.xy/p.w);
  }
  if (behind) { lo = vec2(-1.0); hi = vec2(1.0); }
  lo = clamp(lo, -1.0, 1.0); hi = clamp(hi, -1.0, 1.0);
  int v = gl_VertexID; vec2 t = vec2(v == 1 || v == 2 || v == 4 ? 1.0 : 0.0, v == 2 || v == 4 || v == 5 ? 1.0 : 0.0);
  gl_Position = vec4(mix(lo, hi, t), 0.0, 1.0);
}
)GLSL";
static const char* kContactAOFS = R"GLSL(#version 450 core
uniform sampler2D uDepth, uNR; uniform mat4 uInvVP; uniform vec2 uInvSize; uniform vec3 uEye;
flat in vec4 vA, vB, vC;
layout(location=0) out vec4 oAO;
void main(){
  vec2 uv = gl_FragCoord.xy*uInvSize; float z = texture(uDepth, uv).r;
  if (z <= 0.0) discard;
  vec4 p = uInvVP*vec4(uv*2.0 - 1.0, z, 1.0); vec3 w = p.xyz/p.w;
  if (w.y < vC.x || w.y > vC.y) discard;
  if (texture(uNR, uv).y < 0.55) discard;   // ground only: walls and the object's own sides keep their shading
  vec2 d = w.xz - vA.xz; float cs = cos(vA.w), sn = sin(vA.w);
  vec2 l = vec2(cs*d.x - sn*d.y, sn*d.x + cs*d.y);
  vec2 q = abs(l) - vB.xy; float sd = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
  float occ = vB.w*(1.0 - smoothstep(0.0, vB.z, sd))*(1.0 - smoothstep(40.0, 60.0, length(w - uEye)));
  if (occ <= 0.002) discard;
  oAO = vec4(1.0 - occ, 1.0, 1.0, 1.0);
}
)GLSL";
static const char* kCommon = R"GLSL(
const float PI = 3.14159265;
// W1 weather state for every shader (Renderer::wx.st / wx.der, uploaded once a frame by Renderer::uploadWeather): fixed
// layout, std140, binding 4. Fields by name below; read them through these names only (the packing may grow at the end)
layout(std140, binding = 4) uniform NmkWeather { vec4 wxA, wxB, wxC, wxD, wxE, wxF, wxG, wxH; };
#define wxCover wxA.x
#define wxTau wxA.y
#define wxVisM wxA.z
#define wxFogTopM wxA.w
#define wxPrecip int(wxB.x + 0.5)
#define wxPrecipMmH wxB.y
#define wxWindDirDeg wxB.z
#define wxWindMs wxB.w
#define wxGust wxC.x
#define wxGustPeriodS wxC.y
#define wxTempC wxC.z
#define wxDustVisM wxC.w
#define wxLightningPerMin wxD.x
#define wxSigma wxD.y
#define wxCullM wxD.z
#define wxSunShadow wxD.w
#define wxWindXZ wxE.xy
#define wxWindK wxE.z
#define wxPhys (wxE.w > 0.5)
#define wxTint wxF.rgb
#define wxFogG wxF.w
#define wxESunLux wxG.x
#define wxESkyLux wxG.y
#define wxFogAlbedo wxG.z
#define wxFlash wxG.w
#define wxWet wxH.x
#define wxPuddle wxH.y
#define wxSnowDepth wxH.z
#define wxDustCover wxH.w
#ifdef NMK_FS
// FL1 greenery (content/settings/greenery.json, App::greenery): green leaf pixels drawn toward the reference's spring green
// (uGreen.rgb: linear, unit luminance), each keeping its own luminance x gain; only pixels that are green already (dry,
// autumn, dead, brown leaves and bark untouched). k 0: off (arid maps)
uniform vec4 uGreen;   // rgb target, a: card-leaf strength
uniform vec4 uGreenK;  // x card-leaf gain, y grass strength, z grass gain, w model-foliage strength
float greenMask(vec3 c){ return smoothstep(0.04, 0.2, (c.g - max(c.r, c.b))/max(c.g, 1e-4)); }
vec3 greenGrade(vec3 c, float k, float gain){
  if (k <= 0.0) return c;
  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
  return mix(c, uGreen.rgb*l*gain, k*greenMask(c));
}
#endif
float hash12(vec2 p){ vec3 p3 = fract(vec3(p.xyx)*0.1031); p3 += dot(p3, p3.yzx+33.33); return fract((p3.x+p3.y)*p3.z); }
float hash13(vec3 p){ p = fract(p*0.1031); p += dot(p, p.zyx+31.32); return fract((p.x+p.y)*p.z); }
float vnoise(vec3 p){
  vec3 i = floor(p), f = fract(p); f = f*f*(3.0-2.0*f);
  float a=hash13(i), b=hash13(i+vec3(1,0,0)), c=hash13(i+vec3(0,1,0)), d=hash13(i+vec3(1,1,0));
  float e=hash13(i+vec3(0,0,1)), g=hash13(i+vec3(1,0,1)), h=hash13(i+vec3(0,1,1)), k=hash13(i+vec3(1,1,1));
  return mix(mix(mix(a,b,f.x),mix(c,d,f.x),f.y), mix(mix(e,g,f.x),mix(h,k,f.x),f.y), f.z);
}
float fbm(vec3 p){ float s=0.0, a=0.5; for(int i=0;i<4;i++){ s+=a*vnoise(p); p*=2.03; a*=0.5; } return s; }
// WS1 shared wind (the weather state, R.wx: der.windXZ, der.windK, st.gust, st.gustPeriodS): one direction, a sway amplitude from the wind, a sway
// clock and a gust offset both accumulated on the CPU at the current rates (a changing wind never rescales absolute time),
// and one gust pattern travelling downwind: g = 1 + G (2 noise((along - offset) / L, across / 1.7 L) - 1). Defaults: the pre-WS1 breeze
uniform vec4 uWindV = vec4(0.8, 0.6, 0.0, 0.0);    // xy direction (unit), z gust offset this frame (m), w the previous frame's
uniform vec4 uWindP = vec4(0.0, 0.0, 50.0, 0.0);   // x sway clock this frame (s), y previous, z gust length L (m), w gust factor G
float windSway(float k){ return clamp(pow(max(k, 0.0), 0.8), 0.0, 2.5); }
// BF B1 moss field (docs/vfx-rev3/BATTLE-FOUNDRY-SPEC.md 4.5, reference 03 s3.2): one field drives the film, the mat and
// the cushions. moss.cpp mossFieldCpu is its C++ mirror (same integer hash): keep the two identical.
uniform float uMossBias = 0.0;   // the map's wetness: 0.55 - look.mossThreshold (0 = temperate)
uniform vec4 uMoss = vec4(0.0, 0.0, 12.0, 0.5);   // x on (mossfield), y transmission (Medium and up), z mat range (m; film beyond), w lichen age
uint mossHash(ivec3 c){ uint h = uint(c.x)*0x8da6b343u ^ uint(c.y)*0xd8163841u ^ uint(c.z)*0xcb1ab31fu; h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15; return h; }
float mossVn(vec3 p){
  vec3 i = floor(p), f = p - i; f = f*f*(3.0 - 2.0*f); ivec3 c = ivec3(i);
  #define MH(x, y, z) (float(mossHash(c + ivec3(x, y, z)) & 0xffffu)*(1.0/65535.0))
  return mix(mix(mix(MH(0,0,0), MH(1,0,0), f.x), mix(MH(0,1,0), MH(1,1,0), f.x), f.y),
             mix(mix(MH(0,0,1), MH(1,0,1), f.x), mix(MH(0,1,1), MH(1,1,1), f.x), f.y), f.z);
  #undef MH
}
// moisture: rising damp (y = height above grade, m), drips, cavity, distance to water (m)
float mossMoist(float y, float drip, float cav, float distW){ return clamp(0.40*pow(clamp(1.0 - y/1.4, 0.0, 1.0), 1.8) + 0.25*drip + 0.20*cav + 0.15*clamp(1.0 - distW/8.0, 0.0, 1.0), 0.0, 1.0); }
// shelter: sh0 = 1 - sunHours/6 (baked, 0..1); the face's turn away from the sun's path (sunDir: the path's noon side, +z south; moss.cpp)
float mossShelter(float sh0, vec3 n, vec3 sunDir){ vec3 ds = -normalize(vec3(sunDir.x, 0.0, sunDir.z) + vec3(1e-4, 0.0, 0.0)); return 0.60*clamp(sh0, 0.0, 1.0) + 0.40*clamp(dot(n, ds)*0.5 + 0.5, 0.0, 1.0); }
// (M appearance, G instanced relief, L lichen). nearK fades the 8 cm octave out with distance (1 = near; the CPU mirror uses 1)
vec3 mossField(vec3 p, float upDot, float wallAttach, float groundRecv, float moist, float shelter, float por, float root, float traffic, float excl, float snow, float site, float nearK){
  float attach = max(smoothstep(0.22, 0.85, upDot)*groundRecv, wallAttach);
  float hab = attach*clamp(0.42*moist + 0.30*shelter + 0.18*por + 0.10*root, 0.0, 1.0);
  float pt = smoothstep(0.36, 0.65, hab + uMossBias*step(0.001, attach) + 0.22*(mossVn(p/0.6) - 0.5) + 0.17*(mossVn(p/0.08 + 31.0) - 0.5)*nearK);
  float M = pt*(1.0 - smoothstep(0.18, 0.62, traffic))*(1.0 - excl)*(1.0 - snow);
  float L = clamp((1.0 - shelter)*uMoss.w*(1.0 - moist)*2.0, 0.0, 1.0)*smoothstep(0.55, 0.9, por)*(1.0 - M)*(1.0 - snow)*(1.0 - excl)
          * smoothstep(0.5, 0.68, mossVn(p/0.09 + 7.0)*0.7 + mossVn(p/0.7 + 3.0)*0.3);   // crustose discs, 20-150 mm, in old patches
  return vec3(M, smoothstep(0.50, 0.83, M)*site, L);
}
// the back lobe moss needs or it reads as green felt (4.5): x sun colour x shadow at the call site
vec3 mossTransmit(vec3 N, vec3 L, vec3 V, float th){ return vec3(0.10, 0.21, 0.045)*(pow(clamp(dot(V, -L), 0.0, 1.0), 6.0)*0.55*th); }
const vec3 kMossAlb = vec3(0.055, 0.105, 0.030), kMossDry = vec3(0.085, 0.095, 0.045), kLichenAlb = vec3(0.28, 0.29, 0.24);   // sway amplitude from uWind (der.windK x on / off): 1 = Clear's 3 m/s, the pre-weather look; 2.5 from about 9.4 m/s
float windClock(bool prev){ return prev ? uWindP.y : uWindP.x; }
vec3 windDir(){ return vec3(uWindV.x, 0.0, uWindV.y); }
float windGust(vec2 xz, bool prev){
  float L = max(uWindP.z, 1.0), a = dot(xz, uWindV.xy) - (prev ? uWindV.w : uWindV.z), c = dot(xz, vec2(-uWindV.y, uWindV.x));
  return max(0.0, 1.0 + uWindP.w*(vnoise(vec3(a/L, c/(1.7*L), 0.37))*2.0 - 1.0));
}

uniform vec3 uSunDir, uSunCol, uSkyCol, uGroundCol, uCamPos, uFogCol, uFill;
uniform float uFogDens;
uniform float uUnder;   // 1: the camera is under water (swim.cpp): applyFog / sky become a dense uniform water fog
uniform sampler2DShadow uShadow; uniform mat4 uLightVP; uniform int uShadowOn;
uniform float uShadowAtlas;      // VO L3: uShadow is an atlas, the near cascade in [0, uShadowAtlas) of x, the mid one in [0.5, 1) (atlas 0.5)
uniform mat4 uLightVPC3; uniform int uC3On; uniform float uShadowWorldTexelC3; uniform vec2 uFarAx = vec2(1.0, 0.0);   // CP4 cascade4: a fourth cascade in the far map's left half (far on the right)
uniform mat4 uLightVPMid; uniform int uShadowMidOn; uniform float uShadowWorldTexelMid;   // VO L3: the mid cascade (Lighting New, Medium / High)
uniform sampler2D uOcc; uniform mat4 uOccVP; uniform float uOccTop, uOccRange; uniform int uOccOn;
uniform vec2 uShadowTexel;
// ---- lighting model (docs/rendering.md). Original implementation; BSL v10 (Capt Tatsu) was the visual reference.
uniform sampler2DShadow uShadowFar; uniform mat4 uLightVPFar; uniform int uShadowFarOn;
uniform vec2 uShadowTexelFar;
uniform vec2 uShadowWorldTexel;   // metres per shadow texel: near, far cascade
uniform float uShadowDepthRange;  // metres covered by the [0,1] light-space depth
uniform float uShadowSoft;        // PCF radius in near-cascade texels (0 = legacy 3x3 grid)
uniform float uShadowLift;        // VO L1: share of the sun a shadow removes (Classic 0.7: shadows 30 % lighter; New 1)
uniform float uMicroSh;           // VO L4: micro-shadowing from material AO on the direct light (0 Classic, 1 New)
uniform float uShadowWarm;        // VO L1: sun-hue bias of the sky light on surfaces in the sun's shadow (New; 0 Classic)
uniform int uShadowTaps;          // P3: cap on the PCF taps (presets; 0 = no cap)
uniform int uPcss;                // UL1 (Ultra, --rq-off pcss): contact-hardening near-cascade penumbrae
uniform int uFrame;               // frame counter (noise rotation; TAA decorrelation)
uniform int uCloudShadowOn; uniform float uCloudCov, uCloudT;
uniform float uCloudK, uCloudSeed; uniform vec2 uCloudWind;
uniform float uFlash;   // W2 lightning: 0..1 this frame (sky and ambient flash)
float overcastGrad(float y){ return 1.25*(1.0 + 2.0*max(y, 0.0))/3.0; }   // CIE overcast sky: L(theta) ~ (1 + 2 sin theta) / 3, scaled so its mean matches SKY1's   // WX1: Clear -> Cloudy mix, the field's seed and wind (600 m cells / s)
uniform vec4 uCloudOpen; uniform float uCloudOpenK;   // VO L5 look.cloudOpening: direction (xyz), cosine of its half width (w), strength (0: none)
uniform vec4 uCloudXf;    // VO L5 look.cloudScale / cloudStretch / cloudDir: the cloud field's xz transform (mat2 columns; identity by default)
uniform float uSunDisc;   // VO L5 look.sunDisc: 1 default, 0 no sun disc in the sky
uniform int uFogPhase;   // CP4 fogphase
uniform int uW2;   // W2 (weather corpus): 1 = overcast sky the right way up, fog = the sky at the horizon, soft cloud openings
float cloudOpening(vec3 d){   // VO L5: 0..strength toward the opening; W2: a falloff over twice its half width (the old ~8 deg edge read as a disc)
  float c = dot(d, uCloudOpen.xyz);
  if (uW2 == 1) return uCloudOpenK*smoothstep(cos(2.0*acos(clamp(uCloudOpen.w, -1.0, 1.0))), 1.0, c)*(1.0 - 0.9*smoothstep(0.5, 0.85, uCloudK));   // a closed deck: a faint brightening, no hole (the sun halo showed through as a disc)
  return uCloudOpenK*smoothstep(uCloudOpen.w, mix(uCloudOpen.w, 1.0, 0.6), c);
}   // VO L5: 0..strength toward the opening
uniform vec4 uFog2;   // look.fogLayer2: density, floor y, top y, soft (m); x = 0 off
uniform float uEnvFogTop = 1e9, uEnvFogSoft = 12.0;   // look.fogLayer top: the layer's ceiling at world y, soft over uEnvFogSoft m
uniform float uEnvFog, uEnvFogH, uFogIndoor, uEnvFogNear; uniform sampler2D uFogField; uniform vec4 uFogBox;   // WX1 environmental fog: density (1/m), layer height, indoor scale; field (unit 27), box (min xz, 1/size)
uniform float uAoIndirectOnly;    // 1: material AO / sky occlusion darken only indirect light
// ---- L2 lighting engine (src/lighting.cpp, docs/lighting.md): time of day, clustered local lights, probe GI
uniform vec3 uSkyZen, uSkyHor;    // sky model zenith / horizon colour (time of day; Day = the BSL-pass constants)
uniform vec3 uSkySun, uSkySunCol; // true sun direction and colour for the sky dome / haze glow (the key light may be the moon)
uniform vec3 uSkyK;               // horizon colour relative to Day (exactly 1 by Day): scales the ground band and the legacy dome

// interleaved gradient noise (Jimenez 2014), rotated per frame
float ign(vec2 p){ return fract(52.9829189*fract(dot(p, vec2(0.06711056, 0.00583715))) + float(uFrame % 64)*0.618034); }
#ifdef NMK_FS
float pixelNoise(){ return ign(gl_FragCoord.xy); }
// VO A1 hashed alpha: under TAA the cut-out threshold is dithered per pixel and frame (0.5 +- uHashAlpha / 2), so a leaf
// edge's partial coverage resolves to a soft edge instead of a hard, crawling one; 0: the fixed 0.5 test (no TAA, Low,
// --rq-off hashalpha). The shadow / depth passes keep 0.5 (stable shadows).
uniform float uHashAlpha;
float cutRef(){ return 0.5 + (pixelNoise() - 0.5)*uHashAlpha; }
float pixelNoiseFixed(){ return fract(52.9829189*fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715)))); }   // FL1: the same every frame
#else
float pixelNoise(){ return 0.5; }
float pixelNoiseFixed(){ return 0.5; }
#endif
// UL1 PCSS without a depth sampler: the map is compare-only, so the blocker distance is read from the occluded share at
// a few depth offsets (taps in a disk); the penumbra then grows with it at the sun's 0.53 deg (x1.5 for TAA-softened edges)
float pcssRadius(sampler2DShadow sm, vec3 s, vec2 texel, float worldTexel, float bias, float x0, float x1, float rot){
  const float dz[4] = float[4](0.0, 0.5, 2.0, 8.0);   // m above the receiver
  float occ[4] = float[4](0.0, 0.0, 0.0, 0.0);
  for (int i = 0; i < 6; ++i) {
    float r = sqrt((float(i) + 0.5)/6.0), a = float(i)*2.3999632 + rot;
    vec2 t = s.xy + vec2(cos(a), sin(a))*r*8.0*texel; t.x = clamp(t.x, x0, x1);
    for (int k = 0; k < 4; ++k) occ[k] += 1.0 - texture(sm, vec3(t, s.z - bias - dz[k]/uShadowDepthRange));
  }
  if (occ[0] <= 0.0) return -1.0;   // no blocker near: lit
  float d = 0.0, prev = 0.0;
  for (int k = 0; k < 4; ++k) { float f = occ[k]/max(occ[0], 1e-3); d += f*(dz[k] - prev); prev = dz[k]; }
  return clamp(d*0.0047*1.5/worldTexel, 0.7, 14.0);
}
// one cascade: Vogel-disk PCF in light space. Normal offset grows with the slope to the sun (acne), a small
// constant depth bias keeps contact (no peter-panning where walls meet the ground).
// ax: (scale, bias) of x into the map's atlas half (VO L3); taps are kept inside that half
uniform float uSlopeCap = 6.0;   // CP1 slopecap
float cascadePCF(sampler2DShadow sm, mat4 vp, vec2 texel, float worldTexel, vec3 w, vec3 n, float radiusTex, int taps, out float edge, vec2 ax){
  float ndl = clamp(dot(n, uSunDir), 0.0, 1.0), sinT = sqrt(1.0 - ndl*ndl);
  vec3 off = n*worldTexel*(0.6 + 1.6*sinT);
  vec4 lp = vp*vec4(w + off, 1.0);
  vec3 s = lp.xyz/lp.w*0.5 + 0.5;
  vec2 e = abs(s.xy*2.0 - 1.0); edge = max(e.x, e.y);
  if (edge >= 1.0 || s.z >= 1.0) { edge = 2.0; return 1.0; }
  float bias = uSlopeCap < 5.0 ? worldTexel*(0.35 + 0.9*min(sinT/max(ndl, 0.08), uSlopeCap))*(1.0 + 0.08*radiusTex)/uShadowDepthRange   // CP1 slopecap: cap 2, scaled by the filter radius
                               : worldTexel*(0.35 + 0.9*min(sinT/max(ndl, 0.08), 6.0))/uShadowDepthRange;
  // FL1: the PCF disk turns per pixel but not per frame: a per-frame turn left penumbrae (ridge tiles, eaves,
  // railings) shimmering under a still camera where TAA could not average it out
  float rot = pixelNoiseFixed()*6.2831853, sum = 0.0;
  s.x = s.x*ax.x + ax.y;
  float x0 = ax.y + texel.x, x1 = ax.y + ax.x - texel.x;
  if (uPcss == 1 && taps > 1 && ax.y == 0.0 && ax.x < 1.0) {   // near cascade of the atlas, filtered lookups only
    float pr = pcssRadius(sm, s, texel, worldTexel, bias, x0, x1, rot);
    if (pr < 0.0) return 1.0;
    radiusTex = pr; taps = max(taps, 16);
  }
  for (int i = 0; i < taps; ++i) {
    float r = sqrt((float(i) + 0.5)/float(taps)), a = float(i)*2.3999632 + rot;
    vec2 t = s.xy + vec2(cos(a), sin(a))*r*radiusTex*texel; t.x = clamp(t.x, x0, x1);
    sum += texture(sm, vec3(t, s.z - bias));
  }
  return sum/float(taps);
}
float cascadePCF(sampler2DShadow sm, mat4 vp, vec2 texel, float worldTexel, vec3 w, vec3 n, float radiusTex, int taps, out float edge){ return cascadePCF(sm, vp, texel, worldTexel, w, n, radiusTex, taps, edge, vec2(1.0, 0.0)); }
// VOT T6: the terrain's horizon toward the sun (terrain_normals.cpp horizonSync; unit 26, G / B / A beside the T5 sky
// view in R): distant relief stays shadowed where the cascades end. Ground-level horizon for anything at w.xz.
uniform int uTHorOn; uniform sampler2D uTSV; uniform vec4 uTHorMap, uTHorW;   // map: org.xy, 1 / extent; w: bin weights, sun elevation (deg)
uniform int uTHorAll;   // 1 on the ground and grass: the cascades hold no terrain, so the horizon shades them at every distance
float terrainHorizonSun(vec3 w){
  if (uTHorOn == 0) return 1.0;
  vec2 uv = (w.xz - uTHorMap.xy)*uTHorMap.zw;
  if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) return 1.0;
  float h = dot(texture(uTSV, uv).gba, uTHorW.xyz)*50.0 - 5.0;   // horizon elevation (deg)
  return smoothstep(h - 1.0, h + 1.5, uTHorW.w);   // the sun's disc and a little penumbra
}
float sunShadowCasc(vec3 w, vec3 n, float soft, int taps){
  if (uShadowOn == 0) return 1.0;
  if (uShadowSoft <= 0.0) {   // legacy fallback: 3x3 grid, fixed offsets
    float ndl = clamp(dot(n, uSunDir), 0.0, 1.0);
    vec4 lp = uLightVP * vec4(w + n*(0.02 + 0.05*(1.0-ndl)), 1.0);
    vec3 s = lp.xyz/lp.w*0.5+0.5;
    if (s.x<0.0||s.x>1.0||s.y<0.0||s.y>1.0||s.z>1.0) return 1.0;
    float sum = 0.0;
    s.x *= uShadowAtlas;
    for (int y=-1;y<=1;y++) for (int x=-1;x<=1;x++) sum += texture(uShadow, vec3(s.xy + vec2(x,y)*uShadowTexel*1.2, s.z - 0.0006));
    return sum/9.0;
  }
  if (uShadowTaps > 0) taps = min(taps, uShadowTaps);
  float eN, eF;
  float sN = cascadePCF(uShadow, uLightVP, uShadowTexel, uShadowWorldTexel.x, w, n, uShadowSoft*(1.0 + 2.5*soft), taps, eN, vec2(uShadowAtlas, 0.0));
  float tB = smoothstep(0.78, 0.96, eN);
  if (uShadowMidOn == 1 && tB > 0.0) {   // VO L3: near -> mid (texel-snapped, about 3x the near radius) -> far
    float eM; float sM = cascadePCF(uShadow, uLightVPMid, uShadowTexel, uShadowWorldTexelMid, w, n, 1.6 + 2.0*soft, max(taps*3/4, 4), eM, vec2(0.5, 0.5));
    sN = mix(sN, sM, tB); eN = eM; tB = smoothstep(0.8, 0.96, eM);
    if (eM >= 2.0) { sN = 1.0; tB = 1.0; }   // outside the mid box too: the far cascade alone
  }
  if (uC3On == 1 && tB > 0.0) {   // CP4: mid -> the fourth cascade (the old far range, ~240 m) -> far (~600 m)
    float e3; float s3 = cascadePCF(uShadowFar, uLightVPC3, uShadowTexelFar, uShadowWorldTexelC3, w, n, 1.4 + 1.5*soft, max(taps*2/3, 4), e3, vec2(0.5, 0.0));
    if (e3 < 2.0) { sN = mix(sN, s3, tB); eN = e3; tB = smoothstep(0.82, 0.97, e3); }
  }
  if (tB <= 0.0) return sN;
  if (uShadowFarOn == 0) return mix(sN, terrainHorizonSun(w), tB);   // VOT T6: past the last cascade, the terrain horizon
  float sF = cascadePCF(uShadowFar, uLightVPFar, uShadowTexelFar, uShadowWorldTexel.y, w, n, 1.25 + soft, max(taps/2, 4), eF, uFarAx);
  float fE = smoothstep(0.85, 0.98, eF);
  if (fE > 0.0) sF = mix(sF, terrainHorizonSun(w), fE);   // fade to the terrain horizon (T6; 1 when off) at the far edge instead of a hard line
  return mix(sN, sF, tB);
}
float sunShadowRaw(vec3 w, vec3 n, float soft, int taps){
  float s = sunShadowCasc(w, n, soft, taps);
  return uTHorAll == 1 && uShadowOn == 1 ? min(s, terrainHorizonSun(w)) : s;
}
// VO L7: the reflection cube near the eye (lighting.cpp reflPass), box-parallax corrected around its capture point
uniform samplerCube uRefl; uniform int uReflOn; uniform vec3 uReflPos;
uniform int uReflRoom; uniform mat4 uReflRoomInv; uniform vec3 uReflRoomHalf;   // CP4 reflroom: the capture was taken inside this room: its box is the parallax proxy
vec3 reflProbe(vec3 w, vec3 R, float rough, out float wgt){
  if (uReflRoom == 1) {   // an oriented room box (rigid transform): intersect in its local space; outside it the cube fades over 1 m
    vec3 lw = (uReflRoomInv*vec4(w, 1.0)).xyz, lr = mat3(uReflRoomInv)*R;
    vec3 iv = 1.0/(lr + vec3(1e-5)); vec3 tf = max((uReflRoomHalf - lw)*iv, (-uReflRoomHalf - lw)*iv);
    float t = max(min(min(tf.x, tf.y), tf.z), 0.0);
    vec3 o = abs(lw) - uReflRoomHalf; wgt = 1.0 - smoothstep(-0.2, 1.0, max(max(o.x, o.y), o.z));
    return textureLod(uRefl, w + R*t - uReflPos, rough*6.0).rgb;
  }
  vec3 bmin = uReflPos - vec3(24.0, 1.8, 24.0), bmax = uReflPos + vec3(24.0, 14.0, 24.0);
  vec3 iv = 1.0/(R + vec3(1e-5)); vec3 tf = max((bmax - w)*iv, (bmin - w)*iv);
  float t = max(min(min(tf.x, tf.y), tf.z), 0.0);
  vec3 o = max(bmin - w, w - bmax); wgt = 1.0 - smoothstep(-6.0, 0.0, max(max(o.x, o.y), o.z));   // fades out over the box's last 6 m
  return textureLod(uRefl, w + R*t - uReflPos, rough*6.0).rgb;
}
float sunShadowT(vec3 w, vec3 n, float soft, int taps){ return mix(1.0, sunShadowRaw(w, n, soft, taps), uShadowLift); }   // Classic: sun shadows 30 % lighter (user request); VO L1 New: full shadows, grounded by contact shadows
float shadowPCF(vec3 w, vec3 n){ return sunShadowT(w, n, 0.0, 8); }   // 8 rotated taps; TAA integrates the rotation over frames
// cloud deck at 1500 m: the same field the sky draws, in world xz (so the shadows on the ground match the sky)
float cloudField(vec2 xz){
  xz = mat2(uCloudXf.xy, uCloudXf.zw)*xz;   // VO L5: broad directional banks (look.cloudScale / Stretch / Dir)
  vec3 q = vec3(xz/600.0 + uCloudSeed + uCloudT*uCloudWind, uCloudT*0.002);
  float n = 0.0, a = 0.5;
  for (int i = 0; i < 5; ++i) { n += a*vnoise(q); q = q*2.07 + vec3(1.7, 9.2, 3.1); a *= 0.5; }
  return n;
}
float cloudThreshold(){ return mix(0.72, 0.38, uCloudCov); }
// W2 cloudshell: the field with each octave faded where a pixel covers more than ~a quarter of its period (fw: footprint in
// field units of 600 m), so the far deck goes soft instead of streaking toward the horizon
float cloudFieldAA(vec2 xz, float fw){
  xz = mat2(uCloudXf.xy, uCloudXf.zw)*xz;
  vec3 q = vec3(xz/600.0 + uCloudSeed + uCloudT*uCloudWind, uCloudT*0.002);
  float n = 0.0, a = 0.5, f = 1.0;
  for (int i = 0; i < 5; ++i) { float k = 1.0 - smoothstep(0.25, 0.6, fw*f); n += a*mix(0.5, vnoise(q), k); q = q*2.07 + vec3(1.7, 9.2, 3.1); a *= 0.5; f *= 2.07; }
  return n;
}
// the ray's hit on a cloud shell h m above a 6371 km earth (instead of a plane: no infinite stretch at the horizon)
vec2 shellHit(vec3 d, float h){
  const float Re = 6371000.0; float r0 = Re + max(uCamPos.y, 0.0), r1 = Re + h;
  float b = r0*d.y, t = -b + sqrt(max(b*b + r1*r1 - r0*r0, 0.0));
  return uCamPos.xz + d.xz*t;
}
// low-octave version for the clouds' self-shadowing (only the broad optical depth matters)
float cloudFieldLo(vec2 xz){
  xz = mat2(uCloudXf.xy, uCloudXf.zw)*xz;
  vec3 q = vec3(xz/600.0 + uCloudSeed + uCloudT*uCloudWind, uCloudT*0.002);
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
  return 1.0 - mix(0.62, 0.35, uCloudK)*smoothstep(th - 0.02, th + 0.30, cloudField(q));   // WX2: under full overcast the (already weak) sun varies only gently
}
float cloudShadow(vec3 w){
  if (uCloudShadowOn == 0 || uSunDir.y < 0.05) return 1.0;
  vec2 uv = (w.xz - uCloudShadowBox.xy)/(2.0*uCloudShadowBox.z) + 0.5;
  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return cloudShadowAt(w.xz, w.y);
  return texture(uCloudShadowMap, uv).r;
}
uniform int uSkyVisWall;   // CP4 skyviswall
float skyVis(vec3 w, vec3 n){
  if (uOccOn == 0) return 1.0;
  vec3 q = w + vec3(n.x, 0.0, n.z)*0.65;
  vec4 lp = uOccVP * vec4(q, 1.0);
  vec2 uv = lp.xy*0.5+0.5;
  if (uv.x<0.0||uv.x>1.0||uv.y<0.0||uv.y>1.0) return 1.0;
  vec2 eb = abs(uv*2.0 - 1.0); float boxFade = uSkyVisWall == 1 ? 1.0 - smoothstep(0.8, 1.0, max(eb.x, eb.y)) : 1.0;   // CP4: the occlusion fades out over the map's last 20 % (no hard square at its edge)
  float occ = 0.0;
  vec2 t = 1.0/vec2(textureSize(uOcc,0));
  // CP4 skyviswall: a wall sees half the sky sideways, so something high above its footprint (a dormer, an eave 15 m up)
  // shades it only by the share of sky it hides: ~ eave depth / height. Weighted by 2 m / height above the pixel on
  // vertical faces; real eaves (a metre or two up) still shelter fully. Level surfaces keep the plain top-down test.
  float vert = uSkyVisWall == 1 ? 1.0 - smoothstep(0.5, 0.8, abs(n.y)) : 0.0;
  for (int y=-1;y<=1;y++) for (int x=-1;x<=1;x++) {
    float d = texture(uOcc, uv + vec2(x,y)*t*1.5).r;
    float oy = uOccTop - d*uOccRange, dz = oy - w.y;
    occ += smoothstep(0.15, 0.6, dz)*mix(1.0, clamp(2.0/max(dz, 0.5), 0.0, 1.0), vert);
  }
  return 1.0 - boxFade*occ/9.0;
}
// SN1 shared snow layer (snow.cpp): a map's mask (R deposit x retention, G traffic, B melt, A pooling) and the spec's
// snow = saturate(slope_response * deposit * retention - traffic - melt); shelter from skyVis (anything solid above).
// States: dry snow (cool, broad rough highlight, low-contrast grain), dirty slush (darker grit, tighter lobe), meltwater.
uniform int uSnowOn; uniform sampler2D uSnowMask; uniform vec4 uSnowBox, uSnowP, uSnowP2;
float gSnow = 0.0;
vec3 gSnowAlb0 = vec3(0.0), gSnowN0 = vec3(0.0, 1.0, 0.0); float gSnowR0 = 1.0;   // SN2: the ground just before applySnow's snow mix (snowTex re-blends from it)
// SN2 (corpus weather 05): lying snow depth (m) from the baked mask alone (no noise), for burying grass: up to ~0.18 m
float snowDepthAt(vec2 xz){
  if (uSnowOn == 0) return 0.0;
  vec2 uv = (xz - uSnowBox.xy)*uSnowBox.zw;
  vec4 M = (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) ? vec4(0.82, 0.0, 0.0, 0.0) : textureLod(uSnowMask, uv, 0.0);
  return 0.18*smoothstep(uSnowP.z - uSnowP.w, uSnowP.z + uSnowP.w, M.r - M.g - M.b - uSnowP2.w);
}
uniform int uSnowFar;   // BT2 switch snowfar (1: the distance gates in applySnow)
// BT2 / OPT2: true only where applySnow's ground cover is certainly full (worst-case patch noise, the roof shelter, a
// margin for SN2's height-blended edges): the ground under it needs no substrate texture taps. Uses only what
// applySnow reads before any terrTex call: the baked mask, the geometric slope, the shelter tap (near).
bool snowFullCover(vec3 w, vec3 Ng){
  if (uSnowOn == 0) return false;
  vec2 uv = (w.xz - uSnowBox.xy)*uSnowBox.zw;
  vec4 M = (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) ? vec4(0.82, 0.0, 0.0, 0.0) : texture(uSnowMask, uv);
  float slope = smoothstep(uSnowP.x, uSnowP.y, Ng.y);
  float sMin = slope*M.r*(1.0 - uSnowP2.y) - M.g - M.b - uSnowP2.w;   // the noise at its lowest
  if (sMin < uSnowP.z + uSnowP.w + 0.1) return false;                  // + 0.1: room for the height-blended edge
  if (uOccOn == 1 && length(w - uCamPos) < 150.0) { vec4 lp = uOccVP*vec4(w, 1.0); vec2 ouv = lp.xy*0.5 + 0.5;
    if (ouv.x > 0.0 && ouv.x < 1.0 && ouv.y > 0.0 && ouv.y < 1.0 && uOccTop - texture(uOcc, ouv).r*uOccRange - w.y > 0.15) return false; }
  return true;
}
void applySnow(vec3 w, vec3 Ng, float ground, inout vec3 alb, inout float rough, inout vec3 N){
  gSnow = 0.0;
  if (uSnowOn == 0) return;
  vec2 uv = (w.xz - uSnowBox.xy)*uSnowBox.zw;
  vec4 M = (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) ? vec4(0.82, 0.0, 0.0, 0.0) : texture(uSnowMask, uv);
  float dc = length(w - uCamPos);
  float vis = 1.0;                                        // 0 under a roof / eave / canopy (one tap of the sky-occlusion map: M5)
  if (uOccOn == 1 && (dc < 150.0 || uSnowFar == 0)) { vec4 lp = uOccVP*vec4(w, 1.0); vec2 ouv = lp.xy*0.5 + 0.5;   // BT2 perf: shelter near only (switch snowfar)
    if (ouv.x > 0.0 && ouv.x < 1.0 && ouv.y > 0.0 && ouv.y < 1.0) vis = 1.0 - smoothstep(0.15, 0.6, uOccTop - texture(uOcc, ouv).r*uOccRange - w.y); }
  float slope = ground > 0.5 ? smoothstep(uSnowP.x, uSnowP.y, Ng.y) : smoothstep(uSnowP2.z, uSnowP2.z + 0.3, Ng.y);   // roofs hold snow on steeper pitches than the ground
  // BT2 perf: the patch noise only within 120 m (blended to its mean from 100 m); beyond, the baked mask alone shapes the cover
  float n = 0.5;
  if (dc < 120.0 || uSnowFar == 0) n = mix(vnoise(vec3(w.xz*0.35, 41.0))*0.55 + vnoise(vec3(w.xz*1.7, 42.0))*0.3 + (dc < 40.0 ? vnoise(vec3(w.xz*7.0, 43.0))*0.15 : 0.075), 0.5, uSnowFar == 1 ? smoothstep(100.0, 120.0, dc) : 0.0);   // M5: the finest octave near only
  float dep = ground > 0.5 ? M.r : uSnowP2.x*(0.55 + 0.45*M.r);   // raised faces (roofs, wall tops, rails) hold the roof share
  float s = (slope*dep*(1.0 + (n - 0.5)*2.0*uSnowP2.y*(ground > 0.5 ? 1.0 : 0.35)) - M.g - M.b - uSnowP2.w)*vis;   // roofs: a third of the patch noise (an even cover, not blotches)
  float c = smoothstep(uSnowP.z - uSnowP.w, uSnowP.z + uSnowP.w, s);
  if (ground > 0.5) {
    float slush = clamp(max(M.g, M.b*0.9)*smoothstep(0.08, 0.45, M.r)*(1.0 - c)*vis, 0.0, 1.0);
    float grit = dc < 40.0 ? vnoise(vec3(w.xz*5.0, 44.0)) : 0.5;
    alb = mix(alb, mix(vec3(0.17, 0.165, 0.155), vec3(0.30, 0.295, 0.28), grit) + alb*0.12, slush*0.85);
    rough = mix(rough, 0.25, slush*0.85);   // slush 0.15-0.30
    float pool = clamp(M.a*(1.0 - c), 0.0, 1.0);
    alb *= mix(1.0, 0.5, pool); rough = mix(rough, 0.06, pool); N = normalize(mix(N, Ng, pool*0.9));
  }
  gSnowAlb0 = alb; gSnowR0 = rough; gSnowN0 = N;
  float g2 = dc < 25.0 ? vnoise(vec3(w.xz*21.0 + w.y*3.0, 45.0)) : 0.5;   // M5: grain near only
  vec3 sA = vec3(0.85, 0.85, 0.86)*(0.95 + 0.05*g2);   // BT2 (corpus 05 s4.4): neutral; the blue in snow is sky light in shade
  alb = mix(alb, sA, c); rough = mix(rough, 0.58, c); N = normalize(mix(N, Ng, c*0.75));   // fresh snow ~0.5-0.65
  gSnow = c;
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
uniform vec2 uAtmoHorAz;    // VO L5: horizon brightness toward / away from the sun, now relative to the authored hour (1, 1 there)
uniform float uAerial;      // VO L5: wavelength-dependent haze extinction (Rayleigh + Mie, luminance kept): distance goes blue; strength (look.aerial)
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
  { float ca = dot(normalize(d.xz + 1e-5), normalize(uSkySun.xz + 1e-5)); hor *= mix(uAtmoHorAz.y, uAtmoHorAz.x, 0.5 + 0.5*ca); }   // VO L5
  float up = max(h, 0.0);
  float g = exp(-up*3.2);                                       // 1 at the horizon -> ~0.04 at the zenith
  vec3 c = mix(zen, hor, g);
  float halo = pow(sd, 6.0)*0.22 + pow(sd, 48.0)*0.55;
  c += uSkySunCol*vec3(1.0, 0.93, 0.80)*halo*(0.35 + 0.65*g)*0.32;
  c *= 1.0 - 0.06*(1.0 - sd)*(1.0 - g);                         // anti-solar sky a touch deeper
  if (h < 0.0) c = mix(hor*0.92, gnd, clamp(-h*5.0, 0.0, 1.0));   // below the horizon (reflections only)
  c *= 1.0 + 0.6*cloudOpening(d);                                // VO L5: light through the cloud opening (fog in-scatter and reflections read this too)
  c += uSkySunCol*pow(sd, 900.0)*7.0*step(0.0, h)*uSunDisc;      // sun disc
  return c;
}
// Aerial perspective: height-falloff haze integrated along the view ray, density rho(y) = rho0 exp(-y / Hh).
// Optical depth from the eye at e to the point: rho0 * L * exp(-e.y/Hh) * (1 - exp(-dy/Hh)) / (dy/Hh).
// The in-scattered colour is the horizon sky in that direction plus a sun glow, so the land fades into the sky.
vec3 hazeColor(vec3 dir){   // W2: the haze's colour toward dir (= the sky at the horizon under w2sky)
  vec3 fd = normalize(vec3(dir.x, max(dir.y, 0.015), dir.z));
  if (uW2 == 1) { vec3 band = vec3(dot(uSkyHor, vec3(0.2126, 0.7152, 0.0722)))*vec3(0.95, 0.97, 1.0)*0.66*overcastGrad(fd.y); return mix(skyColor(fd)/(1.0 + 0.6*cloudOpening(fd)), band, uCloudK); }   // the opening lights the sky, not the haze over the ground
  return mix(uFogCol, skyColor(fd), 0.65);
}
uniform int uFroxWorld;   // CP4: 1 = the composite's froxel volume fogs the world (the per-surface air fog is skipped)
vec3 applyFogAir(vec3 col, vec3 w){
  if (uFroxWorld == 1 && uUnder < 0.5) { gFogT = 1.0; return col; }
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
  if (uW2 == 1) fc = hazeColor(dir);   // W2: distant ground fades into exactly the sky behind it at the horizon (the grey band under a deck)
  float sd = max(dot(dir, uSkySun), 0.0);
  if (uFogPhase == 1) {   // CP4 fogphase: the sun's in-scatter by a Henyey-Greenstein lobe (g of the air: 0.85 haze / fog, 0.7 dust), only with direct sun
    float g = wxFogG, cth = dot(dir, uSkySun), hg = (1.0 - g*g)/(4.0*PI*pow(max(1.0 + g*g - 2.0*g*cth, 1e-4), 1.5));
    fc += uSkySunCol*wxFogAlbedo*hg*0.035*(1.0 - uCloudK);
  } else fc += uSkySunCol*vec3(1.0, 0.92, 0.78)*pow(sd, 8.0)*0.10;
  if (uEnvFog > 0.0) {
    // WX1 environmental fog: height + distance extinction over the cached field (taken at the ray's midpoint; the drift
    // noise slides downwind), and a cheap sun glow toward the sun, dimmed by the cloud transmittance at the camera and
    // one shadow lookup at the surface. High adds the marched shafts on top (same density, kShaftFS)
    float Lw = max(length(w - uCamPos) - uEnvFogNear, 0.0);   // GR3: look.fogNear: no mist on the ground at your feet
    vec3 m = 0.5*(w + uCamPos);
    vec2 uv = (m.xz - uFogBox.xy)*uFogBox.zw;
    vec4 F = texture(uFogField, uv);
    float drift = texture(uFogField, uv + uCloudWind*uCloudT*0.0004 + 0.37).g;
    float k2 = (w.y - uCamPos.y)/uEnvFogH;
    float hf2 = abs(k2) > 1e-3 ? (1.0 - exp(-k2))/k2 : 1.0 - 0.5*k2;
    float tauE = uEnvFog*uFogIndoor*F.r*(0.6 + 0.8*drift)*Lw*exp(-max(uCamPos.y - F.b, 0.0)/uEnvFogH)*hf2;
    if (uEnvFogTop < 1e8) {   // ceiling: the share of the ray below top (each end softened over uEnvFogSoft)
      float a = smoothstep(uEnvFogTop + uEnvFogSoft, uEnvFogTop - uEnvFogSoft, uCamPos.y), b = smoothstep(uEnvFogTop + uEnvFogSoft, uEnvFogTop - uEnvFogSoft, w.y);
      float m2 = smoothstep(uEnvFogTop + uEnvFogSoft, uEnvFogTop - uEnvFogSoft, 0.5*(uCamPos.y + w.y));
      tauE *= (a + 2.0*m2 + b)*0.25;
    }
    float e; float sh = cascadePCF(uShadow, uLightVP, uShadowTexel, uShadowWorldTexel.x, w, uSunDir, 0.0, 1, e, vec2(uShadowAtlas, 0.0));
    if (e > 0.95) sh = 1.0;
    float c0 = dot(dir, uSunDir), ph = 0.3 + 2.2*pow(max(c0, 0.0), 6.0) + 0.6*pow(max(c0, 0.0), 2.0);   // forward-scattering approximation
    vec3 fcE = mix(uFogCol, skyColor(fd), 0.45)*1.05 + uSunCol*vec3(1.0, 0.95, 0.85)*ph*0.11*cloudShadow(uCamPos)*mix(0.3, 1.0, sh)*(1.0 - uCloudK);   // WX2: no sun glow under overcast
    if (uW2 == 1) { float lc = dot(uSkyHor, vec3(0.2126, 0.7152, 0.0722))*0.8, le = dot(fcE, vec3(0.2126, 0.7152, 0.0722)); if (le > lc) fcE *= lc/le; }   // W2: a fog layer is never brighter than the sky at the horizon (a veil, not snow)
    if (uFog2.x > 0.0) {   // the band: constant density between floor and top (soft edges), sampled at the eye, the midpoint and the far end
      float L2 = max(length(w - uCamPos) - uEnvFogNear, 0.0);
      #define BAND(hh) (smoothstep(uFog2.y - uFog2.w, uFog2.y + uFog2.w, hh)*(1.0 - smoothstep(uFog2.z - uFog2.w, uFog2.z + uFog2.w, hh)))
      tauE += uFog2.x*uFogIndoor*L2*(BAND(uCamPos.y) + 2.0*BAND(0.5*(uCamPos.y + w.y)) + BAND(w.y))*0.25;
      #undef BAND
    }
    float tt = tau + tauE;
    f = 1.0 - exp(-tt);
    fc = mix(fc, fcE, tauE/max(tt, 1e-6));
  }
  if (uAerial > 0.0) {   // VO L5: haze extinction by wavelength (Rayleigh + Mie, half strength), same luminance: distance cools, as in air
    vec3 fr = 1.0 - exp(-max(-log(max(1.0 - f, 1e-5)), 0.0)*mix(vec3(1.0), vec3(0.895, 1.0, 1.27), uAerial*smoothstep(70.0, 260.0, length(w - uCamPos))));   // distance only: near roofs keep their colour
    gFogT = 1.0 - f;
    return col*(1.0 - fr) + fc*fr;
  }
  gFogT = 1.0 - f;
  return mix(col, fc, f);
}
// smoke at the eye (grenade.cpp, Renderer::setLighting: uSmokeSigma 1/m, 0 outside any cloud): inside a cloud the near-faded
// sprites hand over to this term; the smoke is a grey medium lit by the sky / ground and the sun (albedo uSmokeCol)
uniform float uSmokeSigma; uniform vec3 uSmokeCol;
vec3 applyFog(vec3 col, vec3 w){
  vec3 c = applyFogAir(col, w);
  if (uSmokeSigma > 0.0 && uUnder < 0.5) {
    float t = exp(-uSmokeSigma*length(w - uCamPos));
    vec3 lit = uSmokeCol*(0.5*(uSkyCol + uGroundCol) + uSunCol*max(uSunDir.y, 0.0)/PI);
    c = mix(lit, c, t); gFogT *= t;
  }
  return c;
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
layout(std430, binding = 8) readonly buffer NmkProbes { vec4 gProbe[]; };          // 5 per probe: SH R, G, B (irradiance / pi), pos + valid, sky visibility (VO L6)
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
uniform int uCapture;                  // L2 probe capture: radiance + front-facing flag, no emission (FX1: declared here for lightTerm)
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
  if (l.misc.w > 0.5 && uCapture == 1) return;   // FX1: the player's headlamp never reaches the probe GI
  vec3 dl = l.posR.xyz - w; float d2 = dot(dl, dl);
  if (d2 >= l.posR.w*l.posR.w) return;
  float d = sqrt(d2); vec3 L = dl/max(d, 1e-4);
  float NdL = dot(N, L);
  if (NdL <= 0.0) return;
  float att = lightFalloff(l.misc.w > 0.5 ? max(d2, 1.0) : d2, l.posR.w);   // FX1: the headlamp stops brightening inside 1 m (the gun at ADS, a wall at arm's length: lit, never blown out)
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
uniform int uSkyRelit;   // VO L6 (Patry 2021): probes keep the sky's share as visibility SH, relit with the current sky colour
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
    vec4 sR = vec4(0.0), sG = vec4(0.0), sB = vec4(0.0), sV = vec4(0.0); float sw = 0.0, tw = 0.0;
    for (int k = 0; k < 8; ++k) {
      ivec3 o = ivec3(k & 1, (k >> 1) & 1, k >> 2);
      int idx = giIndex(c, uGiOrg[c].xyz + b + o);
      vec3 tri = mix(1.0 - f, f, vec3(o));
      float wt = tri.x*tri.y*tri.z;
      vec4 pp = gProbe[idx*5 + 3];
      if (pp.w < 0.5) continue;
      tw += wt;
      vec3 toP = pp.xyz - w; float dp = length(toP);
      float bf = (dot(toP/max(dp, 1e-4), Ng) + 1.0)*0.5; wt *= bf*bf + 0.05;   // probes behind the surface count less
      vec3 pd = q - pp.xyz; float r = length(pd);
      vec2 m = textureLod(uGiVis, giVisUV(idx, pd/max(r, 1e-4)), 0.0).rg;
      if (r > m.x) { float var = max(m.y - m.x*m.x, 0.0004); float dd = r - m.x; float ch = var/(var + dd*dd); wt *= ch*ch*ch; }
      if (wt < 1e-6) continue;
      sR += gProbe[idx*5]*wt; sG += gProbe[idx*5 + 1]*wt; sB += gProbe[idx*5 + 2]*wt; sV += gProbe[idx*5 + 4]*wt; sw += wt;
    }
    if (sw <= 1e-5) continue;
    float conf = clamp(tw*4.0, 0.0, 1.0)*clamp(sw/max(tw, 1e-4)*8.0, 0.0, 1.0);
    sR /= sw; sG /= sw; sB /= sw;
    vec3 e = max(vec3(sR.x + dot(sR.yzw, N), sG.x + dot(sG.yzw, N), sB.x + dot(sB.yzw, N)), vec3(0.0));
    if (uSkyRelit == 1) e += uSkyCol*max(sV.x/sw + dot(sV.yzw/sw, N), 0.0);   // VO L6: the visible sky, in today's sky colour (no capture lag)
    vec3 v = max(vec3(sR.x + dot(sR.yzw, R), sG.x + dot(sG.yzw, R), sB.x + dot(sB.yzw, R)), vec3(0.0));
    if (uSkyRelit == 1) v += uSkyCol*max(sV.x/sw + dot(sV.yzw/sw, R), 0.0);
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
float gSpecK = 1.0;   // GG1: the caller's specular scale (the terrain's dry ground: content/settings/ground_gloss.json drySpecular)
uniform vec2 uSpecAA;   // M3 (stream M): geometric specular AA, Tokuyoshi & Kaplanyan 2021: x screen-space variance, y the clamp; x = 0 off
float specAA(vec3 N, float rough){   // widens alpha where the normal changes fast across a pixel (curved edges, fine bumps)
#ifdef NMK_FS
  if (uSpecAA.x > 0.0) { vec3 du = dFdx(N), dv = dFdy(N); float a2 = rough*rough*rough*rough; rough = sqrt(sqrt(clamp(a2 + min(2.0*uSpecAA.x*(dot(du,du) + dot(dv,dv)), uSpecAA.y), 0.0, 1.0))); }
#endif
  return rough;
}
// CP step 3 (corpus 03 s4.2, s6.2, s6.3; perf switch splitsum): split-sum environment specular from Karis's analytic fit
// (no DFG table: every texture unit is taken), multi-scatter energy compensation 1 + F0 (1/E_ss - 1) with E_ss the fit at
// F0 = 1, Lagarde's specular occlusion, and multi-bounce material AO by the pixel's own albedo (Jimenez 2016)
uniform int uSplitSum;
uniform float uWetTime;   // W3: seconds, for the rain ripples (kCommon: the terrain program has no uTime)
uniform int uWetOn;   // W3 (perf switch wet): surfaces follow the weather state's wxWet / wxPuddle
// W3 rain ripples, procedural (no texture unit is free): in each 0.25 m cell a drop's ring expands and fades on its own
// clock; returns a normal offset (x, 0, z). k = intensity 0..1
vec3 rainRipple(vec2 xz, float t, float k){
  vec3 n = vec3(0.0);
  for (int i = 0; i < 2; ++i) {
    vec2 q = xz*4.0 + float(i)*vec2(0.37, 0.71), g = floor(q), f = fract(q) - 0.5;
    float h = fract(sin(dot(g, vec2(127.1, 311.7)) + float(i)*13.0)*43758.5453);
    float ph = fract(t*(0.9 + 0.4*h) + h), r = ph*0.45, d = length(f - (vec2(fract(h*7.3), fract(h*3.1)) - 0.5)*0.3);
    if (h > k) continue;   // fewer drops in light rain
    float ring = sin((d - r)*55.0)*exp(-abs(d - r)*28.0)*(1.0 - ph);
    n.xz += (f/max(d, 1e-3))*ring*0.18;
  }
  return n;
}
vec2 envBRDFApprox(float r, float NoV){
  const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022), c1 = vec4(1.0, 0.0425, 1.04, -0.04);
  vec4 q = r*c0 + c1;
  float a004 = min(q.x*q.x, exp2(-9.28*NoV))*q.x + q.y;
  return vec2(-1.04, 1.04)*a004 + q.zw;   // F = F0*x + y
}
uniform int uDbgView;   // CP0 look-dev view (Renderer::dbgView): 2 illum / 6 sky force albedo 0.18, no metal; 6 also drops the sun's direct light
uniform int uMetalMask;   // FIX6 (perf switch metalmask)
uniform int uAlbClamp;   // CP step 4 (perf switch albclamp; corpus 03 s4.4): dielectric albedo luminance kept in linear 0.02-0.9, grass >= 0.10
float gTexDensity = 0.0, gTexChk = 0.0;   // CP step 2 texel view: the densest set tap of this pixel (texels / m) and its tile checker (textures.cpp texTap)
vec3 shade(vec3 w, vec3 N, vec3 alb, float rough, float metal, float ao){
  if (uDbgView == 3) {   // CP step 2 albedo range (corpus 03 s4.5), before the clamp: dielectric red < 0.02, blue > 0.9; metal red < 0.45; metallic 0.1-0.9 yellow
    float l = dot(alb, vec3(0.2126, 0.7152, 0.0722));
    return metal > 0.1 && metal < 0.9 ? vec3(1.0, 1.0, 0.0) : metal >= 0.9 ? (l < 0.45 ? vec3(1.0, 0.0, 0.0) : vec3(l))
         : l < 0.02 ? vec3(1.0, 0.0, 0.0) : l > 0.9 ? vec3(0.0, 0.3, 1.0) : vec3(l);
  }
  if (uDbgView == 5) {   // CP step 2 texel density (corpus 07 s1.3): < 256 red, 256-512 orange, 512-1024 green, > 1024 blue, one checker per set tile; untextured grey
    vec3 k = gTexDensity <= 0.0 ? vec3(0.3) : gTexDensity < 256.0 ? vec3(0.9, 0.1, 0.1) : gTexDensity < 512.0 ? vec3(1.0, 0.55, 0.1) : gTexDensity <= 1024.0 ? vec3(0.2, 0.8, 0.25) : vec3(0.2, 0.4, 1.0);
    return k*mix(0.75, 1.0, gTexChk);
  }
  if (uMetalMask == 1 && metal > 0.0) {   // FIX6 metalmask (corpus 03 s2.4, s4.5): metallic is a mask, not a dial. Partial values (4,245 asset materials use 0.15-0.8
    // as "dull metal") snap: <= 0.4 is the paint / rust / oxide over the metal (a dielectric), >= 0.6 bare metal; bare metal is never darker than F0 0.45
    float mk = smoothstep(0.4, 0.6, metal); float l = dot(alb, vec3(0.2126, 0.7152, 0.0722));
    alb *= mix(1.0, max(l, 0.45)/max(l, 1e-3), mk); metal = mk;
  }
  if (uAlbClamp == 1 && metal < 0.5) { float al = dot(alb, vec3(0.2126, 0.7152, 0.0722)); alb *= clamp(al, 0.02, 0.9)/max(al, 1e-4); }   // hue kept, value in range
  rough = specAA(N, rough);
  if (uDbgView == 2 || uDbgView == 6) { alb = vec3(0.18); metal = 0.0; }
  vec3 V = normalize((gEyeOv.x < 1e29 ? gEyeOv : uCamPos) - w);
  vec3 L = uSunDir; vec3 H = normalize(V+L);
  float NdL = max(dot(N,L),0.0), NdV = max(dot(N,V),1e-3), NdH = max(dot(N,H),0.0);
  if (uDbgView == 4) {   // CP step 2 white furnace (corpus 03 s4.1): a uniform white sky of luminance 1, no sun, no occlusion: what the surface returns (1 = energy kept)
    vec2 ab = envBRDFApprox(rough, NdV); vec3 F0f = mix(vec3(0.04), alb, metal);
    vec3 Fs = F0f*ab.x + ab.y, ms = 1.0 + F0f*(1.0/max(ab.x + ab.y, 0.05) - 1.0);
    return (1.0 - Fs)*(1.0 - metal)*alb + Fs*ms;
  }
  float a = max(rough*rough, 0.02);
  vec3 F0 = mix(vec3(0.04), alb, metal);
  vec3 F = F_Schlick(max(dot(H,V),0.0), F0);
  vec3 spec = D_GGX(NdH,a)*G_Smith(NdV,NdL,rough)*F/(4.0*NdV*max(NdL,1e-3));
  vec3 kd = (1.0-F)*(1.0-metal);
  // shadow and sky-occlusion lookups offset along the *geometric* normal when the material bumped N (gNg): the bumped
  // normal moved the receiver offset / slope bias per pixel, which the per-frame PCF rotation turned into flicker
  vec3 Ng = dot(gNg, gNg) > 0.5 ? gNg : N;
  float sh = NdL > 0.0 ? shadowPCF(w, Ng) * cloudShadow(w) : 0.0;
  vec2 ssAB = envBRDFApprox(rough, NdV);
  vec3 msK = uSplitSum == 1 ? 1.0 + F0*(1.0/max(ssAB.x + ssAB.y, 0.05) - 1.0) : vec3(1.0);   // multi-scatter: rough metals keep their energy
  vec3 direct = (kd*alb/PI + spec*msK*gSpecK) * uSunCol * NdL * sh;
  if (uDbgView == 6) direct = vec3(0.0);
  // VO L4 micro-shadowing (Brinck et al. 2016): a crevice the material AO marks is also shadowed from the sun near the
  // terminator, so texture detail keeps its depth where the light grazes (legacy material AO never touched the sun)
  if (uMicroSh > 0.0) direct *= mix(1.0, clamp(NdL + 2.0*ao*ao - 1.0, 0.0, 1.0), uMicroSh);
  float sv = skyVis(w, Ng);
  float amb = mix(0.06, 1.0, sv);
  vec3 irr = mix(uGroundCol, uSkyCol, 0.5+0.5*N.y) * amb + uFill*(1.0-sv);
  if (uCloudOpenK > 0.0) irr *= 1.0 + 0.35*uCloudOpenK*max(dot(N, uCloudOpen.xyz), 0.0)*sv;   // VO L5: sky light from the opening's side
  // VO L1: Classic's 30 % shadow lift let a warm share of the sun into every shadow; New's deeper shadows keep that hue
  // (not its brightness) in the sky light of surfaces that face the sun but are shaded: forest shade stays warm
  if (uShadowWarm > 0.0 && NdL > 0.0) { vec3 sunHue = uSunCol/max(dot(uSunCol, vec3(0.2126, 0.7152, 0.0722)), 1e-4); irr *= mix(vec3(1.0), sunHue, uShadowWarm*(1.0 - sh)*smoothstep(0.0, 0.3, NdL)); }
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
  if (uReflOn == 1 && uCapture == 0) { float rw; vec3 pc = reflProbe(w, R, rough, rw); envL = mix(envL, pc, rw); }   // VO L7 (never inside a probe capture): what is really there, not the sky estimate
  vec3 envSpec = uSplitSum == 1 ? envL*(F0*ssAB.x + ssAB.y)*msK*cav*gSpecK   // split sum (no 0.6 fudge)
                                 : envL * Fr * (1.0-rough*0.6) * 0.6 * cav * gSpecK;
  // material AO darkens the light arriving from the hemisphere, not the direct sun (legacy: everything)
  float aoD = mix(ao, 1.0, uAoIndirectOnly);
  vec3 r;
  if (uSplitSum == 1) {
    vec3 aoMB = max(vec3(ao), ((ao*(2.0404*alb - 0.3324) + (-4.7951*alb + 0.6417))*ao + (2.7552*alb + 0.6903))*ao);   // multi-bounce, per channel by this albedo
    float so = clamp(pow(NdV + ao, exp2(-16.0*rough - 1.0)) - 1.0 + ao, 0.0, 1.0);   // specular occlusion (Lagarde 2014): once, not ao twice
    r = direct*aoD + irr*alb*kd*aoMB + envSpec*so;
  } else r = direct*aoD + (irr*alb*kd + envSpec*mix(1.0, ao, 0.7)) * ao;
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
out vec3 vW, vN, vAlb, vPc, vPn, vAp, vAn, vEmi; out vec4 vPar; flat out vec3 vEye; flat out vec4 vInstV; layout(location=11) flat out vec3 vPcF;   // MA1b: a box-face corner (edge wear)
invariant gl_Position;   // R1 zprepass: kDepthVS computes the same depth
void main(){
  vInstV = aInst.w > 1.5 ? vec4(aInst.xyz, 0.0) : uInst;
  vec4 w = uModel*vec4(aPos,1.0);
  vW = w.xyz; vN = mat3(uModel)*aNrm; vAlb = aAlb; vPar = aPar; vPc = aPc; vPn = aPn; vAp = aPos; vAn = aNrm; vEmi = aEmi; vPcF = aPc; vEye = uCamPos;
  if (uSnapAp.w > 0.5) { vAp = vec3(w.x - uSnapAp.x, aPos.y, w.z - uSnapAp.z); vAn = vN; }   // V6: patterns continue across snapped joints
  if (aInst.w > 1.2) { vAp = aLp; vAn = aLn; }   // PF2 HLOD (w = 2): world-space vertices, the copy's own pattern space; P2 merged leaves (w = 1.3): leaf-local
  gl_Position = uVP*w;
}
)GLSL";

static const char* kMainFS = R"GLSL(
in vec3 vW, vN, vAlb, vPc, vPn, vAp, vAn, vEmi; in vec4 vPar; flat in vec3 vEye;
uniform vec4 uTint; uniform float uTime; uniform int uWeather;   // 1: world structures get localized weathering
uniform float uMapArid;   // NT4: look.arid (desert): sand-filled cracks
uniform float uMapCave;   // CV1: look.cave: rock without sunlit lichen; calcite flowstone, manganese stains, wet drip streaks
uniform float uMapGrime;   // NT3: an abandoned map's grime (look.grime): ruin walls streaked, roads broken up
uniform float uGlow, uEmissiveK;   // L2 emission: per draw (DrawItem::glow) and global multipliers
uniform int uNoWallRelief;   // A/B: 1 = rough-wall relief off (Renderer::wallRelief = 0)
// uMatLayer (GX5 layered materials: 0 off (Low, probe captures), 1 on, 2 High) is declared with the L4 hooks (textures.cpp)
struct SDec { vec4 a, b, c; };   // GX5 static decals (static_decals.h): centre + type, normal + seed, half size + strength + variant
layout(std430, binding = 10) readonly buffer NmkStaticDecals { SDec gSD[]; };
uniform vec4 uRoomDec[16]; uniform vec4 uExtDec; uniform int uSDecOn; uniform float uSDecR;   // UL1: q.decalRange
uniform int uGxDebug;   // NMK_GX5_DEBUG=1: red = interior room found (green = past 16), blue = decals in its list
uniform ivec2 uRoomX;   // GX5: rooms past the 16 in uniforms, as records in gSD (a = room A, b = room B, c = decal range): first, count
layout(location=0) out vec4 oColor;
layout(location=1) out float oDist;   // eye distance, only stored during the first-person pass (depth of field)
layout(location=2) out vec4 oAmb;     // indirect light (screen-space AO), world pass only
layout(location=3) out vec4 oNR;      // world normal + roughness (screen-space reflections), world pass only
layout(location=4) out vec4 oVel;     // motion: static geometry leaves a = 0 (TAA reprojects it from depth)
uniform float uVmVel;                // 1 while the first-person weapon is drawn
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
uniform vec4 uContactBox; uniform vec4 uContact[4]; uniform int uFoundWx; uniform vec4 uMossHab;   // BF B1: the copy's habitat (water nearness, shelter 1 - sunHours/6, exclusion, canopy; moss.cpp)   // MA2: ground-contact grid (App::contactProfile)
// MA2: height above the LOCAL ground: the copy's terrain grid under its walls (bilinear), or the asset ground (y 0)
float groundH(){
  if (uFoundWx == 0) return vAp.y;
  vec2 t = clamp((vAp.xz - uContactBox.xy)/max(uContactBox.zw - uContactBox.xy, vec2(1e-3)), 0.0, 1.0)*3.0;
  ivec2 i = ivec2(min(floor(t), vec2(2.0))); vec2 f = t - vec2(i);
  return vAp.y - mix(mix(uContact[i.y][i.x], uContact[i.y][i.x + 1], f.x), mix(uContact[i.y + 1][i.x], uContact[i.y + 1][i.x + 1], f.x), f.y);
}
// MA2: 0 at a footprint corner (the contact box less its 0.4 m margin), metres away from it otherwise
float cornerDist(){
  vec2 c = (uContactBox.xy + uContactBox.zw)*0.5, he = (uContactBox.zw - uContactBox.xy)*0.5 - 0.4;
  return length(he - abs(vAp.xz - c));
}
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
float gFootDbg = -1.0;   // MA2 debug 3: 1 under 0.5 m above the local ground on a foundation surface, -1 = not one
vec3 gWxDbg = vec3(0.0);   // MA2 debug 4: (damp, moss, splash) masks
float gMossW = 0.0;        // BF B1: moss weight x thickness here (the transmission lobe after shade())
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
uniform int uGrafLayer1;   // the graffiti atlas's layer in uTexCol + 1 (slot decal_graffiti); 0 = none, also where it is never set (probe captures)
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
    } else if (type == 8) { // graffiti: one tag of the GraffitiSet001 atlas (ambientCG, CC0; 4 x 4 tags, opacity in alpha), over the wall's own material
      if (uGrafLayer1 <= 0) continue;
      vec2 cu = vec2(u.x*0.5 + 0.5, q.y/(2.0*hs.x) + 0.5);   // a square cell as wide as the decal
      if (cu.y < 0.0 || cu.y > 1.0) continue;
      int tile = int(sd*16.0) & 15;
      vec2 tuv = (vec2(float(tile & 3), float(3 - (tile >> 2))) + clamp(cu, 0.004, 0.996))*0.25;   // row 0 = the image's top
      vec4 tg = textureLod(uTexCol, vec3(tuv, float(uGrafLayer1 - 1)), max(0.0, log2(gPix*256.0/(2.0*hs.x))));   // inside a divergent loop: no implicit derivatives
      float m = clamp(tg.a, 0.0, 1.0)*k*(0.7 + 0.3*vnoise(vec3(q*3.0, sd*5.0)));   // weathered: patchy
      alb = mix(alb, tg.rgb*0.85, m); rough = mix(rough, 0.75, m*0.6);   // paint only: the wall's relief stays
    } else if (type == 9) { // NT3 torn poster: paper with a coloured header, an image block, text lines; ragged, sun-faded
      float tear = fbm(vec3(u*4.0, sd*5.0));
      float m = (1.0 - smoothstep(0.84, 0.92, abs(u.x) + 0.18*(tear - 0.5)))*(1.0 - smoothstep(0.84, 0.92, abs(u.y) + 0.18*(tear - 0.5)));
      m *= 1.0 - step(0.6, u.x + u.y*0.8 + 0.4*tear)*step(0.5, sd);   // a corner torn away on half of them
      int vr = int(D.c.w + 0.5);
      vec3 col = vr == 0 ? vec3(0.6, 0.16, 0.12) : vr == 1 ? vec3(0.16, 0.24, 0.42) : vr == 2 ? vec3(0.7, 0.55, 0.15) : vec3(0.1, 0.1, 0.1);
      vec3 paper = vec3(0.74, 0.71, 0.62);
      vec3 pc = u.y > 0.55 ? col : (abs(u.x) < 0.7 && u.y > -0.35 && u.y < 0.45) ? vec3(0.3 + 0.35*fbm(vec3(u*3.0, sd*9.0))) : paper;
      pc = mix(pc, vec3(0.2), (u.y < -0.45 && abs(u.x) < 0.7 ? 1.0 : 0.0)*step(0.5, fract(u.y*9.0))*0.5);
      pc = mix(pc, paper*0.9, 0.3 + 0.2*vnoise(vec3(q*6.0, sd)));   // faded
      alb = mix(alb, pc, m*k); rough = mix(rough, 0.92, m*k);
      gDecH += 0.0008*m*k;
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
uniform float uMatFar;   // PERF1 matfar: > 0 = from this distance (m) to 1.4x it the wear / grime layer, static decals and relief fade out; 0 = off
void main(){
  if (uCapture == 1) gEyeOv = vEye;
  gPix = max(length(dFdx(vW)), length(dFdy(vW)));
  float farK = uMatFar > 0.0 && uCapture == 0 ? smoothstep(uMatFar, uMatFar * 1.4, length(vW - uCamPos)) : 0.0;
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
    // MA2: the foot and the packed soil are in the shared weathering block (L4 replace slots skip this body)
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
    rough = clamp(rough + joint * 0.06, 0.3, 1.0);
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
      int fsl = uImgMat == 1 ? uFloorSlot[clamp(code, 0, 7)] : -1;   // MA1 imgmat: the finish's image set replaces its pattern
      if (fsl >= 0 && texReplace(fsl)) {   // base colour per finish (boards, parquet, tomettes, flags, earth); the set brings the structure
        vec3 fcol[5] = vec3[5](vec3(0.24, 0.16, 0.095), vec3(0.24, 0.15, 0.085), vec3(0.42, 0.21, 0.12), vec3(0.32, 0.30, 0.26), vec3(0.30, 0.27, 0.22));
        c = fcol[clamp(code, 0, 4)]*(0.9 + 0.2*hash12(vec2(s, 5.0))); tslot = fsl;
      } else if (code == 1) {   // herringbone parquet: 7 x 35 cm oak blocks
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
      } else if (code == 5) {   // MOD1: large-format porcelain tiles, 60 cm, thin light grout (modern buildings)
        vec2 tid = floor(q / 0.6), tf = fract(q / 0.6);
        float e = min(min(tf.x, 1.0 - tf.x), min(tf.y, 1.0 - tf.y)) * 0.6;
        float joint = 1.0 - smoothstep(0.0015, 0.004, e);
        c = vec3(0.58, 0.57, 0.54) * (0.95 + 0.07 * hash12(tid + s)) * (0.96 + 0.06 * fbmL(vec3(q * 1.2, tid.x), 1.2));
        c = mix(c, vec3(0.42, 0.41, 0.39), joint);
        hh = (1.0 - joint) * 0.9; r = mix(0.28 + 0.1 * traffic, 0.7, joint); aoF = 1.0 - 0.15 * joint;
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
      float lith = grain == 2 ? 1.0 : 0.0; bool drip = grain == 3;   // CV1: dripstone: limestone tones, no bedding or joints, vertical fluting
      float warp = fbmL(vec3(p.xz*0.06, p.y*0.02 + 42.0), 0.06)*5.0;
      float bedY = p.y*0.42*2.0 + warp;                                                         // boulder scale: ~1.2 m beds
      float bedV = fract(sin(floor(bedY)*91.7)*4375.85);
      bedLine = 1.0 - smoothstep(0.0, 0.07, min(fract(bedY), 1.0 - fract(bedY)));
      float thin = smoothstep(0.82, 0.97, sin((p.y + warp*0.4)*7.5)*0.5 + 0.5);
      float jx = (p.x*0.62 + p.z*0.78)*0.58 + (vnL(vec3(p.xz*0.1, p.y*0.14 + 43.0), 0.1) - 0.5)*0.7;
      crack = (1.0 - smoothstep(0.012, 0.04, abs(fract(jx) - 0.5))) * step(0.45, vnoise(vec3(floor(jx)*1.7, floor(bedY*0.5)*1.3, 52.0)));
      if (drip) { bedLine = 0.0; crack = 0.0; thin = 0.0; bedV = 0.5 + 0.5*mott; }
      vec3 lime = mix(vec3(0.44, 0.40, 0.33), vec3(0.57, 0.52, 0.43), bedV) * (0.74 + 0.42*mott);
      if (drip) lime *= 0.82 + 0.3*vnL(vec3(p.x*2.6, p.y*0.12, p.z*2.6) + 88.0, 2.6);   // flutes run down the form
      vec3 schist = mix(vec3(0.24, 0.23, 0.21), vec3(0.34, 0.31, 0.27), bedV) * (0.78 + 0.36*mott) * (1.0 - 0.25*thin*lith);
      rock = mix(lime, schist, lith) * (0.9 + 0.2*grn);
      rock *= 1.0 - 0.4*bedLine - 0.42*crack;
      rock = mix(rock, vec3(0.30, 0.22, 0.15), 0.25*smoothstep(0.55, 0.8, fbmL(p*0.25 + 46.0, 0.25)));   // iron staining
      h = grn*0.4 + mott*0.4 - bedLine*0.5 - crack*0.6 + thin*lith*0.3; bump = 0.018;
    }
    // weathering at hand scale: blotchy tone, solution pits / vesicles, fine grit relief
    float grit = 0.5, lichen = 0.0;
    if (uMapCave < 0.5 || length(vW - uCamPos) < 40.0) {   // CV1: on cave maps the hand-scale weathering only near (lids / stalactites far overhead)
    float blot = vnL(p*1.7 + 70.0, 1.7), pit = smoothstep(0.72, 0.88, vnL(p*7.0 + 71.0, 7.0)); grit = vnL(p*11.0 + 72.0, 11.0);
    rock *= (0.80 + 0.40*blot) * (1.0 - 0.32*pit) * (0.92 + 0.16*grit);
    h += grit*0.35 - pit*0.6; bump = max(bump, 0.035);
    if (uMoss.x < 0.5) {   // BF B1: replaced by the moss field below when mossfield is on
    lichen = smoothstep(0.62, 0.8, vnL(p*0.9 + 47.0, 0.9)) * smoothstep(0.1, 0.7, up + 0.25);
    rock = mix(rock, mix(vec3(0.16, 0.19, 0.09), vec3(0.42, 0.41, 0.30), grn), lichen*0.55);     // moss / grey lichen
    rock = mix(rock, vec3(0.55, 0.50, 0.26), smoothstep(0.78, 0.86, vnL(p*5.5 + 61.0, 5.5))*smoothstep(0.0, 0.6, up)*0.45);   // crusty yellow lichen
    float foot = 1.0 - smoothstep(-0.05, 0.25 + 0.25*vnL(vAp*1.7, 1.7), vAp.y);
    rock = mix(rock, vec3(0.12, 0.15, 0.06), foot*smoothstep(0.45, 0.7, vnL(vAp*2.3 + 7.0, 2.3))*0.6) * mix(1.0, 0.75, foot);   // damp foot
    }
    } else rock *= 0.92;
    alb = rock * vAlb * 0.8; rough = 0.86 + 0.08*grn; ao *= 1.0 - 0.3*crack;   // 0.8: matches the cliff faces' in-game brightness
    if (uMoss.x > 0.5 && uMapCave < 0.5) {   // BF B1 mossfield on boulders: moss low, wet and shaded; lichen high, dry, sun-facing (one field)
      vec4 hb = uFoundWx == 1 ? uMossHab : vec4(0.3, 0.4, 0.0, 0.3);
      float dCam = length(vW - uCamPos), y = max(vAp.y, 0.0);
      float mo = mossMoist(y, 0.0, clamp(0.5 - h, 0.0, 1.0)*0.8 + 0.5*crack, 8.0*(1.0 - hb.x));
      float shl = mossShelter(hb.y, normalize(vN), vec3(0.0, 0.0, 1.0));
      float por = grain == 1 || grain == 3 ? 1.0 : grain == 0 ? 0.75 : 0.65;
      vec3 mf = mossField(vW, up, (1.0 - abs(up))*0.7, 1.0, mo, shl, por, 0.5*hb.w*(1.0 - smoothstep(0.0, 0.4, y)), 0.0, hb.z, 0.0, 0.0, 1.0 - smoothstep(12.0, 30.0, dCam));
      float hMoss = 0.85 + 0.15*mossVn(vW/0.05), hHost = clamp(h, 0.0, 1.0);   // joints and pits (low h) take moss first
      float wm = dCam < uMoss.z ? smoothstep(-0.10, 0.10, (hMoss + mf.x) - (hHost + 1.0 - mf.x)) * step(0.02, mf.x) : mf.x*0.85;
      alb = mix(alb, mix(kMossAlb, kMossDry, 1.0 - mo)*(0.8 + 0.4*mossVn(vW/0.12 + 5.0))*mix(0.55, 1.0, hMoss), wm); rough = mix(rough, 0.88, wm);
      vec3 lc = mix(kLichenAlb, vec3(0.42, 0.37, 0.16), smoothstep(0.6, 0.8, mossVn(vW/0.25 + 61.0)));   // grey crust, a few yellow
      alb = mix(alb, lc*(0.9 + 0.2*grn), mf.z*0.8); rough = mix(rough, 0.92, mf.z);
      gMossW = wm*mf.x;
    }
    if (uMapCave > 0.5 && length(vW - uCamPos) < 45.0) {   // CV1 cave rock (near only: sub-pixel beyond): no lichen (re-darken it), flowstone and stains run down the faces, wet streaks shine
      float lichAll = clamp(lichen*0.55 + smoothstep(0.78, 0.86, vnL(p*5.5 + 61.0, 5.5))*smoothstep(0.0, 0.6, up)*0.45, 0.0, 1.0);
      alb = mix(alb, alb*0.55, lichAll) * (1.0 + 0.3*bedLine);   // bedding lines faint: dripstone, not masonry
      float drip = vnL(vec3(p.x*1.6, p.y*0.12, p.z*1.6) + 90.0, 1.6);                              // vertically stretched: runs down the face
      float flow = smoothstep(0.62, 0.8, drip) * smoothstep(0.55, 0.75, fbmL(p*0.35 + 91.0, 0.35));
      alb = mix(alb, vec3(0.62, 0.60, 0.55)*(0.85 + 0.3*grn), flow*0.55);                           // calcite flowstone, pale and waxy
      float mang = smoothstep(0.6, 0.85, fbmL(vec3(p.x*0.5, p.y*0.18, p.z*0.5) + 93.0, 0.5));
      alb *= 1.0 - 0.45*mang;                                                                       // manganese / soot-dark stains
      float wet = smoothstep(0.42, 0.7, vnL(vec3(p.x*2.4, p.y*0.2, p.z*2.4) + 95.0, 2.4)) * (0.55 + 0.45*(1.0 - up*up));
      alb *= 1.0 - 0.3*wet; rough = mix(rough, 0.16 + 0.2*grit, max(wet, flow*0.6));                 // dark wet sheen: the lights' highlights
    }
  } else {
    alb *= 0.94 + 0.08*vnL(vAp*7.0, 7.0);
  }
  gTexWear = uInst.y; gTexSeed = uInst.x;   // MA1: worn copies show more substrate; TX6: the grunge mask offset
  texMaterial(tslot, vW, vN, vAp, vAn, vPc, vPn, grain, gPix, alb, rough, ao);   // L4: ambientCG layer under the weathering (textures.cpp)
  {   // PERF1 matfar: both GX5 layers fade out over the far band and are skipped beyond it
    vec3 a0 = alb; float r0 = rough, o0 = ao;
    if (uMatLayer > 0 && farK < 1.0) gxLayer(pat, normalize(vAn), alb, rough, ao);   // GX5 wear / grime layer, roughness breakup, seam occlusion
    if (uSDecOn == 1 && farK < 1.0 && (uSDecR <= 0.0 || length(vW - uCamPos) < uSDecR) && (pat == 20 || (pat == 21 && gRoom >= 0) || (gIn == 0 && pat != 21 && pat != 23 && pat < 25 && uInst.w > 0.5))) sdecals(normalize(vAn), alb, rough, ao);   // GX5 static decals
    if (farK > 0.0) { alb = mix(alb, a0, farK); rough = mix(rough, r0, farK); ao = mix(ao, o0, farK); gSeam = mix(gSeam, 1.0, farK); }
  }
  if (uMapGrime > 0.0 && uWeather == 1 && gIn == 0 && pat != 20 && pat != 21 && farK < 1.0) {   // NT3: abandoned map (look.grime)
    vec3 nW = normalize(vN);
    float gk = uMapGrime*(1.0 - farK);
    float vertG = 1.0 - smoothstep(0.3, 0.6, abs(nW.y));
    if (vertG > 0.0 && uInst.y > 1.2 && rough >= 0.5 && pat != 6 && pat != 7) {   // ruin walls: run-off and rust streaks, a dirty lower storey
      float along = dot(vAp.xz, vec2(-nW.z, nW.x));
      float sdG = fract(uInst.x)*31.0;
      float run = smoothstep(0.55, 0.8, vnoise(vec3(along*1.3, 3.7, sdG))) * smoothstep(0.3, 0.7, vnoise(vec3(along*11.0, vAp.y*0.18, 1.3)));
      float rust = smoothstep(0.84, 0.96, vnoise(vec3(along*0.9, 7.1, sdG*0.5))) * smoothstep(0.35, 0.65, vnoise(vec3(along*14.0, vAp.y*0.25, 2.1)));
      float lower = 1.0 - smoothstep(0.8, 4.0 + 1.5*vnoise(vec3(along*0.4, 5.0, sdG)), vAp.y);
      alb *= mix(vec3(1.0), vec3(0.56, 0.55, 0.5), clamp(run*0.8*gk, 0.0, 0.8)*vertG);
      alb = mix(alb, alb*vec3(0.8, 0.52, 0.32), clamp(rust*0.85*gk, 0.0, 0.85)*vertG);
      alb *= 1.0 - 0.3*lower*gk*vertG;
      rough = min(1.0, rough + 0.05*run*gk);
    }
    float upG = smoothstep(0.85, 0.95, nW.y);
    if (upG > 0.0 && (pat == 10 || pat == 11)) {   // roads, pavements, markings: broken up in world space
      vec2 w = vW.xz;
      alb *= mix(1.0, mix(0.66, 1.22, smoothstep(0.25, 0.75, fbm2(vec3(w*0.07, 3.0)))), gk);   // ~15 m tone variation (reads at eye level)
      vec2 cs = vec2(3.7, 5.3); vec2 cell = floor(w/cs), fc = fract(w/cs);
      float ph = hash12(cell*1.31 + 7.0);
      float pch = step(0.8, ph)*step(abs(fc.x - 0.5), 0.36)*step(abs(fc.y - 0.5), 0.41);   // repair patches
      alb = mix(alb, alb*(ph > 0.9 ? 0.7 : 1.16), pch*0.8*gk);
      vec2 ww = w + 1.6*vec2(fbm2(vec3(w*0.21, 1.0)), fbm2(vec3(w*0.21, 7.0)));   // NT4: warped, irregular, finer
      float cn = fbm2(vec3(ww*0.75, 11.0)), cn2 = fbm2(vec3(ww*1.9 + 4.0, 2.0));
      float cmask = smoothstep(0.52, 0.72, fbm2(vec3(w*0.06, 3.0)));   // cracked in patches, sound asphalt between
      float crack = (1.0 - smoothstep(0.0, 0.005 + gPix*0.4, abs(cn - 0.5)))*cmask;
      crack = max(crack, (1.0 - smoothstep(0.0, 0.004 + gPix*0.4, abs(cn2 - 0.5)))*cmask*smoothstep(0.5, 0.65, vnoise(vec3(w*0.2, 1.0))));
      float mossC = (1.0 - smoothstep(0.0, 0.05, abs(cn - 0.5)))*(1.0 - crack);
      alb = mix(alb, uMapArid > 0.5 ? mix(alb, vec3(0.46, 0.41, 0.32), 0.7) : alb*0.45, crack*(uMapArid > 0.5 ? 0.5 : 0.75)*gk);   // sand-filled (subtle) on arid maps
      alb = mix(alb, vec3(0.16, 0.2, 0.08), mossC*0.5*gk);
      float drift = smoothstep(0.55, 0.8, fbm2(vec3(w*0.25, 21.0)));                   // dirt and leaf drifts
      float leaves = smoothstep(0.55, 0.75, vnoise(vec3(w*3.0, 5.0)));
      alb = mix(alb, uMapArid > 0.5 ? vec3(0.6, 0.53, 0.39)*(0.9 + 0.2*leaves) : mix(vec3(0.24, 0.19, 0.13), vec3(0.33, 0.2, 0.08), leaves), drift*(uMapArid > 0.5 ? 0.75 : 0.55)*gk*upG);   // arid: blown sand
      if (pat == 10) alb = mix(alb, vec3(0.3, 0.29, 0.27), smoothstep(0.35, 0.7, vnoise(vec3(w*0.6, 9.0)))*0.75*gk);   // faded, broken markings
      rough = mix(rough, 1.0, drift*0.3*gk);
    }
  }
  if (uWeather == 1 && (pat == 1 || pat == 2 || pat == 3 || pat == 9 || pat == 11 || pat == 15 || pat == 17 || pat == 18 || pat == 19 || pat == 22)) {
    // localized weathering in asset space (origin at the ground): rising damp and splash at the foot of vertical
    // faces, moss patches in that damp band, faint run-off streaks higher up. Not a uniform dirt overlay.
    // MA2: all of it by height above the LOCAL ground (groundH: the terrain under this wall), so on a slope the band
    // follows the ground instead of a level stripe; heights and strength vary by material, exposure and the copy's seed
    vec3 nW = normalize(vN);
    float vert = 1.0 - smoothstep(0.4, 0.7, abs(nW.y));
    float nn = vnL(vAp*1.3, 1.3);
    float hg = groundH();
    float sd = fract(dot(uModel[3].xz, vec2(0.131, 0.217)));   // the copy's seed
    float shadeF = uFoundWx == 1 ? clamp(0.5 - 0.5*dot(normalize(nW.xz + 1e-4), normalize(uSunDir.xz + 1e-4)), 0.0, 1.0) : 0.5;   // faces turned from the sun stay damp
    float cornerK = uFoundWx == 1 ? 1.0 - smoothstep(0.0, 1.2, cornerDist()) : 0.0;   // dirt and damp climb at the corners
    // MA2 review: has to read at 10-30 m. A damp zone with a visible, irregular top edge (0.2-0.9 m, broken by clean
    // stretches at a 2-6 m scale with the seed), soil splash at the ground, algae on shaded faces and corners, run-off
    // under ground-storey sills; tinted by material; darkening capped at ~35 % on screen (no near-black, no uniform band).
    // Lives here, not in the pattern bodies: L4 replace slots (MA1) skip those, concrete 11 / 15 included.
    vec3 alb0 = alb;
    int mc = pat == 11 || pat == 15 ? 3 : pat == 9 || pat == 18 ? 2 : pat == 1 || pat == 19 || pat == 22 ? 1 : 0;   // render / wood, stone, brick, concrete
    float matK = mc == 2 ? 0.9 : 1.0;   // brick sheds water better than render; QS1 / CP step 6: dressed stone weathers like its wall (was 0.95: kept cleaner than it)
    vec3 dampC = mc == 0 ? vec3(0.45, 0.34, 0.22) : mc == 1 ? vec3(0.50, 0.50, 0.46) : mc == 2 ? vec3(0.40, 0.33, 0.28) : vec3(0.33, 0.35, 0.38);   // brown soil, dark wet stone (GR2: was green-grey, a continuous green band), dark wet
    // GR2: the grunge dirt channel along the wall (1 = dirty): damp and moss come in patches, not as one band
    float gD = uGrungeK > 0.0 ? textureLod(uGrunge, vec2(dot(vAp.xz, vec2(0.7071)), vAp.y)/3.0 + vec2(sd*5.3, sd*2.9), max(0.0, log2(max(gPix*1024.0/3.0, 1e-4)))).g : 1.0;
    float gPatch = mix(1.0, smoothstep(0.12, 0.55, gD), 0.75*uGrungeK);
    float dug = vert * step(-0.05, vAp.y);   // GX1: nothing below the asset's ground line (dug-in trench timber)
    // value noise sits near 0.5: remap so the zone really spans its range along the wall
    float lv = smoothstep(0.25, 0.75, vnL(vec3(vAp.x*0.22, sd*17.0, vAp.z*0.22), 0.22));   // zone height along the wall, ~4 m
    float lc = vnL(vec3(vAp.x*0.3 + sd*7.0, 4.0, vAp.z*0.3), 0.3);         // clean stretches, ~3 m
    float edge = vnL(vAp*2.1 + sd*9.0, 2.1);                                // irregular top edge, ~0.5 m
    float on = max(smoothstep(0.3, 0.38, lc), cornerK);
    float zh = clamp((0.35 + 0.65*lv + 0.3*(edge - 0.5) + 0.3*cornerK + 0.12*shadeF)*matK, 0.2, 1.2);
    float k = 0.85 + 0.1*shadeF + 0.05*sd;
    float damp = (1.0 - smoothstep(zh - 0.12, zh + 0.04, hg)) * dug * on * gPatch;
    float tide = smoothstep(zh - 0.2, zh - 0.05, hg) * damp;                // the darker tide line at the top of the damp
    alb *= mix(vec3(1.0), dampC, min(damp*k + tide*0.2, 1.0));
    rough = mix(rough, rough*0.94, damp*k);   // only a little: a shinier foot reflects the sky at grazing views and cancels the darkening
    gFootDbg = step(hg, 0.5);   // debug 3: white below 0.5 m above the local ground (selftest_visuals reads this line)
    float g1 = (1.0 - smoothstep(0.0, 0.35 + 0.15*nn, hg)) * dug * max(on, 0.5);   // always some grime right at the ground
    alb *= mix(1.0, 0.75, g1);
    float sn = vnL(vAp*23.0 + 3.0, 23.0);
    float spl = (1.0 - smoothstep(0.02, 0.14 + 0.16*nn, hg)) * dug * (0.55 + 0.45*smoothstep(0.4, 0.7, sn));   // soil splash
    alb = mix(alb, (mc == 3 ? vec3(0.27, 0.24, 0.20) : vec3(0.36, 0.29, 0.21))*(0.85 + 0.3*nn), spl*0.6);
    // small exposed-substrate patches just above the ground: the finish is gone and the core shows, darker and rough
    float sub = uFoundWx == 1 ? (1.0 - smoothstep(0.1, 0.45 + 0.2*nn, hg)) * dug * smoothstep(0.78, 0.86, sn*0.4 + nn*0.6 + 0.08*cornerK) * (pat == 17 || pat == 3 || pat == 11 ? 1.0 : 0.5) : 0.0;
    alb = mix(alb, alb*vec3(0.6, 0.56, 0.5), sub); rough = mix(rough, 0.96, sub);
    float moss = uMoss.x > 0.5 ? 0.0 : dug * on * mix(1.0, smoothstep(0.35, 0.75, gD), 0.85*uGrungeK) * (1.0 - smoothstep(0.0, zh*0.8 + 0.25*cornerK, hg)) * smoothstep(0.52 - 0.15*shadeF - 0.15*cornerK, 0.66, vnL(vAp*1.6 + sd*11.0, 1.6)) * (0.2 + 0.8*shadeF) * (mc == 1 ? 1.0 : mc == 3 ? 0.8 : 0.6);
    alb = mix(alb, (mc == 0 ? vec3(0.36, 0.38, 0.24) : vec3(0.30, 0.34, 0.22))*(0.8 + 0.4*vnL(vAp*9.0, 9.0)), clamp(moss, 0.0, 1.0)*0.7);
    rough = mix(rough, 0.95, moss*0.5);
    if (uSills > 0 && vert > 0.5 && hg < 3.4) {   // run-off under the ground-storey sills
      float ro = sillRunoff(vAp) * (1.0 - smoothstep(2.6, 3.4, hg));
      alb *= mix(vec3(1.0), dampC, ro*0.8);
    }
    gWxDbg = vec3(damp*k, moss, spl);
    float streak = smoothstep(0.66, 0.92, vnoise(vec3(dot(vAp.xz, vec2(3.1, 2.7)), vAp.y*0.22, 2.0))) * vert * smoothstep(0.6, 2.0, hg);
    alb *= 1.0 - 0.14*streak;
    if (dot(vPn, vPn) > 2.0 && abs(vPn.y) < 1.0 && vert > 0.5) {   // GR2: run-off below the top edge of box faces (copings, sills, eaves): a few
      // irregular streaks fading down ~1 m, not every metre
      float dTop = abs(vPcF.y) - vPc.y;
      float al = dot(vAp.xz, normalize(vec2(-nW.z, nW.x) + 1e-4));
      float rcol = smoothstep(0.6, 0.88, vnoise(vec3(al*3.3, dTop*0.5, sd*13.0 + 4.0))) * smoothstep(0.35, 0.65, vnoise(vec3(al*0.7, 1.7, sd*7.0)));
      float ro2 = rcol * (1.0 - smoothstep(0.02, 0.6 + 0.6*vnoise(vec3(al*1.9, 3.3, sd)), dTop)) * step(0.0, dTop);
      alb *= mix(vec3(1.0), dampC*vec3(0.95, 0.95, 1.0), ro2*0.55); rough = mix(rough, rough*0.9, ro2);
    }
    float l0 = dot(alb0, vec3(0.3, 0.59, 0.11)), l1 = dot(alb, vec3(0.3, 0.59, 0.11));
    if (l1 < 0.4*l0) alb *= 0.4*l0/max(l1, 1e-4);   // the cap: 0.4 linear = ~35 % darker on screen (sRGB), never near-black
    if (uMoss.x > 0.5 && uFoundWx == 1) {   // BF B1 mossfield: the 4.5 field on walls (after the cap: moss is a material, not a darkening)
      float drip = uSills > 0 && vert > 0.5 && hg < 3.4 ? sillRunoff(vAp)*(1.0 - smoothstep(2.6, 3.4, hg)) : 0.0;
      float cav = 0.6*cornerK;   // shortcut: corner proximity stands in for cavity until PF1's gCav lands
      float por = mc == 1 ? 0.95 : mc == 2 ? 0.70 : mc == 3 ? 0.75 : 0.45;   // stone / lime mortar, brick, concrete, render (3.2)
      float dCam = length(vW - uCamPos);
      float mo = mossMoist(hg, drip, cav, 8.0*(1.0 - uMossHab.x));
      float shl = mossShelter(uMossHab.y, nW, vec3(0.0, 0.0, 1.0));
      vec3 mf = mossField(vW, nW.y, vert*dug*(0.55 + 0.45*cav), dug, mo, shl, por, 0.5*uMossHab.w*(1.0 - smoothstep(0.0, 0.4, hg)), 0.0, uMossHab.z, 0.0, 0.0, 1.0 - smoothstep(12.0, 30.0, dCam));
      float hMoss = 0.85 + 0.15*mossVn(vW/0.05), hHost = clamp(0.5 + gTexH/0.03, 0.0, 1.0);
      // mat: the HOST's height drives the blend, so moss fills the joints first (d = 0.045); film past uMoss.z
      float wm = dCam < uMoss.z ? smoothstep(-0.045, 0.045, (hMoss + mf.x) - (hHost + 1.0 - mf.x)) * step(0.02, mf.x) : mf.x*0.85;
      vec3 mc0 = mix(kMossAlb, kMossDry, 1.0 - mo)*(0.8 + 0.4*mossVn(vW/0.12 + 5.0))*mix(0.55, 1.0, hMoss);
      alb = mix(alb, mc0, wm); rough = mix(rough, 0.88, wm);
      alb = mix(alb, kLichenAlb*(0.9 + 0.2*mossVn(vW/0.03)), mf.z*0.8); rough = mix(rough, 0.92, mf.z);
      gMossW = wm*mf.x; gWxDbg.y = mf.x;
    }
    if (pat == 15) {   // earth packed against the bunker concrete (a different surface, so past the cap)
      float soil = (1.0 - smoothstep(0.0, 0.18 + 0.22*vnL(vAp*4.3 + 2.0, 4.3), hg)) * vert * float(uFoundWx);
      alb = mix(alb, vec3(0.2, 0.17, 0.12)*(0.8 + 0.4*nn), soil*0.8); rough = mix(rough, 0.97, soil);
    }
  }
  vec3 N = dot(vN, vN) > 1e-12 ? normalize(vN) : vec3(0.0, 1.0, 0.0);
  if (!gl_FrontFacing) N = -N;
  if (pat >= 17 && pat <= 22 && pat != 21) gNg = N;   // rough walls: relief ambient term in shade()
  if (uNoWallRelief == 1 && pat >= 17 && pat <= 22 && pat != 21) { bump = 0.0; gCavity = 1.0; gTexH = 0.0; gTexBump = false; gTexDN = vec3(0.0); }
  else gCavity = 1.0;
  if (farK > 0.0) {   // PERF1 matfar: relief and the texture normal fade out; past the band no derivative bump at all
    float kf = 1.0 - farK; bump *= kf; gTexH *= kf; gDetH *= kf; gDecH *= kf; gTexDN *= kf;
    if (farK >= 1.0) { bump = 0.0; gTexBump = false; gDetH = 0.0; gDecH = 0.0; }
  }
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
  if (uSnowOn == 1 && gl_FrontFacing && length(vW - uCamPos) > 1.2) { applySnow(vW, normalize(vN), 0.0, alb, rough, N); metal *= 1.0 - gSnow; }   // SN1 caps (not the weapon in hand)
  if (uWetOn == 1 && wxWet > 0.0 && gl_FrontFacing) {   // W3: rain on walls, roofs and props: only where the sky reaches (shelter stays dry), porous (rough) dielectrics darken most
    vec3 ng0 = normalize(vN); float w = clamp(wxWet, 0.0, 1.0)*skyVis(vW, ng0)*mix(0.55, 1.0, clamp(ng0.y, 0.0, 1.0));
    float poro = (1.0 - metal)*smoothstep(0.35, 0.85, rough);
    alb *= mix(1.0, 0.35, w*poro); rough = mix(rough, max(0.35*rough, 0.25), w);
  }
  vec3 c = shade(vW, N, alb, rough, metal, ao);
  if (gMossW > 0.0 && uMoss.y > 0.5) c += mossTransmit(N, uSunDir, normalize(uCamPos - vW), gMossW)*uSunCol*sunShadowT(vW, -N, 1.0, 1);   // BF B1 (interim: moves into shade() with PF1's blend)
  if (uGxDebug == 2 && uCapture == 0) c = gDbg*0.8 + 0.05;
  if (uGxDebug == 4 && uCapture == 0) c = gWxDbg;
  if (uGxDebug == 3 && uCapture == 0) c = gFootDbg < 0.0 ? vec3(0.0, 0.0, 0.4) : vec3(gFootDbg);   // MA2: the local ground line (white = within 0.5 m of it)
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
  oVel = vec4(0.0, 0.0, 0.0, uVmVel);   // first-person weapon: a = 1, zero motion (TAA keeps its own pixel, not the world reprojection)
}
)GLSL";

static const char* kGlassFS = R"GLSL(
in vec3 vW, vN, vAlb, vPc, vPn, vAp, vAn, vEmi; in vec4 vPar;
uniform float uGlow, uEmissiveK;
uniform float uGlassOpacity = 1.0, uGlassRefl = 1.0;   // GL1: content/settings/glass.json "opacity", "reflection" (1 = the pre-GL1 look)
out vec4 oColor;
void main(){
  vec3 N = normalize(vN); vec3 V = normalize(uCamPos - vW);
  if (dot(N, V) < 0.0) N = -N;
  // old cylinder glass: slight waviness
  N = normalize(N + 0.025*vec3(vnoise(vAp*18.0)-0.5, vnoise(vAp*18.0+7.0)-0.5, vnoise(vAp*18.0+3.0)-0.5));
  float NdV = max(dot(N, V), 0.0);
  // GR2: per-window variety (the part's centre in asset space): reflection strength, a few curtained panes, darker rooms
  float wid = fract(sin(dot(floor((vAp - vPc)*2.0 + 0.5), vec3(12.99, 78.23, 37.71)))*43758.55);
  float F = (0.04 + 0.96*pow(1.0-NdV, 5.0))*uGlassRefl*(0.75 + 0.5*fract(wid*13.7));
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
  float a = clamp((0.10 + dirt*0.08)*uGlassOpacity + F*0.85*mix(0.35, 1.0, smoothstep(0.05, 0.6, dot(uSunCol, vec3(0.3, 0.5, 0.2)))), 0.0, 0.95);
  if (abs(N.y) < 0.5) {
    vec3 Nh = normalize(vec3(N.x, 0.0, N.z) + 1e-4);
    float al = dot(vAp.xz, vec2(-Nh.z, Nh.x));
    if (wid < 0.27) {   // linen / net curtains behind about a quarter of the windows, drawn part-way, lit by the daylight through the pane
      float cov = 0.3 + 0.5*fract(wid*7.3);
      float cur = smoothstep(cov, cov - 0.05, fract(al*1.25 + wid*3.1)) * (0.82 + 0.18*sin(al*85.0 + wid*40.0));
      vec3 lin = vec3(0.60, 0.58, 0.53)*(skyColor(vec3(0.0, 1.0, 0.0))*0.45 + uSunCol*0.08);
      c = mix(c, lin*(1.0 - a) + c, cur*0.8); a = max(a, cur*0.8);
    } else if (wid > 0.72) a = min(a + 0.14, 0.95);   // a darker, unlit room
  }   // GL1: the reflection keeps full strength   // VH6d: a faint night reflection, not a milky pane
  vec3 emi = vEmi*(uGlow*uEmissiveK);   // lit lamp glass: added in full whatever the pane alpha
  c = applyFog(c, vW) - applyFog(vec3(0.0), vW)*(1.0 - a);   // GL1: the fog behind the pane is already in the background: add only the covered share (was a full second fog layer: a milky pane in haze)
  oColor = vec4((c + emi) / max(a, 0.05), a);
}
)GLSL";

// ---- imported textured models (first-person weapons): metal-rough PBR with normal and AO maps
static const char* kModelVS = R"GLSL(
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNrm; layout(location=2) in vec4 aTan; layout(location=3) in vec2 aUV;
uniform mat4 uModel, uVP;
uniform float uThin;   // radians to widen by (thin geometry), 0 = off
// AIB1: motion vectors for moving models (ModelDraw::vel): 1 the previous model matrix, 2 also the skinned soldier's previous-frame
// vertex (aPrev, CharInstance::prevBuf). 0: none (TAA reprojects the pixel from depth, as for static geometry)
layout(location=4) in vec3 aPrev; uniform int uVelMode; uniform mat4 uPrevModel, uVPU, uPrevVP;
out vec3 vW, vN; out vec4 vT; out vec2 vUV; flat out vec4 vTintI; out vec4 vCC, vPC;
void main(){
  vTintI = vec4(0.0);
  vec4 w = uModel*vec4(aPos,1.0);
  mat3 m = mat3(uModel);
  if (uThin > 0.0) { vec3 n = m*aNrm; float l = length(n); if (l > 1e-6) w.xyz += n / l * min(length(w.xyz - uCamPos) * uThin, 0.03); }
  vW = w.xyz; vN = m*aNrm; vT = vec4(m*aTan.xyz, aTan.w); vUV = aUV;
  vCC = uVPU*w; vPC = uPrevVP*(uPrevModel*vec4(uVelMode == 2 ? aPrev : aPos, 1.0));
  gl_Position = uVP*w;
}
)GLSL";
static const char* kModelFS = R"GLSL(
in vec3 vW, vN; in vec4 vT; in vec2 vUV; flat in vec4 vTintI; in vec4 vCC, vPC; uniform int uVelMode;
uniform float uDustK;   // WS4: dust coating on cut-out foliage (progFolModel, progImp; 0 elsewhere)
uniform float uDither;   // CP2 treepxlod cross-fade (model foliage only): > 0 keep the pixels whose fixed noise is under it, < 0 the rest
uniform vec3 uIvyStem; uniform vec2 uIvyRef;   // VR1 (src/ivy.cpp)
uniform sampler2D uAlb, uMR, uNrm, uAO;
uniform int uHasAlb, uHasMR, uHasNrm, uHasAO, uAOinMR; uniform float uAOWeak;   // TK3: 0 as baked (the default of an unset uniform), 1 no baked occlusion
uniform vec4 uBase, uTint; uniform float uMetal, uRough;
layout(location=0) out vec4 oColor;
layout(location=1) out float oDist;
layout(location=2) out vec4 oAmb;
layout(location=3) out vec4 oNR;
layout(location=4) out vec4 oVel; uniform float uVmVel;
uniform float uAlbFloor;
uniform int uCutout;   // NT8: alpha-tested foliage (needles, blades, fronds): discard under 0.5, sun through the thin side
uniform float uFolTrans;   // VO L4: two-sided leaf translucency on model foliage (progFolModel only; 0: the NT8 rim on cut-out plants)
uniform float uGlassOpacity = 1.0, uGlassFilm = 1.0, uGlassRefl = 1.0;   // GL1: content/settings/glass.json "opacity", "film", "reflection" (1 = the pre-GL1 look)
uniform int uGlass;   // 0 opaque; 1 window glass (dusty); 2 a wreck's glass (crazed, grimy); 3 clean (the car you sit in: no film to catch the cabin light at night)
uniform vec4 uVmClip; uniform int uClipMe;   // AT6: the fitted optic's window (px, bottom-left origin; radius px): the gun's own parts there are not drawn
void main(){
  if (uDither != 0.0) { float n = pixelNoiseFixed(); if (uDither > 0.0 ? n >= uDither : n < -uDither) discard; }   // CP2: as kFolDepthFS
  if (uClipMe == 1 && uVmClip.z > 0.0 && length(gl_FragCoord.xy - uVmClip.xy) < uVmClip.z) discard;
  vec4 base = uBase;
  if (uHasAlb == 1) base *= texture(uAlb, vUV);
  if (uCutout == 1 && base.a < cutRef()) discard;
  if (uCutout == 1) base.rgb = mix(base.rgb, vec3(0.45, 0.37, 0.26), 0.5*uDustK);   // WS4: dust on the needles and leaves
  if (vTintI.a > 2.5) {   // FL1 greenery: this species' spring green (already / the texture's mean luminance), leaves only
    if (uCutout == 1) base.rgb = mix(base.rgb, vTintI.rgb*dot(base.rgb, vec3(0.2126, 0.7152, 0.0722)), uGreenK.w*greenMask(base.rgb));
  } else if (vTintI.a > 1.5) base.rgb *= vTintI.rgb;   // HB1c: sandbags, per bag (src/sandbags.cpp)
  else if (vTintI.a > 0.5) {   // VR1: the ivy pack recoloured per instance; the texture keeps its own shading (luminance)
    float l = dot(base.rgb, vec3(0.2126, 0.7152, 0.0722));
    base.rgb = uCutout == 1 ? vTintI.rgb * clamp(l / uIvyRef.x, 0.15, 2.5) : mix(uIvyStem, vTintI.rgb, 0.15) * clamp(l / uIvyRef.y, 0.15, 2.5);
  }
  base.rgb = max(base.rgb, vec3(uAlbFloor));   // VH6d: a near-black supplied cabin reads as dark grey trim from the seat
  if (uGlass > 0) {   // tinted window glass: see-through, a dust film, sky reflection at grazing angles
    vec3 N = normalize(vN); if (!gl_FrontFacing) N = -N;
    vec3 V = normalize(uCamPos - vW);
    float dust = 0.35 + 0.4*smoothstep(0.3, 0.8, fract(sin(dot(floor(vW.xz*7.0 + vW.y*5.0), vec2(12.9898, 78.233)))*43758.5453)*0.3 + 0.5*clamp(-N.y + 0.5, 0.0, 1.0));
    dust *= uGlassFilm;
    if (uGlass == 2) dust = min(1.0, dust + 0.15);   // a wreck's glass: crazed and grimy (VH6d: no punched-out cells, they read as a checker pattern)
    if (uGlass == 3) dust = 0.0;
    dust *= mix(0.2, 1.0, smoothstep(0.05, 0.6, dot(uSunCol, vec3(0.3, 0.5, 0.2))));   // VH6d: an unlit film at night: the pane stays see-through, not a milky sheet
    float fr = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 4.0)*(uGlass == 3 ? 1.0 : uGlassRefl);
    vec3 tint = vec3(0.16, 0.19, 0.19);
    vec3 c = shade(vW, N, mix(tint, vec3(0.42, 0.39, 0.33), dust*0.6), mix(0.06, 0.7, dust), 0.0, 1.0);
    c = applyFog(c, vW);
    oColor = vec4(c, uGlass == 3 ? clamp(0.06 + 0.3*fr, 0.0, 0.5) : clamp((0.22 + 0.28*dust)*uGlassOpacity + 0.5*fr, 0.0, 0.92));
    oDist = length(vW - uCamPos); oAmb = vec4(0.0); oNR = vec4(0.0, 1.0, 0.0, 1.0); oVel = vec4(0.0);
    return;
  }
  float rough = uRough, metal = uMetal, ao = 1.0;
  if (uHasMR == 1) { vec4 mr = texture(uMR, vUV); rough *= mr.g; metal *= mr.b; if (uAOinMR == 1) ao = mr.r; }
  if (uAlbFloor > 0.0) { metal = min(metal, 0.15); rough = max(rough, 0.6); }   // VH6d: cabin trim, not a black mirror
  if (uHasAO == 1) ao = texture(uAO, vUV).r;
  if (uHasAO == 1 || uAOinMR == 1) ao = mix(ao, 1.0, uAOWeak);
  vec3 N = normalize(vN);
  if (!gl_FrontFacing) N = -N;                         // glTF models are double sided
  if (uHasNrm == 1) {
    vec3 T = vT.xyz - N*dot(N, vT.xyz);
    vec3 t = texture(uNrm, vUV).xyz*2.0 - 1.0;
    if (t.z < -0.99) t.z = sqrt(max(0.0, 1.0 - dot(t.xy, t.xy)));   // TK2: a BC5 (RG) normal map: z rebuilt
    if (dot(T, T) > 1e-12) {
      T = normalize(T); vec3 B = cross(N, T) * (vT.w < 0.0 ? -1.0 : 1.0);
      vec3 n = T*t.x + B*t.y + N*max(t.z, 0.05);
      if (dot(n, n) > 1e-12) N = normalize(n);
    } else {   // WP2: no tangent stream (skinned first-person rigs): the cotangent frame from the screen derivatives
      vec3 dp1 = dFdx(vW), dp2 = dFdy(vW); vec2 du1 = dFdx(vUV), du2 = dFdy(vUV);
      vec3 p2 = cross(dp2, N), p1 = cross(N, dp1);
      vec3 Tc = p2*du1.x + p1*du2.x, Bc = p2*du1.y + p1*du2.y;
      float k = max(dot(Tc, Tc), dot(Bc, Bc));
      if (k > 1e-24) { k = inversesqrt(k); vec3 n = Tc*k*t.x + Bc*k*t.y + N*max(t.z, 0.05); if (dot(n, n) > 1e-12) N = normalize(n); }
    }
  }
  vec3 c = shade(vW, N, base.rgb, clamp(rough, 0.04, 1.0), clamp(metal, 0.0, 1.0), mix(1.0, ao, 0.85));
  if (uFolTrans > 0.0) {   // VO L4 (Barre-Brisebois & Bouchard 2011): light through the leaf from behind, peaked toward the sun, plus a
    // diffuse share through the lit face's back; shadowed by what shades the leaf's sun side
    vec3 Vv = normalize(uCamPos - vW), Lt = normalize(uSunDir + N*0.35);
    float tr = 0.7*pow(clamp(dot(Vv, -Lt), 0.0, 1.0), 4.0) + 0.18*max(dot(-N, uSunDir), 0.0);
    c += base.rgb*vec3(1.0, 1.04, 0.72)*uSunCol*tr*uFolTrans*sunShadowT(vW, -N, 1.0, 1);
  } else if (uCutout == 1) {   // thin needles / blades: the low sun through them from behind, a warm rim (still shadowed)
    vec3 Vv = normalize(uCamPos - vW); float VoL = max(dot(-Vv, uSunDir), 0.0);
    c += base.rgb*vec3(1.0, 1.05, 0.6)*uSunCol*(0.04 + 0.35*pow(VoL, 5.0))*sunShadowT(vW, N, 1.0, 1);
  }
  if (uTint.a >= 0.999) c *= uTint.rgb;   // opaque tint multiplies (white = unchanged; tank wrecks are blackened)
  c = applyFog(c, vW);
  if (uTint.a < 0.999) c = mix(c, uTint.rgb, 0.55);
  oColor = vec4(c, 1.0);
  oDist = length(vW - uCamPos);
  oAmb = vec4(gInd*gFogT, 1.0);
  oNR = vec4(N, clamp(rough, 0.04, 1.0) + (1.0 - gFogT));
  oVel = uVelMode > 0 ? vec4(vPC.xy/vPC.w*0.5 - vCC.xy/vCC.w*0.5, 0.0, 1.0)   // AIB1: a moving model's own motion (TAA follows it, as for grass)
       : vec4(0.0, 0.0, 0.0, uVmVel);   // first-person weapon: a = 1, zero motion (TAA keeps its own pixel, not the world reprojection)
}
)GLSL";

// ---- NT8 model foliage: one instanced draw per (model, part); per instance a mat4 in binding 12 whose column 0 .w carries
// the wind amount (0 for rocks / trunks of no sway). Sway grows with the height above the instance origin.
static const char* kFolVS = R"GLSL(
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNrm; layout(location=2) in vec4 aTan; layout(location=3) in vec2 aUV;
layout(std430, binding = 12) readonly buffer NmkFol { vec4 gFol[]; };
uniform mat4 uVP; uniform float uTime, uWind; uniform int uInstBase;
out vec3 vW, vN; out vec4 vT; out vec2 vUV; flat out vec4 vTintI; out vec4 vCC, vPC;   // vCC / vPC: kModelFS's motion (AIB1), unused here
invariant gl_Position;
void main(){
  int b = (uInstBase + gl_InstanceID)*4;
  vec4 c0 = gFol[b], c1 = gFol[b + 1], c2 = gFol[b + 2], c3 = gFol[b + 3]; float wk = c0.w; c0.w = 0.0;
  vTintI = c3.w > 5.5 ? vec4(c1.w, c2.w, c3.w - 6.0, 2.0) : c3.w > 1.5 ? vec4(c1.w, c2.w, c3.w - 2.0, 1.0) : c3.w < -1.5 ? vec4(c1.w, c2.w, -c3.w - 2.0, 3.0) : vec4(0.0);   // VR1: the matrix's free w row carries a colour (HB1c: from 6, a multiplier, a = 2; FL1: < 0, a greenery target, a = 3)
  c1.w = 0.0; c2.w = 0.0; c3.w = 1.0;
  mat4 M = mat4(c0, c1, c2, c3);
  vec4 w = M*vec4(aPos, 1.0);
  float h = max(aPos.y, 0.0), ph = 0.21*M[3].x + 0.17*M[3].z;
  float wt = windClock(false);
  float s = sin(1.1*wt + ph) + 0.35*sin(2.7*wt + 1.3*ph + 0.4*h);
  float f = 0.012*sin(7.0*wt + 3.1*dot(w.xyz, vec3(1.0, 0.7, 1.3)));   // needle / blade flutter
  w.xyz += (windDir()*(0.018*h*s) + vec3(f, 0.5*f, f)*min(h, 1.5))*wk*windSway(uWind)*windGust(M[3].xz, false);
  mat3 m = mat3(M);
  vW = w.xyz; vN = m*aNrm; vT = vec4(m*aTan.xyz, aTan.w); vUV = aUV; vCC = vPC = vec4(0.0, 0.0, 0.0, 1.0);
  gl_Position = uVP*w;   // invariant: the CP1 depth prepass (fol_depth) and the GL_EQUAL shading pass must agree
}
)GLSL";
// CP3 octahedral impostors (corpus 09 s2.4): each model tree baked into 12 x 12 hemi-octahedral frames (albedo + alpha,
// object-space normal + depth). The bake: kFolVS with one identity instance, an orthographic camera per frame.
static const char* kImpBakeFS = R"GLSL(
in vec3 vW, vN; in vec4 vT; in vec2 vUV; flat in vec4 vTintI;
uniform sampler2D uAlb; uniform int uHasAlb, uCutout; uniform vec4 uBase;
layout(location=0) out vec4 oAlb; layout(location=1) out vec4 oNrm;
void main(){
  vec4 b = uBase; if (uHasAlb == 1) b *= texture(uAlb, vUV);
  if (uCutout == 1 && b.a < 0.5) discard;
  vec3 N = normalize(vN); if (!gl_FrontFacing) N = -N;
  vec3 n = N/(abs(N.x) + abs(N.y) + abs(N.z)); vec2 o = n.z >= 0.0 ? n.xy : (1.0 - abs(n.yx))*vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);   // octahedral: two channels (BC5)
  oAlb = vec4(b.rgb, 1.0); oNrm = vec4(o*0.5 + 0.5, gl_FragCoord.z, 1.0);
}
)GLSL";
// the draw: one camera-facing quad per tree; the frame nearest the view direction (object space), the quad's pixel
// projected into that frame's basis. Per instance (SSBO 13): centre, radius | yaw, dither, -, - | tint (as FolModelDraw)
static const char* kImpVS = R"GLSL(
layout(std430, binding = 13) readonly buffer NmkImp { vec4 gImp[]; };
uniform mat4 uVP;
out vec3 vW; flat out vec4 vCR; flat out vec4 vYD; flat out vec4 vTint;
void main(){
  int b = gl_InstanceID*3; vCR = gImp[b]; vYD = gImp[b + 1]; vTint = gImp[b + 2];
  vec3 f = uCamPos - vCR.xyz; f = length(f) > 1e-3 ? normalize(f) : vec3(0.0, 0.0, 1.0);
  vec3 rt = normalize(cross(abs(f.y) > 0.99 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0), f)), up = cross(f, rt);
  int k = gl_VertexID; vec2 q = k == 0 ? vec2(-1, -1) : k == 1 ? vec2(1, -1) : k == 2 ? vec2(1, 1) : k == 3 ? vec2(-1, -1) : k == 4 ? vec2(1, 1) : vec2(-1, 1);
  vW = vCR.xyz + (rt*q.x + up*q.y)*vCR.w;
  gl_Position = uVP*vec4(vW, 1.0);
}
)GLSL";
static const char* kImpFS = R"GLSL(
in vec3 vW; flat in vec4 vCR; flat in vec4 vYD; flat in vec4 vTint;
uniform sampler2D uImpAlb, uImpNrm; uniform float uImpUse; uniform float uFolTrans; uniform float uDustK;   // uImpUse: the frames' share of the atlas (12 x 170 / 2048)
layout(location=4) out vec4 oVel; layout(location=0) out vec4 oColor; layout(location=2) out vec4 oAmb; layout(location=3) out vec4 oNR;
vec2 hoEnc(vec3 d){ d.y = max(d.y, 0.0); d /= abs(d.x) + abs(d.y) + abs(d.z); return vec2(d.x + d.z, d.z - d.x)*0.5 + 0.5; }
vec3 hoDec(vec2 uv){ vec2 e = uv*2.0 - 1.0; float x = (e.x - e.y)*0.5, z = (e.x + e.y)*0.5; return normalize(vec3(x, 1.0 - abs(x) - abs(z), z)); }
vec3 rotY(vec3 p, float a){ float c = cos(a), s = sin(a); return vec3(c*p.x + s*p.z, p.y, -s*p.x + c*p.z); }
void main(){
  if (vYD.y != 0.0) { float n = pixelNoiseFixed(); if (vYD.y > 0.0 ? n >= vYD.y : n < -vYD.y) discard; }   // CP2 cross-fade with the model
  const float N = 12.0;
  vec3 Vo = rotY(normalize(uCamPos - vCR.xyz), -vYD.x);
  vec2 fi = clamp(floor(hoEnc(Vo)*N), 0.0, N - 1.0);
  vec3 dF = hoDec((fi + 0.5)/N), rF = normalize(cross(vec3(0.0, 1.0, 0.0), dF)), uF = cross(dF, rF);
  vec3 po = rotY(vW - vCR.xyz, -vYD.x)/vCR.w;
  vec2 uv = vec2(dot(po, rF), dot(po, uF))*0.5 + 0.5;
  if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) discard;
  vec2 auv = (fi + uv)/N*uImpUse;
  vec4 a = texture(uImpAlb, auv);
  float lod = textureQueryLod(uImpAlb, auv).y;   // averaged mips thin the needles out: the cut falls with the mip so coverage holds
  if (a.a < mix(0.5, 0.18, clamp(lod/4.0, 0.0, 1.0))) discard;
  vec3 alb = a.rgb/max(a.a, 1e-3);
  if (vTint.a > 2.5) alb = mix(alb, vTint.rgb*dot(alb, vec3(0.2126, 0.7152, 0.0722)), uGreenK.w*greenMask(alb));   // FL1 greenery, as kModelFS
  else if (vTint.a > 1.5) alb *= vTint.rgb;
  alb = mix(alb, vec3(0.45, 0.37, 0.26), 0.5*uDustK);   // WS4, as kModelFS
  vec2 on = texture(uImpNrm, auv).xy*2.0 - 1.0; vec3 no = vec3(on, 1.0 - abs(on.x) - abs(on.y)); float tq = max(-no.z, 0.0); no.xy += vec2(no.x >= 0.0 ? -tq : tq, no.y >= 0.0 ? -tq : tq);
  vec3 Nn = normalize(rotY(normalize(no), vYD.x));
  // lit as the model it stands for (kModelFS on NT8 foliage): shade(), then the leaf translucency (uFolTrans) or the needle rim,
  // so the far forest keeps its value and colour across the swap (it read darker with a lighting of its own)
  vec3 c = shade(vW, Nn, alb, 0.85, 0.0, 1.0);
  vec3 Vv = normalize(uCamPos - vW);
  if (uFolTrans > 0.0) {
    vec3 Lt = normalize(uSunDir + Nn*0.35);
    float tr = 0.7*pow(clamp(dot(Vv, -Lt), 0.0, 1.0), 4.0) + 0.18*max(dot(-Nn, uSunDir), 0.0);
    c += alb*vec3(1.0, 1.04, 0.72)*uSunCol*tr*uFolTrans*sunShadowT(vW, -Nn, 1.0, 1);
  } else {
    float VoL = max(dot(-Vv, uSunDir), 0.0);
    c += alb*vec3(1.0, 1.05, 0.6)*uSunCol*(0.04 + 0.35*pow(VoL, 5.0))*sunShadowT(vW, Nn, 1.0, 1);
  }
  c = applyFog(c, vW);
  oColor = vec4((any(isnan(c)) || any(isinf(c))) ? vec3(0.0) : c, 1.0);
  oAmb = vec4(gInd*gFogT, 1.0); oNR = vec4(Nn, 0.85 + (1.0 - gFogT)); oVel = vec4(0.0, 0.0, 0.0, 1.0);
}
)GLSL";
static const char* kFolDepthFS = R"GLSL(
in vec3 vW, vN; in vec4 vT; in vec2 vUV;
uniform sampler2D uAlb; uniform int uCutout; uniform vec4 uBase; uniform float uDither;
void main(){ if (uDither != 0.0) { float n = pixelNoiseFixed(); if (uDither > 0.0 ? n >= uDither : n < -uDither) discard; }   // CP2 cross-fade, every pass
  if (uCutout == 1 && uBase.a*texture(uAlb, vUV).a < cutRef()) discard; }   // kModelFS's cut-out exactly (CP1: the main prepass before its GL_EQUAL pass)
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
//   SC1 rifle scopes (every reticle but the TZF): R = half the screen height (the ring touches top and bottom), a solid black
//   rim from 0.86 R out with a soft inner vignette, and beyond it the same (magnified) frame lightly blurred and dimmed, not black.
// uMode 1 (after the weapon, blended ONE / SRC_ALPHA into hdrColor): the inside comes back from the composite, so the
//   weapon shows only around the eyepiece; outside, the weapon is shaded by uShade (out of focus, in the eye's shadow).
// TH1 thermal: the heat of the warm things (soldiers, engines) into an R16F target, depth-tested against the scene (walls
// hide them; smoke, which writes no depth, does not). The eyepiece shader maps it over a cold monochrome of the scene.
static const char* kHeatVS = R"GLSL(
#version 450 core
layout(location=0) in vec3 aPos;
uniform mat4 uVP, uModel; out vec3 vW;
void main(){ vec4 w = uModel*vec4(aPos, 1.0); vW = w.xyz; gl_Position = uVP*w; }
)GLSL";
static const char* kHeatFS = R"GLSL(
#version 450 core
uniform float uHeat; uniform vec3 uCam; uniform vec4 uObj; in vec3 vW; out float oHeat;
// TH2: a depth map of the figure: across the object's own depth extent (its centre +- reach from the camera) the nearest
// surface 1.0, the farthest ~0.45; far objects a little dimmer overall; one even heat (no head / chest bands)
void main(){
  float shade = 1.0, fall = 1.0;
  if (uObj.w > 0.0) {
    float zc = length(uObj.xyz - uCam), z = length(vW - uCam);
    shade = mix(1.0, 0.45, smoothstep(0.0, 1.0, clamp((z - (zc - uObj.w))/(2.0*uObj.w), 0.0, 1.0)));
    fall = mix(1.0, 0.8, smoothstep(10.0, 150.0, zc));
  }
  oHeat = uHeat*shade*fall;
}
)GLSL";

static const char* kEyepieceFS = R"GLSL(
#version 450 core
in vec2 vUV;
uniform sampler2D uSrc; uniform vec2 uRes; uniform float uR, uDark, uShade; uniform int uReticle, uMode;
uniform float uPxMoa, uRetSize, uRetBright;   // AT2: px per MOA in the magnified view; Settings > Gameplay reticle size / brightness
uniform int uThermal, uNSpots; uniform sampler2D uHeat, uDepthT; uniform vec4 uSpots[16]; uniform float uSpotZ[16], uTime;   // TH1
out vec4 oColor;
float segD(vec2 p, vec2 a, vec2 b){ vec2 pa = p - a, ba = b - a; float h = clamp(dot(pa, ba)/dot(ba, ba), 0.0, 1.0); return length(pa - ba*h); }
float cov(float d){ return clamp(0.5 - d, 0.0, 1.0); }   // ink coverage at signed distance d (px, < 0 inside)
float redInk = 0.0;   // SC1: illuminated (red) reticle parts, set by reticleInk
float tick(vec2 q, float pos, float len, float w){ return cov(max(abs(q.x - pos) - w, abs(q.y) - len)); }   // a short tick across the x axis at pos
float reticleInk(vec2 q, float R){
  float ink = 0.0; redInk = 0.0;
  if (uReticle == 1) {          // German no. 1 (ZF39): pointed post from below, its tip the aim point; heavy side bars, open centre
    float hw = max(1.6, R*0.011), tip = R*0.07;
    if (q.y <= 0.0) ink = cov(abs(q.x) - hw*min(1.0, -q.y/tip));
    ink = max(ink, cov(max(abs(q.y) - max(1.0, R*0.007), R*0.18 - abs(q.x))));
  } else if (uReticle == 2) {   // US M81 / M82 (M1C): fine crosshair, tapered lower post
    float t = 0.5*max(1.0, uRetSize);   // AT2: 1 px lines
    ink = cov(abs(q.y) - t);
    ink = max(ink, cov(abs(q.x) - (q.y > 0.0 ? t : max(t, 0.012*(-q.y)))));
  } else if (uReticle == 4) {   // MG3 plain "+": fine full-width crosshair (the AR-15's 3x scope)
    float t = 0.5*max(1.0, uRetSize);
    ink = cov(min(abs(q.x), abs(q.y)) - t);
  } else if (uReticle == 5) {   // SC1 M24 mil-dot style: thick outer posts tapering to a fine cross, mil ticks, red dotted centre, holdover dots
    float t = 0.5*max(1.0, uRetSize), m = R*0.06;   // line half width (AT2: 1 px), one mil
    float cd = max(0.75*max(0.67, uRetSize), 0.375*uPxMoa*uRetSize);   // AT2: the red centre dots 0.75 MOA across, never under ~1.5 px
    vec2 a = abs(q); float post = smoothstep(0.62*R, 0.72*R, max(a.x, a.y)) * max(2.5, R*0.012);
    ink = max(cov(a.y - (a.x > a.y ? max(t, post) : t)), cov(a.x - (a.y > a.x ? max(t, post) : t)));
    for (int k = 1; k <= 9; ++k) {
      float h = (k % 5 == 0 ? 0.35 : 0.2)*m;
      ink = max(ink, max(cov(max(abs(a.x - float(k)*m) - t, a.y - h)), cov(max(abs(a.y - float(k)*m) - t, a.x - h))));
    }
    for (int k = -3; k <= 3; ++k) if (k != 0) redInk = max(redInk, max(cov(length(q - vec2(float(k)*0.33*m, 0.0)) - cd), cov(length(q - vec2(0.0, float(k)*0.33*m)) - cd)));
    for (int s = -1; s <= 1; s += 2) for (int i = 0; i < 2; ++i) for (int j = 0; j < 2; ++j)   // holdover dot clusters, lower left / right
      ink = max(ink, cov(length(q - vec2(float(s)*(2.6 + 0.5*float(i))*m, -(2.6 + 0.5*float(j))*m)) - max(1.0, uRetSize)));
  } else if (uReticle == 6) {   // SC1 .50: thick side / bottom posts, thin top line, fine centre cross with mil ticks, range funnel below
    float t = 0.5*max(1.0, uRetSize), m = R*0.05, pw = max(2.5, R*0.012);
    vec2 a = abs(q);
    ink = cov(a.y - (a.x > 0.32*R ? pw : t));                       // horizontal: thick posts from 0.32 R out
    ink = max(ink, cov(a.x - (q.y < -0.78*R ? pw : t)));            // vertical: thin, thick post at the bottom
    for (int k = 1; k <= 4; ++k) ink = max(ink, max(cov(max(abs(a.x - float(k)*m) - t, a.y - 0.25*m)), cov(max(abs(a.y - float(k)*m) - t, a.x - 0.25*m))));
    if (q.y < -0.18*R && q.y > -0.76*R) {                             // the funnel: curved stadia widening toward the bottom, a ladder between
      float u = (-q.y - 0.18*R)/(0.58*R), w = (0.07 + 0.38*u*u)*R;
      ink = max(ink, cov(abs(a.x - w) - t*1.2));
      float stp = 0.058*R, yy = mod(-q.y - 0.18*R, stp);
      if (a.x < w) ink = max(ink, cov(min(yy, stp - yy) - t));
    }
  } else if (uReticle == 7) {   // AT3 ACOG (the user's reference picture, its layout in clear-field radii from the chevron tip): a small red
    // chevron, its tip the aim point (the shot line at the zero); below it a stadia to the rim with range ticks and 4 / 6 / 8 / 10
    // to its right; each side a bar well clear of the centre with end caps and two ticks. No ring, dot or full crosshair
    float t = 0.5*max(1.0, uRetSize), S = uRetSize;   // 1 px lines
    float uc = 0.026*R*S, dch = min(segD(q, vec2(0.0), vec2(-uc, -1.27*uc)), segD(q, vec2(0.0), vec2(uc, -1.27*uc)));
    redInk = cov(dch - 0.6*max(1.0, S));
    vec2 a = abs(q);
    if (q.y < -0.051*R && q.y > -0.99*R) ink = cov(a.x - t);   // the stadia
    const float num[4] = float[4](0.093, 0.228, 0.414, 0.670);
    const float sml[6] = float[6](0.153, 0.31, 0.363, 0.53, 0.823, 0.95);
    for (int k = 0; k < 6; ++k) ink = max(ink, cov(max(abs(q.y + sml[k]*R) - t, a.x - (k == 0 ? 0.023 : 0.013)*R)));
    const int glyph[5] = int[5](31599, 11415, 23497, 31215, 31727);   // 3 x 5 cells, top row first: 0 1 4 6 8
    float cs = max(1.5, 0.0095*R);   // a digit's cell (px): about 0.03 R wide, 0.047 R tall as drawn
    for (int k = 0; k < 4; ++k) {
      float y = -num[k]*R; ink = max(ink, cov(max(abs(q.y - y) - t, a.x - 0.028*R)));
      int d0 = k == 0 ? 2 : k == 1 ? 3 : k == 2 ? 4 : 1, nd = k == 3 ? 2 : 1;   // 4, 6, 8, 10
      for (int j = 0; j < nd; ++j) {
        vec2 c = vec2((q.x - 0.04*R - float(j)*4.0*cs)/cs, (y + 2.5*cs - q.y)/cs);   // cell coordinates from the digit's top left
        if (c.x >= 0.0 && c.x < 3.0 && c.y >= 0.0 && c.y < 5.0) { int g = glyph[j == 0 ? d0 : 0], bit = 14 - (int(c.y)*3 + int(c.x)); if (((g >> bit) & 1) == 1) ink = 1.0; }
      }
    }
    float yb = -0.044*R;   // the side bars, 0.33 - 0.83 R out: a tall cap inside, a short one outside, a tall tick at 0.66, a short one at 0.50
    if (a.x > 0.33*R && a.x < 0.83*R) ink = max(ink, cov(abs(q.y - yb) - t));
    const float bx[4] = float[4](0.33, 0.498, 0.66, 0.83);
    const float bh[4] = float[4](0.06, 0.03, 0.058, 0.028);
    for (int k = 0; k < 4; ++k) ink = max(ink, cov(max(abs(a.x - bx[k]*R) - t, abs(q.y - yb) - bh[k]*R)));
  } else if (uReticle == 8) {   // TH1 thermal: thin cross with an open centre, a small red dot, range ticks down the lower post
    float t = 0.5*max(1.0, uRetSize), gap = R*0.06; vec2 a = abs(q);
    ink = max(cov(max(a.y - t, gap - a.x)), cov(max(a.x - t, gap - a.y)));
    for (int k = 1; k <= 5; ++k) ink = max(ink, cov(max(abs(q.y + gap + float(k)*R*0.075) - t, a.x - R*((k % 2 == 0) ? 0.032 : 0.018))));
    redInk = cov(length(q) - uRetSize);   // AT2: a 2 px dot
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
  bool rifle = uReticle != 3;   // SC1: rifle scopes: R = the ring's outer edge (half the screen height), clear field inside 0.86 R
  float Ri = rifle ? 0.86*R : R;
  if (rifle) f = clamp(Ri + 0.5 - r, 0.0, 1.0);
  if (uMode == 1) { vec3 c = textureLod(uSrc, vUV, 0.0).rgb; oColor = vec4(c*f, (1.0 - f)*uShade*mix(1.0, 0.55, outer)); return; }
  vec3 sharp = textureLod(uSrc, vUV, 0.0).rgb;
  if (uThermal == 1) {   // TH1: cold surroundings in deep blue / teal by the scene's own relative brightness (sky coldest), heat over it
    const vec3 lw = vec3(0.2126, 0.7152, 0.0722);
    float avg = max(dot(textureLod(uSrc, vec2(0.5), 12.0).rgb, lw), 1e-4), L = dot(sharp, lw), d = textureLod(uDepthT, vUV, 0.0).r;
    float loc = max(dot(textureLod(uSrc, vUV, 5.0).rgb, lw), 1e-5);   // local contrast: surfaces against their surroundings (the sensor's detail)
    float cold = d <= 0.0 ? 0.03 : clamp(0.08 + 0.3*L/(L + 1.6*avg) + 0.12*clamp(L/loc - 1.0, -0.6, 0.6), 0.02, 0.42);
    float heat = textureLod(uHeat, vUV, 0.0).r;
    for (int i = 0; i < uNSpots; ++i) {
      vec2 dd = (vUV - uSpots[i].xy)*vec2(uRes.x/uRes.y, 1.0); float rr = length(dd)/max(uSpots[i].z, 1e-4);
      if (rr < 1.0 && d <= uSpotZ[i]*1.002 + 1e-6) heat = max(heat, uSpots[i].w*(1.0 - rr*rr));
    }
    float T = max(cold, heat);
    // TH2: one hue: the cold scene deep blue / teal, heat from a pale teal-grey to white by its value (bodies carry their
    // own depth shading); only true emitters (fire, flashes) go past 1: overexposed white
    vec3 c = T < 0.4 ? mix(vec3(0.004, 0.012, 0.035), vec3(0.03, 0.16, 0.22), T/0.4)
           : T < 1.0 ? mix(vec3(0.16, 0.30, 0.34), vec3(0.97, 1.0, 1.0), smoothstep(0.4, 1.0, T))
                     : vec3(0.97, 1.0, 1.0)*(1.0 + 0.6*clamp(T - 1.0, 0.0, 1.0));
    float g = fract(sin(dot(floor(vUV*uRes*0.5) + floor(uTime*12.0), vec2(12.9898, 78.233)))*43758.5453);   // sensor grain, a low refresh
    sharp = c*(1.0 + 0.06*(g - 0.5))*max(avg, 0.05)*4.0;   // the sensor's own gain: as bright at night as by day
  }
  sharp *= (rifle ? mix(1.0, uReticle == 7 ? 0.7 : 0.25, smoothstep(uReticle == 7 ? 0.8*R : 0.62*R, Ri, r)) :   // AT3: the ACOG's field clear to its rim
              mix(1.0, 0.6, smoothstep(0.8*R, R, r)));
  sharp = mix(sharp, vec3(0.0015), reticleInk(q, Ri));
  sharp = mix(sharp, vec3(4.0, 0.12, 0.06)*uRetBright, redInk);   // illuminated parts glow red (AT2: Settings > Gameplay brightness)
  float bl = R*(rifle ? 0.035 : 0.09), lod = log2(max(1.0, bl/3.0));   // SC1: the rifle surround only a little blurred
  vec3 s = vec3(0.0);
  for (int i = 0; i < 16; ++i) {
    float rr = sqrt((float(i) + 0.5)/16.0)*bl, a = float(i)*2.39996323;
    s += textureLod(uSrc, vUV + vec2(cos(a), sin(a))*rr/uRes, lod).rgb;
  }
  vec3 outside = rifle ? s/16.0*uDark*clamp(r - R + 0.5, 0.0, 1.0) : s/16.0*uDark*smoothstep(R, 1.1*R, r)*mix(1.0, 0.5, outer);   // rifle: black rim between Ri and R, the blurred view beyond
  vec3 c = mix(outside, sharp, f);
  oColor = vec4((any(isnan(c)) || any(isinf(c))) ? vec3(0.0) : c, 1.0);
}
)GLSL";

// ---- volumetric light shafts (half resolution): march the shadow cascades along the view ray up to the world
// depth (never the first-person weapon: it is drawn after this pass), accumulate lit haze with a forward-biased
// Henyey-Greenstein phase. Restrained: it adds light only where the haze is sunlit between occluders.
static const char* kShaftFS = R"GLSL(
in vec2 vUV;
uniform sampler2D uDepth; uniform mat4 uInvVP; uniform float uMaxDist, uDensity; uniform int uShaftN;   // WX1: samples (Low cloud quality 8)
out vec4 oColor;
float hgPhase(float c, float g){ float g2 = g*g; return (1.0 - g2)/(4.0*PI*pow(1.0 + g2 - 2.0*g*c, 1.5)); }
void main(){
  float z = texture(uDepth, vUV).r;
  vec4 p = uInvVP*vec4(vUV*2.0 - 1.0, z, 1.0); vec3 w = p.xyz/p.w;   // reverse-Z: ndc z = depth (sky 0 = far plane)
  vec3 rd = w - uCamPos; float L = length(rd); rd /= max(L, 1e-4);
  L = min(L, uMaxDist);
  int N = uShaftN;
  float dt = L/float(N), jit = pixelNoise(), lit = 0.0, T = 1.0;
  for (int i = 0; i < N; ++i) {
    vec3 q = uCamPos + rd*(dt*(float(i) + jit));
    float e; float s = cascadePCF(uShadow, uLightVP, uShadowTexel, uShadowWorldTexel.x, q, uSunDir, 0.0, 1, e, vec2(uShadowAtlas, 0.0));
    if (e > 0.95 && uShadowFarOn == 1) s = cascadePCF(uShadowFar, uLightVPFar, uShadowTexelFar, uShadowWorldTexel.y, q, uSunDir, 0.0, 1, e, uFarAx);
    s *= cloudShadow(q);   // WX1: the cloud transmittance at the sample itself: beams only through the gaps, none when the sun is covered
    vec4 F = texture(uFogField, (q.xz - uFogBox.xy)*uFogBox.zw);
    float rho = uDensity*exp(-max(q.y, 0.0)/uHazeH) + uEnvFog*uFogIndoor*F.r*exp(-max(q.y - F.b, 0.0)/uEnvFogH);   // WX1: + the environmental fog
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
uniform int uGtao, uSlices, uSteps;   // VO L2: ground-truth AO (Lighting New): slices x steps per side
out vec4 oColor;
vec3 vpos(vec2 uv){ float z = texture(uDepth, uv).r; vec4 p = uInvProj*vec4(uv*2.0 - 1.0, z, 1.0); return p.xyz/p.w; }   // reverse-Z
// VO L2 (Jimenez et al. 2016, XeGTAO): per slice the two horizon angles found along a screen direction, then the
// cosine-weighted visible arc between them, projected-normal weighted; the horizon falls off past the radius (no halos)
vec3 gBent;   // CP4 bentnormal: the visible cone's mean direction (view space), set by gtao()
float gtao(vec3 P, vec3 N){
  vec3 V = normalize(-P);
  float rad = uRadius*1.6*clamp(1.0 + (-P.z - 20.0)/60.0, 1.0, 3.0);
  vec2 radUV = rad*0.5*vec2(uProj[0][0], uProj[1][1])/(-P.z);   // the radius on screen (uv)
  if (radUV.y < uTexel.y) return 1.0;   // under a texel: nothing to resolve
  float noise = pixelNoise(), jit = fract(noise*7.31 + 0.37);
  float vis = 0.0; gBent = vec3(0.0);
  for (int s = 0; s < uSlices; ++s) {
    float phi = (float(s) + noise)/float(uSlices)*3.14159265;
    vec2 d2 = vec2(cos(phi), sin(phi));
    vec3 dirV = vec3(d2, 0.0), ortho = dirV - dot(dirV, V)*V, axis = normalize(cross(dirV, V));
    vec3 pN = N - axis*dot(N, axis); float pL = max(length(pN), 1e-4);
    float cosN = clamp(dot(pN, V)/pL, -1.0, 1.0), n = sign(dot(ortho, pN))*acos(cosN);
    float c0 = -1.0, c1 = -1.0;
    for (int j = 0; j < uSteps; ++j) {
      float t = (float(j) + jit)/float(uSteps); t = t*t*0.98 + 0.02;   // denser near the pixel
      vec2 o = d2*radUV*t;
      for (int k = 0; k < 2; ++k) {
        vec2 uv = vUV + (k == 0 ? o : -o);
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) continue;
        vec3 dv = vpos(uv) - P; float l = length(dv);
        float h = mix(-1.0, dot(dv/max(l, 1e-4), V), 1.0 - smoothstep(rad*0.6, rad, l));   // past the radius: no horizon
        if (k == 0) c0 = max(c0, h); else c1 = max(c1, h);
      }
    }
    float h0 = acos(clamp(c0, -1.0, 1.0)), h1 = -acos(clamp(c1, -1.0, 1.0));   // +side, -side horizon angles
    h0 = n + min(h0 - n, 1.5707963); h1 = n + max(h1 - n, -1.5707963);
    float sn = sin(n);
    vis += pL*0.25*((-cos(2.0*h0 - n) + cosN + 2.0*h0*sn) + (-cos(2.0*h1 - n) + cosN + 2.0*h1*sn));
    float hb = 0.5*(h0 + h1); vec3 od = normalize(ortho + vec3(1e-5));   // the slice's bent direction: halfway between its horizons
    gBent += (V*cos(hb) + od*sin(hb))*pL;
  }
  gBent = length(gBent) > 1e-4 ? normalize(gBent) : N;
  return clamp(vis/float(uSlices), 0.0, 1.0);
}
void main(){
  float z = texture(uDepth, vUV).r;
  if (z <= 0.0) { oColor = vec4(1.0); return; }   // sky
  vec3 P = vpos(vUV);
  vec3 px = vpos(vUV + vec2(uTexel.x, 0.0)) - P, nx = P - vpos(vUV - vec2(uTexel.x, 0.0));
  vec3 py = vpos(vUV + vec2(0.0, uTexel.y)) - P, ny = P - vpos(vUV - vec2(0.0, uTexel.y));
  vec3 dx = dot(px, px) < dot(nx, nx) ? px : nx, dy = dot(py, py) < dot(ny, ny) ? py : ny;
  vec3 N = normalize(cross(dx, dy));
  if (dot(N, P) > 0.0) N = -N;
  if (uGtao == 1) { float a = gtao(P, N); oColor = vec4(a, -P.z, gBent.x, gBent.y); return; }   // CP4: the bent normal's view x, y (z faces the eye)
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

// CP4 froxel fog (High / Ultra, perf switch froxel): a 160 x 90 x 64 view-space grid to uFroxFar m, slices exponential.
// inject: each cell's extinction (haze + ground-fog layer + fogLayer2 band) and in-scatter (fog colour + the sun through the
// far cascade, Henyey-Greenstein); integrate: front to back per column -> rgb in-scattered light, a transmittance to there.
static const char* kFroxInjectCS = R"GLSL(
#version 450 core
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(rgba16f, binding = 0) writeonly uniform image3D uInj;
uniform mat4 uInvVP; uniform vec3 uCam; uniform ivec3 uN; uniform float uNear, uFar;
uniform float uHazeD, uHazeH, uEnvFog, uEnvFogH, uEnvTop, uEnvSoft, uFogIndoor; uniform vec4 uFog2, uFogBox;
uniform sampler2D uFogField; uniform sampler2DShadow uShFar; uniform mat4 uVPFar; uniform vec2 uFarAx; uniform int uShOn;
uniform vec3 uSunDir, uSunCol, uAmb; uniform float uG, uAlb, uCloudK;
float band(float y, float lo, float hi, float s){ return smoothstep(lo - s, lo + s, y)*(1.0 - smoothstep(hi - s, hi + s, y)); }
void main(){
  ivec3 id = ivec3(gl_GlobalInvocationID.xy, gl_WorkGroupID.z);
  if (any(greaterThanEqual(id, uN))) return;
  vec2 uv = (vec2(id.xy) + 0.5)/vec2(uN.xy);
  vec4 p = uInvVP*vec4(uv*2.0 - 1.0, 0.5, 1.0); vec3 dir = normalize(p.xyz/p.w - uCam);
  float d = uNear*pow(uFar/uNear, (float(id.z) + 0.5)/float(uN.z));
  vec3 w = uCam + dir*d;
  float sig = uHazeD*exp(-max(w.y, 0.0)/uHazeH)*smoothstep(10.0, 14.0, d);   // the analytic haze's 12 m clear zone
  if (uEnvFog > 0.0) {
    vec4 F = texture(uFogField, (w.xz - uFogBox.xy)*uFogBox.zw);
    float e = uEnvFog*uFogIndoor*F.r*(0.6 + 0.8*F.g)*exp(-max(w.y - F.b, 0.0)/uEnvFogH);
    if (uEnvTop < 1e8) e *= smoothstep(uEnvTop + uEnvSoft, uEnvTop - uEnvSoft, w.y);
    if (uFog2.x > 0.0) e += uFog2.x*uFogIndoor*band(w.y, uFog2.y, uFog2.z, uFog2.w);
    sig += e;
  }
  float vis = 1.0;
  if (uShOn == 1) {
    vec4 lp = uVPFar*vec4(w, 1.0); vec3 s = lp.xyz/lp.w*0.5 + 0.5;
    if (all(greaterThan(s.xy, vec2(0.0))) && all(lessThan(s.xy, vec2(1.0))) && s.z < 1.0) vis = texture(uShFar, vec3(s.x*uFarAx.x + uFarAx.y, s.y, s.z - 0.002));
  }
  float cth = dot(dir, uSunDir), g = uG, hg = (1.0 - g*g)/(4.0*3.14159265*pow(max(1.0 + g*g - 2.0*g*cth, 1e-4), 1.5));
  vec3 L = uAmb + uSunCol*uAlb*hg*0.035*(1.0 - uCloudK)*vis;
  imageStore(uInj, id, vec4(L*sig, sig));
}
)GLSL";
static const char* kFroxIntegrateCS = R"GLSL(
#version 450 core
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(rgba16f, binding = 0) readonly uniform image3D uInj;
layout(rgba16f, binding = 1) writeonly uniform image3D uInt;
uniform ivec3 uN; uniform float uNear, uFar;
void main(){
  ivec2 xy = ivec2(gl_GlobalInvocationID.xy);
  if (any(greaterThanEqual(xy, uN.xy))) return;
  vec3 S = vec3(0.0); float T = 1.0, dPrev = 0.0;
  for (int k = 0; k < uN.z; ++k) {
    float d = uNear*pow(uFar/uNear, (float(k) + 1.0)/float(uN.z));   // the slice's far end
    vec4 v = imageLoad(uInj, ivec3(xy, k)); float sig = v.a, ds = d - dPrev;
    float tr = exp(-sig*ds);
    vec3 si = sig > 1e-7 ? v.rgb/sig*(1.0 - tr) : v.rgb*ds;
    S += T*si; T *= tr; dPrev = d;
    imageStore(uInt, ivec3(xy, k), vec4(S, T));
  }
}
)GLSL";
static const char* kCompositeFS = R"GLSL(
in vec2 vUV;
uniform sampler2D uScene, uShafts, uAmb, uAO, uDepth, uNR; uniform float uShaftAmt, uAoAmt, uSsrAmt;
uniform mat4 uInvProj, uProj, uView; uniform vec2 uAoTexel;
uniform float uContactAmt; uniform int uContactSteps;   // VO L1 screen-space sun contact shadows (0: off)
uniform int uAoNew;   // VO L2: multi-bounce and specular occlusion from the (GT)AO
uniform int uBent;    // CP4 bentnormal
uniform int uSsrHalf; uniform sampler2D uSsrTex; uniform vec2 uSsrTexel;
uniform int uFroxOn; uniform sampler3D uFrox; uniform float uFroxNear, uFroxFar, uFroxHazeD, uFroxHazeH; uniform vec3 uFroxAmb;   // CP4 froxel fog (unit 4)
uniform float uShimmer, uShimT;   // W5 wxshimmer: heat shimmer amplitude (px at this resolution, gated on the CPU), its clock   // OPT ssrhalf: the reflection added from a half-resolution pass (rgb: added light, a: view depth)
out vec4 oColor;
vec3 viewPosAt(vec2 uv){ float z = texture(uDepth, uv).r; vec4 p = uInvProj*vec4(uv*2.0 - 1.0, z, 1.0); return p.xyz/p.w; }   // reverse-Z
vec2 toUV(vec3 q){ vec4 c = uProj*vec4(q, 1.0); return c.xy/c.w*0.5 + 0.5; }
uniform float uSsrMaxT = 1e9;   // OPT ssrrange
uniform int uSsrClamp;   // FIX7 (perf switch ssrclamp)
uniform int uSsrEnvCube;   // 1 in the half-resolution SSR pass (it has the reflection cube bound)
uniform vec2 uSsrRough = vec2(0.1, 0.4);   // CP4 ssrfade: SSR fades out over roughness x..y (0.3..0.5; was 0.1..0.4)
vec4 traceSSR(vec3 Pv, vec3 Rv){
  float stepL = 0.22, t = 0.10, jit = pixelNoise();
  vec3 prev = Pv;
  for (int i = 0; i < 28; ++i) {
    if (t > uSsrMaxT) break;
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
// VO L1 (Aldridge, Bend Studio 2023): march the depth buffer toward the sun for ~0.4-1.6 m; an occluder in front of
// the ray within a thickness shadows the direct light the shadow map is too coarse for (grass, props on the ground, joints)
float contactShadow(vec2 uv){
  float z0 = texture(uDepth, uv).r;
  if (z0 <= 0.0) return 1.0;   // sky
  vec3 P = viewPosAt(uv);
  float dist = -P.z;
  if (dist > 90.0) return 1.0;
  vec3 L = normalize(mat3(uView)*uSunDir);
  if (L.z > 0.97) return 1.0;   // sun straight behind the camera: the ray leaves along the view (no information)
  float len = clamp(0.4 + dist*0.02, 0.4, 1.6), thick = 0.06 + dist*0.006;
  float n = float(uContactSteps), jit = pixelNoise();
  float occ = 0.0;
  for (int i = 0; i < uContactSteps; ++i) {
    float t = (float(i) + jit)/n;
    vec3 q = P + L*(len*t*t + 0.02 + dist*0.0015);   // denser near the surface; start a little off it (depth precision)
    vec2 u2 = toUV(q);
    if (u2.x < 0.0 || u2.x > 1.0 || u2.y < 0.0 || u2.y > 1.0) break;
    float dz = viewPosAt(u2).z - q.z;   // > 0: the visible surface is in front of the ray point
    if (dz > 0.005 && dz < thick) { occ = 1.0 - t*0.5; break; }
  }
  return 1.0 - occ*(1.0 - smoothstep(60.0, 90.0, dist));
}
vec3 ssrAdd(vec2 uv){   // the screen-space reflection's contribution here (geometry seen in it replaces the sky estimate)
  vec4 nr = texture(uNR, uv);
  if (nr.w >= uSsrRough.y || texture(uDepth, uv).r <= 0.0) return vec3(0.0);   // rough or sky
  vec3 Pv = viewPosAt(uv);
  vec3 Nv = normalize(mat3(uView)*nr.xyz);
  vec3 Vv = normalize(-Pv), Rv = reflect(-Vv, Nv);
  float F = 0.02 + 0.98*pow(1.0 - max(dot(Nv, Vv), 0.0), 5.0);
  vec4 hit = traceSSR(Pv, Rv);
  if (hit.w <= 0.0) return vec3(0.0);
  vec3 Rw = normalize(transpose(mat3(uView))*Rv);
  vec3 refl = textureLod(uScene, hit.xy, 0.0).rgb;
  vec3 env = skyColor(Rw);   // what the world pass reflected here, which the hit replaces
#ifdef NMK_SSR_HALF   // only this pass has the cube on unit 9: in the composite, a live uRefl (samplerCube) would share unit 9 with uShafts (2D) - invalid, every frame garbage
  if (uSsrEnvCube == 1 && uReflOn == 1) {   // the world pass used the L7 cube: subtract that, not the sky (a bright sky over a dim cube went below zero: black puddles)
    vec3 Pw = transpose(mat3(uView))*Pv + uCamPos; float wg; vec3 rp = reflProbe(Pw, Rw, nr.w, wg); env = mix(env, rp, wg);
  }
#endif
  vec3 sub = F*env;   // what the world pass reflected here, which the hit replaces
  if (uSsrClamp == 1) sub = min(sub, textureLod(uScene, uv, 0.0).rgb);   // FIX7 ssrclamp: never more than the pixel holds (shelter, weather, specular occlusion dim the world pass's
  // reflection below F x the sky / cube estimate; subtracting the estimate took sheltered and wet puddles below zero: black). Open-sky puddles: unchanged
  return (F*refl - sub)*hit.z*(1.0 - smoothstep(uSsrRough.x, uSsrRough.y, nr.w))*uSsrAmt;
}
void main(){
#ifdef NMK_SSR_HALF
  float zz = texture(uDepth, vUV).r;
  oColor = vec4(uSsrAmt > 0.0 ? ssrAdd(vUV) : vec3(0.0), zz > 0.0 ? -viewPosAt(vUV).z : 1e6); return;
#endif
  vec3 c = texture(uScene, vUV).rgb;
  if (uShimmer > 0.0) {   // W5 heat shimmer: rays that pass low over hot ground, 80 -> 400 m away, wobble mostly vertically
    float z = texture(uDepth, vUV).r;
    vec4 pf = uInvProj*vec4(vUV*2.0 - 1.0, z > 0.0 ? z : 1e-6, 1.0); vec3 pv = pf.xyz/pf.w;
    float dist = z > 0.0 ? -pv.z : 1e4;
    vec3 dw = transpose(mat3(uView))*normalize(pv);
    float band = 1.0 - smoothstep(0.0, 0.035, dw.y);   // below the horizon and ~2 deg above it
    float w = band*smoothstep(80.0, 400.0, dist);
    if (w > 0.0) {
      vec2 f = gl_FragCoord.xy;
      float n1 = vnoise(vec3(f.x*0.02, f.y*0.08 - uShimT*0.6*6.0, 1.0)) + 0.5*vnoise(vec3(f.x*0.04, f.y*0.16 - uShimT*1.1*6.0, 7.0)) - 0.75;
      float n2 = vnoise(vec3(f.x*0.02 + 17.0, f.y*0.08 - uShimT*0.6*6.0, 3.0)) - 0.5;
      vec2 o = vec2(n2*0.35, n1)*uShimmer*w/vec2(textureSize(uScene, 0));
      float zo = texture(uDepth, vUV + o).r; vec4 qo = uInvProj*vec4((vUV + o)*2.0 - 1.0, zo > 0.0 ? zo : 1e-6, 1.0);
      if (zo <= 0.0 || -qo.z/qo.w > 80.0) c = texture(uScene, vUV + o).rgb;   // nothing nearer than 80 m smears
    }
  }
  if (uContactAmt > 0.0) {   // VO L1: only the direct share (colour less the indirect light the world pass wrote) is shadowed
    vec3 amb = texture(uAmb, vUV).rgb;
    c = amb + max(c - amb, vec3(0.0))*mix(1.0, contactShadow(vUV), uContactAmt);
  }
  if (uSsrAmt > 0.0 && uSsrHalf == 1) {   // OPT ssrhalf: depth-aware 2x2 upsample of the half-resolution reflection
    float z0 = texture(uDepth, vUV).r;
    if (z0 > 0.0 && texture(uNR, vUV).w < uSsrRough.y) {
      float d0 = -viewPosAt(vUV).z; vec3 s = vec3(0.0); float ws = 0.0;
      for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x) {
        vec4 a = texture(uSsrTex, vUV + (vec2(x, y) - 0.5)*uSsrTexel);
        float k = exp(-abs(a.w - d0)/(0.03*d0 + 0.05)) + 1e-4;
        s += a.rgb*k; ws += k;
      }
      c = max(c + s/ws, vec3(0.0));
    }
  } else if (uSsrAmt > 0.0) c = max(c + ssrAdd(vUV), vec3(0.0));
  if (uAoAmt > 0.0) {
    // depth-aware 4x4 blur of the half-resolution AO, then remove the occluded share of the indirect light
    float z = texture(uDepth, vUV).r;
    vec4 p = uInvProj*vec4(vUV*2.0 - 1.0, z, 1.0); float d0 = -p.z/p.w;
    float s = 0.0, w = 0.0; vec2 bs = vec2(0.0);
    for (int y = -2; y < 2; ++y) for (int x = -2; x < 2; ++x) {
      vec2 o = (vec2(x, y) + 0.5)*uAoTexel;
      vec4 a = texture(uAO, vUV + o);
      float k = exp(-abs(a.g - d0)/(0.04*d0 + 0.05));
      s += a.r*k; w += k; bs += a.ba*k;
    }
    float ao = w > 1e-4 ? s/w : 1.0;
    vec3 amb = texture(uAmb, vUV).rgb;
    if (uBent == 1 && w > 1e-4 && z > 0.0) {   // CP4 bentnormal: sky / ground light along the open direction instead of the surface normal
      vec2 b2 = bs/w; vec3 bv = vec3(b2, sqrt(max(1.0 - dot(b2, b2), 0.0)));
      vec3 bw = normalize(transpose(mat3(uView))*bv), nw = texture(uNR, vUV).xyz;
      const vec3 lw = vec3(0.2126, 0.7152, 0.0722);
      float ib = dot(mix(uGroundCol, uSkyCol, 0.5 + 0.5*bw.y), lw), inn = dot(mix(uGroundCol, uSkyCol, 0.5 + 0.5*nw.y), lw);
      float kb = clamp(ib/max(inn, 1e-5), 0.6, 1.6);
      c += amb*(kb - 1.0); amb *= kb;
    }
    if (uAoNew == 1) {
      // VO L2 multi-bounce (Jimenez 2016 fit): light inter-reflected inside the occluded region, by the surface's
      // albedo (approximated as 0.4: the composite does not see albedo); occlusion stays darkest on dark materials
      const float al = 0.4; float a = 2.0404*al - 0.3324, b = -4.7951*al + 0.6417, cc = 2.7552*al + 0.6903;
      ao = max(ao, ((ao*a + b)*ao + cc)*ao);
      vec4 nr = texture(uNR, vUV);
      if (nr.w < 0.95 && z > 0.0) {   // specular occlusion (Lagarde 2014): the sky reflection estimate is occluded too
        vec3 Pv = viewPosAt(vUV), Nv = normalize(mat3(uView)*nr.xyz), Vv = normalize(-Pv);
        float NdV = clamp(dot(Nv, Vv), 0.0, 1.0), r = clamp(nr.w, 0.0, 1.0);
        float so = clamp(pow(NdV + ao, exp2(-16.0*r - 1.0)) - 1.0 + ao, 0.0, 1.0);
        vec3 Rw = normalize(transpose(mat3(uView))*reflect(-Vv, Nv));
        float F = 0.04 + 0.96*pow(1.0 - NdV, 5.0);
        vec3 envS = skyColor(Rw)*F*(1.0 - r*0.6)*0.6;
        c -= min(envS*(1.0 - so)*uAoAmt, c*0.5);
      }
    }
    c -= amb*(1.0 - ao)*uAoAmt;
  }
  if (uFroxOn == 1) {   // CP4 froxel fog: the integrated volume at this pixel's distance; past its far end the analytic haze
    float zf = texture(uDepth, vUV).r;
    if (zf > 0.0) {
      vec3 pv = viewPosAt(vUV); float dist = length(pv);
      float t = log(max(dist, uFroxNear)/uFroxNear)/log(uFroxFar/uFroxNear);
      vec4 fv = texture(uFrox, vec3(vUV, clamp(t - 0.5/64.0, 0.0, 1.0)));
      c = c*fv.a + fv.rgb;
      if (dist > uFroxFar) {
        vec3 wy = transpose(mat3(uView))*pv; float y = 0.5*wy.y + uCamPos.y;
        float tr = exp(-uFroxHazeD*exp(-max(y, 0.0)/uFroxHazeH)*(dist - uFroxFar));
        c = c*tr + uFroxAmb*(1.0 - tr);
      }
    }
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
uniform int uUp; uniform vec2 uInTexel, uJit;
uniform int uTaaVel, uTaaBox;   // CP1: velocity from the closest depth in 3x3 (taavel); history also clamped to the 3x3 min / max box (taabox)
uniform int uTaaDbg;   // NMK_TAA_DEBUG: 1 uv, 2 texelFetch(uCur), 3 uInTexel x 1000 / uUp, 4 texture(uCur) (Metal shim checks)   // P1 TAAU: 1 = this pass runs at output size; the internal texel; the frame's jitter (internal px)
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
  if (uTaaDbg > 0) {
    vec4 d = uTaaDbg == 1 ? vec4(vUV*4.0, 0.0, 1.0) : uTaaDbg == 2 ? texelFetch(uCur, ivec2(vUV*vec2(textureSize(uCur, 0))), 0)
           : uTaaDbg == 3 ? vec4(uInTexel*1000.0, float(uUp), 1.0) : texture(uCur, vUV);
    oColor = d; oHist = d; return;
  }
  vec3 cur, m1 = vec3(0.0), m2 = vec3(0.0), sum = vec3(0.0);
  vec2 dUV = vUV; float conf = 1.0;
  if (uUp == 1) {
    // TAAU: the internal texel c holds the scene at the unjittered screen position c + jitter (projJ moves the image by -jitter). This output pixel takes a
    // Gaussian of the 3x3 internal samples by their distance to it; conf = how close the nearest sample fell (history
    // carries the rest). The clip box and the sharpen reference are the same 3x3 internal samples.
    vec2 p = vUV/uInTexel; ivec2 b = ivec2(floor(p - uJit)); ivec2 mx = textureSize(uCur, 0) - 1;
    vec3 acc = vec3(0.0); float wsum = 0.0, wmax = 0.0;
    for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) {
      ivec2 t = clamp(b + ivec2(x, y), ivec2(0), mx);
      vec3 s = tm(texelFetch(uCur, t, 0).rgb);
      vec2 d = vec2(b + ivec2(x, y)) + 0.5 + uJit - p;
      float w = exp(-2.0*dot(d, d));
      acc += s*w; wsum += w; wmax = max(wmax, w);
      vec3 yc = toYC(s); m1 += yc; m2 += yc*yc; sum += s;
    }
    cur = acc/max(wsum, 1e-4); sum *= 8.0/9.0; conf = wmax;
    dUV = (vec2(clamp(b, ivec2(0), mx)) + 0.5)*uInTexel;   // depth / motion of the nearest internal sample
  } else {
    cur = tm(texture(uCur, vUV).rgb);
    for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) {
      vec3 s = tm(texture(uCur, vUV + vec2(x, y)*uTexel).rgb);
      vec3 yc = toYC(s); m1 += yc; m2 += yc*yc;
      if (x != 0 || y != 0) sum += s;
    }
  }
  m1 /= 9.0; vec3 sig = sqrt(max(m2/9.0 - m1*m1, 0.0));
  vec3 bmn = vec3(1e9), bmx = vec3(-1e9);
  if (uTaaBox == 1) for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) {
    vec3 s = toYC(uUp == 1 ? tm(texelFetch(uCur, clamp(ivec2(dUV/uInTexel) + ivec2(x, y), ivec2(0), textureSize(uCur, 0) - 1), 0).rgb) : tm(texture(uCur, vUV + vec2(x, y)*uTexel).rgb));
    bmn = min(bmn, s); bmx = max(bmx, s);
  }
  if (uTaaVel == 1) {   // the nearest surface in 3x3 (reverse-Z: largest depth) carries its motion onto silhouette edges
    vec2 tx = uUp == 1 ? uInTexel : uTexel; vec2 best = dUV; float bz = texture(uDepth, dUV).r;
    for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) { vec2 o = dUV + vec2(x, y)*tx; float zz = texture(uDepth, o).r; if (zz > bz) { bz = zz; best = o; } }
    dUV = best;
  }
  float z = texture(uDepth, dUV).r;   // (TAAU's output depth: kDepthUpFS after this pass; a depth write here broke the Metal pipeline)
  vec4 wp = uInvVP*vec4(vUV*2.0 - 1.0, z, 1.0);   // reverse-Z: ndc z = depth; the sky (0) lands on the far plane
  vec3 w = wp.xyz/wp.w;
  vec4 pc = uPrevVP*vec4(w, 1.0); vec2 puv = pc.xy/pc.w*0.5 + 0.5;
  vec4 mv = texture(uVel, dUV);
  if (mv.a > 0.5) puv = vUV + mv.xy;   // animated foliage / grass: their own motion, not the depth reprojection
  vec3 res = cur;
  if (uValid == 1 && puv.x > 0.0 && puv.x < 1.0 && puv.y > 0.0 && puv.y < 1.0) {
    vec3 h = toYC(tm(histCR(puv)));
    float motion = length((puv - vUV)/uTexel);
    // FL1: a still pixel gets a wider clip box (2.25 sigma): sub-pixel bars, wires, pickets and blade tips show up in only
    // some jitter positions, and the tight box clipped their history away every frame, so they sparkled with the camera
    // standing still. Under motion it stays tight against ghosting.
    // L3: swaying grass / leaves narrow the box over 1 px of their own motion (was a flat 1.75 even standing still: leaf-card
    // edges against the sky, sub-pixel and on in only some jitter positions, had their history clipped away and shimmered)
    float gam = mix(2.25, 1.1, clamp(motion/(mv.a > 0.5 ? 1.0 : 1.5), 0.0, 1.0));
    vec3 lo = m1 - gam*sig, hi = m1 + gam*sig;
    vec3 ctr = 0.5*(lo + hi), ext = max(0.5*(hi - lo), vec3(1e-4));   // clip toward the box centre
    vec3 d = h - ctr; vec3 a = abs(d/ext); float mx = max(a.x, max(a.y, a.z));
    if (mx > 1.0) h = ctr + d/mx;
    if (uTaaBox == 1) h = clamp(h, bmn, bmx);
    // L3: a still pixel keeps more history (0.05, was 0.08): sub-pixel sky gaps in a canopy are sampled in only some jitter
    // positions, and the blend's own noise (~ sqrt(alpha)) was what still flickered; from 1 px of motion as before
    float alpha = motion < 1.0 ? mix(0.05, 0.08, motion) : mix(0.08, 0.30, clamp(motion/16.0, 0.0, 1.0));
    if (uUp == 1) {   // TAAU: a sample far from this pixel counts less; wind-animated pixels (approximate motion) keep less history
      alpha *= 0.25 + 1.5*conf;
      if (mv.a > 0.5) alpha = max(alpha, 0.2*clamp(motion*2.0, 0.0, 1.0));   // L3: only once they really move (0.5 px+): still / far canopies keep their history
    }
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
// P1 Mac hook helpers. kMotionFS: full motion in input pixels toward the previous position (uv delta x size); animated
// foliage / grass (velTex a = 1) carry their own, the rest is the camera reprojection of the depth with the unjittered
// matrices (as kTaaFS). kDepthUpFS: the nearest internal depth into the output-size target (effects, overlays).
static const char* kMotionFS = R"GLSL(
in vec2 vUV;
uniform sampler2D uDepth, uVel; uniform mat4 uInvVP, uPrevVP; uniform vec2 uSize; uniform float uSignY;
layout(location=0) out vec4 oMotion;
void main(){
  vec4 mv = texture(uVel, vUV); vec2 puv;
  if (mv.a > 0.5) puv = vUV + mv.xy;
  else {
    vec4 wp = uInvVP*vec4(vUV*2.0 - 1.0, texture(uDepth, vUV).r, 1.0);
    vec4 pc = uPrevVP*vec4(wp.xyz/wp.w, 1.0); puv = pc.xy/pc.w*0.5 + 0.5;
  }
  vec2 d = (puv - vUV)*uSize; d.y *= uSignY;
  oMotion = vec4((any(isnan(d)) || any(isinf(d))) ? vec2(0.0) : d, 0.0, 1.0);
}
)GLSL";
static const char* kDepthUpFS = R"GLSL(
in vec2 vUV;
uniform sampler2D uDepth; uniform vec2 uInSize;
void main(){ gl_FragDepth = texelFetch(uDepth, clamp(ivec2(vUV*uInSize), ivec2(0), ivec2(uInSize) - 1), 0).r; }
)GLSL";
// ---- bloom: a downsample chain (first level de-fireflied by a luminance-weighted average) and a tent upsample
// accumulated back up; the tonemap adds a small fraction. Wide and faint: a soft glow, no threshold streaks.
static const char* kBloomDownFS = R"GLSL(
in vec2 vUV;
uniform sampler2D uSrc; uniform vec2 uSrcTexel; uniform int uFirst, uDown13;
out vec4 oColor;
vec3 kw(vec3 a, vec3 b, vec3 c, vec3 d, int first){   // a 2x2 block; the first level weighs by 1 / (1 + luma) (Karis: no fireflies)
  if (first == 0) return (a + b + c + d)*0.25;
  float wa = 1.0/(1.0 + dot(a, vec3(0.2126, 0.7152, 0.0722))), wb = 1.0/(1.0 + dot(b, vec3(0.2126, 0.7152, 0.0722)));
  float wc = 1.0/(1.0 + dot(c, vec3(0.2126, 0.7152, 0.0722))), wd = 1.0/(1.0 + dot(d, vec3(0.2126, 0.7152, 0.0722)));
  return (a*wa + b*wb + c*wc + d*wd)/(wa + wb + wc + wd);
}
void main(){
  if (uDown13 == 1) {   // VO L8: Jimenez 2014's 13-tap downsample (five overlapping 2x2 boxes): no aliasing / flicker in the glow
    vec2 o = uSrcTexel;
    vec3 A = texture(uSrc, vUV + o*vec2(-2, -2)).rgb, B = texture(uSrc, vUV + o*vec2(0, -2)).rgb, C = texture(uSrc, vUV + o*vec2(2, -2)).rgb;
    vec3 D = texture(uSrc, vUV + o*vec2(-1, -1)).rgb, E = texture(uSrc, vUV + o*vec2(1, -1)).rgb, F = texture(uSrc, vUV + o*vec2(-2, 0)).rgb;
    vec3 G = texture(uSrc, vUV).rgb, H = texture(uSrc, vUV + o*vec2(2, 0)).rgb, I = texture(uSrc, vUV + o*vec2(-1, 1)).rgb;
    vec3 J = texture(uSrc, vUV + o*vec2(1, 1)).rgb, K = texture(uSrc, vUV + o*vec2(-2, 2)).rgb, L = texture(uSrc, vUV + o*vec2(0, 2)).rgb, M = texture(uSrc, vUV + o*vec2(2, 2)).rgb;
    vec3 c = kw(D, E, I, J, uFirst)*0.5 + (kw(A, B, F, G, uFirst) + kw(B, C, G, H, uFirst) + kw(F, G, K, L, uFirst) + kw(G, H, L, M, uFirst))*0.125;
    oColor = vec4((any(isnan(c)) || any(isinf(c))) ? vec3(0.0) : min(c, vec3(64.0)), 1.0); return;
  }
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
layout(location=5) in float aTone;   // L2: leaf tone, dark -1 .. light 1 (foliage.cpp; ivy: no stream, 0)
uniform mat4 uModel, uVP; uniform float uTime, uWind;
uniform mat4 uVPU, uPrevVP; uniform float uPrevTime;   // motion vectors (TAA): unjittered current / previous
out vec3 vW, vN; out vec2 vUV; out float vM, vShade; flat out float vSeed; flat out float vTone;
flat out float vLayer; out vec3 vLN;   // L1: leaf texture layer (-1: the procedural ovals), the leaf plane's normal
out vec4 vCC, vPC;
invariant gl_Position;
vec3 leafSway(vec3 w, bool prev, float m){
  const float A = 0.10, f = 1.3, kx = 0.21, kz = 0.17; vec3 D = windDir(); float t = windClock(prev);
  float s = sin(f*t + kx*w.x + kz*w.z) + 0.3*sin(2.9*t + 0.8*w.x - 0.5*w.y);
  return (D*A*s + vec3(0.0, 0.25*A*s, 0.0)) * m * windSway(uWind) * windGust(w.xz, prev);
}
void main(){
  vec4 w0 = uModel*vec4(aPos, 1.0), w = w0;
  w.xyz += leafSway(w0.xyz, false, aMask);
  vW = w.xyz; vN = normalize(mat3(uModel)*aNrm); vUV = aUV; vM = aMask;
  vSeed = floor(dot(aPos, vec3(13.1, 7.3, 11.7)));
  float lk = floor(aShade*0.5);   // IV1: shade + 2 (layer + 1) = a leaves.json layer (wall ivy); tree cards stay below 2
  vShade = aShade - 2.0*lk; vLayer = lk - 1.0; vLN = vN; vTone = aTone;
  vCC = uVPU*w; vPC = uPrevVP*vec4(w0.xyz + leafSway(w0.xyz, true, aMask), 1.0);
  gl_Position = uVP*w;   // invariant: the PF2 canopy depth prepass (leaf_shadow) and the GL_EQUAL shading pass must agree
}
)GLSL";
// L1 species leaves (foliage.cpp, docs/leaves.md): instanced sprig meshes. Per vertex: card-space position (x, y in
// [-1, 1] over the card's T / B half extents, z along its normal), leaf normal in the same frame, layer uv, layer.
// Per instance (one canopy card): centre, T*s, B*s, cluster direction, mask / shade / seed. Same sway and outputs as kLeafVS.
static const char* kLeafInstVS = R"GLSL(
layout(location=0) in vec3 aC; layout(location=1) in vec3 aN; layout(location=2) in vec2 aUV; layout(location=3) in float aLayer;
layout(location=4) in vec3 iCtr; layout(location=5) in vec3 iT; layout(location=6) in vec3 iB; layout(location=7) in vec3 iDir; layout(location=8) in vec4 iMSS;   // mask, shade, seed, L2 tone
uniform mat4 uModel, uVP; uniform float uTime, uWind;
uniform float uCardScale;   // DR1 farleaf: the far tiers keep a share of the cards and grow them (1 near)
uniform mat4 uVPU, uPrevVP; uniform float uPrevTime;
out vec3 vW, vN; out vec2 vUV; out float vM, vShade; flat out float vSeed; flat out float vTone;
flat out float vLayer; out vec3 vLN;
out vec4 vCC, vPC;
invariant gl_Position;
vec3 leafSway(vec3 w, bool prev, float m){
  const float A = 0.10, f = 1.3, kx = 0.21, kz = 0.17; vec3 D = windDir(); float t = windClock(prev);
  float s = sin(f*t + kx*w.x + kz*w.z) + 0.3*sin(2.9*t + 0.8*w.x - 0.5*w.y);
  return (D*A*s + vec3(0.0, 0.25*A*s, 0.0)) * m * windSway(uWind) * windGust(w.xz, prev);
}
void main(){
  vec3 n = cross(iT, iB)/max(length(iT), 1e-6);   // card normal, same length as iT / iB
  vec4 w0 = uModel*vec4(iCtr + (iT*aC.x + iB*aC.y)*uCardScale + n*aC.z, 1.0), w = w0;
  w.xyz += leafSway(w0.xyz, false, iMSS.x);
  vec3 ln = normalize(iT*aN.x + iB*aN.y + n*aN.z);
  if (dot(ln, iDir) < 0.0) ln = -ln;               // two-sided: the face toward the cluster's outside
  vW = w.xyz; vLN = normalize(mat3(uModel)*ln);
  vN = normalize(mat3(uModel)*normalize(iDir*0.7 + ln*0.3));   // the crown's volume first, the leaf's own tilt second
  vUV = aUV; vM = iMSS.x; vShade = iMSS.y; vSeed = iMSS.z; vTone = iMSS.w; vLayer = aLayer;
  vCC = uVPU*w; vPC = uPrevVP*vec4(w0.xyz + leafSway(w0.xyz, true, iMSS.x), 1.0);
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
in vec3 vW, vN; in vec2 vUV; in float vM, vShade; flat in float vSeed; flat in float vTone;
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
  if ((vLayer < 0.0 ? leafAlpha(vUV, vSeed) : tx.a) < cutRef()) discard;
  vec3 N = normalize(vN);
  float var = hash12(vec2(vSeed, 2.0));
  // textured: uLeafCol is the asset's leafColor / the species mean, so the crown keeps its authored colour
  vec3 alb = vLayer < 0.0 ? uLeafCol*(0.84 + 0.32*var)*vShade : tx.rgb*uLeafCol*(0.92 + 0.16*var)*vShade;   // L2b: +-8 % per card (lighter and darker leaves side by side)
  // L2 / L2b tone inside the crown (foliage.cpp cardTone, about -1.25 .. 1.25): darker and a touch cooler in the interior, the
  // underside and the dark clumps; lighter and a touch warmer on the sunlit top, the outside and the light clumps (about +-20-30 % value)
  alb *= (1.0 + 0.34*vTone)*mix(vec3(0.955, 0.99, 1.06), vec3(1.05, 1.01, 0.9), clamp(0.5 + 0.4*vTone, 0.0, 1.0));
  alb = greenGrade(alb, uGreen.a, uGreenK.x);   // FL1: spring green (hedges, ferns, card shrubs)
  alb = mix(alb, alb*vec3(1.08, 0.95, 0.7), hash12(vec2(vSeed, 5.0))*0.35);   // some yellowed / olive leaves
  // seasonal variation: whole crowns drift fresher or yellower-olive on a slow world-space noise (neighbouring trees
  // differ) and clusters inside a crown vary a little, so a row of one species never reads as copies
  float sv = vnoise(vec3(vW.xz*0.045, 3.7))*0.7 + vnoise(vW*0.4)*0.3;
  alb *= mix(vec3(0.9, 1.04, 0.92), vec3(1.1, 1.0, 0.78), smoothstep(0.25, 0.75, sv));
  float sh = 1.0;
  if (uShadowSoft > 0.0) sh = sunShadowT(vW + N*0.06, N, 1.0, 8) * cloudShadow(vW);   // soft: thin leaves blur the map
  else if (uShadowOn == 1) { vec4 lp = uLightVP*vec4(vW + N*0.12, 1.0); vec3 sc = lp.xyz/lp.w*0.5 + 0.5;
    if (sc.x > 0.0 && sc.x < 1.0 && sc.y > 0.0 && sc.y < 1.0 && sc.z < 1.0) sh = texture(uShadow, vec3(sc.x*uShadowAtlas, sc.y, sc.z - 0.002)); }
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
void main(){ if (leafCut(vUV, vSeed, vLayer) < cutRef()) discard; }   // the same cutout as the shaded leaves (cutRef: 0.5 in the shadow passes; the colour pass's in the main prepass)
)GLSL";

static const char* kShadowVS = R"GLSL(
#version 450 core
layout(location=0) in vec3 aPos;
uniform mat4 uModel, uVP;
void main(){ gl_Position = uVP*uModel*vec4(aPos,1.0); }
)GLSL";
// P2 instdepth: kShadowVS with the model matrix per instance (VAO binding 3, attributes 12-15, divisor 1)
static const char* kShadowIVS = R"GLSL(
#version 450 core
layout(location=0) in vec3 aPos;
layout(location=12) in vec4 iM0; layout(location=13) in vec4 iM1; layout(location=14) in vec4 iM2; layout(location=15) in vec4 iM3;
uniform mat4 uVP;
void main(){ gl_Position = uVP*mat4(iM0, iM1, iM2, iM3)*vec4(aPos,1.0); }
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
layout(location=4) in float aMorph;   // VOT T1: the next LOD's height minus this vertex's (base chunks; 0 elsewhere)
uniform mat4 uVP;
uniform vec4 uMorph;                  // VOT T1 geomorph: x start distance, y 1 / ramp length, z on
out vec3 vW, vN0; out float vDirt; out vec3 vEw;
invariant gl_Position;                // PF tzprepass: the depth pass and the colour pass must rasterise identical depths
void main(){
  vec3 P = aPos;
  if (uMorph.z > 0.5) P.y += aMorph*clamp((length(aPos - uCamPos) - uMorph.x)*uMorph.y, 0.0, 1.0);
  vW = P; vN0 = aNrm; vDirt = aDirt; vEw = aEw; gl_Position = uVP*vec4(P,1.0);
}
)GLSL";
// PF tzprepass: the terrain's depth-only pass (kTerrainVS, whose gl_Position is invariant, so the colour pass's EQUAL test
// matches it exactly); it alone discards the base under active tiles
static const char* kTerrainDepthFS = R"GLSL(
in vec3 vW, vN0; in float vDirt; in vec3 vEw;
uniform int uDiscard; uniform sampler2D uTileMask; uniform float uTileSize; uniform vec2 uTerrainOrg;
void main(){
  if (uDiscard == 1 && texelFetch(uTileMask, ivec2(floor((vW.xz - uTerrainOrg)/uTileSize)), 0).r > 0.5) discard;
}
)GLSL";
static const char* kTerrainFS = R"GLSL(
uniform vec4 uGrassTop; uniform vec2 uGrassTopR;   // CP4: clumps' mean top-down albedo (linear), strength; ramp start / end (m)
in vec3 vW, vN0; in float vDirt; in vec3 vEw;   // vEw: GX1 earthworks (terrain_cuts.cpp): packed mud, freshly turned earth, wet
uniform int uTNOn; uniform sampler2D uTNrm; uniform vec4 uTNMap;   // VOT T1: per-pixel base normals (terrain_normals.cpp, unit 18), xy origin, zw 1 / extent
uniform int uTSVOn;   // uTSV is declared with the shadow code (T6)   // VOT T5: terrain sky-view factor (unit 26, same mapping as uTNMap)
vec3 vN;   // the shading normal: vN0 (vertex) or the baked per-pixel one
uniform int uGrid; uniform vec2 uBuildHalf;
uniform int uDiscard; uniform sampler2D uTileMask; uniform float uTileSize; uniform vec2 uTerrainOrg, uTerrainExt;
uniform vec4 uDust;   // W5 tdust: xy wind direction (toward), z streamer strength (0: calm or not arid), w advection phase (m along the wind)
uniform vec4 uTPom;   // VFX rev3 4.4 tpom: x depth (m), y steps (1 = one offset), z fade range (m), w the ground sets' uv per metre
uniform sampler2D uSurf;   // R wear, G wet, B route kind, A rut (surface.cpp)
uniform float uTerrCave;   // CV1: look.cave: dark wet glossy floors
uniform int uPudRelief;   // FIX7 (perf switch pudrelief)
uniform int uDbgTerr;      // GG1 (NMK_DBG_TERRAIN): 1 roughness classes (red < 0.4 SSR mirror, yellow < 0.7 sheen, grey matte), 2 specular share (red > 25 %, yellow > 10 %)
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
  vN = vN0;
  if (uTNOn == 1) { vec2 r = texture(uTNrm, (vW.xz - uTNMap.xy)*uTNMap.zw).rg; vN = vec3(r.x, sqrt(max(1.0 - dot(r, r), 0.0)), r.y); }   // VOT T1
  vec3 p = vW;
  vec3 tdx = dFdx(p), tdy = dFdy(p), tn0 = normalize(vN); float tDist = length(p - uCamPos);   // L4 terrain layers (before any discard)
  if (uTPom.x > 0.0) {   // VFX rev3 4.4 tpom: parallax occlusion on the ground layers, through texTap's march (the materials code);
    // tdx / tdy stay the derivatives of the unshifted p. Depth fades over the last 30 % of the range and at grazing views
    gPomV = normalize(uCamPos - p); gPomN = int(uTPom.y + 0.5);
    gPomK = uTPom.x*uTPom.w*(1.0 - smoothstep(uTPom.z*0.7, uTPom.z, tDist))*smoothstep(0.08, 0.3, abs(dot(tn0, gPomV)));
  }
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
  grass *= mix(1.0, mix(0.5, 0.85, clump), 0.5 + 0.5*nearK);
  if (uAlbClamp == 1) { float gl = dot(grass, vec3(0.2126, 0.7152, 0.0722)); if (gl < 0.10) grass *= 0.10/max(gl, 1e-4); }   // CP step 4: the clump shading reached 0.08 (corpus: grass 0.15-0.25, never under 0.10)                                             // shadowed sward depth between plants
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
  TerrT tI = TerrT(vec3(1.0), 1.0, 1.0, vec3(0.0), 0.5), tG = tI, tS = tI, tV = tI, tA = tI;
  float wGrav = smoothstep(0.75, 1.0, kind), wSand = 1.0 - smoothstep(0.07, 0.16, abs(kind - 0.251)), wSoil = (1.0 - wGrav)*(1.0 - wSand);
  bool under = paintCover(p) < 0.995 || vDirt > 0.15;   // CV1: full ground paint replaces the layers below it: take no samples there
  const bool snowAll = uOpt2Off == 0 && snowFullCover(p, normalize(vN));   // OPT2 (weather 05 s4.7): under certain full snow cover the substrate's taps are wasted (applySnow covers them)
  under = under && !snowAll;
  float wTr = max(wSoil + wGrav*(1.0 - wSand) + wSand, 1e-4);   // OPT2: each layer's share of the pixel (gLayerW: hex only on a dominant layer)
  if (under && tw < 0.995) { gLayerW = 1.0 - tw; tG = terrTex(0, p, tn0, tdx, tdy, tDist); }
  if (under && tw > 0.005 && wSoil > 0.01) { gLayerW = tw*wSoil/wTr; tS = terrTex(1, p, tn0, tdx, tdy, tDist); }
  if (under && tw > 0.005 && wGrav > 0.01) { gLayerW = tw*wGrav*(1.0 - wSand)/wTr; tV = terrTex(3, p, tn0, tdx, tdy, tDist); }
  if (under && tw > 0.005 && wSand > 0.01) { gLayerW = tw*wSand/wTr; tA = terrTex(4, p, tn0, tdx, tdy, tDist); }
  gLayerW = 1.0;
  float wT = max(wSoil + wGrav*(1.0 - wSand) + wSand, 1e-4);
  TerrT tTr = TerrT((tS.alb*wSoil + tV.alb*wGrav*(1.0 - wSand) + tA.alb*wSand)/wT, (tS.r*wSoil + tV.r*wGrav*(1.0 - wSand) + tA.r*wSand)/wT,
                    (tS.ao*wSoil + tV.ao*wGrav*(1.0 - wSand) + tA.ao*wSand)/wT, (tS.dn*wSoil + tV.dn*wGrav*(1.0 - wSand) + tA.dn*wSand)/wT, (tS.h*wSoil + tV.h*wGrav*(1.0 - wSand) + tA.h*wSand)/wT);
  grass *= tG.alb; track *= tTr.alb;
  // CP4 grasstint (corpus 09 s7.4): where the blades thin out (from the near radius to the grass radius) the turf takes the
  // clumps' mean top-down colour, so a lower density reads as the same field instead of greener / browner ground showing through
  if (uGrassTop.a > 0.0) grass = mix(grass, uGrassTop.rgb, uGrassTop.a*smoothstep(uGrassTopR.x, uGrassTopR.y, length(vW.xz - uCamPos.xz)));
  float twH = hbK(tw, tG.h, tTr.h), hB = mix(tG.h, tTr.h, twH);   // M2: the grass / track edge by height; hB the ground's height so far
  vec3 tdn = mix(tG.dn, tTr.dn, twH); float trr = mix(tG.r, tTr.r, twH), tao = mix(tG.ao, tTr.ao, twH);
  vec3 alb = mix(grass, track, twH);
  // W5b: slope material. Soil and scree from ~28 to ~42 deg, bare rock above ~45 deg (cliffs, quarry and ravine
  // walls): grey-brown limestone and darker schist in beds, bedding lines, vertical joints, lichen on ledges.
  // World-space 3D noise, so vertical faces are textured, not stretched. Wear / route / rut / wet still apply below.
  float rockK = 0.0, screeK = 0.0, rCrack = 0.0; vec3 rBump = vec3(0.0);
  float sny = normalize(vN).y + (nM - 0.5)*0.07;                       // band edges wander
  float ewK = clamp(vEw.x + vEw.y, 0.0, 1.0);                           // GX1: dug earth is never rock or scree
  float ewRock = 1.0 - smoothstep(0.0, 0.12, ewK);                       // (any earthworks weight at all suppresses it)
  screeK = smoothstep(0.89, 0.75, sny)*ewRock;
  if (screeK > 0.001 && uTerrCave > 0.5) {   // CV1: cave walls everywhere on screen: the same palette, a fraction of the noise (one texture tap)
    rockK = smoothstep(0.75, 0.62, sny)*ewRock;
    float bedV = vnoise(vec3(p.xz*0.05, p.y*0.35 + 40.0)), mott = vnoise(vec3(p*0.7 + 45.0));
    float flute = vnoise(vec3(p.x*1.4, p.y*0.07, p.z*1.4) + 60.0);
    vec3 rock = mix(vec3(0.30,0.28,0.25), vec3(0.44,0.41,0.35), bedV) * (0.78 + 0.4*mott) * (0.78 + 0.38*flute);
    TerrT tRk = terrTex(5, p, tn0, tdx, tdy, tDist); rock *= tRk.alb;
    tdn = mix(tdn, tRk.dn, rockK); trr = mix(trr, tRk.r, rockK); tao = mix(tao, tRk.ao, rockK);
    alb = mix(alb, rock, max(rockK, screeK*0.8));
    rBump = vec3(mott - 0.5, (flute - 0.5)*0.4, bedV - 0.5)*0.8;
  } else if (screeK > 0.001) {
    float rBreak = fbm(vec3(p.xz*0.21, p.y*0.21 + 40.0));
    rockK = smoothstep(0.75, 0.62, sny + (rBreak - 0.5)*0.16)*ewRock;
    float lith = smoothstep(0.35, 0.65, fbm(vec3(p.xz*0.018, p.y*0.03 + 41.0)));              // limestone <-> schist
    float warp = fbm(vec3(p.xz*0.06, p.y*0.02 + 42.0))*5.0;
    float bedY = p.y*0.42*(0.7 + 0.6*fbm(vec3(p.xz*0.011, 61.0))) + warp;                       // ~1.6-3.4 m beds, thicker / thinner across the face
    float bedV = fract(sin(floor(bedY)*91.7)*4375.85);                                         // per-bed tone
    float farK = 1.0 - smoothstep(35.0, 140.0, tDist);                                       // VOT: fine lines read up close; from 100-400 m only the masses
    float bedLine = (1.0 - smoothstep(0.0, 0.07, min(fract(bedY), 1.0 - fract(bedY))))
                  * step(0.48, fract(sin(floor(bedY)*37.3 + floor(p.x*0.02 + p.z*0.02)*11.1)*917.3))*farK;   // bedding planes: about half show, by stretches
    float thin = smoothstep(0.82, 0.97, sin((p.y + warp*0.4)*7.5)*0.5 + 0.5);                  // fine laminae (schist)
    float jx = (p.x*0.62 + p.z*0.78)*(0.17 + 0.12*vnoise(vec3(p.xz*0.012, 62.0))) + (vnoise(vec3(p.xz*0.05, p.y*0.07 + 43.0)) - 0.5)*1.1;   // sub-vertical joints 3.4-6 m apart, wandering
    rCrack = (1.0 - smoothstep(0.012, 0.04, abs(fract(jx) - 0.5))) * step(0.62, vnoise(vec3(floor(jx)*1.7, floor(bedY*0.5)*1.3, 52.0)))*mix(0.35, 1.0, farK);   // fewer; some run, some stop at a bed
    float grain = vnoise(vec3(p*3.1 + 44.0)), mott = fbm(vec3(p*0.6 + 45.0));
    vec3 lime = mix(vec3(0.44,0.40,0.33), vec3(0.57,0.52,0.43), bedV) * (0.74 + 0.42*mott);
    vec3 schist = mix(vec3(0.24,0.23,0.21), vec3(0.34,0.31,0.27), bedV) * (0.78 + 0.36*mott) * (1.0 - 0.25*thin*lith);
    vec3 rock = mix(lime, schist, lith) * (0.9 + 0.2*grain);
    float macroR = fbm(vec3(p*0.012 + 63.0)), macroC = fbm(vec3(p*0.006 + 64.0));               // VOT: 80-170 m masses, so a whole face is not one tone
    rock *= 0.7 + 0.6*macroR; rock = mix(rock, rock*vec3(1.08, 1.0, 0.88), smoothstep(0.4, 0.7, macroC));   // lighter / warmer and darker / greyer reaches
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
  if (under && mudK > 0.01) { gLayerW = mudK; TerrT tM = terrTex(2, p, tn0, tdx, tdy, tDist); gLayerW = 1.0; mudK = hbK(mudK, hB, tM.h); mud *= tM.alb; tdn = mix(tdn, tM.dn, mudK); trr = mix(trr, tM.r, mudK); tao = mix(tao, tM.ao, mudK); }   // L4; M2: mud settles in the lows
  alb = mix(alb, mud, mudK);
  // crater and disturbed soil
  float dirt = clamp(vDirt, 0.0, 1.0);
  float dirtK = smoothstep(0.15, 0.75, dirt + (nM - 0.5)*0.35);
  if (dirtK > 0.01 && !snowAll) { gLayerW = dirtK; TerrT tC = terrTex(7, p, tn0, tdx, tdy, tDist); gLayerW = 1.0; dirtK = hbK(dirtK, hB, tC.h); soil *= tC.alb; tdn = mix(tdn, tC.dn, dirtK); trr = mix(trr, tC.r, dirtK); tao = mix(tao, tC.ao, dirtK); }   // L4
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
  vec3 albPrePaint = alb;
  PaintOut po = paintApply(p, normalize(vN), alb, dirt, dFdx(p), dFdy(p), length(p - uCamPos));   // lasso ground paint: albedo (ground_paint.cpp)
  // cliffs keep their rock (world-space, triplanar): ground paint is ground cover, and its planar sets and xz noise smear
  // into vertical streaks on a steep face (castle spur, quarry walls), so it fades out where the slope material is rock
  if (rockK > 0.0) { alb = mix(alb, albPrePaint, rockK); po.w *= 1.0 - rockK; }
  // localized wetness: darker, and glossy only where water stands (puddles follow ruts / lows)
  float puddle = smoothstep(0.55, 0.8, wet + (nX - 0.5)*0.2);
  alb *= mix(1.0, 0.62, clamp(wet*1.2, 0.0, 1.0));
  // W3 the wet world (perf switch wet; weather corpus 04, corpus 08 s7): rain from the weather state, not the map. A film
  // darkens porous ground (albedo x lerp(1, 0.35, wet x porosity); rock and scree shed water), and puddles fill the lows
  // (the layers' own height, the map's wet field, ruts) as wxPuddle rises, on flat ground only
  float wxw = uWetOn == 1 ? clamp(wxWet, 0.0, 1.0) : 0.0, wxp = uWetOn == 1 ? clamp(wxPuddle, 0.0, 1.0) : 0.0;
  if (wxw > 0.0 || wxp > 0.0) { float shel = smoothstep(0.15, 0.55, skyVis(p, normalize(vN))); wxw *= mix(0.2, 1.0, shel); wxp *= shel; }   // W3b: under a bridge, eave or canopy the ground stays damp at most, no puddles (it mirrored the underside black)
  if (wxw > 0.0) alb *= mix(1.0, 0.35, wxw*(1.0 - 0.65*max(rockK, screeK*0.5))*(1.0 - 0.6*clamp(wet*1.2, 0.0, 1.0)));
  float hR = uPudRelief == 1 ? mix(hB, po.h, po.w) : hB, pv = uPudRelief == 1 ? 0.14*po.paved*po.w : 0.0;   // FIX7 pudrelief: the paint's own height (cobble joints, ruts) and paving sheds water
  if (wxp > 0.0) puddle = max(puddle, smoothstep(0.86, 0.97, normalize(vN).y)*smoothstep(0.86 + pv - 0.42*wxp, 0.96 + pv - 0.42*wxp, (1.0 - hR)*0.55 + wet*0.3 + rut*0.12 + (nX - 0.5)*0.25));
  float rough = mix(mix(0.96, 0.86, tw), 0.10, puddle);
  rough = mix(rough, 0.78, vEw.z*ewK);   // GX1: wet mud low in the cut: a dull damp sheen, not a mirror
  vec3 N = normalize(vN + vec3(nX - 0.5, 0.0, nZ - 0.5)*mix(0.22, 0.42, max(tw, dirt))*(1.0 - puddle)
                     + vec3(streak - 0.5, 0.0, swN - 0.5)*0.35*nearK*(1.0 - tw)*(1.0 - puddle));   // sward relief (shading only)
  float ao = (1.0 - 0.25*dirt) * (1.0 - 0.18*rut*tw);
  if (uTSVOn == 1) ao *= texture(uTSV, (vW.xz - uTerrainOrg)/uTerrainExt).r;   // VOT T5: the ground's own horizon (indirect light only)
  if (screeK > 0.001) {   // W5b: rock relief and joint shadow (shading only)
    N = normalize(mix(N, normalize(normalize(vN) + rBump), max(rockK, screeK*0.4)));
    rough = mix(rough, 0.9, rockK);
    ao *= 1.0 - 0.35*rCrack*rockK;
  }
  N = normalize(N + tdn*(1.0 - puddle)); rough = clamp(rough*mix(trr, 1.0, puddle), 0.04, 1.0); ao *= tao;   // L4 terrain layers
  rough = max(rough, uGroundMinRough*(1.0 - puddle)*(1.0 - vEw.z*ewK)*(1.0 - wxw));   // W3: the matte floor is for dry ground only   // GG1: dry ground matte (puddles and wet cut mud keep theirs)
  paintFinish(po, rough, N, ao);   // lasso ground paint: roughness, AO, relief over every other layer (after L4)
  if (wxw > 0.0) rough = mix(rough, max(0.35*rough, 0.25), wxw);   // W3 film: smoother, but kept >= 0.25 outside puddles (no mirror ground without real reflections)
  if (puddle > 0.0) { rough = mix(rough, 0.08, puddle); N = normalize(mix(N, normalize(vN), puddle*0.85)); }
  if (puddle > 0.0 && wxw > 0.0 && wxPrecipMmH > 0.05) N = normalize(N + rainRipple(p.xz, uWetTime, min(wxPrecipMmH/8.0, 1.0))*puddle);   // W3: rings while it rains   // CV1: standing water stays a mirror over painted ground
  if (uTerrCave > 0.5) {   // CV1 cave floor: dark wet rock under a water film, black away from the lights, mirror-bright where it catches them
    float flat_k = smoothstep(0.72, 0.9, normalize(vN).y), film = smoothstep(0.35, 0.65, vnoise(vec3(p.xz*0.3, 97.0)) + 0.25*(vnoise(vec3(p.xz*2.1, 98.0)) - 0.5));
    alb *= mix(0.6, mix(0.34, 0.2, film), flat_k);
    rough = mix(rough, min(rough, mix(0.42, 0.1, film)), flat_k);
    N = normalize(mix(N, normalize(vN), flat_k*film*0.6));
  }
  applySnow(p, normalize(vN), 1.0, alb, rough, N); snowTex(p, normalize(vN), tdx, tdy, tDist, hB, alb, rough, N);   // SN1; SN2 the snow sets near (snow.cpp)
  if (uDust.z > 0.0) {   // W5 (weather corpus 06): wind-blown sand streaming over the ground near the eye, along the wind (arid maps)
    float dk = uDust.z*(1.0 - smoothstep(40.0, 60.0, length(p.xz - uCamPos.xz)))*smoothstep(0.75, 0.92, normalize(vN).y);
    if (dk > 0.0) {
      vec2 q = vec2(dot(p.xz, uDust.xy), dot(p.xz, vec2(-uDust.y, uDust.x)));   // along / across the wind (m)
      float s1 = vnoise(vec3((q.x - uDust.w)*0.35, q.y*2.4, 31.0)), s2 = vnoise(vec3((q.x - 1.4*uDust.w)*0.9, q.y*6.0, 32.0));
      float s3 = vnoise(vec3((q.x - 0.5*uDust.w)*0.07, q.y*0.35, 33.0));   // the gust-scale patches the ribbons run in
      float st = smoothstep(0.46, 0.74, 0.6*s1 + 0.4*s2)*smoothstep(0.22, 0.55, s3);
      alb = mix(alb, vec3(0.86, 0.78, 0.62), st*0.75*dk); rough = mix(rough, 1.0, st*dk);   // read at a glance (the first cut was too faint)
    }
  }
  gSpecK = mix(uGroundSpec, 1.0, max(max(puddle, vEw.z*ewK), wxw));   // W3: wet ground keeps its sheen   // GG1: dry ground: no broad sun glare or sky sheen at grazing angles (standing water / wet cut mud keep theirs)
  float gs0 = gSpecK; gSpecK = 0.0;
  vec3 cMatte = uDbgTerr == 2 ? shade(vW, N, alb, 1.0, 0.0, ao) : vec3(0.0);   // GG1 measurement: no specular at all
  gSpecK = gs0;
  vec3 c = shade(vW, N, alb, rough, 0.0, ao);
  if (uDbgTerr == 1) c = rough < 0.4 ? vec3(3.0, 0.0, 0.0) : rough < 0.7 ? vec3(3.0, 3.0, 0.0) : vec3(0.35);
  if (uDbgTerr == 3) c = vec3(terrainHorizonSun(vW), uTHorOn == 1 ? 0.5 : 0.0, uTHorW.w/30.0);   // VOT T6 measurement: red = sun over the terrain horizon, green = map on, blue = sun elevation
  if (uDbgTerr == 4) c = alb;   // VFX rev3 4.1 meter (tools/vot_flatness.py): albedo only, cue 3
  if (uDbgTerr == 5) c = vec3(1.0, ao, 0.0);   // ... the ground's occlusion as green over red (the meter keeps only these ground pixels: G / R), cue 4
  if (uDbgTerr == 2) { float l = dot(c, vec3(0.3, 0.5, 0.2)), s = l > 1e-4 ? max(0.0, l - dot(cMatte, vec3(0.3, 0.5, 0.2)))/l : 0.0; c = s > 0.25 ? vec3(3.0, 0.0, 0.0) : s > 0.1 ? vec3(3.0, 3.0, 0.0) : vec3(0.35); }
  if (uGrid == 1 && abs(p.x) <= uBuildHalf.x && abs(p.z) <= uBuildHalf.y) {
    vec2 g = abs(fract(p.xz/5.0 + 0.5) - 0.5)*5.0;
    vec2 fw = fwidth(p.xz)*1.2;
    float line = 1.0 - min(smoothstep(0.0, fw.x, g.x), smoothstep(0.0, fw.y, g.y));
    float d = length(p - uCamPos);
    c = mix(c, vec3(0.85,0.85,0.7), line*0.35*(1.0 - smoothstep(60.0, 220.0, d)));
  }
  if (uDbgTerr == 0) c = applyFog(c, vW);
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
uniform float uPxAngle;              // FL1: radians per pixel (main pass; 0 in the shadow pass)
invariant gl_Position;               // CP1: the grass depth prepass (grass_pre) and the GL_EQUAL shading pass must agree
uniform int uVehN; uniform vec4 uVeh[32];   // TK3: vehicle footprints (oriented boxes): no grass inside, flattened just outside
float vehFlat(vec2 xz){
  float k = 1.0;
  for (int i = 0; i < uVehN; ++i) {
    vec4 a = uVeh[2*i], b = uVeh[2*i + 1]; vec2 r = xz - a.xy;
    vec2 l = vec2(a.z*r.x - a.w*r.y, a.w*r.x + a.z*r.y);   // into the vehicle's frame
    float d = max(abs(l.x) - b.x, abs(l.y) - b.y);          // outside distance (negative inside)
    k = min(k, d < 0.0 ? 0.0 : mix(0.35, 1.0, smoothstep(0.0, 0.7, d)));
  }
  return k;
}
uniform float uCount, uShadowPass;   // pasture: clumps in this chunk (0 = nature patch plants); sun-shadow pass (radius)
uniform vec4 uLod, uLod2;            // pasture LOD: drawn share, near r, mid r, falloff | floor, -, -, wind r
uniform vec4 uCards;                 // G1: this species group's near / mid / far cards | shadow-pass cards (0 = main pass)
// GT1 grass plant atlas (grass.cpp grassAtlasInit): per cell (u0, u1, v1, width / height), v0; per pasture species the
// first / count of its cells in uGAIdx and how many of its blade cards show a plant (the rest collapse: each plant
// card carries several blades, so the clump keeps about its blade count at a fraction of the fill)
uniform int uGAOn; uniform vec4 uGACell[32]; uniform float uGAV0; uniform ivec4 uGASp[24]; uniform int uGAIdx[48];
layout(location=0) out vec3 vW; layout(location=1) out vec3 vAlb; layout(location=2) out vec3 vF; layout(location=3) out vec2 vUV; layout(location=4) out float vWid; layout(location=5) out float vTap;   // vTap: G1 blade card width / full card width (trimmed padding)
layout(location=6) out vec4 vCC; layout(location=7) out vec4 vPC;
layout(location=8) flat out int vSp; layout(location=9) flat out int vPart; layout(location=10) flat out float vSeed;   // part: 0 blade / stem, 1 flower head (disc), 2 basal leaf; pasture 10+
layout(location=11) flat out int vGA;                    // GT1: atlas layer of this card, -1 = procedural
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
  if (uVehN > 0) fade *= vehFlat(base.xz);   // TK3
  if (uShadowPass > 0.0) fade *= 1.0 - smoothstep(uShadowPass*0.7, uShadowPass, d);   // G1: casters shrink away toward the shadow radius (no ring)
  // GT1: plant cards of the atlas on this species' leading blade cards; its other blade cards collapse
  int ga = -1;
  // plant cards near the eye, where the photo detail reads. Inside 5 m every clump shows 2 plants and drops its other
  // blade cards; across 5-10.5 m a falling share of clumps (stable per-clump hash) shows 1 plant and keeps its outer
  // blade cards, so density and silhouette change gradually; beyond, blades only (plant rectangles cost fill)
  float gaBand = 1.0 - clamp((d - 5.0)/5.5, 0.0, 1.0);
  if (uGAOn == 1 && ph < gaBand) {
    ivec4 gs = uGASp[sp];   // first cell, cell count, first plant card, plant cards
    if (gs.y > 0 && !(sp == 2 && bl < 2.5) && sp != 3 && sp != 16 && sp != 17) {
      bool inner = d < 5.0;
      int nPl = inner ? gs.w : 1;
      if (bl > float(gs.z + nPl) - 0.5) { }   // every other blade card stays: dropping them left a bare circle round the player
      else if (bl > float(gs.z) - 0.5) ga = uGAIdx[gs.x + min(gs.y - 1, int(r3*float(gs.y)))];

    }
  }
  if (fade <= 0.0) {   // clump / card past its LOD cut: the whole card leaves the clip volume (no shading, no raster)
    gl_Position = vec4(0.0, 0.0, -2.0, 1.0); vW = vAlb = vF = vec3(0.0); vUV = vec2(0.0); vWid = 0.0; vTap = 1.0; vCC = vPC = vec4(0.0, 0.0, 0.0, 1.0);
    vSp = 0; vPart = 10; vSeed = 0.0; vGA = -1; return;
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
  float bury = uSnowOn == 1 ? clamp(1.0 - snowDepthAt(base.xz)/max(H*hf, 0.02), 0.0, 1.0) : 1.0;   // SN2: snow buries short grass
  h *= bury;
  float tap = part == 10 ? max(1.0 - 0.95*v*v*v, 0.1) : 1.0;   // G1: blade cards follow the pointed silhouette (no transparent padding)
  w *= widen*taper*tap;
  if (ga >= 0) {   // GT1: the card is the plant's rectangle: its aspect, a gentle lean instead of a blade's curve
    h = H*mix(0.8, 0.65, k)*(0.8 + 0.4*r2)*fade*bury;   // the plants' apparent height matches the blade clumps around them
    h *= mix(0.5, 1.0, smoothstep(0.3, 1.1, d));      // prone: no giant smooth leaf right in front of the eye
    w = h*uGACell[ga].w; tap = 1.0; curve *= 0.3; spread *= 1.6;
  }
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
    float t = windClock(f == 1);
    float gust = sin(t*1.5 + ph*6.28 + dot(base.xz, vec2(0.13, 0.07)))*0.55 + sin(t*0.63 + dot(base.xz, vec2(0.045, 0.11)))*0.45;
    vec3 wv = windDir()*(0.08 + gust*0.22*windK)*windSway(uWind)*windAmp*windGust(base.xz, f == 1);
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
  vW = p; vUV = vec2(aB.x*0.5 + 0.5, v); vSp = sp; vPart = part; vWid = w; vTap = tap; vSeed = r1; vF = fwd; vGA = ga;
  if (ga >= 0) vUV = vec2(mix(uGACell[ga].x, uGACell[ga].y, aB.x*0.5 + 0.5), mix(uGAV0, uGACell[ga].z, v));
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
  if (uVehN > 0) fade *= vehFlat(iP.xz);   // TK3
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
  if (uSnowOn == 1) h *= clamp(1.0 - snowDepthAt(root.xz)/max(iS.x, 0.02), 0.0, 1.0);   // SN2: snow buries short plants
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
  // FL1: no blade thinner than ~0.75 px on screen: sub-pixel blades hit different pixels under each TAA jitter offset
  // and sparkled on and off with the camera standing still (distant pasture, dune grass, stubble)
  if (uPxAngle > 0.0) w = max(w, length(root - uCamPos)*uPxAngle*0.75);
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
    float t = windClock(k == 1);
    float gust = sin(t*freq + iS.w + dot(iP.xz, vec2(0.13, 0.07)))*0.55 + sin(t*0.63 + dot(iP.xz, vec2(0.045, 0.11)))*0.45;
    if (sp == 13) gust = sin(t*freq - dot(iP.xz, uWindV.xy)*0.34)*0.6 + sin(t*0.57 - dot(iP.xz, uWindV.xy)*0.11 + iS.w*0.15)*0.4;   // waves rolling across the field
    vec3 bend = fwd*curve*((sp == 3 || (sp == 12 && isLeaf)) ? 0.5 : 1.0) + windDir()*(0.08 + gust*0.20)*windSway(uWind)*windAmp*windGust(iP.xz, k == 1);
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
  vPart = head ? 1 : isLeaf ? 2 : 0; vWid = w; vTap = 1.0; vSeed = r1; vGA = -1;
  gl_Position = uVP*vec4(p, 1.0);
}
)GLSL";
static const char* kGrassFS = R"GLSL(
uniform float uGrassDead;   // BT1: look.grassDead (0 summer .. 1 dead late-winter grass)
uniform float uDustK;   // WS4: dust coating 0..1 from the weather's dust visibility (st.dustVisM)
layout(location=0) in vec3 vW; layout(location=1) in vec3 vAlb; layout(location=2) in vec3 vF; layout(location=3) in vec2 vUV; layout(location=4) in float vWid; layout(location=5) in float vTap;
layout(location=6) in vec4 vCC; layout(location=7) in vec4 vPC;
layout(location=8) flat in int vSp; layout(location=9) flat in int vPart; layout(location=10) flat in float vSeed;
layout(location=11) flat in int vGA;
uniform sampler2DArray uGATex; uniform vec3 uGAMean[32];   // GT1: unit 27, coverage-preserving mips; linear mean colour per cell
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
    if (vGA >= 0) {             // GT1 atlas plant: its own colour detail, scaled to the clump's palette (root blend kept)
      vec4 tx = texture(uGATex, vec3(vUV, float(vGA)));
      if (tx.a < cutRef()) discard;
      // one palette across the atlas / blade boundary: the clump's own colour (as the blades), modulated by the plant's
      // luminance detail, with a little of the photo's hue kept
      vec3 m = max(uGAMean[vGA], vec3(0.01));
      float lt = dot(tx.rgb, vec3(0.2126, 0.7152, 0.0722)), lm = dot(m, vec3(0.2126, 0.7152, 0.0722));
      alb = mix(vAlb*clamp(lt/lm, 0.4, 1.8), tx.rgb*vAlb/m, 0.3);
    } else if (vPart == 10) {   // grass blade: pointed tip, light midrib, darker margins, fine veins, some browned tips
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
#ifdef NMK_PRE
  return;   // CP1 grass_pre: every cut-out above, no shading (the compiler drops it)
#endif
  alb = mix(alb, vec3(0.45, 0.37, 0.26), 0.5*uDustK);   // WS4: blown dust settles on the blades (0 without a dust preset)
  if (vPart == 1) alb *= 0.85;
  // cheap lighting: one hardware-PCF shadow tap, one sky-occlusion tap, wrapped diffuse, no specular
  float sh = 1.0;
  vec3 shP = past ? vW + uSunDir*0.05 + vec3(0.0, 0.02, 0.0) : vW + vec3(0.0, 0.04, 0.0);   // pasture cards cast: step off their own shadow
  if (uShadowSoft > 0.0) sh = sunShadowT(shP, vec3(0.0, 1.0, 0.0), 0.6, 4) * cloudShadow(vW);
  else if (uShadowOn == 1) {
    vec4 lp = uLightVP*vec4(vW + vec3(0.0, 0.06, 0.0), 1.0); vec3 sc = lp.xyz/lp.w*0.5 + 0.5;
    if (sc.x > 0.0 && sc.x < 1.0 && sc.y > 0.0 && sc.y < 1.0 && sc.z < 1.0) sh = texture(uShadow, vec3(sc.x*uShadowAtlas, sc.y, sc.z - 0.0015));
  }
  float sv = 1.0;
  if (uOccOn == 1) {
    vec4 lp = uOccVP*vec4(vW, 1.0); vec2 uv = lp.xy*0.5 + 0.5;
    if (uv.x > 0.0 && uv.x < 1.0 && uv.y > 0.0 && uv.y < 1.0) sv = 1.0 - smoothstep(0.15, 0.6, (uOccTop - texture(uOcc, uv).r*uOccRange) - vW.y);
  }
  alb *= 0.9 + 0.2*fract(vSeed*91.7 + float(vPart)*0.37);   // PERF1 (user): each blade / card a little lighter or darker (+-10 %)
  alb = greenGrade(alb, uGreenK.y, uGreenK.z);   // FL1: spring green
  if (uGrassDead > 0.0) {   // BT1 look.grassDead: late winter: blades and flower heads dead (straw, grey-brown seed heads), same luma range
    float lum = dot(alb, vec3(0.2126, 0.7152, 0.0722));
    vec3 straw = mix(vec3(0.34, 0.29, 0.21), vec3(0.46, 0.41, 0.31), fract(vSeed*17.3))*clamp(lum*3.2, 0.45, 1.15);
    alb = mix(alb, vPart == 1 ? vec3(0.27, 0.23, 0.18)*clamp(lum*3.0, 0.5, 1.1) : straw, uGrassDead);
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
layout(location=3) in vec2 vUV; layout(location=4) in float vWid; layout(location=5) in float vTap;
layout(location=8) flat in int vSp; layout(location=9) flat in int vPart; layout(location=10) flat in float vSeed;
layout(location=11) flat in int vGA;
uniform sampler2DArray uGATex;
void main(){
  float e = abs(vUV.x*2.0 - 1.0)*vTap, v = vUV.y;
  if (vGA >= 0) { if (texture(uGATex, vec3(vUV, float(vGA))).a < cutRef()) discard; }   // GT1: the plant's own cut-out
  else if (vPart == 11 || vPart == 14) { float y = v*2.0 - 1.0; if (e > sqrt(max(0.0, 1.0 - y*y))) discard; }
  else if (vPart == 13) { if (e > 0.12 && fract(v*6.0 + vSeed*3.0 + e*0.4) > 0.75) discard; }
  else if (e > 1.0 - pow(v, 3.0)*0.95) discard;
}
)GLSL";

const char* rendererFullVS();
static const char* kFullVS = R"GLSL(
#version 450 core
out vec2 vUV;
void main(){ vec2 p = vec2((gl_VertexID<<1)&2, gl_VertexID&2); vUV = p; gl_Position = vec4(p*2.0-1.0, 1.0, 1.0); }
)GLSL";
const char* rendererFullVS() { return kFullVS; }   // VO L7: lighting.cpp's reflection prefilter
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
uniform mat4 uInvVP; uniform int uCloudOn, uCloudQ; uniform float uCloudCover, uTime;   // WX1: uCloudQ = cloud quality 0..2
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
  if (uCloudK > 0.0) {
    // WX2 Cloudy: a continuous grey upper layer (3200 m, broad and slow) hides the blue sky and the sun disc; grey-white
    // toward the horizon. The broken low deck below moves faster over it. Two noise taps: cheap on every preset
    vec2 up = uW2 == 1 ? shellHit(d, 3200.0) : uCamPos.xz + d.xz/(max(d.y, 0.0) + 0.05)*(3200.0 - uCamPos.y);
    vec2 q2 = up/2400.0 + uCloudSeed*0.7 + uCloudT*uCloudWind*0.35;
    float n2 = vnoise(vec3(q2, uCloudT*0.001))*0.6 + vnoise(vec3(q2*2.3, 4.0))*0.4;
    if (uW2 == 1) n2 = mix(0.5, n2, 1.0 - smoothstep(0.3, 0.8, length(fwidth(q2))*2.3));   // W2: the upper layer goes soft where it would streak
    n2 = mix(0.5, n2, smoothstep(-0.01, 0.03, d.y));   // SKY1: no noise below the horizon (the projection smeared it into vertical streaks)
    float L0 = dot(uSkyHor, vec3(0.2126, 0.7152, 0.0722));
    vec3 ov = vec3(L0)*vec3(0.95, 0.97, 1.0)*(0.56 + 0.2*n2)*(uW2 == 1 ? overcastGrad(d.y) : mix(1.75, 0.88, smoothstep(0.0, 0.4, d.y)));   // SKY1: a grey deck overhead, a pale band low at the horizon; W2: CIE overcast, zenith 3x the horizon
    c = mix(c, ov, uCloudK*(1.0 - 0.85*cloudOpening(d)));   // VO L5: the overcast parts at the opening
  }
  if (uCloudOn == 1 && d.y > 0.0) {
    // ray to the cloud plane (1500 m), fBm (5 octaves) scrolling with the wind; coverage shifts the threshold
    vec2 cpw = uW2 == 1 ? shellHit(d, 1500.0) : uCamPos.xz + d.xz/(d.y + 0.06)*(1500.0 - uCamPos.y);   // deck hit point in world xz (~600 m cells); W2: on a shell
    if (uCloudQ == 2) {   // WX1 High: a thin high layer (4500 m, streaked along the wind, slower); not in the shadow map
      vec2 hp = uW2 == 1 ? shellHit(d, 4500.0) : uCamPos.xz + d.xz/(d.y + 0.03)*(4500.0 - uCamPos.y);
      vec2 wd = normalize(uCloudWind);
      vec2 sp = vec2(dot(hp, wd)/2600.0, dot(hp, vec2(-wd.y, wd.x))/700.0) + uCloudSeed*1.7 + uCloudT*0.004;
      float ci = smoothstep(0.55, 0.85, vnoise(vec3(sp, 3.0))*0.65 + vnoise(vec3(sp*2.7, 7.0))*0.35);
      if (uW2 == 1) ci *= (1.0 - smoothstep(0.4, 0.8, uCloudK))*(1.0 - smoothstep(0.3, 0.8, length(fwidth(sp))));   // W2: cirrus sits above the deck (hidden under overcast) and goes soft where it would streak
      c = mix(c, mix(skyColor(d), vec3(1.0)*1.3, 0.55)*uCloudLit, ci*mix(0.35, 0.55, uCloudK)*smoothstep(0.02, 0.2, d.y));
    }
    float n = uCloudQ == 0 ? cloudFieldLo(cpw) : uW2 == 1 ? cloudFieldAA(cpw, max(length(mat2(uCloudXf.xy, uCloudXf.zw)*dFdx(cpw)), length(mat2(uCloudXf.xy, uCloudXf.zw)*dFdy(cpw)))*1.4/600.0) : cloudField(cpw);
    // SKY1 Cloudy: a near-closed deck (the threshold drops) with soft, wide edges, like an overcast stratocumulus sky
    float th = mix(0.72, 0.38, uCloudCover) - 0.12*uCloudK + 0.5*cloudOpening(d);   // VO L5: fewer clouds toward the opening
    float cl = smoothstep(th, th + mix(0.22, 0.45, uCloudK), n);
    vec3 bg = c;   // the sky behind the deck (under Cloudy: the grey upper layer, brighter at the horizon)
    float sd = max(dot(d, uSunDir), 0.0);
    float thick = smoothstep(th + 0.05, th + 0.45, n);
    vec3 lit = vec3(0.95, 0.95, 0.96)*1.35*mix(1.0, 0.5, thick) + uSunCol*0.35*pow(sd, 6.0)*(1.0 - thick);   // grey bases, silver lining at thin edges
    if (uLook == 1 && uCloudQ > 0) {
      // self-shadowing: optical depth toward the sun from three samples of the same field (Beer), plus a
      // forward-scattering lobe that brightens thin edges near the sun; bases are blue-grey, never black
      vec2 toSun = normalize(uSunDir.xz + 1e-4)*160.0;
      float od = 0.0;
      for (int i = 1; i <= 3; ++i) od += max(cloudFieldLo(cpw + toSun*float(i)) - th, 0.0);
      float lightT = exp(-od*3.2);
      vec3 base = mix(vec3(0.52, 0.56, 0.62), vec3(0.98, 0.98, 0.97), lightT)*1.45;
      base = mix(base, mix(vec3(0.3, 0.325, 0.35), vec3(0.68, 0.7, 0.71), lightT)*1.15, uCloudK);   // SKY1: blue-grey bases, paler mottled tops
      float hg = 0.25 + 0.75*pow(sd, 5.0);
      lit = base*mix(1.0, 0.72, thick) + uSunCol*vec3(1.0, 0.95, 0.85)*0.45*hg*(1.0 - thick)*lightT;
      float far = 1.0 - smoothstep(0.02, mix(0.25, 0.4, uCloudK), d.y);           // distant cloud fades into the horizon haze
      lit = mix(lit, mix(skyColor(d), bg, uCloudK), far*mix(0.65, 0.9, uCloudK));   // SKY1: under Cloudy a pale band low at the horizon
    }
    lit *= mix(1.0, 0.72, uCloudK*thick);   // WX1 Cloudy: heavier, darker undersides
    // WX1: thick cloud is opaque (the HDR sun disc behind it no longer leaks through as a sprite); thin edges stay translucent
    c = mix(c, lit*uCloudLit, cl*mix(0.9, 1.0, thick)*smoothstep(0.0, 0.12, d.y));
  }
  { float vis = 3.912/max(wxSigma, 1e-6); if (wxPhys && vis < 800.0) c = mix(c, hazeColor(d), 1.0 - smoothstep(250.0, 800.0, vis)); }   // W2: in a storm under ~500 m visibility the sky is the air's own colour (no blue, no cloud shapes)
  if (uUnder > 0.5) c = uFogCol*(0.75 + 0.45*max(d.y, 0.0));   // under water: the fog, a little lighter toward the surface
  oColor = vec4((any(isnan(c)) || any(isinf(c))) ? skyColor(d) : c, 1.0);
}
)GLSL";
#define NMK_GRADE_GLSL R"GLSL(
uniform float uUnderTone;   // 1: under water (swim.cpp): blue-green tint, stronger vignette
uniform float uNightGamma;   // 1 by day; below 1 lifts the darks at night
uniform float uMapGamma;     // BR1c: the open map's gamma (> 1 brighter), 1 = none
uniform float uWxK;   // WX2: the Cloudy mix (desaturation, cooler grade)
uniform float uWxDark = 1.0;   // W2: 0 under a preset row (its exposure carries the light level; the grade only greys)
uniform float uMapSat; uniform vec3 uMapTint;   // NT2b: a map's own saturation / tint
uniform int uPhysGrade;   // CP2 physgrade: the grade ran in scene-linear before the curve (pivot 0.18); here only the weather / map / gamma steps
uniform int uGradeOn; uniform float uGradeSat, uGradeContrast, uGradeVig, uGradeSharpen, uGradeCA; uniform vec3 uGradeShadow, uGradeHigh;
// VO L8: every colour-only step after the tone curve, in Classic's order (vignettes are applied apart: they are positional)
vec3 gradeColor(vec3 c){
  float l = dot(c, vec3(0.2126,0.7152,0.0722));
  if (uGradeOn == 1 && uPhysGrade == 0) {
    c = mix(vec3(l), c, uGradeSat);
    float sh = 1.0 - smoothstep(0.0, 0.42, l), hi = smoothstep(0.3, 0.95, l);
    c *= mix(vec3(1.0), uGradeShadow, sh)*mix(vec3(1.0), uGradeHigh, hi);
    c = clamp(c, 0.0, 1.0); c = pow(c, vec3(uNightGamma)); c = mix(c, c*c*(3.0 - 2.0*c), uGradeContrast);
  } else c = mix(vec3(l), c, 0.93);
  if (uWxK > 0.0) { float lw = dot(c, vec3(0.2126, 0.7152, 0.0722)); c = mix(c, vec3(lw), 0.22*uWxK)*mix(vec3(1.0), vec3(0.94, 0.98, 1.05), uWxK)*(1.0 - 0.38*uWxK*uWxDark); c = mix(c, c*c*(3.0 - 2.0*c), 0.25*uWxK); }
  if (uMapSat != 1.0 || uMapTint != vec3(1.0)) { float lm = dot(c, vec3(0.2126, 0.7152, 0.0722)); c = mix(vec3(lm), c, uMapSat)*uMapTint; }
  if (uMapGamma != 1.0) c = pow(clamp(c, 0.0, 1.0), vec3(1.0/uMapGamma));
  if (uUnderTone > 0.0) c = mix(c, c*vec3(0.55, 0.92, 0.98) + vec3(0.0, 0.025, 0.035), uUnderTone);
  return c;
}
)GLSL"
// VO L8: the grade baked into a 32^3 LUT each frame (sqrt-encoded input: precision in the darks); slice uSlice
static const char* kGradeLutFS = R"GLSL(
#version 450 core
uniform float uSlice;
out vec4 oColor;
)GLSL" NMK_GRADE_GLSL R"GLSL(
void main(){
  vec3 u = vec3(floor(gl_FragCoord.xy), uSlice)/31.0;
  oColor = vec4(gradeColor(u*u), 1.0);
}
)GLSL";
static const char* kToneFS = R"GLSL(
#version 450 core
in vec2 vUV;
uniform sampler2D uHdr, uExpo, uBloom, uVmDist; uniform float uFixedExpo, uBloomAmt, uEyeR, uAspect, uRectVig, uMicro; uniform int uTonemap;
uniform float uNewExpo;   // VO L1: Lighting New's re-fit of a fixed exposure (1 in Classic)
uniform float uCas;     // CP1: contrast-adaptive sharpen 0..1 (Settings > Sharpness; replaces the TAA and grade sharpens), 0 off
uniform int uDither;    // CP1: triangular +-1 LSB dither at the final write
float tpdf(){ vec2 p = gl_FragCoord.xy; float a = fract(sin(dot(p, vec2(12.9898, 78.233)))*43758.5453), b = fract(sin(dot(p + 17.31, vec2(39.3468, 11.135)))*24634.6345); return (a + b - 1.0)/255.0; }
uniform int uDbgView;   // CP0: 1 false-colour luminance, 6 a centre patch of exposed 0.18 (the look-dev readout)
uniform int uNewTone; uniform float uAgxK, uAgxSat; uniform sampler3D uGradeLut;   // VO L8: AgX-style curve and the grade LUT (Lighting New)
// GX5 grade (content/settings/grade.json, docs/graphics-overhaul.md#gx5-grade): saturation, cool shadows / warm
// highlights, a gentle S-curve, vignette, chromatic aberration (0 = off), and an edge-aware sharpen of the image
)GLSL" NMK_GRADE_GLSL R"GLSL(
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
// VO L8 AgX-style display transform (Sobotka's AgX): the curve runs in AgX's inset primaries space and the result goes
// back through the outset matrix, so bright saturated colour (desert sand, snow, sky, fire) travels to white instead of
// skewing hue per channel. The curve itself stays the game's filmic one: in the linear part inset and outset cancel,
// so mid-tones and shadows, and with them each map's mood, are unchanged; only the shoulder differs
vec3 agxFilmic(vec3 v){
  const mat3 inset = mat3(0.842479062253094, 0.0423282422610123, 0.0423756549057051, 0.0784335999999992, 0.878468636469772, 0.0784336, 0.0792237451477643, 0.0791661274605434, 0.879142973793104);
  const mat3 outset = mat3(1.19687900512017, -0.0528968517574562, -0.0529716355144438, -0.0980208811401368, 1.15190312990417, -0.0980434501171241, -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
  return clamp(outset*filmic(inset*max(v, vec3(0.0))), 0.0, 1.0);
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
  if (uCas > 0.0) {   // CP1 contrast-adaptive sharpen (AMD CAS on luminance in a compressed space): less where the cross already has contrast; no halos
    vec2 tx = 1.0/vec2(textureSize(uHdr, 0)); const vec3 lw = vec3(0.2126, 0.7152, 0.0722);
    float L = dot(c, lw); float yc = L/(1.0 + L);
    float a0 = dot(texture(uHdr, vUV + vec2(tx.x, 0.0)).rgb, lw), a1 = dot(texture(uHdr, vUV - vec2(tx.x, 0.0)).rgb, lw), a2 = dot(texture(uHdr, vUV + vec2(0.0, tx.y)).rgb, lw), a3 = dot(texture(uHdr, vUV - vec2(0.0, tx.y)).rgb, lw);
    vec4 yn = vec4(a0, a1, a2, a3)/(1.0 + vec4(a0, a1, a2, a3));
    float mn = min(yc, min(min(yn.x, yn.y), min(yn.z, yn.w))), mx = max(yc, max(max(yn.x, yn.y), max(yn.z, yn.w)));
    float amp = sqrt(clamp(min(mn, 1.0 - mx)/max(mx, 1e-4), 0.0, 1.0)), wgt = -amp/mix(8.0, 5.0, uCas);
    float ys = clamp((yc + wgt*(yn.x + yn.y + yn.z + yn.w))/(1.0 + 4.0*wgt), 0.0, 0.999);
    c *= (ys/(1.0 - ys))/max(L, 1e-5);
  }
  if (uMicro > 0.0) {   // UI2 microcontrast: an unsharp mask on luminance against the HDR mip 3 (1/8 size, ~8 px; made for the exposure anyway), before the tone curve
    float L = dot(c, vec3(0.2126, 0.7152, 0.0722)), Lb = dot(textureLod(uHdr, vUV, 3.0).rgb, vec3(0.2126, 0.7152, 0.0722));
    float r = clamp((L - Lb)/(Lb + 1e-4), -0.5, 0.5);   // relative difference from the local mean, clamped: no halos round strong edges or the sky line
    float e0 = uFixedExpo > 0.0 ? uFixedExpo : texelFetch(uExpo, ivec2(0), 0).r;
    c *= max(1.0 + r*uMicro*0.9*smoothstep(0.004, 0.03, L*e0), 0.0);   // linear: the local mean (and so the overall brightness) stays put; nothing in the deep shadows (no lifted noise)
  }

  float bAmt = uBloomAmt;   // optic eyepiece: no bloom over its dark surround (the bloom is taken from the whole lit view)
  if (uEyeR > 0.0) bAmt *= 1.0 - smoothstep(0.98*uEyeR, 1.02*uEyeR, length((vUV - 0.5)*vec2(uAspect, 1.0)));
  if (texture(uVmDist, vUV).r > 0.0) bAmt = 0.0;   // VM2: the bloom is the world's, taken before the first-person weapon was drawn: blended over the dark gun it showed the ground through it
  if (bAmt > 0.0) c = mix(c, texture(uBloom, vUV).rgb/6.0, bAmt);   // chain sums 6 levels; energy-preserving blend, not an add
  float e = uFixedExpo > 0.0 ? uFixedExpo*uNewExpo : texelFetch(uExpo, ivec2(0), 0).r;   // VO L1: auto exposure already re-adapts to New's darker scene; a fixed one is lifted
  vec2 q = vUV - 0.5;
  if (uDbgView >= 3 && uDbgView <= 5) { oColor = vec4(clamp(texture(uHdr, vUV).rgb, 0.0, 1.0), 1.0); return; }   // CP step 2: albedo / furnace / texel views shown as computed (no exposure, curve or bloom)
  if (uDbgView == 1) {   // CP0 false colour: one band per stop of exposed luminance around 0.18 (grey = 0.18 +- half a stop)
    const vec3 band[13] = vec3[13](vec3(0.0), vec3(0.25, 0.0, 0.35), vec3(0.2, 0.1, 0.8), vec3(0.0, 0.45, 0.9), vec3(0.0, 0.65, 0.6), vec3(0.3, 0.55, 0.3),
      vec3(0.46), vec3(0.65, 0.8, 0.45), vec3(0.95, 0.75, 0.75), vec3(0.95, 0.9, 0.2), vec3(1.0, 0.6, 0.0), vec3(1.0, 0.1, 0.0), vec3(1.0));
    float st = log2(max(dot(c*e, vec3(0.2126, 0.7152, 0.0722)), 1e-7)/0.18);
    oColor = vec4(band[int(clamp(floor(st + 0.5) + 6.0, 0.0, 12.0))], 1.0); return;   // -6 and below black ... +6 and above white
  }
  if (uDbgView == 6 && all(lessThan(abs(gl_FragCoord.xy - vec2(textureSize(uHdr, 0))*0.5), vec2(12.0)))) c = vec3(0.18)/max(e, 1e-6);   // scene 0.18 through the whole display chain
  if (uNewTone >= 1) {   // VO L8: AgX curve (matched to the old one at mid grey), saturation re-fit, then the grade from its LUT
    if (uPhysGrade == 1 && uGradeOn == 1) {   // CP2 physgrade: contrast around scene 0.18, saturation, cool shadows / warm highlights by stops, all before the curve
      vec3 x = c*e; const vec3 lw = vec3(0.2126, 0.7152, 0.0722); float L = max(dot(x, lw), 1e-6);
      x *= 0.18*pow(L/0.18, 1.0 + 0.6*uGradeContrast)/L; L = max(dot(x, lw), 1e-6);
      x = max(mix(vec3(L), x, uGradeSat), 0.0);
      float st = log2(L/0.18);
      x *= mix(uGradeShadow, vec3(1.0), smoothstep(-4.0, 0.0, st))*mix(vec3(1.0), uGradeHigh, smoothstep(0.0, 3.0, st));
      c = x/max(e, 1e-8);
    }
    c = agxFilmic(c*e*1.22*uAgxK);
    float la = dot(c, vec3(0.2126, 0.7152, 0.0722)); c = max(mix(vec3(la), c, uAgxSat), 0.0);
    c = uNewTone == 2 ? gradeColor(clamp(c, 0.0, 1.0)) : texture(uGradeLut, sqrt(clamp(c, 0.0, 1.0))*(31.0/32.0) + 0.5/32.0).rgb;   // 2: NMK_VO_NOLUT (A/B of the LUT)
    c *= 1.0 - dot(q,q)*(uGradeOn == 1 ? uGradeVig : 0.16);
    if (uRectVig > 0.5) { vec2 a = abs(vUV - 0.5)*2.0; float r = pow(pow(a.x, 6.0) + pow(a.y, 6.0), 1.0/6.0); float k = smoothstep(0.62, 1.04, r); k = k*k*(3.0 - 2.0*k);
      float n = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233)))*43758.5453) - 0.5; c = max(c*(1.0 - 0.96*k) + n/255.0, 0.0); }
    if (uUnderTone > 0.0) c *= 1.0 - dot(q,q)*1.4*uUnderTone;
    oColor = vec4(toSRGB(clamp(c, 0.0, 1.0)) + (uDither == 1 ? vec3(tpdf()) : vec3(0.0)), 1.0); return;
  }
  c = uTonemap == 0 ? aces(c*e*0.8) : filmic(c*e*1.22);
  // subdued, slightly desaturated period grade (grade.json): saturation, cool shadows / warm highlights, vignette
  float l = dot(c, vec3(0.2126,0.7152,0.0722));
  if (uGradeOn == 1) {   // GX5: the muted, cool-shadow / warm-light grading of the brief's targets
    c = mix(vec3(l), c, uGradeSat);
    float sh = 1.0 - smoothstep(0.0, 0.42, l), hi = smoothstep(0.3, 0.95, l);
    c *= mix(vec3(1.0), uGradeShadow, sh)*mix(vec3(1.0), uGradeHigh, hi);
    c = clamp(c, 0.0, 1.0); c = pow(c, vec3(uNightGamma)); c = mix(c, c*c*(3.0 - 2.0*c), uGradeContrast);
    c *= 1.0 - dot(q,q)*uGradeVig;
  } else {
    c = mix(vec3(l), c, 0.93);
    c *= 1.0 - dot(q,q)*0.16;
  }
  if (uRectVig > 0.5) {   // TK3: a superellipse (rounded into the corners) on all four edges: clear to 62 % of the half
    // extent, a smooth fall to near black only at the very border; dithered so the gradient does not band
    vec2 a = abs(vUV - 0.5)*2.0; float r = pow(pow(a.x, 6.0) + pow(a.y, 6.0), 1.0/6.0);
    float k = smoothstep(0.62, 1.04, r); k = k*k*(3.0 - 2.0*k);
    float n = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233)))*43758.5453) - 0.5;
    c = max(c*(1.0 - 0.96*k) + n/255.0, 0.0);
  }
  if (uWxK > 0.0) { float lw = dot(c, vec3(0.2126, 0.7152, 0.0722)); c = mix(c, vec3(lw), 0.22*uWxK)*mix(vec3(1.0), vec3(0.94, 0.98, 1.05), uWxK)*(1.0 - 0.38*uWxK*uWxDark); c = mix(c, c*c*(3.0 - 2.0*c), 0.25*uWxK); }   /* overcast: 38 % darker, more muted and cooler, a little more contrast (moody) */   // WX2 Cloudy: ~13 % less saturation, a touch cooler
  if (uMapSat != 1.0 || uMapTint != vec3(1.0)) { float lm = dot(c, vec3(0.2126, 0.7152, 0.0722)); c = mix(vec3(lm), c, uMapSat)*uMapTint; }   // NT2b
  if (uMapGamma != 1.0) c = pow(clamp(c, 0.0, 1.0), vec3(1.0/uMapGamma));   // BR1c
  if (uUnderTone > 0.0) { c = mix(c, c*vec3(0.55, 0.92, 0.98) + vec3(0.0, 0.025, 0.035), uUnderTone); c *= 1.0 - dot(q,q)*1.4*uUnderTone; }
  oColor = vec4(toSRGB(c) + (uDither == 1 ? vec3(tpdf()) : vec3(0.0)), 1.0);
}
)GLSL";
static const char* kExpoFS = R"GLSL(
#version 450 core
uniform sampler2D uHdr, uPrev; uniform float uLod, uDt, uReset;
uniform float uExpoKey, uExpoMin, uExpoMax;   // L2 time of day: Day 0.24, 0.9 .. 1.45; night opens the range (lighting.cpp)
uniform float uExpoFloor;   // FX1: while the headlamp is on, the exposure at switch-on (fading): its lit patch does not pump the scene down
uniform int uHist; uniform float uHistCal;   // VO L8: histogram metering (Lighting New), its calibration to the old mean metering
out float oE;
// VO L8 (Lagarde & de Rousiers 2014): a centre-weighted 32-bin histogram of log2 luminance over a 16 x 16 grid of a
// ~64 px mip, metered as the mean between the 40th and 95th percentiles: the sky's few brightest pixels and dark
// corners no longer swing the exposure (blown desert and snow, murky forest)
float histLum(){
  float h[32]; for (int i = 0; i < 32; ++i) h[i] = 0.0;
  float lod = max(uLod - 6.0, 0.0), ws = 0.0;
  for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
    vec2 uv = (vec2(x, y) + 0.5)/16.0;
    float L = dot(textureLod(uHdr, uv, lod).rgb, vec3(0.2126, 0.7152, 0.0722));
    if (isnan(L) || isinf(L)) continue;
    float w = 1.0 - 0.7*smoothstep(0.1, 0.7, length((uv - 0.5)*vec2(1.0, 1.3)));
    h[int(clamp((log2(max(L, 1e-6)) + 12.0)*2.0, 0.0, 31.0))] += w; ws += w;
  }
  float lo = 0.4*ws, hi = 0.95*ws, acc = 0.0, sum = 0.0, cnt = 0.0;
  for (int i = 0; i < 32; ++i) {
    float t = max(min(acc + h[i], hi) - max(acc, lo), 0.0);
    sum += t*((float(i) + 0.5)*0.5 - 12.0); cnt += t; acc += h[i];
  }
  return exp2(sum/max(cnt, 1e-4))*uHistCal;
}
void main(){
  vec3 avg = textureLod(uHdr, vec2(0.5), uLod).rgb;
  float prev = texelFetch(uPrev, ivec2(0), 0).r;
  if (uReset > 0.0) { oE = uReset; return; }
  float lr = uHist == 1 ? histLum() : dot(avg, vec3(0.2126,0.7152,0.0722));
  if (isnan(lr) || isinf(lr)) { oE = prev; return; }   // one non-finite pixel poisons the whole mip average
  float l = max(lr, 1e-4);
  float target = max(clamp(uExpoKey/l, uExpoMin, uExpoMax), uExpoFloor);
  float k = 1.0 - exp(-uDt*(target > prev ? 0.6 : 1.2));
  oE = mix(prev, target, k);
}
)GLSL";
// SF1 snowfall (Blackwater Thaw spec 6): two groups of streaks, procedural per instance, in boxes around the eye that wrap
// on world coordinates (stable world movement: the flakes do not ride with the camera). Group 0 near (sparse, a broad
// range of sizes and apparent speeds), group 1 far (fine, long paths: depth). A flake under anything solid (the
// top-down sky-occlusion map: roofs, eaves, canopies) is not drawn; the last 0.4-0.8 m before the eye fade; indoors none.
static const char* kSnowfallVS = R"GLSL(
// W4 precipitation (corpus weather 04 rain, 05 snow): one stateless path, kind by uKind. 0 snow (the map's snowfall,
// Light snow, Blizzard: near-horizontal with the storm's wind), 1 rain (Drizzle, Steady rain, Thunderstorm: streaks along
// the fall-and-wind velocity, length = speed x exposure, sub-pixel width given to alpha), 2 splashes (short-lived crowns
// on the top surface under each drop: the sky-occlusion map's height, so roofs and ground alike), 3 spindrift (snow
// racing over the ground in a blizzard). Every particle: a function of its id and the clock in a box round the eye that
// wraps on world coordinates; the wind is the CPU-integrated drift (uSFAcc); hidden under roofs (occlusion map) and
// indoors; the scene's fog (wxSigma) over its distance; lit by the lightning flash (wxFlash). kCommon (weather block,
// uCamPos, the occlusion map) is prepended.
uniform mat4 uVP; uniform vec3 uCamRight, uCamUp; uniform float uTime;
uniform vec4 uSF;    // nearN, near box half (m), far box half (m), fall speed (m/s)
uniform vec4 uSF2;   // unused x / y (the wind is uSFAcc), gust 0..1, indoor 0..1
uniform vec2 uSFAcc; // BT2 (corpus weather 05 s2.4): the wind's accumulated drift (m), integrated on the CPU each frame
uniform vec3 uCamFwd; uniform float uPxAng;   // the eye's forward; vertical radians per pixel
uniform int uKind; uniform vec3 uPCol;        // W4: the kind; the lit particle colour (CPU)
uniform float uPSize;                         // W4: flake size factor (1; a blizzard's clumped flakes larger)
out vec2 vC; out float vA; flat out int vK; out vec3 vCol;
float h1(float n){ return fract(sin(n*12.9898 + 78.233)*43758.5453); }
void main(){
  int id = gl_InstanceID; bool near = float(id) < uSF.x;
  float fi = float(id);
  int c = gl_VertexID; vec2 qv = c == 0 ? vec2(-1, -1) : c == 1 ? vec2(1, -1) : c == 2 ? vec2(1, 1) : c == 3 ? vec2(-1, -1) : c == 4 ? vec2(1, 1) : vec2(-1, 1);
  vK = uKind; vCol = uPCol*(1.0 + 4.0*wxFlash);
  if (uKind == 2) {   // a splash: a random spot round the eye each 0.3 s cycle, on the top surface there, a crown rising and fading
    float cyc = uTime/0.3 + h1(fi*3.3), k = floor(cyc), f = fract(cyc);
    vec2 o = (vec2(h1(fi*1.7 + k*7.1), h1(fi*2.9 + k*5.3))*2.0 - 1.0)*uSF.y;
    vec3 p = vec3(uCamPos.x + o.x, 0.0, uCamPos.z + o.y);
    float a = 0.0;
    if (uOccOn == 1) { vec4 lp = uOccVP*vec4(p.x, uCamPos.y, p.z, 1.0); vec2 uv = lp.xy*0.5 + 0.5;
      if (uv.x > 0.0 && uv.x < 1.0 && uv.y > 0.0 && uv.y < 1.0) { p.y = uOccTop - texture(uOcc, uv).r*uOccRange; a = 1.0; } }
    float d = length(p - uCamPos);
    a *= (1.0 - f)*(1.0 - smoothstep(uSF.y*0.7, uSF.y, d))*(1.0 - uSF2.w)*exp(-wxSigma*d);
    float rad = 0.03 + 0.06*f;
    vec3 tv = normalize(p - uCamPos), side = normalize(cross(tv, vec3(0.0, 1.0, 0.0)) + vec3(1e-4));
    vec3 w = p + side*qv.x*rad + vec3(0.0, 1.0, 0.0)*(qv.y*0.5 + 0.5)*rad*1.2;
    vC = qv; vA = a*0.6;
    gl_Position = uVP*vec4(w, 1.0);
    if (vA <= 0.001) gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    return;
  }
  const bool rain = uKind == 1, spin = uKind == 3;
  float R = near ? uSF.y : uSF.z;
  vec3 r = vec3(h1(fi*1.31), h1(fi*2.17 + 3.0), h1(fi*3.73 + 7.0));
  float spd = uSF.w*(near ? mix(0.6, 1.5, h1(fi*5.1)) : mix(0.5, 0.9, h1(fi*5.1)));
  if (rain) spd = uSF.w*mix(0.85, 1.15, h1(fi*5.1));
  vec2 wf = vec2(0.7 + 0.6*h1(fi*7.7), 0.7 + 0.6*h1(fi*8.3)) * (spin ? 2.4 : 1.0);
  // BT2: position = start + the accumulated wind (each flake its own share) + a constant fall + a small flutter (3-15 cm at
  // ~1 Hz). Never velocity x time: with the gust in the velocity that drifted ever faster the longer the map ran
  float fa = rain ? 0.0 : mix(0.03, 0.15, h1(fi*6.1)), fq = 6.2832*mix(0.7, 1.3, h1(fi*6.9)), ph = 6.2832*h1(fi*9.1);
  vec3 box = spin ? vec3(2.0*R, 1.6, 2.0*R) : vec3(2.0*R, 1.2*R, 2.0*R);
  vec3 p0 = (r - 0.5)*box;
  vec3 p = p0 + vec3(uSFAcc.x*wf.x, -spd*uTime*(spin ? 0.15 : 1.0), uSFAcc.y*wf.y) + vec3(sin(uTime*fq + ph), 0.0, cos(uTime*fq*0.8 + ph))*fa;
  vec3 cen = spin ? vec3(uCamPos.x, uCamPos.y - 1.0, uCamPos.z) + uCamFwd*vec3(0.35, 0.0, 0.35)*box : uCamPos + uCamFwd*0.35*box;
  p = mod(p - cen + box*0.5, box) - box*0.5 + cen;                    // wrapped on world coordinates (no riding with the camera)
  vec3 q = abs(p - cen)/(box*0.5);
  float a = near ? 1.0 : 0.55;
  a *= near ? mix(0.8, 1.0, uSF2.z) : smoothstep(h1(fi*9.9)*0.9, h1(fi*9.9)*0.9 + 0.1, 0.45 + 0.55*uSF2.z);
  a *= 1.0 - smoothstep(0.8, 1.0, max(q.x, max(q.y, q.z)));          // fade over the last tenth of the box
  if (uOccOn == 1) {   // sheltered: a soft fade 0.5-1.5 m under anything solid above the particle
    vec4 lp = uOccVP*vec4(p, 1.0); vec2 uv = lp.xy*0.5 + 0.5;
    if (uv.x > 0.0 && uv.x < 1.0 && uv.y > 0.0 && uv.y < 1.0) { float top = uOccTop - texture(uOcc, uv).r*uOccRange; a *= 1.0 - smoothstep(0.5, 1.5, top - p.y); if (spin && p.y < top - 0.05) a = 0.0; }
  }
  float d = length(p - uCamPos);
  a *= smoothstep(0.4, 0.8, d)*(1.0 - uSF2.w)*exp(-wxSigma*d);       // the scene's fog over the particle's own distance
  vec3 tv = normalize(p - uCamPos);
  vec3 w;
  if (rain) {   // a streak along the velocity: length = speed x a 1/45 s exposure, at least 1.5 px wide (the rest to alpha)
    vec3 vel = vec3(uSF2.x*wf.x, -spd, uSF2.y*wf.y); float sp = length(vel); vec3 ax = vel/max(sp, 1e-3);
    float len = clamp(sp/45.0, 0.08, 0.6)*0.5, wid = 0.0006, wmin = 0.75*uPxAng*d;
    if (wid < wmin) { a *= wid/wmin; wid = wmin; }
    vec3 side = normalize(cross(ax, tv) + vec3(1e-4));
    w = p + side*qv.x*wid + ax*qv.y*len;
    a *= 0.45;
  } else {      // a soft disc 4-12 mm across near, ~3x far; under 1.5 px it keeps 1.5 px and gives the lost area to alpha
    float rad = 0.5*uPSize*(near ? mix(0.004, 0.012, h1(fi*4.4)) : mix(0.012, 0.036, h1(fi*4.4)));
    float rmin = 0.75*uPxAng*d;
    if (rad < rmin) { a *= (rad*rad)/(rmin*rmin); rad = rmin; }
    vec3 side = normalize(cross(tv, vec3(0.0, 1.0, 0.0)) + vec3(1e-4)), up = cross(side, tv);
    w = p + (side*qv.x + up*qv.y)*rad;
  }
  vC = qv; vA = a;
  gl_Position = uVP*vec4(w, 1.0);
  if (a <= 0.001) gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
}
)GLSL";
static const char* kSnowfallFS = R"GLSL(
#version 450 core
in vec2 vC; in float vA; flat in int vK; in vec3 vCol; out vec4 oColor;
void main(){
  float k = vK == 1 ? (1.0 - vC.x*vC.x)*(1.0 - smoothstep(0.6, 1.0, abs(vC.y)))   // a rain streak: soft across, tapered ends
          : vK == 2 ? (1.0 - smoothstep(0.2, 1.0, length(vC)))                       // a splash crown
          : 1.0 - smoothstep(0.35, 1.0, length(vC));                                  // a soft round flake
  oColor = vec4(vCol, k*vA*0.85);
}
)GLSL";
// WS2 drifting leaves (corpus 07 s7.3), the snowfall's stateless path: each leaf is a function of its instance id and the
// clock, in a box round the eye that wraps on world coordinates. Fall speed is constant per leaf (so x time is exact); the
// sideways travel is the CPU-accumulated wind displacement (uLD.zw) x a per-leaf share, never speed x absolute time. The
// travelling gust (windGust, kCommon) decides how many show where: denser bands pass downwind. Alpha-tested ovals, two-sided,
// tumbling. Hidden indoors and where something solid sits just above the leaf (eaves, porches, low roofs: the sky-occlusion map).
static const char* kLeafDriftVS = R"GLSL(
uniform mat4 uVP; uniform vec3 uCamRight, uCamUp; uniform float uTime;
uniform vec4 uLD;    // count, box half (m), wind displacement x, z (m, accumulated)
uniform vec4 uLD2;   // indoor 0..1, mode (0 leaves, 1 WS3 pollen), -, -   (uOcc, uOccVP, uOccTop, uOccRange, uOccOn: kCommon)
out vec2 vC; out vec3 vW, vN, vCol; out float vA;
float h1(float n){ return fract(sin(n*12.9898 + 78.233)*43758.5453); }
void main(){
  float fi = float(gl_InstanceID), R = uLD.y;
  vec3 r = vec3(h1(fi*1.31), h1(fi*2.17 + 3.0), h1(fi*3.73 + 7.0));
  const bool pol = uLD2.y > 0.5;   // WS3 pollen: near the ground, hardly falling, carried by the wind, bobbing
  float fall = pol ? mix(0.02, 0.12, h1(fi*5.1)) : mix(0.55, 1.4, h1(fi*5.1)), share = pol ? mix(0.8, 1.0, h1(fi*7.7)) : mix(0.45, 0.9, h1(fi*7.7)), ph = h1(fi*8.3)*6.2832;
  const float hy = pol ? 3.0 : 12.0, cy = pol ? 0.6 : 3.0;
  vec3 p = vec3((r.x*2.0 - 1.0)*R, r.y*hy - 0.5*hy, (r.z*2.0 - 1.0)*R);
  p += vec3(uLD.z*share, -fall*uTime, uLD.w*share);
  p.xz += vec2(sin(uTime*mix(0.8, 1.9, h1(fi*4.4)) + ph), cos(uTime*mix(0.6, 1.5, h1(fi*6.6)) + ph))*(pol ? 0.15 : 0.35);   // zig-zag of a falling leaf / a mote's wander
  if (pol) p.y += 0.12*sin(uTime*mix(0.7, 1.6, h1(fi*18.1)) + ph);
  vec3 box = vec3(2.0*R, hy, 2.0*R), c = uCamPos + vec3(0.0, cy, 0.0);
  p = mod(p - c + box*0.5, box) - box*0.5 + c;                                  // wrap round the eye, on world coordinates
  float g = windGust(p.xz, false), a = step(h1(fi*9.9), clamp(0.6 + 0.8*(g - 1.0), 0.0, 1.0));
  if (uOccOn == 1) { vec4 lp = uOccVP*vec4(p, 1.0); vec2 uv = lp.xy*0.5 + 0.5;
    if (uv.x > 0.0 && uv.x < 1.0 && uv.y > 0.0 && uv.y < 1.0) { float top = uOccTop - texture(uOcc, uv).r*uOccRange; if (top > p.y + 0.1 && top < p.y + 2.5) a = 0.0; } }
  float d = length(p - uCamPos);
  a *= step(1.0, d)*(1.0 - smoothstep(R*0.75, R, d))*(1.0 - uLD2.x);
  // tumble: the leaf plane turns about a per-leaf axis at a per-leaf rate
  vec3 ax = normalize(vec3(h1(fi*11.1) - 0.5, h1(fi*12.7) - 0.5, h1(fi*13.3) - 0.5) + vec3(1e-3));
  float an = uTime*mix(1.5, 4.5, h1(fi*14.9)) + ph, cs = cos(an), sn = sin(an);
  vec3 u0 = normalize(cross(ax, vec3(0.0, 1.0, 0.3))), v0 = cross(ax, u0);
  vec3 U = u0*cs + v0*sn, V = cross(ax, U);
  float sz = pol ? mix(0.0025, 0.005, h1(fi*15.5)) + d*0.0006 : mix(0.025, 0.045, h1(fi*15.5));   // motes keep about a pixel with distance
  if (pol) { U = uCamRight; V = uCamUp; }   // motes face the eye
  int k = gl_VertexID; vec2 q = k == 0 ? vec2(-1, -1) : k == 1 ? vec2(1, -1) : k == 2 ? vec2(1, 1) : k == 3 ? vec2(-1, -1) : k == 4 ? vec2(1, 1) : vec2(-1, 1);
  vec3 w = p + (U*q.x*0.62 + V*q.y)*sz;
  float t = h1(fi*16.1);   // dead-leaf palette: brown, ochre, olive (linear)
  vCol = t < 0.45 ? vec3(0.16, 0.085, 0.035) : t < 0.8 ? vec3(0.30, 0.19, 0.055) : vec3(0.14, 0.15, 0.05);
  vCol *= mix(0.75, 1.2, h1(fi*17.3));
  if (pol) vCol = mix(vec3(0.95, 0.88, 0.55), vec3(0.98, 0.96, 0.85), h1(fi*19.7));
  vC = q; vW = w; vN = ax; vA = a;
  gl_Position = uVP*vec4(w, 1.0);
  if (a <= 0.001) gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
}
)GLSL";
static const char* kLeafDriftFS = R"GLSL(
in vec2 vC; in vec3 vW, vN, vCol; in float vA; out vec4 oColor;
uniform vec4 uLD2;
void main(){
  if (uLD2.y > 0.5) {   // WS3 pollen: a round mote, lit mostly by the sun behind it (forward scattering)
    if (vA < 0.5 || dot(vC, vC) > 1.0) discard;
    float VoL = max(dot(normalize(vW - uCamPos), uSunDir), 0.0);
    oColor = vec4(applyFog(vCol*(uSunCol*(0.12 + 0.6*pow(VoL, 8.0)) + mix(uGroundCol, uSkyCol, 0.7)*0.8), vW), 1.0); return;   // pale motes: never dark specks against the sky
  }
  if (vA < 0.5 || dot(vC, vC) > 1.0 - 0.25*abs(vC.x)*(1.0 - vC.y)) discard;   // oval with a slight point
  vec3 N = normalize(vN); if (dot(N, uCamPos - vW) < 0.0) N = -N;
  float ndl = abs(dot(N, uSunDir))*0.7 + 0.3;
  vec3 c = vCol/PI*uSunCol*ndl + vCol*mix(uGroundCol, uSkyCol, 0.6)*0.6;
  oColor = vec4(applyFog(c, vW), 1.0);
}
)GLSL";
// Weather W6 (corpus 7.17-7.21): the particles belong to the scene's air. Per vertex (kCommon: the same fog and sun shadow as the
// geometry): an alpha sprite is lit like a matte surface facing the eye (sun x half phase x one shadow tap, sky / ground
// ambient) and fogged with applyFog; an additive one (fire, flashes) keeps its colour and loses what the fog absorbs. Per
// fragment: a soft fade over 0.35 x the sprite's radius where it meets the scene (a depth copy; no hard line through walls).
static const char* kFxVS = R"GLSL(
layout(location=0) in vec3 aPos; layout(location=1) in vec4 aCol; layout(location=2) in vec2 aUV; layout(location=3) in float aR;
uniform mat4 uVP; uniform int uAdd, uFxLit; out vec4 vC; out vec2 vUV; out float vR;
void main(){
  vC = aCol; vUV = aUV; vR = aR;
  if (uFxLit == 1) {
    if (uAdd == 0) {
      vec3 n = normalize(uCamPos - aPos);
      float sh = sunShadowRaw(aPos, n, 1.0, 1);
      // smoke scatters forward: Henyey-Greenstein g 0.5 toward the sun, scaled so a side view (90 deg) keeps the old half phase
      float ct = dot(-n, uSunDir), hg = 0.75/pow(1.25 - ct, 1.5);   // 4 pi x HG(0.5): 0.54 side-on, 6 into the sun
      float ph = 0.5*clamp(hg/0.537, 0.6, 4.0);
      // inside fog, dust or a storm the particle is lit by the air around it too (multiple scattering): the haze's colour by what
      // ~15 m of that air scatters (without it a dark puff in Dense fog went black, darker than anything near it)
      vec3 amb = mix(uGroundCol, uSkyCol, 0.75);
      if (wxPhys) amb += hazeColor(-n)*(1.0 - exp(-wxSigma*15.0));
      vec3 lit = aCol.rgb/PI*uSunCol*ph*sh + aCol.rgb*amb;
      vC.rgb = applyFog(lit, aPos);
      // the scene fog keeps a 12 m clear zone for close combat (applyFogAir); smoke and dust are the air itself, so they take the
      // preset haze over that first stretch too (without it a plume 10 m off in Dense fog got no in-scatter and read as a hole)
      if (wxPhys) { float dz = min(length(aPos - uCamPos), 12.0); vC.rgb = mix(vC.rgb, hazeColor(-n), 1.0 - exp(-wxSigma*dz)); }
    } else { applyFog(vec3(0.0), aPos); vC.rgb *= gFogT; }
  }
  gl_Position = uVP*vec4(aPos,1.0);
}
)GLSL";
static const char* kFxFS = R"GLSL(
in vec4 vC; in vec2 vUV; in float vR; uniform int uAdd; out vec4 o;
uniform sampler2D uSoftDepth; uniform int uSoft; uniform float uP22, uP32;
float softK(){   // 0 at the scene surface .. 1 a 0.35-radius in front of it (reverse-Z: distance = P32 / (depth + P22))
  if (uSoft == 0 || vR <= 0.0) return 1.0;
  float ds = uP32/(texelFetch(uSoftDepth, ivec2(gl_FragCoord.xy), 0).r + uP22), df = uP32/(gl_FragCoord.z + uP22);
  return clamp((ds - df)/(0.35*vR), 0.0, 1.0);
}
float fxH(vec2 p){ p = fract(p*vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x*p.y); }
float fxN(vec2 p){ vec2 i = floor(p), f = fract(p); f = f*f*(3.0 - 2.0*f);
  return mix(mix(fxH(i), fxH(i + vec2(1,0)), f.x), mix(fxH(i + vec2(0,1)), fxH(i + vec2(1,1)), f.x), f.y); }
void fxMain();
void main(){ fxMain(); float k = softK(); if (uAdd == 1) o.rgb *= k; else o.a *= k; }
void fxMain(){
  if (uAdd == 1 && vC.a > 150.0) {   // DF1 muzzle fireball sprite: alpha carries 200 + id + age; a soft, noise-ragged puff, its colour as given
    float id = floor(vC.a - 200.0), u = fract(vC.a - 200.0);
    vec2 p = vUV*2.0 - 1.0; float ang = id*2.399, cs = cos(ang), sn = sin(ang);
    p = vec2(cs*p.x - sn*p.y, sn*p.x + cs*p.y);
    float d = length(p);
    float n = fxN(p*2.2 + id*1.3)*0.55 + fxN(p*4.6 - id*0.7 + u*1.5)*0.30 + fxN(p*9.0 + id*2.1)*0.15;
    float body = 1.0 - d + (n - 0.5)*0.85;
    float a = smoothstep(-0.1, 0.75, body)*smoothstep(1.0, 0.55, d);   // soft edges
    float core = pow(clamp(1.0 - d, 0.0, 1.0), 2.0);
    o = vec4(vC.rgb*a*(0.55 + 0.9*core), 1.0);
    return;
  }
  if (uAdd == 1 && vC.a > 50.0) {   // FT1 flame sprite: alpha carries 100 + id + age; a noise-eroded, rotated body, hot core
    float id = floor(vC.a - 100.0), u = fract(vC.a - 100.0);
    vec2 p = vUV*2.0 - 1.0; float ang = id*2.399, cs = cos(ang), sn = sin(ang);
    p = vec2(cs*p.x - sn*p.y, sn*p.x + cs*p.y);
    float d = length(p);
    float n = fxN(p*2.6 + id*1.7)*0.6 + fxN(p*5.2 - id*0.9 + u*2.0)*0.4;
    float body = 1.0 - d + (n - 0.5)*(0.9 + 0.8*u);
    float a = smoothstep(0.05, 0.55, body)*smoothstep(1.0, 0.72, d);   // never to the quad edge
    float core = pow(clamp(1.0 - d, 0.0, 1.0), 1.5);
    o = vec4(vC.rgb*a*(0.5 + 1.2*core), 1.0);
    return;
  }
  if (uAdd == 0 && vUV.y > 3.0) {   // WP1 firing smoke: uv carries 2*id + (0..1) and 4 + (0..1); a noise-eroded, irregular wisp, not a disc
    float id = floor(vUV.x*0.5); vec2 p = (vUV - vec2(2.0*id, 4.0))*2.0 - 1.0;
    float d = length(p);
    float n = fxN(p*2.2 + id*1.7)*0.6 + fxN(p*4.7 - id*0.9)*0.4;
    float a = smoothstep(0.0, 0.55, 1.0 - d + (n - 0.5)*1.15)*smoothstep(1.0, 0.7, d);
    o = vec4(vC.rgb, vC.a*a);
    return;
  }
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
// CP 5.8 program-binary cache: a linked program is kept as its driver binary in %LOCALAPPDATA%/BattleFoundry/shaders
// (NMK_SHADER_CACHE: another folder; NMK_NO_SHADER_CACHE: off), keyed by every source string plus GL_RENDERER and
// GL_VERSION, so a driver update or any shader edit misses and recompiles; a binary the driver rejects falls back to source.
// shortcut: no size cap (a few hundred programs, tens of MB at most); add it to user_cache.cpp's pruning if it grows
namespace {
uint64_t fnv64(uint64_t h, const char* s) { for (; s && *s; ++s) { h ^= (uint8_t)*s; h *= 1099511628211ull; } return h; }
fs::path progCachePath(uint64_t key) { static const fs::path d = userCacheDir("shaders", "NMK_SHADER_CACHE"); char b[24]; std::snprintf(b, sizeof b, "%016llx.bin", (unsigned long long)key); return d / b; }
bool progCacheOn() { static const bool on = !std::getenv("NMK_NO_SHADER_CACHE"); return on; }
}
GLuint Renderer::compile(const char* vs, const char* fs, const char* name) {
  crumb("shader compile");   // UI5: the launch splash presents between programs
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
  uint64_t key = 1469598103934665603ull;
  for (const char* s : {(const char*)glGetString(GL_RENDERER), (const char*)glGetString(GL_VERSION), "|", vsHasVersion ? vs : vsrc[0], vsHasVersion ? "" : kCommon, vsHasVersion ? "" : vs, "|", fsHasVersion ? fs : fsrc[0], fsHasVersion ? "" : kCommon, fsHasVersion ? "" : fs})
    key = fnv64(key, s);
  if (progCacheOn()) {   // the cached binary, if the driver still takes it
    std::ifstream in(progCachePath(key), std::ios::binary);
    std::vector<char> buf;
    if (in) buf.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (buf.size() > sizeof(GLenum)) {
      GLenum fmt; std::memcpy(&fmt, buf.data(), sizeof fmt);
      GLuint p = glCreateProgram(); glProgramBinary(p, fmt, buf.data() + sizeof fmt, (GLsizei)(buf.size() - sizeof fmt));
      GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
      if (ok) { ++shaderCacheHits; return p; }
      glDeleteProgram(p);
    }
  }
  GLuint v = vsHasVersion ? sh(GL_VERTEX_SHADER, &vs, 1) : sh(GL_VERTEX_SHADER, vsrc, 3);
  GLuint f = fsHasVersion ? sh(GL_FRAGMENT_SHADER, &fs, 1) : sh(GL_FRAGMENT_SHADER, fsrc, 3);
  GLuint p = glCreateProgram(); glAttachShader(p, v); glAttachShader(p, f);
  if (progCacheOn()) glProgramParameteri(p, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
  glLinkProgram(p);
  GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) { char log[4096]; glGetProgramInfoLog(p, sizeof log, nullptr, log); logf("link %s error:\n%s", name, log); }
  glDeleteShader(v); glDeleteShader(f);
  if (ok && progCacheOn()) {   // keep the binary (a temp name, then renamed: a crash leaves no half file)
    GLint len = 0; glGetProgramiv(p, GL_PROGRAM_BINARY_LENGTH, &len);
    if (len > 0) {
      std::vector<char> buf(sizeof(GLenum) + len); GLenum fmt = 0; GLsizei got = 0;
      glGetProgramBinary(p, len, &got, &fmt, buf.data() + sizeof fmt); std::memcpy(buf.data(), &fmt, sizeof fmt);
      fs::path dst = progCachePath(key), tmp = dst; tmp += ".tmp"; std::error_code ec; fs::create_directories(dst.parent_path(), ec);
      { std::ofstream o(tmp, std::ios::binary); o.write(buf.data(), (std::streamsize)(sizeof fmt + got)); }
      fs::rename(tmp, dst, ec);
    }
  }
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

// CP2 Lighting Physical (corpus 04 / 14; docs/corpus-implementation.md step 2): the sun from its elevation by air mass
// (E_DN = 133,000 exp(-0.21 m) lux), the sky E = 800 + 15,500 sqrt(sin h) lux, both by the weather row's beam / diffuse
// ratios; sun colour 5,800 K high to 2,500 K at the horizon; ground bounce L_g = rho (E_sun sin h + E_sky) / pi with the
// map's ground albedo (snow 0.8, sand 0.3, else 0.2); the sky dome and fog scaled to the same sky; EV100 from the scene's
// illuminance (log2(E / 2.5)) with two-thirds compensation around clear midday (EV 15), clamped 9-15.5 by day, 3-15.5 at
// night. Engine units: illuminance / 30,000 lux (sun), radiance x pi / 30,000 (sky, ground); 1 engine radiance = 30,000 nits.
static vec3 kelvinRGB(float T) {   // blackbody colour, unit luminance (Helland's fit)
  float t = T / 100.0f, r, g, b;
  r = t <= 66 ? 1.0f : glm::clamp(1.2929f * std::pow(t - 60.0f, -0.1332f), 0.0f, 1.0f);
  g = t <= 66 ? glm::clamp(0.3901f * std::log(t) - 0.6318f, 0.0f, 1.0f) : glm::clamp(1.1298f * std::pow(t - 60.0f, -0.0755f), 0.0f, 1.0f);
  b = t >= 66 ? 1.0f : t <= 19 ? 0.0f : glm::clamp(0.5432f * std::log(t - 10.0f) - 1.1962f, 0.0f, 1.0f);
  vec3 c(r * r, g * g, b * b);   // ~linear
  return c / std::max(0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b, 1e-4f);
}
void Renderer::physLight(vec3& sun, vec3& sky, vec3& ground, vec3& fog, vec3& fill, vec3& zen, vec3& hor, vec3& skySunCol, bool main) {
  const float K = 30000.0f, mu = std::max(sunDir.y, 0.0f);
  const vec3 lw(0.2126f, 0.7152f, 0.0722f);
  const float h = glm::degrees(std::asin(glm::clamp(mu, 0.0f, 1.0f)));
  const float m = 1.0f / (std::max(mu, 1e-3f) + 0.50572f * std::pow(h + 6.07995f, -1.6364f));
  const float eDN = mu > 0.0f ? 133000.0f * std::exp(-0.21f * m) * wx.der.beamRatio : 0.0f;
  const float eSky = mu > 0.0f ? (800.0f + 15500.0f * std::sqrt(mu)) * wx.der.skyRatio : 0.0f;
  const vec3 sunC = kelvinRGB(glm::mix(2500.0f, 5800.0f, glm::smoothstep(0.0f, 0.5f, mu))) * wx.der.tint;
  extern bool snowLayerOn(); extern bool groundPaintArid();
  const float rho = snowLayerOn() ? 0.8f : groundPaintArid() ? 0.3f : 0.2f;
  const float oldSky = std::max(glm::dot(sky, lw), 1e-5f), oldSun = std::max(glm::dot(sun, lw), 1e-5f);
  const vec3 skyHue = sky / oldSky;
  vec3 nSun = sunC * (eDN / K), nSky = skyHue * wx.der.tint * (eSky / (3.14159265f * K));
  const float nightK = mu > 0.0f ? 0.0f : 1.0f;   // by night the time-of-day art (moon, sky) stays as it is, in the same units
  if (nightK > 0.0f) { nSun = sun; nSky = sky; }
  const float skyK = glm::dot(nSky, lw) / oldSky;
  zen *= skyK; hor *= skyK; fog *= skyK; fill *= skyK;
  skySunCol *= glm::dot(nSun, lw) / oldSun;
  sun = nSun; sky = nSky;
  ground = vec3(0.95f, 0.92f, 0.86f) * rho * (eDN * mu + eSky) / (3.14159265f * K);
  if (nightK > 0.0f) ground = ground * 0.0f + sky * rho;
  if (main) {
    const float eTot = std::max(eDN * mu + eSky, 1e-3f);
    const float evScene = std::log2(eTot / 2.5f);
    physEV = glm::clamp(15.0f + 0.67f * (evScene - 15.0f), mu > 0.0f ? 9.0f : 3.0f, 15.5f);
    physExpo = mu > 0.05f ? q.physExpoK * K / (1.2f * std::exp2(physEV)) : 0.0f;   // dusk / night: the engine's own night exposure (the art stays in its units)
  }
}
void Renderer::froxRun(const RenderView& v) {   // CP4 froxel fog: inject + integrate (High / Ultra)
  froxOn = perf.froxel && shadowTier >= 2 && q.lightNew && !q.legacy && !thumbMode && !v.underwater && progFroxInj && progFroxInt;
  if (!froxOn) return;
  const int NX = 160, NY = 90, NZ = 64;
  if (!froxInj) {
    for (GLuint* t : {&froxInj, &froxInt}) { glCreateTextures(GL_TEXTURE_3D, 1, t); glTextureStorage3D(*t, 1, GL_RGBA16F, NX, NY, NZ);
      glTextureParameteri(*t, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(*t, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      for (GLenum pn : {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_WRAP_R}) glTextureParameteri(*t, pn, GL_CLAMP_TO_EDGE); }
  }
  const float nearD = 0.5f, farD = 600.0f;
  const mat4 inv = glm::inverse(v.proj * v.view);
  GLuint p = progFroxInj; glUseProgram(p);
  glBindImageTexture(0, froxInj, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
  glUniformMatrix4fv(U(p, "uInvVP"), 1, GL_FALSE, glm::value_ptr(inv)); glUniform3fv(U(p, "uCam"), 1, glm::value_ptr(v.camPos));
  glUniform3i(U(p, "uN"), NX, NY, NZ); glUniform1f(U(p, "uNear"), nearD); glUniform1f(U(p, "uFar"), farD);
  glUniform1f(U(p, "uHazeD"), hazeSigma()); glUniform1f(U(p, "uHazeH"), wx.der.phys && wx.der.visM < 3000.0f ? std::max(lt.mapHazeH > 0.0f ? lt.mapHazeH : q.hazeHeight, 600.0f) : lt.mapHazeH > 0.0f ? lt.mapHazeH : q.hazeHeight);
  glUniform1f(U(p, "uEnvFog"), perf.envFog && fogTex ? envFogSigma() : 0.0f); glUniform1f(U(p, "uEnvFogH"), envFogH());
  glUniform1f(U(p, "uEnvTop"), lt.mapFogLayerTop); glUniform1f(U(p, "uEnvSoft"), lt.mapFogLayerSoft); glUniform1f(U(p, "uFogIndoor"), camIndoor ? 0.15f : 1.0f);
  glUniform4fv(U(p, "uFog2"), 1, glm::value_ptr(perf.envFog ? lt.fog2 : vec4(0.0f))); glUniform4fv(U(p, "uFogBox"), 1, glm::value_ptr(fogBox));
  glBindTextureUnit(0, fogTex); glUniform1i(U(p, "uFogField"), 0);
  glBindTextureUnit(1, shadowFarTex); glUniform1i(U(p, "uShFar"), 1); glUniformMatrix4fv(U(p, "uVPFar"), 1, GL_FALSE, glm::value_ptr(lightVPFar));
  glUniform2f(U(p, "uFarAx"), farAtlas ? 0.5f : 1.0f, farAtlas ? 0.5f : 0.0f); glUniform1i(U(p, "uShOn"), sunShK > 0.0f && q.farCascade ? 1 : 0);
  glUniform3fv(U(p, "uSunDir"), 1, glm::value_ptr(sunDir)); glUniform3fv(U(p, "uSunCol"), 1, glm::value_ptr(lastSunCol));
  glUniform3fv(U(p, "uAmb"), 1, glm::value_ptr(vec3(fogNow))); glUniform1f(U(p, "uG"), wx.der.fogG); glUniform1f(U(p, "uAlb"), wx.der.fogAlbedo); glUniform1f(U(p, "uCloudK"), wx.k);
  glDispatchCompute((NX + 7) / 8, (NY + 7) / 8, NZ);
  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
  p = progFroxInt; glUseProgram(p);
  glBindImageTexture(0, froxInj, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F); glBindImageTexture(1, froxInt, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
  glUniform3i(U(p, "uN"), NX, NY, NZ); glUniform1f(U(p, "uNear"), nearD); glUniform1f(U(p, "uFar"), farD);
  glDispatchCompute((NX + 7) / 8, (NY + 7) / 8, 1);
  glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
  froxNear = nearD; froxFar = farD;
}
void Renderer::uploadWeather() {   // W1: NmkWeather (binding 4), the layout kCommon names
  const WxState& s = wx.st; const WxDerived& d = wx.der;
  const vec4 b[8] = {vec4(s.cover, s.tau, s.visM, s.fogTopM), vec4((float)s.precip, s.precipMmH, s.windDirDeg, s.windMs), vec4(s.gust, s.gustPeriodS, s.tempC, s.dustVisM),
                     vec4(s.lightningPerMin, d.sigma, d.cullM, d.sunShadow), vec4(d.windXZ, d.windK, d.phys ? 1.0f : 0.0f), vec4(d.tint, d.fogG), vec4(d.eSunLux, d.eSkyLux, d.fogAlbedo, d.flash), vec4(s.wet, s.puddle, s.snowDepth, s.dustCover)};
  if (!wxUbo) { glCreateBuffers(1, &wxUbo); glNamedBufferStorage(wxUbo, sizeof b, nullptr, GL_DYNAMIC_STORAGE_BIT); }
  glNamedBufferSubData(wxUbo, 0, sizeof b, b);
  glBindBufferBase(GL_UNIFORM_BUFFER, 4, wxUbo);
}
int dbgViewFromName(const std::string& n) {   // CP0 debug views (Renderer::dbgView); the materials session owns 3-5
  static const char* names[] = {"off", "lum", "illum", "albedo", "furnace", "texel", "sky"};
  for (int k = 0; k < 7; ++k) if (n == names[k]) return k;
  return 0;
}
bool Renderer::init(int w, int h) {
  if (const char* e = std::getenv("NMK_DEBUG_VIEW")) dbgView = dbgViewFromName(e);
  progMain = compile(kMainVS, (std::string(tex::glslCommon()) + tex::glslMain() + kMainFS).c_str(), "main");   // L4 texture hooks first
  progGlass = compile(kMainVS, kGlassFS, "glass");
  progShadow = compile(kShadowVS, kShadowFS, "shadow");
  progShadowI = compile(kShadowIVS, kShadowFS, "shadow_inst");
  progDepth = compile(kDepthVS, kShadowFS, "depth prepass");
  { extern const char* groundPaintGlsl(); extern const char* snowTexGlsl(); std::string fs = std::string(tex::glslCommon()) + groundPaintGlsl() + snowTexGlsl() + kTerrainFS;   // L4 defines first, then lasso ground paint
    progTerrain = compile(kTerrainVS, fs.c_str(), "terrain");
    // PF terrain early depth: the same shader without the tile-mask discard, for every base chunk not under an active tile
    const std::string cut = "if (texelFetch(uTileMask, tc, 0).r > 0.5) discard;";
    size_t at = fs.find(cut);
    if (at != std::string::npos && fs.find("discard;", at + cut.size()) == std::string::npos && fs.substr(0, at).find("discard;") == std::string::npos) {   // statements only (comments mention the word)
      fs.replace(at, cut.size(), ";");
      progTerrainND = compile(kTerrainVS, fs.c_str(), "terrain_nodiscard");
      progTerrainDepth = compile(kTerrainVS, kTerrainDepthFS, "terrain_depth");
    } else { ++shaderErrors; logf("shader terrain_nodiscard: kTerrainFS no longer has exactly one discard, the tile-mask line \"%s\" (tzearly would do nothing)", cut.c_str()); } }
  tex::init(contentDir() / "textures"); tex::attach(progMain); tex::attach(progTerrain); if (progTerrainND) tex::attach(progTerrainND);   // L4: ambientCG sets, units 20-22
  progSky = compile(kSkyVS, kSkyFS, "sky");
  progTone = compile(kFullVS, kToneFS, "tone");
  progGradeLut = compile(kFullVS, kGradeLutFS, "grade_lut");   // VO L8
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
  progLeafDrift = compile(kLeafDriftVS, kLeafDriftFS, "leaf_drift");   // WS2
  { GLint ok = 0; glGetProgramiv(progLeafDrift, GL_LINK_STATUS, &ok); if (!ok) { glDeleteProgram(progLeafDrift); progLeafDrift = 0; } }   // a failed program would draw with whatever is bound
  progSnowfall = compile(kSnowfallVS, kSnowfallFS, "snowfall"); glCreateVertexArrays(1, &snowfallVao);
  progGrass = compile(kGrassVS, kGrassFS, "grass");
  progGrassShadow = compile(kGrassVS, kGrassShadowFS, "grass_shadow");
  progGrassPre = compile(kGrassVS, (std::string("#define NMK_PRE 1\n") + kGrassFS).c_str(), "grass_pre");
  { GLint ok = 0; glGetProgramiv(progGrassPre, GL_LINK_STATUS, &ok); if (!ok) { glDeleteProgram(progGrassPre); progGrassPre = 0; } }   // CP1: no grass prepass rather than no grass
  { extern bool grassAtlasInit(GLuint, GLuint, GLuint); grassAtlasReady = grassAtlasInit(progGrass, progGrassShadow, progGrassPre); }   // GT1 (grass.cpp)
  progModel = compile(kModelVS, kModelFS, "model");
  {   // GL1 window glass clarity (content/settings/glass.json)
    json g; try { g = json::parse(readTextFile(contentDir() / "settings" / "glass.json")); } catch (...) { logf("glass.json: missing or invalid, the old look"); }
    float op = std::clamp(g.value("opacity", 1.0f), 0.0f, 2.0f), film = std::clamp(g.value("film", 1.0f), 0.0f, 2.0f), refl = std::clamp(g.value("reflection", 1.0f), 0.0f, 2.0f);
    for (GLuint p : {progGlass, progModel}) { glProgramUniform1f(p, glGetUniformLocation(p, "uGlassOpacity"), op); glProgramUniform1f(p, glGetUniformLocation(p, "uGlassRefl"), refl); }
    glProgramUniform1f(progModel, glGetUniformLocation(progModel, "uGlassFilm"), film);
  }
  progFolModel = compile(kFolVS, kModelFS, "fol_model"); progFolDepth = compile(kFolVS, kFolDepthFS, "fol_depth");   // NT8
  progImpBake = compile(kFolVS, kImpBakeFS, "imp_bake"); progImp = compile(kImpVS, kImpFS, "impostor");   // CP3
  for (GLuint* pp : {&progImpBake, &progImp}) { GLint ok = 0; glGetProgramiv(*pp, GL_LINK_STATUS, &ok); if (!ok) { glDeleteProgram(*pp); *pp = 0; } }   // a failed program: no impostors
  progDof = compile(kFullVS, kDofFS, "dof");
  progEyepiece = compile(kFullVS, kEyepieceFS, "eyepiece");
  progHeat = compile(kHeatVS, kHeatFS, "thermal heat");   // TH1
  progShaft = compile(kFullVS, kShaftFS, "shafts");
  progSsao = compile(kFullVS, kSsaoFS, "ssao");
  progContactAO = compile(kContactAOVS, kContactAOFS, "contact_ao");   // BF B4
  { GLint ok = 0; if (progContactAO) glGetProgramiv(progContactAO, GL_LINK_STATUS, &ok); if (!ok) progContactAO = 0; }
  progBloomDown = compile(kFullVS, kBloomDownFS, "bloom_down");
  progBloomUp = compile(kFullVS, kBloomUpFS, "bloom_up");
  progTaa = compile(kFullVS, kTaaFS, "taa");
  progMotion = compile(kFullVS, kMotionFS, "upscale motion"); progDepthUp = compile(kFullVS, kDepthUpFS, "upscale depth");
  progCloudSh = compile(kFullVS, kCloudShadowFS, "cloud_shadow");
  glCreateTextures(GL_TEXTURE_2D, 1, &cloudShTex); glTextureStorage2D(cloudShTex, 1, GL_R16F, 512, 512);
  glTextureParameteri(cloudShTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(cloudShTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameteri(cloudShTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(cloudShTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glCreateFramebuffers(1, &cloudShFbo); glNamedFramebufferTexture(cloudShFbo, GL_COLOR_ATTACHMENT0, cloudShTex, 0);
  if (glCheckNamedFramebufferStatus(cloudShFbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { logf("cloud shadow framebuffer incomplete"); ++fboErrors; }
  progComposite = compile(kFullVS, kCompositeFS, "composite");
  {   // CP4 froxel fog: two standalone compute programs
    auto cs = [&](const char* src, const char* name) {
      GLuint sh = glCreateShader(GL_COMPUTE_SHADER); glShaderSource(sh, 1, &src, nullptr); glCompileShader(sh);
      GLint ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
      if (!ok) { char log[4096]; glGetShaderInfoLog(sh, sizeof log, nullptr, log); logf("shader %s (cs) error: %s", name, log); ++shaderErrors; }
      GLuint pr = glCreateProgram(); glAttachShader(pr, sh); glLinkProgram(pr); glDeleteShader(sh);
      glGetProgramiv(pr, GL_LINK_STATUS, &ok); if (!ok) { logf("link %s failed", name); ++shaderErrors; glDeleteProgram(pr); return 0u; }
      return pr;
    };
    progFroxInj = cs(kFroxInjectCS, "frox_inject"); progFroxInt = cs(kFroxIntegrateCS, "frox_integrate");
  }
  progSsrHalf = compile(kFullVS, (std::string("#define NMK_SSR_HALF 1\n") + kCompositeFS).c_str(), "ssr_half");   // OPT ssrhalf
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
  for (int k = 0; k < 4; ++k) { glEnableVertexArrayAttrib(fxVao, k); glVertexArrayAttribBinding(fxVao, k, 0); }
  glVertexArrayAttribFormat(fxVao, 3, 1, GL_FLOAT, GL_FALSE, offsetof(FxVert, r));   // W6: the sprite radius
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
  warmPrograms();   // CP 5.8: no program left to compile on first use
  logf("shaders: %d programs from the binary cache", shaderCacheHits);
  return true;
}

// Shadow maps (near cascade and far cascade): sizes come from the settings presets (setShadowSizes).
void Renderer::createShadowTargets() {
  farFrame = occFrame = -1000;   // PF2 caches: the maps are new
  if (shadowFbo) { glDeleteFramebuffers(1, &shadowFbo); glDeleteFramebuffers(1, &shadowFarFbo); GLuint t[2] = {shadowTex, shadowFarTex}; glDeleteTextures(2, t); }
  glCreateTextures(GL_TEXTURE_2D, 1, &shadowTex);
  glTextureStorage2D(shadowTex, 1, GL_DEPTH_COMPONENT32F, shadowSize * 2, shadowSize);   // VO L3: near | mid atlas (the mid half unused in Classic / on Low)
  glTextureParameteri(shadowTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(shadowTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameteri(shadowTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(shadowTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTextureParameteri(shadowTex, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE); glTextureParameteri(shadowTex, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
  glCreateFramebuffers(1, &shadowFbo); glNamedFramebufferTexture(shadowFbo, GL_DEPTH_ATTACHMENT, shadowTex, 0);
  glNamedFramebufferDrawBuffer(shadowFbo, GL_NONE); glNamedFramebufferReadBuffer(shadowFbo, GL_NONE);
  // far cascade (same comparison sampler setup)
  glCreateTextures(GL_TEXTURE_2D, 1, &shadowFarTex);
  farAtlas = shadowFarSize >= 4096;   // CP4 cascade4 (High / Ultra): the far map is 2 wide, the fourth cascade | far
  glTextureStorage2D(shadowFarTex, 1, GL_DEPTH_COMPONENT32F, shadowFarSize * (farAtlas ? 2 : 1), shadowFarSize);
  glTextureParameteri(shadowFarTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(shadowFarTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameteri(shadowFarTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(shadowFarTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTextureParameteri(shadowFarTex, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE); glTextureParameteri(shadowFarTex, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
  glCreateFramebuffers(1, &shadowFarFbo); glNamedFramebufferTexture(shadowFarFbo, GL_DEPTH_ATTACHMENT, shadowFarTex, 0);
  glNamedFramebufferDrawBuffer(shadowFarFbo, GL_NONE); glNamedFramebufferReadBuffer(shadowFarFbo, GL_NONE);
  // GP1 shadowcache: the far cascade's static layer (buildings, props, terrain-standing trees); copied into shadowFarTex
  if (farStaticFbo) { glDeleteFramebuffers(1, &farStaticFbo); glDeleteTextures(1, &farStaticTex); }
  glCreateTextures(GL_TEXTURE_2D, 1, &farStaticTex);
  glTextureStorage2D(farStaticTex, 1, GL_DEPTH_COMPONENT32F, shadowFarSize, shadowFarSize);
  glCreateFramebuffers(1, &farStaticFbo); glNamedFramebufferTexture(farStaticFbo, GL_DEPTH_ATTACHMENT, farStaticTex, 0);
  glNamedFramebufferDrawBuffer(farStaticFbo, GL_NONE); glNamedFramebufferReadBuffer(farStaticFbo, GL_NONE);
  farSig = ~0ull; farFrame = -1000;
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
  int d = full ? 1 : std::clamp(q.ssaoDiv, 1, 4);
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
  glCreateTextures(GL_TEXTURE_2D, 1, &aoTex); glTextureStorage2D(aoTex, 1, GL_RGBA16F, std::max(1, width / aoDiv), std::max(1, height / aoDiv));
  glTextureParameteri(aoTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(aoTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameteri(aoTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(aoTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glCreateFramebuffers(1, &aoFbo); glNamedFramebufferTexture(aoFbo, GL_COLOR_ATTACHMENT0, aoTex, 0);
  if (ssrTex) { glDeleteTextures(1, &ssrTex); glDeleteFramebuffers(1, &ssrFbo); }   // OPT ssrhalf: reflection at half resolution (rgb, view depth)
  glCreateTextures(GL_TEXTURE_2D, 1, &ssrTex); glTextureStorage2D(ssrTex, 1, GL_RGBA16F, std::max(1, width / 2), std::max(1, height / 2));
  glTextureParameteri(ssrTex, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTextureParameteri(ssrTex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTextureParameteri(ssrTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(ssrTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glCreateFramebuffers(1, &ssrFbo); glNamedFramebufferTexture(ssrFbo, GL_COLOR_ATTACHMENT0, ssrTex, 0);
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
  // a full card (several games at once) can fail these allocations: the targets then hold undefined memory (garbage frames)
  for (GLenum e; (e = glGetError()) != GL_NO_ERROR;) { logf("render targets %dx%d: GL error 0x%04x%s", width, height, e, e == GL_OUT_OF_MEMORY ? " (out of video memory)" : ""); ++fboErrors; }
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
  int iw = std::max(16, (int)std::lround(w * effScale())), ih = std::max(16, (int)std::lround(h * effScale()));
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

// ---- VO L5 atmosphere (Hillaire 2020's model, single scattering, evaluated on the CPU for a few directions): Rayleigh,
// Mie and an ozone layer over a spherical Earth, the sun's transmittance and the sky radiance at the zenith, the horizon
// (mean, sun side, far side) and the cosine-weighted hemisphere. Lighting New scales each map's authored colours by the
// ratio of now to the authored hour (setLighting), so the authored look is kept and the atmosphere carries the change.
namespace {
using AtmoOut = Renderer::AtmoSamples;
AtmoOut atmoEval(vec3 sun) {
  const vec3 bR(5.802e-6f, 13.558e-6f, 33.1e-6f), bO(0.650e-6f, 1.881e-6f, 0.085e-6f);
  const float bM = 3.996e-6f, bMe = 4.40e-6f, Re = 6360e3f, Ra = 6460e3f, HR = 8000.0f, HM = 1200.0f;
  const vec3 p0(0.0f, Re + 2.0f, 0.0f);
  auto ext = [&](float h) { float r = std::exp(-h / HR), m = std::exp(-h / HM), o = std::max(0.0f, 1.0f - std::fabs(h - 25000.0f) / 15000.0f); return bR * r + vec3(bMe * m) + bO * o; };
  auto top = [&](vec3 p, vec3 d) { float b = glm::dot(p, d), c = glm::dot(p, p) - Ra * Ra; return -b + std::sqrt(std::max(b * b - c, 0.0f)); };
  auto ground = [&](vec3 p, vec3 d) { float b = glm::dot(p, d), c = glm::dot(p, p) - Re * Re, D = b * b - c; return b < 0.0f && D > 0.0f ? -b - std::sqrt(D) : -1.0f; };
  auto trans = [&](vec3 p, vec3 d) -> vec3 {
    if (ground(p, d) > 0.0f) return vec3(0.0f);
    float L = top(p, d); vec3 od(0.0f); const int n = 10;
    for (int i = 0; i < n; ++i) { vec3 q = p + d * (L * (i + 0.5f) / n); od += ext(glm::length(q) - Re) * (L / n); }
    return glm::exp(-od);
  };
  auto radiance = [&](vec3 d) -> vec3 {
    float L = top(p0, d), gd = ground(p0, d); if (gd > 0.0f) L = gd;
    float mu = glm::dot(d, sun), g = 0.8f;
    float pr = 3.0f / (16.0f * 3.14159265f) * (1.0f + mu * mu);
    float pm = 3.0f / (8.0f * 3.14159265f) * ((1.0f - g * g) * (1.0f + mu * mu)) / ((2.0f + g * g) * std::pow(1.0f + g * g - 2.0f * g * mu, 1.5f));
    vec3 sum(0.0f), od(0.0f); const int n = 14;
    for (int i = 0; i < n; ++i) {
      float dt = L / n; vec3 q = p0 + d * (dt * (i + 0.5f)); float h = glm::length(q) - Re;
      od += ext(h) * dt;
      float r = std::exp(-h / HR), m = std::exp(-h / HM);
      sum += glm::exp(-od) * trans(q, glm::normalize(sun)) * (bR * r * pr + vec3(bM * m * pm)) * dt;
    }
    return sum + vec3(1e-6f);
  };
  AtmoOut o; o.sunT = trans(p0, sun) + vec3(1e-6f);
  o.zen = radiance(vec3(0, 1, 0));
  vec2 az = glm::length(vec2(sun.x, sun.z)) > 1e-4f ? glm::normalize(vec2(sun.x, sun.z)) : vec2(1, 0);
  const float e = 0.05f;   // ~3 deg above the horizon
  o.hor = vec3(0.0f);
  for (int k = 0; k < 8; ++k) { float a = k * 0.7853982f; o.hor += radiance(glm::normalize(vec3(std::cos(a), e, std::sin(a)))) / 8.0f; }
  o.horSun = radiance(glm::normalize(vec3(az.x, e, az.y))); o.horAnti = radiance(glm::normalize(vec3(-az.x, e, -az.y)));
  o.amb = vec3(0.0f); float wsum = 0.0f;
  for (int i = 0; i < 4; ++i) for (int k = 0; k < 6; ++k) {
    float el = (i + 0.5f) / 4.0f * 1.5707963f, a = k * 1.0471976f; vec3 d(std::cos(el) * std::cos(a), std::sin(el), std::cos(el) * std::sin(a));
    float w = std::sin(el) * std::cos(el); o.amb += radiance(d) * w; wsum += w;
  }
  o.amb /= wsum;
  return o;
}
float gAtmoK = 1.0f;   // the map's look.atmo while setLighting runs
vec3 ratioC(vec3 a, vec3 b) { return glm::pow(glm::clamp(a / glm::max(b, vec3(1e-12f)), vec3(0.2f), vec3(5.0f)), vec3(gAtmoK)); }
}  // namespace
void Renderer::setLighting(GLuint p, const RenderView& v) {
  glUniform1f(glGetUniformLocation(p, "uSmokeSigma"), smokeSigma); glUniform3fv(glGetUniformLocation(p, "uSmokeCol"), 1, &smokeCol.x);   // weather 7: for applyFog (the lighting session's line)
  glUseProgram(p);
  // thumbnails / model sheets / item icons: a fixed studio state whatever map, weather or hour is loaded. Uniforms
  // persist per program, so everything the main frame sets per map is pinned here: a night / low sun (the asset tiles
  // set their own key light; the template and sheet renders kept the map's), rain wetness, grime, arid paint, the
  // material layer / relief, cloud deck
  const vec3 sunDirMap = sunDir;
  if (thumbMode && sunDir.y < 0.25f) sunDir = glm::normalize(vec3(0.45f, 0.8f, 0.4f));
  if (thumbMode) {
    glUniform1i(U(p, "uWeather"), 0); glUniform1f(U(p, "uMapArid"), 0.0f); glUniform1f(U(p, "uMapCave"), 0.0f); glUniform1f(U(p, "uMapGrime"), 0.0f);
    glUniform1i(U(p, "uMatLayer"), 0); glUniform1f(U(p, "uMatFar"), 0.0f); glUniform1i(U(p, "uRelief"), 0); glUniform1i(U(p, "uMatLod"), 0);
    glUniform1i(U(p, "uCloudOn"), 0);
  }
  glUniform3fv(U(p, "uSunDir"), 1, glm::value_ptr(sunDir));
  // sun : sky balance. Legacy ~1.75:1 on lit diffuse (flat, milky shadows); now ~3:1 with a cooler sky fill so
  // shadowed sides read blue-grey and lit faces warm, while shadows stay open (no crushed blacks).
  vec3 sun(2.65f, 2.52f, 2.32f), sky(0.42f, 0.49f, 0.60f), ground(0.20f, 0.19f, 0.155f), fog(0.64f, 0.68f, 0.72f), fill(0.10f, 0.095f, 0.08f);
  if (!q.legacy) { sun = vec3(3.30f, 3.05f, 2.62f); sky = vec3(0.30f, 0.36f, 0.47f); ground = vec3(0.17f, 0.155f, 0.12f); fill = vec3(0.075f, 0.072f, 0.065f); }
  vec3 zen(0.28f, 0.40f, 0.62f), hor(0.80f, 0.84f, 0.87f), skySun = sunDir, skySunCol = sun;
  lightingTodColors(*this, sun, sky, ground, fog, fill, zen, hor, skySun, skySunCol);   // L2 time of day (no change by Day / in thumbnails)
  vec2 horAz(1.0f);   // VO L5: horizon brightness toward / away from the sun, now relative to the authored hour
  if (!thumbMode && !q.legacy) {   // VO L5: authored colours at the authored hour; the atmosphere moves them away from it
    const float authH = lt.mapHour >= 0.0f ? lt.mapHour : 14.0f;
    // shortcut: the snapshot is the last one taken at an authored hour; a map opened straight at another hour (Settings hour)
    // uses the previous map's until its own authored hour comes round; key it on the map load if that shows
    if (std::fabs(lt.hour - authH) < 0.02f) {   // at the authored hour: its colours (they follow weather / map changes); its atmosphere when the sun moved
      bool moved = !atmoRef.valid || glm::distance(sunDir, atmoRef.sunDir) > 0.002f;
      atmoRef = {true, sunDir, sun, sky, ground, fog, zen, hor};
      if (moved) atmoRefT = atmoEval(sunDir);
    }
    if (q.lightNew && perf.atmo && atmoRef.valid && atmoRef.sunDir.y > 0.02f) {
      if (glm::distance(sunDir, atmoNowDir) > 0.002f) { atmoNow = atmoEval(sunDir); atmoNowDir = sunDir; }
      const AtmoOut& A = atmoNow; const AtmoOut& B = atmoRefT;
      gAtmoK = lt.mapAtmo;
      float w = glm::smoothstep(0.02f, 0.10f, sunDir.y);   // toward dusk and night the time-of-day art takes over again
      vec3 lumW(0.2126f, 0.7152f, 0.0722f);
      sun = glm::mix(sun, atmoRef.sun * ratioC(A.sunT, B.sunT), w); skySunCol = sun;
      sky = glm::mix(sky, atmoRef.sky * ratioC(A.amb, B.amb), w);
      ground = glm::mix(ground, atmoRef.ground * glm::clamp(glm::dot(A.sunT * std::max(sunDir.y, 0.0f) + A.amb, lumW) / std::max(glm::dot(B.sunT * std::max(atmoRef.sunDir.y, 0.0f) + B.amb, lumW), 1e-9f), 0.2f, 5.0f), w);
      zen = glm::mix(zen, atmoRef.zen * ratioC(A.zen, B.zen), w);
      hor = glm::mix(hor, atmoRef.hor * ratioC(A.hor, B.hor), w);
      fog = glm::mix(fog, atmoRef.fog * ratioC(A.hor, B.hor), w);
      float sa = glm::dot(A.horSun, lumW) / std::max(glm::dot(A.hor, lumW), 1e-12f), aa = glm::dot(A.horAnti, lumW) / std::max(glm::dot(A.hor, lumW), 1e-12f);
      float sb = glm::dot(B.horSun, lumW) / std::max(glm::dot(B.hor, lumW), 1e-12f), ab = glm::dot(B.horAnti, lumW) / std::max(glm::dot(B.hor, lumW), 1e-12f);
      horAz = glm::mix(vec2(1.0f), glm::pow(glm::clamp(vec2(sa / sb, aa / ab), vec2(0.5f), vec2(2.0f)), vec2(gAtmoK)), w);
    }
  }
  glUniform2fv(U(p, "uAtmoHorAz"), 1, glm::value_ptr(horAz));
  glUniform4fv(U(p, "uCloudOpen"), 1, glm::value_ptr(lt.cloudOpen)); glUniform1f(U(p, "uCloudOpenK"), thumbMode ? 0.0f : lt.cloudOpenK);   // VO L5 per-map sky knobs
  { vec4 xf = thumbMode ? vec4(1, 0, 0, 1) : lt.cloudXf; glUniform4fv(U(p, "uCloudXf"), 1, glm::value_ptr(xf)); glUniform1f(U(p, "uSunDisc"), thumbMode ? 1.0f : lt.sunDisc); }
  glUniform1f(U(p, "uAerial"), q.lightNew && perf.atmo && !q.legacy && !thumbMode ? lt.mapAerial : 0.0f);   // VO L5 (look.aerial)
  if (wx.der.flash > 0.0f && !thumbMode) { const vec3 fl = vec3(0.85f, 0.9f, 1.0f) * wx.der.flash; sky += fl * 2.5f; fill += fl * 0.6f; zen += fl * 3.0f; hor += fl * 2.0f; }   // W2 lightning: the sky and the ambient flash (cloud-lit, no direction under the deck)
  if (wx.der.phys && !thumbMode) {   // W1 preset light (weather.cpp): sun and sky by the corpus's formulas, as ratios to Clear; fog takes the horizon's hue
    WxDerived& d = wx.der; const vec3 lw(0.2126f, 0.7152f, 0.0722f);
    if (p == progMain) {   // W2: two-thirds compensation against Clear at this sun height and this map's own sun / sky balance (a low golden sun is most of the light)
      const float mu = std::max(sunDir.y, 0.0f), ls = glm::dot(sun, lw), lk = glm::dot(sky, lw);
      const float eOld = ls * mu / 3.14159265f + lk, eNew = ls * d.sunK * mu / 3.14159265f + lk * d.skyK;
      const float r = std::max(eNew / std::max(eOld, 1e-5f), 1e-3f);
      d.keyMul = std::pow(r, 1.0f / 3.0f); d.fixedMul = std::pow(r, -2.0f / 3.0f);
    }
    const float fogL = glm::dot(fog, lw) * d.skyK;
    sun *= d.sunK * d.tint; skySunCol *= d.sunK * d.tint;
    sky *= d.skyK * d.tint; ground *= d.skyK * d.tint; fill *= d.skyK; zen *= d.skyK * d.tint; hor *= d.skyK * d.tint;
    fog = hor * (fogL / std::max(glm::dot(hor, lw), 1e-5f));
  }
  if (q.lightPhys && q.lightNew && !q.legacy && !thumbMode) physLight(sun, sky, ground, fog, fill, zen, hor, skySunCol, p == progMain);   // CP2 Lighting Physical
  if (!thumbMode && (lt.hour < 5.8f || lt.hour > 20.4f)) { sky *= lt.mapNightSky; fill *= lt.mapNightFill; }   // GR3: lighting.nightSky (a faint cool sky light on upward faces) / nightFill (rooms lit from within) at night
  if (!thumbMode) fill = fill * lt.mapFill + (p == progModel ? lt.mapAmbient * 0.4f : lt.mapAmbient);   // UG4: x the map's fill scale   // BR2: a map's own even ambient (Backrooms); BR4: characters and the viewmodel take 40 %, so the lights and their shadows shape them
  glUniform3fv(U(p, "uSkyZen"), 1, glm::value_ptr(zen)); glUniform3fv(U(p, "uSkyHor"), 1, glm::value_ptr(hor));
  { vec3 sk = hor / vec3(0.80f, 0.84f, 0.87f); glUniform3fv(U(p, "uSkyK"), 1, glm::value_ptr(sk)); }
  glUniform3fv(U(p, "uSkySun"), 1, glm::value_ptr(skySun)); glUniform3fv(U(p, "uSkySunCol"), 1, glm::value_ptr(skySunCol));
  if (v.underwater && !thumbMode) { sun *= vec3(0.22f, 0.38f, 0.40f); sky *= vec3(0.45f, 0.75f, 0.80f); ground *= vec3(0.45f, 0.75f, 0.80f); }   // swim.cpp: light filtered by the water above
  glUniform3fv(U(p, "uSunCol"), 1, glm::value_ptr(sun)); if (p == progMain) lastSunCol = sun;   // CP4 froxel inject
  { const vec3 lw(0.2126f, 0.7152f, 0.0722f); float d = glm::dot(sun, lw) * std::max(sunDir.y, 0.0f) / 3.14159265f, a = glm::dot(sky, lw);
    sunDirectFrac = d / std::max(d + a, 1e-6f); }   // OPT wxsun: the sun's share of a level surface's light (shade(): sun x N.L / pi against the sky term), read by the next frame's cascades
  glUniform3fv(U(p, "uSkyCol"), 1, glm::value_ptr(sky));
  glUniform3fv(U(p, "uGroundCol"), 1, glm::value_ptr(ground));
  glUniform3fv(U(p, "uFogCol"), 1, glm::value_ptr(fog));
  glUniform3fv(U(p, "uFill"), 1, glm::value_ptr(fill));
  glUniform3fv(U(p, "uCamPos"), 1, glm::value_ptr(v.camPos));
  const float hazeD = thumbMode ? 0.0f : hazeSigma();   // W1: a preset's visibility replaces the map's haze
  glUniform1f(U(p, "uFogDens"), hazeD);   // BR2: a map may have no haze
  if (v.underwater && !thumbMode) { fog = v.underFogCol; glUniform3fv(U(p, "uFogCol"), 1, glm::value_ptr(fog)); glUniform1f(U(p, "uFogDens"), v.underFogDens); }   // swim.cpp
  glUniform1f(U(p, "uUnder"), v.underwater && !thumbMode ? 1.0f : 0.0f);
  if (perf.snow) snowUniforms(p, thumbMode); else glUniform1i(U(p, "uSnowOn"), 0);   // SN1 (snow.cpp): off unless the map has a snow layer
  glUniform1i(U(p, "uSnowFar"), perf.snowFar ? 1 : 0);   // BT2 switch snowfar
  glUniform1i(U(p, "uSnowTexOn"), perf.snowTex ? 1 : 0);   // SN2 switch snowtex
  glUniform1f(U(p, "uGrassDead"), thumbMode ? 0.0f : lt.mapGrassDead);   // BT1
  if (p == progMain) fogNow = vec4(fog, v.underwater && !thumbMode ? v.underFogDens : hazeD);
  glUniformMatrix4fv(U(p, "uLightVP"), 1, GL_FALSE, glm::value_ptr(lightVP));
  glUniformMatrix4fv(U(p, "uOccVP"), 1, GL_FALSE, glm::value_ptr(occVP));
  glUniform1f(U(p, "uOccTop"), occTop); glUniform1f(U(p, "uOccRange"), occRange);
  glUniform1i(U(p, "uShadowOn"), thumbMode || !perf.sunShadow || sunShK <= 0.0f ? 0 : 1);
  glUniform1i(U(p, "uOccOn"), thumbMode ? 0 : 1);
  glUniform2f(U(p, "uShadowTexel"), 0.5f / shadowSize, 1.0f / shadowSize);   // VO L3: the atlas is 2 maps wide
  glUniform1f(U(p, "uShadowAtlas"), 0.5f);
  glUniform1i(U(p, "uShadowMidOn"), midOn && !thumbMode ? 1 : 0);
  glUniformMatrix4fv(U(p, "uLightVPMid"), 1, GL_FALSE, glm::value_ptr(lightVPMid));
  glUniform1f(U(p, "uShadowWorldTexelMid"), 2 * midR / shadowSize);
  glUniform1i(U(p, "uShadow"), 0); glUniform1i(U(p, "uOcc"), 1);
  glBindTextureUnit(0, shadowTex); glBindTextureUnit(1, occTex);
  bool far = !q.legacy && q.farCascade && !thumbMode;
  glUniform1i(U(p, "uShadowFar"), 6); glBindTextureUnit(6, shadowFarTex);
  glUniform1i(U(p, "uShadowFarOn"), far ? 1 : 0);
  glUniform1i(U(p, "uTSV"), 26); glUniform1i(U(p, "uTHorOn"), thorTex && !thumbMode ? 1 : 0); glUniform1i(U(p, "uTHorAll"), p == progTerrain || p == progTerrainND || p == progGrass ? 1 : 0);   // VOT T6 (unit 26 = the terrain's T5 / T6 texture)
  if (thorTex && !thumbMode) { glBindTextureUnit(26, thorTex); glUniform4fv(U(p, "uTHorMap"), 1, glm::value_ptr(thorMap)); glUniform4fv(U(p, "uTHorW"), 1, glm::value_ptr(thorW)); }
  glUniformMatrix4fv(U(p, "uLightVPFar"), 1, GL_FALSE, glm::value_ptr(lightVPFar));
  glUniform2f(U(p, "uShadowTexelFar"), (farAtlas ? 0.5f : 1.0f) / shadowFarSize, 1.0f / shadowFarSize);
  glUniform2f(U(p, "uFarAx"), farAtlas ? 0.5f : 1.0f, farAtlas ? 0.5f : 0.0f);
  glUniform1i(U(p, "uC3On"), c3On && !thumbMode ? 1 : 0); glUniformMatrix4fv(U(p, "uLightVPC3"), 1, GL_FALSE, glm::value_ptr(lightVPC3)); glUniform1f(U(p, "uShadowWorldTexelC3"), 2 * c3R / shadowFarSize);
  glUniform2f(U(p, "uShadowWorldTexel"), 2 * nearR / shadowSize, 2 * farR / shadowFarSize);
  glUniform1f(U(p, "uShadowDepthRange"), 800.0f); glUniform1f(U(p, "uSlopeCap"), perf.slopeCap && !q.legacy ? 2.0f : 6.0f);
  glUniform1f(U(p, "uShadowSoft"), q.legacy ? 0.0f : q.shadowSoft * (1.0f + 2.0f * wx.k));   // WX2: overcast sun casts very soft shadows
  glUniform1f(U(p, "uShadowLift"), (q.lightNew && perf.fullShadow ? q.newShadowLift : 0.7f) * sunShK);   // VO L1
  glUniform1f(U(p, "uShadowWarm"), q.lightNew && perf.fullShadow && !q.legacy ? q.newShadowWarm : 0.0f);   // VO L1
  glUniform1i(U(p, "uReflOn"), reflOn && q.lightNew && perf.reflProbe && !q.legacy && !thumbMode ? 1 : 0); glUniform3fv(U(p, "uReflPos"), 1, glm::value_ptr(reflPos));   // VO L7
  glUniform1i(U(p, "uReflRoom"), reflRoom >= 0 && perf.reflRoom ? 1 : 0); glUniformMatrix4fv(U(p, "uReflRoomInv"), 1, GL_FALSE, glm::value_ptr(reflRoomInv)); glUniform3fv(U(p, "uReflRoomHalf"), 1, glm::value_ptr(reflRoomHalf));   // CP4
  glUniform1i(U(p, "uRefl"), 9); if (p != progComposite && reflTex) glBindTextureUnit(9, reflTex);   // unit 9: free in the world pass (the composite binds its own there first)
  glUniform1i(U(p, "uSkyRelit"), q.lightNew && perf.skyRelit && !q.legacy ? 1 : 0);   // VO L6
  glUniform1f(U(p, "uMicroSh"), q.lightNew && perf.microShadow && !q.legacy ? 1.0f : 0.0f);   // VO L4
  glUniform1i(U(p, "uShadowTaps"), q.shadowTaps);
  glUniform1i(U(p, "uPcss"), q.ultra && perf.pcss && q.lightNew && !q.legacy ? 1 : 0);   // UL1
  glUniform1i(U(p, "uFrame"), frame);
  glUniform1i(U(p, "uCloudShadowOn"), !q.legacy && q.cloudShadows && clouds && perf.cloudShadow && !thumbMode ? 1 : 0);
  glUniform1f(U(p, "uCloudCov"), cloudCoverNow()); glUniform1f(U(p, "uCloudT"), cloudClock());
  glUniform1f(U(p, "uEnvFog"), thumbMode ? 0.0f : envFogSigma()); glUniform1f(U(p, "uEnvFogH"), envFogH());   /* W1 Dawn ground fog: the preset's layer */   /* FG1 */ glUniform1f(U(p, "uEnvFogNear"), thumbMode ? 0.0f : lt.mapFogNear); glUniform1f(U(p, "uFogIndoor"), camIndoor ? 0.15f : 1.0f);
  glUniform4fv(U(p, "uFog2"), 1, glm::value_ptr(thumbMode || q.legacy || !perf.envFog ? vec4(0.0f) : lt.fog2));
  glUniform1f(U(p, "uEnvFogTop"), lt.mapFogLayerTop); glUniform1f(U(p, "uEnvFogSoft"), lt.mapFogLayerSoft);
  glUniform1i(U(p, "uFogField"), 27); glBindTextureUnit(27, fogTex);
  if (U(p, "uGATex") >= 0) {   // the grass programs: 27 is their plant atlas (GT1, a 2D array): the fog field moves to a unit none of their active samplers uses
    static std::unordered_map<GLuint, std::pair<int, int>> freeUnit;   // unit, calls seen: re-scanned for the first 3 calls (later setLighting lines set more sampler units)
    auto it = freeUnit.find(p);
    if (it == freeUnit.end() || it->second.second < 3) {
      bool used[32] = {}; GLint n = 0; glGetProgramiv(p, GL_ACTIVE_UNIFORMS, &n);
      for (GLint i = 0; i < n; ++i) {
        char nm[128]; GLint sz = 0; GLenum ty = 0; glGetActiveUniform(p, (GLuint)i, sizeof nm, nullptr, &sz, &ty, nm);
        const bool smp = (ty >= GL_SAMPLER_1D && ty <= GL_SAMPLER_2D_SHADOW) || (ty >= GL_SAMPLER_1D_ARRAY && ty <= GL_SAMPLER_CUBE_SHADOW) || ty == GL_SAMPLER_2D_RECT || ty == GL_SAMPLER_CUBE_MAP_ARRAY || ty == GL_SAMPLER_2D_MULTISAMPLE || ty == GL_SAMPLER_2D_MULTISAMPLE_ARRAY || ty == GL_SAMPLER_BUFFER;
        if (!smp || std::string(nm) == "uFogField") continue;
        GLint loc = glGetUniformLocation(p, nm); if (loc < 0) continue;
        for (GLint k = 0; k < std::max(sz, 1); ++k) { GLint u = 0; glGetUniformiv(p, loc + k, &u); if (u >= 0 && u < 32) used[u] = true; }
      }
      int f = -1; for (int u : {9, 13, 6, 1, 0}) if (f < 0 && !used[u]) f = u;   // only units setLighting rebinds every call (20-31 hold the texture library, bound once)
      if (f < 0) logf("grass: no free texture unit for the fog field (env fog off in the grass)");
      int seen = it == freeUnit.end() ? 1 : it->second.second + 1; freeUnit[p] = {f, seen}; it = freeUnit.find(p); if (seen == 3) logf("grass: fog field on texture unit %d for program %u (atlas on 27)", f, p);
    }
    if (it->second.first >= 0) { glUniform1i(U(p, "uFogField"), it->second.first); glBindTextureUnit(it->second.first, fogTex); }
    else glUniform1f(U(p, "uEnvFog"), 0.0f);
    extern GLuint grassAtlasTex(); glBindTextureUnit(27, grassAtlasTex());
  } glUniform4fv(U(p, "uFogBox"), 1, glm::value_ptr(fogBox));
  glUniform1f(U(p, "uCloudK"), thumbMode ? 0.0f : wx.k); glUniform1f(U(p, "uCloudSeed"), wx.seed); { vec2 cw = wx.wind * wx.der.windK; glUniform2fv(U(p, "uCloudWind"), 1, glm::value_ptr(cw)); }   // W1: the deck drifts with the preset's wind
  glUniform1i(U(p, "uCloudShadowMap"), 13); glBindTextureUnit(13, cloudShTex);
  glUniform3fv(U(p, "uCloudShadowBox"), 1, glm::value_ptr(cloudShBox));
  glUniform1f(U(p, "uAoIndirectOnly"), q.legacy ? 0.0f : 1.0f);
  glUniform1i(U(p, "uLook"), q.legacy ? 0 : 1);
  glUniform1i(U(p, "uSkyVisWall"), perf.skyVisWall && !q.legacy ? 1 : 0);
  glUniform1i(U(p, "uFroxWorld"), froxOn && p != progComposite && p != progSsrHalf ? 1 : 0);
  glUniform1i(U(p, "uFogPhase"), perf.fogPhase && q.lightNew && !q.legacy && !thumbMode ? 1 : 0);
  glUniform1i(U(p, "uW2"), perf.w2Sky && q.lightNew && !q.legacy && !thumbMode ? 1 : 0); glUniform1f(U(p, "uFlash"), thumbMode ? 0.0f : wx.der.flash);   // W2
  glUniform1i(U(p, "uDbgView"), dbgView);   // CP0
  { vec2 sa(0.25f, 0.18f); if (const char* e = std::getenv("NMK_SPECAA")) std::sscanf(e, "%f,%f", &sa.x, &sa.y); glUniform2f(U(p, "uSpecAA"), q.legacy || !perf.specAA ? 0.0f : sa.x, sa.y); }   // M3: specaa (--rq-off specaa); NMK_SPECAA="variance,clamp"
  { float hd = 1.0f; if (const char* e = std::getenv("NMK_HEX")) hd = (float)std::atof(e); glUniform1f(U(p, "uHBlend"), q.legacy || !perf.hblend || q.matLayer == 0 ? 0.0f : (std::getenv("NMK_HBLEND") ? (float)std::atof(std::getenv("NMK_HBLEND")) : 1.5f));   // M2: height blending (--rq-off hblend; off at Low); NMK_HBLEND = strength
  {   // UL1 material knobs (q.tex*, q.hexFade, q.hblendFade, q.pom*, q.decalRange); NMK_TEX_NEAR="a,b" still overrides
    vec2 tn = q.texNear; if (const char* e = std::getenv("NMK_TEX_NEAR")) std::sscanf(e, "%f,%f", &tn.x, &tn.y);
    glUniform2f(U(p, "uTexNear"), tn.x, std::max(tn.y, tn.x + 0.1f));
    static const bool envOpt2Off = std::getenv("NMK_OPT2") && std::string(std::getenv("NMK_OPT2")) == "0";   // OPT2 A/B (perf switch groundtaps)
    const bool opt2Off = envOpt2Off || !perf.groundTaps;
    glUniform1i(U(p, "uOpt2Off"), opt2Off ? 1 : 0);
    glUniform2f(U(p, "uHexFade"), opt2Off ? 0.0f : q.hexFade.x, opt2Off ? 0.0f : q.hexFade.y); glUniform2f(U(p, "uHBFade"), q.hblendFade.x, q.hblendFade.y);
    glUniform2f(U(p, "uPomQ"), q.pomRange, (float)q.pomSteps); glUniform1f(U(p, "uSDecR"), q.decalRange);
    const float an = perf.texBias ? std::max(q.texAniso, 16.0f) : q.texAniso;   // CP step 1: 16x with texbias
    static float aniso = -1.0f; if (an != aniso) { aniso = an; tex::setAnisotropy(aniso); }
    float tb = (perf.taau && upscaleMode == 0 ? std::log2(std::max(resScale, 0.25f)) : 0.0f) + (q.taa ? -0.5f : 0.0f);   // temporal upscaling only (mode 1 is a spatial blit: no history resolves a negative bias)   // texbias: follows the render scale (dynamic resolution)
    if (const char* e = std::getenv("NMK_TEX_BIAS")) tb = (float)std::atof(e);
    glUniform1i(U(p, "uTexBiasOn"), perf.texBias && !thumbMode ? 1 : 0); glUniform1f(U(p, "uTexBias"), tb);
  }
  glUniform1i(U(p, "uSplitSum"), q.legacy || !perf.splitSum ? 0 : 1);   // CP step 3
  glUniform1i(U(p, "uAlbClamp"), q.legacy || !perf.albClamp ? 0 : 1);   // CP step 4
  glUniform1i(U(p, "uWetOn"), q.legacy || !perf.wet || thumbMode ? 0 : 1); glUniform1f(U(p, "uWetTime"), (float)std::fmod(time, 3600.0));   // W3   // CP step 3
  glUniform1i(U(p, "uMaskLib"), !q.legacy && perf.maskLib ? 1 : 0);   // CP step 6 part 2
  glUniform1i(U(p, "uPudRelief"), !q.legacy && perf.pudRelief ? 1 : 0);   // FIX7
  glUniform1i(U(p, "uMetalMask"), !q.legacy && perf.metalMask ? 1 : 0);   // FIX6
  glUniform1i(U(p, "uAbsFar"), !q.legacy && perf.absFar ? 1 : 0);   // FIX8
  glUniform1i(U(p, "uSteepTri"), !q.legacy && perf.steepTri ? 1 : 0);   // FIX5
  glUniform1i(U(p, "uWearCause"), !q.legacy && q.matLayer > 0 && perf.wearCause ? 1 : 0);   // M4
  glUniform2i(U(p, "uMicroOn"), !q.legacy && q.matLayer > 0 && perf.microDetail ? 1 : 0, !q.legacy && perf.roughVar ? 1 : 0);   // CP step 6 (micro detail off at Low)
  glUniform2f(U(p, "uHex"), q.legacy || !perf.hex || q.matLayer == 0 || hd <= 0 ? 0.0f : 1.0f, std::max(hd, 0.05f)); }   // M1: hex tiling (--rq-off hex; off at Low); NMK_HEX = cells per texture repeat
  glUniform1f(U(p, "uHazeH"), wx.der.phys && wx.der.visM < 3000.0f ? std::max(lt.mapHazeH > 0.0f ? lt.mapHazeH : q.hazeHeight, 600.0f) : lt.mapHazeH > 0.0f ? lt.mapHazeH : q.hazeHeight);   // W2: a storm's air fills the valley, not a thin ground layer   // FG1: a map's own haze scale height
  glUniform1f(U(p, "uTime"), time);
  lightingUniforms(*this, p);   // L2: local lights, light shadows, probe GI, sky extras
  if (U(p, "uLeaves") >= 0 && U(p, "uTSV") >= 0) {   // the canopy colour programs: 26 is their leaf array (L1, a 2D array); the horizon map (uTSV, 2D) set to 26
    // above would put two sampler types on one unit (invalid GL: the leaves' alpha read 0, every canopy fragment was cut
    // while the depth prepass, without uTSV, kept them: white canopy shapes). The horizon map moves to a unit no active
    // sampler of this program uses, as the fog field does for the grass atlas above.
    static std::unordered_map<GLuint, std::pair<int, int>> tsvUnit;   // unit, calls seen (re-scanned for the first 3 calls)
    auto it = tsvUnit.find(p);
    if (it == tsvUnit.end() || it->second.second < 3) {
      bool used[32] = {}; GLint n = 0; glGetProgramiv(p, GL_ACTIVE_UNIFORMS, &n);
      for (GLint i = 0; i < n; ++i) {
        char nm[128]; GLint sz = 0; GLenum ty = 0; glGetActiveUniform(p, (GLuint)i, sizeof nm, nullptr, &sz, &ty, nm);
        const bool smp = (ty >= GL_SAMPLER_1D && ty <= GL_SAMPLER_2D_SHADOW) || (ty >= GL_SAMPLER_1D_ARRAY && ty <= GL_SAMPLER_CUBE_SHADOW) || ty == GL_SAMPLER_2D_RECT || ty == GL_SAMPLER_CUBE_MAP_ARRAY || ty == GL_SAMPLER_2D_MULTISAMPLE || ty == GL_SAMPLER_2D_MULTISAMPLE_ARRAY || ty == GL_SAMPLER_BUFFER;
        if (!smp || std::string(nm) == "uTSV") continue;
        GLint loc = glGetUniformLocation(p, nm); if (loc < 0) continue;
        for (GLint k = 0; k < std::max(sz, 1); ++k) { GLint u = 0; glGetUniformiv(p, loc + k, &u); if (u >= 0 && u < 32) used[u] = true; }
      }
      int f = -1; for (int u : {9, 13, 6, 1, 0}) if (f < 0 && !used[u]) f = u;   // units setLighting rebinds every call
      if (f < 0) logf("leaves: no free texture unit for the horizon map (terrain horizon off in the canopies)");
      int seen = it == tsvUnit.end() ? 1 : it->second.second + 1; tsvUnit[p] = {f, seen}; it = tsvUnit.find(p); if (seen == 3) logf("leaves: horizon map on texture unit %d for program %u (leaf array on 26)", f, p);
    }
    if (it->second.first >= 0) { glUniform1i(U(p, "uTSV"), it->second.first); if (thorTex && !thumbMode) glBindTextureUnit(it->second.first, thorTex); }
    else glUniform1i(U(p, "uTHorOn"), 0);
    extern GLuint leafLibTex(); glBindTextureUnit(kLeafUnit, leafLibTex());
  }
  glUniform1f(U(p, "uMossBias"), lt.mapMossBias);
  glUniform4f(U(p, "uMoss"), perf.mossField && !thumbMode && q.mossTier >= 1 ? 1.0f : 0.0f, q.mossTier >= 2 ? 1.0f : 0.0f, q.mossMatM, 0.5f);   // BF B1 (8.2: no moss detail below Low, transmission from Medium)
  sunDir = sunDirMap;   // the thumbnail studio sun never leaks back into the map
}

Renderer::DrawLocs Renderer::drawLocs(GLuint p) {
  for (auto& [k, l] : locCache) if (k == p) return l;
  DrawLocs l;
  l.model = U(p, "uModel"); l.tint = U(p, "uTint"); l.inst = U(p, "uInst"); l.snapAp = U(p, "uSnapAp"); l.glow = U(p, "uGlow");   // snapAp: V6 snap-chain texture origin
  l.roomA = U(p, "uRoomA"); l.roomB = U(p, "uRoomB"); l.sills = U(p, "uSills"); l.sillA = U(p, "uSillA"); l.sillB = U(p, "uSillB");
  l.contactBox = U(p, "uContactBox"); l.contact = U(p, "uContact"); l.foundWx = U(p, "uFoundWx"); l.mossHab = U(p, "uMossHab");   // MA2
  l.roomDec = U(p, "uRoomDec"); l.extDec = U(p, "uExtDec"); l.roomX = U(p, "uRoomX");   // GX5 static decal ranges, rooms past 16
  l.vp = U(p, "uVP"); l.time = U(p, "uTime"); l.wind = U(p, "uWind"); l.leafCol = U(p, "uLeafCol");
  l.base = U(p, "uBase"); l.metal = U(p, "uMetal"); l.rough = U(p, "uRough"); l.thin = U(p, "uThin");
  l.alb = U(p, "uAlb"); l.mr = U(p, "uMR"); l.nrm = U(p, "uNrm"); l.ao = U(p, "uAO");
  l.hasAlb = U(p, "uHasAlb"); l.hasMR = U(p, "uHasMR"); l.hasNrm = U(p, "uHasNrm"); l.hasAO = U(p, "uHasAO"); l.aoInMR = U(p, "uAOinMR"); l.aoStr = U(p, "uAOWeak");
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
  for (uint32_t k = itemLo; k < (uint32_t)std::min<size_t>(items.size(), itemHi); ++k) {   // GP1: a layer of the list, or all of it
    const DrawItem& it = items[k];
    if (!it.mesh || !it.mesh->count) continue;
    if (fr && !fr->visible(it.mesh->bmin, it.mesh->bmax, it.model, kCullEps)) { stats.culled++; continue; }
    if (shadowPass && minCaster > 0 && worldDiag(it.mesh->bmin, it.mesh->bmax, it.model) < minCaster) { stats.culled++; continue; }
    if (shadowPass && castCull && it.castR > 0) {   // R2 castercull: a small prop's sun shadow only near the eye
      vec3 c = vec3(it.model * vec4((it.mesh->bmin + it.mesh->bmax) * 0.5f, 1.0f)) - castEye;
      if (glm::dot(c, c) > it.castR * it.castR) { stats.casterCulled++; continue; }
    }
    if (shadowPass && farPass && it.noFarCast) { stats.casterCulled++; continue; }   // P2 furndist
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
    // CV1: a large item without finish parameters (cave roof lids, boulders: > 4 m across) is a run of its own, so
    // overlapping rock sorts front to back too; small ones keep the shared run (their coplanar ties stay put)
    auto big = [&](uint32_t k) { const DrawItem& it = items[k]; return !it.surf && glm::length(it.mesh->bmax - it.mesh->bmin) > 4.0f; };
    for (uint32_t a = 0; a < drawIdx.size();) {
      const SurfaceParams* s = surfOf(drawIdx[a]); uint32_t b = a; float d = 1e30f;
      if (big(drawIdx[a])) { const DrawItem& it = items[drawIdx[a]]; runs.push_back({glm::distance(vec3(it.model * vec4(0.5f * (it.mesh->bmin + it.mesh->bmax), 1.0f)), sortEye), a, a + 1}); a = a + 1; continue; }
      for (; b < drawIdx.size() && surfOf(drawIdx[b]) == s && !big(drawIdx[b]); ++b) {
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
  const vec4* lastContact = reinterpret_cast<const vec4*>(1);   // MA2: forces the first upload
  GLuint boundVao = 0;
  // P2 instdepth: a depth-only pass (progShadow: sun cascades, sky occlusion, light shadows) draws every mesh that occurs
  // more than once as one instanced draw (matrices streamed to instBuf); single meshes keep the per-item draw below
  const bool vpKnown = depthVPSet; depthVPSet = false;   // set by the caller right before this call (sun cascades, sky occlusion); others: per-item draws
  if (shadowPass && p == progShadow && progShadowI && perf.instDepth && vpKnown && drawIdx.size() > 1) {
    std::stable_sort(drawIdx.begin(), drawIdx.end(), [&](uint32_t a, uint32_t b) { return std::less<const GpuMesh*>()(items[a].mesh, items[b].mesh); });
    instMats.clear(); instRuns.clear();
    std::vector<uint32_t> single; single.reserve(drawIdx.size());
    for (size_t a = 0; a < drawIdx.size();) {
      size_t b = a; while (b < drawIdx.size() && items[drawIdx[b]].mesh == items[drawIdx[a]].mesh) ++b;
      if (b - a < 2 || !items[drawIdx[a]].mesh->vbo || !items[drawIdx[a]].mesh->ebo) for (size_t k = a; k < b; ++k) single.push_back(drawIdx[k]);   // meshes without their own buffers: per item
      else { instRuns.push_back({items[drawIdx[a]].mesh, (uint32_t)instMats.size(), (uint32_t)(b - a)}); for (size_t k = a; k < b; ++k) instMats.push_back(items[drawIdx[k]].model); }
      a = b;
    }
    if (!instRuns.empty()) {
      if (!instBuf) glCreateBuffers(1, &instBuf);
      glNamedBufferData(instBuf, instMats.size() * sizeof(mat4), instMats.data(), GL_STREAM_DRAW);   // orphaned per pass
      glUseProgram(progShadowI);
      glUniformMatrix4fv(drawLocs(progShadowI).vp, 1, GL_FALSE, glm::value_ptr(depthVP));
      for (auto& r : instRuns) {
        // the mesh's own instancing VAO (GpuMesh::ivao, freed with the mesh, so a rebuilt damaged bay never inherits a
        // stale setup): position from its vertex buffer, attributes 12-15 = one matrix per instance from instBuf
        GLuint& iv = const_cast<GpuMesh*>(r.mesh)->ivao;
        if (!iv) {
          glCreateVertexArrays(1, &iv);
          glVertexArrayVertexBuffer(iv, 0, r.mesh->vbo, 0, sizeof(GpuVertex)); glVertexArrayElementBuffer(iv, r.mesh->ebo);
          glEnableVertexArrayAttrib(iv, 0); glVertexArrayAttribFormat(iv, 0, 3, GL_FLOAT, GL_FALSE, (GLuint)offsetof(GpuVertex, pos)); glVertexArrayAttribBinding(iv, 0, 0);
          for (int c = 0; c < 4; ++c) { glEnableVertexArrayAttrib(iv, 12 + c); glVertexArrayAttribFormat(iv, 12 + c, 4, GL_FLOAT, GL_FALSE, c * 16); glVertexArrayAttribBinding(iv, 12 + c, 1); }
          glVertexArrayBindingDivisor(iv, 1, 1);
          glVertexArrayVertexBuffer(iv, 1, instBuf, 0, sizeof(mat4));
        }
        glBindVertexArray(iv);
        glDrawElementsInstancedBaseVertexBaseInstance(GL_TRIANGLES, r.mesh->count, GL_UNSIGNED_INT, nullptr, (GLsizei)r.count, 0, r.first);   // the call the leaf path (and the Mac shim) already uses
        stats.draws++; stats.instDrawn += r.count; stats.tris += r.mesh->count / 3 * r.count;
      }
      glUseProgram(p);
    }
    drawIdx.swap(single);
  }
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
      if (L.foundWx >= 0 && it.contact != lastContact) {   // MA2: the copy's ground-contact grid, when it changes
        lastContact = it.contact;
        bool on = it.contact && perf.foundWx && !q.legacy;
        glUniform1i(L.foundWx, on ? 1 : 0);
        if (on) { glUniform4fv(L.contactBox, 1, glm::value_ptr(it.contact[0])); glUniform4fv(L.contact, 4, glm::value_ptr(it.contact[1])); if (L.mossHab >= 0) glUniform4fv(L.mossHab, 1, glm::value_ptr(it.contact[5])); }
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
// DR1 farleaf: far canopy tiers. Leaf LOD 3 / 4 / 5 (selectLeafLods: from 80 / 180 / 400 m) keeps 40 / 15 / 6 % of every
// leaf group's cards and grows them 1.5 / 2.4 / 3.5 x (uCardScale), so the leaf area stays about the same. The kept
// cards are a prefix of the group, which foliage.cpp shuffles once at load: the subsets are nested, so a tree never
// swaps one random set for another when its tier changes.
static const float kLeafFarKeep[3] = {0.40f, 0.15f, 0.06f}, kLeafFarScale[3] = {1.5f, 2.4f, 3.5f};
static int leafFarCount(int n, int tier) { return std::max(1, (int)(n * kLeafFarKeep[tier] + 0.5f)); }

// P2 folbatch: per-frame cell membership; a cell whose member set changed (a tree felled, placed, moved, in or out of
// the draw distance) is rebuilt, at most 8 a frame; until then its trees draw one by one. DR1: a cell holds only the
// two farthest tiers (15 % + 6 % of the cards, in world space); nearer trees draw from their asset's own buffer.
void Renderer::prepFoliageBatches() {
  folBatchBytes = folBatchTrees = 0; folBatchCells = folCells.size();
  for (auto& [k, c] : folCells) { c.sig = 0; c.want.clear(); folBatchBytes += c.bytes; folBatchTrees += c.ids.size(); }
  for (auto& f : foliage) f.batched = false;
  if (!perf.folBatch || !perf.farLeaf || !perf.leafTex || !leafLibReady() || thumbMode) return;
  auto mix = [](uint64_t h, uint64_t v) { h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2); return h * 0x100000001B3ull; };
  for (int k = 0; k < (int)foliage.size(); ++k) {
    const FoliageDraw& f = foliage[k];
    if (!f.id || !f.tree || !f.tree->leafVao || f.tree->leafInstCpu.empty() || f.tree->leafGroups.empty()) continue;
    int64_t cx = (int64_t)std::floor(f.m[3].x / 64.0f), cz = (int64_t)std::floor(f.m[3].z / 64.0f);
    FolCell& c = folCells[mix(mix((uint64_t)(uintptr_t)f.tree, (uint64_t)cx), (uint64_t)cz)];
    uint32_t pb[5]; const float pf[5] = {f.m[3].x, f.m[3].y, f.m[3].z, f.m[0].x, f.m[0].z}; std::memcpy(pb, pf, sizeof pb);   // a moved / turned tree rebuilds its cell
    c.tree = f.tree; c.sig = mix(c.sig, f.id * 0x9E3779B97F4A7C15ull);
    c.sig = mix(mix(mix(c.sig, ((uint64_t)pb[0] << 32) | pb[1]), ((uint64_t)pb[2] << 32) | pb[3]), ((uint64_t)pb[4] << 32) | f.tree->leafInst);
    c.want.push_back(k);
  }
  int rebuilt = 0;
  for (auto it = folCells.begin(); it != folCells.end();) {
    FolCell& c = it->second;
    if (c.want.empty()) { if (c.vao) glDeleteVertexArrays(1, &c.vao); if (c.vbo) glDeleteBuffers(1, &c.vbo); it = folCells.erase(it); continue; }
    if (c.sig != c.builtSig) {
      if (rebuilt >= 8) { c.fol.clear(); ++it; continue; }
      ++rebuilt;
      std::vector<int> ord = c.want;   // neighbours next to each other: runs of visible trees stay long
      std::sort(ord.begin(), ord.end(), [&](int a, int b) { const vec4 &p = foliage[a].m[3], &q = foliage[b].m[3]; return p.x != q.x ? p.x < q.x : p.z < q.z; });
      const TreeDef& td = *c.tree;
      const size_t T = ord.size(), G = td.leafGroups.size();
      c.ids.resize(T); c.slot.clear(); for (size_t t = 0; t < T; ++t) { c.ids[t] = foliage[ord[t]].id; c.slot[c.ids[t]] = (int)t; }
      c.farR[0].assign(G * T, glm::ivec2(0)); c.farR[1].assign(G * T, glm::ivec2(0));
      std::vector<float> buf;
      auto put = [&](const TreeDef::LeafGroup& g, int count, const mat4& M, std::vector<glm::ivec2>& rng, size_t at) {
        glm::mat3 R(M);
        rng[at] = glm::ivec2((int)(buf.size() / 16), count);
        for (int n = 0; n < count; ++n) {
          const float* s = &td.leafInstCpu[(size_t)(g.first + n) * 16];
          vec3 ctr = vec3(M * vec4(s[0], s[1], s[2], 1)), T3 = R * vec3(s[3], s[4], s[5]), B3 = R * vec3(s[6], s[7], s[8]), D = glm::normalize(R * vec3(s[9], s[10], s[11]));
          buf.insert(buf.end(), {ctr.x, ctr.y, ctr.z, T3.x, T3.y, T3.z, B3.x, B3.y, B3.z, D.x, D.y, D.z, s[12], s[13], s[14], s[15]});
        }
      };
      for (int k = 0; k < 2; ++k)   // leaf LOD 4 and 5
        for (size_t g = 0; g < G; ++g) for (size_t t = 0; t < T; ++t) put(td.leafGroups[g], leafFarCount(td.leafGroups[g].count, k + 1), foliage[ord[t]].m, c.farR[k], g * T + t);
      if (c.vao) glDeleteVertexArrays(1, &c.vao); if (c.vbo) glDeleteBuffers(1, &c.vbo);
      glCreateBuffers(1, &c.vbo); glNamedBufferStorage(c.vbo, std::max<size_t>(buf.size(), 16) * 4, buf.empty() ? nullptr : buf.data(), 0);
      c.bytes = buf.size() * 4;
      c.vao = leafInstVao(c.vbo);
      c.builtSig = c.sig;
    }
    c.fol.assign(c.ids.size(), -1);
    for (int k : c.want) { auto s = c.slot.find(foliage[k].id); if (s != c.slot.end()) { c.fol[s->second] = k; foliage[k].batched = true; } }
    ++it;
  }
}

void Renderer::drawFoliage(GLuint p, const mat4& vp, const Frustum* fr, bool view, float lodFrom) {
  if (lt.mapLeafless) return;   // BT1: look.leafless: the deciduous canopies (leaf cards) are bare in winter; the model conifers keep theirs
  float sway = 0.14f * windSwayAmp() * (1.0f + windGustG()) + kCullEps;   // kLeafVS leafSway: |offset| <= 0.10 x 1.3 x |(0.8, 0.25, 0.6)| x uWind
  bool tex = perf.leafTex && leafLibReady();
  // unit 26 is also the terrain's horizon-shadow (setLighting) and sky-view map: bind the leaf array here, or the cards
  // sample the wrong texture, read alpha 0 and are cut away (with the canopy prepass: depth without colour, drawn white)
  { extern unsigned leafLibTex(); if (leafLibTex()) glBindTextureUnit(kLeafUnit, leafLibTex()); }
  for (int pass = 0; pass < 2; ++pass) {
    auto mine = [&](const FoliageDraw& f) { bool inst = tex && f.tree && f.tree->leafVao; return f.tree && inst == (pass == 1) && (inst || f.tree->cardVao); };
    if (std::none_of(foliage.begin(), foliage.end(), mine)) continue;
    GLuint q = pass == 0 ? p : p == progLeaf ? progLeafI : progLeafIShadow;
    glUseProgram(q);
    const DrawLocs L = drawLocs(q);
    glUniformMatrix4fv(L.vp, 1, GL_FALSE, glm::value_ptr(vp));
    glUniform1f(L.time, time); glUniform1f(L.wind, wind);
    GLint lm = L.model, lc = L.leafCol;
    GLint lcs = pass == 1 ? U(q, "uCardScale") : -1; float curScale = -1.0f;
    auto setScale = [&](float sc) { if (lcs >= 0 && sc != curScale) { curScale = sc; glUniform1f(lcs, sc); } };
    setScale(1.0f);
    // the canopy tier of a tree in this pass: its leaf LOD in the main view; in the far cascade and the sky-occlusion map
    // (lodFrom == 0) at least the first far tier; the sprig cards in the near cascade
    auto tierOf = [&](const FoliageDraw& f) -> int { return view ? f.cur : perf.farLeaf && lodFrom == 0.0f ? std::max<int>(f.cur, 3) : 2; };
    for (auto& f : foliage) {
      if (!mine(f)) continue;
      const int tier = pass == 1 ? tierOf(f) : 2;
      if (pass == 1 && tier >= 4 && (f.batched || !perf.treeFar)) continue;   // drawn from its cell below (treefar off, measurement: not at all)
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
        stats.draws++; stats.foliageDraws++; stats.tris += n / 3;
        continue;
      }
      const int lod = std::min(tier, 2); const bool farT = tier >= 3;
      setScale(farT ? kLeafFarScale[tier - 3] : 1.0f);
      glBindVertexArray(f.tree->leafVao);
      for (auto& g : !farT && slod && !f.tree->leafShadowGroups.empty() ? f.tree->leafShadowGroups : f.tree->leafGroups) {
        const LeafMeshRange& r = gLeafSprigMesh[g.sprig * 3 + lod];
        const int n = farT ? leafFarCount(g.count, tier - 3) : g.count;   // a far tier: the first share of the (shuffled) group
        if (lc >= 0) glUniform3fv(lc, 1, glm::value_ptr(g.tint));
        glDrawElementsInstancedBaseVertexBaseInstance(GL_TRIANGLES, r.count, GL_UNSIGNED_INT, (void*)(uintptr_t)(r.firstIndex * 4u), n, r.baseVertex, g.first);
        stats.draws++; stats.foliageDraws++; stats.tris += (size_t)r.count / 3 * n;
      }
    }
    if (pass == 1 && !folCells.empty() && perf.treeFar) {   // P2 folbatch: each cell's visible far-tier trees, per leaf group, in runs of one tier
      const mat4 I(1); glUniformMatrix4fv(lm, 1, GL_FALSE, glm::value_ptr(I));
      std::vector<int> st;
      for (auto& [key, c] : folCells) {
        if (c.fol.empty() || !c.vao) continue;
        const TreeDef& td = *c.tree; const size_t T = c.ids.size(), G = td.leafGroups.size();
        st.assign(T, -1); bool any = false;
        for (size_t t = 0; t < T; ++t) {
          int k = c.fol[t];
          if (k < 0 || k >= (int)foliage.size() || !foliage[k].batched || foliage[k].id != c.ids[t] || foliage[k].tree != c.tree) continue;   // only this frame's list (thumbnails swap it)
          const FoliageDraw& f = foliage[k];
          if (fr && !fr->visible(td.cardMin, td.cardMax, f.m, sway + 0.05f)) { stats.culled++; continue; }
          const int tier = tierOf(f);
          if (tier < 4) continue;   // nearer tiers drew above, from the asset's buffer
          st[t] = tier; any = true;
        }
        if (!any) continue;
        glBindVertexArray(c.vao);
        for (size_t g = 0; g < G; ++g) {
          if (lc >= 0) glUniform3fv(lc, 1, glm::value_ptr(td.leafGroups[g].tint));
          for (size_t t = 0; t < T;) {
            if (st[t] < 0) { ++t; continue; }
            const int s = st[t]; const std::vector<glm::ivec2>& rg = c.farR[s - 4];
            setScale(kLeafFarScale[s - 3]);
            int first = rg[g * T + t].x, n = 0;
            while (t < T && st[t] == s) { n += rg[g * T + t].y; ++t; }
            if (!n) continue;
            const LeafMeshRange& r = gLeafSprigMesh[td.leafGroups[g].sprig * 3 + 2];   // the sprig card
            glDrawElementsInstancedBaseVertexBaseInstance(GL_TRIANGLES, r.count, GL_UNSIGNED_INT, (void*)(uintptr_t)(r.firstIndex * 4u), n, r.baseVertex, first);
            stats.draws++; stats.foliageDraws++; stats.tris += (size_t)r.count / 3 * n;
          }
        }
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
    // levels 0 / 1 / 2 as before (near, mid, sprig cards); DR1 farleaf adds 3 / 4 / 5 from 80 / 180 / 400 m, each with hysteresis
    static const float in[5] = {kLeafNearIn, kLeafMidIn, 80.0f, 180.0f, 400.0f}, out[5] = {kLeafNearOut, kLeafMidOut, 92.0f, 205.0f, 450.0f};
    const int top = perf.farLeaf ? 5 : 2;
    int cur = std::min<int>(f.lod ? *f.lod : 2, top);
    while (cur < top && d > out[cur]) ++cur;
    while (cur > 0 && d < in[cur - 1]) --cur;
    f.cur = (uint8_t)cur; if (f.lod) *f.lod = (uint8_t)cur;
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

// NT8: model foliage. Instances are culled per model (bounding sphere), their matrices packed into one SSBO (binding 12),
// then each part is one instanced draw; cutout parts alpha test (depth too), no back-face culling.
// ---------------- CP3 octahedral impostors
// CP3c: the atlases end block-compressed (albedo BC3, octahedral normal BC5, about 6.7 MB a species at 2048): a bake draws
// RGBA8 (usable at once), reads its mips back and compresses them on a worker thread; the result replaces the RGBA8 pair
// and is written to the per-user texture cache (texbc), keyed by the bake's content (mesh.bin, model.json), the part
// range, the atlas size and the bake version, so a second load reads it instead of baking.
static fs::path impCacheFile(const GpuModel* m, int first, int count, int atlas) {
  if (!m || m->srcDir.empty()) return {};
  const std::string a = userCacheContentStamp(m->srcDir / "mesh.bin"), b = userCacheContentStamp(m->srcDir / "model.json");
  if (a.empty()) return {};
  std::string key = "imp1|" + a + "|" + b + "|" + std::to_string(first) + "|" + std::to_string(count) + "|" + std::to_string(atlas) + (m->turned ? "|t" : "");
  uint64_t x = 1469598103934665603ull; for (unsigned char c : key) { x ^= c; x *= 1099511628211ull; }
  char nm[40]; std::snprintf(nm, sizeof nm, "imp_%016llx.bc", (unsigned long long)x);
  return userCacheDir("texbc", "NMK_TEXBC_CACHE") / nm;
}
static GLuint impUpload(GLenum fmt, int w, const std::vector<std::vector<uint8_t>>& lv) {
  GLuint t; glCreateTextures(GL_TEXTURE_2D, 1, &t); glTextureStorage2D(t, (GLsizei)lv.size(), fmt, w, w);
  for (int L = 0, lw = w; L < (int)lv.size(); ++L, lw = std::max(1, lw / 2)) glCompressedTextureSubImage2D(t, L, 0, 0, lw, lw, fmt, (GLsizei)lv[L].size(), lv[L].data());
  glTextureParameteri(t, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); glTextureParameteri(t, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameteri(t, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(t, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return t;
}
// levels of RGBA8 (w x w, halving) -> BC3 (rg5: BC5 from r, g)
static std::vector<std::vector<uint8_t>> impCompress(const std::vector<std::vector<uint8_t>>& src, int w, bool rg5) {
  std::vector<std::vector<uint8_t>> out;
  for (int L = 0, lw = w; L < (int)src.size(); ++L, lw = std::max(1, lw / 2)) {
    const int bw = std::max(1, (lw + 3) / 4); std::vector<uint8_t> o((size_t)bw * bw * 16);
    for (int by = 0; by < bw; ++by) for (int bx = 0; bx < bw; ++bx) {
      uint8_t blk[64], rg[32];
      for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
        const int sx = std::min(lw - 1, bx * 4 + x), sy = std::min(lw - 1, by * 4 + y); const uint8_t* p = &src[L][((size_t)sy * lw + sx) * 4];
        std::memcpy(blk + (y * 4 + x) * 4, p, 4); rg[(y * 4 + x) * 2] = p[0]; rg[(y * 4 + x) * 2 + 1] = p[1];
      }
      uint8_t* d = &o[((size_t)by * bw + bx) * 16];
      if (rg5) stb_compress_bc5_block(d, rg); else stb_compress_dxt_block(d, blk, 1, STB_DXT_NORMAL);
    }
    out.push_back(std::move(o));
  }
  return out;
}
static void impWrite(const fs::path& f, const std::array<float, 6>& g, int atlas, const std::vector<std::vector<uint8_t>>& a, const std::vector<std::vector<uint8_t>>& n) {
  std::error_code ec; fs::create_directories(f.parent_path(), ec);
  fs::path tmp = f; tmp += "." + std::to_string((unsigned long long)std::chrono::steady_clock::now().time_since_epoch().count()) + ".tmp";
  { std::ofstream o(tmp, std::ios::binary); const uint32_t hd[4] = {0x31504D49u /* IMP1 */, (uint32_t)atlas, (uint32_t)a.size(), (uint32_t)n.size()};
    o.write((const char*)hd, sizeof hd); o.write((const char*)g.data(), sizeof(float) * 6);
    for (auto* v : {&a, &n}) for (auto& l : *v) { uint32_t s = (uint32_t)l.size(); o.write((const char*)&s, 4); o.write((const char*)l.data(), s); } }
  fs::rename(tmp, f, ec);
}
static bool impRead(const fs::path& f, Renderer::Impostor& I) {
  std::ifstream in(f, std::ios::binary); if (!in) return false;
  uint32_t hd[4]; float g[6];
  if (!in.read((char*)hd, sizeof hd) || hd[0] != 0x31504D49u || hd[1] < 256 || hd[1] > 4096 || hd[2] > 14 || hd[3] > 14 || !in.read((char*)g, sizeof g)) return false;
  std::vector<std::vector<uint8_t>> lv[2];
  for (int k = 0; k < 2; ++k) for (uint32_t L = 0; L < hd[2 + k]; ++L) { uint32_t s = 0; if (!in.read((char*)&s, 4) || s > (32u << 20)) return false; lv[k].emplace_back(s); if (!in.read((char*)lv[k].back().data(), s)) return false; }
  I.c = vec3(g[0], g[1], g[2]); I.r = g[3]; I.fpx = g[4]; I.use = g[5];
  I.alb = impUpload(0x83F3 /* GL_COMPRESSED_RGBA_S3TC_DXT5_EXT */, (int)hd[1], lv[0]); I.nrm = impUpload(GL_COMPRESSED_RG_RGTC2, (int)hd[1] / 2, lv[1]);
  I.ready = true; return true;
}
const Renderer::Impostor* Renderer::impostorFor(const GpuModel* m, int first, int count) {
  if (!progImp || !progImpBake || !m || !m->vao) return nullptr;
  auto key = std::make_tuple(m, first, count);
  auto it = impostors.find(key);
  if (it != impostors.end()) return it->second.ready ? &it->second : nullptr;
  if ((int)impostors.size() >= q.impMax) return nullptr;   // shortcut: the tier's species cap, first come; an LRU if maps need more
  Impostor& I = impostors[key];
  if (perf.impostorBC) { const fs::path f = impCacheFile(m, first, count, q.impAtlas); if (!f.empty() && impRead(f, I)) { userCacheTouch(f); logf("impostor: %p parts %d+%d from the cache", (const void*)m, first, count); return &I; } }
  impWant.push_back(key); return nullptr;
}

void Renderer::impostorPoll() {   // CP3c: swap finished compressions in (the RGBA8 pair is freed)
  for (auto& [k, I] : impostors) if (I.job.valid() && I.job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
    ImpBC r = I.job.get();
    if (r.w <= 0 || r.alb.empty() || r.nrm.empty()) { logf("impostor: compression failed (RGBA8 kept)"); continue; }
    GLuint a = impUpload(0x83F3 /* GL_COMPRESSED_RGBA_S3TC_DXT5_EXT */, r.w, r.alb), n = impUpload(GL_COMPRESSED_RG_RGTC2, r.w / 2, r.nrm);
    glDeleteTextures(1, &I.alb); glDeleteTextures(1, &I.nrm); I.alb = a; I.nrm = n;
  }
}

void Renderer::impostorBakeSome() {
  const int kAtlas = q.impAtlas, kN = 12, kF = kAtlas / kN, kNrm = kAtlas / 2;   // shortcut: a tier change keeps earlier bakes at their size
  GLint prevFbo = 0, vpOld[4], dfOld = GL_LESS; glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo); glGetIntegerv(GL_VIEWPORT, vpOld); glGetIntegerv(GL_DEPTH_FUNC, &dfOld);
  const GLboolean cullOld = glIsEnabled(GL_CULL_FACE), blendOld = glIsEnabled(GL_BLEND), dtOld = glIsEnabled(GL_DEPTH_TEST);
  if (!impFbo || impFboSize != kAtlas) {
    if (impFbo) { glDeleteFramebuffers(1, &impFbo); glDeleteRenderbuffers(1, &impDepth); glDeleteTextures(1, &impNrmTmp); } else glCreateBuffers(1, &impBakeBuf);
    glCreateFramebuffers(1, &impFbo); glCreateRenderbuffers(1, &impDepth); glNamedRenderbufferStorage(impDepth, GL_DEPTH_COMPONENT24, kAtlas, kAtlas);
    glCreateTextures(GL_TEXTURE_2D, 1, &impNrmTmp); glTextureStorage2D(impNrmTmp, 1, GL_RGBA8, kAtlas, kAtlas); impFboSize = kAtlas; }
  for (int done = 0; done < 2 && !impWant.empty(); ++done) {
    auto key = impWant.back(); impWant.pop_back();
    Impostor& I = impostors[key];
    const GpuModel* m = std::get<0>(key); const int p0 = std::get<2>(key) < 0 ? 0 : std::get<1>(key), p1 = std::get<2>(key) < 0 ? (int)m->parts.size() : std::min((int)m->parts.size(), std::get<1>(key) + std::get<2>(key));
    vec3 lo(FLT_MAX), hi(-FLT_MAX); for (int k = p0; k < p1; ++k) { lo = glm::min(lo, m->parts[k].bmin); hi = glm::max(hi, m->parts[k].bmax); }
    if (lo.x > hi.x) continue;
    I.c = 0.5f * (lo + hi); I.r = 0.5f * glm::length(hi - lo);
    int lv = 1; while ((kAtlas >> lv) > 0) ++lv;
    glCreateTextures(GL_TEXTURE_2D, 1, &I.alb); glTextureStorage2D(I.alb, lv, GL_RGBA8, kAtlas, kAtlas);
    glNamedFramebufferTexture(impFbo, GL_COLOR_ATTACHMENT0, I.alb, 0); glNamedFramebufferTexture(impFbo, GL_COLOR_ATTACHMENT1, impNrmTmp, 0);
    glNamedFramebufferRenderbuffer(impFbo, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, impDepth);
    const GLenum bufs[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1}; glNamedFramebufferDrawBuffers(impFbo, 2, bufs);
    if (glCheckNamedFramebufferStatus(impFbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { logf("impostor: framebuffer incomplete"); continue; }
    glBindFramebuffer(GL_FRAMEBUFFER, impFbo);
    glClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE); glClearDepth(1.0);
    const float z4[4] = {0, 0, 0, 0}, n4[4] = {0.5f, 0.5f, 1.0f, 1.0f};   // n4: octahedral (0, 0, 1) = 0.5, 0.5
    glViewport(0, 0, kAtlas, kAtlas); glDisable(GL_SCISSOR_TEST); glDepthMask(GL_TRUE);
    glClearNamedFramebufferfv(impFbo, GL_COLOR, 0, z4); glClearNamedFramebufferfv(impFbo, GL_COLOR, 1, n4); glClearNamedFramebufferfv(impFbo, GL_DEPTH, 0, n4 + 3);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
    glUseProgram(progImpBake);
    const vec4 inst[4] = {vec4(1, 0, 0, 0), vec4(0, 1, 0, 0), vec4(0, 0, 1, 0), vec4(0, 0, 0, 1)};   // identity, wind 0, no tint
    glNamedBufferData(impBakeBuf, sizeof inst, inst, GL_STREAM_DRAW); glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 12, impBakeBuf);
    const DrawLocs L = drawLocs(progImpBake);
    glUniform1i(U(progImpBake, "uInstBase"), 0); glUniform1f(L.time, 0.0f); glUniform1f(L.wind, 0.0f); glUniform1i(L.alb, 2);
    const GLint lCut = U(progImpBake, "uCutout"), lHas = U(progImpBake, "uHasAlb"), lBase = U(progImpBake, "uBase");
    glBindVertexArray(m->vao);
    for (int fy = 0; fy < kN; ++fy) for (int fx = 0; fx < kN; ++fx) {
      vec2 e = vec2((fx + 0.5f) / kN, (fy + 0.5f) / kN) * 2.0f - 1.0f; float x = (e.x - e.y) * 0.5f, z = (e.x + e.y) * 0.5f;
      vec3 d = glm::normalize(vec3(x, 1.0f - std::abs(x) - std::abs(z), z));   // kImpFS hoDec
      vec3 rF = glm::normalize(glm::cross(vec3(0, 1, 0), d)), uF = glm::cross(d, rF);
      mat4 VP = glm::ortho(-I.r, I.r, -I.r, I.r, 0.01f, 4.0f * I.r) * glm::lookAt(I.c + d * (2.0f * I.r), I.c, uF);
      glViewport(fx * kF, fy * kF, kF, kF);
      glUniformMatrix4fv(L.vp, 1, GL_FALSE, glm::value_ptr(VP));
      for (int pi = p0; pi < p1; ++pi) {
        const auto& pt = m->parts[pi];
        const int mi = pt.mat >= 0 && pt.mat < (int)m->mats.size() ? pt.mat : -1;
        const GLuint alb = mi >= 0 ? m->mats[mi].alb : 0;
        glBindTextureUnit(2, alb); glUniform1i(lHas, alb ? 1 : 0); glUniform1i(lCut, pt.cutout && alb ? 1 : 0); glUniform4fv(lBase, 1, glm::value_ptr(pt.base));
        glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)pt.count, GL_UNSIGNED_INT, (void*)(uintptr_t)(pt.first * 4u), 1);
      }
    }
    glGenerateTextureMipmap(I.alb);
    int lvN = 1; while ((kNrm >> lvN) > 0) ++lvN;   // normals at half resolution (lower frequency)
    glCreateTextures(GL_TEXTURE_2D, 1, &I.nrm); glTextureStorage2D(I.nrm, lvN, GL_RGBA8, kNrm, kNrm);
    GLuint rf = 0; glCreateFramebuffers(1, &rf); glNamedFramebufferTexture(rf, GL_COLOR_ATTACHMENT0, I.nrm, 0);
    glNamedFramebufferReadBuffer(impFbo, GL_COLOR_ATTACHMENT1);
    glBlitNamedFramebuffer(impFbo, rf, 0, 0, kAtlas, kAtlas, 0, 0, kNrm, kNrm, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glDeleteFramebuffers(1, &rf); glGenerateTextureMipmap(I.nrm);
    for (GLuint t : {I.alb, I.nrm}) { glTextureParameteri(t, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); glTextureParameteri(t, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                                       glTextureParameteri(t, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(t, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); }
    I.fpx = (float)kF; I.use = kN * (float)kF / kAtlas;
    I.ready = true; logf("impostor: baked %p parts %d-%d (r %.1f m, %d atlas)", (const void*)m, p0, p1, I.r, kAtlas);
    if (perf.impostorBC) {   // CP3c: read the RGBA8 mips back (one stall per species), compress and cache on a worker
      auto readLv = [](GLuint t, int w, int lv) { std::vector<std::vector<uint8_t>> o; for (int L = 0, lw = w; L < lv; ++L, lw = std::max(1, lw / 2)) {
        o.emplace_back((size_t)lw * lw * 4); glGetTextureImage(t, L, GL_RGBA, GL_UNSIGNED_BYTE, (GLsizei)o.back().size(), o.back().data()); } return o; };
      auto a = std::make_shared<std::vector<std::vector<uint8_t>>>(readLv(I.alb, kAtlas, lv)), n = std::make_shared<std::vector<std::vector<uint8_t>>>(readLv(I.nrm, kNrm, lvN));
      const fs::path cf = impCacheFile(m, std::get<1>(key), std::get<2>(key), kAtlas); const std::array<float, 6> head = {I.c.x, I.c.y, I.c.z, I.r, (float)kF, kN * (float)kF / kAtlas};
      try {   // under memory pressure: no worker (std::system_error) or a failed job (bad_alloc) keeps the RGBA8 pair, never a crash
        I.job = std::async(std::launch::async, [a, n, cf, head, kAtlas, kNrm]() noexcept {
          ImpBC r;
          try { r.alb = impCompress(*a, kAtlas, false); r.nrm = impCompress(*n, kNrm, true); if (!cf.empty()) impWrite(cf, head, kAtlas, r.alb, r.nrm); r.w = kAtlas; }
          catch (...) { r = ImpBC(); }
          return r; });
      } catch (...) { logf("impostor: no compression worker (RGBA8 kept)"); }
    }
  }
  glBindVertexArray(0); glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo); glViewport(vpOld[0], vpOld[1], vpOld[2], vpOld[3]);
  glDepthFunc((GLenum)dfOld); if (cullOld) glEnable(GL_CULL_FACE); if (blendOld) glEnable(GL_BLEND); if (!dtOld) glDisable(GL_DEPTH_TEST);
}

void Renderer::drawImpostors(const RenderView& v, const mat4& vp, const Frustum& fr) {
  std::map<const Impostor*, std::vector<vec4>> by;
  for (auto& d : impDraws) {
    if (!d.imp || !fr.sphere(d.c, d.r)) continue;
    auto& b = by[d.imp]; b.push_back(vec4(d.c, d.r)); b.push_back(vec4(d.yaw, d.dither, 0, 0)); b.push_back(d.tint);
  }
  if (by.empty()) return;
  setLighting(progImp, v);
  glUniformMatrix4fv(U(progImp, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
  glUniform1i(U(progImp, "uImpAlb"), 2); glUniform1i(U(progImp, "uImpNrm"), 4); const GLint lUse = U(progImp, "uImpUse");
  glUniform1f(U(progImp, "uFolTrans"), q.lightNew && perf.folTrans && !q.legacy ? 1.0f : 0.0f);   // as progFolModel (drawFolModels)
  if (!impInstBuf) glCreateBuffers(1, &impInstBuf);
  glDisable(GL_CULL_FACE); glBindVertexArray(snowfallVao);
  for (auto& [I, b] : by) {
    const size_t bytes = b.size() * sizeof(vec4);
    if (impInstBytes < bytes) { glDeleteBuffers(1, &impInstBuf); glCreateBuffers(1, &impInstBuf); impInstBytes = std::max(bytes, impInstBytes * 2); glNamedBufferData(impInstBuf, impInstBytes, nullptr, GL_STREAM_DRAW); }
    glNamedBufferSubData(impInstBuf, 0, bytes, b.data()); glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 13, impInstBuf);
    glBindTextureUnit(2, I->alb); glBindTextureUnit(4, I->nrm); glUniform1f(lUse, I->use);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, (GLsizei)(b.size() / 3));
    stats.draws++; stats.tris += (int)(b.size() / 3) * 2;
  }
  glBindVertexArray(0); glEnable(GL_CULL_FACE);
}

void Renderer::drawFolModels(bool depth, const mat4& vp, const Frustum* fr, float minH, bool shadowLod) {
  if (folModels.empty()) return;
  struct Key { const GpuModel* m; int first, count; float dith; bool operator<(const Key& o) const { return std::tie(m, first, count, dith) < std::tie(o.m, o.first, o.count, o.dith); } };
  std::map<Key, std::vector<const FolModelDraw*>> by;
  std::vector<FolModelDraw> swapped;   // CP5: the mid cascade's fixed far level
  if (shadowLod) { swapped = folModels; for (auto& d : swapped) if (d.shadowModel) { d.model = d.shadowModel; d.first = d.sFirst; d.count = d.sCount; } }
  for (auto& d : shadowLod ? swapped : folModels) {
    if (!d.model || !d.model->vao || (d.shadowOnly && (!depth || minH <= 0))) continue;   // CP3: a tree drawn as an impostor still casts
    vec3 lo = d.model->bmin, hi = d.model->bmax;
    if (d.count >= 0) {   // VR1: a range of parts (one ivy piece): its own bounds
      lo = vec3(FLT_MAX); hi = vec3(-FLT_MAX);
      for (int k = d.first; k < d.first + d.count && k < (int)d.model->parts.size(); ++k) { lo = glm::min(lo, d.model->parts[k].bmin); hi = glm::max(hi, d.model->parts[k].bmax); }
      if (lo.x > hi.x) continue;
    }
    if (minH > 0 && (hi.y - lo.y) * glm::length(vec3(d.m[1])) < minH && !(d.cast && minH < 2.0f)) continue;   // ferns / grass do not cast into this cascade
    if (fr) { vec3 c = vec3(d.m * vec4(0.5f * (lo + hi), 1)); float r = 0.5f * glm::length(vec3(d.m * vec4(hi - lo, 0)));
              if (!fr->sphere(c, r)) { stats.culled++; continue; } }
    by[{d.model, d.count >= 0 ? d.first : 0, d.count, d.dither}].push_back(&d);
  }
  std::vector<vec4> buf; std::vector<std::pair<Key, std::pair<int, int>>> runs;
  for (auto& [m, v] : by) {
    runs.push_back({m, {(int)(buf.size() / 4), (int)v.size()}});
    for (auto* d : v) {
      mat4 M = d->m; M[0].w = d->wind;
      if (d->tint.a > 0) { M[1].w = d->tint.r; M[2].w = d->tint.g; M[3].w = d->tint.a > 2.5f ? -2.0f - d->tint.b : (d->tint.a > 1.5f ? 6.0f : 2.0f) + d->tint.b; }   // kFolVS reads it back (a 1 = ivy recolour, 2 = multiply (HB1c), 3 = greenery target (FL1), sent negative)
      for (int k = 0; k < 4; ++k) buf.push_back(M[k]);
    }
  }
  if (buf.empty()) return;
  static const bool dbg = std::getenv("NMK_DBG_FOL") != nullptr; static int dbgN = 0;
  if (dbg && !depth && dbgN++ < 3) for (auto& [key, rn] : runs) if (const GpuModel* m = key.m) logf("fol: model %p parts %zu inst %d tris %d mats %zu alb0 %u cut0 %d", (const void*)m, m->parts.size(), rn.second, m->tris, m->mats.size(), m->mats.empty() ? 0u : m->mats[0].alb, m->parts.empty() ? -1 : (int)m->parts[0].cutout);
  size_t bytes = buf.size() * sizeof(vec4);
  if (!folBuf || folBufBytes < bytes) { if (folBuf) glDeleteBuffers(1, &folBuf); glCreateBuffers(1, &folBuf); folBufBytes = std::max(bytes, folBufBytes * 2); glNamedBufferData(folBuf, folBufBytes, nullptr, GL_STREAM_DRAW); }
  glNamedBufferSubData(folBuf, 0, bytes, buf.data());
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 12, folBuf);
  GLuint p = depth ? progFolDepth : progFolModel;
  glUseProgram(p);
  if (!depth) glUniform1f(U(p, "uFolTrans"), q.lightNew && perf.folTrans && !q.legacy ? 1.0f : 0.0f);   // VO L4
  glUniformMatrix4fv(U(p, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
  glUniform1f(U(p, "uTime"), time); glUniform1f(U(p, "uWind"), wind);
  const DrawLocs L = drawLocs(p);
  glUniform1i(L.alb, 2);
  if (!depth) { glUniform1i(L.mr, 3); glUniform1i(L.nrm, 4); glUniform1i(L.ao, 5); glUniform4f(L.tint, 1, 1, 1, 1); glUniform1f(L.thin, 0); glUniform1i(U(p, "uGlass"), 0); glUniform1f(U(p, "uAlbFloor"), 0);
                glUniform3fv(U(p, "uIvyStem"), 1, glm::value_ptr(ivyStem)); glUniform2fv(U(p, "uIvyRef"), 1, glm::value_ptr(ivyRef)); }
  GLint lCut = U(p, "uCutout"), lBase = U(p, "uInstBase"), lDith = U(p, "uDither");
  glDisable(GL_CULL_FACE);
  for (auto& [key, rn] : runs) {
    const GpuModel* m = key.m;
    glBindVertexArray(m->vao); glUniform1i(lBase, rn.first); glUniform1f(lDith, key.dith);
    int boundMat = -2;
    const int p0 = key.first, p1 = key.count < 0 ? (int)m->parts.size() : std::min((int)m->parts.size(), key.first + key.count);
    for (int pi = p0; pi < p1; ++pi) {
      const auto& pt = m->parts[pi];
      int mi = pt.mat >= 0 && pt.mat < (int)m->mats.size() ? pt.mat : -1;
      if (mi != boundMat) {
        boundMat = mi; GpuModel::MatTex none; const GpuModel::MatTex& t = mi >= 0 ? m->mats[mi] : none;
        glBindTextureUnit(2, t.alb);
        if (!depth) { glBindTextureUnit(3, t.mr); glBindTextureUnit(4, t.n); glBindTextureUnit(5, t.ao);
          glUniform1i(L.hasAlb, t.alb ? 1 : 0); glUniform1i(L.hasMR, t.mr ? 1 : 0); glUniform1i(L.hasNrm, t.n ? 1 : 0); glUniform1i(L.hasAO, t.ao ? 1 : 0); glUniform1i(L.aoInMR, t.aoInMR ? 1 : 0); }
      }
      glUniform1i(lCut, pt.cutout && (mi >= 0 && m->mats[mi].alb) ? 1 : 0);
      if (depth) glUniform4fv(L.base, 1, glm::value_ptr(pt.base));   // fol_depth's cut-out multiplies by the base alpha, as kModelFS
      if (!depth) { glUniform4fv(L.base, 1, glm::value_ptr(pt.base)); glUniform1f(L.metal, pt.metal); glUniform1f(L.rough, pt.rough); }
      glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)pt.count, GL_UNSIGNED_INT, (void*)(uintptr_t)(pt.first * 4u), rn.second);
      stats.draws++; stats.tris += (int)pt.count / 3 * rn.second;
    }
  }
  glEnable(GL_CULL_FACE);
}

void Renderer::drawModels(const std::vector<ModelDraw>& items, const Frustum* fr) {
  GLuint p = progModel;
  const DrawLocs L = drawLocs(p);
  glUniform1i(L.alb, 2); glUniform1i(L.mr, 3); glUniform1i(L.nrm, 4); glUniform1i(L.ao, 5);
  GLint lm = L.model, lb = L.base, lme = L.metal, lr = L.rough, lt = L.tint, lth = L.thin;
  static GLuint velProg = 0; static GLint lvel = -1, lpm = -1;   // AIB1: looked up once per program, not per draw
  if (velProg != p) { velProg = p; lvel = U(p, "uVelMode"); lpm = U(p, "uPrevModel"); }
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
      if (L.aoStr >= 0) glUniform1f(L.aoStr, 1.0f - m->aoStrength);   // TK3: a model's baked occlusion, weakened per model
    }
    glUniformMatrix4fv(lm, 1, GL_FALSE, glm::value_ptr(it.m));
    glUniform4fv(lb, 1, glm::value_ptr(pt.base)); glUniform1f(lme, pt.metal); glUniform1f(lr, pt.rough);
    glUniform4fv(lt, 1, glm::value_ptr(it.tint));
    glUniform1f(lth, it.thinPx * pxAngle);
    glUniform1i(U(p, "uGlass"), it.glass); glUniform1f(U(p, "uAlbFloor"), it.albFloor);
    glUniform1i(U(p, "uClipMe"), it.optic ? 0 : 1);   // AT6 (uVmClip is set only in the first-person pass)
    const int vel = perf.charVel ? it.vel : 0;   // AIB1
    glUniform1i(lvel, vel); if (vel) glUniformMatrix4fv(lpm, 1, GL_FALSE, glm::value_ptr(it.prevM));
    glDrawElements(GL_TRIANGLES, (GLsizei)pt.count, GL_UNSIGNED_INT, (void*)(uintptr_t)(pt.first * 4u));
    stats.draws++; stats.tris += (int)pt.count / 3;
  }
}

bool Renderer::renderModelSheet(const std::vector<ModelDraw>& items, const std::vector<DrawItem>& boxes, const mat4& view, const mat4& proj, vec3 camPos, int w, int h, std::vector<uint8_t>& rgba, bool transparent) {
  GLuint col, dep, fbo;
  glCreateTextures(GL_TEXTURE_2D, 1, &col); glTextureStorage2D(col, 1, GL_RGBA16F, w, h);
  glCreateTextures(GL_TEXTURE_2D, 1, &dep); glTextureStorage2D(dep, 1, GL_DEPTH_COMPONENT32F, w, h);
  glCreateFramebuffers(1, &fbo); glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0, col, 0); glNamedFramebufferTexture(fbo, GL_DEPTH_ATTACHMENT, dep, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo); glViewport(0, 0, w, h);
  glClearColor(0.62f, 0.64f, 0.66f, transparent ? 0.0f : 1.0f); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
  RenderView v; v.view = view; v.proj = proj; v.camPos = camPos;
  thumbMode = true;
  mat4 vp = proj * view;
  setLighting(progMain, v); glUniformMatrix4fv(U(progMain, "uVP"), 1, GL_FALSE, glm::value_ptr(vp)); drawItems(progMain, boxes, false);
  setLighting(progModel, v); glUniformMatrix4fv(U(progModel, "uVP"), 1, GL_FALSE, glm::value_ptr(vp)); drawModels(items);
  thumbMode = false;
  std::vector<float> px((size_t)w * h * 4), dz;
  glGetTextureImage(col, 0, GL_RGBA, GL_FLOAT, (GLsizei)(px.size() * 4), px.data());
  if (transparent) { dz.resize((size_t)w * h); glGetTextureImage(dep, 0, GL_DEPTH_COMPONENT, GL_FLOAT, (GLsizei)(dz.size() * 4), dz.data()); }
  rgba.resize((size_t)w * h * 4);
  // SM4: a transparent sheet is a live UI preview of a gun (smithing table, Guns editor): it takes the game's own display
  // transform (kToneFS filmic at the fixed Day exposure, sRGB), so it reads as the first-person gun. The old icon curve
  // below clips at ~1.1 linear (every sky glint on metal a flat white) and has no toe (milky darks).
  auto shoulder = [](float x) { return x / std::pow(1.0f + std::pow(x, 1.7f), 1.0f / 1.7f); };
  auto srgb = [](float c) { return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f; };
  for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
    const float* s = &px[((size_t)(h - 1 - y) * w + x) * 4]; uint8_t* o = &rgba[((size_t)y * w + x) * 4];
    if (transparent) {
      vec3 c = glm::max(vec3(s[0], s[1], s[2]) * 1.22f, vec3(0));   // x the Day exposure (lightingFixedExposure: 1)
      float L = glm::dot(c, vec3(0.2126f, 0.7152f, 0.0722f)), k = shoulder(L) / std::max(L, 1e-5f);
      for (int i = 0; i < 3; ++i) { float f = glm::mix(shoulder(c[i]), c[i] * k, 0.45f); f = std::pow(std::clamp(f, 0.0f, 1.0f), 1.10f); o[i] = (uint8_t)std::lround(srgb(f) * 255); }
      o[3] = dz[(size_t)(h - 1 - y) * w + x] < 1.0f ? 255 : 0;   // depth written = covered
      continue;
    }
    for (int c = 0; c < 3; ++c) { float f = std::clamp(s[c] * 0.9f, 0.0f, 4.0f); f = f / (1.0f + f * 0.25f) * 1.25f; f = std::pow(std::clamp(f, 0.0f, 1.0f), 1.0f / 2.2f); o[c] = (uint8_t)std::lround(f * 255); }
    o[3] = 255;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &fbo); glDeleteTextures(1, &col); glDeleteTextures(1, &dep);
  return true;
}

static const char* kLiveFS = R"GLSL(
#version 450 core
in vec2 vUV; uniform sampler2D uHdr, uDep; uniform float uSat; out vec4 o;
vec3 tone(vec3 c){ c = clamp(c*0.9, 0.0, 4.0); c = c/(1.0 + c*0.25)*1.25; return pow(clamp(c, 0.0, 1.0), vec3(1.0/2.2)); }   // as renderModelSheet
void main(){
  vec2 tx = 1.0/vec2(textureSize(uHdr, 0)); vec3 c = vec3(0.0); float a = 0.0;
  for (int k = 0; k < 4; ++k) {   // the 2x2 source texels of this pixel: soft edges
    vec2 uv = vUV + (vec2(k & 1, k >> 1) - 0.5)*tx;
    float cov = texture(uDep, uv).r < 1.0 ? 1.0 : 0.0; c += tone(texture(uHdr, uv).rgb)*cov; a += cov;
  }
  c = a > 0.0 ? c/a : vec3(0.0); a *= 0.25;
  float l = dot(c, vec3(0.2126, 0.7152, 0.0722)); o = vec4(mix(vec3(l), c, uSat), a);
}
)GLSL";
// weather corpus 08 s4: beyond 1.42 x the visibility V = 3.912 / sigma an unlit object is under 0.4 % contrast against the fog,
// so it is not drawn. sigma at the eye from the same terms kCommon's fog uses (aerial haze by height, the WX1 / FG1 fog
// layer), with the fog field and drift at their low end (0.6) so the cut is never nearer than what is visible.
// shortcut: no dithered fade at the cut (at 0.4 % contrast it does not show); reads the haze until the shared weather
// state's cullDistance lands, then switch to that one number
// The single cut (every cull site: trees, nature, ivy, lines, hlod) takes the extinction at the height of the highest
// ground plus 30 m, not at the eye: from a valley floor the fog thins along a ray that climbs to a ridge, so anything on
// the highest ground stays drawn; the per-object test (fogHides) does the exact cut for instances and trees
// the fog terms the shader is fed (setLighting), shared with the CPU tests below so culling follows the same air
float Renderer::hazeSigma() const { return lt.mapNoFog ? 0.0f : q.legacy ? 0.0016f * lt.mapHaze : wx.der.phys ? wx.der.sigma : q.hazeDensity * lt.mapHaze; }
float Renderer::envFogSigma() const {
  return q.legacy || !perf.envFog || fogTex == 0 || lt.mapNoFog ? 0.0f : (wx.der.fogLayerSigma > 0.0f ? wx.der.fogLayerSigma : lt.mapFogLayerDens >= 0.0f ? lt.mapFogLayerDens : std::max(wx.fogNow, 0.0f));
}
float Renderer::envFogH() const { return wx.der.fogTopM > 0.0f ? wx.der.fogTopM * 0.4f : lt.mapFogLayerH > 0.0f ? lt.mapFogLayerH : 22.0f; }
// With a W1 preset the base is the weather state's cullM (1.42 x its visibility at the ground); the height rule below only
// ever lengthens it (fog thins upward), so the shared cut is max(cullM, the height-conservative cut)
float Renderer::visCullDist(vec3 eye) const {
  if (!perf.visCull || lt.mapNoFog || q.legacy) return 1e9f;
  float y = std::max(eye.y, fogGroundMax + 30.0f);
  float sigma = hazeSigma() * std::exp(-std::max(y, 0.0f) / std::max(1.0f, q.hazeHeight));
  float env = !fogCpu.empty() ? envFogSigma() : 0.0f;
  float envH = envFogH();
  sigma += 0.6f * env * (camIndoor ? 0.15f : 1.0f) * std::exp(-std::max(y - fogGroundMin, 0.0f) / envH);   // the layer at its thinnest: its lowest floor, its low drift
  float cut = sigma > 1e-6f ? 1.42f * 3.912f / sigma : 1e9f;
  return wx.der.phys ? std::max(cut, wx.der.cullM) : cut;
}
bool Renderer::fogHides(vec3 eye, vec3 top) const {   // kCommon's applyFog terms on the CPU, drift at its low end (0.6)
  if (!perf.visCull || lt.mapNoFog || q.legacy) return false;
  float L = glm::length(top - eye); if (L < 1.0f) return false;
  auto hf = [](float k) { return std::fabs(k) > 1e-3f ? (1.0f - std::exp(-k)) / k : 1.0f - 0.5f * k; };   // the height integral (closed form)
  float H = std::max(1.0f, q.hazeHeight);
  float tau = hazeSigma() * L * std::exp(-std::max(eye.y, 0.0f) / H) * hf((top.y - eye.y) / H);
  float env = !fogCpu.empty() ? envFogSigma() : 0.0f;
  if (env > 0.0f) {
    const int N = 256; vec3 m = 0.5f * (eye + top);
    int i = (int)((m.x - fogBox.x) * fogBox.z * N), j = (int)((m.z - fogBox.y) * fogBox.w * N);
    i = ((i % N) + N) % N; j = ((j % N) + N) % N;   // the field wraps (GL_REPEAT), like the shader's lookup
    const vec4& F = fogCpu[(size_t)j * N + i];
    float eH = envFogH(), Lw = std::max(L - lt.mapFogNear, 0.0f);
    tau += env * (camIndoor ? 0.15f : 1.0f) * F.x * 0.6f * Lw * std::exp(-std::max(eye.y - F.z, 0.0f) / eH) * hf((top.y - eye.y) / eH);
  }
  return tau > 5.545f;   // ln 255: under one 8-bit step of the object's own contrast
}
// A program whose active samplers of different types (sampler2D / samplerCube / sampler2DArray ...) read the same texture
// unit is an invalid draw (GL 4.5 s7.10): garbage or nothing, found only by eye (units 25, 27, 26, 9 so far). Units are
// set at draw time, so this runs after views have rendered; program names are small integers, walked with glIsProgram.
int Renderer::checkSamplerUnits() {
  auto isSampler = [](GLenum t) {
    switch (t) { case GL_SAMPLER_1D: case GL_SAMPLER_2D: case GL_SAMPLER_3D: case GL_SAMPLER_CUBE: case GL_SAMPLER_2D_SHADOW: case GL_SAMPLER_2D_ARRAY:
      case GL_SAMPLER_2D_ARRAY_SHADOW: case GL_SAMPLER_CUBE_SHADOW: case GL_SAMPLER_CUBE_MAP_ARRAY: case GL_SAMPLER_2D_MULTISAMPLE: case GL_SAMPLER_BUFFER:
      case GL_INT_SAMPLER_2D: case GL_INT_SAMPLER_3D: case GL_INT_SAMPLER_2D_ARRAY: case GL_UNSIGNED_INT_SAMPLER_2D: case GL_UNSIGNED_INT_SAMPLER_3D:
      case GL_UNSIGNED_INT_SAMPLER_2D_ARRAY: case GL_UNSIGNED_INT_SAMPLER_BUFFER: case GL_INT_SAMPLER_BUFFER: return true; default: return false; } };
  int bad = 0, unset = 0, progs = 0;
  for (GLuint p = 1; p < 8192; ++p) {
    if (!glIsProgram(p)) continue;
    GLint linked = 0; glGetProgramiv(p, GL_LINK_STATUS, &linked); if (!linked) continue;
    ++progs;
    GLint n = 0; glGetProgramiv(p, GL_ACTIVE_UNIFORMS, &n);
    std::map<GLint, std::pair<GLenum, std::string>> unit;   // unit -> (type, first uniform on it)
    for (GLint k = 0; k < n; ++k) {
      char nm[256]; GLsizei len = 0; GLint size = 0; GLenum type = 0;
      glGetActiveUniform(p, (GLuint)k, sizeof nm, &len, &size, &type, nm);
      if (!isSampler(type)) continue;
      std::string base(nm); if (auto b = base.find('['); b != std::string::npos) base.resize(b);
      for (GLint e = 0; e < size; ++e) {   // sampler arrays: each element has its own unit
        std::string el = size > 1 ? base + "[" + std::to_string(e) + "]" : base;
        GLint loc = glGetUniformLocation(p, el.c_str()); if (loc < 0) continue;
        GLint u = 0; glGetUniformiv(p, loc, &u);
        auto [it, fresh] = unit.emplace(u, std::make_pair(type, el));
        if (!fresh && it->second.first != type) {   // unit 0 = never assigned (a feature off: snow mask, cloud shadows): reported, not failed
          if (u != 0) ++bad; else ++unset;
          logf("sampler clash%s: program %u, unit %d: %s (type 0x%04x) and %s (type 0x%04x)", u ? "" : " (unassigned, unit 0)", p, u, it->second.second.c_str(), it->second.first, el.c_str(), type);
        }
      }
    }
  }
  logf("sampler units: %d programs checked, %d clashes on assigned units (shader errors), %d on unassigned unit 0", progs, bad, unset);   // shortcut: unit-0 clashes are invalid draws by the spec too (NVIDIA draws them); fail them once those programs park their unused samplers
  shaderErrors += bad;
  return bad;
}
static GLuint gLiveProg = 0;   // CP 5.8: compiled at init (warmPrograms)
void Renderer::warmPrograms() {
  if (!gLiveProg) gLiveProg = compile(kFullVS, kLiveFS, "modelLive");
  extern void waterWarm(Renderer&); waterWarm(*this);
}
GLuint Renderer::renderModelLive(const std::vector<ModelDraw>& items, const mat4& view, const mat4& proj, vec3 camPos, int w, int h, float saturation, int slot) {
  struct Slot { GLuint hdr = 0, dep = 0, fbo = 0, ldr = 0, ldrFbo = 0; int W = 0, H = 0; };
  static Slot slots[2]; GLuint& prog = gLiveProg;
  Slot& S = slots[std::clamp(slot, 0, 1)];
  GLuint &hdr = S.hdr, &dep = S.dep, &fbo = S.fbo, &ldr = S.ldr, &ldrFbo = S.ldrFbo; int &W = S.W, &H = S.H;
  if (!prog) prog = compile(kFullVS, kLiveFS, "modelLive");
  w = std::max(w, 8); h = std::max(h, 8);
  if (w != W || h != H) {
    if (fbo) { glDeleteFramebuffers(1, &fbo); glDeleteFramebuffers(1, &ldrFbo); GLuint t[3] = {hdr, dep, ldr}; glDeleteTextures(3, t); }
    glCreateTextures(GL_TEXTURE_2D, 1, &hdr); glTextureStorage2D(hdr, 1, GL_RGBA16F, w * 2, h * 2);
    glCreateTextures(GL_TEXTURE_2D, 1, &dep); glTextureStorage2D(dep, 1, GL_DEPTH_COMPONENT32F, w * 2, h * 2);
    for (GLuint t : {hdr, dep}) { glTextureParameteri(t, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTextureParameteri(t, GL_TEXTURE_MAG_FILTER, GL_NEAREST); glTextureParameteri(t, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(t, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); }
    glCreateFramebuffers(1, &fbo); glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0, hdr, 0); glNamedFramebufferTexture(fbo, GL_DEPTH_ATTACHMENT, dep, 0);
    glCreateTextures(GL_TEXTURE_2D, 1, &ldr); glTextureStorage2D(ldr, 1, GL_RGBA8, w, h);
    glTextureParameteri(ldr, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(ldr, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glCreateFramebuffers(1, &ldrFbo); glNamedFramebufferTexture(ldrFbo, GL_COLOR_ATTACHMENT0, ldr, 0);
    W = w; H = h;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, fbo); glViewport(0, 0, w * 2, h * 2);
  glClearColor(0, 0, 0, 0); glClearDepth(1.0); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
  RenderView v; v.view = view; v.proj = proj; v.camPos = camPos;
  thumbMode = true;
  setLighting(progModel, v); glUniformMatrix4fv(U(progModel, "uVP"), 1, GL_FALSE, glm::value_ptr(proj * view)); drawModels(items);
  thumbMode = false;
  glBindFramebuffer(GL_FRAMEBUFFER, ldrFbo); glViewport(0, 0, w, h);
  glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
  glUseProgram(prog); glBindVertexArray(emptyVao);
  glBindTextureUnit(0, hdr); glBindTextureUnit(1, dep); glUniform1i(U(prog, "uHdr"), 0); glUniform1i(U(prog, "uDep"), 1); glUniform1f(U(prog, "uSat"), saturation);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindFramebuffer(GL_FRAMEBUFFER, 0); glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
  return ldr;
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
                                    "glass", "composite", "taa", "bloom", "fx", "precip", "weapon", "post", "ui"};
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
  if (fq[0][0]) {   // CP step 0: this slot's fragment counts (read once every one is ready; never waits)
    bool* fs = fqSet[gqSlot]; bool fr = true;
    for (int i = 0; i < GP_Count && fr; ++i) if (fs[i]) { GLint a = 0; glGetQueryObjectiv(fq[gqSlot][i], GL_QUERY_RESULT_AVAILABLE, &a); fr = a != 0; }
    if (fr) { std::fill(fragInvM, fragInvM + GP_Count, 0.0);
      for (int i = 0; i < GP_Count; ++i) if (fs[i]) { GLuint64 v = 0; glGetQueryObjectui64v(fq[gqSlot][i], GL_QUERY_RESULT, &v); fragInvM[i] = v / 1e6; } }
    std::fill(fs, fs + GP_Count, false);
  }
}
void Renderer::gpuMark(int p) {
  {   // P2: CPU time from this mark to the next one (NMK_DLPROF reports it per pass)
    auto now = std::chrono::steady_clock::now();
    if (cpuMarkP >= 0 && cpuMarkP < GP_Count) cpuPassMs[cpuMarkP] += std::chrono::duration<double, std::milli>(now - cpuMarkT).count();
    cpuMarkP = p; cpuMarkT = now;
  }
  gCrumb = p >= 0 && p < GP_Count ? gpuPassName(p) : "present";   // the render pass as the crumb: the hitch log and the crash reporter name it
  if (!gpuTimers || !gq[0][0] || p < 0 || p > GP_Count) return;
  glQueryCounter(gq[gqSlot][p], GL_TIMESTAMP); gqSet[gqSlot][p] = true;
  constexpr GLenum kFragInv = 0x82F4;   // GL_FRAGMENT_SHADER_INVOCATIONS (GL 4.6 / ARB_pipeline_statistics_query; not in our glad)
  if (fqActive >= 0) { glEndQuery(kFragInv); fqActive = -1; }
  if (fragStats && fqOk && p < GP_Count) {
    if (!fq[0][0]) {   /* core in GL 4.6; the context is 4.5, so the ARB extension */ GLint mj = 0, mn = 0, ne = 0; glGetIntegerv(GL_MAJOR_VERSION, &mj); glGetIntegerv(GL_MINOR_VERSION, &mn); fqOk = mj * 10 + mn >= 46; glGetIntegerv(GL_NUM_EXTENSIONS, &ne); for (GLint i = 0; i < ne && !fqOk; ++i) { const char* e = (const char*)glGetStringi(GL_EXTENSIONS, i); fqOk = e && std::strcmp(e, "GL_ARB_pipeline_statistics_query") == 0; } if (!fqOk) { logf("fragment statistics: GL_ARB_pipeline_statistics_query not available"); return; } for (auto& s : fq) glGenQueries(GP_Count, s); }
    glBeginQuery(kFragInv, fq[gqSlot][p]); fqSet[gqSlot][p] = true; fqActive = p;
  }
}

void Renderer::render(const RenderView& v0, const Terrain& t, const std::vector<DrawItem>& opaque,
                      const std::vector<DrawItem>& glass, const std::vector<DrawItem>& ghosts, const Overlay& ov) {
  auto c0 = std::chrono::high_resolution_clock::now();
  for (GLuint pg : {progLeaf, progLeafI, progGrass, progFolModel, progImp}) if (pg) {   // FL1 greenery (App::greenery); VO A1 hashed alpha under TAA
    glProgramUniform4fv(pg, U(pg, "uGreen"), 1, glm::value_ptr(green)); glProgramUniform4fv(pg, U(pg, "uGreenK"), 1, glm::value_ptr(greenK));
    static const float hashSpread = std::getenv("NMK_HASH_SPREAD") ? float(std::atof(std::getenv("NMK_HASH_SPREAD"))) : 0.2f;   // A1: dither width of the cut-out threshold (0.8 stippled stills under the TAA clamp; 0.35 / 0.2 look as off)
    glProgramUniform1f(pg, U(pg, "uHashAlpha"), perf.hashAlpha && q.taa && !thumbMode ? hashSpread : 0.0f); glProgramUniform1i(pg, U(pg, "uFrame"), frame);
  }
  {   // WS1 wind: the sway clock and the gust offset advance at this frame's rates (render() may run more than once per frame: by time)
    const double dt = windLastT < 0 ? 0.0 : std::clamp(double(time) - windLastT, 0.0, 0.25); windLastT = time;
    if (dt > 0) { windClockPrev = windClock; windGustPrev = windGustOff; windClock += dt * (0.7 + 0.3 * windSwayAmp()); windGustOff += dt * windSpeed(); windDrift += glm::dvec2(wx.der.windXZ) * dt; }
    if (holdTime) { windClockPrev = windClock; windGustPrev = windGustOff; }   // FL1 frozen captures: no motion
    const vec2 d = windDir2();
    // WS4: dust coating from the dust visibility (Sandstorm 100 m: 1, Blowing dust 3000 m: about 0.26, none: 0)
    const float dv = wx.st.dustVisM, dustK = perf.vegDust && dv > 0.0f ? std::clamp((std::log(10000.0f) - std::log(std::max(dv, 1.0f))) / (std::log(10000.0f) - std::log(100.0f)), 0.0f, 1.0f) : 0.0f;
    for (GLuint pg : {progLeaf, progLeafI, progLeafShadow, progLeafIShadow, progGrass, progGrassShadow, progGrassPre, progFolModel, progFolDepth, progImp}) if (pg) {
      glProgramUniform4f(pg, U(pg, "uWindV"), d.x, d.y, float(windGustOff), float(windGustPrev));
      glProgramUniform4f(pg, U(pg, "uWindP"), float(windClock), float(windClockPrev), windGustL(), windGustG());
      glProgramUniform1f(pg, U(pg, "uDustK"), dustK);
    }
  }
  if (!impWant.empty() && !thumbMode) impostorBakeSome();   // CP3: queued impostor bakes, 2 a frame
  if (!impostors.empty()) impostorPoll();
  if (displayW > 0) {   // P1: the internal size follows effScale() (the taau switch can flip it between frames: A/B)
    int iw = std::max(16, (int)std::lround(displayW * effScale())), ih = std::max(16, (int)std::lround(displayH * effScale()));
    if (iw != width || ih != height) { width = iw; height = ih; createTargets(); }
  }
  upActive = false;
  // shadow cascades and sky occlusion: standard depth; the camera passes below switch to reverse-Z
  glClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE); glClearDepth(1.0);
  RenderView v = v0;
  v.proj = reverseZ(v0.proj);
  if (v0.vmProj[2][3] != 0.0f) v.vmProj = reverseZ(v0.vmProj);
  stats.draws = 0; stats.tris = 0; stats.culled = 0; stats.casterCulled = 0; stats.shadowDraws = 0; stats.foliageDraws = 0; stats.instDrawn = 0;
  prepFoliageBatches();   // P2 folbatch   // roomCulled: counted from here too
  stats.roomCulled = 0; stats.probePending = stats.probeDirty = stats.probeStaleMax = 0;
  if (!holdTime) time += v.dt;
  wx.k += glm::clamp(wx.wantK() - wx.k, -v.dt / 3.0f, v.dt / 3.0f);   // WX2: Clear <-> Cloudy over 3 s (8 s went unnoticed behind the menu) (sun, sky, cover, exposure follow)
  if (wx.enclosed) wx.k = 0.0f;
  if (wx.fogNow < 0) wx.fogNow = wx.fogDens(); else wx.fogNow += glm::clamp(wx.fogDens() - wx.fogNow, -v.dt * 0.002f, v.dt * 0.002f);   // the fog level ramps too
  if (!thumbMode) bakeFog(t);
  uploadStaticDecals();      // GX5: before the probe captures (they run the main shader too)
  lightingBegin(*this, v);   // L2: time of day (sunDir = the key light until lightingEnd)
  thorTex = perf.terrainHorizon && !thumbMode ? t.horizonSync(sunDir) : 0;   // VOT T6, after lightingBegin: the hour's sun, not the Day sun (bakes on a new map, a new sun band, or after edits)
  thorMap = t.horizonMap(); thorW = t.horizonWeights(sunDir);
  weatherDerive(*this, v.camPos);      // W1: the weather state and what follows from it this frame (weather.cpp)
  uploadWeather();
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
  if (!thumbMode && !q.legacy && q.cloudShadows && clouds && perf.cloudShadow) {
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
  float radius = v.editor ? std::clamp(60.0f + camH * 1.2f, 60.0f, 260.0f) : (q.legacy ? 55.0f : q.shadowNearR);
  {   // GX2: inside a room the near cascade tightens to 24 m (about 1.2 cm texels at 4096) so window bars cast crisp shadows
    bool indoor = false;
    for (auto& b : rooms) { vec3 l = vec3(glm::inverse(b.m) * vec4(v.camPos, 1)); if (std::fabs(l.x) < b.half.x && std::fabs(l.y) < b.half.y && std::fabs(l.z) < b.half.z) { indoor = true; break; } }
    camIndoor = indoor;   // WX1: environmental fog thins inside rooms
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
  glEnable(GL_SCISSOR_TEST); glScissor(0, 0, shadowSize, shadowSize); glClear(GL_DEPTH_BUFFER_BIT); glDisable(GL_SCISSOR_TEST);   // VO L3: the near half only
  glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(1.5f, 2.0f);
  glDisable(GL_CULL_FACE);
  glUseProgram(progShadow);
  glUniformMatrix4fv(U(progShadow, "uVP"), 1, GL_FALSE, glm::value_ptr(lightVP)); depthVP = lightVP; depthVPSet = true;
  // every depth / colour pass culls against its own view-projection (docs/rendering.md#frustum-culling): a cascade keeps
  // the casters inside its ortho box even when they are behind the camera
  Frustum frNear = Frustum::fromVP(lightVP);
  castCull = perf.casterCull && !thumbMode; castEye = v.camPos;   // R2: on for the two sun cascades only
  const float leafLod = perf.leafShadowLod && !thumbMode ? q.leafShadowLodM : -1.0f;   // R2: full cards under / beside the eye, the LOD past 12 m
  const int shDraws0 = stats.draws;
  // OPT wxsun (weather corpus 08 s4.2): when the direct sun is under ~5 % of a level surface's light its cast shadows are
  // invisible: the three cascades are neither drawn nor sampled; between 5 and 10 % their strength fades (nothing snaps).
  // Contact shadows, GTAO and sky visibility stay. sunshadow off: no cascades at all (knock-out)
  sunShK = perf.wxSun && q.lightNew && !q.legacy && !thumbMode ? glm::smoothstep(0.05f, 0.10f, sunDirectFrac) : 1.0f;
  stats.sunDirectPct = 100.0f * sunDirectFrac;
  const bool sunSh = perf.sunShadow && sunShK > 0.0f;
  if (sunSh) {
  drawItems(progShadow, opaque, true, &frNear);
  if (!foliage.empty() && !thumbMode) drawFoliage(progLeafShadow, lightVP, &frNear, false, leafLod);
  if (grass && !thumbMode && perf.grassShadow) {   // GX3: near pasture clumps cast (within the preset's shadow radius, in the eye's view)
    glUseProgram(progGrassShadow);
    { int n = (int)std::min<size_t>(16, grassVeh.size() / 2); glUniform1i(U(progGrassShadow, "uVehN"), n); if (n) glUniform4fv(U(progGrassShadow, "uVeh"), 2 * n, glm::value_ptr(grassVeh[0])); }   // TK3
    glUniform3fv(U(progGrassShadow, "uCamPos"), 1, glm::value_ptr(v.camPos)); glUniform1f(U(progGrassShadow, "uTime"), time);
    glUniform1i(U(progGrassShadow, "uGAOn"), perf.grassAtlas && grassAtlasReady && grassAtlasTier);
    grass->drawShadow(progGrassShadow, lightVP, v0.proj * v.view, v.camPos);
  }
  if (!folModels.empty() && !thumbMode) drawFolModels(true, lightVP, &frNear, 1.0f);   // NT8: ground plants under 1 m cast no sun shadow
  glUseProgram(progShadow);
  if (!worldModels.empty()) drawModelsDepth(progShadow, worldModels, &frNear);
  }
  // VO L3 mid cascade (Lighting New, Medium / High): about 3x the near radius in the atlas's right half, texel-snapped like
  // the near one (no crawl), redrawn every 4th frame on Medium / 2nd on High or when its focus moves 4 m (movers lag a few frames there)
  midOn = q.lightNew && perf.midCascade && !q.legacy && !thumbMode && shadowTier >= 1 && sunSh;
  const bool midCacheOn = midOn && perf.midCache && shadowTier >= 2;   // CP4 midcache (High / Ultra): a static layer redrawn on change; movers drawn into the mid cascade every frame
  if (midCacheOn) {
    if (midStaticSize != shadowSize) {
      if (midStaticFbo) { glDeleteFramebuffers(1, &midStaticFbo); glDeleteTextures(1, &midStaticTex); }
      glCreateTextures(GL_TEXTURE_2D, 1, &midStaticTex); glTextureStorage2D(midStaticTex, 1, GL_DEPTH_COMPONENT32F, shadowSize, shadowSize);
      glCreateFramebuffers(1, &midStaticFbo); glNamedFramebufferTexture(midStaticFbo, GL_DEPTH_ATTACHMENT, midStaticTex, 0);
      glNamedFramebufferDrawBuffer(midStaticFbo, GL_NONE); glNamedFramebufferReadBuffer(midStaticFbo, GL_NONE);
      midStaticSize = shadowSize; midFrame = -1000;
    }
    const float mR = std::max(90.0f, radius * 3.0f);
    const vec3 mfw = v.camPos + vec3(fwd.x, 0, fwd.z) * mR * 0.5f;
    const size_t nS = std::min(opaqueStatic, opaque.size());
    if (frame - midFrame >= 60 || mR != midR || glm::distance(mfw, midFocus) > 4.0f || sunDir != midSun) {   // the static layer: on a move / sun change, else every 60 frames (LOD changes)
      midR = mR; midFrame = frame; midFocus = mfw; midSun = sunDir;
      vec3 mf = mfw; mf.y = t.height(mf.x, mf.z);
      mat4 lv3; lightVPMid = stableOrtho(mf, sunDir, midR, 400.0f, shadowSize, lv3);
      Frustum frMid = Frustum::fromVP(lightVPMid);
      minCaster = perf.smallCasters ? 3.0f * (2 * midR / shadowSize) : 0.0f;
      glBindFramebuffer(GL_FRAMEBUFFER, midStaticFbo); glViewport(0, 0, shadowSize, shadowSize); glClear(GL_DEPTH_BUFFER_BIT);
      glUseProgram(progShadow); glUniformMatrix4fv(U(progShadow, "uVP"), 1, GL_FALSE, glm::value_ptr(lightVPMid)); depthVP = lightVPMid;
      itemHi = (uint32_t)nS; drawItems(progShadow, opaque, true, &frMid); itemHi = UINT32_MAX;
      if (!foliage.empty()) drawFoliage(progLeafShadow, lightVPMid, &frMid, false, perf.leafShadowLod ? 0.0f : -1.0f);
      if (!folModels.empty()) { drawFolModels(true, lightVPMid, &frMid, 4.0f, perf.vegCascades); glUseProgram(progShadow); }
      minCaster = 0;
    }
    // every frame: the static layer into the atlas's right half, then what moves
    glCopyImageSubData(midStaticTex, GL_TEXTURE_2D, 0, 0, 0, 0, shadowTex, GL_TEXTURE_2D, 0, shadowSize, 0, 0, shadowSize, shadowSize, 1);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo); glViewport(shadowSize, 0, shadowSize, shadowSize);
    Frustum frMid = Frustum::fromVP(lightVPMid);
    glUseProgram(progShadow); glUniformMatrix4fv(U(progShadow, "uVP"), 1, GL_FALSE, glm::value_ptr(lightVPMid)); depthVP = lightVPMid;
    if (opaque.size() > nS) { itemLo = (uint32_t)nS; drawItems(progShadow, opaque, true, &frMid); itemLo = 0; }
    if (!worldModels.empty()) drawModelsDepth(progShadow, worldModels, &frMid);
  } else if (midOn) {
    const float mR = std::max(90.0f, radius * 3.0f);
    const vec3 mfw = v.camPos + vec3(fwd.x, 0, fwd.z) * mR * 0.5f;
    if (frame - midFrame >= (shadowTier >= 2 ? 2 : 4) || mR != midR || glm::distance(mfw, midFocus) > 4.0f || sunDir != midSun) {
      midR = mR; midFrame = frame; midFocus = mfw; midSun = sunDir;
      vec3 mf = mfw; mf.y = t.height(mf.x, mf.z);
      mat4 lv3; lightVPMid = stableOrtho(mf, sunDir, midR, 400.0f, shadowSize, lv3);
      Frustum frMid = Frustum::fromVP(lightVPMid);
      minCaster = perf.smallCasters ? 3.0f * (2 * midR / shadowSize) : 0.0f;   // casters under ~3 mid texels: the contact shadows (L1) and the near cascade carry them
      glViewport(shadowSize, 0, shadowSize, shadowSize);
      glEnable(GL_SCISSOR_TEST); glScissor(shadowSize, 0, shadowSize, shadowSize); glClear(GL_DEPTH_BUFFER_BIT); glDisable(GL_SCISSOR_TEST);
      glUseProgram(progShadow); glUniformMatrix4fv(U(progShadow, "uVP"), 1, GL_FALSE, glm::value_ptr(lightVPMid)); depthVP = lightVPMid;
      drawItems(progShadow, opaque, true, &frMid);
      if (!foliage.empty()) drawFoliage(progLeafShadow, lightVPMid, &frMid, false, perf.leafShadowLod ? 0.0f : -1.0f);
      if (!folModels.empty()) { drawFolModels(true, lightVPMid, &frMid, 4.0f, perf.vegCascades); glUseProgram(progShadow); }   // trees only, as in the far cascade (CP5: from their far level)
      if (!worldModels.empty()) drawModelsDepth(progShadow, worldModels, &frMid);
      minCaster = 0;
    }
  } else midFrame = -1000;
  gpuMark(GP_ShadowFar);
  // far cascade: the whole visible village / fields (texel ~0.23 m), blended in where the near one ends
  // PF2 farcache: redrawn every 2nd frame, or at once when its focus moves > 8 m, its radius or the key light changes;
  // in between the shaders keep sampling the last map with the matrix it was drawn with (moving casters lag <= 1 frame)
  c3On = farAtlas && perf.cascade4 && q.lightNew && !q.legacy && !thumbMode && q.farCascade && sunSh;
  const float fR = c3On ? std::max(600.0f, radius * 6.0f) : std::max(240.0f, radius * 2.5f);   // CP4: with the fourth cascade the far one reaches ~600 m
  const int farX = farAtlas ? shadowFarSize : 0;   // the far cascade's half of the far map
  stats.c3 = c3On ? 1 : 0; stats.farAtlas = farAtlas ? 1 : 0;
  const bool vegC3 = !(perf.vegCascades && midOn);   // vegetation stays in the two nearest cascades (CP5)
  if (c3On) {   // CP4 cascade4: the old far range at the far map's resolution, redrawn every 2nd frame or on a 6 m move / sun change
    const float cR = std::max(240.0f, radius * 2.5f);
    const vec3 cfw = v.camPos + vec3(fwd.x, 0, fwd.z) * cR * 0.6f;
    if (frame - c3Frame >= 2 || cR != c3R || glm::distance(cfw, c3Focus) > 6.0f || sunDir != c3Sun) {
      c3R = cR; c3Frame = frame; c3Focus = cfw; c3Sun = sunDir; farPass = true;
      vec3 cf = cfw; cf.y = t.height(cf.x, cf.z);
      mat4 lv4; lightVPC3 = stableOrtho(cf, sunDir, c3R, 400.0f, shadowFarSize, lv4);
      Frustum frC = Frustum::fromVP(lightVPC3);
      minCaster = perf.smallCasters ? 3.0f * (2 * c3R / shadowFarSize) : 0.0f;
      glBindFramebuffer(GL_FRAMEBUFFER, shadowFarFbo); glViewport(0, 0, shadowFarSize, shadowFarSize);
      glEnable(GL_SCISSOR_TEST); glScissor(0, 0, shadowFarSize, shadowFarSize); glClear(GL_DEPTH_BUFFER_BIT); glDisable(GL_SCISSOR_TEST);
      glUseProgram(progShadow); glUniformMatrix4fv(U(progShadow, "uVP"), 1, GL_FALSE, glm::value_ptr(lightVPC3)); depthVP = lightVPC3; depthVPSet = true;
      drawItems(progShadow, opaque, true, &frC);
      if (!foliage.empty() && vegC3) drawFoliage(progLeafShadow, lightVPC3, &frC, false, perf.leafShadowLod ? 0.0f : -1.0f);
      if (!folModels.empty() && vegC3) { drawFolModels(true, lightVPC3, &frC, 4.0f); glUseProgram(progShadow); }
      if (!worldModels.empty()) drawModelsDepth(progShadow, worldModels, &frC);
      minCaster = 0; farPass = false;
    }
  } else c3Frame = -1000;
  const vec3 ffw = v.camPos + vec3(fwd.x, 0, fwd.z) * fR * 0.6f;
  // GP1 shadowcache: the list's static part (opaque[0 .. opaqueStatic) and the canopies) is drawn into its own map only
  // when it changes - the cascade re-centres (> 8 m), the key light or radius changes, or the signature of what is in the
  // list does (a breached bay's rebuilt mesh, a door or turret moving, a placed / removed / LOD-switched item, a felled
  // tree) - and at most every farEvery frames, as before. The live map is that layer copied, with the dynamic part
  // (opaque[opaqueStatic ..): weapons, debris, fragments; and the models: soldiers, vehicles) drawn on top every
  // farEvery frames. Far canopy sway is not re-drawn (under a far texel). Depth only: the sun's colour / cloud
  // transmittance changing over time does not touch it.
  const size_t nStat = std::min(opaqueStatic, opaque.size());
  const bool cacheOn = perf.shadowCache && perf.farCache && farStaticFbo;
  uint64_t fsig = 1469598103934665603ull;
  const bool vegFar = !(perf.vegCascades && midOn);   // CP5 (corpus 09 s3.5): vegetation in the two nearest cascades only while the mid one is drawn
  if (cacheOn) {
    auto mix = [&](uint64_t x) { fsig = (fsig ^ x) * 1099511628211ull; };
    auto fb = [](float f) { uint32_t u; std::memcpy(&u, &f, 4); return (uint64_t)u; };
    for (size_t k = 0; k < nStat; ++k) {
      const DrawItem& it = opaque[k]; if (!it.mesh) continue;
      mix((uint64_t)(uintptr_t)it.mesh); mix(((uint64_t)it.mesh->vao << 32) | (uint32_t)it.mesh->count);
      mix((fb(it.model[3].x) << 32) | fb(it.model[3].z)); mix((fb(it.model[3].y) << 32) | fb(it.model[0].x)); mix((fb(it.model[1].y) << 32) | fb(it.model[2].x));
    }
    for (auto& f : foliage) { mix((uint64_t)(uintptr_t)f.tree); mix((fb(f.m[3].x) << 32) | fb(f.m[3].z)); mix((fb(f.m[3].y) << 32) | (uint64_t)std::max<int>(f.cur, 3)); }
    mix((uint64_t)nStat); mix(perf.casterCull | perf.smallCasters << 1 | perf.farLeaf << 2 | perf.leafShadowLod << 3 | perf.instDepth << 4 | (uint64_t)vegFar << 5);
  }
  const bool farCadence = frame - farFrame >= std::max(perf.farLite ? 4 : 1, q.farEvery);   // OPT farlite: the far cascade at most every 4th frame
  const bool farMoved = fR != farR || glm::distance(ffw, farFocus) > 8.0f || sunDir != farSun;
  const bool farDyn = opaque.size() > nStat || !worldModels.empty();
  const bool staticRedo = cacheOn && (farMoved || (farCadence && fsig != farSig));
  const bool farRedo = cacheOn ? staticRedo || (farCadence && (farDyn || farHadDyn))
                               : !perf.farCache || farCadence || farMoved;
  stats.farStatic = cacheOn ? (staticRedo ? 1 : 0) : -1;
  if (!q.legacy && q.farCascade && !thumbMode && farRedo && sunSh) {
    farR = fR; farFrame = frame; farFocus = ffw; farSun = sunDir; farPass = true;
    vec3 ff = ffw; ff.y = t.height(ff.x, ff.z);
    mat4 lv2; lightVPFar = stableOrtho(ff, sunDir, farR, 400.0f, shadowFarSize, lv2);
    minCaster = perf.smallCasters ? (perf.farLite ? 4.0f : 2.0f) * (2 * farR / shadowFarSize) : 0.0f;   // PF2: casters under ~1 texel
    Frustum frFar = Frustum::fromVP(lightVPFar);
    if (!cacheOn || staticRedo) {   // the static layer (everything, without the cache)
      glBindFramebuffer(GL_FRAMEBUFFER, cacheOn ? farStaticFbo : shadowFarFbo);
      glViewport(cacheOn ? 0 : farX, 0, shadowFarSize, shadowFarSize);
      glEnable(GL_SCISSOR_TEST); glScissor(cacheOn ? 0 : farX, 0, shadowFarSize, shadowFarSize); glClear(GL_DEPTH_BUFFER_BIT); glDisable(GL_SCISSOR_TEST);
      glUseProgram(progShadow);
      glUniformMatrix4fv(U(progShadow, "uVP"), 1, GL_FALSE, glm::value_ptr(lightVPFar)); depthVP = lightVPFar; depthVPSet = true;
      if (cacheOn) itemHi = (uint32_t)nStat;
      drawItems(progShadow, opaque, true, &frFar);
      itemHi = UINT32_MAX;
      if (!foliage.empty() && vegFar) drawFoliage(progLeafShadow, lightVPFar, &frFar, false, perf.leafShadowLod ? 0.0f : -1.0f);
      farSig = fsig;
    }
    if (cacheOn) {   // the live map: the static layer, then what moves
      glCopyImageSubData(farStaticTex, GL_TEXTURE_2D, 0, 0, 0, 0, shadowFarTex, GL_TEXTURE_2D, 0, farX, 0, 0, shadowFarSize, shadowFarSize, 1);
      glBindFramebuffer(GL_FRAMEBUFFER, shadowFarFbo); glViewport(farX, 0, shadowFarSize, shadowFarSize);
      if (opaque.size() > nStat) {
        glUseProgram(progShadow);
        glUniformMatrix4fv(U(progShadow, "uVP"), 1, GL_FALSE, glm::value_ptr(lightVPFar));
        itemLo = (uint32_t)nStat; drawItems(progShadow, opaque, true, &frFar); itemLo = 0;
      }
      farHadDyn = farDyn;
    }
    glUseProgram(progShadow); glUniformMatrix4fv(U(progShadow, "uVP"), 1, GL_FALSE, glm::value_ptr(lightVPFar));
    if (!folModels.empty() && vegFar) { drawFolModels(true, lightVPFar, &frFar, 4.0f); glUseProgram(progShadow); }   // NT8: trees only
    if (!worldModels.empty()) drawModelsDepth(progShadow, worldModels, &frFar);
    minCaster = 0;
  }
  castCull = false; farPass = false; stats.shadowDraws = stats.draws - shDraws0;
  gpuMark(GP_SkyOcc);
  // ---- sky occlusion (top down). PF2 occcache: redrawn every 16 frames, or at once when the camera moves 4 m across
  // or 8 m up / down (the map covers +-90 m, so the camera stays >= 86 m inside it); thumbnails always redraw.
  float occR = perf.occSmall ? 64.0f : 90.0f;
  occR = std::max(occR, std::min(1.5f * std::max(v.camPos.y - t.height(v.camPos.x, v.camPos.z), 0.0f), 400.0f));   // a high (plan / overview) camera sees ground far out: the box grows with height above the ground   // OPT occsmall (weather corpus s3.5): the sky-occlusion map over +-64 m (fewer casters, finer texels)
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
  glUniformMatrix4fv(U(progShadow, "uVP"), 1, GL_FALSE, glm::value_ptr(occVP)); depthVP = occVP; depthVPSet = true; farPass = true;
  Frustum frOcc = Frustum::fromVP(occVP);
  minCaster = perf.smallCasters ? std::max(0.3f, 2.0f * (2 * occR / occSize)) : 0.0f;   // PF2: props too small to shade the sky
  drawItems(progShadow, opaque, true, &frOcc);
  if (!foliage.empty() && !thumbMode) drawFoliage(progLeafShadow, occVP, &frOcc, false, perf.leafShadowLod ? 0.0f : -1.0f);
  glUseProgram(progShadow); glUniformMatrix4fv(U(progShadow, "uVP"), 1, GL_FALSE, glm::value_ptr(occVP));
  if (!worldModels.empty()) drawModelsDepth(progShadow, worldModels, &frOcc);
  minCaster = 0; farPass = false;
  }
  glDisable(GL_POLYGON_OFFSET_FILL);
  lightingPrepass(*this, v, opaque, t);   // L2: light upload, light shadow atlas, probe captures, froxel clusters (standard Z)

  gpuMark(GP_Clears);
  // ---- main HDR pass. With TAA the world is rendered with a sub-pixel jitter (Halton 2,3, 8 phases); effects,
  // the weapon and overlays use the unjittered matrices.
  // P1 TAAU: below the output size the TAA resolve reconstructs at output size (TAA is then always on: it is the upscaler)
  const bool taau = upscaleMode == 0 && !q.legacy && !thumbMode && displayW > 0 && (width < displayW || height < displayH);
  bool taaOn = (q.taa || taau) && !q.legacy && !thumbMode;
  if (taau != histUp) { taaValid = false; histUp = taau; }   // the two histories have different sizes
  mat4 vpU = v.proj * v.view;
  mat4 projJ = v.proj;
  if (taaOn) {
    auto halton = [](int i, int b) { float f = 1, r = 0; while (i > 0) { f /= b; r += f * (i % b); i /= b; } return r; };
    int nPh = q.taaPhases;
    if (perf.taaJitPh && taau) { float sc = (float)displayW / std::max(width, 1); nPh = std::max(nPh, (int)std::lround(8.0f * sc * sc)); }   // CP1 taajitph: 8 x (display / render)^2 phases under upscaling
    int ph = frame % std::clamp(nPh, 1, 64) + 1;
    jitterPx = vec2(halton(ph, 2) - 0.5f, halton(ph, 3) - 0.5f) * (taau ? 1.0f : q.taaJitter);   // +-0.25 px default: edges stay crisp; TAAU: a whole internal pixel
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
    glUniform1i(U(progSky, "uCloudOn"), clouds && perf.clouds && !(perf.skyGate && wx.k > 0.99f) ? 1 : 0);   /* OPT skygate: under a closed deck the low cloud field is not drawn (the grey layer covers) */ glUniform1f(U(progSky, "uCloudCover"), cloudCoverNow()); glUniform1i(U(progSky, "uCloudQ"), wx.quality);
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
    {   // W5 tdust (weather corpus 06, saltation): streamer strength drive(u) = clamp(((u - u_t) / (u_full - u_t))^3, 0, 1), u_t 5.5 m/s,
        // full by 14 m/s, on above u_t and off below 0.8 u_t (hysteresis); gusts from the shared state; arid maps only
      using clk = std::chrono::steady_clock; static clk::time_point last = clk::now(); static double tAcc = 0;
      const double dt = std::min(0.1, std::chrono::duration<double>(clk::now() - last).count()); last = clk::now(); tAcc += dt;
      const auto& ws = wx.st; const float gp = std::max(1.0f, ws.gustPeriodS);
      const float u = ws.windMs * (1.0f + (ws.gust - 1.0f) * 0.5f * (1.0f + (float)std::sin(6.2831853 * tAcc / gp)));
      const float ut = 5.5f, uf = 14.0f;
      dustOn = u > ut || (dustOn && u > 0.8f * ut);
      float k = dustOn ? std::clamp((u - 0.8f * ut) / (uf - 0.8f * ut), 0.0f, 1.0f) : 0.0f; k = k * k * k;
      if (ws.dustVisM > 0) k = std::max(k, 0.6f);   // Blowing dust / Sandstorm: always streaming
      const bool arid = groundPaintArid();
      vec2 dir = glm::length(wx.der.windXZ) > 1e-3f ? glm::normalize(wx.der.windXZ) : vec2(std::sin(glm::radians(ws.windDirDeg)), std::cos(glm::radians(ws.windDirDeg)));
      dustPhase = std::fmod(dustPhase + (float)dt * 0.35f * u, 4096.0f);   // the ribbons travel at about a third of the wind
      dustU = vec4(dir, perf.terrainDust && arid && !thumbMode ? k : 0.0f, dustPhase);
    }
    const GLuint progTN = perf.terrainEarlyZ ? progTerrainND : 0;   // PF: tzearly
    for (GLuint progTerrain : {this->progTerrain, progTN}) { if (!progTerrain) continue;   // the same set-up for both terrain shaders
    setLighting(progTerrain, v);
    glUniformMatrix4fv(U(progTerrain, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
    glUniform1i(U(progTerrain, "uGrid"), v.grid ? 1 : 0);
    { vec3 gc(0); float gk = 0; vec2 gr(0, 1);   // CP4 grasstint
      if (perf.grassTint && grass && !thumbMode && grass->meanTop(gc)) { const GrassPreset& P = grassPreset(grass->quality); if (P.radius > 0) { gk = 0.5f; gr = vec2(P.nearR, P.radius); } }
      glUniform4f(U(progTerrain, "uGrassTop"), gc.r, gc.g, gc.b, gk); glUniform2f(U(progTerrain, "uGrassTopR"), gr.x, gr.y); }
    terrainCacheBind(t, progTerrain);
    glEnable(GL_CULL_FACE); glCullFace(GL_BACK);
    { extern bool gGroundPaintPerf; gGroundPaintPerf = perf.groundPaint; }
    // FL1: the ground a hair behind anything laid on it (paving, plinths, aprons, roads, ground patches at ~1 cm),
    // so those win the depth test every frame instead of z-fighting with the terrain
    glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(-zToward(), -2.0f * zToward());
    glProgramUniform1f(progTerrain, glGetUniformLocation(progTerrain, "uTerrCave"), lt.mapCave);   // CV1
    {   // GG1: ground gloss limits (content/settings/ground_gloss.json)
      static json gj = [] { json j; try { j = json::parse(readTextFile(contentDir() / "settings" / "ground_gloss.json")); } catch (...) {} return j.is_object() ? j : json::object(); }();
      static const vec2 mud(gj.value("mudDry", 0.92f), gj.value("mudDamp", 0.8f));
      static const float gmin = gj.value("groundMinRough", 0.82f), pmin = gj.value("pavedMinRough", 0.68f);
      glProgramUniform2f(progTerrain, glGetUniformLocation(progTerrain, "uMudRough"), ggOff ? 0.55f : mud.x, ggOff ? 0.22f : mud.y);
      glProgramUniform1f(progTerrain, glGetUniformLocation(progTerrain, "uGroundMinRough"), ggOff ? 0.0f : gmin);
      glProgramUniform1f(progTerrain, glGetUniformLocation(progTerrain, "uPavedMinRough"), ggOff ? 0.0f : pmin);
      static const float dspec = gj.value("drySpecular", 0.35f);
      glProgramUniform1f(progTerrain, glGetUniformLocation(progTerrain, "uGroundSpec"), ggOff ? 1.0f : dspec);
    }
    { static const int dbg = std::getenv("NMK_DBG_TERRAIN") ? std::atoi(std::getenv("NMK_DBG_TERRAIN")) : 0; glProgramUniform1i(progTerrain, glGetUniformLocation(progTerrain, "uDbgTerr"), dbgTerr >= 0 ? dbgTerr : dbg); }   // GG1 measurement
    glProgramUniform4fv(progTerrain, glGetUniformLocation(progTerrain, "uDust"), 1, glm::value_ptr(dustU));   // W5 tdust
    {   // VFX rev3 4.4 / 8.2 tpom tiers by preset: Ultra Low off; Low one offset 0.015 m within 8 m; Medium 8 steps 0.030 m / 18 m;
        // High 10 steps 0.050 m / 35 m; Ultra 10 steps 0.065 m / 55 m (texTap caps the march at 10). Custom follows the material tier.
      GfxPreset pr = settings().s.preset();
      if (pr == GfxPreset::Custom) pr = q.matLayer >= 2 ? GfxPreset::High : q.matLayer == 1 ? GfxPreset::Medium : GfxPreset::Low;
      vec4 tp(0.0f);
      if (perf.terrainPom && !thumbMode) switch (pr) {
        case GfxPreset::Low: tp = vec4(0.015f, 1.0f, 8.0f, 0.5f); break;
        case GfxPreset::Medium: tp = vec4(0.030f, 8.0f, 18.0f, 0.5f); break;
        case GfxPreset::High: tp = vec4(0.050f, 10.0f, 35.0f, 0.5f); break;
        case GfxPreset::Ultra: tp = vec4(0.065f, 10.0f, 55.0f, 0.5f); break;
        default: break;   // Ultra Low: off
      }   // shortcut: w = 0.5 uv per metre (the ground sets' 2 m tile); read it from the soil slot's scale if the sets diverge
      glProgramUniform4fv(progTerrain, glGetUniformLocation(progTerrain, "uTPom"), 1, glm::value_ptr(tp));
    }
    }
    t.drawOpts.normals = perf.terrainNormals; t.drawOpts.morph = perf.terrainMorph; t.drawOpts.skyView = perf.terrainSkyView; t.drawOpts.tileLod = perf.terrainTileLod;   // VOT T1 / T5 / T2
    // PF terrain: samples the terrain pass wrote, read a ring slot later (the shaded-fragment count is the bench's fragment query, gpuMark)
    if (!terrQ[0][1]) for (auto& q : terrQ) glCreateQueries(GL_SAMPLES_PASSED, 1, &q[1]);
    if (terrQn >= 2) { GLuint64 b = 0; GLint ok = 0; glGetQueryObjectiv(terrQ[terrQi][1], GL_QUERY_RESULT_AVAILABLE, &ok); if (ok) { glGetQueryObjectui64v(terrQ[terrQi][1], GL_QUERY_RESULT, &b); stats.terrSamples = (double)b; } }
    glBeginQuery(GL_SAMPLES_PASSED, terrQ[terrQi][1]);
    const GLuint progTD = progTN && perf.terrainPrepass ? progTerrainDepth : 0;   // PF: tzprepass
    if (progTD) { glProgramUniformMatrix4fv(progTD, U(progTD, "uVP"), 1, GL_FALSE, glm::value_ptr(vp)); glProgramUniform3fv(progTD, U(progTD, "uCamPos"), 1, glm::value_ptr(v.camPos)); }
    t.draw(progTerrain, v.camPos, vp, progTN, progTD);   // streamed LOD chunks + deformation tiles (terrain_chunks.cpp)
    glEndQuery(GL_SAMPLES_PASSED); terrQi ^= 1; ++terrQn;
    glDisable(GL_POLYGON_OFFSET_FILL);
  };
  if (!perf.terrainLast) drawTerrain();
  gpuMark(GP_Opaque);
  // opaque
  glEnable(GL_CULL_FACE); // all authored solids are closed with outward winding
  setLighting(progMain, v);
  glUniform1i(U(progMain, "uWeather"), thumbMode ? 0 : 1);
  glUniform1f(U(progMain, "uMapArid"), groundPaintArid() ? 1.0f : 0.0f);
  glUniform1f(U(progMain, "uMapCave"), lt.mapCave);
  glUniform1f(U(progMain, "uMapGrime"), std::getenv("NMK_AB_OFF") && std::strstr(std::getenv("NMK_AB_OFF"), "grime") ? 0.0f : lt.mapGrime);   // NT3
  glUniform1i(U(progMain, "uNoWallRelief"), wallRelief ? 0 : 1);
  glUniform1i(U(progMain, "uMatLod"), !thumbMode && perf.matLod ? 1 : 0);
  tex::setImgMat(perf.imgMat);   // MA1: the image-material slot table (re-uploads only when it flips)
  static const std::string abOff = std::getenv("NMK_AB_OFF") ? std::getenv("NMK_AB_OFF") : "";   // NT3 dev A/B: decals,layer,grime
  glUniform1i(U(progMain, "uGrafLayer1"), tex::slotLayer(tex::slotIndex("decal_graffiti")) + 1);
  glUniform1i(U(progMain, "uMatLayer"), thumbMode || q.legacy || !perf.matLayer || abOff.find("layer") != std::string::npos ? 0 : q.matLayer);
  // PERF1 matfar: Medium / Low (material detail 0-1) from 25 m (x the zoom, so scopes keep the detail); High keeps its look
  glUniform1f(U(progMain, "uMatFar"), thumbMode || q.legacy || !perf.matFar || q.matLayer >= 2 ? 0.0f : 25.0f * std::max(1.0f, 1.43f / std::max(0.1f, v.proj[1][1])));
  glUniform1i(U(progMain, "uRelief"), thumbMode || q.legacy || !perf.relief || !perf.matLayer || q.matLayer == 0 ? 0 : 1);   // MA1b: Medium and up
  uploadStaticDecals();
  { static const int dbg = std::getenv("NMK_GX5_DEBUG") ? std::atoi(std::getenv("NMK_GX5_DEBUG")) : 0; glUniform1i(U(progMain, "uGxDebug"), gxDebug ? gxDebug : dbg); }
  glUniform1i(U(progMain, "uSDecOn"), thumbMode || q.legacy || q.matLayer == 0 || lt.mapClean || !perf.staticDecals || !sdecBuf || sdec.empty() || abOff.find("decals") != std::string::npos ? 0 : 1);
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
    glUniformMatrix4fv(U(progModel, "uVPU"), 1, GL_FALSE, glm::value_ptr(vpU));   // AIB1: motion vectors (ModelDraw::vel)
    glUniformMatrix4fv(U(progModel, "uPrevVP"), 1, GL_FALSE, glm::value_ptr(taaValid ? prevVP : vpU));
    drawModels(worldModels, &frMain);
    glEnable(GL_CULL_FACE);
  }
  // CP1: the dithered cut-out threshold (hashalpha) in a main-view depth prepass, so it cuts exactly where the colour pass does
  auto preHash = [&](std::initializer_list<GLuint> ps, bool on) { for (GLuint pg : ps) if (pg) {
    static const float hs = std::getenv("NMK_HASH_SPREAD") ? float(std::atof(std::getenv("NMK_HASH_SPREAD"))) : 0.2f;
    glProgramUniform1f(pg, U(pg, "uHashAlpha"), on && perf.hashAlpha && q.taa && !thumbMode ? hs : 0.0f); glProgramUniform1i(pg, U(pg, "uFrame"), frame); } };
  auto colourBufs = [&] { if (thumbMode) glNamedFramebufferDrawBuffer(worldFbo, GL_COLOR_ATTACHMENT0); else glNamedFramebufferDrawBuffers(worldFbo, 5, b5); };
  if (!folModels.empty() && perf.koFolModels) {   // NT8 model foliage (kofolmodels: the CP knock-out)
    setLighting(progFolModel, v);
    const bool pre = perf.vegPrepass && !thumbMode;
    if (pre) {   // CP1: depth first (alpha test only), then each covered pixel is shaded once at GL_EQUAL
      glNamedFramebufferDrawBuffer(worldFbo, GL_NONE); preHash({progFolDepth}, true);
      drawFolModels(true, vp, &frMain);
      preHash({progFolDepth}, false); colourBufs();
      glDepthFunc(GL_EQUAL); glDepthMask(GL_FALSE);
    }
    drawFolModels(false, vp, &frMain);
    if (pre) { glDepthFunc(zLess()); glDepthMask(GL_TRUE); }
  }
  if (!impDraws.empty() && !thumbMode) drawImpostors(v, vp, frMain);   // CP3
  gpuMark(GP_Trees);
  // tree canopies (double sided, alpha tested)
  if (!foliage.empty() && perf.koTrees) {
    glDisable(GL_CULL_FACE);
    selectLeafLods(v);
    for (GLuint lp : {progLeaf, progLeafI}) {
      setLighting(lp, v);
      glUniformMatrix4fv(U(lp, "uVPU"), 1, GL_FALSE, glm::value_ptr(vpU));
      glUniformMatrix4fv(U(lp, "uPrevVP"), 1, GL_FALSE, glm::value_ptr(taaValid ? prevVP : vpU));
      glUniform1f(U(lp, "uPrevTime"), holdTime ? time : time - v.dt);
    }
    if (perf.leafPrepass) {   // PF2: canopy depth first (alpha test only), then each covered pixel is shaded once at GL_EQUAL
      glNamedFramebufferDrawBuffer(worldFbo, GL_NONE); preHash({progLeafShadow, progLeafIShadow}, true);
      drawFoliage(progLeafShadow, vp, &frMain, true);
      preHash({progLeafShadow, progLeafIShadow}, false); colourBufs();
      glDepthFunc(GL_EQUAL); glDepthMask(GL_FALSE);
      drawFoliage(progLeaf, vp, &frMain, true);
      glDepthFunc(zLess()); glDepthMask(GL_TRUE);
    } else drawFoliage(progLeaf, vp, &frMain, true);
    glEnable(GL_CULL_FACE);
  }
  if (perf.terrainLast) drawTerrain();   // PF2: after solids / models / canopies, so the ground they hide is never shaded
  if (v.thermal && progHeat) {   // TH1: the warm bodies into the heat target, against the scene depth (no depth writes)
    // TH2: here, after the terrain (drawn last: it hid nothing before) and with MSAA its depth resolved first (the world
    // draws into msFbo: depthTex was last frame's / cleared, so walls hid nothing). Grass comes after: it never hides heat
    if (ms) glBlitNamedFramebuffer(msFbo, hdrFbo, 0, 0, width, height, 0, 0, width, height, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    if (!heatTex || heatW != width || heatH != height || heatDepthOf != depthTex) {
      if (heatTex) glDeleteTextures(1, &heatTex); if (heatFbo) glDeleteFramebuffers(1, &heatFbo);
      glCreateTextures(GL_TEXTURE_2D, 1, &heatTex); glTextureStorage2D(heatTex, 1, GL_R16F, width, height);
      glTextureParameteri(heatTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(heatTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glCreateFramebuffers(1, &heatFbo); glNamedFramebufferTexture(heatFbo, GL_COLOR_ATTACHMENT0, heatTex, 0); glNamedFramebufferTexture(heatFbo, GL_DEPTH_ATTACHMENT, depthTex, 0);
      heatW = width; heatH = height; heatDepthOf = depthTex;
    }
    GLint prevFbo = 0, df = GL_GREATER; glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo); glGetIntegerv(GL_DEPTH_FUNC, &df);
    const float zero[4] = {0, 0, 0, 0}; glClearNamedFramebufferfv(heatFbo, GL_COLOR, 0, zero);
    glBindFramebuffer(GL_FRAMEBUFFER, heatFbo);
    glDepthFunc(df == GL_GREATER ? GL_GEQUAL : df == GL_LESS ? GL_LEQUAL : (GLenum)df); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND); glBlendEquation(GL_MAX);
    glUseProgram(progHeat); glUniformMatrix4fv(U(progHeat, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
    const GLint lM = U(progHeat, "uModel"), lH = U(progHeat, "uHeat"), lO = U(progHeat, "uObj");
    glUniform3fv(U(progHeat, "uCam"), 1, glm::value_ptr(v.camPos));
    for (auto& h : heatDraws) {
      const GpuModel* m = h.md.model; if (!m || !m->vao || h.md.part < 0 || h.md.part >= (int)m->parts.size()) continue;
      const auto& pt = m->parts[h.md.part];
      glBindVertexArray(m->vao); glUniformMatrix4fv(lM, 1, GL_FALSE, glm::value_ptr(h.md.m)); glUniform1f(lH, h.heat); glUniform4fv(lO, 1, glm::value_ptr(h.obj));
      glDrawElements(GL_TRIANGLES, (GLsizei)pt.count, GL_UNSIGNED_INT, (void*)(uintptr_t)(pt.first * 4u));
    }
    glBlendEquation(GL_FUNC_ADD); glDisable(GL_BLEND); glDepthMask(GL_TRUE); glDepthFunc((GLenum)df);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
  }
  gpuMark(GP_Grass);
  // grass after opaque so buildings reject hidden blades early; double sided
  if (grass && !thumbMode && perf.koGrass) {
    glDisable(GL_CULL_FACE);
    auto grassUni = [&](GLuint pg) {   // the colour program and its CP1 depth twin take the same inputs
      setLighting(pg, v);
      glUniformMatrix4fv(U(pg, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
      glUniform1f(U(pg, "uPxAngle"), pxAngle);
      glUniformMatrix4fv(U(pg, "uVPU"), 1, GL_FALSE, glm::value_ptr(vpU));
      glUniformMatrix4fv(U(pg, "uPrevVP"), 1, GL_FALSE, glm::value_ptr(taaValid ? prevVP : vpU));
      glUniform1f(U(pg, "uPrevTime"), holdTime ? time : time - v.dt);
      glUniform1i(U(pg, "uGAOn"), perf.grassAtlas && grassAtlasReady && grassAtlasTier);   // GT1 grass plant atlas (off at Low)
      { int n = (int)std::min<size_t>(16, grassVeh.size() / 2); glUniform1i(U(pg, "uVehN"), n); if (n) glUniform4fv(U(pg, "uVeh"), 2 * n, glm::value_ptr(grassVeh[0])); }   // TK3
    };
    const bool pre = perf.grassPrepass && progGrassPre;
    if (pre) {   // CP1: blades' depth first (kGrassFS's own cut-outs), then each covered pixel is shaded once at GL_EQUAL
      glNamedFramebufferDrawBuffer(worldFbo, GL_NONE);
      grassUni(progGrassPre); preHash({progGrassPre}, true);
      grass->draw(progGrassPre, vp, v.camPos);
      colourBufs(); glDepthFunc(GL_EQUAL); glDepthMask(GL_FALSE);
    }
    grassUni(progGrass);
    grass->draw(progGrass, vp, v.camPos);
    if (pre) { glDepthFunc(zLess()); glDepthMask(GL_TRUE); }
    stats.draws += grass->lastDrawnChunks; stats.tris += (int)grass->lastDrawnBlades * 8;
    glEnable(GL_CULL_FACE);
  }
  gpuMark(GP_Nature);
  natDraws = 0; natTufts = 0;
  if (!natPatches.empty() && grass && !thumbMode && perf.koGrass) {   // flower / tall-grass patches (nature.cpp): same program, wind and fade
    glDisable(GL_CULL_FACE);
    setLighting(progGrass, v);
    glUniformMatrix4fv(U(progGrass, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
    glUniform1f(U(progGrass, "uPxAngle"), pxAngle);
    glUniformMatrix4fv(U(progGrass, "uVPU"), 1, GL_FALSE, glm::value_ptr(vpU));
    glUniformMatrix4fv(U(progGrass, "uPrevVP"), 1, GL_FALSE, glm::value_ptr(taaValid ? prevVP : vpU));
    glUniform1f(U(progGrass, "uPrevTime"), holdTime ? time : time - v.dt);
    // Low / Medium / High share of each patch and reach (the pre-GX3 grass radii: patches keep their own LOD)
    int gq = std::clamp((int)grass->quality, 0, 4);
    const float natFrac[5] = {0.0f, 0.6f, 0.8f, 1.0f, 1.0f}, natR[5] = {0.0f, 38.0f, 58.0f, 85.0f, 85.0f};   // G1 Ultra: patches as High
    if (natFrac[gq] > 0) natureDrawPatches(progGrass, natPatches, vp, v.camPos, natR[gq] * 1.1f, natFrac[gq], wind, natDraws, natTufts);
    stats.draws += natDraws; stats.tris += (int)natTufts * 30;
    glEnable(GL_CULL_FACE);
  }
  if (!thumbMode && perf.scatter) { extern void scatterRenderPass(Renderer&, const RenderView&, const mat4&); scatterRenderPass(*this, v, vp); }   // BF B2/B3: moss cushions, litter sheets
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
  if (!worldModelsGlass.empty()) {   // imported models' windows (cars): far to near, after the box glass
    std::vector<ModelDraw> mg = worldModelsGlass;
    std::sort(mg.begin(), mg.end(), [&](const ModelDraw& a, const ModelDraw& b) { return glm::length(vec3(a.m[3]) - v.camPos) > glm::length(vec3(b.m[3]) - v.camPos); });
    setLighting(progModel, v);
    glUniformMatrix4fv(U(progModel, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
    drawModels(mg, &frMain);
    glUseProgram(progModel); glUniform1i(U(progModel, "uGlass"), 0);
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
  if (vmOnly == 3) { float z[4] = {0, 0, 0, 0}; glClearTexImage(hdrColor, 0, GL_RGBA, GL_FLOAT, z); }   // VM2 self-test: the world black from here on (bloom, effects, the weapon over it)
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
    {   // W6: the scene's fog / sun on the particles (fxlit) and the soft fade against a copy of the depth (fxsoft)
      const bool lit = perf.fxLit && !q.legacy, soft = perf.fxSoft && !q.legacy && depthTex;
      if (lit) setLighting(progFx, v);
      glUseProgram(progFx); glUniform1i(U(progFx, "uFxLit"), lit ? 1 : 0); glUniform1i(U(progFx, "uSoft"), soft ? 1 : 0);
      if (soft) {
        if (!softDepth || softW != width || softH != height) {
          if (softDepth) glDeleteTextures(1, &softDepth);
          GLint fmt = GL_DEPTH_COMPONENT32F; glGetTextureLevelParameteriv(depthTex, 0, GL_TEXTURE_INTERNAL_FORMAT, &fmt);
          glCreateTextures(GL_TEXTURE_2D, 1, &softDepth); glTextureStorage2D(softDepth, 1, (GLenum)fmt, width, height); softW = width; softH = height;
          glTextureParameteri(softDepth, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTextureParameteri(softDepth, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        }
        glCopyImageSubData(depthTex, GL_TEXTURE_2D, 0, 0, 0, 0, softDepth, GL_TEXTURE_2D, 0, 0, 0, 0, width, height, 1);
        glBindTextureUnit(7, softDepth); glUniform1i(U(progFx, "uSoftDepth"), 7);   // unit 7: the post passes' depth unit, free here
        glUniform1f(U(progFx, "uP22"), v.proj[2][2]); glUniform1f(U(progFx, "uP32"), v.proj[3][2]);
      }
    }
    glBindVertexArray(fxVao);
    if (!fxAlpha.empty() && !v.thermal) {   // TH1: smoke and dust do not block a thermal view (fires and flashes, additive, stay)
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
  // W4 precipitation (one path, kinds by uKind; the perf switch "precip", its GPU pass "precip"): snow (the map's snowfall,
  // Light snow, Blizzard with spindrift), rain (Drizzle, Steady rain, Thunderstorm) with splashes, on any open-sky map
  gpuMark(GP_Precip);
  const bool wxSnow = wx.der.phys && wx.st.precip == WxSnow;   // W1: Light snow / Blizzard on any open-sky map (the map's own snowfall when it has one)
  const bool wxRainy = wx.der.phys && (wx.st.precip == WxRain || wx.st.precip == WxDrizzle);
  if (perf.precip && (lt.mapSnowfall || wxSnow || wxRainy) && !thumbMode && !v.thermal && !v.underwater && q.snowfallScale > 0.0f) {
    float gust = 0.5f + 0.5f * std::sin(time * 0.23f) * std::sin(time * 0.071f + 1.3f);   // slow gusts (shared wind feel; deterministic)
    static float lastT = -1.0f; const float dt = lastT < 0.0f ? 0.0f : std::clamp(time - lastT, 0.0f, 0.1f); lastT = time;
    // the wind each kind drifts on, integrated frame by frame (the gust changes the speed, never a particle's past path)
    const vec2 wWx = wx.der.windXZ * (0.6f + 0.8f * gust);                                          // the weather's wind (m/s)
    const vec2 wSnow = lt.mapSnowfall ? lt.sfWind * (wx.der.windK * (0.6f + 0.8f * gust)) : wWx;   // the map's snow drift, else the weather's
    static double accS[2] = {0, 0}, accR[2] = {0, 0};
    accS[0] = std::fmod(accS[0] + wSnow.x * dt, 7560.0); accS[1] = std::fmod(accS[1] + wSnow.y * dt, 7560.0);   // shortcut: the wrap jumps particles once per ~2 h of wind
    accR[0] = std::fmod(accR[0] + wWx.x * dt, 7560.0); accR[1] = std::fmod(accR[1] + wWx.y * dt, 7560.0);
    glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE); glEnable(GL_BLEND); glEnable(GL_DEPTH_TEST);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(progSnowfall);
    glUniformMatrix4fv(U(progSnowfall, "uVP"), 1, GL_FALSE, glm::value_ptr(vpU));
    glUniform3fv(U(progSnowfall, "uCamPos"), 1, glm::value_ptr(v.camPos));
    glUniform1f(U(progSnowfall, "uTime"), time);
    glUniform3f(U(progSnowfall, "uCamFwd"), -v.view[0][2], -v.view[1][2], -v.view[2][2]);
    glUniform1f(U(progSnowfall, "uPxAng"), pxAngle);
    glBindTextureUnit(1, occTex); glUniform1i(U(progSnowfall, "uOcc"), 1);
    glUniformMatrix4fv(U(progSnowfall, "uOccVP"), 1, GL_FALSE, glm::value_ptr(occVP));
    glUniform1f(U(progSnowfall, "uOccTop"), occTop); glUniform1f(U(progSnowfall, "uOccRange"), occRange); glUniform1i(U(progSnowfall, "uOccOn"), 1);
    glBindVertexArray(snowfallVao);
    auto draw = [&](int kind, int nearN, int farN, float nearBox, float farBox, float fall, vec2 wind, const double* acc, vec3 col, float size = 1.0f) {
      if (nearN + farN <= 0) return;
      glUniform1i(U(progSnowfall, "uKind"), kind);
      glUniform4f(U(progSnowfall, "uSF"), (float)nearN, nearBox, farBox, fall);
      glUniform4f(U(progSnowfall, "uSF2"), wind.x, wind.y, gust, camIndoor ? 1.0f : 0.0f);
      glUniform2f(U(progSnowfall, "uSFAcc"), (float)acc[0], (float)acc[1]);
      glUniform3fv(U(progSnowfall, "uPCol"), 1, glm::value_ptr(col)); glUniform1f(U(progSnowfall, "uPSize"), size);
      glDrawArraysInstanced(GL_TRIANGLES, 0, 6, nearN + farN);
    };
    const float qs = q.snowfallScale;   // Low 0.4, Medium 0.75, High 1 (the sky quality)
    if (lt.mapSnowfall || wxSnow) {     // SF1 snowfall (look.snowfall) / Light snow / Blizzard
      const bool blizzard = wxSnow && wx.st.precipMmH >= 3.0f;
      const float sfN = lt.mapSnowfall ? lt.sfNear : 0.0f, sfF = lt.mapSnowfall ? lt.sfFar : 0.0f;
      const float wN = wxSnow ? (blizzard ? 9000.0f : 60.0f) : 0.0f, wF = wxSnow ? (blizzard ? 16000.0f : 2500.0f) : 0.0f;   // a blizzard reads by count: ~2.6 flakes / m3 near
      const vec3 sc = vec3(0.92f, 0.94f, 0.98f) * (lt.mapSnowfall ? lt.sfBright : 1.0f);
      draw(0, (int)(std::max(sfN, wN) * qs), (int)(std::max(sfF, wF) * qs), 9.0f, 42.0f, lt.mapSnowfall ? lt.sfFall : 1.0f, wSnow, accS, sc, blizzard ? 2.2f : 1.0f);
      if (blizzard) draw(3, (int)(3000 * qs), 0, 12.0f, 12.0f, 1.0f, wSnow, accS, sc * 0.95f, 1.6f);   // spindrift over the ground
    }
    if (wxRainy) {   // rain: streaks, and splashes on whatever top surface is under them
      const float rate = wx.st.precipMmH, share = std::clamp(0.25f + rate / 12.0f, 0.25f, 1.2f);
      const float fall = wx.st.precip == WxDrizzle ? 3.5f : rate > 25.0f ? 8.5f : 7.5f;
      const vec3 rc = vec3(0.55f, 0.58f, 0.62f);
      draw(1, (int)(8000 * share * qs / 0.75f * 0.75f), (int)(8000 * share * qs), 9.0f, 30.0f, fall, wWx, accR, rc);
      const float splash = std::clamp(rate / 10.0f, 0.2f, 1.0f) * std::max(wx.st.puddle, 0.4f);
      if (wx.st.precip == WxRain) draw(2, (int)(1000 * splash * qs), 0, 12.0f, 12.0f, 0.0f, wWx, accR, rc * 1.2f);
    }
    glBindVertexArray(0);
  }
  if ((perf.leafDrift || perf.pollen) && q.leafDriftN > 0 && !thumbMode && !v.thermal && !v.underwater && progLeafDrift && snowfallVao) {   // WS2 drifting leaves, WS3 pollen
    // how wooded the eye's surroundings are: standing card trees (broadleaf, orchard) within 30 m, eased over ~2 s
    int nT = 0; for (auto& f : foliage) if (f.tree) { vec3 d = vec3(f.m[3]) - v.camPos; if (d.x * d.x + d.z * d.z < 900.0f) ++nT; }
    leafDriftK += (std::clamp((nT - 4) / 26.0f, 0.0f, 1.0f) - leafDriftK) * std::clamp(v.dt * 0.5f, 0.0f, 1.0f);
    const int n = perf.leafDrift && !lt.mapLeafless ? (int)(q.leafDriftN * leafDriftK) : 0;
    // WS3 pollen: dry, mild days in daylight, over standing grass (this frame's drawn clumps), Medium 600 / High 1500
    { extern bool snowLayerOn(); static float lastT = -999.0f; static int lastP = -1;
      if (perf.pollen && (wx.st.tempC != lastT || wx.st.precip != lastP)) { lastT = wx.st.tempC; lastP = wx.st.precip; logf("pollen gate: tempC %.1f, precip %d, leafless %d, snow layer %d", lastT, lastP, (int)lt.mapLeafless, (int)snowLayerOn()); }
      pollenOk = !lt.mapLeafless && !snowLayerOn(); }   // winter maps: no pollen, whatever the temperature says
    pollenK += ((perf.pollen && pollenOk && wx.st.precip == WxNone && wx.st.tempC > 5.0f && sunDir.y > 0.05f && grass ? std::clamp(grass->lastDrawnClumps / 3000.0f, 0.0f, 1.0f) : 0.0f) - pollenK) * std::clamp(v.dt * 0.5f, 0.0f, 1.0f);
    const int np = (int)(q.leafDriftN * 0.75f * pollenK);
    if (n > 0 || np > 0) {
      const GLboolean blendWas = glIsEnabled(GL_BLEND), cullWas = glIsEnabled(GL_CULL_FACE);
      glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE); glDisable(GL_BLEND); glEnable(GL_DEPTH_TEST);
      setLighting(progLeafDrift, v);
      glUseProgram(progLeafDrift);
      glUniformMatrix4fv(U(progLeafDrift, "uVP"), 1, GL_FALSE, glm::value_ptr(vpU));
      glUniform1f(U(progLeafDrift, "uTime"), time);
      glUniform4f(U(progLeafDrift, "uLD"), (float)n, 16.0f, float(windDrift.x), float(windDrift.y));
      glUniform4f(U(progLeafDrift, "uLD2"), camIndoor ? 1.0f : 0.0f, 0, 0, 0);
      glUniform4f(U(progLeafDrift, "uWindV"), windDir2().x, windDir2().y, float(windGustOff), float(windGustPrev));
      glUniform4f(U(progLeafDrift, "uWindP"), float(windClock), float(windClockPrev), windGustL(), windGustG());
      glBindTextureUnit(1, occTex); glUniform1i(U(progLeafDrift, "uOcc"), 1);
      glUniformMatrix4fv(U(progLeafDrift, "uOccVP"), 1, GL_FALSE, glm::value_ptr(occVP));
      glUniform1f(U(progLeafDrift, "uOccTop"), occTop); glUniform1f(U(progLeafDrift, "uOccRange"), occRange); glUniform1i(U(progLeafDrift, "uOccOn"), 1);
      glBindVertexArray(snowfallVao);
      if (n > 0) glDrawArraysInstanced(GL_TRIANGLES, 0, 6, n);
      if (np > 0) {
        glUniform4f(U(progLeafDrift, "uLD"), (float)np, 12.0f, float(windDrift.x), float(windDrift.y));
        glUniform4f(U(progLeafDrift, "uLD2"), camIndoor ? 1.0f : 0.0f, 1.0f, 0, 0);
        glUniform3fv(U(progLeafDrift, "uCamRight"), 1, glm::value_ptr(vec3(glm::inverse(v.view)[0])));
        glUniform3fv(U(progLeafDrift, "uCamUp"), 1, glm::value_ptr(vec3(glm::inverse(v.view)[1])));
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, np);
      }
      glBindVertexArray(0);
      if (cullWas) glEnable(GL_CULL_FACE); if (blendWas) glEnable(GL_BLEND);
    }
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
    glUniform1f(U(progEyepiece, "uDark"), v.reticle == 3 ? 0.006f : 0.7f);   // linear: gunner sight soft black; SC1 rifle scopes: the blurred view, a little darker
    glUniform1f(U(progEyepiece, "uShade"), 0.6f);
    glUniform1i(U(progEyepiece, "uReticle"), v.reticle); glUniform1i(U(progEyepiece, "uMode"), mode);
    glUniform1f(U(progEyepiece, "uPxMoa"), v.proj[1][1] * height * 0.5f * 2.908882e-4f);   // AT2: reticle parts sized in angle
    glUniform1f(U(progEyepiece, "uRetSize"), v.reticleSize); glUniform1f(U(progEyepiece, "uRetBright"), v.reticleBright);
    glUniform1i(U(progEyepiece, "uThermal"), v.thermal && mode == 0 && heatTex ? 1 : 0);
    if (v.thermal && mode == 0 && heatTex) {   // TH1: the heat target, the scene depth, the hot spots (fires, blasts, muzzle flashes) projected
      glBindTextureUnit(5, heatTex); glUniform1i(U(progEyepiece, "uHeat"), 5); glBindTextureUnit(6, depthTex); glUniform1i(U(progEyepiece, "uDepthT"), 6);
      static double clock = 0; clock = std::fmod(clock + v.dt, 1000.0); glUniform1f(U(progEyepiece, "uTime"), (float)clock);
      vec4 sp[16]; float sz[16]; int n = 0; const mat4 P = v.proj * v.view;
      std::sort(heatSpots.begin(), heatSpots.end(), [&](const vec4& a, const vec4& b) { return glm::length(vec3(a) - v.camPos) < glm::length(vec3(b) - v.camPos); });   // the nearest 16
      for (auto& h : heatSpots) {
        if (n >= 16) break;
        vec4 c = P * vec4(vec3(h), 1.0f); if (c.w <= 0.05f) continue;
        vec3 ndc = vec3(c) / c.w; float rad = std::fabs(h.w) * v.proj[1][1] / c.w * 0.5f;
        if (std::fabs(ndc.x) > 1.2f || std::fabs(ndc.y) > 1.2f) continue;
        sp[n] = vec4(ndc.x * 0.5f + 0.5f, ndc.y * 0.5f + 0.5f, rad, h.w > 0 ? 1.15f : 0.75f); sz[n] = ndc.z; ++n;
      }
      glUniform1i(U(progEyepiece, "uNSpots"), n);
      if (n) { glUniform4fv(U(progEyepiece, "uSpots"), n, glm::value_ptr(sp[0])); glUniform1fv(U(progEyepiece, "uSpotZ"), n, sz); }
    }
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
  if (viewModel.empty() && viewModelTex.empty()) { float z[4] = {0, 0, 0, 0}; glClearTexImage(vmDistTex, 0, GL_RED, GL_FLOAT, z); }   // no weapon (scoped, GM1b): no stale eye distances for DOF / probes
  if (!viewModel.empty() || !viewModelTex.empty()) {
    // First-person weapon (and arms): drawn over a cleared depth buffer so it never clips into walls, lit at its
    // world position, with its own fixed-FOV projection so the world FOV can zoom while the weapon keeps its size.
    glClear(GL_DEPTH_BUFFER_BIT);
    GLenum bufs[5] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_NONE, GL_NONE, GL_COLOR_ATTACHMENT4};   // + motion: the weapon has its own projection, so the depth reprojection is wrong for it (shimmer)
    glNamedFramebufferDrawBuffers(hdrFbo, 5, bufs);
    float zero[4] = {0, 0, 0, 0}; glClearNamedFramebufferfv(hdrFbo, GL_COLOR, 1, zero);
    mat4 vvp = (v.vmProj[2][3] != 0.0f ? v.vmProj : v.proj) * v.view;
    glEnable(GL_CULL_FACE);
    setLighting(progMain, v);
    glUniform1i(U(progMain, "uWeather"), 0);
    glUniformMatrix4fv(U(progMain, "uVP"), 1, GL_FALSE, glm::value_ptr(vvp));
    glUniform1f(U(progMain, "uVmVel"), 1.0f);
    drawItems(progMain, viewModel, false);
    glUniform1f(U(progMain, "uVmVel"), 0.0f);
    if (!viewModelTex.empty()) {
      glDisable(GL_CULL_FACE);
      setLighting(progModel, v);
      glUniform1f(U(progModel, "uVmVel"), 1.0f);
      glUniformMatrix4fv(U(progModel, "uVP"), 1, GL_FALSE, glm::value_ptr(vvp));
      {   // AT6: the fitted optic's window, in this pass's pixels
        GLint vpt[4]; glGetIntegerv(GL_VIEWPORT, vpt);
        glUniform4f(U(progModel, "uVmClip"), vpt[0] + v.vmClip.x * vpt[2], vpt[1] + (1 - v.vmClip.y) * vpt[3], v.vmClip.z * vpt[3], 0);
      }
      bool fpsArmsIsModel(const GpuModel* g);
      auto vmArmMaterial = [](std::string n) { for (auto& c : n) c = (char)std::tolower((unsigned char)c); for (const char* k : {"arms", "hand", "glove", "sleeve", "cloth", "body", "manny"}) if (n.find(k) != std::string::npos) return true; return false; };   // the rigs' arm, glove and sleeve materials
      if (vmOnly) {   // VM2 self-test: the arms alone (1) or the gun alone (2)
        std::vector<ModelDraw> keep; for (auto& d : viewModelTex) { const auto& pt = d.model->parts[d.part]; bool arm = fpsArmsIsModel(d.model) || (pt.mat >= 0 && pt.mat < (int)d.model->mats.size() && vmArmMaterial(d.model->mats[pt.mat].name)); if (arm == (vmOnly == 1)) keep.push_back(d); }
        drawModels(keep);
      } else drawModels(viewModelTex);
      glUniform1f(U(progModel, "uVmVel"), 0.0f);
      glUniform4f(U(progModel, "uVmClip"), 0, 0, 0, 0);   // AT6: off for every other pass
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
  glUniform1f(U(progExpo, "uExpoFloor"), flashOn ? flashExpoFloor : 0.0f);   // FX1
  glUniform1i(U(progExpo, "uHist"), q.lightNew && perf.histExpo && !q.legacy ? 1 : 0); glUniform1f(U(progExpo, "uHistCal"), q.histCal);   // VO L8
  glBindVertexArray(emptyVao); glDrawArrays(GL_TRIANGLES, 0, 3);
  expoIdx ^= 1;

  // VO L8: the colour grade (and the weather / map / underwater colour steps after the tone curve) baked into a 32^3 LUT
  const bool gradeOn = grade.on && perf.grade && !q.legacy && !thumbMode && !lt.mapClean;   // BR2: a clean map keeps its own colours (the cool-shadow grade turned yellow olive)
  auto gradeU = [&](GLuint p) {
    glUniform1i(U(p, "uGradeOn"), gradeOn ? 1 : 0);
    glUniform1i(U(p, "uPhysGrade"), perf.physGrade && q.lightPhys && q.lightNew && !q.legacy && !thumbMode ? 1 : 0);   // CP2
    glUniform1f(U(p, "uWxK"), thumbMode ? 0.0f : wx.k); glUniform1f(U(p, "uWxDark"), wx.der.phys ? 0.0f : 1.0f);
    glUniform1f(U(p, "uMapSat"), thumbMode ? 1.0f : lt.mapSat); glUniform3fv(U(p, "uMapTint"), 1, glm::value_ptr(thumbMode ? vec3(1.0f) : lt.mapTint));
    glUniform1f(U(p, "uNightGamma"), 1.0f / (1.0f + 0.3f * lightingTod(lt.hour).nightK) / settings().gamma());   // x the Settings gamma slider   // night: gamma +30 % (user request)
    glUniform1f(U(p, "uMapGamma"), std::max(lt.mapGamma, 0.1f));   // BR1c: the map's own gamma (extra.lighting.gamma)
    glUniform1f(U(p, "uGradeSat"), grade.saturation); glUniform1f(U(p, "uGradeContrast"), grade.contrast);
    glUniform3fv(U(p, "uGradeShadow"), 1, glm::value_ptr(grade.shadowTint)); glUniform3fv(U(p, "uGradeHigh"), 1, glm::value_ptr(grade.highlightTint));
    glUniform1f(U(p, "uUnderTone"), v.underwater ? 1.0f : 0.0f);   // swim.cpp
  };
  const bool newTone = q.lightNew && perf.agx && !q.legacy && !thumbMode;
  if (newTone) {
    if (!gradeLutTex) {
      glCreateTextures(GL_TEXTURE_3D, 1, &gradeLutTex); glTextureStorage3D(gradeLutTex, 1, GL_RGBA16F, 32, 32, 32);
      for (GLenum pn : {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_WRAP_R}) glTextureParameteri(gradeLutTex, pn, GL_CLAMP_TO_EDGE);
      glTextureParameteri(gradeLutTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(gradeLutTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glCreateFramebuffers(1, &gradeLutFbo);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, gradeLutFbo); glViewport(0, 0, 32, 32);
    glUseProgram(progGradeLut); gradeU(progGradeLut);
    for (int z = 0; z < 32; ++z) { glNamedFramebufferTextureLayer(gradeLutFbo, GL_COLOR_ATTACHMENT0, gradeLutTex, 0, z); glUniform1f(U(progGradeLut, "uSlice"), (float)z); glDrawArrays(GL_TRIANGLES, 0, 3); }
  }
  // ---- tonemap into LDR (shares depth for overlays)
  glBindFramebuffer(GL_FRAMEBUFFER, ldrFbo); glInvalidateNamedFramebufferData(ldrFbo, 1, kInvC0);   // the tonemap writes every pixel (depth is shared: kept)
  glViewport(0, 0, width, height);
  if (ldProbe.want) {   // CP0 look-dev readout: scene-linear HDR at world points, and the exposure the tone pass uses
    ldProbe.want = false; ldProbe.hdr.clear(); ldProbe.px.clear();
    float fe = lt.mapExposure > 0 && !thumbMode ? lightingFixedExposure(*this) * lt.mapExposure : v.autoExposure ? -1.0f : lightingFixedExposure(*this);
    ldProbe.expo = fe > 0 ? fe * wx.der.fixedMul * (q.lightNew && !q.legacy && !thumbMode ? q.newExpo : 1.0f) : exposure();
    for (vec3 p : ldProbe.pts) {
      vec4 c = vpU * vec4(p, 1.0f); vec3 r(-1.0f); int x = -1, y = -1;
      if (c.w > 0.01f) {
        x = (int)((c.x / c.w * 0.5f + 0.5f) * width); y = (int)((c.y / c.w * 0.5f + 0.5f) * height);
        if (x >= 2 && y >= 2 && x < width - 2 && y < height - 2) {
          float px[25 * 4]; glGetTextureSubImage(hdrColor, 0, x - 2, y - 2, 0, 5, 5, 1, GL_RGBA, GL_FLOAT, sizeof px, px);
          r = vec3(0.0f); for (int k = 0; k < 25; ++k) r += vec3(px[k * 4], px[k * 4 + 1], px[k * 4 + 2]) / 25.0f;
        }
      }
      ldProbe.hdr.push_back(r); ldProbe.px.push_back({x, height - 1 - y});
    }
  }
  glUseProgram(progTone);
  glBindTextureUnit(0, hdrColor); glBindTextureUnit(1, expoTex[expoIdx]);
  glUniform1i(U(progTone, "uDbgView"), dbgView);
  glUniform1f(U(progTone, "uCas"), perf.cas && !q.legacy && !thumbMode ? q.sharpness : 0.0f); glUniform1i(U(progTone, "uDither"), perf.dither && !thumbMode ? 1 : 0);   // CP1
  glUniform1i(U(progTone, "uHdr"), 0); glUniform1i(U(progTone, "uExpo"), 1);
  glUniform1f(U(progTone, "uNewExpo"), q.lightNew && !q.legacy && !thumbMode ? q.newExpo : 1.0f);   // VO L1
  { float fe = lt.mapExposure > 0 && !thumbMode ? lightingFixedExposure(*this) * lt.mapExposure : v.autoExposure ? -1.0f : lightingFixedExposure(*this); if (camIndoor && perf.indoorExpo && !thumbMode && !q.legacy) fe = -1.0f;   /* CP4 indoorexpo: indoors always metered (editor stills too) */ if (q.lightPhys && q.lightNew && !q.legacy && !thumbMode && !camIndoor && physExpo > 0.0f) fe = physExpo; else if (fe > 0 && !thumbMode) fe *= wx.der.fixedMul; glUniform1f(U(progTone, "uFixedExpo"), fe); }   // CP2: Physical exposes by EV100 outdoors (indoors the meter)   // W1: a preset moves a fixed exposure by two thirds of its light change   // 1 by Day; BR3: a map may pin it (play = editor)
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
  glBindTextureUnit(3, vmDistTex); glUniform1i(U(progTone, "uVmDist"), 3);   // VM2: no world bloom on the first-person weapon
  glUniform1f(U(progTone, "uMicro"), thumbMode || q.legacy || eyepiece ? 0.0f : q.microcontrast);   // UI2 (the eyepiece view has no fresh mips)
  glUniform1f(U(progTone, "uBloomAmt"), !q.legacy && q.bloom ? std::min(1.0f, q.bloomStrength * lt.mapBloom) : 0.0f);   // BR3: x the map's bloom
  glUniform1f(U(progTone, "uRectVig"), v.rectVignette && !thumbMode ? 1.0f : 0.0f);   // TK3
  glUniform1f(U(progTone, "uEyeR"), eyepiece ? v.eyepieceR : 0.0f); glUniform1f(U(progTone, "uAspect"), (float)width / std::max(1, height));
  {   // GX5 grade (content/settings/grade.json)
    gradeU(progTone);
    glUniform1f(U(progTone, "uGradeVig"), grade.vignette); glUniform1f(U(progTone, "uGradeCA"), grade.chromatic);
    glUniform1f(U(progTone, "uGradeSharpen"), q.taa && !(perf.cas && !q.legacy) ? grade.sharpen : 0.0f);
    glUniform1i(U(progTone, "uNewTone"), newTone ? (std::getenv("NMK_VO_NOLUT") ? 2 : 1) : 0); glUniform1f(U(progTone, "uAgxK"), q.agxK); glUniform1f(U(progTone, "uAgxSat"), q.agxSat);   // VO L8
    glBindTextureUnit(4, gradeLutTex); glUniform1i(U(progTone, "uGradeLut"), 4);
  }
  glDrawArrays(GL_TRIANGLES, 0, 3);

  // ---- editor overlays (separate pass); in Explore only the game's ground markings (gWorldMarks)
  const Overlay& ovD = v.editor ? ov : gWorldMarks;
  if ((v.editor || !thumbMode) && (!ovD.lines.empty() || !ovD.tris.empty() || !ovD.linesTop.empty())) {
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
    drawV(ovD.tris, GL_TRIANGLES, true);
    drawV(ovD.lines, GL_LINES, true);
    drawV(ovD.linesTop, GL_LINES, false);
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
  if (upActive) swapUp();   // P1: back to the internal set for the next frame (upActive stays: readPixels reads the output set)
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE); glClearDepth(1.0); glDepthFunc(GL_LESS);   // back to standard GL
  thermalWanted = v.thermal; heatDraws.clear(); heatSpots.clear();   // TH1: the game collects heat next frame only while a thermal view is up
  water.clear();
  lightingEnd(*this);
  gpuMark(GP_UI);   // ends at the app's GP_Count mark after ImGui
  if (elapsedQ) { glEndQuery(GL_TIME_ELAPSED); qUsed[qidx] = true; qidx = (qidx + 1) % 3; qvalid = true; } else qvalid = false;
  stats.cpuMs = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - c0).count();
}

void Renderer::worldComposite(const RenderView& v, const mat4& vp) {
  bool shafts = (q.shafts || (lt.mapShafts > 0 && q.bloom)) && perf.shafts && q.shaftStrength > 0 && !lt.mapNoFog && wx.k < 0.99f && sunShK > 0.0f,   /* NT8: a map's own shafts from Medium up */   /* WX2: no sun shafts under overcast */ ao = q.ssao && q.ssaoStrength > 0, contact = q.lightNew && perf.contactShadow && !q.legacy, ssr = q.ssr && q.ssrStrength > 0;
  if (!shafts && !ao && !ssr && !contact) return;
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
    { bool gt = q.lightNew && perf.gtao && !q.legacy;   // VO L2: GTAO, 2 x 3 steps a side at quarter resolution (Medium), 3 x 4 at half (High)
      glUniform1i(U(progSsao, "uGtao"), gt ? 1 : 0); glUniform1i(U(progSsao, "uSlices"), aoDiv <= 2 ? 3 : 2); glUniform1i(U(progSsao, "uSteps"), aoDiv <= 2 ? 4 : 3); }
    glUniform2f(U(progSsao, "uTexel"), (float)aoDiv / width, (float)aoDiv / height);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (perf.contactAO && progContactAO && !contactAO.empty() && !thumbMode) {   // BF B4: multiply the contact decals into the AO
      const size_t bytes = contactAO.size() * sizeof(vec4);
      if (!contactAOVao) {
        glCreateVertexArrays(1, &contactAOVao);
        for (int k = 0; k < 3; ++k) { glEnableVertexArrayAttrib(contactAOVao, k); glVertexArrayAttribFormat(contactAOVao, k, 4, GL_FLOAT, GL_FALSE, k * 16); glVertexArrayAttribBinding(contactAOVao, k, 0); }
        glVertexArrayBindingDivisor(contactAOVao, 0, 1);
      }
      if (bytes > contactAOBytes) { if (contactAOBuf) glDeleteBuffers(1, &contactAOBuf); glCreateBuffers(1, &contactAOBuf); contactAOBytes = std::max(bytes, size_t(48 * 256)); glNamedBufferStorage(contactAOBuf, contactAOBytes, nullptr, GL_DYNAMIC_STORAGE_BIT); glVertexArrayVertexBuffer(contactAOVao, 0, contactAOBuf, 0, 48); }
      glNamedBufferSubData(contactAOBuf, 0, bytes, contactAO.data());
      glUseProgram(progContactAO);
      glUniformMatrix4fv(U(progContactAO, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
      glUniformMatrix4fv(U(progContactAO, "uInvVP"), 1, GL_FALSE, glm::value_ptr(inv));
      glUniform2f(U(progContactAO, "uInvSize"), 1.0f / std::max(1, width / aoDiv), 1.0f / std::max(1, height / aoDiv));
      glUniform3fv(U(progContactAO, "uEye"), 1, glm::value_ptr(v.camPos));
      glBindTextureUnit(7, depthTex); glUniform1i(U(progContactAO, "uDepth"), 7);
      glBindTextureUnit(12, nrTex); glUniform1i(U(progContactAO, "uNR"), 12);
      glEnable(GL_BLEND); glBlendFunc(GL_ZERO, GL_SRC_COLOR); glColorMaski(0, GL_TRUE, GL_FALSE, GL_FALSE, GL_FALSE);
      glBindVertexArray(contactAOVao); glDrawArraysInstanced(GL_TRIANGLES, 0, 6, GLsizei(contactAO.size() / 3));
      glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); glDisable(GL_BLEND); glBindVertexArray(emptyVao);
    }
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
  glUniform1f(U(progShaft, "uDensity"), (wx.der.phys ? wx.der.sigma : q.hazeDensity) * 1.6f);
  glUniform1i(U(progShaft, "uShaftN"), wx.quality == 0 ? 8 : 14);   // WX1: the cheaper variant at Low cloud quality
  glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  // composite into hdrColor from a copy of it
  glCopyImageSubData(hdrColor, GL_TEXTURE_2D, 0, 0, 0, 0, sceneCopy, GL_TEXTURE_2D, 0, 0, 0, 0, width, height, 1);
  froxRun(v);   // CP4 froxel fog (before the composite that applies it)
  const bool ssrHalf = ssr && perf.ssrHalf && progSsrHalf && ssrTex;
  if (ssrHalf) {   // OPT ssrhalf: the reflection march at a quarter of the pixels; the composite upsamples it by depth
    glBindFramebuffer(GL_FRAMEBUFFER, ssrFbo); glViewport(0, 0, std::max(1, width / 2), std::max(1, height / 2));
    glUseProgram(progSsrHalf);
    glBindTextureUnit(8, sceneCopy); glBindTextureUnit(7, depthTex);
    glUniform1i(U(progSsrHalf, "uScene"), 8); glUniform1i(U(progSsrHalf, "uDepth"), 7);
    setLighting(progSsrHalf, v); glUseProgram(progSsrHalf);
    glBindTextureUnit(12, nrTex); glUniform1i(U(progSsrHalf, "uNR"), 12);
    glUniform1f(U(progSsrHalf, "uSsrAmt"), q.ssrStrength); glUniform1i(U(progSsrHalf, "uSsrEnvCube"), 1); glUniform1i(U(progSsrHalf, "uSsrClamp"), perf.ssrClamp ? 1 : 0); glUniform1f(U(progSsrHalf, "uSsrMaxT"), perf.ssrRange ? 0.75f * wx.der.visM : 1e9f); glUniform2f(U(progSsrHalf, "uSsrRough"), perf.ssrFade ? 0.3f : 0.1f, perf.ssrFade ? 0.5f : 0.4f);
    glUniformMatrix4fv(U(progSsrHalf, "uProj"), 1, GL_FALSE, glm::value_ptr(v.proj));
    glUniformMatrix4fv(U(progSsrHalf, "uView"), 1, GL_FALSE, glm::value_ptr(v.view));
    glUniformMatrix4fv(U(progSsrHalf, "uInvProj"), 1, GL_FALSE, glm::value_ptr(invP));
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, compFbo); glInvalidateNamedFramebufferData(compFbo, 1, kInvC0);   // rebuilt from sceneCopy
  glViewport(0, 0, width, height);
  glUseProgram(progComposite);
  glBindTextureUnit(8, sceneCopy); glBindTextureUnit(9, shaftTex); glBindTextureUnit(10, ambTex); glBindTextureUnit(11, aoTex); glBindTextureUnit(7, depthTex);
  glUniform1i(U(progComposite, "uScene"), 8); glUniform1i(U(progComposite, "uShafts"), 9);
  glUniform1i(U(progComposite, "uAmb"), 10); glUniform1i(U(progComposite, "uAO"), 11); glUniform1i(U(progComposite, "uDepth"), 7);
  glUniform1f(U(progComposite, "uShaftAmt"), shafts ? q.shaftStrength * (lt.mapShafts > 0 ? lt.mapShafts : 1.0f) * (1.0f - 0.75f * lightingTod(lt.hour).nightK) * (1.0f - wx.k) : 0.0f);   // moon shafts at a quarter strength: the night exposure made them glare
  glUniform1f(U(progComposite, "uAoAmt"), ao ? q.ssaoStrength : 0.0f);
  glUniform1f(U(progComposite, "uContactAmt"), contact ? 0.85f * (1.0f - lightingTod(lt.hour).nightK) * (1.0f - wx.k) : 0.0f);   // VO L1: the sun's (none from the moon or an overcast sky)
  glUniform1i(U(progComposite, "uAoNew"), q.lightNew && perf.gtao && !q.legacy ? 1 : 0);
  glUniform1i(U(progComposite, "uBent"), q.lightNew && perf.gtao && perf.bentNormal && !q.legacy ? 1 : 0);
  glUniform1i(U(progComposite, "uContactSteps"), shadowTier >= 2 ? 16 : shadowTier == 1 ? 12 : 8);
  setLighting(progComposite, v);   // sky model + noise for the reflection estimate
  glUseProgram(progComposite);
  glBindTextureUnit(12, nrTex); glUniform1i(U(progComposite, "uNR"), 12);
  glUniform1i(U(progComposite, "uSsrHalf"), ssrHalf ? 1 : 0); glBindTextureUnit(3, ssrHalf ? ssrTex : 0); glUniform1i(U(progComposite, "uSsrTex"), 3);   // unit 3: model material maps, rebound per draw
  glUniform2f(U(progComposite, "uSsrTexel"), 2.0f / width, 2.0f / height);
  glUniform1f(U(progComposite, "uSsrAmt"), ssr ? q.ssrStrength : 0.0f);
  glUniform1i(U(progComposite, "uFroxOn"), froxOn ? 1 : 0); glBindTextureUnit(4, froxOn ? froxInt : 0); glUniform1i(U(progComposite, "uFrox"), 4);   // unit 4: model AO maps, rebound per draw
  glUniform1f(U(progComposite, "uFroxNear"), froxNear); glUniform1f(U(progComposite, "uFroxFar"), froxFar); glUniform1f(U(progComposite, "uFroxHazeD"), hazeSigma());
  glUniform1f(U(progComposite, "uFroxHazeH"), wx.der.phys && wx.der.visM < 3000.0f ? std::max(lt.mapHazeH > 0.0f ? lt.mapHazeH : q.hazeHeight, 600.0f) : lt.mapHazeH > 0.0f ? lt.mapHazeH : q.hazeHeight); glUniform3fv(U(progComposite, "uFroxAmb"), 1, glm::value_ptr(vec3(fogNow)));
  {   // W5 wxshimmer gate (terrain session's spec): arid map, sun > 25 deg, cover < 0.3, wind < 4.5 m/s (fading from 3), dry, no dust storm
    extern bool groundPaintArid();
    float g = perf.wxShimmer && !thumbMode && !q.legacy && groundPaintArid() && sunDir.y > 0.4226f && wx.st.cover < 0.3f && wx.st.precip == WxNone && wx.st.dustVisM <= 0.0f ? 1.0f : 0.0f;
    g *= 1.0f - glm::smoothstep(3.0f, 4.5f, wx.st.windMs);
    g *= 0.5f + 0.5f * glm::clamp((wx.st.tempC - 20.0f) / 15.0f, 0.0f, 1.0f);
    const float pitch = std::asin(glm::clamp(-v.view[1][2], -1.0f, 1.0f));   // camera forward's y (view row 2 negated)
    g *= std::max(0.0f, 1.0f - std::fabs(glm::degrees(pitch)) / 20.0f);
    const float mag = std::max(1.0f, v.proj[1][1] / 1.43f);   // a scope's zoom (narrower than the ~70 deg view) scales the wobble
    shimmerPx = 2.0f * (height / 1080.0f) * g * mag;
  } glUniform1i(U(progComposite, "uSsrClamp"), perf.ssrClamp ? 1 : 0); glUniform1f(U(progComposite, "uSsrMaxT"), perf.ssrRange ? 0.75f * wx.der.visM : 1e9f); glUniform2f(U(progComposite, "uSsrRough"), perf.ssrFade ? 0.3f : 0.1f, perf.ssrFade ? 0.5f : 0.4f);   // OPT ssrrange: no march past 0.75 x visibility
  glUniformMatrix4fv(U(progComposite, "uProj"), 1, GL_FALSE, glm::value_ptr(v.proj));
  glUniformMatrix4fv(U(progComposite, "uView"), 1, GL_FALSE, glm::value_ptr(v.view));
  glUniformMatrix4fv(U(progComposite, "uInvProj"), 1, GL_FALSE, glm::value_ptr(invP));
  glUniform2f(U(progComposite, "uAoTexel"), (float)aoDiv / width, (float)aoDiv / height);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindFramebuffer(GL_FRAMEBUFFER, hdrFbo);
  glViewport(0, 0, width, height);
  glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
}

// TAAU: the nearest internal depth into the output-size depth (effects, overlays); depth only, colour untouched
void Renderer::upDepth() {
  glBindFramebuffer(GL_FRAMEBUFFER, up.hdrFbo); glNamedFramebufferDrawBuffer(up.hdrFbo, GL_NONE); glViewport(0, 0, up.w, up.h);
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_ALWAYS); glDepthMask(GL_TRUE);
  glUseProgram(progDepthUp); glBindTextureUnit(7, depthTex);
  glUniform1i(U(progDepthUp, "uDepth"), 7); glUniform2f(U(progDepthUp, "uInSize"), (float)width, (float)height);
  glBindVertexArray(emptyVao); glDrawArrays(GL_TRIANGLES, 0, 3);
  glNamedFramebufferDrawBuffer(up.hdrFbo, GL_COLOR_ATTACHMENT0);
}

void Renderer::taaResolve(const mat4& vpJ, const mat4& vpU, const RenderView& v) {
  glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
  glBindVertexArray(emptyVao);
  glCopyImageSubData(hdrColor, GL_TEXTURE_2D, 0, 0, 0, 0, sceneCopy, GL_TEXTURE_2D, 0, 0, 0, 0, width, height, 1);
  int cur = histIdx ^ 1;
  if (histUp) {   // P1 TAAU: reconstruct at output size into the output-size set, then continue the frame there
    if (up.w != displayW || up.h != displayH) { createUpTargets(); taaValid = false; }
    const bool hook = temporalUpscaler && hookOn;
    if (hook != hookWas) { taaValid = false; hookWas = hook; }   // the GL history and the hook's do not carry over
    if (hook) {   // full motion for the hook (input pixels toward the previous position)
      if (motionW != width || motionH != height) {
        if (motionTex) { glDeleteTextures(1, &motionTex); glDeleteFramebuffers(1, &motionFbo); }
        glCreateTextures(GL_TEXTURE_2D, 1, &motionTex); glTextureStorage2D(motionTex, 1, GL_RG16F, width, height);
        glTextureParameteri(motionTex, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTextureParameteri(motionTex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glCreateFramebuffers(1, &motionFbo); glNamedFramebufferTexture(motionFbo, GL_COLOR_ATTACHMENT0, motionTex, 0);
        motionW = width; motionH = height;
      }
      glBindFramebuffer(GL_FRAMEBUFFER, motionFbo); glViewport(0, 0, width, height);
      glUseProgram(progMotion);
      glBindTextureUnit(7, depthTex); glBindTextureUnit(10, velTex);
      glUniform1i(U(progMotion, "uDepth"), 7); glUniform1i(U(progMotion, "uVel"), 10);
      glUniformMatrix4fv(U(progMotion, "uInvVP"), 1, GL_FALSE, glm::value_ptr(glm::inverse(vpU)));
      glUniformMatrix4fv(U(progMotion, "uPrevVP"), 1, GL_FALSE, glm::value_ptr(taaValid ? prevVP : vpU));
      glUniform2f(U(progMotion, "uSize"), (float)width, (float)height); glUniform1f(U(progMotion, "uSignY"), hookSign.y);
      glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    if (hook && temporalUpscaler({sceneCopy, depthTex, motionTex, up.hdrColor, up.depthTex, expoTex[expoIdx], width, height, up.w, up.h, jitterPx * hookSign.x,
                                  glm::inverse(vpU), prevVP, !taaValid})) {
      upDepth();
      taaValid = true; prevVP = vpU; prevCam = v.camPos;
      swapUp(); upActive = true;
      glBindFramebuffer(GL_FRAMEBUFFER, hdrFbo); glViewport(0, 0, width, height);
      glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDepthFunc(zLess());
      return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, up.taaFbo[cur]);
    glViewport(0, 0, up.w, up.h);
  } else {
    glBindFramebuffer(GL_FRAMEBUFFER, taaFbo[cur]);
    { const GLenum b2[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1}; glInvalidateNamedFramebufferData(taaFbo[cur], 2, b2); }   // colour (from sceneCopy) and the new history
    glViewport(0, 0, width, height);
  }
  glUseProgram(progTaa);
  glUniform1i(U(progTaa, "uUp"), histUp ? 1 : 0);
  { static const int dbg = std::getenv("NMK_TAA_DEBUG") ? std::atoi(std::getenv("NMK_TAA_DEBUG")) : 0; glUniform1i(U(progTaa, "uTaaDbg"), dbg); }
  glUniform2f(U(progTaa, "uInTexel"), 1.0f / width, 1.0f / height);
  glUniform2f(U(progTaa, "uJit"), jitterPx.x, jitterPx.y);
  glBindTextureUnit(8, sceneCopy); glBindTextureUnit(9, histUp ? up.hist[histIdx] : histTex[histIdx]); glBindTextureUnit(7, depthTex);
  glUniform1i(U(progTaa, "uCur"), 8); glUniform1i(U(progTaa, "uHist"), 9); glUniform1i(U(progTaa, "uDepth"), 7);
  glBindTextureUnit(10, velTex); glUniform1i(U(progTaa, "uVel"), 10);
  // reproject with the *unjittered* matrices: a still camera then fetches history at exactly the same pixel (the
  // jittered inverse would resample history half a pixel off every frame and accumulate blur)
  (void)vpJ; glUniformMatrix4fv(U(progTaa, "uInvVP"), 1, GL_FALSE, glm::value_ptr(glm::inverse(vpU)));
  glUniformMatrix4fv(U(progTaa, "uPrevVP"), 1, GL_FALSE, glm::value_ptr(prevVP));
  glUniform2f(U(progTaa, "uTexel"), histUp ? 1.0f / up.w : 1.0f / width, histUp ? 1.0f / up.h : 1.0f / height);   // output (history) texel
  glUniform1i(U(progTaa, "uValid"), taaValid ? 1 : 0);
  glUniform1f(U(progTaa, "uSharpen"), perf.cas && !q.legacy ? 0.0f : histUp ? 0.5f * q.taaSharpen : q.taaSharpen);   // CP1 cas: the one sharpener is in the tone pass
  glUniform1i(U(progTaa, "uTaaVel"), perf.taaVel ? 1 : 0); glUniform1i(U(progTaa, "uTaaBox"), perf.taaBox ? 1 : 0);   // TAAU: its blur reference is the coarser internal neighbourhood
  glDrawArrays(GL_TRIANGLES, 0, 3);
  histIdx = cur; taaValid = true;
  prevVP = vpU; prevCam = v.camPos;
  if (histUp) { upDepth(); glDepthFunc(zLess()); swapUp(); upActive = true; }
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
    glUniform1i(U(progBloomDown, "uDown13"), q.lightNew && perf.agx && !q.legacy ? 1 : 0);   // VO L8
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
  w = upActive ? up.w : width; h = upActive ? up.h : height; rgba.resize((size_t)w * h * 4);   // P1: the frame's final (output-size) image
  glBindFramebuffer(GL_READ_FRAMEBUFFER, upActive ? up.ldrFbo : ldrFbo);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  return true;
}

bool Renderer::readVmDist(std::vector<float>& d, int& w, int& h) {
  w = upActive ? up.w : width; h = upActive ? up.h : height; d.assign((size_t)w * h, 0.0f);
  GLuint t = upActive ? up.vmDistTex : vmDistTex;
  if (!t) return false;
  glGetTextureImage(t, 0, GL_RED, GL_FLOAT, (GLsizei)(d.size() * sizeof(float)), d.data());
  return true;
}

// P1 TAAU output-size post targets (formats as createTargets; the history ping-pong at output size)
void Renderer::createUpTargets() {
  if (up.hdrFbo) {
    GLuint f[5] = {up.hdrFbo, up.dofFbo, up.ldrFbo, up.taaFbo[0], up.taaFbo[1]}; glDeleteFramebuffers(5, f);
    GLuint t[7] = {up.hdrColor, up.depthTex, up.vmDistTex, up.dofTex, up.ldrColor, up.hist[0], up.hist[1]}; glDeleteTextures(7, t);
  }
  int w = up.w = displayW, h = up.h = displayH;
  auto tex = [&](GLenum fmt, int lv, GLenum filt) {
    GLuint t; glCreateTextures(GL_TEXTURE_2D, 1, &t); glTextureStorage2D(t, lv, fmt, w, h);
    glTextureParameteri(t, GL_TEXTURE_MIN_FILTER, filt); glTextureParameteri(t, GL_TEXTURE_MAG_FILTER, filt == GL_NEAREST ? GL_NEAREST : GL_LINEAR);
    glTextureParameteri(t, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(t, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
  };
  up.hdrColor = tex(GL_RGBA16F, 1 + (int)std::floor(std::log2((float)std::max(w, h))), GL_LINEAR_MIPMAP_LINEAR);   // exposure / eyepiece read its mips
  up.depthTex = tex(GL_DEPTH_COMPONENT32F, 1, GL_NEAREST);
  up.vmDistTex = tex(GL_R16F, 1, GL_NEAREST);
  up.dofTex = tex(GL_RGBA16F, 1, GL_LINEAR);
  up.ldrColor = tex(GL_RGBA8, 1, GL_LINEAR);
  glCreateFramebuffers(1, &up.hdrFbo);
  glNamedFramebufferTexture(up.hdrFbo, GL_COLOR_ATTACHMENT0, up.hdrColor, 0); glNamedFramebufferTexture(up.hdrFbo, GL_COLOR_ATTACHMENT1, up.vmDistTex, 0);
  glNamedFramebufferTexture(up.hdrFbo, GL_DEPTH_ATTACHMENT, up.depthTex, 0); glNamedFramebufferDrawBuffer(up.hdrFbo, GL_COLOR_ATTACHMENT0);
  glCreateFramebuffers(1, &up.dofFbo); glNamedFramebufferTexture(up.dofFbo, GL_COLOR_ATTACHMENT0, up.dofTex, 0);
  glCreateFramebuffers(1, &up.ldrFbo);
  glNamedFramebufferTexture(up.ldrFbo, GL_COLOR_ATTACHMENT0, up.ldrColor, 0); glNamedFramebufferTexture(up.ldrFbo, GL_DEPTH_ATTACHMENT, up.depthTex, 0);
  for (int k = 0; k < 2; ++k) {
    up.hist[k] = tex(GL_RGBA16F, 1, GL_LINEAR);
    glCreateFramebuffers(1, &up.taaFbo[k]);
    glNamedFramebufferTexture(up.taaFbo[k], GL_COLOR_ATTACHMENT0, up.hdrColor, 0); glNamedFramebufferTexture(up.taaFbo[k], GL_COLOR_ATTACHMENT1, up.hist[k], 0);
    GLenum b2[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1}; glNamedFramebufferDrawBuffers(up.taaFbo[k], 2, b2);
  }
  for (GLuint f : {up.hdrFbo, up.dofFbo, up.ldrFbo, up.taaFbo[0], up.taaFbo[1]})
    if (glCheckNamedFramebufferStatus(f, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { logf("TAAU framebuffer %u incomplete at %dx%d", f, w, h); ++fboErrors; }
}

void Renderer::swapUp() {
  std::swap(hdrFbo, up.hdrFbo); std::swap(hdrColor, up.hdrColor); std::swap(depthTex, up.depthTex); std::swap(vmDistTex, up.vmDistTex);
  std::swap(dofTex, up.dofTex); std::swap(dofFbo, up.dofFbo); std::swap(ldrFbo, up.ldrFbo); std::swap(ldrColor, up.ldrColor);
  std::swap(width, up.w); std::swap(height, up.h);
}

// Renderer::renderThumbnail moved to thumbs.cpp (W2: build-menu thumbnails)

// ---- WX1 environmental fog field (docs/rendering.md#weather): generated from the heightmap only, cached on the GPU
// (unit 27) and on the CPU (AI sight). R = where ground fog pools (hollows below the smoothed ground, low ground, the
// shore), G = static drift noise (the shader scrolls it on the weather wind), B = the smoothed ground the layer sits on.
void Renderer::bakeFog(const Terrain& t) {
  const int N = 256;
  vec2 ext(t.tilesX() * t.tileSize(), t.tilesZ() * t.tileSize());
  float sig = (perf.w2Mist ? 0.731f : 0.0f) + t.org.x + t.org.y * 3.1f + ext.x * 7.3f + ext.y * 11.7f + t.height(t.org.x + ext.x * 0.31f, t.org.y + ext.y * 0.57f) + t.height(t.org.x + ext.x * 0.73f, t.org.y + ext.y * 0.21f) * 1.37f;
  if (fogTex && sig == fogSig) return;
  fogSig = sig;
  std::vector<float> h((size_t)N * N), s, tmp((size_t)N * N);
  float hmin = 1e9f;
  for (int j = 0; j < N; ++j) for (int i = 0; i < N; ++i) { float v = t.height(t.org.x + (i + 0.5f) * ext.x / N, t.org.y + (j + 0.5f) * ext.y / N); h[(size_t)j * N + i] = v; hmin = std::min(hmin, v); }
  s = h;
  for (int it = 0; it < 3; ++it) for (int pass = 0; pass < 2; ++pass) {   // ~3 box blurs of radius 8 texels: the broad ground
    for (int j = 0; j < N; ++j) for (int i = 0; i < N; ++i) {
      float a = 0; int n = 0;
      for (int k = -8; k <= 8; ++k) { int x = pass == 0 ? std::clamp(i + k, 0, N - 1) : i, y = pass == 1 ? std::clamp(j + k, 0, N - 1) : j; a += s[(size_t)y * N + x]; ++n; }
      tmp[(size_t)j * N + i] = a / n;
    }
    s.swap(tmp);
  }
  auto hash = [](int x, int y) { uint32_t v = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u; v = (v ^ (v >> 13)) * 1274126177u; return ((v ^ (v >> 16)) & 0xffff) / 65535.0f; };
  auto vn = [&](float x, float y) { int x0 = (int)std::floor(x), y0 = (int)std::floor(y); float fx = x - x0, fy = y - y0; fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    float a = hash(x0, y0), b = hash(x0 + 1, y0), c = hash(x0, y0 + 1), d = hash(x0 + 1, y0 + 1); return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy; };
  fogCpu.assign((size_t)N * N, vec4(0));
  for (int j = 0; j < N; ++j) for (int i = 0; i < N; ++i) {
    size_t k = (size_t)j * N + i; float v = h[k];
    float basin = std::clamp((s[k] - v) / 6.0f, 0.0f, 1.0f);            // hollows, valley floors between rises
    float low = std::clamp(1.0f - (v - hmin) / 35.0f, 0.0f, 1.0f);      // the lowest ground of the map
    float shore = hmin < 2.0f ? std::clamp(1.0f - std::fabs(v - 1.0f) / 6.0f, 0.0f, 1.0f) : 0.0f;   // coastal haze
    float r = std::clamp((perf.w2Mist ? 0.0f : 0.3f) + 0.8f * basin + 0.45f * low + 0.6f * shore, 0.0f, 1.8f);
    if (perf.w2Mist) r *= 0.45f + 1.1f * vn(i / 3.0f + 57.0f, j / 3.0f + 11.0f);   // W2: no floor (flat open ground stays clear), density 2-3x patchy over tens of metres
    fogCpu[k] = vec4(r, vn(i / 24.0f, j / 24.0f) * 0.65f + vn(i / 9.0f + 31.0f, j / 9.0f) * 0.35f, s[k], 0);
  }
  if (!fogTex) {
    glCreateTextures(GL_TEXTURE_2D, 1, &fogTex); glTextureStorage2D(fogTex, 1, GL_RGBA16F, N, N);
    glTextureParameteri(fogTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(fogTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(fogTex, GL_TEXTURE_WRAP_S, GL_REPEAT); glTextureParameteri(fogTex, GL_TEXTURE_WRAP_T, GL_REPEAT);
  }
  glTextureSubImage2D(fogTex, 0, 0, 0, N, N, GL_RGBA, GL_FLOAT, fogCpu.data());
  fogBox = vec4(t.org.x, t.org.y, 1.0f / ext.x, 1.0f / ext.y);
  fogGroundMin = hmin; fogGroundMax = *std::max_element(h.begin(), h.end());   // viscull (visCullDist)
}
// the shader's optical depth with the field taken at the segment's midpoint (no drift noise): bot sight in fog
float Renderer::fogT(vec3 a, vec3 b) const {
  float dens = wx.fogNow >= 0 ? wx.fogNow : wx.fogDens();
  const float tPreset = wx.der.phys ? std::exp(-wx.der.sigma * glm::length(b - a)) : 1.0f;   // a preset's visibility (Dense fog, Blizzard, Sandstorm)
  if (fogCpu.empty() || dens <= 0) return tPreset;   // the bots' sight: no fog field on the map must not mean clear air under a fog preset
  const int N = 256; vec3 m = (a + b) * 0.5f;
  int i = std::clamp((int)((m.x - fogBox.x) * fogBox.z * N), 0, N - 1), j = std::clamp((int)((m.z - fogBox.y) * fogBox.w * N), 0, N - 1);
  const vec4& F = fogCpu[(size_t)j * N + i];
  float hm = std::max(m.y - F.z, 0.0f);
  return std::exp(-dens * F.x * glm::length(b - a) * std::exp(-hm / 22.0f)) * tPreset;   // W1: a preset's visibility counts for the bots' sight
}

// TH2: the last thermal frame's heat target read back (tests): hot pixels and their box, rows counted from the top
Renderer::HeatStats Renderer::heatStats(float thr) const {
  HeatStats st; if (!heatTex || heatW <= 0) return st;
  st.w = heatW; st.h = heatH;
  std::vector<float> px((size_t)heatW * heatH);
  glGetTextureImage(heatTex, 0, GL_RED, GL_FLOAT, (GLsizei)(px.size() * sizeof(float)), px.data());
  for (int y = 0; y < heatH; ++y) for (int x = 0; x < heatW; ++x) if (px[(size_t)y * heatW + x] > thr) {
    const int r = heatH - 1 - y; ++st.hot; st.x0 = std::min(st.x0, x); st.x1 = std::max(st.x1, x); st.y0 = std::min(st.y0, r); st.y1 = std::max(st.y1, r);
  }
  return st;
}
