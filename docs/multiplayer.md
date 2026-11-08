# LAN co-op (listen server)

Up to 32 combatants. Humans on several PCs defend together against the attacking AI, with the same rules as singleplayer ([ai.md](ai.md)). One PC hosts and plays; the others join over the local network. Internet matchmaking, NAT traversal, host migration and dedicated servers are **not** part of this milestone.

## Before you play

- **Every PC needs the same build and `content/` folder.** The join checks a network protocol version and a content hash (assets, weapons, AI data, characters). On a mismatch it refuses with "Different game build" or "Game content differs". The protocol version is **3** (crew-served guns in the snapshots and the gun, rack, crate and build requests). 2 belongs to the internet-play branch, so a merge of both needs a new number.
- **Every PC opens the same map file** (File > Open) before joining. The join compares a map hash (instances, routes, terrain edits), which ignores the scenario, the camera and the name, because the host sends its scenario and rules. It also ignores the match's session placements (field defences built during play) and the id counter, because late joiners receive those through the world log. On a mismatch it refuses with "Map differs: the host plays '...'. Open the same map file on this PC".
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
  - **Weapon switches** (movement payload, fire requests) are accepted only for a weapon the soldier carries: its faction's player loadout plus rack swaps.
  - **Crew-served guns** (MG 42 mount, PaK 40, Flak 18, M2HB):
    - **Crew:** E on a gun sends a request. The host grants it only if the soldier is within 4 m of the gun's bounds, nobody else crews it (another client or the host), the soldier crews no other gun, and the gun is deployed and not destroyed. The host then pins the soldier at the gunner's position. Only the gunner's position (Aim) is open to clients: moving a PaK carriage (Move, T) stays host-only, with a message.
    - **Aim:** while crewing, the 30 Hz movement payload carries the gun's traverse and elevation targets instead of the eye angles (posture bit 0x80). The host clamps them to the mount's limits and turns the gun at its own traverse and elevation rates.
    - **Fire:** every round is a request. The host checks that the gun is the soldier's, has a round loaded, is not reloading or destroyed, and that the time since its last round is at least 75 % of the cycle (MG 42 1200 rpm, M2HB 500 rpm; 0.5 s for field guns). It then fires from its own gun with the singleplayer code (`App::stationShoot`), and the round is credited to the soldier.
    - **Reload:** the host runs the reload on its own clock with the gun's own reserve (with the AT / HE choice for the PaK).
    - **Release:** the host frees the gun when the gunner leaves (it accepts a dismount point within 6 m), dies, disconnects or loses the gun.
  - **Racks:** a take request is accepted within 4 m of the rack, for a weapon of the rack's faction (not one hidden by the strict D-Day preset), and not while crewing a gun. The weapon replaces the one in hand in the host's loadout, with each weapon's own ammunition kept. Racks have no stock, so there is nothing else to replicate.
  - **Crates:** a resupply request is accepted within 4 m of the crate, and the host runs the singleplayer transaction (`App::crateTransaction`) on its own copy of the soldier's weapon and the crate's stock: whole containers only, never beyond the weapon's allowance. A request where nothing moves (full, empty crate, wrong feed) counts as refused. Guns in the crate's service radius are restocked as in singleplayer.
  - **Field defences (build mode, B):** clients get the whole build mode (1 / 2 / Q quick defences, the V7 Assets palette, rack deployments). The ghost and its checks run locally, and LMB sends the asset, position and yaw. The host re-runs the builder's rules (`App::buildCheckAt`): reach (12 m from the eye), slope, the editor's placement check, a clear footprint, never round the builder. It also refuses vehicles and more than one placement per 0.25 s. The piece then gets the host's id. Removing (Del) and undo (Ctrl+Z) stay host-only.
  - Requests for another slot, while dead, or while the match is not running are rejected. Clients can never set capture progress, choose an enemy slot, spawn soldiers or create grenades. Malformed requests (wrong types) are rejected without touching the match. The host counts refusals by reason in `Net::rejects` (for example "gun: taken", "build: dead").
- **Replication:**
  - **Snapshots** carry the match state (phase, active objective, progress and occupancy, wave timers) and every soldier: position, facing, aim, posture, health, weapon, life id, reload / throw / recoil phases, lives, kills. They also carry your own grenades and ammo, and grenades in flight.
  - **Crew-served guns** are in the snapshots too: occupant, traverse, elevation, loaded rounds, reserve, AT reserve, and the reloading, destroyed, AT selection and deployed flags. Every crewed gun is in every snapshot, and the rest follow in turn, 6 per snapshot. On your own gun the aim is yours, and the ammunition is the host's once your rounds have been answered. Other players' rounds arrive as `gshot` events (tracer, flash, sound, recoil).
  - Clients draw remote soldiers **100 ms in the past**, interpolated between snapshots.
  - **World events, reliable and in order:** each explosion is replayed through the same `explode()` with the same parameters. Wall damage is deterministic per instance and event count, so the stamps match exactly. Bullet impacts go through the same `bulletImpact()`; doors, smoke clouds, and shots (tracers and sounds) are sent as events too.
  - **Placements** (`place`): every session piece the host creates, whether by a client's request or the host's own build mode, goes out with its id, asset, position, yaw and extra data. A bunker's companions are included. Every PC creates exactly those instances. Removals and undos on the host go out the same way. Damage to defences replicates like other world damage (the same explosions and impacts). The host also marks the area changed for the AI navigation; clients run no AI.
  - **Crate stock** (`crate`) after every transaction, whether a client's or the host's own.
- **Late join:** the host keeps the ordered log of state-changing events (explosions, structure impacts, clouds, placements, crate stock) since the match start. A joiner gets START (roster, scenario, rules, host time), then the log and the current door positions. The log is replayed quietly. Gun state (occupants, angles, ammunition, wrecks) reaches the joiner through the snapshots within a few of them, and every soldier's weapon (a rack swap included) with the first.
- **Client shots:** a client's own rounds become fire requests and are drawn as tracers at once. Hits, kills and world damage come back from the host, so a client never resolves a round itself.

## Tests (one PC)

```bash
tools/run_net_test.sh "%TEMP%/nmk/build-mp" 27970
```

It starts five processes on 127.0.0.1 (`src/selftest_net.cpp`):

| Role | Checks |
|---|---|
| host | lobby, start, roster (2 humans + AI), wall breach and smoke before the late joiner, server-validated requests (shots accepted, over-rate fire and a teleport rejected). Places two MG 42 mounts, a rack and a crate round A, and a third MG 42 30+ m away, then crews the second gun itself. Checks A's interactions: gun rounds fired by the host, the traverse matches, the crate stock is down, the rack weapon is in hand and in the loadout, one sandbag. Checks the refusals: the gun it crews, the far gun, the far placement, a full resupply, a placement and a gun while dead. Every client's wall-damage hash, smoke age, placements (ids), crate stock and guns (occupant, belt, reserve) equal to the host's; living soldier counts; ending the session |
| a | joins with **60 ms simulated delay and 5 % loss**, readies, spawns, moves, fires, misbehaves. Receives the host's placements, is refused someone else's gun and a gun out of reach, crews the MG 42, traverses and fires a burst (each round a request), leaves it. Resupplies at the crate (then is refused at the allowance), takes an M1C from the rack, places a sandbag (and is refused one 40 m off). After B has looked, it is killed and sends a placement and a gun request while dead. Reports its view, handles the host leaving |
| b | **late join** mid-match, once A is done (it waits for A's flag file in `selftest_out`): world replay, A's rack weapon, sandbag and spent MG belt on arrival, takes a slot via respawn, reports |
| c | different map: refused with the map message |
| d | full lobby: refused with the full message |

Simulated conditions are set with `Net::simLagMs` / `simLoss`; they apply to everything that process sends. The LAN roles run the self-test on the wall clock (`SelfTest::realTime`). The other suites step a fixed 1/60 s per frame, and that made processes drift apart under GPU load.

## Not validated (outstanding)

- **Separate PCs.** All tests ran as processes on one PC (loopback). Discovery across machines, firewall prompts, Wi-Fi and real packet loss, and cross-machine content and map checks still need the user to run two PCs.
- No lag compensation (rewind). A client's shot is resolved where the target is on the host when the request arrives, so on a LAN this is a few milliseconds.
- **Host-only in LAN matches:** moving a gun carriage (the PaK 40's Move position), removing or undoing placements in build mode, and the carried M2HB's deploy / pack-up (a rack's M2HB tripod deployment goes through build mode and works). Clients get a message. Detached leaves and debris are simulated by each PC separately (cosmetic).
- Snapshots with 32 soldiers, many grenades and crewed guns approach the 1.4 kB packet limit. A snapshot that doesn't fit is not sent, so a full 32-player match with all of that at once has not been measured.
- Audio from replayed late-join events is muted by moving the listener; the visual debris of past events is not recreated.
