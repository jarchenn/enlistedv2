// L4 texture layer (docs/textures.md): ambientCG CC0 sets, triplanar sampling hooks for kMainFS / kTerrainFS,
#include "crumb.h"
// the --pack-texture packer used by tools/fetch_ambientcg.py and placeholder sets for bring-up / tests.
#include "user_cache.h"
#include "textures.h"
#include <stb_image.h>
#include <stb_image_write.h>
#define STB_DXT_IMPLEMENTATION
#include <stb_dxt.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
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
constexpr int kSizeHi = 2048, kLevelsHi = 10;   // TX2K: sets packed at 2K or more (the ground sets) go to a second array, units 29-31
int levelsFor(int n) { return n == kSizeHi ? kLevelsHi : kLevels; }
#ifndef GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT 0x8C4F
#endif

struct Slot {
  std::string name, set;
  int mode = 0, space = 0, rot = 0;   // mode 0 detail, 1 replace; space 0 asset, 1 part, 2 world, 3 ground (planar xz)
  float scale = 1, kAlb = 0.8f, kN = 0.8f, kR = 0.5f, kAO = 0.5f, kH = 0, fadeA = 0, fadeB = 0;
  int layer = -1;
  std::string under; int underIdx = -1; float expo = 0.6f, contrast = 1.0f, macro = 0;   // MA1: substrate slot behind the macro exposure mask; macro variation strength
  float hex = 0;   // M1 hex tiling: rotation strength 0..1 of the ground taps (0 = plain tiling); packed into uTexC.y above the quarter turns
  float relD = 0, crev = 0, edge = 0, lumps = 0;   // MA1b relief (Perf relief): parallax depth (m), crevice darkening, edge wear, fill lumps (m)
  bool lazy = false;   // TX2: a library slot (textures.json "lazy"): its set loads only once a material names it
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
GLuint grungeTex = 0; float grungeK = 0;   // TX6: grunge masks (unit 28): R paint chips, G dirt, B cracks (content/textures/grunge_masks)
GLuint macroTex = 0;                   // MA1: world-space macro variation (unit 25): R tint, G roughness, B damp, A substrate exposure
std::map<std::string, int> slotByName;
std::map<std::string, int> matSlot;    // material name -> slot, -2 = none
std::map<std::string, std::string> matImg;   // MA1: material -> imgmat slot name, for materials without a base rule
int patSlot[32];
int terrSlot[8];
int paintSlot[kPaintTypes];
int paintSlotEff[kPaintTypes];   // NT4: after the open map's look.paintSets override
std::string gPaintKey = "";
std::vector<SetInfo> sets;             // loaded layers
std::vector<GLuint> progs;
GLuint arr[3] = {0, 0, 0}, arrHi[3] = {0, 0, 0};
int hiBase = 1 << 20;                  // TX2K: layers from here on live in arrHi (layer - hiBase)
bool gpuOn = false, wantOn = true;
Info gInfo;
fs::path root;
std::set<std::string> gLazyWant;   // TX2: lazy slots' sets that a material named (loaded by flushPending)
bool gPending = false;

int findSet(const std::string& n) { for (auto& s : sets) if (s.name == n) return s.layer; return -1; }

Slot parseSlot(const std::string& name, const json& j) {
  Slot s; s.name = name;
  s.set = j.value("set", "");
  std::string m = j.value("mode", "detail"); s.mode = m == "replace" ? 1 : 0;
  std::string sp = j.value("space", "asset"); s.space = sp == "part" ? 1 : sp == "world" ? 2 : sp == "ground" ? 3 : 0;
  s.scale = j.value("scale", 1.0f); s.rot = j.value("rot", 0) & 3;
  {   // M1: natural ground (soil, grass, gravel, sand, litter, rock) hex-tiles by default; laid or directional patterns (paving, setts, ploughed furrows, ruts,
      // cobbles, roads, planks, bricks, tiles, slabs) keep their grid; "hex": 0..1 in textures.json overrides either way
    std::string id = name + "|" + s.set; bool laid = false;
    for (const char* k : {"paving", "setts", "cobble", "tactile", "road", "brick", "plank", "tile", "concrete", "apron", "slab", "floor_board", "parquet", "plough", "furrow", "rut", "track", "crop", "row"}) laid |= id.find(k) != std::string::npos;
    bool loose = false;   // loose natural material on asset surfaces too (path beds, yards): the shader skips part space
    for (const char* k : {"gravel", "dirt", "mud", "sand", "soil", "shingle", "litter", "moss", "grass", "earth"}) loose |= id.find(k) != std::string::npos;
    s.hex = std::clamp(j.value("hex", (sp == "ground" || loose) && !laid ? 1.0f : 0.0f), 0.0f, 1.0f);
  }
  s.kAlb = j.value("albedo", s.kAlb); s.kN = j.value("normal", s.kN); s.kR = j.value("rough", s.kR);
  if (j.value("absolute", false)) s.kAlb = -std::max(s.kAlb, 0.01f);   // NT8: a painted slot drawn in its texture's own colour (sign = flag)
  s.kAO = j.value("ao", s.kAO);
  if (j.value("assetAbsolute", false)) s.kAO = -std::max(s.kAO, 0.01f);   // trails: an asset material on this slot draws the set's own colour (sign = flag)
  s.kH = j.value("height", s.kH);
  if (j.contains("fade") && j["fade"].is_array() && j["fade"].size() == 2) { s.fadeA = j["fade"][0]; s.fadeB = j["fade"][1]; }
  s.under = j.value("under", ""); s.expo = j.value("exposure", s.expo); s.macro = j.value("macro", 0.0f); s.contrast = j.value("contrast", 1.0f);
  s.lazy = j.value("lazy", false);
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
  std::copy(std::begin(paintSlot), std::end(paintSlot), std::begin(paintSlotEff)); gPaintKey = "";
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
struct Cpu { bool ok = false; int n = kSize; std::vector<uint8_t> lv[3][kLevelsHi]; SetInfo info; std::string err; };   // n: the array size it is built for

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

float gAniso = 8.0f;   // UL1: Renderer::q.texAniso (tex::setAnisotropy)
}  // namespace
void setAnisotropy(float a) {
  gAniso = std::clamp(a, 1.0f, 16.0f);
  for (GLuint t : {arr[0], arr[1], arr[2], arrHi[0], arrHi[1], arrHi[2], grungeTex}) if (t) glTextureParameterf(t, 0x84FE /* GL_TEXTURE_MAX_ANISOTROPY */, gAniso);
}
namespace {
float toksvigK() { static const float k = [] { const char* e = std::getenv("NMK_TEX_TOKSVIG"); return e ? (float)std::atof(e) : 1.0f; }(); return k; }
void buildSetRaw(const fs::path& dir, bool bc, Cpu& o) {
  initLut();
  Img col, nrm, rma;
  if (!loadImg(dir / "color.jpg", 3, col) && !loadImg(dir / "color.png", 3, col)) { o.err = "no color"; return; }
  if (!loadImg(dir / "normal.jpg", 3, nrm) && !loadImg(dir / "normal.png", 3, nrm)) { nrm.w = nrm.h = 1; nrm.c = 3; nrm.px = {128, 128, 255}; }
  if (!loadImg(dir / "rma.jpg", 3, rma) && !loadImg(dir / "rma.png", 3, rma)) { rma.w = rma.h = 1; rma.c = 3; rma.px = {200, 255, 128}; }
  const int sn = o.n, levels = levelsFor(sn);
  col = resample(col, sn); nrm = resample(nrm, sn); rma = resample(rma, sn);
  const size_t N = (size_t)sn * sn;
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
  // M3 Toksvig (stream M): the mean of the unit normals under each mip texel, unnormalised; its shortening is the
  // normal variance the mip filtered away, added to that texel's alpha^2 so bumps that blur out turn into roughness
  // instead of sparkle (Toksvig 2004). NMK_TEX_TOKSVIG = strength (0 off; part of the cache key)
  const float tk = toksvigK();
  std::vector<float> nv(tk > 0 ? N * 3 : 0);
  for (size_t i = 0; i < nv.size() / 3; ++i) {
    float x = b[i * 2] / 127.5f - 1, y = b[i * 2 + 1] / 127.5f - 1, z = std::sqrt(std::max(0.0f, 1 - x * x - y * y)), l = std::max(1e-4f, std::sqrt(x * x + y * y + z * z));
    nv[i * 3] = x / l; nv[i * 3 + 1] = y / l; nv[i * 3 + 2] = z / l;
  }
  int n = sn;
  for (int L = 0; L < levels; ++L) {
    encodeLevel(a, n, 4, bc, 0, o.lv[0][L]); encodeLevel(b, n, 2, bc, 1, o.lv[1][L]); encodeLevel(c, n, 2, bc, 2, o.lv[2][L]);
    if (L + 1 == levels) break;
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
      if (tk > 0) {
        float v[3];
        for (int k = 0; k < 3; ++k) { v[k] = 0.25f * (nv[i00 * 3 + k] + nv[i10 * 3 + k] + nv[i01 * 3 + k] + nv[i11 * 3 + k]); nv[d * 3 + k] = v[k]; }   // in place: d <= i00
        float r = std::clamp(std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]), 1e-3f, 1.0f), rr = c2[d * 2] / 255.0f;
        c2[d * 2] = (uint8_t)std::clamp(std::pow(std::min(1.0f, rr * rr * rr * rr + tk * (1 - r) / r), 0.25f) * 255.0f + 0.5f, 0.0f, 255.0f);
      }
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
// CACHE1: a built set (compressed levels and its means) cached per user next to the model maps (texbc, "set-*"): the
// decode, resample, mip and compression of 90+ sets took ~1.6 s every start. Keyed by the folder, its files' sizes / times
fs::path setCacheFile(const fs::path& dir, bool bc, int n) {
  std::string key = "set2|" + std::to_string(bc) + (toksvigK() > 0 ? "|tk" + std::to_string(toksvigK()) : std::string()) + "|" + std::to_string(n) + "|" + std::to_string(levelsFor(n));   // CACHE2: by content, not by folder or time
  std::error_code ec;
  for (const char* f : {"color.jpg", "color.png", "normal.jpg", "normal.png", "rma.jpg", "rma.png", "meta.json"}) {
    fs::path q = dir / f; if (!fs::exists(q, ec)) continue;
    key += std::string("|") + f + ":" + userCacheContentStamp(q);
  }
  uint64_t x = 1469598103934665603ull; for (unsigned char c : key) { x ^= c; x *= 1099511628211ull; }
  char nm[48]; std::snprintf(nm, sizeof nm, "set-%016llx.bin", (unsigned long long)x);
  return userCacheDir("texbc", "NMK_TEXBC_CACHE") / nm;
}
void buildSet(const fs::path& dir, bool bc, Cpu& o) {
  fs::path cf = setCacheFile(dir, bc, o.n);
  const int levels = levelsFor(o.n);
  {
    std::ifstream in(cf, std::ios::binary); uint32_t magic = 0;
    if (in && in.read((char*)&magic, 4) && magic == 0x31535458u) {   // "XTS1"
      bool good = true;
      for (int k = 0; k < 3 && good; ++k) for (int L = 0; L < levels && good; ++L) { uint32_t n = 0; good = (bool)in.read((char*)&n, 4) && n <= (64u << 20); if (good) { o.lv[k][L].resize(n); good = (bool)in.read((char*)o.lv[k][L].data(), n); } }
      float f[9]; if (good) good = (bool)in.read((char*)f, sizeof f);
      if (good) { o.info.meanLin = vec3(f[0], f[1], f[2]); o.info.meanAO = f[3]; o.info.meanR = f[4]; o.info.meanH = f[5]; o.info.size = vec2(f[6], f[7]); o.ok = true; userCacheTouch(cf); return; }
      for (auto& a : o.lv) for (auto& l : a) l.clear();
    }
  }
  buildSetRaw(dir, bc, o);
  if (!o.ok) return;
  std::error_code ec; fs::create_directories(cf.parent_path(), ec);
  fs::path tmp = cf; tmp += "." + std::to_string((unsigned long long)(uintptr_t)&o) + ".tmp";   // several builder threads / game instances
  { std::ofstream out(tmp, std::ios::binary); uint32_t magic = 0x31535458u; out.write((const char*)&magic, 4);
    for (auto& a : o.lv) for (int L = 0; L < levels; ++L) { uint32_t n = (uint32_t)a[L].size(); out.write((const char*)&n, 4); out.write((const char*)a[L].data(), n); }
    float f[9] = {o.info.meanLin.x, o.info.meanLin.y, o.info.meanLin.z, o.info.meanAO, o.info.meanR, o.info.meanH, o.info.size.x, o.info.size.y, 0}; out.write((const char*)f, sizeof f); }
  fs::rename(tmp, cf, ec);
}

bool hasExt(const char* e) {
  GLint n = 0; glGetIntegerv(GL_NUM_EXTENSIONS, &n);
  for (GLint i = 0; i < n; ++i) if (std::strcmp((const char*)glGetStringi(GL_EXTENSIONS, i), e) == 0) return true;
  return false;
}

void upload(GLuint p) {
  auto U = [&](const char* n) { return glGetUniformLocation(p, n); };
  glProgramUniform1i(p, U("uTexCol"), kUnitBase + 0); glProgramUniform1i(p, U("uTexNrm"), kUnitBase + 1); glProgramUniform1i(p, U("uTexRH"), kUnitBase + 2);
  glProgramUniform1i(p, U("uTexColH"), kUnitBase + 9); glProgramUniform1i(p, U("uTexNrmH"), kUnitBase + 10); glProgramUniform1i(p, U("uTexRHH"), kUnitBase + 11);   // TX2K: units 29-31
  glProgramUniform1i(p, U("uTexHiBase"), hiBase);
  { int l[4]; for (int k = 0; k < 4; ++k) { int si = slotIndex("mask_lib_" + std::to_string(k)); l[k] = si >= 0 ? slots[si].layer : -1; }
    glProgramUniform4i(p, U("uMaskLayer"), l[0], l[1], l[2], l[3]); }   // CP step 6 part 2
  { int ms = slotIndex("micro_detail"); glProgramUniform1i(p, U("uMicroLayer"), ms >= 0 ? slots[ms].layer : -1); }   // CP step 6
  glProgramUniform1i(p, U("uTexOn"), gpuOn && wantOn ? 1 : 0);
  glProgramUniform1i(p, U("uImgMat"), wantImg ? 1 : 0); glProgramUniform1i(p, U("uTexSkipFar"), wantImg ? 1 : 0);
  glProgramUniform1iv(p, U("uFloorSlot"), 8, floorSlotImg);
  glProgramUniform1i(p, U("uTexMacro"), kUnitBase + 5);   // unit 25 (23 ground paint, 24 terrain cache)
  glProgramUniform1i(p, U("uGrunge"), kUnitBase + 8); glProgramUniform1f(p, U("uGrungeK"), grungeK);   // TX6: unit 28 (26 leaves, 27 grass / fog)
  vec4 lod(3.0f, 2.0f, 16.0f, 0.95f), lod2(8.0f, 48.0f, 0.7f, 1.6f);   // lod2.w (MA1b): replace sets' gradient scale under relief   // tuned on --selftest-render (docs/textures.md#shimmer); NMK_TEX_LOD="a,b,c,d" overrides (A/B)
  if (const char* e = std::getenv("NMK_TEX_LOD")) std::sscanf(e, "%f,%f,%f,%f", &lod.x, &lod.y, &lod.z, &lod.w);
  if (const char* e = std::getenv("NMK_TEX_LOD2")) std::sscanf(e, "%f,%f,%f,%f", &lod2.x, &lod2.y, &lod2.z, &lod2.w);
  float lodG = 1.5f;   // TX2K: less bias on the ground (×3 blurred it at the eye); tuned on --selftest-render, NMK_TEX_LOD_GROUND overrides
  if (const char* e = std::getenv("NMK_TEX_LOD_GROUND")) lodG = (float)std::atof(e);
  glProgramUniform1f(p, U("uTexLodG"), lodG > 0 ? lodG : lod.x);
  vec2 nearR(3.0f, 10.0f);   // TX3: bias-free within 3 m, full bias from 10 m (NMK_TEX_NEAR="a,b")
  if (const char* e = std::getenv("NMK_TEX_NEAR")) std::sscanf(e, "%f,%f", &nearR.x, &nearR.y);
  glProgramUniform2f(p, U("uTexNear"), nearR.x, std::max(nearR.y, nearR.x + 0.1f));
  glProgramUniform4fv(p, U("uTexLod"), 1, &lod.x); glProgramUniform4fv(p, U("uTexLod2"), 1, &lod2.x);
  glProgramUniform3i(p, U("uSnowSlot"), slotIndex("snow_packed"), slotIndex("snow_trodden"), slotIndex("snow_thaw"));   // SN2: the snow sets for applySnow (terrTexSlot), -1 when missing
  glProgramUniform1iv(p, U("uPatSlot"), 32, wantImg ? patSlotImg : patSlot); glProgramUniform1iv(p, U("uTerrSlot"), 8, terrSlot); glProgramUniform1iv(p, U("uPaintSlot"), kPaintTypes, paintSlotEff);
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
    B[i] = vec4(s.kAlb, s.kN, s.kR, s.kAO); C[i] = vec4(s.kH, (float)s.rot + 4.0f * std::round(s.hex * 8.0f), s.fadeA, s.fadeB);   // M1: y = quarter turns + 4 x hex eighths
    Uu[i] = vec4((float)s.underIdx, s.expo, s.contrast, s.macro);
    R[i] = vec4(s.relD, s.crev, s.edge, s.lumps);
  }
  for (auto& s : sets) if (s.layer >= 0 && s.layer < kMaxLayers) { Ly[s.layer] = vec4(1.0f / s.size.x, 1.0f / s.size.y, s.meanR, s.meanH); M[s.layer] = vec4(s.meanLin, s.meanAO); }
  if (!tableUbo) {   // TX4: the block is near the 64 KB minimum every GL 4.x driver guarantees
    GLint mx = 0; glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &mx);
    if (mx < (GLint)(d.size() * sizeof(vec4))) logf("textures: GL_MAX_UNIFORM_BLOCK_SIZE %d < the %d-byte slot tables", mx, (int)(d.size() * sizeof(vec4)));
  }
  if (!tableUbo) { glCreateBuffers(1, &tableUbo); glNamedBufferStorage(tableUbo, (GLsizeiptr)(d.size() * sizeof(vec4)), nullptr, GL_DYNAMIC_STORAGE_BIT); }
  glNamedBufferSubData(tableUbo, 0, (GLsizeiptr)(d.size() * sizeof(vec4)), d.data());
  glBindBufferBase(GL_UNIFORM_BUFFER, 3, tableUbo);
}
void uploadAll() { uploadTables(); for (GLuint p : progs) upload(p); }

// MA1 macro variation: 256^2 RGBA8, tileable value-noise fBm (one tile = 32 m in the shader), mipmapped, unit 25.
// R broad tint, G roughness, B damp / weathering (broadest), A substrate exposure (patches of ~2-3 m, ragged edges)
// TX6: the wall wear masks, one tileable RGBA image (R paint chips: 1 = paint gone, G dirt, B cracks), 2.5 m a tile.
// local.png (built from the user's Texturelabs grunge images; git-ignored, never committed) wins over cc0.png (built from
// ambientCG CC0 sets, committed). Neither: uGrungeK = 0 and the walls break by the procedural macro mask as before.
void loadGrunge() {
  if (grungeTex) { glDeleteTextures(1, &grungeTex); grungeTex = 0; }
  grungeK = 0;
  for (const char* f : {"local.png", "cc0.png"}) {
    fs::path p = root / "grunge_masks" / f; std::error_code ec;
    if (!fs::exists(p, ec)) continue;
    int w = 0, h = 0, c = 0;
    unsigned char* px = stbi_load(p.string().c_str(), &w, &h, &c, 4);
    if (!px) { logf("textures: grunge mask %s unreadable", p.string().c_str()); continue; }
    int lv = 1; while ((std::max(w, h) >> lv) > 0) ++lv;
    glCreateTextures(GL_TEXTURE_2D, 1, &grungeTex);
    glTextureStorage2D(grungeTex, lv, GL_RGBA8, w, h);
    glTextureSubImage2D(grungeTex, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    stbi_image_free(px);
    glGenerateTextureMipmap(grungeTex);
    glTextureParameteri(grungeTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); glTextureParameteri(grungeTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(grungeTex, GL_TEXTURE_WRAP_S, GL_REPEAT); glTextureParameteri(grungeTex, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTextureParameterf(grungeTex, 0x84FE /* GL_TEXTURE_MAX_ANISOTROPY */, gAniso);
    grungeK = std::getenv("NMK_NO_GRUNGE") ? 0.0f : 1.0f;
    gInfo.bytes += (double)w * h * 4 * 4 / 3;
    logf("textures: grunge masks %s (%dx%d)", f, w, h);
    break;
  }
  if (!grungeTex) {   // a neutral 1x1 so the sampler is always complete
    const unsigned char z[4] = {0, 0, 0, 0};
    glCreateTextures(GL_TEXTURE_2D, 1, &grungeTex); glTextureStorage2D(grungeTex, 1, GL_RGBA8, 1, 1); glTextureSubImage2D(grungeTex, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, z);
  }
  glBindTextureUnit(kUnitBase + 8, grungeTex);
}

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
    else if (!s.set.empty() && !s.lazy && std::find(gInfo.missing.begin(), gInfo.missing.end(), s.set) == gInfo.missing.end()) gInfo.missing.push_back(s.set);
  }
  gInfo.slots = (int)slots.size();
  { static bool told = false; int h = 0; for (auto& sl : slots) h += sl.hex > 0; if (!told) logf("textures: %d slots hex-tiled (M1)", h); told = true; }
}

const char* kCommonGLSL = R"GLSL(
// ---- L4 texture layer (src/textures.cpp, docs/textures.md): ambientCG sets in three 2D arrays (units 20-22)
uniform sampler2DArray uTexCol, uTexNrm, uTexRH;   // colour (sRGB) + ao, normal xy, roughness + height
uniform sampler2DArray uTexColH, uTexNrmH, uTexRHH;   // TX2K: the same for the 2K sets (units 29-31), layers uTexHiBase and up
uniform int uTexHiBase;
bool texHi(float L){ return L > float(uTexHiBase) - 0.5; }
vec4 texGCol(vec3 q, vec2 gx, vec2 gy){ return texHi(q.z) ? textureGrad(uTexColH, vec3(q.xy, q.z - float(uTexHiBase)), gx, gy) : textureGrad(uTexCol, q, gx, gy); }
vec4 texGNrm(vec3 q, vec2 gx, vec2 gy){ return texHi(q.z) ? textureGrad(uTexNrmH, vec3(q.xy, q.z - float(uTexHiBase)), gx, gy) : textureGrad(uTexNrm, q, gx, gy); }
vec4 texGRH(vec3 q, vec2 gx, vec2 gy){ return texHi(q.z) ? textureGrad(uTexRHH, vec3(q.xy, q.z - float(uTexHiBase)), gx, gy) : textureGrad(uTexRH, q, gx, gy); }
uniform int uTexOn;
uniform int uTexSkipFar;
uniform int uImgMat;       // MA1 imgmat table active (render.cpp: room floor finishes take uFloorSlot; terrain layers alternate sets)
uniform sampler2D uGrunge; uniform float uGrungeK;   // TX6: wall wear masks (unit 28): R chips, G dirt, B cracks; 4 m a tile
uniform sampler2D uTexMacro;   // MA1 macro variation (unit 25): R tint, G roughness, B damp, A substrate / alternate-set mask; one tile = 32 m
float gTexNFade = 1.0;     // MA1: share of the far normal fade (texSlotTaps: 0.5 for replace sets under imgmat)   // MA1 imgmat: 1 = skip normal taps whose detail is faded out (far)
// MA1: the slot / layer tables live in one uniform buffer (binding 3, 14 KB): as plain uniforms they passed the 1024
// constant registers of a fragment program once the tables grew (160 slots, 128 layers; TX2: 320 / 192, 32 KB; TX3: 448 / 256, 44 KB; TX4: 704 / 256, 63 KB)
layout(std140, binding = 3) uniform NmkTexTables {
  vec4 uTexA[704];   // per slot: layer (-1 none), mode (0 detail, 1 replace), space (0 asset, 1 part, 2 world, 3 ground), scale
  vec4 uTexB[704];   // per slot: albedo, normal, roughness, ao strength
  vec4 uTexC[704];   // per slot: height (m), quarter turns, distance fade start, end (m; end 0 = no fade)
  vec4 uTexU[704];   // MA1 per slot: substrate ("under") slot (-1 none), exposure threshold, contrast (replace sets), macro variation strength
  vec4 uTexL[256];   // per layer: 1/size u, 1/size v (1/m), mean roughness, mean height
  vec4 uTexM[256];   // per layer: mean linear colour, mean ao
  vec4 uTexR[704];   // MA1b per slot: relief depth (m), crevice darkening, edge wear, fill lumps (m) (Perf relief)
};
uniform int uTerrSlot[8]; // terrain layers: grass, soil, mud, gravel, sand, rock, scree, crater
#define NMK_TEX_PAINT 1   // painted ground: uPaintSlot[k] for marsh, dirt, mud, gravel, sand, cobbles, rock, dry_grass, ploughed, leaf_litter, scorched, pebbles
uniform int uPaintSlot[12];
uniform int uOpt2Off;   // OPT2 A/B (NMK_OPT2=0): hex on every layer at any distance, the old far-normal rule, no snow tap gate
uniform ivec3 uSnowSlot;   // SN2: slots snow_packed, snow_trodden, snow_thaw (-1 missing): terrTexSlot(uSnowSlot.x, p, n, dpx, dpy, dist)
uniform int uTexBiasOn;   // CP step 1 (perf switch texbias): 1 = gradient scale 1.0 everywhere (corpus 07 s4.1) + uTexBias; 0 = the old 3.0 / 1.5 ground / 1.6 relief
uniform float uTexBias;   // log2(render / display), -0.5 more under TAA (texbias on only)
uniform int uMicroLayer;   // CP step 6: the micro-detail set's array layer (slot micro_detail), -1 none
uniform ivec4 uMaskLayer;  // CP step 6 part 2: the mask library's array layers (mask_lib_0..3), -1 missing
uniform int uMaskLib;      // perf switch masklib
uniform int uAbsFar;      // FIX8 (perf switch absfar)
uniform int uSteepTri;     // FIX5 (perf switch steeptri): ground-space slots triplanar on steep normals
uniform int uWearCause;    // M4 (perf switch wearcause): wear placed by its causes (splash, runoff), roughness first
uniform ivec2 uMicroOn;    // x: micro detail (perf switch microdetail), y: macro roughness variation (perf switch roughvar)
uniform float uTexLodG;   // TX2K: the ground's gradient scale (terrain layers, painted ground; uTexLod.x for everything else)
uniform vec4 uTexLod;     // gradient scale (LOD bias against TAA-jitter shimmer), normal fade start / end (texels per pixel), fade depth
uniform vec4 uTexLod2;    // albedo / roughness detail fade start / end (texels per pixel), fade depth
struct TexS { vec3 col; float ao, r, h; vec3 n; float d; };   // d: MA1b detail height (0-centred) near the eye
// GX5 parallax occlusion (High): the eye direction (world, toward the eye) and the height in uv units; 0 = off. Set by
// texMaterial for slots with a height, near the eye; the taps march the height map before sampling (docs/textures.md)
vec3 gPomV = vec3(0.0); float gPomK = 0.0;
int gPomN = 10;          // MA1b: parallax steps (High 10, Medium 4 under Perf relief)
uniform vec2 uHexFade, uHBFade, uPomQ;   // UL1 (Renderer::q): hex / height-blend fade distances (y 0 = none), parallax range / steps (0 = by tier)
uniform vec2 uTexNear;    // TX3: the LOD bias fades in between these eye distances (m): near the eye the surface is magnified, so
float gEyeD = 1e9;        //   there is no shimmer to bias against and the 2K sets show their detail (gEyeD: set by each caller)
float gTexGS = 0.0;      // MA1b: gradient scale for this slot's taps (0 = uTexLod.x); replace sets under relief: uTexLod2.w
float gDetF = 0.0;       // MA1b: detail height tap near the eye (0 = off)
bool texOn(){ return uTexOn == 1 && uLook == 1; }
void texFrame(int q, inout vec3 T, inout vec3 B){ vec3 t = T; if (q == 1) { T = B; B = -t; } else if (q == 2) { T = -T; B = -B; } else if (q == 3) { T = -B; B = t; } }
// one planar tap: frame T (u), B (v), Nf (out of the plane); explicit gradients (taps sit in divergent branches);
// the tangent-space normal is whiteout-blended with the surface normal ns
// M1 hex tiling (Mikkelsen, "Practical Real-Time Hex-Tiling", JCGT 2022): the plane is cut into a triangle grid; each
// vertex owns a copy of the texture turned (by gHexK of a full turn) and shifted at random, and a pixel blends the three
// copies of its triangle, sharpened by the set's own height so the seams follow stones and clods instead of a smooth
// cross-fade. Three taps per map instead of one, ground and cliff slots only (terrTexOne sets gHexK); uHex.x 0 = off
// (switch "hex", Low), uHex.y = cells per texture repeat.
uniform vec2 uHex;
float gHexK = 0.0;
float gLayerW = 1.0;   // OPT2: the calling layer's share of the pixel (kTerrainFS / paintApply set it); hex only at >= uHexMinW
vec2 hexHash(vec2 v){ return fract(sin(mod(vec2(dot(v, vec2(127.1, 311.7)), dot(v, vec2(269.5, 183.3))), 3.14159))*43758.5453); }
void texHexFetch(vec3 q, vec2 gx, vec2 gy, out vec4 c, out vec2 nx, out vec2 rh, float nfk, bool nrm, float hf){   // hf: UL1 fade (0 = three equal copies = plain tiling)
  vec2 sk = mat2(1.0, 0.0, -0.57735027, 1.15470054)*(q.xy*uHex.y);
  vec2 b = floor(sk); vec3 t = vec3(fract(sk), 0.0); t.z = 1.0 - t.x - t.y;
  float s = step(0.0, -t.z), s2 = 2.0*s - 1.0;
  vec3 bw = vec3(-t.z*s2, s - t.y*s2, s - t.x*s2);
  vec2 v[3] = vec2[3](b + vec2(s, s), b + vec2(s, 1.0 - s), b + vec2(1.0 - s, s));
  c = vec4(0.0); nx = vec2(0.0); rh = vec2(0.0); float ws = 0.0, w2 = 0.0;
  for (int i = 0; i < 3; ++i) {
    if (bw[i] < 0.02) continue;
    float ang = (mod(abs(v[i].x*v[i].y) + abs(v[i].x + v[i].y), 6.2831853) - 3.14159265)*gHexK*hf;
    float cs = cos(ang), sn = sin(ang); mat2 R = mat2(cs, sn, -sn, cs);
    vec2 cen = mat2(1.0, 0.0, 0.5, 0.8660254)*v[i]/uHex.y;
    vec3 qi = vec3(R*(q.xy - cen) + cen + hexHash(v[i])*hf, q.z); vec2 gxi = R*gx, gyi = R*gy;
    vec2 h = texGRH(qi, gxi, gyi).rg;
    float W = pow(bw[i], 5.0)*exp2(10.0*h.y);   // barycentric weight, sharpened, then the higher surface wins
    c += texGCol(qi, gxi, gyi)*W; rh += h*W; ws += W; w2 += W*W;
    if (nrm) nx += transpose(R)*(texGNrm(qi, gxi, gyi).rg*2.0 - 1.0)*nfk*W;   // the turned copy's normal back into the surface frame
  }
  c /= ws; rh /= ws; nx /= ws;
  // variance-preserving blend (Mikkelsen 2022, after Heitz & Neyret): a mix of copies loses contrast by sqrt(sum w^2);
  // push each channel back out from the set's mean by its inverse (capped), so the blended ground is as crisp as one tap
  float g = mix(1.0, min(inversesqrt(max(w2/(ws*ws), 1e-4)), 1.7), hf);
  int li = int(q.z + 0.5); vec4 m = uTexM[li]; vec2 mr = uTexL[li].zw;
  c = clamp(m + (c - m)*g, 0.0, 1.0); rh = clamp(mr + (rh - mr)*g, 0.0, 1.0); nx *= g;
}
void texTap(float L, vec3 p, vec3 dpx, vec3 dpy, vec2 inv, vec3 T, vec3 B, vec3 Nf, vec3 ns, float w, inout TexS s){
  vec3 q = vec3(dot(p, T)*inv.x, dot(p, B)*inv.y, L);
  float gs = uTexBiasOn == 1 ? exp2(uTexBias) : (gTexGS > 0.0 ? gTexGS : uTexLod.x);
  gs = mix(min(gs, 1.0), gs, smoothstep(uTexNear.x, uTexNear.y, gEyeD));   // TX3: no bias up close
  vec2 gx = vec2(dot(dpx, T), dot(dpx, B))*inv*gs, gy = vec2(dot(dpy, T), dot(dpy, B))*inv*gs;
  if (gPomK > 0.0 && w > 0.3) {   // GX5 POM: march from the top of the relief down until the height map is above the ray
    vec3 Vt = vec3(dot(gPomV, T), dot(gPomV, B), max(dot(gPomV, Nf), 0.2));
    if (gPomN == 1) q.xy -= Vt.xy*gPomK*(1.0 - texGRH(q, gx, gy).g);   // MA1b Medium: single offset, offset limiting (one tap; a 4-step march cost +1.6 ms in chateau_town)
    else {
      float n = float(gPomN);
      vec2 step = Vt.xy/Vt.z*gPomK/n;
      float depth = 0.0, hd = 1.0 - texGRH(q, gx, gy).g, pd = 0.0, ph = hd; vec2 pq = q.xy;
      for (int i = 0; i < 10 && i < gPomN && depth < hd; ++i) { pq = q.xy; pd = depth; ph = hd; q.xy -= step; depth += 1.0/n; hd = 1.0 - texGRH(q, gx, gy).g; }
      float a = depth - hd, b = ph - pd;   // MA1b: refine linearly between the last two steps (no stair lines)
      if (a >= 0.0 && a + b > 1e-4) q.xy = mix(q.xy, pq, a/(a + b));
    }
  }
  if (gDetF > 0.0 && w > 0.3) s.d += (texGRH(vec3(q.xy*3.7 + vec2(0.37, 0.61), q.z), gx*3.7, gy*3.7).g - 0.5)*w;   // MA1b: the set's own grain / pores at 3.7x
  // normal detail near the pixel frequency flickers under the TAA jitter: fade it with the footprint (texels per pixel)
  float fp = max(length(gx), length(gy))*(texHi(L) ? 2048.0 : 1024.0)/max(gs, 1e-3);
  if (w > 0.3) { float dn = (texHi(L) ? 2048.0 : 1024.0)*max(inv.x, inv.y); if (dn > gTexDensity) { gTexDensity = dn; gTexChk = mod(floor(q.x) + floor(q.y), 2.0); } }   // CP step 2 texel view
  float nfk = 1.0 - uTexLod.w*gTexNFade*smoothstep(uTexLod.y, uTexLod.z, fp);
  bool nrm = uOpt2Off == 1 ? !(uTexSkipFar == 1 && nfk < 0.06) : nfk >= 0.06;   // far: the normal has faded out, so no normal tap (OPT2: every table; was the imgmat table only)
  vec4 c; vec2 nx, rh;
  float hf = uHexFade.y > 0.0 ? 1.0 - smoothstep(uHexFade.x, uHexFade.y, gEyeD) : 1.0;   // UL1: q.hexFade
  if (gHexK > 0.0 && hf > 0.0 && w > 0.04) texHexFetch(q, gx, gy, c, nx, rh, nfk, nrm, hf);
  else {
    c = texGCol(q, gx, gy);
    nx = nrm ? (texGNrm(q, gx, gy).rg*2.0 - 1.0)*nfk : vec2(0.0);
    rh = texGRH(q, gx, gy).rg;
  }
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
struct TerrT { vec3 alb; float r, ao; vec3 dn; float h; };   // h (M2): the set's height against its mean, 0.5 = level / untextured
// M2 height-based blending (after Mishkinis 2013): k = the weight of layer b over layer a; mid-transition the higher
// surface wins (stones stand out of mud, mud and sand settle between them). Untouched at k = 0 / 1 and between layers
// of equal height (untextured: 0.5), so the procedural edges and every map's look stay; uHBlend 0 = off (switch "hblend")
uniform float uHBlend;
uniform int uLayerBlend;   // BF 3.3 (perf switch layerblend): the merged N-layer height blend; off = M2's two-way bias below
// BF rev 3 spec 3.3 (mirrored in C++ by tex::layerBlend for the selftest): n <= 4 layers, q = authored weights >= 0,
// h = sampled heights 0..1 (0.5 = level). Guaranteed fallback (all-zero q -> the base layer), the qi factor (an inactive
// layer at maximum height never shows), weights sum to 1. beta 0.24, width 0.22 [A]; beta scales with the eye fade.
void layerBlend(float q[4], float h[4], int n, int base, float beta, float width, out float w[4]){
  float Q = 0.0; for (int i = 0; i < 4; ++i) { w[i] = 0.0; if (i < n) Q += max(q[i], 0.0); }
  if (Q <= 1e-5) { w[base] = 1.0; return; }
  float s[4]; float sMax = -1e9; int am = base; float qm = -1.0;
  for (int i = 0; i < n; ++i) { float qi = max(q[i], 0.0)/Q; s[i] = qi > 1e-5 ? qi + beta*(h[i] - 0.5) : -1e9; sMax = max(sMax, s[i]); if (qi > qm) { qm = qi; am = i; } }
  float G = 0.0;
  for (int i = 0; i < n; ++i) { float qi = max(q[i], 0.0)/Q; w[i] = qi > 1e-5 ? qi*max(0.0, s[i] - sMax + width) : 0.0; G += w[i]; }
  if (G > 1e-5) { for (int i = 0; i < n; ++i) w[i] /= G; } else { for (int i = 0; i < n; ++i) w[i] = i == am ? 1.0 : 0.0; }
}
// variance restoration (spec 3.3): blended value about the blended mean, scaled by 1/sqrt(sum w^2)
vec3 vRestore(vec3 c, vec3 cMean, float sumW2){ return cMean + (c - cMean)*inversesqrt(max(sumW2, 0.25)); }
// RNM (spec 3.3, Barre-Brisebois & Hill): tangent-space detail Nd onto base Nb; a neutral detail (0,0,1) returns Nb
vec3 rnm(vec3 Nb, vec3 Nd){ vec3 T = Nb + vec3(0.0, 0.0, 1.0), U = Nd*vec3(-1.0, -1.0, 1.0); return normalize(T*dot(T, U)/max(T.z, 1e-4) - U); }
float hbK(float k, float ha, float hb){ float s = uHBlend*(uHBFade.y > 0.0 ? 1.0 - smoothstep(uHBFade.x, uHBFade.y, gEyeD) : 1.0);
  if (uLayerBlend == 1) {   // BF 3.3: b over a by the merged blend (k = b's authored weight); s fades the height term with the eye
    if (s <= 0.0) return k;
    float q[4] = float[4](1.0 - k, k, 0.0, 0.0), h[4] = float[4](ha, hb, 0.5, 0.5), w[4];
    layerBlend(q, h, 2, 0, 0.24*s/max(uHBlend, 1e-3), 0.22, w); return w[1];   // beta 0.24 x the eye fade only (M2's strength is not applied twice)
  }
  return s > 0.0 ? clamp(k + (hb - ha)*s*4.0*k*(1.0 - k), 0.0, 1.0) : k; }
// any slot (terrain layers, painted ground: terrTexSlot(uPaintSlot[k], ...)); dpx / dpy = dFdx / dFdy of p taken in
// uniform control flow (kTerrainFS: tdx, tdy), dist = eye distance (tDist)
TerrT terrTexOne(int slot, vec3 p, vec3 n, vec3 dpx, vec3 dpy, float dist){
  TerrT o = TerrT(vec3(1.0), 1.0, 1.0, vec3(0.0), 0.5);
  if (!texOn() || slot < 0 || slot >= 704) return o;
  vec4 A = uTexA[slot]; if (A.x < 0.0) return o;
  int li = int(A.x + 0.5); vec4 K = uTexB[slot], C = uTexC[slot], Lp = uTexL[li], M = uTexM[li];
  float f = C.w > 0.0 ? 1.0 - smoothstep(C.z, C.w, dist) : 1.0;
  if (f <= 0.0) { if (uAbsFar == 1 && K.x < 0.0) o.alb = M.rgb*(-K.x); return o; }   // FIX8 absfar: past the fade an "absolute" set is its mean colour, not a white x1 modulation (paint_dirt / trail_dirt read white beyond 160-180 m)
  gEyeD = dist;
  vec2 inv = Lp.xy*A.w; int rot = int(mod(C.y + 0.5, 4.0));
  gHexK = uHex.x > 0.0 && (uOpt2Off == 1 || gLayerW >= 0.35) ? floor(C.y/4.0 + 0.01)/8.0 : 0.0;   // M1: this slot's hex tiling (ground and cliff taps); OPT2: a layer under 35 % of the pixel takes one plain tap
  TexS s = TexS(vec3(0.0), 0.0, 0.0, 0.0, vec3(0.0), 0.0);
  float gs0 = gTexGS; gTexGS = uTexLodG;
  if (int(A.z + 0.5) == 3 && (uSteepTri == 0 || n.y > 0.8)) {   // ground space: planar xz; FIX5 steeptri: steeper than ~37 deg the slot goes triplanar (no stretch on 45-52 deg banks; texTri is still mostly the top plane at the switch)
    vec3 T = vec3(1.0, 0.0, 0.0), B = vec3(0.0, 0.0, -1.0); texFrame(rot, T, B);
    texTap(A.x, p, dpx, dpy, inv, T, B, vec3(0.0, 1.0, 0.0), n, 1.0, s); s.n = normalize(s.n);
  } else s = texTri(A.x, p, n, dpx, dpy, inv, rot);
  gTexGS = gs0; gHexK = 0.0;
  o.alb = K.x < 0.0 ? mix(M.rgb, s.col, f)*(-K.x)   // NT8 "absolute": the texture's own colour (paintApply uses it as the albedo)
                    : mix(vec3(1.0), clamp(s.col/max(M.rgb, vec3(0.004)), 0.0, 2.5), K.x*f);
  o.r = mix(1.0, s.r/max(Lp.z, 0.02), K.z*f);
  o.ao = mix(1.0, s.ao, abs(K.w)*f);
  o.dn = (s.n - n)*K.y*f;
  o.h = mix(0.5, clamp(s.h - Lp.w + 0.5, 0.0, 1.0), f);   // M2
  if (uMicroOn.x == 1 && uMicroLayer >= 0 && dist < 7.0) {   // CP step 6: micro detail on the ground too (planar xz, 0.2 m)
    float mf = (1.0 - smoothstep(2.5, 7.0, dist))*f;
    vec3 mq = vec3(p.xz/0.2, float(uMicroLayer));
    vec2 mn = (texGNrm(mq, dpx.xz/0.2, dpy.xz/0.2).rg*2.0 - 1.0)*0.3*mf; float mrh = texGRH(mq, dpx.xz/0.2, dpy.xz/0.2).r;
    o.dn += vec3(mn.x, 0.0, mn.y); o.r *= 1.0 + (mrh - uTexL[uMicroLayer].z)*0.5*mf;
  }
  vec4 Uu = uTexU[slot];
  if (uImgMat == 1 && K.x >= 0.0 && Uu.z != 1.0) {   // GR2: per-set contrast on the ground too (contrast < 1: softer seams between paving stones)
    float l = max(dot(o.alb, vec3(0.2126, 0.7152, 0.0722)), 1e-3); o.alb *= pow(l, Uu.z - 1.0);
  }
  if (uImgMat == 1 && Uu.w > 0.0) {   // GR2: patches of tone and roughness a few metres across (variation that reads at a sprint),
    // and darker, glossier damp lows (the macro damp channel): one macro tap, no derivatives
    vec3 a = abs(n); vec2 uv = a.y > max(a.x, a.z) ? p.xz : a.x > a.z ? p.zy : p.xy;
    vec4 mac = textureLod(uTexMacro, uv/32.0, max(0.0, log2(max(max(length(dpx), length(dpy)), 1e-4)*256.0/32.0)));
    o.alb *= mix(vec3(1.0), vec3(0.82, 0.81, 0.79) + vec3(0.34, 0.35, 0.36)*mac.r, Uu.w*f);
    o.r *= 1.0 + (mac.g - 0.5)*0.45*Uu.w*f;
    float wetP = smoothstep(0.58, 0.76, mac.b)*Uu.w*f;
    o.alb *= 1.0 - 0.2*wetP; o.r *= 1.0 - 0.55*wetP;
  }
  return o;
}
// MA1 imgmat: a slot with an "under" set alternates with it by the world macro mask (A), patches of a few metres, so
// two ground sets break each other's tiling; both are sampled only in the narrow transition band
TerrT terrTexSlot(int slot, vec3 p, vec3 n, vec3 dpx, vec3 dpy, float dist){
  if (uImgMat == 1 && slot >= 0 && slot < 704 && uTexU[slot].x >= 0.0) {
    vec3 a = abs(n); bool fl = a.y > max(a.x, a.z), xw = a.x > a.z;
    vec2 uv = fl ? p.xz : xw ? p.zy : p.xy;
    float lod = log2(max(max(length(dpx), length(dpy)), 1e-4)*256.0/32.0);
    float m = smoothstep(uTexU[slot].y, uTexU[slot].y + 0.12, textureLod(uTexMacro, uv/32.0 + 0.37, max(lod, 0.0)).a);
    int u = int(uTexU[slot].x + 0.5);
    if (m <= 0.001) return terrTexOne(slot, p, n, dpx, dpy, dist);
    if (m >= 0.999) return terrTexOne(u, p, n, dpx, dpy, dist);
    TerrT x = terrTexOne(slot, p, n, dpx, dpy, dist), y = terrTexOne(u, p, n, dpx, dpy, dist);
    return TerrT(mix(x.alb, y.alb, m), mix(x.r, y.r, m), mix(x.ao, y.ao, m), mix(x.dn, y.dn, m), mix(x.h, y.h, m));
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
float gTexSeed = 0.0;      // TX6: the copy's seed (uInst.x): its grunge mask offset
float gTexWear = 1.0;      // the copy's wear (kMainFS sets uInst.y before texMaterial): more substrate on worn buildings
int texSlot(float code, int pat){ int c = int(code + 0.5)/100; return c == 0 ? (pat >= 0 && pat < 32 ? uPatSlot[pat] : -1) : c - 2; }
bool texReplace(int slot){ return texOn() && slot >= 0 && slot < 704 && uTexA[slot].x >= 0.0 && uTexA[slot].y > 0.5; }
// sample the slot's set and layer it on the material: albedo and roughness multiplied by the sample against the set's
// means (the material colour, per-stone tint and room finishes stay), ao, the normal (applied after the bump) and the
// height (into the derivative bump, faded by the detail filter like the procedural relief)
// one slot's taps in its own mapping space: colour / ao / roughness / height, and the world normal delta dn
TexS texSlotTaps(int slot, vec3 w, vec3 ng, vec3 ap, vec3 an, vec3 pc, vec3 pn, int grain,
                 vec3 dwx, vec3 dwy, vec3 dax, vec3 day, vec3 dcx, vec3 dcy, out vec3 dn){
  vec4 A = uTexA[slot], C = uTexC[slot];
  vec2 inv = uTexL[int(A.x + 0.5)].xy*A.w; int sp = int(A.z + 0.5), rot = int(mod(C.y + 0.5, 4.0));
  TexS s = TexS(vec3(0.0), 0.0, 0.0, 0.0, vec3(0.0), 0.0); dn = vec3(0.0);
  gTexNFade = (uImgMat == 1 && A.y > 0.5) ? 0.5 : 1.0;
  gTexGS = uRelief == 1 && A.y > 0.5 ? uTexLod2.w : 0.0;   // MA1b: the image is the structure; less LOD bias keeps courses and tiles at 15-35 m
  gHexK = uHex.x > 0.0 && sp != 1 ? floor(C.y/4.0 + 0.01)/8.0 : 0.0;   // M1: ground slots on assets too (path and yard surfaces)
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
  gHexK = 0.0;
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
  if (!texOn() || slot < 0 || slot >= 704) return;
  vec4 A = uTexA[slot]; if (A.x < 0.0) return;
  int li = int(A.x + 0.5); vec4 K = uTexB[slot], C = uTexC[slot], Lp = uTexL[li], M = uTexM[li];
  float f = C.w > 0.0 ? 1.0 - smoothstep(C.z, C.w, length(w - uCamPos)) : 1.0;
  if (f <= 0.0) return;
  vec2 inv = Lp.xy*A.w; int sp = int(A.z + 0.5);
  vec3 ng = normalize(nW);
  float eyeD = length(w - uCamPos);
  gEyeD = eyeD;
  gPomK = 0.0;
  vec4 R = uTexR[slot];
  bool rel = uRelief == 1 && R.x + R.y + R.z > 0.0;   // MA1b: this slot's family has relief (textures.json "relief" / "crevice" / "edge")
  float pomD = rel ? R.x : max(C.x, A.y > 0.5 ? 0.015 : 0.0);   // GX5: replace-mode sets (their own structure: brick, slates, cobbles) get 1.5 cm
  float pomR = uPomQ.x > 0.0 ? uPomQ.x : uMatLayer == 2 ? 14.0 : 8.0;   // MA1b: Medium one offset within 8 m, High a 10-step march within 14 m
  gPomN = uPomQ.y > 0.5 ? int(uPomQ.y + 0.5) : uMatLayer == 2 ? 10 : 1;
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
  vec3 a2 = K.w < 0.0 ? mix(alb, s.col*abs(K.x), f)   // trails ("assetAbsolute": path_dirt / path_gravel): the set's own colour replaces the path material's
                     : alb*mix(vec3(1.0), ratio, abs(K.x)*fa);   // TX4: an "absolute" (ground) slot named by an asset material: its strength, not a negative mix
  float r2 = rough*mix(1.0, s.r/max(Lp.z, 0.02), K.z*fa), ao2 = mix(1.0, s.ao, abs(K.w)*f);
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
    float wa = mac.a, wk = clamp(gTexWear, 0.0, 2.0);
    if (uGrungeK > 0.0) {   // TX6: the paint breaks along a grunge mask's flakes and cracks (one tap), more at box edges (corners,
      // openings), dirt low down and in the mask's stains; the macro mask keeps whole walls apart
      vec3 ga = abs(ng); bool gf = ga.y > max(ga.x, ga.z), gx = ga.x > ga.z;
      vec2 guv = (gf ? w.xz : gx ? w.zy : w.xy)/4.0, gdx = (gf ? dwx.xz : gx ? dwx.zy : dwx.xy)/4.0, gdy = (gf ? dwy.xz : gx ? dwy.zy : dwy.xy)/4.0;
      vec4 gm = textureGrad(uGrunge, guv + vec2(fract(gTexSeed*7.13), fract(gTexSeed*3.71)), gdx, gdy);
      if (uMaskLib == 1 && uMaskLayer.x >= 0) {   // CP step 6 part 2 (corpus 08 s10.1): this building's own stain mask from the library (layer and channel by its seed)
        int k = int(fract(gTexSeed*13.71)*3.999); int ml = k == 0 ? uMaskLayer.x : k == 1 ? uMaskLayer.y : k == 2 ? uMaskLayer.z : uMaskLayer.w;
        vec3 lm = texGCol(vec3(guv*0.5 + vec2(fract(gTexSeed*2.3), fract(gTexSeed*5.9)), float(ml)), gdx*0.5, gdy*0.5).rgb;
        int c = int(fract(gTexSeed*7.31)*2.999);
        gm.g = mix(gm.g, c == 0 ? lm.r : c == 1 ? lm.g : lm.b, 0.6);
      }
      float edgeW = 0.0;
      if (dot(pn, pn) > 2.0) { vec3 g3 = abs(vPcF) - abs(pc); vec3 gp = abs(pn); int gn = gp.x >= gp.y && gp.x >= gp.z ? 0 : gp.y >= gp.z ? 1 : 2; g3[gn] = 1e3;
        edgeW = 1.0 - smoothstep(0.02, 0.4, min(min(g3.x, g3.y), g3.z)); }
      wa = mac.a + ((gm.r - 0.5)*0.32 + edgeW*gm.r*0.3)*uGrungeK;   // the macro mask keeps the big peeled patches; the grunge mask breaks their edges into flakes and cracks
      float low = 1.0 - smoothstep(0.25, 1.8 + 0.6*mac.r, ap.y);
      float dirt = clamp(gm.g*(0.18 + 0.55*low) + gm.b*0.35, 0.0, 1.0)*wk*0.5*uGrungeK*f;
      a2 *= 1.0 - min(0.6*dirt, uMicroOn.y == 1 ? 0.35 : 1.0); r2 = mix(r2, 0.95, dirt);   // CP step 6: grime moves luminance by 35 % at most (corpus 08 s12: 20-40 %)
    }
    float m = Uu.x >= 0.0 ? smoothstep(Uu.y, Uu.y + 0.08, wa + (gTexWear - 1.0)*0.12) : 0.0;
    if (m > 0.001 && uTexA[u].x >= 0.0) {   // the substrate (masonry under render, brick behind plaster): its own image colour
      vec3 dnu; TexS su = texSlotTaps(u, w, ng, ap, an, pc, pn, grain, dwx, dwy, dax, day, dcx, dcy, dnu);
      float rim = m*(1.0 - m)*4.0;   // the broken render edge: a thin shadowed lip
      a2 = mix(a2, su.col, m)*(1.0 - (rel ? 0.4 : 0.25)*rim); r2 = mix(r2, su.r, m); ao2 = mix(ao2, su.ao, m);
      dn2 = mix(dn2, dnu*uTexB[u].y, m); h2 -= (rel ? 0.014 : 0.006)*m;   // MA1b: a visible step down to the substrate
    }
  }
  // CP step 6 (corpus 08 s4, s9): roughness varies before colour, at macro scale on every textured surface (one tap),
  // and a micro-detail layer (0.2 m tiling, normal 0.3, roughness +-0.1 around its mean) near the eye, gone by 7 m
  if (uMicroOn.y == 1 && !(uImgMat == 1 && Uu.w > 0.0) && eyeD < 80.0) { vec4 mr = texMacro(w, ng, dwx, dwy); r2 *= 1.0 + (mr.g - 0.5)*0.3*f; }   // FIX5: no second macro tap where the slot already took one (its MA1 macro varies roughness), none past 80 m
  // M4 (corpus 08 s1-s2, s12): wear has causes, so it has places. Splash: the bottom 0.6 m of vertical faces, where rain
  // throws soil back up; runoff: streaks down vertical faces, from the grunge mask stretched 20:1; both raise roughness
  // first, then darken within the 20-40 % budget; the amount varies per building (its seed), so neighbours differ and
  // some stay clean. ap.y is the height above the asset's base (its ground)
  if (uWearCause == 1 && uGrungeK > 0.0 && f > 0.0) {
    float vert = 1.0 - smoothstep(0.35, 0.7, abs(ng.y)), amt = (0.35 + 0.65*fract(gTexSeed*5.31 + 0.17))*f*vert;
    float splash = pow(clamp(1.0 - ap.y/0.6, 0.0, 1.0), 2.0)*amt;
    vec2 ax = normalize(vec2(-ng.z, ng.x) + 1e-5);
    vec2 suv = vec2(dot(w.xz, ax)*0.5, w.y*0.025) + vec2(fract(gTexSeed*3.7), 0.0);
    float run = smoothstep(0.55, 0.85, textureGrad(uGrunge, suv, vec2(dot(dwx.xz, ax)*0.5, dwx.y*0.025), vec2(dot(dwy.xz, ax)*0.5, dwy.y*0.025)).g)*amt*0.8;
    r2 = mix(r2, min(r2 + 0.25, 1.0), max(splash, run*0.6));   // roughness first: dried soil and dust are matt
    a2 *= mix(vec3(1.0), vec3(0.78, 0.74, 0.68), splash*0.9);  // splashed soil, a warm grey-brown (<= 25 % darker)
    a2 *= 1.0 - 0.16*run;                                       // runoff streaks
  }
  if (uMicroOn.x == 1 && uMicroLayer >= 0 && eyeD < 7.0) {
    float mf = (1.0 - smoothstep(2.5, 7.0, eyeD))*f;
    vec3 ga = abs(ng); bool fl = ga.y > max(ga.x, ga.z), xw = ga.x > ga.z;
    vec2 muv = (fl ? w.xz : xw ? w.zy : w.xy)/0.2, mdx = (fl ? dwx.xz : xw ? dwx.zy : dwx.xy)/0.2, mdy = (fl ? dwy.xz : xw ? dwy.zy : dwy.xy)/0.2;
    vec3 mq = vec3(muv, float(uMicroLayer));
    vec2 mn = (texGNrm(mq, mdx, mdy).rg*2.0 - 1.0)*0.3*mf; float mrh = texGRH(mq, mdx, mdy).r;
    vec3 Tm = fl ? vec3(1.0, 0.0, 0.0) : xw ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0), Bm = fl ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    dn2 += Tm*mn.x + Bm*mn.y; r2 *= 1.0 + (mrh - uTexL[uMicroLayer].z)*0.5*mf;
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

void loadSets();
void init(const fs::path& texRoot) {
  root = texRoot;
  gLazyWant.clear(); gPending = false;
  loadMapping(texRoot / "textures.json");
  loadSets();
}

// OPT1 (CP step 5, the mid-match freeze): a set a material names after the load (a lazy library slot, an asset first
// built mid-match) no longer reloads and re-uploads every set: the 1K arrays keep kSpare empty layers and a late set is
// built from the cache and uploaded into the next one (addLateSet), one per frame. Only when the spares are used up does
// it fall back to the full reload, logged as such for the hitch log.
const int kSpare = 16;
int spareNext = 0, spareEnd = 0;
std::set<std::string> gLateFailed;   // late sets that would not build: not retried
void loadSets() {   // TX2: the sets on the GPU (re-run by flushPending; the slot tables and codes stay)
  auto t0 = std::chrono::steady_clock::now();
  const fs::path& texRoot = root;
  // every folder with a colour map becomes a layer (sets named only by a material "texture" field need no slot)
  std::vector<fs::path> dirs;
  std::error_code ec;
  if (fs::exists(texRoot, ec)) for (auto& e : fs::directory_iterator(texRoot, ec)) if (e.is_directory() && (fs::exists(e.path() / "color.jpg") || fs::exists(e.path() / "color.png"))) dirs.push_back(e.path());
  std::sort(dirs.begin(), dirs.end());
  {   // only sets a slot uses take GPU memory; the rest stay on disk (a material "texture" field naming one falls back)
    // only the active table's sets take GPU memory (NMK_TEX_ALL=1: both tables, so the imgmat A/B flips without a reload)
    loadedBoth = std::getenv("NMK_TEX_ALL") != nullptr; loadedImg = wantImg;
    std::set<std::string> want;
    if (loadedBoth || !wantImg) for (auto& s : slots) if (!s.lazy || gLazyWant.count(s.set)) want.insert(s.set);
    if (loadedBoth || wantImg) for (auto& s : slotsImg) if (!s.lazy || gLazyWant.count(s.set)) want.insert(s.set);
    for (auto& n : gLazyWant) want.insert(n);   // sets named by a material "texture" field with no slot of their own
    size_t before = dirs.size();
    dirs.erase(std::remove_if(dirs.begin(), dirs.end(), [&](const fs::path& d) { return !want.count(d.filename().string()); }), dirs.end());
    gInfo.unused = (int)(before - dirs.size());
  }
  if ((int)dirs.size() > kMaxLayers) { logf("textures: %d sets, only the first %d load", (int)dirs.size(), kMaxLayers); dirs.resize(kMaxLayers); }
  bool bc = hasExt("GL_EXT_texture_compression_s3tc") && hasExt("GL_EXT_texture_sRGB");
  gInfo.compressed = bc;
  // TX2K: a set packed at 2K or more (colour map width) goes to the 2K array, after the 1K layers; NMK_TEX_HI=0 keeps all at 1K
  GLint units = 0; glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &units);
  const char* he = std::getenv("NMK_TEX_HI");
  bool hiOk = units >= kUnitBase + 12 && !(he && *he == '0');
  auto isHi = [&](const fs::path& d) {
    int w = 0, h = 0, c = 0;
    return hiOk && (stbi_info((d / "color.jpg").string().c_str(), &w, &h, &c) || stbi_info((d / "color.png").string().c_str(), &w, &h, &c)) && w >= kSizeHi;
  };
  std::stable_partition(dirs.begin(), dirs.end(), [&](const fs::path& d) { return !isHi(d); });
  std::vector<Cpu> cpu(dirs.size());
  for (size_t i = 0; i < dirs.size(); ++i) cpu[i].n = isHi(dirs[i]) ? kSizeHi : kSize;
  std::atomic<size_t> next{0};
  unsigned nt = std::clamp(std::thread::hardware_concurrency(), 1u, 12u);
  std::vector<std::thread> th;
  std::atomic<unsigned> running{0};
  for (unsigned t = 0; t < nt && t < dirs.size(); ++t) { ++running; th.emplace_back([&] { for (size_t i; (i = next++) < dirs.size();) buildSet(dirs[i], bc, cpu[i]); --running; }); }
  while (running.load() > 0) { crumb("texture sets"); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }   // UI9: a loading screen keeps presenting meanwhile
  for (auto& t : th) t.join();
  sets.clear();
  int n = 0, nHi = 0;
  for (size_t i = 0; i < dirs.size(); ++i) if (cpu[i].ok) { if (cpu[i].n == kSizeHi) ++nHi; else ++n; } else logf("textures: %s skipped (%s)", dirs[i].filename().string().c_str(), cpu[i].err.c_str());
  gpuOn = false;
  for (GLuint& a : arr) if (a) { glDeleteTextures(1, &a); a = 0; }
  for (GLuint& a : arrHi) if (a) { glDeleteTextures(1, &a); a = 0; }
  const int spare = std::max(0, std::min(kSpare, kMaxLayers - n - nHi));   // OPT1: empty 1K layers for late sets
  gInfo.bytes = 0; hiBase = n + spare; spareNext = n; spareEnd = n + spare;
  if (n + nHi > 0) {
    GLenum fmt[3] = {bc ? (GLenum)GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT : (GLenum)GL_SRGB8_ALPHA8, bc ? (GLenum)GL_COMPRESSED_RG_RGTC2 : (GLenum)GL_RG8, bc ? (GLenum)GL_COMPRESSED_RG_RGTC2 : (GLenum)GL_RG8};
    auto mk = [&](GLuint* a, int sz, int layers) {   // an empty array still gets one layer: its samplers stay complete
      for (int k = 0; k < 3; ++k) {
        glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &a[k]);
        glTextureStorage3D(a[k], levelsFor(sz), fmt[k], sz, sz, std::max(layers, 1));
        glTextureParameteri(a[k], GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); glTextureParameteri(a[k], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(a[k], GL_TEXTURE_WRAP_S, GL_REPEAT); glTextureParameteri(a[k], GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTextureParameterf(a[k], 0x84FE /* GL_TEXTURE_MAX_ANISOTROPY */, gAniso);
      }
    };
    mk(arr, kSize, n + spare);
    if (nHi > 0) mk(arrHi, kSizeHi, nHi);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    int layer = 0;
    for (size_t i = 0; i < dirs.size(); ++i) {
      if (!cpu[i].ok) continue;
      bool hi = cpu[i].n == kSizeHi; if (hi && layer < hiBase) layer = hiBase;   // OPT1: the 2K layers start after the spares
      GLuint* a = hi ? arrHi : arr; int z = hi ? layer - hiBase : layer;
      for (int k = 0; k < 3; ++k) {
        int s = cpu[i].n;
        for (int L = 0; L < levelsFor(cpu[i].n); ++L, s /= 2) {
          auto& d = cpu[i].lv[k][L];
          if (bc) glCompressedTextureSubImage3D(a[k], L, 0, 0, z, s, s, 1, fmt[k], (GLsizei)d.size(), d.data());
          else glTextureSubImage3D(a[k], L, 0, 0, z, s, s, 1, k == 0 ? GL_RGBA : GL_RG, GL_UNSIGNED_BYTE, d.data());
          gInfo.bytes += d.size();
        }
      }
      SetInfo si = cpu[i].info; si.name = dirs[i].filename().string(); si.layer = layer++;
      sets.push_back(si);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (units >= kUnitBase + 3) {
      for (int k = 0; k < 3; ++k) glBindTextureUnit(kUnitBase + k, arr[k]);
      if (nHi > 0) for (int k = 0; k < 3; ++k) glBindTextureUnit(kUnitBase + 9 + k, arrHi[k]);
      gpuOn = true;
    }
    else logf("textures: GL_MAX_TEXTURE_IMAGE_UNITS = %d, units %d-%d unavailable: procedural only", units, kUnitBase, kUnitBase + 2);
  }
  gInfo.sets = (int)sets.size();
  makeMacro(); gInfo.bytes += 256 * 256 * 4 * 4 / 3;
  loadGrunge();
  resolveLayers();
  invalidateKey();
  gInfo.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  if (nHi > 0) logf("textures: %d of them 2K (TX2K, units %d-%d)", nHi, kUnitBase + 9, kUnitBase + 11);
  logf("textures: array layers %d of %d used (%d 1K sets + %d spare for late sets, %d 2K); slots %d of %d", hiBase + nHi, kMaxLayers, n, spareEnd - spareNext, nHi, (int)slots.size(), kMaxSlots);
  logf("textures: %d sets (%s, %.0f MB), %d/%d slots live, %d sets on disk not used by a slot, %.0f ms", gInfo.sets, bc ? "BC3/BC5" : "RGBA8/RG8", gInfo.bytes / 1048576.0,
       gInfo.slotsLive, gInfo.slots, gInfo.unused, gInfo.ms);
  for (auto& m : gInfo.missing) logf("textures: set '%s' not found (its slots stay procedural)", m.c_str());
  uploadAll();
}

// OPT1: one late set into a spare 1K layer (false: no spare left, or it would not build)
bool addLateSet(const std::string& name, Cpu* built = nullptr) {
  if (!gpuOn || spareNext >= spareEnd || findSet(name) >= 0) return findSet(name) >= 0;
  auto t0 = std::chrono::steady_clock::now();
  Cpu c; c.n = kSize;
  if (built) c = std::move(*built); else buildSet(root / name, gInfo.compressed, c);
  if (!c.ok) { logf("textures: late set %s skipped (%s)", name.c_str(), c.err.c_str()); gLateFailed.insert(name); return true; }
  const bool bc = gInfo.compressed;
  GLenum fmt[3] = {bc ? (GLenum)GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT : (GLenum)GL_SRGB8_ALPHA8, bc ? (GLenum)GL_COMPRESSED_RG_RGTC2 : (GLenum)GL_RG8, bc ? (GLenum)GL_COMPRESSED_RG_RGTC2 : (GLenum)GL_RG8};
  const int z = spareNext++; size_t bytes = 0;
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  for (int k = 0; k < 3; ++k) {
    int s = kSize;
    for (int L = 0; L < levelsFor(kSize); ++L, s /= 2) {
      auto& d = c.lv[k][L];
      if (bc) glCompressedTextureSubImage3D(arr[k], L, 0, 0, z, s, s, 1, fmt[k], (GLsizei)d.size(), d.data());
      else glTextureSubImage3D(arr[k], L, 0, 0, z, s, s, 1, k == 0 ? GL_RGBA : GL_RG, GL_UNSIGNED_BYTE, d.data());
      bytes += d.size();
    }
  }
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  SetInfo si = c.info; si.name = name; si.layer = z; sets.push_back(si);
  gInfo.bytes += bytes; gInfo.sets = (int)sets.size();
  resolveLayers(); invalidateKey(); uploadAll();
  logf("textures: late set %s into spare layer %d (%d of %d spares left), %.1f MB, %.1f ms", name.c_str(), z, spareEnd - spareNext, kSpare,
       bytes / 1048576.0, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
  return true;
}
// budget: sets placed per call (0 = all); a set that finds no spare layer forces the full reload
void flushImpl(int budget, const char* who) {
  if (!gPending) return;
  int placed = 0;
  for (const std::string& n : gLazyWant) {
    if (findSet(n) >= 0 || gLateFailed.count(n)) continue;
    if (budget > 0 && placed >= budget) return;   // the rest next frame (gPending stays)
    if (!addLateSet(n)) {
      auto t0 = std::chrono::steady_clock::now();
      gPending = false; loadSets(); uploadAll();
      logf("textures: FULL RELOAD (%s: no spare layer for %s), %.0f ms", who, n.c_str(), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
      return;
    }
    ++placed;
  }
  gPending = false;
}
void flushPending() { if (!gPending) return; gPending = false; loadSets(); uploadAll(); }   // loading screens: the full reload (it repacks and leaves the spare layers free for mid-match sets)
// per frame: the CPU side (cache read, or decode + compress on a cache miss) runs on a worker; this frame only uploads a
// finished one (~4 MB), so a cache miss costs no frame time
struct LateJob { std::string name; std::shared_ptr<Cpu> cpu; std::future<void> f; };
LateJob gLate;
void flushPendingFrame(bool late) {
  if (!gPending) return;
  static const bool envOld = std::getenv("NMK_TEX_LATE") && std::string(std::getenv("NMK_TEX_LATE")) == "0";   // A/B: the old full reload (perf switch texlate)
  if (!late || envOld) { auto t0 = std::chrono::steady_clock::now(); gPending = false; loadSets(); uploadAll(); logf("textures: FULL RELOAD (frame, NMK_TEX_LATE=0), %.0f ms", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count()); return; }
  if (gLate.f.valid()) {
    if (gLate.f.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    gLate.f.get();
    if (!addLateSet(gLate.name, gLate.cpu.get())) { gLazyWant.insert(gLate.name); flushImpl(0, "frame"); }
    gLate = LateJob{};
    return;   // one upload per frame
  }
  for (const std::string& n : gLazyWant) {
    if (findSet(n) >= 0 || gLateFailed.count(n)) continue;
    if (!gpuOn || spareNext >= spareEnd) { flushImpl(0, "frame"); return; }
    gLate.name = n; gLate.cpu = std::make_shared<Cpu>(); gLate.cpu->n = kSize;
    const bool bc = gInfo.compressed; fs::path dir = root / n; auto cpu = gLate.cpu;
    gLate.f = std::async(std::launch::async, [dir, bc, cpu] { buildSet(dir, bc, *cpu); });
    return;
  }
  gPending = false;
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
    if (s >= 0) {   // TX2: a lazy slot, or a set not on the GPU yet: queued for the next flushPending (after the asset library)
      const std::string& set = slots[s].set;
      std::error_code ec;
      if (!set.empty() && findSet(set) < 0 && !gLazyWant.count(set) && fs::exists(root / set / "color.jpg", ec)) { gLazyWant.insert(set); gPending = true; }
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
  if (!loadedBoth && loadedImg != on && !root.empty()) { loadSets(); uploadAll(); }   // the other table's sets are not loaded: reload them (TX2: the slot tables stay, lazy sets kept)
  else { invalidateKey(); uploadAll(); }
}
bool imgMat() { return wantImg; }
bool enabled() { return gpuOn && wantOn; }
void setEnabled(bool on) { wantOn = on; invalidateKey(); for (GLuint p : progs) glProgramUniform1i(p, glGetUniformLocation(p, "uTexOn"), gpuOn && wantOn ? 1 : 0); }
const Info& info() { return gInfo; }
int slotIndex(const std::string& n) { auto it = slotByName.find(n); return it == slotByName.end() ? -1 : it->second; }
void layerBlend(const float* q, const float* h, int n, int base, float beta, float width, float* w) {   // mirrors the GLSL layerBlend line for line
  float Q = 0; for (int i = 0; i < 4; ++i) { w[i] = 0; if (i < n) Q += std::max(q[i], 0.0f); }
  if (Q <= 1e-5f) { w[base] = 1; return; }
  float s[4]; float sMax = -1e9f; int am = base; float qm = -1;
  for (int i = 0; i < n; ++i) { float qi = std::max(q[i], 0.0f) / Q; s[i] = qi > 1e-5f ? qi + beta * (h[i] - 0.5f) : -1e9f; sMax = std::max(sMax, s[i]); if (qi > qm) { qm = qi; am = i; } }
  float G = 0;
  for (int i = 0; i < n; ++i) { float qi = std::max(q[i], 0.0f) / Q; w[i] = qi > 1e-5f ? qi * std::max(0.0f, s[i] - sMax + width) : 0.0f; G += w[i]; }
  if (G > 1e-5f) { for (int i = 0; i < n; ++i) w[i] /= G; } else { for (int i = 0; i < n; ++i) w[i] = i == am ? 1.0f : 0.0f; }
}
vec3 rnm(vec3 Nb, vec3 Nd) { vec3 T = Nb + vec3(0, 0, 1), U = Nd * vec3(-1, -1, 1); return glm::normalize(T * glm::dot(T, U) / std::max(T.z, 1e-4f) - U); }
int slotLayer(int s) { const auto& t = wantImg ? slotsImg : slots; return s >= 0 && s < (int)t.size() ? t[s].layer : -1; }   // the active table (MA1)
int patternSlot(int p) { return p >= 0 && p < 32 ? (wantImg ? patSlotImg : patSlot)[p] : -1; }

// ------------------------------------------------------------------ offline
int packSet(const fs::path& src, const fs::path& dst, int kSize) {   // kSize: the packed size (1024, or 2048 for TX2K)
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
    // MEM1: every set on disk, not the ones loaded now: lazy sets load as assets build, and a key that followed them
    // missed the cache on every start (each miss rendered, so built, every catalogue asset)
    std::vector<std::string> names;
    for (auto& e : fs::directory_iterator(root, ec)) if (e.is_directory()) names.push_back(e.path().filename().string());
    std::sort(names.begin(), names.end());
    for (auto& nm : names) {
      k += "|" + nm;
      for (const char* f : {"color.jpg", "normal.jpg", "rma.jpg", "meta.json"}) k += ":" + std::to_string(fs::exists(root / nm / f, ec) ? (long long)fs::file_size(root / nm / f, ec) : -1LL);
    }
  }
  gKey = k; gKeyValid = true;
  return gKey;
}
}  // namespace tex

namespace tex {
// NT4: a map's own ground sets ("look": {"paintSets": {"gravel": "<slot>", ...}}): the painted types draw those slots on
// that map only (other maps keep content/textures/textures.json "paint")
void mapPaintSets(const json* look) {
  std::string key = look && look->is_object() && look->contains("paintSets") ? (*look)["paintSets"].dump() : std::string();
  if (key == gPaintKey) return;
  gPaintKey = key;
  std::copy(std::begin(paintSlot), std::end(paintSlot), std::begin(paintSlotEff));
  bool load = false;   // TX4: reload only for a picked set not on the GPU (a reload is seconds)
  if (!key.empty()) for (int k = 0; k < kPaintTypes; ++k) {
    const json& ps = (*look)["paintSets"];
    if (!ps.contains(kPaintTypeNames[k]) || !ps[kPaintTypeNames[k]].is_string()) continue;
    auto it = slotByName.find(ps[kPaintTypeNames[k]].get<std::string>());
    if (it != slotByName.end()) {
      paintSlotEff[k] = it->second;
      const std::string& set = slots[it->second].set;   // TX4: a lazy library set picked for the ground loads now
      std::error_code ec;
      if (!set.empty() && findSet(set) < 0 && !gLazyWant.count(set) && fs::exists(root / set / "color.jpg", ec)) { gLazyWant.insert(set); gPending = load = true; }
    } else logf("textures: look.paintSets: unknown slot '%s'", ps[kPaintTypeNames[k]].get<std::string>().c_str());
  }
  if (load) flushPending();
  uploadAll();
}
std::vector<std::string> groundSlots() {
  std::vector<std::string> v;
  for (auto& s : slots) if ((s.space == 2 || s.space == 3) && !s.set.empty() && s.name.find('@') == std::string::npos && s.name.rfind("set:", 0) != 0) v.push_back(s.name);
  std::sort(v.begin(), v.end());
  return v;
}
}  // namespace tex
