#!/usr/bin/env python3
"""Normandy Mapmaker rendezvous + relay server (internet play).

One file, Python 3.8+ standard library only. Wire format: see PROTOCOL.md next to this file.

  python3 -I rendezvous.py                 run the server (UDP 0.0.0.0:27900)
  python3 -I rendezvous.py --selftest      run the built-in tests on 127.0.0.1 (ephemeral port)
  python3 -I rendezvous.py --probe HOST[:PORT]   send one LOOK to a running server and print the reply

The server keeps no game state: only rooms, observed endpoints and relay pairs, in memory.
Optional settings come from rendezvous.conf (next to this file, or --conf PATH):
  secret = some-shared-string     # REG must carry the same "secret"; empty/absent = open
  port   = 27900
  bind   = 0.0.0.0
"""
import argparse
try:
    import asyncio
except ImportError:          # stripped embeddable Python: fall back to a plain socket loop
    asyncio = None
import hmac
import json
import os
import secrets
import socket
import struct
import sys
import threading
import time

MAGIC = b"NMKR"
DEFAULT_PORT = 27900
ALPHABET = "23456789ABCDEFGHJKMNPQRSTUVWXYZ"   # no 0/O, 1/I/L
CODE_LEN = 6

# message types (u8, printable ASCII so a probe can be typed with printf | nc -u)
T_REG, T_REGOK = ord("R"), ord("r")
T_LOOK, T_ROOM, T_NOROOM, T_VERSION = ord("K"), ord("O"), ord("N"), ord("V")
T_INTRO = ord("I")
T_RELAY, T_RELAYOK = ord("Y"), ord("y")
T_L = ord("L")
T_ERR = ord("E")
T_PUNCH, T_PUNCHOK = ord("P"), ord("p")       # client <-> host only; the server ignores them
CONTROL = (T_REG, T_LOOK, T_RELAY)


class Limits:
    max_dgram = 1500            # bytes, whole datagram
    rooms_per_ip = 64
    room_ttl = 15.0             # s after the last REG
    req_per_min = 300           # control requests per source IP per 60 s window
    refresh_free = 1.0          # a REG refresh from the room owner >= this many s apart is not counted
    room_bw = 2_000_000         # relay bytes/s per room (per host endpoint), 1 s burst
    total_bw = 20_000_000       # relay bytes/s for the whole server, 1 s burst
    max_rooms = 10000
    relays_per_room = 16
    max_relays = 4096
    relay_ttl = 30.0            # s without any L traffic in either direction
    max_ips = 100000            # rate-limit table size
    name_max = 64               # UTF-8 bytes
    map_max = 64
    secret_max = 64
    lan_max = 4


# ---------------------------------------------------------------- helpers
def norm_code(v):
    """Normalise a typed room code: drop spaces/dashes, upper-case. None if not a valid code."""
    if not isinstance(v, str) or len(v) > 16:
        return None
    c = v.replace(" ", "").replace("-", "").upper()
    if len(c) != CODE_LEN or any(ch not in ALPHABET for ch in c):
        return None
    return c


def get_int(m, key, lo, hi, default=None):
    v = m.get(key, default)
    if isinstance(v, bool) or not isinstance(v, int) or v < lo or v > hi:
        return default
    return v


def get_str(m, key, maxbytes):
    v = m.get(key, "")
    if not isinstance(v, str):
        return ""
    b = v.encode("utf-8", "replace")
    if len(b) > maxbytes:
        v = b[:maxbytes].decode("utf-8", "ignore")
    return v


def parse_ep(s):
    """'a.b.c.d:port' -> canonical string, or None."""
    if not isinstance(s, str) or len(s) > 21:
        return None
    host, sep, port = s.rpartition(":")
    parts = host.split(".")
    if not sep or len(parts) != 4 or not port.isdigit() or len(port) > 5:
        return None
    if not all(p.isdigit() and 1 <= len(p) <= 3 and int(p) <= 255 for p in parts):
        return None
    pt = int(port)
    if not 1 <= pt <= 65535:
        return None
    return "%d.%d.%d.%d:%d" % (int(parts[0]), int(parts[1]), int(parts[2]), int(parts[3]), pt)


def parse_lan(v, n):
    out = []
    if isinstance(v, list):
        for s in v[:16]:
            e = parse_ep(s)
            if e and e not in out:
                out.append(e)
            if len(out) >= n:
                break
    return out


def ep_str(addr):
    return "%s:%d" % (addr[0], addr[1])


def pack(t, obj):
    return MAGIC + bytes((t,)) + json.dumps(obj, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


class Bucket:
    __slots__ = ("rate", "tokens", "last")

    def __init__(self, rate, now):
        self.rate, self.tokens, self.last = float(rate), float(rate), now

    def refill(self, now):
        if now > self.last:
            self.tokens = min(self.rate, self.tokens + (now - self.last) * self.rate)
        self.last = now


class Room:
    __slots__ = ("code", "host", "ver", "name", "map", "humans", "max", "lan", "last_reg")


class Relay:
    __slots__ = ("rid", "code", "client", "host", "nonce", "last")


# ---------------------------------------------------------------- server
class Server:
    def __init__(self, secret="", limits=None, clock=time.monotonic):
        self.secret = secret or ""
        self.lim = limits or Limits()
        self.clock = clock
        self.rooms = {}            # code -> Room
        self.owner = {}            # host (ip, port) -> code
        self.ip_rooms = {}         # ip -> number of rooms
        self.relays = {}           # rid -> Relay
        self.relay_key = {}        # (client addr, host addr) -> rid
        self.host_relays = {}      # host addr -> set(rid)
        self.rate = {}             # ip -> [window_start, count]
        self.room_bw = {}          # host addr -> Bucket
        self.total_bw = None
        self.transport = None
        self.stats = dict.fromkeys(("rx", "dropped", "ratelimited", "relayed", "relay_bytes",
                                    "bw_dropped", "relay_spoof"), 0)
        self.rand_code = lambda: "".join(secrets.choice(ALPHABET) for _ in range(CODE_LEN))
        self._last_rate_sweep = 0.0

    # -- transport
    def send(self, data, addr):
        if self.transport is not None:
            self.transport.sendto(data, addr)

    def reply(self, addr, t, obj):
        self.send(pack(t, obj), addr)

    # -- bookkeeping
    def _allow(self, ip, now):
        w = self.rate.get(ip)
        if w is None:
            if len(self.rate) >= self.lim.max_ips:
                return False
            w = self.rate[ip] = [now, 0]
        elif now - w[0] >= 60.0:
            w[0], w[1] = now, 0
        w[1] += 1
        if w[1] > self.lim.req_per_min:
            self.stats["ratelimited"] += 1
            return False
        return True

    def _room(self, code, now):
        r = self.rooms.get(code) if code else None
        if r is not None and now - r.last_reg > self.lim.room_ttl:
            self._drop_room(r)
            r = None
        return r

    def _drop_room(self, r):
        self.rooms.pop(r.code, None)
        if self.owner.get(r.host) == r.code:
            del self.owner[r.host]
        ip = r.host[0]
        n = self.ip_rooms.get(ip, 0) - 1
        if n > 0:
            self.ip_rooms[ip] = n
        else:
            self.ip_rooms.pop(ip, None)

    def _drop_relay(self, rl):
        self.relays.pop(rl.rid, None)
        self.relay_key.pop((rl.client, rl.host), None)
        s = self.host_relays.get(rl.host)
        if s is not None:
            s.discard(rl.rid)
            if not s:
                del self.host_relays[rl.host]
                self.room_bw.pop(rl.host, None)

    def _new_code(self, now):
        for _ in range(64):
            c = self.rand_code()
            if self._room(c, now) is None:
                return c
        return None

    def sweep(self):
        now = self.clock()
        for r in [r for r in self.rooms.values() if now - r.last_reg > self.lim.room_ttl]:
            self._drop_room(r)
        for rl in [rl for rl in self.relays.values() if now - rl.last > self.lim.relay_ttl]:
            self._drop_relay(rl)
        if now - self._last_rate_sweep >= 10.0:
            self._last_rate_sweep = now
            for ip in [ip for ip, w in self.rate.items() if now - w[0] >= 60.0]:
                del self.rate[ip]

    # -- entry point
    def handle(self, data, addr):
        st = self.stats
        st["rx"] += 1
        n = len(data)
        if n < 5 or n > self.lim.max_dgram or data[:4] != MAGIC:
            st["dropped"] += 1
            return
        t = data[4]
        now = self.clock()
        if t == T_L:
            self._on_relay_data(data, addr, now)
            return
        if t not in CONTROL:
            st["dropped"] += 1
            return
        free = False
        if t == T_REG:
            code = self.owner.get(addr)
            r = self.rooms.get(code) if code else None
            free = r is not None and now - r.last_reg >= self.lim.refresh_free
        if not free and not self._allow(addr[0], now):
            return
        try:
            m = json.loads(data[5:].decode("utf-8"))
        except Exception:
            st["dropped"] += 1
            return
        if not isinstance(m, dict):
            st["dropped"] += 1
            return
        if t == T_REG:
            self._on_reg(m, addr, now)
        elif t == T_LOOK:
            self._on_look(m, addr, now)
        else:
            self._on_relay_req(m, addr, now)

    def _on_reg(self, m, addr, now):
        lim = self.lim
        if self.secret:
            s = m.get("secret", "")
            if not isinstance(s, str) or not hmac.compare_digest(s.encode("utf-8", "replace")[:lim.secret_max],
                                                                  self.secret.encode("utf-8")):
                self.reply(addr, T_ERR, {"err": "secret"})
                return
        ver = get_int(m, "ver", 0, 65535)
        if ver is None:
            self.stats["dropped"] += 1
            return
        want = norm_code(m.get("code"))
        room = self._room(self.owner.get(addr), now)
        if room is None or (want is not None and want != room.code):
            if room is not None:
                self._drop_room(room)
            if self.ip_rooms.get(addr[0], 0) >= lim.rooms_per_ip:
                self.reply(addr, T_ERR, {"err": "rooms"})
                return
            if len(self.rooms) >= lim.max_rooms:
                self.reply(addr, T_ERR, {"err": "full"})
                return
            code = want if (want is not None and self._room(want, now) is None) else self._new_code(now)
            if code is None:
                self.reply(addr, T_ERR, {"err": "full"})
                return
            room = Room()
            room.code, room.host = code, addr
            self.rooms[code] = room
            self.owner[addr] = code
            self.ip_rooms[addr[0]] = self.ip_rooms.get(addr[0], 0) + 1
        room.ver = ver
        room.name = get_str(m, "name", lim.name_max)
        room.map = get_str(m, "map", lim.map_max)
        room.humans = get_int(m, "humans", 0, 255, 0)
        room.max = get_int(m, "max", 0, 255, 0)
        room.lan = parse_lan(m.get("lan"), lim.lan_max)
        room.last_reg = now
        self.reply(addr, T_REGOK, {"code": room.code, "pub": ep_str(addr), "ttl": int(lim.room_ttl)})

    def _on_look(self, m, addr, now):
        code = norm_code(m.get("code"))
        room = self._room(code, now)
        if room is None:
            self.reply(addr, T_NOROOM, {"code": code or ""})
            return
        ver = get_int(m, "ver", 0, 65535, 0)
        if ver != room.ver:
            self.reply(addr, T_VERSION, {"code": code, "ver": room.ver})
            return
        nonce = get_int(m, "nonce", 0, 0xFFFFFFFF, 0)
        lan = parse_lan(m.get("lan"), self.lim.lan_max)
        self.reply(room.host, T_INTRO, {"code": code, "pub": ep_str(addr), "lan": lan, "nonce": nonce})
        self.reply(addr, T_ROOM, {"code": code, "pub": ep_str(room.host), "lan": room.lan, "name": room.name,
                                  "map": room.map, "ver": room.ver, "humans": room.humans, "max": room.max,
                                  "nonce": nonce})

    def _on_relay_req(self, m, addr, now):
        code = norm_code(m.get("code"))
        room = self._room(code, now)
        if room is None:
            self.reply(addr, T_NOROOM, {"code": code or ""})
            return
        if addr == room.host:
            self.stats["dropped"] += 1
            return
        nonce = get_int(m, "nonce", 0, 0xFFFFFFFF, 0)
        rid = self.relay_key.get((addr, room.host))
        rl = self.relays.get(rid) if rid else None
        if rl is None:
            if len(self.host_relays.get(room.host, ())) >= self.lim.relays_per_room:
                self.reply(addr, T_ERR, {"err": "relays"})
                return
            if len(self.relays) >= self.lim.max_relays:
                self.reply(addr, T_ERR, {"err": "full"})
                return
            rid = 0
            while rid == 0 or rid in self.relays:
                rid = secrets.randbits(32)
            rl = Relay()
            rl.rid, rl.client, rl.host = rid, addr, room.host
            self.relays[rid] = rl
            self.relay_key[(addr, room.host)] = rid
            self.host_relays.setdefault(room.host, set()).add(rid)
        rl.code, rl.nonce, rl.last = code, nonce, now
        self.reply(room.host, T_RELAYOK, {"rid": rl.rid, "code": code, "nonce": nonce, "pub": ep_str(addr)})
        self.reply(addr, T_RELAYOK, {"rid": rl.rid, "code": code, "nonce": nonce})

    def _on_relay_data(self, data, addr, now):
        st = self.stats
        if len(data) < 9:
            st["dropped"] += 1
            return
        rid = struct.unpack_from("<I", data, 5)[0]
        rl = self.relays.get(rid)
        if rl is not None and now - rl.last > self.lim.relay_ttl:
            self._drop_relay(rl)
            rl = None
        if rl is None:
            if rid and self._allow(addr[0], now):
                self.reply(addr, T_ERR, {"err": "norelay", "rid": rid})
            return
        if addr == rl.client:
            dst = rl.host
        elif addr == rl.host:
            dst = rl.client
        else:
            st["relay_spoof"] += 1
            return
        n = len(data)
        b = self.room_bw.get(rl.host)
        if b is None:
            b = self.room_bw[rl.host] = Bucket(self.lim.room_bw, now)
        g = self.total_bw
        if g is None or g.rate != self.lim.total_bw:
            g = self.total_bw = Bucket(self.lim.total_bw, now)
        if b.rate != self.lim.room_bw:
            b.rate = float(self.lim.room_bw)
        b.refill(now)
        g.refill(now)
        if b.tokens < n or g.tokens < n:
            st["bw_dropped"] += 1
            return
        b.tokens -= n
        g.tokens -= n
        rl.last = now
        st["relayed"] += 1
        st["relay_bytes"] += n
        self.send(data, dst)


def tune_socket(sock):
    for opt in (socket.SO_RCVBUF, socket.SO_SNDBUF):
        try:
            sock.setsockopt(socket.SOL_SOCKET, opt, 1 << 20)
        except OSError:
            pass
    if hasattr(socket, "SIO_UDP_CONNRESET"):   # Windows: an ICMP port-unreachable must not break recv
        try:
            sock.ioctl(socket.SIO_UDP_CONNRESET, False)
        except (OSError, AttributeError, ValueError):
            pass


def safe_handle(srv, data, addr):
    try:
        srv.handle(data, addr[:2])
    except Exception as e:     # never let one packet kill the server
        srv.stats["dropped"] += 1
        print("rendezvous: error handling packet: %r" % (e,), file=sys.stderr, flush=True)


class Ticker:
    """Runs sweep() about once a second and prints a stats line every log_every seconds."""

    def __init__(self, srv, log_every):
        self.srv, self.log_every = srv, log_every
        self.last_sweep = self.last_log = time.monotonic()

    def tick(self):
        now = time.monotonic()
        if now - self.last_sweep >= 0.25:
            self.last_sweep = now
            self.srv.sweep()
        if self.log_every and now - self.last_log >= self.log_every:
            self.last_log = now
            srv, s = self.srv, self.srv.stats
            print("rendezvous: rooms=%d relays=%d rx=%d relayed=%d (%.1f MB) dropped=%d ratelimited=%d bw_dropped=%d"
                  % (len(srv.rooms), len(srv.relays), s["rx"], s["relayed"], s["relay_bytes"] / 1e6,
                     s["dropped"], s["ratelimited"], s["bw_dropped"]), flush=True)


if asyncio is not None:
    class Proto(asyncio.DatagramProtocol):
        def __init__(self, srv):
            self.srv = srv

        def connection_made(self, transport):
            self.srv.transport = transport
            sock = transport.get_extra_info("socket")
            if sock is not None:
                tune_socket(sock)

        def datagram_received(self, data, addr):
            safe_handle(self.srv, data, addr)

        def error_received(self, exc):
            pass


async def serve_async(srv, bind, port, ready, stop, log_every):
    loop = asyncio.get_running_loop()
    transport, _ = await loop.create_datagram_endpoint(lambda: Proto(srv), local_addr=(bind, port),
                                                       family=socket.AF_INET)
    if ready is not None:
        ready(transport.get_extra_info("sockname")[1])
    ticker = Ticker(srv, log_every)
    try:
        while stop is None or not stop.is_set():
            await asyncio.sleep(0.1)
            ticker.tick()
    finally:
        transport.close()


def serve_plain(srv, bind, port, ready, stop, log_every):
    """Fallback for Pythons without a usable asyncio (e.g. a stripped embeddable Windows build)."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.bind((bind, port))
        tune_socket(sock)
        sock.settimeout(0.1)
        srv.transport = sock
        if ready is not None:
            ready(sock.getsockname()[1])
        ticker = Ticker(srv, log_every)
        while stop is None or not stop.is_set():
            try:
                data, addr = sock.recvfrom(65536)
            except socket.timeout:
                data = None
            except OSError:          # e.g. WSAECONNRESET / WSAEMSGSIZE: skip this datagram
                data = None
            if data is not None:
                safe_handle(srv, data, addr)
            ticker.tick()
    finally:
        srv.transport = None
        sock.close()


def run_server(srv, bind, port, ready=None, stop=None, log_every=300.0, plain=False):
    if plain or asyncio is None:
        serve_plain(srv, bind, port, ready, stop, log_every)
        return
    loop = asyncio.SelectorEventLoop() if sys.platform == "win32" else asyncio.new_event_loop()
    try:
        loop.run_until_complete(serve_async(srv, bind, port, ready, stop, log_every))
    finally:
        loop.close()


def load_conf(path):
    conf = {}
    if path and os.path.isfile(path):
        with open(path, "r", encoding="utf-8") as f:
            for line in f:
                line = line.split("#", 1)[0].strip()
                if "=" in line:
                    k, v = line.split("=", 1)
                    conf[k.strip().lower()] = v.strip().strip('"').strip("'")
    return conf


# ---------------------------------------------------------------- probe
def probe(target):
    host, _, port = target.rpartition(":") if ":" in target else (target, "", str(DEFAULT_PORT))
    port = int(port or DEFAULT_PORT)
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(3.0)
    try:
        s.sendto(MAGIC + b'K{"code":"TEST22","ver":0}', (host, port))
        data, frm = s.recvfrom(2048)
    except (socket.timeout, OSError) as e:
        print("no reply from %s:%d (%s). Check the VCN security list, the VM firewall and the service." % (host, port, e))
        return 1
    print("reply from %s: %r" % (ep_str(frm), data))
    return 0 if data[:4] == MAGIC else 1


# ---------------------------------------------------------------- selftest
class FakeClock:
    def __init__(self):
        self.t = 1000.0

    def __call__(self):
        return self.t


class Peer:
    def __init__(self, port):
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.s.bind(("127.0.0.1", 0))
        if hasattr(socket, "SIO_UDP_CONNRESET"):
            try:
                self.s.ioctl(socket.SIO_UDP_CONNRESET, False)
            except (OSError, AttributeError, ValueError):
                pass
        self.srv = ("127.0.0.1", port)
        self.addr = self.s.getsockname()
        self.ep = ep_str(self.addr)

    def raw(self, data, to=None):
        self.s.sendto(data, to or self.srv)

    def send(self, t, obj):
        self.raw(pack(ord(t), obj))

    def recv(self, timeout=1.0):
        """-> (type char, body dict or bytes, source addr) or None."""
        self.s.settimeout(timeout)
        try:
            data, frm = self.s.recvfrom(4096)
        except (socket.timeout, OSError):
            return None
        if data[:4] != MAGIC:
            return ("?", data, frm)
        t = chr(data[4])
        if t == "L":
            return (t, data, frm)
        return (t, json.loads(data[5:].decode("utf-8")), frm)

    def ask(self, t, obj, timeout=1.0):
        self.send(t, obj)
        return self.recv(timeout)

    def drain(self):
        while self.recv(0.05) is not None:
            pass

    def close(self):
        self.s.close()


def selftest(plain=False):
    clock = FakeClock()
    lim = Limits()
    srv = Server(limits=lim, clock=clock)
    ready_port = []
    started = threading.Event()
    stop = threading.Event()
    mode = "plain socket loop" if (plain or asyncio is None) else "asyncio"

    def run():
        run_server(srv, "127.0.0.1", 0, ready=lambda p: (ready_port.append(p), started.set()),
                   stop=stop, log_every=0, plain=plain)

    th = threading.Thread(target=run, daemon=True)
    th.start()
    if not started.wait(5.0):
        print("FAIL could not start server")
        return 1
    port = ready_port[0]
    print("rendezvous selftest: server on 127.0.0.1:%d (%s, Python %s)" % (port, mode, sys.version.split()[0]),
          flush=True)
    results = []
    peers = []

    def peer():
        p = Peer(port)
        peers.append(p)
        return p

    def check(name, cond, detail=""):
        results.append(bool(cond))
        print("%s %s%s" % ("PASS" if cond else "FAIL", name, "" if cond or not detail else ": " + str(detail)),
              flush=True)

    def reset_rate():
        srv.rate.clear()

    try:
        host = peer()
        cli = peer()
        reg = {"ver": 2, "name": "Test game", "map": "Omaha Dog Green", "humans": 1, "max": 8,
               "lan": ["192.168.1.20:27960", "bogus", "10.0.0.5:27960"]}

        # --- registration
        r = host.ask("R", reg)
        code = r[1].get("code") if r and r[0] == "r" else None
        check("REG -> REGOK with a 6-char code", code and norm_code(code) == code, r)
        check("REGOK reports observed public endpoint", r and r[1].get("pub") == host.ep, r)
        r2 = host.ask("R", dict(reg, code=code, humans=2))
        check("refresh keeps the same code", r2 and r2[0] == "r" and r2[1]["code"] == code, r2)
        r3 = host.ask("R", reg)
        check("REG without code from the owner returns its room", r3 and r3[1].get("code") == code, r3)
        check("one room per host endpoint", len(srv.rooms) == 1, len(srv.rooms))

        # --- lookup + intro
        r = cli.ask("K", {"code": code.lower()[:3] + "-" + code.lower()[3:], "ver": 2, "nonce": 12345,
                          "lan": ["192.168.1.30:27960"]})
        check("LOOK (lower case, dash) -> ROOM", r and r[0] == "O", r)
        if r and r[0] == "O":
            b = r[1]
            check("ROOM carries host pub/lan/name/map/nonce",
                  b["pub"] == host.ep and b["lan"] == ["192.168.1.20:27960", "10.0.0.5:27960"]
                  and b["name"] == reg["name"] and b["map"] == reg["map"] and b["nonce"] == 12345
                  and b["humans"] == 1 and b["max"] == 8 and b["ver"] == 2, b)
        i = host.recv()
        check("host gets INTRO with client pub/lan/nonce",
              i and i[0] == "I" and i[1]["pub"] == cli.ep and i[1]["lan"] == ["192.168.1.30:27960"]
              and i[1]["nonce"] == 12345 and i[1]["code"] == code, i)
        r = cli.ask("K", {"code": code, "ver": 1})
        check("LOOK with other version -> VERSION", r and r[0] == "V" and r[1]["ver"] == 2, r)
        r = cli.ask("K", {"code": "TEST22", "ver": 2})
        check("LOOK unknown code -> NOROOM", r and r[0] == "N" and r[1]["code"] == "TEST22", r)
        r = cli.ask("K", {"code": "O0I1L!", "ver": 2})
        check("LOOK invalid code -> NOROOM with empty code", r and r[0] == "N" and r[1]["code"] == "", r)
        host.drain()

        # --- code collisions
        h2 = peer()
        r = h2.ask("R", dict(reg, code=code))
        c2 = r[1].get("code") if r and r[0] == "r" else None
        check("REG asking for a taken code gets a different code", c2 and c2 != code, r)
        check("taken room still owned by its host", srv.rooms[code].host == host.addr)
        h3 = peer()
        r = h3.ask("R", dict(reg, code="K7Q2MX"))
        check("REG asking for a free valid code gets it (server restart)", r and r[1].get("code") == "K7Q2MX", r)
        seq = iter([code, c2, "K7Q2MX", "ZZZZZZ"])
        srv.rand_code = lambda: next(seq)
        h4 = peer()
        r = h4.ask("R", reg)
        check("generator collision retries until a free code", r and r[1].get("code") == "ZZZZZZ", r)
        srv.rand_code = lambda: "".join(secrets.choice(ALPHABET) for _ in range(CODE_LEN))
        r = h4.ask("R", dict(reg, code="22222O"))
        check("REG with invalid code keeps owner's room", r and r[1].get("code") == "ZZZZZZ", r)
        r = h4.ask("R", dict(reg, code="WWWWWW"))
        check("owner REG with a new free code moves the room",
              r and r[1].get("code") == "WWWWWW" and "ZZZZZZ" not in srv.rooms, r)

        # --- secret
        srv.secret = "s3cret"
        h5 = peer()
        r = h5.ask("R", reg)
        check("secret set: REG without secret -> ERR secret", r and r[0] == "E" and r[1]["err"] == "secret", r)
        r = h5.ask("R", dict(reg, secret="wrong"))
        check("secret set: wrong secret -> ERR secret", r and r[0] == "E", r)
        r = h5.ask("R", dict(reg, secret="s3cret"))
        check("secret set: right secret -> REGOK", r and r[0] == "r", r)
        srv.secret = ""

        # --- expiry
        clock.t += 10.0
        host.ask("R", dict(reg, code=code))
        clock.t += 10.0          # host refreshed 10 s ago, others 20 s ago
        r = cli.ask("K", {"code": code, "ver": 2})
        check("refreshed room survives 20 s", r and r[0] == "O", r)
        host.drain()
        r = cli.ask("K", {"code": c2, "ver": 2})
        check("room expires 15 s after last REG", r and r[0] == "N", r)
        srv.sweep()
        check("sweep removes expired rooms", list(srv.rooms) == [code], list(srv.rooms))
        r = h2.ask("R", dict(reg, code=c2))
        check("expired host re-registers with its old code", r and r[1].get("code") == c2, r)

        # --- rooms per IP
        reset_rate()
        many = [peer() for _ in range(lim.rooms_per_ip)]
        got = 0
        last = None
        for p in many:
            last = p.ask("R", reg)
            got += bool(last and last[0] == "r")
        check("64 rooms per IP enforced", len(srv.ip_rooms) == 1 and srv.ip_rooms["127.0.0.1"] == 64
              and last and last[0] == "E" and last[1]["err"] == "rooms",
              (srv.ip_rooms, got, last))
        for p in many:
            p.close()
            peers.remove(p)
        clock.t += 16.0
        srv.sweep()
        check("expired rooms free the per-IP quota", srv.ip_rooms == {}, srv.ip_rooms)

        # --- relay
        reset_rate()
        r = host.ask("R", dict(reg, code=code))
        check("host re-registers for relay test", r and r[0] == "r" and r[1]["code"] == code, r)
        r = cli.ask("Y", {"code": code, "nonce": 777})
        rid = r[1].get("rid") if r and r[0] == "y" else None
        check("RELAY -> RELAYOK with rid", isinstance(rid, int) and 0 < rid < 2 ** 32, r)
        h = host.recv()
        check("host gets RELAYOK copy with client pub",
              h and h[0] == "y" and h[1]["rid"] == rid and h[1]["pub"] == cli.ep and h[1]["nonce"] == 777, h)
        r = cli.ask("Y", {"code": code, "nonce": 778})
        check("repeated RELAY returns the same rid", r and r[1].get("rid") == rid, r)
        host.drain()
        r = host.ask("Y", {"code": code})
        check("host cannot relay to itself", r is None, r)
        hdr = MAGIC + b"L" + struct.pack("<I", rid or 0)
        cli.raw(hdr + b"NMK1hello")
        h = host.recv()
        check("client -> host relayed byte-exact", h and h[0] == "L" and h[1] == hdr + b"NMK1hello"
              and h[2][1] == port, h)
        host.raw(hdr + b"NMK1world")
        c = cli.recv()
        check("host -> client relayed byte-exact", c and c[1] == hdr + b"NMK1world", c)
        evil = peer()
        evil.raw(hdr + b"spoof")
        check("third party cannot inject into a relay", host.recv(0.3) is None and srv.stats["relay_spoof"] == 1)
        evil.raw(MAGIC + b"L" + struct.pack("<I", (rid or 0) ^ 0x5A5A5A5A) + b"x")
        e = evil.recv()
        check("unknown rid -> ERR norelay", e and e[0] == "E" and e[1]["err"] == "norelay", e)
        r = cli.ask("Y", {"code": "TEST22"})
        check("RELAY to unknown room -> NOROOM", r and r[0] == "N", r)
        c3 = peer()
        r = c3.ask("Y", {"code": code, "nonce": 1})
        rid3 = r[1].get("rid") if r and r[0] == "y" else None
        check("second relayed client gets a distinct rid", rid3 and rid3 != rid, r)
        host.drain()
        c3.raw(MAGIC + b"L" + struct.pack("<I", rid3 or 0) + b"from3")
        h = host.recv()
        check("host tells relayed clients apart by rid", h and struct.unpack_from("<I", h[1], 5)[0] == rid3, h)

        # --- bandwidth caps (lowered so the test is deterministic; clock is frozen)
        lim.room_bw = 10000
        srv.room_bw.clear()
        srv.total_bw = None
        payload = hdr + b"x" * (1000 - len(hdr))
        before = srv.stats["bw_dropped"]
        for _ in range(15):
            cli.raw(payload)
        n = 0
        while host.recv(0.3) is not None:
            n += 1
        check("per-room relay cap (10 of 15 x 1000 B at 10 kB/s)", n == 10 and srv.stats["bw_dropped"] - before == 5,
              (n, srv.stats["bw_dropped"] - before))
        clock.t += 1.0
        cli.raw(payload)
        check("per-room cap refills over time", host.recv() is not None)
        lim.room_bw = 2_000_000
        lim.total_bw = 3000
        srv.room_bw.clear()
        srv.total_bw = None
        for _ in range(5):
            cli.raw(payload)
        n = 0
        while host.recv(0.3) is not None:
            n += 1
        check("total relay cap (3 of 5 x 1000 B at 3 kB/s)", n == 3, n)
        lim.total_bw = 20_000_000
        srv.total_bw = None

        # --- relay expiry
        clock.t += lim.relay_ttl + 1
        host.ask("R", dict(reg, code=code))
        cli.raw(hdr + b"late")
        e = cli.recv()
        check("idle relay expires -> ERR norelay", e and e[0] == "E" and e[1].get("rid") == rid, e)
        srv.sweep()
        check("sweep removes idle relays", not srv.relays, list(srv.relays))
        host.drain()

        # --- malformed input
        before = srv.stats["dropped"]
        junk = [b"", b"NMK", b"NMKR", b"XXXXK{}", MAGIC + b"K", MAGIC + b"K{bad", MAGIC + b"K[1,2]",
                MAGIC + b"K" + b"[" * 1400, MAGIC + b"R{\"ver\":\"2\"}", MAGIC + b"Z{}", MAGIC + b"L\x01\x02",
                MAGIC + b"K\xff\xfe", MAGIC + b"K" + b"{" * 1495]
        for j in junk:
            cli.raw(j)
        check("malformed packets get no reply", cli.recv(0.3) is None)
        check("malformed packets counted as dropped", srv.stats["dropped"] - before >= len(junk) - 1,
              srv.stats["dropped"] - before)
        cli.raw(MAGIC + b'K{"code":"TEST22","ver":0,"x":"' + b"a" * 1500 + b'"}')
        check("datagram over 1500 bytes dropped", cli.recv(0.3) is None)
        r = cli.ask("R", {"ver": 2, "name": "é" * 100, "map": 5, "humans": True, "max": 999,
                          "lan": ["1.2.3.4:5", "1.2.3.4:5", "300.1.1.1:1", "1.2.3.4:0", 7] * 3})
        rm = srv.rooms.get(r[1]["code"]) if r and r[0] == "r" else None
        check("bad field values sanitised", rm and len(rm.name.encode()) <= 64 and rm.map == "" and rm.humans == 0
              and rm.max == 0 and rm.lan == ["1.2.3.4:5"], rm and (rm.name, rm.map, rm.humans, rm.max, rm.lan))

        # --- rate limit
        clock.t += 2.0
        reset_rate()
        ok = 0
        for _ in range(lim.req_per_min):
            r = cli.ask("K", {"code": "TEST22", "ver": 2})
            ok += bool(r and r[0] == "N")
        r = cli.ask("K", {"code": "TEST22", "ver": 2}, timeout=0.3)
        check("300 requests/min per IP allowed, 301st dropped", ok == lim.req_per_min and r is None, (ok, r))
        r = host.ask("R", dict(reg, code=code), timeout=0.5)
        check("owner REG refresh (>= 1 s apart) exempt from the limit", r and r[0] == "r", r)
        clock.t += 60.0
        r = cli.ask("K", {"code": "TEST22", "ver": 2})
        check("rate window resets after 60 s", r and r[0] == "N", r)
        check("server thread still alive", th.is_alive())
    finally:
        stop.set()
        th.join(3.0)
        for p in peers:
            p.close()
    passed = sum(results)
    print("rendezvous selftest: %d/%d passed" % (passed, len(results)), flush=True)
    return 0 if passed == len(results) else 1


# ---------------------------------------------------------------- main
def main(argv=None):
    ap = argparse.ArgumentParser(description="Normandy Mapmaker rendezvous + relay server")
    ap.add_argument("--selftest", action="store_true", help="run the built-in tests and exit")
    ap.add_argument("--probe", metavar="HOST[:PORT]", help="send one LOOK to a server and print the reply")
    ap.add_argument("--conf", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "rendezvous.conf"))
    ap.add_argument("--port", type=int)
    ap.add_argument("--bind")
    ap.add_argument("--secret", help="override the conf file's shared secret")
    ap.add_argument("--plain", action="store_true", help="use the plain socket loop instead of asyncio")
    a = ap.parse_args(argv)
    if a.selftest:
        return selftest(a.plain)
    if a.probe:
        return probe(a.probe)
    conf = load_conf(a.conf)
    port = a.port if a.port is not None else int(conf.get("port", DEFAULT_PORT))
    bind = a.bind or conf.get("bind", "0.0.0.0")
    srv = Server(secret=a.secret if a.secret is not None else conf.get("secret", ""))

    def ready(p):
        print("rendezvous: listening on udp %s:%d (secret %s, %s)"
              % (bind, p, "on" if srv.secret else "off", "plain" if (a.plain or asyncio is None) else "asyncio"),
              flush=True)

    try:
        run_server(srv, bind, port, ready=ready, plain=a.plain)
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
