"""SD2 map 3: Rue Haute, a Search and Destroy map: a Norman town quarter on an overcast morning. The lanes run between
house frontages (tools/mapgen/sd_town_facades.py, one 6 m bay per plan edge: the play space is the old walled plan's,
so the timings hold), roofed passages under the houses, the churchyard behind its wall (A), the market hall (B) and the
market square with its war memorial (mid). Raiders spawn in the station yard, Security in the gendarmerie yard.

    <python> -I tools/mapgen/build.py sd_town"""
import math
import os

from mapgen import (GroundPaint, Grid, Frame, MapBuilder, ScenarioBuilder, write_map, write_template, write_summary, asset_usage,
                    rng, look_yaw)
from mapgen.terrain import KCELL
from maps.sd_common import door_points, rect_ok, inside_eye, rows_from, Plan, place_facades, spread, place_masses, place_cover, wall_spot, site_signs, place_along_walls, place_in_space

TID, NAME, VERSION, SEED = "sd_town", "Rue Haute", 1, 2613
CELL = 6.0
WALL = "mod_nt_compound_wall"            # perimeter (razor wire)
WALL_IN = "mod_nt_compound_wall_plain"   # inner yard walls
BREACH = "sd_town_wall_door"   # the connector doorways
CULVERT = "mod_nt_compound_wall_culvert"
FACADES = ["sd_facade_render_a", "sd_facade_timber_a", "sd_facade_render_b", "sd_facade_stone", "sd_facade_render_c", "sd_facade_timber_b",
           "sd_facade_brick", "sd_facade_slate", "sd_facade_cafe", "sd_facade_timber_a", "sd_facade_slate", "sd_facade_render_a"]

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


TILT = (0.6, 0.5)   # VOT T3/T2: the gentle fall west -> east and south -> north under the terraces (m)
# VOT T2: the town stands on three street levels (m above the station yard): the lanes south of the river lane, the
# market terraces (B, the square, the arcade) and the upper town (the back lanes, the gendarmerie, the churchyard).
# Every open link stays level-continuous through a ramp in a lane cell; level changes between regions that do not link
# fall under the wall piece already on that edge. The churchyard's north strip by the church stands one storey higher
# behind a stone retaining wall with a flight of steps (drs_retaining_wall, drs_town_steps).
L1, L2A, L2, L3 = 1.3, 2.0, 2.6, 3.9
A_STEP_Z = 45.0   # the churchyard terrace edge (the row 17 / 18 boundary)
B_STEP_Z = -1.5   # VOT: Rue du Moulin climbs into B by a flight of steps (it was a 12 m ramp); the top edge of the flight


def _lin(v, a, b, ha, hb):
    import numpy as np
    return ha + (hb - ha) * np.clip((v - a) / (b - a), 0.0, 1.0)


def town_level(plan, X, Z):
    """The street level (m) over the plan's open cells (NaN on solid ground and off the plan)."""
    import numpy as np
    I = np.floor((X - plan.x0) / plan.cell).astype(int)
    J = np.floor((Z - plan.z0) / plan.cell).astype(int)
    R = np.full(X.shape, "#", dtype="<U1")
    ok = (I >= 0) & (J >= 0) & (I < plan.nx) & (J < plan.nz)
    for j in range(plan.nz):
        for i in range(plan.nx):
            ch = plan.at(i, j)
            if ch not in "# ":
                R[ok & (I == i) & (J == j)] = ch
    L = np.full(X.shape, np.nan)
    flat = {"T": 0.0, "B": L1, "M": L1, "w": L1, "D": L2}
    for ch, v in flat.items():
        L[R == ch] = v
    m = R == "t"   # up into B by two flights of steps (the face behind their top step) with a cart ramp along the east 1.8 m
    ramp_x = plan.x0 + 3.5 * plan.cell + 1.25
    L[m] = np.where(X[m] > ramp_x, _lin(Z[m], -9.0, 3.0, 0.0, L1), _lin(Z[m], B_STEP_Z - 0.4, B_STEP_Z, 0.0, L1))
    m = R == "p"; L[m] = _lin(Z[m], -9.0, 3.0, 0.0, L1)        # up into the square
    m = R == "s"; L[m] = _lin(Z[m], -3.0, 27.0, 0.0, L2A)      # Rue de l'Eglise climbs to A
    m = R == "f"   # the river lane meets the t / p ramps level (no step at the junctions), then slopes east to s
    xp, xs = plan.x0 + 10 * plan.cell, plan.x0 + 12 * plan.cell
    up, us = _lin(Z[m], -9.0, 3.0, 0.0, L1), _lin(Z[m], -3.0, 27.0, 0.0, L2A)
    L[m] = up + (us - up) * np.clip((X[m] - xp) / (xs - xp), 0, 1)
    m = R == "n"; L[m] = _lin(Z[m], 15.0, 27.0, L1, L2A)       # the cafe passage up to A's door
    m = R == "k"; L[m] = _lin(Z[m], 27.0, 39.0, L1, L2)        # defender access down to the square
    m = R == "R"; L[m] = np.where(Z[m] < 39.0, _lin(Z[m], 27.0, 39.0, L1, L2), L2)
    m = R == "c"   # the arcade at L1; its east corner climbs to A's south strip (the c - A link at x = 15)
    cx = np.clip((X[m] - 9.0) / 6.0, 0, 1); cz = np.clip((Z[m] - 21.0) / 6.0, 0, 1)
    corner = (X[m] >= 9.0) & (Z[m] < 27.0)
    L[m] = np.where(Z[m] >= 27.0, L2A, np.where(corner, L1 + (L2A - L1) * cx * cz / np.maximum(cx * cz + (1 - cx) * (1 - cz), 1e-6), L1))
    m = R == "A"
    za = Z[m]
    L[m] = np.where(za < A_STEP_Z - 0.6, _lin(za, 33.0, 39.0, L2A, L2), _lin(za, A_STEP_Z - 0.6, A_STEP_Z - 0.25, L2, L3))   # the face inside the wall's 0.8 m
    return L


def _fill(L, iters=400):
    """Solid ground takes the level of the nearest street: repeated 4-neighbour averaging into the unknown samples."""
    import numpy as np
    L = L.copy()
    known = ~np.isnan(L)
    for _ in range(iters):
        if known.all():
            break
        v = np.where(known, L, 0.0); k = known.astype(float)
        s = np.zeros_like(v); n = np.zeros_like(v)
        for sl_a, sl_b in (((slice(1, None), slice(None)), (slice(None, -1), slice(None))), ((slice(None, -1), slice(None)), (slice(1, None), slice(None))),
                           ((slice(None), slice(1, None)), (slice(None), slice(None, -1))), ((slice(None), slice(None, -1)), (slice(None), slice(1, None)))):
            s[sl_a] += v[sl_b]; n[sl_a] += k[sl_b]
        new = ~known & (n > 0)
        L[new] = s[new] / n[new]
        known |= new
    L[np.isnan(L)] = 0.0
    return L


def town_relief(grid, plan):
    """VOT T3/T2: the terraced town (town_level) on a gentle plane, its solid blocks at the level of their nearest
    street; beyond the build area the levels fade out and the ground rises into eroded hills tall enough to stand
    over the roofs from the long lanes (north and west up to ~28 m). Returns (stats, fine(grid) -> FineLayer): the
    base grid holds what 3.1 m cells can; call fine() after grid.quantise() for the sub-cell residual (the terrace
    wall's step, the ramp ends) as sculpt tiles."""
    import numpy as np
    from mapgen.relief import relief_layers, hydraulic_erosion, thermal_erosion, drainage, smooth, _arr, _coords, FineLayer
    hx, hz = -plan.x0, -plan.z0
    fs = KCELL / 7.0
    fx = grid.org_x + np.arange((grid.nx - 1) * 7 + 1) * fs
    fz = grid.org_z + np.arange((grid.nz - 1) * 7 + 1) * fs
    FX, FZ = np.meshgrid(fx, fz)
    lev = _fill(town_level(plan, FX, FZ))
    out_f = np.maximum(np.abs(FX) - grid.width / 2, np.abs(FZ) - grid.depth / 2)
    lev *= 1.0 - np.clip(out_f / 40.0, 0, 1) ** 2   # the levels fade out over 40 m past the build area
    exact = 10.0 + TILT[0] * (FX / hx) * 0.5 + TILT[1] * (FZ / hz) * 0.5 + lev
    H = _arr(grid)
    H[...] = exact[::7, ::7]
    xs, zs = _coords(grid)
    X, Z = np.meshgrid(xs, zs)
    out = np.maximum(np.abs(X) - grid.width / 2, np.abs(Z) - grid.depth / 2)   # metres outside the build area (the plan + the 30 m ring of town houses)
    t = np.clip((out - 1.0) / 35.0, 0, 1)
    w = t * t * (3 - 2 * t)
    ang = np.arctan2(Z, -X)   # north-west hills the tallest: they close the views up Rue de l'Eglise and along the arcade
    tall = 34.0 + 16.0 * np.clip(np.cos(ang - np.radians(135.0)), 0, 1)   # VOT T2: tall enough to clear the roofs down the long lanes
    H += w * tall * np.clip((out - 1.0) / 55.0, 0, 1) ** 1.1
    relief_layers(grid, [(5.0, 180.0, 41, 3), (1.6, 60.0, 42, 4), (0.4, 20.0, 43, 3)], w)
    hydraulic_erosion(grid, w, iters=45, kc=1.0)
    thermal_erosion(grid, w, iters=15, talus=0.6)
    smooth(grid, w, passes=3)
    drainage(grid, w, depth=0.5, start=90, full=1200, width=2)
    smooth(grid, w, passes=1)
    inb = (np.abs(FX) <= grid.width / 2) & (np.abs(FZ) <= grid.depth / 2)

    def fine(grid):
        f = FineLayer(grid)
        ix = np.where(inb.any(axis=0))[0]; iz = np.where(inb.any(axis=1))[0]
        for tx, tz in f.tiles_in((fx[ix[0]], fz[iz[0]], fx[ix[-1]], fz[iz[-1]])):
            sl = (slice(tz * 35, tz * 35 + 36), slice(tx * 35, tx * 35 + 36))
            X_, Z_ = FX[sl], FZ[sl]
            D = np.where(inb[sl], exact[sl] - f.base(X_, Z_), 0.0)
            if np.abs(D).max() >= 0.001:
                f.d[tz * f.tx_n + tx] = D
        return f
    return {"tilt": TILT, "levels": [0.0, L1, L2A, L2, L3], "hillMaxM": round(float((w * tall).max()), 1)}, fine


def seat_on_relief(b, y0):
    """VOT T3: the layout code places on the flat datum y0 with explicit heights; from here on every explicit height
    is moved by the ground under the piece: the lowest of its footprint samples (on the plane that is a corner, so no
    edge floats). A piece resting on another (support) moves with its host; companions are already relative to theirs.
    Wrapping place() (not a pass afterwards) keeps the pads the engine-style buildings level at their real height."""
    orig = b.place

    def place(aid, x, z, yaw=0.0, **kw):
        if kw.get("y") is not None and kw.get("companions", True):
            sup = kw.get("support")
            if sup is not None:
                d = sup.get("_dz", 0.0)
            else:   # padded buildings (the engine levels a pad) at their mean ground: the cut and the fill balance
                a = b.cat[aid]
                hs = [s[2] for s in b._samples(a, x, z, yaw, inset=0.0)]
                d = (sum(hs) / len(hs) if b.padded(a) else min(hs)) - y0
            kw["y"] = kw["y"] + d
            e = orig(aid, x, z, yaw, **kw)
            if e is not None:
                e["_dz"] = d
            return e
        return orig(aid, x, z, yaw, **kw)
    b.place = place


def reseat_after_pads(b, y0):
    """VOT T3: the building pads placed later moved the ground under walls and props placed before them: re-seat every
    explicit-height piece that is not itself padded on the finished ground (lowest footprint sample) and carry what
    rests on it along. Returns the largest move (m)."""
    delta, worst = {}, 0.0
    for e in b.inst:
        a = b.cat[e["asset"]]
        if "_dz" not in e or "support" in e["extra"] or b.padded(a):
            continue
        d = min(s[2] for s in b._samples(a, e["pos"][0], e["pos"][2], e["yaw"], inset=0.0)) - y0
        dd = d - e["_dz"]
        if abs(dd) > 0.003:
            e["pos"][1] += dd
            e["_dz"] = d
            delta[e["id"]] = dd
            worst = max(worst, abs(dd))
    for _ in range(3):
        for e in b.inst:
            hid = e["extra"].get("support")
            if hid in delta and e["id"] not in delta:
                e["pos"][1] += delta[hid]
                delta[e["id"]] = delta[hid]
    return worst


def build(ctx):
    R = rng(SEED)
    plan = Plan(ROWS, CELL, LINKS, stretch_cols=STRETCH_COLS, stretch_rows=STRETCH_ROWS)
    bad = plan.check_links()
    assert not bad, bad
    W = D = plan.nx * CELL + 2 * 30.0
    MARGIN = 60
    frame = Frame(W, D)
    grid = Grid(W, D, margin=MARGIN, base=10.0)
    t3, fine_fn = town_relief(grid, plan)   # VOT T3 / T2
    grid.quantise()
    b = MapBuilder(ctx.catalog, grid, frame, seed=SEED)
    fine = fine_fn(grid)   # VOT T2: the sub-cell terrace edges as sculpt tiles; every later ground() sees them
    fine.install(b)
    paint = GroundPaint()
    paint.poly("sand", [(-W / 2 - MARGIN, -D / 2 - MARGIN), (W / 2 + MARGIN, -D / 2 - MARGIN), (W / 2 + MARGIN, D / 2 + MARGIN), (-W / 2 - MARGIN, D / 2 + MARGIN)], weight=1.0, edge=0.5)
    E = 30.0 - 2.0   # VOT: the pasture comes in over the 30 m ring of town houses to the street plan's edge (it was sand there)
    for q in ((-W / 2 - MARGIN, -D / 2 - MARGIN, -W / 2 + E, D / 2 + MARGIN), (W / 2 - E, -D / 2 - MARGIN, W / 2 + MARGIN, D / 2 + MARGIN),
              (-W / 2 - MARGIN, -D / 2 - MARGIN, W / 2 + MARGIN, -D / 2 + E), (-W / 2 - MARGIN, D / 2 - E, W / 2 + MARGIN, D / 2 + MARGIN)):
        paint.poly("grass", [(q[0], q[1]), (q[2], q[1]), (q[2], q[3]), (q[0], q[3])], weight=1.0, edge=4.0, ragged=0.6, seed=7)   # VOT T2: the hills are pasture, not sand
    y0 = b.ground(0.0, 0.0)
    seat_on_relief(b, y0)   # VOT T3: explicit heights below are on the datum; they land on the tilted town
    # the lanes are the gaps between house fronts; the churchyard keeps its wall; walls only between two open regions
    houses = []
    nw = place_facades(b, plan, FACADES, "sd_town_wall", y=y0, plain="A", plain_asset="sd_town_wall", out=houses)
    n_walls = sum(nw.values())
    # VOT T2: the churchyard's north strip, one storey up behind a stone retaining wall; a flight of steps in the middle
    wz = A_STEP_Z - 0.32
    for x in (20.0, 24.0, 31.15, 35.15, 40.0):
        b.place("drs_retaining_wall", x, wz, 0.0, check=False, cls="none", y=y0, tags={"wall"})
    b.place("drs_town_steps", 27.6, A_STEP_Z - 1.3, 0.0, check=False, cls="none", y=y0, tags={"wall"})
    # VOT: Rue du Moulin's climb into B, two flights across the west of the 6 m lane (the outer end runs into the house
    # block), a cart ramp along the east 1.8 m (the attackers' route keeps its timing)
    xt = plan.x0 + 3.5 * plan.cell
    for dx in (-2.4, 0.0):
        b.place("drs_town_steps", xt + dx, B_STEP_Z - 1.3, 0.0, check=False, cls="none", y=y0, tags={"wall"})
    # ground: cobbled lanes, the square and the spawn yards cobbled too, the churchyard grass,
    # a strip of setts (pebbles -> setts_paint) along every house front: the gutter
    for ch, kind in (("A", "grass"), ("B", "cobbles"), ("M", "cobbles"), ("T", "cobbles"), ("D", "cobbles"), ("s", "cobbles"), ("t", "cobbles"), ("p", "cobbles"), ("f", "cobbles"), ("R", "cobbles"), ("c", "cobbles"), ("w", "cobbles"), ("n", "cobbles"), ("k", "cobbles")):
        for i, j in plan.cells(ch):
            x, z = plan._cc(i, j)
            h = CELL / 2 + 0.9   # overlap: the sand base showed through the cell seams as light grid lines
            paint.poly(kind, [(x - h, z - h), (x + h, z - h), (x + h, z + h), (x - h, z + h)], weight=1.0, edge=0.6, ragged=0.4, seed=i * 31 + j)
    for ch in "TDA":   # worn tracks across the yards
        for i, j in plan.cells(ch):
            x, z = plan._cc(i, j)
            paint.poly("gravel", [(x - 1.4, z - 1.4), (x + 1.4, z - 1.4), (x + 1.4, z + 1.4), (x - 1.4, z + 1.4)], weight=0.55, edge=1.2, ragged=1.0, seed=i * 17 + j)
    for x, z, nx_, nz_, _h in houses:
        tx, tz = -nz_, nx_
        q = [(x + nx_ * d + tx * t, z + nz_ * d + tz * t) for d, t in ((0.0, -3.0), (1.05, -3.0), (1.05, 3.0), (0.0, 3.0))]
        paint.poly("pebbles", q, weight=1.0, edge=0.12)
    for ch in "stpfR":   # the narrow lanes drain to a central gutter of setts
        for i, j in plan.cells(ch):
            ns = plan.walk(i, j - 1) or plan.walk(i, j + 1)
            ew = plan.walk(i - 1, j) or plan.walk(i + 1, j)
            if ns != ew:
                x, z = plan._cc(i, j)
                hw, hd = (0.35, CELL / 2 + 0.2) if ns else (CELL / 2 + 0.2, 0.35)
                paint.poly("pebbles", [(x - hw, z - hd), (x + hw, z - hd), (x + hw, z + hd), (x - hw, z + hd)], weight=1.0, edge=0.1)
    Rp = rng(SEED + 7)
    for ch in "stpfRBM":   # puddles in the low spots
        cells = plan.cells(ch)
        for i, j in Rp.sample(cells, max(1, len(cells) // 5)):
            x, z = plan._cc(i, j)
            x += Rp.uniform(-1.8, 1.8); z += Rp.uniform(-1.8, 1.8)
            r1, r2 = Rp.uniform(0.6, 1.3), Rp.uniform(0.4, 0.9)
            paint.poly("mud", [(x - r1, z - r2), (x + r1 * 0.6, z - r2 * 1.1), (x + r1, z + r2 * 0.5), (x - r1 * 0.4, z + r2)], weight=0.8, edge=0.35, ragged=0.5, seed=i * 13 + j)

    # ---------------------------------------------------------------- dressing: masses behind the walls, a mid landmark, cover
    # the church behind the churchyard wall (north of A), then the town's roofs beyond the frontages (3.4 m clear of the
    # lanes: the facade bodies reach 2.9 m into the solid cells)
    _acs = [plan._cc(i, j) for i, j in plan.cells("A")]
    _ax = sum(p[0] for p in _acs) / len(_acs)
    _azmax = max(p[1] for p in _acs) + CELL / 2
    b.place("vil_church", _ax, _azmax + 1.2 + 4.7, 180.0, check=False, cls="none", y=y0, tags={"mass", "landmark"})
    n_mass = place_masses(b, plan, MASSES, R, y0, margin=3.4) + 1
    mx, mz = plan.centre("M")
    b.place("sd_monument", mx, mz, 0.0, check=False, cls="none", y=y0, tags={"landmark", "cover"})   # the square's war memorial (the old well's footprint): the mid landmark
    _xa, _za = plan.cc(*PLANT_A_CELL); _xb, _zb = plan.cc(*PLANT_B_CELL)
    PADS = [(_xa + PLANT_A_OFF[0], _za + PLANT_A_OFF[1], 3.5), (_xb + PLANT_B_OFF[0], _zb + PLANT_B_OFF[1], 3.5)]
    EYES = [("a_inside",) + inside_eye(plan, "A", PADS[0][:2])[:2] + (PADS[0][:2],), ("b_inside",) + inside_eye(plan, "B", PADS[1][:2])[:2] + (PADS[1][:2],),
            ("m_inside",) + inside_eye(plan, "M", plan.centre("M"), 8.0)[:2] + (plan.centre("M"),)]   # eye views from inside each site and mid
    PADS += [(ex, ez, 1.8) for _n, ex, ez, _t in EYES]
    n_cover = 0
    for ch, items in COVER.items():
        n_cover += place_cover(b, plan, ch, items, R, y0, keep=PADS)
    # interiors: the Workshop, Maintenance and the defender access are roofed rooms between the lanes (a slab on the walls,
    # a caged lamp per bay, a concrete floor, some clutter off the route)
    n_roof = 0
    for ch in INTERIORS:
        for i, j in plan.cells(ch):
            x, z = plan._cc(i, j)
            b.place("sd_roof_bay_timber", x, z, 0.0, check=False, cls="none", y=y0, tags={"roof", "interior"})
            b.place("lamp_hanging_lantern", x + 0.8, z, 0.0, check=False, cls="none", y=y0 + 3.0, tags={"lamp", "interior"})
            h = CELL / 2 + 0.9   # overlap: the sand base showed through the cell seams as light grid lines
            paint.poly("cobbles", [(x - h, z - h), (x + h, z - h), (x + h, z + h), (x - h, z + h)], weight=1.0, edge=0.3)
            n_roof += 1
        n_cover += place_cover(b, plan, ch, [("ph_metal_toolbox", 0.3, 0.15, 1), ("ph_wooden_crate_01", 0.5, 0.5, 1)], R, y0, lane=1.6)
    # wall lanterns on every third house front, washing lines across the narrow lanes
    n_lamp = 0
    by_edge = {(round(x, 1), round(z, 1)): h for x, z, _nx, _nz, h in houses}
    for k, (x, z, nx_, nz_, house) in enumerate(sorted(houses, key=lambda t: (t[0], t[1]))):
        if k % 3 == 0:
            tx, tz = -nz_, nx_
            b.place("lamp_wall_lantern", x + nx_ * 0.33 + tx * 2.75, z + nz_ * 0.33 + tz * 2.75, math.degrees(math.atan2(nx_, nz_)) % 360.0,
                    check=False, cls="none", y=y0 + 2.75, tags={"lamp"}, support=house)
            n_lamp += 1
    n_line = 0
    for ch in "stRf":
        for i, j in plan.cells(ch)[1::3]:
            x, z = plan._cc(i, j)
            ew = not plan.walk(i - 1, j) and not plan.walk(i + 1, j)   # the lane runs north-south: the line spans east-west
            ns = not plan.walk(i, j - 1) and not plan.walk(i, j + 1)
            host = by_edge.get((round(x - CELL / 2, 1), round(z, 1)) if ew else (round(x, 1), round(z - CELL / 2, 1)))
            if (ew or ns) and host is not None:
                b.place("sd_washing_line", x, z, 0.0 if ew else 90.0, check=False, cls="none", y=y0 + 4.9, tags={"dressing"}, support=host)
                n_line += 1
    # signs at every site entrance
    n_sign = site_signs(b, plan, "A", "a", y0) + site_signs(b, plan, "B", "b", y0)
    # the town kit against the walls (4.5 m clear of doorways, 1 m walkways): the market row and stalls in the Market Hall,
    # the cafe terrace and doorsteps on the square, a barricade and roadblock in the lanes, carts and rubble
    n_kit = 0
    # the square's divider walls carry a covered market (oak posts, a pent tile roof) with a stall under each bay
    used_m = []
    for x, z, yaw, kind, a, c in plan.edges():
        if kind or not (plan.walk(*a) and plan.walk(*c)) or "M" not in (plan.at(*a), plan.at(*c)):
            continue
        inside = a if plan.at(*a) == "M" else c
        ix, iz = plan._cc(*inside)
        nx_, nz_ = (1.0 if ix > x else -1.0, 0.0) if yaw == 90.0 else (0.0, 1.0 if iz > z else -1.0)
        fy = math.degrees(math.atan2(nx_, nz_)) % 360.0
        b.place("sd_market_lean_to", x, z, fy, check=False, cls="none", y=y0, tags={"cover", "market"})
        b.place("cl_town_market_stall", x + nx_ * 1.25, z + nz_ * 1.25, (fy + 90.0) % 360.0 + R.uniform(-3, 3), check=False, cls="none", y=y0, tags={"cover", "market"})
        used_m.append((x + nx_ * 1.2, z + nz_ * 1.2, 3.0 if yaw == 0.0 else 1.3, 1.3 if yaw == 0.0 else 3.0))
        n_kit += 2
    for ch, items in (("B", [("cl_town_market_row", 4.75, 1.6, 1), ("cl_town_market_stall", 1.6, 0.9, 2)]),
                      ("M", [("mod_fountain_wall", 1.7, 0.95, 1), ("cl_town_doorstep", 1.6, 0.6, 1), ("cl_town_bench_corner", 1.5, 0.7, 1)]),
                      ("A", [("cl_town_doorstep", 1.6, 0.6, 1), ("cl_town_bench_corner", 1.5, 0.7, 1)]),
                      ("s", [("cl_town_roadblock", 2.25, 0.5, 1), ("cl_town_doorstep", 1.6, 0.6, 2)]),
                      ("t", [("cl_town_barricade", 5.25, 1.8, 1), ("cl_town_doorstep", 1.6, 0.6, 2)]),
                      ("f", [("cl_town_doorstep", 1.6, 0.6, 1)]), ("R", [("cl_town_doorstep", 1.6, 0.6, 2), ("cl_town_cafe_terrace", 2.8, 1.2, 1)]),
                      ("c", [("cl_town_market_stall", 1.6, 0.9, 1)])):
        Rk = rng(SEED * 31 + ord(ch))   # each region's own draws: dressing one region never moves another's (the barricade on the B route)
        n_kit += place_along_walls(b, plan, ch, items, Rk, y0, keep=PADS, placed=used_m if ch == "M" else None)
    # the open square: stalls, a cart and benches round the memorial; the covered passages: barrels and doorsteps
    b.place("sd_memorial_railing", mx, mz, 0.0, check=False, cls="none", y=y0, tags={"cover"})
    taken = [(mx, mz, 3.6)]
    # market day: two rows of stalls under striped canopies across the square, 4.2 m either side of the memorial, 1.2 m
    # walks between them; a stall never within 3 m of a doorway, a plant pad or an audit eye
    mcs = [plan._cc(i, j) for i, j in plan.cells("M")]
    wide = (max(p[0] for p in mcs) - min(p[0] for p in mcs)) >= (max(p[1] for p in mcs) - min(p[1] for p in mcs))
    mdoors = door_points(plan, "M")
    for row in (-4.2, 4.2):
        for k in range(-3, 4):
            t = k * 4.7
            if k == 0:   # the memorial's benches
                continue
            sx, sz = (mx + t, mz + row) if wide else (mx + row, mz + t)
            syaw = 0.0 if wide else 90.0
            ex, ez = (1.75, 1.05) if wide else (1.05, 1.75)
            if not rect_ok(plan, sx, sz, ex, ez, walkable=True, margin=0.6):
                continue
            if any(math.hypot(sx - dx, sz - dz) < 3.0 + 1.75 for dx, dz in mdoors) or any(math.hypot(sx - kx, sz - kz) < kr + 1.75 for kx, kz, kr in PADS):
                continue
            if any(abs(sx - ux) < ua + ex + 1.0 and abs(sz - uz) < ud + ez + 1.0 for ux, uz, ua, ud in used_m):   # clear of the covered market
                continue
            b.place("cl_town_market_stall", sx, sz, syaw + R.uniform(-3, 3), check=False, cls="none", y=y0, tags={"cover", "market"})
            b.place("sd_stall_awning_%d" % R.randint(1, 3), sx, sz, syaw, check=False, cls="none", y=y0, tags={"cover", "market"})
            taken.append((sx, sz, 2.0))
            n_kit += 2
    n_kit += place_in_space(b, plan, "M", [("drs_cart", 1.9, 0.95, 2)], R, y0, PADS, taken, lane=1.0)
    # the cafe's terrace spills out in front of the cafe nearest the square
    cafes = [(math.hypot(x - mx, z - mz), x, z, nx_, nz_) for x, z, nx_, nz_, h in houses if h is not None and h["asset"] == "sd_facade_cafe"]
    if cafes:
        _d, x, z, nx_, nz_ = min(cafes)
        b.place("cl_town_cafe_terrace_large", x + nx_ * 2.75, z + nz_ * 2.75, math.degrees(math.atan2(-nz_, nx_)) % 360.0, check=False, cls="none", y=y0, tags={"cover"})
        n_kit += 1
    for k, (dx, dz) in enumerate(((0.0, 3.4), (3.4, 0.0), (0.0, -3.4), (-3.4, 0.0))):
        if k % 2 == 0:
            b.place("mod_garden_bench", mx + dx, mz + dz, math.degrees(math.atan2(-dx, -dz)) % 360.0, check=False, cls="none", y=y0, tags={"cover"})
            n_kit += 1
    for ch in "k":   # the defenders' access only: barrels in the bakery and cafe passages slowed the attackers' B route past 20 s
        n_kit += place_along_walls(b, plan, ch, [("drs_barrels", 1.0, 0.7, 1), ("cl_town_doorstep", 1.6, 0.6, 1)], R, y0, keep=PADS)
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
    t3["reseatMaxM"] = round(reseat_after_pads(b, y0), 3)
    from mapgen.relief import reseat_on_engine_ground, level_rigid_pieces
    ra = b.reseat_all

    def reseat_all(*a, **kw):   # VOT T3: last, on the ground the engine draws after its own pads (write_png calls this)
        m = ra(*a, **kw)
        for e in b.inst:   # churchyard cover off the terrace step (a stone across it sinks on one side)
            hd = max(b.cat[e["asset"]].half)
            if e["asset"] in {c[0] for c in A_COVER} and 15.0 < e["pos"][0] < 45.0 and e["pos"][2] - hd < A_STEP_Z + 0.1 and e["pos"][2] + hd > A_STEP_Z - 0.7:
                up, dn = A_STEP_Z + 0.1 + hd, A_STEP_Z - 0.7 - hd
                e["pos"][2] = round(up if up - e["pos"][2] < e["pos"][2] - dn else dn, 3)
                e["pos"][1] = b.ground(e["pos"][0], e["pos"][2])
        t3["engineReseat"] = reseat_on_engine_ground(b)
        n, worst = level_rigid_pieces(fine, b, skip=lambda e: e["asset"] in ("drs_retaining_wall", "drs_town_steps") or "wall" in e["asset"])
        def back_pad(e, y, ring=0.4):   # the block behind a facade (out of reach) at its seat, so its back sits on the ground
            cs, sn = math.cos(math.radians(e["yaw"])), math.sin(math.radians(e["yaw"]))
            x, z = e["pos"][0], e["pos"][2]

            def fn(X, Z, G):
                import numpy as np
                dx, dz = X - x, Z - z
                lx, lz = dx * cs - dz * sn, dx * sn + dz * cs
                d = np.hypot(np.maximum(np.abs(lx) - 2.6, 0), np.maximum(np.maximum(-3.2 - lz, lz + 0.6), 0))
                t = np.clip(d / ring, 0, 1)
                return y + (G - y) * (t * t * (3 - 2 * t))
            fine.add(fn, (x - 5, z - 5, x + 5, z + 5))
        # facades along a sloping lane: the door sill at the street, the low end on the 0.6 m stone foundation
        from mapgen.geom import local_to_world
        pads = []
        for e in b.inst:
            a = b.cat[e["asset"]]
            if not e["asset"].startswith("sd_facade"):
                continue
            fx, fz = local_to_world((e["pos"][0], e["pos"][2]), e["yaw"], 0.0, 0.5)
            front = [s[2] for s, n in zip(b._samples(a, e["pos"][0], e["pos"][2], e["yaw"], inset=0.1), b.NINE) if n[1] >= 0]
            y = min(b.ground(fx, fz), min(front) + a.raw.get("foundation", 0.0) - 0.02)
            pads.append((e, y))
            d = y - e["pos"][1]
            e["pos"][1] = y
            for c in b.inst:
                if c["extra"].get("support") == e["id"] or (c.get("host") == e["id"] and not c.get("free")):
                    c["pos"][1] += d
        for e, y in pads:
            back_pad(e, y)
        for e in b.inst:   # the roofed passages rest on their walls: the ceiling at the highest wall's seat (+ its lantern)
            if e["asset"] != "sd_roof_bay_timber":
                continue
            x, z = e["pos"][0], e["pos"][2]
            ys = [o["pos"][1] for o in b.inst if (o["asset"].startswith(("sd_facade", "sd_town_wall"))) and abs(o["pos"][0] - x) <= 3.4 and abs(o["pos"][2] - z) <= 3.4]
            if ys:
                d = max(ys) - e["pos"][1]
                e["pos"][1] += d
                for o in b.inst:
                    if o["asset"] == "lamp_hanging_lantern" and abs(o["pos"][0] - x - 0.8) < 0.1 and abs(o["pos"][2] - z) < 0.1:
                        o["pos"][1] = e["pos"][1] + 3.0   # as placed: from the ceiling
        t3["fineSeats"] = [n, round(worst, 3)]
        reseat_on_engine_ground(b, skip=lambda e: e["asset"].startswith(("sd_facade", "sd_roof_bay")) or e["asset"] == "lamp_hanging_lantern")   # the small pieces around the new seats
        t3["sculptTiles"] = len(fine.d)
        return m
    b.reseat_all = reseat_all
    for o in sc.objectives:   # VOT T3: the scenario on the tilted ground
        o["pos"][1] = round(b.ground(o["pos"][0], o["pos"][2]), 3)
        for sl in o.get("defenseSlots", []):
            sl["pos"][1] = round(b.ground(sl["pos"][0], sl["pos"][2]), 3)
    for s in sc.spawns:
        s["pos"][1] = round(b.ground(s["pos"][0], s["pos"][2]), 3)
    P = lambda i, j, dx=0.0, dz=0.0: [round(plan.cc(i, j)[0] + dx, 2), round(b.ground(plan.cc(i, j)[0] + dx, plan.cc(i, j)[1] + dz), 2), round(plan.cc(i, j)[1] + dz, 2)]
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
            {"name": "mid to A via cafe", "kind": "attackMid", "points": [P(12, 2), P(10, 2), P(9, 7), P(10, 8), P(10, 11), [round(plantA[0], 2), round(b.ground(*plantA), 2), round(plantA[1], 2)]]},
            {"name": "mid to B via bakery", "kind": "attackMid", "points": [P(12, 2), P(10, 2), P(9, 7), P(5, 8), P(4, 8), [round(plantB[0], 2), round(b.ground(*plantB), 2), round(plantB[1], 2)]]},
        ],
    }
    rules = {"mode": "snd", "sd": sd}
    err, warn = [], []

    # ---------------------------------------------------------------- output
    tdir = os.path.join(ctx.repo, "content", "templates")
    grid.write_png(os.path.join(tdir, TID + ".height.png"))
    a1 = sc.spawns[0]["pos"]
    cam = ([0.0, 120.0, -110.0], 0.0, -48.0)
    eye = 1.65
    views = [{"name": "overview_high", "pos": [0.0, 130.0, -100.0], "yaw": 0.0, "pitch": -55.0}]
    for name, ex, ez, (tx, tz) in EYES:
        views.append({"name": name, "pos": [round(ex, 2), round(b.ground(ex, ez) + eye, 2), round(ez, 2)], "yaw": look_yaw(tx - ex, tz - ez), "pitch": 0.0, "gameplay": True})
    for name, (i, j), (ti, tj) in (("a_site", (12, 10), (12, 12)), ("b_site", (3, 6), (2, 8)), ("mid", (9, 6), (8, 8)), ("t_spawn", (12, 1), (12, 3)), ("d_spawn", (4, 15), (4, 13)), ("bakery", (6, 8), (4, 8)), ("arcade", (5, 10), (9, 10))):
        x, z = plan.cc(i, j); tx, tz = plan.cc(ti, tj)
        views.append({"name": name + "_eye", "pos": [round(x, 2), round(b.ground(x, z) + eye, 2), round(z, 2)], "yaw": look_yaw(tx - x, tz - z), "pitch": 0.0, "gameplay": True})
    _v = views[1]   # the A site eye view (the old fixed offset from A looked into a wall)
    thumb_cam = ([_v["pos"][0], _v["pos"][1] + 0.6, _v["pos"][2]], _v["yaw"], -5.0)
    write_map(os.path.join(tdir, TID + ".nmap.json"), name=NAME, template_id=TID, template_version=VERSION, seed=SEED, width=W, depth=D,
              builder=b, scenario_json=sc.to_json(rules_extra=rules), explore_start=((a1[0], a1[1], a1[2]), 0.0), camera=cam, bookmarks={},
              extra={"terrainSculpt": fine.to_json(), "groundPaint": paint.to_json(), "lighting": {"hour": 9.6, "enclosed": False, "fill": 1.3},   # GR2: a bright clouded morning sky as the main light, a low soft sun raking the street, shade lifted
                     "look": {"haze": 0.7, "weather": 0.45, "clouds": 0.75, "saturation": 0.92, "tint": [1.0, 0.99, 0.97], "paintSets": {"pebbles": "setts_paint"}}})
    write_template(os.path.join(tdir, TID + ".json"), tid=TID, name=NAME, order=20, version=VERSION, seed=SEED,
                   description="Search and Destroy: a Norman town quarter on an overcast morning, its lanes between house fronts. "
                               "A: the churchyard; B: the market hall; Mid: the market square. 5 v 5.",
                   width=W, depth=D, margin=MARGIN, heightmap=grid.meta(TID + ".height.png"), water_level=-1000.0,
                   map_file=TID + ".nmap.json", camera=cam, thumbnail=TID + ".thumb.png", thumb_camera=thumb_cam,
                   extra={"userMap": True, "views": views})
    rep = {"instances": len(b.inst), "walls": nw, "masses": n_mass, "cover": n_cover, "plan": [plan.nx, plan.nz], "errors": err, "relief": t3}
    write_summary(os.path.join(ctx.out, TID + ".summary.json"), rep)
    print("instances %d | walls %d, masses %d, cover %d | plan %d x %d cells of %.0f m" % (len(b.inst), n_walls, n_mass, n_cover, plan.nx, plan.nz, CELL))
