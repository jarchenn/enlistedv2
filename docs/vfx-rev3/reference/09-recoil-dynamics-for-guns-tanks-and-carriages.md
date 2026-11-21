# 09. Recoil dynamics for guns, tanks and carriages

Most games animate recoil as a damped harmonic oscillator: an impulse, a decaying sinusoid, back to rest. That is correct for a rifle against a shoulder and **wrong for every artillery piece and tank gun**, because a gun's recoil mechanism is not a spring. It is a hydraulic brake designed to hold the resisting force nearly constant over the stroke, followed by a much slower gas-spring return. The displacement curve is a parabola out and a parabola back, strongly asymmetric, not a sinusoid. This chapter gives the physics, the measured strokes, and the implementation.

## 9.1 Momentum

```
p = m_p · v_p + β · m_c · v_p          [N·s]

    m_p   projectile mass (kg)          m_c   propellant charge mass (kg)
    v_p   muzzle velocity (m/s)         β     effective gas-momentum factor, 1.25 - 1.50
                                              (use 1.30; it is the Résal/Le Duc convention)
V_0 = p / m_r                           free recoil velocity of the recoiling mass
E_r = ½ m_r V_0²                        recoil energy, absorbed by the brake
```

`m_r` is the **recoiling mass**: tube, breech, and the piston — not the vehicle. Published values: CN120-26 1995 kg, CN120-25 1830 kg, L11/L30 1778–2000 kg.

## 9.2 Measured recoil strokes

| Gun | Calibre | Recoil stroke `L` | Recoiling mass | Mechanism |
|---|---|---|---|---|
| **M256** (M1 Abrams) | 120 mm | **279 mm** (one source) / 304.8 mm (another) | ~1905 kg | hydro-pneumatic |
| Rheinmetall Rh-120 L/55 | 120 mm | ~300 mm | ~1950 kg | hydro-pneumatic |
| **L11 / L30** (Challenger) | 120 mm | **370 mm** | 1778–2000 kg | hydro-pneumatic, two buffers |
| CN120-26 (Leclerc) | 120 mm | **400 mm** | 1995 kg | recoil effort at trunnions 550 kN (APFSDS at +51 °C) |
| CN120-25 (G1) | 120 mm | **485 mm** | 1830 kg | recoil effort at trunnions 365 kN |
| M68 (M60, M1 early) | 105 mm | **305 mm** | — | concentric recoil spring, no separate buffer/recuperator |
| CN 105 F1 (AMX-30) | 105 mm | **385 mm** | — | two opposed hydraulic brakes + one hydro-pneumatic recuperator |
| CN 105-57 (SK-105) | 105 mm | **368 mm** | — | single cylinder + single hydro-pneumatic recuperator |

**The detail that matters most for animation: the M256 recoils about 38 mm *before the projectile leaves the tube*.** The barrel is already moving when the muzzle flash appears. A recoil animation that starts on the flash frame is late by 2–3 ms and, more importantly, by 14 % of the stroke. Sequence it the other way around.

## 9.3 The correct displacement curve

A hydraulic recoil brake meters oil through a variable orifice specifically so that the resisting force is nearly constant — that is what minimises the peak load at the trunnions. Constant force means constant deceleration.

```
RECOIL PHASE  (gun moving back)
    a      = V_0² / (2L)                    constant deceleration
    x(t)   = V_0 t − ½ a t²
    v(t)   = V_0 − a t
    t_r    = 2L / V_0                       time to full stroke
    F_R    = m_r · a                        force at the trunnions

COUNTER-RECOIL PHASE  (recuperator pushing the gun forward)
    a_cr   = f · a,  with f ≈ 0.025 - 0.06  (the recuperator is weak by design)
    x(t')  = L − ½ a_cr t'²
    t_cr   = sqrt(2L / a_cr)                t_cr / t_r = 1 / sqrt(f) ... typically 4-7x
    final  0.1 L of travel is taken by a counter-recoil buffer; decelerate
           smoothly to zero over that last stretch or the gun will "clack" home.
```

| Gun | L (m) | V₀ (m/s) | a (m/s²) | a (g) | t_recoil (ms) | f | t_counter (ms) | Cycle (ms) | F_R (kN) |
|---|---|---|---|---|---|---|---|---|---|
| M256 120 mm | 0.280 | 16.9 | 509 | 52 | **33** | 0.030 | 191 | **225** | 970 |
| Rh-120 L/55 | 0.300 | 17.9 | 533 | 54 | 34 | 0.030 | 194 | 227 | 1039 |
| L30 120 mm (HESH) | 0.370 | 9.2 | 114 | 12 | 81 | 0.035 | 431 | 511 | 202 |
| 2A46M 125 mm | 0.300 | 11.1 | 204 | 21 | 54 | 0.030 | 313 | 367 | 511 |
| M68 105 mm | 0.305 | 15.1 | 373 | 38 | 41 | 0.030 | 234 | 274 | 478 |
| Pak 40 75 mm | 0.920 | 11.7 | 74 | 8 | 158 | 0.040 | 788 | 946 | 52 |
| ZiS-3 76 mm | 0.900 | 10.0 | 55 | 6 | 180 | 0.040 | 902 | 1082 | 29 |
| Bofors 40 mm | 0.200 | 5.2 | 67 | 7 | 77 | 0.060 | 316 | 393 | 15 |

Sanity check: the computed `F_R` of 970 kN for the M256 over a 280 mm stroke sits in the right range against the published 550 kN at the trunnions for the CN120-26 over its longer 400 mm stroke; longer stroke, lower force, same momentum. Larger `m_p v_p` with a shorter stroke gives a larger force.

**Read the table as animation data.** A tank gun snaps back in 33 ms (2 frames at 60 fps) and takes 190 ms to come home. A towed 76 mm field gun takes 180 ms back and nearly a second to return. That asymmetry — a crack back and a slow glide home — is the whole character of large-gun recoil and a symmetric spring cannot produce it.

```glsl
// Exact recoil displacement, drop-in. Returns metres of rearward travel.
float gunRecoilX(float t, float L, float V0, float f) {
    float a   = V0 * V0 / (2.0 * L);
    float tr  = 2.0 * L / V0;
    if (t <= 0.0) return 0.0;
    if (t < tr)  return V0 * t - 0.5 * a * t * t;
    float acr = f * a;
    float tcr = sqrt(2.0 * L / acr);
    float t2  = t - tr;
    if (t2 >= tcr) return 0.0;
    // last 10 % of travel: blend to a smooth buffered stop
    float x = L - 0.5 * acr * t2 * t2;
    float s = smoothstep(0.0, 0.10 * L, x);
    return x * s;
}
```

## 9.4 Firing sequence, in order

For a 120 mm tank gun. Times relative to primer ignition.

| t (ms) | Event |
|---|---|
| 0 | primer; chamber pressure rises |
| 0.5 – 7 | projectile travels the bore; **the gun has already recoiled ~38 mm when the projectile exits** |
| 7 | muzzle exit. Muzzle flash begins (chapter 06). Gun is at 38 mm and at near-peak velocity |
| 7 – 40 | recoil stroke completes: 280 mm at t ≈ 40 ms |
| 10 – 45 | blast sphere and ground dust disc develop |
| 15 – 60 | hull pitch builds (9.5) |
| 40 – 231 | counter-recoil: the gun glides forward |
| 225 – 235 | counter-recoil buffer: the gun seats, with a small visible jolt |
| 230 – 400 | breech opens, case ejects (if cased), bore evacuator begins to purge |
| 400 – 1500 | evacuator puff at the bulge and the muzzle |
| 60 – 2500 | hull oscillation decays at 1.2 Hz |

Animating the gun's motion from the formula and everything else from this table removes the most common error, which is a flash and a recoil that start on the same frame and end on the same frame.

## 9.5 What the vehicle does

The recoil impulse goes into the hull once the brake has transferred it. Treat the hull as a rigid body on a suspension.

```
Δv_hull  = p / M_vehicle                           rearward translation
ω_pitch  = p · h / I_pitch                         h = trunnion height above the CG
I_pitch  ≈ M · (0.35 · L_hull)²                    radius of gyration ≈ 0.35 of hull length
θ_peak   ≈ ω_pitch / (2π f_n)                      f_n = suspension pitch frequency
```

| Platform | p (kN·s) | M (t) | Δv (m/s) | h (m) | I_pitch (kg·m²) | ω (rad/s) | f_n (Hz) | Peak pitch |
|---|---|---|---|---|---|---|---|---|
| M1A2 / M256 | 32.2 | 62.0 | 0.52 | 1.50 | 474,000 | 0.102 | 1.2 | **0.77°** |
| T-72B / 2A46M | 27.7 | 44.5 | 0.62 | 1.30 | 260,000 | 0.139 | 1.3 | 0.97° |
| M60A3 / M68 | 19.3 | 48.0 | 0.40 | 1.50 | 280,000 | 0.103 | 1.2 | 0.79° |
| **Pak 40 towed** | 8.2 | 1.43 | 5.75 | 0.55 | 1,790 | 2.52 | 2.6 | **8.9°** |
| ZiS-3 towed | 5.2 | 1.12 | 4.66 | 0.50 | 1,230 | 2.11 | 2.7 | 7.1° |
| M113 / 25 mm | 1.1 | 12.3 | 0.09 | 1.10 | 36,200 | 0.033 | 1.6 | 0.19° |

A tank pitches about **0.8–1.0°** and slides about 2–5 cm. That is small and it must stay small: a tank that rocks 5° when firing reads as a toy. A towed anti-tank gun pitches **7–9°** and visibly hops — but it does not pitch freely, because the trail spades are dug in.

### Hull motion model

```
On fire:
    body.applyImpulse(-boreAxis * p, atWorldPos = trunnionPosition)
Let the existing rigid-body + suspension solver produce the motion. If there
is no such solver, integrate a 2-DOF model:

    heave:  z'' + 2 ζ_z ω_z z' + ω_z² z = 0,   ω_z = 2π·1.4 Hz,  ζ_z = 0.35
    pitch:  θ'' + 2 ζ_θ ω_θ θ' + ω_θ² θ = 0,   ω_θ = 2π·1.2 Hz,  ζ_θ = 0.30
    initial conditions: θ'(0) = ω_pitch from above, z'(0) = Δv·sin(elevation)

Tracked vehicles are heavily damped (torsion bars + rotary dampers): ζ = 0.25-0.40.
Wheeled vehicles on soft tyres are lighter: ζ = 0.15-0.25, f_n 1.6-2.2 Hz.
Settling to visually zero takes 3/(ζ ω) ≈ 1.1 - 1.6 s.
```

### Gun elevation changes the split

```
p_horizontal = p · cos(elev)      -> translation + pitch
p_vertical   = p · sin(elev)      -> heave; the hull squats
At 0 deg elevation: all pitch. At 45 deg: 71 % each. At high angle (mortars,
howitzers at charge 7): mostly heave, and the trails/spades take it.
```

### Towed carriages

A towed gun is not a free rigid body. The trail spades anchor the rear.

```
1  If the spades are set (after the first shot has bedded them), the carriage
   rotates about the spade contact line, not about its CG:
       I_spade = I_cg + M·d²            d = distance CG to spade line
   This roughly triples I and reduces the pitch to 2-4 deg - but the wheels
   lift clear of the ground by 30-120 mm, which is the recognisable "hop".
2  The first shot from an unbedded position displaces the whole gun rearward
   by 0.2 - 0.8 m and throws a spray of soil from the spades. Subsequent
   shots move it only a few centimetres. Model the bedding as a per-gun
   state: displacement *= exp(-shotCount / 1.5).
3  The spades dig: write into the deformation buffer (chapter 04, section 4.9)
   a pair of trenches growing with shot count.
4  A muzzle brake removes 25-50 % of the impulse into the hull by redirecting
   gas rearward; scale p by (1 - brakeEfficiency) for the hull, but NOT for
   the recoiling mass before the brake.
```

## 9.6 Small arms

Here the damped-spring model **is** right, because the resisting element is a shoulder, a hand and a buffer spring, not a metered orifice.

| Weapon | m_p (g) | v_p (m/s) | m_c (g) | p (N·s) | Weapon mass (kg) | V_free (m/s) | E_free (J) | Shoulder travel (mm) |
|---|---|---|---|---|---|---|---|---|
| 5.56×45 M4 | 4.0 | 920 | 1.7 | 5.71 | 3.1 | 1.84 | 5.3 | 34 |
| 7.62×51 M14 | 9.5 | 850 | 3.0 | 11.39 | 4.5 | 2.53 | 14.4 | 57 |
| 7.62×39 AK-47 | 7.9 | 715 | 1.6 | 7.14 | 4.3 | 1.66 | 5.9 | 36 |
| 7.62×54R PKM | 9.6 | 825 | 3.1 | 11.24 | 7.5 | 1.50 | 8.4 | 43 |
| 12.7×99 M2HB | 42.0 | 890 | 15.2 | 54.97 | 38.0 | 1.45 | 39.8 | 94 |
| .300 Win Mag | 11.7 | 900 | 4.5 | 15.79 | 4.1 | 3.85 | 30.4 | 82 |
| 12 ga slug | 28.4 | 500 | 2.3 | 15.70 | 3.6 | 4.36 | 34.2 | 87 |
| 9×19 pistol | 8.0 | 380 | 0.35 | 3.21 | 0.9 | 3.57 | 5.7 | 36 |

Shoulder travel assumes an effective coupled mass of 3× the weapon and a 3000 N/m shoulder stiffness; it is an authored model, not a measurement, but the *ratios* are physical.

```glsl
// Damped harmonic recoil for a hand-held weapon. Re-triggerable: impulses add.
// state: x (m), v (m/s).  Call once per fixed step.
void stepRecoil(inout float x, inout float v, float dt, float f_n, float zeta) {
    float w = 6.2831853 * f_n;
    float a = -w*w*x - 2.0*zeta*w*v;
    v += a * dt;
    x += v * dt;
}
// on fire:  v += p / m_effective;

// Per-class values:
//   pistol      f_n 7.0 Hz   zeta 0.45
//   carbine     f_n 6.0 Hz   zeta 0.50
//   battle rifle f_n 5.0 Hz  zeta 0.50
//   LMG bipod   f_n 4.5 Hz   zeta 0.60
//   HMG mount   f_n 3.5 Hz   zeta 0.70
//   sniper      f_n 4.0 Hz   zeta 0.40
```

Six degrees of freedom, not one. Real recoil is mostly rearward translation plus a **rise** (because the bore axis is above the shoulder/grip pivot) plus a small roll.

```
Per shot, drawn from the weapon's profile:
    back   = p / m_eff                                   along -bore
    rise   = back · (h_bore_above_pivot / r_grip) · k     k ≈ 0.6 - 1.0
    yaw    = back · yawBias + N(0, yawSigma)              yawBias from stock/grip geometry
    roll   = back · rollBias                              handedness; ±2-6°
Typical:  h_bore_above_pivot 0.04 - 0.08 m,  r_grip 0.22 - 0.30 m
```

### Automatic fire

```
- Impulses ADD into the same spring state. Do not reset.
- Settling time 3/(ζ ω). At 6 Hz and ζ 0.5 that is 160 ms, so above
  ~375 rpm the weapon never returns to rest and recoil accumulates. That
  accumulation IS the climb; do not add a separate scripted climb curve.
- Add a small per-shot random yaw so the pattern is not a straight line,
  and a slow drift so long bursts wander. Keep the first 2-3 shots nearly
  deterministic: players learn the first-shot behaviour.
- Bolt-carrier cycling is a SECOND impulse, 8-25 ms after the shot and
  opposite in sign, when the carrier hits the rear of the receiver. It is
  what gives a gas gun its characteristic double-bump. Amplitude 0.15-0.35
  of the main impulse.
```

## 9.7 Camera

Camera response must be a *filtered* version of the weapon's motion, never a copy.

```
camera_pitch = lowpass(weapon_rise, cutoff 8-14 Hz) · couplingFactor
    couplingFactor:  hip fire 0.55,  aimed 0.30,  braced/bipod 0.18,  prone 0.15
Optical sights move with the weapon exactly (they are bolted to it); the
VIEW through them does not need extra shake. Adding camera shake on top of
sight motion double-counts and makes aiming feel mushy.

For nearby large guns (tank, artillery) the camera takes a SEPARATE impulse
from the blast, not from the recoil:
    amplitude ∝ sqrt(Δp(Z))     (chapter 07, section 7.2)
    frequency 2-6 Hz, duration 0.3-1.2 s, random direction with a radial bias
```

The spring/impulse approach is preferred over authored curves for the default arsenal because it stacks naturally during automatic fire and needs almost no authoring; keep authored curves for a few signature weapons where the exact feel matters, layered on top of a short spring.

## 9.8 Secondary motion that sells it

| Element | Timing | Note |
|---|---|---|
| **Barrel whip** | during and after the stroke | a tank barrel 5.3 m long deflects visibly; first bending mode ~8–15 Hz, amplitude 10–40 mm at the muzzle, decaying over 0.5–1.5 s. Animate as a bend along the tube, not a rigid rotation. |
| **Mantlet / gun shield shudder** | 0–200 ms | small, 2–6 mm |
| **Dust shed from the vehicle** | 5–60 ms | every horizontal surface of the firing vehicle sheds accumulated dust. One of the most recognisable real details. |
| **Suspension travel** | 15–600 ms | road wheels visibly compress on the near side |
| **Case ejection** | 230–500 ms (tank), 20–60 ms (small arms) | hot case, ejects with a tumble, rings when it lands |
| **Crew reaction** | 0–400 ms | the gunner's body absorbs the hull motion with a lag of 60–120 ms |
| **Spade soil spray** | 0–200 ms (towed) | from under the trails |
| **Blast-flattened grass** | 0–300 ms | the radial wind impulse of chapter 08, section 8.8 |
| **Bore evacuator puff** | 400–1500 ms | chapter 06, section 6.3 |

Barrel whip is the highest-value of these and is usually missing. A 5.3 m tube decelerating at 52 g does not stay straight.

```
// Bend the barrel along its length. s in 0..1 from breech to muzzle.
// First cantilever bending mode shape, approximated:
float mode(float s) { return s*s * (3.0 - 2.0*s) * 0.5 + s*s*s*s * 0.5; }
offset = up * A * mode(s) * sin(2π f_whip t) * exp(-t / τ_whip);
    A ≈ 0.010 - 0.040 m,  f_whip 8 - 15 Hz,  τ_whip 0.15 - 0.4 s
```

## 9.9 Budgets

| Item | Very Low | Low | Medium | High | Ultra High |
|---|---|---|---|---|---|
| Gun recoil model | 2-key animation | parabolic formula | parabolic + buffer | parabolic + buffer + whip | + mantlet, + tube thermal sag |
| Pre-exit recoil (38 mm) | no | no | yes | yes | yes |
| Hull response | none | pitch only, scripted | 2-DOF spring | full rigid body + suspension | + per-wheel contact |
| Barrel whip | no | no | no | yes | yes |
| Dust shed from the vehicle | no | no | 6 sprites | 16 sprites | 30 sprites |
| Spade soil spray (towed) | no | no | yes | yes | yes + deformation write |
| Case ejection | no | yes (no physics) | physics, 1 bounce | physics, pooled, sound | + heat shimmer on the case |
| Small-arms DOF | 1 (back) | 2 (back, rise) | 4 (+yaw, roll) | 6 | 6 + bolt-carrier second impulse |
| Bolt-carrier impulse | no | no | no | yes | yes |
| Camera coupling | fixed scalar | fixed scalar | stance-dependent | stance-dependent + lowpass | + crew-body lag |
| Blast camera shake | no | yes | yes | yes | yes |
| Suspension visual travel | no | no | yes | yes | yes |

## 9.10 Common errors

| Error | Correction |
|---|---|
| Tank recoil as a damped sinusoid | Constant-force brake: parabola out in 33 ms, parabola home in 190 ms. |
| Recoil and counter-recoil take the same time | 4–7× longer to return. |
| Recoil starts on the muzzle-flash frame | The gun has already moved 38 mm when the projectile exits. |
| Tank rocks 3–5° | 0.8–1.0°. Towed guns are the ones that hop, at 7–9°. |
| Towed gun pivots about its CG | It pivots about the dug-in spades, and the wheels lift. |
| First shot from a new position behaves like the tenth | Unbedded spades let the gun move 0.2–0.8 m. |
| Barrel stays rigid | 5.3 m of tube at 52 g whips 10–40 mm at 8–15 Hz. |
| Automatic fire uses a scripted climb curve | Accumulate impulses in one spring; the climb is emergent. |
| Camera shake added on top of sight motion | The sight is bolted to the weapon. Do not double-count. |
| No bolt-carrier second impulse | 8–25 ms later, opposite sign, 0.15–0.35 amplitude. That is the "feel" of a gas gun. |
| Vehicle sheds no dust when firing | Every horizontal surface does, and it is instantly recognisable. |
| Muzzle brake reduces the recoiling mass's travel | It reduces the impulse into the *mount*, not the tube's stroke before the brake acts. |

## Rules of thumb

- Guns: parabola out, slow parabola home, ratio 4–7. Rifles: damped spring.
- 33 ms back, 190 ms home, for a 120 mm tank gun. 180 ms back, 900 ms home, for a towed 76 mm.
- The gun is already 38 mm into its stroke when the flash appears.
- A tank pitches under 1°. A towed gun hops 7–9° about its spades.
- Barrel whip, shed dust and the evacuator puff are the three cheap details that make a tank gun read as a machine rather than an animation.
- In automatic fire, add impulses into one spring state and let the climb emerge.
- Couple the camera to the weapon by stance, through a low-pass filter, at 0.15–0.55.

## Sources

- Recoil strokes and recoiling masses: M256 304.8 mm (https://en.wikipedia.org/wiki/M256_(tank_gun)) and "the gun tube, breech, and piston recoil approximately 11 inches (279 mm) during firing. The gun recoils about 1.5 inches (38 mm) before the projectile leaves the gun tube" (https://www.inetres.com/gp/military/cv/weapon/M256.html); L11/L30 370 mm, hydro-pneumatic with two buffers, 1778–2000 kg (Forecast International archive); CN120-26 400 mm, 1995 kg, 550 kN at the trunnions (https://en.wikipedia.org/wiki/CN120-26); CN120-25 485 mm, 1830 kg, 365 kN (https://en.wikipedia.org/wiki/CN120-25); M68 305 mm with a concentric recoil spring (https://en.wikipedia.org/wiki/M68_tank_gun); CN 105 F1 385 mm with two opposed brakes and one hydro-pneumatic recuperator (https://en.wikipedia.org/wiki/CN_105_F1); CN 105-57 368 mm (https://en.wikipedia.org/wiki/CN_105-57).
- Spring/impulse recoil as a damped harmonic oscillator that stacks naturally during automatic fire and needs almost no authoring, versus hand-authored curves for signature weapons: https://www.strayspark.studio/blog/weapon-recoil-camera-shake-ue5-spring-vs-curve
- Momentum with the gas factor `β` (Résal / Le Duc convention) and the constant-force design intent of a hydraulic recoil brake are standard ordnance-engineering material.
- Projectile masses, muzzle velocities, charge masses, vehicle masses, trunnion heights, radii of gyration, suspension frequencies and damping ratios are nominal published or estimated values; the derived accelerations, times, forces, pitch angles and all cost figures are computed from them here and are **not** measurements. The computed M256 trunnion force (970 kN over 280 mm) is sanity-checked against the published CN120-26 figure (550 kN over 400 mm).
- Barrel whip mode shape, amplitudes and frequencies; shoulder stiffness and coupled-mass model; bolt-carrier impulse ratios; and camera coupling factors are authored values.
