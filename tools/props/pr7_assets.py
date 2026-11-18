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
# PR7 addition: the old garage (FBX, 3ds Max / Corona materials whose maps the importer drops: wired here from the FBX's own
# connections) and the Soviet gas station (a kiosk, two LPG dispensers, three drums; no canopy in the model).
GARAGE = ("old-garage/textures/old_garage.fbx", "old-garage/textures")
GARAGE_CREDIT = '"Old garage" (https://skfb.ly/6QSUn) by Yury Misiyuk, CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/); modified: baked / decimated / textures resized'
GAS = "abandoned_soviet_gas_station/abandoned_soviet_gas_station.glb"
GAS_CREDIT = '"Abandoned Soviet gas station" (https://skfb.ly/pzuX8) by olegolegperepelica, CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/); modified: baked / decimated / textures resized'
WALLS = {"Base Color": "mtZsAJ.jpg", "Normal": "1mtZsAJ.jpg"}
GARAGE_MATS = {"Material #2100603827": WALLS, "Material #2100603822": WALLS, "Material #2100603823": {"Base Color": "rough_tin_roof1_20.jpg"},
               "Material #2100603825": {"Base Color": "12258521_dsc-1424.jpg"}, "Material #2100603826": {"Base Color": "xSC76Y.jpg"},
               "Material #26": {"Base Color": "3628e0f98993a405be2808a6177835ef.jpg", "Normal": "7835ef.jpg"},
               "Material #2100603820": {"Base Color": "Vents0178_600.jpg", "Normal": "Vents017228_600.jpg"},
               "Material #2100603819": {"Base Color": "124.jpg", "Normal": "122.jpg"},
               "Material #2100603818": {"Base Color": "rusted_metal_door_from_venice_4_20131018_1.jpg", "Normal": "rusted_metal_door_from_venice_4.jpg"},
               "Material #2100603821": {"Base Color": "2.jpg", "Normal": "222.jpg"}, "Material #2100603824": {"Base Color": "шифер_23.jpg"}}
# collision boxes measured off the models (raycast; source axes, metres: [x0, x1, y0, y1, z0, z1])
GARAGE_BOXES = [[-2.22, 4.16, -4.14, 6.54, 0.0, 3.96], [-2.59, 4.56, -4.45, 6.85, 3.96, 4.39]]   # closed: both doors are shut panels
GO, GA, GD, GB = ("Abandoned Soviet gas station_" + n for n in ("Bilding_LPG_ON_0", "Atributs.002_0", "LPG_DETAILS.001_0", "Lpg_box.001_0"))
GAS_ALL = [GO, "Abandoned Soviet gas station_Bilding_LPG_IN_0", GA, GD, GB]
GAS_BOXES = [   # the kiosk is open: a 1.0 m doorway in its +x wall (the door leaf stands open outside it); windows closed
    [-2.41, -1.9, -4.91, 3.41, 0.0, 3.2], [-2.41, 1.91, -4.91, -4.5, 0.0, 3.2], [-2.41, 1.91, 3.0, 3.41, 0.0, 3.2],   # back and end walls
    [1.5, 1.91, -4.5, -1.5, 0.0, 3.2], [1.5, 1.91, -0.4, 3.0, 0.0, 3.2], [1.5, 1.91, -1.5, -0.4, 2.6, 3.2],          # front wall, door gap, lintel
    [-2.41, 1.91, -4.91, 3.41, 3.2, 3.58],                                                                            # roof
    [1.91, 2.75, -0.46, -0.03, 0.0, 2.9],                                                                             # the open door leaf
    [5.0, 5.99, 1.02, 2.0, 0.0, 2.26], [5.0, 5.99, -3.05, -2.06, 0.0, 2.26],                                          # the two dispensers
    [-0.28, 0.29, -6.3, -5.73, 0.0, 0.93], [-0.05, 0.52, -5.59, -5.02, 0.0, 0.93], [0.65, 1.22, -5.85, -5.28, 0.0, 0.93]]   # three drums
EXTRA = [   # id, name, category, source, selection / options, credit, maxTex
    ("mod_old_garage", "Old garage", "Small buildings", "garage", {"objects": ["Box001"], "boxes": GARAGE_BOXES}, GARAGE_CREDIT, 2048),
    ("mod_gas_station_soviet", "Abandoned Soviet gas station", "Small buildings", "gas", {"objects": GAS_ALL, "yaw": -90, "boxes": GAS_BOXES}, GAS_CREDIT, 2048),
    ("mod_lpg_dispenser", "LPG dispenser (model)", "Modern: Urban", "gas", {"multi": [[GB, [0]], [GD, [0, 2, 4, 6, 8]]], "yaw": -90, "colliders": "box"}, GAS_CREDIT, 1024),
    ("mod_lpg_dispensers_pair", "LPG dispensers, pair (model)", "Modern: Urban", "gas", {"objects": [GB, GD], "yaw": -90, "colliders": "parts"}, GAS_CREDIT, 1024),
    ("mod_gas_drums", "Oil drums, three (model)", "Modern: Industrial", "gas", {"mesh": GA, "clusters": [7, 8, 9], "yaw": -90, "colliders": "parts"}, GAS_CREDIT, 1024),
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


def convert_extra(dl, glbs):
    for key, src, scale, more in (("garage", GARAGE[0], 1.0, {"texfolder": os.path.join(dl, GARAGE[1]), "matmap": GARAGE_MATS}), ("gas", GAS, 1.0, {})):
        pieces = [{"id": aid, **sel} for aid, _, _, k, sel, _, _ in EXTRA if k == key and not os.path.exists(os.path.join(glbs, aid + ".glb"))]
        if not pieces: continue
        job = {"src": os.path.join(dl, src), "scale": scale, "out": glbs, "pieces": pieces, **more}
        jp = os.path.join(glbs, "_job_%s.json" % key); json.dump(job, open(jp, "w", encoding="utf-8"), ensure_ascii=False)
        r = subprocess.run([BLENDER, "-b", "--factory-startup", "--disable-autoexec", "--python", os.path.join(HERE, "pr6_split.py"), "--", jp], capture_output=True, text=True, encoding="utf-8", errors="replace")
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
    for aid, name, cat, key, sel, credit, mt in EXTRA:
        side = json.load(open(os.path.join(glbs, aid + ".json"))); sx, sy, sz = side["size"]
        building = cat == "Small buildings"
        desc = {"mod_old_garage": "A corrugated-roofed brick garage (%.1f x %.1f m, %.1f m to the ridge) with a vehicle door and a person door; both doors are shut: a closed shell, solid cover.",
                "mod_gas_station_soviet": "A derelict Soviet roadside LPG station (%.1f x %.1f m, %.1f m to the lamp post): a kiosk with an open doorway and empty window frames, two dispensers and three drums. The model has no canopy.",
                }.get(aid, name.replace(" (model)", "") + ", %.2f x %.2f x %.2f m (Abandoned Soviet gas station by olegolegperepelica).")
        A = asset(aid, name, cat, desc % (sx, sz, sy), side, {"maxTex": mt, "lodDist": 40 if building else 15, "cullDist": 600 if building else 300, "credit": credit},
                  {"maxGrade": 2 if building else 5})
        if building:
            A["nav"]["cover"] = [{"p": [round(x, 2), 0, round(sg * (sz / 2 + 0.5), 2)], "yaw": 180 if sg > 0 else 0, "height": "high", "peek": ["left", "right"]} for x in (-sx / 4, sx / 4) for sg in (1, -1)]
        else:
            A["nav"]["effect"] = "none"
        open(os.path.join(ROOT, "content", "assets", aid + ".json"), "w", encoding="utf-8", newline="").write(json.dumps(A, separators=(",", ":")))
    # docs/credits.md: the PR7 table (keeps the file's own line endings)
    cp = os.path.join(ROOT, "docs", "credits.md"); doc = open(cp, encoding="utf-8", newline="").read(); nl = "\r\n" if "\r\n" in doc else "\n"
    a, b = "<!-- pr7_assets: written by tools/props/pr7_assets.py -->", "<!-- /pr7_assets -->"
    rows = ["| `%s` | Crafting table - Tech Desk | IDALGAME | CC-BY-4.0 | https://skfb.ly/osTKB | modified: split / baked / textures resized | yes |" % aid for aid, _, _, _ in TABLES]
    rows.append("| `%s` | Abandoned Security Hut (Low Poly) | Berk Gedik | CC-BY-4.0 | https://skfb.ly/6TANP | modified: split / baked / textures resized | yes |" % HUT_ID)
    rows.append("| `mod_old_garage` | Old garage | Yury Misiyuk | CC-BY-4.0 | https://skfb.ly/6QSUn | modified: baked / decimated / textures resized | yes |")
    rows += ["| `%s` | Abandoned Soviet gas station | olegolegperepelica | CC-BY-4.0 | https://skfb.ly/pzuX8 | modified: baked / decimated / textures resized | yes |" % aid for aid, _, _, k, _, _, _ in EXTRA if k == "gas"]
    table = a + "\nPR7: the user's crafting tables (three smithing tables), security hut, old garage and Soviet gas station.\n\n| Asset | Model | Author | Licence | Source | Changes | In the repo |\n|---|---|---|---|---|---|---|\n" + "\n".join(rows) + "\n" + b
    doc = doc.replace("\r\n", "\n")
    doc = doc[:doc.index(a)] + table + doc[doc.index(b) + len(b):] if a in doc else doc.rstrip() + "\n\n" + table + "\n"
    open(cp, "w", encoding="utf-8", newline="").write(doc.replace("\n", nl))
    print("4 assets written")


if __name__ == "__main__":
    {"convert": lambda d, g: (convert(d, g), convert_extra(d, g)), "write": write}[sys.argv[1]](sys.argv[2], sys.argv[3])
