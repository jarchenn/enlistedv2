# EN1/EN2 Blender background converter: FBX (armature + skinned mesh + takes) -> skinned GLB for the game's CharModel.
# - only the armature and the named meshes are kept (effect empties, alpha hair cards dropped)
# - the meshes are re-based into the armature's space (data transformed, parented, identity local) so the skinned
#   vertices stand where the bones are (the troll's mesh carried a 90 deg turn of its own)
# - object-level curves (root motion and the FBX parent chain's transform) are dropped from the takes: the armature
#   object keeps its import transform and the clips play in place; only pose.bones curves stay
# - the model turned to face glTF +z (the game's clip analysis assumes it), one action per take (armature only)
# - bone names are kept in Blender (renaming there does not reach the curves of inactive actions) and renamed in the
#   exported GLB: mapped bones to the game's semantic joint names (src/character.cpp kSemanticNames), others sanitized
# - textures: albedo + normal from the given PNGs, at most 1024 px
# usage: blender -b --factory-startup --python convert_enemy_glb.py -- <fbx> <out.glb> <alb.png> <nrm.png> <keep_mesh,...> <map.json>
import bpy, sys, json, re, math, os, struct
from mathutils import Matrix
a = sys.argv[sys.argv.index('--') + 1:]
src, out, alb, nrm, keep, mapf = a[0], a[1], a[2], a[3], a[4].split(','), a[5]
bmap = json.load(open(mapf))   # semantic name -> source bone name
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=src)
arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
meshes = [o for o in bpy.data.objects if o.type == 'MESH' and o.name in keep]
assert meshes, [o.name for o in bpy.data.objects if o.type == 'MESH']
missing = [v for v in bmap.values() if v not in arm.data.bones]
assert not missing, missing
bpy.context.view_layer.update()
# armature: unparent with its import (frame 1) world transform kept; its takes lose every non-bone curve
aw = arm.matrix_world.copy(); arm.parent = None; arm.matrix_world = aw
for act in list(bpy.data.actions):
    parts = act.name.split('|')
    if parts[0] != arm.name: bpy.data.actions.remove(act); continue
    act.name = parts[1] if len(parts) > 1 else act.name
    act.use_fake_user = True
    for l in act.layers:
        for s in l.strips:
            for cb in s.channelbags:
                for fc in [f for f in cb.fcurves if not f.data_path.startswith('pose.bones')]: cb.fcurves.remove(fc)
if arm.animation_data: arm.animation_data.action = None
arm.matrix_world = aw
# meshes: vertices into the armature's space, parented to it with an identity local transform. The FBX import can
# leave a mesh turned against its bones (the troll's lay on its back, mirrored), so the placement is fitted: of the
# 24 axis-aligned turns of the mesh data (scaled like the imported placement), the one that brings each vertex
# nearest the bone it is mostly weighted to wins.
import itertools
def seg(p, a, b):
    ab = b - a; t = max(0.0, min(1.0, (p - a).dot(ab) / max(1e-12, ab.dot(ab)))); return (p - (a + ab * t)).length
turns = []
for perm in itertools.permutations(range(3)):
    for sg in itertools.product((1, -1), repeat=3):
        R = Matrix.Identity(4)
        for r in range(3):
            for c in range(3): R[r][c] = sg[r] if perm[r] == c else 0
        if R.to_3x3().determinant() > 0: turns.append(R)
for m in meshes:
    rel = aw.inverted() @ m.matrix_world; sc = Matrix.Diagonal(rel.to_scale().to_4d())
    gname = {g.index: g.name for g in m.vertex_groups}
    samp = []
    for v in m.data.vertices[::max(1, len(m.data.vertices) // 3000)]:
        g = max(v.groups, key=lambda x: x.weight, default=None)
        b = arm.data.bones.get(gname.get(g.group, '')) if g else None
        if b: samp.append((v.co.copy(), b.head_local, b.tail_local))
    fit = [sum(seg(R @ sc @ co, h, t) for co, h, t in samp) / max(1, len(samp)) for R in turns]
    k = fit.index(min(fit)); M = turns[k] @ sc
    print('MESH', m.name, 'turn', [[round(x) for x in r] for r in turns[k].to_3x3()], 'mean dist to bone', round(fit[k], 4), 'next best', round(sorted(fit)[1], 4))
    m.data.transform(M)
    m.parent = arm; m.parent_type = 'OBJECT'; m.matrix_parent_inverse = Matrix.Identity(4); m.matrix_basis = Matrix.Identity(4)
    for md in m.modifiers:
        if md.type == 'ARMATURE': md.object = arm
for o in list(bpy.data.objects):
    if o not in [arm] + meshes: bpy.data.objects.remove(o, do_unlink=True)
bpy.context.view_layer.update()
# facing: the head ahead of the hips along +y (Blender) -> turn 180 deg so it faces -y (glTF +z). Judged mid-walk:
# the rest pose can face elsewhere (the spider's bind pose faces opposite its clips; the troll's stands near vertical)
walk = next((x for x in bpy.data.actions if 'walk' in x.name.lower() and 'back' not in x.name.lower()), None)
if walk:
    arm.animation_data_create(); arm.animation_data.action = walk
    if walk.slots: arm.animation_data.action_slot = walk.slots[0]
    bpy.context.scene.frame_set(int(sum(walk.frame_range) / 2)); bpy.context.view_layer.update()
pb = arm.pose.bones
fy = (arm.matrix_world @ pb[bmap['Head']].head).y - (arm.matrix_world @ pb[bmap['Hips']].head).y
if walk: arm.animation_data.action = None; bpy.context.scene.frame_set(1); arm.matrix_world = aw
if fy > 0: arm.matrix_world = Matrix.Rotation(math.pi, 4, 'Z') @ arm.matrix_world
print('FACING', 'turned' if fy > 0 else 'kept', round(fy, 5))
print('ACTIONS', len(bpy.data.actions), sorted((x.name, tuple(x.frame_range)) for x in bpy.data.actions))
# materials: one principled material from the two textures
def img(p):
    i = bpy.data.images.load(p)
    if max(i.size) > 1024: i.scale(1024 * i.size[0] // max(i.size), 1024 * i.size[1] // max(i.size))
    return i
mat = bpy.data.materials.new('body'); mat.use_nodes = True
nt = mat.node_tree; bsdf = nt.nodes['Principled BSDF']
ta = nt.nodes.new('ShaderNodeTexImage'); ta.image = img(alb); nt.links.new(ta.outputs['Color'], bsdf.inputs['Base Color'])
tn = nt.nodes.new('ShaderNodeTexImage'); tn.image = img(nrm); tn.image.colorspace_settings.name = 'Non-Color'
nm = nt.nodes.new('ShaderNodeNormalMap'); nt.links.new(tn.outputs['Color'], nm.inputs['Color']); nt.links.new(nm.outputs['Normal'], bsdf.inputs['Normal'])
bsdf.inputs['Roughness'].default_value = 0.8; bsdf.inputs['Metallic'].default_value = 0.0
for m in meshes:
    m.data.materials.clear(); m.data.materials.append(mat)
    for p in m.data.polygons: p.material_index = 0
for o in bpy.data.objects: o.select_set(True)
bpy.context.view_layer.objects.active = arm
bpy.ops.export_scene.gltf(filepath=out, export_format='GLB', use_selection=True, export_animations=True, export_animation_mode='ACTIONS',
                          export_skins=True, export_yup=True, export_image_format='AUTO', export_force_sampling=True)
# GLB node names: semantic for the mapped bones, sanitized for the rest (the JSON chunk is rewritten, the binary kept)
f = open(out, 'rb').read(); n = struct.unpack('<I', f[12:16])[0]; J = json.loads(f[20:20 + n]); rest = f[20 + n:]
inv = {v: k for k, v in bmap.items()}
for nd in J['nodes']:
    nm0 = nd.get('name', '')
    nd['name'] = inv[nm0] if nm0 in inv else ('b_' + re.sub(r'[^A-Za-z0-9]+', '_', nm0).strip('_'))
js = json.dumps(J, separators=(',', ':')).encode(); js += b' ' * ((4 - len(js) % 4) % 4)
open(out, 'wb').write(struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(js) + len(rest)) + struct.pack('<II', len(js), 0x4E4F534A) + js + rest)
print('WROTE', out, os.path.getsize(out))
