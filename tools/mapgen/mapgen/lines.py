"""LN2 (docs/lines.md#ln2): the laid fence / wire / wall / hedge pieces become lines before the map is written, chained
exactly as the engine migrates old maps (src/lines_ln2.cpp lineMigratePieces, rules in content/settings/line_types.json
"migrate" / "gates" / "minChain"): piece ends that meet within 0.6 m chain through every two-way joint of one line type,
a gate piece touching a chain becomes a gate span, a gateway piece a gap span. The map scripts keep placing pieces (their
spacing, gaps and scoring stay as they were); every default map is written with lines."""
import json
import math
import os

from .geom import local_to_world

JOIN = 0.6


def _types(cat):
    with open(os.path.join(cat.content_dir, "settings", "line_types.json"), encoding="utf-8") as f:
        return json.load(f)["types"]


def pieces_to_lines(b, scenario_json=None):
    """Converts b.inst in place; returns (pieces removed, lines made)."""
    cat = b.cat
    types = _types(cat)
    by_id, gate_types = {}, {}
    for k, t in enumerate(types):
        for pid, m in t.get("migrate", {}).items():
            by_id[pid] = (k, m)
        for g in t.get("gates", []):
            gate_types.setdefault(g, []).append(k)
    ps = []   # dict(e, type, p [(x, z)], gaps [(a, b)], gate)
    for e in b.inst:
        if (e.get("extra") or {}).get("lineGate"):
            continue
        gate = e["asset"] in gate_types
        if e["asset"] not in by_id and not gate:
            continue
        a = cat[e["asset"]]
        ty, m = by_id.get(e["asset"], (-1, {}))
        axis = 0 if a.fw >= a.fd else 2
        L = max(0.5, a.fw if axis == 0 else a.fd)
        W = lambda lx, lz: local_to_world((e["pos"][0], e["pos"][2]), e["yaw"], lx, lz)
        if "path" in m:
            p = [W(q[0], q[1]) for q in m["path"]]
        elif axis == 0:
            p = [W(-L / 2, 0), W(L / 2, 0)]
        else:
            p = [W(0, -L / 2), W(0, L / 2)]
        ps.append({"e": e, "type": ty, "p": p, "gaps": [tuple(m["gap"])] if "gap" in m else [], "gate": e["asset"] if gate else ""})
    if not ps:
        return 0, 0
    nodes = []

    def node(q):
        for k, n in enumerate(nodes):
            if math.hypot(n[0] - q[0], n[1] - q[1]) < JOIN:
                return k
        nodes.append(q)
        return len(nodes) - 1
    ends = [(node(P["p"][0]), node(P["p"][-1])) for P in ps]
    for k, P in enumerate(ps):   # a gate piece takes the type of a line piece it touches
        if P["type"] >= 0:
            continue
        for m, Q in enumerate(ps):
            if Q["type"] >= 0 and Q["type"] in gate_types.get(P["gate"], []) and set(ends[k]) & set(ends[m]):
                P["type"] = Q["type"]
                break
    deg = {}
    for k, P in enumerate(ps):
        if P["type"] >= 0:
            for n in ends[k]:
                deg[(n, P["type"])] = deg.get((n, P["type"]), 0) + 1
    used = [False] * len(ps)
    gone, made, remap = set(), 0, {}
    for k0, P0 in enumerate(ps):
        if used[k0] or P0["type"] < 0:
            continue
        ty = P0["type"]
        two = lambda n: deg.get((n, ty), 0) == 2
        k, rev = k0, False
        for _ in range(len(ps)):   # back to a chain end
            n = ends[k][1] if rev else ends[k][0]
            if not two(n):
                break
            nk = None
            for m in range(len(ps)):
                if m != k and ps[m]["type"] == ty and n in ends[m]:
                    nk = m
            if nk is None or nk == k0:
                break
            rev = ends[nk][0] == n
            k = nk
        pts, spans, srcs, s = [], [], [], 0.0
        for _ in range(len(ps)):
            used[k] = True
            P = ps[k]
            srcs.append(P["e"])
            seg = list(reversed(P["p"])) if rev else list(P["p"])
            segL = sum(math.hypot(seg[q][0] - seg[q - 1][0], seg[q][1] - seg[q - 1][1]) for q in range(1, len(seg)))
            if P["gate"]:
                spans.append({"s": round(s + segL / 2, 3), "w": round(segL, 3), "kind": "field_gate" if P["gate"] == "field_gate" else "gate_piers"})
            for g0, g1 in P["gaps"]:
                a_, b_ = (segL - g1, segL - g0) if rev else (g0, g1)
                spans.append({"s": round(s + (a_ + b_) / 2, 3), "w": round(b_ - a_, 3), "kind": "gap"})
            pts += seg if not pts else seg[1:]
            s += segL
            n = ends[k][0] if rev else ends[k][1]
            if not two(n):
                break
            nk = next((m for m in range(len(ps)) if m != k and not used[m] and ps[m]["type"] == ty and n in ends[m]), None)
            if nk is None:
                break
            rev = ends[nk][1] == n
            k = nk
        t = types[ty]
        if len(pts) < 2 or len(srcs) < t.get("minChain", 1):
            continue
        clean = []
        for q in pts:
            if not clean or math.hypot(q[0] - clean[-1][0], q[1] - clean[-1][1]) > 0.05:
                clean.append(q)
        line = {"pts": [[round(q[0], 3), round(q[1], 3)] for q in clean], "seed": int(srcs[0]["id"]) * 2654435761 % 4294967296}
        if spans:
            line["spans"] = sorted(spans, key=lambda x: x["s"])
        e = b.place(t["asset"], clean[0][0], clean[0][1], 0.0, check=False, cls="none", extra={"line": line}, tags={"line", t["id"]},
                    y=b.ground(clean[0][0], clean[0][1]))
        if not e:
            continue
        made += 1
        for x in srcs:
            gone.add(id(x))
            remap[x["id"]] = e["id"]
    for P in ps:
        if id(P["e"]) in gone:
            b.remove(P["e"])
    for o in (scenario_json or {}).get("objectives", []):   # objectives keep their links (the line now)
        if "linkedStructures" in o:
            ids = []
            for v in o["linkedStructures"]:
                v = remap.get(v, v)
                if v not in ids:
                    ids.append(v)
            o["linkedStructures"] = ids
    return len(gone), made


# ------------------------------------------------------------------------------------------------ LN1 trench lines
def trench_lines(b, pieces, pinned=()):
    """LN1: the laid trench pieces become trench lines (docs/lines.md), chained exactly as the engine migrates old maps
    (src/lines.cpp lineMigrateTrenches): every piece's centreline (a junction's arms too) is a polyline, polylines whose
    ends meet are chained through every two-way joint, the pieces are removed. Returns {piece id: line id}."""
    cat = b.cat
    polys = []   # (points [(x, z, d, y)], piece id, fire +1 left / -1 right / 0); y: a pinned piece's fixed floor
    depth = 1.62   # content/settings/terrain_blend.json trench depth
    for e in pieces:
        tj = cat[e["asset"]].raw["trench"]
        W = lambda q: (*local_to_world((e["pos"][0], e["pos"][2]), e["yaw"], q[0], q[1]), q[2] if len(q) > 2 else 1.0,
                       e["pos"][1] - depth * (q[2] if len(q) > 2 else 1.0) if e["id"] in pinned else None)
        P = [W(q) for q in tj["path"]]
        dx, dz = P[1][0] - P[0][0], P[1][1] - P[0][1]
        L = math.hypot(dx, dz)
        lz = (math.sin(math.radians(e["yaw"])), math.cos(math.radians(e["yaw"])))
        f = (-dz / L) * lz[0] + (dx / L) * lz[1]
        polys.append((P, e["id"], 1 if f > 0.3 else -1 if f < -0.3 else 0))
        for br in tj.get("branches", []):
            a, z = W(br[0]), W(br[-1])
            if math.hypot(z[0] - a[0], z[1] - a[1]) >= 3.0:
                polys.append(([a, z], e["id"], 0))
    nodes, deg = [], []

    def node(p):
        for k, q in enumerate(nodes):
            if math.hypot(q[0] - p[0], q[1] - p[1]) < 0.3:
                return k
        nodes.append((p[0], p[1]))
        deg.append(0)
        return len(nodes) - 1
    ends = []
    for P, _, _ in polys:
        a, z = node(P[0]), node(P[-1])
        deg[a] += 1
        deg[z] += 1
        ends.append((a, z))
    used, remap, lines = [False] * len(polys), {}, []
    for k0 in range(len(polys)):
        if used[k0]:
            continue
        k, rev = k0, False
        for _ in range(len(polys)):   # back to a chain end
            n = ends[k][1] if rev else ends[k][0]
            if deg[n] != 2:
                break
            nk = next((m for m in range(len(polys)) if m != k and n in ends[m]), None)
            if nk is None or nk == k0:
                break
            rev = ends[nk][0] == n
            k = nk
        pts, srcs, fire = [], [], 0
        for _ in range(len(polys)):
            used[k] = True
            seg = list(reversed(polys[k][0])) if rev else list(polys[k][0])
            if not fire and polys[k][2]:
                fire = -polys[k][2] if rev else polys[k][2]
            pts += seg if not pts else seg[1:]
            srcs.append(polys[k][1])
            n = ends[k][0] if rev else ends[k][1]
            if deg[n] != 2:
                break
            nk = next((m for m in range(len(polys)) if m != k and not used[m] and n in ends[m]), None)
            if nk is None:
                break
            rev = ends[nk][1] == n
            k = nk
        clean = []
        for p in pts:
            if not clean or math.hypot(p[0] - clean[-1][0], p[1] - clean[-1][1]) > 0.05:
                clean.append(p)
        if len(clean) < 2:
            continue
        line = {"pts": [[round(p[0], 3), round(p[1], 3)] + ([round(p[2], 3)] if p[2] != 1.0 or p[3] is not None else []) + ([round(p[3], 3)] if p[3] is not None else []) for p in clean],
                "seed": int(srcs[0]) * 7919 % 100000 + 1,
                "params": {"fire": "left" if fire > 0 else "right" if fire < 0 else "none", "ramps": "none", "sharp": True}}
        e = b.place("line_trench", clean[0][0], clean[0][1], 0.0, check=False, cls="none", extra={"line": line}, tags={"trench", "trench_line"},
                    y=b.ground(clean[0][0], clean[0][1]))
        lines.append(e)
        for s in srcs:
            remap[s] = e["id"]
    for e in pieces:
        b.remove(e)
    return remap, lines


def trench_pieces_to_lines(b, scenario_json=None):
    """LN1: any aw_trench_* pieces still laid become trench lines before the map is written (as the engine migrates
    them on load); objectives linking a piece link its line. Returns (pieces, lines)."""
    pieces = [e for e in b.inst if e["asset"].startswith("aw_trench_")]
    if not pieces:
        return 0, 0
    remap, lines = trench_lines(b, pieces)
    for o in (scenario_json or {}).get("objectives", []):
        if "linkedStructures" in o:
            o["linkedStructures"] = sorted({remap.get(v, v) for v in o["linkedStructures"]})
    return len(pieces), len(lines)
