"""PR9: debris props from three packs the user supplied: a crushed soft-drink can, ten low-poly hand tools (one asset per
tool, scaled to its real length) and a small potbelly stove (its flue cut short above the body).

    python -I tools/props/pr9_assets.py convert <incoming dir> <glb dir>   # Blender (pr6_split.py): one GLB + side JSON per asset
    python -I tools/props/pr9_assets.py write <incoming dir> <glb dir>     # asset JSONs + .gitignore rows
then bake: NormandyMapmaker --import-weapon-models <glb dir> content.

No licence, author or source text in any of the three zips: the models are LOCAL-ONLY (bakes git-ignored); the committed
game shows box stand-ins ("mf" parts) until credits are known. Category "Town clutter" (the palette's small-debris set).
"""
import json, os, subprocess, sys, xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
BLENDER = r"C:\Program Files\Blender Foundation\Blender 5.2\blender.exe"
CAT = "Town clutter"
CAN_DAE = "wasteland-explorer-soft-drink-can_src/model/model.dae"   # inside the pack's nested source zip
CAN_TEX = "wasteland-explorer-soft-drink-can/textures"
TOOLS_FBX = "lowpoly-tools-free-download_src/lowpoly_tools_free/lowpoly_tools.fbx"
TOOLS_TEX = "lowpoly-tools-free-download/textures"   # the pack's 512 px maps (the nested zip's copies are smaller files)
STOVE_FBX, STOVE_TEX = "small-stove/source/urjuika_uv_002.fbx", "small-stove/textures"
# the stove (FBX units after import): body 0.1315 tall, flue 0.025 wide up to 0.526. Body to 0.60 m (a small burzhuika);
# the flue cut 0.45 m above the body: 1.05 m overall
STOVE_K = 0.60 / 0.1315; STOVE_CUT = 0.1315 + 0.45 / STOVE_K
TOOLS = [   # id, name, mesh / material stem, real overall length (m)
    ("mod_tool_hammer_club", "Club hammer", "hammer1", 0.28),
    ("mod_tool_hammer_claw", "Claw hammer", "hammer2", 0.33),
    ("mod_tool_hatchet", "Hatchet", "hatchet", 0.40),
    ("mod_tool_clamp", "F-clamp", "clamp", 0.25),
    ("mod_tool_pincers", "End-cutting pincers", "pliers", 0.20),
    ("mod_tool_screwdriver", "Screwdriver", "screwdriver", 0.22),
    ("mod_tool_prybar", "Pry bar", "prybar", 0.38),
    ("mod_tool_wrench", "Open-end wrench", "wrench", 0.22),
    ("mod_tool_adjustable_wrench", "Adjustable wrench", "adjustable_wrench", 0.25),
    ("mod_tool_socket_wrench", "L-handled socket wrench", "socket_wrench", 0.25),
]
CAN = ("mod_debris_soda_can", "Soft-drink can, crushed", 0.122)   # a 330 ml can lying on its side
STOVE = ("mod_debris_stove", "Potbelly stove, flue cut short")
PACKS = {"can": "wasteland-explorer-soft-drink-can.zip", "tools": "lowpoly-tools-free-download.zip", "stove": "small-stove.zip", "paper": "impostors-paper.zip"}
# the paper pack (two FBX in its nested source zip, one diffuse map): sheets about 0.5 units long, to A4 (0.297 m)
PAPER_DIR, PAPER_TEX, PAPER_K = "impostors-paper_src", "impostors-paper_src/T_Mole_CSI_ScatteredPapers_D.png", 0.6
PAPERS = [("mod_debris_papers_scattered", "Papers, scattered", ("ImpostorsPaperScattered.fbx", "SM_Mole_ScatteredPapers_A.mo"), "Loose sheets of paper strewn across the floor"),
          ("mod_debris_papers_pile", "Papers, small pile", ("ImpostorsPaperScatteredPile.fbx", "SM_Mole_ScatteredPapers_Pile_A.mo"), "A few crumpled sheets of paper in a pile")]


def dae_to_obj(src, out):
    """The can ships as Collada (an Assimp export: one polylist, one index per corner); Blender 5 has no .dae importer."""
    t = ET.parse(src).getroot(); N = t.tag.split("}")[0] + "}"
    g = next(t.iter(N + "geometry")); gid = g.get("id")
    arr = {s.get("id"): [float(x) for x in s.find(N + "float_array").text.split()] for s in g.iter(N + "source")}
    P, Nn, T = arr[gid + "-positions"], arr[gid + "-normals"], arr[gid + "-tex0"]
    pl = next(g.iter(N + "polylist")); assert len(pl.findall(N + "input")) == 1, "expected one shared index per corner"
    vc = [int(x) for x in pl.find(N + "vcount").text.split()]; ix = [int(x) for x in pl.find(N + "p").text.split()]
    assert sum(vc) == len(ix) and max(ix) < len(P) // 3
    L = ["mtllib can.mtl", "usemtl defaultMat"] + ["v %f %f %f" % tuple(P[i:i + 3]) for i in range(0, len(P), 3)]
    L += ["vt %f %f" % tuple(T[i:i + 2]) for i in range(0, len(T), 2)] + ["vn %f %f %f" % tuple(Nn[i:i + 3]) for i in range(0, len(Nn), 3)]
    k = 0
    for n in vc: L.append("f " + " ".join("%d/%d/%d" % ((ix[k + j] + 1,) * 3) for j in range(n))); k += n
    open(out, "w").write("\n".join(L) + "\n")
    open(os.path.join(os.path.dirname(out), "can.mtl"), "w").write("newmtl defaultMat\n")


def run(job, glbs, key):
    jp = os.path.join(glbs, "_job_%s.json" % key); json.dump(job, open(jp, "w"))
    r = subprocess.run([BLENDER, "-b", "--factory-startup", "--disable-autoexec", "--python", os.path.join(HERE, "pr6_split.py"), "--", jp], capture_output=True, text=True, encoding="utf-8", errors="replace")
    out = [l for l in r.stdout.splitlines() if l.startswith("PR6")]
    print("\n".join(out) if out else key + " FAILED " + (r.stdout + r.stderr)[-1500:])


def convert(dl, glbs):
    os.makedirs(glbs, exist_ok=True)
    obj = os.path.join(glbs, "_can", "can.obj"); os.makedirs(os.path.dirname(obj), exist_ok=True)
    dae_to_obj(os.path.join(dl, CAN_DAE), obj)
    run({"src": obj, "texdir": os.path.join(dl, CAN_TEX), "out": glbs, "pieces": [{"id": CAN[0], "objects": ["can"], "length": CAN[2], "colliders": "box"}]}, glbs, "can")
    run({"src": os.path.join(dl, TOOLS_FBX), "out": glbs, "texfolder": os.path.join(dl, TOOLS_TEX),
         "matmap": {s + "_LOD3": {"Base Color": s + "_LOD3_albedo.jpeg", "Normal": s + "_LOD3_normal.jpeg", "Roughness": s + "_LOD3_roughness.jpeg", "Metallic": s + "_LOD3_metallic.jpeg"} for _, _, s, _ in TOOLS},
         "pieces": [{"id": aid, "objects": [s + "_lod3"], "length": ln, "colliders": "box"} for aid, _, s, ln in TOOLS]}, glbs, "tools")
    run({"src": os.path.join(dl, STOVE_FBX), "texdir": os.path.join(dl, STOVE_TEX), "out": glbs,
         "pieces": [{"id": STOVE[0], "objects": ["oven", "door_up", "door_down"], "cut_above": STOVE_CUT, "scale": STOVE_K, "colliders": "parts"}]}, glbs, "stove")
    for aid, _, f, _ in PAPERS:
        run({"src": os.path.join(dl, PAPER_DIR, f[0]), "tex": os.path.join(dl, PAPER_TEX), "out": glbs, "pieces": [{"id": aid, "objects": [f[1]], "scale": PAPER_K, "colliders": "box"}]}, glbs, aid)


def write(dl, glbs):
    rows = [(CAN[0], CAN[1], "can", 512, "A crushed, faded soft-drink can lying on its side")]
    rows += [(aid, name, "tools", 512, "%s lying flat" % name) for aid, name, _, _ in TOOLS]
    rows.append((STOVE[0], STOVE[1], "stove", 1024, "A small cast-iron potbelly stove with its flue pipe cut short above the body"))
    rows += [(aid, name, "paper", 512, what) for aid, name, _, what in PAPERS]
    gi_add = []
    for aid, name, key, mt, what in rows:
        side = json.load(open(os.path.join(glbs, aid + ".json"))); sx, sy, sz = side["size"]
        cols = side["colliders"] or [[0, sy / 2, 0, sx, sy, sz]]
        if key == "stove": cols = cols[:2]   # the body (feet to top) and the flue stub; the doors and handles stand 2 cm proud
        credit = "%s supplied by the user; author, source and licence not in the files: local only" % PACKS[key]
        # paper: a stand-in that is walked over and shot through (collide false)
        A = {"id": aid, "version": 1, "name": name + " (model)", "category": CAT,
             "description": "%s, %.2f x %.2f x %.2f m (local only: the committed game shows a box stand-in)." % (what, sx, sz, sy),
             "footprint": [round(sx, 3), round(sz, 3)], "floors": 0, "enterable": False, "plinthDepth": 0, "maxGrade": 5, "noAutoPanels": True,
             "model": {"pivot": "ground", "maxTex": mt, "simplify": 1, "lod1": 0.3, "lodDist": 15 if key == "stove" else 8, "cullDist": 300 if key == "stove" else 120,
                       "src": aid + ".glb", "dir": aid, "jpgMaps": True, "scale": 1, "credit": credit},
             "nav": {"effect": "none", "rooms": [], "doors": [], "entrances": [], "corners": [], "firingPoints": [], "areas": [], "cover": []},
             "materials": [{"name": "collider", "albedo": [0.4, 0.4, 0.38], "roughness": 0.9, "pattern": "concrete", "metallic": 0, "grain": 1, "texture": "concrete_aged"}],
             "parts": [{"t": "b", "c": c[:3], "s": c[3:], "m": "collider", "mf": 1, **({"collide": False} if key == "paper" else {})} for c in cols], "furniture": [], "interactables": [], "panels": []}
        open(os.path.join(ROOT, "content", "assets", aid + ".json"), "w", encoding="utf-8", newline="").write(json.dumps(A, separators=(",", ":")))
        gi_add.append("content/assets/models/%s/" % aid)
        print("%-28s %.3f x %.3f x %.3f m  tris %d  boxes %d" % (aid, sx, sz, sy, side["tris"], len(cols)))
    gi = os.path.join(ROOT, ".gitignore"); g = open(gi, encoding="utf-8").read()
    new = [l for l in gi_add if l not in g]
    if new: g = g.rstrip("\n") + "\n# PR9: local-only debris models (no licence in the packs)\n" + "\n".join(new) + "\n"
    open(gi, "w", encoding="utf-8", newline="").write(g)
    print(len(rows), "assets written (local only)")


if __name__ == "__main__":
    {"convert": convert, "write": write}[sys.argv[1]](sys.argv[2], sys.argv[3])
