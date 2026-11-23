"""VH1d: the House Village houses restyled after the user's reference (medieval_house.glb, "Medieval house" by Vanuartw,
CC-BY-4.0, looked at only): pale plaster walls framed in oak, thick dark shingle roofs with wood rims and swept eaves.
Run inside pr6_split.py (Blender), on one house's joined shell in source units, z up, transforms applied.

  plaster(ob, P, out): the walls' stone in their baked atlases repainted as plaster, texel by texel, where the atlas shows
      plain wall; painted windows, doors, shutters and timber (features: very dark, very bright, blue or red paint, as
      blobs, not the stones' mortar lines) are kept. Returns the feature masks for frame().
  frame(ob, F, fe): oak members on the outer walls, found by casting rays at each wall of the outline: corner posts,
      studs, posts either side of every opening, a sole rail, the storey rail and the eave plate, lintels and sills,
      and braces in the end panels. Members stop where the wall does (gables, openings, painted windows).
  sweep(bm, S): a roof's eaves turned up (the band above each roof island's lowest edge flattened and pushed out).
"""
import bpy, bmesh, os, math
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree
from mathutils.geometry import barycentric_transform

LUM = np.array([0.2126, 0.7152, 0.0722], np.float32)
Z = Vector((0, 0, 1))


def material(name, tex, rough=0.92):
    m = bpy.data.materials.get(name)
    if m: return m
    m = bpy.data.materials.new(name); m.use_nodes = True; N, L = m.node_tree.nodes, m.node_tree.links
    bs = next(n for n in N if n.type == "BSDF_PRINCIPLED"); bs.inputs["Roughness"].default_value = rough; bs.inputs["Metallic"].default_value = 0.0
    ti = N.new("ShaderNodeTexImage"); ti.image = bpy.data.images.load(os.path.join(tex, "color.jpg"), check_existing=True); L.new(ti.outputs[0], bs.inputs["Base Color"])
    if os.path.exists(os.path.join(tex, "normal.jpg")):
        nt = N.new("ShaderNodeTexImage"); nt.image = bpy.data.images.load(os.path.join(tex, "normal.jpg"), check_existing=True); nt.image.colorspace_settings.name = "Non-Color"
        nm = N.new("ShaderNodeNormalMap"); L.new(nt.outputs[0], nm.inputs["Color"]); L.new(nm.outputs[0], bs.inputs["Normal"])
    return m


def _base(m):
    if not m or not m.use_nodes: return None
    bs = next((n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None)
    if not bs or not bs.inputs["Base Color"].links: return None
    n = bs.inputs["Base Color"].links[0].from_node
    return n if n.type == "TEX_IMAGE" and n.image and n.image.size[0] else None


def _grow(a, r, op):   # square dilation (op = or) / erosion (op = and) of a bool mask, radius r texels
    for ax in (0, 1):
        b = a.copy()
        for d in range(1, r + 1): b = op(b, op(np.roll(a, d, ax), np.roll(a, -d, ax)))
        a = b
    return a


def _raster(mask, uvs, w, h):   # fill one UV triangle into a (h, w) mask (row 0 = v 0, as image.pixels)
    p = np.array([(u * w, v * h) for u, v in uvs], np.float64)
    if (p[:, 0].max() - p[:, 0].min()) > w or (p[:, 1].max() - p[:, 1].min()) > h: mask[:] = True; return   # tiles: the whole image
    off = np.floor(p.min(0) / (w, h)) * (w, h); p -= off
    x0, y0 = np.floor(p.min(0)).astype(int); x1, y1 = np.ceil(p.max(0)).astype(int)
    xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
    (ax, ay), (bx, by), (cx, cy) = p
    den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
    if abs(den) < 1e-9: return
    l1 = ((by - cy) * (xs - cx) + (cx - bx) * (ys - cy)) / den; l2 = ((cy - ay) * (xs - cx) + (ax - cx) * (ys - cy)) / den
    e = 1.0 / max(1.0, abs(den)) ** 0.5   # a texel's slack, so seams fill
    ins = (l1 >= -e) & (l2 >= -e) & (1 - l1 - l2 >= -e)
    mask[(ys[ins].astype(int)) % h, (xs[ins].astype(int)) % w] = True


def plaster(ob, P, out):
    me = ob.data; uvl = me.uv_layers.active
    pim = bpy.data.images.load(os.path.join(P["tex"], "color.jpg"), check_existing=True)
    pw, ph = pim.size; pl = np.empty(pw * ph * 4, np.float32); pim.pixels.foreach_get(pl); pl = pl.reshape(ph, pw, 4)
    nodes = {}   # image name -> the texture nodes that show it
    wall, roof = {}, {}
    oak = None
    if P.get("timber"):   # the shell's own posts and beams (thin strips of wall) in oak
        me.materials.append(material("vh1_oak", P["timber"])); oak = len(me.materials) - 1
    for p in me.polygons:
        if oak is not None and abs(p.normal.z) < 0.3 and p.area > 1e-4:
            ax_ = 0 if abs(p.normal.x) >= abs(p.normal.y) else 1; vs_ = [me.vertices[v].co for v in p.vertices]
            if min(max(v[1 - ax_] for v in vs_) - min(v[1 - ax_] for v in vs_), max(v.z for v in vs_) - min(v.z for v in vs_)) < P.get("thin", 0.75):
                p.material_index = oak; t_ = P["timber_tile"]
                for li in p.loop_indices:
                    co = me.vertices[me.loops[li].vertex_index].co; uvl.data[li].uv = (co.z / t_, co[1 - ax_] / t_)
                continue
        n = _base(ob.material_slots[p.material_index].material) if p.material_index < len(ob.material_slots) else None
        if n is None: continue
        nodes.setdefault(n.image.name, set()).add(n)
        tgt = wall if abs(p.normal.z) < 0.3 else roof
        tgt.setdefault(n.image.name, []).append([tuple(uvl.data[li].uv) for li in p.loop_indices])
    fe = {}
    for name, polys in wall.items():
        im = bpy.data.images[name]; w, h = im.size
        a = np.empty(w * h * 4, np.float32); im.pixels.foreach_get(a); a = a.reshape(h, w, 4)
        wm = np.zeros((h, w), bool); rm = np.zeros((h, w), bool)
        for uv in polys:
            for k in range(1, len(uv) - 1): _raster(wm, (uv[0], uv[k], uv[k + 1]), w, h)
        for uv in roof.get(name, []):
            for k in range(1, len(uv) - 1): _raster(rm, (uv[0], uv[k], uv[k + 1]), w, h)
        c = a[:, :, :3]; l = c @ LUM
        f = (l < P.get("dark", 0.05)) | (l > P.get("bright", 0.7)) | ((c[:, :, 2] > c[:, :, 0] + 0.06) & (c[:, :, 2] > 0.22)) | ((c[:, :, 0] > c[:, :, 1] + 0.12) & (c[:, :, 0] > c[:, :, 2] + 0.14))
        r = max(1, int(round(P.get("blob", 3) * w / 2048)))
        f = _grow(_grow(f, r, np.logical_and), r, np.logical_or)   # opened: mortar lines and specks go, painted panes and frames stay
        f = _grow(f, max(1, r // 2 + 1), np.logical_or)
        rep = P.get("rep", 3) * w / 2048.0   # plaster tiles across the atlas
        yi = (np.arange(h) * pw * rep / w).astype(int) % ph; xi = (np.arange(w) * pw * rep / w).astype(int) % pw
        pt = pl[yi][:, xi, :3] * np.array(P.get("tint", (1.0, 1.0, 1.0)), np.float32)
        put = wm & ~f & ~rm
        a[:, :, :3] = np.where(put[:, :, None], pt, c)
        im2 = bpy.data.images.new(name[:50] + "_pl", w, h); im2.pixels.foreach_set(a.ravel())
        im2.filepath_raw = os.path.join(out, "_vh1_" + bpy.path.clean_name(name) + "_pl.png"); im2.file_format = "PNG"; im2.save()
        for n in nodes[name]: n.image = im2
        fe[name] = fe[im2.name] = (f & wm, w, h)
        print("PR6 plaster %s: %d%% of its wall texels plaster" % (name, 100 * put.sum() // max(1, wm.sum())))
    for sl in ob.material_slots:   # the stone's bump goes with it
        m = sl.material
        bs = next((n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None) if m and m.use_nodes else None
        if bs:
            for l in list(bs.inputs["Normal"].links): m.node_tree.links.remove(l)
    return fe


def frame(ob, F, fe):
    me = ob.data; me.calc_loop_triangles(); uvl = me.uv_layers.active
    V = [v.co.copy() for v in me.vertices]; LT = list(me.loop_triangles)
    tree = BVHTree.FromPolygons(V, [tuple(t.vertices) for t in LT])
    imgs = [(_base(sl.material).image.name if _base(sl.material) else None) for sl in ob.material_slots]
    st, w, pr, sp = F["step"], F["w"], F["proud"], F["spacing"]; zmax = max(v.z for v in V); S = F["S"]

    def feature(ti, loc):
        t = LT[ti]; nm = imgs[me.polygons[t.polygon_index].material_index] if me.polygons[t.polygon_index].material_index < len(imgs) else None
        if nm not in fe: return False
        m, mw, mh = fe[nm]
        uv = [Vector((*uvl.data[li].uv, 0)) for li in t.loops]
        u = barycentric_transform(loc, V[t.vertices[0]], V[t.vertices[1]], V[t.vertices[2]], *uv)
        return bool(m[int((u.y % 1.0) * mh) % mh, int((u.x % 1.0) * mw) % mw])

    boxes = []   # (centre-line p0, p1 in 3D on the face, the wall's outward normal)
    Pl = F["outline"]
    for e in range(len(Pl)):
        A = Vector((*Pl[e], 0)); B = Vector((*Pl[(e + 1) % len(Pl)], 0)); Ln = (B - A).length
        if Ln < 4 * w: continue
        t = (B - A) / Ln; n = Vector((t.y, -t.x, 0))
        ni, nj = int(Ln / st), int(zmax / st) + 1
        ok = np.zeros((ni, nj), bool); D = np.zeros((ni, nj))
        for i in range(ni):
            for j in range(nj):
                o = A + t * ((i + 0.5) * st) + Z * ((j + 0.5) * st) + n * 1.5
                loc, nor, ti, dist = tree.ray_cast(o, -n, 4.0)
                if loc is None or abs(nor.z) > 0.3 or abs(dist - 1.5) > F.get("band", 1.2): continue
                if feature(ti, loc): continue
                ok[i, j] = True; D[i, j] = dist - 1.5
        if ok.sum() < 20: continue

        def at(s, z): return A + t * s + Z * z
        def depth(cells):
            d = [D[i, j] for i, j in cells if 0 <= i < ni and 0 <= j < nj and ok[i, j]]
            return float(np.median(d)) if d else 0.0
        def runs(line, mn):   # contiguous True runs (one-cell gaps bridged), at least mn cells
            out, k = [], 0
            while k < len(line):
                if not line[k]: k += 1; continue
                a_ = k
                while k < len(line) and (line[k] or (k + 1 < len(line) and line[k + 1])): k += 1
                if k - a_ >= mn: out.append((a_, k))
            return out
        def add(s0, z0, s1, z1, cells):
            d = depth(cells); boxes.append((at(s0, z0) - n * d, at(s1, z1) - n * d, n))
        # posts: corners, studs, either side of each opening (in the window band)
        posts = [w / 2, Ln - w / 2]
        jb = [j for j in range(nj) if F["win"][0] <= (j + 0.5) * st <= F["win"][1]]
        for j in jb:
            for i in range(ni - 1):
                if ok[i, j] and not ok[i + 1, j]: posts.append((i + 1) * st - w / 2)
                if not ok[i, j] and ok[i + 1, j]: posts.append((i + 1) * st + w / 2)
        posts.sort(); keep = []
        for s in posts:
            if all(abs(s - q) > w * 1.5 for q in keep): keep.append(s)
        studs = []
        for a_, b_ in zip(keep, keep[1:]):   # studs between, every `spacing`
            k = int((b_ - a_) / sp)
            studs += [a_ + (b_ - a_) * m / (k + 1) for m in range(1, k + 1)] if k else []
        verts_s = sorted(keep + studs)
        for s in verts_s:
            i = min(ni - 1, max(0, int(s / st)))
            for j0, j1 in runs(ok[i], int(0.5 / S / st)):
                add(s, j0 * st, s, j1 * st, [(i, j) for j in range(j0, j1)])
        # rails: sole, storey, eave plate (the eave: the low tops along the wall, where it rises well above the storey)
        tops = [max(j for j in range(nj) if ok[i, j]) for i in range(ni) if ok[i].any()]
        rails = [F["sole"], F["Hc"]]
        ze = float(np.percentile(tops, 20) + 0.5) * st if tops else 0
        if ze > F["Hc"] + 1.0 / S: rails.append(ze - w / 2)
        for z in rails:
            j = min(nj - 1, int(z / st))
            for i0, i1 in runs(ok[:, j], int(0.4 / S / st)):
                add(i0 * st, z, i1 * st, z, [(i, j) for i in range(i0, i1)])
        # lintels and sills over / under each opening in the window band
        if jb:
            jm = jb[len(jb) // 2]; i = 0
            while i < ni:
                if ok[i, jm]: i += 1; continue
                i0 = i
                while i < ni and not ok[i, jm]: i += 1
                if i0 == 0 or i == ni: continue
                ic = (i0 + i) // 2; col = ok[ic]
                jt = next((j for j in range(jm, nj) if col[j]), None); jl = next((j for j in range(jm, -1, -1) if col[j]), None)
                s0, s1 = i0 * st - w, i * st + w
                if jt is not None: add(s0, jt * st + w / 2, s1, jt * st + w / 2, [(ic, jt)])
                if jl is not None and jl * st > F["sole"] + w: add(s0, (jl + 1) * st - w / 2, s1, (jl + 1) * st - w / 2, [(ic, jl)])
        # braces: the panels at either end of the wall, between the sole and the storey rail, leaning in toward the middle
        for (s0, s1), lean in (((verts_s[0], verts_s[1]), 1), ((verts_s[-2], verts_s[-1]), -1)) if len(verts_s) > 2 else ():
            za, zb = F["sole"] + w / 2, F["Hc"] - w / 2
            if not (0.6 / S < s1 - s0 < 3.0 / S) or zb - za < 0.8 / S: continue
            pa, pb = (s0 + w / 2, za), (s1 - w / 2, zb)
            if lean < 0: pa, pb = (s1 - w / 2, za), (s0 + w / 2, zb)
            cells = [(int((pa[0] + (pb[0] - pa[0]) * q / 12) / st), int((pa[1] + (pb[1] - pa[1]) * q / 12) / st)) for q in range(13)]
            if all(0 <= i < ni and 0 <= j < nj and ok[i, j] for i, j in cells): add(pa[0], pa[1], pb[0], pb[1], cells)
    if not boxes: return ob, 0
    mat = material("vh1_oak", F["tex"]); bm = bmesh.new(); uv = bm.loops.layers.uv.new(uvl.name)
    for p0, p1, n in boxes:
        ax = p1 - p0; L_ = ax.length
        if L_ < 1e-4: continue
        ax /= L_; b = n.cross(ax).normalized()
        q0, q1 = p0 - ax * (w / 2 if abs(ax.z) < 0.9 else 0), p1 + ax * (w / 2 if abs(ax.z) < 0.9 else 0)
        cs = [q + b * (sb * w / 2) + n * (pr if sn > 0 else -0.03 / S) for q in (q0, q1) for sb in (-1, 1) for sn in (-1, 1)]
        vv = [bm.verts.new(c) for c in cs]   # index = 4 end + 2 side + face
        for f in ((0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)):
            try: fc = bm.faces.new([vv[k] for k in f])
            except ValueError: continue
            for l in fc.loops:
                co = l.vert.co; l[uv].uv = (co.dot(ax) / F["tile"], (co.dot(b) + co.dot(n)) / F["tile"])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    fm = bpy.data.meshes.new("vh1_frame"); bm.to_mesh(fm); bm.free(); fm.materials.append(mat)
    fo = bpy.data.objects.new("vh1_frame", fm); bpy.context.scene.collection.objects.link(fo)
    for o_ in bpy.context.selected_objects: o_.select_set(False)
    ob.select_set(True); fo.select_set(True); bpy.context.view_layer.objects.active = ob; bpy.ops.object.join()
    return bpy.context.view_layer.objects.active, len(boxes)


def sweep(bm, band, lift, out):
    """Each roof island's eave band flattened: cut at band above its lowest edge, that edge raised and pushed out downhill."""
    bm.faces.ensure_lookup_table(); seen, isl = set(), []
    for f in bm.faces:
        if f in seen: continue
        stack, grp = [f], []
        seen.add(f)
        while stack:
            g = stack.pop(); grp.append(g)
            for e in g.edges:
                for h in e.link_faces:
                    if h not in seen: seen.add(h); stack.append(h)
        isl.append(grp)
    moved = 0
    for grp in isl:
        z0 = min(v.co.z for f in grp for v in f.verts)
        if max(v.co.z for f in grp for v in f.verts) - z0 < band * 2: continue
        low = {v for f in grp for v in f.verts if v.co.z < z0 + 1e-3}   # verts survive the cut
        geom = list({x for f in grp for x in list(f.verts) + list(f.edges)}) + grp
        bmesh.ops.bisect_plane(bm, geom=geom, plane_co=(0, 0, z0 + band), plane_no=(0, 0, 1))
        for v in low:
            hd = Vector((0, 0, 0))
            for f in v.link_faces: hd += Vector((f.normal.x, f.normal.y, 0))
            if hd.length < 1e-6: continue
            v.co += hd.normalized() * out + Z * lift; moved += 1
    return moved
