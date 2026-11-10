"""Generates the Vegetation (trees) and Defences (tank trap, sandbags) assets into content/assets/.

Run from the repository root:  python tools/gen_env_assets.py
All geometry is procedural box parts in the engine's asset format (metres, y up). Deterministic (fixed seeds).
"""
import json, math, random, os

OUT = os.path.join(os.path.dirname(__file__), '..', 'content', 'assets')
# navigation metadata (src/navmeta.h) for these assets, kept in one table with the rest of the catalogue backfill:
# tools/nav_backfill.json, written by tools/build_mpu_assets.mjs (W4). Written as each asset's last key.
_NAV_PATH = os.path.join(os.path.dirname(__file__), 'nav_backfill.json')
NAV = json.load(open(_NAV_PATH, encoding='utf-8')) if os.path.exists(_NAV_PATH) else {}
# P1: the lights' furnishing envelopes as their version 1 parts gave them (tools/prop_envelopes.json, see gen_furniture.py)
ENVELOPES = json.load(open(os.path.join(os.path.dirname(__file__), 'prop_envelopes.json'), encoding='utf-8'))


def box(c, s, m, r=None, k=1, v=1):
    p = {"t": "b", "c": [round(x, 4) for x in c], "s": [round(x, 4) for x in s], "m": m}
    if r: p["r"] = [round(x, 2) for x in r]
    if k != 1: p["k"] = k
    if not v: p["v"] = 0
    return p


def cyl(c, d, h, m, dz=None, top=1.0, n=0, seg=0, r=None, k=1, v=1, t="c"):
    """P1 (B1 kit): cylinder / frustum along local y (d across x, dz across z; top = top / base section)."""
    p = {"t": t, "c": [round(x, 4) for x in c], "s": [round(d, 4), round(h, 4), round(d if dz is None else dz, 4)], "m": m}
    if n: p["n"] = n
    if seg: p["seg"] = seg
    if t == "k" or top != 1.0: p["top"] = round(top, 4)
    if r: p["r"] = [round(x, 2) for x in r]
    if k != 1: p["k"] = k
    if not v: p["v"] = 0
    return p


def arch(c, s, m, th, r=None, k=0, fill=False):
    """P1: arch ring (or a solid half-disc when th exceeds both radii) standing on its box's bottom face, along local z."""
    p = {"t": "a", "c": [round(x, 4) for x in c], "s": [round(x, 4) for x in s], "m": m, "th": round(th, 4), "k": k}
    if fill: p["fill"] = 1
    if r: p["r"] = [round(x, 2) for x in r]
    return p


def ring(c, d, w, h, m):
    """P1: a flat open hoop (diameter d, band width w, height h) in the xz plane: two half-ring arches laid flat."""
    return [arch([c[0], c[1], c[2] + sg * d / 4], [d, d / 2, h], m, th=w, r=[90, 0 if sg > 0 else 180, 0]) for sg in (1, -1)]


def bevel(p, bv):
    """P1: mesh-only chamfer on a box part (the collider stays the box)."""
    p["bv"] = round(min(bv, 0.5 * min(p["s"]) - 1e-4), 4)
    return p


def twisted_wire(parts, x0, x1, y, z, m, pitch=0.24, r=0.004, w=0.003, barb=0.3, sag=0.0, k=0, seed=0):
    """P1: two-ply barbed wire along x from x0 to x1 at height y (sagging by `sag` mid-span): each ply crosses the
    other every half pitch (a braid seen from the side), four-point barbs every `barb` metres."""
    L = x1 - x0
    n = max(2, int(round(L / (pitch / 2))))
    seg = L / n
    yy = lambda x: y - sag * (1 - (2 * (x - (x0 + x1) / 2) / L) ** 2)
    for i in range(n):
        xa, xb = x0 + i * seg, x0 + (i + 1) * seg
        for s in (-1, 1):
            sg = s * (1 if i % 2 == 0 else -1)
            ya, yb = yy(xa) + sg * r, yy(xb) - sg * r
            ang = math.degrees(math.atan2(yb - ya, seg))
            parts.append(box([(xa + xb) / 2, (ya + yb) / 2, z + s * 0.0006], [math.hypot(seg, yb - ya) + w * 0.6, w, w], m, r=[0, 0, ang], k=k))
    rng = random.Random(seed)
    nb = int(L / barb) if barb else 0
    for i in range(nb):
        x = x0 + (i + 0.5) * L / nb
        a0 = rng.uniform(0, 90)
        for a in (a0, a0 + 90):
            parts.append(box([x, yy(x), z], [0.003, 0.026, 0.003], m, r=[a, 0, 0], k=k))


def save(a):
    if a["id"] in NAV:
        a = dict(a); a.pop("nav", None); a["nav"] = NAV[a["id"]]
    with open(os.path.join(OUT, a["id"] + ".json"), "w", encoding="utf-8", newline="") as f:
        json.dump(a, f, separators=(",", ":"))


# ---------------------------------------------------------------- trees
def trunk(parts, h, r0, r1, segs, m, lean=(0, 0), rng=None):
    """stacked boxes tapering from radius r0 to r1 (two crossed boxes per segment read as a rounded bole)"""
    for i in range(segs):
        y0, y1 = h * i / segs, h * (i + 1) / segs
        r = r0 + (r1 - r0) * (i + 0.5) / segs
        cx, cz = lean[0] * (y0 + y1) * 0.5, lean[1] * (y0 + y1) * 0.5
        parts.append(box([cx, (y0 + y1) / 2, cz], [2 * r, y1 - y0 + 0.02, 2 * r], m))
        parts.append(box([cx, (y0 + y1) / 2, cz], [2 * r * 0.98, y1 - y0 + 0.02, 2 * r * 0.98], m, r=[0, 45, 0], k=0))


def limb(parts, base, length, yaw, pitch, rad, m):
    yr, pr = math.radians(yaw), math.radians(pitch)
    d = [math.sin(yr) * math.cos(pr), math.sin(pr), math.cos(yr) * math.cos(pr)]
    c = [base[i] + d[i] * length / 2 for i in range(3)]
    # box long axis = y, rotated: pitch from vertical then yaw
    parts.append(box(c, [2 * rad, length, 2 * rad], m, r=[90 - pitch, yaw, 0], k=0))
    return [base[i] + d[i] * length for i in range(3)]


def tree(id_, name, desc, h_trunk, r0, r1, crown, limbs, leaf_col, leaf_size, cards, seed, height):
    rng = random.Random(seed)
    parts = []
    trunk(parts, h_trunk, r0, r1, 6, "bark")
    for (yaw, pitch, L, rad) in limbs:
        limb(parts, [0, h_trunk * 0.92, 0], L, yaw, pitch, rad, "bark")
    canopy = [[round(c[0], 3), round(c[1], 3), round(c[2], 3), round(c[3], 3)] for c in crown]
    return {
        "id": id_, "version": 1, "name": name, "category": "Vegetation", "description": desc,
        "floors": 0, "enterable": False, "footprint": [1.2, 1.2], "plinthDepth": 0, "maxGrade": 1.5,
        "materials": [{"name": "bark", "albedo": [0.17, 0.14, 0.11], "roughness": 0.95, "pattern": "wood", "metallic": 0, "grain": 1}],
        "parts": parts, "furniture": [], "interactables": [], "panels": [],
        "tree": {"hpBullet": 30, "hpAT": 1, "stumpHeight": 0.4, "trunkRadius": r0, "mass": round(math.pi * r0 * r0 * height * 0.5 * 700),
                 "canopy": canopy, "cardsPerSphere": cards, "leafSize": leaf_size, "leafColor": leaf_col, "seed": seed,
                 "note": "Hit points are game tuning: 30 small-arms hits, or one AT / HE strike within 1.5 m of the trunk."},
    }


def crown_ring(n, y, R, r, rng, jitter=0.25):
    out = []
    for i in range(n):
        a = 2 * math.pi * i / n + rng.uniform(-0.3, 0.3)
        out.append((math.sin(a) * R, y + rng.uniform(-jitter, jitter), math.cos(a) * R, r * rng.uniform(0.85, 1.1)))
    return out


# ---- v2 trees: recursive branch hierarchy, foliage clusters at the limb tips (layered canopy), smaller leaves
def branch_tree(id_, name, desc, seed, trunk_h, r0, n_primary, prim_len, prim_pitch, sec_per, sec_len, cluster_r, leaf_col, leaf_size,
                height, lean=(0.0, 0.0), columnar=False, extra_inner=3, density=34, mat="bark", hp=30):
    rng = random.Random(seed)
    parts = []
    trunk(parts, trunk_h, r0, r0 * 0.72, 6, mat, lean=lean)
    top = [lean[0] * trunk_h, trunk_h * 0.96, lean[1] * trunk_h]
    canopy = []
    for i in range(n_primary):
        yaw = 360.0 * i / n_primary + rng.uniform(-25, 25)
        pitch = prim_pitch + rng.uniform(-10, 10)
        L = prim_len * rng.uniform(0.75, 1.2)
        base = [top[0], top[1] - rng.uniform(0, trunk_h * (0.25 if not columnar else 0.6)), top[2]]
        tip = limb(parts, base, L, yaw, pitch, r0 * 0.34, mat)
        for k in range(sec_per):
            y2 = yaw + rng.uniform(-45, 45); p2 = pitch + rng.uniform(-15, 25)
            st = [base[c] + (tip[c] - base[c]) * rng.uniform(0.45, 0.9) for c in range(3)]
            t2 = limb(parts, st, sec_len * rng.uniform(0.7, 1.2), y2, p2, r0 * 0.16, mat)
            r = cluster_r * rng.uniform(0.75, 1.15)
            canopy.append((t2[0], t2[1] + r * 0.25, t2[2], r))
        canopy.append((tip[0], tip[1] + cluster_r * 0.2, tip[2], cluster_r * rng.uniform(0.9, 1.25)))
    for k in range(extra_inner):   # inner mass so the crown has depth, not a shell of blobs
        canopy.append((top[0] + rng.uniform(-0.6, 0.6) * prim_len * 0.4, top[1] + prim_len * rng.uniform(0.2, 0.55), top[2] + rng.uniform(-0.6, 0.6) * prim_len * 0.4, cluster_r * rng.uniform(1.0, 1.4)))
    canopy = [[round(c[0], 3), round(c[1], 3), round(c[2], 3), round(c[3], 3)] for c in canopy]
    return {"id": id_, "version": 2, "name": name, "category": "Vegetation", "description": desc,
            "floors": 0, "enterable": False, "footprint": [1.2, 1.2], "plinthDepth": 0, "maxGrade": 1.5,
            "materials": [{"name": "bark", "albedo": [0.16, 0.135, 0.11], "roughness": 0.95, "pattern": "wood", "metallic": 0, "grain": 1},
                          {"name": "bark_pale", "albedo": [0.30, 0.29, 0.26], "roughness": 0.9, "pattern": "wood", "metallic": 0, "grain": 1}],
            "parts": parts, "furniture": [], "interactables": [], "panels": [],
            "tree": {"hpBullet": hp, "hpAT": 1, "stumpHeight": 0.4, "trunkRadius": r0, "mass": round(math.pi * r0 * r0 * height * 0.5 * 700),
                     "canopy": canopy, "cardsPerSphere": density, "cardDensity": density, "leafSize": leaf_size, "leafColor": leaf_col, "seed": seed,
                     "note": "Hit points are game tuning: small-arms hits, or one AT / HE strike within 1.5 m of the trunk."}}


# ---- shrubs and hedges (bocage): woody stems under layered foliage; hedgerows sit on an earth bank

# ---- v3 nature models (NT pass): round tapered trunks with a root flare and buttress roots, curved tapering limbs with
# twigs, clumped and layered leaf clusters, coppiced shrubs and hedges with bare stems at the foot. Tree colliders stay
# the v2 trunk boxes (now invisible), so cover, damage, felling and the stump cut behave exactly as before; every visual
# part is "k": 0. Shared helpers (other species can use them):
#   seg3(parts, a, b, ra, rb, m, n)      one tapered round segment (B1 frustum "k") from point a to point b
#   flare3(parts, r0, stump, m, rng)     root flare + buttress roots, all below the stump cut (they stay on the stump)
#   trunk3(...) / limb3(...)             a wobbling tapered bole / a curved limb, as point lists
#   clump3(canopy, c, r, rng, sats, out) a leaf cluster: a main sphere and smaller satellites pushed outward (lumpy)
# Foliage assets split their parts at the stump cut by the part's own y extent (foliage.cpp), so anything meant to stay
# on the stump must end below it; round parts never split, they go whole to the side their extent says.
def mat3(name, albedo, rough=0.95):   # bark: the L4 bark set along the part's axis over a plain base (no plank seams)
    return {"name": name, "albedo": albedo, "roughness": rough, "pattern": "plain", "texture": "bark", "metallic": 0, "grain": 1}


BARK3 = [mat3("bark", [0.15, 0.125, 0.10], 0.96), mat3("bark_pale", [0.30, 0.29, 0.26], 0.9), mat3("bark_moss", [0.13, 0.15, 0.085], 0.97)]


def seg3(parts, a, b, ra, rb, m, n=8):
    d = [b[i] - a[i] for i in range(3)]
    L = math.sqrt(sum(x * x for x in d))
    pitch = math.degrees(math.asin(max(-1.0, min(1.0, d[1] / L))))
    p = {"t": "k", "c": [round((a[i] + b[i]) / 2, 4) for i in range(3)], "s": [round(2 * ra, 4), round(L + 0.02, 4), round(2 * ra, 4)],
         "top": round(min(1.0, rb / ra), 3), "seg": n, "m": m, "k": 0}
    if abs(90 - pitch) > 0.05:
        p["r"] = [round(90 - pitch, 2), round(math.degrees(math.atan2(d[0], d[2])), 2), 0]
    parts.append(p)
    return list(b)


def along(pts, f):
    """point at fraction f of a polyline (by segment count)"""
    s = min(len(pts) - 2, int(f * (len(pts) - 1))); t = f * (len(pts) - 1) - s
    return [pts[s][i] + (pts[s + 1][i] - pts[s][i]) * t for i in range(3)]


def flare3(parts, r0, stump, m, rng, roots=5, n=12):
    rb, rt = r0 * 1.55, r0 * 1.04
    parts.append({"t": "k", "c": [0, round((stump - 0.15) / 2, 4), 0], "s": [round(2 * rb, 4), round(stump + 0.145, 4), round(2 * rb, 4)],
                  "top": round(rt / rb, 3), "seg": n, "m": m, "k": 0})
    y0 = stump * 0.3
    for i in range(roots):
        yaw = math.radians(360.0 * i / roots + rng.uniform(-20, 20)); p = math.radians(rng.uniform(-12, -5))
        L = min(r0 * 2.4, 2 * (stump - 0.015 - y0) / (1 + math.sin(p)))   # top of the part's y extent stays under the cut
        a = [math.sin(yaw) * r0 * 0.55, y0, math.cos(yaw) * r0 * 0.55]
        d = [math.sin(yaw) * math.cos(p), math.sin(p), math.cos(yaw) * math.cos(p)]
        seg3(parts, a, [a[k] + d[k] * L for k in range(3)], r0 * 0.42, r0 * 0.05, m, 6)


def trunk3(parts, h, r0, r1, stump, lean, m, rng, nseg=3, n=12, wob=0.25):
    pts, rad = [], []
    for i in range(nseg + 1):
        t = i / nseg; y = stump + (h - stump) * t
        w = wob * r0 * (1 if 0 < i else 0)
        pts.append([lean[0] * y + rng.uniform(-w, w), y, lean[1] * y + rng.uniform(-w, w)])
        rad.append(r0 * 1.04 + (r1 - r0 * 1.04) * t ** 0.8)
    for i in range(nseg):
        seg3(parts, pts[i], pts[i + 1], rad[i], rad[i + 1], m, n)
    return pts


def limb3(parts, base, L, yaw, pitch, bend, ra, rb, m, nseg=2, n=7, rng=None):
    pts = [list(base)]
    for s in range(nseg):
        pr = math.radians(pitch + bend * s / max(1, nseg - 1)); yr = math.radians(yaw + (rng.uniform(-8, 8) if rng else 0))
        d = [math.sin(yr) * math.cos(pr), math.sin(pr), math.cos(yr) * math.cos(pr)]
        pts.append([pts[-1][k] + d[k] * L / nseg for k in range(3)])
        seg3(parts, pts[-2], pts[-1], ra + (rb - ra) * s / nseg, ra + (rb - ra) * (s + 1) / nseg, m, n)
    return pts


def clump3(canopy, c, r, rng, sats=2, out=(0.0, 0.0), up=0.25):
    canopy.append((c[0], c[1] + r * up, c[2], r * rng.uniform(0.82, 0.95)))
    for _ in range(sats):
        a = rng.uniform(0, 2 * math.pi); e = rng.uniform(-0.3, 0.75)
        d = [math.cos(e) * math.sin(a) + out[0] * 0.8, math.sin(e), math.cos(e) * math.cos(a) + out[1] * 0.8]
        dl = math.sqrt(sum(x * x for x in d)); o = r * rng.uniform(0.6, 0.85) / dl
        canopy.append((c[0] + d[0] * o, c[1] + r * up + d[1] * o, c[2] + d[2] * o, r * rng.uniform(0.45, 0.62)))


def cards3(canopy, dens):   # leaf cards the engine builds for this canopy (foliage.cpp): draw-cost check
    return sum(max(12, int(dens * c[3] * c[3] * 3)) for c in canopy)


def tree3(id_, name, desc, seed, trunk_h, r0, n_primary, prim_len, prim_pitch, sec_per, sec_len, cluster_r, leaf_col, leaf_size,
          height, lean=(0.0, 0.0), columnar=False, extra_inner=3, density=34, mat="bark", hp=30, bend=-14, roots=5, nseg=3, low=0.25):
    rng = random.Random(seed)
    parts = []
    for i in range(6):   # colliders: the v2 trunk boxes, unchanged, invisible
        y0, y1 = trunk_h * i / 6, trunk_h * (i + 1) / 6
        r = r0 + (r0 * 0.72 - r0) * (i + 0.5) / 6
        parts.append(box([lean[0] * (y0 + y1) * 0.5, (y0 + y1) / 2, lean[1] * (y0 + y1) * 0.5], [2 * r, y1 - y0 + 0.02, 2 * r], mat, v=0))
    flare3(parts, r0, 0.4, "bark_moss" if mat == "bark" else mat, rng, roots)
    top_h = trunk_h + prim_len * 0.55 if columnar else trunk_h * 1.04
    bole = trunk3(parts, top_h, r0, r0 * (0.14 if columnar else 0.5), 0.4, lean, mat, rng, nseg)
    at = lambda y: along(bole, max(0.0, min(1.0, (y - 0.4) / (top_h - 0.4))))
    canopy = []
    for i in range(n_primary):
        yaw = 360.0 * i / n_primary + rng.uniform(-25, 25)
        pitch = prim_pitch + rng.uniform(-10, 10)
        L = prim_len * rng.uniform(0.75, 1.2)
        y = trunk_h * 0.35 + (trunk_h * 0.98 - trunk_h * 0.35) * (i + rng.uniform(0.2, 0.8)) / n_primary if columnar else trunk_h * 0.96 - rng.uniform(0, trunk_h * low)
        pts = limb3(parts, at(y), L, yaw, pitch, bend, r0 * 0.42, r0 * 0.14, mat, 2, 8, rng)
        out = (math.sin(math.radians(yaw)), math.cos(math.radians(yaw)))
        for k in range(sec_per):
            st = along(pts, rng.uniform(0.45, 0.9))
            y2 = yaw + rng.uniform(-45, 45); p2 = pitch + rng.uniform(-15, 25); L2 = sec_len * rng.uniform(0.7, 1.2)
            sp = limb3(parts, st, L2, y2, p2, 10, r0 * 0.15, r0 * 0.05, mat, 1, 5)
            tw = along(sp, 0.55)   # a twig off the secondary, up and out into its own leaf mass
            twt = seg3(parts, tw, [tw[0] + out[0] * L2 * 0.35 + rng.uniform(-0.2, 0.2), tw[1] + L2 * 0.4, tw[2] + out[1] * L2 * 0.35 + rng.uniform(-0.2, 0.2)],
                       r0 * 0.06, r0 * 0.02, mat, 4)
            clump3(canopy, sp[-1], cluster_r * rng.uniform(0.75, 1.15), rng, 1, out)
            canopy.append((twt[0], twt[1] + 0.1, twt[2], cluster_r * rng.uniform(0.45, 0.6)))
        clump3(canopy, pts[-1], cluster_r * rng.uniform(0.9, 1.2), rng, 2, out, 0.2)
    top = bole[-1]
    for k in range(extra_inner):   # inner mass so the crown has depth, not a shell of blobs (columnar: all along the stem)
        c = along(bole, rng.uniform(0.35, 1.0)) if columnar else [top[0], top[1] + prim_len * rng.uniform(0.15, 0.5), top[2]]
        canopy.append((c[0] + rng.uniform(-0.6, 0.6) * prim_len * 0.4, c[1], c[2] + rng.uniform(-0.6, 0.6) * prim_len * 0.4, cluster_r * rng.uniform(0.9, 1.25)))
    canopy = [[round(c[0], 3), round(c[1], 3), round(c[2], 3), round(c[3], 3)] for c in canopy]
    print(f"{id_}: {len(parts)} parts, {len(canopy)} clusters, {cards3(canopy, density)} leaf cards")
    return {"id": id_, "version": 3, "name": name, "category": "Vegetation", "description": desc,
            "floors": 0, "enterable": False, "footprint": [1.2, 1.2], "plinthDepth": 0, "maxGrade": 1.5,
            "materials": BARK3, "parts": parts, "furniture": [], "interactables": [], "panels": [],
            "tree": {"hpBullet": hp, "hpAT": 1, "stumpHeight": 0.4, "trunkRadius": r0, "mass": round(math.pi * r0 * r0 * height * 0.5 * 700),
                     "canopy": canopy, "cardsPerSphere": density, "cardDensity": density, "leafSize": leaf_size, "leafColor": leaf_col, "seed": seed,
                     "note": "Hit points are game tuning: small-arms hits, or one AT / HE strike within 1.5 m of the trunk."}}


save(tree3("tree_apple", "Apple tree (orchard)", "Low Normandy cider-orchard apple: short leaning bole, spreading crooked limbs, clustered crown (~4.5 m).",
           11, 1.4, 0.14, 5, 1.5, 34, 2, 0.9, 0.85, [0.17, 0.25, 0.09], 0.19, 4.5, lean=(0.05, 0.02), extra_inner=3, density=56, bend=-18, roots=4))
save(tree3("tree_oak", "Hedgerow oak", "Pedunculate oak of the bocage: thick flared bole, wide irregular crown of many leaf clusters (~11 m).",
           13, 3.6, 0.38, 6, 3.2, 26, 3, 2.0, 1.35, [0.13, 0.2, 0.07], 0.22, 11.0, lean=(0.02, -0.03), extra_inner=7, density=60, bend=-16, roots=6, low=0.45))
save(tree3("tree_poplar", "Lombardy poplar", "Tall columnar poplar used as a windbreak along lanes (~16 m).",
           17, 12.0, 0.26, 12, 2.0, 76, 2, 1.2, 1.1, [0.19, 0.27, 0.1], 0.17, 16.0, columnar=True, extra_inner=8, density=40, mat="bark_pale", bend=6, nseg=5))
save(tree3("tree_ash", "Ash tree", "Young field ash: slender grey bole, open upswept crown (~8 m).",
           23, 2.8, 0.2, 5, 2.2, 45, 2, 1.4, 1.0, [0.18, 0.26, 0.09], 0.19, 8.0, lean=(-0.03, 0.03), extra_inner=3, density=50, mat="bark_pale", bend=10))


def shrub3(id_, name, desc, seed, canopy, parts, leaf_col, leaf_size, density, hp=15, bank=None, hedge=False):
    """v2 shrub / hedge: like shrub() but with ready stems (coppice3 / cane3) and clumped canopy"""
    soil = {"name": "bank_soil", "albedo": [0.19, 0.21, 0.11], "roughness": 0.97, "pattern": "plain", "metallic": 0, "grain": 1}   # turf-covered bank
    if bank:
        L, W, H = bank
        parts.append(box([0, H * 0.5, 0], [L, H, W * 0.55], "bank_soil"))
        parts.append(box([0, H * 0.25, W * 0.33], [L, H * 0.5, W * 0.3], "bank_soil", r=[-25, 0, 0]))
        parts.append(box([0, H * 0.25, -W * 0.33], [L, H * 0.5, W * 0.3], "bank_soil", r=[25, 0, 0]))
    canopy = [[round(a, 3) for a in c] for c in canopy]
    print(f"{id_}: {len(parts)} parts, {len(canopy)} clusters, {cards3(canopy, density)} leaf cards")
    t = {"hpBullet": 99999 if hedge else hp, "hpAT": 99999 if hedge else 1, "stumpHeight": 0.15, "trunkRadius": 0.05, "mass": 30,
         "canopy": canopy, "cardsPerSphere": density, "cardDensity": density, "leafSize": leaf_size, "leafColor": leaf_col, "seed": seed, "hedge": hedge}
    save({"id": id_, "version": 2, "name": name, "category": "Vegetation", "description": desc, "floors": 0, "enterable": False,
          "footprint": [bank[0], bank[1]] if bank else [1.5, 1.5], "plinthDepth": 0, "maxGrade": 1.5,
          "materials": BARK3 + [soil], "parts": parts, "furniture": [], "interactables": [], "panels": [], "tree": t})


def coppice3(parts, canopy, rng, x, z, y, stems, h0, h1, rs, spread, lobe, bare=0.4, side=True):
    """a coppice stool: stems splaying from one root, a leaf lobe at each top and a smaller one on the outer side; the
    lower `bare` share of each stem carries no leaves (visible stems at the foot)"""
    for i in range(stems):
        yaw = 360.0 * i / stems + rng.uniform(-30, 30); pitch = rng.uniform(90 - spread, 86); h = rng.uniform(h0, h1)
        pts = limb3(parts, [x + rng.uniform(-0.06, 0.06), y - 0.05, z + rng.uniform(-0.06, 0.06)], h / math.sin(math.radians(pitch)), yaw, pitch,
                    -rng.uniform(4, 14), rs, rs * 0.3, "bark", 2, 5)
        out = (math.sin(math.radians(yaw)), math.cos(math.radians(yaw)))
        clump3(canopy, pts[-1], lobe * rng.uniform(0.85, 1.1), rng, 1, out, 0.1)
        if not side:
            continue
        m = along(pts, rng.uniform(bare + 0.15, bare + 0.35))
        canopy.append((m[0] + out[0] * lobe * 0.45, m[1], m[2] + out[1] * lobe * 0.45, lobe * rng.uniform(0.55, 0.7)))


rng = random.Random(41)
parts, canopy = [], []
x = -3.75
while x < 3.8:   # the hedge body: overlapping leaf masses, undersides ~0.45 m above the bank (stems show), rolling top
    r = rng.uniform(0.72, 0.86)
    canopy.append((x, 0.5 + 0.5 + r * 0.95 + 0.25 * math.sin(x * 0.8 + 0.6), rng.uniform(-0.17, 0.17), r))
    x += rng.uniform(0.6, 0.75)
for k in range(8):   # hazel / hawthorn stools: bare stems at the foot, tops breaking the line unevenly, a few standards
    x = -3.5 + k * 1.0 + rng.uniform(-0.2, 0.2)
    tall = 0.35 * math.sin(k * 0.9 + 0.6) + (0.9 if rng.random() < 0.18 else 0.0)
    coppice3(parts, canopy, rng, x, rng.uniform(-0.2, 0.2), 0.5, 3 if rng.random() < 0.4 else 2, 1.7 + tall, 2.3 + tall, 0.035, 18, 0.5, 0.4, False)
    if k % 3 == 1:   # a low skirt here and there (the foot is not bare everywhere)
        canopy.append((x + 0.3, 0.85, rng.uniform(-0.3, 0.3), 0.38))
shrub3("hedgerow_bocage", "Bocage hedgerow (8 m)", "Earth bank topped by a dense hazel / hawthorn hedge on coppiced stools, uneven top, woody stems showing at the foot: blocks sight, gaps only where you leave them.",
       43, canopy, parts, [0.14, 0.21, 0.075], 0.34, 66, bank=(8.0, 1.5, 0.5), hedge=True)
parts, canopy = [], []
coppice3(parts, canopy, rng, 0, 0, 0, 8, 1.7, 2.5, 0.04, 26, 0.55, 0.35)
shrub3("shrub_hazel", "Hazel shrub", "Multi-stemmed coppiced hazel clump (~2.5 m), bare grey stems under clumped leaf masses.", 47,
       canopy, parts, [0.16, 0.24, 0.085], 0.3, 56)
parts, canopy = [], []
for i in range(7):   # arching bramble canes: up from the crown, over and down to root again; leaves in lumps along them
    yaw = 360.0 * i / 7 + rng.uniform(-20, 20)
    pts = limb3(parts, [rng.uniform(-0.2, 0.2), 0.0, rng.uniform(-0.2, 0.2)], rng.uniform(1.3, 1.8), yaw, rng.uniform(48, 62), -95, 0.009, 0.005, "bark", 3, 4, rng)
    for f in (0.25, 0.5, 0.75, 0.95):   # leaves out to the tip (a bare cane end reads as a spider leg)
        p = along(pts, f)
        canopy.append((p[0], max(0.2, p[1]), p[2], rng.uniform(0.27, 0.37)))
canopy.append((0, 0.45, 0, 0.5))
shrub3("shrub_bramble", "Bramble patch", "Low tangled bramble (~0.9 m) of arching canes for fence lines and ditch edges.", 53,
       canopy, parts, [0.12, 0.16, 0.07], 0.32, 45, hp=6)

# ---------------------------------------------------------------- defences
steel = {"name": "steel", "albedo": [0.20, 0.19, 0.17], "roughness": 0.6, "pattern": "metal", "metallic": 0.7, "grain": 1}
rust = {"name": "rust", "albedo": [0.30, 0.17, 0.09], "roughness": 0.85, "pattern": "metal", "metallic": 0.3, "grain": 1}
bag = {"name": "hessian", "albedo": [0.42, 0.36, 0.24], "roughness": 0.95, "pattern": "fabric", "metallic": 0, "grain": 1}
bag2 = {"name": "hessian_dk", "albedo": [0.36, 0.30, 0.20], "roughness": 0.95, "pattern": "fabric", "metallic": 0, "grain": 1}

# Czech hedgehog: three mutually perpendicular 1.4 m angle-iron beams, turned so a body diagonal is vertical
# (it then stands on three feet). Rotations are written as quaternions ("q": [x, y, z, w]).
def qmul(a, b):
    ax, ay, az, aw = a; bx, by, bz, bw = b
    return [aw*bx + ax*bw + ay*bz - az*by, aw*by - ax*bz + ay*bw + az*bx, aw*bz + ax*by - ay*bx + az*bw, aw*bw - ax*bx - ay*by - az*bz]
def qaxis(ax, deg):
    s_ = math.sin(math.radians(deg) / 2); return [ax[0]*s_, ax[1]*s_, ax[2]*s_, math.cos(math.radians(deg) / 2)]
def qfromto(u, v):
    c = [u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0]]; d = sum(u[i]*v[i] for i in range(3))
    q = [c[0], c[1], c[2], 1 + d]; n = math.sqrt(sum(x*x for x in q)); return [x / n for x in q]
def qrot(q, v):
    x, y, z, w_ = q; u = [x, y, z]
    d = sum(u[i] * v[i] for i in range(3)); uu = sum(c * c for c in u)
    cr = [u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0]]
    return [2 * d * u[i] + (w_ * w_ - uu) * v[i] + 2 * w_ * cr[i] for i in range(3)]
k3 = 1 / math.sqrt(3)
stand = qmul(qaxis([0, 1, 0], 15), qfromto([k3, k3, k3], [0, 1, 0]))
parts = []
# Complete update E2: the hedgehog is 1.25x its first version in every linear dimension (beams 1.4 -> 1.75 m,
# flanges 100 -> 125 mm, crossing 0.43 -> 0.54 m above the ground, overall ~1.1 m high). The size lives only in the
# asset (instances have no scale of their own), so maps saved with version 1 adopt it on load with no double scaling.
TT = 1.25
L, w, t = 1.4 * TT, 0.1 * TT, 0.012 * TT
cy = L / 2 * k3 + 0.03 * TT
for i, base in enumerate([[0, 0, 0, 1], qaxis([0, 0, 1], 90), qaxis([0, 1, 0], 90)]):
    q = qmul(stand, base)
    for sz in ([L, w, t], [L, t, w]):   # angle iron: two flanges
        p_ = bevel(box([0, cy, 0], sz, "steel" if i % 2 == 0 else "rust"), 0.005); p_["q"] = [round(x, 5) for x in q]; parts.append(p_)
parts.append(bevel(box([0, cy, 0], [0.16 * TT, 0.16 * TT, 0.16 * TT], "rust"), 0.012))   # gusset plate block at the crossing
parts.append(box([0, cy, 0], [1.1 * TT, 2 * cy, 1.1 * TT], "steel", k=1, v=0))  # collision hull (stops vehicles, cover for men)
save({"id": "def_tank_trap", "version": 2,   # P1: chamfers only (mesh), the v2 size and collision are kept (v1->v2 migration)
      "name": "Tank trap (Czech hedgehog)", "category": "Defences",
      "description": "Welded steel hedgehog, three 1.75 m angle-iron beams (about 1.1 m high). Immune to small arms; only HE / AT damage it.",
      "floors": 0, "enterable": False, "footprint": [1.75, 1.75], "plinthDepth": 0, "maxGrade": 0.6,
      "materials": [steel, rust], "parts": parts, "furniture": [], "interactables": [], "panels": [],
      "defense": {"kind": "tank_trap", "hp": 1500, "smallArmsMultiplier": 0.0, "blocksVehicles": True, "maxSlopeDeg": 35}})


def sandbag_wall(length, courses=5, curved_r=None, arc_deg=45):
    parts = []
    bl, bh, bd = 0.5, 0.16, 0.3   # one filled bag (length along the wall, height, depth)
    rng = random.Random(3)
    for c in range(courses):
        y = bh * 0.5 + c * bh * 0.98
        inset = c * 0.03
        for row in (0, 1):   # two bags deep
            z = (row - 0.5) * (bd - 0.02) * (1 - c * 0.08)
            n = int(length / bl) + 1
            for i in range(n):
                s = (i + (0.5 if c % 2 else 0)) * bl - length / 2 + bl / 2
                if abs(s) > length / 2 - bl * 0.3 + 0.001:
                    continue
                jit = rng.uniform(-3, 3)
                # P1: a filled bag is a soft pillow (deep chamfer, smooth normals), squashed by the courses above it,
                # bulging sideways, each a little different; bags slump toward the ends of the run
                load = (courses - 1 - c) / max(1, courses - 1)
                sz = [bl * rng.uniform(0.95, 1.04), bh * (1.02 - 0.1 * load) * rng.uniform(0.92, 1.05), (bd * 0.95 - inset) * (1 + 0.08 * load) * rng.uniform(0.96, 1.04)]
                slump = rng.uniform(-4, 4) + (6 * s / (length / 2) if c == courses - 1 else 0)
                ys = y - (bh - sz[1]) * 0.5 - rng.uniform(0, 0.012)
                mat = "hessian" if (i + c + row) % 3 else "hessian_dk"
                if curved_r is None:
                    parts.append(bevel(box([s, ys, z], sz, mat, r=[rng.uniform(-3, 3), jit, slump], k=0), 0.055))
                else:
                    a = s / curved_r   # arc parameter, centred on the wall middle
                    R = curved_r + z
                    parts.append(bevel(box([math.sin(a) * R, ys, math.cos(a) * R - curved_r], sz, mat,
                                     r=[rng.uniform(-3, 3), math.degrees(a) + jit, slump], k=0), 0.055))
                if (i + c) % 2 == 0:   # the tied-off neck folded under at one end of every other bag
                    e = (bl * 0.5 - 0.01) * (1 if (i + row) % 2 else -1)
                    if curved_r is None:
                        parts.append(bevel(box([s + e, ys - sz[1] * 0.18, z], [0.07, sz[1] * 0.55, sz[2] * 0.6], mat, r=[0, jit, 0], k=0), 0.025))
                    else:
                        a = (s + e) / curved_r
                        parts.append(bevel(box([math.sin(a) * R, ys - sz[1] * 0.18, math.cos(a) * R - curved_r], [0.07, sz[1] * 0.55, sz[2] * 0.6], mat, r=[0, math.degrees(a) + jit, 0], k=0), 0.025))
    h = courses * bh * 0.98
    if curved_r is None:
        parts.append(box([0, h / 2, 0], [length, h, 0.6], "hessian", v=0))
        sockets = [{"p": [-length / 2, 0, 0], "t": [-1, 0, 0]}, {"p": [length / 2, 0, 0], "t": [1, 0, 0]}]
    else:
        n = 6
        a0 = -length / curved_r / 2
        for i in range(n):   # collision: chord boxes along the arc
            a = a0 + (i + 0.5) * (length / curved_r) / n
            parts.append(box([math.sin(a) * curved_r, h / 2, math.cos(a) * curved_r - curved_r], [length / n + 0.04, h, 0.6], "hessian", r=[0, math.degrees(a), 0], v=0))
        ae = length / curved_r / 2
        sockets = [{"p": [round(-math.sin(ae) * curved_r, 4), 0, round(math.cos(ae) * curved_r - curved_r, 4)], "t": [round(-math.cos(ae), 4), 0, round(math.sin(ae), 4)]},
                   {"p": [round(math.sin(ae) * curved_r, 4), 0, round(math.cos(ae) * curved_r - curved_r, 4)], "t": [round(math.cos(ae), 4), 0, round(math.sin(ae), 4)]}]
    return parts, h, sockets


p1, h1, s1 = sandbag_wall(2.0)
save({"id": "def_sandbag_straight", "version": 2, "name": "Sandbag wall (straight)", "category": "Defences",
      "description": "2 m run of filled sandbags, five courses (~0.8 m), two bags deep. Stops small-arms fire; shelters from blast.",
      "floors": 0, "enterable": False, "footprint": [2.0, 0.6], "plinthDepth": 0, "maxGrade": 0.6,
      "materials": [bag, bag2], "parts": p1, "furniture": [], "interactables": [], "panels": [],
      "defense": {"kind": "sandbag", "hp": 300, "stopsBullets": True, "blastOcclusion": 0.1, "maxSlopeDeg": 35, "sockets": s1}})
arc = 2.5 * math.radians(45)
p2, h2, s2 = sandbag_wall(arc, curved_r=2.5)
save({"id": "def_sandbag_curved", "version": 2, "name": "Sandbag wall (curved 45 deg)", "category": "Defences",
      "description": "45 degree arc of sandbags on a 2.5 m radius; chains with straight runs into fighting positions.",
      "floors": 0, "enterable": False, "footprint": [2.0, 0.9], "plinthDepth": 0, "maxGrade": 0.6,
      "materials": [bag, bag2], "parts": p2, "furniture": [], "interactables": [], "panels": [],
      "defense": {"kind": "sandbag", "hp": 300, "stopsBullets": True, "blastOcclusion": 0.1, "maxSlopeDeg": 35, "sockets": s2}})
print("ok")


# ---------------------------------------------------------------- weapon racks (A-frame, weathered wood)
# Structure after references/weapon_rack_reference.png: two A-shaped end frames with angled legs, a lower
# buttstock tray with lips, an upper rail with individual barrel slots, feet boards and a rear brace. Launchers
# rest on side hooks outside one end frame; the Allied rack has the M2 on its tripod beside it.
def rack(id_, name, faction, display, deploy, desc):
    W = {"name": "rack_wood", "albedo": [0.34, 0.26, 0.17], "roughness": 0.86, "pattern": "wood", "metallic": 0, "grain": 1}
    W2 = {"name": "rack_wood_end", "albedo": [0.29, 0.22, 0.15], "roughness": 0.9, "pattern": "wood", "metallic": 0, "grain": 0}
    Fe = {"name": "rack_iron", "albedo": [0.12, 0.11, 0.1], "roughness": 0.7, "pattern": "metal", "metallic": 0.5, "grain": 1}
    parts = []
    H, half = 1.25, 0.82
    def leg(x, z0, z1):   # board from the foot (z0, 0) to the apex (z1, H)
        L = math.hypot(H, z1 - z0); a = math.degrees(math.atan2(z1 - z0, H))
        parts.append(box([x, H / 2, (z0 + z1) / 2], [0.05, L, 0.07], "rack_wood", r=[a, 0, 0]))
    for x in (-half, half):
        leg(x, 0.44, 0.02); leg(x, -0.42, -0.0)
        parts.append(box([x, 0.025, 0.0], [0.07, 0.05, 1.02], "rack_wood_end"))            # foot board
        parts.append(box([x, 0.36, 0.0], [0.045, 0.06, 0.66], "rack_wood"))               # stretcher
        parts.append(box([x, H - 0.02, 0.01], [0.07, 0.07, 0.12], "rack_wood_end"))       # apex block
    # buttstock tray with front and back lips
    parts.append(box([0, 0.085, 0.27], [2 * half - 0.05, 0.025, 0.32], "rack_wood"))
    parts.append(box([0, 0.12, 0.43], [2 * half - 0.05, 0.07, 0.025], "rack_wood"))
    parts.append(box([0, 0.12, 0.115], [2 * half - 0.05, 0.07, 0.025], "rack_wood"))
    # upper rail on the front legs with barrel slots (blocks between the slots)
    ry, rz = 1.02, 0.44 - (0.44 - 0.02) * 1.02 / H
    parts.append(box([0, ry, rz - 0.01], [2 * half - 0.05, 0.05, 0.06], "rack_wood"))
    for k in range(9):
        xs = -0.75 + k * 0.1875
        parts.append(box([xs, ry + 0.045, rz + 0.0], [0.05, 0.04, 0.07], "rack_wood_end", k=0))
    parts.append(box([0, 0.62, -0.2], [2 * half - 0.05, 0.05, 0.03], "rack_wood", r=[-60, 0, 0], k=0))   # rear brace
    for y, z in ((0.82, 0.22), (0.82, -0.22)):   # side hooks (launcher support): round bar, upturned tip, square plate
        parts.append(cyl([half + 0.09, y, z], 0.026, 0.16, "rack_iron", seg=8, r=[0, 0, 90], k=0))
        parts.append(cyl([half + 0.165, y + 0.03, z], 0.024, 0.07, "rack_iron", top=0.7, seg=8, k=0))
        parts.append(bevel(box([half + 0.032, y, z], [0.008, 0.07, 0.06], "rack_iron", k=0), 0.002))
    # P1: carriage bolts (round heads, square washers) at the joints instead of nail stubs
    for x in (-half, half):
        sx = 1 if x > 0 else -1
        for y, z in ((0.36, 0.2), (0.36, -0.2), (1.02, 0.08), (H - 0.03, 0.01), (0.03, 0.4), (0.03, -0.4)):
            parts.append(bevel(box([x + sx * 0.0275, y, z], [0.004, 0.034, 0.034], "rack_iron", k=0), 0.0015))
            parts.append(cyl([x + sx * 0.032, y, z], 0.022, 0.008, "rack_iron", top=0.6, seg=8, r=[0, 0, -90 * sx], k=0))
    for p in parts:   # every timber edge chamfered (mesh only: colliders are the boxes as before)
        if p["m"] in ("rack_wood", "rack_wood_end") and p["t"] == "b":
            bevel(p, 0.008)
    for p in [p for p in parts if p["m"] == "rack_wood_end" and p["s"] == [0.05, 0.04, 0.07]]:   # slot blocks get rounded tops
        parts.append(arch([p["c"][0], p["c"][1] + 0.02, p["c"][2]], [0.05, 0.022, 0.07], "rack_wood_end", th=0.2))
    parts.append(box([0, 0.65, 0.0], [2 * half + 0.1, 1.3, 0.95], "rack_wood", v=0))   # collision hull: simple and snag-free
    save({"id": id_, "version": 2, "name": name, "category": "Supply", "description": desc, "floors": 0, "enterable": False,
          "footprint": [2.6 if faction == "US" else 2.0, 1.1], "plinthDepth": 0, "maxGrade": 0.4, "materials": [W, W2, Fe], "parts": parts,
          "furniture": [], "interactables": [], "panels": [], "faction": faction,
          "rack": {"faction": faction, "display": display, "deploy": deploy}})

slots = [-0.56, -0.28, 0.0, 0.28, 0.56]
rack("rack_allied", "Allied weapon rack", "US",
     [{"weapon": w, "pose": "lean", "p": [x, 0.1, 0.42], "lean": 14} for w, x in zip(["m1_garand", "m1_carbine", "m1_thompson", "m1918a2_bar", "m1c_garand"], slots)] +
     [{"weapon": "bazooka_m9", "pose": "flat", "p": [0.985, 0.855, -0.75], "dir": 1},
      {"weapon": "m2hb_carry", "pose": "stand", "p": [1.75, 0.0, 0.0], "yaw": 0}],
     ["gun_m2hb_tripod"],
     "Weathered wooden A-frame rack. E opens the Allied arsenal (every US weapon in the registry, launcher and the carried M2).")
rack("rack_axis", "Axis weapon rack", "DE",
     [{"weapon": w, "pose": "lean", "p": [x, 0.1, 0.42], "lean": 14} for w, x in zip(["kar98k", "mp40", "fg42", "kar98k_zf39", "mg42"], slots)] +
     [{"weapon": "panzerschreck", "pose": "flat", "p": [0.985, 0.855, -0.8], "dir": 1}],
     ["gun_pak40"],
     "Weathered wooden A-frame rack. E opens the Axis arsenal (every German weapon in the registry and the Panzerschreck; the PaK 40 deploys through build mode).")
print("racks ok")


# ---------------------------------------------------------------- farm dressing (restrained, snag-free collision)
def dressing(id_, name, desc, parts, mats, footprint, version=1, inter=None):
    save({"id": id_, "version": version, "name": name, "category": "Dressing", "description": desc, "floors": 0, "enterable": False,
          "footprint": footprint, "plinthDepth": 0, "maxGrade": 0.8, "materials": mats, "parts": parts, "furniture": [], "interactables": inter or [], "panels": []})

post = {"name": "post_wood", "albedo": [0.27, 0.22, 0.16], "roughness": 0.92, "pattern": "wood", "metallic": 0, "grain": 1}
wire = {"name": "wire", "albedo": [0.30, 0.29, 0.27], "roughness": 0.6, "pattern": "metal", "metallic": 0.6, "grain": 1}
stone = {"name": "field_stone", "albedo": [0.42, 0.40, 0.36], "roughness": 0.92, "pattern": "stone", "metallic": 0, "grain": 1}
P_IRON = {"name": "gate_iron", "albedo": [0.09, 0.08, 0.075], "roughness": 0.7, "pattern": "metal", "metallic": 0.4, "grain": 1}
P_TAR = {"name": "pole_tar", "albedo": [0.08, 0.065, 0.05], "roughness": 0.8, "pattern": "wood", "metallic": 0, "grain": 1}
P_INS = {"name": "insulator", "albedo": [0.72, 0.7, 0.64], "roughness": 0.15, "pattern": "plain", "metallic": 0, "grain": 1}
rng = random.Random(61)
rngp = random.Random(611)   # P1: draws of the new detail (rng keeps its sequence so stone_wall_low is unchanged)
parts = []
for k in range(4):   # 8 m run: posts every 2.4 m, three strands of wire
    x = -3.6 + k * 2.4
    p_ = box([x, 0.6, 0], [0.1, 1.2 + rng.uniform(-0.06, 0.06), 0.1], "post_wood", r=[rng.uniform(-3, 3), rng.uniform(0, 40), rng.uniform(-3, 3)])
    p_["v"] = 0; parts.append(p_)   # P1: the post's box stays its collider; a split chestnut post (irregular pentagon) is drawn
    parts.append(cyl(p_["c"], 0.11, p_["s"][1], "post_wood", dz=0.095, top=0.88, n=5, r=p_["r"], k=0))
    for y in (0.45, 0.78, 1.08):   # wire staples
        parts.append(arch([x, y - 0.006, 0.05], [0.012, 0.016, 0.004], "wire", th=0.0025, r=[0, 0, 0]))
for y, barb in ((0.45, None), (0.78, None), (1.08, 0.3)):   # 2-ply twisted fence wire, barbed on top, sagging between posts
    for k in range(3):
        twisted_wire(parts, -3.6 + k * 2.4, -3.6 + (k + 1) * 2.4, y, 0.055, "wire", pitch=0.24, barb=barb, sag=0.025, seed=k * 7 + int(y * 100))
parts.append(box([0, 0.6, 0], [7.4, 1.2, 0.12], "post_wood", v=0))
dressing("fence_post_wire", "Post-and-wire fence (8 m)", "Split chestnut posts carrying three sagging strands of 2-ply twisted fence wire, the top strand barbed, along a field edge.", parts, [post, wire], [8.0, 0.4], 2)
parts = []
for x in (-1.75, 1.75):
    parts.append(bevel(box([x, 0.7, 0], [0.18, 1.4, 0.18], "post_wood"), 0.02))
    parts.append(bevel(box([x, 1.42, 0], [0.2, 0.04, 0.2], "post_wood", r=[0, 0, 0], k=0), 0.012))   # weathered cap
for k, y in enumerate((0.3, 0.65, 1.0)):
    parts.append(bevel(box([0, y, 0.12], [3.3, 0.09, 0.05], "post_wood"), 0.01))
parts.append(bevel(box([-0.2, 0.65, 0.12], [3.0, 0.08, 0.05], "post_wood", r=[0, 0, 15]), 0.01))
for x in (-1.1, 0.0, 1.1):
    parts.append(bevel(box([x, 0.65, 0.12], [0.07, 0.85, 0.05], "post_wood", k=0), 0.008))
for y in (0.3, 1.0):   # P1: strap hinges on the hanging post, a latch on the shutting post, bolt heads on the bars
    parts.append(bevel(box([-1.45, y, 0.149], [0.5, 0.04, 0.008], "gate_iron", k=0), 0.002))
    parts.append(cyl([-1.66, y, 0.12], 0.035, 0.07, "gate_iron", seg=8, k=0))
    for x in (-1.1, 0.0, 1.1):
        parts.append(cyl([x, y, 0.149], 0.018, 0.008, "gate_iron", top=0.6, seg=6, r=[90, 0, 0], k=0))
parts.append(bevel(box([1.6, 0.98, 0.15], [0.22, 0.03, 0.012], "gate_iron", k=0), 0.003))
parts.append(arch([1.75, 0.95, 0.15], [0.08, 0.05, 0.012], "gate_iron", th=0.012, r=[0, 0, -90]))
# LN2: the leaf (rails, brace, stiles, straps, latch) is a door hung on the left post: open = passable, closed = blocked (AI / nav as house doors)
leaf, parts = parts[4:], parts[:4]
dressing("field_gate", "Field gate", "Five-bar style timber field gate between two posts on iron strap hinges, with a latch; it opens like a door.", parts, [post, P_IRON], [3.8, 0.4], 2,
         inter=[{"id": "gate", "kind": "door", "label": "gate", "sound": "wood", "axis": [0, 1, 0], "openAngle": 95, "duration": 1.6, "defaultOpen": False,
                 "leaves": [{"parts": leaf, "hinge": [-1.66, 0, 0.12], "sign": 1}], "aperture": {"min": [-1.66, 0.05, -0.1], "max": [1.66, 1.3, 0.2]}}])
# telegraph pole: the old box stays the collider; drawn as a tapered round pole with a tarred foot, a bevelled cross-arm on
# two raking braces, a roof cap, step bolts and porcelain insulators on iron pins
parts = [box([0, 4.0, 0], [0.24, 8.0, 0.24], "post_wood", v=0), bevel(box([0, 7.5, 0], [1.6, 0.1, 0.1], "post_wood", k=0), 0.012)]
parts.append(cyl([0, 4.0, 0], 0.26, 8.0, "post_wood", top=0.72, seg=16, k=0))
parts.append(cyl([0, 0.45, 0], 0.27, 0.9, "pole_tar", top=0.97, seg=16, k=0))
parts.append(cyl([0, 8.03, 0], 0.2, 0.08, "pole_tar", top=0.3, seg=12, k=0))   # weather cap
for sx in (-1, 1):
    parts.append(bevel(box([sx * 0.28, 7.25, 0.0], [0.04, 0.6, 0.05], "post_wood", r=[0, 0, sx * 45], k=0), 0.008))
for i in range(10):   # staggered step bolts from 2.4 m up
    a_ = math.pi * (i % 2)
    parts.append(cyl([math.cos(a_) * 0.13, 2.4 + i * 0.45, 0.0], 0.02, 0.18, "gate_iron", seg=6, r=[0, 0, 90], k=0))
for x in (-0.65, -0.3, 0.3, 0.65):
    parts.append(cyl([x, 7.6, 0], 0.016, 0.1, "wire", seg=6, k=0))                                    # pin
    parts.append(cyl([x, 7.66, 0], 0.075, 0.06, "insulator", top=0.55, seg=12, k=0))                  # bell
    parts.append(cyl([x, 7.7, 0], 0.05, 0.03, "insulator", top=0.7, seg=12, k=0))                     # crown
dressing("utility_pole", "Telegraph pole", "Creosoted telegraph pole (~8 m), tarred at the foot: a cross-arm on raking braces, step bolts and porcelain insulators on iron pins.", parts, [post, wire, P_IRON, P_TAR, P_INS], [0.5, 0.5], 2)
parts = []
for k in range(12):   # dry-stone boundary wall, 6 m, ~0.9 m high, irregular stones
    x = -2.75 + k * 0.5
    for y in (0.15, 0.45, 0.72):
        parts.append(box([x + rng.uniform(-0.06, 0.06), y + rng.uniform(-0.03, 0.03), rng.uniform(-0.04, 0.04)], [0.48, 0.28 + rng.uniform(-0.04, 0.05), 0.55], "field_stone", r=[0, rng.uniform(-6, 6), rng.uniform(-4, 4)], k=0))
parts.append(box([0, 0.45, 0], [6.0, 0.9, 0.6], "field_stone", v=0))
dressing("stone_wall_low", "Low dry-stone wall (6 m)", "Field boundary of rough local stone, ~0.9 m: waist-high cover.", parts, [stone], [6.0, 0.6])
print("dressing ok")


# ---------------------------------------------------------------- wire obstacles (supplied GLB, complete update E1)
# Source: "wire_obstacle_defence_set (1).glb" (same geometry as the other supplied copy, larger textures), baked by
#   NormandyMapmaker.exe --import-weapon-models assets_src/models content
# The set holds four complete assemblies; three distinct ones become entities (Helix044 is the same coil mesh as
# Helix045, only compressed, so it is not a fourth type). Each keeps only its own source group ("hide" the rest),
# sits on the ground (pivot "ground"), is simplified to a runtime budget with a distance LOD, and is split into
# destruction sections along its length. Colliders: solid boxes for posts / timber (rounds and grenades hit them),
# "k": 2 player-only volumes for the wire (stops the player, ignored by rays and thrown objects).
WIRE_SRC = "wire_obstacle_defence_set (1).glb"
WIRE_GROUPS = ["Helix045", "Box003", "Capsule004", "Capsule003", "Helix044"]


def wire_model(keep, dir_, simplify, sections, extra=None):
    m = {"src": WIRE_SRC, "dir": dir_, "pivot": "ground", "scale": 1.0, "maxTex": 1024, "hide": [g for g in WIRE_GROUPS if g not in keep],
         "simplify": simplify, "lod1": 0.3, "sections": {"axis": 2, "count": sections}, "lodDist": 30, "cullDist": 350, "thinLinesPx": 0.6}
    if extra: m.update(extra)
    return m


def wire_asset(id_, name, desc, model, parts, footprint, sections, traversal="block", hp_note=""):
    return {"id": id_, "version": 1, "name": name, "category": "Defences", "description": desc, "floors": 0, "enterable": False,
            "footprint": footprint, "plinthDepth": 0, "maxGrade": 0.6, "noAutoPanels": True,
            "materials": [{"name": "wire_post", "albedo": [0.3, 0.24, 0.17], "roughness": 0.9, "pattern": "wood", "metallic": 0, "grain": 1}],
            "parts": parts, "furniture": [], "interactables": [], "panels": [], "model": model,
            "defense": {"kind": "wire", "hp": 200, "smallArmsMultiplier": 0.0, "unsafeVault": True, "maxSlopeDeg": 30,
                        "traversal": traversal, "slowFactor": 0.35, "sections": sections, "sectionHp": 60}}


def wire_volume(x0, x1, y1, z0, z1, n):
    """player-only wire volume split into n stretches along z (matching the model's destruction sections)"""
    out = []
    for s in range(n):
        a, b = z0 + (z1 - z0) * s / n, z0 + (z1 - z0) * (s + 1) / n
        p = box([(x0 + x1) / 2, y1 / 2, (a + b) / 2], [x1 - x0, y1, b - a], "wire_post", k=2, v=0); p["sec"] = s
        out.append(p)
    return out


# 1. concertina coil laid on the ground (Helix045): 3.9 m x 0.65 m, 0.86 m high
save(wire_asset("wire_concertina", "Barbed wire: concertina coil (3.9 m)",
                "A single concertina coil of barbed wire laid along the ground, 0.86 m high. Blocks the way on foot (cannot be vaulted); rounds pass between the loops; blasts cut it stretch by stretch.",
                wire_model(["Helix045"], "wire_concertina", 0.05, 3), wire_volume(-0.33, 0.33, 0.86, -1.94, 1.94, 3), [0.7, 3.9], 3))


def sec_of(z, z0, z1, n): return max(0, min(n - 1, int((z - z0) / (z1 - z0) * n)))


# 2. barbed-wire post fence (Capsule004 + Capsule003): 4.7 m, posts 1.06 m. The source models it as two runs that
#    do not meet; the first run's islands slide +0.33 m at bake so its strands reach the next post (no geometry added).
FZ = 2.339
fence_parts = []
for x, z in ((0.038, -1.19), (-0.029, -0.06), (-0.029, 1.139), (-0.029, 2.266)):
    p = box([x, 0.53, z], [0.08, 1.06, 0.08], "wire_post", v=0); p["sec"] = sec_of(z, -FZ, FZ, 4); fence_parts.append(p)
fence_parts += wire_volume(-0.12, 0.12, 1.0, -FZ, FZ, 4)
save(wire_asset("wire_fence", "Barbed wire: post fence (4.7 m)",
                "Four timber posts carrying three strands of barbed wire, about 1 m high. Cannot be vaulted or walked through; rounds pass between the strands and hit the posts; blasts cut it stretch by stretch.",
                wire_model(["Capsule004", "Capsule003"], "wire_fence", 0.025, 4,
                           {"meshEdits": [{"islandsIn": [[-0.2, -0.2, -2.8], [0.2, 1.3, -0.3]], "translate": [0, 0, 0.33],
                                           "note": "close the source gap between the two fence runs"}], "offset": [0, 0, -0.165]}),
                fence_parts, [0.4, 4.7], 4))

# 3. knife rest (Box003): two timber X-trestles joined by a ridge beam with a concertina coil inside, 2.3 m x 1.3 m x 1.2 m
rest_parts = []
for z, sgn in ((-1.09, 1), (-0.99, -1), (0.945, 1), (1.05, -1)):
    p = box([0.0, 0.62, z], [1.73, 0.1, 0.1], "wire_post", r=[0, 0, sgn * 43.6], v=0); p["sec"] = 0 if z < 0 else 1; rest_parts.append(p)
p = box([0.002, 0.72, 0.0], [0.1, 0.1, 2.27], "wire_post", v=0); rest_parts.append(p)   # ridge beam: stays with either half
rest_parts += wire_volume(-0.6, 0.6, 1.15, -1.147, 1.147, 2)
save(wire_asset("wire_knife_rest", "Barbed wire: knife rest (2.3 m)",
                "Timber knife rest (cheval de frise): two X-trestles and a ridge beam wrapped in a concertina coil, 1.2 m high. Cannot be vaulted or walked through; blasts cut each half separately.",
                wire_model(["Box003"], "wire_knife_rest", 0.05, 2), rest_parts, [1.3, 2.3], 2))
print("wire ok")


# ---------------------------------------------------------------- path entities (complete update E4)
# Length / width are per placed instance (extra.path, editable in the Selection panel); these are the defaults and
# limits. The surface is drawn procedurally in path coordinates (main shader patterns 12-14), so resizing never
# stretches it, and draped on the ground (src/paths.cpp).
def path_asset(id_, name, desc, kind, w, l, albedo, rough, extra=None):
    p = {"kind": kind, "width": w, "length": l, "minWidth": 0.8, "maxWidth": 8.0, "minLength": 1.0, "maxLength": 60.0,
         "albedo": albedo, "roughness": rough}
    if extra: p.update(extra)
    save({"id": id_, "version": 1, "name": name, "category": "Paths", "description": desc, "floors": 0, "enterable": False,
          "footprint": [w, l], "plinthDepth": 0, "maxGrade": 9.0, "noAutoPanels": True, "materials": [], "parts": [],
          "furniture": [], "interactables": [], "panels": [], "path": p})


path_asset("path_gravel", "Gravel path", "Loose gravel over soil: mixed small stones (1.5-4 cm), soil showing between, ragged edges into the grass. Adjustable length and width; joins end to end.",
           "gravel", 2.0, 8.0, [0.5, 0.48, 0.44], 0.85)
path_asset("path_dirt", "Dirt track", "Compacted farm track: two worn wheel ruts, grass down the centre strip, damp patches, grass creeping in at the edges. Adjustable length and width; joins end to end.",
           "dirt", 3.0, 10.0, [0.3, 0.25, 0.18], 0.9)
path_asset("path_concrete", "Concrete path", "Poured concrete slabs (2 m) with expansion seams, exposed aggregate, chipped seam edges and staining. Blasts break individual slabs. Adjustable length and width; joins end to end.",
           "concrete", 2.0, 8.0, [0.56, 0.55, 0.52], 0.88, {"slab": 2.0, "slabHp": 60})
print("paths ok")


# ---------------------------------------------------------------- 8.8 cm Flak 18 emplacement (complete update E5)
# Supplied model "anti-aircraft_gun_8.8_cm_flak_18(1).glb" (153 named meshes, 7,933 tris) baked with the barrel along
# +z (rot -90 deg about y) on a ground pivot. Its mesh families map onto the station transforms: 01 barrel (recoil),
# 02 / 04 cradle, recuperator, equilibrators (elevation), 03 / 08 top carriage, sights, handwheels, seats, shield
# (traverse), 05 / 06 / 07 cruciform base, outriggers, levelling jacks (fixed). Colliders are invisible boxes.
# Pivots measured on the bake: pedestal axis (0, *, -0.495), trunnions (0, 1.66, -0.45), bore axis y 1.817,
# muzzle z 3.354. Game tuning: 360 deg traverse, -3..+85 deg elevation, ground-target HE / AP only (no air targets,
# no time fuzes), reload 5 s, 1.0 m recoil.
def vb(c, s, r=None):
    p = box(c, s, "steel", r=r, v=0); return p
flak = {"id": "gun_flak18", "version": 1, "name": "8.8 cm Flak 18 (emplacement)", "category": "Ordnance", "faction": "DE",
        "description": "German 8.8 cm Flak 18 on its cruciform platform, dug in as a ground-target emplacement. Crew it from the gunner's position (Aim): 360 deg traverse, -3 to +85 deg elevation, HE or AP (X). It is not moved once emplaced. Values are game tuning.",
        "floors": 0, "enterable": False, "footprint": [5.3, 5.3], "plinthDepth": 0, "maxGrade": 0.5, "noAutoPanels": True,
        "materials": [{"name": "steel", "albedo": [0.25, 0.26, 0.22], "roughness": 0.6, "pattern": "metal", "metallic": 0.5, "grain": 1}],
        "parts": [vb([0, 0.28, -0.495], [0.45, 0.55, 5.2]), vb([0, 0.24, -0.495], [5.2, 0.45, 0.45]), vb([0, 0.75, -0.495], [0.8, 0.6, 0.8])],
        "furniture": [], "interactables": [], "panels": [],
        "model": {"src": "anti-aircraft_gun_8.8_cm_flak_18(1).glb", "dir": "flak18", "pivot": "ground", "scale": 1.0, "rot": [0, -90, 0], "maxTex": 1024,
                  "stationRoles": {"SM_Flak18Gun_01": "recoil", "SM_Flak18Gun_02": "elevation", "SM_Flak18Gun_04": "elevation",
                                   "SM_Flak18Gun_03": "traverse", "SM_Flak18Gun_08": "traverse",
                                   "SM_Flak18Gun_05": "base", "SM_Flak18Gun_06": "base", "SM_Flak18Gun_07": "base"}},
        "station": {"kind": "flak18", "label": "Flak 18", "heName": "8.8 cm Sprgr. (HE)", "atName": "8.8 cm PzGr. (AP)", "fixed": True,
                    "yawPivot": [0, 1.0, -0.495], "elevPivot": [0, 1.66, -0.45], "muzzle": [0, 1.817, 3.354], "optic": [0.42, 1.95, 0.45],
                    "gunner": [-1.3, 0, -1.7], "handler": [0, 0, -3.8], "traverse": [-180, 180], "elevation": [-3, 85],
                    "traverseRate": 20, "elevateRate": 8, "recoil": 1.0, "reloadTime": 5.0, "loaded": 1, "reserve": 8, "ammo": "88mm_he",
                    "reserveAT": 4, "ammoAT": "88mm_ap", "moveHalf": [2.6, 1.2, 2.6], "serviceRadius": 7, "sightFov": 12.0, "muzzleScale": 1.4,
                    "fireSound": "flak_fire", "shellHE": {"velocity": 820, "damage": 150, "radius": 2.8, "crater": 1.15}, "shellAT": {"velocity": 1000},
                    "yawParts": [vb([0, 0.4, 0], [1.6, 0.8, 1.6]), vb([0, 0.75, 0.8], [2.5, 1.45, 0.06])],
                    "elevParts": [vb([0, 0.05, 0.25], [0.55, 0.5, 2.9])],
                    "recoilParts": [vb([0, 0.157, 1.62], [0.26, 0.26, 4.35])]}}
save(flak)
print("flak ok")


# ---------------------------------------------------------------- L3 lights (docs/lights.md): street lamps, lanterns, fires
# Category "Lighting". Each is a "prop" (interior.cpp: breakable, glass panes shot out, tanks crush it) with a "light"
# block (lights.cpp): street lamps light at dusk and night, lanterns in barns are dim by day, fires burn day and night.
# Materials with "emissive" (lamp glass, mantles, flames, embers) glow through the lighting engine (L2), which scales them
# by the light's state. Lantern panes sit in an iron frame: a round in the panes breaks the glass (and puts the light out),
# a round on the post rings off the iron.
L_IRON = {"name": "lamp_iron", "albedo": [0.045, 0.045, 0.043], "roughness": 0.55, "pattern": "metal", "metallic": 0.2, "grain": 1}
L_GREEN = {"name": "lamp_green", "albedo": [0.07, 0.11, 0.08], "roughness": 0.5, "pattern": "metal", "metallic": 0.15, "grain": 1}
L_GLASS = {"name": "lamp_glass", "albedo": [0.82, 0.8, 0.72], "roughness": 0.05, "pattern": "glass", "metallic": 0, "grain": 1, "emissive": [1.6, 1.15, 0.62]}
L_MANTLE = {"name": "gas_mantle", "albedo": [0.9, 0.88, 0.8], "roughness": 0.6, "pattern": "plain", "metallic": 0, "grain": 1, "emissive": [6.0, 5.0, 3.2]}
L_FLAME = {"name": "flame", "albedo": [0.95, 0.62, 0.22], "roughness": 0.9, "pattern": "plain", "metallic": 0, "grain": 1, "emissive": [7.0, 3.4, 0.9]}
L_EMBER = {"name": "ember", "albedo": [0.32, 0.08, 0.02], "roughness": 0.95, "pattern": "plain", "metallic": 0, "grain": 1, "emissive": [3.2, 0.9, 0.18]}
L_ASH = {"name": "ash", "albedo": [0.2, 0.19, 0.18], "roughness": 0.98, "pattern": "plain", "metallic": 0, "grain": 1}
L_BARK = {"name": "bark", "albedo": [0.2, 0.14, 0.09], "roughness": 0.95, "pattern": "wood", "metallic": 0, "grain": 1}
L_TIN = {"name": "lamp_tin", "albedo": [0.3, 0.3, 0.28], "roughness": 0.45, "pattern": "metal", "metallic": 0.6, "grain": 1}


def lpart(c, s, m, g, r=None):
    """visible, no collision, in prop group g"""
    p = box(c, s, m, r=r, k=0); p["g"] = g; return p


def lcol(c, s):
    return box(c, s, "lamp_iron", v=0)


def lcyl(c, d, h, m, g, **kw):
    """P1: visible, no collision cylinder / frustum in prop group g"""
    p = cyl(c, d, h, m, k=0, **kw); p["g"] = g; return p


def octo(parts, c, s, m, g, top=1.0):
    """an octagonal casting (P1: a true 8-sided prism, flats facing the axes; was two crossed squares)"""
    parts.append(lcyl(c, s[0], s[1], m, g, dz=s[2], n=8, top=top))


def lantern4(parts, cx, cy, cz, w, h, frame, g_frame, g_glass, flame="flame", fh=0.04):
    """four-paned lantern centred at (cx, cy, cz): glass box, round corner posts, a moulded base, a pyramid hood with
    a vented chimney and ball finial, a burner and a flame or mantle"""
    parts.append(lpart([cx, cy, cz], [w * 0.9, h, w * 0.9], "lamp_glass", g_glass))
    for sx in (-1, 1):
        for sz in (-1, 1):
            parts.append(lcyl([cx + sx * w * 0.47, cy, cz + sz * w * 0.47], 0.02, h + 0.02, frame, g_frame, seg=6))
    parts.append(bevel(lpart([cx, cy - h / 2 - 0.02, cz], [w, 0.04, w], frame, g_frame), 0.008))
    parts.append(lcyl([cx, cy - h / 2 - 0.055, cz], w * 0.7, 0.03, frame, g_frame, n=4, top=0.6, r=[180, 0, 0]))   # tapering base
    parts.append(bevel(lpart([cx, cy + h / 2 + 0.012, cz], [w * 1.12, 0.024, w * 1.12], frame, g_frame), 0.006))     # hood rim
    parts.append(lcyl([cx, cy + h / 2 + 0.06, cz], w * 1.1, 0.075, frame, g_frame, n=4, top=0.45, t="k"))  # pyramid hood
    parts.append(lcyl([cx, cy + h / 2 + 0.115, cz], w * 0.32, 0.04, frame, g_frame, seg=10))                            # vent chimney
    parts.append(lcyl([cx, cy + h / 2 + 0.14, cz], w * 0.45, 0.012, frame, g_frame, seg=10))                            # vent cap
    parts.append(lpart([cx, cy + h / 2 + 0.165, cz], [0.035, 0.035, 0.035], frame, g_frame)); parts[-1]["t"] = "r"; parts[-1]["rad"] = 0.017
    parts.append(lcyl([cx, cy - h / 2 + 0.03, cz], w * 0.3, 0.05, frame, g_frame, top=0.7, seg=10))  # burner
    parts.append(lpart([cx, cy - h * 0.12, cz], [0.014 if flame == "flame" else 0.05, fh, 0.008 if flame == "flame" else 0.05], flame, g_glass))


def light_asset(id_, name, desc, parts, mats, footprint, groups, prop, light):
    p = {"family": prop.get("family", "metal"), "rounds": prop.get("rounds", 0), "blast": prop.get("blast", 120), "blocking": prop.get("blocking", True),
         "wall": prop.get("wall", False), "front": prop.get("front", 0.0)}
    for k in ("mount", "hang"):
        if k in prop: p[k] = prop[k]
    p["groups"] = groups
    p["fragments"] = [groups.index(g) for g in prop.get("fragments", [])]
    p["keep"] = [groups.index(g) for g in prop.get("keep", [])]
    if "glass" in prop: p["glass"] = groups.index(prop["glass"])
    p["surfaces"] = []
    if id_ in ENVELOPES: p["bounds"] = ENVELOPES[id_]
    lt = dict(light)
    if isinstance(lt.get("group"), str): lt["group"] = groups.index(lt["group"])
    save({"id": id_, "version": 2, "name": name, "category": "Lighting", "description": desc, "floors": 0, "enterable": False,
          "footprint": footprint, "plinthDepth": 0, "maxGrade": 0.8, "noAutoPanels": True, "materials": mats, "parts": parts,
          "furniture": [], "interactables": [], "panels": [], "prop": p, "light": lt})


# gas street lamp (candelabre): cast-iron fluted column on an octagonal plinth, ladder bar, four-paned lantern, gas mantle
G = ["post", "lantern", "glass"]
parts = []
octo(parts, [0, 0.15, 0], [0.36, 0.3, 0.36], "lamp_green", 0)
parts.append(lcyl([0, 0.32, 0], 0.38, 0.04, "lamp_green", 0, n=8, top=0.75))      # P1: moulded plinth cap
octo(parts, [0, 0.42, 0], [0.24, 0.24, 0.24], "lamp_green", 0, top=0.8)
for i in range(5):   # P1: the fluted column: a 16-sided tapering shaft in drums, a collar ring at each joint
    y0, y1 = 0.54 + i * 0.45, 0.54 + (i + 1) * 0.45
    r = 0.085 - 0.007 * i
    parts.append(lcyl([0, (y0 + y1) / 2, 0], 2 * r, y1 - y0 + 0.01, "lamp_green", 0, n=16, top=(r - 0.007) / r))
    parts.append(lcyl([0, y0, 0], 2 * r + 0.025, 0.03, "lamp_green", 0, seg=16))
octo(parts, [0, 2.8, 0], [0.16, 0.06, 0.16], "lamp_green", 0)
parts.append(lcyl([0, 2.86, 0], 0.035, 0.7, "lamp_green", 0, seg=8, r=[0, 0, 90]))   # ladder bar
for sx in (-1, 1):
    parts.append(lpart([sx * 0.34, 2.9, 0], [0.04, 0.06, 0.04], "lamp_green", 0))
    parts.append(lpart([sx * 0.34, 2.94, 0], [0.05, 0.05, 0.05], "lamp_green", 0)); parts[-1]["t"] = "r"; parts[-1]["rad"] = 0.024   # ball ends
    parts.append(arch([sx * 0.17, 2.79, 0], [0.3, 0.1, 0.02], "lamp_green", th=0.02, r=[180, 0, 0])); parts[-1]["g"] = 0          # scroll under the bar
parts.append(lcyl([0, 2.94, 0], 0.13, 0.08, "lamp_green", 1, top=1.0, seg=12))
lantern4(parts, 0, 3.2, 0, 0.32, 0.42, "lamp_green", 1, 2, flame="gas_mantle", fh=0.08)
parts.append(lcyl([0, 3.62, 0], 0.03, 0.1, "lamp_green", 1, top=0.15, seg=8, t="k"))
parts += [lcol([0, 1.45, 0], [0.22, 2.9, 0.22]), lcol([0, 3.22, 0], [0.34, 0.58, 0.34])]
light_asset("lamp_gas_street", "Gas street lamp", "Cast-iron gas street lamp of a Norman town square: fluted column, ladder bar for the lamplighter, four-paned lantern with a mantle (~3.6 m). Lit at dusk and night; shoot the panes or blast it to put it out.",
            parts, [L_GREEN, L_GLASS, L_MANTLE], [0.5, 0.5], G,
            {"family": "metal", "blast": 140, "fragments": ["lantern"], "keep": ["post"], "glass": "glass"},
            {"color": [1.0, 0.84, 0.58], "intensity": 3.0, "radius": 14.0, "offset": [0, 3.18, 0], "shadow": 1, "group": "glass", "use": "street"})

# iron lantern post: a wrought-iron post with a scrolled arm, a lantern hanging over the lane (farm lanes, church paths)
parts = []
parts.append(lcyl([0, 0.03, 0], 0.22, 0.06, "lamp_iron", 0, n=8, top=0.7))                 # P1: cast foot
parts.append(lcyl([0, 0.09, 0], 0.11, 0.06, "lamp_iron", 0, seg=12, top=0.75))
parts.append(lpart([0, 1.33, 0], [0.07, 2.6, 0.07], "lamp_iron", 0)); bevel(parts[-1], 0.01)  # square wrought post
for y in (0.6, 2.3):
    parts.append(bevel(lpart([0, y, 0], [0.085, 0.03, 0.085], "lamp_iron", 0), 0.006))       # collars
parts.append(lcyl([0, 2.68, 0], 0.05, 0.1, "lamp_iron", 0, seg=8, top=0.6))
parts.append(lpart([0, 2.745, 0], [0.045, 0.045, 0.045], "lamp_iron", 0)); parts[-1]["t"] = "r"; parts[-1]["rad"] = 0.022   # ball finial
parts.append(lcyl([0, 2.6, 0.3], 0.03, 0.62, "lamp_iron", 0, seg=8, r=[90, 0, 0]))           # arm
parts.append(lpart([0, 2.42, 0.13], [0.025, 0.42, 0.025], "lamp_iron", 0, r=[-40, 0, 0]))
parts.append(arch([0, 2.44, 0.2], [0.2, 0.15, 0.016], "lamp_iron", th=0.014, r=[0, 90, 0])); parts[-1]["g"] = 0   # scroll
parts.append(arch([0, 2.5, 0.42], [0.1, 0.09, 0.016], "lamp_iron", th=0.012, r=[180, 90, 0])); parts[-1]["g"] = 0
parts.append(lpart([0, 2.55, 0.55], [0.012, 0.08, 0.012], "lamp_iron", 1))
lantern4(parts, 0, 2.33, 0.55, 0.18, 0.26, "lamp_iron", 1, 2)
parts += [lcol([0, 1.35, 0], [0.12, 2.7, 0.12]), lcol([0, 2.36, 0.55], [0.22, 0.42, 0.22])]
light_asset("lamp_lantern_post", "Iron lantern post", "Wrought-iron post with a scrolled arm and a paraffin lantern hanging over the lane (~2.7 m): farmyards, church paths, the auto-path lamp posts. Lit at dusk and night.",
            parts, [L_IRON, L_GLASS, L_FLAME], [0.4, 0.8], G,
            {"family": "metal", "blast": 110, "fragments": ["lantern"], "keep": ["post"], "glass": "glass"},
            {"color": [1.0, 0.74, 0.42], "intensity": 2.0, "radius": 11.0, "offset": [0, 2.33, 0.55], "flicker": 0.08, "shadow": 1, "group": "glass", "use": "street"})

# wall lantern: a bracket on a facade, the lantern 0.3 m off the wall (wall-hung: "mount" height above the ground or floor)
G = ["bracket", "lantern", "glass"]
parts = []
parts.append(bevel(lpart([0, 0.3, 0.012], [0.12, 0.24, 0.024], "lamp_iron", 0), 0.006))
for y in (0.21, 0.39):   # P1: bolt heads on the wall plate
    parts.append(lcyl([0, y, 0.026], 0.02, 0.008, "lamp_iron", 0, seg=6, top=0.6, r=[90, 0, 0]))
parts.append(lcyl([0, 0.43, 0.17], 0.025, 0.32, "lamp_iron", 0, seg=8, r=[90, 0, 0]))
parts.append(lpart([0, 0.3, 0.11], [0.02, 0.3, 0.02], "lamp_iron", 0, r=[50, 0, 0]))
parts.append(arch([0, 0.33, 0.13], [0.16, 0.09, 0.014], "lamp_iron", th=0.012, r=[0, 90, 0])); parts[-1]["g"] = 0   # scroll
parts.append(lpart([0, 0.4, 0.3], [0.012, 0.04, 0.012], "lamp_iron", 1))
lantern4(parts, 0, 0.2, 0.3, 0.17, 0.24, "lamp_iron", 1, 2)
parts.append(lcol([0, 0.24, 0.19], [0.22, 0.44, 0.34]))
light_asset("lamp_wall_lantern", "Wall lantern", "Iron wall lantern on a scrolled bracket, hung beside a door or on a farmhouse gable (wall-hung, 2 m up). Lit at dusk and night.",
            parts, [L_IRON, L_GLASS, L_FLAME], [0.24, 0.4], G,
            {"family": "metal", "blast": 60, "mount": 2.0, "wall": True, "blocking": False, "fragments": ["lantern"], "keep": ["bracket"], "glass": "glass"},
            {"color": [1.0, 0.74, 0.42], "intensity": 1.4, "radius": 9.0, "offset": [0, 0.19, 0.3], "flicker": 0.08, "shadow": 1, "group": "glass", "use": "street"})

# hanging storm lantern: hung by its top from a beam, a porch or a barn ceiling (origin at the hook)
G = ["hook", "lantern", "glass"]
parts = []
parts.append(arch([0, -0.04, 0], [0.04, 0.04, 0.008], "lamp_iron", th=0.008)); parts[-1]["g"] = 0          # P1: the hook
parts.append(lcyl([0, -0.1, 0], 0.01, 0.13, "lamp_iron", 0, seg=6))
parts.append(arch([0, -0.2, 0], [0.2, 0.06, 0.008], "lamp_iron", th=0.006)); parts[-1]["g"] = 1           # wire bail
parts.append(lcyl([0, -0.185, 0], 0.16, 0.035, "lamp_tin", 1, top=0.55, seg=16))                           # top dome
parts.append(lcyl([0, -0.15, 0], 0.07, 0.05, "lamp_tin", 1, top=0.8, seg=12))                              # vent cap
parts.append(lcyl([0, -0.125, 0], 0.09, 0.008, "lamp_tin", 1, seg=12))
parts.append(lcyl([0, -0.36, 0], 0.13, 0.12, "lamp_glass", 2, top=0.72, seg=16, r=[180, 0, 0]))          # the globe: bulging
parts.append(lcyl([0, -0.25, 0], 0.13, 0.1, "lamp_glass", 2, top=0.62, seg=16))
for sx in (-1, 1):   # side air tubes and the wire guard
    parts.append(lcyl([sx * 0.095, -0.32, 0], 0.016, 0.3, "lamp_tin", 1, seg=8))
for a in (0, 90, 180, 270):
    parts.append(lpart([math.sin(math.radians(a)) * 0.072, -0.31, math.cos(math.radians(a)) * 0.072], [0.006, 0.2, 0.006], "lamp_iron", 1))
parts.append(lcyl([0, -0.42, 0], 0.16, 0.01, "lamp_iron", 1, seg=16))
parts.append(lcyl([0, -0.46, 0], 0.17, 0.07, "lamp_tin", 1, top=0.88, seg=16))                             # fount
parts.append(lcyl([0, -0.497, 0], 0.15, 0.006, "lamp_tin", 1, seg=16))
parts.append(lcyl([0.06, -0.42, 0.05], 0.02, 0.03, "lamp_tin", 1, seg=8))                                   # wick knob
parts.append(lpart([0, -0.36, 0], [0.012, 0.035, 0.006], "flame", 2))
parts.append(lcol([0, -0.32, 0], [0.2, 0.32, 0.2]))
light_asset("lamp_hanging_lantern", "Hanging lantern", "Paraffin storm lantern hung from a hook: porches, barn beams, workshops and stables. Hangs by its top from a ceiling or beam; dim by day, full at dusk and night.",
            parts, [L_IRON, L_TIN, L_GLASS, L_FLAME], [0.2, 0.2], G,
            {"family": "metal", "blast": 40, "hang": True, "blocking": False, "fragments": ["lantern"], "keep": ["hook"], "glass": "glass"},
            {"color": [1.0, 0.7, 0.4], "intensity": 1.0, "radius": 7.0, "offset": [0, -0.33, 0], "flicker": 0.15, "shadow": 1, "group": "glass", "use": "interior"})

# brazier: an iron fire basket on three legs, coals glowing, flames and smoke (sentry posts, gun pits, bivouacs)
G = ["frame", "fire"]
parts = []
for k in range(3):   # P1: round bar legs with flattened feet
    a = math.radians(k * 120)
    parts.append(lcyl([math.sin(a) * 0.2, 0.3, math.cos(a) * 0.2], 0.03, 0.62, "lamp_iron", 0, seg=8, r=[-12 * math.cos(a), 0, 12 * math.sin(a)]))
    parts.append(lcyl([math.sin(a) * 0.265, 0.008, math.cos(a) * 0.265], 0.07, 0.016, "lamp_iron", 0, seg=8))
parts.append(lcyl([0, 0.66, 0], 0.46, 0.04, "lamp_iron", 0, seg=20))   # dished bottom plate
parts.append(lcyl([0, 0.63, 0], 0.46, 0.03, "lamp_iron", 0, top=0.6, seg=20, r=[180, 0, 0]))
for k in range(12):   # basket bars, flaring a little outward
    a = math.radians(k * 30)
    parts.append(lcyl([math.sin(a) * 0.245, 0.76, math.cos(a) * 0.245], 0.018, 0.2, "lamp_iron", 0, seg=6, r=[-5 * math.cos(a), 0, 5 * math.sin(a)]))
for y, d in ((0.77, 0.5), (0.86, 0.53)):   # open band rings
    for p_ in ring([0, y, 0], d, 0.02, 0.02, "lamp_iron"):
        p_["g"] = 0; parts.append(p_)
parts.append(lcyl([0, 0.72, 0], 0.44, 0.1, "ember", 1, top=0.7, seg=16))                                    # heaped coals
for x, z in ((-0.1, 0.05), (0.08, -0.06), (0.02, 0.1)):
    parts.append(bevel(lpart([x, 0.78, z], [0.09, 0.06, 0.08], "ember", 1, r=[0, x * 300, 10]), 0.02))  # lumps of coal
for x, z, h, rot in ((-0.08, 0.02, 0.28, 10), (0.06, -0.05, 0.34, -20), (0.02, 0.08, 0.22, 60)):
    parts.append(lpart([x, 0.76 + h / 2, z], [0.1, h, 0.012], "flame", 1, r=[0, rot, 0]))
parts.append(lcol([0, 0.45, 0], [0.52, 0.9, 0.52]))
light_asset("fire_brazier", "Brazier", "Iron fire basket on three legs with glowing coals, flames and a little smoke: sentry posts, gun pits and bivouacs. Burns day and night; a blast scatters it.",
            parts, [L_IRON, L_EMBER, L_FLAME], [0.6, 0.6], G,
            {"family": "metal", "blast": 120, "fragments": ["frame"], "keep": []},
            {"color": [1.0, 0.5, 0.2], "intensity": 2.5, "radius": 9.0, "offset": [0, 1.0, 0], "flicker": 0.75, "shadow": 1, "group": "fire", "use": "fire", "fx": "flame_smoke", "fxSize": 0.26})

# campfire: a ring of field stones, a teepee of split logs over embers and ash
stone = {"name": "field_stone", "albedo": [0.42, 0.40, 0.36], "roughness": 0.92, "pattern": "stone", "metallic": 0, "grain": 1}
G = ["ring", "logs", "fire"]
rng = random.Random(131)
parts = []
for k in range(9):
    a = 2 * math.pi * k / 9 + rng.uniform(-0.1, 0.1)
    parts.append(bevel(lpart([math.sin(a) * 0.5, 0.07, math.cos(a) * 0.5], [0.2 + rng.uniform(-0.03, 0.04), 0.14 + rng.uniform(-0.02, 0.03), 0.16], "field_stone", 0, r=[0, math.degrees(a) + rng.uniform(-15, 15), rng.uniform(-8, 8)]), 0.045))   # P1: rounded field stones
parts.append(lcyl([0, 0.012, 0], 0.8, 0.024, "ash", 0, top=0.8, seg=20))
parts.append(lcyl([0, 0.03, 0], 0.5, 0.04, "ember", 2, top=0.6, seg=16))
for k in range(5):   # P1: round logs, charred ends toward the fire
    a = 2 * math.pi * k / 5 + 0.3
    parts.append(lcyl([math.sin(a) * 0.14, 0.2, math.cos(a) * 0.14], 0.075, 0.48, "bark", 1, top=0.75, seg=8, r=[-28 * math.cos(a), 0, 28 * math.sin(a)]))
    parts.append(lcyl([math.sin(a) * 0.035, 0.42, math.cos(a) * 0.035], 0.045, 0.05, "ember", 2, seg=8, r=[-28 * math.cos(a), 0, 28 * math.sin(a)]))
for x, z, h, rot in ((-0.05, 0.03, 0.42, 15), (0.06, -0.04, 0.5, -25), (0.0, 0.07, 0.34, 70), (0.04, 0.0, 0.28, 120)):
    parts.append(lpart([x, 0.05 + h / 2, z], [0.12, h, 0.012], "flame", 2, r=[0, rot, 0]))
light_asset("fire_campfire", "Campfire", "A camp fire in a ring of field stones: a teepee of split logs over embers and ash, flames and smoke. Burns day and night (cosmetic: walk round it).",
            parts, [stone, L_ASH, L_EMBER, L_BARK, L_FLAME], [1.2, 1.2], G,
            {"family": "masonry", "blast": 300, "blocking": False, "keep": ["ring", "logs"]},
            {"color": [1.0, 0.5, 0.18], "intensity": 3.0, "radius": 10.0, "offset": [0, 0.5, 0], "flicker": 0.85, "shadow": 1, "group": "fire", "use": "fire", "fx": "flame_smoke", "fxSize": 0.36})
print("lights ok")
