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

from mapgen import (Grid, Frame, MapBuilder, ScenarioBuilder, stamp_valley, stamp_flatten_poly, hedgerow,
                    sunken_lane, cut_gaps, slots_from_structures, catmull_rom, write_map, write_template, write_summary,
                    asset_usage, rect, rng, point_at, polyline_length, polyline_dist, offset_polyline, in_poly, poly_area,
                    asset_yaw_facing, asset_yaw_along, look_yaw, local_to_world, fbm, circle_poly, dist, resample)

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


def ring_fields(R, n=16):
    """The green ring as bocage fields: sectors between jittered radial hedges from the town edge to the border, the
    wide ones (corners) split again by a cross hedge. Returns (field polygons, hedge lines)."""
    c = (0.0, 8.0)
    inner = lambda a: (c[0] + 128.0 * 1.06 * math.cos(a), c[1] + 150.0 * 1.06 * math.sin(a))

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
        lines.append(ins)                               # the hedge along the town edge
        band = math.hypot(outs[4][0] - ins[4][0], outs[4][1] - ins[4][1])
        if band > 80:
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

    def clear(aid, x, z, yaw, pad=0.6):
        a = cat[aid]
        hx, hz = a.half[0] + pad, a.half[1] + pad
        nx, nz = max(2, int(math.ceil(2 * hx / 3.0)) + 1), max(2, int(math.ceil(2 * hz / 3.0)) + 1)
        for i in range(nx):
            for j in range(nz):
                wx, wz = local_to_world((x, z), yaw, -hx + 2 * hx * i / (nx - 1), -hz + 2 * hz * j / (nz - 1))
                if not grid.in_build(wx, wz) or b.in_keepout(wx, wz):
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
    b.keepout_line(stream, 10.5)
    b.keepout_poly(rect(ENCL[0] - 1, ENCL[1] - 1, ENCL[2] + 1, ENCL[3] + 1))
    b.keepout_poly(rect(YARD[0] - 1, YARD[1] - 1, YARD[2] + 1, YARD[3] + 1))
    b.keepout_poly(circle_poly(SQUARE[0], SQUARE[1], 17.0))
    streets = [g["ring"], g["S1"], g["S2"], g["S3"], g["S4"], g["loop"], g["mill_lane"]]

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

    def frontage(street, half, side, s0, s1, tags=(), setback=(2.2, 3.6), gap=(0.8, 4.0)):
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
        elif r < 0.5:
            kinds[id(f)] = "orchard"
        elif r < 0.86:
            kinds[id(f)] = "meadow"
        else:
            kinds[id(f)] = "wheat"
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

    decor_trees = ["tree_oak", "tree_ash", "tree_poplar", "tree_apple"]
    ndecor = 0
    for off in (16.0, 44.0):
        for side in range(4):
            along_x = side < 2
            sgn = 1.0 if side in (0, 2) else -1.0
            half = (HX if along_x else HZ) + off
            yaw_in = (180.0 if side == 0 else 0.0) if along_x else (270.0 if side == 2 else 90.0)
            u, last = -half + R.uniform(0, 12), None
            while u < half:
                aid = R.choice(DECOR_HOUSES)
                a = cat[aid]
                uc = u + a.fw * 0.5
                dd = (HZ if along_x else HX) + off + a.fd * 0.5 + R.uniform(0, 4)
                x, z = (uc, sgn * dd) if along_x else (sgn * dd, uc)
                e = decor(aid, x, z, yaw_in + R.uniform(-5, 5))
                if e is None:
                    u += 6.0
                    continue
                ndecor += 1
                if last is not None and u - last > 9:   # hedge pieces and a tree or two in the gap
                    rd = (HZ if along_x else HX) + off + 2.0
                    s = last + 1.0
                    while s + 8.0 <= u - 1.0:
                        hx_, hz_ = (s + 4, sgn * rd) if along_x else (sgn * rd, s + 4)
                        decor("hedgerow_bocage", hx_, hz_, 0.0 if along_x else 90.0)
                        s += 8.0
                    for _ in range(int((u - last) / 12.0 + R.random())):
                        tu, td = R.uniform(last + 2, u - 2), rd + R.uniform(3, 10)
                        decor(R.choice(decor_trees), *((tu, sgn * td) if along_x else (sgn * td, tu)), R.uniform(0, 360))
                last = u + a.fw
                u += a.fw + R.uniform(4, 14)
    rep["decorHouses"] = ndecor
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
        r = R.random()
        rear = local_to_world((x, z), yaw, R.uniform(-hx + 2, hx - 2), -hz - 1.6)
        if r < 0.25:
            b.place("drs_woodpile", *rear, yaw, tags={"dressing"})
        elif r < 0.42:
            b.place("drs_barrels", *rear, yaw + R.uniform(-20, 20), tags={"dressing"})
        elif r < 0.52:
            b.place("drs_hay_bale_stack", *rear, yaw, tags={"dressing"})
        if R.random() < 0.3:
            for k in (-1, 0, 1):
                p = local_to_world((x, z), yaw, k * 3.0, -hz - 5.0)
                if not b.in_keepout(*p):
                    b.place("drs_picket_fence", *p, yaw, tags={"dressing"})
    # forecourt: flower beds and clipped shrubs along the drive, sandbag walls in front of the steps
    for zz in range(-32, 0, 5):
        for sx in (-1, 1):
            b.place("drs_flower_bed", sx * 19.5, zz, 90.0, tags={"forecourt"})
            b.place("shrub_hazel", sx * 23.5, zz + 2.5, R.uniform(0, 360), tags={"forecourt"})
    for sx in (-1, 1):
        b.place("drs_sandbag_wall", sx * 9.0, -6.0, 0.0 + sx * 10, tags={"forecourt", "cover"})
        b.place("drs_sandbag_wall", sx * 14.0, -22.0, 0.0 - sx * 15, tags={"forecourt", "cover"})
        b.place("drs_well" if sx < 0 else "drs_cart", sx * 28.0, 42.0, R.uniform(0, 90), tags={"garden"})
    b.scatter([("tree_oak", 2), ("tree_ash", 2), ("tree_apple", 1), ("shrub_hazel", 2), ("nat_hawthorn_bush", 1)],
              lambda x, z: x0 + 2 < x < x1 - 2 and z0 + 2 < z < z1 - 2 and (abs(x) > 26 or z > 34), 1 / 45.0, R,
              bounds=(x0, z0, x1, z1), avoid_routes=False, tags={"garden"}, min_dist=4.5)
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
        gaps = cut_gaps(ln, roads, 6.0) + gaps_where(ln, lambda x, z: town(x, z, 1.0) or polyline_dist((x, z), stream)[0] < 9
                                                     or any(e is not None and math.hypot(x - e["pos"][0], z - e["pos"][2]) < 12 for e in bridges.values()))
        pl, tr = hedgerow(b, ln, R, group=grp, gaps=gaps, gate_every=(45.0, 90.0), tags={"green"})
        nhedge += len(pl)
    rep["hedgePieces"] = nhedge
    norch = 0
    for f, c in green:
        k = kinds[id(f)]
        inner = [(c[0] + (p[0] - c[0]) * 0.86, c[1] + (p[1] - c[1]) * 0.86) for p in f]
        if k == "orchard":
            ang = math.radians(R.uniform(-15, 15))
            ca, sa = math.cos(ang), math.sin(ang)
            for u in range(-60, 61, 8):
                for v in range(-60, 61, 7):
                    if R.random() < 0.12:
                        continue
                    x = c[0] + u * ca - v * sa + R.uniform(-0.8, 0.8)
                    z = c[1] + u * sa + v * ca + R.uniform(-0.8, 0.8)
                    if in_poly((x, z), inner) and not b.in_keepout(x, z) and not town(x, z):
                        norch += b.place("tree_apple", x, z, R.uniform(0, 360), tags={"orchard"}) is not None
            b.scatter([("nat_meadow_mixed", 2), ("nat_flowers_daisy", 1), ("nat_clover_tufts", 1)], inner, 1 / 400.0, R, tags={"orchard"},
                      cls="flat", ok_with={"flat", "none", "veg", "wire"})
        elif k == "meadow":
            b.scatter([("nat_meadow_mixed", 3), ("nat_flowers_meadow", 2), ("nat_flowers_buttercup", 1), ("nat_flowers_poppy", 1),
                       ("nat_flowers_cowparsley", 1), ("nat_tallgrass_patch", 2)], inner, 1 / 160.0, R, tags={"meadow"}, cls="flat",
                      ok_with={"flat", "none", "veg", "wire"})
            b.scatter([("tree_oak", 2), ("tree_ash", 1), ("nat_hawthorn_bush", 2), ("nat_blackthorn_low", 1)], inner, 1 / 900.0, R,
                      tags={"meadow"}, cls="veg")
        elif k == "wheat":
            b.scatter([("nat_wheat_field", 6), ("nat_wheat_trampled", 1)], inner, 1 / 85.0, R, tags={"wheat"}, min_dist=8.5, cls="flat",
                      ok_with={"flat", "none", "veg", "wire"})
        else:
            b.scatter([("nat_drygrass_tufts", 1), ("nat_green_tufts", 1)], inner, 1 / 500.0, R, tags={"farm"}, cls="flat",
                      ok_with={"flat", "none", "veg", "wire"})
    for c, gp in farms:
        for aid in ("drs_hay_bale_stack", "drs_hay_bale_stack", "drs_cart", "drs_woodpile", "drs_well"):
            b.try_place(aid, c[0] + R.uniform(-18, 18), c[1] + R.uniform(-18, 18), R.uniform(0, 360), R, tags={"farm"})
    rep["orchardTrees"] = norch

    # ------------------------------------------------------------------ 10. town greenery between the buildings
    in_town = lambda x, z: town(x, z) and not in_poly((x, z), rect(*ENCL)) and not in_poly((x, z), rect(*YARD))
    b.scatter([("tree_oak", 3), ("tree_ash", 3), ("tree_apple", 3), ("tree_poplar", 1)], in_town, 1 / 330.0, R,
              bounds=(-HX, -HZ, HX, HZ), tags={"town_green"}, min_dist=6.0)
    b.scatter([("shrub_hazel", 3), ("shrub_bramble", 1), ("nat_hawthorn_bush", 2), ("nat_blackthorn_low", 1), ("nat_fern_clump", 1)],
              in_town, 1 / 170.0, R, bounds=(-HX, -HZ, HX, HZ), tags={"town_green"}, cls="veg")
    b.scatter([("nat_flowers_cowparsley", 1), ("nat_tallgrass_verge", 2), ("nat_flowers_poppy", 1), ("nat_green_tufts", 2)], in_town,
              1 / 450.0, R, bounds=(-HX, -HZ, HX, HZ), tags={"town_green"}, cls="flat", ok_with={"flat", "none", "veg", "wire"})
    # stream banks: willows are not in the catalogue; ash and alder-like hazel along the water
    b.scatter([("tree_ash", 2), ("shrub_hazel", 2), ("nat_fern_clump", 1)], lambda x, z: 7.0 < polyline_dist((x, z), stream)[0] < 13.0,
              1 / 120.0, R, bounds=(-HX, -HZ, HX, HZ), avoid_routes=False, tags={"stream_bank"}, cls="veg",
              accept=lambda x, z: 0.0 if any(e and math.hypot(x - e["pos"][0], z - e["pos"][2]) < 18 for e in bridges.values()) else 1.0)

    # ------------------------------------------------------------------ 11. decor ring: trees in the margin band
    nt = 0
    for _ in range(2600):
        x, z = R.uniform(-HX - 86, HX + 86), R.uniform(-HZ - 86, HZ + 86)
        if abs(x) < HX + 6 and abs(z) < HZ + 6:
            continue
        nt += decor(R.choice(decor_trees + ["shrub_hazel"]), x, z, R.uniform(0, 360)) is not None
        if nt >= 420:
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
                         for i, o in enumerate(sc.objectives)})
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
