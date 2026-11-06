# Credits: third-party models

You supplied these five GLB files and confirmed you own or have licensed them. All five are Sketchfab exports, but none carries an author, licence or copyright field in its glTF `asset` block. Please fill in the blanks below before you redistribute a build. A CC-BY licence requires visible attribution, for example in an About box or this file.

| File | Used for | Sketchfab exporter | Author | Licence | Source URL |
|---|---|---|---|---|---|
| realistic_m1_garand.glb | M1 Garand, M1C Garand | 12.68.0 | _fill in_ | _fill in_ | _fill in_ |
| low-poly_thompson_m1a1.glb | M1 Thompson | 12.67.0 | _fill in_ | _fill in_ | _fill in_ |
| mp40.glb | MP 40 | 16.72.0 | _fill in_ | _fill in_ | _fill in_ |
| mg42.glb | MG 42 | 16.60.0 | _fill in_ | _fill in_ | _fill in_ |
| browning_m1918a2_bar.glb | M1918A2 BAR | 12.67.0 | _fill in_ | _fill in_ | _fill in_ |
| m1_carbine.glb (expansion) | M1 Carbine | 12.68.0 | **unknown** | **unknown** | **unknown** |
| fg42.glb (expansion) | FG 42 | 12.66.0 | **unknown** | **unknown** | **unknown** |
| bazooka.glb (expansion) | M9 Bazooka (two-piece tube, reads as M9/M9A1; factor-only material repainted OD in the weapon JSON) | 12.65.0 | **unknown** | **unknown** | **unknown** |
| panzerschreck_pickup.glb (expansion) | Panzerschreck (pickup-grade mesh, 2.9k tris, no normal map) | 12.68.0 | **unknown** | **unknown** | **unknown** |
| m2_browning_machine_gun.glb (expansion) | Carried M2HB and the M2HB tripod station | 12.68.0 | **unknown** | **unknown** | **unknown** |
| kar98k (1).glb (expansion) | Kar 98k and Kar 98k ZF39 (replaces the box model; the ZF39 scope stays procedural) | 14.39.1 | **unknown** | **unknown** | **unknown** |
| tommy_gun.glb (expansion) | **Not used**: an M1928-style drum gun (finned barrel, Cutts compensator, vertical foregrip), a different variant from the shipped M1 Thompson | 14.62.0 | **unknown** | **unknown** | **unknown** |
| pak_anti_tank_gun.glb (expansion) | **Not used**: one static 227k-triangle mesh with an unlit material; it cannot traverse or elevate, so the procedural PaK 40 with moving parts is kept | 12.68.0 | **unknown** | **unknown** | **unknown** |

The expansion package (`Normandy_Claude_Expansion.zip`) states that file names are mappings only, not proof of variant, licence or authorship. None of its GLBs carries author or licence metadata. Verify each one before redistributing; nothing here assumes public-domain status. Only baked derivatives ship (`content/weapons/models/`). The originals stay in `assets_src/weapons/`, which is git-ignored, and the full package is kept unchanged outside the repository.

**What ships:**
- Only the baked derivatives in `content/weapons/models/`: meshes, plus textures reduced to 1024² (512² for the many-material FG 42 and M2).
- The original GLB files stay in `assets_src/weapons/`, which is ignored by git and not packaged.

Everything else in the game is generated procedurally from box parts: the structures, trees, shrubs, hedgerows, defences, racks, farm dressing and arms. The ground, grass and weathering are shader-generated; no external textures were downloaded.
