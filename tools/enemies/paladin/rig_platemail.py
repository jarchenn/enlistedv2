# the platemail's MetaHuman rigs -> one Mixamo-named rig in a T-pose bind, for the knights' Mixamo clips (retarget_clips.py)
# - the four armour sets' armatures (the same skeleton, numbered names) to base names; all meshes onto the first armature
# - the T-pose (arms straight out along +-X), applied as the rest pose; the helmet (a rigid prop) weighted to the head
# - MetaHuman joints mapped to Mixamo's; every other joint's weights merged into its nearest mapped ancestor, then removed
# - a mixamorig:Weapon joint on the right hand (as on Knight 4); exported as one skinned GLB, no clips
#   blender -b --factory-startup --disable-autoexec --python rig_platemail.py -- <platemail.glb> <out.glb>
import bpy, sys, re
from mathutils import Vector, Matrix
A = sys.argv[sys.argv.index('--') + 1:]; SRC, OUT = A[:2]
bpy.ops.wm.read_factory_settings(use_empty=True); bpy.ops.import_scene.gltf(filepath=SRC)
for o in list(bpy.data.objects):
    if o.name.startswith('Icosphere'): bpy.data.objects.remove(o, do_unlink=True)
base = lambda n: re.sub(r'_\d{3,6}$', '', n)   # the export's numbering (3+ digits), not MetaHuman's own _01 / _02
arms = [o for o in bpy.data.objects if o.type == 'ARMATURE']; main = arms[0]
for a in arms:
    for b in a.data.bones: b.name = base(b.name)
meshes = [o for o in bpy.data.objects if o.type == 'MESH']
allbones = {b.name for a_ in arms for b in a_.data.bones}
for o in meshes:   # renaming a bone renames its groups already: only the ones still numbered
    for vg in o.vertex_groups:
        if vg.name not in allbones: vg.name = base(vg.name)
helm = [o for o in meshes if not any(m.type == 'ARMATURE' for m in o.modifiers)]   # the rigid helmet: weighted to the head once the T-pose is the rest pose (below)
# stray weights (the heavy armour's belt straps carry arm weights in the source): a vertex weighted to an arm joint but over 0.35 m
# from that arm (shoulder to fingertips) gives that weight to the pelvis, so the T-pose does not drag it along
ARMK = ('clavicle', 'upperarm', 'lowerarm', 'hand', 'wrist', 'thumb', 'index', 'middle', 'ring', 'pinky', 'metacarpal')
fixed = 0
for o in [o for o in meshes if o.find_armature()]:
    a_ = o.find_armature(); AW_ = a_.matrix_world; bj = {b.name: AW_ @ b.head_local for b in a_.data.bones}
    side_pts = {}
    for sd in ('l', 'r'): side_pts[sd] = [p_ for n_, p_ in bj.items() if n_.endswith('_' + sd) and any(k in n_ for k in ARMK)]
    gi = {g.index: g.name for g in o.vertex_groups}; pel = o.vertex_groups.get('pelvis') or o.vertex_groups.new(name='pelvis'); mw = o.matrix_world
    for v in o.data.vertices:
        p_ = mw @ v.co
        for g_ in list(v.groups):
            nm = gi.get(g_.group, '')
            if g_.weight <= 0 or not any(k in nm for k in ARMK) or not (nm.endswith('_l') or nm.endswith('_r')): continue
            body = any(gi.get(h.group, '').startswith(('spine', 'pelvis', 'thigh')) and h.weight > 0.05 for h in v.groups)
            lower = any(k in nm for k in ('lowerarm', 'hand', 'wrist', 'thumb', 'index', 'middle', 'ring', 'pinky', 'metacarpal'))
            if min((p_ - q).length for q in side_pts[nm[-1]]) > 0.35 or (body and lower):   # far from the arm, or a body piece the hand brushes
                pel.add([v.index], g_.weight, 'ADD'); o.vertex_groups[g_.group].remove([v.index]); fixed += 1
print('STRAY arm weights moved to the pelvis:', fixed)
# a loose piece of a garment that is mostly body-weighted (a belt end) but partly on a forearm or hand: all of it on the body
import bmesh
LOWER = ('lowerarm', 'hand', 'wrist', 'thumb', 'index', 'middle', 'ring', 'pinky', 'metacarpal')
moved = 0
for o in [o for o in meshes if o.find_armature()]:
    gi = {g.index: g.name for g in o.vertex_groups}; pel = o.vertex_groups.get('pelvis') or o.vertex_groups.new(name='pelvis')
    bm = bmesh.new(); bm.from_mesh(o.data); bm.verts.ensure_lookup_table(); seen = set()
    for v0 in bm.verts:
        if v0.index in seen: continue
        isl, st = [], [v0]; seen.add(v0.index)
        while st:
            x = st.pop(); isl.append(x.index)
            for e in x.link_edges:
                y = e.other_vert(x)
                if y.index not in seen: seen.add(y.index); st.append(y)
        body = low = 0.0
        for i in isl:
            for g_ in o.data.vertices[i].groups:
                nm = gi.get(g_.group, '')
                if nm.startswith(('spine', 'pelvis', 'thigh')): body += g_.weight
                elif any(k in nm for k in LOWER): low += g_.weight
        if low > 0 and body > 2 * low:
            for i in isl:
                for g_ in list(o.data.vertices[i].groups):
                    nm = gi.get(g_.group, '')
                    if any(k in nm for k in LOWER) and g_.weight > 0: pel.add([i], g_.weight, 'ADD'); o.vertex_groups[g_.group].remove([i]); moved += 1
    bm.free()
print('STRAY loose body pieces: %d hand / forearm weights moved to the pelvis' % moved)
# T-pose on every armature (the meshes follow their own), each bone aimed head -> next head
CH = {('upperarm_l', 'lowerarm_l', 'hand_l', 'middle_01_l'): Vector((1, 0, 0)), ('upperarm_r', 'lowerarm_r', 'hand_r', 'middle_01_r'): Vector((-1, 0, 0))}
for a in arms:
    bpy.context.view_layer.objects.active = a; bpy.ops.object.mode_set(mode='POSE'); AW = a.matrix_world
    for chain, target in CH.items():
        for bn, nx in zip(chain, chain[1:]):
            pb = a.pose.bones[bn]; bpy.context.view_layer.update()
            h = AW @ pb.head; t = AW @ a.pose.bones[nx].head; R = (t - h).normalized().rotation_difference(target).to_matrix().to_4x4()
            pb.matrix = AW.inverted() @ (Matrix.Translation(h) @ R @ Matrix.Translation(-h) @ (AW @ pb.matrix))
    bpy.ops.object.mode_set(mode='OBJECT')
bpy.context.view_layer.update()
# bake the T-pose into the meshes, then the main armature's pose becomes its rest; every mesh on the main armature
for o in meshes:
    bpy.ops.object.select_all(action='DESELECT'); o.select_set(True); bpy.context.view_layer.objects.active = o
    for m in [m for m in o.modifiers if m.type == 'ARMATURE']: bpy.ops.object.modifier_apply(modifier=m.name)
    mw = o.matrix_world.copy(); o.parent = None; o.matrix_world = mw
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
# a small loose piece left far from the body below the shoulders after the bake (the heavy armour's belt end): removed
import bmesh
gone = 0
for o in meshes:
    bm = bmesh.new(); bm.from_mesh(o.data); bm.verts.ensure_lookup_table(); seen = set(); kill = []
    for v0 in bm.verts:
        if v0.index in seen: continue
        isl, st = [], [v0]; seen.add(v0.index)
        while st:
            x = st.pop(); isl.append(x)
            for e in x.link_edges:
                y = e.other_vert(x)
                if y.index not in seen: seen.add(y.index); st.append(y)
        far = [v for v in isl if (v.co.x ** 2 + v.co.y ** 2) ** 0.5 > 0.6 and v.co.z < 1.2]
        if far and len(isl) < 500: kill += isl
    if kill: bmesh.ops.delete(bm, geom=kill, context='VERTS'); bm.to_mesh(o.data); gone += len(kill)
    bm.free()
print('LOOSE pieces removed:', gone, 'vertices')
bpy.ops.object.select_all(action='DESELECT'); bpy.context.view_layer.objects.active = main; main.select_set(True)
bpy.ops.object.mode_set(mode='POSE'); bpy.ops.pose.select_all(action='SELECT'); bpy.ops.pose.armature_apply(selected=False); bpy.ops.object.mode_set(mode='OBJECT')
mw = main.matrix_world.copy(); main.parent = None; main.matrix_world = mw
bpy.ops.object.select_all(action='DESELECT'); main.select_set(True); bpy.context.view_layer.objects.active = main
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
for a in arms[1:]: bpy.data.objects.remove(a, do_unlink=True)
for o in helm:   # the helmet, where it stands, weight 1 on the head
    vg = o.vertex_groups.new(name='head'); vg.add(list(range(len(o.data.vertices))), 1.0, 'REPLACE')
import os
if os.environ.get('DBG'):
    import math
    sc = bpy.context.scene; cam = bpy.data.objects.new('c', bpy.data.cameras.new('c')); sc.collection.objects.link(cam); sc.camera = cam; cam.data.type = 'ORTHO'; cam.data.ortho_scale = 4
    l = bpy.data.objects.new('s', bpy.data.lights.new('s', 'SUN')); sc.collection.objects.link(l); l.data.energy = 3
    sc.render.engine = 'BLENDER_EEVEE'; sc.render.resolution_x = 500; sc.render.resolution_y = 500
    for o in meshes:
        if not any(m.type == 'ARMATURE' for m in o.modifiers): o.modifiers.new('Armature', 'ARMATURE').object = main
    for pp in ('REST', 'POSE'):
        main.data.pose_position = pp; cam.location = (6, 0, 0.9); cam.rotation_euler = (math.radians(90), 0, math.radians(90))
        sc.render.filepath = os.environ['DBG'] + '_' + pp + '.png'; bpy.ops.render.render(write_still=True)
    main.data.pose_position = 'POSE'
for o in [o for o in bpy.data.objects if o.type == 'EMPTY']: bpy.data.objects.remove(o, do_unlink=True)
# MetaHuman -> Mixamo
MAP = {'pelvis': 'Hips', 'spine_01': 'Spine', 'spine_03': 'Spine1', 'spine_05': 'Spine2', 'neck_01': 'Neck', 'head': 'Head'}
for s, S in (('l', 'Left'), ('r', 'Right')):
    MAP.update({'clavicle_' + s: S + 'Shoulder', 'upperarm_' + s: S + 'Arm', 'lowerarm_' + s: S + 'ForeArm', 'hand_' + s: S + 'Hand',
                'thigh_' + s: S + 'UpLeg', 'calf_' + s: S + 'Leg', 'foot_' + s: S + 'Foot', 'ball_' + s: S + 'ToeBase'})
    for f, F in (('thumb', 'Thumb'), ('index', 'Index'), ('middle', 'Middle'), ('ring', 'Ring'), ('pinky', 'Pinky')):
        for k in (1, 2, 3): MAP['%s_0%d_%s' % (f, k, s)] = '%sHand%s%d' % (S, F, k)
bones = main.data.bones; missing = [k for k in MAP if k not in bones]; print('MAP missing', missing)
keep = {k for k in MAP if k in bones}
def target(name):
    b = bones[name]
    while b is not None and b.name not in keep: b = b.parent
    return b.name if b else 'pelvis'
for o in meshes:   # weights of the joints that go: onto their nearest kept ancestor
    o.parent = main; o.matrix_parent_inverse = main.matrix_world.inverted()
    md = o.modifiers.new('Armature', 'ARMATURE'); md.object = main
    for vg in list(o.vertex_groups):
        if vg.name in keep or vg.name not in bones: continue
        t = target(vg.name); tg = o.vertex_groups.get(t) or o.vertex_groups.new(name=t)
        idx = vg.index
        for v in o.data.vertices:
            for gg in v.groups:
                if gg.group == idx and gg.weight > 0: tg.add([v.index], gg.weight, 'ADD')
        o.vertex_groups.remove(vg)
    for vg in list(o.vertex_groups):
        if vg.name not in keep: o.vertex_groups.remove(vg)
bpy.context.view_layer.objects.active = main; bpy.ops.object.mode_set(mode='EDIT'); eb = main.data.edit_bones
for b in list(eb):
    if b.name not in keep: eb.remove(b)
h = eb['hand_r']; w = eb.new('Weapon'); w.head, w.tail, w.roll = h.head.copy(), h.tail.copy(), h.roll; w.parent = h   # as Knight 4's: on the right hand, its frame
for b in eb:
    if b.name in MAP: b.name = 'mixamorig:' + MAP[b.name]
w.name = 'mixamorig:Weapon'
bpy.ops.object.mode_set(mode='OBJECT')
for o in meshes:
    for vg in o.vertex_groups:
        if vg.name in MAP: vg.name = 'mixamorig:' + MAP[vg.name]
print('BONES', len(main.data.bones), [b.name for b in main.data.bones][:12])
print('HIPS', [round(x, 3) for x in main.matrix_world @ main.data.bones['mixamorig:Hips'].head_local], 'RHAND', [round(x, 3) for x in main.matrix_world @ main.data.bones['mixamorig:RightHand'].head_local])
bpy.ops.object.select_all(action='DESELECT'); main.select_set(True)
for o in meshes: o.select_set(True)
bpy.ops.export_scene.gltf(filepath=OUT, export_format='GLB', use_selection=True, export_skins=True, export_animations=False, export_yup=True, export_image_format='JPEG', export_jpeg_quality=90)
print('wrote', OUT)
