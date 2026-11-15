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
| `tun_stairs` | -z low, +z high | stair drift rising 3.6 m (one level) |
| `tun_chamber_*` | all four | 24 x 24 m (3 x 3 cells), 3.3 m high: `store`, `bunks`, `generator`, `sump` (flooded pool), `collapse` (fallen slab, rubble) |
| `tun_lamp`, `_flicker`, `_shadow`, `_red`, `_dead` | - | caged bulb hung from the ceiling (origin at the ceiling underside): steady, failing, shadow-casting, red emergency, dead |

Tunnel pieces are never auto-furnished (`interior.cpp`).

## Layout

- 27 x 27 cells; junction nodes on a 13 x 13 lattice (odd cells), passages in the cells between linked nodes.
- Maze: randomised Kruskal spanning tree (every node reachable on full-height tunnels) plus ~1/3 of the other links as
  loops; ~35% of loop straights are squeezes.
- Five chambers at fixed nodes, all four portals connected. An upper level (y 3.6 m) covers nodes 8-12 x 8-12 in the
  north-east, reached by three stair drifts from the node column 7.
- Lamps: ~36% of cells get one outside four dark zones (~5% inside); of those ~36% flicker, ~12% are dead; ~28% of
  junctions get a red lamp; three lamps per chamber, one of them shadowed.
- The map fixes its hour at night: `"lighting": {"hour": 0}` in the map JSON (`app.cpp` restores the previous hour on
  another map), so any leak is dark.

## Play

Assault: the Allies push in from the west galleries, the Axis hold the east; objectives Generator room, Flooded sump,
The collapse. Team Deathmatch: team A east, team B west. Solo: by the algorithm. A supply crate stands next to every
spawn.
