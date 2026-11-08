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


def box(c, s, m, r=None, k=1, v=1):
    p = {"t": "b", "c": [round(x, 4) for x in c], "s": [round(x, 4) for x in s], "m": m}
    if r: p["r"] = [round(x, 2) for x in r]
    if k != 1: p["k"] = k
    if not v: p["v"] = 0
    return p


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

save(branch_tree("tree_apple", "Apple tree (orchard)", "Low Normandy cider-orchard apple: short leaning bole, spreading limbs, clustered crown (~4.5 m).",
                 11, 1.4, 0.14, 4, 1.5, 38, 2, 0.9, 0.95, [0.17, 0.25, 0.09], 0.19, 4.5, lean=(0.05, 0.02), extra_inner=3, density=62))
save(branch_tree("tree_oak", "Hedgerow oak", "Pedunculate oak of the bocage: thick bole, wide irregular crown of many leaf clusters (~11 m).",
                 13, 3.6, 0.38, 6, 3.2, 28, 3, 2.0, 1.6, [0.13, 0.2, 0.07], 0.22, 11.0, lean=(0.02, -0.03), extra_inner=6, density=58))
save(branch_tree("tree_poplar", "Lombardy poplar", "Tall columnar poplar used as a windbreak along lanes (~16 m).",
                 17, 12.0, 0.26, 8, 2.4, 74, 2, 1.4, 1.1, [0.19, 0.27, 0.1], 0.17, 16.0, columnar=True, extra_inner=8, density=56, mat="bark_pale"))
save(branch_tree("tree_ash", "Ash tree", "Young field ash: slender grey bole, open upswept crown (~8 m).",
                 23, 2.8, 0.2, 5, 2.2, 45, 2, 1.4, 1.15, [0.18, 0.26, 0.09], 0.19, 8.0, lean=(-0.03, 0.03), extra_inner=3, density=52, mat="bark_pale"))

# ---- shrubs and hedges (bocage): woody stems under layered foliage; hedgerows sit on an earth bank
def shrub(id_, name, desc, seed, spheres, stems, leaf_col, leaf_size, density, hp=15, bank=None, hedge=False):
    rng = random.Random(seed)
    parts = []
    for (x, z, h) in stems:
        limb(parts, [x, 0.0, z], h, rng.uniform(0, 360), rng.uniform(62, 85), 0.025, "bark")
    soil = {"name": "bank_soil", "albedo": [0.19, 0.21, 0.11], "roughness": 0.97, "pattern": "plain", "metallic": 0, "grain": 1}   # turf-covered bank
    if bank:
        L, W, H = bank
        parts.append(box([0, H * 0.5, 0], [L, H, W * 0.55], "bank_soil"))
        parts.append(box([0, H * 0.25, W * 0.33], [L, H * 0.5, W * 0.3], "bank_soil", r=[-25, 0, 0]))
        parts.append(box([0, H * 0.25, -W * 0.33], [L, H * 0.5, W * 0.3], "bank_soil", r=[25, 0, 0]))
    t = {"hpBullet": 99999 if hedge else hp, "hpAT": 99999 if hedge else 1, "stumpHeight": 0.15, "trunkRadius": 0.05, "mass": 30,
         "canopy": [[round(a, 3) for a in c] for c in spheres], "cardsPerSphere": density, "cardDensity": density, "leafSize": leaf_size, "leafColor": leaf_col, "seed": seed,
         "hedge": hedge}
    fp = [bank[0], bank[1]] if bank else [1.5, 1.5]
    save({"id": id_, "version": 1, "name": name, "category": "Vegetation", "description": desc, "floors": 0, "enterable": False,
          "footprint": fp, "plinthDepth": 0, "maxGrade": 1.5,
          "materials": [{"name": "bark", "albedo": [0.17, 0.14, 0.11], "roughness": 0.95, "pattern": "wood", "metallic": 0, "grain": 1}, soil],
          "parts": parts, "furniture": [], "interactables": [], "panels": [], "tree": t})

rng = random.Random(41)
hedge_sph = []
for k in range(9):
    x = -3.6 + k * 0.9 + rng.uniform(-0.2, 0.2)
    hedge_sph.append((x, 1.15 + rng.uniform(-0.1, 0.25), rng.uniform(-0.15, 0.15), 0.75 + rng.uniform(-0.1, 0.2)))
    hedge_sph.append((x + 0.45, 2.0 + rng.uniform(-0.25, 0.35), rng.uniform(-0.2, 0.2), 0.6 + rng.uniform(-0.1, 0.15)))
stems = [(-3.8 + k * 0.5 + rng.uniform(-0.15, 0.15), rng.uniform(-0.2, 0.2), rng.uniform(1.4, 2.4)) for k in range(16)]
shrub("hedgerow_bocage", "Bocage hedgerow (8 m)", "Earth bank topped by a dense hazel / hawthorn hedge with woody stems: blocks sight, gaps only where you leave them.",
      43, hedge_sph, [(x, z, h) for (x, z, h) in stems], [0.14, 0.21, 0.075], 0.34, 70, bank=(8.0, 1.5, 0.5), hedge=True)
shrub("shrub_hazel", "Hazel shrub", "Multi-stemmed hazel clump (~2.5 m).", 47,
      [(0, 1.5, 0, 0.9), (0.5, 1.0, 0.3, 0.7), (-0.4, 1.1, -0.3, 0.7), (0.1, 2.1, -0.1, 0.6)],
      [(rng.uniform(-0.3, 0.3), rng.uniform(-0.3, 0.3), rng.uniform(1.6, 2.4)) for _ in range(7)], [0.16, 0.24, 0.085], 0.3, 70)
shrub("shrub_bramble", "Bramble patch", "Low tangled bramble (~0.9 m) for fence lines and ditch edges.", 53,
      [(0, 0.45, 0, 0.6), (0.7, 0.35, 0.2, 0.5), (-0.6, 0.4, -0.2, 0.55), (0.2, 0.55, -0.5, 0.45)],
      [], [0.12, 0.16, 0.07], 0.32, 80, hp=6)

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
# Complete update E2: the hedgehog is 1.25x its first version in every linear dimension (beams 1.4 -> 1.75 m,
# flanges 100 -> 125 mm, crossing 0.43 -> 0.54 m above the ground, overall ~1.1 m high). The size lives only in the
# asset (instances have no scale of their own), so maps saved with version 1 adopt it on load with no double scaling.
TT = 1.25
L, w, t = 1.4 * TT, 0.1 * TT, 0.012 * TT
cy = L / 2 * k3 + 0.03 * TT
for i, base in enumerate([[0, 0, 0, 1], qaxis([0, 0, 1], 90), qaxis([0, 1, 0], 90)]):
    q = qmul(stand, base)
    for sz in ([L, w, t], [L, t, w]):   # angle iron: two flanges
        p_ = box([0, cy, 0], sz, "steel" if i % 2 == 0 else "rust"); p_["q"] = [round(x, 5) for x in q]; parts.append(p_)
parts.append(box([0, cy, 0], [0.16 * TT, 0.16 * TT, 0.16 * TT], "rust"))   # gusset plate block at the crossing
parts.append(box([0, cy, 0], [1.1 * TT, 2 * cy, 1.1 * TT], "steel", k=1, v=0))  # collision hull (stops vehicles, cover for men)
save({"id": "def_tank_trap", "version": 2, "name": "Tank trap (Czech hedgehog)", "category": "Defences",
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
    for y, z in ((0.82, 0.22), (0.82, -0.22)):   # side hooks (launcher support) outside the +x frame
        parts.append(box([half + 0.09, y, z], [0.16, 0.035, 0.035], "rack_iron", k=0))
        parts.append(box([half + 0.165, y + 0.035, z], [0.02, 0.07, 0.035], "rack_iron", k=0))
    # nails / weathering details at the joints
    for x in (-half, half):
        for y in (0.36, 1.02):
            parts.append(box([x + (0.028 if x > 0 else -0.028), y, 0.2], [0.006, 0.012, 0.012], "rack_iron", k=0))
    parts.append(box([0, 0.65, 0.0], [2 * half + 0.1, 1.3, 0.95], "rack_wood", v=0))   # collision hull: simple and snag-free
    save({"id": id_, "version": 1, "name": name, "category": "Supply", "description": desc, "floors": 0, "enterable": False,
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
def dressing(id_, name, desc, parts, mats, footprint):
    save({"id": id_, "version": 1, "name": name, "category": "Dressing", "description": desc, "floors": 0, "enterable": False,
          "footprint": footprint, "plinthDepth": 0, "maxGrade": 0.8, "materials": mats, "parts": parts, "furniture": [], "interactables": [], "panels": []})

post = {"name": "post_wood", "albedo": [0.27, 0.22, 0.16], "roughness": 0.92, "pattern": "wood", "metallic": 0, "grain": 1}
wire = {"name": "wire", "albedo": [0.30, 0.29, 0.27], "roughness": 0.6, "pattern": "metal", "metallic": 0.6, "grain": 1}
stone = {"name": "field_stone", "albedo": [0.42, 0.40, 0.36], "roughness": 0.92, "pattern": "stone", "metallic": 0, "grain": 1}
rng = random.Random(61)
parts = []
for k in range(4):   # 8 m run: posts every 2.4 m, three strands of wire
    x = -3.6 + k * 2.4
    parts.append(box([x, 0.6, 0], [0.1, 1.2 + rng.uniform(-0.06, 0.06), 0.1], "post_wood", r=[rng.uniform(-3, 3), rng.uniform(0, 40), rng.uniform(-3, 3)]))
for y in (0.45, 0.78, 1.08):
    parts.append(box([0, y, 0.055], [7.3, 0.008, 0.008], "wire", k=0))
parts.append(box([0, 0.6, 0], [7.4, 1.2, 0.12], "post_wood", v=0))
dressing("fence_post_wire", "Post-and-wire fence (8 m)", "Split chestnut posts with three strands of plain wire along a field edge.", parts, [post, wire], [8.0, 0.4])
parts = []
for x in (-1.75, 1.75):
    parts.append(box([x, 0.7, 0], [0.18, 1.4, 0.18], "post_wood"))
for k, y in enumerate((0.3, 0.65, 1.0)):
    parts.append(box([0, y, 0.12], [3.3, 0.09, 0.05], "post_wood"))
parts.append(box([-0.2, 0.65, 0.12], [3.0, 0.08, 0.05], "post_wood", r=[0, 0, 15]))
for x in (-1.1, 0.0, 1.1):
    parts.append(box([x, 0.65, 0.12], [0.07, 0.85, 0.05], "post_wood", k=0))
dressing("field_gate", "Field gate", "Five-bar style timber field gate between two posts (closed; walk round or vault the hedge).", parts, [post], [3.8, 0.4])
parts = [box([0, 4.0, 0], [0.24, 8.0, 0.24], "post_wood"), box([0, 7.5, 0], [1.6, 0.1, 0.1], "post_wood", k=0)]
for x in (-0.65, 0.0, 0.65):
    parts.append(box([x, 7.62, 0], [0.05, 0.12, 0.05], "wire", k=0))
dressing("utility_pole", "Telegraph pole", "Creosoted telegraph pole with a cross-arm and insulators (~8 m).", parts, [post, wire], [0.5, 0.5])
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


def octo(parts, c, s, m, g):
    """a square section and the same turned 45 degrees: reads as an octagonal casting"""
    parts.append(lpart(c, s, m, g)); parts.append(lpart(c, [s[0] * 0.98, s[1], s[2] * 0.98], m, g, r=[0, 45, 0]))


def lantern4(parts, cx, cy, cz, w, h, frame, g_frame, g_glass, flame="flame", fh=0.04):
    """four-paned lantern centred at (cx, cy, cz): glass box, corner posts, top and bottom frames, a flame or mantle"""
    parts.append(lpart([cx, cy, cz], [w * 0.9, h, w * 0.9], "lamp_glass", g_glass))
    for sx in (-1, 1):
        for sz in (-1, 1):
            parts.append(lpart([cx + sx * w * 0.47, cy, cz + sz * w * 0.47], [0.018, h + 0.02, 0.018], frame, g_frame))
    parts.append(lpart([cx, cy - h / 2 - 0.02, cz], [w, 0.04, w], frame, g_frame))
    parts.append(lpart([cx, cy + h / 2 + 0.025, cz], [w * 1.12, 0.05, w * 1.12], frame, g_frame))
    parts.append(lpart([cx, cy + h / 2 + 0.07, cz], [w * 0.6, 0.06, w * 0.6], frame, g_frame))
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
    lt = dict(light)
    if isinstance(lt.get("group"), str): lt["group"] = groups.index(lt["group"])
    save({"id": id_, "version": 1, "name": name, "category": "Lighting", "description": desc, "floors": 0, "enterable": False,
          "footprint": footprint, "plinthDepth": 0, "maxGrade": 0.8, "noAutoPanels": True, "materials": mats, "parts": parts,
          "furniture": [], "interactables": [], "panels": [], "prop": p, "light": lt})


# gas street lamp (candelabre): cast-iron fluted column on an octagonal plinth, ladder bar, four-paned lantern, gas mantle
G = ["post", "lantern", "glass"]
parts = []
octo(parts, [0, 0.15, 0], [0.36, 0.3, 0.36], "lamp_green", 0)
octo(parts, [0, 0.42, 0], [0.24, 0.24, 0.24], "lamp_green", 0)
for i in range(5):
    y0, y1 = 0.54 + i * 0.45, 0.54 + (i + 1) * 0.45
    r = 0.085 - 0.007 * i
    octo(parts, [0, (y0 + y1) / 2, 0], [2 * r, y1 - y0 + 0.01, 2 * r], "lamp_green", 0)
octo(parts, [0, 2.8, 0], [0.16, 0.06, 0.16], "lamp_green", 0)
parts.append(lpart([0, 2.86, 0], [0.7, 0.035, 0.035], "lamp_green", 0))
for sx in (-1, 1):
    parts.append(lpart([sx * 0.34, 2.9, 0], [0.04, 0.06, 0.04], "lamp_green", 0))
parts.append(lpart([0, 2.94, 0], [0.12, 0.08, 0.12], "lamp_green", 1))
lantern4(parts, 0, 3.2, 0, 0.32, 0.42, "lamp_green", 1, 2, flame="gas_mantle", fh=0.08)
parts.append(lpart([0, 3.55, 0], [0.04, 0.14, 0.04], "lamp_green", 1))
parts += [lcol([0, 1.45, 0], [0.22, 2.9, 0.22]), lcol([0, 3.22, 0], [0.34, 0.58, 0.34])]
light_asset("lamp_gas_street", "Gas street lamp", "Cast-iron gas street lamp of a Norman town square: fluted column, ladder bar for the lamplighter, four-paned lantern with a mantle (~3.6 m). Lit at dusk and night; shoot the panes or blast it to put it out.",
            parts, [L_GREEN, L_GLASS, L_MANTLE], [0.5, 0.5], G,
            {"family": "metal", "blast": 140, "fragments": ["lantern"], "keep": ["post"], "glass": "glass"},
            {"color": [1.0, 0.84, 0.58], "intensity": 3.0, "radius": 14.0, "offset": [0, 3.18, 0], "shadow": 1, "group": "glass", "use": "street"})

# iron lantern post: a wrought-iron post with a scrolled arm, a lantern hanging over the lane (farm lanes, church paths)
parts = []
parts.append(lpart([0, 0.03, 0], [0.22, 0.06, 0.22], "lamp_iron", 0))
parts.append(lpart([0, 1.33, 0], [0.07, 2.6, 0.07], "lamp_iron", 0))
parts.append(lpart([0, 2.68, 0], [0.05, 0.1, 0.05], "lamp_iron", 0))
parts.append(lpart([0, 2.6, 0.3], [0.03, 0.03, 0.62], "lamp_iron", 0))
parts.append(lpart([0, 2.42, 0.13], [0.025, 0.42, 0.025], "lamp_iron", 0, r=[-40, 0, 0]))
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
parts.append(lpart([0, 0.3, 0.012], [0.12, 0.24, 0.024], "lamp_iron", 0))
parts.append(lpart([0, 0.43, 0.17], [0.025, 0.025, 0.32], "lamp_iron", 0))
parts.append(lpart([0, 0.3, 0.11], [0.02, 0.3, 0.02], "lamp_iron", 0, r=[50, 0, 0]))
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
parts.append(lpart([0, -0.02, 0], [0.04, 0.04, 0.012], "lamp_iron", 0))
parts.append(lpart([0, -0.1, 0], [0.012, 0.13, 0.012], "lamp_iron", 0))
parts.append(lpart([0, -0.185, 0], [0.16, 0.035, 0.16], "lamp_tin", 1))
parts.append(lpart([0, -0.15, 0], [0.07, 0.05, 0.07], "lamp_tin", 1))
parts.append(lpart([0, -0.32, 0], [0.13, 0.2, 0.13], "lamp_glass", 2))
parts.append(lpart([0, -0.32, 0], [0.13, 0.2, 0.13], "lamp_glass", 2, r=[0, 45, 0]))
for sx in (-1, 1):
    parts.append(lpart([sx * 0.095, -0.32, 0], [0.014, 0.3, 0.014], "lamp_tin", 1))
    parts.append(lpart([0, -0.32, sx * 0.075], [0.008, 0.22, 0.008], "lamp_iron", 1))
parts.append(lpart([0, -0.46, 0], [0.17, 0.07, 0.17], "lamp_tin", 1))
parts.append(lpart([0, -0.46, 0], [0.17, 0.07, 0.17], "lamp_tin", 1, r=[0, 45, 0]))
parts.append(lpart([0, -0.36, 0], [0.012, 0.035, 0.006], "flame", 2))
parts.append(lcol([0, -0.32, 0], [0.2, 0.32, 0.2]))
light_asset("lamp_hanging_lantern", "Hanging lantern", "Paraffin storm lantern hung from a hook: porches, barn beams, workshops and stables. Hangs by its top from a ceiling or beam; dim by day, full at dusk and night.",
            parts, [L_IRON, L_TIN, L_GLASS, L_FLAME], [0.2, 0.2], G,
            {"family": "metal", "blast": 40, "hang": True, "blocking": False, "fragments": ["lantern"], "keep": ["hook"], "glass": "glass"},
            {"color": [1.0, 0.7, 0.4], "intensity": 1.0, "radius": 7.0, "offset": [0, -0.33, 0], "flicker": 0.15, "shadow": 1, "group": "glass", "use": "interior"})

# brazier: an iron fire basket on three legs, coals glowing, flames and smoke (sentry posts, gun pits, bivouacs)
G = ["frame", "fire"]
parts = []
for k in range(3):
    a = math.radians(k * 120)
    parts.append(lpart([math.sin(a) * 0.2, 0.3, math.cos(a) * 0.2], [0.035, 0.62, 0.035], "lamp_iron", 0, r=[-12 * math.cos(a), 0, 12 * math.sin(a)]))
octo(parts, [0, 0.66, 0], [0.5, 0.025, 0.5], "lamp_iron", 0)
for k in range(8):
    a = math.radians(k * 45)
    parts.append(lpart([math.sin(a) * 0.25, 0.76, math.cos(a) * 0.25], [0.02, 0.2, 0.02], "lamp_iron", 0))
octo(parts, [0, 0.86, 0], [0.52, 0.02, 0.52], "lamp_iron", 0)
octo(parts, [0, 0.72, 0], [0.42, 0.08, 0.42], "ember", 1)
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
    parts.append(lpart([math.sin(a) * 0.5, 0.07, math.cos(a) * 0.5], [0.2 + rng.uniform(-0.03, 0.04), 0.14 + rng.uniform(-0.02, 0.03), 0.16], "field_stone", 0, r=[0, math.degrees(a) + rng.uniform(-15, 15), rng.uniform(-8, 8)]))
parts.append(lpart([0, 0.012, 0], [0.75, 0.024, 0.75], "ash", 0))
parts.append(lpart([0, 0.03, 0], [0.5, 0.03, 0.5], "ember", 2, r=[0, 30, 0]))
for k in range(5):
    a = 2 * math.pi * k / 5 + 0.3
    parts.append(lpart([math.sin(a) * 0.14, 0.2, math.cos(a) * 0.14], [0.07, 0.48, 0.07], "bark", 1, r=[-28 * math.cos(a), 0, 28 * math.sin(a)]))
for x, z, h, rot in ((-0.05, 0.03, 0.42, 15), (0.06, -0.04, 0.5, -25), (0.0, 0.07, 0.34, 70), (0.04, 0.0, 0.28, 120)):
    parts.append(lpart([x, 0.05 + h / 2, z], [0.12, h, 0.012], "flame", 2, r=[0, rot, 0]))
light_asset("fire_campfire", "Campfire", "A camp fire in a ring of field stones: a teepee of split logs over embers and ash, flames and smoke. Burns day and night (cosmetic: walk round it).",
            parts, [stone, L_ASH, L_EMBER, L_BARK, L_FLAME], [1.2, 1.2], G,
            {"family": "masonry", "blast": 300, "blocking": False, "keep": ["ring", "logs"]},
            {"color": [1.0, 0.5, 0.18], "intensity": 3.0, "radius": 10.0, "offset": [0, 0.5, 0], "flicker": 0.85, "shadow": 1, "group": "fire", "use": "fire", "fx": "flame_smoke", "fxSize": 0.36})
print("lights ok")
