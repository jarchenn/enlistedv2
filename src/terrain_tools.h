#pragma once
// V8 terrain tools (docs/terrain-tools.md): the sculpt brush and Generate hills (terrain_brush.cpp); decor that maps
// saved from the old Beautify tool still draws (decor.cpp).
// The editor reaches them through one-line hooks in editor.cpp / app.cpp.
#include "app.h"

// ---------------------------------------------------------------- sculpt brush
enum BrushMode : int { BR_RAISE = 0, BR_LOWER, BR_SMOOTH, BR_FLATTEN, BR_HILLS, BR_TERRACE, BR_COUNT };
const char* brushModeName(int m);
struct BrushParams {
  int mode = BR_RAISE;
  float radius = 12.0f;     // m
  float strength = 2.0f;    // raise / lower / hills / terrace: metres per second at the centre; smooth / flatten: blend rate
  float falloff = 0.6f;     // fraction of the radius that feathers to zero (0 = hard edge, 1 = all feather)
  float scale = 28.0f;      // hills: noise wavelength (m)
  uint32_t seed = 1;        // hills
  int steps = 3;            // terrace: number of steps across the falloff
  float target = 0;         // flatten: height picked at the stroke start
};
// one dab of the brush at c (world x/z) over dt seconds. Writes Terrain::sculpt, re-bases and remeshes the touched tiles
// (colliders follow through Terrain::physDirty) and re-seats the placements over them. Returns the touched tile keys.
std::vector<int> terrainBrushDab(App& app, const BrushParams& p, vec2 c, float dt);
void terrainStrokeBegin(App& app, BrushParams& p, vec3 at);   // one undo step per stroke; flatten picks its height here
void terrainStrokeEnd(App& app);
void terrainCommitSculpt(App& app, const std::vector<int>& keys);   // Terrain::sculpt -> MapDoc::terrainSculpt for those keys

// ---------------------------------------------------------------- whole-map layers
struct HillsParams { float amp = 4.0f, scale = 120.0f; uint32_t seed = 1; float pad = 4.0f, feather = 18.0f; };
json terrainProtectShapes(const App& app);     // footprints, objectives, spawns, routes, paths (Terrain::applyGenLayers format)
void terrainGenerateHills(App& app, const HillsParams& p);   // one undo step; placements re-seat
void terrainClearHills(App& app);
// re-applies MapDoc::terrainGen (regenerates the base grid, re-installs sculpt and deltas) and re-seats placements
void terrainRegenerate(App& app);

// placement seating (shared by the brush, hills and the ring)
struct SeatRec { uint64_t id = 0; float h = 0; uint64_t host = 0; std::vector<float> fp; bool rest = false; };   // rest: ED1 on an object (host = its support): never pulled to the terrain   // fp: GX1 pinned footprint samples (terrain_blend.cpp)
std::vector<SeatRec> terrainSeats(const App& app, vec2 c, float radius);   // radius < 0: every placement
int terrainReseat(App& app, const std::vector<SeatRec>& before);          // returns the number moved

// ---------------------------------------------------------------- decor saved by the old Beautify tool (decor.cpp)
bool isDecor(const Instance& i);
bool decorMayLeaveBorder(const App& app, uint64_t id, vec3 w);   // checkPlacement exception for extra.decor buildings
// cheap drawing for decor buildings (distance cull, no leaves / glass at range); true = handled (drawn or culled)
bool decorDraw(App& app, const Instance& i, InstanceRT& r, const mat4& M, std::vector<DrawItem>& op, std::vector<DrawItem>& gl);

// ---------------------------------------------------------------- editor hooks
void terrainToolbar(App& app);                 // toolbar button (editor.cpp)
void terrainMenus(App& app);                   // "Terrain" menu (main menu bar)
void terrainEditorUI(App& app);                // brush panel and Generate hills dialog
void terrainToolUpdate(App& app, float dt, bool uiMouse);   // strokes (updateCreative, Tool::Terrain)
void terrainOverlay(App& app, Overlay& ov);    // brush ring preview
