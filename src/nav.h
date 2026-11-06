#pragma once
// Layered walk grid for bots, sampled from the live physics world (terrain, buildings, stairs, defences).
// Each 0.5 m column holds up to kLayers floors (downward all-hit rays, then an upward headroom ray per floor). Links to
// the 8 neighbours follow step height (+0.45 / -0.6 m) and are cleared by knee / chest rays near structures; a link
// blocked only by a movable door leaf is kept as a DOOR link (bots open the door with the same interaction as the
// player). Breaches and placed defences re-sample only the affected columns (rebuildArea).
#include "common.h"

class Nav {
public:
  static constexpr int kLayers = 4;
  static constexpr float kCell = 0.5f;
  enum Flags : uint8_t { N_USED = 1, N_CROUCH = 2, N_STRUCT = 4, N_WALL = 8 };
  enum LinkBits : int8_t { L_NONE = -1 };
  struct Slot { float y = 0; uint16_t head = 0; uint8_t flags = 0; uint8_t doorMask = 0; int8_t link[8]; };
  vec2 origin{0}; int nx = 0, nz = 0;
  std::vector<Slot> slots;                          // (col * kLayers + layer)
  double buildMs = 0; int usedNodes = 0, links = 0, doorLinks = 0, rebuilds = 0;
  bool valid() const { return nx > 0; }

  void build(vec3 lo, vec3 hi, int threads = 0);
  void rebuildArea(vec3 lo, vec3 hi);
  int nearest(vec3 p, float maxDist = 2.0f) const;  // closest used slot (vertical preference), -1 none
  vec3 pos(int s) const { int c = s / kLayers; return vec3(origin.x + (c % nx + 0.5f) * kCell, slots[s].y, origin.y + (c / nx + 0.5f) * kCell); }
  bool used(int s) const { return s >= 0 && s < (int)slots.size() && (slots[s].flags & N_USED); }
  int neighbour(int s, int dir) const;              // linked slot in direction 0..7 or -1
  bool doorLink(int s, int dir) const { return (slots[s].doorMask >> dir) & 1; }
  // A*: slot path from a to b (inclusive). maxExpand bounds the work; returns false if unreachable / budget hit.
  bool path(int a, int b, std::vector<int>& out, int maxExpand = 80000, float* cost = nullptr) const;
  bool reachable(int a, int b, int maxExpand = 120000) const { std::vector<int> t; return path(a, b, t, maxExpand); }
  // straight walk check between two points (knee + chest rays, leaves count as blocking unless allowDoors)
  static bool walkLine(vec3 a, vec3 b, bool allowDoors = false);
  int column(int s) const { return s / kLayers; }
  static const int kDX[8], kDZ[8];
private:
  void sampleColumn(int cx, int cz);
  void linkColumn(int cx, int cz);
  mutable std::vector<float> gScore; mutable std::vector<uint32_t> stamp; mutable uint32_t curStamp = 0;
  mutable std::vector<int> parent;
};
