#pragma once
#include "common.h"
#include "navmeta.h"
struct GpuModel;

// Surface pattern IDs understood by the main shader.
enum Pattern : int {
  PAT_PLAIN = 0, PAT_STONE = 1, PAT_WOOD = 2, PAT_PLASTER = 3, PAT_SLATE = 4, PAT_TILE = 5,
  PAT_METAL = 6, PAT_GLASS = 7, PAT_FABRIC = 8, PAT_BRICK = 9, PAT_PAINTED = 10, PAT_CONCRETE = 11,
  PAT_NET = 16,     // camouflage net with garnish (docs/bunkers.md#camouflage)
  PAT_BUNKER = 15,  // board-formed (12-14: path surfaces, src/paths.cpp) bunker concrete; grain = height of the roof lip in decimetres (run-off stains below it)
  // walls, interiors and furniture (docs/interiors.md)
  PAT_LIMEWASH = 17, PAT_BRICKWORK = 18, PAT_ASHLAR = 19, PAT_INTERIOR = 20, PAT_FLOORFIN = 21, PAT_RUBBLE = 22,
  PAT_RUG = 23, PAT_CERAMIC = 24, PAT_JOINERY = 25, PAT_PAINTWOOD = 26, PAT_PAPER = 27, PAT_TEXTILE = 28,
  PAT_ROCK = 29     // natural rock (docs/nature.md): grain = lithology 0 granite, 1 limestone, 2 schist; matches kTerrainFS's slope rock
};

struct Material {
  std::string name;
  vec3 albedo{0.6f};
  float roughness = 0.8f, metallic = 0.0f;
  int pattern = PAT_PLAIN;
  int grainAxis = 1; // part-local axis that wood grain / slate courses follow; -1 = the part's longest axis (furniture)
  vec3 emissive{0};  // "emissive": [r, g, b] linear HDR emission added before bloom (lamp glass, flames; docs/lighting.md)
  int texCode = 0;   // L4 texture layer (textures.h): 0 = the pattern's default slot, 1 = none, 2 + slot ("texture" field / textures.json)
};

// A primitive part. Boxes and triangular prisms, in a parent space.
struct Part {
  // ROUND: a box with every edge rounded by `rad` (render and collision). CYL / ARCH / WEDGE fill their bounding box
  // (center, size, rot): CYL = cylinder, cone, frustum or spire along local y; ARCH = arch ring, vault or spandrel
  // standing on the box's bottom face, extruded along local z; WEDGE = a slope falling toward local +z (docs/asset-format.md#b1-geometry-kit)
  enum Shape { BOX, PRISM, ROUND, CYL, ARCH, WEDGE } shape = BOX;
  vec3 center{0}, size{1};
  quat rot{1, 0, 0, 0};
  int mat = 0;
  bool visible = true, collide = true;
  bool playerOnly = false;   // "k": 2 - traversal volume only the player meets (barbed wire), ignored by rays and bodies
  int sec = -1;              // "sec": destruction section it belongs to (wire obstacles), -1 = always present
  // PRISM: triangle in (u,v) plane extruded along `axis` from a0..a1.
  int axis = 0; float a0 = 0, a1 = 0; vec2 tri[3]{};
  float rad = 0;   // ROUND: edge radius
  int grp = -1;    // "g": prop group (furniture pieces that break off together, docs/interiors.md)
  float bv = 0;    // BOX "bv": edge chamfer in the mesh only (the collider stays the box); stripped from wall bays
  int sides = 0;   // CYL "n": 0 = round (smooth normals), else an n-gon (4 square, 8 octagonal) with flat faces
  float top = 1;   // CYL "top": top section / bottom section (0 = a point: cone, spire); WEDGE "top": front height / height
  float th = 0.25f;  // ARCH "th": ring thickness (crown thickness when `fill`)
  bool fill = false; // ARCH "fill": the solid between the box's rectangle and the intrados (a spandrel over an opening)
  int seg = 0;     // CYL / ARCH "seg": curve segments, 0 = by size
  bool lodDetail = false;   // "lod": 1 - purely visual detail (trim, relief, dressing): dropped from the asset's LOD meshes (PF2)
};
// Convex pieces of a CYL / ARCH / WEDGE part in its local frame (relative to center, before rot): its colliders.
void partHulls(const Part& p, std::vector<std::vector<vec3>>& out);

struct Vertex {
  vec3 pos, nrm, albedo;
  vec4 params;  // pattern, roughness, metallic, grain axis
  vec3 pcoord;  // part-local position (for rotated-part patterns)
  vec3 pnrm;    // part-local normal
  vec3 emis{0}; // material emission (linear HDR, attribute 6)
};

struct MeshData { std::vector<Vertex> v; std::vector<uint32_t> i; };

struct GpuMesh {
  GLuint vao = 0, vbo = 0, ebo = 0; int count = 0;
  vec3 bmin{0}, bmax{0};   // mesh-space bounds of the uploaded vertices (frustum culling, src/frustum.h); empty mesh: 0, 0
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
  int trim = -1;   // >= 0: the trim of opening AssetDef::trims[trim] (frames, jambs, sills, lintels): never stamped, breaks whole
};

// The trim around one opening (a door or window interactable): frame, jambs, sills, lintels, hood moulds and threshold,
// bound to the wall bays around it at load (docs/destruction.md#trim). Asset-space opening rectangle in the wall plane.
struct TrimGroup {
  std::vector<int> interact;   // the opening's interactables (sash / door / shutters): they go with the frame
  int panel = -1;              // index of its trim bay in AssetDef::panels
  int an = 2;                  // wall normal axis
  float u0 = 0, u1 = 0, v0 = 0, v1 = 0;   // opening rectangle: u = horizontal in-plane axis, v = y
  float d0 = 0, d1 = 0;        // wall slab along the normal
  vec3 bmin{0}, bmax{0};       // all trim parts
  int parts = 0;
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
// B1 ivy on stone walls (src/ivy.cpp): "ivy": "auto" | {"auto": true, "density": d} |
// [{"face": "+z" | "-z" | "+x" | "-x", "a0", "a1" (along the wall), "y0", "y1", "density"}]
struct IvyDef {
  enum Mode { NONE, AUTO, BLOCKS } mode = NONE;
  float density = 1;
  struct Block { int axis = 2; float sign = 1, a0 = 0, a1 = 0, y0 = 0, y1 = 3, density = 1; };
  std::vector<Block> blocks;
};
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
  vec3 cardMin{0}, cardMax{0};                          // leaf card bounds (asset space, before wind sway): frustum culling
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

// Natural ground dressing (nature.cpp, docs/nature.md): procedural boulders / outcrops ("rock") and scatter patches
// ("scatter": flowers, tall grass, stones), one placed instance with a radius and density that scatters deterministically.
struct RockPiece { int lith = 1; uint32_t seed = 1; vec3 size{1}, pos{0}; float yaw = 0, tiltX = 0, tiltZ = 0, sink = 0.25f; int detail = 2; };
struct NatureDef {
  enum Kind { NONE = 0, ROCK = 1, SCATTER = 2 } kind = NONE;
  enum Scatter { FLOWERS = 1, TALLGRASS = 2, STONES = 3, WHEAT = 4 } scatter = FLOWERS;
  // V9 ground cover (nature.cpp): square tiles (radius = half side, no ragged rim), per-species colour overrides,
  // drying toward straw, a pebble layer under plants, wheat state (1 ripe, 2 trampled, 3 stubble) and its soil layer
  bool square = false;
  std::vector<std::pair<int, vec3>> tint;   // (species, albedo) overrides of the species' green
  float dryMin = 0, dryMax = 0;             // share of each plant's colour blended to straw (world-space noise between)
  float pebDensity = 0, pebMin = 0.03f, pebMax = 0.12f; int pebLith = -1;   // pebbles per m^2 under the plants
  int wheat = 0;
  vec3 soil{0.25f, 0.20f, 0.13f};           // WHEAT: soil / straw-litter layer albedo
  std::vector<RockPiece> pieces;           // ROCK
  bool collide = true;                     // ROCK: convex hull per piece, stops bullets, solid for the navmesh
  float radius = 3, density = 4, minR = 0.5f, maxR = 15, minD = 0.2f, maxD = 40;   // SCATTER: m, items per m^2
  std::vector<std::pair<int, float>> mix;  // SCATTER flowers / grass: (grass species id, weight)
  float sizeMin = 0.05f, sizeMax = 0.4f, sizePow = 2.2f;   // STONES: metres, power-law exponent (many small, few large)
  int lith = 1;                            // STONES: lithology
  float clump = 0.5f;                      // 0 even .. 1 strongly clumped
  float hMin = 1.0f, hMax = 1.0f;          // FLOWERS / TALLGRASS: height scale range (x the species heights, nature.cpp)
  std::vector<vec3> tris;                  // ROCK: CPU copy of the render triangles, asset space (collision tests)
};
struct AssetDef {
  StationDef station;
  TreeDef tree;
  IvyDef ivy;
  PathDef path;
  NatureDef nature;
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
  std::vector<TrimGroup> trims;  // opening trim bound to its bays (wallfield.cpp)
  GpuMesh panelsIntact;         // all panels intact, merged (fast path for undamaged copies)
  ShapePtr wallsIntact;          // shared compound of all intact panel shapes
  std::string id, name, category, description, file;
  int version = 1, floors = 1;
  bool enterable = false;
  vec2 footprint{4, 4};
  float plinthDepth = 1.0f, maxGrade = 1.0f;
  std::vector<Material> materials;
  std::vector<Part> parts;       // static parts incl. furniture
  std::vector<std::string> furnitureNames;
  size_t furnFirst = 0;           // parts[furnFirst..] are the built-in furniture groups
  GpuMesh noFurnOpaque, noFurnGlass; ShapePtr noFurnShape;   // the building without its built-in furniture (room templates)
  std::vector<InteractDef> interactables;
  GpuMesh opaque, glass;
  // PF2 asset LOD (docs/rendering.md#asset-lod): [0] LOD1 = the opaque mesh without built-in furniture, "lod": 1 parts and
  // parts under 0.35 m, bevels / rounding / curve segments simplified; [1] LOD2 = the main volumes only. count 0 = no
  // variant (the asset is too small to gain): the finer mesh is used. Walls (panels), glass and colliders have no LOD.
  GpuMesh lodMesh[2];
  // PF2 Phase B (hlod.cpp): CPU copies for HLOD cells, buildings / walls only: the LOD1 opaque geometry (or the full
  // static mesh) plus the intact wall panels, and the static glass. Null for everything else.
  std::shared_ptr<MeshData> hlodOpaque, hlodGlass;
  ShapePtr shape;                 // static collision
  vec3 bmin{0}, bmax{0};
  GLuint thumbnail = 0;
  json walkthrough;               // scripted traversal test (asset-local)
  NavMeta navMeta;                // "nav" + "terrainCut" (navmeta.h): navmesh effect, area, room graph, cover, cuts
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
