"""Defensive positions: MG-nest kits, interlocking-fire siting, defense slots derived from structures.

Slot yaw uses the scenario convention (src/mpui.cpp: objective attackYaw + 180): a character look yaw, i.e. the
slot faces (-sin yaw, -cos yaw). Asset nav yaws (0 = local +Z) are converted with look_yaw().
"""
import math

from .geom import (local_to_world, look_yaw, asset_yaw_facing, wrap_deg, resample, dist, norm)

MG_RANGE = 300.0       # effective MG 42 range used for sector coverage (m)
MG_ARC = 55.0          # traverse either side of the bore (gun_mg42_embrasure: 55 deg)
CROSS_MIN = 30.0       # two guns "interlock" on a point when their bearings to it differ by at least this


def slot(x, y, z, look, kind="position", priority=1):
    return {"pos": [round(x, 3), round(y, 3), round(z, 3)], "yaw": round(wrap_deg(look), 2), "kind": kind, "priority": priority}


_KIND = {"embrasure": ("embrasure", 3), "mg": ("mg_nest", 3), "firestep": ("trench_bay", 2), "window": ("window", 2)}


def slots_from_structures(b, insts, max_per=None, kinds=None):
    """Defense slots at the nav firing points of placed structures (bunker embrasures, MG rings, trench firesteps,
    house windows, foxholes). kinds: optional subset of firing-point kinds to keep."""
    out = []
    for e in insts:
        a = b.cat[e["asset"]]
        fps = a.firing_points
        if kinds:
            fps = [f for f in fps if f.get("kind") in kinds]
        if max_per:
            fps = fps[:max_per]
        for f in fps:
            p = f["p"]
            wx, wz = local_to_world((e["pos"][0], e["pos"][2]), e["yaw"], p[0], p[2])
            Y = math.radians(e["yaw"] + f.get("yaw", 0.0))
            kind, pri = _KIND.get(f.get("kind"), ("position", 1))
            if e["asset"] == "aw_foxhole":
                kind, pri = "foxhole", 1
            out.append(slot(wx, e["pos"][1] + p[1], wz, look_yaw(math.sin(Y), math.cos(Y)), kind, pri))
        if not fps and e["asset"] == "aw_foxhole":
            out.append(slot(e["pos"][0], e["pos"][1] - 1.2, e["pos"][2], look_yaw(math.sin(math.radians(e["yaw"])), math.cos(math.radians(e["yaw"]))), "foxhole", 1))
    return out


# ---------------------------------------------------------------- line of sight on the base grid
def los(b, p0, h0, p1, h1, step=8.0):
    d = dist(p0, p1)
    n = max(2, int(d / step))
    y0 = b.ground(*p0) + h0
    y1 = b.ground(*p1) + h1
    for k in range(1, n):
        t = k / n
        x, z = p0[0] + (p1[0] - p0[0]) * t, p0[1] + (p1[1] - p0[1]) * t
        if b.grid.sample(x, z) > y0 + (y1 - y0) * t + 0.3:
            return False
    return True


def _bearing(frm, to):
    return math.degrees(math.atan2(to[0] - frm[0], to[1] - frm[1]))


def _adiff(a, b):
    return abs(wrap_deg(a - b))


def lane_samples(lanes, center, reach=350.0, step=20.0):
    pts = []
    for ln in lanes:
        for p in resample(ln, step):
            d = dist(p, center)
            if d <= reach:
                pts.append((p, 1.0 + 1.5 * (1.0 - d / reach)))   # nearer the objective weighs more
    return pts


def covers(nest, p, b=None):
    (x, z), bore = nest[0], nest[1]
    if dist((x, z), p) > MG_RANGE:
        return False
    if _adiff(_bearing((x, z), p), bore) > MG_ARC:
        return False
    return los(b, (x, z), 1.5, p, 1.2) if b is not None else True


def site_mg_nests(b, center, lanes, n, rng, *, ring=(25.0, 110.0), toward=None, arc=110.0, min_sep=28.0,
                  candidates=160, fixed=(), accept=None):
    """Choose n MG positions around an objective so their sectors interlock over the attack lanes.
    center: objective (x, z); lanes: polylines the attackers use; toward: direction (x, z) attackers come from
    (default: mean of the lane points). fixed: nests already decided [((x, z), bore_bearing), ...] that count for
    crossfire. accept(x, z) -> bool filters candidates (placeable ground). Returns [((x, z), bore_bearing), ...] with
    bore_bearing in degrees as atan2(dx, dz) (asset-yaw convention: local +Z along the bore)."""
    samples = lane_samples(lanes, center)
    if not samples:
        return []
    if toward is None:
        mx = sum(p[0] for p, _ in samples) / len(samples) - center[0]
        mz = sum(p[1] for p, _ in samples) / len(samples) - center[1]
        toward = norm((mx, mz))
    base = math.degrees(math.atan2(toward[0], toward[1]))
    cands = []
    tries = 0
    while len(cands) < candidates and tries < candidates * 20:
        tries += 1
        ang = math.radians(base + rng.uniform(-arc, arc))
        r = rng.uniform(*ring)
        x, z = center[0] + r * math.sin(ang), center[1] + r * math.cos(ang)
        if accept and not accept(x, z):
            continue
        # bore: the bearing (of 13 around the threat axis) that sees the most weighted lane points
        best = None
        for k in range(-6, 7):
            bore = base + k * 12.0
            cov = [i for i, (p, w) in enumerate(samples) if dist((x, z), p) <= MG_RANGE and _adiff(_bearing((x, z), p), bore) <= MG_ARC]
            sc = sum(samples[i][1] for i in cov)
            if best is None or sc > best[0]:
                best = (sc, bore, cov)
        vis = [i for i in best[2] if los(b, (x, z), 1.5, samples[i][0], 1.2)]
        cands.append(((x, z), best[1], vis))
    chosen = [(tuple(f[0]), f[1], [i for i, (p, w) in enumerate(samples) if covers(f, p, b)]) for f in fixed]
    out = []
    for _ in range(n):
        bestc, bests = None, -1
        for c in cands:
            if any(dist(c[0], o[0]) < min_sep for o in chosen):
                continue
            g = 0.0
            for i in c[2]:
                p, w = samples[i]
                others = [o for o in chosen if i in o[2]]
                if not others:
                    g += w
                elif any(_adiff(_bearing(o[0], p), _bearing(c[0], p)) >= CROSS_MIN for o in others):
                    g += 0.8 * w if len(others) == 1 else 0.15 * w
                else:
                    g += 0.05 * w
            if g > bests:
                bests, bestc = g, c
        if bestc is None:
            break
        chosen.append(bestc)
        out.append((bestc[0], bestc[1]))
    return out


def interlock_report(b, nests, lanes, center, reach=250.0):
    """Fraction of lane points within `reach` of the objective seen by >= 1 and by >= 2 guns from bearings at least
    CROSS_MIN apart, and the pairs of guns whose sectors overlap on some lane point."""
    samples = lane_samples(lanes, center, reach, 15.0)
    one = two = 0
    pairs = set()
    for p, _ in samples:
        seen = [k for k, n in enumerate(nests) if covers(n, p, b)]
        if seen:
            one += 1
        ok = False
        for i in range(len(seen)):
            for j in range(i + 1, len(seen)):
                if _adiff(_bearing(nests[seen[i]][0], p), _bearing(nests[seen[j]][0], p)) >= CROSS_MIN:
                    ok = True
                    pairs.add((seen[i], seen[j]))
        two += ok
    n = max(1, len(samples))
    return {"lanePoints": len(samples), "covered": one / n, "crossfire": two / n, "interlockingPairs": len(pairs)}


# ---------------------------------------------------------------- nest kits
def build_mg_nest(b, pos, bore, style="sandbag", tags=(), rng=None, gun=True):
    """Place an MG position facing `bore` (deg, asset-yaw convention) and return (instances, slot).
    style: "sandbag" (three-sided sandbag horseshoe with an MG 42 on its pedestal), "tobruk" (Ringstand 58c),
    "hedge" (sandbag shelf in a hedgerow corner: the caller places the hedge), "pit" (curved sandbags only).
    Returns ([], None) when the ground refuses it."""
    x, z = pos
    tg = set(tags) | {"mg_nest"}
    placed = []
    if style == "tobruk":
        e = b.place("aw_tobruk_mg", x, z, bore, tags=tg)
        if not e:
            return [], None
        fp = b.cat["aw_tobruk_mg"].firing_points
        s = slots_from_structures(b, [e])
        sl = s[0] if s else slot(x, b.ground(x, z), z, look_yaw(math.sin(math.radians(bore)), math.cos(math.radians(bore))), "mg_nest", 3)
        sl["kind"], sl["priority"] = "mg_nest", 3
        return [e], sl
    grp = b.new_group()
    front = [(0.0, 1.6, 0.0), (-1.75, 1.0, 40.0), (1.75, 1.0, -40.0)]
    if style == "pit":
        front = [(0.0, 2.2, 0.0), (-1.55, 1.55, 45.0), (1.55, 1.55, -45.0)]
    for lx, lz, dy in front:
        wx, wz = local_to_world((x, z), bore, lx, lz)
        aid = "def_sandbag_curved" if style == "pit" else "def_sandbag_straight"
        e = b.place(aid, wx, wz, bore + dy, group=grp, tags=tg, ok_with={"flat", "none", "wire", "veg"})
        if e:
            placed.append(e)
    if len(placed) < 2:
        for e in placed:
            b.remove(e)
        return [], None
    y = b.ground(x, z)
    if gun and style != "pit":
        g = b.place("gun_mg42_embrasure", x, z, bore, group=grp, tags=tg, ok_with={"flat", "none", "wire", "veg"})
        if g:
            placed.append(g)
    look = look_yaw(math.sin(math.radians(bore)), math.cos(math.radians(bore)))
    return placed, slot(x, y, z, look, "mg_nest", 3)
