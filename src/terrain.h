#pragma once
#include "common.h"
#include "terrain_chunks.h"

struct TemplateInfo {
  std::string id, name, description, file;
  int version = 1;
  bool available = true;
  // Map size (SPEC 9.1): the buildable area is width x depth metres (x, z), centred on the origin; the terrain extends
  // `margin` metres beyond it on every side. `size` / `buildHalf` are the pre-9.1 square fields (read as a fallback).
  float width = 600, depth = 600, margin = 100;
  float size = 800, buildHalf = 300, waterLevel = -1000;
  uint32_t seed = 1;
  vec3 camPos{0, 40, 80}; float camYaw = 0, camPitch = -20;
  json raw;
  GLuint thumbnail = 0;
};

std::vector<TemplateInfo> loadTemplates(const fs::path& dir);

struct SurfaceFootprint { vec2 pos; float yaw; vec2 bmin, bmax; float strength = 1; };

// A terrain cut (navmeta.h TerrainCut) placed in the world by an instance: inside the outline the ground is lowered to
// a flat floor at floorY (instance y - depth), ramping linearly from the outline over `bank` metres. Never raises ground.
struct WorldCut {
  uint64_t owner = 0;                 // instance id
  std::vector<vec2> poly;             // world x/z outline
  float floorY = 0, topY = 0, bank = 0.3f;   // topY = instance y (cut height at the outline)
  vec2 bmin{0}, bmax{0};
  bool operator==(const WorldCut& o) const { return owner == o.owner && poly == o.poly && floorY == o.floorY && topY == o.topY && bank == o.bank; }
};

// Ground = coarse base heightfield + sparse high-resolution deformation tiles.
// height() is the single canonical query used by rendering, physics patches, placement and grass.
// The base grid is rectangular (nx x nz vertices, kCell spacing, min corner `org`). It is drawn and collided as
// kChunkCells-cell chunks (terrain_chunks.cpp): streamed LOD meshes and one Jolt heightfield per chunk.
class Terrain {
public:
  static constexpr float kCell = 800.0f / 255.0f;   // base vertex spacing (m): the pre-9.1 800 m / 256-vertex grid
  static constexpr int kTileCells = 5;      // base cells per tile edge (tile borders lie on base vertices)
  static constexpr int kFine = 35;          // fine intervals per tile edge (7 per base cell)
  static constexpr int kFineN = kFine + 1;  // fine samples per tile edge (even, as Jolt requires)
  static constexpr int kChunkCells = 20;    // base cells per chunk edge (4 tiles, ~62.7 m)
  struct Tile {
    int tx = 0, tz = 0;
    std::vector<float> h;      // total height per fine sample (authored base + delta)
    std::vector<float> base;   // authored base per fine sample: base heightfield lowered by terrain cuts
    std::vector<float> dirt;   // disturbed-soil blend 0..1
    GLuint vao = 0, vbo = 0, ebo = 0; int count = 0;
    uint32_t body = 0xffffffffu;
    uint32_t rev = 0;          // bumped on every remesh (grass / cache invalidation)
    bool cut = false;          // a terrain cut covers part of it (kept active while the cut exists)
  };

  // width / depth: buildable area (0 = the template's); the grid covers it plus the template margin
  void generate(const TemplateInfo& t, float width = 0, float depth = 0);
  float height(float x, float z) const;
  float baseHeight(float x, float z) const;
  vec3 normal(float x, float z) const;
  vec3 baseNormal(float x, float z) const;   // of the base heightfield only (chunk meshes)
  float dirtAt(float x, float z) const;      // disturbed-soil blend at a point (0 off-tile)
  const Tile* tileAt(int tx, int tz) const { if (tx < 0 || tz < 0 || tx >= tilesX() || tz >= tilesZ()) return nullptr; auto it = tiles.find(key(tx, tz)); return it == tiles.end() ? nullptr : &it->second; }
  bool raycast(vec3 o, vec3 d, float maxT, vec3& hit) const;
  bool inBuildArea(float x, float z) const { return std::fabs(x) <= buildHalfX && std::fabs(z) <= buildHalfZ; }
  bool inTerrain(float x, float z) const { return x >= org.x && z >= org.y && x <= org.x + size && z <= org.y + sizeZ; }
  // draws the streamed base chunks (LOD by distance to cam, culled by vp) and the deformation tiles
  void draw(GLuint prog, vec3 cam, const mat4& vp) const;
  void destroy();

  // deformation
  int tilesX() const { return (nx - 1) / kTileCells; }
  int tilesZ() const { return (nz - 1) / kTileCells; }
  int tilesN() const { return tilesX(); }   // pre-9.1 name (square maps)
  int keyTx(int k) const { return k % tilesX(); }
  int keyTz(int k) const { return k / tilesX(); }
  float cell() const { return kCell; }
  float tileSize() const { return cell() * kTileCells; }
  float fineSpacing() const { return tileSize() / kFine; }
  vec2 tileOrigin(int tx, int tz) const { return vec2(org.x + tx * tileSize(), org.y + tz * tileSize()); }
  int key(int tx, int tz) const { return tz * tilesX() + tx; }
  Tile* activate(int tx, int tz);
  // crater; weight(x,z) in [0,1] protects foundations. Returns touched tile keys. The outline is an irregular oblong
  // (random per impact point, never narrower than `radius`, so walls are never steeper than the round profile).
  static float craterShape(vec2 c, float angle);   // radius multiplier (>= 1) of the crater at c in direction angle
  std::vector<int> crater(vec3 c, float radius, float depth, const std::function<float(float, float)>& weight, float maxDepth = 1.6f, bool irregular = true);
  void rebuildMesh(Tile& t);
  void clearTiles();               // drops every crater delta; tiles under terrain cuts are rebuilt from the cuts
  std::map<int, std::vector<float>> deltas() const;          // canonical world-metre deltas (vs the authored base) for persistence
  void applyDeltas(const std::map<int, std::vector<float>>& d);
  // ---- terrain cuts (terrain_cuts.cpp)
  std::vector<WorldCut> cuts;
  float cutBase(float x, float z, uint64_t excludeOwner = 0) const;   // base height lowered by every cut (but excludeOwner's)
  float cutDepthAt(float x, float z) const { return baseHeight(x, z) - cutBase(x, z); }
  // ground for placing instance `owner`: its own cut is ignored, so re-placing a trench never sinks it into itself
  float placementHeight(float x, float z, uint64_t owner) const;
  int setCuts(std::vector<WorldCut> next);   // re-cuts the tiles under old and new outlines; returns tiles touched
  uint32_t cutRev = 0;
  // ---- base edits (map tools): bump the chunk revisions over a rectangle so meshes and colliders rebuild
  void baseEdited(vec2 lo, vec2 hi);
  // ground surface mask (surface.cpp): worn routes, ruts, foundation soil, wetness
  static constexpr int kSurfRes = 2048;   // texels over the 800 m pre-9.1 template (0.39 m); larger maps scale up the texel
  static constexpr size_t kSurfMaxTexels = 8u << 20;   // cap (8 M texels = 32 MB) for large maps
  int surfW = kSurfRes, surfH = kSurfRes;
  std::vector<uint8_t> surf;       // RGBA8: wear, wet, kind, rut (surfW x surfH over the terrain rectangle)
  uint32_t surfRev = 0; uint64_t surfSig = 0;
  void buildSurface(const json& routes, const std::vector<SurfaceFootprint>& feet);
  float surfSample(float x, float z, int channel) const;
  float wearAt(float x, float z) const { return surfSample(x, z, 0); }
  float wetAt(float x, float z) const { return surfSample(x, z, 1); }
  GLuint surfTex = 0;
  std::map<int, Tile> tiles;
  std::set<int> physDirty;         // tile keys whose physics patch / chunk holes need syncing (terrain_chunks.cpp)
  uint32_t revision = 0, tileRev = 0, generation = 0;

  int nx = 256, nz = 256;           // base vertices along x / z
  float size = 800, sizeZ = 800;    // terrain extent along x (`size`, the pre-9.1 square name) and z
  vec2 org{-400, -400};             // min corner
  float buildHalf = 300, buildHalfX = 300, buildHalfZ = 300, waterLevel = -1000;   // buildHalf = max of the two
  float margin = 100;
  std::vector<float> h;             // base heights, nx * nz, row-major in z
  // chunks: streamed render meshes + per-chunk physics bodies (terrain_chunks.cpp)
  int chunksX() const { return (nx - 2) / kChunkCells + 1; }
  int chunksZ() const { return (nz - 2) / kChunkCells + 1; }
  mutable TerrainStream stream;
  std::vector<uint32_t> chunkBodies;
  std::vector<uint32_t> chunkRev;   // per chunk: bumped by baseEdited
  GLuint maskTexture() const { return maskTex; }
private:
  GLuint maskTex = 0;
  void setMask(int tx, int tz, uint8_t v);
  void recutTile(Tile& t);
  friend struct TerrainStream;
};

// Map-size rules and the New Map estimate (SPEC 9.1; values from content/settings/editor.json)
struct MapSizeRules {
  float minSide = 400, maxSide = 4000, step = 100, margin = 100;
  float comfortableKm2 = 3.0f;      // [DECISION] above this buildable area the dialog warns (min-spec comfort)
  std::vector<vec2> presets{{600, 600}, {1000, 2000}, {1200, 2400}, {2000, 4000}};
  vec2 clampSize(vec2 wd) const;
};
const MapSizeRules& mapSizeRules();
struct TerrainEstimate { int verticesX = 0, verticesZ = 0, chunks = 0, tiles = 0; double cpuMB = 0, physicsMB = 0, gpuMB = 0, surfaceMB = 0, totalMB = 0; bool comfortable = true; };
TerrainEstimate estimateTerrain(float width, float depth, float margin = -1);
