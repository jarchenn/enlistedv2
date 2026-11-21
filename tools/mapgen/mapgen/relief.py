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


# ---------------------------------------------------------------- VOT T2: the fine layer (the engine's sculpt tiles)
class FineLayer:
    """Sub-cell relief written as the map's "terrainSculpt": per 5 x 5-cell tile (15.7 m) a 36 x 36 lattice of metres
    added to the base heightfield (terrain.h kFineN, spacing KCELL / 7 = 0.45 m). Retaining-wall steps, kerbs, ditches,
    ruts and level seats under rigid pieces live here; the 3.1 m base grid cannot hold them.

      fine = FineLayer(grid)
      fine.add(fn, bounds)        fn(X, Z, G) -> new ground (numpy arrays; G = the current ground: base + fine)
      fine.ground(x, z)           the engine's ground before its pads (base + sculpt), as Terrain::height
      fine.to_json()              {"fineN": 36, "tiles": [{"key": k, "d": [...]}]}

    Keys are tz * tilesX + tx over the whole grid (no terrainGen ring). Tiles whose deltas are all under 1 mm are dropped;
    neighbours share their edge samples (the same fn at the same points), so a feature must fade out inside `bounds`."""
    N = 36

    def __init__(self, grid):
        self.g = grid
        self.ts = KCELL * 5
        self.fs = self.ts / 35.0
        self.tx_n, self.tz_n = (grid.nx - 1) // 5, (grid.nz - 1) // 5
        self.d = {}

    def _xz(self, tx, tz):
        i = np.arange(self.N) * self.fs
        return np.meshgrid(self.g.org_x + tx * self.ts + i, self.g.org_z + tz * self.ts + i)

    def base(self, X, Z):
        """Vectorised Grid.sample (the a-d diagonal split)."""
        H = _arr(self.g)
        fx = np.clip((X - self.g.org_x) / KCELL, 0, self.g.nx - 1.001); fz = np.clip((Z - self.g.org_z) / KCELL, 0, self.g.nz - 1.001)
        xi, zi = fx.astype(int), fz.astype(int); tx, tz = fx - xi, fz - zi
        a, b, c, d = H[zi, xi], H[zi, xi + 1], H[zi + 1, xi], H[zi + 1, xi + 1]
        return np.where(tx >= tz, a + (b - a) * tx + (d - b) * tz, a + (c - a) * tz + (d - c) * tx)

    def tiles_in(self, bounds):
        x0, z0, x1, z1 = bounds
        a0 = max(0, int(math.floor((x0 - self.g.org_x) / self.ts))); a1 = min(self.tx_n - 1, int(math.floor((x1 - self.g.org_x) / self.ts)))
        b0 = max(0, int(math.floor((z0 - self.g.org_z) / self.ts))); b1 = min(self.tz_n - 1, int(math.floor((z1 - self.g.org_z) / self.ts)))
        return [(tx, tz) for tz in range(b0, b1 + 1) for tx in range(a0, a1 + 1)]

    def add(self, fn, bounds):
        for tx, tz in self.tiles_in(bounds):
            k = tz * self.tx_n + tx
            X, Z = self._xz(tx, tz)
            B = self.base(X, Z)
            D = self.d.get(k, np.zeros((self.N, self.N)))
            G = fn(X, Z, B + D)
            D = G - B
            if np.abs(D).max() >= 0.001:
                self.d[k] = D
            elif k in self.d:
                del self.d[k]

    def ground(self, x, z):
        h = self.g.sample(x, z)
        tx, tz = int(math.floor((x - self.g.org_x) / self.ts)), int(math.floor((z - self.g.org_z) / self.ts))
        D = self.d.get(tz * self.tx_n + tx)
        if D is None:
            return h
        fx = min(max((x - self.g.org_x - tx * self.ts) / self.fs, 0.0), 34.9999); fz = min(max((z - self.g.org_z - tz * self.ts) / self.fs, 0.0), 34.9999)
        i, j = int(fx), int(fz); u, v = fx - i, fz - j
        a, b, c, d = D[j, i], D[j, i + 1], D[j + 1, i], D[j + 1, i + 1]
        return h + (a + (b - a) * u + (d - b) * v if u >= v else a + (c - a) * v + (d - c) * u)

    def install(self, b):
        """Every later MapBuilder.ground() (seating, cuts) sees the fine layer too."""
        g0 = b.ground
        fine = self

        def ground(x, z, ignore_owner=None, before_fills=False):
            return g0(x, z, ignore_owner, before_fills) + (fine.ground(x, z) - fine.g.sample(x, z))
        b.ground = ground
        return g0

    def to_json(self):
        return {"fineN": self.N, "tiles": [{"key": int(k), "d": [round(float(v), 3) for v in self.d[k].ravel()]} for k in sorted(self.d)]}


def fine_pad(fine, b, e, ring=0.9, keep_low=True):
    """A rigid piece that is not padded by the engine (a facade, a cart, gate piers) gets a level seat in the fine
    layer: its footprint at its own base height, eased into the ground over `ring` m. Returns the largest change (m)."""
    from .geom import local_to_world
    a = b.cat[e["asset"]]
    x, y, z = e["pos"]
    hx, hz = a.half
    cs, sn = math.cos(math.radians(e["yaw"])), math.sin(math.radians(e["yaw"]))
    r = math.hypot(hx, hz) + ring + 0.5
    worst = [0.0]

    def fn(X, Z, G):
        dx, dz = X - x, Z - z
        lx, lz = dx * cs - dz * sn, dx * sn + dz * cs
        d = np.hypot(np.maximum(np.abs(lx) - hx, 0), np.maximum(np.abs(lz) - hz, 0))
        t = np.clip(d / ring, 0, 1)
        t = t * t * (3 - 2 * t)
        out = y + (G - y) * t
        worst[0] = max(worst[0], float(np.abs(out - G).max()))
        return out
    fine.add(fn, (x - r, z - r, x + r, z + r))
    return worst[0]


def level_rigid_pieces(fine, b, min_area=4.0, max_range=0.7, skip=lambda e: False):
    """VOT T2: every rigid piece the engine does not pad (facades, carts, gate piers, market stalls: footprint >= min_area
    m2, not resting on another, not a wall line) moves to the ground at its centre and gets a level seat in the fine
    layer (fine_pad), so it neither floats on its low side nor sinks on its high side and a door sill stays at the
    ground. Pieces whose footprint spans more than max_range m of ground (a wall carrying a terrace step) keep their
    seat. Returns (pieces levelled, largest ground change m)."""
    n, worst = 0, 0.0
    for e in b.inst:
        a = b.cat[e["asset"]]
        if (b.padded(a) and b.grid.in_build(e["pos"][0], e["pos"][2])) or a.path_on or a.tree or ("host" in e and not e.get("free")) or "support" in e["extra"] or skip(e):
            continue   # the engine pads buildings inside the build area only (terrain_blend.cpp)
        if a.raw.get("nature") or a.raw.get("scatter") or a.raw.get("line") or a.fw * a.fd < min_area:
            continue
        hs = [s[2] for s in b._samples(a, e["pos"][0], e["pos"][2], e["yaw"], inset=0.0)]
        if max(hs) - min(hs) > max_range or e["pos"][1] - min(hs) > 0.35 or e["pos"][1] - min(hs) < -0.5:
            continue   # spans a step, or mounted / buried on purpose
        y = b.ground(e["pos"][0], e["pos"][2])
        d = y - e["pos"][1]
        e["pos"][1] = y
        for c in b.inst:
            if c["extra"].get("support") == e["id"] or (c.get("host") == e["id"] and not c.get("free")):
                c["pos"][1] += d
        worst = max(worst, fine_pad(fine, b, e))
        n += 1
    return n, worst


def _poly_dist(X, Z, P):
    """Distance, arclength and signed side (+1 left) from every (X, Z) to the polyline P (n x 2 array)."""
    best = np.full(X.shape, 1e9); S = np.zeros(X.shape); side = np.ones(X.shape)
    acc = 0.0
    for k in range(len(P) - 1):
        ax, az = P[k]; bx, bz = P[k + 1]
        ux, uz = bx - ax, bz - az
        L = math.hypot(ux, uz)
        if L < 1e-6:
            continue
        t = np.clip(((X - ax) * ux + (Z - az) * uz) / (L * L), 0, 1)
        d = np.hypot(X - (ax + ux * t), Z - (az + uz * t))
        m = d < best
        best = np.where(m, d, best); S = np.where(m, acc + t * L, S)
        side = np.where(m, np.where(ux * (Z - az) - uz * (X - ax) > 0, 1.0, -1.0), side)
        acc += L
    return best, S, side, acc


def fine_road(fine, path, half, crown=0.06, ruts=0.0, gutter=0.0, verge=0.0, ditch=0.0, sunk=0.0, sunk_half=None,
              keep=None, ends=6.0, seed=7, kerb=0.0):
    """VOT T2: a road's cross-section in the fine layer, on top of the ground it already follows.
      crown   m the centre stands above the edges (camber)        ruts   m two wheel tracks 0.85 m off the centre sink
      gutter  m the edges sink (a paved street's water line)      verge  m a raised grass verge beyond the edge
      kerb    m a kerbed footway: a 1.6 m pavement this far above the road edge, a sharp step at the edge (0: none)
      ditch   m a roadside ditch beyond the verge (V, rounded)     sunk   m a hollow way: the floor this far under the
              smoothed ground along the road, banks easing back over sunk_half (half-width at the top, m)
    keep(X, Z) -> 0..1 scales the whole profile (0 at junctions, bridges, building pads); it eases in over `ends` m."""
    P = np.array(path, dtype=float)
    reach = half + (1.2 if verge else 0) + (3.8 if ditch else 0) + (2.4 if kerb else 0) + 0.6
    if sunk:
        reach = max(reach, sunk_half + 0.6)
    x0, z0 = P.min(axis=0) - reach - 1; x1, z1 = P.max(axis=0) + reach + 1
    if sunk:   # the floor follows the ground along the lane, smoothed over ~24 m
        L = sum(math.hypot(*(P[k + 1] - P[k])) for k in range(len(P) - 1))
        n = max(2, int(L / 3.0))
        sx = []; acc = 0.0; k = 0
        for q in range(n + 1):
            s_ = L * q / n
            while k < len(P) - 2 and acc + math.hypot(*(P[k + 1] - P[k])) < s_:
                acc += math.hypot(*(P[k + 1] - P[k])); k += 1
            seg = math.hypot(*(P[k + 1] - P[k]))
            t = min(1.0, max(0.0, (s_ - acc) / max(seg, 1e-6)))
            sx.append(fine.ground(*(P[k] + (P[k + 1] - P[k]) * t)))
        r = 4
        sm = np.array([np.mean(sx[max(0, q - r):q + r + 1]) for q in range(n + 1)])

    def fn(X, Z, G):
        d, S, side, LL = _poly_dist(X, Z, P)
        if (d < reach).sum() == 0:
            return G
        k = np.clip(np.minimum(S, LL - S) / ends, 0, 1); k = k * k * (3 - 2 * k)
        if keep is not None:
            k = k * keep(X, Z)
        off = np.zeros_like(G)
        u = np.clip(d / half, 0, 1)
        off += crown * (1 - u * u) * (d < half)
        if ruts:
            var = 0.6 + 0.4 * np.sin(S / 7.0 + seed) * np.cos(S / 3.1 + 2 * seed)
            off -= ruts * var * (np.exp(-((d * side - 0.85) / 0.2) ** 2) + np.exp(-((d * side + 0.85) / 0.2) ** 2))
        if gutter:
            off -= gutter * np.exp(-((d - half + 0.25) / 0.25) ** 2)
        e = half
        if kerb:   # the footway: up over 0.2 m at the road edge (the kerb face), down again over 0.6 m past 1.6 m
            off += kerb * np.clip((d - e) / 0.2, 0, 1) * (1 - np.clip((d - e - 1.6) / 0.6, 0, 1))
        if verge:
            t = np.clip((d - e) / 0.5, 0, 1); off += verge * t * t * (3 - 2 * t) * (d < e + 1.6)
            t2 = np.clip((d - e - 1.2) / 0.4, 0, 1); off -= verge * t2 * (d < e + 1.6)
            e += 1.2
        if ditch:
            c = e + 1.7
            t = np.clip(1 - np.abs(d - c) / 1.7, 0, 1)
            off -= ditch * t * t * (3 - 2 * t) * (d < e + 3.4)
        out = G + off * k
        if sunk:
            q = np.clip(np.round(S / max(LL, 1e-6) * (len(sm) - 1)).astype(int), 0, len(sm) - 1)
            floor = sm[q] - sunk
            t = np.clip((d - half) / (sunk_half - half), 0, 1)
            t = t * t * (3 - 2 * t)
            prof = floor + (G - floor) * t
            out = np.where(d < sunk_half, G + (np.minimum(prof, G) - G) * k + (off * k) * (d < half), out)
        return np.where(d < reach, out, G)
    fine.add(fn, (x0, z0, x1, z1))


def fine_track(fine, path, w=1.8, crown=0.05, drain_bias=0.02, rut_depth=(0.03, 0.07), rut_sigma=(0.22, 0.30),
               track_offset=0.8, drain=0.0, drain_side=1, bank=0.0, relief=0.03, keep=None, ends=6.0, seed=11):
    """VFX rev3 spec 4.3: a road's cross-section from its spline, in the fine layer (0.45 m, the collision surface at every
    tier). zRoad = zSpline(s) - C - R + boundedRelief, zFinal = lerp(zExisting, zRoad, rim) with
      C   = (crown - drainageBias*sign(x)) * |x| / w       capped cross-slope, crown centre-to-edge, drainage to one side
      R   = left / right ruts as Gaussians at +-track_offset, depth and sigma moving slowly along s
      rim = 1 - ss(0.72 w, 1.22 w, |x|)                    the fade into the authored terrain
    Beyond the rim: a shallow drain on `drain_side` (+1 = left of travel) and a bank rising `bank` m on the other side.
    zSpline is the ground along the road smoothed over ~24 m. Ruts are kept at sigma >= 0.22 m: the 0.45 m mesh carries
    what it can resolve (spec 4.3's split); finer relief is the POM height's."""
    P = np.array(path, dtype=float)
    reach = 1.22 * w + (2.8 if drain else 0.0) + (3.4 if bank else 0.0) + 0.6
    x0, z0 = P.min(axis=0) - reach - 1; x1, z1 = P.max(axis=0) + reach + 1
    L = sum(math.hypot(*(P[k + 1] - P[k])) for k in range(len(P) - 1))
    n = max(2, int(L / 3.0)); sx = []; acc = 0.0; k = 0
    for q in range(n + 1):
        s_ = L * q / n
        while k < len(P) - 2 and acc + math.hypot(*(P[k + 1] - P[k])) < s_:
            acc += math.hypot(*(P[k + 1] - P[k])); k += 1
        seg = math.hypot(*(P[k + 1] - P[k])); t = min(1.0, max(0.0, (s_ - acc) / max(seg, 1e-6)))
        sx.append(fine.ground(*(P[k] + (P[k + 1] - P[k]) * t)))
    sm = np.array([np.mean(sx[max(0, q - 4):q + 5]) for q in range(n + 1)])
    ss = lambda e0, e1, v: np.clip((v - e0) / (e1 - e0), 0, 1) ** 2 * (3 - 2 * np.clip((v - e0) / (e1 - e0), 0, 1))
    slow = lambda lo, hi, f, ph, S: lo + (hi - lo) * (0.5 + 0.5 * np.sin(S / f + ph))

    def fn(X, Z, G):
        d, S, side, LL = _poly_dist(X, Z, P)
        if (d < reach).sum() == 0:
            return G
        x = d * side
        q = np.clip(np.round(S / max(LL, 1e-6) * n).astype(int), 0, n)
        wS = w * (0.93 + 0.07 * np.sin(S / 23.0 + seed))                     # width moves slowly along s
        C = (crown - drain_bias * np.sign(x) * drain_side) * np.abs(x) / np.maximum(wS, 0.1)
        sig = slow(rut_sigma[0], rut_sigma[1], 17.0, seed, S)
        rut = lambda c: np.exp(-0.5 * ((x - c) / sig) ** 2)
        Rr = slow(*rut_depth, 13.0, seed + 1, S) * rut(-track_offset) + slow(*rut_depth, 11.0, seed + 2, S) * rut(track_offset)
        br = relief * np.sin(S / 1.9 + 0.7 * x) * np.cos(S / 3.7 - 1.3 * x)   # bounded relief, |br| <= relief
        zRoad = sm[q] - C - Rr + br
        rim = 1 - ss(0.72 * wS, 1.22 * wS, np.abs(x))
        out = G + (zRoad - G) * rim
        e = 1.22 * wS
        if drain:   # a shallow drain on one shoulder, rounded V centred 1.2 m past the rim
            t = np.clip(1 - np.abs(np.abs(x) - (e + 1.2)) / 1.2, 0, 1)
            out -= drain * t * t * (3 - 2 * t) * (side == drain_side)
        if bank:    # the other shoulder rises into the forest floor
            out += bank * ss(e, e + 2.6, np.abs(x)) * (1 - ss(e + 2.6, e + 3.4, np.abs(x)) * 0.35) * (side != drain_side)
        kk = np.clip(np.minimum(S, LL - S) / ends, 0, 1); kk = kk * kk * (3 - 2 * kk)
        if keep is not None:
            kk = kk * keep(X, Z)
        return np.where(d < reach, G + (out - G) * kk, G)
    fine.add(fn, (x0, z0, x1, z1))


def fine_bumps(fine, centre, radius, amp=0.12, scale=1.6, hollow=0.1, keep=None, seed=3):
    """VOT T2: foreground micro-relief around a viewpoint: low fbm bumps and a few shallow puddle hollows, fading out at
    `radius`; keep(X, Z) -> 0..1 protects roads, footprints and paths."""
    cx, cz = centre

    def fn(X, Z, G):
        r = np.hypot(X - cx, Z - cz)
        k = np.clip(1 - r / radius, 0, 1) ** 0.7
        if keep is not None:
            k = k * keep(X, Z)
        n = np.vectorize(lambda x, z: fbm(x / scale, z / scale, seed, 3))(X, Z) - 0.5
        h = np.vectorize(lambda x, z: fbm(x / 5.0, z / 5.0, seed + 11, 2))(X, Z)
        hol = -hollow * np.clip((h - 0.62) / 0.12, 0, 1)
        return G + (amp * 2 * n + hol) * k
    fine.add(fn, (cx - radius, cz - radius, cx + radius, cz + radius))


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
    H[...] = jj * 0.1 * KCELL   # a 10 % slope: a fine pad levels a 4 x 2 m footprint at its own height
    f = FineLayer(g)
    f.g.sample = lambda x, z: float(f.base(np.array(x), np.array(z)))

    class B:
        cat = {"p": type("A", (), {"half": (2.0, 1.0)})()}
    e = {"asset": "p", "pos": [60.0, 6.0, 60.0], "yaw": 30.0}
    fine_pad(f, B(), e)
    for lx, lz in ((0, 0), (1.9, 0.9), (-1.9, -0.9), (1.9, -0.9)):
        from .geom import local_to_world
        w = local_to_world((60.0, 60.0), 30.0, lx, lz)
        assert abs(f.ground(*w) - 6.0) < 0.02, (lx, lz, f.ground(*w))
    assert abs(f.ground(60.0, 75.0) - f.g.sample(60.0, 75.0)) < 1e-6, "beyond the ring the ground is untouched"
    H[...] = 10.0   # road profiles on flat ground
    f = FineLayer(g); f.g.sample = lambda x, z: float(f.base(np.array(x), np.array(z)))
    fine_road(f, [(20.0, 60.0), (100.0, 60.0)], 2.0, crown=0.06, ruts=0.08, verge=0.25, ditch=0.6)
    assert f.ground(60, 60) - 10 > 0.04 and f.ground(60, 62.6) - 10 > 0.15 and f.ground(60, 64.9) - 10 < -0.5 and abs(f.ground(60, 68.5) - 10) < 1e-3
    f = FineLayer(g); f.g.sample = lambda x, z: float(f.base(np.array(x), np.array(z)))
    fine_road(f, [(20.0, 60.0), (100.0, 60.0)], 2.0, sunk=1.9, sunk_half=4.5)
    assert f.ground(60, 60) - 10 < -1.75 and abs(f.ground(60, 65.2) - 10) < 1e-3, "a hollow way's banks stand over head height"


if __name__ == "__main__":
    _selfcheck()
    print("relief selfcheck ok")
