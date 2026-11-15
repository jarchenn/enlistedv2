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
                    asset_usage, rng, look_yaw, local_to_world)

TID, NAME, VERSION, SEED = "tunnels", "Tunnels", 3, 19440605
CELL = 8.0
NC = 21                          # UG3: 21 cells of 8 m (168 m) centred in a 176 x 176 m map (2/3 of the UG2 area)
W = D = 176.0
OFF = (W - NC * CELL) / 2
MARGIN = 60
NN = 10                          # node lattice: node (a, b) is cell (2a + 1, 2b + 1)
STEP = 1.2                       # UG3: node heights in steps of 1.2 m; a link climbs by ramp (1.2 / 2.4 m) or stairs (3.6 m)
CHAMBERS = {(5, 5): "generator", (7, 2): "sump", (8, 8): "collapse", (2, 2): "store", (5, 8): "shaft", (8, 5): "shaft"}
DIRS = {"N": (0, 1), "E": (1, 0), "S": (0, -1), "W": (-1, 0)}
OPP = {"N": "S", "S": "N", "E": "W", "W": "E"}
ROT = {"N": "E", "E": "S", "S": "W", "W": "N"}   # an instance yaw of +90 deg turns the asset's sides this way
BASE = {"end": ["S"], "straight": ["N", "S"], "corner": ["N", "E"], "tee": ["N", "E", "W"], "cross": ["N", "E", "S", "W"]}
MEDIUM = {"office": (5.0, 2.9), "workshop": (6.0, 3.0), "magazine": (5.5, 2.9), "dugout": (6.5, 3.0), "pump": (5.5, 2.9), "mess": (7.0, 3.0)}   # UG2: half size, height
N_MEDIUM = 8                     # UG3: medium rooms
CEIL = {"tun_straight_shelled": 2.75, "tun_breach": 2.9, "tun_cross_shelled": 2.9, "tun_tee_shelled": 2.9, "tun_room_s_end": 2.75, "tun_room_s_straight": 2.75,
        "tun_room_s_corner": 2.75, "tun_room_s_tee": 2.75, "tun_room_s_cross": 2.75, "tun_end": 2.75, "tun_straight": 2.75, "tun_straight_kink": 2.75, "tun_straight_niche": 2.75, "tun_straight_lined": 2.75,
        "tun_ramp_12": 2.75, "tun_ramp_24": 2.75, "tun_straight_humped": 2.75, "tun_straight_timbered": 2.75, "tun_straight_meshed": 2.75,
        "tun_wide": 3.0, "tun_corner": 2.75, "tun_corner_lined": 2.75, "tun_tee": 2.9, "tun_cross": 2.9}
CH_H = 3.3
PORTAL_CLEAR = 1.6   # keep props this far off the axes of a room's doorways


def cc(i, j):
    """Cell centre (world x, z)."""
    return (-W / 2 + OFF + (i + 0.5) * CELL, -D / 2 + OFF + (j + 0.5) * CELL)


def ayaw(dx, dz):
    """Instance yaw that turns an asset's local +z to world (dx, dz)."""
    return math.degrees(math.atan2(dx, dz)) % 360.0


def fit_to(sides, base):
    """Yaw turning the side list `base` into the set `sides` (None if no quarter turn does)."""
    want, cur = set(sides), set(base)
    for k in range(4):
        if cur == want:
            return 90.0 * k
        cur = {ROT[x] for x in cur}
    return None


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
    grid = Grid(W, D, margin=MARGIN, base=-30.0)   # UG3: the terrain far under the deepest pit (the passages are closed shells)
    grid.quantise()
    b = MapBuilder(ctx.catalog, grid, frame, seed=SEED)
    paint = GroundPaint()
    paint.poly("mud", [(-W / 2 - MARGIN, -D / 2 - MARGIN), (W / 2 + MARGIN, -D / 2 - MARGIN), (W / 2 + MARGIN, D / 2 + MARGIN), (-W / 2 - MARGIN, D / 2 + MARGIN)], weight=1.0, edge=0.5)
    nodes = [(a, c) for a in range(NN) for c in range(NN)]
    rep = {}

    # ---------------------------------------------------------------- rooms: chambers (fixed) and medium rooms
    kinds = list(MEDIUM)
    medium = {}

    def free(n, taken):
        return all(max(abs(n[0] - t[0]), abs(n[1] - t[1])) >= 2 and abs(n[0] - t[0]) + abs(n[1] - t[1]) >= 3 for t in taken)   # no shared doorway neighbour
    cand = [(a, c) for a in range(1, NN - 1) for c in range(1, NN - 1)]
    R.shuffle(cand)
    for n in cand:
        if len(medium) < N_MEDIUM and free(n, list(CHAMBERS) + list(medium)):
            medium[n] = kinds[len(medium) % len(kinds)]
    rooms = list(CHAMBERS) + list(medium)

    # ---------------------------------------------------------------- heights: a smooth field quantised to 1.2 m, rooms level with their doorways
    ph = [R.uniform(0, 2 * math.pi) for _ in range(3)]
    H = {n: round((4.6 * math.sin(0.62 * n[0] + ph[0]) + 3.8 * math.cos(0.55 * n[1] + ph[1]) + 2.0 * math.sin(0.45 * (n[0] + n[1]) + ph[2])) / STEP) * STEP for n in nodes}
    locked = set()
    for n in rooms:
        locked.add(n)
        for d in DIRS.values():
            m = (n[0] + d[0], n[1] + d[1])
            if m in H:
                H[m] = H[n]
                locked.add(m)
    for _ in range(80):   # links climb by ramps (at most 2.4 m) where it can be helped; stairs only next to rooms
        moved = False
        for p in nodes:
            for d in DIRS.values():
                q = (p[0] + d[0], p[1] + d[1])
                if q in H and q not in locked and abs(H[q] - H[p]) > 2 * STEP + 1e-6:
                    H[q] = H[p] + math.copysign(2 * STEP, H[q] - H[p])
                    moved = True
        if not moved:
            break
    lo = min(H.values())
    H = {n: round(h - lo, 3) for n, h in H.items()}

    # ---------------------------------------------------------------- the maze (links steeper than stairs are never made)
    forced, banned = set(), set()
    for n in rooms:
        for d in DIRS.values():
            forced.add(frozenset((n, (n[0] + d[0], n[1] + d[1]))))
    for p in nodes:
        for d in DIRS.values():
            q = (p[0] + d[0], p[1] + d[1])
            if q in H and abs(H[q] - H[p]) > 3 * STEP + 1e-6:
                banned.add(frozenset((p, q)))
    tree, loops = maze(nodes, R, 0.33, forced - banned, banned)
    edges = tree | loops
    assert components(nodes, edges) == 1
    rep.update({"nodes": len(nodes), "links": len(edges), "loops": len(loops)})

    room_cells = set()
    for n in rooms:
        ci, cj = node_cell(n)
        room_cells |= {(ci + di, cj + dj) for di in (-1, 0, 1) for dj in (-1, 0, 1)}
    node_sides = {}
    link = {}   # mid cell -> (low node, high node, rise)
    for e in edges:
        p, q = sorted(e)
        s = side_to(p, q)
        pc, qc = node_cell(p), node_cell(q)
        mid = ((pc[0] + qc[0]) // 2, (pc[1] + qc[1]) // 2)
        for c, sd in ((pc, s), (qc, OPP[s])):
            if c not in room_cells:
                node_sides.setdefault(c, set()).add(sd)
        if mid not in room_cells:
            lo_n, hi_n = (p, q) if H[p] <= H[q] else (q, p)
            link[mid] = (lo_n, hi_n, round(H[hi_n] - H[lo_n], 3), s)
    cell_y = {}

    # ---------------------------------------------------------------- pieces
    placed = []   # (cell, asset, yaw, y, ceiling, shape, rise)
    n_crawl = n_breach = n_small = n_ramp = n_stairs = 0
    loop_mids = set()
    for e in loops:
        p, q = sorted(e)
        pc, qc = node_cell(p), node_cell(q)
        loop_mids.add(((pc[0] + qc[0]) // 2, (pc[1] + qc[1]) // 2))
    for (i, j), sides in sorted(node_sides.items()):
        n = ((i - 1) // 2, (j - 1) // 2)
        shape, yaw = fit(sorted(sides))
        if R.random() < 0.4:
            aid = "tun_room_s_" + shape
            n_small += 1
        elif shape == "straight":
            aid = R.choice(["tun_straight", "tun_straight_humped", "tun_straight_meshed"])
        elif shape == "corner":
            aid = "tun_corner_lined" if R.random() < 0.3 else "tun_corner"
        elif shape in ("tee", "cross") and R.random() < 0.35:
            aid = "tun_%s_shelled" % shape
        else:
            aid = "tun_" + shape
        x, z = cc(i, j)
        b.place(aid, x, z, yaw, check=False, cls="none", y=H[n], tags={"tunnel"})
        cell_y[(i, j)] = H[n]
        placed.append(((i, j), aid, yaw, H[n], CEIL.get(aid, 2.75), shape, 0.0))
    for (i, j), (lo_n, hi_n, rise, s) in sorted(link.items()):
        x, z = cc(i, j)
        d = (hi_n[0] - lo_n[0], hi_n[1] - lo_n[1])
        if rise < 0.01:
            u = R.random()
            if (i, j) in loop_mids and u < 0.2:
                aid = "tun_crawl"
                n_crawl += 1
            elif (i, j) in loop_mids and u < 0.5:
                aid = "tun_breach"
                n_breach += 1
            else:
                aid = R.choices(["tun_straight", "tun_straight_kink", "tun_straight_niche", "tun_straight_lined", "tun_wide", "tun_straight_shelled",
                                 "tun_straight_humped", "tun_straight_timbered", "tun_straight_meshed"], [14, 14, 8, 6, 8, 10, 16, 14, 10])[0]
            yaw = ayaw(*d) + (180.0 if R.random() < 0.5 else 0.0)
        elif rise < 3 * STEP - 0.01:
            aid = "tun_ramp_12" if rise < 2 * STEP - 0.01 else "tun_ramp_24"
            yaw = ayaw(*d)
            n_ramp += 1
        else:
            aid = "tun_stairs"
            yaw = ayaw(*d)
            n_stairs += 1
        y = H[lo_n]
        b.place(aid, x, z, yaw % 360.0, check=False, cls="none", y=y, tags={"tunnel"})
        cell_y[(i, j)] = y
        placed.append(((i, j), aid, yaw % 360.0, y, CEIL.get(aid, 2.75 if aid != "tun_stairs" else 2.6), "straight", rise))
    mediums = []
    for n, kind in sorted(medium.items()):
        x, z = cc(*node_cell(n))
        e = b.place("tun_room_m_" + kind, x, z, 90.0 * R.randrange(4), check=False, cls="none", y=H[n], tags={"tunnel", "room"})
        mediums.append((n, kind, e))
    chambers = []
    for n, kind in CHAMBERS.items():
        x, z = cc(*node_cell(n))
        e = b.place("tun_chamber_" + kind, x, z, 0.0 if kind in ("shaft", "sump") else 90.0 * R.randrange(4), check=False, cls="none", y=H[n], tags={"tunnel", "chamber"})
        chambers.append((n, kind, e))
    relief = max(H.values()) - min(H.values())
    rep.update({"nodeRelief_m": relief, "totalRelief_m": relief + 10.0, "ramps": n_ramp, "stairs": n_stairs})

    # ---------------------------------------------------------------- rail: the longest stair-free haulage route west -> east, a siding, carts
    adj = {n: [] for n in nodes}
    for e in edges:
        p, q = tuple(e)
        if abs(H[p] - H[q]) < 3 * STEP - 0.01 and p not in rooms and q not in rooms:
            adj[p].append(q)
            adj[q].append(p)

    def bfs(src):
        prev, dist, todo = {src: None}, {src: 0}, [src]
        for u in todo:
            for v in adj[u]:
                if v not in dist:
                    dist[v], prev[v] = dist[u] + 1, u
                    todo.append(v)
        return prev, dist
    best = None   # the longest stair-free route that avoids the rooms (the haulage line)
    for w in [n for n in nodes if n not in rooms]:
        prev, dist = bfs(w)
        for t, dd in dist.items():
            if best is None or dd > best[0]:
                best = (dd, w, t, prev)
    route = []
    if best:
        _, w, t, prev = best
        while t is not None:
            route.append(t)
            t = prev[t]
        route.reverse()
    track = {}   # cell -> (asset, yaw, y)
    for k, n in enumerate(route):
        c = node_cell(n)
        sd = set()
        if k > 0:
            sd.add(side_to(n, route[k - 1]))
        if k + 1 < len(route):
            sd.add(side_to(n, route[k + 1]))
        if len(sd) == 1:
            track[c] = ("tun_track_buffer", fit_to(sd, ["S"]), H[n])
        elif fit_to(sd, ["N", "S"]) is not None:
            track[c] = ("tun_track_straight", fit_to(sd, ["N", "S"]), H[n])
        else:
            track[c] = ("tun_track_curve", fit_to(sd, ["S", "E"]), H[n])
        if k + 1 < len(route):
            q = route[k + 1]
            pc, qc = node_cell(n), node_cell(q)
            mid = ((pc[0] + qc[0]) // 2, (pc[1] + qc[1]) // 2)
            lo_n, hi_n, rise, _ = link[mid]
            d = (hi_n[0] - lo_n[0], hi_n[1] - lo_n[1])
            if rise < 0.01:
                track[mid] = ("tun_track_straight", ayaw(*d), H[lo_n])
            else:
                track[mid] = ("tun_track_ramp_12" if rise < 2 * STEP - 0.01 else "tun_track_ramp_24", ayaw(*d), H[lo_n])
    # a siding: points where the route runs straight past a level side link to a node off the route
    siding = None
    for k in range(1, len(route) - 1):
        n = route[k]
        if track[node_cell(n)][0] != "tun_track_straight":
            continue
        for q in adj[n]:
            if q in route:
                continue
            ins = {side_to(n, route[k - 1]), side_to(n, route[k + 1])}
            branch = side_to(n, q)
            for yw in (0.0, 90.0, 180.0, 270.0):   # points: straight S-N and a curve S-E
                base = {"S": "S", "N": "N", "E": "E"}
                turned = {}
                for key in ("S", "N", "E"):
                    v = key
                    for _ in range(int(yw // 90)):
                        v = ROT[v]
                    turned[key] = v
                if {turned["S"], turned["N"]} == ins and turned["E"] == branch:
                    pc, qc = node_cell(n), node_cell(q)
                    mid = ((pc[0] + qc[0]) // 2, (pc[1] + qc[1]) // 2)
                    track[pc] = ("tun_track_points", yw, H[n])
                    lo_n, hi_n, rise, _ = link[mid]
                    dd = (hi_n[0] - lo_n[0], hi_n[1] - lo_n[1])
                    track[mid] = ("tun_track_straight" if rise < 0.01 else "tun_track_ramp_12" if rise < 2 * STEP - 0.01 else "tun_track_ramp_24", ayaw(*dd), H[lo_n])
                    track[qc] = ("tun_track_buffer", fit_to({OPP[branch]}, ["S"]), H[q])
                    siding = (n, q)
                    break
            if siding:
                break
        if siding:
            break
    for c, (aid, yw, y) in track.items():
        x, z = cc(*c)
        b.place(aid, x, z, yw, check=False, cls="none", y=y, tags={"rail"})
    carts = {"tun_cart": 0, "tun_cart_ore": 0, "tun_cart_tipped": 0, "tun_cart_derailed": 0}
    piece_at = {c: (aid, shape) for c, aid, yaw, y, h, shape, rise in placed}
    route_nodes = [n for n in route[1:-1] if piece_at.get(node_cell(n), ("", ""))[1] in ("tee", "cross") or piece_at.get(node_cell(n), ("", ""))[0].startswith("tun_room_s_")]
    R.shuffle(route_nodes)
    for k, n in enumerate(route_nodes[:8]):   # carts stand in the wide junction hubs on the route (room to pass on either side)
        c = node_cell(n)
        aid, yw, y = track[c]
        x, z = cc(*c)
        if aid == "tun_track_straight" and k % 3 != 2:
            kind = R.choice(["tun_cart", "tun_cart_ore", "tun_cart_ore"])
            b.place(kind, x, z, yw, check=False, cls="none", y=y, tags={"cart", "cover"})
        else:
            kind = R.choice(["tun_cart_tipped", "tun_cart_derailed"])
            b.place(kind, x, z, yw + R.uniform(-25, 25), check=False, cls="none", y=y, tags={"cart", "cover"})
        carts[kind] += 1
    if siding:   # a cart waiting at the buffer of the siding
        qc = node_cell(siding[1])
        x, z = cc(*qc)
        aid, yw, y = track[qc]
        b.place("tun_cart_ore", x, z, yw, check=False, cls="none", y=y, tags={"cart", "cover"})
        carts["tun_cart_ore"] += 1
    rep.update({"trackCells": len(track), "trackLength_m": len(track) * CELL, "siding": bool(siding), "carts": carts})

    # ---------------------------------------------------------------- lamps (none flicker, no shadowed lamps: swapping shadow slots read as flicker)
    dark = [(R.uniform(-60, 60), R.uniform(-60, 60)) for _ in range(3)]
    red_cells = []
    lamps = {"tun_lamp": 0, "tun_lamp_dead": 0, "tun_lamp_red": 0}
    for c, aid, yaw, y, h, shape, rise in placed:
        if aid in ("tun_crawl", "tun_stairs"):
            continue
        x, z = cc(*c)
        in_dark = any(math.hypot(x - dx, z - dz) < 20 for dx, dz in dark)
        if shape in ("tee", "cross") and R.random() < 0.28:
            kind = "tun_lamp_red"
        elif R.random() < (0.05 if in_dark else 0.6 if aid.startswith("tun_room_s_") else 0.38):
            kind = R.choices(["tun_lamp", "tun_lamp_dead"], [86, 14])[0]
        else:
            continue
        b.place(kind, x, z, R.uniform(0, 360), check=False, cls="none", y=y + h + rise / 2, tags={"lamp"})
        lamps[kind] += 1
        if kind == "tun_lamp_red" and shape == "cross":
            red_cells.append(c)
    for n, kind, e in chambers:
        cx, cz = e["pos"][0], e["pos"][2]
        for ox, oz in ([(-6.5, -6.5), (6.5, 6.5), (6.5, -6.5), (-6.5, 6.5)] if kind == "shaft" else [(-4.0, -4.0), (4.0, 4.0), (4.0, -4.0)]):
            b.place("tun_lamp", cx + ox, cz + oz, 0.0, check=False, cls="none", y=H[n] + CH_H, tags={"lamp"})
            lamps["tun_lamp"] += 1
    for n, kind, e in mediums:
        hw, h = MEDIUM[kind]
        for ox, oz in ((-hw * 0.45, -hw * 0.3), (hw * 0.45, hw * 0.3)):
            lk = "tun_lamp_dead" if R.random() < 0.12 else "tun_lamp"
            b.place(lk, e["pos"][0] + ox, e["pos"][2] + oz, 0.0, check=False, cls="none", y=H[n] + h, tags={"lamp"})
            lamps[lk] += 1
    rep["lamps"] = lamps
    rep["lights"] = sum(v for k, v in lamps.items() if k != "tun_lamp_dead")

    # ---------------------------------------------------------------- props: rooms by kind (with the mine kit), corridors
    CH_PROPS = {
        "store": [("tun_powder_crates", 2), ("tun_cable_reel", 2), ("furn_crate", 3), ("furn_mil_crates", 1), ("crate_mixed_supply", 1)],
        "bunks": [("furn_mil_bunk", 4), ("furn_mil_locker", 2), ("furn_mil_table", 1), ("tun_tool_rack", 1)],
        "generator": [("furn_mil_generator", 1), ("tun_compressor", 1), ("tun_fan", 1), ("furn_mil_jerrycans", 2), ("furn_barrel", 2)],
        "sump": [("furn_barrel", 2), ("tun_cable_reel", 1)],
        "collapse": [("tun_ore_pile", 3), ("rub_stone_pile_m", 2), ("tun_hopper", 1), ("tun_conveyor", 1)],
        "shaft": [("tun_warning_board", 2), ("tun_cable_reel", 1)],
    }
    spots = [(-7.0, 3.2), (-3.2, 7.0), (3.2, 7.0), (7.0, 3.2), (7.0, -3.2), (3.2, -7.0), (-3.2, -7.0), (-7.0, -3.2), (-6.6, 6.6), (6.6, 6.6), (6.6, -6.6), (-6.6, -6.6)]
    for n, kind, e in chambers:
        cx, cz = e["pos"][0], e["pos"][2]
        y = H[n]
        sp = [p for p in spots if kind not in ("shaft", "sump") or max(abs(p[0]), abs(p[1])) > 6.0]
        R.shuffle(sp)
        for aid, k in CH_PROPS[kind]:
            for _ in range(k):
                if sp and aid in b.cat:
                    ox, oz = sp.pop()
                    b.place(aid, cx + ox, cz + oz, ayaw(-ox, -oz) if aid in ("tun_conveyor",) else look_yaw(ox, oz) + R.uniform(-15, 15), check=False, cls="none", y=y, tags={"prop", "cover"})
        if kind == "shaft":   # the winch at the pit edge, ladders down the pit wall
            b.place("tun_winch", cx + 6.0, cz + 0.0, 90.0, check=False, cls="none", y=y, tags={"prop", "cover"})
            b.place("tun_ladder", cx + 4.45, cz - 2.0, 270.0, check=False, cls="none", y=y - 4.0, tags={"prop"})
            b.place("tun_ladder", cx + 4.45, cz - 2.0, 270.0, check=False, cls="none", y=y - 8.0, tags={"prop"})
        if kind == "sump":
            b.place("tun_pump", cx + 5.6, cz + 0.0, 90.0, check=False, cls="none", y=y, tags={"prop", "cover"})
    M_PROPS = {
        "office": [("furn_mil_table", 2), ("furn_mil_locker", 2), ("tun_cable_reel", 1)],
        "workshop": [("tun_tool_rack", 2), ("tun_drill_rig", 1), ("tun_compressor", 1), ("furn_mil_table", 1)],
        "magazine": [("tun_powder_crates", 3), ("furn_mil_crates", 2)],
        "dugout": [("furn_mil_bunk", 2), ("tun_ore_pile", 1), ("furn_barrel", 1)],
        "pump": [("tun_pump", 1), ("furn_barrel", 2)],
        "mess": [("furn_mil_table", 3), ("furn_mil_jerrycans", 1), ("tun_fan", 1)],
    }
    for n, kind, e in mediums:
        hw, h = MEDIUM[kind]
        cx, cz = e["pos"][0], e["pos"][2]
        sp = [(sx * (hw - 1.1), sz * (hw - 1.1) * f) for sx in (-1, 1) for sz in (-1, 1) for f in (0.35, 1.0)] + [(sx * 2.0, sz * (hw - 1.1)) for sx in (-1, 1) for sz in (-1, 1)]
        sp = [p for p in sp if abs(p[0]) > PORTAL_CLEAR or abs(p[1]) > PORTAL_CLEAR]
        R.shuffle(sp)
        for aid, k in M_PROPS[kind]:
            for _ in range(k):
                if sp and aid in b.cat:
                    ox, oz = sp.pop()
                    b.place(aid, cx + ox, cz + oz, look_yaw(-ox, -oz) + R.uniform(-15, 15), check=False, cls="none", y=H[n], tags={"prop", "cover"})
    n_duct = n_board = 0
    for c, aid, yaw, y, h, shape, rise in placed:
        x, z = cc(*c)
        if aid.startswith("tun_room_s_") and R.random() < 0.7:
            ox, oz = R.choice([(1.5, 1.5), (-1.5, 1.5), (1.5, -1.5), (-1.5, -1.5)])
            b.place(R.choice(["furn_crate", "tun_powder_crates", "tun_cable_reel", "furn_barrel", "tun_tool_rack"]), x + ox, z + oz, R.uniform(0, 360), check=False, cls="none", y=y, tags={"prop", "cover"})
        elif aid == "tun_end":
            b.place("tun_ore_pile", x, z, R.uniform(0, 360), check=False, cls="none", y=y, tags={"prop", "cover"})
        elif shape == "straight" and not rise and aid not in ("tun_crawl", "tun_stairs") and c not in track and R.random() < 0.22:
            lx, lz = local_to_world((x, z), yaw, -0.6, 0.0)   # a vent duct under the roof along the passage
            b.place("tun_duct", lx, lz, yaw, check=False, cls="none", y=y + h - 0.45, tags={"prop"})
            n_duct += 1
        elif shape in ("tee", "cross") and R.random() < 0.18:
            ox, oz = R.choice([(1.35, 1.35), (-1.35, 1.35), (1.35, -1.35), (-1.35, -1.35)])
            b.place("tun_warning_board", x + ox, z + oz, look_yaw(ox, oz), check=False, cls="none", y=y, tags={"prop"})
            n_board += 1
    rep.update({"ducts": n_duct, "warningBoards": n_board})

    # ---------------------------------------------------------------- scenario
    def deg(n):
        return sum(1 for e in edges if n in e)
    west = [n for n in nodes if n[0] == 0 and deg(n) >= 2 and n not in rooms]
    east = [n for n in nodes if n[0] == NN - 1 and deg(n) >= 2 and n not in rooms]
    R.shuffle(west)
    R.shuffle(east)
    sc = ScenarioBuilder(b, defending="Axis")
    order = [("generator", "Generator room"), ("sump", "Flooded sump"), ("collapse", "The collapse")]
    prev = cc(*node_cell(west[0]))
    obj_entities = {kind: e for n, kind, e in chambers}
    for kind, label in order:
        e = obj_entities[kind]
        sc.objective(label, e["pos"][0], e["pos"][2], radius=10.0, capture_time=45.0, attack_from=prev, linked=[e["id"]])
        sc.objectives[-1]["pos"][1] = e["pos"][1]
        prev = (e["pos"][0], e["pos"][2])
    spawn_nodes = []
    for k, n in enumerate(west[:3]):
        x, z = cc(*node_cell(n))
        m = sc.spawn("attacker", [0, 1, 2], x, z, label="Allies - west galleries %d" % (k + 1), face=(x + 10, z), rack=None)
        spawn_nodes.append((m, n))
    for k, n in enumerate(east[:3]):
        x, z = cc(*node_cell(n))
        m = sc.spawn("defender", [0, 1, 2], x, z, label="Axis - east galleries %d" % (k + 1), face=(x - 10, z), rack=None)
        spawn_nodes.append((m, n))
    for team, n, fac, lab in ((0, east[3] if len(east) > 3 else east[0], "Axis", "Team A spawn (Axis)"), (1, west[3] if len(west) > 3 else west[0], "Allies", "Team B spawn (Allies)")):
        x, z = cc(*node_cell(n))
        m = sc.spawn("defender" if team == 0 else "attacker", [0], x, z, face=(0.0, z), faction=fac, label=lab, rack=None)
        m["team"], m["phases"] = team, []
        spawn_nodes.append((m, n))
    sc.rules["teamFaction"] = ["Axis", "Allies"]
    for m, n in spawn_nodes:   # spawns on their node's floor; a supply crate a few metres along a passage
        m["pos"][1] = H[n]
        x, z = m["pos"][0], m["pos"][2]
        sides = node_sides.get(node_cell(n), set())
        if sides:
            dx, dz = DIRS[sorted(sides)[0]]
            b.place("crate_mixed_supply", x + dx * 2.2 + dz * 0.8, z + dz * 2.2 - dx * 0.8, look_yaw(dx, dz), check=False, cls="none", y=H[n], tags={"supply"})
    err, warn = sc.validate()

    # ---------------------------------------------------------------- report numbers
    n_corr = sum(1 for p in placed if not p[1].startswith("tun_room_s_"))
    rep.update({"size_m": W, "pieces": len(placed) + len(mediums) + len(chambers), "tunnelLength_m": round(n_corr * CELL), "chambers": len(chambers),
                "mediumRooms": len(mediums), "smallRooms": n_small, "crawlSqueezes": n_crawl, "breaches": n_breach})

    # ---------------------------------------------------------------- output (+ capture spots for --shots-backrooms with NMK_SHOTS_MAP)
    tdir = os.path.join(ctx.repo, "content", "templates")
    grid.write_png(os.path.join(tdir, TID + ".height.png"))
    a1 = sc.spawns[0]["pos"]
    gen = obj_entities["generator"]["pos"]
    eye = 1.65
    cams = {}
    ramp = next(((c, p) for c, p in track.items() if p[0].startswith("tun_track_ramp")), None)
    if ramp:
        c, (aid, yw, y) = ramp
        x, z = cc(*c)
        dx, dz = math.sin(math.radians(yw)), math.cos(math.radians(yw))
        cams["incline"] = [round(x - dx * 9.5, 2), round(y, 2), round(z - dz * 9.5, 2), round(x + dx * 4, 2), round(z + dz * 4, 2)]
    sh = next(e for n, kind, e in chambers if kind == "shaft")
    cams["shaft"] = [round(sh["pos"][0] + 6.5, 2), round(sh["pos"][1], 2), round(sh["pos"][2] - 6.5, 2), round(sh["pos"][0] - 2.0, 2), round(sh["pos"][2] + 2.0, 2)]
    ws = next((e for n, kind, e in mediums if kind == "workshop"), mediums[0][2])
    cams["room"] = [round(ws["pos"][0] - 4.5, 2), round(ws["pos"][1], 2), round(ws["pos"][2] - 4.5, 2), round(ws["pos"][0] + 3, 2), round(ws["pos"][2] + 3, 2)]
    rep["cams"] = cams
    cam = ([0.0, 150.0, 150.0], 0.0, -45.0)
    if ramp:
        tc = cams["incline"]
        thumb_cam = ([tc[0], tc[1] + 1.65, tc[2]], look_yaw(tc[3] - tc[0], tc[4] - tc[2]), 6.0)
    else:
        thumb_cam = ([a1[0], a1[1] + 1.65, a1[2]], look_yaw(1.0, 0.0), -4.0)
    views = [
        {"name": "overview_high", "pos": [0.0, 160.0, 140.0], "yaw": 0.0, "pitch": -50.0},
        {"name": "generator_eye", "pos": [round(gen[0] - 6.0, 2), round(gen[1] + eye, 2), round(gen[2] - 6.0, 2)], "yaw": look_yaw(1.0, 1.0), "pitch": 0.0, "gameplay": True},
    ]
    write_map(os.path.join(tdir, TID + ".nmap.json"), name=NAME, template_id=TID, template_version=VERSION, seed=SEED, width=W, depth=D,
              builder=b, scenario_json=sc.to_json(), explore_start=((a1[0], a1[1], a1[2]), look_yaw(1.0, 0.0)), camera=cam,
              bookmarks={str(i + 1): {"position": [o["pos"][0], o["pos"][1] + 40.0, o["pos"][2] + 30], "yaw": 0.0, "pitch": -50} for i, o in enumerate(sc.objectives)},
              extra={"groundPaint": paint.to_json(), "lighting": {"hour": 0.0, "fill": 0.35, "exposure": 1.0}, "look": {"fog": False}})   # BR2: no haze underground; UG4: darker (fill x 0.35, exposure pinned so auto exposure does not lift the dark stretches)
    write_template(os.path.join(tdir, TID + ".json"), tid=TID, name=NAME, order=7, version=VERSION, seed=SEED,
                   description="An underground mine labyrinth, 176 x 176 m, with some 20 m of relief: galleries that climb and drop by "
                               "inclines and stair drifts, two shafts with ledges and a bridge over the pit, a deep sump, a narrow-gauge "
                               "haulage line with points, a siding and mine carts, rooms full of mine gear. Steady caged lamps, dark "
                               "stretches and red emergency lamps. Assault (the Allies push in from the west galleries: generator room, "
                               "sump, collapse), Team Deathmatch and Solo.",
                   width=W, depth=D, margin=MARGIN, heightmap=grid.meta(TID + ".height.png"), water_level=-1000.0,
                   map_file=TID + ".nmap.json", camera=cam, thumbnail=TID + ".thumb.png", thumb_camera=thumb_cam,
                   extra={"userMap": True, "views": views})
    use = asset_usage(b)
    update_usage_doc(os.path.join(ctx.repo, "docs", "asset-usage-default-maps.md"), use, "Tunnels",
                     "- **Tunnels:** *Tunnels* (UG1, `tools/mapgen/maps/tunnels.py`): placed counts; \"not used\" = not on this map.")
    rep.update({"instances": len(b.inst), "errors": err, "warnings": warn, "use": dict(sorted(use.items())),
                "objectives": [(o["label"], o["displayName"], o["pos"]) for o in sc.objectives], "spawns": [(m["label"], m["pos"]) for m in sc.spawns]})
    write_summary(os.path.join(ctx.out, TID + ".summary.json"), rep)
    print("instances %d | %d m, node relief %.1f m (+10 m shafts), ramps %d, stairs %d | corridors %d m, rooms: %d chambers, %d medium, %d small | "
          "track %d m (siding %s), carts %s | lights %d %s | ducts %d, boards %d | errors %s | warnings %d" %
          (len(b.inst), W, relief, n_ramp, n_stairs, rep["tunnelLength_m"], len(chambers), len(mediums), n_small, rep["trackLength_m"], bool(siding),
           carts, rep["lights"], lamps, n_duct, n_board, err, len(warn)))
