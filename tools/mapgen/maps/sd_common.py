"""SD2: shared generator for the Search and Destroy maps (docs/search-and-destroy.md, map contract).

A map is a plan on a square grid (CELL m) drawn as ASCII, north at the top. Each character is one cell:
  '#'  solid (no walking; decorative masses may stand there)
  any other character: a walkable region; cells with the same character form one region
A wall goes on every edge between two cells of different regions, or a region and solid ground, unless the edge is in
the plan's links:  {((i, j), (i2, j2)): kind}, kind "open" (no wall), or a wall-piece asset id (a breach or culvert
segment, a gate). Cell (i, j): column i from the west, row j from the south.
The map module supplies the wall asset, the region roles (spawns, sites, callouts), the cover and the look."""
import math
import os

from mapgen import (GroundPaint, Grid, Frame, MapBuilder, ScenarioBuilder, write_map, write_template, write_summary, asset_usage,
                    rng, look_yaw)


class Plan:
    """stretch_cols / stretch_rows: original indices drawn twice (the layout scale knob: routes grow by a bay each).
    Every public call (cc, links in the constructor) takes ORIGINAL indices; a duplicated cell's cc is the middle of its
    two copies; a link across the stretched axis is kept on the first copy only (a door stays one bay wide)."""
    def __init__(self, rows, cell, links=None, stretch_cols=(), stretch_rows=()):
        rows = [r.rstrip("\n") for r in rows]
        nx0 = max(len(r) for r in rows)
        rows = [r.ljust(nx0, "#") for r in rows]
        sc, sr = set(stretch_cols), set(stretch_rows)
        nz0 = len(rows)
        self.mi, self.mj = {}, {}   # original index -> (first, last) new index
        k = 0
        for i in range(nx0):
            self.mi[i] = (k, k + (i in sc))
            k += 1 + (i in sc)
        k = 0
        for j in range(nz0):
            self.mj[j] = (k, k + (j in sr))
            k += 1 + (j in sr)
        wide = ["".join(r[i] * (2 if i in sc else 1) for i in range(nx0)) for r in rows]
        tall = []
        for jr, r in enumerate(wide):   # rows are listed north first: original row j = nz0 - 1 - jr
            tall.extend([r] * (2 if (nz0 - 1 - jr) in sr else 1))
        self.rows = tall
        self.nz = len(self.rows)
        self.nx = len(self.rows[0])
        self.cell = cell
        self.x0 = -self.nx * cell / 2.0
        self.z0 = -self.nz * cell / 2.0
        self.links = {}
        for (a, b), kind in (links or {}).items():
            if a[0] != b[0]:   # across x: the facing copies, on the first copy of the row
                lo, hi = (a, b) if a[0] < b[0] else (b, a)
                na, nb = (self.mi[lo[0]][1], self.mj[lo[1]][0]), (self.mi[hi[0]][0], self.mj[hi[1]][0])
            else:
                lo, hi = (a, b) if a[1] < b[1] else (b, a)
                na, nb = (self.mi[lo[0]][0], self.mj[lo[1]][1]), (self.mi[hi[0]][0], self.mj[hi[1]][0])
            self.links[frozenset((na, nb))] = kind

    def at(self, i, j):
        if i < 0 or j < 0 or i >= self.nx or j >= self.nz:
            return "#"
        r = self.rows[self.nz - 1 - j]
        return r[i] if i < len(r) else "#"

    def walk(self, i, j):
        return self.at(i, j) not in "# "

    def _cc(self, i, j):
        return (self.x0 + (i + 0.5) * self.cell, self.z0 + (j + 0.5) * self.cell)

    def cc(self, i, j):
        """Centre of ORIGINAL cell (i, j) (the middle of its copies when stretched)."""
        a, b = self.mi.get(i, (i, i)), self.mj.get(j, (j, j))
        return (self.x0 + ((a[0] + a[1]) / 2.0 + 0.5) * self.cell, self.z0 + ((b[0] + b[1]) / 2.0 + 0.5) * self.cell)

    def cells(self, ch):
        return [(i, j) for j in range(self.nz) for i in range(self.nx) if self.at(i, j) == ch]

    def centre(self, ch):
        cs = self.cells(ch)
        xs = [self._cc(*c) for c in cs]
        return (sum(p[0] for p in xs) / len(xs), sum(p[1] for p in xs) / len(xs))

    def edges(self):
        """Every cell edge that needs a wall: (x, z, yaw, kind) at the edge's midpoint; kind None = plain wall."""
        return [e for e in self.all_edges() if e[3] != "open"]

    def all_edges(self):
        """Every edge between different regions (or a region and solid ground), open links included."""
        out = []
        for j in range(-1, self.nz):
            for i in range(-1, self.nx):
                for di, dj in ((1, 0), (0, 1)):
                    a, b = (i, j), (i + di, j + dj)
                    wa, wb = self.walk(*a), self.walk(*b)
                    if not wa and not wb:
                        continue
                    if wa and wb and self.at(*a) == self.at(*b):
                        continue
                    kind = self.links.get(frozenset((a, b)))
                    x, z = self._cc(*a)
                    if di:
                        out.append((x + self.cell / 2, z, 90.0, kind, a, b))
                    else:
                        out.append((x, z + self.cell / 2, 0.0, kind, a, b))
        return out

    def check_links(self):
        bad = []
        for k in self.links:
            a, b = tuple(k)
            if abs(a[0] - b[0]) + abs(a[1] - b[1]) != 1:
                bad.append("link %s-%s: not neighbours" % (a, b))
            elif not (self.walk(*a) and self.walk(*b)):
                bad.append("link %s-%s: a side is solid" % (a, b))
        return bad


def place_walls(b, plan, wall, y=0.0, tags=("wall",)):
    n = 0
    for x, z, yaw, kind, a, c in plan.edges():
        aid = kind or wall
        b.place(aid, x, z, yaw, check=False, cls="none", y=y, tags=set(tags))
        n += 1
    return n


def spread(points, centre, n, min_gap=1.6, radius=9.0):
    """n spawn spots: the given candidate points first (in order), at least min_gap apart, within radius of centre."""
    out = []
    for p in points:
        if math.hypot(p[0] - centre[0], p[1] - centre[1]) <= radius and all(math.hypot(p[0] - q[0], p[1] - q[1]) >= min_gap for q in out):
            out.append(p)
        if len(out) == n:
            break
    return out


# ---------------------------------------------------------------- dressing
def _seg_dist(p, a, b):
    ax, az = b[0] - a[0], b[1] - a[1]
    l2 = ax * ax + az * az
    t = 0.0 if l2 < 1e-9 else max(0.0, min(1.0, ((p[0] - a[0]) * ax + (p[1] - a[1]) * az) / l2))
    return math.hypot(p[0] - a[0] - ax * t, p[1] - a[1] - az * t)


def rect_ok(plan, x, z, hw, hd, walkable=False, margin=0.6):
    """The rectangle (centre, half sizes) lies in solid cells (walkable=False) or inside one walkable region (True),
    keeping `margin` from the other kind."""
    c = plan.cell
    pts = [(x + sx * (hw + margin), z + sz * (hd + margin)) for sx in (-1, -0.5, 0, 0.5, 1) for sz in (-1, -0.5, 0, 0.5, 1)]
    regs = set()
    for px, pz in pts:
        i, j = int(math.floor((px - plan.x0) / c)), int(math.floor((pz - plan.z0) / c))
        regs.add(plan.at(i, j))
    if walkable:
        return len(regs) == 1 and "#" not in regs
    return regs == {"#"}


def place_masses(b, plan, cands, R, y, ring=2, tags=("mass",)):
    """Decorative buildings on solid ground (behind the walls, seen above them): largest first, greedy on a 1 m step,
    never within 0.6 m of a walkable cell; out to `ring` cells past the plan's edge."""
    placed, n = [], 0
    c = plan.cell
    xs = [plan.x0 - ring * c + 0.5 * k for k in range(int((plan.nx + 2 * ring) * c * 2))]
    zs = [plan.z0 - ring * c + 0.5 * k for k in range(int((plan.nz + 2 * ring) * c * 2))]
    for aid, fw, fd in sorted(cands, key=lambda t: -t[1] * t[2]):
        for _ in range(3):   # up to three of each
            spots = [(x, z, yaw) for x in xs[::2] for z in zs[::2] for yaw in (0.0, 90.0)]
            R.shuffle(spots)
            for x, z, yaw in spots:
                hw, hd = (fw / 2, fd / 2) if yaw == 0.0 else (fd / 2, fw / 2)
                if not rect_ok(plan, x, z, hw, hd):
                    continue
                if any(abs(x - px) < hw + phw + 1.0 and abs(z - pz) < hd + phd + 1.0 for px, pz, phw, phd in placed):
                    continue
                b.place(aid, x, z, yaw + R.choice((0.0, 180.0)), check=False, cls="none", y=y, tags=set(tags))
                placed.append((x, z, hw, hd))
                n += 1
                break
    return n


def door_points(plan, ch):
    """Midpoints of the open / gated edges of region ch (its doorways)."""
    out = []
    for x, z, yaw, kind, a, b2 in plan.all_edges():
        if (plan.at(*a) == ch) != (plan.at(*b2) == ch) and kind is not None:
            out.append((x, z))
    return out


def place_cover(b, plan, ch, items, R, y, lane=2.2, tries=400, tags=("cover",)):
    """items: [(asset, half w, half d, count)]: scattered in region ch, never within `lane` m of a straight line between
    two of its doorways (the routes stay open) and 1.0 m off its walls."""
    doors = door_points(plan, ch)
    segs = [(doors[a], doors[k]) for a in range(len(doors)) for k in range(a + 1, len(doors))]
    cells = plan.cells(ch)
    placed, n = [], 0
    for aid, hw, hd, cnt in items:
        for _ in range(cnt):
            for _t in range(tries):
                i, j = R.choice(cells)
                cx, cz = plan._cc(i, j)
                x, z = cx + R.uniform(-plan.cell / 2, plan.cell / 2), cz + R.uniform(-plan.cell / 2, plan.cell / 2)
                yaw = R.choice((0.0, 90.0)) + R.uniform(-8, 8)
                if not rect_ok(plan, x, z, max(hw, hd), max(hw, hd), walkable=True, margin=1.0):
                    continue
                if any(_seg_dist((x, z), s0, s1) < lane + max(hw, hd) for s0, s1 in segs):
                    continue
                if any(math.hypot(x - px, z - pz) < max(hw, hd) + pr + 1.4 for px, pz, pr in placed):
                    continue
                b.place(aid, x, z, yaw, check=False, cls="none", y=y, tags=set(tags))
                placed.append((x, z, max(hw, hd)))
                n += 1
                break
    return n
