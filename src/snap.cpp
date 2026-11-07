// V6: seamless snapping (sockets, chain base height, joined-end caps and faces, world-space texture origin per chain)
// and crush & flatten under buildings. See snap.h for the data and hooks, docs/plan-visual-physics-overhaul.md (V6).
//
// Socket maths (asset yaw convention, common.h yawQuat): rotating by yaw adds yaw to atan2(x, z) of a direction, so a
// piece's socket s meets a target socket (world P, outward D) when yaw = atan2(-D.x, -D.z) - atan2(s.dir.x, s.dir.y)
// and pos = P - R(yaw) * s.p. The new piece takes the target's base height (pos.y = target.y + t.p.y - s.p.y), so
// the whole chain shares one base and its terrain-cut floors (navSyncCuts: floor = pos.y - depth) line up.
#include "snap.h"
#include "app.h"
#include "physics.h"
#include <algorithm>
#include <unordered_map>
#include <functional>
#include <cstring>

// ------------------------------------------------------------------ settings
namespace {
struct SnapConfig {
  SnapSettings s;
  std::vector<std::pair<std::string, std::string>> compat;
  json extraSockets = json::object();   // asset id -> [{p, dir, kind}]
};
const SnapConfig& cfg() {
  static SnapConfig c = [] {
    SnapConfig k;
    k.s.buildingCategories = {"Houses", "Large buildings", "Small buildings", "Landmarks", "Bunkers", "Ruins", "Ruined buildings"};
    try {
      json j = json::parse(readTextFile(contentDir() / "settings" / "snap.json"));
      k.s.radius = j.value("radius", k.s.radius); k.s.joinTol = j.value("joinTol", k.s.joinTol);
      for (auto& p : j.value("compatible", json::array())) if (p.is_array() && p.size() == 2) k.compat.push_back({p[0].get<std::string>(), p[1].get<std::string>()});
      if (j.contains("assets") && j["assets"].is_object()) k.extraSockets = j["assets"];
      if (j.contains("crush")) {
        const json& c = j["crush"];
        k.s.crushHeight = c.value("maxHeight", k.s.crushHeight); k.s.crushMargin = c.value("margin", k.s.crushMargin); k.s.skirt = c.value("skirt", k.s.skirt);
        if (c.contains("categories")) { k.s.buildingCategories.clear(); for (auto& x : c["categories"]) k.s.buildingCategories.push_back(x.get<std::string>()); }
      }
    } catch (...) { logf("[snap] content/settings/snap.json missing or invalid: defaults"); }
    return k;
  }();
  return c;
}

struct AssetSnap {
  std::vector<SnapSocket> sockets;
  std::vector<int> partCap;   // per part: socket index it caps, -1 = always present
  bool anyCap = false;
};
std::vector<SnapSocket> parseSockets(const json& arr) {
  std::vector<SnapSocket> v;
  if (!arr.is_array()) return v;
  for (auto& s : arr) {
    SnapSocket k;
    if (s.contains("p")) k.p = vec3(s["p"][0].get<float>(), s["p"][1].get<float>(), s["p"][2].get<float>());
    if (s.contains("dir")) { const json& d = s["dir"]; k.dir = d.size() >= 3 ? vec2(d[0].get<float>(), d[2].get<float>()) : vec2(d[0].get<float>(), d[1].get<float>()); if (glm::length(k.dir) > 1e-6f) k.dir = glm::normalize(k.dir); }   // dir: [x, z] or [x, y, z] (y ignored)
    k.kind = s.value("kind", std::string("default"));
    v.push_back(k);
  }
  return v;
}
const AssetSnap& assetSnap(const AssetDef* a) {
  static std::unordered_map<const AssetDef*, AssetSnap> cache;
  auto it = cache.find(a);
  if (it != cache.end()) return it->second;
  AssetSnap s;
  if (a && !a->path.on) {
    if (a->raw.is_object() && a->raw.contains("snap")) s.sockets = parseSockets(a->raw["snap"]);
    else if (cfg().extraSockets.contains(a->id)) s.sockets = parseSockets(cfg().extraSockets[a->id]);
    s.partCap.assign(a->parts.size(), -1);
    if (a->raw.is_object() && a->raw.contains("parts")) {
      const json& pj = a->raw["parts"];
      for (size_t k = 0; k < pj.size() && k < a->parts.size(); ++k)
        if (pj[k].contains("cap")) { s.partCap[k] = pj[k]["cap"].get<int>(); s.anyCap = true; }
    }
  }
  return cache.emplace(a, std::move(s)).first->second;
}
vec3 dir3(vec2 d) { return vec3(d.x, 0, d.y); }
float angDiff(float a, float b) { return std::fabs(std::fmod(a - b + 540.0f, 360.0f) - 180.0f); }

// joins, chain origins (snapSync)
std::unordered_map<uint64_t, uint32_t> gMask;
std::unordered_map<uint64_t, vec2> gOrigin;
}   // namespace

const SnapSettings& snapSettings() { return cfg().s; }
SnapGhost& snapGhost() { static SnapGhost g; return g; }

bool snapCompatible(const std::string& a, const std::string& b) {
  if (a == b) return true;
  for (auto& [x, y] : cfg().compat) if ((x == a && y == b) || (x == b && y == a)) return true;
  return false;
}

std::vector<SnapSocket> snapSockets(const App& app, const AssetDef* a, const Instance* inst) {
  if (!a) return {};
  if (a->path.on) {   // the ends of the path's own length (extra.path), along local Z
    float L = inst ? app.pathSize(*inst, a).y : a->path.length;
    return {SnapSocket{vec3(0, 0, -L * 0.5f), vec2(0, -1), "path"}, SnapSocket{vec3(0, 0, L * 0.5f), vec2(0, 1), "path"}};
  }
  return assetSnap(a).sockets;
}

std::vector<WorldSocket> snapWorldSockets(const App& app, const Instance& i) {
  std::vector<WorldSocket> out;
  const AssetDef* a = app.assetFor(i);
  quat q = yawQuat(i.yaw);
  for (auto& s : snapSockets(app, a, &i)) out.push_back({i.pos + q * s.p, q * dir3(s.dir), s.kind});
  return out;
}

static bool joined(const WorldSocket& a, const WorldSocket& b, float tol) {
  bool drape = a.kind == "path" && b.kind == "path";   // paths drape on the ground: their ends need not share a height
  return glm::length(vec2(a.p.x - b.p.x, a.p.z - b.p.z)) < tol && (drape || std::fabs(a.p.y - b.p.y) < 0.08f) && glm::dot(a.dir, b.dir) < -0.95f && snapCompatible(a.kind, b.kind);
}

// ------------------------------------------------------------------ placement
bool snapPlacement(const App& app, const AssetDef* a, vec3& pos, float& yaw, uint64_t ignoreId, SnapHit* hit) {
  if (!a) return false;
  if (a->path.on) return app.snapPath(a, pos, yaw);                     // path ends (paths.cpp)
  if (a->defense.kind == "sandbag") return app.snapDefense(a, pos, yaw);  // sandbag runs (builder.cpp)
  const auto mine = snapSockets(app, a);
  if (mine.empty()) return false;
  const float R = snapSettings().radius, tol = snapSettings().joinTol;
  struct WS { WorldSocket w; const Instance* inst; int idx; };
  std::vector<WS> all;
  for (auto& o : app.doc.instances) {
    if (o.id == ignoreId) continue;
    auto ws = snapWorldSockets(app, o);
    for (size_t k = 0; k < ws.size(); ++k) all.push_back({ws[k], &o, (int)k});
  }
  float best = 1e9f; vec3 bp = pos; float by = yaw; SnapHit bh;
  quat q0 = yawQuat(yaw);
  for (size_t t = 0; t < all.size(); ++t) {
    const WorldSocket& T = all[t].w;
    if (glm::length(vec2(T.p.x - pos.x, T.p.z - pos.z)) > R + glm::length(a->footprint) + 1.0f) continue;   // far away
    bool taken = false;   // another piece already joins this socket
    for (size_t u = 0; u < all.size() && !taken; ++u) if (all[u].inst != all[t].inst && joined(all[u].w, T, tol)) taken = true;
    if (taken) continue;
    for (size_t s = 0; s < mine.size(); ++s) {
      if (!snapCompatible(mine[s].kind, T.kind)) continue;
      float sy = wrapDeg(degrees(std::atan2(-T.dir.x, -T.dir.z) - std::atan2(mine[s].dir.x, mine[s].dir.y)));
      quat q = yawQuat(sy);
      vec3 sp = T.p - q * mine[s].p;
      vec3 own = pos + q0 * mine[s].p;
      float d = std::min({glm::length(vec2(pos.x - T.p.x, pos.z - T.p.z)), glm::length(vec2(own.x - T.p.x, own.z - T.p.z)), glm::length(vec2(pos.x - sp.x, pos.z - sp.z))});
      if (d > R) continue;
      float score = d + 0.01f * angDiff(sy, yaw);   // nearest socket; between a piece's ends, the one nearest the wanted heading
      if (score < best) { best = score; bp = vec3(sp.x, all[t].inst->pos.y + (T.p.y - all[t].inst->pos.y) - mine[s].p.y, sp.z); by = sy; bh = {all[t].inst->id, (int)s, all[t].idx, d}; }
    }
  }
  if (best > 1e8f) return false;
  pos = bp; yaw = by;
  if (hit) *hit = bh;
  return true;
}

SnapFit snapFit(const App& app, const AssetDef* a, vec3 pos, float yaw, uint64_t ignoreId) {
  SnapFit f;
  if (!a || a->path.on) return f;
  const auto mine = snapSockets(app, a);
  if (mine.empty()) return f;
  quat q = yawQuat(yaw);
  const float tol = snapSettings().joinTol;
  bool haveY = false;
  for (auto& o : app.doc.instances) {
    if (o.id == ignoreId) continue;
    if (glm::length(vec2(o.pos.x - pos.x, o.pos.z - pos.z)) > 60.0f) continue;
    auto ws = snapWorldSockets(app, o);
    bool nb = false;
    for (auto& t : ws)
      for (auto& s : mine) {
        WorldSocket m{pos + q * s.p, q * dir3(s.dir), s.kind};
        m.p.y = t.p.y;   // height is what the fit decides
        if (!joined(m, t, tol)) continue;
        if (!haveY) { f.y = t.p.y - s.p.y; haveY = true; }
        nb = true;
      }
    if (nb) f.neighbours.push_back(o.id);
  }
  if (f.neighbours.empty()) return f;
  f.on = true;
  // grade against the chain's base: the ground under the piece without any terrain cut (the neighbours' cuts open
  // the joint; its own is ignored anyway)
  vec2 hf = a->footprint * 0.5f;
  float top = -1e9f, low = 1e9f;
  const vec3 samples[] = {{0, 0, 0}, {-hf.x, 0, -hf.y}, {hf.x, 0, -hf.y}, {-hf.x, 0, hf.y}, {hf.x, 0, hf.y}, {0, 0, -hf.y}, {0, 0, hf.y}, {-hf.x, 0, 0}, {hf.x, 0, 0}};
  for (auto& s : samples) {
    vec3 w = pos + q * s;
    float h = app.terrain.height(w.x, w.z) + app.terrain.cutDepthAt(w.x, w.z);
    top = std::max(top, h); low = std::min(low, h);
  }
  f.top = std::max(top, f.y); f.low = std::min(low, f.y);
  return f;
}

// ------------------------------------------------------------------ joins (every frame) and seams
uint32_t snapMask(const Instance& i) { auto it = gMask.find(i.id); return it == gMask.end() ? 0u : it->second; }

int snapSync(App& app) {
  static uint64_t lastSig = 0;
  uint64_t sig = 1469598103934665603ull;
  auto mix = [&](uint64_t v) { sig = (sig ^ v) * 1099511628211ull; };
  mix(app.doc.instances.size());
  for (auto& i : app.doc.instances) { uint32_t b[4]; std::memcpy(&b[0], &i.pos.x, 4); std::memcpy(&b[1], &i.pos.y, 4); std::memcpy(&b[2], &i.pos.z, 4); std::memcpy(&b[3], &i.yaw, 4); mix(i.id); mix(i.rev); for (auto x : b) mix(x); }
  if (sig == lastSig) return 0;
  lastSig = sig;
  struct Node { Instance* inst; std::vector<WorldSocket> ws; };
  std::vector<Node> nodes;
  for (auto& i : app.doc.instances) { auto ws = snapWorldSockets(app, i); if (!ws.empty()) nodes.push_back({&i, std::move(ws)}); }
  // spatial hash of sockets (1 m cells)
  std::unordered_map<int64_t, std::vector<std::pair<int, int>>> grid;
  auto cellKey = [](int cx, int cz) { return ((int64_t)cx << 32) ^ (int64_t)(uint32_t)cz; };
  for (int n = 0; n < (int)nodes.size(); ++n)
    for (int k = 0; k < (int)nodes[n].ws.size(); ++k) grid[cellKey((int)std::floor(nodes[n].ws[k].p.x), (int)std::floor(nodes[n].ws[k].p.z))].push_back({n, k});
  std::vector<int> parent(nodes.size());
  for (size_t k = 0; k < parent.size(); ++k) parent[k] = (int)k;
  std::function<int(int)> root = [&](int x) { return parent[x] == x ? x : parent[x] = root(parent[x]); };
  std::vector<uint32_t> mask(nodes.size(), 0);
  const float tol = snapSettings().joinTol;
  for (int n = 0; n < (int)nodes.size(); ++n)
    for (int k = 0; k < (int)nodes[n].ws.size(); ++k) {
      const WorldSocket& a = nodes[n].ws[k];
      int cx = (int)std::floor(a.p.x), cz = (int)std::floor(a.p.z);
      for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) {
        auto it = grid.find(cellKey(cx + dx, cz + dz)); if (it == grid.end()) continue;
        for (auto [m, l] : it->second) {
          if (m == n || !joined(a, nodes[m].ws[l], tol)) continue;
          mask[n] |= 1u << k;
          int ra = root(n), rb = root(m); if (ra != rb) parent[std::max(ra, rb)] = std::min(ra, rb);
        }
      }
    }
  // chain texture origin: the lowest-id piece's position (stable while the chain is edited elsewhere)
  std::unordered_map<int, uint64_t> rootId;
  for (int n = 0; n < (int)nodes.size(); ++n) { int r = root(n); auto it = rootId.find(r); if (it == rootId.end() || nodes[n].inst->id < it->second) rootId[r] = nodes[n].inst->id; }
  std::unordered_map<uint64_t, vec2> idPos;
  for (auto& nd : nodes) idPos[nd.inst->id] = vec2(nd.inst->pos.x, nd.inst->pos.z);
  int changed = 0;
  std::unordered_map<uint64_t, uint32_t> next;
  gOrigin.clear();
  for (int n = 0; n < (int)nodes.size(); ++n) {
    Instance& i = *nodes[n].inst;
    next[i.id] = mask[n];
    vec2 o = idPos[rootId[root(n)]];
    gOrigin[i.id] = vec2(std::floor(o.x), std::floor(o.y));
    uint32_t old = snapMask(i);
    if (old != mask[n]) {
      ++changed;
      const AssetDef* a = app.assetFor(i);
      if (a && !a->path.on && assetSnap(a).anyCap) i.rev = ++app.revCounter;   // the caps' collision changes: rebuild the body
    }
  }
  gMask.swap(next);
  return changed;
}

const GpuMesh* snapMesh(App& app, const AssetDef* a, uint32_t mask, std::vector<vec3>* tris) {
  if (!a || a->path.on) return nullptr;
  const AssetSnap& s = assetSnap(a);
  if (s.sockets.empty()) return nullptr;
  struct Key { const AssetDef* a; uint32_t m; bool operator==(const Key& o) const { return a == o.a && m == o.m; } };
  struct KH { size_t operator()(const Key& k) const { return std::hash<const void*>()(k.a) ^ (k.m * 2654435761u); } };
  static std::unordered_map<Key, std::pair<GpuMesh, std::vector<vec3>>, KH> cache;
  auto it = cache.find({a, mask});
  if (it == cache.end()) {
    MeshData m;
    for (size_t k = 0; k < a->parts.size(); ++k) {
      const Part& p = a->parts[k];
      int cap = k < s.partCap.size() ? s.partCap[k] : -1;
      if (cap >= 0 && (mask >> cap & 1u)) continue;
      if (a->materials[p.mat].pattern == PAT_GLASS) continue;
      appendPart(m, p, a->materials[p.mat], mat4(1));
    }
    // faces lying in a joined end's plane and facing out through it are inside the joint: drop them
    MeshData out; out.v = m.v;
    std::vector<vec3> tv;
    for (size_t t = 0; t + 2 < m.i.size(); t += 3) {
      vec3 p0 = m.v[m.i[t]].pos, p1 = m.v[m.i[t + 1]].pos, p2 = m.v[m.i[t + 2]].pos;
      vec3 n = glm::cross(p1 - p0, p2 - p0); float L = glm::length(n);
      vec3 c = (p0 + p1 + p2) / 3.0f;
      bool drop = false;
      if (L > 1e-9f) {
        n /= L;
        for (size_t k = 0; k < s.sockets.size() && !drop; ++k) {
          if (!(mask >> k & 1u)) continue;
          vec3 d = dir3(s.sockets[k].dir);
          if (glm::dot(n, d) > 0.95f && std::fabs(glm::dot(c - s.sockets[k].p, d)) < 0.03f && glm::length(c - s.sockets[k].p) < 9.0f) drop = true;
        }
      }
      if (drop) continue;
      out.i.insert(out.i.end(), {m.i[t], m.i[t + 1], m.i[t + 2]});
      tv.insert(tv.end(), {p0, p1, p2});
    }
    auto& e = cache[{a, mask}];
    e.first.upload(out); e.second = std::move(tv);
    it = cache.find({a, mask});
  }
  if (tris) *tris = it->second.second;
  return &it->second.first;
}

void snapDraw(App& app, const Instance& i, DrawItem& d) {
  const AssetDef* a = app.assetFor(i);
  if (!a) return;
  auto o = gOrigin.find(i.id);
  if (o == gOrigin.end()) return;   // not a snap piece (or not synced yet)
  d.apWorld = vec4(o->second.x, 0, o->second.y, 1);   // world-space texture coordinates from the chain's origin (kMainVS)
  uint32_t m = snapMask(i);
  if (m && d.mesh == &a->opaque) if (const GpuMesh* v = snapMesh(app, a, m)) d.mesh = v;
}

ShapePtr snapShape(App& app, const Instance& i, const AssetDef* a) {
  if (!a || a->path.on) return nullptr;
  const AssetSnap& s = assetSnap(a);
  uint32_t m = snapMask(i);
  if (!s.anyCap || !m) return nullptr;
  static std::unordered_map<const AssetDef*, std::unordered_map<uint32_t, ShapePtr>> cache;
  ShapePtr& sh = cache[a][m];
  if (!sh) {
    std::vector<Part> col;
    for (size_t k = 0; k < a->parts.size(); ++k) {
      const Part& p = a->parts[k];
      int cap = k < s.partCap.size() ? s.partCap[k] : -1;
      if (cap >= 0 && (m >> cap & 1u)) continue;
      if (p.collide && !p.playerOnly) col.push_back(p);
    }
    sh = Physics::buildShape(col);
  }
  return sh;
}

// ------------------------------------------------------------------ crush & flatten
static bool isVegetationImpl(const AssetDef* b);
bool crushVegetation(const AssetDef* b) { return isVegetationImpl(b); }
static bool isVegetation(const AssetDef* b) { return isVegetationImpl(b); }
static bool isVegetationImpl(const AssetDef* b) {
  if (!b) return false;
  if (b->tree.on && b->tree.hedge) return false;   // hedgerow banks are structures
  if (b->nature.kind == NatureDef::SCATTER) return false;   // flower / grass / stone patches already clear themselves under footprints (nature.cpp)
  return b->tree.on || b->nature.kind != NatureDef::NONE || b->category == "Trees" || b->category == "Bushes" || b->category == "Vegetation" || b->category == "Nature";
}
static float vegHeight(const AssetDef* b) { return std::max(b->bmax.y, b->tree.on ? b->tree.top : 0.0f); }
static bool underFootprint(const AssetDef* a, vec3 pos, float yaw, vec3 p, float margin) {
  vec3 l = glm::inverse(yawQuat(yaw)) * (p - pos);
  vec2 hf = a->footprint * 0.5f + vec2(margin);
  return std::fabs(l.x) <= hf.x && std::fabs(l.z) <= hf.y;
}

bool crushBuilding(const AssetDef* a) {
  if (!a || a->path.on || isVegetation(a)) return false;
  for (auto& c : snapSettings().buildingCategories) if (a->category == c) return true;
  return a->enterable && !a->panels.empty() && a->navMeta.terrainCuts.empty();
}

bool crushBlocked(const App& app, const AssetDef* a, vec3 pos, float yaw, uint64_t ignoreId, std::string& reason) {
  if (!crushBuilding(a)) return false;
  for (auto& o : app.doc.instances) {
    if (o.id == ignoreId) continue;
    const AssetDef* b = app.assetFor(o);
    if (!isVegetation(b) || !underFootprint(a, pos, yaw, o.pos, snapSettings().crushMargin)) continue;
    float h = vegHeight(b);
    if (h > snapSettings().crushHeight) {
      char buf[200]; std::snprintf(buf, sizeof buf, "%s #%llu (%.1f m tall) stands in the footprint: only growth up to %.1f m is crushed.", b->name.c_str(), (unsigned long long)o.id, h, snapSettings().crushHeight);
      reason = buf; return true;
    }
  }
  return false;
}

int crushAndFlatten(App& app, uint64_t id) {
  Instance* in = app.doc.find(id);
  if (!in) return 0;
  const AssetDef* a = app.assetFor(*in);
  if (!crushBuilding(a)) return 0;
  const vec3 pos = in->pos; const float yaw = in->yaw;
  const SnapSettings& S = snapSettings();
  // crush: low growth under the footprint goes
  int removed = 0;
  auto& v = app.doc.instances;
  v.erase(std::remove_if(v.begin(), v.end(), [&](const Instance& o) {
    if (o.id == id) return false;
    const AssetDef* b = app.assetFor(o);
    bool c = isVegetation(b) && underFootprint(a, pos, yaw, o.pos, S.crushMargin) && vegHeight(b) <= S.crushHeight;
    removed += c;
    return c;
  }), v.end());
  // flatten: the ground under the footprint to the base, feathered over the skirt (dug-in assets keep their cuts;
  // Explore keeps its session ground)
  if (app.mode == Mode::Creative && a->navMeta.terrainCuts.empty()) {
    Terrain& T = app.terrain;
    vec2 hf = a->footprint * 0.5f;
    float sk = std::max(0.1f, S.skirt), R = glm::length(hf) + sk, fs = T.fineSpacing(), ts = T.tileSize();
    quat iq = glm::inverse(yawQuat(yaw));
    auto deltas = app.doc.terrainDeltas;
    int tx0 = std::max(0, (int)std::floor((pos.x - R - T.org.x) / ts)), tx1 = std::min(T.tilesX() - 1, (int)std::floor((pos.x + R - T.org.x) / ts));
    int tz0 = std::max(0, (int)std::floor((pos.z - R - T.org.y) / ts)), tz1 = std::min(T.tilesZ() - 1, (int)std::floor((pos.z + R - T.org.y) / ts));
    bool any = false;
    for (int tz = tz0; tz <= tz1; ++tz)
      for (int tx = tx0; tx <= tx1; ++tx) {
        int k = T.key(tx, tz);
        vec2 o = T.tileOrigin(tx, tz);
        const Terrain::Tile* tile = T.tileAt(tx, tz);
        auto dit = deltas.find(k);
        std::vector<float> dv = dit != deltas.end() ? dit->second : std::vector<float>((size_t)Terrain::kFineN * Terrain::kFineN, 0.0f);
        bool touched = false;
        for (int j = 0; j < Terrain::kFineN; ++j)
          for (int i = 0; i < Terrain::kFineN; ++i) {
            float x = o.x + i * fs, z = o.y + j * fs;
            vec3 l = iq * (vec3(x, 0, z) - pos);
            float d = glm::length(vec2(std::max(std::fabs(l.x) - hf.x, 0.0f), std::max(std::fabs(l.z) - hf.y, 0.0f)));
            if (d >= sk) continue;
            if (T.cutDepthAt(x, z) > 0.02f) continue;   // another piece's terrain cut
            float t = d / sk, w = 1.0f - t * t * (3.0f - 2.0f * t);
            size_t idx = (size_t)j * Terrain::kFineN + i;
            float cur = tile ? tile->h[idx] : T.cutBase(x, z);
            float nd = dv[idx] + (pos.y - cur) * w;
            if (std::fabs(nd - dv[idx]) > 0.01f) { dv[idx] = std::fabs(nd) < 1e-4f ? 0.0f : nd; touched = true; }   // ground already within a centimetre stays
          }
        if (!touched) continue;
        any = true;
        bool nz = std::any_of(dv.begin(), dv.end(), [](float x) { return x != 0.0f; });
        if (nz) deltas[k] = dv; else deltas.erase(k);
      }
    if (any) {
      app.doc.terrainDeltas = deltas;
      app.doc.terrainTemplateVersion = app.doc.templateVersion;
      app.resetGround();
      // growth and rocks left in the skirt follow the new ground
      for (auto& o : app.doc.instances) {
        if (o.id == id || !isVegetation(app.assetFor(o)) || !underFootprint(a, pos, yaw, o.pos, sk + 0.5f)) continue;
        float h = T.height(o.pos.x, o.pos.z);
        if (std::fabs(h - o.pos.y) > 1e-3f) { o.pos.y = h; app.bump(o); }
      }
    }
  }
  app.dirty = true;
  return removed;
}
