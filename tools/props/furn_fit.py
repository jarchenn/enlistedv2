"""PR2: fit imported Poly Haven models to the furniture they replace -> content/settings/furniture_models.json.

tools/props/furn_replace.json maps a furniture asset id to {"ph": <Poly Haven id>, "turn": 0 | 90 | 180 | 270 (optional: tables and other symmetric pieces only; a seat keeps its front)}.
For each pair: the asset's envelope (its prop "bounds", else the box of its visible parts), the model's size
(tools/props/ph_props.json, metres, front +z, ground pivot), turned only by an explicit "turn";
one uniform scale that keeps the model inside the footprint (never wider or deeper, 3 % slack), without distorting it;
an offset that centres it on the envelope (bottom on the envelope's floor: wall pieces sit at their mount, hanging
lamps at their drop). A pair whose height ends up more than 30 % off, or that leaves more than 30 % of the footprint's other side empty, is refused (printed, not written): the
replacement must keep the footprint, collision and cover of the piece it replaces. The engine loads the result
(src/assets.cpp furnitureModelFor) and --import-weapon-models bakes it from the GLB folder given there.

    python -I tools/props/furn_fit.py [--check]
"""
import json, math, os, sys

HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(ROOT, "content", "settings", "furniture_models.json")


def envelope(aid):
    j = json.load(open(os.path.join(ROOT, "content", "assets", aid + ".json"), encoding="utf-8"))
    b = (j.get("prop") or {}).get("bounds")
    if b: return b[0], b[1]
    lo, hi = [1e9] * 3, [-1e9] * 3
    for p in j.get("parts", []):
        if p.get("v", 1) == 0: continue
        c, s = p.get("c", [0, 0, 0]), p.get("s", [0, 0, 0])
        for k in range(3): lo[k] = min(lo[k], c[k] - s[k] / 2); hi[k] = max(hi[k], c[k] + s[k] / 2)
    return lo, hi


def fit(env, size, turn=None):
    lo, hi = env; E = [hi[k] - lo[k] for k in range(3)]
    best = None
    for t in ([turn] if turn is not None else [0]):   # the model's front stays the asset's front unless a turn is asked for
        mx, my, mz = (size[2], size[1], size[0]) if t in (90, 270) else size
        r = [E[0] / mx, E[1] / my, E[2] / mz]
        s = min(r[0], r[2]) * 1.03   # never wider or deeper than the footprint (3 % slack): walls and neighbours stay clear
        err = max(abs(r[1] / s - 1), 1 - min(r[0], r[2]) / max(r[0], r[2]))   # height off, footprint left empty
        if best is None or err < best[2]: best = (t, s, err, r)
    return best


assert fit(([-0.5, 0, -0.25], [0.5, 1, 0.25]), [0.5, 1, 1], 90)[2] < 0.05 and fit(([-0.5, 0, -0.25], [0.5, 1, 0.25]), [0.5, 1, 1])[2] > 0.5   # turned only when asked


def main():
    reps = json.load(open(os.path.join(HERE, "furn_replace.json"), encoding="utf-8"))
    ph = json.load(open(os.path.join(HERE, "ph_props.json"), encoding="utf-8"))
    out = {"_doc": json.load(open(OUT, encoding="utf-8")).get("_doc", "") if os.path.exists(OUT) else ""}
    bad = 0
    for aid, r in sorted(reps.items()):
        if aid.startswith("_"): continue
        p = ph.get("ph_" + r["ph"].lower())
        if not p: print("skip  %-26s ph_%s not imported" % (aid, r["ph"].lower())); bad += 1; continue
        env = envelope(aid); t, s, err, rr = fit(env, p["size"], r.get("turn"))
        lo, hi = env
        line = "%-26s <- %-28s turn %3d scale %.2f  axis ratios %s  worst %.0f %%" % (aid, r["ph"], t, s, [round(x / s, 2) for x in rr], err * 100)
        if err > 0.30: print("REFUSE", line); bad += 1; continue
        print("ok    ", line)
        out[aid] = {"_ref": "%s (Poly Haven, CC0): %s" % (r["ph"], r.get("why", "")), "src": "ph_%s.glb" % r["ph"].lower(), "dir": "fr_" + aid[5:],
                    "pivot": "ground", "scale": round(s, 4), "rot": [0, t, 0], "offset": [round((lo[0] + hi[0]) / 2, 3), round(lo[1], 3), round((lo[2] + hi[2]) / 2, 3)],
                    "jpgMaps": True, "maxTex": p["maxTex"], "simplify": 0.5, "lod1": 0.25, "lodDist": 12, "cullDist": 60, "credit": p["credit"]}
    if "--check" not in sys.argv:
        json.dump(out, open(OUT, "w", encoding="utf-8"), indent=1, ensure_ascii=False)
    print("written" if "--check" not in sys.argv else "checked", len(out) - 1, "refused / missing", bad)


if __name__ == "__main__":
    main()
