# CAN1 (Blender background): a supplied cannon GLB -> the game's station model: parts "tube", "carriage", "wheel_<side><n>" and
# "fuse", in metres, the muzzle along glTF +z (Blender -y), centred on its bounds in x / z with its lowest point on the ground
# (what the bake's pivot "ground" would do, so the station points below hold in the baked asset), the tube turned level about
# its trunnions (elevation 0 = level). A slow-match fuse cord is added at the vent. Writes <out>.glb and <out>.json (the station
# points in asset space: x right, y up, z forward).
#   blender -b --factory-startup --disable-autoexec --python prep_cannon.py -- <c18|mortar> <in.glb> <out stem>
import bpy, bmesh, sys, json, math
import numpy as np
from mathutils import Vector, Matrix
A = sys.argv[sys.argv.index('--') + 1:]; KIND, IN, OUT = A[0], A[1], A[2]
bpy.ops.wm.read_factory_settings(use_empty=True); bpy.ops.import_scene.gltf(filepath=IN)
sc = bpy.context.scene; vl = bpy.context.view_layer
for o in list(bpy.data.objects):   # apply every transform into the meshes (world space), drop empties
    if o.type == 'MESH':
        o.data = o.data.copy(); o.data.transform(o.matrix_world); o.parent = None; o.matrix_world = Matrix()
for o in [o for o in bpy.data.objects if o.type != 'MESH']: bpy.data.objects.remove(o, do_unlink=True)

def islands(o):
    bm = bmesh.new(); bm.from_mesh(o.data); bm.verts.ensure_lookup_table()
    V = np.array([v.co[:] for v in bm.verts]); tol = max(1e-6, float((V.max(0) - V.min(0)).max()) * 2e-5)
    _, w = np.unique(np.round(V / tol).astype(np.int64), axis=0, return_inverse=True); w = w.ravel()
    par = list(range(w.max() + 1))
    def f(x):
        while par[x] != x: par[x] = par[par[x]]; x = par[x]
        return x
    for fc in bm.faces:
        r = [f(w[v.index]) for v in fc.verts]
        for x in r[1:]: par[f(x)] = f(r[0])
    lab = np.array([f(w[i]) for i in range(len(V))]); bm.free(); return V, lab
def split(o, groups, names):   # groups: per-vertex group index -> new objects (faces by their first vertex)
    out = []
    for gi, nm in enumerate(names):
        bm = bmesh.new(); bm.from_mesh(o.data); bm.verts.ensure_lookup_table()
        kill = [f for f in bm.faces if groups[f.verts[0].index] != gi]; bmesh.ops.delete(bm, geom=kill, context='FACES')
        bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context='VERTS')
        me = bpy.data.meshes.new(nm); bm.to_mesh(me); bm.free()
        for m in o.data.materials: me.materials.append(m)
        n = bpy.data.objects.new(nm, me); sc.collection.objects.link(n); out.append(n)
    return out
def join(objs, name):
    vl.update()
    for x in bpy.data.objects: x.select_set(False)
    objs = [x for x in objs if x and len(x.data.polygons)]
    for x in objs: x.select_set(True)
    vl.objects.active = objs[0]; bpy.ops.object.join(); j = vl.objects.active; j.name = name; j.data.name = name; return j
ob = bpy.data.objects
if KIND == 'c18':   # LACooper's 18th Century Cannon (cm): Object_2 the tube, 3 / 4 / 5 the carriage (3 holds the four wheels), 6 / 7 the crate and balls (dropped)
    for n in ('Object_6', 'Object_7'): bpy.data.objects.remove(ob[n], do_unlink=True)
    for o in list(ob): o.data.transform(Matrix.Scale(0.01, 4))
    tube = ob['Object_2']; tube.name = 'tube'
    V, lab = islands(ob['Object_3']); g = np.zeros(len(V), int)
    for l in np.unique(lab):
        q = V[lab == l]
        if np.abs(q[:, 0]).min() >= 0.479 and q[:, 2].max() <= 0.475:   # a wheel (rim, spokes, hub): outboard of the cheeks, under 47.5 cm
            g[lab == l] = 1 + (2 if q[:, 1].mean() > 0.4 else 0) + (1 if q[:, 0].mean() > 0 else 0)
    parts = split(ob['Object_3'], g, ['carr3', 'wheel_L0', 'wheel_R0', 'wheel_L1', 'wheel_R1'])
    bpy.data.objects.remove(ob['Object_3'], do_unlink=True)
    carriage = join([ob['carr3'], ob['Object_4'], ob['Object_5']], 'carriage')
    tube_islands_obj = tube
else:   # Delfrador's Cannon (a mortar on a truck carriage, m, one mesh, the muzzle toward +x): turned so the muzzle is -y
    o = ob['Test_T_Test_0']; o.data.transform(Matrix.Rotation(math.radians(-90), 4, 'Z'))
    V, lab = islands(o); g = np.zeros(len(V), int)
    ids, cnt = np.unique(lab, return_counts=True); main = ids[np.argmax(cnt)]   # the tube's body: the biggest island
    T0 = V[lab == main]; c0 = T0.mean(0); _, _, vt = np.linalg.svd(T0 - c0, full_matrices=False); ax0 = vt[0]
    for l in ids:
        q = V[lab == l]
        if np.abs(q[:, 0]).min() >= 0.40 and q[:, 2].max() <= 0.36 and np.ptp(q[:, 0]) < 0.3: g[lab == l] = 1 + (2 if q[:, 1].mean() > 0 else 0) + (1 if q[:, 0].mean() > 0 else 0); continue
        t = (q - c0) @ ax0; r = np.linalg.norm((q - c0) - np.outer(t, ax0), axis=1)
        if l == main or (r.max() < 0.33 and t.min() > -0.40 and t.max() < 0.70 and q[:, 2].min() > 0.30): g[lab == l] = 5   # on the tube
    parts = split(o, g, ['carriage', 'wheel_L0', 'wheel_R0', 'wheel_L1', 'wheel_R1', 'tube'])
    bpy.data.objects.remove(o, do_unlink=True); tube = ob['tube']; carriage = ob['carriage']
# ---- the tube level about its trunnions (the ends of the tube at its widest x): the angle that gives the tube its least height
# (a fit of the axis is skewed by the mortar's rings, handles and lugs); the muzzle is the -y end in both models
TV = np.array([v.co[:] for v in tube.data.vertices])
ys = TV[np.abs(TV[:, 0]) > 0.9 * np.abs(TV[:, 0]).max()]; piv = np.array([0.0, ys[:, 1].mean(), ys[:, 2].mean()])
def hgt(a):
    R = np.array(Matrix.Rotation(a, 3, 'X')); q = (TV - piv) @ R.T; return q[:, 2].max() - q[:, 2].min()
ang = min((math.radians(d / 4) for d in range(-360, 361)), key=hgt)
tube.data.transform(Matrix.Translation(Vector(piv)) @ Matrix.Rotation(ang, 4, 'X') @ Matrix.Translation(-Vector(piv)))
TV = np.array([v.co[:] for v in tube.data.vertices]); ang = -ang   # the modelled elevation (up toward the muzzle, -y)
ymin, ymax = TV[:, 1].min(), TV[:, 1].max(); L = ymax - ymin
mouth = TV[TV[:, 1] < ymin + 0.03 * L]; bore_z = float((mouth[:, 2].min() + mouth[:, 2].max()) / 2)
print('CAN tube modelled elevation %.1f deg, trunnion pivot %s, bore z %.3f, length %.3f' % (math.degrees(ang), [round(float(x), 3) for x in piv], bore_z, L))
muzzle = np.array([0.0, ymin, bore_z])
yv = ymax - (0.42 if KIND == 'c18' else 0.12 * L)   # the vent: just ahead of the base ring / over the chamber (the breech end is +y)
top = TV[(np.abs(TV[:, 1] - yv) < (0.06 if KIND == "c18" else 0.08 * L)) & (np.abs(TV[:, 0]) < 0.12) & (TV[:, 2] > bore_z)]; vent = np.array([0.0, yv, float(np.percentile(top[:, 2], 50 if KIND == "c18" else 10))])   # the median: the surface, not a lug or ring on it
# ---- the fuse: a slow-match cord out of the vent, up and back over the breech, drooping
pts = [vent + np.array(d) for d in ([0, 0, -0.008], [0, 0, 0.03], [0, 0.03, 0.045], [0, 0.07, 0.04], [0, 0.10, 0.025])]
rad = 0.0045; bm = bmesh.new(); rings = []
for k, p in enumerate(pts):
    d = (pts[min(k + 1, len(pts) - 1)] - pts[max(k - 1, 0)]); d = d / np.linalg.norm(d); a_ = np.cross(d, [1, 0, 0]); a_ /= np.linalg.norm(a_); b_ = np.cross(d, a_)
    rings.append([bm.verts.new(Vector((p + rad * (math.cos(2 * math.pi * i / 8) * a_ + math.sin(2 * math.pi * i / 8) * b_)).tolist())) for i in range(8)])
for k in range(len(rings) - 1):
    for i in range(8): bm.faces.new((rings[k][i], rings[k][(i + 1) % 8], rings[k + 1][(i + 1) % 8], rings[k + 1][i]))
bm.faces.new(rings[-1][::-1])
fme = bpy.data.meshes.new('fuse'); bm.to_mesh(fme); bm.free()
fm = bpy.data.materials.new('fuse_match'); fm.use_nodes = True; bs = fm.node_tree.nodes['Principled BSDF']; bs.inputs['Base Color'].default_value = (0.20, 0.15, 0.09, 1); bs.inputs['Roughness'].default_value = 0.95
fme.materials.append(fm); fuse = bpy.data.objects.new('fuse', fme); sc.collection.objects.link(fuse)
# ---- centre on the bounds (x / Blender-y), lowest point at 0: the bake's pivot "ground" becomes the identity
allv = np.concatenate([np.array([o_.matrix_world @ v.co for v in o_.data.vertices]) for o_ in bpy.data.objects if o_.type == 'MESH'])
off = np.array([-(allv[:, 0].min() + allv[:, 0].max()) / 2, -(allv[:, 1].min() + allv[:, 1].max()) / 2, -allv[:, 2].min()])
for o_ in [o_ for o_ in bpy.data.objects if o_.type == 'MESH']: o_.data.transform(Matrix.Translation(Vector(off.tolist())))
def A_(p): p = np.array(p) + off; return [round(float(p[0]), 4), round(float(p[2]), 4), round(float(-p[1]), 4)]   # Blender -> asset (x, y up, z forward)
allv = allv + off
wheels = {}
for o_ in bpy.data.objects:
    if o_.name.startswith('wheel_'):
        q = np.array([v.co[:] for v in o_.data.vertices]); wc = (q.min(0) + q.max(0)) / 2
        print('CAN wheel', o_.name, len(q), q.min(0).round(3).tolist(), q.max(0).round(3).tolist())
        wheels[o_.name] = {'centre': A_(wc - off), 'radius': round(float((q[:, 2].max() - q[:, 2].min()) / 2), 4)}
breech = np.array([0.0, ymax, bore_z]); eye = breech + np.array([0.0, 0.55, 0.22])
S = {'elevPivot': A_(piv), 'muzzle': A_(muzzle), 'vent': A_(vent), 'fuseTip': A_(pts[-1]), 'breech': A_(breech), 'optic': A_(eye),
     'gunner': [0.55, 0.0, round(A_(breech)[2] - 0.75, 3)], 'handler': [0.0, 0.0, round(float(-(allv[:, 1].max())) - 0.45, 3)],
     'moveHalf': [round(float((allv[:, 0].max() - allv[:, 0].min()) / 2), 3), round(float(allv[:, 2].max() / 2), 3), round(float((allv[:, 1].max() - allv[:, 1].min()) / 2), 3)],
     'wheels': wheels, 'tubeLength': round(float(L), 3), 'modelledElevationDeg': round(math.degrees(ang), 1), 'boreAbovePivot': round(bore_z - float(piv[2]), 4),
     'bounds': [[round(float(x), 3) for x in A_(allv.min(0) - off)], [round(float(x), 3) for x in A_(allv.max(0) - off)]]}
json.dump(S, open(OUT + '.json', 'w'), indent=1); print('CAN points', json.dumps(S))
vl.update()
for x in bpy.data.objects: x.select_set(x.type == 'MESH')
bpy.ops.export_scene.gltf(filepath=OUT + '.glb', export_format='GLB', use_selection=True, export_apply=True, export_yup=True)
print('CAN parts', sorted(o_.name for o_ in bpy.data.objects if o_.type == 'MESH'))
