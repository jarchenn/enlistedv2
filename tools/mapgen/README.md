# mapgen: default-map authoring toolkit

`tools/mapgen` builds the shipped default maps (SPEC §3) as ordinary data: a **template** in `content/templates/`, a
**16-bit heightmap** next to it, and a **map file** (`.nmap.json`) holding every instance, the scenario v2, and the
ground routes. "Create editable copy" in New Map loads that map file into a normal creative document, so everything it
places stays editable.

It is plain Python 3 (standard library only). Run it with the build environment's interpreter:

```bash
"$TEMP/nmk/py/python.exe" -I tools/mapgen/build.py omaha_easy_red
```

`build.py <name>` imports `maps/<name>.py` and calls its `build(ctx)`. `ctx` carries the repository and content paths
and the asset catalogue (`ctx.catalog`). Build reports go to `tools/mapgen/out/` (git-ignored).
`maps/example_small.py` is a 600 × 1000 m smoke test that uses every feature and writes only to `out/`.

## Coordinates

| Term | Meaning |
|---|---|
| world | The engine frame. The build area is `width × depth`, centred on the origin. +X is east and +Z is south. |
| `Frame(width, depth)` | The attack-axis frame. `s` is metres from the attackers' edge (world `z = -depth/2`) toward +Z. `frame.w(x, s)` returns world `(x, z)`; `frame.s_of(z)` converts back. The spec tables (§3.3/§3.4: "z = 0 is the sea") are in `s`. |
| instance yaw | Degrees about +Y; asset-local +Z points at world `(sin yaw, cos yaw)`. Use `asset_yaw_facing(dx, dz)` to point local +Z, or `asset_yaw_along(dx, dz)` to lay local +X along a direction. W4 assets have their front (enemy side) on local +Z. |
| look yaw | Spawns, objective attack arrows and defense slots face `(-sin yaw, -cos yaw)`. Use `look_yaw(dx, dz)`. |

## Modules (`mapgen/`)

### `terrain.py`: base ground on W3's grid

- `Grid(width, depth, margin)` reproduces `Terrain::generate` exactly:
  - spacing `KCELL = 800/255` m;
  - the build area plus `margin`, rounded up to whole 5-cell tiles;
  - `sample(x, z)` uses the engine's triangle split.

  Heights read here are therefore the engine's heights.
- **Zone tables:** `sculpt_zones(grid, frame, zones)`. Each zone is a dict:
  - `s` (s0, s1);
  - `height`: either `(h0, h1)`, eased with smoothstep, or `f(x, s)`;
  - `noise` (amplitude m, scale m);
  - `blend` (m), a cross-fade into the previous zone.

  `Profile([(s, h), ...])` is a smooth knot curve, handy inside `height` functions.
- **Stamps:**

  | Stamp | Use |
  |---|---|
  | `stamp_valley(grid, path, floor(u), top_half(u), floor_half(u))` | Draws and gullies. Only lowers. `u` runs 0..1 along the path. |
  | `stamp_channel(grid, path, half_width, depth)` | Lowers ground relative to the surface. |
  | `stamp_bank(grid, path, half_width, height)` | Raises ground (dunes, banks). |
  | `stamp_flatten(grid, x, z, radius, height)` | Flattens a disc; `stamp_flatten_poly` flattens a polygon. |
  | `stamp_crater(grid, x, z, R, depth)` | The engine's crater bowl plus a lip. Use it for big bombardment craters; the grid is about 3.1 m. |
  | `stamp_noise(grid, amp, scale, seed, region)` | Adds noise. |

- **Finish:** `grid.quantise()` snaps to the 16-bit steps. Call it before placing instances, so their heights match.
  Then `grid.write_png(path)`, and put `grid.meta(file)` into the template's `heightmap` block.

### `layout.py`: `MapBuilder(catalog, grid, frame, seed, water_level)`

`place(asset_id, x, z, yaw, ...)` places one instance the way the editor does:

- **Height:** the highest of nine footprint samples. Defences (`defense`) and paths use the centre height.
- **Overlaps:** the oriented footprint is refused if it overlaps another placement. Two exceptions:
  - classes listed in `ok_with` (`solid`, `veg`, `flat`, `wire`, `none`);
  - pieces of the same `group`.
- **Also refused:** water and the outside of the build area.
- **Companions:** listed `companions` are added with the host (for example the R667's PaK 40), as the palette does.
- **Terrain cuts:** they are tracked, so later heights see trench and ditch floors.
- **Options:**
  - `check=False` skips the tests;
  - `max_grade` refuses slopes;
  - `tags` mark instances for later queries (`tagged()`);
  - `extra` is merged into the instance JSON.

`try_place(...)` nudges a refused placement a few times.

Linear features:

- `chain(assets, polyline, along="x"|"z", front=pt, gaps=[(s0, s1)])` lays joining pieces end to end:
  - hedgerow banks, trench runs (`front` = the enemy, so the parapet faces it), the shingle bank, the AT ditch and wire run along X;
  - sunken lanes run along Z.
- `belt(assets, polyline, spacing, rng, jitter_along, jitter_across, gaps, rows, row_gap, yaw_jitter)` builds obstacle
  belts: single obstacles, irregular, with open gaps.
- `scatter(assets_or_weights, polygon_or_predicate, density_per_m2, rng, min_dist, slope_max, accept)` is jittered
  density scatter by dart throwing, never a grid. The default minimum distance is `0.55/√density`. `accept(x, z)`
  thins it into gradients. It avoids routes and keep-outs.
- `route(kind, pts, width, wet=, strength=)` paints the surface mask. These are `MapDoc.extra.routes`.

  | Kind | Surface |
  |---|---|
  | `lane` | Ruts |
  | `footpath` | Footpath |
  | `gravel` | Gravel |
  | `sand` | Beach sand with a sharp 5 m edge |

  `wet` adds standing water on tidal flats. `strength` < 1 gives partial wear (sand with dune grass). Routes also
  become scatter keep-outs; add more with `keepout_poly()` and `keepout_line()`.
- `cover_gaps(path, lateral=15)` walks an attack path and lists the stretches with no cover within `lateral` m. Cover
  is nav cover, blocking assets, buildings, `cover`-tagged instances or `cover_points` (register craters there). This
  checks the §3.5 rule of something to stop behind every 30–50 m.

### `bocage.py`

- `split_fields(region, rng, field_area=(9000, 20000))`: recursive, skewed splits into irregular 1–2 ha fields. It returns
  the fields and the hedge lines, with no duplicate edges.
- `hedgerow(b, line, rng, gaps=cut_gaps(line, roads, 6))`: mostly W4 10 m banks, with some of the older 8 m hedge.
  - **Field gates:** a gate gap every 140–260 m, sometimes using the W4 gap piece.
  - **Trees:** standalone oak, ash and poplar on the bank. With the pieces' own pollards, that keeps trees every 6–12 m.
  - **Groups:** pass the same `group` to a whole network so junctions may overlap.
- `orchard(b, poly, rng, spacing=8, gap=0.15)`: the loose 8 m grid with gaps the spec asks for, rotated and jittered.
- `sunken_lane(b, pts)`: W4 sunken-lane pieces chained along Z. Paint the lane with `route("lane", ...)` too.

### `defense.py`

- `site_mg_nests(b, objective, lanes, n, rng, ring=(25, 110), fixed=[...], accept=f)` picks MG positions and bores
  greedily, to maximise weighted coverage of the attack lanes.
  - **Scoring:** a lane point already seen by another gun scores again only when the bearings differ by at least 30°.
    That is crossfire, not guns all facing down the axis.
  - **Assumptions:** guns have a 300 m range and ±55° traverse. Line of sight is checked on the grid.
  - **Return value:** `[((x, z), bore), ...]`. `bore` is an asset yaw, so local +Z runs along the bore.
- `interlock_report(b, nests, lanes, objective)` returns the fraction of lane points within 250 m that are covered, and
  covered by two or more guns from bearings at least 30° apart.
- `build_mg_nest(b, pos, bore, style)` builds the nest and returns `(instances, slot)`. Styles:
  - `sandbag`: three-sided sandbags and an MG 42 on its pedestal;
  - `pit`: curved sandbags;
  - `tobruk`: Ringstand 58c.
- `slots_from_structures(b, insts)` makes defense slots from the assets' nav `firingPoints`:

  | Firing point | Slot kind | Priority |
  |---|---|---|
  | embrasure | `embrasure` | 3 |
  | mg | `mg_nest` | 3 |
  | firestep | `trench_bay` | 2 |
  | window | `window` | 2 |
  | foxhole | `foxhole` | 1 |

  `slot(x, y, z, look_yaw, kind, priority)` hand-authors one.

### `scenario.py`: `ScenarioBuilder(b, defending="Axis")`

- `objective(name, x, z, radius, capture_time, attack_from, linked=[ids], slots=[...], routes=[(lane, pts)])`.
  Objectives are labelled A, B, C… in call order. `attack_from` sets the attack arrow.
- `spawn(role, phases, x, z, rack="auto"|asset_id|instance|None)`. The marker faces its first phase's objective.
  - **`auto`:** places the team rack 2.5–4 m to the marker's right or left, as `MpUi::placeRack` does.
  - **Asset id:** pass an id for another rack, such as the beach crate racks.
  - **Instance:** pass an already placed instance (inside an LCVP) to link it.
- `validate()` returns `(errors, warnings)`:
  - errors are `validateScenario`'s SPEC 1.3 checks;
  - warnings are the §1.3 and §2.1 distance bands: objectives 450–600 m apart, attackers 300–450 m before, defenders
    250–500 m behind. The first attacker spawn may be further.

### `output.py`

- `write_map(path, name, template_id, template_version, seed, width, depth, builder, scenario_json, explore_start, camera)`
  writes `mapToJson`'s format. Routes go in `routes` and the scenario in `scenario`.
- `write_template(path, tid, name, description, order, version, seed, width, depth, margin, heightmap, water_level,
  map_file, camera, thumbnail, thumb_camera, playable)` writes the template.
- `write_summary` and `asset_usage` produce the build report.

### `paint.py`: `GroundPaint`

Lasso ground paint for authored maps (MapDoc `groundPaint`): `poly(kind, poly, weight, edge, ragged)` and
`line(kind, pts, width)` paint 0.5 m texels (kinds in `content/ground_paint.json` order: dirt, mud, gravel, ploughed,
dry_grass, leaf_litter...); a partial weight goes over the old layer as the lasso stroke does. Pass
`extra={"groundPaint": paint.to_json()}` to `write_map`.

## Engine side (`src/w5_maps.cpp`, `src/w5.h`)

A template may carry these keys:

| Key | Effect | Hook |
|---|---|---|
| `heightmap` | `{file, nx, nz, min, max, org, cell}`: the PNG is sampled into `Terrain::h` | `Terrain::generate` |
| `waterLevel` | Sea or standing-water surface (m): wading, no placement in water | `Terrain::generate` |
| `map` | The authored `.nmap.json`, loaded into the new document by New Map | `App::newMap` |
| `thumbnail` | The pre-rendered New Map preview: a square PNG whose middle 60 % band is shown | `App::makeThumbnails` |
| `thumbnailCamera` | The fixed camera that `--render-map-thumbs` renders the preview from | — |
| `playable` | Lists the map in the Play setup's **Default maps** picker | `MpUi::setupWindow` |

`content/settings/maps.json` holds `decorCull`: draw distances for decorators per asset category, with per-asset
overrides under `assets`. The draw distance is `clamp(perMetre × largest extent, min, max)`. It keeps dense default
maps inside the frame budget. Foliage (hedgerows and trees) is drawn in the main pass and every shadow pass, so its
distance dominates the frame. In bocage the next hedge hides what lies beyond it, so short distances barely show.

## Shipped maps

- `maps/omaha_easy_red.py`: Normandy Beaches - Omaha, Easy Red (SPEC 3.3). It writes `content/templates/omaha_easy_red.{json,height.png,nmap.json}`; the preview `omaha_easy_red.thumb.png` comes from `--render-map-thumbs`.
- `maps/normandy_forest.py`: Normandy Forest - Mont-Castre, Hill 122, a **terrain base** (850 x 1700 m, set by the
  one constant `SIZE`): rolling hills, the Hill 122 ridge, rock escarpments, tors, a quarry, a ravine, a draw with a
  sunken lane, streams and a marsh. Its map file has no instances and no `scenario` key, so the user lays out
  everything; the template is not `playable`. It defines its own stamps (`stamp_escarpment`, `stamp_tor`,
  `stamp_quarry`, `stamp_cut`) and writes `views` (named cameras) into the template for `--selftest-maps`.

- `maps/chateau_town.py`: Chateau town (M1), a dense 350 x 400 m town around B3's grand chateau, built in layers: the
  walled chateau forecourt with its parterre garden, a ring road and winding streets lined with near-continuous
  terraces of the B2 / B3 houses (a weighted deck so every type comes round; frontage placement keeps houses clear of
  every route, keep-out and back yard), fenced back yards (vegetable plots in ground paint, fruit trees, firewood), a
  walled and hedged back lane between the two house rings with outbuildings filling the gaps, the walled churchyard,
  the square, a mill stream (`wat_stream_straight` in a sculpted valley) under B2's two stone bridges, sandbags and
  wooden X at the four approaches; then a green ring of bocage sector fields (radial hedgerows lined with trees) with
  orchards, meadow, wheat stubble and ploughed fields (ground paint), three farms with walled yards, and sunken lanes
  on the south and west roads; beyond the border irregular hamlets of 3-6 houses, woods and fields (`extra.decor` /
  `extra.beautify`: never furnished, outside the navmesh tiles). The layout runs
  twice: pass 1 records the building footprints and levels the ground under them, pass 2 places everything on the
  levelled, quantised grid. Assault A-D, TDM team spawns (`team` 0 / 1) and a supply crate at every spawn. It is a
  `"userMap": true` template, so `--selftest-maps` runs the generic checks and reports (does not gate) its frame time,
  and its `views` are the M1 screenshots. It also writes the "Chateau town" column of
  `docs/asset-usage-default-maps.md`. Dev hooks: `NMK_MAPS_ONLY=chateau_town` limits `--selftest-maps` /
  `--render-map-thumbs` to one template; `NMK_MODES_MAP=templates/chateau_town.nmap.json` runs `--selftest-modes`'
  live matches on it.

- `promote_map.py <map.nmap.json> <id> "<Name>" --order N [--description ...] [--thumb x,y,z,yaw,pitch]` turns a
  saved map into a New Map template on the same procedural ground (template seed and size), with its instances,
  scenario and cameras; `playable` when it has objectives. Shipped this way: `tank_range` (Tank Range, from
  `content/examples/tank_range.nmap.json`) and `village_assault` (Normandy Village Assault, the user's `battle1`).
  `--selftest-maps` checks them as `"selftest": "authored"` templates (loads whole, scenario as authored and
  validated, save and reopen, preview / first-person / overview views), not against the SPEC 3 default-map rules.

A template's own `width` x `depth` is kept as authored even off the New Map dialog's 100 m size step. Steep ground
(about 45 deg and over) renders as bare rock with a scree band from about 28 deg (terrain shader), so cliffs need no
assets. `--selftest-maps` treats a template whose map file has no `scenario` as a terrain base: size, relief, slope
statistics, cliffs over 60 deg, a walkable route (steps up to 45 deg) from the attackers' edge to the ridge top, NaNs,
save and reopen, shader errors, and the `views` screenshots.

## Adding a map

1. Copy `maps/example_small.py` to `maps/<id>.py`.
2. Sculpt with a zone table and stamps, then call `grid.quantise()`.
3. Place structures, then linear features, then defences, then scatter last (it fills around what is already there).
4. Add objectives, then spawns, then `validate()`.
5. Write `content/templates/<id>.json`, `<id>.height.png` and `<id>.nmap.json`. Bump the template `version` whenever
   the heightmap changes, because saved crater damage is tied to it.
6. Render the preview with `NormandyMapmaker.exe --render-map-thumbs`, then copy `selftest_out/<id>.thumb.png` into
   `content/templates/`.
7. Add the map's column to `docs/asset-usage-default-maps.md`.
