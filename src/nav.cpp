#include "nav.h"
#include "physics.h"
#include <thread>
#include <queue>
#include <chrono>
#include <algorithm>
#include <cmath>

const int Nav::kDX[8] = {1, 0, -1, 0, 1, -1, -1, 1};
const int Nav::kDZ[8] = {0, 1, 0, -1, 1, 1, -1, -1};

static bool isTerrainUD(uint64_t u) { return u == 0 || (u >> 60) == 0xF; }
static bool isLeafUD(uint64_t u) { if (isTerrainUD(u)) return false; uint64_t l = u & 0xFFFF; return l != 0 && l != 0xFFFF; }

void Nav::build(vec3 lo, vec3 hi, int threads) {
  auto t0 = std::chrono::steady_clock::now();
  origin = vec2(std::floor(lo.x / kCell) * kCell, std::floor(lo.z / kCell) * kCell);
  nx = std::max(1, (int)std::ceil((hi.x - origin.x) / kCell));
  nz = std::max(1, (int)std::ceil((hi.z - origin.y) / kCell));
  slots.assign((size_t)nx * nz * kLayers, Slot{});
  for (auto& s : slots) std::fill(std::begin(s.link), std::end(s.link), (int8_t)-1);
  if (threads <= 0) threads = std::max(1, (int)std::thread::hardware_concurrency() - 2);
  auto run = [&](auto fn) {
    std::vector<std::thread> T;
    for (int t = 0; t < threads; ++t) T.emplace_back([&, t] { for (int cz = t; cz < nz; cz += threads) for (int cx = 0; cx < nx; ++cx) fn(cx, cz); });
    for (auto& x : T) x.join();
  };
  run([&](int cx, int cz) { sampleColumn(cx, cz); });
  // structure proximity: columns within 1 cell of anything built need ray-checked links
  std::vector<uint8_t> st(nx * nz, 0);
  for (int cz = 0; cz < nz; ++cz) for (int cx = 0; cx < nx; ++cx) for (int l = 0; l < kLayers; ++l) if (slots[((size_t)cz * nx + cx) * kLayers + l].flags & N_STRUCT) st[cz * nx + cx] = 1;
  for (int cz = 0; cz < nz; ++cz) for (int cx = 0; cx < nx; ++cx) {
    if (!st[cz * nx + cx]) continue;
    for (int d = 0; d < 8; ++d) { int x = cx + kDX[d], z = cz + kDZ[d]; if (x < 0 || z < 0 || x >= nx || z >= nz) continue; for (int l = 0; l < kLayers; ++l) slots[((size_t)z * nx + x) * kLayers + l].flags |= N_WALL; }
  }
  run([&](int cx, int cz) { linkColumn(cx, cz); });
  usedNodes = links = doorLinks = 0;
  for (auto& s : slots) if (s.flags & N_USED) { ++usedNodes; for (int d = 0; d < 8; ++d) if (s.link[d] >= 0) { ++links; if ((s.doorMask >> d) & 1) ++doorLinks; } }
  buildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  logf("nav: %d x %d columns (%.0f x %.0f m), %d floors, %d links (%d door), %.0f ms", nx, nz, nx * kCell, nz * kCell, usedNodes, links, doorLinks, buildMs);
}

void Nav::sampleColumn(int cx, int cz) {
  vec3 c(origin.x + (cx + 0.5f) * kCell, 0, origin.y + (cz + 0.5f) * kCell);
  Physics::Hit h[24];
  int n = Physics::castRayAll(vec3(c.x, 80.0f, c.z), vec3(0, -1, 0), 200.0f, h, 24, true);
  Slot* col = &slots[((size_t)cz * nx + cx) * kLayers];
  int layer = 0;
  bool structured = false;
  for (int k = 0; k < n; ++k) if (!isTerrainUD(h[k].ud)) structured = true;
  float ground = n > 0 ? h[n - 1].pos.y : 0.0f;
  // bottom-up so layer 0 is the lowest floor
  for (int k = n - 1; k >= 0 && layer < kLayers; --k) {
    if (h[k].normal.y < 0.70f) continue;                       // too steep to stand on (wall faces, roofs)
    vec3 f = h[k].pos;
    float head = 6.0f;
    Physics::Hit up;
    if (Physics::castRayEx(f + vec3(0, 0.05f, 0), vec3(0, 1, 0), 6.0f, up)) head = up.t + 0.05f;
    else if (f.y > ground + 2.5f) continue;                    // open sky high above the ground: a roof or wall top (no ladders)
    // a body whose top we stand on but that we are also inside of (overlapping parts) shows up as zero headroom
    if (head < 1.25f) continue;
    if (layer > 0 && f.y - col[layer - 1].y < 1.3f) {            // same floor reported twice (stacked parts): keep the higher
      if (f.y > col[layer - 1].y) { col[layer - 1].y = f.y; col[layer - 1].head = (uint16_t)std::min(head * 100.0f, 60000.0f); }
      continue;
    }
    Slot& s = col[layer++];
    s.y = f.y; s.head = (uint16_t)std::min(head * 100.0f, 60000.0f);
    s.flags = N_USED | (head < 1.85f ? N_CROUCH : 0) | (structured ? N_STRUCT : 0);
  }
}

bool Nav::walkLine(vec3 a, vec3 b, bool allowDoors) {
  for (float hgt : {0.45f, 1.1f}) {
    vec3 p = a + vec3(0, hgt, 0), q = b + vec3(0, hgt, 0);
    vec3 d = q - p; float L = glm::length(d);
    if (L < 1e-4f) continue;
    Physics::Hit h[4];
    int n = Physics::castRayAll(p, d, L, h, 4, false);
    for (int k = 0; k < n; ++k) { if (allowDoors && isLeafUD(h[k].ud)) continue; return false; }
  }
  return true;
}

void Nav::linkColumn(int cx, int cz) {
  for (int l = 0; l < kLayers; ++l) {
    Slot& s = slots[((size_t)cz * nx + cx) * kLayers + l];
    if (!(s.flags & N_USED)) continue;
    std::fill(std::begin(s.link), std::end(s.link), (int8_t)-1); s.doorMask = 0;
    vec3 a(origin.x + (cx + 0.5f) * kCell, s.y, origin.y + (cz + 0.5f) * kCell);
    for (int d = 0; d < 8; ++d) {
      int x = cx + kDX[d], z = cz + kDZ[d];
      if (x < 0 || z < 0 || x >= nx || z >= nz) continue;
      int best = -1; float bdy = 1e9f;
      for (int m = 0; m < kLayers; ++m) {
        const Slot& t = slots[((size_t)z * nx + x) * kLayers + m];
        if (!(t.flags & N_USED)) continue;
        float dy = t.y - s.y;
        if (dy > 0.45f || dy < -0.6f) continue;
        if (std::fabs(dy) < bdy) { bdy = std::fabs(dy); best = m; }
      }
      if (best < 0) continue;
      const Slot& t = slots[((size_t)z * nx + x) * kLayers + best];
      vec3 b(origin.x + (x + 0.5f) * kCell, t.y, origin.y + (z + 0.5f) * kCell);
      bool near = (s.flags & (N_STRUCT | N_WALL)) || (t.flags & (N_STRUCT | N_WALL));
      if (near) {
        if (!walkLine(a, b, true)) continue;                     // solid wall / furniture between the cells
        if (!walkLine(a, b, false)) s.doorMask |= (uint8_t)(1 << d);   // only a door / window leaf: openable
      }
      s.link[d] = (int8_t)best;
    }
  }
}

void Nav::rebuildArea(vec3 lo, vec3 hi) {
  if (!valid()) return;
  int x0 = std::max(0, (int)std::floor((lo.x - origin.x) / kCell) - 1), x1 = std::min(nx - 1, (int)std::floor((hi.x - origin.x) / kCell) + 1);
  int z0 = std::max(0, (int)std::floor((lo.z - origin.y) / kCell) - 1), z1 = std::min(nz - 1, (int)std::floor((hi.z - origin.y) / kCell) + 1);
  if (x0 > x1 || z0 > z1) return;
  for (int z = z0; z <= z1; ++z) for (int x = x0; x <= x1; ++x) {
    Slot* col = &slots[((size_t)z * nx + x) * kLayers];
    for (int l = 0; l < kLayers; ++l) { col[l] = Slot{}; std::fill(std::begin(col[l].link), std::end(col[l].link), (int8_t)-1); }
    sampleColumn(x, z);
    for (int l = 0; l < kLayers; ++l) if (col[l].flags & N_USED) col[l].flags |= N_WALL;
  }
  for (int z = std::max(0, z0 - 1); z <= std::min(nz - 1, z1 + 1); ++z) for (int x = std::max(0, x0 - 1); x <= std::min(nx - 1, x1 + 1); ++x) linkColumn(x, z);
  ++rebuilds;
}

int Nav::neighbour(int s, int d) const {
  const Slot& a = slots[s];
  if (a.link[d] < 0) return -1;
  int c = s / kLayers, cx = c % nx + kDX[d], cz = c / nx + kDZ[d];
  return (cz * nx + cx) * kLayers + a.link[d];
}

int Nav::nearest(vec3 p, float maxDist) const {
  if (!valid()) return -1;
  int cx = (int)std::floor((p.x - origin.x) / kCell), cz = (int)std::floor((p.z - origin.y) / kCell);
  int R = (int)std::ceil(maxDist / kCell);
  int best = -1; float bd = 1e9f;
  for (int dz = -R; dz <= R; ++dz) for (int dx = -R; dx <= R; ++dx) {
    int x = cx + dx, z = cz + dz;
    if (x < 0 || z < 0 || x >= nx || z >= nz) continue;
    for (int l = 0; l < kLayers; ++l) {
      int s = (z * nx + x) * kLayers + l;
      if (!(slots[s].flags & N_USED)) continue;
      bool linked = false; for (int d = 0; d < 8; ++d) if (slots[s].link[d] >= 0) linked = true;
      if (!linked) continue;
      vec3 q = pos(s);
      float dy = p.y - q.y;
      if (dy < -0.6f || dy > 2.0f) continue;                  // a floor above the feet, or far below
      float d = glm::length(vec2(q.x - p.x, q.z - p.z)) + std::fabs(dy) * 2.0f;
      if (d < bd) { bd = d; best = s; }
    }
  }
  return bd <= maxDist + 1.0f ? best : -1;
}

bool Nav::path(int a, int b, std::vector<int>& out, int maxExpand, float* costOut) const {
  out.clear();
  if (!used(a) || !used(b)) return false;
  if (a == b) { out.push_back(a); return true; }
  size_t N = slots.size();
  if (gScore.size() != N) { gScore.assign(N, 0); stamp.assign(N, 0); parent.assign(N, -1); curStamp = 0; }
  if (++curStamp == 0) { std::fill(stamp.begin(), stamp.end(), 0); curStamp = 1; }
  vec3 goal = pos(b);
  using QE = std::pair<float, int>;
  std::priority_queue<QE, std::vector<QE>, std::greater<QE>> open;
  gScore[a] = 0; stamp[a] = curStamp; parent[a] = -1;
  open.push({glm::length(pos(a) - goal), a});
  int expanded = 0;
  while (!open.empty()) {
    auto [f, s] = open.top(); open.pop();
    if (s == b) break;
    if (f - glm::length(pos(s) - goal) > gScore[s] + 1e-3f) continue;   // stale entry
    if (++expanded > maxExpand) return false;
    vec3 ps = pos(s);
    for (int d = 0; d < 8; ++d) {
      int t = neighbour(s, d);
      if (t < 0) continue;
      if (d >= 4) {   // diagonal: no corner cutting
        int dirX = kDX[d] > 0 ? 0 : 2, dirZ = kDZ[d] > 0 ? 1 : 3;
        if (slots[s].link[dirX] < 0 || slots[s].link[dirZ] < 0 || doorLink(s, dirX) || doorLink(s, dirZ)) continue;
      }
      vec3 pt = pos(t);
      float c = glm::length(pt - ps);
      if (slots[t].flags & N_CROUCH) c *= 1.6f;
      if (slots[t].flags & N_WALL) c *= 1.08f;                 // hug walls slightly less
      if (doorLink(s, d)) c += 2.0f;
      float g = gScore[s] + c;
      if (stamp[t] != curStamp || g < gScore[t]) {
        stamp[t] = curStamp; gScore[t] = g; parent[t] = s;
        open.push({g + glm::length(pt - goal), t});
      }
    }
  }
  if (stamp[b] != curStamp) return false;
  for (int s = b; s >= 0; s = parent[s]) { out.push_back(s); if (s == a) break; }
  std::reverse(out.begin(), out.end());
  if (costOut) *costOut = gScore[b];
  return !out.empty() && out.front() == a;
}
