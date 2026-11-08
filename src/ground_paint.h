#pragma once
// Lasso ground paint (docs/lasso.md#paint): ground types painted over the default grass, saved in the map
// (MapDoc::groundPaint). Texels are 0.5 m on a world-anchored lattice in 16 m chunks; each texel holds two layers
// (top type + weight, the type underneath + weight), so a soft edge between two painted types blends both.
// The terrain shader reads a copy on texture unit 23 (paintApply, spliced into kTerrainFS); grass, nature patches,
// the navmesh and footsteps read the CPU copy.
#include "lasso.h"

constexpr float kPaintTexel = 0.5f;
constexpr int kPaintChunk = 32;          // texels per chunk edge (16 m)
constexpr int kPaintTypes = 13;          // 0 = grass (the default ground; painting it erases)
inline int64_t groundPaintKey(int cx, int cz) { return (int64_t)((uint64_t)(uint32_t)cx << 32 | (uint32_t)cz); }
inline int groundPaintKeyX(int64_t k) { return (int)(int32_t)(uint32_t)((uint64_t)k >> 32); }
inline int groundPaintKeyZ(int64_t k) { return (int)(int32_t)(uint32_t)((uint64_t)k & 0xffffffffu); }

struct PaintType {
  std::string id, name;
  vec3 swatch{0.3f};        // panel colour
  float grass = 1;          // pasture grass density kept (0 = none)
  bool natureOk = true;     // flower / tall grass / stone patches may grow on it
  int navArea = -1;         // NavArea for the navmesh (-1 = ground)
  std::string step;         // footstep audio event ("" = the default)
};
const std::vector<PaintType>& groundPaintTypes();   // content/ground_paint.json (built-in defaults without it)

// paint `type` over the region: weight = smoothstep across the outline over `feather` metres x strength, with a
// noise-jittered edge. One undo step; returns the texels changed (0 = nothing, no undo step).
int groundPaintRegion(App& app, const LassoRegion& r, int type, float strength, float feather);
// per-type weights at a point (bilinear between texel centres, as the shader); w[0] = unpainted grass share
void groundPaintWeights(const MapDoc& doc, float x, float z, float w[kPaintTypes]);
float groundPaintAt(const App& app, float x, float z, int* type = nullptr);   // the dominant painted type's weight
// queries on the current map (App::doc, registered by groundPaintSync): grass, nature, navmesh, footsteps
float groundPaintGrass(float x, float z);
bool groundPaintBlocksNature(float x, float z);
int groundPaintNavArea(float x, float z);       // -1 = none
const std::string& groundPaintStep(float x, float z);
float lassoWaterAtCurrent(float x, float z);    // lassoWaterAt on the current map (-1e9 = none)
uint64_t lassoGroundRev();                       // changes with any paint or water body edit
uint64_t lassoGroundSig(vec2 lo, vec2 hi);       // paint texels and water bodies over a rectangle

// GPU: per frame (App::updateSurface), the texture follows MapDoc::groundPaint (changed chunks only)
void groundPaintSync(App& app);
void groundPaintBind(GLuint prog);               // Terrain::draw: unit 23 and the paint uniforms
const char* groundPaintGlsl();                   // prepended to kTerrainFS: paintApply(...)
void groundPaintPicker(int& type);               // panel (lasso_ui.cpp)

