# Tunnels (UG1)

An underground labyrinth, 216 x 216 m (2/3 of D-day's area), on a flat, unseen ground. Generator:
`tools/mapgen/maps/tunnels.py` (seeded); kit: `tools/catalog_tunnels.mjs` (registered in `tools/build_mpu_assets.mjs`).

## Tunnel kit (`tun_*`)

Every piece is an 8 m cell, a closed shell (floor, rock / concrete walls, ceiling) whose edge openings are all the same
portal (2.5 m wide, 2.6 m high, centred on the cell edge), so any two pieces snap (`snap` sockets, kind `tunnel`).
Passages are 2.2-3 m wide and 2.75-3 m high, with kinks, bulges and niches; colliders, nav rooms / entrances and cover
come with each asset.

| Piece | Sides | Notes |
|---|---|---|
| `tun_end` | S | dead end |
| `tun_straight`, `_kink`, `_niche`, `_lined` | N S | plain, bent, a niche, concrete-lined |
| `tun_wide` | N S | widening, 3.0 m high |
| `tun_crawl` | N S | squeeze 1.5 m wide x 1.7 m high: no heavies (loop links only) |
| `tun_corner`, `tun_corner_lined` | N E | bend |
| `tun_tee`, `tun_cross` | N E W / all | junctions, 2.9 m high |
| `tun_straight_shelled`, `tun_tee_shelled`, `tun_cross_shelled` | as plain | UG2: three deep wall craters and a shell crater in the floor |
| `tun_breach` | N S | UG2: a hole blasted through the rock between two galleries (loop links): ragged, cratered, spoil on the floor |
| `tun_room_s_*` | end / straight / corner / tee / cross | UG2: small concrete-lined rooms, 4.0-5.0 m, at junctions |
| `tun_room_m_*` | all four | UG2: medium rooms 3 x 3 cells, hall 10-14 m: `office`, `workshop`, `magazine`, `dugout`, `pump` (sunken pit), `mess` |
| `tun_stairs` | -z low, +z high | stair drift rising 3.6 m (one level) |
| `tun_chamber_*` | all four | 24 x 24 m (3 x 3 cells), 3.3 m high: `store`, `bunks`, `generator`, `sump` (flooded pool), `collapse` (fallen slab, rubble) |
| `tun_lamp`, `_flicker`, `_shadow`, `_red`, `_dead` | - | caged bulb hung from the ceiling (origin at the ceiling underside): steady, failing, shadow-casting, red emergency, dead |

Wall damage (UG2): most passage pieces carry blast craters carved 1.1-1.8 m into the rock (spoil in them, a scorched ceiling over them); rock protrusions stand 0.15-0.45 m out of the wall, the big ones (0.75-1.3 m) colliding; chambers and medium rooms have craters and rock bosses in their hall walls.

Tunnel pieces are never auto-furnished (`interior.cpp`).

## Layout

- 27 x 27 cells; junction nodes on a 13 x 13 lattice (odd cells), passages in the cells between linked nodes.
- Maze: randomised Kruskal spanning tree (every node reachable on full-height tunnels) plus ~1/3 of the other links as
  loops; ~35% of loop straights are squeezes.
- Five chambers at fixed nodes and (UG2) 18 medium rooms (16 on level 0, 2 above) at random nodes never next to another room, all four doorways connected; about 40% of the remaining junction nodes are small rooms. Loop links: 25% squeezes, 35% breaches.
- Five chambers at fixed nodes, all four portals connected. An upper level (y 3.6 m) covers nodes 8-12 x 8-12 in the
  north-east, reached by three stair drifts from the node column 7.
- Lamps: ~36% of cells get one outside four dark zones (~5% inside); ~15% are dead (no lamp flickers); ~28% of
  junctions get a red lamp; three lamps per chamber, one of them shadowed.
- The map fixes its hour at night: `"lighting": {"hour": 0}` in the map JSON (`app.cpp` restores the previous hour on
  another map), so any leak is dark.

## Play

Assault: the Allies push in from the west galleries, the Axis hold the east; objectives Generator room, Flooded sump,
The collapse. Team Deathmatch: team A east, team B west. Solo: by the algorithm. A supply crate stands next to every
spawn.

## UG3: a mine on many levels

- **Size:** 176 x 176 m (2/3 of the UG2 area). 21 cells of 8 m, centred; a 10 x 10 node lattice.
- **Elevation:**
  - Every junction node has a height from a smooth seeded field in 1.2 m steps, with about 19 m of relief between the
    nodes, plus two 10 m shafts.
  - Neighbours are relaxed to at most 2.4 m apart, so links climb by inclines: `tun_ramp_12` and `tun_ramp_24` rise
    1.2 / 2.4 m over a cell, with the floor, ceiling and dressing following the slope.
  - Stair drifts (`tun_stairs`, 3.6 m) appear only where rooms force them. Links steeper than that are never made.
  - Rooms and the nodes at their doorways are level.
- **Rooms:**
  - Chambers: generator, sump, collapse, store and two shafts.
  - `tun_chamber_shaft`: a 9 m pit 10 m deep in the middle, ledges round it, a timber bridge across it (north-south),
    and switchback stairs up the pit walls, so a fall is never a trap.
  - The sump is now deep: dark water over a 2.4 m pool, with steps out.
  - Also `tun_straight_humped` (gentle rock humps underfoot), `tun_straight_timbered` (close-set timber sets and
    lagging) and `tun_straight_meshed` (roof mesh and bolt plates).
- **Rail:**
  - A 0.6 m gauge mine track on sleepers: `tun_track_straight`, `_curve`, `_points` (with a lever), `_buffer`, and
    `_ramp_12` / `_ramp_24` on the inclines.
  - It follows the longest stair-free route that avoids the rooms, with points into a siding that ends at a buffer
    stop.
  - Carts stand as static cover in the wide junction hubs: `tun_cart`, `tun_cart_ore`, `tun_cart_tipped` (ore
    spilled), `tun_cart_derailed`.
- **Mine gear:** `tun_duct`, `tun_fan`, `tun_hopper`, `tun_winch` (at the shafts), `tun_ladder`, `tun_ore_pile`,
  `tun_tool_rack`, `tun_drill_rig`, `tun_compressor`, `tun_cable_reel`, `tun_powder_crates`, `tun_pump` (at the sump),
  `tun_conveyor` and `tun_warning_board` (striped, no text). They are placed by room kind, with ducts in the passages
  and ore piles in the dead ends.
- **No flicker:**
  - No shadowed lamps are placed: swapping the two shadow slots between them read as flicker.
  - Every thin floor and ceiling decal (puddles, mud, scorch) now sits on its own height step: overlapping coplanar
    decals z-fought.
  - Tunnel pieces are never furnished, so there are no candles or room lamps.
  - Fog stays off.

## UG4: floors with relief, darker

- **Floors:**
  - Every passage and room floor is a textured material with strong normals and relief: rock (two sets), gravel,
    rubble, mud, or broken concrete in the lined sections (texture slots `tun_floor_*`), chosen per piece.
  - Each piece also has `floorRelief()` geometry over its air:
    - low mounds that collide but stay under step height (flatter, and non-colliding, in the middle band where the
      track runs);
    - embedded rocks and loose stones, and shallow dark hollows;
    - rubble drifts at the wall feet, and broken slab edges on concrete.
  - A worn strip runs under the haulage line.
- **Darker:**
  - Lamps reach 5.2 m (red lamps 4.5 m), so each lamp is a pool of light.
  - The map sets `"lighting": {"fill": 0.35, "exposure": 1.0}`: the fill / indirect term is scaled down
    (`render.cpp`), and the exposure is pinned so auto exposure does not lift the dark stretches.
  - No new lights and nothing flickers; fog stays off.
