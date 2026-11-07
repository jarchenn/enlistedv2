// V8 Beautify (Creative > Beautify..., docs/terrain-tools.md):
//  1. scatter on open ground (rocks, vegetation, ground cover, water, rubble) from the roles in
//     content/settings/beautify.json, never inside footprints, paths, worn routes, objectives, spawns, approach routes or
//     the attack axis, and never moving or removing an existing placement;
//  2. the outer ring: the terrain grows by whole tiles beyond its old edge (MapDoc::terrainGen.ring; the playable area
//     and every placement keep their exact ground, tile keys of crater / sculpt layers are remapped) with gentle hills;
//  3. 2-3 rows of decorative buildings beyond the world border facing inward, hedges and trees between them.
// Everything placed is tagged extra.beautify (a re-run replaces it); everything beyond the border also extra.decor:
// allowed outside the build area (decorMayLeaveBorder), never furnished (interior.cpp), outside the navmesh (the AI
// navmesh is clamped to the build area) and drawn cheaply (beautifyDecorDraw). One undo step per Apply.
#include "terrain_tools.h"
#include "tank.h"
#include <imgui.h>
#include <algorithm>

float terrainFbm(float x, float z, uint32_t s);   // terrain.cpp

const json& beautifySettings() {
  static json j = [] {
    try { return json::parse(readTextFile(contentDir() / "settings" / "beautify.json")); }
    catch (std::exception& e) { logf("beautify.json: %s", e.what()); return json::object(); }
  }();
  return j;
}

bool isDecor(const Instance& i) { return i.extra.is_object() && i.extra.contains("decor") && i.extra["decor"].is_boolean() && i.extra["decor"].get<bool>(); }
static bool isBeautify(const Instance& i) { return i.extra.is_object() && i.extra.contains("beautify"); }

bool decorMayLeaveBorder(const App& app, uint64_t id, vec3 w) {
  if (!id) return false;
  const Instance* i = nullptr;
  for (auto& x : app.doc.instances) if (x.id == id) { i = &x; break; }
  if (!i || !isDecor(*i)) return false;
  const Terrain& T = app.terrain;
  return w.x > T.org.x + 2 && w.z > T.org.y + 2 && w.x < T.org.x + T.size - 2 && w.z < T.org.y + T.sizeZ - 2;
}

bool beautifyDecorDraw(App& app, const Instance& i, InstanceRT& r, const mat4& M, std::vector<DrawItem>& op, std::vector<DrawItem>& gl) {
  if (!r.asset) return false;
  const bool decor = isDecor(i);
  if (!decor && !isBeautify(i)) return false;
  static const json d = beautifySettings().value("decor", json::object());
  static const float cull = d.value("cullDist", 950.0f), nearD = d.value("nearDist", 140.0f), glassD = d.value("glassDist", 320.0f);
  static const float treeCull = d.value("treeCullDist", 480.0f), smallCull = d.value("smallCullDist", 300.0f);
  vec3 eye = app.mode == Mode::Explore ? app.eyePos() : app.camPos;
  float dist = glm::length(i.pos - eye);
  if (decor && dist > cull) return true;   // beyond the horizon band: not drawn at all
  const AssetDef* a = r.asset;
  // the scene draws every opaque item in the main and all shadow passes without frustum culling; decor beyond the
  // near band is skipped when its bounds (+30 m, so off-screen shadow casters near the view stay) are outside the view
  if (dist > nearD) {
    mat4 vp = app.projM * app.viewM;
    vec3 c = vec3(M * vec4((a->bmin + a->bmax) * 0.5f, 1.0f));
    float rad = glm::length(a->bmax - a->bmin) * 0.5f + 30.0f;
    for (int k = 0; k < 4; ++k) {   // left, right, bottom, top planes (Gribb-Hartmann)
      int row = k / 2; float sgn = (k % 2) ? -1.0f : 1.0f;
      vec4 pl(vp[0][3] + sgn * vp[0][row], vp[1][3] + sgn * vp[1][row], vp[2][3] + sgn * vp[2][row], vp[3][3] + sgn * vp[3][row]);
      float len = glm::length(vec3(pl));
      if (len > 1e-6f && (glm::dot(vec3(pl), c) + pl.w) / len < -rad) return true;
    }
  }
  if (!decor) return false;   // Beautify scatter inside the border: frustum cull only
  if (!a->enterable) {   // ring dressing: trees, hedges, rocks, patches. Culled well inside their usual distances
    float c = a->tree.on ? treeCull : a->category == "Vegetation" && a->footprint.x >= 6.0f ? treeCull : smallCull;
    return dist > c;     // otherwise the ordinary path
  }
  // near, damaged, furnished, modelled or non-building decor: the ordinary path
  if (dist < nearD || r.damaged || a->model || a->nature.kind != NatureDef::NONE || a->tree.on || a->path.on || app.interior.hideBuiltin(i)) return false;
  op.push_back({&a->opaque, M});
  op.push_back({&a->panelsIntact, M});
  if (dist < glassD && a->glass.count) gl.push_back({&a->glass, M});
  return true;   // doors and shutters (separate leaf meshes) are skipped at range: 1-3 draws per building
}

namespace {
struct Rng { uint64_t s; explicit Rng(uint64_t v) : s(v * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull) {} uint64_t u() { uint64_t z = (s += 0x9E3779B97F4A7C15ull); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); } float f() { return (u() >> 40) / 16777216.0f; } };
bool matchPat(const std::string& id, const std::string& pat) {
  if (!pat.empty() && pat.back() == '*') return id.compare(0, pat.size() - 1, pat, 0, pat.size() - 1) == 0;
  return id == pat;
}
std::vector<const AssetDef*> assetsFor(const App& app, const json& pats) {
  std::vector<const AssetDef*> out;
  if (!pats.is_array()) return out;
  for (auto& a : app.lib.assets) {
    for (auto& p : pats) if (p.is_string() && matchPat(a->id, p.get<std::string>())) { out.push_back(a.get()); break; }
  }
  std::sort(out.begin(), out.end(), [](const AssetDef* x, const AssetDef* y) { return x->id < y->id; });
  return out;
}
bool obbOverlap(vec2 c1, vec2 h1, float a1, vec2 c2, vec2 h2, float a2) {
  vec2 ax[4] = {{std::cos(a1), -std::sin(a1)}, {std::sin(a1), std::cos(a1)}, {std::cos(a2), -std::sin(a2)}, {std::sin(a2), std::cos(a2)}};
  vec2 d = c2 - c1;
  for (auto& a : ax) {
    float r1 = h1.x * std::fabs(glm::dot(ax[0], a)) + h1.y * std::fabs(glm::dot(ax[1], a));
    float r2 = h2.x * std::fabs(glm::dot(ax[2], a)) + h2.y * std::fabs(glm::dot(ax[3], a));
    if (std::fabs(glm::dot(d, a)) > r1 + r2) return false;
  }
  return true;
}
float smooth01(float e0, float e1, float x) { float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f); return t * t * (3 - 2 * t); }

// keep-out shapes (terrainProtectShapes format) with a bucket index: distance to the nearest one (<= 0 inside)
struct Protect {
  struct S { int type; vec2 c, h; float yaw, r; std::vector<vec2> pts; vec2 lo, hi; };
  std::vector<S> shapes; std::map<int64_t, std::vector<int>> grid; static constexpr float B = 24.0f; float reach = 30.0f;
  static int64_t cell(int x, int z) { return ((int64_t)x << 32) ^ (uint32_t)z; }
  void add(S s) {
    int id = (int)shapes.size(); shapes.push_back(s);
    for (int z = (int)std::floor((s.lo.y - reach) / B); z <= (int)std::floor((s.hi.y + reach) / B); ++z)
      for (int x = (int)std::floor((s.lo.x - reach) / B); x <= (int)std::floor((s.hi.x + reach) / B); ++x) grid[cell(x, z)].push_back(id);
  }
  void addObb(vec2 c, vec2 h, float yaw) { float rr = glm::length(h); add({0, c, h, yaw, 0, {}, c - vec2(rr), c + vec2(rr)}); }
  void load(const json& j) {
    for (auto& o : j.value("o", json::array())) if (o.size() >= 5) addObb(vec2(o[0].get<float>(), o[1].get<float>()), vec2(o[2].get<float>(), o[3].get<float>()), o[4].get<float>());
    for (auto& o : j.value("c", json::array())) if (o.size() >= 3) { vec2 c(o[0].get<float>(), o[1].get<float>()); float r = o[2].get<float>(); add({1, c, vec2(0), 0, r, {}, c - vec2(r), c + vec2(r)}); }
    for (auto& o : j.value("l", json::array())) if (o.size() >= 3) {
      S s{2, vec2(0), vec2(0), 0, o[0].get<float>() * 0.5f, {}, vec2(1e30f), vec2(-1e30f)};
      for (size_t k = 1; k + 1 < o.size(); k += 2) { vec2 p(o[k].get<float>(), o[k + 1].get<float>()); s.pts.push_back(p); s.lo = glm::min(s.lo, p); s.hi = glm::max(s.hi, p); }
      if (s.pts.empty()) continue;
      // long polylines (the attack axis): one shape per segment keeps the buckets tight
      for (size_t k = 0; k + 1 < s.pts.size() || (k == 0 && s.pts.size() == 1); ++k) {
        S g = s; g.pts = {s.pts[k], s.pts[std::min(k + 1, s.pts.size() - 1)]};
        g.lo = glm::min(g.pts[0], g.pts[1]) - vec2(g.r); g.hi = glm::max(g.pts[0], g.pts[1]) + vec2(g.r);
        add(g);
        if (s.pts.size() == 1) break;
      }
    }
  }
  static float dist(const S& s, vec2 p) {
    if (s.type == 0) {
      float ca = std::cos(radians(s.yaw)), sa = std::sin(radians(s.yaw));
      vec2 d = p - s.c; vec2 l(ca * d.x - sa * d.y, sa * d.x + ca * d.y);
      vec2 q = glm::abs(l) - s.h;
      return glm::length(glm::max(q, vec2(0))) + std::min(std::max(q.x, q.y), 0.0f);
    }
    if (s.type == 1) return glm::length(p - s.c) - s.r;
    vec2 a = s.pts[0], b = s.pts[1], ab = b - a; float L2 = glm::dot(ab, ab);
    float t = L2 > 1e-9f ? std::clamp(glm::dot(p - a, ab) / L2, 0.0f, 1.0f) : 0.0f;
    return glm::length(p - (a + ab * t)) - s.r;
  }
  float nearest(vec2 p) const {
    auto it = grid.find(cell((int)std::floor(p.x / B), (int)std::floor(p.y / B)));
    if (it == grid.end()) return 1e9f;
    float best = 1e9f;
    for (int k : it->second) best = std::min(best, dist(shapes[k], p));
    return best;
  }
};

// placed things as circles (spacing between scatter items and around existing dressing)
struct Occupancy {
  struct E { vec2 p; float r; int role; };
  std::map<int64_t, std::vector<E>> grid; static constexpr float B = 16.0f;
  static int64_t cell(int x, int z) { return ((int64_t)x << 32) ^ (uint32_t)z; }
  void add(vec2 p, float r, int role) { grid[cell((int)std::floor(p.x / B), (int)std::floor(p.y / B))].push_back({p, r, role}); }
  bool free(vec2 p, float r, int role, float spacing) const {
    int cx = (int)std::floor(p.x / B), cz = (int)std::floor(p.y / B), n = (int)std::ceil((spacing + r + 12.0f) / B);
    for (int z = cz - n; z <= cz + n; ++z) for (int x = cx - n; x <= cx + n; ++x) {
      auto it = grid.find(cell(x, z)); if (it == grid.end()) continue;
      for (auto& e : it->second) {
        float need = std::max(e.r + r + 0.4f, e.role == role ? spacing : 0.0f);
        if (glm::length(e.p - p) < need) return false;
      }
    }
    return true;
  }
};

float assetRadius(const AssetDef* a) { return std::max(0.3f, 0.5f * std::max(a->footprint.x, a->footprint.y)); }

// ground height for a placement beyond the border (checkPlacement refuses those): the editor's height rules without
// the build-area and overlap tests. ok = false over water, off the terrain or on too steep a grade.
float seatOutside(const App& app, const AssetDef* a, vec3 pos, float yaw, bool& ok) {
  const Terrain& T = app.terrain;
  ok = true;
  vec2 hf = a->footprint * 0.5f; quat q = yawQuat(yaw);
  const vec3 loc[] = {{0, 0, 0}, {-hf.x, 0, -hf.y}, {hf.x, 0, -hf.y}, {-hf.x, 0, hf.y}, {hf.x, 0, hf.y}, {0, 0, -hf.y}, {0, 0, hf.y}, {-hf.x, 0, 0}, {hf.x, 0, 0}};
  float top = -1e9f, low = 1e9f, centre = T.height(pos.x, pos.z);
  for (auto& l : loc) {
    vec3 w = pos + q * l;
    if (w.x < T.org.x + 3 || w.z < T.org.y + 3 || w.x > T.org.x + T.size - 3 || w.z > T.org.y + T.sizeZ - 3) { ok = false; return 0; }
    float h = T.height(w.x, w.z);
    if (h < T.waterLevel + 0.2f) { ok = false; return 0; }
    top = std::max(top, h); low = std::min(low, h);
  }
  if (a->nature.kind == NatureDef::ROCK) return (low + centre) * 0.5f;
  if (a->nature.kind != NatureDef::NONE || a->category == "Nature" || a->path.on) return centre;
  float lim = a->plinthDepth > 0 ? std::min(a->maxGrade, a->plinthDepth) : a->maxGrade;
  if (top - low > lim) { ok = false; return 0; }
  return top;
}

uint64_t addInstance(App& app, const AssetDef* a, vec3 p, float yaw, bool decor) {
  Instance i; i.id = app.doc.newId(); i.assetId = a->id; i.assetVersion = a->version;
  i.pos = p; i.yaw = wrapDeg(yaw);
  i.extra = json::object(); i.extra["beautify"] = true;
  if (decor) i.extra["decor"] = true;
  app.bump(i);
  app.doc.instances.push_back(i);
  return i.id;
}
float sliderOf(const BeautifyParams& p, const std::string& s) {
  if (s == "rocks") return p.rocks;
  if (s == "vegetation") return p.vegetation;
  if (s == "groundCover") return p.groundCover;
  if (s == "water") return p.water;
  if (s == "buildings") return p.buildings;
  return 0.5f;
}
}

int beautifyRemove(App& app) {
  size_t n = app.doc.instances.size();
  app.doc.instances.erase(std::remove_if(app.doc.instances.begin(), app.doc.instances.end(), isBeautify), app.doc.instances.end());
  if (app.selected && !app.doc.find(app.selected)) app.selected = 0;
  if (n != app.doc.instances.size()) app.dirty = true;
  return (int)(n - app.doc.instances.size());
}

static int growRing(App& app, uint32_t seed) {
  const json rj = beautifySettings().value("ring", json::object());
  int want = std::clamp(rj.value("tiles", 6), 1, 24), have = app.terrain.ringTiles();
  if (have >= want) return have;
  int shift = want - have, oldTX = app.terrain.tilesX(), newTX = oldTX + 2 * shift;
  // tile keys follow the grid: crater deltas and sculpted tiles move to their new keys (same world position)
  auto remap = [&](std::map<int, std::vector<float>>& m) { std::map<int, std::vector<float>> o; for (auto& [k, v] : m) o[Terrain::remapKey(k, oldTX, shift, newTX)] = std::move(v); m.swap(o); };
  remap(app.doc.terrainDeltas); remap(app.doc.terrainSculpt);
  json r = rj; r["tiles"] = want; r["seed"] = seed;
  if (!app.doc.terrainGen.is_object()) app.doc.terrainGen = json::object();
  app.doc.terrainGen["ring"] = r;
  terrainRegenerate(app);
  return want;
}

BeautifyReport beautifyRun(App& app, const BeautifyParams& P) {
  BeautifyReport rep;
  const json& S = beautifySettings();
  beautifyRemove(app);
  if (P.ring) rep.ringTiles = growRing(app, P.seed);
  const Terrain& T = app.terrain;
  const float Hx = T.buildHalfX, Hz = T.buildHalfZ;
  const float outer = std::max(0.0f, T.margin - 14.0f);   // usable band beyond the border
  // ---- keep-out shapes and occupancy from what is on the map now
  Protect prot; prot.load(terrainProtectShapes(app));
  Occupancy occ;
  for (auto& i : app.doc.instances) { const AssetDef* a = app.assetFor(i); if (a) occ.add(vec2(i.pos.x, i.pos.z), assetRadius(a), -1); }
  std::vector<std::pair<vec2, vec3>> decorObb;   // centre, (half x, half z, yaw)
  // ---- decorative buildings in rows beyond the border, facing inward
  if (P.ring && P.buildings > 0.01f) {
    const json D = S.value("decor", json::object());
    auto houses = assetsFor(app, D.value("buildings", json::array())), hedges = assetsFor(app, D.value("hedges", json::array())), trees = assetsFor(app, D.value("trees", json::array()));
    if (houses.empty()) rep.missing.push_back("decor buildings");
    std::vector<float> rowsAt = D.value("rowOffsets", std::vector<float>{16, 44, 72});
    int rows = std::clamp(1 + (int)std::round(P.buildings * 2.0f), 1, (int)rowsAt.size());
    float gap = glm::mix(D.value("gapMax", 34.0f), D.value("gapMin", 5.0f), P.buildings);
    float treesPerGap = D.value("treesPerGap", 1.2f);
    Rng rng(P.seed * 7919ull + 17);
    auto overlapsDecor = [&](vec2 c, vec2 h, float yaw) {
      for (auto& [dc, dh] : decorObb) if (obbOverlap(c, h, radians(yaw), dc, vec2(dh.x, dh.y), radians(dh.z))) return true;
      for (auto& i : app.doc.instances) {   // anything already standing out there (W5 maps place nothing beyond the border)
        if (std::fabs(i.pos.x) <= Hx && std::fabs(i.pos.z) <= Hz) continue;
        const AssetDef* b = app.assetFor(i); if (!b) continue;
        if (obbOverlap(c, h, radians(yaw), vec2(i.pos.x, i.pos.z), b->footprint * 0.5f, radians(i.yaw))) return true;
      }
      return false;
    };
    for (int r = 0; r < rows && !houses.empty(); ++r) {
      float off = rowsAt[r];
      if (off + 10.0f > outer) break;
      for (int side = 0; side < 4; ++side) {
        // side 0 north (+z), 1 south (-z), 2 east (+x), 3 west (-x); u runs along the row
        bool alongX = side < 2;
        float sgn = (side == 0 || side == 2) ? 1.0f : -1.0f;
        float half = (alongX ? Hx : Hz) + off + (alongX ? 12.0f : -6.0f);
        float yawIn = side == 0 ? 180.0f : side == 1 ? 0.0f : side == 2 ? -90.0f : 90.0f;
        float u = -half + rng.f() * gap;
        float lastEnd = -1e9f;
        while (u < half) {
          const AssetDef* a = houses[rng.u() % houses.size()];
          float w = a->footprint.x, dep = a->footprint.y;
          float uc = u + w * 0.5f;
          float d = (alongX ? Hz : Hx) + off + dep * 0.5f + rng.f() * 4.0f;
          vec2 c = alongX ? vec2(uc, sgn * d) : vec2(sgn * d, uc);
          float yaw = yawIn + (rng.f() - 0.5f) * 10.0f;
          bool ok = false; float y = 0;
          if (!overlapsDecor(c, a->footprint * 0.5f + vec2(1.5f), yaw)) y = seatOutside(app, a, vec3(c.x, 0, c.y), yaw, ok);
          if (!ok) { u += 6.0f; continue; }
          // the gap since the previous building: hedge pieces along the row and a tree or two behind them
          float g0 = lastEnd, g1 = u;
          if (g0 > -1e8f && g1 - g0 > 9.0f) {
            float rowD = (alongX ? Hz : Hx) + off + 2.0f;
            if (!hedges.empty()) {
              const AssetDef* hd = hedges[rng.u() % hedges.size()];
              float L = hd->footprint.x;
              for (float s = g0 + 1.0f; s + L <= g1 - 1.0f; s += L) {
                float sc = s + L * 0.5f;
                vec2 hc = alongX ? vec2(sc, sgn * rowD) : vec2(sgn * rowD, sc);
                float hy = alongX ? 0.0f : 90.0f;
                bool hok; float hh = seatOutside(app, hd, vec3(hc.x, 0, hc.y), hy, hok);
                if (!hok || overlapsDecor(hc, hd->footprint * 0.5f, hy)) continue;
                addInstance(app, hd, vec3(hc.x, hh, hc.y), hy, true); decorObb.push_back({hc, vec3(hd->footprint * 0.5f, hy)}); ++rep.hedges;
                occ.add(hc, assetRadius(hd), -2);
              }
            }
            int nt = (int)std::floor(treesPerGap * (g1 - g0) / 14.0f + rng.f());
            for (int k = 0; k < nt && !trees.empty(); ++k) {
              const AssetDef* ta = trees[rng.u() % trees.size()];
              float tu = g0 + 2.0f + rng.f() * std::max(0.0f, g1 - g0 - 4.0f), td = rowD + 3.0f + rng.f() * 8.0f;
              vec2 tc = alongX ? vec2(tu, sgn * td) : vec2(sgn * td, tu);
              float ty = rng.f() * 360.0f;
              bool tok; float th = seatOutside(app, ta, vec3(tc.x, 0, tc.y), ty, tok);
              if (!tok || overlapsDecor(tc, ta->footprint * 0.5f, ty) || !occ.free(tc, assetRadius(ta), -3, 6.0f)) continue;
              addInstance(app, ta, vec3(tc.x, th, tc.y), ty, true); occ.add(tc, assetRadius(ta), -3); ++rep.ringTrees;
            }
          }
          addInstance(app, a, vec3(c.x, y, c.y), yaw, true);
          decorObb.push_back({c, vec3(a->footprint * 0.5f, yaw)}); occ.add(c, assetRadius(a), -2);
          ++rep.decor;
          lastEnd = u + w;
          u += w + gap * (0.5f + rng.f());
        }
      }
    }
    for (auto& [c, h] : decorObb) prot.addObb(c, vec2(h.x, h.y), h.z);
  }
  // ---- scatter roles
  const json roles = S.value("roles", json::array());
  const float ringScale = S.value("ringScatter", 0.6f);
  for (int ri = 0; ri < (int)roles.size(); ++ri) {
    const json& R = roles[ri];
    std::string name = R.value("name", "role" + std::to_string(ri));
    float sl = sliderOf(P, R.value("slider", ""));
    if (sl <= 0.001f) continue;
    auto list = assetsFor(app, R.value("assets", json::array()));
    if (list.empty()) { ++rep.skippedRoles; rep.missing.push_back(name); continue; }
    float perHa = R.value("perHa", 1.0f) * sl * 2.0f, spacing = R.value("spacing", 6.0f), pad = R.value("pad", 2.0f);
    float maxSlope = R.value("maxSlope", 30.0f), cluster = std::clamp(R.value("cluster", 0.0f), 0.0f, 1.0f);
    int block = std::max(1, R.value("block", 1));
    for (int zone = 0; zone < 2; ++zone) {   // 0 inside the playable area, 1 the ring band beyond the border
      bool inside = zone == 0;
      if (inside ? (!P.inside || !R.value("inside", true)) : (!P.ring || !R.value("ring", true))) continue;
      float band0 = 6.0f, band1 = outer;
      if (!inside && band1 <= band0 + 4) continue;
      double areaM2 = inside ? (2.0 * (Hx - 3)) * (2.0 * (Hz - 3)) : (2.0 * (Hx + band1)) * (2.0 * (Hz + band1)) - (2.0 * (Hx + band0)) * (2.0 * (Hz + band0));
      int target = (int)std::round(areaM2 / 10000.0 * perHa * (inside ? 1.0f : ringScale) / block);
      Rng rng((uint64_t)P.seed * 1000003ull + (uint64_t)ri * 7907ull + (uint64_t)zone * 104729ull);
      int placed = 0;
      for (int att = 0; att < target * 14 + 20 && placed < target; ++att) {
        vec2 p;
        if (inside) p = vec2((rng.f() * 2 - 1) * (Hx - 3), (rng.f() * 2 - 1) * (Hz - 3));
        else {   // uniform over the band: a point in the outer rectangle, retried until it is outside the inner one
          p = vec2((rng.f() * 2 - 1) * (Hx + band1), (rng.f() * 2 - 1) * (Hz + band1));
          if (std::fabs(p.x) < Hx + band0 && std::fabs(p.y) < Hz + band0) continue;
        }
        if (cluster > 0) {
          float n = terrainFbm(p.x / 70.0f, p.y / 70.0f, P.seed * 31u + (uint32_t)ri * 101u);
          float keep = glm::mix(1.0f, smooth01(-0.15f, 0.45f, n), cluster);
          if (rng.f() > keep) continue;
        }
        const AssetDef* a = list[rng.u() % list.size()];
        float rr = assetRadius(a), yaw = rng.f() * 360.0f;
        if (block > 1) yaw = std::round(yaw / 90.0f) * 90.0f;
        if (prot.nearest(p) < pad + rr) continue;
        vec3 n = T.normal(p.x, p.y);
        if (degrees(std::acos(std::clamp(n.y, -1.0f, 1.0f))) > maxSlope) continue;
        if (T.height(p.x, p.y) < T.waterLevel + 0.3f) continue;
        int bn = block > 1 ? 1 + (int)(rng.u() % block) : 1, bm = block > 1 ? 1 + (int)(rng.u() % block) : 1;
        quat q = yawQuat(yaw);
        int got = 0;
        for (int bj = 0; bj < bm; ++bj) for (int bi = 0; bi < bn; ++bi) {
          vec3 o3 = q * vec3(bi * a->footprint.x, 0, bj * a->footprint.y);
          vec2 pp(p.x + o3.x, p.y + o3.z);
          if (got && (prot.nearest(pp) < pad + rr || T.height(pp.x, pp.y) < T.waterLevel + 0.3f)) continue;
          if (!occ.free(pp, rr, ri, got ? 0.0f : spacing)) continue;
          float y = 0; bool ok = false;
          if (inside) { PlaceCheck c = app.checkPlacement(a, vec3(pp.x, 0, pp.y), yaw, 0); ok = c.ok; y = c.y; }
          else y = seatOutside(app, a, vec3(pp.x, 0, pp.y), yaw, ok);
          if (!ok) continue;
          addInstance(app, a, vec3(pp.x, y, pp.y), yaw, !inside);
          occ.add(pp, rr, ri);
          ++got;
        }
        if (got) { ++placed; rep.placed[name] += got; }
      }
    }
  }
  app.dirty = true;
  return rep;
}

// ================================================================ dialog
void beautifyDialog(App& app, bool& open) {
  static BeautifyParams P;
  static bool previewing = false, ringAdded = false;
  static Snapshot pre;
  static BeautifyReport last; static bool haveLast = false;
  auto cancel = [&] { if (previewing) { app.restore(pre); if (!app.undoStack.empty()) app.undoStack.pop_back(); } previewing = false; haveLast = false; };
  ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_Appearing);
  bool keep = true;
  if (ImGui::Begin("Beautify", &keep, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextWrapped("Dresses the map: scatter on open ground (never in footprints, paths, routes, objectives, spawns or the attack axis) and an outer ring of countryside beyond the world border. Existing placements are not moved.");
    ImGui::SliderFloat("Rocks", &P.rocks, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Vegetation", &P.vegetation, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Ground cover", &P.groundCover, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Water", &P.water, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Border buildings", &P.buildings, 0.0f, 1.0f, "%.2f");
    int s = (int)P.seed; if (ImGui::InputInt("Seed", &s)) P.seed = (uint32_t)std::max(0, s);
    ImGui::Checkbox("Scatter inside the playable area", &P.inside);
    ImGui::Checkbox("Outer ring (terrain + border buildings)", &P.ring);
    ImGui::Separator();
    if (ImGui::Button("Preview")) {
      if (!previewing) { app.pushUndo(); pre = app.undoStack.back(); previewing = true; ringAdded = false; }
      else if (ringAdded && !P.ring) { app.restore(pre); ringAdded = false; }   // ring switched off again: back to the old terrain
      last = beautifyRun(app, P); haveLast = true;
      ringAdded = ringAdded || last.ringTiles > 0;
    }
    ImGui::SameLine();
    if (ImGui::Button("Apply")) {
      if (!previewing) { app.pushUndo(); last = beautifyRun(app, P); }
      previewing = false; haveLast = false; keep = false;
      app.toast("Beautify applied (one undo step: Ctrl+Z).", 5);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) { cancel(); keep = false; }
    ImGui::SameLine();
    if (ImGui::Button("Remove earlier Beautify")) { app.pushUndo(); int n = beautifyRemove(app); app.toast(std::to_string(n) + " Beautify placements removed (Ctrl+Z to undo).", 4); }
    if (haveLast) {
      ImGui::Separator();
      int total = 0; for (auto& [k, v] : last.placed) total += v;
      ImGui::Text("Preview: %d scatter placements, %d border buildings, %d hedges, %d ring trees", total, last.decor, last.hedges, last.ringTrees);
      for (auto& [k, v] : last.placed) ImGui::BulletText("%s: %d", k.c_str(), v);
      if (!last.missing.empty()) { std::string m; for (auto& x : last.missing) m += (m.empty() ? "" : ", ") + x; ImGui::TextDisabled("Not in this catalogue (skipped): %s", m.c_str()); }
    }
  }
  ImGui::End();
  if (!keep) { if (previewing) cancel(); open = false; }
}
