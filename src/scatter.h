#pragma once
// BF B2 / B3 ground scatter (docs/vfx-rev3/BATTLE-FOUNDRY-SPEC.md 4.5, 4.6; reference 03 s3.3, 04 s4.7):
//   stones (large 0.2-0.6 m to 60 m, small 0.03-0.12 m to 18 m: two-level Poisson clusters, sunk 25-45 %),
//   twigs (to 14 m), exposed roots near model trees (to 40 m), leaf-litter sheets (against walls, in hollows, on
//   lee sides; to the tier's litter range) and moss cushions (the moss field's G, 0-4 m, three LODs).
// Placement is a pure function of (map seed, 16 m chunk, candidate cell): a preset only filters by each candidate's
// stable seed (u < scatterMul), so changing presets never moves anything. Nothing lands above the angle of repose
// (34 deg stones / gravel, 40 deg rooted soil), in water, on paths or inside building footprints.
// A map opts in with "look": {"scatter": density 0..2} (or {"density": d, "moss": 0/1}); the perf switch is "scatter".
#include "common.h"
class App; class Renderer; struct RenderView; struct DrawItem;

void scatterFrame(App& app, std::vector<DrawItem>& op, vec3 eye);                   // chunks near the eye: meshes into op, contacts, cushions
void scatterRenderPass(Renderer& r, const RenderView& v, const mat4& vp);            // cushions and litter (after the grass)
struct ScatterStats { int chunks = 0, stones = 0, twigs = 0, roots = 0, litter = 0, cushions = 0, cushionsDrawn = 0, checkFails = 0; };
const ScatterStats& scatterStats();
int scatterRestingIn(vec2 lo, vec2 hi);   // stones, twigs, roots and litter sheets of the built chunks inside the xz box (VOT meter cue 5)
