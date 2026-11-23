# EK5: every clip of a skinned GLB, one after another, rendered to one MP4 with the clip's name in the corner (Blender background)
#   blender -b --factory-startup --disable-autoexec --python anim_mp4.py -- <in.glb> <out.mp4> [clip ...]
import bpy, sys, os, math, bmesh
from mathutils import Vector
A = sys.argv[sys.argv.index('--') + 1:]; GLB, OUT = A[:2]; ORDER = A[2:]
bpy.ops.wm.read_factory_settings(use_empty=True); bpy.ops.import_scene.gltf(filepath=GLB)
sc = bpy.context.scene; sc.render.fps = 24
arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE'); ad = arm.animation_data or arm.animation_data_create(); ad.action = None
acts = {a.name: a for a in bpy.data.actions}
order = [n for n in (ORDER or ['idle', 'walk', 'gallop_scan', 'attack_charge', 'attack_right', 'attack_pivot_left', 'rider_death', 'rider_death_left', 'horse_death']) if n in acts]
tr = ad.nla_tracks.new(); f = 1; spans = []
for n in order:
    a = acts[n]; L = a.frame_range[1] - a.frame_range[0]; reps = int(os.environ.get('REPS', 2 if L < 40 else 1))   # short loops twice
    for r in range(reps):
        st = tr.strips.new(n, int(f), a)
        if a.slots: st.action_slot = a.slots[0]
        spans.append((n, int(f), int(f + L))); f += L + 1
sc.frame_start, sc.frame_end = 1, int(f) - 1
# light and ground as the horse checks
w = bpy.data.worlds.new('w'); sc.world = w; w.use_nodes = True; nt = w.node_tree; nt.nodes.clear()
sky = nt.nodes.new('ShaderNodeTexSky'); bg = nt.nodes.new('ShaderNodeBackground'); wo = nt.nodes.new('ShaderNodeOutputWorld')
try: sky.sky_type = 'NISHITA'; sky.sun_elevation = math.radians(35); sky.sun_rotation = math.radians(-30)
except Exception: pass
bg.inputs[1].default_value = 0.3; nt.links.new(sky.outputs[0], bg.inputs[0]); nt.links.new(bg.outputs[0], wo.inputs[0])
sun = bpy.data.objects.new('sun', bpy.data.lights.new('sun', 'SUN')); sc.collection.objects.link(sun); sun.data.energy = 3.5; sun.rotation_euler = (math.radians(50), 0, math.radians(-30))
gp = bpy.data.objects.new('ground', bpy.data.meshes.new('ground')); sc.collection.objects.link(gp); gb = bmesh.new(); bmesh.ops.create_grid(gb, x_segments=1, y_segments=1, size=12); gb.to_mesh(gp.data); gb.free()
gm = bpy.data.materials.new('g'); gm.use_nodes = True; gm.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = (0.18, 0.17, 0.15, 1); gp.data.materials.append(gm)
try: sc.view_settings.view_transform = 'AgX'
except Exception: pass
cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam')); sc.collection.objects.link(cam); sc.camera = cam; cam.data.lens = 40
lo, look = (Vector((1.5, -2.3, 1.15)), Vector((0, -0.62, 1.0))) if os.environ.get('CAM') == 'chest' else (Vector((6.5, -0.4, 1.7)), Vector((0, -0.2, 1.45))) if os.environ.get('CAM') == 'side' else (Vector((5.6, -5.0, 2.5)), Vector((0, 0, 1.2)))   # CAM=chest: a close-up of the breast
if os.environ.get('CAM') == 'chest': cam.data.lens = 35
cam.location = lo; cam.rotation_euler = (look - lo).to_track_quat('-Z', 'Y').to_euler()
# clip names: a text object in front of the camera, shown only during its clip
tm = bpy.data.materials.new('t'); tm.use_nodes = True; tm.node_tree.nodes['Principled BSDF'].inputs['Emission Color'].default_value = (1, 1, 1, 1); tm.node_tree.nodes['Principled BSDF'].inputs['Emission Strength'].default_value = 3.0
for n in order:
    cu = bpy.data.curves.new(n, 'FONT'); cu.body = n.replace('_', ' '); cu.size = 0.06
    t = bpy.data.objects.new('txt_' + n, cu); sc.collection.objects.link(t); t.data.materials.append(tm); t.parent = cam; t.location = (-0.33, 0.22, -1.0)
    t.hide_render = True; t.keyframe_insert('hide_render', frame=0)
    for nn, a_, b_ in spans:
        if nn == n: t.hide_render = False; t.keyframe_insert('hide_render', frame=a_); t.hide_render = True; t.keyframe_insert('hide_render', frame=b_ + 1)
sc.render.engine = 'BLENDER_EEVEE'; sc.render.resolution_x = 960; sc.render.resolution_y = 720
sc.render.image_settings.media_type = 'VIDEO'; sc.render.image_settings.file_format = 'FFMPEG'; sc.render.ffmpeg.format = 'MPEG4'; sc.render.ffmpeg.codec = 'H264'; sc.render.ffmpeg.constant_rate_factor = 'HIGH'
sc.render.filepath = OUT; print('CLIPS', spans, 'frames', sc.frame_end)
bpy.ops.render.render(animation=True); print('wrote', OUT)
