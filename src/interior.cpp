// Walls, interiors and furniture (docs/interiors.md).
//   * furniture props: simple colliders, small-arms and blast damage with wall shielding, bounded rigid fragments,
//     support loss, session overlay / Apply;
//   * room detection per enterable asset: 0.1 m occupancy grid per storey, doorways, keep-clear volumes (door
//     swings, stairs and landings, windows from the sill up), window sills;
//   * seeded room templates (kitchen / dining, sitting room, bedroom, utility / storage, workshop, hall) placing
//     ordinary furniture instances tagged extra.furnish = {host, slot};
//   * per building copy finish parameters (SurfaceParams) for the wall / floor patterns 17-22.
#include "interior.h"
#include "app.h"
#include <imgui.h>
#include <chrono>
#include <algorithm>

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

const char* roomTypeName(int t) {
  static const char* n[] = {"Kitchen / dining", "Sitting room", "Bedroom", "Utility / storage", "Workshop", "Hall / passage"};
  return t >= 0 && t < 6 ? n[t] : "Room";
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
  float density = d.family == "masonry" ? 1600.0f : d.family == "ceramic" ? 900.0f : d.family == "cloth" ? 200.0f : 600.0f;
  for (size_t fi = 0; fi < d.fragments.size() && fi < 4; ++fi) {
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
    bool flagFloor = (m == "hearthstone" || m == "stone_plinth") && p.size.y <= 0.06f && p.size.x > 1.5f && p.size.z > 1.5f;   // flagged floors (workshop, dairy)
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
      if (area < 2.5f || ri.rooms.size() >= 16) { for (int c : cells) id[c] = RoomInfo::NOFLOOR; continue; }
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
  if (a->walkthrough.is_object() && !ri.storeys.empty()) {
    float lvl = ri.storeys[0].y; vec2 prev(0); bool has = false;
    if (a->walkthrough.contains("start")) { vec3 sp = jvec3(a->walkthrough["start"]["pos"]); prev = vec2(sp.x, sp.z); has = true; }
    auto markLeg = [&](vec2 p0, vec2 p1, float level) {
      int s = 0; for (size_t q = 0; q < ri.storeys.size(); ++q) if (std::fabs(ri.storeys[q].y - level) < std::fabs(ri.storeys[s].y - level)) s = (int)q;
      auto& S = ri.storeys[s];
      vec2 d = p1 - p0; float L2 = std::max(glm::dot(d, d), 1e-6f);
      for (int j = 0; j < S.nz; ++j) for (int i = 0; i < S.nx; ++i) {
        vec2 c(S.o.x + (i + 0.5f) * C, S.o.y + (j + 0.5f) * C);
        float u = std::clamp(glm::dot(c - p0, d) / L2, 0.0f, 1.0f);
        if (glm::length(c - (p0 + d * u)) < 0.4f) S.keep[(size_t)j * S.nx + i] = 1;
      }
    };
    for (auto& s : a->walkthrough.value("steps", json::array())) {
      if (!s.contains("goto")) continue;
      vec2 p(s["goto"][0].get<float>(), s["goto"][1].get<float>());
      if (has) markLeg(prev, p, lvl);
      if (s.contains("expectY")) { lvl = s["expectY"].get<float>(); if (has) markLeg(prev, p, lvl); }
      prev = p; has = true;
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
  for (size_t k = 0; k < ri.rooms.size(); ++k) ri.rooms[k].type = roomType(a, ri, (int)k);
  logf("interior: %s: %zu storeys, %zu rooms, %zu keep-clear volumes, %zu sills", a->id.c_str(), ri.storeys.size(), ri.rooms.size(), ri.keep.size(), ri.sills.size());
  return &ri;
}

int InteriorSys::roomType(const AssetDef* a, const RoomInfo& ri, int k) const {
  const RoomInfo::Room& R = ri.rooms[k];
  vec2 e = R.mx - R.mn;
  const std::string& id = a->id;
  if (id.find("workshop") != std::string::npos) return R.area < 4 ? ROOM_UTILITY : ROOM_WORKSHOP;
  if (id.find("storehouse") != std::string::npos || id.find("warehouse") != std::string::npos || id.find("dairy") != std::string::npos) return ROOM_UTILITY;
  if (std::min(e.x, e.y) < 1.7f || R.cells < (int)(0.55f * (e.x * e.y) / (ri.cell * ri.cell))) return R.area < 4 ? ROOM_UTILITY : ROOM_HALL;
  if (R.area < 5.0f) return ROOM_UTILITY;
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
  return i.extra.is_object() && i.extra.contains("interior") && i.extra["interior"].value("on", false);
}

ShapePtr InteriorSys::staticShape(App& app, const Instance& i, const AssetDef* a) {
  if (const PropDef* pd = prop(a)) {
    PropRT& s = st[i.id];
    s = PropRT{};
    s.applied = i.extra.is_object() && i.extra.value("propBroken", false);
    s.broken = s.applied;
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
  int n = std::min((int)ri->rooms.size(), 16);
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
      default: wall = 4; floor = R.storey == 0 ? 3 : 0; break;
    }
    if (civic) { wall = R.type == ROOM_UTILITY ? 5 : 4; floor = R.storey == 0 && R.type == ROOM_HALL ? 3 : (hB < 0.5f ? 1 : 0); }
    sp.roomA[k] = vec4(R.mn.x, R.mn.y, R.mx.x, R.mx.y);
    sp.roomB[k] = vec4(R.y0, R.y1, (float)wall, (float)floor);
  }
  sp.inst.w = (float)n;
  sp.sills = std::min((int)ri->sills.size(), 12);
  for (int k = 0; k < sp.sills; ++k) {
    const auto& s = ri->sills[k];
    sp.sillA[k] = vec4(s.c.x, s.c.y, s.c.z, s.halfW);
    sp.sillB[k] = vec4(s.n.x, s.n.y, 0, 0);
  }
  (void)app;
  return &sp;
}

// ============================================================ drawing
bool InteriorSys::propDraw(App&, const Instance& i, const InstanceRT& r, const mat4& M, std::vector<DrawItem>& op, std::vector<DrawItem>& gl) {
  const PropDef* pd = prop(r.asset);
  if (!pd) return false;
  auto it = st.find(i.id);
  PropRT s = it == st.end() ? PropRT{} : it->second;
  if (s.broken) { if (pd->brokenMesh.count) op.push_back({&pd->brokenMesh, M}); return true; }
  op.push_back({&r.asset->opaque, M});
  if (r.asset->glass.count && !s.glassBroken) gl.push_back({&r.asset->glass, M});
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
  if (pd->family == "masonry") { app.fx.impact(at, -dir, "masonry"); return 1; }
  if (!s.broken) {
    if (pd->glass >= 0 && !s.glassBroken) { s.glassBroken = true; ++glassBreaks; app.audio.play("glass_break", at, 0.6f); app.fx.impact(at, -dir, "glass"); }
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

bool InteriorSys::breakProp(App& app, Instance& i, InstanceRT& r, vec3 push, const char* why) {
  const PropDef* pd = prop(r.asset);
  if (!pd) return false;
  PropRT& s = st[i.id];
  if (s.broken) return false;
  s.broken = true; s.glassBroken = true; ++breaks;
  lastBreak = r.asset->id + " #" + std::to_string(i.id) + " (" + why + ")";
  Physics::remove(r.staticBody); r.staticBody = 0xffffffffu;
  if (pd->brokenShape) r.staticBody = Physics::addStatic(pd->brokenShape, i.pos, yawQuat(i.yaw), i.id << 16);
  mat4 M = app.instanceMatrix(i);
  quat q = yawQuat(i.yaw);
  vec3 c = vec3(M * vec4((pd->bmin + pd->bmax) * 0.5f, 1));
  // bounded rigid fragments with capped speed; family-coloured particles for the fine debris
  float sp = std::min(glm::length(push), 7.0f);
  vec3 dirN = sp > 1e-3f ? push / std::max(glm::length(push), 1e-4f) : vec3(0, -1, 0);
  uint32_t seed = (uint32_t)i.id * 2654435761u ^ (uint32_t)breaks * 40503u;
  int n = 0;
  for (auto& f : pd->frags) {
    if (n >= 4) break;
    vec3 wp = vec3(M * vec4(f->center, 1)) + vec3(0, 0.03f, 0);
    wp.y = std::max(wp.y, app.terrain.height(wp.x, wp.z) + glm::length(f->half) + 0.01f);   // clear of uneven ground
    Frag fr; fr.inst = i.id; fr.mesh = &f->mesh;
    fr.body = Physics::addDynamic(f->shape, wp, q, (i.id << 16) | 0xFFE0u, f->mass);
    vec3 jit(h01(seed, n, 1) - 0.5f, h01(seed, n, 2) * 0.6f, h01(seed, n, 3) - 0.5f);
    vec3 v = dirN * sp * (0.6f + 0.4f * h01(seed, n, 4)) + jit * 1.6f + vec3(0, sp > 0.5f ? 1.2f : 0.0f, 0);
    v.y = std::max(v.y, 0.3f);   // never driven down into the floor / ground
    if (glm::length(v) > 7.0f) v = glm::normalize(v) * 7.0f;
    Physics::setVelocity(fr.body, v, vec3(h01(seed, n, 5) - 0.5f, h01(seed, n, 6) - 0.5f, h01(seed, n, 7) - 0.5f) * 6.0f);
    Physics::setDamping(fr.body, 0.2f, 0.4f);
    Physics::setMaterial(fr.body, 0.7f, 0.15f, true);   // continuous collision: no tunnelling through floors
    fr.m = glm::translate(mat4(1), wp) * glm::mat4_cast(q);
    frags.push_back(fr); ++n; ++fragsSpawned;
  }
  size_t live = 0; for (auto& f : frags) live += !f.frozen;
  maxLiveSeen = std::max(maxLiveSeen, (int)std::min(live, maxLive));
  freezeOldest();
  vec4 col = pd->family == "ceramic" ? vec4(0.8f, 0.78f, 0.72f, 0.7f) : pd->family == "glass" ? vec4(0.7f, 0.75f, 0.75f, 0.35f)
           : pd->family == "masonry" ? vec4(0.55f, 0.52f, 0.46f, 0.7f) : pd->family == "cloth" || pd->family == "paper" ? vec4(0.62f, 0.58f, 0.5f, 0.45f)
           : vec4(0.42f, 0.33f, 0.22f, 0.65f);
  for (int k = 0; k < 10; ++k) {
    vec3 d(h01(seed, k, 11) - 0.5f, h01(seed, k, 12), h01(seed, k, 13) - 0.5f);
    app.fx.smoke(c + d * 0.3f, (d * 2.5f + dirN * sp * 0.3f), 0.05f + 0.06f * h01(seed, k, 14), 0.5f + h01(seed, k, 15), col, 0.6f, 0.1f);
  }
  if (pd->family == "glass" || pd->glass >= 0) app.fx.flash(c, 0.12f, 0.04f, vec4(2, 2, 2, 1));
  app.fx.impact(c, vec3(0, 1, 0), pd->family == "masonry" ? "masonry" : "wood");
  app.audio.play(pd->family == "ceramic" || pd->family == "glass" ? "glass_break" : "door_break", c, pd->family == "cloth" || pd->family == "paper" ? 0.2f : 0.6f);
  static int depth = 0;
  if (depth < 4) { ++depth; checkSupport(app, c, std::max(1.5f, glm::length(pd->bmax - pd->bmin))); --depth; }   // what stood on it falls
  return true;
}

bool InteriorSys::supported(App& app, const Instance& i, const AssetDef* a, int* hitsOut) {
  const PropDef* pd = prop(a);
  if (!pd) return true;
  quat q = yawQuat(i.yaw);
  uint64_t self = i.id << 16;
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
  float R = radius * 1.4f + 0.3f;
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
    float e = dmg * std::pow(std::max(0.0f, 1.0f - dist / R), 1.5f) * vis;
    if (dist < 0.15f) e = std::max(e, dmg * vis);
    PropRT& s = st[i.id];
    if (pd->glass >= 0 && !s.glassBroken && e > pd->blast * 0.15f) { s.glassBroken = true; ++glassBreaks; }
    if (e >= pd->blast) {
      vec3 push = c - p; float pl = glm::length(push);
      push = (pl > 1e-3f ? push / pl : d) * (2.0f + 5.0f * std::min(1.0f, e / (pd->blast * 3.0f)));
      if (breakProp(app, i, it->second, push, kind == WALL_GRENADE ? "grenade" : kind == WALL_AP ? "AP shot" : kind == WALL_ROCKET ? "rocket" : "HE blast")) any = true;
    }
  }
  checkSupport(app, p, R + 3.0f);   // floors and tables brought down by this blast drop what stood on them
  (void)any;
}

void InteriorSys::step(App& app, float dt) {
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
    if (s != st.end() && (s->second.broken != applied || s->second.glassBroken || s->second.hits > 0)) app.buildRT(i, it->second);
  }
}

int InteriorSys::applyToMap(App& app) {
  int n = 0;
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
  float rnd(uint32_t a, uint32_t b = 0) { return h01(seed, a, b); }
  const AssetDef* A(const char* id) { return app.lib.find(id); }
  vec3 world(vec3 lp) const { return host.pos + yawQuat(host.yaw) * lp; }
  bool rectCells(int s, int room, vec2 mn, vec2 mx, int mode) {   // mode 0: free; 1: not furniture (clearance); 2: room only
    const auto& S = ri.storeys[s]; const float C = ri.cell;
    int i0 = (int)std::floor((mn.x - S.o.x) / C), i1 = (int)std::ceil((mx.x - S.o.x) / C) - 1;
    int j0 = (int)std::floor((mn.y - S.o.y) / C), j1 = (int)std::ceil((mx.y - S.o.y) / C) - 1;
    if (i0 < 0 || j0 < 0 || i1 >= S.nx || j1 >= S.nz) return false;
    for (int j = j0; j <= j1; ++j) for (int i = i0; i <= i1; ++i) {
      size_t c = (size_t)j * S.nx + i;
      if (S.id[c] != room) return false;
      if (mode < 2 && S.keep[c]) return false;
      if (mode == 0 && occ[s][c]) return false;
      if (mode == 1 && occ[s][c] == 1) return false;
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
    const AssetDef* fa = A(asset); if (!fa) return false;
    const PropDef* pd = sys.prop(fa);
    const auto& R = ri.rooms[k];
    vec3 mn, mx; bounds(pd, p, yaw, mn, mx);
    if (!rectCells(R.storey, k, vec2(mn.x, mn.z), vec2(mx.x, mx.z), mode)) return false;
    if ((pd->blocking || pd->mount > 0) && !windowFree(mn, mx)) return false;
    for (auto& b : boxes) if (boxOverlap(mn + vec3(0.01f), mx - vec3(0.01f), b.first, b.second)) return false;
    vec3 fmn(0), fmx(0);
    if (front && pd->front > 0) {   // access clearance in front of it must stay walkable
      quat q = yawQuat(yaw); vec3 f = q * vec3(0, 0, 1);
      vec3 a0 = mn, a1 = mx;
      if (std::fabs(f.x) > 0.5f) { if (f.x > 0) { a0.x = mx.x; a1.x = mx.x + pd->front; } else { a1.x = mn.x; a0.x = mn.x - pd->front; } }
      else { if (f.z > 0) { a0.z = mx.z; a1.z = mx.z + pd->front; } else { a1.z = mn.z; a0.z = mn.z - pd->front; } }
      if (!rectCells(R.storey, k, vec2(a0.x, a0.z), vec2(a1.x, a1.z), 1)) return false;
      fmn = a0; fmx = a1;
    }
    if (!solidFree(fa, pd, p, yaw)) return false;
    if (hard && pd->blocking) { mark(R.storey, vec2(mn.x, mn.z), vec2(mx.x, mx.z), 1); boxes.push_back({mn, mx}); }
    if (front && pd->front > 0) mark(R.storey, vec2(fmn.x, fmn.z), vec2(fmx.x, fmx.z), 2);
    PlanItem it; it.slot = slot; it.asset = asset; it.p = p; it.yaw = yaw;
    out.push_back(it);
    if (got) *got = it;
    return true;
  }
  // against a wall: back to the wall, front clearance kept; corner > 0 prefers corners, < 0 the middle of a wall
  bool wall(int k, const char* asset, const std::string& slot, float corner, PlanItem* got = nullptr, float lift = 0) {
    const AssetDef* fa = A(asset); if (!fa) return false;
    const PropDef* pd = sys.prop(fa);
    const auto& R = ri.rooms[k]; const auto& S = ri.storeys[R.storey]; const float C = ri.cell;
    struct Cand { vec3 p; float yaw, score; };
    std::vector<Cand> cs;
    const int di[4] = {0, -1, 0, 1}, dj[4] = {-1, 0, 1, 0};
    const float yaws[4] = {0, 90, 180, 270};
    for (int j = 0; j < S.nz; ++j) for (int i = 0; i < S.nx; ++i) {
      if (S.id[(size_t)j * S.nx + i] != k) continue;
      if (((i * 7 + j * 3) % 2) != 0) continue;   // every other cell is plenty
      for (int d = 0; d < 4; ++d) {
        int ni = i + di[d], nj = j + dj[d];
        if (ni < 0 || nj < 0 || ni >= S.nx || nj >= S.nz || S.id[(size_t)nj * S.nx + ni] != RoomInfo::WALL) continue;
        vec3 mn, mx; turnedBounds(pd->bmin, pd->bmax, yaws[d], mn, mx);
        vec3 p(S.o.x + (i + 0.5f) * C, R.y0 + lift, S.o.y + (j + 0.5f) * C);
        float face = d == 0 ? S.o.y + j * C : d == 2 ? S.o.y + (j + 1) * C : d == 1 ? S.o.x + i * C : S.o.x + (i + 1) * C;
        {   // the real wall face along this direction (the cell edge can be up to half a cell off)
          vec3 dl = d == 0 ? vec3(0, 0, -1) : d == 2 ? vec3(0, 0, 1) : d == 1 ? vec3(-1, 0, 0) : vec3(1, 0, 0);
          vec3 o(S.o.x + (i + 0.5f) * C, R.y0 + 0.6f + lift * 0.5f, S.o.y + (j + 0.5f) * C);
          Physics::Hit wh;
          if (Physics::castRayEx(world(o), yawQuat(host.yaw) * dl, 0.3f, wh)) { vec3 lw = glm::inverse(yawQuat(host.yaw)) * (wh.pos - host.pos); face = (d == 0 || d == 2) ? lw.z : lw.x; }
        }
        if (d == 0) p.z = face + 0.012f - mn.z;
        if (d == 2) p.z = face - 0.012f - mx.z;
        if (d == 1) p.x = face + 0.012f - mn.x;
        if (d == 3) p.x = face - 0.012f - mx.x;
        vec2 cc = (R.mn + R.mx) * 0.5f, he = (R.mx - R.mn) * 0.5f;
        float edge = glm::length(glm::abs(vec2(p.x, p.z) - cc) / glm::max(he, vec2(0.5f)));   // ~1.41 in a corner
        cs.push_back({p, yaws[d], -corner * edge + rnd((uint32_t)(i * 131 + j), strHash(slot)) * 0.8f});
      }
    }
    std::sort(cs.begin(), cs.end(), [](const Cand& a, const Cand& b) { return a.score < b.score; });
    for (auto& c : cs) if (tryAt(k, asset, slot, c.p, c.yaw, true, true, 0, got)) return true;
    return false;
  }
  bool center(int k, const char* asset, const std::string& slot, float ring, PlanItem* got = nullptr) {
    const AssetDef* fa = A(asset); if (!fa) return false;
    const PropDef* pd = sys.prop(fa);
    const auto& R = ri.rooms[k]; const auto& S = ri.storeys[R.storey]; const float C = ri.cell;
    vec2 e = R.mx - R.mn;
    float yaw = e.x >= e.y ? 0.0f : 90.0f;
    struct Cand { vec3 p; float score; };
    std::vector<Cand> cs;
    vec2 cc = (R.mn + R.mx) * 0.5f;
    for (int j = 0; j < S.nz; j += 2) for (int i = 0; i < S.nx; i += 2) {
      if (S.id[(size_t)j * S.nx + i] != k) continue;
      vec3 p(S.o.x + (i + 0.5f) * C, R.y0, S.o.y + (j + 0.5f) * C);
      cs.push_back({p, glm::length(vec2(p.x, p.z) - cc) + rnd((uint32_t)(i * 31 + j), strHash(slot)) * 0.7f});
    }
    std::sort(cs.begin(), cs.end(), [](const Cand& a, const Cand& b) { return a.score < b.score; });
    for (auto& c : cs) {
      vec3 mn, mx; bounds(pd, c.p, yaw, mn, mx);
      if (ring > 0 && !rectCells(R.storey, k, vec2(mn.x - ring, mn.z - ring), vec2(mx.x + ring, mx.z + ring), 1)) continue;
      if (tryAt(k, asset, slot, c.p, yaw, true, false, 0, got)) {
        if (ring > 0) mark(R.storey, vec2(mn.x - ring, mn.z - ring), vec2(mx.x + ring, mx.z + ring), 2);
        return true;
      }
    }
    return false;
  }
  void chairsAround(int k, const PlanItem& t, const char* chair, int want, bool ends) {
    const PropDef* tp = sys.prop(A(t.asset.c_str()));
    float W = tp->bmax.x - tp->bmin.x, D = tp->bmax.z - tp->bmin.z;
    quat q = yawQuat(t.yaw);
    struct S { vec3 off; float yaw; };
    std::vector<S> spots = {{vec3(-W * 0.25f, 0, D * 0.5f + 0.2f), 180}, {vec3(W * 0.25f, 0, -D * 0.5f - 0.2f), 0}, {vec3(W * 0.25f, 0, D * 0.5f + 0.2f), 180}, {vec3(-W * 0.25f, 0, -D * 0.5f - 0.2f), 0}};
    if (ends) { spots.push_back({vec3(W * 0.5f + 0.22f, 0, 0), 270}); spots.push_back({vec3(-W * 0.5f - 0.22f, 0, 0), 90}); }
    int n = 0;
    for (size_t s = 0; s < spots.size() && n < want; ++s) {
      vec3 p = t.p + q * spots[s].off;
      float jitter = (rnd(strHash(t.slot), (uint32_t)s) - 0.5f) * 12.0f;
      if (tryAt(k, chair, t.slot + "/chair" + std::to_string(s), p, wrapDeg(t.yaw + spots[s].yaw + jitter), true, false, 1, nullptr)) ++n;
    }
  }
  void onTop(const PlanItem& base, const std::vector<const char*>& choices, int count) {
    const AssetDef* ba = A(base.asset.c_str()); if (!ba) return;
    const PropDef* bp = sys.prop(ba);
    if (bp->surfaces.empty() || choices.empty()) return;
    quat q = yawQuat(base.yaw);
    for (int n = 0; n < count; ++n) {
      uint32_t h = strHash(base.slot) + (uint32_t)n * 977u;
      size_t si = (size_t)(rnd(h, 1) * bp->surfaces.size()) % bp->surfaces.size();
      vec4 s = bp->surfaces[si];
      const AssetDef* pa = A(choices[(size_t)(rnd(h, 2) * choices.size()) % choices.size()]); if (!pa) continue;
      const PropDef* pp = sys.prop(pa);
      vec3 he = (pp->bmax - pp->bmin) * 0.5f; float r = std::max(he.x, he.z);
      if (s.z - s.x < 2 * r || s.w - s.y < 2 * r) continue;
      vec3 lp(s.x + r + (s.z - s.x - 2 * r) * rnd(h, 3), bp->surfY[si], s.y + r + (s.w - s.y - 2 * r) * rnd(h, 4));
      lp -= vec3((pp->bmin.x + pp->bmax.x) * 0.5f, 0, (pp->bmin.z + pp->bmax.z) * 0.5f);
      PlanItem it; it.slot = base.slot + "/top" + std::to_string(n); it.asset = pa->id; it.p = base.p + q * lp;
      it.yaw = wrapDeg(base.yaw + (rnd(h, 5) - 0.5f) * 50.0f);
      bool clash = false;   // two props on one top never overlap
      for (auto& o : out) if (o.slot.rfind(base.slot + "/top", 0) == 0 && glm::length(vec2(o.p.x - it.p.x, o.p.z - it.p.z)) < r + 0.12f) clash = true;
      if (!clash) out.push_back(it);
    }
  }
  bool rug(int k, const char* asset, const std::string& slot, vec2 at, float yaw) {
    const auto& R = ri.rooms[k];
    vec3 p(at.x, R.y0, at.y);
    return tryAt(k, asset, slot, p, yaw, false, false, 2, nullptr) || false;
  }
  void curtains(int k) {
    const auto& R = ri.rooms[k];
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
        vec3 p = (mn + mx) * 0.5f; p[na] = face;
        p.y = mx.y + 0.05f - 1.5f;
        if (ri.at(R.storey, p + (na == 0 ? vec3(in * 0.3f, 0, 0) : vec3(0, 0, in * 0.3f))) != k) continue;
        float yaw = na == 2 ? (in < 0 ? 180.0f : 0.0f) : (in < 0 ? 270.0f : 90.0f);
        PlanItem it; it.slot = "r" + std::to_string(k) + "/curtains" + std::to_string(n++); it.asset = "furn_curtains"; it.p = p; it.yaw = yaw;
        out.push_back(it);
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
  void room(int k) {
    const auto& R = ri.rooms[k];
    std::string r = "r" + std::to_string(k) + "/";
    uint32_t base = (uint32_t)k * 1009u;
    PlanItem t, w;
    vec2 cc = (R.mn + R.mx) * 0.5f; vec2 e = R.mx - R.mn;
    switch (R.type) {
      case ROOM_KITCHEN: {
        wall(k, rnd(base, 1) < 0.5f ? "furn_hutch" : "furn_hutch_glazed", r + "hutch", -0.5f, &w);
        onTop(w, {"furn_jug_bowl", "furn_bottles", "furn_crockery"}, 1 + (clutter > 0.6f));
        bool big = R.area > 11.0f;
        if (center(k, big ? "furn_table_dining" : "furn_table_small", r + "table", 0.75f, &t)) {
          chairsAround(k, t, rnd(base, 2) < 0.6f ? "furn_chair_ladder" : "furn_chair_slat", 2 + (int)std::round(2 * density * rnd(base, 3) + (big ? 1 : 0)), big);
          onTop(t, {"furn_crockery", "furn_bottles", "furn_jug_bowl", "furn_lamp"}, (int)std::round(1 + 2 * clutter));
        }
        if (maybe(base + 4, density)) wall(k, "furn_shelf", r + "shelf", 0.8f, &w) && (onTop(w, {"furn_bottles", "furn_crockery", "furn_books"}, (int)std::round(1 + 2 * clutter)), true);
        if (maybe(base + 5, density * 0.7f)) wall(k, "furn_bench", r + "bench", -0.2f);
        if (maybe(base + 6, density * 0.6f)) wall(k, "furn_crate", r + "crate", 1.0f);
        break;
      }
      case ROOM_SITTING: {
        float yaw = e.x >= e.y ? 0.0f : 90.0f;
        rug(k, rnd(base, 7) < 0.6f ? "furn_rug_medallion" : "furn_rug_kilim", r + "rug", cc, yaw);
        wall(k, "furn_commode", r + "commode", -0.3f, &w);
        onTop(w, {"furn_lamp", "furn_books", "furn_jug_bowl"}, 1 + (clutter > 0.5f));
        if (maybe(base + 8, density)) wall(k, "furn_clock", r + "clock", 0.6f);
        if (center(k, "furn_side_table", r + "table", 0.55f, &t)) {
          chairsAround(k, t, "furn_chair_slat", 1 + (int)std::round(density), false);
          onTop(t, {"furn_books", "furn_lamp", "furn_crockery"}, 1);
          if (maybe(base + 9, clutter * 0.8f)) papers(k, t, 1);
        }
        if (maybe(base + 10, density)) wall(k, "furn_shelf", r + "shelf", 0.8f, &w) && (onTop(w, {"furn_books", "furn_crockery", "furn_bottles"}, (int)std::round(1 + 2 * clutter)), true);
        for (int p = 0; p < 1 + (density > 0.6f); ++p) wall(k, "furn_picture", r + "picture" + std::to_string(p), -0.6f, nullptr, sys.prop(A("furn_picture"))->mount);
        curtains(k);
        break;
      }
      case ROOM_BEDROOM: {
        if (wall(k, "furn_bed", r + "bed", -0.2f, &t)) {
          quat q = yawQuat(t.yaw);   // a rug beside the bed, not under it
          vec3 side = t.p + q * vec3(1.05f, 0, 0.1f);
          if (!rug(k, "furn_rug_kilim", r + "rug", vec2(side.x, side.z), wrapDeg(t.yaw + 90))) { vec3 s2 = t.p + q * vec3(-1.05f, 0, 0.1f); rug(k, "furn_rug_kilim", r + "rug", vec2(s2.x, s2.z), wrapDeg(t.yaw + 90)); }
        }
        if (R.area > 9.0f || maybe(base + 11, density)) wall(k, "furn_armoire", r + "armoire", 0.6f);
        if (wall(k, rnd(base, 12) < 0.5f ? "furn_commode" : "furn_chest", r + "commode", 0.2f, &w)) onTop(w, {"furn_jug_bowl", "furn_lamp", "furn_books"}, 1 + (clutter > 0.5f));
        if (maybe(base + 13, density)) wall(k, "furn_chair_ladder", r + "chair", 0.4f);
        if (maybe(base + 14, density * 0.8f)) wall(k, "furn_picture", r + "picture", -0.5f, nullptr, sys.prop(A("furn_picture"))->mount);
        curtains(k);
        break;
      }
      case ROOM_WORKSHOP: {
        wall(k, "furn_bench", r + "bench", -0.3f);
        if (wall(k, "furn_shelf", r + "shelf", 0.6f, &w)) onTop(w, {"furn_bottles", "furn_books", "furn_crate"}, (int)std::round(1 + 2 * clutter));
        for (int c = 0; c < 1 + (int)std::round(2 * density); ++c) wall(k, "furn_crate", r + "crate" + std::to_string(c), 1.0f, &w);
        wall(k, "furn_stool", r + "stool", 0.0f);
        break;
      }
      case ROOM_HALL: {
        if (maybe(base + 15, density * 0.8f)) wall(k, "furn_bench", r + "bench", -0.4f);
        if (maybe(base + 16, density * 0.6f)) wall(k, "furn_picture", r + "picture", -0.5f, nullptr, sys.prop(A("furn_picture"))->mount);
        break;
      }
      default: {   // utility / storage
        if (wall(k, "furn_shelf", r + "shelf", 0.8f, &w)) onTop(w, {"furn_bottles", "furn_crockery", "furn_crate"}, (int)std::round(1 + 2 * clutter));
        for (int c = 0; c < 1 + (int)std::round(2 * density); ++c) wall(k, "furn_crate", r + "crate" + std::to_string(c), 1.0f);
        if (maybe(base + 17, density * 0.6f)) wall(k, "furn_chest", r + "chest", 0.5f);
        if (maybe(base + 18, density * 0.5f)) wall(k, "furn_stool", r + "stool", 0.3f);
        break;
      }
    }
  }
};
}

int InteriorSys::unfurnish(App& app, uint64_t hostId) {
  Instance* h = app.doc.find(hostId); if (!h) return 0;
  size_t n0 = app.doc.instances.size();
  app.doc.instances.erase(std::remove_if(app.doc.instances.begin(), app.doc.instances.end(), [&](const Instance& i) {
    return i.extra.is_object() && i.extra.contains("furnish") && i.extra["furnish"].value("host", (uint64_t)0) == hostId; }), app.doc.instances.end());
  h = app.doc.find(hostId);
  if (h && h->extra.is_object() && h->extra.contains("interior")) { h->extra["interior"]["on"] = false; h->extra["interior"].erase("slots"); }
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
  float density = std::clamp(in.value("density", 0.7f), 0.0f, 1.0f), clutter = std::clamp(in.value("clutter", 0.5f), 0.0f, 1.0f);
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
  app.bump(*h);
  app.syncRuntime();   // built-in furniture leaves the collision before planning
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
  for (int k : order) P.room(k);
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
    n.extra["furnish"] = {{"host", hostId}, {"slot", it.slot}, {"loc", loc}, {"gen", loc}, {"hp", {hp.x, hp.y, hp.z, hy}}};
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
  logf("interior: furnished %s #%llu: %d pieces (%zu slots, %zu kept overrides, %zu removed) in %.1f ms, %d solid queries", ha->id.c_str(), (unsigned long long)hostId, made, P.out.size(), manual.size(), removed.size(), lastFurnishMs, P.calls);
  return made;
}

void InteriorSys::sync(App& app) {
  if (app.mode != Mode::Creative) return;
  bool changed = false;
  std::map<uint64_t, Instance*> hosts;
  for (auto& i : app.doc.instances) if (hideBuiltin(i)) hosts[i.id] = &i;
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
}

// ============================================================ editor
void InteriorSys::editorPanel(App& app, Instance& s) {
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
  float dens = in.value("density", 0.7f), clut = in.value("clutter", 0.5f);
  bool a2 = false, e2 = false;
  ImGui::SliderFloat("Furniture density", &dens, 0.0f, 1.0f, "%.2f"); a2 |= ImGui::IsItemActivated(); e2 |= ImGui::IsItemEdited();
  ImGui::SliderFloat("Clutter density", &clut, 0.0f, 1.0f, "%.2f"); a2 |= ImGui::IsItemActivated(); e2 |= ImGui::IsItemEdited();
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
