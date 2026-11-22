# KH8 (Blender background): a knight's helmet brought to its body's "Metal" finish, the way knight_parts.py finishes Knights 6
# and 7's helms: the tile (metal_tile.py, cut from this knight's own Metal) box-projected at `tiles` across the helmet's largest
# dimension, multiplied by the helmet's own luminance (x 0.7 + 0.6, so its rivets, breaths and relief survive); roughness and
# metallic from the tile's metal-rough map. Baked into the helmet's own UVs (base colour, metal-rough at `px`), then the GLB's
# helmet material points at the baked maps (factors 1; its normal map, if any, kept); every other byte as it was (run
# gltf_prune.py after it to drop the replaced scan).
#   blender -b --factory-startup --disable-autoexec --python tools/enemies/helm_finish.py -- <knight.glb> <helmet node> <tile dir/> <tiles> <px> <out.glb>
import bpy, sys, os, json, struct, io
import numpy as np
GLB, NODE, TEX, TILES, PX, OUT = sys.argv[sys.argv.index('--') + 1:]; TILES = float(TILES); PX = int(PX)
bpy.ops.wm.read_factory_settings(use_empty=True); bpy.ops.import_scene.gltf(filepath=GLB, disable_bone_shape=True)
o = bpy.data.objects[NODE]; assert len(o.data.materials) == 1, [m.name for m in o.data.materials]
mt = o.data.materials[0]; MATNAME = mt.name; nt = mt.node_tree; b = next(n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED')
co = np.array([v.co for v in o.data.vertices]); dim = float((co.max(0) - co.min(0)).max())
tc = nt.nodes.new('ShaderNodeTexCoord'); mp = nt.nodes.new('ShaderNodeMapping'); s_ = TILES / dim; mp.inputs['Scale'].default_value = (s_, s_, s_); nt.links.new(tc.outputs['Object'], mp.inputs['Vector'])
def tile(fn, cs):
    n = nt.nodes.new('ShaderNodeTexImage'); n.image = bpy.data.images.load(os.path.join(TEX, fn)); n.image.colorspace_settings.name = cs; n.projection = 'BOX'; n.projection_blend = 0.35; nt.links.new(mp.outputs[0], n.inputs['Vector']); return n
tb = tile('tile_base.png', 'sRGB'); tm = tile('tile_mr.png', 'Non-Color')
ib = b.inputs['Base Color']; assert ib.is_linked, 'the helmet has no base colour texture to take its relief from'
bw = nt.nodes.new('ShaderNodeRGBToBW'); nt.links.new(ib.links[0].from_socket, bw.inputs[0])
ma = nt.nodes.new('ShaderNodeMath'); ma.operation = 'MULTIPLY_ADD'; ma.inputs[1].default_value = 0.7; ma.inputs[2].default_value = 0.6; nt.links.new(bw.outputs[0], ma.inputs[0])
mx = nt.nodes.new('ShaderNodeMixRGB'); mx.blend_type = 'MULTIPLY'; mx.inputs[0].default_value = 1; nt.links.new(tb.outputs[0], mx.inputs[1]); nt.links.new(ma.outputs[0], mx.inputs[2])
sp = nt.nodes.new('ShaderNodeSeparateColor'); nt.links.new(tm.outputs[0], sp.inputs[0])
src = {'Base Color': mx.outputs[0], 'Roughness': sp.outputs[1], 'Metallic': sp.outputs[2]}
sc = bpy.context.scene; sc.render.engine = 'CYCLES'; sc.cycles.samples = 1; sc.cycles.device = 'CPU'; sc.render.bake.margin = 8
bpy.ops.object.select_all(action='DESELECT'); o.select_set(True); bpy.context.view_layer.objects.active = o
out = next(n for n in nt.nodes if n.type == 'OUTPUT_MATERIAL'); em = nt.nodes.new('ShaderNodeEmission'); nt.links.new(em.outputs[0], out.inputs['Surface'])
img = nt.nodes.new('ShaderNodeTexImage'); res = {}
for ch, sock in src.items():
    im = bpy.data.images.new('helm_' + ch[:4], PX, PX, alpha=False); im.colorspace_settings.name = 'sRGB' if ch == 'Base Color' else 'Non-Color'
    img.image = im; nt.nodes.active = img
    for l in list(em.inputs[0].links): nt.links.remove(l)
    nt.links.new(sock, em.inputs[0]); bpy.ops.object.bake(type='EMIT', use_clear=True); res[ch] = im
def jpeg(im, path):
    sc.render.image_settings.file_format = 'JPEG'; sc.render.image_settings.quality = 90; sc.view_settings.view_transform = 'Standard'
    im.save_render(path, scene=sc); return open(path, 'rb').read()
pr = np.array(res['Roughness'].pixels[:]).reshape(-1, 4); pm = np.array(res['Metallic'].pixels[:]).reshape(-1, 4)
mr = bpy.data.images.new('helm_mr', PX, PX, alpha=False); mr.colorspace_settings.name = 'Non-Color'; px = np.ones_like(pr); px[:, 1] = pr[:, 0]; px[:, 2] = pm[:, 0]; mr.pixels[:] = px.ravel()
tmp = os.path.splitext(OUT)[0]; jb = jpeg(res['Base Color'], tmp + '_base.jpg'); jm = jpeg(mr, tmp + '_mr.jpg')
print('HELM baked %s: roughness mean %.3f, metallic mean %.3f' % (NODE, pr[:, 0].mean(), pm[:, 0].mean()))
# ---- the GLB: the helmet material onto the baked maps
d = open(GLB, 'rb').read(); jl = struct.unpack('<I', d[12:16])[0]; J = json.loads(d[20:20 + jl]); bl = struct.unpack('<I', d[20 + jl:24 + jl])[0]; BIN = bytearray(d[28 + jl:28 + jl + bl])
def add(data, name):
    while len(BIN) % 4: BIN.append(0)
    J['bufferViews'].append({'buffer': 0, 'byteOffset': len(BIN), 'byteLength': len(data)}); BIN.extend(data)
    J['images'].append({'name': name, 'mimeType': 'image/jpeg', 'bufferView': len(J['bufferViews']) - 1}); return len(J['images']) - 1
m = next(x for x in J['materials'] if x['name'] == MATNAME); pbr = m['pbrMetallicRoughness']
samp = J['textures'][pbr['baseColorTexture']['index']].get('sampler')
def tex(i): t = {'source': i}; t.update({'sampler': samp} if samp is not None else {}); J['textures'].append(t); return len(J['textures']) - 1
pbr['baseColorTexture'] = {'index': tex(add(jb, NODE.lower() + '_finish_base'))}; pbr['metallicRoughnessTexture'] = {'index': tex(add(jm, NODE.lower() + '_finish_mr'))}
pbr['metallicFactor'] = 1.0; pbr['roughnessFactor'] = 1.0
J['buffers'] = [{'byteLength': len(BIN)}]
js = json.dumps(J, separators=(',', ':')).encode(); js += b' ' * (-len(js) % 4)
while len(BIN) % 4: BIN.append(0)
open(OUT, 'wb').write(b'glTF' + struct.pack('<II', 2, 28 + len(js) + len(BIN)) + struct.pack('<I', len(js)) + b'JSON' + js + struct.pack('<I', len(BIN)) + b'BIN\0' + bytes(BIN))
print('wrote', OUT)
