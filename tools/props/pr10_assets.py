"""PR10: post-apocalyptic buildings and props from the user's batch (stage A: the concrete outbuilding).

    python -I tools/props/pr10_assets.py convert <incoming dir> <glb dir>   # Blender (pr6_split.py): one GLB + side JSON per asset
    python -I tools/props/pr10_assets.py write <incoming dir> <glb dir>     # asset JSONs, credits rows, .gitignore
then bake: NormandyMapmaker --import-weapon-models <glb dir> content.

Every building is enterable the PR8 way (pr8_openings.py): the model's door panel deleted and a game door fitted, window
bays cut through the collision boxes, a walkable floor collider; the floor is a collider only, so the auto-furnisher
leaves the building to its five authored post-apocalyptic pieces ("companions", removable like any placed piece).
Licences not confirmed: the models are LOCAL-ONLY (bakes git-ignored, the committed game shows box stand-ins, "mf").
Measurements: E:/nmk/pr10scripts walls.py / zscan.py / down.py / inside.py (raycasts on the source), source units, z up.
"""
import json, os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pr8_openings

HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
BLENDER = r"C:\Program Files\Blender Foundation\Blender 5.2\blender.exe"
THEMES = ["postapoc"]


def pbr(folder, stem):   # the rar pack's per-material folders: <Mat>/LittleBuilding_LittleBuilding_<Mat>_<map>
    f = "%s/LittleBuilding_LittleBuilding_%s_" % (folder, stem)
    return {"Base Color": f + "BaseColor.jpg", "Normal": f + "Normal.jpg", "Roughness": f + "Rough.png"}


# buildings: walls / roof / floor / openings as boxes in the source ([x0, x1, y0, y1, z0, z1]); the door front is -y
BUILDINGS = {
    "pa_concrete_outbuilding": dict(
        name="Abandoned concrete outbuilding", pack="abounded-house.zip (Sketchfab.rar: LittleBuilding.fbx)",
        src="abounded-house/source/rar_x/Sketchfab/LittleBuilding.fbx", texfolder="abounded-house/source/rar_x/Sketchfab",
        matmap={"LittleBuilding_" + m: pbr(m, m) for m in ("Concrete", "Door", "Metal", "Roof", "Wood")},
        delete_mats=["LittleBuilding_Door"], maxTex=2048,
        desc="A one-room concrete outbuilding with a corrugated roof, graffiti and a boarded back window: enterable through a steel door (E).",
        walls=[[-4.19, 4.19, -2.17, -1.86, 0.0, 3.13], [-4.19, 4.19, 1.96, 2.27, 0.0, 3.13],          # front, back
               [-4.19, -3.88, -1.86, 1.96, 0.0, 3.13], [3.88, 4.19, -1.86, 1.96, 0.0, 3.13],           # ends
               [-4.54, 4.54, -2.65, 2.65, 3.06, 3.6]],                                                   # roof slab
        doors={"door": ("door", "door_steel", 1, [-2.79, -1.59, -2.17, -2.11, 0.2, 2.46])},
        holes={"window_back": [-0.39, 2.61, 1.9, 2.33, 1.05, 2.4]},                                       # boarded bay: the boards stay
        floor=[-3.88, 3.88, -2.17, 1.96, 0.0, 0.2],
        furnish=[("ph_worn_metal_rack", 3.45, 0.2, 90), ("ph_old_bed_frame", -3.38, 0.6, 0), ("mod_debris_stove", 2.9, -1.3, 0),
                 ("furn_chair_soviet", 0.3, -0.6, 30), ("furn_debris_plaster", 1.3, 0.9, 0)]),
}

# props: id -> name, pack, source, selection, scale rule, colliders, palette group, maxTex, description
PROPS = {
    "pa_crate_slatted": dict(name="Slatted crate", pack="box.zip (Cinema 4D export)", src="box/source/box.zip_x/box/box.fbx",
                             texfolder="box/textures", matmap={"box_mat": {"Base Color": "wood97.jpeg", "Normal": "wood97_normal.jpeg"}},
                             piece={"height": 0.9, "colliders": "box"}, group="Small objects", maxTex=1024,
                             desc="A weathered slatted wooden crate with X-braced sides and a loose lid"),
}


def mark(x, y, z=0.2):
    return [x - 0.01, x + 0.01, y - 0.01, y + 0.01, z, z + 0.01]


def convert(dl, glbs):
    os.makedirs(glbs, exist_ok=True)
    for bid, B in BUILDINGS.items():
        if os.path.exists(os.path.join(glbs, bid + ".glb")): continue
        marks = {**{k: v[3] for k, v in B["doors"].items()}, **B["holes"], "floor": B["floor"],
                 **{"f%d" % i: mark(x, y, B["floor"][5]) for i, (_, x, y, _) in enumerate(B["furnish"])}}
        piece = {"id": bid, "objects": None, "boxes": B["walls"], "marks": marks, "delete_mats": B["delete_mats"], "scale": B.get("scale", 1.0), "yaw": B.get("yaw", 0)}
        job = {"src": os.path.join(dl, B["src"]), "out": glbs, "pieces": [piece]}
        if B.get("matmap"): job.update(texfolder=os.path.join(dl, B["texfolder"]), matmap=B["matmap"])
        run(job, glbs, bid)
    for pid, P in PROPS.items():
        if os.path.exists(os.path.join(glbs, pid + ".glb")): continue
        job = {"src": os.path.join(dl, P["src"]), "out": glbs, "pieces": [{"id": pid, "objects": P.get("objects"), **P["piece"]}]}
        if P.get("matmap"): job.update(texfolder=os.path.join(dl, P["texfolder"]), matmap=P["matmap"])
        run(job, glbs, pid)


def run(job, glbs, key):
    jp = os.path.join(glbs, "_job_%s.json" % key); json.dump(job, open(jp, "w"))
    r = subprocess.run([BLENDER, "-b", "--factory-startup", "--disable-autoexec", "--python", os.path.join(HERE, "pr6_split.py"), "--", jp], capture_output=True, text=True, encoding="utf-8", errors="replace")
    out = [l for l in r.stdout.splitlines() if l.startswith("PR6")]
    print("\n".join(out) if out else key + " FAILED " + (r.stdout + r.stderr)[-1500:])


def write(dl, glbs):
    gi_add, rows = [], []
    for bid, B in BUILDINGS.items():
        side = json.load(open(os.path.join(glbs, bid + ".json"))); sx, sy, sz = side["size"]; m = side["marks"]
        A = {"id": bid, "version": 1, "name": B["name"], "category": "Small buildings", "themes": THEMES,
             "description": "%s %.1f x %.1f m, %.1f m high (local only: the committed game shows box stand-ins)." % (B["desc"], sx, sz, sy),
             "footprint": [round(sx, 3), round(sz, 3)], "floors": 0, "enterable": False, "plinthDepth": 0, "maxGrade": 2, "noAutoPanels": True,
             "model": {"pivot": "ground", "scale": 1, "jpgMaps": True, "lod1": 0.3, "src": bid + ".glb", "dir": bid, "maxTex": B["maxTex"], "lodDist": 40, "cullDist": 600,
                       "credit": "%s supplied by the user; licence to be confirmed: local only" % B["pack"]},
             "nav": {"effect": "carve", "rooms": [], "doors": [], "entrances": [], "corners": [], "firingPoints": [], "areas": [], "cover": []},
             "materials": [{"name": "collider", "albedo": [0.4, 0.4, 0.38], "roughness": 0.9, "pattern": "concrete", "metallic": 0, "grain": 1, "texture": "concrete_aged"}],
             "parts": [{"t": "b", "c": c[:3], "s": c[3:], "m": "collider", "mf": 1} for c in side["colliders"]], "furniture": [], "interactables": [], "panels": []}
        pr8_openings.apply(A, m, {k: v[:3] + v[4:] for k, v in B["doors"].items()}, holes=list(B["holes"]), floor_visible=False)
        A["companions"] = [{"asset": a, "pos": [round(m["f%d" % i][0], 3), round(m["f%d" % i][1] - 0.005, 3), round(m["f%d" % i][2], 3)], "yaw": yaw}
                           for i, (a, _, _, yaw) in enumerate(B["furnish"])]
        assert len(A["companions"]) <= 5
        A["nav"]["cover"] = [{"p": [round(x, 2), 0, round(s * (sz / 2 + 0.5), 2)], "yaw": 180 if s > 0 else 0, "height": "high", "peek": ["left", "right"]} for x in (-sx / 4, sx / 4) for s in (1, -1)]
        open(os.path.join(ROOT, "content", "assets", bid + ".json"), "w", encoding="utf-8", newline="").write(json.dumps(A, separators=(",", ":")))
        gi_add.append("content/assets/models/%s/" % bid)
        rows.append("| `%s` | %s | to be confirmed | to be confirmed | - | modified: openings cut, colliders, baked / textures resized | no (local-only) |" % (bid, B["pack"]))
        print("%-28s %.2f x %.2f x %.2f m  tris %d  parts %d  doors %d  holes %d  furnish %d" % (bid, sx, sz, sy, side["tris"], len(A["parts"]), len(A["interactables"]), len(B["holes"]), len(A["companions"])))
    for pid, P in PROPS.items():
        side = json.load(open(os.path.join(glbs, pid + ".json"))); sx, sy, sz = side["size"]
        A = {"id": pid, "version": 1, "name": P["name"] + " (model)", "category": P.get("category", "Town clutter"), "themes": P.get("themes", THEMES), "group": P["group"],
             "description": "%s, %.2f x %.2f x %.2f m (local only: the committed game shows a box stand-in)." % (P["desc"], sx, sz, sy),
             "footprint": [round(sx, 3), round(sz, 3)], "floors": 0, "enterable": False, "plinthDepth": 0, "maxGrade": 5, "noAutoPanels": True,
             "model": {"pivot": "ground", "maxTex": P["maxTex"], "simplify": 1, "lod1": 0.3, "lodDist": P.get("lodDist", 15), "cullDist": P.get("cullDist", 300),
                       "src": pid + ".glb", "dir": pid, "jpgMaps": True, "scale": 1, "credit": "%s supplied by the user; licence to be confirmed: local only" % P["pack"]},
             "nav": {"effect": P.get("nav", "none"), "rooms": [], "doors": [], "entrances": [], "corners": [], "firingPoints": [], "areas": [], "cover": []},
             "materials": [{"name": "collider", "albedo": [0.4, 0.4, 0.38], "roughness": 0.9, "pattern": "concrete", "metallic": 0, "grain": 1, "texture": "concrete_aged"}],
             "parts": [{"t": "b", "c": c[:3], "s": c[3:], "m": "collider", "mf": 1, **P.get("part", {})} for c in (side["colliders"] or [[0, sy / 2, 0, sx, sy, sz]])],
             "furniture": [], "interactables": [], "panels": []}
        A.update(P.get("extra", {}))
        open(os.path.join(ROOT, "content", "assets", pid + ".json"), "w", encoding="utf-8", newline="").write(json.dumps(A, separators=(",", ":")))
        gi_add.append("content/assets/models/%s/" % pid)
        rows.append("| `%s` | %s | to be confirmed | to be confirmed | - | modified: scaled / baked / textures resized | no (local-only) |" % (pid, P["pack"]))
        print("%-28s %.2f x %.2f x %.2f m  tris %d  colliders %d" % (pid, sx, sz, sy, side["tris"], len(A["parts"])))
    gi = os.path.join(ROOT, ".gitignore"); g = open(gi, encoding="utf-8").read()
    new = [l for l in gi_add if l not in g]
    if new: g = g.rstrip("\n") + "\n# PR10: local-only models (licences to be confirmed)\n" + "\n".join(new) + "\n"
    open(gi, "w", encoding="utf-8", newline="").write(g)
    cp = os.path.join(ROOT, "docs", "credits.md"); doc = open(cp, encoding="utf-8", newline="").read(); nl = "\r\n" if "\r\n" in doc else "\n"
    a, b = "<!-- pr10_assets: written by tools/props/pr10_assets.py -->", "<!-- /pr10_assets -->"
    table = a + "\nPR10: the user's post-apocalyptic batch. Licences to be confirmed: local-only until then.\n\n| Asset | Model | Author | Licence | Source | Changes | In the repo |\n|---|---|---|---|---|---|---|\n" + "\n".join(rows) + "\n" + b
    doc = doc.replace("\r\n", "\n")
    doc = doc[:doc.index(a)] + table + doc[doc.index(b) + len(b):] if a in doc else doc.rstrip() + "\n\n" + table + "\n"
    open(cp, "w", encoding="utf-8", newline="").write(doc.replace("\n", nl))


if __name__ == "__main__":
    {"convert": convert, "write": write}[sys.argv[1]](sys.argv[2], sys.argv[3])
