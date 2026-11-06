"""Retarget building materials to the wall / interior finish patterns (docs/interiors.md).

Deterministic and idempotent: rewrites only the "materials" entries named below in content/assets/*.json and adds
the `brick` material where parts use it without a definition (the schoolhouse walls fell back to the default grey).
The same table is applied in tools/parts.mjs, so a future regeneration with build_assets.mjs keeps it.

Usage: python -I tools/retarget_materials.py [repo root]
"""
import json
import pathlib
import sys

# name -> (pattern, grain); grain: wall / floor finish code used where a fragment is outside every detected room
MAP = {
    "stone": ("rubble", 1),
    "stone_plinth": ("rubble", 1),
    "stone_dressed": ("ashlar", 1),
    "render": ("limewash", 1),
    "brick": ("brickwork", 1),
    "plaster": ("interior", 0),          # painted limewash
    "plaster_smoke": ("interior", 6),    # smoke-darkened chimney breast
    "floor_x": ("floorfin", 0),          # boards
    "terracotta": ("floorfin", 2),       # hexagonal tomettes
}
BRICK = {"name": "brick", "albedo": [0.25, 0.13, 0.09], "roughness": 0.86, "pattern": "brickwork", "metallic": 0, "grain": 1}


def uses(a, name):
    def walk(o):
        if isinstance(o, dict):
            if o.get("m") == name:
                return True
            return any(walk(v) for v in o.values())
        if isinstance(o, list):
            return any(walk(v) for v in o)
        return False
    return walk(a.get("parts", [])) or walk(a.get("panels", [])) or walk(a.get("furniture", [])) or walk(a.get("interactables", []))


def main():
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parent.parent)
    changed = 0
    for f in sorted((root / "content" / "assets").glob("*.json")):
        txt = f.read_text(encoding="utf-8")
        a = json.loads(txt)
        if "materials" not in a or a.get("category") == "Furniture":
            continue
        before = json.dumps(a["materials"], sort_keys=True)
        for m in a["materials"]:
            if m.get("name") in MAP:
                m["pattern"], m["grain"] = MAP[m["name"]]
        if not any(m.get("name") == "brick" for m in a["materials"]) and uses(a, "brick"):
            a["materials"].append(dict(BRICK))
        if json.dumps(a["materials"], sort_keys=True) != before:
            f.write_text(json.dumps(a, ensure_ascii=False, separators=(",", ":")), encoding="utf-8", newline="")
            changed += 1
            print("retargeted", f.name)
    print(changed, "asset(s) changed")


if __name__ == "__main__":
    main()
