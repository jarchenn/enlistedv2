# Graphics overhaul: changes per workstream

## GX2: interior lighting and dressing (brief sections 2 and 3), branch `gx2-interiors`

### Done
**Lighting (section 2)**: `content/settings/interior_lights.json`, `src/lights.cpp`:
- **Config.** Every lamp's colour temperature is set in Kelvin (2700-3200 K), converted by a blackbody fit to linear
  RGB. The file also sets intensity, radius, the daytime dimming and each room type's ceiling and table lights.
  Falloff stays inverse-square (L2).
- **Hallway pendants.** Halls at least 4.5 m long get a pendant every 5-6 m along the centre line. A pendant that
  would hang within 0.7 m of a window's centre line moves to the midpoint between two windows, so light pools
  alternate with the window light.
- **Wall lights by doorways.** Rooms of 16 m2 or more, and every hall, get wall lights either side of their
  doorways: electric sconces in town buildings, candle sconces or wall oil lamps in rural ones. They are placed
  through the room planner, so they keep clear of doors, windows and walking routes.
- **Long corridors are halls now.** A room at most 3 m wide and at least 3.5 times as long counts as a hall. The
  chateau's 2.2 x 36 m rear corridors used to be typed as sitting rooms, with one lamp each.
- **New fixtures**, each a mesh with an emissive part and a light:
  - wall sconce with a frosted tulip shade;
  - brass candle sconce;
  - kerosene wall lamp with a reflector;
  - tall candlestick;
  - hall pendant with an opal-glass bowl.
- **Dust motes** drift in the sun shafts of the nearest sunlit windows. The window is checked by a ray toward the
  sun, the particles have no stored state, and they show in every mode. A new graphics setting controls them:
  "Dust motes" is Off on Low, Low on Medium (18 per window) and High on High (40).
- **Window shadows.** Indoors, the near sun cascade tightens from 40 m to 24 m, about 1.2 cm per texel at 4096,
  so window bars cast crisp shadows on the floor.

**Dressing (section 3)**: `content/settings/room_dressing.json`, `src/interior.cpp` `Planner`:
- **The recipes are data now.** The hard-coded recipes of the 12 room types moved into the JSON as step lists. The
  file adds:
  - `clusters`: an anchor piece plus satellites, with seeded jitter, flush placement, pieces on top and a piece
    hung above it;
  - `zones`: groupings for oversized rooms;
  - `wall`: wall items per metre and the longest bare wall allowed;
  - rug, curtain (open or half-drawn) and debris chances.
- **Two new room types**, detected automatically:
  - attic: headroom under 2.25 m somewhere under the roof;
  - cellar: floor below -0.8 m.
- **Oversized rooms** (more than 8 m a side; halls, barns, naves, taverns and classrooms excepted) get one cluster
  grouping per 4-5 m zone.
- **Hallways** get:
  - a runner rug tiled down the middle;
  - side tables with lamps;
  - pictures every 3-4 m on both long walls;
  - a coat stand or coat hooks by the outside door.
- **Wall fill.** After a room's recipe has run, its longest bare wall is filled with a picture, mirror, shelf, pan
  rail, coat hooks or sconce, repeatedly, until no bare run is longer than 4 m. A floor piece is used where nothing
  can hang. Frames hang with their centre at 1.5-1.7 m.
- **Floor pieces** stand 3 cm off the wall, on the front of the new skirting board.
- **Deterministic.** Every pick is seeded by the building's furnishing seed and the slot name, so every client
  dresses a building the same way.
- **Stats.** `InteriorSys::lastStats` and the `interior: dressed` log line give each room's pieces, wall-mounted
  pieces, lights and longest bare wall. `NMK_GX2_DEBUG=1` logs why a wall could not be filled.

**New props** (`tools/gen_furniture.py`, 21 pieces): each carries a `prop.bounds` envelope computed from its rotated
parts, and none goes over 75 parts.
- Furniture: iron bedstead (spindled head and foot, brass knobs, a blanket draped in folds, a pillow), gramophone
  with a fluted brass horn, cathedral-top wireless set, bentwood coat stand with coats, a hat and an umbrella, coat
  hooks with a greatcoat, jacket and scarf, stacked leather suitcases.
- Fittings: half-drawn curtains, a pan rail with skillets.
- Floor: broken glass, plaster debris with lath and a brick, a 3.2 m hall runner.
- Paintings: five framed paintings (landscape, seascape, evening, portrait, small).

**Trim geometry** (`tools/parts.mjs` `interiorTrim`):
- **What it adds.** Real skirting boards (board and moulded top, 2 cm proud), a dado-rail lip at 0.955 m that
  matches the shader's wainscot cap, and jamb liners plus architraves round partition doorways the builder had
  not framed.
- **Where.** On every interior plaster face with a floor along it, stopping at doors and at windows whose sill is
  below the rail.
- **Visual only** (`k: 0`), placed in the same part lists as the plaster, so the trim breaks with its wall bay.
  Colliders, panels and nav are unchanged.
- **Applied by** B2 `dress()` (13 buildings at version 3) and B3 `house()` / `gatehouse()` (11 buildings at
  version 2).

### Numbers
(filled in below from the captures and suites)

### Deferred, and engine limits
- **The chateau's 10 x 18.6 m rooms keep their volume.** They are furnished as zones rather than split by new
  partitions. More rooms would pass the 16-room cap of the per-room wall finish (`SurfaceParams`) and change the
  B3 tours and nav.
- **Baked lighting.** Static fixtures are dynamic clustered lights; there is no lightmap baker. The probe GI (L2)
  provides the bounce light.
- **GTAO, decals and material layering** belong to GX5.
