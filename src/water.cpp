// Water pass (V1, docs/rendering.md#water). See water.h. Surfaces come from the asset "water" block:
//   { "surfaces": [ { "outline": [[x, z], ...], "level": y, "kind": "pond|stream|puddle|crater|sea",
//                     "path": [[x, z], ...], "speed": m/s, "clarity": x, "waves": x, "tint": [r, g, b] } ] }
// The outline is triangulated once per asset (ear clipping); streams store a per-vertex flow vector from the nearest
// segment of their centreline. The pass runs inside the reverse-Z world pass after the opaque geometry: it copies the lit
// colour and the depth, then draws every surface with depth test and depth write, into the colour, indirect (zero:
// no SSAO on water), normal + roughness (SSR in the world composite) and motion (static: TAA reprojects from depth)
// attachments.
#include "water.h"
#include "app.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <unordered_map>

namespace {
struct WSurf {
  GLuint vao = 0, vbo = 0; int count = 0;
  int kind = 1; float clarity = 1, waves = 1, ice = 0; vec3 tint{0}; bool hasTint = false;
  float level = 0, area = 0; vec2 bmin{1e9f}, bmax{-1e9f}; bool flows = false;
  std::vector<vec2> poly;
  std::vector<vec2> gpath; std::vector<float> glev;   // BT1 ribbon: the graded centreline (level per point)
};
struct WAsset { std::vector<WSurf> s; bool gpu = false; };
std::unordered_map<const AssetDef*, WAsset> gCache;
WaterStats gStats;

int kindOf(const std::string& k) { return k == "sea" ? 0 : k == "stream" ? 2 : k == "puddle" ? 3 : k == "crater" ? 4 : 1; }
const char* kindName(int k) { static const char* n[] = {"sea", "pond", "stream", "puddle", "crater"}; return n[std::clamp(k, 0, 4)]; }
float cross2(vec2 a, vec2 b, vec2 c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); }
float signedArea(const std::vector<vec2>& p) { float s = 0; for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) s += (p[j].x - p[i].x) * (p[j].y + p[i].y); return s * 0.5f; }
// ear clipping of a simple polygon in the xz plane. Triangles come out counter-clockwise seen from below (x right,
// z down the screen); the pass draws both faces, so only consistency matters.
std::vector<vec2> triangulate(std::vector<vec2> P) {
  std::vector<vec2> out;
  if (P.size() < 3) return out;
  if (signedArea(P) < 0) std::reverse(P.begin(), P.end());
  std::vector<int> idx(P.size()); for (size_t i = 0; i < P.size(); ++i) idx[i] = (int)i;
  auto inside = [&](vec2 p, vec2 a, vec2 b, vec2 c) { return cross2(a, b, p) >= 0 && cross2(b, c, p) >= 0 && cross2(c, a, p) >= 0; };
  int guard = 0;
  while (idx.size() > 3 && guard++ < 100000) {
    bool cut = false;
    for (size_t k = 0; k < idx.size(); ++k) {
      int i0 = idx[(k + idx.size() - 1) % idx.size()], i1 = idx[k], i2 = idx[(k + 1) % idx.size()];
      vec2 a = P[i0], b = P[i1], c = P[i2];
      if (cross2(a, b, c) <= 1e-9f) continue;
      bool any = false;
      for (int j : idx) if (j != i0 && j != i1 && j != i2 && inside(P[j], a, b, c)) { any = true; break; }
      if (any) continue;
      out.push_back(a); out.push_back(b); out.push_back(c);
      idx.erase(idx.begin() + (long)k); cut = true; break;
    }
    if (!cut) break;   // degenerate remainder: drop it
  }
  if (idx.size() == 3) for (int i : idx) out.push_back(P[i]);
  return out;
}
bool inPoly(const std::vector<vec2>& p, vec2 q) {
  bool in = false;
  for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++)
    if (((p[i].y > q.y) != (p[j].y > q.y)) && (q.x < (p[j].x - p[i].x) * (q.y - p[i].y) / (p[j].y - p[i].y) + p[i].x)) in = !in;
  return in;
}
WAsset& parse(const AssetDef* a) {
  auto it = gCache.find(a);
  if (it != gCache.end()) return it->second;
  WAsset& w = gCache[a];
  if (!a || !a->raw.is_object() || !a->raw.contains("water")) return w;
  for (auto& sj : jkey(a->raw, "water").value("surfaces", json::array())) {
    WSurf s;
    for (auto& p : sj.value("outline", json::array())) if (p.is_array() && p.size() >= 2) s.poly.push_back(vec2(p[0].get<float>(), p[1].get<float>()));
    if (s.poly.size() < 3) continue;
    s.level = sj.value("level", 0.0f); s.kind = kindOf(sj.value("kind", std::string("pond")));
    s.clarity = sj.value("clarity", 1.0f); s.waves = sj.value("waves", 1.0f);
    s.ice = std::clamp(sj.value("ice", 0.0f), 0.0f, 1.0f);   // BT1: thin shore ice (0 open water .. 1 a frozen skin)
    if (sj.contains("tint") && sj["tint"].is_array() && sj["tint"].size() >= 3) { s.tint = vec3(sj["tint"][0].get<float>(), sj["tint"][1].get<float>(), sj["tint"][2].get<float>()); s.hasTint = true; }
    for (auto& p : s.poly) { s.bmin = glm::min(s.bmin, p); s.bmax = glm::max(s.bmax, p); }
    s.area = std::fabs(signedArea(s.poly));
    s.flows = sj.contains("path") && sj["path"].size() >= 2;
    if (s.flows) {
      for (auto& p : sj["path"]) if (p.is_array() && p.size() >= 3) { s.gpath.push_back(vec2(p[0].get<float>(), p[1].get<float>())); s.glev.push_back(p[2].get<float>()); }
      if (s.glev.size() != sj["path"].size()) { s.gpath.clear(); s.glev.clear(); }
      if (!s.glev.empty()) s.level = *std::max_element(s.glev.begin(), s.glev.end());   // the top of the reach (bounds)
    }
    w.s.push_back(std::move(s));
  }
  return w;
}
void upload(const AssetDef* a, WAsset& w) {
  if (w.gpu) return;
  w.gpu = true;
  const json* surfs = a->raw.contains("water") ? &a->raw["water"] : nullptr;
  for (size_t k = 0; k < w.s.size(); ++k) {
    WSurf& s = w.s[k];
    std::vector<vec2> path; std::vector<float> plev; float speed = 0.5f, halfW = 0.0f;
    if (s.flows && surfs) {
      const json& sj = (*surfs)["surfaces"][k];
      for (auto& p : sj["path"]) { path.push_back(vec2(p[0].get<float>(), p[1].get<float>())); if (p.size() >= 3) plev.push_back(p[2].get<float>()); }
      if (plev.size() != path.size()) plev.clear();   // BT1 ribbon: [x, z, level] points grade the surface along the stream
      speed = sj.value("speed", 0.5f);
      halfW = sj.value("halfWidth", 0.0f);   // BT1: the current slows toward the banks (still edge pockets)
    }
    auto levelAt = [&](vec2 q) {
      if (plev.empty()) return s.level;
      float best = 1e30f, lv = s.level;
      for (size_t i = 0; i + 1 < path.size(); ++i) {
        vec2 ab = path[i + 1] - path[i]; float L2 = glm::dot(ab, ab); if (L2 < 1e-8f) continue;
        float t = std::clamp(glm::dot(q - path[i], ab) / L2, 0.0f, 1.0f);
        float d = glm::length(q - (path[i] + ab * t));
        if (d < best) { best = d; lv = plev[i] + (plev[i + 1] - plev[i]) * t; }
      }
      return lv;
    };
    auto flowAt = [&](vec2 q) {
      if (path.size() < 2) return vec2(0);
      float best = 1e30f; vec2 dir(0);
      for (size_t i = 0; i + 1 < path.size(); ++i) {
        vec2 ab = path[i + 1] - path[i]; float L2 = glm::dot(ab, ab); if (L2 < 1e-8f) continue;
        float t = std::clamp(glm::dot(q - path[i], ab) / L2, 0.0f, 1.0f);
        float d = glm::length(q - (path[i] + ab * t));
        if (d < best) { best = d; dir = ab / std::sqrt(L2); }
      }
      if (halfW > 0.0f) return dir * speed * std::clamp(1.0f - best / halfW, 0.0f, 1.0f);
      return dir * speed;
    };
    std::vector<vec2> tri = triangulate(s.poly);
    if (!plev.empty()) {   // a graded ribbon: split long triangles so the level follows the path (<= 2 m edges)
      std::vector<vec2> fine;
      for (size_t t = 0; t + 2 < tri.size() + 0; t += 3) {
        std::vector<std::array<vec2, 3>> st{{tri[t], tri[t + 1], tri[t + 2]}};
        for (int it = 0; it < 6; ++it) {
          std::vector<std::array<vec2, 3>> nx;
          for (auto& T : st) {
            float l0 = glm::length(T[0] - T[1]), l1 = glm::length(T[1] - T[2]), l2 = glm::length(T[2] - T[0]);
            float m = std::max(l0, std::max(l1, l2));
            if (m <= 2.0f) { nx.push_back(T); continue; }
            int e = m == l0 ? 0 : m == l1 ? 1 : 2;
            vec2 A = T[e], B = T[(e + 1) % 3], C = T[(e + 2) % 3], M = (A + B) * 0.5f;
            nx.push_back({A, M, C}); nx.push_back({M, B, C});
          }
          st.swap(nx);
        }
        for (auto& T : st) { fine.push_back(T[0]); fine.push_back(T[1]); fine.push_back(T[2]); }
      }
      tri.swap(fine);
    }
    std::vector<float> v; v.reserve(tri.size() * 5);
    for (vec2 p : tri) { vec2 f = flowAt(p); v.insert(v.end(), {p.x, levelAt(p), p.y, f.x, f.y}); }
    s.count = (int)tri.size();
    glCreateBuffers(1, &s.vbo); glNamedBufferData(s.vbo, (GLsizeiptr)(v.size() * sizeof(float)), v.data(), GL_STATIC_DRAW);
    glCreateVertexArrays(1, &s.vao);
    glVertexArrayVertexBuffer(s.vao, 0, s.vbo, 0, 5 * sizeof(float));
    glEnableVertexArrayAttrib(s.vao, 0); glVertexArrayAttribFormat(s.vao, 0, 3, GL_FLOAT, GL_FALSE, 0); glVertexArrayAttribBinding(s.vao, 0, 0);
    glEnableVertexArrayAttrib(s.vao, 1); glVertexArrayAttribFormat(s.vao, 1, 2, GL_FLOAT, GL_FALSE, 3 * sizeof(float)); glVertexArrayAttribBinding(s.vao, 1, 0);
  }
}

// ---------------------------------------------------------------- shaders (kCommon is prepended: sky, fog, shadows)
const char* kWaterVS = R"GLSL(
layout(location=0) in vec3 aPos; layout(location=1) in vec2 aFlow;
uniform mat4 uModel, uVP;
out vec3 vW; out vec2 vFlow;
void main(){
  vec4 w = uModel*vec4(aPos, 1.0);
  vW = w.xyz;
  vFlow = (mat3(uModel)*vec3(aFlow.x, 0.0, aFlow.y)).xz;   // world flow (m/s)
  gl_Position = uVP*w;
}
)GLSL";
// Surface model. Slopes of the height field: a few directional wave trains (deep-water dispersion w = sqrt(g k)) plus
// value-noise ripples, advected along the flow for streams (two phases half a period apart, cross-faded: no stretching).
// Every component whose wavelength nears two pixels fades out and its slope variance goes into the roughness instead
// (no sparkle at range). Optics: Schlick Fresnel; reflection = sky (scaled by the sky-occlusion map) + GGX sun glint in
// the sun shadow; transmission = the refracted bed from the colour copy, attenuated per channel by the path length
// through the water (from the depth copy); in-scattering by the water's own colour, lit by sky and sun. Fog is applied
// once: the bed copy already carries it. Within a few centimetres of depth the surface fades into the bed (shore).
const char* kWaterFS = R"GLSL(
in vec3 vW; in vec2 vFlow;
uniform sampler2D uSceneCopy, uDepthCopy;
uniform vec2 uSize; uniform float uZA, uZB, uTime;
uniform vec4 uWLook;  // kind (0 sea, 1 pond, 2 stream, 3 puddle, 4 crater), clarity, wave scale, ice (BT1: 0..1)
uniform int uWAbs;     // CP step 7 (perf switch wabs): extinction by absorption + scattering
uniform vec4 uTint;   // optional water colour (rgb), a = 1 when set
layout(location=0) out vec4 oColor;
layout(location=2) out vec4 oAmb;
layout(location=3) out vec4 oNR;
layout(location=4) out vec4 oVel;
float gPx = 0.001;
float resolved(float freq){ return 1.0 - smoothstep(0.15, 0.45, freq*gPx); }
vec2 noiseSlope(vec2 p, float f, float t){
  vec3 q = vec3(p*f, t);
  float hx = vnoise(q + vec3(0.25, 0.0, 0.0)) - vnoise(q - vec3(0.25, 0.0, 0.0));
  float hz = vnoise(q + vec3(0.0, 0.25, 0.0)) - vnoise(q - vec3(0.0, 0.25, 0.0));
  return vec2(hx, hz)*2.0*f;   // d(noise)/d(metre)
}
void main(){
  gPx = max(length(dFdx(vW)), length(dFdy(vW)));
  int kind = int(uWLook.x + 0.5);
  float wscale = uWLook.z;
  // ---- slopes
  vec2 p = vW.xz, g = vec2(0.0); float lost = 0.0;   // lost: slope variance faded out (-> roughness)
  float L0 = kind == 0 ? 7.0 : kind == 3 ? 0.45 : kind == 2 ? 0.9 : 1.1;     // longest wave (m)
  float S0 = kind == 0 ? 0.10 : kind == 3 ? 0.015 : kind == 2 ? 0.04 : 0.028; // its steepness (slope amplitude)
  for (int i = 0; i < 5; ++i) {
    float fi = float(i);
    float ang = 0.7 + fi*2.39 + 0.35*sin(fi*7.1 + 0.3*float(kind));
    vec2 d = vec2(cos(ang), sin(ang));
    float L = L0*pow(0.62, fi), k = 6.2831853/L, w = sqrt(9.81*k);
    float s = S0*pow(0.78, fi)*wscale, r = resolved(1.0/L);
    g += s*r*cos(dot(d, p)*k - w*uTime*0.6 + fi*1.7)*d;
    lost += s*s*(1.0 - r)*0.5;
  }
  vec2 fl = vFlow;
  float fs = length(fl);
  if (fs > 1e-3) {   // streams: ripples carried by the current, elongated across it
    float ph = fract(uTime*0.35), w0 = 1.0 - abs(2.0*ph - 1.0);
    for (int o = 0; o < 2; ++o) {
      float f = o == 0 ? 2.2 : 6.5, a = (o == 0 ? 0.05 : 0.03)*wscale, r = resolved(f);
      vec2 g0 = noiseSlope(p - fl*ph*2.86, f, 3.0 + float(o)), g1 = noiseSlope(p - fl*fract(ph + 0.5)*2.86 + 0.37, f, 7.0 + float(o));
      g += (g0*w0 + g1*(1.0 - w0))*a/f*r;
      lost += a*a*(1.0 - r)*0.5;
    }
  } else {           // still water: wind ripples drifting slowly
    vec2 wind = vec2(0.31, 0.17)*uTime;
    for (int o = 0; o < 2; ++o) {
      float f = o == 0 ? 2.6 : 8.0, a = (o == 0 ? 0.045 : 0.025)*wscale*(kind == 0 ? 1.4 : 1.0), r = resolved(f);
      g += noiseSlope(p + wind*(1.0 + float(o)), f, uTime*0.12 + float(o)*5.0)*a/f*r;
      lost += a*a*(1.0 - r)*0.5;
    }
  }
  vec3 N = normalize(vec3(-g.x, 1.0, -g.y));
  vec3 V = normalize(uCamPos - vW);
  bool under = V.y < 0.0;
  if (under) N = -N;
  float rough = clamp(sqrt(0.0016 + lost*2.0) + 0.02, 0.03, 0.35);
  // BT1 thin ice (spec 4: roughness 0.16-0.4, dark water seen through shallow margins, sparse hairline fractures): a
  // patchy frozen skin, flat (no waves under it), rougher, paler; open leads stay water
  float iceK = 0.0, crack = 0.0;
  if (uWLook.w > 0.0) {
    float pn = vnoise(vec3(vW.xz*0.45, 61.0))*0.7 + vnoise(vec3(vW.xz*1.9, 62.0))*0.3;
    iceK = smoothstep(0.55, 0.62, pn*uWLook.w + 0.3*uWLook.w);
    iceK *= 1.0 - smoothstep(0.06, 0.2, length(vFlow));   // only where the water is still (stream edges, pools)
    crack = (1.0 - smoothstep(0.0, 0.035, abs(vnoise(vec3(vW.xz*0.9, 63.0)) - 0.5)))*iceK;
    N = normalize(mix(N, vec3(0.0, under ? -1.0 : 1.0, 0.0), iceK));
    rough = mix(rough, mix(0.18, 0.36, vnoise(vec3(vW.xz*3.0, 64.0))), iceK);
  }
  // ---- depths: reverse-Z, view depth d = B / (z + A)
  vec2 suv = gl_FragCoord.xy/uSize;
  float dW = 1.0/gl_FragCoord.w;
  float zS = texture(uDepthCopy, suv).r;
  float dS = zS > 0.0 ? uZB/(zS + uZA) : 1e5;
  float rayK = length(vW - uCamPos)/max(dW, 1e-3);
  float thick = clamp((dS - dW)*rayK, 0.0, 60.0);       // metres of water along the view ray
  float depthV = thick*abs(V.y);                         // about the vertical depth there
  // ---- refraction: offset the bed lookup by the surface slope, more for deeper water, less far away
  vec2 off = N.xz*0.045*clamp(thick, 0.0, 1.5)*clamp(6.0/max(dW, 6.0), 0.0, 1.0);
  vec2 ruv = clamp(suv + off, vec2(0.001), vec2(0.999));
  if (texture(uDepthCopy, ruv).r > gl_FragCoord.z) ruv = suv;   // the offset landed on something in front of the water
  vec3 bed = textureLod(uSceneCopy, ruv, 0.0).rgb;
  vec3 dry = textureLod(uSceneCopy, suv, 0.0).rgb;
  // ---- water body: scattering colour and extinction per kind (1/m)
  vec3 sc; float ext;
  if (kind == 0) { sc = vec3(0.020, 0.062, 0.068); ext = 0.5; }        // Channel shallows: blue-green
  else if (kind == 2) { sc = vec3(0.030, 0.042, 0.026); ext = 1.2; }   // brook: tea-clear over gravel
  else if (kind == 3) { sc = vec3(0.075, 0.064, 0.048); ext = 7.0; }   // puddle: muddy
  else if (kind == 4) { sc = vec3(0.065, 0.062, 0.050); ext = 3.2; }   // crater pool: clay-grey
  else { sc = vec3(0.026, 0.040, 0.024); ext = 2.2; }                  // pond: peaty green
  if (uTint.a > 0.5) sc = uTint.rgb;
  ext /= max(uWLook.y, 0.05);
  vec3 T = exp(-ext*vec3(1.35, 1.0, 0.85)*thick);        // red is absorbed first
  // CP step 7 (perf switch wabs; corpus 11.7): extinction from what is in the water instead of a tint. Pure water absorbs
  // (0.340, 0.0565, 0.0092) /m at 650 / 550 / 450 nm (Pope & Fry 1997); dissolved organics (CDOM: tea, peat) absorb blue
  // most, silt scatters grey. Each kind: CDOM amount, silt scattering (1/m). What the bed shows is exp(-sigma_t d); what
  // the water itself shows is the scattered share sigma_s / sigma_t of the light, so its colour follows the physics.
  vec3 wOmega = vec3(0.0);
  if (uWAbs == 1 && uTint.a <= 0.5) {
    vec2 ks = kind == 0 ? vec2(0.02, 0.25) : kind == 2 ? vec2(0.45, 0.5) : kind == 3 ? vec2(1.0, 6.0) : kind == 4 ? vec2(0.5, 3.0) : vec2(1.2, 1.2);
    vec3 sa = vec3(0.340, 0.0565, 0.0092) + ks.x*vec3(0.15, 0.42, 1.0), st = sa + ks.y;
    T = exp(-st*thick/max(uWLook.y, 0.05));
    wOmega = ks.y/st;   // single-scattering albedo per channel
  }
  // ---- light
  vec3 up = vec3(0.0, 1.0, 0.0);
  float sh = shadowPCF(vW, up)*cloudShadow(vW);
  float sv = skyVis(vW, up);
  vec3 lightIn = giAmbient(vW, up, sv, uSkyCol*mix(0.35, 1.0, sv)) + uSunCol*max(uSunDir.y, 0.0)*sh*0.6;
  vec3 Lin = sc*lightIn;   // L2: probe light by night / under cover
  float NdV = max(dot(N, V), 1e-3);
  float F = 0.02 + 0.98*pow(1.0 - NdV, 5.0);
  vec3 R = reflect(-V, N); R.y = abs(R.y);
  vec3 env = skyColor(R)*mix(0.3, 1.0, sv);
  vec3 L = uSunDir, H = normalize(V + L);
  float NdL = max(dot(N, L), 0.0), NdH = max(dot(N, H), 0.0), a = max(rough*rough, 0.0009);
  vec3 spec = D_GGX(NdH, a)*G_Smith(NdV, NdL, rough)*vec3(F_Schlick(max(dot(H, V), 0.0), vec3(0.02)))/(4.0*NdV*max(NdL, 1e-3))*uSunCol*NdL*sh;
  if (uLightCount > 0) { vec3 ld, ls; localLights(vW, N, up, V, rough, vec3(0.02), ld, ls); Lin += sc*ld*0.6; lightIn += ld*0.6; spec += ls; }   // L2: lamps on the water
  if (under) { env = Lin*2.0; spec = vec3(0.0); }
  if (uWAbs == 1 && uTint.a <= 0.5) Lin = wOmega*0.1*lightIn;   // the scattered share of the light (~10 % goes back up), no tint
  vec3 own = F*env + spec + (1.0 - F)*(1.0 - T)*Lin;
  vec3 trans = (1.0 - F)*T;
  vec3 fogAdd = applyFog(vec3(0.0), vW);                 // the haze colour times its share; sets gFogT
  vec3 col = trans*bed + own*gFogT + fogAdd*(1.0 - trans);
  // ---- shore: the last centimetres fade into the bed, a wet sheen just inside the line; foam for sea / brook
  // WT1: still ponds fade over ~25 cm of depth, reflection and glint included: a 4 cm fade left the sky reflection at full
  // strength in water a few centimetres deep, so the waterline (the coarse terrain's straight facets) read as a bright rim
  float shoreW = kind == 0 ? 0.08 : kind == 3 ? 0.012 : (kind == 1 || kind == 4) ? 0.25 : 0.04;
  float shore = smoothstep(0.0, shoreW, depthV);
  if (kind == 1 || kind == 4) {   // an irregular waterline: the fade depth wanders by +-8 cm (~1.5 m scale), so the terrain's straight facets do not draw the edge
    shore = smoothstep(0.0, shoreW, depthV + (vnoise(vec3(vW.xz*0.7, 3.1)) - 0.5)*0.16 + (vnoise(vec3(vW.xz*2.3, 5.7)) - 0.5)*0.05);
    float wd = mix(1.0, 0.75, smoothstep(0.0, 0.08, depthV)); bed *= wd; dry *= wd;   // WT2: the bed reads as wet mud from the waterline down; in the shore band the water mirrors the bank behind it, not
    // the sky (the SSR that swaps the sky for the bank in deeper water is off there, so the sky estimate drew a pale line along the far shore)
    float sr = shore*shore; own = F*mix(dry, env, smoothstep(0.75, 1.0, shore))*sr + spec*sr + (1.0 - F)*(1.0 - T)*Lin; col = trans*bed + own*gFogT + fogAdd*(1.0 - trans); }
  if (kind == 0 || kind == 2) {
    float foam = (1.0 - smoothstep(0.0, kind == 0 ? 0.25 : 0.1, depthV))*smoothstep(0.5, 0.85, vnoise(vec3(vW.xz*2.5, uTime*0.5)))*shore;
    col += foam*0.22*(uSkyCol*0.6 + uSunCol*sh*0.25)*gFogT;
  }
  if (iceK > 0.0) {   // the frozen skin: a pale, slightly milky layer lit by the sky, the dark water through its clear parts
    vec3 iceAlb = mix(vec3(0.30, 0.34, 0.37), vec3(0.62, 0.66, 0.70), vnoise(vec3(vW.xz*2.2, 65.0)));
    vec3 iceCol = iceAlb*(giAmbient(vW, up, sv, uSkyCol*mix(0.35, 1.0, sv)) + uSunCol*max(uSunDir.y, 0.0)*sh*0.5)*gFogT + fogAdd;
    col = mix(col, iceCol + F*env*gFogT*0.5, iceK*0.75);
    col = mix(col, col*0.55, crack*0.6);
  }
  col = mix(dry, col, shore);
  col = (any(isnan(col)) || any(isinf(col))) ? dry : max(col, vec3(0.0));
  oColor = vec4(col, 1.0);
  oAmb = vec4(0.0);
  oNR = vec4(N, mix(1.0, rough, (kind == 1 || kind == 4) ? shore*shore : shore) + (1.0 - gFogT));   // WT1: no SSR sky glint in the last few cm of a pond   // smooth: the world composite adds SSR (hazed-out: none)
  oVel = vec4(0.0);
}
)GLSL";
}  // namespace

bool waterAsset(const AssetDef* a) { return a && a->raw.is_object() && a->raw.contains("water"); }

std::vector<WaterSurfaceInfo> waterSurfaces(const AssetDef* a) {
  std::vector<WaterSurfaceInfo> out;
  if (!waterAsset(a)) return out;
  for (auto& s : parse(a).s) {
    WaterSurfaceInfo i; i.kind = kindName(s.kind); i.level = s.level; i.area = s.area; i.tris = (int)(triangulate(s.poly).size() / 3);
    i.flows = s.flows; i.bmin = s.bmin; i.bmax = s.bmax;
    out.push_back(i);
  }
  return out;
}

bool waterDrawItems(App& app, const Instance& i, const InstanceRT& r, const mat4& M, std::vector<DrawItem>& op) {
  (void)i;
  const AssetDef* a = r.asset;
  if (!waterAsset(a)) return false;
  if (a->opaque.count) op.push_back({&a->opaque, M});   // banks: reeds, stones, crater rim
  vec3 cam = app.mode == Mode::Explore ? app.eyePos() : app.camPos;
  vec3 c = vec3(M[3]);
  float reach = 0.5f * glm::length(a->footprint);
  if (glm::length(vec2(c.x - cam.x, c.z - cam.z)) - reach > 2500.0f) return true;
  WAsset& w = parse(a);
  upload(a, w);
  for (auto& s : w.s) if (s.count) app.R.water.push_back({s.vao, s.count, M, vec4((float)s.kind, s.clarity, s.waves, s.ice), vec4(s.tint, s.hasTint ? 1.0f : 0.0f)});
  return true;
}

uint64_t placementSig(const std::vector<Instance>& v, bool withPose);   // app.cpp
float waterSurfaceAt(const App& app, float x, float z) {
  // the water instances only: this runs per AI agent per physics step (Physics::waterQuery) and walked the whole map.
  // The list is checked against every instance's id / revision / pose once per frame, and again within a frame when
  // an instance is added, removed or re-revisioned.
  static std::vector<size_t> water; static uint64_t key = 1; static double frame = -1; static uint32_t rc = 0; static size_t n = ~(size_t)0;
  if (frame != app.now || rc != app.revCounter || n != app.doc.instances.size()) {
    frame = app.now; rc = app.revCounter; n = app.doc.instances.size();
    uint64_t k = placementSig(app.doc.instances, true);
    if (k != key) { key = k; water.clear(); for (size_t q = 0; q < app.doc.instances.size(); ++q) if (waterAsset(app.assetFor(app.doc.instances[q]))) water.push_back(q); }
  }
  float best = -1e9f;
  for (size_t q : water) {
    const Instance& inst = app.doc.instances[q];
    const AssetDef* a = app.assetFor(inst);
    mat4 inv = glm::inverse(app.instanceMatrix(inst));
    vec3 l = vec3(inv * vec4(x, inst.pos.y, z, 1.0f));
    for (auto& s : parse(a).s) {
      if (l.x < s.bmin.x || l.z < s.bmin.y || l.x > s.bmax.x || l.z > s.bmax.y) continue;
      if (inPoly(s.poly, vec2(l.x, l.z))) {
        float lv = s.level;
        if (!s.glev.empty()) {   // a graded ribbon: the level at the nearest point of its centreline
          float bd = 1e30f; vec2 q(l.x, l.z);
          for (size_t i = 0; i + 1 < s.gpath.size(); ++i) {
            vec2 ab = s.gpath[i + 1] - s.gpath[i]; float L2 = glm::dot(ab, ab); if (L2 < 1e-8f) continue;
            float t = std::clamp(glm::dot(q - s.gpath[i], ab) / L2, 0.0f, 1.0f);
            float d = glm::length(q - (s.gpath[i] + ab * t));
            if (d < bd) { bd = d; lv = s.glev[i] + (s.glev[i + 1] - s.glev[i]) * t; }
          }
        }
        best = std::max(best, inst.pos.y + lv);
      }
    }
  }
  return best;
}

const WaterStats& waterStats() { return gStats; }

struct WaterPass {
  static void run(Renderer& r, const RenderView& v, const mat4& vp) {
    static GLuint prog = 0, depCopy = 0; static int cw = 0, ch = 0;
    auto t0 = std::chrono::high_resolution_clock::now();
    gStats = WaterStats{};
    if (!prog) prog = r.compile(kWaterVS, kWaterFS, "water");
    if (!depCopy || cw != r.width || ch != r.height) {
      if (depCopy) glDeleteTextures(1, &depCopy);
      glCreateTextures(GL_TEXTURE_2D, 1, &depCopy); glTextureStorage2D(depCopy, 1, GL_DEPTH_COMPONENT32F, r.width, r.height);
      glTextureParameteri(depCopy, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTextureParameteri(depCopy, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      cw = r.width; ch = r.height;
    }
    // the lit opaque world and its depth, read by the surface (sceneCopy is free until the world composite reuses it)
    glCopyImageSubData(r.hdrColor, GL_TEXTURE_2D, 0, 0, 0, 0, r.sceneCopy, GL_TEXTURE_2D, 0, 0, 0, 0, r.width, r.height, 1);
    glCopyImageSubData(r.depthTex, GL_TEXTURE_2D, 0, 0, 0, 0, depCopy, GL_TEXTURE_2D, 0, 0, 0, 0, r.width, r.height, 1);
    r.setLighting(prog, v);
    glUseProgram(prog);
    auto U = [&](const char* n) { return glGetUniformLocation(prog, n); };
    glUniformMatrix4fv(U("uVP"), 1, GL_FALSE, glm::value_ptr(vp));
    glUniform1f(U("uTime"), r.time);
    glUniform1i(U("uWAbs"), r.perf.wAbs && !r.q.legacy ? 1 : 0);   // CP step 7
    glUniform2f(U("uSize"), (float)r.width, (float)r.height);
    glUniform1f(U("uZA"), v.proj[2][2]); glUniform1f(U("uZB"), v.proj[3][2]);   // reverse-Z: view depth = B / (z + A)
    glBindTextureUnit(8, r.sceneCopy); glUniform1i(U("uSceneCopy"), 8);
    glBindTextureUnit(9, depCopy); glUniform1i(U("uDepthCopy"), 9);
    glEnable(GL_DEPTH_TEST); glDepthFunc(r.zLess()); glDepthMask(GL_TRUE); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    GLint lm = U("uModel"), ll = U("uWLook"), lt = U("uTint");
    for (auto& d : r.water) {
      glUniformMatrix4fv(lm, 1, GL_FALSE, glm::value_ptr(d.model));
      glUniform4fv(ll, 1, glm::value_ptr(d.look)); glUniform4fv(lt, 1, glm::value_ptr(d.tint));
      glBindVertexArray(d.vao); glDrawArrays(GL_TRIANGLES, 0, d.count);
      ++gStats.draws; gStats.tris += d.count / 3;
    }
    r.stats.draws += gStats.draws; r.stats.tris += gStats.tris;
    glBindTextureUnit(8, 0); glBindTextureUnit(9, 0);
    glEnable(GL_CULL_FACE); glBindVertexArray(0);
    gStats.ms = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - t0).count();
  }
};
void waterRenderPass(Renderer& r, const RenderView& v, const mat4& vp) { WaterPass::run(r, v, vp); }
