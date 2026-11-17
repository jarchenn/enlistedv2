"""PR1: Poly Haven props (CC0) -> placeable model assets, one batch at a time.

For each Poly Haven id (downloaded by fetch_polyhaven.py into <dl>/<id>/): Blender converts the glTF with
convert_supplied.py into <glb dir>/ph_<id>.glb (metres, ground pivot, front +z) plus its size and per-piece colliders;
the entry (asset id ph_<id>, name, category, size, colliders, credit) is merged into tools/props/ph_props.json, which
tools/catalog_modern_models.mjs turns into assets. Then: regen.mjs writes content/assets/ph_*.json and the engine's
--import-weapon-models <glb dir> bakes content/assets/models/ph_*/.

    python -I tools/props/ph_batch.py <dl dir> <glb dir> <category> <id> [<id> ...]
"""
import json, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
BLENDER = r"C:\Program Files\Blender Foundation\Blender 5.2\blender.exe"
DB = os.path.join(HERE, "ph_props.json")
# files that ship a second (aged / rusted / "_b") copy beside the first: keep one
DROP = {"fire_hydrant": "aged", "exterior_aircon_unit": "rusted", "metal_trash_can": "rust", "old_military_crate": "_b",
        "rollershutter_door": "graffiti"}


def drop_contained(boxes, tol=0.04):
    """Per-piece boxes [cx, cy, cz, sx, sy, sz], largest first: a box inside a kept one (a barrel's hoops, a lid) is dropped."""
    keep = []
    for b in boxes:
        if not any(all(abs(b[i] - k[i]) + b[i + 3] / 2 <= k[i + 3] / 2 + tol for i in range(3)) for k in keep): keep.append(b)
    return keep


assert len(drop_contained([[0, 0.5, 0, 1, 1, 1], [0, 0.5, 0, 1.02, 0.05, 1.02], [2, 0.5, 0, 1, 1, 1]])) == 2


def main():
    dl, glbs, cat, ids = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
    man = json.load(open(os.path.join(dl, "manifest.json"), encoding="utf-8"))
    db = json.load(open(DB, encoding="utf-8")) if os.path.exists(DB) else {}
    for pid in ids:
        m = man.get(pid)
        if not m or m.get("status") != "ok": print("skip  %-32s not downloaded" % pid); continue
        aid = "ph_" + pid.lower(); out = os.path.join(glbs, aid + ".glb")
        def convert(size_opt):
            r = subprocess.run([BLENDER, "-b", "--factory-startup", "--disable-autoexec", "--python", os.path.join(HERE, "convert_supplied.py"), "--",
                                os.path.join(dl, pid, m["gltf"]), out] + size_opt + (["--drop", DROP[pid]] if pid in DROP else []), capture_output=True, text=True)
            if not os.path.exists(out): print("FAIL  %-32s %s" % (pid, (r.stdout + r.stderr)[-400:]))
            return os.path.exists(out)
        if not os.path.exists(out) and not convert(["--scale", "1"]): continue
        side = json.load(open(os.path.splitext(out)[0] + ".json"))
        ph_h = (m.get("dimensions_mm") or [0, 0, 0])[2] / 1000   # Poly Haven's listed height; a few sources are in the wrong unit
        if ph_h > 0.05 and abs(side["size"][1] / ph_h - 1) > 0.2:
            print("scale %-32s %.2f m -> listed %.2f m" % (pid, side["size"][1], ph_h)); os.remove(out)
            if not convert(["--height", "%.4f" % ph_h]): continue
            side = json.load(open(os.path.splitext(out)[0] + ".json"))
        sx, sy, sz = side["size"]
        side["colliders"] = drop_contained(side["colliders"])
        db[aid] = {"ph": pid, "name": m["name"], "category": cat, "size": side["size"], "colliders": side["colliders"],
                   "maxTex": 1024 if max(sx, sy, sz) > 0.7 else 512, "lodDist": 30 if max(sx, sy, sz) > 1.5 else 15,
                   "credit": "%s by %s, Poly Haven (polyhaven.com/a/%s), CC0" % (m["name"], ", ".join(m["authors"]), pid)}
        print("ok    %-32s %5.2f x %5.2f x %5.2f m  %d colliders" % (pid, sx, sy, sz, len(side["colliders"])))
    json.dump(dict(sorted(db.items())), open(DB, "w", encoding="utf-8"), indent=1, ensure_ascii=False)
    # the credits table in docs/credits.md, between the ph_props markers
    cp = os.path.join(HERE, "..", "..", "docs", "credits.md"); doc = open(cp, encoding="utf-8").read()
    a, b = "<!-- ph_props: written by tools/props/ph_batch.py -->", "<!-- /ph_props -->"
    rows = "\n".join("| `%s` | %s | %s | CC0 (Poly Haven) | https://polyhaven.com/a/%s | yes |" % (k, v["name"], v["credit"].split(" by ", 1)[1].split(", Poly Haven")[0], v["ph"]) for k, v in sorted(db.items()))
    table = "%s\n| Asset | Model | Author | Licence | Source | In the repo |\n|---|---|---|---|---|---|\n%s\n%s" % (a, rows, b)
    doc = doc[:doc.index(a)] + table + doc[doc.index(b) + len(b):] if a in doc else doc.replace(
        "The petrol station's fuel dispensers", "Poly Haven props (CC0), downloaded by `tools/props/fetch_polyhaven.py`, converted by `tools/props/ph_batch.py`:\n\n" + table + "\n\nThe petrol station's fuel dispensers", 1)
    open(cp, "w", encoding="utf-8").write(doc)


if __name__ == "__main__":
    main()
