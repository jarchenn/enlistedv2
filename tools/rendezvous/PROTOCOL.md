# NMKR rendezvous / relay protocol (version 1)

This is the wire format between the game (`src/inet.cpp`) and `rendezvous.py`. Every message is a single UDP datagram over IPv4.

The game sends all of these messages from **its existing game socket**, the one bound for `NMK1` traffic. The server records the source address it sees (the "observed endpoint" or `pub`), and that is the NAT mapping the game itself uses.

## 1. Framing

```
offset  size  field
0       4     magic   = the 4 ASCII bytes 'N' 'M' 'K' 'R'  (0x4E 0x4D 0x4B 0x52)
4       1     type    = u8, one of the ASCII letters in section 2
5       ...   payload = depends on type (section 3)
```

- **Byte order:** every multi-byte integer field is **little-endian**. Read as a little-endian u32, the magic is `0x524B4D4E`. The game's own magic `NMK1` is `0x314B4D4E`, so the two never collide.
- **Size:** the **maximum datagram is 1500 bytes**, header included. The server silently drops anything longer, shorter than 5 bytes, or with the wrong magic.
- **Control payloads:** UTF-8 JSON, a single **object**, filling the rest of the datagram. There is no length prefix, no NUL terminator and no trailing newline. The server writes compact JSON (no spaces). It ignores unknown keys, and clients must ignore unknown keys too.
- **Relay payloads:** binary. Type `L` is not JSON (section 3.6).
- **Bad input:** the server drops malformed JSON and non-object JSON without replying.

## 2. Message types

| Type byte | Name | Direction | Payload |
|---|---|---|---|
| `R` (0x52) | REG | host → server | JSON |
| `r` (0x72) | REGOK | server → host | JSON |
| `K` (0x4B) | LOOK | client → server | JSON |
| `O` (0x4F) | ROOM | server → client | JSON |
| `N` (0x4E) | NOROOM | server → client | JSON |
| `V` (0x56) | VERSION | server → client | JSON |
| `I` (0x49) | INTRO | server → host | JSON |
| `Y` (0x59) | RELAY | client → server | JSON |
| `y` (0x79) | RELAYOK | server → client **and** server → host | JSON |
| `L` (0x4C) | relayed packet | client ↔ server ↔ host | binary |
| `E` (0x45) | ERR | server → any | JSON |
| `P` (0x50) | PUNCH | client ↔ host directly | binary (section 4), never sent to the server |
| `p` (0x70) | PUNCHOK | client ↔ host directly | binary (section 4), never sent to the server |

The server ignores any other type byte, including `P`/`p`.

## 3. Messages

### Field types and limits (all JSON messages)

| Field | JSON type | Limit / format |
|---|---|---|
| `code` | string | Room code. Six characters from the alphabet `23456789ABCDEFGHJKMNPQRSTUVWXYZ`, which has no `0 O 1 I L`. The server normalises incoming codes: it removes spaces and `-`, upper-cases, and then requires exactly 6 alphabet characters. Input strings over 16 characters are invalid. The server always sends codes in canonical form. |
| `ver` | integer | 0..65535. The game's `kNetVersion`. |
| `name` | string | Room / host name. The server truncates it to **64 UTF-8 bytes**. |
| `map` | string | Map name. Truncated to **64 UTF-8 bytes**. |
| `humans`, `max` | integer | 0..255. Any other value becomes 0. |
| `lan` | array of strings | At most **4** IPv4 endpoints, each `"a.b.c.d:port"` in decimal (port 1..65535, at most 21 characters). The server drops invalid entries and duplicates and keeps the first 4 valid ones. |
| `pub` | string | `"a.b.c.d:port"`, the observed public endpoint as the server saw it. |
| `nonce` | integer | 0..4294967295 (u32). Chosen by the client. Anything else becomes 0. |
| `rid` | integer | 1..4294967295 (u32, never 0). A relay id, chosen randomly by the server. |
| `secret` | string | At most 64 UTF-8 bytes. Only used in REG. |

Booleans are never accepted as integers.

### 3.1 REG (`R`) and REGOK (`r`): register or refresh a room

The host sends this **every 5 s** for as long as the room should be joinable or relayed. A room expires **15 s after its last REG**.

```json
{"code":"K7Q2MX","ver":2,"name":"Alex's game","map":"Omaha","humans":1,"max":8,
 "lan":["192.168.1.20:27960"],"secret":"..."}
```

- `ver` is **required**. A REG without a valid `ver` is dropped.
- `code` is optional. Omit it on the first REG; afterwards, send the code from the last REGOK.
- `secret` is required only when the server has one configured.

The server replies to the host:

```json
{"code":"K7Q2MX","pub":"203.0.113.7:51234","ttl":15}
```

How the server chooses the code:
1. The **room owner is the exact source endpoint** (IP and port). One endpoint owns at most one room.
2. If this endpoint already owns a live room and `code` is absent or equal to it, the server refreshes that room and updates name, map, humans, max and lan.
3. Otherwise, if `code` is valid and free (for example after a server restart or after the room expired), the host gets that code back. If the endpoint owned a different room, that room is deleted.
4. Otherwise, for example when the code is taken by another endpoint, the server assigns a **new random code**.

**The host must always display the code from the latest REGOK**, because it can change in case 4.

Errors are sent as ERR (section 3.7):
- `secret`: the secret is missing or wrong;
- `rooms`: this IP already has 64 rooms;
- `full`: the server already has 10000 rooms.

### 3.2 LOOK (`K`): find a room

The client sends:

```json
{"code":"K7Q2MX","ver":2,"nonce":3735928559,"lan":["192.168.1.30:27960"]}
```

- `nonce` defaults to 0 and `lan` to `[]`, but clients should always send both.
- `nonce` should be random and is reused for this join attempt's PUNCH and RELAY.

The server replies with one of:
- **ROOM (`O`)**, to the client:
  ```json
  {"code":"K7Q2MX","pub":"203.0.113.7:51234","lan":["192.168.1.20:27960"],"name":"Alex's game",
   "map":"Omaha","ver":2,"humans":1,"max":8,"nonce":3735928559}
  ```
- **NOROOM (`N`)**: `{"code":"K7Q2MX"}`. `code` is `""` if the requested code was invalid. This means the room is unknown or expired.
- **VERSION (`V`)**: `{"code":"K7Q2MX","ver":2}`. The room exists but runs a different `ver` (the host's is given). No INTRO is sent.

When it replies ROOM, the server **first** sends the host an **INTRO (`I`)**:

```json
{"code":"K7Q2MX","pub":"198.51.100.9:61000","lan":["192.168.1.30:27960"],"nonce":3735928559}
```

Here `pub` and `lan` belong to the client. Both sides then punch (section 4).

Every LOOK that gets a ROOM produces an INTRO. Repeating a LOOK is safe; the host should treat an INTRO with the same (`pub`, `nonce`) as the same attempt.

### 3.3 RELAY (`Y`) and RELAYOK (`y`): allocate a relay after punching fails

The client sends:

```json
{"code":"K7Q2MX","nonce":3735928559}
```

The server allocates relay id `rid` for the pair (this client endpoint, the room's host endpoint) and sends RELAYOK twice:
1. to the **host**: `{"rid":2864434397,"code":"K7Q2MX","nonce":3735928559,"pub":"198.51.100.9:61000"}`. `pub` is the client's endpoint. The host may use it to stop punching that client.
2. to the **client**: `{"rid":2864434397,"code":"K7Q2MX","nonce":3735928559}`.

Rules:
- **Idempotent:** a repeated RELAY from the same client endpoint to the same host returns the **same `rid`** and refreshes its `nonce`. Clients should resend RELAY every 500 ms until they get RELAYOK, up to a timeout.
- An unknown or expired code gets **NOROOM**.
- A RELAY sent by the room's own host is ignored.
- ERR `relays`: this room already has 16 relays.
- ERR `full`: the server already has 4096 relays.

The relay does **not** depend on the room staying registered. It lives until **30 s pass with no `L` traffic in either direction**. Even so, hosts should keep sending REG during a match so new players can join.

### 3.4 INTRO (`I`)

INTRO is server → host only (see 3.2). The host should cap concurrent punch attempts (8 is suggested) and ignore further INTROs. INTROs cost the server nothing, so a flood of LOOKs turns into a flood of INTROs.

### 3.5 NOROOM / VERSION

See 3.2.

### 3.6 `L`: relayed game packet (binary)

```
offset  size  field
0       4     magic 'N' 'M' 'K' 'R'
4       1     type  'L' (0x4C)
5       4     rid   u32, little-endian, != 0
9       n     payload: one complete game datagram (normally starting with NMK1), 0 <= n <= 1491
```

- The client sends `L` to the server. The server checks that the source is that relay's client endpoint and forwards the **identical datagram, byte for byte**, to the host endpoint.
- The host sends `L` with the same `rid` to the server. The server checks that the source is that relay's host endpoint and forwards the identical datagram to the client.
- `L` from any other source is dropped. A third party can't inject into a relay even when it knows the `rid`.
- An `L` with a `rid` the server doesn't know, or one that has expired, gets **ERR `norelay`** `{"err":"norelay","rid":N}`. That reply counts toward the rate limit. A client that receives it should run RELAY again (it gets a new rid) or give up.
- The host tells relayed peers apart **by `rid`**. Every packet from the server's address arrives from the same `sockaddr`, so the host must key relayed peers on `rid` (a virtual address) rather than on the source address.
- The maximum payload is 1491 bytes, because the whole datagram is at most 1500. Game snapshots are capped at 1400.
- If a bandwidth cap (section 5) is exceeded, the server drops the packet silently. The game's reliable channel resends.

### 3.7 ERR (`E`)

```json
{"err":"secret"}
```

| `err` | Meaning | Sent in reply to |
|---|---|---|
| `secret` | Wrong or missing shared secret | REG |
| `rooms` | This IP already has 64 rooms | REG |
| `full` | Server room or relay table is full | REG, RELAY |
| `relays` | This room already has 16 relays | RELAY |
| `norelay` | Unknown or expired `rid` (an extra `rid` field is included) | `L` |

Clients must ignore unknown `err` values. Other problems get **no reply at all**: malformed packets, rate-limited requests, an oversize datagram, or an `L` from the wrong endpoint. Clients need timeouts and retries for those.

## 4. PUNCH / PUNCHOK (client ↔ host, direct)

The server never sees these. The format is fixed here so both sides of the game agree:

```
offset  size  field
0       4     magic 'N' 'M' 'K' 'R'
4       1     type  'P' (PUNCH) or 'p' (PUNCHOK)
5       4     nonce u32, little-endian (the LOOK nonce)
```

The payload is exactly 9 bytes.

1. After ROOM (client) or INTRO (host), each side sends PUNCH every 100 ms for 3 s to **every** candidate endpoint of the other side: `pub` first, then each `lan` entry.
2. When either side receives a PUNCH whose nonce matches, it replies PUNCHOK to the packet's **source address** and fixes that address as the peer endpoint.
3. On PUNCHOK, or on any matching PUNCH, the client stops punching and starts the normal `H`/`W` handshake to that address.
4. If 3 s pass without a match, the client falls back to RELAY (3.3).

## 5. Server limits

| Limit | Value |
|---|---|
| Datagram size | ≤ 1500 bytes (larger ones are dropped) |
| Rooms per source IP | 64 |
| Rooms total | 10000 |
| Room expiry | 15 s after the last REG |
| Control requests per source IP | 300 per 60 s window (REG, LOOK, RELAY, and `L` packets answered with `norelay`) |
| REG refresh exemption | A REG from the endpoint that owns a room, at least 1 s after that room's previous REG, does not count toward the 300 (so many hosts behind one campus NAT can keep refreshing) |
| Relays per room / total | 16 / 4096 |
| Relay expiry | 30 s without `L` traffic |
| Relay bandwidth per room (per host endpoint) | 2 MB/s, 1 s burst |
| Relay bandwidth total | 20 MB/s, 1 s burst |

**What counts:**
- Each 1 MB here is 1,000,000 bytes of whole forwarded datagrams.
- The rate-limit window is fixed: it starts with the first request from an IP and resets 60 s later.
- `L` traffic on a valid relay never counts toward the request limit; only the bandwidth caps apply to it.

## 6. Security notes

- **Plaintext:** everything is unencrypted UDP. Room codes are not secrets once they are shared, and the `secret` travels in plaintext. The secret only keeps casual third-party builds from registering rooms.
- **Spoofing:**
  - The relay checks the exact source endpoint, which is enough to stop off-path injection.
  - Game-level spoofing is handled by the session token in `W`/`D`/`X`, not by this server.
- **Amplification:** a spoofed LOOK produces one ROOM reply (about 200 bytes) to the spoofed address, and that is rate-limited per source IP.
- **Stored data:** the server keeps no game data. It holds rooms, endpoints and relay pairs in memory only, and loses them on restart; hosts re-register with their old code within 5 s.

## 7. Quick probe

```bash
printf 'NMKRK{"code":"TEST22","ver":0}' | nc -u -w2 <server-ip> 27900
# expected reply:  NMKRN{"code":"TEST22"}
```

Without nc: `python3 -I rendezvous.py --probe <server-ip>:27900`.
