# R2: shadow casters, foliage shadow LOD, probe budget, interior visibility

Brief Part II sections 22-24. Five `Renderer::Perf` switches. On by default: `castercull`, `leafshadowlod`, `probebudget`. Off by default: `roomcull` (measured no gain) and `propdist` (not yet measured). Turn one off with `--rq-off <name>`; A/B it with `NMK_PERF_AB=<name>`. `r2` names all five together.

| Switch | What it does | Code |
|---|---|---|
| `castercull` | Small props cast sun shadows (near and far cascades) only within about 14 m of the eye (±15 % per piece, so there is no ring). | `DrawItem::castR`, `Renderer::drawItems`, `InteriorSys::propDraw` |
| `leafshadowlod` | Canopy depth passes draw a shadow card LOD: every 2nd cluster card, grown 1.25× about its centre, with the same uv and the same `leafAlpha` cut-out (about 80 % of the leaf area). Near cascade: only for trees more than 12 m from the eye, so the dappling under and beside you uses the full set. Far cascade and sky occlusion: always. Styled cards (fronds, nettles) are copied unchanged. | `foliage.cpp` (`TreeDef::shadowFirst/shadowVerts`), `Renderer::drawFoliage` |
| `probebudget` | The GI capture budget is counted in cost units: a near-volume probe costs 1, a far-volume probe (60 m range) costs 4. Unused units carry over (capped at max(budget, 4)). Never-captured probes come first, then invalidated ones, then the age refresh. Within each group, priority is divided by cost, so near probes go first. | `lighting.cpp` `probePass` |
| `roomcull` | The camera pass skips a furnished piece inside a closed, roofed building when no opening of that building can show it from the eye. Shadow passes and probe captures still draw it. | `InteriorSys::interiorHidden`, `DrawItem::mainHidden` |
| `propdist` | A small decorative piece (`prop.blocking == false`, so never cover) is drawn while it spans at least about 3 px (±15 % per piece). | `InteriorSys::propDraw` |

## Caster classes (metadata, not names)

`PropDef::smallCaster`:
- **Explicit:** `"caster": "small" | "full"` in an asset's `prop` block sets the class.
- **Default:** derived from the prop's generated metadata. A piece is small when it is non-blocking and its bounds are under 1.6 m, or when it is flat (rugs, papers).
- **Small:** books, papers, frames, candles, crockery, rugs.
- **Always cast:** structures, blocking furniture (silhouettes, cover), vehicles, characters, trees, and anything that is not a prop.

## Room culling: how it stays conservative

Per building and frame, `InteriorSys::interiorHidden` does the following:

1. **Which buildings take part.** Only buildings that are closed. A building is closed when:
   - it is enterable, has rooms, and is not a ruin;
   - above the centre and quarter points of every room some visible part covers it: the floor above, or the roof.

   Wrecked buildings (`defDestroyed`) are never culled.
2. **Eye position.** If the eye is inside the building's box (+0.3 m), everything is drawn.
3. **Openings.** The building's openings are listed as boxes in asset space:
   - every interactable aperture (doors, gates, windows, shutters);
   - every glass part;
   - every opening trim;
   - every connected run of doorway cells from the room grid, floor to ceiling (gaps under a lintel, with or without a door);
   - every bay that is damaged or breached, roof bays included. These are read from `InstanceRT::state` and `wf` each frame, so a breach counts on the frame it happens.
4. **Projection.** The axis of the eye's largest gap to the building box is the projection axis. Every point of the building lies in front of the eye along it. Each opening box is projected to a 2D rectangle. If any opening corner comes within 2 cm of the eye's plane, everything is drawn.
5. **The piece test.** A piece is hidden only when all of these hold:
   - its centre is on a room cell of the building;
   - its projected rectangle overlaps no opening rectangle.

   Any ray from the eye to the piece has to cross the shell, which is closed except for the listed openings. Connected rooms, multiple openings and elevated windows are therefore covered without a portal graph. Visibility is decided in the same frame from the current eye, so there is no latency and no pop-in when you round a doorway.

Checked in `--selftest-interiors` ("R2 roomcull"), from 48 eyes around the farmhouse at 25 m:
- every piece view it hides has a solid physics hit on the line from the eye;
- with the eye inside the building nothing is hidden;
- with every bay breached fewer pieces are hidden.

**Limits:**
- Only furnished (room-template) pieces are culled. Built-in furniture is part of the building mesh, and the merged far batches (≥ 18 m) are not tested.
- An opening that is none of the kinds above would be missed: a hole modelled as a gap between parts, with no lintel, glass or interactable. The image diff (all switches on vs `--rq-off roomcull`) over the maps views is the check for that.

## Probe invalidation and staleness

`lightingInvalidate(lo, hi)` marks the probes in a world box (grown by one probe spacing) dirty. A dirty probe is re-captured next and blended into its old value (blend 0.6). It is called from:
- `App::rebuildWalls`, for every breach, repair or tank ram, over the building box +1.5 m;
- `App::explode`, for craters, felled trees and blown shutters, over the blast radius +2 m;
- a door or shutter finishing its swing, over ±4 m around the aperture.

`--selftest-maps` reports `probePending` (never captured), `probeDirty`, and `probeStaleMax`: the oldest capture among the near probes within 12 m, in frames.

## Near-cascade static caching: not shipped

See the report: the near cascade follows the eye, so its box moves with every turn of the camera. It carries wind-animated canopies and grass, moving soldiers and vehicles. A static/dynamic split needs a second 4096² depth target, a per-frame copy, and invalidation on every transform, damage and light change. The filtered redraw above was the brief's preferred option.
