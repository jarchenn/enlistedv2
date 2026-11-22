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
| Kar 98k ZF39 | kar98k_aria.glb (ARIA, CC-BY-4.0, [credits](credits.md)) | 9 767 | Kar98k_Bolt_1 and Kar98k_Bolt_2 → bolt (lift −80° about the bore, `boltPivot`), Kar98k_Scope → scope, Body, Trigger and Belt (sling) → body; `lengthNode` Kar98k_Body |

## 2. Viewmodel rendering and ADS

**No clipping.** The weapon and arms are drawn after the world, over a cleared depth buffer, so they never sink into walls. They are lit at their real world position.

**Fixed weapon FOV.** The weapon uses its own projection, `vmProj = perspective(60°, aspect, 0.01, 6)`. The world FOV narrows from 72° to 58° in ADS, or to 72/mag for scopes, while the weapon keeps its 60°. The screen centre is invariant under this, so ADS alignment holds at any world FOV.

**Apparent positions.** Effects such as the muzzle flash are drawn with the world projection. To match the barrel drawn at 60°, their view-space x and y are scaled by `tan(worldFov/2) / tan(vmFov/2)` (`App::vmApparent`).

**ADS alignment** (`sightFrame`, `vmTarget` in `viewmodel.cpp`):

```
L = normalize(F - R)                    R rear aperture, F front post tip (weapon space)
Q = Ry(180) * rotation(L -> +z)         weapon +z onto view -z
T_ads = (0, 0, -relief) - Q * R         relief 0.21 m (irons), 0.07 m (scopes)
T_hip = (0, 0, -0.21) - Q * R + hipOffset   hipOffset (0.12, -0.07, 0.06) unless the weapon sets ads.hipOffset
```

**One ADS blend.** `adsT` moves toward the held state at 1 / `adsTime_s` per second (`content/settings/movement.json`, default 0.25 s; it is the only writer of `adsT`). With t = smoothstep(adsT), three things follow it together:
- the weapon pose target `mix(T_hip, T_ads, t)`;
- the world FOV `mix(72°, ads.worldFov, t)` (58° unless the weapon sets one);
- the weapon FOV `mix(60°, ads.vmFov, t)` when a weapon sets one.

Releasing aim half-way reverses from the current blend. Recoil and breathing never write the FOV, so a held aim keeps a constant zoom. Before this update the world FOV stepped from 72° to 58° in one frame at adsT 0.85: that step was the "zoom jump". Scoped weapons still switch to the optic (eyepiece view, 72°/mag) at adsT 0.85; see [Optic eyepiece](#eyepiece).

**Spring.** The weapon offset follows that target on a critically damped spring: x″ = ω²(target − x) − 2ωx′ with ω = 18/s. It settles to within 0.5 mm in about 0.6 s with no overshoot.

**Sight-origin ballistics.** When adsT > 0.85, the round starts at the world-space front sight tip F and travels along `V = normalize(F − R)`. The usual dispersion draw and the declared-zero tilt are added on top. A wall between the eye and F still stops the shot. From the hip, the existing sweep from the muzzle is unchanged. Because the sights are taken from the posed weapon, sway and breathing move the sight picture and the shot together.

<a id="calibration"></a>
### Per-weapon sight calibration

`sights.rear`, `sights.front` and `muzzle` in the weapon JSON are the **measured** anchors on the baked mesh:
- **rear:** the centre of the aperture, or the bottom of the notch.
- **front:** the tip of the front post.
- **muzzle:** the centre of the bore at the barrel's end.

An optional `ads` block refines the pose without moving or scaling any mesh:

```json
"muzzleDir": [0, 0, 1],
"ads": { "eyeRelief": 0.186, "vmFov": 0, "posCorrection": [0, 0, 0], "rotCorrection": [0, 0, 0] }
```

| Field | Meaning | Default |
|---|---|---|
| `eyeRelief` | Eye to rear sight in ADS (m). Smaller values give a larger sight picture: the rear sight's apparent size scales with 1/relief. | 0.21 |
| `vmFov` | Weapon-projection FOV while aiming (deg), blended in with the ADS spring. 0 keeps the fixed 60°. | 0 |
| `posCorrection` | View-space translation added to `T_ads` (m). | 0 |
| `rotCorrection` | View-space rotation about the rear sight (deg: pitch, yaw, roll), applied as `Q = C · Q`. | 0 |
| `muzzleDir` | Bore axis at the muzzle (weapon space), for the flash direction. | +z |

With zero corrections, R and F lie exactly on the view axis. The hip pose is computed from the shared 0.21 m relief, so a weapon's ADS calibration never moves its hip pose.

**How the anchors were measured.**
- The baked `mesh.bin` was rasterised as a rear view (looking down +z) at 0.05–0.25 mm per pixel, restricted to a z slab and, where possible, a material part.
- Enclosed holes give the aperture and bore centres. The topmost pixels give the post tip and the bottom of the notch. Side views (z against y) locate each sight along the barrel.
- The scripts are not shipped. The method is described here so it can be repeated.

**FG 42 (measured this pass).**
- The bore sits at x −0.0356, not 0. The side magazine widens the bounding box that the baker centred on.
- The old anchors were at x 0 with the rear 8 mm too low. The ADS line therefore ran 36 mm beside the real sights and was yawed and pitched against them.

| Anchor | Old | Measured |
|---|---|---|
| rear (diopter ring centre, 2.6 mm hole) | (0, 0.088, 0.300) | (−0.0356, 0.0961, 0.3105) |
| front (hooded post tip; hood centre y 0.1018) | (0, 0.100, 0.855) | (−0.0356, 0.1012, 0.8544) |
| muzzle (bore centre, +z) | (0, 0.030, 0.975) | (−0.0356, 0.0441, 0.975) |
| hand sockets R / L | x 0 | x −0.0356 (y, z unchanged) |
| `ads` | n/a | relief 0.21, no FOV override, no corrections |

**MG 42 (measured this pass).**
- The bore sits at x −0.0292.
- The rear sight is a raised leaf with a Y-shaped top whose V-notch bottom is at y 0.1114. The old anchor (y 0.087) sat on the leaf's stem, 24 mm low, and at x 0, which put the Y above and to the right of the dot.
- This mesh has the front post folded flat on the jacket (z 1.01–1.075, only a few mm proud), so nothing upright can be seen through the notch. The sight line therefore runs from the notch parallel to the bore (straight +z), and the front anchor is a point on that line.

| Anchor | Old | Measured |
|---|---|---|
| rear (V-notch bottom) | (0, 0.087, 0.620) | (−0.0292, 0.1114, 0.6177) |
| front (on the bore-parallel line) | (0, 0.087, 1.050) | (−0.0292, 0.1114, 1.050) |
| muzzle (bore centre, +z) | (0, 0.034, 1.220) | (−0.0292, 0.0335, 1.220) |
| hand sockets R / L | x 0 | x −0.0292 (y, z unchanged) |
| `ads.eyeRelief` | 0.21 | **0.186**: the Y appears 0.21/0.186 = 12.9 % larger |

<a id="w1"></a>
**Complete update, part 2 (W1): six more weapons, measured the same way on the supplied GLBs.** Every change is anchor data, pose / projection data, or a bake-time edit of the supplied mesh (`model.meshEdits`, below). No geometry was added.

| Weapon | Anchor | Old | Measured / new |
|---|---|---|---|
| Kar98k | rear | (0, 0.082, 0.600) | (−0.0316, 0.0880, 0.585): shoulder line over the 1.2 mm deep V-notch (bottom y 0.0868). The barleycorn tip sits level with the shoulders, centred in the V (*gestrichen Korn*). |
| | front | (0, 0.081, 1.085) | (−0.0316, 0.0880, 1.093), barleycorn tip |
| | muzzle, sockets | x 0 | bore (−0.0316, 0.0661, 1.11); sockets x −0.0316 |
| | `ads` | none | relief 0.20, weapon FOV 42° |
| M1 Carbine | rear | (0, 0.079, 0.330) | (−0.0006, 0.0858, 0.336), centre of the 2.1 mm peep |
| | front | (0, 0.0805, 0.870) | (−0.0006, 0.0745, 0.887), blade tip between the wings |
| | muzzle | (0, 0.060, 0.904) | bore (−0.0006, 0.0613, 0.904) |
| | `ads` | none | relief 0.12, weapon FOV 40° |
| M1 Thompson | rear | (0.003, 0.107, 0.250) | (0.005, 0.1059, 0.258), centre of the 7.5 mm aperture |
| | front | (0.003, 0.1068, 0.805) | (0.005, 0.1068, 0.8008), blade tip |
| | muzzle | (0.003, 0.082, 0.813) | bore (0.005, 0.079, 0.813) |
| | `ads` | none | relief 0.186 (13 % larger), no FOV change |
| M1 Garand | rear | (−0.0012, 0.111, 0.306) | (−0.0008, 0.1111, 0.305), peep centre |
| | front | (−0.0008, 0.113, 1.069) (the wing tops) | (−0.0008, 0.111, 1.069), blade tip |
| | muzzle | (0, 0.086, 1.107) | bore (−0.0008, 0.0861, 1.107) |
| | mesh | peep 2.2 mm | inner ring opened to 3.5 mm at bake; the outer ring (4.4–4.7 mm) and the rest of the mesh are untouched. M1 and M1C share the bake. |
| | `ads` | none | relief 0.15 |
| BAR | rear | (0, 0.112, 0.285), inside the raised ladder | (−0.001, 0.1105, 0.300): eye line just over the folded leaf (front-sight-only) |
| | front | (0, 0.1075, 1.090) | (−0.001, 0.1030, 1.0976), hooded post tip (hood top y 0.1078) |
| | muzzle | (0, 0.079, 1.214) | bore (−0.001, 0.0788, 1.214) |
| | mesh | ladder leaf raised, top y 0.135 (the "tall rectangle") | leaf folded forward 90° onto its base at bake, top y 0.110 |
| | `ads` | none | relief 0.20, weapon FOV 32° |
| Panzerschreck | sight line | (−0.1, 0.06, 1.0 → 1.55), behind the shield | virtual line (−0.28, 0.11, 0.75 → 1.55), 19.6 cm right of and 10 cm above the tube axis, clear of the shield |
| | muzzle | (0, 0, 1.64) | tube axis (−0.0844, 0.0066, 1.64); sockets x −0.0844, left hand back to z 1.05 behind the shield (z 1.30 was out of reach from the right-side pose) |
| | `ads` | none | `hipOffset` (0, −0.06, 0.05), `worldFov` 62 (mild zoom), `virtualLine` |

**Extra `ads` fields** (all optional):

| Field | Meaning | Default |
|---|---|---|
| `hipOffset` | View-space offset of the hip pose from the ADS line (m) | (0.12, −0.07, 0.06) |
| `worldFov` | World FOV at full ADS (deg) | 58 |
| `virtualLine` | rear / front are a view line beside the weapon, not its sights. Aimed rounds then leave the muzzle toward the aim point instead of the front sight. | false |

**Weapon FOV (`vmFov`) versus eye relief.** Relief scales only the rear sight (apparent size ∝ 1/relief). A narrower weapon FOV magnifies the whole drawn weapon about the screen centre while the world keeps its own FOV, so it is framing, not zoom. The Carbine and BAR need both, because their sights are millimetre-sized parts a metre from the eye.

<a id="w2"></a>
**MP & Creative update (W2): StG 44 and M3, measured the same way.** Both are now in the calibrated set (`--selftest-sights`, 10 weapons).

| Weapon | Anchor | Old | Measured / new |
|---|---|---|---|
| StG 44 | mesh | the GLB carries a 12.8° cant about the bore (top leaning right, seen from behind) | `model.rot` [-7.3, 180, **-12.77**]: the roll is removed at bake, so the hip pose, ADS and the third-person weapon are level (`--import-weapon-models`). The supplied mesh is also yawed 1.45° about y; bore and sight line agree, so ADS absorbs it and it shows a little of the left side (charging handle). |
| | rear | (−0.0317, 0.16, 0.5): on the canted mesh, beside the notch | (−0.0014, 0.16275, 0.466): shoulder line over the 2 mm U-notch of the tangent leaf (notch bottom y 0.16145) |
| | front | (−0.0317, 0.1616, 0.904): 12 mm beside the post | (−0.01275, 0.1637, 0.9155): pointed post tip inside the hood (hood top y 0.1726) |
| | muzzle, sockets | canted bake | bore (−0.01355, 0.1045, 0.94); sockets carried through the same roll (rigid transform) |
| | `ads` | none | relief 0.18, weapon FOV 42° (the rear sight block fills the top centre) |
| M3 | rear | (0, 0.193, 0.115): 1 mm right of and 1.7 mm below the aperture | (−0.00095, 0.19146, 0.115): centre of the visible opening of the 5.7 mm peep; the leaf is 8 mm thick, and down the 1.1° sight line the opening centre is 0.16 mm above the hole axis |
| | front | (0, 0.1843, 0.485) | (−0.00065, 0.1843, 0.487): centre of the 2.1 mm flat blade top |
| | `ads` | none | relief 0.12, weapon FOV 40° (the ring about 70 px across at 1080p) |

**Acceptance sweep (spec 8.3).** `--selftest-sights` aims every weapon at 1920×1080, 2560×1440 and 3440×1440. The renderer size is forced for the sweep, because the OS clamps a window to the desktop's work area. Two checks:
- **Image:** the measured front tip is within 1 px of the centre pixel and the aperture or notch within 2 px. This covers the calibrated iron sights.
- **Analytic, every weapon including the scopes:** both anchors project within 0.5 px, and the roll of the drawn weapon is under 0.05°.

The carried M2HB has no sights; it deploys. The pixel results are in `report_sights.txt` and `summary_sights.json` ("sweep").

**Dev overlay.** With *Sight calibration markers* on (Loadout, L), `src/sight_overlay.cpp` adds:
- a gapped magenta crosshair on the camera centre;
- lines from the centre to the projected rear and front points;
- a readout of the rear and front offsets (px), the weapon's roll (deg) and the framebuffer size. It turns red outside 1 px front, 2 px rear or 0.05° roll.

It works for whichever weapon is in hand.

<a id="mesh-edits"></a>
**Bake-time mesh edits** (`model.meshEdits` in the weapon JSON, applied by `--import-weapon-models`):
- Each edit selects the connected triangle islands of the supplied mesh that lie entirely inside `islandsIn` (weapon space, after the import transform). Islands are joined through shared triangles and coincident positions.
- An edit then does one of three things:
  - re-poses the islands: `deg` about `axis` through `pivot`, then `translate`;
  - drops them (`hide`);
  - opens a hole (`radial`): vertices closer than `rMax` to the z-parallel axis through `centre` are pushed outward by `scale`.
- The bake log names every edit and counts the vertices it touched.
- Weapons sharing a model folder (M1 / M1C) must carry the same model block. The importer reads the JSONs in sorted order and warns on a mismatch.

**Hip-fire flash and tracers.** These use the original effects, restored at the user's request: the flash and smoke are placed at `vmApparent(muzzle) + aimDir·0.05`, and tracers and streaks are drawn along the true projectile path from the true start. The corrected `muzzle` anchors still move the flash onto the FG 42 and MG 42 barrels, because both effects start from the anchor. `muzzleDir` is parsed but currently unused.

**Debug markers.** Loadout (L) → *Sight calibration markers* draws:
- the rear anchor (cyan ring) and the front tip (green cross), both through the weapon projection
- the muzzle (orange square)
- where the flash appears through the world projection (white dot)
- the aim point (red ring)

A readout gives the pixel errors, relief and weapon FOV. Self-test screenshots also carry the markers and the centre dot.

<a id="eyepiece"></a>
### Optic eyepiece (sniper scopes and the AT gun sight)

**Scopes (ZF39, M1C).** At adsT > 0.85 the world renders at 72° / magnification, and the weapon **stays drawn**. Its scoped pose puts `scope.eye` (the centre of the ocular lens) on the view axis at `scope.relief` (default 0.07 m; the ZF39 uses 0.06 m, so its 49 mm ocular bell rings the eyepiece circle). `App::scopeView()` turns the eyepiece on. Previously the model was hidden and an ImGui mask covered the screen.

**Passes** (`Renderer::render`, `kEyepieceFS`):
1. The world is drawn as usual.
2. **Eyepiece composite** into `dofTex`, copied back to `hdrColor`:
   - inside a circle of radius `App::kScopeEyepieceR` (0.35 of the frame height, so 70 % across), the sharp, magnified world with a slight edge falloff and the reticle ink;
   - outside it, the world blurred heavily (16 taps on a coarse `hdrColor` mip), darkened to 3.5 % (linear; about 20 % on screen), with a black tube rim just past the circle.
3. **The view model**, without the IK arms while scoped (the support arm read as a flat plank beside the eyepiece).
4. **Eyepiece restore**, blended `ONE, SRC_ALPHA`: the inside of the circle comes back from the composite, so the weapon shows only around the eyepiece (tube rim, receiver, bolt, stock below it). Outside, the weapon is shaded to 60 % (55 % of that toward the corners).

**Exposure.** The exposure reads the `hdrColor` mips made before the eyepiece, so the dark surround does not brighten the view.

**Reticles** are drawn in the shader, so they appear in screenshots:
- ZF39 (`de_absehen1`): a pointed post from below (its tip is the aim point) and heavy side bars with an open centre;
- M1C (`us_post_cross`): a fine crosshair with a tapered lower post.

`scopeIronsMode` (V) still switches the M1C to its irons, with no eyepiece.

**AT gun sight (PaK 40, Flak).** `App::gunnerOptic()` uses the same pass with radius `kGunnerEyepieceR` (0.44, which matches the 8° field of the ZF 3x8), a near-black surround (0.6 % linear) and a TZF-style reticle:
- a big chevron, its tip the aim point;
- a row of three small range chevrons each side, one stadia unit (R / 4) apart;
- a vertical post below;
- three dotted range arcs over the upper field.

ImGui adds the readout at the bottom right of the eyepiece, for example "Shell AP · Traverse +3.2° · Elevation +12 mil" (6400 mil per turn). It also shows the traverse and elevation stop warnings.

**Tests.**
- `--selftest-sights` (`NMK_SIGHTS_ONLY=kar98k_zf39`): the ZF39 part map and anchors; for ZF39 and M1C, weapon pixels under the eyepiece, the reticle centred within 1.5 px, a clear eyepiece against a darker surround; irons mode.
- `--selftest-fps`: scoped weapons stay drawn, with the scope eye on the centre.
- `--selftest-pak`: the `41_pak_gunner_sight` fixture, plus the `41_pak_gunner_sight_ui.png` shot with the readout.

## 3. Sway, bob, breathing and depth of field

| Effect | Formula / value |
|---|---|
| Sway | Target = (0.006 · Δyaw, −0.006 · Δpitch) m and (1.6 · Δyaw, −1.6 · Δpitch) deg. Δ is the look delta in degrees per step, clamped to ±0.6. Smooth-damped (Gems 4 critically damped approach, 0.12 s). Scaled ×0.35 in ADS, ×0.3 with reduced motion. Rotates about the rear sight. |
| Bob | φ += π · v / 1.5 · dt; y = 12 mm · k · sin(2φ), x = 8 mm · k · sin(φ); k = min(v / (0.8 · sprint), 1) (0.8 · sprint was the fixed 4.6 m/s before the ×1.1 speed change) · (1 − ADS) · (0.5 when crouched) · grounded |
| Breathing | Pitch = A · sin(2π · 0.22 · t), yaw = A/2 · sin(π · 0.22 · t). A = 0.08°: ×0.5 while moving, ×2 during recovery. It moves the camera and the sights, so the shot follows. |
| Hold breath | Left Shift while in ADS. Sprint is ignored when aiming. The breathing phase freezes for up to 5 s, then a forced 3 s recovery follows at double amplitude. |
| Depth of field | Only first-person pixels write eye distance (an R16F attachment). Focus is relief + \|F − R\| + 3 cm. CoC = ADS · clamp((focus − d) / (0.6 · focus), 0, 1) · 6 px at 1080p. A 24-tap scatter-as-gather lets the blurred rear sight bleed over the background, while the front post and target stay sharp. Turned off by reduced motion and in the scope view. |

### Recoil: no automatic recovery

Each shot adds aim displacement: pitch `pitch × (0.85–1.15) + climb × min(burst, 12)` and a random yaw wander of up to `± yaw`, scaled by the stance (`content/weapons/handling.json` recoil: standing ×1, crouched ×0.85, prone ×0.6; mounted on a deployed bipod: the pre-overhaul values, the old posture factor (prone ×0.2, crouched ×0.7) × the weapon's `recoil.supported`, e.g. MG 42 prone 0.2 × 0.35 = 0.07, with `handling.json` mounted 0.5 only for a bipod weapon without one) and ×0.85 in ADS. Standing still kicks pitch only; while moving the yaw wander is 25 % of the weapon's `yaw`. The climb has no ceiling apart from the ±85° look clamp. While a burst runs, the displacement is held as `recoilP / recoilY` on top of the camera's own pitch and yaw.

**Dispersion by stance** (gunplay overhaul, 2026-11-08). Each shot leaves inside a cone: the weapon class's hip or ADS half-angle (rifle / sniper 0.15° / 0.03°, auto / lmg 3.0° / 0.45°; `handling.json` `classes`) × the stance (moving 1.0, standing still 0.8, crouched 0.4, prone 0.08, mounted 0.05, sprinting 1.6). The ADS sight-line path uses the same draw. Bots keep their own aim model.

**ADS rate and bolt actions.** The blend runs at `ads.speed / move.adsTime` (StG 44, FG 42, BAR and M3: `ads.speed` 1.2). A Kar98k or ZF39 shot drops out of ADS at once (adsT 0); aiming waits for the bolt and the cooldown, then a held aim key re-enters. The iron-sight Kar98k zooms to `ads.worldFov` 45.

**Tracers.** `tracerEvery` 3 on the MG 42 and BAR, and the MG 42 / M2HB emplacements: a bright red bolt about 6 m long every 3rd round, a short white-yellow streak (about 2 m) on the others (`Fx::bullet`, `Projectile::tracerKind`, the LAN `tr` field).

**Suppression flinch.** A bullet hit on the local player (host: `dmBulletHit`; LAN client: the replicated gore event) moves the aim 0.6–1.2° in a random direction. Like settled recoil, it stays in the aim.

**Nothing pulls it back.** Once the burst ends (no shot for 1.6 cycles + 0.06 s), `App::settleRecoil` folds the displacement into the camera's pitch and yaw in one step. The sight line does not move on that frame: the aim stays where the pattern ended, vertically and horizontally. A weapon swap folds it the same way, so the view never snaps back. The player pulls the mouse down to compensate, during the burst or after it. Until this change the offset decayed back toward the start pitch at `recoil.recover` per second once firing stopped. That field is still in the weapon JSON but is ignored.

Checks (`--selftest-update`): the full FG 42 magazine climbs on every shot; after release the aim drifts less than 0.01° over 2.5 s and the camera pitch has risen by the full climb; pulling down by the climb returns to the start pitch; two 4-round bursts stack; a burst fired while the mouse is pulled down against each shot ends within 0.5° of where it started.

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

## DMG1: bullet damage (reference-sheet rebalance)

`content/ai/damage.json`, user request 2026-11-16: damage close to a Counter-Strike style reference sheet; pistol and SMG headshots must not kill alone. Health stays 100.

- Zones, every gun: head x4, chest / neck / arms x1 (base), stomach (lower torso) x1.25, legs and feet x0.75.
- Class `headMax` caps a head hit: pistols and SMGs 80, so two headshots kill.
- Armour (Battle Royale helmet: head; vest: chest, arms, stomach; legs never): damage x (1 - (1 - pen) x share), share 60 / 80 / 100 % for levels 1 / 2 / 3 (`content/settings/loot.json` armor.penApply). Durability wears by what the armour stopped. Blasts and other non-bullet damage keep the level's flat `reduce`.
- Class penetration: pistol 0.5, SMG 0.62, assault 0.75, battle 0.8, semi / bolt 0.85, sniper 0.9, LMG 0.78, shotgun 0.65, heavy 0.95. Rows may override (AK 0.775, MP7 0.69, .22 0.4).
- Enemy types with their own `head_mult` take base x head_mult on the head, replacing the weapon's x4 or cap (no double multiplication).
- Asserted by `--selftest-rules`.

Damage at 10 m, unarmoured (shotguns: per pellet). Shots to kill (STK): chest at 10 / 50 / 100 / 200 m, head and stomach at 10 m, legs at 10 / 100 m.

| Weapon | Class | Near / far (falloff m) | Head | Stomach | Legs | Pen | STK chest | STK head | STK stomach | STK legs |
|---|---|---|---|---|---|---|---|---|---|---|
| kar98k | bolt | 90 / 90 (0-0) | 360 | 112.5 | 67.5 | 0.85 | 2/2/2/2 | 1 | 1 | 2/2 |
| kar98k_zf39 | bolt | 90 / 90 (0-0) | 360 | 112.5 | 67.5 | 0.85 | 2/2/2/2 | 1 | 1 | 2/2 |
| m1903a4 (data only) | bolt | 95 / 80 (100-150) | 380 | 118.75 | 71.25 | 0.85 | 2/2/2/2 | 1 | 1 | 2/2 |
| m1_garand | semi | 65 / 50 (60-150) | 260 | 81.25 | 48.75 | 0.85 | 2/2/2/2 | 1 | 2 | 3/3 |
| m1c_garand | semi | 65 / 50 (60-150) | 260 | 81.25 | 48.75 | 0.85 | 2/2/2/2 | 1 | 2 | 3/3 |
| g43 (data only) | semi | 65 / 50 (60-150) | 260 | 81.25 | 48.75 | 0.85 | 2/2/2/2 | 1 | 2 | 3/3 |
| m1_carbine | assault | 30 / 23 (40-120) | 120 | 37.5 | 22.5 | 0.7 | 4/4/5/5 | 1 | 3 | 5/6 |
| m1918a2_bar | battle | 40 / 30 (60-200) | 160 | 50 | 30 | 0.8 | 3/3/3/4 | 1 | 2 | 4/4 |
| stg44 | assault | 33 / 25 (100-250) | 132 | 41.25 | 24.75 | 0.75 | 4/4/4/4 | 1 | 3 | 5/5 |
| m1_thompson | smg | 33 / 20 (20-60) | 80 | 41.25 | 24.75 | 0.62 | 4/5/5/5 | 2 | 3 | 5/7 |
| m3_grease_gun | smg | 33 / 20 (20-60) | 80 | 41.25 | 24.75 | 0.62 | 4/5/5/5 | 2 | 3 | 5/7 |
| mp40 | smg | 28 / 17 (15-60) | 80 | 35 | 21 | 0.62 | 4/6/6/6 | 2 | 3 | 5/8 |
| mg42 | lmg | 35 / 27 (60-200) | 140 | 43.75 | 26.25 | 0.78 | 3/3/3/4 | 1 | 3 | 4/4 |
| fg42 | battle | 40 / 30 (60-200) | 160 | 50 | 30 | 0.8 | 3/3/3/4 | 1 | 2 | 4/4 |
| m1919a6 (data only) | lmg | 35 / 27 (60-200) | 140 | 43.75 | 26.25 | 0.78 | 3/3/3/4 | 1 | 3 | 4/4 |
| m2hb_carry | heavy | 120 / 80 (100-200) | 480 | 150 | 90 | 0.95 | 1/1/1/2 | 1 | 1 | 2/2 |
| m1911a1 | pistol | 36 / 21 (10-35) | 80 | 45 | 27 | 0.5 | 3/5/5/5 | 2 | 3 | 4/7 |
| luger | pistol | 34 / 20 (10-35) | 80 | 42.5 | 25.5 | 0.5 | 3/5/5/5 | 2 | 3 | 4/7 |
| walther_p38 (data only) | pistol | 34 / 20 (10-35) | 80 | 42.5 | 25.5 | 0.5 | 3/5/5/5 | 2 | 3 | 4/7 |
| modern_scar | battle | 40 / 30 (150-300) | 160 | 50 | 30 | 0.8 | 3/3/3/3 | 1 | 2 | 4/4 |
| modern_ak | assault | 35 / 27 (120-250) | 140 | 43.75 | 26.25 | 0.775 | 3/3/3/4 | 1 | 3 | 4/4 |
| modern_m4 | assault | 32 / 24 (100-220) | 128 | 40 | 24 | 0.75 | 4/4/4/5 | 1 | 3 | 5/5 |
| modern_m4_irons | assault | 32 / 24 (100-220) | 128 | 40 | 24 | 0.75 | 4/4/4/5 | 1 | 3 | 5/5 |
| modern_m16 | assault | 33 / 25 (120-250) | 132 | 41.25 | 24.75 | 0.75 | 4/4/4/4 | 1 | 3 | 5/5 |
| modern_lmg | lmg | 32 / 24 (150-300) | 128 | 40 | 24 | 0.78 | 4/4/4/4 | 1 | 3 | 5/5 |
| modern_shotgun | shotgun | 28 / 6 (10-40) | 112 | 35 | 21 | 0.65 | 4/17/17/17 | 1 | 3 | 5/23 |
| modern_sniper_bolt | sniper | 105 / 95 (300-600) | 420 | 131.25 | 78.75 | 0.9 | 1/1/1/1 | 1 | 1 | 2/2 |
| modern_sniper_semi | sniper | 79 / 65 (250-500) | 316 | 98.75 | 59.25 | 0.83 | 2/2/2/2 | 1 | 2 | 2/2 |
| modern_pistol | pistol | 34 / 20 (15-40) | 80 | 42.5 | 25.5 | 0.5 | 3/5/5/5 | 2 | 3 | 4/7 |
| modern_ar15 | assault | 32 / 24 (100-220) | 128 | 40 | 24 | 0.75 | 4/4/4/5 | 1 | 3 | 5/5 |
| modern_mp7 | smg | 28 / 17 (40-120) | 80 | 35 | 21 | 0.69 | 4/4/6/6 | 2 | 3 | 5/8 |
| modern_scorpion | smg | 26 / 16 (25-70) | 80 | 32.5 | 19.5 | 0.62 | 4/5/7/7 | 2 | 4 | 6/9 |
| modern_uzi | smg | 27 / 16 (25-70) | 80 | 33.75 | 20.25 | 0.62 | 4/5/7/7 | 2 | 3 | 5/9 |
| modern_shotgun2 | shotgun | 26 / 6 (10-40) | 104 | 32.5 | 19.5 | 0.65 | 4/17/17/17 | 1 | 4 | 6/23 |
| modern_sniper_50 | heavy | 600 / 600 (0-0) | 2400 | 750 | 450 | 0.95 | 1/1/1/1 | 1 | 1 | 1/1 |
| modern_sniper_3 | sniper | 105 / 95 (300-600) | 420 | 131.25 | 78.75 | 0.9 | 1/1/1/1 | 1 | 1 | 2/2 |
| modern_pistol3 | pistol | 30 / 18 (15-40) | 80 | 37.5 | 22.5 | 0.5 | 4/6/6/6 | 2 | 3 | 5/8 |
| modern_ak12 | assault | 33 / 25 (120-250) | 132 | 41.25 | 24.75 | 0.775 | 4/4/4/4 | 1 | 3 | 5/5 |
| modern_fal | battle | 40 / 30 (150-300) | 160 | 50 | 30 | 0.8 | 3/3/3/3 | 1 | 2 | 4/4 |
| modern_pistol2 | pistol | 22 / 13 (15-40) | 80 | 27.5 | 16.5 | 0.4 | 5/8/8/8 | 2 | 4 | 7/11 |

## Mouse sensitivity

Mouse look turns 0.1° per count when running around (72° vertical field of view). Every narrower view uses that rate × **tan(fov / 2) / tan(36°)** (`App::lookScaleFor`, `src/explore.cpp`). The scale applies to the field of view rendered on the previous frame, so ADS blends and the tank sights' RMB zoom change it smoothly.

Why: near the crosshair, a turn of a degrees moves the image by (H / 2) · tan(a) / tan(fov / 2) pixels. Keeping that equal to the hip-fire value gives a_view ≈ a_hip · tan(fov_view / 2) / tan(36°). So the same mouse movement slides a target across the screen by the same distance with iron sights, scopes and tank sights as on foot, instead of turning the same number of degrees (which made 4× scopes and tank sights 3–8 times too fast).

| View | Field of view (vertical) | Look scale | Degrees per mouse count |
|---|---|---|---|
| hip (running around) | 72.0° | 1.000 | 0.1000° |
| Iron sights / ADS (bazooka_m9, fg42, kar98k, kar98k_zf39, m1918a2_bar, m1_carbine, m1_garand, m1_thompson, m1c_garand, m2hb_carry, m3_grease_gun, mg42, mp40, stg44) | 58.0° | 0.763 | 0.0763° |
| Kar98k ZF39 scope 4× | 18.0° | 0.218 | 0.0218° |
| M1C Garand scope 3× | 28.8° | 0.353 | 0.0353° |
| Panzerschreck sight | 62.0° | 0.827 | 0.0827° |
| Sherman sight | 24.0° | 0.293 | 0.0293° |
| Sherman sight, RMB zoom | 10.0° | 0.120 | 0.0120° |
| Sherman hatch | 70.0° | 0.964 | 0.0964° |
| Tiger I sight | 30.0° | 0.369 | 0.0369° |
| Tiger I sight, RMB zoom | 12.0° | 0.145 | 0.0145° |
| Tiger I hatch | 70.0° | 0.964 | 0.0964° |

The self-test (`--selftest-tanks`, "sensitivity") checks every view against the hip value, to within 0.2 %. Crew-served gun hand-wheels (PaK 40, MG emplacements) keep their mechanical rates.

## Controls added

| Key | Action |
|---|---|
| Left Shift (in ADS) | Hold breath (5 s) |
| X (PaK 40 gunner) | Next round HE / AT |
| B | Build mode with the Assets palette: Tab palette (fills the hotbar slot), 1-0 hotbar, RMB place, LMB select, Del remove, wheel / Z / X / R rotate, Ctrl+Z undo, B or Esc leave ([builder.md](builder.md)) |
| Hold Space | Vault a low obstacle, or mantle onto / over a ledge up to 2.1 m ([movement.md](movement.md)) |

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

<a id="mg1"></a>
## MG1: modern guns and the modern weapons case

The user supplied ten first-person arm rigs (`incoming/modern_guns/<folder>`, FBX / one `.blend`) and a hard case GLB. Credits and licences: [credits](credits.md#modern-guns-mg1).

**Conversion.** `tools/modern_guns_config.cjs` writes one config per source; `tools/modern_guns_convert.py` runs in Blender 5.2 in background mode (`blender -b --factory-startup --python tools/modern_guns_convert.py -- <config>.json`). Per source it:
- keeps only the gun meshes (arms, gloves, sleeves, armatures, cameras and animations are dropped), with armatures at rest;
- bakes the world transforms, so the muzzle points down glTF +z;
- splits a skinned gun by its dominant vertex groups where the bolt or magazine has its own bone (the marksman rifle);
- rebuilds each material from the folder's textures at 1K or less: albedo, normal (DirectX maps flipped), and one ORM image (AO, roughness = 1 − smoothness or gloss, metallic; spec-gloss converted: metallic where the specular luminance is over 0.5);
- writes `assets_src/weapons/modern_<name>.glb`, which stays out of git.

`--import-weapon-models assets_src/weapons` then bakes them as usual. These static models are what other soldiers carry, what the case and racks would display, and what the icons are drawn from. In first person each gun uses its source's own arms and clips instead ([animated viewmodels](#mg1-animated)); the static model with the generated arm is the fallback when a rig cannot load.

| Weapon (`id`) | Source folder | Triangles | Textures | Parts → roles |
|---|---|---|---|---|
| FN SCAR 7.62 mm (`modern_scar`) | scar | 16 795 (simplify 0.6 of 28 k) | 1 set, 1K | magazine → mag |
| AK-74 (`modern_ak`) | fps-ak-animated | 8 389 | 1 set, 1K | bolt → bolt; magazine and round → mag |
| M4 carbine, red dot (`modern_m4`) | fps-animated-carbine | 11 363 | 1 set, 1K | bolt → bolt; magazine → mag; charging handle → handle; the red dot's lens is dropped (ADS looks through the tube) |
| M16A3 (`modern_m16`) | animated-assault-rifle | 17 348 (simplify 0.8) | 1 set, 1K | magazine → mag |
| M249 LMG (`modern_lmg`) | lmg-animated | 15 958 (simplify 0.7) | 1 set, 1K | bolt → bolt; belt box, belt and feed cover stay on the body |
| Remington 870 (`modern_shotgun`) | remington-870-shotgun-animated | 6 202 | 1 set, 1K | pump → handle (slides back on each shot); the loose shell is dropped |
| M24 sniper (`modern_sniper_bolt`) | sniper-animated | 11 122 | 1 set, 1K | bolt and bolt rear → bolt (lift about the bore); magazine → mag |
| Marksman rifle, semi (`modern_sniper_semi`) | fps-animations-sniper-rifle | 19 516 | 2 sets, 1K | Bolt / Mag vertex groups → bolt / mag; the hanging sling is dropped |
| Springfield XD (`modern_pistol`) | animatedpistol | 14 362 | 1 set, 1K (from spec-gloss) | slide → bolt; magazine → mag |
| Modern weapons case (`rack_modern`) | hardcase_for_weapons.glb | 14 343 (simplify 0.5 of 28.7 k) | 1 set, 1K | the dirty duplicate is dropped; scaled ×1.27 to 1.06 × 0.40 × 0.20 m |

The second Remington source (`fps-arms-remington-shotgun`) is not used: it is one skinned mesh, and its pump cannot be separated without guessing.

**Defs.** `content/weapons/modern_*.json` have `side` `"modern"` and `"modern": true`. They are only offered by the case: not in the Allied / Axis racks, the default loadout, the Loadout window, classes or bot pools. The stats are real-world rounded figures (rate, velocity, magazine).

| Data | Where |
|---|---|
| Damage rows | `content/ai/damage.json` |
| Sounds: the nearest existing weapon's recordings | `content/audio/manifest.json`, `tools/audio_manifest.py` |
| Support-hand grip stations | `content/settings/fps_arms.json` |
| Ammunition, stocked by the mixed supply crate | `crate_mixed_supply` |

The silhouettes are rendered from the models like every other weapon's.

**Pellets.** `pellets` and `spreadDeg` in a weapon JSON make each shot fire that many rounds (`App::fireRounds`). The first goes along the aimed line and the rest fall evenly over the cone's area, deterministic from the shot. The Remington fires 9 pellets of 00 buckshot in 2.2°, with the damage per pellet. The first pellet carries the tracer.
- **Player and bots:** both fire every pellet locally.
- **LAN client:** sends one request per shell (extra pellets are flagged `extraPellet`). The host fires the whole shell.
- **LAN host:** its own pellets send one `shot` event.

**Sights** (measured on the baked meshes, rear-view slabs; each JSON's `sights.note` says what):
- **Iron sights:** each line passes from the rear aperture or notch to the front post tip.
- **M249:** the hooded front ring shows no post on this mesh, so its line runs level through the ring.
- **M4:** the line is the red-dot tube axis.
- **Scoped rifles:** use the existing scope view. The eye sits at the ocular lens face.
- **ADS calibration:** eye relief is 0.19 m (M4 0.17 m, pistol 0.4 m), with no corrections.

**The case** is placeable in Creative (Supply, beside the racks). E opens the same arsenal panel as the racks, titled "Modern", for either side. Equipping follows the rack rules (two main slots, own ammunition, LAN requests checked against the case's list).
- **LAN:** weapons replicate by index, sent as `u8`. There are 34 defs, well under 255.

<a id="mg1-animated"></a>
### MG1: animated first-person viewmodels

**Export.** `tools/modern_guns_anim_config.cjs` writes the configs and `tools/modern_guns_anim.py` runs per source in Blender 5.2 in background mode. Each source becomes `content/weapons/models/<dir>/vm.glb`, holding:
- the source's arms and gun as one skin, with textures at 1K or less;
- one glTF animation per clip.

`vm.json` beside it holds the clip lengths, the gun joint, and the rear sight (or scope eye), front sight and muzzle in rig space. The weapon-space anchors measured for the static model are carried over through the shared rest pose.

**Cinema 4D exports.** They animate the gun parts as objects, so each part becomes a bone, baked per frame. Their single timeline is cut into clips at the rest frames between the actions; the cuts were found from the part and arm motion.

**Framing.** Rig space is the camera frame: eye at the origin, −z forward, metres. At the first idle frame the sight line looks straight ahead, and the rear sight sits at the engine's hip pose, view (0.12, −0.07, −0.15), with `hipRear` overriding it. The sources' own cameras disagree and the Cinema 4D files have none, so they are not used. `tools/modern_guns_anim_preview.py` re-imports a GLB and renders its clips from the eye.

| Weapon | Source clips → states (seconds) |
|---|---|
| FN SCAR | Idle → idle (one pose), Shoot → fire 0.42, Reload 4.58, Take → draw 0.83, Hide → holster, Walk, Run |
| AK-74 | one 8.1 s timeline: fire 0.27, reload_empty 3.07, reload 1.73, inspect 1.6, idle 0.67, draw 0.77 |
| M4 | one 7.4 s timeline: fire 0.27, reload_empty 2.0, reload 2.2, inspect 1.4, idle 0.93, draw 0.6 |
| M16A3 | Idle (one pose), Shoot 0.42, Reload 5.63, Take 0.83, Hide, Watch → inspect 5.0 |
| M249 | one 15.6 s timeline: fire 0.4, reload 5.07, reload_empty 5.07, inspect 2.13, idle 1.47, draw 0.93 |
| Remington 870 | one 4.7 s timeline: fire 0.93 (shot and pump), reload 1.47 (one shell), reload_end 1.07 (not played), idle 0.67, draw 0.6 |
| M24 | one 6.7 s timeline: fire 1.6 (shot and bolt), reload 1.73, inspect 1.6, idle 1.07, draw 0.67 |
| Marksman rifle | SRifle_Idle 3.0, FPS_Pistol_Fire → fire 0.23, SRifle_Shot_nosight / _sight (shot and bolt, not played: the gun is semi-automatic here), SRifle_Reload 3.17, SRifle_Reload_Full → reload_empty 4.75, SRifle_Walk; no draw clip |
| Springfield XD | Idle (one pose), Shoot 0.42, Reload 1.88, Take 0.5, Hide, Run |

**Runtime** (`src/vm_anim.{h,cpp}`). It uses the zombie clip player (`AnimPlayer`) and the character compute skinning (`CharInstance`). The skinned buffer is drawn through the existing textured first-person pass: own FOV, cleared depth, zero motion. `render.cpp` is unchanged.
- **Clips:**
  - Equipping goes straight to the idle (MG2: the sources' draw clips start from odd poses; none is mapped).
  - Every shot restarts fire, so automatics retrigger it at the cyclic rate.
  - Each reload plays reload_empty (from empty, when the source has it) or reload, stretched to the reload time. One shell per clip for the shotgun.
  - Idle loops otherwise. Cross-fades are about 0.1 s (0.04 s into fire).
  - Walk, run, inspect and holster are exported but not played.
- **Reload timing:**
  - The clip lengths set the reload times.
  - The rounds move at `animated.insert` (0.6) of the clip. Swapping weapons after that keeps them.
  - The weapon fires again only when the clip ends (reload kind 4).
  - `cycleInFire` (shotgun, M24): the fire clip holds the pump / bolt stroke, so the cyclic rate or the bolt time waits for it.
- **Placement:**
  - camera × mantle / swim lowering × sway and bob (procedural, as for the static weapons) × ADS.
  - ADS turns and moves the rig so the posed rear and front sights land on the view axis at the eye relief. They are posed each frame through the gun joint.
  - The muzzle flash, tracer origin and aimed-fire sight line come from the same posed points.
- **Arms:** the generated support arm is not drawn (`showArms` off).
- **Scopes:** the scoped rifles switch to the existing scope view.

### MG2: playtest fixes and five more guns

**Fixes (from the user's playtest).**
- **The weapons case was invisible.** The rack draw path drew only the asset's box parts; `rack_modern`'s one box is an invisible collider. It now also draws the supplied model. Collider and thumbnail were already right.
- **Shimmer when panning.** The rig matrix was built in the weapon step, before the look, view kick and breathing of that frame. It trailed the camera by a frame, and TAA smeared the difference. The camera is now applied when the rig is drawn (`animVmDraw`, `animVmPoint`). The zero-motion TAA flag was already written: the skinned rig goes through the textured first-person pass with `uVmVel` = 1.
- **Equip:** no draw clip; the rig starts in its standing idle.
- **Movement:** no walk, run or strafe clip is mapped. All movement uses the idle with the procedural bob. Scripted strafe screenshots show the idle unchanged.
- **Still idles:** `animated.idleHold` (seconds) holds one frame of the idle clip; the M4 uses frame 0, so its support hand does not squeeze.
- **Shell loop.** A single-feed rig with `reload_start`, `reload_insert` and `reload_end` loads like this:
  - the start plays before the first round;
  - the insert plays once per round, and the round goes in at `animated.shellInsert` (0.7) of it (reload kind 5 while the segment plays out);
  - the end plays after the last round;
  - firing interrupts the loop, start, insert or end.

  Remington 870 cuts, in source frames at 30 fps: start 30-38 (the hand leaves the pump), insert 38-62 (hand out, shell into the port, hand out), end 62-74 (hand back on the pump). The source's 74-106 (a drop out of view, then a pump stroke) is not used.
- **Sights measured on the rigs.** `animated.points` holds rig-space rear / front / muzzle points that override `vm.json`, measured at the idle pose by offline ADS renders from the eye:
  - SCAR: the post is centred in the aperture.
  - M16: the eye is closer (`ads.eyeRelief` 0.08, `ads.vmFov` 40, `ads.worldFov` 45), so the aperture ring fills the centre with the front post in it.
  - M249: the line runs level through the post tip inside the hooded ring. The eye sits ahead of the rear sight (`eyeRelief` −0.16), so only the front sight and barrel are in view. The test skips a rear point behind the eye.
- **Red dots** (`animated.redDot`, rig point, and `redDotRadius`): a small emissive disc at the front lens centre, facing back along the sight line, drawn from adsT 0.6. The tube hides it off the axis. Used on the M4, AR-15 and MP7.
- **Depth of field** focuses on the rig's posed front sight.
- **Marksman rifle:** 2× scope.

**New guns** (all in the modern case; ammunition in the mixed supply crate).
- AR-15 (suppressed, holographic sight, semi): 5.56 mm, 30 rounds.
- HK MP7A1 (suppressed, red dot): 4.6x30 mm, 40 rounds, 950 rpm.
- CZ Scorpion EVO 3 A1: 9 mm, 30 rounds, 1150 rpm.
- Uzi: 9 mm, 32 rounds, 600 rpm.
- RPG-7: the rocket path, as the bazooka, with the GM1b impact reticle. PG-7V, one per reload.
- Pump-action shotgun (`shotgun-animated`): 12 gauge, 6-shell tube, loaded shell by shell like the 870, 9 pellets.

`animatedpistol-2` is skipped: its `rig.blend` is byte-identical to `animatedpistol` (same SHA-256), which is already `modern_pistol`.

Each gun has a static model (third person, racks, icons) and an animated rig. The RPG-7 sources, the MP7, the Scorpion and the Uzi ship as .rar: `tools/modern_guns_config.cjs` and `tools/modern_guns_anim_config.cjs` take an extract directory (default `incoming/modern_guns/_extracted`) holding `rpg7/`, `mp7/`, `scorpion/` and `uzi/`. Windows' own `tar.exe` (libarchive) extracts them.

New exporter options:
- `decimate` (per mesh, applied before the armature);
- `cutLens` (opaque lens discs inside an optic tube deleted; the renderer has no glass on rigs);
- `defOnly` (Rigify rigs: deform bones only);
- a clip can be a frame range of a named action, or several actions back to back;
- parents dropped with the unused objects keep the rig's world transform.

| Weapon | Source clips → states (seconds) |
|---|---|
| AR-15 | no idle and no fire clip in the set: idle = the last frame of `Draw` (held), a shot is a procedural kick (15 mm back, 2° up, 0.12 s); `Reload` → reload 2.5, `Full_Reload` → reload_empty 2.6; `Draw`, `Draw_2` not used |
| MP7A1 | `MC_MP7_static_pose` → idle, `MC_MP7_shoot` → fire 0.55, `MC_MP7_reload` → reload 2.32, `MC_MP7_full_reload` → reload_empty 2.72; idle (breathing), walk / run, show, hide, inspect, safety, shoot_dry not used |
| RPG-7 | `MC_RPG7_static_pose` → idle, `MC_RPG7_static_pose_empty` → idle_empty (no rocket: shown while empty), `MC_RPG7_shoot` → fire 0.55, `MC_RPG7_reload` → reload 3.77; walk, show, hide, inspect not used |
| Scorpion EVO | `Idle` → idle (frame 0 held), `Fire` → fire 0.43, `Reload` → reload 3.5, `Reload_Empty` → reload_empty 4.03; Equip, Unequip, Walk, Run, Inspect not used |
| Pump shotgun | no idle clip: idle = the first frame of `Fire`, held; fire = `Fire` + `PostFire` played back to back (shot and pump, 1.23 s, `cycleInFire`); `PriorToReload` → reload_start 0.33, `ReloadOne` → reload_insert 0.53 (once per shell), `ReloadLastOne` → reload_end 0.27; Weild, Hide, Unhide not used |
| Uzi | `Idle` → idle (frame 0 held), `Fire` → fire 0.27, `Reload` → reload 2.03, `Reload_Empty` → reload_empty 3.7; Idle_2, Aim_In / Out, Equip, Unequip, Firemode, Walk, Run, Inspect not used |

The Scorpion's gun texture (`RGB.png`) is in neither the archive nor the folder, so its gun has a flat dark polymer finish. The Uzi's loose cartridge by the stock is hidden in the static model.

### MG3: second live test

- **Still idles everywhere:** every rig now holds frame 0 of its idle (`animated.idleHold` defaults to 0; < 0 plays the clip). Breathing still sways the view, as for every weapon.
- **SCAR:** front point 0.4 mm left, so the post sits centred in the aperture.
- **Scorpion:** zoomed in (`eyeRelief` 0.12, `vmFov` 40, `worldFov` 50); both points 0.5 mm right, so the picture moves left.
- **AR-15 / MP7 cut-offs:** the MG2 lens cut also removed hood and tube walls. `cutLens` now removes only faces facing along the sight line (the lens discs), on every kept mesh.
- **AR-15:** a 3× scope (`scope.mag` 3) with a plain "+" reticle (`scope.reticle` "plus" → eyepiece reticle 4, `render.cpp`).
- **Remington 870:** the user's reflex sight (`red-dot-sight/rds_low.fbx`) on the receiver top (exporter `attach`: placed in weapon space at z 0.47, its blue lens material dropped, weighted to the gun joint). The sight line runs through its window, with a red dot (`animated.redDot`). Only the animated rig has it; the static model (third person, rack, icon) keeps the bead.
- **M4:** the lenses, the inner tube and the inner rims (faces centred within 13.5 mm of the axis, `cutLens` `all`) are removed. The clear circle reaches the outer housing, and the eye is closer (`eyeRelief` 0.12).
- **M249:**
  - ADS: the rear aperture centred, with the front post-in-ring inside it, on one level line (`eyeRelief` 0.07, `vmFov` 40). This replaces MG2's front-only view.
  - Hip: `animated.hipShift` (0, 0, 0.12) brings the rig 12 cm closer; it fades out in ADS.
- **RPG-7:** no impact reticle for an animated launcher (`explore.cpp`). Both sight points are 1.2 mm lower, so the post tip sits on the screen centre; the rocket goes there at its zero, as computed in `weapons.cpp`.
- **New:**
  - FN FAL (`modern_fal`, Cinema 4D timeline at 30 fps): fire 1–9, reload_empty 9–90, reload 90–152, idle 200–230. Inspect 152–200 and the draw 230–250 are not used.
  - Suppressed .22 pistol (`modern_pistol2`, `pistol-animated`): fire 1–11, reload_empty 11–57, reload 58–135, idle 169–197. 135–150 (the gun out of view), inspect and the draw are not used.
- **Skipped:**
  - `pistol-anim-pack`: the archive extracted, but it is a pistol rig and a UE5 arm rig as two separate armatures, with one FBX per clip. The exporter makes one skin from one armature with its actions in one file; merging them needs a retarget step that is not written.
  - `fps-animated-carbine-2`: byte-identical to the M4's source.
  - `animatedpistol-2` (MG2): byte-identical to `animatedpistol`.

### MG4: grenade launcher, higher-resolution weapons

**Six-shot grenade launcher** (`modern_glauncher`, `grenadelauncher-animated`): a revolver launcher, loaded round by round. Cinema 4D timeline at 30 fps:
- fire 1–15;
- reload_start 15–42 (the cylinder opens);
- reload_insert 120–146, once per round (one round per 26 frames in the source, rest frames 120 / 146 / 172), the round seated at 0.4;
- reload_end 198–215 (it closes);
- idle 267–287.

216–266 (the gun out of view, then an inspect) and the draw are not used. Its red-dot scope's lens mesh is dropped (the tube is open), with a red dot in it.

It fires through the launcher path with new `rocket` fields:
- `gravity` 1.0: a full-gravity arc (the bazooka and RPG keep 0.35);
- `atKill` false;
- `backblast` false;
- the frag grenade's blast: 80 damage, 5 m radius, crater 0.3, with an impact fuse.

The sight is aligned through the zero tilt (`weapons.cpp`), so the round lands on the dot at 100 m. Blasts go through the shared explosion pipeline, so the 5× explosive multiplier against the heavy enemies applies. Ammunition: `40mm_grenade` in the mixed crate.

**Higher resolution.**
- **WW2 weapons:** re-baked from their sources with textures up to 2048 (`model.maxTex`; never upscaled) and the full mesh. Only the flamethrower was simplified: its first-person model is the full mesh, and its old 0.6 mesh with 512 textures is `models/flamethrower/lod1` (`model.lod1`, `lod1Tex`). `WeaponDef::worldModel()` uses a weapon's lod1, when it has one, for soldiers, dropped weapons and rack displays.
- **Modern guns:** the first-person model is the animated rig. All 18 were re-exported with textures up to 2048 and no decimation (AR-15 104k, MP7 64k, RPG-7 56k, Uzi 43k triangles; all under 150k). Their static models (third person, racks, icons) are unchanged.
- **Unchanged shape:** the rig frames and every WW2 mesh's bounds and triangles are identical, so sight and grip points stay valid.
- **Sources:** WW2 sources were found in Downloads, `%TEMP%/nmk/mpai_src` and the incoming folders. `kar98k_new.glb` is Downloads' `kar98k (1).glb`, and the bazooka also needs `incoming/weapons_batch/metal046b`.
- **Rigs load on first equip** (`vm_anim.cpp`): textures are uploaded uncompressed (RGBA8), so all 18 rigs at 2048 would take about 1.7 GB of video memory at start. Only the rigs a player equips now take memory; clip lengths come from `vm.json`.

**Optic rims (M4, MP7).** MG3's M4 cut deleted every face centred near the axis, which opened a wedge in the housing wall and left lit tube faces around the window. `cutLens` now works like this:
- axis-facing faces are deleted (lenses, plus the inner rims with `all`);
- with `black`, the inward-facing faces of the bore (within `blackR`) are kept and given a flat black material (`mg1_black`);
- with `islands`, only whole connected pieces inside the radius are deleted (the MP7's lens discs), leaving its tube intact.

Both optics are now closed from every side, with a solid black rim around the clear window. The M4's clear circle is as large as in MG3.

**Shotguns (user's request).**
- **870 sight line:** it now runs through the reflex sight's window centre, measured on the rig (3.8 mm right of and 1.3 mm above the first, bore-derived line). The dot is on it. Aimed shots leave along the same posed line, so the pellet pattern is centred on the dot.
- **Recoil:** pitch +50 % on both shotguns.
- **Speed:** `animated.reloadRate` 1.54 and `fireRate` 1.43 play the shell loop and the pump clip faster. The clip lengths, and so the shell timing and the pump-limited fire rate, shrink with them: the shell loop is 35 % quicker and the pump cycle 30 % quicker. The animations still play through.

**AK:** `fps-ak-animated-2` is byte-identical to `fps-ak-animated` (the current `modern_ak`); nothing to replace.

### MG6: Barrett M95 (.50 BMG)

`modern_sniper_50` comes from `fps-50cal.zip`: one Cinema 4D FBX (`arms@bmg.fbx`: the arms, the gun as object-animated parts, one 218-frame timeline at 30 fps) and 10 textures, with no licence file. It is a bolt-action bullpup with a 5-round box, so it is a bolt gun. Clips:
- fire = the shot (1–13) and the bolt stroke (134–176) back to back, 1.8 s (`cycleInFire`: the next shot waits for it);
- reload_empty 13–84 (with the bolt pull);
- reload 84–134;
- idle 176–202, held.

The draw is not used. `animated.reloadRate` 0.6 plays the reloads slower (about 2.8 s and 3.9 s).

Handling:
- **Scope:** 10× with the plain "+" reticle; the eye at the ocular face, measured on the static mesh.
- **Damage:** 300 at any range, so one hit kills anywhere (legs 180). Penetration follows its energy: 42 g at 854 m/s.
- **Recoil:** pitch 11, recover 4.
- **Aiming:** `ads.speed` 0.6.
- **Sway:** `sway` 2.5, a new weapon field that multiplies the scope breathing only when standing; crouched and prone are unchanged.

Ammunition: `50bmg_mag5` in the mixed crate. Sounds: wired to the id in `audio.cpp`.

### SC1: scope view

Every rifle scope (scopeView, the eyepiece pass, `kEyepieceFS`) uses one layout:
- **Ring:** the outer edge is the screen height (`kScopeEyepieceR` 0.5). A solid black rim runs from 0.86 R out, with a soft inner vignette.
- **Inside:** the magnified view and the gun's reticle.
- **Outside:** no black. It is the same magnified frame, lightly blurred (16 taps on a coarse mip, radius 0.035 R) and dimmed (`uDark` 0.7). It is not a second unmagnified world pass, so the scoped view costs what it did before: one fullscreen pass.
- **The TZF gunner sight** keeps its own layout.

Reticles, drawn in the shader and centred on the aim point:

| Gun | Reticle |
|---|---|
| Kar98k ZF39 | German no. 1 (`de_absehen1`), unchanged |
| M1C Garand | M81, unchanged |
| AR-15 3× | plain "+" (`plus`) |
| M24 | `mil`: thick outer posts to a fine cross, mil ticks, red dotted centre, holdover dot clusters |
| .50 | `bmg`: thick side and bottom posts, thin top line, ticked centre cross, the curved range funnel with its ladder below |
| Marksman rifle | `acog`, 2×: red chevron (aim point at its tip) and a bullet-drop ladder |

Red parts glow (`redInk`).

### MG7: scout rifle, Glock 43, SCAR-H, AK-12, first-person knife

| Weapon | Source | Clips |
|---|---|---|
| `modern_sniper_3` bolt-action scout rifle, 4× scope (`plus` reticle, SC1 view) | `sniper-animated (2).zip`: Cinema 4D `arms@sniper.fbx`, one 159-frame timeline (not the M24's pack) | fire = shot + bolt 1–51, reload 51–93, idle 114–141 (rest frames 14 / 51 / 93 / 114 / 141) |
| `modern_pistol3` Glock 43 | `pistol-43-tactical-fps-animations.zip`: `Glock_Anim.fbx` | idle = `Shot` frame 1 (the 2-frame `Origin` is not the holding pose), fire `Shot`, reload `Reloat_t`, reload_empty `Reloat_f`; Inspect, Walk, Run not used |
| `modern_scar` FN SCAR-H (replaces the MG1 SCAR model; id, stats and sounds kept) | `scar-h_first-person_fps_animated.glb`: one 280-frame timeline at 24 fps | **its arms and clips are not used**: the hand skin is broken in the source (the sleeves stretch into long bars, even in Blender's own glTF import), so the SCAR-H gun is on the static path with the game's arms |
| `modern_ak12` AK-12 with its own tube optic | `ak-12animated.glb` | `Idle`, `Shoot`, `Reload`; `Equip` not used |
| `knife_bayonet` (every class's knife) | `knife_animated.glb`: one 116-frame timeline | idle = frame 0 held, fire = the quick slash 32–48 played 1.5× (0.44 s, the hit at its strike frame: `melee.windup` 0.083 s) |

- **AK-12 optic:** the model's lens mesh is left out (the tube is open). Its mark is a green ring with a centre dot (`animated.dotColor`, `dotRing`) at the optic's front centre, on the sight line.
- **Glock 43:**
  - this set animates bone scale and its unskinned rest is about 3× off its skin, so the static model is baked posed at the idle (`poseFrame` / `poseAction`, `staticPoseFrame` / `staticPoseAction`) with stray islands cut by `keepIslandsIn`;
  - the sight points are measured on the rig (`animated.points`);
  - `eyeRelief` 0.2: further back the eye sits inside the shooting arm's sleeve.
- **SCAR-H:** the rest pose is not the rifle assembled, so the static model is baked posed at frame 60 (`poseFrame`).
- **Knife:** framed by its own camera (`frame`; no sights), its blade skinned to the right wrist (`boneAttach`); its source hangs it off an IK goal empty. The equipment branch steps the rig, and a swing plays the slash.
- **Exporter additions:**
  - GLB sources; embedded textures are capped at `maxTex`;
  - skinned meshes with their own object transform are baked into the mesh (glTF skinning ignores a skinned mesh's node transform).

### MG8: M4 with iron sights, M249 realigned

`modern_m4_irons` comes from `m4-fps-weapon-animations-pack-v1.zip` (`M4.fbx`: arms, an M4 with a flip-up rear aperture and a front post, named clips on one timeline). It has `modern_m4`'s stats.
- **Clips:** Idle, Fire, Reload, Reload_Empty; Draw and Holster not used.
- **Bone-parented parts:** the gun parts are meshes parented to bones; the exporter now turns such meshes into meshes skinned 1 to that bone at rest (they did not survive the rig re-parenting in the glTF export).
- **Magazine:** its bone is scaled at rest, so the static (third-person) model hides it.

**Sights and point of impact.** An aimed shot leaves from the posed front sight along the rear-to-front sight line, tilted for the drop at the zero (`weapons.cpp`). ADS puts that line on the screen centre. So the round lands on the sight line at the zero, and alignment is a question of where the two points sit on the real aperture and post. Both guns were measured on their rigs (offline ADS renders, 5-20°):
- **M4 irons:** the post tip on the screen centre, centred in the aperture (`animated.points`).
- **M249:** the rear point moved 0.32 mm right and 0.06 mm up onto the aperture centre. Before, the aperture centre sat 4.6 mrad right of and 0.9 mrad above the post tip; after, within about 0.6 mrad. The front point (post tip) is unchanged.

**Shot line (MG8):** an animated rig's aimed shot leaves from the front sight along the camera axis. The rig's ADS already puts both sights on that axis, and using the axis directly removes the alignment's numeric residual (0.5 mrad on the M4 irons).

**Measured impact (no dispersion), relative to the point under the sights:**

| Gun | 25 m | 100 m | At its zero |
|---|---|---|---|
| M249 (zero 200 m) | +26 mm | +63 mm | 200 m: +9 mm (0.05 mrad) |
| M4 irons (zero 50 m, as `modern_m4`) | +5 mm | −27 mm | 50 m: +2 mm (0.04 mrad) |

At 25 m and 100 m these are the planned trajectory around each zero.

**Dev point-of-impact check:** `NMK_MG_POI=id,...` stands 25 m, 100 m and at the zero range from the farmhouse wall, aims (no dispersion, sway or breathing), fires one round and logs the impact against the point under the screen centre (`[mg8poi]`).

**Dev look** (`--selftest-weapons` with `NMK_MG_LOOK=all` or a list of ids; `NMK_MG_ONLY=1` runs only the MG steps): `selftest_out/mg2_<id>_{hip,strafe_r,strafe_l,ads,reload_a,b,c}.png` and `mg2_case.png`.

### MG12: more animated guns

Sources: `incoming/mg12/<name>/` (credits: [MG12](credits.md#modern-guns-mg12)). Same pipeline as MG1-MG8: `tools/modern_guns_config.cjs` + `modern_guns_convert.py` (static gun, 1K textures) → `--import-weapon-models`; `tools/modern_guns_anim_config.cjs` + `modern_guns_anim.py` (the source's own arms and clips, textures up to 2K) → `models/<id>/vm.glb`, `vm.json`. `MG_ONLY=id,...` limits the anim config to those ids. Every gun uses its source's own arms (none uses the game's generated arm). The WP4c fire-clip mask (the clip moves only the parts under the gun joint), the WP4 recoil spring, the muzzle point (flash, tracer) and the casing rule apply to them as to every rig.

| Weapon (`id`) | Source, clips (30 fps) | Stats |
|---|---|---|
| .357 Magnum revolver (`modern_revolver`) | DJMaesen `revolver animated`; fire 1-15, reload_start 15-52 (cylinder out, empties ejected), reload_insert 52-75 (one round; loops), reload_end 190-224, inspect 224-270, idle 270-297 | 6 rounds, round by round, 150 rpm, 440 m/s, 10.2 g; no case ejected per shot (`casings` false) |
| Sawn-off double-barrel (`modern_sawnoff`) | DJMaesen `sawnoff animated`; fire 1-15, reload 15-107, reload_empty 107-195, idle 221-252 | 12 gauge, 2 shells (`containerSize` 2), 9 pellets in 4.5°, 380 m/s; no pump sound, no case per shot |
| SMG-9 (`modern_smg9`) | DJMaesen `fps smg9 animated`; fire 1-9, reload_empty 9-79 (with the bolt pull), reload 79-133, inspect 133-179, idle 179-206 | 9x19 mm, 30 rounds, 900 rpm auto / semi, 380 m/s; red dot (`animated.redDot` at the lens centre) |
| Desert Eagle (`modern_deagle`) | DJMaesen `animated pistol` (heavy pistol); fire 1-16, reload 16-71, reload_empty 71-159, idle 220-250 | .50 AE, 7 rounds, 200 rpm, 470 m/s, 19.4 g |

- **Sights** (rig points from the static mesh's top profile, checked in the ADS captures): revolver rear notch / front post (the post in the notch, level); Desert Eagle the same; sawn-off from just over the breech to the top of the muzzle block (the bead); SMG-9 through the red dot's lens centre (the lens object is dropped: open window).
- **New weapon field** `casings` (default true): false = no spent case per shot (`App::shotCasing`).
- **Pump sound:** only tube-fed (`feed` single) multi-pellet guns play it after a shot.
- **Reload sounds** take an optional pitch (`RSet.pitch`, resampled like the shot variants).
- **Battle Royale ammo** by calibre (`loot.cpp ammoTypeOf`): .357 and .50 AE count as .45 (the .50 pool is the BMG's), 9x18 as 9 mm, 9x39 as 7.62; a pistol for the loot weights is any semi-automatic 9 mm / .45 gun with 16 rounds or fewer, whatever its feed.
- **Crate:** `357_speedloader6` and `50ae_mag7` in the mixed supply crate.
- **Operators:** the SMG-9 joins both sides' primary pools (weight 1).
- **Damage** (`content/ai/damage.json`, DMG1 bands): revolver 62 / 40, Desert Eagle 60 / 38, sawn-off 28 / 5 per pellet, SMG-9 30 / 18; DMG1 classes pistol / shotgun / smg with `headMax` 80 / 75.

**Batch 2.**

| Weapon (`id`) | Source, viewmodel | Stats |
|---|---|---|
| Glock 34 with compensator (`modern_glock34`) | DJMaesen `fps pistol Animated`, its own arms and clips; fire 226-235 (the slide cycles; the source's other fire clip 1-10 leaves it locked back), reload_empty 10-66 (from the slide lock), reload 66-132, idle 180-206 | 9x19 mm, 17 rounds, 450 rpm, 375 m/s; DMG1 pistol 32 / 20, `headMax` 80 |
| Makarov PM (`modern_makarov`) | Cransh GLB: **the gun only**, static on the game's arms (slide -> bolt, mag -> mag) | 9x18 mm, 8 rounds, 400 rpm, 315 m/s; pistol 30 / 18, `headMax` 75; the Axis operators' sidearm |
| HK Mk 23 (`modern_mark23`) | BarcodeGames FBX, its own arms and clips (named actions: Idle, Shoot, Reload); **local-only** (licence) | .45 ACP, 12 rounds, 350 rpm, 260 m/s; pistol 36 / 22, `headMax` 80 |

- **The Cransh GLB (Makarov):** its arms' skin is broken: the arms explode to thousands of units even in Blender's own glTF import, whatever its bind-pose options (as the MG7 SCAR-H). Its static gun is baked posed at the first frame of its idle (its rest pose is taken apart; `poseFrame` / `poseAction`). WP7: the FN 502 Tactical (a second Cransh GLB) was removed from the game at the user's request; its shot sound moved to the Desert Eagle and the .357 revolver.
- **The Mk 23:** the committed `modern_mark23.json` shows the Springfield XD's model and rig. The real model, rig and sights come from `content/weapons/local/modern_mark23.json` (merged by the weapon loader; `modern_guns_anim_config.cjs` reads it too) and `models/modern_mark23_local/`, both git-ignored.
- **Crate:** `9mm_mag17`, `9x18_mag8`, `45_mag12`. Sounds are borrowed and pitched apart (`audio.cpp` MG12 batch 2).

### WP8: hip framing (the HK Mk 23 as the reference)

The hip view is framed by the firing hand: the weapon's `rightGrip` socket sits at a fixed camera-space point, measured at the hip by `--selftest-weapons` with `NMK_MG_LOOK` (the `[mg11hip]` log line: rear sight, front sight, muzzle and grip, camera space, x right, y up, -z ahead). The viewmodel FOV is the same 60 deg for every gun at the hip.

| Class | Firing grip (m) | Notes |
|---|---|---|
| Pistols | (0.072, -0.191, -0.300) | the Mk 23's own framing. The Glock 34 sits on the same line of sight 4 cm closer, (0.062, -0.166, -0.26), so its slimmer slide reads the same size. The static Luger, M1911 and Makarov sit 4.5 cm higher, because their socket is above the visible hand. |
| Rifles, SMGs, LMGs, shotguns, snipers, launchers, blasters | (0.12, -0.235, -0.16) | the modern rifle rigs' cluster (M4, M16, FAL, MP7, SCAR, RPG-7); longer guns extend further forward |

- **How the shift is applied:** rigs take it as `animated.hipShift` and static models as `ads.hipOffset`. Both apply only at the hip and fade out in ADS, so every iron-sight and optic sight picture is unchanged (ADS captures compared before / after).
- **Blasters:** their zoom-aim keeps the hip pose, so their aimed view moves with the hip framing. The zoom and the dot crosshair are unchanged.
- **Kept on their own framing:**
  - AK-12 and .50 sniper: the rule brings their scopes too close.
  - Semi-auto sniper, Uzi and Scorpion: their grip sockets don't match the visible hand.
  - Thompson: its hip view reads better as before.
  - VSK-94 (MG12 batch 3): its grip socket sits high on the grip, so the rule drops the gun out of view. The Saiga-12 follows the rule (its firing hand was behind the eye).
  - Carried M2HB and Panzerschreck: they use their own holds.

**Batch 3.** Both are Cransh GLBs. Their arms' skin is broken, as with the Makarov, so they are static guns on the game's arms (bolt and mag roles), baked posed at their idle. The VSK's idle holds it reversed and upside down, so it is turned 180 degrees about x when converted (`rotX`).

| Weapon (`id`) | Stats |
|---|---|
| Saiga-12 with suppressor (`modern_saiga`) | 12 gauge, 8-round box, 8 pellets in 3.2°, semi, 400 m/s; DMG1 shotgun 27 / 6 per pellet; suppressed (`handling.json`); sights along its rail |
| VSK-94 (`modern_vsk`) | 9x39 mm, 20 rounds, semi / auto 800 rpm, 270 m/s, 16 g; DMG1 semi 48 / 36; integrally suppressed |

Both are in the Axis operators' primary pool (weight 1), with crate magazines `12ga_mag8` and `9x39_mag20`. Sounds are borrowed, both muffled: the 870's shot for the Saiga, the suppressed .22's for the VSK (heavier); the AK reload, pitched apart.

### SW1: blasters, blaster troopers and robot commandos

Two AI types, selectable per side in the AI rows: **Blaster troopers** (`AiType::Troopers`, 16) and **Robot commandos** (`AiType::Robots`, 17).
- **Brain:** the soldier brain (cover, facing, shouldered moving pose, hit zones), on either side.
- **Bodies and loadouts:** `content/ai/combat.json` `kits`.
  - Troopers: one of `trooper_a` / `_b` / `_c` in turn, carrying only `blaster_carbine` (weight 7) or `blaster_rifle` (weight 3).
  - Robots: `robot_commando`, carrying only `blaster_robot`.
  - No class kit, sidearm, grenades or explosion pack. They drop what they carry.
- **Robot traits:** 0.85 health, 1.3x pace, 1.4x aim turn. Its hits spark with a metal sound instead of bleeding.
- **Bodies:** baked by `tools/bake_ai_characters.py kits`, local-only (git-ignored `content/characters/trooper_*.glb`, `robot_*.glb`). Without them the soldier body is used.

**Blasters** (`content/weapons/blaster_*.json`, side `blaster`): in no rack, case or loot table.
- **Weapon fields:** `"blaster": "blue" | "red"` and `casings` false.
- **Bolts:** every shot is a bolt (tracer kind 3 or 4) with no gravity and no zero tilt. Each bolt draws a white-hot 1.4 m core in a coloured glow, a short fading trail and a small travelling light.
- **Muzzle flash:** tinted to the bolt's colour, with a coloured light.
- **Impact** (`Fx::blasterImpact`): a coloured flash, sparks, scorch smoke and a brief light, with the synthesised `blaster_hit` sound. There is no dust and no ricochet.

| id | Fire | Rounds | Velocity | Damage (DMG1) |
|---|---|---|---|---|
| `blaster_carbine` | auto 700 / semi | 50 | 180 m/s | 31 / 22, assault class, auto spread |
| `blaster_rifle` | semi / auto 320 | 30 | 200 m/s | 44 / 36, battle class, rifle accuracy |
| `blaster_robot` | auto 600 / semi | 40 | 170 m/s | 29 / 20, assault class, auto spread |

- **Sounds:** synthesised in-house (a pitch sweep with FM wobble, odd harmonics, an attack click and a short spring-like echo): `content/audio/custom/guns/blaster_a|b|c.wav` and `fx/blaster_hit.wav`. The reloads borrow `reload_m4.mp3`, pitched apart.
- **Committed models:** stand-ins, the static models and sights of the M4, M16 and AK.
- **Local overrides:** an optional local file `content/weapons/local/<file>.json` (git-ignored) is merged over a weapon's JSON (`merge_patch`): names, model directory, sights, muzzle. The blasters' real models are local-only bakes in `content/weapons/models/blaster_*_local/`.
- **Dev:**
  - `NMK_AI_TYPES=<side 0>,<side 1>` makes every AI of a side that AI type.
  - `--shots-poses` takes `NMK_POSE_POSTS` (`kneel`, `carry`, `moveaim`, `idle`, `fire`; `fire` shoots one round a frame before the picture).
  - `NMK_POSE_WEAPONS=own:none` keeps the bot's own weapon.

**SW2 (after play):**
- **Hold:** a weapon's `"hold"` block sets its third-person hold.
  - `offset`: in the chest frame (m), moves the shouldered and low-ready weapon.
  - `weld`: the cheek-weld share.
  - `kind`: `pistol` / `rifle` / `launcher`, overriding the guess from where the grip sits.
  - The blasters are held out in front of the helmet: carbine (0, -0.02, 0.21), long rifle (0, -0.02, 0.06), robot blaster (0, -0.10, 0.12).
  - Their grip and support sockets sit on the meshes: the pistol grip, and the shroud or fore-end underside.
- **Robot blaster size:** its local bake is now 0.63 m long (it was 0.92 m: the asset itself was too big), so the hands, pickup and first-person model all use the real size.
- **Aiming:** no sight picture for the blasters. `ads.zoom` keeps the hip pose (lifted and centred a little by `ads.posCorrection`) and narrows the FOV: 0.62x, and 0.5x for the long rifle.
  - Zoom-aimed shots go from the muzzle to the camera ray's hit point, not along a sight line.
  - `"crosshair": "dot"` draws a white dot in a thin outlined +, tighter when aimed.
  - The dev look also writes `mg2_<id>_hip_ui.png` / `_ads_ui.png` with the HUD.

### NG1: user-supplied FPS packs (local-only)

Sources: `E:/nmk/incoming/ng1/<name>/` (credits: [NG1](credits.md#new-guns-ng1)). Same pipeline as MG12 (`modern_guns_config.cjs` + `modern_guns_convert.py`, `--import-weapon-models`, `modern_guns_anim_config.cjs` + `modern_guns_anim.py`); the bakes go to `models/<id>_local/` and the real model / sights / rig to `content/weapons/local/<id>.json`; the committed JSONs carry the stats on a stand-in (the XD for the pistols, the SMG-9 for the .45 SMG; the AK keeps its MG1 model).

| Weapon (`id`) | Source, clips | Stats |
|---|---|---|
| AK-74M (`modern_ak`, replaces the AK-74's model and rig) | FpsAnims.fbx: AK_Idle / AK_Shot / AK_Reload / AK_Reload_full (60 fps; AK_Draw, Run, Walk not used); its spare magazine kept | the AK-74's (5.45x39, 30, 650 rpm) |
| Makarov PM (`modern_makarov`, back after WR1) | Arms_Mak.fbx: MK_Idle / MK_Shot / MK_ReloadFast / MK_ReloadFull; its parked suppressor / optic bracket dropped (`dropMats`) | MG12's (9x18, 8 rounds) |
| .22 tactical pistol (`modern_fn502`, back after WP7) | FPS_Anims.fbx: FN_DefPose (idle), FN_Shot, FN_Reload, FN_ReloadFull | MG12's (.22 LR, 15); its own shot (the 9 mm recording raised to a rimfire crack) |
| .45 SMG (`modern_smg45`, new) | arms@smg45.fbx, one 235-frame timeline at 30 fps: fire 1-9, reload_empty 9-81, reload 81-130, inspect 130-177, idle 177-216 | .45 ACP, 25 rounds, 600 rpm, 285 m/s, auto / semi |
| 9 mm service pistol (`modern_pistol4`, new) | arms@beretta.fbx, 265 frames: fire 1-12, reload 12-84, reload_empty 96-177 (from the slide lock), inspect 177-234, idle 234-265 | 9x19, 15 rounds, 400 rpm, 375 m/s |

- **Sights** from each static mesh (front post tip; rear notch shoulders, the .45 SMG's aperture centre), checked in offline sight-picture renders from behind the rear sight (the post tip level with the shoulders).
- **Hip framing:** the pistols by `hipRear` (no `hipShift`, as the Desert Eagle / Mk 23); the .45 SMG's grip socket at the rifle-class hip point (computed offline); the AK-74M as the FAL.
- **Not checked in the game** (no game instance for this task): the ADS sight picture on the rigs and the hip framing; the user tests them.
- `pistol.zip` is the Springfield XD source again (identical): nothing new; its missing hands were XD1.

### PS1: the pistol red dot (sight value 7)

The only optic a pistol takes, and no other gun takes it (`loot.cpp` `lootSightFits` case 7; the red dot, holographic, ACOG, 4x, 10x and thermal stay refused on pistols). Item `br_sight_pistol` (`content/settings/loot.json`, weight 10, rarity 4, like the red dot); the smithing table, the inventory and Battle Royale loot list it like the other optics.
- **Model:** the user's `vortex_viper_red_dot.glb`, baked by `tools/bake_br_assets.py` (`br_sight_pistol`: no mount adapter, 46 mm, 2 434 triangles, 512 px textures, the lens removed: no glass in the view); licence unknown: local-only (`content/assets/models/br_sight_pistol/`). Without it the committed asset shows a part-built frame with an open window (`tools/br_standins.py`).
- **Mount:** each pistol's `rail` (its base on the slide top, 34 mm ahead of the rear sight; measured on the triangles of the model the JSON uses, so the local overrides carry their own). In first person it rides the slide: `animated.slideJoint` (the XD `Up`, the .22 `gun_bolt`, the Glock 43, Makarov, .22 tactical `Slide`, the Glock 34, Desert Eagle, 9 mm service pistol `gun_slide`, the Mk 23 `side_j`; the revolver and the static WW2 pistols: the frame).
- **Picture:** the 2 MOA HUD dot (AT2) on the shot line at the zero, clipped to this sight's 7 mm window; AT6 keeps the pistol's own sights out of the window. Eye relief 0.35 m (the pistol at arm's length).

### XB1: crossbow and the user's MP5

- **Crossbow (`modern_crossbow`, committed):** DJMaesen's `crossbow animated` GLB. Static: `tools/modern_guns_config.cjs` `crossbow` + `modern_guns_convert.py` (its rest pose is assembled with a bolt on the rail; muzzle already down glTF +z), baked by `--import-weapon-models`. Rig: the GLB imports as two armatures (arms, bow), so `tools/crossbow_prep.py` freezes the bow's limbs and string at rest and drops the bow armature; `modern_guns_anim.py` then bakes the parts per frame (`rigidify`). Clips from the one 5.67 s timeline (24 fps after import): fire 0-3, reload 3-62 (cock, lay the bolt; 2.46 s), inspect 62-109 (not played), idle 109-136. Embedded textures extracted to `textures/img<n>_x.png`; the exporter's new `mr` key reads a glTF metal-rough map (G roughness, B metal).
  - **Stats:** one bolt (`cap` 1, `feed` single, container `crossbow_bolt`, 30 in the mixed supply crate), 95 m/s, 25 g: the ordinary bullet ballistics give the drop (about 0.5 m at 30 m; `zero` 30). DMG1 row: 85 near / 55 far, class rifle. Sound: a stand-in (the suppressed rifle shot at 0.7 pitch, darker, quieter; `reload_b` for the reload); the performance session owns sound.
  - **Sights:** a peep aperture (rear) and a post (front). Rear = the hole centre of the rear sight block over the trigger (a rear-view render clipped to it); front = the TIP of the post inside the front globe ring (XB3; the ring's inner centre 0.03 mm under the tip, inner radius 9.7 mm, so the rear hole and the globe are concentric when the tip is on the line). XB1 took the ring's top (13 mm high: the post sat low), XB2 the bolt rail's front guide (two ears, 4 cm behind the globe, 16.5 mm low). XB4: in the game XB3 drew the ring 25 px over the rear hole (XB1 16 px under), so the front point is set from the captures: XB4 weapon y 0.0985, XB5 0.0966 (XB3 28 px high, XB4 8.7 px low: 4.65 px per mm; 6 mm over the tip), z unchanged; the cause of the 8 mm difference from the mesh is not found (the rig at its idle hold has the tip on the baked line). The line is level with the bolt rail, 4.4 cm above it; the shot is tilted for the drop at the zero (30 m). `animated.points.up` (the rig's gun vertical, 2.37 deg lean corrected) keeps ADS unrolled. Eye relief 0.12 m (a peep at the eye).
  - **Limitations:** the limbs and string do not flex (a frozen, drawn bow), and no separate bolt projectile model: the round is a bullet.
- **MP5 (`modern_mp5`, local-only):** `fps-animated-mp5.zip` is the user's MP5 meshes on DJMaesen's fps-animated-smg rig (same 235-frame timeline as the UMP-45; the file carries no source for the meshes). It replaces JUST's MP5 model and rig through `content/weapons/local/modern_mp5.json` and `models/modern_mp5_local/` (static `mp5_local`, rig `modern_mp5_local` with `wid` modern_mp5); stats, sounds and hold stay JUST's. Clips: fire 1-9, reload_empty 9-81, reload 81-130, idle 177-216. Sights: the drum aperture centre and the post tip in its hood (rear-view renders), `up` from the rig (upright). Perf switch `mp5new` (on; `--rq-off mp5new` at start-up) keeps JUST's MP5.

### MG13: the minigun

`modern_minigun` from DJMaesen's `Minigun animated` GLB (committed). Static `tools/modern_guns_config.cjs` `minigun` (barrels a part of their own); rig `modern_guns_anim_config.cjs` `modern_minigun` straight from the GLB (one armature; the gun parts on animated empties, baked per frame). Clips from the 6 s timeline (24 fps after import): fire 0-5 (the barrel cluster turns one barrel, 60 deg, and the lid kicks), idle 5-48, reload 48-100 (the box out and in), inspect 100-131 (not played). No draw / spin-up / wind-down clip.
- **Spin:** every round restarts the fire clip played at `animated.fireRate` 8.33 (its 0.21 s in one 25 ms shot interval), so each round turns the cluster one barrel: a continuous 400 rev/min at 2400 rpm (six barrels look the same after each step).
- **New weapon fields:** `spinUp` (s; the first round waits until the barrels have spun that long with the trigger held; they wind down twice as fast off it) and `hipSpreadMul` (the class hip cone x this, not in ADS).
- **Numbers:** 7.62x51, 500-round box (`762n_box500`, new; 4 in the mixed supply crate), 2400 rpm after 0.4 s, 850 m/s, 9.5 g, tracer every 3rd (as the MG 42), recoil pitch 1.1 / yaw 0.8 / climb 0.06, hip cone x2 (lmg 3 deg -> 6 deg), `moveSpeed` 0.72, reload 4.3 s (the clip at 0.5x). DMG1 34 / 22, class lmg. Hold and sockets: the M249's (`modern_lmg`).
- **Aiming (MG13d, the user: "minigun doesnt need an aiming view, it should just zoom in slightly"):** `ads.zoom` 0.18: holding aim keeps the hip pose (vm_anim.cpp: an animated rig ignores the ADS blend when `ads.zoom` > 0, as the static zoom guns already did) and narrows the world FOV by 18 % (about 1.2x); the aimed cone is the lmg's plain hip cone (3 deg; hipSpreadMul 2 gives 6 at the hip). No sight line, eye relief or ADS up: `sights` are MG13's bore-parallel line again, used only by the hip framing. MG13b's raised line (0.29 m) and relief 0.30 are gone.
- **Hip (MG13d):** the MG13 framing again (the default hipRear): the rig's own left hand on the left wooden handle, the forearm from the bottom left. MG13b's hipRear [0.12, 0.65, 0.02] (the eye 0.5 m back) left both handles bare. The right spade grip's upper part and its fire button (9-16 cm from the eye: the slab at the right edge) are cut from the gun mesh (the bake's `cutBox`, rig space at the hip; the user: trim the grip, keep the eye).
- **ADS eye (MG13c):** the game poses the rig's sight points with the gun joint's delta from the GLB's rest nodes to the idle (anim_clip.cpp); the bake wrote them at the idle. In this file the armature node's rest translation is (-0.2, 0.008, 1.0) while every clip keys it to ~0, so the delta is a 1 m shift and the points landed 1 m behind the gun (MG13 / MG13b's broken aiming view: the eye behind the arms). The bake's cfg `glbRest` takes the points back through that delta; it logs `glbIdleDelta` for every rig (the others are near identity).
- **Capture:** `--capture-recoil` pre-spins the barrels (spin = `spinUp`) on its one-frame trigger, so the peak frame is a real shot (MG13: no shot, 0.000 m).
- **Sound:** the vehicle minigun's shot (`guns/gun_shot_1.mp3`, pitch 1.15, its rapid-fire voice limits) and `reload_a` slowed, stand-ins.

### STR1: the Sten on the animated FPS arms

Before: `sten` had no `animated` block, so it was drawn as its static model ("Sten Gun" by Am I dead?, CC-BY) with the game's generated arm (IK hands, `hold` pose); no Sten-like gun used a rig. Now `sten.json` carries an `animated` block on `models/sten_rig_local` (local-only, git-ignored): the fps-animated-smg arms and clips (the user's MP5 pack; the same DJMaesen arms as the committed UMP-45 rig, 7 436 vertices) with the Sten's static parts hung on the rig's gun (base, trigger), bolt and magazine objects (`tools/sten_rig_prep.py`). Without that folder, or with `--rq-off stenrig`, the Sten keeps the static model and the generated arm.
- **Placement:** the Sten's rightGrip socket on the MP5's (weapon space shift 25.7 / 5 / 80 mm); the bore lands on the MP5's within ~2 mm. The rig bake uses the MP5's static mapping (`stat`, `realLength` 0.7) with the Sten's sight points shifted (`pointShift`).
- **Hands:** the right palm on the grip behind the trigger (z 0.23); the support palm under the receiver (z 0.44, 8 cm under the bore), 8-11 cm behind the side magazine housing (z 0.52-0.56): the rig's support pose is the MP5's handguard and does not reach the side housing. The magazine leaves downward in the reload (the MP5's path), not sideways.
- **Clips:** fire 1-9 retriggered per shot (550 rpm), reload_empty / reload at 0.8x (3.0 / 2.04 s), idle 177-216.
- **Sights:** the Sten's own rear aperture (centre (-0.0238, 0.097, 0.275), the receiver's rear peep, hole 0.0945-0.0995) and front post tip ((-0.0238, 0.096, 0.585)), as measured for the static model; up vertical (the rig is framed level), so ADS has no roll.
