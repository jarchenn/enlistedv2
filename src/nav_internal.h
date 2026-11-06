#pragma once
// Shared between nav.cpp (tile pipeline) and nav_links.cpp (off-mesh link generation). Not for other files.
#include "nav.h"
#include "nav_geom.h"
#include <Recast.h>

// raw solid columns of a tile's heightfield (before ledge / low-height filtering), for link probing
struct NavColumns {
  struct Floor { float top, clear; bool walk, wire; };
  int w = 0, h = 0; float bx = 0, by = 0, bz = 0, cs = 0.16f, ch = 0.05f;
  std::vector<int> start;               // w*h + 1 offsets into floors
  std::vector<Floor> floors;            // bottom up per column
  void fromHeightfield(const rcHeightfield& hf);
  int col(float x, float z) const { int cx = (int)std::floor((x - bx) / cs), cz = (int)std::floor((z - bz) / cs); return cx < 0 || cz < 0 || cx >= w || cz >= h ? -1 : cx + cz * w; }
};

// links for one tile from its poly mesh boundary edges; ids = idBase + index
void navGenerateLinks(const NavConfig& c, const NavColumns& cols, const rcPolyMesh& pm, float tileMinX, float tileMinZ, float tileW,
                      uint32_t idBase, std::vector<NavLink>& out);
