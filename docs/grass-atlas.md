# Grass plant atlas (GT1)

The pasture grass clumps (G1 / GX3, `kGrassVS` and `kGrassFS` in `src/render.cpp`) draw photographed grass plants on their blade cards. Before GT1 they used the procedural blade shape.

- **Sources:** Poly Haven [grass_bermuda_01](https://polyhaven.com/a/grass_bermuda_01) and [grass_medium_01](https://polyhaven.com/a/grass_medium_01), both CC0. Credits are in `docs/credits.md`; both are listed under `polyhaven` in `tools/texture_sets.json`.
- **Packed by:** `node tools/pack_grass_atlas.mjs [sourceDir]` → `content/textures/grass_atlas/`, containing `grass_atlas.png`, `grass_atlas.json` and `contact_sheet.png`. The default source is `../incoming/ground_ph_small` (`2k/`, falling back to `1k/`).
- **A/B:** the Perf switch `grassatlas` (`--rq-off grassatlas` gives the procedural blades).

## Packing

- **bermuda:** cells are the connected components of the alpha map (above 0.2, 8-neighbour). Free blade tips within 10 px join their plant. The lone seed head is dropped, because a bare spike reads as a stick on its own card. The two hair-thin single blades are dropped too: a few texels wide, their coverage-preserving mips fill the whole column, and they showed as floating slivers.
- **medium_01:** its sheet is mostly unwrapped mesh islands. Only the five clumps along the bottom are billboards. They are hand-picked rectangles, keeping the largest component, and the dilation bars at the base are cut off. The colour and dry-colour versions both become cells.
- **Not used:** medium_01's seed plumes. Their islands carry dilation streaks under alpha 0.5, which the coverage-preserving mips turn opaque at range, so they showed as floating rectangles.
- **Layers:** one cell per 256 px layer, 22 layers in all. Each cell is scaled to fit with its aspect kept, centred, with the base on the 6 px bottom gutter.
  - Colour is area-sampled, weighted by alpha, then dilated outward, so filtering never pulls in the black background.
  - Alpha is the source alpha, stored linear.
- **JSON:** per cell, its uv rectangle, aspect (width / height) and linear mean colour; per species, its cells.

## Engine

`grassAtlasInit` (`src/grass.cpp`) builds the texture.

- One `GL_SRGB8_ALPHA8` `GL_TEXTURE_2D_ARRAY` on texture unit 27, about 6 MB with mips.
- Mips come from L1's `leafMips`: each level's alpha is scaled so the share of texels over the 0.5 cutoff stays as at level 0.
- The cell, mean and species tables are set as uniforms on `progGrass` and `progGrassShadow`.

### Species to cells

| Pasture species | Cells | Plant cards per clump |
| --- | --- | --- |
| 0 turf | Low spreading bermuda tufts | 2 (cards 0-1) |
| 1 medium meadow | Fuller bermuda tufts and leafy stems; grass_medium_01 clumps | 2 |
| 2 seed-head grass | Tall bermuda stems on its leafy cards (cards 3-4). Its two procedural seed stems stay | 2 |
| 4 straw | grass_medium_01 dry clumps, browned bermuda tuft | 2 |
| 18 marram | The upright tall stems | 2 |
| 3 clover, 16 shrub, 17 plantain | Unchanged (procedural leaflets, twigs, plantain) | – |

### Per card

- **Shape:** the card is the plant's rectangle. Height comes from the clump height; width is height × aspect, times √widen for the G1 LOD widening, capped. It keeps a gentle lean and the clump's wind.
- **Plant cards:** only clumps within 5-6.5 m of the eye use them (dithered per clump, so there is no line). There, each carries several blades, so only the first 2 blade cards of a clump show a plant, and the rest collapse before raster through the same out-of-clip path as the LOD cut. Beyond that range the clump keeps its procedural blades. Submitted cards and LOD are unchanged.
- **Cost:** a plant rectangle carries much more transparent, alpha-tested area than a tapered blade. Within 9 m with 3-4 cards, the grass main pass roughly doubled (same-run A/B, Medium open field 1.00 → 1.93 ms), so the range and card count were cut to the values above. The final numbers are under Measurements.
- **Colour:** `tex.rgb × clump albedo / cell mean`. The clump's palette, its dry tips and the root blend into the terrain colour (the per-vertex `vAlb`) are kept.
- **Lighting and shadows:** same as before. The alpha test is at 0.5, and the shadow program uses the same cut-out.
- **Cost:** one texture sample per fragment replaces the procedural blade maths.
- **Normal map:** not used. The plants are thin cards seen at grazing angles under the existing wrapped diffuse and backlight, and a normal sample would be a second texture fetch.

## Measurements

The bench (`--bench-grass`, open field, prone and moving, Medium / High / Ultra) is run once with `--rq-off grassatlas` and once without, in one GPU turn. Shots and numbers are in `Documents/MapMaker/screens/gt1/`.
