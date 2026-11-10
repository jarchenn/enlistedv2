#pragma once
// Light assets, interior lighting and lamp placement (L3, docs/lights.md). The data side of local lights: which lights
// exist, where, how bright and whether they are on. The lighting engine (lighting.cpp, L2) calls lightsGather() each
// frame and renders the result; it also renders the "emissive" material field and scales it by lightsEmissiveScale().
//
// An asset carries its lights in a "light" block (one object, or an array of up to 16):
//   {"color": [r,g,b] linear, "intensity": 1 (= a 60 W bulb at 1 m), "radius": m, "cone": [inner, outer] deg (absent: point),
//    "dir": [x,y,z] asset space, "flicker": 0..1, "offset": [x,y,z] asset space, "group": prop group (index or name),
//    "shadow": 0/1, "use": "street" | "interior" | "electric" | "fire", "fx": "" | "flame" | "flame_smoke" | "smoke_column" (D-day: a tall drifting smoke column), "fxSize": m}
// A light goes out when its prop breaks (unless its group is a kept group), when its group is the prop's glass and the
// glass breaks, when a wall bay of the generated room it lights is breached, or when switched off (extra.light.on).
#include "light_types.h"
#include "assets.h"
#include <vector>
#include <string>
#include <functional>

class App;
class InteriorSys;
struct Instance;
struct InstanceRT;
struct RoomInfo;

struct LightSpec {
  vec3 color{1.0f, 0.75f, 0.45f}, offset{0}, dir{0, -1, 0};
  float intensity = 1.0f, radius = 6.0f, cosInner = -2.0f, cosOuter = -2.0f, flicker = 0.0f, fxSize = 0.2f;
  int group = -1, shadow = 0;
  std::string use = "interior", fx;
};
struct LightAssetDef { std::vector<LightSpec> lights; };
const LightAssetDef* lightDef(const AssetDef* a);   // cached per asset; null without a "light" block

// ---- per frame (L2's entry point): appends every lit light of the map (the caller clears `out` first)
void lightsGather(App& app, std::vector<LightDesc>& out);
// why a light of an instance is dark ("" when it is lit): "switched off", "broken", "glass broken", "room breached", "ruin"
std::string lightOffReason(App& app, const Instance& i, int k = 0);
// 0 dark .. 1 full: the instance's emissive (lamp glass, flames, embers) follows its first light (off, time of day)
float lightsEmissiveScale(App& app, const Instance& i);

// ---- time of day: daylight 1 noon .. 0 night. L2 calls lightsSetDaylight() from its time of day; a negative value (the
// default) derives it from the sun elevation (R.sunDir). Street lamps are off by day; interior lamps are dim by day
// (kInteriorDay of their output); fires and electric bunker lamps burn day and night.
void lightsSetDaylight(float d);
float lightsDaylight(App& app);
constexpr float kInteriorDay = 0.25f;   // the default of interior_lights.json "dayScale"
float lightsDayScale();                   // GX2: content/settings/interior_lights.json "dayScale"
// GX2: colour temperature (K) -> linear RGB, brightest channel 1 (blackbody fit, sRGB decoded)
vec3 kelvinToRgb(float kelvin);
// GX2: content/settings/interior_lights.json (fixtures, room rules, hallway spacing, sconces, dust motes); read once
const json& interiorLightsConfig();

// ---- interiors: generated with the room templates (InteriorSys::furnish), no undo step
bool lightsRuinedAsset(const AssetDef* a);    // ruin_*, rub_*, vil_church_ruined, vil_chateau_keep, aw_r612_unfinished, bch_lcvp_wrecked
bool lightsOnlyHost(const AssetDef* a);       // bunkers / casemates: lit, built-in furniture kept, no room templates
bool lightsLitBuilding(const AssetDef* a);    // an intact building that gets interior lights (not rubble, not landing craft)
struct LightPlanItem { std::string slot, asset; vec3 p{0}; float yaw = 0; };   // host-local, like the room templates' items
// GX2: places a wall fixture in room k on the wall in direction d (0: wall at -z, 1: -x, 2: +z, 3: +x of the piece) at a
// point along that wall, with the room planner's checks (doors, windows, routes, other pieces); fills `got` on success
using LightWallFn = std::function<bool(int k, const std::string& asset, const std::string& slot, int d, float along, LightPlanItem& got)>;
// appends the lights of every room (content/settings/interior_lights.json): a ceiling or table light by room type and
// area, pendants every 5-6 m between the windows of a hallway, wall lights beside the doorways of larger rooms, a fire
// glow on a hearth; `items` holds the room template pieces already planned (tables and dressers carry the table lights)
void lightsPlanRooms(App& app, InteriorSys& sys, const Instance& host, const AssetDef* ha, const RoomInfo& ri, uint32_t seed, std::vector<LightPlanItem>& items,
                     const LightWallFn& wallFix = {});
bool lightsRoomBreached(App& app, uint64_t hostId, int room);   // a wall bay around the room is breached
bool lightsRoofed(App& app, const Instance& host);              // some room has a ceiling (open-topped Tobruk pits have none: no lamp)

// ---- per frame in Explore (InteriorSys::step): flame / smoke particles of lit fires near the camera
void lightsStep(App& app, float dt);
// ---- GX2, every frame and mode: dust motes drifting in the sun shafts of windows near the camera (additive quads,
// stateless: positions are a function of time). Quality from the graphics settings (dustMotes 0 off, 1 low, 2 high).
struct FxVert;
int lightsMotes(App& app, const mat4& view, std::vector<FxVert>& add);
// ---- Apply damage to map: lights put out this session are saved off (extra.light {on: false, applied: true})
int lightsApplyToMap(App& app);
void lightsClearApplied(App& app);

// ---- placement: lamp posts along routes (auto paths, Beautify "street_lamps")
// polylines: [[x, z], ...] world; width: the route's width (m). Tags every lamp extra.auto = tag (autopathRemove removes
// "lamp"). Returns lamps placed.
struct LampLine { std::vector<vec2> pts; float width = 2.0f; };
int lightsLineRoutes(App& app, const std::vector<LampLine>& lines, const std::string& asset, float spacing, const std::string& tag);
std::vector<LampLine> lightsRoutesOf(const json& routes);   // MapDoc::extra["routes"]-style array -> polylines
std::vector<LampLine> lightsPathLines(App& app);            // every path instance's centre line plus the map's routes
extern bool gAutoPathLamps;                                  // Paths menu: line auto paths with lamp posts
extern std::string gLightsSkip;                              // GX2 A/B timing (capture): this asset's lights are left out

// ---- editor: the Light section of the Selection panel (colour, intensity, on / off)
void lightsEditorPanel(App& app, Instance& s);
