# Blender (5.x) background script: bake the Battle Royale models (BR3) from the user's downloads.
#   blender -b --factory-startup --disable-autoexec -noaudio --python tools/bake_br_assets.py -- <br_assets> <glb out> <content> [ids...]
#   <br_assets>: Documents/MapMaker/incoming/br_assets (one folder per download, extracted there; untrusted data, never run)
#   <glb out>:   br_marine.glb and br/<id>.glb; then  NormandyMapmaker --import-weapon-models <glb out> <content>
#                bakes the items into content/assets/models/br_<id>/ (git-ignored: licences unknown, the repo is public)
#   <content>:   writes content/assets/br_<id>.json (committed): the stand-in boxes (drawn when the bake is missing),
#                the model block, and the fitted data the game reads: "worn" (helmet / vest / backpack placement on the
#                marine's rest pose, joint-relative in the game) and "sight" (the optic's eye point above its mount).
# Item space (glTF, metres): pivot "ground", i.e. centred x / z, lowest point y = 0; front (muzzle side) +z.
# The marine: Mixamo rig (joint names as bake_ai_characters.py), 1.80 m, feet at 0, facing +z, textures <= 1024.
import bpy, sys, os, json, math, re
import xml.etree.ElementTree as ET
from mathutils import Vector, Matrix

argv = sys.argv[sys.argv.index('--') + 1:]
SRC, OUT, CONTENT = argv[0], argv[1], argv[2]
ONLY = set(argv[3:])
os.makedirs(os.path.join(OUT, 'br'), exist_ok=True)
J = lambda *p: os.path.join(SRC, *p)

def reset(): bpy.ops.wm.read_factory_settings(use_empty=True)
def objs_of(t): return [o for o in bpy.context.scene.objects if o.type == t]
def select(objs):
    bpy.ops.object.select_all(action='DESELECT')
    for o in objs: o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]

def bbox(objs, verts=None):
    lo = Vector((1e9,) * 3); hi = -lo
    for o in objs:
        for v in o.data.vertices:
            w = o.matrix_world @ v.co; lo = Vector(map(min, lo, w)); hi = Vector(map(max, hi, w))
    return lo, hi

def shrink_images(tex):
    for img in bpy.data.images:
        w, h = img.size
        if w > tex or h > tex:
            k = tex / max(w, h); img.scale(max(1, int(w * k)), max(1, int(h * k)))

def mat_tex(name, png, rough=0.7, metal=0.0):
    m = bpy.data.materials.new(name); m.use_nodes = True
    b = m.node_tree.nodes.get('Principled BSDF')
    if png and os.path.exists(png):
        img = bpy.data.images.load(png); t = m.node_tree.nodes.new('ShaderNodeTexImage'); t.image = img
        m.node_tree.links.new(t.outputs['Color'], b.inputs['Base Color'])
    b.inputs['Roughness'].default_value = rough; b.inputs['Metallic'].default_value = metal
    return m

def mat_col(name, rgb, rough=0.6, metal=0.0):
    m = bpy.data.materials.new(name); m.use_nodes = True
    b = m.node_tree.nodes.get('Principled BSDF')
    b.inputs['Base Color'].default_value = (*rgb, 1); b.inputs['Roughness'].default_value = rough; b.inputs['Metallic'].default_value = metal
    return m

# ---------------------------------------------------------------- import: meshes only, unparented, unskinned
def load_dae(path, texdir):
    # minimal Collada reader (Blender 5 has no importer): Assimp's export, one index per corner (all inputs offset 0)
    ns = {'c': 'http://www.collada.org/2005/11/COLLADASchema'}
    root = ET.parse(path).getroot()
    out = []
    for gi, g in enumerate(root.iterfind('.//c:library_geometries/c:geometry', ns)):
        mesh = g.find('c:mesh', ns)
        arr = {s.get('id'): [float(x) for x in s.find('c:float_array', ns).text.split()] for s in mesh.iterfind('c:source', ns)}
        vin = mesh.find('c:vertices/c:input[@semantic="POSITION"]', ns).get('source')[1:]
        pl = mesh.find('c:polylist', ns)
        uvsrc = next((i.get('source')[1:] for i in pl.iterfind('c:input', ns) if i.get('semantic') == 'TEXCOORD' and i.get('set', '0') == '0'), None)
        P = arr[vin]; pts = [Vector((P[i], -P[i + 2], P[i + 1])) for i in range(0, len(P), 3)]   # Y up -> Blender Z up
        vc = [int(x) for x in pl.find('c:vcount', ns).text.split()]; idx = [int(x) for x in pl.find('c:p', ns).text.split()]
        faces = []; k = 0
        for n in vc: faces.append(idx[k:k + n]); k += n
        me = bpy.data.meshes.new(g.get('name') or 'dae'); me.from_pydata(pts, [], faces); me.update()
        if uvsrc:
            T = arr[uvsrc]; st = 3 if len(T) == len(pts) * 3 else 2
            uv = me.uv_layers.new(name='UVMap')
            for li, l in enumerate(me.loops): uv.data[li].uv = (T[l.vertex_index * st], T[l.vertex_index * st + 1])
        o = bpy.data.objects.new(me.name, me); bpy.context.scene.collection.objects.link(o); out.append(o)
    return out

def imp(path, keep=None, drop=()):
    before = set(bpy.data.objects); ext = path.lower().rsplit('.', 1)[-1]
    if ext == 'fbx': bpy.ops.import_scene.fbx(filepath=path)
    elif ext == 'obj': bpy.ops.wm.obj_import(filepath=path)
    elif ext == 'dae': load_dae(path, None)
    elif ext == 'blend':   # objects appended without running anything (scripts are disabled: --disable-autoexec)
        with bpy.data.libraries.load(path, link=False) as (src, dst): dst.objects = list(src.objects)
        for o in dst.objects:
            if o is not None: bpy.context.scene.collection.objects.link(o)
    else: bpy.ops.import_scene.gltf(filepath=path)
    new = [o for o in bpy.data.objects if o not in before]
    ms = [o for o in new if o.type == 'MESH' and len(o.data.polygons) and (not keep or any(k in o.name for k in keep))
          and not any(d in o.name for d in drop)]
    for o in ms:
        for md in list(o.modifiers):
            if md.type == 'ARMATURE': o.modifiers.remove(md)
        mw = o.matrix_world.copy(); o.parent = None; o.matrix_world = mw
    for o in new:
        if o not in ms: bpy.data.objects.remove(o, do_unlink=True)
    bpy.context.view_layer.update()
    return ms

def join(ms):
    for o in ms:
        if o.data.users > 1: o.data = o.data.copy()
    select(ms); bpy.ops.object.make_single_user(object=True, obdata=True)
    if len(ms) > 1: bpy.ops.object.join()
    o = bpy.context.view_layer.objects.active
    select([o]); bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    return o

def tris(o): return sum(len(p.vertices) - 2 for p in o.data.polygons)

def decimate(o, target):
    n = tris(o)
    if n <= target: return
    select([o]); d = o.modifiers.new('dec', 'DECIMATE'); d.ratio = target / n; bpy.ops.object.modifier_apply(modifier='dec')

def xform(o, M):
    o.data.transform(M); o.data.update()

def place_ground(o):   # centre x / y, lowest point z = 0 (glTF: centred x / z, y = 0), so the engine's "ground" pivot is a no-op
    lo, hi = bbox([o]); xform(o, Matrix.Translation(-Vector(((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, lo.z))))
    return bbox([o])

def size_to(o, axis, length):   # uniform scale so the extent along axis (0 x, 1 y, 2 z) is length
    lo, hi = bbox([o]); xform(o, Matrix.Scale(length / (hi[axis] - lo[axis]), 4))

def long_to_y(o):   # the longest extent onto Blender y (glTF z, the weapon axis)
    lo, hi = bbox([o]); e = hi - lo; a = max(range(3), key=lambda i: e[i])
    if a == 0: xform(o, Matrix.Rotation(math.pi / 2, 4, 'Z'))
    if a == 2: xform(o, Matrix.Rotation(math.pi / 2, 4, 'X'))

def big_end_forward(o):   # optics: the objective (the larger end) toward Blender -y (glTF +z, the muzzle side)
    lo, hi = bbox([o]); L = hi.y - lo.y
    def area(pick):
        vs = [v.co for v in o.data.vertices if pick(v.co.y)]
        if not vs: return 0
        return (max(v.x for v in vs) - min(v.x for v in vs)) * (max(v.z for v in vs) - min(v.z for v in vs))
    if area(lambda y: y > hi.y - 0.12 * L) > area(lambda y: y < lo.y + 0.12 * L) * 1.05: xform(o, Matrix.Rotation(math.pi, 4, 'Z'))

def set_materials(o, mats):
    o.data.materials.clear()
    for m in mats: o.data.materials.append(m)

def export(o, idn, tex):
    shrink_images(tex)
    for img in list(bpy.data.images):
        if img.users == 0: bpy.data.images.remove(img)
    o.name = idn; select([o])
    p = os.path.join(OUT, 'br', idn + '.glb')
    bpy.ops.export_scene.gltf(filepath=p, export_format='GLB', use_selection=True, export_animations=False, export_skins=False,
                              export_morph=False, export_image_format='JPEG', export_jpeg_quality=85, export_yup=True, export_apply=True,
                              export_extras=False, export_cameras=False, export_lights=False)
    print('wrote', p, os.path.getsize(p), 'tris', tris(o))

# ---------------------------------------------------------------- asset JSON (committed): stand-in box + model block + fit data
STANDIN = {'helmet': (0.30, 0.34, 0.20), 'vest': (0.22, 0.30, 0.16), 'backpack': (0.30, 0.32, 0.18), 'sight': (0.08, 0.08, 0.08),
           'att': (0.10, 0.10, 0.10), 'world': (0.6, 0.6, 0.55)}
CREDIT = {   # from the downloads' metadata (glTF asset.extras / AUTHOR); the rest carry none
    'br_sight_10x': "'Rifle Scope w/ Laser & Sight' by MaximDeduytsche (sketchfab.com/MaximDeduytsche), CC-BY-4.0 per its GLB metadata",
    'br_mag_ext_pistol': "'E-11 Magazine' by Matthew Spindler (sketchfab.com/The_DirtyMilkman), CC-BY-4.0 per its GLB metadata",
    'br_helmet_1': "'ARMOR | Armor and Helmet Model (CS2)' by 6lucius (sketchfab.com/6lucius), CC-BY-4.0 per its GLB metadata",
    'br_vest_1': "'ARMOR | Armor and Helmet Model (CS2)' by 6lucius (sketchfab.com/6lucius), CC-BY-4.0 per its GLB metadata",
    'br_vest_2': "'Combat Vest' by csheffield (sketchfab.com/csheffield), CC-BY-4.0 per its GLB metadata",
    'br_helmet_2': "'Military Helmet' by ED BOX (sketchfab.com/Elyar), CC-BY-4.0 per its GLB metadata",
    'br_vest_3': "'Military Armor 3D Model' by Exactly (sketchfab.com/txyrm70), CC-BY-4.0 per its GLB metadata",
    'br_backpack_mil': "'Military Backpack' by Neslihan Cakmak (sketchfab.com/neslihancakmak), CC-BY-4.0 per its GLB metadata",
    'br_parachute': "'Parachute Simple' by sxnneh (sketchfab.com/Sxnneh), CC-BY-4.0 per its GLB metadata",
    'br_backpack': "by smallpoly (sketchfab.com/smallpoly, the download's AUTHOR file); licence not stated",
    'br_recoil_pad': 'generated by tools/bake_br_assets.py',
}

def write_asset(idn, name, kind, size, extra=None):
    # size: the bake's glTF extents (x, y, z); the stand-in is one box of that size
    sx, sy, sz = (round(v, 4) for v in size)
    a = {'id': idn, 'version': 1, 'name': name, 'category': 'Battle Royale', 'hidden': True,
         'description': 'BR3 Battle Royale item. Stand-in box; the real model is local-only (licence unknown): bake it with tools/bake_br_assets.py.',
         'floors': 0, 'enterable': False, 'footprint': [max(sx, 0.05), max(sz, 0.05)], 'plinthDepth': 0, 'maxGrade': 1, 'noAutoPanels': True,
         'furniture': [], 'interactables': [], 'panels': [],
         'materials': [{'name': 'br_standin', 'albedo': list(STANDIN[kind]), 'roughness': 0.7, 'pattern': 'plain', 'metallic': 0, 'grain': 1}],
         'parts': [{'t': 'b', 'c': [0, round(sy / 2, 4), 0], 's': [sx, sy, sz], 'm': 'br_standin', 'k': 0}],
         'model': {'src': 'br/' + idn + '.glb', 'dir': idn, 'pivot': 'ground', 'scale': 1, 'maxTex': 1024,
                   'credit': CREDIT.get(idn, 'supplied by the user (Downloads); licence unknown: local-only')},
         'nav': {'effect': 'none'}}
    if extra: a.update(extra)
    p = os.path.join(CONTENT, 'assets', idn + '.json')
    try:   # BR10: keep the part-built stand-in (tools/br_standins.py) and per-item draw tweaks
        old = json.load(open(p))
        for k in ('parts', 'materials', 'standinParts', 'standinSize', 'description', 'attachScale'):
            if old.get('standinParts') and k in old: a[k] = old[k]
    except (OSError, ValueError): pass
    with open(p, 'w', newline='\n') as f: json.dump(a, f, indent=1)
    print('asset', p)

def gl_size(o):
    lo, hi = bbox([o]); e = hi - lo; return (e.x, e.z, e.y)

def r4(v): return [round(x, 4) for x in v]

def worn_block(joint, t, s):
    # Blender (x, y, z) -> glTF (x, z, -y): placement of the item in the marine's rest model space
    # rest: the marine's joint head there; another body moves the item by its own joint's offset from it
    j = FIT['head_bone' if joint == 'Head' else 'spine2']
    return {'worn': {'joint': joint, 'pos': r4((t.x, t.z, -t.y)), 'scale': r4((s.x, s.z, s.y)), 'rest': r4((j.x, j.z, -j.y))}}

# ---------------------------------------------------------------- the marine (player / bot body)
SEM = ['Hips', 'Spine', 'Spine1', 'Spine2', 'Neck', 'Head', 'LeftShoulder', 'LeftArm', 'LeftForeArm', 'LeftHand',
       'RightShoulder', 'RightArm', 'RightForeArm', 'RightHand', 'LeftUpLeg', 'LeftLeg', 'LeftFoot', 'LeftToeBase',
       'RightUpLeg', 'RightLeg', 'RightFoot', 'RightToeBase']
FIT = {}

def bake_marine():
    reset()
    bpy.ops.import_scene.fbx(filepath=J('us-marine', 'source', 'US_Marine1.fbx'))
    arm = objs_of('ARMATURE')[0]; meshes = objs_of('MESH')
    if arm.animation_data:
        arm.animation_data.action = None
        for t in list(arm.animation_data.nla_tracks): arm.animation_data.nla_tracks.remove(t)
    for a in list(bpy.data.actions): bpy.data.actions.remove(a)
    for pb in arm.pose.bones: pb.matrix_basis = Matrix()
    bpy.context.view_layer.update()
    for o in list(bpy.context.scene.objects):
        if o is not arm and o not in meshes: bpy.data.objects.remove(o, do_unlink=True)
    for o in [arm] + meshes:
        m = o.matrix_world.copy(); o.parent = None; o.matrix_world = m
    B = lambda n: arm.data.bones['mixamorig:' + n]
    d = arm.matrix_world.to_3x3() @ (B('LeftToeBase').head_local - B('LeftFoot').head_local)
    if d.y > 0: arm.matrix_world = Matrix.Rotation(math.pi, 4, 'Z') @ arm.matrix_world   # face Blender -y (glTF +z)
    bpy.context.view_layer.update()
    for o in meshes: o.matrix_world = o.matrix_world if d.y <= 0 else Matrix.Rotation(math.pi, 4, 'Z') @ o.matrix_world
    bpy.context.view_layer.update()
    lo, hi = bbox(meshes); s = 1.80 / (hi.z - lo.z)
    for o in [arm] + meshes: o.matrix_world = Matrix.Diagonal((s, s, s, 1)) @ o.matrix_world
    bpy.context.view_layer.update()
    lo, hi = bbox(meshes); off = Matrix.Translation(-Vector(((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, lo.z)))
    for o in [arm] + meshes: o.matrix_world = off @ o.matrix_world
    bpy.context.view_layer.update()
    select([arm] + meshes); bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    for mo in meshes:
        for md in mo.modifiers:
            if md.type == 'ARMATURE': md.object = arm
        mo.parent = arm; mo.matrix_parent_inverse = arm.matrix_world.inverted()
    bpy.context.view_layer.update()
    # materials: the diffuse map per part (Body / Bottom / Hat / Shoes / Top)
    tex = J('us-marine', 'textures'); mats = {}
    for mo in meshes:
        for sl in mo.material_slots:
            part = next((p for p in ('Body', 'Bottom', 'Hat', 'Shoes', 'Top') if sl.material and p.lower() in sl.material.name.lower()), None)
            if not part: continue
            if part not in mats: mats[part] = mat_tex('marine_' + part, os.path.join(tex, 'US_Marine1_%s_Diffuse.png' % part), 0.85)
            sl.material = mats[part]
    print('marine parts', [(mo.name, [sl.material.name for sl in mo.material_slots]) for mo in meshes])
    # fit data: vertices by dominant joint (rest pose, Blender coordinates)
    def region(groups, skip=('Hats',)):
        lo = Vector((1e9,) * 3); hi = -lo; n = 0
        for mo in meshes:
            if mo.name in skip: continue   # the cap is hidden under a helmet (br_models.cpp)
            gi = {g.index: g.name.split(':')[-1] for g in mo.vertex_groups}
            for v in mo.data.vertices:
                if not v.groups: continue
                g = max(v.groups, key=lambda g: g.weight)
                if gi.get(g.group) in groups:
                    w = mo.matrix_world @ v.co; lo = Vector(map(min, lo, w)); hi = Vector(map(max, hi, w)); n += 1
        return lo, hi, n
    hlo, hhi, _ = region({'Head', 'HeadTop_End'})
    tlo, thi, _ = region({'Spine1', 'Spine2'})
    def band(z0, z1, xmax):   # the body surface in a height band, arms excluded
        vs = [mo.matrix_world @ v.co for mo in meshes for v in mo.data.vertices]
        vs = [w for w in vs if z0 < w.z < z1 and abs(w.x) < xmax]
        return Vector((min(w.x for w in vs), min(w.y for w in vs), z0)), Vector((max(w.x for w in vs), max(w.y for w in vs), z1))
    clo, chi = band(1.33, 1.43, 0.13)    # chest depth: front (-y) and back (+y)
    wlo, whi = band(1.22, 1.28, 0.30)    # width under the armpits
    tlo = Vector((wlo.x, clo.y, tlo.z)); thi = Vector((whi.x, chi.y, thi.z))
    eyes = [o for o in meshes if o.name.startswith('Eyes')]
    elo, ehi = bbox(eyes)
    # the skull: head-weighted vertices above the eyes (cap excluded): its width, centre and crown
    sk = []
    for mo in meshes:
        if mo.name in ('Hats',): continue
        gi = {g.index: g.name.split(':')[-1] for g in mo.vertex_groups}
        for v in mo.data.vertices:
            if v.groups and gi.get(max(v.groups, key=lambda g: g.weight).group) in ('Head', 'HeadTop_End'):
                w = mo.matrix_world @ v.co
                if w.z > ehi.z: sk.append(w)
    slo = Vector((min(w.x for w in sk), min(w.y for w in sk), min(w.z for w in sk))); shi = Vector((max(w.x for w in sk), max(w.y for w in sk), max(w.z for w in sk)))
    FIT.update(head=(hlo, hhi), torso=(tlo, thi), neck=B('Neck').head_local.copy(), spine2=B('Spine2').head_local.copy(),
               head_bone=B('Head').head_local.copy(), eyes=(elo, ehi), skull=(slo, shi), back=chi.y)
    print('FIT eyes', elo, ehi, 'skull', slo, shi)
    print('FIT head', hlo, hhi, 'torso', tlo, thi, 'neck', FIT['neck'])
    shrink_images(1024)
    select([arm] + meshes)
    p = os.path.join(OUT, 'br_marine.glb')
    bpy.ops.export_scene.gltf(filepath=p, export_format='GLB', use_selection=True, export_animations=False, export_skins=True,
                              export_morph=False, export_image_format='JPEG', export_jpeg_quality=85, export_yup=True,
                              export_extras=False, export_def_bones=False)
    print('wrote', p, os.path.getsize(p))

# ---------------------------------------------------------------- worn gear
def drop_front_parts(o, frac=0.22):
    # loose parts in the front `frac` of the item, or hanging low in its front half (goggles over the face), go
    import bmesh
    bm = bmesh.new(); bm.from_mesh(o.data); bm.verts.ensure_lookup_table()
    lo, hi = bbox([o]); cut = lo.y + frac * (hi.y - lo.y); seen = set(); kill = []
    for v in bm.verts:
        if v in seen: continue
        comp = []; stack = [v]; seen.add(v)
        while stack:
            a = stack.pop(); comp.append(a)
            for e in a.link_edges:
                b = e.other_vert(a)
                if b not in seen: seen.add(b); stack.append(b)
        cy = sum(c.co.y for c in comp) / len(comp); cz = sum(c.co.z for c in comp) / len(comp)
        if cy < cut or (cz < lo.z + 0.3 * (hi.z - lo.z) and cy < (lo.y + hi.y) / 2): kill += comp   # also what hangs low at the front half
    bmesh.ops.delete(bm, geom=kill, context='VERTS'); bm.to_mesh(o.data); bm.free()
    print('dropped front parts:', len(kill), 'verts')

def fit_helmet(o, idn, name, kw=1.25, gap=0.02, back=0.0):
    # fitted to the skull, not to the item's box (goggles, NVG mounts and headsets stand off the shell): the dome
    # (upper half) is kw x the skull's width, centred over it (+ back), its crown `gap` above the scalp
    slo, shi = FIT['skull']; lo, hi = bbox([o]); e = hi - lo
    dome = [v.co for v in o.data.vertices if v.co.z > lo.z + 0.5 * e.z]
    dx0, dx1 = min(v.x for v in dome), max(v.x for v in dome); dy0, dy1 = min(v.y for v in dome), max(v.y for v in dome)
    s = (shi.x - slo.x) * kw / (dx1 - dx0)
    crown = [v.z for v in dome if abs(v.x - (dx0 + dx1) / 2) < 0.15 * (dx1 - dx0) and abs(v.y - (dy0 + dy1) / 2) < 0.15 * (dy1 - dy0)]
    ctop = max(crown) if crown else hi.z
    S = Vector((s, s, s))
    scalp = max(shi.z, FIT['eyes'][1].z + 0.105)   # the mesh has no scalp under its cap: the crown ~10.5 cm over the eyes
    t = Vector(((slo.x + shi.x) / 2 - s * (dx0 + dx1) / 2, (slo.y + shi.y) / 2 + back - s * (dy0 + dy1) / 2, scalp + gap - s * ctop))
    print('helmet', idn, 'scale', round(s, 3), 'rim z', round(t.z + s * lo.z, 3), 'eyes top', round(FIT['eyes'][1].z, 3))
    write_asset(idn, name, 'helmet', gl_size(o), worn_block('Head', t, S))

def fit_vest(o, idn, name, lift=0.0, wk=1.0, dk=1.0, zk=1.0):
    # wraps the chest: width and depth from the body (+ room for the plates), its front plate's top at the sternum
    tlo, thi = FIT['torso']; lo, hi = bbox([o]); e = hi - lo
    sx = (thi.x - tlo.x + 0.06) * wk / e.x; sy = (thi.y - tlo.y + 0.07) * dk / e.y
    m = (sx + sy) / 2 * zk; S = Vector((sx, sy, m))
    front = [v.co for v in o.data.vertices if v.co.y < lo.y + 0.25 * e.y and abs(v.co.x - (lo.x + hi.x) / 2) < 0.15 * e.x]
    ptop = max(v.z for v in front)
    t = Vector(((tlo.x + thi.x) / 2 - sx * (lo.x + hi.x) / 2, (tlo.y + thi.y) / 2 - sy * (lo.y + hi.y) / 2, FIT['neck'].z - 0.05 + lift - m * ptop))
    # how far its back stands off the body's back at the shoulder blades: a backpack goes on top of it (br_models.cpp)
    bk = [t.y + sy * v.co.y for v in o.data.vertices if abs(v.co.x - (lo.x + hi.x) / 2) < 0.15 * e.x and abs(t.z + m * v.co.z - 1.38) < 0.08]
    out = max(0.0, (max(bk) if bk else 0) - FIT['back'])
    print('vest', idn, 'scale', r4(S), 'plate top', round(t.z + m * ptop, 3), 'back out', round(out, 3))
    blk = worn_block('Spine2', t, S); blk['worn']['backOut'] = round(out, 4)
    write_asset(idn, name, 'vest', gl_size(o), blk)

def fit_backpack(o, idn, name, width_k, drop=0.0, gap=-0.01):
    # its body (not the strap loops standing proud of it: the 30th percentile of its depth) against the shirt's back;
    # over a vest the game moves it out by that vest's backOut
    tlo, thi = FIT['torso']; lo, hi = bbox([o]); e = hi - lo
    s = (thi.x - tlo.x) * width_k / e.x; S = Vector((s, s, s))
    ys = sorted(v.co.y for v in o.data.vertices); front = ys[int(0.30 * len(ys))]
    top = FIT['neck'].z + 0.02 - drop
    t = Vector(((tlo.x + thi.x) / 2 - s * (lo.x + hi.x) / 2, FIT['back'] + gap - s * front, top - s * hi.z))
    write_asset(idn, name, 'backpack', gl_size(o), worn_block('Spine2', t, S))

def item(idn, path, keep=None, drop=(), rotz=0, rotx=0, roty=0, tris_max=8000, tex=1024, mats=None):
    reset(); ms = imp(path, keep, drop)
    if mats is not None:
        for mo in ms: set_materials(mo, mats(mo))
    o = join(ms)
    if rotz: xform(o, Matrix.Rotation(math.radians(rotz), 4, 'Z'))
    if rotx: xform(o, Matrix.Rotation(math.radians(rotx), 4, 'X'))
    if roty: xform(o, Matrix.Rotation(math.radians(roty), 4, 'Y'))
    decimate(o, tris_max)
    return o

def want(i): return not ONLY or i in ONLY

def bake_gear():
    if want('br_helmet_1') or want('br_vest_1'):
        o = item('br_helmet_1', J('Level1HelmetArmor', 'Level1HelmetArmor.glb'), keep=['Object_13'], rotz=90, rotx=15, tris_max=6000)   # its goggles side is its front; tipped forward: an open face starting high
        drop_front_parts(o)   # the goggles: the face stays clear
        place_ground(o); export(o, 'br_helmet_1', 1024); fit_helmet(o, 'br_helmet_1', 'Helmet (level 1)', kw=1.25, gap=0.025, back=-0.03)   # goggles up on the brow
        o = item('br_vest_1', J('Level1HelmetArmor', 'Level1HelmetArmor.glb'), keep=['Object_7'], roty=-90, tris_max=6000)
        place_ground(o); export(o, 'br_vest_1', 1024); fit_vest(o, 'br_vest_1', 'Vest (level 1)')
    if want('br_helmet_2'):
        o = item('br_helmet_2', J('Level2Helmet', 'Level2Helmet.glb'), rotz=90, tris_max=6000)
        place_ground(o); size_to(o, 0, 0.26); place_ground(o); export(o, 'br_helmet_2', 1024); fit_helmet(o, 'br_helmet_2', 'Helmet (level 2)')
    if want('br_helmet_3'):
        o = item('br_helmet_3', J('Level3Helmet', 'source', 'Untitled.glb'), rotz=-90, tris_max=6000)
        place_ground(o); size_to(o, 0, 0.27); place_ground(o); export(o, 'br_helmet_3', 1024); fit_helmet(o, 'br_helmet_3', 'Helmet (level 3)', gap=0.04, back=0.012)
    if want('br_vest_2'):
        o = item('br_vest_2', J('Level2Armor', 'Level2Armor.glb'), tris_max=8000)
        place_ground(o); export(o, 'br_vest_2', 1024); fit_vest(o, 'br_vest_2', 'Vest (level 2)')
    if want('br_vest_3'):
        o = item('br_vest_3', J('Level3Armor', 'Level3Armor.glb'), tris_max=8000)
        place_ground(o); size_to(o, 0, 0.5); place_ground(o); export(o, 'br_vest_3', 1024); fit_vest(o, 'br_vest_3', 'Vest (level 3)')
    if want('br_backpack'):
        o = item('br_backpack', J('regBackpack', 'source', 'unzipped', 'scene.gltf'), rotz=-90, tris_max=10000)
        place_ground(o); size_to(o, 0, 0.34); place_ground(o); export(o, 'br_backpack', 1024); fit_backpack(o, 'br_backpack', 'Backpack', 0.95, 0.12)
    if want('br_backpack_mil'):
        o = item('br_backpack_mil', J('military_backpack', 'military_backpack.glb'), rotz=180, tris_max=12000)
        place_ground(o); export(o, 'br_backpack_mil', 1024); fit_backpack(o, 'br_backpack_mil', 'Military backpack', 0.9)

# ---------------------------------------------------------------- optics: along the weapon axis, mount base at y = 0
def rings(o, n_at=(0.3, 0.7)):
    # scope rings + bases down to the rail (the 4x / 10x sources are bare tubes)
    lo, hi = bbox([o]); L = hi.y - lo.y
    zc = eye_height(o)   # the tube axis (turrets and a laser box stand off it)
    r = min(zc - min(v.co.z for v in o.data.vertices if abs(v.co.y - (lo.y + f * L)) < 0.03 * L and v.co.z < zc) for f in n_at)
    zb = zc - r
    xform(o, Matrix.Translation((0, 0, -zb + 0.018)))   # 18 mm of base under the tube
    zc += -zb + 0.018
    steel = mat_col('ring_steel', (0.05, 0.05, 0.05), 0.45, 0.6)
    parts = [o]
    for f in n_at:
        y = lo.y + f * L
        bpy.ops.mesh.primitive_torus_add(major_radius=r + 0.003, minor_radius=0.0045, location=(0, y, zc), rotation=(math.pi / 2, 0, 0), major_segments=24, minor_segments=6)
        t = bpy.context.active_object; bpy.ops.transform.resize(value=(1, 3.0, 1)); parts.append(t)
        bpy.ops.mesh.primitive_cube_add(size=1, location=(0, y, (zc - r) / 2)); c = bpy.context.active_object
        c.scale = (r * 1.4, 0.022, max(0.004, zc - r)); parts.append(c)
        bpy.ops.mesh.primitive_cube_add(size=1, location=(0, y, 0.004)); c = bpy.context.active_object
        c.scale = (0.026, 0.026, 0.008); parts.append(c)
    for p in parts[1:]: set_materials(p, [steel])
    return join(parts)

def clear_glass(o, ez, probe=0.004):
    # the lenses go (the game draws no glass, so an opaque disc would block the view through the optic): every
    # surface the sight line meets (rays along it, within `probe` of the axis), grown over neighbouring faces that
    # face along the axis and lie in the same lens (a curved lens drifts a little in y; the housing steps away)
    import bmesh
    from mathutils.bvhtree import BVHTree
    bm = bmesh.new(); bm.from_mesh(o.data); bm.faces.ensure_lookup_table()
    tree = BVHTree.FromBMesh(bm)
    lo, hi = bbox([o]); seeds = set()
    for dx, dz in [(0, 0)] + [(probe * math.cos(a), probe * math.sin(a)) for a in [k * math.pi / 3 for k in range(6)]]:
        p = Vector((dx, lo.y - 0.01, ez + dz))
        while True:
            hit, n, fi, d = tree.ray_cast(p, Vector((0, 1, 0)))
            if hit is None: break
            seeds.add(fi); p = hit + Vector((0, 1e-4, 0))
    kill = set()
    for s in seeds:
        f0 = bm.faces[s]
        if abs(f0.normal.y) < 0.5: continue
        y0 = f0.calc_center_median().y; stack = [f0]; kill.add(f0)
        while stack:
            f = stack.pop()
            for e in f.edges:
                for g in e.link_faces:
                    if g in kill or abs(g.normal.y) < 0.5: continue
                    c = g.calc_center_median()
                    if abs(c.y - y0) > 0.15 * math.hypot(c.x, c.z - ez) + 0.0015: continue   # off the lens's curve: the housing
                    kill.add(g); stack.append(g)
    bmesh.ops.delete(bm, geom=list(kill), context='FACES'); bm.to_mesh(o.data); bm.free()
    print('glass cut:', len(seeds), 'surfaces hit,', len(kill), 'faces')

def eye_height(o, rear_frac=0.1):   # optical axis: centre of the rear (eyepiece) end's cross-section
    lo, hi = bbox([o]); L = hi.y - lo.y
    vs = [v.co for v in o.data.vertices if v.co.y > hi.y - rear_frac * L]
    return (min(v.z for v in vs) + max(v.z for v in vs)) / 2

def sight_block(o, eye_z=None, frac=0.1):
    lo, hi = bbox([o]); ez = eye_height(o, frac) if eye_z is None else eye_z
    return {'sight': {'eye': r4((0, ez, -hi.y)), 'length': round(hi.y - lo.y, 4)}}   # glTF: eye end at -z

def bake_sights():
    if want('br_sight_reddot'):
        tx = J('red-dot', 'source', 'unzipped', 'model', 'textures')
        def m(mo): return [mat_tex('rd_body', os.path.join(tx, 'base_albedo.jpg'), 0.5, 0.3)] if len(mo.data.polygons) > 500 else [mat_col('rd_lens', (0.55, 0.25, 0.12), 0.05, 0.2)]
        o = item('br_sight_reddot', J('red-dot', 'source', 'unzipped', 'model', 'model.dae'), mats=m, tris_max=5000)
        long_to_y(o); big_end_forward(o); place_ground(o); size_to(o, 1, 0.085); place_ground(o)
        # the lens: its centre is the eye line; it goes (the game draws no glass: an opaque disc would hide the view)
        li = [i for i, s in enumerate(o.material_slots) if s.material and s.material.name.startswith('rd_lens')]
        lz = [o.data.vertices[v].co.z for p in o.data.polygons if p.material_index in li for v in p.vertices]
        ez = (min(lz) + max(lz)) / 2
        import bmesh
        bm = bmesh.new(); bm.from_mesh(o.data)
        bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.material_index in li], context='FACES'); bm.to_mesh(o.data); bm.free()
        export(o, 'br_sight_reddot', 512)
        write_asset('br_sight_reddot', 'Red dot sight', 'sight', gl_size(o), sight_block(o, eye_z=ez))
    if want('br_sight_pistol'):   # PS1: a pistol red dot (the user's vortex_viper_red_dot.glb, Sketchfab; LIC1: epavlenko, CC-BY-4.0 per its GLB, the bake is committed):
        # no mount adapter (it sits on the slide), ~46 mm long; the lens' centre is the eye line, the lens itself goes (no glass)
        o = item('br_sight_pistol', J('ps1', 'viper', 'vortex_viper_red_dot.glb'), drop=('adapter',), tris_max=2500)
        long_to_y(o); place_ground(o); size_to(o, 1, 0.046); place_ground(o)
        li = [i for i, s in enumerate(o.material_slots) if s.material and s.material.name.startswith('glass')]
        lz = [o.data.vertices[v].co.z for p in o.data.polygons if p.material_index in li for v in p.vertices]
        ly = [o.data.vertices[v].co.y for p in o.data.polygons if p.material_index in li for v in p.vertices]
        lo_, hi_ = bbox([o])
        if ly and (sum(ly) / len(ly)) > (lo_.y + hi_.y) / 2: xform(o, Matrix.Rotation(math.pi, 4, 'Z')); lz = None   # the lens (objective) toward Blender -y (glTF +z)
        if lz is None:
            lz = [o.data.vertices[v].co.z for p in o.data.polygons if p.material_index in li for v in p.vertices]
        ez = (min(lz) + max(lz)) / 2
        import bmesh
        bm = bmesh.new(); bm.from_mesh(o.data)
        bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.material_index in li], context='FACES'); bm.to_mesh(o.data); bm.free()
        export(o, 'br_sight_pistol', 512)
        write_asset('br_sight_pistol', 'Pistol red dot', 'sight', gl_size(o), sight_block(o, eye_z=ez))
        print('pistol dot: eye z', round(ez, 4), 'size', [round(v, 4) for v in gl_size(o)])
    if want('br_sight_holo'):   # AT1: a compact hooded holographic sight (source: <br_assets>/at1, an FBX + its texture sets; licence unknown: local-only)
        tx = J('at1', 'textures')
        def m(mo):
            n = mo.material_slots[0].material.name if mo.material_slots and mo.material_slots[0].material else ''
            if n.startswith('Holo'): return [mat_col('holo_plate', (0.1, 0.1, 0.1), 0.1, 0.0)]   # the reticle plate (its own image): cut below; the game draws the reticle
            if n.startswith('Bolts'): return [mat_tex('holo_bolts', os.path.join(tx, 'Boltsnstuff_Base_color.png'), 0.45, 0.6)]
            return [mat_tex('holo_body', os.path.join(tx, 'Main_Base_color.png'), 0.55, 0.35)]
        o = item('br_sight_holo', J('at1', 'source', 'exps3_done.fbx'), mats=m, tris_max=6000)
        long_to_y(o); big_end_forward(o); place_ground(o); size_to(o, 1, 0.096); place_ground(o)
        li = [i for i, s in enumerate(o.material_slots) if s.material and s.material.name.startswith('holo_plate')]
        pz = [o.data.vertices[v].co.z for p in o.data.polygons if p.material_index in li for v in p.vertices]
        py = [o.data.vertices[v].co.y for p in o.data.polygons if p.material_index in li for v in p.vertices]
        ez = (min(pz) + max(pz)) / 2 if pz else None   # the window's centre height: the eye line
        px_ = [o.data.vertices[v].co.x for p in o.data.polygons if p.material_index in li for v in p.vertices]
        # AT1: the model's own holographic reticle: the plate's rectangle (glTF: x, y = Blender z, z = -Blender y) and its image
        # (copied beside the local bake, 256 px); the game draws that image's lit texels as emissive quads on the plate
        reticle = {'image': 'reticle.png', 'center': r4(((min(px_) + max(px_)) / 2, ez, -(min(py) + max(py)) / 2)), 'size': r4((max(px_) - min(px_), max(pz) - min(pz)))} if pz else None
        rimg = bpy.data.images.load(os.path.join(tx, 'EotechEXPS.png')); rimg.scale(256, 256)
        # beside the GLB: after --import-weapon-models, copy <glb out>/br/br_sight_holo_reticle.png to content/assets/models/br_sight_holo/reticle.png
        rimg.filepath_raw = os.path.join(OUT, 'br', 'br_sight_holo_reticle.png'); rimg.file_format = 'PNG'; rimg.save()
        print('holo plate: z', round(min(pz), 4) if pz else None, '..', round(max(pz), 4) if pz else None, 'y', round(min(py), 4) if py else None)
        import bmesh
        bm = bmesh.new(); bm.from_mesh(o.data)
        bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.material_index in li], context='FACES'); bm.to_mesh(o.data); bm.free()
        if ez is not None: clear_glass(o, ez, probe=0.008)   # the front and rear window panes (the game draws no glass: they would block the view)
        export(o, 'br_sight_holo', 512)
        write_asset('br_sight_holo', 'Holographic sight', 'sight', gl_size(o), sight_block(o, eye_z=ez))
        # the committed stand-in is the red dot's (an open frame, not a solid box that would block the view): its parts and eye
        # point; the real bake's eye point is kept as "modelSight" (used when the local model is loaded)
        p = os.path.join(CONTENT, 'assets', 'br_sight_holo.json'); hj = json.load(open(p, encoding='utf-8'))
        rj = json.load(open(os.path.join(CONTENT, 'assets', 'br_sight_reddot.json'), encoding='utf-8'))
        hj['modelSight'] = hj['sight']; hj['sight'] = rj['sight']
        if reticle: hj['modelSight']['reticle'] = reticle; hj['modelSight']['ownReticle'] = True   # the model's own reticle is the only one drawn
        for k in ('parts', 'materials', 'footprint'): hj[k] = rj[k]
        hj['description'] = "AT1 Battle Royale optic. Stand-in: the red dot sight's part-built frame; the real model is local-only (licence unknown): bake it with tools/bake_br_assets.py."
        json.dump(hj, open(p, 'w', encoding='utf-8'), indent=1)
    if want('br_sight_acog'):
        tx = J('acog-scope', 'textures')
        o = item('br_sight_acog', J('acog-scope', 'source', 'unzipped', 'ACOG Scope.obj'), tris_max=6000,
                 mats=lambda mo: [mat_tex('acog', os.path.join(tx, 'ACOG_Scope_ACOG_Scope_BaseColor.png'), 0.5, 0.3)])
        long_to_y(o); big_end_forward(o)   # AT7: the flared housing (the red fibre on top) is the objective: forward (it was turned back)
        place_ground(o); size_to(o, 1, 0.15); place_ground(o)
        ez = eye_height(o); clear_glass(o, ez)   # the eye: the round ocular's centre (no mount under the rear end now)
        export(o, 'br_sight_acog', 512); write_asset('br_sight_acog', 'ACOG', 'sight', gl_size(o), sight_block(o, eye_z=ez))
    if want('br_sight_4x'):
        o = item('br_sight_4x', J('4x', 'source', 'm24Sniper.fbx'), keep=['Scope'], tris_max=6000,
                 mats=lambda mo: [mat_col('glass', (0.1, 0.14, 0.18), 0.05, 0.5)] if 'Glass' in mo.name else [mat_col('scope_black', (0.04, 0.04, 0.045), 0.4, 0.4)])
        long_to_y(o); big_end_forward(o); place_ground(o); size_to(o, 1, 0.30); place_ground(o)
        o = rings(o); place_ground(o)
        clear_glass(o, eye_height(o))
        export(o, 'br_sight_4x', 512); write_asset('br_sight_4x', '4x scope', 'sight', gl_size(o), sight_block(o))
    if want('br_sight_thermal'):   # TH1: the supplied rifle scope (rifle-scope.zip, licence unknown: local-only); NMK_TH1_SRC: its extracted folder
        base = os.environ.get('NMK_TH1_SRC') or J('rifle-scope')
        o = item('br_sight_thermal', os.path.join(base, 'source', 'scope.blend'), tris_max=8000,
                 mats=lambda mo: [mat_tex('thermal_scope', os.path.join(base, 'textures', 'p3dm_ru_CZ-LM4_1P_Diff.png'), 0.55, 0.25)])
        long_to_y(o); big_end_forward(o); place_ground(o); size_to(o, 1, 0.30); place_ground(o)
        o = rings(o); place_ground(o)
        clear_glass(o, eye_height(o))
        export(o, 'br_sight_thermal', 512); write_asset('br_sight_thermal', 'Thermal scope', 'sight', gl_size(o), sight_block(o))
        # the committed stand-in: the 4x scope's part-built tube (drawn when the local bake is missing); the bake's eye point kept as "modelSight"
        p = os.path.join(CONTENT, 'assets', 'br_sight_thermal.json'); tj = json.load(open(p, encoding='utf-8'))
        fj = json.load(open(os.path.join(CONTENT, 'assets', 'br_sight_4x.json'), encoding='utf-8'))
        tj['modelSight'] = tj['sight']; tj['sight'] = fj['sight']
        tj['modelSight']['scale'] = 0.8   # AT7: drawn 24 cm long (a clip-on thermal's size; the 30 cm bake read as half the AK's length)
        for k in ('parts', 'materials', 'footprint'): tj[k] = fj[k]
        tj['description'] = "TH1 Battle Royale optic: a thermal scope (loot.json br_sight_thermal). Stand-in: the 4x scope's part-built tube; the real model is local-only (licence unknown): bake it with tools/bake_br_assets.py (NMK_TH1_SRC)."
        json.dump(tj, open(p, 'w', encoding='utf-8'), indent=1)
    if want('br_sight_10x'):
        o = item('br_sight_10x', J('10x', '10x .glb'), tris_max=8000)
        long_to_y(o); big_end_forward(o); place_ground(o); size_to(o, 1, 0.36); place_ground(o)
        o = rings(o); place_ground(o)
        clear_glass(o, eye_height(o))
        export(o, 'br_sight_10x', 512); write_asset('br_sight_10x', '10x scope', 'sight', gl_size(o), sight_block(o))

# ---------------------------------------------------------------- other attachments and world items (pick-up meshes)
def recoil_pad():
    # after the reference picture: an AR-style buttstock (buffer tube, stock body with a sling slot, a lever below); front +z glTF
    blk = mat_col('stock_black', (0.035, 0.035, 0.035), 0.75)
    parts = []
    def cube(c, s):
        bpy.ops.mesh.primitive_cube_add(size=1, location=c); o = bpy.context.active_object; o.scale = s; parts.append(o)
    bpy.ops.mesh.primitive_cylinder_add(radius=0.019, depth=0.17, location=(0, -0.02, 0.085), rotation=(math.pi / 2, 0, 0), vertices=20)
    parts.append(bpy.context.active_object)                              # buffer tube, forward end at -y
    cube((0, 0.035, 0.083), (0.036, 0.11, 0.045))                        # upper body around the tube
    cube((0, 0.072, 0.035), (0.032, 0.035, 0.07))                        # butt plate
    cube((0, 0.03, 0.045), (0.03, 0.05, 0.02))                           # lower web
    cube((0, 0.088, 0.09), (0.036, 0.012, 0.06))                         # sling loop block
    cube((0, -0.03, 0.05), (0.012, 0.012, 0.04))                         # adjustment lever
    for p in parts: set_materials(p, [blk])
    o = join(parts)
    # the bevelled lower corner: a slanted prism instead of the butt's square bottom
    return o

def bake_attachments():
    if want('br_recoil_pad'):
        reset(); o = recoil_pad(); place_ground(o); export(o, 'br_recoil_pad', 256)
        write_asset('br_recoil_pad', 'Recoil pad', 'att', gl_size(o))
    if want('br_suppressor'):
        tx = J('supressor', 'source', 'unzipped', 'model', 'textures')
        o = item('br_suppressor', J('supressor', 'source', 'unzipped', 'model', 'model.dae'), tris_max=4000,
                 mats=lambda mo: [mat_tex('supp', os.path.join(tx, '03___Default_1001_albedo.jpg'), 0.5, 0.4)])
        long_to_y(o); place_ground(o); size_to(o, 1, 0.17); place_ground(o)
        export(o, 'br_suppressor', 512); write_asset('br_suppressor', 'Suppressor', 'att', gl_size(o))
    if want('br_muzzle_brake'):
        tx = J('ak-762x39-muzzle-break', 'textures')
        o = item('br_muzzle_brake', J('ak-762x39-muzzle-break', 'source', 'Muzzle Break 7_62x39_low.fbx'), tris_max=3000,
                 mats=lambda mo: [mat_tex('brake', os.path.join(tx, 'Muzzle_Break_7.62x39_lp_AK_Muzzle_Break_76.png'), 0.5, 0.5)])
        long_to_y(o); place_ground(o); size_to(o, 1, 0.08); place_ground(o)
        export(o, 'br_muzzle_brake', 512); write_asset('br_muzzle_brake', 'Muzzle brake', 'att', gl_size(o))
    mags = []
    if want('br_mag_ext_pistol'): mags.append(('br_mag_ext_pistol', 'Extended magazine (pistol)', J('e-11_magazine', 'e-11_magazine.glb'), None, (), None, 0.17))
    if want('br_mag_ext_smg'):
        tx = J('magazine-submachine-gun-sten-mk-ii', 'textures')
        mags.append(('br_mag_ext_smg', 'Extended magazine (SMG)', J('magazine-submachine-gun-sten-mk-ii', 'source', 'MAGAZYNEK UPLOAD V2.blend'), ['Cube.0', 'Sphere', 'Cylinder.004'], ('Circle',),
                     lambda mo: [mat_tex('sten', os.path.join(tx, 'TexturesCom_Metal_PaintedGun_1K_albedo1.png'), 0.5, 0.5)], 0.30))
    tx = J('magpul-p-mag-style-ar-magazine', 'textures')
    pmag = lambda mo: [mat_tex('pmag', os.path.join(tx, 'Magpul_Pmag_30_MP_30_BaseColor.png'), 0.7)]
    for idn, nm, L in (('br_mag_ext', 'Extended magazine', 0.26), ('br_mag_ext_rifle', 'Extended magazine (rifle)', 0.26), ('br_mag_ext_sniper', 'Extended magazine (sniper)', 0.22)):
        if want(idn): mags.append((idn, nm, J('magpul-p-mag-style-ar-magazine', 'source', 'Magpul Pmag 30.blend'), None, (), pmag, L))
    for idn, nm, path, keep, drop, mats, L in mags:
        o = item(idn, path, keep=keep, drop=drop, tris_max=4000, mats=mats)
        long_to_y(o); place_ground(o); size_to(o, 1, L); place_ground(o)
        export(o, idn, 512); write_asset(idn, nm, 'att', gl_size(o))

def bake_world():
    if want('br_molotov'):
        tx = J('l4d2-molotov-remake', 'textures')
        o = item('br_molotov', J('l4d2-molotov-remake', 'source', 'MolotovLP.obj'), tris_max=4000,
                 mats=lambda mo: [mat_tex('molotov', os.path.join(tx, 'albedo.tga.png'), 0.35)])
        place_ground(o); size_to(o, 2, 0.26); place_ground(o)
        export(o, 'br_molotov', 512); write_asset('br_molotov', 'Molotov', 'att', gl_size(o))
    if want('br_parachute'):
        o = item('br_parachute', J('parachute_simple', 'parachute_simple.glb'), tris_max=3000)
        place_ground(o); size_to(o, 0, 7.0); place_ground(o)
        export(o, 'br_parachute', 512); write_asset('br_parachute', 'Parachute', 'world', gl_size(o))
    if want('br_bomber'):
        cols = {'Hull': (0.30, 0.33, 0.24), 'Wing': (0.28, 0.31, 0.22), 'Detail': (0.12, 0.12, 0.12)}
        o = item('br_bomber', J('light-carpet-bomber', 'source', 'Bomber.fbx'), tris_max=14000,
                 mats=lambda mo: [mat_col('bomber_' + k, c, 0.6, 0.2) for k, c in cols.items() if k in mo.name][:1] or [mat_col('bomber', (0.3, 0.32, 0.24))])
        place_ground(o); size_to(o, 0, 22.0); place_ground(o)
        export(o, 'br_bomber', 1024); write_asset('br_bomber', 'Bomber', 'world', gl_size(o))

if not ONLY or ONLY & {'br_marine'} or any(i.startswith(('br_helmet', 'br_vest', 'br_backpack')) for i in ONLY):
    bake_marine()   # the gear is fitted to it
bake_gear(); bake_sights(); bake_attachments(); bake_world()
