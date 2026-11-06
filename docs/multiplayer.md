# LAN co-op (listen server)

Up to 20 combatants. Humans on several PCs defend together against the attacking AI, with the same rules as singleplayer ([ai.md](ai.md)). One PC hosts and plays; the others join over the local network. Internet matchmaking, NAT traversal, host migration and dedicated servers are **not** part of this milestone.

## Before you play

- **Every PC needs the same build and `content/` folder.** The join checks a network protocol version and a content hash (assets, weapons, AI data, characters). On a mismatch it refuses with "Different game build" or "Game content differs".
- **Every PC opens the same map file** (File > Open) before joining. The join compares a map hash (instances, routes, terrain edits), which ignores the scenario, the camera and the name, because the host sends its scenario and rules. On a mismatch it refuses with "Map differs: the host plays '...'. Open the same map file on this PC".
- **Windows Firewall:** the first time a PC hosts or joins, Windows may ask whether Normandy Mapmaker may use private networks. Answer **Allow** on private networks. The game never changes firewall settings itself. If no prompt appeared and joining times out, allow `NormandyMapmaker.exe` (UDP) for private networks in Windows Security yourself.
- Port **27960 / UDP** on the host. Clients use a random local port.

## Hosting

1. **Creative > Play > Host LAN...** shows the same setup as singleplayer: faction, slots, AI, respawns, waves, grenades, friendly fire.
2. Press **Open LAN lobby**. The window shows the PC's IPv4 addresses and the port, and lists the players with their ready state.
3. Every human takes a defender slot, displacing a friendly AI; the setup counts show it. When the lobby has as many players as defender slots, further joins get "Lobby full".
4. **Start LAN match** unlocks when every player is ready. All PCs enter the match together.
5. After the result, **Return to lobby** brings everyone back to the lobby, with ready states reset. **Close lobby** (or quitting) ends the session: every client is told "The host ended the session" and returns to the menu.

The host keeps simulating while its pause menu is open, since clients depend on it.

## Joining

**Creative > Play > Join LAN...**:
- **Search LAN** broadcasts on the local subnet and also checks this PC. Each host found shows its map, player count, and whether your map or build differs.
- Alternatively, type the host's address and press **Join address**.
- In the lobby, press **Ready**.
- **Joining a running match** (late join):
  - you receive the match and world as they are now: wall damage, craters, broken glass and doors, smoke clouds at their current age, objective states and progress
  - you take a defender slot through the **normal respawn flow**: when that slot's AI soldier next dies (or at once if it is dead), you spawn with the next defender wave
  - until then you watch a defender; no soldier is duplicated and no inventory is refilled
- **Leaving:** a living soldier you controlled is handed to the AI in place, with nothing duplicated and nothing left orphaned. The slot becomes an AI slot again.
- Failures are reported in the join window and never reach a fake success screen:
  - refusals (build, content, map, full) carry the host's reason
  - "No answer from host:port" (nothing heard for 20 s) means check the address, the host's lobby and its firewall
  - "Connection to the host was lost (no reply for 6 s)"
  - "The host ended the session"

## How it works (`src/net.cpp`)

- **Transport:** one Winsock2 UDP socket per process.
  - Each peer has a **reliable ordered channel**: messages are chunked (≤ 1000 B), acknowledged cumulatively, and resent after 150 ms.
  - Each peer also gets an **unreliable** payload per packet (latest wins): 20 Hz snapshots host → client, and 30 Hz movement client → host.
  - Packets stay under 1.4 kB. A peer is dropped after 6 s of silence.
- **Authority:** the host runs the whole match (spawns, waves, capture, damage, AI, grenades, smoke, victory). Clients only **request**, and the host validates each request:
  - **Movement:** client-reported; the host accepts it if the step fits the speed and fall limits and no wall lies between the old and new position, and otherwise sends a correction.
  - **Fire:** the host checks the slot belongs to the peer, the soldier is alive, the origin is within 2.2 m of its eye, the weapon is real, the host's own magazine has a round, it is not reloading, and the time since the last shot is ≥ 75 % of the weapon's cycle. The **host** then fires the projectile, with the same weapon data.
  - **Reload:** the host's copy of the containers; partial magazines are kept.
  - **Throw:** checked against the inventory now and again at release.
  - **Door:** within 3.5 m of the opening.
  - Requests for another slot, while dead, or while the match is not running are rejected. Clients can never set capture progress, choose an enemy slot, spawn soldiers or create grenades.
- **Replication:**
  - **Snapshots** carry the match state (phase, active objective, progress and occupancy, wave timers) and every soldier: position, facing, aim, posture, health, weapon, life id, reload / throw / recoil phases, lives, kills. They also carry your own grenades and ammo, and grenades in flight.
  - Clients draw remote soldiers **100 ms in the past**, interpolated between snapshots.
  - **World events, reliable and in order:** each explosion is replayed through the same `explode()` with the same parameters. Wall damage is deterministic per instance and event count, so the stamps match exactly. Bullet impacts go through the same `bulletImpact()`; doors, smoke clouds, and shots (tracers and sounds) are sent as events too.
- **Late join:** the host keeps the ordered log of state-changing events (explosions, structure impacts, clouds) since the match start. A joiner gets START (roster, scenario, rules, host time), then the log and the current door positions. The log is replayed quietly.
- **Client shots:** a client's own rounds become fire requests and are drawn as tracers at once. Hits, kills and world damage come back from the host, so a client never resolves a round itself.

## Tests (one PC)

```bash
tools/run_net_test.sh "%TEMP%/nmk/build-mp" 27970
```

It starts five processes on 127.0.0.1 (`src/selftest_net.cpp`):

| Role | Checks |
|---|---|
| host | lobby, start, roster (2 humans + AI), wall breach and smoke before the late joiner, server-validated requests (shots accepted, over-rate fire and a teleport rejected), every client's wall-damage hash and smoke age equal to the host's, living soldier counts, ending the session |
| a | joins with **60 ms simulated delay and 5 % loss**, readies, spawns, moves, fires, misbehaves, reports its view, handles the host leaving |
| b | **late join** mid-match: world replay, takes a slot via respawn, reports |
| c | different map: refused with the map message |
| d | full lobby: refused with the full message |

Simulated conditions are set with `Net::simLagMs` / `simLoss`; they apply to everything that process sends. The LAN roles run the self-test on the wall clock (`SelfTest::realTime`). The other suites step a fixed 1/60 s per frame, and that made processes drift apart under GPU load.

## Not validated (outstanding)

- **Separate PCs.** All tests ran as processes on one PC (loopback). Discovery across machines, firewall prompts, Wi-Fi and real packet loss, and cross-machine content and map checks still need the user to run two PCs.
- No lag compensation (rewind). A client's shot is resolved where the target is on the host when the request arrives, so on a LAN this is a few milliseconds.
- **Not replicated in LAN matches:** crew-served guns, supply crates, weapon racks and building field defences. Clients get a message saying they are host-only. Detached leaves and debris are simulated by each PC separately (cosmetic).
- Audio from replayed late-join events is muted by moving the listener; the visual debris of past events is not recreated.
