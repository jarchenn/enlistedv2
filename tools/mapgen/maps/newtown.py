"""Newtown (BR4): a modern town for the Battle Royale mode, 350 x 400 m, nine landing districts in a 3 x 3 grid.

    NW 1 Military base   | N 2 Industrial  | NE 3 Rural outskirts
    W  4 Residential     | C 5 Downtown    | E  6 Commercial strip
    SW 7 Construction    | S 8 Civic       | SE 9 Lakeside

Rolling ground with the base on a low hill and the lake in the south-east (template waterLevel). A ring road round
downtown links the outer districts; the two main streets cross downtown N-S and E-W, bent and stopped at the square
(no map-long firing lanes). A shallow drainage channel runs from the base culvert through the industrial yards and
down the commercial strip into the lake, with road bridges, footbridges, maintenance steps and paths. Roads,
sidewalks, fences and hedges are MOD1 lines. Every enterable building carries extra.br = {size, district[, military]}
for the loot spawner; scenario.districts lists the district names and centres; scenario rules mode "royale" (BR1),
with TDM team spawns (Solo spreads its own spawns). Uses the MOD1 modern set plus tools/catalog_newtown_{a,b,c}.mjs.

    <python> -I tools/mapgen/build.py newtown

Writes content/templates/newtown.{json,height.png,nmap.json} and the "Newtown" column of
docs/asset-usage-default-maps.md. The thumbnail comes from --render-map-thumbs (NMK_MAPS_ONLY=newtown)."""
import math
import os

from mapgen import (GroundPaint, Grid, Frame, MapBuilder, ScenarioBuilder, stamp_valley, stamp_flatten_poly, catmull_rom,
                    write_map, write_template, write_summary, asset_usage, rect, rng, point_at, polyline_length,
                    polyline_dist, offset_polyline, in_poly, asset_yaw_facing, asset_yaw_along, look_yaw, local_to_world,
                    fbm, circle_poly, dist, resample, smoothstep, lerp, clamp)
from mapgen.catalog import Asset
from maps.backrooms import update_usage_doc

TID, NAME, VERSION, SEED = "newtown", "Newtown", 1, 2026
STYLE = "abandoned"   # AS1: "abandoned" = the ruined town (<id>_ruin assets, wrecks, rubble, weeds; extra.look.style); "" = clean
W, D, MARGIN = 350, 400, 100
HX, HZ = W / 2, D / 2
WATER = 6.0

DISTRICTS = {1: ("Military base", (-128.0, -150.0)), 2: ("Industrial", (0.0, -152.0)), 3: ("Rural outskirts", (132.0, -150.0)),
             4: ("Residential", (-130.0, 0.0)), 5: ("Downtown", (0.0, 0.0)), 6: ("Commercial strip", (136.0, 0.0)),
             7: ("Construction", (-132.0, 150.0)), 8: ("Civic centre", (0.0, 150.0)), 9: ("Lakeside", (134.0, 152.0))}
HILL = (-131.0, -150.0)
COMPOUND = (-166.0, -190.0, -96.0, -110.0)           # x0, z0, x1, z1 of the base walls
LAKE = (134.0, 150.0, 24.0, 22.0)                    # centre x, z, radius x, z
SQUARE = (-24.0, -20.0, 24.0, 20.0)
DECOR = {"beautify": True, "decor": True}
TREES = ["tree_oak", "tree_ash", "tree_poplar", "tree_oak", "tree_ash"]

# new BR4 assets (tools/catalog_newtown_{a,b,c}.mjs): footprints, for NT_STUB dry runs before the catalogs are built
NEW = {"mod_nt_barracks": (30, 12, 2), "mod_nt_armory": (16, 12, 1), "mod_nt_hq": (20, 14, 2), "mod_nt_motor_pool": (24, 14, 1),
       "mod_nt_radar_tower": (6, 6, 3), "mod_nt_compound_wall": (6, 0.5, 0), "mod_nt_compound_wall_breach": (6, 3, 0),
       "mod_nt_compound_wall_culvert": (6, 3, 0), "mod_nt_compound_gate": (14, 6, 0), "mod_nt_training_wall": (3, 1, 0),
       "mod_nt_training_frame": (6, 3, 0), "mod_nt_crawl_frame": (6, 3, 0), "mod_nt_warehouse": (36, 24, 1), "mod_nt_workshop": (18, 12, 1),
       "mod_nt_storage_tank": (6, 6, 0), "mod_nt_rail_track": (12, 4, 0), "mod_nt_rail_buffer": (4, 4, 0), "mod_nt_boxcar": (15, 3.2, 0),
       "mod_nt_flatcar": (15, 3, 0),
       "mod_nt_hotel": (24, 16, 5), "mod_nt_parking_garage": (40, 30, 3), "mod_nt_clock_tower": (8, 8, 4), "mod_nt_shop_small": (10, 12, 1),
       "mod_nt_shop_row": (24, 12, 2), "mod_nt_cafe": (12, 10, 1), "mod_nt_car_dealership": (30, 20, 1), "mod_nt_motel": (36, 12, 2),
       "mod_nt_fire_station": (26, 18, 2), "mod_nt_municipal": (30, 18, 3), "mod_nt_covered_passage": (6, 16, 0), "mod_nt_kiosk": (3, 3, 0),
       "mod_nt_ambulance": (2.3, 6.2, 0), "mod_nt_fire_engine": (2.6, 9.5, 0),
       "mod_nt_house_small": (10, 9, 1), "mod_nt_house_two": (12, 10, 2), "mod_nt_house_split": (14, 11, 2), "mod_nt_waterfront_house": (14, 12, 2),
       "mod_nt_farmhouse": (16, 12, 2), "mod_nt_barn_steel": (20, 14, 1), "mod_nt_construction_frame": (28, 14, 3),
       "mod_nt_construction_frame_small": (14, 10, 2), "mod_nt_site_office": (7, 3, 1), "mod_nt_tower_crane": (6, 6, 0),
       "mod_nt_excavation_shoring": (8, 2, 0), "mod_nt_boathouse": (12, 16, 1), "mod_nt_pier": (4, 24, 0), "mod_nt_pontoon_finger": (2, 10, 0),
       "mod_nt_boat_motor": (2.4, 6.5, 0), "mod_nt_boat_trailer": (2.6, 7.5, 0), "mod_nt_promenade_rail": (6, 0.6, 0),
       "mod_nt_pump_station": (14, 10, 1), "mod_nt_road_bridge": (12, 10, 0), "mod_nt_footbridge": (3, 12, 0), "mod_nt_channel_steps": (3, 6, 0)}
ENTERABLE_PROPS = {"mod_nt_compound_gate", "mod_nt_covered_passage", "mod_nt_kiosk"}   # not loot buildings


def district_of(x, z):
    col = 0 if x < -86 else 2 if x > 88 else 1
    row = 0 if z < -98 else 2 if z > 96 else 1
    return row * 3 + col + 1


# ------------------------------------------------------------------------------------------------ geometry
def rounded_rect(x0, z0, x1, z1, r, n=5):
    pts = []
    for cx, cz, a0 in ((x1 - r, z0 + r, -90), (x1 - r, z1 - r, 0), (x0 + r, z1 - r, 90), (x0 + r, z0 + r, 180)):
        for k in range(n + 1):
            a = math.radians(a0 + 90.0 * k / n)
            pts.append((cx + r * math.cos(a), cz + r * math.sin(a)))
    return pts + [pts[0]]


def geometry():
    g = {}
    g["ring"] = rounded_rect(-84, -96, 84, 96, 24)
    # main streets: bent, stopped at the square (pedestrian), offset across it
    g["ns_n"] = catmull_rom([(10, -200), (12, -172), (2, -132), (-6, -96)], 8)
    g["ns_n2"] = catmull_rom([(-6, -96), (-11, -62), (-6, -36), (-5, -21)], 8)
    g["ns_s2"] = catmull_rom([(11, 21), (16, 50), (8, 96)], 8)
    g["ns_s"] = catmull_rom([(8, 96), (4, 130), (-4, 168), (0, 200)], 8)
    g["ew_w"] = catmull_rom([(-175, 4), (-140, -1), (-110, -8), (-84, -6)], 8)
    g["ew_w2"] = catmull_rom([(-84, -6), (-55, -2), (-25, -7)], 8)
    g["ew_e2"] = catmull_rom([(25, 12), (55, 17), (84, 12)], 8)
    g["ew_e"] = catmull_rom([(84, 12), (110, 19), (140, 11), (175, 15)], 8)
    # district streets
    g["base_rd"] = catmull_rom([(-130, -107), (-122, -100), (-100, -94), (-80, -86)], 8)              # base gate to the ring
    g["service"] = catmull_rom([(-93, -178), (-70, -160), (-40, -156), (2, -154), (40, -156), (70, -152), (98, -150), (130, -148), (168, -150)], 8)
    g["rural_ln"] = catmull_rom([(70, -84), (92, -104), (112, -122), (118, -148)], 8)
    g["res_a"] = catmull_rom([(-150, -88), (-152, -40), (-148, 0), (-150, 40), (-147, 88)], 8)
    g["res_b"] = catmull_rom([(-112, -90), (-110, -45), (-112, 0), (-110, 45), (-112, 88)], 8)
    g["res_n"] = catmull_rom([(-152, -90), (-130, -93), (-112, -91), (-90, -88), (-79, -88)], 8)
    g["con_rd"] = catmull_rom([(-79, 88), (-92, 102), (-120, 104), (-150, 104), (-168, 100)], 8)
    g["com_lot"] = catmull_rom([(84, -22), (100, -22), (110, -21)], 6)
    g["com_alley"] = catmull_rom([(106, -86), (130, -88), (170, -86)], 6)
    g["com_back"] = catmull_rom([(108, 46), (130, 47), (170, 46)], 6)
    g["lake_rd"] = catmull_rom([(84, 60), (96, 94), (100, 110)], 6)
    # the drainage channel: base culvert -> industrial -> down the commercial strip -> the lake (north shore)
    g["channel"] = catmull_rom([(-93, -122), (-60, -117), (-20, -118), (20, -115), (50, -110), (76, -98), (92, -82), (96, -55),
                                (96, -20), (96, 20), (98, 55), (104, 88), (116, 112), (126, 128)], 8)
    lx, lz, rx, rz = LAKE
    g["promenade"] = [(lx + (rx + 11) * math.cos(a), lz + (rz + 10) * math.sin(a)) for a in
                      [2 * math.pi * k / 48 for k in range(49)]]
    g["rail"] = [(-74, -194), (74, -194)]
    return g


ROADS = ["ring", "ns_n", "ns_n2", "ns_s2", "ns_s", "ew_w", "ew_w2", "ew_e2", "ew_e", "base_rd", "service", "rural_ln", "res_a", "res_b",
         "res_n", "con_rd", "com_lot", "com_alley", "com_back", "lake_rd"]
DOWNTOWN_ST = ["ns_n2", "ns_s2", "ew_w2", "ew_e2"]


def lake_k(x, z, wob=True):
    lx, lz, rx, rz = LAKE
    a = math.atan2(z - lz, x - lx)
    w = 1.0 + (0.08 * math.sin(3 * a + 0.7) + 0.05 * math.sin(5 * a + 2.1) if wob else 0.0)
    return math.hypot((x - lx) / rx, (z - lz) / rz) / w


# ------------------------------------------------------------------------------------------------ terrain
def base_height(x, z):
    h = 12.0 + 3.2 * fbm(x / 120.0, z / 120.0, 7, 4) + 1.1 * fbm(x / 45.0, z / 45.0, 11, 3)
    h -= 0.012 * (x + z)                                                     # falls gently toward the lake (SE)
    dc = math.hypot(x, z)
    h = lerp(12.0 - 0.012 * (x + z) + 0.6 * fbm(x / 90.0, z / 90.0, 5, 3), h, smoothstep(70, 130, dc))   # downtown calmer
    h += 7.5 * math.exp(-((x - HILL[0]) ** 2 + (z - HILL[1]) ** 2) / (2 * 52.0 ** 2))   # the base hill
    h += 2.5 * math.exp(-((x - 150) ** 2 + (z + 120) ** 2) / (2 * 30.0 ** 2))          # a rural rise
    h += 1.8 * math.exp(-((x + 40) ** 2 + (z - 175) ** 2) / (2 * 26.0 ** 2))           # civic rise
    return h


def stamp_road(grid, pts, half, fall=7.0):
    """Ease the ground under a road to a smoothed profile along it (no cross slope, gentle grades)."""
    rs = resample(pts, 3.0)
    hs = [grid.sample(*p) for p in rs]
    for _ in range(3):                                                       # box-filter the profile (about 30 m)
        hs = [sum(hs[max(0, i - 5):i + 6]) / len(hs[max(0, i - 5):i + 6]) for i in range(len(hs))]
    L = polyline_length(rs)
    xs = [p[0] for p in pts]
    zs = [p[1] for p in pts]
    pad = half + fall + 4

    def fn(x, z, h):
        d, s, _ = polyline_dist((x, z), rs)
        if d >= half + fall:
            return h
        t = clamp(s / 3.0, 0, len(hs) - 1.001)
        i = int(t)
        target = lerp(hs[i], hs[i + 1], t - i)
        w = 1.0 if d <= half else 1.0 - smoothstep(half, half + fall, d)
        return lerp(h, target, w)
    grid.apply(fn, (min(xs) - pad, min(zs) - pad, max(xs) + pad, max(zs) + pad))


def stamp_lake(grid):
    lx, lz, rx, rz = LAKE

    def fn(x, z, h):
        k = lake_k(x, z)
        if k >= 1.45:
            return h
        if k < 1.0:
            return lerp(WATER - 2.6, WATER + 0.5, smoothstep(0.55, 1.0, k))
        return lerp(WATER + 0.6, h, smoothstep(1.0, 1.45, k))
    grid.apply(fn, (lx - rx * 1.8, lz - rz * 1.8, lx + rx * 1.8, lz + rz * 1.8))


def stamp_pit(grid, cx, cz, hx, hz, yaw, depth, ramp_side=1):
    """An excavation: a flat-floored pit with steep sides and a ramp out on the local +x side."""
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    top = grid.sample(cx, cz)

    def fn(x, z, h):
        dx, dz = x - cx, z - cz
        lx, lz = dx * c - dz * s, dx * s + dz * c
        ex = abs(lx) - hx if lx * ramp_side < 0 else (abs(lx) - hx) / 4.0   # the ramp side eases 4x longer
        e = max(ex, abs(lz) - hz)
        if e >= 2.5:
            return h
        t = smoothstep(-0.5, 2.5, e)
        return min(h, lerp(top - depth, h, t))
    r = max(hx, hz) * 2 + 14
    grid.apply(fn, (cx - r, cz - r, cx + r, cz + r))


def channel_floor(grid, path):
    L = polyline_length(path)
    n = 200
    raw = [grid.sample(*point_at(path, L * i / n)[0]) - 1.9 for i in range(n + 1)]
    fl, m = [], 1e9
    for i, v in enumerate(raw):
        m = min(m, v)
        u = i / n
        fl.append(max(WATER + 0.25, lerp(m, WATER + 0.25, smoothstep(0.86, 1.0, u))))   # dry to the shore (only the lake holds water)
    return lambda u: fl[min(n, max(0, int(round(u * n))))]


# ------------------------------------------------------------------------------------------------ layout
class Deck:
    def __init__(self, pool, R):
        self.pool, self.R, self.cards = pool, R, []

    def peek(self, k):
        while len(self.cards) <= k:
            c = [a for a, n in self.pool for _ in range(n)]
            self.R.shuffle(c)
            self.cards += c
        return self.cards[k]

    def take(self, k):
        self.peek(k)
        return self.cards.pop(k)


def populate(ctx, grid, pads=None):
    R = rng(SEED)
    cat = ctx.catalog
    b = MapBuilder(cat, grid, Frame(W, D), seed=SEED, water_level=WATER)
    if STYLE == "abandoned":   # AS1: every placement goes through the abandoned substitutions
        RW = rng(SEED + 7)
        _place = b.place
        PCARS = ["compact", "hatchback", "minivan", "offroad", "pickup", "sedan", "sport", "suv", "wagon"]   # NT2: the user's car pack
        WRECKS = ["mod_pcar_%s_wreck" % k for k in PCARS] + ["mod_pcar_%s" % k for k in PCARS] + ["mod_sedan_burned", "mod_car_wreck_sedan", "mod_car_shell_burned"]
        INTACT = {"mod_sedan", "mod_sedan_red", "mod_hatchback", "mod_suv", "mod_pickup", "mod_delivery_van"}

        def place_r(aid, *a, **k):
            if aid in INTACT:
                aid = RW.choice(["mod_pcar_minivan", "mod_pcar_minivan_wreck", "mod_van_burned"]) if aid == "mod_delivery_van" else RW.choice(WRECKS)
            elif aid + "_ruin" in cat:
                aid = aid + "_ruin"
            return _place(aid, *a, **k)
        b.place = place_r
    g = geometry()
    paint = GroundPaint()
    b.paint = paint
    rep = {"missed": {}, "lines": {}}
    lines_made = []
    seedc = [1]

    def line(tid, pts, spans=None, tags=()):
        ln = {"pts": [[round(p[0], 3), round(p[1], 3)] for p in pts], "seed": (seedc[0] * 2654435761) % 4294967296}
        seedc[0] += 1
        if spans:
            ln["spans"] = sorted(spans, key=lambda s: s["s"])
        e = b.place("line_" + tid, pts[0][0], pts[0][1], 0.0, check=False, cls="none", extra={"line": ln}, tags={"line", tid} | set(tags),
                    y=b.ground(pts[0][0], pts[0][1]))
        rep["lines"][tid] = rep["lines"].get(tid, 0) + 1
        lines_made.append(e)
        return e

    def a_floors(aid):
        return int(cat[aid].raw.get("floors", 1) or 0)

    def clear(aid, x, z, yaw, pad=0.8):
        a = cat[aid]
        hx, hz = a.half[0] + pad, a.half[1] + pad
        nx, nz = max(2, int(math.ceil(2 * hx / 3.0)) + 1), max(2, int(math.ceil(2 * hz / 3.0)) + 1)
        for i in range(nx):
            for j in range(nz):
                wx, wz = local_to_world((x, z), yaw, -hx + 2 * hx * i / (nx - 1), -hz + 2 * hz * j / (nz - 1))
                if not grid.in_build(wx, wz) or b.in_keepout(wx, wz) or b.ground(wx, wz) < WATER + 0.3:
                    return False
        return True

    def tag_br(e, aid, dist_id, military=False):
        a = cat[aid]
        if not a.enterable or aid in ENTERABLE_PROPS:
            return
        area = a.fw * a.fd
        br = {"size": "large" if area >= 280 or a_floors(aid) >= 3 else "small", "district": dist_id}
        if military:
            br["military"] = True
        e["extra"]["br"] = br
        e["extra"]["loot"] = dict(br)   # BR2 (src/loot.cpp lootBegin) reads extra.loot; extra.br as the BR4 brief named it

    def site(aid, x, z, yaw, tags=(), pad=0.8, nudge=0, keep=True, military=False, extra=None, check=True):
        """A building or large fixture: keep-outs and overlaps checked, nudged a few times; tagged for BR2."""
        if aid not in cat:
            rep["missed"][aid] = rep["missed"].get(aid, 0) + 1
            return None
        why = ""
        for k in range(nudge + 1):
            px, pz = (x, z) if k == 0 else (x + R.uniform(-1, 1) * min(8, 2 + k), z + R.uniform(-1, 1) * min(8, 2 + k))
            if keep and not clear(aid, px, pz, yaw, pad):
                why = why or "keepout/water/border"
                continue
            o = b.overlaps(cat[aid], px, pz, yaw, {"flat", "none"})
            e = b.place(aid, px, pz, yaw, tags=set(tags) | {"building"}, extra=extra, check=check)
            if e is None:
                why = why or ("overlap " + (o["asset"] if o else "?"))
                continue
            tag_br(e, aid, district_of(px, pz), military)
            if pads is not None and cat[aid].enterable:
                pads.append((aid, px, pz, e["yaw"]))
            return e
        rep["missed"]["%s@%d,%d %s" % (aid, x, z, why)] = 1
        return None

    def prop(aid, x, z, yaw, tags=(), **kw):
        if aid not in cat:
            rep["missed"][aid] = rep["missed"].get(aid, 0) + 1
            return None
        return b.place(aid, x, z, yaw, tags=set(tags), **kw)

    def tprop(aid, x, z, yaw, tags=(), **kw):
        if aid not in cat:
            rep["missed"][aid] = rep["missed"].get(aid, 0) + 1
            return None
        return b.try_place(aid, x, z, yaw, R, tags=set(tags), **kw)

    def frontage(street, half, side, s0, s1, pool, tags=(), setback=(1.5, 3.0), gap=(1.0, 4.0), yaw_j=2.0, region=None):
        deck = Deck(pool, R)
        out = []
        L = polyline_length(street)
        s, s1 = s0, min(s1, L)
        while s < s1:
            done = None
            for k in range(5):
                aid = deck.peek(k)
                if aid not in cat:
                    deck.take(k)
                    rep["missed"][aid] = rep["missed"].get(aid, 0) + 1
                    continue
                a = cat[aid]
                sm = s + a.fw * 0.5
                if sm + a.fw * 0.5 > s1:
                    continue
                (px, pz), t = point_at(street, sm)
                nx, nz = -t[1] * side, t[0] * side
                off = half + R.uniform(*setback) + a.fd * 0.5
                x, z = px + nx * off, pz + nz * off
                yaw = asset_yaw_facing(-nx, -nz) + R.uniform(-yaw_j, yaw_j)
                if (region and not region(x, z)) or not clear(aid, x, z, yaw):
                    continue
                e = b.place(aid, x, z, yaw, tags=set(tags) | {"building"})
                if e:
                    deck.take(k)
                    tag_br(e, aid, district_of(x, z))
                    if pads is not None and a.enterable:
                        pads.append((aid, x, z, e["yaw"]))
                    out.append(e)
                    done = a
                    break
            s += (done.fw + R.uniform(*gap)) if done else 3.0
        return out

    def street_side(pts, s, side, off):
        (px, pz), t = point_at(pts, s)
        return (px - t[1] * side * off, pz + t[0] * side * off), t

    # ------------------------------------------------------------------ 1. keep-outs: roads, channel, lake, square, compound
    for k in ROADS:
        b.keepout_line(g[k], 5.2)
    for k in DOWNTOWN_ST:
        b.keepout_line(g[k], 7.0)                       # road + sidewalks
    b.keepout_line(g["channel"], 6.5)
    b.keepout_poly(rect(*SQUARE))
    b.keepout_line(g["promenade"], 2.6)
    b.keepout_line(g["rail"], 3.0)
    tdm = {"A": (-131.0, -62.0), "B": (128.0, -22.0)}
    for p in tdm.values():
        b.keepout_poly(circle_poly(p[0], p[1], 7.0))

    # ------------------------------------------------------------------ 2. roads, sidewalks, bridges over the channel
    chan = g["channel"]
    bridges = []

    def crossings(pts):
        out, L, s, prev = [], polyline_length(pts), 0.0, None
        while s <= L:
            d = polyline_dist(point_at(pts, s)[0], chan)[0]
            if prev is not None and prev[0] < 6.0 and d > prev[0] and (not out or s - out[-1] > 15) and prev[0] < 2.5:
                out.append(prev[1])
            prev = (d, s)
            s += 1.0
        return out
    for k in ROADS:
        pts = g[k]
        spans = []
        for s in crossings(pts):
            (px, pz), t = point_at(pts, s)
            ya = asset_yaw_facing(*t)
            hb = max(b.ground(px - t[0] * 6.5, pz - t[1] * 6.5), b.ground(px + t[0] * 6.5, pz + t[1] * 6.5))
            br = prop("mod_nt_road_bridge", px, pz, ya, tags={"bridge"}, check=False, y=hb, cls="none")
            if br:
                bridges.append(br)
            spans.append({"s": round(s, 3), "w": 10.6, "kind": "gap"})
        line("mod_road_2lane", pts, spans, tags={"road", k})
    for k in DOWNTOWN_ST:
        for side in (1, -1):
            line("mod_sidewalk", offset_polyline(g[k], 5.3 * side), tags={"sidewalk"})
    sq = rect(*SQUARE)
    line("mod_sidewalk", [sq[0], sq[1], sq[2], sq[3], sq[0]], tags={"sidewalk"})
    rep["roadBridges"] = len(bridges)

    # channel: footbridges, maintenance steps, paths along it, culvert headwalls at the head
    Lc = polyline_length(chan)
    nfb = 0
    for s in (70.0, 150.0, 235.0, 300.0, 345.0):
        if s > Lc - 20:
            continue
        (px, pz), t = point_at(chan, s)
        if any(math.hypot(px - e["pos"][0], pz - e["pos"][2]) < 25 for e in bridges):
            continue
        yaw = asset_yaw_facing(-t[1], t[0])
        hb = max(b.ground(px - t[1] * 7, pz + t[0] * 7), b.ground(px + t[1] * 7, pz - t[0] * 7))
        nfb += prop("mod_nt_footbridge", px, pz, yaw, tags={"footbridge"}, check=False, y=hb, cls="none") is not None
    rep["footbridges"] = nfb
    for s in (40.0, 120.0, 200.0, 270.0):
        if s > Lc - 25:
            continue
        (px, pz), t = point_at(chan, s)
        side = 1 if (s // 40) % 2 else -1
        q = (px - t[1] * side * 3.6, pz + t[0] * side * 3.6)
        prop("mod_nt_channel_steps", q[0], q[1], asset_yaw_facing(t[1] * side, -t[0] * side), tags={"channel"}, check=False, cls="none",
             y=b.ground(px - t[1] * side * 6.5, pz + t[0] * side * 6.5))
    for side in (1, -1):
        mp = offset_polyline(chan, 7.6 * side)
        b.route("footpath", mp, 2.2, keepout=False)
    hx0, hz0 = chan[0]
    t0 = point_at(chan, 2.0)[1]
    prop("mod_culvert_headwall", hx0 + t0[0] * 3, hz0 + t0[1] * 3, asset_yaw_facing(-t0[0], -t0[1]), tags={"channel"}, check=False)
    paint.line("mud", chan, 6.0, weight=0.75, edge=1.5)

    # ------------------------------------------------------------------ 3. District 1: military base on the hill
    x0, z0, x1, z1 = COMPOUND
    mil = {"military": True}
    gate_x, rear_z, breach_z, culv_z = -130.0, -178.0, -150.0, -122.0
    walls = []
    for pts, gaps in (([(x0, z1), (gate_x - 7, z1)], []), ([(gate_x + 7, z1), (x1, z1)], []),                  # south, either side of the gate
                      ([(x1, z1), (x1, z0)], [(z1 - culv_z - 3, z1 - culv_z + 3), (z1 - rear_z - 4.2, z1 - rear_z + 4.2)]),   # east
                      ([(x1, z0), (x0, z0)], []),                                                                # north
                      ([(x0, z0), (x0, z1)], [(breach_z - z0 - 3, breach_z - z0 + 3)])):                         # west
        walls += b.chain("mod_nt_compound_wall", pts, along="x", gaps=gaps, tags={"compound"}, check=False) if "mod_nt_compound_wall" in cat else []
    rep["wallBays"] = len(walls)
    gate = site("mod_nt_compound_gate", gate_x, z1, 0.0, tags={"compound", "gate"}, keep=False, military=True, check=False)
    prop("mod_sliding_gate", x1, rear_z, 90.0, tags={"compound", "gate"}, check=False)
    prop("mod_nt_compound_wall_breach", x0, breach_z, 90.0, tags={"compound", "breach"}, check=False)
    prop("mod_nt_compound_wall_culvert", x1, culv_z, 90.0, tags={"compound", "culvert"}, check=False)
    paint.poly("gravel", rect(x0 + 1, z0 + 1, x1 - 1, z1 - 1), weight=0.45, edge=2.0, ragged=2.0, seed=11)
    paint.line("gravel", [(gate_x, z1), (gate_x, -150), (-118, -138), (-104, -138)], 6.0, weight=0.85)
    for aid, x, z, yaw in (("mod_nt_barracks", -148.0, -177.0, 0.0), ("mod_nt_barracks", -148.0, -156.0, 0.0), ("mod_nt_hq", -112.0, -178.0, 0.0),
                           ("mod_nt_armory", -112.0, -156.0, 270.0), ("mod_mil_hut", -151.0, -125.0, 0.0), ("mod_nt_radar_tower", -104.0, -130.0, 0.0)):
        site(aid, x, z, yaw, tags={"base"}, keep=False, nudge=4, military=True)
    # training yard: walls, frames, crawl frames, sandbags
    for aid, x, z, yaw in (("mod_nt_training_wall", -134.0, -140.0, 0.0), ("mod_nt_training_wall", -128.0, -136.0, 10.0), ("mod_nt_training_frame", -122.0, -142.0, 0.0),
                           ("mod_nt_crawl_frame", -132.0, -128.0, 90.0), ("mod_nt_crawl_frame", -126.0, -128.0, 90.0), ("mod_sandbag_position", -117.0, -124.0, 180.0)):
        tprop(aid, x, z, yaw, tags={"base", "training"})
    paint.poly("dirt", rect(-138, -146, -116, -122), weight=0.85, edge=2.0, ragged=1.5, seed=12)
    for x, z, yaw in ((-161.0, -185.0, 45.0), (-101.0, -185.0, -45.0), (-161.0, -115.0, 135.0)):
        tprop("mod_guard_tower", x, z, yaw, tags={"base", "cover"})
    for x in (-140.0, -120.0):                                       # HESCO chicane and sandbags inside the gate
        for k in range(3):
            tprop("mod_hesco_2x1", x + (k - 1) * 2.3, -118.0, 0.0, tags={"base", "cover"})
    tprop("mod_fighting_position_oh", -142.0, -114.5, 180.0, tags={"base", "cover"})
    for aid, x, z, yaw in (("mod_mil_utility", -138.0, -120.0, 90.0), ("mod_mil_utility_green", -138.0, -127.0, 90.0), ("mod_apc_wheeled", -137.0, -134.0, 90.0),
                           ("mod_container_20", -160.0, -142.0, 0.0), ("mod_container_40", -100.0, -146.0, 90.0), ("mod_camo_net", -126.0, -168.0, 0.0),
                           ("mod_ammo_box_stack", -105.0, -163.0, 0.0), ("mod_mil_generator", -120.0, -186.0, 0.0), ("mod_antenna_mast", -110.0, -168.0, 0.0),
                           ("mod_weapon_rack", -128.0, -170.0, 0.0), ("mod_hard_cases", -124.0, -165.0, 30.0), ("mod_jerry_cans", -140.0, -112.5, 0.0),
                           ("mod_hesco_3x1", -160.0, -134.0, 90.0), ("mod_hesco_3x1", -100.0, -120.0, 0.0), ("mod_barrel_group", -158.0, -112.5, 0.0)):
        tprop(aid, x, z, yaw, tags={"base"})
    # outside the walls: HESCO and a sandbag post at the gate, razor coil, the rear road
    for x in (-145.0, -115.0):
        tprop("mod_hesco_3x1", x, -104.0, 0.0, tags={"base", "cover"})
    tprop("mod_razor_coil", -150.0, -106.0, 0.0, tags={"base"})
    tprop("mod_razor_coil", -110.0, -106.0, 0.0, tags={"base"})

    # ------------------------------------------------------------------ 4. District 2: industrial
    for aid, x, z, yaw in (("mod_nt_warehouse", -44.0, -177.0, 0.0), ("mod_nt_warehouse", 44.0, -177.0, 0.0),
                           ("mod_nt_workshop", -60.0, -138.0, 180.0), ("mod_nt_workshop", -16.0, -138.0, 180.0),
                           ("mod_nt_workshop", 30.0, -137.0, 180.0), ("mod_nt_workshop", 56.0, -136.0, 180.0)):
        site(aid, x, z, yaw, tags={"industrial"}, nudge=6)
    rail = g["rail"]
    b.chain("mod_nt_rail_track", rail, along="x", tags={"rail"}, check=False, cls="none") if "mod_nt_rail_track" in cat else None
    prop("mod_nt_rail_buffer", -76.0, -194.0, 270.0, tags={"rail"}, check=False)
    for aid, x in (("mod_nt_boxcar", -50.0), ("mod_nt_boxcar", -34.0), ("mod_nt_flatcar", -18.0), ("mod_nt_boxcar", 30.0), ("mod_nt_flatcar", 46.0)):
        prop(aid, x, -194.0, 0.0, tags={"rail", "cover"}, check=False, y=b.ground(x, -194.0))
    paint.line("gravel", rail, 5.5, weight=0.9)
    # container yard (west of the NS street, south of the service road) and stacks by the warehouses
    for k, (x, z) in enumerate([(-38.0, -134.0), (-38.0, -127.0), (-31.0, -134.0), (-31.0, -127.0), (-24.0, -129.0)]):
        tprop(["mod_container_40", "mod_container_40_stack", "mod_container_20", "mod_container_40_open", "mod_container_20_damaged"][k], x, z, 0.0,
              tags={"industrial", "containers", "cover"})
    for x, z, yaw in ((-68.0, -170.0, 0.0), (68.0, -170.0, 0.0), (14.0, -184.0, 90.0), (-74.0, -150.0, 90.0)):
        tprop(R.choice(["mod_container_40", "mod_container_20", "mod_container_40_stack"]), x, z, yaw, tags={"industrial", "cover"})
    for x, z in ((74.0, -134.0), (76.0, -124.0)):
        tprop("mod_nt_storage_tank", x, z, 0.0, tags={"industrial", "cover"})
    for aid in ("mod_pallet_stack", "mod_pallet_stack", "mod_ibc_tote", "mod_barrel_group", "mod_cable_spool_large", "mod_crate_wood", "mod_dumpster_rolloff",
                "mod_forklift" if "mod_forklift" in cat else "mod_tool_chest", "mod_box_truck", "mod_semi_trailer", "mod_semi_tractor", "mod_air_compressor",
                "mod_generator_set", "mod_crate_plastic_stack", "mod_barrel_steel_rust", "mod_skip"):
        x, z = R.uniform(-72, 72), R.choice([R.uniform(-166, -160), R.uniform(-150, -144), R.uniform(-130, -124)])
        tprop(aid, x, z, R.choice([0.0, 90.0]) + R.uniform(-8, 8), tags={"industrial"})
    paint.poly("gravel", rect(-74, -192, 74, -122), weight=0.35, edge=4.0, ragged=3.0, seed=21)
    for x, z, yaw in ((-90.0, -150.0, 0.0), (-80.0, -200.0, 0.0)):
        pass
    # chain-link round the warehouse yards (lines) with the road gaps left open
    for xa, xb in ((-72.0, -6.0), (24.0, 74.0)):
        line("mod_chainlink", [(xa, -165.0), (xa, -190.0)], tags={"fence"})
        line("mod_chainlink", [(xb, -190.0), (xb, -165.0)], tags={"fence"})

    # ------------------------------------------------------------------ 5. District 3: rural outskirts
    farm = (140.0, -176.0)
    site("mod_nt_farmhouse", 124.0, -178.0, 0.0, tags={"rural", "farm"}, nudge=4)
    site("mod_nt_barn_steel", 155.0, -180.0, 0.0, tags={"rural", "farm"}, nudge=4)
    for k in range(3):
        tprop("mod_greenhouse", 116.0 + k * 5.0, -162.0, 0.0, tags={"rural", "farm"})
    for aid, x, z in (("mod_pickup", 140.0, -165.0), ("mod_nt_boat_trailer", 165.0, -166.0), ("furn_hay_bales", 150.0, -165.0), ("drs_hay_bale_stack", 160.0, -164.0),
                      ("mod_ibc_tote", 132.0, -168.0), ("mod_raised_bed", 112.0, -170.0), ("mod_raised_bed", 112.0, -174.0), ("mod_tyre_pile", 168.0, -186.0)):
        tprop(aid, x, z, R.uniform(-20, 20), tags={"rural", "farm"})
    paint.poly("dirt", circle_poly(farm[0], farm[1] + 10, 18.0, 18), weight=0.8, edge=4.0, ragged=3.0, seed=31)
    ring_w = [(108, -158), (108, -195), (172, -195), (172, -158)]
    for a_, c_ in zip(ring_w, ring_w[1:]):
        b.chain("stone_wall_low", [a_, c_], along="x", tags={"rural", "wall"}, ok_with={"flat", "none", "veg"})
    b.chain("stone_wall_low", [(172, -158), (146, -158)], along="x", tags={"rural", "wall"}, ok_with={"flat", "none", "veg"})
    b.chain("stone_wall_low", [(134, -158), (108, -158)], along="x", tags={"rural", "wall"}, ok_with={"flat", "none", "veg"})
    for aid, x, z, yaw in (("mod_nt_house_small", 104.0, -138.0, 225.0), ("mod_nt_house_two", 160.0, -118.0, 180.0), ("mod_nt_house_small", 166.0, -136.0, 270.0),
                           ("mod_bld_bungalow", 140.0, -108.0, 180.0)):
        site(aid, x, z, yaw, tags={"rural"}, nudge=6)
    norch = 0
    for u in range(112, 170, 7):                                   # orchard rows between the farm and the houses
        for v in range(-146, -124, 7):
            if R.random() < 0.12:
                continue
            x, z = u + R.uniform(-0.8, 0.8), v + R.uniform(-0.8, 0.8)
            if not b.in_keepout(x, z):
                norch += prop("tree_apple", x, z, R.uniform(0, 360), tags={"orchard"}) is not None
    rep["orchardTrees"] = norch
    b.scatter([("nat_meadow_mixed", 3), ("nat_flowers_meadow", 1), ("nat_tallgrass_patch", 2)], lambda x, z: district_of(x, z) == 3, 1 / 180.0, R,
              bounds=(88, -200, 175, -98), tags={"rural"}, cls="flat", ok_with={"flat", "none", "veg", "wire"})

    # ------------------------------------------------------------------ 6. District 4: residential
    homes = [("mod_nt_house_small", 4), ("mod_nt_house_two", 4), ("mod_nt_house_split", 2), ("mod_bld_family_house", 2), ("mod_bld_bungalow", 2)]
    res = []
    res += frontage(g["ew_w"], 4.0, 1, 4.0, 60.0, [("mod_bld_apartment_block", 1), ("mod_bld_townhouse_row", 1)], tags={"residential"},
                    setback=(3.0, 4.0), gap=(5.0, 8.0))
    res += frontage(g["ew_w"], 4.0, -1, 4.0, 60.0, [("mod_bld_apartment_block", 1), ("mod_bld_townhouse_row", 1)], tags={"residential"},
                    setback=(3.0, 4.0), gap=(5.0, 8.0))
    for k in ("res_a", "res_b"):
        for side in (1, -1):
            res += frontage(g[k], 4.0, side, 4.0, 1e9, homes, tags={"residential"}, setback=(4.0, 6.0), gap=(4.0, 7.0))
    rep["residential"] = len(res)
    for e in res:                                                   # front fences / hedges (lines) beside the walk, a car on the drive
        if e["asset"] in ("mod_bld_apartment_block", "mod_bld_townhouse_row"):
            continue
        a = cat[e["asset"]]
        hx, hz = a.half
        x, z, yaw = e["pos"][0], e["pos"][2], e["yaw"]
        L = lambda lx, lz: local_to_world((x, z), yaw, lx, lz)
        tid = R.choice(["mod_fence_white_picket", "mod_hedge_modern", "mod_fence_white_picket", "mod_hedge_modern_tall"])
        line(tid, [L(-hx, hz + 2.6), L(-1.2, hz + 2.6)], tags={"yard"})
        line(tid, [L(1.2, hz + 2.6), L(hx - 3.5, hz + 2.6)], tags={"yard"})
        if R.random() < 0.6:
            prop(R.choice(["mod_sedan", "mod_hatchback", "mod_suv", "mod_pickup", "mod_sedan_red"]), *L(hx - 1.6, hz + 3.2), yaw + 90 * 0, tags={"yard"},
                 check=False)
        if R.random() < 0.5:
            tprop(R.choice(["mod_garden_shed", "mod_raised_bed", "mod_picnic_table", "mod_trash_bin"]), *L(R.uniform(-hx + 2, hx - 2), -hz - 3.0), yaw,
                  tags={"yard"})
        if R.random() < 0.6:
            tprop(R.choice(["mod_tree_ornamental", "mod_tree_blossom", "tree_ash"]), *L(-hx + 1.6, hz + 1.2), R.uniform(0, 360), tags={"yard"}, cls="veg")
    park = (-131.0, 64.0)
    for aid, x, z, yaw in (("mod_play_climbing_frame", -133.0, 52.0, 0.0), ("mod_play_swings_4", -124.0, 58.0, 90.0), ("mod_play_seesaw", -137.0, 60.0, 0.0),
                           ("mod_play_roundabout", -130.0, 62.0, 0.0), ("mod_play_spring_rider_twin", -126.0, 50.0, 0.0), ("mod_fountain_round", -131.0, 74.0, 0.0),
                           ("mod_basketball_half", -131.0, 30.0, 90.0), ("mod_gazebo", -138.0, 78.0, 0.0), ("mod_garden_bench", -124.0, 72.0, 270.0),
                           ("mod_garden_bench", -138.0, 70.0, 90.0), ("mod_picnic_table", -124.0, 80.0, 10.0), ("mod_play_sandpit", -136.0, 45.0, 0.0)):
        tprop(aid, x, z, yaw, tags={"park"})
    paint.poly("dirt", circle_poly(-131.0, 56.0, 9.0, 16), weight=0.4, edge=2.0, ragged=1.0, seed=41)
    b.scatter([("mod_tree_blossom", 1), ("mod_tree_ornamental", 2), ("tree_oak", 1)], circle_poly(park[0], park[1], 20.0, 20), 1 / 90.0, R,
              tags={"park"}, min_dist=6.0)

    # ------------------------------------------------------------------ 7. District 5: downtown
    ct = site("mod_nt_clock_tower", 0.0, -11.0, 0.0, tags={"downtown", "landmark"}, keep=False)
    prop("mod_fountain_tiered", 0.0, 8.0, 0.0, tags={"square"}, check=False)
    paint.poly("cobbles", rect(*SQUARE), weight=0.95, edge=0.6)
    if STYLE == "abandoned":   # NT2: the square gone to grass: moss and weeds in the joints, saplings, a tree in the paving
        paint.poly("dry_grass", rect(*SQUARE), weight=0.45, edge=3.0, ragged=4.0, seed=505)
        paint.poly("leaf_litter", circle_poly(-12.0, 10.0, 7.0, 14), weight=0.6, edge=2.5, ragged=2.0, seed=506)
        for _ in range(60):
            x, z = R.uniform(SQUARE[0] + 1, SQUARE[2] - 1), R.uniform(SQUARE[1] + 1, SQUARE[3] - 1)
            if abs(x) < 4 and -16 < z < 12:
                continue
            aid = R.choice(["nat_tallgrass_patch", "mod_weeds_clump", "nat_drygrass_tufts", "mod_weeds_crack", "nat_nettle_clump", "nat_meadow_mixed"])
            if aid in cat:
                b.place(aid, x, z, R.uniform(0, 360), check=False, cls="veg", tags={"square", "ruin_dress"})
        for x, z in ((-17.0, 7.0), (16.0, -9.0), (9.0, 15.0)):
            b.place("tree_ash", x, z, R.uniform(0, 360), check=False, cls="veg", tags={"square"})
    for k in range(8):
        a = 2 * math.pi * k / 8 + 0.4
        tprop("mod_planter_concrete", 15.0 * math.cos(a), 13.0 * math.sin(a), math.degrees(-a), tags={"square", "cover"})
        tprop("mod_bench", 9.0 * math.cos(a + 0.3), 8.5 * math.sin(a + 0.3) + 2.0, math.degrees(-(a + 0.3)) + 90, tags={"square"})
    for x, z in ((-20.0, -16.0), (20.0, -16.0), (-20.0, 16.0), (20.0, 16.0)):
        tprop("mod_tree_ornamental", x, z, 0.0, tags={"square"}, cls="veg")
        tprop("mod_street_lamp", x * 0.6, z * 0.75, 0.0, tags={"square"})
    tprop("mod_nt_kiosk", 14.0, -4.0, 270.0, tags={"square"})
    for aid, x, z, yaw in (("mod_nt_parking_garage", -48.0, -70.0, 90.0), ("mod_nt_hotel", 40.0, -5.0, 0.0), ("mod_bld_office_wide", -46.0, 17.0, 180.0),
                           ("mod_bld_office_setback", 45.0, 34.0, 180.0), ("mod_bld_tower_glass", 44.0, -64.0, 0.0), ("mod_bld_restaurant_family", -46.0, 58.0, 0.0),
                           ("mod_nt_shop_row", 46.0, 72.0, 0.0)):
        site(aid, x, z, yaw, tags={"downtown", "anchor"}, nudge=5)
    shops = [("mod_nt_shop_small", 3), ("mod_nt_shop_row", 1), ("mod_nt_cafe", 2), ("mod_bld_mixed_use", 2), ("mod_bld_diner_small", 1)]
    dt = []
    for k in DOWNTOWN_ST:
        for side in (1, -1):
            dt += frontage(g[k], 7.9, side, 2.0, 1e9, shops, tags={"downtown"}, setback=(0.3, 1.0), gap=(0.5, 2.5), yaw_j=0.0)
    # square frontage: shops facing the square across its edges
    sqp = [(SQUARE[0], SQUARE[1]), (SQUARE[2], SQUARE[1]), (SQUARE[2], SQUARE[3]), (SQUARE[0], SQUARE[3]), (SQUARE[0], SQUARE[1])]
    for k in range(4):
        dt += frontage([sqp[k], sqp[k + 1]], 1.4, -1, 0.0, 1e9, [("mod_nt_shop_small", 2), ("mod_nt_cafe", 1)], tags={"downtown", "square"},
                       setback=(0.2, 0.6), gap=(0.5, 1.5), yaw_j=0.0)
    # the ring's inner side
    dt += frontage(g["ring"], 6.2, 1, 0.0, 1e9, [("mod_nt_shop_small", 2), ("mod_bld_mixed_use", 2), ("mod_nt_cafe", 1), ("mod_bld_diner_small", 1)],
                   tags={"downtown", "ring"}, setback=(1.0, 2.5), gap=(3.0, 8.0), region=lambda x, z: abs(x) < 80 and abs(z) < 92)
    rep["downtown"] = len(dt)
    for k in DOWNTOWN_ST:                                           # street furniture and parked cars
        pts = g[k]
        L = polyline_length(pts)
        s = 6.0
        while s < L - 6:
            side = 1 if int(s / 9) % 2 else -1
            (q, t) = street_side(pts, s, side, 5.6)
            r = R.random()
            if r < 0.22:
                tprop(R.choice(["mod_streetlight", "mod_trash_bin", "mod_bench", "mod_fire_hydrant", "mod_newspaper_box", "mod_bike_rack",
                                "mod_parking_meter", "mod_mailbox"]), q[0], q[1], asset_yaw_along(*t), tags={"street"})
            elif r < 0.42:
                tprop(R.choice(["tree_ash", "mod_tree_ornamental"]), q[0], q[1], R.uniform(0, 360), tags={"street"}, cls="veg")
            elif r < 0.62:
                (c, _) = street_side(pts, s, side, 2.6)
                prop(R.choice(["mod_sedan", "mod_hatchback", "mod_suv", "mod_sedan_red", "mod_delivery_van", "mod_sedan_wrecked"]), c[0], c[1],
                     asset_yaw_facing(t[0] * side, t[1] * side), tags={"street", "cover"}, check=False, y=None)
            s += R.uniform(7.0, 11.0)
    for k, p in (("ns_n2", (-6.0, -96.0)), ("ns_s2", (8.0, 96.0)), ("ew_w2", (-84.0, -6.0)), ("ew_e2", (84.0, 12.0))):
        tprop("mod_traffic_light", p[0] + 6.0, p[1] + 6.0, 0.0, tags={"street"})
    for p in ((-18.0, -40.0), (30.0, 22.0)):
        tprop("mod_bus_shelter", p[0], p[1], 0.0, tags={"street"})
    # a few wrecks and a roadblock: cover in the open
    for aid, x, z, yaw in (("mod_roadblock", -60.0, -96.0, 0.0), ("mod_jersey_barrier", 32.0, 18.0, 10.0), ("mod_car_shell_burned", 60.0, 14.0, 80.0),
                           ("mod_jersey_barrier_damaged", -70.0, -4.0, 80.0), ("mod_water_barrier_red", 4.0, 60.0, 15.0)):
        tprop(aid, x, z, yaw, tags={"street", "cover"})

    # ------------------------------------------------------------------ 8. District 6: commercial strip
    for aid, x, z, yaw in (("mod_bld_bigbox", 143.0, -60.0, 0.0), ("mod_bld_gas_station", 158.0, -16.0, 0.0), ("mod_bld_diner_small", 116.0, 32.0, 180.0),
                           ("mod_bld_fastfood", 146.0, 30.0, 180.0), ("mod_nt_car_dealership", 122.0, 63.0, 0.0), ("mod_nt_motel", 150.0, 86.0, 180.0)):
        site(aid, x, z, yaw, tags={"commercial"}, nudge=6)
    lot = prop("mod_parking_large", 127.0, -20.0, 0.0, tags={"commercial", "lot"}, cls="flat")
    ncar = 0
    for row in (-34.0, -24.0, -14.0, -5.0):                          # cars in the bays (some gaps)
        for k in range(7):
            if R.random() < 0.45:
                continue
            x = 111.0 + k * 5.2 + R.uniform(-0.2, 0.2)
            ncar += prop(R.choice(["mod_sedan", "mod_hatchback", "mod_suv", "mod_pickup", "mod_sedan_red", "mod_sedan_wrecked", "mod_suv"]), x, row,
                         R.choice([0.0, 180.0]) + R.uniform(-4, 4), tags={"lot", "cover"}, check=False) is not None
    for x, z in ((112.0, -38.0), (140.0, -38.0)):
        tprop("mod_cart_corral", x, z, 90.0, tags={"lot"})
    prop("mod_parking_medium", 116.0, 78.0, 90.0, tags={"commercial", "lot"}, cls="flat")
    for k in range(5):
        if R.random() < 0.7:
            ncar += prop(R.choice(["mod_sedan", "mod_suv", "mod_hatchback", "mod_pickup"]), 104.0 + k * 5.0, 72.0, 0.0, tags={"lot", "cover"}, check=False) is not None
    rep["parkedCars"] = ncar
    for aid in ("mod_dumpster_frontload", "mod_dumpster_frontload", "mod_pallet_stack", "mod_crate_plastic_stack", "mod_semi_trailer", "mod_box_truck",
                "mod_skip", "mod_hvac_unit", "mod_ibc_tote", "mod_tyre_pile"):
        tprop(aid, R.uniform(112, 168), R.uniform(-84, -81) if R.random() < 0.6 else R.uniform(43, 50), 0.0, tags={"alley"})
    for x in (150.0, 156.0, 162.0):
        tprop(R.choice(["mod_sedan", "mod_pickup", "mod_hatchback"]), x, -12.0, 90.0, tags={"gas", "cover"})
    # NT2: the abandoned forecourt (user reference 56): worn ground, puddles, a wreck by the pumps, cans, barrels, rubble
    paint.poly("dirt", rect(140.0, -30.0, 176.0, 2.0), weight=0.55, edge=3.0, ragged=3.0, seed=561)
    paint.poly("gravel", rect(144.0, -6.0, 172.0, 4.0), weight=0.5, edge=2.0, ragged=2.5, seed=562)
    paint.poly("leaf_litter", circle_poly(166.0, -24.0, 6.0, 12), weight=0.6, edge=2.0, ragged=2.0, seed=563)
    for aid, x, z, yaw in (("mod_pcar_sedan_wreck", 153.0, -3.0, 25.0), ("mod_pcar_suv", 170.0, -4.0, 95.0), ("wat_puddles", 158.0, 0.0, 30.0),
                           ("mod_jerry_cans", 147.5, -8.0, 15.0), ("mod_barrel_steel_rust", 167.0, -9.0, 0.0), ("mod_barrel_group", 174.0, -20.0, 40.0),
                           ("mod_rubble_small", 161.0, 2.0, 70.0), ("mod_tyre_pile", 145.0, -22.0, 0.0), ("mod_rubble_small", 175.0, -9.0, 200.0),
                           ("mod_trash_bin", 149.0, -18.0, 90.0)):
        tprop(aid, x, z, yaw, tags={"gas", "cover"})
    for k in range(26):                                                  # weeds through every joint of the slab
        tprop(R.choice(["mod_weeds_crack", "mod_weeds_crack", "rub_debris_patch"]), R.uniform(142, 174), R.uniform(-26, 4), R.uniform(0, 360), tags={"gas"}, cls="veg")

    # ------------------------------------------------------------------ 9. District 7: construction
    for aid, x, z, yaw in (("mod_nt_construction_frame", -146.0, 120.0, 0.0), ("mod_nt_construction_frame", -112.0, 182.0, 180.0),
                           ("mod_nt_construction_frame_small", -160.0, 180.0, 90.0), ("mod_nt_construction_frame_small", -106.0, 122.0, 0.0),
                           ("mod_nt_site_office", -166.0, 140.0, 90.0), ("mod_nt_site_office", -158.0, 146.0, 0.0)):
        site(aid, x, z, yaw, tags={"construction"}, nudge=5)
    prop("mod_nt_tower_crane", -138.0, 166.0, 0.0, tags={"construction"}, check=False)
    for x0_ in (-140.0, -118.0):                                       # shoring along the excavation's long sides
        for zz in (144.0, 156.0):
            prop("mod_nt_excavation_shoring", x0_ + 6, zz, 0.0, tags={"construction", "cover"}, check=False)
    for aid, x, z, yaw in (("mod_excavator", -116.0, 140.0, 30.0), ("mod_concrete_pipe_stack", -150.0, 160.0, 0.0), ("mod_concrete_pipe", -126.0, 150.0, 90.0),
                           ("mod_lumber_stack", -164.0, 158.0, 0.0), ("mod_rebar_bundle", -152.0, 136.0, 0.0), ("mod_cinder_wall", -100.0, 150.0, 90.0),
                           ("mod_skip", -96.0, 166.0, 0.0), ("mod_portable_toilet", -168.0, 130.0, 90.0), ("mod_portable_toilet", -168.0, 132.0, 90.0),
                           ("mod_container_20", -160.0, 166.0, 90.0), ("mod_cable_spool_large", -132.0, 134.0, 0.0), ("mod_generator_set", -100.0, 136.0, 0.0),
                           ("mod_pallet_stack", -146.0, 136.0, 0.0), ("mod_box_truck", -96.0, 110.0, 90.0), ("mod_traffic_drum", -122.0, 106.0, 0.0)):
        tprop(aid, x, z, yaw, tags={"construction"})
    for k in range(10):                                               # scaffold along the frames' faces
        tprop("mod_scaffold_bay", -159.0 + k * 3.0, 128.5, 0.0, tags={"construction", "scaffold"})
    for pts in ([(-172, 128), (-172, 168)], [(-172, 168), (-150, 168)]):
        b.chain("mod_construction_fence", pts, along="x", tags={"construction", "fence"}, ok_with={"flat", "none", "veg"})
    paint.poly("dirt", rect(-172, 108, -92, 196), weight=0.7, edge=4.0, ragged=4.0, seed=71)
    paint.poly("mud", rect(-142, 142, -114, 158), weight=0.8, edge=2.0, ragged=2.0, seed=72)

    # ------------------------------------------------------------------ 10. District 8: civic
    for aid, x, z, yaw in (("mod_bld_hospital", -36.0, 156.0, 90.0), ("mod_bld_bus_station", -60.0, 118.0, 0.0), ("mod_nt_fire_station", 46.0, 120.0, 270.0),
                           ("mod_nt_municipal", 46.0, 168.0, 270.0), ("mod_nt_covered_passage", 46.0, 144.0, 0.0)):
        site(aid, x, z, yaw, tags={"civic"}, nudge=5)
    for k, (x, z) in enumerate(((-14.0, 146.0), (-14.0, 154.0), (-18.0, 164.0))):
        tprop("mod_nt_ambulance", x, z, 0.0 if k < 2 else 90.0, tags={"civic", "ambulance", "cover"})
    paint.poly("gravel", rect(-24, 138, -6, 174), weight=0.5, edge=1.0)
    for x, z in ((30.0, 112.0), (30.0, 122.0)):
        tprop("mod_nt_fire_engine", x, z, 90.0, tags={"civic", "cover"})
    for x, z, yaw in ((-74.0, 132.0, 90.0), (-74.0, 108.0, 90.0)):
        tprop("mod_city_bus", x, z, yaw, tags={"civic", "cover"})
    for aid, x, z in (("mod_bench", 24.0, 144.0), ("mod_bench", 24.0, 150.0), ("mod_flower_bed", 28.0, 160.0), ("mod_planter_concrete", 20.0, 132.0),
                      ("mod_bus_shelter", 12.0, 112.0), ("mod_bollard_concrete", 26.0, 140.0), ("mod_bollard_concrete", 26.0, 148.0)):
        tprop(aid, x, z, 0.0, tags={"civic"})

    # ------------------------------------------------------------------ 11. District 9: lakeside
    lx, lz, rx, rz = LAKE
    prom = g["promenade"]
    b.route("footpath", prom, 3.0, keepout=False)
    for k in range(0, len(prom) - 1):
        if k % 2:
            continue
        p, q = prom[k], prom[k + 1]
        m = ((p[0] + q[0]) / 2, (p[1] + q[1]) / 2)
        n = (lx - m[0], lz - m[1])
        nl = math.hypot(*n) or 1
        rp = (m[0] + n[0] / nl * 2.2, m[1] + n[1] / nl * 2.2)        # on the water side of the path
        if b.ground(*rp) > WATER + 0.2:
            prop("mod_nt_promenade_rail", rp[0], rp[1], asset_yaw_along(q[0] - p[0], q[1] - p[1]), tags={"promenade"}, check=False)
        if k % 6 == 0:
            o = (m[0] - n[0] / nl * 2.4, m[1] - n[1] / nl * 2.4)
            tprop(R.choice(["mod_street_lamp", "mod_bench", "mod_trash_bin"]), o[0], o[1], asset_yaw_facing(n[0], n[1]), tags={"promenade"})
    for aid, x, z, yaw in (("mod_nt_waterfront_house", 112.0, 190.0, 180.0), ("mod_nt_waterfront_house", 134.0, 191.0, 180.0),
                           ("mod_nt_waterfront_house", 156.0, 190.0, 180.0), ("mod_nt_waterfront_house", 168.0, 112.0, 225.0),
                           ("mod_nt_pump_station", 90.0, 150.0, 90.0), ("mod_nt_house_two", 96.0, 186.0, 90.0)):
        site(aid, x, z, yaw, tags={"lakeside"}, nudge=6)
    # boathouse on the east shore (origin on the shoreline, its slip over the water), the marina pier from the west shore
    shore_e = next(((lx + rx * f, lz) for f in [1.0 + 0.02 * i for i in range(40)] if b.ground(lx + rx * f, lz) > WATER + 0.25), (lx + rx, lz))
    bh = prop("mod_nt_boathouse", shore_e[0] - 0.5, shore_e[1], 270.0, tags={"lakeside", "boathouse"}, check=False, y=b.ground(*shore_e))
    if bh:
        tag_br(bh, "mod_nt_boathouse", 9)
        if pads is not None:
            pads.append(("mod_nt_boathouse", shore_e[0] + 4, shore_e[1], 270.0))
    shore_w = next(((lx - rx * f, lz + 4) for f in [1.0 + 0.02 * i for i in range(40)] if b.ground(lx - rx * f, lz + 4) > WATER + 0.25), (lx - rx, lz + 4))
    pier = prop("mod_nt_pier", shore_w[0] + 11.0, shore_w[1], 90.0, tags={"lakeside", "marina"}, check=False, y=b.ground(*shore_w))
    if pier:
        py = pier["pos"][1]
        for k, dx in enumerate((4.0, 10.0, 16.0)):
            for sz in (1, -1):
                fx, fz = shore_w[0] + dx + 2.0, shore_w[1] + sz * 7.0
                prop("mod_nt_pontoon_finger", fx, fz, 0.0, tags={"marina"}, check=False, y=WATER + 0.1, cls="none")
                if R.random() < 0.65:
                    prop("mod_nt_boat_motor", fx + 2.4, fz, 0.0, tags={"marina"}, check=False, y=WATER - 0.1, cls="none")
    for aid, x, z, yaw in (("mod_nt_boat_trailer", 108.0, 128.0, 30.0), ("mod_pickup", 112.0, 136.0, 120.0), ("mod_picnic_table", 150.0, 124.0, 0.0),
                           ("mod_garden_bench", 120.0, 178.0, 180.0), ("mod_flower_bed", 140.0, 182.0, 0.0), ("mod_sun_lounger", 130.0, 182.0, 180.0)):
        tprop(aid, x, z, yaw, tags={"lakeside"})

    # ------------------------------------------------------------------ 11b. block infill: buildings turned to the nearest street
    def fill(dists, pool, n, roads, maxd=30.0, step=8, pad=1.4, tags=()):
        pts_ = [(x, z) for x in range(-170, 171, step) for z in range(-196, 197, step) if district_of(x, z) in dists]
        R.shuffle(pts_)
        rds = [g[k] for k in roads]
        got = 0
        for x, z in pts_:
            if got >= n:
                break
            x, z = x + R.uniform(-2, 2), z + R.uniform(-2, 2)
            if b.in_keepout(x, z) or b.near(x, z, 9.0, classes={"solid"}):
                continue
            (d, s, _), ln = min(((polyline_dist((x, z), l), l) for l in rds), key=lambda r: r[0][0])
            if d > maxd:
                continue
            (px, pz), t = point_at(ln, s)
            aid = R.choice(pool)
            if aid not in cat:
                continue
            yaw = asset_yaw_facing(px - x, pz - z) if d > 0.1 else 0.0
            yaw = round(yaw / 90.0) * 90.0 if abs(yaw - round(yaw / 90.0) * 90.0) < 25 else yaw
            if clear(aid, x, z, yaw, pad=pad):
                e = b.place(aid, x, z, yaw, tags=set(tags) | {"building", "fill"})
                if e:
                    tag_br(e, aid, district_of(x, z))
                    if pads is not None and cat[aid].enterable:
                        pads.append((aid, x, z, e["yaw"]))
                    got += 1
        return got
    rep["fill"] = {
        5: fill({5}, ["mod_nt_shop_small", "mod_nt_shop_small", "mod_bld_mixed_use", "mod_nt_cafe", "mod_nt_shop_row", "mod_bld_restaurant_family",
                      "mod_bld_office_setback", "mod_bld_apartment_block", "mod_bld_diner_small"], 22, ROADS, tags={"downtown"}),
        2: fill({2}, ["mod_nt_workshop", "mod_nt_workshop", "mod_nt_site_office", "mod_nt_storage_tank"], 4, ["service", "ns_n"], maxd=34, tags={"industrial"}),
        6: fill({6}, ["mod_nt_shop_small", "mod_bld_diner_small", "mod_nt_cafe", "mod_nt_shop_row"], 4, ["ew_e", "com_back", "com_alley"], tags={"commercial"}),
        8: fill({8}, ["mod_nt_shop_small", "mod_nt_cafe", "mod_nt_house_two", "mod_nt_kiosk"], 5, ["ns_s", "ring"], tags={"civic"}),
        9: fill({9}, ["mod_nt_house_small", "mod_nt_house_two", "mod_nt_waterfront_house"], 4, ["lake_rd", "ew_e"], maxd=40, tags={"lakeside"}),
        7: fill({7}, ["mod_nt_site_office", "mod_nt_construction_frame_small"], 2, ["con_rd"], maxd=40, tags={"construction"}),
        4: fill({4}, ["mod_nt_house_small", "mod_nt_house_two", "mod_bld_bungalow"], 4, ["res_a", "res_b", "res_n", "ew_w"], tags={"residential"}),
    }

    # ------------------------------------------------------------------ 12. between the districts: small buildings, kiosks, terrain features
    infill_pool = ["mod_nt_house_small", "mod_nt_kiosk", "mod_nt_house_small", "mod_garden_shed", "mod_nt_site_office", "mod_bus_shelter"]
    pts = [(x, z) for x in range(-170, 171, 9) for z in range(-196, 197, 9)]
    R.shuffle(pts)
    nin = 0
    for x, z in pts:
        x, z = x + R.uniform(-3, 3), z + R.uniform(-3, 3)
        near_band = abs(abs(x) - 87) < 10 or abs(abs(z) - 97) < 10   # the seams between the districts
        if not near_band or b.in_keepout(x, z) or b.near(x, z, 12.0, classes={"solid"}):
            continue
        rds = [g[k] for k in ROADS]
        (d, s, _), ln = min(((polyline_dist((x, z), l), l) for l in rds), key=lambda r: r[0][0])
        if d > 22:
            continue
        (px, pz), t = point_at(ln, s)
        aid = R.choice(infill_pool)
        if aid not in cat:
            continue
        yaw = asset_yaw_facing(px - x, pz - z)
        if clear(aid, x, z, yaw, pad=1.5) and b.place(aid, x, z, yaw, tags={"infill", "building"}) is not None:
            e = b.inst[-1]
            tag_br(e, aid, district_of(x, z))
            if pads is not None and cat[aid].enterable:
                pads.append((aid, x, z, e["yaw"]))
            nin += 1
            if nin >= 26:
                break
    rep["infill"] = nin
    if pads is not None:
        return b, None, g, rep

    # outcrops and boulders (terrain features) on the seams and the rural / base slopes
    b.scatter([("nat_outcrop_limestone", 1), ("nat_boulders_granite_group", 2), ("nat_boulder_granite_l", 2), ("mod_rock_large", 2), ("mod_rock_medium", 2)],
              lambda x, z: (abs(abs(x) - 87) < 9 or abs(abs(z) - 97) < 9 or district_of(x, z) in (1, 3)) and not in_poly((x, z), rect(*COMPOUND)),
              1 / 1400.0, R, bounds=(-HX, -HZ, HX, HZ), tags={"rocks", "cover"}, min_dist=14.0)
    tprop("mod_fallen_tree", 80.0, -186.0, 30.0, tags={"feature"})
    tprop("mod_fallen_tree", -170.0, 92.0, 80.0, tags={"feature"})

    # ------------------------------------------------------------------ 12b. AS1 abandoned: stones and weeds on the roads, rubble, broken walls
    if STYLE == "abandoned":
        nd = {"stones": 0, "weeds": 0, "saplings": 0, "rubble": 0, "walls": 0}
        RD = rng(SEED + 23)   # NT2b: its own stream, so the rest of the town keeps its layout

        def drop(aid, x, z, yaw, dy=0.0, cls="flat"):
            if aid not in cat or not grid.in_build(x, z) or b.ground(x, z) < WATER + 0.2:
                return None
            if b.overlaps(cat[aid], x, z, yaw, {"flat", "none", "veg"}):
                return None
            return b.place(aid, x, z, yaw, check=False, cls=cls, dy=dy, tags={"ruin_dress"})
        for k in ROADS:
            pts = g[k]
            Lr, s_ = polyline_length(pts), R.uniform(0, 4)
            half = 7.5 if k in DOWNTOWN_ST else 3.4
            while s_ < Lr:
                (px, pz), t = point_at(pts, s_)
                nx, nz = -t[1], t[0]
                r = R.random()
                if r < 0.5:   # stones over the carriageway
                    o = R.uniform(-3.3, 3.3)
                    nd["stones"] += drop(R.choice(["nat_stones_patch", "nat_stones_granite_patch", "mod_rock_small", "nat_stones_patch"]), px + nx * o, pz + nz * o, R.uniform(0, 360), dy=0.06) is not None
                elif r < 0.86:   # weeds at the kerbs and in the joints
                    o = R.choice((-1, 1)) * (R.uniform(2.6, 3.9) if R.random() < 0.45 else R.uniform(0.0, 2.6))
                    nd["weeds"] += drop(R.choice(["mod_weeds_crack", "mod_weeds_clump", "nat_tallgrass_patch", "nat_drygrass_tufts", "mod_weeds_crack", "nat_nettle_clump"]),
                                        px + nx * o, pz + nz * o, R.uniform(0, 360), dy=0.05, cls="veg") is not None
                elif r < 0.875:
                    o = R.choice((-1, 1)) * R.uniform(half + 0.6, half + 2.0)
                    nd["saplings"] += drop("mod_sapling_paving", px + nx * o, pz + nz * o, R.uniform(0, 360), cls="veg") is not None
                elif r < 0.895:   # NT2: a tree grown out of the road
                    o = R.choice((-1, 1)) * R.uniform(0.5, 3.0)
                    tr = drop(R.choice(["tree_ash", "tree_oak", "mod_tree_ornamental", "nat_sapling_ash", "mod_sapling_paving"]), px + nx * o, pz + nz * o, R.uniform(0, 360), cls="veg")
                    nd["saplings"] += tr is not None
                    if tr is not None:   # NT2b: the slabs heaved round its roots
                        for a in (0.0, 2.1, 4.2):
                            drop(RD.choice(["mod_sapling_paving", "mod_rubble_small", "mod_ivy_patch_ground"]), px + nx * o + math.cos(a) * 1.6, pz + nz * o + math.sin(a) * 1.6, RD.uniform(0, 360), cls="veg")
                elif r < 0.95:   # rubble spilled onto the road edge
                    o = R.choice((-1, 1)) * R.uniform(2.0, 3.6)
                    nd["rubble"] += drop(R.choice(["mod_rubble_small", "mod_wall_chunks", "mod_glass_pile"]), px + nx * o, pz + nz * o, R.uniform(0, 360), dy=0.04, cls="solid") is not None
                else:   # a low broken wall along the pavement
                    o = R.choice((-1, 1)) * (half + 1.6)
                    nd["walls"] += drop(R.choice(["mod_cinder_wall", "mod_jersey_barrier_damaged", "mod_wall_chunks", "mod_retaining_wall"]), px + nx * o, pz + nz * o,
                                        asset_yaw_along(*t) + R.uniform(-8, 8), cls="solid") is not None
                s_ += R.uniform(1.2, 2.8)
        nd.update({"potholes": 0, "puddles": 0, "debris": 0, "papers": 0, "groundIvy": 0, "drifts": 0})
        for k in ROADS:   # NT2b: the carriageway breaking up: potholes, puddles, debris, litter, ivy creeping in from the kerbs
            pts = g[k]
            Lr, s_ = polyline_length(pts), RD.uniform(0, 6)
            half = 7.5 if k in DOWNTOWN_ST else 3.4
            while s_ < Lr:
                (px, pz), t = point_at(pts, s_)
                nx, nz = -t[1], t[0]
                r = RD.random()
                if r < 0.07:
                    o = RD.uniform(-1.5, 1.5)
                    nd["potholes"] += drop("mod_asphalt_crater", px + nx * o, pz + nz * o, RD.uniform(0, 360), cls="flat") is not None
                elif r < 0.2:
                    o = RD.uniform(-2.0, 2.0)
                    nd["puddles"] += drop("wat_puddles", px + nx * o, pz + nz * o, asset_yaw_along(*t) + RD.uniform(-20, 20), cls="flat") is not None
                elif r < 0.42:
                    o = RD.uniform(-2.5, 2.5)
                    nd["debris"] += drop("rub_debris_patch", px + nx * o, pz + nz * o, RD.uniform(0, 360), dy=0.02, cls="flat") is not None
                elif r < 0.62:
                    o = RD.choice((-1, 1)) * RD.uniform(1.0, half)
                    nd["papers"] += drop(RD.choice(["furn_papers", "furn_papers", "mod_glass_pile"]), px + nx * o, pz + nz * o, RD.uniform(0, 360), dy=0.01, cls="flat") is not None
                elif r < 0.85:
                    o = RD.choice((-1, 1)) * RD.uniform(half - 1.2, half + 0.4)
                    nd["groundIvy"] += drop("mod_ivy_patch_ground", px + nx * o, pz + nz * o, RD.uniform(0, 360), dy=0.02, cls="veg") is not None
                else:   # a drift of tall grass and weeds banked against the kerb (clumped, not a row)
                    side = RD.choice((-1, 1))
                    for q in range(RD.randint(4, 8)):
                        o = side * RD.uniform(half - 2.0, half + 0.8)
                        a = RD.uniform(-2.5, 2.5)
                        nd["drifts"] += drop(RD.choice(["nat_tallgrass_patch", "nat_tallgrass_verge", "mod_weeds_clump", "nat_nettle_clump", "nat_bramble_thicket"]),
                                             px + nx * o + t[0] * a, pz + nz * o + t[1] * a, RD.uniform(0, 360), cls="veg") is not None
                s_ += RD.uniform(7.0, 13.0)
        jams = 0   # NT2: abandoned traffic: short queues of dead cars on the main roads, a few barricaded junctions
        for k in ("ring", "ns_n", "ns_s", "ew_w", "ew_e", "service"):
            pts = g[k]; Lr = polyline_length(pts)
            for _ in range(3):
                s0 = R.uniform(10, max(12, Lr - 40)); lane = R.choice((-1, 1)) * 1.8
                for q in range(R.randint(2, 4)):
                    (px, pz), t = point_at(pts, min(Lr - 1, s0 + q * R.uniform(5.5, 7.5)))
                    yw = asset_yaw_facing(t[0] * lane, t[1] * lane) + R.uniform(-12, 12)
                    car = drop(RW.choice(WRECKS), px - t[1] * lane, pz + t[0] * lane, yw, dy=0.08, cls="solid")
                    jams += car is not None
                    if car is not None:   # NT2b: brambles and grass grown up round the dead cars
                        for _ in range(RD.randint(1, 3)):
                            a = RD.uniform(0, 2 * math.pi)
                            drop(RD.choice(["nat_bramble_thicket", "nat_tallgrass_patch", "mod_weeds_clump", "mod_ivy_patch_ground"]),
                                 px - t[1] * lane + math.cos(a) * 2.6, pz + t[0] * lane + math.sin(a) * 2.6, RD.uniform(0, 360), cls="veg")
        for k in ("ns_n2", "ew_e2", "ring"):
            (px, pz), t = point_at(g[k], R.uniform(8, polyline_length(g[k]) - 8))
            for j in (-1, 0, 1):
                drop(R.choice(["mod_jersey_barrier", "mod_jersey_barrier_damaged", "mod_water_barrier_red"]), px - t[1] * j * 3.1, pz + t[0] * j * 3.1, asset_yaw_along(-t[1], t[0]), dy=0.05, cls="solid")
            drop("mod_sandbag_position", px - t[1] * 6.5, pz + t[0] * 6.5, asset_yaw_facing(t[0], t[1]), cls="solid")
        nd["jamCars"] = jams
        for e in [e for e in b.inst if "building" in e["tags"]]:   # rubble and weeds against the walls
            a = cat[e["asset"]]
            hx, hz = a.half
            for q in range(R.randint(2, 5) + RD.randint(2, 4)):   # NT2b: more growth against the walls
                side = R.randrange(4)
                u = R.uniform(-0.45, 0.45)
                lx, lz = ((u * 2 * hx, -hz - 0.6), (u * 2 * hx, hz + 0.6), (-hx - 0.6, u * 2 * hz), (hx + 0.6, u * 2 * hz))[side]
                wx, wz = local_to_world((e["pos"][0], e["pos"][2]), e["yaw"], lx, lz)
                aid = R.choice(["mod_weeds_clump", "nat_nettle_clump", "nat_tallgrass_patch", "mod_weeds_crack", "mod_rubble_small", "nat_bramble_thicket",
                                "mod_sapling_paving", "shrub_hazel", "mod_ivy_patch_ground", "nat_bramble_thicket"])
                nd["weeds"] += drop(aid, wx, wz, R.uniform(0, 360), cls="veg" if not aid.startswith("mod_rubble") else "solid") is not None
        rep["ruinDressing"] = nd

    # ------------------------------------------------------------------ 13. trees everywhere (not on roads, lots or in the square)
    def open_ground(x, z):
        return (not in_poly((x, z), rect(*SQUARE)) and lake_k(x, z) > 1.25 and not in_poly((x, z), rect(*COMPOUND))
                and not (100 < x < 172 and -80 < z < 0) and not (-150 < x < 150 and -196 < z < -188))
    dens = {1: 1 / 240.0, 2: 1 / 330.0, 3: 1 / 125.0, 4: 1 / 110.0, 5: 1 / 450.0, 6: 1 / 240.0, 7: 1 / 260.0, 8: 1 / 150.0, 9: 1 / 105.0}
    if STYLE == "abandoned":   # overgrown
        dens = {k: v * 1.3 for k, v in dens.items()}
    ntree = 0
    for dk, dd in dens.items():
        pool = [("tree_oak", 3), ("tree_ash", 3), ("tree_poplar", 1)] if dk in (1, 3, 7) else \
               [("tree_oak", 2), ("tree_ash", 3), ("mod_tree_ornamental", 2), ("mod_tree_blossom", 1), ("tree_poplar", 1)]
        ntree += len(b.scatter(pool, lambda x, z, dk=dk: district_of(x, z) == dk and open_ground(x, z), dd, R, bounds=(-HX, -HZ, HX, HZ),
                               tags={"trees"}, cls="veg", min_dist=5.5))
    # copses on the seams
    for c in ((-88.0, -100.0), (88.0, 100.0), (-160.0, 92.0), (160.0, -96.0), (70.0, -196.0), (-60.0, 96.0), (-170.0, -96.0), (176.0, 60.0)):
        ntree += len(b.scatter([("tree_oak", 2), ("tree_ash", 2), ("tree_poplar", 1)], circle_poly(c[0], c[1], 13.0, 16), 1 / 45.0, R,
                               tags={"trees", "copse"}, cls="veg", min_dist=4.5))
    b.scatter([("shrub_hazel", 3), ("nat_hawthorn_bush", 2), ("shrub_bramble", 1)], lambda x, z: open_ground(x, z) and district_of(x, z) != 5, 1 / 520.0, R,
              bounds=(-HX, -HZ, HX, HZ), tags={"shrubs"}, cls="veg")
    rep["trees"] = ntree
    for e in [e for e in b.inst if 138 < e["pos"][0] < 180 and -32 < e["pos"][2] < 10 and e["asset"].startswith(("tree_", "mod_tree_", "nat_sapling", "mod_sapling", "shrub_"))]:
        b.remove(e)   # NT2b: the petrol forecourt stays open (no tree in front of the canopy)

    # ------------------------------------------------------------------ 14. decor ring beyond the border (trees, extra.decor)
    nt = 0

    def decor(aid, x, z, yaw):
        a = cat[aid]
        if b.overlaps(a, x, z, yaw, {"flat", "none"}):
            return None
        return b.place(aid, x, z, yaw, check=False, extra=DECOR, tags={"decor"})
    for _ in range(3000):
        x, z = R.uniform(-HX - 60, HX + 60), R.uniform(-HZ - 60, HZ + 60)
        if (abs(x) < HX + 5 and abs(z) < HZ + 5) or abs(x) > HX + 40 or abs(z) > HZ + 40:
            continue
        if lake_k(x, z) < 1.2:
            continue
        nt += decor(R.choice(TREES), x, z, R.uniform(0, 360)) is not None
        if nt >= 300:
            break
    rep["decorTrees"] = nt

    # ------------------------------------------------------------------ 15. scenario: Battle royale (no spawns), TDM teams, districts
    sc = ScenarioBuilder(b, defending="Axis")
    sc.rules["mode"] = "royale"
    sc.rules["lootModern"] = True
    sc.rules["teamFaction"] = ["Axis", "Allies"]
    # Assault (also what the scenario validator requires): the Allies push from the lakeside to the square, the industrial
    # yards and the base; the Axis hold each from behind
    dts = [e for e in b.inst if "downtown" in e["tags"] and "building" in e["tags"] and math.hypot(e["pos"][0], e["pos"][2]) < 45]
    ind = [e for e in b.inst if "industrial" in e["tags"] and "building" in e["tags"]]
    base = [e for e in b.inst if "base" in e["tags"] and "building" in e["tags"]]
    sc.objective("Clock-tower square", 0.0, 2.0, radius=20.0, capture_time=45.0, attack_from=(0, 120), linked=[e["id"] for e in dts])
    sc.objective("Industrial yards", 0.0, -150.0, radius=22.0, capture_time=45.0, attack_from=(0, -40), linked=[e["id"] for e in ind])
    sc.objective("Military base", -130.0, -150.0, radius=24.0, capture_time=55.0, attack_from=(-130, -100), linked=[e["id"] for e in base])
    for ph, (x, z), lab in ((0, (60.0, 128.0), "Allies - civic centre"), (1, (-40.0, -60.0), "Allies - downtown north"), (2, (-140.0, -82.0), "Allies - residential north")):
        sc.spawn("attacker", [ph], x, z, label=lab, rack=None)
    for ph, (x, z), lab in ((0, (-20.0, -120.0), "Axis - industrial south"), (1, (40.0, -190.0), "Axis - rail siding"), (2, (-150.0, -186.0), "Axis - base north")):
        sc.spawn("defender", [ph], x, z, label=lab, rack=None)
    for team, key, fac in ((0, "A", "Axis"), (1, "B", "Allies")):
        x, z = tdm[key]
        m = sc.spawn("defender" if team == 0 else "attacker", [0], x, z, face=(0.0, 0.0), faction=fac,
                     label="Team A spawn (Residential)" if team == 0 else "Team B spawn (Commercial strip)", rack="auto")
        m["team"], m["phases"] = team, []
        yw = math.radians(m["yaw"])
        b.try_place("crate_mixed_supply", x + math.sin(yw) * 3.0, z + math.cos(yw) * 3.0, m["yaw"], R, tags={"supply"}, radius=3.0)
    return b, sc, g, rep


def lake_body():
    """The lake's surface (a lasso water body: the template waterLevel alone draws no water)."""
    lx, lz, rx, rz = LAKE
    out = []
    for k in range(64):
        a = 2 * math.pi * k / 64
        dx, dz = math.cos(a), math.sin(a)
        w = 1.0 + 0.08 * math.sin(3 * a + 0.7) + 0.05 * math.sin(5 * a + 2.1)
        out.append([round(lx + dx * rx * w * 1.06, 3), round(lz + dz * rz * w * 1.06, 3)])
    return {"id": 1, "kind": "pond", "level": WATER, "seed": [lx, lz], "outline": out}


def cover_catalogue(path, cat):
    """Rows for every catalogue asset (--selftest-maps checks the doc covers the catalogue)."""
    with open(path, encoding="utf-8") as f:
        lines = f.read().split("\n")
    known = {l[3:l.index("`", 3)] for l in lines if l.startswith("| `")}
    hdr = next(i for i, l in enumerate(lines) if l.startswith("| Asset |"))
    ncols = lines[hdr].count("|") - 2
    k = max(i for i, l in enumerate(lines) if l.startswith("| `"))
    for aid in sorted(set(cat.ids()) - known):
        k += 1
        lines.insert(k, "| `%s` |" % aid + " not used |" * ncols)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))


def build(ctx):
    cat = ctx.catalog
    if os.environ.get("NT_STUB"):                                    # dry runs before the BR4 catalogs are written
        for aid, (fw, fd, fl) in NEW.items():
            if aid not in cat:
                cat.assets[aid] = Asset({"id": aid, "footprint": [fw, fd], "category": "Modern", "enterable": fl > 0, "floors": fl}, "")
    grid = Grid(W, D, margin=MARGIN)
    grid.apply(lambda x, z, h: base_height(x, z))
    g = geometry()
    # the base plateau, the lake, the roads (smoothed profiles), the channel, gullies and the excavation
    x0, z0, x1, z1 = COMPOUND
    stamp_flatten_poly(grid, rect(x0 - 4, z0 - 4, x1 + 4, z1 + 4), height=grid.sample(*HILL) - 0.5, falloff=18.0)
    grid.apply(lambda x, z, h: max(h, WATER + 1.6 + 1.5 * smoothstep(1.45, 2.4, lake_k(x, z))))   # dry land everywhere but the lake
    stamp_lake(grid)
    for k in ROADS:
        stamp_road(grid, g[k], 4.6 if k not in DOWNTOWN_ST else 7.5)
    stamp_road(grid, g["rail"], 3.0)
    ch = g["channel"]
    stamp_valley(grid, ch, floor=channel_floor(grid, ch), top_half=lambda u: 4.6, floor_half=lambda u: 2.1, power=0.8)
    for path, depth in (([(150, -200), (158, -170), (168, -140), (176, -118)], 2.6), ([(96, -200), (100, -186), (104, -170)], 2.0),
                        ([(-176, 40), (-166, 60), (-164, 84)], 1.8)):
        L = polyline_length(path)
        fl = [grid.sample(*point_at(path, L * i / 50.0)[0]) - depth for i in range(51)]
        stamp_valley(grid, path, floor=lambda u, fl=fl: fl[min(50, int(round(u * 50)))], top_half=lambda u: 6.0, floor_half=lambda u: 1.4)
    stamp_pit(grid, -128.0, 150.0, 10.0, 5.5, 0.0, 3.2)
    # pass 1: building pads (levelled), pass 2 on the quantised ground
    pads = []
    populate(ctx, grid, pads)
    for aid, x, z, yaw in pads:
        a = cat[aid]
        hx, hz = a.half[0] + 1.2, a.half[1] + 1.2
        poly = [local_to_world((x, z), yaw, lx, lz) for lx, lz in ((-hx, -hz), (hx, -hz), (hx, hz), (-hx, hz))]
        stamp_flatten_poly(grid, poly, height=grid.sample(x, z), falloff=4.0)
    grid.quantise()
    b, sc, g, rep = populate(ctx, grid)
    sj = sc.to_json()
    sj["districts"] = [{"id": k, "name": n, "pos": [c[0], round(b.ground(*c), 2), c[1]], "radius": 60.0} for k, (n, c) in DISTRICTS.items()]

    # ------------------------------------------------------------------ counts for the report
    by = {}
    for e in b.inst:
        br = (e.get("extra") or {}).get("br")
        if br:
            k = by.setdefault(br["district"], {"small": 0, "large": 0})
            k[br["size"]] += 1
    rep["buildingsByDistrict"] = {"%d %s" % (k, DISTRICTS[k][0]): v for k, v in sorted(by.items())}
    rep["treesTotal"] = sum(1 for e in b.inst if e["asset"].startswith(("tree_", "mod_tree_")) and "decor" not in e["tags"])

    # ------------------------------------------------------------------ output
    tdir = ctx.templates
    grid.write_png(os.path.join(tdir, TID + ".height.png"))
    eye = lambda x, z: [round(x, 2), round(b.ground(x, z) + 1.7, 2), round(z, 2)]
    cam = ([0.0, 220.0, 330.0], 0.0, -36.0)
    thumb_cam = ([150.0, 120.0, 260.0], look_yaw(-150.0, -260.0), -24.0)
    sp, st = point_at(g["ew_e2"], 12.0)
    views = [
        {"name": "overview_high", "pos": [0.0, 260.0, 300.0], "yaw": 0.0, "pitch": -45.0},
        {"name": "street_eye", "pos": eye(sp[0] - st[1] * 4.6, sp[1] + st[0] * 4.6), "yaw": look_yaw(st[0], st[1]), "pitch": 2.0, "gameplay": True},
        {"name": "square_eye", "pos": eye(-14.0, 16.0), "yaw": look_yaw(14.0, -26.0), "pitch": 6.0, "gameplay": True},
        {"name": "base_eye", "pos": eye(-130.0, -98.0), "yaw": look_yaw(0.0, -1.0), "pitch": 4.0, "gameplay": True},
        {"name": "lake_eye", "pos": eye(110.0, 120.0), "yaw": look_yaw(1.0, 1.0), "pitch": 0.0, "gameplay": True},
    ]
    write_map(os.path.join(tdir, TID + ".nmap.json"), name=NAME, template_id=TID, template_version=VERSION, seed=SEED, width=W, depth=D,
              builder=b, scenario_json=sj, explore_start=((-14.0, b.ground(-14.0, 16.0), 16.0), look_yaw(14.0, -26.0)), camera=cam,
              bookmarks={str(k): {"position": [c[0], b.ground(*c) + 60.0, c[1] + 70.0], "yaw": 0.0, "pitch": -40} for k, (n, c) in DISTRICTS.items()},
              extra={"groundPaint": b.paint.to_json(), "waterBodies": [lake_body()],
                     **({"look": {"style": "abandoned", "haze": 4.0, "weather": 0.45, "envFog": 1, "bloom": 1.35, "saturation": 1.12, "tint": [1.04, 1.0, 0.94]},
                        "lighting": {"fill": 1.2, "ambient": [0.035, 0.05, 0.045]}} if STYLE == "abandoned" else {})})
    write_template(os.path.join(tdir, TID + ".json"), tid=TID, name=NAME, order=9, version=VERSION, seed=SEED,
                   description="A modern town for Battle Royale, 350 x 400 m, nine districts: a walled military base on a hill, "
                               "industrial yards with a rail siding, rural farms and orchards, a residential quarter with a park, "
                               "downtown round its clock-tower square, a commercial strip, a construction site, the civic centre with "
                               "the hospital, and a lake with a marina. A ring road, two bent main streets and a drainage channel to "
                               "the lake. Battle Royale, Team Deathmatch and Solo.",
                   width=W, depth=D, margin=MARGIN, heightmap=grid.meta(TID + ".height.png"), water_level=WATER,
                   map_file=TID + ".nmap.json", camera=cam, thumbnail=TID + ".thumb.png", thumb_camera=thumb_cam,
                   extra={"userMap": True, "views": views})
    use = asset_usage(b)
    update_usage_doc(os.path.join(ctx.repo, "docs", "asset-usage-default-maps.md"), use, "Newtown",
                     "- **Newtown:** *Newtown* (BR4, `tools/mapgen/maps/newtown.py`): placed counts; \"not used\" = not on this map.")
    cover_catalogue(os.path.join(ctx.repo, "docs", "asset-usage-default-maps.md"), cat)
    rep.update({"instances": len(b.inst), "use": dict(sorted(use.items())), "rejects": {"%s/%s" % k: v for k, v in sorted(b.rejects.items())},
                "districts": sj["districts"]})
    write_summary(os.path.join(ctx.out, TID + ".summary.json"), rep)
    print("instances %d | buildings %s | trees %d (+%d decor) | infill %d | bridges %d road, %d foot | lines %s | missed %s" %
          (len(b.inst), rep["buildingsByDistrict"], rep["treesTotal"], rep["decorTrees"], rep["infill"], rep["roadBridges"], rep["footbridges"],
           rep["lines"], rep["missed"]))
