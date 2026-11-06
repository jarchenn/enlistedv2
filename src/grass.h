#pragma once
// GPU-instanced pasture grass.
//  * Placement is a pure function of (template seed, chunk coordinates, terrain heights, disturbed soil,
//    exclusion rectangles): rebuilding a chunk always gives bit-identical instances, so undo, F5 reset
//    and reopening a map restore the same grass.
//  * Each chunk carries a cheap signature (overlapping deformation tiles + their revisions, overlapping
//    footprints). When it changes, a content signature (the actual fine height / soil samples under the
//    chunk + footprints) decides whether the chunk really needs rebuilding, so a crater only rebuilds the
//    chunks whose ground it changed.
//  * Instances are generated once at the highest density in a shuffled order; presets and distance LOD
//    draw a prefix of that list, so changing the preset never re-places grass.
#include "terrain.h"

struct GrassFootprint { vec2 pos; float yaw; vec2 bmin, bmax; float margin = -1; };   // asset-local XZ bounds, world placement; margin < 0 = kMargin

enum class GrassQuality : int { Off = 0, Low = 1, Medium = 2, High = 3 };
struct GrassPreset { const char* name; float density, radius, lodStart; };
const GrassPreset& grassPreset(GrassQuality q);

struct GrassInstance { vec4 posYaw; vec4 shape; };   // xyz root, yaw | height, width, packed RGBA8 albedo (bits), wind phase

class Grass {
public:
  static constexpr float kChunk = 12.0f;          // metres per chunk edge
  static constexpr float kMaxDensity = 40.0f;     // tufts per square metre generated (High draws all)
  static constexpr int kBlades = 7;               // blades per tuft (narrow, tapered; stems use 3)
  // species per tuft (complete update V1), chosen from correlated noise fields so patches grade into each other:
  // 0 short base turf, 1 medium meadow grass, 2 tall meadow stems with seed heads, 3 clover / broadleaf weeds,
  // 4 dry straw. Stored in the packed albedo's top byte (0x38 + species: never a NaN / denormal float pattern).
  enum Species { TURF = 0, MEDIUM = 1, TALL = 2, CLOVER = 3, STRAW = 4 };
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
  bool wind = true;

  // Keep chunks near the camera built and current. budgetMs < 0 builds everything synchronously.
  void update(const Terrain& t, uint32_t seed, const std::vector<GrassFootprint>& fp, vec3 cam, double budgetMs);
  void draw(GLuint prog, const mat4& vp, vec3 cam) const;
  void clear();

  size_t instanceCount() const;
  size_t gpuBytes() const;
  uint64_t layoutHash() const;                    // hash of every live instance (determinism tests)
  uint64_t chunkHash(int cx, int cz) const;
  int chunkCount() const { return (int)chunks.size(); }
  template <class F> void forEachInstance(F f) const { for (auto& kv : chunks) for (auto& g : kv.second.inst) f(g); }
  static float footprintDistance(const GrassFootprint& f, float x, float z);   // metres to bounds, < 0 inside
  bool density(float x, float z, const Terrain& t, const std::vector<GrassFootprint>& fp) const; // would a blade grow here?
  double lastBuildMs = 0, peakBuildMs = 0;        // CPU build time of the last update / worst single update
  int lastBuilt = 0, pending = 0;
  mutable int lastDrawnChunks = 0; mutable size_t lastDrawnBlades = 0;

private:
  std::map<int64_t, Chunk> chunks;
  GLuint vao = 0, bladeVbo = 0, bladeEbo = 0; int indexCount = 0;
  uint32_t seed_ = 0, frame = 0;
  float terrainSize = 0;
  void ensureGL();
  uint64_t signature(int cx, int cz, const Terrain& t, const std::vector<GrassFootprint>& fp) const;
  uint64_t contentSignature(int cx, int cz, const Terrain& t, const std::vector<GrassFootprint>& fp) const;
  void build(Chunk& c, const Terrain& t, const std::vector<GrassFootprint>& fp);
};
