# 08. Dirt ejecta, debris and craters

What an explosion throws up is usually bigger, longer-lived and more characteristic than the fireball, and it is where game explosions most often fail. A generic puff of brown sprites is not a dirt column. This chapter gives the ejecta velocity distribution, the launch geometry, the particle-size physics that decides what is a sprite and what is a mesh, the crater, and the persistent residue.

## 8.1 Particle size decides everything

One table determines how each piece of ejecta must be simulated and drawn. Quartz, ρ = 2650 kg/m³, still air at sea level. Stokes terminal velocity `v_t = ρ_p g D² / (18 μ)` for `D ≲ 0.1 mm`, Newton `v_t = sqrt(4 g ρ_p D / (3 ρ_a C_d))` above. Ballistic stopping length `L = 4 ρ_p D / (3 ρ_a C_d)` is the distance over which air drag removes most of the particle's momentum.

| D | Class | v_terminal (m/s) | L_stop (m) | Fall time from 10 m | Render as |
|---|---|---|---|---|---|
| 1 µm | smoke-size | 8.0 × 10⁻⁵ | 0.01 | > 10⁵ s | suspended cloud, sprite |
| 10 µm | fine dust | 8.0 × 10⁻³ | 0.06 | 1250 s | suspended cloud, sprite |
| 50 µm | dust | 0.20 | 0.31 | 50 s | suspended cloud, sprite |
| 100 µm | fine sand | 0.80 | 0.61 | 13 s | short-lived sprite |
| 500 µm | sand | 5.5 | 3.1 | 2 s | GPU point or streak |
| 2 mm | coarse sand | 11 | 12.3 | 0.9 s | GPU point or streak |
| 10 mm | grit | 24.5 | 61 | 0.4 s | GPU point or streak |
| 30 mm | small clod | 42.5 | 184 | — | mesh debris |
| 100 mm | clod | 78 | 614 | — | mesh debris |
| 300 mm | rock | 134 | 1841 | — | mesh debris |

Three thresholds fall out and they are the architecture of the whole system:

```
D < 0.2 mm     ->  L_stop < 1.5 m. These particles are stopped by air almost
                   immediately and become a SUSPENDED CLOUD. Do not give them
                   ballistic trajectories. Advect them with the blast wind and
                   then with the ambient wind, and let them settle at v_t.

0.2 - 20 mm    ->  BALLISTIC WITH DRAG. GPU particles with a drag term.
                   Visible as individual specks or short streaks.

D > 20 mm      ->  BALLISTIC, drag negligible over the flight. MESH DEBRIS,
                   with collision and a settling/resting state.
```

By mass, soil ejecta is typically 60–85 % in the ballistic band and 5–25 % fine enough to suspend — but the *suspended* fraction dominates the look, because it is what forms the column and the lingering haze. (*Mass-fraction split is an authored estimate; it depends entirely on the soil.*)

## 8.2 Ejecta velocity: the sampling law

Crater ejecta follows a power-law mass–velocity distribution (Housen & Holsapple): the cumulative mass ejected faster than `v` goes as

```
M(> v) ∝ v^(-3μ)
```

with `μ` a soil constant. Invert it to sample a particle's speed from a uniform random `u ∈ (0, 1]`:

```
v = v_min · u^(-1/(3μ))          clamped to v_max
```

| Soil | μ | 3μ | exponent −1/(3μ) | v at u = 0.5 | u = 0.1 | u = 0.01 |
|---|---|---|---|---|---|---|
| Dry sand | 0.41 | 1.23 | −0.813 | 7.0 | 26.0 | 169 |
| Damp soil | 0.45 | 1.35 | −0.741 | 6.7 | 22.0 | 121 |
| Cohesive soil / clay | 0.50 | 1.50 | −0.667 | 6.3 | 18.6 | 86 |
| Competent rock | 0.55 | 1.65 | −0.606 | 6.1 | 16.1 | 65 |

(with `v_min` = 4 m/s)

```
v_min = sqrt(g · R_crater)        the speed that just clears the rim.
                                  R = 1.5 m -> 3.8 m/s.
v_max = 20 · W^0.18 · 2.5         cap for the visible column; beyond this the
                                  particles leave the play area and are wasted.
```

This is the single most important formula in the chapter. A uniform or Gaussian velocity distribution produces a puff. The power law produces the real thing: **a dense, slow, low skirt and a sparse, fast, high column from the same emitter**, because most of the mass is slow and a small tail is very fast.

## 8.3 Launch geometry: the ejecta curtain

Launch position `ξ = x / R_crater ∈ [0, 1]`, measured from the burst point.

```
Elevation from the surface:   θ(ξ) = 52° − 22°·ξ
Speed:                        v(ξ) = v_max · (ξ + 0.08)^(-1/(2μ)) , clamped
Azimuth:                      uniform, unless the impact was oblique (7.9)
```

Measured anchors for those constants: ejection angle is **above 45° near the impact point and 30–40° at 0.6 R**; the ejecta curtain angle runs **52° to 63°**; a buried charge produces an inverted cone with an **included angle of 79.5°** (half-angle 39.8° from vertical, i.e. 50.2° elevation).

Sampling `ξ` so that mass is distributed correctly over the crater floor:

```
xi = sqrt(u2)          u2 ~ U(0,1]     // area-weighted: more particles from the rim
```

Resulting ballistics (no drag):

| ξ | θ (°) | Example v (m/s) | Range (m) | Apex (m) | Flight (s) |
|---|---|---|---|---|---|
| 0.0 | 52.0 | 60 | 356 | 114 | 9.6 |
| 0.2 | 47.6 | 34 | 117 | 32 | 5.1 |
| 0.4 | 43.2 | 20 | 41 | 9.6 | 2.8 |
| 0.6 | 38.8 | 12 | 14.3 | 2.9 | 1.5 |
| 0.8 | 34.4 | 7 | 4.7 | 0.8 | 0.8 |
| 1.0 | 30.0 | 4 | 1.4 | 0.2 | 0.4 |

The characteristic **inverted-cone curtain** emerges automatically from this: fast particles leave steeply from the centre, slow ones leave shallowly from the rim, and the envelope of their trajectories is a cone wall expanding outward. Do not model the cone directly; sample the distribution and the cone appears.

### Column height

With `v_col = 20 · W^0.18` m/s as the speed of the visible column top (calibrated so a 155 mm shell tops out near 25 m):

| Munition | W (kg) | v_col (m/s) | Column apex (m) | t to apex (s) | Column life (s) | Crater D (m) | apex / D |
|---|---|---|---|---|---|---|---|
| M67 grenade | 0.21 | 15 | 7.2 | 1.21 | 2.8 | 0.95 | 7.6 |
| 81 mm mortar | 1.09 | 20 | 13.1 | 1.63 | 3.8 | 1.65 | 7.9 |
| 105 mm HE | 2.30 | 23 | 17.1 | 1.87 | 4.3 | 2.11 | 8.1 |
| 120 mm HE | 3.30 | 25 | 19.5 | 1.99 | 4.6 | 2.38 | 8.2 |
| 155 mm M107 | 6.60 | 28 | **25.0** | 2.26 | **5.2** | 3.00 | 8.3 |
| 155 mm M795 | 12.40 | 31 | 31.3 | 2.53 | 5.8 | 3.70 | 8.5 |
| 250 kg bomb | 128.0 | 48 | 72.6 | 3.85 | 8.8 | 8.06 | 9.0 |

The ratio `apex / crater diameter ≈ 8` across three orders of magnitude of charge is a useful sanity check on any ejecta system.

## 8.4 The timeline of a dirt column

For a 155 mm point-detonating surface burst, dry soil:

| t (ms) | Event | Render |
|---|---|---|
| 0 – 1 | detonation, fireball emerges | — |
| 1 – 10 | ground beneath is compressed and accelerated; the **dome** lifts coherently | a smooth, dark, rising hemisphere, 1–3 m across, with the fireball glowing through cracks in it |
| 10 – 60 | the dome breaks; the fastest ejecta leaves | the first fast streaks appear; radial fracture lines in the lifting soil |
| 20 – 100 | the fine fraction is stripped by drag and forms the dust base | a dense, low, expanding dust skirt, radius 3–10 m |
| 60 – 600 | main curtain | the inverted cone, widest and densest here |
| 200 – 2200 | fast tail reaches apex | sparse individual clods and streaks at 15–25 m |
| 600 – 3000 | clods land | impact puffs, bounces, resting debris |
| 2000 – 5200 | the column collapses back into a drifting cloud | fine dust only |
| 5000 – 30,000 | settling and drift | low haze, wind-advected |

The **dome phase** (10–60 ms) is the detail that sells a delay-fuze or buried burst and is almost never implemented: for 20–60 ms the ground lifts as a coherent, intact surface before breaking. Implement it as a short displacement of the terrain mesh plus a cracking decal, then release the particles.

## 8.5 Emitter construction

```
EJECTA EMITTER (one burst)

Inputs: burstPoint, surfaceNormal, impactVelocity, W, SurfaceResponse (7.4)

A. SUSPENDED DUST  (D < 0.2 mm)
   count         = DUST_N[tier] * surf.dustToFireRatio
   spawn radius  = U(0, 0.6 R_crater) on the surface
   initial v     = radialOutward * (6..18 m/s) + up * (2..8 m/s)
   drag          = heavy: v *= exp(-dt / 0.12)       tau = 0.12 s
   then          = advect with ambient wind; settle at v_t = 0.2 m/s
   lifetime      = 8 - 25 s, alpha fading as (1 - t/T)^1.5
   render        = soft sprites, 6-way lit, size 0.8 - 4 m growing 25 %/s
   colour        = surf.ejectaAlbedo, brightened 1.3x (dust scatters forward)

B. BALLISTIC GRAINS  (0.2 - 20 mm)
   count         = GRAIN_N[tier]
   xi            = sqrt(U(0,1));  theta = 52 - 22*xi deg;  phi per 7.9
   v             = v_min * U(0,1]^(-1/(3mu)) , clamp to v_max
   drag          = dv/dt = -v*|v| / L_stop(D)
   collision     = against the depth buffer (High+) or the terrain heightfield
   on impact     = 35 % chance of a 0.15 m dust puff within 15 m of the camera
   lifetime      = until rest or 6 s
   render        = GPU points; motion-blur streak of length v*dt with
                   alpha scaled by L_ref/(v*dt) to conserve energy

C. MESH DEBRIS  (> 20 mm)
   count         = CLOD_N[tier] * surf.clodFraction
   same launch distribution as B, biased to low xi (clods come from deeper)
   rigid bodies with 1-2 bounces, then switch to static and merge into the
   persistent-debris pool
   render        = 3-6 clod meshes at 20-80 tris, instanced, with LODs

D. GROUND DUST RING (the lateral blast sheet, distinct from the column)
   an annulus expanding to 2.5 R_fb with tau = 0.055 s, 16-40 sprites,
   height 0.3 - 1.2 m. Identical in structure to the muzzle-blast dust disc
   of chapter 06, section 6.3.
```

### Why the three systems must be separate

If you emit everything with one velocity distribution and one drag constant, you get a puff. The three bands have stopping lengths differing by a factor of 1000 (0.06 m to 614 m). It is that spread — the dust hanging where it was born while clods arc 40 m away — that reads as an explosion rather than a particle effect.

## 8.6 Ejecta lighting and colour

```
- Dust is lit, never emissive, EXCEPT within the first ~60 ms where the
  fireball illuminates the inside of the column. Feed the fireball light
  into the dust's 6-way evaluation (chapter 07, section 7.7) and the base of
  the column glows orange while the top stays dark. This is the signature
  look of a night artillery impact.
- Dust scatters strongly forward. Multiply its lit colour by
      1 + 1.4 * pow(saturate(dot(V, -L)), 4.0)
  so dust between the camera and the sun lights up. This is why dust columns
  look bright against the sun and dark with the sun behind you.
- Dust albedo is the dry soil albedo raised: fine particles scatter more than
  the bulk. Use surf.ejectaAlbedo * 1.3, clamped to 0.85.
- Wet soil ejecta is 2-3x darker and does not form a suspended cloud: the
  fines are bound into clods. Set dustToFireRatio low and clodFraction high.
- Snow ejecta is bright (albedo 0.75-0.85), has a very large suspended
  fraction, and settles for 20-60 s.
```

## 8.7 Craters

```
Apparent crater, surface burst, cohesive soil:
    D = 1.6 · W^(1/3)   m          depth = 0.4 · D       lip height = 0.12 · D
    ejecta volume ≈ pi (D/2)^2 · depth / 2
```

| W (kg TNT-eq) | D (m) | Depth (m) | Lip (m) | Ejecta volume (m³) |
|---|---|---|---|---|
| 0.2 | 0.94 | 0.37 | 0.11 | 0.1 |
| 1.0 | 1.60 | 0.64 | 0.19 | 0.6 |
| 2.3 | 2.11 | 0.84 | 0.25 | 1.5 |
| 6.6 | 3.00 | 1.20 | 0.36 | 4.2 |
| 12.4 | 3.70 | 1.48 | 0.44 | 8.0 |
| 25 | 4.68 | 1.87 | 0.56 | 16.1 |
| 128 | 8.06 | 3.23 | 0.97 | 82.4 |

Multiply `D` by `SurfaceResponse.craterDepthScale`: 1.3 soft soil, 1.0 cohesive soil, 0.55 compacted gravel, 0.25 asphalt, 0.10 concrete (spall scoop only), 0.0 rock and water. An airburst gets no crater at all.

Shape: **craters from oblique impacts are elliptical**, elongated downrange, with an entry furrow uprange. At 15–20° (artillery), eccentricity is pronounced; at 70–80° (mortar) it is nearly circular.

```
semi-major / semi-minor ≈ 1 / sin(impactElevation),  clamped to [1, 3.5]
    15 deg -> 3.5 (clamped)
    20 deg -> 2.9
    45 deg -> 1.41
    75 deg -> 1.04
```

### Three ways to render a crater

| Method | Cost | Persistence | Use |
|---|---|---|---|
| **Decal only** | 1 projected decal | full | Very Low, Low; and for all distant/old craters |
| **Decal + displacement into the deformation buffer** (chapter 04, section 4.9) | a write into an existing buffer | until the buffer scrolls away or decays | Medium, High |
| **Mesh crater** | a prefab crater mesh blended into the terrain via RVT, plus terrain collision edit | permanent | High, Ultra High, and always for gameplay-significant craters |

The deformation-buffer route is the best value: craters, ruts and footprints share one R16 target, one displacement path and one collision query.

```
CRATER WRITE INTO THE DEFORMATION BUFFER
    profile(r) = -depth · (1 - (r/R)^2)^1.6          for r < R        (bowl)
               + lip   · exp(-((r - R)/(0.35R))^2)   for r near R     (raised rim)
    disturbance = 1 inside 1.4 R   (clears grass, moss, litter, snow)
    scorch decal radius = 1.8 R, alpha falling as (1 - r/(1.8R))^2
    ejecta blanket decal radius = 3.5 R, alpha ~ (R/r)^3, patchy
```

### Residue layers, in order

| Layer | Radius | Persistence | Notes |
|---|---|---|---|
| Crater geometry | R | permanent | displacement + collision |
| Scorch / soot decal | 1.8 R | long; weathers over minutes in rain | darkest albedo 0.012 |
| Ejecta blanket decal | 3.5 R | long | loose soil over the original ground, patchy, thinning as `r^-3` |
| Scattered clod meshes | 2–8 R | pooled, capped | a merged, static instance set |
| Vegetation kill | 1.4 R | permanent | cleared via the disturbance channel |
| Fragment scars | up to 30 R | medium | small pock decals on nearby walls, **directional**: a cone away from the burst |

Fragment scars on nearby surfaces are cheap and strongly diagnostic. Trace 20–60 rays from the burst point in the fragment distribution of chapter 07 section 7.5, and place a small decal where each hits a surface within 30 m. Players read the pattern as evidence of where the round landed.

## 8.8 Interaction with the rest of the world

```
Blast impulse on physics bodies:
    J = Delta_p(Z) · A_projected · t_positive / 2
    t_positive ≈ 1.8e-3 · W^(1/3) · Z^0.35   s     (authored fit)
    Apply as an impulse through the body's centre plus a torque from the
    offset between the centroid of the exposed face and the centre of mass.

Blast wind on vegetation:
    a radial velocity impulse into the wind field, amplitude
    v_wind = 18 · (W^(1/3) / R)^1.4  m/s, decaying with tau = 0.3 s.
    Grass and foliage bend out and spring back. Cheap and very effective.

Dust lift from surfaces:
    any loose-surface quad within 2.5 R_fb emits a short dust burst; this is
    what produces the dust that appears off rooftops and ledges when a round
    lands nearby, and it is a strong cue that the shock is real.

Camera and audio:
    camera shake amplitude ∝ sqrt(Delta_p(Z)), duration 0.3-1.2 s
    audio delay = distance / 343 m/s
    low-pass the whole mix for 0.4-2 s if within Z < 4 (pressure-wave
    hearing effect); never a flat silence, which reads as a bug
```

## 8.9 Budgets

| Item | Very Low | Low | Medium | High | Ultra High |
|---|---|---|---|---|---|
| Suspended dust sprites per burst | 3 | 8 | 20 | 45 | 90 |
| Ballistic grains per burst | 0 | 40 | 200 | 800 | 2500 |
| Mesh clods per burst | 0 | 0 | 4 | 14 | 30 |
| Ground dust ring sprites | 0 | 6 | 14 | 26 | 40 |
| Grain collision | none | kill plane at ground Y | terrain heightfield | depth buffer | depth buffer + SDF |
| Impact sub-effects | 0 | 0 | 4 | 12 | 24 |
| Dome phase | no | no | no | yes | yes |
| Crater | decal | decal | decal + deformation buffer | + mesh for significant | + mesh, always |
| Scorch + ejecta-blanket decals | 1 | 1 | 2 | 3 | 3 + fragment scars |
| Fragment scar rays | 0 | 0 | 0 | 20 | 60 |
| Persistent clod instances (global cap) | 0 | 0 | 150 | 600 | 1500 |
| Dust lifetime | 3 s | 6 s | 12 s | 20 s | 30 s |
| Dust lighting | unlit, tinted | per-particle | per-vertex | 6-way | 6-way per pixel |
| Forward-scatter term | no | no | yes | yes | yes |
| Blast wind on vegetation | no | yes | yes | yes | yes |
| Estimated cost per burst | 0.03 ms | 0.10 ms | 0.35 ms | 0.9 ms | 1.8 ms |

Grains are GPU particles: at 2500 per burst and 32 concurrent bursts that is 80,000 particles, which is well inside the 100,000 Ultra High stateful-particle cap in volume 2 chapter 7.28 — but the *fill* is what costs, not the count, and grains are sub-pixel at range. Use the coverage-preserving alpha of volume 2 chapter 7.25 so they thin out instead of aliasing.

## 8.10 Common errors

| Error | Correction |
|---|---|
| All ejecta at the same speed | `v = v_min · u^(−1/3μ)`. Most mass is slow; a thin tail is very fast. |
| Dust given ballistic arcs | Below 0.2 mm, `L_stop < 1.5 m`. Dust stops where it is born and hangs. |
| Clods treated as sprites | Above 20 mm they are meshes with collision and a resting state. |
| One emitter for everything | Three bands with stopping lengths differing by 1000×. |
| Symmetric cone for artillery | 15–20° impact: elliptical crater, downrange-biased lobes. |
| No dome phase | 20–60 ms of coherent ground lift sells a delay-fuze burst. |
| Dust unlit or emissive | Lit, 6-way, forward-scattering, and lit from inside during the first 60 ms. |
| Column height unrelated to charge | apex ≈ 8 × crater diameter across three orders of magnitude. |
| Column gone in 1 s | 5.2 s for 155 mm, then 25 s of settling haze. |
| No crater, no scorch, no scars | The residue is the memory of the event. |
| Debris never rests | Bounce twice, then merge into a static pooled instance set. |
| Wet ground throws dust | Wet soil throws dark clods and almost no suspended dust. |

## Rules of thumb

- Size decides simulation. Under 0.2 mm it is a cloud; over 20 mm it is a mesh; in between it is a GPU point.
- Sample speed from the power law, not from a range. That one change turns a puff into a column.
- Elevation falls from 52° at the centre to 30° at the rim, and the inverted-cone curtain appears on its own.
- Column apex ≈ 8 × crater diameter.
- Crater diameter = 1.6 W^⅓, depth 0.4 D, lip 0.12 D, scaled by the surface.
- Light the column from inside while the fireball burns.
- Dust scatters forward: bright against the sun, dark with it behind you.
- The crater, the scorch, the ejecta blanket and the fragment scars are what the player finds afterwards. Budget for them.

## Sources

- Ejecta mass–velocity power law `M(>v) ∝ v^(−3μ)` and its insensitivity to impact angle: Housen and Holsapple, "Ejecta from impact craters", *Icarus* 211 (2011) (https://data.boulder.swri.edu/ksinger/Papers/Housen&Holsapple_2011_Ejecta_from_Impact_craters_Icarus.pdf). The inverse-transform sampling form `v = v_min u^(−1/3μ)` is derived here from that law.
- Ejection angle above 45° near the impact point falling to 30–40° at 0.6 R, and ejecta curtain angle increasing from 52° to 63° with projectile density: "Ejecta velocity distribution of impact craters formed on quartz sand", *Icarus* (ScienceDirect S0019103515003875).
- First material ejected at ≈ 45°, increasing thereafter: Stickle et al., "Influence of the projectile geometry on the momentum transfer from a kinetic impactor" (https://arxiv.org/pdf/2201.01730).
- Buried-charge soil dome and an inverted-cone ejecta flow with an included angle of 79.5° at 1 g, with the flow angle approaching a horizontal asymptote at elevated gravity: "Centrifuge Characterization of Buried, Explosive-Induced Soil Ejecta Kinematics and Crater Morphology" (https://link.springer.com/article/10.1007/s40870-016-0067-1).
- Ballistic range `d = v² sin 2θ / g` and the ballistic nature of individual ejecta particles: LPI Impact Cratering Lab (https://www.lpi.usra.edu/exploration/education/hsResearch/crateringLab/lab/part2/background/).
- Oblique impacts spraying ejecta preferentially downrange; elliptical craters from low-angle strikes; artillery 15–20° and mortar 70–80°: LPI (as above) and ARES, "An Introduction to Crater Analysis" (https://armamentresearch.com/an-introduction-to-crater-analysis/).
- Stokes and Newton terminal-velocity relations and the drag stopping length are standard fluid mechanics, evaluated here for quartz in sea-level air.
- Crater scaling `D = 1.6 W^(1/3)`, `v_col = 20 W^0.18`, `t_positive`, blast-wind amplitude, mass-fraction splits, residue radii, persistence times and all cost figures are constructed planning values calibrated to produce plausible magnitudes (a 155 mm shell: 3.0 m crater, 25 m column, 5.2 s). They are not measurements. Measure or compare against reference footage before fixing them.
- Overdraw, sorting, sub-pixel coverage and particle-count context: volume 2, chapters 7.19, 7.20, 7.25 and 7.28.
