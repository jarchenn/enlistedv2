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
from mapgen.geom import circle_poly

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
ORCH_IDS = []
PAINT = [None]   # milestone 3: the map's ground paint while dressing (the castle road)
FIELD = [None]   # BT2 perf: the terrain field while building (stand_seen)
CREEK_PROFILE = []   # BT2: (x, z, water level above BASE) along the creek, 2 m apart (build_terrain)
FIR_STANDS = []   # BT2: (x, z, radius) of each fir stand (dark needle floor, no snow under it)
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
    # BT2 creek-to-court slice (footbridge -> court): a stepped cluster on the court approach. The cottage's door and the
    # annex's door face the lane (~4.6 m wall to wall at its narrowest); a 2.1 m service gap between the two cottages
    ("house_stone_cottage", 1.0, -20.0, 90.0, "slice cottage"),
    ("house_stone_cottage", -7.3, -20.0, 270.0, "slice cottage (west)"),
    ("sd_market_lean_to", 1.0, -25.6, 180.0, "slice lean-to"),
    ("small_stone_dairy", 14.8, -14.9, 270.0, "slice pump annex"),
]
SLICE_PASSAGE = (-3.15, -27.0)   # the service gap's south mouth (passage bookmark)
OVERLOOK_EYE = (63.0, 58.5)      # the improved overlook bookmark: on the quarry top's south-west lip, scrub and rock just ahead


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
    # BT2: no wall at the envelope. The far edge of the terrain carries the high, asymmetric ridges (north highest, the
    # south-west valley exit lowest); the apron and the near shoulders are set after the first solve (below)
    gap = (X < -60) & (Z < -60)                      # the south-west valley exit (the bright opening)
    edge = (np.abs(X - X.min()) < 1) | (np.abs(X - X.max()) < 1) | (np.abs(Z - Z.min()) < 1) | (np.abs(Z - Z.max()) < 1)
    far_h = np.where(Z > 120, 112.0, np.where(Z > 0, 88.0, np.where(X > 0, 66.0, 54.0)))
    F.set_mask(edge, np.where(gap, 24.0, far_h))   # Phase 1 leftover: a shallower notch (16 m read as a pale wedge)
    castle_edge(F, X, Z, edge)   # BT2 phase 2: the castle spur (north-west, beyond the play area)
    castle_terrain(F, X, Z)
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
    # BT2 boundary as depth layers, not a wall: the ground at the envelope keeps its solved height; a graded apron rises
    # 6-14 m over the first ~20 m outside; irregular near ridge shoulders ~50 m out (+26..+56, broken crests, low saddles),
    # cut down at the south-west valley exit; the far ridges at the terrain's edge. The play limit is a hidden collision
    # line in the trees and rock (bt_boundary), not visible terrain.
    inner = (outside > -1.5) & (outside <= 0.0)
    F.set_mask(inner, F.H.copy())
    def lowf(scale, seed):
        r_ = np.random.default_rng(seed)
        v = ndimage.gaussian_filter(r_.standard_normal(F.H.shape), scale / RES)
        return (v - v.min()) / (np.ptp(v) + 1e-9)
    v1, v2 = lowf(16.0, SEED + 11), lowf(34.0, SEED + 12)
    spur = castle_spur_zone(X, Z)
    apron = (outside > 18.0) & (outside < 22.0) & ~spur
    F.set_mask(apron, F.H + 4.0 + 7.0 * v1)
    shoulder = (outside > 62.0) & (outside < 68.0) & ~spur   # far enough out that the slope to it stays ~25-35 deg, not a bowl wall
    sh_h = F.H + 16.0 + 28.0 * v2 * (0.6 + 0.4 * v1)
    F.set_mask(shoulder, np.where(gap, F.H + 6.0, sh_h))
    castle_road_terrain(F)   # the explore-only road out of the play area and up the spur (graded, overrides the bands)
    F.solve()
    H = F.H
    # gentle ground variation off the lanes and pads (0.0-0.35 m, 6-15 m scale)
    rs = np.random.default_rng(SEED)
    noise = ndimage.gaussian_filter(rs.standard_normal(H.shape), 14.0 / RES)
    noise = noise / (np.abs(noise).max() + 1e-9) * 0.35
    H += np.where(F.fix, 0.0, noise)
    # the creek: a channel along the bed profile, 3-5 m wide at the bed, asymmetric banks (steeper on the outside of the
    # bend), the bank flattened at the ford; lanes on embankments keep their grade
    # BT2 (user): the water 0.3-0.6 m under the lower bank top, not at the bottom of a trench: the bed is filled up to
    # that line. The water keeps 0.9 m under each bridge deck, the ford keeps its wadeable depth, and the level never
    # rises downstream below the stone bridge (so the east gorge below the ford stays as it is)
    # the south bank's low patch below the stone bridge (x -78..-70) lifted to its neighbours' height, so it does not
    # pin the whole village reach's water line down (it sits between the creek and the creek path, off the lane)
    dip = np.clip(1.0 - np.maximum(np.maximum(-79.0 - X, X + 69.0), np.maximum(-58.5 - Z, Z + 55.0)) / 1.5, 0.0, 1.0)
    H = np.where(dip > 0, np.maximum(H, H + (2.05 - H) * dip * (H < 2.05)), H)
    def Hs(px, pz):
        return float(ndimage.map_coordinates(H, [[(pz - F.z0) / RES], [(px - F.x0) / RES]], order=1, mode="nearest")[0])
    dense = lane_dense([(x, z, b) for x, z, b in CREEK_CTRL], 2.0)
    prof = []
    for i, (x, z, b) in enumerate(dense):
        a_, c_ = dense[max(i - 1, 0)], dense[min(i + 1, len(dense) - 1)]
        L = math.hypot(c_[0] - a_[0], c_[1] - a_[1]) or 1.0
        nx, nz = -(c_[1] - a_[1]) / L, (c_[0] - a_[0]) / L
        w = max(b + 0.32, min(Hs(x + nx * 5.5, z + nz * 5.5), Hs(x - nx * 5.5, z - nz * 5.5)) - 0.45)
        for (cx, cz), deck in ((BRIDGE_W, 2.6), (FOOTBRIDGE, 2.2)):
            if math.hypot(x - cx, z - cz) < 12.0:
                w = max(b + 0.32, min(w, deck - (0.9 if deck > 2.4 else 0.7)))
        if math.hypot(x - FORD[0], z - FORD[1]) < 14.0 or x < -100.0:   # the ford stays wadeable; west of the cart yard the
            w = b + 0.32                                                    # creek keeps its cut through the outer slopes
        if prof and x > BRIDGE_W[0] + 2.0:   # the cart-yard reach above the stone bridge stands on its own (a low yard bank)
            w = min(w, prof[-1][2])
        prof.append((x, z, w))
    CREEK_PROFILE[:] = prof
    _, _, d_cr, h_cr = F.corridor([(x, z, w - 0.32) for x, z, w in prof], 60.0)
    # Phase 1 leftover (brief 1.1): the channel's width and bank shapes change along the reach: a wandering bed (1.2-3.4 m
    # half width), a pool under the footbridge, stretches of gentle shingle shelf between steep cut banks
    rsc = np.random.default_rng(SEED + 21)
    wob = ndimage.gaussian_filter(rsc.standard_normal(X.shape), 9.0 / RES); wob = wob / (np.abs(wob).max() + 1e-9)
    shelf = ndimage.gaussian_filter(rsc.standard_normal(X.shape), 14.0 / RES); shelf = (shelf - shelf.min()) / (np.ptp(shelf) + 1e-9)
    pool = np.exp(-((X - FOOTBRIDGE[0]) ** 2 + (Z - FOOTBRIDGE[1]) ** 2) / (2 * 7.0 ** 2))
    bed_half = np.clip(1.8 + 0.6 * np.sin(X / 23.0) + 0.7 * wob + 1.1 * pool, 1.2, 3.4)
    bank = 0.42 + 0.18 * (np.sin(X / 31.0 + 1.0) > 0)              # low stone banks, 60-67 deg: crossed only at the bridges and the ford
    bank = np.where(shelf > 0.72, 1.4, bank)                        # shingle shelves (~35 deg) on some stretches
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
            cx_, cz_ = ox + ts / 2, oz + ts / 2   # M5: only where the play happens (envelope + 15 m) and the detail is real (> 0.1 m)
            inside = ENV[0] - 15 - ts < cx_ < ENV[2] + 15 + ts and ENV[1] - 15 - ts < cz_ < ENV[3] + 15 + ts
            if inside and np.abs(dlt).max() > 0.1:
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
    PAINT[0] = paint

    # ground paint (greybox: the layer families the spec names, coarse)
    E = W / 2 + MARGIN
    paint.poly("dry_grass", [(-E, -E), (E, -E), (E, E), (-E, E)], weight=1.0, edge=0.5)
    for name, (pts, w, kind) in ROUTES.items():
        # SN2 trails (the user's Ground109 / Ground110 sets through look.paintSets): wheeled ways gravel, foot ways dirt
        paint.line("gravel" if kind == "road" or name == "quarry path" else "dirt", [(p[0], p[1]) for p in pts], w, weight=0.9, edge=1.0)
    paint.line("pebbles", CREEK, 9.0, weight=0.9, edge=2.0)
    for name, ((cx, cz), (hx, hz), fl) in AREAS.items():
        if name in ("Waterworks court", "Lower cart yard", "Workshop yards", "Washhouse terrace"):
            paint.poly("gravel" if name in ("Lower cart yard", "Workshop yards") else "mud", [(cx - hx, cz - hz), (cx + hx, cz - hz), (cx + hx, cz + hz), (cx - hx, cz + hz)], weight=0.55, edge=3.0, ragged=2.0, seed=len(name))

    # buildings: stand-ins seated on the true ground at their pads
    n_bld = 0
    placed = []
    caps = write_snowcaps(ctx, cat, sorted({e[0] for e in BUILDINGS}))   # Phase 1 leftover: roof snow with thickness
    for aid, x, z, yaw, fam in BUILDINGS:
        e = b.place(aid, x, z, yaw, check=False, y=G(x, z), tags={"building"}, companions=False)
        if e is not None:
            n_bld += 1
            placed.append(e)
            if aid in caps:
                b.place(caps[aid], x, z, yaw, check=False, cls="none", y=e["pos"][1], tags={"snowcap"}, companions=False, support=e)

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
    write_boundary_asset(ctx, cat, G, gaps=[CASTLE_ROAD_EXIT])   # BT2: the play limit as a hidden collision line; a gap for the castle road
    write_boundary_gate(ctx, cat, G)   # closed (solid) only while a match runs: the castle is explore-only
    b.place("bt_boundary_gate", CASTLE_ROAD_EXIT[0], CASTLE_ROAD_EXIT[1], 90.0, check=False, cls="none", y=G(*CASTLE_ROAD_EXIT), tags={"boundary"}, companions=False)
    b.place("bt_boundary", 0.0, 0.0, 0.0, check=False, cls="none", y=0.0, tags={"boundary"}, companions=False)
    line("picket_fence", [(-104.0, -40.0), (-104.0, -20.0)])                                              # a crooked orchard rail (camera 1)

    # crossings: the stone bridge (cart road), the plank footbridge (bend), stepping stones at the ford
    b.place("drs_bridge_stone_small", BRIDGE_W[0], BRIDGE_W[1], 90.0, check=False, y=BASE + 2.6 - 0.0, tags={"crossing"})
    b.place("mp1_footbridge", FOOTBRIDGE[0], FOOTBRIDGE[1], 0.0, check=False, y=BASE + 2.2, tags={"crossing"})
    for k in range(3):
        b.place("mod_stepping_stones", FORD[0] + 1.2 * k, FORD[1] + 2.0 * k - 2.0, 300.0, check=False, y=G(FORD[0] + 1.2 * k, FORD[1] + 2.0 * k - 2.0), tags={"crossing"})

    # the far ridge: the cable-haul station (two lattice trestles of different heights, a machinery shed, the haul cable)
    sx, sz, sy = write_station_asset(ctx, G, cat)
    b.place("bt_cable_station", sx, sz, 0.0, check=False, cls="none", y=sy, tags={"station"}, companions=False)
    # BT2 phase 2: the hilltop castle on the north-west spur (graybox)
    n_castle = write_castle_asset(ctx, cat)
    b.place("bt_castle", CASTLE_C[0], CASTLE_C[1], CASTLE_YAW, check=False, cls="none", y=BASE + CASTLE_PAD, tags={"castle"}, companions=False)
    # the load-when-close rule on a stub room (the interior's stand-in until the rooms are built): built within 90 m of
    # the player, dropped beyond 130 m
    write_castle_interior(ctx, cat)
    ci = b.place("castle_interior", CASTLE_C[0], CASTLE_C[1], CASTLE_YAW, check=False, cls="none", y=BASE + CASTLE_PAD, tags={"castle"}, companions=False)
    if ci is not None:   # fixtures that go with the rooms (loaded with them): lanterns down the hall, the stair, bower, anteroom; tower sconces
        for lx, ly, lz, aid, gain in CASTLE_LAMPS:
            if aid in cat:
                wx, wz = castle_world(lx, lz)
                b.place(aid, wx, wz, CASTLE_YAW, check=False, cls="none", y=BASE + CASTLE_PAD + ly, tags={"castle", "lamp"}, companions=False,
                        extra={"loadWith": ci["id"], "light": lamp_light(gain)}, support=ci)
    # the waterworks court's stepped header channel, the court's north-east side, head to the north
    write_flume_asset(ctx, cat)
    b.place("bt_flume", 8.0, 15.0, 0.0, check=False, y=G(8.0, 15.0), tags={"flume"}, companions=False)
    # dressing by use (spec 4): the washhouse's pails, boards and porch; the smithy's fuel under cover, its fire, ash downwind
    for aid, x, z, yaw in (("sd_market_lean_to", 8.0, 57.4, 0.0), ("ph_wooden_bucket_01", 2.6, 57.6, 0.0), ("ph_wooden_bucket_02", 3.2, 57.3, 40.0),
                           ("ph_wooden_bucket_01", 13.7, 57.8, 10.0), ("pa_crate_slatted", 14.6, 57.2, 15.0),
                           ("drs_woodpile", 57.6, -17.0, 0.0), ("fire_brazier", 59.6, -10.5, 0.0), ("ph_wine_barrel_01", 59.4, -14.6, 0.0),
                           ("drs_woodpile", -64.0, -73.5, 90.0), ("drs_woodpile", -96.0, -2.0, 0.0),
                           ("lamp_lantern_post", 0.6, 56.2, 200.0)):   # M4: the one minor practical light (the washhouse porch, lit at dusk)
        if aid in cat:
            b.place(aid, x, z, yaw, check=False, y=G(x, z), tags={"dress"}, companions=False)
    if "bt_vent_smoke" in cat:   # the smithy's working chimney (small_carpenter_workshop's stack: local (-3.6, 6.05, -1.2))
        vx, vz = local_to_world((54.0, -12.0), 90.0, -3.6, -1.2)
        b.place("bt_vent_smoke", vx, vz, 0.0, check=False, cls="none", y=G(54.0, -12.0) + 5.9, tags={"fx"}, companions=False)
    # BT2 slice dressing: a dark trunk in the near third of the bridge shot, rough bank stones at the footbridge, the
    # cluster's woodpile off the lane, foreground scrub and chipped rock at the improved overlook
    for aid, x, z, yaw, sink in (("tr_dead_b", 17.5, -41.0, 40.0, 0.0),
                                 ("nat_boulder_schist_m", 7.2, -27.6, 20.0, 0.25), ("nat_boulder_schist_slab", 16.8, -28.2, 110.0, 0.2),
                                 ("nat_boulder_limestone_m", 8.0, -35.0, 200.0, 0.3), ("nat_boulder_schist_m", 16.4, -35.4, 300.0, 0.25),
                                 ("nat_boulder_granite_s", 4.5, -33.0, 70.0, 0.15), ("nat_boulder_granite_s", 19.5, -31.5, 150.0, 0.15),
                                 ("drs_woodpile", -2.6, -16.8, 0.0, 0.0),
                                 ("fl_bush_small_1", 61.5, 58.2, 30.0, 0.0), ("nat_boulder_granite_s", 62.6, 57.0, 75.0, 0.25),
                                 ("fl_bush_small_3", 61.0, 57.2, 160.0, 0.0)):
        if aid in cat:
            gy = G(x, z)
            if aid.startswith("tr_"):   # a tree stands at its lowest root contact (1.5 m ring), so no root floats on the bank
                gy = min([gy] + [G(x + 1.5 * math.cos(k * math.pi / 4), z + 1.5 * math.sin(k * math.pi / 4)) for k in range(8)])
            b.place(aid, x, z, yaw, check=False, cls="veg" if aid.startswith(("tr_", "fl_")) else "none",
                    y=gy - sink, tags={"dress", "slice"}, companions=False)
    paint.poly("pebbles", [(3.0, -29.5), (21.0, -29.0), (22.0, -34.5), (3.5, -36.5)], weight=0.8, edge=1.5, ragged=1.5, seed=31)   # the gravel margin at the footbridge
    paint.poly("scorched", [(61.0, -9.0), (65.0, -10.5), (64.0, -13.0), (60.5, -12.5)], weight=0.6, edge=1.2, ragged=0.8, seed=21)   # ash downwind (south-east)

    # ---------------------------------------------------------------- M2: the creek ribbon, puddles, winter vegetation
    cpath = write_creek_asset(ctx, F)
    from mapgen.catalog import Asset
    with open(cpath, encoding="utf-8") as fh:
        cat.assets["bt_creek"] = Asset(__import__("json").load(fh), cpath)   # written just now: into this run's catalogue
    if "bt_creek" in cat:
        b.place("bt_creek", 0.0, 0.0, 0.0, check=False, cls="none", y=0.0, tags={"water"}, companions=False)
    global ORCH_IDS
    # user: "get rid of the trees and any other vegetation generated by you, replace by tree 3d models": the orchard is
    # the library's bare broadleaf models (DJMaesen, CC-BY), not the part-built bt_orchard_tree_* any more
    ORCH_IDS = [a for a in ("fl_bare_tree_1", "fl_bare_tree_2", "fl_bare_tree_3") if a in cat]
    FIELD[0] = F   # BT2 perf: the fir stands' visibility test samples the ground in bulk
    rep_veg = dress_vegetation(b, cat, G, R)
    rep_veg["boundary_rock"] = dress_boundary(b, cat, G, R)
    rep_veg["castle_firs"] = dress_castle_spur(b, cat, G, R)
    rep_veg["castle_parts"] = n_castle
    for k, (cx, cz, rad) in enumerate(FIR_STANDS):   # BT2: the stands' dark needle floor
        paint.poly("leaf_litter", circle_poly(cx, cz, rad + 1.5, 14), weight=0.9, edge=3.0, ragged=2.5, seed=100 + k)

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
    # BT2: the second set (improved compositions); the six above stay for before/after comparison
    cams2 = [("Slice passage", cam(SLICE_PASSAGE[0], SLICE_PASSAGE[1], -3.0, 0.0, pitch=2.0)),
             ("Slice bridge", cam(13.5, -47.0, 4.0, -18.0, pitch=2.0)),
             ("Overlook improved", cam(OVERLOOK_EYE[0], OVERLOOK_EYE[1], -3.0, 2.0, dy=0.2, pitch=-9.0))]
    cams = cams + cams2
    # BT2 phase 2: the castle from the quarry (the panorama turned right toward it), the orchard and the creek path
    cams += [("Castle quarry", cam(84.0, 76.0, CASTLE_C[0], CASTLE_C[1], dy=0.6, pitch=7.0)),
             ("Castle orchard", cam(-101.5, -36.0, CASTLE_C[0], CASTLE_C[1], pitch=12.0)),
             ("Castle creek", cam(-36.0, -60.0, CASTLE_C[0], CASTLE_C[1], pitch=10.0))]
    bookmarks = {str(k + 1): dict(c, name=n) for k, (n, c) in enumerate(cams)}
    tdir = os.path.join(ctx.repo, "content", "templates")
    grid.write_png(os.path.join(tdir, TID + ".height.png"))
    views = [{"name": "plan_high", "pos": [0.0, 260.0, -20.0], "yaw": 0.0, "pitch": -89.0}]
    # M4 landmark close-ups (editor / eye views): the cable-haul station, the stepped flume, the washhouse
    sty = G(236.0, 242.0)
    views.append({"name": "station_close", "pos": [205.0, round(sty + 6.0, 2), 214.0], "yaw": round(look_yaw(268.0 - 205.0, 226.0 - 214.0), 2), "pitch": 6.0})
    views.append({"name": "flume_close", "pos": [13.0, round(G(13.0, 7.5) + 1.65, 2), 7.5], "yaw": round(look_yaw(8.0 - 13.0, 12.0 - 7.5), 2), "pitch": -6.0, "gameplay": True})
    ct = [e for e in b.inst if "creek_trees" in e["tags"]]
    if ct:   # BT2: a creek tree close up (its roots on the ground, on the bank's slope)
        e = ct[len(ct) // 2]; tx_, tz_ = e["pos"][0], e["pos"][2]
        views.append({"name": "creek_tree_close", "pos": [round(tx_ + 4.5, 2), round(G(tx_ + 4.5, tz_ - 3.0) + 1.65, 2), round(tz_ - 3.0, 2)],
                      "yaw": round(look_yaw(-4.5, 3.0), 2), "pitch": -8.0, "gameplay": True})
    views.append({"name": "washhouse_close", "pos": [4.0, round(G(4.0, 47.0) + 1.65, 2), 47.0], "yaw": round(look_yaw(8.0 - 4.0, 63.0 - 47.0), 2), "pitch": 4.0, "gameplay": True})
    for n, c in cams:
        views.append({"name": n.lower().replace(" ", "_"), "pos": c["position"], "yaw": c["yaw"], "pitch": c["pitch"], "gameplay": True})
    cam0 = ([0.0, 150.0, -170.0], 0.0, -40.0)
    # spec 6: bright overcast over dark slushy ground; near air ~0.0025 / m (93 % at 30 m, 61 % at 200 m, 11 % at 900 m),
    # a valley mist layer (12 m e-folding) hugging the creek and the low ground
    # BT2: thinner world haze (the old 2.4 made a uniform veil), the damp valley pocket carries the mist (~0-12 m, tapering
    # by ~30 m), lighter cloud deck, a cooler but less grey grade
    look = {"weather": 0.8, "clouds": 0.75, "haze": 1.6, "hazeHeight": 90.0, "envFog": 1, "fogNear": 6.0,
            "fogLayer": {"density": 0.003, "height": 22.0},
            # brief 2.2: a soft basin band over the castle spur's lowest trees and terrace wall (y ~65-90), tower tops clear
            "fogLayer2": {"density": 0.002, "floor": 55.0, "top": 100.0, "soft": 10.0}, "saturation": 0.8, "tint": [0.97, 0.99, 1.03],
            # BT2 sky (lighting session's per-map knobs): a soft low opening toward the south-west valley exit (azimuth
            # atan2(dx, dz) = -130.7), broad cloud banks stretched along the valley, no sun disc
            "paintSets": {"dirt": "trail_dirt", "gravel": "trail_gravel"},   # SN2: the user's trail sets on this map's ways
            "cloudOpening": [-130.7, 11.0, 20.0, 0.8], "cloudScale": 2.0, "cloudStretch": 3.0, "cloudDir": -130.7, "sunDisc": 0.0,
            "bloom": 1.1, "grime": 1.3, "season": "winter", "leafless": True, "grassDead": 1.0,
            "snowfall": {"near": 40, "far": 1800, "fall": 1.0, "wind": [0.8, -0.6], "bright": 1.0},   # BT2: sparse near flakes
            # GR3 night / dusk air for the same map (the time of day picks it): thicker low mist, a closer start
            "night": {"haze": 2.0, "envFog": 1, "fogNear": 3.0}}   # SF1: light fall, wind from the north-west
    sculpt = write_sculpt(F, grid)
    write_audio_anchors(tdir, G)
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
SNOW_BOX = (ENV[0] - 90.0, ENV[1] - 80.0, (ENV[2] - ENV[0]) + 180.0, (ENV[3] - ENV[1]) + 180.0)   # BT2: out over the fir stands
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
    for cx, cz, rad in FIR_STANDS:   # BT2: little snow under a fir stand (the crowns hold it): the stand reads as one dark mass
        m = np.hypot(X - cx, Z - cz) < rad + 4.0
        if m.any():
            k = np.clip((rad + 3.0 - np.hypot(X[m] - cx, Z[m] - cz)) / 4.0, 0, 1)
            R[m] = R[m] * (1.0 - 0.6 * k)
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
    dense = [(x, z, BASE + w) for x, z, w in CREEK_PROFILE]   # BT2: the raised water line (build_terrain)
    left, right = [], []
    for a, c in zip(dense, dense[1:] + [dense[-1]]):
        dx, dz = c[0] - a[0], c[1] - a[1]
        L = math.hypot(dx, dz) or 1.0
        nx, nz = -dz / L, dx / L
        left.append([round(a[0] + nx * 6.0, 2), round(a[1] + nz * 6.0, 2)])   # BT2: wider (the higher water spreads)
        right.append([round(a[0] - nx * 6.0, 2), round(a[1] - nz * 6.0, 2)])
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
    FIR_STANDS.clear()
    rep = {}
    lanes = [(lane_dense(pts, 2.0), w) for pts, w, k in ROUTES.values()] + [(lane_dense([st[0], st[1]], 1.0), 2.4) for st in STAIRS]
    if CASTLE_ROAD_WORLD:   # the castle road stays clear of plants and stands
        lanes.append((lane_dense(CASTLE_ROAD_WORLD, 2.0), 4.0))

    def lane_gap(x, z):
        return min(min(math.hypot(x - p[0], z - p[1]) for p in pts[::2]) - w / 2 for pts, w in lanes)

    def put(aid, x, z, yaw, key, clear=1.2, inside=True, seat=0.0, extra=None):
        if aid not in cat:
            return False
        if inside and not (ENV[0] + 3 < x < ENV[2] - 3 and ENV[1] + 3 < z < ENV[3] - 3):
            return False
        if lane_gap(x, z) < clear or b.overlaps(cat[aid], x, z, yaw, {"flat", "none", "veg"}):
            return False
        # BT2: seat = the root spread (m): the tree stands at the lowest ground under it (its lowest root contact), so no
        # root floats on a slope; the asset's model.sink buries the flare
        ring = [G(x, z)] + [G(x + seat * math.cos(t), z + seat * math.sin(t)) for t in [k * math.pi / 4 for k in range(8)]] if seat > 0 else [G(x, z)]
        if max(ring) - min(ring) > 2.0:   # BT2 (user): not over a drop-off (the 1.8 m ring, wider than the roots, sets the seat)
            return False
        y = min(ring)
        e = b.place(aid, x, z, yaw, check=False, cls="veg", y=y, tags={"veg", key}, companions=False, extra=extra)
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
                    put(R.choice(ORCH_IDS), xx + R.uniform(-0.6, 0.6), zz + R.uniform(-0.5, 0.5), R.uniform(0, 360), "orchard", clear=2.2, seat=1.6)
                xx += R.uniform(4.6, 6.4)
            zz += R.uniform(5.0, 6.5)
    # bare alder / willow along the creek (dead ash and oak forms), not at the crossings
    for k, (x, z, _) in enumerate(lane_dense([(cx, cz, 0) for cx, cz, _b in CREEK_CTRL], 9.0)):
        if not (ENV[0] < x < ENV[2]):
            continue
        if min(math.hypot(x - c[0], z - c[1]) for c in (BRIDGE_W, FOOTBRIDGE, FORD)) < 12.0:
            continue
        side = 1 if k % 2 else -1
        for _try in range(5):   # a few spots along the bank top before giving the slot up
            if put(R.choice(["nt3_tree_dead_ash", "nt3_tree_dead_oak", "nt3_tree_dead_ash"]), x + R.uniform(-3.0, 3.0), z + side * R.uniform(7.0, 11.0), R.uniform(0, 360), "creek_trees", clear=2.0, seat=1.8):
                break
    # Phase 1 leftover: occasional stone ledges at the water's edge (slabs set into the bank), alternating sides
    import random as _random
    RL = _random.Random(SEED + 31)   # its own stream: the fir stands after it stay where they were
    slabs = [a_ for a_ in ("nat_boulder_schist_slab", "nat_boulder_limestone_m", "nat_boulder_schist_m") if a_ in cat]
    for k, (x, z, _) in enumerate(lane_dense([(cx, cz, 0) for cx, cz, _b in CREEK_CTRL], 23.0)):
        if not (ENV[0] + 6 < x < ENV[2] - 6) or not slabs:
            continue
        if min(math.hypot(x - c[0], z - c[1]) for c in (BRIDGE_W, FOOTBRIDGE, FORD)) < 10.0:
            continue
        side = 1 if k % 2 else -1
        for _try in range(3):
            if put(RL.choice(slabs), x + RL.uniform(-2, 2), z + side * RL.uniform(2.6, 3.6), RL.uniform(0, 360), "ledges", clear=1.0):
                break
    # fir and spruce: ridgeline masses outside the envelope and border screens just inside it
    firs = ["ph_fir_tree_01_a", "ph_fir_tree_01_b", "ph_fir_tree_01_c", "ph_fir_sapling_medium_a", "ph_fir_sapling_medium_b"]
    # BT2: massed stands (8-16 overlapping crowns each) with open ground between, not an even sprinkle: a stand reads as
    # one dark mass at vista distance where single crowns broke into specks; the stands also hide the play limit
    n_st = 0
    while n_st < 240:
        cx, cz = R.uniform(ENV[0] - 70, ENV[2] + 70), R.uniform(ENV[1] - 60, ENV[3] + 90)
        out = max(ENV[0] - cx, cx - ENV[2], ENV[1] - cz, cz - ENV[3])
        if out < -4.0:
            continue
        if cx < -60 and cz < -60 and R.random() < 0.45:   # the south-west valley gap stays mostly open (the bright opening);
            continue                                          # stands on its flanks break the pale wedge it made
        if castle_spur_zone(cx, cz):   # the castle spur has its own stands (dress_castle_spur)
            continue
        n_st += 1
        rad = R.uniform(7.0, 13.0)
        # BT2 perf: a stand outside the play area that no route or start point can see is not placed; one seen only from
        # beyond 150 m keeps 3 trees in 5, one seen only from beyond 60 m drops its saplings (they hide behind the big crowns);
        # the RNG draws stay the same, so every other tree is where it was
        seen, near = (True, 0.0) if out < 0.0 else stand_seen(cx, cz, rad)
        keep = seen
        if keep:
            FIR_STANDS.append((cx, cz, rad))
        rep["stands_hidden"] = rep.get("stands_hidden", 0) + (not seen)
        for k_ in range(R.randint(12, 22)):
            a_, d_ = R.uniform(0, 2 * math.pi), rad * math.sqrt(R.random())
            x, z = cx + d_ * math.cos(a_), cz + d_ * math.sin(a_)
            if max(ENV[0] - x, x - ENV[2], ENV[1] - z, z - ENV[3]) < -6.0:
                continue
            ch, yw = R.choice(firs), R.uniform(0, 360)
            if not keep:
                continue
            thin = (near > 150.0 and k_ % 5 in (1, 3)) or (near > 60.0 and "sapling" in ch)   # saplings hide in a far stand
            # thinned trees stay in the map marked perfThin: the engine skips them while the firthin switch is on (A/B in one run)
            put(ch, x, z, yw, "firs_thin" if thin else "firs", clear=3.0, inside=False, extra={"perfThin": True} if thin else None)
    # dead grass and frosted seed heads at path edges, orchard margins and unused strips; bramble at wall feet
    for _ in range(900):
        x, z = R.uniform(ENV[0] + 4, ENV[2] - 4), R.uniform(ENV[1] + 4, ENV[3] - 4)
        g = lane_gap(x, z)
        if g < 1.0 or g > 14.0:
            continue
        r = R.random()
        if r >= 0.85:   # bramble at wall feet (already the library's bush model through the tree aliases)
            put("shrub_bramble", x, z, R.uniform(0, 360), "ground_veg", clear=0.8)
            continue
        # the user's request: library grass models (DJMaesen dry grass patches, CC-BY) instead of the part-built grass
        # pieces; one part-built piece covered ~7 m, a model ~0.7 m, so each spot gets a small cluster of them
        kinds = ["fl_drygrass_tuft_1", "fl_drygrass_tuft_2", "fl_drygrass_tuft_3"] if r < 0.4 or r >= 0.7 else ["fl_drygrass_patch_1", "fl_drygrass_patch_2", "fl_drygrass_patch_3"]
        for _k in range(4 if r < 0.7 else 2):
            a_, d_ = R.uniform(0, 2 * math.pi), R.uniform(0.0, 2.5)
            put(R.choice(kinds), x + d_ * math.cos(a_), z + d_ * math.sin(a_), R.uniform(0, 360), "ground_veg", clear=0.8)
    return rep


# ---------------------------------------------------------------- M3: generated landmark assets (part-built, written by this script)
M_STATION = [{"name": "timber", "albedo": [0.20, 0.18, 0.16], "roughness": 0.85, "pattern": "wood", "metallic": 0, "grain": 1},
             {"name": "iron", "albedo": [0.16, 0.14, 0.13], "roughness": 0.6, "pattern": "metal", "metallic": 0.5, "grain": 1},
             {"name": "cable", "albedo": [0.08, 0.08, 0.08], "roughness": 0.5, "pattern": "metal", "metallic": 0.6, "grain": 1},
             {"name": "shed_board", "albedo": [0.24, 0.22, 0.20], "roughness": 0.85, "pattern": "wood", "metallic": 0, "grain": 1},
             {"name": "roof_iron", "albedo": [0.18, 0.15, 0.13], "roughness": 0.7, "pattern": "metal", "metallic": 0.3, "grain": 1},
             {"name": "stone", "albedo": [0.36, 0.35, 0.33], "roughness": 0.9, "pattern": "stone", "metallic": 0, "grain": 1}]


def _box(c, s, m, r=None, k=1):
    p = {"t": "b", "c": [round(v, 3) for v in c], "s": [round(v, 3) for v in s], "m": m}
    if r:
        p["r"] = [round(v, 2) for v in r]
    if not k:
        p["k"] = 0
    return p


def _beam(a, b, w, m, k=1):
    """a box from point a to point b (x, y, z), w thick: its length along local y, rotated into place."""
    dx, dy, dz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
    L = math.sqrt(dx * dx + dy * dy + dz * dz)
    c = ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2)
    yaw = math.degrees(math.atan2(dx, dz))
    pitch = math.degrees(math.atan2(math.hypot(dx, dz), dy))   # tilt from vertical toward (dx, dz)
    return _box(c, (w, L, w), m, r=(pitch, yaw, 0.0) if L > 1e-6 else None, k=k)


def _trestle(x, z, base_y, top_y, half=2.2, top_half=0.9):
    """a lattice trestle from its own ground (base_y) to top_y: four leaning legs, X braces every ~4 m, a crossarm, two sheaves."""
    parts = []
    corners = [(-1, -1), (1, -1), (1, 1), (-1, 1)]
    legs = []
    for sx, sz in corners:
        a = (x + sx * half, base_y, z + sz * half)
        b = (x + sx * top_half, top_y, z + sz * top_half)
        legs.append((a, b))
        parts.append(_beam(a, b, 0.32, "timber"))
    H = top_y - base_y
    n = max(2, int(H / 4.2))
    lerp = lambda a, b, t: (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t)
    for k in range(n):
        t0, t1 = k / n, (k + 1) / n
        for i in range(4):
            la, lb = legs[i], legs[(i + 1) % 4]
            p0, p1 = lerp(*la, t0), lerp(*lb, t1)
            q0, q1 = lerp(*lb, t0), lerp(*la, t1)
            parts.append(_beam(p0, p1, 0.12, "timber"))
            parts.append(_beam(q0, q1, 0.12, "timber"))
            parts.append(_beam(lerp(*la, t1), lerp(*lb, t1), 0.16, "timber"))
    parts.append(_box((x, top_y + 0.25, z), (top_half * 2 + 3.6, 0.5, 0.6), "timber"))       # the crossarm
    for sx in (-1, 1):
        parts.append({"t": "c", "c": [round(x + sx * 1.9, 3), round(top_y + 0.85, 3), round(z, 3)], "s": [0.12, 1.1, 1.1], "m": "iron", "r": [0, 0, 90]})   # sheave
    parts.append(_box((x, base_y + 0.4, z), (half * 2 + 1.2, 0.8, half * 2 + 1.2), "stone"))   # footing
    return parts


def _catenary(a, b, sag, n=24):
    pts = []
    for k in range(n + 1):
        t = k / n
        p = (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t - sag * 4.0 * t * (1 - t), a[2] + (b[2] - a[2]) * t)
        pts.append(p)
    return [_beam(p, q, 0.07, "cable", k=0) for p, q in zip(pts, pts[1:])]


def write_station_asset(ctx, G, cat):
    """bt_cable_station: two trestles of different heights ~70 m apart on the far ridge (each from its own ground), a
    machinery shed, a sagging haul cable between them and on down toward the valley. Origin = trestle A's foot."""
    import json
    from mapgen.catalog import Asset
    ax, az = 236.0, 242.0
    bx, bz = 300.0, 212.0
    y0 = G(ax, az)
    ya, yb = 0.0, G(bx, bz) - y0
    ta, tb = 24.0, yb + 17.0                     # trestle tops (asset space): different heights
    parts = _trestle(0.0, 0.0, ya - 1.0, ta) + _trestle(bx - ax, bz - az, yb - 1.0, tb)
    # the machinery shed by trestle A: board walls, a pitched iron roof, a drive housing
    sx, sz = -9.0, 6.0
    parts += [_box((sx, 2.0, sz), (8.0, 4.0, 5.0), "shed_board"),
              _box((sx, 4.6, sz - 1.35), (8.6, 0.12, 3.0), "roof_iron", r=(-30, 0, 0)),
              _box((sx, 4.6, sz + 1.35), (8.6, 0.12, 3.0), "roof_iron", r=(30, 0, 0)),
              _box((sx + 3.0, 6.2, sz), (0.6, 2.4, 0.6), "iron"),           # the vent stack
              {"t": "c", "c": [sx - 4.3, 2.4, sz], "s": [0.5, 2.4, 2.4], "m": "iron", "r": [0, 0, 90]}]   # the drive wheel
    top_a = (1.9, ta + 0.85, 0.0)
    top_b = (bx - ax + 1.9, tb + 0.85, bz - az)
    parts += _catenary(top_a, top_b, 6.0)
    # the cable on down toward the valley (south-west), ending in the mist
    far = (-90.0, ta - 70.0, -95.0)
    parts += _catenary((top_a[0] - 3.8, top_a[1], top_a[2]), far, 9.0, n=30)
    j = {"id": "bt_cable_station", "version": 1, "name": "Cable-haul station (Blackwater Thaw)", "category": "Landmarks",
         "description": "BT1: the old cable-haul station on the far ridge: two lattice trestles of different heights, a machinery shed, a sagging haul cable. A skyline silhouette, generated by tools/mapgen/maps/blackwater_thaw.py.",
         "floors": 0, "enterable": False, "footprint": [round(bx - ax + 8, 1), round(abs(bz - az) + 8, 1)], "plinthDepth": 0, "maxGrade": 90,
         "noAutoPanels": True, "nav": {"effect": "none"}, "materials": M_STATION, "parts": parts, "furniture": [], "interactables": [], "panels": []}
    path = os.path.join(ctx.repo, "content", "assets", "bt_cable_station.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(j, f, separators=(",", ":"))
    cat.assets["bt_cable_station"] = Asset(j, path)
    return (ax, az, y0)


M_FLUME = [{"name": "stone", "albedo": [0.38, 0.37, 0.34], "roughness": 0.9, "pattern": "stone", "metallic": 0, "grain": 1},
           {"name": "wet_stone", "albedo": [0.22, 0.22, 0.21], "roughness": 0.55, "pattern": "stone", "metallic": 0, "grain": 1},
           {"name": "gate_wood", "albedo": [0.22, 0.19, 0.16], "roughness": 0.8, "pattern": "wood", "metallic": 0, "grain": 1},
           {"name": "iron_rust", "albedo": [0.26, 0.14, 0.08], "roughness": 0.75, "pattern": "metal", "metallic": 0.2, "grain": 1}]


def write_flume_asset(ctx, cat, steps=3, step_len=3.0, drop=0.4, inner=0.9, wall=0.45):
    """bt_flume: the stepped header channel (pump room family): a stone U-channel along local -z, stepping down `drop` per
    `step_len`, thick abutments at each step, wet bands, a timber gate with iron bolts at the head, water in each step
    (water.cpp surfaces), a drain grate at the foot. Origin = the head of the channel at the court's floor."""
    import json
    from mapgen.catalog import Asset
    parts, surfs = [], []
    hw = inner / 2 + wall / 2
    top0 = 1.2                                     # the head stands 1.2 m above the court
    for k in range(steps):
        z0, z1 = -k * step_len, -(k + 1) * step_len
        zc = (z0 + z1) / 2
        floor = top0 - 0.6 - k * drop
        parts.append(_box((0, floor / 2, zc), (inner + 2 * wall, max(0.2, floor), step_len), "stone"))           # the bed block
        for sx in (-1, 1):
            parts.append(_box((sx * hw, (floor + 0.55) / 2 + 0.0, zc), (wall, floor + 0.55, step_len), "stone"))   # side walls
            parts.append(_box((sx * (inner / 2 - 0.005), floor + 0.15, zc), (0.02, 0.3, step_len), "wet_stone", k=0))   # wet band
        parts.append(_box((0, floor + 0.35, z1 + 0.25), (inner + 2 * wall + 0.5, floor + 0.7 + 0.1, 0.5), "stone"))   # the abutment / lip
        surfs.append({"kind": "stream", "outline": [[-inner / 2, z0], [inner / 2, z0], [inner / 2, z1 + 0.5], [-inner / 2, z1 + 0.5]],
                      "level": round(floor + 0.22, 3), "path": [[0.0, z0], [0.0, z1]], "speed": 0.9, "clarity": 0.9, "waves": 1.2})
    # the timber gate and its bolts at the head
    parts.append(_box((0, top0 - 0.1, 0.15), (inner + 0.1, 0.9, 0.1), "gate_wood"))
    for sx in (-1, 1):
        parts.append(_box((sx * (inner / 2 + 0.1), top0 + 0.2, 0.15), (0.14, 1.5, 0.16), "gate_wood"))
        for yy in (top0 - 0.3, top0 + 0.1):
            parts.append({"t": "c", "c": [sx * (inner / 2 - 0.12), yy, 0.21], "s": [0.06, 0.04, 0.06], "m": "iron_rust", "r": [90, 0, 0], "k": 0})
    # the drain grate at the foot
    zf = -steps * step_len - 0.4
    parts.append(_box((0, 0.03, zf), (inner + 0.6, 0.06, 0.8), "iron_rust"))
    j = {"id": "bt_flume", "version": 1, "name": "Stepped header channel (Blackwater Thaw)", "category": "Small buildings",
         "description": "BT1: the waterworks court's stepped header channel: a stone U-channel stepping down 0.4 m per 3 m, thick abutments, wet bands, a timber gate with iron bolts, water in each step, a drain grate. Generated by tools/mapgen/maps/blackwater_thaw.py.",
         "floors": 0, "enterable": False, "footprint": [inner + 2 * wall + 0.6, steps * step_len + 1.0], "plinthDepth": 0, "maxGrade": 6,
         "noAutoPanels": True, "nav": {"effect": "carve"}, "materials": M_FLUME, "parts": parts, "water": {"surfaces": surfs},
         "furniture": [], "interactables": [], "panels": []}
    path = os.path.join(ctx.repo, "content", "assets", "bt_flume.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(j, f, separators=(",", ":"))
    cat.assets["bt_flume"] = Asset(j, path)

# ---------------------------------------------------------------- the sound session's anchors (content/templates/<id>.audio.json)
def write_audio_anchors(tdir, G=None):
    """positions the ambience reads: the creek centreline, the crossings, the flume and its drain, the smithy hearth and
    vent, the creaky sheds, the overlook. World x, z (spec X, Y); y = the ground when known."""
    import json
    P = lambda x, z: [round(x, 2), round(G(x, z), 2) if G else None, round(z, 2)]
    sheds = [(78.0, -16.0), (50.0, 14.0), (18.5, 66.0), (-98.0, -44.0), (-64.0, -78.0)]
    j = {"map": TID, "note": "generated by tools/mapgen/maps/blackwater_thaw.py; world x, z = spec X, Y; creek bed datum y = %.1f" % BASE,
         "creek": [P(x, z) for x, z, _ in CREEK_CTRL],
         "crossings": {"stone_bridge": P(*BRIDGE_W), "footbridge": P(*FOOTBRIDGE), "ford": P(*FORD)},
         "flume": {"head": P(8.0, 15.0), "foot": P(8.0, 6.0)}, "drains": [P(8.0, 5.6), P(8.0, 56.0)],
         "hearth": P(59.6, -10.5), "vent": P(52.8, -8.4), "sheds": [P(x, z) for x, z in sheds], "overlook": P(82.0, 74.0),
         "court": P(*COURT), "orchard": P(-76.0, -20.0)}
    with open(os.path.join(tdir, TID + ".audio.json"), "w", encoding="utf-8") as f:
        json.dump(j, f, indent=1)


# ---------------------------------------------------------------- M4: pruned orchard trees (spec 5: 2.2-5.5 m, crooked trunk,
# uneven branching with thickened pruning stubs, 3-5 silhouettes). Part-built, bare: no leaf cards in any season.
M_ORCH = [{"name": "bark", "albedo": [0.20, 0.17, 0.14], "roughness": 0.95, "pattern": "wood", "metallic": 0, "grain": 1},
          {"name": "bark_pale", "albedo": [0.36, 0.33, 0.29], "roughness": 0.9, "pattern": "wood", "metallic": 0, "grain": 1},
          {"name": "cut", "albedo": [0.55, 0.45, 0.32], "roughness": 0.85, "pattern": "wood", "metallic": 0, "grain": 1}]


def write_orchard_trees(ctx, cat, n=4):
    import json, random
    from mapgen.catalog import Asset
    ids = []
    for v in range(n):
        rr = random.Random(9100 + v)
        parts = []
        lean = (rr.uniform(-0.35, 0.35), rr.uniform(-0.35, 0.35))
        trunk_h = rr.uniform(1.4, 2.1)
        base = (0.0, 0.0, 0.0)
        top = (lean[0], trunk_h, lean[1])
        tw = rr.uniform(0.2, 0.28)
        mid = (lean[0] * 0.4 + rr.uniform(-0.08, 0.08), trunk_h * 0.5, lean[1] * 0.4 + rr.uniform(-0.08, 0.08))   # a crook
        parts.append(_beam(base, mid, tw, "bark"))
        parts.append(_beam(mid, top, tw * 0.9, "bark"))
        parts.append(_box((0, 0.6, 0), (tw * 1.3, 1.2, tw * 1.3), "bark", k=1) | {"v": 0})   # the trunk collider only
        limbs = rr.randint(3, 5)
        for k in range(limbs):
            a = 2 * math.pi * (k + rr.uniform(-0.25, 0.25)) / limbs
            L = rr.uniform(1.3, 2.4)
            rise = rr.uniform(0.9, 2.1)
            end = (top[0] + math.cos(a) * L, top[1] + rise, top[2] + math.sin(a) * L)
            w = tw * rr.uniform(0.45, 0.6)
            parts.append(_beam(top, end, w, "bark", k=0))
            parts.append(_box(end, (w * 1.5, w * 1.2, w * 1.5), "bark", k=0))                 # the thickened pruning stub
            parts.append(_box((end[0], end[1] + w * 0.65, end[2]), (w * 1.2, 0.02, w * 1.2), "cut", k=0))   # its pale cut face
            for s in range(rr.randint(2, 4)):   # water shoots from the stub and along the limb: thin, mostly upright
                t = rr.uniform(0.4, 1.0)
                p0 = (top[0] + (end[0] - top[0]) * t, top[1] + (end[1] - top[1]) * t, top[2] + (end[2] - top[2]) * t)
                p1 = (p0[0] + rr.uniform(-0.5, 0.5), p0[1] + rr.uniform(0.6, 1.4), p0[2] + rr.uniform(-0.5, 0.5))
                parts.append(_beam(p0, p1, rr.uniform(0.025, 0.045), "bark_pale", k=0))
        h = max(p["c"][1] + p["s"][1] / 2 for p in parts)
        aid = "bt_orchard_tree_%s" % "abcd"[v]
        j = {"id": aid, "version": 1, "name": "Pruned orchard tree %s (winter)" % "ABCD"[v], "category": "Vegetation",
             "description": "BT1: a pruned orchard tree in winter, %.1f m: a crooked trunk, uneven scaffold limbs ending in thickened pruning stubs with pale cuts, water shoots. Bare (no leaves). Generated by tools/mapgen/maps/blackwater_thaw.py." % h,
             "floors": 0, "enterable": False, "footprint": [1.0, 1.0], "plinthDepth": 0, "maxGrade": 30, "noAutoPanels": True,
             "nav": {"effect": "carve"}, "materials": M_ORCH, "parts": parts, "furniture": [], "interactables": [], "panels": []}
        path = os.path.join(ctx.repo, "content", "assets", aid + ".json")
        with open(path, "w", encoding="utf-8") as f:
            json.dump(j, f, separators=(",", ":"))
        cat.assets[aid] = Asset(j, path)
        ids.append(aid)
    return ids


# ---------------------------------------------------------------- M4: the envelope's rock face broken up (rock faces set into it,
# boulders and outcrops at its foot; the firs on its top already break the skyline)
def dress_boundary(b, cat, G, R):
    # BT2: no rock-face cladding (on the old cliff it read as a wall); outcrops and boulder groups at irregular spacing
    faces = [a for a in ("nat_outcrop_limestone", "nat_boulders_granite_group", "nat_boulder_granite_l") if a in cat]
    foot = [a for a in ("nat_boulder_granite_s", "nat_boulder_limestone_m", "nat_boulder_schist_m", "nat_boulder_schist_slab") if a in cat]
    if not faces:
        return 0
    x0, z0, x1, z1 = ENV
    edges = [((x0, z0), (x1, z0)), ((x1, z0), (x1, z1)), ((x1, z1), (x0, z1)), ((x0, z1), (x0, z0))]
    n = 0
    for (ax, az), (bx, bz) in edges:
        L = math.hypot(bx - ax, bz - az)
        tx, tz = (bx - ax) / L, (bz - az) / L
        nx, nz = tz, -tx                                   # outward (the envelope runs counter-clockwise)
        t = R.uniform(0, 3)
        while t < L:
            x, z = ax + tx * t, az + tz * t
            if min(math.hypot(x - cx, z - cz) for cx, cz, _ in CREEK_CTRL) < 10.0 or math.hypot(x - CASTLE_ROAD_EXIT[0], z - CASTLE_ROAD_EXIT[1]) < 6.0:
                t += 6.0
                continue
            if R.random() < 0.45:                           # gaps: the stands carry the line there
                t += R.uniform(6.0, 14.0)
                continue
            fx, fz = x + nx * R.uniform(-1.0, 2.5), z + nz * R.uniform(-1.0, 2.5)
            yaw = R.uniform(0, 360)
            e = b.place(R.choice(faces), fx, fz, yaw, check=False, cls="none", y=G(fx, fz) - R.uniform(0.3, 0.8), tags={"rock"}, companions=False)
            n += e is not None
            if foot and R.random() < 0.45:
                bx_, bz_ = x - nx * R.uniform(1.0, 3.5) + tx * R.uniform(-2, 2), z - nz * R.uniform(1.0, 3.5) + tz * R.uniform(-2, 2)
                b.place(R.choice(foot), bx_, bz_, R.uniform(0, 360), check=False, cls="none", y=G(bx_, bz_), tags={"rock"}, companions=False)
            t += R.uniform(6.0, 14.0)
    return n


# ---------------------------------------------------------------- BT2: the play limit (hidden collision, generated per bake)
def write_boundary_asset(ctx, cat, G, step=4.0, height=7.0, gaps=()):
    """bt_boundary: thin hidden collider boxes along the envelope, each seated on the ground under it (asset at the origin,
    y 0). The visible boundary is terrain, firs and rock; this only stops a player walking out of the play area."""
    import json
    from mapgen.catalog import Asset
    x0, z0, x1, z1 = ENV
    parts = []
    for (ax, az), (bx, bz) in (((x0, z0), (x1, z0)), ((x1, z0), (x1, z1)), ((x1, z1), (x0, z1)), ((x0, z1), (x0, z0))):
        L = math.hypot(bx - ax, bz - az)
        n = max(1, int(math.ceil(L / step)))
        for k in range(n):
            t0, t1 = k / n, (k + 1) / n
            cx, cz = ax + (bx - ax) * (t0 + t1) / 2, az + (bz - az) * (t0 + t1) / 2
            if any(math.hypot(cx - gx, cz - gz) < 3.5 for gx, gz in gaps):
                continue   # the castle road's gap (closed in matches by bt_boundary_gate)
            gy = min(G(ax + (bx - ax) * t0, az + (bz - az) * t0), G(ax + (bx - ax) * t1, az + (bz - az) * t1), G(cx, cz))
            sx, sz = (abs(bx - ax) / n + 0.6, 0.6) if abs(bx - ax) > abs(bz - az) else (0.6, abs(bz - az) / n + 0.6)
            parts.append({"t": "b", "c": [round(cx, 2), round(gy + height / 2 - 1.0, 2), round(cz, 2)], "s": [round(sx, 2), height, round(sz, 2)], "m": "x", "v": 0})
    j = {"id": "bt_boundary", "version": 1, "name": "Play-area limit (Blackwater Thaw)", "category": "Effects",
         "description": "BT2: the Blackwater Thaw play-area limit as hidden collider boxes along the envelope, generated by tools/mapgen/maps/blackwater_thaw.py; the trees, rock and slopes on it are what the player sees.",
         "floors": 0, "enterable": False, "footprint": [round(x1 - x0, 1), round(z1 - z0, 1)], "plinthDepth": 0, "maxGrade": 90, "noAutoPanels": True,
         "nav": {"effect": "block"}, "materials": [{"name": "x", "albedo": [0.1, 0.1, 0.1], "roughness": 0.9, "pattern": "plain", "metallic": 0, "grain": 1}],
         "parts": parts, "furniture": [], "interactables": [], "panels": []}
    path = os.path.join(ctx.repo, "content", "assets", "bt_boundary.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(j, f, separators=(",", ":"))
    cat.assets["bt_boundary"] = Asset(j, path)


# ---------------------------------------------------------------- BT2 perf: which backdrop fir stands any player can see
_VIEWPTS = None


def stand_seen(cx, cz, rad, eye=1.65):
    """(seen, nearest viewer distance): line of sight from any route point (every 8 m, eye height) or start point to the
    stand's crown middle or top (ground + 7 m / + 14 m) at its centre or either side, over the true ground."""
    global _VIEWPTS
    F = FIELD[0]
    samp = lambda xs, zs: ndimage.map_coordinates(F.H, [(zs - F.z0) / RES, (xs - F.x0) / RES], order=1, mode="nearest")
    if _VIEWPTS is None:
        pts = [(p[0], p[1]) for pts_, w, k in ROUTES.values() for p in lane_dense(pts_, 8.0)]
        pts += [SPAWN_LOW, SPAWN_UP] + [(st[0][0], st[0][1]) for st in STAIRS]
        P = np.array(pts, dtype=float)
        _VIEWPTS = np.column_stack([P, samp(P[:, 0], P[:, 1]) + eye])
    V = _VIEWPTS
    d = np.hypot(V[:, 0] - cx, V[:, 1] - cz)
    tg = []
    for ox, oz in ((0.0, 0.0), (rad * 0.7, 0.0), (-rad * 0.7, 0.0), (0.0, rad * 0.7), (0.0, -rad * 0.7)):
        g0 = float(samp(np.array([cx + ox]), np.array([cz + oz]))[0])
        tg += [(cx + ox, cz + oz, g0 + 7.0), (cx + ox, cz + oz, g0 + 14.0)]
    t = np.linspace(0.04, 0.96, 72)
    for tx, tz, ty in tg:
        X = V[:, 0:1] + (tx - V[:, 0:1]) * t
        Z = V[:, 1:2] + (tz - V[:, 1:2]) * t
        Y = V[:, 2:3] + (ty - V[:, 2:3]) * t
        H = samp(X.ravel(), Z.ravel()).reshape(X.shape)
        clear = np.all(H < Y - 0.3, axis=1)
        if clear.any():
            return True, float(d[clear].min())
    return False, float(d.min())


# ---------------------------------------------------------------- BT2 phase 2: the hilltop castle (scenic landmark, graybox)
# A Neuschwanstein-type ensemble after the four reference photos (the real building's hierarchy, original geometry):
# gatehouse with twin round towers on a curved buttressed terrace at the low (south) end, a stepped lower court (bower,
# square-to-round tower), a connecting wing, the long pale palace with a steep slate roof and dormer gables, a tall narrow
# spire behind its roof and a slender crowned tower on a lower lateral wing toward the quarry. It stands on a tapering
# spur that rises out of the north-west corner beyond the play area. Local frame: +z (s) = gate -> apex, +x (t) = east.
CASTLE_C = (-235.0, 215.0)                      # compound centre (world x, z)
CASTLE_YAW = -11.53                             # local +z = (-0.2, 0.98): the spur climbs north-north-west
# the castle's fixtures (castle-local x, y, z, asset, light gain): lanterns down the hall, the stair block, bower and anteroom;
# candle sconces up the tower
CASTLE_LAMPS = ([(x, 6.5, float(z), "lamp_hanging_lantern", 3.0) for z in range(1, 52, 7) for x in (-4.0, 4.0)]
                + [(12.0, -4.0, -14.0, "lamp_hanging_lantern", 1.8), (12.0, -1.5, -12.0, "lamp_hanging_lantern", 1.8),
                   (-11.0, -6.0, -30.0, "lamp_hanging_lantern", 1.8), (-11.0, 4.0, -30.0, "lamp_hanging_lantern", 1.6),
                   (20.0, -5.0, -12.0, "lamp_hanging_lantern", 1.6), (20.0, -5.0, -2.0, "lamp_hanging_lantern", 1.6)]
                + [(26.0 + 3.0 * math.cos(math.radians(a_)), -10.0 + 1.6 + 5.0 * k_, 3.0 * math.sin(math.radians(a_)), "furn_sconce_candle", 2.0)
                   for k_, a_ in enumerate(range(30, 330, 40))])
def lamp_light(gain):   # the hall's lanterns (gain 3) carry a 56 m room: 14 m reach (lighting session); the rest the room rule
    return {"gain": gain, "radius": 14.0} if gain >= 3.0 else {"gain": gain}


SPUR_STANDS = 46                                # fir stands on the castle spur (dark masses on its flanks; far-tree impostors)
CASTLE_PAD = 100.0                              # palace pad above the creek-bed datum (the village floor is ~ +4..10)


def castle_frame(X, Z):
    """(s, t) of world points in the castle's local frame (s along the spur, t across it, east positive)."""
    a = math.radians(CASTLE_YAW)
    ux, uz, ex, ez = math.sin(a), math.cos(a), math.cos(a), -math.sin(a)
    dx, dz = X - CASTLE_C[0], Z - CASTLE_C[1]
    return dx * ux + dz * uz, dx * ex + dz * ez


def castle_world(t, s):
    return local_to_world(CASTLE_C, CASTLE_YAW, t, s)


def castle_spur_zone(X, Z):
    S, T = castle_frame(X, Z)
    return (np.abs(T) < 60.0) & (S > -165.0) & (S < 95.0)


def castle_terrain(F, X, Z):
    """the spur: stepped pads (gate terrace, lower court, palace), a crest falling south toward the valley, steep
    flanks with an exposed rock cliff under the palace's east side, the terrain edge behind it held low (sky behind)."""
    S, T = castle_frame(X, Z)
    P = CASTLE_PAD
    F.set_mask((S >= -84) & (S < -50) & (np.abs(T) <= 23), P - 12.0)                       # the gate terrace
    F.set_mask((S >= -50) & (S < -8) & (np.abs(T) <= 21), P - 10.0)                        # the lower court (2 m over the gate)
    F.set_mask((S >= -22) & (S < 8) & (T > 0) & (T <= 31), P - 10.0)                       # the lateral wing's ledge
    F.set_mask((S >= -8) & (S < 66) & (np.abs(T) <= 19), P)                                # the palace
    for s0, h in ((-100.0, 70.0), (-125.0, 45.0), (-150.0, 24.0)):                         # the crest, down to the valley
        F.set_mask((np.abs(S - s0) < 1.0) & (np.abs(T) < 8.0), h)
    F.set_mask((T > 40) & (T < 44) & (S > -150) & (S < 80), 46.0)                          # east flank (toward the quarry)
    F.set_mask((T > 29) & (T < 31) & (S > -6) & (S < 44), P - 38.0)                        # the exposed cliff under the palace
    F.set_mask((T < -40) & (T > -44) & (S > -150) & (S < 80), 52.0)                        # west flank
    F.set_mask((S > 74) & (S < 78) & (np.abs(T) < 40), P - 34.0)                           # the saddle behind the apex


def castle_edge(F, X, Z, edge):
    """the terrain edge near the castle held at +62 (the spire and the side tower stand against sky, not a ridge)."""
    m = edge & (np.hypot(X - CASTLE_C[0], Z - CASTLE_C[1]) < 140.0)
    F.set_mask(m, 62.0)


# milestone 2: the kit's patterns. Pale warm-grey limestone ashlar body, cleaner pale trim on corners and belts, rough
# masonry base and terrace, the gatehouse's oxide-red render, blue-charcoal slate (plain: procedural slate courses alias
# into moire at 350 m), dark window recesses
M_CASTLE = [{"name": "limestone", "albedo": [0.62, 0.60, 0.56], "roughness": 0.85, "pattern": "ashlar", "metallic": 0, "grain": 1},
            {"name": "paving", "albedo": [0.42, 0.41, 0.39], "roughness": 0.9, "pattern": "stone", "metallic": 0, "grain": 1},
            {"name": "door", "albedo": [0.20, 0.13, 0.08], "roughness": 0.7, "pattern": "wood", "metallic": 0, "grain": 1},
            {"name": "snow", "albedo": [0.85, 0.85, 0.86], "roughness": 0.6, "pattern": "plain", "metallic": 0, "grain": 1},
            {"name": "glass", "albedo": [0.8, 0.85, 0.82], "roughness": 0.05, "pattern": "glass", "metallic": 0, "grain": 1},
            {"name": "masonry", "albedo": [0.44, 0.42, 0.38], "roughness": 0.92, "pattern": "stone", "metallic": 0, "grain": 1},
            {"name": "red", "albedo": [0.40, 0.19, 0.14], "roughness": 0.88, "pattern": "plaster", "metallic": 0, "grain": 1},
            {"name": "slate", "albedo": [0.085, 0.095, 0.12], "roughness": 0.6, "pattern": "plain", "metallic": 0, "grain": 1},
            {"name": "trim", "albedo": [0.72, 0.69, 0.63], "roughness": 0.8, "pattern": "ashlar", "metallic": 0, "grain": 1},
            {"name": "dark", "albedo": [0.03, 0.03, 0.035], "roughness": 0.7, "pattern": "plain", "metallic": 0, "grain": 1}]


def write_castle_asset(ctx, cat):
    """bt_castle: the graybox ensemble as one part-built asset (one batched mesh per LOD), no colliders, origin = the
    palace pad. lodKeep 2 m: LOD2 (from ~300 m) keeps everything down to the window bays (the castle is ~3.4k tris)."""
    import json
    from mapgen.catalog import Asset
    P = []

    def box(c, s, m, lod=0, r=None, k=1):   # shell: solid (players reach it); trims and openings k=0
        p = {"t": "b", "c": [round(v, 2) for v in c], "s": [round(v, 2) for v in s], "m": m}
        if not k:
            p["k"] = 0
        if lod:
            p["lod"] = 1
        if r is not None:
            p["r"] = [0, round(r, 1), 0]
        P.append(p)

    def cyl(c, d, h, m, n=16, cone=False, lod=0):
        p = {"t": "k" if cone else "c", "c": [round(c[0], 2), round(c[1], 2), round(c[2], 2)], "s": [round(d, 2), round(h, 2), round(d, 2)], "m": m, "n": n}
        if lod:
            p["lod"] = 1
        P.append(p)

    def snow_strips(hw, h, along, ax):   # snow lying along the eaves (with thickness) and the ridge of a pitched roof
        a = math.atan2(h, hw); L = math.hypot(hw, h)
        return a, L

    def gable_z(x0, x1, y0, h, z0, z1, m="slate", lod=0):   # a pitched roof running along z
        p = {"t": "p", "axis": 2, "a": [round(z0, 2), round(z1, 2)], "tri": [[round(x0, 2), round(y0, 2)], [round(x1, 2), round(y0, 2)], [round((x0 + x1) / 2, 2), round(y0 + h, 2)]], "m": m}
        if lod:
            p["lod"] = 1
        P.append(p)
        hw = (x1 - x0) / 2; a, L = snow_strips(hw, h, z1 - z0, "z"); xm = (x0 + x1) / 2; zc = (z0 + z1) / 2
        for sg in (-1, 1):   # the eave strip: 35 % of the slope up from the eave, 12 cm thick
            f = 0.18; ex, ey = xm + sg * hw * (1 - f), y0 + h * f
            P.append({"t": "b", "c": [round(ex + sg * math.sin(a) * 0.08, 2), round(ey + math.cos(a) * 0.08, 2), round(zc, 2)],
                      "s": [round(L * 0.36, 2), 0.12, round(z1 - z0 - 0.3, 2)], "r": [0, 0, round(-sg * math.degrees(a), 1)], "m": "snow", "k": 0})
        P.append({"t": "b", "c": [round(xm, 2), round(y0 + h + 0.02, 2), round(zc, 2)], "s": [0.5, 0.14, round(z1 - z0 - 0.3, 2)], "m": "snow", "k": 0})

    def gable_x(z0, z1, y0, h, x0, x1, m="slate", lod=0):   # a pitched roof running along x
        p = {"t": "p", "axis": 0, "a": [round(x0, 2), round(x1, 2)], "tri": [[round(z0, 2), round(y0, 2)], [round(z1, 2), round(y0, 2)], [round((z0 + z1) / 2, 2), round(y0 + h, 2)]], "m": m}
        if lod:
            p["lod"] = 1
        P.append(p)
        hw = (z1 - z0) / 2; a, L = snow_strips(hw, h, x1 - x0, "x"); zm = (z0 + z1) / 2; xc = (x0 + x1) / 2
        for sg in (-1, 1):
            f = 0.18; ez, ey = zm + sg * hw * (1 - f), y0 + h * f
            P.append({"t": "b", "c": [round(xc, 2), round(ey + math.cos(a) * 0.08, 2), round(ez + sg * math.sin(a) * 0.08, 2)],
                      "s": [round(abs(x1 - x0) - 0.3, 2), 0.12, round(L * 0.36, 2)], "r": [round(sg * math.degrees(a), 1), 0, 0], "m": "snow", "k": 0})
        P.append({"t": "b", "c": [round(xc, 2), round(y0 + h + 0.02, 2), round(zm, 2)], "s": [round(abs(x1 - x0) - 0.3, 2), 0.14, 0.5], "m": "snow", "k": 0})

    def tower(x, z, d, y0, h, crown=1.15, cap=1.0, m="limestone", n=16):   # shaft, crown band, pointed cap
        cyl((x, y0 + h / 2, z), d, h, m, n)
        cyl((x, y0 + h + 0.9, z), d * crown, 1.8, "limestone", n)
        cyl((x, y0 + h + 1.8 + cap * d * 0.6, z), d * crown, cap * d * 1.2, "slate", n, cone=True)

    # ---- rooms commit: hollow shell volumes (walls 0.8 m with door openings); the rooms' floors, stairs, linings and
    # furnishing are castle_interior (built only near: loadRadius)
    def hollow(cx, cy, cz, sx, sy, sz, m, doors=(), t=0.8):
        """four walls of a box volume, openings as (face, along, y0, w, h): face 'x-','x+','z-','z+', along = the
        opening's centre on that face's long axis, y0 its sill (absolute local y)."""
        y0, y1 = cy - sy / 2, cy + sy / 2
        for face in ("x-", "x+", "z-", "z+"):
            ops = sorted([d for d in doors if d[0] == face], key=lambda d: d[1])
            if face[0] == "x":
                fx = cx - sx / 2 + t / 2 if face == "x-" else cx + sx / 2 - t / 2
                a0, a1 = cz - sz / 2, cz + sz / 2
            else:
                fz = cz - sz / 2 + t / 2 if face == "z-" else cz + sz / 2 - t / 2
                a0, a1 = cx - sx / 2 + t, cx + sx / 2 - t
            cur = a0
            for _f, al, oy, w, h in ops + [(face, a1 + 99.0, y0, 0.0, 0.0)]:
                lo, hi = (al - w / 2, al + w / 2) if w > 0 else (a1, a1)
                if lo > cur:   # a full-height piece up to the opening
                    L, mid = lo - cur, (cur + lo) / 2
                    box((fx, cy, mid) if face[0] == "x" else (mid, cy, fz), (t, sy, L) if face[0] == "x" else (L, sy, t), m)
                if w > 0:      # over and under the opening
                    if oy + h < y1:
                        hh = y1 - (oy + h); yc = oy + h + hh / 2
                        box((fx, yc, al) if face[0] == "x" else (al, yc, fz), (t, hh, w) if face[0] == "x" else (w, hh, t), m)
                    if oy > y0:
                        hh = oy - y0; yc = y0 + hh / 2
                        box((fx, yc, al) if face[0] == "x" else (al, yc, fz), (t, hh, w) if face[0] == "x" else (w, hh, t), m)
                    cur = hi

    yT, yL = -12.0, -10.0   # shell: the court sits 2 m over the gate terrace (a ramp in the passage), the palace 10 m over it

    def window(face_x, y, z, w, h, side, arch=True, n_axis="x", glass=False):
        """a recessed window on a wall face at x = face_x (normal +-x): dark opening, pale stone jambs, sill and lintel or
        an arched head, a mullion; the frames stand proud so the opening reads deep."""
        fx = face_x + side * 0.08
        if glass:   # a real opening (the hall behind it): glazing that lets the sky light in, set back in the wall
            box((face_x - side * 0.3, y, z), (0.05, h, w), "glass", k=0)
        else:
            box((fx, y, z), (0.16, h, w), "dark", k=0)
        for dz in (-w / 2 - 0.11, w / 2 + 0.11):
            box((face_x + side * 0.16, y, z + dz), (0.32, h + 0.2, 0.22), "trim", k=0)
        box((face_x + side * 0.22, y - h / 2 - 0.1, z), (0.44, 0.18, w + 0.6), "trim", k=0)   # sill
        box((face_x + side * 0.16, y, z), (0.24, h, 0.1), "trim", k=0)                         # mullion
        if arch:
            P.append({"t": "c", "c": [round(face_x + side * 0.16, 2), round(y + h / 2, 2), round(z, 2)], "s": [w + 0.44, 0.32, w + 0.44], "m": "trim", "r": [0, 0, 90], "n": 12, "k": 0})
            P.append({"t": "c", "c": [round(face_x + side * 0.2, 2), round(y + h / 2, 2), round(z, 2)], "s": [w, 0.32, w], "m": "dark", "r": [0, 0, 90], "n": 12, "k": 0})
        else:
            box((face_x + side * 0.2, y + h / 2 + 0.15, z), (0.4, 0.3, w + 0.5), "trim", k=0)  # lintel

    # the terrace: a battered retaining drum (a wider plinth course), coping, a parapet along its front, paving on top
    cyl((0.0, yT - 10.0, -66.0), 44.0, 20.0, "masonry", 32)
    cyl((0.0, yT - 17.0, -66.0), 46.5, 6.0, "masonry", 32)                       # the batter: a wider foot course
    cyl((0.0, yT + 0.08, -66.0), 43.4, 0.16, "paving", 32)                        # paving
    cyl((0.0, yT - 0.2, -66.0), 44.8, 0.4, "trim", 32)                            # coping
    for k in range(9):
        ph = math.radians(-70.0 + 17.5 * k)
        box((22.4 * math.sin(ph), yT - 11.0, -66.0 - 17.4 * math.cos(ph)), (2.4, 18.0, 3.2), "masonry", r=math.degrees(ph))
    for k in range(40):   # the parapet: continuous along the front arc with a coping, one gap where the road arrives (west)
        a0_, a1_ = math.radians(-100.0 + 5.0 * k), math.radians(-95.0 + 5.0 * k)
        if -100.5 < -100.0 + 5.0 * k < -84.5:
            continue
        p0 = (21.6 * math.sin(a0_), -66.0 - 16.6 * math.cos(a0_)); p1 = (21.6 * math.sin(a1_), -66.0 - 16.6 * math.cos(a1_))
        L = math.hypot(p1[0] - p0[0], p1[1] - p0[1]) + 0.05; yaw_ = math.degrees(math.atan2(p1[0] - p0[0], p1[1] - p0[1])) - 90.0
        box(((p0[0] + p1[0]) / 2, yT + 0.55, (p0[1] + p1[1]) / 2), (L, 1.1, 0.5), "masonry", r=yaw_)
        box(((p0[0] + p1[0]) / 2, yT + 1.17, (p0[1] + p1[1]) / 2), (L, 0.14, 0.7), "trim", r=yaw_, k=0)
    # the gatehouse: red body split round a 4.4 x 6.4 m passage (open door leaves inside), stepped gable, towers
    box((-8.6, yT + 8.0, -50.0), (12.8, 16.0, 11.0), "red")
    box((8.6, yT + 8.0, -50.0), (12.8, 16.0, 11.0), "red")
    box((0.0, yT + 11.2, -50.0), (4.4, 9.6, 11.0), "red")                         # over the passage
    for k in range(5):                                                             # the ramp up to the court (2 m)
        box((0.0, yT + 0.2 * (k + 1), -54.4 + 2.2 * k), (4.4, 0.4 * (k + 1), 2.2), "paving")
    for k in range(9):   # the arch's surround: pale voussoirs in a semicircle over the opening, proud of the red face
        ph = math.radians(10.0 + 20.0 * k)
        P.append({"t": "b", "c": [round(2.75 * math.cos(ph), 2), round(yT + 6.4 + 2.75 * math.sin(ph) - 0.6, 2), -55.62], "s": [0.55, 1.0, 0.3],
                  "r": [0, 0, round(math.degrees(ph) - 90.0, 1)], "m": "trim", "k": 0})
    for dx, yaw in ((-2.1, 90.0), (2.1, -90.0)):                                   # door leaves, folded open against the passage walls
        box((dx + (0.1 if dx < 0 else -0.1), yT + 3.0, -53.2), (0.15, 6.0, 2.1), "door", k=0)
    gable_x(-55.5, -44.5, yT + 16.0, 6.0, -15.0, 15.0)
    for w, h, y in ((10.0, 5.0, 18.5), (6.5, 3.0, 22.5), (3.0, 2.2, 25.1)):
        box((0.0, yT + y, -55.3), (w, h, 1.4), "red")
    for x in (-16.5, 16.5):
        tower(x, -54.0, 7.0, yT, 24.0, m="red", cap=0.9)
    for x in (-10.0, -6.0, 6.0, 10.0):                                            # the gatehouse windows (front face z -55.5)
        for y in (yT + 8.5, yT + 12.5):
            box((x, y, -55.58), (1.4, 2.2, 0.16), "dark", k=0)
            box((x, y - 1.2, -55.75), (1.9, 0.18, 0.4), "trim", k=0)
            box((x, y + 1.25, -55.7), (1.9, 0.3, 0.3), "trim", k=0)
    # the lower court: a paved floor, side walls, the bower (left), the square-to-round tower (right, behind the gate)
    box((0.0, yL - 0.15, -26.0), (38.0, 0.3, 37.0), "paving")
    box((0.0, yL + 5.0, -6.8), (40.0, 10.0, 1.6), "masonry")   # the retaining wall up to the palace's level (no raw slope)
    for x in (-20.0, 20.0):
        box((x, yL + 4.0, -30.0), (2.0, 8.0, 36.0), "limestone")   # the facade's ashlar (masonry read as giant brick)
    hollow(-11.0, yL + 10.0, -30.0, 13.0, 20.0, 16.0, "limestone", doors=[("x+", -26.0, yL, 1.6, 2.6)])
    box((-11.0, yL + 19.8, -30.0), (13.0, 0.4, 16.0), "limestone")   # its ceiling under the roof
    gable_z(-17.8, -4.2, yL + 20.0, 7.0, -38.0, -22.0)
    tower(-4.2, -22.0, 3.2, yL + 14.0, 10.0, cap=1.4)
    for y in (yL + 6.0, yL + 12.0):
        window(-4.5, y, -30.0, 1.1, 2.0, 1, arch=False)
    box((11.0, yL + 12.0, -38.0), (9.0, 24.0, 9.0), "limestone")
    tower(11.0, -38.0, 8.0, yL + 24.0, 16.0, cap=1.1)
    # the connecting wing (right) between the court and the palace
    hollow(12.0, yL + 9.0, -14.0, 12.0, 18.0, 12.0, "limestone", doors=[("x-", -14.0, yL, 1.6, 2.6), ("z+", 7.6, 0.0, 1.6, 2.6)])
    box((12.0, yL + 17.8, -14.0), (12.0, 0.4, 12.0), "limestone")
    gable_x(-20.0, -8.0, yL + 18.0, 5.0, 6.0, 18.0)
    # the lower lateral wing toward the quarry and its slender, crowned, independent-looking tower
    hollow(20.0, yL + 7.0, -8.0, 10.0, 14.0, 20.0, "limestone", doors=[("x-", -12.0, yL, 1.6, 2.6)])
    box((20.0, yL + 13.8, -8.0), (10.0, 0.4, 20.0), "limestone")
    gable_z(15.0, 25.0, yL + 14.0, 4.5, -18.0, 2.0)
    # the side tower: a hollow ring (the spiral stair inside is the interior's), a door into the wing, an open lookout of
    # eight piers under its crown and cap
    TX, TZ, TR, TH = 26.0, 0.0, 3.6, 42.0
    for k in range(16):
        ph = math.radians(11.25 + 22.5 * k); seg = 2 * TR * math.sin(math.radians(11.25)) + 0.1
        cx_, cz_ = TX + (TR - 0.3) * math.cos(ph), TZ + (TR - 0.3) * math.sin(ph)
        if abs(math.degrees(ph) - 180.0) < 12.0:   # the door (west, into the wing)
            box((cx_, yL + 2.6 + (TH - 2.6) / 2, cz_), (0.6, TH - 2.6, seg), "limestone", r=-math.degrees(ph))
        else:
            box((cx_, yL + TH / 2, cz_), (0.6, TH, seg), "limestone", r=-math.degrees(ph))
    for k in range(8):
        ph = math.radians(22.5 * k)
        box((TX + (TR - 0.3) * math.cos(ph), yL + TH + 1.2, TZ + (TR - 0.3) * math.sin(ph)), (0.6, 2.4, 0.9), "trim", r=-math.degrees(ph))
    cyl((TX, yL + TH + 2.4 + 0.9, TZ), 2 * TR * 1.25, 1.8, "limestone", 16)
    cyl((TX, yL + TH + 4.2 + 0.7 * 7.2 * 0.6, TZ), 2 * TR * 1.25, 0.7 * 7.2 * 1.2, "slate", 16, cone=True)
    # the palace: the long pale slab (its foot from the court level up), steep dark roof, dormer gables, throne-hall
    # projection, corner turrets, chimneys
    box((0.0, 20.0, 25.0), (18.0, 20.0, 58.0), "limestone")                      # the floors above the hall
    HALL_WIN = [(f, z) for f in ("x-", "x+") for z in [-0.5 + 4.6 * k for k in range(12)] if not (f == "x-" and 27.0 < z < 41.0)]
    hollow(0.0, 5.0, 25.0, 18.0, 10.0, 58.0, "limestone", doors=[("z-", 7.6, 0.0, 1.6, 2.6)] + [(f, z, 1.5, 1.8, 5.2) for f, z in HALL_WIN])   # the great hall's walls, its ground-floor windows open
    box((0.0, -5.0, 25.0), (18.6, 10.0, 58.6), "masonry")                         # the base course down to the court
    gable_z(-9.6, 9.6, 30.0, 12.0, -4.0, 54.0)
    for z in (6.0, 18.0, 30.0, 42.0):
        for side in (-1, 1):
            box((side * 8.3, 32.2, z), (3.0, 4.4, 4.2), "limestone")
            gable_x(z - 2.4, z + 2.4, 34.4, 3.0, side * 6.8, side * 9.9)
    box((-12.5, 15.0, 34.0), (7.0, 30.0, 14.0), "limestone")
    gable_x(27.0, 41.0, 30.0, 6.0, -16.0, -9.0)
    for (x, z, y0, h) in ((9.0, -4.0, 22.0, 16.0), (-9.0, -4.0, 24.0, 12.0), (9.0, 54.0, 20.0, 20.0), (-9.0, 54.0, 23.0, 14.0)):
        tower(x, z, 3.2, y0, h, cap=1.5)
    for (x, z) in ((3.5, 10.0), (-3.5, 24.0), (3.5, 40.0)):
        box((x, 40.5, z), (1.3, 5.0, 1.3), "masonry")
    # the tall narrow spire behind the main roof: square shaft, octagonal upper stage, pointed cap (top +70)
    box((5.0, 25.0, 50.0), (6.0, 50.0, 6.0), "limestone")
    cyl((5.0, 54.0, 50.0), 8.0, 8.0, "limestone", 8)
    cyl((5.0, 64.0, 50.0), 8.4, 12.0, "slate", 8, cone=True)
    # the palace's windows in vertical bays (4 floors x 12), arched heads on the upper two floors (as on the real
    # building's main floors), string courses under each floor, clean pale corner courses
    for side in (-1, 1):
        for fl, y in enumerate((5.0, 11.5, 18.0, 24.5)):
            for k in range(12):
                z = -0.5 + k * 4.6
                if side < 0 and 27.0 < z < 41.0:
                    continue   # the throne-hall projection covers this stretch on the west
                if fl == 0:   # the great hall's tall arched windows (1.8 x 5.2 m + the arch), glazed openings
                    window(side * 9.0, 4.1, z, 1.8, 5.2, side, arch=True, glass=True)
                else:
                    window(side * 9.0, y, z, 1.35, 2.6 if fl < 3 else 2.0, side, arch=fl in (1, 2))
            if fl > 0:   # (no course across the hall's tall windows)
                box((side * 9.12, y - 2.05, 25.0), (0.25, 0.45, 58.4), "trim", k=0)
        for zc in (-4.0, 54.0):
            box((side * 8.75, 15.0, zc + (0.75 if zc < 0 else -0.75)), (0.6, 30.0, 1.6), "trim")
    box((0.0, yT + 16.3, -55.6), (31.0, 0.5, 0.5), "trim", k=0)   # the gatehouse's cornice
    for (x, z, d) in ((11.0, -38.0, 8.0), (26.0, 0.0, 7.2)):
        cyl((x, (yL + 24.0 if d == 8.0 else yL + 26.0), z), d * 1.06, 0.8, "trim", 16)   # belts
    j = {"id": "bt_castle", "version": 1, "name": "Hilltop castle (Blackwater Thaw)", "category": "Landmarks",
         "description": "BT2: the scenic hilltop castle on the north-west spur, after the real Neuschwanstein's architecture (original geometry, graybox stage), generated by tools/mapgen/maps/blackwater_thaw.py. Far scenery: no colliders, one batched mesh per LOD.",
         "floors": 0, "enterable": False, "footprint": [60.0, 150.0], "plinthDepth": 0, "maxGrade": 90, "noAutoPanels": True, "lodKeep": 2.0,
         "nav": {"effect": "none"}, "materials": M_CASTLE, "parts": P, "furniture": [], "interactables": [], "panels": []}
    # the palette asset (user: "castle should be placeable asset as well once furnished"): the same shell under its own id,
    # with the interior and its lamps as companions (placed with it from the palette, the interior loading only near,
    # the lamps loading with the interior), a 20 m plinth to seat it on uneven ground; Blackwater places the parts itself
    hj = dict(j, id="castle_hilltop", name="Hilltop castle", category="Landmarks", plinthDepth=20.0,
              description="A hilltop castle after the real Neuschwanstein's architecture (original geometry): gatehouse and terrace, court, palace, spire and side tower, with an explorable interior (built only within ~90 m) and its lamps placed with it. Generated by tools/mapgen/maps/blackwater_thaw.py.")
    comps = [{"asset": "castle_interior", "pos": [0, 0, 0], "yaw": 0}]
    for lx, ly, lz, aid, gain in CASTLE_LAMPS:
        comps.append({"asset": aid, "pos": [lx, ly, lz], "yaw": 0, "extra": {"light": lamp_light(gain)}, "loadWithCompanion": 0, "supportCompanion": 0})
    hj["companions"] = comps
    hj["companionsOwned"] = True   # deleting the castle removes its interior and lamps
    with open(os.path.join(ctx.repo, "content", "assets", "castle_hilltop.json"), "w", encoding="utf-8") as f:
        json.dump(hj, f, separators=(",", ":"))
    path = os.path.join(ctx.repo, "content", "assets", "bt_castle.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(j, f, separators=(",", ":"))
    cat.assets["bt_castle"] = Asset(j, path)
    return len(P)


def dress_castle_spur(b, cat, G, R):
    """dark fir masses on the spur's flanks (below the pads), rock faces on the exposed cliff under the palace."""
    firs = [a for a in ("ph_fir_tree_01_a", "ph_fir_tree_01_b", "ph_fir_tree_01_c") if a in cat]
    n = 0
    for _ in range(SPUR_STANDS):
        s0, t0 = R.uniform(-150.0, 70.0), R.choice([-1, 1]) * R.uniform(24.0, 52.0)
        if s0 > -90.0 and abs(t0) < 26.0:
            continue
        for _k in range(R.randint(10, 18)):
            s, t = s0 + R.uniform(-10, 10), t0 + R.uniform(-8, 8)
            S_, T_ = s, t
            if S_ > -88.0 and abs(T_) < 24.0:    # not on the pads
                continue
            if S_ > -8.0 and S_ < 44.0 and 26.0 < T_ < 33.0:   # the exposed cliff stays bare
                continue
            x, z = castle_world(t, s)
            if not (-298.0 < x and z < 288.0) or firs == []:
                continue
            if CASTLE_ROAD_WORLD and min(math.hypot(x - p_[0], z - p_[1]) for p_ in _ROAD_DENSE()) < 4.0:
                continue   # not on the castle road
            b.place(R.choice(firs), x, z, R.uniform(0, 360), check=False, cls="veg", y=G(x, z), tags={"veg", "firs", "castle"}, companions=False)
            n += 1
    # milestone 3: the approach road climbing the spur's east side and curving round the terrace to the gate (paint),
    # boulders where the terrace wall meets the cliff
    road = [(p_[0], p_[1]) for p_ in CASTLE_ROAD_WORLD] + [castle_world(-10.0, -64.0), castle_world(0.0, -60.0)]   # on to the gate
    PAINT[0].line("gravel", road, 4.0, weight=0.9, edge=1.2)
    foot = [a for a in ("nat_boulder_granite_l", "nat_boulder_limestone_m", "nat_boulder_schist_slab") if a in cat]
    for k in range(10):
        ph = math.radians(-80.0 + 17.0 * k)
        x, z = castle_world(29.0 * math.sin(ph), -66.0 - 24.0 * math.cos(ph))   # past the terrace's pad: at its foot, not on top
        if foot:
            b.place(foot[k % len(foot)], x, z, R.uniform(0, 360), check=False, cls="none", y=G(x, z) - 0.4, tags={"rock", "castle"}, companions=False)
    faces = [a for a in ("ph_rock_face_01", "ph_rock_face_02") if a in cat]
    for k, s in enumerate(range(-4, 44, 7)):
        if not faces:
            break
        x, z = castle_world(29.0, float(s))
        b.place(faces[k % len(faces)], x, z, CASTLE_YAW + 90.0 + R.uniform(-12, 12), check=False, cls="none", y=G(x, z) - 1.0, tags={"rock", "castle"}, companions=False)
    return n


# ---------------------------------------------------------------- Phase 1 leftover: snow caps with real thickness
ROOF_MATS = ("slate", "tile", "tile_orange", "roof", "roof_tile", "shingle", "thatch")


def write_snowcaps(ctx, cat, ids):
    """bt_snowcap_<asset>: a 9 cm snow slab over each sloped roof slab of a building (box parts in a roof material tilted
    about one axis), inset 0.2 m from the eaves and 0.35 m from the verges, so the roofline has real buildup and the
    tile courses (whose pattern rings into moire at distance) are covered. Placed with each building's own transform."""
    import json
    from mapgen.catalog import Asset
    made = {}
    for aid in ids:
        if aid not in cat:
            continue
        j0 = cat[aid].raw
        parts = []
        for p in j0.get("parts", []):
            if p.get("t", "b") != "b" or str(p.get("m", "")).lower() not in ROOF_MATS or "r" not in p:
                continue
            rx, ry, rz = (float(v) for v in p["r"])
            sx, sy, sz = (float(v) for v in p["s"])
            if ry != 0.0 or (rx != 0.0) == (rz != 0.0) or max(abs(rx), abs(rz)) < 12.0 or max(abs(rx), abs(rz)) > 62.0:
                continue   # one tilt axis, a roof pitch (12-62 deg): flat tops and towers keep the shader's snow
            if rx != 0.0:
                a = math.radians(rx); n = (0.0, math.cos(a), math.sin(a)); size = (sx - 0.7, 0.09, sz - 0.4)
            else:
                a = math.radians(rz); n = (-math.sin(a), math.cos(a), 0.0); size = (sx - 0.4, 0.09, sz - 0.7)
            if min(size[0], size[2]) < 0.6:
                continue
            off = sy / 2 + 0.045
            c = [round(float(p["c"][k]) + n[k] * off, 3) for k in range(3)]
            parts.append({"t": "b", "c": c, "s": [round(v, 3) for v in size], "r": [rx, 0, rz], "m": "snow", "k": 0})
        if not parts:
            continue
        cid = "bt_snowcap_" + aid
        j = {"id": cid, "version": 1, "name": "Snow cap (%s)" % aid, "category": "Effects",
             "description": "Blackwater Thaw: the roof snow cap of %s, generated by tools/mapgen/maps/blackwater_thaw.py from its roof slabs." % aid,
             "floors": 0, "enterable": False, "footprint": j0.get("footprint", [1, 1]), "plinthDepth": 0, "maxGrade": 90, "noAutoPanels": True,
             "nav": {"effect": "none"}, "materials": [{"name": "snow", "albedo": [0.85, 0.85, 0.86], "roughness": 0.6, "pattern": "plain", "metallic": 0, "grain": 1}],
             "parts": parts, "furniture": [], "interactables": [], "panels": []}
        path = os.path.join(ctx.repo, "content", "assets", cid + ".json")
        with open(path, "w", encoding="utf-8") as f:
            json.dump(j, f, separators=(",", ":"))
        cat.assets[cid] = Asset(j, path)
        made[aid] = cid
    return made


# ---------------------------------------------------------------- the castle: explore-only reach (players only)
CASTLE_ROAD_EXIT = (-128.0, 30.0)   # where the road leaves the play area (the west edge, above the orchard)
# the road in the castle's local frame (t, s, height over the datum): from the spur's west foot three legs climb the
# west flank (out of the quarry view) at 13-16 %, hairpins at their ends, onto the gate terrace's west edge
CASTLE_ROAD_LOCAL = [(-50.0, -150.0, 30.0), (-52.0, 40.0, 56.0), (-40.0, 46.0, 57.0), (-38.0, -118.0, 78.0),
                     (-28.0, -124.0, 79.0), (-22.0, -66.0, 88.0)]


def castle_road_points(h_exit):
    pts = [(CASTLE_ROAD_EXIT[0] + 2.0, CASTLE_ROAD_EXIT[1], h_exit)]
    for t, s_, h in CASTLE_ROAD_LOCAL:
        x, z = castle_world(t, s_)
        pts.append((x, z, h))
    return pts


def castle_road_terrain(F):
    """the road's floor (4 m) as a fixed corridor in the terrain solve, with a 2 m graded shoulder."""
    h_exit = float(ndimage.map_coordinates(F.H, [[(CASTLE_ROAD_EXIT[1] - F.z0) / RES], [(CASTLE_ROAD_EXIT[0] + 2.0 - F.x0) / RES]], order=1, mode="nearest")[0])
    pts = castle_road_points(h_exit)
    CASTLE_ROAD_WORLD[:] = pts
    _, _, d, h = F.corridor(pts, 8.0)
    F.set_mask(d <= 2.0, h)


CASTLE_ROAD_WORLD = []


def write_boundary_gate(ctx, cat, G, width=7.0, height=7.0):
    """bt_boundary_gate: the play limit across the castle road's gap, solid only while a match runs ("matchOnly"),
    nav block (bots never path out); hidden, no collision outside matches, so a player exploring walks through."""
    import json
    from mapgen.catalog import Asset
    j = {"id": "bt_boundary_gate", "version": 1, "name": "Play-limit gate (match only)", "category": "Effects",
         "description": "BT2: closes the play limit's gap at the castle road while a match runs (matchOnly), so the explore-only castle is never part of a fight; generated by blackwater_thaw.py.",
         "floors": 0, "enterable": False, "footprint": [width, 0.6], "plinthDepth": 0, "maxGrade": 90, "noAutoPanels": True, "matchOnly": True,
         "nav": {"effect": "block"}, "materials": [{"name": "x", "albedo": [0.1, 0.1, 0.1], "roughness": 0.9, "pattern": "plain", "metallic": 0, "grain": 1}],
         "parts": [{"t": "b", "c": [0.0, height / 2 - 1.0, 0.0], "s": [width, height, 0.6], "m": "x", "v": 0}], "furniture": [], "interactables": [], "panels": []}
    path = os.path.join(ctx.repo, "content", "assets", "bt_boundary_gate.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(j, f, separators=(",", ":"))
    cat.assets["bt_boundary_gate"] = Asset(j, path)


_ROAD_CACHE = []


def _ROAD_DENSE():
    if not _ROAD_CACHE and CASTLE_ROAD_WORLD:
        _ROAD_CACHE.extend(lane_dense(CASTLE_ROAD_WORLD, 2.0))
    return _ROAD_CACHE


def write_castle_stub(ctx, cat):
    """castle_interior_stub: a 6 x 5 x 4 m stone room with a doorway, carrying "loadRadius": [90, 130], to prove the
    load-when-close rule before the castle's rooms depend on it."""
    import json
    from mapgen.catalog import Asset
    P = [{"t": "b", "c": [0, 0.1, 0], "s": [6.0, 0.2, 5.0], "m": "floor"},
         {"t": "b", "c": [0, 4.1, 0], "s": [6.4, 0.2, 5.4], "m": "stone"},
         {"t": "b", "c": [-3.1, 2.0, 0], "s": [0.4, 4.0, 5.4], "m": "stone"}, {"t": "b", "c": [3.1, 2.0, 0], "s": [0.4, 4.0, 5.4], "m": "stone"},
         {"t": "b", "c": [0, 2.0, 2.6], "s": [6.4, 4.0, 0.4], "m": "stone"},
         {"t": "b", "c": [-2.05, 2.0, -2.6], "s": [2.3, 4.0, 0.4], "m": "stone"}, {"t": "b", "c": [2.05, 2.0, -2.6], "s": [2.3, 4.0, 0.4], "m": "stone"},
         {"t": "b", "c": [0, 3.3, -2.6], "s": [1.8, 1.4, 0.4], "m": "stone"}]
    j = {"id": "castle_interior_stub", "version": 1, "name": "Castle interior (stub)", "category": "Effects",
         "description": "BT2: a stand-in room proving the load-when-close rule (loadRadius) for the castle's interior; generated by blackwater_thaw.py.",
         "floors": 0, "enterable": False, "footprint": [6.4, 5.4], "plinthDepth": 0, "maxGrade": 90, "noAutoPanels": True,
         "loadRadius": [90.0, 130.0], "nav": {"effect": "none"},
         "materials": [{"name": "stone", "albedo": [0.5, 0.48, 0.44], "roughness": 0.9, "pattern": "stone", "metallic": 0, "grain": 1},
                       {"name": "floor", "albedo": [0.35, 0.33, 0.3], "roughness": 0.85, "pattern": "plain", "metallic": 0, "grain": 1}],
         "parts": P, "furniture": [], "interactables": [], "panels": []}
    path = os.path.join(ctx.repo, "content", "assets", "castle_interior_stub.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(j, f, separators=(",", ":"))
    cat.assets["castle_interior_stub"] = Asset(j, path)


def write_castle_interior(ctx, cat):
    """castle_interior: the castle's rooms (the shell bt_castle stays the far exterior and holds their outer walls), built
    only within 90 m and dropped beyond 130 m ("loadRadius"); enterable, so the room detection, auto-furnishing and
    interior lighting work on it. Same transform as the shell (origin = the palace pad). Rooms: a two-storey guard room
    and chamber in the bower, the stair block from the court (y -10) to the palace level (y 0), the great hall on the
    palace's ground floor, the lateral wing as an anteroom, a spiral stair up the side tower to an open lookout."""
    import json
    from mapgen.catalog import Asset
    P = []
    WIN = []

    def box(c, s_, m, r=None, k=1):
        p_ = {"t": "b", "c": [round(v, 3) for v in c], "s": [round(v, 3) for v in s_], "m": m}
        if r is not None:
            p_["r"] = [0, round(r, 2), 0]
        if not k:
            p_["k"] = 0
        P.append(p_)

    def lining(cx, cy, cz, sx, sy, sz, doors=(), t=0.12):   # the room's inner faces, 0.8 m in from the shell's outer
        ix, iz = sx - 1.6, sz - 1.6
        for face, fx, fz, L, along in (("x-", cx - ix / 2 - t / 2, None, iz, "z"), ("x+", cx + ix / 2 + t / 2, None, iz, "z"),
                                       ("z-", None, cz - iz / 2 - t / 2, ix, "x"), ("z+", None, cz + iz / 2 + t / 2, ix, "x")):
            a0 = (cz if along == "z" else cx) - L / 2; cur = a0
            ops = sorted([d for d in doors if d[0] == face], key=lambda d: d[1])
            for _f, al, oy, w, h in ops + [(face, a0 + L + 99.0, 0.0, 0.0, 0.0)]:
                lo, hi = (al - w / 2, al + w / 2) if w > 0 else (a0 + L, a0 + L)
                if lo > cur:
                    mid, ln = (cur + lo) / 2, lo - cur
                    box((fx, cy, mid) if along == "z" else (mid, cy, fz), (t, sy, ln) if along == "z" else (ln, sy, t), "plaster")
                if w > 0:
                    top = cy + sy / 2
                    if oy + h < top:
                        hh = top - oy - h
                        box((fx, oy + h + hh / 2, al) if along == "z" else (al, oy + h + hh / 2, fz), (t, hh, w) if along == "z" else (w, hh, t), "plaster")
                    cur = hi

    def stairs(x0, x1, z0, dz, y0, n, rise=0.2, run=0.28, m="tread_x"):   # a straight flight along z (dz = +1 / -1)
        for k_ in range(n):
            box(((x0 + x1) / 2, y0 + rise * (k_ + 0.5), z0 + dz * run * (k_ + 0.5)), (x1 - x0, rise * (k_ + 1), run), m)

    yL = -10.0
    # the bower: guard room (ground) and chamber (upper, +6 m), a flight along its north wall
    lining(-11.0, yL + 10.0, -30.0, 13.0, 20.0, 16.0, doors=[("x+", -26.0, yL, 1.6, 2.6)])
    box((-11.0, yL + 0.1, -30.0), (11.4, 0.2, 14.4), "floor_x")
    stairs(-16.3, -15.0, -36.6, 1, yL, 30)
    for k_ in range(0, 30, 6):   # a wall string and rail on the open side
        box((-14.95, yL + 0.2 * (k_ + 3) + 0.9, -36.6 + 0.28 * (k_ + 3)), (0.06, 0.06, 1.75), "plank_x", k=0)
    box((-10.15, yL + 6.0, -30.0), (9.7, 0.3, 14.4), "plank_x")                     # the upper floor, open over the stair
    box((-15.65, yL + 6.0, -25.3), (1.3, 0.3, 5.0), "plank_x")
    # the stair block: from the court door (west) up two flights to a gallery at the palace level, out (north) to the hall
    lining(12.0, yL + 9.0, -14.0, 12.0, 18.0, 12.0, doors=[("x-", -14.0, yL, 1.6, 2.6), ("z+", 7.6, 0.0, 1.6, 2.6)])
    box((12.0, yL + 0.1, -14.0), (10.4, 0.2, 10.4), "floor_x")
    stairs(9.0, 10.4, -9.2, -1, yL, 25)                                            # up 5 m going south
    for k_ in range(0, 25, 5):   # handrails on the flights' open sides
        box((10.45, yL + 0.2 * (k_ + 2.5) + 0.9, -9.2 - 0.28 * (k_ + 2.5)), (0.06, 0.06, 1.45), "plank_x", k=0)
        box((11.55, yL + 5.0 + 0.2 * (k_ + 2.5) + 0.9, -16.2 + 0.28 * (k_ + 2.5)), (0.06, 0.06, 1.45), "plank_x", k=0)
    box((11.0, yL + 5.0 - 0.15, -17.6), (4.0, 0.3, 2.8), "plank_x")                   # the landing
    stairs(11.6, 13.0, -16.2, 1, yL + 5.0, 25)                                     # up 5 m going north
    box((10.0, -0.15, -9.4), (6.4, 0.3, 1.2), "plank_x")                              # the gallery to the north door
    # the great hall: the palace's ground floor (y 0 .. 10), 16.4 x 56.4 m
    hall_win = [(f, z) for f in ("x-", "x+") for z in [-0.5 + 4.6 * k for k in range(12)] if not (f == "x-" and 27.0 < z < 41.0)]
    lining(0.0, 5.0, 25.0, 18.0, 10.0, 58.0, doors=[("z-", 7.6, 0.0, 1.6, 2.6)] + [(f, z, 1.5, 1.8, 6.1) for f, z in hall_win])
    box((0.0, -0.1, 25.0), (16.4, 0.2, 56.4), "plank_x")
    for f, z in hall_win:   # declared windows: the room pass gives them sills (daylight spill) and keeps them clear
        x = -9.0 if f == "x-" else 9.0
        WIN.append({"id": "hall_%s_%d" % (f[1], int(z * 10)), "kind": "window", "label": "window", "sound": "glass", "axis": [0, 1, 0],
                    "openAngle": 0, "duration": 0.5, "defaultOpen": False,
                    "aperture": {"min": [round(min(x, x * 0.92), 2), 1.5, round(z - 0.9, 2)], "max": [round(max(x, x * 0.92), 2), 7.6, round(z + 0.9, 2)]}, "leaves": []})
    box((0.0, 0.2, 50.0), (16.0, 0.4, 6.0), "floor_x")                            # the dais at the north end
    for zc in range(4, 50, 9):                                                     # wall hangings between the windows
        for x in (-8.1, 8.1):
            box((x, 6.8, zc + 2.3), (0.06, 3.2, 1.6), "hanging", k=0)
    for zc in range(2, 50, 8):                                                     # a row of stone piers down the hall
        box((0.0, 5.0, float(zc) + 2.0), (0.8, 10.0, 0.8), "stone")
    # the lateral wing (anteroom), the side tower's spiral stair and its lookout platform
    lining(20.0, yL + 7.0, -8.0, 10.0, 14.0, 20.0, doors=[("x-", -12.0, yL, 1.6, 2.6)])
    box((20.0, yL + 0.1, -8.0), (8.4, 0.2, 18.4), "floor_x")
    TX, TZ, TH = 26.0, 0.0, 42.0
    P.append({"t": "c", "c": [TX, yL + TH / 2, TZ], "s": [1.0, TH, 1.0], "m": "stone", "n": 12})   # the newel
    nst = int(TH / 0.2); last = 0.0
    for k_ in range(nst - 1):
        ang = math.radians(180.0 + 15.0 * k_); last = ang
        cx_, cz_ = TX + 1.8 * math.cos(ang), TZ + 1.8 * math.sin(ang)
        box((cx_, yL + 0.2 * (k_ + 1) - 0.1, cz_), (2.6, 0.2, 0.85), "tread_x", r=-math.degrees(ang))
    for k_ in range(16):   # the lookout floor: a ring of slabs, open over the stair's last turn
        ang = math.radians(11.25 + 22.5 * k_)
        d_ = (math.degrees(ang) - math.degrees(last)) % 360.0
        if d_ < 40.0 or d_ > 330.0:
            continue
        box((TX + 1.8 * math.cos(ang), yL + TH - 0.1, TZ + 1.8 * math.sin(ang)), (2.6, 0.2, 1.4), "floor_x", r=-math.degrees(ang))
    j = {"id": "castle_interior", "version": 1, "name": "Hilltop castle interior", "category": "Landmarks",
         "description": "BT2: the hilltop castle's rooms (guard room and chamber, stair block, great hall, anteroom, the side tower's spiral stair and lookout), built only near (loadRadius 90 / 130 m); placed with bt_castle. Generated by blackwater_thaw.py.",
         "floors": 2, "enterable": True, "footprint": [60.0, 150.0], "plinthDepth": 0, "maxGrade": 90, "noAutoPanels": True,
         "loadRadius": [90.0, 130.0],
         # the rooms' kinds for the furnishing (GX6: nav room boxes + roomTypes; nav "none": players only, no bots)
         "nav": {"effect": "none", "rooms": [
             {"id": "guard", "min": [-16.7, -10.0, -37.2], "max": [-5.3, -4.2, -22.8]},
             {"id": "chamber", "min": [-16.7, -4.0, -37.2], "max": [-5.3, 9.6, -22.8]},
             {"id": "stairs", "min": [6.8, -10.0, -19.2], "max": [17.2, 7.8, -8.8]},
             {"id": "great_hall", "min": [-8.2, -0.5, -3.2], "max": [8.2, 10.0, 53.2]},
             {"id": "anteroom", "min": [15.8, -10.0, -17.2], "max": [24.2, 4.0, 1.2]},
             {"id": "lookout", "min": [22.0, 31.0, -4.0], "max": [30.0, 36.0, 4.0]}]},
         "roomTypes": {"guard": "tavern", "chamber": "bedroom", "stairs": "hall", "great_hall": "tavern", "anteroom": "sitting", "lookout": "hall"},
         "materials": [{"name": "plaster", "albedo": [0.62, 0.58, 0.50], "roughness": 0.9, "pattern": "plaster", "metallic": 0, "grain": 1},
                       {"name": "stone", "albedo": [0.46, 0.44, 0.41], "roughness": 0.88, "pattern": "stone", "metallic": 0, "grain": 1},
                       {"name": "plank_x", "albedo": [0.30, 0.21, 0.13], "roughness": 0.7, "pattern": "wood", "metallic": 0, "grain": 0},
                       {"name": "floor_x", "albedo": [0.42, 0.40, 0.37], "roughness": 0.85, "pattern": "stone", "metallic": 0, "grain": 1},
                       {"name": "tread_x", "albedo": [0.40, 0.38, 0.35], "roughness": 0.85, "pattern": "stone", "metallic": 0, "grain": 1},
                       {"name": "hanging", "albedo": [0.32, 0.10, 0.08], "roughness": 0.95, "pattern": "fabric", "metallic": 0, "grain": 1}],
         "parts": P, "furniture": [], "interactables": WIN, "panels": []}
    path = os.path.join(ctx.repo, "content", "assets", "castle_interior.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(j, f, separators=(",", ":"))
    cat.assets["castle_interior"] = Asset(j, path)
    return len(P)
