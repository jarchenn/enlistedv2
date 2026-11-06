# Build-menu thumbnails

Code: `src/thumbs.cpp` / `thumbs.h` (W2, MP & Creative update spec 9.4). `App::makeThumbnails` calls `makeAssetThumbnails`; map-template tiles still use `Renderer::renderThumbnail` (terrain views), which moved into the same file.

## What was wrong
- **Blank tiles.** Assets with an imported model (the Flak 18 and the three barbed-wire assets) filled `R.worldModels`, but the thumbnail pass never drew them. Their tiles came out as an empty sky.
- **Bad framing.** The camera framed a bounding sphere at a fixed 3/4 view. A tall, thin asset (the telegraph pole, 8 m by 0.25 m) filled only a sliver of the tile.

## Rendering
- **What is drawn.** Box parts, intact panels, interactable leaves at their default pose, imported models and tree canopies are drawn opaque. Glass is blended.
- **Thin wire.**
  - **Models:** imported wire is widened with the world's thin-line rule (`thinPx`), with at least 1.2 px at tile scale.
  - **Box parts:** a visible box part thinner than about 1.5 px of the framed bounds (fence strands) gets a tile-only copy thickened to that size, drawn with the normal mesh.
- **Light rig.** One key light fixed to the camera, from the upper left and in front. There are no shadows and no fog.
- **Tonemap.** Fixed exposure 1.1 and the filmic tonemap. The main frame's bloom is never applied.
- **Background.** A flat neutral grey (HDR 0.20). The tonemap's vignette is the only variation.

## Framing
1. The camera looks at the centre of the asset bounds. Those bounds already combine every box part, panel, imported model and canopy.
2. A 128 px probe pass reads the silhouette from the depth buffer.
3. The projection is zoomed and shifted in clip space so the silhouette fills 88 % of the tile, centred: `x' = s (x - c_x w)`, `s = 2 · 0.88 / max(w_ndc, h_ndc)`. The same applies to y.
4. The default view is yaw 37°, pitch 24°, the old 3/4 view. If its silhouette covers less than `kThumbMinFill` (15 %) of the tile, the asset counts as thin or tall.
5. For a thin or tall asset, 8 yaws × 4 pitches (20–65°) are probed. The view with the largest covered share wins. Steep views are weighted down slightly (×0.92 at 50°, ×0.82 at 65°), so a near tie keeps the gentler one.
6. Vehicles (`"vehicle"` assets) keep the tile `TankSys::makeThumbnails` draws from the baked hull, turret and tracks.

## Cache
- **Location.** Tiles are stored as `<exe>/cache/thumbs/<asset id>-<key>.png`. `NMK_THUMB_CACHE` overrides the folder; the tests use this.
- **Key.** FNV-1a 64 over:
  - `kThumbVersion`;
  - the asset JSON;
  - for a model asset, `model.json`, `mesh.bin`, and the name and size of every file in the model folder.

  Editing or re-baking an asset changes its key. Its tile is then rendered again on the next start, and its stale file is deleted.
- **Look changes.** Bump `kThumbVersion` when the thumbnail look or framing changes.

## Tests (`--selftest-visuals`)
- **Cold cache.** Every tile is rendered and written.
- **Blank tiles.** No tile may be more than 95 % background, measured per pixel against an empty tile. The five tiles that used to be blank are named in the report. Before and after tiles go to `selftest_out/thumbs/`: `before_*` comes from the legacy renderer and `after_*` from the new one.
- **Warm cache.** Every tile loads from the cache, pixel-identical to the cold render.
- **Changed asset.** Only that asset re-renders, and its old file is replaced.
- **Footprint labels.** One decimal, for example "0.3 × 0.3 m", and two below 0.1 m, for example "0.6 × 0.04 m" (`footprintLabel`, used by the editor's `fmtFoot`). No catalogue label reads 0.0.
