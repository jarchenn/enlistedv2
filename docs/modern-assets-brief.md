# Modern asset set: shared authoring brief (MOD1)

Repo: `C:/Users/50bmg/Documents/MapMaker/wt-ug2` (branch `modern-assets`). C++/OpenGL map editor + FPS ("Normandy Mapmaker").
Assets are authored in JS catalogs (`tools/*.mjs`) and written to `content/assets/<id>.json` by
`tools/build_mpu_assets.mjs` (the integrator runs it; you do not).

## Rules for every authoring agent
- Write ONLY the file(s) your task names. Never edit shared files (`tools/build_mpu_assets.mjs`, `src/*`,
  `content/settings/room_dressing.json`, other catalogs) unless your task says so. No git commands.
- Never run `node tools/build_assets.mjs` or `node tools/build_mpu_assets.mjs`. Do not start the game.
- Python is not on PATH: `$TEMP/nmk/py/python.exe -I` if you need it (Git Bash).
- Self-check before you finish: `node -e 'import("./tools/<your file>.mjs").then(m=>{for(const A of m.<fn>()){const j=A.json();console.log(j.id,j.parts.length)}})'`
  must run without errors; every id unique, prefixed `mod_` (furniture `furn_mod_`).
- Original work only: no logos, brand names, readable text or trade dress of real companies or games.
  A "Target / Walmart style" store is a generic big-box retailer; a fast-food place is a generic burger diner.

## Kit (read `tools/kit.mjs` first, ~120 lines)
- `new Asset({ id, version: 1, name, category, description, footprint: [x, z], floors, enterable, plinthDepth: 0, maxGrade, noAutoPanels: true })`.
- `A.mat(name, albedo[rgb 0..1], roughness, pattern, extra)`; pattern one of plain, stone, wood, plaster, slate, tile, metal,
  glass, fabric, brick, painted, concrete, bunker. `extra`: `{ texture: "<texture set or slot name>", emissive: [r,g,b], metallic, grain }`.
  Texture sets you may name directly (`texture: "<set>"`): concrete_aged, concrete_light, concrete_rough, concrete_stained,
  concrete_aggregate, concrete_formwork, paving_worn, floor_cement_tile, slate_square, brick_factory, brick_muted,
  brick_worn, render_lime, plaster_painted, paint_metal_grey, paint_metal_green, paint_metal_worn_white,
  paint_wood_white, steel_scratched, steel_rust_light, iron_worn, corrugated_galv, wood_oak, planks_038, wood_dark,
  rug_beige, cloth_grey, leather_brown, gravel_fine, grass_lawn, sand_054, rubble_grey, debris_brick ...
  (full list: `ls content/textures`). Glass: pattern "glass".
- Parts: `A.box(min, max, mat, o)`, `A.obox(centre, size, eulerDeg, mat, o)`, `A.cyl(centre, size[dx,h,dz], mat, {n, r, top})`,
  `A.cone`, `A.arch`, `A.wedge`, `A.prism`. Options `o`: `{ k: 0 }` = no collider (detail), `{ v: 0 }` = invisible
  (collider only), `{ lod: 1 }` = detail that may drop at distance, `{ bv: 0.02 }` = bevelled box edges (mesh only).
  Big solid pieces collide (default); trims / small detail `{ k: 0, lod: 1 }`.
- Nav (`setNav`, `room`, `cover`, `rng` from `tools/catalog_atlantic.mjs`): every asset calls
  `setNav(A, { effect, rooms: [], doors: [], entrances: [], corners: [], firingPoints: [], cover: [], areas: [] })`.
  Props: `effect: "carve"` (or "none" for things you can walk over / overhead). Buildings: `effect: "block"` with rooms.
  Give cover points (`cover([x,0,z], yawDeg, "low"|"high", ["over"|"left"|"right"])`) on anything a soldier can hide behind.
- Units are metres, +y up, the asset's front faces local +z. Origin on the ground at the footprint centre.

## Fidelity bar (the user asked for "high fidelity, enough texture and depth")
- No flat untextured slabs: every visible surface gets a texture set or a patterned material, real thickness,
  bevels on hard edges (`bv`), trims (frames, caps, kerbs, sills, mullions, handrails), and secondary detail
  (bolts, seams, vents, drains, signs as blank coloured panels, cabling) as `{ k: 0, lod: 1 }` parts.
- Proportions true to life (door 2.1 m, storey 3.0-3.5 m, kerb 0.15 m, car 4.5 x 1.8 x 1.45 m, lane 3.5 m ...).
- Modern palette: concrete, glass curtain walls, metal panels, render, brick slips, painted steel, asphalt,
  pavers, rubber surfacing (playgrounds), timber decking.

## Enterable buildings (buildings agents)
- Follow `tools/catalog_gx_buildings.mjs` (`brickTownhouse()` ~l.416, `cornerCafe()` ~l.482, `pensionHotel()` ~l.843)
  and the `house(A, c)` builder in `tools/catalog_chateau.mjs` (~l.110: X, Z, T, h, n, win, wd, parts, names, doors,
  roof, dress). Walls with real openings, door interactables (`door` kind with `aperture`) and window interactables
  (`_casement` / `window` kind with `aperture`) - the auto-furnisher keeps wall items off them only if they exist -
  stair cores, floors per storey, nav rooms with ids, tour.
- Room types for the auto-furnisher: `A.meta.roomTypes = { <navRoomId>: "<type>" }`. Types available after
  integration: kitchen, sitting, bedroom, utility, workshop, hall, office, class, tavern, and NEW: bathroom, shop, dining,
  prokitchen (commercial kitchen), ward (hospital), gym, lobby. Give every room a type.
- Do NOT put loose furniture in the building yourself unless it is fixed (counters, fixed seating, built-in kitchen
  lines, lockers, bleachers, shelving runs, pool, ticket desks): the auto-furnisher dresses rooms by type.
  Fixed fittings that define the building (fast-food counter, fryer line, hoods; retail shelving gondolas;
  checkout lanes; hospital nurse station; gym bleachers; ticket barriers; fuel pumps) are modelled as part of the
  building asset (parts, collide) or as separate `mod_` props placed via `A.meta.companions =
  [{ asset, pos: [x, y, z], yaw }]` (placed with the building from the palette).
- Max 7 storeys for any building. Roof access (stair to a roof door / hatch with a parapet) where asked.
- Keep each building asset under ~1500 parts.

## Delivery
- Your catalog exports one function returning an array of Assets; list every id with a one-line description at the
  end of your final report, plus anything the integrator must do (room types used, companions, new line types).
