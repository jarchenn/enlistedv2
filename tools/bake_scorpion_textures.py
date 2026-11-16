# WP2: a first-person texture set for the CZ Scorpion EVO 3 A1 rig. The supplied set has no textures for the gun (only a UV
# checker), so the exporter gave it one flat near-black colour. This bakes a procedural finish onto the gun's own UVs:
#   base colour  dark polymer / steel with a slow tonal drift, darker in the cavities (AO), lighter worn edges
#   roughness    0.55-0.65 matte polymer, handled edges smoother, cavities rougher
#   normal       bevelled edges (1.2 mm Bevel node) plus a fine stipple grain on the polymer
#   metal        0.12, worn edges toward bare aluminium (baked as emission)
# The gun's UVs use two UDIM tiles (u 0..2); glTF has no UDIM, so u is halved and the maps are 4096 x 2048 (2K per tile).
#   blender -b --factory-startup --python tools/bake_scorpion_textures.py -- <CZ_Scorpion_Evo3_A1_Anim.blend> <out dir>
# Writes scorpion_base.png (sRGB), scorpion_rough.png, scorpion_metal.png, scorpion_normal.png (OpenGL tangent space) into <out dir>, and
# <out dir>/scorpion_uv_halved.blend: the source with the gun's UVs halved (tools/modern_guns_anim_config.cjs reads it).
import bpy, sys, os, math
a = sys.argv[sys.argv.index("--") + 1:]; src, out = a[0], a[1]
os.makedirs(out, exist_ok=True)
bpy.ops.wm.open_mainfile(filepath=src, load_ui=False)
g = bpy.data.objects["CZScorpion"]; me = g.data
uv = me.uv_layers["UVMap"]
for d in uv.data: d.uv.x *= 0.5   # two UDIM tiles -> one texture
for m in g.modifiers: m.show_render = m.show_viewport = False   # bake in the rest pose
for o in bpy.data.objects:
    if o != g: o.hide_render = True
sc = bpy.context.scene; sc.render.engine = "CYCLES"; sc.cycles.device = "CPU"; sc.cycles.samples = 24
sc.render.bake.margin = 12; sc.render.bake.use_selected_to_active = False
W, H = 4096, 2048
# ---- the procedural finish
mat = bpy.data.materials.new("wp2_scorpion"); mat.use_nodes = True; nt = mat.node_tree; N, L = nt.nodes, nt.links
for n in list(N):
    if n.type != "OUTPUT_MATERIAL": N.remove(n)
outn = next(n for n in N if n.type == "OUTPUT_MATERIAL")
bs = N.new("ShaderNodeBsdfPrincipled"); L.new(bs.outputs[0], outn.inputs[0])
tc = N.new("ShaderNodeTexCoord")
def noise(scale, detail=4.0, rough=0.55):
    n = N.new("ShaderNodeTexNoise"); n.inputs["Scale"].default_value = scale; n.inputs["Detail"].default_value = detail; n.inputs["Roughness"].default_value = rough
    L.new(tc.outputs["Object"], n.inputs["Vector"]); return n
def math_(op, x, y=None, clamp=False):
    m = N.new("ShaderNodeMath"); m.operation = op; m.use_clamp = clamp
    for k, v in enumerate((x, y)):
        if v is None: continue
        if isinstance(v, (int, float)): m.inputs[k].default_value = v
        else: L.new(v, m.inputs[k])
    return m.outputs[0]
# edge mask: how far the 0.6 mm bevelled normal turns from the face normal (convex and concave edges), broken up by noise
bev = N.new("ShaderNodeBevel"); bev.inputs["Radius"].default_value = 0.0012; bev.samples = 8
geo = N.new("ShaderNodeNewGeometry")
dp = N.new("ShaderNodeVectorMath"); dp.operation = "DOT_PRODUCT"; L.new(bev.outputs[0], dp.inputs[0]); L.new(geo.outputs["Normal"], dp.inputs[1])
edge = math_("MULTIPLY", math_("SUBTRACT", 1.0, dp.outputs["Value"]), 14.0, True)
ao = N.new("ShaderNodeAmbientOcclusion"); ao.samples = 16; ao.inputs["Distance"].default_value = 0.012; ao.only_local = True
convex = math_("MULTIPLY", edge, math_("POWER", ao.outputs["AO"], 3.0), True)   # wear only where the edge is exposed (not in a slot)
brk = noise(90.0, 6.0, 0.6)
wear = math_("MULTIPLY", convex, math_("MULTIPLY", math_("SUBTRACT", brk.outputs["Fac"], 0.22), 2.6, True), True)
# base colour: polymer black-grey with a slow drift, darker in cavities, worn edges toward bare grey
def stretch(sock, lo=0.35, hi=0.65):   # noise output clusters near 0.5: spread it over 0..1
    r = N.new("ShaderNodeMapRange"); r.inputs["From Min"].default_value = lo; r.inputs["From Max"].default_value = hi; r.clamp = True
    L.new(sock, r.inputs["Value"]); return r.outputs["Result"]
drift = math_("ADD", math_("MULTIPLY", stretch(noise(14.0, 3.0, 0.5).outputs["Fac"]), 0.6), math_("MULTIPLY", stretch(noise(55.0, 4.0, 0.6).outputs["Fac"]), 0.4))
base = N.new("ShaderNodeMix"); base.data_type = "RGBA"; base.blend_type = "MIX"
base.inputs[6].default_value = (0.036, 0.037, 0.036, 1); base.inputs[7].default_value = (0.082, 0.080, 0.075, 1)
L.new(drift, base.inputs[0])
grain = N.new("ShaderNodeMix"); grain.data_type = "RGBA"; grain.blend_type = "MULTIPLY"; grain.inputs[0].default_value = 1.0
gr = math_("ADD", 0.88, math_("MULTIPLY", stretch(noise(900.0, 2.0, 0.5).outputs["Fac"]), 0.24))
L.new(base.outputs[2], grain.inputs[6]); cg = N.new("ShaderNodeCombineColor"); [L.new(gr, cg.inputs[k]) for k in range(3)]; L.new(cg.outputs[0], grain.inputs[7])
cav = N.new("ShaderNodeMix"); cav.data_type = "RGBA"; cav.blend_type = "MULTIPLY"; cav.inputs[0].default_value = 1.0
L.new(grain.outputs[2], cav.inputs[6]); aoc = math_("ADD", math_("MULTIPLY", ao.outputs["AO"], 0.6), 0.4); L.new(aoc, cav.inputs[7])
worn = N.new("ShaderNodeMix"); worn.data_type = "RGBA"; L.new(wear, worn.inputs[0]); L.new(cav.outputs[2], worn.inputs[6]); worn.inputs[7].default_value = (0.30, 0.295, 0.28, 1)
L.new(worn.outputs[2], bs.inputs["Base Color"])
# roughness
rgh = math_("ADD", 0.48, math_("MULTIPLY", stretch(noise(60.0, 5.0, 0.6).outputs["Fac"]), 0.2))
rgh = math_("SUBTRACT", rgh, math_("MULTIPLY", wear, 0.3))
rgh = math_("ADD", rgh, math_("MULTIPLY", math_("SUBTRACT", 1.0, ao.outputs["AO"]), 0.15), True)
L.new(rgh, bs.inputs["Roughness"])
# normal: the bevel plus a fine polymer stipple
bump = N.new("ShaderNodeBump"); bump.inputs["Strength"].default_value = 0.12; bump.inputs["Distance"].default_value = 0.0002
L.new(noise(2600.0, 2.0, 0.5).outputs["Fac"], bump.inputs["Height"]); L.new(bev.outputs[0], bump.inputs["Normal"])
L.new(bump.outputs[0], bs.inputs["Normal"])
orig = list(me.materials)
me.materials.clear(); me.materials.append(mat)
# ---- bake targets
def target(name, colour):
    im = bpy.data.images.new(name, W, H, alpha=False, float_buffer=False); im.colorspace_settings.name = "sRGB" if colour else "Non-Color"
    for n in N: n.select = False
    t = N.new("ShaderNodeTexImage"); t.image = im; t.select = True; N.active = t
    return im, t
bpy.context.view_layer.objects.active = g
for o in bpy.data.objects: o.select_set(o == g)
def bake(kind, name, colour, **kw):
    im, t = target(name, colour)
    bpy.ops.object.bake(type=kind, **kw)
    im.filepath_raw = os.path.join(out, name + ".png"); im.file_format = "PNG"; im.save()
    N.remove(t); print("WP2 baked", name, im.size[0], im.size[1])
bake("DIFFUSE", "scorpion_base", True, pass_filter={"COLOR"})
bake("ROUGHNESS", "scorpion_rough", False)
bake("NORMAL", "scorpion_normal", False, normal_space="TANGENT")
em = N.new("ShaderNodeEmission"); L.new(math_("ADD", 0.12, math_("MULTIPLY", wear, 0.7), True), em.inputs["Strength"]); em.inputs["Color"].default_value = (1, 1, 1, 1)
L.new(em.outputs[0], outn.inputs[0])
bake("EMIT", "scorpion_metal", False)
# the source with halved UVs and the original materials kept for the exporter (the baked material is not saved into it)
me.materials.clear(); bpy.data.materials.remove(mat)
for m in orig: me.materials.append(m)
for m in g.modifiers: m.show_render = m.show_viewport = True
for o in bpy.data.objects: o.hide_render = False
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out, "scorpion_uv_halved.blend"), copy=True)
print("WP2 done")
