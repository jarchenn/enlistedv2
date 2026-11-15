"""Tunnels (UG1): an underground labyrinth, 216 x 216 m (2/3 of D-day's area), built from the tunnel kit
(tools/catalog_tunnels.mjs, docs/tunnels.md).

The map is a grid of 8 m cells. Junction nodes sit on a 13 x 13 lattice (every other cell); the cell between two
linked nodes is a passage. A seeded maze links the nodes: a random spanning tree (every node reachable on full-height
tunnels) plus about a third of the remaining links as loops, so there are several routes between any two areas;
some loop links are low squeezes the heavies cannot use. Five chambers (storage, bunk room, generator room, flooded
sump, collapse) replace a node and its four passages. An upper level (3.6 m up) covers the north-east, joined by
three stair drifts. Every piece is a closed shell (floor, walls, ceiling) on flat ground: nothing outside a passage
is visible or reachable, and the map's hour is fixed at night (extra.lighting.hour) so any leak is dark.

Lamps: caged bulbs under the ceiling, sparse (dark zones with almost none), many failing (flicker) or dead, red
emergency lamps at some junctions, shadows on a few chamber lamps only.

    <python> -I tools/mapgen/build.py tunnels

Writes content/templates/tunnels.{json,height.png,nmap.json} and the "Tunnels" column of
docs/asset-usage-default-maps.md. The thumbnail comes from --render-map-thumbs."""
import math
import os

from mapgen import (GroundPaint, Grid, Frame, MapBuilder, ScenarioBuilder, write_map, write_template, write_summary,
                    asset_usage, rng, look_yaw)

TID, NAME, VERSION, SEED = "tunnels", "Tunnels", 2, 19440605
CELL = 8.0
NC = 27                          # cells per side: 216 m
W = D = NC * CELL
MARGIN = 60
NN = 13                          # node lattice: node (a, b) is cell (2a + 1, 2b + 1)
L1Y = 3.6                        # the upper level (tun_stairs rise)
L1 = [(a, b) for a in range(8, 13) for b in range(8, 13)]
STAIRS = [(7, 8), (7, 10), (7, 12)]          # L0 node (7, b) -> L1 node (8, b), stairs in cell (16, 2b + 1)
CHAMBERS = {(6, 6): "generator", (9, 3): "sump", (10, 10): "collapse", (3, 2): "store", (2, 9): "bunks"}   # level 0
DIRS = {"N": (0, 1), "E": (1, 0), "S": (0, -1), "W": (-1, 0)}
OPP = {"N": "S", "S": "N", "E": "W", "W": "E"}
ROT = {"N": "E", "E": "S", "S": "W", "W": "N"}   # an instance yaw of +90 deg turns the asset's sides this way
BASE = {"end": ["S"], "straight": ["N", "S"], "corner": ["N", "E"], "tee": ["N", "E", "W"], "cross": ["N", "E", "S", "W"]}
MEDIUM = {"office": (5.0, 2.9), "workshop": (6.0, 3.0), "magazine": (5.5, 2.9), "dugout": (6.5, 3.0), "pump": (5.5, 2.9), "mess": (7.0, 3.0)}   # UG2: half size, height
N_MEDIUM0, N_MEDIUM1 = 16, 2     # medium rooms on level 0 / the upper level
CEIL = {"tun_straight_shelled": 2.75, "tun_breach": 2.9, "tun_cross_shelled": 2.9, "tun_tee_shelled": 2.9, "tun_room_s_end": 2.75, "tun_room_s_straight": 2.75,
        "tun_room_s_corner": 2.75, "tun_room_s_tee": 2.75, "tun_room_s_cross": 2.75, "tun_end": 2.75, "tun_straight": 2.75, "tun_straight_kink": 2.75, "tun_straight_niche": 2.75, "tun_straight_lined": 2.75,
        "tun_wide": 3.0, "tun_corner": 2.75, "tun_corner_lined": 2.75, "tun_tee": 2.9, "tun_cross": 2.9}
CH_H = 3.3
PORTAL_CLEAR = 1.6   # keep props this far off the axes of a room's doorways


def cc(i, j):
    """Cell centre (world x, z)."""
    return (-W / 2 + (i + 0.5) * CELL, -D / 2 + (j + 0.5) * CELL)


def node_cell(n):
    return (2 * n[0] + 1, 2 * n[1] + 1)


def fit(sides):
    """(shape, yaw) of the kit piece whose open sides, turned by yaw, are `sides` (world N = +z)."""
    want = set(sides)
    for shape, s in BASE.items():
        cur = set(s)
        for k in range(4):
            if cur == want:
                return shape, 90.0 * k
            cur = {ROT[x] for x in cur}
    raise ValueError(sides)


def side_to(p, q):
    d = (q[0] - p[0], q[1] - p[1])
    return next(k for k, v in DIRS.items() if v == d)


def maze(nodes, R, loops, forced=(), banned=()):
    """Spanning tree over the lattice neighbours of `nodes` (randomised Kruskal) plus loop links; `forced` links are
    always in, `banned` never. Returns (tree edges, loop edges) as sets of frozensets."""
    nodes = set(nodes)
    cand = []
    for p in nodes:
        for d in DIRS.values():
            q = (p[0] + d[0], p[1] + d[1])
            if q in nodes and p < q and frozenset((p, q)) not in banned:
                cand.append((p, q))
    R.shuffle(cand)
    parent = {p: p for p in nodes}

    def find(p):
        while parent[p] != p:
            parent[p] = parent[parent[p]]
            p = parent[p]
        return p
    tree, rest = set(), []
    for p, q in [(a, b) for a, b in cand if frozenset((a, b)) in forced] + cand:
        e = frozenset((p, q))
        if e in tree:
            continue
        a, b = find(p), find(q)
        if a != b:
            parent[a] = b
            tree.add(e)
        else:
            rest.append(e)
    extra = {e for e in rest if e in forced or R.random() < loops}
    return tree, extra - tree


def components(nodes, edges):
    adj = {p: [] for p in nodes}
    for e in edges:
        p, q = tuple(e)
        adj[p].append(q)
        adj[q].append(p)
    seen, comps = set(), 0
    for p in nodes:
        if p in seen:
            continue
        comps += 1
        st = [p]
        while st:
            x = st.pop()
            if x in seen:
                continue
            seen.add(x)
            st.extend(adj[x])
    return comps


def update_usage_doc(path, use, col="Tunnels", note=None):
    """The `col` column of docs/asset-usage-default-maps.md (placed counts); as dday.py's, for another map."""
    with open(path, encoding="utf-8") as f:
        lines = f.read().split("\n")
    has = any(l.startswith("| Asset |") and col in l for l in lines)
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
            if has:
                body = body[:-1].rstrip()
                body = body[:body.rfind("|") + 1]
            body = body + (cell + "|" if cell == "---" else " %s |" % cell)
        out.append(body if cell is not None else l)
    # assets of this map missing from the table get rows (the other columns "not used")
    known = {l[3:l.index("`", 3)] for l in out if l.startswith("| `")}
    hdr = next(i for i, l in enumerate(out) if l.startswith("| Asset |"))
    ncols = out[hdr].count("|") - 2
    k = max(i for i, l in enumerate(out) if l.startswith("| `"))
    for aid in sorted(set(use) - known):
        row = "| `%s` |" % aid + " not used |" * (ncols - 1) + " %d |" % use[aid]
        k += 1
        out.insert(k, row)
    if note and not any(l.startswith(note.split(":**")[0] + ":**") for l in out):
        k = max(i for i, l in enumerate(out) if l.startswith("- **"))
        out.insert(k + 1, note)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out))


def build(ctx):
    R = rng(SEED)
    frame = Frame(W, D)
    grid = Grid(W, D, margin=MARGIN)   # flat: the passages are closed shells on the ground
    grid.quantise()
    b = MapBuilder(ctx.catalog, grid, frame, seed=SEED)
    paint = GroundPaint()
    paint.poly("mud", [(-W / 2 - MARGIN, -D / 2 - MARGIN), (W / 2 + MARGIN, -D / 2 - MARGIN), (W / 2 + MARGIN, D / 2 + MARGIN), (-W / 2 - MARGIN, D / 2 + MARGIN)], weight=1.0, edge=0.5)

    # ---------------------------------------------------------------- the two mazes and the stairs
    nodes0 = [(a, b) for a in range(NN) for b in range(NN)]
    # UG2: medium rooms (3 x 3 cells like the chambers, a doorway on every side), never next to another room
    stair_nodes = {(a, bb) for a0, bb in STAIRS for a in (a0, a0 + 1)}
    kinds = list(MEDIUM)
    medium = [dict(), dict()]

    def free(n, taken):
        return all(max(abs(n[0] - t[0]), abs(n[1] - t[1])) >= 2 for t in taken)
    cand0 = [(a, b2) for a in range(1, NN - 1) for b2 in range(1, NN - 1) if (a, b2) not in stair_nodes]
    R.shuffle(cand0)
    for n in cand0:
        if len(medium[0]) < N_MEDIUM0 and free(n, list(CHAMBERS) + list(medium[0])):
            medium[0][n] = kinds[len(medium[0]) % len(kinds)]
    cand1 = [(a, b2) for a in range(9, 12) for b2 in range(9, 12)]
    R.shuffle(cand1)
    for n in cand1:
        if len(medium[1]) < N_MEDIUM1 and free(n, list(medium[1])):
            medium[1][n] = kinds[(len(medium[1]) + 3) % len(kinds)]
    forced0, forced1 = set(), set()
    for rooms_, forced in ((list(CHAMBERS) + list(medium[0]), forced0), (list(medium[1]), forced1)):
        for (a, b2) in rooms_:   # a chamber / medium room has all four portals: its four links are always in
            for d in DIRS.values():
                forced.add(frozenset(((a, b2), (a + d[0], b2 + d[1]))))
    banned0 = {frozenset(((7, bb), (8, bb))) for _, bb in STAIRS}   # the stair cells
    tree0, loop0 = maze(nodes0, R, 0.33, forced0, banned0)
    tree1, loop1 = maze(L1, R, 0.3, forced1)
    edges0, edges1 = tree0 | loop0, tree1 | loop1
    assert components(nodes0, edges0) == 1 and components(L1, edges1) == 1
    rep = {"nodes": len(nodes0) + len(L1), "links": len(edges0) + len(edges1), "loops": len(loop0) + len(loop1)}

    # cells per level: (i, j) -> open sides; chamber footprints; stair cells
    cells = [dict(), dict()]
    room_cells = [set(), set()]
    for lv, rooms_ in ((0, list(CHAMBERS) + list(medium[0])), (1, list(medium[1]))):
        for (a, b2) in rooms_:
            ci, cj = node_cell((a, b2))
            for di in (-1, 0, 1):
                for dj in (-1, 0, 1):
                    room_cells[lv].add((ci + di, cj + dj))
    for lv, edges in ((0, edges0), (1, edges1)):
        for e in edges:
            p, q = sorted(e)
            s = side_to(p, q)
            pc, qc = node_cell(p), node_cell(q)
            mid = ((pc[0] + qc[0]) // 2, (pc[1] + qc[1]) // 2)
            for c, sd in ((pc, s), (qc, OPP[s]), (mid, s), (mid, OPP[s])):
                if c in room_cells[lv]:
                    continue
                cells[lv].setdefault(c, set()).add(sd)
    for (a, bb) in STAIRS:   # the stair drift's low portal opens east from node (7, b), its high one west into (8, b)
        cells[0].setdefault(node_cell((a, bb)), set()).add("E")
        cells[1].setdefault(node_cell((a + 1, bb)), set()).add("W")

    # ---------------------------------------------------------------- pieces
    loops_cells = set()
    for lv, loops in ((0, loop0), (1, loop1)):
        for e in loops:
            p, q = sorted(e)
            pc, qc = node_cell(p), node_cell(q)
            loops_cells.add((lv, ((pc[0] + qc[0]) // 2, (pc[1] + qc[1]) // 2)))
    placed = []   # (lv, (i, j), asset, yaw, ceiling height)
    n_crawl = n_breach = n_small = 0
    for lv in (0, 1):
        y = L1Y * lv
        for (i, j), sides in sorted(cells[lv].items()):
            shape, yaw = fit(sorted(sides))
            is_node = i % 2 == 1 and j % 2 == 1
            if shape == "straight":
                u = R.random()
                if is_node and u < 0.4:
                    aid = "tun_room_s_straight"
                elif not is_node and (lv, (i, j)) in loops_cells and u < 0.25:
                    aid = "tun_crawl"
                    n_crawl += 1
                elif not is_node and (lv, (i, j)) in loops_cells and u < 0.6:
                    aid = "tun_breach"   # UG2: a loop link blown through the rock
                    n_breach += 1
                else:
                    aid = R.choices(["tun_straight", "tun_straight_kink", "tun_straight_niche", "tun_straight_lined", "tun_wide", "tun_straight_shelled"], [25, 22, 13, 10, 12, 18])[0]
                yaw = yaw + (180.0 if R.random() < 0.5 else 0.0)   # a straight reads either way
            elif is_node and R.random() < 0.4:
                aid = "tun_room_s_" + shape   # UG2: a small room at the junction
            elif shape == "corner":
                aid = "tun_corner_lined" if R.random() < 0.3 else "tun_corner"
            elif shape in ("tee", "cross") and R.random() < 0.35:
                aid = "tun_%s_shelled" % shape
            else:
                aid = "tun_" + shape
            if aid.startswith("tun_room_s_"):
                n_small += 1
            x, z = cc(i, j)
            e = b.place(aid, x, z, yaw, check=False, cls="none", y=y, tags={"tunnel", "level%d" % lv})
            if e:
                placed.append((lv, (i, j), aid, yaw, CEIL.get(aid, 2.75), shape))
    for (a, bb) in STAIRS:
        x, z = cc(2 * a + 2, 2 * bb + 1)
        b.place("tun_stairs", x, z, 90.0, check=False, cls="none", y=0.0, tags={"tunnel", "stairs"})
    mediums = []
    for lv in (0, 1):
        for (a, b2), kind in sorted(medium[lv].items()):
            x, z = cc(*node_cell((a, b2)))
            e = b.place("tun_room_m_" + kind, x, z, 90.0 * R.randrange(4), check=False, cls="none", y=L1Y * lv, tags={"tunnel", "room"})
            if e:
                mediums.append((lv, kind, e))
    chambers = {}
    for (a, b2), kind in CHAMBERS.items():
        x, z = cc(*node_cell((a, b2)))
        chambers[kind] = b.place("tun_chamber_" + kind, x, z, 90.0 * R.randrange(4), check=False, cls="none", y=0.0, tags={"tunnel", "chamber"})

    # ---------------------------------------------------------------- lamps: sparse, failing, dark zones, red at junctions
    dark = [(R.uniform(-80, 80), R.uniform(-80, 80)) for _ in range(4)]
    red_cells = []
    lamps = {"tun_lamp": 0, "tun_lamp_dead": 0, "tun_lamp_red": 0, "tun_lamp_shadow": 0}
    for lv, (i, j), aid, yaw, h, shape in placed:
        if aid == "tun_crawl":
            continue
        x, z = cc(i, j)
        in_dark = any(math.hypot(x - dx, z - dz) < 26 for dx, dz in dark)
        if shape in ("tee", "cross") and R.random() < 0.28:
            kind = "tun_lamp_red"
        elif R.random() < (0.05 if in_dark else 0.6 if aid.startswith("tun_room_s_") else 0.33):
            kind = R.choices(["tun_lamp", "tun_lamp_dead"], [85, 15])[0]   # UG2: no flickering lamps
        else:
            continue
        b.place(kind, x, z, R.uniform(0, 360), check=False, cls="none", y=L1Y * lv + h, tags={"lamp"})
        lamps[kind] += 1
        if kind == "tun_lamp_red" and lv == 0 and shape == "cross":
            red_cells.append((i, j))
    for kind, e in chambers.items():
        cx, cz = e["pos"][0], e["pos"][2]
        for k, (ox, oz) in enumerate([(-4.0, -4.0), (4.0, 4.0), (4.0, -4.0)]):
            lk = "tun_lamp_shadow" if k == 0 else "tun_lamp"
            b.place(lk, cx + ox, cz + oz, 0.0, check=False, cls="none", y=CH_H, tags={"lamp"})
            lamps[lk] += 1
    for lv, kind, e in mediums:   # two lamps in a medium room, one of them dead now and then
        hw, h = MEDIUM[kind]
        for ox, oz in ((-hw * 0.45, -hw * 0.3), (hw * 0.45, hw * 0.3)):
            lk = "tun_lamp_dead" if R.random() < 0.15 else "tun_lamp"
            b.place(lk, e["pos"][0] + ox, e["pos"][2] + oz, 0.0, check=False, cls="none", y=L1Y * lv + h, tags={"lamp"})
            lamps[lk] += 1
    rep["lamps"] = lamps
    rep["lights"] = sum(v for k, v in lamps.items() if k != "tun_lamp_dead")

    # ---------------------------------------------------------------- props: chambers by kind, cover at junctions
    CH_PROPS = {
        "store": [("furn_crate", 4), ("furn_mil_crates", 2), ("furn_barrel_rack", 1), ("crate_mixed_supply", 1)],
        "bunks": [("furn_mil_bunk", 4), ("furn_mil_locker", 2), ("furn_mil_table", 1)],
        "generator": [("furn_mil_generator", 1), ("furn_mil_jerrycans", 2), ("drs_barrels", 1), ("furn_barrel", 3)],
        "sump": [("furn_barrel", 3), ("def_sandbag_straight", 2)],
        "collapse": [("rub_stone_pile_m", 3), ("rub_stone_pile_s", 4)],
    }
    spots = [(-7.0, 3.2), (-3.2, 7.0), (3.2, 7.0), (7.0, 3.2), (7.0, -3.2), (3.2, -7.0), (-3.2, -7.0), (-7.0, -3.2), (-6.6, 6.6), (6.6, 6.6), (6.6, -6.6), (-6.6, -6.6)]
    for kind, e in chambers.items():
        cx, cz = e["pos"][0], e["pos"][2]
        sp = spots[:]
        R.shuffle(sp)
        for aid, n in CH_PROPS[kind]:
            for _ in range(n):
                if not sp or aid not in b.cat:
                    continue
                ox, oz = sp.pop()
                b.place(aid, cx + ox, cz + oz, look_yaw(ox, oz) + R.uniform(-15, 15), check=False, cls="none", y=0.0, tags={"prop", "cover"})
    M_PROPS = {
        "office": [("furn_mil_table", 2), ("furn_mil_locker", 2), ("furn_crate", 1)],
        "workshop": [("furn_mil_table", 1), ("furn_barrel", 2), ("furn_mil_crates", 1), ("furn_crate", 2)],
        "magazine": [("furn_mil_crates", 3), ("furn_crate", 3)],
        "dugout": [("furn_mil_bunk", 2), ("rub_stone_pile_s", 2), ("furn_barrel", 1)],
        "pump": [("furn_barrel", 2), ("drs_barrels", 1)],
        "mess": [("furn_mil_table", 3), ("furn_mil_jerrycans", 1), ("furn_barrel_rack", 1)],
    }
    for lv, kind, e in mediums:
        hw, h = MEDIUM[kind]
        cx, cz = e["pos"][0], e["pos"][2]
        sp = [(sx * (hw - 1.1), sz * (hw - 1.1) * f) for sx in (-1, 1) for sz in (-1, 1) for f in (0.35, 1.0)] + [(sx * 2.0, sz * (hw - 1.1)) for sx in (-1, 1) for sz in (-1, 1)]
        sp = [p for p in sp if abs(p[0]) > PORTAL_CLEAR or abs(p[1]) > PORTAL_CLEAR]
        R.shuffle(sp)
        for aid, n in M_PROPS[kind]:
            for _ in range(n):
                if sp and aid in b.cat:
                    ox, oz = sp.pop()
                    b.place(aid, cx + ox, cz + oz, look_yaw(-ox, -oz) + R.uniform(-15, 15), check=False, cls="none", y=L1Y * lv, tags={"prop", "cover"})
    for lv, (i, j), aid, yaw, h, shape in placed:   # small rooms: a crate or a barrel in a corner
        if aid.startswith("tun_room_s_") and R.random() < 0.75:
            x, z = cc(i, j)
            ox, oz = R.choice([(1.5, 1.5), (-1.5, 1.5), (1.5, -1.5), (-1.5, -1.5)])
            b.place(R.choice(["furn_crate", "furn_barrel", "furn_mil_crates", "furn_mil_table"]), x + ox, z + oz, R.uniform(0, 360), check=False, cls="none", y=L1Y * lv, tags={"prop", "cover"})
    n_cover = 0
    for lv, (i, j), aid, yaw, h, shape in placed:
        if shape in ("tee", "cross") or aid == "tun_wide":
            if R.random() < 0.4:
                x, z = cc(i, j)
                ox, oz = R.choice([(1.15, 1.15), (-1.15, 1.15), (1.15, -1.15), (-1.15, -1.15)])
                pid = R.choice(["furn_barrel", "furn_crate", "furn_barrel", "def_sandbag_straight"])
                if b.place(pid, x + ox, z + oz, R.uniform(0, 360), check=False, cls="none", y=L1Y * lv, tags={"prop", "cover"}):
                    n_cover += 1
    rep["coverProps"] = n_cover

    # ---------------------------------------------------------------- scenario
    def deg(n):
        return sum(1 for e in edges0 if n in e)
    west = [n for n in nodes0 if n[0] == 0 and deg(n) >= 2 and 3 <= n[1] <= 9]
    east = [n for n in nodes0 if n[0] == 12 and deg(n) >= 2 and 1 <= n[1] <= 7]
    R.shuffle(west)
    R.shuffle(east)
    sc = ScenarioBuilder(b, defending="Axis")
    order = [("generator", "Generator room"), ("sump", "Flooded sump"), ("collapse", "The collapse")]
    prev = cc(*node_cell(west[0]))
    for kind, label in order:
        e = chambers[kind]
        sc.objective(label, e["pos"][0], e["pos"][2], radius=10.0, capture_time=45.0, attack_from=prev, linked=[e["id"]])
        prev = (e["pos"][0], e["pos"][2])
    for k, n in enumerate(west[:3]):
        x, z = cc(*node_cell(n))
        sc.spawn("attacker", [0, 1, 2], x, z, label="Allies - west galleries %d" % (k + 1), face=(x + 10, z), rack=None)
    for k, n in enumerate(east[:3]):
        x, z = cc(*node_cell(n))
        sc.spawn("defender", [0, 1, 2], x, z, label="Axis - east galleries %d" % (k + 1), face=(x - 10, z), rack=None)
    for team, n, fac, lab in ((0, east[3] if len(east) > 3 else east[0], "Axis", "Team A spawn (Axis)"), (1, west[3] if len(west) > 3 else west[0], "Allies", "Team B spawn (Allies)")):
        x, z = cc(*node_cell(n))
        m = sc.spawn("defender" if team == 0 else "attacker", [0], x, z, face=(0.0, z), faction=fac, label=lab, rack=None)
        m["team"], m["phases"] = team, []
    sc.rules["teamFaction"] = ["Axis", "Allies"]
    for m in sc.spawns:   # a supply crate a few metres along a passage from every spawn
        x, z = m["pos"][0], m["pos"][2]
        i, j = int((x + W / 2) // CELL), int((z + D / 2) // CELL)
        sides = cells[0].get((i, j), set())
        if sides:
            dx, dz = DIRS[sorted(sides)[0]]
            b.place("crate_mixed_supply", x + dx * 2.2 + dz * 0.8, z + dz * 2.2 - dx * 0.8, look_yaw(dx, dz), check=False, cls="none", y=0.0, tags={"supply"})
    err, warn = sc.validate()

    # ---------------------------------------------------------------- report numbers
    n_pieces = len(placed) + len(STAIRS) + len(CHAMBERS)
    n_medium = len(mediums)
    n_pieces += n_medium
    n_corr = len(placed) - n_small
    rep.update({"pieces": n_pieces, "tunnelLength_m": round(n_corr * CELL + len(STAIRS) * CELL), "chambers": len(CHAMBERS), "mediumRooms": n_medium,
                "smallRooms": n_small, "levels": 2, "crawlSqueezes": n_crawl, "breaches": n_breach, "level1Pieces": sum(1 for p in placed if p[0] == 1)})

    # ---------------------------------------------------------------- output
    tdir = os.path.join(ctx.repo, "content", "templates")
    grid.write_png(os.path.join(tdir, TID + ".height.png"))
    a1 = sc.spawns[0]["pos"]
    gen = chambers["generator"]["pos"]
    cor = next(cc(i, j) for lv, (i, j), aid, yaw, h, shape in placed if lv == 0 and aid == "tun_straight" and abs(cc(i, j)[0]) < 40)
    eye = 1.65
    cam = ([0.0, 150.0, 170.0], 0.0, -45.0)
    jx, jz = cc(*red_cells[0])   # the map is underground and at night: the thumbnail looks down a gallery at a red-lit crossing
    thumb_cam = ([round(jx - 6.5, 2), 1.65, round(jz + 0.3, 2)], look_yaw(1.0, 0.0), -4.0)
    views = [
        {"name": "overview_high", "pos": [0.0, 170.0, 150.0], "yaw": 0.0, "pitch": -50.0},
        {"name": "corridor_eye", "pos": [round(cor[0], 2), eye, round(cor[1] - 3.0, 2)], "yaw": look_yaw(0.0, 1.0), "pitch": 0.0, "gameplay": True},
        {"name": "generator_eye", "pos": [round(gen[0] - 6.0, 2), eye, round(gen[2] - 6.0, 2)], "yaw": look_yaw(1.0, 1.0), "pitch": 0.0, "gameplay": True},
    ]
    write_map(os.path.join(tdir, TID + ".nmap.json"), name=NAME, template_id=TID, template_version=VERSION, seed=SEED, width=W, depth=D,
              builder=b, scenario_json=sc.to_json(), explore_start=((a1[0], a1[1], a1[2]), look_yaw(1.0, 0.0)), camera=cam,
              bookmarks={str(i + 1): {"position": [o["pos"][0], 40.0, o["pos"][2] + 30], "yaw": 0.0, "pitch": -50} for i, o in enumerate(sc.objectives)},
              extra={"groundPaint": paint.to_json(), "lighting": {"hour": 0.0}})
    write_template(os.path.join(tdir, TID + ".json"), tid=TID, name=NAME, order=7, version=VERSION, seed=SEED,
                   description="An underground labyrinth, 216 x 216 m: winding rock and concrete galleries on two levels joined by "
                               "stair drifts, junctions, squeezes, and five chambers (storage, bunk room, generator room, a flooded "
                               "sump, a collapse). Sparse caged lamps, many of them failing, dark stretches and red emergency lamps. "
                               "Assault (the Allies push in from the west galleries: generator room, sump, collapse), Team "
                               "Deathmatch and Solo.",
                   width=W, depth=D, margin=MARGIN, heightmap=grid.meta(TID + ".height.png"), water_level=-1000.0,
                   map_file=TID + ".nmap.json", camera=cam, thumbnail=TID + ".thumb.png", thumb_camera=thumb_cam,
                   extra={"userMap": True, "views": views})
    use = asset_usage(b)
    update_usage_doc(os.path.join(ctx.repo, "docs", "asset-usage-default-maps.md"), use, "Tunnels",
                     "- **Tunnels:** *Tunnels* (UG1, `tools/mapgen/maps/tunnels.py`): placed counts; \"not used\" = not on this map.")
    rep.update({"instances": len(b.inst), "errors": err, "warnings": warn, "use": dict(sorted(use.items())),
                "objectives": [(o["label"], o["displayName"], o["pos"]) for o in sc.objectives], "spawns": [(m["label"], m["pos"]) for m in sc.spawns]})
    write_summary(os.path.join(ctx.out, TID + ".summary.json"), rep)
    print("instances %d | pieces %d, corridors %d m, rooms: %d chambers (24 m), %d medium (10-14 m), %d small (4-5 m), levels 2, loops %d, squeezes %d, breaches %d | lights %d (%s) | errors %s | warnings %d" %
          (len(b.inst), n_pieces, rep["tunnelLength_m"], len(CHAMBERS), n_medium, n_small, rep["loops"], n_crawl, n_breach, rep["lights"], lamps, err, len(warn)))
