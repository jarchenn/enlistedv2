# Blender background (5.x): the FSB operator download -> a clean skinned GLB for tools/bake_gunbody.py --skin.
#   blender -b --factory-startup --disable-autoexec --python tools/fsb_prep.py -- <fsb_operator.glb> <out.glb>
#   then: python -I tools/bake_gunbody.py --skin content/characters/allied_body.glb <out.glb> content/characters/skin_fsb.glb
# The bind pose (A-pose) stood upright by the animation's own root turn at frame 1 (its armature rests on its side); only the
# AKS-74u dropped (Gun_99 with its stock and its own magazine: the game's guns are held instead); the four spare magazines, four
# pouches and two patches (rigid, each on its own animated empty: right only at frame 1) carried from frame 1 onto Spine (pouches,
# magazines), Spine2 (back patch), RightArm (shoulder patch), weight 1; decimated to ~26 k triangles; textures at most 1024 px.
# The T-pose comes from tools/bake_gunbody.py --skin (the arms re-aimed onto allied_body's T-pose rest).
import bpy, sys
from mathutils import Vector, Matrix
argv = sys.argv[sys.argv.index('--') + 1:]
SRC, OUT = argv[0], argv[1]
import os
TARGET_TRIS = int(os.environ.get('FSB_TRIS', '0'))   # 0: no decimation (the user: every detail but the rifle)
TEX_MAX = int(os.environ.get('FSB_TEX', '2048'))       # the source maps up to this (the 4096 uniform halves; the rest stay)
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=SRC, import_shading='NORMALS', guess_original_bind_pose=True)
arm = next(o for o in bpy.context.scene.objects if o.type == 'ARMATURE')
bn = {b.name.split(':')[-1].rsplit('_', 1)[0]: b.name for b in arm.data.bones}
def chain_has(o, words):
    p = o
    while p is not None:
        if any(w in p.name for w in words): return True
        p = p.parent
    return False
skinned = [o for o in bpy.context.scene.objects if o.type == 'MESH' and any(m.type == 'ARMATURE' for m in o.modifiers)]
rigid = [o for o in bpy.context.scene.objects if o.type == 'MESH' and o not in skinned]
drop = [o for o in rigid if chain_has(o, ('Gun_', 'Stock_')) or o.name.startswith('Icosphere')]   # the AKS-74u (its stock and its own magazine hang under Gun_99) only
keep = [o for o in rigid if o not in drop and chain_has(o, ('pouch', 'Plane', 'Magazine.'))]   # the pouches, both patches, the four spare magazines
other = [o for o in rigid if o not in drop and o not in keep]
print('skinned', len(skinned), 'rigid kept', [o.parent.name for o in keep], 'dropped', len(drop), 'unclassified', [o.name for o in other])
# 1. frame 1 (the animation stands him up): every pose bone's world matrix, the kept parts' worlds
bpy.context.scene.frame_set(1); bpy.context.view_layer.update()
posed = {pb.name: arm.matrix_world @ pb.matrix for pb in arm.pose.bones}
restW = {b.name: arm.matrix_world @ b.matrix_local for b in arm.data.bones}
hips = bn['Hips']; upright = posed[hips] @ restW[hips].inverted()
cands = [bn[k] for k in ('Hips', 'Spine', 'Spine1', 'Spine2', 'LeftUpLeg', 'RightUpLeg', 'LeftShoulder', 'RightShoulder', 'LeftArm', 'RightArm') if k in bn]
att = []
for o in keep:
    c = sum((o.matrix_world @ Vector(v) for v in o.bound_box), Vector()) / 8
    # each piece on its bone (not the nearest one: in the idle hold the hands sit in front of the chest): pouches and spare
    # magazines on Spine, the back patch (Plane_104) on Spine2, the shoulder patch (Plane.001_105) on RightArm
    b = bn['RightArm'] if chain_has(o, ('Plane.001',)) else bn['Spine2'] if chain_has(o, ('Plane_',)) else bn['Spine']
    att.append((o, b, posed[b].inverted() @ o.matrix_world))
    print('  %-22s -> %-22s (%.3f m from its joint)' % (o.parent.name if o.parent else o.name, b, (posed[b].translation - c).length))
# 2. back to the bind pose, the armature stood up; the kept parts placed on their bones there
if arm.animation_data: arm.animation_data.action = None
for pb in arm.pose.bones: pb.matrix_basis.identity()
for o in drop: bpy.data.objects.remove(o, do_unlink=True)
for o in other: bpy.data.objects.remove(o, do_unlink=True)
arm.matrix_world = upright @ arm.matrix_world
bpy.context.view_layer.update()
for o, b, local in att:
    w = arm.matrix_world @ arm.data.bones[b].matrix_local @ local
    o.parent = None; o.matrix_world = w
    bpy.context.view_layer.update()
    vg = o.vertex_groups.new(name=b); vg.add(list(range(len(o.data.vertices))), 1.0, 'REPLACE')
    md = o.modifiers.new('Armature', 'ARMATURE'); md.object = arm
    o.parent = arm; o.matrix_parent_inverse = arm.matrix_world.inverted()
meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
# 3. decimate to the budget (collapse keeps the vertex groups)
def tris(o): return sum(len(p.vertices) - 2 for p in o.data.polygons)
total = sum(tris(o) for o in meshes); r = min(1.0, TARGET_TRIS / max(1, total)) if TARGET_TRIS > 0 else 1.0
print('triangles %d -> ratio %.2f' % (total, r))
for o in meshes:
    if tris(o) < 300 or r >= 0.999: continue
    bpy.context.view_layer.objects.active = o
    md = o.modifiers.new('Dec', 'DECIMATE'); md.ratio = r; md.use_collapse_triangulate = True
    bpy.ops.object.modifier_move_to_index(modifier='Dec', index=0); bpy.ops.object.modifier_apply(modifier='Dec')
print('triangles after', sum(tris(o) for o in meshes))
# 4. textures at most 1024 px
for im in bpy.data.images:
    if im.size[0] > TEX_MAX or im.size[1] > TEX_MAX:
        k = TEX_MAX / max(im.size); im.scale(max(1, int(im.size[0] * k)), max(1, int(im.size[1] * k)))
    print('  image %-40s %dx%d' % (im.name[:40], im.size[0], im.size[1]))
# 5. export the armature and the meshes, the bind pose as rest, no clips
bpy.ops.object.select_all(action='DESELECT')
arm.select_set(True)
for o in meshes: o.select_set(True)
bpy.context.view_layer.objects.active = arm
bpy.ops.export_scene.gltf(filepath=OUT, export_format='GLB', use_selection=True, export_animations=False, export_skins=True, export_yup=True, export_image_format='JPEG', export_jpeg_quality=92)
print('wrote', OUT)
