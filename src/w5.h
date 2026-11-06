#pragma once
// W5 default maps (MP & Creative update, SPEC 3): authored templates built by tools/mapgen.
// A template may carry
//   "heightmap": {file, nx, nz, min, max, org, cell}   16-bit PNG of base heights on the template's terrain grid
//   "waterLevel": metres                               sea / standing water surface
//   "map": "<id>.nmap.json"                            authored content (instances, scenario, routes) for New Map
//   "thumbnail": "<id>.thumb.png"                       pre-rendered New Map preview (fixed camera, --selftest-maps)
//   "playable": true                                    listed in the Play setup's default-map picker
// Hooks (one line each): Terrain::generate, App::newMap, App::makeThumbnails, MpUi::setupWindow, App::buildDrawLists.
#include "common.h"

struct TemplateInfo;
class Terrain;
class App;
struct AssetDef;
struct Instance;

void w5TerrainBase(Terrain& t, const TemplateInfo& ti);          // authored base heights + water level
void w5NewMapContent(App& app, const TemplateInfo& t);           // load the template's map content into the new doc
bool w5TemplateThumbnail(TemplateInfo& t);                       // true = preview loaded from the template's PNG
void w5PlayMapPicker(App& app);                                  // "Default maps" picker in the match setup window
bool w5CullDraw(const App& app, const Instance& i, const AssetDef* a);   // distance cull for small decorators
json w5MapSettings();                                            // content/settings/maps.json (cached)
