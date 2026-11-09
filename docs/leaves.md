# Species leaves (L1)

Tree and shrub canopies draw species leaf images instead of the procedural oval cards. Brief sections 25 and 26.

- **Images:** `content/textures/leaves/`. They are authored by a reproducible procedural generator, `tools/gen_leaf_textures.mjs`. No image service was used.
- **Engine:** `src/foliage.cpp` loads the leaf library and builds per-tree instances. `src/render.cpp` holds `kLeafInstVS`, `leafCut`, `drawFoliage` and `selectLeafLods`.
- **A/B:** the Perf switch `leaftex`. Use `--rq-off leaftex` for the old ovals, or `NMK_PERF_AB=leaftex` for the interleaved maps-suite A/B.

## Species table

The species for each asset is set in `leaves.json` → `assets`. The key is an exact asset name or a `prefix*`.

- **Layers:** indices into the 46-layer array `content/textures/leaves/leaves.png`. There are 8 tiles per row, each 288 px with a 16 px gutter, so a leaf's long side is 256 px.
- **Near:** fitted leaf mesh, 6 triangles per leaf plus a 2-triangle twig strip per card.
- **Mid:** 2 triangles per leaf plus the twig.
- **Far:** the sprig card, 2 triangles. This is the same card geometry, count and placement as before L1. Far canopies use the cards and the existing whole-tree frustum cull; the engine has no separate tree impostor.

| Asset(s) | Leaf family (species) | Leaf layers | Sprig layers | Variants | Leaves per card | Near / mid / far triangles per card |
| --- | --- | --- | --- | --- | --- | --- |
| `tree_oak`, `nat_sapling_oak` | Pedunculate oak: obovate, 4-6 rounded lobes, auricled base | 0-3 | 4, 5 | 4 | 6 | 38 / 14 / 2 |
| `tree_ash`, `nat_sapling_ash` | Common ash: pinnate, 7-9 lanceolate serrulate leaflets | 6-8 | 9, 10 | 3 | 3 compound | 20 / 8 / 2 |
| `tree_apple` | Apple: ovate-elliptic, crenate-serrate | 11-13 | 14, 15 | 3 | 6 | 38 / 14 / 2 |
| `tree_poplar` | Black poplar: deltoid, truncate base, long petiole | 16-18 | 19, 20 | 3 | 6 | 38 / 14 / 2 |
| `nat_hawthorn_bush` | Hawthorn: small, deeply 3-5 lobed | 21-23 | 24, 25 | 3 | 6 | 38 / 14 / 2 |
| `nat_blackthorn_low` | Blackthorn: small elliptic-obovate, finely serrate | 26-28 | 29, 30 | 3 | 8 | 50 / 18 / 2 |
| `nat_gorse_shrub` | Gorse: branched spine sprays, some with yellow flowers | 31-33 | 34, 35 | 3 | 7 sprays | 44 / 16 / 2 |
| `shrub_hazel` | Hazel: rounded, cordate, doubly serrate | 36-38 | 39, 40 | 3 | 5 | 32 / 12 / 2 |
| `shrub_bramble`, `nat_bramble_thicket` | Bramble: 3 or 5 ovate serrate leaflets, prickly petiole | 41-43 | 44, 45 | 3 | 3 compound | 20 / 8 / 2 |
| `hedgerow_bocage`, `bcg_hedgerow_*`, `bcg_rampart`, `bcg_sunken_lane` | Hedge mix: hawthorn, blackthorn and hazel, one species per canopy cluster | as above | as above | 9 | 5-8 | as above |

### Assets without species leaves

- **Not changed:** `nat_fern_clump`, `nat_bracken_clump` and `nat_nettle_clump`. They have no canopy cards. Their styled fronds and stems keep the procedural `leafAlpha` shapes.
- **Ivy** uses the same leaf program without a species. It keeps the ovals.

### Artistic choices

- **Generic hedges:** the names `hedgerow_bocage`, `bcg_*`, `bcg_rampart` and `bcg_sunken_lane` name no species. They take the Normandy bocage hedge mix of hawthorn, blackthorn and hazel. The species is chosen per canopy cluster with a hash, so a hedge reads as clumps of different shrubs, not as confetti.
- **Saplings** use their parent species.
- **Black poplar:** `tree_poplar` is a tall, narrow crown. We gave it the black poplar's deltoid leaf (*Populus nigra*), the common Normandy roadside poplar.
- **Compound leaves and gorse:** each ash leaf, bramble leaf and gorse spray is one image. Its 6-triangle hull is therefore mostly transparent: 0.1-0.2 of the hull is opaque for gorse and 0.4-0.5 for ash, against 0.7-0.85 for simple leaves. That is the recorded exception to the 6-8 triangle budget. A tighter per-leaflet mesh would need 20 or more triangles for each compound leaf. Ash and bramble cards carry 3 compound leaves instead of 6, so their triangle count stays lower. Gorse cards carry 7 sprays and blackthorn cards 8 small leaves: with fewer, their sprig cards covered only 8 % and 13 % of the card, against about 18 % for the old ovals, and those shrubs looked thin.

## Generator (`tools/gen_leaf_textures.mjs`)

Run `node tools/gen_leaf_textures.mjs`. It takes about 40 s and has no npm dependencies; PNGs are written with node `zlib`. Output is deterministic: seeded mulberry32, and every shape is a pure function of the species tables.

### Leaf model

- **Blades:** each blade is a lamina with:
  - a width profile `prof(v)`;
  - lobes (a cosine envelope with narrow sinuses);
  - sharp or crenate serration, optionally doubly serrate;
  - a midrib, secondary veins from per-side positions with slope and curve, and reticulate tertiary veins (Voronoi cell borders).
- **Compound and lobed leaves:**
  - Hawthorn lobes, ash leaflets and bramble leaflets are several blades. Each lobe's midrib is its main vein.
  - Stems, rachis, twigs and bramble prickles are capsules.
  - Gorse flowers are five-lobed blossoms.
- **Colour:**
  - The species base colour is in sRGB.
  - Low-frequency pigmentation is ±6 % value with a warmer patch toward the tip.
  - The margin is a touch darker, and veins are lighter.
  - Wear is restrained: 0-3 small brown spots on some variants, a browned tip rim on one oak and one apple variant, and one insect hole each on oak and hazel.
  - No lighting, shading or highlights are baked in.
- **Alpha:** 4×4 supersampled coverage, stored linear in the PNG alpha.
- **Dilation:** outside the silhouette, every transparent texel takes the colour of its nearest leaf texel (breadth-first fill), so filtering never pulls in white or black.

### Hulls

Each leaf layer's alpha is fitted, per row on each side of the midrib, with:

- **near:** a hexagon of base, two left points, tip and two right points, fanned from a midrib vertex into 6 triangles;
- **mid:** a diamond of 2 triangles.

Both contain every texel with alpha > 0. The search tries base vertices below the leaf as well, for cordate bases. `leaves.json` → `hulls` stores them, with the fill (opaque share of the hull) printed per layer.

### Sprigs

Each species has two sprigs. A sprig is 3-8 leaves alternating along a twig, with the terminal leaf along the axis.

- They are composited (painter's order over the twig) into a card layer.
- The layout is recentred and scaled so every leaf texel stays inside the card gutter. The generator self-checks this.
- `leaves.json` keeps each leaf's card transform, so the engine places the near and mid leaf meshes exactly where the card shows them. The LOD switch changes geometry, not the picture.

### Base colour

`base` is the mean linear colour of the leaf texels. The engine tints a species layer by the asset's `leafColor / base`, so every asset keeps its authored crown colour, and hedges keep theirs. The species identity comes from the shape and venation, not a hue.

### Contact sheet

`content/textures/leaves/contact_sheet.png` shows every layer, labelled, half over bright sky and half over a dark wall.

## Engine

### Loading

`loadLeafLib`, `foliage.cpp`, runs on the first tree asset.

- One `GL_TEXTURE_2D_ARRAY` (`GL_SRGB8_ALPHA8`, 288², 9 mips, 46 layers, about 20 MB) is bound once on texture unit 26 (`kLeafUnit`). The sampler uses clamp and 4× anisotropy.
- **Mips:** built on the CPU with a 2×2 box. Each level's alpha is scaled (binary search) so the share of texels over the 0.5 cutoff equals level 0's. Alpha-tested canopies keep their coverage at distance instead of thinning to sparkles.
- **Sprig meshes:** one shared VBO and IBO, three LODs per sprig.
  - The vertex holds a card-space position: x and y in [-1, 1] over the card's T and B half extents, z along its normal. It also holds a leaf normal, layer uv and the layer index.
  - Near leaves are folded: the midrib sits 6 % of the leaf length above the wings, and the tip arches 4 % down. Normals come from the folded faces.

### Per tree

`buildTreeAsset` keeps the card placement exactly as before. The rng stream is untouched; species and sprig choice use their own hash. Each canopy card also becomes an instance:

- centre, T·s, B·s;
- cluster direction;
- mask, shade and seed.

Instances are grouped by sprig. One VAO per tree binds the shared mesh (binding 0) and the instances (binding 1, divisor 1). A tree draws with one `glDrawElementsInstancedBaseVertexBaseInstance` per sprig group: 2 for a single species, up to 6 for a hedge mix.

### LOD

`Renderer::selectLeafLods` runs once per frame before the trees pass.

- It measures the eye's distance to the tree's canopy box in asset space, so a long hedge is near where you stand. The distance is multiplied by `1.43 / proj[1][1]`, so zoomed optics keep detail further out.
- **Thresholds** (m): near in at 10 and out at 12; mid in at 22 and out at 26. With grass quality Low or Off (the Low preset), `Renderer::leafLodScale` is 0.6, set in `settings.cpp`: near 6 m and mid 13 m. A chateau_town A/B with that setting measured Low's foliage cost at 3.72 / 3.48 ms (+0.24, down from +0.47). The remainder is the far shadow, 1.27 / 0.75 ms, which the LOD distances do not touch. `InstanceRT::leafLod` keeps the state for hysteresis.
- The canopy depth prepass and the `GL_EQUAL` shading pass use the same LOD. The vertex shaders declare `invariant gl_Position`.

### Shadows

The sun cascades and sky occlusion always draw the sprig cards, with the same layer alpha through `leafCut` in `kLeafShadowFS`. That gives the same dappled cutout as the leaves at the same card cost, with no solid blobs or rectangles.

### Shading

`kLeafFS` handles both paths. In the textured path:

- **Albedo:** `tex.rgb × tint × (0.86..1.14 per card) × the canopy self-shade`.
- **Normal:** 70 % cluster direction, for crown volume and inner-canopy darkening, and 30 % the leaf's own two-sided normal. The normal is flipped toward the cluster's outside.
- **Thin-leaf backlight:** sun reaching the far face of the leaf as seen from the eye, warm-tinted and still multiplied by the shadow term, so leaves under roofs or inside the crown don't glow.
- **Material:** no metallic term (the leaf shader has none) and the existing roughness output.

The sky-visibility, probe GI, lamp and fog terms are unchanged.

## Measurements

Run with `NMK_MAPS=battle1,chateau_town NMK_PERF_AB=leaftex --selftest-maps`, on the BASE-28 views of battle1 and chateau_town, at revision 34d2010 with these LOD distances.

- **Method:** interleaved A/B in 0.4 s blocks, so other sessions sharing the GPU hit both sides alike. The absolute ms are higher than BASE-28 for that reason; the on / off difference is what counts.
- **Foliage cost:** trees (the main pass, including the canopy depth prepass) plus the near shadow, far shadow and sky-occlusion passes.

| Preset (views) | trees | near shadow | far shadow | sky occlusion | foliage sum | GPU total |
| --- | --- | --- | --- | --- | --- | --- |
| Medium (44) | 2.72 / 2.00 | 2.31 / 4.49 | 1.98 / 1.22 | 0.15 / 0.13 | **7.16 / 7.84 (-0.69)** | 28.28 / 29.29 |
| Low (16) | 1.31 / 0.74 | 0.87 / 1.38 | 1.06 / 0.65 | 0.11 / 0.11 | 3.35 / 2.88 (+0.47) | 15.41 / 14.93 |
| High (16) | 5.98 / 5.13 | 2.32 / 4.18 | 3.20 / 2.58 | 0.13 / 0.12 | 11.64 / 12.01 (-0.38) | 44.66 / 45.02 |

Values are ms, species leaves / ovals.

### Medium views

| View | Foliage sum, species / ovals (ms) |
| --- | --- |
| orchard_eye | 8.14 / 10.53 (-2.39) |
| churchyard_eye | 8.27 / 8.11 |
| forecourt_eye | 6.11 / 5.74 |
| street_eye | 8.01 / 7.91 |

### Where the cost moved

- **Near shadow is about half.** One texture fetch replaces the six procedural ellipse tests per fragment.
- **Why the shadow shift is not noise:** the old shadow `leafAlpha` evaluates 6 hashed, rotated ellipses for every card fragment. That costs the most in the near cascade, which has the most leaf fragments: 4.49 → 2.31 ms. The far cascade rises 1.22 → 1.98 ms. There, distant cards are small, so the old path's few fragments per card were cheap, while the instanced cards now sample a 46-layer texture array whose small mips are scattered across memory, and each tree issues one draw per sprig group. The shift appears in every Medium view of both runs, and its sign never flips.
- **Trees pass rises 0.7 ms.** The near and mid leaf meshes add triangles. LOD near is 10 / 12 m and mid 22 / 26 m, tightened from 14 / 34 after a first run. That first run measured trees 2.57 / 1.55 ms at Medium.
- **Far shadow rises 0.6-0.8 ms.** It draws the same cards with the texture alpha.
- **Low:** net +0.47 ms. On Low the shadows are cheap, so the trees pass dominates.

### Memory

- Leaf array: about 20 MB (RGBA8, 46 × 288², full mips).
- Instances: 64 B per canopy card.
- Shared sprig meshes: 1316 vertices.

### Shots and data

`Documents/MapMaker/screens/l1/`:

- `*_ab.png`: ovals on the left, species leaves on the right, from `NMK_LEAF_SHOTS=1 --selftest-nature`;
- `raw/`: the full-size shots;
- `contact_sheet.png`;
- `ab_leaftex_final.txt`: every view.

Whole-frame draws and triangles per shot are in `shots_draws_tris.txt`.
