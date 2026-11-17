"""SD2 map 3: Sun Fort, a Search and Destroy map: a desert military compound of precast concrete walls, courtyards and
huts in hard noon light. Laid out on the 6 m bay of the compound wall (sd_common.Plan). Raiders spawn at the south
gate, Security in the north yard. A: the west vehicle court; B: the east supply yard; Mid: the parade square.

    <python> -I tools/mapgen/build.py sd_desert"""
import math
import os

from mapgen import (GroundPaint, Grid, Frame, MapBuilder, ScenarioBuilder, write_map, write_template, write_summary, asset_usage,
                    rng, look_yaw)
from maps.sd_common import Plan, place_walls, spread

TID, NAME, VERSION, SEED = "sd_desert", "Sun Fort", 1, 2611
CELL = 6.0
WALL = "mod_nt_compound_wall"
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
    n_walls = place_walls(b, plan, WALL, y=y0)

    # ---------------------------------------------------------------- scenario: sites, spawns, timings
    sc = ScenarioBuilder(b, defending="Axis")
    xA, zA = plan.cc(2, 10)
    xB, zB = plan.cc(12, 11)
    plantA, plantB = (xA + 0.5, zA - 3.0), (xB - 0.5, zB - 7.5)
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
    thumb_cam = ([aC[0] + 14.0, y0 + 1.7, aC[1] - 14.0], look_yaw(-1.0, 1.0), -4.0)
    eye = 1.65
    views = [{"name": "overview_high", "pos": [0.0, 130.0, -100.0], "yaw": 0.0, "pitch": -55.0}]
    for name, (i, j), (ti, tj) in (("a_site", (3, 9), (2, 11)), ("b_site", (11, 9), (12, 11)), ("mid", (7, 7), (7, 9)), ("t_spawn", (7, 1), (7, 3)), ("d_spawn", (7, 15), (7, 13))):
        x, z = plan.cc(i, j); tx, tz = plan.cc(ti, tj)
        views.append({"name": name + "_eye", "pos": [round(x, 2), round(y0 + eye, 2), round(z, 2)], "yaw": look_yaw(tx - x, tz - z), "pitch": 0.0, "gameplay": True})
    write_map(os.path.join(tdir, TID + ".nmap.json"), name=NAME, template_id=TID, template_version=VERSION, seed=SEED, width=W, depth=D,
              builder=b, scenario_json=sc.to_json(rules_extra=rules), explore_start=((a1[0], a1[1], a1[2]), 0.0), camera=cam, bookmarks={},
              extra={"groundPaint": paint.to_json(), "lighting": {"hour": 12.5}, "look": {"haze": 0.6, "saturation": 1.05, "tint": [1.06, 1.0, 0.9]}})
    write_template(os.path.join(tdir, TID + ".json"), tid=TID, name=NAME, order=20, version=VERSION, seed=SEED,
                   description="Search and Destroy: a desert compound of precast concrete walls and courtyards in hard noon light. "
                               "A: the west vehicle court; B: the east supply yard; Mid: the parade square. 5 v 5.",
                   width=W, depth=D, margin=MARGIN, heightmap=grid.meta(TID + ".height.png"), water_level=-1000.0,
                   map_file=TID + ".nmap.json", camera=cam, thumbnail=TID + ".thumb.png", thumb_camera=thumb_cam,
                   extra={"userMap": True, "views": views})
    rep = {"instances": len(b.inst), "walls": n_walls, "plan": [plan.nx, plan.nz], "errors": err}
    write_summary(os.path.join(ctx.out, TID + ".summary.json"), rep)
    print("instances %d | walls %d | plan %d x %d cells of %.0f m" % (len(b.inst), n_walls, plan.nx, plan.nz, CELL))
