"""PR7: the user's crafting-table pack (three tables, all smithing tables) and an abandoned security hut.

    python -I tools/props/pr7_assets.py convert <incoming dir> <glb dir>   # Blender (pr6_split.py): one GLB + side JSON per asset
    python -I tools/props/pr7_assets.py write <incoming dir> <glb dir>     # asset JSONs + docs/credits.md rows
then bake: NormandyMapmaker --import-weapon-models <glb dir> content.

Both are CC BY 4.0 (the user's statement; the hut's GLB metadata says the same): bakes committed.
Scale: the tables' worktops (0.965 units in the FBX) to 0.9 m; the hut's door (0.757 units) to 2.05 m.
The hut is a closed shell: its door is a panel on a solid wall (no opening), so it is solid, with cover round it.
"""
import json, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
BLENDER = r"C:\Program Files\Blender Foundation\Blender 5.2\blender.exe"
DESK = ("crafting-table-tech-desk/source_x/textures/Tech_Desk.fbx", "crafting-table-tech-desk/source_x/textures", 0.9 / 0.965)
HUT_K = 0.70 / 0.60   # the doorway: 0.60 m wide with the door at 2.05 m, too tight for the 0.6 m player capsule: the whole hut 1.17x
HUT = ("abandoned_security_hut_low_poly/abandoned_security_hut_low_poly.glb", None, 2.05 / 0.757 * HUT_K)
# The hut is open inside (the door leaf stands open): collision as boxes measured off the model (raycast, at the door-2.05 m
# scale, Blender axes x, y, z up: [x0, x1, y0, y1, z0, z1]), scaled by HUT_K; the front wall (-y, the game's +z) keeps a
# 0.83 m gap where the 0.70 m doorway is; the windows stay closed (glass).
HUT_BOXES = [
    [-1.711, 1.711, -2.708, 2.708, 0.0, 0.25],      # the kerbed pad (a 0.25 m step up)
    [-1.237, -0.36, -1.549, -1.219, 0.25, 2.5], [0.35, 1.237, -1.549, -1.219, 0.25, 2.5],   # front wall either side of the door
    [-0.36, 0.35, -1.549, -1.219, 2.25, 2.5],      # lintel over the door
    [-1.237, -0.907, -1.219, 1.279, 0.25, 2.5], [0.907, 1.237, -1.219, 1.279, 0.25, 2.5],   # side walls
    [-0.907, 0.907, 0.949, 1.279, 0.25, 2.5],      # back wall
    [-1.357, 1.357, -1.65, 1.399, 2.5, 2.945],     # roof slab and parapet
]
DESK_CREDIT = '"Crafting table - Tech Desk" (https://skfb.ly/osTKB) by IDALGAME, CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/); modified: split / baked / textures resized'
HUT_CREDIT = '"Abandoned Security Hut (Low Poly)" (https://skfb.ly/6TANP) by Berk Gedik, CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/); modified: split / baked / textures resized'
TABLES = [   # id, name, FBX object, its turn in the file (undone so the bench runs along x and faces +z)
    ("smithing_table_tech_desk", "Smithing table: drawer bench", "Tech_Desk_01", -53.0),
    ("smithing_table_tech_pegboard", "Smithing table: pegboard bench", "Tech_Desk_02", -47.7),
    ("smithing_table_tech_saw", "Smithing table: saw bench", "Tech_Desk_03", -26.4),
]
HUT_ID, HUT_NAME = "mod_security_hut", "Abandoned security hut"


def convert(dl, glbs):
    os.makedirs(glbs, exist_ok=True)
    for (src, texdir, scale), pieces in ((DESK, [{"id": a, "objects": [o], "colliders": "parts", "yaw": y} for a, _, o, y in TABLES]),
                                         (HUT, [{"id": HUT_ID, "objects": None, "colliders": "parts"}])):
        if pieces[0]["objects"] is None:   # the hut: every mesh in the file
            pieces[0]["objects"] = ["defaultMaterial"] + ["defaultMaterial.%03d" % k for k in range(1, 12)]
        job = {"src": os.path.join(dl, src), "texdir": os.path.join(dl, texdir) if texdir else None, "scale": scale, "out": glbs, "pieces": pieces}
        jp = os.path.join(glbs, "_job_%s.json" % pieces[0]["id"]); json.dump(job, open(jp, "w"))
        r = subprocess.run([BLENDER, "-b", "--factory-startup", "--disable-autoexec", "--python", os.path.join(HERE, "pr6_split.py"), "--", jp], capture_output=True, text=True)
        out = [l for l in r.stdout.splitlines() if l.startswith("PR6")]
        print("\n".join(out) if out else "FAILED " + (r.stdout + r.stderr)[-1500:])


def asset(aid, name, cat, desc, side, model, extra):
    sx, sy, sz = side["size"]
    cols = side["colliders"] or [[0, sy / 2, 0, sx, sy, sz]]
    A = {"id": aid, "version": 1, "name": name, "category": cat, "description": desc,
         "footprint": [round(sx, 3), round(sz, 3)], "floors": 0, "enterable": False, "plinthDepth": 0, "maxGrade": 1, "noAutoPanels": True,
         "model": {"pivot": "ground", "scale": 1, "jpgMaps": True, "lod1": 0.3, "src": aid + ".glb", "dir": aid, **model},
         "nav": {"effect": "carve", "rooms": [], "doors": [], "entrances": [], "corners": [], "firingPoints": [], "areas": [], "cover": []},
         "materials": [{"name": "collider", "albedo": [0.35, 0.27, 0.18], "roughness": 0.8, "pattern": "wood", "metallic": 0, "grain": 1}],
         "parts": [{"t": "b", "c": c[:3], "s": c[3:], "m": "collider", "v": 0} for c in cols], "furniture": [], "interactables": [], "panels": []}
    A.update(extra)
    open(os.path.join(ROOT, "content", "assets", aid + ".json"), "w", encoding="utf-8", newline="").write(json.dumps(A, separators=(",", ":")))
    return A


def write(dl, glbs):
    for aid, name, obj, _ in TABLES:
        side = json.load(open(os.path.join(glbs, aid + ".json"))); sx, sy, sz = side["size"]
        asset(aid, name, "Gameplay",
              "Weapon fitting bench (%.2f x %.2f m, worktop 0.9 m, %.2f m high). E at it opens the fitting screen, as the smithing table." % (sx, sz, sy),
              side, {"maxTex": 1024, "lodDist": 20, "cullDist": 250, "credit": DESK_CREDIT}, {"smithy": True})
    side = json.load(open(os.path.join(glbs, HUT_ID + ".json"))); sx, sy, sz = side["size"]
    k = HUT_K
    side["colliders"] = [[(x0 + x1) / 2 * k, (z0 + z1) / 2 * k, -(y0 + y1) / 2 * k, (x1 - x0) * k, (z1 - z0) * k, (y1 - y0) * k] for x0, x1, y0, y1, z0, z1 in HUT_BOXES]
    side["colliders"] = [[round(v, 3) for v in c] for c in side["colliders"]]
    cov = [{"p": [round(x, 2), 0, round(s * (sz / 2 + 0.5), 2)], "yaw": 180 if s > 0 else 0, "height": "high", "peek": ["left", "right"]} for x in (-sx / 4, sx / 4) for s in (1, -1)]
    A = asset(HUT_ID, HUT_NAME, "Small buildings",
              "A small concrete guard hut on a kerbed pad (%.1f x %.1f m, %.1f m to the antenna tip), its metal door standing open: walk in through the 0.7 m doorway; the windows are closed." % (sx, sz, sy),
              side, {"maxTex": 1024, "lodDist": 40, "cullDist": 600, "credit": HUT_CREDIT}, {"maxGrade": 2})
    A["nav"]["cover"] = cov
    open(os.path.join(ROOT, "content", "assets", HUT_ID + ".json"), "w", encoding="utf-8", newline="").write(json.dumps(A, separators=(",", ":")))
    # docs/credits.md: the PR7 table (keeps the file's own line endings)
    cp = os.path.join(ROOT, "docs", "credits.md"); doc = open(cp, encoding="utf-8", newline="").read(); nl = "\r\n" if "\r\n" in doc else "\n"
    a, b = "<!-- pr7_assets: written by tools/props/pr7_assets.py -->", "<!-- /pr7_assets -->"
    rows = ["| `%s` | Crafting table - Tech Desk | IDALGAME | CC-BY-4.0 | https://skfb.ly/osTKB | modified: split / baked / textures resized | yes |" % aid for aid, _, _, _ in TABLES]
    rows.append("| `%s` | Abandoned Security Hut (Low Poly) | Berk Gedik | CC-BY-4.0 | https://skfb.ly/6TANP | modified: split / baked / textures resized | yes |" % HUT_ID)
    table = a + "\nPR7: the user's crafting tables (three smithing tables) and security hut.\n\n| Asset | Model | Author | Licence | Source | Changes | In the repo |\n|---|---|---|---|---|---|---|\n" + "\n".join(rows) + "\n" + b
    doc = doc.replace("\r\n", "\n")
    doc = doc[:doc.index(a)] + table + doc[doc.index(b) + len(b):] if a in doc else doc.rstrip() + "\n\n" + table + "\n"
    open(cp, "w", encoding="utf-8", newline="").write(doc.replace("\n", nl))
    print("4 assets written")


if __name__ == "__main__":
    {"convert": convert, "write": write}[sys.argv[1]](sys.argv[2], sys.argv[3])
