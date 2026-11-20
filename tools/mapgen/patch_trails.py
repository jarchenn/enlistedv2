"""Trails on maps that were edited after generation (the committed .nmap.json is the truth, a rebuild would undo the
edits): relabel the ground paint of the trail corridors to a spare paint type drawn as the user's trail set, and add
that type to look.paintSets. Only texels whose top or under type is the trail's old type, within the corridor of a trail
polyline (half width + edge), change type; their weights stay. Idempotent: a second run changes nothing.
  <python> -I tools/mapgen/patch_trails.py outpost_woods outpost_desert
The map scripts paint the same types (outpost_woods.py, outpost_desert.py), so a future rebuild keeps the trails."""
import base64
import importlib
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from mapgen.paint import TYPES, TEXEL, CHUNK  # noqa: E402
from mapgen.geom import polyline_dist, catmull_rom  # noqa: E402

REPO = os.path.normpath(os.path.join(HERE, "..", ".."))


def trails(mid):
    """(old type, new type, [(polyline, width, edge)]) per trail group, and the paintSets entries."""
    m = importlib.import_module("maps." + mid)
    if mid == "outpost_woods":   # the forest tracks are dirt tracks (WP1): dirt -> "rock" (no step_dirt type is free here)
        tr = [(m.TRACK, 2.6, 0.9)] + [(p, w, 1.2) for p, w in ((m.TRACK, 4.0), (m.SPUR_RADAR, 3.2), (m.SPUR_MILL, 3.5), (m.SPUR_POND, 3.0))]
        return [("dirt", "rock", tr)], {"rock": "trail_dirt"}
    if mid == "outpost_desert":   # gravel tracks -> "ploughed" (trail_gravel); the dirt tyre ruts -> "cobbles" (trail_dirt)
        ruts = []
        for pts in ([(-36.0, -20.0), (-20.0, -8.0), (4.0, -4.0), (30.0, 2.0)], [(-30.0, 60.0), (-6.0, 46.0), (20.0, 20.0), (34.0, -2.0)],
                    [(-58.0, -30.0), (-46.0, -10.0), (-40.0, 10.0)], [(10.0, 66.0), (24.0, 50.0), (46.0, 44.0)]):
            for side in (-0.85, 0.85):   # outpost_desert.py: paired ruts 0.6 / 0.8 m either side of the line
                ruts.append((catmull_rom([(x + side * 0.6, z + side * 0.8) for x, z in pts]), 0.45, 0.25))
        return [("gravel", "ploughed", [(p, w, 1.2) for p, w in m.TRACKS]), ("dirt", "cobbles", ruts)], {"ploughed": "trail_gravel", "cobbles": "trail_dirt"}
    raise SystemExit("no trail patch for " + mid)


def decode(d):
    raw, out = base64.b64decode(d), bytearray()
    for k in range(0, len(raw), 5):
        out += raw[k + 1:k + 5] * raw[k]
    return out


def encode(c):
    r, k = bytearray(), 0
    while k < len(c):
        m = k + 4
        while m < len(c) and m - k < 255 * 4 and c[m:m + 4] == c[k:k + 4]:
            m += 4
        r.append((m - k) // 4)
        r += c[k:k + 4]
        k = m
    return base64.b64encode(bytes(r)).decode("ascii")


def patch(mid):
    path = os.path.join(REPO, "content", "templates", mid + ".nmap.json")
    doc = json.load(open(path, encoding="utf-8"))
    gp = doc["groundPaint"]
    assert gp.get("texel") == TEXEL and gp.get("chunk") == CHUNK
    groups, sets = trails(mid)
    changed = 0
    for ch in gp["chunks"]:
        cx, cz = ch["c"]
        c = decode(ch["d"])
        assert len(c) == CHUNK * CHUNK * 4
        for t in range(CHUNK * CHUNK):
            k = t * 4
            x, z = (cx * CHUNK + t % CHUNK + 0.5) * TEXEL, (cz * CHUNK + t // CHUNK + 0.5) * TEXEL
            for old, new, lines in groups:
                o, n = TYPES.index(old), TYPES.index(new)
                if c[k] != o and c[k + 2] != o:
                    continue
                if not any(polyline_dist((x, z), p)[0] <= w / 2 + e for p, w, e in lines):
                    continue
                for s in (k, k + 2):
                    if c[s] == o and c[s + 1]:
                        c[s] = n; changed += 1
        ch["d"] = encode(c)
    look = doc.setdefault("look", {})
    look.setdefault("paintSets", {}).update(sets)
    with open(path, "w", encoding="utf-8", newline="") as f:
        json.dump(doc, f, separators=(",", ":"))
    print("%s: %d texel layers relabelled, paintSets %s" % (mid, changed, look["paintSets"]))


if __name__ == "__main__":
    for a in sys.argv[1:]:
        patch(a)
