#pragma once
// V6 (docs/plan-visual-physics-overhaul.md): seamless snapping, crush & flatten under buildings, and the editor's
// Select-tool mouse gestures. Implemented in snap.cpp (snapping, seams, crush) and editor_nav.cpp (gestures, zoom).
//
// Sockets: an asset declares "snap": [{"p": [x, y, z], "dir": [x, z], "kind": "trench"}] in asset space (dir points
// out of the piece through that end). Parts tagged "cap": n close socket n's free end and are left out (render and
// collision) while that socket is joined. content/settings/snap.json adds sockets for assets whose generator is not
// re-run (hedgerow_bocage) and lists kinds that join each other; path_* sockets follow the path's own length.
// Any kind works: two sockets join when their kinds are equal or listed as compatible.
#include "common.h"

class App;
struct AssetDef;
struct Instance;
struct PlaceCheck;
struct DrawItem;
struct GpuMesh;
struct ShapeHandle;
using ShapePtr = std::shared_ptr<ShapeHandle>;

struct SnapSocket { vec3 p{0}; vec2 dir{1, 0}; std::string kind; };

struct SnapSettings {
  float radius = 1.5f;        // m: placing / moving this close to a free compatible socket snaps onto it
  float joinTol = 0.06f;      // m: two sockets this close (and facing each other) are joined
  // crush & flatten (placing / moving a building)
  float crushHeight = 1.6f;   // m: vegetation / nature under the footprint up to this height is removed; taller blocks
  float crushMargin = 0.3f;   // m: footprint grown by this for the crush test
  float skirt = 2.5f;         // m: feathered flatten band around the footprint
  std::vector<std::string> buildingCategories;   // categories that crush and flatten
};
const SnapSettings& snapSettings();

// sockets of an asset (inst: per-instance length for paths); empty = not a snap piece
std::vector<SnapSocket> snapSockets(const App& app, const AssetDef* a, const Instance* inst = nullptr);
struct WorldSocket { vec3 p{0}, dir{1, 0, 0}; std::string kind; };
std::vector<WorldSocket> snapWorldSockets(const App& app, const Instance& i);
bool snapCompatible(const std::string& a, const std::string& b);

// One API for every snap rule: sandbag runs (snapDefense), path ends (snapPath), socket pieces. pos / yaw in: the
// cursor and the wanted heading; out: the snapped pose. ignoreId: the instance being moved. Returns true if snapped.
struct SnapHit { uint64_t other = 0; int mine = -1, theirs = -1; float dist = 0; };
bool snapPlacement(const App& app, const AssetDef* a, vec3& pos, float& yaw, uint64_t ignoreId = 0, SnapHit* hit = nullptr);
struct SnapGhost { bool on = false; vec3 pos{0}; float yaw = 0; };   // the Place tool's snapped preview pose (editor.cpp)
SnapGhost& snapGhost();

// checkPlacement hook: a pose sitting exactly on neighbours' sockets takes the chain's base height, the neighbours'
// terrain cuts are ignored for its grade, and the overlap test lets those neighbours through.
struct SnapFit { bool on = false; float y = 0, top = 0, low = 0; std::vector<uint64_t> neighbours; };
SnapFit snapFit(const App& app, const AssetDef* a, vec3 pos, float yaw, uint64_t ignoreId);

// joined sockets: bit k = socket k of instance i is joined to a neighbour (computed by snapSync)
uint32_t snapMask(const Instance& i);
int snapSync(App& app);   // every frame (w3Frame): joins, chain texture origins; re-builds pieces whose joins changed
// draw hook (app.cpp, foliage.cpp, paths): the piece without the caps / faces of its joined ends, chain texture origin
void snapDraw(App& app, const Instance& i, DrawItem& d);
// buildRT hook: the collision shape without the joined ends' caps (null = the asset's own)
ShapePtr snapShape(App& app, const Instance& i, const AssetDef* a);
// mesh of an asset with the given joined-socket mask (tests); nullptr if the asset has no sockets
const GpuMesh* snapMesh(App& app, const AssetDef* a, uint32_t mask, std::vector<vec3>* tris = nullptr);

// crush & flatten: checkPlacement hook (taller vegetation blocks) and the editor's Place tool / drag-move hook (removes
// the low vegetation under the footprint and flattens the ground to the base with a feathered skirt; the caller pushed
// undo). Not called from placeInstance: templates, tests and map tools place without touching the ground.
bool crushBuilding(const AssetDef* a);
bool crushVegetation(const AssetDef* b);   // growth / nature a building crushes or is blocked by (not hedgerow banks, not scatter patches)
bool crushBlocked(const App& app, const AssetDef* a, vec3 pos, float yaw, uint64_t ignoreId, std::string& reason);
int crushAndFlatten(App& app, uint64_t id);   // returns the number of instances removed

// Select tool LMB (editor_nav.cpp): press-drag orbits about the pressed point, a click selects, a press-drag that starts
// on the already selected instance moves it. One call per frame; px in window pixels, ro / rd the cursor ray.
struct EdPointer { vec2 px{0}; bool lmb = false, overUi = false; vec3 ro{0}, rd{0, 0, -1}; };
enum class EdGesture { None, Pending, Orbit, Move };
EdGesture edSelectPointer(App& app, const EdPointer& p);
void navZoomCentre(App& app, float steps);   // the +/- buttons: zoom toward the ground at the screen centre
