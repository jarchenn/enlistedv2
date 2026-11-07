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
enum NatSpecies : int { NS_TALLMEADOW = 5, NS_POPPY = 6, NS_DAISY = 7, NS_BUTTERCUP = 8, NS_CORNFLOWER = 9, NS_COWPARSLEY = 10, NS_FOXGLOVE = 11, NS_CLOVER = 12 };
int natSpeciesFrom(const std::string& s);   // "poppy" .. ; -1 unknown

struct NatScatterPt { vec2 p{0}; float size = 0, r0 = 0, r1 = 0, r2 = 0, r3 = 0; int species = 0; };
struct NatPatchParams { float radius = 3, density = 4; uint32_t seed = 1; };

void buildNatureAsset(AssetDef& a, const json& j, std::vector<vec3>& pts);   // assets.cpp: "rock" / "scatter" blocks
void buildRockPiece(MeshData& m, const RockPiece& rp, vec3 tint, std::vector<vec3>* hull, std::vector<vec3>* tris = nullptr);
NatPatchParams naturePatchParams(const Instance& i, const AssetDef& a);
void setNaturePatchParams(Instance& i, const AssetDef& a, NatPatchParams p);   // clamped; bumps rev
void natureLayout(const NatureDef& n, const std::string& assetId, const NatPatchParams& pp, std::vector<NatScatterPt>& out);
uint64_t natureLayoutHash(const std::vector<NatScatterPt>& pts);

// runtime (App side)
void natureDrawItems(App& app, const Instance& i, InstanceRT& r, std::vector<DrawItem>& op, float camDist);
void natureDestroyRT(InstanceRT& r);
uint64_t naturePatchHash(const InstanceRT& r);   // hash of the built patch (plants: instances; stones: vertex positions)
size_t naturePatchItems(const InstanceRT& r);
extern int gNatRebuilds;   // patch rebuilds since start (tests / profiling)

// renderer: plant patches (progGrass), drawn after the pasture grass
#include "render.h"
void natureDrawPatches(GLuint prog, const std::vector<NatPatchDraw>& items, const mat4& vp, vec3 cam, float radius, float frac, float wind, int& draws, size_t& tufts);
