# Lights: light assets, interior lighting and lamp placement (L3)

This is the data side of local lights (`src/lights.h`, `src/lights.cpp`). It decides which lights exist, where they are, how bright they are and whether they are on. The lighting engine (`src/lighting.cpp`, stream L2) renders them.

- **Plan:** `docs/plan-lighting-textures.md`, section L3.
- **Shared type:** `src/light_types.h` (`LightDesc`). L2 has a byte-identical copy.

## 1. Interface for the lighting engine

| Call | What it does |
|---|---|
| `lightsGather(app, out)` | **Appends** every lit light of the map to `out`. Clear `out` first. Call it once per frame. |
| `lightsEmissiveScale(app, inst)` | Returns 0 (dark) to 1 (full). Multiply the instance's `emissive` materials by it: glass, mantles, flames and embers go dark with the light. |
| `lightsSetDaylight(d)` | Daylight from 1 (noon) to 0 (night). L2 calls it from its time of day. A negative value, the default, derives daylight from the sun's elevation (`R.sunDir.y`): full day above about 15°, night below the horizon. |
| `lightOffReason(app, inst, k)` | Why light `k` of an instance is dark, or `""` when it is lit. Used by the tests and the editor. |

`LightDesc.id` is `instance id * 16 + light index`, so it stays stable across frames for flicker phase and shadow-slot caching. `intensity` already includes the time-of-day factor and the editor gain. `flicker` is passed through, and L2 animates it.

**Emissive materials.** A material may carry `"emissive": [r, g, b]`, a linear HDR multiplier. The light assets set it on:

| Material | Emissive |
|---|---|
| `lamp_glass` | 1.6, 1.15, 0.62 |
| `bulb` | 6.0, 4.6, 2.9 |
| `gas_mantle` | 6.0, 5.0, 3.2 |
| `flame` | 7.0, 3.4, 0.9 |
| `ember` | 3.2, 0.9, 0.18 |

L2 parses and renders the field.

## 2. The `light` block

An asset carries its lights in a `"light"` block: one object, or an array of up to 16. It sits next to the `"prop"` block, which provides breakage, glass and support (`docs/interiors.md`). It is cached per asset (`lightDef()`), the same way `InteriorSys::prop()` caches `prop`.

| Field | Default | Meaning |
|---|---|---|
| `color` | `[1, 0.75, 0.45]` | Linear RGB |
| `intensity` | 1 | 1 = a 60 W bulb seen at 1 m |
| `radius` | 6 | m; the light falls to zero here |
| `offset` | `[0, 0, 0]` | Asset space |
| `dir`, `cone` | `[0, -1, 0]`, none | `cone: [inner, outer]` in degrees makes a spot light. Without `cone`, it is a point light. |
| `flicker` | 0 | 0 is steady, 1 is a strong fire flicker |
| `shadow` | 0 | 1 = may get one of L2's shadow maps |
| `group` | none | A prop group, by index or name (see §3) |
| `use` | `interior`, or `fire` when `flicker` ≥ 0.3 | `street`: off by day, on at dusk and night. `interior`: `kInteriorDay` (25 %) by day, full at night. `fire`: 80 % by day, full at night. `electric`: always full (bunkers). |
| `fx`, `fxSize` | none | `flame` or `flame_smoke`: particles while lit (§6) |

## 3. On and off

A light is dark when any of these holds, checked in this order:
1. **Switched off:** `extra.light.on == false`. Set from the editor, or by Apply (§7).
2. **Ruin:** it is a generated piece of a ruined building (§4).
3. **Broken:** the prop is broken or crushed by a tank, unless the light's group is one of the prop's `keep` groups.
4. **Glass broken:** the light's group is the prop's `glass` group, and the glass is broken.
5. **Room breached:** it is a generated piece (`extra.furnish {host, slot "r<k>/..."}`), and a wall bay around room `k` of its host is `PANEL_BREACHED`.
   - The bays are the host's panels overlapping the room's box, grown by 0.45 m, within its storey.
   - Opening trim is not counted.

All of these run through the existing damage paths: `bulletHit`, `breakProp`, `blast`, `checkSupport`, `tankContact`, and wall stamping. Those paths already replicate over LAN: client impacts and explosions are world events replayed on clients and on late joiners. Lights therefore need no network code of their own.

**Changes in `interior.cpp`** (small hooks for lights):
- **`PropDef::hang`** (`"hang": true` in `prop`):
  - the piece hangs by its origin, which is its top, from a ceiling or beam;
  - `supported()` casts up 0.15 m instead of down;
  - `checkPlacement` finds the ceiling above the cursor.

  When a floor or beam goes, the lamp falls and breaks.
- **`PropDef::gmin/gmax`** are the glass group's bounds. A round landing there shatters the panes of a metal or masonry prop, such as a lantern in an iron frame, and passes through (returns 2). A round on the iron post still stops (returns 1).
- **Collider:** cosmetic light props keep one invisible collider box over the glass, so rounds can hit them. The navmesh ignores invisible colliders on `nav: none` props (`ai_core.cpp`).

## 4. Interior lighting

`lightsPlanRooms()` runs inside `InteriorSys::furnish()`, after the room templates. Its pieces are ordinary template pieces:
- tagged `extra.furnish {host, slot}`;
- regenerated with the building;
- kept when moved by hand;
- left out when deleted.

They come with auto-furnishing (`autoFurnishAll` on open and new map, `autoFurnish` on placement), with no undo step and no dirty flag.

**Per room** (from `InteriorSys::rooms()`):

| Room type | Light |
|---|---|
| Kitchen, office, classroom, tavern, hall | Ceiling lamp (`furn_lamp_ceiling`). Kitchens, offices, classrooms and taverns also get an oil lamp (`furn_lamp`) on a table if there is no lamp yet; halls get a candle. |
| Sitting room | Table lamp (`furn_lamp_table`) or oil lamp on a table. A ceiling lamp too from 12 m². |
| Bedroom | Candle (`furn_candle`) or oil lamp on a table or dresser |
| Utility / storage | Hanging lantern from 6 m², or a candle |
| Workshop, barn | Hanging lantern (`lamp_hanging_lantern`) |
| Church nave | Hanging lantern, and a candelabra (`furn_candelabra`) on the altar |
| Any room with a hearth | A fire (`furn_fire_hearth`) in front of the sooted firebox back |
| Bunkers and casemates | Caged bulkhead lamp (`furn_lamp_caged`) in every room |

Placement rules:
- **Table lights** go on the highest surface of the best piece in the room (tables first, then dressers and commodes, then chests), clear of what already stands there. Shelf boards and hutch tops are never used.
- **Fallback:** a room with neither kind of light still gets a ceiling lamp.
- **Ceiling lamps** hang at the highest ceiling or beam found over the cells nearest the room's centre. A hit lower than 1.9 m (a bunk, a shelf) is ignored, and so is a spot where the lamp's box would clip a beam (the editor's solid check).
- **Low ceilings:** where a pendant would leave less than 2.05 m to walk under, the flush caged lamp is used instead. Nothing is hung lower than 1.75 m.
- **Rooms that already have a light:** the room templates sometimes put oil lamps and candlesticks on tables, and those have lights too. A room that already has one gets no extra table light.

**Not lit:**
- **Rubble and ruins:** `ruin_*`, `rub_*`, `vil_church_ruined`, `vil_chateau_keep`, `aw_r612_unfinished` and `bch_lcvp_wrecked` (`lightsRuinedAsset()`).
  - V4 is adding a shared `isRuinedAsset()` with the same list. **At merge, replace the local list with it.**
  - Pieces that the room templates put into those buildings (for example candlesticks in the keep) stay dark: reason `ruin`.
- **Landing craft** (`bch_*`) are not buildings.
- **Open-topped emplacements** (`aw_tobruk_mg`, `aw_tobruk_mortar`) have no ceiling to hang a lamp from (`lightsRoofed()`).
- **Hearth fires** step forward out of the fireplace's own collider until they stand free on the hearth. A hearth with no free spot gets no fire.

**Bunkers** (`bunker_*`, `aw_*` and the Bunkers / Atlantic Wall categories) go through `furnish()` in a lights-only mode:
- `extra.interior.lightsOnly` is set, and no room templates are used;
- `hideBuiltin()` stays false, so their own bunks and tables stay;
- `sync()` treats them as generated hosts, so the lamps follow the bunker when it is moved.

## 5. The assets

**Outdoor** (`tools/gen_env_assets.py`, section "L3 lights", category **Lighting**):

| Id | What | Light |
|---|---|---|
| `lamp_gas_street` | Cast-iron gas lamp, ~3.6 m: fluted column, ladder bar, four-paned lantern with a mantle | street, 3.0, 14 m, shadow |
| `lamp_lantern_post` | Wrought-iron post, ~2.7 m, with a paraffin lantern hanging from a scrolled arm (+z) | street, 2.0, 11 m, shadow |
| `lamp_wall_lantern` | Iron wall lantern on a bracket (wall-hung, `mount` 2.0 m) | street, 1.4, 9 m |
| `lamp_hanging_lantern` | Paraffin storm lantern (`hang`): porches, barns, workshops | interior, 1.0, 7 m |
| `fire_brazier` | Iron fire basket on three legs; flames and smoke | fire, 2.5, 9 m |
| `fire_campfire` | Stone ring, log teepee, embers (cosmetic); flames and smoke | fire, 3.0, 10 m |

**Indoor** (`tools/gen_furniture.py`, class `A`, category **Furniture**):

| Id | What | Light |
|---|---|---|
| `furn_lamp` (existing) | Oil lamp; now has a flame in the chimney | interior, 0.45, 4 m |
| `furn_candlesticks` (existing) | Pair of candlesticks; now with flames | interior, 0.22, 3 m |
| `furn_stove` (existing) | Cooking range; now has glowing draught slots | fire, 0.3, 2.5 m |
| `furn_lamp_table` | Electric table lamp: porcelain base, silk shade | interior, 0.9, 5.5 m |
| `furn_candle` | Tallow candle in an enamelled chamberstick | interior, 0.12, 2.5 m |
| `furn_candelabra` | Five-branch brass altar candelabra | interior, 0.55, 4.5 m |
| `furn_lamp_ceiling` | Pendant: green enamel shade on a cord (`hang`) | interior, 1.6, 8 m, spot 70/105°, shadow |
| `furn_lamp_caged` | Bunker bulkhead lamp: globe, wire guard (`hang`) | electric, 1.3, 7 m, shadow |
| `furn_fire_hearth` | Log fire: logs, embers, ash, flames | fire, 1.8, 7 m, shadow, flame particles |
| `furn_stove_potbelly` | Cast-iron heating stove with a glowing grate | fire, 0.5, 3.5 m |

**Breakage:**
- Lanterns and street lamps keep their post or bracket when broken (`keep`), and the lantern falls off as a fragment.
- A round in the panes puts the light out.
- Tanks crush them, like furniture.

**Regenerating the assets.** Run only these:

```bash
$TEMP/nmk/py/python.exe -I tools/gen_furniture.py .
```

```bash
$TEMP/nmk/py/python.exe -I tools/gen_env_assets.py
```

```bash
git checkout content/assets/gun_flak18.json
```

```bash
"C:/Program Files/nodejs/node.exe" tools/build_mpu_assets.mjs
```

`gen_env_assets.py` also rewrites `gun_flak18.json`, which is out of step on master (W2's `build_bunkers.mjs` merges into it), so restore it as shown. `build_mpu_assets.mjs` gives `lamp_*` and `fire_*` the furniture nav rule: blocking props carve, cosmetic ones are `none`. Afterwards, `build_mpu_assets.mjs --check` reports only `gun_flak18.json`, as before.

## 6. Fire particles

`lightsStep()` is called from `InteriorSys::step`, in Explore only. For every lit light with `fx` within 40 m of the camera (16 at most), it emits flame tongues using the wreck-flame recipe (`tank_sim.cpp`, the `flame` lambda):
- additive, short-lived and licking upward;
- velocity and lift scaled by `fxSize` against the tank fire's 0.55 m;
- an occasional `Fx::flash`;
- for `flame_smoke`, an occasional grey `Fx::smoke` puff.

## 7. Save and Apply

| What | Where it is saved |
|---|---|
| Authored switch, colour and gain | `extra.light {on, color, gain}`. `gain` multiplies the asset's intensity. |
| Session breakage | **Apply damage to map** (`InteriorSys::applyToMap`) calls `lightsApplyToMap`, which writes `extra.light {on: false, applied: true}` on every light put out this session (broken, glass, breach). It follows the `propBroken` pattern. **Clear applied damage** removes exactly those. |

A glass-only break is not otherwise persisted: a lamp that was only shot out keeps its pane look until reload, but stays dark.

## 8. Placement

- **Auto paths:** the Paths menu has **Line auto paths with lamp posts** (`gAutoPathLamps`). **Connect houses** then lines every new route with `lamp_lantern_post`:
  - spaced 22 m from any other street lamp, staggered across the route;
  - 0.75 m beyond the path edge, never on a path, in water, on steep ground or against a wall, tree or fence;
  - the solid check ignores auto pieces and bodies of instances already removed from the doc (physics syncs later), so a re-run lays out the same posts;
  - not within 3.5 m of a route's ends (doors);
  - tagged `extra.auto = "lamp"`, so `autopathRemove` and re-runs remove them with the paths;
  - the report counts them (`AutoPathReport::lamps`).
- **Beautify role `street_lamps`** (V8 `beautify.cpp` is not merged yet). The coordinator applies this at merge:

  ```json
  {"name": "street_lamps", "slider": "dressing", "assets": ["lamp_gas_street"], "along": "paths", "spacing": 26, "pad": 0.9, "maxSlope": 12, "inside": true, "ring": false}
  ```

  In `beautify.cpp`'s role loop, roles with `"along": "paths"` do not scatter. They line the map's paths and routes instead:

  ```cpp
  if (R.value("along", std::string()) == "paths") { placed += lightsLineRoutes(app, lightsPathLines(app), assetFor(R), R.value("spacing", 26.0f), "beautify"); continue; }
  ```

  `lightsPathLines()` returns every path instance's centre line plus `doc.extra.routes`. Use the role's own tag in place of `"beautify"` if V8 tags its placements differently.
- **Editor:** the Selection panel has a **Light** section for any light asset (`lightsEditorPanel`, called from `InteriorSys::editorPanel`). It shows:
  - lit or dark and why, with the share of output at this time of day;
  - **On**, **Colour** and **Intensity**, each one undo step;
  - **Reset light**.

  Edits on a generated piece mark it `furnish.manual`, so a regenerate keeps it. A selected building shows a **Lights** section with its light count and **All lights on** / **All lights off**.

## 9. Tests

- **`--selftest-interiors`**, step "lights", which runs on the all-buildings placement:
  - every intact furnished building is lit, and at least 80 % of its rooms have their own light;
  - ruins and landing craft are dark;
  - bunkers are lit and keep their own furniture;
  - a ceiling lamp hangs with at least 1.75 m clear under it and is supported from above;
  - by day, street lamps are off and interior lamps drop to 25 %;
  - a round on a street lamp's post stops and the lamp stays lit; a round in its panes passes and puts it out;
  - a table lamp shot out, a ceiling bulb shot out and a blown-over street lamp each leave `lightsGather`;
  - Apply and Clear;
  - a breached bay of the room puts its lamp out, and a far bay does not;
  - the editor switch and the emissive scale;
  - reset relights.

  The furniture-catalogue check accepts one collider on a cosmetic light prop. The skip-rule check counts bunkers' non-light pieces.
- **`--selftest-autopath`**, step "battle1: lamp posts along the auto paths":
  - lamp count, one undo step;
  - spacing ≥ 22 m, nothing on a path, every lamp within 3.5 m of its route;
  - lit at night and dark by day;
  - re-run replaces them, and **Remove** removes them.
- **`--selftest-net`** (host):
  - the host shoots a ceiling lamp's bulb with a real projectile;
  - clients A (live) and B (late join) report it in `lightsOut`.
