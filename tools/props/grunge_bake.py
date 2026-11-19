# TX6: grunge wear baked into imported models' own textures (paint chips, edge wear, dirt), the recipe piloted in the
# user's Blender (scene NMK_GrungePilot; docs/textures.md#tx6). Per material of content/assets/models/<id>/:
#   edge wear   AO (inside, 6 cm) + noise -> ramp: exposed edges, broken up
#   chips       a grunge mask, box-projected in object space at a seeded offset / turn -> ramp by strength
#               metal: paint chipped to rust; wood: to bare wood; generic: scuffed lighter; masonry: none
#   dirt        an inverted grunge mask x a height band (dirtiest at the foot) and crevice AO; masonry also cracks
#               and run-off streaks
#   roughness   to 0.95 where worn or dirty; metal goes to 0.3 where the paint is chipped to rust
# Bakes EMIT into new images the size of the originals in the model's own UVs: <albedo stem>_g.jpg and <mr stem>_g.jpg,
# written next to the originals (and into lod1/ when it has the same files). model.json then names the baked files and
# keeps the originals in "grungeSrc", so a re-run starts from the originals again (repeatable).
#   blender -b --factory-startup --python tools/props/grunge_bake.py -- --masks <dir> [--strength S] [--res-cap N] id [id ...]
#   --config tools/props/grunge_assets.json (default): per-asset {"strength": s, "kinds": {material: kind}, "skip": bool};
#   "materialKinds" (material name -> kind, every asset), "prefixes" (id prefix -> {"strength"}), "skipAssets" (id regexes)
#   --list <file>: bake the ids in a file (one per line) instead of the config's assets
#   --restore: put the originals back (model.json only; the baked files stay on disk)
# Masks: Texturelabs grunge images (local only, never committed: docs/credits.md). Kinds: metal, wood, masonry, generic, skip.
import bpy, sys, os, json, struct, array, math, re, shutil

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
opt = {"masks": os.path.join(os.path.dirname(ROOT), "incoming", "grunge_texturelabs"), "strength": None, "config": os.path.join(HERE, "grunge_assets.json"),
       "restore": False, "res-cap": 2048, "samples": 8, "models": os.path.join(ROOT, "content", "assets", "models")}
ids = []
i = 0
while i < len(argv):
    a = argv[i]
    if a == "--restore": opt["restore"] = True
    elif a.startswith("--"): opt[a[2:]] = argv[i + 1]; i += 1
    else: ids.append(a)
    i += 1
cfg = json.load(open(opt["config"], encoding="utf-8")) if os.path.exists(opt["config"]) else {}
if opt.get("list"): ids += [l.strip() for l in open(opt["list"], encoding="utf-8") if l.strip() and not l.startswith("#")]
if not ids: ids = [k for k in cfg.get("assets", {})]

CHIP = {"metal": ["293", "331", "338i"], "wood": ["342", "293", "338i"], "generic": ["331", "338i", "293"]}
DIRT = ["308", "327", "348", "323"]
SKIP_RE = re.compile(r"glass|window|optic|lamp|light|emiss|flame|leaf|leaves|foliage|plant|grass|cloth|fabric|curtain|sack|juta|cushion|pillow|mattres|matress|blanket|sofa|soft|leather|tyre_side|decal|screen|artwork|mirror", re.I)
KIND_RE = [("wood", re.compile(r"wood|plank|crate|pallet|timber|board|ladder|wine_barrel|bucket|chest|shelf|stool|commode|dresser|nightstand|piano|clock|gothic", re.I)),
           ("masonry", re.compile(r"wall|brick|concrete|cement|plaster|stone|render|block", re.I)),
           ("metal", re.compile(r"metal|steel|iron|locker|barrel|drum|container|generator|shel|rack|pipe|door|body|tank|cylinder|vent|lpg|sign|post|table", re.I))]


def fnv(s):
    h = 2166136261
    for ch in s.encode(): h = ((h ^ ch) * 16777619) & 0xFFFFFFFF
    return h


def rnd(seed, k):   # deterministic 0..1 per (seed, k)
    return (fnv(f"{seed}:{k}") % 100000) / 100000.0


def setup_gpu():
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"; sc.cycles.samples = int(opt["samples"]); sc.render.bake.margin = 8
    try:
        p = bpy.context.preferences.addons["cycles"].preferences
        for t in ("OPTIX", "CUDA"):
            try:
                p.compute_device_type = t; p.get_devices()
                if any(d.type == t for d in p.devices):
                    for d in p.devices: d.use = d.type == t
                    sc.cycles.device = "GPU"; print("grunge: GPU", t); return
            except TypeError: pass
    except Exception as e: print("grunge: no GPU,", e)
    sc.cycles.device = "CPU"


mask_cache = {}
def mask_img(code):
    name = "Texturelabs_Grunge_%sM.jpg" % code.rstrip("i")
    if name not in mask_cache:
        im = bpy.data.images.load(os.path.join(opt["masks"], name)); im.colorspace_settings.name = "Non-Color"; mask_cache[name] = im
    return mask_cache[name]


def clear_scene():
    for o in list(bpy.data.objects): bpy.data.objects.remove(o, do_unlink=True)
    for m in list(bpy.data.meshes): bpy.data.meshes.remove(m)
    for m in list(bpy.data.materials): bpy.data.materials.remove(m)
    for im in list(bpy.data.images):
        if im.name not in mask_cache: bpy.data.images.remove(im)


def load_mesh(d):
    b = open(os.path.join(d, "mesh.bin"), "rb").read()
    tag, nv, ni = struct.unpack_from("<3I", b, 0)
    assert tag == 0x31574D4E, "bad mesh.bin"
    f = array.array("f"); f.frombytes(b[12:12 + nv * 48])
    I = array.array("I"); I.frombytes(b[12 + nv * 48:12 + nv * 48 + ni * 4])
    return f, I


def kind_of(mname, mj, a_id, over):
    if mname in over: return over[mname]
    gk = cfg.get("materialKinds", {})
    if mname in gk: return gk[mname]
    if not mj.get("albedo"): return "skip"
    if SKIP_RE.search(mname): return "skip"
    for k, rx in KIND_RE:
        if rx.search(mname): return k
    for k, rx in KIND_RE:
        if rx.search(a_id): return k
    return "generic"


class G:   # node-graph helper
    def __init__(s, mat): s.nt = mat.node_tree; s.N = s.nt.nodes; s.L = s.nt.links; s.tc = s.N.new("ShaderNodeTexCoord")
    def link(s, a, b):
        if isinstance(a, (int, float)): b.default_value = (a, a, a, 1.0) if b.type == "RGBA" else a
        else: s.L.new(a, b)
    def math(s, op, x, y=None, clamp=False):
        m = s.N.new("ShaderNodeMath"); m.operation = op; m.use_clamp = clamp
        s.link(x, m.inputs[0])
        if y is not None: s.link(y, m.inputs[1])
        return m.outputs[0]
    def ramp(s, x, a, b, ta=0.0, tb=1.0):   # map x from [a, b] to [ta, tb], clamped
        m = s.N.new("ShaderNodeMapRange"); m.clamp = True
        s.link(x, m.inputs["Value"]); m.inputs["From Min"].default_value = a; m.inputs["From Max"].default_value = b
        m.inputs["To Min"].default_value = ta; m.inputs["To Max"].default_value = tb
        return m.outputs["Result"]
    def mix(s, f, a, b):
        m = s.N.new("ShaderNodeMix"); m.data_type = "RGBA"; m.clamp_factor = True
        s.link(f, m.inputs["Factor"]); s.link(a, m.inputs[6]); s.link(b, m.inputs[7])
        return m.outputs[2]
    def rgb(s, c):
        n = s.N.new("ShaderNodeRGB"); n.outputs[0].default_value = (c[0], c[1], c[2], 1.0); return n.outputs[0]
    def mask(s, code, scale, seed, k):   # box-projected grunge mask in object space, seeded offset and turn
        mp = s.N.new("ShaderNodeMapping"); s.L.new(s.tc.outputs["Object"], mp.inputs["Vector"])
        mp.inputs["Scale"].default_value = (scale, scale, scale)
        mp.inputs["Location"].default_value = (rnd(seed, k + "x") * 7.0, rnd(seed, k + "y") * 7.0, rnd(seed, k + "z") * 7.0)
        mp.inputs["Rotation"].default_value = (0.0, 0.0, rnd(seed, k + "r") * math.tau)
        t = s.N.new("ShaderNodeTexImage"); t.image = mask_img(code); t.projection = "BOX"; t.projection_blend = 0.25; t.extension = "REPEAT"
        s.L.new(mp.outputs["Vector"], t.inputs["Vector"])
        v = t.outputs["Color"]
        bw = s.N.new("ShaderNodeRGBToBW"); s.L.new(v, bw.inputs[0]); v = bw.outputs[0]
        return s.math("SUBTRACT", 1.0, v) if code.endswith("i") else v
    def uvimg(s, im):
        t = s.N.new("ShaderNodeTexImage"); t.image = im; t.interpolation = "Linear"
        uv = s.N.new("ShaderNodeUVMap"); s.L.new(uv.outputs[0], t.inputs["Vector"]); return t


def build(mat, kind, alb, mr, aoInMR, seed, strength):
    g = G(mat); N = g.N
    for n in list(N):
        if n not in (g.tc,): N.remove(n)
    g.tc = N.new("ShaderNodeTexCoord")
    out = N.new("ShaderNodeOutputMaterial"); em = N.new("ShaderNodeEmission"); g.L.new(em.outputs[0], out.inputs[0])
    base = g.uvimg(alb).outputs["Color"]
    if mr is not None:
        sep = N.new("ShaderNodeSeparateColor"); g.L.new(g.uvimg(mr).outputs["Color"], sep.inputs[0])
        mR, mG, mB = sep.outputs[0], sep.outputs[1], sep.outputs[2]
    s = strength
    # edge wear (recipe 1)
    ao_in = N.new("ShaderNodeAmbientOcclusion"); ao_in.inside = True; ao_in.only_local = True; ao_in.samples = 8; ao_in.inputs["Distance"].default_value = 0.06
    nz = N.new("ShaderNodeTexNoise"); g.L.new(g.tc.outputs["Object"], nz.inputs["Vector"])
    nz.inputs["Scale"].default_value = 9.0; nz.inputs["Detail"].default_value = 8.0; nz.inputs["Roughness"].default_value = 0.75
    edge = g.ramp(g.math("MULTIPLY", g.math("ADD", ao_in.outputs["AO"], nz.outputs["Fac"]), 0.5), 0.50 - 0.04 * (s - 1), 0.62 - 0.04 * (s - 1), 1.0, 0.0)
    # dirt (recipe 3): inverted mask x height band, max with crevice AO
    dcode = DIRT[fnv(seed + "d") % len(DIRT)]
    dm = g.math("SUBTRACT", 1.0, g.mask(dcode, 0.35, seed, "d"))
    sz = N.new("ShaderNodeSeparateXYZ"); g.L.new(g.tc.outputs["Object"], sz.inputs[0])
    band = g.ramp(sz.outputs["Z"], 0.0, 0.9, 0.9, 0.35)
    ao_c = N.new("ShaderNodeAmbientOcclusion"); ao_c.only_local = True; ao_c.samples = 8; ao_c.inputs["Distance"].default_value = 0.25
    dirt = g.math("MAXIMUM", g.math("MULTIPLY", dm, band), g.math("SUBTRACT", 1.0, ao_c.outputs["AO"]))
    dirt = g.math("MULTIPLY", g.ramp(dirt, 0.15, 0.85), 0.85 * s, clamp=True)
    col = base
    worn = None
    if kind in ("metal", "wood", "generic"):
        codes = CHIP[kind]; ccode = codes[fnv(seed + "c") % len(codes)]
        lo = 0.75 - 0.3 * s
        raw = g.mask(ccode, 0.45 + 0.55 * rnd(seed, "cs"), seed, "c")   # chip size varies by asset (mask tile ~1-2 m)
        chip = g.ramp(raw, lo, lo + 0.3)
        edgeB = g.math("MULTIPLY", edge, g.ramp(raw, lo - 0.3, lo))   # edges wear only where the mask already leans to a chip (thin parts are all "edge")
        worn = g.math("MAXIMUM", g.math("MULTIPLY", edgeB, 0.9 if kind != "generic" else 0.6), chip)
        if kind == "metal":   # paint chipped to rust, two rust tones by a slow noise
            rn = N.new("ShaderNodeTexNoise"); g.L.new(g.tc.outputs["Object"], rn.inputs["Vector"]); rn.inputs["Scale"].default_value = 3.0
            bare = g.mix(rn.outputs["Fac"], g.rgb((0.14, 0.085, 0.06)), g.rgb((0.075, 0.055, 0.045)))   # muted rust (reads as rust, not orange paint)
        elif kind == "wood":
            bare = g.rgb((0.30, 0.21, 0.13))
        else:   # scuffed: the surface lighter and greyer
            bare = g.mix(0.5, g.math("MULTIPLY", base, 1.4), g.rgb((0.35, 0.34, 0.32)))
        col = g.mix(worn, col, bare)
    else:   # masonry: cracks and run-off streaks, no chips
        cr = g.ramp(g.mask("312", 0.4, seed, "k"), 0.55, 0.25)   # black hairlines -> 1
        col = g.mix(g.math("MULTIPLY", cr, 0.7 * s, clamp=True), col, g.math("MULTIPLY", base, 0.4))
        st = g.ramp(g.mask("293", 0.3, seed, "s"), 0.35, 0.8)   # vertical streaks
        dirt = g.math("MAXIMUM", dirt, g.math("MULTIPLY", st, 0.75 * s), clamp=True)
    col = g.mix(dirt, col, g.rgb((0.03, 0.024, 0.018)))
    g.L.new(col, em.inputs["Color"])
    nodes = {"col": col}
    if mr is not None:
        wd = dirt if worn is None else g.math("MAXIMUM", dirt, worn)
        rough = g.mix(wd, mG, 0.95)
        metal = g.mix(worn, mB, 0.3) if kind == "metal" else mB   # rust is not metal
        cmb = N.new("ShaderNodeCombineColor"); g.L.new(mR, cmb.inputs[0])
        for ch, v in ((1, rough), (2, metal)):
            bw = N.new("ShaderNodeRGBToBW"); g.L.new(v, bw.inputs[0]); g.L.new(bw.outputs[0], cmb.inputs[ch])
        nodes["mr"] = cmb.outputs[0]
    return g, em, nodes


def bake_target(name, src, colorspace, cap):
    w, h = src.size
    sc = min(1.0, cap / max(w, h))
    im = bpy.data.images.new(name, max(4, int(w * sc)), max(4, int(h * sc)), alpha=True)
    im.colorspace_settings.name = colorspace
    if sc == 1.0:   # start from the original, so texels outside the UV islands keep its padding (no black bleeding into mips)
        px = array.array("f", [0.0]) * (w * h * 4); src.pixels.foreach_get(px); im.pixels.foreach_set(px)
    return im


def do_asset(a_id):
    d = os.path.join(opt["models"], a_id)
    jp = os.path.join(d, "model.json")
    j = json.load(open(jp, encoding="utf-8"))
    ac = cfg.get("assets", {}).get(a_id, {})
    if opt["restore"]:
        for m in j.get("materials", []):
            src = m.pop("grungeSrc", None)
            if src: m.update(src)
        j.pop("grunge", None)
        json.dump(j, open(jp, "w", encoding="utf-8"), indent=1); restore_lod(d, j); print(a_id, "restored"); return
    for pat in cfg.get("skipAssets", []):
        if re.fullmatch(pat, a_id): print(a_id, "skipped (skipAssets %s)" % pat); return
    if ac.get("skip"): print(a_id, "skipped (config)"); return
    pdef = next((v for k, v in cfg.get("prefixes", {}).items() if a_id.startswith(k)), {})
    strength = float(opt["strength"]) if opt["strength"] is not None else float(ac.get("strength", pdef.get("strength", 1.0)))
    over = ac.get("kinds", {})
    clear_scene()
    f, I = load_mesh(d)
    nv = len(f) // 12
    mats = j.get("materials", [])
    kinds, used = {}, {}
    for p in j["parts"]:
        mi = p.get("mat", -1)
        if mi < 0 or mi >= len(mats) or not p.get("textured"): continue
        mj = mats[mi]; src = mj.get("grungeSrc", {})
        k = kind_of(mj.get("name", "m%d" % mi), {**mj, **src}, a_id, over)
        kinds[mj.get("name", "m%d" % mi)] = k
        if k != "skip": used.setdefault(mi, []).append(p)
    # group the baked materials by their (original) albedo file: one bake target per file (atlases shared by materials)
    groups = {}
    for mi in used:
        mj = mats[mi]; src = mj.get("grungeSrc", {})
        groups.setdefault(src.get("albedo", mj["albedo"]), []).append(mi)
    baked = {}
    for albf, mis in groups.items():
        clear_scene()
        verts = [(f[k * 12], -f[k * 12 + 2], f[k * 12 + 1]) for k in range(nv)]
        tris = []; tmat = []
        for slot, mi in enumerate(mis):
            for p in used[mi]:
                for t in range(p["first"], p["first"] + p["count"], 3):
                    tris.append((I[t], I[t + 1], I[t + 2])); tmat.append(slot)
        me = bpy.data.meshes.new(a_id); me.from_pydata(verts, [], tris); me.update()
        uvl = me.uv_layers.new(name="UV")
        lv = array.array("i", [0]) * (len(me.loops)); me.loops.foreach_get("vertex_index", lv)
        uvs = array.array("f", [0.0]) * (len(me.loops) * 2)
        for li, vi in enumerate(lv): uvs[li * 2] = f[vi * 12 + 10]; uvs[li * 2 + 1] = 1.0 - f[vi * 12 + 11]
        uvl.data.foreach_set("uv", uvs)
        me.polygons.foreach_set("material_index", tmat)
        ob = bpy.data.objects.new(a_id, me); bpy.context.scene.collection.objects.link(ob)
        # drop vertices no triangle uses (other parts): keeps object-space AO honest only to this group's faces
        srcA = bpy.data.images.load(os.path.join(d, albf)); srcA.colorspace_settings.name = "sRGB"
        mrf = None
        for mi in mis:
            src = mats[mi].get("grungeSrc", {}); mrf = mrf or src.get("mr", mats[mi].get("mr"))
        srcM = None
        if mrf and os.path.exists(os.path.join(d, mrf)): srcM = bpy.data.images.load(os.path.join(d, mrf)); srcM.colorspace_settings.name = "Non-Color"
        tA = bake_target("A", srcA, "sRGB", int(opt["res-cap"]))
        tM = bake_target("M", srcM, "Non-Color", int(opt["res-cap"])) if srcM else None
        ems = []
        for slot, mi in enumerate(mis):
            mj = mats[mi]; nm = mj.get("name", "m%d" % mi)
            mat = bpy.data.materials.new(nm); mat.use_nodes = True; ob.data.materials.append(mat)
            seed = a_id + ":" + nm
            g, em, nodes = build(mat, kinds[nm], srcA, srcM, mj.get("aoInMR", False), seed, strength)
            tn = g.N.new("ShaderNodeTexImage"); tn.image = tA; g.N.active = tn
            ems.append((g, em, nodes, tn))
        bpy.ops.object.select_all(action="DESELECT"); ob.select_set(True); bpy.context.view_layer.objects.active = ob
        bpy.ops.object.bake(type="EMIT", use_clear=False)
        cut = any(mats[mi].get("cutout") for mi in mis) or albf.lower().endswith(".png")
        if cut and tuple(tA.size) == tuple(srcA.size):   # cutouts: the original alpha back (the bake writes 1), saved as PNG
            n = tA.size[0] * tA.size[1] * 4
            pa = array.array("f", [0.0]) * n; tA.pixels.foreach_get(pa)
            ps = array.array("f", [0.0]) * n; srcA.pixels.foreach_get(ps)
            pa[3::4] = ps[3::4]; tA.pixels.foreach_set(pa)
        stemA = os.path.splitext(albf)[0] + ("_g.png" if cut else "_g.jpg")
        save(tA, os.path.join(d, stemA))
        stemM = None
        if tM:
            for g, em, nodes, tn in ems:
                if "mr" in nodes: g.L.new(nodes["mr"], em.inputs["Color"])
                tn.image = tM
            bpy.ops.object.bake(type="EMIT", use_clear=False)
            stemM = os.path.splitext(mrf)[0] + "_g.jpg"
            save(tM, os.path.join(d, stemM))
        for mi in mis:
            mj = mats[mi]
            src = mj.setdefault("grungeSrc", {})
            src.setdefault("albedo", mj["albedo"]); mj["albedo"] = stemA
            if stemM: src.setdefault("mr", mj["mr"]); mj["mr"] = stemM
        baked[albf] = (stemA, stemM)
    j["grunge"] = {"strength": strength, "kinds": kinds, "tool": "tools/props/grunge_bake.py"}
    json.dump(j, open(jp, "w", encoding="utf-8"), indent=1)
    sync_lod(d, j)
    print(a_id, "baked", json.dumps(kinds), "strength", strength)


def save(im, path):
    im.filepath_raw = path; im.file_format = "PNG" if path.endswith(".png") else "JPEG"
    try: im.save(filepath=path, quality=92)
    except TypeError: im.save()


def sync_lod(d, j):   # lod1/ carries copies of the textures: give it the baked files and the same material names
    ld = os.path.join(d, "lod1"); lp = os.path.join(ld, "model.json")
    if not os.path.exists(lp): return
    lj = json.load(open(lp, encoding="utf-8"))
    for lm, m in zip(lj.get("materials", []), j.get("materials", [])):
        src = m.get("grungeSrc")
        if not src: continue
        for key in ("albedo", "mr"):
            if key in src and lm.get(key) in (src[key], m[key]) and os.path.exists(os.path.join(d, m[key])):
                lm.setdefault("grungeSrc", {}).setdefault(key, src[key]); shutil.copyfile(os.path.join(d, m[key]), os.path.join(ld, m[key])); lm[key] = m[key]
    json.dump(lj, open(lp, "w", encoding="utf-8"), indent=1)


def restore_lod(d, j):
    lp = os.path.join(d, "lod1", "model.json")
    if not os.path.exists(lp): return
    lj = json.load(open(lp, encoding="utf-8"))
    for lm in lj.get("materials", []):
        src = lm.pop("grungeSrc", None)
        if src: lm.update(src)
    json.dump(lj, open(lp, "w", encoding="utf-8"), indent=1)


setup_gpu()
fails = []
for a in ids:
    try: do_asset(a)
    except Exception as e:
        import traceback; traceback.print_exc(); fails.append(a); print(a, "FAILED", e)
print("grunge: %d assets, %d failed %s" % (len(ids), len(fails), " ".join(fails)))
