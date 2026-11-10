// Lasso ground paint (docs/lasso.md#paint): data, painting, CPU queries, the GPU copy and the shader function.
#include "ground_paint.h"
#include "geom2d.h"
#include <cfloat>
#include <cstring>
#include <imgui.h>

// ================================================================ types
const std::vector<PaintType>& groundPaintTypes() {
  static std::vector<PaintType> T;
  if (!T.empty()) return T;
  // built-in defaults (content/ground_paint.json overrides names and gameplay values; the order is the shader's)
  T = {
    {"grass", "Grass (erase paint)", {0.20f, 0.26f, 0.09f}, 1.0f, true, -1, ""},
    {"marsh", "Marsh / shallow water", {0.10f, 0.12f, 0.08f}, 0.15f, false, 4, "step_water"},
    {"dirt", "Dirt", {0.26f, 0.20f, 0.13f}, 0.0f, false, -1, "step_dirt"},
    {"mud", "Mud", {0.14f, 0.11f, 0.08f}, 0.0f, false, 4, "step_mud"},
    {"gravel", "Gravel", {0.40f, 0.38f, 0.33f}, 0.0f, false, -1, "step_gravel"},
    {"sand", "Sand", {0.62f, 0.55f, 0.40f}, 0.0f, false, -1, "step_sand"},
    {"cobbles", "Cobbles", {0.36f, 0.35f, 0.32f}, 0.0f, false, -1, "step_stone"},
    {"rock", "Rock", {0.48f, 0.46f, 0.40f}, 0.0f, false, -1, "step_stone"},
    {"dry_grass", "Dry grass", {0.44f, 0.38f, 0.20f}, 0.55f, true, -1, ""},
    {"ploughed", "Ploughed farmland", {0.22f, 0.16f, 0.10f}, 0.0f, false, -1, "step_dirt"},
    {"leaf_litter", "Leaf litter", {0.34f, 0.19f, 0.07f}, 0.1f, true, -1, "step_leaves"},
    {"scorched", "Scorched earth", {0.07f, 0.06f, 0.055f}, 0.0f, false, -1, "step_dirt"},
    {"pebbles", "Pebbles", {0.50f, 0.47f, 0.41f}, 0.0f, false, -1, "step_gravel"},
  };
  try {
    fs::path p = contentDir() / "ground_paint.json";
    if (fs::exists(p)) {
      json j = json::parse(readTextFile(p));
      for (auto& o : j.value("types", json::array())) {
        std::string id = o.value("id", std::string());
        for (auto& t : T) if (t.id == id) {
          t.name = o.value("name", t.name);
          t.grass = o.value("grass", t.grass);
          t.natureOk = o.value("natureOk", t.natureOk);
          t.navArea = o.value("navArea", t.navArea);
          t.step = o.value("step", t.step);
          if (o.contains("swatch") && o["swatch"].is_array() && o["swatch"].size() == 3) t.swatch = jvec3(o["swatch"]);
        }
      }
    }
  } catch (std::exception& e) { logf("ground_paint.json: %s", e.what()); }
  return T;
}

// ================================================================ painting
namespace {
const MapDoc* gDoc = nullptr;          // the current map (App::doc never moves)
const std::vector<WaterBody>* gWater = nullptr;
uint64_t gSigCounter = 0x9e3779b97f4a7c15ull;

uint32_t hash2(int x, int z) { uint32_t h = (uint32_t)x * 0x8da6b343u ^ (uint32_t)z * 0xd8163841u; h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15; return h; }
float hash01(int x, int z) { return (hash2(x, z) & 0xffffff) / 16777216.0f; }
float vnoise2(float x, float z) {   // value noise for the paint edge (deterministic, CPU only)
  int ix = (int)std::floor(x), iz = (int)std::floor(z); float fx = x - ix, fz = z - iz;
  fx = fx * fx * (3 - 2 * fx); fz = fz * fz * (3 - 2 * fz);
  float a = hash01(ix, iz), b = hash01(ix + 1, iz), c = hash01(ix, iz + 1), d = hash01(ix + 1, iz + 1);
  return glm::mix(glm::mix(a, b, fx), glm::mix(c, d, fx), fz);
}
float sstep(float e0, float e1, float x) { float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f); return t * t * (3 - 2 * t); }
int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
const uint8_t* texelOf(const MapDoc& d, int i, int j) {
  int cx = floorDiv(i, kPaintChunk), cz = floorDiv(j, kPaintChunk);
  auto it = d.groundPaint.find(groundPaintKey(cx, cz));
  if (it == d.groundPaint.end()) return nullptr;
  int lx = i - cx * kPaintChunk, lz = j - cz * kPaintChunk;
  return &it->second[((size_t)lz * kPaintChunk + lx) * 4];
}
}   // namespace

int groundPaintRegion(App& app, const LassoRegion& r, int type, float strength, float feather) {
  if (r.empty() || type < 0 || type >= kPaintTypes) return 0;
  vec2 lo, hi; r.bounds(lo, hi);
  feather = std::max(0.0f, feather); strength = std::clamp(strength, 0.0f, 1.0f);
  float pad = feather * 0.5f + 1.0f;
  int i0 = (int)std::floor((lo.x - pad) / kPaintTexel), i1 = (int)std::ceil((hi.x + pad) / kPaintTexel);
  int j0 = (int)std::floor((lo.y - pad) / kPaintTexel), j1 = (int)std::ceil((hi.y + pad) / kPaintTexel);
  std::map<int64_t, std::vector<uint8_t>> next;   // the chunks this stroke touches, after the stroke
  int changed = 0;
  for (int j = j0; j <= j1; ++j)
    for (int i = i0; i <= i1; ++i) {
      vec2 c((i + 0.5f) * kPaintTexel, (j + 0.5f) * kPaintTexel);
      float sd = r.signedDist(c);
      if (sd < -pad) continue;
      // a wandering edge: the outline moves by up to a third of the feather (and 0.3 m on a hard edge)
      float jit = (vnoise2(c.x * 0.35f, c.y * 0.35f) - 0.5f) * (feather * 0.6f + 0.6f);
      float w = feather > 0.01f ? sstep(-feather * 0.5f, feather * 0.5f, sd + jit) : (sd + jit * 0.5f >= 0 ? 1.0f : 0.0f);
      w *= strength;
      if (w < 0.5f / 255.0f) continue;
      int cx = floorDiv(i, kPaintChunk), cz = floorDiv(j, kPaintChunk);
      int64_t key = groundPaintKey(cx, cz);
      auto nit = next.find(key);
      if (nit == next.end()) {
        auto dit = app.doc.groundPaint.find(key);
        nit = next.emplace(key, dit != app.doc.groundPaint.end() ? dit->second : std::vector<uint8_t>((size_t)kPaintChunk * kPaintChunk * 4, 0)).first;
      }
      uint8_t* px = &nit->second[((size_t)(j - cz * kPaintChunk) * kPaintChunk + (i - cx * kPaintChunk)) * 4];
      uint8_t old[4]; std::memcpy(old, px, 4);
      int tt = px[0], tw = px[1], ut = px[2], uw = px[3];
      auto q = [](float v) { return (int)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f); };
      if (type == 0) { tw = q(tw / 255.0f * (1 - w)); uw = q(uw / 255.0f * (1 - w)); }   // grass erases
      else if (tt == type || tw == 0) { float a = tt == type ? tw / 255.0f : 0.0f; tt = type; tw = std::max(tw == 0 ? 0 : tw, q(a + w * (1 - a))); if (ut == type) { ut = 0; uw = 0; } }
      else if (w > 0.996f) { tt = type; tw = 255; ut = 0; uw = 0; }
      else { ut = tt; uw = tw; tt = type; tw = q(w); }   // a new layer over the old one: the old shows through
      if (tw == 0) tt = 0;
      if (uw == 0) ut = 0;
      if (tw == 0 && uw != 0) { tt = ut; tw = uw; ut = 0; uw = 0; }
      px[0] = (uint8_t)tt; px[1] = (uint8_t)tw; px[2] = (uint8_t)ut; px[3] = (uint8_t)uw;
      if (std::memcmp(old, px, 4) != 0) ++changed;
    }
  if (!changed) return 0;
  app.pushUndo();
  for (auto& [k, v] : next) {
    bool any = false; for (size_t t = 0; t < v.size(); t += 4) if (v[t + 1] || v[t + 3]) { any = true; break; }
    if (any) app.doc.groundPaint[k] = std::move(v); else app.doc.groundPaint.erase(k);
  }
  gSigCounter = gSigCounter * 6364136223846793005ull + 1442695040888963407ull;
  app.doc.paintSig = gSigCounter | 1;
  app.dirty = true;
  return changed;
}

void groundPaintWeights(const MapDoc& d, float x, float z, float w[kPaintTypes]) {
  for (int k = 0; k < kPaintTypes; ++k) w[k] = 0;
  float qx = x / kPaintTexel - 0.5f, qz = z / kPaintTexel - 0.5f;
  int i = (int)std::floor(qx), j = (int)std::floor(qz); float fx = qx - i, fz = qz - j;
  float painted = 0;
  const float b[4] = {(1 - fx) * (1 - fz), fx * (1 - fz), (1 - fx) * fz, fx * fz};
  const int di[4] = {0, 1, 0, 1}, dj[4] = {0, 0, 1, 1};
  if (!d.groundPaint.empty())
    for (int c = 0; c < 4; ++c) {
      const uint8_t* t = texelOf(d, i + di[c], j + dj[c]);
      if (!t) continue;
      float wt = t[1] / 255.0f, wu = t[3] / 255.0f * (1 - wt);
      if (t[0] > 0 && t[0] < kPaintTypes) { w[t[0]] += b[c] * wt; painted += b[c] * wt; }
      if (t[2] > 0 && t[2] < kPaintTypes) { w[t[2]] += b[c] * wu; painted += b[c] * wu; }
    }
  w[0] = std::max(0.0f, 1.0f - painted);
}

float groundPaintAt(const App& app, float x, float z, int* type) {
  float w[kPaintTypes]; groundPaintWeights(app.doc, x, z, w);
  int best = 0; float bw = 0;
  for (int k = 1; k < kPaintTypes; ++k) if (w[k] > bw) { bw = w[k]; best = k; }
  if (type) *type = best;
  return bw;
}

float groundPaintGrass(float x, float z) {
  if (!gDoc || gDoc->groundPaint.empty()) return 1.0f;
  float w[kPaintTypes]; groundPaintWeights(*gDoc, x, z, w);
  const auto& T = groundPaintTypes();
  float g = w[0];
  for (int k = 1; k < kPaintTypes; ++k) g += w[k] * T[k].grass;
  return std::clamp(g, 0.0f, 1.0f);
}
float groundPaintWeight(float x, float z, int type) {
  if (!gDoc || gDoc->groundPaint.empty() || type < 0 || type >= kPaintTypes) return 0.0f;
  float w[kPaintTypes]; groundPaintWeights(*gDoc, x, z, w);
  return w[type];
}
float groundPaintTint(float x, float z, vec3& col) {   // GX3: painted swatch under a grass clump (weight 0 = unpainted)
  if (!gDoc || gDoc->groundPaint.empty()) return 0.0f;
  float w[kPaintTypes]; groundPaintWeights(*gDoc, x, z, w);
  const auto& T = groundPaintTypes(); vec3 c(0); float pw = 0;
  for (int k = 1; k < kPaintTypes; ++k) { c += w[k] * T[k].swatch; pw += w[k]; }
  if (pw > 0.001f) col = c / pw;
  return std::min(pw, 1.0f);
}
bool groundPaintBlocksNature(float x, float z) {
  if (!gDoc || gDoc->groundPaint.empty()) return false;
  float w[kPaintTypes]; groundPaintWeights(*gDoc, x, z, w);
  const auto& T = groundPaintTypes();
  float bad = 0; for (int k = 1; k < kPaintTypes; ++k) if (!T[k].natureOk) bad += w[k];
  return bad > 0.5f;
}
int groundPaintNavArea(float x, float z) {
  if (!gDoc || gDoc->groundPaint.empty()) return -1;
  float w[kPaintTypes]; groundPaintWeights(*gDoc, x, z, w);
  int best = 0; float bw = 0.5f;
  for (int k = 1; k < kPaintTypes; ++k) if (w[k] > bw) { bw = w[k]; best = k; }
  return best ? groundPaintTypes()[best].navArea : -1;
}
const std::string& groundPaintStep(float x, float z) {
  static const std::string none;
  if (!gDoc || gDoc->groundPaint.empty()) return none;
  float w[kPaintTypes]; groundPaintWeights(*gDoc, x, z, w);
  int best = 0; float bw = 0.5f;
  for (int k = 1; k < kPaintTypes; ++k) if (w[k] > bw) { bw = w[k]; best = k; }
  return best ? groundPaintTypes()[best].step : none;
}
float lassoWaterAtCurrent(float x, float z) {
  if (!gWater) return -1e9f;
  float best = -1e9f;
  for (auto& w : *gWater) {
    if (w.level <= best) continue;
    vec2 lo, hi; geom2d::bounds(w.outline, lo, hi);
    if (x < lo.x || z < lo.y || x > hi.x || z > hi.y) continue;
    if (geom2d::inside(w.outline, vec2(x, z))) best = w.level;
  }
  return best;
}
uint64_t lassoGroundRev() {
  uint64_t h = gDoc ? gDoc->paintSig : 0;
  if (gWater) for (auto& w : *gWater) { uint32_t b; std::memcpy(&b, &w.level, 4); h = (h ^ w.id ^ ((uint64_t)b << 20) ^ w.outline.size()) * 1099511628211ull; }
  return h;
}
uint64_t lassoGroundSig(vec2 lo, vec2 hi) {
  uint64_t h = 1469598103934665603ull;
  if (gDoc && !gDoc->groundPaint.empty()) {
    int cx0 = (int)std::floor(lo.x / (kPaintTexel * kPaintChunk)) - 1, cx1 = (int)std::floor(hi.x / (kPaintTexel * kPaintChunk)) + 1;
    int cz0 = (int)std::floor(lo.y / (kPaintTexel * kPaintChunk)) - 1, cz1 = (int)std::floor(hi.y / (kPaintTexel * kPaintChunk)) + 1;
    for (int cz = cz0; cz <= cz1; ++cz) for (int cx = cx0; cx <= cx1; ++cx) {
      auto it = gDoc->groundPaint.find(groundPaintKey(cx, cz));
      if (it == gDoc->groundPaint.end()) continue;
      h = (h ^ (uint64_t)it->first) * 1099511628211ull;
      const uint8_t* p = it->second.data();
      for (size_t k = 0; k < it->second.size(); k += 8) { uint64_t v; std::memcpy(&v, p + k, 8); h = (h ^ v) * 1099511628211ull; }
    }
  }
  if (gWater) for (auto& w : *gWater) {
    vec2 a, b; geom2d::bounds(w.outline, a, b);
    if (b.x < lo.x || b.y < lo.y || a.x > hi.x || a.y > hi.y) continue;
    uint32_t lv; std::memcpy(&lv, &w.level, 4);
    h = (h ^ w.id) * 1099511628211ull; h = (h ^ lv) * 1099511628211ull; h = (h ^ w.outline.size()) * 1099511628211ull;
  }
  return h;
}

// ================================================================ GPU copy
namespace {
struct Gpu {
  GLuint tex = 0; int w = 0, h = 0, step = 1; vec2 org{0}; uint64_t sig = 0;
  std::map<int64_t, std::vector<uint8_t>> uploaded;   // chunks as last uploaded
  vec2 tOrg{1e9f}; float tSize = 0, tSizeZ = 0;
  bool on = false;
} gG;

void uploadChunk(int64_t key, const std::vector<uint8_t>* data) {
  int cx = groundPaintKeyX(key), cz = groundPaintKeyZ(key);
  int ti0 = cx * kPaintChunk, tj0 = cz * kPaintChunk;                   // world texel range of the chunk
  int gi0 = (int)std::lround(gG.org.x / kPaintTexel), gj0 = (int)std::lround(gG.org.y / kPaintTexel);
  int s = gG.step;
  // destination texels (each covers s x s world texels: the chunk's samples at the destination texel centres)
  int x0 = std::max(0, floorDiv(ti0 - gi0, s)), x1 = std::min(gG.w - 1, floorDiv(ti0 + kPaintChunk - 1 - gi0, s));
  int y0 = std::max(0, floorDiv(tj0 - gj0, s)), y1 = std::min(gG.h - 1, floorDiv(tj0 + kPaintChunk - 1 - gj0, s));
  if (x0 > x1 || y0 > y1) return;
  int W = x1 - x0 + 1, H = y1 - y0 + 1;
  std::vector<uint8_t> buf((size_t)W * H * 4, 0);
  if (data)
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
      int wi = gi0 + (x0 + x) * s + s / 2 - ti0, wj = gj0 + (y0 + y) * s + s / 2 - tj0;
      wi = std::clamp(wi, 0, kPaintChunk - 1); wj = std::clamp(wj, 0, kPaintChunk - 1);
      std::memcpy(&buf[((size_t)y * W + x) * 4], &(*data)[((size_t)wj * kPaintChunk + wi) * 4], 4);
    }
  glTextureSubImage2D(gG.tex, 0, x0, y0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
}
}   // namespace

void groundPaintSync(App& app) {
  gDoc = &app.doc; gWater = &app.doc.waterBodies;
  const Terrain& T = app.terrain;
  const auto& P = app.doc.groundPaint;
  if (P.empty() && !gG.tex) { gG.on = false; return; }
  bool resize = !gG.tex || T.org != gG.tOrg || T.size != gG.tSize || T.sizeZ != gG.tSizeZ;
  if (resize) {
    if (gG.tex) glDeleteTextures(1, &gG.tex);
    int wt = (int)std::ceil(T.size / kPaintTexel) + 2, ht = (int)std::ceil(T.sizeZ / kPaintTexel) + 2;
    gG.step = std::max(1, (std::max(wt, ht) + 4095) / 4096);
    gG.w = (wt + gG.step - 1) / gG.step; gG.h = (ht + gG.step - 1) / gG.step;
    gG.org = vec2(std::floor(T.org.x / kPaintTexel) * kPaintTexel, std::floor(T.org.y / kPaintTexel) * kPaintTexel);
    glCreateTextures(GL_TEXTURE_2D, 1, &gG.tex);
    glTextureStorage2D(gG.tex, 1, GL_RGBA8, gG.w, gG.h);
    glTextureParameteri(gG.tex, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTextureParameteri(gG.tex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(gG.tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(gG.tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    std::vector<uint8_t> zero((size_t)gG.w * gG.h * 4, 0);
    glTextureSubImage2D(gG.tex, 0, 0, 0, gG.w, gG.h, GL_RGBA, GL_UNSIGNED_BYTE, zero.data());
    gG.uploaded.clear(); gG.sig = 0;
    gG.tOrg = T.org; gG.tSize = T.size; gG.tSizeZ = T.sizeZ;
  }
  if (gG.sig != app.doc.paintSig || resize) {
    for (auto& [k, v] : gG.uploaded) if (!P.count(k)) uploadChunk(k, nullptr);   // erased chunks
    for (auto& [k, v] : P) { auto it = gG.uploaded.find(k); if (it == gG.uploaded.end() || it->second != v) uploadChunk(k, &v); }
    gG.uploaded = P;
    gG.sig = app.doc.paintSig;
  }
  gG.on = !P.empty();
}

void groundPaintBind(GLuint prog) {
  GLint on = glGetUniformLocation(prog, "uPaintOn");
  if (on < 0) return;
  glUniform1i(on, gG.on && gG.tex ? 1 : 0);
  if (!gG.on || !gG.tex) return;
  glUniform1i(glGetUniformLocation(prog, "uPaint"), 23);
  glUniform2f(glGetUniformLocation(prog, "uPaintOrg"), gG.org.x, gG.org.y);
  glUniform1f(glGetUniformLocation(prog, "uPaintTexel"), kPaintTexel * gG.step);
  glUniform2i(glGetUniformLocation(prog, "uPaintSize"), gG.w, gG.h);
  glBindTextureUnit(23, gG.tex);
}

// ================================================================ shader (prepended to kTerrainFS; kCommon's noise)
const char* groundPaintGlsl() {
  return R"GLSL(
uniform sampler2D uPaint; uniform int uPaintOn; uniform vec2 uPaintOrg; uniform float uPaintTexel; uniform ivec2 uPaintSize;
float pHash(vec2 c) { return fract(sin(dot(c, vec2(127.1, 311.7)))*43758.5453); }
void paintAcc(inout float w[13], ivec2 c, float b) {
  if (c.x < 0 || c.y < 0 || c.x >= uPaintSize.x || c.y >= uPaintSize.y || b <= 0.0) return;
  vec4 t = texelFetch(uPaint, c, 0)*255.0;
  int tt = int(t.r + 0.5), ut = int(t.b + 0.5);
  float wt = t.g/255.0, wu = t.a/255.0*(1.0 - wt);
  if (tt > 0 && tt < 13) w[tt] += b*wt;
  if (ut > 0 && ut < 13) w[ut] += b*wu;
}
// one ground type: albedo, roughness, normal offset (xz), ambient occlusion
void paintMat(int t, vec3 p, out vec3 a, out float r, out vec3 bm, out float ao) {
  float n1 = vnoise(vec3(p.xz*0.9, 71.0)), n2 = vnoise(vec3(p.xz*4.3, 72.0)), nb = fbm(vec3(p.xz*0.07, 73.0));
  bm = vec3(n2 - 0.5, 0.0, vnoise(vec3(p.xz*4.3, 74.0)) - 0.5)*0.3; ao = 1.0; r = 0.93;
  vec3 soil = mix(vec3(0.20,0.155,0.10), vec3(0.28,0.22,0.145), n1)*(0.85 + 0.3*n2)*(0.9 + 0.2*nb);
  if (t == 1) {          // marsh: wet mud, standing water in the lows, sedge tufts
    float pool = smoothstep(0.47, 0.6, fbm(vec3(p.xz*0.22, 75.0)) + (n1 - 0.5)*0.12);
    vec3 m = mix(vec3(0.09,0.075,0.055), vec3(0.14,0.12,0.075), n1);
    vec3 sedge = mix(vec3(0.15,0.19,0.07), vec3(0.27,0.29,0.13), n2);
    a = mix(m, sedge, smoothstep(0.55, 0.75, vnoise(vec3(p.xz*1.7, 76.0)))*(1.0 - pool));
    a = mix(a, vec3(0.03,0.04,0.036), pool);
    r = mix(0.5, 0.05, pool); bm *= 1.0 - pool; ao = mix(0.9, 1.0, pool);
  } else if (t == 2) {   // dirt
    a = soil; r = 0.95;
  } else if (t == 3) {   // mud: darker, glossy where it is wettest, churned
    float gl = smoothstep(0.6, 0.8, n1);
    a = mix(vec3(0.10,0.078,0.056), vec3(0.155,0.12,0.085), n2)*(0.92 + 0.16*nb)*mix(1.0, 0.8, gl);
    r = mix(0.55, 0.22, gl); bm *= 1.5; ao = 0.92;
  } else if (t == 4) {   // gravel
    float s = step(0.55, vnoise(vec3(p.xz*9.0, 77.0)));
    a = mix(vec3(0.33,0.29,0.23), vec3(0.48,0.43,0.35), s)*(0.8 + 0.3*n2); r = 0.9; bm *= 1.7; ao = 0.88 + 0.12*s;
  } else if (t == 5) {   // sand: pale, fine ripples
    float rip = sin(p.x*5.0 + p.z*2.2 + n1*4.0)*0.5 + 0.5;
    a = mix(vec3(0.55,0.48,0.34), vec3(0.67,0.60,0.44), n1)*(0.94 + 0.08*rip + 0.06*n2); r = 0.97; bm = vec3(cos(p.x*5.0 + p.z*2.2 + n1*4.0)*0.12, 0.0, 0.05);
  } else if (t == 6) {   // cobbles: running-bond setts with rounded tops and dark joints
    vec2 g = p.xz*vec2(3.6, 4.6); float row = floor(g.y); g.x += mod(row, 2.0)*0.5;
    vec2 id = floor(g), f = fract(g) - 0.5, q = abs(f);
    float edge = 0.5 - max(q.x, q.y);
    float joint = 1.0 - smoothstep(0.035, 0.1, edge + (n2 - 0.5)*0.04);
    float v = pHash(id);
    a = mix(vec3(0.21,0.20,0.19), vec3(0.37,0.355,0.33), v)*(0.85 + 0.25*n2);
    a = mix(a, vec3(0.30,0.24,0.18), 0.25*step(0.8, pHash(id + 7.0)));
    a = mix(a, vec3(0.10,0.09,0.075), joint);
    bm = vec3(f.x, 0.0, f.y)*0.9*(1.0 - joint); r = mix(0.72, 0.95, joint); ao = 1.0 - 0.45*joint;
  } else if (t == 7) {   // rock: a flat limestone pavement with joints and lichen
    float jx = abs(fract(p.x*0.35 + n1*0.6) - 0.5), jz = abs(fract(p.z*0.28 + nb*1.3) - 0.5);
    float crack = 1.0 - smoothstep(0.0, 0.025, min(jx, jz));
    a = mix(vec3(0.44,0.44,0.41), vec3(0.58,0.57,0.53), nb)*(0.85 + 0.25*n2);
    a = mix(a, mix(vec3(0.17,0.20,0.10), vec3(0.42,0.41,0.30), n2), 0.4*smoothstep(0.65, 0.85, vnoise(vec3(p.xz*0.5, 81.0))));
    a *= 1.0 - 0.45*crack; r = 0.86; bm *= 1.2; ao = 1.0 - 0.4*crack;
  } else if (t == 8) {   // dry grass: straw sward
    float st = vnoise(vec3(p.xz*vec2(13.0, 38.0) + vec2(n1*6.0, 0.0), 82.0));
    a = mix(vec3(0.34,0.29,0.15), vec3(0.50,0.44,0.25), n1)*(0.82 + 0.3*st);
    a = mix(a, vec3(0.22,0.24,0.10), 0.2*smoothstep(0.6, 0.8, nb)); r = 0.95;
  } else if (t == 9) {   // ploughed farmland: furrows ~0.8 m apart, clods on the ridges
    float u = p.z*1.25 + (vnoise(vec3(p.xz*0.04, 78.0)) - 0.5)*3.0;
    float rr = sin(u*6.2832);
    a = soil*mix(0.68, 1.12, rr*0.5 + 0.5)*(0.9 + 0.2*step(0.6, vnoise(vec3(p.xz*6.0, 83.0))));
    bm = vec3(0.0, 0.0, cos(u*6.2832)*0.65) + bm*0.6; ao = mix(0.72, 1.0, rr*0.5 + 0.5); r = 0.96;
  } else if (t == 10) {  // leaf litter: overlapping brown / orange / yellow leaves over dark soil
    float lf = vnoise(vec3(mat2(0.8, -0.6, 0.6, 0.8)*p.xz*5.5, 79.0)), c1 = vnoise(vec3(p.xz*2.1, 84.0)), c2 = vnoise(vec3(p.xz*3.3, 85.0));
    a = mix(vec3(0.20,0.11,0.045), vec3(0.42,0.24,0.08), c1);
    a = mix(a, vec3(0.46,0.36,0.12), smoothstep(0.7, 0.9, c2));
    a = mix(vec3(0.08,0.06,0.045), a, smoothstep(0.3, 0.45, lf)); r = 0.9; bm *= 1.3; ao = mix(0.8, 1.0, smoothstep(0.3, 0.45, lf));
  } else if (t == 11) {  // scorched earth: char, grey ash, burnt brown fringes
    a = mix(vec3(0.03,0.027,0.025), vec3(0.085,0.075,0.065), n1);
    a = mix(a, vec3(0.30,0.29,0.27), 0.7*smoothstep(0.62, 0.8, fbm(vec3(p.xz*0.6, 80.0))));
    a = mix(a, vec3(0.16,0.10,0.06), 0.3*smoothstep(0.6, 0.85, nb)); r = 0.97; ao = 0.95;
  } else {               // pebbles: rounded stones packed on coarse sand
    vec2 g = p.xz*6.5, id = floor(g), f = fract(g) - 0.5;
    vec2 o = (vec2(pHash(id), pHash(id + 3.1)) - 0.5)*0.35;
    float rad = 0.28 + 0.15*pHash(id + 5.3), d = length(f - o)/rad;
    float st = 1.0 - smoothstep(0.85, 1.0, d);
    vec3 sc = mix(vec3(0.38,0.36,0.32), vec3(0.62,0.58,0.52), pHash(id + 9.7));
    a = mix(vec3(0.42,0.37,0.28)*(0.85 + 0.2*n2), sc, st);
    bm = vec3(f.x - o.x, 0.0, f.y - o.y)/rad*0.7*st; r = mix(0.95, 0.7, st); ao = mix(0.75, 1.0, st);
  }
}
// Two calls in kTerrainFS (docs/lasso.md#shading):
//   PaintOut po = paintApply(p, nGeo, alb, dirt, dpx, dpy, dist);   after the crater mix, before the wet / puddle lines
//   paintFinish(po, rough, N, ao);                                   just before shade(), after every other layer
// With L4's textures in the build (tex::glslCommon() prepended first: NMK_TEX_PAINT, uPaintSlot[12], terrTexSlot)
// each painted type is modulated by its ambientCG set; without them the materials are the procedural ones above.
struct PaintOut { float w; float rough, ao; vec3 dn, ng; };
PaintOut paintApply(vec3 p, vec3 nGeo, inout vec3 alb, float dirt, vec3 dpx, vec3 dpy, float dist) {
  PaintOut o; o.w = 0.0; o.rough = 1.0; o.ao = 1.0; o.dn = vec3(0.0); o.ng = nGeo;
  if (uPaintOn == 0) return o;
  float w[13]; for (int i = 0; i < 13; ++i) w[i] = 0.0;
  vec2 j = (vec2(vnoise(vec3(p.xz*0.8, 61.0)), vnoise(vec3(p.xz*0.8, 62.0))) - 0.5)*0.8;   // no texel grid at the edges
  vec2 q = (p.xz - uPaintOrg)/uPaintTexel - 0.5 + j;
  ivec2 c = ivec2(floor(q)); vec2 f = q - vec2(c);
  paintAcc(w, c, (1.0 - f.x)*(1.0 - f.y)); paintAcc(w, c + ivec2(1, 0), f.x*(1.0 - f.y));
  paintAcc(w, c + ivec2(0, 1), (1.0 - f.x)*f.y); paintAcc(w, c + ivec2(1, 1), f.x*f.y);
  float W = 0.0; for (int i = 1; i < 13; ++i) W += w[i];
  W = clamp(W, 0.0, 1.0)*(1.0 - smoothstep(0.15, 0.75, dirt));   // craters show through the paint
  if (W < 0.004) return o;
  vec3 a = vec3(0.0), bm = vec3(0.0); float r = 0.0, ao = 0.0, tot = 0.0;
  for (int i = 1; i < 13; ++i) {
    if (w[i] < 0.01) continue;
    vec3 ai, bi; float ri, oi;
    paintMat(i, p, ai, ri, bi, oi);
#ifdef NMK_TEX_PAINT
    TerrT t = terrTexSlot(uPaintSlot[i - 1], p, nGeo, dpx, dpy, dist);   // identity when texturing is off / the set is missing
    ai *= t.alb; ri *= t.r; oi *= t.ao; bi += t.dn;
#endif
    a += ai*w[i]; r += ri*w[i]; ao += oi*w[i]; bm += bi*w[i]; tot += w[i];
  }
  if (tot <= 0.0) return o;
  alb = mix(alb, a/tot, W);
  o.w = W; o.rough = r/tot; o.ao = ao/tot; o.dn = bm/tot;
  return o;
}
void paintFinish(PaintOut o, inout float rough, inout vec3 N, inout float ao) {
  if (o.w <= 0.0) return;
  rough = mix(rough, o.rough, o.w); ao = mix(ao, o.ao, o.w);
  N = normalize(mix(N, normalize(o.ng + o.dn), o.w));   // the painted ground's own relief replaces the sward's
}
)GLSL";
}

// ================================================================ panel
void groundPaintPicker(int& type) {
  const auto& T = groundPaintTypes();
  for (int k = 0; k < (int)T.size(); ++k) {
    ImGui::PushID(k);
    ImGui::ColorButton("##sw", ImVec4(T[k].swatch.r, T[k].swatch.g, T[k].swatch.b, 1), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(14, 14));
    ImGui::SameLine();
    if (ImGui::Selectable(T[k].name.c_str(), type == k, 0, ImVec2(130, 0))) type = k;
    ImGui::PopID();
    if (k % 2 == 0 && k + 1 < (int)T.size()) ImGui::SameLine(160);
  }
}
