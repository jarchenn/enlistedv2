// HB1b sandbags (docs/sandbags.md): every sandbag box part of every asset and line, and the GX1 trench bags, are drawn as
// one of four bag meshes cut from the user's "Sandbag Wall - 24MB" model (adventurer, CC-BY-4.0; content/assets/
// sb_bag_lib.json, baked to content/assets/models/sb_bag_[a-d], ~420 triangles, LOD1 ~130), instanced through the
// model-foliage pass. The box itself stays exactly as it was for everything else: colliders, bullets, cover, snapping,
// destruction (Part::bag only takes it out of the drawn meshes, appendPart). A course block (one long box for a row of
// bags) is split into bags. HB1c: packed tight and uneven (bags meet end to end and bed into the course below, per-bag
// size / yaw / tilt / in-out / level jitter, the odd sagging bag), colour from content/settings/sandbags.json.
#include "app.h"
#include "sandbags.h"

namespace {
struct BagLib { const GpuModel* m[4] = {}; const GpuModel* lod[4] = {}; bool tried = false, ok = false; };
BagLib gLib;
uint32_t hsh(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
float u01(uint32_t& s) { s = hsh(s + 0x9E3779B9u); return (s & 0xFFFFFF) / 16777216.0f; }
float sym(uint32_t& s) { return u01(s) * 2.0f - 1.0f; }
const float kBagLod = 18.0f, kBagCull = 260.0f;   // LOD1 past 18 m; past 260 m a bag is under a pixel (the box course draws nothing there either)

// content/settings/sandbags.json (HB1c): the look, tunable without re-baking the bags
struct Cfg {
  vec3 tint{1.12f, 1.4f, 1.5f};   // x the bag texture (linear): the model's red-brown toward a dull hessian tan-brown
  float valueVar = 0.14f;          // per-bag brightness spread (+-)
  float hueVar = 0.05f;            // per-bag warm / cool spread
  float dirt = 0.72f, dirtH = 0.32f;   // bags whose centre is below dirtH m (asset space) darken toward this factor at the ground
  float lenFill = 1.22f, fullness = 1.28f, bed = 0.02f;   // end-to-end overlap, plumper than the box, sunk into the course below (m)
  float jitter = 1.0f;             // scales every per-bag irregularity (0 = tidy courses)
  float sag = 0.07f;               // share of bags slumped over at a steeper tilt
};
const Cfg& cfg() {
  static Cfg c; static bool loaded = false;
  if (loaded) return c;
  loaded = true;
  json j; try { j = json::parse(readTextFile(contentDir() / "settings" / "sandbags.json")); } catch (...) { logf("sandbags.json: missing or invalid, defaults"); return c; }
  if (j.contains("tint")) c.tint = jvec3(j["tint"], c.tint);
  c.valueVar = j.value("valueVar", c.valueVar); c.hueVar = j.value("hueVar", c.hueVar); c.dirt = j.value("dirt", c.dirt); c.dirtH = j.value("dirtHeight", c.dirtH);
  c.lenFill = j.value("lengthFill", c.lenFill); c.fullness = j.value("fullness", c.fullness); c.bed = j.value("bed", c.bed);
  c.jitter = j.value("jitter", c.jitter); c.sag = j.value("sagShare", c.sag);
  return c;
}

const BagLib& lib(App& app) {
  if (gLib.tried) return gLib;
  const AssetDef* a = app.lib.find("sb_bag_lib");
  if (!a || a->attached.size() < 4) { gLib.tried = true; logf("sandbags: sb_bag_lib (4 bag models) missing: sandbags draw as boxes"); return gLib; }
  for (int k = 0; k < 4; ++k) { gLib.m[k] = a->attached[k].model.get(); gLib.lod[k] = a->attached[k].lod ? a->attached[k].lod.get() : gLib.m[k]; if (!gLib.m[k] || !gLib.m[k]->vao) return gLib; }
  gLib.tried = gLib.ok = true;
  return gLib;
}

// one bag filling box (c, R, s) in asset space; the bag's own frame: x length, y thickness, z width
void fitBag(const BagLib& L, vec3 c, const quat& R, vec3 s, uint32_t seed, vec3 tint, std::vector<BagXf>& out) {
  const Cfg& C = cfg(); const float J = C.jitter;
  uint32_t r = seed;
  int v = (int)(u01(r) * 4.0f) & 3;
  const GpuModel* m = L.m[v];
  vec3 D = glm::max(m->bmax - m->bmin, vec3(0.01f)), mc = 0.5f * (m->bmin + m->bmax);
  // the box's axes by size: longest = length, shortest = thickness, the other = width
  int ax[3] = {0, 1, 2};
  std::sort(ax, ax + 3, [&](int a, int b) { return s[a] > s[b]; });
  int li = ax[0], wi = ax[1], ti = ax[2];
  glm::mat3 B(0.0f); B[0][li] = 1; B[1][ti] = 1; B[2][wi] = 1;   // bag x -> box length axis, y -> thickness, z -> width
  if (glm::determinant(B) < 0) B[2] = -B[2];
  // HB1c packing: long enough to meet its neighbours end to end, fuller than the box, settled into the course below;
  // each bag a little different in length, height, yaw, tilt, how far it sits in or out of the line and how level it is
  vec3 S(s[li] / D.x, s[ti] / D.y, s[wi] / D.z);
  S *= vec3(C.lenFill * (1.0f + 0.07f * J * sym(r)), C.fullness * (1.0f + 0.1f * J * sym(r)), 1.03f + 0.04f * J * sym(r));
  bool sag = u01(r) < C.sag;
  vec3 off(0.015f * J * sym(r), -C.bed * s[ti] / 0.15f + 0.012f * J * sym(r), 0.03f * J * sym(r));   // bag frame: along, down into the bag below, in / out
  if (sag) off.y -= 0.03f;
  float tilt = sag ? (10.0f + 4.0f * u01(r)) * (u01(r) < 0.5f ? -1.0f : 1.0f) : 3.0f * J * sym(r);
  float yaw = radians(4.0f * J * sym(r)), roll = radians(tilt), pitch = radians(3.0f * J * sym(r));
  float flip = u01(r) < 0.5f ? 1.0f : -1.0f;   // the tied end on either side
  mat4 X = glm::translate(mat4(1), c) * glm::mat4_cast(R) * mat4(B) * glm::translate(mat4(1), off)
         * glm::rotate(mat4(1), yaw, vec3(0, 1, 0)) * glm::rotate(mat4(1), roll, vec3(1, 0, 0)) * glm::rotate(mat4(1), pitch, vec3(0, 0, 1))
         * glm::scale(mat4(1), vec3(S.x * flip, S.y, S.z * flip)) * glm::translate(mat4(1), -mc);
  // colour: the configured tint x this bag's value / hue x the caller's tint (trench weathering)
  float val = 1.0f + C.valueVar * sym(r), hue = C.hueVar * sym(r);
  vec3 t = C.tint * tint * val * vec3(1.0f + hue, 1.0f, 1.0f - hue);
  out.push_back({v, X, -1, vec4(t, 2.0f)});
}
}  // namespace

int sandbagKind(const std::string& mat, vec3 s) {
  // only when the bag meshes are baked (else the boxes stay the drawn sandbags); NMK_NO_BAGS=1: the boxes (A/B)
  static const bool on = !std::getenv("NMK_NO_BAGS") && fs::exists(contentDir() / "assets" / "models" / "sb_bag_d" / "mesh.bin");
  if (!on) return 0;
  bool sack = mat.find("hessian") != std::string::npos || mat.find("burlap") != std::string::npos || mat == "sacking" || mat.rfind("sandbag", 0) == 0;
  if (!sack) return 0;
  float a[3] = {s.x, s.y, s.z}; std::sort(a, a + 3);
  if (a[2] <= 0.3f) return 2;   // ties, ears and stitched seams: the bag meshes have their own
  if (a[2] <= 0.8f && a[1] >= 0.15f && a[1] <= 0.5f && a[0] >= 0.06f && a[0] <= 0.26f) return 1;   // one bag
  float h = s.y, w = std::min(s.x, s.z), l = std::max(s.x, s.z);
  if (h >= 0.12f && h <= 1.4f && w >= 0.25f && w <= 1.1f && l >= 0.8f && l <= 8.0f) return 3;   // a course block: split into bags
  return 0;
}

void sandbagBoxes(const Part& p, uint32_t seed, App& app, std::vector<BagXf>& out, vec3 tint) {
  const BagLib& L = lib(app);
  if (!L.ok || !p.bag || p.bag == 2) return;
  uint32_t sd = hsh(seed ^ (uint32_t)(p.center.x * 977.0f) ^ (uint32_t)(p.center.y * 131.0f) * 7u ^ (uint32_t)(p.center.z * 613.0f) * 13u);
  if (p.bag == 1) {
    int ax = p.size.x >= p.size.y && p.size.x >= p.size.z ? 0 : p.size.y >= p.size.z ? 1 : 2;
    int n = p.size[ax] > 0.7f ? 2 : 1;   // a box longer than a bag holds two
    for (int k = 0; k < n; ++k) {
      vec3 lc(0), ls = p.size; ls[ax] /= n; lc[ax] = -p.size[ax] / 2 + (k + 0.5f) * ls[ax];
      fitBag(L, p.center + p.rot * lc, p.rot, ls, sd + (uint32_t)k * 7919u, tint, out);
    }
    return;
  }
  // course block: courses up its height; stretcher courses (bags along the length) alternate their joints, and where the
  // block is deep enough for a bag across it, header courses (bags across) come between them
  const bool alongX = p.size.x >= p.size.z;
  float len = alongX ? p.size.x : p.size.z, dep = alongX ? p.size.z : p.size.x;
  int nc = std::max(1, (int)std::lround(p.size.y / 0.17f));
  float bh = p.size.y / nc;
  for (int c = 0; c < nc; ++c) {
    bool header = (c & 1) && dep >= 0.45f && dep <= 0.8f;
    int nd = header ? 1 : std::max(1, (int)std::lround(dep / 0.36f));
    int nl = std::max(1, (int)std::lround(len / (header ? 0.36f : 0.58f)));
    float bd = dep / nd, bl = len / nl, off = (!header && (c & 2)) ? 0.5f : 0.0f;
    for (int d = 0; d < nd; ++d)
      for (int k = 0; k < nl + (off > 0 ? 1 : 0); ++k) {
        float a0 = -len / 2 + (k - off) * bl, a1 = a0 + bl;
        a0 = std::max(a0, -len / 2); a1 = std::min(a1, len / 2);
        if (a1 - a0 < bl * 0.3f) continue;
        vec3 lc((a0 + a1) / 2, -p.size.y / 2 + (c + 0.5f) * bh, -dep / 2 + (d + 0.5f) * bd), ls(a1 - a0, bh, bd);
        if (!alongX) { lc = vec3(lc.z, lc.y, lc.x); ls = vec3(ls.z, ls.y, ls.x); }
        fitBag(L, p.center + p.rot * lc, p.rot, ls, sd + (uint32_t)(c * 977 + d * 131 + k * 31), tint, out);
      }
  }
}

const std::vector<BagXf>& sandbagXforms(App& app, const AssetDef& a, bool broken) {
  auto& c = a.bagCache[broken ? 1 : 0];
  if (a.bagBuilt[broken ? 1 : 0] || !lib(app).ok) return c;
  a.bagBuilt[broken ? 1 : 0] = true;
  uint32_t seed = 0; for (char ch : a.id) seed = seed * 31u + (uint8_t)ch;
  for (const Part& p : a.parts) {
    if (!p.bag) continue;
    Part q = p;
    if (broken) { q.center.y *= 0.3f; q.size.y *= 0.55f; q.center.z *= 1.35f; }   // the destroyed slump (assets.cpp defBrokenMesh)
    size_t n0 = c.size();
    sandbagBoxes(q, seed, app, c);
    sandbagDirt(c, n0);
  }
  return c;
}
void sandbagDirt(std::vector<BagXf>& bags, size_t from) {   // dirt and damp toward the ground: bags low in the piece darker
  const Cfg& C = cfg();
  for (size_t k = from; k < bags.size(); ++k) {
    float y = bags[k].m[3].y, f = 1.0f - (1.0f - C.dirt) * (1.0f - glm::clamp(y / std::max(C.dirtH, 0.01f), 0.0f, 1.0f));
    bags[k].tint = vec4(vec3(bags[k].tint) * f, bags[k].tint.a);
  }
}

static void pushBag(App& app, const BagLib& L, const BagXf& b, const mat4& M, float camDist) {
  Renderer::FolModelDraw d{camDist > kBagLod ? L.lod[b.v] : L.m[b.v], M * b.m, 0.0f, true};
  d.tint = b.tint; app.R.folModels.push_back(d);
}
void sandbagDraw(App& app, const AssetDef& a, const mat4& M, float camDist, bool broken) {
  if (!a.hasBags || camDist > kBagCull) return;
  const BagLib& L = lib(app);
  if (!L.ok) return;
  for (const BagXf& b : sandbagXforms(app, a, broken)) pushBag(app, L, b, M, camDist);
}
void sandbagDrawList(App& app, const std::vector<BagXf>& bags, const std::vector<uint8_t>& secCut, const mat4& M, float camDist) {
  if (bags.empty() || camDist > kBagCull + 100.0f) return;   // a line is long: its own distance is to its origin
  const BagLib& L = lib(app);
  if (!L.ok) return;
  for (const BagXf& b : bags) {
    if (b.sec >= 0 && b.sec < (int)secCut.size() && secCut[b.sec]) continue;   // a blasted bay's bags are gone with its mesh
    pushBag(app, L, b, M, camDist);
  }
}
