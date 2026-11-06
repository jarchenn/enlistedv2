# Bot AI inventory and baseline (AI_SPEC §2)

This describes the AI as of master 45c4d8f, which came from the AI/LAN milestone on branch `multiplayer-ai`. Workstream W6 replaces it as AI_SPEC describes. The baseline numbers recorded here are the ones W6 is measured against.

## Current architecture

| Concern | Today | File |
|---|---|---|
| Path data | No designated paths. Each objective has optional lane routes (left / main / right waypoints relative to the attack arrow); attackers follow their lane's points, then converge on the point. | `src/scenario.h` `ApproachRoute`, `src/ai.cpp` `laneRoute` |
| Navigation | Hand-written layered walk grid of 0.5 m columns with up to 4 floors each. It is sampled from the live physics world with all-hit rays plus a headroom ray, and has 8-neighbour links. Doors are a link kind; knee and chest rays block the rest. A\* is budgeted per step, followed by string pulling. `rebuildArea` re-samples columns after breaches. There are no area costs, no off-mesh links (mantle / vault / drop), and no tiles. | `src/nav.*` |
| Movement | `aiMove` follows the path at the archetype's speed. It opens doors through the shared interaction, runs a stuck timer (1.5 s, then re-plan, then an alternate goal), and never teleports. There is no avoidance beyond the character-vs-character collision in Jolt. | `src/ai.cpp:487` |
| Decision structure | One state machine per bot. Modes: IDLE / ADVANCE / HOLD / ENGAGE / COVER / INVESTIGATE / EVADE / RETURN / RELOAD / THROW. Tactics update at 1 Hz; perception at 5 Hz, staggered. There is no commander or fireteam layer. Lanes are assigned once (20/60/20, largest remainder) in `Match::setup`. | `src/ai.cpp:329` `aiTactics`, `src/game.h` `Brain` |
| Perception | FOV cone and line of sight. Glass is see-through; doors and walls block. Smoke opacity is integrated along the sight line. Hearing covers shots, blasts and grenade landings. Detection builds up with distance and movement; last-known positions decay. No team knowledge sharing. | `src/ai.cpp:30-118` |
| Cover | `findCover` samples nav cells around the bot and keeps those that block the line of sight to the threat. Cover is not pre-generated and points are not reserved. Defenders take scored spots inside the capture area (`buildDefSpots`, `pickDefSpot`), plus authored defense slots as a bonus. | `src/ai.cpp:119-208` |
| Firing | Aim turns at `aimTurn` °/s, separately from the body yaw. Reaction delay, dispersion that grows with distance, movement and suppression, and bursts. Uses the same projectiles, magazines and cooldowns as the player. | `src/ai.cpp:431` |
| Grenades | `planFrag` / `planSmoke`: squad reservations, a teammate safety radius and a trajectory check, through the shared E3 grenade core. | `src/ai.cpp:249-328` |
| Targets and objectives | Target = the best remembered enemy (distance, visibility). Objective = the match's active point. Defenders hold it, attackers advance and converge. | `src/ai.cpp` |
| Tuning data | `content/ai/combat.json` (calibers, zones, falloff, smoke) and `content/ai/archetypes.json` (rifleman / SMG / MG, …: reaction, sigma, sight, aggression, bursts). | `content/ai/` |

**Decision for W6:** the state machine cannot cleanly host the three-layer utility design in AI_SPEC §9 (commander, fireteam, individual). W6 replaces `aiTactics` with those layers and keeps the perception and firing code, extended per §4 and §6.4. The walk grid is replaced by Recast/Detour (AI_SPEC §2.3: "don't hand-write a grid pathfinder").

## Baseline recording

Run with `NormandyMapmaker.exe --baseline-ai`. It writes `selftest_out/ai_baseline.json`; the committed copy is `docs/baseline/ai_baseline.json`.

**Setup:**
- `village_assault_5`: the 5-point village assault, about 600 m map. It is a v1 map, so the contest capture rule applies, Allies defend, and there are 10 attacking AI with 1 + 5 lives against 5 defenders (4 AI and the idle local player).
- 180 s of match time.

**Metrics** (AI_SPEC §2.2 and §12.3 definitions, applied to attackers):
- **Stuck event:** the bot is following a path and has covered less than 0.5 m in 1.5 s (§3.6). Events lasting more than 5 s are counted separately.
- **Advancing:** following a path, not holding. The metric is the mean horizontal speed while advancing.
- **Under fire:** hurt in the last 2 s, or suppression above 0.3.
- **Exposed:** under fire and not settled at the chosen cover point. This is a proxy for "no cover within 5 m".
- **Deaths per minute:** attacker deaths divided by match minutes.
- **Nearest teammate while advancing:** the §12.3 spacing metric.

### Recorded values (2026-11-06)

From [baseline/ai_baseline.json](baseline/ai_baseline.json), 180 s:

| Metric | Baseline | AI_SPEC §12.3 target |
|---|---|---|
| Stuck events | **0** per bot per minute (0 events, 0 over 5 s) | < 1 per bot per 10 min, none over 5 s |
| Mean speed while advancing | **2.34 m/s** | at least jog (sprint 6.3 m/s, run 3.16 m/s) |
| Time under fire spent exposed | **100 %** (74 bot-seconds under fire) | < 10 % |
| Attacker deaths | **6.7 per minute** | should fall clearly |
| Nearest teammate while advancing | **6.6 m** | ≥ 8 m |
| Objectives taken in 180 s | **1** of 5 | 4–10 min per objective |

**What this shows:**
- Bots are slow: they average below the 3.16 m/s run speed and never sprint.
- They bunch below the 8 m spacing target.
- In practice they are never settled in cover while under fire.
- They die at a high rate.

**What it does not show:** the "stuck in ditches and trenches" failure (AI_SPEC §1.1). The village map has no ditches, trenches, craters or wire, and the grid's own stuck timer re-plans within 1.5 s. That failure has to be measured on the obstacle course (AI_SPEC §12.2), which W6 builds as its first step and records against this same tool.

## W6: the new AI (AI_SPEC 3-11)

The grid and the state machine are replaced. The pieces:

| Concern | Now | File |
|---|---|---|
| Navmesh | Recast/Detour v1.6.0 (zlib), 32 m tiles, cell 0.16 m, agent = the player capsule (radius 0.3, height 1.8, crouch 1.2, step 0.45, slope 45). Baked from the live Jolt world (every static body: terrain tiles and crater patches, structures, wall states, stumps, defences; wire volumes as forbidden), not from authored data. Door leaves are not baked, so doorways are open; bots open the doors as they reach them. | `src/nav.*`, `src/nav_geom.cpp`, `cmake/recast.cmake` |
| Area costs | `navAreaCost()` per Detour area: asset `nav.area` / `nav.areas`, map `extra.navAreas` (painted marsh / minefield polygons), terrain under water (shallow = water cost, deep = forbidden), crouch-only headroom (x1.6). Carve assets are walked around, never over. | `src/ai_core.cpp` (`areaMarks`, `classify`) |
| Links | Generated per tile by probing outward from every boundary edge over the raw voxel columns: drop 1.6-3 m (one way), mantle 0.5-1.6 m (two way), vault <= 1.1 m high and <= 0.6 m thick, jump gaps <= 1.5 m. Wire is never vaulted. Faces above 3 m (anti-tank ditch, quarry) get nothing. Traversed with scripted player-like moves (gravity and jump speed from `movement.json`). | `src/nav_links.cpp`, `src/ai_path.cpp` |
| Live rebuild | The AI polls the placed instances and terrain at 10 Hz (fingerprint per instance, height signature per terrain tile on a ground revision change) and breaches still call `Game::worldChanged`. Dirty tiles + 1 border rebuild on 2 worker threads; bots whose corridor crosses a committed tile re-plan at once. | `src/ai_core.cpp` (`pollWorld`, `onTilesChanged`) |
| Pathing | Per-bot requests, at most 4 started per step by priority (under fire first), solved by Detour sliced A* on a worker thread under the navmesh read lock. Per-bot filter: threat (viewsheds of team knowledge, x2-5 by difficulty), crowding (teammates' claimed routes), lane bias, x50 near edges the bot got stuck on, link time costs. Corridor following with Detour's funnel plus visibility / topology optimisation. | `src/ai_path.cpp` |
| Avoidance | Detour's sampled velocity obstacles against soldiers (radius + 0.3 m), navmesh walls and freshly probed geometry; teammate separation inside 6 m (sideways only); every move clamped along the mesh surface. | `src/ai_path.cpp` (`move`) |
| Stuck | Monitor (< 0.5 m in 1.5 s), ladder re-plan, step back, nearby link, penalise edge (this bot, 20 s), alternate goal, snap within 3 m only when no player sees it. Every event goes to `selftest_out/ai_stuck.log` and the log. | `src/ai_path.cpp` (`stuckLadder`) |
| Perception | 120 deg moving / 90 deg aiming cone, head and chest rays, detection by distance (difficulty range), posture, movement, firing, vegetation areas, smoke. Hearing by weapon class (250 / 120 m), explosions 200 m, footsteps 15 m. Near misses within 2 m and hits: suppression and a cone toward the shooter. Team knowledge: fireteam at once, team after the difficulty delay; confidence decays over 10 s. | `src/ai_combat.cpp`, `src/ai_core.cpp` |
| Cover | Generated per tile from wall edges (low / high, peek left / right / over), craters, asset `nav.cover`; scored by protection (rays from threats), progress, route exposure, 8 m spacing, reservation and firing position. | `src/ai_cover.cpp` |
| Decisions | Commander 1 Hz (fireteams of 3-4 per lane, 25-35 % flankers committed from spawn, 30 s regroup from `rules.json`), fireteam 3 Hz (bounding overwatch, target assignment, room clearing), individual 5-10 Hz (2 Hz beyond 300 m of every living player) by utility with inertia. Defense slots: scenario `defenseSlots`, linked structures' `nav.firingPoints`, building windows, cover inside the point. Spawn-protected soldiers get near-zero target priority. | `src/ai_layers.cpp` |
| Firing | Rifles stop and shoot; SMGs and the BAR fire on the move under 20 m. Aim error from the difficulty, settling on target, growing with movement, suppression, target movement, range and posture. Suppressive bursts at last known positions. | `src/ai_combat.cpp` (`aim`) |
| Grenades | Six simulated arcs; land within 2 m, no bounce-back, nobody of our side within 11 m of the landing or 2 m of the arc; cooldown from the difficulty. Bots sprint away from grenades seen or heard within 8 m. | `src/ai_combat.cpp` |
| Clearing | Rooms, entrances, corners from asset `nav` annotations, generated (one room, door entrances, inset corners, window firing points) for enterable assets without them. Stack beside the entrance, grenade if occupied, enter one at a time, corners in turn, next room by the door graph. | `src/ai_cover.cpp`, `src/ai_layers.cpp` |
| Debug | F6: navmesh by area, links by kind, routes, action and top 3 utilities, suppression, cover, threats, stuck events; the "AI debug" window: difficulty, freeze / step, follow camera, spawn a bot at the crosshair, profiler. | `src/ai_debug.cpp` |
| Tunables | `content/ai/navigation.json`, `content/ai/tactics.json`, `content/ai/difficulty.json`, `content/settings/rules.json`. | |

Tests: `--selftest-ai` (village match, navmesh checks), `--selftest-ai-course` (AI_SPEC 12.4 live rebuild, then the 12.2 obstacle course `content/examples/ai_obstacle_course.nmap.json`, rebuilt with `--make-ai-course`), `--soak-ai <minutes> [village|course|random|all]` (12.3 metrics against the baseline above, `selftest_out/ai_soak_<map>.json`).
