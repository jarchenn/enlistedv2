"""D-day (D1): a compact Normandy landing, 265 x 265 m (half the Chateau town's 350 x 400 m playable area).

Normalised design coordinates (the brief's): u runs west to east, v from the sea (0) to the village (1). The engine's +Z is
south, so the sea lies at +Z: world x = (u - 0.5) * W, z = (0.5 - v) * D. Three regions of about a third each:

  v 0.00-0.33  sea and beach: shallow water, wet and dry sand, a dune belt; landing craft, obstacle groups, wire
  v 0.33-0.67  bunker country: two staggered belts on rolling knolls and a saddle, mil_stronghold at (0.51, 0.56),
               one trench network (forward line, rear branches, three communication trenches, loops, spurs, exits)
  v 0.67-1.00  village: a level plateau, a crossroads square, a church, gardens, walls, trees; three Axis spawn houses

Out of bounds: open sea to the south, forest (decor) to the west, north and east, coastal scrub at the southern
corners. Assault A-C (forward belt, stronghold, village square), TDM and Solo.

    <python> -I tools/mapgen/build.py dday

Writes content/templates/dday.{json,height.png,nmap.json} and the "D-day" column of docs/asset-usage-default-maps.md.
The thumbnail comes from --render-map-thumbs."""
import math
import os

from mapgen import (GroundPaint, Grid, Frame, MapBuilder, ScenarioBuilder, Profile, stamp_valley, stamp_flatten_poly,
                    stamp_crater, stamp_bank, slots_from_structures, catmull_rom, write_map, write_template, write_summary,
                    asset_usage, rect, rng, point_at, polyline_length, polyline_dist, in_poly, asset_yaw_facing,
                    asset_yaw_along, look_yaw, local_to_world, fbm, vnoise, circle_poly, dist, smoothstep, resample)

TID, NAME, VERSION, SEED = "dday", "D-day", 1, 19440606
W = D = 265.0                     # sqrt(0.5 x 350 x 400) = 264.6 m
MARGIN = 110
HX = HZ = W / 2
WATER = 0.0
DECOR = {"beautify": True, "decor": True}
BUILDING_CATS = ("Houses", "Landmarks", "Large buildings", "Small buildings")


def U(u):
    return (u - 0.5) * W


def V(v):
    return (0.5 - v) * D


def uv(x, z):
    return x / W + 0.5, 0.5 - z / D


# ------------------------------------------------------------------------------------------------ the layout
STRONG = (3.8, -16.0)             # mil_stronghold (0.514, 0.560): front (+Z) to the sea; its rear passage ends on x = 0
BUNKERS = [                       # (id, x, z, yaw, role)
    ("bunker_mg_casemate", -92.0, 40.0, 45.0, "forward west MG"),          # (0.15, 0.35) south-east across the beach
    ("bunker_pak_casemate", 0.0, 44.0, 0.0, "forward central"),            # (0.50, 0.33) the central dry-beach approach
    ("bunker_mg_casemate", 82.7, 43.6, -45.0, "forward east MG"),          # (0.81, 0.34) south-west
    ("aw_r667_casemate", -54.6, -17.9, 0.0, "rear west"),                  # (0.29, 0.57) gun east across the valley gap
    ("bunker_large_casemate", 66.0, -32.0, -12.0, "rear east"),            # (0.75, 0.61) the eastern inland approach
]
TOBRUKS = [("aw_tobruk_mg", -38.0, 39.5, 12.0), ("aw_tobruk_mg", 40.0, 40.5, -14.0)]   # MG nests ahead of the forward line
FLAK = [(-104.0, -2.0, 160.0), (99.0, -22.0, 210.0)]                                 # west (0.11, 0.51), east (0.87, 0.58)
ALLIED = [(U(1 / 6), 106.0), (U(0.5), 106.0), (U(5 / 6), 106.0)]                     # v = 0.10, equal spacing
AXIS_HOUSES = [("vil_granite_longere", -53.0, -101.0, 0.0), ("vil_rectory", 0.0, -111.0, 0.0), ("vil_colombage_auge", 79.5, -101.0, 0.0)]
SQUARE = (0.0, -80.0)
VILLAGE = [   # (id, x, z, yaw): fronts on the main street (x = 0), the cross street (z ~ -80) and the lanes
    ("vil_church", -38.0, -63.0, 180.0), ("vil_cafe_corner", 15.0, -64.0, 180.0), ("vil_terrace_pair", -13.0, -58.0, 90.0),
    ("vil_bakery", -17.0, -97.0, 0.0), ("vil_merchant_house", 20.0, -98.0, 0.0), ("vil_townhouse_jettied", 43.0, -93.0, 0.0),
    ("vil_brick_townhouse", -22.0, -120.0, 0.0), ("vil_rendered_shutters", 44.0, -63.0, 180.0), ("house_stone_cottage", -84.0, -92.0, 25.0),
    ("vil_stone_barn", -100.0, -112.0, 90.0), ("small_storehouse", 58.0, -114.0, -20.0), ("vil_norman_farmhouse", 104.0, -82.0, 12.0),
    ("vil_cottage_barn", -100.0, -68.0, 90.0), ("small_stone_dairy", 108.0, -110.0, -90.0),
]


def shore_v(x):
    """v of the waterline: an organic, broken shoreline around 0.07."""
    return 0.07 + 0.018 * fbm(x / 55.0, 1.3, 7, 3) + 0.01 * math.sin(x / 23.0 + 1.1)


PROFILE = Profile([(-0.6, -7.0), (-0.25, -3.2), (0.0, -0.9), (0.07, 0.0), (0.13, 0.35), (0.25, 1.35), (0.30, 2.3), (0.335, 3.0),
                   (0.37, 4.8), (0.41, 6.3), (0.455, 5.5), (0.49, 6.0), (0.53, 8.8), (0.575, 11.0), (0.62, 11.8), (0.68, 11.6),
                   (0.80, 12.0), (1.0, 12.4), (1.45, 17.0)])


def base_height(x, z):
    u, v = uv(x, z)
    shift = (shore_v(x) - 0.07) * (1.0 - smoothstep(0.15, 0.3, v))
    h = PROFILE(v - shift)
    # beach: gentle shelves, shallow drainage runnels toward the sea, uneven dry sand
    if v < 0.3:
        k = smoothstep(0.03, 0.09, v) * (1.0 - smoothstep(0.24, 0.3, v))
        h += k * (0.12 * fbm(x / 14.0, z / 14.0, 3, 3) - 0.22 * max(0.0, math.sin(x / 9.0 + 2.0 * fbm(x / 40.0, z / 40.0, 4, 2))) ** 6)
    # dunes: low ridges 1-3 m over the beach with gaps between them
    dune = smoothstep(0.23, 0.27, v) * (1.0 - smoothstep(0.32, 0.36, v))
    if dune > 0:
        r = max(0.0, fbm(x / 21.0, z / 8.0, 11, 3) + 0.18)
        h += dune * (2.4 * r + 0.4 * fbm(x / 6.0, z / 6.0, 12, 2))
    # bunker country: knolls and hollows, broken ground in the east
    bc = smoothstep(0.33, 0.38, v) * (1.0 - smoothstep(0.63, 0.68, v))
    if bc > 0:
        east = smoothstep(0.55, 0.72, u)
        h += bc * (1.8 * fbm(x / 42.0, z / 42.0, 21, 3) + east * 0.7 * fbm(x / 9.0, z / 9.0, 22, 2))
    # village plateau: gentle undulation only
    vil = smoothstep(0.64, 0.72, v)
    h += vil * 0.45 * fbm(x / 70.0, z / 70.0, 31, 2)
    # beyond the east, west and north borders: wooded rolling ground rising a little (no square cutoff)
    out = max(0.0, abs(x) - HX) + max(0.0, -z - HZ)
    if out > 0 and v > 0.18:
        h += min(1.0, out / 40.0) * (2.5 + 2.0 * fbm(x / 35.0, z / 35.0, 41, 3)) * smoothstep(0.18, 0.32, v)
    return h


# routes inland (for keep-outs and the cover check): a western gully, an exposed central approach, eastern broken ground
GULLY = catmull_rom([(U(0.31), V(0.27)), (U(0.33), V(0.36)), (U(0.30), V(0.47)), (U(0.34), V(0.60)), (U(0.35), V(0.70))], 8)
LANES = {"west": [(U(1 / 6), 98), (U(0.24), V(0.3)), (U(0.32), V(0.4)), (U(0.30), V(0.5)), (U(0.33), V(0.66))],
         "centre": [(U(0.5), 98), (U(0.5), V(0.3)), (U(0.42), V(0.42)), (U(0.42), V(0.5)), (U(0.47), V(0.68))],
         "east": [(U(5 / 6), 98), (U(0.74), V(0.3)), (U(0.66), V(0.42)), (U(0.64), V(0.52)), (U(0.6), V(0.68))]}


# ------------------------------------------------------------------------------------------------ trench network
# Pieces join exactly at their sockets (the engine's GX1 networks union at 0.3 m). Each piece: [(local p, outward dir)].
SOCK = {
    "aw_trench_straight": [((-4, 0), (-1, 0)), ((4, 0), (1, 0))],
    "aw_trench_zigzag": [((-6, 0), (-1, 0)), ((6, 0), (1, 0))],
    "aw_trench_corner": [((-4, 0), (-1, 0)), ((0, 4), (0, 1))],
    "aw_trench_bend": [((-4, 0), (-1, 0)), ((4 * math.sqrt(0.5), 4 * math.sqrt(0.5)), (math.sqrt(0.5), math.sqrt(0.5)))],
    "aw_trench_entry": [((3, 0), (1, 0))],
    "aw_trench_tee": [((-4, 0), (-1, 0)), ((4, 0), (1, 0)), ((0, 4), (0, 1))],
    "aw_trench_cross": [((-4, 0), (-1, 0)), ((4, 0), (1, 0)), ((0, 4), (0, 1)), ((0, -4), (0, -1))],
}
DIRS = {"E": (1, 0), "W": (-1, 0), "S": (0, 1), "N": (0, -1)}   # world: +Z is south


def rot(yaw, v):
    return local_to_world((0.0, 0.0), yaw, v[0], v[1])


def close(a, b, e=0.05):
    return abs(a[0] - b[0]) < e and abs(a[1] - b[1]) < e


class Trenches:
    """Lays the network piece by piece from junction sockets, checking that every run lands on its target socket."""

    def __init__(self, b):
        self.b, self.inst, self.junctions, self.edges, self.ends = b, [], {}, [], []

    def put(self, aid, c, yaw, y=None):
        e = self.b.place(aid, c[0], c[1], yaw, check=False, cls="flat", tags={"trench"}, y=self.b.ground(*c) if y is None else y)
        self.inst.append(e)
        return e

    def junction(self, name, c, arms):
        """A tee (3 arms) or cross (4) at c with arms toward the named world directions; its sockets by direction."""
        aid = "aw_trench_cross" if len(arms) == 4 else "aw_trench_tee"
        want = sorted(DIRS[a] for a in arms)
        for yaw in (0.0, 90.0, 180.0, -90.0):
            got = sorted(tuple(round(v) for v in rot(yaw, d)) for _, d in SOCK[aid])
            if got == want:
                e = self.put(aid, c, yaw)
                self.junctions[name] = {a: ((c[0] + 4 * DIRS[a][0], c[1] + 4 * DIRS[a][1]), DIRS[a]) for a in arms}
                self.junctions[name]["_e"] = e
                return e
        raise ValueError("no yaw gives arms %s" % arms)

    def attach(self, aid, p, d, out=None, front=None):
        """Join piece aid at point p, where the trench arrives travelling along d; out: the direction to leave by
        (two-socket pieces). Returns (instance, exit point, exit dir)."""
        socks = SOCK[aid]
        best = None
        for k in range(8):
            yaw = k * 45.0
            for i, (pi, di) in enumerate(socks):
                if not close(rot(yaw, di), (-d[0], -d[1]), 0.02):
                    continue
                lp = rot(yaw, pi)
                c = (p[0] - lp[0], p[1] - lp[1])
                if len(socks) == 1:
                    return self.put(aid, c, yaw), None, None
                j = 1 - i
                do = rot(yaw, socks[j][1])
                if out is not None and not close(do, out, 0.02):
                    continue
                q = rot(yaw, socks[j][0])
                score = 0.0
                if front is not None:   # zig-zag bays and firesteps toward the enemy (local +Z)
                    fz = rot(yaw, (0, 1))
                    score = -(fz[0] * front[0] + fz[1] * front[1])
                if best is None or score < best[0]:
                    best = (score, c, yaw, (c[0] + q[0], c[1] + q[1]), do)
        if best is None:
            raise ValueError("%s cannot join at %s along %s (out %s)" % (aid, p, d, out))
        _, c, yaw, q, do = best
        return self.put(aid, c, yaw), q, do

    def run(self, start, moves, end=None, front=(0, 1)):
        """Lay moves from socket start (point, outward dir): "S" 8 m, "Z" 12 m zig-zag, ("C"|"B", out dir) a corner /
        45-degree bend, "E" an entry ramp (end of the run). end: the socket it must land on."""
        p, d = start
        for m in moves:
            if m == "S":
                _, p, d = self.attach("aw_trench_straight", p, d, out=d, front=front)
            elif m == "Z":
                _, p, d = self.attach("aw_trench_zigzag", p, d, out=d, front=front)
            elif m == "E" or (isinstance(m, tuple) and m[0] == "E"):
                e, _, _ = self.attach("aw_trench_entry", p, d)
                if isinstance(m, tuple):   # the ramp tops out at a given level (the stronghold's entrance floor)
                    e["pos"][1] = m[1]
                self.ends.append(p)
                return None
            else:
                kind, o = m
                _, p, d = self.attach("aw_trench_corner" if kind == "C" else "aw_trench_bend", p, d, out=o, front=front)
        if end is not None:
            assert close(p, end[0], 0.05) and close(d, (-end[1][0], -end[1][1]), 0.02), "trench run ends at %s %s, not %s" % (p, d, end)
        return p, d


def build_trenches(b, strong):
    T = Trenches(b)
    J = T.junctions
    # forward line (z = 28) and its three junctions; the rear branches and their junctions
    T.junction("TW", (-72, 28), "WEN")
    T.junction("TC", (0, 28), "WENS")
    T.junction("TE", (84, 28), "WNS")
    T.junction("FWJ", (-72, -4), "NSW")
    T.junction("RWJ", (-72, -44), "SEN")
    T.junction("RWS", (-56, -44), "WES")
    T.junction("W2", (-40, -44), "WES")
    T.junction("CJ", (0, 8), "WES")
    T.junction("SE", (44, -16), "WES")
    T.junction("REJ", (84, -16), "WENS")
    T.junction("REN", (84, -40), "SNW")
    sea = (0, 1)
    # forward line: zig-zag bays and straights toward the sea
    T.run(J["TW"]["E"], ["Z", "S", "Z", "S", "Z", "Z"], end=J["TC"]["W"], front=sea)
    T.run(J["TC"]["E"], ["Z", "S", "Z", "Z", "S", "Z", "Z"], end=J["TE"]["W"], front=sea)
    # communication trenches: west (u 0.23), central (u 0.50, round the stronghold's front), east (u 0.78)
    T.run(J["TW"]["N"], ["Z", "Z"], end=J["FWJ"]["S"], front=(1, 0))
    T.run(J["FWJ"]["N"], ["S", "Z", "Z"], end=J["RWJ"]["S"], front=(1, 0))
    T.run(J["TC"]["N"], ["Z"], end=J["CJ"]["S"], front=sea)
    T.run(J["CJ"]["E"], ["Z", "Z", "Z", ("C", DIRS["N"]), "S", "S"], end=J["SE"]["S"], front=sea)
    T.run(J["CJ"]["W"], ["Z", "S", "Z", ("C", DIRS["N"]), "Z", "Z", "Z", "S"], end=J["W2"]["S"], front=(-1, 0))
    T.run(J["TE"]["N"], ["Z", "Z", "Z"], end=J["REJ"]["S"], front=(-1, 0))
    # rear branches: west along z = -44 to the stronghold's sunken entrance, east along z = -16 from its steps
    T.run(J["RWJ"]["E"], ["S"], end=J["RWS"]["W"], front=sea)
    T.run(J["RWS"]["E"], ["S"], end=J["W2"]["W"], front=sea)
    T.run(J["W2"]["E"], ["Z", "S", "Z", ("C", DIRS["S"]), ("E", strong["pos"][1] + 0.1)], front=sea)   # up into the sunken entrance
    T.run(J["SE"]["E"], ["Z", "S", "Z"], end=J["REJ"]["W"], front=sea)
    T.run(J["REJ"]["N"], ["S", "S"], end=J["REN"]["S"], front=(-1, 0))
    # spurs to the bunkers, the flak positions, the stronghold and the rear exits toward the village (entry ramps)
    T.run(J["TW"]["W"], ["S", ("B", (-math.sqrt(0.5), math.sqrt(0.5))), "E"])           # forward west MG casemate (rear door)
    T.run(J["TC"]["S"], ["E"])                                                            # forward central casemate
    T.run(J["TE"]["S"], ["E"])                                                            # forward east MG casemate
    T.run(J["RWS"]["S"], ["S", "E"])                                                      # rear west R667
    T.run(J["REN"]["W"], ["E"])                                                           # rear east casemate
    T.run(J["FWJ"]["W"], ["S", "S", "E"])                                                 # west flak
    T.run(J["REJ"]["E"], ["E"])                                                           # east flak
    T.run(J["SE"]["W"], ["E"])                                                            # the stronghold's east steps
    T.run(J["RWJ"]["N"], ["E"])                                                           # rear exit west
    T.run(J["REN"]["N"], ["E"])                                                           # rear exit east
    return T



# ------------------------------------------------------------------------------------------------ helpers
def footprint_poly(cat, aid, x, z, yaw, pad=0.0, half=None, off=(0.0, 0.0)):
    hx, hz = half or cat[aid].half
    hx, hz = hx + pad, hz + pad
    return [local_to_world((x, z), yaw, off[0] + lx, off[1] + lz) for lx, lz in ((-hx, -hz), (hx, -hz), (hx, hz), (-hx, hz))]


def los_clear(b, p0, h0, p1, h1, step=0.5):
    """Terrain-only line of sight from (p0, eye h0) to (p1, h1), absolute heights; True when nothing blocks."""
    L = dist(p0, p1)
    n = max(2, int(L / step))
    for i in range(1, n):
        t = i / n
        x, z = p0[0] + (p1[0] - p0[0]) * t, p0[1] + (p1[1] - p0[1]) * t
        if b.ground(x, z) > h0 + (h1 - h0) * t:
            return False
    return True


def update_usage_doc(path, use):
    """The "D-day" column of docs/asset-usage-default-maps.md (placed counts, companions included)."""
    with open(path, encoding="utf-8") as f:
        lines = f.read().split("\n")
    has = any(l.startswith("| Asset |") and "D-day" in l for l in lines)
    out = []
    for l in lines:
        body = l.rstrip()
        cell = None
        if body.startswith("| Asset |"):
            cell = "D-day"
        elif body.startswith("|---|"):
            cell = "---"
        elif body.startswith("| `"):
            aid = body[3:body.index("`", 3)]
            cell = str(use[aid]) if aid in use else "not used"
        if cell is not None:
            if has:
                body = body[:-1].rstrip()
                body = body[:body.rfind("|") + 1]
            body = body + (cell + "|" if cell == "---" else " %s |" % cell)
        out.append(body if cell is not None else l)
    note = "- **D-day:** *D-day* (D1, `tools/mapgen/maps/dday.py`): placed counts, companions included; \"not used\" = not on this map."
    if not any(l.startswith("- **D-day:**") for l in out):
        k = max(i for i, l in enumerate(out) if l.startswith("- **Chateau town:**"))
        out.insert(k + 1, note)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out))


# ------------------------------------------------------------------------------------------------ build
def sculpt(ctx, grid, R):
    cat = ctx.catalog
    grid.apply(lambda x, z, h: base_height(x, z))
    # the western gully: a shallow draw from the dunes up between the knolls (only lowers)
    gl = [grid.sample(*point_at(GULLY, polyline_length(GULLY) * i / 100.0)[0]) for i in range(101)]
    stamp_valley(grid, GULLY, floor=lambda u: gl[min(100, int(round(u * 100)))] - 2.2 * math.sin(math.pi * min(1.0, u * 1.3)),
                 top_half=lambda u: 13.0, floor_half=lambda u: 3.0)
    # spawn cover: a curved sand bar 3.4 m high 7-8 m inland of each Allied landing, open at both ends (two ways out)
    for x, z in ALLIED:
        stamp_bank(grid, [(x - 15, z - 2.0), (x - 10, z - 6.5), (x - 3, z - 8.0), (x + 3, z - 8.0), (x + 10, z - 6.5), (x + 15, z - 2.0)], 4.0, 3.4)
    # bombardment craters: the dry beach and dunes, the bunker slopes (cover on the lanes inland)
    craters = []
    for n, z0, z1, r0, r1, d0, d1 in ((10, 52, 92, 2.4, 3.6, 0.6, 1.1), (16, -40, 50, 2.6, 4.4, 0.8, 1.5)):
        for _ in range(n):
            x, z = R.uniform(-HX + 12, HX - 12), R.uniform(z0, z1)
            if any(dist((x, z), (bx, bz)) < 16 for _, bx, bz, _, _ in BUNKERS) or dist((x, z), STRONG) < 40 or abs(x) < 6:
                continue
            stamp_crater(grid, x, z, R.uniform(r0, r1), R.uniform(d0, d1))
            craters.append((x, z))
    # pads: buildings and bunkers stand level on their footprint (+1.5 m); the stronghold on its walls' core only
    pads = [(aid, x, z, yaw) for aid, x, z, yaw, _ in BUNKERS] + [(aid, x, z, yaw) for aid, x, z, yaw in TOBRUKS]
    pads += [(aid, x, z, yaw) for aid, x, z, yaw in AXIS_HOUSES + VILLAGE]
    for aid, x, z, yaw in pads:
        poly = footprint_poly(cat, aid, x, z, yaw, pad=1.5)
        stamp_flatten_poly(grid, poly, height=grid.sample(x, z), falloff=5.0)
    ew = cat["mil_stronghold"].raw["earthwork"]["core"]
    core = footprint_poly(cat, "mil_stronghold", STRONG[0], STRONG[1], 0.0, pad=2.0, half=(ew[0], ew[1]), off=(ew[2], ew[3]))
    stamp_flatten_poly(grid, core, height=grid.sample(STRONG[0] + ew[2], STRONG[1] + ew[3]), falloff=7.0)
    for x, z, _ in FLAK:
        stamp_flatten_poly(grid, circle_poly(x, z, 5.5, 16), height=grid.sample(x, z), falloff=4.0)
    for x, z in ALLIED:   # the spawn pocket itself: firm, level wet sand
        stamp_flatten_poly(grid, circle_poly(x, z + 1.5, 3.2, 16), height=0.5, falloff=1.5)
    stamp_flatten_poly(grid, circle_poly(SQUARE[0], SQUARE[1], 16.0, 20), height=grid.sample(*SQUARE), falloff=10.0)
    grid.quantise()
    return craters


def build(ctx):
    R = rng(SEED)
    cat = ctx.catalog
    grid = Grid(W, D, margin=MARGIN)
    craters = sculpt(ctx, grid, R)
    b = MapBuilder(cat, grid, Frame(W, D), seed=SEED, water_level=WATER)
    for x, z in craters:
        b.cover_points.append((x, z, "crater"))
    paint = GroundPaint()
    b.paint = paint
    rep = {"size": [W, D], "area": W * D, "chateauArea": 350 * 400, "areaRatio": round(W * D / (350.0 * 400.0), 3)}

    # ------------------------------------------------------------------ 1. routes, keep-outs
    streets = {
        "main": catmull_rom([(0, -44), (1.5, -60), (0, -80), (-1, -100)], 6),
        "cross": catmull_rom([(-130, -86), (-70, -83), (0, -80), (60, -77), (130, -82)], 6),
        "west_lane": catmull_rom([(-72, -56), (-66, -68), (-60, -83)], 6),
        "east_lane": catmull_rom([(84, -52), (80, -66), (74, -78)], 6),
        "alley_w": catmull_rom([(-26, -82), (-30, -104), (-44, -116)], 6),
        "alley_e": catmull_rom([(30, -79), (34, -106), (62, -104), (80, -112)], 6),
    }
    b.route("gravel", streets["main"], 5.0)
    b.route("gravel", streets["cross"], 5.0)
    b.route("gravel", [(SQUARE[0] - 1, SQUARE[1]), (SQUARE[0] + 1, SQUARE[1])], 26.0)
    for k in ("west_lane", "east_lane"):
        b.route("lane", streets[k], 3.5)
    for k in ("alley_w", "alley_e"):
        b.route("footpath", streets[k], 2.4)
    # beach surfaces: the sea's wet margin, the wet sand (standing water), the dry beach and dune sand
    b.route("sand", [(-HX - 120, 125), (HX + 120, 125)], 40, wet=0.9, keepout=False)
    b.route("sand", [(-HX - 120, 106), (HX + 120, 106)], 22, wet=0.5, keepout=False)
    b.route("sand", [(-HX - 120, 78), (HX + 120, 78)], 36, keepout=False)
    b.route("sand", [(-HX - 120, 55), (HX + 120, 55)], 18, strength=0.55, keepout=False)
    for k, pts in LANES.items():   # worn routes inland: footpaths of the attack (sparse grass on them)
        b.route("footpath", catmull_rom(pts, 6)[1:], 1.8, strength=0.6, keepout=False)
    for x, z in ALLIED:
        b.keepout_poly(circle_poly(x, z, 7.0))
    for aid, x, z, _ in AXIS_HOUSES:
        b.keepout_poly(circle_poly(x, z + 9, 5.0))

    # ------------------------------------------------------------------ 2. the stronghold, the belts, flak, trenches
    ew = cat["mil_stronghold"].raw["earthwork"]["core"]
    sy = grid.sample(STRONG[0] + ew[2], STRONG[1] + ew[3])
    strong = b.place("mil_stronghold", STRONG[0], STRONG[1], 0.0, check=False, y=sy, tags={"stronghold", "bunker", "cover"})
    bunkers = []
    for aid, x, z, yaw, role in BUNKERS:
        bunkers.append(b.place(aid, x, z, yaw, check=False, tags={"bunker", "cover", role}))
    tobruks = [b.place(aid, x, z, yaw, check=False, tags={"tobruk", "cover"}) for aid, x, z, yaw in TOBRUKS]
    flak = []
    for x, z, yaw in FLAK:   # open emplacement: the gun in a sandbag ring with a gap toward its service trench
        flak.append(b.place("gun_flak18", x, z, yaw, check=False, tags={"flak"}))
        for k in range(7):
            a = math.radians(yaw + 60 + k * 40)
            b.place("def_sandbag_curved", x + 6.2 * math.sin(a), z + 6.2 * math.cos(a), math.degrees(a), check=False, tags={"flak", "cover"})
        for k in range(3):
            b.place(R.choice(["crate_mixed_supply", "furn_crate", "crate_pak40_he"]), x + R.uniform(-3, 3), z + R.uniform(-3, 3),
                    R.uniform(0, 360), tags={"flak"})
    T = build_trenches(b, strong)
    rep["trenchPieces"] = len(T.inst)
    rep["trenchJunctions"] = len(T.junctions)
    rep["bunkers"] = 1 + len(bunkers)
    rep["mgPositions"] = sum(1 for e in bunkers + tobruks + [strong] for f in cat[e["asset"]].firing_points if f.get("kind") == "mg")
    # wire and knife rests ahead of the forward line, with gaps on the three lanes
    b.chain("wire_concertina", [(-120, 50), (-60, 51), (0, 52), (60, 51), (120, 50)], along="z", gaps=[(30, 44), (112, 126), (176, 190)],
            jitter_yaw=5, rng=R, tags={"wire"}, check=False)
    for x in (-84, 0, 66):
        b.place("wire_knife_rest", x + R.uniform(-3, 3), 51 + R.uniform(-1, 1), 90 + R.uniform(-15, 15), tags={"wire"}, check=False)
    # trench junction landmarks: ammunition and sandbags at the main junctions
    for name in ("TC", "CJ", "SE", "REJ", "RWJ", "FWJ", "TW", "TE"):
        c = T.junctions[name]["_e"]
        for k in range(2):
            b.try_place(R.choice(["crate_pak40_he", "crate_mixed_supply", "drs_sandbag_wall"]), c["pos"][0] + R.uniform(-6, 6),
                        c["pos"][2] + R.uniform(-6, 6), R.uniform(0, 360), R, tags={"junction"})

    # ------------------------------------------------------------------ 3. the beach: craft, obstacles, the dunes
    for x in range(-int(HX) - 100, int(HX) + 101, 50):
        for z in (140, 190):
            b.place("env_sea_shallows", x, z, 0.0, check=False, y=WATER, cls="none", tags={"sea"})
    lcvps = []
    for x, z in ALLIED:   # each spawn's craft beached just behind it, ramp down toward the shore
        lcvps.append(b.place("bch_lcvp_beached", x + R.uniform(-2, 2), z + 11.5, 180 + R.uniform(-8, 8), check=False, water_ok=True, tags={"lcvp", "cover"}))
    for x, z, aid, yaw in ((-62, 121, "bch_lcvp_wrecked", 160), (36, 118, "bch_lcvp_wrecked", 205), (118, 116, "bch_lcvp_intact", 172),
                           (-118, 126, "bch_lcvp_intact", 190), (-20, 128, "bch_lcvp_intact", 168), (62, 98, "bch_lcvp_wrecked", 228)):
        lcvps.append(b.place(aid, x, z, yaw, check=False, water_ok=True, tags={"lcvp", "cover"}))
    # obstacle groups, staggered and imperfect: gates in the shallows, stakes and ramps on the wet sand, hedgehogs and
    # wooden X on the dry beach; open lanes up from each landing
    lane_x = [x for x, _ in ALLIED]
    groups = 0
    for z0, assets, n, w in ((114, ["bch_belgian_gate"], 9, 9.0), (100, ["bch_log_stake_mine", "bch_log_ramp_mined", "bch_log_stake_mine"], 16, 6.0),
                             (84, ["def_tank_trap", "def_tank_trap", "drs_wooden_x_obstacle"], 18, 5.0), (68, ["def_tank_trap", "drs_wooden_x_obstacle"], 12, 7.0)):
        for _ in range(n):
            cx, cz = R.uniform(-HX + 10, HX - 10), z0 + R.uniform(-5, 5)
            if any(abs(cx - lx) < w + 5 for lx in lane_x):
                continue
            k = R.randint(2, 4)
            for j in range(k):
                aid = R.choice(assets)
                yw = 180 + R.uniform(-15, 15) if aid == "bch_log_ramp_mined" else R.uniform(0, 360)
                b.place(aid, cx + j * R.uniform(2.6, 4.5) - k * 1.8, cz + R.uniform(-2.2, 2.2), yw, tags={"obstacle", "cover"}, water_ok=True)
            groups += 1
    rep["obstacleGroups"] = groups
    for x, z in ALLIED:   # hard cover on the open flanks of each landing pocket
        for sx in (-1, 1):
            b.try_place(R.choice(["bch_belgian_gate", "def_tank_trap"]), x + sx * 8.5, z - 2.5, R.uniform(0, 360), R, tags={"obstacle", "cover", "spawn_cover"}, water_ok=True)
    b.chain("wire_concertina", [(-128, 64), (-64, 62), (0, 63), (64, 61), (128, 63)], along="z", gaps=[(36, 50), (124, 140), (208, 224)],
            jitter_yaw=6, rng=R, tags={"wire"}, check=False)
    beach = lambda x, z: 46 < z < 98 and abs(x) < HX - 3
    dunes = lambda x, z: 44 < z < 66 and abs(x) < HX - 3
    FLATKW = dict(cls="flat", ok_with={"flat", "none", "veg", "wire"})
    b.scatter([("nat_drygrass_tall", 3), ("nat_drygrass_sparse", 3), ("nat_drygrass_tufts", 2), ("nat_gorse_shrub", 1)], dunes, 1 / 26.0, R,
              bounds=(-HX, 44, HX, 66), tags={"dune_grass"}, **FLATKW)
    b.scatter([("nat_drygrass_sparse", 2), ("nat_stones_patch", 1), ("rub_debris_patch", 1), ("furn_crate", 1), ("furn_barrel", 1)], beach, 1 / 240.0, R,
              bounds=(-HX, 46, HX, 98), tags={"beach_detail"}, ok_with={"flat", "none", "veg", "wire"})

    # ------------------------------------------------------------------ 4. bunker country: scrub, rock, sandbags
    land = lambda x, z: -46 < z < 44 and abs(x) < HX - 3
    b.scatter([("nat_brush_scrub", 3), ("nat_hawthorn_bush", 2), ("nat_gorse_shrub", 2), ("shrub_bramble", 1), ("nat_bracken_clump", 2),
               ("nat_blackthorn_low", 1)], land, 1 / 140.0, R, bounds=(-HX, -46, HX, 44), tags={"scrub"}, cls="veg", slope_max=24)
    b.scatter([("nat_drygrass_tall", 2), ("nat_tallgrass_patch", 2), ("nat_drygrass_tufts", 2), ("nat_green_tufts", 1)], land, 1 / 60.0, R,
              bounds=(-HX, -46, HX, 44), tags={"rough_grass"}, **FLATKW)
    b.scatter([("nat_outcrop_limestone", 1), ("nat_boulder_limestone_m", 2), ("nat_boulder_limestone_l", 1), ("nat_scree_patch", 1)],
              lambda x, z: land(x, z) and grid.slope_deg(x, z) > 9, 1 / 700.0, R, bounds=(-HX, -46, HX, 44), tags={"rock", "cover"})
    for e in bunkers + tobruks:   # sandbags and supply behind each position
        for k in range(2):
            p = local_to_world((e["pos"][0], e["pos"][2]), e["yaw"], R.uniform(-6, 6), -cat[e["asset"]].half[1] - R.uniform(1.5, 3.5))
            b.try_place(R.choice(["drs_sandbag_wall", "def_sandbag_curved", "crate_mixed_supply"]), *p, R.uniform(0, 360), R, tags={"bunker_dressing"})

    # ------------------------------------------------------------------ 5. the village
    houses = []
    for aid, x, z, yaw in AXIS_HOUSES:
        houses.append(b.place(aid, x, z, yaw, tags={"house", "spawn_house", "building", "cover"}))
    refused = [aid for aid, *_ in AXIS_HOUSES if not houses[[a for a, *_ in AXIS_HOUSES].index(aid)]]
    for aid, x, z, yaw in VILLAGE:
        e = b.place(aid, x, z, yaw, tags={"house", "building", "cover"})
        houses.append(e)
        if not e:
            refused.append(aid)
    rep["villageBuildings"] = sum(1 for e in houses if e)
    rep["villageRefused"] = refused
    houses = [e for e in houses if e]
    for e in houses:   # gardens: a wall or fence round the back, a fruit tree, a plot, firewood; flower beds in front
        a = cat[e["asset"]]
        hx, hz = a.half
        x, z, yaw = e["pos"][0], e["pos"][2], e["yaw"]
        L = lambda lx, lz: local_to_world((x, z), yaw, lx, lz)
        piece = "stone_wall_low" if R.random() < 0.6 else "drs_picket_fence"
        pl = cat[piece].fw
        for k in range(max(1, int((2 * hx + 2) // pl))):
            p = L(-hx - 1 + pl * (k + 0.5), -hz - 7.0)
            if not b.in_keepout(*p):
                b.place(piece, *p, yaw, tags={"yard"}, ok_with={"flat", "none", "veg"})
        for sx in (-1, 1):
            for k in range(int(6.5 // pl)):
                p = L(sx * (hx + 1), -hz - 0.4 - pl * (k + 0.5))
                if not b.in_keepout(*p):
                    b.place(piece, *p, yaw + 90.0, tags={"yard"}, ok_with={"flat", "none", "veg"})
        b.place(R.choice(["tree_apple", "tree_apple", "tree_ash"]), *L(R.choice((-1, 1)) * min(hx - 1.5, 3.5), -hz - 4.2), R.uniform(0, 360), tags={"yard"}, cls="veg")
        paint.poly(R.choice(["ploughed", "dirt"]), [L(0.8, -hz - 1.6), L(hx - 1.0, -hz - 1.6), L(hx - 1.0, -hz - 5.8), L(0.8, -hz - 5.8)], weight=0.9, edge=0.6)
        for aid in R.sample(["drs_woodpile", "drs_barrels", "furn_sacks", "drs_hay_bale_stack", "furn_bucket", "furn_crate"], 2):
            b.place(aid, *L(R.uniform(-hx + 1.5, hx - 1.5), -hz - R.uniform(0.9, 1.6)), yaw + R.uniform(-15, 15), tags={"yard"})
        if R.random() < 0.7:
            b.place("drs_flower_bed", *L(R.choice((-1, 1)) * (hx - 1.8), hz + 0.75), yaw, tags={"dressing"})
    # the square: well, lamps, benches, a cart and barrels round the edge
    b.place("drs_well", SQUARE[0] + 4, SQUARE[1] - 3, 0.0, tags={"square"})
    for k in range(4):
        a = 2 * math.pi * k / 4 + 0.6
        b.place("lamp_gas_street", SQUARE[0] + 9.5 * math.cos(a), SQUARE[1] + 9.5 * math.sin(a), 0.0, tags={"square"})
    for k in range(7):
        a = 2 * math.pi * k / 7 + 0.3
        b.try_place(["drs_cart", "drs_barrels", "furn_bench", "drs_hay_bale_stack", "drs_flower_bed", "furn_bench", "drs_barrels"][k], SQUARE[0] + 12.5 * math.cos(a),
                    SQUARE[1] + 12.5 * math.sin(a), math.degrees(-a), R, tags={"square"})
    for x, z, yaw in ((-6, -52, 10), (7, -52, -10), (-64, -62, 30), (78, -62, -25)):   # barricades at the village edge
        b.place("def_sandbag_straight", x, z, yaw, tags={"barricade", "cover"})
    town = lambda x, z: -132 < z < -46 and abs(x) < HX - 4
    b.scatter([("tree_oak", 3), ("tree_ash", 3), ("tree_apple", 2), ("tree_poplar", 1)], town, 1 / 240.0, R, bounds=(-HX, -HZ, HX, -46),
              tags={"village_green"}, min_dist=7.0)
    b.scatter([("shrub_hazel", 3), ("nat_hawthorn_bush", 2), ("shrub_bramble", 1), ("nat_fern_clump", 1), ("nat_nettle_clump", 1)], town, 1 / 160.0, R,
              bounds=(-HX, -HZ, HX, -46), tags={"village_green"}, cls="veg")
    b.scatter([("nat_meadow_mixed", 3), ("nat_flowers_cowparsley", 1), ("nat_tallgrass_verge", 2), ("nat_flowers_poppy", 1), ("nat_flowers_daisy", 1)], town,
              1 / 90.0, R, bounds=(-HX, -HZ, HX, -46), tags={"village_green"}, **FLATKW)

    # ------------------------------------------------------------------ 6. boundaries: forest W / N / E, sea S
    # in-bounds edge woods (a natural cue at the border), then a layered decor forest beyond it, thinning with distance;
    # toward the sea the forest gives way to scrub and dune grass
    edge = lambda x, z: (abs(x) > HX - 9 or z < -HZ + 9) and z < 40
    b.scatter([("tree_oak", 3), ("tree_ash", 3), ("tree_poplar", 1), ("shrub_hazel", 2), ("nat_hawthorn_bush", 1)], edge, 1 / 90.0, R,
              bounds=(-HX, -HZ, HX, 40), tags={"edge_wood"}, min_dist=4.0)
    nd = 0
    for _ in range(9000):
        x, z = R.uniform(-HX - 75, HX + 75), R.uniform(-HZ - 75, HZ + 40)
        if abs(x) < HX + 2 and z > -HZ - 2:
            continue
        d_out = max(abs(x) - HX, -z - HZ)
        if R.random() > 1.25 - d_out / 60.0:
            continue
        if R.random() < smoothstep(20, 70, z):   # toward the sea: scrub, then dune grass
            aid = R.choice(["nat_gorse_shrub", "nat_brush_scrub", "nat_drygrass_tall", "shrub_hazel"]) if z < 100 else "nat_drygrass_tall"
        else:
            aid = R.choice(["tree_oak", "tree_ash", "tree_oak", "tree_poplar", "tree_ash", "shrub_hazel"])
        if b.overlaps(cat[aid], x, z, 0.0, {"flat", "none", "veg"}):
            continue
        nd += b.place(aid, x, z, R.uniform(0, 360), check=False, extra=DECOR, tags={"decor"}, cls="veg") is not None
        if nd >= 900:
            break
    rep["decorForest"] = nd

    # ------------------------------------------------------------------ 7. smoke and fire at a few wrecks and impacts
    fires = 0
    for x, z, aid in ((-62, 117, "fx_wreck_fire"), (62, 94, "fx_wreck_fire"), (-104, 30, "fx_impact_smoke"), (52, 24, "fx_impact_smoke"),
                      (118, 70, "fx_impact_smoke")):
        if aid in cat:
            fires += b.place(aid, x, z, R.uniform(0, 360), check=False, tags={"fx"}) is not None
    rep["fires"] = fires

    # ------------------------------------------------------------------ 8. scenario: Assault A-C, TDM, Solo
    sc = ScenarioBuilder(b, defending="Axis")
    fwd = [bunkers[1]] + tobruks + [e for e in T.inst if dist((e["pos"][0], e["pos"][2]), (0, 28)) < 40]
    sc.objective("Forward belt", 0.0, 30.0, radius=16.0, capture_time=45.0, attack_from=(0, 106), linked=[e["id"] for e in fwd],
                 slots=slots_from_structures(b, fwd)[:16])
    sc.objective("The stronghold", STRONG[0], STRONG[1] - 1.0, radius=20.0, capture_time=55.0, attack_from=(STRONG[0], 30),
                 linked=[strong["id"]], slots=slots_from_structures(b, [strong]))
    sq = [e for e in houses if dist((e["pos"][0], e["pos"][2]), SQUARE) < 40]
    sc.objective("Village square", SQUARE[0], SQUARE[1], radius=14.0, capture_time=45.0, attack_from=(0, -40), linked=[e["id"] for e in sq],
                 slots=slots_from_structures(b, sq, kinds=("window",))[:16])
    for (x, z), lab in zip(ALLIED, ["Allies - west landing", "Allies - centre landing", "Allies - east landing"]):
        sc.spawn("attacker", [0, 1, 2], x, z, label=lab, face=(x, z - 50), rack="auto")
    for (aid, x, z, yaw), lab in zip(AXIS_HOUSES, ["Axis - west farmhouse", "Axis - rectory", "Axis - east house"]):
        e = next(h for h in houses if h["asset"] == aid)
        rooms = cat[aid].nav.get("rooms") or []
        r0 = min(rooms, key=lambda r: (r["min"][1], -(r["max"][0] - r["min"][0]) * (r["max"][2] - r["min"][2]))) if rooms else {"min": [-1, 0.3, -1], "max": [1, 3, 1]}
        lx, lz = (r0["min"][0] + r0["max"][0]) / 2, (r0["min"][2] + r0["max"][2]) / 2
        px, pz = local_to_world((x, z), e["yaw"], lx, lz)
        m = sc.spawn("defender", [0, 1, 2], px, pz, label=lab, face=(px, pz + 10), rack=None)
        m["pos"][1] = round(e["pos"][1] + r0["min"][1], 3)
        rk = b.place("rack_axis", *local_to_world((x, z), e["yaw"], lx + 1.2, lz), e["yaw"] + 90, check=False, cls="none", y=m["pos"][1], tags={"rack"})
        m["rack"] = rk["id"]
    for team, (x, z), fac, lab in ((0, ALLIED[1], "Allies", "Team A spawn"), (1, (0.0, -96.0), "Axis", "Team B spawn")):
        m = sc.spawn("attacker" if team == 0 else "defender", [0], x, z, face=(0.0, 0.0), faction=fac, label=lab, rack="auto")
        m["team"], m["phases"] = team, []
    crates = 0
    for m in sc.spawns:
        if m["role"] == "defender" and "team" not in m:
            continue   # indoors: the rack is there
        x, z, yw = m["pos"][0], m["pos"][2], math.radians(m["yaw"])
        crates += b.try_place("crate_mixed_supply", x + math.sin(yw) * 3.0, z + math.cos(yw) * 3.0, m["yaw"], R, tags={"supply"}, radius=3.0) is not None
    rep["crates"] = crates
    err, warn = sc.validate()

    # ------------------------------------------------------------------ 9. checks: spawn exposure to the fixed guns
    exposed = []
    for e in bunkers + tobruks + [strong]:
        for f in cat[e["asset"]].firing_points:
            wx, wz = local_to_world((e["pos"][0], e["pos"][2]), e["yaw"], f["p"][0], f["p"][2])
            Y = math.radians(e["yaw"] + f.get("yaw", 0.0))
            eye_h = e["pos"][1] + f["p"][1] + 1.62
            for sx, sz in ALLIED:
                dx, dz = sx - wx, sz - wz
                c = (dx * math.sin(Y) + dz * math.cos(Y)) / max(1e-6, math.hypot(dx, dz))
                if c < 0.5:   # outside the embrasure's +-60 degree arc
                    continue
                for ox in (-2.5, 0.0, 2.5):
                    if los_clear(b, (wx, wz), eye_h, (sx + ox, sz), b.ground(sx + ox, sz) + 1.7):
                        exposed.append((e["asset"], f.get("kind"), round(sx), ox))
    rep["spawnExposure"] = exposed
    rep["trenchEnds"] = [(round(p[0], 1), round(p[1], 1)) for p in T.ends]

    # ------------------------------------------------------------------ output
    tdir = ctx.templates
    grid.write_png(os.path.join(tdir, TID + ".height.png"))
    eye = lambda x, z, h=1.7: [round(x, 2), round(b.ground(x, z) + h, 2), round(z, 2)]
    cam = ([0.0, 120.0, 230.0], 0.0, -30.0)
    thumb_cam = ([-70.0, 85.0, 200.0], look_yaw(70.0, -200.0), -24.0)
    views = [
        {"name": "overview_high", "pos": [0.0, 175.0, 150.0], "yaw": 0.0, "pitch": -50.0},
        {"name": "landing_eye", "pos": eye(ALLIED[1][0] + 3.0, ALLIED[1][1] + 1.0), "yaw": look_yaw(-0.1, -1.0), "pitch": 3.0, "gameplay": True},
        {"name": "dune_eye", "pos": eye(U(0.3), 60.0), "yaw": look_yaw(0.25, -1.0), "pitch": 2.0, "gameplay": True},
        {"name": "forward_trench_eye", "pos": eye(-30.0, 28.0, 0.1), "yaw": look_yaw(1.0, 0.05), "pitch": 0.0, "gameplay": True},
        {"name": "stronghold_eye", "pos": eye(STRONG[0] + 30.0, 14.0), "yaw": look_yaw(-30.0, -28.0), "pitch": 4.0, "gameplay": True},
        {"name": "stronghold_over_beach", "pos": [STRONG[0], strong["pos"][1] + 7.0, STRONG[1] + 9.0], "yaw": look_yaw(0.0, 1.0), "pitch": -4.0, "gameplay": True},
        {"name": "village_eye", "pos": eye(SQUARE[0] + 6.0, SQUARE[1] + 16.0), "yaw": look_yaw(-0.2, -1.0), "pitch": 3.0, "gameplay": True},
        {"name": "beach_from_sea", "pos": [20.0, 14.0, 175.0], "yaw": look_yaw(-0.1, -1.0), "pitch": -6.0},
    ]
    a1 = sc.spawns[1]["pos"]
    write_map(os.path.join(tdir, TID + ".nmap.json"), name=NAME, template_id=TID, template_version=VERSION, seed=SEED, width=W, depth=D,
              builder=b, scenario_json=sc.to_json(), explore_start=((a1[0], a1[1], a1[2]), 0.0), camera=cam,
              bookmarks={str(i + 1): {"position": [o["pos"][0], o["pos"][1] + 40, o["pos"][2] + 70], "yaw": 0.0, "pitch": -28}
                         for i, o in enumerate(sc.objectives)},
              extra={"groundPaint": paint.to_json()})
    write_template(os.path.join(tdir, TID + ".json"), tid=TID, name=NAME, order=6, version=VERSION, seed=SEED,
                   description="A compact Normandy landing, 265 x 265 m: landing craft and obstacle belts on a broad beach, a dune "
                               "belt, then rolling bunker country with two staggered belts of casemates, tobruks and flak around an "
                               "earth-covered command stronghold, all joined by one trench network; behind it a walled French village "
                               "with gardens, a church and a crossroads square. Open sea to the south, forest to the west, north and "
                               "east. Assault (Allies land and take the forward belt, the stronghold and the square), Team Deathmatch "
                               "and Solo.",
                   width=W, depth=D, margin=MARGIN, heightmap=grid.meta(TID + ".height.png"), water_level=WATER,
                   map_file=TID + ".nmap.json", camera=cam, thumbnail=TID + ".thumb.png", thumb_camera=thumb_cam,
                   extra={"userMap": True, "views": views})
    use = asset_usage(b)
    update_usage_doc(os.path.join(ctx.repo, "docs", "asset-usage-default-maps.md"), use)
    rep.update({"instances": len(b.inst), "errors": err, "warnings": warn, "use": dict(sorted(use.items())),
                "rejects": {"%s/%s" % k: v for k, v in sorted(b.rejects.items())},
                "objectives": [(o["label"], o["displayName"], o["pos"], len(o["linkedStructures"]), len(o["defenseSlots"])) for o in sc.objectives],
                "spawns": [(m["label"], m["pos"]) for m in sc.spawns]})
    write_summary(os.path.join(ctx.out, TID + ".summary.json"), rep)
    print("instances %d | trench pieces %d | village %d (refused %s) | errors %s | warnings %d | exposed %d" %
          (len(b.inst), rep["trenchPieces"], rep["villageBuildings"], refused, err, len(warn), len(exposed)))
