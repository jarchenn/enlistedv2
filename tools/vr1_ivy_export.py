# VR1, Blender background (blender -b --factory-startup --disable-autoexec --python tools/vr1_ivy_export.py -- <ivy_pack.glb> <out dir>),
# then NormandyMapmaker --import-weapon-models <out dir> content (content/assets/ivy_pack.json; its "offset" is
# ivy_pack_bake.json's). The ivy pack (LOLIPOP, CC-BY-4.0) -> two GLBs for the game's importer, every piece
# anchored at its own origin and named <piece>_leaf / <piece>_stem:
#   <out>/ivy_pack.glb       LOD0 of the 12 pieces
#   <out>/ivy_pack_lod1.glb  LOD2 (same leaves as LOD1, a quarter of LOD0's stems)
#   <out>/ivy_pack_bake.json the importer offset (so its "ground" pivot moves nothing) and per-piece stats
# Anchors (Blender, z up; glTF swaps to y up, -y -> +z):
#   wall_k:    x centre, the wall plane y = 0 (the pack's demo wall), z 0      -> model: on a wall facing +z
#   corner_k:  the inside corner of the demo wall (fin face x, wall face y 0.23) -> model: walls facing +z and -x
#   ground_k / gdirect_k: the patch's centre on the ground
# The demo walls, the 19 m "Ivy_ground_3_LOD0.003" scene and the texture-holder quads are left out.
import bpy, sys, json, os
import numpy as np
a = sys.argv[sys.argv.index("--") + 1:]
src, out = a[0], a[1]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
objs = {o.name: o for o in bpy.context.scene.objects}

PIECES = {}
for k in (1, 2, 3):
    PIECES["wall_%d" % k] = ("Ivy_wall_%d" % k, "wall")
    PIECES["corner_%d" % k] = ("Ivy_corner_%d" % k, "corner")
    PIECES["ground_%d" % k] = ("Ivy_ground_%d" % k, "ground")
    PIECES["gdirect_%d" % k] = ("Ivy_ground_direct_%d" % k, "ground")
FIN_X = {"corner_1": 54.21, "corner_2": 60.21, "corner_3": 66.21}   # the demo wall's fin faces (analyse.py)


def members(parent):
    return [o for o in bpy.context.scene.objects if o.type == "MESH" and o.parent and o.parent.name == parent]


def world_verts(o):
    M = np.array(o.matrix_world); n = len(o.data.vertices); V = np.zeros(n * 3); o.data.vertices.foreach_get("co", V)
    return V.reshape(n, 3) @ M[:3, :3].T + M[:3, 3]


anchors, stats = {}, {}
for pid, (pre, kind) in PIECES.items():
    ms = members(pre + "_LOD0")
    assert len(ms) == 2, (pid, [o.name for o in ms])
    V = np.vstack([world_verts(o) for o in ms]); lo, hi = V.min(0), V.max(0)
    if kind == "wall": anchors[pid] = (0.5 * (lo[0] + hi[0]), 0.0, 0.0)
    elif kind == "corner": anchors[pid] = (FIN_X[pid], 0.23, 0.0)
    else: anchors[pid] = (0.5 * (lo[0] + hi[0]), 0.5 * (lo[1] + hi[1]), 0.0)

allV = []
for pid, (pre, kind) in PIECES.items():   # LOD0 bounds first: both GLBs carry the same two 1 mm anchor triangles at its
    for o in members(pre + "_LOD0"): allV.append(world_verts(o) - np.array(anchors[pid]))   # corners, so the importer
U = np.vstack(allV); BLO, BHI = U.min(0) - 0.01, U.max(0) + 0.01                                 # shifts both alike
def anchor_obj():
    me = bpy.data.meshes.new("anchor"); e = 0.001
    vs = [tuple(BLO), (BLO[0] + e, BLO[1], BLO[2]), (BLO[0], BLO[1] + e, BLO[2]), tuple(BHI), (BHI[0] - e, BHI[1], BHI[2]), (BHI[0], BHI[1] - e, BHI[2])]
    me.from_pydata([tuple(map(float, v)) for v in vs], [], [(0, 1, 2), (3, 4, 5)]); me.update()
    o = bpy.data.objects.new("anchor", me); bpy.context.scene.collection.objects.link(o); return o
for lod, fn in ((0, "ivy_pack.glb"), (2, "ivy_pack_lod1.glb")):
    bpy.ops.object.select_all(action="DESELECT")
    keep = []
    for pid, (pre, kind) in PIECES.items():
        for o in members(pre + "_LOD%d" % lod):
            c = o.copy(); c.data = o.data.copy(); bpy.context.scene.collection.objects.link(c)
            c.parent = None; c.matrix_world = o.matrix_world.copy()
            M = np.array(c.matrix_world)
            ax = anchors[pid]
            me = c.data; n = len(me.vertices); V = np.zeros(n * 3); me.vertices.foreach_get("co", V); V = V.reshape(n, 3)
            W = V @ M[:3, :3].T + M[:3, 3] - np.array(ax)
            me.vertices.foreach_set("co", W.reshape(-1))
            # normals follow the rotation part (the pack's empties carry no shear)
            import mathutils
            c.matrix_world = mathutils.Matrix.Identity(4)
            mat = me.materials[0].name if me.materials else ""
            c.name = "%s_%s" % (pid, "stem" if mat.startswith("Stem") else "leaf")
            me.update()
            keep.append(c)
            tris = sum(len(p.vertices) - 2 for p in me.polygons)
            stats.setdefault(pid, {})["lod%d_%s" % (lod, "stem" if mat.startswith("Stem") else "leaf")] = tris
            if lod == 0:
                lo, hi = W.min(0), W.max(0)
                stats[pid].setdefault("bmin", [1e9] * 3); stats[pid].setdefault("bmax", [-1e9] * 3)
                # glTF / model space: (x, z, -y)
                gl_lo = [lo[0], lo[2], -hi[1]]; gl_hi = [hi[0], hi[2], -lo[1]]
                stats[pid]["bmin"] = [round(min(p, q), 3) for p, q in zip(stats[pid]["bmin"], gl_lo)]
                stats[pid]["bmax"] = [round(max(p, q), 3) for p, q in zip(stats[pid]["bmax"], gl_hi)]
    keep.append(anchor_obj())
    for o in keep: o.select_set(True)
    bpy.context.view_layer.objects.active = keep[0]
    bpy.ops.export_scene.gltf(filepath=os.path.join(out, fn), export_format="GLB", use_selection=True, export_apply=False,
                              export_yup=True, export_image_format="AUTO")
    for o in keep: bpy.data.objects.remove(o, do_unlink=True)

lo, hi = BLO, BHI   # the anchors set the model's bounds: the importer's "ground" pivot is undone by "offset" (x, z centre; y min)
offset = [float(0.5 * (lo[0] + hi[0])), float(lo[2]), float(-0.5 * (lo[1] + hi[1]))]
json.dump({"offset": [round(v, 5) for v in offset], "pieces": stats}, open(os.path.join(out, "ivy_pack_bake.json"), "w"), indent=1)
print("EXPORT offset", offset)
for k, v in stats.items(): print("EXPORT", k, v)
