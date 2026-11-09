#pragma once
// GX1 terrain conformance (docs/graphics-overhaul.md#gx1-terrain-conformance; content/settings/terrain_blend.json):
// the ground conforms to what is placed on it. Everything is derived from MapDoc::instances (no map data, no
// replication change), so every client computes the same ground:
//   - pinned objects (buildings, bunkers, emplacements) get a levelled pad with a smoothstep blend ring and, past
//     the raise cap, a stone retaining skirt; reforms (brush, lasso, generators) re-conform the ground to them;
//   - earth-covered bunkers ("earthwork" asset block) get their banks as terrain: repose profile, noise, smoothing;
//   - trench pieces ("trench" asset block) joined at their sockets form one network: a carved trench with spoil
//     parapets, revetment generated along the cut, flush with the soil.
#include "app.h"

struct BlendConfig {
  bool enabled = true;
  float margin = 1.5f, ringMin = 2.0f, ringMax = 3.5f, ringPerMetre = 1.5f, minDelta = 0.05f, skirtThreshold = 0.9f;
  bool revetment = true, sandbags = true; float pickets = 0.75f;
  StampParams sp;
};
const BlendConfig& blendConfig();               // content/settings/terrain_blend.json (read once)
bool blendEnabled();

bool blendPinned(const AssetDef* a);            // gets a pad: buildings, bunkers, emplacements with a footprint
bool blendTrench(const AssetDef* a);            // a trench piece ("trench" block)
// asset loading: true when part pj is replaced by generated terrain / revetment ("trench" / "earthwork" replace)
bool blendReplacesPart(const json& asset, const json& pj);
// navSyncCuts: the stamps for the current instances (pads, berms, trench networks); returns the tiles touched
int blendSyncStamps(App& app);
// checkPlacement for a pinned object: the auto-level height (median of the footprint) or a refusal with the variance
bool blendPlacement(const App& app, const AssetDef* a, vec3 pos, float yaw, uint64_t ignoreId, float& y, std::string& reason);
// per-instance generated meshes (revetment, pickets, sandbags; retaining skirts) for the draw list
void blendDrawItems(App& app, const Instance& i, InstanceRT& r, const mat4& M, std::vector<DrawItem>& op);
// Place tool preview: the pad outline at its level, the blend ring on the ground, colour by raise / cut
void blendPreview(const App& app, Overlay& ov, const AssetDef* a, vec3 pos, float yaw);
// validation after a reform: pinned footprints vs their anchor, loose props vs the ground. Floating > 5 cm or
// buried > 10 cm is logged; with fix, loose props re-seat (pinned objects never move: the ground conforms to them).
struct BlendIssues { int pinned = 0, floating = 0, buried = 0, fixed = 0; float worst = 0; };
BlendIssues blendValidate(App& app, bool fix, const char* why);
