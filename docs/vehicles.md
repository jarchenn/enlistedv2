# Wheeled vehicles (VH1)

Nine placeable vehicle types, built on the tank framework (`src/tank*.cpp`, [tanks.md](tanks.md)). A vehicle definition with `"car": true` reuses the tanks' catalog entry, placement ghost, thumbnail, E to enter or leave, safe exit points, collision body, ramming, damage, wreck, effects and Apply. What differs is the driving, the drawing, the camera and the HUD (`TankSys::stepCar`, `drawCar`, `drawCarUI`).

| Type | Asset id | Vehicle file | Model | Licence / in the repo |
|---|---|---|---|---|
| 1 | `car_jeep_willys` | `jeep_willys.json` | Willys MB (uchronia) | CC-BY-NC-ND-4.0: bake **local-only**; clones draw `jeep_fallback` |
| 2 | `car_jeep_ww2` | `jeep_ww2.json` | WW2 jeep (leoxx300) | CC-BY-4.0: bake committed (`models/jeep_ww2`, 7.9 MB) |
| 3 | `car_truck_cargo` | `truck_cargo.json` | Opel Blitz, flatbed | Creative Commons (per the user): bake committed (`models/truck_cargo`) |
| 4 | `car_truck_canopy` | `truck_canopy.json` | Opel Blitz, canvas-covered bed | as type 3 |
| 5 | `car_truck_medical` | `truck_medical.json` | Opel Blitz ambulance box body | as type 3 |
| 6 | `car_truck_flak` | `truck_flak.json` | Opel Blitz with a Flak gun on the bed (the gun is part of the body mesh: not crewable) | as type 3 |
| 7 | `car_truck_kitchen` | `truck_kitchen.json` | Opel Blitz field kitchen | as type 3 |
| 8 | `car_humvee` | `humvee.json` | Humvee, low-poly, with a furnished cab (VH2) | no author or licence in the archive: bake **local-only**; clones draw `humvee_fallback` |
| 9 | `car_boxer_apc` | `boxer_apc.json` | Boxer 8x8 APC with an HMG remote weapon station (VH2) | no author or licence in the archive: bake **local-only**; clones draw `boxer_apc_fallback` |

The jeeps and the Humvee are Allied and the trucks Axis (faction only labels the catalog; anyone may drive any of them).

## Controls

| Input | In a vehicle |
|---|---|
| E | Enter / get out below 3.6 km/h at a safe exit point. The Tanker-only rule applies to tanks only |
| W / S | Throttle; S brakes, then reverses (W brakes when rolling back) |
| A / D | Steer, smoothed (jeeps: 1.1 s lock to lock; trucks: 1.5 s) |
| Space | Handbrake (rear wheels lock) |
| Mouse | Head: ±110° around the bonnet, −50° to +35° |
| Ctrl | Only on a vehicle with a pintle MG (`variant.pintle`, none of the seven now): driver's seat ↔ gun |

## Driving model

Kinematic, like the tanks: speed approaches the throttle target, and the slope, map-edge, water and swept-box checks are the tanks'.

| | Jeeps | Trucks |
|---|---|---|
| Road / off-road top speed | 65 / 39 km/h | 60 / 33 km/h |
| Acceleration / braking | 3.2 / 7.5 m/s² | 1.8 / 5.5 m/s² |
| Steering | 32°, 2.5 turns lock to lock | 34°, 4 turns lock to lock |
| Wheelbase / track / wheel radius (from the models) | Willys 2.00 / 1.24 / 0.35 m; WW2 jeep 1.96 / 1.36 / 0.37 m | 4.50 / 1.63 / 0.49 m |
| Mass (ramming) / run-over damage | 1,100 kg / 9 per m/s | 5,000 kg / 14 per m/s |

- **Steering:** bicycle model, yaw rate = v·tan δ / wheelbase, capped by grip at about 0.75 g (more with the handbrake).
- **Wheels and suspension:**
  - The front wheels steer with Ackermann geometry, and all wheels roll with ground speed (the rear ones stop on the handbrake).
  - Each wheel rises or drops to the ground within its travel. This is visual only.
- **Steering wheel:** rotates about its measured column axis with the smoothed steering.
- **Running people over:** soldiers and zombies inside the moving body box take damage by impact speed, each at most once every 0.8 s. A friendly struck takes it too, with no kill credit (`mpaiRunOver`; host or single player).
- **Damage:** soft-skinned. One shell, AT shell or rocket wrecks a vehicle; bullets take 3–4 of its 100 points.
- **Sound:** the Sherman engine loop pitched up. Dust and exhaust are scaled down from the tanks'.

## Camera

The camera is the driver's head (left-hand drive): the steering wheel's centre plus `rig.driverEyeFromSteer`, so it also fits the part-built Willys fallback. It looks through the windshield frame, with the dashboard, steering wheel and bonnet in view, and the viewmodel is hidden.

## Models

`tools/vehicles/convert_vehicles.py` (Blender, background, scripts disabled for the `.blend`) converts each source to metres and the game's axes. It groups the meshes under role nodes (`hull`, `wheel_fl` / `fr` / `rl` / `rr`, `steer`, `windshield`, `stowage`) and drops the glass, since the vehicle bake has no transparent pass. Then `--import-vehicle-models assets_src/vehicles content` bakes them.

| Mode | Source | What the conversion does |
|---|---|---|
| `willys` | uchronia's Willys MB | Named nodes. The tyres are decimated |
| `ww2jeep` | leoxx300's WW2 jeep | Unnamed Cube / Cylinder parts, taken by measured position: the four tyre cylinders with their hubs, and the steering wheel rim. Scale 0.44 m per unit |
| `opel` | Opel Blitz truck collection (`all5 together.blend`, one collection per truck) | Relinks the textures shipped in the archive (36 of 82; the rest point at the author's machine), drops the glass faces, finds the wheel cylinders by quadrant, and cuts the steering wheel out of the merged body: the 0.52 m ring island in the cab plus the islands inside its disc |
| `humvee` | low-poly Humvee (`Humvee.fbx` + its 1K texture) | Scales x1.36 to 4.6 m. The cab was a closed shell with painted windows: the windshield and side window faces are removed and replaced by frames, the shell gets a 2.5 cm inner wall in plain paint (back faces are culled), and the cab is furnished: floor, drive tunnel, dash with gauges and switch panel, radio stack, gear and transfer levers, steering wheel (`steer`) on its column, pedals, four bucket seats, grab bar, door armrests |
| `boxer` | Boxer APC (`boxer-apc-hmg.fbx`, BMP textures) | Turns front +x to the game's front; 8 wheels by axle (`wheel_fl/fr`, `wheel_f2l/f2r`, `wheel_r2l/r2r`, `wheel_rl/rr`); the weapon station cradle with its launchers and ammo box is `rws` (traverses), the gun with its sensors `mg` (traverses and elevates); the driver's vision-block glass is dropped |

`tools/vehicles/make_fallback.py jeep` / `humvee` / `boxer` builds the committed part-built Willys / Humvee / Boxer stand-ins.

## Boxer APC (VH2)

- **8x8:** the two front axles steer about the line between the rear two (the second axle less, Ackermann); every wheel spins and rides its own 0.25 m of suspension travel. 100 km/h on roads, 60 off-road, 40 deg slopes, 0.53 m under the hull, lower grip than a car (wide ~18 m turning circle), 33 t for ramming.
- **Driver:** head-out above the driver's cupola (front right). The model's vision blocks are closed glass slits too small to see through at this level of detail, so the head is out.
- **Gunner (Ctrl):** the remote weapon station's sight (`rig.mgOptic`, moves with the gun): mouse traverses / elevates (-8..60 deg), LMB / Space fires the .50 (M2HB sounds, tracer every 5th round), RMB zooms (40 -> 12 deg), belts of 100 change on their own. The vehicle holds on its handbrake while the gunner is up.
- **Armour:** `durability.heavyBulletJ` 9 kJ: rifle rounds (~3.5 kJ) do nothing, .50 rounds (~18 kJ) 1 each; launchers 34 (3 wreck it); tank / AT gun shells 50. Wreck as the other vehicles.
- **Not done:** the rear ramp / hatches do not open (no animations in the file); not adopted in Battle Royale (the royale adopts only parked part-built civilian cars).

## Helicopter (VH5)

The AH-64D Apache: vehicle `ah64d_apache` (`content/vehicles/ah64d_apache.json`, `"heli": true`), catalog entry `heli_ah64d_apache` (**Vehicles**, faction Modern). The code is in `src/heli.cpp`, with hooks in the tank system. It uses the tanks' entry and exit, collision body, damage table, wreck fire and persistence. Anyone may fly it; Tanker bots never board it.

- **Model:**
  - The supplied CC BY GLB is baked and committed in `content/vehicles/models/ah64d_apache/`, with the front turned to +z and the scale set by the 14.6 m rotor.
  - The bake splits the main rotor (`rotor`), the scissor tail rotor (`tailrotor`) and the M230 chin gun (`gun`) out of the body by island boxes.
  - The rotors spin about `rig.rotorHub` (4.8 rev/s) and `rig.tailRotorHub` (23 rev/s) at the rotor speed. The rotor spins up over 4 s when the pilot enters and runs down over 9 s after they leave. There is no blur disc.
- **Controls:**

  | Key | Action |
  |---|---|
  | Space / Ctrl | Climb (8 m/s) / descend (6 m/s) |
  | W / S | Forward (55 m/s) / back (12 m/s) |
  | A / D | Strafe (15 m/s) |
  | Mouse | Turns the nose (70°/s) and aims (pitch −60° to +25°) |
  | LMB | Chin gun |
  | R | One rocket per press (R only fires rockets while piloting) |
  | E | Get out, only once landed and slower than 1 m/s |

  With no input it holds a hover: the velocities ease to their targets at 9 m/s². It cannot lift off until the rotor is fully spun up. The camera is a chase camera, pulled in front of walls. The HUD shows altitude, speed, rotor, durability, 30 mm rounds and heat, rockets and which pod fires next. The key hint is shown on entry and at the bottom of the screen.
- **Collision:**
  - Ground means terrain or anything solid under the skids, so it can land on a pad or a roof.
  - Every move is checked against the airframe boxes and a 14 m rotor-disc box. It slides along walls and never passes through them.
- **M230 chin gun (LMB):**
  - Fires toward the crosshair within ±100° traverse and −60° to +11° elevation, at 625 rounds/min and 805 m/s, with a tracer every third round.
  - Each round is explosive (`Projectile::cannon30`): a 60-damage blast of 2 m. It hits infantry hard and counts as a launcher hit on cars, so it wrecks jeeps and wears down the APC (34 per hit). Tanks only take a spark from it.
  - 1200 rounds. Heat rises 0.034 per round and cools 0.22/s, so about 80 rounds (7.7 s) of continuous fire overheat it. It fires again once cooled to 35 %.
- **Hydra rockets (R):**
  - 38 rockets, one per press (0.25 s minimum), from the pod muzzles (`rig.rocketPods`), left and right in turn.
  - They fly toward the crosshair if it is within 12° of the nose, else straight ahead.
  - The shared rocket projectile: 300 m/s, 420 blast damage, 2.6 m radius, kills AT guns on a direct hit. Backblast and smoke trail.
- **Damage:**
  - Treated as an armoured car: 2 rocket hits (50 each) or 1 tank, AT gun or Flak shell (100).
  - Rifle and MG rounds do nothing; .50 rounds do 4 each.
  - Destroyed in the air, it loses power, spins up to 320°/s with a smoke and fire trail, and falls. It crashes in a 300-damage, 6 m blast and stays as a burning wreck.
  - The pilot is put out of the airframe and killed (`Game::killSoldier`) during a match. Outside a match there is no player death, so they are only put out beside the wreck.
- **LAN:** host and single player only. Vehicles are not replicated, so a client sees it parked.
- `--capture-heli` captures the helicopter parked, then runs a scripted flight with the gun and two rockets, logs the numbers, and records the shoot-down and the wreck.

## Limits

- **Driver only:** no passenger seats and no LAN replication. The tanks have none either, so a LAN client sees a parked vehicle.
- **No hands on the wheel:** the first-person arms are hidden while driving.
- **No splash damage:** blasts beside a vehicle do not hurt it.
- **No glass:** windshields and side windows are empty frames.
- **The trucks are the source's size:** 7.1–8.0 m long with a 4.5 m wheelbase. A real Opel Blitz 3 t is about 6.0 m long with a 3.6 m wheelbase; width and height match.
- **Untextured truck surfaces:** truck materials whose textures were not in the archive show their base colour.
- **Kinematic:** no tyre or traction physics. Trenches and steep banks stop a vehicle rather than tipping it.
- **Not verified in a capture:** running people over (logic only) and the road top speeds (the capture map has no road).
