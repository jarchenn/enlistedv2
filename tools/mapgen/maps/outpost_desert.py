"""NT3 variant A: Outpost Desert (126 x 140 m Battle Royale). A HESCO forward base on a low rise in the north, an old
highway down the west side, a dry wadi across the middle, a ruined roadside hamlet with a petrol station in the
south-west, and a flat-topped mesa along the east with a radar and an observation bunker. See outpost_common.py.

    <python> -I tools/mapgen/build.py outpost_desert"""
import math

from mapgen import polyline_dist, catmull_rom, rect, circle_poly, fbm, smoothstep, stamp_valley, asset_yaw_along, point_at, polyline_length, look_yaw
from maps.newtown import stamp_road
from maps.outpost_common import build_variant, range_layout, HX, HZ, WRECKS

FOB = (-34.0, -64.0, 20.0, -22.0)                   # x0, z0, x1, z1 of the HESCO wall
ROAD = catmull_rom([(-44.0, -78.0), (-42.0, -30.0), (-38.0, 8.0), (-44.0, 44.0), (-41.0, 78.0)])
WADI = catmull_rom([(-75.0, -2.0), (-40.0, 6.0), (-8.0, 12.0), (22.0, 6.0), (48.0, 16.0), (75.0, 10.0)])
RAMP = catmull_rom([(20.0, -42.0), (30.0, -40.0), (40.0, -34.0), (48.0, -24.0)])
TRACKS = [(catmull_rom([(-34.0, -40.0), (-40.0, -40.0)]), 4.0), (RAMP, 4.0),
          (catmull_rom([(-6.0, -22.0), (-4.0, 0.0), (-10.0, 24.0), (-22.0, 36.0), (-36.0, 38.0)]), 3.5),
          (catmull_rom([(40.0, 30.0), (30.0, 34.0), (10.0, 36.0), (-8.0, 40.0)]), 3.0)]


def mesa(x, z):
    edge = 33.0 + 4.0 * fbm(z / 25.0, 3.1, 21, 3)
    return smoothstep(edge - 4.0, edge + 3.0, x) * (1.0 - smoothstep(44.0, 58.0, z))


def dune(x, z):
    """Crescent dune ridges (~25 m apart, crests across the wind), fading near the base, the road and the hamlet."""
    u = x * 0.92 + z * 0.38 + 9.0 * fbm(x / 60.0, z / 60.0, 13, 3)
    ridge = 1.0 - abs(2.0 * ((u / 24.0) % 1.0) - 1.0)
    ridge = ridge ** 2.2 * (0.6 + 0.8 * fbm(x / 40.0, z / 40.0, 14, 2))
    keep = smoothstep(18.0, 34.0, math.hypot(x + 8, z + 43)) * smoothstep(8.0, 16.0, abs(x + 41)) * smoothstep(14.0, 26.0, math.hypot(x + 14, z - 44))
    return ridge * keep * (1.0 - mesa(x, z))


def height(x, z):
    h = 10.0 + 0.6 * fbm(x / 32.0, z / 32.0, 3, 3) + 1.4 * fbm(x / 85.0, z / 85.0, 9, 3) + 2.6 * dune(x, z)
    h += 1.6 * math.exp(-((x + 8) ** 2 + (z + 43) ** 2) / (2 * 26.0 ** 2))       # the base rise
    h += 7.5 * mesa(x, z)
    return h


def berm(grid):   # NT4: the range backstop, an earth berm across the north end of the range strip
    def fn(x, z, h):
        k = math.exp(-((z + 67.0) / 2.6) ** 2) * smoothstep(-64.0, -60.0, x) * (1.0 - smoothstep(-48.0, -45.0, x))
        return h + 2.8 * k
    grid.apply(fn)


def stamps(grid):
    L = polyline_length(WADI)
    stamp_valley(grid, WADI, floor=lambda u: grid.sample(*point_at(WADI, L * u)[0]) - 2.4, top_half=lambda u: 7.5, floor_half=lambda u: 2.4, power=0.8)
    stamp_road(grid, RAMP, 2.4, fall=5.0)


SITES = [
    ("mod_nt_hq", -18.0, -51.0, 0.0), ("mod_nt_armory", 6.0, -54.0, 0.0), ("mod_mil_hut", 8.0, -33.0, 90.0),
    ("mod_guard_tower", -31.0, -61.0, 45.0), ("mod_guard_tower", 17.0, -61.0, 315.0), ("mod_guard_tower", -31.0, -25.0, 135.0),
    ("mod_nt_radar_tower", 50.0, -30.0, 0.0), ("bunker_observation_post", 46.0, 22.0, 270.0),
    ("mod_bld_gas_station", -21.0, 52.0, 90.0), ("mod_nt_shop_small", -24.0, 27.0, 90.0), ("mod_nt_house_small", 0.0, 50.0, 0.0),
    ("mod_bld_bungalow", 6.0, 30.0, 180.0), ("mod_nt_site_office", 20.0, 56.0, 0.0),
    ("mod_helipad_raised", 12.0, -9.0, 180.0),   # NT4: beside the base, stair toward the wadi, clear air above
]
INFO = {("mod_nt_hq", -18.0, -51.0): {"d": 1, "mil": True}, ("mod_nt_armory", 6.0, -54.0): {"d": 1, "mil": True},
        ("mod_mil_hut", 8.0, -33.0): {"d": 1, "mil": True}, ("mod_nt_radar_tower", 50.0, -30.0): {"d": 3, "mil": True},
        ("bunker_observation_post", 46.0, 22.0): {"d": 3, "mil": True},
        ("mod_guard_tower", -31.0, -61.0): {"d": 1, "mil": True}, ("mod_guard_tower", 17.0, -61.0): {"d": 1, "mil": True},
        ("mod_guard_tower", -31.0, -25.0): {"d": 1, "mil": True},
        ("mod_bld_gas_station", -21.0, 52.0): {"d": 4}, ("mod_nt_shop_small", -24.0, 27.0): {"d": 4}, ("mod_nt_house_small", 0.0, 50.0): {"d": 4},
        ("mod_bld_bungalow", 6.0, 30.0): {"d": 2}, ("mod_nt_site_office", 20.0, 56.0): {"d": 2}}


def dress(b, R, prop, drop, line, paint, rep):
    x0, z0, x1, z1 = FOB
    # HESCO perimeter (gates: west to the highway, east to the mesa ramp, south to the wadi track)
    sides = [([(x0, z0), (x1, z0)], []), ([(x1, z0), (x1, z1)], [(18.0, 26.0)]), ([(x1, z1), (x0, z1)], [(20.0, 31.0)]), ([(x0, z1), (x0, z0)], [(14.0, 22.0)])]
    nh = 0
    for pts, gaps in sides:
        nh += len(b.chain(["mod_hesco_3x1", "mod_hesco_3x1", "mod_hesco_3x2", "mod_hesco_3x1"], pts, along="x", gaps=gaps, check=False,
                          pattern_rng=True, rng=R, tags={"hesco"}))
    rep["hesco"] = nh
    for (x, z) in ((x0 - 3.0, -44.0), (x0 - 3.0, -36.0), (-6.0, z1 + 3.0), (6.0, z1 + 3.0)):
        prop("mod_jersey_barrier_damaged", x, z, 90.0 if x < x0 else 0.0, tags={"gate", "cover"})
    for k in range(10):   # razor wire outside the wall, half of it trampled flat
        t = R.uniform(0, 1)
        side = R.randrange(3)
        x, z = ((lerp(x0, x1, t), z0 - 2.5), (x1 + 2.5, lerp(z0, z1, t)), (lerp(x0, x1, t), z1 + 2.5))[side]
        prop("mod_razor_coil", x, z, 0.0 if side != 1 else 90.0, tags={"wire"})
    for aid, x, z, yaw in (("mod_camo_net", -20.0, -33.0, 0.0), ("mod_camo_net", -8.0, -33.0, 10.0), ("mod_container_20_damaged", -26.0, -30.0, 90.0),
                           ("mod_container_40_open", -10.0, -42.0, 0.0), ("mod_container_20_open", 15.0, -46.0, 0.0), ("mod_generator_set", 0.0, -60.0, 0.0),
                           ("mod_antenna_mast", -27.0, -41.0, 0.0), ("mod_mil_generator", -4.0, -50.0, 90.0), ("car_truck_canopy", -14.0, -38.0, 80.0),
                           ("car_humvee", 0.0, -28.0, 200.0), ("mod_sandbag_position", 14.0, -26.0, 180.0), ("mod_sandbag_position", -28.0, -46.0, 270.0),
                           ("mod_barrel_steel_rust", -24.0, -36.0, 0.0), ("mod_barrel_group", 12.0, -60.0, 0.0), ("mod_pallet_stack", -2.0, -40.0, 20.0),
                           ("mod_crate_wood", -6.0, -36.0, 10.0), ("furn_mil_jerrycans", -12.0, -46.0, 0.0), ("mod_hesco_2x1_half", -2.0, -45.0, 90.0),
                           ("mod_hesco_1x2", 10.0, -44.0, 0.0), ("mod_mil_utility", -30.0, -50.0, 0.0)):
        prop(aid, x, z, yaw, tags={"fob", "cover"})
    # the mesa: sandbags round the radar, a broken antenna, boulders on the rim
    for a in range(0, 360, 60):
        prop("def_sandbag_straight", 50.0 + 6.5 * math.cos(math.radians(a)), -30.0 + 6.5 * math.sin(math.radians(a)), -a + 90.0, tags={"mesa", "cover"})
    prop("mod_antenna_mast", 56.0, -14.0, 0.0, tags={"mesa"})
    # the highway: a dead convoy, a checkpoint, cars abandoned at the hamlet
    L = polyline_length(ROAD)
    for s, aid, lane in ((18.0, "car_truck_cargo", 1.8), (27.0, "car_humvee", 1.8), (36.0, R.choice(WRECKS), -1.8), (96.0, R.choice(WRECKS), 1.8),
                         (104.0, R.choice(WRECKS), -1.8), (112.0, "mod_car_shell_burned", 1.8), (128.0, R.choice(WRECKS), -1.8)):
        (px, pz), t = point_at(ROAD, s)
        drop(aid, px - t[1] * lane, pz + t[0] * lane, math.degrees(math.atan2(t[0], t[1])) + R.uniform(-15, 15), dy=0.06, cls="solid")
    (px, pz), t = point_at(ROAD, 62.0)
    for j in (-1, 1):
        drop("mod_jersey_barrier", px - t[1] * j * 2.0, pz + t[0] * j * 2.0, asset_yaw_along(-t[1], t[0]) + R.uniform(-10, 10), cls="solid")
    drop("mod_sandbag_position", px + 6.0, pz, 270.0, cls="solid")
    for aid, x, z, yaw in (("mod_pcar_sedan_wreck", -18.0, 40.0, 30.0), ("mod_pcar_pickup_wreck", -30.0, 60.0, 100.0), ("mod_jerry_cans", -12.0, 46.0, 0.0),
                           ("mod_barrel_steel_rust", -10.0, 58.0, 0.0), ("mod_tyre_pile", -32.0, 40.0, 0.0), ("wat_puddles", -20.0, 64.0, 0.0),
                           ("mod_trash_bin", -30.0, 20.0, 0.0), ("mod_pcar_minivan_wreck", 12.0, 44.0, 200.0), ("mod_skip", -6.0, 22.0, 90.0),
                           ("mod_pallet", 16.0, 50.0, 0.0), ("mod_construction_fence", 24.0, 48.0, 0.0)):
        prop(aid, x, z, yaw, tags={"hamlet", "cover"})
    # rubble and sand drifts against everything on the highway shoulder
    for s in range(0, int(L), 3):
        (px, pz), t = point_at(ROAD, s + R.uniform(0, 2))
        o = R.choice((-1, 1)) * R.uniform(3.4, 5.5)
        r = R.random()
        aid = "nat_drygrass_tufts" if r < 0.45 else "mod_weeds_crack" if r < 0.7 else "nat_scree_patch" if r < 0.85 else "mod_rubble_small"
        drop(aid, px - t[1] * o, pz + t[0] * o, R.uniform(0, 360), cls="veg" if r < 0.7 else "solid")
        if R.random() < 0.55:   # the desert taking the asphalt back: sand-blown debris, cracks and tufts on the carriageway
            o = R.uniform(-3.0, 3.0)
            drop(R.choice(["rub_debris_patch", "mod_weeds_crack", "nat_drygrass_tufts", "nat_stones_patch", "mod_weeds_crack"]), px - t[1] * o, pz + t[0] * o,
                 R.uniform(0, 360), dy=0.03, cls="veg")
    # the mesa's cliff: outcrops and boulder falls along its edge (the ramp left open)
    for z in range(-66, 46, 4):
        xs = [x * 0.5 for x in range(40, 130)]
        x = next((x for x in xs if mesa(x, z) > 0.82), None)
        if x is None or polyline_dist((x, z), RAMP)[0] < 6.0 or b.grid.slope_deg(x, z) > 22.0:   # NT4: nothing perched on the steep rim
            continue
        drop(R.choice(["nat_outcrop_limestone", "nat_outcrop_limestone", "nat_boulder_limestone_l", "nat_boulders_granite_group"]), x + R.uniform(-1, 1), z + R.uniform(-1.5, 1.5),
             R.uniform(0, 360), dy=-0.7, cls="solid")
        if R.random() < 0.5:
            drop(R.choice(["nat_scree_patch", "nat_boulder_limestone_m", "nat_stones_patch"]), x - R.uniform(3, 6), z + R.uniform(-2, 2), R.uniform(0, 360), cls="solid")
    # NT4: Ural-4320 trucks (drivable): one in the base, one at the head of the convoy
    prop("car_ural_4320", -14.0, -28.5, 95.0, tags={"fob", "cover", "vehicle"})
    (px, pz), t = point_at(ROAD, 48.0)
    drop("car_ural_4320", px - t[1] * 1.9, pz + t[0] * 1.9, math.degrees(math.atan2(t[0], t[1])) + 6.0, cls="solid")
    # NT4: the shooting range in the strip west of the highway, firing north into the berm
    range_layout(b, R, prop, (-54.5, -34.0), 0.0, (16.0, 25.0, 30.0), 1, rep)
    for aid, x, z, yaw in (("mod_sandbag_position", -46.0, 30.0, 90.0), ("mod_ammo_crate_old", -47.5, 27.0, 20.0)):   # the checkpoint's crate
        e = prop(aid, x, z, yaw, tags={"checkpoint", "cover"})
        if e is not None and aid == "mod_ammo_crate_old":
            e["extra"] = e.get("extra") or {}; e["extra"]["loot"] = {"spot": True, "size": "small", "district": 4, "military": True}
    # NT4: tyre ruts across the sand, burn scars, dead vehicles in the open, sandbag and HESCO lines, more rock
    for k, pts in enumerate([[(-36.0, -20.0), (-20.0, -8.0), (4.0, -4.0), (30.0, 2.0)], [(-30.0, 60.0), (-6.0, 46.0), (20.0, 20.0), (34.0, -2.0)],
                             [(-58.0, -30.0), (-46.0, -10.0), (-40.0, 10.0)], [(10.0, 66.0), (24.0, 50.0), (46.0, 44.0)]]):
        for side in (-0.85, 0.85):
            off = [(x + side * 0.6, z + side * 0.8) for x, z in pts]
            paint.line("dirt", catmull_rom(off), 0.45, weight=0.55, edge=0.25)
    for x, z, r in ((14.0, 4.0, 4.5), (-30.0, -10.0, 3.5), (24.0, 38.0, 4.0)):
        paint.poly("scorched", circle_poly(x, z, r, 14), weight=0.75, edge=1.5, ragged=1.5, seed=int(x * 7 + z))
    for aid, x, z, yaw in (("mod_car_shell_burned", 14.0, 4.0, 40.0), ("car_truck_cargo", -30.0, -10.0, 120.0), ("mod_pcar_pickup_wreck", 24.0, 38.0, 300.0),
                           ("mod_pcar_offroad_wreck", -52.0, 30.0, 20.0), ("mod_container_20_damaged", 32.0, 60.0, 70.0), ("mod_camo_net", 36.0, 58.0, 30.0)):
        drop(aid, x, z, yaw, dy=-0.15, cls="solid")
    line("sandbag_wall", catmull_rom([(36.0, -8.0), (38.0, 4.0), (37.0, 16.0)]), tags=("sandbags",))
    line("sandbag_wall", catmull_rom([(-24.0, 14.0), (-12.0, 16.0), (0.0, 17.0)]), tags=("sandbags",))
    for x0, z0, x1, z1 in ((-60.0, 6.0, -50.0, 2.0), (2.0, 24.0, 12.0, 26.0)):
        b.chain(["mod_hesco_3x1", "mod_hesco_3x2"], [(x0, z0), (x1, z1)], along="x", check=False, pattern_rng=True, rng=R, tags={"hesco"})
    for k in range(40):
        x, z = R.uniform(-HX, HX), R.uniform(-HZ, HZ)
        drop(R.choice(["nat_outcrop_limestone", "nat_boulder_limestone_l", "nat_boulders_granite_group", "nat_stones_patch", "nat_scree_patch", "nat_brush_scrub", "nat_gorse_shrub"]),
             x, z, R.uniform(0, 360), dy=-0.3, cls="solid") if b.grid.slope_deg(x, z) < 20.0 else None
    # tufted dry grass through the sand (nature patches only grow off the sand paint)
    for k in range(46):
        x, z = R.uniform(-HX, HX), R.uniform(-HZ, HZ)
        if mesa(x, z) < 0.3:
            paint.poly("dry_grass", circle_poly(x, z, R.uniform(3.0, 8.0), 12), weight=R.uniform(0.6, 0.85), edge=3.0, ragged=3.0, seed=100 + k)
    # the highway breaking up (NT2b): potholes, sand-blown debris, papers and glass round the hamlet
    for k in range(22):
        (px, pz), t = point_at(ROAD, R.uniform(4, L - 4))
        o = R.uniform(-2.2, 2.2)
        drop(R.choice(["mod_asphalt_crater", "rub_debris_patch", "rub_debris_patch", "furn_papers", "mod_glass_pile", "nat_scree_patch"]),
             px - t[1] * o, pz + t[0] * o, R.uniform(0, 360), dy=0.02, cls="flat")
    # the wadi: boulders and dead trees in the bed
    Lw = polyline_length(WADI)
    for k in range(34):
        (px, pz), t = point_at(WADI, R.uniform(0, Lw))
        o = R.uniform(-4.5, 4.5)
        drop(R.choice(["nat_boulder_limestone_m", "nat_boulder_limestone_l", "nat_scree_patch", "nat_stones_patch", "nat_drygrass_tall", "nt3_tree_dead_ash", "nat_brush_scrub"]),
             px - t[1] * o, pz + t[0] * o, R.uniform(0, 360), cls="solid")


def lerp(a, b, t):
    return a + (b - a) * t


def mesa_slope(x, z):
    m = mesa(x, z)
    return 0.15 < m < 0.92


V = {
    "tid": "outpost_desert", "name": "Outpost Desert", "order": 10, "seed": 3101, "water": -20.0,
    "description": "A small Battle Royale map, 126 x 140 m (1/8 of Newtown): an abandoned HESCO forward base on a rise, a dead "
                   "convoy on the old highway, a dry wadi, a ruined roadside hamlet with a petrol station, and a mesa with a "
                   "radar and an observation bunker. Military loot (level-3 armour) in the base, the radar and the bunker. "
                   "16 players by default, short storm circles. Battle Royale, Team Deathmatch and Solo.",
    "height": height, "stamps": [stamps, berm], "roads": [(ROAD, 3.6)], "tracks": TRACKS, "track_paint": "gravel",
    "sites": SITES, "site_info": INFO,
    "flats": [(rect(37.0, -48.0, 61.0, 34.0), None), (rect(-62.0, -64.0, -47.5, -28.0), None)],   # the mesa top; the range strip
    "paint": [("poly", "sand", rect(-HX - 70, -HZ - 70, HX + 70, HZ + 70), {"weight": 1.0, "edge": 0.5}),
              ("poly", "dirt", rect(FOB[0] + 1, FOB[1] + 1, FOB[2] - 1, FOB[3] - 1), {"weight": 0.7, "edge": 2.0, "ragged": 2.0, "seed": 31}),
              ("poly", "rock", rect(31.0, -70.0, 70.0, 52.0), {"weight": 0.95, "edge": 3.0, "ragged": 5.0, "seed": 32}),
              ("poly", "sand", rect(40.0, -46.0, 60.0, 32.0), {"weight": 0.45, "edge": 5.0, "ragged": 4.0, "seed": 36}),
              ("poly", "gravel", circle_poly(-21.0, 52.0, 16.0, 16), {"weight": 0.6, "edge": 3.0, "ragged": 3.0, "seed": 33}),
              ("poly", "pebbles", circle_poly(0.0, 10.0, 9.0, 12), {"weight": 0.5, "edge": 3.0, "ragged": 3.0, "seed": 34}),
              ("line", "pebbles", WADI, 5.0, {"weight": 0.85, "edge": 2.0}),
              ("poly", "dry_grass", circle_poly(-50.0, 20.0, 14.0, 14), {"weight": 0.75, "edge": 4.0, "ragged": 4.0, "seed": 35})]
             + [("poly", "dry_grass", circle_poly(x, z, r, 12), {"weight": 0.8, "edge": 3.0, "ragged": 3.0, "seed": 40 + k})   # tufted patches through the sand
                for k, (x, z, r) in enumerate([(-52, -60, 7), (-56, -10, 6), (-30, 0, 8), (-12, 20, 5), (14, 22, 7), (24, -8, 6), (-48, 50, 9), (-4, 64, 6),
                                               (26, 40, 7), (-26, -12, 5), (6, 0, 6), (-60, 36, 6), (20, -64, 5), (-14, 62, 5), (34, 62, 6)])],
    "dress": dress,
    "helis": [("mod_helipad_raised", 2.0, 2.825, 30.0)],   # deck centre at local z +2, 2.8 m deck + paint
    "yard_paint": ["gravel", "dirt", "gravel"],
    "views_clear": 4.0,
    "wall_dress": ["nat_drygrass_tufts", "nat_drygrass_sparse", "mod_rubble_small", "nat_scree_patch", "mod_weeds_crack", "mod_glass_pile", "nat_brush_scrub"],
    "scatter": [([("nat_drygrass_sparse", 4), ("nat_drygrass_tufts", 4), ("nat_brush_scrub", 2), ("nat_gorse_shrub", 1)], lambda x, z: True, 1 / 32.0, 2.4,
                 {"ok_with": {"flat", "none", "wire", "veg"}}),
                ([("nat_outcrop_limestone", 3), ("nat_boulder_limestone_l", 2), ("nat_boulder_limestone_m", 2), ("nat_scree_patch", 3), ("nat_boulders_granite_group", 1)],
                 mesa_slope, 1 / 26.0, 3.0, {"ok_with": {"flat", "none", "wire", "veg"}, "slope_max": 24.0}),
                ([("nt3_tree_dead_oak", 1), ("nt3_tree_dry_ash", 2), ("nt3_tree_dead_ash", 1)], lambda x, z: mesa(x, z) < 0.1, 1 / 650.0, 9.0),
                ([("nat_stones_patch", 1), ("nat_boulder_limestone_m", 1)], lambda x, z: True, 1 / 400.0, 6.0)],
    "decor": ["nat_boulder_limestone_l", "nat_outcrop_limestone", "nt3_tree_dead_oak", "nat_brush_scrub", "nt3_tree_dry_ash", "nat_boulders_granite_group"],
    "decor_n": 140,
    "tdm": {"A": (-54.0, 62.0), "B": (26.0, 64.0)},
    "objective": (-6.0, -44.0), "objective_name": "Forward base", "attack_from": (-6.0, 20.0), "defend_at": (-6.0, -58.0),
    "districts": [("Forward base", (-8.0, -43.0)), ("Wadi", (10.0, 10.0)), ("Mesa radar", (49.0, -12.0)), ("Roadside hamlet", (-14.0, 44.0))],
    "views": [("fob_eye", (-48.0, -38.0), (-10.0, -46.0)), ("hamlet_eye", (-38.0, 22.0), (-18.0, 50.0)), ("wadi_eye", (-20.0, 8.0), (30.0, 8.0)),
              ("mesa_eye", (26.0, -40.0), (50.0, -28.0)), ("range_eye", (-52.0, -27.0), (-55.0, -60.0)), ("helipad_eye", (17.0, 12.0), (12.0, -6.0))],
    "start": ((-38.0, 22.0), (20.0, 28.0)),
    "thumb_cam": ([60.0, 70.0, 110.0], look_yaw(-60.0, -110.0), -28.0),
    "look": {"style": "abandoned", "haze": 5.0, "weather": 0.1, "envFog": 1, "bloom": 1.45, "saturation": 1.02, "tint": [1.08, 1.0, 0.86], "grime": 0.9, "arid": True,
             "paintSets": {"gravel": "gravel_mixed", "rock": "rock_slab_paint", "dirt": "ground_rubble_paint"}},
    "lighting": {"fill": 1.1, "ambient": [0.07, 0.05, 0.03], "hour": 17.4},
}


def build(ctx):
    build_variant(ctx, V)
