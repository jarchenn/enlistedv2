# Battle Foundry — environment materials and combat VFX

**Merged implementation spec, rev 3.** Supersedes `Battle_Foundry_Environment_and_Combat_VFX_Spec.md` (rev 1) and the `combat-vfx-and-surfaces` reference corpus (rev 2), both of which are folded in here. Where they disagreed, this document states which was right and why.

Implement in the existing C++ / OpenGL Battle Foundry project. Preserve the active modes, gameplay event rules, collision intent, camera controls, lighting pipeline, and preset names. Make render changes asset- or map-local where practical. The acceptance gates in §9 define the first implementation target.

**Reuse:** terrain chunks and map editor paint; GLB bake/import; HDR scene colour and the existing tonemapper; weather, particles, and transient lights; damage visuals and surface material query; camera, weapon, and vehicle render rigs; the existing timer and graphics options. Find the real equivalents in the project. Names in pseudocode describe data, not required engine APIs.

---

## 0. How to use this document

### Two layers

| Layer | File | Role |
|---|---|---|
| **Plan of record** | this file | What to build, in what order, to what acceptance. Project-bound. Every magnitude it states is ready to type in. |
| **Derivation** | `reference/01` … `reference/11` | Why each magnitude is what it is, the physics behind it, and how to re-derive it for a weapon or munition not in the tables. |
| **Parameters** | `data/*.json` | The same numbers, loadable. |

When this spec gives a number, use it. When you need a number it does not give — a new calibre, a new charge, a new surface — go to the reference chapter named in the margin and derive it rather than inventing one.

### Provenance tags

Every quantity in this document is one of:

- **[P]** *parsed* — read directly out of the supplied GLBs.
- **[M]** *measured* — from a cited published measurement.
- **[D]** *derived* — computed here from [M] or [P] inputs; the derivation is in the named reference chapter.
- **[A]** *authored* — a tuning seed chosen to produce a plausible magnitude. In-engine measurement supersedes it.

Untagged prose is procedure. **No performance figure in this document was measured on Battle Foundry.** All timing budgets are [A].

### Conventions

World positions and geometry in project metres after import validation. Time in seconds. Colours in equations are scene-linear sRGB primaries **before exposure**; hex swatches are encoded sRGB. `sat(x)=clamp(x,0,1)`, `ss(a,b,x)=smoothstep(a,b,x)` with `a<b`, `lerp(a,b,w)=a+(b-a)w`. Charge masses are TNT-equivalent kilograms.

### Corrections carried from both source documents

| Claim | Status | Correct value |
|---|---|---|
| rev 1 §2: "11,061 primitives use one BLEND decal material and account for 325,386 triangles" | **numerically right, architecturally misleading** | True, but 11,050 of those 11,061 are duplicated grass and leaf cards. See §2.2. |
| rev 2 ch 01: "the decal mesh is 2,408 triangles … 0.45 %" | **wrong** | The authored decal set is 11 primitives / 4,886 tris; the *weathering* subset is 5 primitives / **1,624 tris / 0.30 %**. See §2.2. |
| rev 2 ch 01 import step: "strip TEXCOORD_1, TEXCOORD_2" | **unsafe on the road asset** | Road materials 0, 11 and 12 bind their normal map to UV1. See §2.4. |
| rev 1: no licence record | **gap, now closed** | Two of six assets are CC-BY-NC-4.0. See §2.1. |
| rev 1 §7: tank recoil 0.40–0.68 s, peak at u = 0.12–0.21, pitch 0.15–0.45° | **2–3× slow, ~2× low** | M256: 33 ms out, 191 ms back, 0.77° pitch. See §7. |
| rev 1 §5: flash core (1.00, 0.87, 0.70) | **a ~4600 K source** | Propellant combustion is 1645–2500 K. The pale core is exposure clipping. See §5.3. |
| rev 2: brightness in absolute nits only | **correct but not incrementally adoptable** | Both conventions, with the conversion, in §5.4. |

---

## 1. Research translated into requirements

### 1.1 Production technique

| Evidence | Implement and verify | Source |
|---|---|---|
| Naughty Dog used shader, UV and vertex-blending work on the sewers and publishes modular vehicle grunge examples for The Last of Us Part II. | Separate tile, unique-UV and localised wear. Reuse inputs; drive placement with different masks. | [Javier Perez](https://meshmodeler.artstation.com/projects/28xrXe) · [Matthew Trevelyan Johns, GDC 2021](https://media.gdcvault.com/GDC%2B2021/33713_GDC-2021-MatthewTJohns-ND.pdf) |
| Frostbite's PBR course and the glTF spec document material parameters and colour-space handling. | Preserve base colour, roughness, metallic and tangent-normal roles through the bake. Compare in two lighting conditions. | [Lagarde & de Rousiers](https://blog.selfshadow.com/publications/s2014-shading-course/frostbite/s2014_pbs_frostbite_slides.pdf) · [Khronos glTF 2.0](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html) |
| Far Cry 5 used a heightfield pipeline with LOD and stitching plus separately generated cliffs. Horizon Zero Dawn used artist-authored GPU placement rules. | Build terrain-scale relief separately from material bumps. Use seed-stable rule-driven scatter, thinned by projected size. | [Jeremy Moore, GDC 2018](https://media.gdcvault.com/gdc2018/presentations/TerrainRenderingFarCry5.pdf) · [van Muijden, Guerrilla](https://www.guerrilla-games.com/read/gpu-based-procedural-placement-in-horizon-zero-dawn) |
| Epic documents height-blended terrain and the degenerate all-zero blend case. Hill and Barré-Brisebois show how to orient detail normals onto a base normal. | Guaranteed fallback in height-weight normalisation. Consistent tangent frame, RNM for detail. | [Epic, Landscape Material Expressions](https://dev.epicgames.com/documentation/unreal-engine/landscape-material-expressions-in-unreal-engine) · [Blending in Detail](https://blog.selfshadow.com/publications/blending-in-detail/) |
| Heitz & Neyret give a histogram-preserving blend operator for non-repeating tiling. | Hex tiling on ground and large walls at Medium+, with variance restoration. | Heitz & Neyret, HPG 2018 |
| Kaplanyan et al. convert normal-map variance into roughness. | Specular anti-aliasing on every surface carrying a detail normal. Non-optional. | Kaplanyan et al., HPG 2016; Tokuyoshi & Kaplanyan 2019 |
| Forest-road documentation describes crowns, ruts and flow redirected by shallow depressions. A forest study links deadwood bryophyte communities to substrate and microclimate. | Use road cross-section and drainage to place mud and pools. Condition moss on ground type, moisture, shelter and disturbance. | [USFS, Traveled Way Surface Shape](https://www.fs.usda.gov/biology/nsaec/assets/traveledwaysurfaceshape.pdf) · [Táborská et al., J. Veg. Sci.](https://onlinelibrary.wiley.com/doi/10.1111/jvs.12919) |
| Infinigen's procedural moss aligns clump Z between surface normal and world up, with rotation from a smooth noise field so neighbours agree. | Scatter rule in §4.4. Random per-clump rotation reads as shuffled; a noise field reads as grown. | [Raistrick et al., CVPR 2023](https://arxiv.org/pdf/2306.09310) |
| DICE presented lighting particles with their environment in Battlefield 3. Unity and the Skull & Bones team document six-way lightmap smoke. | Smoke inherits scene light. Six-way basis on hero smoke; the 4-channel front/back approximation where memory is tight. | [Magnusson, GDC 2011](https://media.contentapi.ea.com/content/dam/eacom/frostbite/files/gdc11-lightingyouupinbattlefield3.pdf) · [Unity six-way](https://unity.com/blog/engine-platform/realistic-smoke-with-6-way-lighting-in-vfx-graph) · [Skull & Bones](https://realtimevfx.com/t/smoke-lighting-and-texture-re-usability-in-skull-bones/5339) |
| SideFX separates burst, smoke, flame and debris stages. Motion-vector frame blending reduces an 11×11 flipbook to 6×6 at equal smoothness. | Bake separately controllable flame and smoke sheets. Keep opaque pieces and soft dust on different paths. Motion vectors at Medium+. | [SideFX](https://www.sidefx.com/docs/houdini/pyro/explode) · [flipbook motion vectors](https://loronsodev.github.io/posts/explosion-fx/) |
| NVIDIA documents reduced-resolution soft particles and the depth complications; Epic documents measuring quad overdraw. | Low-res depth-aware soft pass, native-resolution sharp components. Profile coverage, distance and overlap, not count. | [Cantlay, GPU Gems 3 ch. 23](https://developer.nvidia.com/gpugems/gpugems3/part-iv-image-effects/chapter-23-high-speed-screen-particles) · [Epic, Niagara performance](https://dev.epicgames.com/documentation/unreal-engine/measuring-performance-in-niagara) |
| VFX design guidance shows a compact flash communicates a shot without swallowing the sight picture. | Test from normal player FOV with a tight flash core. | [An-Tim Nguyen, GDC 2022](https://media.gdcvault.com/GDC%2B2022/Speaker%2BSlides/VFXasagamedesignlanguage_Nguyen_An-Tim.pdf) |

### 1.2 Physical measurement

These set the magnitudes. Without them every VFX number is a free parameter and every new weapon needs hand-tuning.

| Evidence | Implement and verify | Source |
|---|---|---|
| Emission–absorption spectroscopy puts the **primary muzzle flash at 1645 K** and the **secondary flash at ~2500 K**, 300–500 mm downstream for a 7.62 mm rifle; firing into inert gas removes the secondary flash alone. | Flash palette from blackbody temperature, not a swatch. A flash hider removes the *secondary* component specifically. §5.3. | [Klingenberg, OSTI 7238764](https://www.osti.gov/biblio/7238764) |
| CARS measurement gives the intermediate flash 1400–2000 K, falling to 1468 K at 0.70 ms. | Three flash components with separate temperatures and lifetimes. §5.3. | [DTIC ADA190786](https://apps.dtic.mil/sti/pdfs/ADA190786.pdf) |
| Small-arms visible flash lasts **1–2 ms** (to 7 ms for .30-06); large-calibre flash is **under 200 ms** in MWIR. | A rifle flash is 0.12 of a frame at 60 fps. Integrate energy over the presentation interval. §5.5. | [Burke & Bratlie via arXiv:1410.6025](https://arxiv.org/abs/1410.6025) · [Telops](http://info.telops.com/rs/telops/images/FAST%20Thermal%20Imaging%20-%20Tank%20Muzzle%20Flash%20Analysis.pdf) |
| The intermediate-flash disc appears ~7.5 cm from the muzzle of a small-calibre weapon and 20–25 calibres out on large guns. | The underexpanded-jet Mach-disc relation reproduces both. Scale the whole flash to it. §5.2. | [NavWeaps](https://www.navweaps.com/index_tech/tech-090.php) |
| Hopkinson–Cranz cube-root scaling; Taylor–Sedov blast radius; conventional HE peaks near 5000 °C. | Explosion size and timing from charge mass, not an artist radius. §6.1. | [LLNL-JRNL-845394](https://www.osti.gov/servlets/purl/2203616) · [Glasstone & Dolan ch. II](https://atomicarchive.com/resources/documents/effects/glasstone-dolan/chapter2.html) |
| Crater ejecta follows `M(>v) ∝ v^(−3μ)`; ejection angle runs from above 45° near the impact point to 30–40° at 0.6 R; a buried charge throws an inverted cone of 79.5° included angle. | Sample ejecta speed from the power law. The curtain shape emerges; do not model it. §6.4. | [Housen & Holsapple, Icarus 211](https://data.boulder.swri.edu/ksinger/Papers/Housen&Holsapple_2011_Ejecta_from_Impact_craters_Icarus.pdf) · [Icarus S0019103515003875](https://www.sciencedirect.com/science/article/pii/S0019103515003875) · [J. Dyn. Behav. Mater. 2016](https://link.springer.com/article/10.1007/s40870-016-0067-1) |
| Artillery gun projectiles strike at **15–20°** from horizontal, mortars at **70–80°**; low-angle strikes give elliptical craters. | Nothing about an artillery impact is symmetric. §6.5. | [ARES, Crater Analysis](https://armamentresearch.com/an-introduction-to-crater-analysis/) |
| 155 mm fragments throw past **549 m** while the blast-overpressure lethal radius is under 3 % of that. | The dangerous radius is fragments, not the fireball. §6.3. | [J. Trauma, fragment velocities](https://www.researchgate.net/publication/24415884) |
| M256 recoil is **279 mm**, and the gun recoils **38 mm before the projectile leaves the tube**; L30 370 mm; CN120-26 400 mm at 550 kN trunnion load. | Recoil is a constant-force hydraulic brake, not a spring, and it starts before the flash. §7. | [inetres M256](https://www.inetres.com/gp/military/cv/weapon/M256.html) · [CN120-26](https://en.wikipedia.org/wiki/CN120-26) |

Use real references for placement, colour, shape, occlusion and temporal ordering. Evaluate the house and ground in neutral daylight and in the actual winter preset. For VFX capture a bright sky scene and a dark near-wall scene at fixed exposure.

---

## 2. Asset inventory, licensing and import

### 2.1 Licensing gate — read before building anything on these

Embedded author and licence metadata, from the GLB JSON. **[P]**

| Asset | Author | Licence | Shippable commercially |
|---|---|---|---|
| `fantasy_house_with_interior.glb` | CGMeller | **CC-BY-NC-4.0** | **no** |
| `stone_with_moss.glb` | Plutonium.software | **CC-BY-NC-4.0** | **no** |
| `update_dirt_road_through_forest.glb` | 99.Miles | CC-BY-4.0 | yes, with attribution |
| `old_stone_wall_full_of_moss_-_scan.glb` | Tijerín Art Studio | CC-BY-4.0 | yes, with attribution |
| `bridge.glb` | Nikolayy | CC-BY-4.0 | yes, with attribution |
| `cc0__apple_moss_bartramia_pomiformis.glb` | ffish.asia / floraZia.com | CC0-1.0 | yes |
| Three reference photographs | — | **no verified terms** | **no** |

The two NC assets are the two the rest of this spec studies most closely. That is fine — they are **material language to imitate with original geometry**, which is what §3 and §4 describe. Do not ship their meshes or textures. The photographs are colour and coverage references only; author production maps from scratch.

SHA-256 checksums for the whole bundle are in `ASSET-MANIFEST.md`.

### 2.2 The house, parsed

537,281 triangles, 11,123 primitives, 22,374 nodes, 5 materials, 10 images. **[P]**

| Material | Primitives | Triangles | % | Maps | alphaMode | metallic | KHR specularFactor → F0 |
|---|---|---|---|---|---|---|---|
| 0 `tile` | 42 | 112,205 | 20.9 | 4096² BC, 2048² MR, 2048² N | OPAQUE | 0.0 | 0.075 → **0.0030** |
| 1 `decal` | **11,061** | **325,386** | **60.6** | 4096² RGBA | BLEND | 0.0 | 0.075 → 0.0030 |
| 2 `uniq` | 18 | 82,014 | 15.3 | 2048² BC, 1024² MR, 1024² N | OPAQUE | — | 0.045 → 0.0018 |
| 3 `ground` | 1 | 8,680 | 1.6 | 2048² BC, 1024² MR, 1024² N | OPAQUE | 0.0 | 0.045 → 0.0018 |
| 4 `water` | 1 | 8,996 | 1.7 | none; `COLOR_0` mean 0.332 | OPAQUE | 0.0 | 0.373 → 0.0149 |

**Sixty per cent of the house is one alpha-blended material in eleven thousand primitives. That is not a grunge layer.** Decomposed by mesh name: **[P]**

| Group | Primitives | Triangles | % of asset |
|---|---|---|---|
| `GROUND\|GRASS\|Dupli\|_decal` | 7,000 | — | — |
| `TREE_low_poly\|LEAVE\|Dupli\|_decal` | 3,200 | — | — |
| `BUSH_low_poly\|LEAVE\|Dupli\|_decal` | 850 | — | — |
| **All `Dupli` vegetation cards** | **11,050** | **320,500** | **59.7 %** |
| **Authored decals** | **11** | **4,886** | 0.91 % |
| — of which the *weathering* set | 5 | **1,624** | **0.30 %** |

Median vegetation-card primitive: **40 triangles**. These are grass and leaf cards exported one at a time instead of instanced, and they are also where the 22,374 nodes come from. **They are the asset's dominant import problem, not the house.** The building proper — tile, uniq, ground, water and the eleven authored decals — is **60 primitives and 216,781 triangles**.

The eleven authored decals: **[P]**

| Mesh | Tris | Role |
|---|---|---|
| `BOTTLES_low_poly_decal_0` | 2,408 | prop cut-outs |
| `LEAKING_WALL_decal_0` | 799 | **vertical streak band** |
| `ALPHA_WINDOW_low_poly_decal_0` | 792 | glazing |
| `LIANE_decal_0` | 346 | **hanging stringy moss** |
| `MOSS_decal_0` | 338 | **moss patches** |
| `LEAKING_BOTTOM_decal_0` | 101 | **ground-contact waterline band** |
| `ASH_decal_0` | 40 | **soot** |
| `GRASS_decal_0` | 40 | grass tufts |
| `LEAVE_1/2_decal_0` | 20 | leaves |
| `PICTURE_low_poly_decal_0` | 2 | painted banner |

The five bolded meshes are the entire weathering decal system of a building that reads as heavily weathered: **1,624 triangles, 0.30 % of the asset**. That is the lesson to copy.

**The tile atlas is a packed library of horizontally-tileable materials**, not a trim sheet. Measured UV ranges: **[P]**

| Mesh | u range | v range | Reading |
|---|---|---|---|
| `ROOF` | 0.01 … 0.99 | 0.50 … 0.62 | band, full-width, 12 % of v |
| `BOXS` | 0.03 … 1.02 | 0.66 … 0.98 | band |
| `STONE_WALL` | 0.70 … 0.89 | 0.02 … 0.24 | small square region |
| `FLOOR_INSIDE` | 0.37 … 0.91 | 0.27 … 0.47 | mid band |
| `CARCASS` | −0.16 … 1.63 | 0.64 … 0.99 | **tiled 1.8× in u** |
| `STAIRS_MAIN` | −0.84 … 1.93 | 0.65 … 0.99 | **tiled 2.8× in u** |

Bands: cream lime plaster (~40 % of area), dark lichened stone, rusted iron column, and plank bands in tan, blue-grey ×2 and brown. **The plank bands are one pattern recoloured** — identical blotch and knot structure. Low-frequency mottling is painted into every band's albedo.

Three further parsed facts:

- `TEXCOORD_0`, `_1` and `_2` are present on every tile/uniq primitive with **identical ranges**. An export artefact, not a second UV set. **[P]**
- Every material's F0 is 10–20× below physical. This over-corrects for the "wet plastic" failure. Copy the intent, not the number: **F0 0.02–0.035** and let roughness do the work. **[P]**
- `water` has no textures, no normal animation, no refraction, no depth absorption. Placeholder. **[P]**

### 2.3 The other five, parsed

| Asset | Bounds | Tris | Prims | Mats | Images | Texel density | Note |
|---|---|---|---|---|---|---|---|
| `update_dirt_road_through_forest.glb` | 551 × 111 × 548 u | 341,109 | 27 | 26 | 70 | — | `COLOR_0` on every primitive; **materials 0, 11, 12 bind normal maps to UV1** |
| `stone_with_moss.glb` | 2.34 × 1.12 × 2.13 m | 372 | 1 | 1 | 3 × 4096² | **1448 px/m** | the correct geometry/texture ratio |
| `old_stone_wall_full_of_moss_-_scan.glb` | 8.77 × 0.84 × 32.91 m | 1,000,000 | 12 | 1 | 1 × 4096² | **230 px/m** | **binds albedo as emissive with `emissiveFactor [1,1,1]` — the wall glows** |
| `cc0__apple_moss_bartramia_pomiformis.glb` | 0.124 × 0.048 × 0.055 m | 1,390,394 | 28 | 2 | 17.8 MP | ~20,000 px/m | `KHR_materials_unlit`; lighting baked into albedo |
| `bridge.glb` | 5.48 × 6.10 × 16.72 m | 1,932 | 1 | 1 | 2 × 1024² | — | usable as-is if span and clearance match |

**The road scene is the ground reference.** Its 26 materials are a complete taxonomy, and the structural fact is that **the road itself is a BLEND layer painted over `Dirt_Road_Bare`**, as are the ruts, the gravel fringe, the puddles and the embedded stones: **[P]**

| OPAQUE base and forms | BLEND layers over them |
|---|---|
| `Dirt_Road_Bare`, `Ground_Dirt`, `Mud_Pile`, `Sloped_Rock`, `Tall_Cliff`, `Broken_Rocks`, `Cobblestone`, `Terrain_Far`, `Grass_Close`, `Aerial_Grass`, trunks | `Dirt_Road` (10,052 t), `Dirt_Road_Trails` (5,292), `Road_Edge_Gravel_Dusty` (3,126), `Puddle_Streaks` (3,693, roughness 0.778), `Rock_Decal` (20,529), `Fallen_Maple_Leaves` (11,505), `Fallen_Generic_Leaves` (7,649), grass and canopy cards |

`Grass_Vegetation_Dry` and `Grass_Vegetation_Green` **share normal and MR textures** and differ only in albedo. Ground layers total ~66k triangles over ~42 × 42 m ≈ **37 tris/m²** — cheap. Vegetation and trunks are 220k of the 341k.

**Unit calibration.** Three assets are already in metres. For the two that are not, measure a known object; do not guess from overall bounds. **[A]**

```
s = L_real / L_file
House:  total height 4059 u, assume ~20 m tower house  -> s ≈ 0.0049  -> 12.6 × 20 × 13.9 m
Road:   Wood_Log minor axis 3.95 u, assume 0.30 m log  -> s ≈ 0.076   -> 41.9 × 8.4 × 41.6 m
```
Verify by importing, placing the player capsule, and checking door clearance and fence height.

### 2.4 Import pipeline

```
FOR EVERY SUPPLIED GLB

1  Scale        multiply node translations and mesh positions by s (2.3), then
                validate against the player capsule.
2  Strip        remove a UV set ONLY IF no material in the file references it
                AND its values match UV0.
                House: UV1/UV2 safe to strip (saves 979,482 × 16 B = 15.7 MB).
                Road:  materials 0, 11, 12 bind their normal map to UV1. Keep it.
3  Fix          clear emissiveTexture where it equals baseColorTexture
                (old_stone_wall will otherwise glow at full albedo).
                clear KHR_materials_unlit; de-light the albedo (§4.5).
                raise F0 from 0.003 to 0.02-0.035; raise roughness instead.
4  Split        separate vegetation cards from authored decals even though they
                share a material. They need different draw order, LOD and culling.
5  Instance     the 11,050 Dupli cards become one card mesh + a per-instance
                transform buffer, or are discarded and re-scattered with the
                engine's own foliage system (§4.6). Do not merge them as static
                geometry.
6  Flatten      bake node transforms into vertices; discard the hierarchy except
                for animated parts, doors and windows.
7  Merge        group remaining primitives by (material, alphaMode, 32 m cell).
                Target ≤24 visible draw batches for the house excluding separately
                animated entities; verify by renderer capture.
8  LOD          50 % / 20 % / 5 % or impostor. Near 35-80k tris, mid 15-35k,
                distant exterior 4-10k for the first original house.
9  Compress     BC7 sRGB colour, BC5 normals, BC4 or packed BC7 ORM, with fallback.
                A 4096² BC texture with mips ≈ 21.3 MiB; 2048² ≈ 5.33 MiB.
                Log true resident bytes including mips and duplicates.
10 Verify       tangent sign after handedness changes, roughness in G and metal
                in B, premultiplication convention against the runtime blend state,
                authored normalTexture.scale.
```

Expected for the house:

| Stage | Draw batches | Triangles | Vertex bytes |
|---|---|---|---|
| As supplied | 11,123 | 537,281 | ~47 MB |
| After 2–7, vegetation instanced | ~12 | 537,281 | ~17 MB |
| After 8, LOD1 | ~12 | 268,000 | ~9 MB |

### 2.5 Texel density targets

`d = sqrt(texels / surface_area_m²)`, texels per linear metre, at 1080p and 60–70° horizontal FOV. **[A]**

| Class | Range | Target px/m | 2048² covers | 4096² covers |
|---|---|---|---|---|
| Hero prop, weapon in hand | 0.3–1 m | 2048 | 1.0 m² | 4.0 m² |
| Interior wall, floor, door | 1–3 m | 512 | 16 m² | 64 m² |
| Exterior building wall | 3–15 m | 256 | 64 m² | 256 m² |
| Ground under foot | 1–4 m | 512 | 16 m² | 64 m² |
| Ground mid-distance | 4–30 m | 128 | 256 m² | 1024 m² |
| Terrain far | 30 m+ | 16 | 16,384 m² | 65,536 m² |
| Decal / grunge overlay | 1–5 m | 256 | 64 m² | 256 m² |

Measured against those: house tile atlas **547 px/m** (correct); house ground **324 px/m** (under); `stone_with_moss` **1448 px/m** (2.8× over — drop to 2048² and save 40 MB); `old_stone_wall` **230 px/m** (half, while carrying 30,000 tris per metre of run — invert that balance). **[P]**

---

## 3. House construction and shader contract

### 3.1 Build the art test

Build a compact exterior and **two readable interior spaces**, or a room plus loft: outer stone foundation, distinct plaster and timber fields, deep windows, dimensioned frames, a believable floor-to-wall joint, a stair, and a roof with layered edges. Walk it with the existing player capsule. Save matched shots from a street corner, an angled doorway, the foot of the stairs and an upstairs window, under broad overcast and dim interior light.

| Where | Geometry carrying form | Cause-driven material | Selective details |
|---|---|---|---|
| Stone footing | Wall thickness, stone top edges, foundation/terrain contact, stone-size variation | Damp base, moss at persistent wet joints, old patch repairs | Capstones, exposed joints, a small dry span |
| Outer plaster | Crisp bounded panels, real trim reveals, window recesses | Chipped plaster opening to substrate at exposed corners, near sill, below cap; runoff trails follow drainage | Broad quiet fields, an isolated old repair, mineral stains |
| Timber and roof | Beam joinery, end grain, eaves, flashing edge, slate or shingle profiles | Dry protected boards, wet end sections, finish lost near contact edges and exposed grain | Dark drip line, thin snow where it accumulates, an occasional lifted shingle |
| Exterior step and door | Walkable threshold, sill overhang, frame, deep recess | Soil carried inside as a narrow gradient, wet near the drip edge, polished traffic highlights | Shoe scuffs, weathered fasteners, damp corners |
| Interior floor and stairs | Board direction, stair nosings and risers, support below, guard where needed | Worn centre path and tread fronts, rougher edges, grime along wall contacts | Foot of stair looks used; floor under shelves reads sheltered |
| Interior walls and furnishings | Real openings, furniture feet, shelf thickness, stacks at different heights | Dust on ledges, moisture only where it can reach, soot near a plausible source | Several storage arrangements, contact shadows, compact footprint |

Test **2–5 cm** trim edge relief and **12–25 cm** window inset where scale supports it. **[A]** Give wood grain one direction per component and a distinct end-grain patch. Continue floor and wall patterns into real returns. Keep AO at supporting contact points and out of direct light.

### 3.2 Material parameters

Linear Rec.709 base colour. Nothing outside albedo ∈ [0.02, 0.85], F0 ∈ [0.02, 0.05], metallic ∈ {0,1}. **[A]**, with the limits **[M]**. Reference ch 02.

| Material | Linear albedo | sRGB | Roughness | F0 | Normal | Porosity |
|---|---|---|---|---|---|---|
| Lime plaster, clean | 0.52, 0.49, 0.44 | `#C4BFB5` | 0.82–0.92 | 0.028 | 0.4 | 0.45 |
| Lime plaster, weathered | 0.33, 0.31, 0.28 | `#9F9A92` | 0.86–0.95 | 0.025 | 0.6 | 0.45 |
| Oak plank, dry grey | 0.14, 0.13, 0.11 | `#6B6760` | 0.72–0.88 | 0.030 | 0.8 | 0.65 |
| Oak plank, warm | 0.13, 0.085, 0.045 | `#684F3C` | 0.70–0.86 | 0.030 | 0.8 | 0.65 |
| Oak plank, dark aged | 0.055, 0.040, 0.028 | `#463A30` | 0.68–0.84 | 0.030 | 0.9 | 0.65 |
| Limestone, dry | 0.17, 0.17, 0.16 | `#737370` | 0.70–0.90 | 0.035 | 1.0 | 1.00 |
| Limestone, lichened | 0.21, 0.22, 0.19 | `#80837A` | 0.80–0.95 | 0.030 | 1.0 | 1.00 |
| Clay roof tile | 0.095, 0.050, 0.035 | `#5B4034` | 0.75–0.90 | 0.030 | 0.9 | 0.55 |
| Slate | 0.030, 0.032, 0.036 | `#343639` | 0.45–0.70 | 0.035 | 0.7 | 0.30 |
| Iron, painted | 0.045, 0.045, 0.048 | `#3E3E40` | 0.35–0.55 | 0.040 | 0.5 | 0.25 |
| Iron, bare | F0 = 0.56, 0.57, 0.58 | — | 0.25–0.45 | metal | 0.6 | 0.05 |
| Rust, mature | 0.085, 0.035, 0.013 | `#543322` | 0.80–0.98 | 0.035 | 1.0 | 0.90 |
| Soil, dry | 0.085, 0.060, 0.038 | `#534438` | 0.88–0.97 | 0.030 | 1.0 | 1.00 |
| Soil, wet | 0.030, 0.021, 0.013 | `#332A22` | 0.25–0.55 | 0.030 | 0.8 | 1.00 |
| Moss, healthy | 0.055, 0.105, 0.030 | `#3E5630` | 0.78–0.92 | 0.035 | 1.0 | 1.00 |
| Moss, dry | 0.085, 0.095, 0.045 | `#514F36` | 0.85–0.95 | 0.035 | 1.0 | 1.00 |
| Algae film | 0.040, 0.065, 0.030 | `#35452C` | 0.70–0.88 | 0.030 | 0.3 | 1.00 |
| Lichen, crustose | 0.28, 0.29, 0.24 | `#4A4C3D` | 0.90–0.95 | 0.032 | 0.5 | 1.00 |
| Soot / scorch | 0.012, 0.012, 0.012 | `#1E1E1E` | 0.90–1.00 | 0.020 | 0.2 | 0.80 |
| Standing water | 0.012, 0.014, 0.013 | `#1F211F` | 0.02–0.12 | 0.020 | 0.0 | 0.00 |

Moss swatches cross-check against the rev 1 photo-derived values `#394331`, `#596846`, `#857B55`: those are **encoded sRGB of photographed moss under its own exposure**, which is why they are lighter than the physical albedos above. Use the table for material authoring; use the rev 1 swatches when matching the photographs directly. Both are in `data/materials.json`.

### 3.3 Layer blend — merged formulation

Layer slots: `substrate, finish, deposited_dirt, moss, snow`, with wear and exposure as masks. Encode a geometric height in [0,1] where 1 is a raised feature; set height conventions at bake time and never substitute luma for height.

This combines rev 1's degenerate-case handling and alpha-space roughness with rev 2's variance restoration. Both halves are necessary: without the first it produces NaNs and invisible-layer bugs, without the second every transition band is visibly flatter than either input.

```text
# N competing solid layers. q = authored nonnegative weights. h = sampled heights.
# beta = 0.24, blendWidth = 0.22, epsilon = 1e-5.            [A]

Q = sum(q)
if Q <= epsilon:
    w[baseLayer] = 1; goto APPEARANCE              # guaranteed fallback

qi   = q[i] / Q                                     # consider only qi > epsilon
si   = qi + beta * (h[i] - 0.5)
sMax = max(si over active i)
gi   = qi * max(0, si - sMax + blendWidth)          # qi factor: an inactive layer
                                                    # can never become visible
G    = sum(gi)
w[i] = (G > epsilon) ? gi / G : onehot(argmax qi)

APPEARANCE:
C      = sum(w[i] * C_linear[i])
Cmean  = sum(w[i] * C_mean[i])                      # C_mean = each layer's 1×1 mip
C      = Cmean + (C - Cmean) * inversesqrt(sum(w[i]*w[i]))    # variance restoration
alpha  = sum(w[i] * roughness[i]^2)                 # blend in alpha space, not roughness
rough  = sqrt(clamp(alpha, 0.04^2, 1))
```

`inversesqrt(Σwᵢ²)` is exact for uncorrelated inputs of equal variance: blending K samples with weights summing to 1 gives σ′² = σ²Σwᵢ², so dividing by √(Σwᵢ²) restores it. It equals 1 at the ends and 1.414 at a 50/50 blend.

Blend height first, then appearance with identical weights across related channels. Select the **top two authoring weights** for lower-tier sampling, renormalise, and bake paints needing more than two layers. Test at saturated and zero-mask conditions. For materials crossing the metal/dielectric boundary use explicit coverage.

Normals: decode to tangent space and normalise. Mix like-material normals with the same weights in a shared tangent frame. For small detail over a base use RNM:

```text
Nd = normalize(vec3(strength * detail.xy, detail.z))
T  = base + vec3(0,0,1)
U  = Nd * vec3(-1,-1,1)
N  = normalize(T * dot(T,U) / max(T.z, 1e-4) - U)
```

For triplanar cliff detail, transform each projected normal into world space before weighting, accounting for axis signs.

**Specular anti-aliasing is mandatory** on any surface with a detail normal; without it every high-frequency normal shimmers under a moving light. **[M]**

```text
variance = 0.25 * (dot(dFdx(N),dFdx(N)) + dot(dFdy(N),dFdy(N)))
kernel   = min(2 * variance, 0.18)
alpha    = roughness^2
roughness = sqrt(min(1, alpha + kernel))
```

### 3.4 Weathering fields

Reserve scalar fields for damp bias, exposure/wear, runoff and moss paint in asset-local UV1 or the existing paint-mask system. Bake `cavity`, `shelter`, `up` and ground contact from geometry. Evaluate road and terrain masks in metres. Noise bands: **1.5–5 m** broad, **0.2–0.8 m** patch, **0.015–0.08 m** edge. **[A]**

```text
# y = height above local foundation or threshold, in metres.
splash  = (1 - ss(0.06, 0.55, y)) * splashPaint
damp    = sat(0.65*splash + 0.45*runoff + 0.30*moisture + 0.12*cavity)
wear    = sat(traffic*wearPaint + 0.35*exposedEdge*wearPaint)
reach   = exp(-pow((y - 1.05) / 0.45, 2.0))          # hand height; multiply into
                                                     # polish, scuff, finish loss
stain   = sat(paintedStain * (0.55 + 0.45*patchNoise))
Cwet    = C * lerp(1.0, coveredDryToWetScale, damp)
Rwet    = lerp(R, wetEndpointRoughness, damp)
Cfinal  = lerp(Cwet, stainColorLinear, stain * stainOpacity)
```

`coveredDryToWetScale` **0.75–0.92** for soil and stone. Roughness endpoints: dry stone **0.60–0.85**, persistently damp stone **0.35–0.60**, dry moss **0.70–0.94**, wet moss **0.55–0.83**, standing water **0.07–0.19**. **[A]**

Every geometric mask must be roughened or it reads as a decal:

```text
roughen(m, noise, spread, threshold, contrast) =
    sat((m + (noise - 0.5)*spread - threshold)*contrast + 0.5)
```

| Effect | Mask | Cut-off | Exponent | spread | contrast | Band |
|---|---|---|---|---|---|---|
| Dust on ledges | up | 0.55 | 2.0 | 0.35 | 2.0 | — |
| Snow | up | 0.30 | 1.5 | 0.25 | 3.0 | — |
| Soot under arch | down | 0.40 | 2.5 | 0.40 | 2.0 | — |
| Rain streak below sill | down + flow | 0.20 | 1.0 | 0.60 | 4.0 | 0.6–2.0 m run |
| Splash dirt | splash | — | 1.6 | 0.45 | 3.0 | h = 0.35 m |
| Rising damp | splash | — | 2.2 | 0.30 | 2.5 | h = 0.70 m |
| Moss on wall | shade × splash × cavity | — | 1.0 | 0.55 | 5.0 | h = 1.20 m |
| Algae waterline | splash | — | 6.0 | 0.70 | 8.0 | h = 0.25 m |
| Edge chipping | curvature | 0.35 | — | 0.50 | 30.0 | — |
| Handle polish | curvature × reach | 0.25 | — | 0.20 | 4.0 | 1.05 ± 0.45 m |

Tint stains in linear space. If an existing decal stores straight alpha, convert once at output for premultiplied blending. Give normal, colour and roughness separate influence settings — a decal should not paint identical darkness into every channel. Do not multiply baked scan shadow and contact AO at full strength together.

### 3.5 Decal meshes

The technique the supplied house is built on: thin double-sided geometry a few millimetres off the host surface, alpha-blended, sharing one atlas. **1,624 triangles do the weathering for the whole building.** Use it for anything authored that follows a specific edge, sill or broken surface; use projected decals for anything the game spawns (bullet holes, scorch, blood, tracks).

```
CONSTRUCTION
1  Select host triangles intersecting the region; duplicate and clip.
2  Offset along the smoothed vertex normal:
       wall:   eps = max(0.0015, 0.0008 * d_near)      metres
       ground: eps = 0.0040 + 0.0008 * d_near          (grazing angles)
   Keep eps/d < 0.0008.
3  UVs: streak/waterline bands -> u = arclength / tileLength, v = (y-y0)/bandHeight
       patch cut-outs          -> planar along the host normal, fitted to the rect
4  Taper alpha over 3-8 % of the shorter axis at the clip boundary.
5  Copy the HOST's smoothed vertex normal into the decal vertices. Do not recompute.
6  State: depth test LEQUAL, depth write OFF, bias constant -2 / slope-scale -1.0
   (sign per the project's depth convention).
```

Split the two tiling bands: author the core alpha-tested (`alphaCutoff ≈ 0.4`) and render it in the opaque pass, leaving only the soft lower fringe blended. That removes most of the sorting and blend cost.

### 3.6 Per-instance variation

Three identical houses is the tell. Variation comes from per-instance constants, not per-instance textures. Work in YCoCg so value and hue separate:

```text
Y = 0.25r + 0.5g + 0.25b ;  Co = 0.5(r - b) ;  Cg = -0.25r + 0.5g - 0.25b
rotate (Co,Cg) by hueAngle, scale by satScale, scale Y by valScale, invert.
```

| Parameter | Range | Source |
|---|---|---|
| hueAngle | ±0.12 rad | hash(instanceID) |
| satScale | 0.80–1.15 | hash |
| valScale | 0.78–1.18 | hash |
| roughness offset | ±0.06 | hash |
| atlas band index | 1 of N wood bands | hash |
| decal set index | 1 of M variants | hash |
| UV offset in band | ±0.5 in u | hash |

Plus one world-space macro noise at 64 m and 11 m octaves, modulating albedo value 0.80–1.20 and roughness ±0.07. One tap, and it removes the "same material everywhere" read. **[A]**

Detail layer at 8–16× the base tiling, faded out by 5–12 m: a single shared 512² BC5 detail normal and 512² BC4 detail roughness (0.5 MB with mips) serves the whole project. **[A]**

---

## 4. Forest floor, road and moss

### 4.1 The six flatness cues — the acceptance test

Ground reads as flat when the eye gets no parallax between near and far features as the camera moves, and no occlusion between them. Six independent cues, each measurable. **The supplied house ground fails five of six; the supplied road scene passes all six.** **[P]**

| # | Cue | Test | Failing | Target |
|---|---|---|---|---|
| 1 | Silhouette relief | vertical range over a 4 m window | < 0.05 m | 0.15–0.6 m |
| 2 | Sub-mesh parallax | does texture shift against mesh on a 0.3 m camera translation | no | POM depth ≥ 0.02 m |
| 3 | Scale bands | independent spatial frequencies in albedo | 1 | ≥ 4 |
| 4 | Contact occlusion | AO darkening within 0.15 m of contact | none | 0.30–0.70 |
| 5 | Object relief | ground-resting meshes per m² | 0 | 0.3–3 |
| 6 | Material boundaries | distinct ground materials in a 10 m view | 1 | 3–6, height-blended |

A seventh, weaker: water pools in the low places rather than wetting everything equally.

### 4.2 The 40 × 30 m reference slice

Extend 25–35 m of winding road past one enterable house, one wall base and a shaded forest edge. One shoulder slopes into a shallow drain; the other rises into roots and scree. Put a log and a cluster of damp mossy stones off-road, a puddle in the lower rut, and a remnant of snow on the protected shoulder. Preserve gameplay grade requirements and usable crossings. Compare at drive, stand and crouch eye heights.

| Relief wavelength | Features | Build path |
|---|---|---|
| 5–35 m | Road grade, valley shape, drainage line, raised forest edge, bank | Terrain mesh or heightfield. Verify continuity at tile edges and bridge approaches. |
| 0.45–5 m | Ruts, compacted hump, soft filled edges, camber, scoured bank, large roots | Small road mesh or heightfield edits. Physics shape coherent with visual grading. 0.02–0.08 m rut depth in the lane, 0.10–0.30 m off-road. |
| 0.035–0.60 m | Soil clods, roots, protruding gravel, twigs, moss cushions | Sparse instancing and occasional authored mesh shoulders. Keep major roots and clods for grazing angles. |
| < 0.04 m | Fine grit, leaf fragments, needles, pores | Normal, roughness, albedo. Fade sub-pixel frequencies, preserve broad roughness. |

### 4.3 Road cross-section

Road spline with arc coordinate `s` and signed lateral `x` normal to the road. Capped cross-slope with a smooth lateral fade into authored terrain.

```text
rut(x,center,sigma) = exp(-0.5*((x-center)/sigma)^2)
C    = (crown(s) - drainageBias(s)*sign(x)) * abs(x)/max(w(s),0.1)
R    = leftDepth(s)  * rut(x,-trackOffset(s),rutSigma(s))
     + rightDepth(s) * rut(x, trackOffset(s),rutSigma(s))
rim  = 1 - ss(0.72*w, 1.22*w, abs(x))
zRoad  = zSpline(s) - C - R + boundedRelief(s,x)
zFinal = lerp(zExistingTerrain, zRoad, rim)
```

Half-width **1.6–2.6 m**, crown difference **0.03–0.075 m** centre-to-edge, `rutSigma` **0.11–0.24 m**. **[A]** Move rut depth, wetness and width slowly along the spline. Leave uninterrupted access at gates and stairs.

**Split the height field between geometry and parallax** or the ground looks inflated:

```text
e_world = px_target * 2 * d * tan(fovY/2) / H_px        px_target = 8-16
   at 1080p, 55°, 12 px:  d=2 m -> 0.023 m ; 5 m -> 0.058 ; 15 m -> 0.173 ; 40 m -> 0.462
h_mesh = lowpass(h, cutoff = 2 * e_world)               displaced as geometry
h_pom  = h - h_mesh                                      left to POM
```

Sample the material at the **undisplaced** position or the texture slides. Clamp swells above ~0.25 m unless collision is displaced too. **Bake one collision surface from the Medium displacement and use it at every tier.**

### 4.4 Soil-to-litter selection

Author `compaction, grit, needle_litter, moisture, slope, drainage, snow` and exclusions.

```text
trail        = 1 - ss(0.53*w, 1.07*w, abs(x))
flowHold     = ss(depthLow,depthHigh,localBasinDepth) * (1 - ss(0.03,0.13,abs(localGrade)))
mudQ         = trail * sat(0.25 + 0.55*moisture + 0.35*flowHold)
gravelQ      = gravelPaint * (0.5 + 0.5*ss(0.43*w, 0.95*w, abs(x)))
litterQ      = (1 - trail) * shelteredLitter * (1 - floodMask)
poolCoverage = floodMask * ss(0.006, 0.02, waterPlaneZ - terrainZ)
```

Feed these as `q` into §3.3. Keep pools physically flat within each depression and force the normal to geometric inside them. Add a 2–5 cm damp meniscus ring outside the water edge. Puddles dry from the edges inward over 60–600 s. **[A]**

**Two tiling scales per layer**, blended by distance, with an irrational-ish ratio so the periods do not beat:

```text
mix( tex(uv/2.0), tex(uv/17.0), sat((dist - 8)/22) )     then variance-restore per §3.3
```

**Parallax occlusion mapping** is the highest-value single fix for cue 2:

```text
maxOffset = V_ts.xy / max(V_ts.z, 0.3) * depth          0.3 clamp kills grazing swim
steps     = mix(STEPS_MAX, STEPS_MIN, abs(V_ts.z))
linear search, then one secant refinement
h' = h - 0.5                                            centre the field or props sink
fade depth to 0 over the last 30 % of the draw range
```

**Deformation buffer** for ruts, tracks, footprints and craters — one camera-anchored RG16 target shared by all of them:

```text
R = depression depth 0..0.35 m     G = disturbance 0..1 (clears moss, grass, litter)
write:  depth = min(depthMax, load_kPa / soilStiffness_kPa * 0.1)
        0.02-0.06 m jeep, 0.10-0.25 m tracked in soft soil, 0.00-0.01 m on hard road
read:   terrain displacement -= R ; normal perturbed by grad(R)
        grass/moss density *= (1-G) ; wetness += 2R   (water pools in ruts)
decay:  R *= exp(-dt/tau), tau 120-600 s soil, 20-60 s slush
memory: 96 m at 4 cm/texel = 2400² × 4 B = 23 MB ; at 8 cm = 5.8 MB
```

### 4.5 Moss — merged placement

Combines rev 1's attach/habitat/patch/M/G skeleton (which correctly separates ground from wall cases and yields a threshold for instanced relief) with rev 2's richer moisture, shade and substrate terms.

```text
groundAttach = ss(0.22, 0.85, dot(Ngeo, Up))
wallAttach   = wallMossPaint * (0.55 + 0.45*cavity)
attach       = max(groundAttach * groundReceivesMoss, wallAttach)

moisture     = sat( 0.40*pow(sat(1 - y/1.4), 1.8)       # rising damp, y = height above grade
                  + 0.25*dripMask                        # from roof/sill geometry
                  + 0.20*cavity
                  + 0.15*sat(1 - distToWater/8) )
shelter      = 0.60*(1 - sat(sunHours/6)) + 0.40*sat(dot(N, dirShade)*0.5 + 0.5)

habitat      = attach * sat( 0.42*moisture + 0.30*shelter
                           + 0.18*substratePorosity + 0.10*rootProximity )
patch        = ss(0.36, 0.65, habitat + 0.22*(noise_0p6m - 0.5)
                                      + 0.17*(noise_0p08m - 0.5))
M            = patch * (1 - ss(0.18, 0.62, traffic))
                     * (1 - explicitExclusion) * (1 - visibleSnowCoverage)
G            = ss(0.50, 0.83, M) * approvedSilhouetteSite
```

`substratePorosity` from the §3.2 table: lime mortar 1.00, granite/concrete 0.75, brick 0.70, weathered timber 0.65, clay tile 0.55, plaster 0.45, painted timber 0.25, steel/glass 0.05.

**`M` is appearance, `G` is instanced relief.** Three representations driven from the one field:

| Level | What | Silhouette | Use at |
|---|---|---|---|
| Film | albedo + roughness shift only | none | > 12 m; algae at any range |
| Mat | height-blended layer with its own normal, 5–25 mm | implied | 2–12 m |
| Cushion | 3D instances where `G` passes | **real** | 0–4 m |

**Moss fills joints first**: drive the height blend with the *host's* height, not the moss's. Blend depth `d = 0.045`. **[A]**

**Moss needs transmission** or it reads as green felt:

```text
wrap = 0.35
diff = sat((N·L + wrap) / (1+wrap)^2)
back = pow(sat(dot(V,-L)), 6.0) * 0.55 * thickness
lit  = lightColor * (albedo*diff + transmitColor*back)
transmitColor (linear) = (0.10, 0.21, 0.045)
```

**Cushion scatter:** candidate spacing **0.2–0.45 m**, cushion width **0.03–0.16 m**, relief **0.006–0.055 m**, at least three distinct source-baked silhouettes. **[A]** Align clump +Z to `lerp(surfaceNormal, Up, 0.35)`, then rotate about it by a **smooth noise field sampled at p × 0.5** so neighbours lean together — random per-clump rotation reads as shuffled, a noise field reads as grown. **[M]** Sink 20–35 % of height. Reject inside doors, under hidden snowbanks, on active roads, and in puddle centres. Lift leaf tips off the ground while keeping roots embedded. Keep patches sparse on exposed dry high stone.

**Lichen is the inverse of moss** — dry, sun-facing, high. Drive it with `L = sat(shelter_inverse × age × (1 − moisture))` from the same field. A wall with moss low and north, lichen high and south, has a history for free.

**Do not apply wind to moss or lichen.** Only the hanging `LIANE`-type strands and ferns move, at 0.004–0.012 m amplitude and 0.4–0.9 Hz. **[A]**

### 4.6 Scatter and contact

| Element | Size | Density | Draw distance | Form |
|---|---|---|---|---|
| Large stones | 0.2–0.6 m | 0.02–0.08 /m² | 60 m | mesh, 200–600 tris, 3 LODs |
| Small stones | 0.03–0.12 m | 0.5–3 /m² | 18 m | mesh, 40–120 tris, 2 LODs |
| Gravel | 0.01–0.03 m | — | 6 m | POM + sparse decal, never meshes |
| Exposed roots | 0.4–2 m | 0.01–0.05 /m² near trees | 40 m | mesh |
| Twigs | 0.1–0.5 m | 0.3–1.5 /m² | 14 m | 2–4 tri cards |
| Leaf litter | 0.03–0.1 m | clustered | 20 m | decal sheets, not per-leaf |
| Grass clumps | 0.1–0.5 m | 8–60 /m² | 25–45 m | 6–20 tri cards, instanced |
| Moss cushions | 0.03–0.16 m | per `G` | 4 m | §4.5 |

**[A]** Placement rules matter more than density: stones **sink** 25–45 % of their height; stones **cluster** (two-level Poisson: cluster centres at 0.01–0.03 /m², then 2–8 /m² inside a 1–3 m radius); litter accumulates against walls, in ruts and on lee sides; grass avoids paths and ruts; nothing scatters above the angle of repose (~34° gravel, ~40° rooted soil).

Spawn within each chunk from a stable integer hash of map seed, chunk coordinate and candidate cell, or the existing seeded scatter. Retain an instance when its stable seed maps below the density multiplier, with a distance cross-fade — **do not reshuffle positions when switching presets.**

Contact (cue 4) is the cheapest of the six: a small projected AO decal under every resting object, darkening the ground to AO 0.35–0.70 within 0.10–0.25 m. Where the project can cache terrain shading into a texture, have resting meshes sample it and blend over 0.05–0.20 m.

---

## 5. Combat VFX event and rendering contract

### 5.1 Author separate visual events

| Event | Position and orientation | Owned outputs |
|---|---|---|
| Small-arms fire | Existing fired event ID; weapon world and viewmodel muzzle sockets at event time | Brief flash, tiny lit puff if visible, cheap local bounce. Both views synchronised to the same event. |
| Heavy-weapon / artillery discharge | Existing fired event transform on the firing model's barrel axis and visual-time pose | Directional flash, short lit source smoke, **ground dust disc** if a loose surface qualifies, local recoil pose. |
| Ground or surface impact | Existing impact position, surface normal, substrate, visual scale bucket | Flash per recipe, opaque clods or chips, thin expanding dust, lit irregular plume, transient decals, crater write. |
| Residual plume | Impact-local spawn point with world wind and valid scene depth | Delayed expansion and dissipation after the core event. Shared bounded buffers for nearby plumes. |

Event payload: `{id, tick, visTime, recipe, pos, orientation, normal, surface, seed, owner}`. Build layer seeds from stable hashes. Use event age from a single simulation clock; use display interpolation for transforms. Suppress duplicate late replication and reject expired phases; a late update may show a continuing plume at its age. **Store the muzzle transform at visual emission and detach emitted smoke into world space** — later rifle sway, elevation or turret traverse must not move it. Only the short attached flash follows the source pose.

### 5.2 Rendering contract

Render in scene HDR with scene depth and local lighting. Emission, opaque debris and soft smoke on separate paths.

```
- Apply fog exactly once, with the correct eye-to-particle path.
- First-person viewmodel depth and projection may differ from world depth.
  Provide a matching render path or depth correction for the viewmodel flash,
  and separately reject world emission and light across solid blockers.
- Turn off global autoexposure response to small high-luma VFX. If the meter
  supports exclusion, exclude; otherwise reject the top 4 % of weighted pixels
  in the histogram and freeze adaptation for 150 ms around own-weapon fire.
  Rate-limit EV: 0.6-1.2 stops/s down, 2.0-4.0 up. Hard-clamp to the level range.
- Keep screen bloom tied to emission: unthresholded, multi-radius (0.004, 0.011,
  0.028, 0.071, 0.18, 0.45 of screen height, weights 0.40/0.24/0.16/0.10/0.07/0.03),
  total energy 0.4-2 % of the source. A thresholded bloom forces the peak clamp
  in 5.4 down by a factor of 3.
- Emissive particle buffers must be R11G11B10F minimum. RGBA8 cannot hold these
  values.
```

### 5.3 Flash geometry and palette

**Geometry.** The muzzle gas is a highly underexpanded supersonic jet; its structure is set by the pressure ratio. One relation places everything: **[M] for the relation, [D] for the table.**

```text
x_Mach = 0.67 * D * sqrt(p_muzzle / p_ambient)        D = bore (m), p_ambient = 101325 Pa
```

| Weapon | D (mm) | p_muzzle (MPa) | x_Mach (m) | calibres | Envelope L | Max radius |
|---|---|---|---|---|---|---|
| 5.56×45 | 5.56 | 45 | **0.079** | 14 | 0.24 m | 0.036 m |
| 7.62×51 | 7.62 | 55 | 0.119 | 16 | 0.36 m | 0.054 m |
| 7.62×39 | 7.62 | 40 | 0.101 | 13 | 0.32 m | 0.045 m |
| 12.7×99 | 12.7 | 60 | 0.207 | 16 | 0.70 m | 0.093 m |
| 20 mm | 20 | 70 | 0.352 | 18 | 1.2 m | 0.158 m |
| 40 mm Bofors | 40 | 55 | 0.624 | 16 | 2.5 m | 0.281 m |
| 76 mm | 76 | 45 | 1.073 | 14 | 4.3 m | 0.483 m |
| 105 mm | 105 | 60 | 1.712 | 16 | 7.5 m | 0.770 m |
| **120 mm** | 120 | 70 | **2.113** | **18** | **9.5 m** | 0.951 m |
| 125 mm | 125 | 65 | 2.121 | 17 | 9.5 m | 0.954 m |
| 155 mm howitzer | 155 | 50 | 2.300 | 15 | 11 m | 1.035 m |

The relation reproduces both independently reported disc positions: 7.9 cm against an observed ~7.5 cm for small arms, and 18 calibres against an observed 20–25 for large guns. **Compute it; do not hand-place the disc.**

Profile for the swept core mesh: `r(x) = R · (x/x₀)^0.45 · exp(−1.2(x/x₀ − 1)²)`, `x₀ = 0.6 x_Mach`. Half-angle at the muzzle 12–18° plain, 25–35° braked, 8–12° flash-hidden.

**Palette.** Propellant combustion is 1645–2500 K. That is **orange**, and the pale core is exposure clipping, not the source. **[M] temperatures, [D] colours** (Planck integrated against the CIE 1931 CMFs; validated against the published Planckian chromaticities at 5778 K and 6500 K).

| Component | T (K) | ε | Linear RGB (max = 1) | sRGB hue |
|---|---|---|---|---|
| Primary flash | **1645** | 0.12 | (1.000, 0.193, 0.000) | `#FF7A00` |
| Intermediate disc | 1800 | 0.20 | (1.000, 0.224, 0.000) | `#FF8200` |
| Secondary flash | **2500** | 0.25 | (1.000, 0.380, 0.068) | `#FFA64A` |
| Tank flash core | 2400 | 0.55 | (1.000, 0.355, 0.050) | `#FFA23F` |
| Ember remnant | 1300 | 0.70 | (1.000, 0.120, 0.000) | `#FF5F00` |

The rev 1 warm-tip value **(1.00, 0.36, 0.075)** is within 2 % of the derived 2500 K secondary flash — two independent routes agreeing. The rev 1 core **(1.00, 0.87, 0.70)** is a ~4600 K source; replace it with the physical value and let exposure produce the white.

### 5.4 Brightness — the two conventions and the bridge

rev 1 authored peak as a multiple of `Yref`, the median scene-linear luminance of a lit 18 % reference patch under the actual shot exposure. rev 2 authored absolute nits. Both are usable; here is the conversion, so the project can adopt the first now and the second later without re-tuning.

```text
Yref = 0.18 * E / pi          E = scene illuminance in lux
```

| Condition | E (lx) | Yref (nits) | EV100 |
|---|---|---|---|
| Full sun, clear | 100,000 | 5730 | 15.5 |
| Hazy sun | 60,000 | 3438 | 14.7 |
| Overcast bright | 20,000 | 1146 | 13.2 |
| Overcast dull | 10,000 | 573 | 12.2 |
| Deep shade / canopy | 2,000 | 115 | 9.8 |
| Interior, lit | 300 | 17.2 | 7.1 |
| Dusk | 50 | 2.87 | 4.5 |
| Full moon | 0.25 | 0.0143 | −3.1 |

Physical source luminance, and the same thing as a multiple of `Yref`: **[D]**

| Source | L_v (nits) | × Yref sun | × Yref overcast | × Yref interior | × Yref moonlight |
|---|---|---|---|---|---|
| Rifle primary flash | 1.25 × 10³ | **0.22** | 2.2 | 72 | 87,000 |
| Rifle intermediate disc | 7.5 × 10³ | 1.3 | 13 | 437 | 524,000 |
| Rifle secondary flash | 4.5 × 10⁵ | **78** | 783 | 26,100 | 3.1 × 10⁷ |
| Tank muzzle flash core | 6.5 × 10⁵ | 113 | 1,134 | 37,800 | 4.5 × 10⁷ |
| Tracer 7.62 | 4.1 × 10⁵ | 72 | 720 | 24,000 | 2.9 × 10⁷ |
| HE fireball, t = 5 ms | 8.7 × 10⁶ | 1,513 | 15,130 | 504,500 | 6.1 × 10⁸ |
| HE fireball, t = 50 ms | 5.0 × 10⁵ | 86 | 865 | 28,800 | 3.5 × 10⁷ |
| Glowing fragment | 1.5 × 10² | 0.025 | 0.25 | 8.4 | 10,100 |

Two things fall out immediately. **A rifle's primary flash is 0.22 × Yref in daylight — invisible.** What you see in daylight footage is the secondary flash and ejected unburnt powder. And **every combustion source clips by three to eight orders of magnitude at night**, which is why the hue survives only at the optically thin rim.

**The authored peak.** rev 1's 8–16 × Yref is not wrong; it is the correct clamp for a pipeline with no bloom to carry the overbright. Use:

```text
peak = min( L_v * eps / Yref , CLAMP )
```

| Bloom and tonemap | CLAMP | Why |
|---|---|---|
| Unthresholded, multi-radius, ≤2 % energy, filmic | **none — use the physical value** | the PSF tail carries the halo; a 78× source gives a small white core, not a washed screen |
| Unthresholded, single-radius gaussian | 60 | one wide gaussian spreads too much energy at high peak |
| Thresholded bloom | 24 | above this the threshold makes bloom pop discontinuously |
| No bloom | **16** | nothing carries the overbright; this is rev 1's range, and it is right here |

Keep rev 1's SDR proof checks: peak small core **0.82–0.96 display luma**, thin outer lobes **0.32–0.72**, surrounding walls brightening only near the source. If an entire entrance, tree line or aiming area washes out, **the bloom is wrong, not the peak** — fix the bloom before lowering the peak.

### 5.5 Envelope and frame-rate independence

A rifle flash is **1–2 ms**, which at 60 fps is 0.12 of a frame. **[M]** Sampling it instead of integrating it makes brightness track frame rate. rev 1's formulation is the correct one and is adopted unchanged:

```text
life     = flashEnd - flashStart
u        = sat((eventAge - flashStart) / life)
envelope = (1 - u)^2                    # zero outside [flashStart, flashEnd]
P        = peak * envelope * emissionMask * normalizedRelativeRGB

For a render interval covering part of the flash, integrate the envelope over its
overlap with the interval. Antiderivative of (1-u)^2 is  u - u^2 + u^3/3.
Multiply the difference by `life`, divide by the presentation interval, and clamp
the averaged emission to the intended peak. Resolve each interval once per
presentation. Where a minimum readable cue is required, apply a bounded peak floor
only for the frame containing the shot.
```

That last clause is what stops 30 Hz players both missing the shortest flashes and getting disproportionately long fireballs. Use the same time domain on every quality setting.

### 5.6 Flash construction

```
FOUR ELEMENTS, NOT ONE SPRITE

E1 CORE MESH   swept-profile mesh (5.3), additive, emissive from T(t), oriented
               along the bore, scaled to x_Mach. 60-200 tris. A real 3D shape:
               a billboard seen from the side is the most obvious tell there is.
E2 DISC        one quad normal to the bore at x_Mach, dished to an 80° half-angle,
               brightest at its inner edge: alpha = pow(1 - r/R, 0.6)
E3 PUFF        1-3 soft lit sprites for the secondary-flash vortex and smoke,
               with motion-vector frame blending
E4 GROUND DUST the disc of 5.7, for large calibre or prone fire
```

Per-shot variation: roll the core mesh about the bore by `hash(shotIndex, weaponID) × 2π`; scale by **±12 % at most** — real flashes are consistent, and large random scaling reads as a game effect. One rotated atlas variant per seed. Use a compact irregular core with two or three low-alpha off-axis lobes; test from a side orbit and straight down the camera axis; never let a billboard clip over the barrel silhouette or stretch into the reticle.

Visibility: small flash **12–35 ms** of virtual presentation time across the style set; heavy core **35–85 ms** with a longer dim edge envelope. A large impact may retain a few small hot patches after the initial flash. Dark environments should read through local lighting rather than extra atlas area.

Local lights: unshadowed, from the existing pool, approved by wall occlusion, smooth falloff between inner range and cutoff, modulated by the same envelope. **Never make viewmodel and world light additive duplicates.** A forward-renderer fallback applies an emissive bump to the weapon or the first visible surface only. **The spectacle must read with local lights disabled.** Derive intensity and radius rather than typing them: **[D]**

```text
Phi   = pi^2 * d^2 * eps * sigma*T^4 * eta_v     lumens   (eta_v from reference ch 05)
I     = Phi / (4 pi)                              candela
r_cut = sqrt(I / E_cut)                           E_cut = 0.05 lx night, 2 lx day
```

| Source | d (m) | T (K) | ε | I (cd) | Illuminance at 10 m |
|---|---|---|---|---|---|
| Rifle muzzle flash | 0.15 | 2200 | 0.20 | 5.2 × 10³ | 52 lx |
| Tank muzzle flash | 2.5 | 2400 | 0.55 | 5.1 × 10⁶ | 5.1 × 10⁴ lx |
| 155 mm fireball at peak | 7.0 | 3000 | 0.90 | 1.1 × 10⁸ | 1.1 × 10⁶ lx |
| Grenade fireball at peak | 2.0 | 2800 | 0.85 | 4.5 × 10⁶ | 4.5 × 10⁴ lx |

For comparison, full daylight is ~10⁵ lx. A tank muzzle flash at 10 m is half of daylight; a 155 mm burst at 10 m is ten times it. Both must visibly relight the scene.

### 5.7 Muzzle blast, ground dust and smoke

The propellant gas is itself an explosion. Treat it as an equivalent charge: **[D]**

```text
W_eq = m_propellant * (3.9 / 4.184) * f_blast      f_blast = 0.30-0.40   [A]
blast sphere R = 3.77 * W_eq^(1/3)                 ground dust radius = 2.5 R   [A]
```

| Weapon | m_prop (kg) | W_eq (kg) | Blast sphere | Ground dust radius |
|---|---|---|---|---|
| 5.56×45 | 0.0017 | 0.0006 | 0.31 m | 0.8 m (prone only) |
| 12.7×99 | 0.0152 | 0.0050 | 0.64 m | 1.6 m |
| 40 mm Bofors | 0.300 | 0.098 | 1.74 m | 4.3 m |
| 76 mm | 1.10 | 0.359 | 2.68 m | 6.7 m |
| 105 mm | 5.60 | 1.83 | 4.61 m | 11.5 m |
| **120 mm** | 7.90 | 2.58 | **5.17 m** | **12.9 m** |
| 155 mm howitzer | 9.00 | 2.94 | 5.40 m | 13.5 m |

**The ground dust disc is the most recognisable feature of real large-calibre firing and rev 1 has no model for it.** **[M]** that muzzle blast raises dust from prone firing and from large guns firing low and flat; **[A]** for the shape:

```
Trigger when the muzzle is low over a loose surface (dust, sand, dry soil, snow).
Origin:    ground point below the muzzle, offset 0.3-0.8 x_Mach forward
Shape:     annulus, inner 0.55 × outer, height 0.15-0.6 m, strongly asymmetric:
           1.0 forward, 0.55 sideways, 0.25 behind for a plain muzzle;
           near-symmetric with a brake, with two lobes along the port angle
Expansion: r_o(t) = R_max * (1 - exp(-t/0.055)) ; stops by t ≈ 0.25 s
Opacity:   0.5-0.8 dry dust, 0.1-0.2 damp, 0.0 wet / grass / concrete
Count:     18-40 soft sprites, each rotated so its long axis is tangential
```

Smoke volume at the muzzle: rifle 0.02–0.1 m³ (invisible outdoors in daylight); **suppressed rifle 0.1–0.4 m³ — more, not less, released late**; 12.7 mm 0.3–1 m³; tank gun 3–15 m³; artillery 20–100 m³. **[A]**

**Bore evacuator:** 0.4–1.5 s after the shot, a second smaller puff emerges from the evacuator bulge and the muzzle. One extra emitter, instantly recognisable, and absent from rev 1.

### 5.8 Muzzle devices

| Device | x_Mach | Envelope | Dust | Secondary flash | Lateral lobes |
|---|---|---|---|---|---|
| None | ×1.0 | 1.0 | 1.0 | present | 0 |
| Flash hider | ×0.9 | 0.45 | 0.6 | **largely removed** | 3–5 thin jets |
| Muzzle brake | ×1.0 | 0.70 | **1.6, side-lobed** | present | 2 at the port angle |
| Suppressor | ×0.35 | 0.25 | 0.4 | removed | 0; **2.5× smoke, delayed 0.15 s** |
| Compensator | ×1.0 | 0.85 | 1.2 | present | 1 upward |

A flash hider removes the *secondary* component specifically — this is measured, by firing into inert gas — and leaves the primary and intermediate unchanged. **[M]** Do not model it as a uniform 60 % scale-down.

### 5.9 Heavy artillery presentation

Two clearly distinct cues: **at the firing position**, the short warm flash, directional source smoke, ground dust disc and brief rig travel; **at the impact location**, the surface-shaped bright source, dirt and evolving dust, driven by the separate in-game event. Do not loop one effect at the firing position until impact. Respect the event timestamp and existing visibility rules.

Artillery impacts seen at distance keep the core timing and silhouette with a simplified plume and less fine debris. **Variable render quality must never change game timing or gameplay-relevant obscuration.**

**Sound arrives at 343 m/s** — 1.46 s at 500 m. Implementing the delay is nearly free and is one of the strongest realism cues available.

---

## 6. Dirt, lit smoke and aftermath

### 6.1 Scaling — everything from the charge

Three constants size any explosion. **[M]** for Hopkinson–Cranz and Taylor–Sedov, **[D]** for `t_peak`, **[A]** for `t_lum`.

```text
R_fb   = 3.77 * W^(1/3)     m      maximum fireball radius
t_peak = 0.0139 * W^(1/3)   s      time to R_fb (derived: Sedov radius = R_fb)
t_lum  = 0.10 * W^(1/3)     s      visible luminous duration
R(t)   = 1.03 * (4.184e6*W*t^2 / 1.225)^(1/5)    Taylor-Sedov, strong-shock only
Z      = R / W^(1/3)                              scaled distance
```

**Characteristic times scale as W^(1/3): ten times the charge evolves 2.15 times more slowly.** Large explosions that evolve quickly look like small ones filmed close — the most common scale error in games, and `artistRadius` alone cannot prevent it.

| Munition | W (kg TNT-eq) | R_fb | t_peak | t_lum | Crater D | Column apex | Column life |
|---|---|---|---|---|---|---|---|
| 40 mm M433 | 0.04 | 1.2 m | 4.5 ms | 33 ms | 0.55 m | 4.3 m | 2.0 s |
| M67 grenade | 0.21 | 2.2 m | 8.3 ms | 59 ms | 0.95 m | 7.2 m | 2.8 s |
| RPG-7 PG-7V | 0.52 | 3.0 m | 11 ms | 80 ms | 1.28 m | 10.2 m | 3.3 s |
| 81 mm mortar | 1.09 | 3.9 m | 14 ms | 103 ms | 1.65 m | 13.1 m | 3.8 s |
| 105 mm HE | 2.30 | 5.0 m | 18 ms | 132 ms | 2.11 m | 17.1 m | 4.3 s |
| 120 mm tank HE | 3.30 | 5.6 m | 21 ms | 149 ms | 2.38 m | 19.5 m | 4.6 s |
| **155 mm M107** | **6.60** | **7.1 m** | **26 ms** | **188 ms** | **3.00 m** | **25.0 m** | **5.2 s** |
| 155 mm M795 | 12.40 | 8.7 m | 32 ms | 231 ms | 3.70 m | 31.3 m | 5.8 s |
| 250 kg GP bomb | 128.0 | 19.0 m | 70 ms | 504 ms | 8.06 m | 72.6 m | 8.8 s |

`apex / crater diameter ≈ 8` across three orders of magnitude — a useful sanity check on any ejecta system.

Overpressure for damage radii and physics impulse, Brode's fit: **[M]**

| Z (m/kg^⅓) | Δp (kPa) | Effect |
|---|---|---|
| 0.5 | 5,360 | reinforced concrete fails |
| 1.0 | 826 | brick wall collapse |
| 2.0 | 156 | lethal to personnel in the open |
| 3.0 | 68 | eardrum rupture likely |
| 5.0 | 28 | knocked down |
| 8.0 | 14 | windows shatter |
| 15.0 | 5 | windows crack |

For a 155 mm shell: lethal-in-the-open at **3.8 m**, windows shatter at 15 m. The *fragment* hazard radius is two orders larger — see 6.3.

### 6.2 Phase timeline

Times for a 155 mm point-detonating surface burst; multiply by `W^(1/3)/1.876` for another charge. **[D]/[A]**

| Phase | t | Visible output |
|---|---|---|
| Detonation | 0–0.05 ms | nothing yet at frame resolution |
| Casing break-up | 0.05–0.3 ms | fragment emitters armed |
| **Bright core** | 0.3–3 ms | brilliant near-point white core; T 3200 K, ε → 0.95 |
| Expansion | 3–26 ms | fireball grows to 7.1 m; core clipped white, rim orange |
| **Soil breach and dome** | 10–60 ms | **the ground lifts as a coherent intact hemisphere before breaking** |
| Shock separation | 5–40 ms | fast faint refraction ring; condensation disc in humid air |
| Near-ground dust | 55–180 ms start, 0.5–2.1 s persistence | low disturbed sheet running through ruts and around rocks |
| Cooling | 26–190 ms | brightness collapses as `T^8.1`; smoke appears from outside in |
| Main curtain | 60–600 ms | the inverted cone, widest and densest here |
| Main plume | 0.10–0.24 s start, fades over 1.4–4.2 s | rising irregular loosely connected shapes, lit from sky, shaded at base |
| Clods land | 0.6–3.0 s | impact puffs, bounces, resting debris |
| Column collapse | 2.0–5.2 s | drifting fine dust |
| Settling | 5–30 s | low haze, wind-advected |
| Residue | permanent | crater, scorch, ejecta blanket, scars |

A 155 mm burst is therefore **a 26 ms flash, a 1.5 s dirt column, an 8 s smoke column and 25 s of settling dust.** Most games invert these proportions. The **dome phase** is the detail that sells a delay-fuze or buried burst and is in neither source document: implement it as a short coherent displacement of the terrain mesh plus a cracking decal, then release the particles.

Cooling curve and emission: `T(t) = T_amb + (T₀ − T_amb)(1 + t/τ)^(−n)` with `T₀ = 3200 K, τ = 0.020 s, n = 0.85`; and because the visible band is far out on the Wien tail, **`L_v ∝ T^8.1`**, not `T⁴`:

| t (ms) | T (K) | Relative brightness |
|---|---|---|
| 1 | 3160 | 1.00 |
| 5 | 3030 | 0.71 |
| 20 | 2650 | 0.26 |
| 50 | 2260 | 0.064 |
| 100 | 1950 | 0.019 |
| 200 | 1660 | 0.0048 |

By 200 ms the fireball is 0.5 % of peak. It is already smoke.

Timing curves, `u = t / t_lum`:

```text
flashIntensity(u) = pow(max(0, 1-u), 3.2) * exp(-2.1*u)
fireballRadius(u) = R_fb * (1 - exp(-2.6*u / 0.19))
smokeAlpha(u)     = ss(0.35, 1.3, u)
lightIntensity(u) = I_peak * pow(T(u)/T_0, 8.1)
```

### 6.3 Fragments

Gurney cylinder equation: `V₀ = sqrt(2E) · (M/C + ½)^(−½)`, with `sqrt(2E)` = 2440 m/s TNT, 2700 Comp B, 2930 RDX. **[M]**

| Munition | C (kg) | Casing (kg) | V₀ (m/s) |
|---|---|---|---|
| 81 mm mortar | 0.95 | 2.60 | 1500 |
| 105 mm M1 | 2.30 | 12.70 | 994 |
| 155 mm M107 | 6.60 | 36.30 | 996 |
| 155 mm M795 | 10.80 | 36.00 | 1379 |

**155 mm fragments throw past 549 m while the blast lethal radius is under 15 m.** **[M]** Gameplay and visuals should reflect that asymmetry.

Rendering: spawn in an **equatorial belt**, ~75 % within ±25° of the plane normal to the shell axis, with a forward bias from the shell's own velocity. Draw 40–300 as motion-blurred streaks of length `v·dt` with alpha scaled by `L_ref/(v·dt)` so integrated energy is right. **Most fragments do not glow** — incandescence only for steel casing against hard targets. Spawn impact sub-effects only within ~15 m of the camera.

### 6.4 Ejecta — size decides simulation

One table determines how each piece is simulated. Quartz, ρ = 2650, sea-level air. **[D]**

| D | v_terminal | Stopping length | Render as |
|---|---|---|---|
| 10 µm | 0.008 m/s | 0.06 m | suspended cloud, sprite |
| 50 µm | 0.20 m/s | 0.31 m | suspended cloud, sprite |
| 100 µm | 0.80 m/s | 0.61 m | short-lived sprite |
| 500 µm | 5.5 m/s | 3.1 m | GPU point / streak |
| 2 mm | 11 m/s | 12.3 m | GPU point / streak |
| 10 mm | 24.5 m/s | 61 m | GPU point / streak |
| 30 mm | 42.5 m/s | 184 m | **mesh debris** |
| 100 mm | 78 m/s | 614 m | **mesh debris** |

```
D < 0.2 mm    stopping length < 1.5 m  ->  SUSPENDED CLOUD. Advect with the blast
                                           wind then ambient wind; settle at v_t.
                                           Do NOT give these ballistic arcs.
0.2 - 20 mm                             ->  BALLISTIC WITH DRAG. GPU points.
D > 20 mm                               ->  BALLISTIC, MESH DEBRIS with collision.
```

The three bands' stopping lengths differ by a factor of 1000. **That spread — dust hanging where it was born while clods arc 40 m away — is what reads as an explosion rather than a particle effect.** A single emitter with one velocity range and one drag constant produces a puff, which is why rev 1's single normalised arc needs replacing.

**Speed sampling.** Crater ejecta follows `M(>v) ∝ v^(−3μ)`. Invert for sampling: **[M] law, [D] sampler**

```text
v = v_min * u^(-1/(3*mu))          u ~ U(0,1], clamped to v_max
mu: dry sand 0.41 | damp soil 0.45 | cohesive soil 0.50 | competent rock 0.55
v_min = sqrt(g * R_crater)
v_max = 2.5 * 20 * W^0.18
```

With `v_min = 4 m/s` and dry sand, that gives 7.0 m/s at u = 0.5, 26 m/s at u = 0.1, 169 m/s at u = 0.01. **Most of the mass is slow and a thin tail is very fast** — one emitter producing a dense low skirt *and* a sparse high column.

**Launch geometry.** `ξ = x/R_crater`, area-weighted: **[M]**

```text
xi    = sqrt(U(0,1])
theta = 52° - 22°*xi                 elevation from the surface
```

Anchors: ejection angle above 45° near the impact point, 30–40° at 0.6 R; curtain angle 52°–63°; buried charge inverted cone of 79.5° included angle. **The inverted-cone curtain emerges from this distribution — do not model the cone directly.**

Expressed in rev 1's normalised renderer form so it drops into the existing clod path, with the ratios now *sampled* rather than authored:

```text
v_sample = v_min * U(0,1]^(-1/(3*mu))
theta    = radians(52 - 22*sqrt(U(0,1]))
rangeRatio  = (v_sample^2 * sin(2*theta) / g) / artistRadius
heightRatio = ((v_sample*sin(theta))^2 / (2*g)) / artistRadius
# then rev 1's path, unchanged:
v      = sat((age - clod.start)/clod.life)
radial = rangeRatio  * artistRadius * (1 - pow(1-v, 1.6))
lift   = heightRatio * artistRadius * 4*v*(1-v)
p      = spawn + tangentDir*radial + upwardDir*lift
```

Keep rev 1's distribution targets as a cross-check: roughly **55–70 %** of drawn clods in low spreading arcs, **25–40 %** medium, the remainder as high accents — the power law reproduces that split naturally. Keep its other rules: denser at the centre with a few far silhouettes, irregular clustered angles rather than an angle-index grid, bias away from a steep wall with falling driven by world up, terminate on depth entry or a baked ground-height function on low tiers, remove fragments on ceiling intersection, **no rigid bodies for hundreds of particles**, opaque chips at full resolution pruned by projected size, pooled lifetimes.

### 6.5 Surface recipes

The same shell on different ground is a different effect. One table indexed by the material under the burst, not shader work, and the highest-value single addition to a generic explosion system.

```c
struct SurfaceResponse {
    vec3  ejectaAlbedo;       // linear
    float ejectaMassScale;    // 0.0 water .. 1.6 dry sand
    float dustToFireRatio;    // 0.1 airburst .. 4.0 dry dust
    float craterDepthScale;   // 0.0 rock/water .. 1.3 soft soil
    float clodFraction;       // chunks vs dust
    float sparkRate;          // rock, concrete, metal only
    float wetness;
    uint  decalSet, soundSet;
};
```

| Surface | Clods and particles | Colour and shape | Follow-through | mass | dust/fire | crater |
|---|---|---|---|---|---|---|
| Dry loam and litter | irregular ochre-grey dirt, some needles, few stones | dust sampled and modestly lightened from substrate; upward lobes plus a low flank | scuffed patch, dark soil tone; thin dust travels farther than clods | 1.4 | 3.5 | 1.2 |
| Dry sand | fine, very few chunks | pale, huge suspended plume | broad light blanket | 1.6 | 4.0 | 1.3 |
| Wet clay or mud | fewer fines, denser dark compact pieces, small flashing droplets | deep brown near source, smaller soft cloud, dim grey fringe | short damp marks, bounded splash streaks | 1.1 | 0.6 | 1.1 |
| Grass / turf | sods, roots, green fragments | brown plus green | scorched ring | 1.0 | 1.2 | 0.95 |
| Old snow over soil | thin white-grey powder, darker clods where soil exposed | two tones briefly, transparent cool powder | disturbed slush, exposed darker edge | 1.5 | 3.0 | 1.4 |
| Stone, brick, paved | angular chips matching the stone; no wet organic clumps | pale desaturated dust, tight footprint, slight wall-tangent bias | local scuff, chips, grey deposit | 0.5 | 2.2 | 0.10–0.25 |
| Wood | very few splinters | warm grey-brown | plank mark | 0.4 | 0.8 | 0.0 |
| Shallow water | droplets and spray | pale spray, white column | ripple response | 0.9 | 0.0 | 0.0 |
| Airburst (> 3 m) | none | clean spherical fireball, circular fragment pattern | none | 0.0 | 0.1 | 0.0 |

Sample colour once at event material lookup. Dust colour `mix(substrateLinear, neutralWarmGrayLinear, 0.20..0.45)`, passed through skylight rather than tinted orange. Opaque dry dirt near substrate colour with a **0.8–1.18** linear multiplier and subtle hue variation. Shade opaque clods with ambient and direct scene light. Use 3–5 jagged mesh silhouettes across sizes. Dark wet clods against bright snow must stay distinguishable. Avoid a uniform grey bubble or evenly spaced points.

**Impact angle.** Artillery guns strike at **15–20°**, mortars at **70–80°**. **[M]** Nothing about an artillery impact is symmetric:

```text
axis          = normalize( mix(surfaceNormal, reflect(v_impact, surfaceNormal), 0.55) )
density(phi)  = 1 + 0.9*cos(phi - phi_downrange)        1.9× downrange, 0.1× uprange
crater axes   = semi-major/semi-minor = 1/sin(elevation), clamped [1, 3.5]
```

Fuze cases: **PD** standard; **delay** gives the camouflet — bigger column, deeper crater, almost no fireball, pronounced dome; **proximity** gives no crater, no column, a clean spherical fireball and a circular fragment pattern.

### 6.6 Lit smoke compositing

Smoke is **not emissive**. Keep it out of the additive flame buffer. rev 1's compositing is correct and adopted unchanged:

```text
A             = 1 - exp(-densityScale * densityTex * pathLengthProxy)
fadeAtSurface = ss(0.0, fadeMeters, opaqueDepthMeters - particleDepthMeters)
alpha         = A * fadeAtSurface * ageFade
ambient       = skyResponse * skyIrradiance + groundResponse * groundBounce
sunTerm       = sunResponse * directLight * extinctionShadow
scatterColor  = dustAlbedo * (ambient + sunTerm)
Cp            = alpha * scatterColor
outRGB        = Cp + dstRGB*(1 - alpha)          # source over, premultiplied
```

Use baked flipbooks with `frame0=floor(f)`, `frame1=min(frame0+1,last)`, `blend=fract(f)`, `f = sat(age/life)*last`. **Motion-vector frame blending** at Medium+ reduces an 11×11 (121-frame) sheet to 6×6 (32 frames) at equal smoothness; do not block-compress the motion-vector texture. Ensure atlas gutters survive mips. Offset phase, rotation, view variation and scale by seed.

**Six-way lighting on hero smoke.** Two RGBA textures: A = (right, top, back, alpha), B = (left, bottom, front, emissive).

```text
w   = Lts * Lts                                  # squared direction; components sum to 1
lit = w.x*(Lts.x>0 ? A.r : B.r)
    + w.y*(Lts.y>0 ? A.g : B.g)
    + w.z*(Lts.z>0 ? A.b : B.b)
# 4-channel variant, half the memory:
frontMap = 0.25*(lm.x + lm.y + lm.z + lm.w) ;  backMap = 0.6 * frontMap
```

**While the fireball is alive, the smoke is lit from inside.** Feed the fireball's dynamic light into the six-way evaluation and the column goes orange underneath and dark on top — the single most recognisable feature of a real burst, and free once the basis exists.

**Dust scatters forward.** Multiply the lit colour by `1 + 1.4·pow(sat(dot(V,−L)), 4)` so dust between the camera and the sun lights up. This is why dust columns are bright against the sun and dark with it behind you.

Smoke albedo over time: 0.04 fresh soot → 0.12 at 2 s → 0.25 at 5 s as soil dust mixes → 0.45 at 15 s. Column rise `z(t) ≈ z₀ + C√t`, `C = 6–12 m/s^½`, lateral growth `r(z) = r₀ + 0.25(z − z₀)`. **[A]**

In overcast weather use a visibly lit top edge, darker interior folds and cool environment colour. Aim for a neutral lit perimeter and an internally darker core with readable depth through thinner parts. **Keep opacity and colour independent so lit smoke can be dark grey without becoming a solid black wall.** Render soft dust and smoke to a separate depth-aware target where available, upsample with depth rejection, and composite into HDR once; use full-resolution or corrected edge pixels where simple upsampling leaks near fences, terrain or bridge rails. Small or far effects can be cheaper drawn directly into the full-res transparent queue.

### 6.7 Crater and residue

```text
D = 1.6 * W^(1/3) * craterDepthScale      depth = 0.4 D      lip = 0.12 D
profile(r) = -depth*(1 - (r/R)^2)^1.6  +  lip*exp(-((r-R)/(0.35R))^2)
```

Write into the deformation buffer of §4.4 — craters, ruts and footprints share one target, one displacement path and one collision query.

| Layer | Radius | Persistence |
|---|---|---|
| Crater geometry | R | permanent; displacement + collision |
| Scorch / soot decal | 1.8 R | long; weathers in rain |
| Ejecta blanket decal | 3.5 R | long; alpha ~ (R/r)³, patchy |
| Scattered clod meshes | 2–8 R | pooled, capped, merged to static |
| Vegetation kill | 1.4 R | permanent, via the disturbance channel |
| **Fragment scars** | up to 30 R | medium; **directional** |

Fragment scars are cheap and strongly diagnostic: trace 20–60 rays in the §6.3 distribution and place a small pock decal where each hits a surface within 30 m. Players read the pattern as evidence of where the round landed.

Blast interaction: impulse `J = Δp(Z)·A_projected·t_positive/2` with `t_positive ≈ 1.8e-3·W^(1/3)·Z^0.35` **[A]**; radial wind impulse `v = 18·(W^(1/3)/R)^1.4` decaying with τ = 0.3 s, bending grass and foliage out and back; dust burst from any loose surface within 2.5 R_fb, which is what produces dust shedding off nearby rooftops and ledges; camera shake amplitude ∝ √Δp(Z) over 0.3–1.2 s; low-pass the mix for 0.4–2 s within Z < 4, never flat silence.

---

## 7. Heavy recoil presentation

Animate tank and anti-tank render rigs from the existing fire event. Preserve existing aim, projectile origin, reload rules, server state and colliders.

### 7.1 Rig and transform order

| Node | Tank | Towed anti-tank |
|---|---|---|
| Root | Existing vehicle position and heading | Existing carriage placement |
| Cosmetic mass reaction | Delayed hull compression or pitch; preserve the hierarchy used by passengers and the world camera | Delayed carriage pitch or compression; planted or spring-like wheel visuals maintaining ground contact |
| Aim pivots | Existing turret traverse and barrel elevation | Existing traverse and elevation joints |
| Recoil slide | Barrel assembly slides on its local axis relative to cradle and mantlet | Barrel and connected slide move inside a largely fixed cradle |
| Muzzle and fittings | Render socket parented to the barrel; cosmetic point sampled at event time | Same; aligned across elevation and mounting orientations |

Keep a stable per-rig `forwardLocal` axis and a verified rest pivot. Apply `poseLocal = restLocal * translate(-forwardLocal * travel)` in the engine's convention. Height and yaw adjustments rotate the translation with the weapon. Test at max elevation and oblique traverse. **Avoid applying travel twice** to children such as the muzzle socket or viewmodel overlay. Source effects align to the current shot; lingering FX use the captured world pose. Use mesh clearance in the mantlet or cradle to define maximum allowed travel `D`, and do a local clip check at maximum withdrawal.

### 7.2 The motion — a gun is not a spring

A hydraulic recoil brake meters oil through a variable orifice so the resisting force stays nearly constant; that is what minimises peak trunnion load. Constant force means **constant deceleration**: a parabola out, and a much slower parabola back under the recuperator. rev 1's cubic-ease/smoothstep pair approximates "fast out, slow back" but gets the ratio and the absolute timing wrong by 2–3×.

```text
RECOIL          a    = V_0^2 / (2L)              constant deceleration
                x(t) = V_0*t - 0.5*a*t^2
                t_r  = 2L / V_0
                F_R  = m_r * a                   force at the trunnions

COUNTER-RECOIL  a_cr = f * a,  f = 0.025-0.06    the recuperator is weak by design
                x(t') = L - 0.5*a_cr*t'^2
                t_cr  = sqrt(2L / a_cr) = t_r / sqrt(f)
                buffer the last 0.10 L to a smooth stop

MOMENTUM        p   = m_p*v_p + 1.30*m_c*v_p     beta = 1.30 (Résal convention)
                V_0 = p / m_r                    m_r = tube + breech + piston
```

Measured strokes and derived timings: **[M]** strokes and masses, **[D]** the rest.

| Gun | L | V₀ | a | t_recoil | t_counter | Cycle | F_R |
|---|---|---|---|---|---|---|---|
| **M256 120 mm** | **279 mm** | 16.9 m/s | 52 g | **33 ms** | **191 ms** | **225 ms** | 970 kN |
| Rh-120 L/55 | 300 mm | 17.9 | 54 g | 34 ms | 194 ms | 227 ms | 1039 kN |
| L30 120 mm (HESH) | 370 mm | 9.2 | 12 g | 81 ms | 431 ms | 511 ms | 202 kN |
| 2A46M 125 mm | 300 mm | 11.1 | 21 g | 54 ms | 313 ms | 367 ms | 511 kN |
| M68 105 mm | 305 mm | 15.1 | 38 g | 41 ms | 234 ms | 274 ms | 478 kN |
| Pak 40 75 mm | 920 mm | 11.7 | 8 g | 158 ms | 788 ms | 946 ms | 52 kN |
| ZiS-3 76 mm | 900 mm | 10.0 | 6 g | 180 ms | 902 ms | 1082 ms | 29 kN |

The computed 970 kN for the M256 over 280 mm cross-checks against the published 550 kN for the CN120-26 over its longer 400 mm stroke — longer stroke, lower force, comparable momentum.

**A tank gun snaps back in 33 ms and takes 190 ms to come home. A towed 76 mm field gun takes 180 ms back and nearly a second to return.** That asymmetry is the whole character of large-gun recoil.

**The gun recoils ~38 mm — 14 % of its stroke — before the projectile leaves the tube.** **[M]** The barrel is already moving when the muzzle flash appears. Sequence the rig track to start ~3 ms *before* the flash event, not on it.

```c
// Drop-in. Returns metres of rearward travel.
float gunRecoilX(float t, float L, float V0, float f) {
    float a = V0*V0 / (2.0f*L);
    float tr = 2.0f*L / V0;
    if (t <= 0.0f) return 0.0f;
    if (t < tr)    return V0*t - 0.5f*a*t*t;
    float acr = f*a;
    float tcr = sqrtf(2.0f*L / acr);
    float t2  = t - tr;
    if (t2 >= tcr) return 0.0f;
    float x = L - 0.5f*acr*t2*t2;
    return x * smoothstep(0.0f, 0.10f*L, x);     // counter-recoil buffer
}
```

Where a rig's mesh clearance `D` is less than the real stroke, scale by `travelFraction = D/L` and keep the timing. Keep rev 1's portion-of-clearance guidance (0.55–0.80 tank, 0.65–0.88 towed) as the clamp. Where the project prefers an authored curve for a signature weapon, layer it on top of this; do not replace it.

### 7.3 What the vehicle does

```text
dV_hull  = p / M
I_pitch  ≈ M * (0.35 * L_hull)^2
omega    = p * h / I_pitch                h = trunnion height above the CG
theta    = omega / (2*pi*f_n)
heave/pitch then decay:  x'' + 2*zeta*omega_n*x' + omega_n^2*x = 0
```

| Platform | p (kN·s) | M (t) | ΔV | h | f_n | ζ | **Peak pitch** |
|---|---|---|---|---|---|---|---|
| M1A2 / M256 | 32.2 | 62.0 | 0.52 m/s | 1.50 m | 1.2 Hz | 0.32 | **0.77°** |
| T-72B / 2A46M | 27.7 | 44.5 | 0.62 | 1.30 | 1.3 | 0.30 | 0.97° |
| M60A3 / M68 | 19.3 | 48.0 | 0.40 | 1.50 | 1.2 | 0.30 | 0.79° |
| **Pak 40 towed** | 8.2 | 1.43 | 5.75 | 0.55 | 2.6 | 0.20 | **8.9°** free |
| ZiS-3 towed | 5.2 | 1.12 | 4.66 | 0.50 | 2.7 | 0.20 | 7.1° free |

Gun elevation splits the impulse: `p·cos(elev)` into translation and pitch, `p·sin(elev)` into heave. At 0° all pitch; at 45°, 71 % each.

**Towed carriages do not pivot about their CG.** Once the spades are bedded the carriage rotates about the spade contact line, `I_spade = I_cg + M·d²`, which roughly triples `I` and reduces pitch to **2–4°** — but the wheels lift clear by **30–120 mm**, which is the recognisable hop. The first shot from an unbedded position displaces the whole gun **0.2–0.8 m** rearward and throws soil from the spades; subsequent shots move it centimetres. Model bedding as `displacement *= exp(-shotCount/1.5)`, and write the spade trenches into the deformation buffer. A muzzle brake removes 25–50 % of the impulse into the mount — scale `p` for the hull, **not** the tube's stroke before the brake acts.

rev 1's pitch seeds (0.15–0.45° tank, 0.35–0.85° towed) are low; use the derived values, scaled by `travelFraction` if the rig is compressed. Keep rev 1's rule for consecutive events: a bounded envelope from the active previous and current curves (e.g. maximum absolute setback) rather than discontinuous resets or summed rig clipping. Recoil must return exactly to the local bind pose.

### 7.4 Secondary motion

| Element | Timing | Note |
|---|---|---|
| **Barrel whip** | during and after the stroke | a 5.3 m tube at 52 g deflects visibly: first bending mode 8–15 Hz, 10–40 mm at the muzzle, decaying over 0.15–0.4 s. Animate as a bend, not a rigid rotation. Mode shape `0.5s²(3−2s) + 0.5s⁴`. |
| **Dust shed from the vehicle** | 5–60 ms | every horizontal surface sheds accumulated dust. One of the most recognisable real details, and absent from rev 1. |
| Mantlet shudder | 0–200 ms | 2–6 mm |
| Suspension travel | 15–600 ms | near-side road wheels visibly compress |
| Case ejection | 230–500 ms (tank), 20–60 ms (small arms) | hot, tumbling, rings on landing |
| Crew reaction | 0–400 ms | gunner absorbs hull motion with a 60–120 ms lag |
| Spade soil spray | 0–200 ms (towed) | from under the trails |
| Blast-flattened grass | 0–300 ms | radial wind impulse, §6.7 |
| Bore evacuator puff | 400–1500 ms | §5.7 |

Most apparent movement must be along the bore-aligned **game mesh** axis even at a steep viewing angle. Lift, sway and hull pitch are secondary. Keep the external view first-priority and the camera impulse bounded; for the inside view use existing accessibility shake settings, preserve reticle precision, and use smaller impulses than the exterior. Fade tiny accessory motion by projected size. Check that reload still looks correct with no state-dependent cosmetic offset.

### 7.5 Small arms

Here the damped spring **is** correct, because the resisting element is a shoulder and a buffer spring, not a metered orifice.

```text
x'' + 2*zeta*omega*x' + omega^2*x = 0 ,  omega = 2*pi*f_n
on fire: v += p / m_effective          # impulses ADD; never reset
pistol 7.0 Hz/0.45 | carbine 6.0/0.50 | battle rifle 5.0/0.50
LMG bipod 4.5/0.60 | HMG mount 3.5/0.70 | sniper 4.0/0.40
settling time = 3/(zeta*omega)
```

| Weapon | p (N·s) | Weapon mass | V_free |
|---|---|---|---|
| 5.56×45 M4 | 5.71 | 3.1 kg | 1.84 m/s |
| 7.62×51 | 11.39 | 4.5 | 2.53 |
| 7.62×39 | 7.14 | 4.3 | 1.66 |
| 12.7×99 M2HB | 54.97 | 38.0 | 1.45 |
| 12 ga slug | 15.70 | 3.6 | 4.36 |

Six degrees of freedom, not one: rearward translation, plus **rise** because the bore axis is above the grip pivot (`rise = back · (h_bore/r_grip) · k`, `k = 0.6–1.0`, `h_bore` 0.04–0.08 m, `r_grip` 0.22–0.30 m), plus small yaw and roll.

In automatic fire, **impulses accumulate in the same spring state**. At 6 Hz and ζ 0.5 the settling time is 160 ms, so above ~375 rpm the weapon never returns to rest and recoil accumulates — **that accumulation is the climb**; do not add a separate scripted climb curve. Keep the first 2–3 shots nearly deterministic. Add the **bolt-carrier second impulse**: 8–25 ms later, opposite sign, 0.15–0.35 amplitude, which is what gives a gas gun its characteristic double-bump.

Camera: `camera_pitch = lowpass(weapon_rise, 8–14 Hz) × coupling`, coupling **hip 0.55, aimed 0.30, braced 0.18, prone 0.15**. Optical sights are bolted to the weapon; **do not add camera shake on top of sight motion** or you double-count and aiming feels mushy.

---

## 8. Quality tiers

Maintain visual identity across settings. Art-authored ground grade, broad wear and moisture masks, cast silhouette, shot and impact cues, animation timing, event anchors and interaction behaviour are **invariant**. Tilt quality toward the effects that take the most screen space. **Keep rendering quality separate from gameplay visibility: if smoke or dust has a game visibility rule, that rule and equivalent major occlusion must hold at every tier.**

### 8.1 The ten things that survive Very Low

A tier may make a thing cheap. It may not make a thing absent if its absence changes what the player believes about the world.

```
1  Exposure and tone curve                      6  Crater and scorch decal
2  Contact AO/shadow under resting objects      7  Gun recoil
3  Aerial perspective                           8  Material identity
4  Muzzle flash + its light                     9  Collision and deformation gameplay reads
5  Explosion flash + dust + decal              10  Audio delay over distance
```

### 8.2 Environment quality

| Feature | Very Low | Low | Medium | High | Ultra High |
|---|---|---|---|---|---|
| House materials | Baked tile + tint/roughness, static wear atlas | Static wear, simple mask blend on marked surfaces | Height blend, 2 competing textures, one normal scale | Up to 3 active layers on near flagged materials, RNM microdetail | Up to 4 locally at hero blend spots, same palette and masks |
| Texture taps / opaque pixel | 1 | 4 | 6 | 10 | 13 |
| Hex tiling | — | — | — | ground and large walls | ground and large walls |
| Detail normal + roughness | — | — | packed, 1 tap | 2 taps | 2 taps |
| Specular AA | **on** | on | on | on | on |
| Structural house detail | Distant shell sooner; retain doors, window depth, roof/stone silhouette | Mid shell sooner, limited furniture | Normal near kit and cell culling | Near trim, selective modelled contact wear | Closest authored trim/props, limited extra shadow casters |
| Decal-mesh overdraw | 0 | 0.15 screens | 0.35 | 0.70 | 1.20 |
| Terrain cross-section | Same grade, baked macro, ordinary normal | Nearer normal and roughness variation | 2-way height blend, short-range micro detail | 3-way near blends, near geometric edges on selected road strips | 4 layers locally, hero strips refined, mips stable |
| Splat layers | 1 | 2 | 4 | 6 | 6 |
| Tiling scales per layer | 1 | 1 | 2 | 2 | 2 + hex |
| POM steps (min–max) | off | offset, 1 tap | 6–16 | 10–28 + self-shadow | 14–40 + self-shadow |
| POM depth | 0 | 0.015 m | 0.030 m | 0.050 m | 0.065 m |
| Displacement | none | none | baked into mesh | tessellated to 45 m | tessellated to 90 m |
| Deformation buffer | off | off | 8 cm / 48 m | 4 cm / 64 m | 4 cm / 96 m |
| Moss | Baked coverage | Baked + coarse normal | Mask blend, limited near cushion mesh | Near cushions, sparse leaf tips, cached contact shading | Extra cushion shapes, a few hero tufts |
| Moss transmission | off | off | **on** | on | on, per-pixel thickness |
| Moss / litter cull distance | 0 m / baked | ≤6 m at approved hero sites | 12 m / 25 m | 23 m / 42 m | 32 m / 55 m |
| Scatter density multiplier | 0.00 optional detail; keep essential authored geometry | 0.30 | 0.60 | 0.90 | 1.00 |
| Reflections | Existing sky response | Sky/local probe mix | Local probes, selective water patch | Existing solution at reduced quality | Best existing mode where measured |
| Texture residency bias | +2 mips optional sets | +1 mip | 0 to +1 by screen size | 0 near hero detail | 0, optional higher-res near hero only |
| VRAM budget | 0.6 GB | 1.2 GB | 2.5 GB | 5 GB | 9 GB |

Use capability-based fallbacks inside each preset, with names exactly as the existing UI shows them. **Never increase shader work on every tile solely because a small garden patch exists.** For stochastic density, retain an instance when its stable seed maps below the multiplier, with a distance cross-fade; do not reshuffle positions on preset change. Implement tier changes as material permutations or stable flags where that beats divergent branches.

### 8.3 Visual effects quality

Per-event values are caps for the shown class in the isolated near test, counting simultaneously visible render instances including fallback proxies. Target one source-event evaluation for multiple render components. For multiple nearby impacts use global limits and preserve the dominant plume and event cues before dropping accents.

| Feature | Very Low | Low | Medium | High | Ultra High |
|---|---|---|---|---|---|
| Small / heavy flash geometry | 1 / 1 keyed silhouette | 1 / 2 | 2 / 2, mixed orientation | 2 / 3 + small irregular tips | 2 / 3, sharper edge material |
| Flash core form | billboard | billboard | **swept mesh, 60 tris** | swept mesh, 120 | swept mesh, 200 |
| Intermediate disc | no | no | **yes** | yes | yes |
| Flash transient lights at once | 0 | 1 | 2 | 3 | 4 |
| Ground dust disc sprites | 0 | 8 (large calibre) | 16 | 28 | 40 |
| Bore-evacuator puff | no | no | **yes** | yes | yes |
| Soft smoke render per axis | 1/4 when the target wins; direct for tiny | 1/4 with edge rejection | 1/2 with depth rejection | 1/2 + full-res correction near depth edges | 1/2 default; full-res only for selected hard-edge foreground |
| Soft plume / ground dust per major impact | 1–2 sprites | ≤4 | ≤8 | ≤13 | ≤18 |
| Suspended dust sprites per burst | 3 | 8 | 20 | 45 | 90 |
| Ballistic grains per burst | 0 | 40 | 200 | 800 | 2500 |
| Opaque clods per major impact | ≤4 | ≤8 | ≤16 | ≤28 | ≤40 |
| Flipbook sampling | Nearest frame, broad opacity | Nearest frame | **2 frames + motion vectors** for the major plume | 2 frames + MV, cheap directional lighting | 2 frames + MV + six-way response near hero impact |
| Smoke lighting | tinted | per-particle | per-vertex | **six-way** | six-way per pixel |
| Forward-scatter term | no | no | **yes** | yes | yes |
| Fragments drawn | 0 | 0 | 40 | 120 | 300 |
| Dome phase | no | no | no | **yes** | yes |
| Debris contact | Baked height or occlusion kill | Floor query at end points | Ground clamp on near clods | Bounded analytic contacts, ≤8 near clods | ≤12 per event, optional brief visual settle |
| Fragment scar rays | 0 | 0 | 0 | 20 | 60 |
| Shock refraction | no | no | no | yes | yes |
| Recoil | Full main slide and timing, simplified secondary | Main slide plus body motion | Full primary and secondary curves | **+ barrel whip**, visible cradle and fitting offsets | Same, more detailed small attachments |
| Vehicle dust shed | 0 | 0 | 6 sprites | 16 | 30 |
| Extra visual work | No extra decals or distortion | One pooled scuff | Sparse pooled derivative details | Limited localised residue | Same policy; optional shimmer near one hot source after measurement |

Resolution is per axis: 1/2 × 1/2 is one quarter the pixels. Select the direct path for tiny effects below the target or where target setup and resolve cost exceed saved shading. Keep the soft composite premultiplied and recheck thin foreground masks in each preset. **Careful pruning usually beats making thick smoke less opaque.** Overlap and source priority must preserve readable primary soft coverage in a downgrade. **Do not scale brightness inversely by sprite count with no cap** — protect scene readability instead of accumulating full emission from parallel overlapping layers.

### 8.4 Resource and performance targets

**[A] throughout. No figure here was measured on Battle Foundry.**

- Repeatable screenshots and routes with a fixed seed. Benchmark **1440p High on the RTX 2080 Ti** when the machine is available. Keep the existing map budget at **≤16.7 ms p95 GPU** for 60 fps. Proposed *incremental* slice allowances under a busy fixed camera: ground and moss **0.75 ms**, VFX **0.95 ms**, transient lights **0.25 ms p95**. These are envelopes, not independent p95 values to add as a guarantee. If the baseline lacks headroom, profile and reduce measured hotspots first. Report actual differences at each tier.
- Global simultaneous visible major soft effect instances: **6 / 8 / 12 / 16 / 22**. Opaque clods across the frustum: **24 / 48 / 96 / 160 / 224**. Sort by projected cloud area, newness and foreground importance. Separate distance/size culling from the global budget.
- Smoke overdraw proxy: `Σ projected area of visible soft quads / (renderW·renderH)`, before clipping if no GPU instrumentation. Record covered-pixel layer count with the engine debug pass. Group three low-density cards or merge puffs when the profile shows more occlusion than visible value. Prefer baked normal/density response and bounded local lights over more quads. Cap alpha layers near the camera. Benchmark reduced-resolution target plus resolve against direct draw before committing.
- Keep current terrain chunk sizes and editor indices. Parallelise bake; derive placement deterministically at cook or edit time. Projected span `p ≈ h·targetHeightPx / (2·d·tan(fovY/2))`: cull most non-hero silhouettes below **~1.5 px**, merge above that if crowded, preserve cutout area in farther mips.
- Preallocate and pool event entries, draw ranges, transient lights and light constants. Keep timers asynchronous, ring-buffer GPU timestamp queries, log p50/p95/p99 GPU and CPU plus worst shot and texture residency. Test zoomed aiming, winter fog, crossing the doorway, and several simultaneous impacts near a terrain or bridge edge.

### 8.5 Dynamic governor

A fixed tier cannot survive a twelve-round barrage. Layer a per-frame governor over the preset.

```
measure:  GPU frame time, exponentially smoothed over 8 frames
target:   16.0 ms  (0.67 ms headroom against 16.67)
down:     smoothed > target*1.08 for 4 consecutive frames -> step down one rung
up:       smoothed < target*0.88 for 30 consecutive frames -> step up one rung

rung |  res  | smoke cap | grains | lights
  0  | 1.00  |   100 %   |  100 % |  100 %
  1  | 0.93  |    80 %   |   70 % |  100 %
  2  | 0.86  |    60 %   |   45 % |   70 %
  3  | 0.80  |    45 %   |   25 % |   50 %
  4  | 0.72  |    30 %   |   10 % |   35 %
  5  | 0.64  |    20 %   |    0 % |   25 %

Rules:
- Internal resolution changes ≤7 % per step, at most once every 8 frames.
- Never change rung during a cutscene or an ADS transition.
- Caps apply to NEW spawns only. NEVER despawn a live effect: an explosion that
  vanishes mid-flight is worse than a dropped frame.
- Retire oldest-first when a cap lowers, fading over 0.3 s.

spawn priority = (screenArea / (1 + d^2)) * importance * recency
    importance: own weapon 4.0 | nearby hostile 2.5 | gameplay 2.0 | ambient 1.0
```

### 8.6 Ladder of cuts

When a scene misses budget, cut in this order:

```
 1 internal resolution -5 %      8 soft-particle buffer resolution   15 detail layer
 2 Ultra-only terms              9 POM steps, depth, then off        16 scatter distance
 3 shadowed effect lights       10 displacement radius, then off     17 deformation buffer
 4 volumetric injection         11 second blended layer              18 dust lifetime
 5 shock refraction             12 moss cushions -> mat              19 concurrency caps
 6 fragment count               13 decal-mesh overdraw               20 texture resolution
 7 smoke count and lifetime     14 mesh ground layers                   (memory, not time)
```

Never on this ladder: the ten items in §8.1.

---

## 9. Integration and acceptance

| Phase | Implement | Acceptance |
|---|---|---|
| **0. Baseline** | Identify engine paths, current quality keys, terrain scale, colour handling, smoke pass, timers, assets, user-visible controls | Stable preset and camera configurations saved; current 60–90 s trace captured. Build opens from normal map and editor entry points. |
| **0b. Licence gate** | Record embedded licences; mark the two NC assets reference-only in the asset database | No NC-licensed mesh or texture is referenced by any shipping map or prefab. CI check on the asset manifest. |
| **1. Import** | Run the §2.4 pipeline on all six GLBs | House imports at ≤24 visible draw batches with vegetation instanced or re-scattered. No UV1 normal map broken on the road. `old_stone_wall` does not glow. `apple_moss` responds to light. Scale verified against the player capsule. |
| **2. House** | Small enterable prototype; wear, tile/unique scaling, layered materials, openings, stair, interior lighting | Readable plaster, wood, stone and soft roughness change at 0.6 m and 8 m. In-view draw batches limited. Usual door and movement controls work. Blend weights sum to 1 ± 1e-4; all-zero masks select the base layer. |
| **3. Forest strip** | Road cross-section, pooled wetness, moss under shade and on exposed edges, litter, root relief, one wall-base integration | **All six flatness cues (§4.1) pass.** At 0.4 m eye height the ground has silhouette and real slopes. Clustered moss has dry gaps and clear road exclusions. No blue streaks or seams at terrain boundaries. |
| **4. VFX events** | Event phases and debug display; calibrate flash from `Yref`; compare lit smoke; wire dirt, texture updates and residual plume | Peaks, opacity, flash and smoke fade deterministic at **30 / 60 / 120 fps within one presentation frame**. Dirt trajectories and material-specific colours captured on dry soil, mud, rock and snow. No bright self-lit default smoke. Flash core clips only from exposure, never from an authored white. |
| **5. Explosion scaling** | Drive `R_fb`, `t_peak`, `t_lum`, crater and column from charge mass; implement the surface-response table | A grenade, a 105 mm and a 155 mm, side by side, differ in size *and* in rate. Column apex ≈ 8 × crater diameter for each. Dry sand and shallow water produce visibly opposite effects. |
| **6. Recoil** | Rig tracks in local axes; verify mesh clearance and mounts; mounted and towed comparisons | Peak translation travels backwards on the intended axis at any aim angle. Frame series shows **fast retreat, slower return at 4–7×**, and body lag. Rig motion begins before the flash frame. No clipping at peak. Same shot timing at all tiers. |
| **7. Presets and profile** | Wire all five settings through use paths; measure and tune; smoke resolve direct vs offscreen | High benchmark checked. Quality changes preserve art intent and meaningful visibility. Runtime enforces emitter, light and instance caps. Governor holds a 12-round barrage. |
| **8. Playthrough and save** | Solo, multiplayer event dedup, map load/reload, existing saves, standard editor workflow | No duplicated effects, broken collision, or reset of saved graphics preferences. Comparison images and measured shortfalls recorded. |

### 9.1 Required review views

1. Locked exterior and doorway camera; the house beside one existing village building. Capture dry, wet, material-mask, albedo-only, roughness and normal views.
2. Closer interior stair/landing, and a view from the door toward a shadowed corner, walking during capture. Watch for noisy AO, banded normal blends, thin wall and window seams.
3. Forest road both directions: eye level, 0.4 m grazing, and plain geometry. Capture terrain slope, moisture, moss, weighted blend and detail scatter maps. Same positions at all five presets.
4. Single event: high-contrast dirt, bright snow, dim wall, artillery source, distant impact. Frame sequence of highlights, clods, ground dust and plume. Render once with flare and bloom disabled to verify source shape. Inspect sky-facing and shadow-facing smoke separately.
5. Tank and towed rig views: side camera at event start, peak, halfway back, settled. Angled turnaround and viewmodel shot. Draw axes and local rest/peak positions once for QA, then save clean footage.
6. Stress run: start at the house, out through the forest crossing, past the bridge or wall; four overlapping test effects from a developer harness. Record draw counts, optional rock/moss detail, smoke pass, texture residency. Fixed seeds.

### 9.2 Cheap assertions and failure diagnostics

```
BLEND
[ ] All-zero masks select the intended base layer.
[ ] An inactive layer at maximum texture height cannot become visible.
[ ] Returned weights sum to 1 ± 1e-4. Normal solution finite; tangent signs checked.
[ ] Neutral RNM detail leaves the base normal unchanged.
[ ] Variance restoration: a 50/50 blend of two layers has contrast within 10 % of
    either input, not half of it.

IMPORT
[ ] Lit PBR output matches stock GLB tooling on a neutral import. If the reference
    looks glossy or faded, check colour-space flags, metallic channel, vertex colour
    and normal signs first.
[ ] No material references a stripped UV set.
[ ] No emissiveTexture equals its baseColorTexture.
[ ] Primitive count per material logged; flag any material with >100 primitives
    as a candidate for instancing.

PLACEMENT
[ ] Road and moss placement stays in world coordinates through morph and cross-fade.
[ ] Twigs do not float; moss does not seam between chunks. Log world seed and
    snapped grounding on failure.
[ ] Preset change does not reshuffle scatter positions.

EVENTS
[ ] No duplicate activation per event ID. Absolute-age skip for expired FX.
[ ] Pooled buffer capacities enforced; reuse behaviour robust.
[ ] Times tested just before and after both curve endpoints.
[ ] Source socket plausible during visible retreat; world puffs left behind.
[ ] Every multipass item sees exposure and fog exactly once.

BRIGHTNESS
[ ] Spark and alpha frames compared at 30 / 60 / 120 presentation rates.
[ ] Bright flash peaks quickly at source, moves no persistent brightness into a
    smoke-only frame, and never pumps automatic exposure.
[ ] If everything looks orange, isolate emission from density and inspect indirect tint.
[ ] If the plume is a solid black or white shape, inspect density, transfer function,
    exposure and opacity coverage separately.
[ ] Rifle primary flash is invisible in full daylight and dominant at night without
    changing its authored value.

SCALE
[ ] Explosion column apex / crater diameter ≈ 8 for every munition.
[ ] Doubling charge mass multiplies fireball radius by 1.26 and all times by 1.26.
[ ] Recoil counter/recoil time ratio between 4 and 7 for every gun.

PERFORMANCE
[ ] Trace saved on the unchanged baseline and repeated after each phase, warm.
[ ] Largest measured regression fixed before adding a performance-sensitive feature.
```

---

## 10. Parameter data

`data/` holds the same numbers in loadable form. Use these as the authoring source of truth; the tables above are the readable copy.

| File | Contents |
|---|---|
| `blackbody.json` | 51-entry LUT, 1000–6000 K: CIE xy, normalised linear Rec.709, luminance in nits at ε = 1 |
| `yref.json` | Yref by lighting condition, source luminances, × Yref multiples, and the bloom-dependent clamp |
| `materials.json` | 20 calibrated materials plus the rev 1 photo swatches |
| `surfaces.json` | 13 surface-response entries and the soil μ constants |
| `weapons.json` | 12 weapons: bore, muzzle pressure, Mach geometry, flash temperatures, blast equivalent, dust disc, impulse; plus device modifiers |
| `munitions.json` | 12 munitions: fireball radius and timing, Sedov shock radii, Gurney velocity, crater, column |
| `guns_recoil.json` | 8 guns: stroke, impulse, deceleration, recoil/counter times, trunnion force, platform pitch; plus small-arms spring constants and camera coupling |
| `tiers.json` | The five-tier matrix, the governor, the non-negotiable list |

For anything not in these tables, derive it in the reference chapter rather than inventing a value:

| Need | Reference chapter |
|---|---|
| A new weapon's flash | `reference/06`, §6.2 and §6.6 |
| A new munition's explosion | `reference/07`, §7.1 and §7.2 |
| A new surface's ejecta | `reference/08`, §8.1 and §8.5 |
| A new gun's recoil | `reference/09`, §9.1 and §9.3 |
| A new material's parameters | `reference/02`, §2.2 |
| A new moss or ground case | `reference/03`, `reference/04` |
| Any brightness or colour question | `reference/05` |
| Any budget question | `reference/10` |

---

**Handoff expectation.** Implement in the real project; export annotated before/after stills and an ordinary gameplay clip; supply a table of measured frame times. Document which tuning values changed and why, and which **[A]** values were replaced by in-engine measurements. The asset bundle is reference and authoring input, subject to §2.1. Use the existing project and in-engine tests for final visual and runtime validation.
