#pragma once
#include "common.h"

struct TemplateInfo {
  std::string id, name, description, file;
  int version = 1;
  bool available = true;
  float size = 800, buildHalf = 300, waterLevel = -1000;
  uint32_t seed = 1;
  vec3 camPos{0, 40, 80}; float camYaw = 0, camPitch = -20;
  json raw;
  GLuint thumbnail = 0;
};

std::vector<TemplateInfo> loadTemplates(const fs::path& dir);

struct SurfaceFootprint { vec2 pos; float yaw; vec2 bmin, bmax; float strength = 1; };

// Ground = coarse base heightfield + sparse high-resolution deformation tiles.
// height() is the single canonical query used by rendering, physics patches, placement and grass.
class Terrain {
public:
  static constexpr int kTileCells = 5;      // base cells per tile edge (tile borders lie on base vertices)
  static constexpr int kFine = 35;          // fine intervals per tile edge (7 per base cell)
  static constexpr int kFineN = kFine + 1;  // fine samples per tile edge (even, as Jolt requires)
  struct Tile {
    int tx = 0, tz = 0;
    std::vector<float> h;      // total height per fine sample (base + delta)
    std::vector<float> dirt;   // disturbed-soil blend 0..1
    GLuint vao = 0, vbo = 0, ebo = 0; int count = 0;
    uint32_t body = 0xffffffffu;
    uint32_t rev = 0;          // bumped on every remesh (grass / cache invalidation)
  };

  void generate(const TemplateInfo& t);
  float height(float x, float z) const;
  float baseHeight(float x, float z) const;
  vec3 normal(float x, float z) const;
  float dirtAt(float x, float z) const;      // disturbed-soil blend at a point (0 off-tile)
  const Tile* tileAt(int tx, int tz) const { auto it = tiles.find(key(tx, tz)); return it == tiles.end() ? nullptr : &it->second; }
  bool raycast(vec3 o, vec3 d, float maxT, vec3& hit) const;
  bool inBuildArea(float x, float z) const { return std::fabs(x) <= buildHalf && std::fabs(z) <= buildHalf; }
  void draw(GLuint prog) const;
  void destroy();

  // deformation
  int tilesN() const { return (n - 1) / kTileCells; }
  float cell() const { return size / (n - 1); }
  float tileSize() const { return cell() * kTileCells; }
  float fineSpacing() const { return tileSize() / kFine; }
  vec2 tileOrigin(int tx, int tz) const { return vec2(-size * 0.5f + tx * tileSize(), -size * 0.5f + tz * tileSize()); }
  int key(int tx, int tz) const { return tz * tilesN() + tx; }
  Tile* activate(int tx, int tz);
  // crater; weight(x,z) in [0,1] protects foundations. Returns touched tile keys. The outline is an irregular oblong
  // (random per impact point, never narrower than `radius`, so walls are never steeper than the round profile).
  static float craterShape(vec2 c, float angle);   // radius multiplier (>= 1) of the crater at c in direction angle
  std::vector<int> crater(vec3 c, float radius, float depth, const std::function<float(float, float)>& weight, float maxDepth = 1.6f);
  void rebuildMesh(Tile& t);
  void clearTiles();
  std::map<int, std::vector<float>> deltas() const;          // canonical world-metre deltas for persistence
  void applyDeltas(const std::map<int, std::vector<float>>& d);
  // ground surface mask (surface.cpp): worn routes, ruts, foundation soil, wetness
  static constexpr int kSurfRes = 2048;   // 0.39 m texels over the 800 m template
  std::vector<uint8_t> surf;       // RGBA8: wear, wet, kind, rut
  uint32_t surfRev = 0; uint64_t surfSig = 0;
  void buildSurface(const json& routes, const std::vector<SurfaceFootprint>& feet);
  float surfSample(float x, float z, int channel) const;
  float wearAt(float x, float z) const { return surfSample(x, z, 0); }
  float wetAt(float x, float z) const { return surfSample(x, z, 1); }
  GLuint surfTex = 0;
  std::map<int, Tile> tiles;
  std::vector<uint8_t> holeMask;   // base-vertex collision holes under active tiles
  uint32_t revision = 0, tileRev = 0, generation = 0;

  int n = 256; float size = 800, buildHalf = 300, waterLevel = -1000;
  std::vector<float> h;
private:
  GLuint vao = 0, vbo = 0, ebo = 0; int count = 0;
  GLuint maskTex = 0;
  void setMask(int tx, int tz, uint8_t v);
};
