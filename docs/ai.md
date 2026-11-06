# Matches, objectives and AI soldiers

This page covers singleplayer matches, the Creative scenario tools, the match rules, the bot AI, soldiers, grenades and smoke.
- LAN play: [multiplayer.md](multiplayer.md)
- Supplied models: [assets-ai-lan.md](assets-ai-lan.md)
- The original request and how it maps onto this engine: [plan-ai-lan.md](plan-ai-lan.md)

## Playing

**Creative > Play > Singleplayer...** opens the setup window. It shows:
- the map, which side defends (Allies or Axis; humans always defend in this milestone)
- defender slots, attacking AI count and attacker respawns per slot
- wave intervals, grenades per life and friendly fire
- the exact human / friendly AI / enemy AI counts against the 20-combatant limit
- "1 initial life + N respawns = N+1 lives" per attacking slot

**Start** enters Explore and begins the match. **Save these rules as map defaults** writes the setup into the map; otherwise the map's authored defaults are untouched.

Plain Explore (F5) is still the empty walkthrough with no bots.

| Key | In a match |
|---|---|
| H | quick-throw your side's frag grenade (Mk 2 / M24; 2 per life). Or select it with Q and throw with LMB |
| J | quick-throw a smoke grenade (if your loadout has one; rebindable as "smokeGrenade" in content/settings/bindings.json) |
| F6 | AI debug overlay: role, mode and reason, target, last-known positions, route, chosen cover, lane, lives left |
| Q | next weapon (your faction's pool) |
| other keys | as in Explore |

HUD:
- **Top bar:** every objective in order, with number, name, permanent progress and state (Locked / Active / Captured). The active point shows live status: Enemy advantage, Contested (tied), Held or Empty.
- **Below it:** direction and distance to the active objective, and friendly / enemy soldiers on the point.
- **Bottom left:** both sides' living soldiers, unused attacker respawns, your health and grenades.
- **On death:** who killed you and the next wave countdown. The camera watches a teammate (or your killer for 3 s).
- **At the end:** a result window with **Return to lobby**.

## Creative: Scenario panel

Open it with **Creative > Scenario > Objectives and spawns panel**.

**Tools** (pick a tool, then click the ground):
- **Place objective**: 1 to 10 objectives, lettered A to J by order. The panel refuses an 11th.
- **Attacker spawn** and **Defender spawn**: each spawn belongs to the selected objective's phase and faces it. It automatically gets its team's weapons rack (`rack_allied` / `rack_axis`) about 2.5 m to its side. The rack can be moved or deleted. Deleting the spawn also deletes its rack.
- **Add corners**: builds a polygon capture area.
- **Add slots**: defense slots (MG nest, trench bay, window, foxhole, bunker embrasure, position), each with a priority. A slot faces against the attack arrow.
- **Pick in scene**: links or unlinks placed structures (bunkers, emplacements) to the objective.

**Handles:**
- Drag an objective's centre to move it, its ring handle to change the radius, or its polygon corners to reshape it.
- Drag a spawn to move it; its rack follows.
- Drag rows in the list to reorder objectives. Ids stay stable, and spawn phases follow their objective.

**Per objective:**
- display name
- circle (default 25 m) or polygon
- **capture time** (default 45 s with one attacker)
- vertical bounds
- attack arrow ("Aim attack arrow along view")
- linked structures
- defense slots
- optional lane hints (left, main or right waypoints relative to the attack arrow)

**Per spawn:**
- the phases it is active in (it can serve several)
- its rack: "Place weapons rack" puts back a deleted one

**Validation** runs on save and from **Validate now**:
- **Errors:** 0 objectives, more than 10, or a phase with no attacker spawn or no defender spawn. Also duplicate or non-contiguous order, empty vertical bounds, a polygon with fewer than 3 corners, a roster over 32, and a spawn in no phase or in a phase that has no objective.
- **Warnings:**
  - an objective less than 150 m from the previous one
  - an attacker spawn less than 150 m from its objective
  - a defender spawn less than 80 m from its objective, or less than 200 m from an active attacker spawn
  - an objective that is not further along the attack axis than the one before it (axis: from the first attacker spawns toward the last objective)
  - overlapping spawns
- **Saving:** a map whose scenario has spawns but no objective is not saved. Any other problems are summarised in a toast.
- **World checks** (blocked or unwalkable spawns) are reported when a match starts.

**Overlay** (also shown when the panel is closed: Scenario > Show scenario overlay):
- the attack path, from the first attacker spawns through A, B, C and on
- the exact capture volume
- the attack arrow
- lane hints
- defense slots with their facing
- links to linked structures
- spawn markers with their phases

**Storage:** the scenario is stored as `doc.extra["scenario"]` in the normal map file, `version: 2`. Match state is never written back into the map.
- **Version-1 maps migrate on load:**
  - they keep their capture rule (the contest formula), their defending side and their radii
  - labels become display names
  - each `stage` becomes a single phase
  - `defendPoints` become defense slots
  - a migration note is shown
- **Fixture:** `content/tests/village_assault_5_v1.nmap.json`.

Test maps:
- `content/examples/village_assault_3.nmap.json` (Crossroads, Town hall, Cottage)
- `village_assault_5.nmap.json` (+ Farm, Dairy)

## Match rules (`src/match.cpp`, server only)

- **Roster:**
  - defender slots (humans first, then friendly AI) plus attacking AI, at most 32; zero friendly AI is allowed
  - default sides (v2): Allies attack, Axis defend; humans join the defenders
  - each attacking slot keeps its own **budget of 1 + respawns lives** (default 1 + 5 = 6), independent of its soldier and of objective changes
  - a life is spent only by a **successful** spawn; failed placement spends nothing and is retried
  - defenders respawn without limit
- **Waves:**
  - attackers every 10 s, defenders every 8 s, minimum 3 s from death
  - a wave commits ("pends") every eligible dead slot, and pending slots count as not exhausted
- **Capture** (active objective only): the attacker's feet must be inside the shape and within its vertical bounds. Occupancy is sampled at the start of each step.
  - **Timed** (the v2 default): 100 % in `captureTime` (45 s) with one attacker, faster with more attackers up to `captureSpeedCap` (3x, so 15 s). Any defender on the point pauses it ([DECISION]). Progress never decays.
  - **Contest** (v1 maps): `A <= D: 0; else 5 * (A-D)/(A+D) * min(A/5, 1)` %/s. 1v0 = 100 s, 3v0 = 33.3 s, 5v0 or 10v0 = 20 s, 10v5 = 60 s.
  - Rules data: `captureModel`, `captureSpeedCap`, `captureBase`, `capturePresenceFull`.
- **Ordered objectives:**
  - Locked, Active or Captured; later points cannot be captured early; captured points stay captured
  - advancing keeps living soldiers in place, unteleported, and keeps budgets; the next phase's spawn markers are used from the next spawn
- **End of match:** decided once per step. Final capture reaching 100 % in that step wins for the attackers first. Otherwise the defenders win when no attacker is alive, pending, or has lives left. Everything then freezes.
- **Spawn safety:**
  - markers are tried in turn, with rings at 1.2 m and 2.4 m
  - a spot is accepted only if no other soldier is within 0.75 m, the standing capsule fits, and it is on the walk grid
  - after 3 s of failures an actionable message names the phase and asks for more or moved markers

## Soldiers (`src/game.cpp`, `src/character.cpp`, `src/pose.cpp`)

- **Bodies:** the user's skinned GLBs, Allied `allied_ranger_0/2/4` and Axis `axis_grenadier`. They are skinned on the GPU (compute shader into each soldier's own vertex buffer) and drawn by the normal model pass with shadows. Far soldiers re-skin every third frame.
- **Poses are procedural:** the sources have no animation clips. See the list in [assets-ai-lan.md](assets-ai-lan.md). The held weapon is the weapon's own imported model, and the hands are IK'd onto its grip sockets.
- **Health:**
  - 100 HP; hit zones from the posed skeleton: head ×3, torso ×1, limbs ×0.55
  - damage per round comes from the caliber (`content/ai/combat.json`), with range falloff from 60 to 250 m
  - blasts use the shared `blastOcclusion` (walls and sandbags shelter)
  - friendly fire is a setup option (default off)
- **Weapons:** bots fire the **same projectiles** as the player through `App::fireProjectile`. Cooldowns come from the rpm, there are magazines and reloads from the same weapon data, a muzzle-in-wall check, and no hitscan damage.
- **Loadouts** are per faction (`combat.json`). The bot pools are weighted; players get the faction's player pool. StG 44, M3 and FG 42 are **sandbox / later-war** items, not a claim about 6 June 1944 issue.

## Bot AI (`src/ai.cpp`)

One utility-driven state machine per bot, with modes advance, hold, engage, cover, investigate, evade, return, reload and throw. It sits on shared services:
- **Perception (5 Hz, staggered):**
  - field of view, line of sight (walls and closed doors block, glazed windows do not)
  - smoke opacity integrated along the line, with detection building up by distance, movement, posture and firing
  - hearing of shots and blasts gives **approximate** positions only
  - memory holds only perceived last-known positions, forgotten after 20 s; unseen enemies are never tracked
  - finite reaction time (archetype data) and an aim turn rate separate from body facing
- **Fire:** dispersion grows with distance, own and target movement and suppression, and shrinks with steady aim; bursts for automatics; brief suppressive fire at the last seen spot (aggressive archetypes).
- **Attackers:**
  - lanes allocated by largest remainder (10 → 2 / 6 / 2, at least one on main); authored lane routes, or generated detours on the lane's side of the attack arrow
  - every lane ends **inside** the capture area
  - they fire while advancing, take short **covered bounds** every ~12 m under threat, and take cover only when hit or pinned (max ~3.5 s, then 9 s before the next time)
  - a flank whose route keeps failing joins the main assault
- **Defenders:**
  - scored spots inside the volume: cover around and toward the attack direction, a view of the approaches, authored hints
  - spots are reserved and spread out (a penalty within 5 m of a mate)
  - they reposition when hit from an uncovered side, pursue a lost enemy at most 6-8 m past the edge, and return when the point is contested
  - spots are re-scored after nearby destruction
- **Grenades:**
  - **Frag:** at a recently seen position (≤ 4 s old, never an unseen exact position), 8-30 m away, when the enemy went out of sight, is inside a building, or is clustered with another
  - frag checks: a squad reservation per 6 m cell, no friendly (human or bot) within 7 m of the target, and a simulated arc that must land within 3 m without touching door or window geometry or bouncing back at the thrower; ≥ 8 s cooldown per bot
  - **Smoke** (attackers): when a lane has stalled for 18 s, between the known defender positions and the next crossing; one per crossing cell per 25 s, then the advance resumes
  - bots **evade** frags they see or hear land close, preferring escape points behind cover (defenders stay on the point if possible)
- **Movement:**
  - A* on the walk grid (`src/nav.cpp`, 0.5 m columns, up to 4 floors: stairs and storeys; roofs excluded)
  - string pulling; doors on the route are opened through the shared `toggleInteract`
  - stuck timers lead to re-planning, then an alternate destination; **never a teleport**
  - wall breaches re-sample only the affected columns
  - A* requests are spread across frames (3 per step)

Tuning data:
- `content/ai/archetypes.json`: reaction, aim turn, dispersion, sight, FOV, aggression, bursts, grenade and smoke rules, perception rates
- `content/ai/combat.json`: health, zones, calibers, falloff, loadouts, models, corpses

## Grenades and smoke (shared core `src/equipment.cpp` + `src/grenade.cpp`)

- Thrown grenades are the **shared grenade core** (the main session's E3 items): `mk2_grenade`, `m24_grenade`, and `smoke_grenade` from this branch. Each is a Jolt body with a fuse that starts at release (frag 4.5 s, smoke 1.6 s). Bots, LAN clients and the player release them through `App::spawnGrenade(def, pos, vel, owner)`, and the host is the authority.
- **Inventory** (2 frag; attackers also 1 smoke; data):
  - spent **once, at release**; a throw cancelled before release spends nothing
  - a released grenade stays live if its thrower dies
  - no refill on objective change or possession transfer
- **Frag:**
  - walls via the shared `explode()` with `WALL_GRENADE` and the item's blast data (material rules, so it will not demolish a bunker)
  - personnel damage 160 × (1 - d/12 m)^1.6 with blast occlusion (a solid wall cuts it to about 10 %)
- **Smoke:**
  - a cloud is `{position, start time, seed, radius, build / dense / fade}` (5 m, 2 s, 15 s, 5 s)
  - density and visibility are pure functions of its age, so clients and late joiners rebuild it exactly
  - **visibility = exp(-optical depth)** integrated along the sight segment, core-weighted, so thin edges differ from the full cloud
  - bots cannot detect through dense smoke, and thin smoke slows detection
  - rendering: alpha-blended puffs, capped at 6 clouds × 70 particles
  - smoke blocks neither bullets nor movement

## Tests

| Suite | What it checks |
|---|---|
| `--selftest-match` | Headless, 43 checks: capture rates and times, ties, no decay, locked points, vertical bounds, time-step guards, roster and budgets, spawn-failure accounting, waves, precedence, freeze, lanes, join / leave, scenario round trip / migration / reorder / validation. |
| `--benchmark-match` | 20 combatants, smoke, explosions: frame / CPU / GPU / match-step mean and p95 -> `selftest_out/benchmark_match.json` |
| `--selftest-ai` | Village, 3 points, about 4 min of play. Writes both test maps, walk grid (floors, door links, street → upper-floor path), spawns, lanes diverging then converging, defender spread over a window, combat (shots, hits, kills, frags, smoke), life accounting, no orphan pawns, grenade release / cancel / thrower-death, wall blast occlusion, smoke opacity, frame time, objective progress. |

## Known limitations

- Poses are procedural (see the asset notes). The rangers' automatic weights can crease at the shoulders up close.
- Bots stand or crouch; they do not go prone, crew guns or use vehicles.
- Thrown grenades use the supplied models through the shared items: Mk 2 (Allied frag), M24 (Axis frag), and the supplied smoke canister for both sides (no German smoke grenade was supplied). First-person throws use the shared throw pose (E3).
- No match timer (as specified). Capture and damage values are first balance values.
- **Performance** (`--benchmark-match`; 20 combatants, three smoke clouds kept up, a wall explosion every 4 s, 1920x1080, on this PC's RTX 2080 Ti, which other processes were also using): frame **9.2 ms mean / 11.3 ms p95**, render CPU 8.3 ms, GPU 6.2 ms, match step (AI, soldiers, capture) 0.37 ms. That is within 60 FPS. It is not a furnished map, since furniture is not integrated yet, and earlier runs on a heavily shared GPU read 30-40 ms. Compare runs back to back only.
