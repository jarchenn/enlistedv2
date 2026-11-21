// SN1 (Blackwater Thaw spec 3): the shared snow layer. A map turns it on with "snow" in its file:
//   "snow": {"mask": "<id>.snow.png", "box": [x0, z0, width, depth],
//            "slope": [lo, hi], "threshold": t, "soft": s, "roof": r, "roofSlope": lo, "noise": n, "melt": m}
// The mask (RGBA8, written by the map script) holds the authored terms of
//   snow = saturate(slope_response * deposit * retention - traffic - melt)
// R = deposit x retention (sheltered yards hold more, open crests are scoured), G = traffic (lanes, door pads, wheel
// ruts), B = melt (warm sources, the creek edge), A = meltwater pooling (low pockets). The shader adds the upward
// normal response, stable world noise for the patch edges, and shelter: anything solid above the point (the top-down
// sky-occlusion map, render.cpp skyVis) keeps the ground and floors under roofs, eaves and trees clear, so roofs, wall
// tops and rails carry snow caps and the ground under them does not. Three surface states come out of it: dry snow,
// dirty slush (traffic / melt where snow would lie) and meltwater (pooled). Maps without "snow" are unchanged.
#include "app.h"
#include "render.h"
#include "snow.h"
#include <stb_image.h>

namespace {
struct SnowLayer {
  bool on = false;
  std::string key;
  GLuint tex = 0;
  int w = 0, h = 0;
  std::vector<uint8_t> px;     // CPU copy for the footstep query
  vec4 box{0}, p{0.55f, 0.85f, 0.45f, 0.12f}, p2{0.75f, 0.35f, 0.0f, 0.0f};
};
SnowLayer gS;
}

void snowSync(App& app) {
  const json* sj = app.doc.extra.is_object() && app.doc.extra.contains("snow") && app.doc.extra["snow"].is_object() ? &app.doc.extra["snow"] : nullptr;
  std::string key = sj ? sj->value("mask", std::string()) + "|" + sj->dump() : std::string();
  if (key == gS.key) return;
  gS.key = key; gS.on = false;
  if (gS.tex) { glDeleteTextures(1, &gS.tex); gS.tex = 0; }
  gS.px.clear();
  if (!sj) return;
  fs::path f = contentDir() / "templates" / sj->value("mask", std::string());
  int w = 0, h = 0, c = 0;
  stbi_uc* d = stbi_load(f.string().c_str(), &w, &h, &c, 4);
  if (!d) { logf("snow: mask %s not loaded", f.string().c_str()); return; }
  gS.px.assign(d, d + (size_t)w * h * 4); stbi_image_free(d);
  gS.w = w; gS.h = h;
  glCreateTextures(GL_TEXTURE_2D, 1, &gS.tex);
  glTextureStorage2D(gS.tex, 1, GL_RGBA8, w, h);
  glTextureSubImage2D(gS.tex, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, gS.px.data());
  glTextureParameteri(gS.tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(gS.tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameteri(gS.tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(gS.tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  auto v2 = [&](const char* k, vec2 dflt) { const json& a = sj->value(k, json::array()); return a.is_array() && a.size() >= 2 ? vec2(a[0].get<float>(), a[1].get<float>()) : dflt; };
  const json& b = (*sj)["box"];
  if (!b.is_array() || b.size() < 4) { logf("snow: no box"); return; }
  gS.box = vec4(b[0].get<float>(), b[1].get<float>(), 1.0f / std::max(1.0f, b[2].get<float>()), 1.0f / std::max(1.0f, b[3].get<float>()));
  vec2 sl = v2("slope", vec2(0.55f, 0.85f));
  gS.p = vec4(sl.x, sl.y, sj->value("threshold", 0.45f), sj->value("soft", 0.12f));
  gS.p2 = vec4(sj->value("roof", 0.75f), sj->value("noise", 0.35f), sj->value("roofSlope", 0.45f), sj->value("melt", 0.0f));
  gS.on = true;
  logf("snow: layer on (%dx%d mask, box %.0f x %.0f m)", w, h, b[2].get<float>(), b[3].get<float>());
}

bool snowLayerOn() { return gS.on && gS.tex; }   // W2: the map has lying snow (exposure under overcast)
void snowUniforms(GLuint p, bool thumb) {
  auto U = [&](const char* n) { return glGetUniformLocation(p, n); };
  int on = gS.on && gS.tex ? 1 : 0;
  glUniform1i(U("uSnowOn"), on);
  if (!on) return;
  glBindTextureUnit(12, gS.tex);   // unit 12: only the composite samples it (its own nrTex, rebound there); 20-31 are the texture library
  glUniform1i(U("uSnowMask"), 12);
  glUniform4fv(U("uSnowBox"), 1, glm::value_ptr(gS.box));
  glUniform4fv(U("uSnowP"), 1, glm::value_ptr(gS.p));
  glUniform4fv(U("uSnowP2"), 1, glm::value_ptr(gS.p2));
  (void)thumb;
}

bool snowOn() { return gS.on; }

vec4 snowMaskAt(float x, float z) {
  if (!gS.on || gS.px.empty()) return vec4(0);
  float u = (x - gS.box.x) * gS.box.z, v = (z - gS.box.y) * gS.box.w;
  if (u < 0 || v < 0 || u >= 1 || v >= 1) return vec4(0);
  int i = std::min(gS.w - 1, (int)(u * gS.w)), j = std::min(gS.h - 1, (int)(v * gS.h));
  const uint8_t* q = &gS.px[((size_t)j * gS.w + i) * 4];
  return vec4(q[0], q[1], q[2], q[3]) / 255.0f;
}

// footsteps (the sound session's events): the dominant snow state under the feet, or nullptr (the ground paint decides)
const char* snowStepAt(float x, float z) {
  if (!gS.on) return nullptr;
  vec4 m = snowMaskAt(x, z);
  float s = m.r - m.g - m.b - gS.p2.w;
  if (m.a > 0.45f) return "step_shallow";                                  // meltwater pockets
  if (m.b > 0.35f && m.r > 0.15f) return "step_slush";                      // SN2: the thaw state (melting snow)
  if (s > gS.p.z && m.g > 0.2f) return "step_snow";                        // SN2: the trodden state (packed footing)
  if (s > gS.p.z + 0.25f) return "step_snow_deep";
  if (s > gS.p.z) return "step_snow";
  return nullptr;
}

// SN2 (the user's snow sets; corpus weather 05 s3-s4): within 60 m of the eye the ground's snow is the three ambientCG
// sets instead of applySnow's flat white: packed (deposit, little traffic), trodden (the traffic mask: lanes, yards,
// door pads, ruts) and thaw (melt or thin cover: the creek's dirty band, drip lines, the edges giving way to mud). The
// two strongest states blend by height; the snow's edge is cut by the snow's and the substrate's heights (stones and
// ridges poke through first), with a dark wet halo just outside it. Albedo means are scaled to the corpus ranges
// (packed ~0.82, trodden ~0.72, thaw ~0.32, neutral); beyond 45-60 m it hands back to applySnow's flat snow (the same
// cover, no taps). Compiled into the terrain program after the texture GLSL (terrTexSlot, uSnowSlot, hbK).
const char* snowTexGlsl() {
  return R"GLSL(
uniform int uSnowTexOn;   // switch snowtex
int snowSlotOf(int s){ return s == 0 ? uSnowSlot.x : s == 1 ? uSnowSlot.y : uSnowSlot.z; }
vec3 snowAlbScale(int s){ return s == 0 ? vec3(1.498, 1.483, 1.488) : s == 1 ? vec3(1.12, 0.953, 0.836) : vec3(1.05); }
float snowRough(int s){ return s == 0 ? 0.58 : s == 1 ? 0.38 : 0.26; }
void snowTex(vec3 p, vec3 Ng, vec3 dpx, vec3 dpy, float dist, float hSub, inout vec3 alb, inout float rough, inout vec3 N){
  if (uSnowOn == 0 || uSnowTexOn == 0 || dist > 60.0 || gSnow <= 0.002 || uSnowSlot.x < 0 || uSnowSlot.y < 0 || uSnowSlot.z < 0) return;
  vec2 uv = (p.xz - uSnowBox.xy)*uSnowBox.zw;
  vec4 M = (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) ? vec4(0.7, 0.0, 0.0, 0.0) : texture(uSnowMask, uv);
  float wTh = clamp(max(smoothstep(0.15, 0.6, M.b), 1.0 - smoothstep(0.35, 0.85, gSnow)), 0.0, 1.0);   // melt, or thin cover
  float wTr = smoothstep(0.08, 0.35, M.g)*(1.0 - wTh);                                                   // traffic
  float wPk = max(0.0, 1.0 - wTr - wTh);
  vec3 w3 = vec3(wPk, wTr, wTh);
  int a = w3.x >= w3.y && w3.x >= w3.z ? 0 : w3.y >= w3.z ? 1 : 2;
  int b = a == 0 ? (w3.y >= w3.z ? 1 : 2) : a == 1 ? (w3.x >= w3.z ? 0 : 2) : (w3.x >= w3.y ? 0 : 1);
  float wa = w3[a], wb = w3[b];
  TerrT ta = terrTexSlot(snowSlotOf(a), p, Ng, dpx, dpy, dist);
  TerrT tb = ta; float k = 0.0;
  if (wb > 0.02) { tb = terrTexSlot(snowSlotOf(b), p, Ng, dpx, dpy, dist); k = hbK(wb/(wa + wb), ta.h, tb.h); }
  vec3 sAlb = mix(ta.alb*snowAlbScale(a), tb.alb*snowAlbScale(b), k);
  float sR = clamp(mix(ta.r*snowRough(a), tb.r*snowRough(b), k), 0.1, 1.0);
  float sH = mix(ta.h, tb.h, k);
  vec3 sN = normalize(Ng + mix(ta.dn, tb.dn, k));
  // the edge by height: the snow's own relief against the substrate's (stones and ridges through first), not a fade
  float c = clamp((gSnow - 0.5 + (sH - 0.5)*0.35 - (hSub - 0.5)*0.45)/0.12 + 0.5, 0.0, 1.0);
  float halo = (1.0 - c)*smoothstep(0.1, 0.45, gSnow);    // wet ground just outside the snow
  vec3 base = gSnowAlb0*(1.0 - 0.35*halo); float baseR = mix(gSnowR0, 0.35, halo);
  float f = 1.0 - smoothstep(45.0, 60.0, dist);           // hand back to applySnow's snow
  alb = mix(alb, mix(base, sAlb, c), f);
  rough = mix(rough, mix(baseR, sR, c), f);
  N = normalize(mix(N, normalize(mix(gSnowN0, sN, c)), f));
  gSnow = mix(gSnow, c, f);
}
)GLSL";
}
