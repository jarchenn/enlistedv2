# Asset definition format

Each `content/assets/*.json` file is one placeable asset. The editor, movement and interaction code read only this data. Adding a file adds a catalog entry, and none of the editor or movement code needs to change. `tools/build_assets.mjs` (together with `kit.mjs` and `parts.mjs`) generates these files from parametric Norman building components.

Coordinates are in metres in asset space. +Y is up, the front of the building faces +Z, and the origin is the centre of the footprint at the placement height. On a slope the building is placed **plumb** at the highest sampled ground point, and the plinth acts as the foundation skirt.

```jsonc
{
  "id": "house_farmhouse_2story", "version": 1,
  "name": "Two-Story Farmhouse", "category": "Houses", "description": "...",
  "floors": 2, "enterable": true,
  "footprint": [12.2, 9.2],            // x, z extents used for placement and overlap tests
  "plinthDepth": 1.2, "maxGrade": 1.0, // max ground fall under the footprint
  "materials": [ { "name": "stone", "albedo": [r,g,b] /*linear*/, "roughness": 0.88, "metallic": 0,
                   "pattern": "stone|plaster|wood|painted|slate|tile|metal|glass|fabric|brick|concrete|plain",
                   "grain": 0|1|2 /* part-local axis for wood grain / slate courses */ } ],
  "parts": [                           // static geometry
    { "t": "b", "c": [x,y,z], "s": [sx,sy,sz], "r": [rx,ry,rz] /*deg, optional*/, "m": "stone",
      "v": 0 /*invisible: collider only*/, "k": 0 /*visual only: no collision*/ },
    { "t": "p", "axis": 0, "a": [a0,a1], "tri": [[u,v],[u,v],[u,v]], "m": "stone" }   // prism (gables); axis 0: x, 1: y (tri in x,z), 2: z
    { "t": "r", "c": [x,y,z], "s": [sx,sy,sz], "rad": 0.2, "r": [rx,ry,rz] /*optional*/, "m": "concrete" }   // rounded box: every edge rounded by rad (render + matching Jolt convex radius); never a damage bay (docs/bunkers.md)
    // B1 part types (below): cylinder / cone / arch / wedge fill their box c, s, r (or "q"); "bv" chamfers a box's mesh
    { "t": "c", "c": [...], "s": [dx, h, dz], "n": 0, "seg": 0, "m": "stone" },
    { "t": "k", "c": [...], "s": [dx, h, dz], "top": 0, "n": 8, "m": "slate" },
    { "t": "a", "c": [...], "s": [span, rise, depth], "th": 0.25, "fill": 0, "m": "stone_dressed" },
    { "t": "w", "c": [...], "s": [sx, sy, sz], "top": 0.3, "m": "stone_dressed" },
    { "t": "b", "c": [...], "s": [...], "bv": 0.015, "m": "stone_dressed" }
  ],
  "furniture": [ { "name": "Kitchen table", "parts": [ ... ] } ],  // children of the building
  "interactables": [ {
    "id": "kitchen_casement", "kind": "window", "label": "window", "sound": "glass|wood|shutter|steel",
    "axis": [0,1,0], "openAngle": 100, "duration": 0.85, "defaultOpen": false,
    "aperture": { "min": [...], "max": [...] },   // aim volume while closed
    "leaves": [ { "hinge": [x,y,z], "sign": 1, "parts": [ /* boxes at the CLOSED pose, asset space */ ] } ]
  } ],
  "walkthrough": { "start": {...}, "steps": [ /* scripted traversal run by --selftest */ ] }
}
```

## Navigation metadata and terrain cuts (`src/navmeta.h`)

These optional top-level keys are read into `AssetDef::navMeta`. They are the contract between the structure generators, the navmesh and bot AI, and the terrain.

### `"nav"`

| Key | Content |
|---|---|
| `effect` | `"block"` (walls, bunkers, hedgerow banks), `"carve"` (obstacles bots walk around), `"area"` (sets an area type) or `"none"` (grass, small dressing). When `effect` is absent: enterable assets and anything over 1.6 m tall block, lower ones carve. |
| `area` | `road`, `grass`, `brush`, `marsh`, `sand`, `trench`, `minefield`, `wire` or `water`. Costs follow AI_SPEC §3.1: road 1.0, grass 1.2, brush 1.6, marsh 3.0, sand 1.3, trench 0.9. Minefield and wire are forbidden. |
| `areas` | `[{outline: [[x,z],...], area}]`: sub-areas, such as a trench floor. |
| `rooms` | `[{id, min, max}]` |
| `doors` | `[{a, b, p}]`. `b` may be `"outside"`. |
| `entrances` | `[{p, dir, room}]`. `dir` points into the room. |
| `corners` | `[{p, room}]`: clearing points. |
| `firingPoints` | `[{p, yaw, kind: window \| embrasure \| firestep \| mg}]` |
| `cover` | `[{p, yaw, height: low \| high, peek: [left, right, over]}]` |

### `"terrainCut"`

`[{outline: [[x,z],...], depth, bank}]` lowers the ground inside the outline by `depth` metres. `bank` is the width of the sloped edge. Trenches, AT ditches and foxholes use it.

Positions in both keys are asset-local, like parts.

## Snap sockets (`"snap"`, `src/snap.h`)

`"snap": [{p: [x, y, z], dir: [x, z] or [x, 0, z], kind}]` marks the asset's joinable ends:
- `p` is the end point, asset-local.
- `dir` is the outward direction through that end.
- `kind` is any string. Two sockets join when their kinds are equal or are listed as a pair in `content/settings/snap.json` (`compatible`).

When a piece is placed or moved within 1.5 m of a free compatible socket, it snaps onto it: position and yaw. It takes the neighbour's base height, so a chain shares one base and its terrain-cut floors line up. Snapped neighbours pass the overlap test.

A part tagged `"cap": n` closes socket `n`'s free end. While that socket is joined, the part is left out of both the mesh and the collision. Faces lying in a joined end's plane and facing out through it are dropped too.

Every piece of a chain draws its procedural patterns in world xz from one chain origin (`uSnapAp` in `kMainVS`), so the texture continues across joints.

Where sockets come from:
- `aw_trench_*`, `bcg_hedgerow_*`, `bcg_sunken_lane`, `bcg_rampart` and `bch_at_ditch` declare theirs in their generators (`catalog_atlantic`, `catalog_bocage`, `catalog_beach`). Trench and hedgerow ends carry caps.
- `path_*` sockets follow each path's own length (kind `path`).
- Assets whose generator is not re-run get theirs from `snap.json` (`assets`), for example `hedgerow_bocage` (kind `hedge_low`).

## B1 geometry kit

Curved and sloped parts (`src/assets.cpp` `appendCurved` / `partHulls`) fill their bounding box `c`, `s`, rotated by `r` or `q`:

| `t` | Shape | Fields |
|---|---|---|
| `c` | Cylinder along local y, elliptical when `s[0] != s[2]` | `n`: 0 round (smooth normals), 4 square, 8 octagonal (flats face the axes and touch the box). `seg`: segments, 0 = about one per 0.2 m (12 to 48). |
| `k` | Cone, spire or frustum along local y | As `c`, plus `top`: the top section as a fraction of the base (default 0, a point). |
| `a` | Arch standing on the box's bottom face, extruded along local z | Ring (default): an arch ring `th` thick whose extrados is the box (`s[1]` = rise of the extrados). With `th` past both radii it is a solid half-disc (fanlights). `fill: 1`: a spandrel, the box minus an intrados of half-span `s[0]/2` and rise `s[1] - th`. |
| `w` | Wedge: full height at local -z, falling toward +z | `top`: the height at +z as a fraction (default 0, a sharp edge). |
| `b` + `bv` | Box with chamfered edges in the mesh only | The collider stays the box. A bevelled box is trim: the loader never makes it a damage bay, and authored bays drop `bv`. |

Colliders: cylinders, cones and wedges are one convex hull (round sections at up to 24 segments); arches are 4 to 10 convex voussoirs. `--selftest-structures` runs the tour's `"probes"` (`[{note, o, d, hit, tol}]` rays in asset space) against them.

`tools/kit.mjs` adds `Asset.cyl / cone / arch / wedge` and a `bv` option on `box` / `obox`. `tools/parts.mjs` builds facade depth from them on a wall frame `F = {axis, d, out}` (the outer face `d`, exterior along `out`):
- `pilaster`, `stringCourse`, `cornice` (stepped; the walls along X own the corners), `surround` (architrave, sloped sill, keystone);
- `archOpening` / `archWindow` / `archDoor` (voussoir ring, keystone, the spandrel closing a rectangular wall hole, jambs, fanlight);
- `colonnade`, `balcony`, `timberFrame` (half-timbering 4 cm proud of the infill, cut around holes);
- roofs and tops: `dormer`, `hipRoof`, `thatchRoof`, `turret` (round or polygonal, conical spire), `chimney` / `chimneyCap`.

### Ivy (`"ivy"`, `src/ivy.cpp`)

`"ivy": "auto"` (or `{"auto": true, "density": 0.8}`) grows ivy colonies from the ground on the outer stone, brick and rendered walls of every placed copy, seeded by the instance. `"ivy": [{face: "+z", a0, a1, y0, y1, density}]` grows it inside those rectangles instead (a0 / a1 along the wall, on the outermost facade facing that way). The cards are alpha-tested leaf cards 2 to 5.5 cm off the wall, drawn through the foliage pass (wind, culling, shadows). Nothing grows on bevelled trims, behind anything visible or in openings. The cards on a bay go when it is breached. Settings > Ivy on walls and the grass quality control it.

They keep the shimmer rule: members are embedded at least 1 cm into what they sit on, and members that meet differ in projection, so no two faces are coplanar. `kit_demo_folly` (`tools/catalog_kitdemo.mjs`) uses every one of them and is the `--selftest-render` "kit primitives" fixture.

## Rules the kit follows

- Walls are real two-layer solids: rubble stone on the outside and lime plaster inside. Openings are cut out of them, never painted on.
- Each leaf moves as its own rigid body around a local hinge. The leaf's `parts` marked `"v":0` form its kinematic collider.
- Stairs render separate treads and risers. A smooth invisible ramp runs through the tread centres, and the open side has a vertical-sided guard.
- Furniture shows its detailed parts, but each group collides as one invisible bounding box, so the player doesn't snag on legs.
- Every multi-floor building gets two independent stair routes.

## Furniture props (`"prop"`, docs/interiors.md)

Furniture assets (`content/assets/furn_*.json`, category "Furniture", generated by `tools/gen_furniture.py`) carry a `prop` block. Their visible parts have `"k": 0` (no collision) and a group index `"g"`; 1-3 invisible boxes (`"v": 0`) are the collider. Cosmetic pieces (rugs, papers, small props, curtains, pictures) have no collider.

```jsonc
"prop": { "family": "wood|painted|ceramic|glass|cloth|masonry|paper", "rounds": 8, "blast": 20,
          "blocking": true, "wall": true, "front": 0.6, "mount": 1.45,      // mount: wall-hung, height above the floor
          "groups": ["back", "seat", ...], "fragments": [0, 1], "keep": [2], "glass": 3,
          "surfaces": [[x0, z0, x1, z1, y]] }
```

- Material `"grain": -1` = the wood grain follows each part's longest axis.
- New patterns (17-28): `limewash`, `brickwork`, `ashlar`, `interior`, `floorfin`, `rubble`, `rug`, `ceramic`, `joinery`, `paintwood`, `paper`, `textile`. For `interior` / `floorfin` the grain is the finish code used outside detected rooms; for `rug` it packs the variant and half size (variant + 4 * (half x cm + 256 * half z cm)).
- Instance extras: `finish {wear, palette}` (any building), `interior {on, seed, density, clutter, gen, slots, removed}` (furnished building), `furnish {host, slot, loc, gen, hp, manual}` (a template piece), `propBroken` (applied damage).

## Map file (`*.nmap.json`)

`format`, `schemaVersion`, `name`, `template{id,version,seed}`, `nextId`, `instances[{id, asset{id,version}, position, yaw, overrides{interactables{<id>{startsOpen}}}}]`, `removedTemplateVegetation[]`, `exploreStart{position,yaw}`, `editorCamera`. Fields the loader doesn't recognise, at any level, are kept and written back on save.
