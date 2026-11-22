# Knight 2 (Blender background): the new parts of the user's approved preview (knight_v6), placed on knight_ss.glb's rest pose
# and exported alone (world rest coordinates, glTF y-up), for tools/enemies/knight_v2_merge.py to hang on the bones.
#   blender -b --factory-startup --disable-autoexec --python knight_v2_parts.py -- <knight_ss.glb> <Mace.fbx> <mace tex dir/>
#       <frogmouth_helmet.glb> <knights_shield_practice.glb> <tex dir: metal_base_clean.png, tile_base_clean.png, tile_mr.png/> <out_parts.glb>
# - mace: 0.78 m long, gripped 14 % up its shaft at the point of the sword's axis nearest the RightHand joint, shaft on the
#   sword's axis, head toward the sword's tip; its own maps
# - helm: HELM_FROG + COIF_HELM only, scaled by the file's FACE HELM head (0.168 m wide x 0.92), its eye level (42 % down
#   that head) at 1.645 m over the centre of the knight's own head (the vertices over 0.5 on Head + HeadTop_End)
# - round shield: 0.62 m across, centred where the heater shield was, in its plane, the boss away from the forearm
# - helm and shield finish: the armour's clean metal tile box-projected (2.5 / 3 tiles across) x the part's own base
#   luminance (x 0.7 + 0.6), roughness / metal from the armour's metal-rough tile, their own normal maps; baked (Cycles,
#   emission) into each material's own UVs, since box projection does not export
# Node names on export: KN2_mace / KN2_helm_* / KN2_shield (the merge script hangs them by that prefix).
import bpy, sys, os, json, struct
import numpy as np
from mathutils import Vector, Matrix, Quaternion
A = sys.argv[sys.argv.index('--') + 1:]
SRC, MACE, MTEX, HELM, SHLD, TEX, OUT = A
TINT, ROUGH, FLIP = 1.0, 0.0, -1.0   # the approved preview's settings (SH_FLIP=-1 H_TINT=1 H_ROUGH=0)
BAKE_PX = 1024
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=SRC, disable_bone_shape=True)
arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
fh = open(SRC, 'rb'); fh.read(12); n_, _ = struct.unpack('<II', fh.read(8)); J = json.loads(fh.read(n_)); NN = J['nodes']
par = {c: i for i, x in enumerate(NN) for c in x.get('children', [])}
def loc(x):
    t = Matrix.Translation(x.get('translation', (0, 0, 0))); q = x.get('rotation', (0, 0, 0, 1))
    return t @ Quaternion((q[3], q[0], q[1], q[2])).to_matrix().to_4x4() @ Matrix.Diagonal((*x.get('scale', (1, 1, 1)), 1))
def world(i):
    m = loc(NN[i])
    while i in par: i = par[i]; m = loc(NN[i]) @ m
    return m
C = Matrix(((1, 0, 0, 0), (0, 0, -1, 0), (0, 1, 0, 0), (0, 0, 0, 1)))   # glTF y-up -> Blender z-up
arm.data.pose_position = 'REST'; bpy.context.view_layer.update()
for nm in ('Sword', 'Shield'):   # Blender's import misplaces these bone-parented nodes: from the file's own transforms
    i = next(k for k, x in enumerate(NN) if x.get('name') == nm); bpy.data.objects[nm].matrix_world = C @ world(i) @ C.inverted()
bpy.context.view_layer.update()
def bone_head(n): return np.array(arm.matrix_world @ arm.data.bones[n].head_local)
def axis(o):
    P = np.array([o.matrix_world @ v.co for v in o.data.vertices]); c = P.mean(0); w, v = np.linalg.eigh(np.cov((P - c).T)); a = v[:, -1]
    return P, c, a, (P - c) @ a
parts = []
# ---- mace
sw = bpy.data.objects['Sword']; P, c, a, t = axis(sw)
th = float((bone_head('mixamorig:RightHand') - c) @ a)
if abs(t.max() - th) < abs(t.min() - th): a = -a; th = -th
grip_s = c + a * th
before = set(bpy.data.objects)
bpy.ops.import_scene.fbx(filepath=MACE)
ms = [o for o in bpy.data.objects if o not in before and o.type == 'MESH']
bpy.ops.object.select_all(action='DESELECT')
for o in ms: o.select_set(True)
bpy.context.view_layer.objects.active = ms[0]
if len(ms) > 1: bpy.ops.object.join()
m = ms[0]; bpy.ops.object.parent_clear(type='CLEAR_KEEP_TRANSFORM'); bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
for o in [o for o in bpy.data.objects if o not in before and o != m]: bpy.data.objects.remove(o, do_unlink=True)
bpy.context.view_layer.update()
Pm, cm, am, tm = axis(m); L = float(tm.max() - tm.min())
perp = np.linalg.norm((Pm - cm) - np.outer(tm, am), axis=1)
hi = perp[tm > tm.max() - 0.25 * L].max(); lo = perp[tm < tm.min() + 0.25 * L].max()
if lo > hi: am = -am; tm = -tm
k = 0.78 / L
grip_m = cm + am * (tm.min() + 0.14 * L)
q = Vector(am).rotation_difference(Vector(a)).to_matrix().to_4x4()
m.matrix_world = Matrix.Translation(Vector(grip_s)) @ q @ Matrix.Diagonal((k, k, k, 1)) @ Matrix.Translation(-Vector(grip_m))
mat = bpy.data.materials.new('KN2_mace'); mat.use_nodes = True; nt = mat.node_tree; b = nt.nodes['Principled BSDF']
def tex(fn, cs):
    n = nt.nodes.new('ShaderNodeTexImage'); n.image = bpy.data.images.load(os.path.join(MTEX, fn)); n.image.colorspace_settings.name = cs; return n
nt.links.new(tex('DefaultMaterial_Base_Color.jpeg', 'sRGB').outputs[0], b.inputs['Base Color'])
nt.links.new(tex('DefaultMaterial_Metallic.jpeg', 'Non-Color').outputs[0], b.inputs['Metallic'])
nt.links.new(tex('DefaultMaterial_Roughness.jpeg', 'Non-Color').outputs[0], b.inputs['Roughness'])
nm_ = nt.nodes.new('ShaderNodeNormalMap'); nt.links.new(tex('DefaultMaterial_Normal_OpenGL.jpeg', 'Non-Color').outputs[0], nm_.inputs['Color']); nt.links.new(nm_.outputs[0], b.inputs['Normal'])
m.data.materials.clear(); m.data.materials.append(mat); m.name = 'KN2_mace'
parts.append(m)
print('MACE len %.3f m scale %.4f grip %s' % (L * k, k, np.round(grip_s, 3)))
# ---- helm
kn = bpy.data.objects['Knight_Tpose']; W = kn.matrix_world
hg = {g.index for g in kn.vertex_groups if g.name in ('mixamorig:Head', 'mixamorig:HeadTop_End')}
kill = [v.index for v in kn.data.vertices if sum(g.weight for g in v.groups if g.group in hg) > 0.5]
K = np.array([W @ kn.data.vertices[i].co for i in kill]); kmin, kmax = K.min(0), K.max(0)
print('OLDHEAD', len(kill), kmin.round(3), kmax.round(3))
before = set(bpy.data.objects)
bpy.ops.import_scene.gltf(filepath=HELM); bpy.context.view_layer.update()
new = [o for o in bpy.data.objects if o not in before]
def bb(o):
    P = np.array([o.matrix_world @ v.co for v in o.data.vertices]); return P.min(0), P.max(0)
face = next(o for o in new if o.type == 'MESH' and o.data.name.startswith('FACE HELM'))
fmin, fmax = bb(face)
keep = [o for o in new if o.type == 'MESH' and (o.data.name.startswith('HELM_FROG') or o.data.name.startswith('COIF_HELM'))]
assert len(keep) >= 2, [o.data.name for o in new if o.type == 'MESH']
k = 0.92 * 0.168 / (fmax[0] - fmin[0]); h = fmax[2] - fmin[2]
ref = Vector(((fmin[0] + fmax[0]) / 2, (fmin[1] + fmax[1]) / 2, fmax[2] - 0.42 * h)); tgt = Vector(((kmin[0] + kmax[0]) / 2, (kmin[1] + kmax[1]) / 2, 1.645))
T = Matrix.Translation(tgt) @ Matrix.Diagonal((k, k, k, 1)) @ Matrix.Translation(-ref)
mats = [(o, T @ o.matrix_world) for o in keep]
for o, Mw in mats: o.parent = None; o.matrix_world = Mw
for o in new:
    if o not in keep: bpy.data.objects.remove(o, do_unlink=True)
for j, (o, _) in enumerate(mats):
    o.name = 'KN2_helm_%d_%s' % (j, o.data.name.split('.')[0].replace(' ', '_'))
    for mt in o.data.materials:   # the preview's tint (x 1, 0.9, 0.88) and roughness offset (0) before the armour finish
        nt = mt.node_tree; b = next(n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED'); ib = b.inputs['Base Color']
        if ib.is_linked:
            s_ = ib.links[0].from_socket; mx = nt.nodes.new('ShaderNodeMixRGB'); mx.blend_type = 'MULTIPLY'; mx.inputs[0].default_value = 1
            mx.inputs[2].default_value = (TINT, TINT * 0.9, TINT * 0.88, 1); nt.links.new(s_, mx.inputs[1]); nt.links.new(mx.outputs[0], ib)
    parts.append(o)
# ---- round shield
def frame(o):
    P = np.array([o.matrix_world @ v.co for v in o.data.vertices]); c = (P.min(0) + P.max(0)) / 2; w, v = np.linalg.eigh(np.cov((P - P.mean(0)).T)); return P, c, v[:, 0], v[:, 2]
old = bpy.data.objects['Shield']; Po, co, no, uo = frame(old)
if (co - bone_head('mixamorig:LeftForeArm')) @ no < 0: no = -no
before = set(bpy.data.objects)
bpy.ops.import_scene.gltf(filepath=SHLD); bpy.context.view_layer.update()
shs = [o for o in bpy.data.objects if o not in before and o.type == 'MESH']
bpy.ops.object.select_all(action='DESELECT')
for o in shs: o.select_set(True)
bpy.context.view_layer.objects.active = shs[0]
if len(shs) > 1: bpy.ops.object.join()
sh = shs[0]; bpy.ops.object.parent_clear(type='CLEAR_KEEP_TRANSFORM')
for o in [o for o in bpy.data.objects if o not in before and o != sh]: bpy.data.objects.remove(o, do_unlink=True)
bpy.context.view_layer.update()
Ps, cs, ns, us = frame(sh); ns = ns * FLIP
dia = float(np.ptp((Ps - cs) @ us)); ks = 0.62 / dia
def basis(n, u):
    n = n / np.linalg.norm(n); u = u - n * (u @ n); u = u / np.linalg.norm(u); return Matrix(np.column_stack([n, u, np.cross(n, u)]).tolist())
R = (basis(no, uo) @ basis(ns, us).inverted()).to_4x4()
sh.matrix_world = Matrix.Translation(Vector(co)) @ R @ Matrix.Diagonal((ks, ks, ks, 1)) @ Matrix.Translation(-Vector(cs)) @ sh.matrix_world
sh.name = 'KN2_shield'; parts.append(sh)
print('SHIELD across %.3f m (source %.3f), centre %s' % (dia * ks, dia, np.round(co, 3)))
# ---- the armour finish on the helm and shield (the preview's node set), then baked
def armour_finish(o, tiles):
    dim = max(max(v.co[i] for v in o.data.vertices) - min(v.co[i] for v in o.data.vertices) for i in range(3))
    for mt in o.data.materials:
        nt = mt.node_tree; b = next(n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED')
        tc = nt.nodes.new('ShaderNodeTexCoord'); mp = nt.nodes.new('ShaderNodeMapping'); s_ = tiles / dim; mp.inputs['Scale'].default_value = (s_, s_, s_)
        nt.links.new(tc.outputs['Object'], mp.inputs['Vector'])
        def tile(fn, cs):
            n = nt.nodes.new('ShaderNodeTexImage'); n.image = bpy.data.images.load(os.path.join(TEX, fn), check_existing=True); n.image.colorspace_settings.name = cs
            n.projection = 'BOX'; n.projection_blend = 0.35; nt.links.new(mp.outputs[0], n.inputs['Vector']); return n
        tb = tile('tile_base_clean.png', 'sRGB'); tmr = tile('tile_mr.png', 'Non-Color')
        ib = b.inputs['Base Color']; col = tb.outputs[0]
        if ib.is_linked:
            bw = nt.nodes.new('ShaderNodeRGBToBW'); nt.links.new(ib.links[0].from_socket, bw.inputs[0])
            ma = nt.nodes.new('ShaderNodeMath'); ma.operation = 'MULTIPLY_ADD'; ma.inputs[1].default_value = 0.7; ma.inputs[2].default_value = 0.6; nt.links.new(bw.outputs[0], ma.inputs[0])
            mx = nt.nodes.new('ShaderNodeMixRGB'); mx.blend_type = 'MULTIPLY'; mx.inputs[0].default_value = 1; nt.links.new(tb.outputs[0], mx.inputs[1]); nt.links.new(ma.outputs[0], mx.inputs[2]); col = mx.outputs[0]
        nt.links.new(col, ib)
        sp = nt.nodes.new('ShaderNodeSeparateColor'); nt.links.new(tmr.outputs[0], sp.inputs[0])
        nt.links.new(sp.outputs[1], b.inputs['Roughness']); nt.links.new(sp.outputs[2], b.inputs['Metallic'])
for o in parts:
    if o.name.startswith('KN2_helm'): armour_finish(o, 2.5)
armour_finish(sh, 3.0)
sc = bpy.context.scene; sc.render.engine = 'CYCLES'; sc.cycles.samples = 1; sc.cycles.device = 'CPU'
sc.render.bake.margin = 8
def bake(o):
    bpy.ops.object.select_all(action='DESELECT'); o.select_set(True); bpy.context.view_layer.objects.active = o
    for mi, mt in enumerate(o.data.materials):
        nt = mt.node_tree; b = next(n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED'); out = next(n for n in nt.nodes if n.type == 'OUTPUT_MATERIAL')
        src = {}
        for ch in ('Base Color', 'Roughness', 'Metallic'):
            i = b.inputs[ch]; src[ch] = i.links[0].from_socket if i.is_linked else i.default_value
        nrm = b.inputs['Normal'].links[0].from_socket if b.inputs['Normal'].is_linked else None
        surf = out.inputs['Surface'].links[0].from_socket
        em = nt.nodes.new('ShaderNodeEmission'); img = nt.nodes.new('ShaderNodeTexImage'); nt.nodes.active = img
        res = {}
        for ch in ('Base Color', 'Roughness', 'Metallic'):
            im = bpy.data.images.new('%s_%s' % (mt.name, ch[:4]), BAKE_PX, BAKE_PX, alpha=False)
            im.colorspace_settings.name = 'sRGB' if ch == 'Base Color' else 'Non-Color'; img.image = im
            for l in list(em.inputs[0].links): nt.links.remove(l)
            if isinstance(src[ch], bpy.types.NodeSocket): nt.links.new(src[ch], em.inputs[0])
            else:
                v = src[ch]; em.inputs[0].default_value = tuple(v) if hasattr(v, '__len__') else (v, v, v, 1)
            nt.links.new(em.outputs[0], out.inputs['Surface'])
            bpy.ops.object.bake(type='EMIT', use_clear=True)
            res[ch] = im
        # the exported material: baked colour, metal-rough packed (G roughness, B metal), the part's own normal map
        pr = np.array(res['Roughness'].pixels[:]).reshape(-1, 4); pm = np.array(res['Metallic'].pixels[:]).reshape(-1, 4)
        mr = bpy.data.images.new('%s_mr' % mt.name, BAKE_PX, BAKE_PX, alpha=False); mr.colorspace_settings.name = 'Non-Color'
        px = np.ones_like(pr); px[:, 0] = 1; px[:, 1] = pr[:, 0]; px[:, 2] = pm[:, 0]; mr.pixels[:] = px.ravel()
        for im in (res['Base Color'], mr): im.pack()
        nn = bpy.data.materials.new('KN2_%s_%d' % (o.name.split('_', 1)[1], mi)); nn.use_nodes = True; t2 = nn.node_tree; b2 = t2.nodes['Principled BSDF']
        ti = t2.nodes.new('ShaderNodeTexImage'); ti.image = res['Base Color']; t2.links.new(ti.outputs[0], b2.inputs['Base Color'])
        tm_ = t2.nodes.new('ShaderNodeTexImage'); tm_.image = mr; spc = t2.nodes.new('ShaderNodeSeparateColor'); t2.links.new(tm_.outputs[0], spc.inputs[0])
        t2.links.new(spc.outputs[1], b2.inputs['Roughness']); t2.links.new(spc.outputs[2], b2.inputs['Metallic'])
        if nrm is not None:   # the part's own normal map image, re-wired
            nimg = nrm.node.inputs['Color'].links[0].from_node.image if nrm.node.type == 'NORMAL_MAP' and nrm.node.inputs['Color'].is_linked else None
            if nimg:
                tn = t2.nodes.new('ShaderNodeTexImage'); tn.image = nimg; nmn = t2.nodes.new('ShaderNodeNormalMap'); nmn.inputs['Strength'].default_value = nrm.node.inputs['Strength'].default_value
                t2.links.new(tn.outputs[0], nmn.inputs['Color']); t2.links.new(nmn.outputs[0], b2.inputs['Normal'])
        o.data.materials[mi] = nn
        print('BAKED', o.name, mt.name, '->', nn.name)
for o in parts:
    if o != m: bake(o)
# the shield's boss faces away: when the scale flipped its handedness the normals follow the matrix (transform_apply flips them)
for o in parts:
    bpy.ops.object.select_all(action='DESELECT'); o.select_set(True); bpy.context.view_layer.objects.active = o
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
bpy.ops.object.select_all(action='DESELECT')
for o in parts: o.select_set(True)
bpy.ops.export_scene.gltf(filepath=OUT, export_format='GLB', use_selection=True, export_animations=False, export_skins=False, export_yup=True,
                          export_image_format='JPEG', export_jpeg_quality=90)
print('PARTS', [(o.name, len(o.data.polygons)) for o in parts])
print('wrote', OUT)
