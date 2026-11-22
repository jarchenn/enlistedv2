# DR1 Blender background converter for the Mountain Dragon (one GLB, one 96.8 s "Take 001" at 24 fps, animated in place):
# the take cut into the clips of dragon_spec.json (ground moves only: no flight / take-off / landing frames are kept),
# the take removed, the helper icosphere dropped, the model scaled from its centimetre units to metres (x 0.01 on the
# top node), textures at most 1024 px, JPEG. It already faces Blender -y (glTF +z, the game's front).
# Bones renamed in the written GLB as convert_enemy_glb.py does: mapped ones to the game's semantic joint names, the rest
# sanitized. usage: blender -b --factory-startup --python dragon_convert.py -- <in.glb> <out.glb> <dragon_map.json> <dragon_spec.json>
import bpy, sys, json, re, os, struct
from mathutils import Matrix, Vector
a = sys.argv[sys.argv.index('--') + 1:]
src, out, bmap, spec = a[0], a[1], json.load(open(a[2])), json.load(open(a[3]))
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
missing = [v for v in bmap.values() if v not in arm.data.bones]; assert not missing, missing
for o in [o for o in bpy.data.objects if o.type == 'MESH' and o.name == 'Icosphere']: bpy.data.objects.remove(o, do_unlink=True)
take = bpy.data.actions[0]
def use(act):
    ad = arm.animation_data or arm.animation_data_create(); ad.action = act
    if act.slots: ad.action_slot = act.slots[0]
for c in spec['clips']:
    f0, f1 = c['range']; samples = []
    use(take)
    for f in range(f0, f1 + 1):
        bpy.context.scene.frame_set(f); samples.append({p.name: p.matrix_basis.copy() for p in arm.pose.bones})
    act = bpy.data.actions.new(c['name']); act.use_fake_user = True; use(act)
    for i, basis in enumerate(samples):
        for p in arm.pose.bones:
            p.rotation_mode = 'QUATERNION'; p.matrix_basis = basis[p.name]
            for path in ('location', 'rotation_quaternion', 'scale'): p.keyframe_insert(path, frame=i + 1)
    print('CLIP', c['name'], f0, f1, len(samples), 'frames')
bpy.data.actions.remove(take)
arm.animation_data.action = None
# the bind pose is not standing (the source's lies turned, ~12 m tall, wings spread 18 m) and the game sizes and centres a
# model on its bind pose: idle's first frame becomes the bind pose (convert_enemy_glb.py rest_pose): the meshes take
# that shape, the armature is applied, every clip re-keyed against it
pbs = arm.pose.bones; acts = list(bpy.data.actions); rec = {}
for x in acts:
    use(x); n = int(x.frame_range[1]); rec[x.name] = []
    for f in range(1, n + 1):
        bpy.context.scene.frame_set(f); rec[x.name].append((f, {p.name: p.matrix.copy() for p in pbs}))
use(next(x for x in acts if x.name == 'idle')); bpy.context.scene.frame_set(1); bpy.context.view_layer.update()
meshes = [o for o in bpy.data.objects if o.type == 'MESH']
for m in meshes:
    bpy.ops.object.select_all(action='DESELECT'); m.select_set(True); bpy.context.view_layer.objects.active = m
    md = next(d for d in m.modifiers if d.type == 'ARMATURE'); nm0 = md.name
    bpy.ops.object.modifier_apply(modifier=nm0); m.modifiers.new(nm0, 'ARMATURE').object = arm
arm.animation_data.action = None
bpy.ops.object.select_all(action='DESELECT'); arm.select_set(True); bpy.context.view_layer.objects.active = arm
bpy.ops.object.mode_set(mode='POSE'); bpy.ops.pose.armature_apply(selected=False); bpy.ops.object.mode_set(mode='OBJECT')
restm = {b.name: b.matrix_local.copy() for b in arm.data.bones}
for x in acts:
    name = x.name; bpy.data.actions.remove(x)
    a2 = bpy.data.actions.new(name); a2.use_fake_user = True; use(a2)
    for f, pm in rec[name]:
        for p in pbs:
            par = p.parent
            L = restm[p.name] if not par else restm[par.name].inverted() @ restm[p.name]
            P0 = pm[par.name] if par else Matrix.Identity(4)
            p.rotation_mode = 'QUATERNION'; p.matrix_basis = L.inverted() @ P0.inverted() @ pm[p.name]
            for path in ('location', 'rotation_quaternion', 'scale'): p.keyframe_insert(path, frame=f)
arm.animation_data.action = None
print('REST POSE from idle frame 1;', len(acts), 'clips re-keyed')
top = arm
while top.parent: top = top.parent
top.matrix_world = Matrix.Scale(spec.get('scale', 0.01), 4) @ top.matrix_world   # centimetres -> metres
# the model's origin under the pelvis on the ground: the source stands ~12 m from its origin; the game puts the origin at
# the enemy's feet. Measured in the first frame of idle: the pelvis over (0, 0), the lowest skinned vertex at z = 0
ad = arm.animation_data or arm.animation_data_create(); idle = next(x for x in bpy.data.actions if x.name == 'idle'); use(idle)
bpy.context.scene.frame_set(1); bpy.context.view_layer.update()
pel = arm.matrix_world @ arm.pose.bones[bmap['Hips']].head
dg = bpy.context.evaluated_depsgraph_get()
lo = min((o.evaluated_get(dg).matrix_world @ v.co).z for o in bpy.data.objects if o.type == 'MESH' for v in o.evaluated_get(dg).to_mesh().vertices)
top.matrix_world = Matrix.Translation((-pel.x, -pel.y, -lo)) @ top.matrix_world
arm.animation_data.action = None; bpy.context.view_layer.update()
print('RECENTRED pelvis (%.2f, %.2f) -> origin, lowest vertex z %.2f -> 0' % (pel.x, pel.y, lo))
# paws on the ground (the user: the paws hovered): the source stands a little high in some takes (idle's paws 0.34 m over
# its tail, the walk's 0.07). Each clip is lowered by spec "lower_m" (its lowest paw vertex over the clip, measured on the
# exported GLB: E:/nmk/dragon/paws.py); the death keeps its fall. The game grounds the bind pose on its lowest vertex (the
# tail tip) and draws the clips under that, so the tail tip dips up to 0.34 m into the ground in idle
roots = [p for p in arm.pose.bones if not p.parent]
for name, low in spec.get('lower_m', {}).items():
    act = next(x for x in bpy.data.actions if x.name == name); use(act); n = int(act.frame_range[1])
    d = arm.matrix_world.inverted().to_3x3() @ Vector((0, 0, -low))
    for f in range(1, n + 1):
        bpy.context.scene.frame_set(f)
        for p in roots:
            mm = p.matrix.copy(); mm.translation += d; p.matrix = mm; p.keyframe_insert('location', frame=f)
    print('GROUNDED %s: lowered %.3f m' % (name, low))
arm.animation_data.action = None; bpy.context.view_layer.update()
for im in bpy.data.images:
    if im.size[0] and max(im.size) > 1024: im.scale(1024 * im.size[0] // max(im.size), 1024 * im.size[1] // max(im.size))
print('ACTIONS', sorted((x.name, tuple(x.frame_range)) for x in bpy.data.actions))
for o in bpy.data.objects: o.select_set(True)
bpy.context.view_layer.objects.active = arm
bpy.ops.export_scene.gltf(filepath=out, export_format='GLB', use_selection=True, export_animations=True, export_animation_mode='ACTIONS',
                          export_skins=True, export_yup=True, export_image_format='JPEG', export_jpeg_quality=85, export_force_sampling=True)
f = open(out, 'rb').read(); n = struct.unpack('<I', f[12:16])[0]; J = json.loads(f[20:20 + n]); rest = f[20 + n:]
inv = {v: k for k, v in bmap.items()}
for nd in J['nodes']:
    nm0 = nd.get('name', ''); nd['name'] = inv[nm0] if nm0 in inv else ('b_' + re.sub(r'[^A-Za-z0-9]+', '_', nm0).strip('_'))
js = json.dumps(J, separators=(',', ':')).encode(); js += b' ' * ((4 - len(js) % 4) % 4)
open(out, 'wb').write(struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(js) + len(rest)) + struct.pack('<II', len(js), 0x4E4F534A) + js + rest)
print('WROTE', out, os.path.getsize(out))
