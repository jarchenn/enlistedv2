"""SD2 map 1: Rue Haute, a Search and Destroy map: a desert military compound of precast concrete walls, courtyards and
huts in hard noon light. Laid out on the 6 m bay of the compound wall (sd_common.Plan). Raiders spawn at the south
gate, Security in the north yard. A: the west vehicle court; B: the east supply yard; Mid: the parade square.

    <python> -I tools/mapgen/build.py sd_desert"""
import math
import os

from mapgen import (GroundPaint, Grid, Frame, MapBuilder, ScenarioBuilder, write_map, write_template, write_summary, asset_usage,
                    rng, look_yaw)
from maps.sd_common import rows_from, Plan, place_walls2, spread, place_masses, place_cover, wall_spot, site_signs

TID, NAME, VERSION, SEED = "sd_town", "Rue Haute", 1, 2613
CELL = 6.0
WALL = "mod_nt_compound_wall"            # perimeter (razor wire)
WALL_IN = "mod_nt_compound_wall_plain"   # inner yard walls
BREACH = "sd_town_wall_door"   # the connector doorways
CULVERT = "mod_nt_compound_wall_culvert"

# north at the top; column i from the west, row j from the south (the last row is j = 0)
ROWS = rows_from(15, 17, [
    ("D", 3, 14, 5, 15),                       # Security spawn (the gendarmerie yard)
    ("R", 2, 13, 9, 13), ("R", 2, 11, 2, 12),  # the back lanes behind both sites (defender rotation)
    ("A", 10, 11, 13, 14),                     # A Church Yard (north-east)
    ("B", 1, 7, 4, 10),                        # B Market Hall (west)
    ("M", 6, 7, 9, 9),                         # the market square (mid)
    ("T", 11, 1, 13, 2),                       # Raider spawn (the station yard)
    ("s", 12, 3, 12, 10),                      # Rue de l'Eglise to A
    ("t", 3, 1, 10, 1), ("t", 3, 2, 3, 6),     # Rue du Moulin to B
    ("p", 9, 2, 10, 2), ("p", 9, 3, 9, 6),     # mid approach
    ("c", 5, 10, 9, 10), ("c", 9, 11, 9, 11),  # the arcade A - B, along the square's north side
    ("k", 7, 11, 7, 12),                       # defender access to the square
    ("w", 5, 8, 5, 8),                         # the bakery passage to B
    ("n", 10, 8, 10, 10),                      # the cafe passage to A
    ("f", 4, 5, 8, 5), ("f", 10, 5, 11, 5),    # attacker fallback (the river lane)
])
# the layout scale knob (sd_common.Plan): these original columns / rows are drawn twice; tuned with --sd-timings
STRETCH_COLS = ()
STRETCH_ROWS = (2, 3, 4, 5)
# decorative buildings on the solid ground (asset, footprint w, d); cover per region (asset, half w, half d, count)
MASSES = [("vil_notary_house", 21.6, 15.4), ("vil_pension_hotel", 20.8, 13.6), ("vil_merchant_house", 15.6, 13.0), ("vil_bakery", 13.6, 14.6),
          ("vil_colombage_auge", 14.2, 11.4), ("vil_rendered_shutters", 13.6, 11.4), ("vil_terrace_pair", 12.2, 10.6), ("vil_corner_shop", 12.2, 11.0),
          ("vil_brick_townhouse", 10.4, 12.6), ("vil_townhouse_jettied", 11.4, 10.6)]
INTERIORS = "wnk"
A_COVER = [("drs_gravestone_cross", 0.5, 1.15, 4), ("drs_gravestone_round", 0.45, 1.1, 4), ("drs_tomb_chest", 0.55, 1.1, 2), ("drs_churchyard_cross", 0.9, 0.9, 1),
           ("drs_sandbag_wall", 1.5, 0.4, 2), ("drs_cart", 1.9, 0.95, 1)]
B_COVER = [("drs_cart", 1.9, 0.95, 2), ("drs_barrels", 1.0, 0.7, 2), ("drs_woodpile", 1.7, 0.65, 1), ("ph_wooden_crate_02", 0.5, 0.6, 3),
           ("drs_hay_bale_stack", 1.6, 0.6, 1), ("drs_sandbag_wall", 1.5, 0.4, 1)]
SITE_COVER = A_COVER
STREET_COVER = [("drs_barrels", 1.0, 0.7, 1), ("ph_wooden_crate_01", 0.5, 0.5, 1), ("drs_woodpile", 1.7, 0.65, 1)]
COVER = {"A": A_COVER, "B": B_COVER, "M": [("drs_cart", 1.9, 0.95, 2), ("drs_wooden_x_obstacle", 1.3, 1.1, 2), ("drs_sandbag_wall", 1.5, 0.4, 2)],
         "s": STREET_COVER, "t": STREET_COVER, "f": STREET_COVER[:2], "R": STREET_COVER[:2], "D": [("drs_barrels", 1.0, 0.7, 1)],
         "T": [("drs_hay_bale_stack", 1.6, 0.6, 1)], "c": [("drs_barrels", 1.0, 0.7, 1)]}
PLANT_A_CELL, PLANT_A_OFF = (12, 12), (0.0, 0.0)   # plant pads: an original cell and an offset (m)
PLANT_B_CELL, PLANT_B_OFF = (2, 8), (0.0, 0.0)
LINKS = {
    ((11, 1), (10, 1)): "open", ((11, 2), (10, 2)): "open", ((12, 2), (12, 3)): "open",
    ((9, 6), (9, 7)): "open", ((3, 6), (3, 7)): "open", ((12, 10), (12, 11)): "open",
    ((3, 5), (4, 5)): "open", ((8, 5), (9, 5)): "open", ((9, 5), (10, 5)): "open", ((11, 5), (12, 5)): "open",
    ((6, 8), (5, 8)): "open", ((5, 8), (4, 8)): BREACH,
    ((9, 8), (10, 8)): "open", ((10, 10), (10, 11)): BREACH,
    ((6, 9), (6, 10)): "open", ((8, 9), (8, 10)): "open",
    ((4, 10), (5, 10)): "open", ((9, 11), (10, 11)): "open",
    ((7, 10), (7, 11)): "open", ((7, 12), (7, 13)): "open",
    ((2, 11), (2, 10)): "open", ((9, 13), (10, 13)): "open",
    ((3, 14), (3, 13)): "open", ((5, 14), (5, 13)): "open",
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
    nw = place_walls2(b, plan, "sd_town_wall", "sd_town_wall", y=y0)
    n_walls = sum(nw.values())
    # ground: the sites, mid and the spawns are paved aprons; gravel tracks along the streets; sand drifts elsewhere
    for ch, kind in (("A", "grass"), ("B", "cobbles"), ("M", "cobbles"), ("T", "cobbles"), ("D", "cobbles"), ("s", "cobbles"), ("t", "cobbles"), ("p", "cobbles"), ("f", "dirt"), ("R", "cobbles"), ("c", "cobbles"), ("w", "cobbles"), ("n", "cobbles"), ("k", "cobbles")):
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
    b.place("drs_well", mx, mz, 0.0, check=False, cls="none", y=y0, tags={"landmark", "cover"})   # the market square's well: the mid landmark
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
            paint.poly("cobbles", [(x - h, z - h), (x + h, z - h), (x + h, z + h), (x - h, z + h)], weight=1.0, edge=0.3)
            n_roof += 1
        n_cover += place_cover(b, plan, ch, [("ph_metal_toolbox", 0.3, 0.15, 1), ("ph_wooden_crate_01", 0.5, 0.5, 1)], R, y0, lane=1.6)
    # signs at every site entrance
    n_sign = site_signs(b, plan, "A", "a", y0) + site_signs(b, plan, "B", "b", y0)
    # ---------------------------------------------------------------- scenario: sites, spawns, timings
    sc = ScenarioBuilder(b, defending="Axis")
    xA, zA = plan.cc(*PLANT_A_CELL)
    xB, zB = plan.cc(*PLANT_B_CELL)
    plantA, plantB = (xA + PLANT_A_OFF[0], zA + PLANT_A_OFF[1]), (xB + PLANT_B_OFF[0], zB + PLANT_B_OFF[1])
    aC, bC = plan.centre("A"), plan.centre("B")
    sc.objective("A Church Yard", plantA[0], plantA[1], radius=1.75, capture_time=0, attack_from=plan.cc(12, 8), y_min=-1.0, y_max=1.5,
                 slots=[{"pos": [aC[0] + 6.0, y0, aC[1] - 1.0], "yaw": 225.0, "kind": "position", "priority": 1},
                        {"pos": [aC[0] - 8.0, y0, aC[1] + 1.0], "yaw": 135.0, "kind": "position", "priority": 2}],
                 routes=[("main", [plan.cc(12, 3), plan.cc(12, 10)]), ("right", [plan.cc(9, 3), plan.cc(9, 7), plan.cc(10, 8), plan.cc(10, 10)])])
    sc.objective("B Market Hall", plantB[0], plantB[1], radius=1.75, capture_time=0, attack_from=plan.cc(3, 5), y_min=-1.0, y_max=1.5,
                 slots=[{"pos": [bC[0] - 6.0, y0, bC[1] - 1.0], "yaw": 135.0, "kind": "position", "priority": 1},
                        {"pos": [bC[0] + 8.0, y0, bC[1] + 1.0], "yaw": 225.0, "kind": "position", "priority": 2}],
                 routes=[("main", [plan.cc(5, 1), plan.cc(3, 6)]), ("left", [plan.cc(9, 3), plan.cc(9, 7), plan.cc(6, 8), plan.cc(5, 8)])])
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
            ("A Church Yard", aC, 12), ("B Market Hall", bC, 12), ("Market Square", plan.centre("M"), 12), ("Rue de l'Eglise", plan.cc(12, 6), 12),
            ("Rue du Moulin", plan.cc(3, 3), 10), ("Bakery", plan.centre("w"), 5), ("Cafe", plan.centre("n"), 7), ("Arcade", plan.centre("c"), 10),
            ("Back Lanes", plan.centre("R"), 14), ("River Lane", plan.cc(6, 5), 10), ("Raider Spawn", tC, 10), ("Security Spawn", dC, 10))],
        "bounds": [[round(plan.x0, 1), round(plan.z0, 1)], [round(-plan.x0, 1), round(plan.z0, 1)], [round(-plan.x0, 1), round(-plan.z0, 1)], [round(plan.x0, 1), round(-plan.z0, 1)]],
        "timingPoints": {"contest": [P(9, 7), P(12, 8), P(3, 6)]},
        "timingRoutes": [
            {"name": "back lanes", "kind": "rotation", "points": [P(2, 10), P(2, 11), P(9, 13), P(10, 13)]},
            {"name": "arcade", "kind": "mid", "points": [P(4, 10), P(5, 10), P(9, 10), P(9, 11), P(10, 11)]},
            {"name": "river lane", "kind": "fallback", "points": [P(12, 11), P(12, 5), P(3, 5), P(3, 7)]},
            {"name": "mid to A via cafe", "kind": "attackMid", "points": [P(12, 2), P(10, 2), P(9, 7), P(10, 8), P(10, 11), [round(plantA[0], 2), round(y0, 2), round(plantA[1], 2)]]},
            {"name": "mid to B via bakery", "kind": "attackMid", "points": [P(12, 2), P(10, 2), P(9, 7), P(5, 8), P(4, 8), [round(plantB[0], 2), round(y0, 2), round(plantB[1], 2)]]},
        ],
    }
    rules = {"mode": "snd", "sd": sd}
    err, warn = [], []

    # ---------------------------------------------------------------- output
    tdir = os.path.join(ctx.repo, "content", "templates")
    grid.write_png(os.path.join(tdir, TID + ".height.png"))
    a1 = sc.spawns[0]["pos"]
    cam = ([0.0, 120.0, -110.0], 0.0, -48.0)
    thumb_cam = ([aC[0] + 14.0, y0 + 1.7, aC[1] - 14.0], look_yaw(-1.0, 1.0), -4.0)
    eye = 1.65
    views = [{"name": "overview_high", "pos": [0.0, 130.0, -100.0], "yaw": 0.0, "pitch": -55.0}]
    for name, (i, j), (ti, tj) in (("a_site", (12, 10), (12, 12)), ("b_site", (3, 6), (2, 8)), ("mid", (9, 6), (8, 8)), ("t_spawn", (12, 1), (12, 3)), ("d_spawn", (4, 15), (4, 13)), ("bakery", (6, 8), (4, 8)), ("arcade", (5, 10), (9, 10))):
        x, z = plan.cc(i, j); tx, tz = plan.cc(ti, tj)
        views.append({"name": name + "_eye", "pos": [round(x, 2), round(y0 + eye, 2), round(z, 2)], "yaw": look_yaw(tx - x, tz - z), "pitch": 0.0, "gameplay": True})
    write_map(os.path.join(tdir, TID + ".nmap.json"), name=NAME, template_id=TID, template_version=VERSION, seed=SEED, width=W, depth=D,
              builder=b, scenario_json=sc.to_json(rules_extra=rules), explore_start=((a1[0], a1[1], a1[2]), 0.0), camera=cam, bookmarks={},
              extra={"groundPaint": paint.to_json(), "lighting": {"hour": 11.0}, "look": {"haze": 1.0, "weather": 0.75, "saturation": 0.95, "tint": [0.98, 0.99, 1.02]}})
    write_template(os.path.join(tdir, TID + ".json"), tid=TID, name=NAME, order=20, version=VERSION, seed=SEED,
                   description="Search and Destroy: a desert compound of precast concrete walls and courtyards in hard noon light. "
                               "A: the west vehicle court; B: the east supply yard; Mid: the parade square. 5 v 5.",
                   width=W, depth=D, margin=MARGIN, heightmap=grid.meta(TID + ".height.png"), water_level=-1000.0,
                   map_file=TID + ".nmap.json", camera=cam, thumbnail=TID + ".thumb.png", thumb_camera=thumb_cam,
                   extra={"userMap": True, "views": views})
    # SD2: not in the Play / New Map lists until this map reaches the game-ready bar (Sun Fort is listed)
    _tp = os.path.join(tdir, TID + ".json")
    import json as _json
    _t = _json.load(open(_tp, encoding="utf-8")); _t["available"] = False
    open(_tp, "w", encoding="utf-8", newline="\n").write(_json.dumps(_t, indent=1) + "\n")
    rep = {"instances": len(b.inst), "walls": nw, "masses": n_mass, "cover": n_cover, "plan": [plan.nx, plan.nz], "errors": err}
    write_summary(os.path.join(ctx.out, TID + ".summary.json"), rep)
    print("instances %d | walls %d, masses %d, cover %d | plan %d x %d cells of %.0f m" % (len(b.inst), n_walls, n_mass, n_cover, plan.nx, plan.nz, CELL))
