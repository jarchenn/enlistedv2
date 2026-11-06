#pragma once
// Per-asset navigation metadata (AI_SPEC 3.1, 5.1, 8.1) and terrain cuts (SPEC 3.6 trenches / ditches / foxholes).
// Contract shared by the structure generators (tools/catalog_*.mjs write it), the navmesh baker and the bot AI (read
// it) and the terrain (applies terrainCut). All positions are asset-local metres (+Y up, front = +Z), like parts.
//
// JSON (asset file, all optional):
//   "nav": {
//     "effect": "block" | "carve" | "area" | "none",   // default: "auto" (enterable or > 1.6 m tall: block; else carve)
//     "area": "road" | "grass" | "brush" | "marsh" | "sand" | "trench" | "minefield" | "wire" | "water",
//     "areas":   [{"outline": [[x, z], ...], "area": "trench"}],        // sub-areas (e.g. a trench floor inside a bunker)
//     "rooms":   [{"id": "r1", "min": [x, y, z], "max": [x, y, z]}],     // room graph nodes
//     "doors":   [{"a": "r1", "b": "outside", "p": [x, y, z]}],          // openings linking rooms ("outside" = exterior)
//     "entrances": [{"p": [x, y, z], "dir": [x, y, z], "room": "r1"}],   // dir points into the room
//     "corners": [{"p": [x, y, z], "room": "r1"}],                       // clearing points
//     "firingPoints": [{"p": [x, y, z], "yaw": deg, "kind": "window" | "embrasure" | "firestep" | "mg"}],
//     "cover":   [{"p": [x, y, z], "yaw": deg, "height": "low" | "high", "peek": ["left", "right", "over"]}]
//   },
//   "terrainCut": [{"outline": [[x, z], ...], "depth": m, "bank": m}]    // lower the ground inside the outline by depth;
//                                                                         // bank = horizontal width of the sloped edge
#include "common.h"

enum class NavEffect : int { Auto = 0, Block, Carve, Area, None };
enum class NavArea : int { Default = 0, Road, Grass, Brush, Marsh, Sand, Trench, Minefield, Wire, Water, Count };
const char* navAreaName(NavArea a);
NavArea navAreaFrom(const std::string& s);
// AI_SPEC 3.1 cost multipliers; < 0 = forbidden (bots never path through it)
float navAreaCost(NavArea a);

struct NavAreaPoly { std::vector<vec2> outline; NavArea area = NavArea::Default; };
struct NavRoom { std::string id; vec3 min{0}, max{0}; };
struct NavDoor { std::string a, b; vec3 p{0}; };
struct NavEntrance { vec3 p{0}, dir{0, 0, 1}; std::string room; };
struct NavCorner { vec3 p{0}; std::string room; };
struct NavFiringPoint { vec3 p{0}; float yaw = 0; std::string kind = "window"; };
struct NavCoverPoint { vec3 p{0}; float yaw = 0; bool high = false, peekLeft = false, peekRight = false, peekOver = false; };
struct TerrainCut { std::vector<vec2> outline; float depth = 1.0f, bank = 0.3f; };

struct NavMeta {
  NavEffect effect = NavEffect::Auto;
  NavArea area = NavArea::Default;
  std::vector<NavAreaPoly> areas;
  std::vector<NavRoom> rooms;
  std::vector<NavDoor> doors;
  std::vector<NavEntrance> entrances;
  std::vector<NavCorner> corners;
  std::vector<NavFiringPoint> firingPoints;
  std::vector<NavCoverPoint> cover;
  std::vector<TerrainCut> terrainCuts;
  bool authored = false;            // any "nav" block present in the file
  bool hasAnnotations() const { return !rooms.empty() || !entrances.empty() || !firingPoints.empty() || !cover.empty(); }
};

// reads j["nav"] and j["terrainCut"] (missing = defaults)
void parseNavMeta(const json& assetJson, NavMeta& out);
json navMetaToJson(const NavMeta& m);   // tests / tools
// resolves Auto for an asset with these bounds
NavEffect resolveNavEffect(const NavMeta& m, bool enterable, vec3 bmin, vec3 bmax);
