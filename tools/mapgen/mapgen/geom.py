"""Geometry helpers: vectors as (x, z) tuples, yaw conventions, splines, polygons, noise.

Engine conventions (src/app.h, src/scenario.cpp):
  * instance yaw (deg about +Y) maps asset-local +Z to world (sin yaw, cos yaw); local +X to (cos yaw, -sin yaw);
  * a character / spawn / objective-arrow yaw faces (-sin yaw, -cos yaw)  (yaw 0 looks toward -Z, 180 toward +Z).
"""
import math
import random


def lerp(a, b, t):
    return a + (b - a) * t


def clamp(v, lo, hi):
    return lo if v < lo else hi if v > hi else v


def smoothstep(e0, e1, x):
    if e1 == e0:
        return 0.0 if x < e0 else 1.0
    t = clamp((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def dist(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def norm(v):
    L = math.hypot(v[0], v[1])
    return (v[0] / L, v[1] / L) if L > 1e-9 else (0.0, 0.0)


def wrap_deg(d):
    d = math.fmod(d, 360.0)
    if d > 180:
        d -= 360
    if d <= -180:
        d += 360
    return d


def asset_yaw_facing(dx, dz):
    """Instance yaw that turns asset-local +Z toward world direction (dx, dz)."""
    return wrap_deg(math.degrees(math.atan2(dx, dz)))


def asset_yaw_along(dx, dz):
    """Instance yaw that lays asset-local +X along world direction (dx, dz)."""
    return wrap_deg(math.degrees(math.atan2(-dz, dx)))


def look_yaw(dx, dz):
    """Character / spawn / attack-arrow yaw that looks along world direction (dx, dz)."""
    return wrap_deg(math.degrees(math.atan2(-dx, -dz)))


def local_to_world(pos, yaw, lx, lz):
    """Asset-local (x, z) offset -> world (x, z) for an instance at pos (x, z) with yaw (deg)."""
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    return (pos[0] + lx * c + lz * s, pos[1] - lx * s + lz * c)


# ---------------------------------------------------------------- polylines and splines
def catmull_rom(points, samples_per_seg=8):
    """Centripetal-ish uniform Catmull-Rom through all points (ends clamped)."""
    if len(points) < 3:
        return list(points)
    pts = [points[0]] + list(points) + [points[-1]]
    out = []
    for i in range(1, len(pts) - 2):
        p0, p1, p2, p3 = pts[i - 1], pts[i], pts[i + 1], pts[i + 2]
        for k in range(samples_per_seg):
            t = k / samples_per_seg
            t2, t3 = t * t, t * t * t
            out.append(tuple(0.5 * ((2 * p1[j]) + (-p0[j] + p2[j]) * t + (2 * p0[j] - 5 * p1[j] + 4 * p2[j] - p3[j]) * t2 +
                                    (-p0[j] + 3 * p1[j] - 3 * p2[j] + p3[j]) * t3) for j in range(2)))
    out.append(tuple(points[-1]))
    return out


def polyline_length(pts):
    return sum(dist(pts[i], pts[i + 1]) for i in range(len(pts) - 1))


def resample(pts, step):
    """Points every `step` metres of arc length (first and last included)."""
    out = [tuple(pts[0])]
    acc = 0.0
    for i in range(len(pts) - 1):
        a, b = pts[i], pts[i + 1]
        L = dist(a, b)
        if L < 1e-9:
            continue
        s = step - acc
        while s <= L:
            t = s / L
            out.append((lerp(a[0], b[0], t), lerp(a[1], b[1], t)))
            s += step
        acc = (acc + L) % step
    if dist(out[-1], pts[-1]) > 1e-3:
        out.append(tuple(pts[-1]))
    return out


def point_at(pts, s):
    """(point, unit tangent) at arc length s along the polyline."""
    for i in range(len(pts) - 1):
        a, b = pts[i], pts[i + 1]
        L = dist(a, b)
        if s <= L or i == len(pts) - 2:
            t = clamp(s / L if L > 0 else 0, 0, 1)
            return (lerp(a[0], b[0], t), lerp(a[1], b[1], t)), norm((b[0] - a[0], b[1] - a[1]))
        s -= L
    return tuple(pts[-1]), (0.0, 1.0)


def seg_dist(p, a, b):
    """(distance, t along segment) from p to segment ab."""
    abx, abz = b[0] - a[0], b[1] - a[1]
    L2 = abx * abx + abz * abz
    t = clamp(((p[0] - a[0]) * abx + (p[1] - a[1]) * abz) / L2, 0.0, 1.0) if L2 > 1e-12 else 0.0
    return math.hypot(p[0] - (a[0] + abx * t), p[1] - (a[1] + abz * t)), t


def polyline_dist(p, pts):
    """(distance, arc length of the nearest point, total length)."""
    best, best_s, acc = 1e18, 0.0, 0.0
    for i in range(len(pts) - 1):
        d, t = seg_dist(p, pts[i], pts[i + 1])
        L = dist(pts[i], pts[i + 1])
        if d < best:
            best, best_s = d, acc + t * L
        acc += L
    return best, best_s, acc


def offset_polyline(pts, off):
    """Parallel polyline `off` metres to the left (+) of the direction of travel."""
    out = []
    for i, p in enumerate(pts):
        a = pts[max(0, i - 1)]
        b = pts[min(len(pts) - 1, i + 1)]
        d = norm((b[0] - a[0], b[1] - a[1]))
        out.append((p[0] - d[1] * off, p[1] + d[0] * off))
    return out


def in_poly(p, poly):
    inside = False
    j = len(poly) - 1
    for i in range(len(poly)):
        if ((poly[i][1] > p[1]) != (poly[j][1] > p[1])) and \
                p[0] < (poly[j][0] - poly[i][0]) * (p[1] - poly[i][1]) / (poly[j][1] - poly[i][1]) + poly[i][0]:
            inside = not inside
        j = i
    return inside


def poly_area(poly):
    a = 0.0
    for i in range(len(poly)):
        x0, z0 = poly[i]
        x1, z1 = poly[(i + 1) % len(poly)]
        a += x0 * z1 - x1 * z0
    return abs(a) * 0.5


def poly_bounds(poly):
    xs = [p[0] for p in poly]
    zs = [p[1] for p in poly]
    return min(xs), min(zs), max(xs), max(zs)


def rect(x0, z0, x1, z1):
    return [(x0, z0), (x1, z0), (x1, z1), (x0, z1)]


def circle_poly(cx, cz, r, n=24):
    return [(cx + r * math.cos(2 * math.pi * k / n), cz + r * math.sin(2 * math.pi * k / n)) for k in range(n)]


def obb_overlap(c1, h1, a1, c2, h2, a2):
    """2D oriented boxes (centre, half extents, yaw deg) overlap test, same axes as App::checkPlacement."""
    r1, r2 = math.radians(a1), math.radians(a2)
    ax = [(math.cos(r1), -math.sin(r1)), (math.sin(r1), math.cos(r1)), (math.cos(r2), -math.sin(r2)), (math.sin(r2), math.cos(r2))]
    d = (c2[0] - c1[0], c2[1] - c1[1])
    for a in ax:
        e1 = h1[0] * abs(ax[0][0] * a[0] + ax[0][1] * a[1]) + h1[1] * abs(ax[1][0] * a[0] + ax[1][1] * a[1])
        e2 = h2[0] * abs(ax[2][0] * a[0] + ax[2][1] * a[1]) + h2[1] * abs(ax[3][0] * a[0] + ax[3][1] * a[1])
        if abs(d[0] * a[0] + d[1] * a[1]) > e1 + e2:
            return False
    return True


# ---------------------------------------------------------------- noise (deterministic, seedable)
def _hash2(x, y, s):
    h = (x * 374761393 + y * 668265263 + s * 2246822519) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    h ^= h >> 16
    return (h & 0xFFFFFF) / float(0xFFFFFF)


def vnoise(x, y, s=0):
    """Value noise in [-1, 1]."""
    xi, yi = math.floor(x), math.floor(y)
    fx, fy = x - xi, y - yi
    fx = fx * fx * (3 - 2 * fx)
    fy = fy * fy * (3 - 2 * fy)
    a, b = _hash2(xi, yi, s), _hash2(xi + 1, yi, s)
    c, d = _hash2(xi, yi + 1, s), _hash2(xi + 1, yi + 1, s)
    return (lerp(lerp(a, b, fx), lerp(c, d, fx), fy)) * 2 - 1


def fbm(x, y, s=0, octaves=4, lac=2.0, gain=0.5):
    amp, f, tot, norm_ = 1.0, 1.0, 0.0, 0.0
    for o in range(octaves):
        tot += amp * vnoise(x * f, y * f, s + o * 17)
        norm_ += amp
        amp *= gain
        f *= lac
    return tot / norm_


def rng(seed):
    return random.Random(seed)
