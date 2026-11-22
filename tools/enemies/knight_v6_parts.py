# Knight 6 (Blender background): the approved sallet knight (the preview script kcombo.py with HELM=sallet CHEST=metal
# WEAPON=sword SHIELD=polish SH_ROT=90; its numbers kept), placed on knight_ss.glb's rest pose, exported alone for
# tools/enemies/knight_v2_merge.py, plus the body's chest edits as a JSON of rest positions (glTF coordinates).
#   blender -b --factory-startup --disable-autoexec --python knight_v6_parts.py -- <knight_ss.glb> <anikin_head_3.glb>
#       <15th_century_italian_sallet.glb> <burgundian_chapelle_with_bevor.glb> <breastplate_-_metal.glb> <medieval_sword.glb>
#       <polish_mediaeval_shield.glb> <tex dir/> <out_parts.glb> <out_body_edits.json>
# Parts (rigid; node names KNB_<bone>_*: the merge hangs each on mixamorig:<bone>):
# - head: anikin_head_3 at 0.84 x 0.94, eye level (40 % down) at 1.645 - 0.06 - 0.03 + 0.028 m, 0.026 m back of the head
#   region's centre, its neck tapered in below the jaw (to 45 % at the bottom) so the collar closes over it
# - sallet: 0.215 m wide, top at 1.813 - 0.06 m, 0.04 m back; the chapelle file's bevor only (that file's helmet 0.22 m wide,
#   top at 1.835 - 0.06 m, 0.022 m back); both with the armour finish (2.5 tiles), baked into their own UVs
# - breastplate: 0.38 m wide, top at 1.50 m, centred on the chest, on Spine2, armour finish at 1.2 tiles (baked)
# - sword: 0.98 m, the wide end the hilt, gripped 11 % along from the pommel, on the old sword's axis, own materials
# - shield: 0.78 m on its long axis, centred where the old one was, painted face out, turned 90 deg; own texture
# Body edits: the chest (verts over 0.6 on Spine1 + Spine2) scaled to 0.73 across (x, y) about its centre; the mail /
# leather over 0.3 on Spine..Spine2 pulled in a further 0.86 (blended by weight outside the chest set).
import bpy, sys, os, json, struct, math
import numpy as np
from mathutils import Vector, Matrix, Quaternion
A = sys.argv[sys.argv.index('--') + 1:]
SRC, HEAD, SALLET, CHAP, BREAST, SWORD, SHLD, TEX, OUT, EDITS = A
BAKE_PX = 1024
DZ = -0.06; SHR = 0.73; MK = 0.86; NECK = 0.45; SH_ROT = 90.0
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=SRC, disable_bone_shape=True)
arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
fh = open(SRC, 'rb'); fh.read(12); n_, _ = struct.unpack('<II', fh.read(8)); J = json.loads(fh.read(n_)); NN = J['nodes']
par = {c: i for i, x in enumerate(NN) for c in x.get('children', [])}
def loc(x):
    q = x.get('rotation', (0, 0, 0, 1))
    return Matrix.Translation(x.get('translation', (0, 0, 0))) @ Quaternion((q[3], q[0], q[1], q[2])).to_matrix().to_4x4() @ Matrix.Diagonal((*x.get('scale', (1, 1, 1)), 1))
def world(i):
    m = loc(NN[i])
    while i in par: i = par[i]; m = loc(NN[i]) @ m
    return m
C = Matrix(((1, 0, 0, 0), (0, 0, -1, 0), (0, 1, 0, 0), (0, 0, 0, 1)))
arm.data.pose_position = 'REST'; bpy.context.view_layer.update()
for nm in ('Sword', 'Shield'):
    i = next(k for k, x in enumerate(NN) if x.get('name') == nm); bpy.data.objects[nm].matrix_world = C @ world(i) @ C.inverted()
bpy.context.view_layer.update()
def wpts(o): return np.array([o.matrix_world @ v.co for v in o.data.vertices])
def axis(o):
    P = wpts(o); c = P.mean(0); w, v = np.linalg.eigh(np.cov((P - c).T)); a = v[:, -1]; return P, c, a, (P - c) @ a
def imp(path):
    before = set(bpy.data.objects); bpy.ops.import_scene.gltf(filepath=path); bpy.context.view_layer.update()
    return [o for o in bpy.data.objects if o not in before]
def join(objs, name):
    names = [o.name for o in objs]; ms = [o for o in objs if o.type == 'MESH']
    bpy.ops.object.select_all(action='DESELECT')
    for o in ms: o.select_set(True)
    bpy.context.view_layer.objects.active = ms[0]
    if len(ms) > 1: bpy.ops.object.join()
    m = ms[0]; keep = m.name; bpy.ops.object.parent_clear(type='CLEAR_KEEP_TRANSFORM')
    for nm in names:   # the importer's empties (the joined meshes are gone already)
        if nm != keep and nm in bpy.data.objects: bpy.data.objects.remove(bpy.data.objects[nm], do_unlink=True)
    m.name = name; return m
parts = []; finish = {}   # finish: object -> tiles
# ---- the body's chest edits (positions only; the merge applies them to the skinned mesh)
kn = bpy.data.objects['Knight_Tpose']; W = kn.matrix_world; Wi = W.inverted()
orig = [W @ v.co for v in kn.data.vertices]
def region(names, thr):
    g = {x.index for x in kn.vertex_groups if x.name in names}; return [v.index for v in kn.data.vertices if sum(q.weight for q in v.groups if q.group in g) > thr]
ch = region(('mixamorig:Spine1', 'mixamorig:Spine2'), 0.6); CH = np.array([orig[i] for i in ch])
cx, cy_ = (CH.min(0)[0] + CH.max(0)[0]) / 2, (CH.min(0)[1] + CH.max(0)[1]) / 2
new = {}
for i in ch:
    p_ = orig[i].copy(); p_.x = cx + (p_.x - cx) * SHR; p_.y = cy_ + (p_.y - cy_) * SHR; new[i] = p_
mi_ = {i for i, m_ in enumerate(kn.data.materials) if m_ and m_.name in ('ChainMail', 'Leather')}
mv_ = {vi for pl in kn.data.polygons if pl.material_index in mi_ for vi in pl.vertices}
sg_ = {x.index for x in kn.vertex_groups if x.name in ('mixamorig:Spine', 'mixamorig:Spine1', 'mixamorig:Spine2')}
chs_ = set(ch)
for vi in mv_:
    w_ = sum(q.weight for q in kn.data.vertices[vi].groups if q.group in sg_)
    if w_ < 0.3: continue
    f_ = MK if vi in chs_ else 1 - (1 - SHR * MK) * min(1.0, w_)
    p_ = new.get(vi, orig[vi]).copy(); p_.x = cx + (p_.x - cx) * f_; p_.y = cy_ + (p_.y - cy_) * f_; new[vi] = p_
Ci = C.inverted()
json.dump({'moves': [[list(Ci @ orig[i]), list(Ci @ p)] for i, p in new.items()]}, open(EDITS, 'w'))
print('BODY chest %d verts, %d moved' % (len(ch), len(new)))
kill = region(('mixamorig:Head', 'mixamorig:HeadTop_End'), 0.5)
K = np.array([orig[i] for i in kill]); hc = (K.min(0) + K.max(0)) / 2
print('OLDHEAD', len(kill))
# ---- sword
sw = bpy.data.objects['Sword']; P, c, a, t = axis(sw)
th = float((np.array(arm.matrix_world @ arm.data.bones['mixamorig:RightHand'].head_local) - c) @ a)
if abs(t.max() - th) < abs(t.min() - th): a = -a; th = -th
grip_s = c + a * th
m = join(imp(SWORD), 'KNB_RightHand_sword'); bpy.ops.object.transform_apply(location=True, rotation=True, scale=True); bpy.context.view_layer.update()
Pm, cm, am, tm = axis(m); L = float(tm.max() - tm.min()); perp = np.linalg.norm((Pm - cm) - np.outer(tm, am), axis=1)
if not (perp[tm < tm.min() + 0.25 * L].max() > perp[tm > tm.max() - 0.25 * L].max()): am = -am; tm = -tm   # the wide end is the hilt
k = 0.98 / L; grip_m = cm + am * (tm.min() + 0.11 * L)
m.matrix_world = Matrix.Translation(Vector(grip_s)) @ Vector(am).rotation_difference(Vector(a)).to_matrix().to_4x4() @ Matrix.Diagonal((k, k, k, 1)) @ Matrix.Translation(-Vector(grip_m))
parts.append(m); print('SWORD %.3f m, grip %s' % (L * k, np.round(grip_s, 3)))
# ---- head
def fit(objs, width, top, dy, yaw, cy, sy=1.0, ref=None):
    Rz = Matrix.Rotation(math.radians(yaw), 4, 'Z'); P = np.vstack([np.array([Rz @ (o.matrix_world @ v.co) for v in o.data.vertices]) for o in objs if ref is None or ref in o.name.lower()]); lo, hi = P.min(0), P.max(0)
    k = width / (hi[0] - lo[0])
    T = Matrix.Translation((0, cy + dy, top)) @ Matrix.Diagonal((k, k * sy, k, 1)) @ Matrix.Translation((-(lo[0] + hi[0]) / 2, -(lo[1] + hi[1]) / 2, -hi[2])) @ Rz
    return [(o, T @ o.matrix_world) for o in objs]
fr = [o for o in imp(HEAD) if o.type == 'MESH']
FP = np.vstack([wpts(o) for o in fr]); fmin, fmax = FP.min(0), FP.max(0)
kf = 0.94 * 0.84; hh = fmax[2] - fmin[2]; HDZ = DZ + 0.0 - 0.03
Tf = Matrix.Translation((hc[0], hc[1] + 0.006 + 0.02, 1.645 + HDZ + 0.028)) @ Matrix.Diagonal((kf, kf, kf, 1)) @ Matrix.Translation((-(fmin[0] + fmax[0]) / 2, -(fmin[1] + fmax[1]) / 2, -(fmax[2] - 0.40 * hh)))
for o in fr:   # taper the neck so the knight's collar closes over it
    zc = fmax[2] - 0.78 * hh; Mw = o.matrix_world; Mi = Mw.inverted(); cx_, cy2 = (fmin[0] + fmax[0]) / 2, (fmin[1] + fmax[1]) / 2
    for v in o.data.vertices:
        w_ = Mw @ v.co
        if w_.z < zc:
            t_ = min(1.0, (zc - w_.z) / max(zc - fmin[2], 1e-6)); f_ = 1.0 - NECK * t_; w_.x = cx_ + (w_.x - cx_) * f_; w_.y = cy2 + 0.02 + (w_.y - cy2 - 0.02) * f_; v.co = Mi @ w_
    o.matrix_world = Tf @ o.matrix_world
head = join(fr, 'KNB_Head_head'); parts.append(head)
# ---- sallet + the chapelle's bevor
hs = [o for o in imp(SALLET) if o.type == 'MESH']
for o, M in fit(hs, 0.215, 1.813 + DZ, 0.04, 0, hc[1]): o.matrix_world = M
sal = join(hs, 'KNB_Head_sallet'); parts.append(sal); finish[sal] = 2.5
b2 = [o for o in imp(CHAP) if o.type == 'MESH']
bev = []
for o, M in fit(b2, 0.22, 1.835 + DZ, 0.022, 0, hc[1], ref='helmet'):
    if 'bevor' in o.name.lower(): o.matrix_world = M; bev.append(o)
    else: bpy.data.objects.remove(o, do_unlink=True)
assert bev, 'no bevor mesh'
bv = join(bev, 'KNB_Head_bevor'); parts.append(bv); finish[bv] = 2.5
# ---- breastplate
bs = [o for o in imp(BREAST) if o.type == 'MESH']
for o, M in fit(bs, 0.38, 1.50, 0.0, 0, (CH.min(0)[1] + CH.max(0)[1]) / 2, 1.0): o.matrix_world = M
for j, o in enumerate(bs):
    o.parent = None; o.name = 'KNB_Spine2_breast%d' % j; parts.append(o)
    if not any(x in ''.join(mt.name for mt in o.data.materials if mt).lower() for x in ('brass', 'strap')): finish[o] = 1.2
# ---- shield
old = bpy.data.objects['Shield']
def frame(o):
    P = wpts(o); c = (P.min(0) + P.max(0)) / 2; w, v = np.linalg.eigh(np.cov((P - P.mean(0)).T)); return P, c, v[:, 0], v[:, 2]
Po, co, no, uo = frame(old); fa = np.array(arm.matrix_world @ arm.data.bones['mixamorig:LeftForeArm'].head_local)
if (co - fa) @ no < 0: no = -no
sh = join(imp(SHLD), 'KNB_LeftForeArm_shield'); bpy.context.view_layer.update()
Ps, cs, ns, us = frame(sh); ns = -ns * -1.0; ext_ = float(np.ptp((Ps - cs) @ us)); rot_ = math.radians(SH_ROT)
us = math.cos(rot_) * us + math.sin(rot_) * np.cross(ns / np.linalg.norm(ns), us); ks = 0.78 / ext_
def basis(n, u):
    n = n / np.linalg.norm(n); u = u - n * (u @ n); u = u / np.linalg.norm(u); return Matrix(np.column_stack([n, u, np.cross(n, u)]).tolist())
sh.matrix_world = Matrix.Translation(Vector(co)) @ (basis(no, uo) @ basis(ns, us).inverted()).to_4x4() @ Matrix.Diagonal((ks, ks, ks, 1)) @ Matrix.Translation(-Vector(cs)) @ sh.matrix_world
parts.append(sh); print('SHIELD %.3f m long axis, centre %s' % (ext_ * ks, np.round(co, 3)))
# ---- the armour finish (the preview's node set), baked into each material's own UVs
def armour_finish(o, tiles):
    cvs = np.array([v.co for v in o.data.vertices]); dim = float((cvs.max(0) - cvs.min(0)).max())
    for mt in o.data.materials:
        if mt is None: continue
        nt = mt.node_tree; b = next((n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED'), None)
        if b is None: continue
        tc = nt.nodes.new('ShaderNodeTexCoord'); mp = nt.nodes.new('ShaderNodeMapping'); s_ = tiles / dim; mp.inputs['Scale'].default_value = (s_, s_, s_); nt.links.new(tc.outputs['Object'], mp.inputs['Vector'])
        def tile(fn, cs_):
            n = nt.nodes.new('ShaderNodeTexImage'); n.image = bpy.data.images.load(os.path.join(TEX, fn), check_existing=True); n.image.colorspace_settings.name = cs_; n.projection = 'BOX'; n.projection_blend = 0.35; nt.links.new(mp.outputs[0], n.inputs['Vector']); return n
        tb = tile('tile_base_clean.png', 'sRGB'); tmr = tile('tile_mr.png', 'Non-Color')
        ib = b.inputs['Base Color']; col = tb.outputs[0]
        if ib.is_linked:
            bw = nt.nodes.new('ShaderNodeRGBToBW'); nt.links.new(ib.links[0].from_socket, bw.inputs[0])
            ma = nt.nodes.new('ShaderNodeMath'); ma.operation = 'MULTIPLY_ADD'; ma.inputs[1].default_value = 0.7; ma.inputs[2].default_value = 0.6; nt.links.new(bw.outputs[0], ma.inputs[0])
            mx = nt.nodes.new('ShaderNodeMixRGB'); mx.blend_type = 'MULTIPLY'; mx.inputs[0].default_value = 1; nt.links.new(tb.outputs[0], mx.inputs[1]); nt.links.new(ma.outputs[0], mx.inputs[2]); col = mx.outputs[0]
        nt.links.new(col, ib)
        sp = nt.nodes.new('ShaderNodeSeparateColor'); nt.links.new(tmr.outputs[0], sp.inputs[0]); nt.links.new(sp.outputs[1], b.inputs['Roughness']); nt.links.new(sp.outputs[2], b.inputs['Metallic'])
for o, t_ in finish.items():
    for i, mt in enumerate(o.data.materials):   # single-user materials: the bake rewires them
        if mt and mt.users > 1: o.data.materials[i] = mt.copy()
    armour_finish(o, t_)
sc = bpy.context.scene; sc.render.engine = 'CYCLES'; sc.cycles.samples = 1; sc.cycles.device = 'CPU'; sc.render.bake.margin = 8
def bake(o):
    bpy.ops.object.select_all(action='DESELECT'); o.select_set(True); bpy.context.view_layer.objects.active = o
    for mi, mt in enumerate(o.data.materials):
        if mt is None: continue
        nt = mt.node_tree; b = next(n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED'); out = next(n for n in nt.nodes if n.type == 'OUTPUT_MATERIAL')
        src = {ch: (b.inputs[ch].links[0].from_socket if b.inputs[ch].is_linked else b.inputs[ch].default_value) for ch in ('Base Color', 'Roughness', 'Metallic')}
        nrm = b.inputs['Normal'].links[0].from_socket if b.inputs['Normal'].is_linked else None
        em = nt.nodes.new('ShaderNodeEmission'); img = nt.nodes.new('ShaderNodeTexImage'); nt.nodes.active = img; res = {}
        for ch in ('Base Color', 'Roughness', 'Metallic'):
            im = bpy.data.images.new('%s_%s' % (mt.name, ch[:4]), BAKE_PX, BAKE_PX, alpha=False); im.colorspace_settings.name = 'sRGB' if ch == 'Base Color' else 'Non-Color'; img.image = im
            for l in list(em.inputs[0].links): nt.links.remove(l)
            if isinstance(src[ch], bpy.types.NodeSocket): nt.links.new(src[ch], em.inputs[0])
            else: v = src[ch]; em.inputs[0].default_value = tuple(v) if hasattr(v, '__len__') else (v, v, v, 1)
            nt.links.new(em.outputs[0], out.inputs['Surface']); bpy.ops.object.bake(type='EMIT', use_clear=True); res[ch] = im
        pr = np.array(res['Roughness'].pixels[:]).reshape(-1, 4); pm = np.array(res['Metallic'].pixels[:]).reshape(-1, 4)
        mr = bpy.data.images.new('%s_mr' % mt.name, BAKE_PX, BAKE_PX, alpha=False); mr.colorspace_settings.name = 'Non-Color'
        px = np.ones_like(pr); px[:, 1] = pr[:, 0]; px[:, 2] = pm[:, 0]; mr.pixels[:] = px.ravel()
        for im in (res['Base Color'], mr): im.pack()
        nn = bpy.data.materials.new('%s_%d' % (o.name, mi)); nn.use_nodes = True; t2 = nn.node_tree; b2 = t2.nodes['Principled BSDF']
        ti = t2.nodes.new('ShaderNodeTexImage'); ti.image = res['Base Color']; t2.links.new(ti.outputs[0], b2.inputs['Base Color'])
        tm_ = t2.nodes.new('ShaderNodeTexImage'); tm_.image = mr; spc = t2.nodes.new('ShaderNodeSeparateColor'); t2.links.new(tm_.outputs[0], spc.inputs[0])
        t2.links.new(spc.outputs[1], b2.inputs['Roughness']); t2.links.new(spc.outputs[2], b2.inputs['Metallic'])
        if nrm is not None and nrm.node.type == 'NORMAL_MAP' and nrm.node.inputs['Color'].is_linked:
            tn = t2.nodes.new('ShaderNodeTexImage'); tn.image = nrm.node.inputs['Color'].links[0].from_node.image; nmn = t2.nodes.new('ShaderNodeNormalMap')
            nmn.inputs['Strength'].default_value = nrm.node.inputs['Strength'].default_value; t2.links.new(tn.outputs[0], nmn.inputs['Color']); t2.links.new(nmn.outputs[0], b2.inputs['Normal'])
        o.data.materials[mi] = nn; print('BAKED', o.name, mt.name)
for o in finish: bake(o)
for o in parts:
    bpy.ops.object.select_all(action='DESELECT'); o.select_set(True); bpy.context.view_layer.objects.active = o
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
bpy.ops.object.select_all(action='DESELECT')
for o in parts: o.select_set(True)
bpy.ops.export_scene.gltf(filepath=OUT, export_format='GLB', use_selection=True, export_animations=False, export_skins=False, export_yup=True, export_image_format='JPEG', export_jpeg_quality=90)
print('PARTS', [(o.name, len(o.data.polygons)) for o in parts]); print('wrote', OUT, EDITS)
