# Flamethrower (FT1)

One flame gun for both sides: `m2_flamethrower` ("M2 flamethrower", US) and `flammenwerfer41` ("Flammenwerfer 41", DE),
the same model (`content/weapons/models/flamethrower`, credit in docs/credits.md). Main-weapon slot; racks (the faction's
arsenal), the Loadout window (L) and ammo crates (`flame_fuel`) offer it; bots draw it from `content/ai/combat.json`
(weight 1 of 13). All numbers below are game tuning in the weapon JSON `"flame"` block, not real data.

## Model
The source GLB is a whole scene (backpack tanks, frame, hose, pouch, ground plane, the gun, 26,738 triangles, one mesh).
`--import-weapon-models` keeps only the gun: `model.keepIslandsIn` is a box (rotated source space) and only connected
islands lying entirely inside it stay (10,332 triangles), before the length (1.1 m) and the butt / centre placement are
measured. The hose is its own island, so nothing needed capping. `rot [0, 180, 90]` turns the scene's lying-on-its-side
gun upright with the nozzle at +z. Then `simplify 0.6` (6,198 triangles) and `maxTex 512`. The texture's metal channel is a
real mix (steel parts metal, the rest not), not a flat 1.0, so no `matOverride` was needed.

## Handling
* Hold fire to spray. Fuel is the weapon's `loaded` (cap 100 = percent), drained by the second: `flame.fuelSeconds` 20 =
  5 % a second of continuous fire. No magazine, no reload (R only says so); ammo crates refill it (`flame_fuel`).
  The readout shows `73 %` and the fire-mode text `Flame`; empty reads "Out of fuel - refill at an ammo crate".
* Aim (ADS) is a zoom only: world FOV -12 % (`ads.zoom` 0.12, blended like any ADS), no sight alignment, the weapon
  stays in its hip pose (`vmTarget` ignores `adsT` for zoom weapons).
* Sockets: `rightGrip` on the pistol grip, `leftSupport` on the tube near the front, for the support-arm work (FA2).

## The stream
* `FlameSys` (src/flame.cpp): each open nozzle emits `parcelsPerSec` (30) parcels at `speed` (30 m/s) inside `coneDeg`
  (3.5 deg), each living `range` / speed = 22 m of flight; gravity 0.4 g so the stream droops about a metre at 22 m.
  Not hitscan. Each frame a parcel casts its step against the physics world: the first solid surface stops it (walls,
  ground), openings (embrasures, windows, doors) let it through because they are holes in the geometry.
* The local nozzle starts where the weapon projection draws the gun and bends toward the crosshair's target (up to 20 m).
* Host only: a parcel passing a soldier (axis segment, radius 0.3 m + 5 cm per metre flown) hits it once through
  `Game::flameHit` -> `Game::damage` (`parcelDamage` 3.5 each, about 100 per second of the stream on one target; spawn
  protection, friendly-fire rules and the buttoned-up tank crew rule apply unchanged) and, if the hit was not refused,
  ignites it: `burnSeconds` 3 at `burnDps` 14, ticking every 0.25 s whether or not it stays in the stream (zombies burn too).
* A parcel landing on ground (normal.y > 0.45) leaves a fire patch (`patchRadius` 1.3 m, `patchSeconds` 3, `patchDps`
  12; patches within 0.9 m merge; at most 24) that damages and ignites whoever stands in it. Nothing spreads, nothing
  is destroyed.
* Bots near the stream are suppressed (the same suppression a hit gives) and a bot carrying the gun walks to within
  ~14 m of its target (`botRange` 18) and fires bursts of `botBurstSeconds` 1.6 with pauses of `botPauseSeconds` 1.4,
  aiming a little high for the droop.

## FX, light, sound
Flames, a nozzle flare, splashes, smoke and burning soldiers go through `Fx` particles with a 80 % / 90 % guard of the
particle budget (`Fx::maxParts`, Settings presets). Each open nozzle (3 at most) and the two freshest ground fires add a
flickering orange point light through `lightsGather`. Sound: the audio banks hold no flame or burn sample, so the roar
is synthesised at first use (low-passed noise with a swell, flutter and crackle, 1.5 s loop on `Audio::loopStart`);
there is no ignition or burn-out sound.

## LAN
Damage is the host's. A client sprays locally for the look and sends `flame` {p, d, w, dt} at 10 Hz; the host checks the
carried weapon, drains fuel by real elapsed time (at most 0.25 s per request, never faster than the weapon burns it) and
runs the stream with damage. The host (and its bots) broadcast `flame` events at 10 Hz so the other clients draw the
stream, and a `burn` event when a soldier is set alight. The fuel readout follows the host's snapshot once firing stops.
`kNetVersion` 6.

## Tests
`--selftest-weapons`: both weapons exist and the model is the gun only; ADS changes only the FOV; fuel lasts about 20 s
and then the stream stops; the stream reaches 20 m and not 25; a wall 12 m away stops it; in a Solo match the stream
burns a soldier and a zombie, the burn keeps damaging after the trigger is released and ends after about 3 s; a bot
with the gun opens fire from inside 19.5 m. `NMK_FT1_SHOTS=1` also queues the look shots (`ft1_hip_idle`, `ft1_hip_fire`,
`ft1_ads_fire`, `ft1_third_person`) into `selftest_out`.

Not done: no structural damage, no spreading fires, tanks and wire take no flame damage, no burn-out sound, no
first-person arm (the support socket is there for FA2), no corpse burning.
