# Plan: FPS weapon handling, ballistics, environment and buildable defences

## Context
You attached `m1_garand_claude_package_v3.zip` and five Sketchfab GLBs. `claude_instructions.md` in the zip asks for:
- **Part 1, weapon handling:** viewmodel rendering, ADS, depth of field, sway, bob and breathing, IK arms.
- **Part 2, environment:** surface damage and penetration, deeper AT craters, foliage with destructible trees, procedural clouds, ballistics that start at the iron sights.
- **Part 3, defences:** a buildable tank trap and sandbags with placement, snapping and damage mitigation.

The guide is written for Unity or Unreal. Normandy Mapmaker is our own C++20/OpenGL 4.5/Jolt engine, so each part maps to a module of ours (see the table below).

**Decisions you made:**
- The GLBs are yours or licensed, so they ship. `docs/credits.md` will hold author and licence lines for you to fill in.
- AT craters use the guide's formula, scaled down so they stay climbable: **D_max 1.2 m, R 3.0 m, p 2**. That gives a peak wall slope of about 32°, under the player's 50° limit.
- Skip the earlier update's benchmarks, packaging and report. Make one checkpoint commit of that work, then start this.

**Assumptions:**
- The two reference images in the zip are placeholders and were never supplied. The plan works from the GLBs alone.
- The player has no health model. The guide's blast-to-character rule becomes a readout of blast exposure plus a concussion effect, with no death.
- Nothing in the engine drives vehicles or pathing. The tank trap's NavMeshObstacle becomes a `blocksVehicles` footprint flag plus a debug overlay.

| Guide term | Our module |
|---|---|
| PlayerController | `src/explore.cpp` (existing) plus the new `src/builder.cpp` |
| WeaponHandler | new `src/viewmodel.{h,cpp}`, split out of `src/weapons.cpp` |
| BallisticsManager | new `src/ballistics.{h,cpp}` |
| BuilderController | new `src/builder.{h,cpp}` |
| Weapon camera | a viewmodel projection with a fixed FOV in `src/render.cpp` |

## What the GLBs contain
Measured with a read-only parse.

| File | Tris | Textures | Structure | Target weapon |
|---|---|---|---|---|
| realistic_m1_garand.glb (43 MB) | 15.7k | 3 × 4096² (base, MR, normal) | Nodes `Garand001`, `Bolt`, `Charging_Handle`, `Clip`, `Bullet`, `Recieve`, `Casing`, `ClipEmpty`. Animation `Take 001` (6.33 s) moves the bolt, clip, bullet and handle. Offset to y ≈ −9.5. | `m1_garand`, plus the body of `m1c_garand` |
| mp40.glb (33 MB) | 5.7k | 3 × 4096² (base, MR, normal, AO) | 11 Polish-named parts: korpus=receiver, magazynek=mag, zamek=bolt, pokretlo=cocking knob, kolba=stock, szczerbinka=rear sight, spust=trigger | `mp40` |
| mg42.glb (11 MB) | 16k | 3 × 2048² | One mesh; long axis X; 1.21 m (real length 1.22 m) | `mg42` |
| browning_m1918a2_bar.glb (8 MB) | 20k | 3 × 2048² | 5 meshes in one material; long axis X; 6.79 units, needs scaling to 1.214 m | **new** `m1918a2_bar` |
| low-poly_thompson_m1a1.glb (0.6 MB) | 3.9k | none (KHR spec-gloss factors only) | Skinned. Joints `body, bolt, mag, bullet(s), magrelease, safety, selector`. Long axis Y; 9.1 units, real 0.813 m | `m1_thompson` |

`kar98k` and `kar98k_zf39` keep their procedural box models. The engine has no glTF loader yet; it already has nlohmann/json and stb.

## Implementation steps

### F0. Checkpoint
- Commit the current uncommitted tree as-is, with no benchmarks, packaging or report.
- Write memory files: the project, the asset pipeline, and your preference to plan first and switch models to implement. Plan mode doesn't allow writes, so this happens first in the implementation session.

### F1. GLB import pipeline (`src/gltf_import.{h,cpp}`)
**Dependencies.**
- Add **cgltf v1.14** with FetchContent, pinned to a commit (MIT licence).
- Use the `stb_image_resize2.h` already in the stb checkout.

**Bake command.** `NormandyMapmaker.exe --import-weapon-models <srcDir>` bakes each GLB once into `content/weapons/models/<id>/`:
- `mesh.nmw`: positions, normals, tangents (generated when missing) and UVs, with per-node part ranges.
- `albedo.jpg`, `mr.png`, `normal.png` (and `ao` when present), downscaled to **1024²** by default. A `--tex 2048` flag raises that.
- The source GLBs are not committed (`.gitignore` gets `content/weapons/source/`). The baked output, a few MB per weapon, is committed.

**Per-weapon `"model"` block** in `content/weapons/*.json`:
- `file`, `realLength` (m), `longAxis`, `muzzleSign`, `rotDeg`, `offset`.
- `nodeParts`: maps GLB nodes to WPart roles. For example, Garand `Bolt→bolt`, `Charging_Handle→handle`, `Clip→clip`, and `Casing`/`ClipEmpty`→hidden (they become ejected-piece meshes).
- `hide`: a list of nodes to leave out.

**Fit and conversion.**
- Scale = `realLength` ÷ the measured long-axis extent of the main body node (not the whole scene, because the Garand's casing sits far from the rifle).
- Move the origin to the receiver, with +z pointing to the muzzle, matching the current procedural convention so `rear`, `front` and `muzzle` keep their meaning.
- **Thompson:** bake the bind pose (joint world × inverse-bind) into static per-joint parts, and convert spec-gloss to metal-rough. Metallic is 1 when specular luminance > 0.5; roughness = 1 − glossiness.
- **Garand animation:** sample `Take 001` into a reload track keyed by part, driven by the reload timer's 0–1 progress.

**Fallback and tooling.**
- If the baked model is missing, the weapon uses the existing box parts, so nothing breaks.
- A sight calibration panel (Explore → F3 debug) shows an orthographic side view with a millimetre grid. Its sliders adjust `rear`, `front`, `muzzle` and the sockets, and Save writes them to the weapon JSON.

### F2. Viewmodel rendering, ADS and sight-origin ballistics (`src/viewmodel.cpp`, `render.cpp`, `weapons.cpp`)
**Textured shader.** A new `progViewModel` is a metal-rough PBR shader with normal and AO maps. It reuses `setLighting()` and the shadow and sky-visibility code from the main shader. A new `TexMesh` type (pos, nrm, tangent vec4, uv) goes in `assets.h`.

**Clipping and FOV.**
- The current pass (clear depth, then draw `R.viewModel`; render.cpp:772) stays.
- It gets its own projection: `vmProj = perspective(vmFov = 50°, aspect, 0.01, 4)`.
- The world FOV lerps from 72° to 58° (the existing `weaponCamera`) while `vmFov` stays fixed. The screen centre doesn't move, so sight alignment survives.

**Apparent positions.** The muzzle flash and ejected pieces are drawn in world projection. To line them up with the barrel drawn at `vmFov`, multiply their view-space x,y by `tan(worldFov/2) / tan(vmFov/2)` before the effect is spawned.

**General ADS alignment.** This replaces the axis-aligned assumption at weapons.cpp `viewModelMatrix`, with the formula in a code comment:
- `L = normalize(F_local − R_local)`, `q = rotation(L → +z_weapon)`.
- `T_ads` places `R` at `eye + camFwd · relief`, with relief 0.21 m for irons and 0.07 m for scopes.
- `T_hip = T_ads · offset(0.12, −0.07, 0.06)`.

**Spring blend.** The move between hip and ADS is a critically damped spring (`smoothDamp`, ω = 18/s) on the position, plus a slerp on rotation. It replaces the current smoothstep on `adsT`, and `adsT` keeps its meaning for the existing gates.

**Sight-origin ballistics.**
- When `adsT > 0.85`, the shot starts at `F_world` with `V = normalize(F_world − R_world)`. The existing zero tilt is applied on top, and the existing muzzle-obstruction sweep stays.
- From the hip, the current muzzle-sweep logic is unchanged.

### F3. Sway, bob, breathing and DoF (`src/viewmodel.cpp`, `render.cpp`)
**Sway.**
- Take the mouse delta, clamped to ±6 px per frame.
- Offset position by −k_p·Δ and rotation by −k_r·Δ, then return to centre with `smoothDamp` (0.12 s).
- The effect is scaled by 0.35 in ADS and by 0.3 with `reducedMotion`.

**Bob.** A Lissajous pattern driven by horizontal speed v and a phase φ that advances at `v / stride`:
- `y = A_y·sin(2φ)`, `x = A_x·sin(φ)`, with A_y = 0.012 and A_x = 0.008 per m/s, clamped.
- Bob is off in ADS and reduced when crouched.

**Breathing.**
- At idle, pitch follows `0.25°·sin(2π·0.22·t)`.
- **Hold breath** is Left Shift while in ADS (sprint is ignored while aiming). It pauses the breathing, lasts up to 5 s, then forces a 3 s recovery at doubled amplitude. A small HUD bar shows it.

**Depth of field.**
- The viewmodel pass writes linear eye distance to an extra R16F MRT attachment (`vmDist`, 0 for world pixels).
- A DoF pass runs after the opaque, effects and viewmodel passes and before exposure.
- Focus is `|F − eye| + 0.03 m`.
- CoC = `adsT · clamp((focus − d) / (focus · 0.6), 0, 1) · maxR` with maxR = 6 px at 1080p, and applies only to near viewmodel pixels.
- A 16-tap disc gather lets the blur bleed onto the background. The front post (d ≥ focus) and the world stay sharp.
- It can be turned off in View → Post and is disabled automatically with `reducedMotion`. Grass and the exposure NaN guards stay in place.

### F4. IK arms (`src/viewmodel.cpp`)
**Sockets.** Weapon JSON gains `"sockets":{"rightGrip":[x,y,z],"leftSupport":[x,y,z]}` in weapon space. The calibration panel shows them as gizmos.

**Arm geometry.** The arms are procedural low-poly meshes made from tapered boxes:
- upper arm 0.30 m, forearm 0.27 m, a hand block and a sleeve cuff
- OD wool for US weapons and field grey for German, chosen by `side`

**Two-bone IK.** This is analytic, with the formula in comments:
- Shoulders sit in view space at (±0.17, −0.24, +0.06).
- Law of cosines for the elbow angle; the pole vector is `down + 0.4·outward`.
- The right hand is fixed to the grip socket. The left hand is solved to the support socket, and during a reload it targets the mag or clip part.
- If the target is out of reach, the arm straightens without stretching.

The arms render in the viewmodel pass. Explore → View has a "Show arms" toggle.

### F5. Surface damage and penetration (`src/ballistics.cpp`, `damage.cpp`, `physics.cpp`)
**Surface types.** `enum SurfaceType { Glass, WoodShutter, WoodDoor, Concrete, Masonry, Timber, Earth, Sandbag, Steel, Foliage }`, resolved from the physics body user data and the existing panel `family` and `fx.impact` surface strings.

**Openings data.** Existing openings (132 windows, 20 doors and 2 shutters as `kind` entries; `Leaf` has `opaque`/`glass` meshes) gain a session overlay, `leafDmg[inst][idx] = {hp, state}`, and break as follows:

| Opening | HP (bullet hits; shells are instant) | When HP ≤ 0 |
|---|---|---|
| Glass | 1 | Hide the glass mesh, rebuild the leaf shape without its glass colliders, burst of shard particles plus `glass_break` sound |
| Shutter | 2 | Detach into a Jolt dynamic body built from the leaf colliders. Impulse `I = V_bullet · M_force` (M_force 0.01 kg) at P_hit. Mesh swaps to a fractured variant (procedural notch where it was hit). |
| Door | 10 (configurable in the asset JSON, `"hp"`) | Becomes a dynamic body. Angular impulse `τ = (P_hit − hingePoint) × (dir · I)` |

**Debris and persistence.**
- Broken leaves drop their interact prompt.
- At most 24 dynamic leaves exist at once; the oldest sleeping one freezes in place when the cap is hit.
- F5 discards everything. "Apply damage to map" stores broken glass and removed leaves; debris poses are not stored (documented).

**Penetration.**
- Projectiles gain `energy = ½·m·v²`. `bulletMass` (g) is added to the weapon JSON, with sources cited in weapon_cards.
- At a Glass, Shutter or Door hit:
  - Cast from `P_hit + dir·0.5` back to the same body to find `Δ`.
  - `P_exit = P_hit + dir·Δ`.
  - `E_new = E_old − μ·Δ`, with μ in J/m: glass 20 000, shutter 40 000, door 40 000.
- If `E_new > 0`, a new projectile leaves P_exit (+5 mm), ignoring that body, with `v·√(E_new/E_old)` and damage scaled the same way.
- Walls, sandbags, terrain and steel always stop the round. Foliage (canopy) lets it through at no cost.

### F6. Craters (`src/terrain.cpp:192` `Terrain::crater`, `src/ground.cpp`)
**Profile.** Replace the bowl-and-rim profile with the guide's formula:
`Z_new = Z_old − D_max · (max(0, 1 − d²/R²))^p`

| Profile | D_max | R | p | Used by |
|---|---|---|---|---|
| Standard | 0.5 m | 1.5 m | 2 | 7.5 cm HE, the existing `heCrater` |
| AT | 1.2 m | 3.0 m | 2 | New PaK 40 AT round (PzGr. 39), documented as game tuning |

**Supporting changes.**
- Raise the `maxDepth` cap to 1.8 m for AT, and keep foundation weighting.
- Peak slope is about 32° for AT and 27° for standard, so both are climbable. A test walks the player out.
- The dirt and grass mask is kept, with radius scaled to R.
- **PaK ammo switch:** **X** at the gun toggles HE and AT (check the key is free; X isn't bound today). `crate_pak40_he` gains an AT count, and the crate and resupply accounting extend to two ammo types.
- On walls, AT deals concentrated damage: small radius, high damage. Update `docs/destruction.md` and the damage suite's crater-depth checks.

### F7. Trees and clouds (`src/foliage.{h,cpp}`, `render.cpp`)
**Tree assets.** Three new procedural assets in a "Vegetation" category: `tree_apple` (orchard), `tree_oak` (bocage hedgerow) and `tree_poplar`.
- The trunk and limbs are tapered parts.
- The canopy is alpha-tested leaf cards in an instanced buffer (like grass), each with a vertex mask M: 0 at the trunk, rising to 1 at the tips.

**Sway shader.** `V_offset = D_wind · M · A · sin(f·t + k_x·X + k_z·Z)`. It shares the wind direction and toggle with grass. Leaf cards cast shadows (alpha-tested in the shadow pass).

**Tree damage.**
- `hpBullet = 30` (one per small-arms hit) and `hpAT = 1` (an AT or HE direct hit, or a blast within 1.5 m of the trunk).
- When either reaches 0, the tree becomes a static stump (cut at 0.4 m) plus a dynamic fallen-trunk compound body. The canopy follows the trunk, and an impulse pushes away from P_hit.
- Damage lives in the overlay. Apply stores `felled` plus the trunk's rest transform in map JSON. Grass excludes only the trunk footprint.

**Clouds.**
- fbm (5 octaves) on a cloud plane at 1500 m, sampled from the view direction, scrolling with wind.
- Coverage, density and silver lining toward the sun are adjustable.
- The full version draws only in the background sky; reflections use a 2-octave version.
- Horizon fog blending stays as it is, and non-finite results are guarded.
- View → Sky gets a toggle and a coverage slider.

### F8. Buildable defences (`src/builder.{h,cpp}`, new assets)
**Assets.**
- `def_tank_trap`: Czech hedgehog, three crossed L-beams 1.4 m long, steel.
- `def_sandbag_straight`: 2.0 × 0.6 × 0.8 m, 3 courses.
- `def_sandbag_curved`: a 45° arc of radius 2.5 m.
- Each has a `"sockets":[{pos, tangent}]` entry at both ends.

**Explore placement mode.**
- **B** toggles it (B isn't bound today). 1/2/3 pick the item, the mouse wheel rotates, LMB places, and B or RMB cancels.
- A raycast from the screen centre reaches `D_max = 5 m`, hitting the terrain body only. From N: `θ = acos(N·Ŷ)`.
- Placement is invalid when θ > 35°, over water, or overlapping (Jolt broad-phase AABB query).
- The hologram uses the alpha program with a fresnel term, green when valid and red when not.

**Snapping.**
- If P_hit is within `R_snap = 0.6 m` of a free socket on a placed sandbag, the new piece's opposite socket is moved onto it.
- Yaw is set so the tangents continue (dot > 0.999), which also chains curved pieces.

**Shared behaviour.**
- In Creative, the same slope tint and socket snapping apply to normal asset placement for the `def_*` assets.
- Explore placements are a session overlay and are kept by the existing "Apply damage to map" command, renamed "Apply session changes to map" and still undoable. F5 discards them.

**Defence physics.**
- **Tank trap:** HP 1500; small-arms damage multiplier is 0; HE and AT apply through the existing falloff. `blocksVehicles: true` is written to the map, with a footprint overlay in F3 debug.
- **Sandbags:** HP 300. They stop every bullet (no penetration). At 0 HP they become a low spill mesh 0.25 m high.

**Blast occlusion.**
- `explosionExposure(epicenter, target)` casts from the epicenter to the player's chest and head points.
- If a sandbag collider is crossed, the damage is `E_damage · 0.1`.
- With no health system, the result drives a HUD "blast exposure" readout, camera shake and a tinnitus sound. Tests assert the multiplier.

### F9. Docs, tests and wrap-up
**Docs.**
- New `docs/fps-weapons.md` (all formulas, parameters and controls) and `docs/credits.md` (GLB author and licence fields for you to fill in).
- Updates to the README controls (B, X, Left Shift hold breath, view toggles), `docs/STATUS.md`, `docs/testing.md` and `docs/implementation-notes.md`.
- Add the BAR to `tools/weapon_cards.cjs` → `docs/weapons.md`.

**Tests.** New `src/selftest_fps.cpp` (`--selftest-fps`) and `src/selftest_env.cpp` (`--selftest-env`), added to `tools/run_all_tests.sh`.

**Finish.** Commit, then a light frame-time sanity check, not a full benchmark.

**Files.**
- New: gltf_import, viewmodel, ballistics, foliage and builder (.h/.cpp), and the two selftests.
- Changed: `CMakeLists.txt`, `render.{h,cpp}`, `weapons.cpp`, `combat.h`, `assets.{h,cpp}`, `terrain.cpp`, `ground.cpp`, `damage.cpp`, `station.cpp`, `explore.cpp`, `editor.cpp`, `app.{h,cpp}`, `content/weapons/*.json` (+ `m1918a2_bar.json`), `content/assets/tree_*.json` and `def_*.json`, and the crate JSONs.

## Verification
**Build.** `cmake --build build` with the existing MinGW toolchain.

**Legacy suites** must stay green: A 212, B 68, damage 97 (crater checks updated), pak 52, weapons 139, grass 21 and range 13.

**`--selftest-fps` checks:**
- **Import:** each GLB bakes; fitted length is within 1% of `realLength`; textures bind; missing bake falls back.
- **ADS:** at adsT = 1, R and F project to within 0.5 px of the screen centre for every weapon, at world FOV 72 and 58.
- **Origin:** during ADS the shot origin equals F_world (< 1 mm) and its direction is normalize(F − R) (< 1e-4 rad).
- **Movement:** sway returns to < 1% within 0.5 s; bob amplitude scales with speed; breathing stops while breath is held and recovers after 5 s.
- **IK:** both hand errors < 1 mm, elbow below the shoulder–wrist line, and no stretch when out of reach.
- **DoF:** CoC is 0 at d ≥ focus and the maximum at the rear sight.

**`--selftest-env` checks:**
- **Openings:** glass breaks in 1 hit, shutter in 2, door in 10 (dynamic bodies are created).
- **Penetration:** E_new matches the formula exactly; a pistol round stops in a shutter; sandbags stop .30-06.
- **Craters:** formula values at d = 0, R/2 and R; AT depth 1.2 m ± fine-grid error; standard depth 0.5 m; max slope ≤ 35°; scripted walk out of an AT crater.
- **Trees:** felled at 30 bullets or 1 AT; stump and trunk bodies exist.
- **Clouds:** zero non-finite pixels.
- **Builder:** 34° valid and 36° invalid; snap within 0.6 m with tangent dot > 0.999; F5 discards; Apply then undo round-trips.
- **Tank trap:** HP unchanged after 100 rifle hits.
- **Blast occlusion:** factor 0.1 behind sandbags and 1.0 in the open.

**Manual check.** Run `NormandyMapmaker.exe content/examples/range.nmap.json`, press F5, aim each weapon, shoot a window, door and shutter, fire PaK AT and HE, fell a tree, and place a sandbag chain. Capture before/after PNGs into `docs/materials/` with the existing capture mode.
