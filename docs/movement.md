# Movement: climbing and swimming

Speeds, postures, jump tuning and jump reliability are in [expansion.md §5](expansion.md#5-movement). This page covers getting onto things: the **climb** (anything whose top is 0.4–2.2 m up, from a knee-high wall to a trench lip or a roof parapet). It also covers **swimming** in deep water. Both are tuned in `content/settings/movement.json` (`"vault"`, `"mantle"` and `"swim"` blocks, read by `App::loadClimbSettings`, `src/mantle.cpp`).

## Controls

Hold **Space** while standing and facing the obstacle, from a run or standing still. The press itself still jumps at once. After `vault.hold_s` (0.16 s) of holding, the game tries the climb, which may start during that jump. Each hold gives one climb; release Space to climb again. A climb costs stamina like a jump.

## Climb onto (`src/mantle.cpp`)

Since MV1 (2026-11-15) there is no vault over obstacles: every climb leaves the player **standing on top**. From there they walk or jump off either side themselves. This is also how a player who lands on a roof inside its parapet gets out: climb onto the rim, then step or jump off. The `vault` block only keeps `hold_s` and the bots' vault-link cap (`maxHeight_m`).

| Setting (`mantle`) | Value | Meaning |
|---|---|---|
| `minHeight_m` / `maxHeight_m` | 0.4 / 2.2 | Top height above the **ground under the feet**: mid-jump this is the floor the jump left, so jumping does not change what counts. The player is 1.8 m tall; 2.2 covers trench lips (1.9–2.0 m above the floor) and roof rims. |
| `reach_m` | 0.85 | Face distance (rays at 0.35, 0.75 and 1.15 m) |
| `landAbove_m` / `landBelow_m` | 0.3 / 0.6 | The landing may be this much higher or lower than the lip (sloped parapets) |
| `minTop_m` | 0.05 | Narrowest top the body balances on (centred on it) |
| `fenceDrop_m` | 0.4 | A top thinner than `minTop_m` (a rail) is crossed only onto a surface at the lip's height, or within this of the start's level ground |
| `minDuration_s` / `maxDuration_s` | 0.45 / 1.3 | Length of the motion, from a hop at 0.4 m to a pull-up at 2.2 m (+0.1 s over a rail) |
| `pitchUp_deg` / `pitchDip_deg` | 5 / 14 | The view tilts up while pulling, then dips forward onto the top |
| `handSpread_m` | 0.24 | Hands either side of the aim line on the ledge |

**Validation**, all done before anything moves:
1. A face within reach, with a top 0.4–2.2 m above the ground under the feet. A probe that starts inside a solid (a house wall) reads as too tall.
2. Walk across the top in 3 cm steps, up to 1.6 m past the face, to where it falls away (or something taller than the range stands on it).
3. Land on the top: centred on a top narrower than 0.9 m (a wall, a parapet coping), 0.45 m in on a deeper one (further in where needed), within +0.3 / −0.6 m of the lip and where the standing body fits (`Physics::shapeFitsAt`).
4. Only a top thinner than `minTop_m` is crossed: 0.35–1.2 m beyond it, onto a surface at the lip's height or level with the start (a fence in a field). Never down off the far side of a roof.
5. Refuse if uncut barbed wire lies within 1 m of the crossing, from the face to beyond the landing.
6. Sweep the standing body up (at the start position), across (just above the highest top crossed) and down to the landing, in 8 cm steps. Any blocked sample refuses the climb.

The reason for the last refusal is kept in `App::mantle.why` (shown by the self-tests).

**Motion**, as fractions of the duration:

| Phase | Span | Body | Camera / view |
|---|---|---|---|
| Reach | 0–15 % | Barely moves while the hands find the ledge | Weapon swings down out of view; both arms reach for the lip |
| Pull | 15–60 % | Rises straight up to just above the top | The eye rises to about 0.7 m over the lip; the view tilts up a few degrees |
| Roll | 60–85 % | Moves forward onto the top | The head stays low (about 0.6 m over the lip) and the view dips forward up to 14°; the hands release |
| Stand | 85–100 % | Settles onto the landing (or just past a rail) | The eye rises to standing height; the view returns to where it was, and the weapon comes back up |

The path is re-checked every step. If something moves into it (a destroyed ledge, a vehicle), the mantle stops where the body still fits. The weapon cannot fire while mantling. Reduced motion scales the view tilt by 0.3.

**Body pose.** The soldier model (`pose.cpp`, `PoseParams::climb*`) puts both palms on the ledge. The chest leans forward and the right knee comes up during the roll, and the weapon hangs at low ready. In first person the arms (`viewmodel.cpp`) IK to the same ledge points.

**Bots** keep their own navmesh mantle links (`nav_links.cpp` and `ai_path.cpp`, unchanged; `content/ai/navigation.json`: mantle 0.4–1.6 m, vault ≤ 1.1 m). Those ranges stay inside the player's, which `--selftest-move` checks.

## Window vault (`App::tryStartWindowVault`, `src/mantle.cpp`)

Since MV2 the same Space hold also goes **through windows**, on every building (WW2, modern `mod_bld_*`, ruins), from either side. It is tried before the climb:

- **Opening:** horizontal rays 0.3–3.2 m up, with door and window leaves skipped, find the wall face within `mantle.reach_m`. Above it there must be an open band (the ray runs on past the face by more than 0.45 m) and a lintel over that band. With no lintel it is a wall top, and the climb takes over.
- **Sill:** 0.4–2.2 m above the ground under the feet (`mantle` range), in a wall at most 1.2 m deep.
- **Crossing:** the **crouched** body (1.2 m) is swept up, through at the sill's highest point, and down. The window's own sashes (every `"kind": "window"` leaf: frames, glazing bars, glass) are ignored, so they never snag. Anything else solid blocks: closed shutters, bars that are part of the wall, a lintel too low for the crouched body. Openings too small stay blocked.
- **Landing:** 0.35–1.2 m past the far face, at most 1.8 m below the sill, where the standing body fits.
- **Glass:** intact panes along the crossing break as the vault starts (`breakGlass`, the same shatter as a shot). The sash stays.

The motion, camera, hands and body pose are the climb's. Refusals start with `window:` in `App::mantle.why`. **Player only:** bots have no window nav links.

## Lean (MV3, `App::stepLean`, `src/explore.cpp`)

**Keys.** Hold **Q** to lean left and **E** to lean right; release to straighten. With both held the lean returns upright.
- Q and E are shared with Next weapon and Interact. With an interact prompt on screen, E interacts at once and does not lean.
- Otherwise a tap (released within `tap_s`, 0.2 s, before any lean) does the old action on release, and a hold leans.
- In a vehicle, at a gun, in build mode, in the inventory and while airborne in Battle Royale, the keys keep their old meaning only.
- Rebinding Lean left / Lean right in Settings > Controls to other keys gives plain held leans, and Q / E become plain actions again.
- Leaning works standing and crouched, in hip fire and ADS. It is off while sprinting, prone, swimming, climbing or building.

**One shared parameter.** `App::lean.x` runs from -1 to 1. It drives:
- **Eye (`App::leanOffset`, inside `eyePos()`):** the eye swings on an arc about a waist pivot `radius_m` (0.85) below it. That puts it out to `sideways_m` (0.26 m) and drops it about 4 cm. Yaw and pitch are untouched.
- **View roll (`viewUp`):** `roll_deg` (6.5) toward the leaning side. The viewmodel is drawn in view space, so the weapon cants with the camera. In ADS the rear and front sights stay on the screen centre, and recoil acts in the leaned weapon frame.
- **Shots and muzzle flash:** both follow the eye. Hip shots aim with the camera ray and leave from the real muzzle, so a wall between the eye and the muzzle stops them. ADS shots leave the front sight.
- **Third-person body (`PoseParams::lean`):** `bodyBend_deg` (16) sideways through the three spine joints, with the hips and feet planted. The hit capsules come from the posed joints.
- **LAN:** client input posture bits 2-6, and the per-soldier snapshot byte bits 1-5 (`kNetVersion` 16).

**Motion.** A critically damped spring settles in `in_s` (0.22 s) going out and `out_s` (0.17 s) coming back. A reversal passes smoothly through upright, and a release mid-lean returns from where it is.

**Walls.** Rays from the upright eye and shoulders (0.32 m lower) limit the lean on each side. The head stays `clearance_m` (0.2 m) off any geometry, so the camera never enters a wall.

All values are in `content/settings/movement.json` (`"lean"`). Bots do not lean. `--capture-lean` takes a wall edge left and right (standing / crouched, hip / ADS) and a leaned bot from the side and the front.

## Swimming (`src/swim.cpp`)

**Water.** Every gameplay water check goes through `waterAt(app, x, z)` (`src/water_query.{h,cpp}`). It returns the highest of three sources:
- the map's global `terrain.waterLevel`;
- V1's placed water assets (`waterSurfaceAt`, `water.h`);
- the lasso tool's map-saved water bodies (`lassoWaterAt`, `lasso_water.h`).

The two optional sources are picked up with `__has_include`, so the file builds before or after batch B and the lasso branch land. A value at or below −999 means no water. The wading footsteps, the swim state, the underwater view, the agents and the navmesh all use it.

**Entering and leaving.** You start swimming when more than `enterDepth_m` (1.25 m, chest height) of water covers the feet. You walk again below `exitDepth_m` (1.05 m) once the feet are on the ground. Between those depths you keep doing whatever you were doing, so wading up a bank never flickers between the two.

**Controls.**
- **W A S D** swim at 2.2 m/s (`speed_mps`), or 3.2 m/s with **Shift**.
- **Space** rises and **Ctrl** dives.
- Submerged, or looking more than `diveSteerPitch_deg` (30°) down at the surface, the look pitch steers W/S.
- Hold **Space** facing a bank or a pier to climb out (see below).
- Fire, aim, reload, weapon switch and the quick grenade do nothing. The weapon is slung.

**Movement.** `Physics::updateSwim` replaces `updateCharacter`:
- no gravity, and the caller's full 3-D velocity;
- Jolt collisions kept, so banks, piers and wrecks block;
- step-up onto the bed kept, and no stick-to-floor;
- a crouched or prone body stands up when it fits.

Buoyancy brings the feet back to `floatDepth_m` (1.38 m) below the surface, which puts the eye about 0.24 m above it. It acts gently from below (`buoyancy_mps`, 0.45 m/s) and quickly from above, so a jump in settles. Space at the surface lifts the chin by 0.12 m at most.

The velocity follows the wish at `drag_per_s` (4 /s), so a plunge from a pier slows down in the water instead of stopping dead. Water also breaks a fall: there is no fall damage when the feet find ground again.

**Climbing out.** The mantle (`tryStartMantle(feet, yaw, fromWater = true)`) measures heights from the floating feet, not from the bed:
- a lip 0.3 m above the surface measures about 1.7 m, inside the mantle range;
- a lip 1 m above the surface is out of reach.

The head must be near the surface. The mantle then runs as it does on land.

**First person.**
- The weapon is lowered out of view (`swimLower`, using the mantle's transform), then not drawn at all.
- `armDrawItems` draws a hands-only breaststroke with the same box arms and two-bone IK: reach, sweep out, sweep in, back to the chest. When climbing out, the mantle grips take over from the stroke.
- At the surface the eye bobs (`bobAmp_m` 0.035 m at `bobHz` 0.55, plus the stroke) and rolls slightly with the stroke (`roll_deg`).
- Reduced motion scales the bob and the roll by 0.3.

**Under water.** When the render camera is below `waterAt` at the camera, `RenderView::underwater` is set (`app.cpp`). It has three effects:
- **Fog:** `Renderer::setLighting` swaps in the water fog (`underwaterFogColor`, `underwaterFog_per_m` 0.11 /m). `applyFog` becomes a uniform exponential (`uUnder`), and the sky becomes the fog colour.
- **Tonemap:** a blue-green tint and a stronger vignette (`uUnderTone`).
- **Audio:** `Audio::setUnderwater` lowers a low-pass node that every sound routes through, from 20 kHz to 550 Hz, and takes 35 % off the level.

**Breath and drowning.** Breath (`breath_s`, 30 s) runs down while the eye is under water, including when lying prone in shallow water. Above water, `breathRecover_per_s` (7.5 s) comes back per second.

Once breath runs out, `drownDamage_hps` (10 HP/s) is applied every `drownTick_s` (1 s) through `rulesDrown` → `Game::damage(..., "drown")`. As with fall damage, this happens only in a match, on the host or in single player, and spawn protection still applies.

A breath bar shows above the weapon HUD whenever breath is below full. It flashes red while drowning.

**Third person.** `PoseParams::swim` and `swimPhase` (`pose.cpp`) drive the swim pose:
- the body tilts 70° forward about the waterline;
- the arms breaststroke and the legs frog-kick;
- the weapon rides slung on the back.

`Soldier::swim` and `swimBlend` feed it. The local player's flag comes from `App::swim`; a bot's comes from its agent.

**LAN.** Flags `2|4` both set in a snapshot mean swimming. The client sends posture 3 for swimming; the host stores `swim` and keeps the posture as standing. The host's move check needed no change, because vertical speeds while swimming stay inside its limits.

**Bots.**
- **Navmesh:** water deeper than `shallowWater_m` (0.9 m) is forbidden. That check now uses `Nav::waterQuery` (`waterAt`), sampled on a 1 m grid per tile job on the calling thread, so lakes, ponds and water assets count as well as the global level.
- **Floating:** a bot pushed into deep water floats. `Physics::updateAgent` uses `Physics::waterQuery` and `swimTuning`, both set from the same `"swim"` block. Agents get the same hysteresis, float at `floatDepth_m`, swim at 0.6× their wish speed and never walk on the bed.

## Tests

`--selftest-move` covers:
- the settings and the bot link limits;
- getting out of the straight, corner and zig-zag trenches: from the floor (front parapet, parados, inner and outer walls, the second leg, the fire bay) and from each firestep. Each check is one mantle of 0.85–1.35 s that ends at ground level outside the cut;
- onto (not over), from standing still: a 0.6 m and a 1.0 m wall (quick hops under 0.8 s), a 2.1 m × 0.35 m wall, a 2.0 m and a 1.2 m platform, still standing there a second later;
- over a 3 cm rail onto level ground; no climb at a 2.4 m wall;
- from a 4 m roof: onto a 1.1 m parapet and a 2.2 m rim; a 3 cm roof rail is not crossed (no drop off the roof);
- every flat-roofed modern building (`mod_bld_*`, `mod_nt_*`): from the roof 0.6 m inside the rim, 4 sides x 3 lines, the climb lands on the rim (rim heights reported);
- during the motion: the weapon lowered with LMB held and no shot fired, both hands on the ledge, the eye arc, the forward dip, and the view level again afterwards.

`--selftest-entities` covers:
- no mantle at any of the three wire obstacles;
- a 1.6 m ledge next to a coil refused, and the same ledge climbed once the coil is moved away.

`--selftest-update`: a tap is a plain jump, a hold climbs onto the low wall once (standing on it), no climb at a 6 m house wall. `--selftest-structures` keeps the anti-tank ditch faces (3 m) unclimbable.

`--selftest-swim` covers swimming:
- the `"swim"` settings, and the weapon drawn on land;
- depth and hysteresis:
  - no swimming at 1.15 m;
  - swimming at 1.32 m;
  - still swimming when the water drops back to 1.15 m;
  - walking again at 0.95 m once grounded;
- floating in 4 m of water: off the bed, the eye bobbing inside its band, the head never under;
- swim and sprint speeds;
- with LMB and RMB held:
  - no shot and no ADS;
  - the weapon lowered;
  - only the arm boxes in the first-person draw list;
  - the hand stroking;
- Ctrl dives:
  - the eye goes under water;
  - the fog switches to the water fog;
  - the audio is muffled;
  - the screenshot (`swim_underwater.png`) is blue-green, not black;
- under water, the look pitch steers;
- breath:
  - no drowning damage at 29 s under;
  - damage ticks after 30 s;
  - breath comes back after surfacing;
- climbing out onto a bank 0.3 m above the surface;
- a bot agent floating at the surface;
- a pond from the water query, with the global level off, is forbidden in the navmesh:
  - no polygon at its centre;
  - the path goes round it;
  - the straight walk across is blocked;
  - without the query, the same ground is walkable.
