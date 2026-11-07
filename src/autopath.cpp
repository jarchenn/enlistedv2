// Auto paths between houses (V9, user item 3; docs/entities.md#auto-paths).
//
// Paths > Connect houses:
//  1. every house (autopathIsHouse) gets a path_dirt apron straight out of each ground-level entrance
//     (NavMeta::entrances, asset space p / dir; dir points into the room), long enough to clear its own walls;
//  2. houses are joined by a minimum spanning tree over the shortest entrance-to-entrance distances, plus a few loop
//     links where going round the tree would be much longer than the direct way; unused entrances get a short spur to
//     the network when one is near;
//  3. each link is routed by A* on a 0.5 m grid that avoids footprints (with the path's half width as clearance), water
//     and slopes over 24 deg, prefers existing lanes and paths, and strongly prefers cells already routed (links merge);
//  4. the routed cells become one graph; its polylines are string-pulled (line of sight with exact clearance) and
//     Chaikin-smoothed, then emitted as chained path_dirt pieces with curved joint pieces ("bend") at every turn and a
//     continuous along-path texture coordinate ("u0"), plus one ground-wear footpath route per polyline;
//  5. everything is tagged (Instance::extra["auto"], route "auto": true): re-running replaces only that, as one undo
//     step. The "Auto paths" toggle re-runs it whenever a house is placed (inside that placement's undo step).
#include "autopath.h"
#include "nature.h"
#include "lights.h"   // L3: lamp posts along the routes
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <queue>
#include <unordered_map>
#include <unordered_set>

bool gAutoPaths = false;

namespace {
constexpr float kLinkW = 1.8f, kApronW = 1.6f, kApronL = 2.6f, kCell = 0.5f, kMaxSlopeDeg = 24.0f, kMarginWorld = 30.0f;
constexpr float kClear = 0.15f;   // gap kept between a path edge and a footprint

float yawOf(vec2 d) { return std::atan2(d.x, d.y); }   // world yaw (rad) of an xz direction (asset convention: +z at 0)
float wrapPi(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }

// ---- obstacles: oriented boxes (structures, defences, rocks, guns, crates) and discs (tree trunks)
struct Obst { uint64_t id = 0; vec2 c{0}; float yaw = 0; vec2 cen{0}, h{0}; float r = -1; vec2 mn{0}, mx{0}; bool soft = false; };   // soft: hedges, shrubs, wire, defences (relaxed pass only)
struct Obstacles {
  std::vector<Obst> v;
  std::unordered_map<int64_t, std::vector<int>> bucket;   // 8 m buckets
  static int64_t key(int x, int z) { return ((int64_t)x << 32) ^ (uint32_t)z; }
  void add(const Obst& o) {
    int k = (int)v.size(); v.push_back(o);
    for (int z = (int)std::floor(o.mn.y / 8); z <= (int)std::floor(o.mx.y / 8); ++z)
      for (int x = (int)std::floor(o.mn.x / 8); x <= (int)std::floor(o.mx.x / 8); ++x) bucket[key(x, z)].push_back(k);
  }
  static float dist(const Obst& o, vec2 p) {   // signed distance to the shape
    if (o.r >= 0) return glm::length(p - o.c) - o.r;
    float c = std::cos(o.yaw), s = std::sin(o.yaw); vec2 d = p - o.c;
    vec2 l(d.x * c - d.y * s, d.x * s + d.y * c);   // into asset space (yaw about +y)
    vec2 q = glm::abs(l - o.cen) - o.h;
    return glm::length(glm::max(q, vec2(0))) + std::min(std::max(q.x, q.y), 0.0f);
  }
  float clearance(vec2 p, uint64_t ignore = 0, float cap = 6.0f, bool soft = true) const {
    float best = cap;
    auto it = bucket.find(key((int)std::floor(p.x / 8), (int)std::floor(p.y / 8)));
    if (it == bucket.end()) return best;
    for (int k : it->second) { const Obst& o = v[k]; if (o.id == ignore || (!soft && o.soft)) continue; best = std::min(best, dist(o, p)); }
    return best;
  }
};
Obst boxObst(uint64_t id, const Instance& i, vec2 bmin, vec2 bmax, float reach) {
  Obst o; o.id = id; o.c = vec2(i.pos.x, i.pos.z); o.yaw = radians(i.yaw); o.cen = (bmin + bmax) * 0.5f; o.h = (bmax - bmin) * 0.5f;
  float R = glm::length(o.h) + glm::length(o.cen) + reach; o.mn = o.c - vec2(R); o.mx = o.c + vec2(R);
  return o;
}
bool isPropLike(const App& app, const AssetDef* a) { return const_cast<InteriorSys&>(app.interior).isProp(a); }
// what a footpath must go round: everything with a footprint except paths, ground-cover patches, vehicles and furniture
void gatherObstacles(const App& app, Obstacles& ob, float reach) {
  for (auto& i : app.doc.instances) {
    const AssetDef* a = app.assetFor(i);
    if (!a || a->path.on || a->nature.kind == NatureDef::SCATTER || isVehicleAsset(a) || isPropLike(app, a)) continue;
    if (a->tree.on && !a->tree.hedge) { Obst o; o.id = i.id; o.c = vec2(i.pos.x, i.pos.z); o.r = a->tree.trunkR + 0.25f; o.mn = o.c - vec2(o.r + reach); o.mx = o.c + vec2(o.r + reach); ob.add(o); continue; }
    if (a->tree.on && a->tree.hedge) { vec2 h = a->footprint * 0.5f; Obst o = boxObst(i.id, i, vec2(-h.x, -h.y * 0.6f), vec2(h.x, h.y * 0.6f), reach); o.soft = true; ob.add(o); continue; }
    if (a->nature.kind == NatureDef::ROCK) { ob.add(boxObst(i.id, i, vec2(a->bmin.x, a->bmin.z) * 0.85f, vec2(a->bmax.x, a->bmax.z) * 0.85f, reach)); continue; }
    if (a->category == "Trees" || a->category == "Bushes" || a->category == "Vegetation") {   // shrubs: their footprint
      vec2 h = a->footprint * 0.4f; Obst o = boxObst(i.id, i, -h, h, reach); o.soft = true; ob.add(o); continue;
    }
    { NavEffect ne = resolveNavEffect(a->navMeta, a->enterable, a->bmin, a->bmax); if (ne == NavEffect::None || ne == NavEffect::Area) continue; }   // walkable for the navmesh too (marsh, flat patches)
    // structures: the placement footprint and the built bounds
    vec2 hf = a->footprint * 0.5f, bmn(a->bmin.x, a->bmin.z), bmx(a->bmax.x, a->bmax.z);
    vec2 mn = glm::min(bmn, -hf), mx = glm::max(bmx, hf);   // footprint and built bounds
    if (mx.x <= mn.x || mx.y <= mn.y) { mn = -hf; mx = hf; }
    Obst o = boxObst(i.id, i, mn, mx, reach);
    o.soft = !a->defense.kind.empty() || a->category == "Defences" || a->category == "Beach obstacles" || a->category == "Dressing";   // wire, fences, tank traps, low walls
    ob.add(o);
  }
}

// ---- grid
struct Grid {
  int W = 0, H = 0; vec2 org{0};
  std::vector<float> cost;      // per metre
  std::vector<uint8_t> blk;     // 1: too near an obstacle, 2: water / steep / outside the build area
  int idx(int x, int z) const { return z * W + x; }
  bool inb(int x, int z) const { return x >= 0 && z >= 0 && x < W && z < H; }
  vec2 centre(int k) const { return org + vec2((k % W + 0.5f) * kCell, (k / W + 0.5f) * kCell); }
  int cellOf(vec2 p) const { int x = (int)std::floor((p.x - org.x) / kCell), z = (int)std::floor((p.y - org.y) / kCell); return inb(x, z) ? idx(x, z) : -1; }
};

bool gRelax = false;   // second pass: soft obstacles (blk 3) passable at a high cost
bool astar(const Grid& g, int s, const std::function<bool(int)>& goal, int target, const std::vector<uint8_t>& routed, float maxCost, std::vector<int>& out) {
  static std::vector<float> G; static std::vector<int> P; static std::vector<uint8_t> closed;
  size_t N = (size_t)g.W * g.H;
  G.assign(N, 1e30f); P.assign(N, -1); closed.assign(N, 0);
  vec2 tc = target >= 0 ? g.centre(target) : vec2(0);
  auto h = [&](int k) {
    if (target < 0) return 0.0f;
    vec2 d = glm::abs(g.centre(k) - tc); return (std::max(d.x, d.y) + 0.41421f * std::min(d.x, d.y)) * 0.4f;
  };
  using QE = std::pair<float, int>;
  std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
  G[s] = 0; pq.push({h(s), s});
  static const int DX[8] = {1, -1, 0, 0, 1, 1, -1, -1}, DZ[8] = {0, 0, 1, -1, 1, -1, 1, -1};
  int found = -1, expansions = 0;
  auto cc = [&](int k) { return g.cost[k] * (routed[k] ? 0.4f : 1.0f) * (g.blk[k] == 3 ? 8.0f : 1.0f); };
  auto bl = [&](int k) { return g.blk[k] != 0 && !(gRelax && g.blk[k] == 3); };
  while (!pq.empty()) {
    auto [f, k] = pq.top(); pq.pop();
    if (closed[k]) continue;
    closed[k] = 1;
    if (k != s && goal(k)) { found = k; break; }
    if (G[k] > maxCost || ++expansions > 900000) break;
    int x = k % g.W, z = k / g.W;
    for (int d = 0; d < 8; ++d) {
      int nx = x + DX[d], nz = z + DZ[d];
      if (!g.inb(nx, nz)) continue;
      int n = g.idx(nx, nz);
      if (closed[n]) continue;
      bool passable = !bl(n) || goal(n);
      if (!passable) continue;
      if (d >= 4 && (bl(g.idx(x + DX[d], z)) || bl(g.idx(x, z + DZ[d]))) && !goal(n)) continue;   // no corner cutting
      float step = (d >= 4 ? 1.41421f : 1.0f) * kCell * 0.5f * (cc(k) + cc(n));
      float ng = G[k] + step;
      if (ng < G[n]) { G[n] = ng; P[n] = k; pq.push({ng + h(n), n}); }
    }
  }
  if (found < 0) return false;
  out.clear();
  for (int k = found; k >= 0; k = P[k]) out.push_back(k);
  std::reverse(out.begin(), out.end());
  return true;
}

struct EntrancePt { uint64_t house = 0; int hidx = 0; vec2 door{0}, out{0}, end{0}; float apronL = kApronL; bool used = false; int endCell = -1; };
struct Chain { std::vector<vec2> pts; int apron = -1; bool spur = false; };   // apron: entrance index whose apron leads this chain
struct Piece { vec2 c{0}; float yaw = 0, len = 0, width = kLinkW, bend = 0, u0 = 0; const char* tag = "link"; };
}  // namespace

bool autopathIsHouse(const AssetDef* a) {
  if (!a || a->navMeta.entrances.empty() || !a->defense.kind.empty() || a->path.on || a->tree.on || a->nature.kind != NatureDef::NONE) return false;
  const std::string& c = a->category;
  if (c.find("Bunker") != std::string::npos || c.find("Atlantic") != std::string::npos || c.find("Defen") != std::string::npos) return false;
  return c.find("House") != std::string::npos || c.find("building") != std::string::npos || c.find("Landmark") != std::string::npos ||
         c.find("Ruin") != std::string::npos || c.find("Farm") != std::string::npos || c.find("Barn") != std::string::npos;
}

std::string AutoPathReport::summary() const {
  char b[320];
  std::snprintf(b, sizeof b, "Auto paths: %d houses, %d aprons, %d links (%d loops, %d spurs, %d through wire / hedges)%s, %d path pieces (%d curved joints), %d routes; %d group%s; %.0f ms",
                houses, aprons, links, loops, spurs, relaxed, failed ? (", " + std::to_string(failed) + " not routable").c_str() : "", pieces, joints, routes,
                components, components == 1 ? "" : "s", ms);
  std::string out = b;
  if (lamps) out += "; " + std::to_string(lamps) + " lamp posts";
  return out;
}

int autopathRemove(App& app) {
  int n = 0;
  auto& v = app.doc.instances;
  v.erase(std::remove_if(v.begin(), v.end(), [&](const Instance& i) {
    bool a = i.extra.is_object() && i.extra.contains("auto") && i.extra["auto"].is_string();
    if (a) { ++n; if (app.selected == i.id) app.selected = 0; }
    return a;
  }), v.end());
  if (app.doc.extra.is_object() && app.doc.extra.contains("routes") && app.doc.extra["routes"].is_array()) {
    json keep = json::array();
    for (auto& r : app.doc.extra["routes"]) if (!(r.is_object() && r.value("auto", false))) keep.push_back(r);
    if (keep.empty()) app.doc.extra.erase("routes"); else app.doc.extra["routes"] = keep;
  }
  if (n) app.dirty = true;
  return n;
}

AutoPathReport autopathConnectHouses(App& app, bool undoStep) {
  auto t0 = std::chrono::high_resolution_clock::now();
  AutoPathReport rep;
  const AssetDef* dirt = app.lib.find("path_dirt");
  if (!dirt || !dirt->path.on) { app.toast("Auto paths: the path_dirt asset is missing."); return rep; }
  if (undoStep) app.pushUndo();
  rep.removed = autopathRemove(app);
  const Terrain& T = app.terrain;
  float hw = kLinkW * 0.5f, need = hw + kClear;
  Obstacles ob; gatherObstacles(app, ob, need + 1.0f);
  // ---- houses and their entrances (aprons)
  std::vector<uint64_t> houses; std::vector<EntrancePt> ents; std::vector<std::vector<int>> houseEnts;
  for (auto& i : app.doc.instances) {
    const AssetDef* a = app.assetFor(i);
    if (!autopathIsHouse(a)) continue;
    int hidx = (int)houses.size(); houses.push_back(i.id); houseEnts.emplace_back();
    quat q = yawQuat(i.yaw);
    for (auto& e : a->navMeta.entrances) {
      if (e.p.y > 1.2f) continue;   // upper-floor doors / balconies
      vec3 dIn = q * vec3(e.dir.x, 0, e.dir.z); vec2 out = -vec2(dIn.x, dIn.z);
      if (glm::length(out) < 1e-3f) continue;
      out = glm::normalize(out);
      vec3 dp = i.pos + q * vec3(e.p.x, 0, e.p.z);
      EntrancePt ep; ep.house = i.id; ep.hidx = hidx; ep.door = vec2(dp.x, dp.z); ep.out = out;
      // long enough to clear its own house (A* starts outside every footprint), short enough to stop before others
      float L = kApronL;
      while (L < 6.0f && ob.clearance(ep.door + out * L, 0) < need + 0.9f && ob.clearance(ep.door + out * L, i.id) >= need) L += 0.25f;
      float Lok = 0;
      for (float s = 0.5f; s <= L + 1e-3f; s += 0.25f) { if (ob.clearance(ep.door + out * s, i.id) < kApronW * 0.5f + kClear) break; Lok = s; }
      if (Lok < 1.0f) continue;
      ep.apronL = Lok; ep.end = ep.door + out * Lok;
      houseEnts.back().push_back((int)ents.size()); ents.push_back(ep);
    }
  }
  rep.houses = (int)houses.size(); rep.entrances = (int)ents.size();
  // ---- grid over the houses (+ margin), clipped to the build area
  Grid g;
  if (!ents.empty()) {
    vec2 mn(1e9f), mx(-1e9f); for (auto& e : ents) { mn = glm::min(mn, e.end); mx = glm::max(mx, e.end); }
    mn -= vec2(kMarginWorld); mx += vec2(kMarginWorld);
    mn = glm::max(mn, vec2(-T.buildHalfX, -T.buildHalfZ)); mx = glm::min(mx, vec2(T.buildHalfX, T.buildHalfZ));
    g.org = mn; g.W = std::max(1, (int)std::ceil((mx.x - mn.x) / kCell)); g.H = std::max(1, (int)std::ceil((mx.y - mn.y) / kCell));
    size_t N = (size_t)g.W * g.H;
    g.cost.assign(N, 1.0f); g.blk.assign(N, 0);
    float cosMax = std::cos(radians(kMaxSlopeDeg));
    for (size_t k = 0; k < N; ++k) {
      vec2 c = g.centre((int)k);
      if (!T.inBuildArea(c.x, c.y) || T.height(c.x, c.y) < T.waterLevel + 0.25f) { g.blk[k] = 2; continue; }
      vec3 n = T.normal(c.x, c.y);
      if (n.y < cosMax) { g.blk[k] = 2; continue; }
      float sl = std::sqrt(std::max(0.0f, 1 - n.y * n.y)) / std::max(0.2f, n.y);   // tan(slope)
      g.cost[k] = 1.0f + 6.0f * sl * sl;
    }
    // obstacles: blocked where the centre is nearer than half width + gap + half a cell diagonal (any point of a free
    // cell then keeps the clearance); existing paths are cheaper
    float pad = need + kCell * 0.71f;
    for (auto& o : ob.v) {
      int x0 = std::max(0, (int)std::floor((o.mn.x - g.org.x) / kCell)), x1 = std::min(g.W - 1, (int)std::floor((o.mx.x - g.org.x) / kCell));
      int z0 = std::max(0, (int)std::floor((o.mn.y - g.org.y) / kCell)), z1 = std::min(g.H - 1, (int)std::floor((o.mx.y - g.org.y) / kCell));
      for (int z = z0; z <= z1; ++z) for (int x = x0; x <= x1; ++x) { int k = g.idx(x, z); if (g.blk[k] != 1 && g.blk[k] != 2 && Obstacles::dist(o, g.centre(k)) < pad) g.blk[k] = o.soft ? 3 : 1; }
    }
    for (auto& i : app.doc.instances) {
      const AssetDef* a = app.assetFor(i); if (!a || !a->path.on) continue;
      vec2 bmn, bmx; pathLocalBounds(app, i, a, bmn, bmx);
      Obst o = boxObst(0, i, bmn, bmx, 0.5f);
      int x0 = std::max(0, (int)std::floor((o.mn.x - g.org.x) / kCell)), x1 = std::min(g.W - 1, (int)std::floor((o.mx.x - g.org.x) / kCell));
      int z0 = std::max(0, (int)std::floor((o.mn.y - g.org.y) / kCell)), z1 = std::min(g.H - 1, (int)std::floor((o.mx.y - g.org.y) / kCell));
      for (int z = z0; z <= z1; ++z) for (int x = x0; x <= x1; ++x) { int k = g.idx(x, z); if (Obstacles::dist(o, g.centre(k)) < 0) g.cost[k] *= 0.6f; }
    }
    if (app.doc.extra.is_object() && app.doc.extra.contains("routes") && app.doc.extra["routes"].is_array())   // the map's own lanes are cheaper
      for (auto& r : app.doc.extra["routes"]) {
        if (!r.is_object() || r.value("auto", false)) continue;
        float hwR = r.value("width", 3.0f) * 0.5f; std::vector<vec2> pts;
        for (auto& p : r.value("pts", json::array())) pts.push_back(vec2(p[0].get<float>(), p[1].get<float>()));
        for (size_t s = 0; s + 1 < pts.size(); ++s) {
          vec2 a = pts[s], b = pts[s + 1], mn2 = glm::min(a, b) - vec2(hwR), mx2 = glm::max(a, b) + vec2(hwR);
          int x0 = std::max(0, (int)std::floor((mn2.x - g.org.x) / kCell)), x1 = std::min(g.W - 1, (int)std::floor((mx2.x - g.org.x) / kCell));
          int z0 = std::max(0, (int)std::floor((mn2.y - g.org.y) / kCell)), z1 = std::min(g.H - 1, (int)std::floor((mx2.y - g.org.y) / kCell));
          vec2 ab = b - a; float L2 = std::max(1e-6f, glm::dot(ab, ab));
          for (int z = z0; z <= z1; ++z) for (int x = x0; x <= x1; ++x) {
            int k = g.idx(x, z); vec2 c = g.centre(k); float t = std::clamp(glm::dot(c - a, ab) / L2, 0.0f, 1.0f);
            if (glm::length(c - (a + ab * t)) < hwR && !(g.cost[k] < 0.75f)) g.cost[k] *= 0.7f;
          }
        }
      }
    for (auto& e : ents) e.endCell = g.cellOf(e.end);
  }
  // ---- MST over houses (shortest entrance pair per house pair), then loop links
  int H = (int)houses.size();
  struct Cand { float d; int a, b, ea, eb; };
  std::vector<Cand> cand;
  for (int a = 0; a < H; ++a) for (int b = a + 1; b < H; ++b) {
    Cand best{1e9f, a, b, -1, -1};
    for (int ea : houseEnts[a]) for (int eb : houseEnts[b]) {
      if (ents[ea].endCell < 0 || ents[eb].endCell < 0) continue;
      float d = glm::length(ents[ea].end - ents[eb].end); if (d < best.d) { best.d = d; best.ea = ea; best.eb = eb; }
    }
    if (best.ea >= 0) cand.push_back(best);
  }
  std::sort(cand.begin(), cand.end(), [](const Cand& x, const Cand& y) { return x.d != y.d ? x.d < y.d : (x.a != y.a ? x.a < y.a : x.b < y.b); });
  // Kruskal with routing: a candidate joins two groups only when A* finds a way (to any entrance of the other house);
  // a pair that cannot be routed leaves the next-shortest candidate to join those groups
  std::vector<uint8_t> routed(g.cost.size(), 0);
  std::vector<std::vector<int>> cellPaths;
  std::vector<int> huf(H); for (int k = 0; k < H; ++k) huf[k] = k;
  std::function<int(int)> hfind = [&](int x) { return huf[x] == x ? x : huf[x] = hfind(huf[x]); };
  std::vector<std::vector<std::pair<int, float>>> tree(H);
  std::set<std::pair<int, int>> linked;
  auto route = [&](const Cand& c) {
    int s = ents[c.ea].endCell, t = ents[c.eb].endCell;
    if (s < 0 || t < 0) return false;
    std::vector<int> goals; for (int e : houseEnts[c.b]) if (ents[e].endCell >= 0) goals.push_back(ents[e].endCell);
    std::vector<int> cp;
    float maxCost = std::max(220.0f, c.d * 8.0f);
    if (!astar(g, s, [&goals](int k) { return std::find(goals.begin(), goals.end(), k) != goals.end(); }, t, routed, maxCost, cp)) return false;
    for (int k : cp) routed[k] = 1;
    cellPaths.push_back(cp); ents[c.ea].used = true;
    for (int e : houseEnts[c.b]) if (ents[e].endCell == cp.back()) ents[e].used = true;
    tree[c.a].push_back({c.b, c.d}); tree[c.b].push_back({c.a, c.d}); linked.insert({c.a, c.b});
    ++rep.links;
    return true;
  };
  for (auto& c : cand) {
    int ra = hfind(c.a), rb = hfind(c.b); if (ra == rb) continue;
    if (route(c)) huf[ra] = rb;
  }
  {   // groups still apart (e.g. a house behind a wire belt): relaxed pass through hedges, wire and defences
    std::set<int> roots; for (int k = 0; k < H; ++k) roots.insert(hfind(k));
    if (roots.size() > 1) {
      gRelax = true;
      for (auto& c : cand) { int ra = hfind(c.a), rb = hfind(c.b); if (ra == rb) continue; if (route(c)) { huf[ra] = rb; ++rep.relaxed; } }
      gRelax = false;
    }
  }
  int loopsMax = std::max(1, H / 5);
  auto treeDist = [&](int a, int b) {
    std::vector<float> d(H, -1); std::vector<int> st{a}; d[a] = 0;
    while (!st.empty()) { int x = st.back(); st.pop_back(); for (auto& [y, w] : tree[x]) if (d[y] < 0) { d[y] = d[x] + w; st.push_back(y); } }
    return d[b];
  };
  for (auto& c : cand) {
    if (rep.loops >= loopsMax || c.d > 70.0f) break;
    if (linked.count({c.a, c.b})) continue;
    float td = treeDist(c.a, c.b);
    if (td > 0 && td > 2.4f * c.d + 15.0f && route(c)) ++rep.loops;
  }
  { std::set<int> roots; for (int k = 0; k < H; ++k) roots.insert(hfind(k)); rep.components = (int)roots.size(); rep.failed = std::max(0, rep.components - 1); }   // failed: groups left unjoined
  // spurs: unused entrances of connected houses join the network when it is near
  for (auto& e : ents) {
    if (e.used || e.endCell < 0) continue;
    bool any = false; for (int k : houseEnts[e.hidx]) any |= ents[k].used;
    if (!any) continue;
    std::vector<int> cp;
    int s = e.endCell;
    if (astar(g, s, [&routed](int k) { return routed[k] != 0; }, -1, routed, 40.0f, cp)) {
      for (int k : cp) routed[k] = 1;
      cellPaths.push_back(cp); e.used = true; ++rep.spurs;
    }
  }
  // ---- one graph of routed cells: polylines between nodes (entrance ends, junctions, dead ends)
  std::unordered_map<int, std::vector<int>> adj;
  auto link = [&](int a, int b) { auto& v = adj[a]; if (std::find(v.begin(), v.end(), b) == v.end()) v.push_back(b); };
  for (auto& cp : cellPaths) for (size_t k = 0; k + 1 < cp.size(); ++k) { link(cp[k], cp[k + 1]); link(cp[k + 1], cp[k]); }
  std::unordered_map<int, int> endEnt;   // cell -> entrance index
  for (int k = 0; k < (int)ents.size(); ++k) if (ents[k].used && ents[k].endCell >= 0) endEnt[ents[k].endCell] = k;
  auto isNode = [&](int c) { auto it = adj.find(c); return endEnt.count(c) || it == adj.end() || it->second.size() != 2; };
  std::set<std::pair<int, int>> done;
  std::vector<Chain> chains;
  std::vector<int> nodeCells; for (auto& [c, v] : adj) if (isNode(c)) nodeCells.push_back(c);
  std::sort(nodeCells.begin(), nodeCells.end());
  std::unordered_map<int, int> nodeDeg;
  for (int c : nodeCells) {
    std::vector<int> nb = adj[c]; std::sort(nb.begin(), nb.end());
    for (int n0 : nb) {
      if (done.count({c, n0})) continue;
      std::vector<int> cells{c}; int prev = c, cur = n0;
      done.insert({c, n0});
      while (true) {
        cells.push_back(cur);
        if (isNode(cur)) break;
        auto& v = adj[cur]; int nxt = v[0] == prev ? v[1] : v[0];
        done.insert({cur, nxt}); prev = cur; cur = nxt;
        if (cells.size() > 200000) break;
      }
      done.insert({cur, prev});
      Chain ch; for (int k : cells) ch.pts.push_back(g.centre(k));
      auto ea = endEnt.find(cells.front()), eb = endEnt.find(cells.back());
      if (ea != endEnt.end()) ch.pts.front() = ents[ea->second].end;
      if (eb != endEnt.end()) ch.pts.back() = ents[eb->second].end;
      if (ea == endEnt.end() && eb != endEnt.end()) { std::reverse(ch.pts.begin(), ch.pts.end()); std::swap(ea, eb); }
      float len = 0; for (size_t k = 0; k + 1 < ch.pts.size(); ++k) len += glm::length(ch.pts[k + 1] - ch.pts[k]);
      if (len < 0.4f && ea == endEnt.end()) continue;
      ch.apron = ea != endEnt.end() ? ea->second : -1;
      chains.push_back(ch);
      nodeDeg[cells.front()]++; nodeDeg[cells.back()]++;
    }
  }
  // ---- smoothing: line-of-sight string pulling with exact clearance, then Chaikin (kept only where clear)
  auto okAt = [&](vec2 p) {
    int k = g.cellOf(p); if (k < 0 || g.blk[k] == 2) return false;
    if (ob.clearance(p, 0, 6.0f, false) < need) return false;
    return g.blk[k] == 3 || ob.clearance(p) >= need;   // soft obstacles: only where a relaxed link already crosses them
  };
  auto clearSeg = [&](vec2 a, vec2 b) {
    float L = glm::length(b - a); int n = std::max(1, (int)std::ceil(L / 0.25f));
    for (int k = 1; k < n; ++k) if (!okAt(glm::mix(a, b, (float)k / n))) return false;
    return true;
  };
  for (auto& ch : chains) {
    auto& P = ch.pts;
    if (P.size() > 2) {
      std::vector<vec2> S{P.front()}; size_t i = 0;
      while (i + 1 < P.size()) {
        size_t j = P.size() - 1;
        while (j > i + 1 && !clearSeg(P[i], P[j])) --j;
        S.push_back(P[j]); i = j;
      }
      P = S;
    }
    for (int it = 0; it < 2 && P.size() > 2; ++it) {   // Chaikin, endpoints fixed
      std::vector<vec2> Q{P.front()};
      for (size_t k = 0; k + 1 < P.size(); ++k) {
        vec2 a = P[k], b = P[k + 1];
        vec2 q1 = glm::mix(a, b, 0.25f), q3 = glm::mix(a, b, 0.75f);
        if (k > 0) Q.push_back(q1);
        if (k + 2 < P.size()) Q.push_back(q3);
      }
      Q.push_back(P.back());
      bool ok = true; for (size_t k = 0; k + 1 < Q.size() && ok; ++k) ok = okAt(Q[k]) && clearSeg(Q[k], Q[k + 1]);
      if (!ok) break;
      P = Q;
    }
    // drop vertices closer than 0.6 m (keeps pieces a sensible length)
    std::vector<vec2> R{P.front()};
    for (size_t k = 1; k + 1 < P.size(); ++k) if (glm::length(P[k] - R.back()) > 1.1f) R.push_back(P[k]);
    if (P.size() > 1) { if (R.size() > 1 && glm::length(P.back() - R.back()) < 1.1f) R.back() = P.back(); else R.push_back(P.back()); }
    P = R;
  }
  // ---- emit: aprons, straight pieces and curved joints; one wear route per chain
  std::vector<Piece> pieces; json routes = json::array();
  auto emitChain = [&](std::vector<vec2> P, int apronEnt, float width) {
    // apron as the first segment when this chain is the only one leaving that entrance
    if (apronEnt >= 0) P.insert(P.begin(), ents[apronEnt].door);
    size_t n = P.size(); if (n < 2) return;
    std::vector<vec2> D(n - 1); std::vector<float> Ls(n - 1);
    for (size_t k = 0; k + 1 < n; ++k) { vec2 d = P[k + 1] - P[k]; Ls[k] = glm::length(d); D[k] = Ls[k] > 1e-4f ? d / Ls[k] : vec2(0, 1); }
    std::vector<float> tIn(n, 0.0f), tOut(n, 0.0f);   // tangent lengths cut from the segment ends at each vertex
    std::vector<Piece> joint(n); std::vector<uint8_t> hasJ(n, 0);
    float minR = width * 0.5f + 0.2f;
    for (size_t v = 1; v + 1 < n; ++v) {
      float th = wrapPi(yawOf(D[v]) - yawOf(D[v - 1]));
      if (std::fabs(th) < radians(4.0f) || std::fabs(th) > radians(150.0f)) continue;
      float tanH = std::tan(std::fabs(th) * 0.5f);
      float cap = 0.45f * std::min(Ls[v - 1] - (v >= 2 ? tOut[v - 1] : 0.0f), Ls[v]);
      float R = std::clamp(4.0f, minR, std::max(minR, cap / std::max(1e-3f, tanH)));
      R = std::max(R, 1.05f / std::fabs(th));   // a joint is at least the path asset 1 m minimum length
      float t = R * tanH;
      if (t > 0.5f * std::min(Ls[v - 1], Ls[v])) continue;   // too sharp for these segments: leave the corner
      // the arc must keep its clearance too (it cuts the inside of the corner)
      vec2 S = P[v] - D[v - 1] * t;
      Piece j; j.width = width; j.len = R * std::fabs(th); j.bend = degrees(th); j.yaw = yawOf(D[v - 1]) + th * 0.5f;
      vec2 c0 = pathCentre(j.len, j.bend, -j.len * 0.5f);
      float cy = std::cos(j.yaw), sy = std::sin(j.yaw);
      auto toW = [&](vec2 l) { return vec2(l.x * cy + l.y * sy, -l.x * sy + l.y * cy); };   // yawQuat(yaw) on xz
      j.c = S - toW(c0);
      bool ok = true;
      for (int k = 0; k <= 6 && ok; ++k) { vec2 l = pathCentre(j.len, j.bend, -j.len * 0.5f + j.len * k / 6.0f); ok = okAt(j.c + toW(l)) || (apronEnt >= 0 && v == 1); }
      if (!ok) continue;
      j.tag = (apronEnt >= 0 && v == 1) ? "apron" : "link";
      tIn[v] = t; tOut[v] = t; joint[v] = j; hasJ[v] = 1;
    }
    // a straight between two cuts shorter than the 1 m minimum would be stretched: give up the larger joint there
    for (bool again = true; again;) {
      again = false;
      for (size_t k = 0; k + 1 < n; ++k) {
        float L = Ls[k] - tOut[k] - tIn[k + 1];
        if (L <= 0.02f || L >= 1.0f || (tOut[k] == 0 && tIn[k + 1] == 0)) continue;
        size_t v = tOut[k] >= tIn[k + 1] ? k : k + 1;
        hasJ[v] = 0; tIn[v] = tOut[v] = 0; again = true;
      }
    }
    float u = 0; json rp = json::array();
    rp.push_back({P[0].x, P[0].y});
    for (size_t k = 0; k + 1 < n; ++k) {
      float a = tOut[k], b = tIn[k + 1];
      float L = Ls[k] - a - b;
      if (L > 0.02f) {
        Piece s; s.width = (apronEnt >= 0 && k == 0) ? kApronW : width; s.len = L; s.yaw = yawOf(D[k]);
        s.c = P[k] + D[k] * (a + L * 0.5f); s.u0 = u + s.len * 0.5f; s.tag = (apronEnt >= 0 && k == 0) ? "apron" : "link";
        pieces.push_back(s); u += L;
      }
      rp.push_back({(P[k] + D[k] * (Ls[k] - b)).x, (P[k] + D[k] * (Ls[k] - b)).y});
      if (k + 1 < n - 1 && hasJ[k + 1]) {
        Piece j = joint[k + 1]; j.u0 = u + j.len * 0.5f; pieces.push_back(j); u += j.len; ++rep.joints;
        float cy = std::cos(j.yaw), sy = std::sin(j.yaw);
        for (int q = 1; q <= 3; ++q) { vec2 l = pathCentre(j.len, j.bend, -j.len * 0.5f + j.len * q / 4.0f); rp.push_back({j.c.x + l.x * cy + l.y * sy, j.c.y - l.x * sy + l.y * cy}); }
      }
    }
    rp.push_back({P[n - 1].x, P[n - 1].y});
    routes.push_back({{"kind", "footpath"}, {"width", width + 0.6f}, {"strength", 0.75f}, {"pts", rp}, {"auto", true}});
  };
  std::vector<uint8_t> apronDone(ents.size(), 0);
  for (auto& ch : chains) {
    int ae = ch.apron;
    bool solo = ae >= 0 && nodeDeg[ents[ae].endCell] == 1 && !apronDone[ae];
    if (solo) apronDone[ae] = 1;
    emitChain(ch.pts, solo ? ae : -1, kLinkW);
  }
  for (size_t k = 0; k < ents.size(); ++k) if (!apronDone[k]) { emitChain({ents[k].end}, (int)k, kApronW); apronDone[k] = 1; }   // apron only
  rep.aprons = (int)ents.size();
  // ---- instances
  for (auto& p : pieces) {
    Instance i; i.id = app.doc.newId(); i.assetId = dirt->id; i.assetVersion = dirt->version;
    i.pos = vec3(p.c.x, T.height(p.c.x, p.c.y), p.c.y); i.yaw = wrapDeg(degrees(p.yaw));
    i.extra = json::object(); i.extra["auto"] = p.tag;
    app.bump(i); app.doc.instances.push_back(i);
    Instance& ref = app.doc.instances.back();
    app.setPathSize(ref, vec2(p.width, p.len));
    setPathShape(ref, PathShape{p.bend, p.u0});
    app.bump(ref);
  }
  rep.pieces = (int)pieces.size();
  if (!routes.empty()) {
    if (!app.doc.extra.is_object()) app.doc.extra = json::object();
    json all = app.doc.extra.contains("routes") && app.doc.extra["routes"].is_array() ? app.doc.extra["routes"] : json::array();
    for (auto& r : routes) all.push_back(r);
    app.doc.extra["routes"] = all; rep.routes = (int)routes.size();
  }
  if (gAutoPathLamps) rep.lamps = lightsLineRoutes(app, lightsRoutesOf(routes), "lamp_lantern_post", 22.0f, "lamp");   // L3 (docs/lights.md)
  app.dirty = true;
  rep.ms = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - t0).count();
  logf("[autopath] %s", rep.summary().c_str());
  return rep;
}

void autopathOnPlaced(App& app, uint64_t id) {
  if (!gAutoPaths || app.mode != Mode::Creative) return;
  const Instance* i = app.doc.find(id); if (!i) return;
  if (!autopathIsHouse(app.assetFor(*i))) return;
  AutoPathReport r = autopathConnectHouses(app, false);   // inside the placement's own undo step
  app.toast(r.summary());
}

void autopathMenu(App& app) {
  if (!ImGui::BeginMenu("Paths")) return;
  int nAuto = 0; for (auto& i : app.doc.instances) nAuto += i.extra.is_object() && i.extra.contains("auto") && i.extra["auto"].is_string();
  if (ImGui::MenuItem("Connect houses")) { AutoPathReport r = autopathConnectHouses(app, true); app.toast(r.summary(), 8); }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Dirt aprons at every house door and footpaths joining the houses (round footprints, water and steep ground).\nRe-running replaces the previous auto paths only. One undo step.");
  if (ImGui::MenuItem("Remove auto paths", nullptr, false, nAuto > 0)) { app.pushUndo(); int n = autopathRemove(app); app.toast("Removed " + std::to_string(n) + " auto path pieces."); }
  ImGui::MenuItem("Auto paths (connect each new house)", nullptr, &gAutoPaths);
  ImGui::MenuItem("Line auto paths with lamp posts", nullptr, &gAutoPathLamps);   // L3: lamp_lantern_post every ~22 m, removed with the paths
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Connect houses also lines the new footpaths with iron lantern posts (lit at dusk and night).");
  ImGui::TextDisabled("%d auto path pieces on the map", nAuto);
  ImGui::EndMenu();
}
