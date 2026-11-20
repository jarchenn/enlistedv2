# FL1, Blender background: one of the user's plant / stone packs -> two GLBs for the game's importer, every plant
# anchored at its own origin (trunk base centre on the ground) and named <asset>__<k>:
#   blender -b --factory-startup --disable-autoexec --python tools/fl1_flora_export.py -- <pack.glb> <pack key> <out dir>
#   <out>/<key>.glb       LOD0 of every plant of the pack
#   <out>/<key>_lod1.glb  the pack's own LOD2 where it has LODs, else a decimated copy
#   <out>/<key>_bake.json the importer offset (its "ground" pivot then moves nothing) and per-plant size / triangles
# then NormandyMapmaker --import-weapon-models <out> content (content/assets/fl_<key>_pack.json) and tools/fl1_flora.py.
# Packs and what is taken from each: PACKS below. Billboards, seasonal copies, demo grounds, scale men and checkers are
# left out; succulent spines (thousands of tiny meshes) are dropped and the plants decimated.
import bpy, bmesh, sys, os, json, re
import numpy as np
from mathutils import Matrix, Vector


def lolipop(names, lod1="LOD2"):
    return [{"id": aid, "lod0": [r"^%s_LOD0$" % re.escape(src)], "lod1": [r"^%s_%s$" % (re.escape(src), lod1)]} for aid, src in names]


PACKS = {
    "oak": {"unit": 1.0, "lod1Keep": 0.6, "plants": lolipop([("big_%d" % k, "Big_oak_tree__00%d" % k) for k in range(1, 5)] + [("large_%d" % k, "Large_oak_tree_00%d" % k) for k in range(1, 5)]
                                           + [("med_%d" % k, "Medium_oak_tree__00%d" % k) for k in range(1, 4)] + [("small_%d" % k, "Small_oak_tree__00%d" % k) for k in range(1, 4)]
                                           + [("sapling_%d" % k, "Sapling_oak_tree_00%d" % k) for k in range(1, 4)])},
    "maple": {"unit": 1.0, "lod1Keep": 0.6, "plants": lolipop([("%s_%d" % (s.lower(), k), "Acer_%s_%d" % (s, k)) for s in ("large", "medium", "small", "Sapling") for k in range(1, 4)])},
    "lilac": {"unit": 1.0, "plants": lolipop([("%s_%d" % (s.lower(), k), "Lilac_%s_%d" % (s, k)) for s in ("bush", "sapling", "single_tree", "small_bush") for k in range(1, 4)])},
    "bush": {"unit": 1.0, "plants": lolipop([("%s_%d" % (s, k), "Bush_%s_00%d" % (s, k)) for s in ("big", "medium", "small") for k in range(1, 6)])},
    # DJMaesen's packs (centimetres): a trunk mesh and its branch-card mesh, paired by position
    "gnarly": {"unit": 0.01, "pairs": [r"^tree\d$", r"^branches(_\d)?$"], "decimate": 0.3, "maxTris": 9000},
    "bare": {"unit": 0.01, "pairs": [r"^tree\d$", r"^branches(_\d)?$"], "decimate": 0.35},
    "deadbranch": {"unit": 0.01, "pairs": [r"^tree\d$", r"^branch(_+\d?)?$"], "decimate": 0.35},
    "clover": {"unit": 1.0, "plants": [{"id": "patch_%d" % k, "lod0": [r"^%s$" % n]} for k, n in enumerate(["D01", "D01_01", "D01_02", "D01_03"], 1)],
               "maxTris": 3000, "decimate": 0.3},
    "succulent": {"unit": 1.0, "dropMats": ["Spires", "Gravel"], "maxTris": 6000, "decimate": 0.3,
                  "plants": [{"id": "columns", "lod0": [r"^Cactus1(\.\d+)?$"]}, {"id": "ball", "lod0": [r"^Cactus Ball1$"]},
                             {"id": "agave", "lod0": [r"^Plant\d$"]}, {"id": "prickly_pear", "lod0": [r"^Pear Cactus( Fruit)?\d$"]}]},
    "stone": {"unit": 1.0, "loose": 0.5, "maxTris": 4000, "decimate": 0.35},
    "grass": {"unit": 1.0, "plants": [{"id": "reeds", "lod0": [r"^Grass_Reeds"]}], "decimate": 0.5},
    # DF1: evolveduk's desert shrubs (each mesh holds several plants of one kind; cholla and creosote stand on a bark mesh)
    "desertshrub": {"unit": 1.0, "cluster": {"agave": "agave", "yacca": "yucca", "ocotillo": "ocotillo", "creosote": "creosote", "cholla": "cholla"}, "decimate": 0.4},
    "drygrass": {"unit": 0.01, "plants": [{"id": "patch_%d" % k, "lod0": [r"^grasspatch%d$" % k]} for k in (1, 2, 3)], "decimate": 0.5},
}

a = sys.argv[sys.argv.index("--") + 1:]
src, key, out = a[0], a[1], a[2]
P = PACKS[key]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
meshes = [o for o in bpy.context.scene.objects if o.type == "MESH" and len(o.data.vertices)]


def chain(o):
    c = [o.name]; p = o.parent
    while p: c.append(p.name); p = p.parent
    return c


def pick(pats):   # mesh objects with the object itself or an ancestor matching one of the patterns
    return [o for o in meshes if any(re.search(p, n) for p in pats for n in chain(o))]


def world_copy(objs, name):
    """objects -> one new mesh object in world space (unit scale applied later), dropped materials removed"""
    bm = bmesh.new(); mats = []
    for o in objs:
        me = o.data.copy(); me.transform(o.matrix_world)
        b2 = bmesh.new(); b2.from_mesh(me)
        drop = [i for i, s in enumerate(o.material_slots) if s.material and s.material.name in P.get("dropMats", [])]
        if drop: bmesh.ops.delete(b2, geom=[f for f in b2.faces if f.material_index in drop], context="FACES")
        remap = {}
        for i, s in enumerate(o.material_slots):
            m = s.material
            if m is None: continue
            if m not in mats: mats.append(m)
            remap[i] = mats.index(m)
        for f in b2.faces: f.material_index = remap.get(f.material_index, 0)
        tmp = bpy.data.meshes.new("tmp"); b2.to_mesh(tmp); b2.free()
        bm.from_mesh(tmp); bpy.data.meshes.remove(tmp); bpy.data.meshes.remove(me)
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    for m in mats: me.materials.append(m)
    ob = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(ob)
    return ob


def tris(ob): return sum(len(f.vertices) - 2 for f in ob.data.polygons)


def decimate(ob, ratio):
    md = ob.modifiers.new("dec", "DECIMATE"); md.ratio = max(0.02, min(1.0, ratio))
    bpy.context.view_layer.objects.active = ob
    with bpy.context.temp_override(object=ob, active_object=ob): bpy.ops.object.modifier_apply(modifier=md.name)


def thin_cards(ob, keep, seed):
    """the distance LOD: keep this share of the leaf cards (whole cards: linked islands of the alpha-tested material, never
    decimated, so no card is ever stretched); the bark stays"""
    import random
    rnd = random.Random(seed)
    leaf = {i for i, m in enumerate(ob.data.materials) if m and m.blend_method != "OPAQUE" and "bark" not in m.name.lower()}
    bm = bmesh.new(); bm.from_mesh(ob.data); bm.faces.ensure_lookup_table()
    seen, drop = set(), []
    for f in bm.faces:
        if f.index in seen or f.material_index not in leaf: continue
        isl, stack = [], [f]; seen.add(f.index)
        while stack:
            g = stack.pop(); isl.append(g)
            for e in g.edges:
                for h in e.link_faces:
                    if h.index not in seen and h.material_index in leaf: seen.add(h.index); stack.append(h)
        if rnd.random() > keep: drop += isl
    bmesh.ops.delete(bm, geom=drop, context="FACES"); bm.to_mesh(ob.data); bm.free()


# ---- the plants: (id, [LOD0 objects], [LOD1 objects] or None)
plants = []
if "plants" in P:
    for d in P["plants"]:
        l0 = pick(d["lod0"]); l1 = pick(d["lod1"]) if "lod1" in d else None
        if not l0: print("EXPORT missing", d["id"]); continue
        plants.append((d["id"], l0, l1))
elif "pairs" in P:   # one plant per tree node (its trunk and the branch cards under it); stray cards go to the nearest trunk
    def centre(o): W = np.array([o.matrix_world @ v.co for v in o.data.vertices]); return 0.5 * (W.min(0) + W.max(0))
    groups = {}
    for o in meshes:
        t = [n for n in chain(o) if re.search(P["pairs"][0], n)]
        if t: groups.setdefault(t[-1], []).append(o)
    stray = [o for o in meshes if not any(o in g for g in groups.values()) and any(re.search(P["pairs"][1], n) for n in chain(o))]
    for c in stray:
        cc = centre(c); best = min(groups, key=lambda k: np.linalg.norm((centre(groups[k][0]) - cc)[:2]))
        groups[best].append(c)
    for k, name in enumerate(sorted(groups), 1): plants.append(("tree_%d" % k, groups[name], None))
    print("EXPORT groups", {n: [o.name for o in g] for n, g in groups.items()})
elif "cluster" in P:   # DF1: loose pieces of every mesh whose ground footprints overlap are one plant, named by its main material
    whole = world_copy(meshes, "split"); bpy.context.view_layer.objects.active = whole
    for o in bpy.context.scene.objects: o.select_set(o == whole)
    bpy.ops.object.mode_set(mode="EDIT"); bpy.ops.mesh.separate(type="LOOSE"); bpy.ops.object.mode_set(mode="OBJECT")
    bits = [o for o in bpy.context.scene.objects if o.name.startswith("split")]
    box = [(lambda W: (W.min(0), W.max(0)))(np.array([v.co for v in o.data.vertices])) for o in bits]
    par = list(range(len(bits)))
    def root(i):
        while par[i] != i: par[i] = par[par[i]]; i = par[i]
        return i
    for i in range(len(bits)):
        for j in range(i):
            if all(box[i][0][k] <= box[j][1][k] and box[j][0][k] <= box[i][1][k] for k in (0, 1)): par[root(i)] = root(j)
    groups = {}
    for i, o in enumerate(bits): groups.setdefault(root(i), []).append(o)
    named = []
    for g in groups.values():
        area = {}
        for o in g:
            for p in o.data.polygons:
                m = o.data.materials[p.material_index].name.lower() if o.data.materials else ""
                kind = next((v for k, v in P["cluster"].items() if k in m), None)
                if kind: area[kind] = area.get(kind, 0) + p.area
        lo = np.min([box[bits.index(o)][0] for o in g], 0); hi = np.max([box[bits.index(o)][1] for o in g], 0)
        if area: named.append((max(area, key=area.get), float(hi[2] - lo[2]), g))
    for kind in sorted({n[0] for n in named}):
        for k, (_, h, g) in enumerate(sorted([n for n in named if n[0] == kind], key=lambda n: -n[1]), 1): plants.append(("%s_%d" % (kind, k), g, None))
    print("EXPORT clusters", [(p[0], len(p[1])) for p in plants])
    meshes = []
elif "loose" in P:   # one mesh of several stones: each loose part over the size limit is a stone; the whole group too
    whole = world_copy(meshes, "whole")
    plants.append(("group", [whole], None))
    part = world_copy(meshes, "split"); bpy.context.view_layer.objects.active = part
    for o in bpy.context.scene.objects: o.select_set(o == part)
    bpy.ops.object.mode_set(mode="EDIT"); bpy.ops.mesh.separate(type="LOOSE"); bpy.ops.object.mode_set(mode="OBJECT")
    parts = [o for o in bpy.context.scene.objects if o.name.startswith("split")]
    big = []
    for o in parts:
        W = np.array([v.co for v in o.data.vertices]); s = W.max(0) - W.min(0)
        if max(s) * P["unit"] >= P["loose"]: big.append((max(s), o))
    for k, (_, o) in enumerate(sorted(big, key=lambda x: -x[0]), 1): plants.append(("stone_%d" % k, [o], None))
    meshes = []

U = P["unit"]
stats = {}; lod0_objs = []; lod1_objs = []
for pid, l0, l1 in plants:
    for lod, objs in ((0, l0), (1, l1)):
        if lod == 1 and objs is None:
            ob = world_copy(l0, "%s__%d" % (pid, lod))
            if P.get("decimate"): decimate(ob, P["decimate"])
        else:
            if not objs: continue
            ob = world_copy(objs, "%s__%d" % (pid, lod))
        if lod == 1 and P.get("lod1Keep"): thin_cards(ob, P["lod1Keep"], hash(pid) & 0xffff)
        if lod == 0 and P.get("maxTris") and tris(ob) > P["maxTris"]: decimate(ob, P["maxTris"] / tris(ob))
        if lod == 1 and "maxTris" in P and tris(ob) > P["maxTris"] * 0.35: decimate(ob, P["maxTris"] * 0.35 / tris(ob))
        W = np.array([v.co for v in ob.data.vertices]) * U
        if lod == 0:   # anchor: the lowest 15 % of the plant (the trunk foot) centred in x / y, its lowest point on the ground
            z0 = W[:, 2].min(); foot = W[W[:, 2] < z0 + 0.15 * (W[:, 2].max() - z0)]
            anc = np.array([0.5 * (foot[:, 0].min() + foot[:, 0].max()), 0.5 * (foot[:, 1].min() + foot[:, 1].max()), z0])
            stats[pid] = {"anchor": anc.tolist()}
        anc = np.array(stats[pid]["anchor"])
        W = W - anc
        ob.data.vertices.foreach_set("co", W.reshape(-1)); ob.data.update()
        lo, hi = W.min(0), W.max(0)
        stats[pid]["lod%d" % lod] = {"tris": tris(ob), "size": [round(float(x), 2) for x in (hi - lo)], "lo": lo.round(2).tolist(), "hi": hi.round(2).tolist()}
        (lod0_objs if lod == 0 else lod1_objs).append(ob)

allW = np.vstack([np.array([v.co for v in o.data.vertices]) for o in lod0_objs])
BLO, BHI = allW.min(0) - 0.01, allW.max(0) + 0.01


def anchor_obj():
    me = bpy.data.meshes.new("anchor"); e = 0.001
    vs = [tuple(BLO), (BLO[0] + e, BLO[1], BLO[2]), (BLO[0], BLO[1] + e, BLO[2]), tuple(BHI), (BHI[0] - e, BHI[1], BHI[2]), (BHI[0], BHI[1] - e, BHI[2])]
    me.from_pydata([tuple(map(float, v)) for v in vs], [], [(0, 1, 2), (3, 4, 5)]); me.update()
    o = bpy.data.objects.new("anchor", me); bpy.context.scene.collection.objects.link(o); return o


for objs, fn in ((lod0_objs, key + ".glb"), (lod1_objs, key + "_lod1.glb")):
    if not objs: continue
    bpy.ops.object.select_all(action="DESELECT")
    keep = objs + [anchor_obj()]
    for o in keep: o.select_set(True)
    bpy.context.view_layer.objects.active = keep[0]
    bpy.ops.export_scene.gltf(filepath=os.path.join(out, fn), export_format="GLB", use_selection=True, export_yup=True, export_image_format="AUTO")
    for o in keep[-1:]: bpy.data.objects.remove(o, do_unlink=True)

offset = [float(0.5 * (BLO[0] + BHI[0])), float(BLO[2]), float(-0.5 * (BLO[1] + BHI[1]))]   # glTF: (x, z, -y)
json.dump({"offset": [round(v, 5) for v in offset], "plants": stats}, open(os.path.join(out, key + "_bake.json"), "w"), indent=1)
print("EXPORT", key, len(plants), "plants offset", offset)
for k, v in stats.items(): print("EXPORT", k, v.get("lod0", {}).get("tris"), v.get("lod1", {}).get("tris"), v.get("lod0", {}).get("size"))
