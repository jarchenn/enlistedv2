// GX1 terrain conformance (terrain_blend.h; docs/graphics-overhaul.md#gx1-terrain-conformance). The stamps are
// evaluated in terrain_cuts.cpp; this file derives them from the instances, levels placements, generates the
// revetment / skirt meshes and validates seating after reforms.
#include "terrain_blend.h"
#include "lines.h"
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
// the walls' centre in asset space: an earthwork core may sit off the asset origin ("core": [hx, hz, cx, cz])
vec2 padOff(const AssetDef* a) {
  const json& ew = a->raw.is_object() && a->raw.contains("earthwork") ? a->raw["earthwork"] : json();
  if (ew.is_object() && ew.contains("core") && ew["core"].size() >= 4) return vec2(ew["core"][2].get<float>(), ew["core"][3].get<float>());
  return vec2(0);
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

static bool lineTrench(const AssetDef* a) { const LineType* t = lineTypeOf(a); return t && t->builder == "trench"; }
bool blendTrench(const AssetDef* a) { return a && a->raw.is_object() && (a->raw.contains("trench") || lineTrench(a)); }
namespace {
// LN1 trench lines: each line's centreline as GX1 carves and dresses it (world xz, depth scale after the entry ramps),
// and its fire side; filled by blendSyncStamps, read by buildTrench
struct LinePath { std::vector<vec2> P; std::vector<float> D; int front = 0; };
std::map<uint64_t, LinePath> gLinePaths;
float segDist2(vec2 p, vec2 a, vec2 b) { vec2 ab = b - a; float L2 = glm::dot(ab, ab); float t = L2 > 1e-9f ? std::clamp(glm::dot(p - a, ab) / L2, 0.0f, 1.0f) : 0.0f; return glm::length(p - (a + ab * t)); }
bool segCross(vec2 a, vec2 b, vec2 c, vec2 d) {
  auto cr = [](vec2 u, vec2 v) { return u.x * v.y - u.y * v.x; };
  float d1 = cr(b - a, c - a), d2 = cr(b - a, d - a), d3 = cr(d - c, a - c), d4 = cr(d - c, b - c);
  return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0));
}
}
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
    if (top <= r.value("aboveY", -1.0f)) {   // the firestep and floor below stay; "floorDrop": the generated duckboards replace these
      const json& fd = asset["trench"].value("floorDrop", json::object());
      auto fm = fd.value("materials", json::array());
      if (std::find(fm.begin(), fm.end(), m) != fm.end() && top <= fd.value("topBelow", -1.45f)) return 1;
      const json& sd = asset["trench"].value("stepDrop", json::object());   // the firestep's full-width top plank: earth covers it
      auto sm = sd.value("materials", json::array());
      bool wide = pj.contains("s") && pj["s"][2].get<float>() >= sd.value("minWidth", 0.3f) && pj["s"][1].get<float>() <= 0.08f;
      return std::find(sm.begin(), sm.end(), m) != sm.end() && wide && top >= sd.value("topAbove", -1.3f) ? 1 : 0;
    }
    // kept as invisible colliders: the revetment and the earth fill behind it (the mantle ledge and the wall players
    // meet), not the tilted parapet slabs (the terrain parapet replaces those)
    auto keep = r.value("keepCollision", json::array());
    bool tilted = pj.contains("r") && (std::fabs(pj["r"][0].get<float>()) > 1.0f || std::fabs(pj["r"][2].get<float>()) > 1.0f);
    return std::find(keep.begin(), keep.end(), m) != keep.end() && pj.value("k", 1) != 0 && !tilted ? 2 : 1;
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
  struct Piece { uint64_t id; std::vector<vec3> path; std::vector<std::vector<vec3>> branches; int8_t front = 0; bool line = false; std::string ramps; bool mouth[2] = {false, false}; std::vector<float> u, off; };   // lines: each sample's control-point parameter; each control point's fixed-floor offset   // world x, floor y, z; front: the fire side (+1 left of the path)
  std::vector<Piece> pieces;
  for (auto& i : app.doc.instances) {
    const AssetDef* a = app.assetFor(i);
    if (!a) continue;
    if (lineTrench(a)) {   // LN1: a trench line's spline (2 m samples), its floor the ground less the depth; ramps after the joins
      LineData d; if (!lineGet(i, d)) continue;
      json prm = lineParams(*lineTypeOf(a), d);
      Piece p; p.id = i.id; p.line = true;
      float ds = std::clamp(prm.value("depth", sp.depth), 0.5f, 3.0f) / sp.depth;   // the player's depth for this line
      for (auto& q : lineSpline(d, 2.0f, prm.value("sharp", false))) { p.path.push_back(vec3(q.p.x, q.d * ds, q.p.y)); p.u.push_back(q.u); }   // y: the depth scale for now
      for (auto& c : d.pts) p.off.push_back(std::isnan(c.y) ? 0.0f : c.y - (T.groundBase(c.p.x, c.p.y) - sp.depth * c.d * ds));   // a fixed floor, eased out over the span
      std::string fs = prm.value("fire", std::string("left"));
      p.front = fs == "left" ? 1 : fs == "right" ? -1 : 0;
      p.ramps = prm.value("ramps", std::string("both"));
      if (p.path.size() >= 2) pieces.push_back(std::move(p));
      continue;
    }
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
    s.c = vec2(i.pos.x, i.pos.z) + toWorld(padOff(a), i.yaw); s.yaw = i.yaw; s.y = i.pos.y; s.half = padHalf(a); s.seed = idSeed(i.id);
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
      const vec2 o = padOff(a);   // banks, holes and cuts are given in asset space: make them core-relative
      float reach = 0;
      for (auto& k : ew["banks"]) {
        if (k.size() < 5) continue;
        b.banks.push_back(vec4(k[0].get<float>() - o.x, k[1].get<float>() - o.y, k[2].get<float>() - o.x, k[3].get<float>() - o.y));
        float Ha = k[4].get<float>(), Hb = k.size() > 5 ? k[5].get<float>() : Ha, H = std::max(Ha, Hb);   // [ax, az, bx, bz, crest at a(, crest at b)]
        b.bankH.push_back(vec2(Ha, Hb));
        reach = std::max(reach, std::max(glm::length(vec2(k[0].get<float>(), k[1].get<float>()) - o), glm::length(vec2(k[2].get<float>(), k[3].get<float>()) - o)) +
                                    sp.crest + 1.3f * H * (1 + sp.noise) * 1.15f / tanR + 1.0f);
      }
      for (auto& h : ew.value("holes", json::array())) if (h.size() >= 4) {   // [x0, z0, x1, z1(, fade width)]
        b.holes.push_back(vec4(h[0].get<float>() - o.x, h[1].get<float>() - o.y, h[2].get<float>() - o.x, h[3].get<float>() - o.y));
        b.holeEdge.push_back(h.size() > 4 ? h[4].get<float>() : 0.0f);
      }
      for (auto& c : ew.value("cuts", json::array())) if (c.size() >= 7) {   // [ax, az, bx, bz, half width, wall height at a, at b]
        b.cuts.push_back(vec4(c[0].get<float>() - o.x, c[1].get<float>() - o.y, c[2].get<float>() - o.x, c[3].get<float>() - o.y));
        b.cutP.push_back(vec3(c[4].get<float>(), c[5].get<float>(), c[6].get<float>()));
        reach = std::max(reach, glm::length(vec2(c[2].get<float>(), c[3].get<float>()) - o) + c[4].get<float>() + 12.0f);   // the ramp past the open end
      }
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
  auto isArm = [](const std::vector<vec3>& br) { return br.size() >= 2 && glm::length(vec2(br.back().x - br.front().x, br.back().z - br.front().z)) >= 3.0f; };
  std::vector<std::vector<vec3>> pend(n);   // every open end: the path's two, and each junction arm's outer end
  for (size_t k = 0; k < n; ++k) {
    pend[k] = {pieces[k].path.front(), pieces[k].path.back()};
    for (auto& br : pieces[k].branches) if (isArm(br)) pend[k].push_back(br.back());
  }
  // LN1 lines also join where an end meets another trench's centreline (a T) or two centrelines cross (an X)
  auto touches = [&](size_t a, size_t b) {
    const auto& A = pieces[a].path; const auto& B = pieces[b].path;
    for (vec3 e : {A.front(), A.back()}) for (size_t k = 0; k + 1 < B.size(); ++k) if (segDist2(vec2(e.x, e.z), vec2(B[k].x, B[k].z), vec2(B[k + 1].x, B[k + 1].z)) < sp.floorHalf + 0.6f) return true;
    for (size_t j = 0; j + 1 < A.size(); ++j) for (size_t k = 0; k + 1 < B.size(); ++k) if (segCross(vec2(A[j].x, A[j].z), vec2(A[j + 1].x, A[j + 1].z), vec2(B[k].x, B[k].z), vec2(B[k + 1].x, B[k + 1].z))) return true;
    return false;
  };
  std::vector<std::array<bool, 2>> endJoined(n, {false, false});   // a line end that meets another trench gets no ramp
  for (size_t a = 0; a < n; ++a)
    for (size_t b = a + 1; b < n; ++b) {
      bool hit = false;
      for (auto& p : pend[a]) for (auto& q : pend[b]) hit = hit || near2(p, q);
      if (!hit && (pieces[a].line || pieces[b].line)) hit = touches(a, b) || touches(b, a);
      if (hit) root[find(a)] = find(b);
    }
  for (size_t a = 0; a < n; ++a) {
    if (!pieces[a].line) continue;
    auto& A = pieces[a].path;
    for (int e = 0; e < 2; ++e) {
      vec2 pe = e ? vec2(A.back().x, A.back().z) : vec2(A.front().x, A.front().z);
      for (size_t b = 0; b < n && !endJoined[a][e]; ++b) {
        if (b == a) continue;
        const auto& B = pieces[b].path;
        for (size_t k = 0; k + 1 < B.size(); ++k) if (segDist2(pe, vec2(B[k].x, B[k].z), vec2(B[k + 1].x, B[k + 1].z)) < sp.floorHalf + 0.6f) { endJoined[a][e] = true; break; }
      }
    }
    // entry ramps at the free ends the line asks for: the depth eases from 0 at the end over 6 m (slope ~0.28: a man walks it); then the floor heights
    float L = 0; std::vector<float> sAt(A.size(), 0);
    for (size_t k = 1; k < A.size(); ++k) { L += glm::length(vec2(A[k].x - A[k - 1].x, A[k].z - A[k - 1].z)); sAt[k] = L; }
    bool r0 = !endJoined[a][0] && (pieces[a].ramps == "both" || pieces[a].ramps == "start"), r1 = !endJoined[a][1] && (pieces[a].ramps == "both" || pieces[a].ramps == "end");
    LinePath lp; lp.front = pieces[a].front;
    for (size_t k = 0; k < A.size(); ++k) {
      float d = A[k].y;
      if (r0) d = std::min(d, sAt[k] / 6.0f);
      if (r1) d = std::min(d, (L - sAt[k]) / 6.0f);
      d = std::clamp(d, 0.0f, 1.0f);
      lp.P.push_back(vec2(A[k].x, A[k].z)); lp.D.push_back(d);
      float off = 0;
      if (!pieces[a].off.empty()) { float u = pieces[a].u[k]; size_t j = std::min((size_t)u, pieces[a].off.size() - 1), j1 = std::min(j + 1, pieces[a].off.size() - 1); off = glm::mix(pieces[a].off[j], pieces[a].off[j1], u - (float)j); }
      A[k].y = T.groundBase(A[k].x, A[k].z) - sp.depth * d + off;
    }
    pieces[a].mouth[0] = r0 || lp.D.front() < 0.05f; pieces[a].mouth[1] = r1 || lp.D.back() < 0.05f;   // any ground-level end is a way in: no spoil across it
    pend[a][0] = A.front(); pend[a][1] = A.back();   // the ends now carry floor heights (they held the depth scale)
    gLinePaths[pieces[a].id] = std::move(lp);
  }
  std::map<size_t, std::vector<size_t>> nets;
  for (size_t k = 0; k < n; ++k) nets[find(k)].push_back(k);
  for (auto& [rootIdx, members] : nets) {
    (void)rootIdx;
    TerrainStamp t; t.kind = TerrainStamp::TRENCH;
    t.owner = ~0ull;
    std::vector<vec3> ends;   // every piece end, for joins and shared floor heights
    for (size_t k : members) { t.owner = std::min(t.owner, pieces[k].id); t.members.push_back(pieces[k].id); ends.insert(ends.end(), pend[k].begin(), pend[k].end()); }
    std::sort(t.members.begin(), t.members.end());
    t.seed = idSeed(t.owner);
    auto shared = [&](vec3 p, float& y) {   // the mean floor of every piece end at p; true when more than one meets there
      float s = 0; int c = 0;
      for (auto& e : ends) if (near2(e, p)) { s += e.y; ++c; }
      if (c) y = s / c;
      return c > 1;
    };
    vec2 lo(1e30f), hi(-1e30f);
    auto add = [&](vec3 a, vec3 b, bool j0, bool j1, int8_t fr, bool m0 = false, bool m1 = false) {
      t.seg.push_back(vec4(a.x, a.z, b.x, b.z)); t.segY.push_back(vec2(a.y, b.y)); t.segJoin.push_back((uint8_t)((j0 ? 1 : 0) | (j1 ? 2 : 0) | (m0 ? 4 : 0) | (m1 ? 8 : 0))); t.segFront.push_back(fr);
      lo = glm::min(lo, glm::min(vec2(a.x, a.z), vec2(b.x, b.z))); hi = glm::max(hi, glm::max(vec2(a.x, a.z), vec2(b.x, b.z)));
    };
    for (size_t k : members) {
      auto P = pieces[k].path;
      bool j0 = shared(P.front(), P.front().y), j1 = shared(P.back(), P.back().y);
      for (size_t s = 0; s + 1 < P.size(); ++s) add(P[s], P[s + 1], s > 0 || j0, s + 2 < P.size() || j1, pieces[k].front, s == 0 && pieces[k].mouth[0], s + 2 == P.size() && pieces[k].mouth[1]);
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
  vec2 c = vec2(pos.x, pos.z) + toWorld(padOff(a), yaw), half = padHalf(a);
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
// the revetment mesh is built this far below its draw position: the material weathering (rising damp and moss at the
// foot of walls, in mesh space) then never paints the dug-in timber, which has no ground line of its own
constexpr float kTrenchMeshDrop = 3.0f;
Part boxPart(vec3 c, vec3 s, quat q, Part::Shape sh = Part::BOX, float rad = 0) { Part p; p.shape = sh; p.center = c; p.size = s; p.rot = q; p.rad = rad; return p; }
quat yawPitch(float yawRad, float tiltRad) { return glm::angleAxis(yawRad, vec3(0, 1, 0)) * glm::angleAxis(tiltRad, vec3(1, 0, 0)); }

// --- vertex weathering for the generated timber (GX5's wear layer is not there yet: per-vertex albedo does it). The mesh
// is built kTrenchMeshDrop below its draw position, so floorL is the trench floor in that dropped mesh space.
struct Wx { float floorL = 0, wetH = 0.65f, mud = 0.6f; vec3 tint{1.0f}; };
void weather(MeshData& m, size_t start, const Wx& w, uint32_t seed) {
  const vec3 mudC(0.115f, 0.088f, 0.062f), wetDark(0.55f, 0.52f, 0.5f);
  for (size_t k = start; k < m.v.size(); ++k) {
    Vertex& v = m.v[k];
    float h = v.pos.y - w.floorL;
    float wet = 1.0f - sstep(0.08f, w.wetH, h);                                  // dark, wet wood low down
    uint32_t hs = (uint32_t)(int32_t)std::floor(v.pos.x * 7.0f) * 73856093u ^ (uint32_t)(int32_t)std::floor(v.pos.z * 7.0f) * 19349663u ^ (uint32_t)(int32_t)std::floor(v.pos.y * 9.0f) * 83492791u;
    float splash = wet * (0.35f + 0.65f * hash01(seed, hs)) * w.mud;           // mud thrown up from the floor, patchy
    vec3 a = v.albedo * w.tint * glm::mix(vec3(1.0f), wetDark, wet);
    v.albedo = glm::mix(a, mudC, std::clamp(splash, 0.0f, 0.85f));
  }
}

// revetment (boards on posts against an earth backfill), a sandbag cap, duckboards and the firestep's earth fill along
// one trench piece's path, fitted to the carved network around it
void buildTrench(App& app, const Instance& i, const AssetDef* a, MeshData& m) {
  const Terrain& T = app.terrain;
  const BlendConfig& C = blendConfig(); const StampParams& sp = T.sp;
  const TerrainStamp* st = stampOf(T, i.id, TerrainStamp::TRENCH);
  if (!st) return;
  quat iq = glm::inverse(yawQuat(i.yaw));
  auto L = [&](vec3 w) { return iq * (w - i.pos) - vec3(0, kTrenchMeshDrop, 0); };   // world -> instance space (dropped)
  const Material& board = matNamed(a, "wattle"); const Material& post = matNamed(a, "log_x"); const Material& bag = matNamed(a, "sacking");
  const Material& spoil = matNamed(a, "spoil");
  Material earth = matNamed(a, "mud"); earth.texCode = 1; earth.pattern = PAT_PLAIN; earth.albedo = vec3(0.09f, 0.072f, 0.055f);   // damp packed earth, as the floor
  const vec3 burlap(0.5f, 0.54f, 0.62f);   // x the sacking albedo: weathered grey-brown burlap
  const float thk = sp.carvePad + 0.06f;   // the backfill fills the carve pad behind the revetment line: soil, never a slot
  std::vector<vec2> P; std::vector<float> D;
  const bool isLine = lineTrench(a);
  if (isLine) { auto it = gLinePaths.find(i.id); if (it == gLinePaths.end()) return; P = it->second.P; D = it->second.D; }
  else for (auto& q : a->raw["trench"].value("path", json::array())) { P.push_back(vec2(i.pos.x, i.pos.z) + toWorld(vec2(q[0].get<float>(), q[1].get<float>()), i.yaw)); D.push_back(q.size() > 2 ? q[2].get<float>() : 1.0f); }
  if (P.size() < 2) return;
  const float plane = sp.floorHalf + 0.02f, tilt = std::atan(sp.batter), boardT = 0.035f;
  uint32_t seed = idSeed(i.id);
  auto R = [&](uint32_t k) { return hash01(seed, k); };
  auto lipAt = [&](vec2 q, vec2 n) {   // the soil the revetment meets: the highest uncarved ground just past its back
    float y = -1e30f;
    for (float d : {thk + 0.05f, thk + 0.3f}) { vec2 w = q + n * d; y = std::max(y, T.trenchLipY(*st, w.x, w.y)); }
    return y;
  };
  auto floorAt = [&](vec2 q) { return T.height(q.x, q.y); };
  auto floorL = [&](float fy) { return fy - i.pos.y - kTrenchMeshDrop; };
  auto put = [&](const Part& p, const Material& mt, float fy, vec3 tint, float mud, uint32_t k, float wetH = 0.65f) {
    size_t s0 = m.v.size(); appendPart(m, p, mt, mat4(1));
    Wx w; w.floorL = floorL(fy); w.tint = tint; w.mud = mud; w.wetH = wetH; weather(m, s0, w, seed ^ (k * 2654435761u));
  };
  vec2 lz0 = toWorld(vec2(0, 1), i.yaw), d0 = glm::normalize(P[1] - P[0]);
  float f0 = glm::dot(vec2(-d0.y, d0.x), lz0);
  const int frontSide = isLine ? gLinePaths[i.id].front : f0 > 0.3f ? 1 : f0 < -0.3f ? -1 : 0;   // the fire side (local +z; a line's "fire"): low parapet, no sandbags on the lip
  uint32_t uid = 0;   // running id for the per-element random numbers
  auto layRun = [&](const std::vector<vec2>& P, const std::vector<float>& D, const int frontSide) {   // the path, then each junction arm
  for (size_t s = 0; s + 1 < P.size(); ++s) {
    vec2 A = P[s], B = P[s + 1], dir = glm::normalize(B - A);
    vec2 nrm(-dir.y, dir.x);
    float len = glm::length(B - A), yawR = std::atan2(dir.x, dir.y) - 1.5707963f;   // local +x of the boxes along dir
    if (std::min(D[s], D[s + 1]) < 0.25f && std::max(D[s], D[s + 1]) < 0.25f) continue;
    for (int side = -1; side <= 1; side += 2) {
      vec2 n = nrm * (float)side;
      quat lean = iq * yawPitch(yawR, side > 0 ? tilt : -tilt);   // the wall leans back into the soil
      // mitred ends: shorten / lengthen this side at interior path vertices so the walls of two legs meet
      float t0 = 0, t1 = len;
      bool c0 = s > 0, c1 = s + 2 < P.size();
      if (c0) { vec2 pd = glm::normalize(P[s] - P[s - 1]); float cr = pd.x * dir.y - pd.y * dir.x, dt = glm::dot(pd, dir); t0 = plane * side * cr / std::max(0.2f, 1.0f + dt); }
      if (c1) { vec2 nd = glm::normalize(P[s + 2] - P[s + 1]); float cr = dir.x * nd.y - dir.y * nd.x, dt = glm::dot(dir, nd); t1 = len - plane * side * cr / std::max(0.2f, 1.0f + dt); }
      int steps = std::max(1, (int)std::ceil((t1 - t0) / 0.5f));
      float step = (t1 - t0) / steps;
      // 0.5 m sub-columns: open behind (a junction, the niche, the ramp mouth) or not, floor and cap heights
      struct Col { bool ok; float fy, cap; };
      std::vector<Col> col(steps);
      for (int k = 0; k < steps; ++k) {
        float tm = t0 + (k + 0.5f) * step;
        vec2 c = A + dir * tm, q = c + n * plane;
        float fy = floorAt(c), lip = lipAt(q, n);
        float behind = T.height(q.x + n.x * (thk + 0.35f), q.y + n.y * (thk + 0.35f));
        bool corner = (k == 0 && c0) || (k == steps - 1 && c1);   // a mitred corner is always walled (the other leg is "behind" it)
        col[k] = {lip >= fy + 0.45f && (corner || behind >= fy + 0.6f), fy, lip + 0.02f + 0.03f * R(++uid)};
      }
      if (!C.revetment) continue;
      auto at = [&](float t, float off) { return A + dir * t + n * off; };
      // earth backfill per sub-column: what shows through split or missing boards, and the soil right behind the lip
      for (int k = 0; k < steps; ++k) {
        if (!col[k].ok) continue;
        float tm = t0 + (k + 0.5f) * step, y0 = col[k].fy - 0.12f, y1 = col[k].cap - 0.05f, H = y1 - y0;
        vec2 cc = at(tm, plane + boardT + thk * 0.5f + sp.batter * H * 0.5f);
        put(boxPart(L(vec3(cc.x, (y0 + y1) * 0.5f, cc.y)), vec3(step + 0.02f, H, thk), lean), earth, col[k].fy, vec3(0.9f + 0.2f * R(++uid)), 0.3f, ++uid);
      }
      // runs of walled sub-columns, cut into bays of about 1.5 m between posts
      for (int k = 0; k < steps;) {
        if (!col[k].ok) { ++k; continue; }
        int e = k; while (e + 1 < steps && col[e + 1].ok && e + 1 - k < 3) ++e;
        float ta = t0 + k * step, tb = t0 + (e + 1) * step;
        if (k == 0 && c0) ta -= 0.12f;            // into the shared corner post: no slit at the mitre
        if (e == steps - 1 && c1) tb += 0.12f;
        float blen = tb - ta, tmid = (ta + tb) * 0.5f;
        float fy = 1e30f, top = 1e30f;
        for (int j = k; j <= e; ++j) { fy = std::min(fy, col[j].fy); top = std::min(top, col[j].cap - 0.06f); }
        // boards: rows from below the floor to the soil line, each with its own tone, offset, tilt; a few split,
        // sagging or missing (the earth behind shows)
        int row = 0;
        for (float y = fy - 0.1f; y < top - 0.03f; ++row) {
          float bh = std::min(0.17f + 0.07f * R(++uid), top - y);
          float rr = R(++uid), tone = 0.5f + 0.36f * R(++uid);
          vec3 tint(tone * (1.0f + 0.08f * (R(++uid) - 0.5f)), tone, tone * (0.92f + 0.1f * R(++uid)));
          float out = boardT * 0.5f + 0.012f * (R(++uid) - 0.5f), yc = y + bh * 0.5f;
          vec2 bc = at(tmid + 0.03f * (R(++uid) - 0.5f), plane + out + sp.batter * (yc - (fy - 0.1f)));
          float roll = (R(++uid) - 0.5f) * 0.03f;   // slight tilt along the board
          if (rr < 0.05f && row > 1) { y += bh; continue; }   // missing
          if (rr < 0.11f) {   // split: two shorter pieces, the outer one sagging
            float cut = blen * (0.35f + 0.3f * R(++uid));
            for (int h = 0; h < 2; ++h) {
              float l = h ? blen - cut - 0.05f : cut, tc = h ? ta + cut + 0.05f + l * 0.5f : ta + l * 0.5f;
              vec2 pc = at(tc, plane + out + sp.batter * (yc - (fy - 0.1f)));
              quat q = lean * glm::angleAxis(roll + (h ? -0.06f : 0.0f), vec3(0, 0, 1));
              put(boxPart(L(vec3(pc.x, yc - (h ? 0.02f : 0.0f), pc.y)), vec3(l, bh - 0.012f, boardT), q), board, fy, tint, 0.7f, ++uid);
            }
          } else {
            float sag = rr < 0.16f ? (R(++uid) - 0.5f) * 0.12f : 0.0f;
            put(boxPart(L(vec3(bc.x, yc, bc.y)), vec3(blen + 0.04f, bh - 0.012f, boardT), lean * glm::angleAxis(roll + sag, vec3(0, 0, 1))), board, fy, tint, 0.7f, ++uid);
          }
          y += bh;
        }
        // posts at both ends of the bay (darker than the boards), driven 0.35 m into the floor
        for (float tp : {ta, tb}) {
          if ((tp == ta && k == 0 && c0) || (tp == tb && e == steps - 1 && c1)) continue;   // the corner post stands there
          float H = top + 0.14f + 0.06f * R(++uid) - (fy - 0.35f), d = 0.1f + 0.03f * R(++uid);
          vec2 pc = at(tp, plane - d * 0.5f + sp.batter * H * 0.5f);
          Part pp = boxPart(L(vec3(pc.x, fy - 0.35f + H * 0.5f, pc.y)), vec3(d, H, d), lean, Part::CYL);
          pp.seg = 8; put(pp, post, fy, vec3(0.55f + 0.15f * R(++uid)), 0.8f, ++uid, 0.8f);
        }
        // the cap: flat weathered burlap sandbags along the top of the backfill, laid in a running bond (one course on
        // the fire side so the firestep sees over it, two behind), sagging a little; the odd soil clod
        bool fireSide = frontSide != 0 && side == frontSide;
        for (int cs = 0; cs < (fireSide ? 1 : 2); ++cs)
          for (float tb2 = ta + 0.3f + (cs ? 0.3f : 0.0f) + 0.06f * (R(++uid) - 0.5f); tb2 < tb + (cs ? -0.1f : 0.1f); tb2 += 0.6f + 0.03f * R(++uid)) {
            float rr = R(++uid), capY = top + 0.06f + cs * 0.14f;
            vec2 sc = at(tb2, plane + boardT + thk * 0.45f + sp.batter * (capY - fy) + 0.04f * cs + (R(++uid) - 0.5f) * 0.05f);
            float tone = 0.8f + 0.3f * R(++uid);
            vec3 hue = burlap * vec3(tone * (1.0f + 0.08f * (R(++uid) - 0.4f)), tone, tone * (0.92f + 0.12f * R(++uid)));
            Part c = boxPart(L(vec3(sc.x, capY + 0.07f, sc.y)), vec3(0.58f + 0.05f * R(++uid), 0.14f + 0.02f * R(++uid), 0.33f + 0.04f * R(++uid)),
                             iq * glm::angleAxis(yawR + (R(++uid) - 0.5f) * 0.12f, vec3(0, 1, 0)) * glm::angleAxis((R(++uid) - 0.5f) * 0.1f, vec3(0, 0, 1)) * glm::angleAxis((R(++uid) - 0.5f) * 0.08f, vec3(1, 0, 0)));
            c.bv = 0.035f;
            if (rr < 0.12f) put(c, spoil, fy, vec3(0.7f + 0.2f * R(++uid)), 0.0f, ++uid);
            else put(c, bag, fy, hue, 0.0f, ++uid);
          }
        k = e + 1;
      }
      // corner posts on both mitre lines: they cover the seam where two legs' boards meet
      if (c0) {
        vec2 pd = glm::normalize(P[s] - P[s - 1]), np(-pd.y * side, pd.x * side);   // the previous leg's wall normal on this side
        vec2 mitre = at(t0, plane), in = -glm::normalize(n + np);                     // into the trench along the bisector
        float fy = floorAt(A + dir * std::max(t0, 0.2f)), top = (col.front().ok ? col.front().cap : fy + 1.6f) + 0.12f, H = top - (fy - 0.35f);
        vec2 pc = mitre + in * 0.1f + (n + np) * (sp.batter * H * 0.25f);
        Part pp = boxPart(L(vec3(pc.x, fy - 0.35f + H * 0.5f, pc.y)), vec3(0.24f, H, 0.24f), iq, Part::CYL);
        pp.seg = 10; put(pp, post, fy, vec3(0.5f), 0.8f, ++uid, 0.8f);
      }
      // sandbags on the parados (two courses, with gaps); the fire side stays clear so the firestep sees over it
      bool front = frontSide != 0 && side == frontSide;
      if (C.sandbags && !front)
        for (int k = 0; k < steps; ++k) {
          if (!col[k].ok) continue;
          float tm = t0 + (k + 0.5f) * step;
          vec2 c = A + dir * tm, q = c + n * plane;
          if (terrainFbm((c.x + n.x) * 0.35f, (c.y + n.y) * 0.35f, seed + 21) <= -0.35f) continue;
          vec2 bc = q + n * (thk + 0.42f + 0.1f * R(++uid));
          float by = 1e30f;
          for (float dl : {-0.24f, 0.0f, 0.24f}) for (float dn : {-0.14f, 0.14f}) { vec2 w = bc + dir * dl + n * dn; by = std::min(by, T.height(w.x, w.y)); }
          for (int cs = 0; cs < 2; ++cs) {
            float tone = 0.8f + 0.3f * R(++uid);
            vec3 hue = burlap * vec3(tone * (1.0f + 0.08f * (R(++uid) - 0.4f)), tone, tone * (0.92f + 0.12f * R(++uid)));
            vec2 o = bc + dir * (cs ? 0.3f : 0.0f) - n * (0.04f * cs);   // running bond: the upper course straddles the joint
            Part sb = boxPart(L(vec3(o.x, by + 0.065f + cs * 0.14f, o.y)), vec3(0.6f, 0.15f, 0.35f),
                              iq * glm::angleAxis(yawR + (R(++uid) - 0.5f) * 0.12f, vec3(0, 1, 0)) * glm::angleAxis((R(++uid) - 0.5f) * 0.1f, vec3(0, 0, 1)));
            sb.bv = 0.035f; put(sb, bag, col[k].fy, hue, 0.0f, ++uid);
          }
        }
    }
    // duckboards on the floor: two stringers, slats with uneven spacing, dark and muddy, the odd one missing
    if (std::min(D[s], D[s + 1]) > 0.9f) {
      float da = s > 0 ? -0.35f : 0.05f, db = s + 2 < P.size() ? len + 0.35f : len - 0.05f;   // overlap at a corner
      for (int sgn = -1; sgn <= 1; sgn += 2) {
        vec2 sc = A + dir * ((da + db) * 0.5f) + nrm * (sgn * 0.27f);
        float fy = floorAt(A + dir * ((da + db) * 0.5f));
        put(boxPart(L(vec3(sc.x, fy + 0.085f, sc.y)), vec3(db - da, 0.06f, 0.07f), iq * glm::angleAxis(yawR, vec3(0, 1, 0))), board, fy, vec3(0.55f), 1.0f, ++uid, 0.4f);
      }
      for (float t = da + 0.1f; t < db - 0.05f; t += 0.27f + 0.08f * R(++uid)) {
        if (R(++uid) < 0.06f) continue;
        vec2 c = A + dir * t + nrm * ((R(++uid) - 0.5f) * 0.04f);
        float fy = floorAt(c), tone = 0.45f + 0.3f * R(++uid);
        put(boxPart(L(vec3(c.x, fy + 0.13f, c.y)), vec3(0.1f + 0.03f * R(++uid), 0.028f, 0.74f + 0.06f * R(++uid)),
                    iq * glm::angleAxis(yawR + (R(++uid) - 0.5f) * 0.08f, vec3(0, 1, 0)) * glm::angleAxis((R(++uid) - 0.5f) * 0.05f, vec3(1, 0, 0))),
            board, fy, vec3(tone, tone * 0.97f, tone * 0.9f), 1.0f, ++uid, 0.4f);
      }
    }
  }
  };
  layRun(P, D, frontSide);
  std::vector<std::pair<std::vector<vec2>, std::vector<float>>> arms;   // junction arms (branches 3 m or longer): outward from the crossing
  for (auto& br : isLine ? json::array() : a->raw["trench"].value("branches", json::array())) {
    std::vector<vec2> Pa; std::vector<float> Da;
    for (auto& q : br) { Pa.push_back(vec2(i.pos.x, i.pos.z) + toWorld(vec2(q[0].get<float>(), q[1].get<float>()), i.yaw)); Da.push_back(q.size() > 2 ? q[2].get<float>() : 1.0f); }
    if (Pa.size() >= 2 && glm::length(Pa.back() - Pa.front()) >= 3.0f) { layRun(Pa, Da, 0); arms.push_back({Pa, Da}); }
  }
  // the firestep: the asset's earth step stops short of the revetment line (built for the old 0.65 m wattle);
  // packed earth fills it to the wall, so it reads as a step cut against the fire-side wall, not a free slab
  for (auto& pt : a->parts) {
    if (pt.mat < 0 || pt.mat >= (int)a->materials.size() || a->materials[pt.mat].name != "spoil" || !pt.visible) continue;
    float top = pt.center.y + pt.size.y * 0.5f;
    if (top > -1.0f || pt.size.y > 0.6f) continue;
    vec3 ax = pt.rot * vec3(1, 0, 0), az = pt.rot * vec3(0, 0, 1);
    // the piece's path segment nearest the step (local coordinates)
    vec2 cl(pt.center.x, pt.center.z); float best = 1e30f; vec2 sd(1, 0), sa(0);
    for (size_t s = 0; s + 1 < P.size(); ++s) {
      vec2 la = toWorld(P[s] - vec2(i.pos.x, i.pos.z), -i.yaw), lb = toWorld(P[s + 1] - vec2(i.pos.x, i.pos.z), -i.yaw);
      vec2 ab = lb - la; float t = std::clamp(glm::dot(cl - la, ab) / std::max(1e-6f, glm::dot(ab, ab)), 0.0f, 1.0f);
      float dd = glm::length(cl - (la + ab * t)); if (dd < best) { best = dd; sd = glm::normalize(ab); sa = la; }
    }
    vec2 nn(-sd.y, sd.x); float lat = glm::dot(cl - sa, nn); if (lat < 0) { nn = -nn; lat = -lat; }
    float ext = 0.5f * (std::fabs(glm::dot(vec2(ax.x, ax.z), nn)) * pt.size.x + std::fabs(glm::dot(vec2(az.x, az.z), nn)) * pt.size.z);
    float along = 0.5f * (std::fabs(glm::dot(vec2(ax.x, ax.z), sd)) * pt.size.x + std::fabs(glm::dot(vec2(az.x, az.z), sd)) * pt.size.z);
    float gap = plane + 0.02f - (lat - ext);   // from the step's inner face (its plank-edged front) to the revetment line
    if (gap <= 0.01f) continue;
    vec2 fc = cl + nn * (gap * 0.5f - ext);
    vec3 wc = i.pos + yawQuat(i.yaw) * vec3(fc.x, pt.center.y, fc.y);
    float yawL = std::atan2(sd.x, sd.y) - 1.5707963f;
    float fy = floorAt(vec2(wc.x, wc.z));
    wc.y += 0.02f;   // a little proud of the asset's step: packed earth on top
    put(boxPart(L(wc), vec3(along * 2.0f, pt.size.y + 0.04f, gap + 0.02f), iq * yawQuat(i.yaw) * glm::angleAxis(yawL, vec3(0, 1, 0))), earth, fy, vec3(1.0f), 0.5f, ++uid);
  }
  // end walls where the network stops (not the ramp's shallow mouth): backfill, boards, a sandbag cap
  auto endWalls = [&](const std::vector<vec2>& P, const std::vector<float>& D, int e0) {
  for (int e = e0; e < 2; ++e) {
    vec2 p = e ? P.back() : P.front(), q = e ? P[P.size() - 2] : P[1];
    float depth = e ? D.back() : D.front();
    if (depth < 0.4f) continue;
    bool joined = false;
    for (size_t k = 0; k < st->seg.size(); ++k) {
      vec4 sg = st->seg[k];
      for (vec2 x : {vec2(sg.x, sg.y), vec2(sg.z, sg.w)}) if (glm::length(x - p) < 0.3f && glm::length(x - q) > 0.3f) {
        vec2 o = glm::length(x - vec2(sg.x, sg.y)) < 0.3f ? vec2(sg.z, sg.w) : vec2(sg.x, sg.y);   // joined unless it is this piece's own segment
        if (glm::length(o - q) > 0.3f) joined = true;
      }
    }
    if (!joined && isLine)   // a line end on another trench's centreline (a T): open into it
      for (size_t k = 0; k < st->seg.size() && !joined; ++k) {
        vec2 a0(st->seg[k].x, st->seg[k].y), a1(st->seg[k].z, st->seg[k].w);
        bool own = false;
        for (size_t j = 0; j + 1 < P.size() && !own; ++j) own = (glm::length(a0 - P[j]) < 0.05f && glm::length(a1 - P[j + 1]) < 0.05f) || (glm::length(a1 - P[j]) < 0.05f && glm::length(a0 - P[j + 1]) < 0.05f);
        if (!own && segDist2(p, a0, a1) < sp.floorHalf + 0.6f) joined = true;
      }
    if (joined || !C.revetment) continue;
    vec2 dir = glm::normalize(p - q), n = dir, along(n.y, -n.x);
    vec2 pe = p + n * 0.03f;   // the asset's end wall line (its invisible collider stays there)
    float fy = T.height(p.x - dir.x * 0.3f, p.y - dir.y * 0.3f), cap = lipAt(pe, n) + 0.02f, y0 = fy - 0.1f, H = cap - 0.06f - y0;
    float yawR = std::atan2(n.x, n.y);
    quat lean = iq * yawPitch(yawR, tilt);
    vec2 cc = pe + n * (boardT + thk * 0.5f + sp.batter * H * 0.5f);
    put(boxPart(L(vec3(cc.x, y0 + H * 0.5f, cc.y)), vec3(2 * plane + 0.1f, H, thk), lean), earth, fy, vec3(1.0f), 0.3f, ++uid);
    for (float y = y0; y < cap - 0.09f;) {
      float bh = std::min(0.17f + 0.07f * R(++uid), cap - 0.06f - y), tone = 0.5f + 0.36f * R(++uid);
      vec2 bc = pe + n * (boardT * 0.5f + sp.batter * (y + bh * 0.5f - y0));
      put(boxPart(L(vec3(bc.x, y + bh * 0.5f, bc.y)), vec3(2 * plane + 0.04f, bh - 0.012f, boardT), lean * glm::angleAxis((R(++uid) - 0.5f) * 0.03f, vec3(0, 0, 1))),
          board, fy, vec3(tone, tone, tone * 0.93f), 0.7f, ++uid);
      y += bh;
    }
    for (int cl = -1; cl <= 1; ++cl) {
      float tone = 0.8f + 0.3f * R(++uid);
      vec2 sc = pe + along * (cl * plane * 0.66f) + n * (boardT + thk * 0.45f + sp.batter * H);
      Part c = boxPart(L(vec3(sc.x, cap + 0.07f, sc.y)), vec3(0.58f, 0.14f, 0.33f), iq * glm::angleAxis(yawR + 1.5707963f + (R(++uid) - 0.5f) * 0.15f, vec3(0, 1, 0)));
      c.bv = 0.035f; put(c, bag, fy, burlap * tone, 0.0f, ++uid);
    }
  }
  };
  endWalls(P, D, 0);
  for (auto& [Pa, Da] : arms) endWalls(Pa, Da, 1);   // only the arm's outer end (its inner end is the crossing)
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
  if (r.earthMesh.count) op.push_back({&r.earthMesh, tr ? M * glm::translate(mat4(1), vec3(0, kTrenchMeshDrop, 0)) : M});
}

// ================================================================ preview
void blendPreview(const App& app, Overlay& ov, const AssetDef* a, vec3 pos, float yaw) {
  if (!blendPinned(a)) return;
  const BlendConfig& C = blendConfig();
  const Terrain& T = app.terrain;
  vec2 c = vec2(pos.x, pos.z) + toWorld(padOff(a), yaw), half = padHalf(a);
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
        vec2 w = vec2(i.pos.x, i.pos.z) + toWorld(padOff(a) + l, i.yaw);
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
