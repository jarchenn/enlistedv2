#pragma once
// Natural ground dressing (docs/nature.md): procedural boulders and outcrops, scatter patches (flowers, tall grass,
// stones / scree). Asset JSON blocks:
//   "rock":    {"pieces": [{"lith": "granite"|"limestone"|"schist", "seed": n, "size": [x, y, z], "pos": [x, y, z],
//               "yaw": deg, "tilt": [x, z], "sink": f}], "collide": true}
//              Noise-displaced, plane-cut icosphere hulls with a flat base sunk `sink` x height into the ground. Each
//              piece gets its own convex hull collider (stops rounds, solid for the navmesh); shading is pattern 29.
//   "scatter": {"kind": "flowers"|"tallgrass"|"stones", "radius": m, "density": per m^2, "mix": {"poppy": w, ...},
//               "clump": 0..1, "size": [min, max], "sizePow": p, "lith": "limestone"|"mixed", "heightScale": [a, b]}
//              One placed instance scatters items in a disc of `radius` (per-instance Instance::extra["scatter"] =
//              {radius, density, seed} override): dart-throwing (Poisson disc) in a random order, never on a grid, a
//              pure function of (asset, radius, density, seed) - the placement is moved / turned as a whole. Plants
//              draw through the grass shader (species 5-12, shared wind, distance fade and LOD prefix); stones are
//              one merged mesh per patch. Neither collides nor touches the navmesh.
#include "assets.h"
#include "grass.h"

class App; struct Instance; struct InstanceRT; struct DrawItem;

// grass shader species added for patches (grass.h has 0-4)
// 0-4 are the pasture species (grass.h) reused in patches with their own heights / colours; 13-15 are V9 ground cover
enum NatSpecies : int { NS_TURF = 0, NS_MEADOWGRASS = 1, NS_SEEDGRASS = 2, NS_WEEDS = 3, NS_STRAW = 4,
                        NS_TALLMEADOW = 5, NS_POPPY = 6, NS_DAISY = 7, NS_BUTTERCUP = 8, NS_CORNFLOWER = 9, NS_COWPARSLEY = 10, NS_FOXGLOVE = 11, NS_CLOVER = 12,
                        NS_WHEAT = 13, NS_BRUSH = 14, NS_STUBBLE = 15 };
int natSpeciesFrom(const std::string& s);   // "poppy" .. ; -1 unknown

struct NatScatterPt { vec2 p{0}; float size = 0, r0 = 0, r1 = 0, r2 = 0, r3 = 0; int species = -1; };   // species -1: the patch default
struct NatPatchParams { float radius = 3, density = 4; uint32_t seed = 1; };

void buildNatureAsset(AssetDef& a, const json& j, std::vector<vec3>& pts);   // assets.cpp: "rock" / "scatter" blocks
void buildRockPiece(MeshData& m, const RockPiece& rp, vec3 tint, std::vector<vec3>* hull, std::vector<vec3>* tris = nullptr);
NatPatchParams naturePatchParams(const Instance& i, const AssetDef& a);
void setNaturePatchParams(Instance& i, const AssetDef& a, NatPatchParams p);   // clamped; bumps rev
void natureLayout(const NatureDef& n, const std::string& assetId, const NatPatchParams& pp, std::vector<NatScatterPt>& out);
uint64_t natureLayoutHash(const std::vector<NatScatterPt>& pts);
// MA3 foundation dressing (foundation.cpp): a plant in the patch instance format (species, world root, linear albedo,
// height, four 0..1 randoms) and a small stone appended to a world-space mesh (lith 0 granite, 1 limestone, 2 schist)
GrassInstance naturePlant(int sp, vec3 w, vec3 albedo, float h, float r0, float r1, float r2, float r3);
void natureStone(MeshData& m, vec3 w, vec3 groundNrm, float size, float r0, float r1, float r2, float r3, int lith);

// runtime (App side)
void natureDrawItems(App& app, const Instance& i, InstanceRT& r, std::vector<DrawItem>& op, float camDist);
void natureDestroyRT(InstanceRT& r);
uint64_t naturePatchHash(const InstanceRT& r);   // hash of the built patch (plants: instances; stones: vertex positions)
size_t naturePatchItems(const InstanceRT& r);
extern int gNatRebuilds;   // patch rebuilds since start (tests / profiling)

// ---- wheat flattening (V9): tanks driving through and explosions lay the stalks down (session state, like felled
// trees; cleared by natureResetFlatten from App::resetDamage). Stamps are world-space; every wheat patch applies the
// stamps that overlap it to its instance buffer incrementally, and again after a rebuild.
void natureBlast(App& app, vec3 p, float dmg, float radius);              // App::blastPaths (paths.cpp)
void natureTrample(App& app, vec2 c, float yaw, vec2 half, float strength = 1.0f);   // a hull / track footprint (yaw deg)
void natureStepFlatten(App& app);   // once per frame (from natureDrawItems): stamps for tanks moving through wheat
void natureResetFlatten();
size_t natureFlattenStamps();
float naturePatchFlatShare(const InstanceRT& r, float atLeast = 0.5f);   // share of the patch's wheat stalks at least this flat
// pasture-grass footprint for a wheat tile (grass does not grow under a crop; other patches ignore it): false = none
bool natureGrassFootprint(const Instance& i, const AssetDef& a, GrassFootprint& out);

// renderer: plant patches (progGrass), drawn after the pasture grass
#include "render.h"
void natureDrawPatches(GLuint prog, const std::vector<NatPatchDraw>& items, const mat4& vp, vec3 cam, float radius, float frac, float wind, int& draws, size_t& tufts);
