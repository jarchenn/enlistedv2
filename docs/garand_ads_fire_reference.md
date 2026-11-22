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
