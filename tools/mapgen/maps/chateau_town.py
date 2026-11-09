"""Chateau town (M1): a dense Norman town around B3's grand chateau, 350 x 400 m, populated edge to edge.

Layers from the centre out: the chateau on its gravel forecourt behind garden walls and gate piers; a ring road and
four winding streets lined with the B2 / B3 houses, the church in its walled graveyard, the town square, a stream
with the two stone arched bridges, sandbags and wooden X obstacles at the town approaches; then the green ring
(orchards, bocage hedgerows with gaps, sunken lanes, meadow, wheat, a few farms) out to the world border; beyond it a
decorative ring like Beautify's (houses facing inward, hedges and trees; extra.decor, outside the navmesh).
Assault A-D (forecourt, church, bridge, square), TDM team spawns at the south / north town edges, Solo as-is.

    <python> -I tools/mapgen/build.py chateau_town

Writes content/templates/chateau_town.{json,height.png,nmap.json} and the "Chateau town" column of
docs/asset-usage-default-maps.md. The thumbnail comes from --render-map-thumbs."""
import math
import os

from mapgen import (GroundPaint, Grid, Frame, MapBuilder, ScenarioBuilder, stamp_valley, stamp_flatten_poly, hedgerow,
                    sunken_lane, cut_gaps, slots_from_structures, catmull_rom, write_map, write_template, write_summary,
                    asset_usage, rect, rng, point_at, polyline_length, polyline_dist, offset_polyline, in_poly, poly_area,
                    asset_yaw_facing, asset_yaw_along, look_yaw, local_to_world, fbm, circle_poly, dist, resample, seg_dist)

TID, NAME, VERSION, SEED = "chateau_town", "Chateau town", 1, 1944
W, D, MARGIN = 350, 400, 100
HX, HZ = W / 2, D / 2

CH = (0.0, 18.0)                 # chateau centre (front to -z, onto the forecourt)
ENCL = (-34.0, -38.0, 34.0, 50.0)  # garden-wall enclosure x0, z0, x1, z1 (68 m: a 4 m wall slot centred on the gate)
CHURCH = (-95.0, 46.0)           # church centre (south door to -z, onto street S2)
YARD = (-124.0, 27.0, -66.0, 72.0)
SQUARE = (36.0, 138.0)
MILL = (-36.0, 118.0)

TOWN_HOUSES = [("vil_townhouse_jettied", 4), ("vil_terrace_pair", 4), ("vil_corner_shop", 3), ("house_stone_cottage", 3),
               ("house_farmhouse_2story", 2), ("large_village_inn", 1), ("vil_villa_deauville", 2), ("vil_manor_turret", 1),
               ("vil_gatehouse_chapel", 1), ("vil_longhouse_thatched", 2), ("small_carpenter_workshop", 1),
               ("small_roadside_office", 1), ("small_storehouse", 1), ("vil_rectory", 1), ("large_municipal_building", 1),
               ("large_village_schoolhouse", 1), ("small_stone_dairy", 1), ("large_stone_warehouse", 1)]
INFILL = ["small_storehouse", "small_stone_dairy", "vil_stone_barn", "small_carpenter_workshop", "small_storehouse",
          "small_roadside_office"]
FARM = ["vil_cottage_barn", "vil_norman_farmhouse", "vil_stone_barn", "small_stone_dairy"]
DECOR_HOUSES = ["house_farmhouse_2story", "house_stone_cottage", "vil_norman_farmhouse", "vil_stone_barn", "small_stone_dairy",
                "small_storehouse", "small_carpenter_workshop", "vil_longhouse_thatched", "vil_townhouse_jettied",
                "vil_villa_deauville", "vil_manor_turret", "vil_corner_shop", "vil_gatehouse_chapel", "vil_mill_house",
                "vil_terrace_pair", "vil_cottage_barn", "vil_rectory"]
BUILDING_CATS = ("Houses", "Landmarks", "Large buildings", "Small buildings")
DECOR = {"beautify": True, "decor": True}


def town(x, z, k=1.0):
    """The town core: an ellipse around the chateau (k scales it)."""
    return (x / (128.0 * k)) ** 2 + ((z - 8.0) / (150.0 * k)) ** 2 < 1.0


def base_height(x, z):
    rise = 2.4 * math.exp(-((x - CH[0]) ** 2 + (z - CH[1]) ** 2) / (2 * 55.0 ** 2))   # a slight rise for the chateau
    return 10.0 + rise + 0.9 * fbm(x / 90.0, z / 90.0, 5, 3) - 0.004 * z


def rounded_rect(x0, z0, x1, z1, r, n=4):
    pts = []
    for cx, cz, a0 in ((x1 - r, z0 + r, -90), (x1 - r, z1 - r, 0), (x0 + r, z1 - r, 90), (x0 + r, z0 + r, 180)):
        for k in range(n + 1):
            a = math.radians(a0 + 90.0 * k / n)
            pts.append((cx + r * math.cos(a), cz + r * math.sin(a)))
    return pts + [pts[0]]


def geometry():
    g = {}
    g["stream"] = catmull_rom([(-190, 103), (-120, 93), (-60, 101), (0, 96), (55, 86), (92, 64), (112, 30), (122, -12), (140, -60),
                               (165, -95), (190, -112)], 10)
    g["ring"] = rounded_rect(-45, -47, 45, 59, 12)
    g["S1"] = catmull_rom([(0, -47), (-8, -78), (-14, -108), (-6, -140), (6, -170), (4, -204)])
    g["S2"] = catmull_rom([(-45, 4), (-70, 9), (-95, 13), (-125, 4), (-150, -6), (-180, -10)])
    g["S3"] = catmull_rom([(0, 59), (4, 78), (6, 96), (14, 116), (34, 133), (50, 160), (42, 204)])
    g["S4"] = catmull_rom([(45, -4), (75, -16), (100, -20), (122, -14), (148, -4), (180, 2)])
    g["loop"] = catmull_rom([(-122, 22), (-112, -40), (-88, -82), (-45, -112), (0, -118), (50, -108), (88, -72), (100, -22)])
    g["mill_lane"] = catmull_rom([(45, 46), (68, 56), (86, 34), (90, 6), (94, -19)])
    g["forecourt"] = [(0, -41), (0, 2)]
    # the back lane: half-way between the ring road and the loop lane, so the band between the two house rows has a
    # lane with walls, hedges and outbuildings instead of open grass
    ring_pts = resample(g["ring"], 2.0)
    mids = []
    for p in resample(g["loop"], 9.0)[1:-1]:
        q = min(ring_pts, key=lambda r: (r[0] - p[0]) ** 2 + (r[1] - p[1]) ** 2)
        mids.append(((p[0] + q[0]) / 2, (p[1] + q[1]) / 2))
    g["back_lane"] = catmull_rom(mids, 4)
    return g


def crossing(street, stream):
    best = None
    for s in range(0, int(polyline_length(street))):
        p, t = point_at(street, float(s))
        d = polyline_dist(p, stream)[0]
        if best is None or d < best[0]:
            best = (d, float(s), p, t)
    return best


def s_where(pts, pred):
    """First arc length where pred(x, z) holds (or the length)."""
    L = polyline_length(pts)
    s = 0.0
    while s < L:
        if pred(*point_at(pts, s)[0]):
            return s
        s += 1.0
    return L


def gaps_where(line, pred, step=2.0):
    """Arc-length intervals of a line where pred(x, z) holds (hedge pieces are left out there)."""
    L, out, s, start = polyline_length(line), [], 0.0, None
    while s <= L:
        on = pred(*point_at(line, s)[0])
        if on and start is None:
            start = s
        if not on and start is not None:
            out.append((start - 2, s + 2))
            start = None
        s += step
    if start is not None:
        out.append((start - 2, L + 2))
    return out


def ring_fields(R, n=20):
    """The green ring as bocage fields: sectors between jittered radial hedges from the town edge to the border, the
    wide ones (corners) split again by a cross hedge. Returns (field polygons, hedge lines)."""
    c = (0.0, 8.0)
    inner = lambda a: (c[0] + 128.0 * 0.97 * math.cos(a), c[1] + 150.0 * 0.97 * math.sin(a))

    def outer(a):
        dx, dz = math.cos(a), math.sin(a)
        tx = (HX - 1.5) / abs(dx) if abs(dx) > 1e-6 else 1e9
        tz = ((HZ - 1.5) - c[1] * (1 if dz > 0 else -1)) / abs(dz) if abs(dz) > 1e-6 else 1e9
        t = min(tx, tz)
        return (c[0] + dx * t, c[1] + dz * t)

    a0 = R.uniform(0, 2 * math.pi / n)
    angs = [a0 + 2 * math.pi * k / n + R.uniform(-0.07, 0.07) for k in range(n)]
    fields, lines = [], []
    for k in range(n):
        a, b_ = angs[k], angs[(k + 1) % n] + (2 * math.pi if k == n - 1 else 0.0)
        A = [a + (b_ - a) * i / 8.0 for i in range(9)]
        ins, outs = [inner(x) for x in A], [outer(x) for x in A]
        p, q = ins[0], outs[0]
        if math.hypot(q[0] - p[0], q[1] - p[1]) > 12:   # the radial hedge, bent a little at its middle
            m = ((p[0] + q[0]) / 2 + R.uniform(-4, 4), (p[1] + q[1]) / 2 + R.uniform(-4, 4))
            lines.append([p, m, q])
        lines.append(ins)                               # the hedge along the town edge (refused where houses stand)
        lines.append(outs)                              # and along the border: every field is enclosed
        band = math.hypot(outs[4][0] - ins[4][0], outs[4][1] - ins[4][1])
        if band > 75:
            f = R.uniform(0.42, 0.58)
            mids = [(i_[0] + (o[0] - i_[0]) * f, i_[1] + (o[1] - i_[1]) * f) for i_, o in zip(ins, outs)]
            lines.append(mids)
            fields += [ins + mids[::-1], mids + outs[::-1]]
        elif band > 18:
            fields.append(ins + outs[::-1])
    return fields, lines


class Deck:
    """Weighted house deck: every type comes round, in a shuffled order."""

    def __init__(self, pool, R):
        self.pool, self.R, self.cards = pool, R, []

    def _fill(self):
        c = [a for a, n in self.pool for _ in range(n)]
        self.R.shuffle(c)
        self.cards += c

    def peek(self, k):
        while len(self.cards) <= k:
            self._fill()
        return self.cards[k]

    def take(self, k):
        self.peek(k)
        return self.cards.pop(k)


def populate(ctx, grid, pads=None):
    """Lay the whole map out on `grid`. Pass 1 (pads = list) only records the building footprints so the ground under
    them can be levelled; pass 2 lays out the same buildings on the levelled, quantised ground."""
    R = rng(SEED)
    cat = ctx.catalog
    b = MapBuilder(cat, grid, Frame(W, D), seed=SEED)
    g = geometry()
    stream = g["stream"]
    rep = {}
    yards = []   # (poly, bounds) behind each town house: later buildings keep out, the yard dressing goes in

    def in_yard(x, z):
        return any(bb[0] <= x <= bb[2] and bb[1] <= z <= bb[3] and in_poly((x, z), p) for p, bb in yards)

    def clear(aid, x, z, yaw, pad=0.6):
        a = cat[aid]
        hx, hz = a.half[0] + pad, a.half[1] + pad
        nx, nz = max(2, int(math.ceil(2 * hx / 3.0)) + 1), max(2, int(math.ceil(2 * hz / 3.0)) + 1)
        for i in range(nx):
            for j in range(nz):
                wx, wz = local_to_world((x, z), yaw, -hx + 2 * hx * i / (nx - 1), -hz + 2 * hz * j / (nz - 1))
                if not grid.in_build(wx, wz) or b.in_keepout(wx, wz) or in_yard(wx, wz):
                    return False
        return True

    def building(aid, x, z, yaw, tags=(), extra=None, check=True):
        e = b.place(aid, x, z, yaw, tags=set(tags) | {"building"}, extra=extra, check=check)
        if e is not None and pads is not None and cat[aid].category in BUILDING_CATS:
            pads.append((aid, x, z, e["yaw"]))
        return e

    # ------------------------------------------------------------------ 1. routes and keep-outs
    spawn_pts = {"attA": (22, -184), "defA": (-30, 74), "attB": (-42, -88), "defB": (-152, 128), "attC": (-52, -58),
                 "defC": (-22, 178), "attD": (-72, 132), "defD": (138, 168), "teamA": (-46, -186), "teamB": (78, 188)}
    for p in spawn_pts.values():
        b.keepout_poly(circle_poly(p[0], p[1], 9.0))
    b.route("gravel", g["forecourt"], 34.0)
    b.route("gravel", g["ring"], 6.0)
    for k in ("S1", "S2", "S3", "S4"):
        pts = g[k]
        s_out = s_where(pts, lambda x, z: not town(x, z, 0.98))
        L = polyline_length(pts)
        inner = [p for p in pts if polyline_dist(p, pts)[1] <= s_out] or pts[:2]
        outer = [p for p in pts if polyline_dist(p, pts)[1] >= s_out - 4] or pts[-2:]
        b.route("gravel", inner, 5.0)
        if s_out < L - 4:
            b.route("lane", outer, 4.0)
    b.route("footpath", g["loop"], 3.5)
    b.route("footpath", g["mill_lane"], 3.5)
    b.route("gravel", [(SQUARE[0] - 1, SQUARE[1]), (SQUARE[0] + 1, SQUARE[1])], 30.0)
    b.route("footpath", [(CHURCH[0], YARD[1] - 6), (CHURCH[0], CHURCH[1] - 5)], 2.5)
    b.route("footpath", g["back_lane"], 2.6)
    # the chateau's formal garden: side parterre walks and cross paths, the rear garden cross, the path to the rear gate
    for sx in (-1, 1):
        b.route("gravel", [(sx * 25.5, -36.0), (sx * 25.5, 1.0)], 1.8, keepout=False)
        for zz in (-24.0, -12.0):
            b.route("gravel", [(sx * 17.5, zz), (sx * 33.0, zz)], 1.6, keepout=False)
    b.route("gravel", [(-32.0, 41.0), (32.0, 41.0)], 2.0, keepout=False)
    b.route("gravel", [(0.0, 33.0), (0.0, 51.0)], 2.4, keepout=False)
    b.keepout_line(stream, 10.5)
    b.keepout_poly(rect(ENCL[0] - 1, ENCL[1] - 1, ENCL[2] + 1, ENCL[3] + 1))
    b.keepout_poly(rect(YARD[0] - 1, YARD[1] - 1, YARD[2] + 1, YARD[3] + 1))
    b.keepout_poly(circle_poly(SQUARE[0], SQUARE[1], 17.0))
    streets = [g["ring"], g["S1"], g["S2"], g["S3"], g["S4"], g["loop"], g["mill_lane"]]
    paint = GroundPaint()
    b.paint = paint

    # ------------------------------------------------------------------ 2. the stream and the two bridges
    b.chain("wat_stream_straight", stream, along="z", spacing=-0.6, tags={"stream"}, ok_with={"flat", "none"})
    bridges = {}
    for k, aid in (("S3", "drs_bridge_stone_large"), ("S4", "drs_bridge_stone_small")):
        d, s, p, t = crossing(g[k], stream)
        bridges[k] = b.place(aid, p[0], p[1], asset_yaw_along(*t), check=False, tags={"bridge", k})
        b.keepout_poly([local_to_world(p, asset_yaw_along(*t), lx, lz) for lx, lz in ((-16, -4), (16, -4), (16, 4), (-16, 4))])

    # ------------------------------------------------------------------ 3. chateau, walls, church, graveyard, mill
    chateau = building("vil_chateau_grand", CH[0], CH[1], 180.0, tags={"chateau"}, check=False)
    x0, z0, x1, z1 = ENCL
    walls, gates = [], []
    for line, gap in (([(x0, z0), (x1, z0)], [(x1 - x0) / 2, (x1 - x0) / 2]),       # front: the gate
                      ([(x1, z0), (x1, z1)], [(z1 - z0) / 2 - 8, (z1 - z0) / 2 - 4]),            # east postern
                      ([(x1, z1), (x0, z1)], [(x1 - x0) / 2, (x1 - x0) / 2]),       # rear gate
                      ([(x0, z1), (x0, z0)], [(z1 - z0) / 2 + 4, (z1 - z0) / 2 + 8])):           # west postern
        walls += b.chain("drs_garden_wall", line, along="x", gaps=[tuple(gap)], tags={"enclosure"}, check=False)
    for gx, gz in ((0.0, z0), (0.0, z1)):
        gates.append(b.place("drs_gate_piers", gx, gz, 0.0, check=False, tags={"enclosure", "gate"}))
    church = building("vil_church", CHURCH[0], CHURCH[1], 180.0, tags={"church"}, check=False)
    yx0, yz0, yx1, yz1 = YARD
    for line, gap in (([(yx0, yz0), (yx1, yz0)], [30.0, 30.0]),
                      ([(yx1, yz0), (yx1, yz1)], [20, 23]), ([(yx1, yz1), (yx0, yz1)], []), ([(yx0, yz1), (yx0, yz0)], [])):
        b.chain("drs_garden_wall", line, along="x", gaps=[tuple(gap)] if gap else (), tags={"graveyard"}, check=False)
    b.place("drs_gate_piers", yx0 + 30.0, yz0, 0.0, check=False, tags={"graveyard", "gate"})
    mill = building("vil_mill_house", MILL[0], MILL[1], 180.0, tags={"mill"})

    # ------------------------------------------------------------------ 4. houses along the streets and the square
    deck = Deck(TOWN_HOUSES, R)
    houses = []

    def frontage(street, half, side, s0, s1, tags=(), setback=(1.6, 2.8), gap=(0.0, 0.9)):   # terraces: near-continuous
        L = polyline_length(street)
        s, s1 = s0, min(s1, L)
        while s < s1:
            done = None
            for k in range(5):
                aid = deck.peek(k)
                a = cat[aid]
                sm = s + a.fw * 0.5
                if sm + a.fw * 0.5 > s1:
                    continue
                (px, pz), t = point_at(street, sm)
                nx, nz = -t[1] * side, t[0] * side
                off = half + R.uniform(*setback) + a.fd * 0.5
                x, z = px + nx * off, pz + nz * off
                yaw = asset_yaw_facing(-nx, -nz) + R.uniform(-5.0, 5.0)
                if not clear(aid, x, z, yaw):
                    continue
                e = building(aid, x, z, yaw, tags=set(tags) | {"house"})
                if e:
                    deck.take(k)
                    houses.append(e)
                    done = a
                    break
            s += (done.fw + R.uniform(*gap)) if done else 3.0

    def outward(line, c=CH):
        """The side (frontage `side`, offset_polyline sign) that points away from c."""
        (px, pz), t = point_at(line, polyline_length(line) * 0.3)
        return 1 if (-t[1]) * (px - c[0]) + t[0] * (pz - c[1]) > 0 else -1

    frontage(g["ring"], 3.0, outward(g["ring"]), 0, 1e9, tags={"ring"})
    for k, lim in (("S1", lambda x, z: z < -150), ("S2", lambda x, z: x < -150), ("S3", lambda x, z: z > 172), ("S4", lambda x, z: x > 150)):
        end = s_where(g[k], lim)
        for side in (1, -1):
            frontage(g[k], 2.5, side, 6.0, end, tags={k})
    for side in (1, -1):
        frontage(g["loop"], 1.75, side, 0.0, 1e9, tags={"loop"})
        frontage(g["mill_lane"], 1.75, side, 4.0, 1e9, tags={"mill_lane"})
    sq = circle_poly(SQUARE[0], SQUARE[1], 18.0, 40)
    sq = sq + [sq[0]]
    frontage(sq, 0.0, outward(sq, SQUARE), 0.0, 1e9, tags={"square"}, setback=(0.5, 1.5), gap=(0.5, 2.0))
    rep["houses"] = len(houses)
    # back yards: 7.5 m behind every house (no later building there; fenced and dressed in step 7)
    for e in houses:
        a = cat[e["asset"]]
        hx, hz = a.half
        p = [local_to_world((e["pos"][0], e["pos"][2]), e["yaw"], lx, lz) for lx, lz in ((-hx, -hz), (hx, -hz), (hx, -hz - 7.5), (-hx, -hz - 7.5))]
        yards.append((p, (min(q[0] for q in p), min(q[1] for q in p), max(q[0] for q in p), max(q[1] for q in p))))
    # infill: outbuildings, workshops and barns in the gaps between the house rows, turned to the nearest lane
    lanes = streets + [g["back_lane"]]
    infill = 0
    pts = [(x, z) for x in range(-130, 131, 6) for z in range(-140, 160, 6) if town(x, z, 0.98)]
    R.shuffle(pts)
    for x, z in pts:
        x, z = x + R.uniform(-2, 2), z + R.uniform(-2, 2)
        if b.near(x, z, 7.0, classes={"solid"}) or in_yard(x, z) or b.in_keepout(x, z):
            continue
        (d, s, _), ln = min(((polyline_dist((x, z), l), l) for l in lanes), key=lambda r: r[0][0])
        if d > 30:
            continue
        (px, pz), t = point_at(ln, s)
        aid = R.choice(INFILL)
        yaw = asset_yaw_facing(px - x, pz - z) if d > 0.1 else 0.0
        if clear(aid, x, z, yaw, pad=1.2):
            e = building(aid, x, z, yaw, tags={"infill"})
            infill += e is not None
    rep["infill"] = infill

    # ------------------------------------------------------------------ 5. green-ring fields and farms
    fields, lines = ring_fields(R)
    green = [(f, (sum(p[0] for p in f) / len(f), sum(p[1] for p in f) / len(f))) for f in fields]
    kinds = {}
    farm_fields = sorted((f for f, c in green if polyline_dist(c, stream)[0] > 30), key=poly_area, reverse=True)[:6]
    R.shuffle(farm_fields)
    farm_ids = {id(f) for f in farm_fields[:3]}
    for f, c in green:
        r = R.random()
        if id(f) in farm_ids:
            kinds[id(f)] = "farm"
        elif r < 0.32:
            kinds[id(f)] = "orchard"
        elif r < 0.52:
            kinds[id(f)] = "meadow"
        elif r < 0.70:
            kinds[id(f)] = "wheat"
        elif r < 0.85:
            kinds[id(f)] = "stubble"
        else:
            kinds[id(f)] = "ploughed"
    farms = []
    for f, c in green:
        if kinds[id(f)] != "farm":
            continue
        yaw = R.choice([0, 90, 180, 270]) + R.uniform(-8, 8)
        grp = []
        for k, aid in enumerate(R.sample(FARM, 3)):
            lx, lz = ((0, 0), (0, -17), (15, 4))[k]
            wx, wz = local_to_world(c, yaw, lx, lz)
            for _ in range(10):
                if clear(aid, wx, wz, yaw + 90 * k, pad=1.0) and in_poly((wx, wz), f):
                    e = building(aid, wx, wz, yaw + 90 * k, tags={"farm"})
                    if e:
                        grp.append(e)
                        break
                wx, wz = wx + R.uniform(-5, 5), wz + R.uniform(-5, 5)
        farms.append((c, grp))
    rep["farms"] = sum(len(gp) for _, gp in farms)

    # ------------------------------------------------------------------ 6. the decorative ring beyond the border
    def decor(aid, x, z, yaw, cls=None):
        a = cat[aid]
        if b.overlaps(a, x, z, yaw, {"flat", "none"}):
            return None
        e = b.place(aid, x, z, yaw, check=False, extra=DECOR, tags={"decor"}, cls=cls)
        if e is not None and pads is not None and a.category in BUILDING_CATS:
            pads.append((aid, x, z, e["yaw"]))
        return e

    # hamlets of 3-6 houses round a green, at 30-75 m beyond the border (outside the padded navmesh tiles), woods
    # and fields between them; every piece is extra.decor (never furnished, cheap to draw)
    decor_trees = ["tree_oak", "tree_ash", "tree_poplar", "tree_oak", "tree_ash"]
    perim = [(-HX - 52, -HZ - 52), (HX + 52, -HZ - 52), (HX + 52, HZ + 52), (-HX - 52, HZ + 52), (-HX - 52, -HZ - 52)]
    Lp = polyline_length(perim)
    hamlets, woods, plots = [], [], []
    s = R.uniform(0, 30)
    while s < Lp - 20:
        (px, pz), t = point_at(perim, s)
        nx_, nz_ = t[1], -t[0]                     # outward normal (the perimeter runs anticlockwise in x / z)
        if nx_ * px + nz_ * pz < 0:
            nx_, nz_ = -nx_, -nz_
        c = (px + nx_ * R.uniform(-14, 18), pz + nz_ * R.uniform(-14, 18))
        r = R.random()
        (hamlets if r < 0.45 else woods if r < 0.68 else plots).append((c, (nx_, nz_)))
        s += R.uniform(48, 72)
    ndecor = 0
    for c, nrm in hamlets:
        n = R.randint(3, 6)
        a0 = R.uniform(0, 360)
        for k in range(n):
            ang = math.radians(a0 + 360.0 * k / n + R.uniform(-18, 18))
            rr = R.uniform(13, 22)
            x, z = c[0] + rr * math.cos(ang), c[1] + rr * math.sin(ang)
            if abs(x) < HX + 30 and abs(z) < HZ + 30:
                continue
            aid = R.choice(DECOR_HOUSES)
            ndecor += decor(aid, x, z, asset_yaw_facing(c[0] - x, c[1] - z) + R.uniform(-20, 20)) is not None
        for k in range(R.randint(4, 8)):                 # orchard trees and hedges round the green
            ang, rr = R.uniform(0, 2 * math.pi), R.uniform(24, 34)
            decor(R.choice(["tree_apple", "tree_oak", "tree_ash"]), c[0] + rr * math.cos(ang), c[1] + rr * math.sin(ang), R.uniform(0, 360))
        paint.poly("dirt", circle_poly(c[0], c[1], 7.0, 14), weight=0.8, edge=3.0, ragged=1.5, seed=int(c[0]))
        paint.line("dirt", [c, (c[0] - nrm[0] * 60, c[1] - nrm[1] * 60)], 3.0, weight=0.75)   # the track toward town
    for c, nrm in woods:
        for k in range(R.randint(14, 26)):
            ang, rr = R.uniform(0, 2 * math.pi), 24 * math.sqrt(R.random())
            decor(R.choice(decor_trees + ["shrub_hazel"]), c[0] + rr * math.cos(ang), c[1] + rr * math.sin(ang), R.uniform(0, 360))
        paint.poly("leaf_litter", circle_poly(c[0], c[1], 20.0, 16), weight=0.7, edge=6.0, ragged=4.0, seed=int(c[1]))
    for c, nrm in plots:
        tx, tz = -nrm[1], nrm[0]
        corners = [(c[0] + tx * u + nrm[0] * v + R.uniform(-5, 5), c[1] + tz * u + nrm[1] * v + R.uniform(-5, 5))   # irregular, not a rectangle
                   for u, v in ((-32, -18), (0, -22), (31, -17), (34, 16), (-2, 23), (-30, 19))]
        paint.poly(R.choice(["ploughed", "dry_grass"]), corners, weight=0.9, edge=5.0, ragged=2.5, seed=int(c[0] + c[1]))
        for u in range(-28, 29, 8):                      # a hedge along the field's town side
            decor("hedgerow_bocage", c[0] + tx * u - nrm[0] * 22, c[1] + tz * u - nrm[1] * 22, asset_yaw_along(tx, tz))
    rep["decorHouses"] = ndecor
    rep["hamlets"] = len(hamlets)
    if pads is not None:
        return b, None, None, rep

    # ------------------------------------------------------------------ 7. dressing: houses, enclosure, churchyard, square
    for e in houses:
        a = cat[e["asset"]]
        hx, hz = a.half
        x, z, yaw = e["pos"][0], e["pos"][2], e["yaw"]
        if R.random() < 0.65:
            for sx in ((-1, 1) if R.random() < 0.5 else (R.choice((-1, 1)),)):
                b.place("drs_flower_bed", *local_to_world((x, z), yaw, sx * (hx - 1.8), hz + 0.75), yaw, tags={"dressing"})
        # the back yard (7.5 m deep): a picket fence or low stone wall round it, a vegetable plot, a fruit tree,
        # firewood, barrels, sacks or hay by the back door
        L = lambda lx, lz: local_to_world((x, z), yaw, lx, lz)
        street_ok = lambda p: not b.in_keepout(*p)
        wall = R.random() < 0.4
        piece = "stone_wall_low" if wall else "drs_picket_fence"
        pl = cat[piece].fw
        n = max(1, int((2 * hx - 0.4) // pl))
        for k in range(n):                                            # back edge
            p = L(-hx + 0.2 + pl * (k + 0.5), -hz - 7.2)
            if street_ok(p):
                b.place(piece, *p, yaw, tags={"yard"}, ok_with={"flat", "none", "veg"})
        for sx in (-1, 1):                                            # sides
            for k in range(int(7.0 // pl)):
                p = L(sx * (hx - 0.15), -hz - 0.3 - pl * (k + 0.5))
                if street_ok(p):
                    b.place(piece, *p, yaw + 90.0, tags={"yard"}, ok_with={"flat", "none", "veg"})
        if R.random() < 0.6 and hx > 3.5:                              # vegetable plot (ground paint) in one half
            sx = R.choice((-1, 1))
            plot = [L(sx * 0.8, -hz - 1.8), L(sx * (hx - 1.0), -hz - 1.8), L(sx * (hx - 1.0), -hz - 6.4), L(sx * 0.8, -hz - 6.4)]
            paint.poly(R.choice(["ploughed", "dirt"]), plot, weight=0.95, edge=0.6)
            other = -sx
        else:
            other = R.choice((-1, 1))
        if R.random() < 0.7:
            b.place(R.choice(["tree_apple", "tree_apple", "tree_ash"]), *L(other * min(hx - 2.0, 3.5), -hz - 4.5), R.uniform(0, 360),
                    tags={"yard"}, cls="veg")
        for aid in R.sample(["drs_woodpile", "drs_barrels", "furn_sacks", "furn_barrel", "drs_hay_bale_stack", "furn_bucket",
                             "furn_wood_pile", "furn_crate"], 2):
            p = L(R.uniform(-hx + 1.5, hx - 1.5), -hz - R.uniform(0.9, 1.6))
            if street_ok(p):
                b.place(aid, *p, yaw + R.uniform(-15, 15), tags={"yard"})
    # the back lane: low stone walls on one side, a hedge on the other, open where buildings or streets meet it
    bl = g["back_lane"]
    for off, piece in ((2.4, "stone_wall_low"), (-2.4, "hedgerow_bocage")):
        side = offset_polyline(bl, off)
        b.chain(piece, side, along="x", gaps=cut_gaps(side, streets, 5.0), tags={"back_lane"}, ok_with={"flat", "none", "veg", "wire"})
    # the chateau's formal garden: parterre beds along the side walks, clipped shrubs at the corners, benches facing
    # the drive, gas lamps along it; the rear garden with its bed rows, apple trees, the well and benches
    for sx in (-1, 1):
        for z0_, z1_ in ((-35.0, -25.5), (-22.5, -13.5), (-10.5, 0.0)):
            for xx in (sx * 20.0, sx * 30.5):
                for zz in [z0_ + 1.2 + 2.0 * k for k in range(int((z1_ - z0_ - 1.0) // 2.0))]:
                    b.place("drs_flower_bed", xx, zz, 90.0, tags={"parterre"})
            for xx, zz in ((sx * 22.5, z0_ + 0.8), (sx * 28.5, z0_ + 0.8), (sx * 22.5, z1_ - 0.8), (sx * 28.5, z1_ - 0.8)):
                b.place("shrub_hazel", xx, zz, R.uniform(0, 360), tags={"parterre"}, cls="veg")
        for zz in (-30.0, -18.0, -6.0):
            b.place("furn_bench", sx * 17.6, zz, -90.0 * sx, tags={"parterre"})   # facing the drive
            b.place("lamp_gas_street", sx * 16.6, zz + 3.0, 0.0, tags={"parterre"})
        b.chain("hedgerow_bocage", [(sx * 29.0, 5.0), (sx * 29.0, 31.0)], along="x", tags={"garden"}, ok_with={"flat", "none", "veg"})
        b.place("drs_sandbag_wall", sx * 9.0, -6.0, 0.0 + sx * 10, tags={"forecourt", "cover"})
        b.place("drs_sandbag_wall", sx * 12.0, -24.0, 0.0 - sx * 15, tags={"forecourt", "cover"})
        for xx in (sx * 8.0, sx * 14.0, sx * 20.0, sx * 26.0):
            for zz in (37.5, 44.5):
                b.place("drs_flower_bed", xx, zz, 0.0, tags={"garden"})
        for xx, zz in ((sx * 31.0, 35.5), (sx * 31.0, 46.5), (sx * 4.5, 47.0)):
            b.place("tree_apple", xx, zz, R.uniform(0, 360), tags={"garden"}, cls="veg")
        b.place("furn_bench", sx * 3.0, 38.5, 180.0, tags={"garden"})
    b.place("drs_well", 0.0, 44.5, 0.0, tags={"garden"})
    # kitchen gardens on any town lawn still open: a dug plot turned to the nearest lane, fruit trees and a fence
    gardens = 0
    pts = [(x, z) for x in range(-128, 129, 7) for z in range(-148, 160, 7) if town(x, z, 0.97)]
    R.shuffle(pts)
    for x, z in pts:
        x, z = x + R.uniform(-1.5, 1.5), z + R.uniform(-1.5, 1.5)
        if b.in_keepout(x, z) or in_yard(x, z) or b.near(x, z, 6.5, classes={"solid", "veg"}):
            continue
        (d, s, _), ln = min(((polyline_dist((x, z), l), l) for l in lanes), key=lambda r: r[0][0])
        yaw = asset_yaw_along(*point_at(ln, s)[1])
        Lg = lambda lx, lz: local_to_world((x, z), yaw, lx, lz)
        plot = [Lg(-3.5, -2.6), Lg(3.5, -2.6), Lg(3.5, 2.6), Lg(-3.5, 2.6)]
        if any(b.in_keepout(*p) for p in plot):
            continue
        paint.poly(R.choice(["ploughed", "ploughed", "dirt"]), plot, weight=0.95, edge=0.8)
        for lx in (-3.0, 0.0, 3.0):
            b.place("drs_picket_fence", *Lg(lx, -3.4), yaw, tags={"kitchen_garden"}, ok_with={"flat", "none", "veg"})
        for lx in (-2.5, 2.5):
            if R.random() < 0.7:
                b.place(R.choice(["tree_apple", "tree_apple", "shrub_hazel"]), *Lg(lx, 4.6), R.uniform(0, 360), tags={"kitchen_garden"}, cls="veg")
        if R.random() < 0.4:
            b.place(R.choice(["furn_bucket", "furn_sacks", "drs_woodpile", "furn_barrel"]), *Lg(4.6, 0.0), yaw, tags={"kitchen_garden"})
        gardens += 1
    rep["kitchenGardens"] = gardens
    # churchyard: rows of graves before and behind the church, the cross by the path, yews (oaks) by the walls
    graves = ["drs_gravestone_round", "drs_gravestone_cross", "drs_tomb_chest", "drs_gravestone_round"]
    for zz, xs in ((31.0, range(-121, -68, 2)), (35.5, range(-121, -68, 2)), (60.0, range(-120, -69, 2)), (64.5, range(-120, -69, 2)),
                   (68.5, range(-120, -69, 2))):
        for xx in xs:
            if abs(xx - CHURCH[0]) < 3.5 or R.random() < 0.2:
                continue
            b.place(R.choice(graves), xx + R.uniform(-0.2, 0.2), zz, 180.0 + R.uniform(-4, 4), tags={"graveyard"})
    b.place("drs_churchyard_cross", CHURCH[0] - 8.0, 39.0, 180.0, tags={"graveyard"})
    for p in ((-121, 44), (-121, 52), (-69, 44), (-69, 52), (-110, 69), (-80, 69)):
        b.place(R.choice(["tree_oak", "tree_ash"]), p[0], p[1], R.uniform(0, 360), tags={"graveyard"})
    # town square: the well, carts, barrels and flower beds round the edge
    b.place("drs_well", SQUARE[0], SQUARE[1], 0.0, tags={"square"})
    for k in range(4):
        a = 2 * math.pi * k / 4 + 0.75
        b.place("lamp_gas_street", SQUARE[0] + 8.0 * math.cos(a), SQUARE[1] + 8.0 * math.sin(a), 0.0, tags={"square"})
        b.place("furn_bench", SQUARE[0] + 10.0 * math.cos(a + 0.4), SQUARE[1] + 10.0 * math.sin(a + 0.4), math.degrees(-(a + 0.4)) + 90, tags={"square"})
    for k in range(10):
        a = 2 * math.pi * k / 10 + 0.2
        px, pz = SQUARE[0] + 13.0 * math.cos(a), SQUARE[1] + 13.0 * math.sin(a)
        b.try_place(["drs_cart", "drs_barrels", "drs_flower_bed", "drs_hay_bale_stack", "drs_flower_bed"][k % 5], px, pz,
                    math.degrees(-a), R, tags={"square"})
    # carts, wells and hay along the streets
    for k in range(12):
        st = R.choice(streets[1:])
        (px, pz), t = point_at(st, R.uniform(10, polyline_length(st) - 10))
        if not town(px, pz):
            continue
        side = R.choice((-1, 1))
        b.place(R.choice(["drs_cart", "drs_cart", "drs_hay_bale_stack", "drs_barrels"]), px - t[1] * side * 3.6, pz + t[0] * side * 3.6,
                asset_yaw_along(*t), tags={"street"})

    # ------------------------------------------------------------------ 8. the town approaches: sandbags and wooden X
    approaches = []
    for k, lim in (("S1", lambda x, z: z < -150), ("S2", lambda x, z: x < -150), ("S3", lambda x, z: z > 172), ("S4", lambda x, z: x > 150)):
        s = s_where(g[k], lim)
        (px, pz), t = point_at(g[k], s)
        nx, nz = -t[1], t[0]
        ring = b.try_place("drs_sandbag_ring", px + nx * 9 - t[0] * 2, pz + nz * 9 - t[1] * 2, asset_yaw_along(*t), R, tags={"approach", "cover"})
        for j in (-1, 1):
            b.try_place("drs_sandbag_wall", px - nx * 6.5 + t[0] * 2 * j, pz - nz * 6.5 + t[1] * 2 * j, asset_yaw_along(nx, nz) + 15 * j, R,
                        tags={"approach", "cover"})
            b.try_place("def_sandbag_curved", px + nx * 4.5 * j + t[0] * 3, pz + nz * 4.5 * j + t[1] * 3, asset_yaw_along(nx, nz), R,
                        tags={"approach", "cover"})
        cx, cz = px + t[0] * 14, pz + t[1] * 14
        belt = [(cx - nx * 16, cz - nz * 16), (cx + nx * 16, cz + nz * 16)]
        xs = b.belt("drs_wooden_x_obstacle", belt, 3.4, R, gaps=[(17.5, 21.5)], jitter_across=1.2, yaw_jitter=12, tags={"approach"})
        approaches.append((k, (px, pz), len(xs) + (1 if ring else 0)))
    rep["approaches"] = approaches
    for k in ("S1", "S2"):   # the country lanes into town run sunken between the banks
        pts = g[k]
        s0 = s_where(pts, lambda x, z: not town(x, z, 1.08)) + 4
        tail = [p for p in resample(pts, 3.0) if polyline_dist(p, pts)[1] >= s0]
        if len(tail) > 2:
            sunken_lane(b, tail)

    # ------------------------------------------------------------------ 9. green ring: hedgerows, orchards, meadow, wheat
    grp = b.new_group()
    roads = [g["S1"], g["S2"], g["S3"], g["S4"]]
    nhedge = 0
    for ln in lines:
        gaps = cut_gaps(ln, roads, 6.0) + gaps_where(ln, lambda x, z: town(x, z, 0.93) or polyline_dist((x, z), stream)[0] < 9
                                                     or any(e is not None and math.hypot(x - e["pos"][0], z - e["pos"][2]) < 12 for e in bridges.values()))
        pl, tr = hedgerow(b, ln, R, group=grp, gaps=gaps, gate_every=(45.0, 90.0),
                          trees=(("tree_ash", 3.0), ("tree_oak", 1.0), ("tree_poplar", 0.6)), tags={"green"})   # ash: a third of an oak's triangles
        nhedge += len(pl)
        for e in pl:   # brambles, hazel and blackthorn at the foot of the bank, both sides, so no bare bank shows
            if R.random() < 0.3:
                side = R.choice((-1, 1))
                p = local_to_world((e["pos"][0], e["pos"][2]), e["yaw"], R.uniform(-4.0, 4.0), side * R.uniform(2.0, 2.6))
                b.place(R.choice(["shrub_bramble", "shrub_hazel", "nat_blackthorn_low", "nat_bramble_thicket"]), *p, R.uniform(0, 360),
                        tags={"hedge_foot"}, cls="veg")
        if pl:   # the hedge's shade and leaf litter: the field pattern still reads where the hedges are culled
            paint.line("leaf_litter", ln, 5.0, weight=0.55, edge=2.0, mask=lambda x, z: not town(x, z, 0.95))
    rep["hedgePieces"] = nhedge
    for k_, ln in enumerate(l for l in lines if len(l) == 3):   # farm tracks along every third radial hedge
        if k_ % 3 == 0:
            paint.line("dirt", offset_polyline(ln, 4.0), 2.8, weight=0.75, edge=1.0,
                       mask=lambda x, z: not town(x, z, 0.95) and polyline_dist((x, z), stream)[0] > 6)
    norch = 0
    FLATKW = dict(cls="flat", ok_with={"flat", "none", "veg", "wire"})
    for f, c in green:
        k = kinds[id(f)]
        field = lambda x, z, f=f: in_poly((x, z), f) and not town(x, z, 0.95)
        bb = (min(p[0] for p in f), min(p[1] for p in f), max(p[0] for p in f), max(p[1] for p in f))
        # the crop or grass fills the whole enclosure, fading in from the hedge with a ragged edge
        tint = {"orchard": ("dry_grass", 0.3), "meadow": ("dry_grass", 0.18), "wheat": ("dry_grass", 0.85), "stubble": ("dry_grass", 0.7),
                "ploughed": ("ploughed", 1.0), "farm": ("dry_grass", 0.25)}[k]
        paint.poly(tint[0], f, weight=tint[1], edge=6.0, ragged=2.5, seed=int(c[0] * 7 + c[1]), mask=lambda x, z: not town(x, z, 0.95))
        if k == "orchard":   # rows, kept 4 m off the hedges
            ang = math.radians(R.uniform(-15, 15))
            ca, sa = math.cos(ang), math.sin(ang)
            for u in range(-70, 71, 8):
                for v in range(-70, 71, 7):
                    if R.random() < 0.1:
                        continue
                    x = c[0] + u * ca - v * sa + R.uniform(-0.6, 0.6)
                    z = c[1] + u * sa + v * ca + R.uniform(-0.6, 0.6)
                    if field(x, z) and not b.in_keepout(x, z) and min(seg_dist((x, z), f[i], f[(i + 1) % len(f)])[0] for i in range(len(f))) > 4.0:
                        norch += b.place("tree_apple", x, z, R.uniform(0, 360), tags={"orchard"}) is not None
            b.scatter([("nat_meadow_mixed", 2), ("nat_flowers_daisy", 1), ("nat_clover_tufts", 1)], field, 1 / 400.0, R, bounds=bb,
                      tags={"orchard"}, **FLATKW)
        elif k == "meadow":
            b.scatter([("nat_meadow_mixed", 3), ("nat_flowers_meadow", 2), ("nat_flowers_buttercup", 1), ("nat_flowers_poppy", 1),
                       ("nat_flowers_cowparsley", 1), ("nat_tallgrass_patch", 2)], field, 1 / 160.0, R, bounds=bb, tags={"meadow"}, **FLATKW)
            b.scatter([("tree_oak", 2), ("tree_ash", 1), ("nat_hawthorn_bush", 2)], field, 1 / 1200.0, R, bounds=bb, tags={"meadow"}, cls="veg")
        elif k == "wheat":
            b.scatter([("nat_wheat_field", 6), ("nat_wheat_trampled", 1)], field, 1 / 62.0, R, bounds=bb, tags={"wheat"}, min_dist=7.0, **FLATKW)
        elif k == "stubble":   # cut wheat: stubble patches and the hay stacked where it was baled
            b.scatter([("nat_wheat_stubble", 1)], field, 1 / 80.0, R, bounds=bb, tags={"stubble"}, min_dist=8.0, **FLATKW)
            b.scatter([("drs_hay_bale_stack", 1), ("furn_hay_bales", 2)], field, 1 / 260.0, R, bounds=bb, tags={"stubble"}, min_dist=9.0)
        elif k == "farm":
            b.scatter([("nat_drygrass_tufts", 1), ("nat_green_tufts", 1)], field, 1 / 500.0, R, bounds=bb, tags={"farm"}, **FLATKW)
    for c, gp in farms:   # the farmyard: trodden dirt and mud, a low wall with a field gate, hay, carts, firewood, a well
        paint.poly("dirt", circle_poly(c[0], c[1], 17.0, 18), weight=0.9, edge=4.0, ragged=3.0, seed=int(c[0]))
        paint.poly("mud", circle_poly(c[0] + 4, c[1] - 3, 5.0, 10), weight=0.6, edge=2.0, ragged=1.5, seed=int(c[1]))
        ang0 = R.uniform(0, 360)
        for k in range(10):
            a = math.radians(ang0 + 27.0 * k)
            px, pz = c[0] + 21.0 * math.cos(a), c[1] + 21.0 * math.sin(a)
            if not b.in_keepout(px, pz):
                b.place("field_gate" if k == 0 else "stone_wall_low", px, pz, asset_yaw_along(-math.sin(a), math.cos(a)), tags={"farm"},
                        ok_with={"flat", "none", "veg"})
        for aid in ("drs_hay_bale_stack", "drs_hay_bale_stack", "drs_hay_bale_stack", "drs_cart", "drs_cart", "drs_woodpile",
                    "drs_well", "drs_barrels", "furn_sacks"):
            b.try_place(aid, c[0] + R.uniform(-15, 15), c[1] + R.uniform(-15, 15), R.uniform(0, 360), R, tags={"farm"})
    rep["orchardTrees"] = norch

    # ------------------------------------------------------------------ 10. town greenery between the buildings
    in_town = lambda x, z: town(x, z) and not in_poly((x, z), rect(*ENCL)) and not in_poly((x, z), rect(*YARD))
    b.scatter([("tree_oak", 3), ("tree_ash", 3), ("tree_apple", 3), ("tree_poplar", 1)], in_town, 1 / 210.0, R,
              bounds=(-HX, -HZ, HX, HZ), tags={"town_green"}, min_dist=6.0)
    b.scatter([("shrub_hazel", 3), ("shrub_bramble", 1), ("nat_hawthorn_bush", 2), ("nat_blackthorn_low", 1), ("nat_fern_clump", 1)],
              in_town, 1 / 170.0, R, bounds=(-HX, -HZ, HX, HZ), tags={"town_green"}, cls="veg")
    b.scatter([("nat_flowers_cowparsley", 1), ("nat_tallgrass_verge", 2), ("nat_flowers_poppy", 1), ("nat_green_tufts", 2)], in_town,
              1 / 450.0, R, bounds=(-HX, -HZ, HX, HZ), tags={"town_green"}, cls="flat", ok_with={"flat", "none", "veg", "wire"})
    # stream banks: willows are not in the catalogue; ash and alder-like hazel along the water
    b.scatter([("tree_ash", 2), ("shrub_hazel", 2), ("nat_fern_clump", 1)], lambda x, z: 7.0 < polyline_dist((x, z), stream)[0] < 13.0,
              1 / 120.0, R, bounds=(-HX, -HZ, HX, HZ), avoid_routes=False, tags={"stream_bank"}, cls="veg",
              accept=lambda x, z: 0.0 if any(e and math.hypot(x - e["pos"][0], z - e["pos"][2]) < 18 for e in bridges.values()) else 1.0)

    # ------------------------------------------------------------------ 11. decor ring: a loose tree line just beyond the border
    nt = 0
    for _ in range(2600):
        x, z = R.uniform(-HX - 86, HX + 86), R.uniform(-HZ - 86, HZ + 86)
        if (abs(x) < HX + 6 and abs(z) < HZ + 6) or abs(x) > HX + 30 or abs(z) > HZ + 30:
            continue
        nt += decor(R.choice(decor_trees + ["shrub_hazel"]), x, z, R.uniform(0, 360)) is not None
        if nt >= 260:
            break
    rep["decorTrees"] = nt

    # companions that came down on a street (trees in front of a house) are dropped
    street_keep = [k for k in b.keepouts if k[0] == "line" and k[1] is not stream]
    for e in [e for e in b.inst if "companion" in e["tags"]]:
        if any(polyline_dist((e["pos"][0], e["pos"][2]), pts)[0] < half for _, pts, half in street_keep):
            b.remove(e)

    # ------------------------------------------------------------------ 12. scenario: Assault A-D, TDM, crates
    sc = ScenarioBuilder(b, defending="Axis")
    br = bridges["S3"]
    sq_houses = [e for e in houses if "square" in e["tags"]]
    near_bridge = [e for e in houses if math.hypot(e["pos"][0] - br["pos"][0], e["pos"][2] - br["pos"][2]) < 45] + ([mill] if mill else [])
    sc.objective("Chateau forecourt", 0.0, -16.0, radius=24.0, capture_time=50.0, attack_from=(0, -200),
                 linked=[chateau["id"]] + [e["id"] for e in gates], slots=slots_from_structures(b, [chateau]))
    sc.objective("Church and graveyard", CHURCH[0], CHURCH[1] - 10.0, radius=20.0, capture_time=45.0, attack_from=(-42, -88),
                 linked=[church["id"]], slots=slots_from_structures(b, [church]))
    sc.objective("Mill bridge", br["pos"][0], br["pos"][2], radius=14.0, capture_time=40.0, attack_from=(-52, -58),
                 linked=[br["id"]] + [e["id"] for e in near_bridge], slots=slots_from_structures(b, near_bridge))
    sc.objective("Town square", SQUARE[0], SQUARE[1], radius=18.0, capture_time=45.0, attack_from=(-72, 132),
                 linked=[e["id"] for e in sq_houses], slots=slots_from_structures(b, sq_houses))
    P = spawn_pts
    sc.spawn("attacker", [0], *P["attA"], label="Allies - south lane")
    sc.spawn("defender", [0], *P["defA"], label="Axis - behind the chateau")
    sc.spawn("attacker", [1], *P["attB"], label="Allies - Rue du Tour")
    sc.spawn("defender", [1], *P["defB"], label="Axis - north-west fields")
    sc.spawn("attacker", [2], *P["attC"], label="Allies - chateau grounds")
    sc.spawn("defender", [2], *P["defC"], label="Axis - north lane")
    sc.spawn("attacker", [3], *P["attD"], label="Allies - across the stream")
    sc.spawn("defender", [3], *P["defD"], label="Axis - north-east orchards")
    for team, key, role, fac in ((0, "teamA", "defender", "Allies"), (1, "teamB", "attacker", "Axis")):
        m = sc.spawn(role, [0], *P[key], face=(0.0, 0.0), faction=fac, label="Team A spawn" if team == 0 else "Team B spawn")
        m["team"], m["phases"] = team, []
    crates = 0
    for m in sc.spawns:
        x, z, yw = m["pos"][0], m["pos"][2], math.radians(m["yaw"])
        bx, bz = x + math.sin(yw) * 3.0, z + math.cos(yw) * 3.0   # behind the marker (it looks along -sin, -cos)
        crates += b.try_place("crate_mixed_supply", bx, bz, m["yaw"], R, tags={"supply"}, radius=3.0) is not None
    rep["crates"] = crates
    return b, sc, g, rep


def update_usage_doc(path, use):
    """The "Chateau town" column of docs/asset-usage-default-maps.md (placed counts, companions included)."""
    with open(path, encoding="utf-8") as f:
        lines = f.read().split("\n")
    has = any(l.startswith("| Asset |") and "Chateau town" in l for l in lines)
    out = []
    for l in lines:
        body = l.rstrip()
        cell = None
        if body.startswith("| Asset |"):
            cell = "Chateau town"
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
    note = ("- **Chateau town:** *Chateau town* (M1, `tools/mapgen/maps/chateau_town.py`): placed counts, companions included; "
            "\"not used\" = not on this map.")
    if not any(l.startswith("- **Chateau town:**") for l in out):
        k = max(i for i, l in enumerate(out) if l.startswith("- **Mont-Castre:**"))
        out.insert(k + 1, note)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out))


def build(ctx):
    grid = Grid(W, D, margin=MARGIN)
    grid.apply(lambda x, z, h: base_height(x, z))
    g = geometry()
    stream = g["stream"]
    L = polyline_length(stream)
    fl = [grid.sample(*point_at(stream, L * i / 200.0)[0]) - 2.4 for i in range(201)]
    floor = lambda u: fl[min(200, max(0, int(round(u * 200))))]
    stamp_valley(grid, stream, floor=floor, top_half=lambda u: 8.0, floor_half=lambda u: 3.0)
    # pass 1: where the buildings go; level the ground under each (footprint + 1.5 m) so they stand flat
    pads = []
    populate(ctx, grid, pads)
    for aid, x, z, yaw in pads:
        a = ctx.catalog[aid]
        hx, hz = a.half[0] + 1.5, a.half[1] + 1.5
        poly = [local_to_world((x, z), yaw, lx, lz) for lx, lz in ((-hx, -hz), (hx, -hz), (hx, hz), (-hx, hz))]
        stamp_flatten_poly(grid, poly, height=grid.sample(x, z), falloff=4.0)
    grid.quantise()
    b, sc, g, rep = populate(ctx, grid)
    err, warn = sc.validate()
    for m in sc.spawns:
        if not m["rack"]:
            err.append("spawn %s has no rack" % m["label"])

    # ------------------------------------------------------------------ output
    tdir = ctx.templates
    grid.write_png(os.path.join(tdir, TID + ".height.png"))
    eye = lambda x, z: [round(x, 2), round(b.ground(x, z) + 1.7, 2), round(z, 2)]
    a1 = sc.spawns[0]["pos"]
    cam = ([0.0, 150.0, -330.0], 180.0, -26.0)
    thumb_cam = ([60.0, 95.0, -250.0], look_yaw(-60.0, 250.0), -20.0)
    br = [e for e in b.tagged("bridge") if "S3" in e["tags"]][0]
    brx, brz = br["pos"][0], br["pos"][2]
    s1p, s1t = point_at(g["S1"], 70.0)
    orch = [e for e in b.tagged("orchard") if e["asset"] == "tree_apple"]
    # orchard view: between a tree and its nearest neighbour (half a row apart), looking down the gap between the rows
    hedges = [e for e in b.inst if e["asset"].startswith(("bcg_", "hedgerow"))]
    near = lambda e, pool, r: sum(1 for o in pool if o is not e and math.hypot(o["pos"][0] - e["pos"][0], o["pos"][2] - e["pos"][2]) < r)
    inside = [e for e in orch if near(e, orch, 11.0) >= 6 and near(e, hedges, 16.0) == 0]   # deep in a block, no hedge close by
    t0 = (inside or orch)[len(inside or orch) // 2]
    t1 = min((e for e in orch if e is not t0), key=lambda e: math.hypot(e["pos"][0] - t0["pos"][0], e["pos"][2] - t0["pos"][2]))
    ox, oz = (t0["pos"][0] + t1["pos"][0]) / 2, (t0["pos"][2] + t1["pos"][2]) / 2
    ow = (-(t1["pos"][2] - t0["pos"][2]), t1["pos"][0] - t0["pos"][0])
    views = [
        {"name": "overview_high", "pos": [0.0, 170.0, -250.0], "yaw": 180.0, "pitch": -38.0},
        {"name": "forecourt_eye", "pos": eye(4.0, -46.0), "yaw": look_yaw(-4.0, CH[1] - (-46.0)), "pitch": 6.0, "gameplay": True},
        {"name": "street_eye", "pos": eye(s1p[0] + s1t[1] * 1.2, s1p[1] - s1t[0] * 1.2), "yaw": look_yaw(-s1t[0], -s1t[1]), "pitch": 2.0, "gameplay": True},
        {"name": "churchyard_eye", "pos": eye(-89.0, 15.0), "yaw": look_yaw(CHURCH[0] - (-89.0), CHURCH[1] - 15.0), "pitch": 7.0, "gameplay": True},
        {"name": "bridge_eye", "pos": eye(brx + 24.0, brz + 4.0), "yaw": look_yaw(-24.0, -4.0), "pitch": -3.0, "gameplay": True},
        {"name": "square_eye", "pos": eye(SQUARE[0] - 12.0, SQUARE[1] - 10.0), "yaw": look_yaw(12.0, 10.0), "pitch": 3.0, "gameplay": True},
        {"name": "orchard_eye", "pos": eye(ox, oz), "yaw": look_yaw(*ow), "pitch": 1.0, "gameplay": True},
        {"name": "town_from_south", "pos": [30.0, 45.0, -175.0], "yaw": look_yaw(-30.0, 175.0), "pitch": -12.0},
    ]
    write_map(os.path.join(tdir, TID + ".nmap.json"), name=NAME, template_id=TID, template_version=VERSION, seed=SEED, width=W, depth=D,
              builder=b, scenario_json=sc.to_json(), explore_start=((a1[0], a1[1], a1[2]), 180.0), camera=cam,
              bookmarks={str(i + 1): {"position": [o["pos"][0], o["pos"][1] + 40, o["pos"][2] - 70], "yaw": 180.0, "pitch": -28}
                         for i, o in enumerate(sc.objectives)},
              extra={"groundPaint": b.paint.to_json()})
    write_template(os.path.join(tdir, TID + ".json"), tid=TID, name=NAME, order=5, version=VERSION, seed=SEED,
                   description="A dense Norman town around a grand chateau: the chateau on its gravel forecourt behind garden walls "
                               "and an iron gate, a ring road and four winding streets of town houses, the church in its walled "
                               "graveyard, the town square, a mill stream with two stone arched bridges, sandbags and wooden "
                               "obstacles at the approaches; orchards, bocage, sunken lanes and farms out to the border. Assault "
                               "(Allies attack the forecourt, church, bridge and square), Team Deathmatch and Solo. 350 x 400 m.",
                   width=W, depth=D, margin=MARGIN, heightmap=grid.meta(TID + ".height.png"), water_level=-1000.0,
                   map_file=TID + ".nmap.json", camera=cam, thumbnail=TID + ".thumb.png", thumb_camera=thumb_cam,
                   extra={"userMap": True, "views": views})   # userMap: --selftest-maps generic checks (no SPEC 2.1 bands)
    use = asset_usage(b)
    update_usage_doc(os.path.join(ctx.repo, "docs", "asset-usage-default-maps.md"), use)
    rep.update({"instances": len(b.inst), "errors": err, "warnings": warn, "use": dict(sorted(use.items())),
                "rejects": {"%s/%s" % k: v for k, v in sorted(b.rejects.items())},
                "objectives": [(o["label"], o["displayName"], o["pos"], len(o["linkedStructures"]), len(o["defenseSlots"])) for o in sc.objectives]})
    write_summary(os.path.join(ctx.out, TID + ".summary.json"), rep)
    print("instances %d | houses %d | errors %s | warnings %d | crates %d | decor %d+%d" %
          (len(b.inst), rep["houses"], err, len(warn), rep["crates"], rep["decorHouses"], rep["decorTrees"]))
