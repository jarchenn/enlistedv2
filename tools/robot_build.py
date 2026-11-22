# RB1: an original chrome combat robot (our design: thin machine frame, wide sensor band head, open ladder-frame torso
# round a power core, segmented spine column, shoulder gimbals, twin-rod forearms and shins with actuators, three fingers
# and a thumb, shin guards over human leg proportions, stencilled unit number "KR-714"). Built procedurally from the
# shared soldier skeleton (content/characters/allied_body.glb: only its armature is used), every part rigid on one bone
# (weight 1), so the soldier clips drive it like a machine. No downloaded assets; textures generated here.
#   blender -b --factory-startup --disable-autoexec -noaudio --python tools/robot_build.py -- <allied_body.glb> <out dir> [--render]
#   out: robot_src.glb (skinned on the allied skeleton; then  python -I tools/bake_gunbody.py --skin <allied_body.glb>
#        <out>/robot_src.glb content/characters/robot_kr.glb  with SKIN_KEEP_NORMALS=1), textures, and with --render the
#        Eevee approval sheet: T-pose front / side / back, idle / aim / run / crouch (render_*.png).
import bpy, bmesh, sys, os, math
import numpy as np
from mathutils import Vector, Matrix

MCP = 'ROBOT_ARGS' in globals()   # run inside a live Blender (Blender MCP): exec(code, {'ROBOT_ARGS': [...]})
argv = ROBOT_ARGS if MCP else sys.argv[sys.argv.index('--') + 1:]
RIG, OUT = argv[0], argv[1]
RENDER = '--render' in argv
os.makedirs(OUT, exist_ok=True)
P = 'mixamorig:'

if MCP:   # a fresh scene of its own: the user's open scenes are never touched
    old = bpy.data.scenes.get('KR714')
    if old:
        for o in list(old.objects): bpy.data.objects.remove(o, do_unlink=True)
        bpy.data.scenes.remove(old)
    sc0 = bpy.data.scenes.new('KR714'); bpy.context.window.scene = sc0
else:
    bpy.ops.wm.read_factory_settings(use_empty=True)
before = set(bpy.data.objects)
bpy.ops.import_scene.gltf(filepath=RIG)
arm = next(o for o in bpy.context.scene.objects if o.type == 'ARMATURE')
for o in list(bpy.context.scene.objects):
    if o.type == 'MESH': bpy.data.objects.remove(o, do_unlink=True)
arm.animation_data_clear() if arm.animation_data and False else None
bpy.context.view_layer.update()


def J(name):   # a joint's rest position, world (Blender: up +Z, the body faces -Y, its left is +X)
    b = arm.data.bones[P + name]
    return arm.matrix_world @ b.head_local


# ---------------------------------------------------------------- textures (numpy)
N = 512
rng = np.random.default_rng(714)


def save_img(name, rgb, colour=True):   # written as a PNG here, then loaded: a generated image's save() wrote it blank (black)
    import zlib, struct
    a = (np.clip(rgb, 0, 1) * 255 + 0.5).astype(np.uint8)
    raw = b''.join(b'\x00' + a[y].tobytes() for y in range(N))   # array row 0 = the image top (filter byte 0 per row)
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    path = os.path.join(OUT, name + '.png')
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', N, N, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))
    img = bpy.data.images.load(path); img.colorspace_settings.name = 'sRGB' if colour else 'Non-Color'; img.pack()
    return img

def blur(x, r, axis):
    c = np.cumsum(np.concatenate([np.take(x, range(-r, 0), axis=axis), x, np.take(x, range(0, r + 1), axis=axis)], axis=axis), axis=axis)
    return (np.take(c, range(2 * r + 1, c.shape[axis]), axis=axis) - np.take(c, range(0, c.shape[axis] - 2 * r - 1), axis=axis)) / (2 * r + 1)


def streaks(r_along=40, r_across=0):   # brushed metal: noise stretched along u
    n = rng.random((N, N)).astype(np.float32)
    n = blur(n, r_along, 1)
    if r_across: n = blur(n, r_across, 0)
    n = (n - n.mean()) / (n.std() + 1e-6)
    return n


def blotch(scale, seed):   # low-frequency mask 0..1
    r = np.random.default_rng(seed); n = r.random((N // scale + 2, N // scale + 2)).astype(np.float32)
    n = np.kron(n, np.ones((scale, scale), np.float32))[:N, :N]
    for _ in range(3): n = blur(blur(n, scale // 2, 0), scale // 2, 1)
    return (n - n.min()) / (n.max() - n.min() + 1e-6)


def normal_from_height(h, k):
    gy, gx = np.gradient(h)
    nm = np.stack([-gx * k, gy * k, np.ones_like(h)], -1); nm /= np.linalg.norm(nm, axis=-1, keepdims=True)
    return nm * 0.5 + 0.5


def mr(rough, metal):   # glTF metallic-roughness: G roughness, B metallic
    return np.stack([np.zeros_like(rough), rough, metal], -1)


def lin2srgb(c): return np.where(c <= 0.0031308, c * 12.92, 1.055 * np.power(np.clip(c, 0, None), 1 / 2.4) - 0.055)



GLYPH = {   # a 5 x 7 stencil font, enough for the unit mark (no PIL in Blender's Python)
    'K': ["X...X", "X..X.", "X.X..", "XX...", "X.X..", "X..X.", "X...X"],
    'R': ["XXXX.", "X...X", "X...X", "XXXX.", "X.X..", "X..X.", "X...X"],
    '-': [".....", ".....", ".....", "XXXXX", ".....", ".....", "....."],
    '7': ["XXXXX", "....X", "...X.", "..X..", ".X...", ".X...", ".X..."],
    '1': ["..X..", ".XX..", "..X..", "..X..", "..X..", "..X..", ".XXX."],
    '4': ["...X.", "..XX.", ".X.X.", "X..X.", "XXXXX", "...X.", "...X."],
}


def stencil_mask(text):   # the mark centred, with the stencil's bridges (a gap row through each glyph) and a bar under it
    cell = 15; gw, gh = 5 * cell, 7 * cell; gap = cell
    W = len(text) * (gw + gap) - gap; x0 = (N - W) // 2; y0 = int(N * 0.30)
    m = np.zeros((N, N), bool)
    for i, ch in enumerate(text):
        g = GLYPH.get(ch)
        if not g: continue
        for r, row in enumerate(g):
            for c, px in enumerate(row):
                if px == 'X' and r != 3 or (px == 'X' and ch == '-'):
                    y, x = y0 + r * cell, x0 + i * (gw + gap) + c * cell
                    m[y + 1:y + cell - 1, x + 1:x + cell - 1] = True
    m[int(N * 0.62):int(N * 0.65), int(N * 0.15):int(N * 0.85)] = True
    return m   # array row 0 is the image top (save_img flips): upright on the plate


def make_set(key, base_lin, rough_mean, rough_amp, metal, streak=True, wear=None, stencil=None):
    if streak: s = streaks()
    else:   # unbrushed: soft waviness, not per-pixel grit (that read as hammered metal)
        s = blur(blur(rng.normal(0, 1, (N, N)).astype(np.float32), 6, 0), 6, 1); s = (s - s.mean()) / (s.std() + 1e-6)
    rough = np.clip(rough_mean + rough_amp * s, 0.08, 1.0)
    alb = np.broadcast_to(np.array(base_lin, np.float32), (N, N, 3)).copy() * (1.0 + 0.02 * s[..., None])
    met = np.full((N, N), metal, np.float32)
    h = s * 0.4
    if metal > 0.5:   # fine wear: scratches (rougher, cut in) and a few scuffed patches
        sc = np.zeros((N, N), np.float32); r2 = np.random.default_rng(abs(hash(key)) % 1000)
        for _ in range(420):
            x0, y0 = r2.random(2) * N; ang = r2.random() * math.pi; L = 8 + r2.random() * 70
            t = np.arange(0, L, 0.7); xs = ((x0 + np.cos(ang) * t) % N).astype(int); ys = ((y0 + np.sin(ang) * t) % N).astype(int)
            sc[ys, xs] = np.maximum(sc[ys, xs], 0.4 + 0.6 * r2.random())
        sc = np.maximum(sc, blur(blur(sc, 1, 0), 1, 1) * 0.6)
        scuff = blotch(64, abs(hash(key)) % 97) > 0.82
        rough = np.clip(rough + 0.22 * sc + 0.08 * scuff, 0.05, 1.0); h = h - 0.5 * sc
    if wear is not None:   # worn paint: chips and scrapes down to brushed steel (metal 1 there, a real mask: no partial metallic)
        steel_lin, steel_rough = wear
        m = blotch(16, 7); chips = (m + 0.25 * rng.random((N, N))) > 0.92   # small, sparse chips at the edges of wear patches
        alb[chips] = steel_lin; rough[chips] = steel_rough; met[chips] = 1.0; h = h + chips * -0.6
    if stencil:
        st = stencil_mask(stencil)
        st &= rng.random((N, N)) > 0.06   # sprayed, slightly broken
        alb[st] = np.array([0.72, 0.60, 0.22], np.float32)   # yellow stencil paint
        rough[st] = 0.75; met[st] = 0.0
    al = save_img(key + '_albedo', lin2srgb(alb))
    mi = save_img(key + '_mr', mr(rough, met), colour=False)
    ni = save_img(key + '_normal', normal_from_height(h, 0.5), colour=False)
    return al, mi, ni


def material(name, maps=None, emission=None, emis_strength=0.0, base=None, rough=0.5, metal=0.0):
    m = bpy.data.materials.new(name); m.use_nodes = True
    nt = m.node_tree; b = nt.nodes['Principled BSDF']
    if maps:
        al, mi, ni = maps
        ta = nt.nodes.new('ShaderNodeTexImage'); ta.image = al; nt.links.new(ta.outputs['Color'], b.inputs['Base Color'])
        tm = nt.nodes.new('ShaderNodeTexImage'); tm.image = mi; sep = nt.nodes.new('ShaderNodeSeparateColor')
        nt.links.new(tm.outputs['Color'], sep.inputs['Color']); nt.links.new(sep.outputs['Green'], b.inputs['Roughness']); nt.links.new(sep.outputs['Blue'], b.inputs['Metallic'])
        tn = nt.nodes.new('ShaderNodeTexImage'); tn.image = ni; nm = nt.nodes.new('ShaderNodeNormalMap'); nm.inputs['Strength'].default_value = 0.3
        nt.links.new(tn.outputs['Color'], nm.inputs['Color']); nt.links.new(nm.outputs['Normal'], b.inputs['Normal'])
    else:
        b.inputs['Base Color'].default_value = (*base, 1); b.inputs['Roughness'].default_value = rough; b.inputs['Metallic'].default_value = metal
    if emission:
        b.inputs['Emission Color'].default_value = (*emission, 1); b.inputs['Emission Strength'].default_value = emis_strength
    return m


STEEL = (0.56, 0.57, 0.58)   # polished steel F0 (spec 3.2: iron bare 0.56-0.58); not a 0.95 mirror (a bright sky stays readable)
M = {
    'chrome':   material('kr_chrome',   make_set('kr_chrome', STEEL, 0.24, 0.035, 1.0)),
    'polished': material('kr_polished', make_set('kr_polished', STEEL, 0.32, 0.02, 1.0, streak=False)),   # satin, not a mirror: the skull's planes read outdoors
    'shell':    material('kr_shell',    make_set('kr_shell', (0.40, 0.41, 0.43), 0.5, 0.03, 1.0, streak=False)),   # pale satin gunmetal outer shells
    'graphite': material('kr_graphite', make_set('kr_graphite', (0.16, 0.16, 0.17), 0.5, 0.04, 1.0, streak=False)),   # medium-dark structure (internals stay apart at night)   # dark internals   # the skull and teeth: no brushing (box UVs band on curves)
    'brushed':  material('kr_brushed',  make_set('kr_brushed', (0.50, 0.50, 0.51), 0.38, 0.05, 1.0)),
    'gunmetal': material('kr_gunmetal', make_set('kr_gunmetal', (0.20, 0.21, 0.23), 0.46, 0.05, 1.0)),
    'paint':    material('kr_paint',    make_set('kr_paint', (0.15, 0.16, 0.12), 0.68, 0.06, 0.0, streak=False, wear=(STEEL, 0.32))),
    'unit':     material('kr_unit',     make_set('kr_unit', (0.15, 0.16, 0.12), 0.68, 0.06, 0.0, streak=False, wear=(STEEL, 0.32), stencil='KR-714')),
    'rubber':   material('kr_rubber', base=(0.025, 0.025, 0.027), rough=0.8),
    'core':     material('kr_core', base=(0.05, 0.02, 0.01), rough=0.25, emission=(1.0, 0.18, 0.05), emis_strength=0.5),   # a faint red-orange glow behind the ribs (brighter read as a letter through them)
    'eye':      material('kr_eye', base=(0.12, 0.0, 0.0), rough=0.05, emission=(1.0, 0.02, 0.01), emis_strength=2.5),   # a red lens: glows, blooms a little
}
MAT_ORDER = list(M.keys())

# ---------------------------------------------------------------- geometry
parts = []   # (bmesh-built object) each tagged with bone + material


def finish(bm, bone, mat, uv='box'):
    me = bpy.data.meshes.new('kr_part'); bm.to_mesh(me); bm.free()
    o = bpy.data.objects.new('kr_part', me); bpy.context.scene.collection.objects.link(o)
    o.data.materials.append(M[mat])
    o['bone'] = P + bone
    parts.append(o)
    # box-projected UVs in metres (textures tile every 0.5 m; streaks run along x)
    me.uv_layers.new(name='UVMap'); uvl = me.uv_layers[0].data
    for poly in me.polygons:
        n = poly.normal; ax = max(range(3), key=lambda i: abs(n[i]))
        for li in poly.loop_indices:
            v = me.vertices[me.loops[li].vertex_index].co
            if uv == 'plate':   # the stencil plate: its front face fills the texture
                bb = [min(p.co.x for p in me.vertices), max(p.co.x for p in me.vertices), min(p.co.z for p in me.vertices), max(p.co.z for p in me.vertices)]
                uvl[li].uv = ((v.x - bb[0]) / (bb[1] - bb[0] + 1e-6), (v.z - bb[2]) / (bb[3] - bb[2] + 1e-6))
            else:
                a, b = [(1, 2), (0, 2), (0, 1)][ax]
                uvl[li].uv = (v[a] * 3.0, v[b] * 3.0)   # a texture repeat per 0.33 m (512 px: ~1500 px/m, fine brushing)
    return o


def cyl(a, b, r, bone, mat, seg=12, r2=None):   # a capped tube from point a to point b
    a, b = Vector(a), Vector(b); d = b - a; L = d.length
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=seg, radius1=r, radius2=r if r2 is None else r2, depth=L)
    q = Vector((0, 0, 1)).rotation_difference(d.normalized())
    bmesh.ops.transform(bm, matrix=Matrix.Translation((a + b) / 2) @ q.to_matrix().to_4x4(), verts=bm.verts)
    return finish(bm, bone, mat)


def box(c, size, bone, mat, rot=None, bevel=0.006, uv='box'):
    bm = bmesh.new(); bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.scale(bm, vec=Vector(size), verts=bm.verts)
    if bevel > 0: bmesh.ops.bevel(bm, geom=bm.edges[:], offset=bevel, segments=1, affect='EDGES')
    m = Matrix.Translation(Vector(c)) @ (rot.to_4x4() if rot else Matrix.Identity(4))
    bmesh.ops.transform(bm, matrix=m, verts=bm.verts)
    return finish(bm, bone, mat, uv)


def ring(c, axis, R, r, bone, mat, seg=16, sseg=6):   # a torus (shoulder gimbals)
    bm = bmesh.new()
    verts = []
    for i in range(seg):
        a = 2 * math.pi * i / seg; ring_v = []
        for k in range(sseg):
            t = 2 * math.pi * k / sseg
            p = Vector(((R + r * math.cos(t)) * math.cos(a), (R + r * math.cos(t)) * math.sin(a), r * math.sin(t)))
            ring_v.append(bm.verts.new(p))
        verts.append(ring_v)
    for i in range(seg):
        for k in range(sseg):
            bm.faces.new((verts[i][k], verts[(i + 1) % seg][k], verts[(i + 1) % seg][(k + 1) % sseg], verts[i][(k + 1) % sseg]))
    q = Vector((0, 0, 1)).rotation_difference(Vector(axis).normalized())
    bmesh.ops.transform(bm, matrix=Matrix.Translation(Vector(c)) @ q.to_matrix().to_4x4(), verts=bm.verts)
    return finish(bm, bone, mat)


def actuator(a, b, side_off, bone_a, bone_b, mat_a='gunmetal', mat_b='chrome', r=0.012):   # a piston along one segment: barrel and rod
    # both on the segment's own bone (bone_b is kept for the call sites' readability): split over two bones, a bent joint
    # pulled the halves apart and the rods stuck out of the limb
    a, b = Vector(a) + Vector(side_off), Vector(b) + Vector(side_off)
    a, b = a.lerp(b, 0.08), a.lerp(b, 0.92)
    m = a.lerp(b, 0.55)
    cyl(a, m, r * 1.5, bone_a, mat_a, seg=10)
    cyl(m, b, r, bone_a, mat_b, seg=8)


def smooth_part(o):
    for pl in o.data.polygons: pl.use_smooth = True
    return o


def shaft(a, b, r, bone, mat, flare=1.6, seg=14, rings=12, cap=True):   # a lathe-turned long bone: a waisted shaft, flared at both ends
    a, b = Vector(a), Vector(b); d = b - a; L = d.length; q = Vector((0, 0, 1)).rotation_difference(d.normalized()).to_matrix()
    bm = bmesh.new(); prev = None; rows = []
    for i in range(rings + 1):
        t = i / rings
        rr = r * (1.0 + (flare - 1.0) * (math.exp(-(t / 0.13) ** 2) + math.exp(-((1 - t) / 0.13) ** 2))) * (0.92 + 0.08 * math.sin(math.pi * t))
        row = [bm.verts.new(a + q @ Vector((rr * math.cos(2 * math.pi * k / seg), rr * math.sin(2 * math.pi * k / seg), L * t))) for k in range(seg)]
        if prev:
            for k in range(seg): bm.faces.new((prev[k], prev[(k + 1) % seg], row[(k + 1) % seg], row[k]))
        prev = row; rows.append(row)
    if cap:
        bm.faces.new(list(reversed(rows[0]))); bm.faces.new(rows[-1])
    return smooth_part(finish(bm, bone, mat))


def sweep(pts, r, bone, mat, seg=8, r_end=None):   # a tube along a polyline (ribs, clavicles, cables), parallel-transported frame
    pts = [Vector(p) for p in pts]; bm = bmesh.new(); prev = None; n = len(pts)
    tprev = (pts[1] - pts[0]).normalized(); nrm = tprev.orthogonal().normalized()
    first = last = None
    for i, p in enumerate(pts):
        t = (pts[min(i + 1, n - 1)] - pts[max(i - 1, 0)]).normalized()
        ax = tprev.cross(t)
        if ax.length > 1e-6: nrm = Matrix.Rotation(tprev.angle(t), 3, ax.normalized()) @ nrm
        tprev = t; bn = t.cross(nrm)
        rr = r if r_end is None else r + (r_end - r) * i / (n - 1)
        row = [bm.verts.new(p + (nrm * math.cos(2 * math.pi * k / seg) + bn * math.sin(2 * math.pi * k / seg)) * rr) for k in range(seg)]
        if prev:
            for k in range(seg): bm.faces.new((prev[k], prev[(k + 1) % seg], row[(k + 1) % seg], row[k]))
        else: first = row
        prev = row
    bm.faces.new(list(reversed(first))); bm.faces.new(prev)
    return smooth_part(finish(bm, bone, mat))


def ball(c, r, bone, mat, seg=12, ring=8, scale=(1, 1, 1)):
    bm = bmesh.new(); bmesh.ops.create_uvsphere(bm, u_segments=seg, v_segments=ring, radius=r)
    bmesh.ops.scale(bm, vec=Vector(scale), verts=bm.verts); bmesh.ops.translate(bm, vec=Vector(c), verts=bm.verts)
    return smooth_part(finish(bm, bone, mat))


def blob(elems, bone, mat, res=0.005, decimate=0.35):   # metaball ellipsoids -> one smooth anatomical mass (pelvis, scapula, tarsals)
    mb = bpy.data.metaballs.new('blob'); mb.resolution = res; mb.render_resolution = res; mb.threshold = 0.6
    o = bpy.data.objects.new('blob', mb); bpy.context.scene.collection.objects.link(o)
    for c, r, rot in elems:
        e = mb.elements.new(type='ELLIPSOID'); e.co = Vector(c); e.radius = 1.74; e.size_x, e.size_y, e.size_z = r; e.stiffness = 2.0
        if rot: e.rotation = rot
    bpy.context.view_layer.update()
    bpy.ops.object.select_all(action='DESELECT'); o.select_set(True); bpy.context.view_layer.objects.active = o
    bpy.ops.object.convert(target='MESH'); m = bpy.context.view_layer.objects.active
    md_ = m.modifiers.new('d', 'DECIMATE'); md_.ratio = decimate; bpy.ops.object.modifier_apply(modifier=md_.name)
    bm = bmesh.new(); bm.from_mesh(m.data); bm.transform(m.matrix_world); bpy.data.objects.remove(m, do_unlink=True)
    return smooth_part(finish(bm, bone, mat))


def _shell_faces(bm, outer, inner, closed_u=False):   # outer / inner: rows (along) x columns (across) of verts; walls + rims
    nr, nc = len(outer), len(outer[0])
    for i in range(nr - 1):
        for k in range(nc - 1):
            bm.faces.new((outer[i][k], outer[i + 1][k], outer[i + 1][k + 1], outer[i][k + 1]))
            bm.faces.new((inner[i][k + 1], inner[i + 1][k + 1], inner[i + 1][k], inner[i][k]))
    for i in range(nr - 1):   # the two long edges of the open shell
        bm.faces.new((outer[i][0], inner[i][0], inner[i + 1][0], outer[i + 1][0]))
        bm.faces.new((outer[i + 1][nc - 1], inner[i + 1][nc - 1], inner[i][nc - 1], outer[i][nc - 1]))
    for k in range(nc - 1):   # the two end rims
        bm.faces.new((outer[0][k + 1], inner[0][k + 1], inner[0][k], outer[0][k]))
        bm.faces.new((outer[nr - 1][k], inner[nr - 1][k], inner[nr - 1][k + 1], outer[nr - 1][k + 1]))


def shell_tube(a, b, r0, r1, arc, face, thick, bone, mat, seg=14, rings=8, bulge=0.12):   # an open, tapered, thick armour shell round a limb
    a, b = Vector(a), Vector(b); ax = (b - a).normalized(); f = (Vector(face) - ax * Vector(face).dot(ax)).normalized(); g = ax.cross(f)
    bm = bmesh.new(); outer, inner = [], []
    for i in range(rings + 1):
        t = i / rings; r = r0 + (r1 - r0) * t; r *= 1.0 + bulge * math.sin(math.pi * t)   # a curved, swelling profile
        c = a.lerp(b, t); ro, ri = [], []
        for k in range(seg + 1):
            ph = math.radians(-arc / 2 + arc * k / seg); d = f * math.cos(ph) + g * math.sin(ph)
            ro.append(bm.verts.new(c + d * r)); ri.append(bm.verts.new(c + d * (r - thick)))
        outer.append(ro); inner.append(ri)
    _shell_faces(bm, outer, inner)
    return smooth_part(finish(bm, bone, mat))


def dome(c, radii, az0, az1, el0, el1, thick, bone, mat, saz=20, sel=8, pw=1.0):   # a patch of an ellipsoid shell (azimuth 0 = the front, -Y); pw > 1 pulls it toward a diamond (octahedron)
    c = Vector(c); bm = bmesh.new(); outer, inner = [], []
    for i in range(sel + 1):
        el = math.radians(el0 + (el1 - el0) * i / sel); ro, ri = [], []
        for k in range(saz + 1):
            az = math.radians(az0 + (az1 - az0) * k / saz)
            d = Vector((math.sin(az) * math.cos(el), -math.cos(az) * math.cos(el), math.sin(el)))
            d = Vector([math.copysign(abs(v) ** pw, v) for v in d])
            ro.append(bm.verts.new(c + Vector((d.x * radii[0], d.y * radii[1], d.z * radii[2]))))
            ri.append(bm.verts.new(c + Vector((d.x * (radii[0] - thick), d.y * (radii[1] - thick), d.z * (radii[2] - thick)))))
        outer.append(ro); inner.append(ri)
    _shell_faces(bm, outer, inner)
    return smooth_part(finish(bm, bone, mat))


def band(pts, w, t, bone, mat, seg=12, ex=4.0):   # a sweep with a rounded-rectangle section: w across (up), t through (normal)
    pts = [Vector(p) for p in pts]; n = len(pts); bm = bmesh.new(); rows = []
    for i, p in enumerate(pts):
        T = (pts[min(i + 1, n - 1)] - pts[max(i - 1, 0)]).normalized(); B = (Vector((0, 0, 1)) - T * T.z).normalized(); N = T.cross(B)
        row = []
        for k in range(seg):
            a = 2 * math.pi * k / seg; ca, sa = math.cos(a), math.sin(a)
            u = math.copysign(abs(ca) ** (2 / ex), ca) * t; v = math.copysign(abs(sa) ** (2 / ex), sa) * w   # superellipse: a rounded rectangle
            row.append(bm.verts.new(p + N * u + B * v))
        rows.append(row)
    for i in range(n - 1):
        for k in range(seg): bm.faces.new((rows[i][k], rows[i][(k + 1) % seg], rows[i + 1][(k + 1) % seg], rows[i + 1][k]))
    bm.faces.new(list(reversed(rows[0]))); bm.faces.new(rows[-1])
    return smooth_part(finish(bm, bone, mat))


def pbox(c, size, bone, mat, rot=None, bevel=0.012):   # a chunky armour plate: two-segment bevel
    bm = bmesh.new(); bmesh.ops.create_cube(bm, size=1.0); bmesh.ops.scale(bm, vec=Vector(size), verts=bm.verts)
    bmesh.ops.bevel(bm, geom=bm.edges[:], offset=bevel, segments=2, affect='EDGES')
    bmesh.ops.transform(bm, matrix=Matrix.Translation(Vector(c)) @ (rot.to_4x4() if rot else Matrix.Identity(4)), verts=bm.verts)
    return finish(bm, bone, mat)


def along(a, b, side):   # a rotation whose z runs a->b and whose x points to `side` (a world direction across the limb)
    z = (b - a).normalized(); x = (side - z * side.dot(z)).normalized(); y = z.cross(x)
    return Matrix((x, y, z)).transposed()


def disc(c, axis, r, w, bone, mat='gunmetal'):   # a hinge disc with a hub
    ax = Vector(axis).normalized()
    cyl(c - ax * w, c + ax * w, r, bone, mat, seg=20)
    cyl(c - ax * (w + 0.006), c + ax * (w + 0.006), r * 0.45, bone, 'polished', seg=14)


def psweep(pts, wfun, tfun, bone, mat, twist=lambda t: 0.0, out=None, up=(0, 0, 1), seg=14, ex=3.0, convex=0.35):
    """A sculpted member: a section swept along a curved centre line whose half-width w(t) (along `up`), half-thickness
    th(t) (along the normal) and twist (degrees) change along its length; the outward face (+normal, turned towards out(p)
    when given) bulges convex. A constant tube was what made the v4 ribs and limbs read as bent pipes."""
    pts = [Vector(p) for p in pts]; n = len(pts); bm = bmesh.new(); rows = []; upv = Vector(up)
    for i, p in enumerate(pts):
        t = i / (n - 1)
        T = (pts[min(i + 1, n - 1)] - pts[max(i - 1, 0)]).normalized(); B = (upv - T * upv.dot(T)); B = B.normalized() if B.length > 1e-6 else T.orthogonal().normalized(); N = T.cross(B)
        if out is not None and N.dot(Vector(out(p))) < 0: N = -N
        tw = math.radians(twist(t)); N, B = N * math.cos(tw) + B * math.sin(tw), B * math.cos(tw) - N * math.sin(tw)
        w, th = wfun(t), tfun(t); row = []
        for k in range(seg):
            a = 2 * math.pi * k / seg; ca, sa = math.cos(a), math.sin(a)
            u = math.copysign(abs(ca) ** (2 / ex), ca) * th; v = math.copysign(abs(sa) ** (2 / ex), sa) * w
            if u > 0: u *= 1.0 + convex * (1 - abs(sa) ** 2)   # the convex outer face
            row.append(bm.verts.new(p + N * u + B * v))
        rows.append(row)
    for i in range(n - 1):
        for k in range(seg): bm.faces.new((rows[i][k], rows[i][(k + 1) % seg], rows[i + 1][(k + 1) % seg], rows[i + 1][k]))
    bm.faces.new(list(reversed(rows[0]))); bm.faces.new(rows[-1])
    return smooth_part(finish(bm, bone, mat))


def bezier(p0, p1, p2, p3, n=16):   # a cubic curve's points (centre lines)
    p0, p1, p2, p3 = (Vector(x) for x in (p0, p1, p2, p3))
    return [p0 * (1 - t) ** 3 + p1 * 3 * (1 - t) ** 2 * t + p2 * 3 * (1 - t) * t * t + p3 * t ** 3 for t in [k / (n - 1) for k in range(n)]]


FWD, UP, LEFT = Vector((0, -1, 0)), Vector((0, 0, 1)), Vector((1, 0, 0))

# ---- the axial skeleton: vertebrae, ribcage round an inner core, sternum, clavicles, shoulder blades, pelvis
hips, spine, sp1, sp2, neck, head, top = (J(n) for n in ('Hips', 'Spine', 'Spine1', 'Spine2', 'Neck', 'Head', 'HeadTop_End'))
chain = [hips, spine, sp1, sp2, neck]; cbones = ['Hips', 'Spine', 'Spine1', 'Spine2', 'Neck']


def spine_at(z):   # the column's centre line at height z (behind the rig's joints), and the bone that carries it
    for k in range(len(chain) - 1):
        a, b = chain[k], chain[k + 1]
        if z <= b.z or k == len(chain) - 2:
            t = max(0.0, min(1.0, (z - a.z) / max(b.z - a.z, 1e-4)))
            p = a.lerp(b, t); u = max(0.0, min(1.0, (z - sp2.z) / (neck.z - sp2.z)))   # above the shoulders the column curves forward into the neck
            return Vector((p.x, p.y + 0.055 - 0.045 * u, z)), cbones[k]


def bone_at(z):
    return 'Spine2' if z > sp2.z else 'Spine1' if z > sp1.z else 'Spine' if z > spine.z else 'Hips'


nv = 17   # lumbar + thoracic vertebrae from the sacrum to the neck
for k in range(nv):
    z = hips.z + 0.06 + (neck.z - 0.02 - hips.z - 0.06) * k / (nv - 1)
    c, bn = spine_at(z); size = 1.0 - 0.35 * k / (nv - 1)   # lumbar big, upper thoracic small
    cyl(c + Vector((0, 0, -0.008 * size)), c + Vector((0, 0, 0.008 * size)), 0.026 * size, bn, 'polished', seg=14)   # vertebral body
    cyl(c + Vector((0, 0, -0.011 * size)), c + Vector((0, 0, -0.008 * size)), 0.021 * size, bn, 'gunmetal', seg=12)   # disc
    sweep([c + Vector((0, 0.02, 0)), c + Vector((0, 0.045 * size, -0.012)), c + Vector((0, 0.062 * size, -0.026 * size))], 0.006 * size, bn, 'chrome', seg=6, r_end=0.003)   # spinous process
    for side in (-1, 1): sweep([c + Vector((0.012 * side, 0.012, 0.002)), c + Vector((0.042 * side * size, 0.022, 0.0))], 0.005 * size, bn, 'chrome', seg=6, r_end=0.003)   # transverse processes
sweep([spine_at(hips.z + 0.06)[0] + Vector((0, 0, -0.02)), spine_at(neck.z - 0.02)[0] + Vector((0, 0, 0.01))], 0.007, 'Spine1', 'gunmetal', seg=8)   # the cable core through the column
# the rib cage, v5 (user brief): each rib a machined rib bone swept along its own curve - a narrow root at the spine, a broad
# thin blade along the flank, a taper into a recess on the sternum - with a convex outer face and a twist, so its highlight
# spreads on the flank and narrows as it turns. The top pair short under the collar, the middle the widest, the lower drawn in.
front_y = sp1.y - 0.118; back_y = spine_at(sp1.z)[0].y - 0.005; yc = (front_y + back_y) / 2; bdep = (back_y - front_y) / 2
ztop_r, zbot_r = sp2.z + 0.07, sp1.z - 0.095
RIBS = [(ztop_r + (zbot_r - ztop_r) * k / 7, 0.188 - 0.07 * (k / 7) ** 1.15, 0.062 + 0.008 * k) for k in range(8)]   # z, flank half width, tip x: eight pairs, broad at the top, a V-taper down (user)
tips = []
for k, (z, a, tipx) in enumerate(RIBS):
    for side in (-1, 1):
        p0 = Vector((side * 0.026, back_y - 0.004, z + 0.012)); p1 = Vector((side * a * 0.95, back_y - 0.012, z - 0.004))
        p2 = Vector((side * a * 1.04, front_y + 0.03, z - 0.03)); p3 = Vector((side * tipx, front_y + 0.006, z - 0.045 - 0.008 * k))
        pts = bezier(p0, p1, p2, p3, 14)
        cz = Vector((0, yc, z))
        psweep(pts, lambda t: 0.005 + 0.011 * math.sin(math.pi * min(1.0, t * 1.05)) ** 0.8 + 0.002 * t, lambda t: 0.0068 - 0.0022 * math.sin(math.pi * t),
               bone_at(z), 'shell', twist=lambda t: 14 * t, out=lambda q, cz=cz: Vector((q.x - cz.x, q.y - cz.y, 0)), seg=8, ex=3.2, convex=0.3)
        if k < 3:   # armour plates over the top ribs (user), standing off the blade
            arm_pts = [q + Vector((q.x - cz.x, q.y - cz.y, 0)).normalized() * 0.014 for q in pts[3:12]]
            psweep(arm_pts, lambda t: 0.034 - 0.005 * k - 0.014 * abs(t - 0.45), lambda t: 0.008, bone_at(z), 'shell',
                   out=lambda q, cz=cz: Vector((q.x - cz.x, q.y - cz.y, 0)), seg=6, ex=3.0, convex=0.4)
        tips.append((p3, bone_at(z)))
        box(p0 + Vector((0, 0.008, 0)), (0.022, 0.018, 0.022), bone_at(z), 'graphite', bevel=0.004)   # its anchor on the spine
liner_c = Vector((0, yc + 0.02, sp1.z + 0.01))
dome(liner_c, (0.13, 0.072, 0.15), 110, 250, -55, 55, 0.006, 'Spine1', 'graphite', saz=12, sel=8)   # the dark back wall seen through the gaps
for side in (-1, 1):   # diagonal cables inside
    sweep([Vector((0.08 * side, yc + 0.03, sp2.z + 0.02)), Vector((0.02 * side, yc + 0.04, sp1.z)), Vector((-0.04 * side, yc + 0.035, spine.z))], 0.006, 'Spine1', 'graphite' if side > 0 else 'rubber', seg=6)
# the sternum: a broad upper bridge, a faceted lower plate with an inset panel, recesses the rib tips seat into
ztop_st = RIBS[0][0] + 0.005; zbot_st = RIBS[5][0] - 0.03   # a shorter breastplate (it drew the eye from the ribs)
pbox(Vector((0, front_y - 0.004, ztop_st)), (0.2, 0.034, 0.045), 'Spine2', 'chrome', bevel=0.014)                                  # upper bridge
zmid = (ztop_st + zbot_st) / 2
def vplate(top_c, bot_c, halfw, thick, bone, mat, bevel=0.008):   # a bevelled plate whose outline follows halfw: (half width, t down) pairs, top to tip
    top_c, bot_c = Vector(top_c), Vector(bot_c); bm = bmesh.new()
    prof = [(w, t) for w, t in halfw] + [(-w, t) for w, t in reversed(halfw)]
    f = [bm.verts.new(Vector((x, top_c.y - thick / 2, top_c.z + (bot_c.z - top_c.z) * t))) for x, t in prof]
    b = [bm.verts.new(Vector((x, top_c.y + thick / 2, top_c.z + (bot_c.z - top_c.z) * t))) for x, t in prof]
    bm.faces.new(f); bm.faces.new(list(reversed(b)))
    for k in range(len(prof)): bm.faces.new((f[(k + 1) % len(prof)], f[k], b[k], b[(k + 1) % len(prof)]))
    bmesh.ops.bevel(bm, geom=bm.edges[:], offset=bevel, segments=2, affect='EDGES')
    return finish(bm, bone, mat)


vplate(Vector((0, front_y - 0.006, ztop_st - 0.01)), Vector((0, front_y - 0.006, zbot_st - 0.02)),
       [(0.072, 0.0), (0.068, 0.1), (0.056, 0.3), (0.04, 0.55), (0.03, 0.78), (0.024, 0.93), (0.012, 1.0)], 0.03, 'Spine1', 'chrome')   # the breastplate: full shoulders, sides curving in, a blunt chamfered tip (user: not a perfect triangle)
box(Vector((0, front_y - 0.022, zmid + 0.01)), (0.04, 0.006, 0.07), 'Spine1', 'graphite', bevel=0.003)                            # inset access panel
box(Vector((0, front_y - 0.023, zmid + 0.06)), (0.046, 0.004, 0.02), 'Spine1', 'unit', bevel=0.002, uv='plate')                  # the unit tag: KR-714
for q, bn in tips: box(q + Vector((-math.copysign(0.004, q.x), -0.004, 0)), (0.022, 0.03, 0.02), bn, 'graphite', bevel=0.004)     # the recesses the tips seat into
dome(Vector((0, sp2.y + 0.01, sp2.z + 0.035)), (0.18, 0.13, 0.11), -100, 100, 38, 70, 0.012, 'Spine2', 'shell', saz=22, sel=6)   # a yoke over the top of the chest
cpts = [Vector((neck.x + 0.062 * math.cos(a), neck.y - 0.052 * math.sin(a) + 0.012, sp2.z + 0.112 + 0.022 * math.sin(a))) for a in [math.radians(180 - 180 * k / 12) for k in range(13)]]
band(cpts, 0.014, 0.01, 'Spine2', 'chrome', seg=10)   # the collar, raised round the front of the neck
st_top = Vector((0, front_y + 0.03, sp2.z + 0.1))   # the clavicles' inner ends: under the collar
# clavicles (an S from the sternum top to the shoulder) and shoulder blades
for S, side in (('Left', 1), ('Right', -1)):
    sh, ua = J(S + 'Shoulder'), J(S + 'Arm')
    p0 = st_top + Vector((0.02 * side, 0, 0.01)); p3 = ua + Vector((-0.015 * side, -0.01, 0.025))
    sweep([p0, p0.lerp(p3, 0.33) + Vector((0, -0.02, 0.012)), p0.lerp(p3, 0.66) + Vector((0, 0.012, 0.008)), p3], 0.0085, S + 'Shoulder', 'chrome', seg=8)
    blob([((ua.x - 0.045 * side, ua.y + 0.085, ua.z - 0.06), (0.055, 0.012, 0.075), None),
          ((ua.x - 0.02 * side, ua.y + 0.07, ua.z - 0.005), (0.03, 0.025, 0.022), None)], S + 'Shoulder', 'brushed', decimate=0.25)   # scapula + its spine
# pelvis: ilium wings, sacrum, pubic arch, hip sockets (one smooth casting)
lth, rth = J('LeftUpLeg'), J('RightUpLeg')
blob([((0.085, hips.y + 0.015, hips.z + 0.045), (0.055, 0.03, 0.06), None), ((-0.085, hips.y + 0.015, hips.z + 0.045), (0.055, 0.03, 0.06), None),   # ilium wings
      ((0, hips.y + 0.055, hips.z + 0.0), (0.04, 0.022, 0.06), None),                                                                              # sacrum
      ((0, hips.y - 0.045, hips.z - 0.055), (0.05, 0.018, 0.022), None),                                                                           # pubic arch
      ((lth.x, lth.y, lth.z + 0.01), (0.035, 0.035, 0.035), None), ((rth.x, rth.y, rth.z + 0.01), (0.035, 0.035, 0.035), None)],                  # hip sockets
     'Hips', 'brushed', decimate=0.3)

# ---- neck and head: a metal human skull (user: a human skull with chrome teeth and red eyes in real eye sockets). Sculpted
# from skull anatomy: cranium, brow ridge, cheekbones, maxilla fused by a voxel remesh, eye sockets and the nasal opening cut
# by booleans, two rows of chrome teeth, a separate mandible on hinge pins (static on the head bone: the rig has no jaw), a red
# emissive lens deep in each socket. Our own plate lines: a sagittal seam and temple bolts.
hc = head.lerp(top, 0.42) - Vector((0, -0.035, 0.045))   # (user: the neck nearer the body's middle)   # the skull's centre, seated low: the jaw just above the collar (user brief)
skull_base = hc + Vector((0, 0.0, -0.07))
neck_y = (spine_at(neck.z - 0.02)[0].y + skull_base.y) / 2 + 0.005   # one vertical line from the top vertebra into the skull base
for k in range(3):   # a short cervical column into the skull base
    c = Vector((0, neck_y, neck.z - 0.01 + (skull_base.z - neck.z + 0.01) * (k + 0.5) / 3))   # vertical, on the column's line
    cyl(c - Vector((0, 0, 0.008)), c + Vector((0, 0, 0.008)), 0.018, 'Neck', 'polished', seg=12)
    cyl(c + Vector((0, 0, 0.008)), c + Vector((0, 0, 0.011)), 0.014, 'Neck', 'gunmetal', seg=10)
    sweep([c + Vector((0, 0.014, 0)), c + Vector((0, 0.03, -0.01))], 0.004, 'Neck', 'chrome', seg=6, r_end=0.0025)
sweep([Vector((0, neck_y, neck.z - 0.03)), Vector((0, neck_y, skull_base.z + 0.01))], 0.006, 'Neck', 'gunmetal', seg=8)
for side in (-1, 1): actuator(Vector((0.034 * side, neck_y - 0.005, neck.z - 0.01)), Vector((0.034 * side, neck_y - 0.012, skull_base.z + 0.01)), (0, 0, 0), 'Neck', 'Neck', r=0.007)   # paired side linkages
for side in (-1, 1): cyl(Vector((0.045 * side, back_y + 0.025, sp2.z + 0.09)), skull_base + Vector((0.03 * side, 0.02, 0.0)), 0.007, 'Neck', 'graphite', seg=8)   # angled supports, upper back to the skull base


def K(x, y, z):   # skull-local (x across, y forward = the face, z up) -> world (the body faces -Y)
    return hc + Vector((x, -y, z))


def tmp_obj(bm, name='tmp'):
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    o = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(o); return o


def ellipsoid(c, r, seg=24, ring=16):
    bm = bmesh.new(); bmesh.ops.create_uvsphere(bm, u_segments=seg, v_segments=ring, radius=1.0)
    bmesh.ops.scale(bm, vec=Vector(r), verts=bm.verts); bmesh.ops.translate(bm, vec=Vector(c), verts=bm.verts)
    return tmp_obj(bm)


def apply_mod(o, kind, **kw):
    bpy.context.view_layer.objects.active = o
    m = o.modifiers.new('m', kind)
    for k, v in kw.items(): setattr(m, k, v)
    bpy.ops.object.modifier_apply(modifier=m.name)


def adopt(o, bone, mat, smooth=True):   # a finished temp object becomes a part (material, bone, box UVs)
    bm = bmesh.new(); bm.from_mesh(o.data); bm.transform(o.matrix_world); bpy.data.objects.remove(o, do_unlink=True)
    part = finish(bm, bone, mat)
    if smooth:
        for pl in part.data.polygons: pl.use_smooth = True
    return part


# cranium + face masses (skull-local, metres; a human skull ~0.14 wide, 0.19 long, 0.21 tall with the jaw)
SKULL = [((0, -0.03, 0.035), (0.064, 0.088, 0.07), 2.0),          # cranium vault
         ((0, 0.035, 0.03), (0.052, 0.04, 0.05), 2.0),           # the forehead, sloping down into the brow
         ((0, 0.052, -0.04), (0.044, 0.042, 0.032), 2.0),        # maxilla: the upper jaw, forward and low
         ((0, 0.058, -0.008), (0.045, 0.028, 0.028), 2.0),       # the face between the orbits and the maxilla
         ((0.05, 0.045, -0.018), (0.017, 0.032, 0.013), 2.0),    # cheekbone (zygomatic) L
         ((-0.05, 0.045, -0.018), (0.017, 0.032, 0.013), 2.0),   # cheekbone R
         ((0, 0.072, 0.012), (0.057, 0.018, 0.012), 2.0),         # brow ridge, down over the lenses
         ((0, -0.07, -0.028), (0.048, 0.03, 0.034), 2.0)]        # occiput / skull base
mb = bpy.data.metaballs.new('skull'); mb.resolution = 0.004; mb.render_resolution = 0.004; mb.threshold = 0.6
mbo = bpy.data.objects.new('skull_mb', mb); bpy.context.scene.collection.objects.link(mbo)
for c, r, st in SKULL:   # metaball ellipsoids blend into one smooth surface (a union of meshes left creases: a cap-brim ledge)
    e = mb.elements.new(type='ELLIPSOID'); e.co = K(*c); e.radius = 1.74; e.size_x, e.size_y, e.size_z = r[0], r[1], r[2]; e.stiffness = st   # radius 1.74: the threshold-0.6 surface of a stiffness-2 element sits at ~0.575 r, so the sizes above are the surface radii
bpy.context.view_layer.update()
bpy.ops.object.select_all(action='DESELECT'); mbo.select_set(True); bpy.context.view_layer.objects.active = mbo
bpy.ops.object.convert(target='MESH')
sk = bpy.context.view_layer.objects.active
apply_mod(sk, 'SMOOTH', factor=0.5, iterations=3)
apply_mod(sk, 'DECIMATE', ratio=0.14)
# eye sockets (orbits) and the nasal opening, cut deep
cutters = [ellipsoid(K(0.029, 0.09, 0.004), (0.0195, 0.045, 0.0175)), ellipsoid(K(-0.029, 0.09, 0.004), (0.0195, 0.045, 0.0175)),   # deep orbits
           ellipsoid(K(0, 0.095, -0.033), (0.0068, 0.04, 0.0105)), ellipsoid(K(0, 0.095, -0.024), (0.0036, 0.04, 0.007)),   # a short, tapered nasal cavity
           ellipsoid(K(0.05, 0.05, -0.042), (0.03, 0.04, 0.012)), ellipsoid(K(-0.05, 0.05, -0.042), (0.03, 0.04, 0.012)),   # crisp cheekbone undersides
           ellipsoid(K(0.07, 0.025, 0.002), (0.014, 0.035, 0.03)), ellipsoid(K(-0.07, 0.025, 0.002), (0.014, 0.035, 0.03))]   # temple hollows: narrow behind the orbits
def tube_tmp(pts, r, seg=8):   # a closed tube as a temporary cutter (a groove)
    bm = bmesh.new(); rows = []; pts = [Vector(q) for q in pts]
    for i, q in enumerate(pts):
        T = (pts[min(i + 1, len(pts) - 1)] - pts[max(i - 1, 0)]).normalized(); Nn = T.orthogonal().normalized(); Bn = T.cross(Nn)
        rows.append([bm.verts.new(q + (Nn * math.cos(2 * math.pi * k / seg) + Bn * math.sin(2 * math.pi * k / seg)) * r) for k in range(seg)])
    for i in range(len(rows) - 1):
        for k in range(seg): bm.faces.new((rows[i][k], rows[i][(k + 1) % seg], rows[i + 1][(k + 1) % seg], rows[i + 1][k]))
    bm.faces.new(list(reversed(rows[0]))); bm.faces.new(rows[-1])
    return tmp_obj(bm)
cutters.append(tube_tmp([K(0, math.sin(a) * 0.09 - 0.03, math.cos(a) * 0.072 + 0.035) for a in [-0.35 + 1.75 * k / 16 for k in range(17)][::-1]], 0.0028))   # sagittal seam (recessed)
cutters.append(tube_tmp([K(math.sin(t) * 0.066, -0.02, 0.035 + math.cos(t) * 0.072) for t in [-1.2 + 2.4 * k / 18 for k in range(19)]], 0.0028))   # coronal seam
for side in (-1, 1): cutters.append(tube_tmp([K(side * (0.05 + 0.012 * math.sin(t)), 0.03 - 0.05 * t, 0.045 - 0.03 * t) for t in [k / 10 for k in range(11)]], 0.0024))   # temporal seams
for c in cutters:
    apply_mod(sk, 'BOOLEAN', operation='DIFFERENCE', object=c, solver='EXACT')
    bpy.data.objects.remove(c, do_unlink=True)
adopt(sk, 'Head', 'polished')
# temple bolts (the seams are cut into the cranium above)
for side in (-1, 1):
    cyl(K(0.071 * side, 0.01, 0.012), K(0.079 * side, 0.01, 0.012), 0.008, 'Head', 'gunmetal', seg=8)
pbox(K(0, 0.087, -0.011), (0.014, 0.012, 0.016), 'Head', 'polished', rot=Matrix.Rotation(math.radians(45), 3, 'Y'), bevel=0.003)   # the nose bridge, faceted
# the eyes: a small red lens set back in each orbit, ringed by a dark iris
for side in (-1, 1):
    e = ellipsoid(K(0.029 * side, 0.05, 0.002), (0.0042, 0.0035, 0.0042), seg=14, ring=8); adopt(e, 'Head', 'eye')   # smaller, deeper
    ring(K(0.029 * side, 0.052, 0.002), (0, -1, 0), 0.0062, 0.0017, 'Head', 'rubber', seg=16, sseg=6)   # the iris ring
    cyl(K(0.029 * side, 0.045, 0.004), K(0.029 * side, 0.05, 0.004), 0.011, 'Head', 'rubber', seg=14)   # the dark socket back behind the lens


# teeth: individual rounded chrome teeth along each jaw's arch (narrow at the front, broad molars at the back), roots in the bone
def teeth_row(z, ry, rx, h, bone, n=14, down=True):
    for k in range(n):
        u = (k + 0.5) / n; a = math.radians(-84 + 168 * u); edge = abs(u - 0.5) * 2   # 0 at the front, 1 at the back: the rows curve back at the corners
        w = (0.0042 + 0.0032 * edge) * (0.85 + 0.3 * ((k * 7919 + int(z * 1e4)) % 97) / 97); hh = h * (1.0 - 0.25 * edge)   # varied widths
        c = K(math.sin(a) * rx, 0.05 + math.cos(a) * ry, z + (-1 if down else 1) * 0.0015 * edge)
        ball(c, 1.0, bone, 'polished', seg=8, ring=6, scale=(w * 0.5, 0.0042 + 0.0012 * edge, hh * 0.5))
teeth_row(-0.0665, 0.04, 0.026, 0.014, 'Head')            # upper arch, set in the maxilla
# the mandible: one cast jawbone (body round the arch, chin, the angles, rami up to the hinge pins), lower teeth on it
jaw = []
for k in range(11):   # the body along the arch
    u = k / 10; a = math.radians(-74 + 148 * u)
    jaw.append((tuple(K(math.sin(a) * 0.04, 0.025 + math.cos(a) * 0.052, -0.087)), (0.008, 0.01, 0.011), None))
jaw.append((tuple(K(0, 0.08, -0.093)), (0.014, 0.01, 0.011), None))   # the chin
for side in (-1, 1):
    for k in range(5):   # the ramus: from the angle up and back to the condyle under the hinge
        t = k / 4
        jaw.append((tuple(K(side * (0.044 - 0.004 * t), 0.012 - 0.01 * t, -0.092 + 0.05 * t)), (0.006, 0.012, 0.01), None))
blob(jaw, 'Head', 'brushed', res=0.003, decimate=0.4)
for side in (-1, 1):
    cyl(K(side * 0.036, 0.0, -0.042), K(side * 0.052, 0.0, -0.042), 0.008, 'Head', 'gunmetal', seg=10)   # hinge pins
teeth_row(-0.0758, 0.037, 0.024, 0.011, 'Head', n=14, down=False)   # lower arch, set in the mandible

# ---- arms: humerus with a ball head, radius + ulna, hinge hubs, pistons and cables, five-fingered skeletal hands
for S, side in (('Left', 1), ('Right', -1)):
    sh, ua, fa, hd = J(S + 'Shoulder'), J(S + 'Arm'), J(S + 'ForeArm'), J(S + 'Hand')
    ax = (fa - ua).normalized(); up = Vector((0, 0, 1)); perp = ax.cross(up).normalized()   # across the limb, horizontal
    ball(ua, 0.033, S + 'Arm', 'polished', seg=16, ring=10)                                  # humeral head in its socket
    ring(ua, (1, 0, 0), 0.046, 0.007, S + 'Shoulder', 'gunmetal')                            # the socket collar
    shaft(ua + ax * 0.02, fa - ax * 0.01, 0.018, S + 'Arm', 'chrome', flare=1.7)              # humerus
    actuator(ua + ax * 0.03, fa, perp * 0.03 + up * 0.012, S + 'Arm', S + 'Arm', r=0.009)    # biceps piston
    actuator(ua + ax * 0.03, fa, -perp * 0.026 - up * 0.01, S + 'Arm', S + 'Arm', r=0.008)   # triceps piston
    mid = ua.lerp(fa, 0.5) + up * 0.035
    sweep([ua + up * 0.03, mid, fa + up * 0.02], 0.0045, S + 'Arm', 'gunmetal', seg=6)       # a cable over the arm
    cyl(fa - perp * 0.03, fa + perp * 0.03, 0.02, S + 'ForeArm', 'gunmetal', seg=14)         # elbow hinge
    ball(fa + ax * 0.005 - up * 0.02, 0.016, S + 'ForeArm', 'polished')                      # olecranon (the elbow point)
    fx = (hd - fa).normalized(); fperp = fx.cross(up).normalized()
    shaft(fa + up * 0.012, hd + up * 0.01, 0.011, S + 'ForeArm', 'chrome', flare=1.6)        # radius
    shaft(fa - up * 0.012, hd - up * 0.008, 0.0105, S + 'ForeArm', 'chrome', flare=1.6)      # ulna
    actuator(fa + fx * 0.02, hd - fx * 0.02, fperp * 0.022, S + 'ForeArm', S + 'ForeArm', r=0.007)
    sweep([fa - fperp * 0.02, fa.lerp(hd, 0.5) - fperp * 0.03, hd - fperp * 0.012], 0.0035, S + 'ForeArm', 'gunmetal', seg=6)
    tb = J(S + 'HandThumb1'); mid1 = J(S + 'HandMiddle1')
    blob([(tuple(hd.lerp(mid1, 0.5)), (0.03, 0.03, 0.012), None), (tuple(hd.lerp(tb, 0.7)), (0.022, 0.02, 0.02), None), (tuple(hd), (0.022, 0.018, 0.014), None)], S + 'Hand', 'brushed', res=0.003, decimate=0.45)   # palm with a thick thumb base
    for f in ('Thumb', 'Index', 'Middle', 'Ring', 'Pinky'):
        pts = [J(S + 'Hand' + f + str(i)) for i in (1, 2, 3, 4)]
        rr = 0.0072 if f != 'Pinky' else 0.0062
        if f == 'Thumb': rr = 0.0082
        shaft(hd + (pts[0] - hd) * 0.15, pts[0], rr, S + 'Hand', 'chrome', flare=1.5, seg=8, rings=6)   # metacarpal
        for k in range(3):
            ball(pts[k], rr * 1.35, S + 'Hand' + f + str(k + 1), 'gunmetal', seg=8, ring=6)               # knuckle
            hx = (pts[k + 1] - pts[k]).normalized().cross(Vector((0, 0, 1))).normalized(); cyl(pts[k] - hx * rr * 1.6, pts[k] + hx * rr * 1.6, rr * 0.5, S + 'Hand' + f + str(k + 1), 'chrome', seg=6)   # hinge pin
            shaft(pts[k], pts[k + 1], rr * (1.0 - 0.22 * k), S + 'Hand' + f + str(k + 1), 'chrome' if k < 2 else 'polished', flare=1.4, seg=8, rings=5)   # tapering toward the tip
        tip = pts[3] + (pts[3] - pts[2]).normalized() * 0.006
        ball(tip, rr * 0.9, S + 'Hand' + f + '3', 'polished', seg=8, ring=6)                  # fingertip

# ---- armour (user: the look of a heavy armoured skull robot: bevelled plates over the exposed skeleton, ribs showing through an
# open chest frame, cable bundles inside open limb shells, big hinge discs, an ear disc, antennas). Our own plates and layout.

for side in (-1, 1):   # the back: a spine channel (two rails beside the column) and two compact shoulder-blade housings
    cyl(Vector((0.03 * side, back_y + 0.065, spine.z)), Vector((0.03 * side, back_y + 0.06, sp2.z + 0.08)), 0.008, 'Spine1', 'graphite', seg=8)
    pbox(Vector((0.085 * side, back_y + 0.05, sp2.z + 0.0)), (0.085, 0.035, 0.13), 'Spine2', 'shell', rot=Matrix.Rotation(math.radians(-10 * side), 3, 'Z'), bevel=0.014)
# waist: open, the segmented spine with curved cable bundles down either side (no block)
for side in (-1, 1):
    for k in range(3):
        a = Vector((side * (0.045 + 0.016 * k), spine.y + 0.0 - 0.01 * k, sp1.z - 0.1)); b = Vector((side * (0.05 + 0.012 * k), hips.y - 0.005 * k, hips.z + 0.06))
        sweep([a, a.lerp(b, 0.5) + Vector((side * 0.028, -0.014, 0)), b], 0.012 if k == 0 else 0.0055, 'Spine', 'rubber' if k == 0 else 'graphite', seg=8 if k == 0 else 6)   # one substantial cable a side
# pelvis: a tapered central plate, raised side guards, recessed hip pivots
pbox(Vector((0, hips.y - 0.07, hips.z + 0.005)), (0.13, 0.035, 0.09), 'Hips', 'chrome', bevel=0.012)
pbox(Vector((0, hips.y - 0.068, hips.z - 0.055)), (0.075, 0.032, 0.06), 'Hips', 'chrome', rot=Matrix.Rotation(math.radians(45), 3, 'Y'), bevel=0.01)   # its tapered point
for side in (-1, 1):
    pbox(Vector((0.125 * side, hips.y - 0.005, hips.z + 0.035)), (0.035, 0.13, 0.11), 'Hips', 'chrome', rot=Matrix.Rotation(math.radians(-18 * side), 3, 'Y'), bevel=0.012)   # raised side guards
    pbox(Vector((0.138 * side, hips.y - 0.008, hips.z - 0.04)), (0.03, 0.115, 0.07), 'Hips', 'chrome', rot=Matrix.Rotation(math.radians(-26 * side), 3, 'Y'), bevel=0.01)   # a lower side lame lapping under it
    pbox(Vector((0.06 * side, hips.y + 0.085, hips.z - 0.01)), (0.09, 0.03, 0.08), 'Hips', 'chrome', rot=Matrix.Rotation(math.radians(12 * side), 3, 'Z') @ Matrix.Rotation(math.radians(-15), 3, 'X'), bevel=0.01)   # rear plates, either side of the column
    th_ = J(('Left' if side > 0 else 'Right') + 'UpLeg')
    disc(th_ + Vector((0.055 * side, 0, 0)), (1, 0, 0), 0.04, 0.012, 'Hips')   # the recessed hip pivot
band([Vector((0.145 * math.sin(math.radians(a)), hips.y - 0.01 - 0.092 * math.cos(math.radians(a)), hips.z + 0.072)) for a in range(-125, 126, 25)], 0.016, 0.007, 'Hips', 'polished', seg=10)   # the belt, front and sides
for x in (-0.03, 0.03): pbox(Vector((x, hips.y - 0.1, hips.z + 0.072)), (0.018, 0.012, 0.03), 'Hips', 'graphite', bevel=0.003)   # its buckle studs
# antennas behind the shoulder line, with visible bases
for side in (-1, 1):
    base = Vector((0.12 * side, back_y + 0.03, sp2.z + 0.09)); tip = base + Vector((0.03 * side, 0.06, 0.28)) * (1.0 if side < 0 else 0.75)
    cyl(base - Vector((0, 0, 0.022)), base + Vector((0, 0, 0.012)), 0.014, 'Spine2', 'graphite', seg=12)
    cyl(base, tip, 0.0035, 'Spine2', 'chrome', seg=6)
for side in (-1, 1): disc(K(0.064 * side, -0.005, 0.0), (1, 0, 0), 0.026, 0.005, 'Head')   # recessed temple discs, both sides
# arms: shoulder housings round visible pivots, tapered upper-arm shells open below, elbow hinges, dorsal forearm plates
# over parallel tendons, a palm plate
for S, side in (('Left', 1), ('Right', -1)):
    ua, fa, hd = J(S + 'Arm'), J(S + 'ForeArm'), J(S + 'Hand'); up = Vector((0, 0, 1)); ax = (fa - ua).normalized()
    cyl(ua - ax * 0.05, ua + ax * 0.05, 0.042, S + 'Arm', 'chrome', seg=20)                                   # the shoulder pivot cylinder
    hc_s = ua + Vector((0.035 * side, 0, 0.012))   # the housing stands off the rib cage (a gap between)
    dome(hc_s, (0.088, 0.09, 0.08), 90 * side - 115, 90 * side + 115, -38, 90, 0.014, S + 'Arm', 'shell', saz=16, sel=8, pw=1.7)   # (user: more diamond than round)   # crowned housing, outer edge swept down, open inside round the pivot
    psweep([hc_s + Vector((-0.05 * side, 0.0, 0.07)), hc_s + Vector((0.01 * side, 0, 0.088)), hc_s + Vector((0.07 * side, 0, 0.05))], lambda t: 0.012, lambda t: 0.007, S + 'Arm', 'graphite', up=(0, 1, 0), out=lambda q: (0, 0, 1), seg=10)   # the raised crown ridge
    for k in range(3): box(hc_s + Vector((0.082 * side, -0.02 + 0.02 * k, 0.0)), (0.004, 0.012, 0.04), S + 'Arm', 'graphite', bevel=0.001)   # vents in the outer face
    shell_tube(ua + ax * 0.1, fa - ax * 0.035, 0.062, 0.04, 220, up, 0.01, S + 'Arm', 'shell', seg=14, rings=8, bulge=0.16)   # upper arm: tapered, full, open below
    cyl(fa - up * 0.045, fa + up * 0.045, 0.034, S + 'ForeArm', 'graphite', seg=20)                          # the elbow hinge
    for d in (-1, 1): pbox(fa + up * 0.05 * d - ax * 0.03, (0.07, 0.04, 0.012), S + 'Arm', 'shell', rot=along(fa - ax * 0.1, fa, up), bevel=0.005)   # the upper arm's fork round it
    for d in (-1, 1): cyl(fa + up * 0.045 * d, fa + up * 0.051 * d, 0.03, S + 'ForeArm', 'chrome', seg=20)   # its bright caps
    fx = (hd - fa).normalized()
    shell_tube(fa + fx * 0.04, hd - fx * 0.03, 0.045, 0.03, 150, up, 0.008, S + 'ForeArm', 'shell', seg=12, rings=8, bulge=0.08)   # dorsal plate
    for k in range(4):   # parallel tendons under the forearm
        off = Vector((0, (k - 1.5) * 0.011, 0)); cyl(fa + fx * 0.05 - up * 0.022 + off, hd - fx * 0.02 - up * 0.015 + off * 0.7, 0.0035, S + 'ForeArm', 'graphite', seg=6)
    pbox(hd + fx * 0.045 + up * 0.012, (0.075, 0.07, 0.018), S + 'Hand', 'shell', rot=along(hd, hd + fx, up), bevel=0.006)   # the palm plate (back of the hand)
# ---- legs, v5 (user brief: a skeleton you can follow from hip to toe, armour on selected surfaces): a femur and a different-
# profiled secondary linkage on mounts; a thigh shell over the upper front-outside only, its lower edge cut back on a diagonal
# with a visible lip, the inner thigh open; the knee as a fork on the femur with a drum between its cheeks on the shin's axle
# and a projecting outer cap; a ridged tibia beam, an outer rail and a rear linkage bridged near knee and ankle with a daylight
# gap between; an ankle pivot between two side supports; a heel block, an angled instep and two thick split toe plates
for S, side in (('Left', 1), ('Right', -1)):
    th, kn, an, toe, te = J(S + 'UpLeg'), J(S + 'Leg'), J(S + 'Foot'), J(S + 'ToeBase'), J(S + 'Toe_End')
    fwd = Vector((0, -1, 0)); lat = Vector((side, 0, 0)); ax = (kn - th).normalized(); L = (kn - th).length
    # v6 (user): the femur is shaped and shown - a broad hip attachment, a narrower shaft with a raised web, a fork widening
    # round the knee axle - with an offset actuator (housing, sliding shaft, pins); the armour is one guard high on the outer
    # face (broad upper corner, ridge, tapered tip), standing off the femur on two brackets with daylight between
    ball(th, 0.036, S + 'UpLeg', 'chrome', seg=16, ring=10)
    pbox(th + ax * 0.05 + lat * 0.012, (0.07, 0.07, 0.08), S + 'UpLeg', 'graphite', rot=along(th, kn, fwd), bevel=0.014)   # broad hip attachment
    fem = bezier(th + ax * 0.08, th + ax * L * 0.4 + fwd * 0.012, th + ax * L * 0.72 + fwd * 0.008, kn - ax * 0.07, 12)
    psweep(fem, lambda t: 0.02 - 0.004 * math.sin(math.pi * t), lambda t: 0.014 - 0.003 * math.sin(math.pi * t), S + 'UpLeg', 'graphite', up=tuple(fwd), out=lambda p: lat, seg=12)   # the shaft
    psweep([q + fwd * 0.016 for q in fem[1:-1]], lambda t: 0.004, lambda t: 0.009, S + 'UpLeg', 'shell', up=tuple(lat), out=lambda p: fwd, seg=8)   # its raised web, forward
    ha, hb = th + ax * 0.1 - fwd * 0.045 + lat * 0.03, th + ax * L * 0.62 - fwd * 0.05 + lat * 0.03; sb = kn - ax * 0.08 - fwd * 0.05 + lat * 0.028
    cyl(ha, hb, 0.016, S + 'UpLeg', 'graphite', seg=14); cyl(hb, sb, 0.009, S + 'UpLeg', 'polished', seg=12)   # actuator housing + sliding shaft
    for q in (ha, sb): cyl(q - lat * 0.02, q + lat * 0.02, 0.007, S + 'UpLeg', 'chrome', seg=10)                # its pins
    sweep([th - fwd * 0.02 - lat * 0.02, th.lerp(kn, 0.5) - fwd * 0.03 - lat * 0.03, kn - ax * 0.06 - fwd * 0.03 - lat * 0.02], 0.007, S + 'UpLeg', 'rubber', seg=6)   # a hydraulic line
    # the outer guard: rows down the thigh, its arc narrowing to a tapered tip, centred on the outer face
    bm = bmesh.new(); outer, inner = [], []; seg_, rings_ = 14, 10; Lg = L * 0.6
    for ii in range(rings_ + 1):
        t = ii / rings_; arc = 125 * (1 - 0.8 * t ** 1.3); cdir = (lat * 0.9 + fwd * (0.45 - 0.25 * t)).normalized(); gdir = ax.cross(cdir)
        r = 0.088 - 0.012 * t; c = th + ax * (0.02 * L + t * Lg); ro, ri = [], []
        for k in range(seg_ + 1):
            ph = math.radians(-arc / 2 + arc * k / seg_); d = cdir * math.cos(ph) + gdir * math.sin(ph)
            ro.append(bm.verts.new(c + d * r)); ri.append(bm.verts.new(c + d * (r - 0.014)))
        outer.append(ro); inner.append(ri)
    _shell_faces(bm, outer, inner); smooth_part(finish(bm, S + 'UpLeg', 'shell'))
    gc = lambda t: th + ax * (0.02 * L + t * Lg) + (lat * 0.9 + fwd * (0.45 - 0.25 * t)).normalized() * (0.09 - 0.012 * t)
    psweep([gc(t) for t in (0.0, 0.25, 0.5, 0.75, 0.95)], lambda t: 0.006 - 0.003 * t, lambda t: 0.006, S + 'UpLeg', 'polished', up=tuple(fwd), out=lambda p: lat, seg=8)   # the raised ridge
    for tb_ in (0.2, 0.62):   # two brackets from its underside to the frame, paired fasteners on each
        q0 = th + ax * (0.02 * L + tb_ * Lg) + lat * 0.02; q1 = q0 + (lat * 0.9 + fwd * 0.35).normalized() * 0.06
        cyl(q0, q1, 0.008, S + 'UpLeg', 'graphite', seg=8)
        for dd in (-1, 1): cyl(q1 + ax * 0.012 * dd, q1 + ax * 0.012 * dd + lat * 0.008, 0.0035, S + 'UpLeg', 'chrome', seg=6)
    sx = sx_ = (an - kn).normalized()
    # the knee: two cheeks forked off the femur, a drum between them on the shin's axle, a projecting outer cap
    for d in (-1, 1):
        ch = kn + lat * 0.05 * d - ax * 0.032
        pbox(ch, (0.016, 0.085, 0.095), S + 'UpLeg', 'graphite', rot=along(kn - ax * 0.08, kn, fwd), bevel=0.007)   # the fork cheeks, wide round the drum
    cyl(kn - lat * 0.04, kn + lat * 0.04, 0.05, S + 'Leg', 'shell', seg=24)                           # the drum (turns with the shin)
    cyl(kn - lat * 0.006, kn + lat * 0.006, 0.0515, S + 'Leg', 'polished', seg=24)                     # the polished band where it runs
    for d in (-1, 1): cyl(kn + lat * 0.034 * d, kn + lat * 0.036 * d, 0.04, S + 'Leg', 'graphite', seg=20)   # drum rims
    cyl(kn - lat * 0.062, kn + lat * 0.062, 0.012, S + 'Leg', 'chrome', seg=12)                         # the axle
    cyl(kn + lat * 0.05, kn + lat * 0.075, 0.034, S + 'Leg', 'chrome', seg=22)                          # outer cap, projecting
    cyl(kn + lat * 0.075, kn + lat * 0.082, 0.016, S + 'Leg', 'graphite', seg=14)                      # recessed fastener
    ring(kn + lat * 0.076, (1, 0, 0), 0.028, 0.0025, S + 'Leg', 'polished', seg=20, sseg=6)            # retaining ring on the cap
    cyl(kn - fwd * 0.052 + sx_ * 0.04, kn - fwd * 0.05 + sx_ * 0.24, 0.017, S + 'Leg', 'graphite', seg=12)   # calf actuator housing (depth behind the shin)
    # the shin: a ridged tibia beam forward, a slim outer rail, a rear linkage; bridges near knee and ankle; a short guard on top
    Ls = (an - kn).length
    tib = bezier(kn + sx * 0.05 + fwd * 0.035, kn + sx * Ls * 0.35 + fwd * 0.05, kn + sx * Ls * 0.75 + fwd * 0.03, an - sx * 0.035 + fwd * 0.01, 12)
    psweep(tib, lambda t: 0.017 - 0.006 * t, lambda t: 0.02 - 0.008 * t, S + 'Leg', 'graphite', up=tuple(lat), out=lambda p: fwd, seg=12, ex=2.4, convex=0.6)   # the ridge faces forward
    rail_a, rail_b = kn + sx * 0.05 + lat * 0.03 - fwd * 0.005, an - sx * 0.04 + lat * 0.022
    cyl(rail_a, rail_b, 0.007, S + 'Leg', 'chrome', seg=8)
    link_a, link_b = kn + sx * 0.05 - fwd * 0.035, an - sx * 0.05 - fwd * 0.03
    cyl(link_a, link_b, 0.011, S + 'Leg', 'graphite', seg=10)
    actuator(link_a + lat * 0.004, link_b, -fwd * 0.012, S + 'Leg', S + 'Leg', r=0.007)
    for tt in (0.12, 0.88):   # bridges: the daylight gap runs between them
        c = kn.lerp(an, tt); pbox(c + fwd * 0.004, (0.075, 0.085, 0.022), S + 'Leg', 'graphite', rot=along(kn, an, fwd), bevel=0.006)
    shell_tube(kn + sx * 0.07, kn + sx * Ls * 0.45, 0.052, 0.04, 140, fwd, 0.01, S + 'Leg', 'shell', seg=12, rings=6, bulge=0.05)   # short upper shin guard
    # ankle: the pivot between two side supports, a heel block behind, an angled instep, two thick toe plates split down the middle
    for d in (-1, 1): pbox(an + lat * 0.036 * d + sx * -0.03, (0.012, 0.045, 0.065), S + 'Leg', 'graphite', rot=along(an - sx * 0.07, an, fwd), bevel=0.004)
    cyl(an - lat * 0.045, an + lat * 0.045, 0.014, S + 'Foot', 'chrome', seg=14)
    cyl(an - lat * 0.03, an + lat * 0.03, 0.026, S + 'Foot', 'shell', seg=18)
    heel_y = an.y + 0.06
    pbox(Vector((an.x, (heel_y + toe.y) / 2, 0.012)), (0.075, abs(heel_y - toe.y) + 0.01, 0.024), S + 'Foot', 'graphite', bevel=0.006)   # sole plate, heel to toe joint
    pbox(Vector((an.x, heel_y - 0.012, 0.042)), (0.07, 0.045, 0.06), S + 'Foot', 'shell', bevel=0.012)                                   # heel block on it
    ia, ib = Vector((an.x, an.y - 0.01, an.z - 0.005)), Vector((toe.x, toe.y + 0.012, 0.03))
    pbox(ia.lerp(ib, 0.5), (0.068, 0.03, (ib - ia).length + 0.02), S + 'Foot', 'shell', rot=along(ia, ib, Vector((0, 0, 1))), bevel=0.01)   # the angled instep, ankle down to the toe joint
    cyl(Vector((toe.x - 0.04, toe.y, 0.018)), Vector((toe.x + 0.04, toe.y, 0.018)), 0.012, S + 'ToeBase', 'chrome', seg=12)            # the toe hinge
    for d in (-1, 1):   # two toe plates, a clear split between
        pbox(Vector((toe.x + d * 0.021, (toe.y + te.y) / 2 - 0.006, 0.018)), (0.035, abs(te.y - toe.y) + 0.025, 0.03), S + 'ToeBase', 'shell', bevel=0.01)

# ---- wiring (user: underlying wiring, more pieces overall): harnesses of parallel cables, chrome clamps along each run and a
# connector block at each end, one bone per harness so a run never stretches; plus servo boxes, wrist rotators, junctions
def harness(p0, p1, p2, p3, n, spread, across, bone, r=0.0042, clamps=(0.35, 0.7), ends=True):
    pts = bezier(Vector(p0), Vector(p1), Vector(p2), Vector(p3), 6); across = Vector(across).normalized()
    for i in range(n):
        off = across * (i - (n - 1) / 2) * spread
        sweep([q + off for q in pts], r, bone, ('rubber', 'graphite', 'gunmetal')[i % 3], seg=5)
    w = spread * (n - 1) + 2 * r + 0.008
    for t in clamps:
        k = round(t * (len(pts) - 1)); a, b = pts[max(k - 1, 0)], pts[min(k + 1, len(pts) - 1)]
        box(pts[k], (w, 2 * r + 0.007, 0.012), bone, 'chrome', rot=along(a, b, across), bevel=0.002)
    if ends:
        for k, a, b in ((0, pts[0], pts[1]), (-1, pts[-2], pts[-1])):
            box(pts[k], (w + 0.004, 2 * r + 0.012, 0.018), bone, 'graphite', rot=along(a, b, across), bevel=0.003)


X, Y, Z = Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1))
harness((0, neck_y + 0.032, sp2.z + 0.07), (0, neck_y + 0.034, neck.z), (0, neck_y + 0.034, skull_base.z - 0.03), (0, neck_y + 0.03, skull_base.z + 0.005), 4, 0.009, X, 'Neck')   # up the back of the neck into the skull
harness((0, back_y + 0.05, spine.z - 0.01), (0, back_y + 0.056, sp1.z), (0, back_y + 0.056, sp2.z), (0, back_y + 0.05, sp2.z + 0.08), 3, 0.011, X, 'Spine1', clamps=(0.2, 0.5, 0.8))   # down the back channel
for side in (-1, 1):
    S = 'Left' if side > 0 else 'Right'; lat = X * side
    harness((0.1 * side, yc + 0.012, sp2.z + 0.04), (0.135 * side, yc + 0.0, sp2.z - 0.04), (0.12 * side, yc + 0.0, sp1.z - 0.04), (0.06 * side, yc + 0.01, sp1.z - 0.11), 3, 0.009, Y, 'Spine1')   # inside the rib cage, down each flank
    box(Vector((0.105 * side, yc + 0.03, sp2.z + 0.065)), (0.05, 0.04, 0.035), 'Spine2', 'gunmetal', bevel=0.004)          # shoulder servo box
    disc(Vector((0.132 * side, yc + 0.03, sp2.z + 0.065)), (1, 0, 0), 0.014, 0.005, 'Spine2')                             # its output hub
    box(Vector((0.055 * side, hips.y + 0.035, hips.z + 0.05)), (0.035, 0.03, 0.04), 'Hips', 'graphite', bevel=0.004)       # hip junction block
    for d in (-1, 1): cyl(Vector((0.055 * side + 0.009 * d, hips.y + 0.035, hips.z + 0.07)), Vector((0.055 * side + 0.009 * d, hips.y + 0.035, hips.z + 0.082)), 0.004, 'Hips', 'chrome', seg=6)   # its pins
    ua, fa, hd = J(S + 'Arm'), J(S + 'ForeArm'), J(S + 'Hand'); ax = (fa - ua).normalized(); fx = (hd - fa).normalized()
    harness(ua + ax * 0.07 - Z * 0.018, ua.lerp(fa, 0.35) - Z * 0.03, ua.lerp(fa, 0.7) - Z * 0.03, fa - ax * 0.06 - Z * 0.02, 3, 0.009, Y, S + 'Arm')   # inside the upper arm shell
    harness(fa + fx * 0.05 + Y * 0.024, fa.lerp(hd, 0.4) + Y * 0.03, fa.lerp(hd, 0.7) + Y * 0.028, hd - fx * 0.05 + Y * 0.02, 2, 0.009, Z, S + 'ForeArm', clamps=(0.5,))   # along the forearm
    cyl(hd - fx * 0.05, hd - fx * 0.025, 0.027, S + 'ForeArm', 'graphite', seg=16)                                   # wrist rotator
    ring(hd - fx * 0.037, tuple(fx), 0.028, 0.0035, S + 'ForeArm', 'polished', seg=16, sseg=6)                      # its bearing ring
    th, kn, an = J(S + 'UpLeg'), J(S + 'Leg'), J(S + 'Foot'); lax = (kn - th).normalized(); sx = (an - kn).normalized(); fwd = -Y
    harness(th + lax * 0.1 - lat * 0.035, th.lerp(kn, 0.4) - lat * 0.05, th.lerp(kn, 0.75) - lat * 0.045, kn - lax * 0.09 - lat * 0.03, 3, 0.01, fwd, S + 'UpLeg')   # down the open inner thigh
    harness(kn + sx * 0.09 + Y * 0.05 - lat * 0.018, kn.lerp(an, 0.4) + Y * 0.058 - lat * 0.02, kn.lerp(an, 0.75) + Y * 0.05 - lat * 0.018, an - sx * 0.08 + Y * 0.04 - lat * 0.015, 2, 0.01, X, S + 'Leg', clamps=(0.4, 0.75))   # down the back of the shin
    box(kn + sx * 0.06 + Y * 0.03 - lat * 0.04, (0.022, 0.03, 0.04), S + 'Leg', 'gunmetal', bevel=0.004)           # knee junction box

# ---------------------------------------------------------------- join, skin (weight 1 per part), export
bpy.ops.object.select_all(action='DESELECT')
groups = {}
for o in parts:
    vg = o.vertex_groups.new(name=o['bone']); vg.add(list(range(len(o.data.vertices))), 1.0, 'REPLACE')
    o.select_set(True)
bpy.context.view_layer.objects.active = parts[0]
bpy.ops.object.join()
robot = bpy.context.view_layer.objects.active; robot.name = 'KR714'
robot.parent = arm
md = robot.modifiers.new('Armature', 'ARMATURE'); md.object = arm
tris = sum(len(p.vertices) - 2 for p in robot.data.polygons)
print('RB1 robot: %d tris, %d materials, %d bones used' % (tris, len(robot.data.materials), len(robot.vertex_groups)))
assert tris < 60000, tris

bpy.ops.object.select_all(action='DESELECT'); robot.select_set(True); arm.select_set(True)
bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, 'robot_src.glb'), export_format='GLB', use_selection=True, use_active_scene=True, export_skins=True,   # this scene only (a live session's other scenes stay out)
                          export_animations=False, export_yup=True, export_apply=False)
print('RB1 exported', os.path.join(OUT, 'robot_src.glb'))

# ---------------------------------------------------------------- the approval renders (Eevee, real materials)
if RENDER:
    sc = bpy.context.scene
    sc.render.engine = 'BLENDER_EEVEE' if 'BLENDER_EEVEE' in {e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items} else 'BLENDER_EEVEE_NEXT'
    sc.render.resolution_x, sc.render.resolution_y = 900, 1200
    sc.view_settings.view_transform = 'AgX' if 'AgX' in [v.identifier for v in sc.view_settings.bl_rna.properties['view_transform'].enum_items] else 'Filmic'
    w = bpy.data.worlds.new('sky'); sc.world = w; w.use_nodes = True
    bg = w.node_tree.nodes['Background']
    sky = w.node_tree.nodes.new('ShaderNodeTexSky')
    try: sky.sky_type = 'NISHITA'; sky.sun_elevation = math.radians(35); sky.sun_rotation = math.radians(220)
    except Exception: pass
    w.node_tree.links.new(sky.outputs['Color'], bg.inputs['Color']); bg.inputs['Strength'].default_value = 0.16
    sun = bpy.data.objects.new('sun', bpy.data.lights.new('sun', 'SUN')); sc.collection.objects.link(sun)
    sun.data.energy = 3.5; sun.rotation_euler = (math.radians(55), 0, math.radians(35))
    bpy.ops.mesh.primitive_plane_add(size=12); fl = bpy.context.object
    fm = material('floor', base=(0.10, 0.095, 0.085), rough=0.9); sc.view_settings.exposure = -0.4; fl.data.materials.append(fm)
    cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam')); sc.collection.objects.link(cam); sc.camera = cam
    cam.data.lens = 50

    def shoot(name, az, height=1.0, dist=4.2, rise=0.25, cx=0.0):
        a = math.radians(az)
        cam.location = Vector((cx + math.sin(a) * dist, -math.cos(a) * dist, height + rise))
        cam.rotation_euler = (Vector((cx, 0, height)) - cam.location).to_track_quat('-Z', 'Y').to_euler()
        sc.render.filepath = os.path.join(OUT, 'render_' + name + '.png'); bpy.ops.render.render(write_still=True)
        print('RB1 render', sc.render.filepath)

    def pose(action_name, frame):
        acts = [a for a in bpy.data.actions if a.name.split('|')[0].strip() == action_name or a.name.startswith(action_name)]
        if not acts: print('RB1 no action', action_name); return False
        arm.animation_data_create(); arm.animation_data.action = acts[0]
        if arm.animation_data.nla_tracks:
            for t in arm.animation_data.nla_tracks: t.mute = True
        sc.frame_set(frame); return True

    if arm.animation_data:
        for t in arm.animation_data.nla_tracks: t.mute = True
        arm.animation_data.action = None
    for b in arm.pose.bones: b.matrix_basis = Matrix.Identity(4)
    sc.frame_set(0)
    shoot('tpose_front', 0); shoot('tpose_side', 90); shoot('tpose_back', 180)
    shoot('head_front', 0, height=hc.z - 0.02, dist=0.55, rise=0.0, cx=hc.x); shoot('head_34', 35, height=hc.z - 0.02, dist=0.55, rise=0.03, cx=hc.x)
    eb = M['eye'].node_tree.nodes['Principled BSDF'].inputs['Emission Strength']; ev = eb.default_value; eb.default_value = 0.0
    shoot('head_front_noeyes', 0, height=hc.z - 0.02, dist=0.55, rise=0.0, cx=hc.x); eb.default_value = ev   # the expression test: the scowl without the lights
    shoot('legs_34', 40, height=0.5, dist=1.7, rise=0.05)   # hip to foot, daylight three-quarter
    # night: a faint moon, the eyes carry the head
    bg.inputs['Strength'].default_value = 0.004; sun.data.energy = 0.04; sc.view_settings.exposure = 0.6
    shoot('night', 15, height=1.45, dist=1.6, rise=0.05)
    bg.inputs['Strength'].default_value = 0.16; sun.data.energy = 3.5; sc.view_settings.exposure = -0.4
    for nm, act, fr, az in (('idle', 'idle', 10, 25), ('aim', 'idle aiming', 10, 25), ('run', 'run forward', 15, 70), ('crouch', 'idle crouching', 10, 25), ('reach', 'pistol kneeling idle', 10, 40)):
        if pose(act, fr): shoot(nm, az, height=0.8 if nm == 'crouch' else 1.0)

    def rest():
        if arm.animation_data: arm.animation_data.action = None
        for b in arm.pose.bones: b.matrix_basis = Matrix.Identity(4)
        sc.frame_set(0)

    clay = material('kr_clay', base=(0.5, 0.5, 0.5), rough=0.55)
    keep = [m for m in robot.data.materials]

    def set_clay(on):
        for k in range(len(robot.data.materials)): robot.data.materials[k] = clay if on else keep[k]

    # the clay checks (user brief: shapes first): the chest front / three-quarter, the left leg four ways, straight and bent
    rest(); set_clay(True)
    shoot('clay_chest_front', 0, height=sp1.z + 0.03, dist=1.05, rise=0.0)
    shoot('clay_chest_34', 38, height=sp1.z + 0.03, dist=1.05, rise=0.05)
    knee = J('LeftLeg'); lx = knee.x
    for nm, az in (('front', 0), ('34', 40), ('side', 90), ('rear', 180)): shoot('clay_leg_%s' % nm, az + 15, height=0.5, dist=1.45, rise=0.05, cx=lx)
    up_, kn_ = arm.pose.bones[P + 'LeftUpLeg'], arm.pose.bones[P + 'LeftLeg']
    for b in (up_, kn_): b.rotation_mode = 'XYZ'
    up_.rotation_euler = (math.radians(60), 0, 0); kn_.rotation_euler = (math.radians(95), 0, 0)   # the hip flexed, the knee bent ~90 deg
    bpy.context.view_layer.update()
    for nm, az in (('front', 0), ('34', 40), ('side', 90), ('rear', 180)): shoot('clay_legbent_%s' % nm, az + 15, height=0.5, dist=1.45, rise=0.05, cx=lx)
    set_clay(False)
    shoot('leg_closeup_34', 55, height=0.5, dist=0.95, rise=0.08, cx=lx)
    rest()
