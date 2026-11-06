#pragma once
#include "common.h"
struct GpuModel;

// Surface pattern IDs understood by the main shader.
enum Pattern : int {
  PAT_PLAIN = 0, PAT_STONE = 1, PAT_WOOD = 2, PAT_PLASTER = 3, PAT_SLATE = 4, PAT_TILE = 5,
  PAT_METAL = 6, PAT_GLASS = 7, PAT_FABRIC = 8, PAT_BRICK = 9, PAT_PAINTED = 10, PAT_CONCRETE = 11,
  PAT_BUNKER = 15   // board-formed (12-14: path surfaces, src/paths.cpp) bunker concrete; grain = height of the roof lip in decimetres (run-off stains below it)
};

struct Material {
  std::string name;
  vec3 albedo{0.6f};
  float roughness = 0.8f, metallic = 0.0f;
  int pattern = PAT_PLAIN;
  int grainAxis = 1; // part-local axis that wood grain / slate courses follow
};

// A primitive part. Boxes and triangular prisms, in a parent space.
struct Part {
  enum Shape { BOX, PRISM, ROUND } shape = BOX;   // ROUND: a box with every edge rounded by `rad` (render and collision)
  vec3 center{0}, size{1};
  quat rot{1, 0, 0, 0};
  int mat = 0;
  bool visible = true, collide = true;
  bool playerOnly = false;   // "k": 2 - traversal volume only the player meets (barbed wire), ignored by rays and bodies
  int sec = -1;              // "sec": destruction section it belongs to (wire obstacles), -1 = always present
  // PRISM: triangle in (u,v) plane extruded along `axis` from a0..a1.
  int axis = 0; float a0 = 0, a1 = 0; vec2 tri[3]{};
  float rad = 0;   // ROUND: edge radius
};

struct Vertex {
  vec3 pos, nrm, albedo;
  vec4 params;  // pattern, roughness, metallic, grain axis
  vec3 pcoord;  // part-local position (for rotated-part patterns)
  vec3 pnrm;    // part-local normal
};

struct MeshData { std::vector<Vertex> v; std::vector<uint32_t> i; };

struct GpuMesh {
  GLuint vao = 0, vbo = 0, ebo = 0; int count = 0;
  void upload(const MeshData& m);
  void destroy();
  void draw() const;
};

struct ShapeHandle;                       // physics shape (defined in physics.cpp)
using ShapePtr = std::shared_ptr<ShapeHandle>;

struct Leaf {
  vec3 hinge{0};            // pivot in asset space
  float sign = 1;           // rotation direction multiplier
  std::vector<Part> parts;  // in hinge-relative space
  GpuMesh opaque, glass;
  ShapePtr shape;           // collision, hinge-relative
  std::vector<Part> colliders; // hinge-relative boxes (also used for obstruction tests)
};

struct InteractDef {
  std::string id, kind, label, sound;
  vec3 axis{0, 1, 0};
  float openAngle = 95, duration = 0.9f;
  float hp = -1;            // bullet hits to break (doors / shutters); -1 = default for the kind
  bool defaultOpen = false;
  bool apAlways = false;   // aim volume stays active when open (flaps worked through a slit)
  std::vector<Leaf> leaves;
  vec3 apMin{0}, apMax{0}; // aim volume (asset space)
};

// A destructible wall bay. Owns both wall layers; geometry and collision per damage state.
enum PanelState : uint8_t { PANEL_INTACT = 0, PANEL_DAMAGED = 1, PANEL_BREACHED = 2 };
struct Panel {
  std::string id, family;
  float hp = 200;
  vec3 bmin{0}, bmax{0}, normal{0, 0, 1};
  std::vector<Part> parts[3];
  GpuMesh mesh[3];
  ShapePtr shape[3];
};

// Crew-served station (field gun / mounted MG): named moving parts with local pivots.
struct StationDef {
  std::string kind;                         // "" = not a station; "pak40", "m2hb"
  vec3 yawPivot{0}, elevPivot{0};           // asset space, default pose
  vec3 muzzle{0}, optic{0}, gunner{0}, handler{0};
  float yawMin = -32.5f, yawMax = 32.5f, elevMin = -5.0f, elevMax = 22.0f;
  float traverseRate = 14.0f, elevateRate = 8.0f, recoil = 0.6f, reloadTime = 3.5f;
  int loaded = 1, reserve = 5, reserveAT = 0; std::string ammo, ammoAT;   // AT: second ammunition type (PaK 40 PzGr. 39)
  std::vector<Part> yawParts, elevParts, recoilParts;   // relative to their pivots
  GpuMesh yawMesh, elevMesh, recoilMesh;
  vec3 moveHalf{1.0f, 0.6f, 2.0f};          // carriage sweep box (half extents) for repositioning
  float serviceRadius = 6.0f;
  // gun tuning (game values; defaults are the PaK 40's): sight field, shells, report
  std::string label = "PaK 40", heName = "Sprgr. (HE)", atName = "PzGr. 39 (AT)";
  float sightFov = 9.0f, heVelocity = 550, heDamage = 120, heRadius = 2.2f, heCrater = 1.0f, atVelocity = 790, muzzleScale = 1.0f;
  std::string fireSound = "pak_fire";
  bool fixed = false;                       // emplacement: crewed in place, never moved (no handler / Move)
  std::vector<int> modelRole;               // per supplied-model part: 0 base, 1 traverse, 2 elevation, 3 recoil
};

// Destructible tree (Vegetation): trunk parts split at the stump cut, alpha-tested leaf cards with a sway mask.
struct TreeDef {
  bool on = false;
  float hpBullet = 30, hpAT = 1, stumpH = 0.4f, trunkR = 0.25f, mass = 300, leafSize = 0.35f;
  vec3 leafCol{0.2f, 0.3f, 0.1f};
  std::vector<vec4> canopy;                 // xyz centre (asset space), w radius
  float top = 5;                            // highest point (canopy) for the fall impulse
  bool hedge = false;                       // hedgerow / bank: indestructible, grass excluded over the bank
  GpuMesh stumpMesh, trunkMesh;             // trunkMesh / trunkShape relative to the cut (0, stumpH, 0)
  ShapePtr stumpShape, trunkShape;
  GLuint cardVao = 0, cardVbo = 0; int cardVerts = 0;   // leaf cards (asset space): pos, normal, uv, mask
};
// Buildable defence: tank trap / sandbags (builder.cpp, ballistics.cpp).
struct DefenseSocket { vec3 p{0}, t{1, 0, 0}; };
// path entity (paths.cpp): length / width per instance (extra.path), draped on the terrain
struct PathDef { bool on = false; std::string kind; float width = 2.5f, length = 8, minW = 0.8f, maxW = 8, minL = 1, maxL = 60;
                 vec3 albedo{0.5f}; float rough = 0.85f, slab = 2.0f, slabHp = 60; };
struct DefenseDef {
  std::string kind;                         // "" = not a defence; "tank_trap", "sandbag"
  float hp = 300, smallArms = 1, occlusion = 1, maxSlopeDeg = 35;
  bool blocksVehicles = false, stopsBullets = false;
  bool unsafeVault = false;                 // barbed wire: never vaulted, slows traversal
  std::string traversal = "block";          // wire: "block" (player-only volume stops the player) or "slow" (no volume; speed x slowFactor inside)
  float slowFactor = 0.35f;
  int sections = 0; float sectionHp = 60;   // wire: blasts cut individual stretches (model sections + "sec" colliders)
  std::vector<DefenseSocket> sockets;
};

struct AssetDef {
  StationDef station;
  TreeDef tree;
  PathDef path;
  // imported model (JSON "model": baked from a supplied GLB by --import-weapon-models into content/assets/models/<dir>)
  std::shared_ptr<GpuModel> model, modelLod;   // modelLod: <dir>/lod1, drawn beyond modelLodDist
  float modelLodDist = 30, modelCull = 400;   // m
  float modelThinPx = 0;                       // thin-line widening (wire), pixels
  ShapePtr playerShape;                        // union of "k": 2 parts (no sections)
  GpuMesh colliderGhost;                       // every collider as a box: placement preview of model assets
  DefenseDef defense;
  GpuMesh defBrokenMesh; ShapePtr defBrokenShape;
  bool rack = false;             // weapon rack (arsenal.cpp)
  std::string rackFaction;       // "US" / "DE": the registry side it lists
  struct RackDisplay { std::string weapon, pose = "lean"; vec3 p{0}; float lean = 10, dir = 1, yaw = 0; mat4 m{1}; };
  std::vector<RackDisplay> rackDisplay;   // decorative world models resting on the rack
  std::vector<std::string> rackDeploy;    // deployable equipment listed (station assets placed through build mode)
  std::string faction;           // asset-level faction tag (stations)
  vec3 gunHitMin{0}, gunHitMax{0};   // station: direct-hit volume (asset space, default pose)   // destroyed defence: sandbag spill / tipped hedgehog
  json crate;                               // ammo crate config (empty = not a crate)
  std::vector<Panel> panels;
  GpuMesh panelsIntact;          // all panels intact, merged (fast path for undamaged copies)
  ShapePtr wallsIntact;          // shared compound of all intact panel shapes
  std::string id, name, category, description, file;
  int version = 1, floors = 1;
  bool enterable = false;
  vec2 footprint{4, 4};
  float plinthDepth = 1.0f, maxGrade = 1.0f;
  std::vector<Material> materials;
  std::vector<Part> parts;       // static parts incl. furniture
  std::vector<std::string> furnitureNames;
  std::vector<InteractDef> interactables;
  GpuMesh opaque, glass;
  ShapePtr shape;                 // static collision
  vec3 bmin{0}, bmax{0};
  GLuint thumbnail = 0;
  json walkthrough;               // scripted traversal test (asset-local)
  json raw;
  bool placeholder = false;
  int findInteract(const std::string& id) const;
};

class AssetLibrary {
public:
  bool loadAll(const fs::path& dir);
  const AssetDef* find(const std::string& id) const;
  AssetDef* placeholder() { return &placeholder_; }
  std::vector<std::unique_ptr<AssetDef>> assets;
private:
  bool loadOne(const fs::path& p);
  AssetDef placeholder_;
};

void appendPart(MeshData& m, const Part& p, const Material& mat, const mat4& xf);
void buildTreeAsset(AssetDef& a, const json& tj);   // foliage.cpp
void buildPlaceholder(AssetDef& a);
