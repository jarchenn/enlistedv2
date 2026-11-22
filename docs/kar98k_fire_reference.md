# Kar98k fire reference: the user's clip, frame by frame

Source: `Video Project.mp4` (supplied by the user; 1920 x 1080, 30 fps, 118 frames, 3.93 s). Frames extracted with Blender's ffmpeg (`E:/nmk/kar_video/frames/`, contact sheets `sheet_*.jpg`). The clip is aimed down the sights; shot 1 fires at frame 6 and its bolt cycle ends at frame 49; shot 2 fires at frame 73 (its cycle: frames 91-117, not used).

## How each number was measured

- **Gun (camera space)**: normalised cross-correlation of the rear of the receiver (safety wing, cocking piece, bolt shroud; template from frame 1) over a scale x rotation grid at half resolution. Apparent scale s gives the distance toward the eye: back = 0.2 m x (1 - 1/s) (0.2 m: the game's Kar98k ADS eye relief); the screen offset gives up / right in metres at that depth with the game's 42 deg ADS viewmodel FOV; roll is the template rotation (+ = counter-clockwise on screen). Trusted where the match score is >= 0.68 (frames 1-7, 10-23, 50). Frames 8-9 are motion-blurred (interpolated in the curve); 24-48 the receiver turns and the hand covers it (not measured: described).
- **Camera yaw**: the HUD compass strip (labels every 10 deg, 5.70 px per degree), phase-correlated frame to frame. Reliable throughout (score mostly > 0.8).
- **Camera pitch**: the background trees (upper right), gradient phase correlation frame to frame, accumulated, at 118 px per degree (from the compass: 0.85 deg of yaw = 100 px). Low confidence in frames 6-13 (flash, blur, smoke): the shape (up 0.7 deg at frame 9, back by 13) matches the frames by eye, the exact values do not have the compass's accuracy.
- **Hand**: lit skin pixels (reddish) in the right half (count and centroid); a proxy for when the right hand moves to the bolt.

## What the game now does (content/weapons/kar98k.json)

- `fireCurve`: rows 0-44 = frames 5-49 at 30 fps (row 1 = the flash frame): gun back / up / right (m), roll (deg), camera pitch / yaw (deg, + = up / left). It replaces the random aim recoil and the spring kick for this rifle; the camera's last row (0.31 deg left, 0.50 deg low) stays in the aim.
- `boltDelay` 0.6 s: the rifle stays shouldered through the recoil, as in the clip; then `boltTime` 0.83 s (frames 24-49) works the bolt. The next shot is possible 1.43 s after the last, as in the clip.

## Every frame (shot 1)

| frame | t (s) | gun back mm | up mm | right mm | roll deg | scale | camera yaw deg (+left) | camera pitch deg (+up) | hand px | what happens |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | -0.167 | +0.0 | -0.0 | +0.0 | +0.0 | 1.00 | -0.00 | +0.00 | 47111 | Steady on the sights: hooded front sight centred on the tarp-covered vehicles; smoke drifting at left; right hand on the grip, left forward (out of view). |
| 2 | -0.133 | +0.0 | -0.0 | +0.0 | +0.0 | 1.00 | +0.00 | +0.00 | 47111 | Unchanged (still). |
| 3 | -0.100 | +2.0 | -0.1 | -0.1 | +0.0 | 1.01 | +0.00 | -0.00 | 47071 | Unchanged; the gun 1 % larger (breath). |
| 4 | -0.067 | +2.0 | -0.4 | -0.1 | +0.0 | 1.01 | -0.01 | +0.04 | 47249 | Unchanged; camera drifts 0.04 deg up. |
| 5 | -0.033 | +2.0 | -0.4 | -0.1 | +0.0 | 1.01 | -0.03 | +0.13 | 46842 | Last frame before the shot; camera 0.13 deg up (trigger squeeze). Curve row 0. |
| 6 | +0.000 | +14.8 | -2.0 | +1.3 | +0.5 | 1.08 | +1.23 | +0.05 | 65732 | SHOT: muzzle flash round the front sight (frame brightness 34 -> 76); the camera jumps 1.2 deg left in this one frame; the gun starts back (1.08x, 15 mm). |
| 7 | +0.033 | +48.5 | -9.3 | -3.8 | +0.0 | 1.32 | +1.15 | +0.34 | 29845 | Gun at its rearmost: 48 mm back toward the eye (1.32x apparent size), 9 mm low, 4 mm left; camera 0.2 deg up; flash fading. |
| 8 | +0.067 | - | - | - | - | - | +1.13 | +0.50 | 28644 | Motion-blurred: gun coming forward and dropping (interpolated 39 mm back, 11 mm low); camera climbing (0.5 deg up). |
| 9 | +0.100 | - | - | - | - | - | +0.96 | +0.70 | 28539 | Motion-blurred: gun 29 mm back, 13 mm low (interpolated); camera at its highest, 0.7 deg up, 1.0 deg left. |
| 10 | +0.133 | +19.8 | -15.6 | -2.7 | +0.5 | 1.11 | +0.80 | +0.63 | 32716 | Gun 20 mm back, lowest (16 mm low), roll 0.5 deg; camera starting down (0.6 deg up). |
| 11 | +0.167 | +3.9 | -11.6 | -3.8 | +0.0 | 1.02 | +0.75 | +0.40 | 42564 | Gun back on the shoulder line (4 mm), still 12 mm low; camera 0.4 deg up, 0.75 deg left. |
| 12 | +0.200 | -6.2 | -8.2 | -2.8 | +1.0 | 0.97 | +0.83 | +0.10 | 45458 | Gun overshoots forward (6 mm past rest), 8 mm low, roll 1 deg CCW; camera nearly level (0.1 deg up). |
| 13 | +0.233 | +2.0 | -6.6 | -3.0 | +3.0 | 1.01 | +0.85 | -0.05 | 44892 | Roll peaks at 3 deg CCW; gun 2 mm back, 7 mm low; camera 0.05 deg below the start. |
| 14 | +0.267 | +7.7 | -6.0 | -3.0 | +1.0 | 1.04 | +0.85 | -0.05 | 45129 | Second, smaller push back (8 mm): the shoulder settles; roll 1 deg. |
| 15 | +0.300 | +11.3 | -5.1 | -3.1 | +0.5 | 1.06 | +0.84 | -0.12 | 45580 | Push at 11 mm back, 5 mm low; camera 0.12 deg low. |
| 16 | +0.333 | +11.3 | -4.0 | -3.1 | +0.5 | 1.06 | +0.85 | -0.13 | 45978 | 11 mm back, 4 mm low; camera 0.13 deg low. |
| 17 | +0.367 | +7.7 | -2.5 | -2.5 | +0.0 | 1.04 | +0.84 | -0.15 | 45973 | Settling: 8 mm back, 2.5 mm low. |
| 18 | +0.400 | +3.9 | -1.5 | -1.5 | +0.0 | 1.02 | +0.84 | -0.18 | 46685 | 4 mm back, 1.5 mm low. |
| 19 | +0.433 | +2.0 | -1.0 | -1.0 | +0.0 | 1.01 | +0.85 | -0.22 | 47442 | Practically at rest (2 mm back); camera 0.22 deg low, 0.85 deg left of the pre-shot aim. |
| 20 | +0.467 | +2.0 | -1.3 | -0.4 | +0.5 | 1.01 | +0.86 | -0.27 | 47979 | At rest on the sights; the new aim is 0.86 deg left and 0.27 deg low of the old one. |
| 21 | +0.500 | +2.0 | -1.8 | -0.4 | +0.5 | 1.01 | +0.87 | -0.27 | 48970 | At rest. |
| 22 | +0.533 | +3.9 | -2.9 | -0.7 | +0.5 | 1.02 | +0.87 | -0.27 | 48718 | At rest (last recoil row with gun motion). |
| 23 | +0.567 | +7.7 | -12.6 | -3.6 | +2.5 | 1.04 | +0.87 | -0.29 | 49033 | Bolt starts: the rifle begins to drop and cant (12 mm low, roll 2.5 deg). In the game: boltDelay 0.6 s ends here and the bolt cycle (boltTime 0.83 s) begins. |
| 24 | +0.600 | - | - | - | - | - | +0.87 | -0.32 | 49899 | Rifle lowered and canted off the sight line (tracker lost: receiver turned); right hand still on the grip. |
| 25 | +0.633 | - | - | - | - | - | +0.87 | -0.38 | 51307 | Rifle lowered and canted further; camera dipping (0.38 deg low). |
| 26 | +0.667 | - | - | - | - | - | +0.87 | -0.40 | 43394 | Rifle at its lowest cant; camera 0.40 deg low. |
| 27 | +0.700 | - | - | - | - | - | +0.87 | -0.41 | 10296 | Right hand leaves the grip and rises to the bolt handle (top right). |
| 28 | +0.733 | - | - | - | - | - | +0.90 | -0.25 | 16695 | Hand lifts the bolt handle (handle rotating up); camera kicks 0.16 deg up. |
| 29 | +0.767 | - | - | - | - | - | +0.91 | +0.07 | 20505 | Bolt handle up; hand at the knob, top right. |
| 30 | +0.800 | - | - | - | - | - | +0.92 | +0.10 | 21067 | Handle fully up; camera 0.1 deg up. |
| 31 | +0.833 | - | - | - | - | - | +0.91 | -0.04 | 13428 | Bolt drawn back: handle and hand come toward the camera at the right edge. |
| 32 | +0.867 | - | - | - | - | - | +0.90 | -0.07 | 10806 | Bolt fully back (handle largest). |
| 33 | +0.900 | - | - | - | - | - | +0.73 | -0.19 | 48059 | Hand off the bolt / out of view; camera swings right (yaw +0.17). |
| 34 | +0.933 | - | - | - | - | - | +0.36 | -0.34 | 50642 | Camera continues right (+0.5 total); rifle canted. |
| 35 | +0.967 | - | - | - | - | - | +0.01 | -0.49 | 53236 | Camera +0.9 deg right of its post-shot heading; 0.5 deg low. |
| 36 | +1.000 | - | - | - | - | - | -0.13 | -0.56 | 55500 | Camera at its rightmost (+1.0 deg); 0.56 deg low. |
| 37 | +1.033 | - | - | - | - | - | -0.10 | -0.43 | 9668 | Bolt pushed forward: the large right hand fills the right of the frame. |
| 38 | +1.067 | - | - | - | - | - | -0.05 | -0.30 | 9980 | Bolt forward stroke continues; camera rising. |
| 39 | +1.100 | - | - | - | - | - | +0.16 | -0.06 | 19207 | Bolt home; camera swings back left 0.2 deg. |
| 40 | +1.133 | - | - | - | - | - | +0.22 | -0.04 | 34603 | Handle turned down; hand still at the right. |
| 41 | +1.167 | - | - | - | - | - | +0.24 | -0.21 | 24717 | Hand leaves the handle, returning low right. |
| 42 | +1.200 | - | - | - | - | - | +0.24 | -0.40 | 56000 | Hand returning to the grip; rifle rising; camera 0.4 deg low. |
| 43 | +1.233 | - | - | - | - | - | +0.24 | -0.53 | 57298 | Rifle rising toward the sights. |
| 44 | +1.267 | - | - | - | - | - | +0.24 | -0.57 | 57372 | Rifle rising. |
| 45 | +1.300 | - | - | - | - | - | +0.25 | -0.60 | 56872 | Camera lowest of the cycle (0.6 deg low). |
| 46 | +1.333 | - | - | - | - | - | +0.25 | -0.58 | 55950 | Rifle nearly up; hand on the grip. |
| 47 | +1.367 | - | - | - | - | - | +0.26 | -0.51 | 21018 | Front sight re-entering the centre. |
| 48 | +1.400 | - | - | - | - | - | +0.27 | -0.46 | 40113 | Rifle into the shoulder. |
| 49 | +1.433 | - | - | - | - | - | +0.28 | -0.37 | 49801 | Back on the sights: next shot possible (1.43 s after the shot = boltDelay 0.6 + boltTime 0.83). Net aim change for the shot: 0.31 deg left, 0.50 deg low. |
| 50 | +1.467 | -2.0 | +0.6 | +1.4 | +0.0 | 0.99 | +0.28 | -0.28 | 46771 | Steady on the sights. |

## Not matched yet

- **The hands during the bolt cycle (frames 27-48)**: the game's Kar98k bolt is procedural (the handle lifts and draws; no arm clip on this branch). Matching the right hand's path in the clip needs an arm clip on the rig (the gameplay session's bolt / reload rigs).
- **The rifle's cant and drop during the bolt cycle (frames 23-48)**: not measured (the receiver turns away from the template). The game keeps its own bolt motion there; the camera does follow the clip (yaw swing right and back, the dip).
- **Field of view**: an overlay at the same framing needs the game's ADS view to match the clip's. The clip's background moves 118 px per degree (about 16 deg across 1920 px), much narrower than the game's 45 deg ADS world FOV; the gun motion is stored in metres (FOV-independent), the camera motion in degrees.
