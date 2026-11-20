// HB1b sandbags (docs/sandbags.md): every sandbag box part of every asset and line is drawn as one of four bag meshes
// cut from the user's "Sandbag Wall - 24MB" model (adventurer, CC-BY-4.0; content/assets/sb_bag_lib.json, baked to
// content/assets/models/sb_bag_[a-d], ~420 triangles, LOD1 ~130), instanced through the model-foliage pass. The box
// itself stays exactly as it was for everything else: colliders, bullets, cover, snapping, destruction (Part::bag only
// takes it out of the drawn meshes, appendPart). A course block (one long box for a row of bags) is split into bags.
#include "app.h"
#include "sandbags.h"

namespace {
struct BagLib { const GpuModel* m[4] = {}; const GpuModel* lod[4] = {}; bool tried = false, ok = false; };
BagLib gLib;
uint32_t hsh(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
float u01(uint32_t& s) { s = hsh(s + 0x9E3779B9u); return (s & 0xFFFFFF) / 16777216.0f; }
const float kBagLod = 18.0f, kBagCull = 260.0f;   // LOD1 past 18 m; past 260 m a bag is under a pixel (the box course draws nothing there either)

const BagLib& lib(App& app) {
  if (gLib.tried) return gLib;
  gLib.tried = true;
  const AssetDef* a = app.lib.find("sb_bag_lib");
  if (!a || a->attached.size() < 4) { logf("sandbags: sb_bag_lib (4 bag models) missing: sandbags draw as boxes"); return gLib; }
  for (int k = 0; k < 4; ++k) { gLib.m[k] = a->attached[k].model.get(); gLib.lod[k] = a->attached[k].lod ? a->attached[k].lod.get() : gLib.m[k]; if (!gLib.m[k] || !gLib.m[k]->vao) return gLib; }
  gLib.ok = true;
  return gLib;
}

// one bag filling box (c, R, s) in asset space; the bag's own frame: x length, y thickness, z width
void fitBag(const BagLib& L, vec3 c, const quat& R, vec3 s, uint32_t seed, std::vector<BagXf>& out) {
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
  vec3 S(s[li] / D.x, s[ti] / D.y, s[wi] / D.z);
  S *= vec3(1.06f, 1.1f, 1.06f) * (0.97f + 0.06f * u01(r));   // fills its box, bulging a little past it, as filled bags do
  float flip = u01(r) < 0.5f ? 1.0f : -1.0f;   // the tied end on either side
  mat4 X = glm::translate(mat4(1), c) * glm::mat4_cast(R) * mat4(B)
         * glm::rotate(mat4(1), radians((u01(r) - 0.5f) * 6.0f), vec3(0, 1, 0))   // a few degrees of skew in the course
         * glm::scale(mat4(1), vec3(S.x * flip, S.y, S.z * flip)) * glm::translate(mat4(1), -mc);
  out.push_back({v, X});
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

void sandbagBoxes(const Part& p, uint32_t seed, App& app, std::vector<BagXf>& out) {
  const BagLib& L = lib(app);
  if (!L.ok || !p.bag || p.bag == 2) return;
  uint32_t sd = hsh(seed ^ (uint32_t)(p.center.x * 977.0f) ^ (uint32_t)(p.center.y * 131.0f) * 7u ^ (uint32_t)(p.center.z * 613.0f) * 13u);
  if (p.bag == 1) { fitBag(L, p.center, p.rot, p.size, sd, out); return; }
  // course block: bags 0.58 x 0.17 x 0.36 laid along its length (stretchers), courses up its height, staggered half a bag
  const bool alongX = p.size.x >= p.size.z;
  float len = alongX ? p.size.x : p.size.z, dep = alongX ? p.size.z : p.size.x;
  int nc = std::max(1, (int)std::lround(p.size.y / 0.17f)), nd = std::max(1, (int)std::lround(dep / 0.36f)), nl = std::max(1, (int)std::lround(len / 0.58f));
  float bh = p.size.y / nc, bd = dep / nd, bl = len / nl;
  for (int c = 0; c < nc; ++c) for (int d = 0; d < nd; ++d) {
    float off = (c & 1) ? 0.5f : 0.0f;
    for (int k = 0; k < nl + (off > 0 ? 1 : 0); ++k) {
      float a0 = -len / 2 + (k - off) * bl, a1 = a0 + bl;
      a0 = std::max(a0, -len / 2); a1 = std::min(a1, len / 2);
      if (a1 - a0 < bl * 0.3f) continue;
      vec3 lc((a0 + a1) / 2, -p.size.y / 2 + (c + 0.5f) * bh, -dep / 2 + (d + 0.5f) * bd), ls(a1 - a0, bh, bd);
      if (!alongX) { lc = vec3(lc.z, lc.y, lc.x); ls = vec3(ls.z, ls.y, ls.x); }
      fitBag(L, p.center + p.rot * lc, p.rot, ls, sd + (uint32_t)(c * 977 + d * 131 + k * 31), out);
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
    sandbagBoxes(q, seed, app, c);
  }
  return c;
}

void sandbagDraw(App& app, const AssetDef& a, const mat4& M, float camDist, bool broken) {
  if (!a.hasBags || camDist > kBagCull) return;
  const BagLib& L = lib(app);
  if (!L.ok) return;
  for (const BagXf& b : sandbagXforms(app, a, broken)) app.R.folModels.push_back({camDist > kBagLod ? L.lod[b.v] : L.m[b.v], M * b.m, 0.0f, true});
}

void sandbagDrawList(App& app, const std::vector<BagXf>& bags, const std::vector<uint8_t>& secCut, const mat4& M, float camDist) {
  if (bags.empty() || camDist > kBagCull + 100.0f) return;   // a line is long: its own distance is to its origin
  const BagLib& L = lib(app);
  if (!L.ok) return;
  for (const BagXf& b : bags) {
    if (b.sec >= 0 && b.sec < (int)secCut.size() && secCut[b.sec]) continue;   // a blasted bay's bags are gone with its mesh
    app.R.folModels.push_back({camDist > kBagLod ? L.lod[b.v] : L.m[b.v], M * b.m, 0.0f, true});
  }
}
