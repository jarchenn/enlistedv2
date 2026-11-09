# Graphics overhaul: changes per workstream

## GX3: grass and ground cover (brief §4), branch `gx3-grass`

**Done**
- **Clumps instead of tufts.** Each instance is a clump of 12 crossed cards: tall in the middle, shorter and leaning
  outward at the rim, so neighbouring clumps overlap into a sward. Cards per m² drawn within 15 m on Medium:
  284, against about 73 blades per m² before (16 of 40 tufts × 7 blades × ~65 % kept), about 3.9x.
- **Species layers.** There are 7 layers, each placed by its own noise mask and drawn with its own procedural card
  texture:
  - fine turf;
  - meadow grass (the dominant layer);
  - seed-head grass, with spikes or open panicles;
  - clover, as trifoliate leaves with a pale chevron;
  - plantain-type broadleaf rosettes;
  - small leafy shrubs, more of them near walls;
  - dry straw.

  The textures are procedural, so no new third-party asset was needed.
- **Colour.**
  - Each clump gets a tint along green → yellow-green → straw.
  - Each card varies in value, and some cards are yellower.
  - Each blade has a vertical gradient: a darker, saturated base and a lighter, drier tip.
  - The lower 30 % of each blade blends into the ground colour under the clump. That colour is a CPU port of the
    terrain palette, including bare soil, wear, craters and painted ground.
- **Shading.**
  - Base AO is darker inside the clump and under the canopy.
  - Backlit cards glow (translucency).
  - Clumps within the shadow radius (10 m on Medium) cast sun shadows into the near cascade (`Grass::drawShadow`).
- **Placement.**
  - Clumps lean with the terrain normal and sit on the slope plane.
  - The sward thins toward the 0.8 slope cut-off.
  - Bare-earth patches follow the terrain's `bare` field, which now shows 65 % soil instead of 30 %.
  - Painted paths and ground thin the sward and colour the clump bases.
  - Grass grows back from footprint edges over 2–3 m, short first, instead of stopping at a wall of tall weeds.
- **Ground.** The near-field grass ground layer is now ambientCG Ground037 (`grass_forest`: moss, litter and soil,
  albedo + normal + AO, CC0). It was already in `content/textures`. It fades to the meadow palette with distance, which
  is the "tinted ground" beyond the grass.
- **LOD without pops.** Clumps are generated at 44 per m² in a shuffled order. Beyond the near radius:
  - the drawn share falls as (near / d)^falloff;
  - clumps keep fewer cards, 6 then 3 on Medium, and the remaining cards widen.

  Each clump and card fades by its rank at its own distance, in the vertex shader, so there is no per-chunk pop.
  Medium's radius changes from 58 m to 48 m.
- **Wind.** Wind is a vertex-shader effect only within 13 m (Medium) or 15 m (High); Low has none. Distant clumps keep a
  static lean. There is a new setting, Settings → Graphics → Grass wind (`grassWind`), and the Low preset turns it off.
- **Suppression.** Grass reads GX1's earthwork and trench soil (`sdirt`, through `dirtAt`), slopes, ground paint, wear,
  craters and footprints.
- **Tiers.** `content/settings/grass.json` holds the tunables named in the brief: density, near / mid / draw radius,
  falloff, cards per LOD, wind radius and shadow radius. Low is about the pre-GX3 Medium cost.
- **Test field.** `NMK_CAPTURE_SET=grass --capture-materials <tag>` renders a 40 × 40 m pasture with a dirt path, a dry
  patch and a bunker from six cameras. Each camera logs GPU ms with grass on and off, blades drawn, cards per m² within
  15 m, and clumps in the sun map.

**Measured** (RTX 2080 Ti, Medium, 1920 × 1080; before and after run interleaved in one exclusive GPU slot)

GPU ms per camera. The GPU was shared with other sessions' runs even inside the exclusive slot, so only the fastest
of several interleaved samples is a clean reading; later runs (below) were 3-7x slower on the same cameras.

| Camera (gx-base vs GX3, Medium) | before | after | change |
|---|---|---|---|
| current_04 lineup | 7.54 | 8.70 | +1.2 ms |
| current_05 field (acceptance) | 5.70 | 6.92 | +1.2 ms (+21 %) |
| current_06 bunker | 9.20 | 9.71 | +0.5 ms |
| current_01 / 02 / 03 / 07 (no grass in view or little) | 20.4 / 18.6 / 11.1 / 11.3 | 20.8 / 19.1 / 11.4 / 12.4 | +0.3 to +1.1 ms |

- **Medium is over the +10 % guardrail on the field camera** (+21 %, +1.2 ms on a 5.7 ms frame). The grass-on / grass-off
  cost inside the grass test field is about the same before and after (1.5 ms vs 1.7 ms mean), so most of the field
  delta is the denser sward being rasterised at the field camera's longer view.
- Low is the cheap tier: in the noisy tier-tuning run (Low's numbers as Medium) it sat within ~1 ms of the gx-base on every camera.
- Levers if Medium must come down, all in `content/settings/grass.json` with no rebuild: `density` (30 -> 24),
  `radius` (54 -> 48), `shadowRadius` (10 -> 0). Shadows alone showed no measurable cost.
- A vertex early-out for clumps and cards past their LOD cut was added after the A/B and has not been timed.
- Placement build cost: full Medium build 260 ms (a 40 m field), crater rebuild 15 ms for 4 chunks, with the broad
  noise sampled on a per-chunk 1 m lattice (without it the rebuild was 30 ms).
- battle1 gameplay views (`--selftest-maps`, same slot, before -> after): A1_spawn 14.7 -> 13.8 ms, obj_A_eye 26.2 -> 23.0,
  obj_B_eye 35.2 -> 31.3, obj_C_eye 23.7 -> 19.7, obj_D_eye 21.2 -> 17.6. battle1 has mostly short-sward and nature
  grass, and these runs carried outside GPU load that differed between the two builds, so read them as "no
  regression", not as a speed-up.

**Deferred / limits**
- The cards are procedural silhouettes, not photographed alpha cards. ambientCG has no grass-blade cutout sets, and the
  brief requires asking before adding a new asset pack.
- Grass casts into the near sun cascade only, at about 2 cm per texel. Single blades shadow faintly, so most of the
  "internal shadowing" comes from the canopy AO and the darker clump bases.
- The bunker berm footprint still clears a band of grass around the berm box. GX1's earthwork stamps replace the berm
  boxes, and the grass follows GX1's stamp soil.

**Files**
- `src/grass.{h,cpp}`
- the grass shaders in `src/render.cpp` (`kGrassVS` / `kGrassFS`, the new `kGrassShadowFS`), plus:
  - the grass shadow call in the near cascade;
  - the nature patch tier table;
  - one line in `kTerrainFS` (the bare-soil blend).
- `src/nature.cpp` (the plant mesh keeps 7 blades)
- `src/ground_paint.{h,cpp}` (`groundPaintTint`)
- `src/settings.{h,cpp}` (Grass wind)
- `src/capture.cpp` (the grass set)
- `src/selftest_grass.cpp` (species, density and tier checks)
- `content/settings/grass.json`
- `content/textures/textures.json` (`terrain_grass`)
- `docs/ground-grass.md`, `docs/settings.md`
