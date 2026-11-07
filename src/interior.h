#pragma once
// Walls, interiors and furniture (docs/interiors.md): furniture props (collision, damage, fragments, support),
// room detection with keep-clear volumes, seeded room templates, and the per-building finish parameters of the
// wall / floor patterns 17-22.
#include "assets.h"
#include "render.h"
#include <deque>

class App;
struct Instance;
struct InstanceRT;
struct PlaceCheck;

// "prop" block of a furniture asset, with its derived meshes and shapes (built on first use)
struct PropDef {
  std::string family = "wood";            // wood painted ceramic glass cloth masonry paper
  int rounds = 0;                          // small-arms hits that break it (0: never by rounds)
  float blast = 40, front = 0.6f, mount = 0;
  bool blocking = true, wall = false;
  std::vector<std::string> groups;
  std::vector<int> fragments, keep;
  int glass = -1;
  std::vector<vec4> surfaces; std::vector<float> surfY;   // tops small props stand on: (x0, z0, x1, z1), y
  vec3 bmin{0}, bmax{0};                  // visible bounds (asset space)
  GpuMesh brokenMesh; ShapePtr brokenShape;
  struct Frag { GpuMesh mesh; ShapePtr shape; vec3 center{0}, half{0}; float mass = 1; };
  std::vector<std::unique_ptr<Frag>> frags;
  bool flat() const { return bmax.y - bmin.y < 0.06f; }   // rugs, papers
  bool hang = false;                      // L3 lights: hung from a ceiling / beam by its origin (top), supported from above
  vec3 gmin{0}, gmax{0};                  // L3: bounds of the glass group (a round there shatters a metal lantern's panes)
};

// Rooms of an enterable building (asset space), from a 0.1 m occupancy grid per storey
struct RoomInfo {
  enum Cell : int16_t { WALL = -1, DOORWAY = -2, NOFLOOR = -3 };
  struct Storey { float y = 0, ceil = 3; vec2 o{0}; int nx = 0, nz = 0; std::vector<int16_t> id; std::vector<uint8_t> keep; };   // keep: 1 door / stair clearance
  struct Room { int storey = 0; vec2 mn{0}, mx{0}; float y0 = 0, y1 = 3, area = 0; int cells = 0; bool hearth = false; vec3 hearthPos{0}; int type = 0; };
  struct Box { vec3 mn{0}, mx{0}; int kind = 0; std::string why; };   // kind 0 door swing, 1 stair, 2 window (from the sill up)
  struct Sill { vec3 c{0}; float halfW = 0.5f; vec2 n{0, 1}; float top = 0; };
  std::vector<Storey> storeys;
  std::vector<Room> rooms;
  std::vector<Box> keep;
  std::vector<Sill> sills;
  float cell = 0.1f;
  int16_t at(int s, vec3 p) const;   // cell id at an asset-space point on storey s (WALL outside the grid)
};

enum RoomType { ROOM_KITCHEN = 0, ROOM_SITTING, ROOM_BEDROOM, ROOM_UTILITY, ROOM_WORKSHOP, ROOM_HALL,
                ROOM_CHURCH, ROOM_BARN, ROOM_CLASS, ROOM_OFFICE, ROOM_TAVERN, ROOM_RUIN, ROOM_TYPES };
const char* roomTypeName(int t);

class InteriorSys {
public:
  // ---- definitions (cached per asset)
  const PropDef* prop(const AssetDef* a);
  bool isProp(const AssetDef* a) const { return a && a->raw.is_object() && a->raw.contains("prop"); }
  const RoomInfo* rooms(const AssetDef* a);
  // ---- per building copy
  bool hideBuiltin(const Instance& i) const;                 // furnished by room templates: built-in furniture hidden
  // ---- automatic furnishing (V4): every enterable building without extra.interior, on open / new map / placement.
  // Skips bunkers, aw_* / bch_* and extra.decor. Never pushes undo, never changes the dirty flag.
  bool autoFurnishable(App& app, const Instance& i);
  int autoFurnish(App& app, uint64_t hostId);
  int autoFurnishAll(App& app);
  bool autoOnPlace = true;                                   // placeInstance furnishes a new building (tests may turn it off)
  int autoFurnished = 0; float lastAutoMs = 0;
  ShapePtr staticShape(App& app, const Instance& i, const AssetDef* a);   // buildRT: building / prop static collision
  const SurfaceParams* surfaceFor(App& app, const Instance& i, const AssetDef* a);
  // ---- drawing
  bool propDraw(App& app, const Instance& i, const InstanceRT& r, const mat4& M, std::vector<DrawItem>& op, std::vector<DrawItem>& gl);
  void fragDrawItems(std::vector<DrawItem>& op);
  // near a building its furniture draws piece by piece (full detail, shared asset meshes); farther away the intact
  // pieces draw as one merged box-LOD mesh per building (small props culled sooner), and nothing beyond cullBig.
  // A piece that breaks, moves or is selected leaves the batch (rebuilt next frame) and draws on its own.
  void updateBatches(App& app);
  bool batching = true;
  float lodDist = 18.0f, cullBig = 75.0f, cullSmall = 32.0f;
  int batchDraws = 0, batchedPieces = 0, batchRebuilds = 0, soloDraws = 0, culledPieces = 0, batchPasses = 0, syncPasses = 0; size_t batchVerts = 0;
  // ---- damage (session overlay)
  int bulletHit(App& app, uint64_t iid, vec3 at, vec3 dir, std::string& surface);   // 0 not a prop, 1 stops the round, 2 penetrable
  void blast(App& app, vec3 p, vec3 dir, float dmg, float radius, int kind);
  bool breakProp(App& app, Instance& i, InstanceRT& r, vec3 push, const char* why, int violence = 0);   // violence 1: grenade / direct hit (every fragment group, splinters, dust)
  void tankContact(App& app, Instance& i, InstanceRT& r, vec3 at, vec3 dir, float speed);   // tank_ram: furniture is crushed, never blocks
  int checkSupport(App& app, vec3 near, float radius);       // unsupported props fall and break; returns how many
  bool supported(App& app, const Instance& i, const AssetDef* a, int* hitsOut = nullptr);
  void step(App& app, float dt);
  void reset(App& app);
  int applyToMap(App& app);
  void clearApplied(App& app);
  // ---- placement
  PlaceCheck checkPlacement(App& app, const AssetDef* a, vec3 pos, float yaw, uint64_t ignoreId);
  bool hoverPoint(App& app, vec3 ro, vec3 rd, vec3& out, uint64_t ignoreId);
  Instance* hostAt(App& app, vec3 w, uint64_t ignoreId = 0);
  // ---- room templates
  int furnish(App& app, uint64_t hostId, int mode);   // 0 regenerate (keeps overrides / deletions), 1 reroll, 2 reset
  int unfurnish(App& app, uint64_t hostId);
  int roomType(const AssetDef* a, const RoomInfo& ri, int k) const;
  void sync(App& app);                                 // Creative: generated pieces follow / leave with their host
  void editorPanel(App& app, Instance& s);
  // ---- state
  struct PropRT { bool broken = false, glassBroken = false, applied = false, crushed = false; float hits = 0; };
  std::map<uint64_t, PropRT> st;
  struct Frag { uint64_t inst = 0; const GpuMesh* mesh = nullptr; uint32_t body = 0xffffffffu; mat4 m{1}; float age = 0; bool frozen = false; };
  std::deque<Frag> frags;
  size_t maxLive = 32, maxKept = 240;                       // x budgetScale() (V2 particle budget)
  float budgetScale() const;
  int breaks = 0, fragsSpawned = 0, falls = 0, shielded = 0, glassBreaks = 0, maxLiveSeen = 0, fragsDropped = 0, crushed = 0, directHits = 0, splinterBursts = 0;
  float lastBlastMs = 0; int lastBlastBreaks = 0;
  std::string lastBreak;
  float lastFurnishMs = 0;
private:
  std::map<const AssetDef*, std::unique_ptr<PropDef>> props_;
  std::map<const AssetDef*, std::unique_ptr<RoomInfo>> rooms_;
  struct SurfEntry { uint32_t rev = 0; SurfaceParams sp; };
  std::map<uint64_t, SurfEntry> surf_;
  void freezeOldest();
  bool inBlast_ = false;
  // per-frame work is skipped while nothing changed: the edit revision, the instance count, the selection, or a
  // furniture state (epoch_: breaks, glass, crushing, resets)
  uint64_t epoch_ = 1, batchSig_ = 0, syncSig_ = 0;
  struct Batch { GpuMesh lop, slop; uint64_t sig = 0; std::vector<uint64_t> members; vec3 c{0}; float r = 0; bool built = false; size_t verts = 0; };
  std::map<uint64_t, Batch> batches_;                       // by host
  std::map<uint64_t, uint64_t> member_;                     // piece -> host while it is drawn by the batch
  struct CpuMesh { MeshData lod; bool small = false; };
  std::map<const AssetDef*, std::unique_ptr<CpuMesh>> cpu_;
  const CpuMesh& cpuMesh(const AssetDef* a);
  void rebuildBatch(App& app, uint64_t host, Batch& b, const std::vector<const Instance*>& pieces);
};
