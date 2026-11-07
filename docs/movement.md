# Movement: vault and mantle

Speeds, postures, jump tuning and jump reliability are in [expansion.md §5](expansion.md#5-movement). This page covers getting over and onto things: the **vault** (low obstacles) and the **mantle** (ledges up to 2.1 m, such as trench lips). Both are tuned in `content/settings/movement.json` (`"vault"` and `"mantle"` blocks, read by `App::loadClimbSettings`, `src/mantle.cpp`).

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
