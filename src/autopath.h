#pragma once
// Path geometry beyond straight strips (paths.cpp) and auto paths between houses (autopath.cpp, docs/entities.md).
//
// A path instance may carry, in Instance::extra["path"] next to width / length:
//   "bend": deg   the strip follows a circular arc turning this much over its length (signed, + toward local +x);
//                 the instance sits at the arc's midpoint with local +z the tangent there (0 / absent = straight)
//   "u0":   m     along-path texture offset, so chained pieces continue the surface pattern across their joints
// Auto paths (Paths > Connect houses, or the "Auto paths" toggle on placement) are ordinary path_dirt instances with
// Instance::extra["auto"] = "apron" | "link", plus ground-wear routes in MapDoc::extra["routes"] with "auto": true.
// Re-running replaces exactly those; the whole operation is one undo step (the undo snapshot carries the routes).
#include "app.h"
#include <functional>

struct PathShape { float bend = 0, u0 = 0; };
PathShape pathShape(const Instance& i);
void setPathShape(Instance& i, PathShape s);
// arc centre line in the piece's frame: s in [-L/2, L/2] -> local xz, unit heading (tangent)
vec2 pathCentre(float L, float bendDeg, float s, vec2* heading = nullptr);
// a free end (sgn -1 / +1) of a placed path: world position and outward direction (xz, unit)
void pathEnd(const App& app, const Instance& i, const AssetDef* a, float sgn, vec3& pos, vec2& outDir);
// asset-local XZ bounds of the surface (straight: +-W/2, +-L/2; bends: the arc's box)
void pathLocalBounds(const App& app, const Instance& i, const AssetDef* a, vec2& mn, vec2& mx);
// world point -> (across, along) in the piece's own path coordinates; false when outside the strip
bool pathLocalCoords(const App& app, const Instance& i, const AssetDef* a, vec3 w, vec2& acrossAlong);
// the surface mesh (paths.cpp): buildPathMeshData plus the shape
void buildPathMeshDataEx(MeshData& m, const PathDef& p, vec2 wl, vec3 pos, float yaw, const std::function<float(float, float)>& height,
                         const std::vector<uint8_t>* cut, PathShape shape);

// ---- auto paths (autopath.cpp)
struct AutoPathReport {
  int houses = 0, entrances = 0, aprons = 0, links = 0, loops = 0, spurs = 0, failed = 0, relaxed = 0, pieces = 0, joints = 0, routes = 0, removed = 0;
  int components = 0;        // connected groups of houses after the run (1 = every house reachable from every other)
  int lamps = 0;             // L3: lamp posts lining the routes (Paths > Line with lamp posts; extra.auto = "lamp")
  double ms = 0;
  std::string summary() const;
};
bool autopathIsHouse(const AssetDef* a);   // enterable buildings with ground-level entrances (not bunkers / defences)
// rebuilds every auto path (aprons + links + routes) on the map; pushUndo first when undoStep
AutoPathReport autopathConnectHouses(App& app, bool undoStep = true);
int autopathRemove(App& app);              // removes auto paths and auto routes (no undo push); returns pieces removed
void autopathOnPlaced(App& app, uint64_t id);   // App::placeInstance hook: with the toggle on, connects a new house
void autopathMenu(App& app);               // Creative menu bar: Paths > Connect houses / Remove auto paths / Auto paths
extern bool gAutoPaths;                    // the "Auto paths" toggle (session)
