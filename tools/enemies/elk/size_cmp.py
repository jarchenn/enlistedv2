# the Elk Knight beside Knight 10 and its on-foot self, each scaled as the game draws it (height_m / rest extent)
import bpy, sys, math, os
from mathutils import Vector
A = sys.argv[sys.argv.index('--') + 1:]; OUT = A[0]; ITEMS = [a.split('|') for a in A[1:]]   # glb|scale|x|clip|label
bpy.ops.wm.read_factory_settings(use_empty=True); sc = bpy.context.scene
for glb, s, x, clip, label in ITEMS:
    before = set(bpy.data.objects); acts0 = set(bpy.data.actions); bpy.ops.import_scene.gltf(filepath=glb)
    new = [o for o in bpy.data.objects if o not in before]; na = [a for a in bpy.data.actions if a not in acts0]
    arm = next(o for o in new if o.type == 'ARMATURE'); a = next(x_ for x_ in na if x_.name == clip or x_.name.startswith(clip))
    ad = arm.animation_data or arm.animation_data_create(); ad.action = a
    if a.slots: ad.action_slot = a.slots[0]
    for o in new:
        if o.parent is None: o.scale = tuple(c * float(s) for c in o.scale); o.location = (o.location.x * float(s) + float(x), o.location.y * float(s), o.location.z * float(s))
    cu = bpy.data.curves.new(label, 'FONT'); cu.body = label; cu.size = 0.22; cu.align_x = 'CENTER'
    t = bpy.data.objects.new('t_' + label, cu); sc.collection.objects.link(t); t.location = (float(x), 1.2, 0.02); t.rotation_euler = (math.radians(90), 0, 0)
sc.frame_set(1)
for h in (1.0, 2.0, 3.0):   # height marks, metres
    cu = bpy.data.curves.new('m%d' % h, 'FONT'); cu.body = '%d m' % h; cu.size = 0.16; t = bpy.data.objects.new('m%d' % h, cu); sc.collection.objects.link(t); t.location = (-3.6, 0.5, h); t.rotation_euler = (math.radians(90), 0, 0)
    import bmesh
    me = bpy.data.meshes.new('l'); bm = bmesh.new(); v1 = bm.verts.new((-3.2, 0.5, h)); v2 = bm.verts.new((4.2, 0.5, h)); v3 = bm.verts.new((4.2, 0.5, h + 0.008)); v4 = bm.verts.new((-3.2, 0.5, h + 0.008)); bm.faces.new((v1, v2, v3, v4)); bm.to_mesh(me); o = bpy.data.objects.new('l', me); sc.collection.objects.link(o)
w = bpy.data.worlds.new('w'); sc.world = w; w.use_nodes = True; w.node_tree.nodes['Background'].inputs[0].default_value = (0.6, 0.62, 0.66, 1)
for ang, en in ((-30, 3.5), (150, 1.5)):
    l = bpy.data.objects.new('s', bpy.data.lights.new('s', 'SUN')); sc.collection.objects.link(l); l.data.energy = en; l.rotation_euler = (math.radians(55), 0, math.radians(ang))
cam = bpy.data.objects.new('c', bpy.data.cameras.new('c')); sc.collection.objects.link(cam); sc.camera = cam; cam.data.type = 'ORTHO'; cam.data.ortho_scale = 8.4
cam.location = (0.4, -12, 1.7); cam.rotation_euler = (math.radians(90), 0, 0)
sc.render.engine = 'BLENDER_EEVEE'; sc.render.resolution_x = 1400; sc.render.resolution_y = 760; sc.render.filepath = OUT; bpy.ops.render.render(write_still=True)
