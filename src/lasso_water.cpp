// Lasso water fill (docs/lasso.md#water-fill): the spill height of a hollow and the outline of the water it holds.
#include "lasso_water.h"
#include "app.h"
#include "geom2d.h"
#include <cstring>
#include <queue>
#include <unordered_map>
#if __has_include("water.h")
#include "water.h"
namespace {
struct BodyMesh { uint64_t sig = 0; GLuint vao = 0, vbo = 0; int count = 0; bool live = false; };
std::unordered_map<uint64_t, BodyMesh> gMeshes;
}
void lassoWaterDrawItems(App& app) {
  for (auto& [id, m] : gMeshes) m.live = false;
  for (auto& w : app.doc.waterBodies) {
    uint64_t sig = 1469598103934665603ull;
    auto mixf = [&](float f) { uint32_t b; std::memcpy(&b, &f, 4); sig = (sig ^ b) * 1099511628211ull; };
    mixf(w.level); for (vec2 p : w.outline) { mixf(p.x); mixf(p.y); }
    BodyMesh& m = gMeshes[w.id];
    m.live = true;
    if (m.sig != sig || !m.vao) {   // flat, at the level, no flow (a pond); V1's vertex layout: pos.xyz, flow.xy
      if (m.vbo) glDeleteBuffers(1, &m.vbo);
      if (m.vao) glDeleteVertexArrays(1, &m.vao);
      std::vector<vec2> tri = geom2d::triangulate(w.outline);
      std::vector<float> v; v.reserve(tri.size() * 5);
      for (vec2 p : tri) v.insert(v.end(), {p.x, w.level, p.y, 0.0f, 0.0f});
      m.count = (int)tri.size();
      glCreateBuffers(1, &m.vbo); glNamedBufferData(m.vbo, (GLsizeiptr)(std::max<size_t>(1, v.size()) * sizeof(float)), v.data(), GL_STATIC_DRAW);
      glCreateVertexArrays(1, &m.vao);
      glVertexArrayVertexBuffer(m.vao, 0, m.vbo, 0, 5 * sizeof(float));
      glEnableVertexArrayAttrib(m.vao, 0); glVertexArrayAttribFormat(m.vao, 0, 3, GL_FLOAT, GL_FALSE, 0); glVertexArrayAttribBinding(m.vao, 0, 0);
      glEnableVertexArrayAttrib(m.vao, 1); glVertexArrayAttribFormat(m.vao, 1, 2, GL_FLOAT, GL_FALSE, 3 * sizeof(float)); glVertexArrayAttribBinding(m.vao, 1, 0);
      m.sig = sig;
    }
    if (m.count) app.R.water.push_back({m.vao, m.count, mat4(1), vec4(1.0f, 1.0f, 1.0f, 0.0f), vec4(0.0f)});   // kind 1 = pond
  }
  for (auto it = gMeshes.begin(); it != gMeshes.end();) {
    if (it->second.live) { ++it; continue; }
    if (it->second.vbo) glDeleteBuffers(1, &it->second.vbo);
    if (it->second.vao) glDeleteVertexArrays(1, &it->second.vao);
    it = gMeshes.erase(it);
  }
}
#else
void lassoWaterDrawItems(App&) {}   // before V1's water pass is in the build: lasso_ui.cpp draws a translucent sheet
#endif

float lassoWaterAt(const App& app, float x, float z) {
  float best = -1e9f;
  for (auto& w : app.doc.waterBodies) {
    if (w.level <= best) continue;
    vec2 lo, hi; geom2d::bounds(w.outline, lo, hi);
    if (x < lo.x || z < lo.y || x > hi.x || z > hi.y) continue;
    if (geom2d::inside(w.outline, vec2(x, z))) best = w.level;
  }
  return best;
}

namespace {

// boundary of a 4-connected cell blob without holes, traced along the cell edges (corners at cell-centre +- half),
// interior on the left; collinear corners dropped
std::vector<vec2> traceBlob(const std::vector<char>& m, int W, int H, vec2 org, float fs) {
  auto at = [&](int x, int z) { return x >= 0 && z >= 0 && x < W && z < H && m[(size_t)z * W + x]; };
  // start at the lowest-row, lowest-column cell: its bottom edge, walking +x
  int sx = -1, sz = -1;
  for (int z = 0; z < H && sx < 0; ++z) for (int x = 0; x < W; ++x) if (at(x, z)) { sx = x; sz = z; break; }
  std::vector<vec2> out;
  if (sx < 0) return out;
  // vertex lattice: vertex (i, j) is the corner at cell-centre (i - 0.5, j - 0.5); direction 0 +x, 1 +z, 2 -x, 3 -z
  const int dx[4] = {1, 0, -1, 0}, dz[4] = {0, 1, 0, -1};
  int vi = sx, vj = sz, dir = 0, i0 = vi, j0 = vj, d0 = dir;
  // walking along direction d from vertex v, the cell on the left is (v + left offset), the right one (v + right)
  auto leftCell = [&](int i, int j, int d, int& cx, int& cz) {   // cell left of the edge leaving (i,j) along d
    switch (d) { case 0: cx = i; cz = j; break; case 1: cx = i - 1; cz = j; break; case 2: cx = i - 1; cz = j - 1; break; default: cx = i; cz = j - 1; }
  };
  auto rightCell = [&](int i, int j, int d, int& cx, int& cz) {
    switch (d) { case 0: cx = i; cz = j - 1; break; case 1: cx = i; cz = j; break; case 2: cx = i - 1; cz = j; break; default: cx = i - 1; cz = j - 1; }
  };
  size_t guard = (size_t)4 * W * H + 16;
  do {
    out.push_back(org + fs * vec2(vi - 0.5f, vj - 0.5f));
    vi += dx[dir]; vj += dz[dir];
    // at the new vertex prefer a right turn (keeps the blob on the left), then straight, then left
    for (int t : {3, 0, 1, 2}) {
      int nd = (dir + t) & 3, cx, cz, rx, rz;
      leftCell(vi, vj, nd, cx, cz); rightCell(vi, vj, nd, rx, rz);
      if (at(cx, cz) && !at(rx, rz)) { dir = nd; break; }
    }
  } while ((vi != i0 || vj != j0 || dir != d0) && --guard);
  // drop collinear corners
  std::vector<vec2> c;
  for (size_t i = 0; i < out.size(); ++i) {
    vec2 a = out[(i + out.size() - 1) % out.size()], b = out[i], n = out[(i + 1) % out.size()];
    vec2 u = b - a, v = n - b;
    if (std::fabs(u.x * v.y - u.y * v.x) > 1e-6f) c.push_back(b);
  }
  return c;
}

}   // namespace

WaterFill lassoComputeFill(const App& app, const std::vector<vec2>& region, float margin) {
  WaterFill r;
  const Terrain& T = app.terrain;
  if (region.size() < 3) { r.why = "draw a region first"; return r; }
  float fs = T.fineSpacing();
  vec2 lo, hi; geom2d::bounds(region, lo, hi);
  lo -= vec2(margin); hi += vec2(margin);
  lo = glm::max(lo, T.org); hi = glm::min(hi, T.org + vec2(T.size, T.sizeZ));
  // window on the global fine lattice (org + k * fs), so the samples are the ones the tiles hold
  int i0 = (int)std::floor((lo.x - T.org.x) / fs), j0 = (int)std::floor((lo.y - T.org.y) / fs);
  int i1 = (int)std::ceil((hi.x - T.org.x) / fs), j1 = (int)std::ceil((hi.y - T.org.y) / fs);
  int W = i1 - i0 + 1, H = j1 - j0 + 1;
  if (W < 3 || H < 3) { r.why = "region is outside the terrain"; return r; }
  if ((size_t)W * H > (size_t)6 << 20) { r.why = "region is too large to fill (max about 1 km across)"; return r; }
  vec2 org = T.org + fs * vec2((float)i0, (float)j0);
  std::vector<float> h((size_t)W * H);
  for (int z = 0; z < H; ++z) for (int x = 0; x < W; ++x) h[(size_t)z * W + x] = T.height(org.x + x * fs, org.y + z * fs);
  // seed: the lowest sample inside the region
  size_t seed = SIZE_MAX; float sh = 1e30f;
  for (int z = 1; z < H - 1; ++z) for (int x = 1; x < W - 1; ++x) {
    size_t k = (size_t)z * W + x;
    if (h[k] < sh && geom2d::inside(region, org + fs * vec2((float)x, (float)z))) { sh = h[k]; seed = k; }
  }
  if (seed == SIZE_MAX) { r.why = "region is too small"; return r; }
  r.seed = org + fs * vec2((float)(seed % W), (float)(seed / W));
  if (sh < T.waterLevel + 0.02f) { r.why = "the hollow is already under the sea"; return r; }
  // priority flood from the seed: pop the lowest frontier sample; the running maximum is the water level the hollow
  // has reached. The first sample outside the region strictly below it lies past the rim: the water spills there.
  using QE = std::pair<float, size_t>;
  std::priority_queue<QE, std::vector<QE>, std::greater<QE>> q;
  std::vector<char> seen((size_t)W * H, 0);
  q.push({h[seed], seed}); seen[seed] = 1;
  float level = -1e30f; bool spilled = false;
  const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  while (!q.empty()) {
    auto [hv, k] = q.top(); q.pop();
    int x = (int)(k % W), z = (int)(k / W);
    // lower than the water has reached: past the rim when it is outside the region (lower pockets inside the region
    // are part of the hollow: its floor is never perfectly flat)
    if (hv < level - 0.001f && !geom2d::inside(region, org + fs * vec2((float)x, (float)z))) { spilled = true; r.spill = org + fs * vec2((float)x, (float)z); break; }
    level = std::max(level, hv);
    if (level < T.waterLevel) continue;   // (never: the seed is above the sea)
    if (x == 0 || z == 0 || x == W - 1 || z == H - 1) break;   // reached the search edge still climbing: open ground
    for (auto& d : nb) {
      int nx = x + d[0], nz = z + d[1];
      size_t n = (size_t)nz * W + nx;
      if (!seen[n]) { seen[n] = 1; q.push({h[n], n}); }
    }
  }
  if (!spilled) { r.why = "no closed hollow: the ground keeps rising to the edge of the search area"; return r; }
  if (level < T.waterLevel + 0.02f) { r.why = "the hollow drains into the sea"; return r; }
  if (level - sh < 0.05f) { r.why = "no hollow here (less than 5 cm deep)"; return r; }
  // the lake: samples below the level 4-connected to the seed
  std::vector<char> m((size_t)W * H, 0);
  std::vector<size_t> st{seed}; m[seed] = 1;
  while (!st.empty()) {
    size_t k = st.back(); st.pop_back();
    int x = (int)(k % W), z = (int)(k / W);
    for (auto& d : nb) {
      int nx = x + d[0], nz = z + d[1];
      if (nx < 0 || nz < 0 || nx >= W || nz >= H) continue;
      size_t n = (size_t)nz * W + nx;
      if (!m[n] && h[n] < level) { m[n] = 1; st.push_back(n); }
    }
  }
  int cells = 0; for (char c : m) cells += c;
  r.cells = cells;
  // grow one sample into dry ground (never into other low ground), so the plane meets the bank under the terrain
  std::vector<char> g = m;
  for (int z = 0; z < H; ++z) for (int x = 0; x < W; ++x) {
    size_t k = (size_t)z * W + x;
    if (m[k] || h[k] < level) continue;
    for (auto& d : nb) { int nx = x + d[0], nz = z + d[1]; if (nx >= 0 && nz >= 0 && nx < W && nz < H && m[(size_t)nz * W + nx]) { g[k] = 1; break; } }
  }
  // fill holes (islands): the background reached from the window border stays dry, everything else is water
  std::vector<char> outside((size_t)W * H, 0);
  st.clear();
  for (int x = 0; x < W; ++x) for (int z : {0, H - 1}) { size_t k = (size_t)z * W + x; if (!g[k] && !outside[k]) { outside[k] = 1; st.push_back(k); } }
  for (int z = 0; z < H; ++z) for (int x : {0, W - 1}) { size_t k = (size_t)z * W + x; if (!g[k] && !outside[k]) { outside[k] = 1; st.push_back(k); } }
  while (!st.empty()) {
    size_t k = st.back(); st.pop_back();
    int x = (int)(k % W), z = (int)(k / W);
    for (auto& d : nb) {
      int nx = x + d[0], nz = z + d[1];
      if (nx < 0 || nz < 0 || nx >= W || nz >= H) continue;
      size_t n = (size_t)nz * W + nx;
      if (!g[n] && !outside[n]) { outside[n] = 1; st.push_back(n); }
    }
  }
  for (size_t k = 0; k < g.size(); ++k) g[k] = outside[k] ? 0 : 1;
  std::vector<vec2> loop = traceBlob(g, W, H, org, fs);
  if (loop.size() < 3) { r.why = "the water outline could not be traced"; return r; }
  r.outline = geom2d::simplifyLoop(loop, 0.2f);
  if (geom2d::signedArea(r.outline) < 0) std::reverse(r.outline.begin(), r.outline.end());
  r.level = level;
  r.ok = true;
  return r;
}
