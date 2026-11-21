# Reference layer — combat VFX, surfaces and ground

> **This is the derivation layer under `../BATTLE-FOUNDRY-SPEC.md`.** The spec is the plan of
> record: it states what to build, in what order, to what acceptance, and every magnitude in
> it is ready to type in. These chapters say *why* each magnitude is what it is and how to
> re-derive it for a weapon, munition, surface or material the spec's tables do not cover.
> Read the spec first. Come here when you need a number it does not give.
>
> `99-blackwater-thaw-castle-spec.md` is prior project work, carried unchanged for continuity.

---

## Combat VFX, surfaces and ground — an implementation corpus

Volume 3 of the corpus begun in `ultrarealistic-games` (performance, lighting, materials) and continued in `weather-and-particles` (atmosphere and the particle engine). This volume is narrower and more concrete: it answers a specific set of questions about one project, with measured data from that project's own assets and physical constants for the effects it needs.

It assumes volumes 1 and 2 and cross-references them rather than repeating them. Where this volume says "the particle engine", "six-way lighting", "overdraw control" or "exposure in EV100", the general treatment is in those volumes and the chapter reference is given.

## What this volume is for

Six questions, answered in order:

1. The supplied `fantasy_house_with_interior.glb` looks good inside and out, particularly its texture blending and grunge. **What exactly is it doing, and how is it reproduced?** → chapters 01, 02
2. More moss, on assets and on the ground, from three supplied scans. **Where does moss go, how is it represented, and what do the scans have to become?** → chapter 03
3. The ground is flat. **What are the cues that are missing, and what does each cost?** → chapter 04
4. Explosions, muzzle flashes and artillery need to be realistic. **What colour, how bright, how big, how long?** → chapters 05, 06, 07
5. Dirt must fly up after an explosion. **How much, how fast, in what shape, for how long?** → chapter 08
6. Tank and anti-tank gun recoil must move correctly. **What is the actual motion?** → chapter 09

And one that cuts across all of them: **what does each of the five graphics settings — Very Low, Low, Medium, High, Ultra High — do to each system?** → chapter 10, with every per-system table collected there.

## Chapters

| # | File | What it covers |
|---|---|---|
| 01 | [01-asset-audit-and-what-to-copy.md](01-asset-audit-and-what-to-copy.md) | Parsed contents of all six supplied GLBs; the house's trim-atlas + decal-mesh architecture; the road scene's 26-layer ground; texel densities; unit calibration; the import pipeline; what to copy and what not to |
| 02 | [02-material-layering-grunge-and-decals.md](02-material-layering-grunge-and-decals.md) | The layer stack; calibrated albedo/roughness/F0 table; height blending with variance restoration; geometric masks with constants; decal meshes; per-instance variation; tiling break-up; detail layers; specular AA; sample budgets |
| 03 | [03-moss-lichen-and-biological-wear.md](03-moss-lichen-and-biological-wear.md) | Where moss grows and why; the placement field; film / mat / fuzz representations; shells and clump scatter; production pipelines for the three supplied scans; ground moss; lichen as the inverse of moss; budgets |
| 04 | [04-ground-that-is-not-flat.md](04-ground-that-is-not-flat.md) | The six flatness cues and how to measure them; splat vs mesh layering; height blending; parallax occlusion mapping; displacement and tessellation; RVT; scatter densities; contact; ruts and deformation; puddles; fixing the supplied ground |
| 05 | [05-combustion-light-colour-and-exposure.md](05-combustion-light-colour-and-exposure.md) | Computed blackbody colour and luminance tables; emissivity and optical depth; exposure and why cores clip white; what number to write in the emissive channel; bloom; cooling curves; dynamic lights from temperature |
| 06 | [06-muzzle-flash-and-gun-blast.md](06-muzzle-flash-and-gun-blast.md) | The five flash components with measured temperatures and durations; Mach-disc geometry that places everything; the blast sphere and the ground dust disc; muzzle devices; a per-weapon data table from 5.56 mm to 155 mm; budgets |
| 07 | [07-explosions-artillery-and-fireballs.md](07-explosions-artillery-and-fireballs.md) | Hopkinson–Cranz and Taylor–Sedov scaling; per-munition tables; the phase timeline; surface response; Gurney fragments; shock visuals; smoke columns; six-way lighting; artillery impact angles and fuze types; barrage pacing |
| 08 | [08-dirt-ejecta-debris-and-craters.md](08-dirt-ejecta-debris-and-craters.md) | Particle size deciding simulation; the Housen–Holsapple velocity sampling law; launch geometry and the ejecta curtain; column heights; emitter construction; crater scaling and shape; residue layers; blast interaction |
| 09 | [09-recoil-dynamics-for-guns-tanks-and-carriages.md](09-recoil-dynamics-for-guns-tanks-and-carriages.md) | Why a gun is not a spring; measured recoil strokes; the parabolic displacement curve; the firing sequence; hull pitch and towed-carriage hop; small arms; barrel whip; camera coupling |
| 10 | [10-quality-tiers-very-low-to-ultra-high.md](10-quality-tiers-very-low-to-ultra-high.md) | What each of the five tiers *is*; frame budgets; the master matrix of every system at every tier; the ordered ladder of cuts; the dynamic governor; acceptance checklists; player-facing presets |
| 11 | [11-formula-sheet-and-checklists.md](11-formula-sheet-and-checklists.md) | Every formula in one place (A1–H5) and twelve working checklists (B1–B12) |

Machine-readable tables in [`data/`](data/), holding the same numbers as the chapters in a form that can be loaded directly:

| File | Contents | Chapter |
|---|---|---|
| `blackbody.json` | 51-entry LUT, 1000–6000 K: CIE xy, normalised linear Rec.709, luminance in nits at ε = 1 | 05 |
| `materials.json` | 20 calibrated materials: linear albedo, roughness range, F0, metallic, normal intensity, substrate porosity | 02, 03 |
| `surfaces.json` | 13 surface-response entries for explosions, plus the soil `μ` constants | 07, 08 |
| `weapons.json` | 12 weapons: bore, muzzle pressure, Mach-disc geometry, flash temperatures, blast equivalent, dust disc, impulse; plus muzzle-device modifiers | 06 |
| `munitions.json` | 12 munitions: fireball radius and timing, Sedov shock radii, Gurney fragment velocity, crater, dirt column | 07, 08 |
| `guns_recoil.json` | 8 guns: stroke, impulse, deceleration, recoil and counter-recoil times, trunnion force, platform pitch response; plus small-arms spring constants and camera coupling | 09 |
| `tiers.json` | The full five-tier matrix for every system, the dynamic governor, and the non-negotiable list | 10 |

## Reading paths

- **"Recreate the house"**: 01 → 02 → 03 → 04
- **"The ground looks flat"**: 04 (section 4.1 first) → 02 (2.3) → 01 (1.3)
- **"Add moss"**: 03 → 02 (2.3, 2.4) → 04 (4.7)
- **"Make explosions real"**: 05 → 07 → 08
- **"Make guns real"**: 05 → 06 → 09
- **"It runs too slowly"**: 10 → the per-chapter budget sections → volume 1 chapters 01 and 13
- **"What should Low look like?"**: 10 (sections 10.1 and 10.3)
- **"I need one number"**: 11

## The twelve ideas that recur

1. **Size and duration come from a physical quantity, never from a swatch or a feel.** `R = 3.77 W^⅓`, `x_Mach = 0.67 D √(p_e/p_a)`, `t_r = 2L/V_0`. Every effect in this volume is parameterised by the thing that actually causes it.
2. **Combustion is orange.** 1600–3000 K is linear RGB (1.00, 0.19, 0.00) to (1.00, 0.48, 0.16). White is what the exposure does to it, not what the source is.
3. **Brightness goes as `T^8.1` in that range, not `T⁴`.** Real flashes are violent and over almost immediately. Game flashes that glow for a second are an order of magnitude too slow.
4. **Emissivity tracks optical depth.** That is why a fireball has a clipped white core and an orange rim, and it is the whole look.
5. **Characteristic times scale as `W^⅓`.** Ten times the charge evolves 2.15 times more slowly. Large explosions that evolve quickly look like small ones filmed close.
6. **Particle size decides simulation.** Under 0.2 mm it hangs in the air; over 20 mm it is a mesh with collision. The thousand-fold spread in stopping distance is what makes an explosion look like an explosion.
7. **Sample from the distribution, do not model the shape.** The power-law ejecta velocity law produces the inverted-cone curtain by itself. Hand-modelling the cone does not produce the law.
8. **Flat ground is a parallax problem before it is a texture problem.** Six independent cues, each measurable; the supplied ground fails five of them.
9. **Variation must exist at every scale, and masks are cheap.** Macro noise, meso grunge, base tiling, micro detail. Height-blend everything, and always restore the variance.
10. **Decide per feature whether it varies per instance.** If not, paint it into the atlas for free. If so, it is a decal mesh or a shader mask.
11. **A gun is not a spring.** Constant-force hydraulic brake out, slow gas-spring return, ratio 4–7. And the barrel is already moving when the flash appears.
12. **A tier removes detail, never meaning.** Ten things must survive Very Low; everything else is negotiable in a fixed order.

## Conventions

- **Units are SI.** Distances in metres, mass in kilograms, time in seconds, temperature in kelvin, luminance in cd/m² (nits), illuminance in lux, luminous flux in lumens, intensity in candela, pressure in pascals or bar where stated.
- **Colours are linear Rec.709 / sRGB primaries at D65** unless a hex value is given, which is sRGB-encoded.
- **Charge masses are TNT-equivalent kilograms** unless stated.
- **Code is GLSL ES 3.0-compatible** and uses `saturate(x)` as shorthand for `clamp(x, 0.0, 1.0)`. It is written to port without restructuring to HLSL, Godot shader language, Unreal material graphs and Unity Shader Graph / VFX Graph.
- **Shader snippets were not compiled or run.**
- Every number is marked as one of: *parsed* (read from the supplied files), *measured/published* (from a cited source), *derived* (computed here from cited inputs, with the derivation shown), or *authored/constructed* (a planning value chosen to produce plausible magnitudes).

## Engine mapping

| Concept here | Unreal Engine 5 | Unity | Godot 4 | three.js / WebGPU |
|---|---|---|---|---|
| Tiling-material atlas | Material with UV rect parameters | same | same | custom ShaderMaterial |
| Decal mesh | Static Mesh with a translucent material | Mesh Renderer, transparent queue | MeshInstance3D, transparent | Mesh, `transparent: true`, `depthWrite: false` |
| Projected decal | Decal Actor / Deferred Decal | Decal Projector (HDRP/URP) | Decal node | custom box-projection pass |
| Height-blend splat | Landscape Layer Blend (height blend) | Terrain Layers + custom shader | Terrain3D plugin / custom | custom shader |
| POM | Material Function POM node | custom | custom (`heightmap` + deep parallax) | custom |
| Displacement / tessellation | Nanite displacement | Tessellation (HDRP) | custom | compute-generated geometry |
| RVT ground blend | Runtime Virtual Texture | no direct equivalent; render-to-texture | no direct equivalent | render-to-texture |
| Deformation buffer | Render Target + Canvas draws | Custom RenderTexture | SubViewport | WebGLRenderTarget |
| Shell texturing | instanced mesh, N shells | same | same | InstancedMesh |
| Flipbook + motion vectors | SubUV + Flipbook Motion Vectors node | VFX Graph flipbook motion vectors | custom | custom |
| Six-way lit smoke | custom material | VFX Graph "Six-Way Smoke Lit" | custom | custom |
| GPU particles | Niagara GPU emitter | VFX Graph | GPUParticles3D | transform-feedback / compute |
| Mesh debris | Chaos / PhysX rigid bodies | Rigidbody | RigidBody3D | ammo.js / rapier |
| Spring recoil | Camera Shake (spring) + anim | Cinemachine Impulse | custom | custom |
| Histogram auto exposure | Auto Exposure (histogram) | Exposure (histogram) | CameraAttributesPractical | custom compute |
| Dynamic resolution | Dynamic Resolution + TSR | Dynamic Resolution + FSR/DLSS | scaling_3d_scale | custom + TAAU |

## Provenance

About 44,000 words in 12 files, plus seven machine-readable data files.

- **Corrections applied 2026-11-21 (rev 2).** The first revision mis-stated the house's decal geometry: it took the largest single primitive using the BLEND material (`BOTTLES_low_poly_decal_0`, 2,408 tris) for the whole decal layer. The correct figures are in chapter 01 section 1.2: 11,061 primitives use material 1, of which 11,050 are duplicated vegetation cards (320,500 tris, 59.7 % of the asset) and 11 are authored decals (4,886 tris), the weathering subset being 1,624 tris (0.30 %). Also added in rev 2: embedded licence metadata (section 1.4a — two of the six assets are CC-BY-NC), and the road's three materials that bind their normal map to UV1, which makes the blanket UV-stripping rule unsafe on that file.
- **Chapter 01 is entirely parsed data.** Every triangle count, material parameter, image resolution, UV range, bounding box and texel density was read directly out of the supplied `.glb` files by decoding the glTF JSON chunk and the binary buffer and composing world transforms through the node hierarchy. The texture contents described (atlas band layout, decal atlas contents, the hand-painted ground map) were read from the decoded images. Only the unit-scale factors are estimates, and they are marked.
- **Chapter 05's blackbody tables are computed, not recalled.** Planck's law was integrated against analytic fits to the CIE 1931 colour-matching functions and validated against the published Planckian chromaticities at 5778 K and 6500 K, and against the known luminance of a candle flame.
- **Chapters 06–09 rest on primary sources where primary sources exist**: spectroscopic muzzle-flash temperatures from Klingenberg and from CARS measurements; flash durations from DTIC and Telops reports; recoil strokes and recoiling masses from gun data sheets; ejecta velocity distributions from Housen and Holsapple; crater and impact-angle data from crater-analysis practice; buried-charge ejecta cone angles from centrifuge experiments. Each chapter's source list says which.
- **Derived quantities show their derivation.** `t_peak = 0.0139 W^⅓` is derived by equating the Sedov radius to the empirical fireball radius. The Mach-disc relation is checked against two independently reported flash-disc positions, at 5.56 mm and 120 mm, and agrees with both. The computed M256 trunnion force is sanity-checked against the published CN120-26 figure.
- **Everything else is authored.** Budget tables, millisecond costs, particle counts, tier definitions, decal offsets, mask constants, scatter densities, persistence times, colour ramps and most emissivity values are constructed planning allocations chosen to give plausible magnitudes and a consistent internal scale. They are starting points to be replaced by measurement.
- **The widely used fireball-duration correlation `t = 0.45 M^⅓` was rejected** for this volume: it is fitted to hydrocarbon/BLEVE fireballs from propane releases of 279–1708 kg and over-predicts condensed-HE luminous duration by roughly 4–5×. The authored `t_lum = 0.10 W^⅓` is used instead and is labelled as authored.
- **No code in this volume was compiled or run.** No performance figure in it was measured. Profile on the target hardware before fixing any budget.
- Several well-known references could not be opened directly by the fetcher during the research for this volume; where a technique is described second-hand it says so in the chapter's source list.
