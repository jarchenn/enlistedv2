"""Hollow Village (MP1): a poor farming village on a hillside in grey late-autumn light, 250 x 250 m, woods on every side.

The ground climbs ~25 m from the south lane to the church. A muddy square with the well and a bonfire pile sits in the
middle; the lower lane winds up from the south woods through the square and on east to the farmyard (barns, pens);
the upper lane bends north from the square to the walled graveyard and the stone church at the top. A back path
runs west over the plank footbridge across the gully to a lone house in the trees. In the north-east the ground
breaks in a cliff: a path cut along its face climbs to the bluff where the timber watchtower looks over the roofs.
Structure (houses, barns, church, walls, fences, bridge, tower) is generated; the dressing uses imported models only
(PR10 postapoc set, Poly Haven household / yard / nature models, dead trees, conifers, wrecks).
Assault A-C (square, farmyard, church), TDM spawns at the south and north ends, Solo spreads its own spawns, Battle
Royale loot on every enterable building (extra.loot) with four landing districts.

    <python> -I tools/mapgen/build.py hollow_village

Writes content/templates/hollow_village.{json,height.png,nmap.json} and the "Hollow Village" column of
docs/asset-usage-default-maps.md."""
import json
import math
import os
import subprocess

from mapgen import (GroundPaint, Grid, Frame, MapBuilder, ScenarioBuilder, stamp_valley, stamp_flatten_poly, catmull_rom,
                    slots_from_structures, write_map, write_template, write_summary, asset_usage, rect, rng, point_at,
                    polyline_length, polyline_dist, offset_polyline, in_poly, asset_yaw_facing, asset_yaw_along, look_yaw,
                    local_to_world, fbm, circle_poly, smoothstep, lerp)
from maps.backrooms import update_usage_doc
from maps.newtown import cover_catalogue

TID, NAME, VERSION, SEED = "hollow_village", "Hollow Village", 1, 1031
W, D, MARGIN = 250, 250, 80
HX, HZ = W / 2, D / 2

SQUARE = (0.0, 0.0)
CHURCH = (14.0, 95.0)                    # door to -z (yaw 180), onto the upper lane
YARD = (-10.0, 82.0, 40.0, 112.0)        # graveyard walls x0, z0, x1, z1
FARM = (68.0, 16.0)
TOWER = (101.0, 86.0)                    # on the bluff
TOWER_YAW = asset_yaw_facing(-0.6, -1.0)  # looking south-west over the roofs; the stair climbs from the rear
TOWER_IN = local_to_world(TOWER, TOWER_YAW, -0.7, -6.4)   # the foot of its stair
BLUFF = ((40.0, 125.0), (125.0, 30.0))   # the cliff line; the plateau lies north-east of it
BLUFF_H = 8.0
LONE = (-100.0, -14.0)                   # the house past the gully

HOUSES = [("house_stone_cottage", 3), ("house_farmhouse_2story", 2), ("vil_norman_farmhouse", 1), ("vil_longhouse_thatched", 1),
          ("ruin_cottage_corner", 1), ("ruin_house_shelled", 1), ("ruin_farmhouse_gutted", 1), ("small_storehouse", 1),
          ("small_stone_dairy", 1), ("small_carpenter_workshop", 1), ("vil_granite_longere", 1)]
GEN_DECOR = ("drs_", "nat_", "shrub_", "tree_", "furn_", "mod_barrel", "mod_crate", "fx_")   # generated decoration ids (companions dropped)
MAX_HOUSES = 24   # MP1b: the core built up wall to wall (+ 3 farm buildings, the lone house, 4 derelicts)
SQUARE_R, CORE_R0, CORE_R = 13.0, 10.0, 78.0
BUILDING_CATS = ("Houses", "Landmarks", "Large buildings", "Small buildings", "Ruins")
DECOR = {"decor": True}
# imported dressing pools (every id has a committed model: checked in build())
LANE = ["ph_wooden_barrels_01", "ph_wine_barrel_01", "pa_crate_slatted", "pa_fruit_crate_low", "pa_fruit_crate_mid",
        "pa_fruit_crate_high", "ph_wooden_crate_01", "ph_wooden_crate_02", "ph_wicker_basket_01", "ph_wicker_basket_02", "ph_wooden_bucket_01",
        "ph_wooden_bucket_02", "ph_old_tyre", "ph_rusted_wheel_rim_01", "ph_rusted_wheel_rim_02", "ph_wooden_ladder", "ph_wine_barrel_01",
        "ph_dead_tree_trunk", "ph_dry_branches_medium_01_a", "ph_dry_branches_medium_01_b", "ph_root_cluster_01", "pa_chest_wooden"]
YARD_PROPS = ["ph_wooden_barrels_01", "ph_wine_barrel_01", "pa_crate_slatted", "pa_fruit_crate_low", "pa_fruit_crate_high", "ph_wooden_bucket_01",
              "ph_watering_can_metal_01", "ph_rusted_spade_01", "ph_picke_dirty_01", "ph_wooden_axe", "ph_hatchet", "ph_wooden_ladder_02",
              "ph_wooden_broom", "ph_rubber_boots", "ph_old_tyre", "ph_seeding_tray_01", "ph_trowel_01", "ph_painted_wooden_bench",
              "ph_wooden_picnic_table", "ph_dead_tree_trunk_02", "ph_stone_fire_pit", "ph_worn_metal_rack", "ph_garden_gnome"]
ROOM = ["ph_wooden_bucket_02", "pa_chest_wooden", "pa_crate_slatted", "ph_wooden_stool_01",
        "ph_folding_wooden_stool", "ph_rockingchair_01", "ph_jug_01", "ph_ceramic_pot", "ph_metal_jug",
        "ph_pot_enamel_01", "ph_wooden_broom", "ph_painted_wooden_chair_01", "ph_painted_wooden_stool", "ph_vintage_suitcase",
        "ph_wooden_bowl_01", "ph_brass_pot_01", "ph_wine_bottles_01", "ph_wooden_crate_01", "ph_vintage_wooden_drawer_01"]
WOOD_TREES = [("ph_pine_tree_01_b", 3), ("ph_pine_tree_01_c", 1), ("ph_fir_tree_01_c", 6), ("ph_fir_tree_01_b", 2), ("pa_tree_dead_a", 1),
              ("pa_tree_dead_b", 3), ("pa_tree_dead_c", 3), ("pa_tree_dead_d", 3), ("pa_tree_dead_e", 1), ("ph_fir_sapling_medium_a", 3),
              ("ph_fir_sapling_medium_c", 2), ("ph_pine_sapling_medium_a", 2), ("ph_pine_sapling_medium_c", 2)]   # MP1b: light firs, bare trees, saplings
UNDER = [("ph_fern_02_a", 3), ("ph_fern_02_b", 3), ("ph_fern_02_c", 2), ("ph_fern_02_d", 2), ("ph_dry_branches_medium_01_a", 2),
         ("ph_dry_branches_medium_01_c", 2), ("ph_root_cluster_02_a", 1), ("ph_root_cluster_02_c", 1), ("ph_rock_moss_set_01_rock02", 1),
         ("ph_rock_moss_set_02_rock08", 1), ("ph_pine_roots_a", 1), ("ph_dead_tree_trunk", 1)]
DEAD = ["pa_tree_dead_a", "pa_tree_dead_b", "pa_tree_dead_c", "pa_tree_dead_d", "pa_tree_dead_e"]
TREEKW = dict(cls="veg", ok_with={"flat", "none", "veg", "wire"})


def bluff_d(x, z):
    """Signed distance to the cliff line (positive on the plateau)."""
    (ax, az), (bx, bz) = BLUFF
    nx, nz = (bz - az), -(bx - ax)
    n = math.hypot(nx, nz)
    return -((x - ax) * nx + (z - az) * nz) / n


def ramp():
    """The cliff path: a cut along the face from its foot (s = 0) to the plateau edge (8 m up)."""
    (ax, az), (bx, bz) = BLUFF
    L = math.hypot(bx - ax, bz - az)
    n = (-(bz - az) / L, (bx - ax) / L)   # unit normal toward the plateau (bluff_d > 0)
    if bluff_d(ax + n[0], az + n[1]) < 0:
        n = (-n[0], -n[1])
    P = lambda t: (ax + (bx - ax) * t, az + (bz - az) * t)
    a, c = P(0.62), P(0.30)
    return [(a[0] - 5.0 * n[0], a[1] - 5.0 * n[1]), (c[0] + 5.0 * n[0], c[1] + 5.0 * n[1])]


def base_height(x, z):
    h = 20.0 + 0.1 * z + 1.3 * fbm(x / 70.0, z / 70.0, 7, 4) + 0.6 * fbm(x / 18.0, z / 18.0, 9, 2)
    add = BLUFF_H * smoothstep(-2.2, 2.2, bluff_d(x, z))
    r = ramp()
    d, s, _ = polyline_dist((x, z), r)
    if d < 3.2:
        add = lerp(add, BLUFF_H * s / polyline_length(r), smoothstep(3.2, 1.8, d))
    return h + add


def geometry():
    g = {}
    g["LB"] = catmull_rom([(-30, -124), (-22, -96), (-6, -72), (-13, -46), (-3, -20), (0, -6), (16, -5), (28, -1), (46, 9), (60, 14)], 6)
    g["LA"] = catmull_rom([(1, 6), (-14, 20), (-9, 38), (7, 52), (3, 66), (12, 80)], 6)
    g["BP"] = catmull_rom([(-8, 2), (-26, 7), (-46, 1), (-64, -2), (-84, -10), (-97, -12)], 6)
    g["gully"] = catmull_rom([(-56, -128), (-62, -86), (-57, -44), (-65, -4), (-61, 38), (-70, 82), (-66, 128)], 8)
    r = ramp()
    g["CP"] = [(FARM[0] + 6, FARM[1] + 10), (80.0, 44.0), r[0]] + [r[1], (TOWER_IN[0] - 18.0, TOWER_IN[1] + 6.0), TOWER_IN]
    return g


def crossing(line, other):
    best = None
    for s in range(0, int(polyline_length(line))):
        p, t = point_at(line, float(s))
        d = polyline_dist(p, other)[0]
        if best is None or d < best[0]:
            best = (d, float(s), p, t)
    return best


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
    b = MapBuilder(cat, grid, Frame(W, D), seed=SEED)
    g = geometry()
    paint = GroundPaint()
    b.paint = paint
    rep = {"lines": {}}
    seedc = [1]
    yards = []

    def line(tid, pts, spans=None, tags=()):
        ln = {"pts": [[round(p[0], 3), round(p[1], 3)] for p in pts], "seed": (seedc[0] * 2654435761) % 4294967296}
        seedc[0] += 1
        if spans:
            ln["spans"] = sorted(spans, key=lambda s: s["s"])
        rep["lines"][tid] = rep["lines"].get(tid, 0) + 1
        return b.place("line_" + tid, pts[0][0], pts[0][1], 0.0, check=False, cls="none", extra={"line": ln}, tags={"line", tid} | set(tags),
                       y=b.ground(pts[0][0], pts[0][1]))

    def in_yard(x, z):
        return any(bb[0] <= x <= bb[2] and bb[1] <= z <= bb[3] and in_poly((x, z), p) for p, bb in yards)

    def clear(aid, x, z, yaw, pad=0.8):
        a = cat[aid]
        hx, hz = a.half[0] + pad, a.half[1] + pad
        nx, nz = max(2, int(math.ceil(2 * hx / 3.0)) + 1), max(2, int(math.ceil(2 * hz / 3.0)) + 1)
        for i in range(nx):
            for j in range(nz):
                wx, wz = local_to_world((x, z), yaw, -hx + 2 * hx * i / (nx - 1), -hz + 2 * hz * j / (nz - 1))
                if not grid.in_build(wx, wz) or b.in_keepout(wx, wz) or in_yard(wx, wz) or abs(bluff_d(wx, wz)) < 4.0:
                    return False
        return True

    def district(x, z):
        return min(DISTRICTS, key=lambda k: math.hypot(x - DISTRICTS[k][1][0], z - DISTRICTS[k][1][1]))

    def building(aid, x, z, yaw, tags=(), check=True, wear=1.6):
        e = b.place(aid, x, z, yaw, tags=set(tags) | {"building"}, check=check)
        if e is None:
            return None
        a = cat[aid]
        if a.enterable:
            e["extra"]["finish"] = {"wear": wear}   # TX5: the chipped / stained wall sets, worn hard
            e["extra"]["loot"] = {"size": "large" if a.fw * a.fd >= 200 or int(a.raw.get("floors", 1) or 0) >= 2 else "small", "district": district(x, z)}
            e["extra"]["br"] = dict(e["extra"]["loot"])
            e["open"] = [i["id"] for i in a.raw.get("interactables", []) if i.get("kind") in ("door", "window", "shutters", "gate", "hatch")]
        if pads is not None and a.category in BUILDING_CATS:
            pads.append((aid, x, z, e["yaw"]))
        return e

    # ------------------------------------------------------------------ 1. spawns, routes, keep-outs
    spawn_pts = {"attA": (-24.0, -108.0), "defA": (-30.0, 50.0), "attB": (-14.0, -40.0), "defB": (100.0, 40.0), "attC": (40.0, 30.0),
                 "defC": (-40.0, 112.0), "teamA": (-8.0, -114.0), "teamB": (58.0, 116.0)}
    for p in spawn_pts.values():
        b.keepout_poly(circle_poly(p[0], p[1], 8.0))
    b.route("lane", g["LB"], 4.2)
    b.route("lane", g["LA"], 3.6)
    b.route("footpath", g["BP"], 2.4)
    b.route("footpath", g["CP"], 2.2)
    b.route("lane", [(SQUARE[0] - 1, SQUARE[1]), (SQUARE[0] + 1, SQUARE[1])], 26.0)
    b.route("footpath", [(LONE[0] + 2, LONE[1] + 6), g["BP"][-1]], 2.0)
    b.keepout_line(g["gully"], 8.0)
    b.keepout_poly(rect(YARD[0] - 1, YARD[1] - 1, YARD[2] + 1, YARD[3] + 1))
    b.keepout_poly(circle_poly(FARM[0], FARM[1], 22.0))
    b.keepout_poly(circle_poly(TOWER[0], TOWER[1], 9.0))
    lanes = [g["LB"], g["LA"], g["BP"]]

    # ------------------------------------------------------------------ 2. church and graveyard (top of the hill)
    church = building("vil_church", CHURCH[0], CHURCH[1], 180.0, tags={"church"}, check=False, wear=1.4)
    x0, z0, x1, z1 = YARD
    gx = CHURCH[0] - 2.0   # the gate, where the upper lane arrives
    line("garden_wall", [(x0, z0), (x1, z0), (x1, z1), (x0, z1), (x0, z0)],
         spans=[{"s": gx - x0, "w": 3.6, "kind": "gap"}, {"s": (x1 - x0) * 2 + (z1 - z0) + 10.0, "w": 2.2, "kind": "gap"}], tags={"graveyard"})
    graves = ["drs_gravestone_round", "drs_gravestone_cross", "drs_tomb_chest", "drs_gravestone_round"]
    for zz, xs in ((85.0, range(-7, 38, 2)), (88.5, range(-7, 38, 2)), (103.0, range(-7, 38, 2)), (106.5, range(-6, 37, 2)), (110.0, range(-6, 37, 2))):
        for xx in xs:
            if abs(xx - gx) < 3.0 or R.random() < 0.3:
                continue
            b.place(R.choice(graves), xx + R.uniform(-0.3, 0.3), zz, 180.0 + R.uniform(-9, 9), tags={"graveyard"})
    b.place("drs_churchyard_cross", gx - 6.0, 90.0, 180.0, tags={"graveyard"})
    for p in ((x0 + 2.5, 95.0), (x1 - 2.5, 98.0), (x0 + 6.0, z1 - 2.5), (x1 - 8.0, z1 - 2.0)):
        b.place(R.choice(DEAD), p[0], p[1], R.uniform(0, 360), tags={"graveyard"}, **TREEKW)
    for k in range(14):   # leaves, branches, a fallen urn of a basket, roots between the graves
        b.try_place(R.choice(["ph_dry_branches_medium_01_a", "ph_dry_branches_medium_01_b", "ph_root_cluster_02_b", "ph_wicker_basket_02",
                              "ph_ceramic_vase_03", "ph_rock_moss_set_01_rock04", "ph_lantern_01"]),
                    R.uniform(x0 + 1.5, x1 - 1.5), R.uniform(z0 + 1.5, z1 - 1.5), R.uniform(0, 360), R, tags={"graveyard", "dressing"})
    paint.poly("leaf_litter", rect(x0, z0, x1, z1), weight=0.75, edge=3.0, ragged=2.0, seed=11)

    # ------------------------------------------------------------------ 3. the farmyard: barns round a muddy yard, pens
    farm = []
    for aid, lx, lz, yw in (("vil_stone_barn", 0.0, 14.0, 180.0), ("vil_cottage_barn", 18.0, -6.0, 270.0), ("ruin_barn_burnt", -14.0, -12.0, 60.0)):
        wx, wz = FARM[0] + lx, FARM[1] + lz
        e = building(aid, wx, wz, yw, tags={"farm"}, check=False, wear=1.8)
        if e:
            farm.append(e)
    pens = [rect(FARM[0] - 6, FARM[1] - 16, FARM[0] + 4, FARM[1] - 7), rect(FARM[0] + 5, FARM[1] - 16, FARM[0] + 12, FARM[1] - 9),
            rect(FARM[0] - 21, FARM[1] + 1, FARM[0] - 12, FARM[1] + 10)]
    for k, p in enumerate(pens):
        pts = p + [p[0]]
        L = polyline_length(pts)
        line("post_wire_fence" if k != 1 else "pa_fence_picket", pts, spans=[{"s": L * 0.13, "w": 1.8, "kind": "gap"}], tags={"farm", "pen"})
        paint.poly("mud", p, weight=0.95, edge=1.0, ragged=0.6, seed=40 + k)
        cx, cz = sum(q[0] for q in p) / 4, sum(q[1] for q in p) / 4
        for aid in R.sample(["ph_wooden_bucket_01", "ph_wine_barrel_01", "pa_fruit_crate_low", "ph_wooden_barrels_01", "ph_old_tyre", "pa_crate_slatted"], 3):
            b.try_place(aid, cx + R.uniform(-2.5, 2.5), cz + R.uniform(-2.5, 2.5), R.uniform(0, 360), R, tags={"farm", "pen"}, radius=2.0)
    paint.poly("mud", circle_poly(FARM[0], FARM[1], 16.0, 20), weight=0.9, edge=4.0, ragged=3.0, seed=4)
    paint.poly("dirt", circle_poly(FARM[0], FARM[1], 22.0, 20), weight=0.6, edge=4.0, ragged=3.0, seed=5)
    for aid in ("ph_wooden_barrels_01", "ph_wooden_barrels_01", "sd_static_truck_cargo", "ph_wooden_ladder", "ph_rusted_wheel_rim_01",
                "ph_rusted_wheel_rim_02", "pa_chest_wooden", "ph_wooden_picnic_table", "pa_fruit_crate_mid", "pa_fruit_crate_high", "ph_hand_truck",
                "ph_metal_jerrycan", "ph_wooden_crate_02", "ph_dead_tree_trunk_02", "ph_compost_bags"):
        b.try_place(aid, FARM[0] + R.uniform(-13, 13), FARM[1] + R.uniform(-9, 9), R.uniform(0, 360), R, tags={"farm", "dressing"})

    # ------------------------------------------------------------------ 4. houses along the two lanes
    deck = Deck(HOUSES, R)
    houses = []

    def frontage(street, half, side, s0, s1, tags=(), setback=(1.4, 2.0), gap=(0.2, 1.6), region=None):
        s, s1 = s0, min(s1, polyline_length(street))
        while s < s1 and len(houses) < MAX_HOUSES:
            if region and not region(*point_at(street, s)[0]):
                s += 2.0
                continue
            done = None
            for k in range(6):
                aid = deck.peek(k)
                a = cat[aid]
                sm = s + a.fw * 0.5
                if sm + a.fw * 0.5 > s1:
                    continue
                (px, pz), t = point_at(street, sm)
                nx, nz = -t[1] * side, t[0] * side
                off = half + R.uniform(*setback) + a.fd * 0.5
                x, z = px + nx * off, pz + nz * off
                yaw = asset_yaw_facing(-nx, -nz) + R.uniform(-7.0, 7.0)
                if not clear(aid, x, z, yaw, pad=0.3):
                    continue
                e = building(aid, x, z, yaw, tags=set(tags) | {"house"})
                if e:
                    deck.take(k)
                    houses.append(e)
                    done = a
                    break
            s += (done.fw + R.uniform(*gap)) if done else 3.0

    core = lambda x, z: CORE_R0 < math.hypot(x - SQUARE[0], z - SQUARE[1]) < CORE_R   # the village core: terraces along the lanes
    sqr = circle_poly(SQUARE[0], SQUARE[1], SQUARE_R + 2.0, 40)
    sqr = sqr + [sqr[0]]
    (px, pz), t = point_at(sqr, 3.0)
    out = 1 if (-t[1]) * (px - SQUARE[0]) + t[0] * (pz - SQUARE[1]) > 0 else -1
    frontage(sqr, 0.0, out, 0.0, 1e9, tags={"square"}, setback=(0.2, 0.7), gap=(0.2, 0.9))
    for st, half, side in ((g["LB"], 2.1, 1), (g["LB"], 2.1, -1), (g["LA"], 1.8, 1), (g["LA"], 1.8, -1)):
        frontage(st, half, side, 0.0, 1e9, tags={"LB" if st is g["LB"] else "LA"}, region=core)
    lone = building("pa_military_house", LONE[0], LONE[1], asset_yaw_facing(1.0, 0.3), tags={"lone"}, check=False, wear=1.8)
    # the PR10 derelicts (imported models): an abandoned stone house and log house on the outskirts, a shack by the back
    # path, the long shed behind the farmyard; enterable, looted like the houses
    derelicts = []
    for aid, (tx, tz) in (("pa_stone_house_ruin", (22.0, -62.0)), ("pa_log_house_ruin", (-34.0, 34.0)), ("pa_shack_ruin", (-30.0, -22.0)),
                          ("pa_shed_ruin_long", (FARM[0] + 2.0, FARM[1] - 34.0))):
        for k in range(30):
            x, z = (tx, tz) if k == 0 else (tx + R.uniform(-1, 1) * (3 + k), tz + R.uniform(-1, 1) * (3 + k))
            (d_, s_, _), ln = min(((polyline_dist((x, z), l_), l_) for l_ in lanes), key=lambda r: r[0][0])
            yaw = asset_yaw_facing(*(lambda p_: (p_[0] - x, p_[1] - z))(point_at(ln, s_)[0])) + R.uniform(-12, 12)
            if clear(aid, x, z, yaw, pad=1.5):
                e = building(aid, x, z, yaw, tags={"derelict"}, wear=1.9)
                if e:
                    derelicts.append(e)
                    break
    rep["derelicts"] = [e["asset"] for e in derelicts]
    rep["houses"] = len(houses)
    # back yards 7 m behind every house: a picket fence (gap at the back gate), props by the door
    for e in houses:
        a = cat[e["asset"]]
        hx, hz = a.half
        x, z, yaw = e["pos"][0], e["pos"][2], e["yaw"]
        Lw = lambda lx, lz: local_to_world((x, z), yaw, lx, lz)
        p = [Lw(-hx, -hz), Lw(hx, -hz), Lw(hx, -hz - 7.0), Lw(-hx, -hz - 7.0)]
        yards.append((p, (min(q[0] for q in p), min(q[1] for q in p), max(q[0] for q in p), max(q[1] for q in p))))
        fence = [Lw(hx - 0.2, -hz - 0.3), Lw(hx - 0.2, -hz - 7.0), Lw(-hx + 0.2, -hz - 7.0), Lw(-hx + 0.2, -hz - 0.3)]
        if not any(b.in_keepout(*q) for q in fence):
            Lf = polyline_length(fence)
            line(R.choice(["pa_fence_picket", "pa_fence_picket", "post_wire_fence"]), fence, spans=[{"s": Lf * 0.5 + R.uniform(-1.5, 1.5), "w": 1.4, "kind": "gap"}], tags={"yard"})
        paint.poly("mud", p, weight=0.7, edge=1.2, ragged=0.8, seed=int(x * 3 + z))
        for aid in R.sample(YARD_PROPS, 4):
            q = Lw(R.uniform(-hx + 1.2, hx - 1.2), -hz - R.uniform(1.0, 5.8))
            b.try_place(aid, q[0], q[1], yaw + R.uniform(-40, 40), R, tags={"yard", "dressing"}, radius=1.5)
        if R.random() < 0.5:
            q = Lw(R.choice((-1, 1)) * (hx - 1.8), -hz - 4.5)
            b.place(R.choice(DEAD), q[0], q[1], R.uniform(0, 360), tags={"yard"}, **TREEKW)
        for k in range(3):   # by the front door: a barrel, a crate, a bench
            q = Lw(R.uniform(-hx + 0.8, hx - 0.8), hz + R.uniform(0.6, 1.1))
            b.try_place(R.choice(["ph_wine_barrel_01", "pa_crate_slatted", "ph_painted_wooden_bench", "ph_wooden_bucket_02", "ph_wicker_basket_01",
                                  "pa_fruit_crate_low", "ph_wooden_broom"]), q[0], q[1], yaw, R, tags={"door", "dressing"}, radius=0.8)

    # ------------------------------------------------------------------ 5. the square: well, bonfire pile, market leftovers
    sx, sz = SQUARE
    # levelled (build() flattens it), cobbles with a hard edge and a kerb of pebbles, the well in the middle
    paint.poly("mud", circle_poly(sx, sz, SQUARE_R, 40), weight=0.8, edge=0.5, ragged=0.5, seed=1)   # packed earth (MP1b: paving read modern)
    paint.line("pebbles", circle_poly(sx, sz, SQUARE_R + 0.4, 40) + [circle_poly(sx, sz, SQUARE_R + 0.4, 40)[0]], 0.9, weight=0.9, edge=0.3)
    for k in range(6):   # puddles in the hollows
        a, r = R.uniform(0, 2 * math.pi), R.uniform(4, SQUARE_R - 2)
        paint.poly("mud", circle_poly(sx + r * math.cos(a), sz + r * math.sin(a), R.uniform(0.8, 1.6), 10), weight=0.9, edge=0.4, ragged=0.3, seed=60 + k)
    b.place("drs_well", sx, sz, 20.0, tags={"square"}, check=False)
    bon = (sx - 6.5, sz - 4.0)
    b.place("ph_stone_fire_pit", bon[0], bon[1], 0.0, tags={"square", "bonfire"}, check=False)
    for ring, n, rr in ((0, 10, 1.0), (1, 14, 1.9)):   # the pile: two rings of trunks and branches round the pit, stones at its foot
        for k in range(n):
            a = 2 * math.pi * (k + 0.5 * ring) / n
            aid = (["ph_dead_tree_trunk", "ph_dry_branches_medium_01_a", "ph_dry_branches_medium_01_c"] if ring == 0 else
                   ["ph_dead_tree_trunk_02", "ph_dry_branches_medium_01_b", "ph_dead_tree_trunk", "ph_rock_09"])[k % (3 if ring == 0 else 4)]
            b.place(aid, bon[0] + rr * math.cos(a), bon[1] + rr * math.sin(a), math.degrees(-a) + 90 + R.uniform(-15, 15), tags={"square", "bonfire"}, check=False)
    for k, (aid, ang, r) in enumerate((("ph_wooden_picnic_table", 0.4, 8.5), ("ph_wooden_picnic_table", 1.5, 8.5), ("ph_wooden_picnic_table", 4.6, 8.0))):
        q = (sx + r * math.cos(ang), sz + r * math.sin(ang))   # stalls: trestle tables with crates of rotten fruit, baskets on and under them
        tb = b.place(aid, q[0], q[1], math.degrees(-ang), tags={"square", "stall"}, check=False)
        if tb:
            for j in range(3):
                b.place(R.choice(["pa_fruit_crate_low", "pa_fruit_crate_mid", "pa_fruit_crate_high", "ph_wicker_basket_01"]), q[0] + R.uniform(-0.6, 0.6),
                        q[1] + R.uniform(-0.6, 0.6), R.uniform(0, 360), check=False, cls="none", y=tb["pos"][1] + 0.76, tags={"square", "stall"}, support=tb)
            for j in range(3):
                b.try_place(R.choice(["pa_fruit_crate_high", "pa_crate_slatted", "ph_wicker_basket_02", "ph_wooden_bucket_02"]), q[0] + R.uniform(-2.2, 2.2),
                            q[1] + R.uniform(-2.2, 2.2), R.uniform(0, 360), R, tags={"square", "stall"}, radius=1.0)
    for ang in (2.3, 3.2, 5.6):   # benches facing the well
        b.try_place("ph_painted_wooden_bench", sx + 5.5 * math.cos(ang), sz + 5.5 * math.sin(ang), math.degrees(-ang) - 90, R, tags={"square"}, radius=0.8)
    b.try_place("ph_standing_chalkboard_01", sx + 3.0, sz - 5.5, 160.0, R, tags={"square", "notice"}, radius=1.0)   # the notice board
    for k in range(10):   # barrels, crates, a ladder round the edge
        a = 2 * math.pi * k / 10 + 0.15
        b.try_place(R.choice(["ph_wooden_barrels_01", "ph_wine_barrel_01", "pa_crate_slatted", "ph_wooden_ladder", "pa_chest_wooden", "ph_wooden_crate_02"]),
                    sx + (SQUARE_R - 1.0) * math.cos(a), sz + (SQUARE_R - 1.0) * math.sin(a), math.degrees(-a), R, tags={"square", "dressing"}, radius=1.2)
    for e in [e for e in houses if "square" in e["tags"]]:   # firewood stacked against the house fronts
        a = cat[e["asset"]]
        for j in range(3):
            q = local_to_world((e["pos"][0], e["pos"][2]), e["yaw"], R.uniform(-a.half[0] + 1.0, a.half[0] - 1.0), a.half[1] + 0.5)
            b.place(R.choice(["ph_dead_tree_trunk", "ph_dead_tree_trunk_02", "ph_dry_branches_medium_01_a"]), q[0], q[1], e["yaw"] + 90 + R.uniform(-6, 6),
                    tags={"square", "firewood"}, check=False)

    # ------------------------------------------------------------------ 6. gully, footbridge, cliff, watchtower
    d, s, p, t = crossing(g["BP"], g["gully"])
    bridge = b.place("mp1_footbridge", p[0], p[1], asset_yaw_facing(*t), check=False, cls="none", tags={"bridge"}, y=BRIDGE_Y[0])
    rep["bridge"] = [round(p[0], 1), round(p[1], 1), round(BRIDGE_Y[0], 2)]
    tower = building("mp1_watchtower", TOWER[0], TOWER[1], TOWER_YAW, tags={"tower"}, check=False, wear=1.0)
    face = [(BLUFF[0][0] + (BLUFF[1][0] - BLUFF[0][0]) * k / 14.0, BLUFF[0][1] + (BLUFF[1][1] - BLUFF[0][1]) * k / 14.0) for k in range(15)]
    rp = ramp()
    for q in face:   # rock outcrops dressing the cliff face (not on the cut path)
        if polyline_dist(q, rp)[0] < 5.0 or not grid.in_build(*q):
            continue
        b.place(R.choice(["ph_rock_face_01", "ph_rock_face_01", "ph_rock_moss_set_02_rock10", "ph_rock_moss_set_02_rock12", "ph_boulder_01"]),
                q[0] + R.uniform(-1, 1), q[1] + R.uniform(-1, 1), asset_yaw_along(BLUFF[1][0] - BLUFF[0][0], BLUFF[1][1] - BLUFF[0][1]) + R.uniform(-25, 25),
                tags={"cliff"}, check=False)
    for k in range(10):   # loose stones and roots along the path
        q, tt = point_at(rp, polyline_length(rp) * (k + 0.5) / 10)
        b.try_place(R.choice(["ph_rock_moss_set_01_rock05", "ph_root_cluster_02_d", "ph_rock_moss_set_01_rock03", "ph_dry_branches_medium_01_b"]),
                    q[0] + tt[1] * 2.2, q[1] - tt[0] * 2.2, R.uniform(0, 360), R, tags={"cliff", "dressing"}, radius=1.0)
    for aid in ("pa_chest_wooden", "ph_lantern_01", "ph_wooden_bucket_02", "pa_crate_slatted", "ph_wooden_crate_02"):
        b.try_place(aid, TOWER[0] + R.uniform(-5, 5), TOWER[1] + R.uniform(-6, -3), R.uniform(0, 360), R, tags={"tower", "dressing"})
    for q in resample_pts(g["gully"], 9.0):   # the gully: fallen trunks, roots, mossy rocks on its floor and banks
        if R.random() < 0.6:
            b.place(R.choice(["ph_rock_moss_set_01_rock01", "ph_rock_moss_set_02_rock07", "ph_root_cluster_02_e", "ph_dead_tree_trunk_02",
                              "ph_fern_02_b"]), q[0] + R.uniform(-3, 3), q[1] + R.uniform(-3, 3), R.uniform(0, 360), tags={"gully"}, check=False)

    lanes_all = [g["LB"], g["LA"], g["BP"], g["CP"]]

    def on_solid(q, pad):   # q inside a solid footprint grown by pad
        for e in b.near(q[0], q[1], 16.0, classes={"solid"}):
            a = cat[e["asset"]]
            r = math.radians(-e["yaw"])
            dx, dz = q[0] - e["pos"][0], q[1] - e["pos"][2]
            lx, lz = dx * math.cos(r) + dz * math.sin(r), -dx * math.sin(r) + dz * math.cos(r)
            if abs(lx) <= a.half[0] + pad and abs(lz) <= a.half[1] + pad:
                return True
        return False

    # ------------------------------------------------------------------ 7. lane edges: something every few metres
    nlane = 0
    for ln, half in ((g["LB"], 2.1), (g["LA"], 1.8), (g["BP"], 1.2), (g["CP"], 1.1)):
        Ltot = polyline_length(ln)
        s = R.uniform(2, 5)
        while s < Ltot - 2:
            (px, pz), t = point_at(ln, s)
            side = R.choice((-1, 1))
            q = (px - t[1] * side * (half + R.uniform(0.9, 1.8)), pz + t[0] * side * (half + R.uniform(0.9, 1.8)))
            if math.hypot(q[0] - SQUARE[0], q[1] - SQUARE[1]) > SQUARE_R + 2:
                nlane += b.try_place(R.choice(LANE), q[0], q[1], asset_yaw_along(*t) + R.uniform(-30, 30), R, tags={"lane_edge", "dressing"}, radius=1.2) is not None
            s += R.uniform(2.5, 4.5) if math.hypot(px - SQUARE[0], pz - SQUARE[1]) < CORE_R else R.uniform(6.0, 10.0)
    rep["laneProps"] = nlane
    # the lanes: packed earth with a crisp edge, two wheel ruts, puddles, a pebble margin; low walls and fences along the
    # open stretches (not in front of houses), gaps every ~15 m; loose stones along the verge
    nedge = 0
    for ln, half in ((g["LB"], 2.1), (g["LA"], 1.8)):
        paint.line("mud", ln, 2 * half + 0.4, weight=0.8, edge=0.25)
        for o in (-0.75, 0.75):
            paint.line("mud", offset_polyline(ln, o), 0.5, weight=1.0, edge=0.15)
        for side in (-1, 1):
            paint.line("pebbles", offset_polyline(ln, side * (half + 0.45)), 0.55, weight=0.8, edge=0.2)
        for q in resample_pts(ln, 8.5)[1:]:
            paint.poly("mud", circle_poly(q[0] + R.uniform(-0.6, 0.6), q[1] + R.uniform(-0.6, 0.6), R.uniform(0.6, 1.3), 10), weight=0.95, edge=0.3, ragged=0.3,
                       seed=int(q[0] * 13 + q[1]))
        for side in (-1, 1):
            edge_ln = offset_polyline(ln, side * (half + 1.1))
            run = []
            for q in resample_pts(edge_ln, 1.5) + [None]:
                ok = q is not None and grid.in_build(*q) and not on_solid(q, 1.2) and math.hypot(q[0] - SQUARE[0], q[1] - SQUARE[1]) > SQUARE_R + 4 \
                    and not in_poly(q, rect(*YARD)) and min(polyline_dist(q, o_)[0] for o_ in lanes_all if o_ is not ln) > 3.5 \
                    and all(math.hypot(q[0] - sp[0], q[1] - sp[1]) > 9 for sp in spawn_pts.values()) and polyline_dist(q, g["gully"])[0] > 8
                if ok:
                    run.append(q)
                    continue
                if len(run) >= 5:
                    Lr = polyline_length(run)
                    spans = [{"s": s_, "w": 1.8, "kind": "gap"} for s_ in [x_ * 15.0 + 7.0 for x_ in range(int(Lr // 15.0))]]
                    line(R.choice(["stone_wall", "stone_wall", "pa_fence_picket", "post_wire_fence"]), run, spans=spans, tags={"lane_wall"})
                    nedge += 1
                run = []
            for q in resample_pts(offset_polyline(ln, side * (half + 0.55)), 3.2)[1:]:
                if math.hypot(q[0] - SQUARE[0], q[1] - SQUARE[1]) > SQUARE_R + 1 and R.random() < 0.6:
                    b.place(R.choice(["ph_rock_09", "ph_rock_09", "ph_rock_moss_set_01_rock05", "ph_rock_moss_set_01_rock03"]), q[0] + R.uniform(-0.2, 0.2),
                            q[1] + R.uniform(-0.2, 0.2), R.uniform(0, 360), tags={"verge"}, cls="flat", ok_with={"flat", "none", "veg", "wire"})
    rep["laneWalls"] = nedge
    paint.line("dirt", g["BP"], 2.2, weight=0.8, edge=0.8)
    paint.line("dirt", g["CP"], 2.0, weight=0.8, edge=0.8)

    # ------------------------------------------------------------------ 7b. the hillside: dry-stone field walls, paddocks, kitchen gardens
    nwall = 0
    for k in range(40):   # walls along the contour (east-west, gently bent), broken where a lane crosses and at gateways
        if nwall >= 11:
            break
        cx, cz = R.uniform(-HX * 0.66, HX * 0.66), R.uniform(-HZ * 0.66, HZ * 0.62)
        Lw_ = R.uniform(28, 55)
        ang = math.radians(R.uniform(-20, 20) + (90 if R.random() < 0.35 else 0))
        dx, dz = math.cos(ang), math.sin(ang)
        pts = [(cx - dx * Lw_ / 2, cz - dz * Lw_ / 2), (cx + R.uniform(-3, 3), cz + R.uniform(-3, 3)), (cx + dx * Lw_ / 2, cz + dz * Lw_ / 2)]
        bad = lambda q: b.near(q[0], q[1], 5.0, classes={"solid"}) or in_yard(*q) or not grid.in_build(*q) or abs(bluff_d(*q)) < 6             or polyline_dist(q, g["gully"])[0] < 9 or math.hypot(q[0] - SQUARE[0], q[1] - SQUARE[1]) < 16 or in_poly(q, rect(*YARD))             or math.hypot(q[0] - FARM[0], q[1] - FARM[1]) < 23 or any(math.hypot(q[0] - sp[0], q[1] - sp[1]) < 9 for sp in spawn_pts.values())
        samp = [point_at(pts, polyline_length(pts) * t / 12.0)[0] for t in range(13)]
        if any(bad(q) for q in samp):
            continue
        spans = []
        for ln in lanes_all:
            for t in range(13):
                if polyline_dist(samp[t], ln)[0] < 4.0:
                    spans.append({"s": polyline_length(pts) * t / 12.0, "w": 6.0, "kind": "gap"})
        spans.append({"s": polyline_length(pts) * R.uniform(0.25, 0.75), "w": 2.4, "kind": "gap"})   # a field gateway
        line(R.choice(["stone_wall", "stone_wall", "post_wire_fence"]), pts, spans=spans, tags={"field"})
        nwall += 1
        for q in samp[1::3]:   # nettles of the imported kind: grass tufts, branches, a rock against the wall
            b.try_place(R.choice(["ph_grass_medium_01_tall_c", "ph_dry_branches_medium_01_b", "ph_rock_moss_set_01_rock02", "ph_fern_02_d"]),
                        q[0] + R.uniform(-1.5, 1.5), q[1] + R.uniform(-1.5, 1.5), R.uniform(0, 360), R, tags={"field", "dressing"}, radius=1.0,
                        cls="flat", ok_with={"flat", "none", "veg", "wire"})
    rep["fieldWalls"] = nwall
    ngard = 0
    for e in houses + [lone] if lone else houses:   # a kitchen garden beside most houses: dug beds, a picket fence, the tools
        if R.random() < 0.25:
            continue
        a = cat[e["asset"]]
        hx, hz = a.half
        Lg = lambda lx, lz: local_to_world((e["pos"][0], e["pos"][2]), e["yaw"], lx, lz)
        sd0 = R.choice((-1, 1))
        for sd in (sd0, -sd0):   # either side of the house, whichever is free
            plot = [Lg(sd * (hx + 1.8), hz - 0.5), Lg(sd * (hx + 7.5), hz - 0.5), Lg(sd * (hx + 7.5), -hz + 0.5), Lg(sd * (hx + 1.8), -hz + 0.5)]
            if not any(b.in_keepout(*q) or b.near(q[0], q[1], 0.8, classes={"solid"}) or not grid.in_build(*q) or in_yard(*q) for q in plot):
                break
        else:
            continue
        paint.poly("ploughed", plot, weight=0.95, edge=0.6, ragged=0.4, seed=int(e["pos"][0] * 7))
        line("pa_fence_picket", plot + [plot[0]], spans=[{"s": 1.6, "w": 1.2, "kind": "gap"}], tags={"garden"})
        cx, cz = sum(q[0] for q in plot) / 4, sum(q[1] for q in plot) / 4
        for aid in R.sample(["ph_seeding_tray_01", "ph_watering_can_metal_01", "ph_rusted_spade_01", "ph_trowel_01", "ph_wooden_bucket_01",
                             "ph_planter_pot_clay", "ph_wicker_basket_02", "ph_compost_bag_02", "ph_garden_sprinkler_01"], 3):
            b.try_place(aid, cx + R.uniform(-2, 2), cz + R.uniform(-2, 2), R.uniform(0, 360), R, tags={"garden", "dressing"}, radius=1.5)
        ngard += 1
    rep["gardens"] = ngard

    # ------------------------------------------------------------------ 8. interiors: imported household things in every room
    nint = 0
    for e in houses + farm + derelicts + [x for x in (church, lone) if x]:
        a = cat[e["asset"]]
        corners = list((a.nav or {}).get("corners", []))
        if not corners:   # rooms without corner points: 0.6 m in from each room corner, on its floor
            for r in (a.nav or {}).get("rooms", []):
                (ax, ay, az), (bx, by, bz) = r["min"], r["max"]
                if bx - ax > 1.6 and bz - az > 1.6:
                    corners += [{"p": [x, ay, z]} for x in (ax + 0.6, bx - 0.6) for z in (az + 0.6, bz - 0.6)]
        R.shuffle(corners)
        for c in corners[:max(4, min(16, len(corners)))]:
            lx, ly, lz = c["p"]
            wx, wz = local_to_world((e["pos"][0], e["pos"][2]), e["yaw"], lx, lz)
            nint += b.place(R.choice(ROOM), wx, wz, e["yaw"] + R.uniform(-30, 30), check=False, cls="none", y=e["pos"][1] + ly,
                            tags={"interior", "dressing"}) is not None
    rep["interiorProps"] = nint

    # ------------------------------------------------------------------ 9. woods on every side, dead trees in the village
    def wood(x, z):   # the forest edge: 70-95 m out (wavy), and the gully banks
        r = max(abs(x) / HX, abs(z) / HZ)
        edge = 0.64 + 0.08 * fbm(x / 30.0, z / 30.0, 3, 2)
        return r > edge and not in_poly((x, z), rect(*YARD))
    nwood = b.scatter(WOOD_TREES, wood, 1 / 55.0, R, bounds=(-HX, -HZ, HX, HZ), tags={"woods"}, min_dist=4.2, **TREEKW)
    b.scatter(UNDER, wood, 1 / 14.0, R, bounds=(-HX, -HZ, HX, HZ), tags={"woods", "under"}, cls="flat", ok_with={"flat", "none", "veg", "wire"})
    paint.poly("leaf_litter", rect(-HX, -HZ, HX, HZ), weight=0.65, edge=1.0, mask=lambda x, z: wood(x, z) or max(abs(x) / HX, abs(z) / HZ) > 0.7)
    village = lambda x, z: not wood(x, z) and not b.in_keepout(x, z) and not in_yard(x, z)
    ndead = b.scatter([(t, 1) for t in DEAD] + [("ph_fir_sapling_medium_b", 1), ("ph_pine_sapling_medium_b", 1)], village, 1 / 420.0, R,
                      bounds=(-HX, -HZ, HX, HZ), tags={"village_trees"}, min_dist=12.0, **TREEKW)
    lived = lambda x, z: village(x, z) and (min(polyline_dist((x, z), l_)[0] for l_ in lanes_all) < 9.0 or bool(b.near(x, z, 11.0, classes={"solid"})))
    b.scatter([("ph_fern_02_c", 2), ("ph_fern_02_a", 1), ("ph_dry_branches_medium_01_c", 1),
               ("ph_root_cluster_02_f", 1), ("ph_rock_moss_set_01_rock06", 1), ("ph_rock_09", 1), ("ph_dead_tree_trunk_02", 1)],
              lived, 1 / 22.0, R, bounds=(-HX, -HZ, HX, HZ), tags={"village_ground"},
              cls="flat", ok_with={"flat", "none", "veg", "wire"})
    paint.poly("leaf_litter", rect(-HX, -HZ, HX, HZ), weight=0.35, edge=1.0, ragged=4.0, seed=77, mask=lambda x, z: fbm(x / 14.0, z / 14.0, 5, 3) > 0.15)
    rep["woodTrees"], rep["villageTrees"] = len(nwood), len(ndead)
    # beyond the border: the forest closes in (decor, outside the navmesh)
    nt = 0
    for _ in range(3000):
        x, z = R.uniform(-HX - 45, HX + 45), R.uniform(-HZ - 45, HZ + 45)
        if abs(x) < HX + 3 and abs(z) < HZ + 3:
            continue
        aid = R.choice([t for t, w in WOOD_TREES for _ in range(w)])
        if b.overlaps(cat[aid], x, z, 0.0, {"flat", "none", "veg"}):
            continue
        nt += b.place(aid, x, z, R.uniform(0, 360), check=False, extra=DECOR, tags={"decor"}, cls="veg") is not None
        if nt >= 320:
            break
    rep["decorTrees"] = nt

    # ------------------------------------------------------------------ 10. scenario
    sc = ScenarioBuilder(b, defending="Axis")
    sc.rules["teamFaction"] = ["Allies", "Axis"]
    near = lambda c, r: [e for e in houses if math.hypot(e["pos"][0] - c[0], e["pos"][2] - c[1]) < r]
    sqh = near(SQUARE, 40)
    sc.objective("Village square", SQUARE[0], SQUARE[1], radius=15.0, capture_time=45.0, attack_from=spawn_pts["attA"],
                 linked=[e["id"] for e in sqh], slots=slots_from_structures(b, sqh))
    sc.objective("Farmyard", FARM[0], FARM[1], radius=16.0, capture_time=45.0, attack_from=spawn_pts["attB"],
                 linked=[e["id"] for e in farm], slots=slots_from_structures(b, farm))
    sc.objective("Church", CHURCH[0], CHURCH[1] - 8.0, radius=16.0, capture_time=50.0, attack_from=spawn_pts["attC"],
                 linked=[church["id"]], slots=slots_from_structures(b, [church]))
    P = spawn_pts
    sc.spawn("attacker", [0], *P["attA"], label="Attackers - south lane")
    sc.spawn("defender", [0], *P["defA"], label="Defenders - upper lane")
    sc.spawn("attacker", [1], *P["attB"], label="Attackers - lower lane")
    sc.spawn("defender", [1], *P["defB"], label="Defenders - under the bluff")
    sc.spawn("attacker", [2], *P["attC"], label="Attackers - east houses")
    sc.spawn("defender", [2], *P["defC"], label="Defenders - north woods")
    for team, key, role, fac in ((0, "teamA", "attacker", "Allies"), (1, "teamB", "defender", "Axis")):
        m = sc.spawn(role, [0], *P[key], face=(0.0, 0.0), faction=fac, label="Team A spawn (south)" if team == 0 else "Team B spawn (north)")
        m["team"], m["phases"] = team, []
    for m in sc.spawns:
        yw = math.radians(m["yaw"])
        b.try_place("crate_mixed_supply", m["pos"][0] + math.sin(yw) * 3.0, m["pos"][2] + math.cos(yw) * 3.0, m["yaw"], R, tags={"supply"}, radius=3.0)
    return b, sc, g, rep


DISTRICTS = {1: ("Square", (0.0, 0.0)), 2: ("Church", (14.0, 92.0)), 3: ("Farmyard", (68.0, 16.0)), 4: ("South lane", (-14.0, -70.0))}
ROYALE = {"storm": {"wait_s": [60, 45, 35, 25], "shrink_s": [30, 25, 20, 15], "dps": [1, 2, 4, 8], "radiusFrac": [0.45, 0.28, 0.16],
                    "finalRadius_m": 10, "driftPause_s": 10, "driftMove_s": 15, "driftMin_m": 6, "driftMax_m": 16},
          "vehicles": 0, "landSpread_m": 50}
BRIDGE_Y = [0.0]


def resample_pts(pts, step):
    L = polyline_length(pts)
    return [point_at(pts, step * k)[0] for k in range(int(L // step) + 1)]


def committed_dirs(repo):
    out = subprocess.run(["git", "-C", repo, "ls-files", "content/assets/models", "content/vehicles/models"], capture_output=True, text=True, check=True).stdout
    return {("vehicles/" if p.startswith("content/vehicles/") else "") + p.split("/")[3] for p in out.splitlines() if p.count("/") >= 4}


def connectivity(b, grid, points, step=2.0, max_slope=40.0):
    """Flood fill over the ground (steps no steeper than max_slope; solid footprints except enterable buildings
    block) from the first point: returns the labels of the points not reached. shortcut: a terrain + footprint
    estimate, not the game's navmesh (baked only at match start); the in-game bake is the real check."""
    cat = b.cat
    blocked = set()
    for e in b.inst:
        a = cat[e["asset"]]
        if e["cls"] != "solid" or a.enterable or "decor" in e["tags"] or a.fw * a.fd < 4.0:
            continue
        hx, hz = a.half
        for i in range(int(2 * hx // step) + 1):
            for j in range(int(2 * hz // step) + 1):
                wx, wz = local_to_world((e["pos"][0], e["pos"][2]), e["yaw"], -hx + i * step + step / 2, -hz + j * step + step / 2)
                blocked.add((int(math.floor(wx / step)), int(math.floor(wz / step))))
    for e in b.tagged("bridge"):   # the footbridge deck spans the gully
        a = cat[e["asset"]]
        for k in range(-int(a.half[1] // step), int(a.half[1] // step) + 1):
            wx, wz = local_to_world((e["pos"][0], e["pos"][2]), e["yaw"], 0.0, k * step)
            blocked.discard((int(math.floor(wx / step)), int(math.floor(wz / step))))
    bridge_cells = set()
    for e in b.tagged("bridge"):
        a = cat[e["asset"]]
        for k in range(-int(a.half[1] // step) - 1, int(a.half[1] // step) + 2):
            wx, wz = local_to_world((e["pos"][0], e["pos"][2]), e["yaw"], 0.0, k * step)
            bridge_cells.add((int(math.floor(wx / step)), int(math.floor(wz / step))))
    lim = math.tan(math.radians(max_slope)) * step
    cell = lambda p: (int(math.floor(p[0] / step)), int(math.floor(p[1] / step)))
    start = cell(points[0][1])
    seen, q = {start}, [start]
    nx0, nx1, nz0, nz1 = int(-HX // step), int(HX // step), int(-HZ // step), int(HZ // step)
    while q:
        i, j = q.pop()
        h0 = grid.sample((i + 0.5) * step, (j + 0.5) * step)
        for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            n = (i + di, j + dj)
            if n in seen or not (nx0 <= n[0] < nx1 and nz0 <= n[1] < nz1) or n in blocked:
                continue
            h1 = grid.sample((n[0] + 0.5) * step, (n[1] + 0.5) * step)
            if abs(h1 - h0) > lim and not (n in bridge_cells and (i, j) in bridge_cells):
                continue
            seen.add(n)
            q.append(n)
    def reached(p):   # the point, or free ground within 4 m of it (a cart parked on an objective's centre)
        ci, cj = cell(p)
        return any((ci + di, cj + dj) in seen for di in range(-2, 3) for dj in range(-2, 3))
    return [lab for lab, p in points if not reached(p)], len(seen) * step * step


def build(ctx):
    cat = ctx.catalog
    grid = Grid(W, D, margin=MARGIN)
    grid.apply(lambda x, z, h: base_height(x, z))
    g = geometry()
    gl = g["gully"]
    L = polyline_length(gl)
    fl = [grid.sample(*point_at(gl, L * i / 120.0)[0]) - 4.2 for i in range(121)]
    stamp_valley(grid, gl, floor=lambda u: fl[min(120, max(0, int(round(u * 120))))], top_half=lambda u: 7.5, floor_half=lambda u: 1.6)
    # the footbridge abutments: both banks levelled to one height, the deck 0.35 m above it
    d, s, p, t = crossing(g["BP"], gl)
    ends = [(p[0] + t[0] * k, p[1] + t[1] * k) for k in (-7.0, 7.0)]
    BRIDGE_Y[0] = max(grid.sample(*q) for q in ends)
    for k in (-1, 1):
        cx, cz = p[0] + t[0] * 8.0 * k, p[1] + t[1] * 8.0 * k
        stamp_flatten_poly(grid, [(cx - 2.5, cz - 2.5), (cx + 2.5, cz - 2.5), (cx + 2.5, cz + 2.5), (cx - 2.5, cz + 2.5)], height=BRIDGE_Y[0], falloff=3.0)
    sq_h = grid.sample(*SQUARE)
    stamp_flatten_poly(grid, circle_poly(SQUARE[0], SQUARE[1], SQUARE_R + 3.0, 32), height=sq_h, falloff=7.0)   # the square: level
    pads = []
    populate(ctx, grid, pads)
    for aid, x, z, yaw in pads:
        a = cat[aid]
        hx, hz = a.half[0] + 1.2, a.half[1] + 1.2
        poly = [local_to_world((x, z), yaw, lx, lz) for lx, lz in ((-hx, -hz), (hx, -hz), (hx, hz), (-hx, hz))]
        stamp_flatten_poly(grid, poly, height=grid.sample(x, z), falloff=4.0)
    stamp_flatten_poly(grid, circle_poly(SQUARE[0], SQUARE[1], SQUARE_R + 1.0, 32), height=sq_h, falloff=2.0)   # again: the house pads bled into it
    grid.quantise()
    b, sc, g, rep = populate(ctx, grid)
    err, warn = sc.validate()
    sj = sc.to_json()
    sj["districts"] = [{"id": k, "name": n, "pos": [c[0], round(b.ground(*c), 2), c[1]], "radius": 40.0} for k, (n, c) in DISTRICTS.items()]

    # ------------------------------------------------------------------ self-checks
    dirs = committed_dirs(ctx.repo)
    model_dir = lambda aid: (lambda m: m.get("dir") and (("vehicles/" if m.get("root") == "vehicles" else "") + m["dir"]))(cat[aid].raw.get("model") or {})
    for e in [e for e in b.inst if "companion" in e["tags"] and model_dir(e["asset"]) and model_dir(e["asset"]) not in dirs]:
        b.remove(e)   # a ruin's local-only debris companion (a box stand-in in a clean checkout)
    for e in [e for e in b.inst if "companion" in e["tags"] and not model_dir(e["asset"]) and e["asset"].startswith(GEN_DECOR)]:
        b.remove(e)   # the houses' own generated garden dressing: decoration on this map is imported models only
    imported = {e["asset"] for e in b.inst if model_dir(e["asset"])}
    local_only = sorted(a for a in imported if model_dir(a) not in dirs)
    assert not local_only, "imported models that are not committed: %s" % local_only
    pts = [(m["label"], (m["pos"][0], m["pos"][2])) for m in sc.spawns] + [(o["displayName"], (o["pos"][0], o["pos"][2])) for o in sc.objectives]
    pts += [("watchtower", TOWER_IN), ("lone house", (LONE[0] + 3, LONE[1] + 7))]
    unreached, area = connectivity(b, grid, pts)
    assert not unreached, "not reachable on foot from %s: %s" % (pts[0][0], unreached)
    rep["walkableM2"] = area

    # ------------------------------------------------------------------ output
    tdir = ctx.templates
    grid.write_png(os.path.join(tdir, TID + ".height.png"))
    eye = lambda x, z: [round(x, 2), round(b.ground(x, z) + 1.7, 2), round(z, 2)]
    cam = ([0.0, 120.0, -205.0], 180.0, -30.0)
    thumb_cam = ([-60.0, 70.0, -150.0], look_yaw(60.0, 150.0), -18.0)
    lb, lbt = point_at(g["LB"], 52.0)
    la = point_at(g["LA"], polyline_length(g["LA"]) - 12.0)[0]
    br = b.tagged("bridge")[0]
    views = [
        {"name": "overview_high", "pos": [10.0, 125.0, -175.0], "yaw": 180.0, "pitch": -36.0},
        {"name": "square_eye", "pos": eye(-8.0, -8.5), "yaw": look_yaw(8.0, 9.0), "pitch": 2.0, "gameplay": True},
        {"name": "lane_eye", "pos": eye(lb[0] - lbt[1] * 1.0, lb[1] + lbt[0] * 1.0), "yaw": look_yaw(*lbt), "pitch": 1.0, "gameplay": True},
        {"name": "church_eye", "pos": eye(*la), "yaw": look_yaw(CHURCH[0] - la[0], CHURCH[1] - la[1]), "pitch": 9.0, "gameplay": True},
        {"name": "preview", "pos": thumb_cam[0], "yaw": thumb_cam[1], "pitch": thumb_cam[2]},   # the New Map thumbnail (copied from this capture)
        {"name": "bridge_eye", "pos": eye(br["pos"][0] + 13.0, br["pos"][2] + 4.0), "yaw": look_yaw(-13.0, -4.0), "pitch": -2.0, "gameplay": True},
    ]
    look = {"style": "abandoned", "season": "autumn", "haze": 2.6, "weather": 0.9, "clouds": 0.95, "envFog": 2, "bloom": 0.7, "saturation": 0.68,
            "tint": [0.94, 0.97, 1.0], "grime": 1.6, "shafts": 0.4,
            "paintSets": {"ploughed": "moss_floor_paint"}}
    write_map(os.path.join(tdir, TID + ".nmap.json"), name=NAME, template_id=TID, template_version=VERSION, seed=SEED, width=W, depth=D,
              builder=b, scenario_json=sj, explore_start=((views[1]["pos"][0], views[1]["pos"][1] - 1.7, views[1]["pos"][2]), views[1]["yaw"]), camera=cam,
              bookmarks={str(k): {"position": [c[0], b.ground(*c) + 45.0, c[1] - 55.0], "yaw": 180.0, "pitch": -35} for k, (n, c) in DISTRICTS.items()},
              extra={"groundPaint": b.paint.to_json(), "look": look, "royale": ROYALE,
                     "lighting": {"hour": 16.4, "enclosed": False, "fill": 0.9, "ambient": [0.03, 0.035, 0.04]}})
    # open doors, casements and shutters (overrides.interactables startsOpen) on every enterable building
    path = os.path.join(tdir, TID + ".nmap.json")
    with open(path, encoding="utf-8") as f:
        mj = json.load(f)
    opened = {e["id"]: e.get("open", []) for e in b.inst if e.get("open")}
    nopen = 0
    for o in mj["instances"]:
        for k in opened.get(o["id"], []):
            o["overrides"]["interactables"][k] = {"startsOpen": True}
            nopen += 1
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(mj, f, separators=(",", ":"))
    rep["openedInteractables"] = nopen
    write_template(os.path.join(tdir, TID + ".json"), tid=TID, name=NAME, order=32, version=VERSION, seed=SEED,
                   description="A poor farming village on a hillside in grey late-autumn light, woods closing in on every side: a muddy "
                               "square with a well and a bonfire pile, two crooked lanes of stone cottages, barns and ruins, a farmyard "
                               "with pens, the stone church in its walled graveyard at the top, a plank footbridge over the gully, and a "
                               "cliff path up to a timber watchtower on the bluff. Assault (square, farmyard, church), Team Deathmatch, "
                               "Solo and Battle Royale. 250 x 250 m.",
                   width=W, depth=D, margin=MARGIN, heightmap=grid.meta(TID + ".height.png"), water_level=-1000.0,
                   map_file=TID + ".nmap.json", camera=cam, thumbnail=TID + ".thumb.png", thumb_camera=thumb_cam,
                   extra={"userMap": True, "views": views})
    use = asset_usage(b)
    doc = os.path.join(ctx.repo, "docs", "asset-usage-default-maps.md")
    update_usage_doc(doc, use, "Hollow Village",
                     "- **Hollow Village:** *Hollow Village* (MP1, `tools/mapgen/maps/hollow_village.py`): placed counts; \"not used\" = not on this map.")
    cover_catalogue(doc, cat)
    gen = sum(n for a, n in use.items() if not (cat[a].raw.get("model") or {}).get("dir"))
    imp = sum(n for a, n in use.items() if (cat[a].raw.get("model") or {}).get("dir"))
    rep.update({"instances": len(b.inst), "errors": err, "warnings": warn, "generatedInstances": gen, "importedInstances": imp,
                "generatedIds": sorted(a for a in use if not (cat[a].raw.get("model") or {}).get("dir")),
                "importedIds": len([a for a in use if (cat[a].raw.get("model") or {}).get("dir")]),
                "use": dict(sorted(use.items())), "rejects": {"%s/%s" % k: v for k, v in sorted(b.rejects.items())}})
    write_summary(os.path.join(ctx.out, TID + ".summary.json"), rep)
    print("instances %d (generated %d, imported %d) | houses %d | lines %s | lane %d interior %d | trees %d+%d+%d decor | open %d | errors %s | warnings %d | walkable %.0f m2"
          % (len(b.inst), gen, imp, rep["houses"], rep["lines"], rep["laneProps"], rep["interiorProps"], rep["woodTrees"], rep["villageTrees"],
             rep["decorTrees"], nopen, err, len(warn), area))
