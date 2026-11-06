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
| wire_obstacle_defence_set (1).glb (complete update) | The three wire obstacles (concertina coil, post fence, knife rest), simplified to 24-45k tris each; the other copy, `wire_obstacle_defence_set.glb`, has identical geometry with smaller textures and is not used | from Sketchfab (exporter not recorded) | **unknown** | **unknown** | **unknown** |
| mk_2_grenade.glb (complete update) | Mk 2 hand grenade (held, thrown) | 12.68.0 | Nila | CC-BY-4.0 | https://sketchfab.com/3d-models/mk-2-grenade-fc6408a7f7cd485397061ef0b72531b0 |
| m24_stielhandgranate.glb (complete update) | M24 stick grenade (held, thrown) | 0.17.0 | Özcan Özaltın | CC-BY-4.0 | https://sketchfab.com/3d-models/m24-stielhandgranate-5ab9407f84964d449c31a002341bbc74 |
| knife_bayonet.glb (complete update) | Knife (standalone melee item; not fitted to rifles) | **unknown** | **unknown** | **unknown** | **unknown** |
| anti-aircraft_gun_8.8_cm_flak_18(1).glb (complete update) | 8.8 cm Flak 18 emplacement, its 153 parts driven by the station transforms | **unknown** | **unknown** | **unknown** | **unknown** |
| pak_anti_tank_gun.glb (expansion) | **Not used**: one static 227k-triangle mesh with an unlit material; it cannot traverse or elevate, so the procedural PaK 40 with moving parts is kept | 12.68.0 | **unknown** | **unknown** | **unknown** |
| 2k_textured_wwii_smoke_grenade.glb (AI / LAN) | Smoke grenade item, both sides | 15.24.0 | KimbellWhatley | CC-BY-4.0 | https://sketchfab.com/3d-models/2k-textured-wwii-smoke-grenade-a40ae55e5396487598e7e438aa87c5cc |
| stg_-_44__sturmgewehr.glb (AI / LAN) | StG 44 (sling hidden at bake) | 12.68.0 | ELIZION | CC-BY-4.0 | https://sketchfab.com/3d-models/stg-44-sturmgewehr-fa37bef729e141a6a29bb022a3e0be41 |
| 3_grease_gun.glb (AI / LAN) | M3 submachine gun | 15.30.0 | Ruskiy126 | CC-BY-4.0 | https://sketchfab.com/3d-models/3-grease-gun-3d30b825985647ef80662b84a498449c |
| ww2_panzergrenadier_2k_cc0.glb (AI / LAN) | Axis soldier (the filename says CC0, but the file's licence field says CC-BY-4.0) | 15.55.0 | britdawgmasterfunk | CC-BY-4.0 | https://sketchfab.com/3d-models/ww2-panzergrenadier-2k-cc0-6a104431bcb8400080b79446ec019696 |
| us_ranger_infantry_-_call_of_duty_2_hd.glb (AI / LAN) | Allied soldier, variants 0 / 2 / 4, rigged at bake (it names itself a Call of Duty 2 derivative; upload rights not verified) | 16.72.0 | Nazar Okruzhko | CC-BY-4.0 | https://sketchfab.com/3d-models/us-ranger-infantry-call-of-duty-2-hd-fb2e52e95ed341929ccfdfc0f6cae9b6 |

The expansion package (`Normandy_Claude_Expansion.zip`) states that file names are mappings only, not proof of variant, licence or authorship. None of its GLBs carries author or licence metadata. Verify each one before redistributing; nothing here assumes public-domain status. Only baked derivatives ship (`content/weapons/models/`). The originals stay in `assets_src/weapons/`, which is git-ignored, and the full package is kept unchanged outside the repository.

### Phase 2 tanks

Unlike the weapon files, both tank GLBs carry author and licence fields in their glTF `asset.extras` block:

| File | Used for | Author | Licence | Source URL |
|---|---|---|---|---|
| tank_fury.glb | Sherman Firefly **stand-in** (the mesh is an M4A3E8 "Easy Eight", see [tanks.md](tanks.md)) | FlashPoint (https://sketchfab.com/FlashPoin) | CC-BY-4.0 | https://sketchfab.com/3d-models/tank-fury-aa010de33f4e43a6954a9caedbb833db |
| pzkpfw_vi_tiger_1.glb | Tiger I | Tinus (https://sketchfab.com/HeerBeer) | **CC-BY-NC-4.0 (non-commercial use only)** | https://sketchfab.com/3d-models/pzkpfw-vi-tiger-1-dfd7156c43124d1c9ff77d895c647142 |

Both licences need visible attribution. The Tiger's NC licence rules out any commercial distribution of a build that contains it. Only the baked derivatives ship (`content/vehicles/models/`, textures at 1024² or less). The originals stay in the git-ignored `assets_src/vehicles/`. The tank sounds are synthesized placeholders, not recordings.

**What ships:**
- Only the baked derivatives in `content/weapons/models/` and `content/assets/models/`: meshes (simplified for the wire set), plus textures reduced to 1024² (512² for the many-material FG 42 and M2 and the hand equipment).
- The original GLB files stay in `assets_src/weapons/`, which is ignored by git and not packaged.

Everything else in the game is generated procedurally from box parts: the structures, trees, shrubs, hedgerows, defences, racks, farm dressing and arms. The ground, grass and weathering are shader-generated; no external textures were downloaded.

## Rendering reference

BSL Shaders v10.1.8 by Capt Tatsu (https://capttatsu.com) was the visual reference for the lighting, atmosphere and post-processing pass (`docs/rendering.md`). The pack ships no licence file and no reuse terms were found, so none of its code, tables or textures are used or redistributed. The engine's shaders are original implementations, and the pack is not part of this repository.

Author, licence and source for the grenade, StG 44, M3 and soldier rows come from each file's own glTF `asset.extras` (Sketchfab export fields). They were read from the files, not taken from the filenames.
