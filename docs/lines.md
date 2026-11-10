# Line features (LN1, plan Addendum 7)

Contiguous features are drawn as lines with the lasso's Line mode instead of being chained from snap pieces.

## Data model

A line is an ordinary `Instance` of a **line asset**: an asset whose `"line"` block names its line type
(`content/assets/line_<type>.json`, `"line": {"type": "trench"}`, `"hidden": true` so it stays out of the Assets
palette). Its geometry is plain data in `Instance::extra["line"]`:

| Key | Meaning |
|---|---|
| `pts` | `[[x, z], [x, z, d], ...]`: world xz control points; `d` (default 1) scales the type's depth or height there (trench: 0 = ground level, an entry ramp) |
| `seed` | per-line random seed |
| `params` | per-line overrides of the type's `params` |
| `spans` | gates and gaps by arc length: `{"s", "w", "kind": "gap" \| "field_gate" \| "gate_piers"}` (LN2) |

`Instance::pos` is the first point (yaw 0). Everything else comes with the Instance:

- save / load and undo;
- the map hash (`Net::mapHash`, LAN map identity);
- the runtime record `InstanceRT`, including `secHp` / `secCut`: one damage section per bay, so wire cuts, saving and
  replication use the existing damage events.

`src/lines.h`: `lineGet` / `lineSet` read and write the data; `lineSpline` gives the smoothed curve (centripetal
Catmull-Rom through the control points, resampled, with arc length, `d` and tangent per sample).

## Line types

`content/settings/line_types.json`: `{"types": [{"id", "name", "asset", "builder", "spacing", "params"}]}`.

- `builder` names a builder registered in C++ with `LINE_BUILDER("name", fn)`.
- `spacing` is the nominal bay length; builders stretch bays to fit the line.
- `params` are defaults; `lineParams(type, line)` merges a line's own.

Types so far:

| id | builder | What it is |
|---|---|---|
| `trench` | `trench` | Fed straight into the GX1 trench network (`terrain_blend.cpp`): carve, parapets, revetment, sandbags, duckboards, firestep, entry ramps at free ends. Lines that touch or cross form one network; end walls only at real ends. |

LN2 adds the fence, wire, wall and hedge types.

## Builder interface

```cpp
void myBuilder(const LineBuildCtx& ctx, LineBuildOut& out);
LINE_BUILDER("my_type", myBuilder);
```

- `LineBuildCtx`: the app, the instance, the line, the type, the merged `params`, the `spline` (0.5 m samples, world
  xz), `ground(x, z)` (the terrain as drawn), `local(w, y)` (world to instance space).
- `LineBuildOut` (instance space):
  - `mesh`: all materials batched, per-vertex albedo;
  - `colliders`: `Part`s, each with an optional `section` (it vanishes when `InstanceRT::secCut[section]` is set) and
    `playerOnly` (the wire traversal volume);
  - `areas`, `cover`: nav area outlines and cover points;
  - `gates`: gate bays for the door system;
  - `sections`: how many damage sections the line has (secHp from the type's `sectionHp`).

The engine rebuilds a line's output when the instance changes (`rev`) or the ground under it does.

## LN2: fence, wire, wall and hedge lines

`builder: "bays"` (`src/lines_ln2.cpp`). A line is laid out in **bays**, each one copy of an existing piece (the type's
`pieces`, weighted, chosen from the line seed), stretched together to fill each run between gates and gaps exactly:

| Type | Pieces | Damage | Gates |
|---|---|---|---|
| `wire_fence` | `wire_fence` | bay = section (60 hp) | gaps |
| `concertina` | `wire_concertina` | bay = section (60 hp) | gaps |
| `knife_rest` | `wire_knife_rest` (chains of 2+ migrate; single knife rests stay obstacles) | bay = section (60 hp) | gaps |
| `minefield_fence` | `bch_minefield_fence` (its minefield nav area per bay) | none | gaps |
| `post_wire_fence` | `fence_post_wire` | none | field gate |
| `picket_fence` | `drs_picket_fence` | none | field gate |
| `stone_wall` | `stone_wall_low` | none | field gate |
| `garden_wall` | `drs_garden_wall` | none | gate piers |
| `sandbag_wall` | `drs_sandbag_wall` | bay = section (360 hp) | gaps |
| `sandbag_low` | `def_sandbag_straight` (curved pieces migrate into it) | bay = section (300 hp) | gaps |
| `hedgerow` | `bcg_hedgerow_straight` 85 % / `hedgerow_bocage` 15 % | none (as the pieces) | field gate |

- **Geometry:** the piece's part geometry is mapped vertex by vertex along the spline: u (along the piece) becomes arc length, v (across) the offset along the curve's normal, y is lifted by the ground under the centreline. Posts stay upright; rails, strands, courses and banks follow curves and slopes. One batched mesh per line. Snap end caps (`"cap"` parts, the rounded hedge-bank ends) are drawn only where a run ends: at the line's ends and either side of a gate or gap.
- **Supplied models and canopies:** the wire obstacles' models and the hedgerows' canopies (the L1 species leaves) are placed rigidly per bay on the chord between its ends. These draw as the pieces did, so not batched.
- **Colliders:** each collide part's corners are mapped and boxed in the curve's frame (`LineCollider`, with the piece's `playerOnly` traversal volumes and the bay's section).
- **Nav:**
  - the line asset's nav effect classifies the bay colliders (wire: `unsafeVault`, hedges: block, fences and walls: carve);
  - `ai_core.cpp` marks wire footprints, hedge banks and the pieces' nav areas per uncut bay;
  - the pieces' cover points are mapped per bay (`ai_cover.cpp`).
- **Damage:** on types with `sectionHp`, a bay is a damage section. `App::blastDefenses` hits each bay by its own distance; a cut bay drops its colliders, mesh range (`secIdx`), model, nav marks and cover, and opens a gap. Saving, reset and replication use the existing section events. A rebuild after the ground changes keeps the cuts.
- **Gates and gaps:** spans `{s: centre arc length, w, kind}`. A gate span (`field_gate`, `gate_piers`) is an ordinary instance of the gate asset owned by the line (`extra.lineGate = {line, k}`), so doors, AI, nav and replication treat it as they treat every door. `lineSyncGates` (`App::syncRuntime`) keeps it on its span, creates missing ones and removes orphans. `field_gate` is now a door (one leaf on the left post): open is passable, closed is blocked. In Line mode, select a line and **Alt+click** on it to add a gap or gate there (kind and gap width in the panel); Alt+click on an existing span removes it.
- **Migration:** on map load (`lineMigratePieces`) and in mapgen (`tools/mapgen/mapgen/lines.py`, run by `write_map`; `tools/mapgen/convert_lines.py` for the hand-authored maps), the pieces listed under a type's `migrate` chain into lines:
  - ends within 0.6 m join through every two-way joint of one type;
  - `path` gives a piece's own polyline (curved sandbags, hedge corners);
  - `gap` is a gateway inside a piece (`bcg_hedgerow_gap`);
  - gate pieces touching a chain become gate spans.

  Those pieces are hidden from the Assets palette (`lineReplacedPiece`). Free-standing defences (hedgehogs, tobruks, sandbag rings) stay objects.
- **Tests:** `--selftest-lines` (stages 4600+, `src/selftest_lines_ln2.cpp`):
  - an agent walks through an open gate in a fence line and is stopped beside it;
  - a blast cuts wire bays;
  - save and reload keep the lines, spans and gate;
  - every default map loads with no replaced piece left.
