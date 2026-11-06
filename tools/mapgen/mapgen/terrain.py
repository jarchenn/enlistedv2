"""Base terrain on W3's grid: zone-table sculpting, stamps and the 16-bit heightmap the engine loads.

The grid replicates Terrain::generate (src/terrain.cpp): vertex spacing KCELL = 800/255 m, the build area plus
`margin` on every side rounded up to whole 5-cell tiles, centred on the origin, row-major h[z * nx + x].
The engine reads the written PNG back through the template's "heightmap" block (src/w5_maps.cpp) and copies it into
Terrain::h, so heights here are exactly the engine's base heights (after 16-bit quantisation, see quantise()).
"""
import math
import struct
import zlib
from array import array

from .geom import clamp, smoothstep, lerp, vnoise, fbm, polyline_dist, in_poly, poly_bounds

KCELL = 800.0 / 255.0
TILE_CELLS = 5


def _cells_for(ext):
    c = int(math.ceil(ext / KCELL - 1e-3))
    return max(TILE_CELLS * 2, (c + TILE_CELLS - 1) // TILE_CELLS * TILE_CELLS)


class Grid:
    def __init__(self, width, depth, margin=100.0, base=0.0):
        self.width, self.depth, self.margin = float(width), float(depth), float(margin)
        cx, cz = _cells_for(width + 2 * margin), _cells_for(depth + 2 * margin)
        self.nx, self.nz = cx + 1, cz + 1
        self.size_x, self.size_z = cx * KCELL, cz * KCELL
        self.org_x, self.org_z = -self.size_x * 0.5, -self.size_z * 0.5
        self.h = array('d', [base]) * (self.nx * self.nz)
        self.hmin = self.hmax = None

    # ---- coordinates
    def vx(self, i):
        return self.org_x + i * KCELL

    def vz(self, j):
        return self.org_z + j * KCELL

    def in_build(self, x, z):
        return abs(x) <= self.width * 0.5 and abs(z) <= self.depth * 0.5

    def sample(self, x, z):
        """Engine Terrain::baseHeight: bilinear on the a-d diagonal split."""
        fx = clamp((x - self.org_x) / KCELL, 0.0, self.nx - 1.001)
        fz = clamp((z - self.org_z) / KCELL, 0.0, self.nz - 1.001)
        xi, zi = int(fx), int(fz)
        tx, tz = fx - xi, fz - zi
        r0 = zi * self.nx
        r1 = r0 + self.nx
        a, b, c, d = self.h[r0 + xi], self.h[r0 + xi + 1], self.h[r1 + xi], self.h[r1 + xi + 1]
        if tx >= tz:
            return a + (b - a) * tx + (d - b) * tz
        return a + (c - a) * tz + (d - c) * tx

    def slope_deg(self, x, z, e=KCELL):
        gx = (self.sample(x + e, z) - self.sample(x - e, z)) / (2 * e)
        gz = (self.sample(x, z + e) - self.sample(x, z - e)) / (2 * e)
        return math.degrees(math.atan(math.hypot(gx, gz)))

    # ---- sculpting
    def apply(self, fn, bounds=None):
        """h = fn(x, z, h) for every vertex (optionally only inside bounds = (x0, z0, x1, z1))."""
        i0, j0, i1, j1 = 0, 0, self.nx - 1, self.nz - 1
        if bounds:
            i0 = max(0, int((bounds[0] - self.org_x) / KCELL) - 1)
            j0 = max(0, int((bounds[1] - self.org_z) / KCELL) - 1)
            i1 = min(self.nx - 1, int((bounds[2] - self.org_x) / KCELL) + 2)
            j1 = min(self.nz - 1, int((bounds[3] - self.org_z) / KCELL) + 2)
        h, nx = self.h, self.nx
        for j in range(j0, j1 + 1):
            z = self.org_z + j * KCELL
            row = j * nx
            for i in range(i0, i1 + 1):
                k = row + i
                h[k] = fn(self.org_x + i * KCELL, z, h[k])

    def quantise(self, pad=1.0):
        """Snap heights to the 16-bit steps the PNG stores (call last; instance heights are taken after it)."""
        lo, hi = min(self.h) - pad, max(self.h) + pad
        self.hmin, self.hmax = math.floor(lo), math.ceil(hi)
        span = self.hmax - self.hmin
        for k in range(len(self.h)):
            q = int(round((self.h[k] - self.hmin) / span * 65535.0))
            self.h[k] = self.hmin + clamp(q, 0, 65535) / 65535.0 * span
        return self.hmin, self.hmax

    def write_png(self, path):
        """16-bit greyscale PNG, row 0 = minimum z (the engine's j = 0)."""
        if self.hmin is None:
            self.quantise()
        span = self.hmax - self.hmin
        raw = bytearray()
        for j in range(self.nz):
            raw.append(0)
            row = j * self.nx
            for i in range(self.nx):
                q = int(round((self.h[row + i] - self.hmin) / span * 65535.0))
                raw += struct.pack('>H', clamp(q, 0, 65535))

        def chunk(tag, data):
            c = struct.pack('>I', len(data)) + tag + data
            return c + struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF)
        png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', self.nx, self.nz, 16, 0, 0, 0, 0))
        png += chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b'')
        with open(path, 'wb') as f:
            f.write(png)

    def meta(self, file_name):
        return {"file": file_name, "nx": self.nx, "nz": self.nz, "min": self.hmin, "max": self.hmax,
                "org": [round(self.org_x, 4), round(self.org_z, 4)], "cell": KCELL}


# ---------------------------------------------------------------- zone tables
class Profile:
    """Height (or any value) as a smooth function of one coordinate through (s, v) knots.
    Between knots the value eases with smoothstep, so slopes start and end flat (beach -> bluff -> plateau)."""

    def __init__(self, knots):
        self.k = sorted(knots)

    def __call__(self, s):
        k = self.k
        if s <= k[0][0]:
            return k[0][1]
        for i in range(len(k) - 1):
            if s <= k[i + 1][0]:
                t = (s - k[i][0]) / (k[i + 1][0] - k[i][0])
                return lerp(k[i][1], k[i + 1][1], t * t * (3 - 2 * t))
        return k[-1][1]


def sculpt_zones(grid, frame, zones, seed=1):
    """Zone table along the attack axis. `zones` is a list of dicts in attack-axis metres (s):
        {"s": (s0, s1), "height": (h0, h1) or callable(x, s), "noise": (amp, scale), "blend": metres}
    Heights ease between zone ends; noise is fbm with the given amplitude (m) and feature scale (m). Zones are laid
    in order; `blend` cross-fades a zone's start into the previous result (default 0 = hard edge, already continuous
    when neighbouring heights match)."""
    def fn(x, z, h):
        s = frame.s_of(z)
        v = h
        for zn in zones:
            s0, s1 = zn["s"]
            bl = zn.get("blend", 0.0)
            if s < s0 - bl or s > s1:
                continue
            hh = zn["height"]
            if callable(hh):
                target = hh(x, s)
            else:
                t = clamp((s - s0) / max(1e-6, s1 - s0), 0, 1)
                target = lerp(hh[0], hh[1], t * t * (3 - 2 * t)) if zn.get("ease", True) else lerp(hh[0], hh[1], t)
            if "noise" in zn:
                amp, sc = zn["noise"]
                target += amp * fbm(x / sc, z / sc, seed + int(s0), 3)
            w = 1.0 if bl <= 0 or s >= s0 else smoothstep(s0 - bl, s0, s)
            v = lerp(v, target, w)
        return v
    grid.apply(fn)


# ---------------------------------------------------------------- stamps
def stamp_valley(grid, path, floor, top_half, floor_half, power=1.0, only_lower=True):
    """Cut a valley (draw, gully) along a polyline. floor(u), top_half(u), floor_half(u) are functions of the
    normalised position u in [0, 1] along the path: floor height, half width at the lip and of the flat floor.
    The side walls ease from the floor to the existing ground (power > 1 = steeper near the lip)."""
    x0, z0, x1, z1 = poly_bounds(path)
    pad = max(top_half(u) for u in (0, 0.25, 0.5, 0.75, 1)) + 5

    def fn(x, z, h):
        d, s, L = polyline_dist((x, z), path)
        u = s / L if L > 0 else 0
        th, fh = top_half(u), floor_half(u)
        if d >= th:
            return h
        f = floor(u)
        if d <= fh:
            t = 0.0
        else:
            t = ((d - fh) / max(1e-6, th - fh)) ** power
            t = t * t * (3 - 2 * t)
        v = lerp(f, h, t)
        return min(h, v) if only_lower else v
    grid.apply(fn, (x0 - pad, z0 - pad, x1 + pad, z1 + pad))


def stamp_channel(grid, path, half_width, depth, bank=None):
    """Lower the ground by `depth` along a polyline (relative to the current surface), easing over `bank` m."""
    bank = bank if bank is not None else half_width
    x0, z0, x1, z1 = poly_bounds(path)
    pad = half_width + bank + 5

    def fn(x, z, h):
        d, _, _ = polyline_dist((x, z), path)
        if d >= half_width + bank:
            return h
        k = 1.0 if d <= half_width else 1.0 - smoothstep(half_width, half_width + bank, d)
        return h - depth * k
    grid.apply(fn, (x0 - pad, z0 - pad, x1 + pad, z1 + pad))


def stamp_flatten(grid, cx, cz, radius, height=None, falloff=10.0, strength=1.0):
    """Level a disc to `height` (default: the current height at the centre), blending out over `falloff` m."""
    target = grid.sample(cx, cz) if height is None else height

    def fn(x, z, h):
        d = math.hypot(x - cx, z - cz)
        if d >= radius + falloff:
            return h
        w = strength * (1.0 if d <= radius else 1.0 - smoothstep(radius, radius + falloff, d))
        return lerp(h, target, w)
    r = radius + falloff
    grid.apply(fn, (cx - r, cz - r, cx + r, cz + r))
    return target


def stamp_flatten_poly(grid, poly, height=None, falloff=8.0):
    x0, z0, x1, z1 = poly_bounds(poly)
    target = height if height is not None else grid.sample((x0 + x1) / 2, (z0 + z1) / 2)
    from .geom import seg_dist

    def fn(x, z, h):
        if in_poly((x, z), poly):
            return target
        d = min(seg_dist((x, z), poly[i], poly[(i + 1) % len(poly)])[0] for i in range(len(poly)))
        if d >= falloff:
            return h
        return lerp(target, h, smoothstep(0, falloff, d))
    grid.apply(fn, (x0 - falloff, z0 - falloff, x1 + falloff, z1 + falloff))
    return target


def stamp_crater(grid, cx, cz, radius, depth, rim=0.25):
    """Naval-bombardment crater: the engine's bowl Z -= D (1 - d^2/R^2)^2 plus a raised lip out to 1.5 R."""
    def fn(x, z, h):
        d = math.hypot(x - cx, z - cz)
        if d >= radius * 1.6:
            return h
        q = max(0.0, 1.0 - (d * d) / (radius * radius))
        lip = rim * depth * math.exp(-((d - radius * 1.1) / (radius * 0.3)) ** 2)
        return h - depth * q * q + lip
    r = radius * 1.6
    grid.apply(fn, (cx - r, cz - r, cx + r, cz + r))


def stamp_noise(grid, amp, scale, seed, region=None, bounds=None):
    """Add fbm detail (amp metres, feature `scale` metres); region(x, z) -> weight 0..1."""
    def fn(x, z, h):
        w = region(x, z) if region else 1.0
        if w <= 0:
            return h
        return h + w * amp * fbm(x / scale, z / scale, seed, 4)
    grid.apply(fn, bounds)


def stamp_bank(grid, path, half_width, height):
    """Raise a low earth bank along a polyline (field boundary banks, dune ridges)."""
    x0, z0, x1, z1 = poly_bounds(path)
    pad = half_width + 2

    def fn(x, z, h):
        d, _, _ = polyline_dist((x, z), path)
        if d >= half_width:
            return h
        t = d / half_width
        return h + height * (1 - t * t) ** 2
    grid.apply(fn, (x0 - pad, z0 - pad, x1 + pad, z1 + pad))
