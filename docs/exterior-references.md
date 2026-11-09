# Exterior references (GX4)

The user's graphics brief makes five CC-BY-4.0 Sketchfab GLBs the ground truth for building exteriors: silhouette,
proportions, roof pitch, window and door spacing, timber-frame layout, trim depth and material zoning. They are
**reference only**: no mesh or texture from them is in the repository or a build (credits: `docs/credits.md`). The
game's own part-built buildings were rebuilt toward them by `tools/exterior.mjs` (the GX4 exterior pass).

The sixth GLB of the brief (TampaJoey's modern "Military Style Building") is skipped by the user's decision.

## How the references were read

The GLBs live outside the repo in `Documents/MapMaker/incoming/gx_refs/`. Blender 5.2 in background mode (no
interactive scene touched) imported each one, split the asset packs into buildings by footprint clustering, and rendered
orthographic front / back / left / right / top views (known px per metre), a three-quarter view and four street-level
corner views of each building into `incoming/gx_refs_render*/` (not committed). A second pass measured, per building:
the area-weighted roof pitch histogram, eave and ridge heights, wall and roof area per material, and the texture maps.
The scratch scripts (`render.py`, `measure.py`, `montage.py`, `rows.py`, `asset_view.py`) stay in the session
scratchpad; nothing of the GLBs is imported into `content/`.

## Inventory

| GLB | Author | Content | Size | Triangles | Materials / maps |
|---|---|---|---|---|---|
| European Buildings Asset Pack 1 | EddyNL | 8 buildings (Breda-style Dutch / Flemish town): P1-0 stepped-gable brick hall; P1-1 church with a classical tower front; P1-2 a courtyard block with stepped gables, slate and red tile roofs and a stair tower; P1-3 a white-rendered corner town house with a hipped pantile roof and a shop front; P1-4 a brick and a rendered town house side by side, shop fronts; P1-5 a red-brick Art Nouveau town house with stone trim; P1-6 a white stucco neoclassical town house (balcony, cornice, mansard dormers); P1-7 a buff-brick hipped shop building with sash windows | 215 x 90 m spread | 19.1 k | 95 materials, 88 base-colour photo textures (16-2048 px, mostly 128-512); no normal / roughness maps |
| European Buildings Asset Pack 3 | EddyNL | a glasshouse (not used), P3-1 a stone-dressed coach-house with arched cart doors and pergolas, P3-2 a red-brick villa with steep tiled roofs, cross gables and dormers (`Atlas_Schonberg` is its brick atlas, not a half-timber atlas), an octagonal pavilion, billboard trees | 149 x 53 m | 15.6 k | 92 materials, base colour only (photo atlases up to 1024 px) |
| Little Rock Inn | Beardmonkey | single inn: a rubble-stone base, weatherboarded walls, a long porch on posts, a rubble chimney stack, a sign board, low-pitched roofs | 20.0 x 22.4 x 9.1 m | 804 | 1 material, 512 x 1024 base colour + a duplicate |
| Shoeburyness Small Building | artfletch | photogrammetry of a WW2-era utility building: painted brick, concrete coping on a flat roof, a ledged door, an earth bank and a brick niche | 29.8 x 20.4 x 10.9 m (with the bank) | 902 k | 1 material, 1024 px base colour |
| Tilbury Fort Spigot Mortar Emplacement | artfletch | photogrammetry of a concrete emplacement on a mound: spalled concrete, rust runs, grass over the earthwork | 17.1 x 15.3 x 7.1 m | 1.6 M | 1 material, 1024 px base colour |

## Measurements

Heights are above the building's lowest point. Pitch is the dominant roof-slope mode (2.5 degree bins, area-weighted).

| Ref | Footprint (m) | Ridge (m) | Eave (m) | Pitch | Openings and trim | Material zoning |
|---|---|---|---|---|---|---|
| P1-0 hall | 30.5 x 37.4 (with the wings) | 14.5 | ~7 | 47.5 deg, crow-stepped gables | cross windows ~1.2 x 1.9 m at ~2.6 m centres with red shutters; a row of small dormers; stone bands every ~0.5-0.7 m (speklagen) | red brick in stone bands, stone copings, dark slate |
| P1-2 courtyard | 65.7 x 81.5 | 28.1 (tower) | 6.7 | 50-62.5 deg | stepped gables, dormers in every bay, a round stair tower with a lantern | buff and red brick, stone dressings, slate and red tile |
| P1-3 corner house | 6.3 x 14.0 | 9.1 | ~6 | 37.5 deg hipped | 2 bays of tall sash windows (~1.0 x 1.8 m) in plain stone architraves, shop window on the ground floor | white render, brick upper side wall, **dark painted plinth band ~0.7 m**, orange pantiles |
| P1-4 pair | 8.3 x 10.9 | 10.3 | ~8 | 40-42.5 deg | each house its own cornice and window rhythm (2 bays, ~2 m centres) | one brick, one rendered house side by side, dark painted plinth on the rendered one |
| P1-6 neoclassical | 13.2 x 16.3 | 16.3 | 8.5 | 30-35 deg (mansard) | 5 bays at ~2.4 m; architraves with sills and hoods; a dentil cornice; **channelled (rusticated) ground floor**; balcony | white stucco, dark painted plinth |
| P1-7 shop | 10.4 x 12.7 | 15.3 | 10.3 | 45-62.5 deg hipped | 4 bays of sash windows (~2.2 m centres), a full-width shop front with a fascia | buff brick with a stone band, dark slate |
| P3-1 coach-house | 37.6 x 52.7 (with pergolas) | 10.6 | 6.0 | 15-17.5 deg | arched cart doors in stone dressings, triple arched windows, deep timber eaves | ochre render, dressed-stone quoins and arches |
| P3-2 villa | 37.9 x 59.8 (site) / ~15 x 11 body | 17.2 | 4.4 / 8 | 50 deg main, 22.5 deg lower | 3 bays, windows ~1.1 x 2.0 m with stone surrounds and green shutters, cross gables, dormers | red brick in beige stone bands at sill and floor lines, red tile |
| Little Rock Inn | 20.0 x 22.4 | 9.1 | ~3.6 | 17.5 deg | 4 multi-pane windows at ~3 m under a porch on posts | rubble-stone base to ~1 m, weatherboarding over, rubble chimney |
| Shoeburyness | 29.8 x 20.4 (site) | 10.9 | 3.6 | flat (concrete coping) | one ledged door, a vent | painted brick, dirt and moss rising ~1 m up the wall foot, concrete coping |

## Mapping to the game's buildings

| Game id (catalog) | Reference | What the pass took from it |
|---|---|---|
| `large_village_schoolhouse` (houses2 / large) | P1-0 | crow-stepped gables with stone copings, stone bands at the sill and head lines of every storey, a dentil course |
| `large_municipal_building` | P1-6 | rusticated ground floor (channelled courses), dentil course |
| `vil_rectory` (chateau) | P1-6, P1-3 | rusticated ground floor, dentils, dark painted plinth |
| `vil_terrace_pair` | P1-4 | painted plinth on the rendered house, stone bands on the brick house, dentils |
| `vil_corner_shop` | P1-7, P1-3 | stone bands on the brick upper storey, dentils |
| `vil_villa_deauville` | P3-2 | stone bands on the brick ground floor, dentils, red tile courses |
| `large_village_inn` | Little Rock Inn, P1-7 | a heavy channelled base under the timbering |
| `small_roadside_office` | P1-3 | dark painted plinth |
| `large_stone_warehouse`, `small_storehouse`, `small_carpenter_workshop`, `small_stone_dairy` | Shoeburyness, P3-1 | wall-foot weathering, roof courses, quoin relief |
| `vil_chateau_grand`, `vil_manor_turret`, `vil_gatehouse_chapel` | P1-2, P3-2 | steep tile / slate courses, ridge tiles, timber relief |
| `house_farmhouse_2story`, `vil_norman_farmhouse`, `vil_longhouse_thatched`, `vil_townhouse_jettied`, `vil_cottage_barn`, `vil_mill_house`, `house_stone_cottage`, `vil_stone_barn` | (rural Norman: no direct GLB; P1/P3 for trim depth, Shoeburyness for weathering) | roof courses, ridge tiles, deeper chamfered timbers, rusticated quoins, wall-foot weathering |
| `vil_church` | P1-1 | slate courses and ridge tiles |
| bunkers (D1) | Tilbury Fort | concrete weathering and earthwork blending belong to GX1 / GX5 |

Not taken over, by design: the references' photo signs and shop names, the Dutch-specific orange pantile on Norman
houses, and **roof pitch**. Raising the farmhouse / cottage roofs to the references' 45-50 degrees would change the
attics, nav, dormers and roof colliders, which the GX4 rules keep. The steep roofs that already exist (longere 50 deg,
chateau, villa, manor) match the references.

## What the pass does (`tools/exterior.mjs`)

Run last on every B2 / B3 building (`tools/build_mpu_assets.mjs` maps `chateauAssets()` and `houses2Assets()` through
`exterior()`); ids, interiors, panels, nav and colliders are unchanged, and each building's version goes up by one.

- **Roof courses.** Every pitched roof slab (gable roofs, cross gables, dormer roofs) and every hip roof gets real
  courses: thin strips tilted so each lower edge stands proud of the course below (2.4 cm tile, 1.3 cm slate). The
  strip pitch is one L4 texture row (tile 0.264 m, slate 0.208 m) and each strip is sized so a texture joint falls on
  its lower edge (the row phase was measured from the images). Hip roofs get the same on both trapezoids and both hip
  triangles, plus half-round hip rolls. The material `tile` now uses the `roof_tile` slot (RoofingTiles014B) instead of
  the slate set.
- **Ridge tiles.** The ridge bar or roll is overlaid with a run of tapered half-round tiles (0.4 m), flared ends lapping.
- **Timber framing.** Members from `parts.timberFrame` stand 3.0-4.2 cm proud (was 1.8 cm; the open shutters fold at
  3.5 cm), with a 1.6 cm chamfer and +-1.1 cm adzed width variation.
- **Quoins.** Each stone projects its own 0.8-3.8 cm, with deeper bed joints and a 1.4-2.6 cm chamfer.
- **Reference features** (table above): crow steps, dentils, refends, stone bands, painted plinths. Facade strips over a
  destructible wall bay are put in that bay's intact state, so they fall with the wall.
- **Wall foot** (`kMainFS`, `uWeather`): the dirt gradient now reaches ~1 m, with rain-splash speckle in the lowest
  0.35 m and moss patches to ~0.6 m (less on brick).
- **Rain-streak anchors.** `meta.decals`: one `{kind: "rain_streak", pos, n, w, h}` under each sill nose, for GX5's
  static decal system.

### LOD flag for PF2

Parts that are pure exterior relief carry `"lod": 1` (always `k: 0`, never in a bay): roof courses, hip rolls, ridge
tiles, dentils, refends outside the bays. The slab or wall under each one stays, so a far LOD may drop every `lod: 1`
part and keep the silhouette. The current loader ignores the key.
