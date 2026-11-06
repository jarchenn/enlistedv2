#pragma once
// Chunked base terrain (SPEC 9.1): the base heightfield is split into Terrain::kChunkCells-cell chunks.
//  * Rendering: each chunk gets a mesh at one of kLods levels of detail (vertex step 1, 2, 4, 10, 20 cells) chosen by
//    distance to the camera, with skirts that hide LOD seams. Meshes are built on demand within a per-frame budget
//    (nearest / visible first) and evicted beyond maxDist; all chunks of one LOD share a pooled vertex buffer and one
//    index buffer and are drawn with a single glMultiDrawElementsBaseVertex after frustum culling.
//  * Physics: one Jolt heightfield body per chunk at full base resolution. Interior vertices of active deformation
//    tiles are holes (the tile's own high-resolution patch collides there).
#include "common.h"

class Terrain;

struct TerrainStreamConfig {
  float lodDist[4] = {260, 560, 1100, 2200};   // m (3D distance to the chunk box): LOD0 below the first, ...
  float maxDist = 5000;                        // chunks farther than this hold no mesh
  float buildBudgetMs = 1.5f;                  // per frame once something is drawn (the first frame builds everything)
  int minBuildsPerFrame = 8;
  float hysteresis = 0.08f;                    // a loaded LOD changes only when the distance is past the threshold by this fraction
  float skirtDepth = 2.0f;                     // m below the edge (plus one cell step per LOD)
};

struct TerrainStream {
  static constexpr int kLods = 5;
  static constexpr int kStep[kLods] = {1, 2, 4, 10, 20};
  struct Chunk { int lod = -1, slot = -1; float hmin = 0, hmax = 0; uint32_t rev = 0; bool bounds = false; };
  struct Pool { GLuint vao = 0, vbo = 0, ebo = 0; int vertsPerSlot = 0, indexCount = 0, capacity = 0, used = 0; std::vector<int> freeSlots; };
  struct Stats { int loaded[kLods] = {}; int builtLastFrame = 0, drawnLastFrame = 0, culledLastFrame = 0, visibleUnloaded = 0, drawCalls = 0; size_t gpuBytes = 0; int64_t tris = 0; float buildMs = 0; };
  TerrainStreamConfig cfg;
  std::vector<Chunk> chunks;
  Pool pools[kLods];
  Stats stats;
  uint32_t generation = ~0u;
  int cx = 0, cz = 0;
  std::vector<uint32_t> deadBodies;   // tile patch bodies of removed tiles, destroyed by the next physics sync
  std::vector<uint32_t> physRev;      // per chunk: chunkRev its collider was built from

  // brings meshes up to date for a camera (const terrain: called from Terrain::draw). budgetMs < 0 = no limit.
  void update(const Terrain& t, vec3 cam, const mat4& vp, float budgetMs);
  void drawBase(const Terrain& t, vec3 cam, const mat4& vp);
  int desiredLod(const Terrain& t, int idx, vec3 cam, int current) const;
  float chunkDistance(const Terrain& t, int idx, vec3 cam) const;
  void chunkBox(const Terrain& t, int idx, vec3& lo, vec3& hi) const;
  void release();   // all GL objects (map change, shutdown)
  size_t loadedCount() const { size_t n = 0; for (int l = 0; l < kLods; ++l) n += stats.loaded[l]; return n; }
private:
  void reset(const Terrain& t);
  void ensurePool(int lod, int needSlots);
  void build(const Terrain& t, int idx, int lod);
  void unload(int idx);
};

// physics: (re)build every chunk body (map change); sync tile patches + chunk holes for Terrain::physDirty
void terrainPhysicsBuild(Terrain& t);
void terrainPhysicsSync(Terrain& t);
void terrainPhysicsRemovePatches(Terrain& t);   // drops every tile patch body (tiles keep their heights)
void terrainPhysicsRebuildChunk(Terrain& t, int cx, int cz);
size_t terrainPhysicsBytesEstimate(int chunks);
