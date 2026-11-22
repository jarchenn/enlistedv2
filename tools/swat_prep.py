# Blender background (5.x): "SWAT with M4" (static, no skeleton, the FSB Operator base re-textured, frozen in the FSB's frame-1
# rifle hold, centimetres) -> a skinned GLB on the FSB rig, bind pose, upright, for tools/bake_gunbody.py --skin.
#   blender -b --factory-startup --disable-autoexec --python swat_prep.py -- <fsb_operator.glb> <swat.glb> <out.glb>
# 1. The FSB at its animation's frame 1: skinned meshes posed, loose pieces (pouches, magazines, patches) where their empties are.
# 2. SWAT: the rifle (material '*rifle*') dropped; fitted onto the FSB by a similarity transform of the face mesh (same topology).
# 3. Weights per connected island, decided for the island as a whole: on an FSB loose piece (median distance < 8 mm) -> that
#    piece's bone, weight 1 (pouches / magazines Spine, back patch Spine2, shoulder patch RightArm); nearer the FSB gloves than
#    the rest of the body -> the gloves' weights; else the nearest FSB body vertex's weights (gloves left out of that source).
# 4. Each vertex un-posed by the inverse of its blended skin matrix (frame-1 pose -> bind), bound to the FSB armature at rest;
#    the armature stood up as tools/fsb_prep.py does; maps <= 2048 px; no decimation.
import bpy, bmesh, sys
import numpy as np
from mathutils import Vector, Matrix, kdtree
from mathutils.bvhtree import BVHTree
argv = sys.argv[sys.argv.index('--') + 1:]
FSB, SWAT, OUT = argv[0], argv[1], argv[2]
TEX_MAX = 2048
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=FSB, import_shading='NORMALS', guess_original_bind_pose=True)
fsb_objs = set(bpy.context.scene.objects)
arm = next(o for o in fsb_objs if o.type == 'ARMATURE')
bn = {b.name.split(':')[-1].rsplit('_', 1)[0]: b.name for b in arm.data.bones}
def chain_has(o, words):
    p = o
    while p is not None:
        if any(w in p.name for w in words): return True
        p = p.parent
    return False
fskin = [o for o in fsb_objs if o.type == 'MESH' and any(m.type == 'ARMATURE' for m in o.modifiers)]
frigid = [o for o in fsb_objs if o.type == 'MESH' and o not in fskin and chain_has(o, ('pouch', 'Plane', 'Magazine.'))]
bpy.context.scene.frame_set(1); bpy.context.view_layer.update()
A = arm.matrix_world.copy(); Ai = A.inverted()
S = {pb.name: pb.matrix @ arm.data.bones[pb.name].matrix_local.inverted() for pb in arm.pose.bones}   # armature-space skin matrices at frame 1
dg = bpy.context.evaluated_depsgraph_get()
def posed_verts(o):
    e = o.evaluated_get(dg); me = e.to_mesh(); M = o.matrix_world
    P = [M @ v.co for v in me.vertices]; F = [tuple(p.vertices) for p in me.polygons]; e.to_mesh_clear(); return P, F
# weight sources: posed FSB vertices with their weights (gloves separate)
def weights_of(o):
    gi = {g.index: g.name for g in o.vertex_groups}
    return [[(gi[g.group], g.weight) for g in v.groups if g.weight > 1e-4 and gi.get(g.group) in S] for v in o.data.vertices]
body_P, body_W, glove_P, glove_W = [], [], [], []
for o in fskin:
    P, _ = posed_verts(o); W = weights_of(o)
    is_glove = o.active_material and 'glove' in o.active_material.name.lower()
    (glove_P if is_glove else body_P).extend(P); (glove_W if is_glove else body_W).extend(W)
def kd(P):
    t = kdtree.KDTree(len(P))
    for i, p in enumerate(P): t.insert(p, i)
    t.balance(); return t
kb, kg = kd(body_P), kd(glove_P)
armish = lambda b: any(k in b for k in ('Arm', 'ForeArm', 'Hand')) and 'Shoulder' not in b
body_arm = [bool(ws) and armish(max(ws, key=lambda q: q[1])[0]) for ws in body_W]   # FSB vertices dominated by an arm / hand bone
body_P_t = [p for p, a in zip(body_P, body_arm) if not a]; body_W_t = [w for w, a in zip(body_W, body_arm) if not a]
kbt = kd(body_P_t)
body_P_a = [p for p, a in zip(body_P, body_arm) if a]; body_W_a = [w for w, a in zip(body_W, body_arm) if a]
kba = kd(body_P_a)
pieces = []   # (bvh, bone) of each loose piece at frame 1
for o in frigid:
    P, F = posed_verts(o)
    bone = bn['RightArm'] if chain_has(o, ('Plane.001',)) else bn['Spine2'] if chain_has(o, ('Plane_',)) else bn['Spine']
    pieces.append((BVHTree.FromPolygons(P, F), bone, o.parent.name if o.parent else o.name))
face_fsb = next(o for o in fskin if o.active_material and o.active_material.name == 'face')
FF, _ = posed_verts(face_fsb)
# 2. SWAT, the rifle out, fitted by the face
before = set(bpy.context.scene.objects)
bpy.ops.import_scene.gltf(filepath=SWAT, import_shading='NORMALS')
sw = [o for o in set(bpy.context.scene.objects) - before if o.type == 'MESH']
rifle = [o for o in sw if any('rifle' in (s.material.name.lower() if s.material else '') for s in o.material_slots)]
for o in rifle: bpy.data.objects.remove(o, do_unlink=True)
sw = [o for o in sw if o not in rifle]
face_sw = next(o for o in sw if len(o.data.vertices) == len(FF) and len(o.data.polygons) == len(face_fsb.data.polygons))
X = np.array([tuple(face_sw.matrix_world @ v.co) for v in face_sw.data.vertices]); Y = np.array([tuple(p) for p in FF])
mx, my = X.mean(0), Y.mean(0); Xc, Yc = X - mx, Y - my
U, D, Vt = np.linalg.svd(Yc.T @ Xc); dd = np.sign(np.linalg.det(U @ Vt)); E = np.diag([1, 1, dd]); R = U @ E @ Vt
s = (D * np.diag(E)).sum() / (Xc ** 2).sum(); t = my - s * R @ mx
res = np.sqrt((((s * (R @ X.T)).T + t - Y) ** 2).sum(1)).mean()
print('face fit: scale %.5f, mean residual %.2f mm' % (s, res * 1000))
Mfit = Matrix(((*(s * R[0]), t[0]), (*(s * R[1]), t[1]), (*(s * R[2]), t[2]), (0, 0, 0, 1)))
# 3 + 4. per island weights, un-posed, bound
for o in sw:
    o.data.transform(o.matrix_world); o.matrix_world = Matrix.Identity(4); o.data.transform(Mfit); o.data.update()
stats = {'rigid': 0, 'glove': 0, 'body': 0}; rigid_names = {}
for o in sw:
    me = o.data; bm = bmesh.new(); bm.from_mesh(me); bm.verts.ensure_lookup_table()
    island = [-1] * len(bm.verts); k = 0
    for v in bm.verts:
        if island[v.index] >= 0: continue
        stack = [v]; island[v.index] = k
        while stack:
            a = stack.pop()
            for e in a.link_edges:
                b = e.other_vert(a)
                if island[b.index] < 0: island[b.index] = k; stack.append(b)
        k += 1
    bm.free()
    groups = {}; drop = []
    co = [v.co.copy() for v in me.vertices]
    newco = [None] * len(co)
    for isl in range(k):
        idx = [i for i in range(len(co)) if island[i] == isl]
        sample = idx[:: max(1, len(idx) // 200)]
        best = None
        for bvh, bone, nm in pieces:
            ds = sorted((bvh.find_nearest(co[i])[3] or 9.0) for i in sample)
            med = ds[len(ds) // 2]
            if med < 0.008 and (best is None or med < best[0]): best = (med, bone, nm)
        if best:
            stats['rigid'] += 1; rigid_names[best[2]] = rigid_names.get(best[2], 0) + 1
            Wv = {i: [(best[1], 1.0)] for i in idx}
        else:
            dg_ = sorted(kg.find(co[i])[2] for i in sample); db = sorted(kb.find(co[i])[2] for i in sample)
            glove = dg_[len(dg_) // 2] < db[len(db) // 2]
            stats['glove' if glove else 'body'] += 1
            if glove: src, W = kg, glove_W
            else:   # the island's region by a vote of its vertices' nearest FSB vertex: a torso piece (vest webbing against the forearm in
                    # the rifle hold) takes no arm weights at all; an arm piece (a sleeve) takes the whole body's
                votes = sum(1 for i in sample if body_arm[kb.find(co[i])[1]]); torso = votes * 2 < len(sample)
                src, W = (kbt, body_W_t) if torso else (kba, body_W_a)   # an arm piece takes arm weights only: a strap lying on the forearm moves with it
                stats['torso' if torso else 'arm'] = stats.get('torso' if torso else 'arm', 0) + 1
                if not torso:
                    ext = max((co[i] - co[j]).length for i in sample for j in sample) if len(sample) > 1 else 0
                    print('  arm island %s/%d: %d vertices, %.2f m, torso vote %.0f%%' % (o.name, isl, len(idx), ext, 100 - votes * 100 / len(sample)))
            Wv = {i: W[src.find(co[i])[1]] for i in idx}
        for i, ws in Wv.items():
            tot = sum(w for _, w in ws) or 1.0
            M = sum(((S[b]) * (w / tot) for b, w in ws), Matrix(((0,) * 4,) * 4))
            p = Ai @ co[i]; q = M.inverted_safe() @ p; newco[i] = A @ q
            for b, w in ws: groups.setdefault(b, []).append((i, w / tot))
    for i, c in enumerate(newco):
        if c is not None: me.vertices[i].co = c
    me.update()
    for b, lst in groups.items():
        vg = o.vertex_groups.get(b) or o.vertex_groups.new(name=b)
        for i, w in lst: vg.add([i], w, 'REPLACE')
    md = o.modifiers.new('Armature', 'ARMATURE'); md.object = arm
    if drop:
        bm = bmesh.new(); bm.from_mesh(me); bm.verts.ensure_lookup_table(); ds = set(drop)
        bmesh.ops.delete(bm, geom=[v for v in bm.verts if v.index in ds], context='VERTS'); bm.to_mesh(me); bm.free(); me.update()
print('islands:', stats, 'rigid on', rigid_names)
# the FSB meshes out; the armature at rest, stood up (as tools/fsb_prep.py)
posed = {pb.name: arm.matrix_world @ pb.matrix for pb in arm.pose.bones}; restW = {b.name: arm.matrix_world @ b.matrix_local for b in arm.data.bones}
hips = bn['Hips']; upright = posed[hips] @ restW[hips].inverted()
mw = arm.matrix_world.copy(); arm.parent = None; arm.matrix_world = mw   # keep the root's scale / turn (its parents are deleted next)
for o in list(fsb_objs):
    if o.type == 'MESH' or (o.type == 'EMPTY' and o != arm): bpy.data.objects.remove(o, do_unlink=True)
if arm.animation_data: arm.animation_data.action = None
for pb in arm.pose.bones: pb.matrix_basis.identity()
bpy.context.view_layer.update()
Aw_inv = arm.matrix_world.inverted()
for o in sw:   # the vertex data into armature space, the object at the armature's frame (the exporter writes skinned data so)
    o.data.transform(Aw_inv); o.data.update(); o.parent = arm; o.matrix_parent_inverse = Matrix.Identity(4); o.matrix_basis = Matrix.Identity(4)
arm.matrix_world = upright @ arm.matrix_world
bpy.context.view_layer.update()
for im in bpy.data.images:
    if im.size[0] > TEX_MAX or im.size[1] > TEX_MAX:
        kk = TEX_MAX / max(im.size); im.scale(max(1, int(im.size[0] * kk)), max(1, int(im.size[1] * kk)))
print('triangles', sum(sum(len(p.vertices) - 2 for p in o.data.polygons) for o in sw))
bpy.ops.object.select_all(action='DESELECT'); arm.select_set(True)
for o in sw: o.select_set(True)
bpy.context.view_layer.objects.active = arm
bpy.ops.export_scene.gltf(filepath=OUT, export_format='GLB', use_selection=True, export_animations=False, export_skins=True, export_yup=True, export_image_format='JPEG', export_jpeg_quality=92)
print('wrote', OUT)
