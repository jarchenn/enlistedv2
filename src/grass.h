#pragma once
// GPU-instanced pasture grass (GX3: clumps, docs/ground-grass.md).
//  * Placement is a pure function of (template seed, chunk coordinates, terrain heights, disturbed soil,
//    exclusion rectangles): rebuilding a chunk always gives bit-identical instances, so undo, F5 reset
//    and reopening a map restore the same grass.
//  * Each chunk carries a cheap signature (overlapping deformation tiles + their revisions, overlapping
//    footprints). When it changes, a content signature (the actual fine height / soil samples under the
//    chunk + footprints) decides whether the chunk really needs rebuilding, so a crater only rebuilds the
//    chunks whose ground it changed.
//  * Instances are generated once at the highest density in a shuffled order; presets and distance LOD
//    draw a prefix of that list, so changing the preset never re-places grass. The vertex shader fades the
//    instances and blades at the cut by their rank, so the LOD never pops.
//  * One instance is a clump of up to kBlades crossed cards (blades, leaves, twigs) pre-arranged with height
//    variation: tall in the middle, short and leaning outward at the rim. Tunables: content/settings/grass.json.
#include "terrain.h"

struct GrassFootprint { vec2 pos; float yaw; vec2 bmin, bmax; float margin = -1; bool soft = false; };   // asset-local XZ bounds, world placement; margin < 0 = kMargin; soft: a crop tile (pasture grass only, nature patches ignore it)

enum class GrassQuality : int { Off = 0, Low = 1, Medium = 2, High = 3 };
// density: clumps per m^2 inside nearR; beyond it the drawn share falls as (nearR / d)^falloff down to floor,
// and clumps keep fewer cards (midBlades by midR, farBlades by radius). windR: vertex wind only inside it.
// shadowR: clumps inside it cast sun shadows (0 = none).
struct GrassPreset { const char* name; float density, radius, nearR, midR, falloff, floor; int midBlades, farBlades; float windR, shadowR; };
const GrassPreset& grassPreset(GrassQuality q);

struct GrassInstance { vec4 posYaw; vec4 shape; };   // xyz root, yaw | height, width (+ tilt), packed RGBA8 albedo (bits), wind phase (pasture: ground colour bits)

class Grass {
public:
  static constexpr float kChunk = 12.0f;          // metres per chunk edge
  static constexpr float kMaxDensity = 44.0f;     // clumps per square metre generated (High draws all near the eye)
  static constexpr int kBlades = 12;              // cards per pasture clump
  static constexpr int kPlantBlades = 7;          // blades per nature-patch plant (nature.cpp)
  // species per clump, chosen from correlated noise fields so patches grade into each other:
  // 0 short fine turf, 1 medium meadow grass, 2 tall seed-head grass, 3 clover, 4 dry straw, 16 small shrub,
  // 17 broadleaf weed rosette (plantain / dock). Stored in the packed albedo's top byte (0x38 + species: never a
  // NaN / denormal float pattern). Nature patches use 5-15 (nature.h).
  enum Species { TURF = 0, MEDIUM = 1, TALL = 2, CLOVER = 3, STRAW = 4, SHRUB = 16, BROADLEAF = 17 };
  static constexpr float kMargin = 0.45f;         // clear band around footprint bounds (thresholds, steps)
  struct Chunk {
    int cx = 0, cz = 0;
    uint64_t sig = 0, content = 0;
    GLuint vbo = 0; int count = 0;
    float ymin = 0, ymax = 0;
    std::vector<GrassInstance> inst;              // CPU copy (determinism checks, memory report)
    uint32_t lastUsed = 0;
  };
  GrassQuality quality = GrassQuality::Medium;
  bool wind = true;                               // grass and trees (editor View menu, --off wind)
  bool nearWind = true;                           // settings: vertex wind on the near clumps (off = static grass)

  // Keep chunks near the camera built and current. budgetMs < 0 builds everything synchronously.
  void update(const Terrain& t, uint32_t seed, const std::vector<GrassFootprint>& fp, vec3 cam, double budgetMs);
  void draw(GLuint prog, const mat4& vp, vec3 cam) const;
  void drawShadow(GLuint prog, const mat4& lightVP, const mat4& camVP, vec3 cam) const;   // near clumps into the sun map
  void clear();

  size_t instanceCount() const;
  size_t gpuBytes() const;
  uint64_t layoutHash() const;                    // hash of every live instance (determinism tests)
  uint64_t chunkHash(int cx, int cz) const;
  int chunkCount() const { return (int)chunks.size(); }
  template <class F> void forEachInstance(F f) const { for (auto& kv : chunks) for (auto& g : kv.second.inst) f(g); }
  static float footprintDistance(const GrassFootprint& f, float x, float z);   // metres to bounds, < 0 inside
  static int speciesOf(const GrassInstance& g);   // decoded from the packed albedo
  bool density(float x, float z, const Terrain& t, const std::vector<GrassFootprint>& fp) const; // would a blade grow here?
  double lastBuildMs = 0, peakBuildMs = 0;        // CPU build time of the last update / worst single update
  int lastBuilt = 0, pending = 0;
  mutable int lastDrawnChunks = 0; mutable size_t lastDrawnBlades = 0, lastDrawnClumps = 0, lastShadowClumps = 0;

private:
  std::map<int64_t, Chunk> chunks;
  GLuint vao = 0, bladeVbo = 0, bladeEbo = 0; int indexCount = 0;
  uint32_t seed_ = 0, frame = 0;
  float terrainSize = 0, terrainSizeZ = 0;
  void ensureGL();
  void drawChunks(GLuint prog, const mat4& cullVP, vec3 cam, float maxD, int maxBlades, bool shadow) const;
  uint64_t signature(int cx, int cz, const Terrain& t, const std::vector<GrassFootprint>& fp) const;
  uint64_t contentSignature(int cx, int cz, const Terrain& t, const std::vector<GrassFootprint>& fp) const;
  void build(Chunk& c, const Terrain& t, const std::vector<GrassFootprint>& fp);
};
