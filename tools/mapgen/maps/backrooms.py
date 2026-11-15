"""Backrooms (BR1): a liminal interior, 216 x 216 m, built from the backrooms kit (tools/catalog_backrooms.mjs,
docs/backrooms.md). Familiar interiors made unsettling by emptiness and repetition.

The map is a 27 x 27 grid of 8 m cells, every one floored and ceilinged (no sky anywhere; the hour is also fixed at
night, extra.lighting.hour). Zones come from seeded Voronoi regions: yellow rooms (most of it), service areas and
poolrooms. Every edge between two cells is open, a doorless doorway, a wide opening (an arch in the poolrooms) or a
wall: a random spanning tree keeps every cell reachable, the other edges are mostly open in the yellow rooms and the
poolrooms (open plan with loops) and mostly walled between zones and in the service areas. Long straight corridors
run through the yellow rooms and the service areas, walled along their sides. Inside: offset partitions (some whole
fields of identical ones), columns at open corners, fluorescent panels on a regular grid (one lit per cell, a few
dark stretches of dead panels), pool basins and flooded cells, pipes. Dressing, seeded and sparse: furniture half sunk
into walls and floors, piles of chairs and wardrobes, a lone chair in an empty room, rooms full of identical chairs;
all kept off the cell centre lines so the routes stay open.

    <python> -I tools/mapgen/build.py backrooms"""
import math
import os

from mapgen import (GroundPaint, Grid, Frame, MapBuilder, ScenarioBuilder, write_map, write_template, write_summary,
                    asset_usage, rng, look_yaw)

TID, NAME, VERSION, SEED = "backrooms", "Backrooms", 1, 1100100
CELL = 8.0
NC = 27
W = D = NC * CELL
MARGIN = 60
CEIL = {"y": 3.6, "s": 4.2, "p": 4.4}   # BR3: higher ceilings (catalog_backrooms.mjs BR_CEIL)
EXPOSURE = 1.0   # BR3: pinned (x the fixed night exposure) in play and editor alike: auto exposure pulled the bright yellow down
AMBIENT = [0.13, 0.12, 0.08]   # BR4: lower, the panels' pools of light and shadows do the shaping (BR3 had 0.22, 0.2, 0.14)  # BR3: brighter (BR2b had 0.09, 0.085, 0.06); lights characters and furniture too   # BR2: an even ambient (extra.lighting.ambient): with the surfaces' glow, 1.5x the earlier ambient; light sources unchanged
GAMMA = 1.0   # BR2 (BR1c had 2.2, which washed the yellow out): the map's own gamma (extra.lighting.gamma, src/render.cpp)
DIRS = {"N": (0, 1), "E": (1, 0), "S": (0, -1), "W": (-1, 0)}


def cc(i, j):
    return (-W / 2 + (i + 0.5) * CELL, -D / 2 + (j + 0.5) * CELL)


def update_usage_doc(path, use, col, note=None):
    """The `col` column of docs/asset-usage-default-maps.md (placed counts), rows added for this map's new assets."""
    with open(path, encoding="utf-8") as f:
        lines = f.read().split("\n")
    hdr0 = next(l for l in lines if l.startswith("| Asset |"))
    names = [c.strip() for c in hdr0.strip().strip("|").split("|")]
    at = names.index(col) if col in names else None   # BR2: replace this map's own column (not the last one)
    out = []
    for l in lines:
        body = l.rstrip()
        cell = None
        if body.startswith("| Asset |"):
            cell = col
        elif body.startswith("|---|"):
            cell = "---"
        elif body.startswith("| `"):
            aid = body[3:body.index("`", 3)]
            cell = str(use[aid]) if aid in use else "not used"
        if cell is not None:
            if at is not None and not body.startswith("|---|"):
                parts = body.strip().strip("|").split("|")
                if len(parts) > at:
                    parts[at] = " %s " % cell
                body = "|" + "|".join(parts) + "|"
            elif at is None:
                body = body + (cell + "|" if cell == "---" else " %s |" % cell)
        out.append(body if cell is not None else l)
    known = {l[3:l.index("`", 3)] for l in out if l.startswith("| `")}
    hdr = next(i for i, l in enumerate(out) if l.startswith("| Asset |"))
    ncols = out[hdr].count("|") - 2
    k = max(i for i, l in enumerate(out) if l.startswith("| `"))
    for aid in sorted(set(use) - known):
        k += 1
        out.insert(k, "| `%s` |" % aid + " not used |" * (ncols - 1) + " %d |" % use[aid])
    if note and not any(l.startswith(note.split(":**")[0] + ":**") for l in out):
        k = max(i for i, l in enumerate(out) if l.startswith("- **"))
        out.insert(k + 1, note)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out))


def build(ctx):
    R = rng(SEED)
    frame = Frame(W, D)
    grid = Grid(W, D, margin=MARGIN, base=-1.0)   # flat, 1 m under the floor slabs (BR2: at 0 the terrain and its mud paint showed through the carpet)
    grid.quantise()
    b = MapBuilder(ctx.catalog, grid, frame, seed=SEED)
    paint = GroundPaint()
    paint.poly("mud", [(-W / 2 - MARGIN, -D / 2 - MARGIN), (W / 2 + MARGIN, -D / 2 - MARGIN), (W / 2 + MARGIN, D / 2 + MARGIN), (-W / 2 - MARGIN, D / 2 + MARGIN)], weight=1.0, edge=0.5)
    cells = [(i, j) for i in range(NC) for j in range(NC)]

    # ---------------------------------------------------------------- zones (Voronoi over seeded points)
    seeds = [((-40, 20), "p"), ((55, -70), "p"), ((-75, -75), "p"), ((10, -30), "s"), ((80, 65), "s"), ((-80, 75), "s")]
    seeds += [((R.uniform(-100, 100), R.uniform(-100, 100)), "y") for _ in range(18)]
    wts = [R.uniform(0.9, 1.1) * (1.0 if z != "y" else 1.0) for _, z in seeds]   # the yellow rooms win most of the grid
    zone = {}
    for c in cells:
        x, z = cc(*c)
        k = min(range(len(seeds)), key=lambda n: math.hypot(x - seeds[n][0][0], z - seeds[n][0][1]) * wts[n])
        zone[c] = seeds[k][1]
    # spawn columns stay yellow (plain rooms to start in)
    for c in cells:
        if c[0] in (0, NC - 1):
            zone[c] = "y"

    # ---------------------------------------------------------------- connectivity: spanning tree, then edge states
    edges = []
    for (i, j) in cells:
        if i + 1 < NC:
            edges.append(((i, j), (i + 1, j)))
        if j + 1 < NC:
            edges.append(((i, j), (i, j + 1)))
    order = edges[:]
    R.shuffle(order)
    parent = {c: c for c in cells}

    def find(c):
        while parent[c] != c:
            parent[c] = parent[parent[c]]
            c = parent[c]
        return c
    tree = set()
    for a, c in sorted(order, key=lambda e: (zone[e[0]] != zone[e[1]], R.random())):   # within a zone first: few boundary crossings
        ra, rc = find(a), find(c)
        if ra != rc:
            parent[ra] = rc
            tree.add((a, c))
    state = {}
    for e in edges:
        a, c = e
        za, zc = zone[a], zone[c]
        t = e in tree
        if za != zc:
            st = R.choices(["door", "wide"], [70, 30])[0] if t else R.choices(["wall", "door"], [82, 18])[0]
        elif za == "y":
            st = R.choices(["open", "wide", "door"], [55, 25, 20])[0] if t else R.choices(["open", "wall", "door", "wide"], [38, 34, 14, 14])[0]
        elif za == "p":
            st = R.choices(["open", "arch", "door"], [45, 40, 15])[0] if t else R.choices(["open", "arch", "wall"], [35, 30, 35])[0]
        else:
            st = R.choices(["open", "door", "wide"], [45, 35, 20])[0] if t else R.choices(["wall", "door", "open"], [62, 18, 20])[0]
        state[e] = st

    # long straight corridors: open along, walled on the sides (a tree edge across stays a doorway)
    corridor = set()
    run_list = []
    runs = 0
    tries = 0
    while runs < 9 and tries < 3000:
        tries += 1
        horiz = R.random() < 0.5
        L = R.randint(8, 16)
        i0, j0 = R.randrange(1, NC - 1 - (L if horiz else 0)), R.randrange(1, NC - 1 - (0 if horiz else L))
        run = [(i0 + k, j0) if horiz else (i0, j0 + k) for k in range(L)]
        zs = {zone[c] for c in run}
        if len(zs) != 1 or zs.pop() == "p" or any(c in corridor for c in run):
            continue
        runs += 1
        run_list.append((run, horiz))
        for k, c in enumerate(run):
            corridor.add(c)
            if k + 1 < L:
                state[(c, run[k + 1])] = "open"
            for d in ((0, 1), (0, -1)) if horiz else ((1, 0), (-1, 0)):
                o = (c[0] + d[0], c[1] + d[1])
                if not (0 <= o[0] < NC and 0 <= o[1] < NC):
                    continue
                e = (c, o) if (c, o) in state else (o, c)
                if o in corridor:
                    continue
                state[e] = "door" if (e in tree or R.random() < 0.12) else "wall"

    # ---------------------------------------------------------------- cells
    basins, flooded = set(), set()
    Rv = rng(SEED + 11)   # BR4: own sequence for the cell variants (the rest of the map is unchanged)
    n_cell = {"y": 0, "s": 0, "p": 0}
    for c in cells:
        z = zone[c]
        x, zz = cc(*c)
        if z == "y":
            aid = Rv.choice(["brm_cell_yellow", "brm_cell_yellow_b", "brm_cell_yellow_c", "brm_cell_yellow_c"])   # BR4: three detailed variants (BR2: the damp one is not used)
            R.random()   # keeps the seeded sequence of BR1
        elif z == "s":
            aid = "brm_cell_service_cross" if R.random() < 0.3 else "brm_cell_service"
        else:
            u = R.random()
            if u < 0.13 and c[0] not in (0, NC - 1):
                aid = "brm_cell_pool_basin"
                basins.add(c)
            elif u < 0.45:
                aid = "brm_cell_pool_flooded"
                flooded.add(c)
            else:
                aid = "brm_cell_pool"
        b.place(aid, x, zz, 90.0 * R.randrange(4) if aid != "brm_cell_pool_basin" else 0.0, check=False, cls="none", y=0.0, tags={"cell", "zone_" + z})
        n_cell[z] += 1

    # ---------------------------------------------------------------- walls on the edges and the border
    WALL_ZONE = {"s": 3, "p": 2, "y": 1}
    n_wall = {"wall": 0, "door": 0, "wide": 0, "arch": 0, "open": 0}
    wall_faces = []   # (cell, side) of full walls: for furniture sunk into them

    def put_wall(x, z, yaw, zn, st):
        if st == "open":
            return
        aid = "brm_wall_%s" % zn + ("" if st == "wall" else "_door" if st == "door" else "_wide" if st == "wide" else "")
        if st == "arch":
            aid = "brm_wall_p_arch" if zn == "p" else "brm_wall_%s_wide" % zn
        b.place(aid, x, z, yaw, check=False, cls="none", y=0.0, tags={"wall"})
    for e, st in state.items():
        a, c = e
        zn = max(zone[a], zone[c], key=lambda q: WALL_ZONE[q])
        xa, za = cc(*a)
        xc, zc = cc(*c)
        horiz = a[1] == c[1]   # a and c side by side in x: the wall runs along z
        put_wall((xa + xc) / 2, (za + zc) / 2, 90.0 if horiz else 0.0, zn, st)
        n_wall[st] += 1
        if st == "wall":
            wall_faces.append((a, "E" if horiz else "N"))
            wall_faces.append((c, "W" if horiz else "S"))
    for c in cells:
        x, z = cc(*c)
        for s, (dx, dz) in DIRS.items():
            o = (c[0] + dx, c[1] + dz)
            if 0 <= o[0] < NC and 0 <= o[1] < NC:
                continue
            put_wall(x + dx * CELL / 2, z + dz * CELL / 2, 90.0 if dx else 0.0, zone[c], "wall")
            n_wall["wall"] += 1
            wall_faces.append((c, s))

    def open_sides(c):
        out = []
        for s, (dx, dz) in DIRS.items():
            o = (c[0] + dx, c[1] + dz)
            if 0 <= o[0] < NC and 0 <= o[1] < NC:
                e = (c, o) if (c, o) in state else (o, c)
                if state[e] != "wall":
                    out.append(s)
        return out

    # ---------------------------------------------------------------- partitions and columns
    busy = set(corridor) | basins
    fields = []
    for _ in range(200):   # partition fields: the same offset partition repeated cell after cell
        i0, j0 = R.randrange(1, NC - 5), R.randrange(1, NC - 5)
        if len(fields) >= 5:
            break
        blk = [(i0 + di, j0 + dj) for di in range(4) for dj in range(R.randint(3, 4))]
        if all(zone[c] == "y" and c not in busy for c in blk):
            fields.append(blk)
            busy |= set(blk)
            ox, oz, yaw = R.uniform(-1.4, 1.4), R.uniform(-1.4, 1.4), R.choice([0.0, 90.0])
            for k, c in enumerate(blk):
                x, z = cc(*c)
                b.place("brm_partition", x + ox, z + oz, yaw + (90.0 if (c[0] + c[1]) % 2 and R.random() < 0.3 else 0.0), check=False, cls="none", y=0.0, tags={"partition"})
    n_part = sum(len(f) for f in fields)
    for c in cells:
        if zone[c] != "y" or c in busy or R.random() > 0.38:
            continue
        x, z = cc(*c)
        pid = R.choices(["brm_partition", "brm_partition_short", "brm_partition_l", "brm_partition_low"], [45, 25, 18, 12])[0]
        b.place(pid, x + R.uniform(-1.3, 1.3), z + R.uniform(-1.3, 1.3), R.choice([0.0, 90.0, 180.0, 270.0]), check=False, cls="none", y=0.0, tags={"partition"})
        busy.add(c)
        n_part += 1
    n_col = 0
    for i in range(1, NC):
        for j in range(1, NC):   # the grid vertex between cells (i-1..i, j-1..j)
            quad = [(i - 1, j - 1), (i, j - 1), (i - 1, j), (i, j)]
            zs = {zone[c] for c in quad}
            if len(zs) != 1:
                continue
            ee = [((i - 1, j - 1), (i, j - 1)), ((i - 1, j), (i, j)), ((i - 1, j - 1), (i - 1, j)), ((i, j - 1), (i, j))]
            if any(state[e] != "open" for e in ee) or R.random() > 0.8:
                continue
            zn = zs.pop()
            b.place("brm_column_" + zn, -W / 2 + i * CELL, -D / 2 + j * CELL, 0.0, check=False, cls="none", y=0.0, tags={"column"})
            n_col += 1
    Rc = rng(SEED + 7)   # BR2: thick pillars in the middle of open yellow cells (own sequence: the rest of the map is unchanged)
    for c in cells:
        if zone[c] == "y" and c not in busy and len(open_sides(c)) == 4 and Rc.random() < 0.45:
            x, z = cc(*c)
            b.place("brm_column_y", x, z, 0.0, check=False, cls="none", y=0.0, tags={"column"})
            busy.add(c)
            n_col += 1

    # ---------------------------------------------------------------- lights (none flicker): panels on a grid, one lit per cell
    dark = [(R.uniform(-90, 90), R.uniform(-90, 90)) for _ in range(3)]
    n_lights = {"brm_panel_lit": 0, "brm_pool_light": 0, "brm_bulkhead": 0}
    n_panels = n_dead = 0
    for c in cells:
        x, z = cc(*c)
        zn = zone[c]
        h = CEIL[zn]
        in_dark = any(math.hypot(x - dx, z - dz) < 15 for dx, dz in dark)
        if zn == "y":
            for k, (ox, oz) in enumerate(((-2.0, -2.0), (2.0, -2.0), (-2.0, 2.0), (2.0, 2.0))):   # BR2: four square panels per cell, a 4 m grid
                if R.random() < 0.02:   # BR2: a rare dead panel, no dark stretches (in_dark kept for pool / service)
                    pid = "brm_panel_dead"
                    n_dead += 1
                elif k == (c[0] + 2 * c[1]) % 4:
                    pid = "brm_panel_lit"
                else:
                    pid = "brm_panel"
                b.place(pid, x + ox, z + oz, 0.0, check=False, cls="none", y=h, tags={"lamp"})
                n_panels += 1
                if pid in n_lights:
                    n_lights[pid] += 1
        elif zn == "p":
            if not in_dark:
                b.place("brm_pool_light", x, z, 0.0, check=False, cls="none", y=h, tags={"lamp"})
                n_lights["brm_pool_light"] += 1
        else:
            if not in_dark and R.random() < 0.75:
                b.place("brm_bulkhead", x + 1.2, z + 1.2, R.choice([0.0, 90.0]), check=False, cls="none", y=h, tags={"lamp"})
                n_lights["brm_bulkhead"] += 1

    # ---------------------------------------------------------------- dressing: sparse and unpredictable, never on the centre lines
    CORNERS = [(2.4, 2.4), (-2.4, 2.4), (2.4, -2.4), (-2.4, -2.4)]
    dressed = set()
    dress = {"sunk_wall": 0, "sunk_floor": 0, "piles": 0, "lone_chair": 0, "identical_rooms": 0}
    R.shuffle(wall_faces)
    for c, s in wall_faces:
        if dress["sunk_wall"] >= 26 or c in dressed or c in basins or c[0] in (0, NC - 1) or R.random() > 0.2:
            continue
        x, z = cc(*c)
        dx, dz = DIRS[s]
        along = R.choice([-2.6, 2.6])
        px, pz = x + dx * (CELL / 2 - 0.1) + (dz * along), z + dz * (CELL / 2 - 0.1) + (dx * along)
        aid = R.choice(["brm_sunk_wardrobe_wall", "brm_sunk_wardrobe_wall", "brm_sunk_bed_wall", "brm_sunk_chair_wall"])
        b.place(aid, px, pz, look_yaw(-dx, -dz), check=False, cls="none", y=0.0, tags={"dressing"})
        dressed.add(c)
        dress["sunk_wall"] += 1
    pool = [c for c in cells if c not in dressed and c not in basins and c not in flooded and c[0] not in (0, NC - 1)]
    R.shuffle(pool)
    for c in pool[:26]:
        x, z = cc(*c)
        ox, oz = R.choice(CORNERS)
        aid = R.choice(["brm_sunk_wardrobe_floor", "brm_sunk_table_floor", "brm_sunk_armchair_floor", "furn_armoire", "furn_bed", "furn_table_dining"])
        b.place(aid, x + ox, z + oz, R.uniform(0, 360), check=False, cls="none", y=-0.45 if aid.startswith("furn_") else 0.0, tags={"dressing"})
        dressed.add(c)
        dress["sunk_floor"] += 1
    for c in pool[26:38]:
        x, z = cc(*c)
        ox, oz = R.choice(CORNERS)
        b.place(R.choice(["brm_pile_chairs", "brm_pile_chairs", "brm_pile_wardrobes"]), x + ox * 0.95, z + oz * 0.95, R.uniform(0, 360), check=False, cls="none", y=0.0, tags={"dressing", "cover"})
        dressed.add(c)
        dress["piles"] += 1
    lone = [c for c in pool[38:] if zone[c] == "y" and c not in busy and len(open_sides(c)) <= 2]
    for c in lone[:6]:
        x, z = cc(*c)
        b.place("brm_chair_lone", x + R.uniform(-1.5, 1.5), z + R.uniform(-1.5, 1.5), R.uniform(0, 360), check=False, cls="none", y=0.0, tags={"dressing"})
        dressed.add(c)
        dress["lone_chair"] += 1
    full = [c for c in pool[38:] if c not in dressed and c not in busy and zone[c] in ("y", "s")]
    for c in full[:3]:   # a room full of identical chairs in rows, aisles between them
        x, z = cc(*c)
        yaw = R.choice([0.0, 90.0, 180.0, 270.0])
        for gx in (-2.6, -1.0, 1.0, 2.6):
            for gz in (-2.6, -1.2, 1.2, 2.6):
                b.place("brm_chair_lone", x + gx, z + gz, yaw, check=False, cls="none", y=0.0, tags={"dressing"})
        dressed.add(c)
        dress["identical_rooms"] += 1

    # ---------------------------------------------------------------- scenario
    def nearest(target, want, avoid=()):
        best = min((c for c in cells if zone[c] == want and c not in basins and c not in avoid and c[0] not in (0, NC - 1)),
                   key=lambda c: math.hypot(cc(*c)[0] - target[0], cc(*c)[1] - target[1]))
        return best
    obj_cells = [nearest((-40, 20), "p"), nearest((10, -30), "s"), nearest((60, 25), "y")]
    west = [(0, j) for j in range(8, 19)]
    east = [(NC - 1, j) for j in range(8, 19)]
    R.shuffle(west)
    R.shuffle(east)
    sc = ScenarioBuilder(b, defending="Axis")
    labels = ["The poolrooms", "The pipe halls", "The yellow rooms"]
    prev = cc(*west[0])
    for c, lab in zip(obj_cells, labels):
        x, z = cc(*c)
        sc.objective(lab, x, z, radius=9.0, capture_time=45.0, attack_from=prev)
        prev = (x, z)
    for k, c in enumerate(west[:3]):
        x, z = cc(*c)
        sc.spawn("attacker", [0, 1, 2], x, z, label="Allies - west rooms %d" % (k + 1), face=(x + 10, z), rack=None)
    for k, c in enumerate(east[:3]):
        x, z = cc(*c)
        sc.spawn("defender", [0, 1, 2], x, z, label="Axis - east rooms %d" % (k + 1), face=(x - 10, z), rack=None)
    for team, c, fac, lab in ((0, east[3], "Axis", "Team A spawn (Axis)"), (1, west[3], "Allies", "Team B spawn (Allies)")):
        x, z = cc(*c)
        m = sc.spawn("defender" if team == 0 else "attacker", [0], x, z, face=(0.0, z), faction=fac, label=lab, rack=None)
        m["team"], m["phases"] = team, []
    sc.rules["teamFaction"] = ["Axis", "Allies"]
    for m in sc.spawns:   # a supply crate against the outer wall next to every spawn
        x, z = m["pos"][0], m["pos"][2]
        dx = -1.0 if x < 0 else 1.0
        b.place("crate_mixed_supply", x + dx * 3.0, z + 2.6, look_yaw(-dx, 0.0), check=False, cls="none", y=0.0, tags={"supply"})
    for m in sc.spawns + sc.objectives:   # BR2: on the floor slabs, not the sunken terrain
        m["pos"][1] = 0.0
    err, warn = sc.validate()

    # ---------------------------------------------------------------- numbers
    rooms = {}   # rooms = cells joined by open edges (open plan areas), by size
    par = {c: c for c in cells}

    def f2(c):
        while par[c] != c:
            par[c] = par[par[c]]
            c = par[c]
        return c
    for e, st in state.items():
        if st == "open":
            par[f2(e[0])] = f2(e[1])
    for c in cells:
        rooms.setdefault(f2(c), []).append(c)
    sizes = sorted(len(v) for v in rooms.values())
    by_size = {"1 cell (8 m)": sum(1 for s in sizes if s == 1), "2-4 cells": sum(1 for s in sizes if 2 <= s <= 4),
               "5-15 cells": sum(1 for s in sizes if 5 <= s <= 15), "16+ cells (open halls)": sum(1 for s in sizes if s >= 16)}
    corr_len = len(corridor) * CELL
    lights = sum(n_lights.values())
    # capture spots (eye height): a yellow corridor looking along it, the largest yellow open area, a poolroom
    yr = next(((r, hz) for r, hz in run_list if zone[r[0]] == "y"), run_list[0])
    (ci, cj), hz = yr[0][1], yr[1]
    x0, z0 = cc(ci, cj)
    big = max((v for v in rooms.values() if zone[v[0]] == "y"), key=len)
    hx = sum(cc(*c)[0] for c in big) / len(big); hzm = sum(cc(*c)[1] for c in big) / len(big)
    hc = min(big, key=lambda c: math.hypot(cc(*c)[0] - hx, cc(*c)[1] - hzm))
    far = max(big, key=lambda c: math.hypot(cc(*c)[0] - cc(*hc)[0], cc(*c)[1] - cc(*hc)[1]))
    pc = obj_cells[0]
    cams = {"yellow_corridor": [x0, 1.65, z0, x0 + (40 if hz else 0), 1.5, z0 + (0 if hz else 40)],
            "yellow_hall": [cc(*hc)[0], 1.65, cc(*hc)[1], cc(*far)[0], 1.3, cc(*far)[1]],
            "poolroom": [cc(*pc)[0] - 3.5, 1.65, cc(*pc)[1] - 3.5, cc(*pc)[0] + 6, 0.6, cc(*pc)[1] + 6]}
    rep = {"cams": {k: [round(v, 2) for v in c] for k, c in cams.items()}, "cells": n_cell, "rooms": len(rooms), "roomsBySize": by_size, "largestRoomCells": sizes[-1], "corridorRuns": runs,
           "corridorLength_m": corr_len, "edges": n_wall, "partitions": n_part, "partitionFields": len(fields), "columns": n_col,
           "basins": len(basins), "floodedCells": len(flooded), "panels": n_panels, "deadPanels": n_dead, "lights": lights, "lightsByKind": n_lights,
           "dressing": dress}

    # ---------------------------------------------------------------- output
    tdir = os.path.join(ctx.repo, "content", "templates")
    grid.write_png(os.path.join(tdir, TID + ".height.png"))
    a1 = sc.spawns[0]["pos"]
    run0 = sorted(corridor)
    cx0, cz0 = cc(*run0[len(run0) // 2])
    eye = 1.65
    cam = ([0.0, 150.0, 170.0], 0.0, -45.0)
    pool_c = cc(*obj_cells[0])
    thumb_cam = ([round(cx0, 2), eye, round(cz0, 2)], look_yaw(1.0, 0.0), -2.0)
    views = [
        {"name": "overview_high", "pos": [0.0, 170.0, 150.0], "yaw": 0.0, "pitch": -50.0},
        {"name": "corridor_eye", "pos": [round(cx0, 2), eye, round(cz0, 2)], "yaw": look_yaw(1.0, 0.0), "pitch": 0.0, "gameplay": True},
        {"name": "poolrooms_eye", "pos": [round(pool_c[0] - 3.0, 2), eye, round(pool_c[1] - 3.0, 2)], "yaw": look_yaw(1.0, 1.0), "pitch": 0.0, "gameplay": True},
    ]
    write_map(os.path.join(tdir, TID + ".nmap.json"), name=NAME, template_id=TID, template_version=VERSION, seed=SEED, width=W, depth=D,
              builder=b, scenario_json=sc.to_json(), explore_start=((a1[0], a1[1], a1[2]), look_yaw(1.0, 0.0)), camera=cam,
              bookmarks={str(i + 1): {"position": [o["pos"][0], 40.0, o["pos"][2] + 30], "yaw": 0.0, "pitch": -50} for i, o in enumerate(sc.objectives)},
              extra={"groundPaint": paint.to_json(), "lighting": {"hour": 0.0, "gamma": GAMMA, "ambient": AMBIENT, "exposure": EXPOSURE}, "look": {"clean": True, "fog": False, "bloom": 2.5, "shadows": 4}})   # BR2: clean, completely clear
    write_template(os.path.join(tdir, TID + ".json"), tid=TID, name=NAME, order=8, version=VERSION, seed=SEED,
                   description="A liminal interior, 216 x 216 m: faded yellow rooms under humming fluorescent panels, offset partitions "
                               "and corridors that do not end, bare service halls full of pipes, and tiled poolrooms of still turquoise "
                               "water. Furniture sinks into the walls and floors. Assault (the Allies push in from the west rooms: "
                               "the poolrooms, the pipe halls, the yellow rooms), Team Deathmatch and Solo.",
                   width=W, depth=D, margin=MARGIN, heightmap=grid.meta(TID + ".height.png"), water_level=-1000.0,
                   map_file=TID + ".nmap.json", camera=cam, thumbnail=TID + ".thumb.png", thumb_camera=thumb_cam,
                   extra={"userMap": True, "views": views})
    use = asset_usage(b)
    update_usage_doc(os.path.join(ctx.repo, "docs", "asset-usage-default-maps.md"), use, "Backrooms",
                     "- **Backrooms:** *Backrooms* (BR1, `tools/mapgen/maps/backrooms.py`): placed counts; \"not used\" = not on this map.")
    rep.update({"instances": len(b.inst), "errors": err, "warnings": warn, "use": dict(sorted(use.items())),
                "objectives": [(o["label"], o["displayName"], o["pos"]) for o in sc.objectives], "spawns": [(m["label"], m["pos"]) for m in sc.spawns]})
    write_summary(os.path.join(ctx.out, TID + ".summary.json"), rep)
    print("instances %d | cells %s | rooms %d %s, largest %d cells | corridors %d runs, %d m | edges %s | partitions %d (%d fields), columns %d | "
          "basins %d, flooded %d | panels %d (%d dead), lights %d %s | dressing %s | errors %s | warnings %d" %
          (len(b.inst), n_cell, len(rooms), by_size, sizes[-1], runs, corr_len, n_wall, n_part, len(fields), n_col, len(basins), len(flooded),
           n_panels, n_dead, lights, n_lights, dress, err, len(warn)))
