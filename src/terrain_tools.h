#pragma once
// V8 terrain tools (docs/terrain-tools.md): the sculpt brush and Generate hills (terrain_brush.cpp), and Beautify
// (beautify.cpp: open-ground scatter, the outer terrain ring, decorative border buildings).
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
struct SeatRec { uint64_t id = 0; float h = 0; uint64_t host = 0; std::vector<float> fp; };   // fp: GX1 pinned footprint samples (terrain_blend.cpp)
std::vector<SeatRec> terrainSeats(const App& app, vec2 c, float radius);   // radius < 0: every placement
int terrainReseat(App& app, const std::vector<SeatRec>& before);          // returns the number moved

// ---------------------------------------------------------------- Beautify
struct BeautifyParams {
  float rocks = 0.5f, vegetation = 0.5f, groundCover = 0.5f, water = 0.3f, buildings = 0.6f;   // 0..1 densities
  uint32_t seed = 1;
  bool inside = true;   // scatter open ground inside the playable area (existing placements are never moved)
  bool ring = true;     // grow the outer terrain ring and dress it (decor buildings, hedges, trees, scatter)
};
struct BeautifyReport { std::map<std::string, int> placed; int decor = 0, hedges = 0, ringTrees = 0, skippedRoles = 0, ringTiles = 0; std::vector<std::string> missing; };
BeautifyReport beautifyRun(App& app, const BeautifyParams& p);   // the caller owns the undo step
int beautifyRemove(App& app);                                     // removes earlier Beautify placements (extra.beautify)
bool isDecor(const Instance& i);
bool decorMayLeaveBorder(const App& app, uint64_t id, vec3 w);   // checkPlacement exception for extra.decor buildings
// cheap drawing for decor buildings (distance cull, no leaves / glass at range); true = handled (drawn or culled)
bool beautifyDecorDraw(App& app, const Instance& i, InstanceRT& r, const mat4& M, std::vector<DrawItem>& op, std::vector<DrawItem>& gl);
const json& beautifySettings();   // content/settings/beautify.json

// ---------------------------------------------------------------- editor hooks
void terrainToolbar(App& app);                 // toolbar button (editor.cpp)
void terrainMenus(App& app);                   // "Terrain" menu + "Beautify..." (main menu bar)
void terrainEditorUI(App& app);                // brush panel, Generate hills and Beautify dialogs
void terrainToolUpdate(App& app, float dt, bool uiMouse);   // strokes (updateCreative, Tool::Terrain)
void terrainOverlay(App& app, Overlay& ov);    // brush ring preview
