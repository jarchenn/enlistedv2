"""DF1: the user's dry grass patches (DJMaesen, tools/fl1_flora.py "drygrass") on the desert maps, placed from what a map
already has: tufts at the feet of walls, along wadi banks and track edges, round rocks, sparse in open sand. The desert
shrubs come in through the alias tables (tree_aliases.json "maps"), on the spots of the shrubs they replace.

Placement only: the caller gives the instances, a ground sampler and a keep-out test, so a map build and a pass over a
written template (tools/fl1_flora.py desert) place the same way.
"""
import json
import math
import os
import struct
import zlib

PATCH = ["fl_drygrass_patch_1", "fl_drygrass_patch_2", "fl_drygrass_patch_3"]   # about 0.5 m tall, 1 m across
TUFT = ["fl_drygrass_tuft_1", "fl_drygrass_tuft_2", "fl_drygrass_tuft_3"]        # about 0.35 m: wall feet inside S&D lanes
SHRUB = ["fl_desertshrub_%s_%d" % (k, n) for k, c in (("agave", 3), ("cholla", 3), ("creosote", 3), ("ocotillo", 4), ("yucca", 3)) for n in range(1, c + 1)]   # local-only bake, committed fallbacks
FLORA = set(PATCH + TUFT)
WALLISH = ("hesco", "twall", "_wall", "sandbag", "jersey", "barrier", "container")
ROCKS = ("rk_", "rock", "boulder", "outcrop", "stones", "scree")
_FP = {}


def footprint(repo, aid):
    """(w, d) of an asset (content/assets/<id>.json "footprint"), (0, 0) when it has none"""
    if aid not in _FP:
        try:
            j = json.load(open(os.path.join(repo, "content", "assets", aid + ".json"), encoding="utf-8"))
            fp = j.get("footprint", [0, 0])
            solid = j.get("category") not in ("Vegetation", "Desert") and "scatter" not in j and (j.get("parts") or j.get("model"))
            _FP[aid] = (float(fp[0]), float(fp[1]), bool(solid))
        except (OSError, ValueError):
            _FP[aid] = (0.0, 0.0, False)
    return _FP[aid]


def axes(yaw):
    """asset-local +x and +z in world (x, z): local +z points at (sin yaw, cos yaw)"""
    r = math.radians(yaw)
    return (math.cos(r), -math.sin(r)), (math.sin(r), math.cos(r))


def inside(inst, x, z, pad):
    """inst = (aid, x, z, yaw, w, d): (x, z) within its footprint grown by pad"""
    _, ix, iz, yaw, w, d = inst
    ax, az = axes(yaw)
    dx, dz = x - ix, z - iz
    return abs(dx * ax[0] + dz * ax[1]) < w / 2 + pad and abs(dx * az[0] + dz * az[1]) < d / 2 + pad


def obstacles(repo, insts):
    """the solid instances (buildings, walls, props with a footprint): plants stay off them"""
    out = []
    for aid, x, z, yaw in insts:
        w, d, solid = footprint(repo, aid)
        if solid and w * d > 0.2 and not aid.startswith("line_"):
            out.append((aid, x, z, yaw, w, d))
    return out


class Placer:
    def __init__(self, R, ok, gap=1.4):
        self.R, self.ok, self.gap, self.out = R, ok, gap, []

    def add(self, ids, x, z, p=1.0):
        if self.R.random() >= p or not self.ok(x, z):
            return False
        if any((x - a) ** 2 + (z - b) ** 2 < self.gap ** 2 for _, a, b, _ in self.out):
            return False
        self.out.append((self.R.choice(ids), x, z, self.R.uniform(0.0, 360.0)))
        return True

    def along(self, pts, offs, step, ids, p, jitter=0.4):
        """both banks / edges of a polyline: offset offs (m, signed: left +) every ~step m"""
        for (x0, z0), (x1, z1) in zip(pts, pts[1:]):
            L = math.hypot(x1 - x0, z1 - z0)
            if L < 1e-6:
                continue
            nx, nz = -(z1 - z0) / L, (x1 - x0) / L
            n = max(1, int(L / step + self.R.random()))
            for k in range(n):
                t = (k + self.R.random()) / n
                x, z = x0 + (x1 - x0) * t, z0 + (z1 - z0) * t
                for o in offs:
                    o += self.R.uniform(-jitter, jitter)
                    self.add(ids, x + nx * o, z + nz * o, p)

    def walls(self, obst, ids, step, p, off=0.45):
        """the long sides of wall-like pieces (HESCO, T-walls, sandbags, barriers, containers)"""
        for aid, x, z, yaw, w, d in obst:
            if not any(s in aid for s in WALLISH):
                continue
            ax, az = axes(yaw)
            (ux, uz), (vx, vz), half, depth = (ax, az, w, d) if w >= d else (az, ax, d, w)
            for side in (-1.0, 1.0):
                n = max(1, int(half / step + self.R.random()))
                for k in range(n):
                    t = ((k + self.R.random()) / n - 0.5) * half
                    o = side * (depth / 2 + off)
                    self.add(ids, x + ux * t + vx * o, z + uz * t + vz * o, p)

    def rings(self, rocks, ids, n, p):
        for aid, x, z, yaw, w, d in rocks:
            r = 0.5 * max(w, d, 0.6) + 0.5
            for _ in range(n):
                a = self.R.uniform(0, 2 * math.pi)
                self.add(ids, x + math.cos(a) * r, z + math.sin(a) * r, p)

    def scatter(self, x0, z0, x1, z1, count, ids, tries=4):
        for _ in range(count * tries):
            if self.add(ids, self.R.uniform(x0, x1), self.R.uniform(z0, z1)) and count:
                count -= 1
            if count <= 0:
                break


def seg_dist(p, pts):
    best = 1e9
    for (x0, z0), (x1, z1) in zip(pts, pts[1:]):
        dx, dz = x1 - x0, z1 - z0
        L2 = dx * dx + dz * dz or 1e-9
        t = max(0.0, min(1.0, ((p[0] - x0) * dx + (p[1] - z0) * dz) / L2))
        best = min(best, math.hypot(p[0] - x0 - dx * t, p[1] - z0 - dz * t))
    return best


def template_grid(tdir, tid):
    """a terrain.Grid filled from a written template's 16-bit heightmap (filter-0 rows, as Grid.write_png writes)"""
    from .terrain import Grid
    t = json.load(open(os.path.join(tdir, tid + ".json"), encoding="utf-8"))
    hm = t["heightmap"]
    g = Grid(t["width"], t["depth"], t["margin"])
    assert (g.nx, g.nz) == (hm["nx"], hm["nz"]), (g.nx, g.nz, hm)
    data = open(os.path.join(tdir, hm["file"]), "rb").read()
    k, idat = 8, b""
    while k < len(data):
        n, tag = struct.unpack(">I4s", data[k:k + 8])
        if tag == b"IDAT":
            idat += data[k + 8:k + 8 + n]
        k += 12 + n
    raw = zlib.decompress(idat)
    row = 1 + 2 * g.nx
    span = hm["max"] - hm["min"]
    for j in range(g.nz):
        assert raw[j * row] == 0, "PNG filter %d" % raw[j * row]
        for i in range(g.nx):
            q = struct.unpack_from(">H", raw, j * row + 1 + 2 * i)[0]
            g.h[j * g.nx + i] = hm["min"] + q / 65535.0 * span
    return g


if __name__ == "__main__":   # self-check: footprint test and placer spacing
    import random
    o = ("w", 0.0, 0.0, 90.0, 4.0, 1.0)   # 4 m along local x, which yaw 90 lays along world -z
    assert inside(o, 0.0, 1.9, 0.0) and not inside(o, 1.9, 0.0, 0.0)
    P = Placer(random.Random(1), lambda x, z: True, gap=1.0)
    P.scatter(0, 0, 10, 10, 30, PATCH)
    assert all((a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2 >= 1.0 for i, a in enumerate(P.out) for b in P.out[:i])
    print("desert_flora ok", len(P.out))
