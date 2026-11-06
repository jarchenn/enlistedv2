#include "render.h"
#include <chrono>

// ============================================================== shaders
static const char* kCommon = R"GLSL(
#version 450 core
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
uniform sampler2DShadow uShadow; uniform mat4 uLightVP; uniform int uShadowOn;
uniform sampler2D uOcc; uniform mat4 uOccVP; uniform float uOccTop, uOccRange; uniform int uOccOn;
uniform vec2 uShadowTexel;

float shadowPCF(vec3 w, vec3 n){
  if (uShadowOn == 0) return 1.0;
  float ndl = clamp(dot(n, uSunDir), 0.0, 1.0);
  vec4 lp = uLightVP * vec4(w + n*(0.02 + 0.05*(1.0-ndl)), 1.0);
  vec3 s = lp.xyz/lp.w*0.5+0.5;
  if (s.x<0.0||s.x>1.0||s.y<0.0||s.y>1.0||s.z>1.0) return 1.0;
  float sum = 0.0;
  for (int y=-1;y<=1;y++) for (int x=-1;x<=1;x++)
    sum += texture(uShadow, vec3(s.xy + vec2(x,y)*uShadowTexel*1.2, s.z - 0.0006));
  return sum/9.0;
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
vec3 skyColor(vec3 d){
  float h = clamp(d.y, -1.0, 1.0);
  vec3 zen = vec3(0.33,0.44,0.60)*1.25, hor = vec3(0.74,0.77,0.79)*1.25, gnd = vec3(0.40,0.41,0.38);
  vec3 c = h > 0.0 ? mix(hor, zen, pow(h, 0.55)) : mix(hor, gnd, clamp(-h*4.0,0.0,1.0));
  float sd = max(dot(d, uSunDir), 0.0);
  c += uSunCol * (pow(sd, 400.0)*6.0 + pow(sd, 12.0)*0.12);
  return c;
}
vec3 applyFog(vec3 col, vec3 w){
  float d = max(length(w - uCamPos) - 35.0, 0.0);   // clear near view, haze builds with distance
  float f = 1.0 - exp(-d*uFogDens);
  vec3 dir = normalize(w - uCamPos);
  vec3 fc = mix(uFogCol, skyColor(vec3(dir.x, max(dir.y,0.02), dir.z)), 0.5);
  return mix(col, fc, f);
}
float D_GGX(float NdH, float a){ float a2=a*a; float d=NdH*NdH*(a2-1.0)+1.0; return a2/(PI*d*d); }
float G_Smith(float NdV, float NdL, float r){ float k=(r+1.0)*(r+1.0)/8.0; return (NdV/(NdV*(1.0-k)+k))*(NdL/(NdL*(1.0-k)+k)); }
vec3 F_Schlick(float c, vec3 F0){ return F0 + (1.0-F0)*pow(1.0-c, 5.0); }
vec3 shade(vec3 w, vec3 N, vec3 alb, float rough, float metal, float ao){
  vec3 V = normalize(uCamPos - w);
  vec3 L = uSunDir; vec3 H = normalize(V+L);
  float NdL = max(dot(N,L),0.0), NdV = max(dot(N,V),1e-3), NdH = max(dot(N,H),0.0);
  float a = max(rough*rough, 0.02);
  vec3 F0 = mix(vec3(0.04), alb, metal);
  vec3 F = F_Schlick(max(dot(H,V),0.0), F0);
  vec3 spec = D_GGX(NdH,a)*G_Smith(NdV,NdL,rough)*F/(4.0*NdV*max(NdL,1e-3));
  vec3 kd = (1.0-F)*(1.0-metal);
  float sh = NdL > 0.0 ? shadowPCF(w, N) : 0.0;
  vec3 direct = (kd*alb/PI + spec) * uSunCol * NdL * sh;
  float sv = skyVis(w, N);
  float amb = mix(0.06, 1.0, sv);
  vec3 irr = mix(uGroundCol, uSkyCol, 0.5+0.5*N.y) * amb + uFill*(1.0-sv);
  vec3 R = reflect(-V, N);
  vec3 Fr = F0 + (max(vec3(1.0-rough), F0)-F0)*pow(1.0-NdV, 5.0);
  vec3 envL = mix(uFill * 2.4 + uGroundCol * 0.25, skyColor(R) * amb, sv);
  vec3 envSpec = envL * Fr * (1.0-rough*0.6) * 0.6;
  vec3 r = (direct + irr*alb*kd + envSpec) * ao;
  return (any(isnan(r)) || any(isinf(r))) ? vec3(0.0) : r;   // never write a non-finite HDR value
}
)GLSL";

static const char* kMainVS = R"GLSL(
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNrm; layout(location=2) in vec3 aAlb;
layout(location=3) in vec4 aPar; layout(location=4) in vec3 aPc; layout(location=5) in vec3 aPn;
uniform mat4 uModel, uVP;
out vec3 vW, vN, vAlb, vPc, vPn, vAp, vAn; out vec4 vPar;
void main(){
  vec4 w = uModel*vec4(aPos,1.0);
  vW = w.xyz; vN = mat3(uModel)*aNrm; vAlb = aAlb; vPar = aPar; vPc = aPc; vPn = aPn; vAp = aPos; vAn = aNrm;
  gl_Position = uVP*w;
}
)GLSL";

static const char* kMainFS = R"GLSL(
in vec3 vW, vN, vAlb, vPc, vPn, vAp, vAn; in vec4 vPar;
uniform vec4 uTint; uniform float uTime; uniform int uWeather;   // 1: world structures get localized weathering
layout(location=0) out vec4 oColor;
layout(location=1) out float oDist;   // eye distance, only stored during the first-person pass (depth of field)
vec2 faceUV(vec3 p, vec3 n){ vec3 a=abs(n); if (a.y>a.x && a.y>a.z) return p.xz; if (a.x>a.z) return vec2(p.z,p.y); return vec2(p.x,p.y); }
int domAxis(vec3 n){ vec3 a=abs(n); if (a.x>=a.y && a.x>=a.z) return 0; if (a.y>=a.z) return 1; return 2; }
void main(){
  int pat = int(vPar.x + 0.5);
  vec3 alb = vAlb; float rough = vPar.y, metal = vPar.z; int grain = int(vPar.w+0.5);
  float h = 0.0, bump = 0.0, ao = 1.0;
  if (pat == 1) { // coursed limestone: courses of varying height, irregular joints, pillowed faces
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
    float e = min(ex, ey) + (vnoise(vec3(uv * 9.0, 3.0)) - 0.5) * 0.018;
    float mortar = 1.0 - smoothstep(0.005, 0.02, e);
    float pillow = smoothstep(0.0, 0.06, e);
    float u = clamp((x - left) / max(right - left, 1e-3), 0.0, 1.0);
    float tilt = (hash12(id + 3.0) - 0.5) * (u - 0.5) + (hash12(id + 9.0) - 0.5) * ((fy - cy0) / max(rowH, 1e-3) - 0.5);
    float pit = smoothstep(0.62, 0.8, vnoise(vec3(uv * 22.0, id.x)));
    vec3 sc = alb * (0.74 + 0.4 * hash12(id));
    sc = mix(sc, sc * vec3(1.1, 0.98, 0.8), hash12(id + 11.0) * 0.85);          // ochre Caen limestone
    sc = mix(sc, sc * vec3(0.84, 0.88, 0.93), step(0.86, hash12(id + 5.0)) * 0.7);   // occasional grey stone
    sc *= 0.8 + 0.38 * fbm(vec3(uv * 0.3, 9.0));                                     // macro variation along the wall
    sc *= 0.9 + 0.16 * fbm(vec3(uv * 16.0, 5.0));
    sc *= 1.0 - 0.18 * pit;
    vec3 mcol = vec3(0.50, 0.48, 0.43) * (0.85 + 0.22 * fbm(vec3(uv * 11.0, 2.0)));
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
      vec3 paint = alb * (0.9 + 0.1*fbm(vPc*3.0));
      vec3 bare = vec3(0.42,0.37,0.30)*(0.8+0.35*gr);
      wc = mix(paint, bare, wear);
      rough = mix(rough, 0.85, wear);
    }
    if (endGrain) {   // cut end: growth rings, darker, more absorbent
      vec2 q = vec2(vPc[(na+1)%3], vPc[(na+2)%3]) + vec2(hash12(vec2(pid, 1.0)), hash12(vec2(pid, 2.0))) * 0.3;
      float ring = fract(length(q) * 55.0 + vnoise(vec3(q * 30.0, 1.0)) * 1.5);
      wc = alb * (0.62 + 0.18 * smoothstep(0.3, 0.6, ring));
      seam = 0.0; rough = min(1.0, rough + 0.15);
    }
    alb = mix(wc, wc*0.4, seam);
    rough = clamp(rough + (gr-0.5)*0.12, 0.2, 1.0);
    h = (1.0-seam) + gr*0.1; bump = 0.0025; ao = 1.0 - seam*0.3;
  } else if (pat == 3) { // lime plaster / whitewash
    float n = fbm(vAp*1.7);
    float trowel = vnoise(vec3(faceUV(vAp, vAn) * vec2(3.0, 9.0), 7.0));
    alb *= 0.9 + 0.12*n + 0.05*trowel;
    alb = mix(alb, alb*1.06, smoothstep(0.55, 0.7, fbm(vAp*0.6 + 3.0)));
    float dirt = 1.0 - smoothstep(0.0, 0.5, fract(vAp.y) * 0.0 + (vAp.y - floor(vAp.y/3.0)*3.0));
    alb *= mix(1.0, 0.86, dirt*0.0);
    h = fbm(vAp*14.0); bump = 0.0012;
  } else if (pat == 4) { // slate courses (part-local; grain axis = slope)
    int na = domAxis(vPn);
    if (na == 1) {
      float s = vPc[grain]; int ac = (grain == 0) ? 2 : 0; float x = vPc[ac];
      float course = floor(s/0.17); float fs = fract(s/0.17);
      float off = mod(course, 2.0)*0.13; float tid = floor((x+off)/0.26); float fx = fract((x+off)/0.26);
      float edge = min(fx, 1.0-fx)*0.26;
      float gap = 1.0 - smoothstep(0.004, 0.012, edge);
      float v = hash12(vec2(tid, course));
      alb *= (0.75 + 0.45*v) * (0.9+0.15*vnoise(vPc*12.0));
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
    alb *= (0.8 + 0.35*hash12(id)) * (0.9 + 0.15*fbm(vec3(uv*3.0,1.0)));
    alb = mix(alb, vec3(0.45,0.42,0.38), grout);
    rough = mix(rough - 0.15*hash12(id+3.0), 0.95, grout);
    h = 1.0-grout; bump = 0.002; ao = 1.0 - grout*0.3;
  } else if (pat == 6) { // iron / steel
    float n = fbm(vAp*6.0);
    alb *= 0.8 + 0.3*n; rough = clamp(rough + (n-0.5)*0.3, 0.2, 1.0);
    h = n; bump = 0.0008;
  } else if (pat == 8) { // fabric
    float n = vnoise(vAp*140.0)*0.5 + vnoise(vAp*9.0)*0.5;
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
    float n = fbm(vAp*1.6);
    float bj = abs(fract(vAp.y/0.22 + hash12(floor(vAp.xz*0.5))*0.0)-0.5)*0.22;
    float board = 1.0 - smoothstep(0.004, 0.012, 0.11 - bj);
    float grainB = vnoise(vec3(faceUV(vAp, vAn).x*2.0, vAp.y*40.0, 3.0));
    float agg = smoothstep(0.72, 0.85, vnoise(vAp*70.0));
    alb *= 0.8 + 0.28*n; alb *= 1.0 - board*0.12 - grainB*0.05;
    alb = mix(alb, alb*vec3(1.15,1.12,1.05), agg*0.5);
    float damp = 1.0 - smoothstep(-0.3, 0.9, vAp.y);
    alb *= mix(1.0, 0.72, damp); rough = mix(rough, rough*0.8, damp);
    h = n*0.4 - board*0.6 + agg*0.2 + grainB*0.1; bump = 0.0045;
  } else {
    alb *= 0.94 + 0.08*vnoise(vAp*7.0);
  }
  if (uWeather == 1 && (pat == 1 || pat == 2 || pat == 3 || pat == 9 || pat == 11)) {
    // localized weathering in asset space (origin at the ground): rising damp and splash at the foot of vertical
    // faces, moss patches in that damp band, faint run-off streaks higher up. Not a uniform dirt overlay.
    float vert = 1.0 - smoothstep(0.4, 0.7, abs(normalize(vN).y));
    float nn = vnoise(vAp*1.3);
    float foot = (1.0 - smoothstep(0.0, 0.45 + 0.4*nn, vAp.y)) * vert;
    alb *= mix(1.0, 0.7, foot); rough = mix(rough, rough*0.75, foot);
    float moss = foot * smoothstep(0.5, 0.72, vnoise(vAp*2.3 + 7.0)) * (pat == 3 ? 0.5 : 1.0);
    alb = mix(alb, vec3(0.13, 0.17, 0.07), moss*0.6);
    float streak = smoothstep(0.66, 0.92, vnoise(vec3(dot(vAp.xz, vec2(3.1, 2.7)), vAp.y*0.22, 2.0))) * vert * smoothstep(0.6, 2.0, vAp.y);
    alb *= 1.0 - 0.14*streak;
  }
  vec3 N = dot(vN, vN) > 1e-12 ? normalize(vN) : vec3(0.0, 1.0, 0.0);
  if (!gl_FrontFacing) N = -N;
  if (bump > 0.0) {
    vec3 dpdx = dFdx(vW), dpdy = dFdy(vW);
    float dhx = dFdx(h)*bump, dhy = dFdy(h)*bump;
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
  vec3 c = shade(vW, N, alb, rough, metal, ao);
  c = applyFog(c, vW);
  oDist = length(vW - uCamPos);
  if (uTint.a < 0.999) { c = mix(c, uTint.rgb, 0.55); oColor = vec4(c, uTint.a); return; }
  oColor = vec4(c * uTint.rgb, 1.0);   // opaque tint multiplies (white = unchanged; wrecks are blackened)
}
)GLSL";

static const char* kGlassFS = R"GLSL(
in vec3 vW, vN, vAlb, vPc, vPn, vAp, vAn; in vec4 vPar;
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
  vec3 H = normalize(V + uSunDir);
  float spec = pow(max(dot(N,H),0.0), 600.0) * 30.0 * shadowPCF(vW, N);
  float dirt = fbm(vAp*6.0);
  vec3 tint = vec3(0.82,0.88,0.84);
  vec3 c = refl*F + uSunCol*spec*F + tint*0.03*dirt;
  float a = clamp(0.10 + F*0.85 + dirt*0.08, 0.0, 0.95);
  c = applyFog(c, vW);
  oColor = vec4(c / max(a, 0.05), a);
}
)GLSL";

// ---- imported textured models (first-person weapons): metal-rough PBR with normal and AO maps
static const char* kModelVS = R"GLSL(
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNrm; layout(location=2) in vec4 aTan; layout(location=3) in vec2 aUV;
uniform mat4 uModel, uVP;
out vec3 vW, vN; out vec4 vT; out vec2 vUV;
void main(){
  vec4 w = uModel*vec4(aPos,1.0);
  mat3 m = mat3(uModel);
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
  c = applyFog(c, vW);
  if (uTint.a < 0.999) c = mix(c, uTint.rgb, 0.55);
  oColor = vec4(c, 1.0);
  oDist = length(vW - uCamPos);
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

// ---- tree leaf cards: alpha-tested clusters, sway  V_offset = D_wind * M * A * sin(f t + k_x X + k_z Z)
static const char* kLeafVS = R"GLSL(
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNrm; layout(location=2) in vec2 aUV; layout(location=3) in float aMask; layout(location=4) in float aShade;
uniform mat4 uModel, uVP; uniform float uTime, uWind;
out vec3 vW, vN; out vec2 vUV; out float vM, vShade; flat out float vSeed;
void main(){
  vec4 w = uModel*vec4(aPos, 1.0);
  const vec3 D = vec3(0.8, 0.0, 0.6); const float A = 0.10, f = 1.3, kx = 0.21, kz = 0.17;
  float s = sin(f*uTime + kx*w.x + kz*w.z) + 0.3*sin(2.9*uTime + 0.8*w.x - 0.5*w.y);
  w.xyz += (D*A*s + vec3(0.0, 0.25*A*s, 0.0)) * aMask * uWind;
  vW = w.xyz; vN = normalize(mat3(uModel)*aNrm); vUV = aUV; vM = aMask; vShade = aShade;
  vSeed = floor(dot(aPos, vec3(13.1, 7.3, 11.7)));
  gl_Position = uVP*w;
}
)GLSL";
static const char* kLeafAlpha = R"GLSL(
float leafAlpha(vec2 uv, float seed){
  float a = 0.0;
  for (int i = 0; i < 6; ++i) {
    vec2 c = vec2(hash12(vec2(seed, float(i))), hash12(vec2(float(i), seed + 3.1)))*0.64 + 0.18;
    float ang = hash12(vec2(seed + float(i), 7.0))*6.2832;
    vec2 d = uv - c; d = mat2(cos(ang), -sin(ang), sin(ang), cos(ang))*d; d.x *= 2.3;
    if (dot(d, d) < 0.028) a = 1.0;
  }
  return a;
}
)GLSL";
static const char* kLeafFS = R"GLSL(
in vec3 vW, vN; in vec2 vUV; in float vM, vShade; flat in float vSeed;
uniform vec3 uLeafCol;
out vec4 oColor;
float leafAlpha(vec2 uv, float seed);
void main(){
  if (leafAlpha(vUV, vSeed) < 0.5) discard;
  vec3 N = normalize(vN);
  float var = hash12(vec2(vSeed, 2.0));
  vec3 alb = uLeafCol*(0.72 + 0.56*var)*vShade;
  alb = mix(alb, alb*vec3(1.08, 0.95, 0.7), hash12(vec2(vSeed, 5.0))*0.35);   // some yellowed / olive leaves
  float sh = 1.0;
  if (uShadowOn == 1) { vec4 lp = uLightVP*vec4(vW + N*0.12, 1.0); vec3 sc = lp.xyz/lp.w*0.5 + 0.5;
    if (sc.x > 0.0 && sc.x < 1.0 && sc.y > 0.0 && sc.y < 1.0 && sc.z < 1.0) sh = texture(uShadow, vec3(sc.xy, sc.z - 0.002)); }
  float ndl = max(dot(N, uSunDir), 0.0)*0.7 + 0.3;
  vec3 irr = mix(uGroundCol, uSkyCol, 0.5 + 0.5*N.y)*mix(0.45, 1.0, 0.5 + 0.5*N.y);
  vec3 V = normalize(uCamPos - vW);
  vec3 c = alb/PI*uSunCol*ndl*sh + irr*alb*0.7*vShade + alb*uSunCol*0.08*pow(max(dot(-V, uSunDir), 0.0), 4.0)*sh;   // restrained translucency
  c = applyFog(c, vW);
  oColor = vec4((any(isnan(c)) || any(isinf(c))) ? vec3(0.0) : c, 1.0);
}
)GLSL";
static const char* kLeafShadowFS = R"GLSL(
in vec3 vW, vN; in vec2 vUV; in float vM, vShade; flat in float vSeed;
float leafAlpha(vec2 uv, float seed);
void main(){ if (leafAlpha(vUV, vSeed) < 0.5) discard; }
)GLSL";

static const char* kShadowVS = R"GLSL(
#version 450 core
layout(location=0) in vec3 aPos;
uniform mat4 uModel, uVP;
void main(){ gl_Position = uVP*uModel*vec4(aPos,1.0); }
)GLSL";
static const char* kShadowFS = R"GLSL(
#version 450 core
void main(){}
)GLSL";

static const char* kTerrainVS = R"GLSL(
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNrm; layout(location=2) in float aDirt;
uniform mat4 uVP;
out vec3 vW, vN; out float vDirt;
void main(){ vW = aPos; vN = aNrm; vDirt = aDirt; gl_Position = uVP*vec4(aPos,1.0); }
)GLSL";
static const char* kTerrainFS = R"GLSL(
in vec3 vW, vN; in float vDirt;
uniform int uGrid; uniform float uBuildHalf;
uniform int uDiscard; uniform sampler2D uTileMask; uniform float uTileSize, uTerrainSize;
uniform sampler2D uSurf;   // R wear, G wet, B route kind, A rut (surface.cpp)
out vec4 oColor;
void main(){
  vec3 p = vW;
  if (uDiscard == 1) {   // base mesh hidden under high-resolution deformation tiles
    ivec2 tc = ivec2(floor((p.xz + uTerrainSize*0.5)/uTileSize));
    if (texelFetch(uTileMask, tc, 0).r > 0.5) discard;
  }
  vec4 S = texture(uSurf, (p.xz + uTerrainSize*0.5)/uTerrainSize);
  float wear = S.r, wet = S.g, kind = S.b, rut = S.a;
  // four scales so nothing tiles: broad fields (80 m), patches (14 m), clumps (1 m), grit (0.2 m)
  float nB = fbm(vec3(p.xz*0.012, 1.0)), nM = fbm(vec3(p.xz*0.07, 2.0)), nF = vnoise(vec3(p.xz*0.9, 3.0)), nX = vnoise(vec3(p.xz*4.3, 4.0));
  vec3 g1 = vec3(0.12,0.19,0.06), g2 = vec3(0.21,0.26,0.085), dry = vec3(0.32,0.30,0.165);
  vec3 grass = mix(g1, g2, smoothstep(0.3, 0.7, nB));
  grass = mix(grass, dry, smoothstep(0.6, 0.82, nM)*0.5);
  grass *= 0.78 + 0.32*nF;
  grass *= 1.0 - 0.16*smoothstep(0.65, 0.8, vnoise(vec3(p.xz*0.9, 9.0)));   // clover / darker tufts
  vec3 soil = mix(vec3(0.20,0.155,0.10), vec3(0.28,0.22,0.145), nF) * (0.85 + 0.3*nX);
  vec3 mud = mix(vec3(0.12,0.095,0.07), vec3(0.17,0.135,0.10), nX);
  float stone = step(0.6, vnoise(vec3(p.xz*9.0, 7.0)));
  vec3 grav = mix(vec3(0.34,0.32,0.28), vec3(0.46,0.43,0.38), stone) * (0.8 + 0.3*nX);
  vec3 track = mix(soil, grav, smoothstep(0.75, 1.0, kind));
  // compacted earth: trodden patches, small stones and wheel-polished strips break the track up at 0.2-2 m
  float pebble = smoothstep(0.78, 0.9, vnoise(vec3(p.xz*6.5, 11.0)));
  track = mix(track * (0.82 + 0.3*vnoise(vec3(p.xz*1.7, 12.0))), track*1.25 + vec3(0.03), pebble*0.45);
  // grass breaks up into the track along a noisy edge; ruts and damp lows turn to mud
  float tw = smoothstep(0.25, 0.75, wear + (nF - 0.5)*0.35 + (nX - 0.5)*0.15);
  vec3 alb = mix(grass, track, tw);
  alb = mix(alb, mud, clamp(rut*0.85 + wet*0.6, 0.0, 1.0)*tw);
  // crater and disturbed soil
  float dirt = clamp(vDirt, 0.0, 1.0);
  alb = mix(alb, soil, smoothstep(0.15, 0.75, dirt + (nM - 0.5)*0.35));
  // localized wetness: darker, and glossy only where water stands (puddles follow ruts / lows)
  float puddle = smoothstep(0.55, 0.8, wet + (nX - 0.5)*0.2);
  alb *= mix(1.0, 0.62, clamp(wet*1.2, 0.0, 1.0));
  float rough = mix(mix(0.96, 0.86, tw), 0.10, puddle);
  vec3 N = normalize(vN + vec3(nX - 0.5, 0.0, vnoise(vec3(p.xz*4.3, 5.0)) - 0.5)*mix(0.22, 0.42, max(tw, dirt))*(1.0 - puddle));
  float ao = (1.0 - 0.25*dirt) * (1.0 - 0.18*rut*tw);
  vec3 c = shade(vW, N, alb, rough, 0.0, ao);
  if (uGrid == 1 && abs(p.x) <= uBuildHalf && abs(p.z) <= uBuildHalf) {
    vec2 g = abs(fract(p.xz/5.0 + 0.5) - 0.5)*5.0;
    vec2 fw = fwidth(p.xz)*1.2;
    float line = 1.0 - min(smoothstep(0.0, fw.x, g.x), smoothstep(0.0, fw.y, g.y));
    float d = length(p - uCamPos);
    c = mix(c, vec3(0.85,0.85,0.7), line*0.35*(1.0 - smoothstep(60.0, 220.0, d)));
  }
  c = applyFog(c, vW);
  oColor = vec4((any(isnan(c)) || any(isinf(c))) ? vec3(0.0) : c, 1.0);
}
)GLSL";


// ---- instanced grass: one tapered blade per instance, alpha-tested silhouette, rooted wind bend
static const char* kGrassVS = R"GLSL(
layout(location=0) in vec3 aB; layout(location=1) in vec4 iP; layout(location=2) in vec4 iS;
uniform mat4 uVP; uniform float uTime, uRadius, uWidthScale, uWind;
out vec3 vW, vAlb, vF; out vec2 vUV;
void main(){
  float d = length(iP.xyz - uCamPos);
  float fade = 1.0 - smoothstep(uRadius*0.72, uRadius, d);
  float bl = aB.z;
  // per-blade variation inside the tuft (deterministic from the instance phase)
  float r1 = fract(sin(iS.w*12.9898 + bl*78.233)*43758.5453), r2 = fract(sin(iS.w*39.346 + bl*11.135)*24634.6345);
  float ang = iP.w + bl*2.39996 + r1*0.6;
  vec3 side = vec3(cos(ang), 0.0, -sin(ang));
  vec3 fwd = vec3(sin(ang), 0.0, cos(ang));
  vec3 root = iP.xyz + fwd*(0.02 + 0.05*r2);
  float h = iS.x * (0.6 + 0.45*r2) * fade;
  float v = aB.y;
  float w = iS.y * uWidthScale * (1.0 - v*0.55);
  vec3 p = root + side*(aB.x*w*0.5) + vec3(0.0, v*h, 0.0);
  float curve = 0.12 + 0.30*r1;                        // blades lean out of the tuft
  float gust = sin(uTime*1.7 + iS.w + dot(iP.xz, vec2(0.13, 0.07)))*0.55 + sin(uTime*0.63 + dot(iP.xz, vec2(0.045, 0.11)))*0.45;
  vec3 bend = fwd*curve + vec3(0.8, 0.0, 0.6)*(0.08 + gust*0.20)*uWind;
  p += bend*(v*v)*h;            // root (v = 0) never moves
  p.y -= (v*v)*h*dot(bend, bend)*0.35;
  vec3 alb = unpackUnorm4x8(floatBitsToUint(iS.z)).rgb*0.5;   // meadow palette baked per tuft on the CPU
  vAlb = alb; vW = p; vF = fwd; vUV = vec2(aB.x*0.5 + 0.5, v);
  gl_Position = uVP*vec4(p, 1.0);
}
)GLSL";
static const char* kGrassFS = R"GLSL(
in vec3 vW, vAlb, vF; in vec2 vUV;
out vec4 oColor;
void main(){
  float e = abs(vUV.x*2.0 - 1.0), v = vUV.y;
  if (e > 1.0 - pow(v, 1.6)) discard;                   // alpha-tested blade silhouette (pointed tip)
  vec3 F = gl_FrontFacing ? vF : -vF;
  vec3 N = normalize(vec3(0.0, 1.0, 0.0) + F*0.45);
  vec3 alb = vAlb * mix(0.72, 1.0, v);   // darker roots, no bleached tips
  // cheap lighting: one hardware-PCF shadow tap, one sky-occlusion tap, wrapped diffuse, no specular
  float sh = 1.0;
  if (uShadowOn == 1) {
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
  vec3 c = (alb/PI*uSunCol*ndl*sh + irr*alb) * mix(0.72, 1.0, v);
  vec3 V = normalize(uCamPos - vW);
  c += alb * uSunCol * 0.10 * pow(max(dot(-V, uSunDir), 0.0), 3.0) * v * sh;   // thin-leaf transmission
  c = applyFog(c, vW);
  oColor = vec4((any(isnan(c)) || any(isinf(c))) ? vec3(0.0) : c, 1.0);
}
)GLSL";

static const char* kFullVS = R"GLSL(
#version 450 core
out vec2 vUV;
void main(){ vec2 p = vec2((gl_VertexID<<1)&2, gl_VertexID&2); vUV = p; gl_Position = vec4(p*2.0-1.0, 1.0, 1.0); }
)GLSL";
static const char* kSkyFS = R"GLSL(
in vec2 vUV;
uniform mat4 uInvVP; uniform int uCloudOn; uniform float uCloudCover, uTime;
out vec4 oColor;
void main(){
  vec4 a = uInvVP*vec4(vUV*2.0-1.0, -1.0, 1.0), b = uInvVP*vec4(vUV*2.0-1.0, 1.0, 1.0);
  vec3 d = normalize(b.xyz/b.w - a.xyz/a.w);
  vec3 c = skyColor(d);
  if (uCloudOn == 1 && d.y > 0.0) {
    // ray to the cloud plane (1500 m), fBm (5 octaves) scrolling with the wind; coverage shifts the threshold
    vec2 cp = (d.xz/(d.y + 0.06))*1500.0/600.0 + uTime*vec2(0.012, 0.0075);   // ~600 m cells at the 1500 m deck
    float n = 0.0, a = 0.5; vec3 q = vec3(cp, uTime*0.002);
    for (int i = 0; i < 5; ++i) { n += a*vnoise(q); q = q*2.07 + vec3(1.7, 9.2, 3.1); a *= 0.5; }
    float th = mix(0.72, 0.38, uCloudCover);
    float cl = smoothstep(th, th + 0.22, n);
    float sd = max(dot(d, uSunDir), 0.0);
    float thick = smoothstep(th + 0.05, th + 0.45, n);
    vec3 lit = vec3(0.95, 0.95, 0.96)*1.35*mix(1.0, 0.5, thick) + uSunCol*0.35*pow(sd, 6.0)*(1.0 - thick);   // grey bases, silver lining at thin edges
    c = mix(c, lit, cl*0.9*smoothstep(0.0, 0.12, d.y));
  }
  oColor = vec4((any(isnan(c)) || any(isinf(c))) ? skyColor(d) : c, 1.0);
}
)GLSL";
static const char* kToneFS = R"GLSL(
#version 450 core
in vec2 vUV;
uniform sampler2D uHdr, uExpo; uniform float uFixedExpo;
out vec4 oColor;
vec3 aces(vec3 x){ return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14), 0.0, 1.0); }
vec3 toSRGB(vec3 c){ return mix(c*12.92, 1.055*pow(c, vec3(1.0/2.4))-0.055, step(0.0031308, c)); }
void main(){
  vec3 c = texture(uHdr, vUV).rgb;
  float e = uFixedExpo > 0.0 ? uFixedExpo : texelFetch(uExpo, ivec2(0), 0).r;
  c = aces(c*e*0.8);
  // subdued, slightly desaturated period grade
  float l = dot(c, vec3(0.2126,0.7152,0.0722));
  c = mix(vec3(l), c, 0.93);
  vec2 q = vUV - 0.5; c *= 1.0 - dot(q,q)*0.16;
  oColor = vec4(toSRGB(c), 1.0);
}
)GLSL";
static const char* kExpoFS = R"GLSL(
#version 450 core
uniform sampler2D uHdr, uPrev; uniform float uLod, uDt, uReset;
out float oE;
void main(){
  vec3 avg = textureLod(uHdr, vec2(0.5), uLod).rgb;
  float prev = texelFetch(uPrev, ivec2(0), 0).r;
  if (uReset > 0.0) { oE = uReset; return; }
  float lr = dot(avg, vec3(0.2126,0.7152,0.0722));
  if (isnan(lr) || isinf(lr)) { oE = prev; return; }   // one non-finite pixel poisons the whole mip average
  float l = max(lr, 1e-4);
  float target = clamp(0.24/l, 0.9, 1.45);
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
  auto sh = [&](GLenum type, const char* const* srcs, int n) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, n, srcs, nullptr); glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[4096]; glGetShaderInfoLog(s, sizeof log, nullptr, log); logf("shader %s (%s) error:\n%s", name, type == GL_VERTEX_SHADER ? "vs" : "fs", log); ++shaderErrors; }
    return s;
  };
  bool vsHasVersion = std::strncmp(vs, "\n#version", 9) == 0 || std::strncmp(vs, "#version", 8) == 0;
  bool fsHasVersion = std::strncmp(fs, "\n#version", 9) == 0 || std::strncmp(fs, "#version", 8) == 0;
  const char* vsrc[2] = {kCommon, vs}; const char* fsrc[2] = {kCommon, fs};
  GLuint v = vsHasVersion ? sh(GL_VERTEX_SHADER, &vs, 1) : sh(GL_VERTEX_SHADER, vsrc, 2);
  GLuint f = fsHasVersion ? sh(GL_FRAGMENT_SHADER, &fs, 1) : sh(GL_FRAGMENT_SHADER, fsrc, 2);
  GLuint p = glCreateProgram(); glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
  GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) { char log[4096]; glGetProgramInfoLog(p, sizeof log, nullptr, log); logf("link %s error:\n%s", name, log); }
  glDeleteShader(v); glDeleteShader(f);
  return p;
}

static GLint U(GLuint p, const char* n) { return glGetUniformLocation(p, n); }

bool Renderer::init(int w, int h) {
  progMain = compile(kMainVS, kMainFS, "main");
  progGlass = compile(kMainVS, kGlassFS, "glass");
  progShadow = compile(kShadowVS, kShadowFS, "shadow");
  progTerrain = compile(kTerrainVS, kTerrainFS, "terrain");
  progSky = compile(kFullVS, kSkyFS, "sky");
  progTone = compile(kFullVS, kToneFS, "tone");
  progExpo = compile(kFullVS, kExpoFS, "expo");
  progLine = compile(kLineVS, kLineFS, "line");
  progFx = compile(kFxVS, kFxFS, "fx");
  progGrass = compile(kGrassVS, kGrassFS, "grass");
  progModel = compile(kModelVS, kModelFS, "model");
  progDof = compile(kFullVS, kDofFS, "dof");
  {
    std::string fs1 = std::string(kLeafAlpha) + kLeafFS, fs2 = std::string(kLeafAlpha) + kLeafShadowFS;
    progLeaf = compile(kLeafVS, fs1.c_str(), "leaf");
    progLeafShadow = compile(kLeafVS, fs2.c_str(), "leaf_shadow");
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

  // shadow map
  glCreateTextures(GL_TEXTURE_2D, 1, &shadowTex);
  glTextureStorage2D(shadowTex, 1, GL_DEPTH_COMPONENT32F, shadowSize, shadowSize);
  glTextureParameteri(shadowTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(shadowTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameteri(shadowTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(shadowTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTextureParameteri(shadowTex, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE); glTextureParameteri(shadowTex, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
  glCreateFramebuffers(1, &shadowFbo); glNamedFramebufferTexture(shadowFbo, GL_DEPTH_ATTACHMENT, shadowTex, 0);
  glNamedFramebufferDrawBuffer(shadowFbo, GL_NONE); glNamedFramebufferReadBuffer(shadowFbo, GL_NONE);
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
  glCreateQueries(GL_TIME_ELAPSED, 2, queries);
  // thumbnails
  glCreateTextures(GL_TEXTURE_2D, 1, &thumbHdr); glTextureStorage2D(thumbHdr, 1, GL_RGBA16F, 256, 256);
  glCreateTextures(GL_TEXTURE_2D, 1, &thumbDepth); glTextureStorage2D(thumbDepth, 1, GL_DEPTH_COMPONENT32F, 256, 256);
  glCreateFramebuffers(1, &thumbHdrFbo);
  glNamedFramebufferTexture(thumbHdrFbo, GL_COLOR_ATTACHMENT0, thumbHdr, 0);
  glNamedFramebufferTexture(thumbHdrFbo, GL_DEPTH_ATTACHMENT, thumbDepth, 0);
  resize(w, h);
  return true;
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
  glCreateFramebuffers(1, &dofFbo); glNamedFramebufferTexture(dofFbo, GL_COLOR_ATTACHMENT0, dofTex, 0);
  glCreateFramebuffers(1, &ldrFbo);
  glNamedFramebufferTexture(ldrFbo, GL_COLOR_ATTACHMENT0, ldrColor, 0);
  glNamedFramebufferTexture(ldrFbo, GL_DEPTH_ATTACHMENT, depthTex, 0);
}

void Renderer::resize(int w, int h) {
  w = std::max(w, 16); h = std::max(h, 16);
  if (w == width && h == height) return;
  width = w; height = h; createTargets();
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
  vec3 sun(2.65f, 2.52f, 2.32f), sky(0.42f, 0.49f, 0.60f), ground(0.20f, 0.19f, 0.155f), fog(0.64f, 0.68f, 0.72f), fill(0.10f, 0.095f, 0.08f);
  glUniform3fv(U(p, "uSunCol"), 1, glm::value_ptr(sun));
  glUniform3fv(U(p, "uSkyCol"), 1, glm::value_ptr(sky));
  glUniform3fv(U(p, "uGroundCol"), 1, glm::value_ptr(ground));
  glUniform3fv(U(p, "uFogCol"), 1, glm::value_ptr(fog));
  glUniform3fv(U(p, "uFill"), 1, glm::value_ptr(fill));
  glUniform3fv(U(p, "uCamPos"), 1, glm::value_ptr(v.camPos));
  glUniform1f(U(p, "uFogDens"), thumbMode ? 0.0f : 0.0016f);
  glUniformMatrix4fv(U(p, "uLightVP"), 1, GL_FALSE, glm::value_ptr(lightVP));
  glUniformMatrix4fv(U(p, "uOccVP"), 1, GL_FALSE, glm::value_ptr(occVP));
  glUniform1f(U(p, "uOccTop"), occTop); glUniform1f(U(p, "uOccRange"), occRange);
  glUniform1i(U(p, "uShadowOn"), thumbMode ? 0 : 1);
  glUniform1i(U(p, "uOccOn"), thumbMode ? 0 : 1);
  glUniform2f(U(p, "uShadowTexel"), 1.0f / shadowSize, 1.0f / shadowSize);
  glUniform1i(U(p, "uShadow"), 0); glUniform1i(U(p, "uOcc"), 1);
  glBindTextureUnit(0, shadowTex); glBindTextureUnit(1, occTex);
  glUniform1f(U(p, "uTime"), time);
}

void Renderer::drawItems(GLuint p, const std::vector<DrawItem>& items, bool shadowPass) {
  GLint lm = U(p, "uModel"), lt = U(p, "uTint");
  for (auto& it : items) {
    if (!it.mesh || !it.mesh->count) continue;
    glUniformMatrix4fv(lm, 1, GL_FALSE, glm::value_ptr(it.model));
    if (!shadowPass && lt >= 0) glUniform4fv(lt, 1, glm::value_ptr(it.tint));
    it.mesh->draw();
    stats.draws++; stats.tris += it.mesh->count / 3;
  }
}

void Renderer::drawFoliage(GLuint p, const mat4& vp) {
  glUseProgram(p);
  glUniformMatrix4fv(U(p, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
  glUniform1f(U(p, "uTime"), time); glUniform1f(U(p, "uWind"), wind);
  GLint lm = U(p, "uModel"), lc = U(p, "uLeafCol");
  for (auto& f : foliage) {
    if (!f.tree || !f.tree->cardVao) continue;
    glUniformMatrix4fv(lm, 1, GL_FALSE, glm::value_ptr(f.m));
    if (lc >= 0) glUniform3fv(lc, 1, glm::value_ptr(f.tree->leafCol));
    glBindVertexArray(f.tree->cardVao);
    glDrawArrays(GL_TRIANGLES, 0, f.tree->cardVerts);
    stats.draws++; stats.tris += f.tree->cardVerts / 3;
  }
}

void Renderer::drawModelsDepth(GLuint p, const std::vector<ModelDraw>& items) {
  GLint lm = U(p, "uModel");
  const GpuModel* bound = nullptr;
  for (auto& it : items) {
    const GpuModel* m = it.model;
    if (!m || !m->vao || it.part < 0 || it.part >= (int)m->parts.size()) continue;
    if (m != bound) { bound = m; glBindVertexArray(m->vao); }
    const ModelPart& pt = m->parts[it.part];
    glUniformMatrix4fv(lm, 1, GL_FALSE, glm::value_ptr(it.m));
    glDrawElements(GL_TRIANGLES, (GLsizei)pt.count, GL_UNSIGNED_INT, (void*)(uintptr_t)(pt.first * 4u));
  }
}

void Renderer::drawModels(const std::vector<ModelDraw>& items) {
  GLuint p = progModel;
  glUniform1i(U(p, "uAlb"), 2); glUniform1i(U(p, "uMR"), 3); glUniform1i(U(p, "uNrm"), 4); glUniform1i(U(p, "uAO"), 5);
  GLint lm = U(p, "uModel"), lb = U(p, "uBase"), lme = U(p, "uMetal"), lr = U(p, "uRough"), lt = U(p, "uTint");
  const GpuModel* bound = nullptr; int boundMat = -2;
  for (auto& it : items) {
    const GpuModel* m = it.model;
    if (!m || !m->vao || it.part < 0 || it.part >= (int)m->parts.size()) continue;
    const ModelPart& pt = m->parts[it.part];
    if (m != bound) { bound = m; boundMat = -2; glBindVertexArray(m->vao); }
    int mi = pt.mat >= 0 && pt.mat < (int)m->mats.size() ? pt.mat : -1;
    if (mi != boundMat) {
      boundMat = mi;
      GpuModel::MatTex none; const GpuModel::MatTex& t = mi >= 0 ? m->mats[mi] : none;
      glBindTextureUnit(2, t.alb); glBindTextureUnit(3, t.mr); glBindTextureUnit(4, t.n); glBindTextureUnit(5, t.ao);
      glUniform1i(U(p, "uHasAlb"), t.alb ? 1 : 0); glUniform1i(U(p, "uHasMR"), t.mr ? 1 : 0); glUniform1i(U(p, "uHasNrm"), t.n ? 1 : 0);
      glUniform1i(U(p, "uHasAO"), t.ao ? 1 : 0); glUniform1i(U(p, "uAOinMR"), t.aoInMR ? 1 : 0);
    }
    glUniformMatrix4fv(lm, 1, GL_FALSE, glm::value_ptr(it.m));
    glUniform4fv(lb, 1, glm::value_ptr(pt.base)); glUniform1f(lme, pt.metal); glUniform1f(lr, pt.rough);
    glUniform4fv(lt, 1, glm::value_ptr(it.tint));
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

void Renderer::render(const RenderView& v, const Terrain& t, const std::vector<DrawItem>& opaque,
                      const std::vector<DrawItem>& glass, const std::vector<DrawItem>& ghosts, const Overlay& ov) {
  auto c0 = std::chrono::high_resolution_clock::now();
  stats.draws = 0; stats.tris = 0;
  time += v.dt;
  if (qvalid) { GLuint64 ns = 0; glGetQueryObjectui64v(queries[qidx ^ 1], GL_QUERY_RESULT, &ns); stats.gpuMs = ns / 1e6; }
  glBeginQuery(GL_TIME_ELAPSED, queries[qidx]);

  // ---- shadow map: tighter around the camera when walking, wider while flying
  float camH = std::max(0.0f, v.camPos.y - t.height(v.camPos.x, v.camPos.z));
  float radius = v.editor ? std::clamp(60.0f + camH * 1.2f, 60.0f, 260.0f) : 55.0f;
  vec3 fwd = -vec3(glm::inverse(v.view)[2]);
  vec3 focus = v.camPos + vec3(fwd.x, 0, fwd.z) * radius * 0.5f;
  focus.y = t.height(focus.x, focus.z);
  mat4 lv;
  lightVP = stableOrtho(focus, sunDir, radius, 400.0f, shadowSize, lv);
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo);
  glViewport(0, 0, shadowSize, shadowSize);
  glClear(GL_DEPTH_BUFFER_BIT);
  glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(1.5f, 2.0f);
  glDisable(GL_CULL_FACE);
  glUseProgram(progShadow);
  glUniformMatrix4fv(U(progShadow, "uVP"), 1, GL_FALSE, glm::value_ptr(lightVP));
  drawItems(progShadow, opaque, true);
  if (!foliage.empty() && !thumbMode) drawFoliage(progLeafShadow, lightVP);
  glUseProgram(progShadow);
  if (!worldModels.empty()) drawModelsDepth(progShadow, worldModels);
  // ---- sky occlusion (top down)
  float occR = 90.0f;
  vec3 ofocus = v.camPos; occTop = std::max(v.camPos.y, 0.0f) + 60.0f; occRange = 200.0f;
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
  glUniformMatrix4fv(U(progShadow, "uVP"), 1, GL_FALSE, glm::value_ptr(occVP));
  drawItems(progShadow, opaque, true);
  if (!foliage.empty() && !thumbMode) drawFoliage(progLeafShadow, occVP);
  glUseProgram(progShadow); glUniformMatrix4fv(U(progShadow, "uVP"), 1, GL_FALSE, glm::value_ptr(occVP));
  if (!worldModels.empty()) drawModelsDepth(progShadow, worldModels);
  glDisable(GL_POLYGON_OFFSET_FILL);

  // ---- main HDR pass
  mat4 vp = v.proj * v.view;
  glBindFramebuffer(GL_FRAMEBUFFER, hdrFbo);
  glViewport(0, 0, width, height);
  glClearColor(0.7f, 0.73f, 0.75f, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  // sky
  setLighting(progSky, v);
  glUniformMatrix4fv(U(progSky, "uInvVP"), 1, GL_FALSE, glm::value_ptr(glm::inverse(vp)));
  glUniform1i(U(progSky, "uCloudOn"), clouds ? 1 : 0); glUniform1f(U(progSky, "uCloudCover"), cloudCover);
  glDepthFunc(GL_LEQUAL); glDepthMask(GL_FALSE);
  glBindVertexArray(emptyVao); glDrawArrays(GL_TRIANGLES, 0, 3);
  glDepthMask(GL_TRUE); glDepthFunc(GL_LESS);
  // terrain
  setLighting(progTerrain, v);
  glUniformMatrix4fv(U(progTerrain, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
  glUniform1i(U(progTerrain, "uGrid"), v.grid ? 1 : 0);
  glUniform1f(U(progTerrain, "uBuildHalf"), t.buildHalf);
  glEnable(GL_CULL_FACE); glCullFace(GL_BACK);
  t.draw(progTerrain);
  // opaque
  glEnable(GL_CULL_FACE); // all authored solids are closed with outward winding
  setLighting(progMain, v);
  glUniform1i(U(progMain, "uWeather"), thumbMode ? 0 : 1);
  glUniformMatrix4fv(U(progMain, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
  drawItems(progMain, opaque, false);
  if (!worldModels.empty()) {   // imported models placed in the world
    glDisable(GL_CULL_FACE);
    setLighting(progModel, v);
    glUniformMatrix4fv(U(progModel, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
    drawModels(worldModels);
    glEnable(GL_CULL_FACE);
  }
  // tree canopies (double sided, alpha tested)
  if (!foliage.empty()) {
    glDisable(GL_CULL_FACE);
    setLighting(progLeaf, v);
    drawFoliage(progLeaf, vp);
    glEnable(GL_CULL_FACE);
  }
  // grass after opaque so buildings reject hidden blades early; double sided
  if (grass && !thumbMode) {
    glDisable(GL_CULL_FACE);
    setLighting(progGrass, v);
    glUniformMatrix4fv(U(progGrass, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
    grass->draw(progGrass, vp, v.camPos);
    stats.draws += grass->lastDrawnChunks; stats.tris += (int)grass->lastDrawnBlades * 8;
    glEnable(GL_CULL_FACE);
  }
  // transparent: glass then ghosts
  glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);
  glDisable(GL_CULL_FACE);
  if (!glass.empty()) {
    std::vector<DrawItem> sorted = glass;
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
    drawItems(progMain, ghosts, false);
  }
  // effects: smoke (alpha) then flashes / tracers / streaks (additive), depth-tested, no depth writes
  if (!fxAlpha.empty() || !fxAdd.empty()) {
    glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
    glUseProgram(progFx);
    glUniformMatrix4fv(U(progFx, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
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
  }

  // ---- exposure
  glGenerateTextureMipmap(hdrColor);
  int levels = (int)std::floor(std::log2((float)std::max(width, height)));
  glBindFramebuffer(GL_FRAMEBUFFER, expoFbo[expoIdx ^ 1]);
  glViewport(0, 0, 1, 1);
  glDisable(GL_DEPTH_TEST);
  glUseProgram(progExpo);
  glBindTextureUnit(0, hdrColor); glBindTextureUnit(1, expoTex[expoIdx]);
  glUniform1i(U(progExpo, "uHdr"), 0); glUniform1i(U(progExpo, "uPrev"), 1);
  glUniform1f(U(progExpo, "uLod"), (float)levels); glUniform1f(U(progExpo, "uDt"), v.dt);
  glUniform1f(U(progExpo, "uReset"), expoReset); expoReset = -1;
  glBindVertexArray(emptyVao); glDrawArrays(GL_TRIANGLES, 0, 3);
  expoIdx ^= 1;

  // ---- tonemap into LDR (shares depth for overlays)
  glBindFramebuffer(GL_FRAMEBUFFER, ldrFbo);
  glViewport(0, 0, width, height);
  glUseProgram(progTone);
  glBindTextureUnit(0, hdrColor); glBindTextureUnit(1, expoTex[expoIdx]);
  glUniform1i(U(progTone, "uHdr"), 0); glUniform1i(U(progTone, "uExpo"), 1);
  glUniform1f(U(progTone, "uFixedExpo"), v.autoExposure ? -1.0f : 1.0f);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  // ---- editor overlays (separate pass; never drawn in Explore)
  if (v.editor && (!ov.lines.empty() || !ov.tris.empty() || !ov.linesTop.empty())) {
    glUseProgram(progLine);
    glUniformMatrix4fv(U(progLine, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
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
  glBlitNamedFramebuffer(ldrFbo, 0, 0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glEndQuery(GL_TIME_ELAPSED);
  qidx ^= 1; qvalid = true;
  stats.cpuMs = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - c0).count();
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

GLuint Renderer::renderThumbnail(const std::vector<DrawItem>& opaque, const std::vector<DrawItem>& glass, vec3 bmin, vec3 bmax, const Terrain* terr, const mat4* customView) {
  const int S = 256;
  GLuint outTex, outFbo;
  glCreateTextures(GL_TEXTURE_2D, 1, &outTex); glTextureStorage2D(outTex, 1, GL_RGBA8, S, S);
  glTextureParameteri(outTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(outTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glCreateFramebuffers(1, &outFbo); glNamedFramebufferTexture(outFbo, GL_COLOR_ATTACHMENT0, outTex, 0);
  vec3 c = (bmin + bmax) * 0.5f; float r = glm::length(bmax - bmin) * 0.5f;
  RenderView v;
  v.camPos = c + glm::normalize(vec3(0.75f, 0.45f, 1.0f)) * r * 2.1f;
  v.view = customView ? *customView : glm::lookAt(v.camPos, c, vec3(0, 1, 0));
  if (customView) v.camPos = vec3(glm::inverse(*customView)[3]);
  v.proj = glm::perspective(radians(32.0f), 1.0f, 0.1f, 2000.0f);
  thumbMode = true;
  mat4 vp = v.proj * v.view;
  glBindFramebuffer(GL_FRAMEBUFFER, thumbHdrFbo);
  glViewport(0, 0, S, S);
  glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
  glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  setLighting(progSky, v);
  glUniformMatrix4fv(U(progSky, "uInvVP"), 1, GL_FALSE, glm::value_ptr(glm::inverse(vp)));
  glDepthFunc(GL_LEQUAL); glDepthMask(GL_FALSE);
  glBindVertexArray(emptyVao); glDrawArrays(GL_TRIANGLES, 0, 3);
  glDepthMask(GL_TRUE); glDepthFunc(GL_LESS);
  if (terr) {
    setLighting(progTerrain, v);
    glUniformMatrix4fv(U(progTerrain, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
    glUniform1i(U(progTerrain, "uGrid"), 0);
    terr->draw(progTerrain);
  }
  setLighting(progMain, v);
  glUniformMatrix4fv(U(progMain, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
  drawItems(progMain, opaque, false);
  if (!foliage.empty()) { setLighting(progLeaf, v); drawFoliage(progLeaf, vp); }
  glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE);
  setLighting(progGlass, v);
  glUniformMatrix4fv(U(progGlass, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
  drawItems(progGlass, glass, false);
  glDepthMask(GL_TRUE); glDisable(GL_BLEND);
  glBindFramebuffer(GL_FRAMEBUFFER, outFbo);
  glDisable(GL_DEPTH_TEST);
  glUseProgram(progTone);
  glBindTextureUnit(0, thumbHdr); glBindTextureUnit(1, expoTex[0]);
  glUniform1i(U(progTone, "uHdr"), 0); glUniform1i(U(progTone, "uExpo"), 1);
  glUniform1f(U(progTone, "uFixedExpo"), 1.1f);
  glBindVertexArray(emptyVao); glDrawArrays(GL_TRIANGLES, 0, 3);
  glEnable(GL_DEPTH_TEST);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &outFbo);
  thumbMode = false;
  return outTex;
}
