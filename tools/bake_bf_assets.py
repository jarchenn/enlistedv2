# BF rev 3 phase 1 (spec 2.4): the four shippable bundle GLBs -> normalised GLBs for the game's own bake.
#   blender -b --factory-startup --disable-autoexec -noaudio --python tools/bake_bf_assets.py -- <models> <glb out> [ids...]
#   <models>:  the bundle's models/ folder (data only, never run). The two CC-BY-NC GLBs are refused (content/licences.json).
#   <glb out>: bf_<id>.glb; then  NormandyMapmaker --import-weapon-models <glb out> <content>  bakes content/assets/models/bf_<id>/
# Per asset (spec 2.4 steps 1-6; step 7 merge, 8 LOD, 9 compression are the game bake's: model.cpp merges by material,
# simplifies LOD1, packs and downsizes textures to the asset's maxTex):
#   bridge   in metres, clean: passes through.
#   wall     old_stone_wall: emissiveTexture == baseColorTexture with factor 1 (the wall glows): emission cleared;
#            1,000,000-tri scan decimated to ~40k (spec 1.4: 30,000 tris per metre of run where ~300-1,200 do).
#   moss     apple_moss: KHR_materials_unlit -> lit Principled; albedo de-lit (spec 4.5 / ref 03: divide out the baked
#            irradiance = a wide blur of its luminance, then match the mean to moss, healthy 0.055 0.105 0.030 linear);
#            1.39 M tris decimated to ~4k (a 12 cm clump).
#   road     scale s = 0.076 (spec 2.3: Wood_Log minor axis 3.95 u = a 0.30 m log; verified against the 1.8 m capsule by
#            the fence height printed below). Vegetation cards and trunks dropped (spec 2.4 step 5: re-scattered by our
#            foliage); materials whose normal map reads TEXCOORD_1 (Fallen_Maple_Leaves, Rock_Decal; Background_Tree_Atlas
#            is dropped with the cards) get it re-baked into TEXCOORD_0 tangent space (Cycles normal bake), since the game's
#            bake reads UV0 only: the UV1 normal map is carried over, not stripped (spec 2.4 step 2).
import bpy, sys, os, json, math
import numpy as np

argv = sys.argv[sys.argv.index('--') + 1:]
SRC, OUT = argv[0], argv[1]
ONLY = set(argv[2:])
os.makedirs(OUT, exist_ok=True)
REFUSE = {'fantasy_house_with_interior.glb', 'stone_with_moss.glb'}   # CC-BY-NC-4.0: reference only (licences.json)
ASSETS = {
    'bridge': 'bridge.glb',
    'wall_scan': 'old_stone_wall_full_of_moss_-_scan.glb',
    'apple_moss': 'cc0__apple_moss_bartramia_pomiformis.glb',
    'road_slice': 'update_dirt_road_through_forest.glb',
}
ROAD_DROP = {'Background_Tree_Atlas', 'Forest_Bush', 'Grass_Vegetation_Dry', 'Grass_Vegetation_Green', 'Trunk_Oak', 'Trunk_Birch'}
MOSS_ALB = np.array([0.055, 0.105, 0.030])   # spec 3.2 moss, healthy (linear)


def reset(): bpy.ops.wm.read_factory_settings(use_empty=True)
def meshes(): return [o for o in bpy.context.scene.objects if o.type == 'MESH']
def tris(): return sum(sum(len(p.vertices) - 2 for p in o.data.polygons) for o in meshes())


def select(objs):
    bpy.ops.object.select_all(action='DESELECT')
    for o in objs: o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]


def apply_transforms():
    select(meshes()); bpy.ops.object.parent_clear(type='CLEAR_KEEP_TRANSFORM'); bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)


def decimate(target):
    n = tris()
    if n <= target: return
    for o in meshes():
        m = o.modifiers.new('dec', 'DECIMATE'); m.ratio = target / n
        select([o]); bpy.ops.object.modifier_apply(modifier='dec')


def bsdf(mat): return next((n for n in mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED'), None)


def img_np(img):
    a = np.array(img.pixels[:], dtype=np.float32).reshape(img.size[1], img.size[0], img.channels)
    return a


def blur(x, r):   # separable box blur x3, wrap-around
    for _ in range(3):
        for ax in (0, 1):
            c = np.cumsum(np.concatenate([np.take(x, range(-r, 0), axis=ax), x, np.take(x, range(0, r + 1), axis=ax)], axis=ax), axis=ax)
            x = (np.take(c, range(2 * r + 1, c.shape[ax]), axis=ax) - np.take(c, range(0, c.shape[ax] - 2 * r - 1), axis=ax)) / (2 * r + 1)
    return x


def verify(p):   # spec 9.2 IMPORT assertions on the exported file (the game's bake reads TEXCOORD_0 only)
    import struct
    with open(p, 'rb') as f:
        f.read(12); l, _ = struct.unpack('<II', f.read(8)); j = json.loads(f.read(l))
    prims = {}
    for m in j.get('meshes', []):
        for pr in m['primitives']: prims[pr.get('material', -1)] = prims.get(pr.get('material', -1), 0) + 1
    for i, m in enumerate(j.get('materials', [])):
        pbr = m.get('pbrMetallicRoughness', {})
        refs = [pbr.get('baseColorTexture'), pbr.get('metallicRoughnessTexture'), m.get('normalTexture'), m.get('occlusionTexture'), m.get('emissiveTexture')]
        assert all(t is None or t.get('texCoord', 0) == 0 for t in refs), '%s: a map reads a UV set other than UV0' % m.get('name')
        e, bc = m.get('emissiveTexture'), pbr.get('baseColorTexture')
        assert not (e and bc and e['index'] == bc['index']), '%s: emissiveTexture == baseColorTexture' % m.get('name')
        assert 'KHR_materials_unlit' not in m.get('extensions', {}), '%s: still unlit' % m.get('name')
        n = prims.get(i, 0)
        print('BF verify %s: material %-26s %4d primitives%s' % (os.path.basename(p), m.get('name'), n, '  (> 100: instance it)' if n > 100 else ''))


def export(id_):
    p = os.path.join(OUT, 'bf_%s.glb' % id_)
    bpy.ops.export_scene.gltf(filepath=p, export_format='GLB', use_selection=False, export_apply=True, export_yup=True)
    print('BF %s: %d tris, %d objects -> %s' % (id_, tris(), len(meshes()), p))
    verify(p)


def do_wall():
    for m in bpy.data.materials:
        b = m.use_nodes and bsdf(m)
        if not b: continue
        for l in list(b.inputs['Emission Color'].links): m.node_tree.links.remove(l)
        b.inputs['Emission Strength'].default_value = 0.0
        b.inputs['Emission Color'].default_value = (0, 0, 0, 1)
    decimate(40000)


def do_moss():
    for m in bpy.data.materials:
        nt = m.node_tree
        tex = next((n for n in nt.nodes if n.type == 'TEX_IMAGE'), None)
        for n in list(nt.nodes):
            if n.type != 'TEX_IMAGE': nt.nodes.remove(n)
        out = nt.nodes.new('ShaderNodeOutputMaterial'); b = nt.nodes.new('ShaderNodeBsdfPrincipled')
        nt.links.new(b.outputs['BSDF'], out.inputs['Surface'])
        b.inputs['Roughness'].default_value = 0.85; b.inputs['Metallic'].default_value = 0.0
        if tex:
            img = tex.image; img.colorspace_settings.name = 'sRGB'
            a = img_np(img); rgb = a[..., :3]
            lin = np.where(rgb <= 0.04045, rgb / 12.92, ((rgb + 0.055) / 1.055) ** 2.4)
            L = lin @ np.array([0.2126, 0.7152, 0.0722])
            irr = blur(L, max(4, img.size[0] // 32))[..., None]          # the baked light: wide-scale luminance
            d = lin / np.maximum(irr, 1e-3)
            d = d * (MOSS_ALB / np.maximum(d.reshape(-1, 3).mean(0), 1e-4))
            d = np.clip(d, 0.02, 0.85)                                       # spec 3.2 limits
            s = np.where(d <= 0.0031308, d * 12.92, 1.055 * np.power(d, 1 / 2.4) - 0.055)
            a[..., :3] = s; img.pixels[:] = a.ravel(); img.update()
            nt.links.new(tex.outputs['Color'], b.inputs['Base Color'])
        m.pop('KHR_materials_unlit', None)
    decimate(4000)


def do_road():
    for o in list(meshes()):
        names = {s.material.name for s in o.material_slots if s.material}
        if names and names <= ROAD_DROP: bpy.data.objects.remove(o, do_unlink=True)
    apply_transforms()
    for o in meshes(): o.scale = (0.076,) * 3
    apply_transforms()
    fence = [o for o in meshes() if any(s.material and s.material.name == 'Wood_Fence' for s in o.material_slots)]
    if fence:
        zs = [ (o.matrix_world @ v.co).z for o in fence for v in o.data.vertices]
        print('BF road: Wood_Fence height %.2f m (expect ~1.0-1.4 m against the 1.8 m capsule)' % (max(zs) - min(zs)))
    # UV1 normal maps -> UV0 tangent space
    bpy.context.scene.render.engine = 'CYCLES'; bpy.context.scene.cycles.samples = 1
    bpy.context.scene.render.bake.use_selected_to_active = False
    for o in meshes():
        uvs = o.data.uv_layers
        if len(uvs) < 2: continue
        for s in o.material_slots:
            m = s.material
            if not m or not m.use_nodes: continue
            nm = next((n for n in m.node_tree.nodes if n.type == 'NORMAL_MAP' and n.uv_map and n.uv_map != uvs[0].name), None)
            if not nm: continue
            src = next((l.from_node for l in nm.inputs['Color'].links), None)
            w, h = (src.image.size if src and src.image else (1024, 1024))
            tgt = bpy.data.images.new('%s_n_uv0' % m.name, w, h, alpha=False, float_buffer=False); tgt.colorspace_settings.name = 'Non-Color'
            tn = m.node_tree.nodes.new('ShaderNodeTexImage'); tn.image = tgt
            for n in m.node_tree.nodes: n.select = False
            tn.select = True; m.node_tree.nodes.active = tn
            uvs.active_index = 0; select([o])
            bpy.ops.object.bake(type='NORMAL', normal_space='TANGENT', margin=8)
            tgt.pack()
            nm.uv_map = uvs[0].name
            m.node_tree.links.new(tn.outputs['Color'], nm.inputs['Color'])
            print('BF road: %s normal map re-baked from %s into %s (%dx%d)' % (m.name, src.image.name if src and src.image else '?', uvs[0].name, w, h))


if '--verify' in argv:   # check existing exports without Blender work: blender ... -- <models> <glb out> --verify
    for id_ in ASSETS: verify(os.path.join(OUT, 'bf_%s.glb' % id_))
    sys.exit(0)
for id_, f in ASSETS.items():
    if ONLY and id_ not in ONLY: continue
    assert f not in REFUSE
    reset()
    bpy.ops.import_scene.gltf(filepath=os.path.join(SRC, f))
    print('BF %s: imported %d tris' % (id_, tris()))
    {'bridge': lambda: None, 'wall_scan': do_wall, 'apple_moss': do_moss, 'road_slice': do_road}[id_]()
    export(id_)
