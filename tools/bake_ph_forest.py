# NT8 Blender (5.x) background script: Poly Haven (CC0) forest models -> game GLBs for the engine's importer.
#   blender -b --factory-startup --disable-autoexec -noaudio --python tools/bake_ph_forest.py -- <incoming> <out> [ids...]
#   <incoming>: Documents/MapMaker/incoming/nt8 (one folder per Poly Haven id, <id>/<id>_1k.gltf; untrusted data, only
#               opened through Blender's glTF importer here)
#   <out>:      writes ph_<asset>.glb (LOD0) and ph_<asset>_lod1.glb, then
#               NormandyMapmaker --import-weapon-models <out> bakes them into content/assets/models/ph_<asset>/
# Trees and saplings arrive with every needle modelled (pine_tree_01: 17 M triangles). They become game trees here:
#   wood (bark, trunk, dead branches) decimated; the twig geometry clustered on a voxel grid, each cluster a card in its
#   own best-fit plane (PCA), textured from an atlas of real needle clusters rendered from the tree itself (Workbench,
#   flat albedo, transparent film, an elliptical alpha falloff so no card shows a square edge). Card normals lean out
#   from the trunk so a crown shades as a volume. Materials with alpha are written alphaMode MASK (the engine's cutout).
# Small plants that already use alpha cards (fern, shrub, grass) keep their geometry; rocks, roots, logs are decimated.
import bpy, bmesh, sys, os, math, json
import numpy as np
from mathutils import Vector, Matrix

argv = sys.argv[sys.argv.index('--') + 1:]
SRC, OUT = argv[0], argv[1]
ONLY = set(argv[2:])
os.makedirs(OUT, exist_ok=True)

# id -> how to bake. kind: tree (cards), plant (keep), solid (decimate). split: one game asset per object (variant).
#   voxel: card cluster size (m) LOD0 / LOD1; wood: target triangles LOD0 / LOD1; tris: solid target LOD0 / LOD1
SPEC = {
    "pine_tree_01":        {"kind": "tree", "voxel": (0.55, 1.4), "wood": (5000, 900)},
    "fir_tree_01":         {"kind": "tree", "voxel": (0.5, 1.3), "wood": (5000, 900)},
    "pine_sapling_medium": {"kind": "tree", "voxel": (0.35, 0.9), "wood": (2500, 500)},
    "fir_sapling_medium":  {"kind": "tree", "voxel": (0.3, 0.8), "wood": (2000, 400)},
    "fir_sapling":         {"kind": "tree", "voxel": (0.16, 0.4), "wood": (800, 200)},
    "pine_sapling_small":  {"kind": "tree", "voxel": (0.12, 0.3), "wood": (500, 150)},
    "island_tree_01":      {"kind": "tree", "voxel": (0.6, 1.5), "wood": (5000, 900)},
    "fern_02":             {"kind": "plant"},
    "shrub_03":            {"kind": "plant"},
    "grass_medium_01":     {"kind": "plant"},
    "dry_branches_medium_01": {"kind": "solid", "tris": (3000, 600)},
    "dead_tree_trunk":     {"kind": "solid", "tris": (6000, 1200)},
    "dead_tree_trunk_02":  {"kind": "solid", "tris": (6000, 1200)},
    "pine_roots":          {"kind": "solid", "tris": (6000, 1200)},
    "root_cluster_01":     {"kind": "solid", "tris": (6000, 1200)},
    "root_cluster_02":     {"kind": "solid", "tris": (4000, 800)},
    "rock_moss_set_01":    {"kind": "solid", "tris": (2500, 500)},
    "rock_moss_set_02":    {"kind": "solid", "tris": (2500, 500)},
    "boulder_01":          {"kind": "solid", "tris": (5000, 1000)},
    "stone_01":            {"kind": "solid", "tris": (3000, 600)},
    "rock_09":             {"kind": "solid", "tris": (3000, 600)},
    "rock_face_01":        {"kind": "solid", "tris": (5000, 1000)},
    "coastal_cliff_02":    {"kind": "solid", "tris": (9000, 1800)},
    "coastal_cliff_04":    {"kind": "solid", "tris": (9000, 1800)},
    "coast_rocks_05":      {"kind": "solid", "tris": (8000, 1600)},
    "sand_rocks_small_01": {"kind": "solid", "tris": (8000, 1600)},
}
TWIG = ("twig", "leaves")          # material name parts that hold the foliage
ATLAS, TILES = 2048, 4             # 4 x 4 tiles of 512 px
report = {}


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def meshes():
    return [o for o in bpy.context.scene.objects if o.type == 'MESH']


def select(objs, active=None):
    bpy.ops.object.select_all(action='DESELECT')
    for o in objs: o.select_set(True)
    bpy.context.view_layer.objects.active = active or objs[0]


def tri_count(o):
    return sum(len(p.vertices) - 2 for p in o.data.polygons)


def drop_small_islands(o, min_diag):
    """LOD1 wood: remove loose parts smaller than min_diag (thin side branches: the cards hide them at that range, and
    collapse decimation cannot take a loose tube below a few dozen triangles)."""
    bm = bmesh.new(); bm.from_mesh(o.data); bm.faces.ensure_lookup_table()
    seen = set(); kill = []
    for f in bm.faces:
        if f.index in seen: continue
        isl = []; st = [f]; seen.add(f.index)
        while st:
            g = st.pop(); isl.append(g)
            for e in g.edges:
                for h in e.link_faces:
                    if h.index not in seen: seen.add(h.index); st.append(h)
        lo = Vector((1e9,) * 3); hi = -lo
        for g in isl:
            for v in g.verts: lo = Vector(map(min, lo, v.co)); hi = Vector(map(max, hi, v.co))
        if (hi - lo).length < min_diag: kill.extend(isl)
    if kill and len(kill) < len(bm.faces): bmesh.ops.delete(bm, geom=kill, context='FACES')
    bm.to_mesh(o.data); bm.free()


def decimate(o, target):
    n = tri_count(o)
    if n <= target or n == 0: return
    m = o.modifiers.new("dec", 'DECIMATE'); m.ratio = max(0.002, target / n); m.use_collapse_triangulate = True
    select([o]); bpy.ops.object.modifier_apply(modifier=m.name)


def ground_pivot(objs):
    """Centre x / y (Blender), lowest point z = 0, for the variant's objects together; transforms applied."""
    select(objs); bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    lo = Vector((1e9,) * 3); hi = -lo
    for o in objs:
        for v in o.data.vertices: lo = Vector(map(min, lo, v.co)); hi = Vector(map(max, hi, v.co))
    off = Vector(((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, lo.z))
    for o in objs: o.data.transform(Matrix.Translation(-off))
    return hi - lo


def split_by_material(o):
    """Separate o by material: returns {material name: object}."""
    select([o]); bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.separate(type='MATERIAL'); bpy.ops.object.mode_set(mode='OBJECT')
    out = {}
    for p in bpy.context.selected_objects:
        mats = [s.material for s in p.material_slots if s.material]
        used = {p.data.polygons[i].material_index for i in range(len(p.data.polygons))} if len(p.data.polygons) else set()
        name = mats[min(used)].name if used and mats else (mats[0].name if mats else p.name)
        out[name] = p
    return out


def base_image(mat):
    if not mat or not mat.use_nodes: return None
    for n in mat.node_tree.nodes:
        if n.type == 'BSDF_PRINCIPLED':
            l = n.inputs['Base Color'].links
            if l and l[0].from_node.type == 'TEX_IMAGE': return l[0].from_node.image
    for n in mat.node_tree.nodes:
        if n.type == 'TEX_IMAGE': return n.image
    return None


def apply_alpha():
    """Poly Haven ships plant / twig opacity as a separate <name>_alpha_1k.jpg (the glTF's colour map is a JPEG): merge it
    into the colour image as RGBA and make the material alpha-clipped, so the tile renders, the export (alphaMode MASK)
    and the engine's cutout all see the leaf, blade and needle shapes."""
    for m in bpy.data.materials:
        if not m.use_nodes: continue
        for n in m.node_tree.nodes:
            if n.type != 'BSDF_PRINCIPLED': continue
            l = n.inputs['Base Color'].links
            if not l or l[0].from_node.type != 'TEX_IMAGE' or not l[0].from_node.image: continue
            tn = l[0].from_node; img = tn.image
            path = bpy.path.abspath(img.filepath)
            ap = path.replace("_diff_", "_alpha_")
            if ap == path or not os.path.exists(ap): continue
            al = bpy.data.images.load(ap); al.colorspace_settings.name = 'Non-Color'
            w, h = img.size
            if tuple(al.size) != (w, h): al.scale(w, h)
            px = np.array(img.pixels[:], np.float32).reshape(-1, 4); pa = np.array(al.pixels[:], np.float32).reshape(-1, 4)
            px[:, 3] = pa[:, 0]
            rgba = bpy.data.images.new(img.name + "_rgba", w, h, alpha=True); rgba.pixels = px.ravel()
            rgba.filepath_raw = os.path.join(OUT, "_" + os.path.basename(path).replace(".jpg", "_rgba.png")); rgba.file_format = 'PNG'; rgba.save()
            tn.image = rgba
            m.node_tree.links.new(tn.outputs['Alpha'], n.inputs['Alpha'])
            m.blend_method = 'CLIP'
            try: m.surface_render_method = 'DITHERED'
            except Exception: pass
            print("BAKE alpha merged", os.path.basename(ap))


def subset(twig, faces):
    """A new object holding only these faces of twig (positions, UVs, material): the cluster on its own."""
    me = twig.data
    ls = np.zeros(len(me.polygons), np.int32); lt = np.zeros(len(me.polygons), np.int32)
    me.polygons.foreach_get("loop_start", ls); me.polygons.foreach_get("loop_total", lt)
    lv = np.zeros(len(me.loops), np.int32); me.loops.foreach_get("vertex_index", lv)
    co = np.zeros(len(me.vertices) * 3, np.float32); me.vertices.foreach_get("co", co); co = co.reshape(-1, 3)
    uv = np.zeros(len(me.loops) * 2, np.float32); me.uv_layers.active.data.foreach_get("uv", uv); uv = uv.reshape(-1, 2)
    loops = np.concatenate([np.arange(ls[f], ls[f] + lt[f]) for f in faces])
    verts = co[lv[loops]]
    polys, k = [], 0
    for f in faces:
        polys.append(tuple(range(k, k + lt[f]))); k += lt[f]
    m2 = bpy.data.meshes.new("tile"); m2.from_pydata(verts.tolist(), [], polys); m2.update()
    l2 = m2.uv_layers.new(name="UVMap"); l2.data.foreach_set("uv", uv[loops].ravel())
    for s in me.materials: m2.materials.append(s)
    o = bpy.data.objects.new("tile", m2); bpy.context.scene.collection.objects.link(o)
    return o


def render_tiles(twig, clusters, atlas_path):
    """Render each chosen cluster on its own (its faces only: the sprig's real silhouette) through an ortho camera
    into one RGBA atlas (flat albedo)."""
    sc = bpy.context.scene
    sc.render.engine = 'BLENDER_WORKBENCH'
    sh = sc.display.shading; sh.light = 'FLAT'; sh.color_type = 'TEXTURE'
    sc.render.film_transparent = True
    sc.render.resolution_x = sc.render.resolution_y = ATLAS // TILES
    sc.render.image_settings.file_format = 'PNG'; sc.render.image_settings.color_mode = 'RGBA'
    sc.view_settings.view_transform = 'Standard'
    sc.render.filter_size = 0.8
    cam_d = bpy.data.cameras.new("tile"); cam_d.type = 'ORTHO'
    cam = bpy.data.objects.new("tile", cam_d); sc.collection.objects.link(cam); sc.camera = cam
    T = ATLAS // TILES
    atlas = np.zeros((ATLAS, ATLAS, 4), np.float32)
    tmp = os.path.join(OUT, "_tile.png")
    for k, c in enumerate(clusters[:TILES * TILES]):
        ctr, ax_u, ax_v, nrm, ext = c["c"], c["u"], c["v"], c["n"], c["ext"]
        size = max(ext[0], ext[1]) * 1.15
        cam_d.ortho_scale = size
        cam.location = Vector(ctr) + Vector(nrm) * (ext[2] + 2.0)
        R = Matrix((Vector(ax_u), Vector(ax_v), Vector(nrm))).transposed()
        cam.rotation_euler = R.to_euler()
        cam_d.clip_start = 2.0; cam_d.clip_end = 2.0 + 2 * ext[2] + 0.05
        sc.render.filepath = tmp
        twig.hide_render = True
        so = subset(twig, c["faces"])
        cam_d.clip_start = 0.01; cam_d.clip_end = 2.0 + 2 * ext[2] + 50
        bpy.ops.render.render(write_still=True)
        bpy.data.objects.remove(so)
        img = bpy.data.images.load(tmp, check_existing=False)
        px = np.array(img.pixels[:], np.float32).reshape(T, T, 4)
        bpy.data.images.remove(img)
        yy, xx = np.mgrid[0:T, 0:T]; d = np.sqrt(((xx + 0.5) / T * 2 - 1) ** 2 + ((yy + 0.5) / T * 2 - 1) ** 2)
        px[..., 3] *= np.clip((1.06 - d) / 0.12, 0, 1)                      # only the corners trimmed (the sprig is its own silhouette)
        px[..., :3] = np.clip((px[..., :3] - px[..., :3].mean(-1, keepdims=True)) * 1.15 + px[..., :3].mean(-1, keepdims=True), 0, 1) * np.array([1.42, 1.38, 1.18], np.float32)   # Workbench flat albedo reads dull and cold in the game: lift, a warmer green (the banner's sunlit needles)
        px[..., 3] = np.where(px[..., 3] > 0.35, 1.0, px[..., 3] * 0.6)      # crisp needles (alpha-tested at 0.5)
        tx, ty = k % TILES, k // TILES
        atlas[ty * T:(ty + 1) * T, tx * T:(tx + 1) * T] = px
    # bleed colour into the transparent texels (no dark fringe on the mips)
    a = atlas[..., 3] > 0.01
    if a.any():
        mean = atlas[a][:, :3].mean(0); atlas[~a, 0:3] = mean
    im = bpy.data.images.new("atlas", ATLAS, ATLAS, alpha=True)
    im.pixels = atlas.ravel(); im.filepath_raw = atlas_path; im.file_format = 'PNG'; im.save()
    bpy.data.objects.remove(cam)
    return im


def clusters_of(twig, voxel, axis_c):
    """Voxel clusters of the twig faces: centre, PCA axes, extent (u, v, half thickness)."""
    me = twig.data; n = len(me.polygons)
    C = np.zeros(n * 3, np.float32); me.polygons.foreach_get("center", C); C = C.reshape(n, 3)
    A = np.zeros(n, np.float32); me.polygons.foreach_get("area", A)
    key = np.floor(C / voxel).astype(np.int64)
    k1 = (key[:, 0] * 73856093) ^ (key[:, 1] * 19349663) ^ (key[:, 2] * 83492791)
    order = np.argsort(k1); k1s = k1[order]
    cuts = np.flatnonzero(np.diff(k1s)) + 1
    out = []
    for g in np.split(order, cuts):
        if len(g) < 6: continue
        P = C[g]; w = A[g] + 1e-9; m = (P * w[:, None]).sum(0) / w.sum()
        Q = P - m; cov = (Q * w[:, None]).T @ Q / w.sum()
        ev, evec = np.linalg.eigh(cov)
        nrm = evec[:, 0]; u = evec[:, 2]; v = np.cross(nrm, u)
        pu, pv, pn = Q @ u, Q @ v, Q @ nrm
        (u0, u1), (v0, v1) = np.percentile(pu, (5, 95)), np.percentile(pv, (5, 95))   # a few stray faces must not shrink the sprig in its tile
        ext = (float(u1 - u0) + voxel * 0.3, float(v1 - v0) + voxel * 0.3, float(np.percentile(np.abs(pn), 95)) + 0.02)
        ctr = m + u * (u0 + u1) / 2 + v * (v0 + v1) / 2
        out.append({"c": ctr.tolist(), "u": u.tolist(), "v": v.tolist(), "n": nrm.tolist(), "ext": ext, "area": float(w.sum()), "faces": g})
    out.sort(key=lambda c: -c["area"])
    return out


def card_mesh(name, clusters, mat, axis_xy, crossed):
    """One quad (two crossed quads when crossed) per cluster, UVs into an atlas tile, normals leaning out from the trunk."""
    verts, faces, uvs, nrms = [], [], [], []
    for k, c in enumerate(clusters):
        t = k % (TILES * TILES); tx, ty = t % TILES, t // TILES
        u0, v0 = tx / TILES, 1 - (ty + 1) / TILES
        ctr = Vector(c["c"]); u = Vector(c["u"]); v = Vector(c["v"]); nrm = Vector(c["n"])
        hu, hv = c["ext"][0] * 0.5 * 1.15, c["ext"][1] * 0.5 * 1.15
        hs = max(hu, hv)   # the tile was rendered square (size = max extent)
        out = Vector((ctr.x - axis_xy[0], ctr.y - axis_xy[1], 0.35)); out = out.normalized() if out.length > 1e-6 else Vector((0, 0, 1))
        w2 = (u * 0.5 + nrm * 0.866).normalized()
        planes = [(u, v), (u, nrm), (w2, v)] if crossed is True else [(u, v), (u, nrm)] if crossed == 2 else [(u, v)]   # 3 planes near, 2 far: a full crown
        for pu, pv in planes:
            b = len(verts)
            for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                verts.append(ctr + pu * (su * hs) + pv * (sv * hs))
                uvs.append((u0 + (su + 1) / 2 / TILES, v0 + (sv + 1) / 2 / TILES))
                pn = pu.cross(pv).normalized(); pn = pn if pn.dot(out) >= 0 else -pn
                nrms.append((pn * 0.35 + out * 0.65).normalized())
            faces.append((b, b + 1, b + 2, b + 3))
    me = bpy.data.meshes.new(name); me.from_pydata([tuple(p) for p in verts], [], faces); me.update()
    uvl = me.uv_layers.new(name="UVMap")
    for poly in me.polygons:
        for li in poly.loop_indices: uvl.data[li].uv = uvs[me.loops[li].vertex_index]
    me.normals_split_custom_set_from_vertices([tuple(n) for n in nrms])
    me.materials.append(mat)
    o = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(o)
    return o


def card_material(name, atlas):
    m = bpy.data.materials.new(name); m.use_nodes = True
    nt = m.node_tree; b = nt.nodes['Principled BSDF']
    t = nt.nodes.new('ShaderNodeTexImage'); t.image = atlas
    nt.links.new(t.outputs['Color'], b.inputs['Base Color']); nt.links.new(t.outputs['Alpha'], b.inputs['Alpha'])
    b.inputs['Roughness'].default_value = 1.0; b.inputs['Metallic'].default_value = 0.0
    m.blend_method = 'CLIP' if hasattr(m, 'blend_method') else None
    try: m.surface_render_method = 'DITHERED'
    except Exception: pass
    m.use_backface_culling = False
    return m


def export(objs, path):
    select(objs)
    bpy.ops.export_scene.gltf(filepath=path, export_format='GLB', use_selection=True, export_apply=True,
                              export_image_format='AUTO', export_yup=True, export_normals=True, export_tangents=True)


def mark_mask(objs):
    """Materials with an alpha input linked (fern / grass / shrub blades) export as MASK, not BLEND."""
    for o in objs:
        for s in o.material_slots:
            m = s.material
            if not m: continue
            if m.use_nodes and any(n.type == 'BSDF_PRINCIPLED' and n.inputs['Alpha'].links for n in m.node_tree.nodes):
                m.blend_method = 'CLIP'
                try: m.surface_render_method = 'DITHERED'
                except Exception: pass


def bake(pid, spec):
    reset()
    path = os.path.join(SRC, pid, pid + "_1k.gltf")
    bpy.ops.import_scene.gltf(filepath=path)
    apply_alpha()
    variants = [o for o in meshes()]
    # one game asset per variant object (a, b, c ...); a single-object set keeps its id
    names = {}
    for o in variants:
        tag = o.name.replace(pid, "").replace("_LOD0", "").strip("_") or ""
        names[o] = "ph_" + pid + ("_" + tag if tag and len(variants) > 1 else "")
    keep = list(variants)
    for o in keep:
        aid = names[o]
        # isolate this variant: hide the others from the export and the tile renders
        for q in keep: q.hide_render = q is not o
        dims = ground_pivot([o])
        if spec["kind"] == "solid":
            for lod, tgt in enumerate(spec["tris"]):
                c = o.copy(); c.data = o.data.copy(); bpy.context.scene.collection.objects.link(c)
                decimate(c, tgt)
                export([c], os.path.join(OUT, aid + ("_lod1" if lod else "") + ".glb"))
                report.setdefault(aid, {})["lod%d" % lod] = tri_count(c)
                bpy.data.objects.remove(c)
            report[aid]["size"] = [round(dims.x, 2), round(dims.z, 2), round(dims.y, 2)]
            continue
        if spec["kind"] == "plant":
            mark_mask([o])
            export([o], os.path.join(OUT, aid + ".glb"))
            c = o.copy(); c.data = o.data.copy(); bpy.context.scene.collection.objects.link(c)
            decimate(c, max(200, tri_count(o) // 3)); export([c], os.path.join(OUT, aid + "_lod1.glb")); bpy.data.objects.remove(c)
            report[aid] = {"lod0": tri_count(o), "size": [round(dims.x, 2), round(dims.z, 2), round(dims.y, 2)]}
            continue
        # tree: wood + cards
        parts = split_by_material(o)
        twig = [p for n, p in parts.items() if any(t in n.lower() for t in TWIG)]
        wood = [p for n, p in parts.items() if p not in twig]
        if not twig:
            print("BAKE", aid, "no twig material in", list(parts)); continue
        if len(twig) > 1:
            select(twig); bpy.ops.object.join(); twig = [bpy.context.view_layer.objects.active]
        twig = twig[0]
        for q in meshes(): q.hide_render = q is not twig
        axis = (0.0, 0.0)
        lod0 = clusters_of(twig, spec["voxel"][0], axis)
        lod1 = clusters_of(twig, spec["voxel"][1], axis)
        atlas_path = os.path.join(OUT, aid + "_atlas.png")
        atlas = render_tiles(twig, lod0, atlas_path)
        mat = card_material(aid + "_needles", atlas)
        cards0 = card_mesh(aid + "_cards", lod0, mat, axis, crossed=True)
        cards1 = card_mesh(aid + "_cards1", lod1, mat, axis, crossed=2)
        bpy.data.objects.remove(twig)
        w0 = []
        w1 = []
        total = sum(tri_count(p) for p in wood) or 1
        for p in wood:
            c = p.copy(); c.data = p.data.copy(); bpy.context.scene.collection.objects.link(c)
            decimate(p, max(60, spec["wood"][0] * tri_count(p) // total)); w0.append(p)
            drop_small_islands(c, 1.5 if dims.z > 6 else 0.4); decimate(c, max(24, spec["wood"][1] * tri_count(c) // total)); w1.append(c)
        export(w0 + [cards0], os.path.join(OUT, aid + ".glb"))
        export(w1 + [cards1], os.path.join(OUT, aid + "_lod1.glb"))
        report[aid] = {"lod0": sum(map(tri_count, w0)) + tri_count(cards0), "cards0": len(lod0), "lod1": sum(map(tri_count, w1)) + tri_count(cards1),
                       "cards1": len(lod1), "size": [round(dims.x, 2), round(dims.z, 2), round(dims.y, 2)]}
        for q in w0 + w1 + [cards0, cards1]: bpy.data.objects.remove(q)
        # the other variants were split / removed only for this one: reload for the next
        if o is not keep[-1]:
            return bake_rest(pid, spec, [names[q] for q in keep[keep.index(o) + 1:]])
    return None


def bake_rest(pid, spec, todo):
    """Trees: each variant from a fresh import (the split / join above consumes the scene)."""
    for aid in todo:
        reset()
        bpy.ops.import_scene.gltf(filepath=os.path.join(SRC, pid, pid + "_1k.gltf"))
        apply_alpha()
        objs = meshes()
        for o in objs:
            tag = o.name.replace(pid, "").replace("_LOD0", "").strip("_")
            if "ph_" + pid + "_" + tag != aid: bpy.data.objects.remove(o)
        o = meshes()[0]
        dims = ground_pivot([o])
        parts = split_by_material(o)
        twig = [p for n, p in parts.items() if any(t in n.lower() for t in TWIG)]
        wood = [p for n, p in parts.items() if p not in twig]
        if len(twig) > 1:
            select(twig); bpy.ops.object.join(); twig = [bpy.context.view_layer.objects.active]
        twig = twig[0]
        for q in meshes(): q.hide_render = q is not twig
        lod0 = clusters_of(twig, spec["voxel"][0], (0, 0)); lod1 = clusters_of(twig, spec["voxel"][1], (0, 0))
        atlas = render_tiles(twig, lod0, os.path.join(OUT, aid + "_atlas.png"))
        mat = card_material(aid + "_needles", atlas)
        cards0 = card_mesh(aid + "_cards", lod0, mat, (0, 0), True); cards1 = card_mesh(aid + "_cards1", lod1, mat, (0, 0), 2)
        bpy.data.objects.remove(twig)
        total = sum(tri_count(p) for p in wood) or 1; w0, w1 = [], []
        for p in wood:
            c = p.copy(); c.data = p.data.copy(); bpy.context.scene.collection.objects.link(c)
            decimate(p, max(60, spec["wood"][0] * tri_count(p) // total)); w0.append(p)
            drop_small_islands(c, 1.5 if dims.z > 6 else 0.4); decimate(c, max(24, spec["wood"][1] * tri_count(c) // total)); w1.append(c)
        export(w0 + [cards0], os.path.join(OUT, aid + ".glb")); export(w1 + [cards1], os.path.join(OUT, aid + "_lod1.glb"))
        report[aid] = {"lod0": sum(map(tri_count, w0)) + tri_count(cards0), "cards0": len(lod0), "lod1": sum(map(tri_count, w1)) + tri_count(cards1),
                       "cards1": len(lod1), "size": [round(dims.x, 2), round(dims.z, 2), round(dims.y, 2)]}


for pid, spec in SPEC.items():
    if ONLY and pid not in ONLY: continue
    if not os.path.exists(os.path.join(SRC, pid, pid + "_1k.gltf")): print("BAKE", pid, "missing"); continue
    try:
        bake(pid, spec)
    except Exception as e:
        import traceback; traceback.print_exc(); print("BAKE", pid, "FAILED", e)
rp = os.path.join(OUT, "bake_report.json")
old = json.load(open(rp)) if os.path.exists(rp) else {}
old.update(report); json.dump(old, open(rp, "w"), indent=1)
print("BAKE REPORT", json.dumps(report))
