// L4 texture layer (docs/textures.md): ambientCG CC0 sets, triplanar sampling hooks for kMainFS / kTerrainFS,
// the --pack-texture packer used by tools/fetch_ambientcg.py and placeholder sets for bring-up / tests.
#include "textures.h"
#include <stb_image.h>
#include <stb_image_write.h>
#define STB_DXT_IMPLEMENTATION
#include <stb_dxt.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <thread>

namespace tex {
void invalidateKey();   // stateKey() cache (bottom of the file)
namespace {

constexpr int kSize = 1024;            // every layer is resampled to 1024^2 (1K everywhere)
constexpr int kLevels = 9;             // 1024 .. 4: the smallest level is one BC block
#ifndef GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT 0x8C4F
#endif

struct Slot {
  std::string name, set;
  int mode = 0, space = 0, rot = 0;   // mode 0 detail, 1 replace; space 0 asset, 1 part, 2 world, 3 ground (planar xz)
  float scale = 1, kAlb = 0.8f, kN = 0.8f, kR = 0.5f, kAO = 0.5f, kH = 0, fadeA = 0, fadeB = 0;
  int layer = -1;
};
struct SetInfo { std::string name; vec2 size{2, 2}; int layer = -1; vec3 meanLin{0.5f}; float meanAO = 1, meanR = 0.7f, meanH = 0.5f; };

std::vector<Slot> slots;
std::map<std::string, int> slotByName;
std::map<std::string, int> matSlot;    // material name -> slot, -2 = none
int patSlot[32];
int terrSlot[8];
int paintSlot[kPaintTypes];
std::vector<SetInfo> sets;             // loaded layers
std::vector<GLuint> progs;
GLuint arr[3] = {0, 0, 0};
bool gpuOn = false, wantOn = true;
Info gInfo;
fs::path root;

int findSet(const std::string& n) { for (auto& s : sets) if (s.name == n) return s.layer; return -1; }

int slotFromJson(const std::string& name, const json& j) {
  Slot s; s.name = name;
  s.set = j.value("set", "");
  std::string m = j.value("mode", "detail"); s.mode = m == "replace" ? 1 : 0;
  std::string sp = j.value("space", "asset"); s.space = sp == "part" ? 1 : sp == "world" ? 2 : sp == "ground" ? 3 : 0;
  s.scale = j.value("scale", 1.0f); s.rot = j.value("rot", 0) & 3;
  s.kAlb = j.value("albedo", s.kAlb); s.kN = j.value("normal", s.kN); s.kR = j.value("rough", s.kR);
  s.kAO = j.value("ao", s.kAO); s.kH = j.value("height", s.kH);
  if (j.contains("fade") && j["fade"].is_array() && j["fade"].size() == 2) { s.fadeA = j["fade"][0]; s.fadeB = j["fade"][1]; }
  if ((int)slots.size() >= kMaxSlots) { logf("textures: more than %d slots, '%s' ignored", kMaxSlots, name.c_str()); return -1; }
  slots.push_back(s); slotByName[name] = (int)slots.size() - 1;
  return (int)slots.size() - 1;
}

void loadMapping(const fs::path& file) {
  slots.clear(); slotByName.clear(); matSlot.clear();
  std::fill(std::begin(patSlot), std::end(patSlot), -1); std::fill(std::begin(terrSlot), std::end(terrSlot), -1); std::fill(std::begin(paintSlot), std::end(paintSlot), -1);
  if (!fs::exists(file)) { logf("textures: no %s (procedural patterns only)", file.string().c_str()); return; }
  json j;
  try { j = json::parse(readTextFile(file)); } catch (std::exception& e) { logf("textures: %s: %s", file.string().c_str(), e.what()); return; }
  json js = j.value("slots", json::object()), jp = j.value("patterns", json::object()), jm = j.value("materials", json::object());   // named: items() of a temporary dangles
  for (auto& [k, v] : js.items()) slotFromJson(k, v);
  auto ref = [&](const std::string& n) -> int { if (n == "none" || n.empty()) return -2; auto it = slotByName.find(n); if (it == slotByName.end()) { logf("textures: unknown slot '%s'", n.c_str()); return -2; } return it->second; };
  for (auto& [k, v] : jp.items()) { int p = std::atoi(k.c_str()); if (p >= 0 && p < 32) patSlot[p] = std::max(-1, ref(v.get<std::string>())); }
  for (auto& [k, v] : jm.items()) matSlot[k] = ref(v.get<std::string>());
  static const char* tl[8] = {"grass", "soil", "mud", "gravel", "sand", "rock", "scree", "crater"};
  auto tj = j.value("terrain", json::object());
  for (int k = 0; k < 8; ++k) if (tj.contains(tl[k])) terrSlot[k] = std::max(-1, ref(tj[tl[k]].get<std::string>()));
  json pj = j.value("paint", json::object());
  for (int k = 0; k < kPaintTypes; ++k) if (pj.contains(kPaintTypeNames[k])) paintSlot[k] = std::max(-1, ref(pj[kPaintTypeNames[k]].get<std::string>()));
}

// ------------------------------------------------------------------ image helpers
struct Img { int w = 0, h = 0, c = 0; std::vector<uint8_t> px; };
bool loadImg(const fs::path& p, int comp, Img& im) {
  int w, h, c;
  stbi_set_flip_vertically_on_load_thread(1);   // row 0 = bottom: texture v up = image up
  stbi_uc* d = stbi_load(p.string().c_str(), &w, &h, &c, comp);
  stbi_set_flip_vertically_on_load_thread(0);
  if (!d) return false;
  im.w = w; im.h = h; im.c = comp; im.px.assign(d, d + (size_t)w * h * comp);
  stbi_image_free(d);
  return true;
}
// bilinear resample to n x n with area pre-filtering on downscale (repeat wrap: the sets tile)
Img resample(const Img& s, int n) {
  if (s.w == n && s.h == n) return s;
  Img src = s;
  while (src.w >= 2 * n || src.h >= 2 * n) {   // 2x box steps first
    int w2 = std::max(1, src.w / (src.w >= 2 * n ? 2 : 1)), h2 = std::max(1, src.h / (src.h >= 2 * n ? 2 : 1));
    int fx = src.w / w2, fy = src.h / h2;
    Img d; d.w = w2; d.h = h2; d.c = src.c; d.px.resize((size_t)w2 * h2 * src.c);
    for (int y = 0; y < h2; ++y) for (int x = 0; x < w2; ++x) for (int k = 0; k < src.c; ++k) {
      int sum = 0; for (int j = 0; j < fy; ++j) for (int i = 0; i < fx; ++i) sum += src.px[((size_t)(y * fy + j) * src.w + x * fx + i) * src.c + k];
      d.px[((size_t)y * w2 + x) * src.c + k] = (uint8_t)((sum + fx * fy / 2) / (fx * fy));
    }
    src = std::move(d);
  }
  Img d; d.w = n; d.h = n; d.c = src.c; d.px.resize((size_t)n * n * src.c);
  for (int y = 0; y < n; ++y) for (int x = 0; x < n; ++x) {
    float u = (x + 0.5f) * src.w / n - 0.5f, v = (y + 0.5f) * src.h / n - 0.5f;
    int x0 = (int)std::floor(u), y0 = (int)std::floor(v); float fu = u - x0, fv = v - y0;
    auto at = [&](int xx, int yy, int k) { xx = (xx % src.w + src.w) % src.w; yy = (yy % src.h + src.h) % src.h; return (float)src.px[((size_t)yy * src.w + xx) * src.c + k]; };
    for (int k = 0; k < src.c; ++k) {
      float a = at(x0, y0, k) * (1 - fu) + at(x0 + 1, y0, k) * fu, b = at(x0, y0 + 1, k) * (1 - fu) + at(x0 + 1, y0 + 1, k) * fu;
      d.px[((size_t)y * n + x) * src.c + k] = (uint8_t)std::clamp(a * (1 - fv) + b * fv + 0.5f, 0.0f, 255.0f);
    }
  }
  return d;
}

float s2l[256];
uint8_t l2s(float l) { l = std::clamp(l, 0.0f, 1.0f); float s = l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1 / 2.4f) - 0.055f; return (uint8_t)std::clamp(s * 255.0f + 0.5f, 0.0f, 255.0f); }
void initLut() { static bool done = false; if (done) return; done = true; for (int i = 0; i < 256; ++i) { float s = i / 255.0f; s2l[i] = s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f); } }

// one set on the CPU: compressed (or raw RGBA8 / RG8) mip chains of the three layers, and its means
struct Cpu { bool ok = false; std::vector<uint8_t> lv[3][kLevels]; SetInfo info; std::string err; };

void encodeLevel(const std::vector<uint8_t>& src, int n, int comp, bool bc, int kind, std::vector<uint8_t>& out) {
  // kind 0: RGBA -> BC3; 1, 2: RG -> BC5
  if (!bc) { out = src; return; }
  int bw = std::max(1, n / 4);
  size_t bsz = 16;
  out.assign((size_t)bw * bw * bsz, 0);
  uint8_t blk[64];
  for (int by = 0; by < bw; ++by) for (int bx = 0; bx < bw; ++bx) {
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
      int sx = std::min(bx * 4 + x, n - 1), sy = std::min(by * 4 + y, n - 1);
      std::memcpy(blk + (y * 4 + x) * comp, &src[((size_t)sy * n + sx) * comp], comp);
    }
    uint8_t* dst = &out[((size_t)by * bw + bx) * bsz];
    if (kind == 0) stb_compress_dxt_block(dst, blk, 1, STB_DXT_HIGHQUAL); else stb_compress_bc5_block(dst, blk);
  }
}

void buildSet(const fs::path& dir, bool bc, Cpu& o) {
  initLut();
  Img col, nrm, rma;
  if (!loadImg(dir / "color.jpg", 3, col) && !loadImg(dir / "color.png", 3, col)) { o.err = "no color"; return; }
  if (!loadImg(dir / "normal.jpg", 3, nrm) && !loadImg(dir / "normal.png", 3, nrm)) { nrm.w = nrm.h = 1; nrm.c = 3; nrm.px = {128, 128, 255}; }
  if (!loadImg(dir / "rma.jpg", 3, rma) && !loadImg(dir / "rma.png", 3, rma)) { rma.w = rma.h = 1; rma.c = 3; rma.px = {200, 255, 128}; }
  col = resample(col, kSize); nrm = resample(nrm, kSize); rma = resample(rma, kSize);
  const size_t N = (size_t)kSize * kSize;
  // level 0
  std::vector<uint8_t> a(N * 4), b(N * 2), c(N * 2);
  double mr = 0, mg = 0, mb = 0, mao = 0, mR = 0, mH = 0;
  for (size_t i = 0; i < N; ++i) {
    a[i * 4 + 0] = col.px[i * 3]; a[i * 4 + 1] = col.px[i * 3 + 1]; a[i * 4 + 2] = col.px[i * 3 + 2]; a[i * 4 + 3] = rma.px[i * 3 + 1];
    b[i * 2] = nrm.px[i * 3]; b[i * 2 + 1] = nrm.px[i * 3 + 1];
    c[i * 2] = rma.px[i * 3]; c[i * 2 + 1] = rma.px[i * 3 + 2];
    mr += s2l[a[i * 4]]; mg += s2l[a[i * 4 + 1]]; mb += s2l[a[i * 4 + 2]]; mao += rma.px[i * 3 + 1];
    mR += rma.px[i * 3]; mH += rma.px[i * 3 + 2];
  }
  o.info.meanLin = vec3((float)(mr / N), (float)(mg / N), (float)(mb / N));
  o.info.meanAO = (float)(mao / N / 255.0); o.info.meanR = (float)(mR / N / 255.0); o.info.meanH = (float)(mH / N / 255.0);
  int n = kSize;
  for (int L = 0; L < kLevels; ++L) {
    encodeLevel(a, n, 4, bc, 0, o.lv[0][L]); encodeLevel(b, n, 2, bc, 1, o.lv[1][L]); encodeLevel(c, n, 2, bc, 2, o.lv[2][L]);
    if (L + 1 == kLevels) break;
    int m = n / 2;
    std::vector<uint8_t> a2((size_t)m * m * 4), b2((size_t)m * m * 2), c2((size_t)m * m * 2);
    for (int y = 0; y < m; ++y) for (int x = 0; x < m; ++x) {
      size_t i00 = (size_t)(2 * y) * n + 2 * x, i10 = i00 + 1, i01 = i00 + n, i11 = i01 + 1, d = (size_t)y * m + x;
      for (int k = 0; k < 3; ++k) a2[d * 4 + k] = l2s(0.25f * (s2l[a[i00 * 4 + k]] + s2l[a[i10 * 4 + k]] + s2l[a[i01 * 4 + k]] + s2l[a[i11 * 4 + k]]));   // colour averaged in linear light
      a2[d * 4 + 3] = (uint8_t)((a[i00 * 4 + 3] + a[i10 * 4 + 3] + a[i01 * 4 + 3] + a[i11 * 4 + 3] + 2) / 4);
      // normals: average the unit vectors and keep the xy of the (shorter) mean: distant normals flatten out
      float nx = 0, ny = 0;
      for (size_t i : {i00, i10, i01, i11}) { nx += b[i * 2] / 127.5f - 1; ny += b[i * 2 + 1] / 127.5f - 1; }
      b2[d * 2] = (uint8_t)std::clamp((nx / 4 + 1) * 127.5f + 0.5f, 0.0f, 255.0f); b2[d * 2 + 1] = (uint8_t)std::clamp((ny / 4 + 1) * 127.5f + 0.5f, 0.0f, 255.0f);
      for (int k = 0; k < 2; ++k) c2[d * 2 + k] = (uint8_t)((c[i00 * 2 + k] + c[i10 * 2 + k] + c[i01 * 2 + k] + c[i11 * 2 + k] + 2) / 4);
    }
    a.swap(a2); b.swap(b2); c.swap(c2); n = m;
  }
  vec2 sz(2, 2);
  try {
    if (fs::exists(dir / "meta.json")) {
      json m = json::parse(readTextFile(dir / "meta.json"));
      if (m.contains("size") && m["size"].is_array() && m["size"].size() == 2) sz = vec2(m["size"][0].get<float>(), m["size"][1].get<float>());
    }
  } catch (...) {}
  o.info.size = glm::max(sz, vec2(0.05f));
  o.ok = true;
}

bool hasExt(const char* e) {
  GLint n = 0; glGetIntegerv(GL_NUM_EXTENSIONS, &n);
  for (GLint i = 0; i < n; ++i) if (std::strcmp((const char*)glGetStringi(GL_EXTENSIONS, i), e) == 0) return true;
  return false;
}

void upload(GLuint p) {
  auto U = [&](const char* n) { return glGetUniformLocation(p, n); };
  glProgramUniform1i(p, U("uTexCol"), kUnitBase + 0); glProgramUniform1i(p, U("uTexNrm"), kUnitBase + 1); glProgramUniform1i(p, U("uTexRH"), kUnitBase + 2);
  glProgramUniform1i(p, U("uTexOn"), gpuOn && wantOn ? 1 : 0);
  std::vector<vec4> A(kMaxSlots, vec4(-1, 0, 0, 1)), B(kMaxSlots, vec4(0)), C(kMaxSlots, vec4(0)), Ly(kMaxLayers, vec4(0.5f, 0.5f, 0.7f, 0.5f)), M(kMaxLayers, vec4(0.5f));
  for (size_t i = 0; i < slots.size(); ++i) {
    const Slot& s = slots[i];
    A[i] = vec4((float)s.layer, (float)s.mode, (float)s.space, s.scale);
    B[i] = vec4(s.kAlb, s.kN, s.kR, s.kAO); C[i] = vec4(s.kH, (float)s.rot, s.fadeA, s.fadeB);
  }
  for (auto& s : sets) { Ly[s.layer] = vec4(1.0f / s.size.x, 1.0f / s.size.y, s.meanR, s.meanH); M[s.layer] = vec4(s.meanLin, s.meanAO); }
  glProgramUniform4fv(p, U("uTexA"), kMaxSlots, &A[0].x); glProgramUniform4fv(p, U("uTexB"), kMaxSlots, &B[0].x); glProgramUniform4fv(p, U("uTexC"), kMaxSlots, &C[0].x);
  glProgramUniform4fv(p, U("uTexL"), kMaxLayers, &Ly[0].x); glProgramUniform4fv(p, U("uTexM"), kMaxLayers, &M[0].x);
  vec4 lod(3.0f, 2.0f, 16.0f, 0.95f), lod2(8.0f, 48.0f, 0.7f, 0.0f);   // tuned on --selftest-render (docs/textures.md#shimmer); NMK_TEX_LOD="a,b,c,d" overrides (A/B)
  if (const char* e = std::getenv("NMK_TEX_LOD")) std::sscanf(e, "%f,%f,%f,%f", &lod.x, &lod.y, &lod.z, &lod.w);
  if (const char* e = std::getenv("NMK_TEX_LOD2")) std::sscanf(e, "%f,%f,%f", &lod2.x, &lod2.y, &lod2.z);
  glProgramUniform4fv(p, U("uTexLod"), 1, &lod.x); glProgramUniform4fv(p, U("uTexLod2"), 1, &lod2.x);
  glProgramUniform1iv(p, U("uPatSlot"), 32, patSlot); glProgramUniform1iv(p, U("uTerrSlot"), 8, terrSlot); glProgramUniform1iv(p, U("uPaintSlot"), kPaintTypes, paintSlot);
}
void uploadAll() { for (GLuint p : progs) upload(p); }

void resolveLayers() {
  gInfo.slotsLive = 0; gInfo.missing.clear();
  for (auto& s : slots) {
    s.layer = findSet(s.set);
    if (s.layer >= 0) ++gInfo.slotsLive;
    else if (std::find(gInfo.missing.begin(), gInfo.missing.end(), s.set) == gInfo.missing.end()) gInfo.missing.push_back(s.set);
  }
  gInfo.slots = (int)slots.size();
}

const char* kCommonGLSL = R"GLSL(
// ---- L4 texture layer (src/textures.cpp, docs/textures.md): ambientCG sets in three 2D arrays (units 20-22)
uniform sampler2DArray uTexCol, uTexNrm, uTexRH;   // colour (sRGB) + ao, normal xy, roughness + height
uniform int uTexOn;
uniform vec4 uTexA[80];   // per slot: layer (-1 none), mode (0 detail, 1 replace), space (0 asset, 1 part, 2 world, 3 ground), scale
uniform vec4 uTexB[80];   // per slot: albedo, normal, roughness, ao strength
uniform vec4 uTexC[80];   // per slot: height (m), quarter turns, distance fade start, end (m; end 0 = no fade)
uniform vec4 uTexL[64];   // per layer: 1/size u, 1/size v (1/m), mean roughness, mean height
uniform vec4 uTexM[64];   // per layer: mean linear colour, mean ao
uniform int uTerrSlot[8]; // terrain layers: grass, soil, mud, gravel, sand, rock, scree, crater
#define NMK_TEX_PAINT 1   // painted ground: uPaintSlot[k] for marsh, dirt, mud, gravel, sand, cobbles, rock, dry_grass, ploughed, leaf_litter, scorched, pebbles
uniform int uPaintSlot[12];
uniform vec4 uTexLod;     // gradient scale (LOD bias against TAA-jitter shimmer), normal fade start / end (texels per pixel), fade depth
uniform vec4 uTexLod2;    // albedo / roughness detail fade start / end (texels per pixel), fade depth
struct TexS { vec3 col; float ao, r, h; vec3 n; };
bool texOn(){ return uTexOn == 1 && uLook == 1; }
void texFrame(int q, inout vec3 T, inout vec3 B){ vec3 t = T; if (q == 1) { T = B; B = -t; } else if (q == 2) { T = -T; B = -B; } else if (q == 3) { T = -B; B = t; } }
// one planar tap: frame T (u), B (v), Nf (out of the plane); explicit gradients (taps sit in divergent branches);
// the tangent-space normal is whiteout-blended with the surface normal ns
void texTap(float L, vec3 p, vec3 dpx, vec3 dpy, vec2 inv, vec3 T, vec3 B, vec3 Nf, vec3 ns, float w, inout TexS s){
  vec3 q = vec3(dot(p, T)*inv.x, dot(p, B)*inv.y, L);
  vec2 gx = vec2(dot(dpx, T), dot(dpx, B))*inv*uTexLod.x, gy = vec2(dot(dpy, T), dot(dpy, B))*inv*uTexLod.x;
  vec4 c = textureGrad(uTexCol, q, gx, gy);
  // normal detail near the pixel frequency flickers under the TAA jitter: fade it with the footprint (texels per pixel)
  float fp = max(length(gx), length(gy))*1024.0/max(uTexLod.x, 1e-3);
  vec2 nx = (textureGrad(uTexNrm, q, gx, gy).rg*2.0 - 1.0)*(1.0 - uTexLod.w*smoothstep(uTexLod.y, uTexLod.z, fp));
  vec2 rh = textureGrad(uTexRH, q, gx, gy).rg;
  float nz = sqrt(max(1.0 - dot(nx, nx), 0.0));
  vec3 wn = vec3(nx + vec2(dot(ns, T), dot(ns, B)), nz*max(dot(ns, Nf), 0.0));
  s.col += c.rgb*w; s.ao += c.a*w; s.r += rh.x*w; s.h += rh.y*w; s.n += (T*wn.x + B*wn.y + Nf*wn.z)*w;
}
// triplanar: sharpened weights, taps under 4 % dropped (box faces take one tap); walls keep texture up = +y
TexS texTri(float L, vec3 p, vec3 n, vec3 dpx, vec3 dpy, vec2 inv, int rot){
  TexS s = TexS(vec3(0.0), 0.0, 0.0, 0.0, vec3(0.0));
  vec3 a = abs(n), w = a*a; w *= w; w /= max(dot(w, vec3(1.0)), 1e-6);
  w = max(w - 0.04, 0.0); w /= max(dot(w, vec3(1.0)), 1e-6);
  vec3 sg = vec3(n.x < 0.0 ? -1.0 : 1.0, n.y < 0.0 ? -1.0 : 1.0, n.z < 0.0 ? -1.0 : 1.0);
  if (w.x > 0.0) { vec3 T = vec3(0.0, 0.0, -sg.x), B = vec3(0.0, 1.0, 0.0); texFrame(rot, T, B); texTap(L, p, dpx, dpy, inv, T, B, vec3(sg.x, 0.0, 0.0), n, w.x, s); }
  if (w.y > 0.0) { vec3 T = vec3(1.0, 0.0, 0.0), B = vec3(0.0, 0.0, -sg.y); texFrame(rot, T, B); texTap(L, p, dpx, dpy, inv, T, B, vec3(0.0, sg.y, 0.0), n, w.y, s); }
  if (w.z > 0.0) { vec3 T = vec3(sg.z, 0.0, 0.0), B = vec3(0.0, 1.0, 0.0); texFrame(rot, T, B); texTap(L, p, dpx, dpy, inv, T, B, vec3(0.0, 0.0, sg.z), n, w.z, s); }
  s.n = normalize(s.n);
  return s;
}
// terrain layer: albedo / roughness multipliers against the layer's means, ao, world normal delta; strengths and the
// distance fade applied, identity when the layer has no texture
struct TerrT { vec3 alb; float r, ao; vec3 dn; };
// any slot (terrain layers, painted ground: terrTexSlot(uPaintSlot[k], ...)); dpx / dpy = dFdx / dFdy of p taken in
// uniform control flow (kTerrainFS: tdx, tdy), dist = eye distance (tDist)
TerrT terrTexSlot(int slot, vec3 p, vec3 n, vec3 dpx, vec3 dpy, float dist){
  TerrT o = TerrT(vec3(1.0), 1.0, 1.0, vec3(0.0));
  if (!texOn() || slot < 0 || slot >= 80) return o;
  vec4 A = uTexA[slot]; if (A.x < 0.0) return o;
  int li = int(A.x + 0.5); vec4 K = uTexB[slot], C = uTexC[slot], Lp = uTexL[li], M = uTexM[li];
  float f = C.w > 0.0 ? 1.0 - smoothstep(C.z, C.w, dist) : 1.0;
  if (f <= 0.0) return o;
  vec2 inv = Lp.xy*A.w; int rot = int(C.y + 0.5);
  TexS s = TexS(vec3(0.0), 0.0, 0.0, 0.0, vec3(0.0));
  if (int(A.z + 0.5) == 3) {
    vec3 T = vec3(1.0, 0.0, 0.0), B = vec3(0.0, 0.0, -1.0); texFrame(rot, T, B);
    texTap(A.x, p, dpx, dpy, inv, T, B, vec3(0.0, 1.0, 0.0), n, 1.0, s); s.n = normalize(s.n);
  } else s = texTri(A.x, p, n, dpx, dpy, inv, rot);
  o.alb = mix(vec3(1.0), clamp(s.col/max(M.rgb, vec3(0.004)), 0.0, 2.5), K.x*f);
  o.r = mix(1.0, s.r/max(Lp.z, 0.02), K.z*f);
  o.ao = mix(1.0, s.ao, K.w*f);
  o.dn = (s.n - n)*K.y*f;
  return o;
}
TerrT terrTex(int layer, vec3 p, vec3 n, vec3 dpx, vec3 dpy, float dist){ return terrTexSlot(uTerrSlot[layer], p, n, dpx, dpy, dist); }
)GLSL";

const char* kMainGLSL = R"GLSL(
// ---- L4 main-pass hooks (src/textures.cpp): vertex params.x = pattern + 100 * code (0 pattern default, 1 none, 2+ slot)
uniform mat4 uModel;
uniform int uPatSlot[32];
vec3 gTexDN = vec3(0.0); float gTexH = 0.0; bool gTexBump = false;   // world normal delta, height (m) for the derivative bump
int texSlot(float code, int pat){ int c = int(code + 0.5)/100; return c == 0 ? (pat >= 0 && pat < 32 ? uPatSlot[pat] : -1) : c - 2; }
bool texReplace(int slot){ return texOn() && slot >= 0 && slot < 80 && uTexA[slot].x >= 0.0 && uTexA[slot].y > 0.5; }
// sample the slot's set and layer it on the material: albedo and roughness multiplied by the sample against the set's
// means (the material colour, per-stone tint and room finishes stay), ao, the normal (applied after the bump) and the
// height (into the derivative bump, faded by the detail filter like the procedural relief)
void texMaterial(int slot, vec3 w, vec3 nW, vec3 ap, vec3 an, vec3 pc, vec3 pn, int grain, float pix,
                 inout vec3 alb, inout float rough, inout float ao){
  vec3 dwx = dFdx(w), dwy = dFdy(w), dax = dFdx(ap), day = dFdy(ap), dcx = dFdx(pc), dcy = dFdy(pc);
  if (!texOn() || slot < 0 || slot >= 80) return;
  vec4 A = uTexA[slot]; if (A.x < 0.0) return;
  int li = int(A.x + 0.5); vec4 K = uTexB[slot], C = uTexC[slot], Lp = uTexL[li], M = uTexM[li];
  float f = C.w > 0.0 ? 1.0 - smoothstep(C.z, C.w, length(w - uCamPos)) : 1.0;
  if (f <= 0.0) return;
  vec2 inv = Lp.xy*A.w; int sp = int(A.z + 0.5), rot = int(C.y + 0.5);
  vec3 ng = normalize(nW);
  TexS s = TexS(vec3(0.0), 0.0, 0.0, 0.0, vec3(0.0)); vec3 dn = vec3(0.0);
  if (sp == 2) { s = texTri(A.x, w, ng, dwx, dwy, inv, rot); dn = s.n - ng; }
  else if (sp == 1) {
    // part space, dominant face only, texture v along the part's grain axis (planks, slates); the tangent frame is
    // carried to world space by the screen-space cotangent frame (parts are rotated inside the asset)
    vec3 a = abs(pn); int na = a.x >= a.y && a.x >= a.z ? 0 : a.y >= a.z ? 1 : 2;
    float sg = pn[na] < 0.0 ? -1.0 : 1.0;
    vec3 Nf = vec3(0.0); Nf[na] = sg;
    vec3 T = na == 0 ? vec3(0.0, 0.0, -sg) : vec3(na == 1 ? 1.0 : sg, 0.0, 0.0);
    vec3 B = na == 1 ? vec3(0.0, 0.0, -sg) : vec3(0.0, 1.0, 0.0);
    int bAxis = na == 1 ? 2 : 1;
    texFrame(((grain != na && grain != bAxis) ? 1 : 0) + rot & 3, T, B);
    texTap(A.x, pc, dcx, dcy, inv, T, B, Nf, Nf, 1.0, s);
    vec3 tn = vec3(dot(s.n, T), dot(s.n, B), dot(s.n, Nf));
    vec2 ux = vec2(dot(dcx, T), dot(dcx, B)), uy = vec2(dot(dcy, T), dot(dcy, B));
    vec3 p1 = cross(dwy, ng), p2 = cross(ng, dwx);
    vec3 Tw = p1*ux.x + p2*uy.x, Bw = p1*ux.y + p2*uy.y;
    float m = max(dot(Tw, Tw), dot(Bw, Bw));
    if (m > 1e-30) { float im = inversesqrt(m); dn = normalize(Tw*im*tn.x + Bw*im*tn.y + ng*tn.z) - ng; }
  } else {
    vec3 na = normalize(an); s = texTri(A.x, ap, na, dax, day, inv, rot);
    mat3 mm = mat3(uModel); vec3 nw = mm*na; float sc = max(length(nw), 1e-6);
    dn = (mm*s.n - nw)/sc;
  }
  float fa = f*(1.0 - uTexLod2.z*smoothstep(uTexLod2.x, uTexLod2.y, pix*1024.0*max(inv.x, inv.y)));   // detail at the pixel scale: fade (shimmer)
  alb *= mix(vec3(1.0), clamp(s.col/max(M.rgb, vec3(0.004)), 0.0, 2.5), K.x*fa);
  rough = clamp(rough*mix(1.0, s.r/max(Lp.z, 0.02), K.z*fa), 0.04, 1.0);
  ao *= mix(1.0, s.ao, K.w*f);
  gTexDN = dn*K.y*f;
  float fr = 128.0*max(inv.x, inv.y);   // the height map's features: ~8 texels
  gTexH = (s.h - Lp.w)*C.x*f*(1.0 - smoothstep(0.18, 0.5, fr*pix));
  gTexBump = C.x > 0.0;
}
)GLSL";

}  // namespace

// ------------------------------------------------------------------ public
const char* glslCommon() { return kCommonGLSL; }
const char* glslMain() { return kMainGLSL; }

void init(const fs::path& texRoot) {
  auto t0 = std::chrono::steady_clock::now();
  root = texRoot;
  loadMapping(texRoot / "textures.json");
  // every folder with a colour map becomes a layer (sets named only by a material "texture" field need no slot)
  std::vector<fs::path> dirs;
  std::error_code ec;
  if (fs::exists(texRoot, ec)) for (auto& e : fs::directory_iterator(texRoot, ec)) if (e.is_directory() && (fs::exists(e.path() / "color.jpg") || fs::exists(e.path() / "color.png"))) dirs.push_back(e.path());
  std::sort(dirs.begin(), dirs.end());
  {   // only sets a slot uses take GPU memory; the rest stay on disk (a material "texture" field naming one falls back)
    std::set<std::string> want; for (auto& s : slots) want.insert(s.set);
    size_t before = dirs.size();
    dirs.erase(std::remove_if(dirs.begin(), dirs.end(), [&](const fs::path& d) { return !want.count(d.filename().string()); }), dirs.end());
    gInfo.unused = (int)(before - dirs.size());
  }
  if ((int)dirs.size() > kMaxLayers) { logf("textures: %d sets, only the first %d load", (int)dirs.size(), kMaxLayers); dirs.resize(kMaxLayers); }
  bool bc = hasExt("GL_EXT_texture_compression_s3tc") && hasExt("GL_EXT_texture_sRGB");
  gInfo.compressed = bc;
  std::vector<Cpu> cpu(dirs.size());
  std::atomic<size_t> next{0};
  unsigned nt = std::clamp(std::thread::hardware_concurrency(), 1u, 12u);
  std::vector<std::thread> th;
  for (unsigned t = 0; t < nt && t < dirs.size(); ++t) th.emplace_back([&] { for (size_t i; (i = next++) < dirs.size();) buildSet(dirs[i], bc, cpu[i]); });
  for (auto& t : th) t.join();
  sets.clear();
  int n = 0;
  for (size_t i = 0; i < dirs.size(); ++i) if (cpu[i].ok) ++n; else logf("textures: %s skipped (%s)", dirs[i].filename().string().c_str(), cpu[i].err.c_str());
  gpuOn = false;
  for (GLuint& a : arr) if (a) { glDeleteTextures(1, &a); a = 0; }
  gInfo.bytes = 0;
  if (n > 0) {
    GLenum fmt[3] = {bc ? (GLenum)GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT : (GLenum)GL_SRGB8_ALPHA8, bc ? (GLenum)GL_COMPRESSED_RG_RGTC2 : (GLenum)GL_RG8, bc ? (GLenum)GL_COMPRESSED_RG_RGTC2 : (GLenum)GL_RG8};
    for (int k = 0; k < 3; ++k) {
      glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &arr[k]);
      glTextureStorage3D(arr[k], kLevels, fmt[k], kSize, kSize, n);
      glTextureParameteri(arr[k], GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); glTextureParameteri(arr[k], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTextureParameteri(arr[k], GL_TEXTURE_WRAP_S, GL_REPEAT); glTextureParameteri(arr[k], GL_TEXTURE_WRAP_T, GL_REPEAT);
      glTextureParameterf(arr[k], 0x84FE /* GL_TEXTURE_MAX_ANISOTROPY */, 8.0f);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    int layer = 0;
    for (size_t i = 0; i < dirs.size(); ++i) {
      if (!cpu[i].ok) continue;
      for (int k = 0; k < 3; ++k) {
        int s = kSize;
        for (int L = 0; L < kLevels; ++L, s /= 2) {
          auto& d = cpu[i].lv[k][L];
          if (bc) glCompressedTextureSubImage3D(arr[k], L, 0, 0, layer, s, s, 1, fmt[k], (GLsizei)d.size(), d.data());
          else glTextureSubImage3D(arr[k], L, 0, 0, layer, s, s, 1, k == 0 ? GL_RGBA : GL_RG, GL_UNSIGNED_BYTE, d.data());
          gInfo.bytes += d.size();
        }
      }
      SetInfo si = cpu[i].info; si.name = dirs[i].filename().string(); si.layer = layer++;
      sets.push_back(si);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    GLint units = 0; glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &units);
    if (units >= kUnitBase + 3) { for (int k = 0; k < 3; ++k) glBindTextureUnit(kUnitBase + k, arr[k]); gpuOn = true; }
    else logf("textures: GL_MAX_TEXTURE_IMAGE_UNITS = %d, units %d-%d unavailable: procedural only", units, kUnitBase, kUnitBase + 2);
  }
  gInfo.sets = (int)sets.size();
  resolveLayers();
  invalidateKey();
  gInfo.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  logf("textures: %d sets (%s, %.0f MB), %d/%d slots live, %d sets on disk not used by a slot, %.0f ms", gInfo.sets, bc ? "BC3/BC5" : "RGBA8/RG8", gInfo.bytes / 1048576.0,
       gInfo.slotsLive, gInfo.slots, gInfo.unused, gInfo.ms);
  for (auto& m : gInfo.missing) logf("textures: set '%s' not found (its slots stay procedural)", m.c_str());
  uploadAll();
}

void attach(GLuint prog) {
  if (std::find(progs.begin(), progs.end(), prog) == progs.end()) progs.push_back(prog);
  upload(prog);
}

int materialCode(const std::string& matName, int pattern, const std::string& explicitSet) {
  (void)pattern;
  if (!explicitSet.empty()) {
    if (explicitSet == "none") return 1;
    auto it = slotByName.find(explicitSet);
    int s = it != slotByName.end() ? it->second : -1;
    if (s < 0) for (size_t i = 0; i < slots.size(); ++i) if (slots[i].set == explicitSet && slots[i].name.rfind("set:", 0) == 0) s = (int)i;
    if (s < 0) {   // a set with no slot yet: default detail slot in asset space
      s = slotFromJson("set:" + explicitSet, json{{"set", explicitSet}});
      if (s >= 0) { slots[s].layer = findSet(explicitSet); resolveLayers(); uploadAll(); }
    }
    return s >= 0 ? s + 2 : 0;
  }
  auto it = matSlot.find(matName);
  if (it == matSlot.end()) return 0;
  return it->second < 0 ? 1 : it->second + 2;
}

bool active() { return gpuOn; }
bool enabled() { return gpuOn && wantOn; }
void setEnabled(bool on) { wantOn = on; invalidateKey(); for (GLuint p : progs) glProgramUniform1i(p, glGetUniformLocation(p, "uTexOn"), gpuOn && wantOn ? 1 : 0); }
const Info& info() { return gInfo; }
int slotIndex(const std::string& n) { auto it = slotByName.find(n); return it == slotByName.end() ? -1 : it->second; }
int slotLayer(int s) { return s >= 0 && s < (int)slots.size() ? slots[s].layer : -1; }
int patternSlot(int p) { return p >= 0 && p < 32 ? patSlot[p] : -1; }

// ------------------------------------------------------------------ offline
int packSet(const fs::path& src, const fs::path& dst) {
  Img col, nrm, r, ao, h;
  if (!loadImg(src / "color.jpg", 3, col)) { std::printf("pack: no color.jpg in %s\n", src.string().c_str()); return 1; }
  bool hasN = loadImg(src / "normal.jpg", 3, nrm), hasR = loadImg(src / "roughness.jpg", 1, r), hasA = loadImg(src / "ao.jpg", 1, ao), hasH = loadImg(src / "height.jpg", 1, h);
  col = resample(col, kSize);
  if (hasN) nrm = resample(nrm, kSize);
  if (hasR) r = resample(r, kSize);
  if (hasA) ao = resample(ao, kSize);
  if (hasH) h = resample(h, kSize);
  const size_t N = (size_t)kSize * kSize;
  std::vector<uint8_t> rma(N * 3), nn(N * 3);
  for (size_t i = 0; i < N; ++i) {
    rma[i * 3] = hasR ? r.px[i] : 200; rma[i * 3 + 1] = hasA ? ao.px[i] : 255; rma[i * 3 + 2] = hasH ? h.px[i] : 128;
    if (hasN) {   // renormalise after the resample
      float x = nrm.px[i * 3] / 127.5f - 1, y = nrm.px[i * 3 + 1] / 127.5f - 1, z = nrm.px[i * 3 + 2] / 127.5f - 1;
      float l = std::sqrt(x * x + y * y + z * z); if (l < 1e-4f) { x = 0; y = 0; z = 1; l = 1; }
      nn[i * 3] = (uint8_t)std::clamp((x / l + 1) * 127.5f + 0.5f, 0.0f, 255.0f); nn[i * 3 + 1] = (uint8_t)std::clamp((y / l + 1) * 127.5f + 0.5f, 0.0f, 255.0f); nn[i * 3 + 2] = (uint8_t)std::clamp((z / l + 1) * 127.5f + 0.5f, 0.0f, 255.0f);
    } else { nn[i * 3] = 128; nn[i * 3 + 1] = 128; nn[i * 3 + 2] = 255; }
  }
  std::error_code ec; fs::create_directories(dst, ec);
  stbi_flip_vertically_on_write(1);   // loadImg flipped to bottom-up: write the files the right way up again
  bool ok = stbi_write_jpg((dst / "color.jpg").string().c_str(), kSize, kSize, 3, col.px.data(), 92) &&
            stbi_write_jpg((dst / "normal.jpg").string().c_str(), kSize, kSize, 3, nn.data(), 95) &&
            stbi_write_jpg((dst / "rma.jpg").string().c_str(), kSize, kSize, 3, rma.data(), 92);
  stbi_flip_vertically_on_write(0);
  std::printf("pack %s -> %s: %s (normal %d, roughness %d, ao %d, height %d)\n", src.string().c_str(), dst.string().c_str(), ok ? "ok" : "FAILED", hasN, hasR, hasA, hasH);
  return ok ? 0 : 1;
}

// procedural stand-ins with the same file layout (bring-up before the download, and the fallback test)
int writePlaceholders(const fs::path& texRoot, const std::vector<std::string>& names) {
  const int n = kSize;
  for (size_t si = 0; si < names.size(); ++si) {
    fs::path d = texRoot / names[si];
    std::error_code ec; fs::create_directories(d, ec);
    std::vector<uint8_t> col((size_t)n * n * 3), nrm((size_t)n * n * 3), rma((size_t)n * n * 3);
    std::vector<float> H((size_t)n * n);
    uint32_t seed = 2166136261u; for (char c : names[si]) seed = (seed ^ (uint8_t)c) * 16777619u;
    auto hash = [&](int x, int y) { uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed; h = (h ^ (h >> 13)) * 1274126177u; return ((h ^ (h >> 16)) & 0xffff) / 65535.0f; };
    auto noise = [&](float x, float y, int per) {   // tileable value noise
      int x0 = (int)std::floor(x), y0 = (int)std::floor(y); float fx = x - x0, fy = y - y0; fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
      auto v = [&](int a, int b) { return hash(((a % per) + per) % per, ((b % per) + per) % per); };
      return (v(x0, y0) * (1 - fx) + v(x0 + 1, y0) * fx) * (1 - fy) + (v(x0, y0 + 1) * (1 - fx) + v(x0 + 1, y0 + 1) * fx) * fy;
    };
    int kind = (int)(si % 3);   // 0 blotchy plaster, 1 cells (stones), 2 streaks (grain)
    for (int y = 0; y < n; ++y) for (int x = 0; x < n; ++x) {
      float u = (float)x / n, v = (float)y / n, h = 0;
      for (int o = 0, per = 8; o < 5; ++o, per *= 2) h += noise(u * per, v * per, per) / (float)(1 << o);
      h /= 1.94f;
      if (kind == 1) { float cx = std::fmod(u * 6, 1.0f), cy = std::fmod(v * 6, 1.0f); h = 0.5f * h + 0.5f * std::min(1.0f, 6 * std::min(std::min(cx, 1 - cx), std::min(cy, 1 - cy))); }
      if (kind == 2) h = 0.6f * h + 0.4f * (0.5f + 0.5f * std::sin(v * 6.2831853f * 24 + 4 * noise(u * 8, v * 8, 8)));
      H[(size_t)y * n + x] = h;
      float t = 0.75f + 0.5f * h;
      col[((size_t)y * n + x) * 3 + 0] = (uint8_t)std::clamp(150 * t, 0.f, 255.f);
      col[((size_t)y * n + x) * 3 + 1] = (uint8_t)std::clamp(140 * t, 0.f, 255.f);
      col[((size_t)y * n + x) * 3 + 2] = (uint8_t)std::clamp(125 * t, 0.f, 255.f);
    }
    for (int y = 0; y < n; ++y) for (int x = 0; x < n; ++x) {
      auto at = [&](int a, int b) { return H[(size_t)((b + n) % n) * n + (a + n) % n]; };
      float dx = (at(x + 1, y) - at(x - 1, y)) * 6, dy = (at(x, y + 1) - at(x, y - 1)) * 6;
      float l = std::sqrt(dx * dx + dy * dy + 1);
      size_t i = ((size_t)y * n + x) * 3;
      nrm[i] = (uint8_t)((-dx / l + 1) * 127.5f); nrm[i + 1] = (uint8_t)((-dy / l + 1) * 127.5f); nrm[i + 2] = (uint8_t)((1 / l + 1) * 127.5f);
      float h = at(x, y);
      rma[i] = (uint8_t)(255 * (0.6f + 0.35f * (1 - h))); rma[i + 1] = (uint8_t)(255 * std::min(1.0f, 0.55f + h)); rma[i + 2] = (uint8_t)(255 * h);
    }
    bool ok = stbi_write_jpg((d / "color.jpg").string().c_str(), n, n, 3, col.data(), 90) && stbi_write_jpg((d / "normal.jpg").string().c_str(), n, n, 3, nrm.data(), 95) &&
              stbi_write_jpg((d / "rma.jpg").string().c_str(), n, n, 3, rma.data(), 90);
    std::ofstream(d / "meta.json") << json{{"set", names[si]}, {"id", "placeholder"}, {"size", {2.0, 2.0}}, {"license", "generated"}, {"placeholder", true}}.dump(1);
    std::printf("placeholder %s: %s\n", d.string().c_str(), ok ? "ok" : "FAILED");
    if (!ok) return 1;
  }
  return 0;
}

}  // namespace tex

namespace tex {
namespace { std::string gKey; bool gKeyValid = false; }
void invalidateKey() { gKeyValid = false; }
std::string stateKey() {   // cached: thumbnail keys ask once per asset
  if (gKeyValid) return gKey;
  std::string k = enabled() ? "on|" : "off|";
  if (enabled()) {
    k += readTextFile(root / "textures.json");
    std::error_code ec;
    for (auto& s : sets) {
      k += "|" + s.name;
      for (const char* f : {"color.jpg", "normal.jpg", "rma.jpg", "meta.json"}) k += ":" + std::to_string(fs::exists(root / s.name / f, ec) ? (long long)fs::file_size(root / s.name / f, ec) : -1LL);
    }
  }
  gKey = k; gKeyValid = true;
  return gKey;
}
}  // namespace tex
