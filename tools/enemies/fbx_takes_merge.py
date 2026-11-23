# NPC1 (Blender background): one character downloaded from Mixamo as one FBX per clip (each holding the skinned character
# and one take) -> one .blend with the character once and every clip as its own take, for convert_enemy_glb.py (which
# reads a .blend as it reads an FBX). Each take is named "<armature>|<clip>" (the converter keeps the part after the
# armature's name); the textures are saved as PNG into <tex dir> and listed per material (base colour, normal) in
# <tex dir>/materials.json for the converter's <alb.json> argument.
#   blender -b --factory-startup --disable-autoexec --python fbx_takes_merge.py -- <out.blend> <tex dir> <clip>=<file.fbx> ...
import bpy, sys, os, json
A = sys.argv[sys.argv.index('--') + 1:]; OUT, TEX = A[:2]; CLIPS = [x.split('=', 1) for x in A[2:]]
os.makedirs(TEX, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
arm = None
for i, (clip, path) in enumerate(CLIPS):
    before = set(bpy.data.objects); acts0 = set(bpy.data.actions)
    bpy.ops.import_scene.fbx(filepath=path)
    new = [o for o in bpy.data.objects if o not in before]; na = [a for a in bpy.data.actions if a not in acts0]
    a2 = next(o for o in new if o.type == 'ARMATURE')
    act = max(na, key=lambda a: a.frame_range[1] - a.frame_range[0])
    if arm is None: arm = a2; arm.name = 'Armature'
    else:   # only its take is wanted: the same rig, so its bone curves drive the first armature as they are
        missing = {b.name for b in a2.data.bones} ^ {b.name for b in arm.data.bones}
        assert not missing, (path, sorted(missing)[:5])
        for o in new: bpy.data.objects.remove(o, do_unlink=True)
    for a in na:
        if a is not act: bpy.data.actions.remove(a)
    act.name = 'Armature|' + clip; act.use_fake_user = True
    print('TAKE', clip, tuple(int(x) for x in act.frame_range), os.path.basename(path))
if arm.animation_data: arm.animation_data.action = None
for b in list(bpy.data.meshes) + list(bpy.data.armatures) + list(bpy.data.materials) + list(bpy.data.images):
    if b.users == 0: (getattr(bpy.data, {bpy.types.Mesh: 'meshes', bpy.types.Armature: 'armatures', bpy.types.Material: 'materials', bpy.types.Image: 'images'}[type(b)])).remove(b)
mats = {}
def img_of(sock):
    if not sock.is_linked: return None
    n = sock.links[0].from_node
    while n.type != 'TEX_IMAGE':
        ins = [i for i in n.inputs if i.is_linked]
        if not ins: return None
        n = ins[-1 if n.type == 'NORMAL_MAP' else 0].links[0].from_node
    return n.image
for m in {m for o in bpy.data.objects if o.type == 'MESH' for m in o.data.materials if m}:
    b = next((n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED'), None)
    if b is None: continue
    out = []
    for sock in (b.inputs['Base Color'], b.inputs['Normal']):
        im = img_of(sock)
        if im is None: out.append(None); continue
        fn = '%s_%s.png' % (m.name, 'base' if sock.name == 'Base Color' else 'nrm')
        if not im.has_data and not im.packed_file: out.append(None); continue   # BC1: a texture the FBX names but does not carry (Choi's normalmap_texture)
        im.filepath_raw = os.path.join(TEX, fn); im.file_format = 'PNG'; im.save(); out.append(fn)
    mats[m.name] = out
json.dump(mats, open(os.path.join(TEX, 'materials.json'), 'w'), indent=1); print('MATERIALS', mats)
bpy.ops.wm.save_as_mainfile(filepath=OUT); print('wrote', OUT)
