# 05. Combustion light, colour and exposure: the physical basis for flashes, fires and fireballs

Muzzle flashes, tracers, embers, artillery bursts and fuel fires are all the same thing optically: hot gas and hot soot radiating approximately as a grey body, seen through an exposure. Nearly every unconvincing effect in a game comes from authoring the *displayed* colour instead of the *emitted* radiance and letting the exposure produce the displayed colour. This chapter gives the numbers.

## 5.1 Blackbody colour, computed

Computed by integrating Planck's law against the CIE 1931 2° colour-matching functions from 360 to 830 nm at 1 nm, then converting through CIE XYZ to linear Rec.709/sRGB primaries at D65. Colours are normalised so the largest channel is 1 (hue and saturation only — brightness is set separately in 5.3).

| T (K) | CIE x | CIE y | lin R | lin G | lin B | hue as sRGB hex | Wien peak (nm) | M/M(1645 K) |
|---|---|---|---|---|---|---|---|---|
| 1200 | 0.6180 | 0.3746 | 1.000 | 0.095 | 0.000 | `#FF5700` | 2415 | 0.28 |
| 1400 | 0.5935 | 0.3909 | 1.000 | 0.141 | 0.000 | `#FF6900` | 2070 | 0.52 |
| **1645** | 0.5641 | 0.4053 | **1.000** | **0.193** | **0.000** | `#FF7A00` | 1762 | 1.00 |
| 1800 | 0.5462 | 0.4112 | 1.000 | 0.224 | 0.000 | `#FF8200` | 1610 | 1.43 |
| 2000 | 0.5242 | 0.4157 | 1.000 | 0.266 | 0.008 | `#FF8D15` | 1449 | 2.19 |
| 2200 | 0.5036 | 0.4171 | 1.000 | 0.313 | 0.028 | `#FF982F` | 1317 | 3.20 |
| **2500** | 0.4755 | 0.4151 | **1.000** | **0.380** | **0.068** | `#FFA64A` | 1159 | 5.33 |
| 2800 | 0.4507 | 0.4097 | 1.000 | 0.444 | 0.117 | `#FFB260` | 1035 | 8.39 |
| 3000 | 0.4359 | 0.4050 | 1.000 | 0.484 | 0.155 | `#FFB96E` | 966 | 11.06 |
| 3200 | 0.4225 | 0.3998 | 1.000 | 0.522 | 0.195 | `#FFBF7A` | 906 | 14.32 |
| 3500 | 0.4046 | 0.3914 | 1.000 | 0.577 | 0.261 | `#FFC88C` | 828 | 20.49 |
| 4000 | 0.3799 | 0.3772 | 1.000 | 0.659 | 0.379 | `#FFD4A6` | 724 | 34.96 |
| 4500 | 0.3604 | 0.3639 | 1.000 | 0.732 | 0.504 | `#FFDEBC` | 644 | 56.00 |
| 5000 | 0.3449 | 0.3519 | 1.000 | 0.795 | 0.630 | `#FFE7D0` | 580 | 85.35 |
| 5778 (sun) | 0.3263 | 0.3359 | 1.000 | 0.880 | 0.823 | `#FFF1EA` | 502 | 152.21 |
| 6500 (D65) | 0.3135 | 0.3237 | 1.000 | 0.944 | 0.993 | `#FFF9FE` | 446 | 243.78 |

Validation: the row at 6500 K reproduces the textbook Planckian chromaticity (0.3135, 0.3237), and 5778 K reproduces the solar value (0.3263, 0.3359). The last column is `(T/1645)⁴`, the Stefan–Boltzmann ratio.

**The single most important consequence.** Combustion in guns and conventional explosives sits between 1600 K and 3000 K. That is linear RGB between (1.00, 0.19, 0.00) and (1.00, 0.48, 0.16). It is **orange**, not yellow and not white. A muzzle flash authored as `#FFE08A` is physically a 3900 K source, more than twice as hot as the gas actually is.

A runtime approximation accurate to about ±0.02 per channel over 1000–6500 K, if you need the ramp as a function rather than a LUT:

```glsl
// Normalised blackbody hue, max channel = 1. T in kelvin.
vec3 blackbodyHue(float T) {
    float t  = clamp(T, 1000.0, 15000.0) * 1e-3;
    float t2 = t * t, t3 = t2 * t;
    float r = 1.0;
    float g = clamp((-0.2661 + 0.4744*t - 0.0749*t2 + 0.00447*t3), 0.0, 1.0);
    float b = clamp((-0.3310 + 0.3149*t - 0.0348*t2 + 0.00144*t3), 0.0, 1.0);
    if (t < 1.45) b = 0.0;
    return vec3(r, g, b);
}
```

Prefer a 64 × 1 RGBA16F LUT over 1000–6000 K; it is one tap and exact.

## 5.2 Emissivity: gas, soot and optical depth

A flame is not a blackbody. What you see is

```
L(λ) = ε(λ) · L_planck(λ, T)
ε    = 1 − exp(−κ · ρ_soot · s)          κ = absorption coefficient, s = path length
```

Practical emissivities (*literature-typical values, from recall; treat as orders of magnitude*):

| Source | ε (visible) | Note |
|---|---|---|
| Clean premixed gas flame (blue) | 0.01 – 0.05 | emits in bands, not as a grey body; do not use blackbody colour |
| Small-arms primary flash | 0.05 – 0.20 | thin, barely luminous in daylight |
| Small-arms secondary flash | 0.15 – 0.40 | larger, brighter |
| Tank/artillery muzzle flash | 0.3 – 0.7 | metres of path length |
| HE fireball, first 50 ms | 0.6 – 0.95 | soot-rich, nearly a blackbody |
| Diesel / fuel fire | 0.5 – 0.9 | |
| Glowing steel fragment | 0.6 – 0.8 | |
| Tracer | 0.8 – 0.95 | solid pyrotechnic |

Soot dominates the visible emission. Gas-phase emission (the CO₂/H₂O bands) is almost all infrared and contributes little to what the eye sees — which is why a clean-burning flash is dim and a soot-laden one is bright at the same temperature.

The useful rendering consequence: **ε tracks optical depth, so ε is highest in the middle of the fireball and falls to zero at the edge.** That, not the temperature field, is what produces the characteristic bright white core with an orange rim. Render it:

```glsl
float tau   = density * thickness;                 // from the flipbook alpha, or a volume
float eps   = 1.0 - exp(-tau * kappa);             // kappa ~ 2.5 - 6.0 for soot
vec3  Lemit = blackbodyHue(T) * Lv_of_T(T) * eps;  // Lv from the table in 5.3
```

## 5.3 How bright: luminance in nits

Computed by integrating Planck's law against the photopic luminosity function V(λ) and multiplying by 683 lm/W. These are for ε = 1; multiply by ε from 5.2.

| T (K) | Luminance L_v (cd/m²) | Radiant exitance σT⁴ (W/m²) | Luminous efficacy (lm/W) |
|---|---|---|---|
| 1000 | 8.6 × 10⁻¹ | 5.7 × 10⁴ | 0.0 |
| 1200 | 4.5 × 10¹ | 1.2 × 10⁵ | 0.0 |
| 1400 | 7.8 × 10² | 2.2 × 10⁵ | 0.0 |
| **1645** | **1.04 × 10⁴** | 4.2 × 10⁵ | 0.1 |
| 1800 | 3.75 × 10⁴ | 6.0 × 10⁵ | 0.2 |
| 2000 | 1.48 × 10⁵ | 9.1 × 10⁵ | 0.5 |
| 2200 | 4.58 × 10⁵ | 1.3 × 10⁶ | 1.1 |
| **2500** | **1.79 × 10⁶** | 2.2 × 10⁶ | 2.5 |
| 2800 | 5.28 × 10⁶ | 3.5 × 10⁶ | 4.8 |
| 3000 | 9.63 × 10⁶ | 4.6 × 10⁶ | 6.6 |
| 3200 | 1.63 × 10⁷ | 5.9 × 10⁶ | 8.6 |
| 3500 | 3.23 × 10⁷ | 8.5 × 10⁶ | 11.9 |
| 4000 | 8.06 × 10⁷ | 1.5 × 10⁷ | 17.4 |
| 5000 | 2.92 × 10⁸ | 3.5 × 10⁷ | 25.9 |

Validation: a candle flame burns at roughly 1800 K with a soot emissivity near 0.3, giving 3.75 × 10⁴ × 0.3 ≈ 1.1 × 10⁴ nits, which matches the commonly quoted ~10⁴ cd/m² for a candle flame.

Reference luminances for calibration:

| Scene element | Luminance (cd/m²) |
|---|---|
| Solar disc at noon | 1.6 × 10⁹ |
| Sunlit white paper (90 % albedo, 100 klx) | 2.9 × 10⁴ |
| Clear north sky | 8 × 10³ |
| Overcast sky | 2 × 10³ |
| Interior, lit office, white surface | 1 × 10² |
| sRGB display white | 1 × 10² |
| Full moon | 2.5 × 10³ |
| Moonlit grass | 3 × 10⁻² |

## 5.4 Exposure: why the core goes white

In a physically-based exposure pipeline the scene luminance mapping is

```
L_max = 1.2 · 2^EV100                  the luminance that maps to 1.0 before tone mapping
pixel = L_scene / L_max
EV100 = log2( L_avg · 100 / 12.5 )     for an average-metered auto exposure
```

| Scene | EV100 | L_max (nits) |
|---|---|---|
| Bright sunlight | 15 | 39,300 |
| Overcast day | 12 | 4,900 |
| Deep shade / interior by day | 9 | 614 |
| Interior, lit | 6 | 77 |
| Dusk | 3 | 9.6 |
| Moonlight | −2 | 0.3 |

Now compute how far over the clipping point each source is. `stops over = log2(ε·L_v / L_max)`:

| Source (with ε applied) | L_v (nits) | Daylight EV15 | Overcast EV12 | Interior EV6 | Night EV−2 |
|---|---|---|---|---|---|
| Rifle primary flash, 1645 K, ε 0.12 | 1.2 × 10³ | −5.0 | −2.0 | +4.0 | +12.0 |
| Rifle secondary flash, 2500 K, ε 0.25 | 4.5 × 10⁵ | +3.5 | +6.5 | +12.5 | +20.5 |
| Tank muzzle flash, 2400 K, ε 0.55 | 7.5 × 10⁵ | +4.3 | +7.3 | +13.3 | +21.3 |
| HE fireball core, 2800 K, ε 0.85 | 4.5 × 10⁶ | +6.8 | +9.8 | +15.8 | +23.8 |
| Tracer, 2200 K, ε 0.9 | 4.1 × 10⁵ | +3.4 | +6.4 | +12.4 | +20.4 |
| Glowing fragment, 1300 K, ε 0.7 | 1.6 × 10² | −7.9 | −4.9 | +1.0 | +9.0 |

Read the table as a set of design rules:

1. **A rifle's primary flash is invisible in daylight.** It is 5 stops *under* the clipping point and smaller than a pixel at any realistic range. The flash you see in daylight footage is the secondary flash and the ejected unburnt powder. Games that draw a large bright flash in daylight are wrong twice: too big and too bright.
2. **Every combustion source clips to white at night**, by 9 to 24 stops. The orange hue survives only at the optically thin rim, where ε drops. This is the whole visual signature.
3. **Auto exposure must react.** A 20-stop source entering the frame will drag an average-metered exposure down and the world will go black. Use a histogram-based metering that rejects the top 2–4 % of pixels, and clamp the per-frame EV rate of change (see 5.6).
4. **The hue you author is only seen at the rim.** Authoring a white-hot core is redundant: write the physical radiance and let the exposure make the core white.

## 5.5 What number to write into the emissive channel

```
emissive_linear_RGB = blackbodyHue(T) · (ε · L_v(T) / L_max_reference) · brightnessTrim
```

If the renderer works in **absolute nits**, write `ε · L_v(T) · blackbodyHue(T)` directly and let exposure do everything. That is the correct pipeline and the one to prefer.

If the renderer works in **relative units with 1.0 = sRGB white**, pick a reference exposure (daylight EV15, `L_max` = 39,300 nits) and pre-divide:

| Source | T (K) | ε | Emissive value to write (linear, relative to EV15 white) |
|---|---|---|---|
| Rifle primary flash | 1645 | 0.12 | (0.032, 0.006, 0.000) |
| Rifle intermediate flash disc | 1800 | 0.20 | (0.19, 0.043, 0.000) |
| Rifle secondary flash | 2500 | 0.25 | (11.4, 4.3, 0.78) |
| Tank muzzle flash core | 2400 | 0.55 | (18.6, 6.5, 1.0) |
| Artillery fireball, t = 5 ms | 3000 | 0.90 | (220, 107, 34) |
| Artillery fireball, t = 60 ms | 2300 | 0.80 | (12.8, 4.3, 0.5) |
| Artillery fireball, t = 200 ms | 1700 | 0.50 | (0.33, 0.07, 0.00) |
| Tracer 7.62 | 2200 | 0.90 | (10.5, 3.3, 0.29) |
| Ember, hot | 1300 | 0.70 | (0.0075, 0.0005, 0.000) |
| Ember, dying | 1050 | 0.60 | (0.00008, 0.0000, 0.000) |

These are large numbers. An RGBA8 particle buffer cannot hold them; the emissive path must be **RGBA16F or R11G11B10F**, with R11G11B10F capping at 65,504 and losing the blue channel's precision (5-bit mantissa) — acceptable for fire, where blue is near zero, and the reason R11G11B10F is the usual HDR particle format.

## 5.6 Exposure control around flashes

```
HISTOGRAM AUTO EXPOSURE, flash-safe

1  Build a 256-bin log-luminance histogram of the scene at 1/8 resolution.
2  Discard the bottom 40 % and the TOP 4 % of weighted pixels before averaging.
   The top-4 % rejection is what stops a muzzle flash closing the iris.
3  EV_target = log2(L_avg * 100 / 12.5) + exposureCompensation
4  Rate-limit:   EV += clamp(EV_target - EV, -dDown*dt, +dUp*dt)
       dDown (adapting to dark) = 0.6 - 1.2 stops/s      slow, like the eye
       dUp   (adapting to light) = 2.0 - 4.0 stops/s     fast
5  Hard-clamp EV to the level's authored range (e.g. [-3, 16]) so a single
   effect can never black out the scene.
6  Optional, and worth it for weapons: freeze adaptation for 150 ms after a
   player-weapon discharge, then resume. The flash is shorter than the eye's
   response and should not move the exposure at all.
```

Without step 2 and step 6, firing at night produces the familiar wrong behaviour: the world dims for half a second after every shot.

## 5.7 Bloom

A clipped source needs bloom, or it looks like a flat white decal. Physically, bloom is the camera's or eye's point-spread function, which has a wide, low tail, not a Gaussian.

```
Use a sum of 4-6 Gaussians with radii in a geometric series, weighted as ~1/r:
    radii (fraction of screen height): 0.004, 0.011, 0.028, 0.071, 0.18, 0.45
    weights:                           0.40,  0.24,  0.16,  0.10, 0.07, 0.03
Total energy added should be 0.4 - 2 % of the source, not 20 %.
Threshold: NONE. Thresholded bloom is a hack that makes mid-tones flat and
          makes bright things pop discontinuously. Let the wide tail of a
          physical PSF do the work on an unthresholded HDR buffer.
```

A 20-stop source will produce a large visible glow from a 0.4 % energy transfer, because 0.004 × 10⁶ is still 4000× the display white. That is the correct behaviour and is what makes night firing read.

Anamorphic streaks, lens ghosts and a 6-point star are *camera* artefacts. They belong only if the game's conceit is a camera. The human eye's PSF produces a soft halo and faint radial streaks from the lens suture lines, not a clean hexagon.

## 5.8 Temperature as a function of time

All combustion effects are a cooling curve. One model serves them all:

```
T(t) = T_amb + (T_0 - T_amb) · (1 + t/tau)^(-n)
```

| Effect | T₀ (K) | τ (s) | n | Visible until |
|---|---|---|---|---|
| Rifle primary flash | 1700 | 0.0004 | 1.2 | 2 ms |
| Rifle secondary flash | 2500 | 0.0010 | 1.0 | 5 ms |
| Tank muzzle flash | 2400 | 0.006 | 0.9 | 35 ms |
| Artillery HE fireball | 3200 | 0.020 | 0.85 | 250 ms |
| 250 kg bomb fireball | 3300 | 0.060 | 0.80 | 900 ms |
| Fuel/diesel fire | 1400 | — | 0 (steady) | until fuel ends |
| Ember from a fire | 1350 | 0.9 | 0.6 | 4 – 12 s |
| Ejected steel fragment | 1100 | 1.5 | 0.5 | 2 – 8 s |
| Tracer (pyrotechnic, steady) | 2200 | — | 0 | burn time |

`n < 1` gives a long tail, which is right: a fireball's luminosity collapses fast (because `L_v` goes roughly as `T^8` in this range, not `T⁴` — see below) while its temperature decays slowly.

**Why luminosity collapses so fast.** Over 1600–3000 K the visible band is far out on the Wien tail, so the *luminous* exitance rises much faster than `T⁴`. Fitting the computed table in 5.3 over 1600–3000 K:

```
L_v(T) ∝ T^8.1          (empirical fit to the computed values, 1600-3000 K)
```

So a drop from 3000 K to 2000 K is a factor of 65 in brightness, not 5. This single fact is why real explosions flash brilliantly and then go dark almost immediately, while game explosions glow for a second and look fake.

## 5.9 Dynamic lights from combustion

The emissive surface must also light the world. Convert the surface to a point or sphere light:

```
Phi = pi^2 · d^2 · eps · M_e(T) · eta_v          [lumens]
    d        = fireball diameter (m)
    M_e(T)   = sigma·T^4 (W/m^2), from the table in 5.3
    eta_v    = luminous efficacy (lm/W), from the table in 5.3
    the pi^2 d^2 factor = (pi d^2) surface area x pi sr, for a Lambertian sphere

I = Phi / (4 pi)                                  [candela, for a point light]
```

Worked values:

| Source | d (m) | T (K) | ε | Φ (lm) | I (cd) | Illuminance at 10 m (lx) |
|---|---|---|---|---|---|---|
| Rifle muzzle flash | 0.15 | 2200 | 0.2 | 6.6 × 10⁴ | 5.2 × 10³ | 52 |
| Tank muzzle flash | 2.5 | 2400 | 0.55 | 6.4 × 10⁷ | 5.1 × 10⁶ | 5.1 × 10⁴ |
| 155 mm fireball at peak | 7.0 | 3000 | 0.9 | 1.4 × 10⁹ | 1.1 × 10⁸ | 1.1 × 10⁶ |
| Grenade fireball at peak | 2.0 | 2800 | 0.85 | 5.6 × 10⁷ | 4.5 × 10⁶ | 4.5 × 10⁴ |
| Campfire | 0.5 | 1300 | 0.6 | 4.0 × 10² | 32 | 0.32 |

For comparison, full daylight is about 10⁵ lx. A tank muzzle flash at 10 m is half of daylight; a 155 mm burst at 10 m is ten times daylight. Both should visibly relight the scene.

Practical lighting rules:

| | Very Low | Low | Medium | High | Ultra High |
|---|---|---|---|---|---|
| Combustion lights live at once | 1 | 2 | 4 | 8 | 16 |
| Shadow-casting combustion lights | 0 | 0 | 0 | 1 (nearest, 1 cascade) | 2 |
| Light update rate | on spawn only | 10 Hz | 20 Hz | per frame | per frame |
| Radius culling | 8 m | 15 m | 30 m | 60 m | 120 m |
| Injected into volumetric fog | no | no | no | yes | yes |

A combustion light's radius should come from its intensity and a cut-off illuminance, not from a hand-authored number:

```
r_cut = sqrt(I / E_cut)              E_cut = 0.05 lx at night, 2 lx by day
```

That is the only correct way to make a light cull at a distance where it genuinely stops mattering, and it automatically makes flashes huge at night and local in daylight — which is exactly how they behave.

## 5.10 What games typically get wrong

| Error | Physical correction |
|---|---|
| Flash authored as pale yellow or white | 1600–2800 K is `#FF7A00`–`#FFB260`. The white comes from clipping, not from the source. |
| Flash the same brightness day and night | Emit absolute radiance; let exposure decide. A rifle's primary flash is invisible in daylight. |
| Fireball glows for a second at even brightness | `L_v ∝ T^8.1`. Peak brightness lasts 10–40 ms, then collapses. |
| Bloom thresholded and tinted | Unthresholded, multi-radius, ~1 % energy. |
| Auto exposure dips after every shot | Reject the top 4 % in metering; freeze adaptation 150 ms around own-weapon fire. |
| Emissive stored in RGBA8 | R11G11B10F minimum; RGBA16F for anything with meaningful blue. |
| Dynamic light radius hand-set | `r = sqrt(I / E_cut)`. |
| Blue gas flame given a blackbody colour | Premixed flames emit in bands (CH, C₂ Swan); use a measured spectrum or author directly. |
| Smoke drawn as emissive | Smoke is not emissive. It scatters. See chapter 07. |

## Rules of thumb

- Combustion in weapons is 1600–3000 K and therefore orange. Write the radiance, not the colour.
- Brightness goes as `T^8` in this range, not `T⁴`. Everything flashes and dies.
- ε follows optical depth, so the core is dense and clips while the rim shows the hue. That is the whole look.
- Compute dynamic-light intensity from temperature, size and emissivity. Never type a candela value.
- An HDR source 20 stops over white needs no threshold to bloom.
- Never let a weapon effect move the auto exposure.

## Sources

- Blackbody chromaticity table: computed here by integrating Planck's law against the analytic multi-lobe fits to the CIE 1931 2° colour-matching functions of Wyman, Sloan and Shirley, "Simple Analytic Approximations to the CIE XYZ Color Matching Functions", JCGT 2(2), 2013, then XYZ → linear Rec.709 at D65. Validated against the published Planckian chromaticities at 5778 K and 6500 K.
- Luminance table: computed from the same Planck integration weighted by V(λ) × 683 lm/W. Validated against the commonly quoted ~10⁴ cd/m² for a candle flame.
- EV100 / `L_max = 1.2 · 2^EV100` and the exposure-metering relationship: volume 1, chapter 02 (saturation-based sensitivity).
- The `L_v ∝ T^8.1` exponent is an empirical fit to the computed luminance table over 1600–3000 K, made here.
- Emissivity values are recalled literature-typical ranges and are labelled as estimates. Soot-dominated visible emission in hydrocarbon and propellant flames is standard combustion-science material.
- Cooling-curve constants (T₀, τ, n) are authored fits chosen to match the observed durations in chapters 06 and 07; they are not measurements.
- Bloom radii, weights and energy fractions are authored values consistent with the physically-based bloom practice described in volume 1, chapter 10.
