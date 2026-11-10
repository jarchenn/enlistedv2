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
  float pad = 0;                      // outline dilation (m): the tile grid's step, so the floor reaches the outline
  vec2 bmin{0}, bmax{0};
  bool operator==(const WorldCut& o) const { return owner == o.owner && poly == o.poly && floorY == o.floorY && topY == o.topY && bank == o.bank && pad == o.pad; }
};

// GX1 terrain stamp (terrain_cuts.cpp, built from the instances by terrain_blend.cpp): deformations that conform the
// ground to what is placed on it. Applied over the sculpted ground, before the polygon cuts and crater deltas:
//   PAD    pinned footprint levelled to y, blended into the surroundings over a smoothstep ring (raise or cut)
//   BERM   earth banks against a bunker's walls: repose profile from ridge segments, noise, smoothing band
//   TRENCH one joined trench network: spoil parapets raised either side, then the carve (floor, battered walls)
struct TerrainStamp {
  enum Kind : uint8_t { PAD = 0, BERM = 1, TRENCH = 2 };
  uint8_t kind = PAD;
  uint64_t owner = 0;                 // instance id (TRENCH: the network's lowest id)
  std::vector<uint64_t> members;      // every instance it belongs to (TRENCH: all pieces of the network)
  vec2 c{0}; float yaw = 0; vec2 half{0};   // PAD / BERM: oriented footprint (BERM: the walls' outline)
  float y = 0;                        // anchor: the instance's y
  float ring = 2.5f, skirt = 0;       // PAD: blend ring, max raise at the edge before the retaining skirt (0 = no cap)
  float grow = 0;                     // PAD: the level region reaches this far past the footprint (one grid step: no slope under the walls)
  std::vector<vec4> banks;            // BERM: ridge segments along the walls, local (ax, az, bx, bz)
  std::vector<vec2> bankH;            // BERM: crest height above y at each end of the segment (a flank tapers toward an open face)
  std::vector<vec4> holes;            // BERM: local rectangles kept clear (x0, z0, x1, z1): passages, embrasures
  std::vector<float> holeEdge;        // BERM: per hole, the bank's fade width past its edge (<= 0: StampParams::holeEdge)
  std::vector<vec4> cuts;             // BERM: entrance cuts, local centreline (ax, az, bx, bz) from the wall face outward
  std::vector<vec3> cutP;             // BERM: per cut (half width, retaining wall height at a, at b): floor at y, a ramp past b
  std::vector<vec4> seg;              // TRENCH: centreline segments, world (x0, z0, x1, z1)
  std::vector<vec2> segY;             // TRENCH: floor y at both ends
  std::vector<uint8_t> segJoin;       // TRENCH: bit 0 start continues into another segment, bit 1 the end does
  std::vector<int8_t> segFront;       // TRENCH: the fire side, +1 left of the segment, -1 right, 0 none (a lower parapet there)
  uint32_t seed = 0;
  vec2 bmin{0}, bmax{0};              // world bounds of everything the stamp touches
  bool operator==(const TerrainStamp& o) const {
    return kind == o.kind && owner == o.owner && members == o.members && c == o.c && yaw == o.yaw && half == o.half && y == o.y && ring == o.ring && skirt == o.skirt && grow == o.grow &&
           banks == o.banks && bankH == o.bankH && holes == o.holes && holeEdge == o.holeEdge && cuts == o.cuts && cutP == o.cutP && seg == o.seg && segY == o.segY && segJoin == o.segJoin && segFront == o.segFront && seed == o.seed;
  }
};
struct StampParams {                  // content/settings/terrain_blend.json (terrain_blend.cpp fills it)
  float reposeDeg = 33, crest = 0.35f, smoothing = 3, noise = 0.12f, noiseScale = 7, edgeJitter = 0.45f, blendK = 1.2f;
  float sparseSlope = 14, bareSlope = 31, contactBand = 0.8f, holeEdge = 1.2f;
  float soilMax = 0.55f, soilPerMetre = 1.4f, skirtEmbed = 0.15f;
  float carvePad = 0.45f;   // trench floor reaches this far past the revetment line (one grid step: the wall slope stays behind it)
  float depth = 1.62f, floorHalf = 0.68f, batter = 0.12f, parapetH = 0.45f, parapetW = 1.9f, parapetNoise = 0.35f, parapetFront = 0.5f;
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
    std::vector<float> sdirt;  // GX1 stamp soil mask 0..1 (trench mud, spoil, bare berm faces, pad embankments); derived, never saved
    std::vector<float> sew;    // GX1 stamp material per sample, 3 floats: packed mud (cut walls / floor), freshly turned earth (spoil), wet
    GLuint vao = 0, vbo = 0, ebo = 0; int count = 0;
    uint32_t body = 0xffffffffu;
    uint32_t rev = 0;          // bumped on every remesh (grass / cache invalidation)
    bool cut = false;          // a terrain cut covers part of it (kept active while the cut exists)
  };

  // width / depth: buildable area (0 = the template's); the grid covers it plus the template margin
  void generate(const TemplateInfo& t, float width = 0, float depth = 0);
  float height(float x, float z) const;
  float baseHeight(float x, float z) const;
  // ---- V8 sculpt layer (terrain_brush.cpp): sparse fine tiles of absolute metres added to the base heightfield.
  // groundBase = baseHeight + sculpt: the authored ground under cuts and craters (Tile::base includes it), so crater
  // deltas, dirt and persistence (deltas()) never see the sculpt; sculpted ground shades as grass.
  std::map<int, std::vector<float>> sculpt;   // tile key -> kFineN^2 metres (every key keeps its tile active)
  float sculptAt(float x, float z) const;
  float groundBase(float x, float z) const { return baseHeight(x, z) + (sculpt.empty() ? 0.0f : sculptAt(x, z)); }
  void sculptChanged(const std::vector<int>& keys);   // re-bases those tiles (craters kept) and remeshes them + neighbours
  uint32_t sculptRev = 0;                             // bumps with every sculpt change (GX1: pads re-derive, revetments refit)
  // ---- V8 generator layers (terrain_brush.cpp / beautify.cpp): MapDoc::terrainGen, applied by generate()
  //   {"ring": {"tiles": k, "seed": s, "amp": m}}   outer ring: k whole tiles of extra margin on every side (the inner
  //                                                 grid keeps its world positions, so tile keys remap exactly)
  //   {"hills": [{"amp", "scale", "seed", "pad", "feather", "protect": [...]}]}   rolling-hill stamps on the base grid
  json gen = json::object();
  int ringTiles() const;          // of `gen`
  static int remapKey(int key, int oldTilesX, int shift, int newTilesX) { int tx = key % oldTilesX, tz = key / oldTilesX; return (tz + shift) * newTilesX + (tx + shift); }
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
  const std::vector<uint64_t>* cutIgnore = nullptr;   // lasso group checks: these owners' cuts are ignored for support (lasso.cpp)
  float cutBase(float x, float z, uint64_t excludeOwner = 0) const;   // base height lowered by every cut (but excludeOwner's)
  float cutDepthAt(float x, float z) const { return baseHeight(x, z) - cutBase(x, z); }
  // ground for placing instance `owner`: its own cut is ignored, so re-placing a trench never sinks it into itself
  float placementHeight(float x, float z, uint64_t owner) const;
  int setCuts(std::vector<WorldCut> next);   // re-cuts the tiles under old and new outlines; returns tiles touched
  uint32_t cutRev = 0;
  // ---- GX1 stamps (terrain_cuts.cpp): pads, berms, trench networks; applied inside cutBase (Tile::base includes them)
  std::vector<TerrainStamp> stamps;
  StampParams sp;
  int setStamps(std::vector<TerrainStamp> next);   // re-bases the tiles under old and new stamps; returns tiles touched
  // stamped ground at a point from groundBase g (no polygon cuts); mask (optional): soil (grass), mud, fresh earth, wet
  float stampHeight(float x, float z, float g, const std::vector<const TerrainStamp*>& list, uint64_t excludeOwner, vec4* mask) const;
  float stampSoilAt(float x, float z) const;   // the stamp soil mask (0 off-tile)
  float trenchLipY(const TerrainStamp& s, float x, float z) const;   // the uncarved ground (with parapet) at a point
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
  std::map<int, std::vector<uint32_t>> chunkSplitBodies;   // chunks with damage: one base body per undamaged tile
  std::vector<uint32_t> chunkRev;   // per chunk: bumped by baseEdited
  GLuint maskTexture() const { return maskTex; }
private:
  GLuint maskTex = 0;
  float baseFormula(float wx, float wz, uint32_t seed) const;   // the procedural blank-slate ground (before W5 heightmaps)
  void applyGenLayers(const TemplateInfo& t, int innerNx, int innerNz);   // V8: ring + hill stamps over the base grid (terrain.cpp)
  void setMask(int tx, int tz, uint8_t v);
  void recutTile(Tile& t);
  int recutRegions(const std::vector<std::pair<vec2, vec2>>& regions);   // shared by setCuts / setStamps
  float stampEval(float x, float z, float g, const std::vector<const TerrainStamp*>& list, uint64_t excludeOwner, vec4* mask, bool carve) const;
  void stampsNear(vec2 lo, vec2 hi, std::vector<const TerrainStamp*>& out) const;
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
