"""VIL1 Ashwood Hollow, a Search and Destroy map: a decaying hill village in a ring of dead pine forest, under a low
overcast sky at dusk. Rubble-stone and timber houses round a muddy square with a well; the church on its knoll (A)
and the farmyard with the stone barn (B). Raiders come up the woodcutters' road from the south, Wardens hold the mill
yard in the north. Three lanes: the Grave Path along the west forest edge (the long sightline), the main street
through the square, the Hollow Lane (a sunken cart track) in the east. An original layout; only the mood follows the
user's reference village.

    <python> -I tools/mapgen/build.py sd_village"""
import json
import math
import os

from mapgen import (GroundPaint, Grid, Frame, MapBuilder, ScenarioBuilder, write_map, write_template, write_summary,
                    rng, look_yaw, fbm, smoothstep, polyline_dist, catmull_rom, local_to_world, stamp_flatten_poly, in_poly)

TID, NAME, VERSION, SEED = "sd_village", "Ashwood Hollow", 1, 4417
W = D = 230.0           # build area: the playable square (+-74 m) and the forest ring round it
MARGIN = 60
B0 = 74.0               # playable half size (sd bounds)
BASE = 10.0

# ---------------------------------------------------------------- the plan (x east, z north; an asset's +z is its front)
KNOLL = (-28.0, 27.0)                      # the churchyard knoll (A), 2.6 m above the square
KNOLL_H = 2.6
SQUARE = (-16.0, -11.0, 16.0, 13.0)        # the village square (mid)
MAIN = catmull_rom([(1.0, -70.0), (2.0, -56.0), (3.5, -42.0), (-1.5, -27.0), (0.0, -12.0)])
GRAVE = catmull_rom([(-6.0, -64.0), (-30.0, -58.0), (-50.0, -44.0), (-56.0, -22.0), (-56.0, 0.0), (-54.0, 14.0), (-44.0, 21.0), (-33.0, 24.0)])
HOLLOW = catmull_rom([(8.0, -64.0), (30.0, -58.0), (47.0, -44.0), (55.0, -22.0), (54.0, -2.0), (50.0, 12.0), (44.0, 16.0), (34.0, 19.0)])
HOLLOW_DEPTH = 2.2
NORTH = catmull_rom([(-21.0, 29.0), (-10.0, 34.0), (4.0, 35.0), (14.0, 34.0), (21.0, 31.5), (23.0, 25.0)])   # the wardens' track behind the square
CHURCH_LANE = catmull_rom([(-14.0, 12.0), (-18.0, 19.0), (-24.0, 23.0)])
GAP = catmull_rom([(8.0, 13.0), (8.0, 24.0), (6.0, 34.0), (-4.0, 40.0), (-4.0, 52.0), (5.0, 59.0)])    # the rectory gap: wardens to mid
WARD_EAST = catmull_rom([(14.0, 62.0), (21.0, 54.0), (21.5, 40.0), (22.0, 27.0)])            # wardens down the barn side to B
FARM_GATE = catmull_rom([(15.0, 12.0), (21.0, 16.0), (26.0, 20.0)])                        # the square to the farmyard
EAST_ALLEY = catmull_rom([(16.0, 2.0), (27.0, 2.0), (34.0, 8.0), (44.0, 10.0), (50.0, 12.0)])   # through the gatehouse arch to the Hollow Lane
WELL_ALLEY = catmull_rom([(-2.0, -40.0), (-20.0, -41.0), (-36.0, -38.0), (-51.0, -36.0)])  # south cross: main street - Grave Path
BAKERS = catmull_rom([(-16.0, 2.6), (-28.0, 2.6), (-40.0, 4.0), (-56.0, 5.0)])           # mid cross: square - Grave Path
ORCHARD = catmull_rom([(6.0, -26.0), (22.0, -26.0), (38.0, -28.0), (50.0, -30.0)])        # south-east cross: main street - Hollow Lane
LANES = [MAIN, GRAVE, HOLLOW, NORTH, CHURCH_LANE, GAP, WARD_EAST, FARM_GATE, EAST_ALLEY, WELL_ALLEY, BAKERS, ORCHARD]

SPAWN_T = (1.0, -64.0)    # Raiders: the woodcutters' road clearing
SPAWN_D = (6.0, 58.0)    # Wardens: the mill yard
SITE_A = (-26.0, 24.0)    # the churchyard
SITE_B = (26.0, 20.0)     # the farmyard

# (asset, x, z, yaw): buildings; yaw 180 = front to the south
BUILDINGS = [
    # the square
    ("vil_bakery", -24.0, -6.0, 90.0), ("vil_merchant_house", -29.0, 12.5, 0.0), ("vil_townhouse_jettied", -3.0, 25.0, 180.0),
    ("house_stone_cottage", 15.0, 24.0, 90.0), ("vil_gatehouse_chapel", 21.0, 2.0, 90.0), ("vil_cafe_corner", 25.5, -14.5, 270.0),
    ("ruin_house_shelled", -10.0, -16.5, 0.0), ("vil_corner_shop", 10.0, -17.5, 0.0),
    # the main street
    ("vil_norman_farmhouse", -12.5, -30.0, 90.0), ("house_stone_cottage", -11.5, -49.0, 90.0),
    ("vil_colombage_auge", 12.0, -37.5, 270.0), ("house_farmhouse_2story", 13.0, -53.0, 270.0),
    # between the Grave Path and the square
    ("pa_stone_house_ruin", -43.0, -8.0, 90.0), ("vil_longhouse_thatched", -58.0, 40.0, 90.0),
    # the church and the knoll
    ("vil_church", -30.0, 40.0, 180.0),
    # the farm
    ("vil_stone_barn", 36.0, 34.0, 180.0), ("ruin_barn_burnt", 52.0, 30.0, 270.0), ("pa_shed_ruin_long", 42.0, -2.5, 90.0),
    ("small_storehouse", 66.0, 40.0, 90.0), ("vil_relais_inn", 52.0, 52.0, 180.0),
    # east of the main street, west of the Hollow Lane
    ("pa_log_house_ruin", 37.5, -40.0, 0.0), ("ruin_farmhouse_gutted", 41.0, -18.0, 180.0),
    # infill: the village closes round its lanes (enterable houses give the short, indoor sightlines)
    ("house_farmhouse_2story", -27.0, -49.0, 180.0), ("vil_granite_longere", -31.5, -22.0, 0.0), ("house_stone_cottage", -47.0, -26.0, 270.0),
    ("vil_rectory", 8.0, 47.0, 180.0), ("house_stone_cottage", -24.0, 56.0, 180.0), ("small_storehouse", 26.0, -52.0, 330.0),
    ("ruin_house_shelled", 24.5, -38.0, 90.0),
    # the wardens' side
    ("vil_mill_house", -10.0, 70.0, 180.0), ("pa_shack_ruin", 36.0, 64.0, 200.0), ("ruin_cottage_corner", -30.0, 64.0, 160.0),
]


def lane_k(x, z):
    """0 on the Hollow Lane's floor .. 1 on its banks' tops (and away from it)."""
    d, s, L = polyline_dist((x, z), HOLLOW)
    ends = smoothstep(0.0, 14.0, s) * smoothstep(L, L - 16.0, s)   # it climbs out at both ends
    k = smoothstep(2.8, 6.5, d)
    return 1.0 - ends * (1.0 - k)


def height(x, z):
    h = BASE + 0.9 * fbm(x / 45.0, z / 45.0, 3, 4) + 0.25 * fbm(x / 9.0, z / 9.0, 7, 2)
    kx, kz = KNOLL
    r = math.hypot((x - kx) / 1.15, z - kz)
    h += KNOLL_H * (1.0 - smoothstep(10.0, 20.0, r))                 # the church knoll
    h -= HOLLOW_DEPTH * (1.0 - lane_k(x, z))                          # the Hollow Lane
    e = max(abs(x), abs(z)) - B0
    h += 7.0 * smoothstep(0.0, 40.0, e) + 3.0 * smoothstep(10.0, 40.0, e) * fbm(x / 25.0, z / 25.0, 9, 3)   # the wooded slopes
    return h


def near_lane(x, z, pad=0.0):
    return min(polyline_dist((x, z), p)[0] for p in LANES) < 3.2 + pad


def build(ctx):
    cat = ctx.catalog
    R = rng(SEED)
    grid = Grid(W, D, margin=MARGIN)
    grid.apply(lambda x, z, h: height(x, z))
    for aid, x, z, yaw in BUILDINGS:   # level each building's pad
        a = cat[aid]
        hx, hz = a.half[0] + 1.2, a.half[1] + 1.2
        poly = [local_to_world((x, z), yaw, lx, lz) for lx, lz in ((-hx, -hz), (hx, -hz), (hx, hz), (-hx, hz))]
        stamp_flatten_poly(grid, poly, height=grid.sample(x, z), falloff=4.0)
    sq = SQUARE
    stamp_flatten_poly(grid, [(sq[0], sq[1]), (sq[2], sq[1]), (sq[2], sq[3]), (sq[0], sq[3])], height=grid.sample(0.0, 0.0), falloff=6.0)
    grid.quantise()
    b = MapBuilder(cat, grid, Frame(W, D), seed=SEED)
    paint = GroundPaint()
    G = b.ground

    # ---------------------------------------------------------------- ground: dead leaves and needles, mud lanes, a trodden square
    E = W / 2 + MARGIN
    paint.poly("leaf_litter", [(-E, -E), (E, -E), (E, E), (-E, E)], weight=1.0, edge=0.5)
    # bare trodden earth under thin dry grass: over half dirt, so no green nature patches (flowers, leafy bushes) grow in the village
    paint.poly("dirt", [(-B0, -B0), (B0, -B0), (B0, B0), (-B0, B0)], weight=0.62, edge=6.0, ragged=4.0, seed=2)
    paint.poly("dry_grass", [(-B0, -B0), (B0, -B0), (B0, B0), (-B0, B0)], weight=0.38, edge=6.0, ragged=4.0, seed=3)
    for pts, w, kind in ((MAIN, 6.5, "mud"), (GRAVE, 4.0, "dirt"), (HOLLOW, 5.0, "mud"), (NORTH, 4.0, "dirt"), (CHURCH_LANE, 3.2, "dirt"),
                         (GAP, 3.5, "dirt"), (WARD_EAST, 3.5, "dirt"), (FARM_GATE, 3.5, "mud"), (EAST_ALLEY, 3.5, "mud"), (WELL_ALLEY, 3.0, "dirt"), (BAKERS, 3.0, "dirt"), (ORCHARD, 3.0, "dirt")):
        paint.line(kind, pts, w, weight=0.9, edge=1.4)
    paint.poly("mud", [(sq[0], sq[1]), (sq[2], sq[1]), (sq[2], sq[3]), (sq[0], sq[3])], weight=0.85, edge=2.0, ragged=1.5, seed=7)
    paint.poly("pebbles", [(-7, -5), (6, -6), (7, 7), (-6, 8)], weight=0.6, edge=2.5, ragged=1.5, seed=8)
    paint.poly("mud", [(SITE_B[0] - 12, SITE_B[1] - 10), (SITE_B[0] + 11, SITE_B[1] - 9), (SITE_B[0] + 12, SITE_B[1] + 10), (SITE_B[0] - 11, SITE_B[1] + 11)],
               weight=0.8, edge=2.0, ragged=2.0, seed=9)
    paint.poly("dirt", [(SPAWN_T[0] - 10, SPAWN_T[1] - 7), (SPAWN_T[0] + 10, SPAWN_T[1] - 7), (SPAWN_T[0] + 10, SPAWN_T[1] + 6), (SPAWN_T[0] - 10, SPAWN_T[1] + 6)], weight=0.8, edge=2.5, ragged=2.0, seed=10)
    paint.poly("dirt", [(SPAWN_D[0] - 10, SPAWN_D[1] - 6), (SPAWN_D[0] + 10, SPAWN_D[1] - 6), (SPAWN_D[0] + 10, SPAWN_D[1] + 6), (SPAWN_D[0] - 10, SPAWN_D[1] + 6)], weight=0.8, edge=2.5, ragged=2.0, seed=11)

    # ---------------------------------------------------------------- keep-outs: lanes, sites, spawns, the eyes
    for pts in LANES:
        b.keepout_line(pts, 2.4)
    keep = [(SITE_A[0], SITE_A[1], 3.0), (SITE_B[0], SITE_B[1], 3.0), (SPAWN_T[0], SPAWN_T[1], 7.0), (SPAWN_D[0], SPAWN_D[1], 7.0), (0.0, 1.0, 2.0)]

    def clear(x, z, r):
        return all(math.hypot(x - kx, z - kz) > r + kr for kx, kz, kr in keep)

    # ---------------------------------------------------------------- buildings
    n_bld = 0
    boxes = []
    for aid, x, z, yaw in BUILDINGS:
        if b.place(aid, x, z, yaw, check=False, tags={"building"}) is not None:
            n_bld += 1
            a = cat[aid]
            boxes.append((aid, x, z, yaw, [local_to_world((x, z), yaw, lx, lz) for lx, lz in
                                           ((-a.half[0], -a.half[1]), (a.half[0], -a.half[1]), (a.half[0], a.half[1]), (-a.half[0], a.half[1]))]))
    # self-check: no two buildings overlap, no lane runs through a building (lanes keep 1.5 m clear of the walls)
    bad = []
    for i, (ai, xi, zi, _, pi) in enumerate(boxes):
        for aj, xj, zj, _, pj in boxes[i + 1:]:
            if any(in_poly(q, pj) for q in pi) or any(in_poly(q, pi) for q in pj):
                bad.append("overlap %s(%g,%g) / %s(%g,%g)" % (ai, xi, zi, aj, xj, zj))
        for li, pts in enumerate(LANES):
            if ai == "vil_gatehouse_chapel" and pts is EAST_ALLEY:   # the alley runs through its carriage arch
                continue
            for k in range(len(pts)):
                q = pts[k]
                c = (sum(v[0] for v in pi) / 4, sum(v[1] for v in pi) / 4)
                grow = [(c[0] + (v[0] - c[0]) * 1.0 + math.copysign(1.5, v[0] - c[0]), c[1] + (v[1] - c[1]) + math.copysign(1.5, v[1] - c[1])) for v in pi]
                if in_poly(q, grow):
                    bad.append("lane %d through %s(%g,%g) at (%.0f,%.0f)" % (li, ai, xi, zi, q[0], q[1]))
                    break
    # the houses' companions (front garden walls, picket fences, gate piers, sheds) keep 2 m clear of every lane: placed
    # with their host, they ran down Main Street and across the churchyard (IW1)
    def rect_pts(e):
        a = cat[e["asset"]]
        hx, hz = a.half
        nx, nz = max(1, int(2 * hx / 0.5)), max(1, int(2 * hz / 0.5))
        return [local_to_world((e["pos"][0], e["pos"][2]), e["yaw"], -hx + 2 * hx * i / nx, -hz + 2 * hz * k / nz)
                for i in range(nx + 1) for k in range(nz + 1)]

    def lane_gap(e):
        return min(polyline_dist(q, pts)[0] for q in rect_pts(e) for pts in LANES)

    comp_cut = [e for e in b.inst if "companion" in e["tags"] and not cat[e["asset"]].tree and lane_gap(e) < 2.0]
    for e in comp_cut:
        print("IW1 cut companion %s at (%.1f, %.1f), host %s" % (e["asset"], e["pos"][0], e["pos"][2], e.get("host")))
        b.remove(e)
    for e in b.inst:   # self-check: no companion within 1.5 m of a lane
        if "companion" in e["tags"] and not cat[e["asset"]].tree and lane_gap(e) < 1.5:
            bad.append("companion %s(%.0f,%.0f) on a lane" % (e["asset"], e["pos"][0], e["pos"][2]))
    # every ground-floor door keeps a clear way in: 3 m out from the entrance, 1.4 m wide (IW1: doors opened onto a
    # neighbour's wall, a porch post, a woodpile, a lane wall)
    doors = []   # (host id, corridor polygon)
    for e in b.inst:
        if "building" not in e["tags"] or "companion" in e["tags"]:
            continue
        for en in cat[e["asset"]].nav.get("entrances") or []:
            if en["p"][1] > 1.0:
                continue
            px, pz = en["p"][0], en["p"][2]
            ox, oz = -en["dir"][0], -en["dir"][2]
            n = math.hypot(ox, oz) or 1.0
            ox, oz = ox / n, oz / n
            for depth in (3.0, 1.8):
                q = [(px + ox * a_ - oz * w_, pz + oz * a_ + ox * w_) for a_, w_ in ((-0.2, -0.7), (depth, -0.7), (depth, 0.7), (-0.2, 0.7))]
                doors.append((e["id"], depth, [local_to_world((e["pos"][0], e["pos"][2]), e["yaw"], lx, lz) for lx, lz in q]))

    def in_door(e, own=None, depth=3.0):
        pts = rect_pts(e)
        return next((h for h, dp, poly in doors if dp == depth and h != own and h != e["id"] and any(in_poly(p, poly) for p in pts)), None)

    for e in b.inst:   # a building leaves a neighbour's door 1.8 m to step out into
        if "building" in e["tags"] and "companion" not in e["tags"]:
            h = in_door(e, depth=1.8)
            if h is not None:
                bad.append("building %s(%.0f,%.0f) in front of %s's door" % (e["asset"], e["pos"][0], e["pos"][2], b.by_id[h]["asset"]))
    for m in bad:
        print("LAYOUT:", m)

    def put(aid, x, z, yaw, tags=("dress",), check=True, r=None, **kw):
        a = cat[aid]
        rr = r if r is not None else max(a.half)
        if check and (not clear(x, z, rr) or b.in_keepout(x, z) or b.overlaps(a, x, z, yaw, {"flat", "none"})):
            return None
        return b.place(aid, x, z, yaw, check=False, tags=set(tags), **kw)

    n_dress = 0
    # the square: the well, carts, hay, woodpiles, braziers burning at dusk, lantern posts
    for row in (("drs_well", -2.5, 2.5, 20.0), ("drs_cart", 7.5, 6.5, 205.0), ("drs_cart", -10.0, -6.0, 110.0), ("drs_hay_bale_stack", 10.0, -5.5, 80.0),
                ("drs_woodpile", -13.0, 9.0, 90.0), ("fire_brazier", 3.0, -3.0, 0.0), ("fire_brazier", -7.5, 4.5, 0.0), ("ph_wine_barrel_01", 12.5, 9.5, 30.0),
                ("ph_wooden_crate_01", 13.0, 11.0, 10.0), ("pa_crate_slatted", -14.0, -8.5, 25.0), ("ph_wooden_bucket_02", -1.0, 4.4, 0.0),
                ("lamp_lantern_post", -15.0, 12.0, 135.0), ("lamp_lantern_post", 15.0, -10.0, 315.0), ("drs_wooden_x_obstacle", 5.0, -9.5, 15.0),
                ("drs_wooden_x_obstacle", -4.0, 11.0, 170.0)):
        n_dress += put(*row, check=False) is not None
    # the churchyard: gravestones in rows, tomb chests, the calvary cross, a picket fence, dead trees
    ax, az = SITE_A
    for k in range(-3, 4):
        for row in (-1, 1):
            x, z = ax + k * 3.4 + R.uniform(-0.4, 0.4), az + row * 5.5 + R.uniform(-0.6, 0.6)
            if abs(k) <= 0 and row == -1:
                continue
            aid = R.choice(("drs_gravestone_cross", "drs_gravestone_round", "drs_gravestone_cross"))
            n_dress += put(aid, x, z, 180.0 + R.uniform(-12, 12), r=0.6) is not None
    for row in (("drs_tomb_chest", ax - 9.0, az + 1.0, 92.0), ("drs_tomb_chest", ax + 8.5, az - 0.5, 85.0), ("drs_churchyard_cross", ax + 1.0, az + 9.0, 180.0),
                ("pa_tree_dead_b", ax - 14.0, az + 7.0, 40.0), ("nt3_tree_dead_oak", ax + 13.5, az - 6.0, 120.0), ("fire_brazier", ax + 3.0, az - 2.5, 0.0),
                ("ph_wooden_lantern_01", ax - 3.2, az + 2.4, 0.0), ("drs_wooden_x_obstacle", ax + 6.0, az - 9.0, 75.0), ("drs_woodpile", ax - 13.0, az - 4.0, 95.0)):
        n_dress += put(*row) is not None
    # the farmyard: hay, carts, woodpiles, barrels, a ladder against the barn, a tipped cart
    bx, bz = SITE_B
    for row in (("drs_hay_bale_stack", bx - 6.0, bz + 5.5, 10.0), ("drs_hay_bale_stack", bx + 7.0, bz - 4.0, 95.0), ("drs_cart", bx + 3.5, bz + 6.5, 160.0),
                ("drs_woodpile", bx - 8.5, bz - 5.0, 80.0), ("ph_wooden_barrels_01", bx + 9.5, bz + 6.0, 200.0), ("drs_cart", bx - 2.0, bz - 8.5, 290.0),
                ("ph_wooden_ladder", bx + 1.0, bz + 12.6, 180.0), ("fire_brazier", bx - 2.5, bz + 2.5, 0.0), ("pa_crate_slatted", bx + 6.0, bz + 1.0, 20.0),
                ("ph_wooden_bucket_01", bx - 4.0, bz + 1.5, 0.0), ("furn_hay_bales", bx + 1.5, bz - 3.5, 30.0), ("drs_wooden_x_obstacle", bx - 10.5, bz + 1.0, 100.0),
                ("pa_tree_dead_d", bx + 13.0, bz - 12.0, 200.0)):
        n_dress += put(*row) is not None
    # the spawns: the woodcutters' camp (logs, a campfire, a cart) and the mill yard (sacks, crates, a brazier)
    tx, tz = SPAWN_T
    for row in (("fire_campfire", tx + 9.0, tz - 3.0, 0.0), ("drs_woodpile", tx - 10.0, tz + 1.0, 90.0), ("ph_dead_tree_trunk", tx + 11.0, tz + 4.0, 30.0),
                ("drs_cart", tx - 9.0, tz - 5.0, 20.0), ("ph_dead_tree_trunk_02", tx - 12.0, tz + 6.0, 140.0)):
        n_dress += put(row[0], row[1], row[2], row[3], check=False) is not None
    dx_, dz_ = SPAWN_D
    for row in (("fire_brazier", dx_ - 6.0, dz_ + 3.0, 0.0), ("pa_crate_slatted", dx_ + 8.0, dz_ + 3.5, 15.0), ("mod_fp_sack", dx_ + 9.0, dz_ + 2.0, 40.0),
                ("drs_hay_bale_stack", dx_ + 10.0, dz_ - 3.0, 90.0), ("ph_wine_barrel_01", dx_ - 8.5, dz_ - 2.0, 0.0)):
        n_dress += put(row[0], row[1], row[2], row[3], check=False) is not None
    # lanes: cover along the edges (carts, hay, woodpiles, barrels, crates, x-barricades), lanterns at the corners
    LANE_COVER = [("drs_woodpile", 6), ("drs_hay_bale_stack", 4), ("drs_cart", 3), ("ph_wine_barrel_01", 5), ("pa_crate_slatted", 4), ("ph_wooden_crate_02", 3),
                  ("drs_wooden_x_obstacle", 5), ("furn_hay_bales", 3), ("ph_wooden_barrels_01", 1), ("pa_fruit_crate_mid", 2)]
    pool = [a for a, w in LANE_COVER for _ in range(w)]
    for pts, every in ((MAIN, 13.0), (GRAVE, 12.0), (HOLLOW, 14.0), (NORTH, 15.0), (WELL_ALLEY, 11.0), (ORCHARD, 11.0), (BAKERS, 12.0)):
        acc, last = 0.0, pts[0]
        side = 1
        for p in pts[1:]:
            acc += math.hypot(p[0] - last[0], p[1] - last[1])
            if acc >= every:
                acc = 0.0
                tx_, tz_ = p[0] - last[0], p[1] - last[1]
                n = math.hypot(tx_, tz_) or 1.0
                nx, nz = -tz_ / n * side, tx_ / n * side
                aid = R.choice(pool)
                off = 3.4 + max(cat[aid].half) * 0.6
                x, z = p[0] + nx * off, p[1] + nz * off
                if abs(x) < B0 - 2 and abs(z) < B0 - 2 and not (sq[0] < x < sq[2] and sq[1] < z < sq[3]):
                    n_dress += put(aid, x, z, look_yaw(tx_, tz_) + R.uniform(-25, 25)) is not None
                side = -side
            last = p
    for x, z in ((-2.0, -40.0), (-51.0, -36.0), (-44.0, 21.0), (-18.0, 19.0), (34.0, 8.0), (50.0, -30.0), (4.0, 35.0), (-4.0, 46.0), (21.5, 44.0)):
        n_dress += put("lamp_lantern_post", x + 3.6, z + 1.0, R.uniform(0, 360)) is not None
    # garden fences and dead orchards between the lanes
    n_fence = 0
    for x0, z0, x1, z1 in ((-50.0, -30.0, -24.0, -30.0), (-50.0, -4.0, -32.0, -4.0), (20.0, -30.0, 46.0, -34.0),
                           (24.0, 40.0, 24.0, 52.0), (-62.0, -50.0, -62.0, 20.0), (62.0, -40.0, 62.0, 4.0)):
        L = math.hypot(x1 - x0, z1 - z0)
        n = max(1, math.ceil(L / cat["mod_palisade_fence" if abs(x0) > 60 else "drs_picket_fence"].fw))   # bays end to end: no slits (IW1)
        for k in range(n):
            if R.random() < 0.18:   # broken bays
                continue
            t = (k + 0.5) / n
            x, z = x0 + (x1 - x0) * t, z0 + (z1 - z0) * t
            aid = "mod_palisade_fence" if abs(x0) > 60 else "drs_picket_fence"
            if not near_lane(x, z, 0.5) and clear(x, z, 1.5):
                n_fence += b.place(aid, x, z, look_yaw(x1 - x0, z1 - z0) + 90.0 + R.uniform(-4, 4), check=False, tags={"fence"}) is not None
    # garden walls along the lanes (1.3 m rubble: crouch cover, no shooting over them at a run), the Hollow Lane's banks hedged
    n_wall = 0
    wall_a = cat["drs_garden_wall"]
    for pts, off, aid, step_ in ((MAIN, 4.6, "drs_garden_wall", 4.0), (GRAVE, 3.8, "drs_garden_wall", 4.0), (NORTH, 3.8, "drs_garden_wall", 4.0),
                                 (WELL_ALLEY, 3.2, "drs_garden_wall", 4.0), (ORCHARD, 3.2, "drs_garden_wall", 4.0), (BAKERS, 3.2, "drs_garden_wall", 4.0),
                                 (HOLLOW, 6.2, "mod_palisade_fence", 3.0)):
        acc, last = 0.0, pts[0]
        for p in pts[1:]:
            seg = math.hypot(p[0] - last[0], p[1] - last[1])
            acc += seg
            if acc >= step_ and seg > 1e-6:
                acc = 0.0
                tx_, tz_ = (p[0] - last[0]) / seg, (p[1] - last[1]) / seg
                for side in (-1, 1):
                    x, z = p[0] - tz_ * off * side, p[1] + tx_ * off * side
                    if abs(x) > B0 - 1.5 or abs(z) > B0 - 1.5 or (sq[0] - 3 < x < sq[2] + 3 and sq[1] - 3 < z < sq[3] + 3):
                        continue
                    if min(polyline_dist((x, z), q)[0] for q in LANES) < off - 0.6 or not clear(x, z, 3.0) or R.random() < 0.12:
                        continue
                    hl = cat[aid].half[0]   # the piece's ends keep 2 m off a crossing lane too (IW1)
                    if min(polyline_dist((x + tx_ * hl * sg, z + tz_ * hl * sg), q)[0] for q in LANES for sg in (-1, 1)) < 2.0:
                        continue
                    if b.overlaps(cat[aid], x, z, look_yaw(tx_, tz_) + 90.0, {"flat", "none"}):
                        continue
                    n_wall += b.place(aid, x, z, look_yaw(tx_, tz_) + 90.0 + R.uniform(-3, 3), check=False, tags={"wall"}) is not None
            last = p
    DEAD = ["pa_tree_dead_a", "pa_tree_dead_b", "pa_tree_dead_c", "pa_tree_dead_d", "pa_tree_dead_e", "nt3_tree_dead_ash", "nt3_tree_dead_oak"]
    n_tree_in = 0
    for _ in range(260):
        x, z = R.uniform(-B0 + 4, B0 - 4), R.uniform(-B0 + 4, B0 - 4)
        if near_lane(x, z, 3.0) or (sq[0] - 4 < x < sq[2] + 4 and sq[1] - 4 < z < sq[3] + 4) or not clear(x, z, 6.0):
            continue
        aid = R.choice(DEAD + ["ph_dead_tree_trunk", "ph_pine_sapling_medium_a", "ph_pine_sapling_medium_b"])
        e = put(aid, x, z, R.uniform(0, 360), tags=("tree",))
        n_tree_in += e is not None
        if n_tree_in >= 46:
            break

    # ---------------------------------------------------------------- the forest ring: dense pines and dead snags round the village
    PINES = ["ph_pine_tree_01_a", "ph_pine_tree_01_b", "ph_pine_tree_01_c", "ph_fir_tree_01_a", "ph_fir_tree_01_b"]
    n_forest = 0
    lim = W / 2 - 2.0
    step = 5.2
    k = 0
    zz = -lim
    while zz < lim:
        xx = -lim
        while xx < lim:
            k += 1
            x, z = xx + R.uniform(-2.0, 2.0), zz + R.uniform(-2.0, 2.0)
            e = max(abs(x), abs(z)) - B0
            if e > -4.0 and (e > 2.0 or R.random() < 0.5) and R.random() < (0.95 if e < 20 else 0.6) and not near_lane(x, z, 1.0):
                aid = R.choice(PINES) if R.random() < 0.72 else R.choice(DEAD)
                if b.place(aid, x, z, R.uniform(0, 360), check=False, cls="veg", tags={"tree", "forest"}) is not None:
                    n_forest += 1
            xx += step
        zz += step

    # the houses' garden companions are summer-green: their trees die, the flowers and leafy shrubs go
    n_green = 0
    for e in list(b.inst):
        a = e["asset"]
        if a in ("tree_oak", "tree_apple"):
            e["asset"] = "nt3_tree_dead_oak"
        elif a == "tree_ash":
            e["asset"] = "nt3_tree_dead_ash"
        elif a.startswith("nat_flowers_") or a in ("drs_flower_bed", "shrub_hazel", "nat_hawthorn_bush"):
            b.remove(e)
            n_green += 1

    # no slots: a placed piece either touches its neighbour or leaves a real way through (1 m; the player is 0.6 m wide).
    # A 0.15-1.0 m gap between a wall end and the next, a fence beside a wall, a cart off a house wall reads as passable
    # and is not (IW1). The later piece goes; a building's own authored set (the church's graves) is left as authored.
    def solid(e):
        a = cat[e["asset"]]
        return not a.tree and "forest" not in e["tags"] and a.fw * a.fd > 0.04 and not a.raw.get("nature") and (
            a.raw.get("parts") or a.raw.get("model")) and not e["asset"].startswith("nat_")

    col_box = {}

    def col_half(aid):   # the walking-height collider bounds (asset space): a stated footprint can be smaller than the walls
        if aid not in col_box:
            ps = cat[aid].raw.get("parts") or []
            ps = json.loads(ps) if isinstance(ps, str) else ps
            bx = [(q["c"][0] - q["s"][0] / 2, q["c"][0] + q["s"][0] / 2, q["c"][2] - q["s"][2] / 2, q["c"][2] + q["s"][2] / 2) for q in ps
                  if q.get("k", 1) != 0 and "c" in q and "s" in q and q["c"][1] + q["s"][1] / 2 > 0.35 and q["c"][1] - q["s"][1] / 2 < 1.8]
            if bx:
                x0, x1, z0, z1 = min(v[0] for v in bx), max(v[1] for v in bx), min(v[2] for v in bx), max(v[3] for v in bx)
                col_box[aid] = ((x0 + x1) / 2, (z0 + z1) / 2, (x1 - x0) / 2, (z1 - z0) / 2)
            else:
                col_box[aid] = (0.0, 0.0) + cat[aid].half
        return col_box[aid]

    def frame(e):
        ox, oz, hx, hz = col_half(e["asset"])
        yw = math.radians(e["yaw"])
        c, s_ = math.cos(yw), math.sin(yw)
        return e["pos"][0] + ox * c + oz * s_, e["pos"][2] - ox * s_ + oz * c, c, s_, hx, hz

    def rect_gap(ea, eb):
        x, z, c, s_, hx, hz = frame(eb)
        best = 9.0
        for q in rect_edge(ea):
            dx, dz = q[0] - x, q[1] - z
            lx, lz = dx * c - dz * s_, dx * s_ + dz * c
            best = min(best, math.hypot(max(0.0, abs(lx) - hx), max(0.0, abs(lz) - hz)))
        return best

    def rect_edge(e):
        x, z, c, s_, hx, hz = frame(e)
        out = []
        for (ax_, az_), (bx_, bz_) in (((-hx, -hz), (hx, -hz)), ((hx, -hz), (hx, hz)), ((hx, hz), (-hx, hz)), ((-hx, hz), (-hx, -hz))):
            n = max(1, int(math.hypot(bx_ - ax_, bz_ - az_) / 0.1))
            for t in range(n + 1):
                lx, lz = ax_ + (bx_ - ax_) * t / n, az_ + (bz_ - az_) * t / n
                out.append((x + lx * c + lz * s_, z - lx * s_ + lz * c))
        return out

    solids = [e for e in b.inst if abs(e["pos"][0]) < B0 + 2 and abs(e["pos"][2]) < B0 + 2 and solid(e)]
    movable = lambda e: "building" not in e["tags"] or "companion" in e["tags"]
    slot_cut = []
    for e in sorted(solids, key=lambda e: -e["id"]):
        if not movable(e) or e in slot_cut:
            continue
        r_e = math.hypot(*col_half(e["asset"])[2:]) + math.hypot(*col_half(e["asset"])[:2])
        for o in solids:
            if o is e or o in slot_cut:
                continue
            if e.get("host") is not None and (e.get("host") == o.get("host") or e["host"] == o["id"]):
                continue   # the same building's authored set
            if math.hypot(o["pos"][0] - e["pos"][0], o["pos"][2] - e["pos"][2]) > r_e + math.hypot(*col_half(o["asset"])[2:]) + math.hypot(*col_half(o["asset"])[:2]) + 1.0:
                continue
            g = min(rect_gap(e, o), rect_gap(o, e))
            if 0.15 < g < 1.0:
                slot_cut.append(e)
                print("IW1 cut %s at (%.1f, %.1f): a %.2f m slot beside %s" % (e["asset"], e["pos"][0], e["pos"][2], g, o["asset"]))
                break
    for e in slot_cut:
        b.remove(e)

    door_cut = [e for e in b.inst if ("building" not in e["tags"] or "companion" in e["tags"]) and cat[e["asset"]].fw * cat[e["asset"]].fd > 0.01
                and in_door(e, e.get("host")) is not None]
    for e in door_cut:   # nothing stands in a doorway's way (dressing, lane walls, fences, companions, trees)
        print("IW1 cut %s at (%.1f, %.1f): in front of a door" % (e["asset"], e["pos"][0], e["pos"][2]))
        b.remove(e)

    # ---------------------------------------------------------------- scenario: two sites, five spawns a side, S&D timings
    sc = ScenarioBuilder(b, defending="Axis")
    sc.objective("A Churchyard", SITE_A[0], SITE_A[1], radius=1.75, capture_time=0, attack_from=(-44.0, 21.0), y_min=-1.0, y_max=2.0,
                 slots=[{"pos": [ax + 6.0, G(ax + 6.0, az + 5.0), az + 5.0], "yaw": 225.0, "kind": "position", "priority": 1},
                        {"pos": [ax - 7.0, G(ax - 7.0, az + 4.0), az + 4.0], "yaw": 135.0, "kind": "position", "priority": 2}],
                 routes=[("west", [(-50.0, -44.0), (-56.0, 0.0), (-44.0, 21.0)]), ("mid", [(0.0, -40.0), (-14.0, 12.0), (-24.0, 23.0)])])
    sc.objective("B Farmyard", SITE_B[0], SITE_B[1], radius=1.75, capture_time=0, attack_from=(44.0, 16.0), y_min=-1.0, y_max=2.0,
                 slots=[{"pos": [bx - 6.0, G(bx - 6.0, bz + 4.0), bz + 4.0], "yaw": 135.0, "kind": "position", "priority": 1},
                        {"pos": [bx + 6.0, G(bx + 6.0, bz + 8.0), bz + 8.0], "yaw": 225.0, "kind": "position", "priority": 2}],
                 routes=[("hollow", [(30.0, -58.0), (55.0, -22.0), (44.0, 16.0)]), ("mid", [(0.0, -40.0), (15.0, 12.0), (26.0, 20.0)])])
    T = [(SPAWN_T[0] + dx, SPAWN_T[1] + dz) for dx, dz in ((0, 0), (-3.5, 1.0), (3.5, 1.0), (-2.0, -3.0), (2.0, -3.0))]
    Dp = [(SPAWN_D[0] + dx, SPAWN_D[1] + dz) for dx, dz in ((0, 0), (-3.5, -1.0), (3.5, -1.0), (-2.0, 3.0), (2.0, 3.0))]
    for k, (x, z) in enumerate(T):
        sc.spawn("attacker", [0, 1], x, z, label="Raiders %d" % (k + 1), face=(x, z + 10.0), rack=None)
    for k, (x, z) in enumerate(Dp):
        sc.spawn("defender", [0, 1], x, z, label="Wardens %d" % (k + 1), face=(x, z - 10.0), rack=None)
    for s in sc.spawns:
        s["team"] = -1
    P = lambda x, z: [round(x, 2), round(G(x, z), 2), round(z, 2)]
    sd = {
        "callouts": [{"name": n, "x": c[0], "z": c[1], "r": r} for n, c, r in (
            ("A Churchyard", SITE_A, 12), ("B Farmyard", SITE_B, 12), ("Square", (0.0, 1.0), 14), ("Grave Path", (-56.0, -12.0), 12),
            ("Main Street", (1.0, -38.0), 10), ("Hollow Lane", (53.0, -24.0), 12), ("Church Lane", (-18.0, 18.0), 6), ("Farm Gate", (20.0, 15.0), 5), ("Back Track", (-2.0, 35.0), 7), ("Gatehouse", (21.0, 2.0), 6),
            ("Rectory Gap", (7.0, 30.0), 6), ("Well Alley", (-22.0, -41.0), 8), ("Orchard", (24.0, -27.0), 8), ("Bakers' Passage", (-34.0, 3.0), 7),
            ("Woodcutters' Road", SPAWN_T, 10), ("Mill Yard", SPAWN_D, 10))],
        "bounds": [[-B0, -B0], [B0, -B0], [B0, B0], [-B0, B0]],
        "timingPoints": {"contest": [P(0.0, -6.0), P(-56.0, -6.0), P(54.0, -6.0)]},
        "timingRoutes": [
            {"name": "wardens rotation", "kind": "rotation", "points": [P(*SITE_A), P(-21.0, 29.0), P(4.0, 35.0), P(22.0, 26.0), P(*SITE_B)]},
            {"name": "through the square", "kind": "mid", "points": [P(*SITE_A), P(-18.0, 19.0), P(-14.0, 12.0), P(15.0, 12.0), P(*SITE_B)]},
            {"name": "raiders fallback", "kind": "fallback", "points": [P(*SITE_A), P(-40.0, 4.0), P(-16.0, 2.6), P(0.0, -8.0), P(16.0, 2.0), P(27.0, 2.0), P(34.0, 8.0), P(34.0, 19.0), P(*SITE_B)]},
            {"name": "mid to A via church lane", "kind": "attackMid", "points": [P(*T[0]), P(0.0, -12.0), P(-14.0, 12.0), P(-24.0, 23.0), P(*SITE_A)]},
            {"name": "mid to B via the farm gate", "kind": "attackMid", "points": [P(*T[0]), P(0.0, -12.0), P(15.0, 12.0), P(*SITE_B)]},
            {"name": "raiders to mid", "kind": "note", "points": [P(*T[0]), P(0.0, 1.0)]},
            {"name": "wardens to mid", "kind": "note", "points": [P(*Dp[0]), P(0.0, 1.0)]},
        ],
    }
    rules = {"mode": "snd", "sd": sd}

    # ---------------------------------------------------------------- output
    tdir = os.path.join(ctx.repo, "content", "templates")
    grid.write_png(os.path.join(tdir, TID + ".height.png"))
    eye = 1.65
    VIEWS = [("square_eye", (0.0, -10.0), (0.0, 10.0)), ("a_site_eye", (-15.0, 13.0), SITE_A), ("b_site_eye", (16.0, 13.0), SITE_B),
             ("grave_path_eye", (-55.0, -36.0), (-55.0, 10.0)), ("hollow_lane_eye", (48.0, -44.0), (55.0, -14.0)), ("main_street_eye", (2.0, -58.0), (0.0, -20.0))]
    views = [{"name": "overview_high", "pos": [0.0, 62.0, -92.0], "yaw": 0.0, "pitch": -32.0}]
    for name, (x, z), (tx_, tz_) in VIEWS:
        views.append({"name": name, "pos": [x, round(G(x, z) + eye, 2), z], "yaw": look_yaw(tx_ - x, tz_ - z), "pitch": 0.0, "gameplay": True})
    cam = ([0.0, 62.0, -92.0], 0.0, -32.0)
    thumb_cam = ([6.0, G(6.0, -8.0) + 3.0, -8.0], look_yaw(-10.0, 22.0), -4.0)   # from the square's south-east corner, the well and the church lane
    a1 = sc.spawns[0]["pos"]
    look = {"haze": 2.6, "weather": 0.85, "clouds": 0.9, "envFog": 1, "saturation": 0.74, "tint": [1.04, 0.97, 0.88], "bloom": 1.3, "grime": 1.4, "season": "autumn",
            "paintSets": {"leaf_litter": "ph_needle_floor"}}
    write_map(os.path.join(tdir, TID + ".nmap.json"), name=NAME, template_id=TID, template_version=VERSION, seed=SEED, width=W, depth=D,
              builder=b, scenario_json=sc.to_json(rules_extra=rules), explore_start=((a1[0], a1[1], a1[2]), 0.0), camera=cam, bookmarks={},
              extra={"groundPaint": paint.to_json(), "lighting": {"hour": 17.9, "enclosed": False, "fill": 1.1}, "look": look})
    write_template(os.path.join(tdir, TID + ".json"), tid=TID, name=NAME, order=21, version=VERSION, seed=SEED,
                   description="Search and Destroy: a decaying hill village in a ring of dead pine forest, under a low sky at dusk. "
                               "A: the churchyard on its knoll; B: the farmyard by the stone barn; Mid: the village square. 5 v 5.",
                   width=W, depth=D, margin=MARGIN, heightmap=grid.meta(TID + ".height.png"), water_level=-1000.0,
                   map_file=TID + ".nmap.json", camera=cam, thumbnail=TID + ".thumb.png", thumb_camera=thumb_cam,
                   extra={"userMap": True, "views": views})
    rep = {"instances": len(b.inst), "buildings": n_bld, "companions_cut": len(comp_cut), "door_cut": len(door_cut), "slot_cut": len(slot_cut), "dress": n_dress, "fence": n_fence, "walls": n_wall, "layout_issues": len(bad), "trees_in": n_tree_in, "forest": n_forest, "rejects": dict(b.rejects)}
    write_summary(os.path.join(ctx.out, TID + ".summary.json"), rep)
    print(rep)
