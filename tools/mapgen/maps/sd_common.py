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


def place_masses(b, plan, cands, R, y, ring=2, tags=("mass",), out=None, margin=0.6):
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
                if not rect_ok(plan, x, z, hw, hd, margin=margin):
                    continue
                if any(abs(x - px) < hw + phw + 1.0 and abs(z - pz) < hd + phd + 1.0 for px, pz, phw, phd in placed):
                    continue
                b.place(aid, x, z, yaw + R.choice((0.0, 180.0)), check=False, cls="none", y=y, tags=set(tags))
                placed.append((x, z, hw, hd))
                if out is not None:
                    out.append((aid, x, z, hw, hd))
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


def place_cover(b, plan, ch, items, R, y, lane=2.2, tries=400, tags=("cover",), keep=()):
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
                if any(math.hypot(x - kx, z - kz) < kr + max(hw, hd) for kx, kz, kr in keep):   # plant pads stay clear
                    continue
                if any(math.hypot(x - px, z - pz) < max(hw, hd) + pr + 1.4 for px, pz, pr in placed):
                    continue
                b.place(aid, x, z, yaw, check=False, cls="none", y=y, tags=set(tags))
                placed.append((x, z, max(hw, hd)))
                n += 1
                break
    return n


def outer(plan, cell):
    """A solid cell on (or beyond) the plan's outer ring: walls against it face the outside."""
    i, j = cell
    return i <= 0 or j <= 0 or i >= plan.nx - 1 or j >= plan.nz - 1


def place_walls2(b, plan, inner, perimeter, y=0.0, tags=("wall",)):
    """Walls with two looks: `perimeter` (razor wire) where the solid side is on the plan's outer ring, `inner` elsewhere;
    a link's own piece (breach, culvert, gate) wins."""
    n = {"inner": 0, "perimeter": 0, "link": 0}
    for x, z, yaw, kind, a, c in plan.edges():
        if kind:
            aid, key = kind, "link"
        else:
            solid = a if not plan.walk(*a) else c
            aid, key = (perimeter, "perimeter") if not plan.walk(*solid) and outer(plan, solid) else (inner, "inner")
            if isinstance(aid, (list, tuple)):   # variants, picked by the edge's position (stable between builds)
                hx, hz = int(math.floor(x / 3.0 + 0.5)), int(math.floor(z / 3.0 + 0.5))
                aid = aid[((hx * 73856093) ^ (hz * 19349663) ^ (hx * hz * 83492791)) % len(aid)]   # positions are multiples of 3 m: a real spatial hash
        b.place(aid, x, z, yaw, check=False, cls="none", y=y, tags=set(tags))
        n[key] += 1
    return n


def place_facades(b, plan, variants, double, y=0.0, plain=(), plain_asset=None, tags=("wall", "facade"), out=None):
    """Rue Haute: a house frontage (+z to the lane, body into the solid cell) on every plain edge with one walkable side;
    `double` on edges between two walkable regions; a link's own piece wins. Edges of the regions in `plain` keep
    `plain_asset` (the churchyard wall). The variant is a spatial hash of the edge, never the same as the bay before it
    along the same line."""
    n = {"facade": 0, "double": 0, "link": 0, "plain": 0}
    last = {}
    for x, z, yaw, kind, a, c in sorted(plan.edges(), key=lambda e: (e[2], e[1], e[0]) if e[2] == 0.0 else (e[2], e[0], e[1])):
        if kind:
            b.place(kind, x, z, yaw, check=False, cls="none", y=y, tags=set(tags) - {"facade"})
            n["link"] += 1
            continue
        wa, wc = plan.walk(*a), plan.walk(*c)
        if wa and wc:
            b.place(double, x, z, yaw, check=False, cls="none", y=y, tags=set(tags) - {"facade"})
            n["double"] += 1
            continue
        inside = a if wa else c
        ix, iz = plan._cc(*inside)
        dx, dz = (1.0 if ix > x else -1.0, 0.0) if yaw == 90.0 else (0.0, 1.0 if iz > z else -1.0)
        fy = math.degrees(math.atan2(dx, dz)) % 360.0
        if plan.at(*inside) in plain:
            b.place(plain_asset, x, z, fy, check=False, cls="none", y=y, tags=set(tags) - {"facade"})
            n["plain"] += 1
            continue
        hx, hz = int(math.floor(x / 3.0 + 0.5)), int(math.floor(z / 3.0 + 0.5))
        k = ((hx * 73856093) ^ (hz * 19349663) ^ (hx * hz * 83492791)) % len(variants)
        line = (yaw, round(z if yaw == 0.0 else x, 1), dz if yaw == 0.0 else dx)
        prev = last.get(line)
        if prev is not None and prev[0] == k and abs((x if yaw == 0.0 else z) - prev[1]) < plan.cell + 0.1:
            k = (k + 1 + hx % 3) % len(variants)
        last[line] = (k, x if yaw == 0.0 else z)
        inst = b.place(variants[k], x, z, fy, check=False, cls="none", y=y, tags=set(tags))
        if out is not None:
            out.append((x, z, dx, dz, inst))   # the edge, its lane normal, the house (a support for what hangs on it)
        n["facade"] += 1
    return n


def wall_spot(plan, ch, near, inset=0.27):
    """The plain wall of region ch nearest to `near`: (x, z, yaw) on its face, an asset's +z facing into the region."""
    best = None
    for x, z, yaw, kind, a, c in plan.edges():
        if kind:
            continue
        ra, rc = plan.at(*a), plan.at(*c)
        if (ra == ch) == (rc == ch):
            continue
        inside = a if ra == ch else c
        ix, iz = plan._cc(*inside)
        dx, dz = (1.0 if ix > x else -1.0, 0.0) if yaw == 90.0 else (0.0, 1.0 if iz > z else -1.0)
        d = math.hypot(x - near[0], z - near[1])
        if best is None or d < best[0]:
            best = (d, x + dx * inset, z + dz * inset, math.degrees(math.atan2(dx, dz)) % 360.0)
    return best[1:]


def rows_from(nx, nz, rects):
    """ASCII plan rows (north first) from [(ch, i0, j0, i1, j1), ...] (inclusive cell rectangles; later ones win)."""
    g = [["#"] * nx for _ in range(nz)]
    for ch, i0, j0, i1, j1 in rects:
        for j in range(j0, j1 + 1):
            for i in range(i0, i1 + 1):
                g[j][i] = ch
    return ["".join(g[j]) for j in range(nz - 1, -1, -1)]


def place_container_walls(b, plan, perimeter, between, R, y=0.0, boxes=("mod_container_20",), stack=0.35, tags=("wall",), skew=0.0, out=None):
    """Night Yard walls: a 20 ft container along every inner edge that has solid ground on one side, pushed half its
    depth into the solid side (the lane keeps its 6 m), some stacked two high; `perimeter` where the solid side is on
    the plan's outer ring; `between` (a thin wall) between two walkable regions; a link's own piece wins."""
    n = {"container": 0, "perimeter": 0, "between": 0, "link": 0}
    for x, z, yaw, kind, a, c in plan.edges():
        if kind:
            b.place(kind, x, z, yaw, check=False, cls="none", y=y, tags=set(tags)); n["link"] += 1
            continue
        wa, wc = plan.walk(*a), plan.walk(*c)
        if wa and wc:
            b.place(between, x, z, yaw, check=False, cls="none", y=y, tags=set(tags)); n["between"] += 1
            continue
        solid = c if wa else a
        if outer(plan, solid):
            b.place(perimeter, x, z, yaw, check=False, cls="none", y=y, tags=set(tags)); n["perimeter"] += 1
            continue
        sx, sz = plan._cc(*solid)
        dx, dz = (math.copysign(1.0, sx - x), 0.0) if yaw == 90.0 else (0.0, math.copysign(1.0, sz - z))
        px, pz = x + dx * 1.3, z + dz * 1.3
        cyaw = 0.0 if yaw == 90.0 else 90.0   # the container's length (local z) along the edge
        aid = R.choice(boxes)
        low = b.place(aid, px, pz, cyaw + R.choice((0.0, 180.0)), check=False, cls="none", y=y, tags=set(tags))
        if low is not None and R.random() < stack:   # stacked: rests on the box below (ED1 support, never seated on the ground)
            b.place(R.choice(boxes), px, pz, cyaw + R.choice((0.0, 180.0)) + R.uniform(-skew, skew), check=False, cls="none", y=y + 2.6, tags=set(tags) | {"stacked"}, support=low)
            if out is not None:
                out.append((px, pz, -dx, -dz, 5.2))
        elif out is not None:
            out.append((px, pz, -dx, -dz, 2.6))   # the box's centre, its lane normal, its top
        n["container"] += 1
    return n


def site_signs(b, plan, ch, letter, y, tags=("sign",), asset="sd_stencil_%s", inset=0.27):
    """The site's letter board on the wall beside each of its entrances, inside and outside (sd_sign_<a|b>_wall)."""
    n = 0
    seen = []
    for x, z, yaw, kind, a, c in plan.all_edges():
        if kind is None:
            continue
        ra, rc = plan.at(*a), plan.at(*c)
        if (ra == ch) == (rc == ch) or "#" in (ra, rc):
            continue
        other = rc if ra == ch else ra
        for reg in (ch, other):
            try:
                sx, sz, syaw = wall_spot(plan, reg, (x, z), inset=inset)
            except TypeError:
                continue
            if all(math.hypot(sx - p, sz - q) > 3.0 for p, q in seen) and math.hypot(sx - x, sz - z) < 9.0:
                b.place(asset % letter, sx, sz, syaw, check=False, cls="none", y=y, tags=set(tags))   # stencil-painted letters (the pixel boards read low-res)
                seen.append((sx, sz))
                n += 1
    return n


def wall_lamps(b, plan, regions, aid, y, every=3, inset=0.6, tags=("lamp",)):
    """A lamp standard against a wall of each region, about one per `every` cells."""
    n = 0
    for ch in regions:
        cells = plan.cells(ch)
        for k, (i, j) in enumerate(cells):
            if k % every:
                continue
            try:
                x, z, yaw = wall_spot(plan, ch, plan._cc(i, j), inset=inset)
            except TypeError:
                continue
            b.place(aid, x, z, yaw, check=False, cls="none", y=y, tags=set(tags))
            n += 1
    return n


def asset_top(repo, aid):
    """The highest point of an asset's box / cylinder parts (its roof line), from its JSON."""
    import json as _json
    a = _json.load(open(os.path.join(repo, "content", "assets", aid + ".json"), encoding="utf-8"))
    tops = [q["c"][1] + q["s"][1] / 2 for q in a.get("parts", []) if q.get("t") in ("b", "c") and "c" in q and "s" in q and q.get("v", 1) != 0]
    return max(tops) if tops else 0.0


def roof_clutter(b, repo, masses, items, R, y, flat=(), tags=("roof",)):
    """1-3 rooftop items (HVAC units, tanks, antennas) on each flat-roofed mass, at its roof line, so skylines break up."""
    n = 0
    for aid, x, z, hw, hd in masses:
        if aid not in flat:
            continue
        top = asset_top(repo, aid)
        for k in range(R.randint(1, 3)):
            it = R.choice(items)
            px, pz = x + R.uniform(-hw + 1.5, hw - 1.5), z + R.uniform(-hd + 1.5, hd - 1.5)
            b.place(it, px, pz, R.choice((0.0, 90.0, 180.0, 270.0)), check=False, cls="none", y=y + top, tags=set(tags))
            n += 1
    return n


def place_along_walls(b, plan, ch, items, R, y, keep=(), door_gap=4.5, tries=300, tags=("cover",)):
    """items [(asset, half along the wall, half depth, count)]: set against region ch's plain walls (their long side on the
    wall, `half depth` + 0.15 m off it), never within door_gap of a doorway or on a plant pad, not overlapping each other."""
    walls = []
    for x, z, yaw, kind, a, c in plan.edges():
        if kind:
            continue
        ra, rc = plan.at(*a), plan.at(*c)
        if (ra == ch) == (rc == ch):
            continue
        inside = a if ra == ch else c
        ix, iz = plan._cc(*inside)
        nx, nz = (math.copysign(1.0, ix - x), 0.0) if yaw == 90.0 else (0.0, math.copysign(1.0, iz - z))
        walls.append((x, z, nx, nz))
    doors = door_points(plan, ch)
    placed, n = [], 0
    for aid, ha, hd, cnt in items:
        for _ in range(cnt):
            for _t in range(tries):
                x, z, nx, nz = R.choice(walls)
                t = R.uniform(-plan.cell / 2 + ha, plan.cell / 2 - ha)
                px, pz = x + nx * (hd + 0.4) + (-nz) * t, z + nz * (hd + 0.4) + nx * t
                if any(math.hypot(px - dx, pz - dz) < door_gap + ha for dx, dz in doors):
                    continue
                if any(math.hypot(px - kx, pz - kz) < kr + max(ha, hd) for kx, kz, kr in keep):
                    continue
                if any(abs(px - qx) < ha + qa + 1.0 and abs(pz - qz) < hd + qd + 1.0 for qx, qz, qa, qd in placed):   # a 1 m walkway between pieces (tight gaps became vault links the bots fail)
                    continue
                yaw = math.degrees(math.atan2(-nz, nx)) % 360.0 + R.uniform(-4, 4)   # the long side (local x) along the wall
                b.place(aid, px, pz, yaw, check=False, cls="none", y=y, tags=set(tags))
                placed.append((px, pz, ha, hd))
                n += 1
                break
    return n



def free_spot(plan, ch, hw, hd, R, segs, keep, placed, lane=1.5, tries=500):
    """A centre (x, z, yaw 0 / 90) for a hw x hd block inside region ch: 1 m off its walls, `lane` + its size clear of the
    door-to-door lines `segs`, clear of the plant pads `keep` and of the blocks already `placed` [(x, z, r)]."""
    cells = plan.cells(ch)
    for _ in range(tries):
        i, j = R.choice(cells)
        cx, cz = plan._cc(i, j)
        x, z = cx + R.uniform(-plan.cell / 2, plan.cell / 2), cz + R.uniform(-plan.cell / 2, plan.cell / 2)
        yaw = R.choice((0.0, 90.0))
        ex, ez = (hw, hd) if yaw == 0.0 else (hd, hw)
        if not rect_ok(plan, x, z, ex, ez, walkable=True, margin=1.0):
            continue
        r = max(hw, hd)
        if any(_seg_dist((x, z), s0, s1) < lane + r for s0, s1 in segs):
            continue
        if any(math.hypot(x - kx, z - kz) < kr + r for kx, kz, kr in keep):
            continue
        if any(math.hypot(x - px, z - pz) < r + pr + 1.2 for px, pz, pr in placed):
            continue
        return x, z, yaw
    return None


def door_segs(plan, ch):
    d = door_points(plan, ch)
    return [(d[a], d[k]) for a in range(len(d)) for k in range(a + 1, len(d))]


def barrier_line(b, plan, ch, piece, n, length, depth, R, y, keep, placed, lane=1.5, tags=("cover",)):
    """n pieces (each `length` along the line, `depth` deep) side by side as one straight line, at a free spot."""
    spot = free_spot(plan, ch, n * length / 2.0, depth / 2.0, R, door_segs(plan, ch), keep, placed, lane=lane)
    if not spot:
        return 0
    x, z, yaw = spot
    ux, uz = (1.0, 0.0) if yaw == 0.0 else (0.0, 1.0)
    for k in range(n):
        t = (k - (n - 1) / 2.0) * length
        b.place(piece, x + ux * t, z + uz * t, yaw + R.uniform(-3, 3), check=False, cls="none", y=y, tags=set(tags))
    placed.append((x, z, n * length / 2.0))
    return n


def place_in_space(b, plan, ch, items, R, y, keep, placed, lane=1.5, tags=("cover",)):
    """items [(asset, half w, half d, count)] at free spots inside region ch (the playable middle, not the walls)."""
    n = 0
    segs = door_segs(plan, ch)
    for aid, hw, hd, cnt in items:
        for _ in range(cnt):
            spot = free_spot(plan, ch, hw, hd, R, segs, keep, placed, lane=lane)
            if not spot:
                continue
            x, z, yaw = spot
            b.place(aid, x, z, yaw + R.uniform(-12, 12), check=False, cls="none", y=y, tags=set(tags))
            placed.append((x, z, max(hw, hd)))
            n += 1
    return n
