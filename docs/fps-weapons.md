# FPS weapon handling, ballistics, environment and defences

This covers the update requested in `claude_instructions.md` (from `m1_garand_claude_package_v3.zip`). That guide is written for Unity or Unreal; this engine is our own C++20 / OpenGL 4.5 / Jolt code, so each part maps to a module here.

| Guide term | Here |
|---|---|
| WeaponHandler | `src/viewmodel.{h,cpp}`, plus the drawing and firing code in `src/weapons.cpp` |
| Weapon camera / layer | The first-person pass in `Renderer::render`: depth cleared, own projection |
| BallisticsManager | `src/ballistics.cpp` (openings, penetration) and `src/station.cpp` (`stepProjectiles`) |
| Terrain deformation | `Terrain::crater` (`src/terrain.cpp`), `App::deformGround` (`src/ground.cpp`) |
| Foliage / tree state machine | `src/foliage.cpp`, leaf shaders in `src/render.cpp` |
| Clouds | Sky shader in `src/render.cpp` |
| BuilderController | `src/builder.cpp` |
| PlayerController | `src/explore.cpp` (input, movement), `src/viewmodel.cpp` (look-driven sway) |

All numbers below are game tuning unless a source is given.

## 1. Imported weapon models (GLB pipeline)

**Sources.** The user supplied five GLB files. They stay outside git in `assets_src/weapons/`. Each is baked once:

```bash
NormandyMapmaker.exe --import-weapon-models assets_src/weapons content
```

**Reader.** `src/model.cpp` is a small GLB reader built on nlohmann/json. The build is offline, so cgltf could not be fetched. It handles:
- the node hierarchy, with TRS or matrix transforms;
- skins in bind pose: vertex = Σ wᵢ · (jointWorldᵢ · inverseBindᵢ) · v (the Thompson);
- KHR spec-gloss converted to metal-rough: metallic = 1 when specular luminance > 0.5, roughness = 1 − glossiness;
- one PBR texture set per model;
- the first animation, sampled at 96 steps.

**Import transform.**

```
X = T(offset) * S(realLength / L) * R(rot)
```

- L is the extent along z of `lengthNode`, measured after rotation. The whole scene is not used, because the Garand's casing sits beside the rifle.
- The result puts the butt at z = 0, the muzzle at +z and the top at +y, centred in x and y. This is the convention the box parts already use.
- Textures are box-filtered down to 1024² (`maxTex`). Albedo is saved as JPEG 92; metal-rough and normal maps as PNG.
- The MG 42 and MP 40 pack ambient occlusion in the red channel of the metal-rough map.

**Output.** `content/weapons/models/<dir>/` holds `mesh.bin`, `model.json` and the textures. If the folder is missing, the weapon falls back to its box parts. Pass `--box-weapons` to force the fallback.

**Animation.** Each part stores a weapon-space delta per sample: `X · W(t) · W_rest⁻¹ · X⁻¹`. The Garand's 6.33 s clip drives the reload: the clip seats, the follower rises, and the bolt and operating rod cycle. Reload progress 0–1 maps onto that clip.

**Calibration.** `--weapon-sheets <dir>` writes orthographic side and rear views of every weapon on a centimetre grid. The rear sight is marked red, the front sight green, the muzzle blue, and the hand sockets yellow and cyan. The sight points in each weapon JSON were measured from these sheets and from vertex slices of the baked mesh. For example, the Garand's rear aperture centre is (−1.2, 111, 306) mm and its front post tip is (−0.8, 113, 1069) mm.

| Weapon | GLB | Triangles | Parts → roles |
|---|---|---|---|
| M1 Garand, M1C | realistic_m1_garand.glb | 14 026 | Bolt → bolt, Charging_Handle → handle, Clip and Bullet → clip, Casing and ClipEmpty hidden |
| M1 Thompson | low-poly_thompson_m1a1.glb | 3 893 | skin joints bolt_01 → bolt, mag_02 / bullet(s) → mag (factor-only materials) |
| MP 40 | mp40.glb | 5 659 | zamek and pokretlo → bolt, magazynek → mag |
| MG 42 | mg42.glb | 15 978 | single mesh |
| M1918A2 BAR (new) | browning_m1918a2_bar.glb | 20 043 | single material, body |

## 2. Viewmodel rendering and ADS

**No clipping.** The weapon and arms are drawn after the world, over a cleared depth buffer, so they never sink into walls. They are lit at their real world position.

**Fixed weapon FOV.** The weapon uses its own projection, `vmProj = perspective(60°, aspect, 0.01, 6)`. The world FOV narrows from 72° to 58° in ADS, or to 72/mag for scopes, while the weapon keeps its 60°. The screen centre is invariant under this, so ADS alignment holds at any world FOV.

**Apparent positions.** Effects such as the muzzle flash are drawn with the world projection. To match the barrel drawn at 60°, their view-space x and y are scaled by `tan(worldFov/2) / tan(vmFov/2)` (`App::vmApparent`).

**ADS alignment** (`sightFrame`, `vmTarget` in `viewmodel.cpp`):

```
L = normalize(F - R)                    R rear aperture, F front post tip (weapon space)
Q = Ry(180) * rotation(L -> +z)         weapon +z onto view -z
T_ads = (0, 0, -relief) - Q * R         relief 0.21 m (irons), 0.07 m (scopes)
T_hip = T_ads + (0.12, -0.07, 0.06)
```

**Spring.** The weapon offset follows `mix(T_hip, T_ads, smoothstep(adsT))` on a critically damped spring: x″ = ω²(target − x) − 2ωx′ with ω = 18/s. It settles to within 0.5 mm in about 0.6 s with no overshoot.

**Sight-origin ballistics.** When adsT > 0.85, the round starts at the world-space front sight tip F and travels along `V = normalize(F − R)`. The usual dispersion draw and the declared-zero tilt are added on top. A wall between the eye and F still stops the shot. From the hip, the existing sweep from the muzzle is unchanged. Because the sights are taken from the posed weapon, sway and breathing move the sight picture and the shot together.

## 3. Sway, bob, breathing and depth of field

| Effect | Formula / value |
|---|---|
| Sway | Target = (0.006 · Δyaw, −0.006 · Δpitch) m and (1.6 · Δyaw, −1.6 · Δpitch) deg. Δ is the look delta in degrees per step, clamped to ±0.6. Smooth-damped (Gems 4 critically damped approach, 0.12 s). Scaled ×0.35 in ADS, ×0.3 with reduced motion. Rotates about the rear sight. |
| Bob | φ += π · v / 1.5 · dt; y = 12 mm · k · sin(2φ), x = 8 mm · k · sin(φ); k = min(v / 4.6, 1) · (1 − ADS) · (0.5 when crouched) · grounded |
| Breathing | Pitch = A · sin(2π · 0.22 · t), yaw = A/2 · sin(π · 0.22 · t). A = 0.08°: ×0.5 while moving, ×2 during recovery. It moves the camera and the sights, so the shot follows. |
| Hold breath | Left Shift while in ADS. Sprint is ignored when aiming. The breathing phase freezes for up to 5 s, then a forced 3 s recovery follows at double amplitude. |
| Depth of field | Only first-person pixels write eye distance (an R16F attachment). Focus is relief + \|F − R\| + 3 cm. CoC = ADS · clamp((focus − d) / (0.6 · focus), 0, 1) · 6 px at 1080p. A 24-tap scatter-as-gather lets the blurred rear sight bleed over the background, while the front post and target stay sharp. Turned off by reduced motion and in the scope view. |

## 4. IK arms

**Sockets.** `"sockets": {"rightGrip": [...], "leftSupport": [...]}` are given in weapon space, one pair per weapon. During a magazine or clip reload, the support hand travels to the feed.

**Two-bone IK.** Analytic, with shoulders at view (±0.17, −0.24, 0.02), upper arm 0.32 m and forearm 0.30 m:

```
d = clamp(|T - S|, |a - b|, a + b),  cos(alpha) = (a^2 + d^2 - b^2) / (2 a d)
u = (T - S)/|T - S|,  n = normalize(P - u (u . P)),  P = down + 0.4 outward
E = S + u a cos(alpha) + n a sin(alpha),  H = S + u d
```

Out of reach, the arm straightens and does not stretch. Sleeves are OD wool for US weapons and field grey for German ones. The hands are blocks aligned with the weapon, so they stay below the sight line in ADS.

## 5. Surface damage and penetration

Openings keep a per-copy session overlay (`InstanceRT::leafSt` / `leafHits`).

| Opening | HP (bullet hits; shells are instant) | Break |
|---|---|---|
| Window glass | 1 | Glass mesh hidden; an empty sash no longer slows rounds; shard burst; `glass_break` |
| Shutter | 2 | Becomes a Jolt dynamic body (7 kg). Impulse I = V_bullet · 0.01 kg at the hit, plus a hinge-tear push of 0.6 m/s away from the building. Mesh swaps to a fractured copy with a gap at the hit plank. |
| Wooden door | 10 (`"hp"` on the interactable overrides) | Dynamic body (28 kg). Angular impulse τ = (P_hit − hinge) × I. |
| Steel door / flap | — | Never breaks; stops rounds |

**Debris.** At most 24 loose leaves exist at once; past that the oldest freezes in place as a static body. HE within 1.6 R breaks glass, and HE within R blows off shutters and doors.

**Penetration.** This applies to small arms through glass, shutters and doors.

```
E = m v^2 / 2   (bulletMass per weapon JSON: .30-06 9.7 g -> 3529 J, .45 ACP 14.9 g -> 605 J, 9 mm 8 g -> 640 J)
dT from a back-cast against the same body;  P_exit = P_hit + V_dir * dT
E_new = E_old - mu * dT,  mu = 20 kJ/m glass, 40 kJ/m wood;  v_new = v * sqrt(E_new / E_old)
```

Measured in the env suite:
- A .30-06 through a 50 mm door: 3529 − 40000 × 0.05 = 1529 J.
- A .45 stops in a 35 mm shutter.
- Walls, terrain, trunks, sandbags and steel always stop the round. Canopies have no collision, so foliage costs nothing.

## 6. Craters

```
Z_new(x, z) = Z_old - D_max * (max(0, 1 - d^2 / R^2))^p,   p = 2
```

| Profile | D_max | R | Peak wall | Used by |
|---|---|---|---|---|
| Standard | 0.5 m | 1.5 m | ~27° | 7.5 cm HE (`deformGround`) |
| AT | 1.2 m | 3.0 m | ~31° (measured) | PaK 40 PzGr. 39 |

**AT depth.** You chose to scale the guide's 2.5 m AT depth down so the crater stays climbable. The player's controller limit is 50°, and the env suite walks the player out of an AT crater. Accumulated depth is capped at 1.6 m. The old rim term was removed, so the bowl is exactly the formula.

**PaK 40 AT round.** X at the gun toggles the next round between HE and AT; it takes effect at the next reload.
- The gun starts with 4 AT rounds (`reserveAT`). Ammunition crates hold `75mm_ap` (6 in the PaK crate, 2 in the mixed crate).
- AT hits walls with concentrated damage: 320 at 0.9 m radius, against 120 at 2.2 m for HE.

## 7. Trees and clouds

**Assets.** `tree_apple` (orchard, about 4.5 m), `tree_oak` (hedgerow, about 11 m) and `tree_poplar` (about 16 m) are generated by `tools/gen_env_assets.py`:
- Trunks and limbs are box parts.
- Canopies are spheres filled with alpha-tested leaf-cluster cards (deterministic per asset). Each card carries a mask M: 0 at the trunk axis, rising to 1 at the outer tips.

**Sway.** In the vertex shader, for both the colour and shadow passes:

```
V_offset = D_wind * M * A * sin(f t + k_x X_world + k_z Z_world)   D = (0.8, 0, 0.6), A = 0.10, f = 1.3, k_x = 0.21, k_z = 0.17
```

A second, smaller harmonic is added. Sway shares the View → Grass → Wind toggle.

**Felling.**
- HP_bullet = 30 small-arms hits. HP_AT = 1: an AT or HE strike within 1.5 m of the trunk axis, or a direct hit.
- At 0, the static tree becomes a static stump cut at 0.4 m, plus a dynamic trunk compound. The trunk carries the canopy and topples away from the hit (impulse high on the trunk).
- Grass is excluded only around the trunk.
- Applying stores `extra.tree = {felled, trunk pose}`.

**Clouds.**
- fBm (5 octaves of value noise) on a plane at 1500 m, sampled along the view ray, about 600 m cells, scrolling with time.
- Coverage moves the threshold (`Renderer::cloudCover`, default 0.5). Bases are grey and thin edges get a silver lining toward the sun.
- They draw in the background sky only; reflections keep the clear-sky gradient. Non-finite values are guarded.

## 8. Buildable defences

**Assets.**

| Asset | Description | HP |
|---|---|---|
| `def_tank_trap` | Czech hedgehog: three perpendicular 1.4 m angle-iron beams on three feet | 1500 |
| `def_sandbag_straight` | 2 m × 0.6 m × 0.8 m, five courses | 300 |
| `def_sandbag_curved` | 45° arc on a 2.5 m radius | 300 |

**Placement in Explore** (`B`):
- **Keys:** 1 tank trap, 2 sandbags (toggles straight and curved), Q cycles, R or the wheel rotates by 15°, LMB places, RMB or B cancels. The weapon is lowered while building.
- **Ray:** from the screen centre to 5 m, terrain only. θ = acos(N · up) from the hit normal; θ > 35° is invalid.
- **Other checks:** water, overlap (Jolt box query) and the build area.
- **Hologram:** the ghost pass, tinted green when valid and red when not.

**Snapping.** Each sandbag has two end sockets {p, t} with outward tangents. If the aim point is within 0.6 m of a free socket, the new piece is yawed so `R(yaw) · t_new = −t_existing`, and its socket is placed on the existing one. The env suite measures a 0.0 mm gap and tangent continuity of 1.00000, and curved pieces chain too. Creative placement uses the same snap and the same slope rule.

**Session overlay.** Explore placements carry `extra.session`. F5 removes them, and "Apply session changes to map" (in Explore) keeps them as one undo step.

**Defence physics.**
- **Tank trap:** small-arms multiplier 0, so 100 rifle hits leave 1500/1500. HE and AT use the normal falloff. `blocksVehicles` is written to the instance; the engine has no vehicles or navigation, so the flag is data for a future vehicle system.
- **Sandbags:** stop every round (1 HP per small-arms hit). At 0 HP they slump into a low spill about 0.25 m high.
- **Blast occlusion:** `blastOcclusion(epicentre, target)` casts a ray. If it crosses a sandbag collider the factor is 0.1; other solid cover counts the same.
- **Player exposure:** taken at the chest and the head (half each), within 3 × the blast radius. The player has no health model, so this is a HUD readout ("Blast exposure X (raw × m)"), camera shake and a tinnitus tone.

## Controls added

| Key | Action |
|---|---|
| Left Shift (in ADS) | Hold breath (5 s) |
| X (PaK 40 gunner) | Next round HE / AT |
| B | Build mode (1 / 2 / Q / R / wheel / LMB / RMB) |

## Tests

- `--selftest-fps`: 60 checks covering import fit and textures, ADS alignment for every weapon at three FOVs, the spring, sight-origin ballistics, sway, bob, breathing and hold-breath, sprint in ADS, IK residuals and elbows, depth of field, hip and ADS stills for every weapon, the Garand reload clip, and shader compilation.
- `--selftest-env`: 42 checks covering glass, shutters, pistol against a shutter, the door and penetration energy, the debris cap, both crater profiles against the formula, slopes, walking out of an AT crater, the PaK AT round, trees by bullets and by shell, the trunk toppling, clouds with zero non-finite pixels, tank trap immunity, sandbags stopping a round, blast occlusion 0.1 against 1.0, the builder slope threshold either side of the measured θ, snap gap and tangent, curved chaining, F5 discard, Apply, and Ctrl+Z.

## Known limits

- The arms are procedural blocks; no arm model was supplied.
- The BAR and MG 42 are single meshes, so their magazine and belt have no separate moving part. The magazine is shown only through the IK hand and the reload dip.
- The Thompson source has no textures; its colours come from material factors.
- Penetration is limited to openings. Walls stay impenetrable to small arms, as in the destruction update.
- Clouds don't appear in reflections, and they cast no shadows.
- The reference images in the package were placeholders and were never supplied.
