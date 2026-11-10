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
