"""Re-seat a generated map's instances on the engine's runtime ground (MAP1 follow-up).

The generator seats on its own grid; the engine's ground differs a little (pad blends, fills, quantisation), and small
pieces on slopes sit on their lowest corner. `--find-floaters` measures every instance against the runtime ground and
writes selftest_out/floaters_<map>.csv (kind, id, gap_m). This moves each listed instance by its gap in
content/templates/<map>.nmap.json: sunk pieces up to 6 cm under the ground at the centre, floating pieces down by
their gap (base 2 cm over the lowest sample). Run the finder again after it; a piece that flips between the two (a
rigid base on a slope steeper than the 15 cm window) is a justified exception.

    <python> -I tools/mapgen/seat_from_floaters.py <map> <floaters csv>

Re-run after regenerating the map (the generator rewrites the template)."""
import csv
import json
import os
import sys


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    tid, path = sys.argv[1], sys.argv[2]
    repo = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
    mp = os.path.join(repo, "content", "templates", tid + ".nmap.json")
    with open(mp, encoding="utf-8") as f:
        doc = json.load(f)
    fix = {}
    with open(path, encoding="utf-8") as f:
        for r in csv.DictReader(f):
            g = float(r["gap_m"])
            fix[int(r["id"])] = (g - 0.06) if r["kind"] == "sunk" else -(g - 0.02)
    n = 0
    for i in doc["instances"]:
        d = fix.get(int(i["id"]))
        if d is not None and abs(d) > 0.005:
            i["position"][1] = round(i["position"][1] + d, 4)
            n += 1
    with open(mp, "w", encoding="utf-8", newline="\n") as f:
        json.dump(doc, f, separators=(",", ":"))
    print("%s: %d instances moved (%d listed)" % (tid, n, len(fix)))


if __name__ == "__main__":
    main()
