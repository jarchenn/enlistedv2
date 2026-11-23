# CAN1: a cast-iron round shot (UV sphere, metres) for the cannon stations' flying ball and the placeable prop
#   blender -b --factory-startup --disable-autoexec --python make_ball.py -- <radius m> <out.glb>
import bpy, sys
A = sys.argv[sys.argv.index('--') + 1:]; R, OUT = float(A[0]), A[1]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.mesh.primitive_uv_sphere_add(segments=20, ring_count=12, radius=R, location=(0, 0, R)); o = bpy.context.object; o.name = 'ball'
bpy.ops.object.shade_smooth()
m = bpy.data.materials.new('cast_iron'); m.use_nodes = True; b = m.node_tree.nodes['Principled BSDF']
b.inputs['Base Color'].default_value = (0.055, 0.052, 0.05, 1); b.inputs['Metallic'].default_value = 0.6; b.inputs['Roughness'].default_value = 0.7
o.data.materials.append(m)
bpy.ops.export_scene.gltf(filepath=OUT, export_format='GLB', use_selection=True, export_yup=True)
