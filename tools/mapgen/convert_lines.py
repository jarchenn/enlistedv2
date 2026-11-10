"""LN2: convert a hand-authored map file's fence / wire / wall / hedge pieces to lines, in place (the same pass the map
scripts run in write_map, mapgen/lines.py; the engine migrates on load the same way).
    <python> -I tools/mapgen/convert_lines.py content/templates/battle1.nmap.json [...]"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from mapgen import Catalog  # noqa: E402
from mapgen.lines import pieces_to_lines  # noqa: E402


class FileBuilder:
    """The few MapBuilder calls pieces_to_lines needs, over a map file's instances."""
    def __init__(self, j, cat):
        self.j, self.cat = j, cat
        self.next_id = j["nextId"]
        self.inst = []
        for o in j["instances"]:
            e = {"asset": o["asset"]["id"], "id": o["id"], "pos": o["position"], "yaw": o.get("yaw", 0.0), "raw": o}
            if "lineGate" in o:
                e["extra"] = {"lineGate": o["lineGate"]}
            self.inst.append(e)
        self._y = {}

    def ground(self, x, z):   # lines draw on the terrain whatever their y; keep the nearest piece's
        best = min(self.inst, key=lambda e: (e["pos"][0] - x) ** 2 + (e["pos"][2] - z) ** 2, default=None)
        return best["pos"][1] if best else 0.0

    def place(self, asset_id, x, z, yaw=0.0, *, extra=None, y=0.0, **_):
        a = self.cat[asset_id]
        o = dict(extra or {})
        o.update({"id": self.next_id, "asset": {"id": asset_id, "version": a.version}, "position": [round(x, 3), round(y, 3), round(z, 3)],
                  "yaw": yaw, "overrides": {"interactables": {}}})
        self.next_id += 1
        e = {"asset": asset_id, "id": o["id"], "pos": o["position"], "yaw": yaw, "raw": o, "extra": extra}
        self.inst.append(e)
        return e

    def remove(self, e):
        self.inst.remove(e)


def main():
    repo = os.path.normpath(os.path.join(HERE, "..", ".."))
    cat = Catalog(os.path.join(repo, "content"))
    for p in sys.argv[1:]:
        with open(p, encoding="utf-8") as f:
            j = json.load(f)
        b = FileBuilder(j, cat)
        n, m = pieces_to_lines(b, j.get("scenario"))
        j["instances"] = [e["raw"] for e in b.inst]
        j["nextId"] = b.next_id
        with open(p, "w", encoding="utf-8", newline="\n") as f:
            json.dump(j, f, separators=(",", ":"))
        print("%s: %d pieces -> %d lines" % (os.path.basename(p), n, m))


if __name__ == "__main__":
    main()
