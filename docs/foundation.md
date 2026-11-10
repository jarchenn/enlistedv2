# Foundation dressing (MA3)

Grass, weeds, moss, leaf litter, small stones and a few prop arrangements around building bases. The code is in `src/foundation.cpp` / `foundation.h`.

It is generated at run time on every map, default maps and D-day included. No map regeneration is needed, and the map file stores none of it.

## Identity and life cycle

### Hosts

These asset categories get dressing:

- Houses, Small buildings, Large buildings, Landmarks;
- Ruins;
- Bunkers;
- Atlantic Wall casemates (ids with `casemate` or `_r6`).

The footprint must be over 1.5 m on both sides.

### Key

Each host's dressing is keyed by its instance id, asset and version, position and yaw, the map seed, the routes, the terrain generation and an optional style override. The same inputs always produce the same dressing.

### When it rebuilds

`foundationSync` runs once per frame in every mode. It only does work when an instance changed, appeared or went away, or the terrain was regenerated. Then:

- **Placed, moved or style-overridden host:** its old pieces are removed and new ones generated. One set per host, so no duplicates.
- **Deleted host:** its pieces and plants go.
- **Load:** the map file has none of it, so it is generated on the first frame that has the surface mask.
- **Undo / redo:** stray pieces from an old snapshot are removed, and missing ones regenerated.

### Props

Props are ordinary instances, so they get collision, navigation, damage and drawing like any placed asset.

- **Tag:** `extra["dressing"] = {host, sig}`.
- **Ids:** `(1 << 44) | (host << 6) | k`. They are identical on every machine and never use `MapDoc::newId`, so `nextId` does not drift.
- **Map file and hash:** `mapToJson` skips them, so `Net::mapHash` ignores them too.
- **Nav:** the navmesh key includes them, as for V4 furniture. Shipped `.nav` sidecars therefore need a rebake with `NMK_NAV_PREBAKE`.
- **Map dirty flag:** they never set it.

### Plants and stones

Plants are `GrassInstance`s (from `naturePlant` in `nature.cpp`) in one buffer per host. They are drawn with the nature patches through the grass program:

- shared wind, distance fade and LOD;
- no scene objects and no physics bodies;
- a 140 m reach.

Stones are one merged mesh per host (`natureStone`). They cast sun shadows only within 14 m of the eye (R2 `castR`). Furniture props get R2's small-caster class through `interior.propDraw`.

### Destruction

A wall panel turning `PANEL_BREACHED` removes the host's props within 1.2 m of that panel, since their support is gone. Plants on the ground stay. A reset (F5 / `resetDamage`) restores the panels, and the dressing is regenerated.

## Zones

The zones are measured from the asset's ground rectangle (`bmin` / `bmax` in x and z, which includes steps and porches).

- **Band:** a strip along each wall, sampled every 0.3 m.
  - Its width is 0.2-1.2 m from value noise in world space, scaled per style.
  - Bare stretches follow a second noise field; the bare share is set per style.
  - The first 0.3 m by the wall uses the wall mix (moss, litter, weeds); the rest uses the band mix.
  - About 16 plants per m², times 1.0 (maintained), 1.8 (neglected), 1.4 (damaged), 1.6 (bunker) or 1.3 (coastal).
- **Corner clusters:** a quarter disc of 0.9-2.1 m at each corner, by a per-style chance.
- **Stones:** 0.4-4 per 10 m of wall by style, mostly within 0.75 m of the wall.
- **Shading:** walls facing world north get darker, mossier grass.

## Exclusions

All are tested in `Ctx::excluded`, at three levels: prop, tall plant and short plant.

| What | Rule |
| --- | --- |
| Doors / entrances (`navMeta.entrances`) | A lane straight out: 2.6 m long and ±1.15 m wide for plants; 3.6 m and ±1.7 m for props |
| Outside doors (`navMeta.doors` to "outside") | 1.4 m for plants, 2.2 m for props |
| Windows (`firingPoints` kind window) | No tall plants or props within 1.6 m; short grass only |
| Embrasures, MG, fire-steps | Nothing within 1 m; a ±60° sector out to 9 m for tall plants and 16 m for props |
| Paths, roads, trenches, other buildings, walls, boulders, trees | Any other footprint (`App::grassFootprints`, crops excluded): 0.05 m for plants, 0.15 m for props |
| Lanes, footpaths, gravel routes | Surface wear > 0.8. Foundation soil alone stays under 0.75 |
| Lasso paint / water, the sea | `groundPaintBlocksNature`; height under water + 0.05 m |
| Steep ground | Plants on slopes under normal.y 0.62; props on slopes under 0.9; props on a footprint whose grade exceeds 0.25 m |
| Spawn markers | 3 m for plants, 5 m for props |
| Interiors, floors, walls | Everything starts outside the host rectangle; props must keep 3 cm clear of it |

## Styles

The style is chosen from the asset, the map and the seed:

- Bunkers and Atlantic Wall casemates → **bunker**.
- Ruins (or ids with ruin / shelled / burnt) → **damaged**.
- Houses on a coastal map (omaha / dday / beach / coast template) within 6 m of sea level → **coastal**.
- Otherwise **neglected** for 45 % of houses (25 % of large buildings and landmarks), by the seed, else **maintained**.

A host's `extra["dressingStyle"]` overrides the choice; `"none"` turns dressing off for that host.

| Style | Plants | Bare share | Stones / 10 m | Props |
| --- | --- | --- | --- | --- |
| maintained | Short grass, clover, moss (low dark clover) and litter (short brown straw) by the wall, a few tussocks | 42 % | 0.4 | 1-2: firewood, wood pile, garden bed, barrels, crates |
| neglected | Tussocks, seed grass, tall weeds, cow parsley, scrub; tall corner pockets | 24 % | 1.2 | 1-3: firewood, wood pile, barrels, crates, rubble, cart |
| damaged | Litter, straw, seed grass, tall weeds, scrub | 30 % | 4 (larger) | 1-2: mostly brick / stone rubble, barrels, crates |
| bunker | Rough grass, scrub at the earth cover; door lanes stay bare (worn access) | 18 % | 2 | 0-2: barrels, crates, rubble |
| coastal | Pale dune grass, straw, seed grass | 34 % | 2.5 (granite) | 0-2: barrels, crates, firewood, rubble |

### Prop arrangements

Arrangements are made of existing assets: `furn_wood_pile`, `furn_bucket`, `drs_woodpile`, `furn_barrel`, `furn_crate`, `drs_flower_bed`, `rub_brick_pile_s`, `rub_stone_pile_s` and `drs_cart`. Each piece has a chance, and the anchor wall and position come from the seed.

## Tests

`--selftest-nature`, step "MA3 foundation dressing":

- a cottage with a path across its front;
- nothing inside the building, in its door lanes or on the path;
- the same hash after the records are dropped (a reload);
- the map file stores no dressing;
- after a move, one set with no leftovers and no duplicate ids;
- after a delete, nothing left.

`NMK_MA3_SHOTS=1` adds close and medium shots of each style in `selftest_out/ma3/`.
