"""Caverns (CV1): a natural underground cave network on D-day's footprint (265 x 265 m), built for Team Deathmatch.

The terrain is the cave's floor and walls: a rock mass 50 m high everywhere, with the caverns and tunnels carved
down into it (signed-distance shapes with warped outlines; floors with their own relief, steep unclimbable walls).
The roof is a field of overlapping rock lids (tools/catalog_cave.mjs: icosphere hulls turned over, flat top up,
lumpy dome hanging down) whose undersides set each space's height: about 30 m in the central cavern, 18-26 m in the
outer caverns, 5-9 m in the tunnels. Every point of the cave has a lid above it, so nothing reaches the sky; the hour
is fixed at night and the sky is occluded anyway.

Layout: one huge central cavern (the landmark) with Team A's pocket in its middle (a ring of ribbed rock walls round
an ancient spire, four dog-legged exits behind baffles), six outer caverns on a looping tunnel ring, five radial
tunnels from the centre (one narrow), two flanking passages, ledges with ramps, a sunken lake (deep, visibly dark,
paths round it) and shallow flooded hollows. Team B spawns at many points round the outer caverns and the ring; the
engine picks one per respawn away from enemies and out of their sight (Game::trySpawn).

    <python> -I tools/mapgen/build.py caverns

Writes content/templates/caverns.{json,height.png,nmap.json}."""
import math
import os

from mapgen import (GroundPaint, Grid, Frame, MapBuilder, ScenarioBuilder, write_map, write_template, write_summary,
                    asset_usage, rng, look_yaw, catmull_rom, polyline_dist, polyline_length, resample, fbm, vnoise,
                    smoothstep, clamp, lerp)
from maps.backrooms import update_usage_doc
from maps.newtown import cover_catalogue

TID, NAME, VERSION, SEED = "caverns", "Caverns", 1, 19440612
W = D = 265.0                    # D-day's playable footprint (tools/mapgen/maps/dday.py: W = D = 265)
MARGIN = 50.0
PH = 95.0                        # the rock mass: every wall rises to this; lids sit below it (the central dome reaches ~66 m)
EYE = 1.65


# ---------------------------------------------------------------- the spaces
class Room:
    def __init__(self, name, c, rx, rz, rot, floor, clear, warp=3.0, seed=0):
        self.name, self.c, self.rx, self.rz, self.rot, self.floor, self.clear, self.warp, self.seed = name, c, rx, rz, math.radians(rot), floor, clear, warp, seed

    def local(self, x, z):
        dx, dz = x - self.c[0], z - self.c[1]
        cs, sn = math.cos(self.rot), math.sin(self.rot)
        return dx * cs + dz * sn, -dx * sn + dz * cs

    def d(self, x, z):
        u, v = self.local(x, z)
        q = math.hypot(u / self.rx, v / self.rz)
        return (q - 1.0) * min(self.rx, self.rz) + self.warp * (fbm(x / 17.0, z / 17.0, self.seed + 11, 3) - 0.5) * 2.0

    def floor_at(self, x, z):
        return self.floor

    def clear_at(self, x, z):
        u, v = self.local(x, z)
        q = min(1.0, math.hypot(u / self.rx, v / self.rz))
        return self.clear * (0.72 + 0.28 * (1.0 - q * q))   # a dome: highest in the middle


class Tunnel:
    def __init__(self, name, ctrl, width, f0, f1, clear, warp=1.2, seed=0):
        self.name = name
        self.pts = resample(catmull_rom(ctrl, 10), 2.0)
        self.L = polyline_length(self.pts)
        self.width, self.f0, self.f1, self.clear, self.warp, self.seed = width, f0, f1, clear, warp, seed
        xs, zs = [p[0] for p in self.pts], [p[1] for p in self.pts]
        m = width + 8
        self.bb = (min(xs) - m, min(zs) - m, max(xs) + m, max(zs) + m)

    def d(self, x, z):
        if x < self.bb[0] or z < self.bb[1] or x > self.bb[2] or z > self.bb[3]:
            return 1e9
        d = polyline_dist((x, z), self.pts)[0]
        w = self.width * (0.85 + 0.3 * vnoise(x / 9.0, z / 9.0, self.seed + 5))
        return d - w * 0.5 + self.warp * (fbm(x / 7.0, z / 7.0, self.seed + 13, 2) - 0.5) * 2.0

    def t_at(self, x, z):
        return polyline_dist((x, z), self.pts)[1] / max(self.L, 1e-6)

    def floor_at(self, x, z):
        t = self.t_at(x, z)
        return lerp(self.f0, self.f1, smoothstep(0.0, 1.0, t))

    def clear_at(self, x, z):
        return self.clear


# CV1b: expansive. Most of the footprint is cavern floor: a ~115 m central cavern under a 66 m dome, six outer caverns of
# different shapes (a dome, a long slot gallery, a tiered lake bowl, a wide hall, a high east dome, a grotto) opening into
# each other and into the centre through wide mouths (galleries 18-30 m wide with tall roofs); narrow winding passages
# are a minority (two flanks and a crawl).
C = Room("Central cavern", (0.0, 0.0), 60.0, 52.0, 12.0, 0.0, 66.0, warp=5.0, seed=1)
OUT = [
    Room("North vault", (-10.0, 101.0), 38.0, 25.0, -8.0, 5.0, 34.0, warp=4.0, seed=2),          # a dome
    Room("West gallery", (-106.0, 8.0), 20.0, 58.0, 4.0, 1.0, 30.0, warp=3.0, seed=3),          # a long, tall slot
    Room("Sunken lake", (-74.0, -90.0), 40.0, 33.0, 35.0, -3.0, 30.0, warp=4.0, seed=4),        # a tiered bowl round deep water
    Room("South hall", (36.0, -106.0), 44.0, 22.0, 6.0, 2.0, 25.0, warp=4.0, seed=5),           # wide and low-ish
    Room("East dome", (102.0, -14.0), 28.0, 40.0, -5.0, 5.0, 36.0, warp=4.0, seed=6),          # high
    Room("Grotto", (86.0, 86.0), 32.0, 30.0, 30.0, 8.0, 24.0, warp=5.0, seed=7),               # rounded, shelves
]
NG, WH, SL, SC, EV, NE = OUT


def mouth(name, a, b, width, clear, seed, bend=0.0):
    """A wide opening between two caverns: a short gallery as wide as a hall, so each chamber is seen from the next."""
    mx, mz = (a.c[0] + b.c[0]) / 2, (a.c[1] + b.c[1]) / 2
    tx, tz = b.c[1] - a.c[1], -(b.c[0] - a.c[0])
    tl = math.hypot(tx, tz) or 1.0
    return Tunnel(name, [a.c, (mx + tx / tl * bend, mz + tz / tl * bend), b.c], width, a.floor, b.floor, clear, warp=3.0, seed=seed)


TUN = [
    # the centre's mouths (Team A's ways out): north, west, south, east, wide; the south-west is a squeeze
    mouth("North mouth", C, NG, 30.0, 32.0, 31, 6.0), mouth("West mouth", C, WH, 26.0, 28.0, 32, -8.0),
    mouth("South mouth", C, SC, 28.0, 25.0, 33, 7.0), mouth("East mouth", C, EV, 26.0, 30.0, 34, -6.0),
    Tunnel("Lake crawl", [(-30.0, -30.0), (-44.0, -50.0), (-58.0, -66.0), SL.c], 7.0, 0.0, SL.floor, 8.0, seed=35),
    # the outer ring: caverns open into their neighbours through broad mouths
    mouth("North-west mouth", NG, WH, 22.0, 24.0, 21, 10.0), mouth("Lake mouth", WH, SL, 24.0, 26.0, 22, 8.0),
    mouth("Lake-hall mouth", SL, SC, 20.0, 22.0, 23, -10.0), mouth("South-east mouth", SC, EV, 22.0, 24.0, 24, 12.0),
    mouth("East-grotto mouth", EV, NE, 20.0, 22.0, 25, 8.0), mouth("Grotto-vault mouth", NE, NG, 18.0, 20.0, 26, -10.0),
    # narrow flanking passages (a minority): winding, low, dark
    Tunnel("Gallery flank", [(-58.0, 30.0), (-66.0, 52.0), (-48.0, 72.0), (-34.0, 86.0)], 6.5, 1.0, 5.0, 7.5, seed=41),
    Tunnel("Vault flank", [(60.0, 28.0), (76.0, 44.0), (70.0, 64.0)], 6.5, 3.0, 8.0, 7.5, seed=42),
]
SPACES = [C] + OUT + TUN

# raised shelves / tiers (centre, rise, half-length, half-width, yaw) and their ramps (from on the floor, to on the shelf)
LEDGES = [((-34.0, 30.0), 6.0, 18.0, 7.0, 35.0, [(-12.0, 38.0), (-28.0, 32.0)]),       # the central cavern's north-west shelf
          ((38.0, -28.0), 4.5, 16.0, 6.0, 30.0, [(16.0, -40.0), (32.0, -31.0)]),         # and its south-east shelf
          ((-92.0, -70.0), 3.5, 14.0, 6.0, 60.0, [(-80.0, -60.0), (-88.0, -66.0)]),       # the lake bowl's upper tier
          ((-58.0, -104.0), 3.0, 12.0, 5.0, 20.0, [(-62.0, -88.0), (-60.0, -98.0)]),      # and its south tier
          ((-26.0, 110.0), 5.0, 13.0, 5.0, -10.0, [(-6.0, 104.0), (-20.0, 108.0)]),       # the north vault's balcony
          ((112.0, -30.0), 4.0, 11.0, 5.0, 85.0, [(102.0, -4.0), (111.0, -22.0)]),        # an east dome tier
          ((96.0, 98.0), 3.5, 10.0, 5.0, 40.0, [(82.0, 86.0), (92.0, 94.0)])]             # a grotto shelf
# pools: (centre, radius x, radius z, max depth, water below the floor by)
POOLS = [((0.0, -21.0), 8.5, 8.5, 0.7, 0.3),         # the round basin in front of the spire, under the great shaft
         ((-40.0, -10.0), 12.0, 9.0, 1.2, 0.35),     # a flooded hollow in the central cavern
         ((-74.0, -92.0), 20.0, 15.0, 4.5, 0.35),    # the sunken lake: deep in the middle, paths and tiers round it
         ((-104.0, 40.0), 6.0, 9.0, 0.9, 0.3),       # a shallow pool along the west gallery
         ((40.0, -108.0), 8.0, 5.0, 1.0, 0.3),
         ((96.0, -40.0), 6.0, 5.0, 0.8, 0.3),
         ((80.0, 74.0), 6.0, 5.0, 0.8, 0.3)]
POCKET = (0.0, 0.0)                                   # Team A's pocket: raised 1.2 m, ringed by ribbed walls


def field(x, z):
    """(d, floor, clear, owner): signed distance to the cave (negative inside), floor and roof clearance here."""
    best, ws, fs, cs, own = 1e9, 0.0, 0.0, 0.0, None
    for s in SPACES:
        d = s.d(x, z)
        if d < best:
            best, own = d, s
        if d < 4.0:
            w = smoothstep(4.0, -3.0, d) + 1e-4
            ws += w
            fs += w * s.floor_at(x, z)
            cs += w * s.clear_at(x, z)
    if ws <= 0.0:
        return best, own.floor_at(x, z), own.clear_at(x, z), own
    return best, fs / ws, cs / ws, own


def ledge_add(x, z):
    add = 0.0
    for (cx, cz), rise, hl, hw, yaw, (ra, rb) in LEDGES:
        a = math.radians(yaw)
        u = (x - cx) * math.cos(a) + (z - cz) * math.sin(a)
        v = -(x - cx) * math.sin(a) + (z - cz) * math.cos(a)
        e = max(abs(u) / hl, abs(v) / hw)
        e += 0.08 * (vnoise(x / 3.0, z / 3.0, 77) - 0.5)
        add = max(add, rise * smoothstep(1.06, 0.98, e))   # a steep lip: climbed only by the ramp
        # ramp from ra (floor) to rb (ledge top), 4.5 m wide
        dx, dz = rb[0] - ra[0], rb[1] - ra[1]
        L2 = dx * dx + dz * dz
        t = clamp(((x - ra[0]) * dx + (z - ra[1]) * dz) / L2, 0.0, 1.0)
        dd = math.hypot(x - ra[0] - dx * t, z - ra[1] - dz * t)
        if dd < 3.2:
            add = max(add, rise * t * smoothstep(3.2, 2.0, dd))
    return add


def pool_add(x, z):
    add = 0.0
    for (cx, cz), rx, rz, depth, _ in POOLS:
        q = math.hypot((x - cx) / rx, (z - cz) / rz)
        if q < 1.0:
            add = min(add, -depth * smoothstep(1.0, 0.3, q))
    return add


def pocket_add(x, z):
    r = math.hypot(x - POCKET[0], z - POCKET[1])
    return 1.2 * smoothstep(13.0, 8.0, r)


def floor_detail(x, z):
    return 0.45 * (fbm(x / 6.0, z / 6.0, 91, 3) - 0.5) * 2.0 + 0.9 * (fbm(x / 23.0, z / 23.0, 92, 2) - 0.5) * 2.0


def floor_y(x, z):
    """The walkable floor (no walls): the blended space floor, relief, ledges, pools, the pocket."""
    d, f, c, own = field(x, z)
    return f + floor_detail(x, z) + ledge_add(x, z) + pool_add(x, z) + pocket_add(x, z)


def height(x, z):
    d, f, c, own = field(x, z)
    if d > 14.0:
        return PH
    fl = f + floor_detail(x, z) + ledge_add(x, z) + pool_add(x, z) + pocket_add(x, z)
    talus = 1.1 * smoothstep(-5.0, 0.0, d)                     # scree banks up against the walls
    if d <= 0.0:
        return fl + talus
    ww = 3.0 + 3.5 * vnoise(x / 11.0, z / 11.0, 93)
    return fl + talus + (PH - fl - talus) * smoothstep(0.0, ww, d)


# ---------------------------------------------------------------- lids: the roof
LID = {"l": (42.0, 16.0, 40.0, 3), "m": (28.0, 11.0, 27.0, 3), "s": (16.0, 7.0, 15.0, 2)}   # size x, y, z; variants


def lid_under(lids, x, z):
    """The roof's underside here: the lowest lid surface above (x, z) (ellipsoid approximation of the hulls)."""
    y = None
    for (lx, ly, lz, kind, _) in lids:
        sx, sy, sz, _n = LID[kind]
        a, b = (sx + sz) / 4.0, sy / 2.0
        q = ((x - lx) ** 2 + (z - lz) ** 2) / (a * a)
        if q >= 1.0:
            continue
        u = ly - 0.56 * b - b * math.sqrt(1.0 - q)
        y = u if y is None else min(y, u)
    return y


def build(ctx):
    R = rng(SEED)
    frame = Frame(W, D)
    grid = Grid(W, D, margin=MARGIN, base=PH)
    grid.apply(lambda x, z, h: height(x, z))
    grid.quantise()
    b = MapBuilder(ctx.catalog, grid, frame, seed=SEED)
    paint = GroundPaint()
    rep = {}
    cat = ctx.catalog

    # ---- the cave field on a 1 m raster (painting and dressing look it up)
    N = int(W) + 1
    F = {}
    for j in range(N):
        z = -W / 2 + j
        for i in range(N):
            x = -W / 2 + i
            F[(i, j)] = field(x, z)

    def fd(x, z):
        i, j = int(round(x + W / 2)), int(round(z + W / 2))
        return F.get((min(max(i, 0), N - 1), min(max(j, 0), N - 1)))

    def inside(x, z, m=0.0):
        return fd(x, z)[0] < -m

    # ---- the roof: overlapping lids on a jittered 13 m lattice over every point of the cave
    lids = []
    step = 13.0
    nl = int(W / step) + 2
    for j in range(-nl // 2 - 1, nl // 2 + 2):
        for i in range(-nl // 2 - 1, nl // 2 + 2):
            x, z = i * step + R.uniform(-3.5, 3.5), j * step + R.uniform(-3.5, 3.5)
            if abs(x) > W / 2 + 8 or abs(z) > D / 2 + 8:
                continue
            d, f, c, own = field(x, z)
            if d > 7.0:
                continue
            kind = "l" if c >= 20 else "m" if c >= 9.5 else "m" if R.random() < 0.6 else "s"
            sx, sy, sz, nvar = LID[kind]
            dome = 0.78 * sy
            ytop = f + c + R.uniform(0.35, 0.7) * dome
            lids.append((x, ytop, z, kind, R.randrange(nvar)))
    for (x, y, z, kind, v) in lids:
        b.place("cave_lid_%s%d" % (kind, v), x, z, R.uniform(0, 360), check=False, cls="none", y=y, tags={"roof"})
    rep["lids"] = len(lids)

    # ---- ground paint: rock floor everywhere, gravel / sediment / mud by noise and water, pebbles in the channels
    TX = 0.5
    for j in range(int(-W / 2 / TX), int(W / 2 / TX)):
        z = (j + 0.5) * TX
        for i in range(int(-W / 2 / TX), int(W / 2 / TX)):
            x = (i + 0.5) * TX
            d, f, c, own = fd(x, z)
            if d > 1.5:
                continue
            wet = 0.0
            for (cx, cz), rx, rz, depth, _ in POOLS:
                q = math.hypot((x - cx) / (rx + 5), (z - cz) / (rz + 5))
                wet = max(wet, smoothstep(1.0, 0.55, q))
            n1, n2 = fbm(x / 14.0, z / 14.0, 101, 3), fbm(x / 5.0, z / 5.0, 102, 2)
            if wet > 0.4:
                t = "mud"
            elif n1 > 0.58:
                t = "gravel"
            elif n1 < 0.38:
                t = "dirt"
            elif n2 > 0.64:
                t = "pebbles"
            else:
                t = "rock"
            paint.dab(i, j, ["grass", "marsh", "dirt", "mud", "gravel", "sand", "cobbles", "rock", "dry_grass", "ploughed", "leaf_litter", "scorched", "pebbles"].index(t), 1.0)
            if t != "rock" and wet <= 0.4:   # the rock pavement shows through the loose cover at its edges
                paint.dab(i, j, 7, 0.35 * (1.0 - abs(n1 - 0.48) * 4.0) if abs(n1 - 0.48) < 0.25 else 0.0)

    # ---- trickles: narrow, fully wet, winding runs from a wall toward each pool (a glossy thread of water on the floor)
    for (cx, cz), rx, rz, depth, below in POOLS:
        for k in range(2):
            a = R.uniform(0, 6.283)
            pts, x, z = [], cx + math.cos(a) * rx * 0.8, cz + math.sin(a) * rz * 0.8
            for s in range(14):
                pts.append((x, z))
                a += R.uniform(-0.45, 0.45)
                nx, nz = x + math.cos(a) * 2.5, z + math.sin(a) * 2.5
                if not inside(nx, nz, 1.5):
                    break
                x, z = nx, nz
            if len(pts) > 3:
                b.route("lane", pts, 1.1, wet=1.0, strength=0.9, keepout=False)

    # ---- the wet heart: fully wet spokes round the pocket, so the floor before the spire is broken mirror (the reference's glossy disc)
    for k in range(10):
        a = k / 10 * 6.283 + R.uniform(-0.15, 0.15)
        b.route("footpath", [(POCKET[0] + math.sin(a) * 9, POCKET[1] + math.cos(a) * 9), (POCKET[0] + math.sin(a) * 34, POCKET[1] + math.cos(a) * 34)], 9.0, wet=1.0, strength=1.0, keepout=False)

    # ---- standing water on the floors: wet "routes" (surface mask G: darker damp ground, mirror puddles in the lows)
    for t in TUN:
        b.route("footpath", t.pts[::3], t.width * 0.9, wet=0.75, strength=0.6, keepout=False)
    for s in [C] + OUT:
        for a in (0.0, 1.1, 2.3):
            ux, uz = math.cos(s.rot + a), math.sin(s.rot + a)
            r = min(s.rx, s.rz) * 0.85
            b.route("footpath", [(s.c[0] - ux * r, s.c[1] - uz * r), (s.c[0] + ux * r, s.c[1] + uz * r)], min(s.rx, s.rz) * 0.9, wet=0.7, strength=0.55, keepout=False)

    # ---- dressing helpers
    def place(aid, x, z, yaw=None, y=None, tags=("cover",), **kw):
        if aid not in cat:
            rep.setdefault("missing", set()).add(aid)
            return None
        kw2 = dict(kw) if y is None else dict(kw, y=y)   # MAP1: ground pieces are seated by the builder on the final grid (no float, no sink)
        return b.place(aid, x, z, R.uniform(0, 360) if yaw is None else yaw, check=False, cls="none", tags=set(tags), **kw2)

    def roof(x, z):
        u = lid_under(lids, x, z)
        return u

    def clear_spot(x, z, r):   # keep paths: not on a tunnel's centreline, not on the monument's south axis (the entry view and approach)
        if abs(x - POCKET[0]) < 11.0 + r and -70.0 < z - POCKET[1] < -12.0:
            return False
        for t in TUN:
            if t.d(x, z) < -t.width * 0.5 + r and polyline_dist((x, z), t.pts)[0] < r + 1.2:
                return False
        return True

    # ---- the monument (Team A's pocket), framed like the reference from the south entry: the spire on a dais, the tallest
    # organ wall behind it rising into the dark, flanking organ walls left and right, warm niches between them, steps
    # down to a round basin in front. Low ribbed walls close the front corners; exits east, west and south (baffled).
    px, pz = POCKET
    place("cave_monument", px, pz + 1.0, yaw=0.0)                                         # the carved figure, facing the south entry
    place("cave_organ_3", px, pz + 21.0, yaw=look_yaw(0.0, -1.0))                       # the backdrop: 44 m of ribs behind the spire
    for sx in (-1.0, 1.0):
        x, z = px + sx * 19.0, pz + 7.0
        place("cave_organ_2", x, z, yaw=look_yaw(px - x, pz - 4.0 - z))                    # the flanks, turned in toward the spire
        nx, nz = px + sx * 11.0, pz + 9.0                                                  # warm niches between flank and backdrop
        place("cave_glow_ember", nx, nz, tags=("light",))
    for k, a in enumerate((135.0, 225.0)):                                                  # low ribbed walls at the front corners
        ar = math.radians(a)
        x, z = px + math.sin(ar) * 15.0, pz + math.cos(ar) * 15.0
        place("cave_ribwall_0", x, z, yaw=look_yaw(px - x, pz - z))
    for a in (90.0, 270.0):                                                                 # baffles outside the east and west exits
        ar = math.radians(a + 10.0)
        x, z = px + math.sin(ar) * 25.0, pz + math.cos(ar) * 25.0
        place("cave_drip_up_%d" % (4 if a < 180 else 5), x, z, yaw=look_yaw(px - x, pz - z) + 90.0)
    for sx in (-1.0, 1.0):                                                                  # the south approach: two stalagmite stands either side of the basin
        place("cave_drip_up_%d" % (11 if sx < 0 else 12), px + sx * 13.0, pz - 21.0)
    place("cave_fire_altar", px, pz - 5.2, yaw=0.0, tags=("light",))                        # the small warm glow at the centre
    for x, z in ((px - 9.0, pz + 15.5), (px, pz + 16.5), (px + 9.0, pz + 15.5), (px - 15.5, pz + 4.0), (px + 15.5, pz + 4.0)):
        place("cave_light_uplight", x, z, y=floor_y(x, z) + 0.3, tags=("light",))           # rim light carried up the organ walls, fading at the top
    place("cave_steps", px, pz - 9.5, yaw=look_yaw(0.0, 1.0), y=floor_y(px, pz - 9.5) - 0.25)   # the dais steps, down toward the basin
    for a in (60.0, 300.0):
        ar = math.radians(a)
        place("cave_glow_ember", px + math.sin(ar) * 3.6, pz + math.cos(ar) * 3.6, tags=("light",))

    def column_at(x, z):   # a full column from floor to roof: the shortest one that reaches, its foot sunk into the floor
        f, u = floor_y(x, z), roof(x, z)
        if u is None or not clear_spot(x, z, 6.0):
            return False
        for kind, H in (("m", 12), ("l", 20), ("xl", 30), ("h45", 45), ("h60", 60), ("h75", 75)):
            if H >= u - f + 1.0:
                place("cave_column_" + kind, x, z, y=u + 1.0 - H)
                return True
        return False

    # ---- columns: pillars holding the vault (giants in the centre), organ walls on the cavern rims, spine arches
    n_col = sum(column_at(x, z) for x, z in ((-30.0, -16.0), (26.0, 20.0), (-8.0, 38.0), (40.0, -4.0), (-46.0, 22.0), (14.0, -42.0)))
    for o in OUT:
        n_col += column_at(o.c[0] + R.uniform(-8, 8), o.c[1] + R.uniform(-8, 8))
    rep["columns"] = n_col
    rim = []
    for s_ in [C] + OUT:   # organ walls against each cavern's wall, facing in, never in a mouth
        if s_ is not C and min(s_.rx, s_.rz) < 25.0:
            continue   # a narrow cavern: an organ wall's arc could close it off
        n = 9 if s_ is C else 3
        for k in range(n):
            a = k / n * 2 * math.pi + R.uniform(0, 0.5)
            x = z = None
            for r in range(8, 90):
                qx, qz = s_.c[0] + math.sin(a) * r, s_.c[1] + math.cos(a) * r
                if fd(qx, qz)[0] > -5.0:
                    x, z = qx, qz
                    break
            if x is None or fd(x, z)[3] is not s_ or any(t.d(x, z) < 6.0 for t in TUN):
                continue
            rim.append((x, z))
            place("cave_organ_%d" % (R.randrange(2, 4) if s_ is C else 0), x, z, yaw=look_yaw(s_.c[0] - x, s_.c[1] - z))
    rep["organWalls"] = len(rim)
    place("cave_spine_arch", -64.0, 6.0, yaw=95.0)
    place("cave_spine_arch", 66.0, -8.0, yaw=88.0)

    # ---- the roof dressed: dense stalactite fields of every size (giants where the roof is high, curtains and needles
    # elsewhere), denser round the cracks; stalagmites under them (some pairs grow into columns), boulder fields and
    # shelves for cover in the open floors
    CRACKS = [(0.0, -4.0, "main"), (NE.c[0] - 4, NE.c[1] + 2, "ns"), (-36.0, 14.0, "ns"), (30.0, 22.0, "ns"), (NG.c[0] + 6, NG.c[1], "ns"), (SL.c[0] + 8, SL.c[1] - 4, "ns"),
              (EV.c[0], EV.c[1] + 8, "ns"), (WH.c[0] + 2, WH.c[1] + 30, "ns"), (SC.c[0] - 12, SC.c[1], "ns")]
    n_st = n_sg = n_cov = 0
    tries = 0
    while tries < 9000:
        tries += 1
        x, z = R.uniform(-W / 2, W / 2), R.uniform(-D / 2, D / 2)
        v = fd(x, z)
        if v is None or v[0] > -3.0:
            continue
        d, f, c, own = v
        near_crack = min(math.hypot(x - cx, z - cz) for cx, cz, _ in CRACKS)
        u = roof(x, z)
        if u is None:
            continue
        hgt = u - f
        if n_st < 650 and R.random() < (0.5 if near_crack < 22 else 0.2) and hgt > 7.0:
            # size class by roof height: giants (21-25 m) only under the high vaults, never lower than 5 m above the floor
            vmax = max(i for i, h in enumerate((1.5, 3, 5, 8, 12, 17, 21, 25)) if h < hgt - 5.0 or i == 0)
            vv = min(vmax, int(vmax * (R.random() ** 0.8) + 0.5))
            place("cave_drip_down_%d" % (vv + 8 * R.randrange(4)), x, z, y=u + 1.5, tags=("deco",))
            n_st += 1
        elif R.random() < 0.10 and clear_spot(x, z, 3.0) and math.hypot(x - px, z - pz) > 28:
            vv = min(int(R.random() ** 1.6 * 8), 7)
            place("cave_drip_up_%d" % (vv + 8 * R.randrange(3)), x, z)
            n_sg += 1
        elif R.random() < 0.04 and clear_spot(x, z, 2.5) and math.hypot(x - px, z - pz) > 26:
            place(R.choice(["nat_boulder_limestone_l", "nat_boulder_limestone_m", "nat_boulder_schist_slab", "nat_boulders_granite_group", "cave_shelf_0", "cave_shelf_1", "cave_shelf_2"]), x, z)
            n_cov += 1
        if n_st >= 650 and n_sg > 240:
            break
    rep.update({"stalactites": n_st, "stalagmites": n_sg, "cover": n_cov})
    for s_ in [C] + OUT:   # boulder ridges across the big floors: cover that breaks the long sightlines
        for k in range(4 if s_ is C else 2):
            a = R.uniform(0, 6.283)
            r0 = R.uniform(0.3, 0.6) * min(s_.rx, s_.rz)
            x0, z0 = s_.c[0] + math.sin(a) * r0, s_.c[1] + math.cos(a) * r0
            t = a + math.pi / 2 + R.uniform(-0.4, 0.4)
            for j in range(R.randint(4, 7)):
                x, z = x0 + math.sin(t) * j * 3.2 + R.uniform(-0.8, 0.8), z0 + math.cos(t) * j * 3.2 + R.uniform(-0.8, 0.8)
                if inside(x, z, 3.0) and clear_spot(x, z, 2.0) and math.hypot(x - px, z - pz) > 26:
                    place(R.choice(["nat_boulder_limestone_l", "nat_boulder_schist_slab", "cave_shelf_1", "nat_boulders_granite_group", "cave_drip_up_%d" % R.randrange(0, 3)]), x, z)
                    n_cov += 1
    rep["cover"] = n_cov

    # ---- water: the pools' surfaces (their outlines where the bed drops below the water)
    water = []
    for k, ((cx, cz), rx, rz, depth, below) in enumerate(POOLS):
        fbase = floor_y(cx + rx * 1.3, cz) - pool_add(cx + rx * 1.3, cz)
        lvl = None
        # the water line: where the bed's dip reaches `below` under the surrounding floor (q solved on the profile)
        q = 1.0
        while q > 0.3 and depth * smoothstep(1.0, 0.3, q) < below:
            q -= 0.01
        outline = [[round(cx + math.cos(a) * rx * q, 3), round(cz + math.sin(a) * rz * q, 3)] for a in [2 * math.pi * i / 40 for i in range(40)]]
        lvl = round(sum(floor_y(p[0], p[1]) - pool_add(p[0], p[1]) for p in outline) / len(outline) - below, 3)
        water.append({"id": k + 1, "kind": "pond" if depth > 2.0 else "puddle", "level": lvl, "seed": [cx, cz], "outline": outline})
    rep["pools"] = [(w["level"], len(w["outline"])) for w in water]

    # ---- light: the great shaft over the reflecting pool, rare cold cracks, high rim lights grazing the far walls, faint
    # cold glows over water, a few warm accents; greenery where the light falls
    n_crack = 0
    for x, z, kind in CRACKS:
        u = roof(x, z)
        if u is None:
            continue
        place("cave_light_shaft_main" if kind == "main" else "cave_light_crack_ns", x, z, y=u - 0.3, tags=("light",))
        n_crack += 1
        for k in range(16):   # moss, ferns where the light falls
            a, r = R.uniform(0, 6.28), R.uniform(1.0, 8.0)
            gx, gz = x + math.sin(a) * r, z + math.cos(a) * r
            if inside(gx, gz, 1.0) and clear_spot(gx, gz, 1.0) and math.hypot(gx - px, gz - pz) > 9:
                place(R.choice(["nat_fern_clump", "nat_fern_clump", "mod_ivy_patch_ground", "nat_green_tufts"]), gx, gz, tags=("veg",))
        for k in range(3):   # hanging roots from the crack
            place("mod_vines_hanging_long", x + R.uniform(-3, 3), z + R.uniform(-3, 3), y=u - 0.5, tags=("veg",))
    rep["cracks"] = n_crack
    for s_ in [C] + OUT:   # rim lights: high, cold, near the walls, so the organ walls and far formations separate from black
        n = 2 if s_ is C else 1   # rim light on the nearest formations only: each chamber's focus is its crack
        for k in range(n):
            a = k / n * 6.283 + R.uniform(0, 0.6)
            x, z = s_.c[0] + math.sin(a) * s_.rx * 0.7, s_.c[1] + math.cos(a) * s_.rz * 0.7
            u = roof(x, z)
            if u is not None and inside(x, z, 4.0):
                place("cave_light_rim", x, z, y=floor_y(x, z) + (u - floor_y(x, z)) * 0.55, tags=("light",))
    for (cx, cz), rx, rz, depth, below in POOLS:
        place("cave_light_cold", cx, cz, y=floor_y(cx, cz) + 3.5 + depth, tags=("light",))
        for k in range(10):
            a = R.uniform(0, 6.28)
            gx, gz = cx + math.sin(a) * (rx + R.uniform(0.5, 3.0)), cz + math.cos(a) * (rz + R.uniform(0.5, 3.0))
            if inside(gx, gz, 1.0) and math.hypot(gx - px, gz - pz) > 9:
                place(R.choice(["nat_fern_clump", "mod_ivy_patch_ground", "nat_green_tufts"]), gx, gz, tags=("veg",))
    for x, z in ((WH.c[0] + 6, WH.c[1] - 10), (SC.c[0] - 10, SC.c[1] + 2), (NE.c[0], NE.c[1] - 6), (EV.c[0] - 6, EV.c[1] - 14), (SL.c[0] + 20, SL.c[1] + 16)):
        place("cave_glow_ember", x, z, tags=("light",))
    for t in TUN[10:]:   # the narrow flanks: a very faint cold fill every ~30 m (the headlamp does the rest)
        for s2 in range(15, int(t.L) - 8, 30):
            p = t.pts[min(len(t.pts) - 1, int(s2 / 2.0))]
            place("cave_light_cold", p[0], p[1], y=floor_y(*p) + 4.0, tags=("light",))

    # ---- scenario: Team Deathmatch (Team A in the pocket, Team B round the outside), an Assault objective for the validator
    sc = ScenarioBuilder(b, defending="Axis")
    sc.rules.update({"mode": "tdm", "teamFaction": ["Axis", "Allies"], "teamPlayers": [8, 8], "soloPlayers": 12, "royalePlayers": 16, "lootModern": True})
    sc.objective("Central cavern", px, pz, radius=16.0, capture_time=45.0, attack_from=WH.c)
    sc.spawn("attacker", [0], *WH.c, label="Attackers", rack=None)
    sc.spawn("defender", [0], px + 4.0, pz, label="Defenders", rack=None)
    # CV1 fix: the old 5.5 / 8 m ring put points on the altar fire and inside the monument's plinth (a player spawned
    # in the flames: a white screen). Now the open ground east and west of the monument, clear of everything placed
    cand = [(px + math.sin(math.radians(a)) * r, pz + math.cos(math.radians(a)) * r) for r in (9.0, 11.5) for a in (55, 75, 95, 115, 245, 265, 285, 305)]
    def spawn_clear(x, z):
        for e in b.inst:
            if "light" in e.get("tags", ()) or e["asset"].startswith(("cave_monument", "cave_steps", "cave_fire", "cave_glow", "cave_rib", "cave_organ", "cave_drip_up", "cave_standing")):
                hx = min(4.0, b.cat[e["asset"]].half[0]) if e["asset"] in b.cat else 1.0
                if (e["pos"][0] - x) ** 2 + (e["pos"][2] - z) ** 2 < (hx + 2.0) ** 2:
                    return False
        return True
    a_pts = [q for q in cand if spawn_clear(*q)]
    for k, (x, z) in enumerate(a_pts):
        m = sc.spawn("defender", [0], x, z, face=(x * 2 - px, z * 2 - pz), faction="Axis", label="Team A %d" % (k + 1), rack=None)
        m["team"], m["phases"] = 0, []
        m["pos"][1] = round(floor_y(x, z), 3)
    def free_spot(x, z, r):   # no formation, column or cover within r (a spawn inside a column's foot has no way out)
        for e in b.inst:
            if e["asset"].startswith(("cave_column", "cave_drip_up", "cave_organ", "cave_rib", "cave_shelf", "nat_boulder", "cave_spine", "cave_standing")) and                     (e["pos"][0] - x) ** 2 + (e["pos"][2] - z) ** 2 < (r + min(4.0, b.cat[e["asset"]].half[0] if e["asset"] in b.cat else 2.0)) ** 2:
                return False
        return True

    b_pts = []
    for o in OUT:   # up to three points in each outer cavern, on its far side from the centre, on open floor
        got = 0
        for off in (-0.45, 0.45, 0.0, -0.9, 0.9, -1.3, 1.3):
            if got >= 3:
                break
            a = math.atan2(o.c[0], o.c[1]) + off
            for fr in (0.5, 0.4, 0.3, 0.6, 0.2):
                x, z = o.c[0] + math.sin(a) * min(o.rx, o.rz) * fr, o.c[1] + math.cos(a) * min(o.rx, o.rz) * fr
                if inside(x, z, 5.0) and ledge_add(x, z) < 0.2 and pool_add(x, z) > -0.05 and free_spot(x, z, 3.0) and all((x - q[0]) ** 2 + (z - q[1]) ** 2 > 64 for q in b_pts):
                    b_pts.append((x, z, o.c))
                    got += 1
                    break
    for t in TUN[5:]:   # and one in the middle of each ring mouth and flank
        p = t.pts[len(t.pts) // 2]
        if inside(p[0], p[1], 2.0) and pool_add(p[0], p[1]) > -0.05 and free_spot(p[0], p[1], 3.0):
            b_pts.append((p[0], p[1], (0.0, 0.0)))
    for k, (x, z, fc) in enumerate(b_pts):
        m = sc.spawn("attacker", [0], x, z, face=(px, pz), faction="Allies", label="Team B %d" % (k + 1), rack=None)
        m["team"], m["phases"] = 1, []
        m["pos"][1] = round(floor_y(x, z), 3)
    sj = sc.to_json()
    sj["districts"] = [{"id": k + 1, "name": s.name, "pos": [s.c[0], round(floor_y(*s.c), 2), s.c[1]], "radius": 24.0} for k, s in enumerate([C] + OUT)]
    rep["spawns"] = {"A": len(a_pts), "B": len(b_pts)}

    # ---- output
    tdir = ctx.templates
    grid.write_png(os.path.join(tdir, TID + ".height.png"))

    def eye(x, z):
        return [round(x, 2), round(floor_y(x, z) + EYE, 2), round(z, 2)]
    VIEWS = [("central_wide", (-52.0, -30.0), (20.0, 14.0), 7.0), ("pocket_eye", (0.0, -44.0), (0.0, 0.0), 9.0),
             ("look_up", (-20.0, -12.0), (-6.0, -2.0), 58.0), ("outer_cavern", (WH.c[0] + 6, WH.c[1] - 40), (WH.c[0], WH.c[1] + 30), 6.0),
             ("mouth_view", (14.0, 30.0), NG.c, 8.0), ("flooded_eye", (-48.0, -70.0), SL.c, 2.0), ("crack_shaft", (-14.0, -40.0), (2.0, -21.0), 18.0),
             ("headlamp_dark", TUN[10].pts[10], TUN[10].pts[20], 0.0)]
    views = [{"name": "overview_high", "pos": [0.0, 140.0, 150.0], "yaw": 0.0, "pitch": -45.0}]
    for name, (x, z), (tx, tz), pitch in VIEWS:
        views.append({"name": name, "pos": eye(x, z), "yaw": look_yaw(tx - x, tz - z), "pitch": pitch, "gameplay": True})
    cam = ([0.0, 120.0, 140.0], 0.0, -40.0)
    tv = views[1]
    thumb_cam = (tv["pos"], tv["yaw"], 8.0)
    write_map(os.path.join(tdir, TID + ".nmap.json"), name=NAME, template_id=TID, template_version=VERSION, seed=SEED, width=W, depth=D,
              builder=b, scenario_json=sj, explore_start=((px, floor_y(px, pz - 6.0), pz - 6.0), 180.0), camera=cam,
              bookmarks={str(k + 1): {"position": [s.c[0], floor_y(*s.c) + 12.0, s.c[1] + 20.0], "yaw": 0.0, "pitch": -25} for k, s in enumerate([C] + OUT)},
              extra={"groundPaint": paint.to_json(), "waterBodies": water,
                     "lighting": {"hour": 0.0, "fill": 0.02, "exposure": 0.85},
                     "look": {"fog": True, "haze": 0.15, "cave": True, "weather": 0.0, "envFog": 0, "saturation": 0.92, "tint": [0.94, 0.98, 1.06], "grime": 0.0,
                              "paintSets": {"rock": "ground_rubble_paint", "dirt": "ground_rubble_paint", "gravel": "gravel_mixed", "pebbles": "paint_pebbles"}},
                     "audio": {"bed": "cave.wav", "battlefield": False},
                     "royale": {"indoor": True}})
    write_template(os.path.join(tdir, TID + ".json"), tid=TID, name=NAME, order=8, version=VERSION, seed=SEED,
                   description="A vast natural cave on D-day's footprint (265 x 265 m): a central cavern some 115 m across under a "
                               "66 m vault, an ancient spire and Team A's walled pocket at its heart, six outer caverns of different "
                               "shapes opening into each other through wide mouths, shelves and ramps, a deep sunken lake and flooded "
                               "hollows, fields of stalactites from needles to 25 m giants, columns holding the roof, organ walls. Near darkness: rare cold light through cracks in the roof, a few warm embers, your "
                               "headlamp. Team Deathmatch (Team A at the centre, Team B round the outside), Solo and Battle Royale.",
                   width=W, depth=D, margin=MARGIN, heightmap=grid.meta(TID + ".height.png"), water_level=-1000.0,
                   map_file=TID + ".nmap.json", camera=cam, thumbnail=TID + ".thumb.png", thumb_camera=thumb_cam,
                   extra={"userMap": True, "views": views})
    use = asset_usage(b)
    doc = os.path.join(ctx.repo, "docs", "asset-usage-default-maps.md")
    update_usage_doc(doc, use, NAME, "- **Caverns:** *Caverns* (CV1, `tools/mapgen/maps/caverns.py`): placed counts; \"not used\" = not on this map.")
    cover_catalogue(doc, cat)
    rep.update({"instances": len(b.inst), "use": dict(sorted(use.items())), "missing": sorted(rep.get("missing", []))})
    write_summary(os.path.join(ctx.out, TID + ".summary.json"), rep)
    print("instances %d | lids %d | stalactites %d, stalagmites %d, cover %d | cracks %d | pools %s | spawns %s | missing %s" %
          (len(b.inst), rep["lids"], n_st, n_sg, n_cov, n_crack, rep["pools"], rep["spawns"], rep["missing"]))
