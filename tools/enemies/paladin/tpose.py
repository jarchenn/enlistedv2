# the platemail (4 MetaHuman-rigged armatures, one per armour set) into a T-pose: upper arms, forearms and hands straight out
# along +X (left) / -X (right), fingers following; the pose baked into the meshes, armatures and empties removed. Saves a .blend
#   blender -b --factory-startup --disable-autoexec --python tpose.py -- <in.glb> <out.blend>
import bpy, sys, re, os
from mathutils import Vector, Matrix
A = sys.argv[sys.argv.index('--') + 1:]; SRC, OUT = A[:2]
bpy.ops.wm.read_factory_settings(use_empty=True); bpy.ops.import_scene.gltf(filepath=SRC)
for o in list(bpy.data.objects):
    if o.name.startswith('Icosphere'): bpy.data.objects.remove(o, do_unlink=True)   # stray 2 m spheres at the origin
base = lambda n: re.sub(r'_\d{2,6}$', '', n)
CHAINS = {('upperarm_l', 'lowerarm_l', 'hand_l', 'middle_01_l'): Vector((1, 0, 0)), ('upperarm_r', 'lowerarm_r', 'hand_r', 'middle_01_r'): Vector((-1, 0, 0))}   # each bone aimed from its head to the next one's head (glTF bones have no real tails)
arms = [o for o in bpy.data.objects if o.type == 'ARMATURE']
for arm in arms:
    bpy.context.view_layer.objects.active = arm; bpy.ops.object.mode_set(mode='POSE')
    names = {base(b.name): b.name for b in arm.pose.bones}; AW = arm.matrix_world
    for chain, target in CHAINS.items():
        for bn, nx in zip(chain, chain[1:]):
            pb = arm.pose.bones[names[bn]]; bpy.context.view_layer.update()
            h = AW @ pb.head; t = AW @ arm.pose.bones[names[nx]].head; cur = (t - h).normalized()
            R = cur.rotation_difference(target).to_matrix().to_4x4()   # world rotation about the bone's head
            W = AW @ pb.matrix; Wn = Matrix.Translation(h) @ R @ Matrix.Translation(-h) @ W
            pb.matrix = AW.inverted() @ Wn
    bpy.context.view_layer.update(); bpy.ops.object.mode_set(mode='OBJECT')
    hand = arm.pose.bones[names['hand_l']]; print('T', arm.name, 'left hand at', [round(x, 3) for x in AW @ hand.head])
# bake: each skinned mesh keeps its posed shape, unparented with its world transform, then the rigs and empties go
for o in [o for o in bpy.data.objects if o.type == 'MESH']:
    bpy.context.view_layer.objects.active = o; o.select_set(True)
    if o.data.shape_keys: print('shape keys on', o.name, '(dropped)'); o.shape_key_clear()
    for m in [m for m in o.modifiers if m.type == 'ARMATURE']: bpy.ops.object.modifier_apply(modifier=m.name)
    mw = o.matrix_world.copy(); o.parent = None; o.matrix_world = mw; o.select_set(False)
    o.vertex_groups.clear()
for o in [o for o in bpy.data.objects if o.type in ('ARMATURE', 'EMPTY')]: bpy.data.objects.remove(o, do_unlink=True)
for o in [o for o in bpy.data.objects if o.type == 'MESH']:   # transforms applied: plain meshes in metres, feet at 0
    bpy.ops.object.select_all(action='DESELECT'); o.select_set(True); bpy.context.view_layer.objects.active = o
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
print('MESHES', [(o.name, len(o.data.vertices)) for o in bpy.data.objects if o.type == 'MESH'])
bpy.ops.wm.save_as_mainfile(filepath=OUT); print('wrote', OUT)
if os.environ.get('FBX'):   # and the Mixamo FBX from this same session (the GLB's packed images are still here)
    OUT = os.environ['FBX']
    import os
    TEXD = os.path.join(os.path.dirname(os.path.abspath(OUT)), 'platemail_textures'); os.makedirs(TEXD, exist_ok=True)
    for i, im in enumerate(bpy.data.images):   # the GLB's images were packed with no file: written out (2048 px at most), so the FBX can embed them
        try: _ = im.pixels[0]   # loads a packed image's pixels
        except Exception: continue
        if not im.has_data: continue
        if im.size[0] > 2048: im.scale(2048, 2048 * im.size[1] // im.size[0])
        fn = os.path.join(TEXD, 'platemail_%d.png' % i); im.filepath_raw = fn; im.file_format = 'PNG'; im.save()
        if im.packed_file: im.unpack(method='REMOVE')
        im.filepath = fn; im.reload(); print('IMG', im.name, tuple(im.size), fn)
    for m in bpy.data.materials:   # FBX carries a texture only when it feeds the BSDF directly: the colour map past its vertex-colour mix
        if not m.use_nodes: continue
        nt = m.node_tree; bs = next((n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED'), None)
        mix = bs and bs.inputs['Base Color'].is_linked and bs.inputs['Base Color'].links[0].from_node
        if mix and mix.type == 'MIX':
            tex = next((l.from_node for i in mix.inputs if i.is_linked for l in i.links if l.from_node.type == 'TEX_IMAGE'), None)
            if tex: nt.links.new(tex.outputs['Color'], bs.inputs['Base Color']); print('REWIRE', m.name, tex.image.name)
    bpy.ops.object.select_all(action='DESELECT')
    for o in bpy.data.objects:
        if o.type == 'MESH': o.select_set(True)
    bpy.ops.export_scene.fbx(filepath=OUT, use_selection=True, object_types={'MESH'}, apply_unit_scale=True, apply_scale_options='FBX_SCALE_ALL',
                             axis_forward='-Z', axis_up='Y', path_mode='COPY', embed_textures=True, add_leaf_bones=False, mesh_smooth_type='FACE', bake_anim=False)
    print('wrote', OUT)

