# Tanks (Phase 2, T1–T10)

This page covers the two drivable tanks: the Sherman Firefly stand-in and the Tiger I. Both are built from the two GLBs supplied in `Normandy_Phase_2_Tanks`. Nothing about them is procedural geometry: every visible triangle comes from the supplied meshes.

| | |
|---|---|
| Code | `src/tank.h`, `src/tank.cpp` (definitions, preview), `src/tank_import.cpp` (bake post-pass), `src/tank_sim.cpp` (simulation, damage, persistence), `src/tank_ui.cpp` (drawing, sights, HUD, thumbnails, sounds), `src/selftest_tanks.cpp` |
| Data | `content/vehicles/sherman_firefly.json`, `content/vehicles/tiger_i.json` (bake spec, rig sockets, colliders, gun, ammunition, roof MG, mobility, durability) |
| Catalog | `content/assets/tank_sherman_firefly.json`, `content/assets/tank_tiger_i.json` (category **Vehicles**, `"vehicle": "<id>"`) |
| Baked models | `content/vehicles/models/sherman/`, `content/vehicles/models/tiger/` (mesh + textures at 1024² max) |
| Test | `NormandyMapmaker.exe --selftest-tanks` (also in `tools/run_all_tests.sh`) |

Hooks into shared code are small and marked `tanks` / `tank_sim.cpp` in comments: `app.cpp` (draw list, camera, sync, pick, duplicate, grass and surface footprints, flags), `explore.cpp` (step, enter, HUD), `station.cpp` (projectile hook, session reset), `ground.cpp` (Apply, ground protection), `audio.cpp` (looping voices), `render.cpp` (opaque tint on imported models), `selftest*.cpp` (stage 2600).

## Assets (T1)

### Import

The source GLBs are copied into the git-ignored `assets_src/vehicles/`. Only the baked derivatives are committed.

```bash
NormandyMapmaker.exe --import-vehicle-models assets_src/vehicles content
```

The command runs the shared weapon bake (`bakeGlbModel`, `src/model.cpp`) on each `content/vehicles/*.json` `model` block, then a vehicle post-pass (`src/tank_import.cpp`):

1. **Re-origin.** x and z are centred on the hull part, and y = 0 is the lowest running-gear vertex, which is the ground contact.
2. **Splits.** Connected triangle islands of one part that lie entirely inside a box become a new part with their own role. An optional `notIn` box keeps neighbouring islands out. Triangles only change which part they belong to; nothing is added or reshaped.

Scale is the source's own metre scale (`realLength` = the native hull length).

| | Sherman stand-in (`tank_fury.glb`) | Tiger I (`pzkpfw_vi_tiger_1.glb`) |
|---|---|---|
| Triangles / parts | 75,754 / 10 | 64,938 / 5 |
| Textures | 18 maps, 9 materials | 9 maps, 3 materials |
| Size (measured) | 3.11 m wide, hull 6.13 m | 3.72 m wide, turret roof 2.96 m, hull 6.23 m (Tiger I: about 3.0 m high, 3.7 m wide, 6.3 m hull) |
| Moving parts | hull, turret (`Turret_01`), gun + mantlet (`gun_01Shape`), roof .50 M2 (split from the turret mesh), tracks and running gear | hull, hull-top details, turret, gun + mantlet + sleeve (split from the turret mesh) |

`--tank-preview <dir>` renders side, front, top, rear and 3/4 views of both baked models, with the rig sockets and colliders marked. A second set of views shows only the moving parts. Use it to check pivots after editing a rig.

### Identity of the supplied Sherman (provisional stand-in)

The file is named "Fury" and is an **M4A3E8 "Easy Eight"**, not a Sherman VC Firefly. Visible evidence:

- HVSS suspension with paired road wheels. The Firefly used VVSS bogies.
- T23 turret with the 76 mm M1 gun and a muzzle brake. The Firefly had a 75 mm turret with the rear radio box, and the OQF 17-pounder.
- A bow MG in the hull. The Firefly conversion removed the hull machine-gun position.
- M4A3 hull. Most Fireflies were M4A4 (Sherman V) conversions with a longer rear hull.

The mesh is used unchanged and labelled **"Sherman Firefly (stand-in)"**. The gun, sight and ammunition *data* follow the 17-pounder Firefly and are labelled provisional; the HUD shows "OQF 17-pounder (game data; mesh shows the 76 mm M1)". The turret already carries a .50 M2 on its roof, and that M2 is split out at bake as the hatch MG.

### Tiger I

The model is a mid-production Tiger I: commander's cupola, spare track links on the turret sides, and a hull antenna. The gun, the mantlet and the barrel sleeve were one mesh with the turret, so they are split at bake (islands inside the front box move to a `gun` part) to let them elevate on their own. No separate hatch or roof-MG mesh exists. The cupola MG 34 is drawn with the supplied **MG 42** model as a clearly provisional stand-in, while keeping MG 34 data (900 rpm, 7.92 mm, its own synthesized report).

### Licences (from the GLB `asset.extras`)

| File | Author | Licence | Source |
|---|---|---|---|
| tank_fury.glb | FlashPoint (sketchfab.com/FlashPoin) | CC-BY-4.0 | sketchfab.com/3d-models/tank-fury-aa010de33f4e43a6954a9caedbb833db |
| pzkpfw_vi_tiger_1.glb | Tinus (sketchfab.com/HeerBeer) | **CC-BY-NC-4.0 (non-commercial)** | sketchfab.com/3d-models/pzkpfw-vi-tiger-1-dfd7156c43124d1c9ff77d895c647142 |

Both licences require attribution. The Tiger's licence also **forbids commercial use**. See [credits.md](credits.md).

### Rig and collision

All rig positions are in hull space, in metres, with the turret at 0° and the gun level (`rig` in the vehicle JSON): turret pivot, gun trunnion, muzzle, gunner optic, hatch eye, roof-MG pivot and muzzle, engine, exhaust outlets and exit points. They were measured on the baked mesh (barrel axis from the muzzle-brake vertices, node origins of the turret and gun) and checked in `--tank-preview`.

Collision is one compound per tank on the instance's own static body. It holds a hull box plus turret box(es) rotated to the achieved traverse, and is rebuilt when the turret has moved more than 2°. The body's user data is `id << 16`, so a ray hitting any collider resolves to the tank. The barrels have no collider.

## Placing tanks in Creative

1. Open the asset browser's **Vehicles** category and click the Sherman Firefly (stand-in) or the Tiger I thumbnail. The thumbnails are rendered from the baked models.
2. The cursor shows the baked tank model as the placement ghost: green for a valid placement, red when refused. The footprint outline and front arrow are drawn as for buildings. Z / X rotate the ghost; left click places it.
3. The placed tank is an ordinary instance: move, rotate, duplicate, delete, undo, save. Its selection panel has a **Tank: spawn configuration** section.
4. F5 to drive it.

## Controls and stations (T2)

| Input | In the tank |
|---|---|
| E (aiming at any part of the tank, within 4 m) | Enter, straight into the gunner sight. In the tank: get out at a safe spot |
| W / S | Throttle forward / reverse; the opposite key brakes first |
| A / D | Tracked steering: a pivot turn when stopped, a speed-dependent turn when moving |
| Mouse | Desired aim. The turret and gun follow it at their rate limits |
| LMB | Gunner position: fire the loaded cannon round, one round per click. Hatch position: fire the roof MG |
| Space (hold) | Fire the bow MG (hull ball mount) from either position. From the open hatch it fires the roof MG at the same time |
| Left Ctrl | Toggle gunner sight ↔ open hatch / roof MG, once per press. Never crouches the infantry |
| R | Change the next / in-progress shell type HE ↔ AP (never the loaded round) |
| RMB (click) | Gunner position: toggle the sight magnification (zooms into the current view, eased over 0.18 s). Hatch position: fire the main gun, one round per click |

- **Entry.** It uses the same full-object rule as guns and crates: the crosshair ray hits a tank collider within reach, which also gives line of sight. Entry is refused for a wreck or while you crew a gun.
- **Hand-over.** Infantry movement, weapons, building and posture are skipped while you are in the tank. The infantry body waits inside the hull (its controller is not updated), and its equipment is untouched.
- **Ctrl.** A press while entering does not count. A held Ctrl toggles once. Crouch works again only after you leave.
- **Hatch.** Opening checks the overhead clearance first ("Hatch blocked overhead"). The camera rises over 0.8 s. The hull can be driven in both positions; this is a single-player crew abstraction. From the hatch the commander lays the main gun: the turret follows where you look at its normal rate limits, a gun marker (triangle, "RMB" when a round is chambered) shows where it points, and RMB fires it. Loading continues in both positions.
- **Trigger.** Any station change, pause or focus change disarms it until the button is released, so LMB can never fire both weapons or fire through a switch.
- **Exit.** Exit points around the hull (in data) are tried in order. A point is accepted if it has ground support (a surface within 1.2 m of the terrain, normal up), a clear standing capsule, and a clear line from the hatch to the point. That last test means you are never moved through a wall. Exit is refused above 1 m/s ("Slow down to get out") or when every point is blocked, with the reason shown.

## Machine guns and the ammunition strip

| | Sherman stand-in | Tiger I |
|---|---|---|
| Bow MG (Space) | Browning M1918 type, BAR sound pack (`shot_m1918a2_bar`, tail, reload), 550 rpm, 860 m/s | MG 42 type, MG 42 sound pack (`shot_mg42`, tail, reload), 1200 rpm, 755 m/s |
| Bow MG belt | 50 rounds, 1000 spare, 3.2 s change | 50 rounds, 1000 spare, 4.6 s change |
| Ball mount | hull (−0.57, 1.39, 2.30), measured on the supplied mesh (assistant driver's side) | hull (−0.53, 1.58, 2.30), measured on the supplied mesh (radio operator's side) |
| Roof MG | Browning M2 .50, 100-round belts | MG 34 on the cupola (MG 42 model and sound pack), 75-round belts |

- **Bow MG aim.** The ball mount converges on what the crosshair is on (a camera ray, the own tank ignored) inside its arc: ±15° traverse, −8…−10° to +15° elevation. A small blue diamond marks where it points, in the sight and from the hatch. Rounds start at the ball muzzle and never strike the own hull.
- **Simultaneous fire.** From the open hatch, Space works the roof MG and the bow MG together, LMB the roof MG alone, and RMB the main gun, so all three can be used at once.
- **Automatic belt change.** Like the main gun's crew loader, an empty belt with spare rounds starts its change at once (whether or not the trigger is held) and the full belt is in when the timer runs out. With no spare rounds left the trigger only clicks. Bursts end with the bank's recorded tail.
- **Cyclic rate.** Each gun keeps the fractional remainder of its firing interval, so the rate is exact at the 60 Hz step (1200 rpm = 20 rounds/s).
- **Ammunition strip (bottom centre).** Main gun: AP (black, red band) and HE (olive, yellow band) shell icons with the stowed count, the next type framed, and the breech showing the chambered round or the loader's progress. Bow MG and roof MG: the belt in the gun drawn round by round (spent rounds dark), the spare count, and the belt change with its timer. Each group names its controls; the roof MG group is lit only from the hatch.

## Gunner optics (T3)

- **Camera.** It sits at the gunner optic and follows the **achieved** gun line, not the mouse. A hollow amber ring marks where the turret is being driven (the desired aim), so the actual firing axis is never misrepresented.
- **The sight.** A circular field with a dark surround keeps the aspect at any resolution, since it is sized from the screen height. The tank's own gun is not drawn while you are in the optic, so the barrel does not obstruct the view.
- **Reticles.** Both are original vector drawings that use the supplied references only for layout. None of the references' landscape, scratches or HUD is copied.
  - **Firefly:** a vertical post from below with a gap, a small open chevron at the aim point, and a horizontal line with lead ticks every 4 mil.
  - **Tiger:** a central triangle with its apex on the aim point and three smaller triangles each side, 4 mil apart.
  - **Range scales:** both tanks have an AP scale and an HE scale, **calibrated to this game's ballistics**. Projectiles have no drag, so the mark for range R sits (atan(gR / 2v²) − atan(g·zero / 2v²)) below the aim point. They are game graduations, not verified wartime range scales.
- **Zero.** Each round is aimed to cross the sight line at `zeroRange` (400 m). That accounts for the parallax between optic and bore and for the drop. The self-test fires AP at a casemate 255 m away and measures the impact 0.24 m from the sight point (allowance 0.58 m: drop difference to the zero plus parallax).
- **Field of view (provisional game values).** Sherman 24° / 10° (RMB toggle); Tiger 30° / 12°. The base field is deliberately wide for situational awareness; RMB magnifies the current view.
- **Turret drive.** Traverse and elevation each have their own rate and acceleration limit and come to rest without overshoot. Traverse takes the short way across the ±180° wrap, and elevation stops at its limits. Recoil runs along the barrel axis; the optic rides on the non-recoiling cradle.

| Gun (game tuning, provisional) | Sherman stand-in | Tiger I |
|---|---|---|
| Traverse rate / acceleration | 24 °/s, 40 °/s² | 10 °/s, 8 °/s² |
| Elevation limits / rate | −5° … +20°, 5 °/s | −8° … +15°, 4 °/s |
| Reload | 6.0 s | 7.0 s |
| AP / HE muzzle velocity | 884 / 600 m/s | 773 / 820 m/s |
| Reserves (AP / HE) | 30 / 20 | 40 / 40 |

None of these are certified specifications. The faster Sherman traverse is the requested direction.

## Driving (T4)

The tank is a **kinematic chassis with terrain contact queries**, not a Jolt vehicle constraint. Each fixed step does the following:

1. Speed approaches the throttle target, with separate acceleration, braking and coasting rates.
2. The hull yaws at a pivot rate when stopped, or a speed-dependent turn rate when moving.
3. The new pose is sampled on the terrain at the four track corners. Height, pitch and roll come from those samples, through a damped spring, so the hull pitches through craters without snapping.
4. A swept hull box is tested against every body except the tank's own and the terrain.

| Situation | Result |
|---|---|
| Uphill | The attainable speed drops to zero at the slope limit (Sherman 32°, Tiger 30°); a pose steeper than limit + 4° is refused. Coasting on a slope adds a small, bounded downhill pull |
| Telegraph poles, fences, gates, crates, racks, barbed wire, trees, wooden doors, low stone walls, masonry and timber wall bays | Give way under the energy rule below ([Ramming](#ramming)) |
| Tank traps (`blocksVehicles`), sandbags, hedgerow banks, concrete (bunkers), iron doors, guns, other tanks | Stop the tank; they never give way |
| The player on foot in the way | Stops the tank: no crushing, no repeated per-frame damage |
| Road (worn route) vs off-road | Off-road speed is the road speed × `offroadMul` |

**Historical road speed vs gameplay profile.** The Tank Museum lists the Firefly VC at 20 mph (about 32.2 km/h) and the Tiger I at 40 km/h. Those listings do **not** support a faster Firefly top speed. The default gameplay profile keeps those road speeds and makes the Sherman *feel* faster where the plan asks for it: better acceleration (2.6 vs 1.2 m/s²), quicker turning (34 vs 18 °/s), faster traverse, and a better off-road multiplier (0.9 vs 0.6, so 28.8 vs 24 km/h off-road). All gameplay values are labelled tuning in the JSON (`mobility.note`). Using a strict faster Sherman top speed would be a balance override, and it is not enabled.

## Ramming

One energy rule decides what gives way (`src/tank_ram.cpp`, values in `content/vehicles/ramming.json`).

1. Every obstacle has a **break energy** E. Props have their own (telegraph pole 45 kJ, post-and-wire fence 25 kJ, field gate 30 kJ, barbed wire 40–60 kJ, crates 6 kJ, racks 12 kJ, low dry-stone wall 600 kJ). Trees scale with the trunk: 900 kJ × (r / 0.4 m)², kept between 60 kJ and 1.5 MJ. A building wall is pushed as a whole face (its piers, bays and lintels share the effort) and needs kJ per cm of its thickness: timber 4, masonry 22 (a 50 cm stone wall: 1.1 MJ), concrete never.
2. On contact the tank brings its **kinetic energy** ½ M v² (Sherman 33 t, Tiger 57 t). While W is held the **engine** adds P = engine kW × 0.55 × throttle every second (Sherman 370 kW, so 204 kW of push; Tiger 515 kW, so 283 kW).
3. An obstacle gives way when the energy put into it reaches E. The tank keeps **v′ = √(v² − 2E/M)**, then shares momentum with what it knocked over: **v″ = v′ · M / (M + m)**.
4. If ½ M v² is not enough, the tank **stops against the obstacle**, and the energy already put in is kept. Holding W finishes the job, and a bar on the HUD shows "PUSHING … %".

So the slowdown depends on speed, as asked. Measured in the self-test, a Sherman hitting a telegraph pole loses 3 % of its speed at 29 km/h but 43 % at 7 km/h.

**Durable obstacles take longer.** A slow Sherman stops against a low stone wall and needs 2.4 s of W (2.3 s predicted from E / P). A 57 t Tiger at 24 km/h goes straight through the same wall (1.3 MJ > 600 kJ) and keeps 18 km/h. A Sherman stops against a cottage's masonry wall and breaks through after about 2.6 s of W. Concrete never gives way. Driven at a cottage's door, a Sherman breaks the door frame (trim) and pushes through the piers beside it: 1.5 m inside after 7.2 s, never blocked by a core contact at the opening.

| Obstacle | What happens |
|---|---|
| Pole, fence, gate, crate, rack | Torn from its footing and falls at once, the way the tank pushes it (a rigid body: a shove high up plus a little lift). It settles as debris, and a tank driving over it crushes it flat |
| Low dry-stone wall | Collapses into a low heap of rubble with dust; the tank and soldiers cross it |
| Barbed wire | Every stretch is cut (`destroyDefense`) |
| Tree | Felled through the shared foliage path |
| Wooden door, window sash or shutter | Smashed in (`detachLeaf`, which shatters doors and sashes into pieces). Iron doors and steel shutters hold |
| Building wall bay (masonry, timber) | A tank-sized breach is driven through with the shared impact-centred wall damage (AT-class stamps across the hull width up to the turret roof). It is ordinary wall damage, so Apply / undo keeps it |
| Door or window trim (frame, jambs, sill, lintels, threshold) | Breaks whole for 40 kJ (`trimKJ`), taking the door, sash or shutters with it, and falls out in pieces ([docs/destruction.md](destruction.md#trim)). A contact with the building's static core within 0.35 m of an opening (`trimMarginM`) breaks that trim instead of blocking the tank, and once the trim has gone the tank passes what is left at the opening (steps, slivers). Stored as a `WALL_RAM` wall event, so it replays like any wall damage. The bays over the opening lose their lintel and fall in under the support rule |

Knocked-over props are a session overlay: leaving Explore puts them back. Wall breaches, felled trees and cut wire follow their usual Apply rules.

## HE / AP ammunition (T5)

Each tank has an HE reserve, an AP reserve, one chambered round (`loaded`), one selected next / in-progress type (`selected`) and one load timer (`reload`).

Order inside one fixed step, which is deterministic: **fire → R toggle → load timer → completion → automatic start**.

- Firing consumes exactly the chambered round. The crew starts the next load automatically whenever any reserve is left.
- **R while loading** switches the in-progress choice. The timer is never reset, extended or shortened.
- **R while loaded** changes only the NEXT choice; the HUD shows `LOADED: AP  NEXT HE`. The ready round stays until it is fired.
- **Late selection.** At completion, the reserve of the type selected *at that moment* is decremented once and that type is chambered. If its reserve is empty, the other type is loaded and you are told; with no reserves the gun stays empty. No ammunition is taken for a deselected type, and reserves can never go negative or be duplicated. This late selection is a gameplay abstraction.
- **Same-step events.** An R on the step a load completes is applied first, so the new choice is the round chambered. An LMB on that step does not fire, because fire resolves before completion; the round is kept for the next press.
- **Abstract crew loader.** Loading continues while you are at the hatch and while you are outside the tank. Leaving and re-entering never adds a round or restarts the timer.
- **Destruction** stops the timer and loses the chambered round.
- **Apply** stores the reserves and the chambered round. A load in progress is not stored: the next session starts that load from zero, so there is no free time and no extra round.

## Damage rules (T6, user decisions)

| Attack | Front (hull or turret) | Side / rear | Roof / underside |
|---|---|---|---|
| Tank cannon, AT gun (PaK 40), Flak: any `PROJ_HE` shell, HE or AP | counts (front hit) | counts (side-class hit) | counts as a side-class hit |
| Bazooka, Panzerschreck (`PROJ_ROCKET`) | **no damage**: visible non-penetrating detonation (flash, sparks, smoke), no blast stamp, no splash | counts (side-class hit) | no damage (no rule given, so the conservative choice) |
| Small arms (`PROJ_BULLET`) | never | never | never |

- **Durability.** A tank has 100 points. A front hit costs 100 / (4 + 1) = 20 and a side or rear hit 100 / (1 + 1) = 50, so a tank **survives 4 front hits and is destroyed by the 5th**, and **survives 1 side or rear hit and is destroyed by the 2nd**. Mixed hits add up: 1 side + 2 front leaves 10 points, and the next front hit destroys it. Both survive counts are data (`durability`).
- **Zones** come from the face of the struck component's box, in that component's own frame. The turret frame follows the achieved traverse, so a shot from the hull's front can strike the turret's side. Classification never uses the shooter's bearing or compass direction. Corner policy: front / rear wins a 45° tie (within about 1°). The roof and underside are their own zones and never count as side.
- **No splash path.** Only a direct projectile hit on a tank collider counts, so blasts beside a tank (HE, AP, grenades, rockets) never damage it. A counted hit still detonates on the armour through the shared `explode`, damaging nearby walls and props, but makes no ground crater under the tank. Each projectile is removed at its first hit, so direct and blast events cannot double count.
- **No one-shot kills.** Tanks do not inherit the Phase 1 one-shot AT-gun rule; that rule still applies to PaK 40 emplacements.
- **Damage smoke.** At half durability or below, dark smoke rises from the engine deck. It is distinct from the light exhaust.
- **Wreck.** The transition happens exactly once. The engine, tracks, turret drive, weapons and loading stop. A crew inside bails out to a safe exit point, or with every exit blocked to the widest free spot within 9 m (there is no player-health system to apply). The wreck keeps its collision and is darkened to about 13 % of its colour. It burns from the turret-top hatches and the engine deck for about 50 s, with flames and a column of separate, rising black smoke billows (small varied puffs that expand slowly, drifting with a light wind), then smoulders in grey smoke. It cannot be entered.

## Windows and apertures (T7)

- **Walls.** Tank shells use the shared impact-centred wall field ([destruction.md](destruction.md)). On the canonical masonry wall, an AP shell (`WALL_AP`) breaches locally in one hit. An HE shell (`WALL_HE`) craters it on the first hit and breaches at the same spot on the second. Hits far apart do not add up across a wall. The self-test fires real tank shells at the cottage to check this.
- **Open windows and doors, and broken apertures,** are empty space: no collider. HE shells and rockets fly through and detonate on the next solid surface inside.
- **Closed glass.** A shell or rocket striking the **glass** of a closed window breaks the pane through the shared `breakGlass` (shards, sound, applied state) and continues (`shellThroughGlass`, `src/tank_sim.cpp`). It moves 6 cm past the pane, never strikes the same pane twice, and passes through at most 6 surfaces. Frames, shutters, closed doors and walls keep their collision and detonate it as before. Firearms already break glass (`bulletImpact`).

## Sound and exhaust (T9)

- **Sounds are temporary and synthesized** at startup (`buildTankSounds`, `src/tank_ui.cpp`), like every other sound in the game. They are not recordings.
  - **Engine loops:** two, with distinct Sherman and Tiger firing harmonics. Pitch and volume follow rpm and load.
  - **Track and turret-drive loops:** follow track speed and traverse rate.
  - **Cannon:** a 17-pounder and an 8.8 cm report.
  - **MG:** an MG 34 report. The Sherman's M2 reuses `shot_m2hb`.
  - **Breech and hatch:** reuse the existing breech and steel sounds.
- **Loops** use a new looping-voice pool in `src/audio.cpp` (16 voices, one per sound per tank). Each is started once and then updated, never restarted every frame.
- **Missing audio.** The Phase 1 FMOD banks are not decoded (workstream A1), and no tank engine, track, turret, loading or hatch recordings were supplied. All of these need real sources.
- **Exhaust** comes from the actual outlets. Its rate and opacity follow rpm, and the emitted particles drift with their own velocity and lift. It sits behind the hull, so it does not obscure the gunner sight.
- **Track dust** appears on dry ground only when moving. Particles are skipped beyond 300 m.

## Persistence (T10)

- **Creative selection panel ("Tank: spawn configuration").** AP and HE rounds, turret angle, the applied state, and **Repair**, which clears applied damage. Each edit is one undo step stored in `extra["tank"]`.
- **Example map.** `content/examples/tank_range.nmap.json` is the self-test range: both tanks, a PaK 40, a tank trap, a tree, a cottage and a casemate 257 m downrange. It opens as a copy.
- **Explore** is a session overlay, as for guns. F5 starts every tank from its authored or applied state.
- **Apply session changes** stores each tank's durability, hit counts, wreck flag, reserves and chambered round in `Instance::extra["tank"]`, as one undo step. Position is not stored; spawn placement is an editor edit.
- **Save / reopen** preserves the applied state through `extra`.
- **Duplicate** gives a fresh tank: the turret angle is copied; damage history and ammunition counts are not, so it starts with the default loadout. There is never an occupant.
- **Undo / redo** restore the instance exactly.
- **Occupancy** is never saved. Leaving Explore releases it.

## Self-test (`--selftest-tanks`)

The suite drives the real Explore loop (E prompt, Ctrl, R, LMB, W/A/S/D) and fires real projectiles through `stepProjectiles`. It covers:

- **Catalog and bake:** both entries, thumbnails, the separate moving parts, plausible scale, bounded runtime cost.
- **Creative:** a Tiger picked from the browser shows its model as the placement ghost, is placed through the same call as a click, and deletes cleanly.
- **Entry and stations:** entry through the prompt into the sight; one Ctrl toggle per press, never crouching; the hatch camera; the roof MG only in hatch mode; no cannon shot through a held trigger.
- **Machine guns:** Space fires the bow MG from the sight (rate, ball-mount position, BAR sound for the Sherman, MG 42 for the Tiger) and both MGs from the hatch; RMB fires the main gun from the hatch; both belt changes start by themselves and finish with a full 50- / 100-round belt.
- **Ramming:** a pole at speed and at walking pace (speed lost against the formula), a fence plus barbed wire, a stone wall that needs pushing (time against E / P), a Tiger at speed through a wall, a cottage wall pushed through, and a concrete casemate that holds.
- **Mouse sensitivity:** the look scale for every ADS, scope and tank-sight field of view.
- **Turret and optics:** traverse rate limit, the sight on the achieved gun line, settle without overshoot, short-way wrap-around, elevation stop, bore zero against a real impact.
- **Ammunition:** every state-machine case above.
- **Driving:** forward, steer and reverse; refused moving exit; tank trap; tree; cottage; crater; the profile comparison; a safe exit beside a wall.
- **Damage matrix:** tank cannon, Flak-class shell, bazooka, Panzerschreck and rifle against the front, both sides, rear, roof and turret, with the turret at 0° and 90°; corner and underside; mixed hits; splash; a real PaK 40 station shot.
- **T7:** an AP breach in one hit and HE in two, on masonry; HE and a rocket through closed glass; HE through an open window.
- **Wreck and Tiger:** the occupied wreck with bail-out and stopped systems; the Tiger's sight, zoom, exhaust, loops, hatch and cupola MG.
- **Persistence:** Apply, undo, redo, duplicate, save and reopen.

Screenshots are written to `selftest_out/tanks_*.png`.

## Limitations (honest list)

- **No AI crews, no multiplayer.** One player operates driving, gunner and hatch MG. The game has no networking, so there is no replication or late-join.
- **No vehicle physics simulation beyond the kinematic chassis.** There is no Jolt tracked-vehicle constraint, no per-wheel suspension or traction model, and no physical collision response between moving bodies (the tank stops).
- **Tracks and road wheels do not animate.** Each side's running gear is one mesh in both models, and the renderer has no UV scrolling. Track speed drives only the sound and dust.
- **No hatch animation and no visible occupant.** There is no separate hatch mesh, and no character model is drawn.
- **No module damage.** There are no track, engine or turret modules: a durability pool, damage smoke and the wreck only.
- **Identity:** the Sherman is a 76 mm M4A3E8 mesh standing in for a Firefly, and the Tiger's MG 34 is drawn with the MG 42 model.
- **All sounds are synthesized placeholders.**
- **All gun, optic, reload, ammunition and mobility values are provisional game tuning,** except the two museum road speeds.
- **Ramming** acts per prop (a whole 8 m fence or 6 m wall stretch goes at once) and per wall bay. The rubble heap has no collision. Knocked-over props are not saved by Apply. Frame pieces of a building's core can be left standing in a breach.
- **Bow MG** fire comes from the ball mount, which is part of the hull mesh and is not drawn turning.
- **Performance** has only an indicative measurement (the report's summary). There is no controlled CPU / GPU benchmark with several moving tanks, and the GPU is shared with other programs on the reference machine.
- **Licence:** the Tiger model is CC-BY-NC-4.0, which is non-commercial.
