# EN1 Blender background converter: FBX (armature + skinned mesh + takes) -> skinned GLB for the game's CharModel.
# - only the armature and the named meshes are kept (effect empties, alpha hair cards dropped)
# - mapped bones renamed to the game's semantic joint names (src/character.cpp kSemanticNames), others sanitized
# - the model turned to face glTF +z (the game's clip analysis assumes it), one action per take (armature only)
# - textures: albedo + normal from the given PNGs, at most 1024 px
# usage: blender -b --factory-startup --python en1_convert.py -- <fbx> <out.glb> <alb.png> <nrm.png> <keep_mesh,...> <map.json>
import bpy, sys, json, re, math, os
a = sys.argv[sys.argv.index('--') + 1:]
src, out, alb, nrm, keep, mapf = a[0], a[1], a[2], a[3], a[4].split(','), a[5]
bmap = json.load(open(mapf))   # semantic name -> source bone name
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=src)
arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
meshes = [o for o in bpy.data.objects if o.type == 'MESH' and o.name in keep]
assert meshes, [o.name for o in bpy.data.objects if o.type == 'MESH']
# unparent what we keep from the effect empties (keep world transforms), then drop everything else
for o in [arm] + meshes:
    if o.parent and o.parent not in [arm] + meshes:
        mw = o.matrix_world.copy(); o.parent = None; o.matrix_world = mw
for o in list(bpy.data.objects):
    if o not in [arm] + meshes: bpy.data.objects.remove(o, do_unlink=True)
# facing: the head-ish bones ahead of the root along +y (Blender) -> turn 180 deg so it faces -y (glTF +z)
fwd_b = arm.data.bones.get(bmap['Head']); root_b = arm.data.bones.get(bmap['Hips'])
fy = (arm.matrix_world @ fwd_b.head_local).y - (arm.matrix_world @ root_b.head_local).y
if fy > 0:
    for o in [arm] + [m for m in meshes if m.parent is None]:
        o.matrix_world = __import__('mathutils').Matrix.Rotation(math.pi, 4, 'Z') @ o.matrix_world
print('FACING', 'turned' if fy > 0 else 'kept', round(fy, 5))
# bone names: semantic for the mapped ones, sanitized for the rest (renaming updates vertex groups and f-curves)
inv = {v: k for k, v in bmap.items()}
missing = [v for v in bmap.values() if v not in arm.data.bones]
assert not missing, missing
for b in list(arm.data.bones):
    old = b.name
    b.name = inv[old] if old in inv else ('b_' + re.sub(r'[^A-Za-z0-9]+', '_', old).strip('_'))
# actions: the armature's takes only, named after the take
for act in list(bpy.data.actions):
    parts = act.name.split('|')
    owner = parts[0]
    if owner != arm.name and not owner.startswith(arm.name.split('.')[0]):
        bpy.data.actions.remove(act); continue
    act.name = parts[1] if len(parts) > 1 else act.name
    act.use_fake_user = True
print('ACTIONS', len(bpy.data.actions), sorted(x.name for x in bpy.data.actions)[:50])
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
print('WROTE', out, os.path.getsize(out))
