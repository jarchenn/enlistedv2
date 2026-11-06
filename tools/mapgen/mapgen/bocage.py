"""Bocage: irregular hedgerow field networks, hedgerow trees, gates, orchards and sunken lanes."""
import math

from .geom import poly_area, poly_bounds, dist, norm, seg_dist, point_at, polyline_length, in_poly, lerp


def _clip(poly, p, n):
    """Keep the part of a convex polygon where dot(x - p, n) >= 0 (Sutherland-Hodgman, one plane)."""
    out = []
    k = len(poly)
    for i in range(k):
        a, b = poly[i], poly[(i + 1) % k]
        da = (a[0] - p[0]) * n[0] + (a[1] - p[1]) * n[1]
        db = (b[0] - p[0]) * n[0] + (b[1] - p[1]) * n[1]
        if da >= 0:
            out.append(a)
        if (da >= 0) != (db >= 0):
            t = da / (da - db)
            out.append((lerp(a[0], b[0], t), lerp(a[1], b[1], t)))
    return out


def _chord(poly, p, d):
    """Segment of the line p + t d inside a convex polygon."""
    ts = []
    k = len(poly)
    for i in range(k):
        a, b = poly[i], poly[(i + 1) % k]
        ex, ez = b[0] - a[0], b[1] - a[1]
        den = d[0] * ez - d[1] * ex
        if abs(den) < 1e-9:
            continue
        t = ((a[0] - p[0]) * ez - (a[1] - p[1]) * ex) / den
        u = ((a[0] - p[0]) * d[1] - (a[1] - p[1]) * d[0]) / den
        if -1e-6 <= u <= 1 + 1e-6:
            ts.append(t)
    if len(ts) < 2:
        return None
    t0, t1 = min(ts), max(ts)
    return ((p[0] + d[0] * t0, p[1] + d[1] * t0), (p[0] + d[0] * t1, p[1] + d[1] * t1))


def split_fields(region, rng, *, field_area=(9000.0, 20000.0), skew=18.0, min_side=45.0):
    """Recursively split a convex region into irregular fields (Norman bocage: 1-2 ha, few right angles).
    Returns (fields, hedge_lines): field polygons and the split chords (each one hedgerow, no duplicates)."""
    fields, lines = [], []
    stack = [region]
    while stack:
        poly = stack.pop()
        A = poly_area(poly)
        target = rng.uniform(*field_area)
        x0, z0, x1, z1 = poly_bounds(poly)
        if A <= target * 1.3 or min(x1 - x0, z1 - z0) < min_side * 1.6 and A < target * 2.5:
            fields.append(poly)
            continue
        # split across the longer extent, through a point near the middle, skewed a little
        cx = sum(p[0] for p in poly) / len(poly)
        cz = sum(p[1] for p in poly) / len(poly)
        longx = (x1 - x0) >= (z1 - z0)
        ang = (90.0 if longx else 0.0) + rng.uniform(-skew, skew)
        d = (math.sin(math.radians(ang)), math.cos(math.radians(ang)))   # split line direction
        n = (d[1], -d[0])
        off = rng.uniform(-0.18, 0.18) * ((x1 - x0) if longx else (z1 - z0))
        p = (cx + n[0] * off, cz + n[1] * off)
        a = _clip(poly, p, n)
        b = _clip(poly, p, (-n[0], -n[1]))
        if len(a) < 3 or len(b) < 3 or poly_area(a) < 1500 or poly_area(b) < 1500:
            fields.append(poly)
            continue
        ch = _chord(poly, p, d)
        if ch:
            lines.append([ch[0], ch[1]])
        stack += [a, b]
    return fields, lines


def cut_gaps(line, cutters, half_gap):
    """Arc-length intervals of `line` within half_gap of any cutter polyline crossing it (roads through hedges)."""
    gaps = []
    L = polyline_length(line)
    s = 0.0
    step = 2.0
    inside, s0 = False, 0.0
    while s <= L:
        p, _ = point_at(line, s)
        near = any(min(seg_dist(p, c[i], c[i + 1])[0] for i in range(len(c) - 1)) < half_gap for c in cutters)
        if near and not inside:
            inside, s0 = True, s
        elif not near and inside:
            inside = False
            gaps.append((s0, s))
        s += step
    if inside:
        gaps.append((s0, L))
    return gaps


def hedgerow(b, line, rng, *, group=None, gaps=(), gate_every=(140.0, 260.0), tree_every=(6.0, 12.0),
             trees=(("tree_oak", 3.0), ("tree_ash", 2.0), ("tree_poplar", 0.4)), extra_trees=True, tags=(),
             pieces=(("bcg_hedgerow_straight", 0.85), ("hedgerow_bocage", 0.15))):
    """One hedgerow along a line: 10 m bank pieces (some older 8 m hedges), a field-gate gap now and then, standalone
    hedgerow trees on the bank so trees stand every 6-12 m counting the pieces' own pollards."""
    group = group if group is not None else b.new_group()
    L = polyline_length(line)
    if L < 6:
        return [], []
    g = list(gaps)
    gate_s = rng.uniform(*gate_every) * rng.uniform(0.2, 0.8)
    gate_pieces = []
    while gate_s < L - 8:
        g.append((gate_s - 1.5, gate_s + 1.5))
        gate_pieces.append(gate_s)
        gate_s += rng.uniform(*gate_every)
    tot = sum(w for _, w in pieces)
    placed = []
    s = 0.0
    while s < L - 3:
        r = rng.uniform(0, tot)
        for aid, w in pieces:
            r -= w
            if r <= 0:
                break
        a = b.cat[aid]
        plen = a.fw
        if s + plen > L + 1.0:
            aid, plen = "bcg_hedgerow_gap", b.cat["bcg_hedgerow_gap"].fw
            if s + plen > L + 2.0:
                break
        mid = s + plen * 0.5
        if any(g0 - plen * 0.45 < mid < g1 + plen * 0.45 for g0, g1 in g):
            # a gate opening: the W4 gap piece where the gap is short, otherwise leave it open
            if any(abs(mid - gs) < plen * 0.5 for gs in gate_pieces) and rng.random() < 0.6:
                p, t = point_at(line, mid)
                from .geom import asset_yaw_along
                e = b.place("bcg_hedgerow_gap", p[0], p[1], asset_yaw_along(*t), group=group, tags=set(tags) | {"hedge"})
                if e:
                    placed.append(e)
            s += plen
            continue
        p, t = point_at(line, mid)
        from .geom import asset_yaw_along
        e = b.place(aid, p[0], p[1], asset_yaw_along(*t) + (180.0 if rng.random() < 0.5 else 0.0), group=group,
                    tags=set(tags) | {"hedge"}, ok_with={"flat", "none", "wire"})
        if e:
            placed.append(e)
        s += plen
    tr = []
    if extra_trees:
        tt = sum(w for _, w in trees)
        s = rng.uniform(2, 14)
        while s < L - 2:
            if not any(g0 - 2 < s < g1 + 2 for g0, g1 in g):
                p, t = point_at(line, s)
                r = rng.uniform(0, tt)
                for aid, w in trees:
                    r -= w
                    if r <= 0:
                        break
                off = rng.uniform(-0.6, 0.6)
                e = b.place(aid, p[0] - t[1] * off, p[1] + t[0] * off, rng.uniform(-180, 180), group=group,
                            tags=set(tags) | {"hedge_tree"}, ok_with={"flat", "none", "wire"})
                if e:
                    tr.append(e)
            # pieces carry two pollards per 10 m; standalone trees every 2-4 spans keep the 6-12 m rhythm varied
            s += rng.uniform(tree_every[0] * 2.5, tree_every[1] * 3.0)
    return placed, tr


def orchard(b, poly, rng, *, spacing=8.0, jitter=1.3, gap=0.15, asset="tree_apple", tags=()):
    """Orchard: a loose grid (SPEC 3.5: 8 m with gaps), rows turned to the field's long side, every tree jittered."""
    x0, z0, x1, z1 = poly_bounds(poly)
    ang = rng.uniform(-12, 12)
    c, s = math.cos(math.radians(ang)), math.sin(math.radians(ang))
    cx, cz = (x0 + x1) / 2, (z0 + z1) / 2
    R = math.hypot(x1 - x0, z1 - z0) / 2 + spacing
    out = []
    u = -R
    while u <= R:
        v = -R
        while v <= R:
            if rng.random() >= gap:
                x = cx + u * c - v * s + rng.uniform(-jitter, jitter)
                z = cz + u * s + v * c + rng.uniform(-jitter, jitter)
                if in_poly((x, z), poly):
                    e = b.place(asset, x, z, rng.uniform(-180, 180), tags=set(tags) | {"orchard"})
                    if e:
                        out.append(e)
            v += spacing
        u += spacing
    return out


def sunken_lane(b, pts, rng=None, *, tags=(), gaps=()):
    """Chain W4 sunken-lane pieces (join along local Z) along a polyline and paint the lane surface."""
    out = b.chain("bcg_sunken_lane", pts, along="z", tags=set(tags) | {"sunken_lane"}, gaps=gaps,
                  ok_with={"flat", "none", "wire", "veg"})
    return out
