"""VOT T3 (docs/visual-overhaul-plan.md 3.C): real relief for the default maps, applied on the W3 grid at bake time.

Opt-in only: nothing in mapgen calls these; a map imports them (existing maps' output is unchanged).

  weight_field(grid, fn)                    a 0..1 array from fn(x, z) (1 = free to change, 0 = keep exactly)
  relief_layers(grid, layers, w)            layered fbm (amp m, scale m, seed, octaves) times the weight
  thermal_erosion(grid, w, iters, talus)    material slides off slopes steeper than `talus` (rise / run)
  hydraulic_erosion(grid, w, iters, ...)    a simple grid water / sediment model (Mei, Decaudin, Hu 2007, without the
                                            GPU pipes' velocity field): rain, outflow to the 4 neighbours, erosion
                                            and deposition toward a carrying capacity, evaporation
  drainage(grid, w, depth, ...)             D8 flow accumulation: channels carved where enough ground drains (toward
                                            the lowest ground / water); returns the accumulation for paint
  hollow_way(grid, path, depth, ...)        a lane sunk below its banks by traffic and water: its floor follows the
                                            smoothed ground along the lane minus `depth`
  slope_report(grid, paths, step)           max / mean grade (degrees) along polylines (the walkable-lane check)

Every operation blends its result with the input by the weight array, so building pads, roads and lanes stay exactly
where the map protects them."""
import math

import numpy as np

from .geom import fbm, polyline_dist, poly_bounds, resample, point_at, polyline_length
from .terrain import KCELL


def _arr(grid):
    return np.frombuffer(grid.h, dtype=np.float64).reshape(grid.nz, grid.nx)   # a view: writes go straight to the grid


def _coords(grid):
    xs = grid.org_x + np.arange(grid.nx) * KCELL
    zs = grid.org_z + np.arange(grid.nz) * KCELL
    return xs, zs


def weight_field(grid, fn):
    """fn(x, z) -> 0..1 at every vertex (python call per vertex; ~0.1 s per 10 k vertices)."""
    xs, zs = _coords(grid)
    w = np.empty((grid.nz, grid.nx))
    for j, z in enumerate(zs):
        for i, x in enumerate(xs):
            w[j, i] = fn(x, z)
    return np.clip(w, 0.0, 1.0)


def relief_layers(grid, layers, w):
    """layers: [(amp m, scale m, seed, octaves)]; adds amp * (2 fbm - 1) per layer, times w."""
    H = _arr(grid)
    xs, zs = _coords(grid)
    add = np.zeros_like(H)
    for amp, scale, seed, octv in layers:
        for j, z in enumerate(zs):
            for i, x in enumerate(xs):
                if w[j, i] > 0:
                    add[j, i] += amp * (2.0 * fbm(x / scale, z / scale, seed, octv) - 1.0)
    H += add * w
    return float(np.abs(add * w).max())


def _blend(H, H0, w):
    H[...] = H0 + (H - H0) * w


_N8 = [(-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (1, 1), (-1, 1), (1, -1)]


def _shift(a, di, dj, fill):
    """a[j + dj, i + di] at [j, i] (out of range = fill)."""
    out = np.full_like(a, fill)
    nz, nx = a.shape
    js, je = max(0, -dj), nz - max(0, dj)
    is_, ie = max(0, -di), nx - max(0, di)
    out[js:je, is_:ie] = a[js + dj:je + dj, is_ + di:ie + di]
    return out


def thermal_erosion(grid, w, iters=40, talus=0.65, rate=0.35):
    """Slopes steeper than `talus` (rise per metre, 0.65 = 33 deg) shed the excess to their lower neighbours."""
    H = _arr(grid)
    H0 = H.copy()
    for _ in range(iters):
        moved = np.zeros_like(H)
        for di, dj in _N8:
            dist = KCELL * (math.sqrt(2.0) if di and dj else 1.0)
            Hn = _shift(H, di, dj, np.inf)
            ex = H - Hn - talus * dist
            m = np.where(ex > 0, ex * rate / 8.0, 0.0)
            moved -= m
            moved += _shift(m, -di, -dj, 0.0)
        H += moved * w
    _blend(H, H0, w)


def hydraulic_erosion(grid, w, iters=90, rain=0.012, kc=1.6, ks=0.25, kd=0.3, ke=0.04, max_cut=0.12):
    """Rain, flow to the 4 neighbours by water-surface difference, sediment capacity = kc * outflow * slope; dissolve
    (ks) below it, deposit (kd) above it; water evaporates by ke a step. max_cut: most a step may remove (m)."""
    H = _arr(grid)
    H0 = H.copy()
    W = np.zeros_like(H)
    S = np.zeros_like(H)
    n4 = [(-1, 0), (1, 0), (0, -1), (0, 1)]
    for _ in range(iters):
        W += rain * w
        Hs = H + W
        d = [np.maximum(Hs - _shift(Hs, di, dj, np.inf), 0.0) for di, dj in n4]
        tot = d[0] + d[1] + d[2] + d[3]
        f = np.minimum(W, tot * 0.5) / np.maximum(tot, 1e-9)   # move at most half the difference: no oscillation
        out = [dk * f for dk in d]
        osum = out[0] + out[1] + out[2] + out[3]
        gx = (_shift(H, 1, 0, 0) - _shift(H, -1, 0, 0)) / (2 * KCELL)
        gz = (_shift(H, 0, 1, 0) - _shift(H, 0, -1, 0)) / (2 * KCELL)
        slope = np.clip(np.hypot(gx, gz), 0.02, 1.5)
        slope[0, :] = slope[-1, :] = 0.02
        slope[:, 0] = slope[:, -1] = 0.02
        cap = kc * osum * slope
        ero = np.where(S < cap, np.minimum((cap - S) * ks, max_cut), -(S - cap) * kd)
        ero *= w
        H -= ero
        S += ero
        frac = np.where(W > 1e-9, osum / np.maximum(W, 1e-9), 0.0).clip(0, 1)
        s_out = S * frac
        S -= s_out
        Wn = W - osum
        for k, (di, dj) in enumerate(n4):
            share = np.where(osum > 1e-12, out[k] / np.maximum(osum, 1e-12), 0.0)
            Wn += _shift(out[k], -di, -dj, 0.0)
            S += _shift(s_out * share, -di, -dj, 0.0)
        W = Wn * (1.0 - ke)
    H += S * 0.5 * w   # what is still suspended settles where it is
    _blend(H, H0, w)


def smooth(grid, w, passes=2, k=0.5):
    """Weighted 3x3 relaxation (removes single-cell pits and bumps that erosion leaves); w scales it per vertex."""
    H = _arr(grid)
    for _ in range(passes):
        avg = (_shift(H, 1, 0, 0) + _shift(H, -1, 0, 0) + _shift(H, 0, 1, 0) + _shift(H, 0, -1, 0)) * 0.25
        avg[0, :], avg[-1, :], avg[:, 0], avg[:, -1] = H[0, :], H[-1, :], H[:, 0], H[:, -1]
        H += (avg - H) * k * w


def drainage(grid, w, depth=0.6, start=60, full=600, width=1):
    """D8 flow accumulation (cells draining through each vertex). Where it passes `start` cells a channel is cut,
    deepening to `depth` m at `full` cells; `width` widens it by that many neighbours (a softer bed). Returns acc."""
    H = _arr(grid)
    nz, nx = H.shape
    order = np.argsort(-H, axis=None)
    acc = np.ones(nz * nx)
    flat = H.ravel()
    for k in order:
        j, i = divmod(int(k), nx)
        best, bk = 0.0, -1
        for di, dj in _N8:
            jj, ii = j + dj, i + di
            if 0 <= jj < nz and 0 <= ii < nx:
                dist = math.sqrt(2.0) if di and dj else 1.0
                g = (flat[k] - flat[jj * nx + ii]) / dist
                if g > best:
                    best, bk = g, jj * nx + ii
        if bk >= 0:
            acc[bk] += acc[k]
    A = acc.reshape(nz, nx)
    t = np.clip((np.log(A) - math.log(start)) / max(1e-6, math.log(full) - math.log(start)), 0, 1)
    cut = depth * t * t * (3 - 2 * t)
    for _ in range(width):
        cut = np.maximum(cut, 0.6 * np.maximum.reduce([_shift(cut, di, dj, 0.0) for di, dj in _N8[:4]]))
    H -= cut * w
    return A


def hollow_way(grid, path, depth, floor_half, top_half, smooth=24.0, ends=15.0, scale=None):
    """Sink a lane: along `path` the floor is the ground smoothed over `smooth` m of path, minus `depth`; the banks
    ease back to the ground between floor_half and top_half. Only lowers. The depth eases in over `ends` m at both
    ends, and scale(x, z) -> 0..1 (optional) scales it where the lane meets other roads (a level crossing)."""
    L = polyline_length(path)
    n = max(2, int(L / 3.0))
    samp = [grid.sample(*point_at(path, L * k / n)[0]) for k in range(n + 1)]
    r = max(1, int(smooth / 3.0 / 2))
    sm = [sum(samp[max(0, k - r):k + r + 1]) / len(samp[max(0, k - r):k + r + 1]) for k in range(n + 1)]
    x0, z0, x1, z1 = poly_bounds(path)
    pad = top_half + 4

    def fn(x, z, h):
        d, s, LL = polyline_dist((x, z), path)
        if d >= top_half:
            return h
        k = min(1.0, s / ends, (LL - s) / ends) if ends > 0 else 1.0
        k = k * k * (3 - 2 * k)
        if scale is not None:
            k *= scale(x, z)
        if k <= 0.0:
            return h
        f = sm[min(n, max(0, int(round(s / max(LL, 1e-6) * n))))] - depth * k
        if d <= floor_half:
            t = 0.0
        else:
            t = (d - floor_half) / (top_half - floor_half)
            t = t * t * (3 - 2 * t)
        return min(h, f + (h - f) * t)
    grid.apply(fn, (x0 - pad, z0 - pad, x1 + pad, z1 + pad))


def slope_report(grid, paths, step=2.0):
    """[(max grade deg, mean grade deg, length m)] along each polyline, sampled every `step` m."""
    out = []
    for p in paths:
        pts = resample(p, step)
        g = []
        for a, b in zip(pts, pts[1:]):
            dh = abs(grid.sample(*b) - grid.sample(*a))
            run = math.hypot(b[0] - a[0], b[1] - a[1])
            if run > 1e-6:
                g.append(math.degrees(math.atan2(dh, run)))
        out.append((max(g) if g else 0.0, sum(g) / len(g) if g else 0.0, polyline_length(p)))
    return out


# ---------------------------------------------------------------- the engine's pads (GX1), for seating on relief
_FINE = KCELL / 7.0   # terrain fine-tile spacing (the engine pad's growth)


def _fp_samples(hx, hz):
    """terrain_blend.cpp footprintSamples(half, false): a 5 x 5 lattice and the perimeter every ~0.5 m."""
    out = [(-hx + hx * 0.5 * i, -hz + hz * 0.5 * j) for j in range(5) for i in range(5)]
    nx, nz = max(2, math.ceil(hx * 2)), max(2, math.ceil(hz * 2))
    out += [(-hx + 2 * hx * i / nx, sz) for i in range(nx + 1) for sz in (-hz, hz)]
    out += [(sx, -hz + 2 * hz * j / nz) for j in range(1, nz) for sx in (-hx, hx)]
    return out


def engine_ground(b, ring_min=2.0, ring_max=3.5, ring_per_m=1.5, min_delta=0.05, skirt=0.9):
    """The ground the engine draws after its own building pads (terrain_blend.cpp blendBuildStamps + terrain_cuts.cpp
    stampEval): every padded building whose footprint is not already level within min_delta of its floor gets a pad,
    its ring blends into the ground in instance order and every pinned footprint is exactly its floor. Mapgen's own
    pads are on the 3.1 m grid, so where two buildings stand close at different heights the later grid pad leaves the
    earlier footprint uneven and the engine re-pads it with a ring that reaches the props between them.
    Returns (ground(x, z), number of engine pads). Settings: content/settings/terrain_blend.json."""
    from .geom import local_to_world
    base = b.ground
    stamps = []
    for e in b.inst:
        a = b.cat[e["asset"]]
        x, y, z = e["pos"]
        if not b.padded(a) or not b.grid.in_build(x, z):
            continue
        ew = a.raw.get("earthwork") if isinstance(a.raw.get("earthwork"), dict) else None
        hx, hz, ox, oz = a.half[0], a.half[1], 0.0, 0.0
        if ew and ew.get("core"):
            c = ew["core"]; hx, hz = float(c[0]), float(c[1])
            if len(c) >= 4:
                ox, oz = float(c[2]), float(c[3])
        cx, cz = local_to_world((x, z), e["yaw"], ox, oz)
        dmax = max(abs(base(*local_to_world((cx, cz), e["yaw"], lx, lz)) - y) for lx, lz in _fp_samples(hx, hz))
        if dmax < min_delta and not ew:
            continue
        ring = min(ring_max, max(ring_min, ring_min + ring_per_m * dmax))
        grow = 0.0 if ew else _FINE
        r = math.hypot(hx, hz) + grow + ring + 0.5
        cs, sn = math.cos(math.radians(e["yaw"])), math.sin(math.radians(e["yaw"]))
        stamps.append((cx, cz, cs, sn, hx, hz, y, ring, grow, 0.0 if ew else skirt, (cx - r, cz - r, cx + r, cz + r)))

    def rect_out(lx, lz, hx, hz):
        return math.hypot(max(abs(lx) - hx, 0.0), max(abs(lz) - hz, 0.0))

    def ground(x, z, *args, **kw):
        h = base(x, z, *args, **kw)
        near = [s for s in stamps if s[10][0] <= x <= s[10][2] and s[10][1] <= z <= s[10][3]]
        loc = []
        for s in near:
            dx, dz = x - s[0], z - s[1]
            loc.append((s, rect_out(dx * s[2] - dz * s[3], dx * s[3] + dz * s[2], s[4], s[5])))
        for s, ro in loc:   # 1. rings, in order
            d = ro - s[8]
            if 0 < d < s[7]:
                edge = h + s[9] if s[9] > 0 and s[6] - h > s[9] else s[6]
                t = d / s[7]
                t = t * t * (3 - 2 * t)
                h = edge + (h - edge) * t
        for s, ro in loc:   # 4. pinned footprints
            if ro <= s[8]:
                h = s[6]
        return h
    return ground, len(stamps)


def reseat_on_engine_ground(b, skip=lambda e: False):
    """Re-seat every free-standing piece that is not padded itself on engine_ground(): the lowest of its footprint
    samples (trees and plants: under the trunk, 5 cm in), so no edge floats over a ring the engine adds; what rests on
    a piece or is its companion moves with it. Returns (pieces moved, largest move m, engine pads)."""
    from .geom import local_to_world
    lifted = 0
    for _ in range(3):   # a padded building the engine leaves alone (already level) but a neighbour's ring tilts: 6 cm
        g, n_pads = engine_ground(b)   # up makes the engine pad (and pin) it as well
        hit = []
        for e in b.inst:
            a = b.cat[e["asset"]]
            if not b.padded(a) or not b.grid.in_build(e["pos"][0], e["pos"][2]):
                continue
            hx, hz = a.half
            if max(abs(b.ground(*local_to_world((e["pos"][0], e["pos"][2]), e["yaw"], lx, lz)) - e["pos"][1]) for lx, lz in _fp_samples(hx, hz)) >= 0.05:
                continue   # the engine pads it: pinned
            if max(abs(g(*local_to_world((e["pos"][0], e["pos"][2]), e["yaw"], lx, lz)) - e["pos"][1]) for lx, lz in _fp_samples(hx, hz)) > 0.04:
                hit.append(e)
        if not hit:
            break
        for e in hit:
            e["pos"][1] += 0.06
            for c in b.inst:
                if c["extra"].get("support") == e["id"] or (c.get("host") == e["id"] and not c.get("free")):
                    c["pos"][1] += 0.06
        lifted += len(hit)
    keep = b.ground
    b.ground = g
    delta, worst = {}, 0.0
    try:
        for e in b.inst:
            a = b.cat[e["asset"]]
            if ("host" in e and not e.get("free")) or "support" in e["extra"] or b.padded(a) or a.path_on or skip(e):
                continue
            if a.raw.get("nature") or a.raw.get("scatter") or a.raw.get("line"):
                continue
            x, z = e["pos"][0], e["pos"][2]
            was = e["pos"][1] - min(keep(*s[:2]) for s in b._samples(a, x, z, e["yaw"]))
            if was > 0.35 or was < -0.5:   # mounted on a wall or buried on purpose (an explicit height off the ground)
                continue
            if a.tree:
                y = min(g(x + dx, z + dz) for dx, dz in ((0, 0), (0.3, 0), (-0.3, 0), (0, 0.3), (0, -0.3))) - 0.05
            else:
                y = min(s[2] for s in b._samples(a, x, z, e["yaw"]))
            d = y - e["pos"][1]
            if abs(d) > 0.003:
                e["pos"][1] = y
                delta[e["id"]] = d
                worst = max(worst, abs(d))
    finally:
        b.ground = keep
    for _ in range(3):
        for c in b.inst:
            hid = c["extra"].get("support", c.get("host") if not c.get("free") else None)
            if hid in delta and c["id"] not in delta:
                c["pos"][1] += delta[hid]
                delta[c["id"]] = delta[hid]
    return len(delta), worst, n_pads, lifted


def _selfcheck():
    class G:   # a 40 x 40 grid with a cone
        nx = nz = 40
        org_x = org_z = 0.0
    from array import array
    g = G()
    g.h = array('d', [0.0]) * (40 * 40)
    H = _arr(g)
    jj, ii = np.mgrid[0:40, 0:40]
    H[...] = np.maximum(0.0, 20.0 - 0.8 * np.hypot(ii - 20, jj - 20) * KCELL)
    v0 = H.sum()
    thermal_erosion(g, np.ones_like(H), iters=30)
    assert abs(H.sum() - v0) < 1e-6 * max(1.0, abs(v0)), "thermal erosion conserves material"
    assert H.max() < 20.0, "thermal erosion lowers the peak"
    hydraulic_erosion(g, np.ones_like(H), iters=20)
    assert np.isfinite(H).all()
    keep = H.copy()
    hydraulic_erosion(g, np.zeros_like(H), iters=5)
    assert np.array_equal(keep, H), "weight 0 keeps the ground exactly"


if __name__ == "__main__":
    _selfcheck()
    print("relief selfcheck ok")
