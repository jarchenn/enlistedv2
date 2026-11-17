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
    def __init__(self, rows, cell, links=None):
        self.rows = [r.rstrip("\n") for r in rows]
        self.nz = len(self.rows)
        self.nx = max(len(r) for r in self.rows)
        self.cell = cell
        self.x0 = -self.nx * cell / 2.0
        self.z0 = -self.nz * cell / 2.0
        self.links = {}
        for (a, b), kind in (links or {}).items():
            self.links[frozenset((a, b))] = kind

    def at(self, i, j):
        if i < 0 or j < 0 or i >= self.nx or j >= self.nz:
            return "#"
        r = self.rows[self.nz - 1 - j]
        return r[i] if i < len(r) else "#"

    def walk(self, i, j):
        return self.at(i, j) not in "# "

    def cc(self, i, j):
        return (self.x0 + (i + 0.5) * self.cell, self.z0 + (j + 0.5) * self.cell)

    def cells(self, ch):
        return [(i, j) for j in range(self.nz) for i in range(self.nx) if self.at(i, j) == ch]

    def centre(self, ch):
        cs = self.cells(ch)
        xs = [self.cc(*c) for c in cs]
        return (sum(p[0] for p in xs) / len(xs), sum(p[1] for p in xs) / len(xs))

    def edges(self):
        """Every cell edge that needs a wall: (x, z, yaw, kind) at the edge's midpoint; kind None = plain wall."""
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
                    if kind == "open":
                        continue
                    x, z = self.cc(*a)
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
