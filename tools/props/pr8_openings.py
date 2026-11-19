"""PR8: openable doors and windows for the imported model buildings (garage, security hut, gas-station kiosk).

The model's own door panels / door leaves / window glass are deleted in the bake (pr6_split.py delete_mats /
delete_clusters); here each opening becomes one of the game's interactables (src/assets.cpp: kind door / window,
leaves of box parts swinging about a hinge, aperture) and is cut out of the building's collision boxes.
Openings are given as thin boxes on the door / window plane: [x0, x1, y0, y1, z0, z1] in the source's Blender axes
(the bake's "marks" turn them into game axes like the colliders).
"""

MATS = [
    {"name": "door_wood", "albedo": [0.32, 0.22, 0.14], "roughness": 0.8, "pattern": "wood", "metallic": 0, "grain": 1, "texture": "rb_planks"},
    {"name": "door_steel", "albedo": [0.36, 0.3, 0.26], "roughness": 0.7, "pattern": "metal", "metallic": 0.4, "grain": 1, "texture": "rb_steel_rusty"},
    {"name": "iron", "albedo": [0.05, 0.048, 0.046], "roughness": 0.55, "pattern": "metal", "metallic": 0.15, "grain": 1},
    {"name": "window_paint", "albedo": [0.58, 0.56, 0.5], "roughness": 0.5, "pattern": "painted", "metallic": 0, "grain": 1},
    {"name": "glass", "albedo": [0.8, 0.85, 0.82], "roughness": 0.05, "pattern": "glass", "metallic": 0, "grain": 1},
    {"name": "concrete_floor", "albedo": [0.42, 0.41, 0.39], "roughness": 0.85, "pattern": "concrete", "metallic": 0, "grain": 1},
]


def r3(v): return [round(x, 3) for x in v]


def _box(c, s, m, along_x):   # a part box: c centre, s = (width along the wall, height, depth through it)
    return {"t": "b", "c": r3(c), "s": r3([s[0], s[1], s[2]] if along_x else [s[2], s[1], s[0]]), "m": m, "k": 0}


def leaf_parts(kind, mat, x0, x1, y0, y1, plane, along_x, glazed):
    """One leaf spanning [x0, x1] along the wall (game x or z), [y0, y1] up, on the wall plane."""
    w, h, cx, cy = x1 - x0, y1 - y0, (x0 + x1) / 2, (y0 + y1) / 2
    P = lambda u, v, d=0.0: [u, v, plane + d] if along_x else [plane + d, v, u]
    if not glazed:   # a door: the slab, a mid rail, a handle on both faces near the free edge
        out = [_box(P(cx, cy), (w - 0.01, h - 0.01, 0.05), mat, along_x), _box(P(cx, y0 + h * 0.45, 0.03), (w - 0.06, 0.08, 0.012), mat, along_x)]
        return out
    t = 0.045; f = 0.05   # a casement: frame bars and the glass
    return [_box(P(x0 + f / 2, cy), (f, h, t), "window_paint", along_x), _box(P(x1 - f / 2, cy), (f, h, t), "window_paint", along_x),
            _box(P(cx, y1 - f / 2), (w, f, t), "window_paint", along_x), _box(P(cx, y0 + f / 2), (w, f, t), "window_paint", along_x),
            _box(P(cx, cy), (w - 2 * f + 0.01, 0.025, 0.035), "window_paint", along_x), _box(P(cx, cy), (w - 2 * f + 0.01, h - 2 * f + 0.01, 0.006), "glass", along_x)]


def interactable(oid, kind, mat, m, leaves, default_open=False):
    """m: the opening's mark in game axes [cx, cy, cz, sx, sy, sz] (thin across the wall)."""
    cx, cy, cz, sx, sy, sz = m
    along_x = sx >= sz
    plane = cz if along_x else cx
    a0, a1 = (cx - sx / 2, cx + sx / 2) if along_x else (cz - sz / 2, cz + sz / 2)
    y0, y1 = cy - sy / 2, cy + sy / 2
    glazed = kind == "window"
    L = []
    edges = [(a0, a1)] if leaves == 1 else [(a0, (a0 + a1) / 2), ((a0 + a1) / 2, a1)]
    for k, (e0, e1) in enumerate(edges):
        hinge_at_min = k == 0
        parts = leaf_parts(kind, mat, e0, e1, y0, y1, plane, along_x, glazed)
        if not glazed:   # handle near the free edge, both faces
            hx = e1 - 0.09 if hinge_at_min else e0 + 0.09
            for d in (0.04, -0.04):
                parts.append({"t": "b", "c": r3([hx, y0 + 1.0, plane + d] if along_x else [plane + d, y0 + 1.0, hx]), "s": r3([0.03, 0.16, 0.03]), "m": "iron", "k": 0})
        hinge = [e0, 0, plane] if along_x else [plane, 0, e0]
        if not hinge_at_min: hinge = [e1, 0, plane] if along_x else [plane, 0, e1]
        L.append({"parts": parts, "hinge": r3(hinge), "sign": 1 if hinge_at_min else -1})
    th = 0.8   # the aperture runs right through the wall (a window plane can sit off the wall's middle)
    amin = [cx - sx / 2, y0, cz - th] if along_x else [cx - th, y0, cz - sz / 2]
    amax = [cx + sx / 2, y1, cz + th] if along_x else [cx + th, y1, cz + sz / 2]
    return {"id": oid, "kind": kind, "label": "window" if glazed else ("garage door" if (a1 - a0) > 1.6 else "door"),
            "sound": "glass" if glazed else ("wood" if mat == "door_wood" else "steel"), "axis": [0, 1, 0],
            "openAngle": 100, "duration": 0.85 if glazed else 1.1, "defaultOpen": default_open, "aperture": {"min": r3(amin), "max": r3(amax)}, "leaves": L}


def subtract(b, h):
    """Axis-aligned box b minus box h, both [min(3), max(3)]: up to six boxes."""
    (bl, bh), (hl, hh) = b, h
    if any(hh[k] <= bl[k] or hl[k] >= bh[k] for k in range(3)): return [b]
    out, lo, hi = [], list(bl), list(bh)
    for k in range(3):
        if hl[k] > lo[k]: n_hi = list(hi); n_hi[k] = hl[k]; out.append((list(lo), n_hi)); lo[k] = hl[k]
        if hh[k] < hi[k]: n_lo = list(lo); n_lo[k] = hh[k]; out.append((n_lo, list(hi))); hi[k] = hh[k]
    return [o for o in out if all(o[1][k] - o[0][k] > 0.01 for k in range(3))]


def col(p): return p.get("v") == 0 or bool(p.get("mf"))   # a static collider: invisible, or a local-only model's stand-in (PR10)


def apply(A, marks, openings, floor_key="floor", room_type=None, holes=(), floor_visible=True):
    """Fit the openings (marks: game-axis boxes by name; openings: name -> (kind, material, leaves)), cut them out of the
    collision parts, add the visible floor, make the building enterable with nav from its colliders. room_type names the
    one room's furnishing recipe (interior.cpp roomTypes: a guard hut is an office, not the default kitchen).
    PR10: holes = mark names of open bays (windows without leaves: boxes through the wall, cut like the doors);
    floor_visible False: the floor is a collider only (the model has its own; no visible floor part = no auto-furnish)."""
    if room_type and floor_key in marks:
        cx, cy, cz, sx, sy, sz = marks[floor_key]
        A["nav"]["rooms"] = [{"id": "main", "min": r3([cx - sx / 2, cy, cz - sz / 2]), "max": r3([cx + sx / 2, cy + 2.4, cz + sz / 2])}]
        A["roomTypes"] = {"main": room_type}
    have = {m["name"] for m in A["materials"]}
    A["materials"] += [m for m in MATS if m["name"] not in have]
    A["interactables"] = [interactable(n, o[0], o[1], marks[n], o[2], *o[3:]) for n, o in openings.items()]
    for it in A["interactables"]:   # through-wall depth: just the wall colliders the 0.8 m probe crosses (the furnisher keeps the aperture clear, plus 0.7-1 m)
        ap = it["aperture"]; lo, hi = ap["min"], ap["max"]
        m = marks[it["id"]]; n = 2 if m[3] >= m[5] else 0   # through the wall: across the thin mark (as interactable())
        mid = (lo[n] + hi[n]) / 2; a, b = mid - 0.06, mid + 0.06
        for p in A["parts"]:
            if not col(p): continue
            pl = [p["c"][k] - p["s"][k] / 2 for k in range(3)]; ph = [p["c"][k] + p["s"][k] / 2 for k in range(3)]
            if ph[n] - pl[n] < 1.0 and all(ph[k] > lo[k] and pl[k] < hi[k] for k in range(3)): a, b = min(a, pl[n] - 0.02), max(b, ph[n] + 0.02)
        lo[n], hi[n] = round(max(lo[n], a), 3), round(min(hi[n], b), 3)
    bays = [([marks[h][k] - marks[h][3 + k] / 2 for k in range(3)], [marks[h][k] + marks[h][3 + k] / 2 for k in range(3)]) for h in holes]
    holes = [(it["aperture"]["min"], it["aperture"]["max"]) for it in A["interactables"]] + bays
    parts = []
    for p in A["parts"]:
        if not col(p): parts.append(p); continue
        flag = {"v": 0} if p.get("v") == 0 else {"mf": 1}
        boxes = [([p["c"][k] - p["s"][k] / 2 for k in range(3)], [p["c"][k] + p["s"][k] / 2 for k in range(3)])]
        for h in holes: boxes = [q for b in boxes for q in subtract(b, h)]
        parts += [{"t": "b", "c": r3([(lo[k] + hi[k]) / 2 for k in range(3)]), "s": r3([hi[k] - lo[k] for k in range(3)]), "m": p["m"], **flag} for lo, hi in boxes]
    if floor_key in marks:
        cx, cy, cz, sx, sy, sz = marks[floor_key]
        parts.append({"t": "b", "c": r3([cx, cy, cz]), "s": r3([sx, max(sy, 0.02), sz]), "m": "concrete_floor", **({"k": 0} if floor_visible else {"v": 0})})
    A["parts"] = parts
    for amin, amax in holes:   # no static collider may cross an opening (player and bullets pass where the leaf is open)
        lo = [amin[k] + 0.02 for k in range(3)]; hi = [amax[k] - 0.02 for k in range(3)]
        for p in parts:
            if p.get("collide") is False or not col(p): continue   # the static colliders (the walkable floor is under the threshold)
            pl = [p["c"][k] - p["s"][k] / 2 for k in range(3)]; ph = [p["c"][k] + p["s"][k] / 2 for k in range(3)]
            assert any(ph[k] <= lo[k] or pl[k] >= hi[k] for k in range(3)), (A["id"], amin, p)
    A["enterable"] = True
    A["nav"]["effect"] = "block"   # nav from the colliders: the doorways stay walkable


if __name__ == "__main__":   # self-check: a hole through the middle of a wall leaves the jambs, sill and lintel
    w = subtract(([0, 0, 0], [4, 3, 0.3]), ([1, 0, -0.5], [2, 2, 0.8]))
    assert len(w) == 3 and sum((b[1][0] - b[0][0]) * (b[1][1] - b[0][1]) for b in w) == 4 * 3 - 1 * 2, w
    it = interactable("d", "door", "door_steel", [0, 1, 0, 1.0, 2.0, 0.06], 1)
    assert it["leaves"][0]["hinge"] == [-0.5, 0, 0] and it["aperture"]["min"][2] < 0 < it["aperture"]["max"][2]
    B = {"id": "t", "materials": [], "nav": {}, "parts": [{"t": "b", "c": [0, 1.5, 0], "s": [4, 3, 0.3], "m": "w", "mf": 1}]}
    apply(B, {"win": [0, 1.5, 0, 1.0, 1.0, 0.4], "floor": [0, 0.1, 1, 4, 0.02, 2]}, {}, holes=["win"], floor_visible=False)
    assert len([q for q in B["parts"] if q.get("mf")]) == 4 and B["parts"][-1].get("v") == 0, B["parts"]   # jambs, sill, lintel stay stand-ins; the floor a collider
    print("pr8_openings ok")
