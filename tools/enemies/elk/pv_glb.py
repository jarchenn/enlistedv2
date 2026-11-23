# renders a skinned GLB's clips (Blender background): pv_glb.py -- <in.glb> <out dir> clip:frac ...
import bpy, sys, os, math
A = sys.argv[sys.argv.index('--') + 1:]; GLB, OUTD = A[:2]; SHOTS = [a.split(':') for a in A[2:]]
bpy.ops.wm.read_factory_settings(use_empty=True); bpy.ops.import_scene.gltf(filepath=GLB)
sc = bpy.context.scene; os.makedirs(OUTD, exist_ok=True)
for o in bpy.data.objects:
    if o.name in os.environ.get('PV_HIDE', '').split(','): o.hide_render = True
print('OBJECTS', [o.name for o in bpy.data.objects if o.type == 'MESH'])
arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE'); ad = arm.animation_data or arm.animation_data_create()
sc.render.engine = 'BLENDER_EEVEE'; sc.render.resolution_x = 560; sc.render.resolution_y = 520
w = bpy.data.worlds.new('w'); sc.world = w; w.use_nodes = True; nt = w.node_tree; nt.nodes.clear()   # the horse checks' light: sky, sun, ground
sky = nt.nodes.new('ShaderNodeTexSky'); bg = nt.nodes.new('ShaderNodeBackground'); wo = nt.nodes.new('ShaderNodeOutputWorld')
try: sky.sky_type = 'NISHITA'; sky.sun_elevation = math.radians(35); sky.sun_rotation = math.radians(-30)
except Exception: pass
bg.inputs[1].default_value = 0.3; nt.links.new(sky.outputs[0], bg.inputs[0]); nt.links.new(bg.outputs[0], wo.inputs[0])
sun = bpy.data.objects.new('sun', bpy.data.lights.new('sun', 'SUN')); sc.collection.objects.link(sun); sun.data.energy = 3.5; sun.rotation_euler = (math.radians(50), 0, math.radians(-30)); sun.data.angle = math.radians(2)
import bmesh
gpl = bpy.data.objects.new('ground', bpy.data.meshes.new('ground')); sc.collection.objects.link(gpl); gb = bmesh.new(); bmesh.ops.create_grid(gb, x_segments=1, y_segments=1, size=12); gb.to_mesh(gpl.data); gb.free()
gm = bpy.data.materials.new('g'); gm.use_nodes = True; gm.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = (0.18, 0.17, 0.15, 1); gpl.data.materials.append(gm)
try: sc.view_settings.view_transform = 'AgX'
except Exception: pass
cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam')); sc.collection.objects.link(cam); sc.camera = cam; cam.data.lens = 50
for clip, fr in SHOTS:
    a = next(x for x in bpy.data.actions if x.name.split('_Armature')[0] == clip or x.name == clip or x.name.startswith(clip + '_'))
    ad.action = a
    if a.slots: ad.action_slot = a.slots[0]
    f0, f1 = a.frame_range; f = f0 + (f1 - f0) * float(fr); fi = int(f); sc.frame_set(fi, subframe=f - fi)
    from mathutils import Vector
    V = {'s': ((7.5, -0.1, 1.5), (0, -0.1, 1.2)), 'q': ((5.2, -5.2, 2.6), (0, 0, 1.1)), 'head': ((1.5, -2.5, 1.8), (0, -1.05, 1.45)), 'rear': ((2.2, 2.6, 2.1), (0, 0.5, 1.2)), 'top': ((2.0, -0.6, 3.3), (0, -0.2, 1.4)), 'back': ((0.0, 4.2, 2.2), (0, 0.2, 1.9)), 'arms': ((2.6, -2.2, 2.3), (0, -0.4, 1.9)), 'wingside': ((3.0, 0.4, 2.1), (0, 0.4, 2.0)), 'front': ((0.0, -4.8, 2.2), (0, 0, 1.9))}
    for v in os.environ.get('PV_VIEWS', 's,q').split(','):
        lo, look = V[v]; cam.location = lo; cam.rotation_euler = (Vector(look) - Vector(lo)).to_track_quat('-Z', 'Y').to_euler()
        sc.render.filepath = os.path.join(OUTD, '%s_%s_%s.png' % (clip, fr, v)); bpy.ops.render.render(write_still=True)
print('ACTIONS', [x.name for x in bpy.data.actions])
