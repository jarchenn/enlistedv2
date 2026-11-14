# First-person building

In first person, **B** opens build mode, and you can place anything from the Assets palette while walking around (`src/builder.cpp`). The palette is the same one the Creative browser uses (`src/palette.cpp`, `drawAssetPalette`): the same categories, search, Enterable / Min floors filters and thumbnails.

## Controls

| Key | Action |
|---|---|
| B | Enter / leave build mode. Entering opens the Assets palette and frees the cursor. |
| Tab | Show / hide the palette. Picking a thumbnail puts that asset in hand and returns to the view. |
| Esc | Close the palette (a second Esc does the usual thing: back to the editor in a drop-in, the pause menu otherwise). |
| 1 / 2 / Q | Quick defences: tank trap, straight / curved sandbags (as before) |
| Wheel, Z / X, R | Rotate the ghost 15° (Z: −15°, X and R: +15°). While building, Z is not prone and X is not the ammo toggle. |
| LMB | Place |
| Del | Remove the aimed placement (tinted orange while aimed) |
| Ctrl+Z | Undo the last first-person place or remove |
| RMB | Leave build mode |

While the palette is open, the view does not turn. LMB clicks the palette rather than placing, and typing in the search box does not move the player. The weapon is lowered and holds fire while building.

## Placement

- **Ghost.** It goes where the view meets open ground within **12 m** (it was 5 m), drawn like the editor's Place preview: mesh, panels, models and door leaves, green when valid and red with the reason when not. Furniture lands on real floors and table tops (`interior.hoverPoint`, as in the editor).
- **Never around the player.** If the asset's footprint would contain the player, the ghost is pushed forward along the view until it clears.
- **Rules.** The same as the editor: `checkPlacement` (build area, water, foundation grade, overlaps, furniture rules) and, for defences and gun stations, the slope rule from the hit normal (35° for defences, 20° for carriages) plus the blocked-volume test.
- **Snapping.** `buildSnap()` in `builder.cpp` snaps sandbags to sandbag sockets (`snapDefense`) and paths to path ends (`snapPath`). **V6 hook:** route this function through `snap.cpp`'s sockets when that lands.
- **Companions.** A bunker's crew stations and ammunition are placed with it (`placeCompanions`).

## Map edits vs playtest overlay

| Session | Place | Remove (Del) | Undo |
|---|---|---|---|
| Drop-in or "Explore from start" from Creative (`navState().creativeSession`) | **A real map edit** through `placeInstance` (the editor's own path, so V4 furnishing and V6 crush / flatten hooks apply). Marks the map dirty and pushes one step on the editor's undo stack. Kept when you return to the editor. | Any aimed instance (not the gun or tank you are in), with one editor undo step | Ctrl+Z reverts the last action and pops its editor undo step. Back in the editor, Undo / Ctrl+Z also undo first-person edits. |
| F5 playtest, matches | A session overlay (`extra.session`), discarded on the way back to Creative, as before | Only pieces built this session | Ctrl+Z reverts the last action |

Rack deployments (the M2HB tripod and the PaK 40 from a weapons rack) always stay session pieces. Session pieces never end up in an editor undo snapshot.

## Tests

`--selftest-editor`, stage 3346:
1. Drop in from Creative and press B: build mode, with map edits.
2. A palette pick puts a crate in hand. Its ghost is valid 1–12 m ahead.
3. LMB places it as a real edit: no session flag, the map dirty, one editor undo step.
4. Pick a house and rotate it 90°. It is valid, and its footprint stays off the player.
5. Del on the aimed crate removes it.
6. Ctrl+Z three times: the crate comes back, then the house goes, then the crate goes. The map and the editor's undo stack end exactly where they started.
7. Place a crate again and return to the editor. The crate is still there, and the editor's Undo removes it.
8. In an F5 playtest, a placement is a session piece and is discarded on exit.

`--selftest-env` keeps the defence checks: slope rule, socket snap gap and tangent, curved chaining, F5 discard, Apply and Ctrl+Z.

<a id="engineer"></a>
## Engineer building (GM3)

In a match, B gives the Engineer a fixed build menu instead of the sandbox palette (other classes get "Only the
Engineer can build"). Items and their tuning are data: `content/settings/engineer_builds.json`.

| Key | Item | Asset | Build time | Limit |
|---|---|---|---|---|
| 1 | Outpost (team spawn point) | `eng_outpost` | 10 s | 1 standing |
| 2 | AT gun | `gun_pak40` | 10 s | 1 standing |
| 3 | Barbed wire | `wire_concertina` | 5 s | 5 per life |
| 4 | Sandbags | `def_sandbag_straight` | 3 s | 10 per life |
| 5 | Ammo box | `crate_mixed_supply` | 5 s | 3 per life |

- Hold the fire button for the build time (progress bar; releasing starts again). Placement is the first-person
  builder's: the same checks, a session overlay, the LAN build request / place events.
- Each placement carries `extra.engineer = {owner, team, kind}`. Hold Del for 10 s aiming at your own outpost or AT gun
  to dismantle it, so a new one can be built.
- Outposts are spawn points for their team: `engOutposts(app, team)` (src/engineer.h); the deploy screen's
  `Game::spawnOptions` lists each one ("Outpost", 2.2 m behind its sandbags).
- Class check: `engineerCanBuild()` (src/engineer.cpp) reads the local soldier's GM1 class (`builds` in classes.json).
- Not yet: a LAN client's placements go through the host's build request without the owner tag, so they are not
  counted against the limits and cannot be dismantled; bots do not build.
