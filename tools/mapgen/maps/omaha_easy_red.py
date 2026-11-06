"""Default map 1: Normandy Beaches - Omaha, Easy Red / E-1 draw (6 June 1944). SPEC 3.3, layout
reference/layouts/omaha_easy_red_E1_layout.png. 1200 x 2400 m; attack from the sea (s = 0) inland (+Z).

All coordinates below are the spec's (x, s): x east, s metres inland from the sea edge of the map.
"""
import math
import os

from mapgen import (Grid, Frame, MapBuilder, ScenarioBuilder, Profile, sculpt_zones, stamp_valley, stamp_channel,
                    stamp_crater, stamp_flatten, stamp_bank, split_fields, hedgerow, orchard, sunken_lane, cut_gaps,
                    site_mg_nests, build_mg_nest, interlock_report, slots_from_structures, slot, catmull_rom,
                    write_map, write_template, write_summary, asset_usage, rect, rng, vnoise, fbm, dist, look_yaw,
                    asset_yaw_facing, asset_yaw_along, local_to_world, polyline_dist, in_poly, offset_polyline, resample)

TID = "omaha_easy_red"
VERSION = 2            # bump when the heightmap changes (saved crater damage is tied to it)
W, D, MARGIN, SEED = 1200, 2400, 100, 61944
WATER = 0.0

OBJ = {"A": (-230, 520), "B": (150, 800), "C": (-120, 1180), "D": (200, 1600), "E": (0, 2050)}
ATT = [(0, 40), (-100, 430), (0, 780), (100, 1200), (-150, 1680)]
DEF = [(-150, 820), (0, 1100), (250, 1480), (-150, 1900), (100, 2330)]

# E-1 draw centreline (x, s) and the dirt road through it to Saint-Laurent
DRAW = [(-15, 540), (-22, 650), (-38, 800), (-62, 940), (-100, 1090), (-120, 1170), (-122, 1215)]
ROAD = [(-12, 425), (-14, 470), (-20, 560), (-26, 680), (-40, 800), (-64, 945), (-102, 1090), (-118, 1180),
        (-96, 1290), (-58, 1420), (0, 1560), (60, 1650), (58, 1800), (30, 1920), (8, 2010), (0, 2050), (-4, 2200), (-10, 2400)]
VILLAGE_EW = [(-600, 2062), (-300, 2060), (-120, 2056), (0, 2050), (140, 2046), (330, 2058), (600, 2066)]
FARM_TRACK = [(60, 1650), (130, 1625), (185, 1612)]
SPALDING = [(268, 422), (262, 520), (256, 610), (246, 690), (232, 760), (214, 812), (196, 842)]
BLUFF_PATHS = [[(-330, 600), (-345, 660), (-320, 720), (-335, 790), (-310, 840)],
               [(420, 600), (436, 670), (410, 740), (425, 830)]]
WEST_LANE = [(-430, 1180), (-445, 1350), (-420, 1520), (-400, 1700), (-380, 1860)]   # sunken lane, west flank
EAST_LANE = [(380, 880), (395, 1050), (370, 1230), (360, 1400)]                       # sunken lane, east flank


def bluff_base(x):
    """s of the bluff foot (wanders +-18 m along the beach)."""
    return 600 + 18 * vnoise(x / 170.0, 3.1, 11)


HEIGHT = Profile([(-200, -2.4), (0, -1.2), (55, 0.0), (120, 0.35), (400, 1.6), (404, 1.9), (420, 2.4), (600, 3.6)])
BLUFF = Profile([(0, 3.6), (30, 6.5), (110, 30.0), (175, 42.5), (230, 45.0)])   # metres above the bluff foot


def height(x, s):
    if s < 600 - 40:
        h = HEIGHT(s)
        if 425 < s < 590:                                   # dunes and hollows on the sand shelf
            h += 0.55 * fbm(x / 28.0, s / 28.0, 5, 3)
        return h
    sb = bluff_base(x)
    h = BLUFF(s - sb) if s < sb + 230 else 45.0
    h = max(h, HEIGHT(min(s, 600)))
    if s > sb + 150:                                        # plateau: long low swells
        k = min(1.0, (s - sb - 150) / 120.0)
        h += k * (2.4 * fbm(x / 260.0, s / 260.0, 17, 3) + (s - 820) * 0.0024)
    if s > 1900:
        h += min(1.0, (s - 1900) / 200.0) * 2.0
    return h


def build(ctx):
    R = rng(SEED)
    fr = Frame(W, D)
    w = fr.w
    P = lambda pts: [w(x, s) for x, s in pts]
    grid = Grid(W, D, margin=MARGIN)

    # ------------------------------------------------------------------ terrain
    sculpt_zones(grid, fr, [{"s": (-400, 2800), "height": lambda x, s: height(x, s)}])
    draw_floor = Profile([(0.0, 3.2), (0.18, 9.0), (0.42, 24.0), (0.68, 37.0), (0.9, 43.0), (1.0, 44.5)])
    stamp_valley(grid, P(DRAW), floor=draw_floor, top_half=Profile([(0, 105), (0.35, 80), (0.7, 55), (1, 40)]),
                 floor_half=Profile([(0, 26), (0.5, 15), (1, 10)]), power=0.8)
    stamp_channel(grid, P(SPALDING[2:]), half_width=4.0, depth=3.0, bank=7.0)          # the defile Spalding climbed
    for pth in BLUFF_PATHS:
        stamp_channel(grid, P(pth), half_width=1.2, depth=0.5, bank=2.0)
    crater_pts = []
    # naval bombardment: big craters on the bluff and around WN64 / WN65; smaller ones on the sand
    for (n, x0, x1, s0, s1, r0, r1, d0, d1) in ((34, -560, 560, 615, 840, 3.5, 6.5, 1.2, 2.2),
                                                 (22, -560, 560, 430, 590, 3.0, 5.0, 0.8, 1.5),
                                                 (26, -560, 560, 90, 395, 2.8, 4.5, 0.6, 1.2),
                                                 (18, -350, 350, 840, 1300, 3.0, 5.0, 0.9, 1.6)):
        for _ in range(n):
            x, s = R.uniform(x0, x1), R.uniform(s0, s1)
            if abs(x - (-40)) < 30 and s > 600:
                continue                                      # keep the draw road open
            stamp_crater(grid, *w(x, s), R.uniform(r0, r1), R.uniform(d0, d1))
            crater_pts.append((x, s))
    # cover craters along the beach-exit lanes (SPEC 3.5: something every 30-50 m once off the shingle)
    for lane in ([(-100, 430), (-160, 500), (-230, 520)], [(-100, 430), (60, 560), (150, 800)], [(250, 430), (230, 600), (180, 780)]):
        for p in resample(lane, 38.0)[1:-1]:
            x, s = p[0] + R.uniform(-6, 6), p[1] + R.uniform(-6, 6)
            stamp_crater(grid, *w(x, s), R.uniform(3.2, 4.5), R.uniform(1.0, 1.6))
            crater_pts.append((x, s))
    for k in OBJ:                                           # level the objective cores a little
        stamp_flatten(grid, *w(*OBJ[k]), radius=14, falloff=18, strength=0.6)
    stamp_flatten(grid, *w(0, 2055), radius=55, falloff=30, strength=0.8)              # village square
    hmin, hmax = grid.quantise()

    b = MapBuilder(ctx.catalog, grid, fr, seed=SEED, water_level=WATER)
    for x, s in crater_pts:
        b.cover_points.append((*w(x, s), "crater"))

    # ------------------------------------------------------------------ surface routes
    b.route("sand", P([(-720, 230), (720, 230)]), 340, wet=0.55, keepout=False)        # tidal flat, standing water
    b.route("sand", P([(-720, 30), (720, 30)]), 60, wet=0.9, keepout=False)            # wet margin under the sea
    b.route("gravel", P([(-720, 411), (720, 411)]), 18, keepout=False)                 # shingle
    b.route("sand", P([(-720, 505), (720, 505)]), 175, strength=0.6, keepout=False)    # sand shelf with dune grass
    road = catmull_rom(P(ROAD), 6)
    b.route("lane", road, 4.5)
    b.route("lane", catmull_rom(P(VILLAGE_EW), 6), 5.0)
    b.route("lane", catmull_rom(P(FARM_TRACK), 6), 3.5)
    b.route("footpath", catmull_rom(P(SPALDING), 6), 1.6)
    for pth in BLUFF_PATHS:
        b.route("footpath", catmull_rom(P(pth), 6), 1.4)
    b.route("lane", catmull_rom(P(WEST_LANE), 6), 3.0)
    b.route("lane", catmull_rom(P(EAST_LANE), 6), 3.0)
    for k in OBJ:
        b.keepout_poly([w(OBJ[k][0] + 22 * math.cos(a / 8 * math.pi), OBJ[k][1] + 22 * math.sin(a / 8 * math.pi)) for a in range(16)])

    def inst_ids(lst):
        return [e["id"] for e in lst if e]

    # ------------------------------------------------------------------ sea and landing (s 0-60)
    for sx in range(-700, 701, 50):
        for ss in (-75, -25, 25):
            b.place("env_sea_shallows", *w(sx, ss), 0.0, check=False, y=WATER, cls="none", tags={"sea"})
    lcvps = []
    for (x, s, aid, yaw) in ((-9, 44, "bch_lcvp_beached", 4), (10, 47, "bch_lcvp_beached", -6), (-30, 30, "bch_lcvp_intact", 12),
                             (34, 22, "bch_lcvp_intact", -18), (-160, 52, "bch_lcvp_beached", 3), (180, 50, "bch_lcvp_beached", -2),
                             (-420, 140, "bch_lcvp_wrecked", 30), (95, 210, "bch_lcvp_wrecked", -24), (330, 120, "bch_lcvp_wrecked", 8),
                             (-250, 300, "bch_lcvp_wrecked", -40), (470, 280, "bch_lcvp_wrecked", 15)):
        e = b.place(aid, *w(x, s), yaw, water_ok=True, tags={"lcvp", "cover"})
        if e:
            lcvps.append(e)
    # the A1 rack: weapon crates in the well of the first LCVP
    host = lcvps[0]
    rx, rz = local_to_world((host["pos"][0], host["pos"][2]), host["yaw"], 0.0, 2.0)
    rack_a1 = b.place("rack_allied_crates", rx, rz, host["yaw"] + 90, check=False, cls="none", y=host["pos"][1] + 0.45, tags={"rack"})

    # ------------------------------------------------------------------ tidal flat: the four obstacle belts (s 60-400)
    hw = 400.0   # high-water line: obstacle distances are measured seaward from it
    gate_line = P([(-600, hw - 250), (600, hw - 250)])
    b.belt("bch_belgian_gate", gate_line, 11.0, R, jitter_along=0.12, jitter_across=2.0, yaw_jitter=6,
           gaps=[(310, 395), (640, 730), (1010, 1060)], yaw=180.0, tags={"belt1", "cover"}, water_ok=True)
    b.belt("bch_log_stake_mine", P([(-600, hw - 220), (600, hw - 220)]), 9.0, R, rows=2, row_gap=4.0, jitter_along=0.25,
           jitter_across=1.0, yaw_jitter=8, yaw=180.0, gaps=[(330, 370), (700, 720)], tags={"belt2"})
    b.belt("bch_log_ramp_mined", P([(-600, hw - 190), (600, hw - 190)]), 15.0, R, jitter_along=0.25, jitter_across=2.5,
           yaw_jitter=10, yaw=180.0, gaps=[(150, 190), (560, 600), (900, 940)], tags={"belt3"})
    b.belt("def_tank_trap", P([(-600, hw - 150), (600, hw - 150)]), 10.0, R, rows=2, row_gap=7.0, jitter_along=0.3,
           jitter_across=2.0, yaw_jitter=45, yaw=0.0, gaps=[(250, 280), (620, 650)], tags={"belt4", "cover"})

    # ------------------------------------------------------------------ shingle bank and seawall (s 400-420)
    sea_pt = w(0, -500)
    b.chain("bch_shingle_bank", P([(-600, 410), (600, 410)]), front=sea_pt, tags={"shingle", "cover"})
    b.chain("bch_seawall_wood", P([(20, 423), (600, 423)]), front=sea_pt, spacing=0.05, tags={"seawall", "cover"})

    # ------------------------------------------------------------------ sand shelf (s 420-600)
    for (x, s, yaw) in ((-470, 470, 10), (-380, 540, -20), (330, 500, 35), (430, 545, 0), (-60, 520, 70), (520, 470, -30), (-540, 560, 12), (210, 470, 80)):
        b.place("bcg_marsh_patch", *w(x, s), yaw, tags={"marsh"})
    # AT ditch across the E-1 mouth (spoil on the defender side, local -Z)
    ditch1 = b.chain("bch_at_ditch", P([(-160, 476), (70, 476)]), front=sea_pt, tags={"at_ditch", "cover"})
    # wire: concertina along the shelf, a post fence behind it, knife rests in the gaps
    b.chain("wire_concertina", P([(-600, 438), (600, 438)]), along="z", gaps=[(330, 345), (585, 600), (860, 872)], jitter_yaw=4, rng=R, tags={"wire"})
    b.chain("wire_fence", P([(-600, 455), (-170, 455)]), along="z", gaps=[(160, 172)], tags={"wire"})
    b.chain("wire_fence", P([(80, 455), (600, 455)]), along="z", gaps=[(180, 192)], tags={"wire"})
    for (x, s) in ((-270, 438), (-5, 438), (268, 438), (-440, 455), (273, 455)):
        b.place("wire_knife_rest", *w(x, s), 90 + R.uniform(-10, 10), tags={"wire"})
    # minefields behind the shingle and on the bluff, marked by fences and signs; the Spalding path runs between them
    mine_rows = [((-520, 485), (-330, 485)), ((-120, 500), (-30, 500)), ((90, 500), (230, 500)), ((290, 500), (480, 500)),
                 ((-480, 640), (-360, 640)), ((-260, 660), (-150, 660)), ((70, 650), (220, 650)), ((285, 640), (420, 640)),
                 ((90, 720), (225, 720)), ((280, 730), (360, 730))]
    for a_, b_ in mine_rows:
        b.chain("bch_minefield_fence", P([a_, b_]), front=w(a_[0], a_[1] + 50), spacing=0.4, tags={"minefield"})
        for _ in range(2):
            x = R.uniform(a_[0], b_[0])
            b.place("bch_minen_sign", *w(x, a_[1] - 1.5), 180 + R.uniform(-15, 15), tags={"minefield"})
    # the ruined beach house west of the draw
    b.place("house_stone_cottage", *w(-430, 575), 172, tags={"beach_house", "cover"})

    # ------------------------------------------------------------------ objective A: WN65 (Ruquet casemate)
    linked = {k: [] for k in OBJ}
    slots = {k: [] for k in OBJ}
    ax, as_ = OBJ["A"]
    r667 = b.place("aw_r667_casemate", *w(-196, 548), 180.0, check=False, tags={"WN65"})      # embrasure (+X) west along the beach
    linked["A"].append(r667["id"])
    for k in range(4):
        cx, cz = local_to_world((r667["pos"][0], r667["pos"][2]), 180.0, 2.6 + 0.7 * (k % 2), -6.4 - 0.8 * (k // 2))
        b.place("crate_pak40_he", cx, cz, R.uniform(-20, 20), ok_with={"flat", "none", "wire", "veg", "solid"}, tags={"WN65", "ammo"})
    b.place("path_concrete", *w(-199, 562), 0, extra={"path": {"width": 2.5, "length": 14}}, tags={"WN65"})
    for aid, x, s, yaw in (("aw_tobruk_mg", -252, 507, 180), ("aw_tobruk_turret", -214, 503, 175), ("aw_tobruk_mortar", -268, 548, 168),
                           ("bunker_mg_casemate", -300, 520, 205)):
        e = b.try_place(aid, *w(x, s), yaw, R, tags={"WN65"})
        if e:
            linked["A"].append(e["id"])
            slots["A"] += slots_from_structures(b, [e])
    trA = b.chain(["aw_trench_entry", "aw_trench_zigzag", "aw_trench_straight", "aw_trench_zigzag", "aw_trench_straight", "aw_trench_zigzag"],
                  P([(-292, 534), (-250, 530), (-205, 524), (-160, 528)]), front=sea_pt, tags={"WN65", "trench"}, check=False)
    linked["A"] += inst_ids(trA)
    slots["A"] += slots_from_structures(b, trA)[:8]
    b.chain("wire_concertina", P([(-320, 492), (-150, 488)]), along="z", tags={"WN65", "wire"})

    # ------------------------------------------------------------------ objective B: WN64 on the bluff top
    r612 = b.try_place("aw_r612_unfinished", *w(176, 812), 182, R, tags={"WN64"})
    linked["B"].append(r612["id"])
    slots["B"] += slots_from_structures(b, [r612])
    for aid, x, s, yaw in (("bunker_observation_post", 108, 798, 175), ("aw_dugout_personnel", 140, 846, 180), ("aw_tobruk_mortar", 205, 840, 190)):
        e = b.try_place(aid, *w(x, s), yaw, R, tags={"WN64"})
        if e:
            linked["B"].append(e["id"])
            slots["B"] += slots_from_structures(b, [e])
    crest = [(52, 786), (100, 782), (150, 788), (200, 792), (250, 800)]
    trB = b.chain(["aw_trench_entry", "aw_trench_straight", "aw_trench_zigzag", "aw_trench_straight", "aw_trench_zigzag", "aw_trench_straight",
                   "aw_trench_zigzag", "aw_trench_straight", "aw_trench_zigzag", "aw_trench_straight", "aw_trench_zigzag"],
                  P(crest), front=sea_pt, tags={"WN64", "trench"}, check=False)
    linked["B"] += inst_ids(trB)
    slots["B"] += slots_from_structures(b, trB)[:8]
    for x, s in ((120, 825), (182, 826)):     # mortar pits
        ins, sl = build_mg_nest(b, w(x, s), 180, style="pit", tags={"WN64", "mortar_pit"})
        linked["B"] += inst_ids(ins)

    # ------------------------------------------------------------------ objective C: draw-head roadblock
    road_at = lambda s_: min(((dist(p, w(-110, s_)), p) for p in road))[1]
    ditch2 = b.chain("bch_at_ditch", P([(-175, 1150), (-127, 1150)]), front=w(-120, 900), tags={"at_ditch2", "cover"}, check=False)
    ditch2 += b.chain("bch_at_ditch", P([(-100, 1152), (-60, 1152)]), front=w(-120, 900), tags={"at_ditch2", "cover"}, check=False)
    rb = b.place("bch_roadblock", *w(-113, 1154), asset_yaw_along(1, 0) + 180, check=False, tags={"roadblock", "cover"})
    linked["C"] += inst_ids(ditch2) + [rb["id"]]
    for aid, x, s, yaw in (("bunker_field_shelter", -168, 1212, 180), ("vil_stone_barn", -62, 1205, 95), ("aw_dugout_log", -150, 1240, 180)):
        e = b.try_place(aid, *w(x, s), yaw, R, tags={"C"})
        if e:
            linked["C"].append(e["id"])
            slots["C"] += slots_from_structures(b, [e])[:6]

    # ------------------------------------------------------------------ objective D: farm command post and battery
    farm = []
    for aid, x, s, yaw in (("vil_norman_farmhouse", 212, 1622, 180), ("vil_stone_barn", 258, 1648, 92), ("small_stone_dairy", 178, 1655, 5),
                           ("small_storehouse", 246, 1600, 270), ("bunker_field_shelter", 150, 1628, 180), ("bunker_command_shelter", 280, 1625, 180),
                           ("gun_flak18", 240, 1560, 180)):
        e = b.try_place(aid, *w(x, s), yaw, R, tags={"D"})
        if e:
            farm.append(e)
            linked["D"].append(e["id"])
            slots["D"] += slots_from_structures(b, [e])[:6]
    # HQ dressing in the farmyard: map table, chairs, papers, lamp; ammunition dump by the barn
    tx, tz = w(203, 1640)
    t = b.place("furn_table_dining", tx, tz, 8, tags={"D", "cp"})
    if t:
        b.place("furn_papers", tx, tz, 20, check=False, cls="none", y=t["pos"][1] + 0.76, tags={"D"})
        b.place("furn_lamp", *local_to_world((tx, tz), 8, 0.6, 0.1), 0, check=False, cls="none", y=t["pos"][1] + 0.76, tags={"D"})
        b.place("furn_books", *local_to_world((tx, tz), 8, -0.6, -0.1), 0, check=False, cls="none", y=t["pos"][1] + 0.76, tags={"D"})
        for lx, lz, yy in ((0.0, 0.8, 180), (0.0, -0.8, 0), (1.1, 0.0, 90)):
            b.place("furn_chair_ladder", *local_to_world((tx, tz), 8, lx, lz), 8 + yy, tags={"D"})
    for k in range(9):
        aid = ("crate_pak40_he", "crate_mixed_supply", "furn_crate")[k % 3]
        b.try_place(aid, *w(290 + (k % 3) * 1.6, 1662 + (k // 3) * 1.2), 90 + R.uniform(-8, 8), R, radius=2, tags={"D", "ammo_dump"})
    b.place("furn_bench", *w(190, 1640), 98, tags={"D"})
    b.place("furn_stool", *w(196, 1645), 0, tags={"D"})
    b.place("path_dirt", *w(215, 1640), 90, extra={"path": {"width": 3.0, "length": 18}}, tags={"D"})
    trD = b.chain(["aw_trench_entry", "aw_trench_straight", "aw_trench_zigzag", "aw_trench_straight", "aw_trench_corner"],
                  P([(118, 1568), (165, 1562), (205, 1560)]), front=w(150, 1200), tags={"D", "trench"})
    trD += b.chain(["aw_trench_straight", "aw_trench_zigzag", "aw_trench_straight", "aw_trench_entry"],
                   P([(268, 1552), (300, 1548), (330, 1548)]), front=w(250, 1200), tags={"D", "trench"})
    linked["D"] += inst_ids(trD)
    slots["D"] += slots_from_structures(b, trD)[:8]

    # ------------------------------------------------------------------ Saint-Laurent and objective E
    church = b.place("vil_church", *w(42, 2088), 180, check=False, tags={"E", "village"})
    linked["E"].append(church["id"])
    slots["E"] += [sl for sl in slots_from_structures(b, [church]) if sl["pos"][1] - church["pos"][1] > 8][:3] or \
        [slot(*w(42 - 11.2, 2088)[:1], church["pos"][1] + 11.0, w(0, 2088)[1], 0, "window", 3)]
    b.place("path_gravel", *w(40, 2077), 0, extra={"path": {"width": 2.0, "length": 9}}, tags={"E"})
    houses = [("house_farmhouse_2story", -48, 2082, 180), ("house_stone_cottage", -80, 2026, 0), ("large_village_inn", 70, 2024, 0),
              ("large_municipal_building", -120, 2085, 180), ("small_roadside_office", 26, 2030, 0), ("small_carpenter_workshop", -150, 2030, 0),
              ("large_village_schoolhouse", 160, 2090, 180), ("house_stone_cottage", 140, 2020, 0), ("large_stone_warehouse", -230, 2090, 180),
              ("house_farmhouse_2story", -40, 2140, 90), ("house_stone_cottage", 30, 2170, 270), ("small_storehouse", -45, 2200, 90),
              ("house_stone_cottage", 240, 2025, 5), ("house_farmhouse_2story", 250, 2095, 182), ("small_stone_dairy", -280, 2028, 0),
              ("house_stone_cottage", -330, 2092, 178), ("vil_norman_farmhouse", 380, 2090, 182), ("house_stone_cottage", 34, 2260, 268),
              ("house_farmhouse_2story", -42, 2300, 92), ("vil_stone_barn", -400, 2030, 0), ("small_carpenter_workshop", 320, 2022, 0)]
    village = []
    for aid, x, s, yaw in houses:
        e = b.try_place(aid, *w(x, s), yaw, R, radius=6, tags={"village", "cover"})
        if e:
            village.append(e)
            if dist((x, s), OBJ["E"]) < 130:
                linked["E"].append(e["id"])
    winslots = slots_from_structures(b, [e for e in village if dist((e["pos"][0], fr.s_of(e["pos"][2])), OBJ["E"]) < 130])
    slots["E"] += [s_ for s_ in winslots if s_["kind"] == "window"][:12]
    # garden walls and sandbag barricades at the crossroads
    for (x0, s0, x1, s1) in ((-95, 2105, -10, 2108), (90, 2110, 200, 2112), (-200, 2010, -110, 2006), (100, 2002, 200, 2005),
                             (-60, 2240, -60, 2320), (60, 2215, 60, 2300)):
        b.chain("stone_wall_low", P([(x0, s0), (x1, s1)]), spacing=0.1, tags={"village", "wall"})
    for (x, s, yaw) in ((-14, 2032, 0), (14, 2032, 0), (-16, 2068, 180), (16, 2070, 180), (-25, 2045, 90), (25, 2052, 270)):
        e = b.place("def_sandbag_straight", *w(x, s), yaw, tags={"E", "barricade", "cover"})
        if e:
            linked["E"].append(e["id"])

    # ------------------------------------------------------------------ the plateau: bocage, orchards, lanes, poles
    draw_poly = offset_polyline(P(DRAW), 75) + list(reversed(offset_polyline(P(DRAW), -75)))
    b.keepout_poly(draw_poly)
    bocage_region = P([(-600, 845), (600, 845), (600, 1965), (-600, 1965)])
    fields, lines = split_fields(bocage_region, R, field_area=(9000, 19000))
    sunk_w = catmull_rom(P(WEST_LANE), 6)
    sunk_e = catmull_rom(P(EAST_LANE), 6)
    cutters = [road, sunk_w, sunk_e, catmull_rom(P(FARM_TRACK), 6)]
    core = [w(*OBJ[k]) for k in OBJ] + [w(-120, 1155)]
    hedge_grp = b.new_group()

    def blocked_gaps(line):
        gaps = cut_gaps(line, cutters, 7.0)
        Ls = sum(dist(line[i], line[i + 1]) for i in range(len(line) - 1))
        s_, inside, s0 = 0.0, False, 0.0
        while s_ <= Ls:
            from mapgen import point_at
            p, _ = point_at(line, s_)
            bad = in_poly(p, draw_poly) or any(dist(p, c) < 45 for c in core) or fr.s_of(p[1]) > 1960 or \
                any(dist(p, (e["pos"][0], e["pos"][2])) < 16 for e in farm)
            if bad and not inside:
                inside, s0 = True, s_
            elif not bad and inside:
                inside = False
                gaps.append((s0, s_))
            s_ += 3.0
        if inside:
            gaps.append((s0, Ls))
        return gaps
    hedges, htrees = [], []
    for ln in lines:
        hp, ht = hedgerow(b, ln, R, group=hedge_grp, gaps=blocked_gaps(ln), tags={"bocage"})
        hedges += hp
        htrees += ht
    # field edges along the bocage region border (west / east map edges, the bluff crest line)
    for ln in (P([(-600, 845), (-600, 1965)]), P([(600, 845), (600, 1965)]), P([(-600, 862), (-140, 870)]), P([(70, 872), (600, 865)])):
        hp, ht = hedgerow(b, ln, R, group=hedge_grp, gaps=blocked_gaps(ln), tags={"bocage"})
        hedges += hp
        htrees += ht
    # hedgerow-corner MG positions at C (SPEC: MG nests in hedgerow corners)
    for x, s, yaw in ((-190, 1225, 180), (-50, 1235, 180)):
        e = b.place("bcg_hedgerow_corner", *w(x, s), yaw, group=hedge_grp, tags={"C", "bocage"}, ok_with={"flat", "none", "wire", "veg"})
        if e:
            linked["C"].append(e["id"])
    # orchards: the battery at D, the A5 orchard, one behind the village
    orchard_fields = []
    for target, (hw_, hd_) in (((240, 1565), (55, 40)), ((-150, 1700), (50, 45)), ((300, 1880), (45, 35)), ((-330, 1500), (50, 40))):
        f = P([(target[0] - hw_, target[1] - hd_), (target[0] + hw_, target[1] - hd_), (target[0] + hw_, target[1] + hd_), (target[0] - hw_, target[1] + hd_)])
        orchard_fields.append(f)
        orchard(b, f, R, tags={"bocage"})
    sunken_lane(b, sunk_w, tags={"west_lane"})
    sunken_lane(b, sunk_e, tags={"east_lane"})
    # telegraph poles along the road from E-1 to the village, the CP's wire branching to the farm
    pole_line = offset_polyline(catmull_rom(P(ROAD[8:16]), 6), 5.5)
    for p in resample(pole_line, 45.0):
        b.try_place("utility_pole", p[0], p[1], R.uniform(-5, 5), R, radius=2.0, tags={"poles"})
    for p in resample(offset_polyline(catmull_rom(P(FARM_TRACK), 6), 3.5), 40.0)[1:]:
        b.try_place("utility_pole", p[0], p[1], 0, R, radius=2.0, tags={"poles", "D"})
    # roadside poplars, a few field gates and post fences, felled-tree abatis across the flank lanes
    for p in resample(offset_polyline(catmull_rom(P(ROAD[11:15]), 6), -7.5), 14.0):
        b.place("tree_poplar", p[0], p[1], R.uniform(-180, 180), tags={"roadside"})
    for ln in R.sample(lines, 6):
        p, t = __import__("mapgen").point_at(ln, 6.0)
        b.place("field_gate", p[0], p[1], asset_yaw_along(*t), ok_with={"flat", "none", "wire", "veg", "solid"}, tags={"bocage"})
    for (a_, b_) in (((-560, 1300), (-470, 1310)), ((470, 1700), (560, 1690)), ((-560, 1820), (-480, 1835))):
        b.chain("fence_post_wire", P([a_, b_]), tags={"bocage"})
    for x, s in ((-440, 1460), (370, 1300), (-30, 1360), (330, 1720)):
        b.try_place("bcg_abatis", *w(x, s), R.uniform(-30, 30), R, radius=8, tags={"abatis", "cover"})

    # ------------------------------------------------------------------ MG nests: >= 3 per objective, interlocking
    def lanes_for(i):
        frm = ATT[i]
        to = list(OBJ.values())[i]
        mx, ms = (frm[0] + to[0]) / 2, (frm[1] + to[1]) / 2
        dx, ds = to[0] - frm[0], to[1] - frm[1]
        L = math.hypot(dx, ds)
        nx, ns = -ds / L, dx / L
        return [P([frm, (mx, ms), to]), P([frm, (mx + nx * 120, ms + ns * 120), to]), P([frm, (mx - nx * 120, ms - ns * 120), to])]
    nest_rep = {}
    styles = {"A": "sandbag", "B": "sandbag", "C": "sandbag", "D": "sandbag", "E": "sandbag"}
    for i, k in enumerate(OBJ):
        lanes = lanes_for(i)
        ok = lambda x, z: grid.slope_deg(x, z) < 20 and not b.in_keepout(x, z) and b.ground(x, z) > WATER + 0.5 and \
            not b.near(x, z, 6.0, classes={"solid"})
        fixed = [((e["pos"][0], e["pos"][2]), e["yaw"]) for e in b.inst if k in e["tags"] and e["asset"] in ("aw_tobruk_mg",)]
        sites = site_mg_nests(b, w(*OBJ[k]), lanes, 3, R, ring=(30, 120), fixed=fixed, accept=ok)
        built = []
        for pos, bore in sites:
            ins, sl = build_mg_nest(b, pos, bore, style=styles[k], tags={k})
            if sl:
                built.append((pos, bore))
                slots[k].append(sl)
                linked[k] += inst_ids(ins)
        nest_rep[k] = dict(interlock_report(b, fixed + built, lanes, w(*OBJ[k])), nests=len(built) + len(fixed))
    # Tobruk MG ring counts as an MG nest at A
    for sl in slots["A"]:
        if sl["kind"] == "embrasure" and False:
            pass

    # foxholes and hand-authored fallback positions around every objective
    for k in OBJ:
        ox, os_ = OBJ[k]
        n = 0
        for a in range(10):
            ang = math.radians(a * 36 + R.uniform(-10, 10))
            x, s = ox + 45 * math.sin(ang), os_ - 30 + 40 * math.cos(ang)
            if n >= 3:
                break
            e = b.place("aw_foxhole", *w(x, s), 180 + R.uniform(-25, 25), tags={k, "foxhole"})
            if e:
                n += 1
                linked[k].append(e["id"])
                slots[k] += slots_from_structures(b, [e])[:1] or [slot(e["pos"][0], e["pos"][1] - 1.2, e["pos"][2], 0.0, "foxhole", 1)]
        for a in range(4):
            ang = math.radians(a * 90 + 45)
            x, s = ox + 18 * math.sin(ang), os_ + 18 * math.cos(ang)
            wx, wz = w(x, s)
            slots[k].append(slot(wx, b.ground(wx, wz), wz, 0.0, "position", 1))

    # ------------------------------------------------------------------ attack-path cover fill (off the beach)
    def attack_paths():
        pts = [ATT[0]] + [OBJ[k] for k in OBJ]
        out = []
        for i in range(len(pts) - 1):
            out.append(P([pts[i], pts[i + 1]]))
            if i >= 1:
                out.append(P([ATT[i], list(OBJ.values())[i]]))
        return out
    filled = 0
    for path in attack_paths():
        for _ in range(3):
            gaps = [g for g in b.cover_gaps(path, lateral=15.0) if g[2] > 45 and fr.s_of(__import__("mapgen").point_at(path, g[0])[0][1]) > 420]
            if not gaps:
                break
            for g0, g1, L in gaps:
                n = int(L // 38)
                for j in range(1, n + 1):
                    p, t = __import__("mapgen").point_at(path, g0 + L * j / (n + 1))
                    s_ = fr.s_of(p[1])
                    aid = "bcg_abatis" if s_ > 850 else ("def_sandbag_curved" if s_ > 600 else "def_tank_trap")
                    e = b.try_place(aid, p[0] + t[1] * 6, p[1] - t[0] * 6, asset_yaw_along(*t), R, tries=20, radius=9, tags={"cover", "fill"})
                    filled += 1 if e else 0

    # ------------------------------------------------------------------ decorators
    bluff_poly = P([(-600, 596), (600, 596), (600, 842), (-600, 842)])
    bluff = lambda x, z: 598 < fr.s_of(z) < 842 and not in_poly((x, z), draw_poly) and grid.slope_deg(x, z) > 4
    b.scatter([("shrub_bramble", 3), ("shrub_hazel", 1)], bluff, 1 / 38.0, R, bounds=(-600, w(0, 598)[1], 600, w(0, 842)[1]),
              tags={"brush"}, accept=lambda x, z: 0.5 + 0.5 * fbm(x / 40.0, z / 40.0, 9, 2) + 0.55)
    draw_wood = lambda x, z: in_poly((x, z), draw_poly) and polyline_dist((x, z), road)[0] > 8 and 560 < fr.s_of(z) < 1150
    b.scatter([("tree_oak", 2), ("tree_ash", 3)], draw_wood, 1 / 170.0, R, bounds=(-260, w(0, 540)[1], 120, w(0, 1160)[1]), tags={"draw_wood"})
    b.scatter([("shrub_hazel", 2), ("shrub_bramble", 1)], draw_wood, 1 / 45.0, R, bounds=(-260, w(0, 540)[1], 120, w(0, 1160)[1]), tags={"draw_wood"})
    b.scatter([("shrub_bramble", 1)], lambda x, z: 430 < fr.s_of(z) < 595, 1 / 260.0, R, bounds=(-600, w(0, 430)[1], 600, w(0, 595)[1]), tags={"dune_brush"})
    # military dressing around the strongpoints: sandbags, crates, ammunition
    for k in OBJ:
        ox, os_ = OBJ[k]
        for _ in range(6):
            b.try_place(R.choice(["def_sandbag_straight", "def_sandbag_curved"]), *w(ox + R.uniform(-60, 60), os_ + R.uniform(-50, 40)), R.uniform(-180, 180), R, tags={k, "dressing"})
        for _ in range(4):
            b.try_place(R.choice(["crate_mixed_supply", "furn_crate"]), *w(ox + R.uniform(-40, 40), os_ + R.uniform(-30, 30)), R.uniform(-180, 180), R, tags={k, "dressing"})
    b.place("gun_m2hb_tripod", *w(-60, 425), 180.0, tags={"beach", "us"})          # a US .50 set up on the shingle

    # ------------------------------------------------------------------ scenario
    sc = ScenarioBuilder(b, defending="Axis")
    names = {"A": "WN65 - Ruquet casemate", "B": "WN64 - bluff-top strongpoint", "C": "E-1 draw head - roadblock",
             "D": "Farm command post and battery", "E": "Saint-Laurent church crossroads"}
    prev = ATT[0]
    for i, k in enumerate(OBJ):
        lanes = lanes_for(i)
        sc.objective(names[k], *w(*OBJ[k]), radius=25.0, capture_time=45.0, attack_from=w(*prev), linked=linked[k], slots=slots[k],
                     routes=[("main", resample(lanes[0], 60)), ("left", resample(lanes[2], 60)), ("right", resample(lanes[1], 60))])
        prev = OBJ[k]
    sc.spawn("attacker", [0], *w(*ATT[0]), label="A1 LCVPs", rack=rack_a1)
    sc.spawn("attacker", [1], *w(*ATT[1]), label="A2 shingle", rack="rack_allied_crates", rack_offset=(0.0, 4.5))
    sc.spawn("attacker", [2], *w(*ATT[2]), label="A3 bluff crest west of WN64")
    sc.spawn("attacker", [3], *w(*ATT[3]), label="A4 hedgerows past the draw head")
    sc.spawn("attacker", [4], *w(*ATT[4]), label="A5 orchard")
    for i, p in enumerate(DEF):
        sc.spawn("defender", [i], *w(*p), label="D%d" % (i + 1), face=w(*ATT[i]))
    err, warn = sc.validate()
    for m in sc.spawns:
        if not m["rack"]:
            err.append("spawn %s has no rack" % m["label"])

    # ------------------------------------------------------------------ output
    tdir = ctx.templates
    grid.write_png(os.path.join(tdir, TID + ".height.png"))
    a1 = w(*ATT[0])
    cam = ([0.0, 95.0, w(0, -150)[1]], 180.0, -14.0)
    tp, tt = w(-345, 445), w(-150, 585)
    thumb_cam = ([tp[0], 27.0, tp[1]], look_yaw(tt[0] - tp[0], tt[1] - tp[1]), -8.0)
    mp = write_map(os.path.join(tdir, TID + ".nmap.json"), name="Normandy Beaches - Omaha, Easy Red", template_id=TID, template_version=VERSION,
                   seed=SEED, width=W, depth=D, builder=b, scenario_json=sc.to_json(),
                   explore_start=((a1[0], b.ground(*a1), a1[1]), 180.0), camera=cam,
                   bookmarks={str(i + 1): {"position": [w(*OBJ[k])[0] - 40, b.ground(*w(*OBJ[k])) + 45, w(*OBJ[k])[1] - 110], "yaw": 180 + 20, "pitch": -20}
                              for i, k in enumerate(OBJ)})
    write_template(os.path.join(tdir, TID + ".json"), tid=TID, name="Normandy Beaches - Omaha, Easy Red", order=1, version=VERSION, seed=SEED,
                   description="Easy Red and the E-1 draw, 6 June 1944: the tidal flat with its four obstacle belts, the shingle and seawall, "
                               "wire, mines and the anti-tank ditch on the sand shelf, WN65 (R667 casemate) and WN64 (unfinished R612) on the "
                               "bluff, the draw road through the bocage to Saint-Laurent-sur-Mer. Five objectives, Allies attack from the "
                               "landing craft. 1200 x 2400 m.",
                   width=W, depth=D, margin=MARGIN, heightmap=grid.meta(TID + ".height.png"), water_level=WATER, map_file=TID + ".nmap.json",
                   camera=cam, thumbnail=TID + ".thumb.png", thumb_camera=thumb_cam)
    use = asset_usage(b)
    rep = {"instances": len(b.inst), "heights": [hmin, hmax], "errors": err, "warnings": warn, "mgNests": nest_rep, "coverFill": filled,
           "slots": {k: len(slots[k]) for k in slots}, "linked": {k: len(linked[k]) for k in linked},
           "rejects": {"%s/%s" % k: v for k, v in sorted(b.rejects.items())}, "use": dict(sorted(use.items())),
           "unused": [a for a in ctx.catalog.ids() if a not in use],
           "coverGaps": {str(i): [g for g in b.cover_gaps(p) if g[2] > 50 and fr.s_of(__import__("mapgen").point_at(p, g[0])[0][1]) > 420] for i, p in enumerate(attack_paths())}}
    write_summary(os.path.join(ctx.out, TID + ".summary.json"), rep)
    print("instances %d | errors %s | warnings %d | nests %s" % (len(b.inst), err, len(warn), {k: (v["nests"], round(v["crossfire"], 2)) for k, v in nest_rep.items()}))
