# Movement: vault, mantle and swimming

Speeds, postures, jump tuning and jump reliability are in [expansion.md §5](expansion.md#5-movement). This page covers getting over and onto things: the **vault** (low obstacles) and the **mantle** (ledges up to 2.1 m, such as trench lips). It also covers **swimming** in deep water. All three are tuned in `content/settings/movement.json` (`"vault"`, `"mantle"` and `"swim"` blocks, read by `App::loadClimbSettings`, `src/mantle.cpp`).

## Controls

Hold **Space** while standing and facing the obstacle. The press itself still jumps at once. After `vault.hold_s` (0.16 s) of holding, the game tries a vault first and then a mantle. A vault or mantle may start during that jump. Each hold gives one vault or mantle; release Space to climb again.

## Vault (`src/explore.cpp`)

| Setting (`vault`) | Value | Meaning |
|---|---|---|
| `hold_s` | 0.16 | Hold time before a vault is tried |
| `minHeight_m` / `maxHeight_m` | 0.62 / 1.35 | Height of the obstacle top above the feet |
| `reach_m` | 0.85 | Face distance (rays at 0.35 and 0.75 m) |
| `maxDepth_m` | 1.1 | Deeper tops are climbed (mantle), not vaulted |
| `maxDrop_m` | 2.2 | Largest drop from the top to the landing |
| `speed` | 1.0 | Path speed scale (duration 0.45–1.1 s) |

The standing body is swept up, over and down in 8 cm steps before anything moves. Any blocked sample refuses the vault. It never crosses uncut barbed wire within 1 m.

## Mantle (`src/mantle.cpp`)

The mantle covers what the vault refuses: ledges too tall (trench lips, which sit 1.9–2.0 m above the floor and about 1.57 m above the firestep, and garden walls) or too deep (platforms).

| Setting (`mantle`) | Value | Meaning |
|---|---|---|
| `minHeight_m` / `maxHeight_m` | 0.9 / 2.15 | Ledge height above the **ground under the feet**: mid-jump this is the floor the jump left, so jumping does not change what counts. 2.15 keeps a 2.1 m wall in range. |
| `reach_m` | 0.85 | Face distance (rays at 0.35, 0.75 and 1.15 m) |
| `landAbove_m` / `landBelow_m` | 0.3 / 0.6 | A landing on top may be this much higher or lower than the lip (sloped parapets) |
| `narrowTop_m` | 0.55 | Tops narrower than this are crossed: the player drops over to the far side |
| `maxDrop_m` | 2.2 | Largest drop on the far side of a narrow wall |
| `minDuration_s` / `maxDuration_s` | 0.9 / 1.3 | Length of the motion, scaled by how far the body rises (+0.1 s when going over) |
| `pitchUp_deg` / `pitchDip_deg` | 5 / 14 | The view tilts up while pulling, then dips forward over the top |
| `handSpread_m` | 0.24 | Hands either side of the aim line on the ledge |

**Validation**, all done before anything moves:
1. A face within reach, with a top 0.9–2.15 m above the ground under the feet. A probe that starts inside a solid (a house wall) reads as too tall. Up to the vault's 1.35 m only a top deeper than the vault's 1.1 m (a platform) is mantled; low walls stay the vault's, even while the vault waits for the jump to come down.
2. Walk across the top in 8 cm steps, up to 1.6 m past the face, stopping at anything taller than the range. The landing is the first spot at least 0.42 m past the face whose top is within +0.3 / −0.6 m of the lip and where the standing body fits (`Physics::shapeFitsAt`). This is the "standing room on top" rule.
3. If there is no such spot and the top falls away within `narrowTop_m`, look for a far-side landing 0.35–1.2 m beyond the edge, at most `maxDrop_m` down. This is the "over a wall" case.
4. Refuse if uncut barbed wire lies within 1 m of the crossing, from the face to beyond the landing. Wire is never climbed or vaulted.
5. Sweep the standing body up (at the start position), across (just above the highest top crossed) and down to the landing, in 8 cm steps. Any blocked sample refuses the mantle.

The reason for the last refusal is kept in `App::mantle.why` (shown by the self-tests).

**Motion**, as fractions of the duration:

| Phase | Span | Body | Camera / view |
|---|---|---|---|
| Reach | 0–15 % | Barely moves while the hands find the ledge | Weapon swings down out of view; both arms reach for the lip |
| Pull | 15–60 % | Rises straight up to just above the top | The eye rises to about 0.7 m over the lip; the view tilts up a few degrees |
| Roll | 60–85 % | Moves forward over the top | The head stays low (about 0.6 m over the lip) and the view dips forward up to 14°; the hands release |
| Stand | 85–100 % | Settles onto the landing, or drops for an "over" | The eye rises to standing height; the view returns to where it was, and the weapon comes back up |

The path is re-checked every step. If something moves into it (a destroyed ledge, a vehicle), the mantle stops where the body still fits. The weapon cannot fire while mantling. Reduced motion scales the view tilt by 0.3.

**Body pose.** The soldier model (`pose.cpp`, `PoseParams::climb*`) puts both palms on the ledge. The chest leans forward and the right knee comes up during the roll, and the weapon hangs at low ready. In first person the arms (`viewmodel.cpp`) IK to the same ledge points.

**Bots** keep their own navmesh mantle links (`nav_links.cpp` and `ai_path.cpp`, unchanged; `content/ai/navigation.json`: mantle 0.4–1.6 m, vault ≤ 1.1 m). Those ranges stay inside the player's, which `--selftest-move` checks.

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
- over a 2.1 m × 0.35 m wall;
- onto a 2.0 m platform, and onto a 1.2 m platform too deep to vault;
- no mantle at a 2.4 m wall;
- during the motion: the weapon lowered with LMB held and no shot fired, both hands on the ledge, the eye arc, the forward dip, and the view level again afterwards.

`--selftest-entities` covers:
- no mantle at any of the three wire obstacles;
- a 1.6 m ledge next to a coil refused, and the same ledge climbed once the coil is moved away.

`--selftest-update` keeps the existing vault checks (a tap is a plain jump, a hold vaults the low wall once, no vault at a 6 m house wall). `--selftest-structures` keeps the anti-tank ditch faces (3 m) unclimbable.

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
