"""BT1 Blackwater Thaw, milestone 1 (greybox): an original mountain hamlet in a late-winter thaw, built around a stepped
waterworks, after the user's spec (Blackwater_Thaw_Map_Spec.md). Full terrain shape (valley slopes, creek bed, banks,
quarry cut), the three linked routes and their cross-connections, stairs and entries, boundaries, two start clusters,
six capture cameras as bookmarks, a far ridge with a placeholder cable-haul station, and a 10 v 10 Team Deathmatch
scenario. Buildings are stand-ins from the existing catalogue at the spec's family sizes (milestone 3 builds the kit).

Plan: spec X east / Y north = engine x / z. Elevations are metres above the lower creek bed (BASE). The terrain is a
harmonic fill (a minimal surface) between fixed constraints: area pads, graded lanes, terraces, boundary banks and the
far ridge; then the creek channel, stairs and snow shoulders are cut in. Detail finer than the 3.14 m base grid goes
into the engine's V8 sculpt layer (0.45 m fine samples), so the creek bed, stairs and shoulders are real ground.

    <python> -I tools/mapgen/build.py blackwater_thaw"""
import math
import os

import numpy as np
from scipy import ndimage

from mapgen import (GroundPaint, Grid, Frame, MapBuilder, ScenarioBuilder, write_map, write_template, write_summary,
                    rng, look_yaw, catmull_rom, polyline_dist, local_to_world, in_poly)

TID, NAME, VERSION, SEED = "blackwater_thaw", "Blackwater Thaw", 1, 7311
W, D = 440.0, 380.0          # the dressed valley floor
MARGIN = 170.0               # the backdrop: the far ridges and the cable-haul station
BASE = 10.0                  # the lower creek bed
ENV = (-128.0, -100.0, 132.0, 120.0)   # the playable envelope, 260 x 220 m (x0, z0, x1, z1)
RES = 0.5                    # the fill's grid (m)

# ---------------------------------------------------------------- areas (centre, half extents, floor above the creek bed)
AREAS = {
    "Lower cart yard": ((-87.0, -72.0), (16.5, 16.0), 1.0),
    "Western orchard": ((-76.0, -20.0), (30.0, 28.5), None),     # terraced: +4 .. +9 (TERRACES)
    "Creek bend": ((12.0, -31.0), (28.5, 22.5), None),
    "Waterworks court": ((-3.0, 2.0), (17.0, 14.5), 4.0),
    "Workshop yards": ((66.0, -1.0), (26.0, 25.5), None),        # +3.5 south .. +7.5 north
    "Washhouse terrace": ((10.0, 56.0), (22.0, 14.0), 15.0),
    "Upper start yard": ((15.0, 106.0), (13.0, 8.0), 17.4),
    "Lower start": ((-90.0, -72.5), (7.0, 4.5), 1.0),
    "Quarry overlook": ((80.0, 72.0), (19.5, 16.5), 20.0),
}
TERRACES = [(-48.5, -34.0, 4.0), (-34.0, -14.0, 5.5), (-14.0, 4.0, 7.0), (4.0, 8.5, 9.0)]   # orchard (z0, z1, floor)

# ---------------------------------------------------------------- the creek: west to east, bending north at the footbridge
CREEK_CTRL = [(-170.0, -50.0, 0.9), (-128.0, -51.0, 0.8), (-95.0, -51.5, 0.7), (-82.0, -52.0, 0.65), (-60.0, -50.0, 0.55),
              (-35.0, -46.0, 0.45), (-12.0, -38.0, 0.25), (12.0, -31.0, 0.0), (32.0, -40.0, -0.15), (50.0, -50.0, -0.3),
              (75.0, -58.0, -0.45), (105.0, -62.0, -0.6), (135.0, -66.0, -0.8), (180.0, -74.0, -1.0)]
CREEK = catmull_rom([(x, z) for x, z, _ in CREEK_CTRL])
BRIDGE_W = (-82.0, -52.0)     # the cart-road stone bridge (west route)
FOOTBRIDGE = (12.0, -31.0)    # the plank footbridge at the bend (centre route)
FORD = (50.0, -50.0)          # the downstream shallow crossing where the bank flattens (east route)

# ---------------------------------------------------------------- routes: (x, z, floor) control points; width (m)
# West: the cart road over the stone bridge, the orchard terraces, the outer trail curling uphill to the terrace
W_ROAD = [(-88.0, -64.0, 1.2), (-83.0, -61.0, 1.9), (-82.0, -58.5, 2.6), (-82.0, -45.5, 2.6), (-80.0, -40.0, 4.0)]
W_TRAIL = [(-80.0, -40.0, 4.0), (-96.0, -30.0, 4.6), (-104.0, -12.0, 6.2), (-100.0, 6.0, 8.2), (-86.0, 18.0, 9.6),
           (-64.0, 28.0, 10.0), (-46.0, 44.0, 10.1), (-33.0, 52.0, 10.1)]   # joins the north lane; a stone stair climbs to the terrace
W_ORCH = [(-80.0, -40.0, 4.0), (-70.0, -30.0, 4.0), (-66.0, -16.0, 5.5), (-62.0, -8.0, 7.0), (-56.0, -6.0, 7.0)]
BREACH = [(-56.0, -6.0, 7.0), (-46.0, -6.0, 6.6), (-34.0, -3.0, 5.2), (-20.0, 0.0, 4.2), (-14.0, 1.0, 4.0)]   # orchard breach -> court
# Centre: the creek path, the footbridge, the court, the north lane bending up behind the washhouse
C_CREEK = [(-82.0, -66.0, 1.4), (-60.0, -62.0, 1.6), (-36.0, -60.0, 1.8), (-12.0, -52.0, 2.0), (4.0, -44.0, 2.1), (12.0, -40.5, 2.2)]
C_BRIDGE = [(12.0, -40.5, 2.2), (12.0, -22.0, 2.6)]
C_COURT = [(12.0, -22.0, 2.6), (6.0, -14.0, 3.4), (-1.0, -6.0, 4.0), (-3.0, 2.0, 4.0)]
C_NORTH = [(-3.0, 2.0, 4.0), (-6.0, 16.5, 4.6), (-20.0, 24.0, 6.0), (-31.0, 33.0, 7.6), (-33.0, 46.0, 9.3), (-28.0, 60.0, 11.2),
           (-18.0, 72.0, 12.9), (-4.0, 80.0, 14.6), (8.0, 84.0, 16.0), (8.0, 92.0, 16.8), (16.0, 104.0, 17.4)]
# East: on along the south bank to the ford, up to the workshop gate, the yards, the service stairs to the terrace
E_BANK = [(12.0, -40.5, 2.2), (28.0, -52.0, 1.8), (40.0, -56.0, 1.0), (47.0, -55.0, 0.3), (50.0, -50.0, -0.1)]
E_FORD = [(50.0, -50.0, -0.1), (53.0, -45.0, 0.5), (58.0, -40.0, 1.2), (63.0, -34.0, 2.2), (62.0, -28.0, 3.0), (58.0, -24.0, 3.5)]
E_YARD = [(58.0, -24.0, 3.5), (62.0, -10.0, 4.2), (66.0, 4.0, 5.2), (68.0, 18.0, 7.0), (60.0, 30.0, 8.6), (50.0, 38.0, 9.6)]
E_STAIRS = [(50.0, 38.0, 9.6), (40.0, 44.0, 10.4), (36.0, 47.0, 10.6)]           # then the service stair to the terrace
STAIRS = [((36.0, 47.0, 10.6), (28.0, 53.0, 15.0)),      # the narrow service stair to the terrace (east route), 10 m run, 4.4 m rise
          ((-23.5, 65.5, 12.0), (-13.0, 60.0, 15.0))]     # the terrace's west stair from the north lane (west and centre routes)
WALKWAY = [(-3.0, 2.0, 4.0), (14.0, 3.0, 4.2), (28.0, 2.0, 4.5), (42.0, 0.0, 4.8), (52.0, -2.0, 4.6), (62.0, -2.0, 4.6)]   # covered court walkway -> workshop
GATE = [(58.0, -24.0, 3.5), (44.0, -24.0, 3.1), (30.0, -20.0, 2.9), (12.0, -22.0, 2.6)]   # workshop gate -> creek north bank / bridge head
QUARRY = [(68.0, 18.0, 7.0), (88.0, 22.0, 8.4), (112.0, 28.0, 10.2), (118.0, 46.0, 12.0), (104.0, 56.0, 13.6), (112.0, 70.0, 15.6),
          (98.0, 82.0, 17.8), (86.0, 78.0, 19.4), (80.0, 74.0, 20.0)]
OVERLOOK_BACK = [(80.0, 74.0, 20.0), (66.0, 74.0, 19.2), (50.0, 72.0, 17.8), (38.0, 68.0, 16.4), (28.0, 64.0, 15.2)]
REAR = [(8.0, 84.0, 16.0), (24.0, 74.0, 15.8), (28.0, 64.0, 15.2)]               # washhouse rear yard -> east terrace
TERRACE_X = [(-13.0, 60.0, 15.0), (8.0, 52.0, 15.0), (24.0, 60.0, 15.0), (28.0, 53.0, 15.0)]

ROUTES = {   # name: (points, clear width, kind)
    "cart road": (W_ROAD, 5.0, "road"), "outer trail": (W_TRAIL, 3.2, "lane"), "orchard lane": (W_ORCH, 3.4, "lane"),
    "orchard breach": (BREACH, 3.2, "lane"), "creek path": (C_CREEK, 3.6, "lane"), "footbridge": (C_BRIDGE, 2.0, "bridge"),
    "court approach": (C_COURT, 4.0, "lane"), "north lane": (C_NORTH, 4.0, "lane"), "south bank": (E_BANK, 3.2, "lane"),
    "ford path": (E_FORD, 3.2, "lane"), "workshop lane": (E_YARD, 4.0, "lane"), "stair approach": (E_STAIRS, 2.2, "alley"),
    "court walkway": (WALKWAY, 3.6, "lane"), "workshop gate": (GATE, 3.4, "lane"), "quarry path": (QUARRY, 3.0, "lane"),
    "overlook path": (OVERLOOK_BACK, 3.0, "lane"), "rear yard": (REAR, 3.2, "lane"), "terrace": (TERRACE_X, 4.0, "lane"),
}

PUDDLES = [(-30.0, -59.5, 1.3), (-8.0, -50.6, 1.1), (6.0, -42.6, 1.5), (9.5, -17.5, 1.2), (2.0, -9.0, 1.0), (-1.0, -3.5, 1.4),
           (-60.0, -61.9, 1.2), (-84.0, -63.0, 1.6), (40.0, -55.8, 1.0), (62.0, -8.0, 1.3), (-20.0, 24.0, 1.1)]   # (x, z, radius m)
SPAWN_LOW = (-90.0, -73.0)     # in the cart yard, the cart shed behind and a stone wall screening it from the orchard
SPAWN_UP = (18.0, 109.0)       # the screened yard behind the washhouse
COURT = (-3.0, 2.0)

# far ridge (outside the north-east boundary) and the cable-haul station on it
RIDGE = [(150.0, 300.0, 98.0), (205.0, 262.0, 112.0), (255.0, 232.0, 128.0), (300.0, 214.0, 141.0), (345.0, 190.0, 132.0),
         (372.0, 150.0, 118.0)]
STATION = [("mod_nt_tower_crane", 240.0, 240.0, 210.0), ("mod_nt_radar_tower", 292.0, 216.0, 0.0), ("small_storehouse", 268.0, 225.0, 205.0)]

# (asset, x, z, yaw, family): stand-ins at the spec's family sizes; front = local +z (yaw 180 faces south)
BUILDINGS = [
    ("vil_stone_barn", -80.0, -82.5, 0.0, "cart shed (interior)"),
    ("sd_market_lean_to", -64.0, -78.0, 270.0, "covered firewood"),
    ("house_farmhouse_2story", -104.0, 16.0, 90.0, "orchard farmhouse"),
    ("small_storehouse", -98.0, -44.0, 90.0, "orchard shed"),
    ("small_stone_dairy", -12.0, 9.0, 90.0, "pump room"),
    ("house_stone_cottage", -20.0, -12.0, 0.0, "worker cottage"),
    ("house_stone_cottage", 16.0, 15.5, 180.0, "worker cottage"),
    ("house_stone_cottage", -19.0, 13.5, 90.0, "worker cottage"),
    ("sd_shade_canopy", 21.0, 2.5, 90.0, "court walkway roof"),
    ("small_carpenter_workshop", 54.0, -12.0, 90.0, "smithy"),
    ("small_storehouse", 78.0, -16.0, 0.0, "wood store"),
    ("vil_stone_barn", 80.0, 8.0, 90.0, "walk-through shed (workshop interior)"),
    ("small_storehouse", 50.0, 14.0, 270.0, "walk-through shed"),
    ("sd_shade_canopy", 70.0, -2.0, 0.0, "workshop canopy"),
    ("vil_rendered_shutters", 8.0, 64.5, 180.0, "washhouse (interior)"),
    ("small_storehouse", 18.5, 66.0, 90.0, "washhouse annex"),
    ("small_stone_dairy", 34.0, 60.5, 0.0, "terrace shed"),
    ("house_stone_cottage", -30.0, 72.0, 90.0, "worker cottage"),
    ("small_storehouse", 72.0, 82.0, 180.0, "quarry hut"),
]


# ---------------------------------------------------------------- terrain
def lane_dense(pts, step=0.5):
    """dense (x, z, floor) samples along a polyline of (x, z, floor) control points (straight segments)."""
    out = []
    for a, b in zip(pts, pts[1:]):
        L = math.hypot(b[0] - a[0], b[1] - a[1])
        n = max(1, int(L / step))
        for k in range(n):
            t = k / n
            out.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t))
    out.append(pts[-1])
    return out


def creek_bed(s_pts):
    """bed elevation along the creek by nearest control point interpolation."""
    xs = [p[0] for p in CREEK_CTRL]
    return lambda x: float(np.interp(x, xs, [p[2] for p in CREEK_CTRL]))


class Field:
    """a height field over the whole terrain at RES, filled harmonically between fixed cells."""

    def __init__(self, grid):
        self.x0, self.z0 = grid.org_x, grid.org_z
        self.nx = int(grid.size_x / RES) + 1
        self.nz = int(grid.size_z / RES) + 1
        self.H = np.zeros((self.nz, self.nx))
        self.fix = np.zeros((self.nz, self.nx), bool)
        X = self.x0 + np.arange(self.nx) * RES
        Z = self.z0 + np.arange(self.nz) * RES
        self.X, self.Z = np.meshgrid(X, Z)

    def ij(self, x, z):
        return int(round((z - self.z0) / RES)), int(round((x - self.x0) / RES))

    def set_mask(self, m, h):
        self.H[m] = h if np.isscalar(h) else h[m]
        self.fix |= m

    def rect(self, cx, cz, hx, hz):
        return (np.abs(self.X - cx) <= hx) & (np.abs(self.Z - cz) <= hz)

    def corridor(self, pts, half):
        """mask within `half` of a dense polyline and the floor there (nearest sample)."""
        d = lane_dense(pts, 0.5)
        P = np.array([[p[0], p[1]] for p in d])
        Hs = np.array([p[2] for p in d])
        xmin, zmin = P.min(0) - half - 1
        xmax, zmax = P.max(0) + half + 1
        sel = (self.X >= xmin) & (self.X <= xmax) & (self.Z >= zmin) & (self.Z <= zmax)
        idx = np.argwhere(sel)
        q = np.stack([self.X[sel], self.Z[sel]], 1)
        best = np.full(len(q), 1e9)
        hb = np.zeros(len(q))
        for k in range(0, len(P), 64):   # chunked nearest sample
            dd = np.hypot(q[:, None, 0] - P[None, k:k + 64, 0], q[:, None, 1] - P[None, k:k + 64, 1])
            j = dd.argmin(1)
            dm = dd[np.arange(len(q)), j]
            upd = dm < best
            best[upd] = dm[upd]
            hb[upd] = Hs[k:k + 64][j[upd]]
        m = np.zeros_like(self.fix)
        hh = np.zeros_like(self.H)
        ok = best <= half
        m[idx[ok, 0], idx[ok, 1]] = True
        hh[idx[ok, 0], idx[ok, 1]] = hb[ok]
        dist = np.full(self.H.shape, 1e9)
        dist[idx[:, 0], idx[:, 1]] = best
        hfull = np.zeros_like(self.H)
        hfull[idx[:, 0], idx[:, 1]] = hb
        return m, hh, dist, hfull

    def solve(self):
        """harmonic fill of the free cells: multigrid Jacobi (coarse to fine)."""
        H, F = self.H, self.fix
        levels = []
        h, f = H.copy(), F.copy()
        while min(h.shape) > 40:
            levels.append((h, f))
            # restrict: a fixed coarse cell takes the mean of its fixed fine cells
            ny, nx = (h.shape[0] + 1) // 2, (h.shape[1] + 1) // 2
            hp = np.zeros((ny * 2, nx * 2)); fp = np.zeros((ny * 2, nx * 2), bool)
            hp[:h.shape[0], :h.shape[1]] = h * f; fp[:h.shape[0], :h.shape[1]] = f
            s = hp.reshape(ny, 2, nx, 2).sum((1, 3)); c = fp.reshape(ny, 2, nx, 2).sum((1, 3))
            h = np.where(c > 0, s / np.maximum(c, 1), 0.0); f = c > 0
        sol = np.where(f, h, h[f].mean())
        for it in range(3000):
            sol = self._relax(sol, h, f)
        for h, f in reversed(levels):
            up = np.kron(sol, np.ones((2, 2)))[:h.shape[0], :h.shape[1]]
            sol = np.where(f, h, up)
            for it in range(60):
                sol = self._relax(sol, h, f)
        self.H = sol

    @staticmethod
    def _relax(s, h, f):
        p = np.pad(s, 1, mode="edge")
        avg = (p[:-2, 1:-1] + p[2:, 1:-1] + p[1:-1, :-2] + p[1:-1, 2:]) * 0.25
        return np.where(f, h, avg)

    def at(self, x, z):
        return float(ndimage.map_coordinates(self.H, [[(z - self.z0) / RES], [(x - self.x0) / RES]], order=1, mode="nearest")[0])

    def sample(self, xs, zs):
        return ndimage.map_coordinates(self.H, [(np.asarray(zs) - self.z0) / RES, (np.asarray(xs) - self.x0) / RES], order=1, mode="nearest")


def build_terrain(grid):
    F = Field(grid)
    X, Z = F.X, F.Z
    # far constraints: the valley walls beyond the envelope, the south-west valley gap, the far ridge
    ex0, ez0, ex1, ez1 = ENV
    outside = np.maximum.reduce([ex0 - X, X - ex1, ez0 - Z, Z - ez1])
    # a steep bank 8-12 m up within 12 m of the envelope edge (the rock faces / slopes that explain the boundary)
    north = Z > ez1
    rim_h = np.where(north, 26.0, np.where(Z < ez0, 14.0, 16.0))
    gap = (X < -60) & (Z < -60)                      # the south-west valley gap stays lower (the bright opening)
    rim_h = np.where(gap, 7.0, rim_h)
    m = (outside > 10) & (outside < 14)
    F.set_mask(m, rim_h)
    edge = (np.abs(X - X.min()) < 1) | (np.abs(X - X.max()) < 1) | (np.abs(Z - Z.min()) < 1) | (np.abs(Z - Z.max()) < 1)
    far_h = np.where(Z > 0, 70.0, 38.0)
    F.set_mask(edge, np.where(gap, 18.0, far_h))
    # the far ridge north-east: a crest line and its shoulders
    for (x0, z0, h0), (x1, z1, h1) in zip(RIDGE, RIDGE[1:]):
        m, hh, _, _ = F.corridor([(x0, z0, h0), (x1, z1, h1)], 9.0)
        F.set_mask(m, hh)
    # lanes first (their distance fields): pads keep 2.5 m off every lane edge, so the fill grades lane to pad
    lanes = {}
    near_lane = np.zeros(F.H.shape, bool)
    for name, (pts, w, kind) in ROUTES.items():
        m, hh, dist, hfull = F.corridor(pts, w / 2 + 0.6)
        lanes[name] = (dist, hfull, w, m, hh)
        near_lane |= dist < w / 2 + 2.5
    _set = F.set_mask
    F.set_mask = lambda m, h: _set(m & ~near_lane, h)
    # areas
    for name, ((cx, cz), (hx, hz), fl) in AREAS.items():
        if fl is not None:
            F.set_mask(F.rect(cx, cz, hx - 2.0, hz - 2.0), fl)
    (ox, oz), (ohx, ohz), _ = AREAS["Western orchard"]
    for z0, z1, fl in TERRACES:      # terraces: flat steps (retaining walls stand on the risers)
        m = (np.abs(X - ox) <= ohx - 1.0) & (Z >= z0 + 0.6) & (Z <= z1 - 0.6)
        F.set_mask(m, fl)
    (wx, wz), (whx, whz), _ = AREAS["Workshop yards"]
    m = F.rect(wx, wz, whx - 3.0, whz - 3.0)
    F.set_mask(m, np.clip(3.5 + (Z - (wz - whz)) / (2 * whz) * 4.0, 3.5, 7.5))
    # the creek-bend floor between the banks: the court approach and the south bank are lanes; the bend floor follows them
    # lanes (graded, flat across): they win over the areas they cross
    F.set_mask = _set
    for name in list(lanes):
        dist, hfull, w, m, hh = lanes[name]
        F.set_mask(m, hh)
        lanes[name] = (dist, hfull, w)
    # the stairs: straight flights (graded here, risers sculpted later)
    for st in STAIRS:
        m, hh, _, _ = F.corridor([st[0], st[1]], 1.2)
        F.set_mask(m, hh)
    # the terrace's retaining face: the ground below it stays at +6, so the terrace edge is a 9 m wall (the court is
    # reached by the north lane, the west stair or the service stair, not straight down the hill)
    F.set_mask((X >= -14.0) & (X <= 34.0) & (Z >= 38.5) & (Z <= 41.5), 6.0)
    # the quarry cut: the rock rises 4-6 m right behind the overlook (a cut face)
    m = (np.hypot(X - 84.0, Z - 92.0) < 9.0)
    F.set_mask(m, 27.0)
    # the creek corridor is left free here: carved after the fill
    F.solve()
    # the boundary: the ground just inside the envelope keeps its solved height, a rock face rises 12 m just outside it
    # (steeper than the 50 deg the player can climb), so the envelope is a solid edge explained by the valley walls
    inner = (outside > -1.5) & (outside <= 0.0)
    ring = (outside > 1.5) & (outside < 4.5)
    F.set_mask(inner, F.H.copy())
    F.set_mask(ring, F.H + 12.0)
    F.solve()
    H = F.H
    # gentle ground variation off the lanes and pads (0.0-0.35 m, 6-15 m scale)
    rs = np.random.default_rng(SEED)
    noise = ndimage.gaussian_filter(rs.standard_normal(H.shape), 14.0 / RES)
    noise = noise / (np.abs(noise).max() + 1e-9) * 0.35
    H += np.where(F.fix, 0.0, noise)
    # the creek: a channel along the bed profile, 3-5 m wide at the bed, asymmetric banks (steeper on the outside of the
    # bend), the bank flattened at the ford; lanes on embankments keep their grade
    _, _, d_cr, h_cr = F.corridor([(x, z, b) for x, z, b in CREEK_CTRL], 60.0)
    bed_half = 1.8 + 0.6 * np.sin(X / 23.0)
    bank = 0.42 + 0.18 * (np.sin(X / 31.0 + 1.0) > 0)              # low stone banks, 60-67 deg: crossed only at the bridges and the ford
    ford = np.hypot(X - FORD[0], Z - FORD[1])
    bank = np.where(ford < 12.0, 7.0, bank)                         # the ford: a long flat bank
    depth_cap = h_cr                                                 # the bed level (absolute)
    carve = np.where(d_cr <= bed_half, depth_cap, depth_cap + (d_cr - bed_half) / bank)
    Hc = np.minimum(H, carve)
    # the footbridge and the stone bridge span the channel: keep the abutment ground at the lane level
    H = Hc
    F.H = H
    return F, lanes, d_cr


def lane_dist(F, pts):
    d = lane_dense(pts, 1.0)
    P = np.array([[p[0], p[1]] for p in d])
    Hs = np.array([p[2] for p in d])
    tree_x, tree_z = P[:, 0], P[:, 1]
    out_d = np.full(F.H.shape, 1e9)
    out_h = np.zeros(F.H.shape)
    for k in range(0, len(P), 48):
        dd = np.hypot(F.X[..., None] - tree_x[None, None, k:k + 48], F.Z[..., None] - tree_z[None, None, k:k + 48])
        j = dd.argmin(-1)
        dm = np.take_along_axis(dd, j[..., None], -1)[..., 0]
        upd = dm < out_d
        out_d[upd] = dm[upd]
        out_h[upd] = Hs[k:k + 48][j[upd]]
    return out_d, out_h


def sculpt_details(F, lanes, d_cr):
    """pass 2-3 detail on the fine field: stair risers, snow shoulders at lane edges (gaps at crossings and gates)."""
    X, Z, H = F.X, F.Z, F.H
    for (sx0, sz0, sh0), (sx1, sz1, sh1) in STAIRS:
        L = math.hypot(sx1 - sx0, sz1 - sz0)
        ux, uz = (sx1 - sx0) / L, (sz1 - sz0) / L
        t = ((X - sx0) * ux + (Z - sz0) * uz)
        lat = np.abs(-(X - sx0) * uz + (Z - sz0) * ux)
        inside = (t >= 0) & (t <= L) & (lat <= 1.1)
        n = int(math.ceil((sh1 - sh0) / 0.28))      # risers of at most 0.28 m (under the 0.35 m step-up)
        step = np.floor(np.clip(t / L, 0, 0.9999) * n) / n * (sh1 - sh0) + sh0
        H[inside] = step[inside]
    # snow shoulders: 0.22 m drift ridges 1.0 m wide just outside each lane's clear width (pass 3: player-crossable)
    sh = np.zeros_like(H)
    for name, (dist, hfull, w) in lanes.items():
        if name in ("footbridge",):
            continue
        r = dist - (w / 2 + 0.2)
        ridge = np.clip(1.0 - np.abs(r - 0.5) / 0.5, 0, 1) * 0.22
        sh = np.maximum(sh, ridge)
    for name, (dist, hfull, w) in lanes.items():   # never on any lane's own clear width
        sh[dist <= w / 2 + 0.15] = 0.0
    for (gx, gz) in (BRIDGE_W, FOOTBRIDGE, FORD, (-46.0, -6.0), (14.0, 3.0), (58.0, -24.0), (8.0, 84.0)):   # gaps at crossings and gates
        sh[np.hypot(X - gx, Z - gz) < 9.0] = 0.0
    sh[d_cr < 4.0] = 0.0
    F.H = H + sh
    for (px, pz, r) in PUDDLES:   # meltwater pockets: shallow bowls in the lanes (8 cm); the puddles sit in them
        d2 = ((X - px) / r) ** 2 + ((Z - pz) / (r * 0.7)) ** 2
        F.H = F.H - 0.08 * np.clip(1.0 - d2, 0.0, 1.0)


# ---------------------------------------------------------------- output helpers
def write_sculpt(F, grid):
    """the engine's V8 sculpt layer: per 5-cell tile, 36 x 36 fine samples of (fine height - base height)."""
    from mapgen.terrain import KCELL, TILE_CELLS
    ts = KCELL * TILE_CELLS
    fine = 35
    fs = ts / fine
    tx_n = (grid.nx - 1) // TILE_CELLS
    tz_n = (grid.nz - 1) // TILE_CELLS
    tiles = []
    for tz in range(tz_n):
        for tx in range(tx_n):
            ox, oz = grid.org_x + tx * ts, grid.org_z + tz * ts
            xs = ox + np.arange(fine + 1) * fs
            zs = oz + np.arange(fine + 1) * fs
            XX, ZZ = np.meshgrid(xs, zs)
            hf = F.sample(XX.ravel(), ZZ.ravel()).reshape(XX.shape)
            hb = np.array([[grid.sample(x, z) for x in xs] for z in zs])
            dlt = hf - hb
            if np.abs(dlt).max() > 0.04:
                tiles.append({"key": tz * tx_n + tx, "d": [round(float(v), 3) for v in dlt.ravel()]})
    return {"fineN": 36, "tiles": tiles}


def build(ctx):
    cat = ctx.catalog
    R = rng(SEED)
    grid = Grid(W, D, margin=MARGIN)
    F, lanes, d_cr = build_terrain(grid)
    sculpt_details(F, lanes, d_cr)
    F.H = F.H + BASE
    # the base grid: the fine field at each vertex, a little smoothed into the cell so the sculpt carries the detail
    for j in range(grid.nz):
        zs = grid.vz(j)
        xs = np.array([grid.vx(i) for i in range(grid.nx)])
        grid.h[j * grid.nx:(j + 1) * grid.nx] = __import__("array").array("d", F.sample(xs, np.full(len(xs), zs)).tolist())
    grid.quantise()
    b = MapBuilder(cat, grid, Frame(W, D), seed=SEED)
    G = lambda x, z: F.at(x, z)   # the true ground (base + sculpt)
    paint = GroundPaint()

    # ground paint (greybox: the layer families the spec names, coarse)
    E = W / 2 + MARGIN
    paint.poly("dry_grass", [(-E, -E), (E, -E), (E, E), (-E, E)], weight=1.0, edge=0.5)
    for name, (pts, w, kind) in ROUTES.items():
        paint.line("mud" if kind in ("road", "lane") else "dirt", [(p[0], p[1]) for p in pts], w, weight=0.9, edge=1.0)
    paint.line("pebbles", CREEK, 9.0, weight=0.9, edge=2.0)
    for name, ((cx, cz), (hx, hz), fl) in AREAS.items():
        if name in ("Waterworks court", "Lower cart yard", "Workshop yards", "Washhouse terrace"):
            paint.poly("mud", [(cx - hx, cz - hz), (cx + hx, cz - hz), (cx + hx, cz + hz), (cx - hx, cz + hz)], weight=0.55, edge=3.0, ragged=2.0, seed=len(name))

    # buildings: stand-ins seated on the true ground at their pads
    n_bld = 0
    placed = []
    for aid, x, z, yaw, fam in BUILDINGS:
        e = b.place(aid, x, z, yaw, check=False, y=G(x, z), tags={"building"}, companions=False)
        if e is not None:
            n_bld += 1
            placed.append(e)

    # lines: orchard retaining walls (with gates), the breached orchard wall, the yard walls, the overlook rail
    seedc = [1]

    def line(tid, pts, spans=None, tags=()):
        ln = {"pts": [[round(p[0], 3), round(p[1], 3)] for p in pts], "seed": (seedc[0] * 2654435761) % 4294967296}
        if spans:
            ln["spans"] = spans
        seedc[0] += 1
        return b.place("line_" + tid, pts[0][0], pts[0][1], 0.0, check=False, cls="none", extra={"line": ln}, tags={"line", tid} | set(tags),
                       y=G(pts[0][0], pts[0][1]))

    (ox, oz), (ohx, ohz), _ = AREAS["Western orchard"]
    xw0, xw1 = ox - ohx + 1.0, ox + ohx - 1.0
    for z0, z1, fl in TERRACES[1:]:   # each riser carries a stone wall, gaps where the lanes climb through it
        gaps = []
        for pts, w in ((W_TRAIL, 3.2), (W_ORCH, 3.4)):
            for a, c in zip(pts, pts[1:]):
                if (a[1] - z0) * (c[1] - z0) <= 0 and a[1] != c[1]:
                    xc = a[0] + (c[0] - a[0]) * (z0 - a[1]) / (c[1] - a[1])
                    if xw0 < xc < xw1:
                        gaps.append({"s": round(xc - xw0, 2), "w": w + 2.0, "kind": "gap"})
        line("stone_wall", [(xw0, z0), (xw1, z0)], spans=gaps or None)
    line("stone_wall", [(-46.0, -46.0), (-46.0, 8.0)], spans=[{"s": 40.0, "w": 4.2, "kind": "gap"}])       # the breach (z -6)
    line("stone_wall", [(-108.0, -60.0), (-108.0, -86.0), (-96.0, -90.0)])                               # the cart yard's west wall
    line("stone_wall", [(-100.0, -67.0), (-87.5, -67.0)])                                                 # the start's screen (exits: cart road, creek path)
    line("stone_wall", [(-29.0, 40.0), (-14.0, 40.0)])                                                    # closes the hill between the north lane and the terrace face
    line("stone_wall", [(2.0, 114.5), (32.0, 114.5), (32.0, 96.0)])                                       # the screened rear yard
    line("post_wire_fence", [(68.0, 66.0), (76.0, 66.0), (88.0, 67.0)])                                   # the overlook rail (timber stand-in)
    line("stone_wall", [(-11.0, 43.2), (31.0, 43.2)])                                                     # the terrace parapet over its retaining face
    for xb in (ENV[0] + 1.0, ENV[2] - 1.0):   # the creek leaves the map through walled culverts (the boundary across the bed)
        zc = float(np.interp(xb, [p[0] for p in CREEK_CTRL], [p[1] for p in CREEK_CTRL]))
        line("mod_wall_compound", [(xb, zc - 6.0), (xb, zc + 6.0)])
    line("picket_fence", [(-104.0, -40.0), (-104.0, -20.0)])                                              # a crooked orchard rail (camera 1)

    # crossings: the stone bridge (cart road), the plank footbridge (bend), stepping stones at the ford
    b.place("drs_bridge_stone_small", BRIDGE_W[0], BRIDGE_W[1], 90.0, check=False, y=BASE + 2.6 - 0.0, tags={"crossing"})
    b.place("mp1_footbridge", FOOTBRIDGE[0], FOOTBRIDGE[1], 0.0, check=False, y=BASE + 2.2, tags={"crossing"})
    for k in range(3):
        b.place("mod_stepping_stones", FORD[0] + 1.2 * k, FORD[1] + 2.0 * k - 2.0, 300.0, check=False, y=G(FORD[0] + 1.2 * k, FORD[1] + 2.0 * k - 2.0), tags={"crossing"})

    # the far ridge: the placeholder cable-haul station (two trestles of different heights and a ridge shed)
    for aid, x, z, yaw in STATION:
        b.place(aid, x, z, yaw, check=False, y=G(x, z), tags={"station"}, companions=False)

    # ---------------------------------------------------------------- M2: the creek ribbon, puddles, winter vegetation
    cpath = write_creek_asset(ctx, F)
    from mapgen.catalog import Asset
    with open(cpath, encoding="utf-8") as fh:
        cat.assets["bt_creek"] = Asset(__import__("json").load(fh), cpath)   # written just now: into this run's catalogue
    if "bt_creek" in cat:
        b.place("bt_creek", 0.0, 0.0, 0.0, check=False, cls="none", y=0.0, tags={"water"}, companions=False)
    rep_veg = dress_vegetation(b, cat, G, R)

    # ---------------------------------------------------------------- scenario: 10 v 10 Team Deathmatch
    sc = ScenarioBuilder(b, defending="Axis")
    sc.objective("Waterworks court", COURT[0], COURT[1], radius=12.0, capture_time=30.0, attack_from=SPAWN_LOW)
    spawns = []
    for team, (cx, cz), fac, face in ((0, SPAWN_LOW, "Allies", (-60.0, -52.0)), (1, SPAWN_UP, "Axis", (0.0, 70.0))):
        k = 0
        for r in range(2):
            for c in range(5):
                x, z = cx + (c - 2) * 2.4, cz + (r - 0.5) * 3.0
                m = sc.spawn("attacker" if team == 0 else "defender", [0], x, z, face=face, faction=fac,
                             label="%s %d" % ("Lower" if team == 0 else "Upper", k + 1), rack=None)
                m["team"], m["phases"] = team, []
                m["pos"][1] = round(G(x, z), 3)
                k += 1
                spawns.append(m)
    P = lambda x, z: [round(x, 2), round(G(x, z), 2), round(z, 2)]
    sd = {"bounds": [[ENV[0], ENV[1]], [ENV[2], ENV[1]], [ENV[2], ENV[3]], [ENV[0], ENV[3]]],
          "centre": P(*COURT),
          "timingRoutes": [{"name": n, "kind": "note", "points": [P(p[0], p[1]) for p in pts]} for n, (pts, w, kd) in ROUTES.items()],
          "walkRoutes": [{"name": n, "width": w, "points": [P(p[0], p[1]) for p in lane_dense(pts, 1.0)]} for n, (pts, w, kd) in ROUTES.items()]
                        + [{"name": "stair %d" % (k + 1), "width": 1.8, "points": [P(p[0], p[1]) for p in lane_dense([st[0], st[1]], 0.5)]} for k, st in enumerate(STAIRS)]}
    rules = {"mode": "tdm", "teamPlayers": [10, 10], "teamFaction": ["Allies", "Axis"], "sd": sd}

    # ---------------------------------------------------------------- cameras (six bookmarks) and output
    eye = 1.65

    def cam(x, z, tx, tz, dy=0.0, pitch=0.0):
        return {"position": [round(x, 2), round(G(x, z) + eye + dy, 2), round(z, 2)], "yaw": round(look_yaw(tx - x, tz - z), 2), "pitch": pitch}
    cams = [("Orchard approach", cam(-101.5, -36.0, 8.0, 64.0, pitch=4.0)),
            ("Creek bend", cam(14.0, -45.5, 8.0, -18.0, pitch=1.0)),
            ("Wall breach", cam(-48.2, -7.0, -31.0, 33.0)),
            ("Service passage", cam(26.5, 60.0, 24.0, 76.0)),
            ("Workshop yard", cam(70.0, -2.0, 40.0, -30.0)),
            ("Quarry overlook", cam(84.0, 76.0, -20.0, 10.0, dy=0.6, pitch=-6.0))]
    bookmarks = {str(k + 1): dict(c, name=n) for k, (n, c) in enumerate(cams)}
    tdir = os.path.join(ctx.repo, "content", "templates")
    grid.write_png(os.path.join(tdir, TID + ".height.png"))
    views = [{"name": "plan_high", "pos": [0.0, 260.0, -20.0], "yaw": 0.0, "pitch": -89.0}]
    for n, c in cams:
        views.append({"name": n.lower().replace(" ", "_"), "pos": c["position"], "yaw": c["yaw"], "pitch": c["pitch"], "gameplay": True})
    cam0 = ([0.0, 150.0, -170.0], 0.0, -40.0)
    # spec 6: bright overcast over dark slushy ground; near air ~0.0025 / m (93 % at 30 m, 61 % at 200 m, 11 % at 900 m),
    # a valley mist layer (12 m e-folding) hugging the creek and the low ground
    look = {"weather": 0.9, "clouds": 0.95, "haze": 2.4, "hazeHeight": 90.0, "envFog": 1, "fogNear": 6.0,
            "fogLayer": {"density": 0.0015, "height": 12.0}, "saturation": 0.74, "tint": [0.96, 0.99, 1.04],
            "bloom": 1.1, "grime": 1.3, "season": "winter", "leafless": True,
            "snowfall": {"near": 150, "far": 2600, "fall": 1.0, "wind": [0.8, -0.6], "bright": 1.0}}   # SF1: light fall, wind from the north-west
    sculpt = write_sculpt(F, grid)
    write_map(os.path.join(tdir, TID + ".nmap.json"), name=NAME, template_id=TID, template_version=VERSION, seed=SEED, width=W, depth=D,
              builder=b, scenario_json=sc.to_json(rules_extra=rules), explore_start=((SPAWN_LOW[0], G(*SPAWN_LOW), SPAWN_LOW[1]), 45.0),
              camera=cam0, bookmarks=bookmarks,
              extra={"groundPaint": paint.to_json(), "terrainSculpt": sculpt, "snow": snow_mask(F, tdir),
                     "lighting": {"hour": 15.3, "enclosed": False, "fill": 1.3}, "look": look})
    write_template(os.path.join(tdir, TID + ".json"), tid=TID, name=NAME, order=22, version=VERSION, seed=SEED,
                   description="Team Deathmatch, 10 v 10 (milestone 1 greybox): a mountain hamlet in a late-winter thaw around a stepped "
                               "waterworks; three linked routes from the cart yard and the washhouse terrace meet at the waterworks court.",
                   width=W, depth=D, margin=MARGIN, heightmap=grid.meta(TID + ".height.png"), water_level=-1000.0,
                   map_file=TID + ".nmap.json", camera=cam0, thumbnail=TID + ".thumb.png",
                   thumb_camera=(cams[0][1]["position"], cams[0][1]["yaw"], 0.0), extra={"userMap": True, "views": views})
    # route report: lengths and grades from the true ground
    rep = {"buildings": n_bld, "instances": len(b.inst), "sculpt_tiles": len(sculpt["tiles"]), "vegetation": rep_veg, "routes": {}}
    for n, (pts, w, kd) in ROUTES.items():
        d = lane_dense(pts, 1.0)
        L = sum(math.hypot(q[0] - p[0], q[1] - p[1]) for p, q in zip(d, d[1:]))
        g = max(abs(G(q[0], q[1]) - G(p[0], p[1])) / max(1e-6, math.hypot(q[0] - p[0], q[1] - p[1])) for p, q in zip(d[::3], d[3::3])) if len(d) > 4 else 0
        rep["routes"][n] = {"length_m": round(L, 1), "width_m": w, "max_grade_pct": round(100 * g, 1),
                            "rise_m": round(G(pts[-1][0], pts[-1][1]) - G(pts[0][0], pts[0][1]), 1)}
    write_summary(os.path.join(ctx.out, TID + ".summary.json"), rep)
    for n, r in rep["routes"].items():
        print("ROUTE %-15s %6.1f m  w %.1f  rise %+5.1f m  max grade %5.1f %%" % (n, r["length_m"], r["width_m"], r["rise_m"], r["max_grade_pct"]))
    print({k: v for k, v in rep.items() if k != "routes"})


def creek_water(F):
    """the creek as stepped stream bodies: each reach its own level (bed + 0.35 m), outlines round the channel."""
    bodies = []
    dense = lane_dense([(x, z, BASE + b) for x, z, b in CREEK_CTRL], 2.0)
    n = 9
    seg = max(2, len(dense) // n)
    for k in range(0, len(dense) - 1, seg):
        part = dense[k:k + seg + 1]
        if len(part) < 2:
            continue
        lvl = min(p[2] for p in part) + 0.35
        left, right = [], []
        for a, c in zip(part, part[1:] + [part[-1]]):
            dx, dz = c[0] - a[0], c[1] - a[1]
            L = math.hypot(dx, dz) or 1.0
            nx, nz = -dz / L, dx / L
            left.append([round(a[0] + nx * 4.5, 2), round(a[1] + nz * 4.5, 2)])
            right.append([round(a[0] - nx * 4.5, 2), round(a[1] - nz * 4.5, 2)])
        mid = part[len(part) // 2]
        bodies.append({"id": len(bodies) + 1, "kind": "stream", "level": round(lvl, 3), "seed": [round(mid[0], 2), round(mid[1], 2)],
                       "outline": left + right[::-1]})
    return bodies

# ---------------------------------------------------------------- SN1 snow layer mask (spec 3)
SNOW_BOX = (ENV[0] - 20.0, ENV[1] - 20.0, (ENV[2] - ENV[0]) + 40.0, (ENV[3] - ENV[1]) + 40.0)
SNOW_RES = 0.25


def poly_dist(X, Z, pts, chunk=64):
    """distance from every (X, Z) to a polyline of (x, z[, ...]) points (dense, straight segments), and the arc length."""
    P = np.array([[p[0], p[1]] for p in pts])
    best = np.full(X.shape, 1e9)
    for a, b in zip(P[:-1], P[1:]):
        d = b - a
        L2 = max(1e-9, float(d @ d))
        t = np.clip(((X - a[0]) * d[0] + (Z - a[1]) * d[1]) / L2, 0.0, 1.0)
        dd = np.hypot(X - (a[0] + t * d[0]), Z - (a[1] + t * d[1]))
        best = np.minimum(best, dd)
    return best


def snow_mask(F, tdir):
    """R deposit x retention, G traffic, B melt, A meltwater pooling (0..255), over SNOW_BOX at SNOW_RES."""
    x0, z0, w, d = SNOW_BOX
    nx, nz = int(w / SNOW_RES), int(d / SNOW_RES)
    X, Z = np.meshgrid(x0 + (np.arange(nx) + 0.5) * SNOW_RES, z0 + (np.arange(nz) + 0.5) * SNOW_RES)
    rs = np.random.default_rng(SEED + 5)
    def blob(scale, k):   # stable low-frequency patch values 0..1
        g = ndimage.gaussian_filter(rs.standard_normal((nz // k + 2, nx // k + 2)), scale / (SNOW_RES * k))
        g = (g - g.min()) / (np.ptp(g) + 1e-9)
        return ndimage.zoom(g, (nz / g.shape[0], nx / g.shape[1]), order=1)[:nz, :nx]
    # deposit: open field and outer slopes 0.85 (55-85 % cover), sheltered yards / orchard 0.72 (45-75 %), scoured
    # crests lower (the 3-15 m patch field), busy roads and stairs ~0.5 with traffic (15-35 %)
    R = 0.9 - 0.32 * blob(9.0, 8)   # open ground: 55-85 % cover at the 0.70 threshold
    for name, ((cx, cz), (hx, hz), fl) in AREAS.items():
        m = (np.abs(X - cx) <= hx) & (np.abs(Z - cz) <= hz)
        R[m] = np.minimum(R[m], 0.84 - 0.26 * blob(5.0, 4)[m])   # sheltered yards and the orchard: 45-75 %
    G = np.zeros_like(R); B = np.zeros_like(R); A = np.zeros_like(R)
    for name, (pts, wd, kind) in ROUTES.items():
        dist = poly_dist(X, Z, lane_dense(pts, 2.0))
        busy = 0.55 if kind == "road" else 0.45 if kind == "lane" else 0.35
        G = np.maximum(G, busy * np.clip(1.0 - (dist - wd * 0.32) / (wd * 0.12 + 0.35), 0, 1))   # the trodden two thirds; snow keeps the edges (road 15-35 %)
    # wheel ruts: two dark wet lines 0.18-0.32 m wide, 1.6 m apart, inside each road and lane
    for name, (pts, wd, kind) in ROUTES.items():
        if kind not in ("road", "lane") or wd < 3.0:
            continue
        dd = lane_dense(pts, 1.0)
        for side in (-1, 1):
            off = []
            for a, c in zip(dd, dd[1:] + [dd[-1]]):
                tx, tz = c[0] - a[0], c[1] - a[1]
                L = math.hypot(tx, tz) or 1.0
                off.append((a[0] - tz / L * 0.8 * side, a[1] + tx / L * 0.8 * side))
            rd = poly_dist(X, Z, off)
            k = np.clip(1.0 - rd / (0.12 + 0.04 * blob(2.0, 2)), 0, 1)
            G = np.maximum(G, 0.9 * k)
            A = np.maximum(A, k * np.clip((blob(3.0, 2) - 0.62) * 4.0, 0, 1))   # meltwater only in low pockets of the ruts
    # door pads and gate gaps: packed, then clear
    for aid, x, z, yaw, fam in BUILDINGS:
        m = np.hypot(X - x, Z - z) < 9.0
        G[m] = np.maximum(G[m], 0.12)
    for (gx, gz) in ((-46.0, -6.0), (58.0, -24.0), (8.0, 84.0), (-87.0, -67.0)):
        m = np.hypot(X - gx, Z - gz) < 2.2
        B[m] = np.maximum(B[m], 0.9)
    # melt: dirty snow at the creek's water line, the smithy's warm yard
    cd = poly_dist(X, Z, lane_dense([(x, z, b) for x, z, b in CREEK_CTRL], 2.0))
    B = np.maximum(B, 0.55 * np.clip(1.0 - (cd - 2.5) / 2.5, 0, 1))
    A = np.maximum(A, 0.6 * np.clip(1.0 - (cd - 3.2) / 0.6, 0, 1) * (blob(2.5, 2) > 0.55))   # still edge pockets
    sm = np.hypot(X - 58.0, Z - (-12.0))
    B = np.maximum(B, 0.8 * np.clip(1.0 - (sm - 3.0) / 6.0, 0, 1))
    img = np.stack([R, G, B, A], -1)
    img = np.clip(img * 255.0 + 0.5, 0, 255).astype(np.uint8)
    from PIL import Image
    Image.fromarray(img, "RGBA").save(os.path.join(tdir, TID + ".snow.png"), optimize=True)
    return {"mask": TID + ".snow.png", "box": [SNOW_BOX[0], SNOW_BOX[1], SNOW_BOX[2], SNOW_BOX[3]],
            "slope": [0.62, 0.9], "threshold": 0.7, "soft": 0.08, "roof": 0.95, "roofSlope": 0.42, "noise": 0.45, "melt": 0.0}


# ---------------------------------------------------------------- M2: the creek as one graded, flowing ribbon (water.cpp BT1)
_CREEK_DENSE = None


def creek_dist(x, z):
    global _CREEK_DENSE
    if _CREEK_DENSE is None:
        _CREEK_DENSE = lane_dense([(cx, cz, b) for cx, cz, b in CREEK_CTRL], 1.0)
    return min(math.hypot(x - p[0], z - p[1]) for p in _CREEK_DENSE[::2])


def write_creek_asset(ctx, F):
    """content/assets/bt_creek.json: the creek (a stream surface graded along its centreline, the current slowing to
    still edges where thin ice forms) and the lane puddles (thin ice skins), placed once at the origin."""
    import json
    dense = lane_dense([(x, z, BASE + b + 0.32) for x, z, b in CREEK_CTRL], 2.0)
    left, right = [], []
    for a, c in zip(dense, dense[1:] + [dense[-1]]):
        dx, dz = c[0] - a[0], c[1] - a[1]
        L = math.hypot(dx, dz) or 1.0
        nx, nz = -dz / L, dx / L
        left.append([round(a[0] + nx * 4.2, 2), round(a[1] + nz * 4.2, 2)])
        right.append([round(a[0] - nx * 4.2, 2), round(a[1] - nz * 4.2, 2)])
    surfaces = [{"kind": "stream", "outline": left + right[::-1], "level": round(max(p[2] for p in dense), 3),
                 "path": [[round(p[0], 2), round(p[1], 2), round(p[2], 3)] for p in dense], "speed": 0.7, "halfWidth": 2.6,
                 "clarity": 0.8, "waves": 0.8, "ice": 0.55}]
    for (px, pz, r) in PUDDLES:
        lvl = F.at(px, pz) + 0.05
        ring = [[round(px + math.cos(t) * r * (1.0 + 0.12 * math.sin(3 * t)), 2), round(pz + math.sin(t) * r * 0.7 * (1.0 + 0.1 * math.cos(2 * t)), 2)]
                for t in [2 * math.pi * k / 14 for k in range(14)]]
        surfaces.append({"kind": "puddle", "outline": ring, "level": round(lvl, 3), "ice": 0.35})
    xs = [p[0] for p in dense]
    zs = [p[1] for p in dense]
    j = {"id": "bt_creek", "version": 1, "name": "Blackwater Thaw creek", "category": "Water",
         "description": "BT1: the Blackwater Thaw creek as one graded, flowing stream surface with still edge pockets and thin shore ice, and the lane puddles. Generated by tools/mapgen/maps/blackwater_thaw.py; placed once at the origin.",
         "floors": 0, "enterable": False, "footprint": [round(max(xs) - min(xs), 1), round(max(zs) - min(zs), 1)], "plinthDepth": 0, "maxGrade": 90,
         "noAutoPanels": True, "nav": {"effect": "none"}, "water": {"surfaces": surfaces}, "materials": [], "parts": [],
         "furniture": [], "interactables": [], "panels": []}
    path = os.path.join(ctx.repo, "content", "assets", "bt_creek.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(j, f, separators=(",", ":"))
    return path


# ---------------------------------------------------------------- M2: winter vegetation families (spec 5)
def dress_vegetation(b, cat, G, R):
    rep = {}
    lanes = [(lane_dense(pts, 2.0), w) for pts, w, k in ROUTES.values()] + [(lane_dense([st[0], st[1]], 1.0), 2.4) for st in STAIRS]

    def lane_gap(x, z):
        return min(min(math.hypot(x - p[0], z - p[1]) for p in pts[::2]) - w / 2 for pts, w in lanes)

    def put(aid, x, z, yaw, key, clear=1.2, inside=True):
        if aid not in cat:
            return False
        if inside and not (ENV[0] + 3 < x < ENV[2] - 3 and ENV[1] + 3 < z < ENV[3] - 3):
            return False
        if lane_gap(x, z) < clear or b.overlaps(cat[aid], x, z, yaw, {"flat", "none", "veg"}):
            return False
        e = b.place(aid, x, z, yaw, check=False, cls="veg", y=G(x, z), tags={"veg", key}, companions=False)
        rep[key] = rep.get(key, 0) + (e is not None)
        return e is not None

    # pruned orchard trees: loose rows 5-6 m apart on the terraces (bare in winter: look.leafless)
    (ox, oz), (ohx, ohz), _ = AREAS["Western orchard"]
    for z0, z1, fl in TERRACES:
        zz = z0 + 2.6
        while zz < z1 - 1.8:
            xx = ox - ohx + 4.0 + R.uniform(-1, 1)
            while xx < ox + ohx - 3.0:
                if R.random() > 0.12:   # gaps where trees died
                    put("tree_apple", xx + R.uniform(-0.6, 0.6), zz + R.uniform(-0.5, 0.5), R.uniform(0, 360), "orchard", clear=2.2)
                xx += R.uniform(4.6, 6.4)
            zz += R.uniform(5.0, 6.5)
    # bare alder / willow along the creek (dead ash and oak forms), not at the crossings
    for k, (x, z, _) in enumerate(lane_dense([(cx, cz, 0) for cx, cz, _b in CREEK_CTRL], 9.0)):
        if not (ENV[0] < x < ENV[2]):
            continue
        if min(math.hypot(x - c[0], z - c[1]) for c in (BRIDGE_W, FOOTBRIDGE, FORD)) < 12.0:
            continue
        side = 1 if k % 2 else -1
        put(R.choice(["nt3_tree_dead_ash", "nt3_tree_dead_oak", "nt3_tree_dead_ash"]), x + R.uniform(-1.5, 1.5), z + side * R.uniform(4.5, 6.5), R.uniform(0, 360), "creek_trees", clear=2.0)
    # fir and spruce: ridgeline masses outside the envelope and border screens just inside it
    firs = ["ph_fir_tree_01_a", "ph_fir_tree_01_b", "ph_fir_tree_01_c", "ph_fir_sapling_medium_a", "ph_fir_sapling_medium_b"]
    for _ in range(2600):
        x, z = R.uniform(ENV[0] - 70, ENV[2] + 70), R.uniform(ENV[1] - 60, ENV[3] + 90)
        out = max(ENV[0] - x, x - ENV[2], ENV[1] - z, z - ENV[3])
        if out < -6.0 or (out < 4.0 and R.random() < 0.7):
            continue
        if x < -60 and z < -60 and R.random() < 0.75:   # the south-west valley gap stays open (the bright opening)
            continue
        put(R.choice(firs), x, z, R.uniform(0, 360), "firs", clear=3.0, inside=False)
    # dead grass and frosted seed heads at path edges, orchard margins and unused strips; bramble at wall feet
    for _ in range(900):
        x, z = R.uniform(ENV[0] + 4, ENV[2] - 4), R.uniform(ENV[1] + 4, ENV[3] - 4)
        g = lane_gap(x, z)
        if g < 1.0 or g > 14.0:
            continue
        r = R.random()
        aid = "nat_drygrass_tufts" if r < 0.4 else "nat_drygrass_tall" if r < 0.7 else "nat_drygrass_sparse" if r < 0.85 else "shrub_bramble"
        put(aid, x, z, R.uniform(0, 360), "ground_veg", clear=0.8)
    return rep
