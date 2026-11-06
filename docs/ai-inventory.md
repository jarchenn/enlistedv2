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
