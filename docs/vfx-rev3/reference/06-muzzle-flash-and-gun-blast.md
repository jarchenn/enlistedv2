# 06. Muzzle flash and gun blast

A muzzle flash is not a sprite. It is a structured, underexpanded supersonic jet with a shock bottle, a Mach disc, a detached combustion region, an expanding blast sphere, a dust disc on the ground and a smoke cloud, and each of those has a position and a duration you can compute. This chapter gives the geometry, the timing and the implementation, scaled from 5.56 mm to 125 mm.

## 6.1 The five components, measured

| Component | Where | Temperature | Duration | Appearance |
|---|---|---|---|---|
| **Pre-flash** | ahead of the muzzle | — | < 0.3 ms | very weak; gas leaking past the projectile. Usually not rendered. |
| **Primary flash** | immediately at the muzzle, within ~1 calibre | **1645 K measured, max 1400–2000 K** | 0.3 – 1 ms | small, deep orange, dissipates almost instantly |
| **Intermediate flash** | a **reddish disc, slightly dished toward the gun**, at ~7.5 cm for small arms, 20–25 calibres for large guns | 1400 – 2000 K, falling to ~1468 K at 0.70 ms | shot ejection until chamber pressure drops | brightest at the gun-side edge, fading outward |
| **Secondary flash** | 300 – 500 mm downstream for a 7.62 mm rifle; a ragged vortex | **~2500 K** (3000 K when fired into oxygen; ~2200 K by one spectral measurement) | 1 – 5 ms | large, voluminous, yellowish-white, spreads in all directions; much brighter and more extensive than the other two |
| **Afterglow / unburnt powder** | scattered, up to several metres | 1000 – 1600 K | 5 – 300 ms | discrete incandescent grains on ballistic paths |

Durations:

| Weapon class | Visible flash duration | IR duration |
|---|---|---|
| Small arms, general | 1 – 2 ms | < 30 ms |
| .30-06 (occasional) | up to 7 ms | — |
| 5.56 mm M16 | > 80 % of energy within ~6 ms | — |
| Large calibre (tank, artillery) | — | **< 200 ms in MWIR** |
| 120 mm KE round at 100 m | saturates a thermal camera; central portion comes out of saturation after **~600 ms**; flash width ~13 m at the target | — |

Secondary flash is **suppressible** — firing into an inert gas removes it while leaving the primary and intermediate flashes unchanged — which is the mechanism flash hiders and salt-based flash-suppressing propellant additives exploit. In-game, a flash hider should remove the big component and leave the small ones, not scale everything down by 60 %.

## 6.2 Jet geometry: where to put things

The muzzle gas is a highly underexpanded supersonic jet. Its structure is set by the pressure ratio. The Mach disc stand-off, from the standard underexpanded-jet relation:

```
x_Mach = 0.67 · D · sqrt( p_muzzle / p_ambient )

   D         bore diameter (m)
   p_muzzle  gas pressure at the muzzle when the projectile exits (Pa)
   p_ambient 101,325 Pa at sea level
```

This single formula predicts the observed intermediate-flash disc position across the whole calibre range:

| Weapon | D (mm) | p_muzzle (MPa) | x_Mach (m) | in calibres | Bottle radius ≈ 0.4·x_M | Secondary flash, 3–6·x_M |
|---|---|---|---|---|---|---|
| 5.56×45 (M4) | 5.56 | 45 | **0.079** | 14 | 0.03 m | 0.24 – 0.47 m |
| 7.62×51 | 7.62 | 55 | 0.119 | 16 | 0.05 m | 0.36 – 0.71 m |
| 7.62×39 (AK) | 7.62 | 40 | 0.101 | 13 | 0.04 m | 0.30 – 0.61 m |
| 12.7×99 (M2HB) | 12.7 | 60 | 0.207 | 16 | 0.08 m | 0.62 – 1.24 m |
| 20 mm autocannon | 20 | 70 | 0.352 | 18 | 0.14 m | 1.06 – 2.11 m |
| 40 mm Bofors | 40 | 55 | 0.624 | 16 | 0.25 m | 1.87 – 3.74 m |
| 76 mm ZiS-3 | 76 | 45 | 1.073 | 14 | 0.43 m | 3.2 – 6.4 m |
| 105 mm M68 | 105 | 60 | 1.712 | 16 | 0.68 m | 5.1 – 10.3 m |
| 120 mm M256 | 120 | 70 | **2.113** | 18 | 0.85 m | 6.3 – 12.7 m |
| 125 mm 2A46 | 125 | 65 | 2.121 | 17 | 0.85 m | 6.4 – 12.7 m |

Check against the observations in 6.1: the formula puts the 5.56 mm disc at 7.9 cm against an observed "about three inches (7.5 cm) from the muzzle of a small-caliber weapon", and the 120 mm disc at 18 calibres against an observed "about 20–25 calibers". Both agree. **Use the formula; do not hand-place the disc.**

Jet shape, as a set of authoring rules:

```
Total flash envelope:
    length  L  = 2.0 to 3.5 x_Mach        (small arms, no device)
                 4.0 to 7.0 x_Mach        (large calibre, secondary flash present)
    max radius R = 0.35 to 0.55 x_Mach at x ~ 0.6 x_Mach, tapering both ways
    half-angle near the muzzle: 12-18 deg for a plain muzzle
                                25-35 deg with a muzzle brake (ports redirect sideways)
                                8-12 deg with a flash hider (prongs delay mixing)
Shape function (use as the profile of a swept mesh or the alpha of a billboard):
    r(x) = R · (x/x_0)^0.45 · exp(-1.2 · (x/x_0 - 1)^2)      x_0 = 0.6 x_Mach
```

## 6.3 The blast sphere, the dust disc and the smoke

The propellant gas itself is an explosion. Treat it as an equivalent charge:

```
W_eq = m_propellant · (E_propellant / E_TNT) · f_blast
       E_propellant ≈ 3.9 MJ/kg (nitrocellulose/nitroglycerine double base)
       E_TNT        = 4.184 MJ/kg
       f_blast      ≈ 0.30 - 0.40   (the fraction not already spent on the projectile)
```

| Weapon | m_prop (kg) | W_eq (kg TNT) | Blast/flash sphere R = 3.77 W^⅓ (m) | t = 0.10 W^⅓ (s) | Ground dust disc radius (m) |
|---|---|---|---|---|---|
| 5.56×45 | 0.0017 | 0.0006 | 0.31 | 0.008 | 0.8 (prone only) |
| 7.62×51 | 0.0030 | 0.0010 | 0.37 | 0.010 | 0.9 (prone only) |
| 12.7×99 | 0.0152 | 0.0050 | 0.64 | 0.017 | 1.6 |
| 20 mm | 0.040 | 0.013 | 0.89 | 0.024 | 2.2 |
| 40 mm Bofors | 0.300 | 0.098 | 1.74 | 0.046 | 4.3 |
| 76 mm ZiS-3 | 1.10 | 0.359 | 2.68 | 0.071 | 6.7 |
| 105 mm M68 | 5.60 | 1.83 | 4.61 | 0.122 | 11.5 |
| 120 mm M256 | 7.90 | 2.58 | 5.17 | 0.137 | 12.9 |
| 125 mm 2A46 | 7.10 | 2.32 | 4.99 | 0.132 | 12.5 |
| 155 mm howitzer (charge 5) | 9.00 | 2.94 | 5.40 | 0.143 | 13.5 |

Ground dust disc radius is taken as **2.5 × the blast sphere radius** (an authored scaling, sanity-checked against the 10–20 m dust discs visible in tank-firing footage).

### The ground dust disc is the missing element

This is the single most characteristic feature of real large-calibre firing and it is almost never implemented. The muzzle blast reflects off the ground and drives a radially expanding sheet of dust and loose debris away from a point below the muzzle.

```
GROUND DUST DISC

Trigger when:  h_muzzle < 3.0 · D_bore · sqrt(p_e/p_a) · 0.5     (muzzle low over the ground)
               AND surface is loose (dust/sand/dry soil/snow)
Origin:        the ground point directly below the muzzle, offset 0.3 - 0.8 x_Mach forward
Shape:         an annulus, inner radius r_i(t), outer r_o(t), height 0.15 - 0.6 m,
               strongly asymmetric: 1.0 forward, 0.55 sideways, 0.25 behind for a
               plain muzzle; nearly symmetric with a muzzle brake, with two
               pronounced side lobes along the brake ports.
Expansion:     r_o(t) = R_max · (1 - exp(-t / tau)),  tau = 0.055 s,
               R_max from the table above.  r_i(t) = 0.55 · r_o(t).
Lifetime:      the disc stops expanding by t ~ 0.25 s, then lifts and drifts
               with the wind, fading over 3 - 8 s.
Opacity:       peak alpha 0.5 - 0.8 on dry dust, 0.1 - 0.2 on damp ground,
               0.0 on wet ground, grass or concrete.
Count:         18 - 40 soft sprites for the annulus, radially oriented
               (rotate each sprite so its long axis is tangential).
```

### Smoke

| Weapon | Smoke volume | Persistence (still air) | Note |
|---|---|---|---|
| 5.56/7.62 rifle | 0.02 – 0.1 m³ | 0.5 – 2 s | mostly invisible outdoors in daylight |
| Suppressed rifle | 0.1 – 0.4 m³ | 2 – 5 s | the suppressor holds and then releases gas; visibly more smoke, not less |
| 12.7 mm | 0.3 – 1 m³ | 2 – 4 s | |
| Tank gun, with bore evacuator | 3 – 15 m³ at the muzzle | 5 – 20 s | the evacuator then purges the bore 0.4–1.5 s **after** the shot, producing a second, smaller puff at the muzzle — a detail that reads as authenticity |
| Tank gun, no evacuator | 3 – 15 m³ | 5 – 20 s | gas vents into the fighting compartment on opening the breech |
| Artillery | 20 – 100 m³ | 10 – 40 s | |

The bore-evacuator second puff is worth implementing: 0.4–1.5 s after firing, a small cloud emerges from the evacuator bulge partway along the barrel and from the muzzle. It takes one extra emitter and is instantly recognisable.

## 6.4 Timeline

All times from the projectile leaving the muzzle.

### Small arms (5.56 mm, no flash hider)

| t (ms) | Event | Render |
|---|---|---|
| −0.2 | pre-flash | skip |
| 0 | projectile exits; primary flash | 1645 K, ε 0.12, ~1 calibre sphere |
| 0.1 – 0.8 | shock bottle forms; intermediate disc at 7.9 cm | 1800 K, ε 0.20, a dished disc normal to the bore |
| 0.3 – 2.0 | secondary flash at 0.24–0.47 m | 2500 K, ε 0.25, ragged, largest component |
| 0.5 – 8 | blast sphere expands to 0.31 m | not drawn directly; drives the dust and a brief distortion |
| 1 – 300 | unburnt powder grains | 6–20 ember particles, 1000–1600 K, ballistic, 2–15 m/s |
| 2 – 2000 | smoke | 2–5 soft sprites, dissipating |
| 0 – 60 | dynamic light | peak 5.2 × 10³ cd, decaying with `T^8.1` |

Total visible life: **about 2 ms of flash**. At 60 fps that is **0.12 frames**. The flash must therefore be rendered for a minimum of one full frame regardless, and its intensity integrated over the frame rather than sampled:

```
intensity_this_frame = (1/dt) · ∫ I(t) dt over the frame
                     ≈ I_peak · min(1, t_flash / dt)
For t_flash = 2 ms at 60 fps (dt = 16.7 ms): factor 0.12.
```

Without this, the flash brightness depends on frame rate — a classic bug that makes effects look different at 30, 60 and 144 fps.

### Tank gun (120 mm)

| t (ms) | Event | Render |
|---|---|---|
| −3.0 | gun begins to recoil; 38 mm of travel before the projectile exits (chapter 09) | barrel already moving |
| 0 | projectile exits | |
| 0 – 8 | primary + intermediate; disc at 2.11 m | 2000 K, ε 0.5 |
| 2 – 35 | secondary flash, 6.3–12.7 m long envelope | 2400 K, ε 0.55, the main visual |
| 0 – 140 | blast sphere grows to 5.17 m | drives the dust disc, a shockwave distortion ring, and foliage/debris displacement |
| 10 – 250 | ground dust disc expands to ~12.9 m | 24–40 sprites |
| 30 – 600 | thermal afterglow | fades through 1800 K → 1200 K |
| 50 – 20000 | smoke cloud, 3–15 m³ | 8–20 soft sprites, wind-advected |
| 400 – 1500 | bore evacuator purge | 3–5 sprites at the evacuator and the muzzle |
| 0 – 400 | dynamic light | peak 5.1 × 10⁶ cd, 5 × 10⁴ lx at 10 m |

## 6.5 Rendering construction

```
MUZZLE FLASH = 4 ELEMENTS, NOT 1 SPRITE

E1  CORE MESH          a swept-profile mesh (6.2), additive, emissive from the
                       blackbody value for T(t), oriented along the bore,
                       scaled by x_Mach. 60-200 tris. Always camera-independent
                       (a real 3D shape), NOT a billboard: a billboard flash
                       seen from the side is the most obvious tell there is.
E2  DISC               one quad, normal to the bore, at x_Mach, slightly
                       dished (a cone of half-angle 80 deg), additive,
                       brightest at its inner edge:
                           alpha = pow(1 - r/R, 0.6)
E3  FLIPBOOK PUFF      1-3 soft, lit sprites for the secondary-flash vortex
                       and the smoke, with motion-vector frame blending.
E4  GROUND DUST DISC   the annulus of 6.3, only for large calibre or prone fire.

Plus: one dynamic light, one camera shake impulse, one decal if the ground is
      scorched, and ember particles for unburnt powder.
```

Orientation and randomisation:

```glsl
// Roll the core mesh about the bore axis by a per-shot hash so consecutive
// shots do not look identical. Do NOT randomise scale by more than +-12 % -
// real flashes are consistent; large random scaling reads as a game effect.
float roll  = hash1(uvec2(shotIndex, weaponID)) * 6.2831853;
float scale = 1.0 + (hash1(uvec2(shotIndex, weaponID + 7u)) - 0.5) * 0.24;
```

### Muzzle devices

| Device | x_Mach | Envelope | Dust disc | Secondary flash | Lateral lobes |
|---|---|---|---|---|---|
| None | baseline | 1.0× | 1.0× | present | none |
| Flash hider (prong/birdcage) | ×0.9 | 0.45× length, 0.8× radius | 0.6× | **largely removed** | 3–5 thin jets along the prong gaps |
| Muzzle brake | ×1.0 | 0.7× forward | **1.6×, strongly side-lobed** | present | 2 strong side lobes at the port angle (typically 30–60° from the bore) |
| Suppressor | ×0.35 | 0.25× | 0.4× | removed | small, soft; **more smoke, released late** |
| Compensator | ×1.0 | 0.85× | 1.2× | present | 1 upward lobe |

A muzzle brake redirecting gas sideways is why braked tank guns throw a far larger and more symmetric ground dust disc than unbraked ones.

### Subsonic, supersonic and the bullet's own shock

A supersonic projectile drags a Mach cone. It is invisible in clear air but it:

- kicks a thin line of dust along its path over dry ground within ~1 m of the surface,
- produces the characteristic crack at the receiver,
- briefly distorts the background if a refraction pass exists (half-angle `μ = asin(1/M)`; at M = 2.5, μ = 23.6°).

A refraction/distortion pass for the blast sphere and the Mach cone is a High-and-above feature: render a screen-space UV offset proportional to the density gradient. Budget 0.1–0.25 ms at 1080p for the full-screen resolve.

## 6.6 Per-weapon data table

Everything a muzzle-flash system needs, as data. `T_prim`, `T_sec` in kelvin; `I_peak` in candela; durations in ms.

| Weapon | D (mm) | x_Mach (m) | L_env (m) | T_prim | T_sec | ε_sec | t_vis (ms) | I_peak (cd) | Dust R (m) | Embers | Smoke m³ |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 5.56×45 | 5.56 | 0.079 | 0.24 | 1645 | 2500 | 0.20 | 2.0 | 5.2 × 10³ | 0.8 | 6 | 0.05 |
| 7.62×51 | 7.62 | 0.119 | 0.36 | 1645 | 2500 | 0.25 | 2.5 | 1.1 × 10⁴ | 0.9 | 8 | 0.08 |
| 7.62×39 | 7.62 | 0.101 | 0.32 | 1600 | 2450 | 0.28 | 3.0 | 1.3 × 10⁴ | 0.9 | 10 | 0.10 |
| 9 mm pistol | 9.0 | 0.055 | 0.18 | 1550 | 2300 | 0.30 | 3.5 | 6 × 10³ | 0.4 | 12 | 0.06 |
| 12.7×99 | 12.7 | 0.207 | 0.70 | 1700 | 2550 | 0.35 | 4.0 | 9 × 10⁴ | 1.6 | 16 | 0.5 |
| 20 mm | 20 | 0.352 | 1.2 | 1800 | 2550 | 0.40 | 6 | 3 × 10⁵ | 2.2 | 20 | 1.2 |
| 40 mm Bofors | 40 | 0.624 | 2.5 | 1900 | 2500 | 0.45 | 12 | 1.2 × 10⁶ | 4.3 | 25 | 4 |
| 76 mm | 76 | 1.073 | 4.3 | 2000 | 2450 | 0.50 | 20 | 1.8 × 10⁶ | 6.7 | 30 | 8 |
| 105 mm | 105 | 1.712 | 7.5 | 2100 | 2400 | 0.55 | 30 | 4.0 × 10⁶ | 11.5 | 35 | 12 |
| 120 mm | 120 | 2.113 | 9.5 | 2150 | 2400 | 0.55 | 35 | 5.1 × 10⁶ | 12.9 | 40 | 15 |
| 125 mm | 125 | 2.121 | 9.5 | 2150 | 2400 | 0.55 | 35 | 5.0 × 10⁶ | 12.5 | 40 | 15 |
| 155 mm howitzer | 155 | 2.3 | 11 | 2200 | 2450 | 0.60 | 45 | 7 × 10⁶ | 13.5 | 45 | 40 |

`L_env` is the total flash envelope length; scale the core mesh to it. Everything else derives from the formulas above, so a new weapon needs only bore diameter, muzzle pressure, propellant mass and a device type.

## 6.7 Budgets

| Item | Very Low | Low | Medium | High | Ultra High |
|---|---|---|---|---|---|
| Core representation | 1 billboard | 1 billboard | swept mesh (60 tris) | swept mesh (120 tris) | swept mesh (200 tris) |
| Intermediate disc | no | no | yes | yes | yes |
| Secondary flash sprites | 1 | 1 | 2 | 3 | 4 |
| Flipbook | single frame | 4×4, cross-fade | 6×6 + motion vectors | 8×8 + motion vectors | 8×8 + MV + six-way lighting |
| Ground dust disc | no | large calibre only, 8 sprites | 16 sprites | 28 sprites | 40 sprites |
| Ember particles | 0 | 0 | 8 | 20 | 40 |
| Smoke sprites | 0 | 1 | 3 | 8 | 16 |
| Dynamic light | 1, unshadowed, 3-frame | 1, unshadowed | 1, unshadowed | 1, 1-cascade shadow for the player's weapon | 2, shadowed |
| Light injected into volumetrics | no | no | no | yes | yes |
| Blast refraction | no | no | no | yes | yes |
| Bore-evacuator puff | no | no | yes | yes | yes |
| Max concurrent flashes | 4 | 8 | 16 | 32 | 64 |
| Estimated cost per flash | 0.02 ms | 0.05 ms | 0.12 ms | 0.30 ms | 0.55 ms |

With a squad of twelve firing automatic weapons, the concurrency cap matters more than the per-flash cost. Cap by screen-space importance: sort candidate flashes by `I_peak / d²` and keep the top N.

## 6.8 Common errors

| Error | Correction |
|---|---|
| A flat billboard star, 4 or 6 points | A swept 3D mesh. The star shape is a film-camera artefact, not a flash. |
| Pale yellow or white | 1645–2500 K is `#FF7A00`–`#FFA64A`. White is what the exposure does. |
| Same flash day and night | Emit absolute radiance; a rifle primary flash is 5 stops *under* clipping in daylight. |
| Flash lasts 4–8 frames | 1–2 ms for small arms. One frame, with the energy integrated over `dt`. |
| Brightness varies with frame rate | Multiply by `min(1, t_flash / dt)`. |
| Flash hider scales everything down | It removes the *secondary* flash specifically and leaves the primary and intermediate unchanged. |
| No ground interaction | The dust disc is the most recognisable feature of large-calibre firing. |
| Huge random scale variation per shot | ±12 % at most. |
| Suppressor produces less smoke | It produces *more*, released late and low-velocity. |
| No bore-evacuator puff on a tank gun | 0.4–1.5 s after the shot, from the evacuator bulge and the muzzle. |
| Smoke drawn additive | Smoke is not emissive. Alpha-blend and light it (chapter 07). |

## Rules of thumb

- `x_Mach = 0.67 · D · sqrt(p_e/p_a)` places everything. Scale the whole effect to it.
- The secondary flash is the big one, it sits 3–6 Mach-disc lengths out, it is 2500 K, and it is what a flash hider removes.
- Small arms: 2 ms. Tank guns: 35 ms of flash, 250 ms of dust, 20 s of smoke.
- Integrate flash energy over the frame or brightness will track frame rate.
- Large-calibre flashes without a ground dust disc look weightless.
- A muzzle brake doubles the dust and splits it into side lobes.
- One flash is four elements: core mesh, disc, puff, dust.

## Sources

- Primary-flash gas temperature 1645 K by emission–absorption spectroscopy, secondary flash ~2500 K at 300–500 mm downstream (3000 K in oxygen), and the suppression of the secondary flash by firing into inert gas: Klingenberg, "Investigation of combustion phenomena associated with the flow of hot propellant gases I", OSTI 7238764 (https://www.osti.gov/biblio/7238764).
- Intermediate-flash temperatures 1400–2000 K, falling to 1468 K at 0.70 ms: CARS temperature measurements in the muzzle-flash region of a 7.62 mm rifle, DTIC ADA190786 (https://apps.dtic.mil/sti/pdfs/ADA190786.pdf).
- Secondary flash as a "bright-yellow flash with a temperature of approximately 2200 K": spectral measurements of muzzle flash with multispectral and hyperspectral sensors (ResearchGate 233864754).
- Flash component geometry — intermediate flash as "a reddish disc, slightly dished towards the gun, which appears about three inches (7.5 cm) from the muzzle of a small-caliber weapon and about 20–25 calibers from the muzzle for larger caliber weapons", brightest at the gun-side edge; secondary flash as "a rather ragged vortex of yellowish white flame": NavWeaps, History and Technology — Muzzle Flash (https://www.navweaps.com/index_tech/tech-090.php).
- Small-arms flash durations 1–2 ms (up to 7 ms for .30-06), IR < 30 ms: Burke and Bratlie (2011) as reported in "Temporal Characterization of Small Arms Muzzle Flash in the Broadband Visible" and in arXiv:1410.6025.
- 5.56 mm M16: > 80 % of flash energy within ~6 ms: Telops FAST-IR application note on small-calibre muzzle flash.
- Large-calibre flash < 0.2 s in the 3–5 µm band: Telops FAST thermal imaging, tank/artillery muzzle-flash analysis (http://info.telops.com/rs/telops/images/FAST%20Thermal%20Imaging%20-%20Tank%20Muzzle%20Flash%20Analysis.pdf).
- 120 mm M865 KE firing: flash ~13 m wide at the target, central portion out of saturation after ~600 ms: DTIC ADA396882, "Infrared Signatures of the Muzzle Flash of a 120 mm Tank Gun".
- Muzzle blast raising dust from prone firing and from large-calibre guns firing low and flat, and the shock-bottle / Mach-disc / shock-diamond structure: Wikipedia "Muzzle blast" and "Muzzle flash", and the CFD muzzle-blast overpressure study (ResearchGate 328582650).
- `x_Mach = 0.67 D sqrt(p_e/p_a)` is the standard underexpanded-jet Mach-disc stand-off relation (Ashkenas and Sherman); the agreement with the two observed disc positions above is checked here.
- Blast-sphere and fireball scaling: see chapter 07's sources.
- Muzzle pressures, propellant masses, `f_blast`, dust-disc scaling (2.5×), smoke volumes, ember counts, device multipliers, per-weapon `ε` and all cost figures are constructed planning values, not measurements.
