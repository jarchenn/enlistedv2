#pragma once
// GM2 2D minimap (docs/minimap.md): a top-down map (terrain relief and water, structures, objectives, teammates, ammo
// crates, the player) drawn into any screen rectangle. One draw function serves the HUD corner map, the large map
// (M), the deploy screen (GM1: spawn markers as extra markers, clicks through the returned view) and the artillery
// call-in (a crosshair marker on the clicked spot).
#include "common.h"
#include <string>
#include <vector>

class App;
struct ImDrawList;

// layers drawn by drawMinimap
enum MinimapLayer : unsigned {
  MM_TERRAIN = 1, MM_STRUCTURES = 2, MM_OBJECTIVES = 4, MM_TEAMMATES = 8, MM_CRATES = 16, MM_PLAYER = 32, MM_SPAWNS = 64,
  MM_HUD = MM_TERRAIN | MM_STRUCTURES | MM_OBJECTIVES | MM_TEAMMATES | MM_CRATES | MM_PLAYER,
  MM_ALL = 0xFFFFu
};
// an extra marker (world x / z); shape: 0 dot, 1 square, 2 arrow along yaw (deg, 0 = -z, the map's up), 3 crosshair, 4 ring
struct MinimapMarker { vec2 pos{0}; uint32_t col = 0xFFFFFFFFu; int shape = 0; float size = 5; float yaw = 0; std::string label; };
// the mapping of one drawn map: screen rect p0..p1, world centre (x, z) at the rect centre, scale px per metre;
// screen x = world x, screen y = world z (north up = -z)
struct MinimapView {
  vec2 p0{0}, p1{0}, centre{0}; float scale = 1;
  vec2 toScreen(vec2 w) const { vec2 c = (p0 + p1) * 0.5f; return c + (w - centre) * scale; }
  vec2 toWorld(vec2 s) const { vec2 c = (p0 + p1) * 0.5f; return centre + (s - c) / scale; }
  bool contains(vec2 s) const { return s.x >= p0.x && s.y >= p0.y && s.x <= p1.x && s.y <= p1.y; }
};
// Draws the map into the screen rect p0..p1 of `dl`, clipped to it: centred on `centre` (world x, z), `metres` across
// the rect's shorter side (<= 0: the whole terrain fits); `layers` (MinimapLayer bits); `markers` drawn last.
MinimapView drawMinimap(App& app, ImDrawList* dl, vec2 p0, vec2 p1, vec2 centre, float metres, unsigned layers = MM_HUD,
                        const std::vector<MinimapMarker>& markers = {});
// the HUD: the small corner map (Explore, Settings > Gameplay "Minimap") and the large map while `large` (key M)
void minimapHud(App& app, bool large);
