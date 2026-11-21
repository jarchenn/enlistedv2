"""SD2 map 3: Sun Fort, a Search and Destroy map: a desert military compound of precast concrete walls, courtyards and
huts in hard noon light. Laid out on the 6 m bay of the compound wall (sd_common.Plan). Raiders spawn at the south
gate, Security in the north yard. A: the west vehicle court; B: the east supply yard; Mid: the parade square.

    <python> -I tools/mapgen/build.py sd_desert"""
import math
import os

from mapgen import (GroundPaint, Grid, Frame, MapBuilder, ScenarioBuilder, write_map, write_template, write_summary, asset_usage,
                    rng, look_yaw)
from maps.sd_common import inside_eye, Plan, place_walls2, spread, place_masses, place_cover, wall_spot, roof_clutter, place_along_walls, free_spot, door_segs, barrier_line, place_in_space, door_points, _seg_dist
from mapgen import desert_flora as DF
import random as _random

TID, NAME, VERSION, SEED = "sd_desert", "Sun Fort", 1, 2611
CELL = 6.0
WALL = "sd_compound_wall_warm"           # perimeter (razor wire), sun-bleached tan
WALL_IN = ("sd_mud_wall_a", "sd_mud_wall_b", "sd_mud_wall_c")   # inner yard walls: ochre-rendered mud brick
BREACH = "sd_mud_wall_door"   # the connectors' doorways
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
MASSES = [("sd_warm_nt_barracks", 30, 12), ("sd_warm_nt_motor_pool", 24, 14), ("sd_warm_nt_hq", 20, 14), ("sd_warm_nt_workshop", 18, 12),
          ("sd_warm_nt_armory", 16, 12), ("sd_warm_nt_pump_station", 14, 10), ("sd_warm_mil_hut", 11.8, 19.6), ("sd_warm_nt_site_office", 7, 3)]
# site identity: A, the vehicle court (wrecks under camo nets); B, the supply yard (pallets, boxes, a damaged container)
INTERIORS = "wnkc"   # Workshop, Maintenance, the defender access, the Arcade (+ the backstreet shed below)
A_COVER = [("mod_guard_tower", 1.7, 3.3, 1),
           ("mod_jersey_barrier", 1.6, 0.4, 3), ("ph_concrete_road_barrier", 1.0, 0.35, 2), ("mod_sandbag_position", 1.4, 1.1, 2),
           ("furn_mil_crates", 0.8, 0.6, 2), ("mod_crate_wood", 0.6, 0.6, 2), ("mod_pallet_stack", 1.0, 0.45, 1), ("mod_barrel_group", 0.7, 0.7, 2),
           ("mod_mil_generator", 1.2, 0.8, 1), ("mod_tyre_pile", 1.3, 1.0, 1), ("ph_propane_tank", 0.35, 0.35, 2), ("ph_metal_jerrycan", 0.2, 0.1, 3)]
B_COVER = [("mod_container_20_damaged", 1.3, 3.3, 1), ("mod_pallet_stack", 1.0, 0.45, 3), ("furn_mil_crates", 0.8, 0.6, 2), ("mod_crate_plastic_stack", 0.7, 0.6, 2),
           ("ph_wooden_military_crate", 0.6, 0.4, 3), ("mod_jersey_barrier", 1.6, 0.4, 2), ("mod_barrel_group", 0.7, 0.7, 1), ("ph_old_military_compressor", 0.6, 0.4, 1),
           ("mod_generator_set", 1.65, 0.65, 1), ("ph_hand_truck", 0.3, 0.3, 1), ("mod_sandbag_position", 1.4, 1.1, 1), ("ph_cardboard_box_01", 0.25, 0.25, 3)]
SITE_COVER = A_COVER
WALL_CLUTTER = [("mod_pallet_stack", 1.0, 0.45, 1), ("furn_mil_crates", 0.8, 0.6, 1), ("mod_barrel_group", 0.7, 0.7, 1), ("mod_sandbag_position", 1.4, 1.1, 1),
                ("ph_wooden_military_crate", 0.6, 0.4, 2), ("nat_brush_scrub", 0.8, 0.8, 1), ("mod_tyre_pile", 1.3, 1.0, 1)]
VEHICLES = ("mod_apc_wheeled", "mod_mil_utility_green", "mod_pcar_offroad", "mod_container_20_damaged")
SCRUB = [("nat_brush_scrub", 0.8, 0.8, 1), ("ph_dry_branches_medium_01_a", 0.8, 0.8, 1), ("ph_shrub_03_b", 0.5, 0.5, 1), ("mod_rock_small", 0.4, 0.4, 1)]
STREET_COVER = [("ph_wooden_crate_01", 0.5, 0.5, 1), ("ph_barrel_02", 0.35, 0.35, 2), ("ph_old_tyre", 0.4, 0.4, 1), ("mod_jersey_barrier", 1.6, 0.4, 1)] + SCRUB
COVER = {"A": A_COVER, "B": B_COVER, "M": [("mod_jersey_barrier", 1.6, 0.4, 2), ("mod_sandbag_position", 1.4, 1.1, 1), ("furn_mil_crates", 0.8, 0.6, 2),
                                           ("ph_barrel_03", 0.35, 0.35, 2), ("ph_concrete_road_barrier_02", 1.0, 0.35, 1)],
         "s": STREET_COVER, "t": STREET_COVER, "f": STREET_COVER[:2] + SCRUB, "R": STREET_COVER[:2] + SCRUB[:2], "p": SCRUB[:2],
         "D": [("ph_plastic_crate_01", 0.4, 0.3, 2), ("ph_portable_generator", 0.5, 0.35, 1), ("mod_jersey_barrier", 1.6, 0.4, 1)],
         "T": [("ph_plastic_crate_02", 0.4, 0.3, 2), ("ph_metal_toolbox", 0.3, 0.15, 1), ("mod_jersey_barrier", 1.6, 0.4, 1)], "c": [("ph_barrel_01", 0.35, 0.35, 1)]}
PLANT_A_CELL, PLANT_A_OFF = (2, 10), (0.5, -3.0)   # plant pads: an original cell and an offset (m)
PLANT_B_CELL, PLANT_B_OFF = (12, 11), (-0.5, -7.5)
VIEW_CELLS = (("a_site", (3, 9), (2, 11)), ("b_site", (11, 9), (12, 11)), ("mid", (7, 7), (7, 9)), ("t_spawn", (7, 1), (7, 3)), ("d_spawn", (7, 15), (7, 13)),
              ("workshop", (5, 8), (4, 9)), ("a_main", (2, 8), (2, 11)))
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


def flora(b, plan, grid, keep):
    """DF1 / DF1b (mapgen/desert_flora.py): no new cover on the lanes. Inside the compound, plants of 0.6 m or less
    (DF.LOW and the grass tufts) at plain-wall feet and in wall corners, one or two agave / yucca at each site's edge, and
    plants up to 0.75 m (DF.MID) only in the dead gap between a tall blocker (vehicle, container, stack) and a wall or
    another blocker. Nothing within 1.5 m of a door line, 3 m of a doorway (5 m at the spawn exits) or 1 m past a plant
    pad / audit eye. Outside the compound (out of bounds) larger patches and the desert shrubs."""
    R = _random.Random(7302)
    repo = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
    insts = [(e["asset"], e["pos"][0], e["pos"][2], e["yaw"]) for e in b.inst]
    obst = DF.obstacles(repo, insts)
    regs = sorted({c for r in plan.rows for c in r} - set("# "))
    doors = [(x, z, 5.0 if ch in "TD" else 3.0) for ch in regs for x, z in door_points(plan, ch)]   # spawn exits get more room
    segs = [s for ch in regs for s in door_segs(plan, ch)]
    free = lambda x, z: not any(DF.inside(o, x, z, 0.05) for o in obst)
    cell = lambda x, z: (int((x - plan.x0) // plan.cell), int((z - plan.z0) // plan.cell))

    def ok_in(x, z):
        if not plan.walk(*cell(x, z)):
            return False
        if any(math.hypot(x - dx, z - dz) < r for dx, dz, r in doors) or any(_seg_dist((x, z), s0, s1) < 1.5 for s0, s1 in segs):
            return False
        return not any(math.hypot(x - kx, z - kz) < kr + 1.0 for kx, kz, kr in keep) and free(x, z)

    def walled(x0, z0, x, z):
        """(x, z), seen from (x0, z0), is behind a wall or inside a blocker: the gap between them is closed"""
        a, c = cell(x0, z0), cell(x, z)
        if not plan.walk(*c) or any(DF.inside(o, x, z, 0.0) for o in obst):
            return True
        return a != c and plan.at(*a) != plan.at(*c) and plan.links.get(frozenset((a, c))) != "open"
    P = DF.Placer(R, ok_in, gap=1.2)
    # 1. each site's edge: one or two agave / yucca (0.44 / 0.6 m) in its wall corners
    corners = []
    for j in range(plan.nz):
        for i in range(plan.nx):
            ch = plan.at(i, j)
            if not plan.walk(i, j):
                continue
            x, z = plan._cc(i, j)
            for si in (-1, 1):
                for sj in (-1, 1):
                    if all(plan.at(*n) != ch and plan.links.get(frozenset(((i, j), n))) != "open" for n in ((i + si, j), (i, j + sj))):
                        corners.append((ch, x + si * (plan.cell / 2 - 0.85), z + sj * (plan.cell / 2 - 0.85)))
    R.shuffle(corners)
    for site in "AB":
        n = 0
        for ch, x, z in corners:
            if ch == site and n < 2 and P.add(["fl_desertshrub_agave_2", "fl_desertshrub_yucca_2"], x, z):
                n += 1
    # 2. wall corners: a low plant
    for ch, x, z in corners:
        P.add(DF.LOW, x, z, 0.8)
    # 3. plain-wall feet (every walkable region, the spawn yards too): low shrubs and tufts
    for x, z, yaw, kind, a, c in plan.edges():
        if kind:
            continue
        for cl in (a, c):
            if not plan.walk(*cl):
                continue
            ix, iz = plan._cc(*cl)
            nx, nz = (math.copysign(1.0, ix - x), 0.0) if yaw == 90.0 else (0.0, math.copysign(1.0, iz - z))
            for _ in range(2):
                t = R.uniform(-plan.cell / 2 + 0.9, plan.cell / 2 - 0.9)
                P.add(DF.LOW if R.random() < 0.55 else DF.TUFT, x + nx * 0.8 - nz * t, z + nz * 0.8 + nx * t, 0.5)
    # 4. dead ground: the gap between a tall blocker and the wall / blocker behind it
    tall = DF.WALLISH + ("apc", "utility", "pcar", "container", "generator", "pallet", "crate", "sandbag", "tyre", "tower", "supply", "scrap", "deadwood", "compressor")
    for aid, ox, oz, yaw, w, d in obst:
        if not any(s in aid for s in tall) or aid.startswith(("sd_compound", "sd_mud")):
            continue
        big = w * d > 2.0
        ax, az = DF.axes(yaw)
        for (ux, uz), half in ((ax, w / 2), ((-ax[0], -ax[1]), w / 2), (az, d / 2), ((-az[0], -az[1]), d / 2)):
            px, pz = ox + ux * (half + 0.45), oz + uz * (half + 0.45)
            if walled(px, pz, px + ux * 1.0, pz + uz * 1.0):
                P.add(DF.MID if big else DF.LOW, px, pz, 0.8)
    for aid, x, z, yaw in insts:   # a tuft beside the scrub and small rocks already there
        if aid in ("nat_brush_scrub", "ph_dry_branches_medium_01_a", "sd_dry_rock_small", "rk_rock_s_a"):
            a = R.uniform(0, 2 * math.pi)
            P.add(DF.TUFT, x + math.cos(a) * 0.9, z + math.sin(a) * 0.9, 0.7)
    inner = P.out
    for aid, x, z, yaw in inner:   # self-check: the S&D cover rule
        assert DF.HEIGHT[aid] <= 0.6 or aid in DF.MID, aid
        assert all(_seg_dist((x, z), s0, s1) >= 1.5 for s0, s1 in segs), (aid, x, z)
    X0, Z0 = plan.x0 - 2.0, plan.z0 - 2.0

    def ok_out(x, z):
        return not (X0 < x < -X0 and Z0 < z < -Z0) and abs(x) < grid.width / 2 - 1.0 and abs(z) < grid.depth / 2 - 1.0 and free(x, z) and grid.slope_deg(x, z) < 28.0
    Q = DF.Placer(R, ok_out, gap=1.6)
    Q.rings([o for o in obst if any(s in o[0] for s in DF.ROCKS)], DF.PATCH, 2, 0.7)
    Q.scatter(-grid.width / 2, -grid.depth / 2, grid.width / 2, grid.depth / 2, 70, DF.PATCH)
    Q.scatter(-grid.width / 2, -grid.depth / 2, grid.width / 2, grid.depth / 2, 24, DF.SHRUB)
    for aid, x, z, yaw in inner:
        b.place(aid, x, z, yaw, check=False, cls="none", tags={"flora"})
    for aid, x, z, yaw in Q.out:
        b.place(aid, x, z, yaw, check=False, cls="none", tags={"flora"})
    print("DF1b flora: %d inside (%s), %d outside" % (len(inner), ", ".join("%s %d" % (k, sum(1 for a in inner if a[0] in g)) for k, g in (("low", DF.LOW), ("mid", DF.MID), ("tuft", DF.TUFT))), len(Q.out)))
    return len(inner), len(Q.out)


def build(ctx):
    R = rng(int(os.environ.get("NMK_SD_SEED", SEED)))   # SD5 harness: another clutter seed (default: the map's)
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
    for ch, kind in (("A", "gravel"), ("B", "gravel"), ("M", "gravel"), ("T", "gravel"), ("D", "gravel")):
        for i, j in plan.cells(ch):
            x, z = plan._cc(i, j)
            h = CELL / 2
            paint.poly(kind, [(x - h, z - h), (x + h, z - h), (x + h, z + h), (x - h, z + h)], weight=0.9, edge=0.6, ragged=0.4, seed=i * 31 + j)
    for ch in "stfpRc":
        for i, j in plan.cells(ch):
            x, z = plan._cc(i, j)
            paint.poly("gravel", [(x - 1.4, z - 1.4), (x + 1.4, z - 1.4), (x + 1.4, z + 1.4), (x - 1.4, z + 1.4)], weight=0.55, edge=1.2, ragged=1.0, seed=i * 17 + j)

    # ---------------------------------------------------------------- dressing: masses behind the walls, a mid landmark, cover
    masses = []
    n_mass = place_masses(b, plan, MASSES, R, y0, out=masses)
    n_roofc = roof_clutter(b, ctx.repo, masses, ["mod_hvac_unit", "ph_exterior_aircon_unit", "mod_nt_storage_tank", "mod_antenna_mast", "ph_exterior_aircon_unit"], R, y0,
                          flat=("sd_warm_nt_hq", "sd_warm_nt_armory", "sd_warm_nt_barracks", "sd_warm_nt_motor_pool", "sd_warm_nt_workshop", "sd_warm_nt_pump_station", "sd_warm_nt_site_office"))
    mx, mz = plan.centre("M")
    b.place("mod_nt_radar_tower", mx, mz + 1.0, 0.0, check=False, cls="none", y=y0, tags={"landmark", "cover"})   # the parade square's landmark: splits mid
    _xa, _za = plan.cc(*PLANT_A_CELL); _xb, _zb = plan.cc(*PLANT_B_CELL)
    PADS = [(_xa + PLANT_A_OFF[0], _za + PLANT_A_OFF[1], 3.5), (_xb + PLANT_B_OFF[0], _zb + PLANT_B_OFF[1], 3.5)]
    PADS += [(plan.cc(i, j)[0], plan.cc(i, j)[1], 1.6) for _n, (i, j), _t in VIEW_CELLS]   # the audit eyes stand clear of the dressing
    EYES = [("a_inside",) + inside_eye(plan, "A", PADS[0][:2])[:2] + (PADS[0][:2],), ("b_inside",) + inside_eye(plan, "B", PADS[1][:2])[:2] + (PADS[1][:2],),
            ("m_inside",) + inside_eye(plan, "M", plan.centre("M"), 8.0)[:2] + (plan.centre("M"),)]   # eye views from inside each site and mid
    PADS += [(ex, ez, 1.8) for _n, ex, ez, _t in EYES]
    n_cover = 0
    for ch, items in COVER.items():
        if ch in "MTD":   # mid and the spawn yards: their clutter lines the walls too
            extra = WALL_CLUTTER if ch != "D" else [it for it in WALL_CLUTTER if it[0] not in ("mod_sandbag_position", "mod_tyre_pile", "nat_brush_scrub")]   # the Security yard stays quick to leave
            n_cover += place_along_walls(b, plan, ch, items + extra, R, y0, keep=PADS)
            continue
        if ch in "AB":   # sites: vehicles stand free (narrower lanes), the rest lines the walls
            n_cover += place_cover(b, plan, ch, [it for it in items if it[0] in VEHICLES], R, y0, keep=PADS, lane=1.8)
            n_cover += place_along_walls(b, plan, ch, [it for it in items if it[0] not in VEHICLES], R, y0, keep=PADS)
        else:
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
    # the vehicle court's shade canopy (walk under), the backstreet shed by the mid approach
    ax, az = plan.centre("A")
    for i, j in plan.cells("f"):
        x, z = plan._cc(i, j)
        if abs(x - plan.cc(7, 7)[0]) < 13.0:   # the two bays each side of the junction
            b.place("sd_roof_bay", x, z, 0.0, check=False, cls="none", y=y0, tags={"roof", "interior"})
            b.place("tun_lamp", x + 0.8, z, 0.0, check=False, cls="none", y=y0 + 3.0, tags={"lamp", "interior"})
    # the playable middle of the sites and mid: a motor pool (A), clusters, a T-wall / barrier line, pallet stacks
    taken = []
    spot = free_spot(plan, "A", 3.6, 3.4, R, door_segs(plan, "A"), PADS, taken, lane=1.5)
    if spot:
        px, pz, pyaw = spot
        h = 3.6
        paint.poly("rock", [(px - h, pz - h), (px + h, pz - h), (px + h, pz + h), (px - h, pz + h)], weight=0.95, edge=0.4)   # the pool's concrete pad
        b.place("sd_shade_canopy", px, pz, pyaw, check=False, cls="none", y=y0, tags={"dress", "shade"})
        ux, uz = (1.0, 0.0) if pyaw == 0.0 else (0.0, 1.0)
        for k, (veh, ang) in enumerate((("sd_static_humvee", 8.0), ("sd_static_truck_cargo", -14.0))):
            t = (k - 0.5) * 3.0
            vx, vz = px + ux * t, pz + uz * t
            b.place(veh, vx, vz, pyaw + 90.0 + ang, check=False, cls="none", y=y0, tags={"cover", "vehicle"})
            paint.poly("mud", [(vx - 1.0, vz - 0.9), (vx + 0.8, vz - 1.1), (vx + 1.1, vz + 0.8), (vx - 0.9, vz + 1.0)], weight=0.7, edge=0.6, ragged=0.5, seed=k + 3)
        for k, (it, dx, dz) in enumerate((("furn_mil_crates", 3.4, 2.4), ("mod_barrel_group", -3.4, 2.4), ("ph_metal_jerrycan", 3.0, -2.6), ("ph_wooden_military_crate", -3.2, -2.4))):
            b.place(it, px + dx * (ux or 0.7) - dz * uz * 0.0 + (dz if uz else 0.0), pz + dz * (1.0 if ux else 0.0) + dx * uz, pyaw + 7.0 * k, check=False, cls="none", y=y0, tags={"cover"})
        taken.append((px, pz, 4.6))
    n_space = 0
    # the open middle of each site: a parked vehicle pair, a container with T-walls at its ends, the site's dump
    for ch, vehs, dump in (("A", ("sd_static_ural", "sd_static_boxer"), ("cl_desert_supply_dump", 3.7, 2.2)), ("B", ("sd_static_humvee", "sd_static_ural"), ("cl_desert_fuel_dump", 4.2, 2.8))):
        segs = door_segs(plan, ch)
        for k, veh in enumerate(vehs):   # one vehicle per spot (a pair needs more room than the door lines leave)
            spot = free_spot(plan, ch, 1.3, 3.8, R, segs, PADS, taken, lane=0.5)
            if spot:
                sx, sz, syaw = spot
                b.place(veh, sx, sz, syaw + (6.0 if k else -9.0), check=False, cls="none", y=y0, tags={"cover", "vehicle"})
                taken.append((sx, sz, 3.8))
                n_space += 1
        spot = free_spot(plan, ch, 4.0, 1.4, R, segs, PADS, taken, lane=0.5)
        if spot:
            sx, sz, syaw = spot
            ux, uz = (1.0, 0.0) if syaw == 0.0 else (0.0, 1.0)
            b.place("sd_dry_container_20_damaged", sx, sz, syaw + 90.0, check=False, cls="none", y=y0, tags={"cover"})
            for e in (-1, 1):
                b.place("mod_twall", sx + ux * e * 3.9, sz + uz * e * 3.9, syaw + R.uniform(-4, 4), check=False, cls="none", y=y0, tags={"cover"})
            taken.append((sx, sz, 4.6))
            n_space += 3
        n_space += place_in_space(b, plan, ch, [dump + (1,)], R, y0, PADS, taken, lane=0.5)
    for ch in "AB":
        n_space += barrier_line(b, plan, ch, "mod_twall", 3, 1.55, 1.25, R, y0, PADS, taken)
        n_space += barrier_line(b, plan, ch, "mod_jersey_barrier", 2, 3.2, 0.8, R, y0, PADS, taken)
        n_space += place_in_space(b, plan, ch, [("cl_desert_supply_stack", 1.45, 0.7, 1), ("cl_desert_fuel_point", 1.3, 0.8, 1), ("mod_pallet_stack", 1.0, 0.45, 2),
                                                 ("cl_desert_scrap_pile", 0.9, 0.7, 1)], R, y0, PADS, taken)
    n_space += barrier_line(b, plan, "M", "mod_jersey_barrier", 2, 3.2, 0.8, R, y0, PADS, taken)
    n_space += place_in_space(b, plan, "M", [("cl_desert_supply_stack", 1.45, 0.7, 1), ("cl_desert_rocks_deadwood", 3.0, 2.35, 1)], R, y0, PADS, taken)
    if os.environ.get("NMK_SD_BIG", "1") != "0":   # the large clusters against the site / mid walls, edge dressing inside the sites (on since SD5; NMK_SD_BIG=0 drops them)
        for ch, big in (("A", [("cl_desert_guard_point", 3.0, 2.6, 1)]), ("B", [("cl_desert_motor_pool", 3.6, 2.1, 1), ("cl_desert_guard_point", 3.0, 2.6, 1)]),
                        ("M", [("cl_desert_supply_dump", 3.6, 2.4, 1)])):
            n_space += place_along_walls(b, plan, ch, big, R, y0, keep=PADS)
        for ch in "AB":
            n_space += place_along_walls(b, plan, ch, [("ph_namaqualand_rocks_01", 0.8, 0.6, 2), ("ph_namaqualand_boulder_03", 0.7, 0.6, 2), ("mod_pallet_stack", 1.0, 0.45, 2), ("mod_barrel_group", 0.7, 0.7, 2)], R, y0, keep=PADS)
    # the radar tower's base: a chain-link compound (gate to the south) with a generator corner and cable drums
    tx, tz = mx, mz + 1.0
    for side in range(4):
        for k in (-1, 0, 1):
            if side == 0 and k == 0:
                b.place("mod_chainlink_gate_open", tx, tz - 4.8, 0.0, check=False, cls="none", y=y0, tags={"fence"})
                continue
            if side in (0, 2):
                b.place("sd_chainlink_panel", tx + k * 3.15, tz + (-4.8 if side == 0 else 4.8), 0.0, check=False, cls="none", y=y0, tags={"fence"})
            else:
                b.place("sd_chainlink_panel", tx + (4.8 if side == 1 else -4.8), tz + k * 3.15, 90.0, check=False, cls="none", y=y0, tags={"fence"})
    b.place("cl_desert_generator_corner", tx + 3.0, tz + 3.4, 180.0, check=False, cls="none", y=y0, tags={"dress"})
    b.place("tun_cable_reel", tx - 3.4, tz + 3.2, 20.0, check=False, cls="none", y=y0, tags={"dress"})
    # edges: quiver trees, dry plants and the batch K rocks against the walls of the streets and yards; small stones, litter
    EDGE = [("ph_quiver_tree_01", 0.6, 0.5, 1), ("ph_quiver_tree_02", 0.6, 0.5, 1), ("ph_othonna_cerarioides", 0.4, 0.4, 2), ("ph_searsia_lucida", 0.6, 0.6, 1),
            ("ph_namaqualand_rocks_01", 0.8, 0.6, 1), ("ph_namaqualand_stones_01", 0.6, 0.5, 2), ("ph_namaqualand_boulder_03", 0.7, 0.6, 1)]
    for ch in "stfR":   # not in the narrow mid approach (p) nor inside the sites: their gaps made vault links the bots fail
        n_space += place_along_walls(b, plan, ch, EDGE, R, y0, keep=PADS)
    for ch in "ABMstf":
        for i, j in plan.cells(ch):
            if R.random() < 0.5:
                x, z = plan._cc(i, j)
                x, z = x + R.uniform(-2.5, 2.5), z + R.uniform(-2.5, 2.5)
                paint.poly("pebbles", [(x - 1.2, z - 0.9), (x + 1.0, z - 1.1), (x + 1.2, z + 0.9), (x - 1.0, z + 1.1)], weight=0.5, edge=0.8, ragged=0.6, seed=i * 13 + j)
    # ground detail: sand drifted against the wall feet of the aprons, tyre tracks along the streets, oil stains
    for x, z, yaw, kind, a, c in plan.edges():
        for cell in (a, c):
            if plan.at(*cell) in "ABMTD":
                cx, cz = plan._cc(*cell)
                dx, dz = (cx - x, cz - z)
                n_ = max(1e-6, (dx * dx + dz * dz) ** 0.5)
                px, pz = x + dx / n_ * 0.9, z + dz / n_ * 0.9
                if yaw == 90.0:
                    poly = [(px - 0.9, pz - 3.0), (px + 0.9, pz - 3.0), (px + 0.9, pz + 3.0), (px - 0.9, pz + 3.0)]
                else:
                    poly = [(px - 3.0, pz - 0.9), (px + 3.0, pz - 0.9), (px + 3.0, pz + 0.9), (px - 3.0, pz + 0.9)]
                paint.poly("sand", poly, weight=0.75, edge=0.7, ragged=0.6, seed=int(px * 13 + pz * 7))
    for ch in "st":
        cs = sorted(plan.cells(ch), key=lambda q: (q[0], q[1]))
        pts = [plan._cc(*q) for q in cs]
        for off in (-0.85, 0.85):
            for k in range(len(pts) - 1):
                (x0, z0), (x1, z1) = pts[k], pts[k + 1]
                if abs(x0 - x1) + abs(z0 - z1) > plan.cell + 0.1:
                    continue
                nx, nz = (z1 - z0) / plan.cell, -(x1 - x0) / plan.cell
                paint.line("dirt", [(x0 + nx * off, z0 + nz * off), (x1 + nx * off, z1 + nz * off)], 0.42, weight=0.55, edge=0.15)
    for e in list(b.inst):
        if e["asset"] in ("mod_apc_wheeled", "mod_mil_utility_green", "mod_pcar_offroad", "mod_mil_generator", "mod_generator_set"):
            x, z = e["pos"][0], e["pos"][2]
            paint.poly("mud", [(x - 1.1, z - 0.8), (x + 0.9, z - 1.0), (x + 1.0, z + 0.9), (x - 0.8, z + 1.1)], weight=0.7, edge=0.6, ragged=0.5, seed=int(x * 3 + z))
    # signs: each site's letter on its walls facing the main entries, arrows at the street ends
    signs = []
    for ch, letter in (("A", "a"), ("B", "b")):
        cx, cz = plan.centre(ch)
        for near in ((cx, cz - 30.0), (cx, cz + 30.0), (cx + (25.0 if ch == "A" else -25.0), cz)):
            x, z, yaw = wall_spot(plan, ch, near)
            if all(abs(x - q[0]) + abs(z - q[1]) > 4.0 for q in signs):
                b.place("sd_stencil_%s" % letter, x, z, yaw, check=False, cls="none", y=y0, tags={"sign"})
                signs.append((x, z))
    for ch, letter, near in (("s", "a", plan.cc(2, 8)), ("t", "b", plan.cc(12, 8)), ("f", "a", plan.cc(3, 7)), ("f", "b", plan.cc(11, 7)), ("R", "a", plan.cc(4, 13)), ("R", "b", plan.cc(10, 13))):
        x, z, yaw = wall_spot(plan, ch, near)
        b.place("sd_stencil_%s" % letter, x, z, yaw, check=False, cls="none", y=y0, tags={"sign"})
        signs.append((x, z))

    for e in b.inst:   # the desert props: no weed / moss tufts (sd_desert_assets.dry)
        if e["asset"] in ("mod_container_20_damaged", "mod_jersey_barrier", "mod_pallet_stack", "mod_rock_small"):
            e["asset"] = "sd_dry_" + e["asset"][4:]
    flora(b, plan, grid, PADS)
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
    for name, ex, ez, (tx, tz) in EYES:
        views.append({"name": name, "pos": [round(ex, 2), round(y0 + eye, 2), round(ez, 2)], "yaw": look_yaw(tx - ex, tz - ez), "pitch": 0.0, "gameplay": True})
    for name, (i, j), (ti, tj) in VIEW_CELLS:
        x, z = plan.cc(i, j); tx, tz = plan.cc(ti, tj)
        views.append({"name": name + "_eye", "pos": [round(x, 2), round(y0 + eye, 2), round(z, 2)], "yaw": look_yaw(tx - x, tz - z), "pitch": 0.0, "gameplay": True})
    write_map(os.path.join(tdir, TID + ".nmap.json"), name=NAME, template_id=TID, template_version=VERSION, seed=SEED, width=W, depth=D,
              builder=b, scenario_json=sc.to_json(rules_extra=rules), explore_start=((a1[0], a1[1], a1[2]), 0.0), camera=cam, bookmarks={},
              extra={"groundPaint": paint.to_json(), "lighting": {"hour": 13.0, "enclosed": False}, "look": {"haze": 1.45, "weather": 0.0, "clouds": 0.04, "arid": True, "saturation": 1.12, "tint": [1.1, 1.0, 0.84], "bloom": 1.25, "paintSets": {"rock": "apron_concrete_paint", "gravel": "gravel_mixed", "dirt": "trail_dirt"}}})
    write_template(os.path.join(tdir, TID + ".json"), tid=TID, name=NAME, order=20, version=VERSION, seed=SEED,
                   description="Search and Destroy: a desert compound of precast concrete walls and courtyards in hard noon light. "
                               "A: the west vehicle court; B: the east supply yard; Mid: the parade square. 5 v 5.",
                   width=W, depth=D, margin=MARGIN, heightmap=grid.meta(TID + ".height.png"), water_level=-1000.0,
                   map_file=TID + ".nmap.json", camera=cam, thumbnail=TID + ".thumb.png", thumb_camera=thumb_cam,
                   extra={"userMap": True, "views": views})
    rep = {"space": n_space, "instances": len(b.inst), "walls": nw, "masses": n_mass, "cover": n_cover, "plan": [plan.nx, plan.nz], "errors": err}
    write_summary(os.path.join(ctx.out, TID + ".summary.json"), rep)
    print("instances %d | walls %d, masses %d, cover %d | plan %d x %d cells of %.0f m" % (len(b.inst), n_walls, n_mass, n_cover, plan.nx, plan.nz, CELL))
