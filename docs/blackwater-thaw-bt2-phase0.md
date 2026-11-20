# Blackwater Thaw BT2, Phase 0: diagnostics

Baseline: bt1 at 054eced3 (integ-dday 62fce946 + the dead-tree fix), Medium and High, 1920 x 1080 captures (the maps
self-test window on the 2560 x 1440 desktop), map hour 15.3, FOV the game's default, cameras = the six bookmarks in
`blackwater_thaw.nmap.json` (editorCamera.bookmarks) plus the template views. Evidence files are in `E:\nmk\bt1\p0\` and
`E:\nmk\bt1\bt2_trees2\` (not in the repo).

## 1. The straight grey band on the horizon: terrain, the boundary I built

- Rays at eye height, 0-3 deg above level, across +-30 deg of each view, marched through the baked ground (heightmap +
  sculpt; `horizon_trace.py`): in the four outward-facing views the first hit just above the horizon is terrain **0.7-2.3 m
  outside the playable envelope on a 76-80 deg face**: the 7-17 m rock face that rings the envelope (milestone 1/4). In the
  creek-bend and wall-breach views rays also hit a **60-72 deg face 77 m inside the envelope**: the 9 m retaining face under
  the washhouse terrace.
- Fog, clouds, cloud shadow and grade off (`--rq-off wx1,grade`): the straight curtain stays in five of six views
  (`fogoff_montage.jpg`), so it is geometry, not a fog or far-clip discontinuity. Not a model or LOD: the ray hits are the
  heightfield; the rock-face models only clad patches of it.
- The face also shows vertical texture streaks: ground textures stretched down a near-vertical terrain face
  (`firs_medium_vs_high.png`).
- Fix (Phase 1): replace the sheer ring with a graded apron and real ridge layers beyond the envelope; keep the playable
  limit as a separate collision line hidden in trees and rock, not as visible terrain.

## 2. Black dots on the snow roofs: the slate seams' cavity term under the snow layer

- They are short dark dashes along the slate courses, curving in a moire, not round pinholes (`roof_normal_vs_fogoff.png`).
- Stable: the flicker probe (`NMK_FL1`, two still frames 0.3 s apart, then 5 cm sideways) finds no flickering cell on any
  roof; flicker everywhere is under 0.7 % of pixels (`fl1.json`). So not LOD dithering, depth overlap or shadow aliasing.
- Bound to the roof: the slate material darkens its seams (relief AO / cavity); the snow layer (SN1) replaces albedo and
  flattens the normal but leaves that occlusion, so the seams show through the snow as dark dashes and alias into a moire
  at distance. With the weather and grade off they mostly wash out (brighter, harder light).
- Fix (Phase 1): snow cover also suppresses the material's cavity / relief occlusion, and deposits follow slope, shelter
  and seams.

## 3. Distant vegetation shimmer: fir crowns losing alpha coverage

- The far firs read as sparse specks at both Medium and High (same frame, `firs_medium_vs_high.png`): the needle cards'
  alpha coverage thins in the small mips, so a crown breaks into dots instead of a mass. Medium and High do not differ, so
  it is not the preset's density.
- The near grass is stable in the flicker probe.
- Fix (Phase 1): group the backdrop firs into massed stands and darker forest slopes (fewer, larger, overlapping crowns),
  before any instance-count increase.

## 4. Linear ramp

- Colour textures are sRGB formats (`GL_SRGB8_ALPHA8` / DXT5 sRGB: hardware decode); data maps (normal, roughness, the
  snow mask) are linear. Lighting, fog and the snow layer run in linear HDR. The tone pass encodes sRGB once (`toSRGB`)
  into a framebuffer without `GL_FRAMEBUFFER_SRGB`: no double encode. The only other gamma step (`pow 1/2.2`) is the
  separate model-sheet path, not gameplay.
- No defect found here. (A rendered grey-ramp swatch is still to do: Phase 1's neutral material swatch.)

## Other evidence

- Greyscale of the six cameras (`grayscale_montage.jpg`): mid-grey dominates; near objects do not hold the strongest
  local contrast (critique test 2 fails as described).
- Snowfall streaks still show with the weather off (they are their own pass).
