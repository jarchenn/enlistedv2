# Textures: the ambientCG layer (L4)

Photo texture sets from [ambientCG](https://ambientcg.com) (CC0 1.0), all at **1K**, layered on the procedural
surface patterns of buildings, props, furniture, defences, paths and the terrain. The procedural patterns stay in
charge of structure and variation (courses, joints that breaches follow, per-stone tint, room finishes, weathering);
the sets add photographic surface detail, normal maps and roughness and AO variation. Plan: `docs/plan-lighting-textures.md` → L4.

## Files

| Path | What |
|---|---|
| `tools/texture_sets.json` | The curated list: 57 sets, each an ambientCG id, a family and its tile size in metres |
| `tools/fetch_ambientcg.py` | Fetch and pack (Python stdlib only, run with `-I`) |
| `content/textures/<set>/color.jpg` | Colour, sRGB, 1024² |
| `content/textures/<set>/normal.jpg` | NormalGL (OpenGL convention: green up) |
| `content/textures/<set>/rma.jpg` | R roughness, G ambient occlusion, B height (displacement), linear |
| `content/textures/<set>/meta.json` | ambientCG id, source URL, licence, tile size |
| `content/textures/textures.json` | Slots: how patterns, materials and terrain layers use the sets |
| `src/textures.cpp`, `src/textures.h` | Loader, GLSL hooks, packer, placeholders |
| `src/selftest_textures.cpp` | `--selftest-textures` |

## Fetching

```
$TEMP/nmk/py/python.exe -I tools/fetch_ambientcg.py              # dry run: the exact id list and the total size from the API
$TEMP/nmk/py/python.exe -I tools/fetch_ambientcg.py --download   # download + pack (needs a built NormandyMapmaker.exe)
$TEMP/nmk/py/python.exe -I tools/fetch_ambientcg.py --pack-only  # re-pack from the raw folder
```

- The script calls the official API, `https://ambientcg.com/api/v2/full_json?include=downloadData&id=<ids>`, and downloads only each id's **1K-JPG** zip, from `ambientcg.com` hosts only. The size of each download is checked against the metadata.
- Raw zips and the extracted maps go outside the repo, to `%TEMP%/nmk/ambientcg_raw` (`--raw` changes this; a path inside the repo is refused).
- From each zip it keeps Color, NormalGL, Roughness, AmbientOcclusion and Displacement. Members are read by suffix and never extracted by their own paths.
- Packing (resample to 1024², renormalise normals, pack R/AO/H, JPEG q92/95) runs in the engine: `NormandyMapmaker.exe --pack-texture <raw/id> <content/textures/set>`. The standard library has no JPEG codec, and the engine already ships stb_image and stb_image_write. `--packer` picks the exe; the default is the newest `%TEMP%/nmk/build-*/NormandyMapmaker.exe`.
- `--only a,b` limits the run to some sets. A set that fails is reported and skipped; in game it falls back to procedural.
- Changing the list means editing `tools/texture_sets.json` and, for a new set to be used, adding a slot in `textures.json`.

## On the GPU

- **Three `GL_TEXTURE_2D_ARRAY`s**, one layer per set, 1024² with 9 mip levels (down to 4²), trilinear with 8× anisotropy, `GL_REPEAT`. They are bound once at start-up to texture units **20-22**. L2 uses 16-19, and the GL limit is checked (`GL_MAX_TEXTURE_IMAGE_UNITS`, 32 on the 2080 Ti).
  - Unit 20: colour (sRGB) plus AO in alpha, **BC3**.
  - Unit 21: normal xy, **BC5**.
  - Unit 22: roughness plus height, **BC5**.
- Compression runs on the CPU at load with `stb_dxt`, so the result is the same on every driver. Without S3TC the arrays fall back to RGBA8/RG8.
- Mips are built on the CPU: colour in linear light, and normals averaged and left unnormalised, so distant normal detail flattens instead of sparkling.
- Every folder under `content/textures` with a `color.jpg` that a slot references becomes a layer, up to 64. With the shipped mapping that is 48 sets, about 190 MB. Loading is threaded. Load time and memory are in the log line `textures: N sets (...)` and in `selftest_out/textures/summary.json`.

## How a surface uses a set: slots

`content/textures/textures.json` maps the surfaces to slots:

- **`patterns`**: pattern id → slot. Every family has one: stone, wood, plaster, slate, tile, metal, fabric, brick, painted, concrete, the three path surfaces, bunker, limewash, brickwork, ashlar, rubble, rug, joinery, painted wood, textile and rock.
- **`materials`**: material name → slot, checked before the pattern. Examples: `turf` → grass, `earth` → soil, `ridge_tile` → clay roof tile, `od_paint` → painted green steel, `hay` → straw. `"none"` keeps a material procedural (wallpaper, brass, mirror).
- **A material's own `"texture": "<slot or set>"`** field in an asset JSON wins over both. `"none"` disables the layer for that material. A set name with no slot gets a default detail slot.
- **`terrain`**: the eight `kTerrainFS` layers (grass, soil, mud, gravel, sand, rock, scree, crater).

Each slot has these fields:

| Field | Meaning |
|---|---|
| `set` | The folder name |
| `mode` | `detail`: albedo and roughness are multiplied by the sample divided by the set's mean, so the material colour, the procedural structure and the per-building variation stay. `replace`: the set's own structure is used instead of the pattern (bricks, slates, roof tiles, soil props), still tinted by the material colour |
| `space` | `asset`: triplanar in asset space (moves with the building). `part`: the part's dominant face, with texture v along the part's grain axis (planks, slates, bark; parts can be rotated). `world`: triplanar in world space (rock). `ground`: planar xz (terrain) |
| `albedo`, `normal`, `rough`, `ao` | Strengths 0..1 |
| `height` | Metres fed into the derivative bump. It goes through the same detail filter as the procedural relief (`gPix`), so the shimmer fix is kept |
| `scale` | Multiplies the tile frequency |
| `rot` | Quarter turns |
| `fade` | `[start, end]` metres from the eye. Terrain layers fade back to the procedural ground, which hides tiling |

The vertex carries the slot in the existing pattern float, so the vertex format is unchanged: `params.x = pattern + 100 × code`, where code 0 = the pattern's slot, 1 = none, and 2+ = slot. No generator or asset JSON had to change. The mapping is done at load time, so `build_mpu_assets --check` is unaffected.

## Shaders

- **`kMainFS`.** The hooks are in `textures.cpp` (`glslMain`, prepended to the shader). The procedural pattern runs first, then `texMaterial()`, then the weathering (damp foot, moss, streaks) on top.
  - Triplanar weights are sharpened (`n⁴`) and taps under 4 % are dropped, so a box face costs one tap of three textures.
  - Normals use whiteout blending with the surface normal. In part space, a screen-space cotangent frame carries the tangent frame to world space.
  - All taps use `textureGrad` with gradients taken before any branch.
  - The normal delta is added after the derivative bump. The height goes into the same bump (`h*bump + gTexH`), faded by `res()` at about 8 texels per feature.
  - `replace` slots skip the procedural pattern (`texReplace`).
- **`kTerrainFS`.** Each ground layer takes a world planar sample, and only where its weight is above about 1 %: grass, the track (soil, gravel or sand by route kind), mud (ruts and wet), crater soil, scree and rock (triplanar). The samples multiply the existing procedural colours, which keeps the palette, the broad variation and the masks (`uSurf`, crater `aDirt`, slope). The normal, roughness and AO deltas blend with the same weights. Puddles stay mirror-smooth.
- **`--legacy-look`** and **`--no-textures`** (A/B) are procedural only. A missing set, a corrupt file or no `textures.json` also leave the affected slots procedural.

## Painted ground (lasso stream)

`textures.json` → `"paint"` maps the 12 painted-ground types to `paint_<type>` slots: marsh, dirt, mud, gravel, sand, cobbles, rock, dry_grass, ploughed, leaf_litter, scorched and pebbles. The order is `tex::kPaintTypeNames`; the shader sees it as `uniform int uPaintSlot[12]`.

The lasso ground-paint shader calls `terrTexSlot(uPaintSlot[k], p, tn0, tdx, tdy, tDist)` from kTerrainFS. It gets back an albedo and roughness multiplier, AO and a normal delta (`TerrT`), and identity when the layer is off or the set is missing. `#define NMK_TEX_PAINT` marks the API.

Five sets for paint types were requested in addition to the 57 (`side-session-notes/lt4-paint-sets.md`); their slots stay procedural until those sets are downloaded.

## Shimmer

Photo detail near the pixel frequency flickers under the TAA jitter. Three measures, tuned on `--selftest-render` against `--no-textures` (the metric is deterministic: two baseline runs are identical):

- **LOD bias.** Every tap's gradients are scaled ×3, about +1.6 mip levels (`uTexLod.x`).
- **Normal-map fade.** The normal map fades by 95 % between 2 and 16 texels per pixel (`uTexLod.yzw`).
- **Albedo and roughness fade.** These fade by 70 % between 8 and 48 texels per pixel (`uTexLod2`), on top of the per-slot `fade`.
- In addition, the rubble slot uses the low-contrast `sandstone_wall`; `rock_grey`'s sharp crevices flickered on the ruined church.

`NMK_TEX_LOD="a,b,c,d"` and `NMK_TEX_LOD2="a,b,c"` override them for A/B runs.

| Stage | Views worse by > 2 % (of 26) | Mean ratio on/off | Worst |
|---|---|---|---|
| No anti-shimmer terms | 26 | 1.22 | 1.93 (church, no relief, 20 m) |
| ×1.75 bias, normal fade 6→48 | 16 | 1.06 | 1.53 |
| ×2.5 bias, normal fade 3→24 | 9 | 1.03 | 1.33 |
| **Shipped:** ×3, normal 2→16, albedo 8→48, sandstone rubble | **0** | **0.99** | 1.008 |

With the layer on, the farmhouse and the moving strip get *less* shimmer than procedural alone (−2 to −3 %).

## Sets not mapped yet

Nine of the 57 approved sets have no slot. On disk they cost nothing on the GPU: only sets a slot references are loaded. They are: `brick_factory`, `brick_damaged`, `rock_grey`, `cobbles_grey`, `cobbles_old`, `slate_roof`, `roof_clay`, `paint_wood_old` and `rock_mossy`. No surface in the game is cobbled yet. Adding a slot makes a set live.

## Tests

`--selftest-textures` (`tools/run_all_tests.sh`: `textures`) checks:

- the mapping and pattern coverage;
- material codes;
- that slots are live exactly when their set is on disk;
- three buildings and a terrain view rendered with the layer on and off: not black, brightness within 30 %, and changed only where a live slot is used (tiles in `selftest_out/textures/`);
- that `--legacy-look` stays procedural;
- **the missing-texture fallback**: re-initialised from a folder with the mapping, no sets and a corrupt set, the layer is inactive and every tile equals the procedural one; a lone placeholder set brings only its slots live; then the real sets are restored.

`NormandyMapmaker.exe --texture-placeholders a,b[,..] [root]` writes procedural stand-in sets with the same layout. It is used for engine bring-up and by the test.

Thumbnail cache keys include `tex::stateKey()`: the slot file, the set files and on/off. Tiles re-render when the textures change.

## Credits

All sets are CC0 1.0 from ambientCG.com (see `docs/credits.md`). Each set's `meta.json` names its source asset.
