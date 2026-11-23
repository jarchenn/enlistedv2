# EK1 (Blender background): the user's animated elk as the mount for Knight 10, with its own motion and barding after the user's
# reference picture. The elk's takes become the mounted knight's clips, each as long as the rider's clip of that name
# (walk / run / sprint looped, the hips' travel taken off; the standing breath for the clips where the mount stands; the standing
# death stretched to the rider's horse_death), so the rider's own motion is kept and only his seat moves (elk_rider.py).
# The barding: moose_armour_pieces.py (the project's procedural pieces, laid out after the reference) fitted by rays from the
# elk's joints to its body in idle frame 1, each piece weight 1 to its joint, taken back to the rest pose.
#   blender -b --factory-startup --disable-autoexec --python elk_build.py -- <Elk Animated.fbx> <textures dir> <out.glb> <seat.json>
#   env PREVIEW=<dir>: renders from the rig before export
import bpy, sys, os, math, json
import numpy as np
from mathutils import Vector, Matrix
A = sys.argv[sys.argv.index('--') + 1:]; FBX, TEXD, OUT, SEATJ = A[:4]
HERE = os.path.dirname(os.path.abspath(__file__))
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=FBX)
sc = bpy.context.scene; FPS = sc.render.fps
arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
body = next(o for o in bpy.data.objects if o.type == 'MESH' and any(m and m.name == 'Body' for m in o.data.materials))
antl = next((o for o in bpy.data.objects if o.type == 'MESH' and o is not body), None)
for im in bpy.data.images:   # the textures beside the FBX (the file points at the author's Unity folder)
    if im.filepath: im.filepath = os.path.join(TEXD, os.path.basename(im.filepath.replace('\\', '/'))); im.reload()
for im in bpy.data.images: print('IMG', im.name, tuple(im.size), os.path.basename(im.filepath))
if antl and not antl.find_armature():   # the antlers: rigid on the head
    mw = antl.matrix_world.copy(); antl.parent = arm; antl.parent_type = 'OBJECT'; antl.matrix_world = mw
    vg = antl.vertex_groups.new(name='RigHead'); vg.add(list(range(len(antl.data.vertices))), 1.0, 'REPLACE')
    md = antl.modifiers.new('Armature', 'ARMATURE'); md.object = arm
print('ELK body', body.name, len(body.data.vertices), 'antlers', antl.name if antl else None, 'skinned' if antl and antl.find_armature() else '')
# ---- clips: each as long as the rider's (seconds); the elk take, how
CLIPS = [('idle', 'Stand_Breathing_01', 3.0, 'cut'), ('walk', 'Walk', 3.0, 'cycle'), ('gallop_scan', 'Run', 1.958, 'cycle'),
         ('attack_charge', 'Sprint', 1.958, 'cycle'), ('attack_right', 'Stand_Breathing_01', 3.542, 'cut'),
         ('attack_pivot_left', 'Run', 4.125, 'cycle'), ('rider_death', 'Stand_Breathing_01', 1.375, 'cut'),
         ('rider_death_left', 'Stand_Breathing_01', 1.375, 'cut'), ('horse_death', 'Death_Stand_R01', 3.083, 'stretch')]
takes = {a.name.split('|')[1]: a for a in bpy.data.actions if a.name.startswith(arm.name + '|')}
ad = arm.animation_data or arm.animation_data_create()
pel = arm.pose.bones['RigPelvis']; AW0 = arm.matrix_world.copy(); AW = AW0   # the takes also move the armature object (root travel): folded into the pelvis
def use(a):
    ad.action = a
    if a.slots: ad.action_slot = a.slots[0]
def at(f):
    fi = int(math.floor(f)); sc.frame_set(fi, subframe=f - fi); bpy.context.view_layer.update()
new = {}
for name, take, dur, how in CLIPS:
    src = takes[take]; f0, f1 = map(int, src.frame_range); L = f1 - f0; n = int(round(dur * FPS)) + 1
    use(src); at(f0); p0 = np.array(arm.matrix_world @ pel.head); at(f1); D = np.array(arm.matrix_world @ pel.head) - p0; D[2] = 0   # one cycle's travel (horizontal)
    HOLD = name in ('walk',)   # EK6 (user): the rider must not slide back and forth: the body's surge within the stride held too (fore-aft only)
    sp2 = arm.pose.bones['RigSpine2']
    def saddle_y():   # the saddle's seat point (a point fixed on the back over the withers' spine joint), world fore-aft
        return (AW0 @ (sp2.matrix @ LSEAT)).y
    if HOLD:
        use(takes['Stand_Breathing_01']); at(takes['Stand_Breathing_01'].frame_range[0])
        LSEAT = sp2.matrix.inverted() @ (AW0.inverted() @ Vector((0.026, 0.228, 1.482)))   # shortcut: the seat from the last build (elk_seat.json); rebuild if the saddle moves
        ys = []
        for s_ in range(f0, f1):
            use(src); at(s_); AWt = arm.matrix_world.copy()
            if any(abs(x) > 1e-6 for r in (AWt - AW0) for x in r): pel.matrix = AW0.inverted() @ AWt @ pel.matrix; bpy.context.view_layer.update()
            ys.append(saddle_y())
        Ybase = float(np.mean(ys))
    frames = []
    for k in range(n):
        if how == 'cycle': s = f0 + (k % L)
        elif how == 'stretch': s = f0 + k * L / (n - 1)
        else: s = min(f0 + k, f1)
        use(src); at(s)
        AWt = arm.matrix_world.copy()
        if any(abs(x) > 1e-6 for r in (AWt - AW0) for x in r):
            pel.matrix = AW0.inverted() @ AWt @ pel.matrix; bpy.context.view_layer.update()
        basis = {pb.name: pb.matrix_basis.copy() for pb in arm.pose.bones}
        if how == 'cycle':   # in place: the cycle's travel taken off, linearly
            off = Vector(D * ((s - f0) / L))
            if HOLD: off.y = saddle_y() - Ybase
            pm = pel.matrix.copy(); pm.translation -= AW.inverted().to_3x3() @ off
            pel.matrix = pm; bpy.context.view_layer.update(); basis['RigPelvis'] = pel.matrix_basis.copy()
        frames.append(basis)
    act = bpy.data.actions.new(name); act.use_fake_user = True; use(act)
    for k, basis in enumerate(frames):
        for pb in arm.pose.bones:
            pb.rotation_mode = 'QUATERNION'; pb.matrix_basis = basis[pb.name]
            for path in ('location', 'rotation_quaternion', 'scale'): pb.keyframe_insert(path, frame=k + 1)
    arm.matrix_world = AW0; bpy.context.view_layer.update()
    new[name] = act; print('CLIP %-18s <- %-20s %s, %d frames' % (name, take, how, n))
for a in list(bpy.data.actions):
    if a not in new.values(): bpy.data.actions.remove(a)
for o in list(bpy.data.objects):
    if o.type == 'EMPTY':
        for c in o.children: mw = c.matrix_world.copy(); c.parent = None; c.matrix_world = mw
        bpy.data.objects.remove(o, do_unlink=True)
arm.matrix_world = AW0; AW = arm.matrix_world
# ---- the barding, fitted in idle frame 1
import bmesh
from mathutils.bvhtree import BVHTree
use(new['idle']); at(1)
dg = bpy.context.evaluated_depsgraph_get(); eb = body.evaluated_get(dg); me_ = eb.to_mesh()
bm0 = bmesh.new(); bm0.from_mesh(me_); bm0.transform(eb.matrix_world); eb.to_mesh_clear(); BVH = BVHTree.FromBMesh(bm0)
BODYP = [v.co.copy() for v in bm0.verts]   # the body in idle frame 1 (world), for the weight transfer
antl_P = []
if antl:
    ea_ = antl.evaluated_get(dg); am_ = ea_.to_mesh(); antl_P = [ea_.matrix_world @ v.co for v in am_.vertices]; ea_.to_mesh_clear()
base_z = min(p.z for p in antl_P) if antl_P else 9
ANTLER_Y = -0.97
if antl_P:   # the back edge of the antlers' bases: near each side's lowest point
    for sd in (1, -1):
        Q = [p for p in antl_P if p.x * sd > 0]; lo = min(Q, key=lambda p: p.z)
        ANTLER_Y = max([ANTLER_Y] + [p.y for p in Q if p.z < lo.z + 0.06 and (p.xy - lo.xy).length < 0.1])
EARS = {}
for S_, bn in (('L', 'RigLEar1'), ('R', 'RigREar1')):   # each ear: its bone's head to its farthest vertex
    if bn not in body.vertex_groups: continue
    gi = body.vertex_groups[bn].index; root = arm.matrix_world @ arm.pose.bones[bn].head
    pts = [BODYP[v.index] for v in body.data.vertices if any(g.group == gi and g.weight > 0.6 for g in v.groups)]
    if pts: EARS[S_] = (root, max(pts, key=lambda q: (q - root).length))
print('ANTLER_Y %.3f EARS %s' % (ANTLER_Y, {k: [tuple(round(x, 3) for x in v) for v in vv] for k, vv in EARS.items()}))
tip = min((v.co for v in bm0.verts), key=lambda c: c.y).copy(); bm0.free()   # the muzzle: the body's front-most point (the antlers are their own mesh)
def inside(bv, p):   # by crossings: odd along most of four directions
    odd = 0
    for dv in (Vector((1, 0, 0)), Vector((-1, 0, 0)), Vector((0, 0, 1)), Vector((0, 1, 0))):
        o, c = p.copy(), 0
        for _ in range(12):
            h = bv.ray_cast(o, dv, 5.0)
            if h[0] is None: break
            c += 1; o = h[0] + dv * 1e-4
        odd += c % 2
    return odd >= 3
def J(name): return arm.matrix_world @ arm.pose.bones[name].head
def JT(name): return arm.matrix_world @ arm.pose.bones[name].tail
Bn = {'Spine_00': 'RigSpine1', 'Spine_01': 'RigSpine1', 'Spine_02': 'RigSpine2', 'Spine_03': 'RigChest', 'Pelvis': 'RigPelvis',
      'Neck_00': 'RigNeck1', 'Neck_01': 'RigNeck2', 'Neck_02': 'RigNeck3', 'Neck_03': 'RigNeck3', 'Head_00': 'RigHead', 'Head_01': 'RigJaw1',
      'L_Ear_00': 'RigLEar1', 'L_Ear_01': 'RigLEar1', 'R_Ear_00': 'RigREar1', 'R_Ear_01': 'RigREar1',
      'L_UpperArm': 'RigLFLeg1', 'l_Forearm': 'RigLFLeg2', 'L_Hand': 'RigLFLeg3', 'R_UpperArm': 'RigRFLeg1', 'R_Forearm': 'RigRFLeg2', 'R_Hand': 'RigRFLeg3',
      'L_Thing': 'RigLBLeg1', 'L_Calf': 'RigLBLeg2', 'L_HorseLink': 'RigLBLeg3', 'L_Foot': 'RigLBLegAnkle', 'R_Thing': 'RigRBLeg1', 'R_Calf': 'RigRBLeg2',
      'R_HorseLink': 'RigRBLeg3', 'R_Foot': 'RigRBLegAnkle', 'L_Stirrup': 'RigSpine2', 'R_Stirrup': 'RigSpine2', 'Tail_00': 'RigTail1'}
G = {'bpy': bpy, 'bmesh': bmesh, 'math': math, 'os': os, 'Vector': Vector, 'Matrix': Matrix, 'sc': sc, 'arm': arm, 'BVH': BVH, 'J': J, 'JT': JT, 'B': Bn,
     'TEX': os.environ.get('ARMOUR_TEX', 'E:/nmk/horse/tex'), 'KTEX': '', 'HEAD_TIP': tip, 'ANTLER_Y': ANTLER_Y, 'EARS': EARS}
exec(compile(open(os.path.join(HERE, 'moose_armour_pieces.py')).read(), 'moose_armour_pieces', 'exec'), G)
pieces = G['parts']; SEAT = G['SEAT']
if antl:   # EK5 (user): the antlers in the barding's steel, its colour and lustre
    for i in range(len(antl.data.materials)): antl.data.materials[i] = G['M']['steel']
bpy.context.view_layer.update()
seat_bone = 'RigSpine2'
json.dump({'seat_blender': list(SEAT), 'seat_gltf': [SEAT.x, SEAT.z, -SEAT.y], 'seat_bone': seat_bone, 'clip': 'idle', 'time': 0.0}, open(SEATJ, 'w'), indent=1)
# EK4 push-out at the fitted pose: no armour vertex under the skin or within GAP of it; each piece moves as a whole field (a vertex
# takes the largest push within 4 cm on its own piece), so plates keep their shape and thickness
GAP, NEAR, CAP = 0.004, 0.1, 0.025   # CAP per pass: a fold the skin makes cannot be inflated away (trim the plate instead)
def sdist(bv, p):   # signed distance to the skin by the nearest face's normal (None when far from it)
    loc, nrm, fi, dist = bv.find_nearest(p, NEAR)
    return None if loc is None else ((p - loc).dot(nrm), nrm)
from mathutils.kdtree import KDTree
def spread(P, need, r=0.04):   # need: {index: Vector}; each point takes the largest push within r
    if not need: return {}
    kd_ = KDTree(len(need)); keys = list(need)
    for n_, i in enumerate(keys): kd_.insert(P[i], n_)
    kd_.balance(); out = {}
    for i, p in enumerate(P):
        best = max((need[keys[n_]] for co, n_, dd in kd_.find_range(p, r)), key=lambda v: v.length, default=None)
        if best is not None: out[i] = best
    return out
moved = 0
for o, b_ in pieces:
    mw = o.matrix_world; P = [mw @ v.co for v in o.data.vertices]; need = {}
    for i, p in enumerate(P):
        sd = sdist(BVH, p)
        if sd and sd[0] < GAP: need[i] = sd[1] * min(GAP - sd[0], CAP)
    for i, dv in spread(P, need).items(): o.data.vertices[i].co = mw.inverted() @ (P[i] + dv); moved += 1
print('PUSHOUT fit: %d vertices moved' % moved)
# EK4: each armour vertex takes the body's own bone weights where it sits (the nearest skin, inverse-distance over 8 vertices),
# so plates bend with the skin they cover instead of riding one bone; ear guards and stirrups stay rigid. The lower legs,
# jaw, tongue and eyelids are left out (no plate is on them). Taken back to rest by the inverse of the blended bone transforms.
from mathutils.kdtree import KDTree
SKIP = ('Leg3', 'Ankle', 'Tongue', 'Eyelid', 'Jaw')
gname = {g.index: g.name for g in body.vertex_groups}
BW = []
for v in body.data.vertices:
    w = {gname[g.group]: g.weight for g in v.groups if g.weight > 0.001 and gname[g.group] in arm.data.bones and not any(k in gname[g.group] for k in SKIP)}
    tot = sum(w.values()); BW.append({k: x / tot for k, x in w.items()} if tot > 0.5 else None)
ok = [i for i, w in enumerate(BW) if w]; kd = KDTree(len(ok))
for n_, i in enumerate(ok): kd.insert(BODYP[i], n_)
kd.balance()
from mathutils.interpolate import poly_3d_calc
bmt = bmesh.new(); bmt.from_mesh(body.data); bmesh.ops.triangulate(bmt, faces=bmt.faces); bmt.verts.ensure_lookup_table(); bmt.faces.ensure_lookup_table()
for v in bmt.verts: v.co = BODYP[v.index]   # the fit pose (idle frame 1), same vertex order as the body
BVHT = BVHTree.FromBMesh(bmt)
def skin_w(p):   # the skin point under p: its triangle's vertex weights, blended barycentrically
    loc, nrm, fi, dist = BVHT.find_nearest(p, 2.0)
    if loc is None: return None
    vs = bmt.faces[fi].verts; bc = poly_3d_calc([v.co for v in vs], loc); acc = {}
    for v, k in zip(vs, bc):
        if BW[v.index] is None: continue
        for b, x in BW[v.index].items(): acc[b] = acc.get(b, 0) + x * k
    return acc or None
Mb = {b.name: (AW @ arm.pose.bones[b.name].matrix) @ (AW @ b.matrix_local).inverted() for b in arm.data.bones}
for o, bone in pieces:
    rigid = any(k in o.get('src_bone', '') for k in ('Stirrup', 'Ear'))   # ear guards: on the ear itself
    me = o.data; mw = o.matrix_world.copy(); groups = {}
    for v in me.vertices:
        p = mw @ v.co
        if rigid: w = {bone: 1.0}
        else:
            acc = skin_w(p) or {}
            for co, n_, dist in (kd.find_n(p, 8) if not acc else ()):
                k = 1.0 / max(dist, 0.01) ** 2
                for b, x in BW[ok[n_]].items(): acc[b] = acc.get(b, 0) + x * k
            top = sorted(acc.items(), key=lambda t: -t[1])[:4]; tot = sum(x for _, x in top); w = {b: x / tot for b, x in top if x / tot > 0.01}
            tot = sum(w.values()); w = {b: x / tot for b, x in w.items()}
        Mblend = Matrix(((0, 0, 0, 0), (0, 0, 0, 0), (0, 0, 0, 0), (0, 0, 0, 0)))
        for b, x in w.items(): Mblend = Mblend + Mb[b] * x
        v.co = Mblend.inverted() @ p
        for b, x in w.items(): groups.setdefault(b, []).append((v.index, x))
    o.matrix_world = Matrix.Identity(4)
    for b, lst in groups.items():
        vg = o.vertex_groups.get(b) or o.vertex_groups.new(name=b)
        for i, x in lst: vg.add([i], x, 'REPLACE')
PINFO = [(o.get('sec'), o.get('src_bone'), o.name) for o, _ in pieces]
SECS = sorted({o.get('sec', '?') for o, _ in pieces})
for o, _ in pieces:
    at_ = o.data.attributes.new('sec', 'INT', 'POINT'); si = SECS.index(o.get('sec', '?')); pa = o.data.attributes.new('pid', 'INT', 'POINT'); pi_ = [x for x, _ in pieces].index(o)
    for k in range(len(o.data.vertices)): at_.data[k].value = si; pa.data[k].value = pi_
bpy.ops.object.select_all(action='DESELECT')
for o, _ in pieces: o.select_set(True)
bpy.context.view_layer.objects.active = pieces[0][0]; bpy.ops.object.join(); armour = bpy.context.view_layer.objects.active; armour.name = 'elk_armour'
armour.parent = arm; armour.matrix_parent_inverse = arm.matrix_world.inverted(); armour.matrix_basis = Matrix.Identity(4)
armour.modifiers.new('Armature', 'ARMATURE').object = arm
# EK4 push-out in motion: every clip, every 2nd frame; each vertex's deepest approach under GAP, pushed out at rest along its
# skin normal (taken to rest by its own blended bone transform), spread over its piece as above; twice
def posed_pass():
    need = {}; me = armour.data
    for nm, act in new.items():
        use(act); f1 = int(act.frame_range[1])
        for f in range(1, f1 + 1, 2):
            at(f); dg = bpy.context.evaluated_depsgraph_get(); eb = body.evaluated_get(dg); bm1 = bmesh.new(); bm1.from_mesh(eb.to_mesh()); bm1.transform(eb.matrix_world); eb.to_mesh_clear()
            bv = BVHTree.FromBMesh(bm1); bm1.free(); ea = armour.evaluated_get(dg); m_ = ea.to_mesh()
            for v in m_.vertices:
                sd = sdist(bv, ea.matrix_world @ v.co)
                if sd and sd[0] < GAP and GAP - sd[0] > need.get(v.index, 0): need[v.index] = min(GAP - sd[0], CAP)
            ea.to_mesh_clear()
    return need
gidx = {g.index: g.name for g in armour.vertex_groups}
for it in range(int(os.environ.get('PUSH_ITERS', '2'))):
    need = posed_pass(); print('PUSHOUT motion pass %d: %d vertices under the gap, deepest %.3f' % (it, len(need), max(need.values(), default=0)))
    if not need: break
    use(new['idle']); at(1); dg = bpy.context.evaluated_depsgraph_get()
    pid = [x.value for x in armour.data.attributes['pid'].data]; vecs = {}
    for i, dep in need.items():
        v = armour.data.vertices[i]; Mbl = Matrix(((0, 0, 0, 0),) * 4)
        for g_ in v.groups: Mbl = Mbl + Mb[gidx[g_.group]] * g_.weight
        sd = sdist(BVH, Mbl @ v.co); nrm = sd[1] if sd else (Mbl @ v.co - Mbl @ Vector((0, 0, 0))).normalized()
        vecs[i] = (Mbl.inverted().to_3x3() @ nrm).normalized() * (dep + 0.002)
    byp = {}
    for i in range(len(armour.data.vertices)): byp.setdefault(pid[i], []).append(i)
    for pi_, idx in byp.items():
        loc = {i: vecs[i] for i in idx if i in vecs}
        if not loc: continue
        P = [armour.data.vertices[i].co.copy() for i in idx]; sp = spread(P, {idx.index(i): v for i, v in loc.items()})
        for k, dv in sp.items(): armour.data.vertices[idx[k]].co += dv
    armour.data.update()
if os.environ.get('CLIPCHK'):   # every clip, every 3rd frame: armour vertices inside the body (by the nearest skin's normal)
    worst_all = 0
    for nm, act in new.items():
        if os.environ['CLIPCHK'] != '1' and nm not in os.environ['CLIPCHK'].split(','): continue
        use(act); f1 = int(act.frame_range[1]); bad = 0; worst = (0, None, None); per = {}
        SEC = [x.value for x in armour.data.attributes['sec'].data]
        PID = [x.value for x in armour.data.attributes['pid'].data]; perp = {}
        for f in range(1, f1 + 1):
            at(f); dg = bpy.context.evaluated_depsgraph_get(); eb = body.evaluated_get(dg); bm1 = bmesh.new(); bm1.from_mesh(eb.to_mesh()); bm1.transform(eb.matrix_world); eb.to_mesh_clear()
            bv = BVHTree.FromBMesh(bm1); bm1.free(); ea = armour.evaluated_get(dg); m_ = ea.to_mesh()
            for v in m_.vertices:
                p = ea.matrix_world @ v.co; sd = sdist(bv, p); dist = -sd[0] if sd else 0
                if sd and sd[0] < -0.0005:
                    pk = PID[v.index]; perp[pk] = max(perp.get(pk, 0), dist)
                    bad += 1; sn = SECS[SEC[v.index]]; c_, dmax = per.get(sn, (0, 0)); per[sn] = (c_ + 1, max(dmax, dist))
                    if dist > worst[0]: worst = (dist, f, tuple(round(x, 3) for x in p))
            ea.to_mesh_clear()
        print('CLIPPIECES', nm, [(k, *PINFO[k], round(x, 3)) for k, x in sorted(perp.items(), key=lambda t: -t[1])[:8]])
        worst_all = max(worst_all, worst[0]); print('CLIPCHK %-18s inside %6d vertex-frames, deepest %.3f m (frame %s at %s)' % (nm, bad, worst[0], worst[1], worst[2]), {k: (c_, round(x, 3)) for k, (c_, x) in sorted(per.items())})
    print('CLIPCHK worst %.3f' % worst_all)
print('ARMOUR %d pieces, %d verts; seat %s' % (len(pieces), len(armour.data.vertices), [round(x, 3) for x in SEAT]))
# ---- previews from the rig
PV = os.environ.get('PREVIEW')
if PV:
    os.makedirs(PV, exist_ok=True)
    sc.render.engine = 'BLENDER_EEVEE'; sc.render.resolution_x = 520; sc.render.resolution_y = 420
    w = bpy.data.worlds.new('w'); sc.world = w; w.use_nodes = True; w.node_tree.nodes['Background'].inputs[0].default_value = (0.55, 0.58, 0.62, 1)
    for ang, en in ((-40, 4.0), (130, 2.0)):
        l = bpy.data.objects.new('sun', bpy.data.lights.new('sun', 'SUN')); l.data.energy = en; sc.collection.objects.link(l); l.rotation_euler = (math.radians(50), 0, math.radians(ang))
    cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam')); sc.collection.objects.link(cam); sc.camera = cam; cam.data.type = 'ORTHO'; cam.data.ortho_scale = float(os.environ.get('PV_SCALE', '3.8'))
    for nm, fr in (('idle', 0.0), ('walk', 0.3), ('gallop_scan', 0.25), ('attack_charge', 0.15), ('horse_death', 0.5), ('horse_death', 1.0)):
        a = new[nm]; use(a); f1 = int(a.frame_range[1]); at(1 + round((f1 - 1) * fr))
        for v, (lo, ro) in {'s': ((7, -0.2, 1.2), (math.radians(90), 0, math.radians(90))), 'q': ((5, -5.2, 2.0), (math.radians(78), 0, math.radians(45)))}.items():
            cam.location = lo; cam.rotation_euler = ro; sc.render.filepath = os.path.join(PV, '%s_%.2f_%s.png' % (nm, fr, v)); bpy.ops.render.render(write_still=True)
ad.action = None; at(1)
bpy.ops.object.select_all(action='DESELECT')
for o in [arm, body, armour] + ([antl] if antl else []): o.select_set(True)
for im in bpy.data.images:
    if im.size[0] > 2048: im.scale(2048, 2048 * im.size[1] // im.size[0])
bpy.ops.export_scene.gltf(filepath=OUT, export_format='GLB', use_selection=True, export_animations=True, export_animation_mode='ACTIONS', export_skins=True, export_yup=True, export_image_format='JPEG', export_jpeg_quality=88)
print('wrote', OUT, [a.name for a in bpy.data.actions])
