"""CP step 8 (corpus 07 s1.3): texel density per material class from content/textures/textures.json.
density (texels per metre) = the set's array resolution (1024, or 2048 for a 2K set) x the slot's "scale" / the set's
size in metres (meta.json "size"). Corpus targets at 1080p-1440p: walk-up walls, doors, cover 512-1024 /m; ground
512-1024 /m (+ detail); mid props 128-256 /m. Writes docs/texel-density.md; read-only otherwise.
  python -I tools/texel_density.py"""
import json
import os
import statistics
import struct

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
TEX = os.path.join(ROOT, "content", "textures")


def jpg_width(p):
    with open(p, "rb") as f:
        d = f.read()
    i = 2
    while i < len(d) - 9:
        if d[i] != 0xFF:
            i += 1
            continue
        m = d[i + 1]
        if m in (0xC0, 0xC1, 0xC2):
            return struct.unpack(">H", d[i + 7:i + 9])[0]
        i += 2 + struct.unpack(">H", d[i + 2:i + 4])[0]
    return 0


def res_of(s):
    for f in ("color.jpg", "color.png"):
        p = os.path.join(TEX, s, f)
        if os.path.exists(p):
            if f.endswith(".png"):
                with open(p, "rb") as fh:
                    w = struct.unpack(">I", fh.read(24)[16:20])[0]
            else:
                w = jpg_width(p)
            return 2048 if w >= 2048 else 1024
    return 0


def cls(name, fam, space):
    t = (name + "|" + fam).lower()
    has = lambda *k: any(x in t for x in k)
    if has("decal", "graffiti", "overlay", "poster", "smear", "imperfection", "skin", "sign", "reference atlas"):
        return "overlay / special"
    if space == "ground" or has("ground", "terrain", "paint", "grass", "soil", "mud", "sand", "gravel", "paving", "road",
                                "asphalt", "cobble", "sett", "trail", "path", "snow", "forest", "litter", "moss", "pebble"):
        return "ground"
    if has("brick", "stone", "plaster", "concrete", "facade", "render", "ashlar", "rubble", "granite", "wood", "plank",
           "timber", "siding", "metal", "steel", "iron", "rust", "corrugated", "roof", "slate", "tile", "door", "panel", "wall"):
        return "walk-up (walls, roofs, cover)"
    return "props / interior / other"


def main():
    j = json.load(open(os.path.join(TEX, "textures.json"), encoding="utf-8"))
    rows = []
    for name, sl in j["slots"].items():
        if not isinstance(sl, dict) or not sl.get("set"):
            continue
        mp = os.path.join(TEX, sl["set"], "meta.json")
        if not os.path.exists(mp):
            continue
        m = json.load(open(mp, encoding="utf-8"))
        size = max(m.get("size", [2, 2])[0], 0.05)
        r = res_of(sl["set"])
        if not r:
            continue
        d = r * sl.get("scale", 1.0) / size
        rows.append((cls(name, m.get("family", ""), sl.get("space", "asset")), name, sl["set"], r, size, sl.get("scale", 1.0), d, bool(sl.get("lazy"))))
    out = ["# Texel density per material class (CP step 8)", "",
           "`python -I tools/texel_density.py` (corpus 07 s1.3). Density = array resolution x slot scale / set size (texels per",
           "metre). Corpus targets at 1080p-1440p: walk-up surfaces and ground 512-1024 /m (ground plus a detail layer), mid props",
           "128-256 /m, skyline 32-128 /m. At 1080p 512 /m is resolved at 1.9 m, 1024 /m at 0.9 m.", "",
           "| Class | Slots | Min | Median | Max | Under 512 | Over 1024 |", "|---|---:|---:|---:|---:|---:|---:|"]
    for c in sorted({r[0] for r in rows}):
        ds = [r[6] for r in rows if r[0] == c]
        out.append("| %s | %d | %.0f | %.0f | %.0f | %d | %d |" % (c, len(ds), min(ds), statistics.median(ds), max(ds),
                                                                 sum(d < 512 for d in ds), sum(d > 1024 for d in ds)))
    for c in ("walk-up (walls, roofs, cover)", "ground"):
        lo = sorted((r for r in rows if r[0] == c and r[6] < 512), key=lambda r: r[6])
        hi = sorted((r for r in rows if r[0] == c and r[6] > 1024), key=lambda r: -r[6])
        out += ["", "## %s outliers" % c, "", "Under 512 /m (soft up close; candidates for a 2K set, a smaller tile or a detail layer):", ""]
        out += ["- `%s` (set `%s`, %d px over %.2f m, scale %g%s): %.0f /m" % (r[1], r[2], r[3], r[4], r[5], ", lazy" if r[7] else "", r[6]) for r in lo] or ["- none"]
        out += ["", "Over 1024 /m (more texels than 1080p resolves at 1 m; harmless for frame time, a tiling-visibility risk):", ""]
        out += ["- `%s` (set `%s`, %d px over %.2f m, scale %g): %.0f /m" % (r[1], r[2], r[3], r[4], r[5], r[6]) for r in hi[:40]] or ["- none"]
    open(os.path.join(ROOT, "docs", "texel-density.md"), "w", encoding="utf-8").write("\n".join(out) + "\n")
    print("\n".join(out[:16]))


if __name__ == "__main__":
    main()
