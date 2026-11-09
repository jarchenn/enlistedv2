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

Each shot adds aim displacement: pitch `pitch × (0.85–1.15) + climb × min(burst, 12)` and a random yaw wander of up to `± yaw`, scaled by posture (standing ×1, crouched ×0.7, prone ×0.2), the deployed bipod factor and ×0.85 in ADS. The climb has no ceiling apart from the ±85° look clamp. While a burst runs, the displacement is held as `recoilP / recoilY` on top of the camera's own pitch and yaw.

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
| B | Build mode with the Assets palette: Tab palette, 1 / 2 / Q defences, wheel / Z / X / R rotate, LMB place, Del remove, Ctrl+Z undo, RMB leave ([builder.md](builder.md)) |
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
