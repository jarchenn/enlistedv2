# M1 Garand ADS fire: the reference video, measured (GR1)

Source: the user's clip `garand frmae.mp4` (1920x1080, 43 frames at 30 fps, some frames duplicated by the encoder): ADS,
standing, two shots (muzzle flash on frames 9 and 28). The user asked for the Garand's ADS shot to match it in movement,
hands and recoil. Tools: `tools/gr1` (run with `python -I`; frames extracted with Blender's sequencer, no ffmpeg needed).

## Method

- `track.py`: the rear-sight block (knobs and peep) by gradient template matching over scale 0.85-1.75 and position, against
  frame 8 (settled, before the first shot). Scale = how far the rifle came back toward the eye.
- `track3.py`: the front post (blade and wings) against the peep ring: the muzzle's pitch and yaw relative to the receiver.
- `track2.py`: the scene above the gun (window post / roof, trees) for the camera: pitch, yaw, roll (the two patches' height
  difference). The wallpaper repeats, so it is not used.
- `curves.py`: pixels to our units at 1080p in our projections (view model 60 deg, ADS world 58 deg), the two shots aligned
  on the first frame the receiver jumps (taken as half a frame after the shot) and averaged.

## Calibration

The rear sight's knobs (58.1 mm across on our model, their faces 10.2 mm ahead of the peep) span 467 px of 1080 in the
reference (frame 8, x 737-1205). Our view model draws them that wide at an eye relief of 0.124 m (`m1_garand.json` ads).
The reference's own FOV is not fixed by the clip (its front sight differs in shape from ours: from the wings 52 deg
vertical, from the blade 64 deg); 58 deg, the game's ADS default, sits between and is kept.

## The measured shot (mean of the two; t from the shot)

| frame | t s | back mm | muzzle up deg | muzzle right deg | camera up deg | camera roll deg (cw) |
|---|---|---|---|---|---|---|
| 0 | 0.017 | 24.2 | 1.73 | -0.18 | 1.62 | -1.20 |
| 1 | 0.050 | 35.3 | 2.95 | -0.07 | 1.62 | 0.58 |
| 2 | 0.083 | 33.2 | 2.90 | 0.02 | 1.95 | 1.16 |
| 3 | 0.117 | 23.7 | 1.88 | 0.13 | 1.75 | 1.10 |
| 4 | 0.150 | 13.7 | 1.02 | 0.18 | 0.65 | 1.02 |
| 5 | 0.183 | 5.4 | 0.44 | 0.17 | -0.41 | 0.73 |
| 6 | 0.217 | 2.5 | 0.26 | 0.14 | -0.53 | 0.87 |
| 7 | 0.250 | -2.9 | -0.53 | -0.01 | -0.63 | 0.32 |
| 8 | 0.283 | -2.9 | -0.47 | -0.01 | -0.75 | 0.36 |
| 9 | 0.317 | -2.9 | -0.31 | -0.01 | -0.88 | 0.20 |
| 10 | 0.350 | -2.9 | -0.13 | 0.04 | -0.99 | 0.09 |
| 11 | 0.383 | -1.5 | -0.05 | 0.03 | -1.03 | 0.08 |
| 12 | 0.417 | 0.0 | -0.02 | 0.00 | -1.02 | 0.07 |
| 13 | 0.450 | 0.0 | 0.02 | 0.00 | -1.03 | 0.08 |

- The receiver comes 35 mm toward the eye (1.45x on screen) 50 ms after the shot and the muzzle rises 2.9 deg relative to it
  80 ms after; both are half back by 0.12 s, undershoot (-3 mm, -0.5 deg) at 0.25 s and settle by 0.4 s. The rifle does
  not rise or drift sideways on screen: its drop is the perspective of coming closer.
- The operating-rod handle is back on the flash frame and the next, home by the third (`handle_m` 0.08 m for two frames).
- The camera kicks up 1.6-2.0 deg for 0.12 s and rolls ~1 deg clockwise, then snaps back. After that both shots end about
  1 deg lower and the yaw keeps drifting as it did before the first shot: the recorder tracking a running soldier and
  pulling down. That part is not recoil and is left out (the curve's camera ends at 0).

## In the game

`m1_garand.json` `fireCurve` (KF1's rows, 60 fps, with the GR1 columns: muzzle pitch / yaw, camera roll, handle) with
`"ads": true`: in ADS it replaces the random climb and the spring kick; hip fire keeps them. Overlapping shots add.
The operating rod is its own part of the baked model (`tools/garand_oprod_split.py`; re-run it after any re-import).

Not matched: the reference's rifle model itself (its peep ring is larger and its front blade wider than ours), the hands
and sleeves (ours are the engine's generated arms), the static hold (the reference rests the post ~12 px under the peep's
centre; ours is on the sight line), the muzzle flash and smoke.

## Sight picture (GR2, GR3: the user)

- Zoom: the reference's peep ring is 84 px across at 1080p, ours was 62 px; the Garand's ADS field of view is narrowed 1.355x (view model 46.2 deg, world 44.5 deg).
  The fireCurve is converted in that projection (`curves.py` reads the weapon's ads.vmFov / worldFov), so the motion covers the same pixels as the reference's.
- The ring thinned to the reference's hole / outside 0.69 (ours was 0.50): `tools/garand_peep_thin.py`.
- The front sight reshaped to the reference's proportions in the ring: the wings and block 3.9 mm lower (wing tips ~5 px over the blade tip), the block
  0.78 as wide (16 px), the blade stretched down and 1.3x wider; its tip stays on the sight line: `tools/garand_front_sight.py`.
- GR4: the peep and the front blade raised 6.5 mm (`tools/garand_sight_raise.py`; sights.rear / front moved with them) so the barrel and handguard stay
  28 px or more under the hole's centre, as on the reference's rifle (ours covered the hole's bottom from ~13 px); the hole is clear to its own rim.
- Still different: our rear sight block is wider for the same ring (~630 px against 467).
- Re-import order: garand_oprod_split.py, garand_peep_thin.py, garand_front_sight.py, then garand_sight_raise.py.

## The Garand's own recoil everywhere (GR14: the user)

In the game the Garand felt like every other gun. Two causes:
- The fire curve was ADS-only (`"ads": true`), so hip shots, and shots while the aim was still blending in, fell back to the recoil every rifle shares: the aim climbing 6.75 deg per shot and staying, and a 6 Hz spring jabbing the rifle back ~9 cm (`tools/gr1/recoil_compare.py`, chart: take vs game).
- The user's build (197a687b) applies the curve to the arms rig only with `curveRecoil`, which the local file no longer has: in ADS only the camera kicked, the rifle and arms did not move. 130c293b (GR13b) applies it to any rig with a curve.

Now `"ads": false`: the measured shot at the hip and in ADS alike (no random climb, no spring kick, no ADS weighting), and `"breathe": false`: no idle breathing sway in ADS (the approved take's still aim; the mouse look's sway stays).
