"""VFX rev3 phase 3: a map's fine sculpt (its script's V["fine"]) written into the committed .nmap.json, for maps edited
after generation (the committed file is the truth; a rebuild would undo the edits, see patch_trails.py).
  <python> -I tools/mapgen/patch_slice.py outpost_woods
Builds the fine layer on the committed heightmap, replaces "terrainSculpt", and moves every instance standing inside the
changed area by the change of the ground under it (new sculpt minus the old one), so nothing floats or sinks; then, once
(marked "vfxSlice"), V["slice_paint"] over V["slice_rect"]. A second run with the same script changes nothing. The map script carries the same V["fine"], so a future rebuild keeps it."""
import importlib
import json
import math
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from mapgen.terrain import Grid  # noqa: E402
from mapgen.relief import FineLayer  # noqa: E402
from mapgen.paint import GroundPaint, CHUNK  # noqa: E402
import base64  # noqa: E402

REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
TPL = os.path.join(REPO, "content", "templates")


def load_grid(mid):
    tj = json.load(open(os.path.join(TPL, mid + ".json"), encoding="utf-8"))
    g = Grid(tj["width"], tj["depth"], margin=tj["margin"])
    raw = open(os.path.join(TPL, mid + ".height.png"), "rb").read()
    w, h = struct.unpack(">II", raw[16:24])
    assert (w, h) == (g.nx, g.nz), ("grid", (w, h), (g.nx, g.nz))
    idat = b""; p = 8
    while p < len(raw):
        n = struct.unpack(">I", raw[p:p + 4])[0]
        if raw[p + 4:p + 8] == b"IDAT":
            idat += raw[p + 8:p + 8 + n]
        p += 12 + n
    d = zlib.decompress(idat); hm = tj["heightmap"]
    for j in range(h):
        row = d[j * (2 * w + 1) + 1:(j + 1) * (2 * w + 1)]
        for i in range(w):
            g.h[j * w + i] = struct.unpack(">H", row[2 * i:2 * i + 2])[0] / 65535.0 * (hm["max"] - hm["min"]) + hm["min"]
    g.hmin, g.hmax = hm["min"], hm["max"]
    return g


def sculpt_at(fine_d, fine, x, z):
    """The sculpt delta (m) at (x, z) for a tile dict {key: 36 x 36}."""
    tx, tz = int(math.floor((x - fine.g.org_x) / fine.ts)), int(math.floor((z - fine.g.org_z) / fine.ts))
    D = fine_d.get(tz * fine.tx_n + tx)
    if D is None:
        return 0.0
    fx = min(max((x - fine.g.org_x - tx * fine.ts) / fine.fs, 0.0), 34.9999); fz = min(max((z - fine.g.org_z - tz * fine.ts) / fine.fs, 0.0), 34.9999)
    i, j = int(fx), int(fz); u, v = fx - i, fz - j
    a, b, c, d = D[j][i], D[j][i + 1], D[j + 1][i], D[j + 1][i + 1]
    return a + (b - a) * u + (d - b) * v if u >= v else a + (c - a) * v + (d - c) * u


def main(mid):
    import numpy as np
    V = importlib.import_module("maps." + mid).V
    if not V.get("fine"):
        raise SystemExit(mid + ": no V['fine'] in its script")
    g = load_grid(mid)
    fine = FineLayer(g)
    V["fine"](fine)
    path = os.path.join(TPL, mid + ".nmap.json")
    nm = json.load(open(path, encoding="utf-8"))
    old = {t["key"]: np.array(t["d"]).reshape(36, 36) for t in nm.get("terrainSculpt", {}).get("tiles", [])}
    new = fine.to_json()
    newd = {t["key"]: np.array(t["d"]).reshape(36, 36) for t in new["tiles"]}
    moved = 0
    for e in nm["instances"]:
        p = e.get("position")
        if not p:
            continue
        dy = sculpt_at(newd, fine, p[0], p[2]) - sculpt_at(old, fine, p[0], p[2])
        if abs(dy) >= 0.002:
            p[1] = round(p[1] + dy, 4); moved += 1
    nm["terrainSculpt"] = new
    if V.get("slice_paint") and not nm.get("vfxSlice", {}).get("paint"):   # once: dabs strengthen on a second pass
        paint = GroundPaint()
        for c in nm.get("groundPaint", {}).get("chunks", []):
            raw, px = base64.b64decode(c["d"]), bytearray()
            for k in range(0, len(raw), 5):
                px += raw[k + 1:k + 5] * raw[k]
            assert len(px) == CHUNK * CHUNK * 4
            paint.chunks[tuple(c["c"])] = px
        x0, z0, x1, z1 = V["slice_rect"]
        pud = []
        for e in nm["instances"]:
            a = e["asset"] if isinstance(e["asset"], str) else e["asset"]["id"]
            p = e.get("position")
            if a == "wat_puddles" and p and x0 <= p[0] <= x1 and z0 <= p[2] <= z1:
                pud.append((p[0], p[2], 1.2))
        n = V["slice_paint"](paint, fine.ground, pud)
        nm["groundPaint"] = paint.to_json()
        nm.setdefault("vfxSlice", {})["paint"] = 1
        print("%s: %d paint dabs (soil to litter, %d puddle rings)" % (mid, n, len(pud)))
    if V.get("slice_roads"):   # the engine's roadLateral / poolAt data (src/roads.h): the road and the puddles of the slice
        x0, z0, x1, z1 = V["slice_rect"]
        nm["vfxRoads"] = V["slice_roads"]()
        nm["vfxPools"] = [{"x": round(e["position"][0], 3), "z": round(e["position"][2], 3), "r": 1.2, "waterZ": round(e["position"][1], 3)}
                          for e in nm["instances"] if (e["asset"] if isinstance(e["asset"], str) else e["asset"]["id"]) == "wat_puddles"
                          and x0 <= e["position"][0] <= x1 and z0 <= e["position"][2] <= z1]
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(nm, f, separators=(",", ":"))
    print("%s: %d sculpt tiles (was %d), %d instances moved with the ground" % (mid, len(new["tiles"]), len(old), moved))


if __name__ == "__main__":
    for m in sys.argv[1:]:
        main(m)
