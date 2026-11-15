# Wheeled vehicles (VH1)

Three placeable jeep types, built on the tank framework (`src/tank*.cpp`, [tanks.md](tanks.md)): a vehicle definition with `"car": true` reuses the tanks' catalog entry, placement ghost, thumbnail, E to enter or leave, safe exit points, collision body, ramming, damage, wreck, effects and Apply. What differs is the driving, the drawing, the camera and the HUD (`TankSys::stepCar`, `drawCar`, `drawCarUI`).

| Type | Asset id | Vehicle file | What it is |
|---|---|---|---|
| 1 | `car_jeep_willys` | `content/vehicles/jeep_willys.json` | Willys MB as supplied: windshield up, stowage (bags, jerrycans, axe, shovel, rope) |
| 2 | `car_jeep_willys_open` | `jeep_willys_open.json` (`"base": "jeep_willys"`) | Windshield folded flat on the bonnet (82° about its hinge), no stowage |
| 3 | `car_jeep_willys_mg` | `jeep_willys_mg.json` (`"base": "jeep_willys"`) | Pedestal M2 .50 between the front seats (game M2HB model on an added post), windshield folded |

A variant file names its `base` and overrides keys (JSON merge patch); variants share the base's GPU model.

## Controls

| Input | In the jeep |
|---|---|
| E | Enter (anyone; the Tanker rule is for tanks only) / get out below 3.6 km/h at a safe exit point |
| W / S | Throttle; S brakes, then reverses (W brakes when rolling back) |
| A / D | Steer (smoothed: 1.1 s lock to lock, recentres faster) |
| Space | Handbrake (rear wheels lock; the tail steps out more) |
| Mouse | Head: ±110° around the bonnet, −50° to +35° |
| Ctrl (type 3) | Driver's seat ↔ the M2; at the gun LMB or Space fires, belts change on their own; the jeep holds on its handbrake |

## Driving model

Kinematic, like the tanks: speed approaches the throttle target (3.2 m/s² accelerating, 7.5 braking, 0.7 coasting), and the slope, map-edge, water and swept-box checks are the tanks'.

- **Top speed:** 18 m/s (65 km/h) on worn routes (roads), ×0.6 (39 km/h) off-road; reverse 16 km/h.
- **Steering:** bicycle model, yaw rate = v·tan δ / wheelbase (2.0 m), δ up to 32°. Grip caps lateral acceleration at about 0.75 g (1.1 g with the handbrake).
- **Wheels and suspension:**
  - The front wheels steer with Ackermann geometry (the inner wheel turns more).
  - All wheels roll with the ground speed; on the handbrake the rear wheels stop.
  - Each wheel rises or drops up to 12 cm to the ground under it. This is visual only; the body still rests on the four contact points through the tanks' damped spring.
- **Steering wheel:** rotates about its measured column axis, 2.5 turns lock to lock, with the smoothed steering.
- **Running people over:**
  - Soldiers and zombies inside the moving body box (above 1.5 m/s) take 9 damage per m/s of impact speed, each at most once every 0.8 s.
  - The jeep loses 20 % of its speed per victim.
  - A friendly struck takes the damage with no kill credit to the driver.
  - Only the host or single player resolves this (`mpaiRunOver`).
- **Damage:**
  - The jeep is soft-skinned: a single tank shell, AT shell or rocket from any side wrecks it.
  - Each bullet costs 4 of its 100 points.
- **Sound:** the Sherman engine loop pitched up (no jeep recording exists). Dust and exhaust are scaled down from the tanks'.

## Camera

The camera is the driver's head (`rig.driverEye`, left-hand drive), looking through the windshield frame. The dashboard, steering wheel, bonnet and windshield frame are all visible, and the viewmodel is hidden. At the M2 the gunner stands behind the spade grips and moves around the post as the gun traverses.

## Model and licence

- **Source:** the supplied `jeep_willys_mb__free_download_daz3d_poser.glb` (uchronia, Sketchfab) is **CC-BY-NC-ND-4.0**: non-commercial, no derivatives.
- **Conversion:**
  1. `tools/vehicles/convert_jeep_glb.py` (Blender) converts it to metres and groups its meshes under role nodes (`hull`, `wheel_fl` / `fr` / `rl` / `rr`, `steer`, `windshield`, `stowage`, `pintle`).
  2. It drops the glass (no transparent pass) and the display plinth, and decimates the tyres.
  3. `--import-vehicle-models assets_src/vehicles content` bakes the result into `content/vehicles/models/jeep` (110k triangles).
- **Local-only:** that folder is git-ignored. Clones get `content/vehicles/models/jeep_fallback`, a 1.4k-triangle box-and-cylinder jeep made by `tools/vehicles/make_jeep_fallback.py` at the same rig positions (`model.fallbackDir`).

## Limits

- **Driver only:** there is no passenger seat and no LAN replication. The tanks have none either, so a LAN client sees a parked jeep.
- **No hands on the wheel:** the first-person arms are hidden while driving.
- **No splash damage:** grenades and shells bursting beside a jeep do not hurt it (the tanks' direct-hit-only rule).
- **No glass:** the windshield is an empty frame.
- **Kinematic:** no tyre or traction physics and no airborne state. Trenches and steep banks stop the jeep ("Blocked" / "too steep") rather than tipping it.
- **Not verified in a capture:** running people over (logic only), the 65 km/h road speed (the capture map has no road), and the type 3 pintle post and the gun's folded tripod legs at close range.
