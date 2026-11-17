"""PR5: the user's supplied furniture models (Sketchfab downloads) -> furniture assets.

SPEC: one row per model: (key, source folder under the incoming dir, asset id, scale by real height in m (None: as
authored), yaw so the front faces +z, role). role "new": a new furniture asset of the model's own size (written here:
content/assets/<id>.json with a box stand-in, colliders from the model's pieces); role "<asset id>": the model replaces
that existing asset's look through tools/props/furn_replace.json (fitted to its envelope by furn_fit.py).
Licences come from each GLB's own Sketchfab metadata (asset.extras: title, author, license, source); a model that is
not CC0 / CC BY is LOCAL-ONLY (its bake is git-ignored and the committed game shows the stand-in).

    python -I tools/props/user_models.py convert <incoming dir> <glb dir>   # Blender: GLB -> metres, front +z, colliders
    python -I tools/props/user_models.py write <incoming dir> <glb dir>     # registry, assets, credits, furn_replace rows
"""
import json, os, struct, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
BLENDER = r"C:\Program Files\Blender Foundation\Blender 5.2\blender.exe"
REG = os.path.join(HERE, "user_models.json")
# key, source folder, asset id, real height (m) or None, yaw, role, name, family, front, wall, rooms note
SPEC = [
 ("bed_low", "bed", "furn_mod_bed_low", 0.805, 0, "new", "Modern bed (low, striped throw)", "cloth", 0.5, False),
 ("bed_panel", "bed_1", "furn_mod_bed_panel", 0.838, 0, "new", "Panel bed (wood and leather)", "cloth", 0.5, False),
 ("bed_victorian", "victorian_bed", "furn_bed_victorian", 1.236, 0, "new", "Victorian bed", "wood", 0.5, False),
 ("bed_rustic", "old_bed", "furn_bed_rustic", 0.757, 0, "new", "Rustic single bed", "wood", 0.5, False),
 ("bunk_pine", "simple_bunk_bed", "furn_mod_bunk_pine", 1.791, 0, "new", "Pine bunk bed", "wood", 0.6, False),
 ("sofa_3230", "sofa_3230", "furn_mod_sofa3", None, 0, "furn_mod_sofa3", "", "", 0, False),
 ("sofa_202205", "sofa_202205_001", "furn_mod_sofa2", None, 0, "furn_mod_sofa2", "", "", 0, False),
 ("sofa_grey", "grey_sofa", "furn_mod_sofa_grey", 0.726, 0, "new", "Low grey sofa", "cloth", 0.5, False),
 ("sofa_corner", "corner_sofa", "furn_mod_sofa_corner", None, 0, "new", "Corner sofa", "cloth", 0.5, False),
 ("sofa_chesterfield", "sofa_free", "furn_sofa_chesterfield", 0.791, 0, "new", "Chesterfield sofa (leather)", "cloth", 0.5, False),
 ("armchair_wing", "living_room_sofa_furniture", "furn_armchair_wing", 1.1, 0, "new", "Wing armchair (leather)", "cloth", 0.45, False),
 ("chair_tufted", "modern_upholstered_armchair_-_4096px2", "furn_mod_chair_tufted", None, 0, "new", "Tufted armchair", "cloth", 0.45, False),
 ("chair_white", "old_sofa", "furn_mod_chair_white", 0.777, 0, "new", "Lounge armchair (white)", "cloth", 0.45, False),
 ("chair_egg", "sofa_1", "furn_mod_chair_egg", None, 0, "new", "Egg chair (leather)", "cloth", 0.45, False),
 ("chair_bowl", "sofa", "furn_mod_chair_bowl", None, 0, "new", "Bowl chair with cushions", "cloth", 0.5, False),
 ("chair_woodframe", "sofa_lowpoly", "furn_mod_chair_woodframe", None, 0, "new", "Wood-frame armchair", "cloth", 0.45, False),
 ("chair_soviet", "old_soviet_chair", "furn_chair_soviet", None, 0, "new", "Wooden chair (old)", "wood", 0.4, False),
 ("stool_metal", "old_stool", "furn_stool_metal", 0.5, 0, "new", "Stool (wood seat, steel legs)", "wood", 0.3, False),
 ("tv_led", "led_tv", "furn_mod_tv_led", 0.734, 0, "new", "LED television", "metal", 0.0, False),
 ("tv_flat", "928c4eb3c875463bbde6fda85b7d8b3a", "furn_mod_tv_flat", 0.787, 0, "new", "Flat television on a stand", "metal", 0.0, False),
 ("tv_crt", "old_tv", "furn_mod_tv_crt", 0.514, 0, "new", "Old CRT television", "metal", 0.0, False),
 ("tv_bench", "tv_bench_tv_table_tv_stand_wood", "furn_mod_tv_bench", None, 90, "new", "TV bench (wood)", "wood", 0.5, False),
 ("cash_register", "cash_register", "furn_cash_register", None, 0, "new", "Cash register (mechanical)", "metal", 0.0, False),
 ("plant_pot", "plant_pot", "furn_mod_plant_pot", None, 0, "new", "Potted plant (medium)", "cloth", 0.0, False),
 ("piano", "black_piano", "furn_piano", 1.25, 0, "new", "Upright piano", "wood", 0.8, True),
 ("synth", "synthesizer_free", "furn_mod_synth", None, 0, "new", "Synthesizer on a stand", "metal", 0.5, False),
 ("sacks_burlap", "burlap_bags", "furn_sacks_burlap", None, 0, "new", "Burlap sacks (pile)", "cloth", 0.0, False),
]
BLOCKING = {k: True for k, *_ in SPEC}
for k in ("tv_led", "tv_flat", "tv_crt", "cash_register", "plant_pot"): BLOCKING[k] = False   # small pieces stand on tops / in corners
TOP_ITEM = {"tv_led", "tv_flat", "tv_crt", "cash_register"}   # placed on a surface (a stand, a counter)


def glb_meta(p):
    with open(p, "rb") as f:
        f.read(12); clen, _ = struct.unpack("<II", f.read(8)); return json.loads(f.read(clen))


def convert(dl, glbs):
    os.makedirs(glbs, exist_ok=True)
    for key, src, aid, h, yaw, role, *_ in SPEC:
        out = os.path.join(glbs, "frsrc_u_%s.glb" % key)
        if os.path.exists(out): continue
        args = [BLENDER, "-b", "--factory-startup", "--disable-autoexec", "--python", os.path.join(HERE, "convert_supplied.py"), "--", os.path.join(dl, src, src + ".glb"), out]
        args += ["--height", "%.4f" % h] if h else ["--scale", "1"]
        if yaw: args += ["--yaw", str(yaw)]
        r = subprocess.run(args, capture_output=True, text=True)
        line = [l for l in r.stdout.splitlines() if l.startswith("PR1 size")]
        print("%-18s %s" % (key, line[0] if line else "FAIL " + (r.stdout + r.stderr)[-300:]))


LOCAL_OK = ("CC0", "CC-BY-4.0", "CC-BY-3.0")   # committable licences; anything else stays local-only
STANDIN = {"cloth": [0.45, 0.42, 0.38], "wood": [0.32, 0.2, 0.12], "metal": [0.25, 0.26, 0.27]}
SURF = {"tv_bench": 0.0}   # pieces other things stand on (their top surface)


def write(dl, glbs):
    reg, rep_rows, credits = {}, {}, []
    for key, src, aid, h, yaw, role, name, family, front, wall in SPEC:
        side = json.load(open(os.path.join(glbs, "frsrc_u_%s.json" % key)))
        gj = glb_meta(os.path.join(dl, src, src + ".glb")); ex = gj.get("asset", {}).get("extras", {}) or {}
        acc = gj.get("accessors", [])
        tris = sum((acc[pr["indices"]]["count"] if "indices" in pr else acc[pr["attributes"]["POSITION"]]["count"]) // 3 for m in gj.get("meshes", []) for pr in m.get("primitives", []) if pr.get("mode", 4) == 4)
        lic = (ex.get("license") or "unknown").split(" (")[0]
        author = (ex.get("author") or "unknown").split(" (")[0]
        local = not any(lic.startswith(x) for x in LOCAL_OK)
        sx, sy, sz = side["size"]
        reg[key] = {"asset": aid, "source": src, "title": ex.get("title", src), "author": author, "license": lic, "url": ex.get("source", ""), "local": local,
                    "tris": tris, "size": side["size"], "colliders": side["colliders"], "maxTex": 2048 if max(sx, sz) > 1.2 else 1024,
                    "credit": "%s by %s, Sketchfab (%s), %s" % (ex.get("title", src), author, ex.get("source", ""), lic)}
        credits.append("| `%s` | %s | %s | %s | %s | %s |" % (aid, ex.get("title", src), author, lic, ex.get("source", ""), "no (local-only)" if local else "yes"))
        rep_rows[aid] = {"user": key, "why": "PR5: the user's model (%s)" % ex.get("title", src), **({"stretch": True} if role != "new" else {"exact": True})}
        if role != "new": continue
        blocking = BLOCKING[key]
        v = min(sx * sy * sz, 1.5)
        surf = [[round(-sx / 2 + 0.05, 3), round(-sz / 2 + 0.04, 3), round(sx / 2 - 0.05, 3), round(sz / 2 - 0.02, 3), round(sy, 3)]] if key in SURF else []
        prop = {"family": family, "rounds": round(3 + 24 * v), "blast": round(8 + 40 * v), "blocking": blocking, "wall": wall, "front": front, "mount": 0,
                "groups": ["body"], "fragments": [], "keep": [0], "surfaces": surf, "bounds": [[round(-sx / 2, 3), 0, round(-sz / 2, 3)], [round(sx / 2, 3), round(sy, 3), round(sz / 2, 3)]]}
        cols = side["colliders"] or [[0, sy / 2, 0, sx, sy, sz]]
        parts = [{"t": "b", "c": c[:3], "s": c[3:], "m": "standin", "bv": round(min(0.01, min(c[3:]) * 0.2), 4), "k": 0, "g": 0} for c in cols]   # the stand-in (shown until the bake is on disk)
        if blocking: parts += [{"t": "b", "c": [0, round(sy / 2, 3), 0], "s": [round(sx, 3), round(sy, 3), round(sz, 3)], "m": "standin", "v": 0}]
        cover = []
        if blocking and not wall and sy >= 0.7 and sx >= 0.8:
            ht = "high" if sy >= 1.3 else "low"
            cover = [{"p": [0, 0, round(sz / 2 + 0.45, 2)], "yaw": 180, "height": ht, "peek": ["over", "left", "right"]}, {"p": [0, 0, round(-sz / 2 - 0.45, 2)], "yaw": 0, "height": ht, "peek": ["over", "left", "right"]}]
        A = {"id": aid, "version": 1, "name": name, "category": "Furniture",
             "description": "%s: %s by %s (Sketchfab, %s)%s." % (name, ex.get("title", src), author, lic, "; local-only, the committed game shows a box stand-in" if local else ""),
             "footprint": [round(sx, 3), round(sz, 3)], "floors": 0, "enterable": False, "plinthDepth": 0, "maxGrade": 0.3, "noAutoPanels": True, "prop": prop,
             "nav": {"effect": "carve" if blocking else "none", "rooms": [], "doors": [], "entrances": [], "corners": [], "firingPoints": [], "cover": cover, "areas": []},
             "materials": [{"name": "standin", "albedo": STANDIN[family], "roughness": 0.8, "pattern": "plain", "metallic": 0, "grain": 0}],
             "parts": parts, "furniture": [], "interactables": [], "panels": []}
        open(os.path.join(ROOT, "content", "assets", aid + ".json"), "w", encoding="utf-8", newline="").write(json.dumps(A, separators=(",", ":")))
    json.dump(reg, open(REG, "w", encoding="utf-8"), indent=1, ensure_ascii=False)
    # furn_replace.json: these rows (furn_fit.py fits them; "user" reads the registry)
    fr = os.path.join(HERE, "furn_replace.json"); R = json.load(open(fr, encoding="utf-8"))
    R.update(rep_rows); json.dump(R, open(fr, "w", encoding="utf-8"), indent=1, ensure_ascii=False)
    # docs/credits.md: the table between the markers
    cp = os.path.join(ROOT, "docs", "credits.md"); doc = open(cp, encoding="utf-8").read()
    a, b = "<!-- user_models: written by tools/props/user_models.py -->", "<!-- /user_models -->"
    table = a + "\nFurniture models the user supplied (Sketchfab downloads; licence, author and source read from each file's own metadata). A local-only model is not in the repository: the committed game shows its box stand-in.\n\n| Asset | Model | Author | Licence | Source | In the repo |\n|---|---|---|---|---|---|\n" + "\n".join(credits) + "\n" + b
    doc = doc[:doc.index(a)] + table + doc[doc.index(b) + len(b):] if a in doc else doc.rstrip() + "\n\n" + table + "\n"
    open(cp, "w", encoding="utf-8").write(doc)
    # git-ignore the local-only bakes
    gi = os.path.join(ROOT, ".gitignore"); g = open(gi, encoding="utf-8").read() if os.path.exists(gi) else ""
    for key, r in reg.items():
        line = "content/assets/models/fr_%s/" % r["asset"][5:]
        if r["local"] and line not in g: g = g.rstrip("\n") + "\n# PR5: local-only model (its licence), see docs/credits.md\n" + line + "\n"
    open(gi, "w", encoding="utf-8", newline="").write(g)
    print(len(reg), "models;", sum(r["local"] for r in reg.values()), "local-only")


def main():
    if sys.argv[1] == "convert": return convert(sys.argv[2], sys.argv[3])
    if sys.argv[1] == "write": return write(sys.argv[2], sys.argv[3])


if __name__ == "__main__":
    main()
