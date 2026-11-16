"""NT3 variant B: Outpost Woods (126 x 140 m Battle Royale). A palisaded army camp in a forest clearing (barracks,
motor pool, hut, parade ground), a radar and a field bunker on the wooded ridge to the west, a sawmill on the logging
track in the south-west, and cabins by a pond fed by a creek in the south-east. Dense dark woodland, ferns, bracken
and mossy boulders; mist. See outpost_common.py.

    <python> -I tools/mapgen/build.py outpost_woods"""
import math

from mapgen import (polyline_dist, catmull_rom, rect, circle_poly, fbm, smoothstep, stamp_valley, point_at, polyline_length, look_yaw,
                    in_poly)
from maps.outpost_common import build_variant, range_layout, HX, HZ, WRECKS

WATER = 11.0
CAMP = (14.0, -66.0, 60.0, -14.0)                    # x0, z0, x1, z1 of the palisade
POND = (32.0, 52.0, 11.0, 8.5)                       # centre x, z, radius x, z
CREEK = catmull_rom([(6.0, -78.0), (10.0, -50.0), (6.0, -20.0), (2.0, 6.0), (14.0, 30.0), (26.0, 46.0)])
TRACK = catmull_rom([(-6.0, 78.0), (-10.0, 42.0), (-4.0, 12.0), (2.0, -14.0), (8.0, -34.0), (16.0, -40.0)])
SPUR_RADAR = catmull_rom([(-4.0, 12.0), (-20.0, -2.0), (-32.0, -18.0), (-42.0, -36.0)])
SPUR_MILL = catmull_rom([(-9.0, 36.0), (-22.0, 38.0)])
SPUR_POND = catmull_rom([(-6.0, 26.0), (12.0, 34.0), (24.0, 34.0), (44.0, 36.0)])


def pond_k(x, z):
    cx, cz, rx, rz = POND
    return math.hypot((x - cx) / rx, (z - cz) / rz) * (1.0 + 0.07 * math.sin(3 * math.atan2(z - cz, x - cx)))


def height(x, z):
    h = 14.0 + 2.0 * fbm(x / 40.0, z / 40.0, 5, 4) + 0.7 * fbm(x / 14.0, z / 14.0, 6, 2)
    ridge = smoothstep(-12.0, -46.0, x + 5.0 * fbm(z / 30.0, 1.7, 8, 3))
    h += 10.0 * ridge + 3.0 * math.exp(-((x + 44) ** 2 + (z + 42) ** 2) / (2 * 14.0 ** 2))
    h = max(h, WATER + 1.7)
    pk = pond_k(x, z)
    if pk < 1.6:   # the pond's bowl
        h = min(h, WATER + 1.7 - 2.9 * (1.0 - smoothstep(0.4, 1.6, pk)) - 0.0)
    return h


def stamps(grid):
    L = polyline_length(CREEK)
    stamp_valley(grid, CREEK, floor=lambda u: max(WATER - 0.4, grid.sample(*point_at(CREEK, L * u)[0]) - 1.6), top_half=lambda u: 5.0,
                 floor_half=lambda u: 1.2, power=0.9)


SITES = [
    ("mod_nt_barracks", 40.0, -56.0, 0.0), ("mod_nt_motor_pool", 44.0, -24.0, 180.0), ("mod_mil_hut", 23.0, -39.0, 0.0),
    ("mod_guard_tower", 17.0, -63.0, 45.0), ("mod_guard_tower", 57.0, -17.0, 225.0),
    ("mod_nt_radar_tower", -44.0, -44.0, 0.0), ("bunker_field_shelter", -36.0, -22.0, 60.0),
    ("mod_nt_workshop", -32.0, 38.0, 90.0), ("mod_nt_barn_steel", -42.0, 60.0, 0.0),
    ("mod_nt_house_small", 52.0, 38.0, 270.0), ("mod_nt_house_two", 10.0, 58.0, 180.0),
    ("mod_helipad_ground", 44.0, 2.0, 0.0),   # NT4: its own clearing between the camp and the pond
    ("mod_tent_sleeping", 16.5, -7.0, 0.0), ("mod_tent_mess", 16.5, 7.0, 0.0),   # NT4: the tent lines by the track
    ("mod_tent_medical", 27.5, -7.0, 0.0), ("mod_tent_command", 27.5, 7.0, 0.0),
]
INFO = {("mod_tent_sleeping", 16.5, -7.0): {"d": 1, "mil": True, "spot": True}, ("mod_tent_mess", 16.5, 7.0): {"d": 1, "mil": True, "spot": True},
        ("mod_tent_medical", 27.5, -7.0): {"d": 1, "mil": True, "spot": True}, ("mod_tent_command", 27.5, 7.0): {"d": 1, "mil": True, "spot": True},
        ("mod_guard_tower", 17.0, -63.0): {"d": 1, "mil": True}, ("mod_guard_tower", 57.0, -17.0): {"d": 1, "mil": True},("mod_nt_barracks", 40.0, -56.0): {"d": 1, "mil": True}, ("mod_nt_motor_pool", 44.0, -24.0): {"d": 1, "mil": True},
        ("mod_mil_hut", 23.0, -39.0): {"d": 1, "mil": True}, ("mod_nt_radar_tower", -44.0, -44.0): {"d": 2, "mil": True},
        ("bunker_field_shelter", -36.0, -22.0): {"d": 2, "mil": True},
        ("mod_nt_workshop", -32.0, 38.0): {"d": 3}, ("mod_nt_barn_steel", -42.0, 60.0): {"d": 3},
        ("mod_nt_house_small", 52.0, 38.0): {"d": 4}, ("mod_nt_house_two", 10.0, 58.0): {"d": 4}}


def clearing(x, z):
    x0, z0, x1, z1 = CAMP
    return ((x0 - 3 < x < x1 + 3 and z0 - 3 < z < z1 + 3) or math.hypot(x + 44, z + 44) < 11 or math.hypot(x + 36, z - 46) < 20
            or pond_k(x, z) < 1.35 or math.hypot(x - 52, z - 38) < 9 or (29 < x < 59 and -13 < z < 17)
            or (-32 < x < 3 and -66 < z < -46) or (10 < x < 34 and -14 < z < 16) or math.hypot(x - 10, z - 58) < 10 or math.hypot(x + 36, z + 22) < 7)


def forest(x, z):
    return not clearing(x, z) and polyline_dist((x, z), CREEK)[0] > 2.5


def dress(b, R, prop, drop, line, paint, rep):
    x0, z0, x1, z1 = CAMP
    # the palisade: west gate to the track, a breach in the north, the south side half fallen
    nf = 0
    for pts, gaps in (([(x0, z0), (x1, z0)], [(28.0, 33.0)]), ([(x1, z0), (x1, z1)], []), ([(x1, z1), (x0, z1)], [(8.0, 12.0), (24.0, 34.0)]),
                      ([(x0, z1), (x0, z0)], [(20.0, 29.0)])):
        nf += len(b.chain("mod_palisade_fence", pts, along="x", gaps=gaps, check=False, tags={"palisade"}))
    rep["palisade"] = nf
    for x, z, yaw in ((x0 - 2.5, -44.0, 90.0), (x0 - 2.5, -34.0, 90.0)):
        prop("mod_sandbag_position", x, z, yaw + 180.0, tags={"gate", "cover"})
    prop("mod_jersey_barrier_damaged", x0 - 5.0, -39.0, 80.0, tags={"gate", "cover"})
    for aid, x, z, yaw in (("car_truck_canopy", 34.0, -34.0, 0.0), ("car_humvee", 50.0, -36.0, 160.0), ("car_truck_cargo", 26.0, -22.0, 90.0),
                           ("mod_camo_net", 50.0, -44.0, 0.0), ("mod_camo_net", 30.0, -46.0, 15.0), ("mod_antenna_mast", 56.0, -60.0, 0.0),
                           ("mod_generator_set", 20.0, -55.0, 90.0), ("mod_mil_generator", 56.0, -48.0, 0.0), ("mod_container_20_damaged", 30.0, -20.0, 0.0),
                           ("mod_crate_wood", 40.0, -42.0, 20.0), ("furn_mil_jerrycans", 52.0, -30.0, 0.0), ("mod_barrel_steel_rust", 38.0, -32.0, 0.0),
                           ("mod_barrel_group", 58.0, -40.0, 0.0), ("mod_pallet_stack", 46.0, -44.0, 30.0), ("mod_hesco_3x1", 36.0, -40.0, 0.0),
                           ("mod_hesco_1x2", 45.0, -48.0, 0.0), ("mod_mil_utility_green", 18.0, -24.0, 0.0), ("mod_tyre_pile", 54.0, -24.0, 0.0)):
        prop(aid, x, z, yaw, tags={"camp", "cover"})
    # the radar hill: sandbags round the tower, a trench of sandbag wall to the bunker
    for a in range(0, 360, 45):
        prop("def_sandbag_straight", -44.0 + 6.5 * math.cos(math.radians(a)), -44.0 + 6.5 * math.sin(math.radians(a)), -a + 90.0, tags={"radar", "cover"})
    for aid, x, z, yaw in (("mod_generator_set", -38.0, -50.0, 30.0), ("mod_crate_wood", -50.0, -38.0, 0.0), ("mod_razor_coil", -48.0, -54.0, 20.0),
                           ("mod_razor_coil", -54.0, -46.0, 100.0)):
        prop(aid, x, z, yaw, tags={"radar", "cover"})
    # the sawmill yard: lumber, logs, a dead truck
    for aid, x, z, yaw in (("mod_lumber_stack", -22.0, 46.0, 0.0), ("mod_lumber_stack", -24.0, 54.0, 90.0), ("mod_pallet_stack", -20.0, 30.0, 10.0),
                           ("mod_box_truck", -48.0, 42.0, 80.0), ("mod_pcar_pickup_wreck", -26.0, 64.0, 200.0), ("mod_skip", -46.0, 32.0, 0.0),
                           ("mod_barrel_steel_rust", -38.0, 48.0, 0.0), ("mod_tyre_pile", -18.0, 60.0, 0.0), ("mod_concrete_pipe_stack", -50.0, 50.0, 90.0)):
        prop(aid, x, z, yaw, tags={"mill", "cover"})
    # cabins at the pond: a jetty of pallets, a boat, a car in the reeds
    for aid, x, z, yaw in (("mod_pcar_offroad_wreck", 44.0, 30.0, 40.0), ("mod_nt_boat_trailer", 46.0, 46.0, 120.0), ("mod_nt_boat_motor", 20.0, 46.0, 70.0),
                           ("mod_pcar_wagon_wreck", 18.0, 66.0, 300.0), ("mod_trash_bin", 6.0, 50.0, 0.0), ("mod_jerry_cans", 56.0, 30.0, 0.0)):
        prop(aid, x, z, yaw, tags={"pond", "cover"})
    # the logging track: abandoned cars, a fallen-tree roadblock, ruts
    L = polyline_length(TRACK)
    for s, lane in ((22.0, 1.4), (58.0, -1.4), (96.0, 1.4), (110.0, -1.2)):
        (px, pz), t = point_at(TRACK, s)
        drop(R.choice(WRECKS), px - t[1] * lane, pz + t[0] * lane, math.degrees(math.atan2(t[0], t[1])) + R.uniform(-20, 20), dy=0.05, cls="solid")
    for s in range(0, int(L), 3):   # the forest taking the track back
        (px, pz), t = point_at(TRACK, s + R.uniform(0, 2))
        o = R.uniform(-2.2, 2.2)
        drop(R.choice(["nat_green_tufts", "nat_fern_clump", "nat_tallgrass_patch", "mod_weeds_clump", "nat_bracken_clump"]), px - t[1] * o, pz + t[0] * o,
             R.uniform(0, 360), cls="veg")
    # NT4: Ural-4320 trucks (drivable): one at the motor pool, one stopped on the logging track
    prop("car_ural_4320", 45.0, -39.0, 92.0, tags={"camp", "cover", "vehicle"})
    prop("car_ural_4320", 31.0, 34.5, 95.0, tags={"track", "cover", "vehicle"})   # on the pond spur
    # NT4: the range in its clearing west of the creek, firing uphill into the radar hill
    range_layout(b, R, prop, (-3.0, -56.0), 90.0, (15.0, 20.0, 25.0), 2, rep)
    for x, z, yaw in ((22.0, 0.0, 0.0), (11.0, 0.0, 90.0)):   # camo nets and crates round the tents
        prop("mod_camo_net", x, z, yaw, tags={"tents"})
    for x, z in ((21.5, -1.5), (25.0, 1.0)):
        e = prop("mod_ammo_crate_old", x, z, R.uniform(0, 360), tags={"tents", "cover"})
        if e is not None:
            e["extra"] = e.get("extra") or {}; e["extra"]["loot"] = {"spot": True, "size": "small", "district": 1, "military": True}
    # NT4: the track churned to mud with puddles, trunks fallen across it
    paint.line("mud", TRACK, 2.8, weight=0.85, edge=0.8)
    for s in range(10, int(L) - 6, 17):
        (px, pz), t = point_at(TRACK, s + R.uniform(-4, 4))
        drop("wat_puddles", px, pz, math.degrees(math.atan2(t[0], t[1])) + R.uniform(-20, 20), cls="flat")
    for s in (34.0, 76.0, 120.0):
        (px, pz), t = point_at(TRACK, min(s, L - 5))
        drop("mod_fallen_tree", px + t[1] * 2.0, pz - t[0] * 2.0, math.degrees(math.atan2(-t[1], t[0])) + R.uniform(-25, 25), cls="solid")
    # the creek: mossy stones, fallen saplings, reeds near the pond
    Lc = polyline_length(CREEK)
    for k in range(40):
        (px, pz), t = point_at(CREEK, R.uniform(0, Lc))
        o = R.uniform(-2.5, 2.5)
        drop(R.choice(["nat_boulder_schist_m", "nat_boulder_granite_s", "nat_stones_granite_patch", "nat_fern_clump", "nat_nettle_clump", "nat_sapling_ash"]),
             px - t[1] * o, pz + t[0] * o, R.uniform(0, 360), cls="solid")
    # the ridge: schist outcrops along the steep side
    for z in range(-66, 66, 5):
        x = -12.0 - 34.0 * 0.5 - 5.0 * fbm(z / 30.0, 1.7, 8, 3)
        if polyline_dist((x, z), SPUR_RADAR)[0] < 6.0 or clearing(x, z):
            continue
        drop(R.choice(["nat_outcrop_schist", "nat_boulder_schist_slab", "nat_outcrop_schist", "nat_boulders_granite_group"]), x + R.uniform(-3, 3), z, R.uniform(0, 360),
             dy=-0.9, cls="solid")


V = {
    "tid": "outpost_woods", "name": "Outpost Woods", "order": 11, "seed": 3202, "water": WATER,
    "description": "A small Battle Royale map, 126 x 140 m (1/8 of Newtown): an abandoned army camp behind a timber palisade in "
                   "a forest clearing, a radar and a field bunker on the wooded ridge, a sawmill on the logging track, and "
                   "cabins by a pond fed by a creek. Dense woodland, ferns and mist. Military loot (level-3 armour) in the camp, "
                   "the radar and the bunker. 16 players by default, short storm circles. Battle Royale, Team Deathmatch and Solo.",
    "height": height, "stamps": [stamps], "roads": [], "track_paint": "dirt",
    "tracks": [(TRACK, 4.0), (SPUR_RADAR, 3.2), (SPUR_MILL, 3.5), (SPUR_POND, 3.0)],
    "sites": SITES, "site_info": INFO,
    "flats": [(rect(CAMP[0] + 1, CAMP[1] + 1, CAMP[2] - 1, CAMP[3] - 1), None), (circle_poly(-44.0, -44.0, 9.0, 16), None)],
    "paint": [("poly", "leaf_litter", rect(-HX - 70, -HZ - 70, HX + 70, HZ + 70), {"weight": 0.55, "edge": 0.5}),
              ("poly", "grass", rect(CAMP[0] - 2, CAMP[1] - 2, CAMP[2] + 2, CAMP[3] + 2), {"weight": 0.8, "edge": 3.0, "ragged": 3.0, "seed": 51}),
              ("poly", "dirt", rect(28.0, -48.0, 56.0, -34.0), {"weight": 0.8, "edge": 2.0, "ragged": 2.5, "seed": 52}),
              ("poly", "gravel", rect(30.0, -36.0, 58.0, -30.0), {"weight": 0.6, "edge": 2.0, "ragged": 2.0, "seed": 53}),
              ("poly", "dirt", circle_poly(-34.0, 46.0, 16.0, 16), {"weight": 0.6, "edge": 4.0, "ragged": 4.0, "seed": 54}),
              ("poly", "mud", circle_poly(POND[0], POND[1], 15.0, 20), {"weight": 0.6, "edge": 3.0, "ragged": 3.0, "seed": 55}),
              ("line", "mud", CREEK, 3.5, {"weight": 0.75, "edge": 1.5}),
              ("poly", "dry_grass", circle_poly(-44.0, -44.0, 12.0, 14), {"weight": 0.5, "edge": 3.0, "ragged": 3.0, "seed": 56})],
    "dress": dress,
    "helis": [("mod_helipad_ground", 0.0, 0.165, 200.0)],
    "yard_paint": ["dirt", "gravel", "pebbles", "dirt"],
    "wall_dress": ["nat_nettle_clump", "nat_bramble_thicket", "nat_fern_clump", "mod_weeds_clump", "mod_rubble_small", "nat_bracken_clump", "nat_tallgrass_patch"],
    "scatter": [([("nt3_tree_forest_oak", 4), ("nt3_tree_forest_ash", 4), ("nt3_tree_forest_poplar", 2), ("tree_oak", 1)], forest, 1 / 42.0, 4.6),
                ([("nat_fern_clump", 4), ("nat_bracken_clump", 4), ("nat_bramble_thicket", 1), ("nat_nettle_clump", 1), ("shrub_hazel", 1), ("nat_sapling_ash", 1),
                  ("nat_sapling_oak", 1), ("nat_brush_rank", 1)], forest, 1 / 4.5, 1.0, {"ok_with": {"flat", "none", "wire", "veg"}}),
                ([("nat_boulder_schist_m", 2), ("nat_boulder_granite_s", 2), ("nat_boulder_granite_l", 1)], forest, 1 / 220.0, 6.0),
                ([("nat_tallgrass_patch", 3), ("nat_green_tufts", 2), ("nat_meadow_mixed", 1), ("mod_weeds_clump", 2), ("nat_nettle_clump", 1),
                  ("nat_sapling_ash", 1), ("nat_bramble_thicket", 1)], clearing, 1 / 9.0, 1.2,
                 {"ok_with": {"flat", "none", "wire", "veg"}, "accept": lambda x, z: 0.15 + 0.85 * max(0.0, math.sin(x * 0.31 + math.sin(z * 0.23) * 2.0) * math.sin(z * 0.27 + x * 0.05))})],   # clumped, not even
    "decor": ["nt3_tree_forest_oak", "nt3_tree_forest_ash", "nt3_tree_forest_oak", "nt3_tree_forest_poplar", "tree_oak"],
    "decor_n": 300,
    "tdm": {"A": (-52.0, 66.0), "B": (54.0, 22.0)},
    "objective": (36.0, -40.0), "objective_name": "Forest camp", "attack_from": (-4.0, 12.0), "defend_at": (46.0, -46.0),
    "districts": [("Forest camp", (38.0, -40.0)), ("Radar hill", (-44.0, -40.0)), ("Sawmill", (-34.0, 44.0)), ("Pond cabins", (34.0, 44.0))],
    "views": [("camp_eye", (4.0, -38.0), (40.0, -40.0)), ("radar_eye", (-36.0, -34.0), (-46.0, -46.0)), ("mill_eye", (-8.0, 28.0), (-32.0, 42.0)),
              ("pond_eye", (18.0, 40.0), (34.0, 54.0)), ("range_eye", (5.0, -53.0), (-25.0, -56.0)), ("tents_eye", (4.0, -20.0), (22.0, -2.0)),
              ("helipad_eye", (34.0, 19.0), (44.0, 2.0))],
    "start": ((4.0, -38.0), (36.0, -2.0)),
    "thumb_cam": ([70.0, 75.0, 110.0], look_yaw(-70.0, -110.0), -30.0),
    "water_bodies": [{"id": 1, "kind": "pond", "level": WATER, "seed": [POND[0], POND[1]],
                      "outline": [[round(POND[0] + math.cos(a) * POND[2] * 1.12, 3), round(POND[1] + math.sin(a) * POND[3] * 1.12, 3)]
                                  for a in [2 * math.pi * k / 48 for k in range(48)]]}],
    "look": {"style": "abandoned", "haze": 1.4, "weather": 0.35, "envFog": 0, "bloom": 1.4, "saturation": 1.14, "tint": [1.04, 1.01, 0.93], "grime": 1.0,
             "paintSets": {"gravel": "gravel_mixed", "dirt": "ground_rubble_paint", "pebbles": "rock_slab_paint"}},
    "lighting": {"fill": 1.3, "ambient": [0.04, 0.06, 0.045]},
    "views_clear": 3.5,
}


def build(ctx):
    build_variant(ctx, V)
