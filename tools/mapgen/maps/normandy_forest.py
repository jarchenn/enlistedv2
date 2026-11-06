"""Default map 2, terrain only: Normandy Forest - Mont-Castre, Hill 122 (loosely after SPEC 3.4).

A blank, editable starting map: the user places every structure, objective, spawn and rack. This script builds
only the ground: rolling hills, the Hill 122 ridge rising toward the far (defender) end, rock escarpments and tors on
its flanks, a quarry and a ravine cut, a draw with a sunken lane up to the summit, stream valleys and a marsh in the
attackers' lowland. The map file carries no instances and no scenario, only surface routes (lane, marsh, streams).

Coordinates below are (x, s): x east, s metres from the attackers' edge toward the ridge (Frame).
"""
import json
import math
import os

from mapgen import (Grid, Frame, MapBuilder, Profile, sculpt_zones, stamp_valley, stamp_channel, stamp_flatten,
                    catmull_rom, write_map, write_template, write_summary, fbm, vnoise, smoothstep, clamp, lerp,
                    polyline_length, resample, point_at, look_yaw)

TID = "normandy_forest"
VERSION = 1            # bump whenever the heightmap changes (saved crater damage is tied to it)
SIZE = (850, 1700)     # width x depth (m): half of Omaha's area, same 1:2 shape. (600, 1200) for a smaller map.
W, D = SIZE
MARGIN, SEED = 100, 1944122
K = W / 850.0          # every layout coordinate below scales with the map (authored at 850 x 1700)
KS = D / 1700.0
NAME = "Normandy Forest - Mont-Castre, Hill 122"


def P_(x, s):
    return (x * K, s * KS)


# Ridge height above the lowland along the attack axis (absolute metres, Hill 122 tops out near 110 m).
BASE = Profile([(-300, 30), (0, 30), (300, 31), (430, 35), (560, 44), (700, 57), (850, 71), (1000, 85),
                (1150, 99), (1250, 106), (1330, 107), (1450, 99), (1600, 90), (1700, 86), (2000, 84)])


def ground(x, s):
    """Base relief before stamps: the ridge profile, falling away east and west of the summit, plus two layers of
    rolling hills (5-25 m relief) that soften on the crest."""
    sn = s / KS
    xn = x / K
    h = BASE(sn)
    ridge_w = smoothstep(500, 1250, sn) * (1.0 - 0.5 * smoothstep(1350, 1700, sn))
    h -= 16.0 * ridge_w * ((xn - 60) / 425.0) ** 2                      # Hill 122 is a ridge, highest a little east
    hills = 17.0 * fbm(xn / 300.0, sn / 300.0, 5, 3) + 6.5 * fbm(xn / 125.0, sn / 125.0, 9, 3)
    h += hills * (1.0 - 0.45 * smoothstep(1100, 1300, sn) * (1.0 - smoothstep(1350, 1500, sn)))
    h += 0.9 * fbm(xn / 34.0, sn / 34.0, 13, 2)                           # small swells
    return h


# ---------------------------------------------------------------- stamps written for this map
def _nearest(pts, p):
    """Signed distance to a polyline (positive on the travel-left side), arc length at the foot, total length."""
    best = None
    s_acc = 0.0
    for i in range(len(pts) - 1):
        ax, az = pts[i]
        bx, bz = pts[i + 1]
        dx, dz = bx - ax, bz - az
        L2 = dx * dx + dz * dz
        L = math.sqrt(L2)
        t = clamp(((p[0] - ax) * dx + (p[1] - az) * dz) / L2, 0.0, 1.0)
        qx, qz = ax + dx * t, az + dz * t
        d = math.hypot(p[0] - qx, p[1] - qz)
        if best is None or d < best[0]:
            side = 1.0 if dx * (p[1] - az) - dz * (p[0] - ax) > 0 else -1.0
            best = (d, side, s_acc + L * t)
        s_acc += L
    return best[0] * best[1], best[2], s_acc


def _bounds(pts, pad):
    xs, zs = [p[0] for p in pts], [p[1] for p in pts]
    return (min(xs) - pad, min(zs) - pad, max(xs) + pad, max(zs) + pad)


def stamp_escarpment(grid, pts, height, *, face=2.4, back=70.0, seed=1, taper=28.0, up_share=0.6):
    """Rock escarpment along a polyline: a step of `height(u)` m across a 2*face m wide face (steeper than 60 deg for
    heights over ~9 m), the high side on the travel-left. The step blends back into the hillside over `back` m on
    both sides and tapers to nothing over `taper` m at each end, so the ends are walkable. The cliff edge wanders."""
    pts = catmull_rom(pts, 10)
    L = polyline_length(pts)

    def fn(x, z, h):
        d, sa, _ = _nearest(pts, (x, z))
        if abs(d) > back + 8:
            return h
        u = sa / L
        e = smoothstep(0, taper, sa) * smoothstep(0, taper, L - sa)
        if e <= 0:
            return h
        d += 2.2 * vnoise(sa / 13.0, 0.5, seed)                              # the edge is not a ruler line
        H = height(u) * e * (0.82 + 0.18 * vnoise(sa / 21.0, 3.7, seed + 1))
        step = smoothstep(-face, face, d)
        up = up_share * H * step * (1.0 - smoothstep(back * 0.2, back, d))
        down = (1.0 - up_share) * H * (1.0 - step) * (1.0 - smoothstep(back * 0.15, back, -d))
        return h + up - down
    grid.apply(fn, _bounds(pts, back + 10))


def stamp_tor(grid, cx, cz, radius, height, *, face=2.2, seed=1):
    """Rock outcrop (tor): a block of `height` m with a near-vertical wall and an uneven, domed top."""
    def fn(x, z, h):
        dx, dz = x - cx, z - cz
        r = math.hypot(dx, dz)
        if r > radius * 1.5 + face:
            return h
        a = math.atan2(dz, dx)
        R = radius * (1.0 + 0.22 * vnoise(math.cos(a) * 1.7 + 4, math.sin(a) * 1.7 + 4, seed))
        k = 1.0 - smoothstep(R - face, R + face, r)
        top = 0.18 * height * max(0.0, 1.0 - (r / R) ** 2) + 1.2 * fbm(x / 6.0, z / 6.0, seed + 3, 2)
        return h + k * (height + top)
    grid.apply(fn, (cx - radius * 1.6 - face, cz - radius * 1.6 - face, cx + radius * 1.6 + face, cz + radius * 1.6 + face))


def stamp_quarry(grid, cx, cz, half_w, depth, *, face=2.4, rise=0.02, sink=3.0):
    """Old stone quarry cut into a hillside that rises toward +Z: a level floor at the ground height of the mouth
    (z = cz), walled on the back and both sides by the cut, open downhill."""
    floor = grid.sample(cx, cz) - sink

    def fn(x, z, h):
        u, v = x - cx, z - cz
        if v < -25 or v > depth + 8 or abs(u) > half_w + 8:
            return h
        m = smoothstep(-face, face, half_w - abs(u) + 2.0 * vnoise(v / 9.0, 1.3, 71)) * \
            smoothstep(-face, face, depth - v + 2.0 * vnoise(u / 9.0, 2.1, 72)) * smoothstep(-25, 0, v)
        f = floor + rise * max(0.0, v) + 0.4 * fbm(x / 7.0, z / 7.0, 73, 2)
        return h - m * max(0.0, h - f)
    grid.apply(fn, (cx - half_w - 10, cz - 30, cx + half_w + 10, cz + depth + 10))
    return floor


def stamp_cut(grid, path, depth, top_half, floor_half, power=1.0):
    """A valley whose floor follows the existing ground `depth(u)` m down (ravine, draw)."""
    pts = catmull_rom(path, 8)
    L = polyline_length(pts)
    samples = []
    for k in range(41):
        p, _ = point_at(pts, L * k / 40.0)
        samples.append((k / 40.0, grid.sample(*p) - depth(k / 40.0)))
    floor = Profile(samples)
    # the floor must fall monotonically downhill along the path (no ponds): u = 0 is the low end
    lo = [floor(u / 40.0) for u in range(41)]
    for k in range(1, 41):
        lo[k] = max(lo[k], lo[k - 1] + 0.02)
    def mono(u):   # linear between samples (a Profile eases to flat at every knot and would terrace the floor)
        f = clamp(u, 0.0, 1.0) * 40.0
        k = min(39, int(f))
        return lerp(lo[k], lo[k + 1], f - k)
    stamp_valley(grid, pts, floor=mono, top_half=top_half, floor_half=floor_half, power=power)
    return pts


def const(v):
    return lambda u: v


# ---------------------------------------------------------------- layout (authored at 850 x 1700, scaled by K, KS)
STREAMS = [[(-460, 420), (-330, 360), (-250, 260), (-150, 200), (-40, 110), (60, 40), (110, -40)],
           [(470, 330), (380, 300), (300, 230), (240, 140), (160, 70), (110, -40)]]
MARSH = [(205, 210), (265, 180), (320, 215)]
DRAW = [(-40, 560), (-48, 700), (-40, 840), (-20, 980), (10, 1110), (40, 1210)]          # main climbable draw
LANE = [(-120, -40), (-110, 120), (-70, 300), (-52, 450), (-44, 600), (-46, 720), (-38, 850), (-18, 985),
        (12, 1110), (44, 1205), (60, 1275)]                                                  # sunken lane to the summit
TRACK_E = [(330, -40), (300, 200), (330, 420), (300, 640), (330, 820), (300, 960), (250, 1180), (150, 1290)]
TRAIL_W = [(-330, 540), (-260, 760), (-230, 960), (-170, 1120), (-60, 1240), (40, 1280)]
QUARRY = (165, 740)          # mouth of the quarry (opens toward the attackers)
RAVINE = [(-395, 720), (-385, 860), (-360, 980), (-330, 1090), (-300, 1180)]   # u = 0 at the low end
ESCARPS = [   # (polyline with the high side on the travel-left, height(u))
    ([(-380, 905), (-300, 925), (-215, 905), (-130, 930)], lambda u: 11 + 9 * math.sin(math.pi * u)),     # north face, west
    ([(60, 1000), (100, 1020), (140, 1010)], lambda u: 9 + 5 * math.sin(math.pi * u)),                     # above the quarry
    ([(250, 1035), (320, 1060), (395, 1040)], lambda u: 13 + 10 * math.sin(math.pi * u)),                  # north face, east
    ([(-415, 1390), (-400, 1300), (-385, 1210)], lambda u: 10 + 7 * math.sin(math.pi * u)),                # west end of the ridge
    ([(415, 1180), (400, 1290), (410, 1400)], lambda u: 9 + 6 * math.sin(math.pi * u)),                    # east end (falls to +x)
    ([(40, 1485), (-60, 1470), (-150, 1490)], lambda u: 8 + 5 * math.sin(math.pi * u)),                    # south slope ledge
]
TORS = [(-60, 1175, 9, 10), (205, 1305, 11, 13), (-235, 1420, 8, 9), (330, 1215, 7, 8), (-290, 1030, 6, 7)]


def build(ctx):
    fr = Frame(W, D)
    w = fr.w
    P = lambda pts: [w(*P_(x, s)) for x, s in pts]
    grid = Grid(W, D, margin=MARGIN)

    # ------------------------------------------------------------------ base relief
    sculpt_zones(grid, fr, [{"s": (-400, D + 400), "height": lambda x, s: ground(x, s)}])

    # streams and the marsh in the attackers' lowland
    for st in STREAMS:
        stamp_channel(grid, catmull_rom(P(st), 8), half_width=2.2, depth=3.2, bank=22.0)
    mx, mz = w(*P_(265, 205))
    marsh_h = grid.sample(mx, mz) - 1.4
    stamp_flatten(grid, mx, mz, radius=48 * K, height=marsh_h, falloff=30)

    # the escarpments and tors on the ridge flanks
    for k, (pts, hfn) in enumerate(ESCARPS):
        stamp_escarpment(grid, P(pts), hfn, seed=40 + k)
    for k, (x, s, r, hgt) in enumerate(TORS):
        stamp_tor(grid, *w(*P_(x, s)), r, hgt, seed=60 + k)

    # quarry (cut into the north flank) and the ravine on the west flank
    qx, qz = w(*P_(*QUARRY))
    quarry_floor = stamp_quarry(grid, qx, qz, half_w=34 * K, depth=80 * KS)
    stamp_cut(grid, P(RAVINE), depth=lambda u: 4 + 10 * math.sin(math.pi * clamp(u * 1.15, 0, 1)),
              top_half=const(10.0), floor_half=const(2.5), power=0.7)

    # the climbable routes: a broad draw between the escarpments, the sunken lane up it to the summit
    stamp_cut(grid, P(DRAW), depth=lambda u: 2 + 5 * math.sin(math.pi * u), top_half=const(46.0),
              floor_half=const(9.0), power=1.4)
    lane = catmull_rom(P(LANE), 8)
    stamp_channel(grid, lane, half_width=1.9, depth=1.5, bank=3.0)
    sx, sz = w(*P_(60, 1285))
    stamp_flatten(grid, sx, sz, radius=22, falloff=26, strength=0.75)      # the summit clearing
    hmin, hmax = grid.quantise()

    # ------------------------------------------------------------------ surface routes (no instances)
    b = MapBuilder(ctx.catalog, grid, fr, seed=SEED)
    b.route("lane", lane, 3.2)
    b.route("lane", catmull_rom(P(TRACK_E), 8), 3.0)
    b.route("footpath", catmull_rom(P(TRAIL_W), 8), 1.5)
    b.route("footpath", catmull_rom(P(MARSH), 6), 62 * K, wet=1.0, strength=0.42)
    for st in STREAMS:
        b.route("footpath", catmull_rom(P(st), 8), 3.0, wet=1.0, strength=0.55)

    # ------------------------------------------------------------------ stats: relief, slopes
    slopes = []
    xs = [-W / 2 + 2 + k * 3.0 for k in range(int((W - 4) / 3.0))]
    zs = [-D / 2 + 2 + k * 3.0 for k in range(int((D - 4) / 3.0))]
    for z in zs:
        for x in xs:
            slopes.append(grid.slope_deg(x, z))
    steep60 = sum(1 for v in slopes if v > 60)
    steep50 = sum(1 for v in slopes if v > 50)
    build_h = [grid.sample(x, z) for z in zs[::4] for x in xs[::4]]
    lo, hi = min(build_h), max(build_h)
    nan = sum(1 for v in grid.h if v != v)

    # ------------------------------------------------------------------ output
    tdir = ctx.templates
    grid.write_png(os.path.join(tdir, TID + ".height.png"))
    a_edge = w(*P_(-110, 40))
    cam = ([w(0, 0)[0] - 120 * K, hi + 25.0, w(0, 0)[1] - 60], 188.0, -14.0)
    mp = os.path.join(tdir, TID + ".nmap.json")
    write_map(mp, name=NAME, template_id=TID, template_version=VERSION, seed=SEED, width=W, depth=D, builder=b,
              scenario_json=None, explore_start=((a_edge[0], grid.sample(*a_edge), a_edge[1]), 180.0), camera=cam)
    with open(mp, encoding="utf-8") as f:            # terrain base: no scenario at all (the user authors it)
        j = json.load(f)
    j.pop("scenario", None)
    with open(mp, "w", encoding="utf-8", newline="\n") as f:
        json.dump(j, f, separators=(",", ":"))
    def view(name, frm, to, up, gameplay=False):
        a, t = w(*P_(*frm)), w(*P_(*to))
        ya, yt = grid.sample(*a) + up, grid.sample(*t) + 4.0
        dist_ = math.hypot(t[0] - a[0], t[1] - a[1])
        return {"name": name, "pos": [round(a[0], 2), round(ya, 2), round(a[1], 2)], "yaw": round(look_yaw(t[0] - a[0], t[1] - a[1]), 2),
                "pitch": round(math.degrees(math.atan2(yt - ya, dist_)), 2), "gameplay": gameplay}
    views = [view("cliffs_west_escarpment", (-235, 800), (-280, 925), 7.0),
             view("quarry", (150, 690), (165, 800), 4.0),
             view("cliffs_east_escarpment_tor", (250, 930), (330, 1060), 5.0),
             view("ravine", (-410, 760), (-345, 1010), 9.0),
             view("hills_lowland_to_ridge", (-60, 150), (0, 1000), 1.7, True),
             view("summit_overlook", (60, 1285), (-40, 700), 3.0)]
    tv = view("thumb", (-410, 760), (-345, 1010), 9.0)                        # preview: the ravine and the west escarpment
    thumb_cam = (tv["pos"], tv["yaw"], tv["pitch"])
    write_template(os.path.join(tdir, TID + ".json"), tid=TID, name=NAME, order=2, version=VERSION, seed=SEED,
                   description="Terrain base for the forest of Mont-Castre and Hill 122 (90th Infantry Division, 3-12 July 1944): "
                               "rolling bocage lowland with streams and a marsh, the ridge climbing about 80 m to the summit, "
                               "rock escarpments, tors, an old quarry and a ravine on its flanks, a draw and a sunken lane up to "
                               "the top. No structures, objectives or spawns: build your own layout. %d x %d m." % (W, D),
                   width=W, depth=D, margin=MARGIN, heightmap=grid.meta(TID + ".height.png"), water_level=-100.0,
                   map_file=TID + ".nmap.json", camera=cam, thumbnail=TID + ".thumb.png", thumb_camera=thumb_cam,
                   playable=False, extra={"views": views})
    rep = {"size": [W, D], "heights": [hmin, hmax], "buildAreaHeights": [round(lo, 2), round(hi, 2)],
           "relief": round(hi - lo, 2), "maxSlopeDeg": round(max(slopes), 2), "samplesOver60": steep60,
           "samplesOver50": steep50, "samples": len(slopes), "nan": nan, "quarryFloor": round(quarry_floor, 2),
           "summit": round(grid.sample(sx, sz), 2), "lowlandA": round(grid.sample(*a_edge), 2)}
    write_summary(os.path.join(ctx.out, TID + ".summary.json"), rep)
    print("heights %.1f..%.1f (relief %.1f) | max slope %.1f deg | >60: %d, >50: %d of %d | summit %.1f | NaN %d"
          % (lo, hi, hi - lo, max(slopes), steep60, steep50, len(slopes), rep["summit"], nan))
