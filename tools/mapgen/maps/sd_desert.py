"""SD2 map 3: Sun Fort, a Search and Destroy map: a desert military compound of precast concrete walls, courtyards and
huts in hard noon light. Laid out on the 6 m bay of the compound wall (sd_common.Plan). Raiders spawn at the south
gate, Security in the north yard. A: the west vehicle court; B: the east supply yard; Mid: the parade square.

    <python> -I tools/mapgen/build.py sd_desert"""
import math
import os

from mapgen import (GroundPaint, Grid, Frame, MapBuilder, ScenarioBuilder, write_map, write_template, write_summary, asset_usage,
                    rng, look_yaw)
from maps.sd_common import Plan, place_walls2, spread, place_masses, place_cover, wall_spot

TID, NAME, VERSION, SEED = "sd_desert", "Sun Fort", 1, 2611
CELL = 6.0
WALL = "mod_nt_compound_wall"            # perimeter (razor wire)
WALL_IN = "mod_nt_compound_wall_plain"   # inner yard walls
BREACH = "mod_nt_compound_wall_breach"
CULVERT = "mod_nt_compound_wall_culvert"

# north at the top; column i from the west, row j from the south (the last row is j = 0)
ROWS = [
    "###############",   # 16
    "######DDD######",   # 15  D  Security spawn (north yard)
    "######DDD######",   # 14
    "###RRRRRRRRR###",   # 13  R  service spine (defender rotation)
    "#AAAA##k##BBBB#",   # 12  k  defender access to mid
    "#AAAA##k##BBBB#",   # 11
    "#AAAAcccccBBBB#",   # 10  c  central passage A - B (exposed to mid)
    "#AAAAMMMMMBBBB#",   # 9   M  parade square (mid)
    "##s#wMMMMMn#t##",   # 8   w  workshop to A; n  maintenance to B
    "##sffffpfffft##",   # 7   f  attacker fallback (backstreet) under mid; p  mid approach
    "##s####p####t##",   # 6
    "##s####p####t##",   # 5
    "##s####p####t##",   # 4
    "##s####p####t##",   # 3
    "##ssssTTT###t##",   # 2   s  A street;  T  Raider spawn (south gate)
    "######TTTtttt##",   # 1   t  B alley
    "###############",   # 0
]
# the layout scale knob (sd_common.Plan): these original columns / rows are drawn twice; tuned with --sd-timings
STRETCH_COLS = (6, 8)
STRETCH_ROWS = (3, 4, 5, 13, 14)
# decorative buildings on the solid ground (asset, footprint w, d); cover per region (asset, half w, half d, count)
MASSES = [("mod_nt_barracks", 30, 12), ("mod_nt_motor_pool", 24, 14), ("mod_nt_hq", 20, 14), ("mod_nt_workshop", 18, 12),
          ("mod_nt_armory", 16, 12), ("mod_nt_pump_station", 14, 10), ("mod_mil_hut", 11.8, 19.6), ("mod_nt_site_office", 7, 3)]
# site identity: A, the vehicle court (wrecks under camo nets); B, the supply yard (pallets, boxes, a damaged container)
INTERIORS = "wnk"
A_COVER = [("mod_pcar_pickup_wreck", 1.2, 2.9, 1), ("mod_car_shell_burned", 1.2, 2.6, 1), ("mod_tyre_pile", 1.3, 1.0, 1), ("mod_hesco_3x2", 1.6, 0.6, 2),
           ("mod_sandbag_position", 1.4, 1.1, 1), ("ph_barrel_01", 0.35, 0.35, 2), ("ph_metal_jerrycan", 0.2, 0.1, 2)]
B_COVER = [("mod_container_20_damaged", 1.3, 3.3, 1), ("mod_pallet_stack", 1.0, 0.45, 3), ("mod_hesco_3x2", 1.6, 0.6, 1), ("ph_wooden_crate_02", 0.5, 0.6, 3),
           ("ph_cardboard_box_01", 0.25, 0.25, 3), ("mod_barrel_group", 0.7, 0.7, 1), ("mod_generator_set", 1.65, 0.65, 1)]
SITE_COVER = [("mod_container_20", 1.3, 3.1, 1), ("mod_hesco_3x2", 1.6, 0.6, 2), ("mod_sandbag_position", 1.4, 1.1, 1),
              ("ph_wooden_military_crate", 0.6, 0.4, 2), ("ph_barrel_01", 0.35, 0.35, 2), ("ph_old_military_crate", 0.5, 0.35, 1)]
STREET_COVER = [("ph_wooden_crate_01", 0.5, 0.5, 2), ("ph_barrel_02", 0.35, 0.35, 2), ("ph_metal_jerrycan", 0.2, 0.1, 1), ("ph_old_tyre", 0.4, 0.4, 1)]
COVER = {"A": A_COVER, "B": B_COVER, "M": [("mod_hesco_3x1", 1.6, 0.6, 2), ("ph_wooden_crate_02", 0.5, 0.5, 2), ("ph_barrel_03", 0.35, 0.35, 2)],
         "s": STREET_COVER, "t": STREET_COVER, "f": STREET_COVER[:2], "R": STREET_COVER[:2], "D": [("ph_plastic_crate_01", 0.4, 0.3, 2), ("ph_portable_generator", 0.5, 0.35, 1)],
         "T": [("ph_plastic_crate_02", 0.4, 0.3, 2), ("ph_metal_toolbox", 0.3, 0.15, 1)], "c": [("ph_barrel_01", 0.35, 0.35, 1)]}
PLANT_A_CELL, PLANT_A_OFF = (2, 10), (0.5, -3.0)   # plant pads: an original cell and an offset (m)
PLANT_B_CELL, PLANT_B_OFF = (12, 11), (-0.5, -7.5)
LINKS = {
    ((6, 2), (5, 2)): "open", ((8, 1), (9, 1)): "open", ((7, 2), (7, 3)): "open",
    ((7, 7), (7, 8)): "open",
    ((6, 7), (7, 7)): "open", ((7, 7), (8, 7)): "open", ((3, 7), (2, 7)): "open", ((11, 7), (12, 7)): "open",
    ((2, 8), (2, 9)): "open", ((12, 8), (12, 9)): "open",
    ((4, 8), (5, 8)): "open", ((4, 8), (4, 9)): BREACH,
    ((10, 8), (9, 8)): "open", ((10, 8), (10, 9)): BREACH,
    ((6, 9), (6, 10)): "open", ((8, 9), (8, 10)): "open",
    ((5, 10), (4, 10)): "open", ((9, 10), (10, 10)): "open",
    ((7, 11), (7, 10)): "open", ((7, 13), (7, 12)): "open",
    ((3, 13), (3, 12)): "open", ((4, 13), (4, 12)): "open", ((10, 13), (10, 12)): "open", ((11, 13), (11, 12)): "open",
    ((6, 14), (6, 13)): "open", ((8, 14), (8, 13)): "open",
}


def build(ctx):
    R = rng(SEED)
    plan = Plan(ROWS, CELL, LINKS, stretch_cols=STRETCH_COLS, stretch_rows=STRETCH_ROWS)
    bad = plan.check_links()
    assert not bad, bad
    W = D = plan.nx * CELL + 2 * 30.0
    MARGIN = 60
    frame = Frame(W, D)
    grid = Grid(W, D, margin=MARGIN, base=10.0)
    grid.quantise()
    b = MapBuilder(ctx.catalog, grid, frame, seed=SEED)
    paint = GroundPaint()
    paint.poly("sand", [(-W / 2 - MARGIN, -D / 2 - MARGIN), (W / 2 + MARGIN, -D / 2 - MARGIN), (W / 2 + MARGIN, D / 2 + MARGIN), (-W / 2 - MARGIN, D / 2 + MARGIN)], weight=1.0, edge=0.5)
    y0 = b.ground(0.0, 0.0)
    nw = place_walls2(b, plan, WALL_IN, WALL, y=y0)
    n_walls = sum(nw.values())
    # ground: the sites, mid and the spawns are paved aprons; gravel tracks along the streets; sand drifts elsewhere
    for ch, kind in (("A", "rock"), ("B", "rock"), ("M", "rock"), ("T", "gravel"), ("D", "gravel")):
        for i, j in plan.cells(ch):
            x, z = plan._cc(i, j)
            h = CELL / 2
            paint.poly(kind, [(x - h, z - h), (x + h, z - h), (x + h, z + h), (x - h, z + h)], weight=0.9, edge=0.6, ragged=0.4, seed=i * 31 + j)
    for ch in "stfpRc":
        for i, j in plan.cells(ch):
            x, z = plan._cc(i, j)
            paint.poly("gravel", [(x - 1.4, z - 1.4), (x + 1.4, z - 1.4), (x + 1.4, z + 1.4), (x - 1.4, z + 1.4)], weight=0.55, edge=1.2, ragged=1.0, seed=i * 17 + j)

    # ---------------------------------------------------------------- dressing: masses behind the walls, a mid landmark, cover
    n_mass = place_masses(b, plan, MASSES, R, y0)
    mx, mz = plan.centre("M")
    b.place("mod_nt_radar_tower", mx, mz + 1.0, 0.0, check=False, cls="none", y=y0, tags={"landmark", "cover"})   # the parade square's landmark: splits mid
    _xa, _za = plan.cc(*PLANT_A_CELL); _xb, _zb = plan.cc(*PLANT_B_CELL)
    PADS = [(_xa + PLANT_A_OFF[0], _za + PLANT_A_OFF[1], 3.5), (_xb + PLANT_B_OFF[0], _zb + PLANT_B_OFF[1], 3.5)]
    n_cover = 0
    for ch, items in COVER.items():
        n_cover += place_cover(b, plan, ch, items, R, y0, keep=PADS)
    # interiors: the Workshop, Maintenance and the defender access are roofed rooms between the lanes (a slab on the walls,
    # a caged lamp per bay, a concrete floor, some clutter off the route)
    n_roof = 0
    for ch in INTERIORS:
        for i, j in plan.cells(ch):
            x, z = plan._cc(i, j)
            b.place("sd_roof_bay", x, z, 0.0, check=False, cls="none", y=y0, tags={"roof", "interior"})
            b.place("tun_lamp", x + 0.8, z, 0.0, check=False, cls="none", y=y0 + 3.0, tags={"lamp", "interior"})
            h = CELL / 2
            paint.poly("rock", [(x - h, z - h), (x + h, z - h), (x + h, z + h), (x - h, z + h)], weight=1.0, edge=0.3)
            n_roof += 1
        n_cover += place_cover(b, plan, ch, [("ph_metal_toolbox", 0.3, 0.15, 1), ("ph_wooden_crate_01", 0.5, 0.5, 1)], R, y0, lane=1.6)
    # camo nets over the A vehicle court (walk-under, no collision of note)
    ax, az = plan.centre("A")
    b.place("mod_camo_net", ax - 4.0, az - 3.0, 10.0, check=False, cls="none", y=y0, tags={"dress"})
    # signs: each site's letter on its walls facing the main entries, arrows at the street ends
    signs = []
    for ch, letter in (("A", "a"), ("B", "b")):
        cx, cz = plan.centre(ch)
        for near in ((cx, cz - 30.0), (cx, cz + 30.0), (cx + (25.0 if ch == "A" else -25.0), cz)):
            x, z, yaw = wall_spot(plan, ch, near)
            if all(abs(x - q[0]) + abs(z - q[1]) > 4.0 for q in signs):
                b.place("sd_sign_%s_wall" % letter, x, z, yaw, check=False, cls="none", y=y0, tags={"sign"})
                signs.append((x, z))
    for ch, letter, near in (("s", "a", plan.cc(2, 8)), ("t", "b", plan.cc(12, 8)), ("f", "a", plan.cc(3, 7)), ("f", "b", plan.cc(11, 7)), ("R", "a", plan.cc(4, 13)), ("R", "b", plan.cc(10, 13))):
        x, z, yaw = wall_spot(plan, ch, near)
        b.place("sd_sign_%s_arrow" % letter, x, z, yaw, check=False, cls="none", y=y0, tags={"sign"})
        signs.append((x, z))

    # ---------------------------------------------------------------- scenario: sites, spawns, timings
    sc = ScenarioBuilder(b, defending="Axis")
    xA, zA = plan.cc(*PLANT_A_CELL)
    xB, zB = plan.cc(*PLANT_B_CELL)
    plantA, plantB = (xA + PLANT_A_OFF[0], zA + PLANT_A_OFF[1]), (xB + PLANT_B_OFF[0], zB + PLANT_B_OFF[1])
    aC, bC = plan.centre("A"), plan.centre("B")
    sc.objective("A Vehicle Court", plantA[0], plantA[1], radius=1.75, capture_time=0, attack_from=plan.cc(2, 6), y_min=-1.0, y_max=1.5,
                 slots=[{"pos": [aC[0] + 6.0, y0, aC[1] - 1.0], "yaw": 225.0, "kind": "position", "priority": 1},
                        {"pos": [aC[0] - 8.0, y0, aC[1] + 1.0], "yaw": 135.0, "kind": "position", "priority": 2}],
                 routes=[("main", [plan.cc(2, 3), plan.cc(2, 8)]), ("left", [plan.cc(7, 4), plan.cc(7, 8), plan.cc(4, 8), plan.cc(4, 9)])])
    sc.objective("B Supply Yard", plantB[0], plantB[1], radius=1.75, capture_time=0, attack_from=plan.cc(12, 6), y_min=-1.0, y_max=1.5,
                 slots=[{"pos": [bC[0] - 6.0, y0, bC[1] - 1.0], "yaw": 135.0, "kind": "position", "priority": 1},
                        {"pos": [bC[0] + 8.0, y0, bC[1] + 1.0], "yaw": 225.0, "kind": "position", "priority": 2}],
                 routes=[("main", [plan.cc(12, 3), plan.cc(12, 8)]), ("right", [plan.cc(7, 4), plan.cc(7, 8), plan.cc(10, 8), plan.cc(10, 9)])])
    for o in sc.objectives:
        o["pos"][1] = y0
    tC, dC = plan.centre("T"), plan.centre("D")
    tc = [plan._cc(i, j) for i, j in plan.cells("T")]
    dc = [plan._cc(i, j) for i, j in plan.cells("D")]
    for k, (x, z) in enumerate(spread(tc + [(p[0] + 2.0, p[1]) for p in tc], tC, 5)):
        sc.spawn("attacker", [0, 1], x, z, label="Raiders %d" % (k + 1), face=(x, z + 10.0), rack=None)
    for k, (x, z) in enumerate(spread(dc + [(p[0] + 2.0, p[1]) for p in dc], dC, 5)):
        sc.spawn("defender", [0, 1], x, z, label="Security %d" % (k + 1), face=(x, z - 10.0), rack=None)
    for s in sc.spawns:
        s["team"] = -1
        s["pos"][1] = y0
    P = lambda i, j, dx=0.0, dz=0.0: [round(plan.cc(i, j)[0] + dx, 2), round(y0, 2), round(plan.cc(i, j)[1] + dz, 2)]
    sd = {
        "callouts": [{"name": n, "x": round(c[0], 1), "z": round(c[1], 1), "r": r} for n, c, r in (
            ("A Vehicle Court", aC, 12), ("B Supply Yard", bC, 12), ("Parade Square", plan.centre("M"), 14), ("A Street", plan.cc(2, 5), 10),
            ("B Alley", plan.cc(12, 4), 10), ("Workshop", plan.centre("w"), 6), ("Maintenance", plan.centre("n"), 6), ("Central Passage", plan.centre("c"), 10),
            ("Service Spine", plan.centre("R"), 12), ("Backstreet", plan.cc(4, 7), 8), ("Raider Spawn", tC, 10), ("Security Spawn", dC, 10))],
        "bounds": [[round(plan.x0, 1), round(plan.z0, 1)], [round(-plan.x0, 1), round(plan.z0, 1)], [round(-plan.x0, 1), round(-plan.z0, 1)], [round(plan.x0, 1), round(-plan.z0, 1)]],
        "timingPoints": {"contest": [P(7, 8), P(2, 8), P(12, 8)]},
        "timingRoutes": [
            {"name": "spine", "kind": "rotation", "points": [P(4, 12), P(4, 13), P(10, 13), P(10, 12)]},
            {"name": "central passage", "kind": "mid", "points": [P(4, 10), P(10, 10)]},
            {"name": "backstreet", "kind": "fallback", "points": [P(2, 9), P(2, 7), P(12, 7), P(12, 9)]},
            {"name": "mid to A via workshop", "kind": "attackMid", "points": [P(7, 2), P(7, 8), P(4, 8), P(4, 9), [round(plantA[0], 2), round(y0, 2), round(plantA[1], 2)]]},
            {"name": "mid to B via maintenance", "kind": "attackMid", "points": [P(7, 2), P(7, 8), P(10, 8), P(10, 9), [round(plantB[0], 2), round(y0, 2), round(plantB[1], 2)]]},
        ],
    }
    rules = {"mode": "snd", "sd": sd}
    err, warn = [], []

    # ---------------------------------------------------------------- output
    tdir = os.path.join(ctx.repo, "content", "templates")
    grid.write_png(os.path.join(tdir, TID + ".height.png"))
    a1 = sc.spawns[0]["pos"]
    cam = ([0.0, 120.0, -110.0], 0.0, -48.0)
    _tx, _tz = plan.cc(4, 9)
    _ax, _az = plan.cc(1, 12)
    thumb_cam = ([_tx + 1.5, y0 + 2.4, _tz - 1.5], look_yaw(_ax - _tx, _az - _tz), -6.0)   # A Vehicle Court across from its south-east corner
    eye = 1.65
    views = [{"name": "overview_high", "pos": [0.0, 130.0, -100.0], "yaw": 0.0, "pitch": -55.0}]
    for name, (i, j), (ti, tj) in (("a_site", (3, 9), (2, 11)), ("b_site", (11, 9), (12, 11)), ("mid", (7, 7), (7, 9)), ("t_spawn", (7, 1), (7, 3)), ("d_spawn", (7, 15), (7, 13)), ("workshop", (5, 8), (4, 9)), ("a_main", (2, 8), (2, 11))):
        x, z = plan.cc(i, j); tx, tz = plan.cc(ti, tj)
        views.append({"name": name + "_eye", "pos": [round(x, 2), round(y0 + eye, 2), round(z, 2)], "yaw": look_yaw(tx - x, tz - z), "pitch": 0.0, "gameplay": True})
    write_map(os.path.join(tdir, TID + ".nmap.json"), name=NAME, template_id=TID, template_version=VERSION, seed=SEED, width=W, depth=D,
              builder=b, scenario_json=sc.to_json(rules_extra=rules), explore_start=((a1[0], a1[1], a1[2]), 0.0), camera=cam, bookmarks={},
              extra={"groundPaint": paint.to_json(), "lighting": {"hour": 15.5}, "look": {"haze": 0.6, "saturation": 1.05, "tint": [1.06, 1.0, 0.9], "paintSets": {"rock": "apron_concrete_paint", "gravel": "gravel_mixed"}}})
    write_template(os.path.join(tdir, TID + ".json"), tid=TID, name=NAME, order=20, version=VERSION, seed=SEED,
                   description="Search and Destroy: a desert compound of precast concrete walls and courtyards in hard noon light. "
                               "A: the west vehicle court; B: the east supply yard; Mid: the parade square. 5 v 5.",
                   width=W, depth=D, margin=MARGIN, heightmap=grid.meta(TID + ".height.png"), water_level=-1000.0,
                   map_file=TID + ".nmap.json", camera=cam, thumbnail=TID + ".thumb.png", thumb_camera=thumb_cam,
                   extra={"userMap": True, "views": views})
    rep = {"instances": len(b.inst), "walls": nw, "masses": n_mass, "cover": n_cover, "plan": [plan.nx, plan.nz], "errors": err}
    write_summary(os.path.join(ctx.out, TID + ".summary.json"), rep)
    print("instances %d | walls %d, masses %d, cover %d | plan %d x %d cells of %.0f m" % (len(b.inst), n_walls, n_mass, n_cover, plan.nx, plan.nz, CELL))
