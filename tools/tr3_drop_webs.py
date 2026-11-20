"""TR3: the dark folded planes under the conifers (outpost_woods, the user's screenshot): the bake's collapse decimation of
a branch network (tools/bake_ph_forest.py, wood to a few hundred triangles) fills the V between two branches with one
flat triangle, a metre or two across, bark-textured, at trunk height. A tube never makes a wide triangle: a wood
triangle (an opaque, non-card part) whose altitude (2 x area / longest edge) is over MAX_ALT and whose area is over
MIN_AREA, above GROUND (the root flare stays), is a web, and is collapsed (its indices made degenerate: the buffers and part ranges stay as they are).

    python -I tools/tr3_drop_webs.py [--dry] <content/assets/models dir>...   # every bake dir given (its lod1 too)

bake_ph_forest.py drops the same triangles after its wood decimation (drop_webs), so a re-bake comes out clean.
"""
import json, os, struct, sys
import numpy as np

MAX_ALT, MIN_AREA, GROUND = 0.3, 0.12, 0.35   # VO A1 pass: lowered once (0.4 / 0.2 left small slivers); the root flare at the foot is kept


def webs(P, t):
    A, B, C = P[t[:, 0]], P[t[:, 1]], P[t[:, 2]]
    area = np.linalg.norm(np.cross(B - A, C - A), axis=1) / 2
    e = np.max([np.linalg.norm(B - A, axis=1), np.linalg.norm(C - B, axis=1), np.linalg.norm(A - C, axis=1)], axis=0)
    return (area > MIN_AREA) & (2 * area / np.maximum(e, 1e-6) > MAX_ALT) & ((A[:, 1] + B[:, 1] + C[:, 1]) / 3 > GROUND)


def fix(path, dry):
    m = json.load(open(os.path.join(path, "model.json"))); f = os.path.join(path, "mesh.bin")
    b = bytearray(open(f, "rb").read()); _, nv, ni = struct.unpack_from("<III", b, 0)
    P = np.frombuffer(bytes(b), np.float32, nv * 12, 12).reshape(nv, 12)[:, :3]
    I = np.frombuffer(bytes(b), np.uint32, ni, 12 + nv * 48).copy()
    n = 0
    for part in m["parts"]:
        mat = m["materials"][part["mat"]] if 0 <= part.get("mat", -1) < len(m["materials"]) else {}
        if mat.get("cutout"): continue   # leaf cards are flat by design
        t = I[part["first"]:part["first"] + part["count"]].reshape(-1, 3)
        w = webs(P, t)
        if w.any():
            n += int(w.sum()); t[w] = t[w][:, :1]   # degenerate: all three corners the first
            I[part["first"]:part["first"] + part["count"]] = t.reshape(-1)
    if n and not dry:
        b[12 + nv * 48:12 + nv * 48 + ni * 4] = I.astype(np.uint32).tobytes()
        open(f, "wb").write(bytes(b))
    return n


if __name__ == "__main__":
    a = sys.argv[1:]; dry = "--dry" in a; a = [x for x in a if x != "--dry"]
    for d in a:
        for sub in ("", "lod1"):
            p = os.path.join(d, sub)
            if os.path.exists(os.path.join(p, "mesh.bin")):
                n = fix(p, dry)
                if n: print("%-40s %4d web triangles%s" % (os.path.join(os.path.basename(d.rstrip("/\\")), sub), n, " (dry)" if dry else " collapsed"))
