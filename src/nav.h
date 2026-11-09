#pragma once
// Bot navigation (AI_SPEC 3): a tiled Recast/Detour navigation mesh baked from the live physics world (terrain tiles,
// craters, every static structure, wall states), so it always matches what the capsules collide with.
//   - 32 m tiles (content/ai/navigation.json), agent radius 0.35 / height 1.8 / crouch 1.2 / step 0.45 / slope 45.
//   - Area types with costs from navmeta.h (asset "nav": effect / area / areas); minefields, wire and deep water are
//     forbidden. Spans with less than standing headroom are crouch areas (same type, crouch flag, higher cost).
//   - Off-mesh links generated per tile from the voxel field: drop (one way), mantle (two way), vault, jump gap.
//   - Live rebuild: dirty tiles (+1 tile border) rebuild on a background thread; Nav::update() commits them on the
//     main thread and reports which tiles changed, so bots crossing them re-plan.
// Threads: the main thread owns the mesh and is its only writer. Path workers (ai_path.cpp) read it under
// sharedLock(); commit() takes the exclusive lock for the short addTile / removeTile swap.
#include "common.h"
#include "navmeta.h"
#include <memory>
#include <shared_mutex>
#include <atomic>
#include <functional>
#include <set>
#include <string>

class dtNavMesh; class dtNavMeshQuery; class dtQueryFilter;
using NavRef = uint64_t;                       // dtPolyRef (DT_POLYREF64)

// Detour area ids (Recast area 0 is "not walkable"): 1 + NavArea, +16 when headroom is below standing height;
// off-mesh link areas follow.
namespace navarea {
constexpr uint8_t kCrouchBit = 16;
constexpr uint8_t kLinkDrop = 40, kLinkMantle = 41, kLinkVault = 42, kLinkJump = 43;
inline uint8_t fromNav(NavArea a, bool crouch = false) { return (uint8_t)(1 + (int)a + (crouch ? kCrouchBit : 0)); }
inline NavArea toNav(uint8_t id) { if (id >= kLinkDrop) return NavArea::Default; int b = (id & ~kCrouchBit) - 1; return b >= 0 && b < (int)NavArea::Count ? (NavArea)b : NavArea::Default; }
inline bool crouch(uint8_t id) { return id < kLinkDrop && (id & kCrouchBit); }
inline bool isLink(uint8_t id) { return id >= kLinkDrop && id <= kLinkJump; }
}
// Detour polygon flags
enum NavFlags : uint16_t { NF_WALK = 1, NF_CROUCH = 2, NF_FORBID = 4, NF_LINK = 8,
  NF_TRAP = 16,        // ground polygon from which no anchor (objective / spawn) can be walked to: a dead-end pocket
  NF_TRAPLINK = 32,    // one-way link landing in a pocket (excluded by the default filter)
  NF_BADLINK = 64 };   // a link that keeps blocking bodies (W6b: disabled after repeated aborts; excluded)
enum NavLinkKind : int { LINK_DROP = 0, LINK_MANTLE, LINK_VAULT, LINK_JUMP, LINK_KINDS };
const char* navLinkName(int k);

struct NavConfig {
  float tile = 32, cs = 0.16f, ch = 0.05f, radius = 0.3f, height = 1.8f, crouchHeight = 1.2f, climb = 0.45f, slope = 45;
  float regionMin = 4, regionMerge = 40, edgeMaxLen = 12, edgeMaxError = 1.3f, detailDist = 6, detailErr = 1, linkBorder = 2.6f;
  float dropMin = 0.5f, dropMax = 3.0f, mantleMin = 0.5f, mantleMax = 1.6f, vaultMax = 1.1f, vaultThick = 0.6f, gapMax = 1.5f, linkSpacing = 1.2f;
  float shallowWater = 0.9f;                    // deeper water is forbidden; shallower counts as marsh
  float linkCost[LINK_KINDS] = {2.0f, 4.0f, 2.5f, 3.0f};   // extra path cost (m equivalent) per traversal
  int workers = 2;                              // background rebuild threads
  void load(const json& j);
};

// A traversal link (debug, stuck recovery, movement): world endpoints, kind, the obstacle top for vault / mantle.
struct NavLink { vec3 a{0}, b{0}; int kind = 0; bool bidir = true; float top = 0; uint32_t id = 0; };
// one wall edge of the mesh (cover generation, debug)
struct NavEdge { vec3 a{0}, b{0}; vec3 n{0}; uint8_t area = 1; };

struct NavTileState;   // nav.cpp
struct NavBuildJob;

class Nav {
public:
  Nav(); ~Nav();
  Nav(const Nav&) = delete; Nav& operator=(const Nav&) = delete;
  NavConfig cfg;
  // ---- lifecycle (main thread)
  void build(vec3 lo, vec3 hi, int threads = 0);       // full bake of the tiles covering lo..hi (blocking, thread pool)
  // build(): with cachePath set, the baked tiles are loaded from that file when it exists (seconds -> milliseconds),
  // else written there after the bake; progress (0..1) is called from the main thread while the bake runs
  std::string cachePath; bool fromCache = false;
  std::function<void(float)> progress;
  void rebuildArea(vec3 lo, vec3 hi);                  // mark tiles dirty (+1 tile border): queued by update() on a budget
  void update();                                       // commit finished rebuilds; fills `changed`
  void clear();
  bool valid() const { return mesh_ != nullptr && tilesBuilt > 0; }
  bool busy() const;                                   // a rebuild is queued or running
  // ---- queries (main thread)
  // 0 when p is within maxDist of a walkable (not forbidden) polygon, -1 otherwise (kept for game.cpp's spawn check)
  int nearest(vec3 p, float maxDist = 2.0f) const;
  bool closest(vec3 p, vec3 ext, NavRef& ref, vec3& onMesh, const dtQueryFilter* f = nullptr) const;
  bool closest(vec3 p, float r, NavRef& ref, vec3& onMesh) const { return closest(p, vec3(r, std::max(r, 2.0f), r), ref, onMesh); }
  bool pathSync(vec3 a, vec3 b, std::vector<vec3>& pts, std::vector<NavRef>* polys = nullptr, const dtQueryFilter* f = nullptr, float* lenOut = nullptr) const;   // tests / tools
  bool raycastMesh(vec3 a, vec3 b, float& t) const;    // straight walk along the mesh surface (t = 1 clear)
  uint8_t areaAt(vec3 p, float r = 1.0f) const;        // Detour area id under p (0 = off mesh)
  dtNavMesh* mesh() const { return mesh_; }
  dtNavMeshQuery* query() const { return query_; }
  const dtQueryFilter& defaultFilter() const;
  std::shared_mutex& sharedLock() const { return mtx_; }
  // exclusive lock for main-thread writes (tile swaps, flags). std::shared_mutex is not fair: path workers that unlock
  // and relock between A* slices could hold off a writer for seconds (100 AI), so they wait while writerWaiting()
  std::unique_lock<std::shared_mutex> writeLock() const { ++writers_; std::unique_lock<std::shared_mutex> l(mtx_); --writers_; return l; }
  bool writerWaiting() const { return writers_.load(std::memory_order_relaxed) > 0; }
  // ---- tiles
  int tileOf(float v) const { return (int)std::floor(v / cfg.tile); }
  uint32_t tileStamp(int tx, int tz) const;            // bumps on every commit of that tile
  uint32_t stamp() const { return stamp_; }            // bumps on every commit
  std::vector<std::pair<int, int>> changed;            // tiles committed by the last update()
  std::vector<NavLink> linksNear(vec3 p, float r) const;
  const NavLink* linkById(uint32_t id) const;
  std::vector<NavEdge> tileEdges(int tx, int tz) const;  // wall edges of a committed tile (cover generation)
  void forEachTile(const std::function<void(int, int)>& fn) const;
  // dead-end pockets (an AT ditch floor reached only by drops): ground polygons that cannot walk to any anchor get NF_TRAP,
  // one-way links into them NF_TRAPLINK. Re-run after commits (tile rebuilds start without the flags). Returns trap polygons.
  int markTraps(const std::vector<vec3>& anchors);
  bool isTrap(vec3 p, float r = 0.8f) const;
  vec3 lo{0}, hi{0};
  float waterLevel = -1000;                            // terrain water: shallow = water area, deep = forbidden
  std::function<float(float x, float z)> waterQuery;   // local water too (water_query.h waterAt): sampled per tile job on the calling thread
  // ---- statistics (tests / overlay)
  double buildMs = 0, lastRebuildMs = 0, rebuildMsMax = 0; int usedNodes = 0, links = 0, tilesBuilt = 0, rebuilds = 0, queued = 0;
  int linkCount[LINK_KINDS] = {0, 0, 0, 0};
  std::function<int(uint64_t ud)> classify;            // body user data -> 0 solid, 1 wire, 2 skip, 3 carve (set by the AI)
  std::function<void(int tx, int tz, struct NavAreaMarks&)> areaMarks;   // convex area volumes per tile (the AI)
private:
  dtNavMesh* mesh_ = nullptr; dtNavMeshQuery* query_ = nullptr;
  mutable std::shared_mutex mtx_;
  mutable std::atomic<int> writers_{0};
  std::set<std::pair<int, int>> pending_;   // dirty tiles not yet snapshotted (deduplicated: craters overlap)
  bool loadCache();
  void saveCache(const std::vector<std::unique_ptr<NavBuildJob>>& jobs);
  void queueTile(int tx, int tz);
  std::unique_ptr<struct NavImpl> impl_;
  uint32_t stamp_ = 0;
  void commit(NavBuildJob& j);
  void initMesh();
};

// convex area volumes for one tile (filled on the main thread from the placed instances' nav metadata)
struct NavAreaMark { std::vector<vec2> poly; float hmin = -1000, hmax = 1000; uint8_t area = 1; bool nullify = false; };
struct NavAreaMarks { std::vector<NavAreaMark> marks; };
