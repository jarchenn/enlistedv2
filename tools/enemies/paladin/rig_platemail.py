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
base = lambda n: re.sub(r'_\d{2,6}$', '', n)
arms = [o for o in bpy.data.objects if o.type == 'ARMATURE']; main = arms[0]
for a in arms:
    for b in a.data.bones: b.name = base(b.name)
meshes = [o for o in bpy.data.objects if o.type == 'MESH']
allbones = {b.name for a_ in arms for b in a_.data.bones}
for o in meshes:   # renaming a bone renames its groups already: only the ones still numbered
    for vg in o.vertex_groups:
        if vg.name not in allbones: vg.name = base(vg.name)
helm = [o for o in meshes if not any(m.type == 'ARMATURE' for m in o.modifiers)]   # the rigid helmet: weighted to the head once the T-pose is the rest pose (below)
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
