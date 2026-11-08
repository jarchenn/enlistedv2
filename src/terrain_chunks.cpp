// Chunked, streamed base terrain (SPEC 9.1): LOD meshes around the camera, one Jolt heightfield per chunk, the map
// size rules and the New Map memory estimate. See terrain_chunks.h.
#include "terrain.h"
#include "physics.h"
#include "frustum.h"
#include <algorithm>
#include <cfloat>
#include <chrono>

// ------------------------------------------------------------------ settings (content/settings/editor.json)
const json& editorSettings();
const json& editorSettings() {
  static json j = [] {
    try { return json::parse(readTextFile(contentDir() / "settings" / "editor.json")); }
    catch (std::exception& e) { logf("editor.json: %s (defaults used)", e.what()); return json::object(); }
  }();
  return j;
}

vec2 MapSizeRules::clampSize(vec2 wd) const {
  auto one = [&](float v) { v = std::round(v / step) * step; return std::clamp(v, minSide, maxSide); };
  return vec2(one(wd.x), one(wd.y));
}
const MapSizeRules& mapSizeRules() {
  static MapSizeRules r = [] {
    MapSizeRules m;
    const json& j = editorSettings();
    if (j.contains("mapSize")) {
      const json& s = j["mapSize"];
      m.minSide = s.value("min", m.minSide); m.maxSide = s.value("max", m.maxSide); m.step = s.value("step", m.step);
      m.margin = s.value("margin", m.margin); m.comfortableKm2 = s.value("comfortableKm2", m.comfortableKm2);
      if (s.contains("presets")) { m.presets.clear(); for (auto& p : s["presets"]) m.presets.push_back(vec2(p[0].get<float>(), p[1].get<float>())); }
    }
    return m;
  }();
  return r;
}
static TerrainStreamConfig loadStreamConfig() {
  TerrainStreamConfig c;
  const json& j = editorSettings();
  if (!j.contains("terrainStream")) return c;
  const json& s = j["terrainStream"];
  if (s.contains("lodDist")) for (int k = 0; k < 4 && k < (int)s["lodDist"].size(); ++k) c.lodDist[k] = s["lodDist"][k].get<float>();
  c.maxDist = s.value("maxDist", c.maxDist); c.buildBudgetMs = s.value("buildBudgetMs", c.buildBudgetMs);
  c.minBuildsPerFrame = s.value("minBuildsPerFrame", c.minBuildsPerFrame); c.hysteresis = s.value("hysteresis", c.hysteresis);
  c.skirtDepth = s.value("skirtDepth", c.skirtDepth);
  return c;
}

// per LOD: vertices per chunk edge, grid + skirt vertex count, index count
static int lodSide(int lod) { return Terrain::kChunkCells / TerrainStream::kStep[lod] + 1; }
static int lodVerts(int lod) { int m = lodSide(lod); return m * m + 4 * m; }
static int lodIndices(int lod) { int m = lodSide(lod); return (m - 1) * (m - 1) * 6 + 4 * (m - 1) * 12; }
static constexpr int kVertBytes = 24;   // position + normal

size_t terrainPhysicsBytesEstimate(int chunks) { return (size_t)chunks * 3200; }   // Jolt 21x21 heightfield (8-bit samples, range blocks) + body

TerrainEstimate estimateTerrain(float width, float depth, float margin) {
  const MapSizeRules& r = mapSizeRules();
  vec2 wd = r.clampSize(vec2(width, depth));
  if (margin < 0) margin = r.margin;
  auto cellsFor = [&](float ext) { int c = (int)std::ceil(ext / Terrain::kCell - 1e-3f); return std::max(Terrain::kTileCells * 2, (c + Terrain::kTileCells - 1) / Terrain::kTileCells * Terrain::kTileCells); };
  TerrainEstimate e;
  int cx = cellsFor(wd.x + 2 * margin), cz = cellsFor(wd.y + 2 * margin);
  e.verticesX = cx + 1; e.verticesZ = cz + 1;
  int chx = (cx - 1) / Terrain::kChunkCells + 1, chz = (cz - 1) / Terrain::kChunkCells + 1;
  e.chunks = chx * chz;
  e.tiles = (cx / Terrain::kTileCells) * (cz / Terrain::kTileCells);
  double sx = cx * Terrain::kCell, sz = cz * Terrain::kCell;
  double texel = 800.0 / Terrain::kSurfRes;
  if ((sx / texel) * (sz / texel) > (double)Terrain::kSurfMaxTexels) texel = std::sqrt(sx * sz / (double)Terrain::kSurfMaxTexels);
  double surfTexels = std::ceil(sx / texel) * std::ceil(sz / texel);
  e.surfaceMB = surfTexels * 4 * 2 / 1048576.0;   // CPU copy + GPU texture
  e.cpuMB = ((double)e.verticesX * e.verticesZ * 4 + (double)e.chunks * 64 + (double)e.tiles) / 1048576.0;
  e.physicsMB = terrainPhysicsBytesEstimate(e.chunks) / 1048576.0;
  // streamed meshes, worst case: every chunk within each LOD ring loaded (rings clipped to the map)
  TerrainStreamConfig c = loadStreamConfig();
  double chunkArea = std::pow(Terrain::kCell * Terrain::kChunkCells, 2), left = e.chunks, prevR = 0, gpu = 0;
  for (int l = 0; l < TerrainStream::kLods && left > 0; ++l) {
    double R = l < 4 ? c.lodDist[l] : c.maxDist;
    double n = std::min(left, 3.14159 * (R * R - prevR * prevR) / chunkArea + 2 * 3.14159 * R / std::sqrt(chunkArea));
    gpu += n * (lodVerts(l) * kVertBytes); left -= n; prevR = R;
  }
  e.gpuMB = gpu / 1048576.0 + (double)e.tiles / 1048576.0;   // + tile mask texture
  e.totalMB = e.cpuMB + e.physicsMB + e.gpuMB + e.surfaceMB;
  e.comfortable = wd.x * wd.y / 1e6 <= r.comfortableKm2 + 1e-6;
  return e;
}

// ------------------------------------------------------------------ render streaming
void TerrainStream::release() {
  for (auto& p : pools) {
    if (p.vao) glDeleteVertexArrays(1, &p.vao);
    if (p.vbo) glDeleteBuffers(1, &p.vbo);
    if (p.ebo) glDeleteBuffers(1, &p.ebo);
    p = Pool{};
  }
  chunks.clear();
  stats = Stats{};
  generation = ~0u;
}

void TerrainStream::reset(const Terrain& t) {
  release();
  cfg = loadStreamConfig();
  generation = t.generation;
  cx = t.chunksX(); cz = t.chunksZ();
  chunks.assign((size_t)cx * cz, Chunk{});
  for (int l = 0; l < kLods; ++l) { pools[l].vertsPerSlot = lodVerts(l); pools[l].indexCount = lodIndices(l); }
}

void TerrainStream::chunkBox(const Terrain& t, int idx, vec3& lo, vec3& hi) const {
  const Chunk& c = chunks[idx];
  int gx0 = (idx % cx) * Terrain::kChunkCells, gz0 = (idx / cx) * Terrain::kChunkCells;
  if (!c.bounds) {   // full-resolution height range (mutable cache)
    Chunk& m = const_cast<Chunk&>(c);
    m.hmin = 1e30f; m.hmax = -1e30f;
    for (int j = 0; j <= Terrain::kChunkCells; ++j)
      for (int i = 0; i <= Terrain::kChunkCells; ++i) {
        int gx = std::min(gx0 + i, t.nx - 1), gz = std::min(gz0 + j, t.nz - 1);
        float v = t.h[(size_t)gz * t.nx + gx]; m.hmin = std::min(m.hmin, v); m.hmax = std::max(m.hmax, v);
      }
    m.hmin -= 2.0f;   // craters / cuts reach below the base (tiles are drawn separately, but keep a margin)
    m.bounds = true;
  }
  float x0 = t.org.x + gx0 * Terrain::kCell, z0 = t.org.y + gz0 * Terrain::kCell;
  float x1 = t.org.x + std::min(gx0 + Terrain::kChunkCells, t.nx - 1) * Terrain::kCell, z1 = t.org.y + std::min(gz0 + Terrain::kChunkCells, t.nz - 1) * Terrain::kCell;
  lo = vec3(x0, c.hmin, z0); hi = vec3(x1, c.hmax, z1);
}

float TerrainStream::chunkDistance(const Terrain& t, int idx, vec3 cam) const {
  vec3 lo, hi; chunkBox(t, idx, lo, hi);
  vec3 d = glm::max(glm::max(lo - cam, cam - hi), vec3(0));
  return glm::length(d);
}

int TerrainStream::desiredLod(const Terrain& t, int idx, vec3 cam, int cur) const {
  float d = chunkDistance(t, idx, cam);
  float hy = cfg.hysteresis;
  if (d > cfg.maxDist * (cur >= 0 ? 1 + hy : 1)) return -1;
  int b = 4;
  for (int l = 0; l < 4; ++l) if (d < cfg.lodDist[l]) { b = l; break; }
  if (cur < 0 || b == cur) return b;
  if (b > cur) return d > cfg.lodDist[cur] * (1 + hy) ? b : cur;   // coarser only once clearly past the boundary
  return d < cfg.lodDist[b] * (1 - hy) ? b : cur;                  // finer only once clearly inside
}

void TerrainStream::ensurePool(int lod, int need) {
  Pool& p = pools[lod];
  if (!p.vao) {
    // shared index buffer: grid (a c d / a d b, the a-d diagonal of baseHeight and the heightfield) + skirts (both windings)
    int m = lodSide(lod);
    std::vector<uint32_t> idx; idx.reserve(p.indexCount);
    for (int j = 0; j + 1 < m; ++j)
      for (int i = 0; i + 1 < m; ++i) {
        uint32_t a = j * m + i, b = a + 1, c = a + m, d = c + 1;
        idx.insert(idx.end(), {a, c, d, a, d, b});
      }
    uint32_t sk = m * m;
    auto edge = [&](int e, int k) -> uint32_t {   // grid vertex k along edge e (skirt vertex = sk + e*m + k)
      switch (e) { case 0: return k; case 1: return k * m + (m - 1); case 2: return (m - 1) * m + k; default: return k * m; }
    };
    for (int e = 0; e < 4; ++e)
      for (int k = 0; k + 1 < m; ++k) {
        uint32_t a = edge(e, k), b = edge(e, k + 1), c = sk + e * m + k, d = sk + e * m + k + 1;
        idx.insert(idx.end(), {a, c, d, a, d, b, a, d, c, a, b, d});
      }
    glCreateBuffers(1, &p.ebo);
    glNamedBufferData(p.ebo, idx.size() * 4, idx.data(), GL_STATIC_DRAW);
    glCreateVertexArrays(1, &p.vao);
    glVertexArrayElementBuffer(p.vao, p.ebo);
    for (int k = 0; k < 2; ++k) {
      glEnableVertexArrayAttrib(p.vao, k);
      glVertexArrayAttribFormat(p.vao, k, 3, GL_FLOAT, GL_FALSE, k * 12);
      glVertexArrayAttribBinding(p.vao, k, 0);
    }
  }
  if (need <= p.capacity) return;
  int cap = std::max({need, p.capacity * 2, 16});
  GLuint nb = 0; glCreateBuffers(1, &nb);
  glNamedBufferData(nb, (GLsizeiptr)cap * p.vertsPerSlot * kVertBytes, nullptr, GL_DYNAMIC_DRAW);
  if (p.vbo) { glCopyNamedBufferSubData(p.vbo, nb, 0, 0, (GLsizeiptr)p.capacity * p.vertsPerSlot * kVertBytes); glDeleteBuffers(1, &p.vbo); }
  for (int s = cap - 1; s >= p.capacity; --s) p.freeSlots.push_back(s);
  p.vbo = nb; p.capacity = cap;
  glVertexArrayVertexBuffer(p.vao, 0, p.vbo, 0, kVertBytes);
}

void TerrainStream::unload(int idx) {
  Chunk& c = chunks[idx];
  if (c.lod < 0) return;
  pools[c.lod].freeSlots.push_back(c.slot); --pools[c.lod].used; --stats.loaded[c.lod];
  c.lod = -1; c.slot = -1;
}

void TerrainStream::build(const Terrain& t, int idx, int lod) {
  Pool& p = pools[lod];
  if (p.freeSlots.empty()) ensurePool(lod, p.capacity + 1);
  else ensurePool(lod, 0);
  unload(idx);
  int slot = p.freeSlots.back(); p.freeSlots.pop_back(); ++p.used; ++stats.loaded[lod];
  Chunk& c = chunks[idx];
  c.lod = lod; c.slot = slot; c.rev = t.chunkRev.empty() ? 0 : t.chunkRev[idx];
  const int m = lodSide(lod), s = kStep[lod];
  int gx0 = (idx % cx) * Terrain::kChunkCells, gz0 = (idx / cx) * Terrain::kChunkCells;
  std::vector<float> v((size_t)p.vertsPerSlot * 6);
  auto H = [&](int gx, int gz) { gx = std::clamp(gx, 0, t.nx - 1); gz = std::clamp(gz, 0, t.nz - 1); return t.h[(size_t)gz * t.nx + gx]; };
  const float e = Terrain::kCell;
  for (int j = 0; j < m; ++j)
    for (int i = 0; i < m; ++i) {
      int gx = std::min(gx0 + i * s, t.nx - 1), gz = std::min(gz0 + j * s, t.nz - 1);
      vec3 n = glm::normalize(vec3(H(gx - 1, gz) - H(gx + 1, gz), 2 * e, H(gx, gz - 1) - H(gx, gz + 1)));   // = Terrain::baseNormal
      float* o = &v[((size_t)j * m + i) * 6];
      o[0] = t.org.x + gx * e; o[1] = H(gx, gz); o[2] = t.org.y + gz * e; o[3] = n.x; o[4] = n.y; o[5] = n.z;
    }
  float drop = cfg.skirtDepth * (1.0f + 0.25f * s);
  for (int ed = 0; ed < 4; ++ed)
    for (int k = 0; k < m; ++k) {
      int src = ed == 0 ? k : ed == 1 ? k * m + (m - 1) : ed == 2 ? (m - 1) * m + k : k * m;
      float* o = &v[((size_t)m * m + ed * m + k) * 6];
      const float* q = &v[(size_t)src * 6];
      for (int c6 = 0; c6 < 6; ++c6) o[c6] = q[c6];
      o[1] -= drop;
    }
  glNamedBufferSubData(p.vbo, (GLintptr)slot * p.vertsPerSlot * kVertBytes, (GLsizeiptr)p.vertsPerSlot * kVertBytes, v.data());
}

// plane culling: src/frustum.h (shared with the renderer's passes)

void TerrainStream::update(const Terrain& t, vec3 cam, const mat4& vp, float budgetMs) {
  if (t.h.empty()) return;
  if (generation != t.generation || (int)chunks.size() != t.chunksX() * t.chunksZ()) reset(t);
  auto t0 = std::chrono::high_resolution_clock::now();
  Frustum fr = Frustum::fromVP(vp);
  struct Todo { int idx, lod; float key; };
  std::vector<Todo> todo;
  stats.visibleUnloaded = 0;
  for (int i = 0; i < (int)chunks.size(); ++i) {
    Chunk& c = chunks[i];
    int want = desiredLod(t, i, cam, c.lod);
    if (want < 0) { unload(i); continue; }
    bool stale = !t.chunkRev.empty() && c.lod >= 0 && c.rev != t.chunkRev[i];
    if (stale) { c.bounds = false; }
    if (want == c.lod && !stale) continue;
    vec3 lo, hi; chunkBox(t, i, lo, hi);
    bool vis = fr.visible(lo, hi);
    if (vis && c.lod < 0) ++stats.visibleUnloaded;
    float d = chunkDistance(t, i, cam);
    // missing visible chunks first, then refinements (nearest first), then the rest
    float key = (c.lod < 0 ? 0.0f : 1e6f) + (vis ? 0.0f : 2e6f) + d;
    todo.push_back({i, want, key});
  }
  std::sort(todo.begin(), todo.end(), [](const Todo& a, const Todo& b) { return a.key < b.key; });
  bool unlimited = budgetMs < 0 || loadedCount() == 0;
  int built = 0;
  for (auto& w : todo) {
    if (!unlimited && built >= cfg.minBuildsPerFrame) {
      float ms = std::chrono::duration<float, std::milli>(std::chrono::high_resolution_clock::now() - t0).count();
      if (ms > budgetMs) break;
    }
    build(t, w.idx, w.lod); ++built;
  }
  stats.builtLastFrame = built;
  stats.buildMs = std::chrono::duration<float, std::milli>(std::chrono::high_resolution_clock::now() - t0).count();
  size_t bytes = 0;
  for (auto& p : pools) bytes += (size_t)p.capacity * p.vertsPerSlot * kVertBytes + (p.ebo ? (size_t)p.indexCount * 4 : 0);
  stats.gpuBytes = bytes;
}

void TerrainStream::drawBase(const Terrain& t, vec3 cam, const mat4& vp) {
  (void)cam;
  Frustum fr = Frustum::fromVP(vp);
  std::vector<GLsizei> counts[kLods]; std::vector<const void*> offs[kLods]; std::vector<GLint> base[kLods];
  int drawn = 0, culled = 0; int64_t tris = 0;
  for (int i = 0; i < (int)chunks.size(); ++i) {
    const Chunk& c = chunks[i];
    if (c.lod < 0) continue;
    vec3 lo, hi; chunkBox(t, i, lo, hi);
    lo.y -= cfg.skirtDepth * 6;
    if (!fr.visible(lo, hi)) { ++culled; continue; }
    counts[c.lod].push_back(pools[c.lod].indexCount); offs[c.lod].push_back(nullptr); base[c.lod].push_back(c.slot * pools[c.lod].vertsPerSlot);
    ++drawn; tris += pools[c.lod].indexCount / 3;
  }
  int calls = 0;
  for (int l = 0; l < kLods; ++l) {
    if (counts[l].empty()) continue;
    glBindVertexArray(pools[l].vao);
    glMultiDrawElementsBaseVertex(GL_TRIANGLES, counts[l].data(), GL_UNSIGNED_INT, offs[l].data(), (GLsizei)counts[l].size(), base[l].data());
    ++calls;
  }
  stats.drawnLastFrame = drawn; stats.culledLastFrame = culled; stats.drawCalls = calls; stats.tris = tris;
}

// ------------------------------------------------------------------ physics
static uint64_t tilePatchUD(int key) { return (0xFull << 60) | (uint64_t)key; }

// One Jolt heightfield per chunk. Jolt removes the *triangles* touching a hole vertex, which leaves one triangle at two
// corners of every tile (its other vertices all lie on tile borders), so holes cannot cut a tile out exactly. A chunk
// with active deformation tiles is therefore split: one small heightfield per undamaged tile (the damaged tiles have
// their own high-resolution patches). Untouched chunks stay a single body.
void terrainPhysicsRebuildChunk(Terrain& t, int cx, int cz) {
  int idx = cz * t.chunksX() + cx;
  if (idx < 0 || idx >= (int)t.chunkBodies.size()) return;
  uint32_t& body = t.chunkBodies[idx];
  if (body != 0xffffffffu) { Physics::remove(body); body = 0xffffffffu; }
  auto sp = t.chunkSplitBodies.find(idx);
  if (sp != t.chunkSplitBodies.end()) { for (uint32_t b : sp->second) Physics::remove(b); t.chunkSplitBodies.erase(sp); }
  const int K = Terrain::kChunkCells / Terrain::kTileCells, gx0 = cx * Terrain::kChunkCells, gz0 = cz * Terrain::kChunkCells;
  const int tx0 = cx * K, tz0 = cz * K, tx1 = std::min(t.tilesX(), tx0 + K), tz1 = std::min(t.tilesZ(), tz0 + K);
  bool damaged = false;
  for (int tz = tz0; tz < tz1 && !damaged; ++tz) for (int tx = tx0; tx < tx1; ++tx) if (t.tileAt(tx, tz)) { damaged = true; break; }
  auto H = [&](int gx, int gz) { return gx < t.nx && gz < t.nz ? t.h[(size_t)gz * t.nx + gx] : FLT_MAX; };   // FLT_MAX = Jolt cNoCollisionValue
  if (!damaged) {
    const int m = Terrain::kChunkCells + 1;
    std::vector<float> hs((size_t)m * m);
    for (int j = 0; j < m; ++j) for (int i = 0; i < m; ++i) hs[(size_t)j * m + i] = H(gx0 + i, gz0 + j);
    body = Physics::addHeightPatch(hs, m, vec3(t.org.x + gx0 * Terrain::kCell, 0, t.org.y + gz0 * Terrain::kCell), Terrain::kCell, 0);
    return;
  }
  auto& list = t.chunkSplitBodies[idx];
  const int m = Terrain::kTileCells + 1;   // 6 samples: a multiple of the 2-sample block size
  std::vector<float> hs((size_t)m * m);
  for (int tz = tz0; tz < tz1; ++tz)
    for (int tx = tx0; tx < tx1; ++tx) {
      if (t.tileAt(tx, tz)) continue;
      for (int j = 0; j < m; ++j) for (int i = 0; i < m; ++i) hs[(size_t)j * m + i] = H(tx * Terrain::kTileCells + i, tz * Terrain::kTileCells + j);
      uint32_t b = Physics::addHeightPatch(hs, m, vec3(t.org.x + tx * Terrain::kTileCells * Terrain::kCell, 0, t.org.y + tz * Terrain::kTileCells * Terrain::kCell), Terrain::kCell, 0);
      if (b != 0xffffffffu) list.push_back(b);
    }
}

void terrainPhysicsBuild(Terrain& t) {
  for (uint32_t b : t.stream.deadBodies) Physics::remove(b);
  t.stream.deadBodies.clear();
  for (uint32_t b : t.chunkBodies) if (b != 0xffffffffu) Physics::remove(b);
  for (auto& [k, v] : t.chunkSplitBodies) for (uint32_t b : v) Physics::remove(b);
  t.chunkSplitBodies.clear();
  t.chunkBodies.assign((size_t)t.chunksX() * t.chunksZ(), 0xffffffffu);
  t.stream.physRev = t.chunkRev;
  t.physDirty.clear();
  for (auto& [k, tl] : t.tiles) t.physDirty.insert(k);   // tiles that exist already (cuts) get patches below
  for (int z = 0; z < t.chunksZ(); ++z)
    for (int x = 0; x < t.chunksX(); ++x) terrainPhysicsRebuildChunk(t, x, z);
  terrainPhysicsSync(t);
}

void terrainPhysicsSync(Terrain& t) {
  for (uint32_t b : t.stream.deadBodies) Physics::remove(b);
  t.stream.deadBodies.clear();
  std::set<int> chunks;
  // chunks whose base heights were edited (Terrain::baseEdited)
  auto& pr = t.stream.physRev;
  if (pr.size() != t.chunkRev.size()) pr = t.chunkRev;
  for (size_t i = 0; i < pr.size(); ++i) if (pr[i] != t.chunkRev[i]) { pr[i] = t.chunkRev[i]; chunks.insert((int)i); }
  if (t.physDirty.empty() && chunks.empty()) return;
  for (int k : t.physDirty) {
    int tx = t.keyTx(k), tz = t.keyTz(k);
    auto it = t.tiles.find(k);
    if (it != t.tiles.end()) {
      auto& tl = it->second;
      if (tl.body != 0xffffffffu) Physics::remove(tl.body);
      vec2 o = t.tileOrigin(tl.tx, tl.tz);
      tl.body = Physics::addHeightPatch(tl.h, Terrain::kFineN, vec3(o.x, 0, o.y), t.fineSpacing(), tilePatchUD(k));
    }
    chunks.insert((tz * Terrain::kTileCells / Terrain::kChunkCells) * t.chunksX() + tx * Terrain::kTileCells / Terrain::kChunkCells);
  }
  t.physDirty.clear();
  if (t.chunkBodies.empty()) return;   // no physics world for this terrain yet (thumbnails)
  for (int c : chunks) terrainPhysicsRebuildChunk(t, c % t.chunksX(), c / t.chunksX());
}

void terrainPhysicsRemovePatches(Terrain& t) {
  for (uint32_t b : t.stream.deadBodies) Physics::remove(b);
  t.stream.deadBodies.clear();
  for (auto& [k, tl] : t.tiles) if (tl.body != 0xffffffffu) { Physics::remove(tl.body); tl.body = 0xffffffffu; t.physDirty.insert(k); }
}
