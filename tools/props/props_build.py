# PR1 close-up props: real objects built in Blender (bevelled casings, recessed panels, seams, screws, hinges, curved
# hoses and cables, raised labels / dials / digits in our own generic text, glass), with a PBR set baked per prop from
# procedural wear (base colour with sun fade, edge chipping to primer / bare metal, rust runs, dirt in recesses and at
# the base; roughness; metalness; tangent-space normals with the bevels and fine grain). One GLB per prop, ground
# pivot, game axes (+y up, the prop's front toward +z); glass as its own "glass" material. The game bakes them with
# --import-weapon-models (content/assets/<id>.json "model" block, lod1 for distance).
#   blender -b --factory-startup --python tools/props/props_build.py -- <out dir> <prop> [<prop> ...]
# Our own geometry and text only: no brands, no downloaded meshes or images.
import bpy, bmesh, sys, os, math, random
from mathutils import Vector, Matrix

ARGS = sys.argv[sys.argv.index("--") + 1:]
OUT = ARGS[0]; WANT = ARGS[1:]
os.makedirs(OUT, exist_ok=True)

# ------------------------------------------------------------------ scene
def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene; sc.render.engine = "CYCLES"; sc.cycles.device = "CPU"; sc.cycles.samples = 16
    sc.render.bake.margin = 6; sc.render.bake.use_selected_to_active = False
    sc.unit_settings.system = "METRIC"

def G(x, y, z):   # game (y up, front +z) -> Blender (z up, front -y)
    return Vector((x, -z, y))

def _finish(o, mat, bevel=0.0, segs=2):
    if mat: o.data.materials.append(mat)
    if bevel > 0:
        m = o.modifiers.new("bev", "BEVEL"); m.width = bevel; m.segments = segs; m.limit_method = "ANGLE"; m.angle_limit = math.radians(40)
        m.harden_normals = False
    return o

def box(size, c, mat, bevel=0.0, segs=2, rot=(0, 0, 0)):
    """size and centre in game axes (x, y up, z front); rot: game-axis degrees (x, y, z)"""
    bpy.ops.mesh.primitive_cube_add(size=1, location=G(*c))
    o = bpy.context.object; o.scale = (size[0], size[2], size[1])
    o.rotation_euler = (math.radians(rot[0]), math.radians(-rot[2]), math.radians(rot[1]))
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    return _finish(o, mat, min(bevel, 0.45 * min(size)), segs)

def cyl(r, h, c, mat, axis="y", segs=24, bevel=0.0, r2=None):
    """a cylinder (r2: cone top radius) of length h along the game axis, centred at c"""
    bpy.ops.mesh.primitive_cone_add(vertices=segs, radius1=r, radius2=r if r2 is None else r2, depth=h, location=G(*c))
    o = bpy.context.object
    if axis == "x": o.rotation_euler = (0, math.radians(90), 0)
    elif axis == "z": o.rotation_euler = (math.radians(90), 0, 0)
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    return _finish(o, mat, bevel, 2)

def tube(pts, r, mat, res=10, segs=10):
    """a hose / cable / rail through game-space points (a smooth NURBS-like path)"""
    cu = bpy.data.curves.new("tube", "CURVE"); cu.dimensions = "3D"; cu.bevel_depth = r; cu.bevel_resolution = max(2, segs // 4); cu.resolution_u = res
    cu.use_fill_caps = True
    sp = cu.splines.new("BEZIER"); sp.bezier_points.add(len(pts) - 1)
    for p, q in zip(sp.bezier_points, pts): p.co = G(*q); p.handle_left_type = p.handle_right_type = "AUTO"
    o = bpy.data.objects.new("tube", cu); bpy.context.collection.objects.link(o)
    bpy.context.view_layer.objects.active = o; o.select_set(True)
    bpy.ops.object.convert(target="MESH")
    o = bpy.context.object
    if mat: o.data.materials.append(mat)
    return o

def text(s, size, c, mat, face="+z", extrude=0.0006, align="CENTER", bold=False):
    """raised lettering on a face (+z, -z, +x, -x, +y): our own generic words and digits"""
    cu = bpy.data.curves.new("txt", "FONT"); cu.body = s; cu.size = size; cu.extrude = extrude; cu.align_x = align; cu.align_y = "CENTER"
    if bold: cu.offset = 0.0; cu.extrude = extrude; cu.bevel_depth = size * 0.012
    o = bpy.data.objects.new("txt", cu); bpy.context.collection.objects.link(o)
    o.location = G(*c)
    rx = {"+z": (90, 0, 0), "-z": (90, 0, 180), "+x": (90, 0, 90), "-x": (90, 0, -90), "+y": (0, 0, 0)}[face]
    o.rotation_euler = tuple(math.radians(a) for a in rx)
    bpy.context.view_layer.objects.active = o
    for x in bpy.context.selected_objects: x.select_set(False)
    o.select_set(True); bpy.ops.object.convert(target="MESH")
    o = bpy.context.object
    if mat: o.data.materials.append(mat)
    return o

def screws(pts, mat, r=0.005, face="+z", h=0.003):
    for p in pts:
        ax = {"+z": "z", "-z": "z", "+x": "x", "-x": "x", "+y": "y"}[face]
        o = cyl(r, h, p, mat, axis=ax, segs=6, bevel=0.0008)
        for d in (0, 90):   # a cross slot as two thin bars
            pass
    return

# ------------------------------------------------------------------ materials: procedural, baked to one set per prop
class MB:
    """node-tree helpers for one material"""
    def __init__(self, name):
        self.m = bpy.data.materials.new(name); self.m.use_nodes = True
        self.N = self.m.node_tree.nodes; self.L = self.m.node_tree.links
        for n in list(self.N):
            if n.type != "OUTPUT_MATERIAL": self.N.remove(n)
        self.out = next(n for n in self.N if n.type == "OUTPUT_MATERIAL")
        self.bs = self.N.new("ShaderNodeBsdfPrincipled"); self.L.new(self.bs.outputs[0], self.out.inputs[0])
        self.tc = self.N.new("ShaderNodeTexCoord"); self.geo = self.N.new("ShaderNodeNewGeometry")
    def link(self, a, b): self.L.new(a, b)
    def val(self, x):
        n = self.N.new("ShaderNodeValue"); n.outputs[0].default_value = x; return n.outputs[0]
    def sock(self, x): return x if not isinstance(x, (int, float)) else self.val(x)
    def math(self, op, a, b=None, clamp=False):
        n = self.N.new("ShaderNodeMath"); n.operation = op; n.use_clamp = clamp
        for k, v in enumerate((a, b)):
            if v is None: continue
            if isinstance(v, (int, float)): n.inputs[k].default_value = v
            else: self.L.new(v, n.inputs[k])
        return n.outputs[0]
    def noise(self, scale, detail=4.0, rough=0.55, stretch=(1, 1, 1), w=None):
        n = self.N.new("ShaderNodeTexNoise"); n.inputs["Scale"].default_value = scale; n.inputs["Detail"].default_value = detail
        n.inputs["Roughness"].default_value = rough
        mp = self.N.new("ShaderNodeMapping"); mp.inputs["Scale"].default_value = stretch
        if w is not None: mp.inputs["Location"].default_value = (w, w * 0.7, w * 1.3)
        self.L.new(self.tc.outputs["Object"], mp.inputs["Vector"]); self.L.new(mp.outputs[0], n.inputs["Vector"])
        return n.outputs["Fac"]
    def ramp(self, s, lo, hi):
        r = self.N.new("ShaderNodeMapRange"); r.inputs["From Min"].default_value = lo; r.inputs["From Max"].default_value = hi; r.clamp = True
        self.L.new(s, r.inputs["Value"]); return r.outputs["Result"]
    def mix(self, fac, a, b, blend="MIX"):
        n = self.N.new("ShaderNodeMix"); n.data_type = "RGBA"; n.blend_type = blend
        for i, v in ((0, fac), (6, a), (7, b)):
            if isinstance(v, (int, float)): n.inputs[i].default_value = v
            elif isinstance(v, tuple): n.inputs[i].default_value = v if len(v) == 4 else (*v, 1)
            else: self.L.new(v, n.inputs[i])
        return n.outputs[2]
    def ao(self, dist=0.04):
        n = self.N.new("ShaderNodeAmbientOcclusion"); n.samples = 12; n.inputs["Distance"].default_value = dist; n.only_local = False
        return n.outputs["AO"]
    def edge(self, radius=0.004, gain=10.0):
        bev = self.N.new("ShaderNodeBevel"); bev.inputs["Radius"].default_value = radius; bev.samples = 8
        dp = self.N.new("ShaderNodeVectorMath"); dp.operation = "DOT_PRODUCT"
        self.L.new(bev.outputs[0], dp.inputs[0]); self.L.new(self.geo.outputs["Normal"], dp.inputs[1])
        self._bev = bev
        return self.math("MULTIPLY", self.math("SUBTRACT", 1.0, dp.outputs["Value"]), gain, True)
    def height(self):   # object-space height (the joined mesh's origin is on the ground)
        sep = self.N.new("ShaderNodeSeparateXYZ"); self.L.new(self.tc.outputs["Object"], sep.inputs[0]); return sep.outputs["Z"]
    def up(self):   # 1 on upward faces
        sep = self.N.new("ShaderNodeSeparateXYZ"); self.L.new(self.geo.outputs["Normal"], sep.inputs[0]); return self.ramp(sep.outputs["Z"], 0.3, 0.95)
    def finish(self, col, rough, metal, bump=None, bump_strength=0.15):
        self.link(col, self.bs.inputs["Base Color"]); self.link(self.sock(rough), self.bs.inputs["Roughness"])
        self.metal = self.sock(metal)
        bv = getattr(self, "_bev", None)
        if bv is None:
            bv = self.N.new("ShaderNodeBevel"); bv.inputs["Radius"].default_value = 0.002; bv.samples = 6
        if bump is not None:
            b = self.N.new("ShaderNodeBump"); b.inputs["Strength"].default_value = bump_strength; b.inputs["Distance"].default_value = 0.001
            self.link(bump, b.inputs["Height"]); self.link(bv.outputs[0], b.inputs["Normal"]); self.link(b.outputs[0], self.bs.inputs["Normal"])
        else:
            self.link(bv.outputs[0], self.bs.inputs["Normal"])
        self.m["pr1"] = True
        return self.m

SEED = [0]
def nseed(): SEED[0] += 1; return SEED[0] * 3.17

def paint(name, rgb, wear=0.5, rust=0.35, dirt=0.5, gloss=0.4, primer=(0.42, 0.42, 0.4), bare=(0.5, 0.5, 0.52), fade=0.25):
    """painted steel / sheet: tonal drift, sun fade on top faces, chipped edges (primer, then bare steel), rust runs from
    the chips and the base, dirt in recesses and a grime band at the ground"""
    b = MB(name); w = nseed()
    drift = b.ramp(b.noise(3.0, 3, 0.5, w=w), 0.3, 0.7)
    base = b.mix(drift, tuple(c * 0.9 for c in rgb), tuple(min(1, c * 1.08) for c in rgb))
    top = b.up()
    faded = b.mix(b.math("MULTIPLY", top, fade), base, (0.8, 0.79, 0.74), "MIX")
    edge = b.edge(0.004, 12.0)
    chipn = b.ramp(b.noise(28.0, 6, 0.65, w=w + 1), 0.45, 0.62)
    chips = b.math("MULTIPLY", b.math("MULTIPLY", edge, chipn, True), wear * 2.0, True)
    deep = b.math("MULTIPLY", chips, b.ramp(b.noise(60.0, 4, 0.6, w=w + 2), 0.5, 0.6), True)
    c1 = b.mix(chips, faded, primer)
    c2 = b.mix(deep, c1, bare)
    z = b.height()
    streak = b.ramp(b.noise(9.0, 6, 0.7, stretch=(9, 9, 0.9), w=w + 3), 0.52, 0.72)
    low = b.math("SUBTRACT", 1.0, b.ramp(z, 0.0, 0.55))
    rustm = b.math("MULTIPLY", b.math("ADD", b.math("MULTIPLY", streak, low), b.math("MULTIPLY", deep, 0.8)), rust * 1.6, True)
    rustc = b.mix(b.ramp(b.noise(40.0, 6, 0.7, w=w + 4), 0.3, 0.7), (0.23, 0.09, 0.035), (0.42, 0.2, 0.08))
    c3 = b.mix(rustm, c2, rustc)
    ao = b.ao(0.05)
    cav = b.math("MULTIPLY", b.math("SUBTRACT", 1.0, ao), dirt * 1.6, True)
    grime = b.math("MULTIPLY", b.math("SUBTRACT", 1.0, b.ramp(z, 0.0, 0.3)), dirt * 0.8, True)
    dmask = b.math("ADD", cav, b.math("MULTIPLY", grime, b.ramp(b.noise(12.0, 4, 0.6, w=w + 5), 0.3, 0.7)), True)
    col = b.mix(dmask, c3, (0.12, 0.1, 0.075))
    rough = b.math("ADD", b.math("ADD", 1.0 - gloss, b.math("MULTIPLY", rustm, 0.35)), b.math("MULTIPLY", dmask, 0.25), True)
    metal = b.math("MULTIPLY", deep, 0.85)
    bump = b.math("ADD", b.math("MULTIPLY", rustm, 0.6), b.math("MULTIPLY", b.noise(220.0, 3, 0.5, w=w + 6), 0.08))
    return b.finish(col, rough, metal, bump, 0.25)

def metal(name, rgb=(0.62, 0.62, 0.6), rough=0.32, brushed=True, dirt=0.4, rust=0.0):
    b = MB(name); w = nseed()
    br = b.noise(400.0, 2, 0.5, stretch=(1, 1, 40) if brushed else (1, 1, 1), w=w)
    base = b.mix(b.ramp(br, 0.3, 0.7), tuple(c * 0.85 for c in rgb), rgb)
    ao = b.ao(0.04)
    smudge = b.ramp(b.noise(6.0, 4, 0.6, w=w + 1), 0.55, 0.75)
    dmask = b.math("ADD", b.math("MULTIPLY", b.math("SUBTRACT", 1.0, ao), dirt * 1.5), b.math("MULTIPLY", smudge, dirt * 0.4), True)
    col = b.mix(dmask, base, (0.18, 0.16, 0.13))
    if rust > 0:
        z = b.height(); low = b.math("SUBTRACT", 1.0, b.ramp(z, 0.0, 0.3))
        rm = b.math("MULTIPLY", b.math("MULTIPLY", low, b.ramp(b.noise(18.0, 6, 0.7, w=w + 2), 0.5, 0.65)), rust * 2, True)
        col = b.mix(rm, col, (0.3, 0.13, 0.05))
    r = b.math("ADD", rough, b.math("MULTIPLY", dmask, 0.35), True)
    return b.finish(col, r, b.math("SUBTRACT", 1.0, b.math("MULTIPLY", dmask, 0.6)), b.math("MULTIPLY", br, 0.05), 0.1)

def plastic(name, rgb, rough=0.55, dirt=0.4, fade=0.2):
    b = MB(name); w = nseed()
    base = b.mix(b.ramp(b.noise(5.0, 3, 0.5, w=w), 0.3, 0.7), tuple(c * 0.92 for c in rgb), rgb)
    base = b.mix(b.math("MULTIPLY", b.up(), fade), base, (0.78, 0.77, 0.72))
    ao = b.ao(0.04)
    dmask = b.math("MULTIPLY", b.math("SUBTRACT", 1.0, ao), dirt * 1.5, True)
    col = b.mix(dmask, base, (0.1, 0.09, 0.07))
    return b.finish(col, b.math("ADD", rough, b.math("MULTIPLY", dmask, 0.2), True), 0.0, b.noise(700.0, 2, 0.5, w=w + 1), 0.06)

def rubber(name, rgb=(0.035, 0.035, 0.035), dirt=0.5):
    b = MB(name); w = nseed()
    dust = b.math("MULTIPLY", b.ramp(b.noise(14.0, 4, 0.6, w=w), 0.5, 0.8), dirt, True)
    col = b.mix(dust, rgb, (0.22, 0.2, 0.17))
    return b.finish(col, b.math("ADD", 0.82, b.math("MULTIPLY", dust, 0.15), True), 0.0, b.noise(300.0, 3, 0.6, w=w + 1), 0.15)

def printed(name, rgb, rough=0.45, wear=0.35):
    """stickers, decals and lettering: flat print, faded and scuffed"""
    b = MB(name); w = nseed()
    sc = b.math("MULTIPLY", b.ramp(b.noise(35.0, 5, 0.65, w=w), 0.55, 0.75), wear, True)
    col = b.mix(sc, rgb, (0.72, 0.71, 0.66))
    col = b.mix(b.math("MULTIPLY", b.up(), 0.2), col, (0.85, 0.84, 0.8))
    return b.finish(col, rough, 0.0)

def lcd(name, rgb=(0.05, 0.07, 0.06)):
    b = MB(name); w = nseed()
    col = b.mix(b.ramp(b.noise(20.0, 3, 0.5, w=w), 0.4, 0.6), rgb, tuple(c * 1.4 for c in rgb))
    return b.finish(col, 0.12, 0.0)

def concrete(name, rgb=(0.52, 0.5, 0.46), dirt=0.6):
    b = MB(name); w = nseed()
    pores = b.noise(180.0, 6, 0.7, w=w)
    base = b.mix(b.ramp(b.noise(4.0, 5, 0.6, w=w + 1), 0.3, 0.7), tuple(c * 0.85 for c in rgb), rgb)
    base = b.mix(b.ramp(pores, 0.6, 0.75), base, tuple(c * 0.7 for c in rgb))
    ao = b.ao(0.06); z = b.height()
    dmask = b.math("ADD", b.math("MULTIPLY", b.math("SUBTRACT", 1.0, ao), dirt * 1.6), b.math("MULTIPLY", b.math("SUBTRACT", 1.0, b.ramp(z, 0, 0.25)), dirt * 0.6), True)
    col = b.mix(dmask, base, (0.16, 0.14, 0.11))
    edge = b.edge(0.008, 8.0)
    col = b.mix(b.math("MULTIPLY", edge, b.ramp(b.noise(30.0, 5, 0.6, w=w + 2), 0.5, 0.65), True), col, tuple(min(1, c * 1.2) for c in rgb))
    return b.finish(col, 0.88, 0.0, pores, 0.4)

def wood(name, rgb=(0.45, 0.32, 0.2), weather=0.5):
    b = MB(name); w = nseed()
    wave = b.N.new("ShaderNodeTexWave"); wave.wave_type = "BANDS"; wave.inputs["Scale"].default_value = 6.0; wave.inputs["Distortion"].default_value = 6.0
    wave.inputs["Detail"].default_value = 3.0
    b.link(b.tc.outputs["Object"], wave.inputs["Vector"])
    grain = b.ramp(wave.outputs["Fac"], 0.2, 0.9)
    base = b.mix(grain, tuple(c * 0.75 for c in rgb), rgb)
    grey = b.mix(b.math("MULTIPLY", b.up(), weather), base, (0.5, 0.48, 0.44))
    ao = b.ao(0.05)
    col = b.mix(b.math("MULTIPLY", b.math("SUBTRACT", 1.0, ao), 1.2, True), grey, (0.12, 0.09, 0.06))
    return b.finish(col, 0.8, 0.0, grain, 0.3)

def glass_mat(rgb=(0.55, 0.62, 0.62), alpha=0.25):
    m = bpy.data.materials.new("glass"); m.use_nodes = True
    bs = m.node_tree.nodes.get("Principled BSDF")
    bs.inputs["Base Color"].default_value = (*rgb, 1); bs.inputs["Roughness"].default_value = 0.05; bs.inputs["Alpha"].default_value = alpha
    return m

# ------------------------------------------------------------------ bake + export
def bake_and_export(pid, size=1024):
    objs = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    glassy = [o for o in objs if o.data.materials and o.data.materials[0].name.startswith("glass")]
    solid = [o for o in objs if o not in glassy]
    for o in bpy.context.selected_objects: o.select_set(False)
    for o in solid: o.select_set(True)
    bpy.context.view_layer.objects.active = solid[0]
    for o in solid:
        bpy.context.view_layer.objects.active = o
        for m in list(o.modifiers): bpy.ops.object.modifier_apply(modifier=m.name)
    bpy.context.view_layer.objects.active = solid[0]
    for o in solid: o.select_set(True)
    bpy.ops.object.join(); body = bpy.context.object; body.name = pid
    # the origin on the ground under the middle (height masks read object Z)
    bpy.ops.object.origin_set(type="ORIGIN_CURSOR")
    # uv atlas
    bpy.ops.object.mode_set(mode="EDIT"); bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(60), island_margin=0.004, area_weight=0.0, scale_to_bounds=False)
    bpy.ops.uv.pack_islands(margin=0.004, rotate=True)
    bpy.ops.object.mode_set(mode="OBJECT")
    for o in glassy: o.select_set(False)
    body.select_set(True); bpy.context.view_layer.objects.active = body
    imgs = {}
    def target(nm, colour):
        im = bpy.data.images.new(pid + "_" + nm, size, size, alpha=False); im.colorspace_settings.name = "sRGB" if colour else "Non-Color"
        for m in body.data.materials:
            N = m.node_tree.nodes
            for n in N: n.select = False
            t = N.new("ShaderNodeTexImage"); t.image = im; t.select = True; N.active = t; t.name = "bake_target"
        return im
    def clear_targets():
        for m in body.data.materials:
            N = m.node_tree.nodes
            for n in [n for n in N if n.name.startswith("bake_target")]: N.remove(n)
    def principled(m): return next(n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    # base colour: metallic off so the diffuse colour pass is the paint itself
    saved = {}
    for m in body.data.materials:
        bs = principled(m); ls = [l for l in m.node_tree.links if l.to_socket == bs.inputs["Metallic"]]
        saved[m.name] = [l.from_socket for l in ls]
        for l in ls: m.node_tree.links.remove(l)
        bs.inputs["Metallic"].default_value = 0.0
    im = target("base", True); bpy.ops.object.bake(type="DIFFUSE", pass_filter={"COLOR"}); imgs["base"] = im; clear_targets()
    im = target("rough", False); bpy.ops.object.bake(type="ROUGHNESS"); imgs["rough"] = im; clear_targets()
    im = target("normal", False); bpy.ops.object.bake(type="NORMAL", normal_space="TANGENT"); imgs["normal"] = im; clear_targets()
    # metalness through emission
    for m in body.data.materials:
        bs = principled(m); N, L = m.node_tree.nodes, m.node_tree.links
        em = N.new("ShaderNodeEmission"); em.name = "bake_em"; em.inputs["Color"].default_value = (1, 1, 1, 1)
        src = saved[m.name]
        if src: L.new(src[0], em.inputs["Strength"])
        else: em.inputs["Strength"].default_value = 0.0
        out = next(n for n in N if n.type == "OUTPUT_MATERIAL"); L.new(em.outputs[0], out.inputs[0])
    im = target("metal", False); bpy.ops.object.bake(type="EMIT"); imgs["metal"] = im; clear_targets()
    # one glTF material over the baked set: MR packed (G roughness, B metalness)
    import numpy as np
    def px(i): a = np.empty(size * size * 4, dtype=np.float32); i.pixels.foreach_get(a); return a.reshape(-1, 4)
    r = px(imgs["rough"]); mt = px(imgs["metal"])
    mr = np.ones_like(r); mr[:, 1] = r[:, 0]; mr[:, 2] = mt[:, 0]
    mri = bpy.data.images.new(pid + "_mr", size, size, alpha=False); mri.colorspace_settings.name = "Non-Color"; mri.pixels.foreach_set(mr.ravel())
    fm = bpy.data.materials.new(pid); fm.use_nodes = True; N, L = fm.node_tree.nodes, fm.node_tree.links
    bs = N.get("Principled BSDF")
    tb = N.new("ShaderNodeTexImage"); tb.image = imgs["base"]; L.new(tb.outputs[0], bs.inputs["Base Color"])
    tm = N.new("ShaderNodeTexImage"); tm.image = mri; sp = N.new("ShaderNodeSeparateColor"); L.new(tm.outputs[0], sp.inputs[0])
    L.new(sp.outputs[1], bs.inputs["Roughness"]); L.new(sp.outputs[2], bs.inputs["Metallic"])
    tn = N.new("ShaderNodeTexImage"); tn.image = imgs["normal"]; nm = N.new("ShaderNodeNormalMap"); L.new(tn.outputs[0], nm.inputs["Color"]); L.new(nm.outputs[0], bs.inputs["Normal"])
    for i, im in enumerate([imgs["base"], mri, imgs["normal"]]):
        im.filepath_raw = os.path.join(OUT, im.name + (".jpg" if im is imgs["base"] else ".png")); im.file_format = "JPEG" if im is imgs["base"] else "PNG"; im.save()
    body.data.materials.clear(); body.data.materials.append(fm)
    for o in bpy.context.selected_objects: o.select_set(False)
    for o in [body] + glassy: o.select_set(True)
    path = os.path.join(OUT, pid + ".glb")
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True, export_apply=True, export_yup=True,
                              export_image_format="AUTO", export_materials="EXPORT")
    tris = sum(len(p.vertices) - 2 for o in [body] + glassy for p in o.data.polygons)
    print("PR1 exported", pid, "tris", tris, "->", path)

# ------------------------------------------------------------------ props
PROPS = {}
def prop(fn): PROPS[fn.__name__] = fn; return fn

def rbox_hollow_front(size, c, mat, inset_mat, depth=0.012, border=0.03, face="+z", bevel=0.01):
    """a panel with a recessed field: the frame as four bevelled bars round an inset plate"""
    w, h = size
    x, y, z = c
    s = 1 if face == "+z" else -1
    box((w, border, depth * 2), (x, y + h / 2 - border / 2, z), mat, bevel * 0.5)
    box((w, border, depth * 2), (x, y - h / 2 + border / 2, z), mat, bevel * 0.5)
    box((border, h - 2 * border, depth * 2), (x - w / 2 + border / 2, y, z), mat, bevel * 0.5)
    box((border, h - 2 * border, depth * 2), (x + w / 2 - border / 2, y, z), mat, bevel * 0.5)
    box((w - 2 * border, h - 2 * border, 0.004), (x, y, z - s * depth * 0.5), inset_mat, 0.001)

@prop
def pr1_fuel_dispenser():
    """a generic two-sided fuel dispenser (one per island position of the petrol station), 0.96 x 2.35 x 0.64 m"""
    body = paint("pr1_body", (0.82, 0.8, 0.74), wear=0.35, rust=0.3, dirt=0.55, gloss=0.55)
    red = paint("pr1_red", (0.58, 0.07, 0.05), wear=0.4, rust=0.25, dirt=0.45, gloss=0.6)
    dark = paint("pr1_dark", (0.07, 0.075, 0.08), wear=0.3, rust=0.15, dirt=0.4, gloss=0.5)
    steel = metal("pr1_steel", (0.6, 0.6, 0.58), 0.3, True, 0.55, rust=0.35)
    chrome = metal("pr1_chrome", (0.75, 0.75, 0.74), 0.18, False, 0.3)
    rub = rubber("pr1_rubber")
    blk = plastic("pr1_black", (0.03, 0.03, 0.032), 0.45, 0.3)
    grey = plastic("pr1_grey", (0.32, 0.33, 0.34), 0.5, 0.35)
    lcdm = lcd("pr1_lcd", (0.08, 0.1, 0.085))
    seg = printed("pr1_seg", (0.55, 0.62, 0.5), 0.3, 0.1)
    wht = printed("pr1_label_w", (0.85, 0.85, 0.82)); ink = printed("pr1_ink", (0.06, 0.06, 0.06), 0.5, 0.2)
    lred = printed("pr1_label_r", (0.6, 0.06, 0.04)); lyel = printed("pr1_label_y", (0.85, 0.65, 0.08)); lgrn = printed("pr1_label_g", (0.1, 0.42, 0.18))
    lblu = printed("pr1_label_b", (0.08, 0.22, 0.55))
    gl = glass_mat((0.5, 0.58, 0.58), 0.22)
    W, D = 0.84, 0.5   # cabinet width (x) and depth (z)
    # plinth: stainless skirt on a concrete pad with anchor bolts
    box((0.96, 0.1, 0.64), (0, 0.05, 0), concrete("pr1_conc", (0.55, 0.53, 0.5), 0.7), 0.012, 3)
    box((0.9, 0.12, 0.58), (0, 0.16, 0), steel, 0.008, 2)
    for sx in (-0.4, 0.4):
        for sz in (-0.24, 0.24):
            cyl(0.012, 0.02, (sx, 0.225, sz), steel, segs=6, bevel=0.002); cyl(0.006, 0.03, (sx, 0.235, sz), steel, segs=12)
    # lower cabinet: rounded vertical edges, recessed doors on both faces with louvres, lock and hinges
    box((W, 0.72, D), (0, 0.58, 0), body, 0.035, 4)
    box((W + 0.01, 0.08, D + 0.01), (0, 0.26, 0), steel, 0.01, 2)   # kick plate
    for f in (1, -1):
        zf = f * (D / 2 + 0.001); fc = "+z" if f > 0 else "-z"
        rbox_hollow_front((0.62, 0.5), (0, 0.6, zf + f * 0.006), body, body, 0.012, 0.025, fc, 0.006)
        for k in range(6):   # louvres
            box((0.4, 0.012, 0.012), (0, 0.42 + k * 0.025, zf + f * 0.004), dark, 0.003, 1, rot=(f * 35, 0, 0))
        cyl(0.012, 0.012, (0.24, 0.78, zf + f * 0.008), chrome, axis="z", segs=16, bevel=0.002)   # lock
        box((0.004, 0.01, 0.003), (0.24, 0.78, zf + f * 0.0145), ink)
        for hy in (0.45, 0.75):   # hinges
            cyl(0.008, 0.05, (-0.305, hy, zf + f * 0.01), steel, segs=10)
        for sx in (-0.38, 0.38):
            for sy in (0.29, 0.9):
                cyl(0.005, 0.004, (sx, sy, zf + f * 0.002), chrome, axis="z", segs=8)
        # hazard sticker and a serial plate
        box((0.12, 0.05, 0.0012), (-0.2, 0.86, zf + f * 0.0012), lyel, 0.0005)
        text("DANGER", 0.016, (-0.2, 0.866, zf + f * 0.002), ink, fc, 0.0005)
        text("FLAMMABLE", 0.011, (-0.2, 0.846, zf + f * 0.002), ink, fc, 0.0004)
        box((0.09, 0.04, 0.0012), (0.2, 0.33, zf + f * 0.0012), steel, 0.0005)
        text("MOD 4/2  N 27831", 0.0055, (0.2, 0.337, zf + f * 0.0024), ink, fc, 0.0003)
        text("220-240V 50HZ", 0.0055, (0.2, 0.325, zf + f * 0.0024), ink, fc, 0.0003)
    # head: display, keypad, grade buttons, card reader under a bevelled cap
    box((W, 0.62, D - 0.02), (0, 1.25, 0), body, 0.03, 4)
    box((W + 0.02, 0.025, D + 0.0), (0, 0.95, 0), dark, 0.008, 2)   # seam band
    for f in (1, -1):
        zf = f * ((D - 0.02) / 2); fc = "+z" if f > 0 else "-z"
        # display bezel with three windows of digits behind glass
        box((0.56, 0.3, 0.02), (-0.06, 1.33, zf + f * 0.006), dark, 0.012, 3)
        rows = [("TOTAL", "47.32"), ("LITRES", "31.25"), ("PRICE/L", "1.514")]
        for k, (lab, num) in enumerate(rows):
            y = 1.42 - k * 0.09
            box((0.34, 0.065, 0.004), (0.02, y, zf + f * 0.015), lcdm, 0.002)
            text(num, 0.05, (0.165, y - 0.002, zf + f * 0.0175), seg, fc, 0.0006, align="RIGHT")
            text(lab, 0.018, (-0.27, y, zf + f * 0.0175), wht, fc, 0.0005, align="LEFT")
        box((0.5, 0.27, 0.003), (-0.06, 1.33, zf + f * 0.0195), gl, 0.0)
        # grade buttons
        for k, (g, m) in enumerate((("UNLEADED 95", lgrn), ("SUPER 98", lblu), ("DIESEL", ink))):
            x = -0.27 + k * 0.17
            box((0.15, 0.06, 0.016), (x, 1.12, zf + f * 0.008), grey, 0.008, 3)
            box((0.13, 0.04, 0.003), (x, 1.12, zf + f * 0.017), m, 0.002)
            text(g, 0.0135, (x, 1.12, zf + f * 0.0187), wht, fc, 0.0004)
        # keypad and card slot
        box((0.12, 0.16, 0.02), (0.3, 1.33, zf + f * 0.006), blk, 0.008, 2)
        for r in range(4):
            for c in range(3):
                box((0.026, 0.022, 0.008), (0.265 + c * 0.035, 1.38 - r * 0.03, zf + f * 0.019), grey, 0.004, 2)
                text("123456789*0#"[r * 3 + c], 0.012, (0.265 + c * 0.035, 1.38 - r * 0.03, zf + f * 0.0232), wht, fc, 0.0003)
        box((0.1, 0.018, 0.012), (0.3, 1.45, zf + f * 0.012), blk, 0.004)
        box((0.07, 0.003, 0.004), (0.3, 1.45, zf + f * 0.0185), ink)
        box((0.16, 0.05, 0.0012), (0.3, 1.09, zf + f * 0.0011), lred, 0.0005)
        text("STOP ENGINE", 0.011, (0.3, 1.096, zf + f * 0.0021), wht, fc, 0.0004)
        text("NO SMOKING", 0.011, (0.3, 1.08, zf + f * 0.0021), wht, fc, 0.0004)
        text("PAY AT KIOSK", 0.016, (-0.06, 1.515, zf + f * 0.002), ink, fc, 0.0005)
    # top housing: red band and a rounded crown with the grade words on both faces
    box((0.9, 0.3, 0.56), (0, 1.72, 0), red, 0.06, 6)
    box((0.86, 0.04, 0.52), (0, 1.885, 0), red, 0.03, 4)
    box((0.92, 0.03, 0.58), (0, 1.57, 0), steel, 0.012, 2)
    for f in (1, -1):
        fc = "+z" if f > 0 else "-z"
        box((0.6, 0.12, 0.002), (0, 1.73, f * 0.281), printed("pr1_band_w", (0.88, 0.87, 0.82), 0.4, 0.4), 0.001)
        text("FUEL", 0.085, (0, 1.725, f * 0.2825), red, fc, 0.001, bold=True)
    # nozzle columns on both ends: holsters with nozzles, hoses looping from the top with breakaway couplings
    for sx in (-1, 1):
        x = sx * (W / 2 + 0.035)
        box((0.07, 1.18, 0.26), (x, 1.04, 0), body, 0.02, 3)
        for f in (1, -1):
            z = f * 0.08; zf = z + f * 0.03
            # holster: a boot with a lip
            box((0.06, 0.16, 0.06), (x + sx * 0.055, 1.08, z), blk, 0.012, 3)
            box((0.07, 0.02, 0.07), (x + sx * 0.055, 1.165, z), blk, 0.008, 2)
            # nozzle: grip body, trigger and guard, spout down into the boot
            nx = x + sx * 0.06
            box((0.045, 0.14, 0.035), (nx, 1.24, z), blk, 0.014, 4, rot=(0, 0, -sx * 12))
            box((0.012, 0.08, 0.026), (nx + sx * 0.028, 1.22, z), steel, 0.004, 2, rot=(0, 0, -sx * 12))   # trigger lever
            tube([(nx + sx * 0.015, 1.31, z), (nx + sx * 0.04, 1.27, z), (nx + sx * 0.045, 1.19, z)], 0.006, steel)   # guard
            cyl(0.011, 0.11, (nx - sx * 0.005, 1.13, z), chrome, segs=14)   # spout into the boot
            cyl(0.02, 0.03, (nx - sx * 0.012, 1.325, z), rub, segs=14, bevel=0.003)   # swivel
            # hose: from the swivel up over a hook at the top and back into the column, sagging between
            top = (x + sx * 0.04, 1.62, z)
            pts = [(nx - sx * 0.012, 1.34, z), (nx + sx * 0.05, 1.42, z + f * 0.03), (x + sx * 0.18, 1.2, z + f * 0.09),
                   (x + sx * 0.2, 0.9, z + f * 0.12), (x + sx * 0.12, 1.15, z + f * 0.06), (x + sx * 0.09, 1.5, z + f * 0.02), top]
            tube(pts, 0.013, rub, res=14, segs=12)
            cyl(0.02, 0.06, (x + sx * 0.04, 1.6, z), steel, segs=14, bevel=0.003)   # breakaway coupling
            for k in range(3): cyl(0.022, 0.006, (x + sx * 0.04, 1.585 + k * 0.012, z), steel, segs=14)
            box((0.05, 0.03, 0.04), (x + sx * 0.03, 1.66, z), steel, 0.006, 2)   # hose retractor hook
    return {"tex": 1024}

# ------------------------------------------------------------------ run
for pid in WANT:
    if pid not in PROPS: print("PR1 unknown prop", pid); continue
    reset(); SEED[0] = 0
    opts = PROPS[pid]() or {}
    bpy.context.scene.cursor.location = (0, 0, 0)
    bake_and_export(pid, opts.get("tex", 1024))
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, pid + ".blend"))
print("PR1 done")
