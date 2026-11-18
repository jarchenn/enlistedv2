# RB1: three reference buildings recreated

The user supplied three reference models (`C:\Users\50bmg\Downloads`):
- `abandoned__building__shop__old__house.glb`;
- `old-house.zip`;
- `building-container-metal.zip`.

They were extracted to `E:\nmk\incoming\rb1\` and opened only through Blender background scripts (`E:\nmk\rb1\study.py`,
`measure.py`, `islands.py`, `interior.py`, `refcams.py`). From those scripts came:
- renders from front, back, both sides, roof and four 3/4 views, plus eye-level views, interiors and a material-ID pass;
- ray-cast elevations: facade planes, openings and roof profiles;
- per-material island bounds: each pane, door, board and panel.

Their licences are unknown, so nothing of them is in the repo: no mesh, no texture. Each building is rebuilt through
`house()` in `tools/catalog_rb1.mjs`, registered in `tools/build_mpu_assets.mjs`. That gives it:
- rooms with room types for auto-furnish;
- doors and windows as interactables;
- damage panels, colliders, nav and a walkthrough tour;
- `"ruin": true`, so the furnishing is abandoned.

Every surface uses an image-based `replace` slot (`rb_*` in `content/textures/textures.json`). Test map:
`content/templates/rb1_test` (Play > user maps, team deathmatch, a spawn in front of each; editor; selftest
`authored`).

Coordinates below are the game's: x to the right seen from the front, y up, z out of the front. "GF" is the floor at 0.3 m.

## 1. Container cabin (`rb1_container_cabin`)

Reference: an FBX, one mesh and one 25 MB texture atlas. It's a painted sheet-steel cabin on a timber skirt, modelled about 1.8× over size: its doorway is 3.97 m tall. Recreated at ×0.56 (doorway 2.22 m).

| Item | Reference (model units) | Game (m) |
|---|---|---|
| Box (outside the walls) | 14.0 × 5.96, roof 5.66 | 7.84 × 3.34, flat roof at 3.1 + 0.08 deck, 0.14 rim |
| Skirt | 0–0.66, boards in 3.14 bays, 0.16 posts | 0–0.37, bays ≈ 1.76, 0.1 posts proud 0.16 |
| Front window (boarded) | x −4.98…−3.0, z 2.68–4.21, frame 0.13 | c −2.23, 1.11 × 0.86, sill GF + 1.2 |
| Rear windows (boarded) | x 2.0…3.98 and −4.98…−3.0 | c 1.67 and −2.23, same size |
| Doorway | x 2.98…5.32, z 0.67–4.64, steel posts 0.13, header 4.05 × 0.44 | door 1.2 × 2.15 at x ≈ 2.2; posts 0.08; header 2.27 × 0.25 |
| Step | 2.38 × 0.53 at z 0.29 | 1.34 × 0.3 × 0.16 |
| West vent | 3.6 × 2.57 frame, six slats 3.05 long | 2.0 × 1.2 frame, six slats 1.88 long, dark void behind |
| Rooms | one open box | office (west, 4.6 m) and entry store (east, 3.0 m) |

Materials:
- walls: olive paint, `rb_metal_leaks`;
- lining (walls, partition, ceiling): the same paint, lighter;
- floor: worn boards, `floor_boards_old`;
- skirt and frames: `boards_weathered`;
- window boards and door leaf: `rb_planks_old`;
- door posts and header: `iron_rust`;
- roof: `rb_metal_leaks_b`;
- rust run-off: about 30 streaks per long side from the roof edge, with thinning tails, `rb_rust_patch`.

## 2. Log house (`rb1_log_house`)

Reference: `older_house_2.blend`, a Russian izba at real size (×1.0).

| Item | Reference | Game |
|---|---|---|
| Log box | x −5.2…2.32, y −3.44…1.88 (7.52 × 5.32), walls z 0.69–3.1 | X 3.76, Z 2.66, T 0.26; logs 0.75–3.1 |
| Logs | round, about 0.26 dia, ends crossing 0.25 at the corners | cylinders 0.26, courses 0.26, side walls half a log up, end grain `rb_log_end` |
| Plinth | vertical boards 0.08–0.79 | vertical boards −0.05–0.76 with joints |
| Floor | 0.8 (door sill) | 0.3 (GF); windows kept at the reference's height above ground |
| Roof | gable, ridge 5.58 along the long side, eaves 3.1, overhang about 0.6 (front further over the porch), gable overhang about 0.5 | rise 2.48, ov 0.6, gov 0.5; 0.2 boards down both slopes, ragged at the eaves |
| Windows | 7 objects, frames 1.4 × 1.68; glass 2 leaves 0.68 × 0.95 at z 1.58–2.53 | 1.0 × 0.95, sill GF + 1.28; front c 2.05 / −2.03, rear 2.12, east −1.16 / 1.09, west −1.12 / 1.12 |
| Surrounds | carved white boards, peaked crown | side boards, sill board, apron with drops, head board, prism crown, boss; shutters on all but the rear |
| Door | 0.85 × 1.78 at x −0.98, sill 0.8 | 0.9 × 2.25 at x 0.47 (house() door) |
| Porch | posts about 1.2 out, small roof | deck and step, two posts 1.2 out, beam, 18° board lean-to |
| Chimney | 0.52 × 0.57, z 3.78–5.85, rendered | the same at x −0.82, z −1.14, `plaster_rough`, cap |
| Gables | vertical boards, attic hatch | `boards_weathered` gable, a board hatch in a trim frame |
| Inside | log walls throughout, board ceiling | log courses on the inner faces, `rb_wood_rough_old` lining and ceiling |
| Rooms | one room with a log partition | stove room (sitting), entry kitchen (door), store (utility) |

## 3. Weapon shop (`rb1_weapon_shop`)

Reference: a GLB with a 48 m ground plane (excluded), modelled about 2× over size: the door panel is 3.2 × 4.6.
Recreated at ×0.5, and turned so the door gable is the front (game x = reference y × 0.5, z = reference x × 0.5).

| Item | Reference | Game |
|---|---|---|
| Box | 20 × 14, walls 7.29 (low) … 9.29 (high side) | 7 (gable) × 10, walls to 3.65, roof to 4.85 on the counter side |
| Roof | corrugated skillion, rising to 9.74 over the high side, patched | `corrugated_galv` sheet sloping west to east, three rust patches |
| Clerestory | mesh panels 3.9 × 2.1 at z 7.0–9.17 on the high side | five 2.0 m mesh panels in timber frames between the eave and the roof, dark void behind |
| Verandah | plank lean-to 6.3 → 4.9 over 6 m on steel posts at 12.6 | posts 3.0 out every 2.5 m, roof EAVE − 0.5 → 2.46, rafters, wrapping the front corner |
| Counters | green painted steel with boarded hatches | green panelling 0–1.0, three boarded hatches 1.6 × 0.9 at z −3 / 0 / 3, counter boards |
| Door | panel 3.2 × 4.6 on the gable, WEAPON sign 1.54 × 0.67 | orange double door 1.6 × 2.15, sign board 2.0 × 0.45 lettered WEAPONS |
| Low side | two double windows 2.84 × 2.5, sill 2.33 | c −1.88 / 2.12, 1.42 × 1.25, sill GF + 0.87, white frames |
| Inside | open board hall, loft on beams, a post, counter | one open shop room, a 6.4 m counter, a loft over the rear 3 m on beams and a post |

Materials:
- walls: weatherboard, `rb_siding_weathered`;
- lining: `rb_siding_grey`;
- floor: `rb_floor_dirty`;
- counters: green paint over rust, `rb_paint_green_rust`;
- door: orange paint over rust, `rb_paint_orange`;
- posts: `steel_rusty`;
- mesh: `iron_rust`;
- frames, counter and loft: `beam_weathered`;
- verandah roof: `rb_planks_old`;
- window frames and sign: `rb_painted_wood_old`;
- stains below the windows.

## Texture sets added (ambientCG, CC0, `tools/texture_sets.json`)

`rb_siding_weathered` (WoodSiding008), `rb_siding_grey` (WoodSiding009), `rb_metal_leaks` (Metal024), `rb_metal_leaks_b`
(Metal025), `rb_paint_green_rust` (PaintedMetal006), `rb_paint_orange` (PaintedMetal008), `rb_log_end` (TreeEnd002),
`rb_wood_rough_old` (Wood035), `rb_planks_old` (Planks039), `rb_painted_wood_old` (PaintedWood009C), `rb_floor_dirty`
(WoodFloor067B), `rb_rust_patch` (Rust009). Existing sets used through new `rb_*` slots: `boards_weathered`,
`floor_boards_old`, `iron_rust`, `plaster_rough`, `corrugated_galv`, `corrugated_blue_rusty`, `steel_rusty`, `beam_weathered`.

## Reference textures (CC BY 4.0)

Two of the references are licensed CC BY 4.0, with the user's lines:
- "Abandoned | Building | Shop | Old | House" (https://skfb.ly/o7pvZ) by YadroGames;
- "Building | Container | Metal" (https://skfb.ly/osPME) by YadroGames.

For those two, regions of the reference's own texture atlases are cropped into texture sets: `E:\nmk\rb1\crop.py`,
specification in `E:\nmk\rb1\crops.json`. Each crop is turned and unmirrored where the atlas stores it sideways, at
most 1024 px, packed by the engine. They are marked `local` in `tools/texture_sets.json`, credited in each
`meta.json`, in `content/textures/manifest.json` and in `docs/credits.md`. The meshes are still not imported.

| Set | Source | Used on |
|---|---|---|
| `rb_ref_cab_wall` | container atlas: an olive wall panel | the cabin's walls, outside and in |
| `rb_ref_cab_boards` | container atlas: the window boarding | the boarded windows' planks |
| `rb_ref_shop_wall` | shop wall atlas: weatherboard | the shop's walls and gable infills |
| `rb_ref_shop_door` | shop environment atlas: the orange door leaf | the door leaves |
| `rb_ref_shop_sign` | shop environment atlas: the WEAPON sign | the sign board over the door (replaces the generated lettering) |
| `rb_ref_shop_green` | shop environment atlas: a green counter panel | the counter panelling |
| `rb_ref_shop_roof` | shop roof atlas: the corrugated sheets | the skillion roof |

The replace slots divide a set by its mean colour. So each of these materials carries the crop's own mean colour
(linear, from `E:\nmk\rb1\means.py`), and the reference's colours come through. The old house has no licence: it
stays reference-only, on library and ambientCG sets.

## Where the recreations still differ

- **Cabin:**
  - The reference's doorway has no leaf; ours has a weathered plank door, so it works as a door.
  - The reference is one open box; ours has a partition (office and entry).
  - Wall trims (skirting, dado) come from house().
- **Log house:**
  - The floor is at 0.3 m, not 0.8 m, so the porch has one step where the reference has three. The door is 2.25 m, not 1.78 m.
  - The reference's long front eave over the porch is a separate small porch roof here.
  - The interior partition is a plastered house() partition, not logs.
- **Shop:**
  - It is turned (door gable as the front) so house() can put the door where the reference has it.
  - The corner porch is a lean-to with a mesh gate beside the door, simpler than the reference's frame.
  - There is no upstairs flat, as the reference has none: only a loft.
  - The roof is one sheet with patches; the reference has overlapping sheets.
- **All three:**
  - The windows are house() casements, boarded with solid planks where the reference is boarded.
  - Graffiti and grime come from the engine's GX5 decals (urban ruin), not from the reference's own images.
