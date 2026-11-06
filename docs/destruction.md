# Destruction and ground deformation

**Units.** Health, damage and the stamp sizes below are *game tuning* chosen for play balance. They are not joules, penetration figures or a model of any real wall or shell. Historical references (TM-E 30-451 and others, in `docs/weapons.md`) informed the PaK 40's traverse, elevation and optic. They did not set these numbers.

<a id="damage-field"></a>
## Walls: impact-centred, cumulative damage (`src/wallfield.cpp`)

**What changed and why.** Before this update, every wall bay had three pre-built states (intact, damaged, breached), and HP was subtracted per bay. Every breach was therefore the same template hole at the bay's centre, wherever the shell struck, and shots at piers, corners or lintels between bays did nothing, because those were indestructible static parts. Both behaviours are gone.

**Damage field.** Every destructible bay carries, per building copy, a grid of cells over its face:
- The default cell is 4 cm (`destruction.cell`, a quality setting).
- Each cell stores the depth removed from the front face and from the back face.
- Material survives where the two together are less than the bay's thickness.
- The layers come from the bay's own parts, for example 0.47 m of stone plus a 0.03 m plaster skin.

**Every wall surface participates.** Authored bays (`floor-NN/<side>/bay-NN`) keep their IDs.
- At load, structural slabs that were plain parts are converted into bays (`auto/<part>`): piers between openings, corners and quoins, lintels, sills, and upper floors.
- Parts sharing a face rectangle stack into one layered bay.
- Furniture and foundations are never converted.
- The farmhouse went from 27 to 115 bays, the casemate to 23.

**One impact, one stamp, at the true contact point.**
- **Direction and scope.** The stamp uses the shot direction and is applied in every bay it overlaps, in that bay's own coordinates. It reaches bays whose face slab is within 0.2 m along the normal and within the stamp radius in the plane. So a hit on a seam opens both bays with no sliver, and a corner hit cuts both faces.
- **Grenades** act where they lie, whatever way they fell. Each bay within 0.6 m is struck square on from the side the grenade lies on. The radius and depth fall off with that stand-off: full at contact, about 85 % radius and 65 % depth at 0.4 m.
- **Masonry breaks along mortar.** The stamp reproduces the stone shader's course and block layout:
  - A stone struck hard enough comes out whole, leaving a flat socket.
  - A grazed stone only chips, at most 6 cm deep.
  - Timber splits along its vertical grain.
- **Shape: irregular oblongs, never circles.** Each impact gets its own outline from its seed. The same outline is used in every bay the impact reaches, so seams stay consistent.
  - **Oblong.** An ellipse of random aspect 1.2–1.65 at a random orientation. It is area-preserving, so the hole size is unchanged on average.
  - **Lobed.** Four low harmonics (2nd to 5th) on the outline radius, up to ±11 % each.
  - **Ragged edge.** Smooth value noise at 9 cm and 3.5 cm scales moves the break line. It replaces the old per-cell random jitter, which gave a speckled 4 cm edge.
  - **Oblique hits** still gouge along the shot. A 60° hit removes about twice the width of a straight one.
  - **Profile.** A brittle full-removal core with a short steep shoulder (no shallow terraces). The crater floor is rough: up to 20 % less removal in places, never more than the profile.
- **Accumulation.** Removal only grows: a union that never restores material. Repeated hits deepen and enlarge a hole, and two holes join when the strip between them is hit.
- **No slivers between holes.** After every bay has its stamp, thin material left between damaged areas is cleared:
  - The cleanup looks along rows, columns and both diagonals. A solid run of at most `destruction.bridgeMax` (0.18 m) with a through-hole on both sides is opened.
  - It also sees open cells in the coplanar bay across a seam, so a strip at a joint between two holes goes too.
  - On the first pass, a ridge of at most 0.18 m standing proud between two deeper craters on the struck face is broken down to the shallower side. This only happens inside the impact window. If it leaves less than 28 % of the thickness, the ridge gives way.
  - Bridge clearing and the unsupported-fragment pass then alternate over each whole touched bay until neither changes anything. A fragment falling out, or a bridge going, can expose a new bridge anywhere in the bay. So the result has no bridge left for any outline, whatever the seed.
  - Wider strips stay, so holes that are genuinely apart stay apart.
  - **Tested over seeds, not fixture ids.** Launcher-hole pairs are stamped with chosen seeds: 6 spacings from 0.50 to 0.75 m, 32 outline seeds each, so 192 pairs. Without the cleanup they leave 2700 sliver cells; with it, 0.
  - Pairs 1.4 m apart stay two holes for all 32 seeds. That gap is more than the two largest possible outlines (2 × 0.56 m) plus the bridge width.
- **Perforation.** When less than 28 % of the thickness remains, the hit punches through. Past half the thickness, the far face spalls.
- **Unsupported fragments.** Material no longer connected to the bay's edges falls out. A bounded handful of fragments (8 per hit) and dust are thrown with the shot, plus some spall back toward the shooter.

| Hit (`WallHitKind`) | Stamp radius | Removal per hit into masonry (resistance 1) | Result in a 0.5 m masonry wall |
|---|---|---|---|
| HE shell (AT gun or tank gun, damage 120) | 0.60 m (was 0.55) | 0.30 m | crater; a second hit at the same spot breaches (canonical fortification rule) |
| AP shot (PaK 40 PzGr. 39 or tank AP, damage 320) | 0.80 m (was 0.30) | 1.60 m (was 0.95) | large breach in one hit: 2.10 m² in the test (was 0.29). Also perforates 1 m of bunker concrete in one hit |
| Launcher rocket (Bazooka, Panzerschreck) | 0.30 m | 0.55 m | small local breach: smaller than an AT-gun HE crater |
| Grenade (Mk 2, M24; damage 80–85) | 0.29 m (Bazooka size; was 0.22) | 0.36 m (was 0.07) | structural damage where it lands. At 0.25 m from the wall foot: 0.64 m wide and 0.35 m deep (Bazooka at the same spot: 0.72 m wide). Opens thin walls; a second grenade breaches masonry |
| Small arms | 0.035 m | 0.025 m | face chips (reserved; walls currently stop bullets without marking) |

The radius scales with (damage / reference)^¼ and the removal depth with √(damage / reference). The table's reference damage gives the listed values. Removal also scales by 0.55 + 0.45·cos(incidence) and divides by the material resistance:
- masonry 1.0 (HP 200)
- reinforced concrete 1.2 per metre of thickness (its 1.0–1.2 m bays already resist through depth), about four HE hits per spot. Bunker walls are bays; rounded bunker parts (roof slabs, piers, the large casemate's hood, nose, plinth and dome) and the stepped slit liners are static (docs/bunkers.md#destruction)
- timber 0.45 (one HE hit opens a board wall)

**Derived state** for the rest of the game, which still reads intact, damaged or breached:
- **Breached:** a through-hole of at least 0.04 m².
- **Damaged:** any removal.

**Mesh and collision together.**
- A damaged bay's render mesh and its Jolt collision are rebuilt from the same greedy-merged boxes of surviving material, with removed depth quantised to 2 cm. An open hole never keeps collision, and intact material never loses it. A shot through an opening continues to the next real surface.
- Cut faces show the layers: the stone pattern in the wall's depth and the plaster skin broken back from the edge.
- **Broken edges, not stair-steps.** A cell standing back from two adjacent neighbours and the cell between them is the inner corner of a 4 cm step. It may be missing entirely, or broken deeper on the front or back face. A triangular wedge fills its half toward that corner, through the depth the neighbours stand proud. Runs of steps on the hole outline and on the crater rim become straight diagonal break lines. The wedges are render-only (at most half a cell), and collision keeps the cell boxes.
- The compound for the whole building copy is swapped at the impact.
- Measured: 0.78 ms to rebuild a farmhouse bay with its edge wedges (0.35 ms without), and 0.03–0.11 ms for the building compound.

**Debug.** F4 in Explore draws bay states.

<a id="openings"></a>
## Doors, shutters and steel

- **Wooden doors break apart.** At HP 10 (rifle rounds), or a blast within its radius, a door breaks into pieces instead of falling off whole:
  - The leaf splits into 3–6 vertical plank strips of jittered width.
  - Each strip breaks across the grain into one to three pieces, at least 12 cm long. The strips at the hit break out around it.
  - Each piece is clipped from the leaf's own parts (paint, battens, stiles), so it looks like the door it came from. Its broken ends get a few splinter slivers.
  - Each piece is its own small Jolt body. It gets its share of the impulse, concentrated around the hit, plus a push out of the frame and some scatter.
  - The farmhouse front door gives 12 pieces in the test. Splinters and paint flakes are thrown as cosmetic debris.
  - The live rigid-body cap is 64 (was 24). Past it, the oldest piece freezes where it lies.
- **Wooden shutters** come off whole, with the struck plank split (unchanged).
- **Iron doors, steel shutters and steel flaps are unbreakable.** Every opening whose sound is `steel` is affected: the bunker doors, the casemate embrasure shutter and the viewing and loophole flaps. No weapon breaks them:
  - rounds ring off;
  - blasts of any kind (HE, AP, rocket, grenade) leave them;
  - `detachLeaf` itself refuses them, so no other caller can break them either.
  - Tested: 40 rifle rounds and all four blast kinds at 0.3 m on each steel leaf of the casemate.

## Ground (craters)

- The canonical height is the base heightfield plus sparse fine tiles: 5 base cells per tile, 36 × 36 samples at about 0.45 m. Accumulated depth is capped 1.6 m below the base, and overlapping craters never raise excavated ground.
- **Profile** `Z -= D·(1 − d²/R'²)²`. `R'` is the radius in that direction. It comes from an irregular oblong outline, random per impact point (`Terrain::craterShape`):
  - an ellipse stretched 1.1–1.45× along a random axis;
  - four positive low harmonics on top.
  - `R'` is never below the nominal R, so crater walls are never steeper than the round profile, and the walk-out tests still pass.
  - The editor and test debug preset (`craterAt` with explicit radius and depth) stays round. A tool should cut exactly what it says.
- AT-gun craters were enlarged in this update:

| Crater | Was R / D | Now R / D | Change |
|---|---|---|---|
| Standard HE (7.5 cm) | 1.5 / 0.50 m | 2.0 / 0.62 m | +33 % radius, +24 % depth |
| AT shot (PzGr. 39) | 3.0 / 1.20 m | 3.9 / 1.50 m | +30 %, +25 % |
| Bazooka rocket | crater factor 0.6 | 0.35 | smaller; blast radius 1.6 → 1.1 m |
| Panzerschreck rocket | crater factor 0.75 | 0.45 | smaller; blast radius 1.9 → 1.3 m |

- The depth-to-radius ratio is kept, so walls stay climbable. The walk-out and crawl-out tests pass.
- A direct AT-gun, Bazooka or Panzerschreck hit still destroys a PaK in one shot. Near misses use the separate splash rule, now with the smaller launcher radius.
- Foundations are protected: `groundWeight` is 0 inside a placed building's footprint and ramps to 1 at 1.8 m. Disturbed soil is drawn as soil and its grass removed.

## Session overlay and persistence

- Explore damage to walls and ground is a session overlay. F5 back to Creative restores the map exactly.
- **Walls are stored as their impact events** in `extra.wallDamage`: contact point and direction in the building's frame, hit kind, damage, seed. Events replay in order, and replay rebuilds every cell exactly (tested), so save, load, reset and undo reconstruct the same opening.
- **Edit → Apply session changes to map** writes the events and terrain deltas as one undo step. **Clear applied damage** removes them.
- **Old maps' per-panel HP is converted once on load** to the equivalent centred HE hits on that bay.
- Terrain deltas are keyed to the template version.

## Known limits

- **Roofs and stairs.** Sloped roofs and stair treads are not converted to bays (rotated or small parts), so they don't take local damage yet. Upper floors do.
- **Debris** is cosmetic: pooled boxes with simple physics and no collision with the player.
- **Crouching through holes.** A crouched capsule is 1.2 m tall plus 2 cm padding, so a breach must be open about 1.25 m high. `--selftest-damage` opens a 0.8 × 1.26 m breach in a fresh 1.2 m MG-casemate wall and crawls through it crouched. The earlier wedge in the 1 m casemate came from a hole lower than the capsule, not from the controller. The farmhouse crouch-through also works.
