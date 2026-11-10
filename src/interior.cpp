// Walls, interiors and furniture (docs/interiors.md).
//   * furniture props: simple colliders, small-arms and blast damage with wall shielding, bounded rigid fragments,
//     support loss, session overlay / Apply;
//   * room detection per enterable asset: 0.1 m occupancy grid per storey, doorways, keep-clear volumes (door
//     swings, stairs and landings, windows from the sill up), window sills;
//   * seeded room templates (kitchen / dining, sitting room, bedroom, utility / storage, workshop, hall) placing
//     ordinary furniture instances tagged extra.furnish = {host, slot};
//   * per building copy finish parameters (SurfaceParams) for the wall / floor patterns 17-22.
#include "interior.h"
#include "static_decals.h"
#include "app.h"
#include "lights.h"   // L3: light assets, interior lighting (docs/lights.md)
#include <imgui.h>
#include <array>
#include <chrono>
#include <algorithm>
#include <unordered_map>
#include <limits>
#include <fstream>
#if __has_include("settings.h")   // V2: graphics presets (particle / fragment budget)
#include "settings.h"
#define NMK_V4_SETTINGS 1
#endif

namespace {
uint32_t hmix32(uint32_t h) { h ^= h >> 16; h *= 0x7feb352dU; h ^= h >> 15; h *= 0x846ca68bU; h ^= h >> 16; return h; }
float h01(uint32_t a, uint32_t b = 0, uint32_t c = 0) { return (hmix32(a * 0x9E3779B1u ^ hmix32(b + 0x85ebca6bu) ^ hmix32(c * 0xc2b2ae35u + 7u)) & 0xFFFFFF) / 16777216.0f; }
uint32_t strHash(const std::string& s) { uint32_t h = 2166136261u; for (unsigned char c : s) { h ^= c; h *= 16777619u; } return h; }
void partAabb(const Part& p, vec3& mn, vec3& mx) {
  glm::mat3 R = glm::mat3_cast(p.rot); vec3 h = p.size * 0.5f;
  vec3 e = glm::abs(R[0]) * h.x + glm::abs(R[1]) * h.y + glm::abs(R[2]) * h.z;
  mn = p.center - e; mx = p.center + e;
}
std::string matOf(const AssetDef* a, const Part& p) { return p.mat >= 0 && p.mat < (int)a->materials.size() ? a->materials[p.mat].name : std::string(); }
// AABB (host-local xz offsets and y) of an asset's bounds turned by a multiple of 90 degrees
void turnedBounds(vec3 bmin, vec3 bmax, float yaw, vec3& mn, vec3& mx) {
  quat q = yawQuat(yaw); mn = vec3(1e9f); mx = vec3(-1e9f);
  for (int k = 0; k < 8; ++k) {
    vec3 c((k & 1) ? bmax.x : bmin.x, (k & 2) ? bmax.y : bmin.y, (k & 4) ? bmax.z : bmin.z);
    vec3 w = q * c; mn = glm::min(mn, w); mx = glm::max(mx, w);
  }
}
// the first surface along a ray that can carry weight: not the item itself, not loose debris (fragments, door pieces),
// not a body the ray starts inside
bool isDebris(uint64_t ud) { uint64_t low = ud & 0xFFFF; return ud != 0 && (ud >> 60) != 0xF && (low == 0xFFE0u || low == 0xFFF0u); }
bool supportHit(vec3 o, vec3 d, float len, uint64_t self, Physics::Hit& out) {
  Physics::Hit hs[16]; int n = Physics::castRayAll(o, d, len, hs, 16, false);
  for (int k = 0; k < n; ++k) {
    if (hs[k].ud == self || isDebris(hs[k].ud) || hs[k].t < 1e-3f) continue;
    out = hs[k]; return true;
  }
  return false;
}
bool boxOverlap(vec3 amn, vec3 amx, vec3 bmn, vec3 bmx) { return amn.x < bmx.x && amx.x > bmn.x && amn.y < bmx.y && amx.y > bmn.y && amn.z < bmx.z && amx.z > bmn.z; }
}

bool isRuinedAsset(const std::string& id) {
  if (id.rfind("ruin_", 0) == 0 || id.rfind("rub_", 0) == 0) return true;   // V3 ruins and rubble
  static const char* named[] = {"vil_church_ruined", "vil_chateau_keep", "aw_r612_unfinished", "bch_lcvp_wrecked"};
  for (const char* n : named) if (id == n) return true;
  return false;
}
bool isFurnishPiece(const Instance& i) { return i.extra.is_object() && i.extra.contains("furnish"); }
size_t authoredInstanceCount(const std::vector<Instance>& v) { size_t n = 0; for (auto& i : v) n += !isFurnishPiece(i); return n; }

const char* roomTypeName(int t) {
  static const char* n[] = {"Kitchen / dining", "Sitting room", "Bedroom", "Utility / storage", "Workshop", "Hall / passage",
                            "Church nave", "Barn", "Classroom", "Office", "Tavern room", "Ruined room", "Attic", "Cellar",
                            "Command room", "Radio room", "Crew quarters", "Ammunition store", "Fighting room", "Plant room", "Military passage"};
  return t >= 0 && t < ROOM_TYPES ? n[t] : "Room";
}
const char* roomTypeKey(int t) {
  static const char* n[] = {"kitchen", "sitting", "bedroom", "utility", "workshop", "hall", "church", "barn", "class", "office", "tavern", "ruin", "attic", "cellar", "mil_command", "mil_radio", "mil_quarters", "mil_ammo", "mil_fighting", "mil_plant", "mil_passage"};
  return t >= 0 && t < ROOM_TYPES ? n[t] : "utility";
}

int16_t RoomInfo::at(int s, vec3 p) const {
  if (s < 0 || s >= (int)storeys.size()) return WALL;
  const Storey& S = storeys[s];
  int i = (int)std::floor((p.x - S.o.x) / cell), j = (int)std::floor((p.z - S.o.y) / cell);
  if (i < 0 || j < 0 || i >= S.nx || j >= S.nz) return WALL;
  return S.id[j * S.nx + i];
}

// ============================================================ definitions
const PropDef* InteriorSys::prop(const AssetDef* a) {
  if (!isProp(a)) return nullptr;
  auto& slot = props_[a];
  if (slot) return slot.get();
  slot = std::make_unique<PropDef>();
  PropDef& d = *slot;
  const json& pj = a->raw["prop"];
  d.family = pj.value("family", std::string("wood")); d.rounds = pj.value("rounds", 0); d.blast = pj.value("blast", 40.0f);
  d.blocking = pj.value("blocking", true); d.wall = pj.value("wall", false); d.front = pj.value("front", 0.6f); d.mount = pj.value("mount", 0.0f);
  for (auto& g : pj.value("groups", json::array())) d.groups.push_back(g.get<std::string>());
  for (auto& g : pj.value("fragments", json::array())) d.fragments.push_back(g.get<int>());
  for (auto& g : pj.value("keep", json::array())) d.keep.push_back(g.get<int>());
  d.glass = pj.value("glass", -1);
  for (auto& s : pj.value("surfaces", json::array())) { d.surfaces.push_back(vec4(s[0].get<float>(), s[1].get<float>(), s[2].get<float>(), s[3].get<float>())); d.surfY.push_back(s[4].get<float>()); }
  d.bmin = vec3(1e9f); d.bmax = vec3(-1e9f);
  for (auto& p : a->parts) if (p.visible) { vec3 mn, mx; partAabb(p, mn, mx); d.bmin = glm::min(d.bmin, mn); d.bmax = glm::max(d.bmax, mx); }
  if (d.bmin.x > d.bmax.x) { d.bmin = vec3(-0.2f); d.bmax = vec3(0.2f); }
  if (pj.contains("bounds")) { d.bmin = jvec3(pj["bounds"][0]); d.bmax = jvec3(pj["bounds"][1]); }   // P1: the furnishing envelope, fixed when the look changes
  d.hang = pj.value("hang", false);   // L3 lights (docs/lights.md)
  { std::string cc = pj.value("caster", std::string());   // R2 caster class (interior.h)
    d.smallCaster = cc == "small" || (cc != "full" && !d.blocking && (d.flat() || glm::length(d.bmax - d.bmin) < 1.6f)); }
  d.gmin = vec3(1e9f); d.gmax = vec3(-1e9f);
  for (auto& p : a->parts) if (p.visible && p.grp == d.glass && d.glass >= 0) { vec3 mn, mx; partAabb(p, mn, mx); d.gmin = glm::min(d.gmin, mn); d.gmax = glm::max(d.gmax, mx); }
  // broken state: the keep groups stay (static), with one collider box over them when the prop blocks
  MeshData bm; std::vector<Part> kb;
  for (auto& p : a->parts) {
    if (!p.visible || std::find(d.keep.begin(), d.keep.end(), p.grp) == d.keep.end() || p.grp == d.glass) continue;
    appendPart(bm, p, a->materials[p.mat], mat4(1));
    if (p.size.x * p.size.y * p.size.z > 2e-4f) { Part b = p; b.shape = Part::BOX; b.collide = true; b.visible = false; kb.push_back(b); }   // each kept piece collides on its own
  }
  d.brokenMesh.upload(bm);
  if (d.blocking && !kb.empty()) d.brokenShape = Physics::buildShape(kb);
  // fragments: each listed group as one rigid piece (mesh centred on its bounds, one box shape, mass from volume)
  float density = d.family == "masonry" ? 1600.0f : d.family == "metal" ? 2500.0f : d.family == "ceramic" ? 900.0f : d.family == "cloth" ? 200.0f : 600.0f;
  for (size_t fi = 0; fi < d.fragments.size() && fi < 6; ++fi) {
    int g = d.fragments[fi];
    vec3 mn(1e9f), mx(-1e9f); float vol = 0;
    for (auto& p : a->parts) if (p.visible && p.grp == g) { vec3 a0, a1; partAabb(p, a0, a1); mn = glm::min(mn, a0); mx = glm::max(mx, a1); vol += p.size.x * p.size.y * p.size.z; }
    if (mn.x > mx.x) continue;
    auto f = std::make_unique<PropDef::Frag>();
    f->center = (mn + mx) * 0.5f; f->half = glm::max((mx - mn) * 0.5f, vec3(0.012f));
    MeshData fm; mat4 off = glm::translate(mat4(1), -f->center);
    for (auto& p : a->parts) if (p.visible && p.grp == g) appendPart(fm, p, a->materials[p.mat], off);
    f->mesh.upload(fm);
    Part b; b.center = vec3(0); b.size = f->half * 2.0f; f->shape = Physics::buildShape({b});
    f->mass = std::clamp(vol * density, 0.4f, 60.0f);
    d.frags.push_back(std::move(f));
  }
  return &d;
}

const RoomInfo* InteriorSys::rooms(const AssetDef* a) {
  auto& slot = rooms_[a];
  if (slot) return slot.get();
  slot = std::make_unique<RoomInfo>();
  RoomInfo& ri = *slot;
  if (!a || !a->enterable) return &ri;
  const float C = ri.cell;
  struct B { vec3 mn, mx; };
  std::vector<B> solids, floors, stairs;
  auto isFloorMat = [](const std::string& m) { return m == "floor_x" || m == "terracotta" || m == "concrete_floor" || m == "plank_x" || m == "plank_z"; };
  auto add = [&](const Part& p) {
    vec3 mn, mx; partAabb(p, mn, mx); std::string m = matOf(a, p);
    if (m == "tread_x") stairs.push_back({mn, mx});
    bool flagFloor = (m == "hearthstone" || m == "stone_plinth" || m == "stone_dressed" || m == "earth") && p.size.y <= 0.06f && p.size.x > 1.5f && p.size.z > 1.5f;   // flagged floors (workshop, dairy)
    if (p.visible && (isFloorMat(m) || flagFloor) && p.size.y <= 0.35f && p.size.x > 0.5f && p.size.z > 0.5f) floors.push_back({mn, mx});
    if (!p.collide || p.playerOnly) return;
    solids.push_back({mn, mx});
  };
  for (size_t k = 0; k < a->furnFirst && k < a->parts.size(); ++k) add(a->parts[k]);
  for (auto& pn : a->panels) for (auto& p : pn.parts[0]) add(p);
  std::vector<float> levels;
  for (auto& f : floors) { bool dup = false; for (float l : levels) if (std::fabs(l - f.mx.y) < 0.12f) dup = true; if (!dup) levels.push_back(f.mx.y); }
  std::sort(levels.begin(), levels.end());
  vec2 o(a->bmin.x, a->bmin.z);
  int nx = std::max(1, (int)std::ceil((a->bmax.x - a->bmin.x) / C)), nz = std::max(1, (int)std::ceil((a->bmax.z - a->bmin.z) / C));
  auto cellRange = [&](float lo, float hi, float org, int n, int& i0, int& i1) {   // cells whose centre lies in [lo, hi]
    i0 = std::max(0, (int)std::ceil((lo - org) / C - 0.5f)); i1 = std::min(n - 1, (int)std::floor((hi - org) / C - 0.5f));
  };
  for (size_t s = 0; s < levels.size(); ++s) {
    RoomInfo::Storey S; S.y = levels[s]; S.ceil = s + 1 < levels.size() ? levels[s + 1] - 0.2f : levels[s] + 2.8f;
    if (S.ceil - S.y < 1.9f) continue;   // a landing or step, not a storey
    S.o = o; S.nx = nx; S.nz = nz;
    std::vector<int16_t> id((size_t)nx * nz, RoomInfo::NOFLOOR);
    std::vector<uint8_t> wall((size_t)nx * nz, 0), door((size_t)nx * nz, 0);
    float L = S.y;
    for (auto& f : floors) {
      if (std::fabs(f.mx.y - L) > 0.06f) continue;
      int i0, i1, j0, j1; cellRange(f.mn.x, f.mx.x, o.x, nx, i0, i1); cellRange(f.mn.z, f.mx.z, o.y, nz, j0, j1);
      for (int j = j0; j <= j1; ++j) for (int i = i0; i <= i1; ++i) id[(size_t)j * nx + i] = 0x7FFF;
    }
    for (auto& b : solids) {
      float ext = std::max(b.mx.x - b.mn.x, b.mx.z - b.mn.z);
      bool w = b.mn.y < L + 1.8f && b.mx.y > L + 0.25f, dw = !w && b.mn.y >= L + 1.8f && b.mn.y < L + 2.5f && ext <= 2.2f;   // a lintel, not a beam
      if (!w && !dw) continue;
      int i0, i1, j0, j1; cellRange(b.mn.x, b.mx.x, o.x, nx, i0, i1); cellRange(b.mn.z, b.mx.z, o.y, nz, j0, j1);
      for (int j = j0; j <= j1; ++j) for (int i = i0; i <= i1; ++i) (w ? wall : door)[(size_t)j * nx + i] = 1;
    }
    for (size_t c = 0; c < id.size(); ++c) {
      if (wall[c]) id[c] = RoomInfo::WALL;
      else if (door[c] && id[c] == 0x7FFF) id[c] = RoomInfo::DOORWAY;
    }
    // flood fill the free cells into rooms
    int sIdx = (int)ri.storeys.size();
    std::vector<int> stack;
    for (int j = 0; j < nz; ++j) for (int i = 0; i < nx; ++i) {
      size_t c0 = (size_t)j * nx + i;
      if (id[c0] != 0x7FFF) continue;
      int rIdx = (int)ri.rooms.size();
      std::vector<int> cells; stack.assign(1, (int)c0); id[c0] = (int16_t)rIdx;
      while (!stack.empty()) {
        int c = stack.back(); stack.pop_back(); cells.push_back(c);
        int ci = c % nx, cj = c / nx;
        const int di[4] = {1, -1, 0, 0}, dj[4] = {0, 0, 1, -1};
        for (int d = 0; d < 4; ++d) {
          int ni = ci + di[d], nj = cj + dj[d];
          if (ni < 0 || nj < 0 || ni >= nx || nj >= nz) continue;
          size_t n = (size_t)nj * nx + ni;
          if (id[n] == 0x7FFF) { id[n] = (int16_t)rIdx; stack.push_back((int)n); }
        }
      }
      float area = cells.size() * C * C;
      if (area < 2.5f || ri.rooms.size() >= 32) { for (int c : cells) id[c] = RoomInfo::NOFLOOR; continue; }   // 32: the 5-storey mairie alone has ~20 rooms
      RoomInfo::Room R; R.storey = sIdx; R.y0 = S.y; R.y1 = S.ceil; R.area = area; R.cells = (int)cells.size();
      R.mn = vec2(1e9f); R.mx = vec2(-1e9f);
      for (int c : cells) { vec2 lo(o.x + (c % nx) * C, o.y + (c / nx) * C); R.mn = glm::min(R.mn, lo); R.mx = glm::max(R.mx, lo + vec2(C)); }
      ri.rooms.push_back(R);
    }
    S.id = std::move(id);
    S.keep.assign((size_t)nx * nz, 0);
    ri.storeys.push_back(std::move(S));
  }
  // hearths (fireplaces of the building itself)
  for (size_t k = 0; k < a->furnFirst && k < a->parts.size(); ++k) {
    const Part& p = a->parts[k]; std::string m = matOf(a, p);
    if ((m != "hearthstone" && m != "soot") || p.size.x * p.size.z > 3.0f) continue;   // a hearth, not a flagged floor
    for (auto& R : ri.rooms)
      if (p.center.x > R.mn.x - 0.7f && p.center.x < R.mx.x + 0.7f && p.center.z > R.mn.y - 0.7f && p.center.z < R.mx.y + 0.7f && p.center.y > R.y0 - 0.3f && p.center.y < R.y0 + 1.6f) { R.hearth = true; R.hearthPos = p.center; }
  }
  // keep-clear volumes: door swings, stairs with landings, windows from the sill up
  vec3 mid = (a->bmin + a->bmax) * 0.5f;
  for (auto& d : a->interactables) {
    vec3 mn = d.apMin, mx = d.apMax;
    int n = (mx.x - mn.x) < (mx.z - mn.z) ? 0 : 2, l = 2 - n;
    float w = mx[l] - mn[l];
    if (d.kind == "door" || d.kind == "gate") {
      RoomInfo::Box b; b.kind = 0; b.why = "door " + d.id;
      b.mn = mn; b.mx = mx; b.mn[n] -= w + 0.2f; b.mx[n] += w + 0.2f; b.mn[l] -= 0.1f; b.mx[l] += 0.1f; b.mn.y = mn.y - 0.1f; b.mx.y = mn.y + 2.1f;
      ri.keep.push_back(b);
    } else if (d.kind == "window") {
      float in = ((mn[n] + mx[n]) * 0.5f > mid[n]) ? -1.0f : 1.0f;   // toward the inside
      RoomInfo::Box b; b.kind = 2; b.why = "window " + d.id;
      b.mn = mn; b.mx = mx; b.mn[l] -= 0.1f; b.mx[l] += 0.1f;
      if (in < 0) { b.mn[n] -= 0.7f; b.mx[n] += 0.1f; } else { b.mx[n] += 0.7f; b.mn[n] -= 0.1f; }
      b.mn.y = mn.y - 0.03f; b.mx.y = mx.y;
      ri.keep.push_back(b);
      if (ri.sills.size() < 12) {
        RoomInfo::Sill sl; sl.c = vec3((mn.x + mx.x) * 0.5f, mn.y, (mn.z + mx.z) * 0.5f); sl.halfW = w * 0.5f + 0.05f;
        sl.n = vec2(0); if (n == 0) sl.n.x = -in; else sl.n.y = -in; sl.top = mx.y;
        // the run-off hangs below the outer sill: move the reference to the outer face of the wall
        float outer = in < 0 ? mx[n] : mn[n];
        if (n == 0) sl.c.x = outer; else sl.c.z = outer;
        ri.sills.push_back(sl);
      }
    }
  }
  std::vector<B> clusters;
  for (auto& s : stairs) {
    B b = s; bool merged = true;
    while (merged) {
      merged = false;
      for (size_t k = 0; k < clusters.size(); ++k) {
        B& c = clusters[k];
        if (b.mn.x < c.mx.x + 0.3f && b.mx.x > c.mn.x - 0.3f && b.mn.z < c.mx.z + 0.3f && b.mx.z > c.mn.z - 0.3f && b.mn.y < c.mx.y + 0.4f && b.mx.y > c.mn.y - 0.4f) {
          b.mn = glm::min(b.mn, c.mn); b.mx = glm::max(b.mx, c.mx); clusters.erase(clusters.begin() + k); merged = true; break;
        }
      }
    }
    clusters.push_back(b);
  }
  for (auto& c : clusters) {
    if (c.mx.y - c.mn.y < 0.6f) continue;   // a step, not a stair
    int la = (c.mx.x - c.mn.x) > (c.mx.z - c.mn.z) ? 0 : 2, sa = 2 - la;
    RoomInfo::Box b; b.kind = 1; b.why = "stairs";
    b.mn = c.mn; b.mx = c.mx; b.mn[la] -= 1.0f; b.mx[la] += 1.0f; b.mn[sa] -= 0.25f; b.mx[sa] += 0.25f; b.mn.y -= 0.4f; b.mx.y += 2.2f;
    ri.keep.push_back(b);
  }
  // per storey keep flags: door / stair boxes, and 0.75 m around every doorway cell (internal openings)
  for (size_t s = 0; s < ri.storeys.size(); ++s) {
    RoomInfo::Storey& S = ri.storeys[s];
    for (int j = 0; j < S.nz; ++j) for (int i = 0; i < S.nx; ++i) {
      vec3 p(S.o.x + (i + 0.5f) * C, S.y + 0.5f, S.o.y + (j + 0.5f) * C);
      for (auto& b : ri.keep) if (b.kind != 2 && p.x > b.mn.x && p.x < b.mx.x && p.z > b.mn.z && p.z < b.mx.z && S.y + 1.0f > b.mn.y && S.y < b.mx.y) S.keep[(size_t)j * S.nx + i] = 1;
    }
    const int R = 8;
    std::vector<uint8_t> k2 = S.keep;
    for (int j = 0; j < S.nz; ++j) for (int i = 0; i < S.nx; ++i) {
      if (S.id[(size_t)j * S.nx + i] != RoomInfo::DOORWAY) continue;
      for (int dj = -R; dj <= R; ++dj) for (int di = -R; di <= R; ++di) {
        int ni = i + di, nj = j + dj;
        if (ni < 0 || nj < 0 || ni >= S.nx || nj >= S.nz || di * di + dj * dj > R * R) continue;
        k2[(size_t)nj * S.nx + ni] = 1;
      }
    }
    S.keep.swap(k2);
  }
  // the asset's authored circulation (its walkthrough route through doors, rooms and stairs) stays clear of template
  // furniture: 0.4 m either side of every leg, on the storey the route is on (expectY marks storey changes)
  // (also the W4 / V3 "tour" an asset carries for the structures suite: the same goto / expectY vocabulary)
  static const json kNone = json();
  for (const json* route : {&a->walkthrough, a->raw.is_object() && a->raw.contains("tour") ? &a->raw["tour"] : &kNone}) {
  if (!route->is_object() || ri.storeys.empty()) continue;
  {
    float lvl = ri.storeys[0].y; vec2 prev(0); bool has = false;
    if (route->contains("start") && (*route)["start"].contains("pos")) { vec3 sp = jvec3((*route)["start"]["pos"]); prev = vec2(sp.x, sp.z); has = true; }
    auto markLeg = [&](vec2 p0, vec2 p1, float level) {
      int s = 0; for (size_t q = 0; q < ri.storeys.size(); ++q) if (std::fabs(ri.storeys[q].y - level) < std::fabs(ri.storeys[s].y - level)) s = (int)q;
      auto& S = ri.storeys[s];
      vec2 d = p1 - p0; float L2 = std::max(glm::dot(d, d), 1e-6f);
      for (int j = 0; j < S.nz; ++j) for (int i = 0; i < S.nx; ++i) {
        vec2 c(S.o.x + (i + 0.5f) * C, S.o.y + (j + 0.5f) * C);
        float u = std::clamp(glm::dot(c - p0, d) / L2, 0.0f, 1.0f);
        if (glm::length(c - (p0 + d * u)) < 0.45f) S.keep[(size_t)j * S.nx + i] = 1;   // a 0.9 m corridor for the 0.6 m capsule
      }
    };
    for (auto& s : route->value("steps", json::array())) {
      if (!s.contains("goto") || !s["goto"].is_array() || s["goto"].size() < 2) continue;
      vec2 p(s["goto"][0].get<float>(), s["goto"][1].get<float>());
      if (has) markLeg(prev, p, lvl);
      if (s.contains("expectY")) { lvl = s["expectY"].get<float>(); if (has) markLeg(prev, p, lvl); }
      prev = p; has = true;
    }
  }
  }
  {   // a storey no staircase reaches (a loft over a single-storey cottage) has no rooms
    std::vector<bool> reach(ri.storeys.size(), false);
    for (size_t s = 0; s < ri.storeys.size(); ++s) {
      if (s == 0) { reach[s] = true; continue; }
      for (auto& b : ri.keep) if (b.kind == 1 && b.mx.y >= ri.storeys[s].y - 0.2f && b.mn.y <= ri.storeys[s].y - 1.0f) reach[s] = true;
    }
    std::vector<int> remap(ri.rooms.size(), -1); std::vector<RoomInfo::Room> kept;
    for (size_t k = 0; k < ri.rooms.size(); ++k) if (reach[ri.rooms[k].storey]) { remap[k] = (int)kept.size(); kept.push_back(ri.rooms[k]); }
    for (auto& S : ri.storeys) for (auto& c : S.id) if (c >= 0) c = remap[c] >= 0 ? (int16_t)remap[c] : (int16_t)RoomInfo::NOFLOOR;
    ri.rooms.swap(kept);
  }
  // GX2: headroom of each room on the top storey: the lowest part above it at its centre and quarter points (a sloping
  // roof's bounds come down to the eaves, so a room under the roof slopes reads low there: an attic)
  for (auto& R : ri.rooms) {
    if (R.storey + 1 != (int)ri.storeys.size() || R.storey == 0) continue;
    float lowest = 9.0f;
    for (vec2 f : {vec2(0.5f), vec2(0.25f), vec2(0.75f), vec2(0.25f, 0.75f), vec2(0.75f, 0.25f)}) {
      vec2 q = glm::mix(R.mn, R.mx, f); float top = 9.0f;
      for (size_t k = 0; k < a->furnFirst && k < a->parts.size(); ++k) {
        vec3 mn, mx; partAabb(a->parts[k], mn, mx);
        if (q.x < mn.x || q.x > mx.x || q.y < mn.z || q.y > mx.z || mn.y < R.y0 + 0.3f) continue;
        top = std::min(top, mn.y - R.y0);
      }
      lowest = std::min(lowest, top);
    }
    R.headroom = lowest;
  }
  for (size_t k = 0; k < ri.rooms.size(); ++k) ri.rooms[k].type = roomType(a, ri, (int)k);
  logf("interior: %s: %zu storeys, %zu rooms, %zu keep-clear volumes, %zu sills", a->id.c_str(), ri.storeys.size(), ri.rooms.size(), ri.keep.size(), ri.sills.size());
  return &ri;
}

int InteriorSys::roomType(const AssetDef* a, const RoomInfo& ri, int k) const {
  const RoomInfo::Room& R = ri.rooms[k];
  vec2 e = R.mx - R.mn;
  const std::string& id = a->id;
  auto has = [&](const char* w) { return id.find(w) != std::string::npos; };
  auto largestOnGround = [&]() {   // the biggest ground-floor room (a nave, a tavern room, a classroom)
    int best = -1; float area = 0;
    for (size_t q = 0; q < ri.rooms.size(); ++q) if (ri.rooms[q].storey == 0 && ri.rooms[q].area > area) { area = ri.rooms[q].area; best = (int)q; }
    return best;
  };
  // GX6: an asset may name its rooms' types: "roomTypes": {navRoomId: type}; the nav room holding this room's centre
  if (a->raw.is_object() && a->raw.contains("roomTypes") && a->raw.contains("nav")) {
    static const std::pair<const char*, int> kTypes[] = {{"kitchen", ROOM_KITCHEN}, {"sitting", ROOM_SITTING}, {"bedroom", ROOM_BEDROOM}, {"utility", ROOM_UTILITY},
      {"workshop", ROOM_WORKSHOP}, {"hall", ROOM_HALL}, {"barn", ROOM_BARN}, {"office", ROOM_OFFICE}, {"tavern", ROOM_TAVERN},
      {"mil_command", ROOM_MIL_COMMAND}, {"mil_radio", ROOM_MIL_RADIO}, {"mil_quarters", ROOM_MIL_QUARTERS}, {"mil_ammo", ROOM_MIL_AMMO},
      {"mil_fighting", ROOM_MIL_FIGHTING}, {"mil_plant", ROOM_MIL_PLANT}, {"mil_passage", ROOM_MIL_PASSAGE}};
    vec2 c = (R.mn + R.mx) * 0.5f; float y = R.y0 + 0.5f;
    for (auto& nr : a->raw["nav"].value("rooms", json::array())) {
      if (!nr.contains("min") || !nr.contains("max")) continue;
      vec3 mn = jvec3(nr["min"]), mx = jvec3(nr["max"]);
      if (c.x < mn.x || c.x > mx.x || c.y < mn.z || c.y > mx.z || y < mn.y - 0.3f || y > mx.y) continue;
      std::string t = a->raw["roomTypes"].value(nr.value("id", std::string()), std::string());
      for (auto& [n, v] : kTypes) if (t == n) return v;
    }
    if (id.rfind("mil_", 0) == 0) return ROOM_MIL_PASSAGE;
  }
  if (has("church") || has("chapel")) return k == largestOnGround() && R.area > 12.0f ? ROOM_CHURCH : ROOM_UTILITY;
  if (id.rfind("ruin_", 0) == 0) return ROOM_RUIN;   // V3 ruined buildings: sparse, mostly broken
  if (has("barn") || has("stable")) return ROOM_BARN;
  if (R.y0 < -0.8f && R.area >= 4.0f) return ROOM_CELLAR;                              // GX2: below ground
  if (R.storey > 0 && R.headroom < 2.25f && R.area >= 4.0f) return ROOM_ATTIC;           // GX2: under the roof slopes
  if (has("workshop")) return R.area < 4 ? ROOM_UTILITY : ROOM_WORKSHOP;
  if (has("storehouse") || has("warehouse") || has("dairy")) return ROOM_UTILITY;
  if (std::min(e.x, e.y) < 1.7f || R.cells < (int)(0.55f * (e.x * e.y) / (ri.cell * ri.cell))) return R.area < 4 ? ROOM_UTILITY : ROOM_HALL;
  if (std::min(e.x, e.y) <= 3.0f && std::max(e.x, e.y) >= 3.5f * std::min(e.x, e.y)) return ROOM_HALL;   // GX2: a long narrow corridor (the chateau's 2.2 x 36 m)
  if (R.area < 5.0f) return ROOM_UTILITY;
  if (has("school")) return R.area > 14.0f ? ROOM_CLASS : ROOM_OFFICE;
  if (has("municipal") || has("office") || has("mairie")) return R.storey == 0 && k == largestOnGround() && R.area > 20.0f ? ROOM_HALL : ROOM_OFFICE;
  if (has("inn") || has("cafe") || has("tavern")) { if (R.storey == 0) return k == largestOnGround() ? ROOM_TAVERN : ROOM_KITCHEN; return ROOM_BEDROOM; }
  if (R.storey == 0) {
    int kitchen = -1; float best = 0;   // the hearth room, else the largest ground-floor room
    for (size_t q = 0; q < ri.rooms.size(); ++q) {
      const auto& Q = ri.rooms[q]; vec2 qe = Q.mx - Q.mn;
      if (Q.storey != 0 || std::min(qe.x, qe.y) < 1.7f || Q.area < 5.0f) continue;
      float score = Q.area + (Q.hearth ? 100.0f : 0.0f);
      if (score > best) { best = score; kitchen = (int)q; }
    }
    return kitchen == k ? ROOM_KITCHEN : ROOM_SITTING;
  }
  return ROOM_BEDROOM;
}

bool InteriorSys::hideBuiltin(const Instance& i) const {
  return i.extra.is_object() && i.extra.contains("interior") && i.extra["interior"].value("on", false) && !i.extra["interior"].value("lightsOnly", false);   // L3: a lit bunker keeps its own furniture
}

// ============================================================ automatic furnishing (V4)
bool InteriorSys::autoFurnishable(App& app, const Instance& i) {
  const AssetDef* a = app.lib.find(i.assetId);
  if (!a || !a->enterable || isProp(a)) return false;
  const std::string& id = a->id;
  bool lit = lightsOnlyHost(a) || id.rfind("mil_", 0) == 0;   // L3: bunkers and casemates get caged electric lamps only (their own furniture stays); GX6: mil_ hosts get military rooms
  if (!lit && (id.rfind("bunker_", 0) == 0 || id.rfind("aw_", 0) == 0 || id.rfind("bch_", 0) == 0)) return false;   // bunkers, Atlantic Wall, beach / landing craft
  if (isRuinedAsset(id)) return false;   // ruins, rubble, the chateau keep, unfinished works: left as built (V4)
  if (!lit && (a->category == "Bunkers" || a->category == "Atlantic Wall" || a->category == "Landing craft" || a->category.rfind("Beach", 0) == 0)) return false;
  if (i.extra.is_object()) {
    if (i.extra.value("decor", false)) return false;                                   // V8 decorative border buildings
    if (i.extra.contains("furnish")) return false;
    auto in = i.extra.find("interior");
    if (in != i.extra.end() && in->is_object() && in->contains("on")) return false;    // furnished, or furniture removed by the user
  }
  return !rooms(a)->rooms.empty();
}

int InteriorSys::autoFurnish(App& app, uint64_t hostId) {
  Instance* h = app.doc.find(hostId);
  if (!h || !autoFurnishable(app, *h)) return 0;
  bool dirty = app.dirty;   // furnishing an authored building is not an edit: no undo step, no unsaved-changes prompt
  int n = furnish(app, hostId, 0);
  app.dirty = dirty;
  if (n > 0) ++autoFurnished;
  return n;
}

int InteriorSys::autoFurnishAll(App& app) {
  auto t0 = std::chrono::high_resolution_clock::now();
  std::vector<uint64_t> ids;
  for (auto& i : app.doc.instances) if (autoFurnishable(app, i)) ids.push_back(i.id);
  if (!ids.empty()) { app.syncRuntime(); Physics::optimizeBroadPhase(); }   // the planner's ray / solid queries run on a balanced tree
  int pieces = 0;
  for (uint64_t id : ids) pieces += autoFurnish(app, id);
  if (!ids.empty()) Physics::optimizeBroadPhase();   // the pieces' bodies
  lastAutoMs = (float)std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - t0).count();
  if (!ids.empty()) logf("interior: auto-furnished %zu buildings, %d pieces in %.0f ms", ids.size(), pieces, lastAutoMs);
  return pieces;
}

ShapePtr InteriorSys::staticShape(App& app, const Instance& i, const AssetDef* a) {
  if (const PropDef* pd = prop(a)) {
    PropRT& s = st[i.id];
    s = PropRT{};
    s.applied = i.extra.is_object() && i.extra.value("propBroken", false);
    s.broken = s.applied; ++epoch_;
    return s.broken ? pd->brokenShape : a->shape;
  }
  if (hideBuiltin(i) && a->noFurnShape) return a->noFurnShape;
  (void)app;
  return a->shape;
}

const SurfaceParams* InteriorSys::surfaceFor(App& app, const Instance& i, const AssetDef* a) {
  if (!a || (a->panels.empty() && !a->enterable)) return nullptr;
  SurfEntry& e = surf_[i.id];
  if (e.rev == i.rev && e.rev != 0) return &e.sp;
  e.rev = i.rev;
  SurfaceParams& sp = e.sp; sp = SurfaceParams{};
  json fin = i.extra.is_object() && i.extra.contains("finish") ? i.extra["finish"] : json::object();
  float seed = h01((uint32_t)i.id, 77u);
  sp.inst = vec4(seed, std::clamp(fin.value("wear", 1.0f), 0.0f, 2.0f), std::clamp(fin.value("palette", 1.0f), 0.0f, 1.0f), 0.0f);
  const RoomInfo* ri = rooms(a);
  int n = (int)ri->rooms.size();   // GX5: every room (the first 16 in uniforms, the rest in the decal buffer)
  std::vector<vec4> xA, xB;
  bool civic = a->id.find("municipal") != std::string::npos || a->id.find("school") != std::string::npos;
  for (int k = 0; k < n; ++k) {
    const auto& R = ri->rooms[k];
    float hA = h01((uint32_t)i.id, (uint32_t)k, 3u), hB = h01((uint32_t)i.id, (uint32_t)k, 5u);
    int wall = 0, floor = 0;
    switch (R.type) {
      case ROOM_KITCHEN: wall = hA < 0.5f ? 0 : 4; floor = R.storey == 0 ? (hB < 0.6f ? 2 : 3) : 0; break;
      case ROOM_SITTING: wall = hA < 0.55f ? 3 : 1; floor = 1; break;
      case ROOM_BEDROOM: wall = hA < 0.5f ? 1 : 2; floor = hB < 0.3f ? 1 : 0; break;
      case ROOM_UTILITY: wall = 5; floor = R.storey == 0 ? 4 : 0; break;
      case ROOM_WORKSHOP: wall = 5; floor = 4; break;
      case ROOM_CHURCH: wall = 5; floor = 3; break;
      case ROOM_BARN: case ROOM_RUIN: wall = 5; floor = R.storey == 0 ? 4 : 0; break;
      case ROOM_CLASS: wall = 4; floor = 0; break;
      case ROOM_OFFICE: wall = hA < 0.5f ? 3 : 4; floor = R.storey == 0 ? 1 : 0; break;
      case ROOM_TAVERN: wall = 3; floor = 3; break;
      case ROOM_ATTIC: wall = 5; floor = 0; break;
      case ROOM_CELLAR: wall = 6; floor = 4; break;
      default: wall = 4; floor = R.storey == 0 ? 3 : 0; break;
    }
    if (civic) { wall = R.type == ROOM_UTILITY ? 5 : 4; floor = R.storey == 0 && R.type == ROOM_HALL ? 3 : (hB < 0.5f ? 1 : 0); }
    vec4 A(R.mn.x, R.mn.y, R.mx.x, R.mx.y), B(R.y0, R.y1, (float)wall, (float)floor);
    if (k < 16) { sp.roomA[k] = A; sp.roomB[k] = B; } else { xA.push_back(A); xB.push_back(B); }
  }
  sp.inst.w = (float)std::min(n, 16);
  staticDecalsFor(app.R, i.id, *ri, sp, sp.inst.y, isRuinedAsset(a->id) || sp.inst.y > 1.5f, xA, xB);   // GX5 (static_decals.cpp)
  sp.sills = std::min((int)ri->sills.size(), 12);
  for (int k = 0; k < sp.sills; ++k) {
    const auto& s = ri->sills[k];
    sp.sillA[k] = vec4(s.c.x, s.c.y, s.c.z, s.halfW);
    sp.sillB[k] = vec4(s.n.x, s.n.y, s.top, 0);   // z: window head (GX5 openingNear)
  }
  return &sp;
}

// ============================================================ drawing
// box LOD of a furniture asset (CPU): every rounded box drawn as a plain box, glass and tiny bits left out
const InteriorSys::CpuMesh& InteriorSys::cpuMesh(const AssetDef* a) {
  auto& slot = cpu_[a];
  if (slot) return *slot;
  slot = std::make_unique<CpuMesh>();
  for (auto& p : a->parts) {
    if (!p.visible || p.mat < 0 || p.mat >= (int)a->materials.size()) continue;
    const Material& m = a->materials[p.mat];
    if (m.pattern == PAT_GLASS) continue;
    Part b = p; if (b.shape == Part::ROUND) b.shape = Part::BOX;
    b.bv = 0; if (b.shape == Part::CYL && b.sides < 3) b.sides = 6;   // P1: chamfers and smooth turnings go flat and coarse in the LOD
    vec3 e = p.size;
    if (std::max({e.x, e.y, e.z}) > 0.06f || p.size.x * p.size.y * p.size.z > 2e-5f) appendPart(slot->lod, b, m, mat4(1));   // tiny bits (handles, nails) dropped far away
  }
  const PropDef* pd = prop(a);
  slot->small = pd && !pd->blocking && pd->mount <= 0;
  return *slot;
}

namespace {
void appendXf(MeshData& dst, const MeshData& src, const mat4& M) {
  uint32_t base = (uint32_t)dst.v.size();
  glm::mat3 N = glm::mat3(M);
  dst.v.reserve(dst.v.size() + src.v.size());
  for (const Vertex& v : src.v) { Vertex w = v; w.pos = vec3(M * vec4(v.pos, 1)); w.nrm = glm::normalize(N * v.nrm); dst.v.push_back(w); }
  dst.i.reserve(dst.i.size() + src.i.size());
  for (uint32_t k : src.i) dst.i.push_back(base + k);
}
}

void InteriorSys::rebuildBatch(App& app, uint64_t host, Batch& b, const std::vector<const Instance*>& pieces) {
  MeshData lop, slop;
  vec3 mn(1e9f), mx(-1e9f);
  for (const Instance* i : pieces) {
    const AssetDef* a = app.assetFor(*i);
    const CpuMesh& c = cpuMesh(a);
    mat4 M = app.instanceMatrix(*i);
    appendXf(c.small ? slop : lop, c.lod, M);
    mn = glm::min(mn, i->pos); mx = glm::max(mx, i->pos + vec3(0, 2, 0));
  }
  b.lop.destroy(); b.slop.destroy();
  b.lop.upload(lop); b.slop.upload(slop);
  b.verts = lop.v.size() + slop.v.size();
  b.c = (mn + mx) * 0.5f; b.r = glm::length(mx - mn) * 0.5f + 1.0f;
  b.built = true; ++batchRebuilds;
  (void)host;
}

void InteriorSys::updateBatches(App& app) {
  batchDraws = soloDraws = culledPieces = 0;
  if (!batching) {
    for (auto& [h, b] : batches_) { b.lop.destroy(); b.slop.destroy(); }
    batches_.clear(); member_.clear(); batchLeads.clear(); batchedPieces = 0; batchSig_ = 0;
    return;
  }
  uint64_t sig = ((uint64_t)app.revCounter << 32) ^ ((uint64_t)app.doc.instances.size() << 8) ^ (app.selected * 0x9E3779B97F4A7C15ull) ^ (epoch_ * 0xC2B2AE3D27D4EB4Full);
  if (sig == batchSig_) return;
  batchSig_ = sig; ++batchPasses;
  // which pieces each building's batch holds: intact, unselected, unmoved template pieces (cached per instance revision)
  struct HostOf { uint32_t rev = 0; uint64_t host = 0; };
  static std::unordered_map<uint64_t, HostOf> hostOf;
  std::map<uint64_t, std::pair<uint64_t, std::vector<const Instance*>>> want;
  for (auto& i : app.doc.instances) {
    HostOf& ho = hostOf[i.id];
    if (ho.rev != i.rev) {
      ho.rev = i.rev; ho.host = 0;
      if (i.extra.is_object()) { auto f = i.extra.find("furnish"); if (f != i.extra.end() && f->is_object()) ho.host = f->value("host", (uint64_t)0); }
    }
    if (!ho.host || i.id == app.selected) continue;
    auto rit = app.rt.find(i.id);
    if (rit == app.rt.end() || !prop(rit->second.asset)) continue;
    auto s = st.find(i.id);
    if (s != st.end() && (s->second.broken || s->second.glassBroken || s->second.crushed)) continue;
    auto& w = want[ho.host];
    w.first = (w.first ^ (i.id * 0x9E3779B97F4A7C15ull + i.rev)) * 0x100000001B3ull + 7;
    w.second.push_back(&i);
  }
  if (hostOf.size() > app.doc.instances.size() * 2 + 64) hostOf.clear();   // deleted instances
  for (auto it = batches_.begin(); it != batches_.end();) {
    if (!want.count(it->first)) { it->second.lop.destroy(); it->second.slop.destroy(); it = batches_.erase(it); }
    else ++it;
  }
  member_.clear(); batchLeads.clear(); batchedPieces = 0; batchVerts = 0;
  for (auto& [host, w] : want) {
    if (w.second.size() < 2) continue;   // a lone piece draws on its own
    Batch& b = batches_[host];
    if (!b.built || b.sig != w.first) { b.sig = w.first; b.members.clear(); for (auto* pi : w.second) b.members.push_back(pi->id); rebuildBatch(app, host, b, w.second); }
    for (uint64_t id : b.members) member_[id] = host;
    if (!b.members.empty()) batchLeads.insert(b.members.front());   // P2: the piece that draws its batch (never skipped by distance)
    batchedPieces += (int)b.members.size(); batchVerts += b.verts;
  }
}

bool InteriorSys::propDraw(App& app, const Instance& i, const InstanceRT& r, const mat4& M, std::vector<DrawItem>& op, std::vector<DrawItem>& gl) {
  const PropDef* pd = prop(r.asset);
  if (!pd) return false;
  auto it = st.find(i.id);
  PropRT s = it == st.end() ? PropRT{} : it->second;
  vec3 eye = app.mode == Mode::Explore ? app.eyePos() : app.camPos;
  if (batching && !s.broken && !s.glassBroken && !s.crushed && i.id != app.selected) {
    auto m = member_.find(i.id);
    if (m != member_.end()) {
      auto bt = batches_.find(m->second);
      if (bt != batches_.end() && bt->second.built) {
        Batch& b = bt->second;
        float d = std::max(0.0f, glm::length(eye - b.c) - b.r);
        if (d >= lodDist) {   // far: the merged box LOD, drawn once with the batch's first piece
          if (b.members.front() == i.id && d < cullBig) {
            if (b.lop.count) op.push_back({&b.lop, mat4(1)});
            if (d < cullSmall && b.slop.count) op.push_back({&b.slop, mat4(1)});
            ++batchDraws;
          }
          if (d >= cullBig) ++culledPieces;
          return true;
        }
      }
    }
  }
  // drawn on its own: the same distances (hand-placed dressing outside a building is never culled closer than 75 m)
  float d = glm::length(eye - i.pos);
  bool small = !pd->blocking && pd->mount <= 0;
  bool furnished = i.extra.is_object() && i.extra.contains("furnish");
  if (furnished && d > (small ? cullSmall : cullBig)) { ++culledPieces; return true; }
  // a stable per-piece factor 0.85..1.15: the distance limits below never cut a synchronised ring
  const float jit = 0.85f + 0.3f * (float)((i.id * 0x9E3779B97F4A7C15ull) >> 40 & 0xFFFF) / 65535.0f;
  // R2 propdist: a small decorative piece (non-blocking: never cover) is drawn while it spans >= ~3 px
  if (app.R.perf.propDist && !pd->blocking && app.lodPxPerM > 0 && glm::length(pd->bmax - pd->bmin) * app.lodPxPerM < 3.0f * jit * std::max(d, 1.0f)) { ++culledPieces; ++propDistCulled; return true; }
  bool hidden = false;   // R2 roomcull: furnished pieces only (their host is known)
  if (app.R.perf.roomCull && furnished && !s.broken) {
    auto& ho = hostOf_[i.id];
    if (ho.first != i.rev || !ho.second) { ho.first = i.rev; auto f = i.extra.find("furnish"); ho.second = f != i.extra.end() && f->is_object() ? f->value("host", (uint64_t)0) : 0; }
    hidden = ho.second && interiorHidden(app, ho.second, eye, *pd, M);
  }
  const float castR = pd->smallCaster ? 14.0f * jit : 0.0f;   // R2 castercull: small props cast within ~14 m of the eye
  struct Tag {   // stamps the items this piece pushes below (every return path)
    std::vector<DrawItem>& op; std::vector<DrawItem>& gl; size_t o, g; float c; bool h;
    ~Tag() { for (size_t k = o; k < op.size(); ++k) { op[k].castR = c; op[k].mainHidden = h; } for (size_t k = g; k < gl.size(); ++k) gl[k].mainHidden = h; }
  } tag{op, gl, op.size(), gl.size(), castR, hidden};
  ++soloDraws;
  if (s.broken) {
    if (!pd->brokenMesh.count) return true;
    if (s.crushed) {   // run over by a tank: what is left lies flattened on the floor
      mat4 F = glm::translate(mat4(1), vec3(0, i.pos.y, 0)) * glm::scale(mat4(1), vec3(1, 0.2f, 1)) * glm::translate(mat4(1), vec3(0, -i.pos.y, 0)) * M;
      op.push_back({&pd->brokenMesh, F});
    } else op.push_back({&pd->brokenMesh, M});
    return true;
  }
  op.push_back({&r.asset->opaque, M});
  if (r.asset->glass.count && !s.glassBroken) gl.push_back({&r.asset->glass, M});
  return true;
}

// ============================================================ R2 room culling
const InteriorSys::Openings& InteriorSys::openings(const AssetDef* a) {
  Openings& o = openings_[a];
  if (o.built) return o;
  o.built = true;
  const RoomInfo* ri = rooms(a);
  o.closed = a->enterable && ri && !ri->rooms.empty() && !isRuinedAsset(a->id);
  // roofed: above the centre and quarter points of every room some visible part (the floor above, the roof) covers it;
  // a room open to the sky anywhere there leaves the building uncullable
  if (ri && o.closed) for (auto& R : ri->rooms) {
    for (vec2 f : {vec2(0.5f), vec2(0.25f), vec2(0.75f), vec2(0.25f, 0.75f), vec2(0.75f, 0.25f)}) {
      vec2 q = glm::mix(R.mn, R.mx, f); bool cover = false;
      for (auto& p : a->parts) {
        if (!p.visible || a->materials[p.mat].pattern == PAT_GLASS) continue;
        vec3 mn, mx; partAabb(p, mn, mx);
        if (q.x >= mn.x && q.x <= mx.x && q.y >= mn.z && q.y <= mx.z && mn.y > R.y0 + 1.0f) { cover = true; break; }
      }
      if (!cover) for (auto& pn : a->panels) for (auto& p : pn.parts[0]) {
        vec3 mn, mx; partAabb(p, mn, mx);
        if (q.x >= mn.x && q.x <= mx.x && q.y >= mn.z && q.y <= mx.z && mn.y > R.y0 + 1.0f) { cover = true; break; }
      }
      if (!cover) { o.closed = false; break; }
    }
    if (!o.closed) break;
  }
  const vec3 pad(0.05f);
  for (auto& d : a->interactables) o.boxes.push_back({d.apMin - pad, d.apMax + pad});   // doors, gates, windows, shutters
  for (auto& p : a->parts) if (a->materials[p.mat].pattern == PAT_GLASS) { vec3 mn, mx; partAabb(p, mn, mx); o.boxes.push_back({mn - pad, mx + pad}); }
  for (auto& tg : a->trims) { vec3 mn, mx; int u = 2 - tg.an; mn[tg.an] = tg.d0; mx[tg.an] = tg.d1; mn[u] = tg.u0; mx[u] = tg.u1; mn.y = tg.v0; mx.y = tg.v1; o.boxes.push_back({mn - pad, mx + pad}); }
  // doorway cells of the occupancy grid (gaps under a lintel, with or without a door): each connected run, floor to ceiling
  if (ri) for (auto& S : ri->storeys) {
    std::vector<uint8_t> seen(S.id.size(), 0); std::vector<int> st;
    for (size_t c0 = 0; c0 < S.id.size(); ++c0) {
      if (S.id[c0] != RoomInfo::DOORWAY || seen[c0]) continue;
      int i0 = S.nx, i1 = -1, j0 = S.nz, j1 = -1; st.assign(1, (int)c0); seen[c0] = 1;
      while (!st.empty()) {
        int c = st.back(); st.pop_back(); int ci = c % S.nx, cj = c / S.nx;
        i0 = std::min(i0, ci); i1 = std::max(i1, ci); j0 = std::min(j0, cj); j1 = std::max(j1, cj);
        const int di[4] = {1, -1, 0, 0}, dj[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; ++k) {
          int ni = ci + di[k], nj = cj + dj[k];
          if (ni < 0 || nj < 0 || ni >= S.nx || nj >= S.nz) continue;
          size_t n = (size_t)nj * S.nx + ni;
          if (!seen[n] && S.id[n] == RoomInfo::DOORWAY) { seen[n] = 1; st.push_back((int)n); }
        }
      }
      o.boxes.push_back({vec3(S.o.x + i0 * ri->cell, S.y, S.o.y + j0 * ri->cell) - vec3(0.1f), vec3(S.o.x + (i1 + 1) * ri->cell, S.ceil + 0.2f, S.o.y + (j1 + 1) * ri->cell) + vec3(0.1f)});
    }
  }
  logf("roomcull: %s %s, %zu openings", a->id.c_str(), o.closed ? "closed" : "open (never culled)", o.boxes.size());
  return o;
}

bool InteriorSys::interiorHidden(App& app, uint64_t host, vec3 eye, const PropDef& pd, const mat4& M) {
  HostVis& hv = hostVis_[host];
  if (hv.frame != visFrame_) {   // once per building and frame: the eye's projection and every opening's footprint in it
    hv.frame = visFrame_; hv.all = true; hv.rects.clear();
    auto it = app.rt.find(host);
    const InstanceRT* r = it == app.rt.end() ? nullptr : &it->second;
    const Openings* o = r && r->asset ? &openings(r->asset) : nullptr;
    if (o && o->closed && !r->defDestroyed && hv.rev != r->rev) {   // the building's matrix, again only after an edit
      const Instance* hi = app.doc.find(host);
      if (hi) { hv.rev = r->rev; hv.inv = glm::inverse(app.instanceMatrix(*hi)); } else o = nullptr;
    }
    if (o && o->closed && !r->defDestroyed) {
      hv.ri = rooms(r->asset);
      vec3 le = vec3(hv.inv * vec4(eye, 1)), am = r->asset->bmin, ax = r->asset->bmax;
      // the eye outside the building's (asset-space) box: its largest gap picks a projection axis along which every
      // point of the building lies in front of the eye (in asset space: the building's own axes)
      float best = 0.3f; int bax = -1; float bs = 1;
      for (int k = 0; k < 3; ++k) {
        if (am[k] - le[k] > best) { best = am[k] - le[k]; bax = k; bs = 1; }
        if (le[k] - ax[k] > best) { best = le[k] - ax[k]; bax = k; bs = -1; }
      }
      if (bax >= 0) {
        hv.ax = bax; hv.sgn = bs; hv.all = false;
        std::vector<std::pair<vec3, vec3>> boxes = o->boxes;
        for (size_t k = 0; k < r->asset->panels.size(); ++k)   // damaged / breached bays (roof bays too) are openings
          if ((k < r->state.size() && r->state[k] != PANEL_INTACT) || (k < r->wf.size() && r->wf[k].active)) boxes.push_back({r->asset->panels[k].bmin - vec3(0.1f), r->asset->panels[k].bmax + vec3(0.1f)});
        for (auto& b : boxes) {
          vec4 rc(1e9f, 1e9f, -1e9f, -1e9f); bool ok = true;
          for (int c = 0; c < 8; ++c) {
            vec3 p((c & 1) ? b.second.x : b.first.x, (c & 2) ? b.second.y : b.first.y, (c & 4) ? b.second.z : b.first.z);
            vec3 d = p - le; float z = d[bax] * bs;
            if (z < 0.02f) { ok = false; break; }
            vec2 q(d[(bax + 1) % 3] / z, d[(bax + 2) % 3] / z);
            rc = vec4(glm::min(vec2(rc), q), glm::max(vec2(rc.z, rc.w), q));
          }
          if (!ok) { hv.all = true; break; }   // an opening reaching past the eye's plane: no doubt allowed
          hv.rects.push_back(rc);
        }
      }
    }
  }
  if (hv.all) return false;
  ++roomTested;
  mat4 L = hv.inv * M;   // piece -> building space
  vec3 c = vec3(L * vec4((pd.bmin + pd.bmax) * 0.5f, 1));
  bool inRoom = false;   // only a piece standing inside one of the building's rooms
  for (size_t s = 0; s < hv.ri->storeys.size() && !inRoom; ++s) { auto& S = hv.ri->storeys[s]; if (c.y > S.y - 0.1f && c.y < S.ceil + 0.3f) inRoom = hv.ri->at((int)s, c) >= 0; }
  if (!inRoom) return false;
  vec3 le = vec3(hv.inv * vec4(eye, 1));
  vec4 rc(1e9f, 1e9f, -1e9f, -1e9f);
  for (int k = 0; k < 8; ++k) {
    vec3 p = vec3(L * vec4((k & 1) ? pd.bmax.x : pd.bmin.x, (k & 2) ? pd.bmax.y : pd.bmin.y, (k & 4) ? pd.bmax.z : pd.bmin.z, 1)) - le;
    float z = p[hv.ax] * hv.sgn;
    if (z < 0.02f) return false;
    vec2 q(p[(hv.ax + 1) % 3] / z, p[(hv.ax + 2) % 3] / z);
    rc = vec4(glm::min(vec2(rc), q), glm::max(vec2(rc.z, rc.w), q));
  }
  // every ray from the eye into the closed building passes an opening: the piece's footprint must overlap one
  for (auto& o : hv.rects) if (rc.x <= o.z && o.x <= rc.z && rc.y <= o.w && o.y <= rc.w) return false;
  ++roomHidden;
  return true;
}

void InteriorSys::fragDrawItems(std::vector<DrawItem>& op) {
  for (auto& f : frags) {
    if (!f.mesh) continue;
    if (!f.frozen) { vec3 p; quat q; if (Physics::getTransform(f.body, p, q)) f.m = glm::translate(mat4(1), p) * glm::mat4_cast(q); }
    op.push_back({f.mesh, f.m});
  }
}

// ============================================================ damage
static std::string surfaceOf(const PropDef* pd) {
  if (pd->family == "masonry") return "masonry";
  if (pd->family == "metal") return "metal";
  if (pd->family == "ceramic") return "ceramic";
  if (pd->family == "glass") return "glass";
  if (pd->family == "cloth" || pd->family == "paper") return "cloth";
  return "wood";
}

int InteriorSys::bulletHit(App& app, uint64_t iid, vec3 at, vec3 dir, std::string& surface) {
  auto it = app.rt.find(iid);
  if (it == app.rt.end()) return 0;
  const PropDef* pd = prop(it->second.asset);
  if (!pd) return 0;
  Instance* inst = app.doc.find(iid);
  if (!inst) return 0;
  PropRT& s = st[iid];
  surface = surfaceOf(pd);
  if ((pd->family == "masonry" || pd->family == "metal") && pd->glass >= 0 && !s.broken && !s.glassBroken) {   // L3: a lantern's glass panes in an iron frame
    vec3 lp = glm::inverse(yawQuat(inst->yaw)) * (at - inst->pos);
    if (glm::all(glm::greaterThan(lp, pd->gmin - vec3(0.04f))) && glm::all(glm::lessThan(lp, pd->gmax + vec3(0.04f)))) {
      s.glassBroken = true; ++glassBreaks; ++epoch_; app.audio.play("glass_break", at, 0.6f); app.fx.impact(at, -dir, "glass"); return 2;
    }
  }
  if (pd->family == "masonry" || pd->family == "metal") { app.fx.impact(at, -dir, pd->family); return 1; }   // stone, cast iron: rounds stop
  if (!s.broken) {
    if (pd->glass >= 0 && !s.glassBroken) { s.glassBroken = true; ++glassBreaks; ++epoch_; app.audio.play("glass_break", at, 0.6f); app.fx.impact(at, -dir, "glass"); }
    if (pd->rounds > 0) {
      s.hits += 1.0f;
      app.fx.impact(at, -dir, surface == "wood" ? "wood" : surface);
      if (s.hits >= (float)pd->rounds) breakProp(app, *inst, it->second, dir * 2.5f, "small arms");
    }
  }
  return 2;
}

void InteriorSys::freezeOldest() {
  size_t live = 0; for (auto& f : frags) live += !f.frozen;
  for (auto& f : frags) {
    if (live <= maxLive) break;
    if (f.frozen) continue;
    vec3 p; quat q;
    if (Physics::getTransform(f.body, p, q)) f.m = glm::translate(mat4(1), p) * glm::mat4_cast(q);
    Physics::remove(f.body); f.body = 0xffffffffu; f.frozen = true; --live;
  }
  while (frags.size() > maxKept) { if (!frags.front().frozen) Physics::remove(frags.front().body); frags.pop_front(); }
}

float InteriorSys::budgetScale() const {
#ifdef NMK_V4_SETTINGS
  return std::clamp(settings().particleBudget(), 0.25f, 4.0f);   // Low / Medium / High presets (V2)
#else
  return 1.0f;
#endif
}

bool InteriorSys::breakProp(App& app, Instance& i, InstanceRT& r, vec3 push, const char* why, int violence) {
  const PropDef* pd = prop(r.asset);
  if (!pd) return false;
  PropRT& s = st[i.id];
  if (s.broken) return false;
  s.broken = true; s.glassBroken = true; ++breaks; ++epoch_;
  lastBreak = r.asset->id + " #" + std::to_string(i.id) + " (" + why + ")";
  Physics::remove(r.staticBody); r.staticBody = 0xffffffffu;
  if (pd->brokenShape) r.staticBody = Physics::addStatic(pd->brokenShape, i.pos, yawQuat(i.yaw), i.id << 16);
  mat4 M = app.instanceMatrix(i);
  quat q = yawQuat(i.yaw);
  vec3 c = vec3(M * vec4((pd->bmin + pd->bmax) * 0.5f, 1));
  // bounded rigid fragments with capped speed; family-coloured particles for the fine debris. A violent break (grenade,
  // direct shell hit, tank) throws every fragment group (up to six), an ordinary one at most four. Over the live budget a
  // fragment is not spawned as a body (its share goes to the splinters), unless the oldest live one has had time to settle.
  float sp = std::min(glm::length(push), 7.0f);
  vec3 dirN = sp > 1e-3f ? push / std::max(glm::length(push), 1e-4f) : vec3(0, -1, 0);
  uint32_t seed = (uint32_t)i.id * 2654435761u ^ (uint32_t)breaks * 40503u;
  float scale = budgetScale();
  maxLive = (size_t)std::max(8.0f, std::round(32.0f * scale)); maxKept = (size_t)std::max(40.0f, std::round(240.0f * scale));
  int n = 0, cap = violence ? 6 : 4, dropped = 0;
  for (auto& f : pd->frags) {
    if (n >= cap) break;
    size_t live = 0; for (auto& g : frags) live += !g.frozen;
    if (live >= maxLive) {
      Frag* oldest = nullptr; for (auto& g : frags) if (!g.frozen) { oldest = &g; break; }
      if (oldest && oldest->age > 0.8f) {
        vec3 p; quat qq; if (Physics::getTransform(oldest->body, p, qq)) oldest->m = glm::translate(mat4(1), p) * glm::mat4_cast(qq);
        Physics::remove(oldest->body); oldest->body = 0xffffffffu; oldest->frozen = true;
      } else { ++dropped; ++n; continue; }
    }
    vec3 wp = vec3(M * vec4(f->center, 1)) + vec3(0, 0.03f, 0);
    wp.y = std::max(wp.y, app.terrain.height(wp.x, wp.z) + glm::length(f->half) + 0.01f);   // clear of uneven ground
    Frag fr; fr.inst = i.id; fr.mesh = &f->mesh;
    fr.body = Physics::addDynamic(f->shape, wp, q, (i.id << 16) | 0xFFE0u, f->mass);
    vec3 jit(h01(seed, n, 1) - 0.5f, h01(seed, n, 2) * 0.6f, h01(seed, n, 3) - 0.5f);
    vec3 v = dirN * sp * (0.6f + 0.4f * h01(seed, n, 4)) + jit * (violence ? 2.6f : 1.6f) + vec3(0, sp > 0.5f ? 1.2f : 0.0f, 0);
    v.y = std::max(v.y, 0.3f);   // never driven down into the floor / ground
    if (glm::length(v) > 7.0f) v = glm::normalize(v) * 7.0f;
    Physics::setVelocity(fr.body, v, vec3(h01(seed, n, 5) - 0.5f, h01(seed, n, 6) - 0.5f, h01(seed, n, 7) - 0.5f) * (violence ? 9.0f : 6.0f));
    Physics::setDamping(fr.body, 0.2f, 0.4f);
    Physics::setMaterial(fr.body, 0.7f, 0.15f, true);   // continuous collision: no tunnelling through floors
    fr.m = glm::translate(mat4(1), wp) * glm::mat4_cast(q);
    frags.push_back(fr); ++n; ++fragsSpawned;
  }
  fragsDropped += dropped;
  size_t live = 0; for (auto& f : frags) live += !f.frozen;
  maxLiveSeen = std::max(maxLiveSeen, (int)std::min(live, maxLive));
  freezeOldest();
  vec4 col = pd->family == "ceramic" ? vec4(0.8f, 0.78f, 0.72f, 0.7f) : pd->family == "glass" ? vec4(0.7f, 0.75f, 0.75f, 0.35f)
           : pd->family == "masonry" ? vec4(0.55f, 0.52f, 0.46f, 0.7f) : pd->family == "metal" ? vec4(0.2f, 0.2f, 0.2f, 0.6f)
           : pd->family == "cloth" || pd->family == "paper" ? vec4(0.62f, 0.58f, 0.5f, 0.45f) : vec4(0.42f, 0.33f, 0.22f, 0.65f);
  int puffs = (int)std::round((violence ? 14 : 10) * std::min(scale, 1.5f));
  for (int k = 0; k < puffs; ++k) {
    vec3 d(h01(seed, k, 11) - 0.5f, h01(seed, k, 12), h01(seed, k, 13) - 0.5f);
    app.fx.smoke(c + d * 0.3f, (d * 2.5f + dirN * sp * 0.3f), 0.05f + 0.06f * h01(seed, k, 14), 0.5f + h01(seed, k, 15), col, 0.6f, 0.1f);
  }
  // splinters (wood, paint), chips (ceramic, masonry), shards (glass), shreds (cloth, paper); dust when it is blown apart
  float size = glm::length(pd->bmax - pd->bmin);
  int chips = (int)std::round((violence ? 26 : 9 + 3 * dropped) * std::clamp(size, 0.4f, 2.0f) * std::min(scale, 1.5f));
  vec4 chipCol = pd->family == "glass" ? vec4(0.75f, 0.8f, 0.8f, 0.6f) : pd->family == "ceramic" ? vec4(0.85f, 0.83f, 0.77f, 0.9f)
               : vec4(vec3(col) * 0.8f, std::min(1.0f, col.a * 1.4f));
  app.fx.splinters(c, dirN * std::max(sp, 2.0f) * (violence ? 1.3f : 0.8f), chips, chipCol, (pd->family == "wood" || pd->family == "painted") ? 0.022f : 0.012f);
  if (violence || dropped) app.fx.dust(c, std::clamp(size * 0.5f, 0.4f, 1.4f), vec4(0.55f, 0.51f, 0.45f, 0.35f));
  ++splinterBursts;
  if (pd->family == "glass" || pd->glass >= 0) app.fx.flash(c, 0.12f, 0.04f, vec4(2, 2, 2, 1));
  app.fx.impact(c, vec3(0, 1, 0), pd->family == "masonry" ? "masonry" : pd->family == "metal" ? "metal" : "wood");
  app.audio.play(pd->family == "ceramic" || pd->family == "glass" ? "glass_break" : "door_break", c, pd->family == "cloth" || pd->family == "paper" ? 0.2f : 0.6f);
  static int depth = 0;
  if (!inBlast_ && depth < 4) { ++depth; checkSupport(app, c, std::max(1.5f, size)); --depth; }   // what stood on it falls (a blast checks once, at its end)
  return true;
}

void InteriorSys::tankContact(App& app, Instance& i, InstanceRT& r, vec3 at, vec3 dir, float speed) {
  const PropDef* pd = prop(r.asset);
  if (!pd) return;
  PropRT& s = st[i.id];
  if (s.crushed) return;
  vec3 d = glm::length(vec3(dir.x, 0, dir.z)) > 1e-4f ? glm::normalize(vec3(dir.x, 0, dir.z)) : vec3(0, 0, 1);
  if (!s.broken) breakProp(app, i, r, d * std::clamp(speed * 1.5f + 1.5f, 2.0f, 6.0f) + vec3(0, 1.0f, 0), "crushed by a tank", 1);
  // what is left goes flat under the tracks: no collision, drawn squashed
  s.crushed = true; ++crushed; ++epoch_;
  Physics::remove(r.staticBody); r.staticBody = 0xffffffffu;
  app.fx.splinters(at, d * 2.0f + vec3(0, 1.5f, 0), 10, vec4(0.36f, 0.28f, 0.19f, 0.9f), 0.02f);
}

bool InteriorSys::supported(App& app, const Instance& i, const AssetDef* a, int* hitsOut) {
  const PropDef* pd = prop(a);
  if (!pd) return true;
  quat q = yawQuat(i.yaw);
  uint64_t self = i.id << 16;
  if (pd->hang) {   // L3: hung from a ceiling or beam: something solid right above its top
    vec3 c = i.pos + q * vec3((pd->bmin.x + pd->bmax.x) * 0.5f, pd->bmax.y - 0.03f, (pd->bmin.z + pd->bmax.z) * 0.5f);
    Physics::Hit h;
    bool ok = Physics::castRayEx(c, vec3(0, 1, 0), 0.15f, h, self) && !isDebris(h.ud);
    if (hitsOut) *hitsOut = ok ? 5 : 0;
    return ok;
  }
  if (pd->mount > 0) {   // wall-hung: something solid right behind it
    bool ok = false;
    for (float fx : {0.5f, 0.1f, 0.9f}) {
      vec3 c = i.pos + q * vec3(glm::mix(pd->bmin.x, pd->bmax.x, fx), (pd->bmin.y + pd->bmax.y) * 0.5f, pd->bmin.z + 0.02f);
      Physics::Hit h;
      if (Physics::castRayEx(c, q * vec3(0, 0, -1), 0.14f, h, self)) { ok = true; break; }
    }
    if (hitsOut) *hitsOut = ok ? 5 : 0;
    return ok;
  }
  vec3 lo = pd->bmin, hi = pd->bmax;
  float ix = (hi.x - lo.x) * 0.3f, iz = (hi.z - lo.z) * 0.3f;
  vec3 pts[5] = {vec3((lo.x + hi.x) * 0.5f, 0, (lo.z + hi.z) * 0.5f), vec3(lo.x + ix, 0, lo.z + iz), vec3(hi.x - ix, 0, lo.z + iz), vec3(lo.x + ix, 0, hi.z - iz), vec3(hi.x - ix, 0, hi.z - iz)};
  int hits = 0;
  for (auto& p : pts) {
    vec3 o = i.pos + q * p + vec3(0, lo.y + 0.06f, 0);
    Physics::Hit h, h2;
    if (Physics::castRayEx(o, vec3(0, -1, 0), 0.2f, h, self) && (!isDebris(h.ud) || supportHit(o, vec3(0, -1, 0), 0.2f, self, h2))) ++hits;   // debris holds nothing up
    else if (app.terrain.height(o.x, o.z) > o.y - 0.2f) ++hits;
  }
  if (hitsOut) *hitsOut = hits;
  return hits >= (pd->flat() ? 2 : 3);
}

int InteriorSys::checkSupport(App& app, vec3 near, float radius) {
  int fell = 0;
  for (int pass = 0; pass < 3; ++pass) {   // a table going takes the things on it with it
    int now = 0;
    for (auto& i : app.doc.instances) {
      auto it = app.rt.find(i.id); if (it == app.rt.end()) continue;
      if (!prop(it->second.asset) || st[i.id].broken) continue;
      if (glm::length(i.pos - near) > radius) continue;
      if (supported(app, i, it->second.asset)) continue;
      if (breakProp(app, i, it->second, vec3(0, -1.5f, 0), "lost its support")) { ++now; ++falls; }
    }
    fell += now;
    if (!now) break;
  }
  return fell;
}

void InteriorSys::blast(App& app, vec3 p, vec3 dir, float dmg, float radius, int kind) {
  auto t0 = std::chrono::high_resolution_clock::now();
  int b0 = breaks;
  float R = radius * 1.4f + 0.3f;
  // a grenade's fragments shred furniture far better than its blast moves walls; a shell or rocket bursting on a piece
  // blows that piece apart (direct hit) whatever its rating
  float mul = kind == WALL_GRENADE ? 2.2f : 1.0f;
  inBlast_ = true;
  vec3 d = glm::length(dir) > 1e-4f ? glm::normalize(dir) : vec3(0, -1, 0);
  vec3 from = p - d * 0.06f;   // just in front of the struck surface
  bool any = false;
  for (auto& i : app.doc.instances) {
    auto it = app.rt.find(i.id); if (it == app.rt.end()) continue;
    const PropDef* pd = prop(it->second.asset);
    if (!pd || st[i.id].broken) continue;
    quat q = yawQuat(i.yaw);
    vec3 lp = glm::inverse(q) * (p - i.pos);
    float dist = glm::length(glm::clamp(lp, pd->bmin, pd->bmax) - lp);
    if (dist > R) continue;
    // occlusion: walls, floors and ground shield completely, other furniture by half
    vec3 c = i.pos + q * ((pd->bmin + pd->bmax) * 0.5f), top = i.pos + q * vec3((pd->bmin.x + pd->bmax.x) * 0.5f, pd->bmax.y - 0.03f, (pd->bmin.z + pd->bmax.z) * 0.5f);
    float vis = dist < 0.15f ? 1.0f : 0.0f;
    for (vec3 to : {c, top}) {
      vec3 dv = to - from; float len = glm::length(dv);
      if (len < 0.05f) { vis = 1; break; }
      Physics::Hit h;
      float v = 1.0f;
      if (Physics::castRayEx(from, dv / len, len - 0.04f, h, i.id << 16)) {
        uint64_t hid = h.ud >> 16, low = h.ud & 0xFFFF;
        bool ground = h.ud == 0 || (h.ud >> 60) == 0xF;
        auto ht = app.rt.find(hid);
        bool furn = !ground && low == 0 && ht != app.rt.end() && prop(ht->second.asset);
        bool frag = !ground && (low == 0xFFE0u || low == 0xFFF0u);
        v = furn ? 0.5f : frag ? 0.8f : 0.0f;
      }
      vis = std::max(vis, v);
    }
    if (vis <= 0) { ++shielded; continue; }
    float e = dmg * mul * std::pow(std::max(0.0f, 1.0f - dist / R), 1.5f) * vis;
    bool direct = dist < 0.15f && kind != WALL_GRENADE && kind != WALL_BULLET && pd->family != "masonry";
    if (dist < 0.15f) e = std::max(e, dmg * mul * vis);
    if (direct) { e = std::max(e, pd->blast * 1.01f); ++directHits; }
    PropRT& s = st[i.id];
    if (pd->glass >= 0 && !s.glassBroken && e > pd->blast * 0.15f) { s.glassBroken = true; ++glassBreaks; ++epoch_; }
    if (e >= pd->blast) {
      vec3 push = c - p; float pl = glm::length(push);
      push = (pl > 1e-3f ? push / pl : d) * (2.0f + 5.0f * std::min(1.0f, e / (pd->blast * 3.0f)));
      int violent = kind == WALL_GRENADE || direct || e > pd->blast * 2.5f ? 1 : 0;
      if (breakProp(app, i, it->second, push, direct ? "direct hit" : kind == WALL_GRENADE ? "grenade" : kind == WALL_AP ? "AP shot" : kind == WALL_ROCKET ? "rocket" : "HE blast", violent)) any = true;
    }
  }
  inBlast_ = false;
  checkSupport(app, p, R + 3.0f);   // floors and tables brought down by this blast drop what stood on them
  if (any && kind == WALL_GRENADE) app.fx.dust(p + vec3(0, 0.5f, 0), std::min(R * 0.5f, 2.5f), vec4(0.5f, 0.47f, 0.42f, 0.3f));   // the room fills with dust
  lastBlastBreaks = breaks - b0;
  lastBlastMs = (float)std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - t0).count();
}

void InteriorSys::step(App& app, float dt) {
  lightsStep(app, dt);   // L3: flames of lit fires near the camera
  for (auto& f : frags) {
    if (f.frozen) continue;
    f.age += dt;
    { vec3 p; quat q; if (Physics::getTransform(f.body, p, q) && p.y < app.terrain.height(p.x, p.z) - 1.5f) f.age = 99; }   // lost below the ground: retire
    // sleep -> frozen: kept as a visual where it lies, with no collision, so debris never blocks a route
    if (f.age > 6.0f || (f.age > 1.2f && !Physics::isActive(f.body))) {
      vec3 p; quat q;
      if (Physics::getTransform(f.body, p, q)) f.m = glm::translate(mat4(1), p) * glm::mat4_cast(q);
      Physics::remove(f.body); f.body = 0xffffffffu; f.frozen = true;
    }
  }
}

void InteriorSys::reset(App& app) {
  for (auto& f : frags) if (!f.frozen) Physics::remove(f.body);
  frags.clear();
  for (auto& i : app.doc.instances) {
    auto it = app.rt.find(i.id); if (it == app.rt.end() || !prop(it->second.asset)) continue;
    auto s = st.find(i.id);
    bool applied = i.extra.is_object() && i.extra.value("propBroken", false);
    if (s != st.end() && (s->second.broken != applied || s->second.glassBroken || s->second.crushed || s->second.hits > 0)) app.buildRT(i, it->second);
  }
}

int InteriorSys::applyToMap(App& app) {
  int n = lightsApplyToMap(app);   // L3: lights put out this session are saved off
  for (auto& i : app.doc.instances) {
    auto it = app.rt.find(i.id); if (it == app.rt.end() || !prop(it->second.asset)) continue;
    auto s = st.find(i.id);
    if (s == st.end() || !s->second.broken || s->second.applied) continue;
    if (!i.extra.is_object()) i.extra = json::object();
    i.extra["propBroken"] = true; i.rev = ++app.revCounter; ++n;
  }
  return n;
}

void InteriorSys::clearApplied(App& app) {
  lightsClearApplied(app);   // L3
  for (auto& i : app.doc.instances)
    if (i.extra.is_object() && i.extra.contains("propBroken")) { i.extra.erase("propBroken"); i.rev = ++app.revCounter; }
}

// ============================================================ placement
Instance* InteriorSys::hostAt(App& app, vec3 w, uint64_t ignoreId) {
  for (auto& o : app.doc.instances) {
    if (o.id == ignoreId) continue;
    const AssetDef* b = app.assetFor(o);
    if (!b->enterable) continue;
    vec3 lp = glm::inverse(yawQuat(o.yaw)) * (w - o.pos);
    if (lp.x > b->bmin.x && lp.x < b->bmax.x && lp.z > b->bmin.z && lp.z < b->bmax.z && lp.y > b->bmin.y - 0.5f && lp.y < b->bmax.y) return &o;
  }
  return nullptr;
}

bool InteriorSys::hoverPoint(App& app, vec3 ro, vec3 rd, vec3& out, uint64_t ignoreId) {
  Physics::Hit h;
  if (!Physics::castRayEx(ro, rd, 3000.0f, h, ignoreId ? (ignoreId << 16) : ~0ull)) return false;
  vec3 g; bool tg = app.terrain.raycast(ro, rd, 3000, g);
  if (tg && glm::length(g - ro) < h.t - 0.01f) return false;
  out = h.pos;
  return true;
}

PlaceCheck InteriorSys::checkPlacement(App& app, const AssetDef* a, vec3 pos, float yaw, uint64_t ignoreId) {
  PlaceCheck c; c.y = pos.y;
  const PropDef* pd = prop(a);
  if (!app.terrain.inBuildArea(pos.x, pos.z)) { c.ok = false; c.reason = "Outside the build area."; return c; }
  quat q = yawQuat(yaw);
  uint64_t self = ignoreId ? (ignoreId << 16) : ~0ull;
  // support height: the first surface below the cursor point (a floor, the ground, or a table top for small props)
  Physics::Hit h; float ground = app.terrain.height(pos.x, pos.z);
  float y = ground;
  bool onProp = false;
  bool got = Physics::castRayEx(pos + vec3(0, 0.08f, 0), vec3(0, -1, 0), 1.5f, h, self);
  if (got && (h.t < 1e-3f || isDebris(h.ud))) got = supportHit(pos + vec3(0, 0.08f, 0), vec3(0, -1, 0), 1.5f, self, h);   // started inside a chair standing on a rug, or on debris
  if (got && h.pos.y > ground - 0.01f) {
    y = h.pos.y;
    uint64_t hid = h.ud >> 16; auto ht = app.rt.find(hid);
    onProp = (h.ud & 0xFFFF) == 0 && ht != app.rt.end() && prop(ht->second.asset);
  }
  if (onProp && pd->blocking) { c.ok = false; c.reason = a->name + " must stand on a floor, not on other furniture."; return c; }
  if (pd->hang) {   // L3: hung by its top from the ceiling or beam above the cursor point
    Physics::Hit uh;   // from the floor point, or from just under a point already at the ceiling
    if (!Physics::castRayEx(vec3(pos.x, std::max(y + 0.3f, pos.y - 0.25f), pos.z), vec3(0, 1, 0), 8.0f, uh, self)) { c.ok = false; c.reason = a->name + " hangs from a ceiling or a beam: nothing above it."; return c; }
    y = uh.pos.y - pd->bmax.y - 0.004f;
  }
  if (pd->mount > 0) {   // wall-hung: at its height above the floor, flat against the wall behind
    y += pd->mount;
    vec3 back = q * vec3(0, 0, -1);
    vec3 o = vec3(pos.x, y + (pd->bmin.y + pd->bmax.y) * 0.5f, pos.z) - back * 0.3f;
    Physics::Hit wh;
    if (!Physics::castRayEx(o, back, 0.9f, wh, self)) { c.ok = false; c.reason = a->name + " hangs on a wall: no wall behind it."; return c; }
    vec3 snap = wh.pos - back * (0.004f - pd->bmin.z);
    pos.x = snap.x; pos.z = snap.z; c.snapXZ = true; c.xz = vec2(snap.x, snap.z);
  }
  c.y = y;
  Instance probe; probe.id = ignoreId; probe.pos = vec3(pos.x, y, pos.z); probe.yaw = yaw;
  int hits = 0;
  if (!supported(app, probe, a, &hits)) { c.ok = false; c.reason = pd->mount > 0 ? "No wall behind it." : "Unsupported: it would hang over an edge or a stair opening."; return c; }
  // solid: no wall, building part or other blocking furniture inside it
  vec3 ext = pd->bmax - pd->bmin;
  if (!pd->flat()) {
    vec3 he = glm::max(ext * 0.5f - vec3(0.02f, 0.04f, 0.02f), vec3(0.01f));
    if (!pd->blocking) he *= 0.5f;
    vec3 cen = probe.pos + q * ((pd->bmin + pd->bmax) * 0.5f) + vec3(0, 0.02f, 0);
    if (pd->mount > 0) cen += q * vec3(0, 0, 0.02f);
    if (Physics::boxBlocked(cen, he, q, self)) { c.ok = false; c.reason = "Intersects a wall or other solid object."; return c; }
  }
  // keep-clear volumes of the building it stands in: doors, stairs, windows (above the sill)
  if (Instance* host = hostAt(app, probe.pos + vec3(0, 0.3f, 0))) {
    const AssetDef* ha = app.assetFor(*host);
    const RoomInfo* ri = rooms(ha);
    quat hq = glm::inverse(yawQuat(host->yaw));
    vec3 mn(1e9f), mx(-1e9f);
    for (int k = 0; k < 8; ++k) {
      vec3 cc((k & 1) ? pd->bmax.x : pd->bmin.x, (k & 2) ? pd->bmax.y : pd->bmin.y, (k & 4) ? pd->bmax.z : pd->bmin.z);
      vec3 lp = hq * (probe.pos + q * cc - host->pos); mn = glm::min(mn, lp); mx = glm::max(mx, lp);
    }
    mn += vec3(0.03f); mx -= vec3(0.03f);
    for (auto& b : ri->keep) {
      if (!pd->blocking && b.kind != 1) continue;   // cosmetic pieces may lie in a door swing or under a window, never on stairs
      if (!boxOverlap(mn, mx, b.mn, b.mx)) continue;
      c.ok = false; c.reason = std::string("Blocks the ") + (b.kind == 0 ? "door swing (" + b.why.substr(5) + ")" : b.kind == 1 ? "stairs or their landing" : "window (" + b.why.substr(7) + ")") + ".";
      return c;
    }
  }
  if (pd->blocking && app.doc.start.set) {
    vec3 s = glm::inverse(q) * (app.doc.start.pos - probe.pos);
    if (s.x > pd->bmin.x - 0.35f && s.x < pd->bmax.x + 0.35f && s.z > pd->bmin.z - 0.35f && s.z < pd->bmax.z + 0.35f && std::fabs(app.doc.start.pos.y - probe.pos.y) < 1.5f) {
      c.ok = false; c.reason = "Blocks the explore start marker."; return c;
    }
  }
  c.reason = "Valid placement.";
  return c;
}

// ============================================================ room templates
namespace {
struct PlanItem { std::string slot, asset; vec3 p{0}; float yaw = 0; bool broken = false; };
struct Planner {
  App& app; InteriorSys& sys; Instance& host; const AssetDef* ha; const RoomInfo& ri; uint32_t seed; float density, clutter, wear;
  std::vector<std::vector<uint8_t>> occ;   // per storey cell: 0 free, 1 furniture, 2 clearance in front of furniture
  std::vector<PlanItem> out;
  std::vector<std::pair<vec3, vec3>> boxes;   // placed blocking items (host-local AABB)
  int calls = 0;
  std::string failed;   // slots that found no place (the furnish log)
  std::unordered_map<uint64_t, float> faceCache;   // wall-face probes per (storey, cell, side, height): one ray each per furnish
  float rnd(uint32_t a, uint32_t b = 0) { return h01(seed, a, b); }
  std::string why; const char* cellWhy = "";   // GX2: why the last tryAt refused (debug log)
  bool zoneOn = false; vec2 zmn{0}, zmx{0};   // GX2: wall() / center() restricted to one zone of an oversized room
  static constexpr float walkway = 0.9f;   // GX2: kept clear round a centre-floor group
  bool inZone(vec3 p) const { return zoneDist(p) <= 2.5f; }   // a zone is soft: spots up to 2.5 m outside it rank behind those in it
  float zoneDist(vec3 p) const { if (!zoneOn) return 0.0f; vec2 d = glm::max(glm::max(zmn - vec2(p.x, p.z), vec2(p.x, p.z) - zmx), vec2(0)); return glm::length(d); }
  const AssetDef* A(const char* id) { return app.lib.find(id); }
  vec3 world(vec3 lp) const { return host.pos + yawQuat(host.yaw) * lp; }
  bool rectCells(int s, int room, vec2 mn, vec2 mx, int mode) {   // mode 0: free; 1: not furniture (clearance); 2: room only
    const auto& S = ri.storeys[s]; const float C = ri.cell;
    int i0 = (int)std::floor((mn.x - S.o.x) / C), i1 = (int)std::ceil((mx.x - S.o.x) / C) - 1;
    int j0 = (int)std::floor((mn.y - S.o.y) / C), j1 = (int)std::ceil((mx.y - S.o.y) / C) - 1;
    if (i0 < 0 || j0 < 0 || i1 >= S.nx || j1 >= S.nz) { cellWhy = "grid"; return false; }
    for (int j = j0; j <= j1; ++j) for (int i = i0; i <= i1; ++i) {
      size_t c = (size_t)j * S.nx + i;
      if (S.id[c] != room) { cellWhy = "room"; return false; }
      if ((mode < 2 || mode == 3) && S.keep[c]) { cellWhy = "keep"; return false; }   // 3: the room, clear of doors / stairs / routes, furniture below allowed
      if (mode == 0 && occ[s][c]) { cellWhy = occ[s][c] == 1 ? "furniture" : "clearance"; return false; }
      if (mode == 1 && occ[s][c] == 1) { cellWhy = "furniture"; return false; }
    }
    return true;
  }
  void mark(int s, vec2 mn, vec2 mx, uint8_t v) {
    const auto& S = ri.storeys[s]; const float C = ri.cell;
    int i0 = std::max(0, (int)std::floor((mn.x - S.o.x) / C)), i1 = std::min(S.nx - 1, (int)std::ceil((mx.x - S.o.x) / C) - 1);
    int j0 = std::max(0, (int)std::floor((mn.y - S.o.y) / C)), j1 = std::min(S.nz - 1, (int)std::ceil((mx.y - S.o.y) / C) - 1);
    for (int j = j0; j <= j1; ++j) for (int i = i0; i <= i1; ++i) { auto& o = occ[s][(size_t)j * S.nx + i]; o = std::max(o, v); }
  }
  bool windowFree(vec3 mn, vec3 mx) {
    for (auto& b : ri.keep) if (b.kind == 2 && boxOverlap(mn, mx, b.mn, b.mx)) return false;
    return true;
  }
  bool solidFree(const AssetDef* fa, const PropDef* pd, vec3 p, float yaw) {   // physics: the building's own parts
    ++calls;
    if (pd->flat()) return true;
    quat q = yawQuat(host.yaw + yaw);
    vec3 he = glm::max((pd->bmax - pd->bmin) * 0.5f - vec3(0.02f, 0.04f, 0.02f), vec3(0.01f));
    if (!pd->blocking) he *= 0.5f;
    vec3 c = world(p) + q * ((pd->bmin + pd->bmax) * 0.5f) + vec3(0, 0.02f, 0);
    if (pd->mount > 0) c += q * vec3(0, 0, 0.02f);
    (void)fa;
    return !Physics::boxBlocked(c, he, q);
  }
  // footprint (host-local) of an item at p / yaw: xz rect and y range
  void bounds(const PropDef* pd, vec3 p, float yaw, vec3& mn, vec3& mx) { turnedBounds(pd->bmin, pd->bmax, yaw, mn, mx); mn += p; mx += p; }
  bool tryAt(int k, const char* asset, const std::string& slot, vec3 p, float yaw, bool hard, bool front, int mode, PlanItem* got) {
    const AssetDef* fa = A(asset); if (!fa) { why = "asset"; return false; }
    const PropDef* pd = sys.prop(fa);
    const auto& R = ri.rooms[k];
    vec3 mn, mx; bounds(pd, p, yaw, mn, mx);
    bool hung = pd->mount > 0;
    // wall-hung pieces (pictures, racks, pegs, clocks) only need their own room and wall: they may hang above a
    // dresser or a bench (3D boxes below), never in a door swing or over a stair
    // (a solid one -- a wall shelf, a plate rack -- also keeps out of the route corridors and doorway rings: it is at head height)
    {   // a hung piece's back lies in the 0.1 m cell the wall face cuts through: test the cells just in front of it
      vec3 sh = hung ? yawQuat(yaw) * vec3(0, 0, 0.06f) : wallBacked ? yawQuat(yaw) * vec3(0, 0, 0.12f) : vec3(0);
      if (!rectCells(R.storey, k, vec2(mn.x + sh.x, mn.z + sh.z), vec2(mx.x + sh.x, mx.z + sh.z), hung ? (pd->blocking ? 3 : 2) : mode)) { why = std::string("cells:") + cellWhy; return false; }
    }
    // the manual placement rule's exact tests (checkPlacement), so a generated piece never fails them: the keep cells
    // above are 0.1 m cells marked by their centres, and a stair box can reach a few cm into a cell it does not mark
    for (auto& b : ri.keep) {
      if (b.kind == 2 || (!hung && !pd->blocking && b.kind != 1)) continue;   // cosmetic floor pieces may lie in a door swing
      if (hung ? boxOverlap(mn, mx, b.mn, b.mx) : boxOverlap(mn + vec3(0.03f), mx - vec3(0.03f), b.mn, b.mx)) { why = "keep " + b.why; return false; }
    }
    if (pd->blocking && app.doc.start.set) {
      vec3 s = glm::inverse(yawQuat(host.yaw + yaw)) * (app.doc.start.pos - world(p));
      if (s.x > pd->bmin.x - 0.35f && s.x < pd->bmax.x + 0.35f && s.z > pd->bmin.z - 0.35f && s.z < pd->bmax.z + 0.35f && std::fabs(app.doc.start.pos.y - world(p).y) < 1.5f) { why = "start"; return false; }
    }
    if ((pd->blocking || hung) && !windowFree(mn, mx)) { why = "window"; return false; }
    for (auto& b : boxes) if (boxOverlap(mn + vec3(0.01f), mx - vec3(0.01f), b.first, b.second)) { why = "piece"; return false; }
    vec3 fmn(0), fmx(0);
    if (front && pd->front > 0) {   // access clearance in front of it must stay walkable
      quat q = yawQuat(yaw); vec3 f = q * vec3(0, 0, 1);
      vec3 a0 = mn, a1 = mx;
      if (std::fabs(f.x) > 0.5f) { if (f.x > 0) { a0.x = mx.x; a1.x = mx.x + pd->front; } else { a1.x = mn.x; a0.x = mn.x - pd->front; } }
      else { if (f.z > 0) { a0.z = mx.z; a1.z = mx.z + pd->front; } else { a1.z = mn.z; a0.z = mn.z - pd->front; } }
      if (!rectCells(R.storey, k, vec2(a0.x, a0.z), vec2(a1.x, a1.z), 1)) { why = "front"; return false; }
      fmn = a0; fmx = a1;
    }
    if (!solidFree(fa, pd, p, yaw)) { why = "solid"; return false; }
    if (hung) {
      Instance probe; probe.id = 0; probe.pos = world(p); probe.yaw = wrapDeg(host.yaw + yaw);
      if (!sys.supported(app, probe, fa)) { why = "unsupported"; return false; }
    }
    if (hard && pd->blocking && !hung) { mark(R.storey, vec2(mn.x, mn.z), vec2(mx.x, mx.z), 1); boxes.push_back({mn, mx}); }
    else if (hung || (hard && !pd->flat())) boxes.push_back({mn, mx});   // buckets, baskets, pictures: never two in one place
    if (front && pd->front > 0) mark(R.storey, vec2(fmn.x, fmn.z), vec2(fmx.x, fmx.z), 2);
    PlanItem it; it.slot = slot; it.asset = asset; it.p = p; it.yaw = yaw;
    out.push_back(it);
    if (got) *got = it;
    return true;
  }
  // GX2: the pose of a piece backed onto the wall next to cell (i, j) in direction d (0: the wall at -z, 1: -x, 2: +z,
  // 3: +x): its yaw faces into the room and its back stands `gap` off the real wall face (probed by a ray, cached), so
  // floor pieces stand on the skirting board's front and hung pieces sit on the plaster
  bool wallBehind(int s, int i, int j, int d) const {
    const auto& S = ri.storeys[s];
    const int di[4] = {0, -1, 0, 1}, dj[4] = {-1, 0, 1, 0};
    auto id = [&](int a, int b) { return a < 0 || b < 0 || a >= S.nx || b >= S.nz ? (int16_t)RoomInfo::NOFLOOR : S.id[(size_t)b * S.nx + a]; };
    int16_t n1 = id(i + di[d], j + dj[d]);
    return n1 == RoomInfo::WALL || (n1 == RoomInfo::NOFLOOR && id(i + 2 * di[d], j + 2 * dj[d]) == RoomInfo::WALL);
  }
  bool wallBacked = false;   // tryAt: the piece stands backed onto a wall (its back may overhang a no-floor strip)
  bool wallCand(int k, const PropDef* pd, int i, int j, int d, float lift, vec3& p, float& yaw) {
    const auto& R = ri.rooms[k]; const auto& S = ri.storeys[R.storey]; const float C = ri.cell;
    const int di[4] = {0, -1, 0, 1}, dj[4] = {-1, 0, 1, 0};
    const float yaws[4] = {0, 90, 180, 270};
    int ni = i + di[d], nj = j + dj[d];
    if (i < 0 || j < 0 || i >= S.nx || j >= S.nz || S.id[(size_t)j * S.nx + i] != k) return false;
    (void)ni; (void)nj;
    if (!wallBehind(R.storey, i, j, d)) return false;
    vec3 mn, mx; turnedBounds(pd->bmin, pd->bmax, yaws[d], mn, mx);
    p = vec3(S.o.x + (i + 0.5f) * C, R.y0 + lift, S.o.y + (j + 0.5f) * C);
    float face = d == 0 ? S.o.y + j * C : d == 2 ? S.o.y + (j + 1) * C : d == 1 ? S.o.x + i * C : S.o.x + (i + 1) * C;
    {   // the real wall face along this direction (the cell edge can be up to half a cell off)
      uint64_t key = ((uint64_t)R.storey << 48) | ((uint64_t)(j * S.nx + i) << 8) | ((uint64_t)d << 5) | (uint64_t)std::min(31, (int)(lift * 8.0f));
      auto fc = faceCache.find(key);
      if (fc != faceCache.end()) { if (fc->second == fc->second) face = fc->second; }
      else {
        vec3 dl = d == 0 ? vec3(0, 0, -1) : d == 2 ? vec3(0, 0, 1) : d == 1 ? vec3(-1, 0, 0) : vec3(1, 0, 0);
        vec3 o(S.o.x + (i + 0.5f) * C, R.y0 + 0.6f + lift * 0.5f, S.o.y + (j + 0.5f) * C);
        Physics::Hit wh; float f = std::numeric_limits<float>::quiet_NaN();
        if (Physics::castRayEx(world(o), yawQuat(host.yaw) * dl, 0.3f, wh)) { vec3 lw = glm::inverse(yawQuat(host.yaw)) * (wh.pos - host.pos); f = face = (d == 0 || d == 2) ? lw.z : lw.x; }
        faceCache[key] = f;
      }
    }
    float gap = pd->mount > 0 ? 0.012f : 0.03f;   // GX2: floor pieces clear the 2 cm skirting board (trim, parts.mjs)
    if (d == 0) p.z = face + gap - mn.z;
    if (d == 2) p.z = face - gap - mx.z;
    if (d == 1) p.x = face + gap - mn.x;
    if (d == 3) p.x = face - gap - mx.x;
    yaw = yaws[d];
    return true;
  }
  // against a wall: back to the wall, front clearance kept; corner > 0 prefers corners, < 0 the middle of a wall
  bool wall(int k, const char* asset, const std::string& slot, float corner, PlanItem* got = nullptr, float lift = 0) {
    const AssetDef* fa = A(asset); if (!fa) return false;
    const PropDef* pd = sys.prop(fa);
    const auto& R = ri.rooms[k]; const auto& S = ri.storeys[R.storey];
    struct Cand { vec3 p; float yaw, score; };
    std::vector<Cand> cs;
    for (int j = 0; j < S.nz; ++j) for (int i = 0; i < S.nx; ++i) {
      if (S.id[(size_t)j * S.nx + i] != k) continue;
      for (int d = 0; d < 4; ++d) {
        vec3 p; float yaw;
        if (!wallCand(k, pd, i, j, d, lift, p, yaw) || !inZone(p)) continue;
        vec2 cc = zoneOn ? (zmn + zmx) * 0.5f : (R.mn + R.mx) * 0.5f, he = zoneOn ? (zmx - zmn) * 0.5f : (R.mx - R.mn) * 0.5f;
        float edge = glm::length(glm::abs(vec2(p.x, p.z) - cc) / glm::max(he, vec2(0.5f)));   // ~1.41 in a corner
        cs.push_back({p, yaw, -corner * edge + rnd((uint32_t)(i * 131 + j), strHash(slot)) * 0.8f + zoneDist(p) * 4.0f});
      }
    }
    std::sort(cs.begin(), cs.end(), [](const Cand& a, const Cand& b) { return a.score < b.score; });
    std::map<std::string, int> whys;
    wallBacked = true;
    for (auto& c : cs) { if (tryAt(k, asset, slot, c.p, c.yaw, true, true, 0, got)) { wallBacked = false; return true; } ++whys[why]; }
    wallBacked = false;
    if (failed.size() < 900) failed += " " + slot;
    if (getenv("NMK_GX2_DEBUG")) { std::string w; for (auto& [r, n] : whys) w += " " + r + " " + std::to_string(n); logf("gx2 wall %s %s %s: %zu spots%s%s", ha->id.c_str(), slot.c_str(), asset, cs.size(), zoneOn ? " (zone)" : "", w.c_str()); }
    return false;
  }
  // GX2: one piece at a given wall cell (hall pictures, wall fill, sconces by doors); hung pieces at their mount height
  bool wallCell(int k, const char* asset, const std::string& slot, int i, int j, int d, PlanItem* got = nullptr) {
    const AssetDef* fa = A(asset); if (!fa) return false;
    const PropDef* pd = sys.prop(fa);
    vec3 p; float yaw;
    if (!wallCand(k, pd, i, j, d, pd->mount, p, yaw)) { why = "nowall"; return false; }
    wallBacked = true;
    bool ok = tryAt(k, asset, slot, p, yaw, true, pd->mount <= 0, 0, got);
    wallBacked = false;
    return ok;
  }
  // area (GX2 centre-floor groups): the whole group's footprint in the anchor's frame [x0, z0, x1, z1]; the spot needs it and
  // a walkway round it clear of furniture, and nothing is reserved yet (the group's own pieces go there next)
  bool center(int k, const char* asset, const std::string& slot, float ring, PlanItem* got = nullptr, const vec4* area = nullptr) {
    const AssetDef* fa = A(asset); if (!fa) return false;
    const PropDef* pd = sys.prop(fa);
    const auto& R = ri.rooms[k]; const auto& S = ri.storeys[R.storey]; const float C = ri.cell;
    vec2 e = R.mx - R.mn;
    float yaw = e.x >= e.y ? 0.0f : 90.0f;
    struct Cand { vec3 p; float score; };
    std::vector<Cand> cs;
    vec2 cc = (R.mn + R.mx) * 0.5f;
    if (zoneOn) cc = (zmn + zmx) * 0.5f;
    for (int j = 0; j < S.nz; j += 2) for (int i = 0; i < S.nx; i += 2) {
      if (S.id[(size_t)j * S.nx + i] != k) continue;
      vec3 p(S.o.x + (i + 0.5f) * C, R.y0, S.o.y + (j + 0.5f) * C);
      if (!inZone(p)) continue;
      cs.push_back({p, glm::length(vec2(p.x, p.z) - cc) + rnd((uint32_t)(i * 31 + j), strHash(slot)) * 0.7f + zoneDist(p) * 4.0f});
    }
    std::sort(cs.begin(), cs.end(), [](const Cand& a, const Cand& b) { return a.score < b.score; });
    for (float rg : {ring, ring * 0.45f}) {
      for (auto& c : cs) {
        if (area) {
          vec2 a0 = yaw == 0.0f ? vec2((*area)[0], (*area)[1]) : vec2((*area)[1], -(*area)[2]), a1 = yaw == 0.0f ? vec2((*area)[2], (*area)[3]) : vec2((*area)[3], -(*area)[0]);
          vec2 lo = vec2(c.p.x, c.p.z) + glm::min(a0, a1) - vec2(walkway), hi = vec2(c.p.x, c.p.z) + glm::max(a0, a1) + vec2(walkway);
          if (!rectCells(R.storey, k, lo + vec2(walkway), hi - vec2(walkway), 0) || !rectCells(R.storey, k, lo, hi, 1)) continue;   // the group's floor free, a walkway round it
          if (tryAt(k, asset, slot, c.p, yaw, true, false, 0, got)) return true;
          continue;
        }
        vec3 mn, mx; bounds(pd, c.p, yaw, mn, mx);
        if (rg > 0 && !rectCells(R.storey, k, vec2(mn.x - rg, mn.z - rg), vec2(mx.x + rg, mx.z + rg), 1)) continue;
        if (tryAt(k, asset, slot, c.p, yaw, true, false, 0, got)) {
          if (rg > 0) mark(R.storey, vec2(mn.x - rg, mn.z - rg), vec2(mx.x + rg, mx.z + rg), 2);
          return true;
        }
      }
      if (ring <= 0) break;
    }
    if (failed.size() < 900) failed += " " + slot;
    return false;
  }
  void chairsAround(int k, const PlanItem& t, const char* chair, int want, bool ends, int salt = 0) {
    const PropDef* tp = sys.prop(A(t.asset.c_str()));
    float W = tp->bmax.x - tp->bmin.x, D = tp->bmax.z - tp->bmin.z;
    quat q = yawQuat(t.yaw);
    struct S { vec3 off; float yaw; };
    std::vector<S> spots = {{vec3(-W * 0.25f, 0, D * 0.5f + 0.27f), 180}, {vec3(W * 0.25f, 0, -D * 0.5f - 0.27f), 0}, {vec3(W * 0.25f, 0, D * 0.5f + 0.27f), 180}, {vec3(-W * 0.25f, 0, -D * 0.5f - 0.27f), 0}};
    if (ends) { spots.push_back({vec3(W * 0.5f + 0.28f, 0, 0), 270}); spots.push_back({vec3(-W * 0.5f - 0.28f, 0, 0), 90}); }
    int n = 0;
    for (size_t s = 0; s < spots.size() && n < want; ++s) {
      vec3 p = t.p + q * spots[s].off;
      float jitter = (rnd(strHash(t.slot), (uint32_t)s) - 0.5f) * 12.0f;
      if (tryAt(k, chair, t.slot + "/chair" + (salt ? std::to_string(salt) + "_" : std::string()) + std::to_string(s), p, wrapDeg(t.yaw + spots[s].yaw + jitter), true, false, 1, nullptr)) ++n;
    }
  }
  void onTop(const PlanItem& base, const std::vector<const char*>& choices, int count, uint32_t salt = 0) {
    const AssetDef* ba = A(base.asset.c_str()); if (!ba) return;
    const PropDef* bp = sys.prop(ba);
    if (bp->surfaces.empty() || choices.empty()) return;
    quat q = yawQuat(base.yaw);
    int placed = 0;
    for (int n = 0; n < count * 3 && placed < count; ++n) {
      uint32_t h = strHash(base.slot) + (uint32_t)n * 977u + salt * 7919u;
      size_t si = (size_t)(rnd(h, 1) * bp->surfaces.size()) % bp->surfaces.size();
      vec4 s = bp->surfaces[si];
      const AssetDef* pa = A(choices[(size_t)(rnd(h, 2) * choices.size()) % choices.size()]); if (!pa) continue;
      const PropDef* pp = sys.prop(pa);
      vec3 he = (pp->bmax - pp->bmin) * 0.5f; float r = std::max(he.x, he.z);
      bool narrow = s.z - s.x < 2 * r + 0.02f || s.w - s.y < 2 * r + 0.02f;   // a shelf board: the prop stands square to it
      float hx = narrow ? he.x : r, hz = narrow ? he.z : r;
      if (s.z - s.x < 2 * hx || s.w - s.y < 2 * hz) continue;
      vec3 lp(s.x + hx + (s.z - s.x - 2 * hx) * rnd(h, 3), bp->surfY[si], s.y + hz + (s.w - s.y - 2 * hz) * rnd(h, 4));
      lp -= vec3((pp->bmin.x + pp->bmax.x) * 0.5f, 0, (pp->bmin.z + pp->bmax.z) * 0.5f);
      PlanItem it; it.slot = base.slot + "/top" + std::to_string(n + salt * 100); it.asset = pa->id; it.p = base.p + q * lp;
      it.yaw = wrapDeg(base.yaw + (narrow ? 0.0f : (rnd(h, 5) - 0.5f) * 50.0f));
      bool clash = false;   // two props on one top never overlap
      for (auto& o : out) if (o.slot.rfind(base.slot + "/top", 0) == 0 && glm::length(vec2(o.p.x - it.p.x, o.p.z - it.p.z)) < (narrow ? hx + 0.13f : r + 0.12f)) clash = true;
      if (!clash) { out.push_back(it); ++placed; }
    }
  }
  bool rug(int k, const char* asset, const std::string& slot, vec2 at, float yaw) {
    const auto& R = ri.rooms[k];
    vec3 p(at.x, R.y0, at.y);
    return tryAt(k, asset, slot, p, yaw, false, false, 2, nullptr) || false;
  }
  void curtains(int k, float chance = 1.0f, float half = 0.0f) {   // GX2: per window, some half-drawn
    const auto& R = ri.rooms[k];
    if (chance <= 0) return;
    int n = 0;
    for (auto& b : ri.keep) {
      if (b.kind != 2 || b.mn.y < R.y0 || b.mn.y > R.y1) continue;
      for (const auto& d : ha->interactables) {
        if (d.kind != "window" || b.why != "window " + d.id) continue;
        vec3 mn = d.apMin, mx = d.apMax;
        int na = (mx.x - mn.x) < (mx.z - mn.z) ? 0 : 2;
        vec3 mid = (ha->bmin + ha->bmax) * 0.5f;
        float in = ((mn[na] + mx[na]) * 0.5f > mid[na]) ? -1.0f : 1.0f;
        float face = in < 0 ? mn[na] : mx[na];
        vec3 p = (mn + mx) * 0.5f; p[na] = face + in * 0.012f;   // GX2: 12 mm off the plaster, never on it
        p.y = mx.y + 0.05f - 1.5f;
        if (ri.at(R.storey, p + (na == 0 ? vec3(in * 0.3f, 0, 0) : vec3(0, 0, in * 0.3f))) != k) continue;
        float yaw = na == 2 ? (in < 0 ? 180.0f : 0.0f) : (in < 0 ? 270.0f : 90.0f);
        PlanItem it; it.slot = "r" + std::to_string(k) + "/curtains" + std::to_string(n++); it.p = p; it.yaw = yaw;
        if (rnd(strHash(it.slot), 1) >= chance) continue;
        it.asset = rnd(strHash(it.slot), 2) < half && A("furn_curtains_half") ? "furn_curtains_half" : "furn_curtains";
        out.push_back(it);
        if (const AssetDef* ca = A(it.asset.c_str())) { vec3 mn, mx; bounds(sys.prop(ca), it.p, it.yaw, mn, mx); boxes.push_back({mn, mx}); }   // GX2: pictures keep off the curtains
      }
    }
  }
  void papers(int k, const PlanItem& near, int n) {
    for (int c = 0; c < n; ++c) {
      quat q = yawQuat(near.yaw);
      vec3 p = near.p + q * vec3((rnd(strHash(near.slot), 40 + c) - 0.5f) * 0.6f, 0, 0.55f + 0.3f * rnd(strHash(near.slot), 50 + c));
      p.y = ri.rooms[k].y0;
      tryAt(k, "furn_papers", near.slot + "/papers" + std::to_string(c), p, rnd(strHash(near.slot), 60 + c) * 360.0f, false, false, 1, nullptr);
    }
  }
  bool maybe(uint32_t a, float p) { return rnd(a, 99) < p; }
  int count(uint32_t a, float base) { return (int)std::floor(base * density + rnd(a, 98)); }   // density-scaled count
  float mountOf(const char* asset) { const AssetDef* fa = A(asset); const PropDef* pd = fa ? sys.prop(fa) : nullptr; return pd ? pd->mount : 0.0f; }
  bool hang(int k, const char* asset, const std::string& slot, float corner, PlanItem* got = nullptr) { return A(asset) && wall(k, asset, slot, corner, got, mountOf(asset)); }
  // the first of several pieces that fits (a smaller one where the big one has no room)
  bool wallAny(int k, std::initializer_list<const char*> as, const std::string& slot, float corner, PlanItem* got = nullptr) {
    std::string f0 = failed;
    for (const char* a : as) if (wall(k, a, slot, corner, got)) { failed = f0; return true; }
    return false;
  }
  void hangTop(int k, const char* asset, const std::string& slot, float corner, const std::vector<const char*>& choices, int n) {
    PlanItem s; if (hang(k, asset, slot, corner, &s)) { onTop(s, choices, n); return; }
    // no wall left for a solid shelf (they keep out of routes): a picture or a portrait may still hang there
    std::string f0 = failed;
    if (hang(k, rnd(strHash(slot), 3) < 0.5f ? "furn_portrait" : "furn_picture", slot, corner)) failed = f0;
  }
  void breakLast(bool did, float p, uint32_t h) { if (did && !out.empty() && rnd(h, 77) < p) out.back().broken = true; }
  // the end of a room's long axis that faces away from its doors (an altar, a teacher's desk): +1 toward mx, -1 toward mn
  int farEnd(int k, int& axis) {
    const auto& R = ri.rooms[k]; vec2 e = R.mx - R.mn; axis = e.x >= e.y ? 0 : 1;
    float c = axis == 0 ? (R.mn.x + R.mx.x) * 0.5f : (R.mn.y + R.mx.y) * 0.5f, sum = 0; int n = 0;
    for (auto& b : ri.keep) {
      if (b.kind != 0 || b.mx.y < R.y0 - 0.3f || b.mn.y > R.y0 + 2.0f) continue;
      if (b.mx.x < R.mn.x - 0.6f || b.mn.x > R.mx.x + 0.6f || b.mx.z < R.mn.y - 0.6f || b.mn.z > R.mx.y + 0.6f) continue;
      float dc = (axis == 0 ? (b.mn.x + b.mx.x) * 0.5f : (b.mn.z + b.mx.z) * 0.5f) - c;
      sum += dc; ++n;
    }
    return n && sum > 0 ? -1 : 1;
  }
  // rows of pews / school desks facing the far end, with a centre aisle; returns how many were placed
  int rows(int k, const char* asset, const std::string& slot, float pitch, float aisle, float backGap, float frontGap, float breakP) {
    const AssetDef* fa = A(asset); if (!fa) return 0;
    const PropDef* pd = sys.prop(fa);
    const auto& R = ri.rooms[k];
    int ax; int s = farEnd(k, ax);
    float W = pd->bmax.x - pd->bmin.x;
    float lo = ax == 0 ? R.mn.x : R.mn.y, hi = ax == 0 ? R.mx.x : R.mx.y;
    float cl = ax == 0 ? R.mn.y : R.mn.x, ch = ax == 0 ? R.mx.y : R.mx.x, cc = (cl + ch) * 0.5f;
    float yaw = ax == 0 ? (s > 0 ? 90.0f : 270.0f) : (s > 0 ? 0.0f : 180.0f);
    // a column against each side wall, the aisle between them (often the authored route); in a narrow room the second
    // column does not fit beside the first and only one stands
    std::vector<float> across = {cl + W * 0.5f + 0.05f, ch - W * 0.5f - 0.05f};
    (void)cc; (void)aisle;
    int n = 0, row = 0;
    for (float t = 0; ; t += pitch, ++row) {
      float along = s > 0 ? lo + backGap + t : hi - backGap - t;
      if ((s > 0 && along > hi - frontGap) || (s < 0 && along < lo + frontGap)) break;
      for (size_t c = 0; c < across.size(); ++c) {
        vec3 p = ax == 0 ? vec3(along, R.y0, across[c]) : vec3(across[c], R.y0, along);
        std::string sl = slot + std::to_string(row) + (c ? "b" : "a");
        bool ok = tryAt(k, asset, sl, p, yaw, true, false, 0, nullptr);
        breakLast(ok, breakP, strHash(sl));
        n += ok;
      }
    }
    return n;
  }
  // ================================================================== GX2: data-driven dressing (room_dressing.json)
  static const json& rules() {
    static const json j = [] {
      try { return json::parse(readTextFile(contentDir() / "settings" / "room_dressing.json")); }
      catch (...) { logf("interior: content/settings/room_dressing.json missing or invalid: rooms stay bare"); return json::object(); }
    }();
    return j;
  }
  bool ruinedHost() const { return ha->id.find("ruin") != std::string::npos; }
  // the candidates of an "assets" list: each entry tried in order, "a|b" = one of them picked by seed
  std::vector<std::string> pick(const json& as, const std::string& slot) {
    std::vector<std::string> v;
    if (!as.is_array()) return v;
    for (size_t n = 0; n < as.size(); ++n) {
      if (!as[n].is_string()) continue;
      std::string e = as[n].get<std::string>(); std::vector<std::string> alt; size_t a = 0;
      for (size_t b; (b = e.find('|', a)) != std::string::npos; a = b + 1) alt.push_back(e.substr(a, b - a));
      alt.push_back(e.substr(a));
      v.push_back(alt[(size_t)(rnd(strHash(slot), 200 + (uint32_t)n) * alt.size()) % alt.size()]);
    }
    return v;
  }
  bool condOk(const json& s, int k) {
    const auto& R = ri.rooms[k];
    if (!s.contains("if")) return true;
    const json& c = s["if"];
    if (c.contains("minArea") && R.area < c["minArea"].get<float>()) return false;
    if (c.contains("maxArea") && R.area >= c["maxArea"].get<float>()) return false;
    if (c.contains("hearth") && c["hearth"].get<bool>() != R.hearth) return false;
    return true;
  }
  bool rollStep(const json& s, int k, const std::string& slot) {
    const auto& R = ri.rooms[k];
    std::string sure = s.value("sure", std::string());
    if ((sure == "hearth" && R.hearth) || (sure == "noHearth" && !R.hearth) || (sure == "big" && R.area > 9.0f)) return true;
    bool fixed = s.value("fixed", false);
    if (fixed && !s.contains("p")) return true;
    return rnd(strHash(slot), 99) < s.value("p", 1.0f) * (fixed ? 1.0f : density);
  }
  int stepCount(const json& s, int k, const std::string& slot) {
    if (s.contains("perArea")) return std::clamp((int)std::round(ri.rooms[k].area / s["perArea"].get<float>() * std::min(density, 1.5f)), 1, s.value("max", 5));
    if (!s.contains("n")) return 1;
    return s["n"][0].get<int>() + (int)std::floor(s["n"][1].get<float>() * density + rnd(strHash(slot), 98));
  }
  void putTop(const PlanItem& base, const json& t, uint32_t salt = 0) {
    if (!t.is_object()) return;
    std::vector<std::string> from = pick(t.value("from", json::array()), base.slot + "/from" + std::to_string(salt));
    std::vector<const char*> ch; for (auto& f : from) ch.push_back(f.c_str());
    int n = t.value("n", 1);
    if (t.value("clutter", false)) n = (int)std::round(n + 3 * clutter);
    onTop(base, ch, n, salt);
  }
  // satellites of a placed anchor (a cluster), in the anchor's frame (+z its front); "flush" ones stand beside it with
  // their backs on the same wall; a second spot (alt) where the first is taken
  void satellites(int k, const PlanItem& an, const json& sats) {
    if (!sats.is_array()) return;
    const PropDef* ap = sys.prop(A(an.asset.c_str()));
    quat q = yawQuat(an.yaw);
    for (size_t n = 0; n < sats.size(); ++n) {
      const json& st = sats[n];
      std::string slot = an.slot + "/s" + std::to_string(n);
      uint32_t h = strHash(slot);
      if (st.contains("p") && rnd(h, 97) >= st["p"].get<float>() * std::min(density, 1.0f)) continue;
      float jm = 0, jd = 0;
      if (st.contains("jitter")) { jm = st["jitter"][0].get<float>(); jd = st["jitter"][1].get<float>(); }
      vec3 jit((rnd(h, 11) - 0.5f) * 2 * jm, 0, (rnd(h, 12) - 0.5f) * 2 * jm);
      float jy = (rnd(h, 13) - 0.5f) * 2 * jd;
      std::vector<vec3> spots;
      if (st.contains("at")) spots.push_back(jvec3(st["at"]));
      if (st.contains("alt")) spots.push_back(jvec3(st["alt"]));
      if (rnd(h, 14) < 0.5f && spots.size() == 2) std::swap(spots[0], spots[1]);   // either side of the anchor
      bool rugS = st.value("rug", false), done = false;
      for (auto& a : pick(st.value("assets", json::array()), slot)) {
        const AssetDef* sa = A(a.c_str()); if (!sa) continue;
        const PropDef* sp = sys.prop(sa);
        for (auto& at : spots) {
          vec3 off = at;
          if (st.value("flush", false)) {
            off.x = at.x >= 0 ? ap->bmax.x - sp->bmin.x + std::fabs(at.x) : ap->bmin.x - sp->bmax.x - std::fabs(at.x);
            off.z = ap->bmin.z - sp->bmin.z + at.z;
          }
          vec3 p = an.p + q * (off + jit); p.y = ri.rooms[k].y0;
          float yaw = wrapDeg(an.yaw + st.value("turn", 0.0f) + jy);
          PlanItem got;
          // rugs lie anywhere in the room; chairs tucked at a desk may stand in its clearance; the rest keep every
          // walkway free (the room stays walkable)
          bool ok = rugS ? tryAt(k, a.c_str(), slot, p, yaw, false, false, 2, &got) : tryAt(k, a.c_str(), slot, p, yaw, true, false, st.value("tuck", false) ? 1 : 0, &got);
          if (!ok) continue;
          if (st.contains("top")) putTop(got, st["top"]);
          done = true; break;
        }
        if (done) break;
      }
    }
  }
  // a wall piece straight above a piece backed onto the wall (a picture over a desk, a pan rail over the range)
  void hangAbove(int k, const PlanItem& an, const json& as) {
    const PropDef* ap = sys.prop(A(an.asset.c_str()));
    quat q = yawQuat(an.yaw);
    for (auto& a : pick(as, an.slot + "/above")) {
      const AssetDef* fa = A(a.c_str()); if (!fa) continue;
      const PropDef* pd = sys.prop(fa);
      if (pd->mount <= 0 || pd->mount + pd->bmin.y < ap->bmax.y + 0.08f) continue;   // would sit on or behind the piece
      for (float dx : {0.0f, 0.3f, -0.3f}) {
        vec3 p = an.p + q * vec3(dx, 0, ap->bmin.z - 0.03f + 0.012f - pd->bmin.z);
        p.y = ri.rooms[k].y0 + pd->mount;
        if (tryAt(k, a.c_str(), an.slot + "/above", p, an.yaw, true, false, 0, nullptr)) return;
      }
    }
  }
  // one recipe step, or a named cluster
  bool runStep(int k, const json& s, const std::string& slot, PlanItem* got = nullptr) {
    std::string d = s.value("do", std::string("wall"));
    if (d == "cluster") {
      static const json none = json::object();
      const json& C = rules().contains("clusters") ? rules()["clusters"] : none;
      std::string nm = s.value("cluster", std::string());
      return C.contains(nm) && runStep(k, C[nm], slot, got);
    }
    const auto& R = ri.rooms[k];
    std::vector<std::string> as = pick(s.value("assets", json::array()), slot);
    PlanItem w; bool ok = false;
    std::string f0 = failed;
    float corner = s.value("corner", 0.0f);
    if (d == "wall") { for (auto& a : as) if ((ok = wall(k, a.c_str(), slot, corner, &w, s.value("lift", 0.0f)))) break; }
    else if (d == "hang") {
      for (auto& a : as) if ((ok = hang(k, a.c_str(), slot, corner, &w))) break;
      if (!ok && s.contains("top")) {   // no wall left for a solid shelf (they keep out of routes): a picture may still hang there
        if (hang(k, rnd(strHash(slot), 3) < 0.5f ? "furn_portrait" : "furn_picture", slot, corner)) failed = f0;
        return false;
      }
    }
    else if (d == "center") {
      vec4 ar(0); if (s.contains("area")) for (int c = 0; c < 4; ++c) ar[c] = s["area"][c].get<float>();
      for (auto& a : as) if ((ok = center(k, a.c_str(), slot, s.value("ring", 0.6f), &w, s.contains("area") ? &ar : nullptr))) break;
    }
    else if (d == "rug") {
      vec2 cc = zoneOn ? (zmn + zmx) * 0.5f : (R.mn + R.mx) * 0.5f, e = R.mx - R.mn;
      for (auto& a : as) if ((ok = rug(k, a.c_str(), slot, cc, e.x >= e.y ? 0.0f : 90.0f))) { w = out.back(); break; }
    }
    else if (d == "rows") return !as.empty() && rows(k, as[0].c_str(), slot, s.value("pitch", 1.0f), s.value("aisle", 1.0f), s.value("back", 1.0f), s.value("front", 2.0f), ruinedHost() ? s.value("brokenRuined", 0.0f) : 0.0f) > 0;
    else if (d == "farEnd") {
      int ax; int sd = farEnd(k, ax); vec2 cc = (R.mn + R.mx) * 0.5f; float in = s.value("inset", 1.0f);
      vec3 p = ax == 0 ? vec3(sd > 0 ? R.mx.x - in : R.mn.x + in, R.y0, cc.y) : vec3(cc.x, R.y0, sd > 0 ? R.mx.y - in : R.mn.y + in);
      float yaw = s.value("face", std::string("along")) == "class" ? (ax == 0 ? (sd > 0 ? 270.0f : 90.0f) : (sd > 0 ? 180.0f : 0.0f)) : (ax == 0 ? 90.0f : 0.0f);
      for (auto& a : as) if ((ok = tryAt(k, a.c_str(), slot, p, yaw, true, false, 0, &w))) break;
    }
    if (!ok) return false;
    failed = f0;
    float br = s.value("broken", 0.0f); if (ruinedHost()) br = std::max(br, s.value("brokenRuined", 0.0f));
    if (br > 0 && rnd(strHash(slot), 77) < br) out.back().broken = true;
    if (s.contains("top")) putTop(w, s["top"]);
    if (s.contains("top2")) putTop(w, s["top2"], 1);
    if (s.contains("chairs")) for (size_t c = 0; c < s["chairs"].size(); ++c) {
      const json& ch = s["chairs"][c];
      std::vector<std::string> ca = pick(ch.value("assets", json::array()), slot + "/ch" + std::to_string(c));
      if (ca.empty()) continue;
      json nn = ch.value("n", json::array({1, 0}));
      int want = nn[0].get<int>() + (int)std::round(nn[1].get<float>() * std::min(density, 1.0f) * rnd(strHash(slot), 3 + (uint32_t)c)) + (density > 1.2f);
      size_t n0 = out.size();
      chairsAround(k, w, ca[0].c_str(), want, ch.value("ends", false), (int)c);
      float cb = ch.value("broken", 0.0f);
      for (size_t q = n0; q < out.size(); ++q) if (cb > 0 && rnd(strHash(out[q].slot), 5) < cb) out[q].broken = true;
    }
    if (s.contains("satellites")) satellites(k, w, s["satellites"]);
    if (d == "center" && s.contains("area")) {   // the group placed: its footprint and the walkway round it stay clear of later pieces
      const auto& ar = s["area"]; quat q = yawQuat(w.yaw);
      vec3 c0 = w.p + q * vec3(ar[0].get<float>(), 0, ar[1].get<float>()), c1 = w.p + q * vec3(ar[2].get<float>(), 0, ar[3].get<float>());
      mark(R.storey, vec2(std::min(c0.x, c1.x), std::min(c0.z, c1.z)) - vec2(walkway), vec2(std::max(c0.x, c1.x), std::max(c0.z, c1.z)) + vec2(walkway), 2);
    }
    if (s.contains("hangAbove")) hangAbove(k, w, s["hangAbove"]);
    if (s.contains("papers") && rnd(strHash(slot), 9) < s["papers"].value("p", 1.0f) * clutter) papers(k, w, s["papers"].value("n", 1));
    if (got) *got = w;
    return true;
  }
  // ---- wall edges: room cells against a wall (direction d as in wallCand), and the bare runs along them
  struct Edge { int i, j, d, line; float along; };
  std::vector<Edge> wallEdges(int k) {
    std::vector<Edge> v;
    const auto& R = ri.rooms[k]; const auto& S = ri.storeys[R.storey]; const float C = ri.cell;
    const int di[4] = {0, -1, 0, 1}, dj[4] = {-1, 0, 1, 0};
    for (int j = 0; j < S.nz; ++j) for (int i = 0; i < S.nx; ++i) {
      if (S.id[(size_t)j * S.nx + i] != k) continue;
      for (int d = 0; d < 4; ++d) {
        int ni = i + di[d], nj = j + dj[d];
        (void)ni; (void)nj;
        if (!wallBehind(R.storey, i, j, d)) continue;
        bool alongX = d == 0 || d == 2;
        v.push_back({i, j, d, alongX ? j : i, alongX ? S.o.x + (i + 0.5f) * C : S.o.y + (j + 0.5f) * C});
      }
    }
    return v;
  }
  vec2 edgePoint(int k, const Edge& e) const {
    const auto& S = ri.storeys[ri.rooms[k].storey]; const float C = ri.cell;
    vec2 c(S.o.x + (e.i + 0.5f) * C, S.o.y + (e.j + 0.5f) * C);
    const vec2 o[4] = {vec2(0, -C), vec2(-C, 0), vec2(0, C), vec2(C, 0)};
    return c + o[e.d] * 0.5f;
  }
  std::unordered_map<std::string, std::pair<vec3, vec3>> bcache;   // footprints of placed pieces, by slot
  // the footprints (xz) that cover a wall: the room's pieces (not flat, not hung from the ceiling, not on tops) and
  // its doors and windows
  std::vector<vec4> coverBoxes(int k, const std::string& r) {
    const auto& R = ri.rooms[k];
    std::vector<vec4> v;
    for (auto& b : ri.keep) {   // doors and windows break the wall
      if (b.kind == 1 || b.mx.y < R.y0 + 0.2f || b.mn.y > R.y0 + 2.0f) continue;
      v.push_back(vec4(b.mn.x - 0.3f, b.mn.z - 0.3f, b.mx.x + 0.3f, b.mx.z + 0.3f));
    }
    for (auto& it : out) {
      if (it.slot.rfind(r, 0) != 0 || it.slot.find("/top") != std::string::npos) continue;
      auto bc = bcache.find(it.slot);
      if (bc == bcache.end()) {
        const AssetDef* fa = A(it.asset.c_str()); const PropDef* pd = fa ? sys.prop(fa) : nullptr;
        vec3 mn(1e9f), mx(-1e9f);
        if (pd && !pd->flat() && !pd->hang) bounds(pd, it.p, it.yaw, mn, mx);
        bc = bcache.emplace(it.slot, std::make_pair(mn, mx)).first;
      }
      const vec3& mn = bc->second.first; const vec3& mx = bc->second.second;
      if (mn.x <= mx.x) v.push_back(vec4(mn.x - 0.25f, mn.z - 0.25f, mx.x + 0.25f, mx.z + 0.25f));
    }
    return v;
  }
  struct Run { size_t a = 0, b = 0; float len = 0; };   // indices into the sorted edge list, inclusive
  std::vector<Run> bareRuns(int k, std::vector<Edge>& E, const std::string& r, const std::set<size_t>& blocked, bool countBlocked) {
    std::sort(E.begin(), E.end(), [](const Edge& x, const Edge& y) { return x.d != y.d ? x.d < y.d : x.line != y.line ? x.line < y.line : x.along < y.along; });
    std::vector<vec4> cov = coverBoxes(k, r);
    std::vector<Run> runs; const float C = ri.cell;
    bool open = false; Run cur;
    for (size_t n = 0; n < E.size(); ++n) {
      vec2 fp = edgePoint(k, E[n]); bool hit = false;
      for (auto& c : cov) if (fp.x > c.x && fp.x < c.z && fp.y > c.y && fp.y < c.w) { hit = true; break; }
      bool bare = !hit && (countBlocked || !blocked.count(n));
      bool cont = open && n > 0 && E[n].d == E[n - 1].d && E[n].line == E[n - 1].line && E[n].along - E[n - 1].along < C * 1.6f;
      if (bare && cont) { cur.b = n; cur.len = E[n].along - E[cur.a].along + C; continue; }
      if (open) runs.push_back(cur);
      open = bare; if (bare) { cur.a = cur.b = n; cur.len = C; }
    }
    if (open) runs.push_back(cur);
    return runs;
  }
  int wallItems(const std::string& r) {
    int n = 0;
    for (auto& it : out) { if (it.slot.rfind(r, 0) != 0) continue; const AssetDef* fa = A(it.asset.c_str()); const PropDef* pd = fa ? sys.prop(fa) : nullptr; n += pd && pd->mount > 0; }
    return n;
  }
  // wall-mounted pieces until no bare wall is longer than maxGap and the room has perMetre of them per metre of wall
  // (floor pieces where nothing can hang)
  void wallFill(int k, const json& W, const std::string& r) {
    std::vector<Edge> E = wallEdges(k);
    float wallLen = E.size() * ri.cell;
    float perM = W.value("perMetre", 0.3f), maxGap = W.value("maxGap", 4.0f);
    std::vector<std::string> items, floors;
    for (auto& x : W.value("items", json::array())) items.push_back(x.get<std::string>());
    for (auto& x : W.value("floorItems", json::array())) floors.push_back(x.get<std::string>());
    std::set<size_t> blocked;
    for (int it = 0; it < 48; ++it) {
      bcache.clear();
      std::vector<Run> runs = bareRuns(k, E, r, blocked, false);
      if (runs.empty()) break;
      std::sort(runs.begin(), runs.end(), [](const Run& a, const Run& b) { return a.len > b.len; });
      bool few = wallItems(r) < perM * wallLen * std::min(density, 1.5f);
      if (runs[0].len <= maxGap && !(few && runs[0].len > 1.2f)) break;
      const Run run = runs[0];
      size_t mid = (run.a + run.b) / 2, span = run.b - run.a;
      std::string slot = r + "fill" + std::to_string(it);
      bool ok = false;
      for (int off : {0, 4, -4, 8, -8, 12, -12}) {
        long n = (long)mid + off;
        if (n < (long)run.a || n > (long)run.b || (size_t)std::abs(off) > span / 2 + 1) continue;
        const Edge e = E[(size_t)n];
        std::vector<std::string> tryList;
        if (!items.empty()) for (uint32_t q = 0; q < 3; ++q) tryList.push_back(items[(size_t)(rnd(strHash(slot), 600 + q) * items.size()) % items.size()]);
        if (!floors.empty() && ri.rooms[k].area >= 20.0f) tryList.push_back(floors[(size_t)(rnd(strHash(slot), 650) * floors.size()) % floors.size()]);   // small rooms stay walkable
        for (auto& a : tryList) if ((ok = wallCell(k, a.c_str(), slot, e.i, e.j, e.d))) break;
        if (ok) break;
      }
      if (!ok && getenv("NMK_GX2_DEBUG")) { vec2 fp = edgePoint(k, E[mid]); logf("gx2 fill %s%s: %.1f m run at (%.2f, %.2f) d%d refused: %s", ha->id.c_str(), slot.c_str(), run.len, fp.x, fp.y, E[mid].d, why.c_str()); }
      if (!ok) for (size_t n = run.a; n <= run.b; ++n) if (std::fabs(E[n].along - E[mid].along) < 0.8f) blocked.insert(n);
    }
  }
  // the middle of the room across its long axis at one point along it (L-shaped halls)
  float centreAcross(int k, int ax, float at) {
    const auto& R = ri.rooms[k]; const auto& S = ri.storeys[R.storey]; const float C = ri.cell;
    float lo = 1e9f, hi = -1e9f;
    if (ax == 0) { int i = (int)std::floor((at - S.o.x) / C); if (i >= 0 && i < S.nx) for (int j = 0; j < S.nz; ++j) if (S.id[(size_t)j * S.nx + i] == k) { lo = std::min(lo, S.o.y + j * C); hi = std::max(hi, S.o.y + (j + 1) * C); } }
    else { int j = (int)std::floor((at - S.o.y) / C); if (j >= 0 && j < S.nz) for (int i = 0; i < S.nx; ++i) if (S.id[(size_t)j * S.nx + i] == k) { lo = std::min(lo, S.o.x + i * C); hi = std::max(hi, S.o.x + (i + 1) * C); } }
    return lo < hi ? (lo + hi) * 0.5f : (ax == 0 ? (R.mn.y + R.mx.y) * 0.5f : (R.mn.x + R.mx.x) * 0.5f);
  }
  // the room cell against the wall in direction d at a point along that wall
  bool edgeCell(int k, int d, float along, int& ci, int& cj) {
    const auto& R = ri.rooms[k]; const auto& S = ri.storeys[R.storey]; const float C = ri.cell;
    auto id = [&](int i, int j) { return i < 0 || j < 0 || i >= S.nx || j >= S.nz ? (int16_t)RoomInfo::WALL : S.id[(size_t)j * S.nx + i]; };
    if (d == 0 || d == 2) {
      int i = (int)std::floor((along - S.o.x) / C); if (i < 0 || i >= S.nx) return false;
      for (int s = 0; s < S.nz; ++s) { int j = d == 0 ? s : S.nz - 1 - s; if (id(i, j) != k) continue; if (!wallBehind(R.storey, i, j, d)) return false; ci = i; cj = j; return true; }
    } else {
      int j = (int)std::floor((along - S.o.y) / C); if (j < 0 || j >= S.nz) return false;
      for (int s = 0; s < S.nx; ++s) { int i = d == 1 ? s : S.nx - 1 - s; if (id(i, j) != k) continue; if (!wallBehind(R.storey, i, j, d)) return false; ci = i; cj = j; return true; }
    }
    return false;
  }
  // a coat stand or hooks beside a door to the outside
  void coatByDoor(int k, const json& list, const std::string& r) {
    const auto& R = ri.rooms[k];
    for (const auto& dr : ha->interactables) {
      if (dr.kind != "door" || std::fabs(dr.apMin.y - R.y0) > 0.45f) continue;
      vec3 mn = dr.apMin, mx = dr.apMax;
      bool outer = mn.x < ha->bmin.x + 1.2f || mx.x > ha->bmax.x - 1.2f || mn.z < ha->bmin.z + 1.2f || mx.z > ha->bmax.z - 1.2f;
      if (!outer) continue;
      int na = (mx.x - mn.x) < (mx.z - mn.z) ? 0 : 2, la = 2 - na;
      vec3 c = (mn + mx) * 0.5f; c.y = R.y0 + 0.5f;
      for (float sg : {1.0f, -1.0f}) {
        vec3 q = c; q[na] += sg * ((mx[na] - mn[na]) * 0.5f + 0.4f);
        if (ri.at(R.storey, q) != k) continue;
        int dd = na == 2 ? (sg > 0 ? 0 : 2) : (sg > 0 ? 1 : 3);
        for (float off : {0.5f, 0.8f, 1.2f}) for (float side : {1.0f, -1.0f}) {
          int ci, cj;
          if (!edgeCell(k, dd, side > 0 ? mx[la] + off : mn[la] - off, ci, cj)) continue;
          for (auto& a : pick(list, r + "coat")) if (wallCell(k, a.c_str(), r + "coat", ci, cj, dd)) return;
        }
      }
    }
  }
  // hallways: a runner down the middle, side tables with lamps, pictures every 3-4 m on both long walls, a coat stand
  // by the door to the outside
  void hallRules(int k, const json& H, const std::string& r) {
    const auto& R = ri.rooms[k];
    vec2 e = R.mx - R.mn; int ax = e.x >= e.y ? 0 : 1; float L = ax == 0 ? e.x : e.y;
    coatByDoor(k, H.value("coat", json::array()), r);
    if (L < H.value("minLength", 3.5f)) return;
    float lo = ax == 0 ? R.mn.x : R.mn.y, hi = ax == 0 ? R.mx.x : R.mx.y;
    std::string rn = H.value("runner", std::string());
    if (const AssetDef* ra = A(rn.c_str())) {
      const PropDef* rp = sys.prop(ra); float rl = rp->bmax.z - rp->bmin.z, gap = H.value("runnerGap", 0.6f);
      int n = 0;
      for (float t = lo + gap; t + rl <= hi - gap + 1e-3f;) {
        bool ok = false;
        for (float off : {0.0f, 0.2f, -0.2f, 0.4f, -0.4f}) {
          float c = centreAcross(k, ax, t + rl * 0.5f) + off;
          vec3 p = ax == 0 ? vec3(t + rl * 0.5f, R.y0, c) : vec3(c, R.y0, t + rl * 0.5f);
          if ((ok = tryAt(k, rn.c_str(), r + "runner" + std::to_string(n), p, ax == 0 ? 90.0f : 0.0f, false, false, 2, nullptr))) break;
        }
        if (ok) { ++n; t += rl + gap; } else t += 0.3f;
      }
    }
    int dA = ax == 0 ? 0 : 1, dB = ax == 0 ? 2 : 3;   // the two long walls
    json frames = H.value("frames", json::array({"furn_picture"}));
    std::vector<std::string> tables; for (auto& x : H.value("tables", json::array())) tables.push_back(x.get<std::string>());
    float every = H.value("tablesEvery", 6.5f);
    int tn = 0;
    for (float t = lo + std::min(every * 0.5f, L * 0.5f); t < hi - 0.8f; t += every, ++tn) {
      int d = (tn % 2) ? dB : dA; bool ok = false;
      std::string slot = r + "halltable" + std::to_string(tn);
      for (float dt : {0.0f, 0.6f, -0.6f, 1.2f, -1.2f}) {
        int ci, cj; if (!edgeCell(k, d, t + dt, ci, cj)) continue;
        PlanItem got;
        for (auto& a : tables) if ((ok = wallCell(k, a.c_str(), slot, ci, cj, d, &got))) break;
        if (ok) { putTop(got, json{{"from", H.value("tableTop", json::array())}, {"n", 2}}); hangAbove(k, got, frames); break; }
      }
    }
    float e0 = H.contains("pictureEvery") ? H["pictureEvery"][0].get<float>() : 3.0f, e1 = H.contains("pictureEvery") ? H["pictureEvery"][1].get<float>() : 4.0f;
    int pn = 0;
    for (int side = 0; side < 2; ++side) {
      int d = side ? dB : dA;
      for (float t = lo + 1.0f + side * e0 * 0.5f; t < hi - 0.8f; ++pn) {
        std::string slot = r + "hallpic" + std::to_string(pn);
        std::string f = frames[(size_t)(rnd(strHash(slot), 300) * frames.size()) % frames.size()].get<std::string>();
        for (float dt : {0.0f, 0.4f, -0.4f, 0.8f, -0.8f, 1.2f}) {
          int ci, cj; if (!edgeCell(k, d, t + dt, ci, cj)) continue;
          if (wallCell(k, f.c_str(), slot, ci, cj, d)) { t += dt; break; }
        }
        t += e0 + (e1 - e0) * rnd(strHash(slot), 301);
      }
    }
  }
  // oversized rooms (> maxSide on a side, not halls, barns, naves): a cluster grouping in each 4-5 m zone
  void zones(int k, const json& T, const std::string& r) {
    static const json none = json::object();
    const json& D = rules().contains("subdivide") ? rules()["subdivide"] : none;
    const auto& R = ri.rooms[k];
    for (auto& s : D.value("skip", json::array())) if (s.get<std::string>() == roomTypeKey(R.type)) return;
    json Z = T.value("zones", json::array());
    float maxSide = D.value("maxSide", 8.0f), zl = D.value("zone", 4.5f);
    vec2 e = R.mx - R.mn;
    if (Z.empty() || std::max(e.x, e.y) <= maxSide) return;
    int ax = e.x >= e.y ? 0 : 1; float L = ax == 0 ? e.x : e.y, Wd = ax == 0 ? e.y : e.x;
    int nz = (int)std::ceil(L / zl), nw = Wd > maxSide ? 2 : 1;
    size_t z0 = (size_t)(rnd(strHash(r), 500) * Z.size());
    for (int a = 0; a < nz; ++a) for (int b = 0; b < nw; ++b) {
      vec2 lo2 = R.mn, hi2 = R.mx;
      if (ax == 0) { lo2.x = R.mn.x + L * a / nz; hi2.x = R.mn.x + L * (a + 1) / nz; lo2.y = R.mn.y + Wd * b / nw; hi2.y = R.mn.y + Wd * (b + 1) / nw; }
      else { lo2.y = R.mn.y + L * a / nz; hi2.y = R.mn.y + L * (a + 1) / nz; lo2.x = R.mn.x + Wd * b / nw; hi2.x = R.mn.x + Wd * (b + 1) / nw; }
      zoneOn = true; zmn = lo2; zmx = hi2;
      runStep(k, json{{"do", "cluster"}, {"cluster", Z[(z0 + (size_t)(a * nw + b)) % Z.size()]}}, r + "zone" + std::to_string(a) + "_" + std::to_string(b));
      // a wide zone also gets a free-standing grouping in its middle (a table with chairs, armchairs round a rug)
      json CZ = T.value("centerZones", json::array());
      if (!CZ.empty() && !centerFloorOn(k, T) && std::max(hi2.x - lo2.x, hi2.y - lo2.y) >= 5.0f && std::min(hi2.x - lo2.x, hi2.y - lo2.y) >= 3.5f && (a + b) % 2 == 0)
        runStep(k, json{{"do", "cluster"}, {"cluster", CZ[(z0 + (size_t)a) % CZ.size()]}}, r + "zonec" + std::to_string(a) + "_" + std::to_string(b));
    }
    zoneOn = false;
  }
  // GX2: a room wide enough to leave open floor far from its walls gets a free-standing group in each centerCell m
  // cell down its middle (a seating group on a large rug, a dining or work table, a bed against a screen or a pair of
  // wardrobes), the cluster type cycling through the type's list from a seeded start
  bool centerFloorOn(int k, const json& T) {
    static const json none = json::object();
    const json& D = rules().contains("subdivide") ? rules()["subdivide"] : none;
    vec2 e = ri.rooms[k].mx - ri.rooms[k].mn;
    return !T.value("centerFloor", json::array()).empty() && std::min(e.x, e.y) >= D.value("centerMin", 6.0f);
  }
  void centerFloor(int k, const json& T, const std::string& r) {
    if (!centerFloorOn(k, T)) return;
    static const json none = json::object();
    const json& D = rules().contains("subdivide") ? rules()["subdivide"] : none;
    const json CF = T["centerFloor"];
    const auto& R = ri.rooms[k]; vec2 e = R.mx - R.mn;
    int ax = e.x >= e.y ? 0 : 1; float L = ax == 0 ? e.x : e.y, band = D.value("centerBand", 2.3f);
    int n = std::max(1, (int)std::floor(L / D.value("centerCell", 5.0f)));
    size_t c0 = (size_t)(rnd(strHash(r), 510) * CF.size());
    for (int a = 0; a < n; ++a) {
      vec2 lo2 = R.mn + vec2(band), hi2 = R.mx - vec2(band);
      if (ax == 0) { lo2.x = R.mn.x + L * a / n; hi2.x = R.mn.x + L * (a + 1) / n; }
      else { lo2.y = R.mn.y + L * a / n; hi2.y = R.mn.y + L * (a + 1) / n; }
      zoneOn = true; zmn = lo2; zmx = hi2;
      runStep(k, json{{"do", "cluster"}, {"cluster", CF[(c0 + (size_t)a) % CF.size()]}}, r + "floor" + std::to_string(a));
    }
    zoneOn = false;
  }
  void rugPass(int k, float p, const std::string& r) {
    if (rnd(strHash(r), 700) >= p) return;
    for (auto& it : out) if (it.slot.rfind(r, 0) == 0 && it.asset.find("rug") != std::string::npos) return;
    const auto& R = ri.rooms[k]; vec2 cc = (R.mn + R.mx) * 0.5f, e = R.mx - R.mn; int ax = e.x >= e.y ? 0 : 1;
    std::string a = rnd(strHash(r), 701) < 0.5f ? "furn_rug_medallion" : "furn_rug_kilim";
    for (float f : {0.0f, 0.25f, -0.25f}) {
      vec2 at = cc + (ax == 0 ? vec2(e.x * f, 0) : vec2(0, e.y * f));
      if (rug(k, a.c_str(), r + "rugx", at, ax == 0 ? 0.0f : 90.0f)) return;
    }
  }
  void debrisPass(int k, float p, const std::string& r) {
    const auto& R = ri.rooms[k]; const auto& S = ri.storeys[R.storey]; const float C = ri.cell;
    p *= std::pow(rules().value("debrisWear", 1.15f), std::max(0.0f, wear - 1.0f) * 8.0f);   // worn / damaged buildings collect more
    int n = (rnd(strHash(r), 800) < p) + (rnd(strHash(r), 801) < p * 0.5f) + (ruinedHost() && rnd(strHash(r), 802) < p);
    for (int c = 0; c < n; ++c) {
      std::string slot = r + "debris" + std::to_string(c);
      const char* a = rnd(strHash(slot), 1) < 0.65f ? "furn_debris_plaster" : "furn_debris_glass";
      for (uint32_t t = 0; t < 12; ++t) {
        vec3 p3(R.mn.x + (R.mx.x - R.mn.x) * rnd(strHash(slot), 10 + t), R.y0, R.mn.y + (R.mx.y - R.mn.y) * rnd(strHash(slot), 40 + t));
        int i = (int)((p3.x - S.o.x) / C), j = (int)((p3.z - S.o.y) / C);
        if (i < 0 || j < 0 || i >= S.nx || j >= S.nz || S.id[(size_t)j * S.nx + i] != k) continue;
        if (tryAt(k, a, slot, p3, rnd(strHash(slot), 70 + t) * 360.0f, false, false, 1, nullptr)) break;
      }
    }
  }
  // the share of the room's free floor a 0.3 m body reaches from its doorways and routes, around the blocking pieces
  // planned so far (the self-test's walkability rule, within the room)
  float reachable(int k) {
    const auto& R = ri.rooms[k]; const auto& S = ri.storeys[R.storey]; const float C = ri.cell;
    std::vector<uint8_t> blk(S.id.size(), 0);
    for (auto& it : out) {
      if (it.broken) continue;
      const AssetDef* fa = A(it.asset.c_str()); const PropDef* pd = fa ? sys.prop(fa) : nullptr;
      if (!pd || !pd->blocking || pd->mount > 0 || std::fabs(it.p.y - R.y0) > 0.5f) continue;
      vec3 mn, mx; bounds(pd, it.p, it.yaw, mn, mx); mn -= vec3(0.28f); mx += vec3(0.28f);
      if (mx.x < R.mn.x - 0.5f || mn.x > R.mx.x + 0.5f || mx.z < R.mn.y - 0.5f || mn.z > R.mx.y + 0.5f) continue;
      int i0 = std::max(0, (int)((mn.x - S.o.x) / C)), i1 = std::min(S.nx - 1, (int)((mx.x - S.o.x) / C));
      int j0 = std::max(0, (int)((mn.z - S.o.y) / C)), j1 = std::min(S.nz - 1, (int)((mx.z - S.o.y) / C));
      for (int j = j0; j <= j1; ++j) for (int i = i0; i <= i1; ++i) {
        float x = S.o.x + (i + 0.5f) * C, z = S.o.y + (j + 0.5f) * C;
        if (x > mn.x && x < mx.x && z > mn.z && z < mx.z) blk[(size_t)j * S.nx + i] = 1;
      }
    }
    auto inside = [&](size_t c) { return S.id[c] == (int16_t)k || S.id[c] == RoomInfo::DOORWAY; };
    auto flood = [&](bool useBlk) {
      std::vector<uint8_t> seen(S.id.size(), 0); std::vector<int> st;
      int ri0 = std::max(0, (int)((R.mn.x - 1.0f - S.o.x) / C)), ri1 = std::min(S.nx - 1, (int)((R.mx.x + 1.0f - S.o.x) / C));
      int rj0 = std::max(0, (int)((R.mn.y - 1.0f - S.o.y) / C)), rj1 = std::min(S.nz - 1, (int)((R.mx.y + 1.0f - S.o.y) / C));
      for (int j = rj0; j <= rj1; ++j) for (int i = ri0; i <= ri1; ++i) {
        size_t c = (size_t)j * S.nx + i;
        if ((S.id[c] == RoomInfo::DOORWAY || (S.keep[c] && S.id[c] == (int16_t)k)) && !(useBlk && blk[c])) { seen[c] = 1; st.push_back((int)c); }
      }
      while (!st.empty()) {
        int c = st.back(); st.pop_back(); int ci = c % S.nx, cj = c / S.nx;
        const int di[4] = {1, -1, 0, 0}, dj[4] = {0, 0, 1, -1};
        for (int d = 0; d < 4; ++d) {
          int ni = ci + di[d], nj = cj + dj[d]; if (ni < ri0 || nj < rj0 || ni > ri1 || nj > rj1) continue;
          size_t n = (size_t)nj * S.nx + ni;
          if (seen[n] || (useBlk && blk[n]) || !inside(n)) continue;
          seen[n] = 1; st.push_back((int)n);
        }
      }
      return seen;
    };
    std::vector<uint8_t> base = flood(false), seen = flood(true);
    int free = 0, ok = 0;
    for (size_t c = 0; c < S.id.size(); ++c) if (S.id[c] == (int16_t)k && base[c] && !blk[c]) { ++free; ok += seen[c]; }
    return free ? (float)ok / free : 1.0f;
  }
  // a room that a piece walls off: the latest blocking pieces (and what stands on or beside them) leave until it is walkable
  void keepWalkable(int k, const std::string& r) {
    for (int guard = 0; guard < 8 && reachable(k) < 0.88f; ++guard) {
      int last = -1;
      for (int q = (int)out.size() - 1; q >= 0; --q) {
        if (out[q].slot.rfind(r, 0) != 0) continue;
        const AssetDef* fa = A(out[q].asset.c_str()); const PropDef* pd = fa ? sys.prop(fa) : nullptr;
        if (pd && pd->blocking && pd->mount <= 0) { last = q; break; }
      }
      if (last < 0) break;
      std::string gone = out[last].slot;
      out.erase(std::remove_if(out.begin(), out.end(), [&](const PlanItem& it) { return it.slot == gone || it.slot.rfind(gone + "/", 0) == 0; }), out.end());
      if (getenv("NMK_GX2_DEBUG")) logf("gx2 walkable %s %s: removed %s", ha->id.c_str(), r.c_str(), gone.c_str());
    }
  }
  void room(int k) {
    const auto& R = ri.rooms[k];
    std::string r = "r" + std::to_string(k) + "/";
    json T = rules().contains("types") ? rules()["types"].value(roomTypeKey(R.type), json::object()) : json::object();
    if (R.hearth) {   // the hearth and the fire laid on it later (lights) keep their space
      vec3 hp = R.hearthPos; vec3 mn(hp.x - 0.9f, R.y0, hp.z - 0.9f), mx(hp.x + 0.9f, R.y0 + 1.3f, hp.z + 0.9f);
      boxes.push_back({mn, mx}); mark(R.storey, vec2(mn.x, mn.z), vec2(mx.x, mx.z), 1);
    }
    {   // a church's tower rooms (stair landings, the belfry) and rooms that are mostly stair or route: wall pieces only
      const auto& S = ri.storeys[R.storey]; int cells = 0, keep = 0;
      for (size_t c = 0; c < S.id.size(); ++c) if (S.id[c] == (int16_t)k) { ++cells; keep += S.keep[c] != 0; }
      bool tower = R.type != ROOM_CHURCH && (ha->id.find("church") != std::string::npos || ha->id.find("chapel") != std::string::npos);
      if (tower || (cells > 0 && keep * 2 > cells)) {
        json W = T.value("wall", json::object()), flat = json::array(); W["floorItems"] = json::array();
        for (auto& x : W.value("items", json::array())) { const AssetDef* xa = A(x.get<std::string>().c_str()); if (xa && !sys.prop(xa)->blocking) flat.push_back(x); }
        W["items"] = flat;   // nothing that sticks out at head height beside a stair
        T = json{{"wall", W}};
      }
    }
    for (auto& s : T.value("steps", json::array())) {
      if (!condOk(s, k)) continue;
      std::string sl = s.value("slot", std::string("piece"));
      bool counted = s.contains("n") || s.contains("perArea");
      int n = counted ? stepCount(s, k, r + sl) : 1;
      for (int c = 0; c < n; ++c) {
        std::string slot = r + sl + (counted ? std::to_string(c) : std::string());
        if (rollStep(s, k, slot)) runStep(k, s, slot);
      }
    }
    if (T.contains("hall")) hallRules(k, T["hall"], r);
    centerFloor(k, T, r);   // before the zones: the middle of the floor first
    zones(k, T, r);
    rugPass(k, T.value("rug", 0.0f), r);
    curtains(k, T.value("curtains", 0.0f), T.value("halfDrawn", 0.0f));
    debrisPass(k, T.value("debris", 0.0f), r);
    if (T.contains("wall")) wallFill(k, T["wall"], r);
    keepWalkable(k, r);
  }
  // GX2: what a room ended up with (furnish log, self-tests, captures)
  InteriorSys::RoomStat stat(int k) {
    InteriorSys::RoomStat s; s.room = k; s.type = ri.rooms[k].type; s.area = ri.rooms[k].area;
    std::string r = "r" + std::to_string(k) + "/";
    for (auto& it : out) {
      if (it.slot.rfind(r, 0) != 0) continue;
      ++s.pieces;
      const AssetDef* fa = A(it.asset.c_str()); const PropDef* pd = fa ? sys.prop(fa) : nullptr;
      s.wallItems += pd && pd->mount > 0;
      s.lights += lightDef(fa) != nullptr;
    }
    std::vector<Edge> E = wallEdges(k); s.wallLen = E.size() * ri.cell;
    bcache.clear();
    for (auto& run : bareRuns(k, E, r, {}, true)) s.bare = std::max(s.bare, run.len);
    return s;
  }
};
}

int InteriorSys::dropPieces(App& app, uint64_t hostId) {
  size_t n0 = app.doc.instances.size();
  app.doc.instances.erase(std::remove_if(app.doc.instances.begin(), app.doc.instances.end(), [&](const Instance& i) {
    return isFurnishPiece(i) && i.extra["furnish"].value("host", (uint64_t)0) == hostId; }), app.doc.instances.end());
  return (int)(n0 - app.doc.instances.size());
}

int InteriorSys::unfurnish(App& app, uint64_t hostId) {
  Instance* h = app.doc.find(hostId); if (!h) return 0;
  size_t n0 = app.doc.instances.size();
  app.doc.instances.erase(std::remove_if(app.doc.instances.begin(), app.doc.instances.end(), [&](const Instance& i) {
    return i.extra.is_object() && i.extra.contains("furnish") && i.extra["furnish"].value("host", (uint64_t)0) == hostId; }), app.doc.instances.end());
  h = app.doc.find(hostId);
  if (h) {   // stays bare: on: false also keeps auto-furnishing away from it
    if (!h->extra.is_object()) h->extra = json::object();
    json& in = h->extra["interior"]; if (!in.is_object()) in = json::object();
    in["on"] = false; in.erase("slots");
  }
  if (h) app.bump(*h);
  app.syncRuntime();
  return (int)(n0 - app.doc.instances.size());
}

int InteriorSys::furnish(App& app, uint64_t hostId, int mode) {
  auto t0 = std::chrono::high_resolution_clock::now();
  Instance* h = app.doc.find(hostId); if (!h) return 0;
  const AssetDef* ha = app.assetFor(*h);
  const RoomInfo* ri = rooms(ha);
  if (ri->rooms.empty()) return 0;
  if (!h->extra.is_object()) h->extra = json::object();
  json& in = h->extra["interior"];
  if (!in.is_object()) in = json::object();
  if (in.value("gen", hostId) != hostId) { in.erase("slots"); in.erase("removed"); }   // a copy: its own pieces, nothing "deleted"
  uint32_t seed = in.value("seed", (uint32_t)(hmix32((uint32_t)hostId * 2654435761u) & 0xFFFFFF));
  if (mode == 1) seed = hmix32(seed + 0x9E3779B9u) & 0xFFFFFF;
  in["seed"] = seed; in["on"] = true; in["gen"] = hostId;
  const bool lightsOnly = lightsOnlyHost(ha);   // L3: bunkers: lamps only, no room templates
  if (lightsOnly) in["lightsOnly"] = true; else in.erase("lightsOnly");
  float density = std::clamp(in.value("density", 1.0f), 0.0f, 2.0f), clutter = std::clamp(in.value("clutter", 0.7f), 0.0f, 2.0f);   // V4: 1.0 is a lived-in house (3x the first templates), 2.0 crowded
  float wear = h->extra.contains("finish") ? h->extra["finish"].value("wear", 1.0f) : 1.0f;
  std::set<std::string> removed;
  for (auto& s : in.value("removed", json::array())) removed.insert(s.get<std::string>());
  std::set<std::string> lastSlots;
  for (auto& s : in.value("slots", json::array())) lastSlots.insert(s.get<std::string>());
  // existing generated pieces: manual overrides stay (unless reset); the rest is regenerated
  std::map<std::string, uint64_t> manual;
  std::set<std::string> present;
  for (auto& i : app.doc.instances) {
    if (!i.extra.is_object() || !i.extra.contains("furnish") || i.extra["furnish"].value("host", (uint64_t)0) != hostId) continue;
    std::string slot = i.extra["furnish"].value("slot", std::string());
    present.insert(slot);
    if (i.extra["furnish"].value("manual", false) && mode != 2) manual[slot] = i.id;
  }
  if (mode == 0) for (auto& s : lastSlots) if (!present.count(s)) removed.insert(s);   // deleted by the user: stays deleted
  if (mode != 0) removed.clear();
  app.doc.instances.erase(std::remove_if(app.doc.instances.begin(), app.doc.instances.end(), [&](const Instance& i) {
    if (!i.extra.is_object() || !i.extra.contains("furnish") || i.extra["furnish"].value("host", (uint64_t)0) != hostId) return false;
    return !manual.count(i.extra["furnish"].value("slot", std::string())); }), app.doc.instances.end());
  h = app.doc.find(hostId);
  {   // built-in furniture leaves the collision before planning: swap the building's static shape in place (a full
      // rebuild of the building -- walls, terrain cut -- is not needed and is slow on large maps)
    auto rit = app.rt.find(hostId);
    if (rit != app.rt.end() && rit->second.rev == h->rev && rit->second.staticBody != 0xffffffffu) { if (ShapePtr sh = staticShape(app, *h, ha)) Physics::setShape(rit->second.staticBody, sh); }
    else app.bump(*h);
  }
  app.syncRuntime();
  h = app.doc.find(hostId);
  Planner P{app, *this, *h, ha, *ri, seed, density, clutter, wear, {}, {}, {}, 0};
  for (auto& S : ri->storeys) P.occ.emplace_back(S.id.size(), 0);
  for (auto& [slot, iid] : manual) {   // overrides occupy their space
    Instance* m = app.doc.find(iid); if (!m) continue;
    const PropDef* pd = prop(app.assetFor(*m)); if (!pd || !pd->blocking) continue;
    vec3 lp = glm::inverse(yawQuat(h->yaw)) * (m->pos - h->pos);
    vec3 mn, mx; turnedBounds(pd->bmin, pd->bmax, wrapDeg(m->yaw - h->yaw), mn, mx); mn += lp; mx += lp;
    P.boxes.push_back({mn, mx});
    for (size_t s = 0; s < ri->storeys.size(); ++s) if (std::fabs(ri->storeys[s].y - lp.y) < 0.5f) P.mark((int)s, vec2(mn.x, mn.z), vec2(mx.x, mx.z), 1);
  }
  std::vector<int> order(ri->rooms.size());
  for (size_t k = 0; k < order.size(); ++k) order[k] = (int)k;
  for (int k : order) if (!lightsOnly) P.room(k);
  {   // L3 (docs/lights.md): a ceiling or table light per room, a fire on each hearth, caged lamps in bunkers
    std::vector<LightPlanItem> li; for (auto& it : P.out) li.push_back({it.slot, it.asset, it.p, it.yaw});
    // GX2: wall fixtures (sconces by doorways) go through the room planner's checks; the planner keeps their space
    LightWallFn wallFix = [&](int k, const std::string& asset, const std::string& slot, int d, float along, LightPlanItem& got) {
      int ci, cj;
      if (lightsOnly || !P.edgeCell(k, d, along, ci, cj) || !P.wallCell(k, asset.c_str(), slot, ci, cj, d)) return false;
      got = {P.out.back().slot, P.out.back().asset, P.out.back().p, P.out.back().yaw};
      P.out.pop_back();
      return true;
    };
    size_t n0 = li.size(); lightsPlanRooms(app, *this, *h, ha, *ri, seed, li, wallFix);
    for (size_t q = n0; q < li.size(); ++q) { PlanItem it; it.slot = li[q].slot; it.asset = li[q].asset; it.p = li[q].p; it.yaw = li[q].yaw; P.out.push_back(it); }
  }
  if (const char* g = getenv("NMK_GX2_GRID"); g && ha->id == g) {   // GX2 debug: each storey's grid as a PPM image
    for (size_t s = 0; s < ri->storeys.size(); ++s) {
      const auto& S = ri->storeys[s];
      std::ofstream f(exeDir() / "selftest_out" / ("grid_" + ha->id + "_s" + std::to_string(s) + ".ppm"), std::ios::binary);
      f << "P6\n" << S.nx << " " << S.nz << "\n255\n";
      for (int j = S.nz - 1; j >= 0; --j) for (int i = 0; i < S.nx; ++i) {
        size_t c = (size_t)j * S.nx + i; int id = S.id[c];
        unsigned char px[3] = {40, 40, 40};
        if (id == RoomInfo::WALL) px[0] = px[1] = px[2] = 0;
        else if (id == RoomInfo::DOORWAY) { px[0] = 40; px[1] = 80; px[2] = 255; }
        else if (id >= 0) { uint32_t h = hmix32((uint32_t)id * 7919u + 13u); px[0] = 90 + (h & 63); px[1] = 90 + ((h >> 8) & 63); px[2] = 90 + ((h >> 16) & 63); }
        if (S.keep[c]) px[0] = 220;
        if (P.occ[s][c] == 1) { px[0] = 30; px[1] = 200; px[2] = 30; } else if (P.occ[s][c] == 2) { px[1] = 220; }
        f.write((const char*)px, 3);
      }
    }
  }
  lastStats.clear();
  if (!lightsOnly) {   // GX2: what each room ended up with
    std::string rep;
    for (size_t k = 0; k < ri->rooms.size(); ++k) {
      lastStats.push_back(P.stat((int)k));
      const RoomStat& st = lastStats.back();
      char b[160]; std::snprintf(b, sizeof b, " r%zu %s %.0fm2: %d pieces, %d on walls, %d lights, bare %.1fm;", k, roomTypeKey(st.type), st.area, st.pieces, st.wallItems, st.lights, st.bare);
      rep += b;
    }
    logf("interior: dressed %s:%s", ha->id.c_str(), rep.c_str());
  }
  // occasional broken items in worn buildings (restrained: intact houses stay intact)
  for (auto& it : P.out) if (it.asset.rfind("furn_chair", 0) == 0 && h01(seed, strHash(it.slot), 8u) < 0.05f * std::max(0.0f, wear - 0.6f)) it.broken = true;
  json slots = json::array();
  int made = 0;
  quat hq = yawQuat(h->yaw);
  vec3 hp = h->pos; float hy = h->yaw;
  for (auto& it : P.out) {
    slots.push_back(it.slot);
    bool gone = removed.count(it.slot) || manual.count(it.slot);
    for (auto& r : removed) if (it.slot.rfind(r + "/", 0) == 0) gone = true;   // things on / around a deleted piece
    if (gone) continue;
    const AssetDef* fa = app.lib.find(it.asset); if (!fa) continue;
    Instance n; n.id = app.doc.newId(); n.assetId = fa->id; n.assetVersion = fa->version;
    n.pos = hp + hq * it.p; n.yaw = wrapDeg(hy + it.yaw);
    n.extra = json::object();
    json loc = {it.p.x, it.p.y, it.p.z, it.yaw};
    n.extra["furnish"] = {{"host", hostId}, {"slot", it.slot}, {"loc", loc}, {"hp", {hp.x, hp.y, hp.z, hy}}};
    if (it.broken) n.extra["propBroken"] = true;
    app.bump(n);
    app.doc.instances.push_back(n);
    ++made;
  }
  h = app.doc.find(hostId);
  h->extra["interior"]["slots"] = slots;
  json rem = json::array(); for (auto& s : removed) rem.push_back(s);
  h->extra["interior"]["removed"] = rem;
  app.dirty = true;
  app.syncRuntime();
  lastFurnishMs = (float)std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - t0).count();
  if (getenv("NMK_INTERIORS_QUICK")) { std::string l; for (auto& it : P.out) l += " " + it.slot + "=" + it.asset.substr(5); logf("interior: placed%s", l.c_str()); }
  logf("interior: furnished %s #%llu: %d pieces (%zu slots, %zu kept overrides, %zu removed) in %.1f ms, %d solid queries; no room for:%s", ha->id.c_str(), (unsigned long long)hostId, made, P.out.size(), manual.size(), removed.size(), lastFurnishMs, P.calls, P.failed.empty() ? " -" : P.failed.c_str());
  return made;
}

void InteriorSys::sync(App& app) {
  updateBatches(app);   // every frame, every mode: merged furniture meshes follow breaks, edits and selection
  if (app.mode != Mode::Creative) return;
  auto syncSig = [&]() { return ((uint64_t)app.revCounter << 32) ^ (uint64_t)app.doc.instances.size(); };
  if (syncSig() == syncSig_) return;   // nothing moved, appeared or went away since the last pass
  ++syncPasses;
  bool changed = false;
  std::map<uint64_t, Instance*> hosts;
  for (auto& i : app.doc.instances) if (i.extra.is_object() && i.extra.contains("interior") && i.extra["interior"].value("on", false)) hosts[i.id] = &i;   // L3: lit bunkers too
  std::vector<uint64_t> orphans, regen;
  for (auto& [id, h] : hosts) if (h->extra["interior"].value("gen", (uint64_t)0) != id) regen.push_back(id);   // a duplicated building
  for (auto& i : app.doc.instances) {
    if (!i.extra.is_object() || !i.extra.contains("furnish")) continue;
    json& f = i.extra["furnish"];
    uint64_t hid = f.value("host", (uint64_t)0);
    auto hit = hosts.find(hid);
    if (hit == hosts.end()) { if (!app.doc.find(hid)) orphans.push_back(i.id); continue; }
    Instance& h = *hit->second;
    if (!f.contains("hp") || !f.contains("loc")) continue;
    json& hp = f["hp"]; json& loc = f["loc"];
    vec3 hpos(hp[0].get<float>(), hp[1].get<float>(), hp[2].get<float>()); float hyaw = hp[3].get<float>();
    vec3 lp(loc[0].get<float>(), loc[1].get<float>(), loc[2].get<float>()); float ly = loc[3].get<float>();
    if (glm::length(hpos - h.pos) > 1e-3f || std::fabs(wrapDeg(hyaw - h.yaw + 180) - 180) > 1e-2f) {   // the host moved: follow it
      i.pos = h.pos + yawQuat(h.yaw) * lp; i.yaw = wrapDeg(h.yaw + ly);
      hp = {h.pos.x, h.pos.y, h.pos.z, h.yaw};
      i.rev = ++app.revCounter; changed = true;
      continue;
    }
    vec3 want = h.pos + yawQuat(h.yaw) * lp;
    if (glm::length(want - i.pos) > 1e-3f || std::fabs(wrapDeg(h.yaw + ly - i.yaw + 180) - 180) > 1e-2f) {   // the user moved it: an override
      vec3 nl = glm::inverse(yawQuat(h.yaw)) * (i.pos - h.pos);
      loc = {nl.x, nl.y, nl.z, wrapDeg(i.yaw - h.yaw)};
      f["manual"] = true; changed = true;
    }
  }
  if (!orphans.empty()) {
    app.doc.instances.erase(std::remove_if(app.doc.instances.begin(), app.doc.instances.end(), [&](const Instance& i) { return std::find(orphans.begin(), orphans.end(), i.id) != orphans.end(); }), app.doc.instances.end());
    if (app.selected && !app.doc.find(app.selected)) app.selected = 0;
    changed = true;
  }
  for (uint64_t id : regen) furnish(app, id, 0);
  if (changed) app.dirty = true;
  syncSig_ = syncSig();
}

// ============================================================ editor
void InteriorSys::editorPanel(App& app, Instance& s) {
  lightsEditorPanel(app, s);   // L3: the Light section (colour, intensity, on / off)
  if (!app.doc.find(s.id)) return;
  const AssetDef* a = app.assetFor(s);
  if (const PropDef* pd = prop(a)) {
    if (ImGui::CollapsingHeader("Furniture", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::TextDisabled("%s | %s | %s", pd->family.c_str(), pd->blocking ? "simple collider" : "cosmetic: no blocking collision", pd->rounds ? (std::to_string(pd->rounds) + " rounds to break").c_str() : "rounds do not break it");
      if (s.extra.is_object() && s.extra.contains("furnish")) {
        auto& f = s.extra["furnish"];
        ImGui::TextDisabled("Room template piece of #%llu (%s)%s", (unsigned long long)f.value("host", (uint64_t)0), f.value("slot", std::string()).c_str(), f.value("manual", false) ? ", moved by hand: kept on regenerate" : "");
      }
      if (s.extra.is_object() && s.extra.value("propBroken", false)) ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "Applied damage: broken");
    }
    return;
  }
  if (!a->enterable) return;
  const RoomInfo* ri = rooms(a);
  bool furnished = hideBuiltin(s);
  json fin = s.extra.is_object() && s.extra.contains("finish") ? s.extra["finish"] : json::object();
  if (!ImGui::CollapsingHeader("Interior: rooms, finishes and furniture")) return;
  ImGui::TextDisabled("%zu rooms detected, %zu keep-clear volumes (doors, stairs, windows)", ri->rooms.size(), ri->keep.size());
  for (size_t k = 0; k < ri->rooms.size(); ++k) ImGui::BulletText("%s, %.1f m2 (storey %d)%s", roomTypeName(ri->rooms[k].type), ri->rooms[k].area, ri->rooms[k].storey, ri->rooms[k].hearth ? ", hearth" : "");
  float wear = fin.value("wear", 1.0f), pal = fin.value("palette", 1.0f);
  bool act = false, ed = false;
  ImGui::SliderFloat("Wear", &wear, 0.0f, 2.0f, "%.2f"); act |= ImGui::IsItemActivated(); ed |= ImGui::IsItemEdited();
  ImGui::SliderFloat("Palette variation", &pal, 0.0f, 1.0f, "%.2f"); act |= ImGui::IsItemActivated(); ed |= ImGui::IsItemEdited();
  if (act) app.pushUndo();
  Instance* sp = app.doc.find(s.id);
  if (ed && sp) { if (!sp->extra.is_object()) sp->extra = json::object(); sp->extra["finish"]["wear"] = wear; sp->extra["finish"]["palette"] = pal; app.bump(*sp); }
  if (!sp) return;
  json in = sp->extra.is_object() && sp->extra.contains("interior") ? sp->extra["interior"] : json::object();
  float dens = in.value("density", 1.0f), clut = in.value("clutter", 0.7f);
  bool a2 = false, e2 = false;
  ImGui::SliderFloat("Furniture density", &dens, 0.0f, 2.0f, "%.2f"); a2 |= ImGui::IsItemActivated(); e2 |= ImGui::IsItemEdited();
  ImGui::SliderFloat("Clutter density", &clut, 0.0f, 2.0f, "%.2f"); a2 |= ImGui::IsItemActivated(); e2 |= ImGui::IsItemEdited();
  if (a2) { app.pushUndo(); sp = app.doc.find(s.id); }
  if (e2 && sp) { if (!sp->extra.is_object()) sp->extra = json::object(); sp->extra["interior"]["density"] = dens; sp->extra["interior"]["clutter"] = clut; app.dirty = true; }
  int pieces = 0, overrides = 0;
  for (auto& i : app.doc.instances) if (i.extra.is_object() && i.extra.contains("furnish") && i.extra["furnish"].value("host", (uint64_t)0) == s.id) { ++pieces; overrides += i.extra["furnish"].value("manual", false) ? 1 : 0; }
  if (furnished) ImGui::TextDisabled("Furnished by room templates: %d pieces (%d moved by hand), seed %u", pieces, overrides, in.value("seed", 0u));
  else ImGui::TextDisabled("Built-in furniture shown. Room templates replace it with the furniture family.");
  if (ImGui::Button(furnished ? "Regenerate" : "Furnish rooms")) { app.pushUndo(); furnish(app, s.id, 0); }
  if (furnished) {
    ImGui::SameLine(); if (ImGui::Button("New layout")) { app.pushUndo(); furnish(app, s.id, 1); }
    ImGui::SameLine(); if (ImGui::Button("Reset")) { app.pushUndo(); furnish(app, s.id, 2); }
    if (ImGui::Button("Remove template furniture (built-in back)")) { app.pushUndo(); unfurnish(app, s.id); }
  }
  ImGui::TextDisabled("Regenerate keeps pieces you moved and leaves out pieces you deleted.");
}
