// GX1 terrain conformance (terrain_blend.h; docs/graphics-overhaul.md#gx1-terrain-conformance). The stamps are
// evaluated in terrain_cuts.cpp; this file derives them from the instances, levels placements, generates the
// revetment / skirt meshes and validates seating after reforms.
#include "terrain_blend.h"
#include "snap.h"
#include "render.h"
#include <algorithm>
#include <numeric>

float terrainFbm(float x, float z, uint32_t s);   // terrain.cpp

namespace {
float sstep(float e0, float e1, float x) { float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f); return t * t * (3 - 2 * t); }
vec2 toWorld(vec2 l, float yawDeg) { float ca = std::cos(radians(yawDeg)), sa = std::sin(radians(yawDeg)); return vec2(ca * l.x + sa * l.y, -sa * l.x + ca * l.y); }
uint32_t idSeed(uint64_t id) { uint64_t s = (id + 0x9E3779B97F4A7C15ull) * 0xBF58476D1CE4E5B9ull; s ^= s >> 31; return (uint32_t)s; }
float hash01(uint32_t s, uint32_t k) { uint32_t h = s ^ (k * 0x9E3779B1u); h ^= h >> 16; h *= 0x7feb352dU; h ^= h >> 15; h *= 0x846ca68bU; h ^= h >> 16; return (h & 0xffffff) / 16777216.0f; }

BlendConfig loadConfig() {
  BlendConfig c;
  json j;
  try { j = json::parse(readTextFile(contentDir() / "settings" / "terrain_blend.json")); } catch (...) { logf("gx1: content/settings/terrain_blend.json missing or invalid: defaults"); return c; }
  c.enabled = j.value("enabled", true);
  json p = j.value("placement", json::object()), e = j.value("earthwork", json::object()), t = j.value("trench", json::object());
  c.margin = p.value("margin", c.margin); c.ringMin = p.value("ringMin", c.ringMin); c.ringMax = p.value("ringMax", c.ringMax);
  c.ringPerMetre = p.value("ringPerMetre", c.ringPerMetre); c.minDelta = p.value("minDelta", c.minDelta); c.skirtThreshold = p.value("skirtThreshold", c.skirtThreshold);
  StampParams& s = c.sp;
  s.skirtEmbed = p.value("skirtEmbed", s.skirtEmbed); s.soilMax = p.value("soilMax", s.soilMax); s.soilPerMetre = p.value("soilPerMetre", s.soilPerMetre);
  s.reposeDeg = e.value("reposeDeg", s.reposeDeg); s.crest = e.value("crest", s.crest); s.smoothing = e.value("smoothing", s.smoothing);
  s.noise = e.value("noise", s.noise); s.noiseScale = e.value("noiseScale", s.noiseScale); s.edgeJitter = e.value("edgeJitter", s.edgeJitter);
  s.blendK = e.value("blendK", s.blendK); s.sparseSlope = e.value("sparseSlope", s.sparseSlope); s.bareSlope = e.value("bareSlope", s.bareSlope);
  s.contactBand = e.value("contactBand", s.contactBand); s.holeEdge = e.value("holeEdge", s.holeEdge);
  s.depth = t.value("depth", s.depth); s.floorHalf = t.value("floorHalf", s.floorHalf); s.batter = t.value("batter", s.batter);
  s.parapetH = t.value("parapetHeight", s.parapetH); s.parapetW = t.value("parapetWidth", s.parapetW); s.parapetNoise = t.value("parapetNoise", s.parapetNoise);
  s.parapetFront = t.value("parapetFront", s.parapetFront);
  c.revetment = t.value("revetment", true); c.sandbags = t.value("sandbags", true); c.pickets = t.value("pickets", c.pickets);
  return c;
}
float median(std::vector<float> v) {
  if (v.empty()) return 0;
  std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
  float m = v[v.size() / 2];
  if (v.size() % 2 == 0) { float lo = *std::max_element(v.begin(), v.begin() + v.size() / 2); m = 0.5f * (m + lo); }
  return m;
}
// footprint samples (local): a 5 x 5 grid over the footprint plus the perimeter every ~1 m
std::vector<vec2> footprintSamples(vec2 half, bool perimeterOnly) {
  std::vector<vec2> out;
  if (!perimeterOnly) for (int j = 0; j < 5; ++j) for (int i = 0; i < 5; ++i) out.push_back(vec2(-half.x + half.x * 0.5f * i, -half.y + half.y * 0.5f * j));
  int nx = std::max(2, (int)std::ceil(half.x * 2.0f)), nz = std::max(2, (int)std::ceil(half.y * 2.0f));
  for (int i = 0; i <= nx; ++i) { float x = -half.x + 2 * half.x * i / nx; out.push_back(vec2(x, -half.y)); out.push_back(vec2(x, half.y)); }
  for (int j = 1; j < nz; ++j) { float z = -half.y + 2 * half.y * j / nz; out.push_back(vec2(-half.x, z)); out.push_back(vec2(half.x, z)); }
  return out;
}
vec2 padHalf(const AssetDef* a) {
  const json& ew = a->raw.is_object() && a->raw.contains("earthwork") ? a->raw["earthwork"] : json();
  if (ew.is_object() && ew.contains("core")) return vec2(ew["core"][0].get<float>(), ew["core"][1].get<float>());   // the walls: the banks start there
  return a->footprint * 0.5f;
}
const TerrainStamp* stampOf(const Terrain& T, uint64_t id, uint8_t kind) {
  for (auto& s : T.stamps) if (s.kind == kind && std::find(s.members.begin(), s.members.end(), id) != s.members.end()) return &s;
  return nullptr;
}
const Material& matNamed(const AssetDef* a, const char* n) {
  for (auto& m : a->materials) if (m.name == n) return m;
  return a->materials[0];
}
}   // namespace

const BlendConfig& blendConfig() { static BlendConfig c = loadConfig(); return c; }
bool blendEnabled() { return blendConfig().enabled; }

bool blendTrench(const AssetDef* a) { return a && a->raw.is_object() && a->raw.contains("trench"); }
bool blendPinned(const AssetDef* a) {
  if (!a || !blendEnabled() || a->path.on || blendTrench(a)) return false;
  if (a->raw.is_object() && a->raw.contains("prop")) return false;   // furniture
  if (crushBuilding(a)) return true;
  return a->category == "Atlantic Wall" && a->navMeta.terrainCuts.empty() && a->footprint.x * a->footprint.y >= 6.0f;
}

int blendPartMode(const json& asset, const json& pj) {
  if (!blendEnabled() || !asset.is_object() || !pj.is_object()) return 0;
  const std::string m = pj.value("m", std::string());
  if (asset.contains("trench")) {
    const json& r = asset["trench"].value("replace", json::object());
    auto mats = r.value("materials", json::array());
    if (std::find(mats.begin(), mats.end(), m) == mats.end()) return 0;
    float top = pj.contains("c") && pj.contains("s") ? pj["c"][1].get<float>() + 0.5f * pj["s"][1].get<float>() : 1.0f;
    if (top <= r.value("aboveY", -1.0f)) return 0;   // the firestep, floor and duckboards below stay
    auto keep = r.value("keepCollision", json::array());
    return std::find(keep.begin(), keep.end(), m) != keep.end() && pj.value("k", 1) != 0 ? 2 : 1;
  }
  if (asset.contains("earthwork")) {
    const json& r = asset["earthwork"].value("replace", json::object());
    auto mats = r.value("materials", json::array());
    return pj.value("t", std::string()) == r.value("shape", std::string("p")) && std::find(mats.begin(), mats.end(), m) != mats.end() ? 1 : 0;
  }
  return 0;
}

// ================================================================ stamps from the instances
int blendSyncStamps(App& app) {
  Terrain& T = app.terrain;
  const BlendConfig& C = blendConfig();
  if (!C.enabled) return T.setStamps({});
  T.sp = C.sp; T.sp.carvePad = T.fineSpacing();
  const StampParams& sp = C.sp;
  std::vector<TerrainStamp> out;
  const float tanR = std::tan(radians(std::clamp(sp.reposeDeg, 15.0f, 45.0f)));
  struct Piece { uint64_t id; std::vector<vec3> path; std::vector<std::vector<vec3>> branches; int8_t front = 0; };   // world x, floor y, z; front: the fire side (+1 left of the path)
  std::vector<Piece> pieces;
  for (auto& i : app.doc.instances) {
    const AssetDef* a = app.assetFor(i);
    if (!a) continue;
    if (blendTrench(a)) {
      Piece p; p.id = i.id;
      auto pt = [&](const json& q) { vec2 w = vec2(i.pos.x, i.pos.z) + toWorld(vec2(q[0].get<float>(), q[1].get<float>()), i.yaw); return vec3(w.x, i.pos.y - sp.depth * (q.size() > 2 ? q[2].get<float>() : 1.0f), w.y); };
      const json& tj = a->raw["trench"];
      for (auto& q : tj.value("path", json::array())) p.path.push_back(pt(q));
      for (auto& b : tj.value("branches", json::array())) { std::vector<vec3> v; for (auto& q : b) v.push_back(pt(q)); if (v.size() >= 2) p.branches.push_back(v); }
      if (p.path.size() >= 2) {   // the fire side is the piece's local +z, taken on its first leg (a corner keeps it round the turn)
        vec2 d = glm::normalize(vec2(p.path[1].x - p.path[0].x, p.path[1].z - p.path[0].z)), lz = toWorld(vec2(0, 1), i.yaw);
        float f = glm::dot(vec2(-d.y, d.x), lz);
        p.front = f > 0.3f ? 1 : f < -0.3f ? -1 : 0;
        pieces.push_back(std::move(p));
      }
      continue;
    }
    if (!blendPinned(a) || !T.inBuildArea(i.pos.x, i.pos.z)) continue;   // decor beyond the border keeps the old ground
    const json ew = a->raw.value("earthwork", json());
    TerrainStamp s; s.kind = TerrainStamp::PAD; s.owner = i.id; s.members = {i.id};
    s.c = vec2(i.pos.x, i.pos.z); s.yaw = i.yaw; s.y = i.pos.y; s.half = padHalf(a); s.seed = idSeed(i.id);
    float dmax = 0;
    for (vec2 l : footprintSamples(s.half, false)) { vec2 w = s.c + toWorld(l, s.yaw); dmax = std::max(dmax, std::fabs(T.groundBase(w.x, w.y) - s.y)); }
    if (dmax >= C.minDelta || ew.is_object()) {   // ground already within minDelta of the floor: nothing to level
      s.ring = std::clamp(C.ringMin + C.ringPerMetre * dmax, C.ringMin, C.ringMax);
      s.skirt = ew.is_object() ? 0.0f : C.skirtThreshold;
      s.grow = ew.is_object() ? 0.0f : T.fineSpacing();
      float r = glm::length(s.half) + s.grow + s.ring + 0.5f;
      s.bmin = s.c - vec2(r); s.bmax = s.c + vec2(r);
      out.push_back(s);
    }
    if (ew.is_object() && ew.contains("banks")) {   // the bunker's earth cover as terrain
      TerrainStamp b; b.kind = TerrainStamp::BERM; b.owner = i.id; b.members = {i.id};
      b.c = s.c; b.yaw = s.yaw; b.y = s.y; b.seed = s.seed;
      b.half = ew.contains("core") ? vec2(ew["core"][0].get<float>(), ew["core"][1].get<float>()) : a->footprint * 0.25f;
      float reach = 0;
      for (auto& k : ew["banks"]) {
        if (k.size() < 5) continue;
        b.banks.push_back(vec4(k[0].get<float>(), k[1].get<float>(), k[2].get<float>(), k[3].get<float>()));
        float Ha = k[4].get<float>(), Hb = k.size() > 5 ? k[5].get<float>() : Ha, H = std::max(Ha, Hb);   // [ax, az, bx, bz, crest at a(, crest at b)]
        b.bankH.push_back(vec2(Ha, Hb));
        reach = std::max(reach, std::max(glm::length(vec2(k[0].get<float>(), k[1].get<float>())), glm::length(vec2(k[2].get<float>(), k[3].get<float>()))) +
                                    sp.crest + 1.3f * H * (1 + sp.noise) * 1.15f / tanR + 1.0f);
      }
      for (auto& h : ew.value("holes", json::array())) if (h.size() >= 4) b.holes.push_back(vec4(h[0].get<float>(), h[1].get<float>(), h[2].get<float>(), h[3].get<float>()));
      float r = reach + sp.smoothing + sp.edgeJitter + 1.0f;
      b.bmin = b.c - vec2(r); b.bmax = b.c + vec2(r);
      out.push_back(std::move(b));
    }
  }
  // trench networks: pieces whose path ends meet (their joined sockets) form one network and one stamp
  const size_t n = pieces.size();
  std::vector<size_t> root(n); std::iota(root.begin(), root.end(), 0);
  auto find = [&](size_t x) { while (root[x] != x) x = root[x] = root[root[x]]; return x; };
  auto near2 = [](vec3 a, vec3 b) { return glm::length(vec2(a.x - b.x, a.z - b.z)) < 0.3f; };
  for (size_t a = 0; a < n; ++a)
    for (size_t b = a + 1; b < n; ++b) {
      const auto& A = pieces[a].path; const auto& B = pieces[b].path;
      if (near2(A.front(), B.front()) || near2(A.front(), B.back()) || near2(A.back(), B.front()) || near2(A.back(), B.back())) root[find(a)] = find(b);
    }
  std::map<size_t, std::vector<size_t>> nets;
  for (size_t k = 0; k < n; ++k) nets[find(k)].push_back(k);
  for (auto& [rootIdx, members] : nets) {
    (void)rootIdx;
    TerrainStamp t; t.kind = TerrainStamp::TRENCH;
    t.owner = ~0ull;
    std::vector<vec3> ends;   // every piece end, for joins and shared floor heights
    for (size_t k : members) { t.owner = std::min(t.owner, pieces[k].id); t.members.push_back(pieces[k].id); ends.push_back(pieces[k].path.front()); ends.push_back(pieces[k].path.back()); }
    std::sort(t.members.begin(), t.members.end());
    t.seed = idSeed(t.owner);
    auto shared = [&](vec3 p, float& y) {   // the mean floor of every piece end at p; true when more than one meets there
      float s = 0; int c = 0;
      for (auto& e : ends) if (near2(e, p)) { s += e.y; ++c; }
      if (c) y = s / c;
      return c > 1;
    };
    vec2 lo(1e30f), hi(-1e30f);
    auto add = [&](vec3 a, vec3 b, bool j0, bool j1, int8_t fr) {
      t.seg.push_back(vec4(a.x, a.z, b.x, b.z)); t.segY.push_back(vec2(a.y, b.y)); t.segJoin.push_back((uint8_t)((j0 ? 1 : 0) | (j1 ? 2 : 0))); t.segFront.push_back(fr);
      lo = glm::min(lo, glm::min(vec2(a.x, a.z), vec2(b.x, b.z))); hi = glm::max(hi, glm::max(vec2(a.x, a.z), vec2(b.x, b.z)));
    };
    for (size_t k : members) {
      auto P = pieces[k].path;
      bool j0 = shared(P.front(), P.front().y), j1 = shared(P.back(), P.back().y);
      for (size_t s = 0; s + 1 < P.size(); ++s) add(P[s], P[s + 1], s > 0 || j0, s + 2 < P.size() || j1, pieces[k].front);
      for (auto& br : pieces[k].branches) for (size_t s = 0; s + 1 < br.size(); ++s) add(br[s], br[s + 1], true, s + 2 < br.size(), 0);
    }
    float r = sp.floorHalf + 0.12f + sp.parapetW * 1.3f + 1.5f;
    t.bmin = lo - vec2(r); t.bmax = hi + vec2(r);
    out.push_back(std::move(t));
  }
  int touched = T.setStamps(std::move(out));
  if (touched) logf("gx1: %zu terrain stamps, %d tiles re-based", T.stamps.size(), touched);
  return touched;
}

// ================================================================ placement
bool blendPlacement(const App& app, const AssetDef* a, vec3 pos, float yaw, uint64_t ignoreId, float& y, std::string& reason) {
  const BlendConfig& C = blendConfig();
  vec2 c(pos.x, pos.z), half = padHalf(a);
  std::vector<float> hs;
  for (vec2 l : footprintSamples(half, false)) { vec2 w = c + toWorld(l, yaw); hs.push_back(app.terrain.placementHeight(w.x, w.y, ignoreId)); }
  float m = median(hs), dev = 0;
  for (float h : hs) dev = std::max(dev, std::fabs(h - m));
  if (dev > C.margin) {
    char b[200]; std::snprintf(b, sizeof b, "Ground varies %.2f m from the level under the footprint (auto-levelling allows %.1f m).", dev, C.margin);
    reason = b; return false;
  }
  y = m;
  return true;
}

// ================================================================ generated meshes
namespace {
Part boxPart(vec3 c, vec3 s, quat q, Part::Shape sh = Part::BOX, float rad = 0) { Part p; p.shape = sh; p.center = c; p.size = s; p.rot = q; p.rad = rad; return p; }
quat yawPitch(float yawRad, float tiltRad) { return glm::angleAxis(yawRad, vec3(0, 1, 0)) * glm::angleAxis(tiltRad, vec3(1, 0, 0)); }

// revetment, pickets and sandbags along one trench piece's path, fitted to the carved network around it
void buildTrench(App& app, const Instance& i, const AssetDef* a, MeshData& m) {
  const Terrain& T = app.terrain;
  const BlendConfig& C = blendConfig(); const StampParams& sp = T.sp;
  const TerrainStamp* st = stampOf(T, i.id, TerrainStamp::TRENCH);
  if (!st) return;
  quat iq = glm::inverse(yawQuat(i.yaw));
  auto L = [&](vec3 w) { return iq * (w - i.pos); };   // world -> instance space
  const Material& wattle = matNamed(a, "wattle"); const Material& picket = matNamed(a, "picket"); const Material& bag = matNamed(a, "sacking");
  const Material* clod[3] = {&matNamed(a, "spoil"), &matNamed(a, "earth"), &matNamed(a, "turf")};
  const float thk = sp.carvePad + 0.06f;   // the panel fills the carve pad behind the revetment line: no slot between revetment and soil
  std::vector<vec2> P; std::vector<float> D;
  for (auto& q : a->raw["trench"].value("path", json::array())) { P.push_back(vec2(i.pos.x, i.pos.z) + toWorld(vec2(q[0].get<float>(), q[1].get<float>()), i.yaw)); D.push_back(q.size() > 2 ? q[2].get<float>() : 1.0f); }
  if (P.size() < 2) return;
  const float plane = sp.floorHalf + 0.02f, tilt = std::atan(sp.batter);
  uint32_t seed = idSeed(i.id);
  auto lipAt = [&](vec2 q, vec2 n) {   // the soil the panel meets: the highest uncarved ground just past its back
    float y = -1e30f;
    for (float d : {thk + 0.05f, thk + 0.3f}) { vec2 w = q + n * d; y = std::max(y, T.trenchLipY(*st, w.x, w.y)); }
    return y;
  };
  auto floorAt = [&](vec2 q) { return T.height(q.x, q.y); };
  for (size_t s = 0; s + 1 < P.size(); ++s) {
    vec2 A = P[s], B = P[s + 1], dir = glm::normalize(B - A);
    vec2 nrm(-dir.y, dir.x);
    float len = glm::length(B - A), yawR = std::atan2(dir.x, dir.y) - 1.5707963f;   // local +x of the boxes along dir
    if (std::min(D[s], D[s + 1]) < 0.25f && std::max(D[s], D[s + 1]) < 0.25f) continue;
    for (int side = -1; side <= 1; side += 2) {
      vec2 n = nrm * (float)side;
      // mitred ends: shorten / lengthen this side at interior path vertices so the walls of two legs meet
      float t0 = 0, t1 = len;
      if (s > 0) { vec2 pd = glm::normalize(P[s] - P[s - 1]); float cr = pd.x * dir.y - pd.y * dir.x, dt = glm::dot(pd, dir); t0 = plane * side * cr / std::max(0.2f, 1.0f + dt); }
      if (s + 2 < P.size()) { vec2 nd = glm::normalize(P[s + 2] - P[s + 1]); float cr = dir.x * nd.y - dir.y * nd.x, dt = glm::dot(dir, nd); t1 = len - plane * side * cr / std::max(0.2f, 1.0f + dt); }
      int steps = std::max(1, (int)std::ceil((t1 - t0) / 0.5f));
      float step = (t1 - t0) / steps;
      for (int k = 0; k < steps; ++k) {
        float tm = t0 + (k + 0.5f) * step;
        vec2 c = A + dir * tm, q = c + n * plane;
        float fy = floorAt(c), lip = lipAt(q, n);
        float behind = T.height(q.x + n.x * (thk + 0.35f), q.y + n.y * (thk + 0.35f));
        if (behind < fy + 0.6f || lip < fy + 0.45f) continue;   // open behind: a junction, the niche, the ramp mouth
        if (C.revetment) {
          float cap = lip + 0.02f + 0.03f * hash01(seed, (uint32_t)(s * 97 + k * 2 + (side > 0)));
          float y0 = fy - 0.12f, y1 = cap - 0.08f;
          float H = y1 - y0;
          vec2 cc = q + n * (thk * 0.5f + sp.batter * H * 0.5f);
          vec3 w(cc.x, (y0 + y1) * 0.5f, cc.y);
          quat qw = yawPitch(yawR, side > 0 ? tilt : -tilt);   // the wall leans back into the soil
          appendPart(m, boxPart(L(w), vec3(step + 0.03f, H, thk), iq * qw), wattle, mat4(1));
          // heaped clods over the panel's top, flush with the soil behind it (spoil, dark earth, the odd turf)
          for (int cl = 0; cl < 2; ++cl) {
            uint32_t hs = (uint32_t)(s * 7919 + k * 31 + cl * 7 + (side > 0) * 3);
            float r0 = hash01(seed, hs), r1 = hash01(seed, hs + 1), r2 = hash01(seed, hs + 2);
            vec2 sc = q + dir * ((cl - 0.5f) * step * 0.5f + (r0 - 0.5f) * 0.1f) + n * ((thk + 0.1f) * 0.5f + sp.batter * H + (r1 - 0.5f) * 0.08f);
            vec3 sz(step * (0.55f + 0.35f * r1), 0.12f + 0.08f * r2, thk * (0.75f + 0.4f * r0));
            Part c = boxPart(L(vec3(sc.x, cap - sz.y * 0.45f, sc.y)), sz, iq * glm::angleAxis(yawR + (r2 - 0.5f) * 0.6f, vec3(0, 1, 0)) * glm::angleAxis((r0 - 0.5f) * 0.25f, vec3(1, 0, 0)));
            c.bv = 0.045f;
            appendPart(m, c, *clod[r2 < 0.12f ? 2 : r1 < 0.45f ? 1 : 0], mat4(1));
          }
          vec2 rc = q + n * (0.03f + sp.batter * H);
          Part rail = boxPart(L(vec3(rc.x, y1 + 0.01f, rc.y)), vec3(0.08f, step + 0.04f, 0.08f), iq * glm::angleAxis(yawR, vec3(0, 1, 0)) * glm::angleAxis(1.5707963f, vec3(0, 0, 1)), Part::CYL);
          rail.seg = 8; appendPart(m, rail, picket, mat4(1));
        }
        // pickets driven into the floor, every C.pickets metres
        float pk = std::max(0.3f, C.pickets);
        float a0 = std::floor((tm - step * 0.5f) / pk), a1 = std::floor((tm + step * 0.5f) / pk);
        if (C.revetment && a1 > a0) {
          float H = lip - 0.02f + 0.06f * hash01(seed, (uint32_t)(s * 131 + k)) - (fy - 0.35f);
          vec2 pc = q - n * 0.01f + n * (sp.batter * H * 0.5f);
          Part pp = boxPart(L(vec3(pc.x, fy - 0.35f + H * 0.5f, pc.y)), vec3(0.08f, H, 0.08f), iq * yawPitch(yawR, side > 0 ? tilt : -tilt), Part::CYL);
          pp.seg = 8; appendPart(m, pp, picket, mat4(1));
        }
        // sandbags resting on the parapet: two courses on the fire side (local +z), one sparse course behind
        if (C.sandbags) {
          // the fire side stays clear (low parapet, the firestep sees over it); the parados gets two courses, with gaps
          vec2 lz0 = toWorld(vec2(0, 1), i.yaw), d0 = glm::normalize(P[1] - P[0]);
          float f0 = glm::dot(vec2(-d0.y, d0.x), lz0);
          int frontSide = f0 > 0.3f ? 1 : f0 < -0.3f ? -1 : 0;
          bool front = frontSide != 0 && side == frontSide;
          float gap = terrainFbm((c.x + n.x) * 0.35f, (c.y + n.y) * 0.35f, seed + 21);
          if (!front && gap > -0.35f) {
            int courses = 2;
            vec2 bc = q + n * (thk + 0.3f + 0.1f * hash01(seed, (uint32_t)(s * 31 + k * 7 + side)));
            float by = 1e30f;
            for (float dl : {-0.24f, 0.0f, 0.24f}) for (float dn : {-0.14f, 0.14f}) { vec2 w = bc + dir * dl + n * dn; by = std::min(by, T.height(w.x, w.y)); }
            for (int cs = 0; cs < courses; ++cs) {
              float jy = (hash01(seed, (uint32_t)(s * 53 + k * 5 + cs * 3 + side)) - 0.5f) * 0.16f;
              vec2 o = bc + dir * (cs ? 0.25f : 0.0f) - n * (0.04f * cs);
              Part sb = boxPart(L(vec3(o.x, by + 0.055f + cs * 0.145f, o.y)), vec3(0.5f, 0.16f, 0.3f), iq * glm::angleAxis(yawR + jy, vec3(0, 1, 0)), Part::ROUND, 0.065f);
              appendPart(m, sb, bag, mat4(1));
            }
          }
        }
      }
    }
  }
  // end walls where the network stops (not the ramp's shallow mouth)
  for (int e = 0; e < 2; ++e) {
    vec2 p = e ? P.back() : P.front(), q = e ? P[P.size() - 2] : P[1];
    float depth = e ? D.back() : D.front();
    if (depth < 0.4f) continue;
    bool joined = false;
    for (size_t k = 0; k < st->seg.size(); ++k) {
      vec4 sg = st->seg[k];
      for (vec2 x : {vec2(sg.x, sg.y), vec2(sg.z, sg.w)}) if (glm::length(x - p) < 0.3f && glm::length(x - q) > 0.3f) {
        // another segment ends here: joined unless it is this piece's own segment
        vec2 o = glm::length(x - vec2(sg.x, sg.y)) < 0.3f ? vec2(sg.z, sg.w) : vec2(sg.x, sg.y);
        if (glm::length(o - q) > 0.3f) joined = true;
      }
    }
    if (joined || !C.revetment) continue;
    vec2 dir = glm::normalize(p - q), n = dir;
    vec2 pe = p + n * 0.03f;   // the asset's end wall line (its invisible collider stays there)
    float fy = T.height(p.x - dir.x * 0.3f, p.y - dir.y * 0.3f), cap = lipAt(pe, n) + 0.02f, H = cap - 0.08f - (fy - 0.12f);
    vec2 cc = pe + n * (thk * 0.5f + sp.batter * H * 0.5f);
    float yawR = std::atan2(n.x, n.y);
    appendPart(m, boxPart(L(vec3(cc.x, fy - 0.12f + H * 0.5f, cc.y)), vec3(2 * plane + 0.1f, H, thk), iq * yawPitch(yawR, tilt)), wattle, mat4(1));
    for (int cl = -1; cl <= 1; ++cl) {
      float r0 = hash01(seed, 9001 + cl + e * 5), r1 = hash01(seed, 9101 + cl + e * 5);
      vec2 sc = pe + vec2(n.y, -n.x) * (cl * plane * 0.66f) + n * ((thk + 0.1f) * 0.5f + sp.batter * H);
      Part c = boxPart(L(vec3(sc.x, cap - 0.07f, sc.y)), vec3(plane * 0.8f, 0.14f + 0.06f * r0, thk * (0.8f + 0.3f * r1)), iq * glm::angleAxis(yawR + (r1 - 0.5f) * 0.5f, vec3(0, 1, 0)));
      c.bv = 0.045f;
      appendPart(m, c, *clod[r0 < 0.4f ? 1 : 0], mat4(1));
    }
  }
}

// stone retaining skirt along the footprint edges where the pad stands higher than the cap allows
void buildSkirt(App& app, const Instance& i, const AssetDef* a, MeshData& m) {
  const Terrain& T = app.terrain;
  const TerrainStamp* s = stampOf(T, i.id, TerrainStamp::PAD);
  if (!s || s->skirt <= 0) return;
  quat iq = glm::inverse(yawQuat(i.yaw));
  const Material& stone = matNamed(a, "stone_plinth");
  const float embed = blendConfig().sp.skirtEmbed;
  for (int e = 0; e < 4; ++e) {
    vec2 h = s->half + vec2(s->grow);   // along the edge of the levelled region
    vec2 a0 = e == 0 ? vec2(-h.x, -h.y) : e == 1 ? vec2(h.x, -h.y) : e == 2 ? vec2(h.x, h.y) : vec2(-h.x, h.y);
    vec2 a1 = e == 0 ? vec2(h.x, -h.y) : e == 1 ? vec2(h.x, h.y) : e == 2 ? vec2(-h.x, h.y) : vec2(-h.x, -h.y);
    vec2 dl = glm::normalize(a1 - a0), nl(dl.y, -dl.x);   // outward
    float len = glm::length(a1 - a0);
    int steps = std::max(1, (int)std::ceil(len / 0.5f));
    for (int k = 0; k < steps; ++k) {
      vec2 lm = a0 + dl * ((k + 0.5f) * len / steps);
      vec2 wo = s->c + toWorld(lm + nl * 0.06f, s->yaw);
      float g = T.height(wo.x, wo.y), gap = s->y - g;
      if (gap < 0.05f) continue;
      float y0 = g - embed, y1 = s->y + 0.04f;
      vec2 lc = lm - nl * 0.16f;   // outer face 2 cm proud of the level region's edge
      vec3 c(lc.x, 0, lc.y); c.y = (y0 + y1) * 0.5f - i.pos.y;
      float yawL = std::atan2(dl.x, dl.y) - 1.5707963f;
      appendPart(m, boxPart(c, vec3(len / steps + 0.02f, y1 - y0, 0.36f), glm::angleAxis(yawL, vec3(0, 1, 0))), stone, mat4(1));
    }
  }
  (void)iq;
}
}   // namespace

void blendDrawItems(App& app, const Instance& i, InstanceRT& r, const mat4& M, std::vector<DrawItem>& op) {
  if (!blendEnabled() || !r.asset) return;
  bool tr = blendTrench(r.asset), pin = blendPinned(r.asset);
  if (!tr && !pin) return;
  const Terrain& T = app.terrain;
  uint64_t key = ((uint64_t)i.rev << 40) ^ ((uint64_t)T.cutRev << 20) ^ (uint64_t)T.sculptRev ^ 0x5bd1e995ull;
  bool stale = r.earthKey != key;
  if (!stale && r.earthGround != app.groundRev) {   // craters: only those that reach this piece
    float reach = glm::length(r.asset->footprint) * 0.5f + 3.0f;
    if (app.groundChangedNear(r.earthGround, vec2(i.pos.x, i.pos.z), reach)) stale = true; else r.earthGround = app.groundRev;
  }
  if (stale) {
    MeshData m;
    if (tr) buildTrench(app, i, r.asset, m); else buildSkirt(app, i, r.asset, m);
    r.earthMesh.destroy();
    if (!m.i.empty()) r.earthMesh.upload(m);
    r.earthKey = key; r.earthGround = app.groundRev;
  }
  if (r.earthMesh.count) op.push_back({&r.earthMesh, M});
}

// ================================================================ preview
void blendPreview(const App& app, Overlay& ov, const AssetDef* a, vec3 pos, float yaw) {
  if (!blendPinned(a)) return;
  const BlendConfig& C = blendConfig();
  const Terrain& T = app.terrain;
  vec2 c(pos.x, pos.z), half = padHalf(a);
  float dmax = 0, up = 0, down = 0;
  for (vec2 l : footprintSamples(half, false)) { vec2 w = c + toWorld(l, yaw); float d = pos.y - T.height(w.x, w.y); dmax = std::max(dmax, std::fabs(d)); up = std::max(up, d); down = std::max(down, -d); }
  if (dmax < C.minDelta) return;
  float ring = std::clamp(C.ringMin + C.ringPerMetre * dmax, C.ringMin, C.ringMax);
  vec4 padC = up >= down ? vec4(0.95f, 0.8f, 0.3f, 1) : vec4(0.35f, 0.7f, 1.0f, 1);   // raise: amber, cut: blue
  auto rect = [&](vec2 h, float grow, bool onPad, vec4 col) {
    vec2 cs[4] = {{-h.x, -h.y}, {h.x, -h.y}, {h.x, h.y}, {-h.x, h.y}};
    for (int e = 0; e < 4; ++e) {
      vec2 a0 = cs[e], a1 = cs[(e + 1) % 4];
      int nseg = std::max(2, (int)(glm::length(a1 - a0) / 1.0f));
      vec3 prev;
      for (int k = 0; k <= nseg; ++k) {
        vec2 l = glm::mix(a0, a1, (float)k / nseg);
        vec2 dir = glm::normalize(l); l += glm::sign(l) * grow; (void)dir;
        vec2 w = c + toWorld(l, yaw);
        vec3 p(w.x, onPad ? pos.y + 0.05f : T.height(w.x, w.y) + 0.08f, w.y);
        if (k) ov.line(prev, p, col, true);
        prev = p;
      }
    }
  };
  rect(half, 0.0f, true, padC);                                 // the levelled pad
  rect(half, ring, false, vec4(vec3(padC) * 0.8f, 0.7f));       // where the blend ring ends on the ground
  if (C.skirtThreshold > 0 && up > C.skirtThreshold && !a->raw.contains("earthwork")) rect(half, 0.02f, false, vec4(0.85f, 0.85f, 0.8f, 0.9f));   // skirt line
}

// ================================================================ validation
BlendIssues blendValidate(App& app, bool fix, const char* why) {
  BlendIssues r;
  if (!blendEnabled()) return r;
  const Terrain& T = app.terrain;
  std::vector<const Instance*> hosts;
  for (auto& i : app.doc.instances) { const AssetDef* a = app.assetFor(i); if (a && (blendPinned(a) || blendTrench(a) || a->enterable)) hosts.push_back(&i); }
  auto onHost = [&](const Instance& p) {
    for (auto* h : hosts) {
      if (h->id == p.id) continue;
      const AssetDef* ha = app.assetFor(*h);
      vec3 l = glm::inverse(yawQuat(h->yaw)) * (p.pos - h->pos);
      if (std::fabs(l.x) <= ha->footprint.x * 0.5f + 0.2f && std::fabs(l.z) <= ha->footprint.y * 0.5f + 0.2f) return true;
    }
    return false;
  };
  for (auto& i : app.doc.instances) {
    const AssetDef* a = app.assetFor(i);
    if (!a) continue;
    if (blendPinned(a)) {
      ++r.pinned;
      const TerrainStamp* s = stampOf(T, i.id, TerrainStamp::PAD);
      vec2 h = padHalf(a) - vec2(0.15f);
      for (vec2 l : {vec2(-h.x, -h.y), vec2(h.x, -h.y), vec2(h.x, h.y), vec2(-h.x, h.y), vec2(0, -h.y), vec2(0, h.y), vec2(-h.x, 0), vec2(h.x, 0)}) {
        vec2 w = vec2(i.pos.x, i.pos.z) + toWorld(l, i.yaw);
        float d = i.pos.y - T.height(w.x, w.y);   // > 0: the base floats over the ground
        if (d > 0.05f && !(s && s->skirt > 0)) { ++r.floating; r.worst = std::max(r.worst, d); logf("gx1 validate (%s): %s #%llu floats %.2f m at a base corner", why, a->id.c_str(), (unsigned long long)i.id, d); break; }
        if (d < -0.10f) { ++r.buried; r.worst = std::max(r.worst, -d); logf("gx1 validate (%s): %s #%llu buried %.2f m at a base corner", why, a->id.c_str(), (unsigned long long)i.id, -d); break; }
      }
      continue;
    }
    // loose props on open ground: crates, fences, dressing (not furniture, growth, guns, vehicles or anything on a host)
    if (blendTrench(a) || a->path.on || a->tree.on || a->nature.kind != NatureDef::NONE || a->category == "Nature" || a->category == "Vegetation" ||
        !a->station.kind.empty() || isVehicleAsset(a) || app.interior.isProp(a) || !a->defense.kind.empty() || a->footprint.x * a->footprint.y >= 6.0f) continue;
    if (onHost(i)) continue;
    float g = T.placementHeight(i.pos.x, i.pos.z, i.id), d = i.pos.y - g;
    if (d > 0.05f || d < -0.10f) {
      (d > 0 ? r.floating : r.buried)++;
      r.worst = std::max(r.worst, std::fabs(d));
      if (fix) { i.pos.y = g; app.bump(i); ++r.fixed; }
    }
  }
  if (r.floating || r.buried) logf("gx1 validate (%s): %d pinned, %d floating, %d buried (worst %.2f m), %d re-seated", why, r.pinned, r.floating, r.buried, r.worst, r.fixed);
  return r;
}
