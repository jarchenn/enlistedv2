# Hold viewer for the live Blender session (MCP): the Allied game body in its "idle aiming" clip pose, one gun from the game's
# converted models, and the hold exactly as the game applies it (wt-ws1 src/pose.cpp, content/weapons/<id>.json hold.pose).
#   exec(open(".../holdview.py").read());  setup();  show("modern_mp5");  shot("E:/nmk/build-ws1/hv/mp5")
# Spaces: game model / weapon space is x left, y up, z forward (muzzle); Blender is X left, Y back, Z up:  b = (x, -z, y).
import bpy, json, os, math, glob
from mathutils import Vector, Matrix, Quaternion
WT = "E:/nmk/wt-ws1/content/"
MODELS = "E:/nmk/build-guns/models/"
B = Matrix(((1, 0, 0), (0, 0, -1), (0, 1, 0)))          # game -> Blender
BI = B.transposed()
M_ = "mixamorig:"
ST = {}
TWIST = 0.6


def g2b(v): return B @ Vector(v)
def b2g(v): return BI @ Vector(v)
def qg(q): return Quaternion((q[3], q[0], q[1], q[2]))   # xyzw (json) -> mathutils wxyz


def setup(body="allied_body"):
    for o in list(bpy.data.objects): bpy.data.objects.remove(o, do_unlink=True)   # (the MCP add-on blocks a factory reset)
    for a in list(bpy.data.actions): bpy.data.actions.remove(a)
    sc = bpy.context.scene
    bpy.ops.import_scene.gltf(filepath=WT + "characters/%s.glb" % body)
    A = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
    act = next(a for a in bpy.data.actions if a.name.split(".")[0] == "idle aiming")
    A.animation_data_create(); A.animation_data.action = act
    if hasattr(A.animation_data, "action_slot") and act.slots: A.animation_data.action_slot = act.slots[0]
    sc.frame_set(int(act.frame_range[0])); bpy.context.view_layer.update()
    basis = {p.name: p.matrix_basis.copy() for p in A.pose.bones}
    A.animation_data.action = None
    for p in A.pose.bones: p.matrix_basis = basis[p.name]
    bpy.context.view_layer.update()
    ST.update(A=A, basis=basis, sc=sc)
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", 'SUN')); sc.collection.objects.link(sun)
    sun.rotation_euler = (math.radians(50), 0, math.radians(-30)); sun.data.energy = 3.5
    fill = bpy.data.objects.new("fill", bpy.data.lights.new("fill", 'SUN')); sc.collection.objects.link(fill)
    fill.rotation_euler = (math.radians(70), 0, math.radians(150)); fill.data.energy = 1.5
    sc.world = bpy.data.worlds.new("w"); sc.world.color = (0.55, 0.57, 0.6)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam")); sc.collection.objects.link(cam); sc.camera = cam
    sc.render.resolution_x, sc.render.resolution_y = 1000, 800
    ST["cam"] = cam
    return {"bones": len(A.pose.bones), "clip": act.name}


def head(n): A = ST["A"]; return A.matrix_world @ A.pose.bones[M_ + n].head
def rest_rot(pb): A = ST["A"]; return (A.matrix_world @ pb.bone.matrix_local).to_3x3().normalized()


def set_rot(pb, R3):
    """a pose bone's world rotation set to R3, its head staying where it is"""
    A = ST["A"]
    Mw = A.matrix_world @ pb.matrix
    pb.matrix = A.matrix_world.inverted() @ (Matrix.Translation(Mw.translation) @ R3.to_4x4())
    bpy.context.view_layer.update()


def turn(pb, q):
    A = ST["A"]; Mw = A.matrix_world @ pb.matrix; h = Mw.translation.copy()
    pb.matrix = A.matrix_world.inverted() @ (Matrix.Translation(h) @ q.to_matrix().to_4x4() @ Matrix.Translation(-h) @ Mw)
    bpy.context.view_layer.update()


def ik(side, T, pole):
    A = ST["A"]; s = "Right" if side else "Left"
    S, E, W = head(s + "Arm"), head(s + "ForeArm"), head(s + "Hand")
    l1, l2 = (E - S).length, (W - E).length
    d = min(max((T - S).length, abs(l1 - l2) + 1e-3), l1 + l2 - 1e-3); ax = (T - S).normalized()
    ca = max(-1.0, min(1.0, (l1 * l1 + d * d - l2 * l2) / (2 * l1 * d)))
    perp = pole - ax * pole.dot(ax); perp = perp.normalized() if perp.length > 1e-4 else ax.cross(Vector((1, 0, 0))).normalized()
    E2 = S + ax * (ca * l1) + perp * (math.sqrt(max(0.0, 1 - ca * ca)) * l1)
    turn(A.pose.bones[M_ + s + "Arm"], (E - S).rotation_difference(E2 - S))
    turn(A.pose.bones[M_ + s + "ForeArm"], (head(s + "Hand") - head(s + "ForeArm")).rotation_difference(S + ax * d - head(s + "ForeArm")))
    # the swings above leave each bone's roll wherever the clip had it: with the arm near straight the elbow's hinge then lies across
    # the bend and the sleeve reads as wrung. Roll the upper arm, then the forearm, about their own lines until the hinge
    # (rest: world -Z on the left arm, +Z on the right, arms along X) is the bend plane's normal.
    n = perp.cross(ax).normalized(); hz = Vector((0, 0, 1.0 if side else -1.0))
    for nm, tip in ((s + "Arm", s + "ForeArm"), (s + "ForeArm", s + "Hand")):
        pb = A.pose.bones[M_ + nm]; a = (head(tip) - head(nm)).normalized()
        h = ((A.matrix_world @ pb.matrix).to_3x3().normalized() @ rest_rot(pb).inverted()) @ hz
        hp, np_ = h - a * h.dot(a), n - a * n.dot(a)
        if hp.length < 1e-4 or np_.length < 1e-4: continue
        ang = math.atan2(hp.cross(np_).dot(a), hp.dot(np_)); q = Quaternion(a, ang)
        turn(pb, q)
        if nm.endswith("ForeArm") is False: turn(A.pose.bones[M_ + s + "ForeArm"], q.inverted())      # the forearm keeps its line; its own roll is set next
        ST.setdefault("hinge", {})[nm] = round(math.degrees(ang))
    return (T - S).length - d


def show(wid, body_reset=True, override=None):
    """place the gun and both hands as the game does for this weapon; returns numbers worth checking"""
    A = ST["A"]
    for o in [o for o in bpy.data.objects if o.get("hv_gun")]: bpy.data.objects.remove(o, do_unlink=True)
    for p in A.pose.bones: p.matrix_basis = ST["basis"][p.name]
    bpy.context.view_layer.update()
    J = json.load(open(WT + "weapons/%s.json" % wid, encoding="utf-8"))
    P = dict(J["hold"]["pose"])
    for k_, v_ in (override or {}).items(): P[k_] = dict(P.get(k_, {}), **v_) if isinstance(v_, dict) else v_   # l / r / fingers merge (a fit holds only the wrist places and finger turns)
    hold = J.get("hold", {})
    d = J["model"]["dir"]
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=glob.glob(MODELS + d + "/*.gltf")[0])
    parts = [o for o in bpy.data.objects if o not in before]
    try:                                                     # an untextured world model: the flat colour the game draws it in (else Blender shows it white)
        mj = json.load(open(WT + "weapons/models/%s/model.json" % d))
        if not any(p_.get("textured") for p_ in mj["parts"]):
            for o in parts:
                for sl_ in getattr(o, "material_slots", []):
                    bs = sl_.material and sl_.material.node_tree and sl_.material.node_tree.nodes.get("Principled BSDF")
                    if bs: bs.inputs["Base Color"].default_value = mj["parts"][0]["base"]; bs.inputs["Roughness"].default_value = mj["parts"][0].get("rough", 0.5)
    except Exception: pass
    gun = bpy.data.objects.new("GUN", None); ST["sc"].collection.objects.link(gun); gun["hv_gun"] = 1
    for o in parts:
        o["hv_gun"] = 1
        if o.parent is None: o.parent = gun
    gripR = Vector(J["sockets"]["rightGrip"]); gripL = Vector(J["sockets"]["leftSupport"]); k = P.get("scale", 1.0)
    magv = [g2b_inv(o.matrix_world @ Vector(c)) for o in parts if o.type == 'MESH' and o.name.lower().startswith("mag") for c in o.bound_box]
    if hold.get("left") == "magazine" and magv: gripL = sum(magv, Vector()) / len(magv)
    gripLs = gripR + (gripL - gripR) * k
    kind = {"pistol": 0, "rifle": 1, "launcher": 2}.get(hold.get("kind"), 0 if gripR.z < 0.12 else 2 if gripR.z > 0.6 else 1)
    if kind == 0 and gripR.z < 0.12 and gripL.z - gripR.z > 0.2: kind = 1
    shP, shL, ch2 = b2g(head("RightArm")), b2g(head("LeftArm")), b2g(head("Spine2"))
    gofs = Vector(P.get("gun", (0, 0, 0))); butt = Vector(list(P.get("butt", (0, 0))) + [0])
    lv = shL - shP; lv.y = 0; lv.normalize()
    Rw = Matrix.Identity(3)                                  # aimed level along +z (game)
    if kind == 1:
        inn = ch2 - shP; inn.y = 0; inn.normalize()
        pa = shP + Vector((gofs.x, 0, 0.12 + gofs.z)) + inn * 0.02 + Vector((0, 0.02 + gofs.y, 0)) - Vector((gripR.x + butt.x, gripR.y + butt.y, gripR.z * (1 - k)))
    elif kind == 2:
        shx = b2g(A.matrix_world @ A.pose.bones[M_ + "RightArm"].bone.head_local).x
        pa = shP + Vector((-0.2 * shx, 0.10, 0)) - Vector((0, 0, gripR.z - 0.30)) + Vector((gofs.x, gofs.y, gofs.z))
    else:
        l1 = (head("RightForeArm") - head("RightArm")).length; l2 = (head("RightHand") - head("RightForeArm")).length
        mid = (shP + shL) / 2; w = (shP - mid).length; re = 0.96 * (l1 + l2); off = gripR + Vector(P["r"]["p"])
        pa = mid + Vector((0, 0, math.sqrt(max(re * re - w * w, 0.01)))) + Vector((0, 0.03, 0)) - lv * 0.035 - off
    if kind != 0:                                            # the firing hand must reach its grip: the gun comes back along the aim until it does
        Lr = (head("RightForeArm") - head("RightArm")).length + (head("RightHand") - head("RightForeArm")).length
        for _ in range(3):
            dd = (pa + gripR + Vector(P["r"]["p"]) - shP).length
            if dd > 0.92 * Lr: pa = pa - Vector((0, 0, dd - 0.92 * Lr))
    Wg = Matrix.Translation(pa)                              # weapon matrix, game model space
    draw = Wg @ Matrix.Translation(gripR) @ Matrix.Scale(k, 4) @ Matrix.Translation(-gripR)
    B4 = B.to_4x4()
    gun.matrix_world = B4 @ draw @ B4.inverted()
    bpy.context.view_layer.update()
    short = {}
    for side, key in ((1, "r"), (0, "l")):
        s = "Right" if side else "Left"
        if key == "l" and hold.get("left") == "tube": continue
        anchor = gripR if (side == 1 or P["l"].get("anchor") == "grip") else gripLs
        T = g2b(Wg @ (anchor + Vector(P[key]["p"])))
        if side == 0 and kind != 0:                          # a far support grip: the collarbone swings the shoulder toward it first (up to 30 deg), so the arm keeps a bend
            cb = A.pose.bones[M_ + "LeftShoulder"]; Lc = (head("LeftForeArm") - head("LeftArm")).length + (head("LeftHand") - head("LeftForeArm")).length; st = 0
            while (T - head("LeftArm")).length > 0.93 * Lc and st < 10:
                c = head("LeftShoulder"); axq = (head("LeftArm") - c).cross(T - c)
                if axq.length < 1e-5: break
                turn(cb, Quaternion(axq.normalized(), math.radians(3))); st += 1
            ST["collar"] = st * 3
        if side == 0 and kind != 0:                          # the support hand slides back along the gun rather than float out of reach
            Ll = (head("LeftForeArm") - head("LeftArm")).length + (head("LeftHand") - head("LeftForeArm")).length; sl = 0.0
            while (T - head("LeftArm")).length > 0.97 * Ll and sl < 0.25:
                sl += 0.01; T = g2b(Wg @ (anchor + Vector(P[key]["p"]) - Vector((0, 0, sl))))
        pole_g = Vector((0, -1, 0)) + lv * (-0.45 if side else 0.35)
        if kind == 0: pole_g = Vector((0, -1, 0)) + (lv * -0.3 if side else lv * 0.3)
        short[key] = round(ik(side, T, g2b(pole_g)), 3)
        hb = A.pose.bones[M_ + s + "Hand"]
        dq = (B @ (Rw @ qg(P[key]["q"]).to_matrix()) @ BI)
        # the forearm takes 60 % of the hand's roll about the forearm's own line (a palm-up hand on a straight forearm wrings the cuff)
        fb = A.pose.bones[M_ + s + "ForeArm"]
        Rf = (A.matrix_world @ fb.matrix).to_3x3().normalized() @ rest_rot(fb).inverted()
        r = (Rf.inverted() @ dq).to_quaternion()
        ar = (A.matrix_world.to_3x3() @ (hb.bone.head_local - fb.bone.head_local)).normalized()
        pr = ar * Vector((r.x, r.y, r.z)).dot(ar)
        ang = 2.0 * math.atan2(pr.dot(ar), r.w)
        ang = (ang + math.pi) % (2 * math.pi) - math.pi
        turn(fb, Quaternion((Rf @ ar).normalized(), TWIST * ang))
        ST.setdefault("roll", {})[key] = round(math.degrees(ang))                # the hand's roll against the forearm: past ~120 the arm reads as wrung
        set_rot(hb, dq @ rest_rot(hb))
        def fingers(pb, par_delta):
            for c in pb.children:
                q = P["fingers"].get(c.name.split(":")[-1])
                dl = par_delta @ (B @ qg(q).to_matrix() @ BI) if q else par_delta
                set_rot(c, dl @ rest_rot(c)); fingers(c, dl)
        fingers(hb, dq)
    ST.update(gun=gun, wid=wid, P=P, J=J, kind=kind, gripR=gripR, gripL=gripLs, Wg=Wg, parts=parts)
    return {"wid": wid, "kind": kind, "scale": k, "reach_short": short, "template": P.get("template", "own"), "left": hold.get("left"), "anchor": P["l"].get("anchor"),
            "mag_found": bool(magv), "parts": [o.name for o in parts if o.type == 'MESH'][:8]}


def g2b_inv(v): return BI @ Vector(v)   # gun parts are imported at the origin: Blender point -> weapon space


def shot(path, views=("fr", "left", "under", "top")):
    """close-ups of the hands on the gun; returns the file list"""
    cam = ST["cam"]; A = ST["A"]; sc = ST["sc"]
    c = (head("RightHand") + head("LeftHand")) / 2
    V = {"fr": Vector((-0.75, -0.95, 0.25)), "left": Vector((1.1, -0.25, 0.15)), "under": Vector((0.25, -0.55, -0.85)), "top": Vector((-0.1, -0.35, 1.05)), "wide": Vector((-1.6, -2.2, 0.3))}
    out = []
    for v in views:
        cam.location = c + V[v]
        cam.rotation_euler = (c - cam.location).to_track_quat('-Z', 'Y').to_euler()
        cam.data.lens = 50 if v != "wide" else 35
        sc.render.filepath = "%s_%s.png" % (path, v); bpy.ops.render.render(write_still=True); out.append(sc.render.filepath)
    return out


# ---------------------------------------------------------------- fitting the hands to the gun's own geometry
from mathutils.bvhtree import BVHTree


def gun_bvh():
    dg = bpy.context.evaluated_depsgraph_get(); V = []; F = []
    for o in ST["parts"]:
        if o.type != 'MESH': continue
        e = o.evaluated_get(dg); me = e.to_mesh(); n0 = len(V)
        V += [e.matrix_world @ v.co for v in me.vertices]; F += [[n0 + i for i in p.vertices] for p in me.polygons]
        e.to_mesh_clear()
    return BVHTree.FromPolygons(V, F)


def surf(bvh, p):
    """signed distance from p to the gun's surface (negative: inside), and the nearest point"""
    loc, nrm, _, d = bvh.find_nearest(p)
    if loc is None: return 9.0, p
    return (d if (p - loc).dot(nrm) >= 0 else -d), loc


def palm_frame(s):
    w, m = head(s + "Hand"), head(s + "HandMiddle1")
    f = (m - w).normalized(); kl = (head(s + "HandIndex1") - head(s + "HandPinky1")).normalized()
    return w, m, f, kl


def fit_hand(side, bvh, snap=True):
    """palm onto the gun (moved along its own normal when it floats or is buried), then every finger curled until it meets the gun"""
    A = ST["A"]; s = "Right" if side else "Left"; key = "r" if side else "l"; rep = {}
    w, m, f, kl = palm_frame(s)
    pc = w + (m - w) * 0.6                                                    # palm centre
    d0, loc = surf(bvh, pc)
    n = (loc - pc).normalized() if d0 > 0 else (pc - loc).normalized()         # toward the gun
    n = (n - f * n.dot(f)).normalized()
    rep["palm_gap"] = round(d0, 3)
    if snap and (d0 > 0.026 or d0 < 0.014):                                    # the palm skin (22 mm off the bone line) must lie on the part, not hover or sink
        mv = n * (d0 - 0.021)
        P = ST["P"]; anchor_fix = BI @ mv                                       # weapon axes = model axes here (the gun is aimed level)
        P[key] = dict(P[key], p=[round(a + b, 4) for a, b in zip(P[key]["p"], anchor_fix)])
        rep["moved"] = [round(v, 3) for v in anchor_fix]
        return rep, True                                                       # re-pose with the new wrist place first
    pn = f.cross(kl) if s == "Left" else kl.cross(f)                         # the palm's own facing
    hb = A.pose.bones[M_ + s + "Hand"]
    hd = (A.matrix_world @ hb.matrix).to_3x3().normalized() @ rest_rot(hb).inverted()
    for fg in ("Index", "Middle", "Ring", "Pinky"):
        js = [A.pose.bones[M_ + "%sHand%s%d" % (s, fg, i)] for i in (1, 2, 3)]
        tipb = A.pose.bones.get(M_ + "%sHand%s4" % (s, fg))
        for j in js: set_rot(j, hd @ rest_rot(j))                             # straight out from the palm
        tip = lambda: (A.matrix_world @ (tipb.head if tipb else js[2].tail))
        ax = kl if (Quaternion(kl, 0.3) @ (tip() - (A.matrix_world @ js[0].head))).dot(pn) > (tip() - (A.matrix_world @ js[0].head)).dot(pn) else -kl   # fingers close toward the palm side only, never backward onto whatever is nearest
        tot = 0
        for ji, (j, lim) in enumerate(zip(js, (85, 100, 75))):
            nxt = (lambda jj=ji: A.matrix_world @ (js[jj + 1].head if jj < 2 else (tipb.head if tipb else js[2].tail)))
            a = 0
            while a < lim:
                d, _ = surf(bvh, nxt())
                if d < 0.011: break
                turn(j, Quaternion(ax, math.radians(5))); a += 5
            if a >= lim:                                    # nothing to close on (a thin magazine, an edge): a closed hand, short of a full fist
                turn(j, Quaternion(ax, math.radians(-(a - 50)))); a = 50
            tot += a
        rep[fg] = (tot, round(surf(bvh, tip())[0], 3))
    tb = [A.pose.bones[M_ + "%sHandThumb%d" % (s, i)] for i in (1, 2, 3)]; tt = A.pose.bones.get(M_ + s + "HandThumb4")
    for j in tb: set_rot(j, hd @ rest_rot(j))
    ttip = lambda: (A.matrix_world @ (tt.head if tt else tb[2].tail))
    a = 0
    while a < 60:
        d, loc = surf(bvh, ttip())
        if d < 0.012: break
        base = A.matrix_world @ tb[0].head; ax = (ttip() - base).cross(loc - ttip())
        if ax.length < 1e-5: break
        turn(tb[0], Quaternion(ax.normalized(), math.radians(4))); a += 4
    rep["Thumb"] = (a, round(surf(bvh, ttip())[0], 3))
    return rep, False


def fit(wid, snap=True, magpalm=False, palm_at=(0.032, 0, 0)):
    """show + fit both hands (up to 3 re-poses for palm moves); keeps the result in ST["fit"][wid]"""
    r = show(wid, override=ST.get("fit", {}).get(wid)); rep = {}
    over = {}
    if magpalm and (ST["J"].get("hold", {}).get("left") == "magazine" or ST["P"].get("template") == "cgrip"):   # the support palm onto the magazine (or the foregrip socket) itself, from its left
        w, m, f, kl = palm_frame("Left"); pc = w + (m - w) * 0.6
        tgt = g2b(ST["Wg"] @ (ST["gripL"] + Vector(palm_at)))
        dgl = BI @ (tgt - pc); P = ST["P"]
        P["l"] = dict(P["l"], p=[round(a + b, 4) for a, b in zip(P["l"]["p"], dgl)]); rep["magpalm"] = [round(v, 3) for v in dgl]
        over = dict({"l": P["l"], "r": P["r"]}, **{k: P[k] for k in ("gun", "scale") if P.get(k) is not None}); show(wid, override=over)
    for it in range(4):
        bvh = gun_bvh(); again = False
        for side in (1, 0):
            if side == 0 and ST["J"].get("hold", {}).get("left") == "tube": continue
            rep["r" if side else "l"], mv = fit_hand(side, bvh, snap and it < 3)
            again = again or mv
        if not again: break
        over = dict({"l": ST["P"]["l"], "r": ST["P"]["r"]}, **{k: ST["P"][k] for k in ("gun", "scale") if ST["P"].get(k) is not None})   # the re-pose keeps a gun offset / scale under trial
        show(wid, override=over)
    A = ST["A"]; fingers = {}
    for s in ("Left", "Right"):
        hb = A.pose.bones[M_ + s + "Hand"]
        def rec(pb, pd):
            for c in pb.children:
                dj = (A.matrix_world @ c.matrix).to_3x3().normalized() @ rest_rot(c).inverted()
                D = pd.inverted() @ dj
                q = (BI @ D @ B).to_quaternion(); fingers[c.name.split(":")[-1]] = [round(q.x, 4), round(q.y, 4), round(q.z, 4), round(q.w, 4)]
                rec(c, dj)
        rec(hb, (A.matrix_world @ hb.matrix).to_3x3().normalized() @ rest_rot(hb).inverted())
    out = {"l": {"p": ST["P"]["l"]["p"]}, "r": {"p": ST["P"]["r"]["p"]}, "fingers": fingers}
    ST.setdefault("fit", {})[wid] = out
    return {"show": r, "fit": rep}


# ---------------------------------------------------------------- review helpers (grip redo, gun placement, clipping)
def verts():
    dg = bpy.context.evaluated_depsgraph_get(); V = []
    for o in ST["parts"]:
        if o.type != 'MESH': continue
        e = o.evaluated_get(dg); me = e.to_mesh(); V += [(e.matrix_world @ v.co).copy() for v in me.vertices]; e.to_mesh_clear()
    return V


def prof(step=0.02):
    """the gun's outline per slice along the aim: {y: [zmin, zmax, xmin, xmax]} (Blender world)"""
    sl = {}
    for v in verts():
        k = round(v.y / step); a = sl.get(k)
        if a is None: sl[k] = [round(v.z, 3), round(v.z, 3), round(v.x, 3), round(v.x, 3)]
        else: a[0] = min(a[0], round(v.z, 3)); a[1] = max(a[1], round(v.z, 3)); a[2] = min(a[2], round(v.x, 3)); a[3] = max(a[3], round(v.x, 3))
    return {round(k * step, 2): sl[k] for k in sorted(sl)}


def palm(s):
    w, m, f, kl = palm_frame(s); return w + (m - w) * 0.6


def refit(wid):
    """fit again (palm skin onto the part, fingers closed), keeping what a fit does not hold: the gun offset / scale and a turned left hand"""
    f = ST["fit"].get(wid, {}); extra = {k: f[k] for k in ("gun", "scale") if k in f}; lk = {k: f["l"][k] for k in ("q", "anchor") if k in f.get("l", {})}
    r = fit(wid); ST["fit"][wid]["l"].update(lk); ST["fit"][wid].update(extra)
    return {k: (v.get("palm_gap"), v.get("moved")) for k, v in r["fit"].items() if isinstance(v, dict)}


def vgrip(wid, tgt, tilt=None):
    """left hand as on a vertical grip / magazine: palm to the right with its centre on tgt, knuckle line up, fingers forward
    (tilt: degrees the fingers point above level; None = along the forearm, at most 35), then seated and closed"""
    tgt = Vector(tgt); over = {k: dict(v) if isinstance(v, dict) else v for k, v in ST["fit"].get(wid, {}).items()}; log = []
    extra = {k: v for k, v in over.items() if k in ("gun", "scale")}
    show(wid, override=over)
    for it in range(4):
        w, m, f, kl = palm_frame("Left"); pc = w + (m - w) * 0.6
        fa = (head("LeftHand") - head("LeftForeArm")).normalized()
        t = max(-35.0, min(35.0, math.degrees(math.atan2(fa.z, -fa.y)))) if tilt is None else tilt
        fd = Vector((0, -math.cos(math.radians(t)), math.sin(math.radians(t))))
        nd = Vector((-1, 0, 0)); kd = nd.cross(fd)
        if fd.cross(kd).dot(nd) < 0: kd = -kd
        n0 = f.cross(kl).normalized(); k0 = n0.cross(f).normalized()
        D = Matrix((fd, kd.normalized(), nd)).transposed() @ Matrix((f, k0, n0)).transposed().inverted()
        P = ST["P"]; qn = ((BI @ D @ B) @ qg(P["l"]["q"]).to_matrix()).to_quaternion(); dp = BI @ (tgt - pc)
        over["l"] = dict(P["l"], q=[round(qn.x, 4), round(qn.y, 4), round(qn.z, 4), round(qn.w, 4)], p=[round(a + b, 4) for a, b in zip(P["l"]["p"], dp)])
        log.append([round(t, 1), round((tgt - pc).length, 3)])
        show(wid, override=over)
    ST["fit"][wid] = dict({"l": dict(over["l"], anchor=ST["P"]["l"].get("anchor") or "support"), "r": over.get("r", {"p": ST["P"]["r"]["p"]}), "fingers": over.get("fingers", {})}, **extra)
    return {"log": log, "fit": refit(wid)}


def lmove(wid, tgt, n=3):
    """left palm centre onto tgt (hand turn kept)"""
    over = ST["fit"].setdefault(wid, {}); log = []
    for it in range(n):
        show(wid, override=over); pc = palm("Left"); dp = BI @ (Vector(tgt) - pc); log.append(round(dp.length, 3))
        over["l"] = dict(over.get("l", {}), p=[round(a + b, 4) for a, b in zip(ST["P"]["l"]["p"], dp)])
    return log


def place(wid, over, rt_ref, pa_ref, butt_y, n=5):
    """gun forward until its butt is at butt_y, the right palm kept on a point fixed to the gun (rt_ref when the gun's origin was at pa_ref)"""
    log = []
    for it in range(n):
        show(wid, override=over)
        d = ST["Wg"].translation - Vector(pa_ref); tgt = Vector(rt_ref) + Vector((d.x, -d.z, d.y))
        pc = palm("Right"); dp = BI @ (tgt - pc); my = max(v.y for v in verts())
        log.append([round((tgt - pc).length, 3), round(my, 3)])
        over["r"] = dict(ST["P"]["r"], p=[round(a + b, 4) for a, b in zip(ST["P"]["r"]["p"], dp)])
        g = list(over["gun"]); g[2] = round(g[2] + (my - butt_y), 4); over["gun"] = g
    show(wid, override=over)
    return log


def body_bvh():
    A = ST["A"]; dg = bpy.context.evaluated_depsgraph_get(); V = []; F = []
    for o in bpy.data.objects:
        if o.type != 'MESH' or o.get("hv_gun") or o.find_armature() != A: continue
        e = o.evaluated_get(dg); me = e.to_mesh(); n0 = len(V)
        V += [e.matrix_world @ v.co for v in me.vertices]; F += [[n0 + i for i in p.vertices] for p in me.polygons]; e.to_mesh_clear()
    return BVHTree.FromPolygons(V, F)


REG = ("Head", "Neck", "Spine2", "Spine", "RightShoulder", "LeftShoulder", "RightArm", "LeftArm", "RightForeArm", "LeftForeArm", "RightHand", "LeftHand", "RightUpLeg", "LeftUpLeg")


def clip(wid):
    """gun vertices inside the soldier (hands excluded): worst depth per body region. The approved Garand reads 3-4 cm at the
    shoulder / chest (the seated butt in the jacket); a head / neck hit or more than ~4.5 cm is a gun through the body"""
    show(wid, override=ST["fit"].get(wid)); bb = body_bvh()
    hp = {r: head(r) for r in REG}; wr = [hp["RightHand"], hp["LeftHand"]]; worst = {}
    for p in verts():
        if min((p - w).length for w in wr) < 0.13: continue
        loc, nrm, _, d = bb.find_nearest(p, 0.2)
        if loc is None or (p - loc).dot(nrm) >= 0 or d < 0.015: continue
        r = min(REG, key=lambda k: (p - hp[k]).length)
        if r.endswith("Hand"): continue
        if d > worst.get(r, 0): worst[r] = round(d, 3)
    return worst


def curl_ok(wid):
    """per hand: fingers bent toward the palm side (True) or backward (False), and whether the palm faces the gun"""
    show(wid, override=ST["fit"].get(wid)); A = ST["A"]; bvh = gun_bvh(); out = {}
    for s in ("Left", "Right"):
        w, m, f, kl = palm_frame(s); pn = f.cross(kl) if s == "Left" else kl.cross(f); pc = w + (m - w) * 0.6
        tips = [A.matrix_world @ A.pose.bones[M_ + "%sHand%s3" % (s, g)].tail - A.matrix_world @ A.pose.bones[M_ + "%sHand%s1" % (s, g)].head for g in ("Index", "Middle", "Ring", "Pinky")]
        d, loc = surf(bvh, pc)
        out[s[0]] = [min(round(t.normalized().dot(pn), 2) for t in tips), round((loc - pc).normalized().dot(pn), 2)]
    return out
