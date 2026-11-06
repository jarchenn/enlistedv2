"""Generates the Vegetation (trees) and Defences (tank trap, sandbags) assets into content/assets/.

Run from the repository root:  python tools/gen_env_assets.py
All geometry is procedural box parts in the engine's asset format (metres, y up). Deterministic (fixed seeds).
"""
import json, math, random, os

OUT = os.path.join(os.path.dirname(__file__), '..', 'content', 'assets')


def box(c, s, m, r=None, k=1, v=1):
    p = {"t": "b", "c": [round(x, 4) for x in c], "s": [round(x, 4) for x in s], "m": m}
    if r: p["r"] = [round(x, 2) for x in r]
    if not k: p["k"] = 0
    if not v: p["v"] = 0
    return p


def save(a):
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


rng = random.Random(7)
apple_crown = [(0, 3.3, 0, 1.5)] + crown_ring(5, 2.9, 1.2, 1.15, rng)
save(tree("tree_apple", "Apple tree (orchard)", "Low Normandy cider-orchard apple: short bole, wide rounded crown (~4.5 m).",
          1.5, 0.14, 0.11, apple_crown, [(30, 40, 1.4, 0.07), (150, 35, 1.3, 0.065), (260, 45, 1.2, 0.06)],
          [0.20, 0.30, 0.10], 0.30, 70, 11, 4.5))
rng = random.Random(9)
oak_crown = [(0, 8.2, 0, 3.0)] + crown_ring(6, 7.2, 2.6, 2.4, rng, 0.6) + crown_ring(4, 9.2, 1.6, 2.0, rng, 0.4)
save(tree("tree_oak", "Hedgerow oak", "Pedunculate oak of the bocage hedgerows: thick bole, broad crown (~11 m).",
          4.0, 0.38, 0.28, oak_crown, [(20, 35, 3.0, 0.16), (140, 30, 3.2, 0.15), (250, 40, 2.8, 0.14), (320, 55, 2.6, 0.12)],
          [0.16, 0.25, 0.08], 0.42, 80, 13, 11.0))
rng = random.Random(5)
poplar_crown = [(rng.uniform(-0.2, 0.2), y, rng.uniform(-0.2, 0.2), 1.35 - abs(y - 9) * 0.06) for y in [4.0, 6.0, 8.0, 10.0, 12.0, 14.0]]
save(tree("tree_poplar", "Lombardy poplar", "Tall columnar poplar used as a windbreak along lanes (~16 m).",
          13.0, 0.26, 0.12, poplar_crown, [(0, 75, 2.0, 0.07), (120, 70, 2.2, 0.07), (240, 72, 2.0, 0.07)],
          [0.22, 0.31, 0.12], 0.28, 55, 17, 16.0))

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
k3 = 1 / math.sqrt(3)
stand = qmul(qaxis([0, 1, 0], 15), qfromto([k3, k3, k3], [0, 1, 0]))
parts = []
L, w, t = 1.4, 0.1, 0.012
cy = L / 2 * k3 + 0.03
for i, base in enumerate([[0, 0, 0, 1], qaxis([0, 0, 1], 90), qaxis([0, 1, 0], 90)]):
    q = qmul(stand, base)
    for sz in ([L, w, t], [L, t, w]):   # angle iron: two flanges
        p_ = box([0, cy, 0], sz, "steel" if i % 2 == 0 else "rust"); p_["q"] = [round(x, 5) for x in q]; parts.append(p_)
parts.append(box([0, cy, 0], [0.16, 0.16, 0.16], "rust"))   # gusset plate block at the crossing
parts.append(box([0, cy, 0], [1.1, 2 * cy, 1.1], "steel", k=1, v=0))  # collision hull (stops vehicles, cover for men)
save({"id": "def_tank_trap", "version": 1, "name": "Tank trap (Czech hedgehog)", "category": "Defences",
      "description": "Welded steel hedgehog, three 1.4 m angle-iron beams. Immune to small arms; only HE / AT damage it.",
      "floors": 0, "enterable": False, "footprint": [1.4, 1.4], "plinthDepth": 0, "maxGrade": 0.6,
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
                if curved_r is None:
                    parts.append(box([s, y, z], [bl * 0.97, bh, bd * 0.95 - inset], "hessian" if (i + c + row) % 3 else "hessian_dk", r=[0, jit, rng.uniform(-2, 2)], k=0))
                else:
                    a = s / curved_r   # arc parameter, centred on the wall middle
                    R = curved_r + z
                    parts.append(box([math.sin(a) * R, y, math.cos(a) * R - curved_r], [bl * 0.97, bh, bd * 0.95 - inset], "hessian" if (i + c + row) % 3 else "hessian_dk",
                                     r=[0, math.degrees(a) + jit, rng.uniform(-2, 2)], k=0))
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
save({"id": "def_sandbag_straight", "version": 1, "name": "Sandbag wall (straight)", "category": "Defences",
      "description": "2 m run of filled sandbags, five courses (~0.8 m), two bags deep. Stops small-arms fire; shelters from blast.",
      "floors": 0, "enterable": False, "footprint": [2.0, 0.6], "plinthDepth": 0, "maxGrade": 0.6,
      "materials": [bag, bag2], "parts": p1, "furniture": [], "interactables": [], "panels": [],
      "defense": {"kind": "sandbag", "hp": 300, "stopsBullets": True, "blastOcclusion": 0.1, "maxSlopeDeg": 35, "sockets": s1}})
arc = 2.5 * math.radians(45)
p2, h2, s2 = sandbag_wall(arc, curved_r=2.5)
save({"id": "def_sandbag_curved", "version": 1, "name": "Sandbag wall (curved 45 deg)", "category": "Defences",
      "description": "45 degree arc of sandbags on a 2.5 m radius; chains with straight runs into fighting positions.",
      "floors": 0, "enterable": False, "footprint": [2.0, 0.9], "plinthDepth": 0, "maxGrade": 0.6,
      "materials": [bag, bag2], "parts": p2, "furniture": [], "interactables": [], "panels": [],
      "defense": {"kind": "sandbag", "hp": 300, "stopsBullets": True, "blastOcclusion": 0.1, "maxSlopeDeg": 35, "sockets": s2}})
print("ok")
