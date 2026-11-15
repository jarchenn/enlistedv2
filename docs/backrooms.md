# Backrooms (BR1)

A liminal interior, 216 x 216 m: familiar rooms made unsettling by emptiness and repetition. Generator:
`tools/mapgen/maps/backrooms.py` (seeded); kit: `tools/catalog_backrooms.mjs` (registered in `tools/build_mpu_assets.mjs`).
Original work in the style only: no logos, text or assets from any existing game or film. Reference photos (not in the
repo): `Documents/MapMaker/incoming/backrooms_refs`.

## Kit (`brm_*`)

Everything sits on an 8 m grid. A cell piece is an 8 x 8 m floor slab with the zone's ceiling over the whole cell, so
there is no sky anywhere; walls stand on the cell edges, 3.6 m high, so they close the step between two ceiling heights.

| Zone | Cells | Walls | Inside | Lights |
|---|---|---|---|---|
| yellow rooms | `brm_cell_yellow`, `_damp` (soaked carpet, a puddle, stained tiles): beige carpet, a 2.7 m suspended-tile ceiling | `brm_wall_y`, `_door` (doorless 1.3 m), `_wide` (4 m): faded yellow wallpaper (ambientCG Wallpaper002C tinted), brown baseboard, water stains | `brm_partition` (4 m), `_short`, `_l`, `_low`, `brm_column_y` | `brm_panel` (glow only), `brm_panel_lit` (cool white, 11 m), `brm_panel_dead` |
| service | `brm_cell_service`, `_cross`: stained concrete, pipe runs and a cable tray under a 3.2 m slab | `brm_wall_s*`: concrete, electrical boxes, conduit | `brm_column_s` | `brm_bulkhead` (warm, 9 m) |
| poolrooms | `brm_cell_pool`, `_flooded` (0.28 m of still turquoise water), `_basin` (a 6 x 6 m tiled basin 1 m deep, steps and rails) | `brm_wall_p*`, `brm_wall_p_arch`: pale square tiles | `brm_column_p` (round) | `brm_pool_light` (soft, 13 m) |

No light flickers. Lights hang from the ceiling (instance y = the ceiling height).

Dressing, built here from boxes and tilted inside the asset (instances only have a yaw):
- `brm_sunk_wardrobe_wall`, `brm_sunk_bed_wall`, `brm_sunk_chair_wall`: origin on a wall face, half inside the wall.
- `brm_sunk_wardrobe_floor`, `brm_sunk_table_floor`, `brm_sunk_armchair_floor`: leaning, half sunk into the floor.
- `brm_pile_chairs`, `brm_pile_wardrobes`: heaps at odd angles inside one collider.
- `brm_chair_lone`: a single chair.

The generator also sinks some existing `furn_armoire`, `furn_bed` and `furn_table_dining` 0.45 m into the floor.

## Layout

- 27 x 27 cells. Zones are seeded Voronoi regions: about 2/3 yellow rooms, the rest service areas and poolrooms. The
  spawn columns (west and east edges) are always yellow.
- Edges: a random spanning tree keeps every cell reachable. Tree edges are open, wide or doorways. The other edges are
  mostly open in the yellow rooms and poolrooms (open plan with loops), arches in the poolrooms, and mostly walls
  between zones and in the service areas.
- Nine long straight corridors (8-16 cells) cross the yellow rooms and service areas. They are walled along their
  sides, with a doorway wherever the tree crosses.
- Partitions: five fields of the same offset partition repeated over 4 x 3-4 cells, plus random partitions in about a
  third of the other yellow cells. Partitions sit at most 1.3 m off the cell centre and are at most 4 m long, so they
  never close a cell.
- Columns stand at grid corners where all four edges are open.
- Lights: two panels per yellow cell on a regular grid, one of them lit. Three dark stretches (15 m) of dead panels,
  and 6% dead elsewhere. One light per pool cell, a bulkhead in 3/4 of the service cells.
- Dressing (seeded): 26 pieces sunk into walls, 26 sunk into floors, 12 piles, 6 lone chairs, and 3 rooms full of
  identical chairs in rows with aisles. All are kept to cell corners or wall faces, off the centre lines that every
  doorway sits on.
- The map fixes its hour at night and lifts its own gamma (`"lighting": {"hour": 0, "gamma": 2.2}` in the map JSON: the
  tonemap raises the image to 1/2.2 in every preset, so 5 % grey comes out about 26 %). Lights near the light cap fade
  in and out over 12 m instead of popping (`src/lighting.cpp`).

## Play

- Assault: the Allies come in from the west rooms and the Axis hold the east. Objectives: the poolrooms, the pipe
  halls, the yellow rooms.
- Team Deathmatch: team A in the east, team B in the west.
- Solo: spawns chosen by the algorithm.
- A supply crate stands next to every spawn.

## BR2 look (user playtest)

- The map JSON carries `"look": {"clean": true, "fog": false}`, and Tunnels carries `"look": {"fog": false}`. `src/app.cpp`
  reads it into `R.lt.mapClean` / `mapNoFog`:
  - clean: no material wear (`interior.cpp`), static decals, dust motes or colour grade (the cool-shadow grade turned
    the yellow olive);
  - fog off: no aerial haze, weather fog or light shafts.
  Other maps are unchanged.
- Auto-furnishing stays on (the service cells furnish as rooms), but on `brm_*` hosts every piece with a light block or a
  candle / lantern / lamp / fire id is skipped (`interior.cpp`): the candles and room lamps were the flickering lights. Light
  comes only from the steady ceiling panels, pool lights and bulkheads.
- Ambient (user: 1.5x, no added lights): the map sets `"lighting": {"ambient": [0.09, 0.085, 0.06]}`, an even indirect term
  `render.cpp` adds to the fill, so furniture is lit as well as the glowing surfaces.
- The terrain sits 1 m under the floor slabs; at 0 it and its mud paint showed through the carpet.
- Yellow zone: saturated yellow walls in a striped paper (`content/textures/wallpaper_backrooms`, generated by
  `tools/gen_wallpaper_backrooms.py`, slot `wallpaper_backrooms`), plain yellow carpet, pale yellow 1.2 m ceiling tiles,
  square 1 m flush panels on a 4 m grid (one lit per cell, shadowed; 2% dead). Yellow surfaces glow faintly as a
  stand-in for bounce light at the fixed night hour (no GI). Thick 1.2 m pillars stand in open yellow cells. Floors are
  clean: no damp, puddles or stains.
- Pool and service zones: the same faint glow, brighter lights.

## BR3 (user playtest: brighter, glow, shadows, characters)

- `"lighting": {"exposure": 1.1}` pins the exposure (x the fixed night exposure) in play as in the editor (`render.cpp`):
  auto exposure was pulling the bright yellow down in play. The ambient is now [0.22, 0.2, 0.14]; it is the term
  that reaches characters, the first-person arms and the weapon, since they do not get the surfaces' glow.
- `"look": {"bloom": 2.5}` multiplies the bloom on this map. The panel diffusers are about 2.3x more emissive, so each
  lit panel carries a steady halo.
- `"look": {"shadows": 4}` asks for at least 4 shadowed lights here, which the lit panels are; it is never written to
  the user's settings.
- `--shots-backrooms` (`src/selftest_br3.cpp`) takes play-camera shots, with the match paused and a soldier and a
  zombie held in view: `NMK_BR3_SHOTS="name:x:z:tx:tz;..."` writes `selftest_out/br3_<name>.png`.
