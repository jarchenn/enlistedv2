# NPC5 (Blender background): a Sketchfab GLB of a clothed person -> a .blend that convert_enemy_glb.py reads, ready for
# retarget_clips.py (which needs Mixamo's T-pose as the bind pose and the 22 mapped joints).
#   - rigged (a Character Creator "CC_Base_" skeleton in an A-pose): the arms and legs turned to the donor's T-pose
#     directions, the meshes deformed into it, that pose made the bind pose
#   - unrigged (--donor): the donor (a Mixamo character .blend from fbx_takes_merge.py) scaled to the model's height,
#     its arms turned to the model's arm angle, its skin weights copied to the model by nearest surface, then as above
#   - parts the game cannot draw (it has no alpha: lashes, tear lines, eye occlusion, corneas, brow cards) dropped by
#     material name; hair cards (--hair: material name parts) cut to shape: faces whose texture is clear are deleted
#   - a material without a base-colour texture gets a 4 px one of its colour (the converter wants a file per material)
# Writes <out.blend>, <tex dir>/materials.json, <tex dir>/map.json (semantic joint -> bone) and prints KEEP <meshes>.
#   blender -b --factory-startup --disable-autoexec --python npc_skin_prep.py -- <in.glb> <out.blend> <tex dir> <donor.blend> [--unrigged] [--hair a,b] [--drop a,b]
import bpy, sys, os, json, re, math
import numpy as np
from mathutils import Vector, Matrix
A = sys.argv[sys.argv.index('--') + 1:]; SRC, OUT, TEX, DONOR = A[:4]
opt = lambda k: A[A.index(k) + 1].split(',') if k in A else []
HAIR = opt('--hair'); DROP = ['eyelash', 'tearline', 'occlusion', 'cornea', 'brow'] + opt('--drop'); UNRIGGED = '--unrigged' in A
SEM = ['Hips', 'Spine', 'Spine1', 'Spine2', 'Neck', 'Head', 'LeftShoulder', 'LeftArm', 'LeftForeArm', 'LeftHand', 'RightShoulder', 'RightArm', 'RightForeArm', 'RightHand',
       'LeftUpLeg', 'LeftLeg', 'LeftFoot', 'LeftToeBase', 'RightUpLeg', 'RightLeg', 'RightFoot', 'RightToeBase']
CC = {'Hips': 'Hip', 'Spine': 'Waist', 'Spine1': 'Spine01', 'Spine2': 'Spine02', 'Neck': 'NeckTwist01', 'Head': 'Head'}
for s, c in (('Left', 'L'), ('Right', 'R')):
    CC.update({s + 'Shoulder': c + '_Clavicle', s + 'Arm': c + '_Upperarm', s + 'ForeArm': c + '_Forearm', s + 'Hand': c + '_Hand',
               s + 'UpLeg': c + '_Thigh', s + 'Leg': c + '_Calf', s + 'Foot': c + '_Foot', s + 'ToeBase': c + '_ToeBase'})
CHAINS = [[s + 'Arm', s + 'ForeArm', s + 'Hand'] for s in ('Left', 'Right')] + [[s + 'UpLeg', s + 'Leg', s + 'Foot'] for s in ('Left', 'Right')]
os.makedirs(TEX, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
def sel(o, mode='OBJECT'):
    if bpy.context.object and bpy.context.object.mode != 'OBJECT': bpy.ops.object.mode_set(mode='OBJECT')
    bpy.ops.object.select_all(action='DESELECT'); o.select_set(True); bpy.context.view_layer.objects.active = o
    if mode != 'OBJECT': bpy.ops.object.mode_set(mode=mode)
# the donor: its armature (T-pose rest) and, for an unrigged model, its body to copy weights from
with bpy.data.libraries.load(DONOR) as (src, dst): dst.objects = list(src.objects)
for o in dst.objects: bpy.context.scene.collection.objects.link(o)
darm = next(o for o in dst.objects if o.type == 'ARMATURE'); dmesh = [o for o in dst.objects if o.type == 'MESH']
if darm.animation_data: darm.animation_data.action = None
for pb in darm.pose.bones: pb.matrix_basis = Matrix.Identity(4)   # the rest pose, whatever take was last shown
dmap = {k: next(b.name for b in darm.data.bones if b.name.split(':')[-1] == k) for k in SEM}
bpy.context.view_layer.update()
def dirs(arm, bm):   # world direction of each chain bone toward the next joint
    return {c[i]: ((arm.matrix_world @ arm.pose.bones[bm[c[i + 1]]].head) - (arm.matrix_world @ arm.pose.bones[bm[c[i]]].head)).normalized() for c in CHAINS for i in (0, 1)}
TDIR = dirs(darm, dmap)
before = set(bpy.data.objects); bpy.ops.import_scene.gltf(filepath=SRC); new = [o for o in bpy.data.objects if o not in before]
def matname(o): return (o.material_slots[0].material.name if o.material_slots and o.material_slots[0].material else '').lower()
meshes = [o for o in new if o.type == 'MESH' and o.material_slots and not any(d.lower() in matname(o) for d in DROP)]
drop = [o for o in new if o.type == 'MESH' and o not in meshes]; new = [o for o in new if o not in drop]
for o in drop: print('DROPPED', o.name, matname(o)); bpy.data.objects.remove(o, do_unlink=True)
for m in meshes:
    if m.data.shape_keys: m.shape_key_clear()
# hair cards: the game draws them solid, so the clear part of each card is cut away (a face whose mean texture alpha is low)
for m in meshes:
    if not any(h.lower() in matname(m) for h in HAIR): continue
    nt = m.material_slots[0].material.node_tree; im = next((n.image for n in nt.nodes if n.type == 'TEX_IMAGE' and n.image and n.image.channels == 4), None)
    if im is None: continue
    w, h = im.size; al = np.array(im.pixels[:], np.float32).reshape(h, w, 4)[:, :, 3]; uv = m.data.uv_layers.active.data; kill = []
    for p in m.data.polygons:
        pts = [uv[i].uv for i in p.loop_indices]; c = sum(pts, Vector((0, 0))) / len(pts); pts = [c] + [(q + c) / 2 for q in pts]
        if np.mean([al[int(q.y % 1 * (h - 1)), int(q.x % 1 * (w - 1))] for q in pts]) < 0.35: kill.append(p.index)
    import bmesh
    bm = bmesh.new(); bm.from_mesh(m.data); bm.faces.ensure_lookup_table(); bmesh.ops.delete(bm, geom=[bm.faces[i] for i in kill], context='FACES'); bm.to_mesh(m.data); bm.free()
    print('HAIR', m.name, matname(m), 'faces cut', len(kill), 'of', len(kill) + len(m.data.polygons))
def bake_xf(m):   # world transform into the mesh data
    mw = m.matrix_world.copy(); m.parent = None; m.data.transform(mw); m.matrix_world = Matrix.Identity(4)
def apply_arm(m, arm):   # the mesh takes the armature's pose as its shape and follows it again
    sel(m); md = next(d for d in m.modifiers if d.type == 'ARMATURE'); n = md.name; bpy.ops.object.modifier_apply(modifier=n); m.modifiers.new(n, 'ARMATURE').object = arm
def turn(arm, bm, tdir, mesh_list):   # chain bones to the target directions; the meshes into that pose; the pose becomes the bind pose
    Ri = arm.matrix_world.to_3x3().inverted()
    for c in CHAINS:
        for i in (0, 1):
            pb = arm.pose.bones[bm[c[i]]]; ch = arm.pose.bones[bm[c[i + 1]]]; cur = (ch.head - pb.head).normalized(); q = cur.rotation_difference((Ri @ tdir[c[i]]).normalized())
            hd = pb.head.copy(); pb.matrix = Matrix.Translation(hd) @ q.to_matrix().to_4x4() @ Matrix.Translation(-hd) @ pb.matrix; bpy.context.view_layer.update()
            print('TURN', c[i], round(math.degrees(q.angle), 1))
    for m in mesh_list: apply_arm(m, arm)
    sel(arm, 'POSE'); bpy.ops.pose.armature_apply(selected=False); bpy.ops.object.mode_set(mode='OBJECT')
if not UNRIGGED:
    arm = next(o for o in new if o.type == 'ARMATURE')
    bmap = {k: next(b.name for b in arm.data.bones if re.fullmatch('CC_Base_' + v + r'(_\d+)?', b.name)) for k, v in CC.items()}
    turn(arm, bmap, TDIR, meshes)
    for o in dst.objects: bpy.data.objects.remove(o, do_unlink=True)
else:
    for m in meshes: bake_xf(m)
    for o in [o for o in new if o.type != 'MESH']: bpy.data.objects.remove(o, do_unlink=True)
    V = np.array([v.co for m in meshes for v in m.data.vertices]); lo, hi = V.min(0), V.max(0); k0 = 1.0
    if hi[2] - lo[2] > 3: k0 = (0.01 if hi[2] - lo[2] < 300 else 0.001)   # centimetres / millimetres
    for m in meshes: m.data.transform(Matrix.Scale(k0, 4) @ Matrix.Translation((-(lo[0] + hi[0]) / 2, 0, -lo[2])))
    V = (V - [(lo[0] + hi[0]) / 2, 0, lo[2]]) * k0; H = V[:, 2].max()
    for dm in dmesh: bake_xf(dm)
    dH = max((v.co.z for dm in dmesh for v in dm.data.vertices)); dW = darm.matrix_world.copy()
    # the donor to the model's height, standing where the model stands (y: the mean of the torso's vertices)
    ty = V[(V[:, 2] > 0.5 * H) & (V[:, 2] < 0.7 * H) & (abs(V[:, 0]) < 0.12 * H), 1].mean(); DV = np.array([v.co for dm in dmesh for v in dm.data.vertices])
    dy = DV[(DV[:, 2] > 0.5 * dH) & (DV[:, 2] < 0.7 * dH) & (abs(DV[:, 0]) < 0.12 * dH), 1].mean(); S = Matrix.Translation((0, ty - dy * H / dH, 0)) @ Matrix.Scale(H / dH, 4)
    for dm in dmesh: dm.data.transform(S)
    darm.matrix_world = S @ dW; bpy.context.view_layer.update()
    # the model's arm angle: from the shoulder joint (the donor's) to the centre of the outermost 4 % of its vertices on that side
    adir = {}
    for s, sg in (('Left', 1), ('Right', -1)):
        x = V[:, 0] * sg; hand = V[x > x.max() - 0.04 * H].mean(0); sh = darm.matrix_world @ darm.pose.bones[dmap[s + 'Arm']].head
        d = (Vector(hand) - sh); d.y = 0; d.normalize(); adir[s + 'Arm'] = d; adir[s + 'ForeArm'] = d
        print('ARM', s, 'down %.1f deg' % math.degrees(math.atan2(-d.z, abs(d.x))))
    adir.update({k: v for k, v in TDIR.items() if 'Leg' in k})
    turn(darm, dmap, adir, dmesh)   # the donor in the model's pose
    for dm in dmesh[1:]: dm.select_set(True)
    sel(dmesh[0]); [dm.select_set(True) for dm in dmesh]; bpy.ops.object.join(); body = dmesh[0]
    for m in meshes:
        for b in darm.data.bones: m.vertex_groups.new(name=b.name)
        dt = m.modifiers.new('dt', 'DATA_TRANSFER'); dt.object = body; dt.use_vert_data = True; dt.data_types_verts = {'VGROUP_WEIGHTS'}; dt.vert_mapping = 'POLYINTERP_NEAREST'
        dt.layers_vgroup_select_src = 'ALL'; dt.layers_vgroup_select_dst = 'NAME'
        sel(m); bpy.ops.object.modifier_apply(modifier='dt'); m.modifiers.new('Armature', 'ARMATURE').object = darm; m.parent = darm; m.matrix_parent_inverse = darm.matrix_world.inverted()
    bpy.data.objects.remove(body, do_unlink=True)
    arm, bmap = darm, dmap; turn(arm, bmap, TDIR, meshes)   # back to the T-pose, the model with it
arm.name = 'Armature'
# textures
def img_of(sock):
    if not sock.is_linked: return None
    n = sock.links[0].from_node
    while n.type != 'TEX_IMAGE':
        ins = [i for i in n.inputs if i.is_linked]
        if not ins: return None
        n = ins[-1 if n.type == 'NORMAL_MAP' else 0].links[0].from_node
    return n.image
import struct
_b = open(SRC, 'rb').read(); _J = json.loads(_b[20:20 + struct.unpack('<I', _b[12:16])[0]])
FACT = {m.get('name'): (m.get('pbrMetallicRoughness', {}).get('baseColorFactor') or m.get('extensions', {}).get('KHR_materials_pbrSpecularGlossiness', {}).get('diffuseFactor') or [1, 1, 1]) for m in _J.get('materials', [])}
mats = {}
for mt in {s.material for m in meshes for s in m.material_slots if s.material}:
    b = next((n for n in mt.node_tree.nodes if n.type == 'BSDF_PRINCIPLED'), None); out = []; safe = re.sub(r'[^A-Za-z0-9_]+', '_', mt.name)
    for sock in (b.inputs['Base Color'], b.inputs['Normal']):
        im = img_of(sock); fn = '%s_%s.png' % (safe, 'base' if sock.name == 'Base Color' else 'nrm')
        if im is None and sock.name == 'Base Color':   # the file's own colour factor (the socket may be fed by nodes without an image)
            col = [x ** (1 / 2.2) for x in FACT.get(mt.name, [1, 1, 1])[:3]]; im = bpy.data.images.new(fn, 4, 4, alpha=False); im.pixels[:] = [c for _ in range(16) for c in (*col, 1.0)]
        if im is None: out.append(None); continue
        if im.source != 'GENERATED': im = im.copy()   # (a copy of a generated image comes back blank)
        im.filepath_raw = os.path.join(TEX, fn); im.file_format = 'PNG'; im.save(); out.append(fn)
    mats[mt.name] = out
json.dump(mats, open(os.path.join(TEX, 'materials.json'), 'w'), indent=1); json.dump(bmap, open(os.path.join(TEX, 'map.json'), 'w'))
zs = [(m.matrix_world @ v.co).z for m in meshes for v in m.data.vertices]
print('HEIGHT %.3f' % (max(zs) - min(zs))); print('KEEP', ','.join(m.name for m in meshes))
bpy.ops.wm.save_as_mainfile(filepath=OUT); print('wrote', OUT)
