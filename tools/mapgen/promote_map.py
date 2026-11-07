"""Promote a saved map (.nmap.json) to a New Map template: content/templates/<id>.json + <id>.nmap.json.

The map keeps everything it holds (instances, scenario, routes, cameras); "Create editable copy" then loads it as a
normal creative document. The template reuses the ground the map was built on: the procedural terrain is a function of
the template seed and size only, so the map's own template seed and size reproduce it exactly (maps built on a template
with an authored heightmap are not supported here: they carry their template id and stay on that template).

    "$TEMP/nmk/py/python.exe" -I tools/mapgen/promote_map.py <map.nmap.json> <id> "<Name>" --order N
        [--description "..."] [--thumb x,y,z,yaw,pitch] [--playable auto|yes|no]

Then render the preview: NormandyMapmaker.exe --selftest-maps --render-map-thumbs, and copy
selftest_out/<id>.thumb.png into content/templates/.
"""
import argparse
import json
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TDIR = os.path.join(ROOT, "content", "templates")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("map")
    ap.add_argument("id")
    ap.add_argument("name")
    ap.add_argument("--order", type=int, required=True)
    ap.add_argument("--description", default="")
    ap.add_argument("--thumb", default=None, help="x,y,z,yaw,pitch of the fixed preview camera")
    ap.add_argument("--playable", default="auto", choices=("auto", "yes", "no"))
    a = ap.parse_args()

    with open(a.map, encoding="utf-8") as f:
        m = json.load(f)
    src_tid = (m.get("template") or {}).get("id", "blank_slate")
    src_t = os.path.join(TDIR, src_tid + ".json")
    if not os.path.exists(src_t):
        sys.exit("the map's template '%s' is not in content/templates" % src_tid)
    with open(src_t, encoding="utf-8") as f:
        base = json.load(f)
    if "heightmap" in base:
        sys.exit("the map is built on '%s', a template with an authored heightmap: not supported" % src_tid)
    size = m.get("mapSize") or {}
    width = size.get("width", base.get("width", 600))
    depth = size.get("depth", base.get("depth", 600))
    seed = (m.get("template") or {}).get("seed", base.get("seed", 1944))
    sc = m.get("scenario") or {}
    has_sc = bool(sc.get("objectives"))
    playable = has_sc if a.playable == "auto" else a.playable == "yes"

    m["name"] = a.name
    m["template"] = {"id": a.id, "version": 1, "seed": seed}
    m["mapSize"] = {"width": width, "depth": depth}
    with open(os.path.join(TDIR, a.id + ".nmap.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(m, f, separators=(",", ":"))

    cam = m.get("editorCamera") or {}
    t = {"id": a.id, "version": 1, "order": a.order, "name": a.name, "available": True, "seed": seed,
         "description": a.description or ("%s. %d x %d m." % (a.name, width, depth)),
         "width": width, "depth": depth, "margin": base.get("margin", 100),
         "camera": {"pos": cam.get("position", base["camera"]["pos"]), "yaw": cam.get("yaw", base["camera"]["yaw"]),
                    "pitch": cam.get("pitch", base["camera"]["pitch"])},
         "map": a.id + ".nmap.json", "playable": playable, "thumbnail": a.id + ".thumb.png",
         "selftest": "authored"}
    if a.thumb:
        x, y, z, yaw, pitch = (float(v) for v in a.thumb.split(","))
        t["thumbnailCamera"] = {"pos": [x, y, z], "yaw": yaw, "pitch": pitch}
    with open(os.path.join(TDIR, a.id + ".json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(t, f, indent=2)
    print("%s: %d instances, %d objectives, %d spawns, %d x %d m, seed %s, playable %s"
          % (a.id, len(m.get("instances", [])), len(sc.get("objectives", [])), len(sc.get("spawns", [])), width, depth, seed, playable))


if __name__ == "__main__":
    main()
