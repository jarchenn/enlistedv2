# Plan: AI, singleplayer, LAN co-op and content (branch `multiplayer-ai`)

Source request: `Normandy_AI_LAN_Content_Build_Spec.md` (user, 2026-11-06). That spec was written for an Unreal project. The user chose to build it in **Normandy Mapmaker** (this C++20 / OpenGL / Jolt engine), so the spec's Unreal terms map onto this engine as follows:

| Spec term | Here |
|---|---|
| GameMode / match controller | `src/match.cpp` (`Match`, server only) |
| GameState (replicated) | `MatchState` snapshot, serialised by `src/net.cpp` |
| PlayerState | `RosterSlot` (faction, role, lives, occupant) |
| Pawn + components | `Soldier` (`src/soldier.cpp`): health, weapon state, grenades, posture, agent body |
| Behavior Tree / StateTree | one utility-driven state machine with shared services (`src/ai.cpp`) |
| NavMesh | layered walk grid + A* built from Jolt queries (`src/nav.cpp`), patched locally on breach |
| Replication / RPC | Winsock UDP, own reliable-ordered channel + unreliable 20 Hz snapshots (`src/net.cpp`) |
| Creative mode objectives | `doc.extra["scenario"]` (versioned, migration defaults), editor panel `src/scenario_ui.cpp` |

## Fixed by the spec (not re-asked)
- Singleplayer default: 1 human + 4 friendly AI defenders vs 10 attacking AI. Cap 20 combatants. 0 friendly AI and at least 1 enemy AI are allowed.
- Attacker budget: 1 life + 5 respawns per slot (6 lives). A respawn is deducted only after a successful spawn. Defenders respawn without limit. Waves: attackers 10 s, defenders 8 s, minimum 3 s from death to spawn.
- Win rules:
  - Attackers win when the final objective reaches 100 % in the resolved step. That is checked first.
  - Otherwise defenders win when no attacker is alive and none can still respawn (or is pending).
  - After the result, everything freezes.
- Capture: `A<=D -> 0`, else `5 * (A-D)/(A+D) * min(A/5,1)` %/s. Progress never decays. Ordered objectives: Locked, Active or Captured.
- Faction (Allies/Axis) is separate from role (Attacker/Defender). Humans are always defenders this milestone. The host picks the defending faction.
- Grenades: 2 frag per life for everyone, and attackers also get 1 smoke (data). At least 8 s between a bot's throws, plus squad reservations. Smoke: radius 5 m, 2 s build, 15 s dense, 5 s fade. It blocks sight by opacity along the line (not bullets).
- LAN: listen server, local discovery, manual IP, lobby/ready, content hash preflight, late join, results, return to lobby. No internet, NAT traversal or host migration.

## Engine-specific decisions (mine; reported, reversible)
- **Transport:** plain Winsock2 UDP (the only network API available offline). Port 27960, discovery by broadcast on 27961. Windows Firewall may ask on the first host. The user answers that prompt; I never change firewall settings.
- **Movement authority:**
  - Clients move their own character locally, so input stays responsive.
  - The server validates every update (speed, step and teleport limits, capsule fit) and sends a correction when one fails.
  - Shots, reloads, grenade throws, damage, capture, spawns and AI all run only on the server. A client only requests them.
  - This is client-reported movement with server validation, not rewind/prediction. That is adequate on a LAN, and the doc says so.
- **World destruction sync:**
  - The server sends each explosion and each bullet impact as an ordered reliable event. Clients apply them through the same `explode()` / `bulletImpact()` code, which is deterministic per instance seed.
  - Late join receives the current state: each instance's wall-event list, which is already replayable, plus leaf/tree/defence state, the crater list, doors, smoke clouds with their ages, grenades in flight, and the match state.
- **Soldiers:** the user's skinned GLBs (`soldier.glb` Allied, `ww2_panzergrenadier_2k_cc0.glb` Axis), CPU-skinned. Neither has animation clips, so stand, walk, run, crouch, prone, aim, fire, reload, throw and death are **procedural poses on the supplied skeletons**. They are documented as such, not as authored animation. `us_ranger_infantry_-_call_of_duty_2_hd.glb` has no skeleton and is a Call of Duty 2 derivative, so it is inventoried but not used by default (see the asset notes).
- **Nav:**
  - The walk grid is 0.5 m inside the scenario bounds. Columns are sampled with downward rays to find every floor, giving stairs and upper floors.
  - Links follow step height and capsule clearance.
  - Door leaves count as passable "door links", and a bot opens the door through the same `toggleInteract`.
  - Breaches re-sample only the affected tiles.
  - Path failure is handled with a timer, then an alternate goal, then re-planning. Bots are never teleported.

## Milestones (one commit each, suites stay green)
1. **M1 scenario + match core:**
   - Scenario data, migration and validation.
   - Creative panel: objectives with reorder (Move earlier / later), radius, vertical bounds, spawn groups per stage, approach markers left/main/right relative to an attack arrow, and a footprint preview that is exactly the capture volume.
   - Match logic: roster, budgets, waves, capture, ordered progression, precedence.
   - Headless unit suite `--selftest-match`.
2. **M2 soldiers + nav + AI (singleplayer):**
   - Soldier entities with health and hit zones, using the same projectiles and weapon cooldowns and magazines.
   - Agents with the Jolt `CharacterVirtual` per soldier.
   - Nav grid, perception (LOS, hearing, last-known positions that decay, reaction delay, accuracy model).
   - Attacker 20/60/20 flanks that converge; defender cover scoring inside the point.
   - HUD top bar, death/spectate screen, results, setup screen.
   - Suite `--selftest-ai`.
3. **M3 LAN:**
   - Net layer, discovery, lobby with ready states and a rules editor, and preflight hashes.
   - Join/leave displacing or restoring AI slots without orphans.
   - Snapshots and interpolation, client requests validated on the server, late join, host exit, timeouts.
   - Simulated delay/loss. Suite `--selftest-net`, which runs two processes on loopback.
4. **M4 grenades + smoke + tactics:**
   - Frag and smoke grenade core (inventory, release vs cancel, fuse, bounce, occluded blast through `explode`).
   - Smoke volume and opacity integral, used by AI perception and rendered.
   - Squad reservations, safe-throw checks, smoke-then-advance.
   - Grenade danger reaction.
5. **M5 weapons + characters:**
   - StG 44 and M3 grease gun as data-driven weapons from the supplied GLBs.
   - Thompson source comparison and choice.
   - Character import validation, and third-person weapon attachment for bots and remote players.
6. **M6 furniture:**
   - Furniture asset definitions from the supplied GLBs, with simple collision and room sets.
   - Furnished review scene. This waits for the asset-GLB pipeline the main session is building (E1), to avoid two importers.

Separate-PC LAN validation can only be done by the user and stays listed as outstanding until they run it.

## Status (2026-11-06)

| Milestone | State | Evidence |
|---|---|---|
| M1 scenario + match core | **done** | `--selftest-match` 43/43; Creative Scenario panel and overlay; village_assault_3 / _5 test maps |
| M2 soldiers, nav, AI, singleplayer | **done** (procedural poses) | `--selftest-ai`: spawns, 2/6/2 lanes diverge and converge, defenders hold, combat, budgets, grenades, smoke, occlusion; attackers took 2 of 3 points in 222 s |
| M3 LAN | **done on one PC** | `tools/run_net_test.sh` (host + 4 clients, 60 ms / 5 % loss, late join, wrong map, full lobby, host exit) all pass. Separate-PC run outstanding |
| M4 grenades, smoke, tactics | **done** | on the shared E3 grenade core (owner-aware `App::spawnGrenade`, smoke item); opacity line integral, reservations, safe-throw arc checks, evade |
| M5 weapons + characters | **partly done** | StG 44 and M3 added (first-pass sight anchors measured on the mesh; not tuned with `--selftest-sights`). Characters rigged and baked. Thompson kept as the M1, since the supplied ones are M1928s |
| M6 furniture | **not started** | waits for the asset-GLB pipeline from the main session's E1; inventory in `docs/assets-ai-lan.md` |
