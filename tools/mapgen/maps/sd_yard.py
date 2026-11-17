"""SD2 map 2: Night Yard (Rue Haute's timed layout mirrored east-west), a Search and Destroy map: a desert military compound of precast concrete walls, courtyards and
huts in hard noon light. Laid out on the 6 m bay of the compound wall (sd_common.Plan). Raiders spawn at the south
gate, Security in the north yard. A: the west vehicle court; B: the east supply yard; Mid: the parade square.

    <python> -I tools/mapgen/build.py sd_desert"""
import math
import os

from mapgen import (GroundPaint, Grid, Frame, MapBuilder, ScenarioBuilder, write_map, write_template, write_summary, asset_usage,
                    rng, look_yaw)
from maps.sd_common import rows_from, Plan, place_walls2, spread, place_masses, place_cover, wall_spot, place_container_walls, site_signs, wall_lamps

TID, NAME, VERSION, SEED = "sd_yard", "Night Yard", 1, 2612
CELL = 6.0
WALL = "mod_nt_compound_wall"            # perimeter (razor wire)
WALL_IN = "mod_nt_compound_wall_plain"   # inner yard walls
BREACH = "mod_nt_compound_wall_breach"
CULVERT = "mod_nt_compound_wall_culvert"

# north at the top; column i from the west, row j from the south (the last row is j = 0)
ROWS = rows_from(15, 17, [
    ("D", 9, 14, 11, 15),                       # Security spawn (the gendarmerie yard)
    ("R", 5, 13, 12, 13), ("R", 12, 11, 12, 12),  # the back lanes behind both sites (defender rotation)
    ("A", 1, 11, 4, 14),                     # A Church Yard (north-east)
    ("B", 10, 7, 13, 10),                        # B Market Hall (west)
    ("M", 5, 7, 8, 9),                         # the market square (mid)
    ("T", 1, 1, 3, 2),                       # Raider spawn (the station yard)
    ("s", 2, 3, 2, 10),                      # Rue de l'Eglise to A
    ("t", 4, 1, 11, 1), ("t", 11, 2, 11, 6),     # Rue du Moulin to B
    ("p", 4, 2, 5, 2), ("p", 5, 3, 5, 6),     # mid approach
    ("c", 5, 10, 9, 10), ("c", 5, 11, 5, 11),  # the arcade A - B, along the square's north side
    ("k", 7, 11, 7, 12),                       # defender access to the square
    ("w", 9, 8, 9, 8),                         # the bakery passage to B
    ("n", 4, 8, 4, 10),                      # the cafe passage to A
    ("f", 6, 5, 10, 5), ("f", 3, 5, 4, 5),    # attacker fallback (the river lane)
])
# the layout scale knob (sd_common.Plan): these original columns / rows are drawn twice; tuned with --sd-timings
STRETCH_COLS = ()
STRETCH_ROWS = (2, 3, 4, 5)
# decorative buildings on the solid ground (asset, footprint w, d); cover per region (asset, half w, half d, count)
MASSES = [("mod_nt_warehouse", 36, 24), ("mod_nt_barn_steel", 20, 14), ("mod_nt_workshop", 18, 12), ("mod_nt_pump_station", 14, 10),
          ("mod_container_40_stack", 2.6, 12.4), ("mod_nt_site_office", 7, 3)]
INTERIORS = "wnk"
A_COVER = [("mod_pallet_stack", 1.0, 0.45, 3), ("mod_container_20", 1.3, 3.1, 1), ("ph_wooden_crate_02", 0.5, 0.6, 3), ("tun_cable_reel", 0.6, 0.4, 2),
           ("ph_cardboard_box_01", 0.25, 0.25, 3), ("mod_lumber_stack", 1.9, 0.65, 1)]
B_COVER = [("mod_container_20_damaged", 1.3, 3.3, 1), ("mod_container_20", 1.3, 3.1, 1), ("mod_concrete_pipe_stack", 1.35, 1.5, 1), ("mod_barrel_group", 0.7, 0.7, 2),
           ("tun_cable_reel", 0.6, 0.4, 2), ("ph_barrel_01", 0.35, 0.35, 2)]
SITE_COVER = A_COVER
STREET_COVER = [("ph_wooden_crate_01", 0.5, 0.5, 2), ("ph_barrel_02", 0.35, 0.35, 2), ("mod_pallet_stack", 1.0, 0.45, 1), ("ph_old_tyre", 0.4, 0.4, 1)]
COVER = {"A": A_COVER, "B": B_COVER, "M": [("mod_container_20", 1.3, 3.1, 2), ("mod_pallet_stack", 1.0, 0.45, 2), ("ph_barrel_03", 0.35, 0.35, 2)],
         "s": STREET_COVER, "t": STREET_COVER, "f": STREET_COVER[:2], "R": STREET_COVER[:2], "D": [("ph_plastic_crate_01", 0.4, 0.3, 2)],
         "T": [("ph_plastic_crate_02", 0.4, 0.3, 2)], "c": [("ph_barrel_01", 0.35, 0.35, 1)]}
PLANT_A_CELL, PLANT_A_OFF = (2, 12), (0.0, 0.0)   # plant pads: an original cell and an offset (m)
PLANT_B_CELL, PLANT_B_OFF = (12, 8), (0.0, 0.0)
LINKS = {
    ((3, 1), (4, 1)): "open", ((3, 2), (4, 2)): "open", ((2, 2), (2, 3)): "open",
    ((5, 6), (5, 7)): "open", ((11, 6), (11, 7)): "open", ((2, 10), (2, 11)): "open",
    ((11, 5), (10, 5)): "open", ((6, 5), (5, 5)): "open", ((5, 5), (4, 5)): "open", ((3, 5), (2, 5)): "open",
    ((8, 8), (9, 8)): "open", ((9, 8), (10, 8)): BREACH,
    ((5, 8), (4, 8)): "open", ((4, 10), (4, 11)): BREACH,
    ((8, 9), (8, 10)): "open", ((6, 9), (6, 10)): "open",
    ((10, 10), (9, 10)): "open", ((5, 11), (4, 11)): "open",
    ((7, 10), (7, 11)): "open", ((7, 12), (7, 13)): "open",
    ((12, 11), (12, 10)): "open", ((5, 13), (4, 13)): "open",
    ((11, 14), (11, 13)): "open", ((9, 14), (9, 13)): "open",
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
    nw = place_container_walls(b, plan, WALL, WALL_IN, R, y=y0, boxes=("mod_container_20", "mod_container_20_damaged"))
    n_walls = sum(nw.values())
    # ground: the sites, mid and the spawns are paved aprons; gravel tracks along the streets; sand drifts elsewhere
    for ch, kind in (("A", "rock"), ("B", "rock"), ("M", "rock"), ("T", "rock"), ("D", "rock"), ("s", "rock"), ("t", "rock"), ("p", "rock"), ("f", "rock"), ("R", "rock"), ("c", "rock")):
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
    # signs at every site entrance; sodium street lights against the walls; the sheds lit by caged lamps (INTERIORS above)
    n_sign = site_signs(b, plan, "A", "a", y0) + site_signs(b, plan, "B", "b", y0)
    n_lamp = wall_lamps(b, plan, "AB", "sd_sodium_light", y0, every=3) + wall_lamps(b, plan, "MstpfRcTD", "sd_sodium_light", y0, every=5)
    # ---------------------------------------------------------------- scenario: sites, spawns, timings
    sc = ScenarioBuilder(b, defending="Axis")
    xA, zA = plan.cc(*PLANT_A_CELL)
    xB, zB = plan.cc(*PLANT_B_CELL)
    plantA, plantB = (xA + PLANT_A_OFF[0], zA + PLANT_A_OFF[1]), (xB + PLANT_B_OFF[0], zB + PLANT_B_OFF[1])
    aC, bC = plan.centre("A"), plan.centre("B")
    sc.objective("A Loading Dock", plantA[0], plantA[1], radius=1.75, capture_time=0, attack_from=plan.cc(2, 8), y_min=-1.0, y_max=1.5,
                 slots=[{"pos": [aC[0] + 6.0, y0, aC[1] - 1.0], "yaw": 225.0, "kind": "position", "priority": 1},
                        {"pos": [aC[0] - 8.0, y0, aC[1] + 1.0], "yaw": 135.0, "kind": "position", "priority": 2}],
                 routes=[("main", [plan.cc(2, 3), plan.cc(2, 10)]), ("right", [plan.cc(5, 3), plan.cc(5, 7), plan.cc(4, 8), plan.cc(4, 10)])])
    sc.objective("B Crane Yard", plantB[0], plantB[1], radius=1.75, capture_time=0, attack_from=plan.cc(11, 5), y_min=-1.0, y_max=1.5,
                 slots=[{"pos": [bC[0] - 6.0, y0, bC[1] - 1.0], "yaw": 135.0, "kind": "position", "priority": 1},
                        {"pos": [bC[0] + 8.0, y0, bC[1] + 1.0], "yaw": 225.0, "kind": "position", "priority": 2}],
                 routes=[("main", [plan.cc(9, 1), plan.cc(11, 6)]), ("left", [plan.cc(5, 3), plan.cc(5, 7), plan.cc(8, 8), plan.cc(9, 8)])])
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
            ("A Loading Dock", aC, 12), ("B Crane Yard", bC, 12), ("Container Stacks", plan.centre("M"), 12), ("Dock Road", plan.cc(2, 6), 12),
            ("Fence Road", plan.cc(11, 3), 10), ("Reefer Shed", plan.centre("w"), 5), ("Pump House", plan.centre("n"), 7), ("Rail Passage", plan.centre("c"), 10),
            ("Quay Road", plan.centre("R"), 14), ("Back Lane", plan.cc(8, 5), 10), ("Raider Spawn", tC, 10), ("Security Spawn", dC, 10))],
        "bounds": [[round(plan.x0, 1), round(plan.z0, 1)], [round(-plan.x0, 1), round(plan.z0, 1)], [round(-plan.x0, 1), round(-plan.z0, 1)], [round(plan.x0, 1), round(-plan.z0, 1)]],
        "timingPoints": {"contest": [P(5, 7), P(2, 8), P(11, 6)]},
        "timingRoutes": [
            {"name": "quay road", "kind": "rotation", "points": [P(12, 10), P(12, 11), P(5, 13), P(4, 13)]},
            {"name": "rail passage", "kind": "mid", "points": [P(10, 10), P(9, 10), P(5, 10), P(5, 11), P(4, 11)]},
            {"name": "back lane", "kind": "fallback", "points": [P(2, 11), P(2, 5), P(11, 5), P(11, 7)]},
            {"name": "mid to A via pump house", "kind": "attackMid", "points": [P(2, 2), P(4, 2), P(5, 7), P(4, 8), P(4, 11), [round(plantA[0], 2), round(y0, 2), round(plantA[1], 2)]]},
            {"name": "mid to B via reefer shed", "kind": "attackMid", "points": [P(2, 2), P(4, 2), P(5, 7), P(9, 8), P(10, 8), [round(plantB[0], 2), round(y0, 2), round(plantB[1], 2)]]},
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
    for name, (i, j), (ti, tj) in (("a_site", (2, 10), (2, 12)), ("b_site", (11, 6), (12, 8)), ("mid", (5, 6), (6, 8)), ("t_spawn", (2, 1), (2, 3)), ("d_spawn", (10, 15), (10, 13)), ("reefer", (8, 8), (10, 8)), ("rail", (9, 10), (5, 10))):
        x, z = plan.cc(i, j); tx, tz = plan.cc(ti, tj)
        views.append({"name": name + "_eye", "pos": [round(x, 2), round(y0 + eye, 2), round(z, 2)], "yaw": look_yaw(tx - x, tz - z), "pitch": 0.0, "gameplay": True})
    write_map(os.path.join(tdir, TID + ".nmap.json"), name=NAME, template_id=TID, template_version=VERSION, seed=SEED, width=W, depth=D,
              builder=b, scenario_json=sc.to_json(rules_extra=rules), explore_start=((a1[0], a1[1], a1[2]), 0.0), camera=cam, bookmarks={},
              extra={"groundPaint": paint.to_json(), "lighting": {"hour": 22.5, "fill": 1.0}, "look": {"haze": 0.9, "saturation": 0.95, "tint": [0.94, 0.98, 1.06], "bloom": 1.3, "paintSets": {"rock": "yard_asphalt_paint", "gravel": "gravel_mixed"}}})
    write_template(os.path.join(tdir, TID + ".json"), tid=TID, name=NAME, order=20, version=VERSION, seed=SEED,
                   description="Search and Destroy: a desert compound of precast concrete walls and courtyards in hard noon light. "
                               "A: the west vehicle court; B: the east supply yard; Mid: the parade square. 5 v 5.",
                   width=W, depth=D, margin=MARGIN, heightmap=grid.meta(TID + ".height.png"), water_level=-1000.0,
                   map_file=TID + ".nmap.json", camera=cam, thumbnail=TID + ".thumb.png", thumb_camera=thumb_cam,
                   extra={"userMap": True, "views": views})
    rep = {"instances": len(b.inst), "walls": nw, "masses": n_mass, "cover": n_cover, "plan": [plan.nx, plan.nz], "errors": err}
    write_summary(os.path.join(ctx.out, TID + ".summary.json"), rep)
    print("instances %d | walls %d, masses %d, cover %d | plan %d x %d cells of %.0f m" % (len(b.inst), n_walls, n_mass, n_cover, plan.nx, plan.nz, CELL))
