// PF2 Phase B: hierarchical LOD for far static buildings (docs/rendering.md#hlod). Buildings and walls are grouped into
// 128 m ground cells; a cell whose every point is in the LOD2 range is drawn as one merged world-space mesh (their LOD2
// geometry and wall panels, each copy's palette / wear seed baked into vertex attribute 7) plus one merged glass mesh,
// instead of 3-5 draws per building in every pass. A cell rebuilds when a member is added, removed, moved or damaged.
#pragma once
#include "render.h"
#include <vector>

class App; struct Instance; struct InstanceRT;

void hlodFrame(App& app);                                         // buildDrawLists start: membership, rebuilds, active cells
bool hlodSkip(const Instance& i);                                 // the copy is drawn by its active cell this frame
void hlodDraw(std::vector<DrawItem>& op, std::vector<DrawItem>& gl);   // buildDrawLists end: the active cells
struct HlodStats { int cells = 0, active = 0, membersSkipped = 0, rebuilt = 0; double rebuildMs = 0; };
const HlodStats& hlodStats();
