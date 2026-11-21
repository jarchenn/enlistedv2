# 07. Explosions, artillery and fireballs

An HE detonation is a short, violent sequence with well-defined timing, and almost all of it happens in the first 250 ms. Game explosions are usually too slow, too yellow, too long-lived, too symmetric, and have no relationship between their size and their charge. This chapter gives the scaling laws, the phase timeline, the per-munition tables and the rendering construction. Ground interaction — ejecta, craters and debris — is chapter 08.

## 7.1 The scaling laws

Everything scales with the cube root of the charge mass (Hopkinson–Cranz):

```
Z      = R / W^(1/3)                     scaled distance, m/kg^(1/3)
R_fb   = 3.77 · W^(1/3)                  maximum fireball radius, m
t_peak = 0.0139 · W^(1/3)                time to reach R_fb, s
t_lum  = 0.10 · W^(1/3)                  visible luminous duration, s
R(t)   = 1.03 · (E t² / ρ)^(1/5)         Taylor-Sedov shock radius (strong-shock only)
         E = 4.184e6 · W  joules,  ρ = 1.225 kg/m³
```

`t_peak` is derived here by setting the Sedov radius equal to `R_fb`. The three constants 3.77, 0.0139 and 0.10 are all you need to size any explosion from a 40 mm grenade to a bomb.

**Characteristic times scale as `W^(1/3)`, so a fireball ten times larger evolves about 2.15 times more slowly.** Large explosions that evolve quickly look like small ones filmed close — the most common scale error in games.

Maximum temperature in a conventional high-explosive detonation is **about 5000 °C (≈ 5270 K)**, against several tens of millions for a fission weapon. The visible fireball is well below that almost immediately; see the cooling curve in 7.3.

## 7.2 Per-munition table

| Munition | Filler | W (kg TNT-eq) | R_fb (m) | t_peak (ms) | t_lum (ms) | Shock R @1 ms | @5 ms | @20 ms | @100 ms |
|---|---|---|---|---|---|---|---|---|---|
| 40 mm M433 | Comp A5 0.032 | 0.04 | 1.2 | 4.5 | 33 | 0.67 | 1.28 | 2.23 | 4.2 |
| Hand grenade M67 | Comp B 0.18 | 0.21 | 2.2 | 8.3 | 59 | 0.96 | 1.83 | 3.19 | 6.1 |
| 60 mm mortar M49 | TNT 0.18 | 0.18 | 2.1 | 7.8 | 56 | 0.93 | 1.78 | 3.10 | 5.9 |
| RPG-7 PG-7V | 0.45 | 0.52 | 3.0 | 11.2 | 80 | 1.16 | 2.20 | 3.83 | 7.3 |
| 81 mm mortar M374 | Comp B 0.95 | 1.09 | 3.9 | 14.3 | 103 | 1.34 | 2.55 | 4.44 | 8.5 |
| 105 mm M1 HE | TNT 2.3 | 2.30 | 5.0 | 18.3 | 132 | 1.56 | 2.96 | 5.16 | 9.8 |
| 120 mm mortar | Comp B 2.7 | 3.10 | 5.5 | 20.3 | 146 | 1.65 | 3.14 | 5.47 | 10.4 |
| 120 mm tank HE | Comp B 2.9 | 3.30 | 5.6 | 20.7 | 149 | 1.67 | 3.18 | 5.54 | 10.5 |
| **155 mm M107 HE** | TNT 6.6 | 6.60 | **7.1** | **26.1** | **188** | 1.92 | 3.66 | 6.37 | 12.1 |
| 155 mm M795 HE | Comp B 10.8 | 12.40 | 8.7 | 32.2 | 231 | 2.18 | 4.15 | 7.22 | 13.7 |
| ATGM warhead | 6.0 | 6.90 | 7.2 | 26.5 | 190 | 1.94 | 3.69 | 6.42 | 12.2 |
| 250 kg GP bomb | Tritonal 110 | 128.0 | 19.0 | 70.1 | 504 | 3.48 | 6.62 | 11.52 | 21.9 |

Shock radii beyond about 20 ms are outside the strong-shock regime where Sedov is exact; treat the 100 ms column as an upper bound.

Overpressure, from Brode's empirical fit, for sizing damage and the gameplay radius:

| Z (m/kg^⅓) | Δp (kPa) | Effect |
|---|---|---|
| 0.3 | 24,800 | inside the fireball; total destruction |
| 0.5 | 5,360 | reinforced concrete fails |
| 1.0 | 826 | brick wall collapse |
| 2.0 | 156 | lethal to personnel in the open |
| 3.0 | 68 | serious injury, eardrum rupture likely |
| 5.0 | 28 | knocked down, light injury |
| 8.0 | 14 | windows shatter |
| 15.0 | 5 | windows crack |
| 30.0 | 2 | audible, no damage |

For a 155 mm shell (W = 6.6, W^⅓ = 1.876): the lethal-in-the-open radius `Z = 2` is 3.8 m, windows shatter at 15 m, windows crack at 28 m. The *fragment* lethal radius is far larger — see 7.5.

## 7.3 Phase timeline

Times given for a 155 mm shell (W = 6.6 kg, W^⅓ = 1.876); multiply every time by `W^(1/3)/1.876` for another charge.

| Phase | t (ms) | Physical | Visual | Render element |
|---|---|---|---|---|
| **Detonation** | 0 – 0.05 | detonation front at 6900 m/s crosses the filler; 25 GPa, > 4000 K | nothing visible yet at 1 frame = 16.7 ms | — |
| **Casing break-up** | 0.05 – 0.3 | casing dilates ~1.5× and fractures; fragments at Gurney velocity | — | fragment emitters armed |
| **Fireball emergence** | 0.3 – 3 | detonation products break out; shock separates from the product cloud | a brilliant, nearly point-like white core | emissive sphere, ε → 0.95, T 3200 K |
| **Expansion** | 3 – 26 | fireball grows to 7.1 m; secondary combustion of fuel-rich products with air | the main flash; core clipped white, rim orange | fireball flipbook + emissive mesh + light |
| **Shock separation** | 5 – 40 | shock front outruns the luminous ball | a fast, faint refraction ring; a visible condensation disc in humid air | refraction/distortion pass |
| **Cooling** | 26 – 190 | `L_v ∝ T^8.1`: brightness collapses | the flash dies; black smoke appears from the outside in | fireball → smoke cross-fade |
| **Ejecta launch** | 1 – 60 | soil accelerated; the dome lifts | the dirt column starts | chapter 08 |
| **Dirt column** | 60 – 1500 | ballistic ejecta curtain | the inverted cone of soil | chapter 08 |
| **Smoke column** | 190 – 8000 | buoyant plume rises | black-to-grey column, 12–30 m | lit smoke sprites |
| **Dust settle** | 1500 – 25,000 | fine particles fall, wind-advected | the lingering haze | drifting low sprites |
| **Residue** | permanent | crater, scorch, scattered ejecta | — | decals + mesh (chapter 08) |

A 155 mm burst therefore has: **a 26 ms flash, a 1.5 s dirt column, an 8 s smoke column and 25 s of settling dust.** Nearly every game inverts these proportions — a long flash and a short dust phase.

### The cooling curve

From chapter 05: `T(t) = T_amb + (T_0 − T_amb)(1 + t/τ)^(−n)` with `T_0 = 3200 K`, `τ = 0.020 s`, `n = 0.85` for artillery HE, and `L_v ∝ T^8.1`:

| t (ms) | T (K) | Relative brightness | Linear emissive (EV15 reference) |
|---|---|---|---|
| 1 | 3160 | 1.00 | (265, 130, 43) |
| 5 | 3030 | 0.71 | (190, 92, 30) |
| 20 | 2650 | 0.26 | (62, 26, 6.4) |
| 50 | 2260 | 0.064 | (13, 4.6, 0.6) |
| 100 | 1950 | 0.019 | (2.6, 0.68, 0.02) |
| 200 | 1660 | 0.0048 | (0.40, 0.078, 0.00) |
| 400 | 1420 | 0.0010 | (0.047, 0.0066, 0.00) |

By 200 ms the fireball is 0.5 % of its peak brightness. It is already smoke.

## 7.4 Surface dependence

The same shell produces a completely different effect on different ground, and this is where most games apply one generic explosion everywhere.

| Surface | Fireball | Dust / ejecta | Crater | Smoke | Secondary |
|---|---|---|---|---|---|
| **Dry soil / sand** | partially obscured by dust almost immediately | **dominant**; huge light-coloured column | yes, 3.0 m for 155 mm | brown-grey, heavy | lingering haze |
| **Wet soil / mud** | more visible | dark, wet clods, lower and slower | yes, deeper, less lip | dark brown, less | mud splatter decals |
| **Grass / turf** | visible | moderate; sods, roots, green fragments | shallow | brown + green | scorched ring |
| **Concrete / asphalt** | **dominant**, very visible | pale grey dust, concrete spall, sharp fragments | shallow scoop + spall ring | pale grey, light | ricocheting fragments |
| **Rock** | dominant | sharp splinters, very fast | none, a scar | pale grey | sparks |
| **Snow** | visible, then obscured | white, slow-settling, very large plume | deep | white, long-lived | re-freeze crust |
| **Water, shallow** | **suppressed** | white water column | none | steam | ring waves |
| **Water, deep** | **almost none** | column + plume + collapse | none | steam only | surface ring |
| **Inside a building** | **dominant**, reflected shock | dust from everything | — | thick, trapped | glass, furniture |
| **Airburst (> 3 m)** | **dominant** | none or a light ground scuff | none | the shell's own smoke only | fragment pattern is circular |

Implementation: one `SurfaceResponse` table indexed by the material under the burst point, supplying ejecta colour, ejecta mass multiplier, dust/fireball ratio, crater depth multiplier, decal set and sound set. This is a data table, not shader work, and it is the highest-value single addition to a generic explosion system.

```
struct SurfaceResponse {
    vec3  ejectaAlbedo;       // linear
    float ejectaMassScale;    // 0.0 (water) ... 1.6 (dry sand)
    float dustToFireRatio;    // 0.1 (airburst) ... 4.0 (dry dust)
    float craterDepthScale;   // 0.0 (rock, concrete) ... 1.3 (soft soil)
    float clodFraction;       // fraction of ejecta that is chunks vs dust
    float sparkRate;          // rock, concrete, metal only
    float wetness;            // drives mud splatter and darker ejecta
    uint  decalSet, soundSet;
};
```

## 7.5 Fragments

Casing fragment velocity from the Gurney cylinder equation:

```
V_0 = sqrt(2E) · (M/C + 1/2)^(-1/2)

    sqrt(2E) = Gurney constant: TNT 2440 m/s, Comp B 2700 m/s, RDX 2930 m/s
    M        = casing mass, C = charge mass
```

| Munition | C (kg) | Casing M (kg) | M/C | √(2E) | V₀ (m/s) |
|---|---|---|---|---|---|
| Hand grenade M67 | 0.18 | 0.21 | 1.17 | 2700 | 2091 * |
| 60 mm mortar | 0.18 | 1.10 | 6.11 | 2440 | 948 |
| 81 mm mortar | 0.95 | 2.60 | 2.74 | 2700 | 1500 |
| 105 mm M1 | 2.30 | 12.70 | 5.52 | 2440 | 994 |
| 155 mm M107 | 6.60 | 36.30 | 5.50 | 2440 | 996 |
| 155 mm M795 | 10.80 | 36.00 | 3.33 | 2700 | 1379 |

\* The grenade figure is high: the Gurney cylinder model assumes a uniform casing accelerated as one, which over-predicts for a thin casing with a pre-notched liner. Published values for the M67 are nearer 1500 m/s.

For a 155 mm shell, fragment throw can exceed **549 m**, while the blast-overpressure lethal radius is **less than 3 % of that (~15 m)**. Gameplay and visuals should reflect that asymmetry: the dangerous radius is set by fragments, not by the fireball.

Fragment rendering:

```
Spawn pattern: an equatorial belt, not a sphere. A cylindrical casing throws
  ~75 % of its fragments into a band of +-25 deg about the plane normal to the
  shell axis, with a forward bias equal to the shell's own velocity vector.
Count drawn:   40 - 300 (see budgets). The real count is 1,000-20,000; draw a
  representative sample and bias the sample toward the camera.
Speed drawn:   scale the drawn velocity down. A 1000 m/s fragment crosses the
  screen in under a frame. Draw the visible trace as a motion-blurred streak
  with length = v * dt and alpha = 1/(v*dt/L_ref), so the integrated energy is
  right even though the streak is dim (volume 2, chapter 7.24).
Incandescence: only fragments from steel casings against hard targets; most
  fragments are not glowing. A field of glowing orange dots is a stylisation.
Impacts:       spawn a sub-effect per impact within ~15 m of the camera only:
  dust puff, spark burst on rock/metal, splash on water, decal.
```

## 7.6 The shock wave, visually

The shock itself is invisible in clear dry air. What you can see:

| Phenomenon | Condition | Render |
|---|---|---|
| **Refraction ring** | always | screen-space UV distortion proportional to the density gradient; amplitude falls as `1/R²`; ring thickness ~0.02·R |
| **Condensation disc** | humid air, strong shock | a brief white disc in the expansion behind the front; lasts 5–40 ms |
| **Dust lift ring** | dusty ground | the ground dust disc (chapter 08) |
| **Foliage / grass displacement** | vegetation present | a radial wind impulse that bends grass and foliage outward then back |
| **Water surface ring** | over water | an expanding ring of flattened/rippled water |
| **Debris displacement** | loose props | a radial impulse on physics bodies, `J = Δp · A · t_d / 2` |

The refraction pass:

```glsl
// Screen-space distortion from a set of expanding spherical shocks.
// Render each shock as a sphere into an offscreen RG16F buffer, writing
// the view-space-projected gradient direction * strength.
float r    = length(p_world - centre);
float band = exp(-pow((r - R_t) / (0.02 * R_t), 2.0));     // thin gaussian shell
float amp  = strength / max(R_t * R_t, 1.0);
vec2  dir  = normalize(projectedRadialDirection);
offset     = dir * band * amp;                              // in UV, clamp to ~0.02
```

Cost at 1080p: one half-resolution pass plus a full-resolution resolve, roughly 0.15–0.3 ms. High and Ultra High only.

## 7.7 Smoke column

After `t_lum`, the fireball becomes a buoyant, rising plume.

```
Initial upward velocity (surface burst):   w_0 = 8 ... 25 m/s, scaling as W^(1/6)
Buoyant rise of a thermal:                 z(t) ~ z_0 + C * t^(1/2),  C ~ 6-12 m/s^(1/2)
Lateral growth:                            r(z) = r_0 + 0.25 * (z - z_0)     (entrainment)
Column height at 5 s:                      12 - 30 m for 105-155 mm
Wind shear:                                offset the column by integral of wind(z) dt;
                                           wind usually increases with height, so the
                                           column leans and the top trails downwind.
Colour over time:                          black (fresh soot, albedo 0.04)
                                           -> dark grey (0.12) at ~2 s
                                           -> dust-brown (0.25) at ~5 s as soil dust mixes
                                           -> pale (0.45) at ~15 s
```

Smoke is **not emissive**. It is lit. See volume 1 chapter 11.8 and volume 2 chapter 7.21 for the lighting; the essential points for explosion smoke:

- Use **six-way lightmaps** on the hero smoke so the column responds to the sun direction and to the fireball's own light while it is still burning. Two RGBA textures: A = (right, top, back, alpha), B = (left, bottom, front, emissive).
- While the fireball is alive, the smoke is lit **from inside**. Feed the fireball's dynamic light into the six-way evaluation and the column will be orange underneath and dark on top — the single most recognisable feature of a real burst.
- Use **motion-vector frame blending** so a 32–64-frame flipbook can cover a 10–20 s column.

The 6-way evaluation, with the squared-direction (Pythagorean) weighting:

```glsl
// Lts = light direction in the particle's tangent frame, normalised
vec3  w   = Lts * Lts;                               // components sum to 1
float lit = w.x * (Lts.x > 0.0 ? A.r : B.r)
          + w.y * (Lts.y > 0.0 ? A.g : B.g)
          + w.z * (Lts.z > 0.0 ? A.b : B.b);
color += lightColor * smokeAlbedo * lit;             // per light; ambient uses all six
```

A 4-channel variant that halves the memory, with the front/back maps approximated:

```glsl
float frontMap = 0.25 * (lm.x + lm.y + lm.z + lm.w);
float backMap  = frontMap * 0.6;                     // back is dimmer; tune per asset
```

## 7.8 Rendering construction

```
AN EXPLOSION IS SIX SYSTEMS, SEQUENCED

S1  FLASH       1-3 additive sprites + 1 emissive sphere mesh. 0 - t_lum.
                Emissive from the cooling curve (7.3). Scale to R_fb.
S2  FIREBALL    a flipbook with motion vectors, 6-way lit, alpha-blended,
                cross-fading from emissive to scattering as T falls.
                3-8 sprites; sizes drawn from a lognormal, not all equal.
S3  SHOCK       a refraction shell + a radial impulse on physics and foliage.
S4  EJECTA      chapter 08. Begins at t = 1 ms, peaks at 60-200 ms.
S5  SMOKE       6-way lit soft sprites, wind-advected, 8-40 of them.
S6  RESIDUE     crater mesh/decal, scorch decal, scattered debris. Permanent.

Plus 1-2 dynamic lights, 1 camera shake, 1 sound set, 1 physics impulse,
and a gameplay damage query.
```

Sprite sizing and placement rules that stop an explosion looking like a sprite:

```
- Never spawn all the fireball sprites at the same position. Distribute them
  on a sphere of radius 0.35 R_fb with random inward velocity so the cloud
  contracts slightly as it rises: this is what gives the boiling look.
- Draw sprite radii from a lognormal: median 0.45 R_fb, sigma 0.4.
  Equal-sized sprites read as a ring of discs.
- Rotate each sprite counter to its velocity vector. Real smoke is roughly
  incompressible, so an element moving outward rolls backward. This single
  trick removes the "sliding decals" look.
- Spin rate: 0.3 - 1.2 rad/s, sign from a per-particle hash.
- Offset each sprite's flipbook start frame by a per-particle hash so they do
  not pulse in unison.
- Fade sprites IN over 2-4 frames. A sprite that appears at full alpha pops.
```

Timing curves, as normalised functions of `u = t / t_lum`:

```
flashIntensity(u)  = pow(max(0, 1 - u), 3.2) * exp(-2.1*u)      peak at u=0
fireballRadius(u)  = R_fb * (1 - exp(-2.6 * u / 0.19))          63 % by u=0.19
smokeAlpha(u)      = smoothstep(0.35, 1.3, u)                   takes over as flash dies
lightIntensity(u)  = I_peak * pow(T(u)/T_0, 8.1)
```

## 7.9 Artillery specifics

### Impact geometry

Crater analysis gives the usable numbers: **artillery gun projectiles typically strike at 15–20° from horizontal; mortar projectiles at 70–80°.** The consequence:

| | Artillery (15–20°) | Mortar (70–80°) | Airburst |
|---|---|---|---|
| Crater shape | strongly elliptical, elongated downrange | near-circular | none |
| Ejecta pattern | asymmetric, biased downrange, with a "butterfly" of two lobes | symmetric inverted cone | none |
| Fragment pattern on the ground | forward-biased fan | circular | circular, wider |
| Fuze funnel / furrow | pronounced entry furrow uprange | shallow | — |
| Visual | the column leans downrange by 20–35° | vertical column | spherical burst, no column |

Implement the asymmetry by rotating the ejecta cone axis toward the bisector of the impact velocity and the surface normal, and by scaling the azimuthal density:

```
axis   = normalize( mix(surfaceNormal, reflect(v_impact, surfaceNormal), 0.55) )
density(phi) = 1 + 0.9 * cos(phi - phi_downrange)      // 1.9x downrange, 0.1x uprange
```

### Fuze types

| Fuze | Burst point | Effect |
|---|---|---|
| **Point detonating (PD)** | at the surface | standard crater, column, fragment fan into the ground (much wasted) |
| **Delay** | 0.05 s after impact, 0.3–2 m below surface | **the camouflet**: a much larger dirt column, a deeper crater, almost no fireball, a heavy dome-lift phase |
| **Proximity (VT)** | 3–20 m above ground | no crater, no column, a clean spherical fireball and a circular fragment pattern — by far the most lethal to troops in the open |
| **Time** | set altitude | as VT but less accurate |
| **Concrete piercing** | after penetration | spall, structural collapse, little external fireball |

The delay-fuze case is the one worth building specially: a subsurface burst heaves a **dome** of soil that lifts coherently for 20–60 ms before breaking into the ejecta cone. Centrifuge measurements of buried charges show the 1 g case producing rapid radial expansion of a soil dome into an inverted-cone ejecta flow with an **included angle of 79.5°**.

### Barrage pacing

For an artillery barrage, the visual budget is dominated by concurrency, not by the single effect:

```
Stagger:   do not land rounds simultaneously. Real battery fire has 0.2-1.5 s
           of dispersion within a salvo of 4-8 guns, and 8-30 s between salvos.
Budget:    cap simultaneous full-quality explosions at the tier limit; beyond
           that, spawn a reduced set (flash + light + sound + decal, no
           ejecta, 2 smoke sprites). Choose by screen-space area.
Smoke accumulation: cap total smoke sprites globally, not per explosion, and
           retire the oldest. A 10-round barrage must not cost 10x the smoke.
Audio:     the sound arrives at 343 m/s. At 500 m that is 1.46 s after the
           flash. Implementing the delay is nearly free and is one of the
           strongest realism cues in the whole system.
```

## 7.10 Budgets

| Item | Very Low | Low | Medium | High | Ultra High |
|---|---|---|---|---|---|
| Flash sprites | 1 | 1 | 2 | 3 | 3 + emissive mesh |
| Fireball sprites | 1 | 2 | 4 | 6 | 8 |
| Flipbook | 4×4 single | 6×6 cross-fade | 6×6 + motion vectors | 8×8 + MV | 8×8 + MV + 6-way |
| Fireball lighting | unlit, emissive only | unlit | per-vertex | 6-way, nearest 2 | 6-way, per pixel |
| Smoke sprites per burst | 2 | 4 | 8 | 20 | 40 |
| Smoke lifetime | 2 s | 4 s | 8 s | 15 s | 25 s |
| Global smoke sprite cap | 40 | 120 | 400 | 1200 | 3000 |
| Fragments drawn | 0 | 0 | 40 | 120 | 300 |
| Fragment impact sub-effects | 0 | 0 | 4 nearest | 12 nearest | 24 nearest |
| Shock refraction | no | no | no | yes | yes |
| Dynamic lights per burst | 1, 4 frames | 1 | 1 | 2 | 2 + volumetric injection |
| Concurrent full-quality bursts | 2 | 4 | 8 | 16 | 32 |
| Surface response table | 3 surfaces | 5 | 9 | 12 | 12 + wetness blending |
| Sound propagation delay | no | yes | yes | yes | yes + reflections |
| Estimated cost per burst | 0.05 ms | 0.15 ms | 0.45 ms | 1.1 ms | 2.2 ms |

Cross-reference: the soft-particle buffer resolution, overdraw caps, sorting strategy and particle-count caps that these budgets sit inside are in volume 2, chapter 7.28. The explosion system must draw from the same global fill budget as the weather and ambient systems, not have its own.

## 7.11 Common errors

| Error | Correction |
|---|---|
| Fireball size unrelated to charge | `R_fb = 3.77 W^⅓`. A grenade is 2.2 m, a 155 mm is 7.1 m, a 250 kg bomb is 19 m. |
| Flash lasts 0.5–1 s | `t_lum = 0.10 W^⅓`. A 155 mm flash is 190 ms and 95 % of the light is in the first 50 ms. |
| Large and small explosions evolve at the same rate | Times scale as `W^⅓`. |
| Yellow / orange throughout | Core clips white at any realistic exposure; only the thin rim shows 1700–3000 K hue. |
| Same explosion on every surface | A `SurfaceResponse` table. Dry sand and shallow water produce opposite effects. |
| Perfectly spherical, perfectly symmetric | Impact angle 15–20° for guns: elliptical crater, downrange-biased ejecta lobes. |
| Smoke drawn additive and unlit | Alpha-blended, six-way lit, lit from inside while the fireball burns. |
| Smoke dissipates in 2 s | 8–25 s for the column, plus settling dust. |
| Explosions audible instantly | 343 m/s. 1.46 s at 500 m. |
| Every round in a barrage lands at once | 0.2–1.5 s dispersion within a salvo. |
| Thousands of glowing fragments | Most fragments do not glow; draw 40–300 as motion-blurred streaks. |
| No crater or scorch afterwards | The residue is what makes the world remember. Chapter 08. |

## Rules of thumb

- Three constants size any explosion: `R_fb = 3.77 W^⅓`, `t_peak = 0.0139 W^⅓`, `t_lum = 0.10 W^⅓`.
- 26 ms of flash, 1.5 s of dirt, 8 s of smoke, 25 s of dust, and a permanent crater. Get the proportions right before anything else.
- Brightness goes as `T^8.1`, so the flash is over almost before it starts.
- The surface decides what the explosion looks like. Build the table first.
- Smoke is lit, and while the fireball lives it is lit from inside.
- Artillery lands at 15–20°. Nothing about a real artillery impact is symmetric.
- Delay the sound. It is nearly free and it is one of the strongest cues available.

## Sources

- Hopkinson–Cranz cube-root scaling `Z = R/W^(1/3)`: Hopkinson (1915), Cranz (1936); standardisation and the Brode overpressure model as used here are described in LLNL-JRNL-845394, "Chemical Blast Standard (1 kg)" (https://www.osti.gov/servlets/purl/2203616).
- Taylor–Sedov `R = S(E t²/ρ)^(1/5)` with `S ≈ 1.03` for γ = 1.4: G. I. Taylor's dimensional analysis, as re-derived in "Teaching physics in real-life contexts: The Beirut explosion" (https://arxiv.org/pdf/2209.14441) and applied to fireball tracing in "Beirut explosion: TNT equivalence from the fireball evolution" (Shock Waves, 2021).
- `R_fireball = 3.77 W^(1/3)` m as the empirical HE fireball radius: blast-radius calculators implementing the standard empirical dataset (https://calculinohub.com/calculators/physics/blast-radius-calculator; a second source gives 3.9). `t_peak = 0.0139 W^(1/3)` is derived here by equating the Sedov radius to that value. `t_lum = 0.10 W^(1/3)` is an authored value for condensed HE: the widely used `t = 0.45 M^(1/3)` correlation (Roberts; CCPS) is for hydrocarbon/BLEVE fireballs, measured on propane releases of 279–1708 kg, and over-predicts condensed-HE luminous duration by roughly 4–5×.
- Maximum conventional HE temperature ~5000 °C against tens of millions for fission: Glasstone and Dolan, *The Effects of Nuclear Weapons*, chapter II (https://atomicarchive.com/resources/documents/effects/glasstone-dolan/chapter2.html).
- Gurney cylinder equation and Gurney constants: standard terminal-ballistics material. Casing and charge masses are published nominal figures.
- 155 mm fragment throw exceeding 549 m against a blast-overpressure lethal radius under 3 % of that (~15 m): "Typical fragment velocities from artillery and mortar rounds", *Journal of Trauma* (ResearchGate 24415884).
- Artillery impact angles 15–20°, mortar 70–80°, and the circular-versus-elliptical crater consequence: ARES, "An Introduction to Crater Analysis" (https://armamentresearch.com/an-introduction-to-crater-analysis/).
- Buried-charge soil dome with an inverted-cone ejecta flow of 79.5° included angle at 1 g: "Centrifuge Characterization of Buried, Explosive-Induced Soil Ejecta Kinematics and Crater Morphology", *Journal of Dynamic Behavior of Materials* (2016).
- Six-way lighting layout and the squared-direction weighting: Unity VFX Graph six-way lighting (https://unity.com/blog/engine-platform/realistic-smoke-with-6-way-lighting-in-vfx-graph) and the community shader at https://realtimevfx.com/t/6-way-lightmap-wip/5457; the 4-channel front/back approximation `frontMap = 0.25(x+y+z+w)` is from the Skull & Bones breakdown (https://realtimevfx.com/t/smoke-lighting-and-texture-re-usability-in-skull-bones/5339). See also volume 1, chapter 11.8.
- Motion-vector frame blending reducing an 11×11 (121-frame) flipbook to 6×6 (32 frames): https://loronsodev.github.io/posts/explosion-fx/. Battlefront II packing all VFX into a single 12288×8192 atlas with two flipbooks per smoke effect and six packed lightmaps: https://simonschreibt.de/gat/battlefront-ii-layered-explosion/.
- Counter-rotating particles to mimic incompressibility: the realtimevfx community tip quoted at https://simonschreibt.de/category/gat/feed/.
- Smoke-column rise constants, surface-response values, sprite-sizing distributions, fuze behaviour details, barrage pacing and all cost figures are constructed planning values. Measure on the target.
