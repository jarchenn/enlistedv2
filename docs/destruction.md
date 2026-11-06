# Destruction and ground deformation

**Units.** Health and damage below are *game units* chosen for play balance. They are not joules, not penetration figures, and not a model of any real wall or shell. Historical references (TM-E 30-451 and others, listed in `docs/weapons.md`) informed the PaK 40's traverse, elevation and optic. They did not set these numbers.

## Wall panels

All 14 enterable assets carry destructible wall panels. Each panel has a stable semantic ID, `floor-NN/<side>/bay-NN`, so saved damage survives regeneration as long as the layout is unchanged.

| Family | Health | Used by |
|---|---|---|
| masonry | 200 | farmhouse, cottage, small and large stone buildings |
| concrete | 450 | bunkers (casemate, command shelter, field shelter, observation post) |
| timber | 90 | timber-framed bays and workshop walls |

- **States.** Intact above 60% health, damaged below 60%, breached at 0 or below. A damaged panel shows spalling and cracks. A breached panel opens a hole inside a frame collar, the remaining jambs and lintel, so the wall never floats.
- **HE.** One 7.5 cm HE impact does 120 at the point of impact, falling linearly to 0 at 2.2 m. A masonry bay breaches in 2 hits; a concrete bunker bay takes 4.
- **Collision.** The core (floors, roofs, stairs, frames) is immutable. Each placed copy has its own wall compound body rebuilt from its panel states. Rebuilds took 0.011 ms for the farmhouse and 0.034 ms for the municipal building, the largest.
- **Debug.** F4 in Explore draws panel outlines and states.

## Ground

- The canonical height is the base heightfield plus sparse fine tiles: 5 base cells per tile, 36 × 36 samples at about 0.45 m.
- The shipped HE crater is about 3 m across (R 1.5 m) and 0.42 m deep, with a small rim. Accumulated depth is capped at 1.6 m below the base.
- Foundations are protected: `groundWeight` is 0 inside a placed building's footprint and ramps to 1 at 1.8 m, so stairs and thresholds stay supported. Field guns and crates do not protect the ground.
- Disturbed soil is drawn as soil, and grass is removed from it (see `docs/STATUS.md`, Milestone 5).

## Session overlay and persistence

- Explore damage, walls and ground alike, is a session overlay. F5 back to Creative restores the map exactly.
- **Edit → Apply damage to map** copies the current panel health into `overrides.panels` and the terrain deltas into `terrainDeltas` as one undo step. **Edit → Clear applied damage** removes both.
- Terrain deltas are keyed to the template version. Loading them against a different template version discards them with a notice rather than misapplying them.

## Known limits

- A 0.42 m crater does not hide a crouched player's head. Concealment needs about 0.7 m, which takes repeated hits on the same spot.
- Only the 14 enabled assets have panels. The other 26 catalog entries are out of scope for this update and stay disabled.
- Debris is cosmetic: pooled boxes with simple physics, no collision with the player.
