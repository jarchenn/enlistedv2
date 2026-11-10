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
  std::string under; int underIdx = -1; float expo = 0.6f, contrast = 1.0f, macro = 0;   // MA1: substrate slot behind the macro exposure mask; macro variation strength
  float relD = 0, crev = 0, edge = 0, lumps = 0;   // MA1b relief (Perf relief): parallax depth (m), crevice darkening, edge wear, fill lumps (m)
};
struct SetInfo { std::string name; vec2 size{2, 2}; int layer = -1; vec3 meanLin{0.5f}; float meanAO = 1, meanR = 0.7f, meanH = 0.5f; };

std::vector<Slot> slots;
// MA1 imgmat (docs/textures.md#imgmat): a second slot table, index-parallel to slots (codes baked into vertices stay
// valid). Slots only the imgmat table defines are empty (procedural) in the base table, so imgmat off = the old look.
std::vector<Slot> slotsImg;
int patSlotImg[32], floorSlotImg[8];   // imgmat pattern -> slot; room floor finish code (render.cpp pat 21) -> slot
bool wantImg = true;
GLuint tableUbo = 0;                   // MA1: the active slot / layer tables (NmkTexTables, uniform buffer binding 3)
bool loadedImg = true, loadedBoth = false;   // which table's sets are on the GPU (NMK_TEX_ALL=1: both, for the imgmat A/B)
GLuint macroTex = 0;                   // MA1: world-space macro variation (unit 25): R tint, G roughness, B damp, A substrate exposure
std::map<std::string, int> slotByName;
std::map<std::string, int> matSlot;    // material name -> slot, -2 = none
std::map<std::string, std::string> matImg;   // MA1: material -> imgmat slot name, for materials without a base rule
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

Slot parseSlot(const std::string& name, const json& j) {
  Slot s; s.name = name;
  s.set = j.value("set", "");
  std::string m = j.value("mode", "detail"); s.mode = m == "replace" ? 1 : 0;
  std::string sp = j.value("space", "asset"); s.space = sp == "part" ? 1 : sp == "world" ? 2 : sp == "ground" ? 3 : 0;
  s.scale = j.value("scale", 1.0f); s.rot = j.value("rot", 0) & 3;
  s.kAlb = j.value("albedo", s.kAlb); s.kN = j.value("normal", s.kN); s.kR = j.value("rough", s.kR);
  s.kAO = j.value("ao", s.kAO); s.kH = j.value("height", s.kH);
  if (j.contains("fade") && j["fade"].is_array() && j["fade"].size() == 2) { s.fadeA = j["fade"][0]; s.fadeB = j["fade"][1]; }
  s.under = j.value("under", ""); s.expo = j.value("exposure", s.expo); s.macro = j.value("macro", 0.0f); s.contrast = j.value("contrast", 1.0f);
  s.relD = j.value("relief", s.mode == 1 ? 0.012f : 0.0f); s.crev = j.value("crevice", s.mode == 1 ? 0.4f : 0.0f); s.edge = j.value("edge", s.mode == 1 ? 0.6f : 0.0f); s.lumps = j.value("lumps", 0.0f);   // MA1b: replace sets (their own structure) get relief by default
  return s;
}
int slotFromJson(const std::string& name, const json& j) {   // a slot of both tables
  if ((int)slots.size() >= kMaxSlots) { logf("textures: more than %d slots, '%s' ignored", kMaxSlots, name.c_str()); return -1; }
  Slot s = parseSlot(name, j);
  slots.push_back(s); slotsImg.push_back(s); slotByName[name] = (int)slots.size() - 1;
  return (int)slots.size() - 1;
}

void loadMapping(const fs::path& file) {
  slots.clear(); slotsImg.clear(); slotByName.clear(); matSlot.clear(); matImg.clear();
  std::fill(std::begin(patSlotImg), std::end(patSlotImg), -1); std::fill(std::begin(floorSlotImg), std::end(floorSlotImg), -1);
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
  // MA1 imgmat table: slot overrides (an existing name changes only the imgmat table) and imgmat-only slots (empty in
  // the base table); patterns and floor finishes switch per table; materials that had no rule may name a slot (an
  // enabled base slot gets an "img:" alias that is empty in the base table)
  std::copy(std::begin(patSlot), std::end(patSlot), std::begin(patSlotImg));
  json ji = j.value("imgmat", json::object()), jis = ji.value("slots", json::object()), jip = ji.value("patterns", json::object()),
       jim = ji.value("materials", json::object()), jif = ji.value("floors", json::object());
  auto imgOnly = [&](const std::string& name, const Slot& img) {
    if ((int)slots.size() >= kMaxSlots) { logf("textures: more than %d slots, imgmat '%s' ignored", kMaxSlots, name.c_str()); return -1; }
    Slot off; off.name = name; slots.push_back(off); slotsImg.push_back(img); slotByName[name] = (int)slots.size() - 1;
    return (int)slots.size() - 1;
  };
  for (auto& [k, v] : jis.items()) { auto it = slotByName.find(k); if (it != slotByName.end()) slotsImg[it->second] = parseSlot(k, v); else imgOnly(k, parseSlot(k, v)); }
  for (auto& [k, v] : jip.items()) { int p = std::atoi(k.c_str()); if (p >= 0 && p < 32) patSlotImg[p] = std::max(-1, ref(v.get<std::string>())); }
  for (auto& [k, v] : jif.items()) { int c = std::atoi(k.c_str()); if (c >= 0 && c < 8) floorSlotImg[c] = std::max(-1, ref(v.get<std::string>())); }
  for (auto& [k, v] : jim.items()) {
    if (matSlot.count(k)) { logf("textures: imgmat material '%s' already has a rule (kept)", k.c_str()); continue; }
    if (ref(v.get<std::string>()) >= 0) matImg[k] = v.get<std::string>();
  }
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
  glProgramUniform1i(p, U("uImgMat"), wantImg ? 1 : 0); glProgramUniform1i(p, U("uTexSkipFar"), wantImg ? 1 : 0);
  glProgramUniform1iv(p, U("uFloorSlot"), 8, floorSlotImg);
  glProgramUniform1i(p, U("uTexMacro"), kUnitBase + 5);   // unit 25 (23 ground paint, 24 terrain cache)
  vec4 lod(3.0f, 2.0f, 16.0f, 0.95f), lod2(8.0f, 48.0f, 0.7f, 1.6f);   // lod2.w (MA1b): replace sets' gradient scale under relief   // tuned on --selftest-render (docs/textures.md#shimmer); NMK_TEX_LOD="a,b,c,d" overrides (A/B)
  if (const char* e = std::getenv("NMK_TEX_LOD")) std::sscanf(e, "%f,%f,%f,%f", &lod.x, &lod.y, &lod.z, &lod.w);
  if (const char* e = std::getenv("NMK_TEX_LOD2")) std::sscanf(e, "%f,%f,%f,%f", &lod2.x, &lod2.y, &lod2.z, &lod2.w);
  glProgramUniform4fv(p, U("uTexLod"), 1, &lod.x); glProgramUniform4fv(p, U("uTexLod2"), 1, &lod2.x);
  glProgramUniform1iv(p, U("uPatSlot"), 32, wantImg ? patSlotImg : patSlot); glProgramUniform1iv(p, U("uTerrSlot"), 8, terrSlot); glProgramUniform1iv(p, U("uPaintSlot"), kPaintTypes, paintSlot);
}
void uploadTables() {   // the active table into NmkTexTables (std140: vec4 arrays, no padding)
  std::vector<vec4> d((size_t)kMaxSlots * 5 + (size_t)kMaxLayers * 2);
  vec4 *A = &d[0], *B = A + kMaxSlots, *C = B + kMaxSlots, *Uu = C + kMaxSlots, *Ly = Uu + kMaxSlots, *M = Ly + kMaxLayers;
  vec4* R = M + kMaxLayers;   // MA1b: after the layers (std140 order of NmkTexTables)
  for (int i = 0; i < kMaxSlots; ++i) { A[i] = vec4(-1, 0, 0, 1); Uu[i] = vec4(-1, 0, 0, 0); }
  for (int i = 0; i < kMaxLayers; ++i) { Ly[i] = vec4(0.5f, 0.5f, 0.7f, 0.5f); M[i] = vec4(0.5f); }
  const std::vector<Slot>& tbl = wantImg ? slotsImg : slots;
  for (size_t i = 0; i < tbl.size() && i < (size_t)kMaxSlots; ++i) {
    const Slot& s = tbl[i];
    A[i] = vec4((float)s.layer, (float)s.mode, (float)s.space, s.scale);
    B[i] = vec4(s.kAlb, s.kN, s.kR, s.kAO); C[i] = vec4(s.kH, (float)s.rot, s.fadeA, s.fadeB);
    Uu[i] = vec4((float)s.underIdx, s.expo, s.contrast, s.macro);
    R[i] = vec4(s.relD, s.crev, s.edge, s.lumps);
  }
  for (auto& s : sets) if (s.layer >= 0 && s.layer < kMaxLayers) { Ly[s.layer] = vec4(1.0f / s.size.x, 1.0f / s.size.y, s.meanR, s.meanH); M[s.layer] = vec4(s.meanLin, s.meanAO); }
  if (!tableUbo) { glCreateBuffers(1, &tableUbo); glNamedBufferStorage(tableUbo, (GLsizeiptr)(d.size() * sizeof(vec4)), nullptr, GL_DYNAMIC_STORAGE_BIT); }
  glNamedBufferSubData(tableUbo, 0, (GLsizeiptr)(d.size() * sizeof(vec4)), d.data());
  glBindBufferBase(GL_UNIFORM_BUFFER, 3, tableUbo);
}
void uploadAll() { uploadTables(); for (GLuint p : progs) upload(p); }

// MA1 macro variation: 256^2 RGBA8, tileable value-noise fBm (one tile = 32 m in the shader), mipmapped, unit 25.
// R broad tint, G roughness, B damp / weathering (broadest), A substrate exposure (patches of ~2-3 m, ragged edges)
void makeMacro() {
  const int N = 256;
  auto h = [](int x, int y, int s) { uint32_t v = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)s * 2246822519u; v = (v ^ (v >> 13)) * 1274126177u; return ((v ^ (v >> 16)) & 0xffffff) / 16777216.0f; };
  auto vn = [&](float x, float y, int cells, int s) {   // value noise wrapping every 'cells' cells
    int x0 = (int)std::floor(x), y0 = (int)std::floor(y); float fx = x - x0, fy = y - y0; fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    auto w = [&](int a) { return ((a % cells) + cells) % cells; };
    float a = h(w(x0), w(y0), s), b = h(w(x0 + 1), w(y0), s), c = h(w(x0), w(y0 + 1), s), d = h(w(x0 + 1), w(y0 + 1), s);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
  };
  auto fbm = [&](float u, float v, int cells, int s) { float t = 0, amp = 0.5f, n = 0; for (int o = 0; o < 4; ++o) { int c = cells << o; t += amp * vn(u * c, v * c, c, s + o * 17); n += amp; amp *= 0.5f; } return t / n; };
  std::vector<uint8_t> px((size_t)N * N * 4);
  for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x) {
    float u = (x + 0.5f) / N, v = (y + 0.5f) / N;
    float r = fbm(u, v, 4, 1), g = fbm(u, v, 6, 2), b = fbm(u, v, 3, 3), a = fbm(u, v, 12, 4);
    auto q = [](float t) { return (uint8_t)std::clamp(t * 255.0f + 0.5f, 0.0f, 255.0f); };
    uint8_t* p = &px[((size_t)y * N + x) * 4]; p[0] = q(r); p[1] = q(g); p[2] = q(b); p[3] = q(a);
  }
  if (macroTex) glDeleteTextures(1, &macroTex);
  glCreateTextures(GL_TEXTURE_2D, 1, &macroTex);
  glTextureStorage2D(macroTex, 9, GL_RGBA8, N, N);
  glTextureSubImage2D(macroTex, 0, 0, 0, N, N, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  glGenerateTextureMipmap(macroTex);
  glTextureParameteri(macroTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); glTextureParameteri(macroTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameteri(macroTex, GL_TEXTURE_WRAP_S, GL_REPEAT); glTextureParameteri(macroTex, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glBindTextureUnit(kUnitBase + 5, macroTex);
}

void resolveLayers() {
  gInfo.slotsLive = 0; gInfo.missing.clear();
  for (auto* tbl : {&slots, &slotsImg}) for (auto& s : *tbl) {
    s.layer = s.set.empty() ? -1 : findSet(s.set);
    auto u = slotByName.find(s.under); s.underIdx = s.under.empty() || u == slotByName.end() ? -1 : u->second;
    if (tbl != (wantImg ? &slotsImg : &slots)) continue;   // counts: the active table
    if (s.layer >= 0) ++gInfo.slotsLive;
    else if (!s.set.empty() && std::find(gInfo.missing.begin(), gInfo.missing.end(), s.set) == gInfo.missing.end()) gInfo.missing.push_back(s.set);
  }
  gInfo.slots = (int)slots.size();
}

const char* kCommonGLSL = R"GLSL(
// ---- L4 texture layer (src/textures.cpp, docs/textures.md): ambientCG sets in three 2D arrays (units 20-22)
uniform sampler2DArray uTexCol, uTexNrm, uTexRH;   // colour (sRGB) + ao, normal xy, roughness + height
uniform int uTexOn;
uniform int uTexSkipFar;
uniform int uImgMat;       // MA1 imgmat table active (render.cpp: room floor finishes take uFloorSlot; terrain layers alternate sets)
uniform sampler2D uTexMacro;   // MA1 macro variation (unit 25): R tint, G roughness, B damp, A substrate / alternate-set mask; one tile = 32 m
float gTexNFade = 1.0;     // MA1: share of the far normal fade (texSlotTaps: 0.5 for replace sets under imgmat)   // MA1 imgmat: 1 = skip normal taps whose detail is faded out (far)
// MA1: the slot / layer tables live in one uniform buffer (binding 3, 14 KB): as plain uniforms they passed the 1024
// constant registers of a fragment program once the tables grew (160 slots, 128 layers)
layout(std140, binding = 3) uniform NmkTexTables {
  vec4 uTexA[160];   // per slot: layer (-1 none), mode (0 detail, 1 replace), space (0 asset, 1 part, 2 world, 3 ground), scale
  vec4 uTexB[160];   // per slot: albedo, normal, roughness, ao strength
  vec4 uTexC[160];   // per slot: height (m), quarter turns, distance fade start, end (m; end 0 = no fade)
  vec4 uTexU[160];   // MA1 per slot: substrate ("under") slot (-1 none), exposure threshold, contrast (replace sets), macro variation strength
  vec4 uTexL[128];   // per layer: 1/size u, 1/size v (1/m), mean roughness, mean height
  vec4 uTexM[128];   // per layer: mean linear colour, mean ao
  vec4 uTexR[160];   // MA1b per slot: relief depth (m), crevice darkening, edge wear, fill lumps (m) (Perf relief)
};
uniform int uTerrSlot[8]; // terrain layers: grass, soil, mud, gravel, sand, rock, scree, crater
#define NMK_TEX_PAINT 1   // painted ground: uPaintSlot[k] for marsh, dirt, mud, gravel, sand, cobbles, rock, dry_grass, ploughed, leaf_litter, scorched, pebbles
uniform int uPaintSlot[12];
uniform vec4 uTexLod;     // gradient scale (LOD bias against TAA-jitter shimmer), normal fade start / end (texels per pixel), fade depth
uniform vec4 uTexLod2;    // albedo / roughness detail fade start / end (texels per pixel), fade depth
struct TexS { vec3 col; float ao, r, h; vec3 n; float d; };   // d: MA1b detail height (0-centred) near the eye
// GX5 parallax occlusion (High): the eye direction (world, toward the eye) and the height in uv units; 0 = off. Set by
// texMaterial for slots with a height, near the eye; the taps march the height map before sampling (docs/textures.md)
vec3 gPomV = vec3(0.0); float gPomK = 0.0;
int gPomN = 10;          // MA1b: parallax steps (High 10, Medium 4 under Perf relief)
float gTexGS = 0.0;      // MA1b: gradient scale for this slot's taps (0 = uTexLod.x); replace sets under relief: uTexLod2.w
float gDetF = 0.0;       // MA1b: detail height tap near the eye (0 = off)
bool texOn(){ return uTexOn == 1 && uLook == 1; }
void texFrame(int q, inout vec3 T, inout vec3 B){ vec3 t = T; if (q == 1) { T = B; B = -t; } else if (q == 2) { T = -T; B = -B; } else if (q == 3) { T = -B; B = t; } }
// one planar tap: frame T (u), B (v), Nf (out of the plane); explicit gradients (taps sit in divergent branches);
// the tangent-space normal is whiteout-blended with the surface normal ns
void texTap(float L, vec3 p, vec3 dpx, vec3 dpy, vec2 inv, vec3 T, vec3 B, vec3 Nf, vec3 ns, float w, inout TexS s){
  vec3 q = vec3(dot(p, T)*inv.x, dot(p, B)*inv.y, L);
  float gs = gTexGS > 0.0 ? gTexGS : uTexLod.x;
  vec2 gx = vec2(dot(dpx, T), dot(dpx, B))*inv*gs, gy = vec2(dot(dpy, T), dot(dpy, B))*inv*gs;
  if (gPomK > 0.0 && w > 0.3) {   // GX5 POM: march from the top of the relief down until the height map is above the ray
    vec3 Vt = vec3(dot(gPomV, T), dot(gPomV, B), max(dot(gPomV, Nf), 0.2));
    if (gPomN == 1) q.xy -= Vt.xy*gPomK*(1.0 - textureGrad(uTexRH, q, gx, gy).g);   // MA1b Medium: single offset, offset limiting (one tap; a 4-step march cost +1.6 ms in chateau_town)
    else {
      float n = float(gPomN);
      vec2 step = Vt.xy/Vt.z*gPomK/n;
      float depth = 0.0, hd = 1.0 - textureGrad(uTexRH, q, gx, gy).g, pd = 0.0, ph = hd; vec2 pq = q.xy;
      for (int i = 0; i < 10 && i < gPomN && depth < hd; ++i) { pq = q.xy; pd = depth; ph = hd; q.xy -= step; depth += 1.0/n; hd = 1.0 - textureGrad(uTexRH, q, gx, gy).g; }
      float a = depth - hd, b = ph - pd;   // MA1b: refine linearly between the last two steps (no stair lines)
      if (a >= 0.0 && a + b > 1e-4) q.xy = mix(q.xy, pq, a/(a + b));
    }
  }
  if (gDetF > 0.0 && w > 0.3) s.d += (textureGrad(uTexRH, vec3(q.xy*3.7 + vec2(0.37, 0.61), q.z), gx*3.7, gy*3.7).g - 0.5)*w;   // MA1b: the set's own grain / pores at 3.7x
  vec4 c = textureGrad(uTexCol, q, gx, gy);
  // normal detail near the pixel frequency flickers under the TAA jitter: fade it with the footprint (texels per pixel)
  float fp = max(length(gx), length(gy))*1024.0/max(gs, 1e-3);
  float nfk = 1.0 - uTexLod.w*gTexNFade*smoothstep(uTexLod.y, uTexLod.z, fp);
  vec2 nx = (uTexSkipFar == 1 && nfk < 0.06) ? vec2(0.0) : (textureGrad(uTexNrm, q, gx, gy).rg*2.0 - 1.0)*nfk;   // MA1 imgmat: far, no normal tap
  vec2 rh = textureGrad(uTexRH, q, gx, gy).rg;
  float nz = sqrt(max(1.0 - dot(nx, nx), 0.0));
  vec3 wn = vec3(nx + vec2(dot(ns, T), dot(ns, B)), nz*max(dot(ns, Nf), 0.0));
  s.col += c.rgb*w; s.ao += c.a*w; s.r += rh.x*w; s.h += rh.y*w; s.n += (T*wn.x + B*wn.y + Nf*wn.z)*w;
}
// triplanar: sharpened weights, taps under 4 % dropped (box faces take one tap); walls keep texture up = +y
TexS texTri(float L, vec3 p, vec3 n, vec3 dpx, vec3 dpy, vec2 inv, int rot){
  TexS s = TexS(vec3(0.0), 0.0, 0.0, 0.0, vec3(0.0), 0.0);
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
TerrT terrTexOne(int slot, vec3 p, vec3 n, vec3 dpx, vec3 dpy, float dist){
  TerrT o = TerrT(vec3(1.0), 1.0, 1.0, vec3(0.0));
  if (!texOn() || slot < 0 || slot >= 160) return o;
  vec4 A = uTexA[slot]; if (A.x < 0.0) return o;
  int li = int(A.x + 0.5); vec4 K = uTexB[slot], C = uTexC[slot], Lp = uTexL[li], M = uTexM[li];
  float f = C.w > 0.0 ? 1.0 - smoothstep(C.z, C.w, dist) : 1.0;
  if (f <= 0.0) return o;
  vec2 inv = Lp.xy*A.w; int rot = int(C.y + 0.5);
  TexS s = TexS(vec3(0.0), 0.0, 0.0, 0.0, vec3(0.0), 0.0);
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
// MA1 imgmat: a slot with an "under" set alternates with it by the world macro mask (A), patches of a few metres, so
// two ground sets break each other's tiling; both are sampled only in the narrow transition band
TerrT terrTexSlot(int slot, vec3 p, vec3 n, vec3 dpx, vec3 dpy, float dist){
  if (uImgMat == 1 && slot >= 0 && slot < 160 && uTexU[slot].x >= 0.0) {
    vec3 a = abs(n); bool fl = a.y > max(a.x, a.z), xw = a.x > a.z;
    vec2 uv = fl ? p.xz : xw ? p.zy : p.xy;
    float lod = log2(max(max(length(dpx), length(dpy)), 1e-4)*256.0/32.0);
    float m = smoothstep(uTexU[slot].y, uTexU[slot].y + 0.12, textureLod(uTexMacro, uv/32.0 + 0.37, max(lod, 0.0)).a);
    int u = int(uTexU[slot].x + 0.5);
    if (m <= 0.001) return terrTexOne(slot, p, n, dpx, dpy, dist);
    if (m >= 0.999) return terrTexOne(u, p, n, dpx, dpy, dist);
    TerrT x = terrTexOne(slot, p, n, dpx, dpy, dist), y = terrTexOne(u, p, n, dpx, dpy, dist);
    return TerrT(mix(x.alb, y.alb, m), mix(x.r, y.r, m), mix(x.ao, y.ao, m), mix(x.dn, y.dn, m));
  }
  return terrTexOne(slot, p, n, dpx, dpy, dist);
}
TerrT terrTex(int layer, vec3 p, vec3 n, vec3 dpx, vec3 dpy, float dist){ return terrTexSlot(uTerrSlot[layer], p, n, dpx, dpy, dist); }
)GLSL";

const char* kMainGLSL = R"GLSL(
// ---- L4 main-pass hooks (src/textures.cpp): vertex params.x = pattern + 100 * code (0 pattern default, 1 none, 2+ slot)
uniform mat4 uModel;
uniform int uPatSlot[32];
uniform int uMatLayer;   // GX5 layered materials (render.cpp kMainFS, docs/graphics-overhaul.md#gx5): 0 off, 1 on, 2 High (+ POM)
vec3 gTexDN = vec3(0.0); float gTexH = 0.0; bool gTexBump = false;   // world normal delta, height (m) for the derivative bump
uniform int uFloorSlot[8]; // MA1: room floor finish code -> slot (imgmat)
uniform int uRelief;       // MA1b (Perf relief): parallax on Medium, per-family depth, crevices, detail grain, worn edges
layout(location=11) flat in vec3 vPcF;         // MA1b: the triangle's provoking vertex part coordinate: a corner of a box face (edge distance)
float gTexWear = 1.0;      // the copy's wear (kMainFS sets uInst.y before texMaterial): more substrate on worn buildings
int texSlot(float code, int pat){ int c = int(code + 0.5)/100; return c == 0 ? (pat >= 0 && pat < 32 ? uPatSlot[pat] : -1) : c - 2; }
bool texReplace(int slot){ return texOn() && slot >= 0 && slot < 160 && uTexA[slot].x >= 0.0 && uTexA[slot].y > 0.5; }
// sample the slot's set and layer it on the material: albedo and roughness multiplied by the sample against the set's
// means (the material colour, per-stone tint and room finishes stay), ao, the normal (applied after the bump) and the
// height (into the derivative bump, faded by the detail filter like the procedural relief)
// one slot's taps in its own mapping space: colour / ao / roughness / height, and the world normal delta dn
TexS texSlotTaps(int slot, vec3 w, vec3 ng, vec3 ap, vec3 an, vec3 pc, vec3 pn, int grain,
                 vec3 dwx, vec3 dwy, vec3 dax, vec3 day, vec3 dcx, vec3 dcy, out vec3 dn){
  vec4 A = uTexA[slot], C = uTexC[slot];
  vec2 inv = uTexL[int(A.x + 0.5)].xy*A.w; int sp = int(A.z + 0.5), rot = int(C.y + 0.5);
  TexS s = TexS(vec3(0.0), 0.0, 0.0, 0.0, vec3(0.0), 0.0); dn = vec3(0.0);
  gTexNFade = (uImgMat == 1 && A.y > 0.5) ? 0.5 : 1.0;
  gTexGS = uRelief == 1 && A.y > 0.5 ? uTexLod2.w : 0.0;   // MA1b: the image is the structure; less LOD bias keeps courses and tiles at 15-35 m
  vec3 pv = gPomV;   // the eye direction (world): each space below takes its own copy
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
    vec2 ux = vec2(dot(dcx, T), dot(dcx, B)), uy = vec2(dot(dcy, T), dot(dcy, B));
    vec3 p1 = cross(dwy, ng), p2 = cross(ng, dwx);
    vec3 Tw = p1*ux.x + p2*uy.x, Bw = p1*ux.y + p2*uy.y;
    // MA1b: parallax in part space: the eye direction in the part's (T, B, Nf) frame via the cotangent frame
    if (gPomK > 0.0) gPomV = T*dot(pv, Tw*inversesqrt(max(dot(Tw, Tw), 1e-30))) + B*dot(pv, Bw*inversesqrt(max(dot(Bw, Bw), 1e-30))) + Nf*dot(pv, ng);
    texTap(A.x, pc, dcx, dcy, inv, T, B, Nf, Nf, 1.0, s);
    gPomV = pv;
    vec3 tn = vec3(dot(s.n, T), dot(s.n, B), dot(s.n, Nf));
    float m = max(dot(Tw, Tw), dot(Bw, Bw));
    if (m > 1e-30) { float im = inversesqrt(m); dn = normalize(Tw*im*tn.x + Bw*im*tn.y + ng*tn.z) - ng; }
  } else {
    vec3 na = normalize(an);
    if (gPomK > 0.0) gPomV = normalize(transpose(mat3(uModel))*pv);   // asset space (rotation only)
    s = texTri(A.x, ap, na, dax, day, inv, rot);
    gPomV = pv;
    mat3 mm = mat3(uModel); vec3 nw = mm*na; float sc = max(length(nw), 1e-6);
    dn = (mm*s.n - nw)/sc;
  }
  return s;
}
// MA1 macro variation: one tap, planar on the dominant world axis (walls x/z by y, floors and roofs xz)
vec4 texMacro(vec3 w, vec3 n, vec3 dwx, vec3 dwy){
  vec3 a = abs(n); bool fl = a.y > max(a.x, a.z), xw = a.x > a.z;
  vec2 uv = fl ? w.xz : xw ? w.zy : w.xy, gx = fl ? dwx.xz : xw ? dwx.zy : dwx.xy, gy = fl ? dwy.xz : xw ? dwy.zy : dwy.xy;
  return textureGrad(uTexMacro, uv/32.0, gx/32.0, gy/32.0);
}
// MA1 / MA2: the macro damp channel (B, broad patches ~10 m, 0..1) at world w with normal n; pix = metres per pixel (kMainFS
// gPix). No derivatives (callable in divergent code); the texture is bound whether imgmat is on or off
float macroDamp(vec3 w, vec3 n, float pix){
  vec3 a = abs(n); vec2 uv = a.y > max(a.x, a.z) ? w.xz : a.x > a.z ? w.zy : w.xy;
  return textureLod(uTexMacro, uv/32.0, max(0.0, log2(pix*256.0/32.0))).b;
}
void texMaterial(int slot, vec3 w, vec3 nW, vec3 ap, vec3 an, vec3 pc, vec3 pn, int grain, float pix,
                 inout vec3 alb, inout float rough, inout float ao){
  vec3 dwx = dFdx(w), dwy = dFdy(w), dax = dFdx(ap), day = dFdy(ap), dcx = dFdx(pc), dcy = dFdy(pc);
  if (!texOn() || slot < 0 || slot >= 160) return;
  vec4 A = uTexA[slot]; if (A.x < 0.0) return;
  int li = int(A.x + 0.5); vec4 K = uTexB[slot], C = uTexC[slot], Lp = uTexL[li], M = uTexM[li];
  float f = C.w > 0.0 ? 1.0 - smoothstep(C.z, C.w, length(w - uCamPos)) : 1.0;
  if (f <= 0.0) return;
  vec2 inv = Lp.xy*A.w; int sp = int(A.z + 0.5);
  vec3 ng = normalize(nW);
  float eyeD = length(w - uCamPos);
  gPomK = 0.0;
  vec4 R = uTexR[slot];
  bool rel = uRelief == 1 && R.x + R.y + R.z > 0.0;   // MA1b: this slot's family has relief (textures.json "relief" / "crevice" / "edge")
  float pomD = rel ? R.x : max(C.x, A.y > 0.5 ? 0.015 : 0.0);   // GX5: replace-mode sets (their own structure: brick, slates, cobbles) get 1.5 cm
  float pomR = uMatLayer == 2 ? 14.0 : 8.0;   // MA1b: Medium one offset within 8 m, High a 10-step march within 14 m
  gPomN = uMatLayer == 2 ? 10 : 1;
  if ((uMatLayer == 2 || rel) && pomD > 0.0 && (sp != 1 || rel) && eyeD < pomR) {
    gPomV = normalize(uCamPos - w);
    gPomK = pomD*max(inv.x, inv.y)*(1.0 - smoothstep(pomR*0.6, pomR, eyeD))*(rel ? smoothstep(0.08, 0.3, abs(dot(ng, gPomV))) : 1.0);   // MA1b: off at grazing angles (swim)
  }
  gDetF = rel ? 1.0 - smoothstep(2.5, 7.0, eyeD) : 0.0;
  vec3 dn; TexS s = texSlotTaps(slot, w, ng, ap, an, pc, pn, grain, dwx, dwy, dax, day, dcx, dcy, dn);
  gPomK = 0.0;
  // detail at the pixel scale: fade (shimmer). MA1 imgmat: not in replace mode, where the image is the structure (mips filter it)
  float fa = (uImgMat == 1 && A.y > 0.5) ? f : f*(1.0 - uTexLod2.z*smoothstep(uTexLod2.x, uTexLod2.y, pix*1024.0*max(inv.x, inv.y)));
  vec3 ratio = clamp(s.col/max(M.rgb, vec3(0.004)), 0.0, 2.5);
  if (uImgMat == 1 && uTexU[slot].z != 1.0) { float l = max(dot(ratio, vec3(0.2126, 0.7152, 0.0722)), 1e-3); ratio *= pow(l, uTexU[slot].z - 1.0); }   // MA1: per-set contrast
  vec3 a2 = alb*mix(vec3(1.0), ratio, K.x*fa);
  float r2 = rough*mix(1.0, s.r/max(Lp.z, 0.02), K.z*fa), ao2 = mix(1.0, s.ao, K.w*f);
  vec3 dn2 = dn*(rel && A.y > 0.5 ? max(K.y, 1.0) : K.y)*f*(rel ? 1.5 - 0.5*smoothstep(6.0, 16.0, eyeD) : 1.0);   // MA1b: replace sets at full strength, x1.5 near the eye
  float fr = 128.0*max(inv.x, inv.y);   // the height map's features: ~8 texels
  float h2 = (s.h - Lp.w)*C.x*f*(1.0 - smoothstep(0.18, 0.5, fr*pix));
  if (rel) {
    // crevices: below the set's mean height (mortar, joints, tile laps, fissures, bark furrows) sees less light; in the
    // albedo too, so the courses still read where the relief is under a pixel (15-35 m)
    float crev = smoothstep(0.02, 0.3, Lp.w - s.h)*R.y*f;
    ao2 *= 1.0 - 1.1*crev; a2 *= 1.0 - 0.55*crev;
    h2 += s.d*0.006*gDetF*(1.0 - smoothstep(0.18, 0.5, fr*3.7*pix));   // the set's grain / pores at 3.7x, near the eye (derivative bump)
    // worn edges on box parts (assets.cpp pushQuad: |pnrm| = 2): distance to the face's edges from its corner (vPcF);
    // the band is broken by the set's height, lighter and rougher, and rounded into the derivative bump
    vec3 e3 = abs(vPcF) - abs(pc); vec3 ap3 = abs(pn); int na = ap3.x >= ap3.y && ap3.x >= ap3.z ? 0 : ap3.y >= ap3.z ? 1 : 2; e3[na] = 1e3;
    float ed = min(min(e3.x, e3.y), e3.z);
    if (R.w > 0.0 && pix < 0.06) {   // fill lumps (sandbags): broad bulges and creases in part space, into the derivative bump
      float lu = vnoise(pc*4.5 + 3.1)*0.7 + vnoise(pc*11.0 + 7.7)*0.3;
      h2 += (lu - 0.5)*R.w*(1.0 - smoothstep(0.02, 0.06, pix)); ao2 *= 0.8 + 0.2*lu;
    }
    if (R.z > 0.0 && dot(pn, pn) > 2.0 && ed > -0.005 && pix < 0.03) {
      float em = (1.0 - smoothstep(0.0, 0.015 + 0.05*s.h, ed))*(1.0 - smoothstep(0.012, 0.03, pix))*R.z;
      a2 *= 1.0 + 0.22*em*(0.4 + s.h); r2 = mix(r2, min(r2*1.2, 1.0), em);
      h2 -= em*em*0.01;
    }
  }
  vec4 Uu = uTexU[slot];
  if (uImgMat == 1 && (Uu.w > 0.0 || Uu.x >= 0.0)) {   // MA1: large-scale variation and a substrate, one macro tap
    vec4 mac = texMacro(w, ng, dwx, dwy);
    if (Uu.w > 0.0) {   // broad tint / fading, roughness drift, damp patches (identity at distance comes from here)
      a2 *= mix(vec3(1.0), vec3(0.86, 0.84, 0.80) + vec3(0.30, 0.31, 0.33)*mac.r, Uu.w);
      r2 *= 1.0 + (mac.g - 0.5)*0.35*Uu.w;
      a2 *= 1.0 - 0.16*Uu.w*smoothstep(0.58, 0.82, mac.b);
    }
    int u = int(Uu.x + 0.5);
    float m = Uu.x >= 0.0 ? smoothstep(Uu.y, Uu.y + 0.08, mac.a + (gTexWear - 1.0)*0.12) : 0.0;
    if (m > 0.001 && uTexA[u].x >= 0.0) {   // the substrate (masonry under render, brick behind plaster): its own image colour
      vec3 dnu; TexS su = texSlotTaps(u, w, ng, ap, an, pc, pn, grain, dwx, dwy, dax, day, dcx, dcy, dnu);
      float rim = m*(1.0 - m)*4.0;   // the broken render edge: a thin shadowed lip
      a2 = mix(a2, su.col, m)*(1.0 - (rel ? 0.4 : 0.25)*rim); r2 = mix(r2, su.r, m); ao2 = mix(ao2, su.ao, m);
      dn2 = mix(dn2, dnu*uTexB[u].y, m); h2 -= (rel ? 0.014 : 0.006)*m;   // MA1b: a visible step down to the substrate
    }
  }
  alb = a2; rough = clamp(r2, 0.04, 1.0); ao *= ao2;
  gTexDN = dn2; gTexH = h2;
  gTexBump = C.x > 0.0 || h2 != 0.0;
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
    // only the active table's sets take GPU memory (NMK_TEX_ALL=1: both tables, so the imgmat A/B flips without a reload)
    loadedBoth = std::getenv("NMK_TEX_ALL") != nullptr; loadedImg = wantImg;
    std::set<std::string> want;
    if (loadedBoth || !wantImg) for (auto& s : slots) want.insert(s.set);
    if (loadedBoth || wantImg) for (auto& s : slotsImg) want.insert(s.set);
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
  makeMacro(); gInfo.bytes += 256 * 256 * 4 * 4 / 3;
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
  if (!tableUbo) uploadTables();
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
  if (it == matSlot.end()) {
    auto im = matImg.find(matName);
    if (im == matImg.end()) return 0;
    // MA1: alias "<slot>@<pattern>": base table = the pattern's slot (as code 0 gave), imgmat table = the rule's slot
    std::string a = im->second + "@" + std::to_string(pattern);
    auto ex = slotByName.find(a);
    if (ex != slotByName.end()) return ex->second + 2;
    if ((int)slots.size() >= kMaxSlots) return 0;
    int ps = pattern >= 0 && pattern < 32 ? patSlot[pattern] : -1;
    Slot base = ps >= 0 ? slots[ps] : Slot{}; base.name = a;
    Slot img = slotsImg[slotByName[im->second]]; img.name = a;
    slots.push_back(base); slotsImg.push_back(img); slotByName[a] = (int)slots.size() - 1;
    resolveLayers(); uploadAll();
    return (int)slots.size() + 1;
  }
  return it->second < 0 ? 1 : it->second + 2;
}

bool active() { return gpuOn; }
void setImgMat(bool on) {
  if (on == wantImg) return;
  wantImg = on;
  if (!loadedBoth && loadedImg != on && !root.empty()) init(root);   // the other table's sets are not loaded: reload
  else { invalidateKey(); uploadAll(); }
}
bool imgMat() { return wantImg; }
bool enabled() { return gpuOn && wantOn; }
void setEnabled(bool on) { wantOn = on; invalidateKey(); for (GLuint p : progs) glProgramUniform1i(p, glGetUniformLocation(p, "uTexOn"), gpuOn && wantOn ? 1 : 0); }
const Info& info() { return gInfo; }
int slotIndex(const std::string& n) { auto it = slotByName.find(n); return it == slotByName.end() ? -1 : it->second; }
int slotLayer(int s) { const auto& t = wantImg ? slotsImg : slots; return s >= 0 && s < (int)t.size() ? t[s].layer : -1; }   // the active table (MA1)
int patternSlot(int p) { return p >= 0 && p < 32 ? (wantImg ? patSlotImg : patSlot)[p] : -1; }

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
  std::string k = std::string(enabled() ? "on|" : "off|") + (wantImg ? "img|" : "base|");
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
