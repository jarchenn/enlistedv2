// MA3 foundation dressing (foundation.h, docs/foundation.md).
//  * hosts: houses, small / large buildings, landmarks, ruins, bunkers and Atlantic Wall casemates;
//  * zones: an irregular band round the asset's ground rectangle (0.2-1.2 m, value noise along the wall), wider
//    clusters at some corners, bare stretches (style dependent), minus the exclusions below;
//  * exclusions: doors / entrances (a lane straight out), other footprints (paths, roads, trenches, walls, buildings),
//    worn route cores (surface wear > 0.8: foundation soil alone stays under 0.75), lasso paint / water, steep ground,
//    spawn markers, firing sectors of embrasures / MG / fire-steps (props and tall plants), tall plants under windows;
//  * plants: GrassInstances (naturePlant) in one buffer per host, drawn with the nature patches; stones: one mesh
//    per host that casts sun shadows only near the eye (R2 castR);
//  * props: a few authored arrangements per host, seeded, validated against the same exclusions, the terrain grade
//    and every footprint; they are real instances (collision, navigation, damage) that the map file never stores.
#include "foundation.h"
#include "app.h"
#include "nature.h"
#include "scenario.h"
#include "interior.h"
#include "ground_paint.h"
#include <chrono>
#include <map>
#include <cstring>

namespace {
inline uint64_t mix64(uint64_t z) { z += 0x9E3779B97F4A7C15ull; z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); }
inline uint64_t hcomb(uint64_t h, uint64_t v) { return mix64(h ^ (v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2))); }
inline uint32_t fbits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
uint64_t fnv(const std::string& s) { uint64_t h = 1469598103934665603ull; for (unsigned char c : s) h = (h ^ c) * 1099511628211ull; return h; }
struct Rng { uint64_t s; float f() { s = mix64(s); return (float)((s >> 40) & 0xFFFFFF) / 16777216.0f; } };
float hgrid(int x, int y, uint32_t s) { uint64_t h = mix64(((uint64_t)(uint32_t)x << 32) ^ (uint32_t)y ^ ((uint64_t)s << 17)); return (float)(h >> 40) / 16777216.0f; }
float vn(vec2 p, uint32_t s) {   // value noise 0..1
  int x = (int)std::floor(p.x), y = (int)std::floor(p.y); float fx = p.x - x, fy = p.y - y;
  fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
  float a = hgrid(x, y, s), b = hgrid(x + 1, y, s), c = hgrid(x, y + 1, s), d = hgrid(x + 1, y + 1, s);
  return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}

const char* kStyleNames[FS_COUNT] = {"maintained", "neglected", "damaged", "bunker", "coastal"};

// ---- plant kinds: a grass-shader species with its own colour and height range
enum Kind : int { K_MOSS, K_LITTER, K_TURF, K_CLOVER, K_TUSSOCK, K_SEED, K_TALL, K_PARSLEY, K_BRUSH, K_STRAW, K_DUNE, K_COUNT };
struct KindDef { int sp; vec3 col; float h0, h1; bool tall; };
const KindDef kKinds[K_COUNT] = {
  {NS_CLOVER, {0.075f, 0.12f, 0.04f}, 0.07f, 0.10f, false},      // moss cushions (low dark clover leaves: squashed turf reads as pale discs)
  {NS_STRAW, {0.27f, 0.19f, 0.09f}, 0.10f, 0.18f, false},        // leaf litter / dead stems (short brown straw)
  {NS_TURF, {0.14f, 0.22f, 0.06f}, 0.08f, 0.17f, false},         // short grass
  {NS_CLOVER, {0.10f, 0.18f, 0.06f}, 0.07f, 0.15f, false},       // clover
  {NS_MEADOWGRASS, {0.16f, 0.23f, 0.07f}, 0.24f, 0.48f, false},  // grass tussock
  {NS_SEEDGRASS, {0.21f, 0.24f, 0.10f}, 0.45f, 0.85f, true},     // seed-head grass
  {NS_TALLMEADOW, {0.22f, 0.25f, 0.11f}, 0.55f, 1.05f, true},    // tall weeds / rank grass
  {NS_COWPARSLEY, {0.17f, 0.25f, 0.09f}, 0.60f, 1.05f, true},    // cow parsley
  {NS_BRUSH, {0.15f, 0.16f, 0.075f}, 0.30f, 0.62f, true},        // scrub shrublets
  {NS_STRAW, {0.36f, 0.31f, 0.17f}, 0.20f, 0.42f, false},        // dry straw grass
  {NS_SEEDGRASS, {0.32f, 0.33f, 0.20f}, 0.40f, 0.75f, true},     // pale dune grass (coastal)
};
struct Mix { float w[K_COUNT]; };
// [style][zone]: zone 0 at the wall (first 0.3 m), 1 the band, 2 corner clusters
const Mix kMix[FS_COUNT][3] = {
  {{{.45f, .20f, .30f, .05f, 0, 0, 0, 0, 0, 0, 0}}, {{0, .03f, .55f, .30f, .10f, .02f, 0, 0, 0, 0, 0}}, {{.05f, .05f, .35f, .2f, .2f, .1f, .05f, 0, 0, 0, 0}}},          // maintained
  {{{.15f, .20f, 0, .05f, .25f, .15f, .15f, .05f, 0, 0, 0}}, {{0, .05f, .05f, .05f, .30f, .22f, .20f, .06f, .07f, 0, 0}}, {{0, .05f, 0, 0, .15f, .20f, .30f, .15f, .15f, 0, 0}}},   // neglected
  {{{.15f, .30f, 0, 0, .10f, .10f, .10f, 0, .05f, .20f, 0}}, {{0, .10f, .05f, .05f, .20f, .25f, .15f, 0, .08f, .12f, 0}}, {{0, .10f, 0, 0, .10f, .25f, .25f, .05f, .15f, .10f, 0}}},   // damaged
  {{{0, .10f, 0, 0, .30f, .20f, .05f, 0, .20f, .15f, 0}}, {{0, .05f, .05f, 0, .30f, .25f, .10f, 0, .15f, .10f, 0}}, {{0, 0, 0, 0, .15f, .15f, .25f, 0, .45f, 0, 0}}},             // bunker
  {{{0, .10f, .10f, 0, .05f, .15f, 0, 0, 0, .30f, .30f}}, {{0, .03f, .15f, 0, .15f, .12f, 0, 0, 0, .25f, .30f}}, {{0, 0, .05f, 0, .10f, .15f, 0, 0, .10f, .20f, .40f}}},           // coastal
};
const float kBare[FS_COUNT] = {0.42f, 0.24f, 0.30f, 0.18f, 0.34f};     // share of the perimeter left bare (value-noise threshold)
const float kWidth[FS_COUNT] = {0.75f, 1.0f, 0.95f, 1.1f, 0.9f};       // band width scale
const float kStones[FS_COUNT] = {0.4f, 1.2f, 4.0f, 2.0f, 2.5f};        // stones per 10 m of wall
const float kCorner[FS_COUNT] = {0.35f, 0.6f, 0.5f, 0.7f, 0.45f};      // chance of a cluster at each corner
const float kDensity = 16.0f;                                          // plants per m^2 of band (pasture grass: 24 clumps)
const float kDens[FS_COUNT] = {1.0f, 1.8f, 1.4f, 1.6f, 1.3f};          // x kDensity per style

// ---- authored prop arrangements: x along the wall from the anchor, z from the wall face to the piece's centre
struct Piece { const char* asset; float x, z, yaw, chance; };
struct Arr { const char* name; std::vector<Piece> p; float len; float w[FS_COUNT]; };
const std::vector<Arr>& arrangements() {
  static const std::vector<Arr> A = {
    {"firewood", {{"furn_wood_pile", 0, 0.30f, 0, 1}, {"furn_wood_pile", 0.98f, 0.30f, 0, 0.6f}, {"furn_bucket", 1.62f, 0.25f, 0, 0.5f}}, 1.9f, {.25f, .25f, .10f, 0, .10f}},
    {"woodpile", {{"drs_woodpile", 1.7f, 0.75f, 0, 1}}, 3.4f, {.20f, .20f, 0, 0, .05f}},
    {"barrels", {{"furn_barrel", 0, 0.36f, 0, 1}, {"furn_barrel", 0.66f, 0.36f, 30, 0.7f}, {"furn_barrel", 0.32f, 0.96f, 70, 0.35f}}, 1.0f, {.15f, .20f, .10f, .30f, .30f}},
    {"crates", {{"furn_crate", 0, 0.30f, 0, 1}, {"furn_crate", 0.58f, 0.32f, 12, 0.6f}, {"furn_barrel", 1.22f, 0.36f, 0, 0.4f}}, 1.5f, {.10f, .15f, .10f, .30f, .30f}},
    {"garden", {{"drs_flower_bed", 1.15f, 0.60f, 0, 1}, {"furn_bucket", 2.6f, 0.25f, 0, 0.5f}}, 2.8f, {.30f, 0, 0, 0, .10f}},
    {"rubble", {{"rub_brick_pile_s", 1.0f, 0.90f, 0, 1}, {"rub_stone_pile_s", 3.0f, 0.95f, 40, 0.4f}}, 4.0f, {0, .10f, .60f, .30f, .10f}},
    {"cart", {{"drs_cart", 1.9f, 1.3f, 0, 1}}, 3.8f, {0, .10f, 0, 0, 0}},
  };
  return A;
}

struct HostRec {
  uint64_t sig = 0; int style = 0, breached = 0;
  std::vector<uint64_t> props;
  std::vector<GrassInstance> inst; GLuint vbo = 0; vec3 mn{1e9f}, mx{-1e9f};
  GpuMesh stones; std::vector<vec3> stonePts;
  vec3 centre{0}; float radius = 0;
};
std::map<uint64_t, HostRec> gRecs;
FoundationStats gStats;
uint64_t gPassSig = 0; int gTick = 0;

bool hostAsset(const AssetDef* a) {
  if (!a) return false;
  const std::string& c = a->category;
  if (c == "Houses" || c == "Large buildings" || c == "Small buildings" || c == "Landmarks" || c == "Ruins" || c == "Bunkers") return true;
  return c == "Atlantic Wall" && (a->id.find("casemate") != std::string::npos || a->id.find("_r6") != std::string::npos);
}
bool isHost(const App& app, const Instance& i) {
  if (isDressingPiece(i) || isFurnishPiece(i)) return false;
  const AssetDef* a = app.assetFor(i);
  return hostAsset(a) && a->footprint.x > 1.5f && a->footprint.y > 1.5f;
}
int styleOf(const App& app, const Instance& h, const AssetDef* a, uint64_t seed) {
  if (h.extra.is_object() && h.extra.contains("dressingStyle")) {
    std::string s = h.extra.value("dressingStyle", std::string());
    for (int k = 0; k < FS_COUNT; ++k) if (s == kStyleNames[k]) return k;
    if (s == "none") return -1;
  }
  if (a->category == "Bunkers" || a->category == "Atlantic Wall") return FS_BUNKER;
  const std::string& id = a->id;
  if (a->category == "Ruins" || id.find("ruin") != std::string::npos || id.find("shelled") != std::string::npos || id.find("burnt") != std::string::npos) return FS_DAMAGED;
  const std::string& t = app.doc.templateId;
  bool coastMap = t.find("omaha") != std::string::npos || t.find("dday") != std::string::npos || t.find("beach") != std::string::npos || t.find("coast") != std::string::npos;
  if (coastMap && h.pos.y < app.terrain.waterLevel + 6.0f) return FS_COASTAL;
  float u = (float)(mix64(seed ^ 0x51ull) >> 40) / 16777216.0f;
  float neglect = a->category == "Large buildings" || a->category == "Landmarks" ? 0.25f : 0.45f;
  return u < neglect ? FS_NEGLECTED : FS_MAINTAINED;
}
uint64_t hostSig(const App& app, const Instance& h, uint64_t routes) {
  uint64_t s = hcomb(fnv(h.assetId), (uint64_t)h.assetVersion);
  s = hcomb(s, ((uint64_t)fbits(h.pos.x) << 32) | fbits(h.pos.z)); s = hcomb(s, ((uint64_t)fbits(h.pos.y) << 32) | fbits(h.yaw));
  s = hcomb(s, (uint64_t)app.doc.seed); s = hcomb(s, routes); s = hcomb(s, (uint64_t)app.terrain.generation);
  if (h.extra.is_object() && h.extra.contains("dressingStyle")) s = hcomb(s, fnv(h.extra["dressingStyle"].dump()));
  return s | 1;
}
uint64_t propId(uint64_t host, int k) { return (1ull << 44) | (host << 6) | (uint64_t)(k & 63); }   // < 2^48: physics user data is id << 16

struct Ctx {
  App& app; const Instance& h; const AssetDef* a; quat q, qi; vec2 lo, hi; int style;
  std::vector<GrassFootprint> fp;   // every other footprint near the host
  std::vector<vec3> spawns;
  // distance from a local point to the host's ground rectangle (outside > 0)
  float rectDist(vec2 l) const { vec2 c = (lo + hi) * 0.5f, hb = (hi - lo) * 0.5f, d = glm::abs(l - c) - hb; return glm::length(glm::max(d, vec2(0))) + std::min(std::max(d.x, d.y), 0.0f); }
  vec3 world(vec2 l) const { return h.pos + q * vec3(l.x, 0, l.y); }
  // level 0: a prop, 1: a tall plant, 2: a short plant
  bool excluded(vec2 l, int level) const {
    const NavMeta& nm = a->navMeta;
    for (auto& e : nm.entrances) {
      vec2 out = -vec2(e.dir.x, e.dir.z); float ol = glm::length(out); if (ol < 1e-3f) continue; out /= ol;
      vec2 v = l - vec2(e.p.x, e.p.z); float along = glm::dot(v, out), lat = std::fabs(v.x * out.y - v.y * out.x);
      if (along > -0.6f && along < (level == 0 ? 3.6f : 2.6f) && lat < (level == 0 ? 1.7f : 1.15f)) return true;
    }
    for (auto& d : nm.doors) if ((d.a == "outside" || d.b == "outside") && glm::length(l - vec2(d.p.x, d.p.z)) < (level == 0 ? 2.2f : 1.4f)) return true;
    for (auto& f : nm.firingPoints) {
      vec2 v = l - vec2(f.p.x, f.p.z);
      if (f.kind == "window") { if (level <= 1 && glm::length(v) < 1.6f) return true; continue; }   // no shrubs through windows, no props in front of them
      if (level == 2) continue;   // short grass under a gun sector is fine
      vec2 fw(std::sin(radians(f.yaw)), std::cos(radians(f.yaw)));
      float dl = glm::length(v); if (dl < 1.0f) return true;
      if (dl < (level == 0 ? 16.0f : 9.0f) && glm::dot(v / dl, fw) > std::cos(radians(60.0f))) return true;   // gun arcs and firing apertures
    }
    vec3 w = world(l);
    const Terrain& t = app.terrain;
    if (w.x < t.org.x + 1 || w.z < t.org.y + 1 || w.x > t.org.x + t.size - 1 || w.z > t.org.y + t.sizeZ - 1) return true;
    float y = t.height(w.x, w.z);
    if (y < t.waterLevel + 0.05f || t.normal(w.x, w.z).y < (level == 0 ? 0.9f : 0.62f)) return true;
    if (t.wearAt(w.x, w.z) > 0.8f || groundPaintBlocksNature(w.x, w.z)) return true;   // route cores (lanes, footpaths, gravel), paint
    for (auto& f : fp) if (Grass::footprintDistance(f, w.x, w.z) < (level == 0 ? 0.15f : 0.05f)) return true;
    for (auto& s : spawns) if (glm::length(vec2(s.x - w.x, s.z - w.z)) < (level == 0 ? 5.0f : 3.0f)) return true;
    return false;
  }
};

int pickKind(const Mix& m, float u) { float t = 0; for (float w : m.w) t += w; u *= t; for (int k = 0; k < K_COUNT; ++k) { u -= m.w[k]; if (u <= 0) return k; } return K_TURF; }

void destroyRec(App& app, uint64_t host, HostRec& r, bool props) {
  if (r.vbo) glDeleteBuffers(1, &r.vbo);
  r.vbo = 0; r.inst.clear(); r.stones.destroy(); r.stonePts.clear();
  if (props && !r.props.empty()) {
    auto& v = app.doc.instances;
    v.erase(std::remove_if(v.begin(), v.end(), [&](const Instance& i) { return isDressingPiece(i) && i.extra["dressing"].value("host", (uint64_t)0) == host; }), v.end());
    r.props.clear();
  }
}

void generate(App& app, Instance& h, HostRec& rec, const std::vector<GrassFootprint>& fpAll, const std::vector<vec3>& spawns) {
  const AssetDef* a = app.assetFor(h);
  uint64_t seed = mix64(h.id * 0x9E3779B97F4A7C15ull ^ fnv(h.assetId) ^ ((uint64_t)app.doc.seed << 20));
  int style = styleOf(app, h, a, seed);
  rec.style = style;
  if (style < 0) return;
  Ctx c{app, h, a, yawQuat(h.yaw), glm::inverse(yawQuat(h.yaw)), vec2(a->bmin.x, a->bmin.z), vec2(a->bmax.x, a->bmax.z), style, {}, spawns};
  rec.centre = h.pos; rec.radius = glm::length(c.hi - c.lo) * 0.5f + 3.0f;
  vec2 hc(h.pos.x, h.pos.z);
  for (auto& f : fpAll) {
    if (glm::length(f.pos - hc) > rec.radius + glm::length(glm::max(glm::abs(f.bmin), glm::abs(f.bmax))) + 1) continue;
    if (glm::length(f.pos - hc) < 1e-3f && std::fabs(f.yaw - h.yaw) < 1e-3f) continue;   // the host's own footprint
    c.fp.push_back(f);
  }
  const Terrain& t = app.terrain;
  Rng rng{seed};
  uint32_t ns = (uint32_t)(seed >> 32);
  vec2 C[4] = {{c.lo.x, c.lo.y}, {c.hi.x, c.lo.y}, {c.hi.x, c.hi.y}, {c.lo.x, c.hi.y}};
  vec2 N[4] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
  auto addPlant = [&](vec2 l, int zone) {
    int k = pickKind(kMix[style][zone], rng.f());
    float r0 = rng.f(), r1 = rng.f(), r2 = rng.f(), r3 = rng.f();
    if (c.excluded(l, kKinds[k].tall ? 1 : 2)) {
      if (!kKinds[k].tall || c.excluded(l, 2)) return;
      k = zone == 0 ? K_MOSS : K_TURF;   // under a window / in a gun sector: keep it short
    }
    vec3 w = c.world(l); w.y = t.height(w.x, w.z);
    const KindDef& kd = kKinds[k];
    vec3 col = kd.col * (0.88f + 0.24f * r3);
    float sun = std::clamp(0.5f + 0.5f * glm::dot(glm::normalize(c.q * vec3(l.x - (c.lo.x + c.hi.x) * 0.5f, 0, l.y - (c.lo.y + c.hi.y) * 0.5f)), vec3(0, 0, -1)), 0.0f, 1.0f);   // north faces (world -z) darker, mossier
    if (k == K_TURF || k == K_TUSSOCK) col = glm::mix(col, col * vec3(0.85f, 0.92f, 0.85f), sun * 0.5f);
    rec.inst.push_back(naturePlant(kd.sp, w, col, glm::mix(kd.h0, kd.h1, r1), r0, r1, r2, r3));
    rec.mn = glm::min(rec.mn, w - vec3(0.3f, 0, 0.3f)); rec.mx = glm::max(rec.mx, w + vec3(0.3f, kd.h1 + 0.2f, 0.3f));
  };
  // ---- the band along each wall
  for (int e = 0; e < 4; ++e) {
    vec2 A = C[e], B = C[(e + 1) & 3], T = glm::normalize(B - A), n = N[e];
    float L = glm::length(B - A);
    for (float s = 0.15f; s < L - 0.15f; s += 0.3f) {
      vec2 p = A + T * s; vec3 wp = c.world(p);
      float bare = vn(vec2(wp.x, wp.z) * 0.21f, ns + 11);
      if (bare < kBare[style]) continue;   // intentional bare ground
      float width = std::clamp(glm::mix(0.2f, 1.2f, vn(vec2(wp.x, wp.z) * 0.35f, ns + 7)) * kWidth[style], 0.2f, 1.2f);
      int cnt = std::max(1, (int)std::round(width * 0.3f * kDensity * kDens[style] * (0.6f + 0.8f * (bare - kBare[style]) / (1.0f - kBare[style]))));
      for (int j = 0; j < cnt; ++j) {
        float d = 0.05f + width * (j + rng.f()) / cnt;
        vec2 l = p + n * d + T * (rng.f() - 0.5f) * 0.3f;
        addPlant(l, d < 0.3f ? 0 : 1);
      }
    }
  }
  // ---- corner clusters (garden edges, the angle of two walls)
  for (int e = 0; e < 4; ++e) {
    if (rng.f() > kCorner[style]) { rng.f(); continue; }
    float R = glm::mix(0.9f, 2.1f, rng.f()) * kWidth[style];
    vec2 cn = C[e], d1 = N[e], d0 = N[(e + 3) & 3];   // the corner's two outward normals
    int cnt = (int)(R * R * 0.785f * kDensity * kDens[style] * 0.9f);
    for (int j = 0; j < cnt; ++j) {
      float u = rng.f(), v = rng.f(), rr = R * std::sqrt(u);
      vec2 l = cn + (d0 * std::cos(v * 1.5708f) + d1 * std::sin(v * 1.5708f)) * (rr + 0.05f);
      addPlant(l, rr < 0.3f ? 0 : 2);
    }
  }
  // ---- small stones (and restrained rubble chips on damaged styles)
  MeshData sm;
  float perim = 2 * ((c.hi.x - c.lo.x) + (c.hi.y - c.lo.y));
  int ns2 = (int)(perim / 10.0f * kStones[style] + rng.f());
  for (int j = 0; j < ns2; ++j) {
    int e = (int)(rng.f() * 4) & 3; vec2 A = C[e], B = C[(e + 1) & 3];
    vec2 l = A + (B - A) * rng.f() + N[e] * (0.05f + 0.7f * rng.f() * rng.f());
    float r0 = rng.f(), r1 = rng.f(), r2 = rng.f(), r3 = rng.f();
    if (c.excluded(l, 2)) continue;
    vec3 w = c.world(l); w.y = t.height(w.x, w.z);
    float size = glm::mix(0.07f, style == FS_DAMAGED ? 0.30f : 0.19f, r1 * r1);
    natureStone(sm, w, t.normal(w.x, w.z), size, r0, r1, r2, r3, style == FS_COASTAL ? 0 : r3 < 0.6f ? 2 : 1);   // mostly schist: pale limestone chips read as white discs
    rec.stonePts.push_back(w);
  }
  if (!sm.i.empty()) rec.stones.upload(sm);
  // the patch draw thins far plants by drawing a prefix of the buffer: a seeded shuffle spreads that thinning evenly
  for (size_t k = rec.inst.size(); k > 1; --k) std::swap(rec.inst[k - 1], rec.inst[(size_t)(rng.f() * k) % k]);
  if (!rec.inst.empty()) { glCreateBuffers(1, &rec.vbo); glNamedBufferData(rec.vbo, rec.inst.size() * sizeof(GrassInstance), rec.inst.data(), GL_STATIC_DRAW); }
  // ---- props: 0-3 arrangements against the walls
  static const int kMin[FS_COUNT] = {1, 1, 1, 0, 0}, kMax[FS_COUNT] = {2, 3, 2, 2, 2};
  int want = kMin[style] + (int)(rng.f() * (kMax[style] - kMin[style] + 1));
  if (h.id >= (1ull << 38)) want = 0;   // the id space for its pieces (propId)
  const auto& arrs = arrangements();
  std::vector<GrassFootprint> placed;   // this host's accepted pieces
  int k = 0;
  for (int n = 0, tries = 0; n < want && tries < 14; ++tries) {
    float tw = 0; for (auto& ar : arrs) tw += ar.w[style];
    if (tw <= 0) break;
    float u = rng.f() * tw; const Arr* ar = &arrs[0];
    for (auto& x : arrs) { u -= x.w[style]; if (u <= 0) { ar = &x; break; } }
    int e = (int)(rng.f() * 4) & 3; vec2 A = C[e], B = C[(e + 1) & 3], T = glm::normalize(B - A);
    float L = glm::length(B - A);
    if (L < ar->len + 1.2f) continue;
    float s = 0.6f + rng.f() * (L - ar->len - 1.2f);
    float eyaw = e == 0 ? 180.0f : e == 1 ? 90.0f : e == 2 ? 0.0f : -90.0f;   // the piece's front (+z) faces out
    struct Cand { const AssetDef* ca; vec3 pos; float yaw; };
    std::vector<Cand> cands; bool ok = true;
    for (auto& p : ar->p) {
      if (rng.f() > p.chance) continue;
      const AssetDef* ca = app.lib.find(p.asset); if (!ca) continue;
      vec2 l = A + T * (s + p.x) + N[e] * p.z;
      float yaw = eyaw + p.yaw;
      // samples: centre and footprint corners, in host-local space
      vec2 hf = ca->footprint * 0.5f; float ly = radians(yaw), cy = std::cos(ly), sy = std::sin(ly);
      auto toL = [&](float x, float z) { return l + vec2(x * cy + z * sy, -x * sy + z * cy); };
      vec2 S[5] = {l, toL(-hf.x, -hf.y), toL(hf.x, -hf.y), toL(hf.x, hf.y), toL(-hf.x, hf.y)};
      float top = -1e9f, low = 1e9f;
      for (auto& q : S) {
        if (c.rectDist(q) < 0.03f || c.excluded(q, 0)) { ok = false; break; }
        vec3 w = c.world(q); float y = t.height(w.x, w.z); top = std::max(top, y); low = std::min(low, y);
        for (auto& f : placed) if (Grass::footprintDistance(f, w.x, w.z) < 0.05f) { ok = false; break; }
        if (!ok) break;
      }
      if (!ok || top - low > 0.25f) { ok = false; break; }
      vec3 w = c.world(l); w.y = top;
      cands.push_back({ca, w, wrapDeg(h.yaw + yaw)});
    }
    if (!ok || cands.empty()) continue;
    for (auto& cd : cands) {
      if (k >= 63) break;
      Instance i; i.id = propId(h.id, k++); i.assetId = cd.ca->id; i.assetVersion = cd.ca->version; i.pos = cd.pos; i.yaw = cd.yaw;
      i.extra = json::object(); i.extra["dressing"] = {{"host", h.id}, {"sig", rec.sig}};
      if (app.doc.find(i.id)) continue;
      i.rev = ++app.revCounter;   // not app.bump: derived dressing never makes the map dirty
      app.doc.instances.push_back(i);
      rec.props.push_back(i.id);
      placed.push_back({vec2(i.pos.x, i.pos.z), i.yaw, vec2(cd.ca->bmin.x, cd.ca->bmin.z), vec2(cd.ca->bmax.x, cd.ca->bmax.z)});
    }
    ++n;
  }
}
}  // namespace

const char* foundationStyleName(int s) { return s >= 0 && s < FS_COUNT ? kStyleNames[s] : "none"; }
bool isDressingPiece(const Instance& i) { return i.extra.is_object() && i.extra.contains("dressing"); }

void foundationSync(App& app) {
  static const bool off = std::getenv("NMK_NO_DRESSING") != nullptr;   // A/B measurement: no foundation dressing at all
  if (off) return;
  ++gTick;
  // destruction: props beside a breached wall panel go (their support is gone); a reset brings them back
  if (gTick % 10 == 0) for (auto& [hid, rec] : gRecs) {
    auto it = app.rt.find(hid); if (it == app.rt.end()) continue;
    const InstanceRT& r = it->second; const AssetDef* a = r.asset; if (!a || a->panels.empty()) continue;
    int br = 0; for (size_t k = 0; k < r.state.size(); ++k) br += r.state[k] == PANEL_BREACHED;
    if (br < rec.breached) { rec.sig = 0; rec.breached = 0; gPassSig = 0; continue; }   // repaired / reset: rebuilt below
    if (br == rec.breached || rec.props.empty()) { rec.breached = br; continue; }
    rec.breached = br;
    const Instance* h = app.doc.find(hid); if (!h) continue;
    quat qi = glm::inverse(yawQuat(h->yaw));
    std::vector<uint64_t> gone;
    for (uint64_t pid : rec.props) {
      const Instance* p = app.doc.find(pid); if (!p) continue;
      vec3 l = qi * (p->pos - h->pos);
      for (size_t k = 0; k < r.state.size() && k < a->panels.size(); ++k) {
        if (r.state[k] != PANEL_BREACHED) continue;
        const Panel& pn = a->panels[k];
        if (l.x > pn.bmin.x - 1.2f && l.x < pn.bmax.x + 1.2f && l.z > pn.bmin.z - 1.2f && l.z < pn.bmax.z + 1.2f) { gone.push_back(pid); break; }
      }
    }
    if (gone.empty()) continue;
    auto& v = app.doc.instances;
    v.erase(std::remove_if(v.begin(), v.end(), [&](const Instance& i) { return std::find(gone.begin(), gone.end(), i.id) != gone.end(); }), v.end());
    for (uint64_t g : gone) rec.props.erase(std::remove(rec.props.begin(), rec.props.end(), g), rec.props.end());
    gStats.pruned += (int)gone.size();
    app.syncRuntime();
  }
  // placement / load / move / delete: only when an instance changed, appeared or went away
  if (app.terrain.surf.empty()) return;   // the route mask is not built yet (first frame after a load)
  uint64_t sig = hcomb(hcomb((uint64_t)app.revCounter, app.doc.instances.size()), (uint64_t)(uintptr_t)&app.doc);
  sig = hcomb(sig, (uint64_t)app.terrain.generation);
  if (sig == gPassSig) return;
  auto t0 = std::chrono::steady_clock::now();
  uint64_t routes = app.doc.extra.is_object() && app.doc.extra.contains("routes") ? fnv(app.doc.extra["routes"].dump()) : 0;
  std::map<uint64_t, Instance*> hosts;
  for (auto& i : app.doc.instances) if (isHost(app, i)) hosts[i.id] = &i;
  bool changed = false;
  // records and pieces whose host went away
  for (auto it = gRecs.begin(); it != gRecs.end();) {
    if (!hosts.count(it->first)) { destroyRec(app, it->first, it->second, true); it = gRecs.erase(it); changed = true; } else ++it;
  }
  {   // stray pieces (a host deleted while undone, a stale undo snapshot, a reload): their host's record must list them
    auto& v = app.doc.instances; size_t n0 = v.size();
    v.erase(std::remove_if(v.begin(), v.end(), [&](const Instance& i) {
      if (!isDressingPiece(i)) return false;
      uint64_t hid = i.extra["dressing"].value("host", (uint64_t)0);
      auto r = gRecs.find(hid);
      return !hosts.count(hid) || r == gRecs.end() || std::find(r->second.props.begin(), r->second.props.end(), i.id) == r->second.props.end();
    }), v.end());
    changed |= v.size() != n0;
  }
  hosts.clear();   // the erases above moved the instances
  for (auto& i : app.doc.instances) if (isHost(app, i)) hosts[i.id] = &i;
  std::vector<GrassFootprint> fp; bool fpBuilt = false;
  std::vector<vec3> spawns; bool spBuilt = false;
  std::vector<uint64_t> todo;
  for (auto& [id, h] : hosts) {
    HostRec& rec = gRecs[id];
    uint64_t hs = hostSig(app, *h, routes);
    bool missing = false;
    for (uint64_t pid : rec.props) if (!app.doc.find(pid)) { missing = true; break; }   // undo / redo dropped them
    if (rec.sig == hs && !missing) continue;
    todo.push_back(id);
  }
  // build every footprint before the props of this pass are added (they are compared with the placed list instead)
  for (uint64_t id : todo) {
    HostRec& rec = gRecs[id];
    destroyRec(app, id, rec, true);   // erases its pieces: host pointers are re-found below
    if (!fpBuilt) {
      fp = app.grassFootprints(); fpBuilt = true;
      fp.erase(std::remove_if(fp.begin(), fp.end(), [](const GrassFootprint& f) { return f.soft; }), fp.end());
    }
    if (!spBuilt) {
      spBuilt = true;
      if (app.doc.extra.is_object() && app.doc.extra.contains("scenario")) {
        try { Scenario sc = Scenario::fromJson(app.doc.extra["scenario"]); for (auto& s : sc.spawns) spawns.push_back(s.pos); } catch (...) {}
      }
    }
    Instance* h = app.doc.find(id);
    if (!h) continue;
    rec.sig = hostSig(app, *h, routes); rec.breached = 0;
    generate(app, *h, rec, fp, spawns);
    ++gStats.rebuilds; changed = true;
  }
  if (changed) app.syncRuntime();
  gPassSig = hcomb(hcomb((uint64_t)app.revCounter, app.doc.instances.size()), (uint64_t)(uintptr_t)&app.doc);
  gPassSig = hcomb(gPassSig, (uint64_t)app.terrain.generation);
  gStats.hosts = (int)gRecs.size(); gStats.props = 0; gStats.plants = 0; gStats.stones = 0;
  for (auto& [id, r] : gRecs) { gStats.props += (int)r.props.size(); gStats.plants += (int)r.inst.size(); gStats.stones += (int)r.stonePts.size(); }
  if (!todo.empty()) gStats.lastMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

void foundationDrawItems(App& app, std::vector<DrawItem>& op, vec3 eye) {
  for (auto& [id, r] : gRecs) {
    float d = glm::length(r.centre - eye) - r.radius;
    if (d > 140.0f) continue;   // the nature patches' plant reach
    if (r.vbo && !r.inst.empty()) { NatPatchDraw p; p.vbo = r.vbo; p.count = (int)r.inst.size(); p.mn = r.mn; p.mx = r.mx; p.reach = 1.0f; app.R.natPatches.push_back(p); }
    if (r.stones.count > 0 && d < 90.0f) { DrawItem it{&r.stones, mat4(1)}; it.castR = 14.0f; op.push_back(it); }   // tiny casters: near the eye only
  }
}

void foundationReset(App& app) {
  for (auto& [id, r] : gRecs) destroyRec(app, id, r, true);
  gRecs.clear(); gPassSig = 0;
  auto& v = app.doc.instances;
  v.erase(std::remove_if(v.begin(), v.end(), [](const Instance& i) { return isDressingPiece(i); }), v.end());
  app.syncRuntime();
}

FoundationStats foundationStats() { return gStats; }

bool foundationHostInfo(uint64_t host, std::vector<vec3>& plants, std::vector<vec3>& stones, int& style) {
  auto it = gRecs.find(host); if (it == gRecs.end()) return false;
  plants.clear(); for (auto& g : it->second.inst) plants.push_back(vec3(g.posYaw));
  stones = it->second.stonePts; style = it->second.style;
  return true;
}
