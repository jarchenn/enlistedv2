# Co-op over LAN and the internet (listen server)

Up to 32 combatants. Humans on several PCs defend together against the attacking AI, with the same rules as singleplayer ([ai.md](ai.md)). One PC hosts and plays.
- **LAN:** the others join over the local network.
- **Internet:** the others join from anywhere with a **room code**, through a free rendezvous and relay server you run yourself. See [Internet play](#internet-play).

Host migration, dedicated servers and a public server browser are **not** part of this milestone.

## Internet play

**What you need:**
- A **room server**: `tools/rendezvous/rendezvous.py`, one Python file using only the standard library, on any always-on machine with a public IP and UDP port 27900 open. [`tools/rendezvous/README.md`](../tools/rendezvous/README.md) walks through a free **Oracle Cloud Always Free** VM step by step. The server is only an introducer and a relay; it holds no game state.
- On **every PC**, under **Internet settings** (in the host lobby and the Join window), the server address (`host` or `host:port`) and the server secret if you set one. These are saved to `net_user.json` next to the exe, outside `content/`, so the content hash is unaffected.

**Hosting:**
1. Play > Host LAN... > **Open lobby**.
2. Tick **Internet room**. After a moment the lobby shows **Room code: K7Q2MX**, with a Copy button.
3. Share the code. The code stays the same while the lobby is open. If the server restarts, it gives the same code back when it can.

**Joining:**
1. Open the same map file.
2. Play > Join LAN... > **Room code** > **Join room**.
3. The status shows each step: looking up the room, connecting directly, and, if that fails, "Direct connection failed: connecting through the relay...". Then you're in the normal lobby, which says "(through the relay)" when the relay is used.

**How it connects:**
- **Direct first:** the host registers its room with the server every 5 s. A joining client asks the server for the room. The server tells each side the other's public address (the one it saw) and LAN addresses. Both games then send small "punch" packets to each other for up to 3 s from their game socket, which opens a path through most home NATs. The first one to arrive fixes the address, and the normal handshake follows.
- **Relay as fallback:** symmetric NATs, campus and corporate Wi-Fi with client isolation, and some mobile networks block the direct path. The client then asks the server for a relay, and every game packet goes through the server, wrapped with a relay id. The host addresses relayed players by that id, so several relayed players never merge into one. Expect a little more delay, roughly the round trip to the server.
- **Same socket:** all of this, including lookups, punches and relayed packets, uses the game's own UDP socket. The address the server sees is therefore the one the game really uses.
- **Packet size:** every game datagram is at most 1360 bytes, so nothing is IP-fragmented on the internet. The host fills each 20 Hz snapshot with grenades in flight only while there is room.

**Security (the protocol is internet-facing; version 2):**
- **Session tokens:** each player gets a random 64-bit session token when welcomed. Every data or leave packet must carry it, so a stranger cannot kick a player or inject data.
- **Bounded memory:** the reliable channel ignores chunks beyond its 256-chunk window, and a message over 8 MB drops the peer. Senders stay inside the window.
- **Untrusted messages:**
  - Every request from a client, and every message from the host, is parsed defensively: a missing field or a wrong type is rejected and logged, and never crashes the game.
  - The player validation (movement, fire rate and ammo, throws, doors) is unchanged.
- **Unauthenticated packets:**
  - Discovery queries and join requests are limited to 4 per second per source.
  - A discovery query must be padded to 256 bytes, so the answer never amplifies.
  - The content and map hashes are cached instead of being recomputed for every packet.
- **Rendezvous traffic:** it is accepted only from the configured server. Relayed packets are unwrapped only when they come from it. The server checks the exact endpoint on both sides of every relay ([PROTOCOL.md](../tools/rendezvous/PROTOCOL.md)).
- **Not encrypted:** traffic is not encrypted. Room codes are not secrets once shared, and the optional server secret only keeps other builds from registering rooms.

**Limits:**
- IPv4 only.
- The host still runs the match on its own PC, and the match ends if it leaves.
- Relayed players count against the server's bandwidth (2 MB/s per room). A 32-player match is about 30–40 KB/s per relayed client.
- Nothing has been tested between real networks yet: all tests run on one PC (see Tests).

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

### Internet play

```bash
tools/run_inet_test.sh "%TEMP%/nmk/build-inet" 28980 28990
```

It starts the room server on 127.0.0.1:28990 (`tools/rendezvous/rendezvous.py`; override the path with `RENDEZVOUS=`) and four game processes. `NMK_INET_SERVER` points them at the server; test runs never write `net_user.json`.

| Role | Checks |
|---|---|
| ihost | opens an internet room and gets a 6-character code from the server, writes it to `selftest_out/inet_code.txt`, starts the match with both clients, breaches a wall. It then checks: one client came direct and one through the relay, both see its world (wall hash), the hostile traffic kicked nobody, and the clients' requests were validated (shots accepted, the over-rate fire and teleport rejected). |
| e | joins **by room code directly** (UDP hole punch over loopback), plays, misbehaves, reports, handles the host leaving |
| f | joins **by room code through the relay** (`NMK_INET_FORCE_RELAY=1`), the same play and report through the server |
| z | sends about 1900 hostile datagrams to the host: spoofed leave and data packets with guessed tokens for peers 1-4, truncated and oversized datagrams, bad hellos, a lying length prefix, 1300 queries, a fake relay packet, a fake REGOK, random noise. It then checks that the host still answers a well-formed discovery query. |

The room server has its own tests: `python -I tools/rendezvous/rendezvous.py --selftest`.

## Not validated (outstanding)

- **Separate PCs and real networks.** All tests (LAN and internet) ran as processes on one PC (loopback); hole punching through real NATs, the relay over the internet, campus Wi-Fi and the Oracle-hosted server are untested until the user runs them. Discovery across machines, firewall prompts, Wi-Fi and real packet loss, and cross-machine content and map checks still need the user to run two PCs.
- No lag compensation (rewind). A client's shot is resolved where the target is on the host when the request arrives, so on a LAN this is a few milliseconds.
- **Not replicated in LAN matches:** crew-served guns, supply crates, weapon racks and building field defences. Clients get a message saying they are host-only. Detached leaves and debris are simulated by each PC separately (cosmetic).
- Audio from replayed late-join events is muted by moving the listener; the visual debris of past events is not recreated.
