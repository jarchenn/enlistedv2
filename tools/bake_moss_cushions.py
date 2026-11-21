# BF B2: the CC0 apple moss scan -> cushion variants (reference 03 s3.4). Run: blender -b --python this -- <glb> <outdir>
# The scan is triangle soup (1.39 M tris, nearly every edge open): weld, then per variant a voxel remesh (manifold
# cushion with the scan's lumps), decimate to 1500 / 600 / 200, unwrap, and bake the scan's colour (it is unlit:
# EMIT bakes it exactly) into one 1536 x 1024 atlas, 512^2 a variant. Then de-light and scale to the 3.2 moss albedo.
import bpy, bmesh, sys, os, struct
import numpy as np
argv = sys.argv[sys.argv.index('--') + 1:]; SRC, OUT = argv[0], argv[1]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=SRC)
meshes = [o for o in bpy.data.objects if o.type == 'MESH' and o.name != 'Object_4']
for o in bpy.data.objects: o.select_set(False)
for o in meshes: o.select_set(True)
bpy.context.view_layer.objects.active = meshes[0]
bpy.ops.object.parent_clear(type='CLEAR_KEEP_TRANSFORM'); bpy.ops.object.join(); src = bpy.context.view_layer.objects.active
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
for o in [o for o in bpy.data.objects if o != src]: bpy.data.objects.remove(o, do_unlink=True)
bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT'); bpy.ops.mesh.remove_doubles(threshold=1e-5); bpy.ops.object.mode_set(mode='OBJECT')
def tris(o): return sum(len(p.vertices) - 2 for p in o.data.polygons)
def sel(o):
    for x in bpy.data.objects: x.select_set(False)
    o.select_set(True); bpy.context.view_layer.objects.active = o
vs = [src.matrix_world @ v.co for v in src.data.vertices]
mn = [min(v[i] for v in vs) for i in range(3)]; mx = [max(v[i] for v in vs) for i in range(3)]
cx, cy = (mn[0] + mx[0]) / 2, (mn[1] + mx[1]) / 2; sx, sy = mx[0] - mn[0], mx[1] - mn[1]
print('moss: source', tris(src), 'tris, bounds', mn, mx)
regions = [None, ('x', -1), ('x', 1), ('y', -1), ('y', 1), ('c', 0)]
def cut(o, reg):
    if reg is None: return
    bm = bmesh.new(); bm.from_mesh(o.data); kill = []
    for f in bm.faces:
        c = f.calc_center_median()
        if reg[0] == 'x': out = (c.x - cx) * reg[1] < -0.12 * sx
        elif reg[0] == 'y': out = (c.y - cy) * reg[1] < -0.12 * sy
        else: out = ((c.x - cx) / (0.36 * sx)) ** 2 + ((c.y - cy) / (0.36 * sy)) ** 2 > 1
        if out: kill.append(f)
    bmesh.ops.delete(bm, geom=kill, context='FACES'); bm.to_mesh(o.data); bm.free()
scn = bpy.context.scene; scn.render.engine = 'CYCLES'; scn.cycles.samples = 4; scn.cycles.device = 'CPU'
AW, AH, T = 1536, 1024, 512
atlas = bpy.data.images.new('atlas', AW, AH, alpha=False)
out = []
for vi, reg in enumerate(regions):
    part = src.copy(); part.data = src.data.copy(); bpy.context.collection.objects.link(part); cut(part, reg)
    o = part.copy(); o.data = part.data.copy(); bpy.context.collection.objects.link(o); sel(o)
    o.data.materials.clear()
    rm = o.modifiers.new('r', 'REMESH'); rm.mode = 'VOXEL'; rm.voxel_size = 0.0011; rm.adaptivity = 0.0; bpy.ops.object.modifier_apply(modifier='r')
    # the base faces point down into the ground: drop them (sunk 20-35 % in game), so the top-down projection has one layer
    bm = bmesh.new(); bm.from_mesh(o.data); bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.normal.z < -0.35], context='FACES'); bm.to_mesh(o.data); bm.free()
    xs = [v.co.x for v in o.data.vertices]; ys = [v.co.y for v in o.data.vertices]
    u0, v0, span = min(xs), min(ys), max(max(xs) - min(xs), max(ys) - min(ys)) * 1.02
    def planar(ob):   # one seamless top-down projection per variant (all its LODs share it)
        me = ob.data
        if not me.uv_layers: me.uv_layers.new(name='UVMap')
        uvd = me.uv_layers.active.data
        for l in me.loops: c = me.vertices[l.vertex_index].co; uvd[l.index].uv = ((c.x - u0) / span, (c.y - v0) / span)
    lods = []
    for target in [1500, 600, 200]:
        lo = o.copy(); lo.data = o.data.copy(); bpy.context.collection.objects.link(lo); sel(lo)
        d = lo.modifiers.new('d', 'DECIMATE'); d.ratio = min(1.0, target / max(tris(lo), 1)); bpy.ops.object.modifier_apply(modifier='d')
        bm = bmesh.new(); bm.from_mesh(lo.data); bmesh.ops.triangulate(bm, faces=bm.faces[:]); bm.to_mesh(lo.data); bm.free()
        planar(lo); lods.append(lo)
    img = bpy.data.images.new('v%d' % vi, T, T, alpha=False)
    mat = bpy.data.materials.new('bake%d' % vi); mat.use_nodes = True; nt = mat.node_tree
    tn = nt.nodes.new('ShaderNodeTexImage'); tn.image = img; nt.nodes.active = tn; lods[0].data.materials.append(mat)
    sel(part); lods[0].select_set(True); bpy.context.view_layer.objects.active = lods[0]
    bpy.ops.object.bake(type='EMIT', use_selected_to_active=True, cage_extrusion=0.002, max_ray_distance=0.006, margin=8)
    px = np.array(img.pixels[:], dtype=np.float32).reshape(T, T, 4)
    ap = np.array(atlas.pixels[:], dtype=np.float32).reshape(AH, AW, 4)
    ox0, oy0 = (vi % 3) * T, (vi // 3) * T; ap[oy0:oy0 + T, ox0:ox0 + T] = px; atlas.pixels[:] = ap.ravel()
    for li, lo in enumerate(lods):
        me = lo.data; me.calc_loop_triangles(); uv = me.uv_layers.active.data
        zmin = min(v.co.z for v in me.vertices); xs = [v.co.x for v in me.vertices]; ys = [v.co.y for v in me.vertices]
        ox, oy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
        verts, idx, key = [], [], {}
        for t in me.loop_triangles:
            for l2 in t.loops:
                l = me.loops[l2]; v = me.vertices[l.vertex_index]; u = uv[l2].uv
                k = l.vertex_index
                if k not in key:   # engine y-up; base at y = 0, centred in x / z; atlas u / v
                    key[k] = len(verts)
                    verts.append((v.co.x - ox, v.co.z - zmin, -(v.co.y - oy), v.normal.x, v.normal.z, -v.normal.y,
                                  (ox0 + u.x * T) / AW, 1.0 - (oy0 + u.y * T) / AH))
                idx.append(key[k])
        out.append((vi, li, verts, idx)); print('moss: variant', vi, 'lod', li, len(idx) // 3, 'tris', len(verts), 'verts, size', max(xs) - min(xs), max(ys) - min(ys), max(v.co.z for v in me.vertices) - zmin)
        bpy.data.objects.remove(lo, do_unlink=True)
    bpy.data.objects.remove(o, do_unlink=True); bpy.data.objects.remove(part, do_unlink=True)
os.makedirs(OUT, exist_ok=True)
with open(os.path.join(OUT, 'moss_cushions.bin'), 'wb') as f:   # 'MOSC' 1, count; per mesh: variant, lod, nv, ni, nv x 8 floats (pos, nrm, uv), ni x uint32
    f.write(b'MOSC'); f.write(struct.pack('<II', 1, len(out)))
    for vi, li, verts, idx in out:
        f.write(struct.pack('<IIII', vi, li, len(verts), len(idx)))
        f.write(np.array(verts, dtype='<f4').tobytes()); f.write(np.array(idx, dtype='<u4').tobytes())
# de-light: divide out the low-frequency baked light (sigma ~ 0.08 x a variant's width), then scale to the healthy moss albedo (3.2)
px = np.array(atlas.pixels[:], dtype=np.float32).reshape(AH, AW, 4)
lin = np.where(px[..., :3] <= 0.04045, px[..., :3] / 12.92, ((px[..., :3] + 0.055) / 1.055) ** 2.4)
W = np.array([0.2126, 0.7152, 0.0722], dtype=np.float32); lum = lin @ W
small = lum.reshape(AH // 16, 16, AW // 16, 16).mean(axis=(1, 3))
k = np.exp(-0.5 * (np.arange(-8, 9) / 2.6) ** 2); k /= k.sum()
bl = np.apply_along_axis(lambda r: np.convolve(np.pad(r, 8, mode='edge'), k, mode='valid'), 1, small)
bl = np.apply_along_axis(lambda r: np.convolve(np.pad(r, 8, mode='edge'), k, mode='valid'), 0, bl)
I = np.kron(bl, np.ones((16, 16), dtype=np.float32)); mask = lum > 2e-3
I = I / max(I[mask].mean(), 1e-6)
flat = lin / np.maximum(I, 0.25)[..., None]
flat *= np.array([0.055, 0.105, 0.030], dtype=np.float32) / np.maximum(flat[mask].mean(axis=0), 1e-6)   # white balance: the mean lands on the healthy moss albedo, the scan's variation stays
flat = np.clip(flat, 0.02, 0.85)
px[..., :3] = np.where(flat <= 0.0031308, flat * 12.92, 1.055 * flat ** (1 / 2.4) - 0.055)
atlas.pixels[:] = px.ravel(); atlas.filepath_raw = os.path.join(OUT, 'moss_cushion_albedo.png'); atlas.file_format = 'PNG'; atlas.save()
print('moss: albedo mean linear', flat[mask].mean(axis=0), 'coverage', mask.mean())
