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
        "rollershutter_door": "graffiti", "rollershutter_window_01": "graffiti", "rollershutter_window_02": "graffiti", "rollershutter_window_03": "graffiti"}
# kits with assembled presets beside the loose pieces: keep one preset
KEEP = {"modular_electricity_poles": "preset_03"}
# PR4: modular kits -> one asset per piece (exact node names), snapping by their own sizes; FOLIAGE: alpha-tested (cutout) models
KITS = {
 "modular_chainlink_fence": {"panel": "modular_chainlink_fence", "post": "modular_chainlink_fence_post", "gate": "modular_chainlink_fence_door_gate,modular_chainlink_fence_door_frame,modular_chainlink_fence_door_latch", "corner": "modular_chainlink_fence_corner_outer"},
 "modular_pipes": {"straight_2m": "pipe_200cm_metal", "straight_1m": "pipe_100cm_metal", "tee": "pipe_junction_t_metal", "s_curve": "pipe_s_curve_1_metal", "support": "pipe_support_metal", "valve_chamber": "pipe_chamber_large_metal"},
 "modular_airduct_circular_01": {"straight": "modular_airduct_circular_single", "bend": "modular_airduct_circular_bend_quater", "fan": "modular_airduct_circular_fan"},
 "modular_airduct_rectangular_01": {"straight": "modular_airduct_rectangular_01_single_01", "corner": "modular_airduct_rectangular_01_corner_01", "vent": "modular_airduct_rectangular_01_vent_01"},
 "modular_metal_gutter": {"section": "modular_metal_gutter", "downpipe": "modular_metal_gutter_section", "corner": "modular_metal_gutter_corner"},
 "modular_fire_escape": {"stairs": "modular_fire_escape_stairs", "platform": "modular_fire_escape_platform_middle", "platform_bottom": "modular_fire_escape_platform_bottom", "railing": "modular_fire_escape_railing_middle", "ladder": "modular_fire_escape_ladder_bottom"},
 "modular_electric_cables": {"straight": "cable_straight_long", "turn": "cable_turn_long", "box_4way": "cable_box_4way", "double": "cable_double"},
}
FOLIAGE = {"modular_chainlink_fence"}
NOWINDOW = {"rollershutter_window_01", "rollershutter_window_02", "rollershutter_window_03"}


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
    jobs = []
    for pid in ids:
        for suf, nodes in (KITS.get(pid) or {None: None}).items(): jobs.append((pid, suf, nodes))
    for pid, suf, nodes in jobs:
        m = man.get(pid)
        if not m or m.get("status") != "ok": print("skip  %-32s not downloaded" % pid); continue
        aid = "ph_" + pid.lower() + ("_" + suf if suf else ""); out = os.path.join(glbs, aid + ".glb")
        def convert(size_opt):
            r = subprocess.run([BLENDER, "-b", "--factory-startup", "--disable-autoexec", "--python", os.path.join(HERE, "convert_supplied.py"), "--",
                                os.path.join(dl, pid, m["gltf"]), out] + size_opt + (["--drop", DROP[pid]] if pid in DROP else []) + (["--keep", KEEP[pid]] if pid in KEEP else []) + (["--only", nodes] if nodes else []) + (["--nowindow", "1"] if pid in NOWINDOW else []), capture_output=True, text=True)
            if not os.path.exists(out): print("FAIL  %-32s %s" % (pid, (r.stdout + r.stderr)[-400:]))
            return os.path.exists(out)
        if not os.path.exists(out) and not convert(["--scale", "1"]): continue
        side = json.load(open(os.path.splitext(out)[0] + ".json"))
        ph_h = (m.get("dimensions_mm") or [0, 0, 0])[2] / 1000   # Poly Haven's listed height; a few sources are in the wrong unit
        if ph_h > 0.05 and not suf and abs(side["size"][1] / ph_h - 1) > 0.2:   # a kit piece is not the kit's listed size
            print("scale %-32s %.2f m -> listed %.2f m" % (pid, side["size"][1], ph_h)); os.remove(out)
            if not convert(["--height", "%.4f" % ph_h]): continue
            side = json.load(open(os.path.splitext(out)[0] + ".json"))
        sx, sy, sz = side["size"]
        side["colliders"] = drop_contained(side["colliders"])
        db[aid] = {"ph": pid, "name": m["name"] + (" - " + suf.replace("_", " ") if suf else ""), "category": cat, **({"foliage": True} if pid in FOLIAGE else {}), "size": side["size"], "colliders": side["colliders"],
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
