#pragma once
// Light assets, interior lighting and lamp placement (L3, docs/lights.md). The data side of local lights: which lights
// exist, where, how bright and whether they are on. The lighting engine (lighting.cpp, L2) calls lightsGather() each
// frame and renders the result; it also renders the "emissive" material field and scales it by lightsEmissiveScale().
//
// An asset carries its lights in a "light" block (one object, or an array of up to 16):
//   {"color": [r,g,b] linear, "intensity": 1 (= a 60 W bulb at 1 m), "radius": m, "cone": [inner, outer] deg (absent: point),
//    "dir": [x,y,z] asset space, "flicker": 0..1, "offset": [x,y,z] asset space, "group": prop group (index or name),
//    "shadow": 0/1, "use": "street" | "interior" | "electric" | "fire", "fx": "" | "flame" | "flame_smoke", "fxSize": m}
// A light goes out when its prop breaks (unless its group is a kept group), when its group is the prop's glass and the
// glass breaks, when a wall bay of the generated room it lights is breached, or when switched off (extra.light.on).
#include "light_types.h"
#include "assets.h"
#include <vector>
#include <string>

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
constexpr float kInteriorDay = 0.25f;

// ---- interiors: generated with the room templates (InteriorSys::furnish), no undo step
bool lightsRuinedAsset(const AssetDef* a);    // ruin_*, rub_*, vil_church_ruined, vil_chateau_keep, aw_r612_unfinished, bch_lcvp_wrecked
bool lightsOnlyHost(const AssetDef* a);       // bunkers / casemates: lit, built-in furniture kept, no room templates
bool lightsLitBuilding(const AssetDef* a);    // an intact building that gets interior lights (not rubble, not landing craft)
struct LightPlanItem { std::string slot, asset; vec3 p{0}; float yaw = 0; };   // host-local, like the room templates' items
// appends the lights of every room: one ceiling or table light by room type and area, a fire glow on a hearth;
// `items` holds the room template pieces already planned (tables and dressers carry the table lights)
void lightsPlanRooms(App& app, InteriorSys& sys, const Instance& host, const AssetDef* ha, const RoomInfo& ri, uint32_t seed, std::vector<LightPlanItem>& items);
bool lightsRoomBreached(App& app, uint64_t hostId, int room);   // a wall bay around the room is breached
bool lightsRoofed(App& app, const Instance& host);              // some room has a ceiling (open-topped Tobruk pits have none: no lamp)

// ---- per frame in Explore (InteriorSys::step): flame / smoke particles of lit fires near the camera
void lightsStep(App& app, float dt);
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

// ---- editor: the Light section of the Selection panel (colour, intensity, on / off)
void lightsEditorPanel(App& app, Instance& s);
