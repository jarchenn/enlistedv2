# 10. The five quality tiers: Very Low to Ultra High

One table per system is not a scalability plan. A scalability plan is a statement of what each tier *is*, a frame budget that each tier must fit, an ordered ladder of cuts, and a rule for what may never be cut. This chapter is the master reference; every per-system table in chapters 02–09 is a row of it.

## 10.1 What each tier is

| Tier | Target | Internal resolution | Output | Promise |
|---|---|---|---|---|
| **Very Low** | integrated graphics, old laptops, handhelds | 50–58 % of output per axis | 720p–1080p | It runs, and the world is the *same world*: same colours, same silhouettes, same gameplay-relevant information. |
| **Low** | entry discrete GPU, last-gen consoles | 58–67 % | 1080p | Everything is present in a cheap form. Nothing is missing, only simpler. |
| **Medium** | mainstream GPU, current consoles in performance mode | 67–77 % | 1080p–1440p | The intended look. This is the tier the art is authored against. |
| **High** | upper-mainstream, consoles in quality mode | 77–100 % | 1440p | Adds the terms that reward a close look: parallax, self-shadowing, real relief, fragment detail. |
| **Ultra High** | enthusiast | 100 % | 1440p–4K | Adds the terms that only matter when you stop and stare. Explicitly beyond the point of good value. |

Internal resolution driven through a good temporal upscaler is the master quality knob (volume 1, chapter 13). The tiers below assume that is already in place, and describe what changes *on top of it*.

### The non-negotiable list

A tier may make a thing cheap. A tier may not make a thing *absent* if its absence changes what the player sees as true about the world. At Very Low, every one of these must still exist in some form:

```
1   The exposure and tone-mapping curve.               Same image brightness and contrast.
2   Grounding: contact shadow or contact AO under
    every object that touches the ground.              Otherwise everything floats.
3   Distance haze / aerial perspective.                Otherwise there is no sense of scale.
4   A visible muzzle flash and a flash light.          Gameplay information.
5   An explosion flash, a dust puff, and a decal.      Gameplay information + world memory.
6   The crater and scorch decal.                       World memory; must match across tiers.
7   The recoil of the gun itself.                      Gameplay feedback.
8   Material identity: albedo and roughness class.     A wooden wall must read as wood.
9   Collision and deformation that gameplay reads.     Physics must be tier-independent.
10  Audio delay over distance.                         Free, and a major realism carrier.
```

Conversely, these may be cut entirely at Very Low because their absence changes only fidelity, not meaning: decal meshes, moss geometry, POM, displacement, fragments, mesh debris, shock refraction, six-way lighting, barrel whip, bore-evacuator puffs, volumetric injection.

## 10.2 Frame budget

Constructed allocations for a 60 fps target (16.67 ms). These are planning budgets to be replaced by measurements on the target hardware.

| Pass | Very Low | Low | Medium | High | Ultra High |
|---|---|---|---|---|---|
| Depth prepass + culling | 0.4 | 0.5 | 0.7 | 0.9 | 1.1 |
| Opaque geometry + materials | 2.4 | 3.0 | 3.8 | 4.6 | 5.4 |
| — of which ground (ch 04) | 0.25 | 0.5 | 1.1 | 2.2 | 3.6 |
| — of which moss (ch 03) | 0.00 | 0.08 | 0.20 | 0.55 | 0.90 |
| Shadows | 1.0 | 1.6 | 2.4 | 3.2 | 4.0 |
| Lighting + GI | 1.6 | 2.4 | 3.2 | 4.2 | 5.2 |
| Atmosphere + volumetrics | 0.3 | 0.6 | 1.0 | 1.6 | 2.4 |
| **Combat VFX (ch 06–08)** | **0.5** | **1.0** | **2.0** | **3.6** | **5.4** |
| Other particles and weather | 0.2 | 0.4 | 0.6 | 1.0 | 1.5 |
| Post + upscale + UI | 1.1 | 1.4 | 1.8 | 2.2 | 2.8 |
| **GPU total (ms)** | **7.5** | **10.9** | **15.5** | **21.3** | **27.8** |
| Implied frame rate at that cost | 133 | 92 | 65 | 47 | 36 |
| Achieved via internal resolution | 0.54× | 0.62× | 0.72× | 0.85× | 1.00× |
| **Effective ms after upscale savings** | 7.5 | 10.9 | 15.5 | 16.3 | 16.6 |

The combat-VFX line is the one this corpus spends. Its internal split:

| Combat VFX sub-budget | Very Low | Low | Medium | High | Ultra High |
|---|---|---|---|---|---|
| Muzzle flashes (all concurrent) | 0.08 | 0.20 | 0.45 | 0.90 | 1.40 |
| Explosions (all concurrent) | 0.10 | 0.30 | 0.70 | 1.40 | 2.00 |
| Ejecta, debris, dust | 0.12 | 0.30 | 0.60 | 1.00 | 1.60 |
| Decals (scorch, scars, craters) | 0.10 | 0.10 | 0.15 | 0.20 | 0.30 |
| Shock refraction | 0 | 0 | 0 | 0.10 | 0.10 |
| **Total** | **0.40** | **0.90** | **1.90** | **3.60** | **5.40** |

Translucent fill is the binding constraint, not particle count. Hold the soft-sprite fill caps from volume 2, chapter 7.28 (4 / 6 / 8 / 8 screens before the low-resolution buffer's reduction) and the combat systems have to share that budget with weather and ambient effects — they do not get their own.

## 10.3 The master matrix

### Surfaces and materials (chapter 02)

| | Very Low | Low | Medium | High | Ultra High |
|---|---|---|---|---|---|
| Texture taps per opaque pixel | 1 | 4 | 6 | 10 | 13 |
| Base colour | 1 tap | 1 tap | 1 tap | 3-tap hex tiling | 3-tap hex tiling |
| Normal map | no | yes | yes | yes | yes |
| ORM | baked into albedo | yes | yes | yes | yes |
| Detail normal + roughness | no | no | packed, 1 tap | 2 taps | 2 taps |
| Macro world-space noise | no | yes | yes | yes | yes |
| Second blended layer | no | no | no | yes | yes |
| Per-instance hue/value variation | yes (free) | yes | yes | yes | yes |
| Specular anti-aliasing | yes | yes | yes | yes | yes |
| Wetness response | global tint only | albedo + roughness | + puddles | + meniscus + drying | + flow |
| Decal-mesh overdraw (screens) | 0 | 0.15 | 0.35 | 0.70 | 1.20 |
| Decal atlas | — | 1024² | 2048² | 4096² | 4096² + normal |
| Texture budget (VRAM) | 0.6 GB | 1.2 GB | 2.5 GB | 5 GB | 9 GB |

### Moss and biology (chapter 03)

| | Very Low | Low | Medium | High | Ultra High |
|---|---|---|---|---|---|
| Representation | film (albedo + roughness) | film + mat to 8 m | mat everywhere | mat + 8 shells to 3 m | mat + clump meshes to 4.5 m |
| Shell count | 0 | 0 | 0 | 8 | 16 |
| Clump scatter at M = 1 | 0 | 0 | 0 | 400 /m² | 1200 /m² |
| Transmission back lobe | off | off | on | on | on, per-pixel thickness |
| Placement field | per-vertex, baked | per-vertex, baked | per-pixel | per-pixel | per-pixel + cavity |
| Lichen layer | no | no | yes | yes | yes |
| Cost | 0.00 ms | 0.08 ms | 0.20 ms | 0.55 ms | 0.90 ms |

### Ground (chapter 04)

| | Very Low | Low | Medium | High | Ultra High |
|---|---|---|---|---|---|
| Splat layers | 1 | 2 | 4 | 4 (+2 near) | 6 |
| Tiling scales per layer | 1 | 1 | 2 | 2 | 2 + hex |
| POM | off | offset (1 tap) | 6–16 steps | 10–28 + self-shadow | 14–40 + self-shadow |
| POM depth | 0 | 0.015 m | 0.030 m | 0.050 m | 0.065 m |
| Displacement | none | none | baked into the mesh | tessellated to 45 m | tessellated to 90 m |
| Peak displaced tris/m² | 0 | 0 | 15 | 400 | 1200 |
| Mesh ground layers | 1 | 3 | 5 | 7 | 8 |
| RVT | off | off | colour | colour + normal | colour + normal + height |
| Deformation buffer | off | off | 8 cm / 48 m | 4 cm / 64 m | 4 cm / 96 m |
| Scatter draw distance | 8 m | 14 m | 25 m | 40 m | 60 m |
| Grass density | 0 | 6 /m² | 18 /m² | 40 /m² | 60 /m² |
| Cost | 0.25 ms | 0.5 ms | 1.1 ms | 2.2 ms | 3.6 ms |

### Muzzle flash (chapter 06)

| | Very Low | Low | Medium | High | Ultra High |
|---|---|---|---|---|---|
| Core | 1 billboard | 1 billboard | swept mesh 60 tris | swept mesh 120 | swept mesh 200 |
| Intermediate disc | no | no | yes | yes | yes |
| Secondary flash sprites | 1 | 1 | 2 | 3 | 4 |
| Flipbook | single frame | 4×4 cross-fade | 6×6 + motion vectors | 8×8 + MV | 8×8 + MV + 6-way |
| Ground dust disc | no | large calibre, 8 | 16 | 28 | 40 |
| Ember particles | 0 | 0 | 8 | 20 | 40 |
| Smoke sprites | 0 | 1 | 3 | 8 | 16 |
| Dynamic light | 1, 3-frame | 1 | 1 | 1 + shadow for own weapon | 2, shadowed |
| Volumetric injection | no | no | no | yes | yes |
| Blast refraction | no | no | no | yes | yes |
| Bore-evacuator puff | no | no | yes | yes | yes |
| Max concurrent flashes | 4 | 8 | 16 | 32 | 64 |
| Cost per flash | 0.02 ms | 0.05 ms | 0.12 ms | 0.30 ms | 0.55 ms |

### Explosions (chapter 07)

| | Very Low | Low | Medium | High | Ultra High |
|---|---|---|---|---|---|
| Flash sprites | 1 | 1 | 2 | 3 | 3 + emissive mesh |
| Fireball sprites | 1 | 2 | 4 | 6 | 8 |
| Flipbook | 4×4 | 6×6 cross-fade | 6×6 + MV | 8×8 + MV | 8×8 + MV + 6-way |
| Fireball lighting | emissive only | unlit | per-vertex | 6-way, 2 lights | 6-way, per pixel |
| Smoke sprites per burst | 2 | 4 | 8 | 20 | 40 |
| Smoke lifetime | 2 s | 4 s | 8 s | 15 s | 25 s |
| Global smoke cap | 40 | 120 | 400 | 1200 | 3000 |
| Fragments drawn | 0 | 0 | 40 | 120 | 300 |
| Fragment impact sub-effects | 0 | 0 | 4 | 12 | 24 |
| Shock refraction | no | no | no | yes | yes |
| Lights per burst | 1, 4 frames | 1 | 1 | 2 | 2 + volumetric |
| Concurrent full-quality bursts | 2 | 4 | 8 | 16 | 32 |
| Surface-response entries | 3 | 5 | 9 | 12 | 12 + wetness blend |
| Cost per burst | 0.05 ms | 0.15 ms | 0.45 ms | 1.1 ms | 2.2 ms |

### Ejecta and craters (chapter 08)

| | Very Low | Low | Medium | High | Ultra High |
|---|---|---|---|---|---|
| Suspended dust sprites | 3 | 8 | 20 | 45 | 90 |
| Ballistic grains | 0 | 40 | 200 | 800 | 2500 |
| Mesh clods | 0 | 0 | 4 | 14 | 30 |
| Ground dust ring sprites | 0 | 6 | 14 | 26 | 40 |
| Grain collision | none | ground plane | heightfield | depth buffer | depth buffer + SDF |
| Dome phase | no | no | no | yes | yes |
| Crater | decal | decal | + deformation buffer | + mesh when significant | + mesh always |
| Fragment scar rays | 0 | 0 | 0 | 20 | 60 |
| Persistent clod cap | 0 | 0 | 150 | 600 | 1500 |
| Dust lifetime | 3 s | 6 s | 12 s | 20 s | 30 s |
| Dust lighting | tinted | per-particle | per-vertex | 6-way | 6-way per pixel |
| Forward scatter | no | no | yes | yes | yes |
| Cost per burst | 0.03 ms | 0.10 ms | 0.35 ms | 0.9 ms | 1.8 ms |

### Recoil (chapter 09)

| | Very Low | Low | Medium | High | Ultra High |
|---|---|---|---|---|---|
| Gun recoil model | 2-key animation | parabolic formula | parabolic + buffer | + barrel whip | + mantlet, thermal sag |
| Pre-exit 38 mm travel | no | no | yes | yes | yes |
| Hull response | none | scripted pitch | 2-DOF spring | rigid body + suspension | + per-wheel contact |
| Dust shed from the vehicle | no | no | 6 sprites | 16 | 30 |
| Case ejection | no | visual only | physics, 1 bounce | physics, pooled | + heat shimmer |
| Small-arms DOF | 1 | 2 | 4 | 6 | 6 + bolt-carrier impulse |
| Camera coupling | fixed | fixed | stance-based | + low-pass | + crew-body lag |

## 10.4 The ladder of cuts

When a scene misses the budget, cut in this order. Each step is chosen to remove the most milliseconds for the least perceptual loss.

```
 1  Internal resolution -5 %                       (always first; the master knob)
 2  Ultra-only terms: hex tiling, 16-shell moss, 2500 grains, 40 smoke sprites
 3  Shadow-casting combustion lights -> unshadowed
 4  Volumetric injection of effect lights
 5  Shock refraction pass
 6  Fragment count 300 -> 120 -> 40 -> 0
 7  Smoke sprite count and lifetime, globally (biggest VFX win per step)
 8  Soft-particle buffer 1/2 -> 1/4 resolution per axis
 9  POM steps, then POM depth, then POM off
10  Displacement radius, then displacement off
11  Second blended material layer
12  Moss shells -> mat only
13  Decal-mesh overdraw budget
14  Mesh ground layers 7 -> 5 -> 3
15  Detail normal/roughness layer
16  Scatter draw distance and density
17  Deformation buffer resolution, then off
18  Dust lifetime
19  Concurrency caps on flashes and bursts
20  Texture resolution (memory only; buys nothing in frame time until you
    are streaming-bound - do this late, and it is a different budget)
```

Never on this ladder, at any tier: contact AO, exposure, aerial perspective, material identity, decals that record what happened, collision, audio delay.

## 10.5 Dynamic scalability

A fixed tier cannot survive a twelve-round barrage. Add a per-frame governor on top of the tier.

```
GOVERNOR

measure:  GPU frame time, exponentially smoothed over 8 frames
target:   16.0 ms (leave 0.67 ms of headroom against a 16.67 ms budget)

if smoothed > target * 1.08 for 4 consecutive frames:
      step DOWN one rung of a short dynamic ladder
if smoothed < target * 0.88 for 30 consecutive frames:
      step UP one rung

DYNAMIC LADDER (a subset of 10.4; only reversible, non-popping items):
   r0  internal resolution 1.00x   smoke cap 100%   grains 100%   lights 100%
   r1  internal resolution 0.93x   smoke cap  80%   grains  70%   lights 100%
   r2  internal resolution 0.86x   smoke cap  60%   grains  45%   lights  70%
   r3  internal resolution 0.80x   smoke cap  45%   grains  25%   lights  50%
   r4  internal resolution 0.72x   smoke cap  30%   grains  10%   lights  35%
   r5  internal resolution 0.64x   smoke cap  20%   grains   0%   lights  25%

Rules:
 - Only ever change internal resolution by <= 7 % per step, and never more
   than once every 8 frames; larger jumps are visible as a shimmer.
 - Never change a rung during a cutscene or an aim-down-sights transition.
 - Caps apply to NEW spawns only. Never despawn a live effect to save time:
   an explosion that vanishes mid-flight is worse than a dropped frame.
 - Retire oldest-first when a cap is lowered, and fade over 0.3 s.
```

Priority for spawning under a cap — sort candidates and keep the top N:

```
priority = (screenArea / (1 + distance²)) · importance · recency
    importance:  player's own weapon      4.0
                 incoming/nearby hostile  2.5
                 gameplay-relevant        2.0
                 ambient/background       1.0
```

## 10.6 Per-tier acceptance checklist

Run these before shipping a tier. Each is a yes/no with a measurable condition.

| # | Check | VL | L | M | H | UH |
|---|---|---|---|---|---|---|
| 1 | Average and 99th-percentile frame time within budget on the target GPU | ✓ | ✓ | ✓ | ✓ | ✓ |
| 2 | Same scene, every tier: average image luminance within ±5 % | ✓ | ✓ | ✓ | ✓ | ✓ |
| 3 | Same scene, every tier: material hue within ΔE 3 of Medium | ✓ | ✓ | ✓ | ✓ | ✓ |
| 4 | Nothing floats: contact AO or shadow present under all resting objects | ✓ | ✓ | ✓ | ✓ | ✓ |
| 5 | Crater positions and sizes identical across tiers | ✓ | ✓ | ✓ | ✓ | ✓ |
| 6 | Collision geometry identical across tiers | ✓ | ✓ | ✓ | ✓ | ✓ |
| 7 | Muzzle flash visible and correctly timed at 30, 60 and 144 fps | ✓ | ✓ | ✓ | ✓ | ✓ |
| 8 | No effect's brightness varies with frame rate | ✓ | ✓ | ✓ | ✓ | ✓ |
| 9 | Auto exposure does not dip after own-weapon fire | ✓ | ✓ | ✓ | ✓ | ✓ |
| 10 | 12-round barrage stays within budget via the governor | ✓ | ✓ | ✓ | ✓ | ✓ |
| 11 | Ground shows parallax when the camera translates 0.3 m | — | — | ✓ | ✓ | ✓ |
| 12 | Ground silhouette relief ≥ 0.15 m over 4 m | — | — | ✓ | ✓ | ✓ |
| 13 | Moss has a real silhouette within 3 m | — | — | — | ✓ | ✓ |
| 14 | Smoke responds correctly when the sun moves behind it | — | — | — | ✓ | ✓ |
| 15 | VRAM within the tier's budget with 2 GB of headroom | ✓ | ✓ | ✓ | ✓ | ✓ |

## 10.7 Mapping to presets the player sees

Offer the five tiers as presets and expose these as individual overrides, because they are the ones players most often want to move independently:

```
Resolution scale            slider, 50-100 %
Texture quality             VL / L / M / H / UH   (VRAM, not frame time)
Shadow quality              VL / L / M / H / UH
Effects quality             VL / L / M / H / UH   (chapters 06-08)
Terrain detail              VL / L / M / H / UH   (chapter 04)
Foliage and scatter density VL / L / M / H / UH   (chapters 03, 04)
Post-processing             on / off per term
Motion blur                 off / camera / per-object
Dynamic resolution          off / on, with a target frame rate
```

Separating **Effects quality** from **Terrain detail** matters for this project specifically: the ground stack (chapter 04) is the largest fixed per-frame cost, and the combat VFX (chapters 06–08) is the largest variable one. A player on marginal hardware who cares about combat should be able to lower terrain and keep effects, or the reverse.

## Rules of thumb

- Internal resolution first, always. Everything else is a refinement on that.
- A tier removes detail, never meaning. The world must be the same world at Very Low.
- The ten non-negotiables cost almost nothing and carry most of the realism.
- Budget translucent **fill**, not particle count. Combat VFX shares the fill budget with weather.
- Caps throttle spawns; they never kill live effects.
- Keep collision, craters and gameplay-relevant geometry identical across all five tiers.
- A fixed tier cannot survive a barrage. The governor is not optional.
- Check every effect at 30, 60 and 144 fps before believing it.

## Sources

- The principle that internal resolution with a good temporal upscaler is the master quality knob, that temporal stability beats sharpness, and that each preset must keep a cheap form of every term that defines the look: volume 1, chapters 10 and 13.
- Translucent fill caps, soft-particle buffer resolutions, particle-count caps and the per-tier particle cost model: volume 2, chapter 7.28.
- Per-system rows are the tables from chapters 02–09 of this volume, collected.
- The frame-budget allocation, the dynamic-governor ladder and the VRAM figures are constructed planning allocations, not measurements. Replace them with profiled numbers from the target hardware before locking any preset.
