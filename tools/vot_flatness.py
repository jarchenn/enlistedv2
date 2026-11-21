"""VFX rev3 phase 3 (docs/vfx-rev3 spec 4.1): the six flatness cues measured on a map slice.

  python -I tools/vot_flatness.py <map id> x0 z0 x1 z1 [--albedo capture.png] [--ao capture.png] [--pom depth_m] [--log game.log]

Cue 1 silhouette relief: vertical range of the ground (base heightfield + terrainSculpt at 0.45 m) over 4 m windows
       (1 m steps). Target 0.15-0.6 m.
Cue 2 sub-mesh parallax: the POM depth in force (passed in; 0 = none). Target >= 0.02 m.
Cue 3 scale bands: octave bands of the albedo spectrum holding >= 4 % of the total (an albedo-only capture of the
       slice's ground, NMK_DBG_TERRAIN 4). Target >= 4. Coarse: a textured image passes easily; read the shares.
Cue 4 contact occlusion: from an AO capture (NMK_DBG_TERRAIN 5: ground drawn as (1, ao, 0)) the darkening of the ground
       at object bases, 1 - AO over the darkest 5 % of ground pixels only. Target 0.30-0.70. The capture includes the
       composite's AO (SSAO / GTAO and the contactao decals), not only the terrain's own occlusion.
Cue 5 object relief: small ground-resting meshes (stones, roots, logs, litter, clumps; not trees or buildings) per m².
       Target 0.3-3. The map file's instances, plus the runtime scatter's stones / twigs / roots / litter when --log
       names a game log run with NMK_VOT_RECT="x0,z0,x1,z1" (its last "[vot] scatter resting N in <rect>" line).
Cue 6 material boundaries: distinct ground paint types (weight >= 0.25, top or under, plus the bare base) in 10 m
       windows. Target 3-6.
Prints one line per cue and PASS / FAIL, and a JSON line for the record."""
import base64
import json
import math
import os
import re
import struct
import sys
import zlib

import numpy as np

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "content", "templates")
TYPES = ["grass", "marsh", "dirt", "mud", "gravel", "sand", "cobbles", "rock", "dry_grass", "ploughed", "leaf_litter",
         "scorched", "pebbles"]


def load_heights(mid):
    tj = json.load(open(os.path.join(ROOT, mid + ".json"), encoding="utf-8"))
    raw = open(os.path.join(ROOT, mid + ".height.png"), "rb").read()
    w, h = struct.unpack(">II", raw[16:24])
    idat = b""; p = 8
    while p < len(raw):
        n = struct.unpack(">I", raw[p:p + 4])[0]
        if raw[p + 4:p + 8] == b"IDAT":
            idat += raw[p + 8:p + 8 + n]
        p += 12 + n
    d = zlib.decompress(idat)
    rows = [np.frombuffer(d[j * (2 * w + 1) + 1:(j + 1) * (2 * w + 1)], ">u2") for j in range(h)]
    hm = tj["heightmap"]
    A = np.array(rows, float) / 65535 * (hm["max"] - hm["min"]) + hm["min"]
    return A, hm["cell"], hm["org"]


def fine_ground(A, cell, org, nm, x0, z0, x1, z1):
    fs = cell / 7.0
    ox, oz = org
    xs = np.arange(x0, x1, fs); zs = np.arange(z0, z1, fs); X, Z = np.meshgrid(xs, zs)
    fx = (X - ox) / cell; fz = (Z - oz) / cell; xi = fx.astype(int); zi = fz.astype(int); tx = fx - xi; tz = fz - zi
    a, b, c, d = A[zi, xi], A[zi, xi + 1], A[zi + 1, xi], A[zi + 1, xi + 1]
    G = np.where(tx >= tz, a + (b - a) * tx + (d - b) * tz, a + (c - a) * tz + (d - c) * tx)
    tX = (A.shape[1] - 1) // 5
    D = {t["key"]: np.array(t["d"]).reshape(36, 36) for t in nm.get("terrainSculpt", {}).get("tiles", [])}
    gx = np.round((X - ox) / fs).astype(int); gz = np.round((Z - oz) / fs).astype(int)
    K = (gz // 35) * tX + gx // 35
    for k, v in D.items():
        m = K == k
        if m.any():
            G[m] += v[gz[m] - (k // tX) * 35, gx[m] - (k % tX) * 35]
    return G, fs


def cue1(G, fs):
    n = int(round(4.0 / fs)); s = max(1, int(round(1.0 / fs)))
    r = [np.ptp(G[j:j + n, i:i + n]) for j in range(0, G.shape[0] - n, s) for i in range(0, G.shape[1] - n, s)]
    r = np.array(r)
    return float(np.median(r)), float(((r >= 0.15) & (r <= 0.6)).mean())


def paint_types(nm, x0, z0, x1, z1):
    gp = nm.get("groundPaint") or {}
    tex = float(gp.get("texel", 0.5)); ch = int(gp.get("chunk", 32))
    i0, i1 = int(math.floor(x0 / tex)), int(math.ceil(x1 / tex)); j0, j1 = int(math.floor(z0 / tex)), int(math.ceil(z1 / tex))
    T = np.full((j1 - j0, i1 - i0, 2), -1, int)
    for c in gp.get("chunks", []):
        cx, cz = c["c"]
        if (cx + 1) * ch <= i0 or cx * ch >= i1 or (cz + 1) * ch <= j0 or cz * ch >= j1:
            continue
        r = base64.b64decode(c["d"]); px = bytearray()
        for k in range(0, len(r), 5):
            px += r[k + 1:k + 5] * r[k]
        for t in range(ch * ch):
            i, j = cx * ch + t % ch, cz * ch + t // ch
            if i0 <= i < i1 and j0 <= j < j1:
                q = px[t * 4:t * 4 + 4]
                T[j - j0, i - i0, 0] = q[0] if q[1] >= 64 else -1
                T[j - j0, i - i0, 1] = q[2] if q[3] >= 64 else -1
    return T, tex


def cue6(T, tex):
    n = int(round(10.0 / tex)); s = int(round(2.0 / tex)); counts = []
    for j in range(0, max(1, T.shape[0] - n + 1), s):
        for i in range(0, max(1, T.shape[1] - n + 1), s):
            w = T[j:j + n, i:i + n]
            kinds = set(int(v) for v in np.unique(w) if v >= 0)
            if (w[..., 0] < 0).any():
                kinds.add(-1)   # bare base ground shows somewhere in the window
            counts.append(len(kinds))
    return float(np.median(counts)), [TYPES[k] for k in sorted({int(v) for v in np.unique(T) if v >= 0})]


SMALL = ("stone", "rock", "boulder", "root", "log", "trunk", "stump", "twig", "branch", "litter", "leaf", "fern", "clump",
         "tuft", "weeds", "nettle", "grass", "moss", "pebble", "gravel", "mushroom", "pinecone", "scree")


def cue5(nm, x0, z0, x1, z1):
    n = 0
    for e in nm["instances"]:
        a = e["asset"] if isinstance(e["asset"], str) else e["asset"]["id"]
        p = e.get("position")
        if not p or not (x0 <= p[0] <= x1 and z0 <= p[2] <= z1):
            continue
        low = a.lower()
        if any(k in low for k in SMALL) and not any(k in low for k in ("tree", "sapling", "house", "wall")):
            n += 1
    return n / ((x1 - x0) * (z1 - z0))


def cue3(png):
    from PIL import Image
    im = np.asarray(Image.open(png).convert("L"), float)
    h, w = im.shape
    im = im[int(h * 0.45):, :]   # the ground: the lower part of a slice view
    im = im - im.mean()
    F = np.abs(np.fft.fftshift(np.fft.fft2(im))) ** 2
    cy, cx = np.array(F.shape) // 2
    yy, xx = np.indices(F.shape)
    r = np.hypot((yy - cy) / F.shape[0], (xx - cx) / F.shape[1])   # cycles per pixel
    bands = []
    f = 1.0 / 256
    while f < 0.5:
        m = (r >= f) & (r < 2 * f)
        bands.append(F[m].sum())
        f *= 2
    tot = sum(bands) or 1.0
    return sum(1 for b in bands if b / tot >= 0.04), [round(b / tot, 3) for b in bands]


def cue4(png):
    from PIL import Image
    im = np.asarray(Image.open(png).convert("RGB"), float) / 255.0
    R, G, B = im[..., 0], im[..., 1], im[..., 2]
    ground = (R > 0.08) & (B < 0.35 * R) & (G <= R * 1.05)   # debug view 5 draws the ground as (1, ao, 0); everything else is lit
    if not ground.any():
        return float("nan")
    ao = np.sort((G[ground] / R[ground]).ravel())
    return float(1.0 - ao[: max(1, len(ao) // 20)].mean())   # the darkest 5 % of the ground: where it meets objects


def scatter_from_log(path, rect):
    n = None
    for line in open(path, encoding="utf-8", errors="replace"):
        m = re.search(r"\[vot\] scatter resting (\d+) in ([-\d.]+),([-\d.]+),([-\d.]+),([-\d.]+)", line)
        if m and all(abs(float(m.group(k + 2)) - rect[k]) < 0.05 for k in range(4)):
            n = int(m.group(1))
    return n


def main():
    a = sys.argv[1:]
    mid = a[0]; x0, z0, x1, z1 = map(float, a[1:5])
    opt = {a[k]: a[k + 1] for k in range(5, len(a) - 1, 2)}
    nm = json.load(open(os.path.join(ROOT, mid + ".nmap.json"), encoding="utf-8"))
    A, cell, org = load_heights(mid)
    G, fs = fine_ground(A, cell, org, nm, x0, z0, x1, z1)
    res = {}
    med, frac = cue1(G, fs); res["c1_relief_4m_median_m"] = round(med, 3); res["c1_in_band"] = round(frac, 2)
    pom = float(opt.get("--pom", 0)); res["c2_pom_depth_m"] = pom
    if "--albedo" in opt:
        nb, b = cue3(opt["--albedo"]); res["c3_bands"] = nb; res["c3_band_shares"] = b
    if "--ao" in opt:
        res["c4_contact_darkening"] = round(cue4(opt["--ao"]), 3)
    c5 = cue5(nm, x0, z0, x1, z1)
    if "--log" in opt:
        sn = scatter_from_log(opt["--log"], (x0, z0, x1, z1))
        if sn is None:
            print("cue 5: no '[vot] scatter resting' line for this rect in %s (NMK_VOT_RECT unset, or scatter off)" % opt["--log"])
        else:
            res["c5_scatter"] = sn; c5 += sn / ((x1 - x0) * (z1 - z0))
    res["c5_resting_per_m2"] = round(c5, 3)
    T, tex = paint_types(nm, x0, z0, x1, z1)
    m6, kinds = cue6(T, tex); res["c6_materials_10m_median"] = m6; res["c6_types"] = kinds
    ok = lambda c, v: "PASS" if v else "FAIL"
    print("cue 1 relief over 4 m: median %.3f m, %.0f %% of windows in 0.15-0.6 m  %s" % (med, frac * 100, ok(1, 0.15 <= med <= 0.6)))
    print("cue 2 POM depth: %.3f m  %s" % (pom, ok(2, pom >= 0.02)))
    if "c3_bands" in res:
        print("cue 3 albedo scale bands: %d (shares %s)  %s" % (res["c3_bands"], res["c3_band_shares"], ok(3, res["c3_bands"] >= 4)))
    else:
        print("cue 3 albedo scale bands: no capture given")
    if "c4_contact_darkening" in res:
        v = res["c4_contact_darkening"]; print("cue 4 contact darkening: %.2f  %s" % (v, ok(4, 0.3 <= v <= 0.7)))
    else:
        print("cue 4 contact darkening: no capture given")
    v = res["c5_resting_per_m2"]; print("cue 5 resting meshes: %.2f per m2  %s" % (v, ok(5, 0.3 <= v <= 3.0)))
    print("cue 6 ground materials in 10 m: median %.0f (%s)  %s" % (m6, ", ".join(kinds), ok(6, 3 <= m6 <= 6)))
    print("JSON " + json.dumps(res))


if __name__ == "__main__":
    main()
