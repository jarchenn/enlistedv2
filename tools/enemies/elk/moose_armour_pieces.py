# ---------------------------------------------------------------- materials
def img(path, colour):
    i = bpy.data.images.load(path, check_existing=True); i.reload(); i.colorspace_settings.name = 'sRGB' if colour else 'Non-Color'; return i


def pbr(name, base=None, mr=None, nrm=None, col=(0.5, 0.5, 0.5), rough=0.5, metal=0.0, nstr=1.0):
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name); m.use_nodes = True; nt = m.node_tree; nt.nodes.clear()
    out = nt.nodes.new('ShaderNodeOutputMaterial'); bs = nt.nodes.new('ShaderNodeBsdfPrincipled'); nt.links.new(bs.outputs[0], out.inputs[0])
    bs.inputs['Base Color'].default_value = (*col, 1); bs.inputs['Roughness'].default_value = rough; bs.inputs['Metallic'].default_value = metal
    if base:
        t = nt.nodes.new('ShaderNodeTexImage'); t.image = img(base, True); nt.links.new(t.outputs[0], bs.inputs['Base Color'])
    if mr:
        t = nt.nodes.new('ShaderNodeTexImage'); t.image = img(mr, False); sp = nt.nodes.new('ShaderNodeSeparateColor')
        nt.links.new(t.outputs[0], sp.inputs[0]); nt.links.new(sp.outputs[1], bs.inputs['Roughness']); nt.links.new(sp.outputs[2], bs.inputs['Metallic'])
    if nrm:
        t = nt.nodes.new('ShaderNodeTexImage'); t.image = img(nrm, False); nm = nt.nodes.new('ShaderNodeNormalMap'); nm.inputs[0].default_value = nstr
        nt.links.new(t.outputs[0], nm.inputs[1]); nt.links.new(nm.outputs[0], bs.inputs['Normal'])
    return m


T = lambda n: os.path.join(TEX, n + '.png')
M = {
    'steel':   pbr('ha_steel', T('k8steel_base'), T('k8steel_mr'), T('k8steel_n'), nstr=0.6),   # Knight 8's plate tone, a little less lustrous (user)
    'mail':    pbr('ha_mail', T('mail_base'), T('mail_mr'), T('mail_n')),
    'brass':   pbr('ha_brass', col=(0.86, 0.6, 0.2), rough=0.36, metal=1.0),   # Knight 8's gold
    'red':     pbr('ha_red', T('quilt_red_base'), T('quilt_red_mr'), T('quilt_red_n')),
    'blanket': pbr('ha_blanket', T('quilt_dark_base'), T('quilt_dark_mr'), T('quilt_dark_n')),
    'leather': pbr('ha_leather', T('leather_base'), T('leather_mr'), T('leather_n'), nstr=0.5),
    'iron':    pbr('ha_iron', col=(0.12, 0.12, 0.13), rough=0.45, metal=1.0),
}
M['blue'] = pbr('ka_blue', None, T('quilt_dark_mr'), T('quilt_dark_n'), col=(0.05, 0.12, 0.38), rough=0.85, nstr=0.5)   # the reference's blue cloth
M['bluedk'] = pbr('ka_bluedk', col=(0.03, 0.07, 0.22), rough=0.9)
UVS = {'blue': 2.0, 'bluedk': 4.0, 'steel': 2.0, 'mail': 6.0, 'red': 2.5, 'blanket': 2.0, 'leather': 4.0, 'brass': 4.0, 'iron': 4.0}   # repeats per metre

parts = []   # (object, joint)


def finish(bm, bone, mat, uv=None):
    me = bpy.data.meshes.new('ha_' + mat); bm.to_mesh(me); bm.free()
    o = bpy.data.objects.new('ha_' + mat, me); sc.collection.objects.link(o); me.materials.append(M[mat])
    for p in me.polygons: p.use_smooth = True
    if uv is None:   # box-projected UVs (metres x repeats)
        me.uv_layers.new(name='UVMap'); d = me.uv_layers[0].data; s = UVS[mat]
        for p in me.polygons:
            ax = max(range(3), key=lambda i: abs(p.normal[i])); a, b = [(1, 2), (0, 2), (0, 1)][ax]
            for li in p.loop_indices:
                v = me.vertices[me.loops[li].vertex_index].co; d[li].uv = (v[a] * s, v[b] * s)
    o['src_bone'] = bone; o['sec'] = globals().get('SECTION', '?'); parts.append((o, B[bone] if bone in B else bone))
    return o


# ---------------------------------------------------------------- the fitting helper
def resample(pts, n, t0=0.0, t1=1.0):   # n points evenly by arc length between t0 and t1 of a polyline, with tangents
    pts = [Vector(p) for p in pts]; L = [0.0]
    for a, b in zip(pts, pts[1:]): L.append(L[-1] + (b - a).length)
    out = []
    for i in range(n):
        s = (t0 + (t1 - t0) * i / (n - 1)) * L[-1]
        k = max(j for j in range(len(L) - 1) if L[j] <= s + 1e-9) if s < L[-1] else len(L) - 2
        u = (s - L[k]) / max(L[k + 1] - L[k], 1e-9); out.append((pts[k].lerp(pts[k + 1], u), (pts[k + 1] - pts[k]).normalized(), s))
    return out


def surface(axis, n, t0, t1, a0, a1, m, up, off, maxr=1.5, grow=1, outer=False):
    """Rows along the centre line, columns round it (angle 0 = `up`, +90 toward +X); each point where a ray from the centre line
    meets the skin, pushed outward by `off`. Radii are max-filtered then smoothed so the shell bridges hollows and never cuts in."""
    rows = resample(axis, n, t0, t1); R, D, P = [], [], []
    for p, t, s in rows:
        u = Vector(up(p) if callable(up) else up); u = (u - t * u.dot(t)).normalized(); side = t.cross(u)
        if side.x < 0: side = -side
        rr, dd = [], []
        for j in range(m):
            a = math.radians(a0 + (a1 - a0) * j / (m - 1)); d = (u * math.cos(a) + side * math.sin(a)).normalized()
            if outer:   # EK3: from outside in, so the skin is found and not a cavity inside it (the mouth)
                hit = BVH.ray_cast(p + d * maxr, -d, maxr); rr.append(maxr - (hit[0] - (p + d * maxr)).length if hit[0] else None); dd.append(d); continue
            hit = BVH.ray_cast(p, d, maxr); rr.append((hit[0] - p).length if hit[0] else None); dd.append(d)
        R.append(rr); D.append(dd); P.append((p, s))
    MISS = [[x is None for x in rr] for rr in R]
    for i in range(n):   # misses take the nearest hit in the row
        for j in range(m):
            if R[i][j] is None:
                k = min((abs(k - j), k) for k in range(m) if R[i][k] is not None)[1] if any(x is not None for x in R[i]) else None
                R[i][j] = R[i][k] if k is not None else 0.2
    R0 = [rr[:] for rr in R]   # EK4: the skin itself; nothing below may end up inside it
    for i in range(n):   # a ray that did not leave the body within maxr: its real exit is the floor
        for j in range(m):
            if MISS[i][j]:
                p_, d_ = P[i][0], D[i][j]; h = BVH.ray_cast(p_, d_, 3.0); R0[i][j] = max(R0[i][j], (h[0] - p_).length if h[0] else R0[i][j])
    flat = sorted(x for rr in R for x in rr); med = flat[len(flat) // 2]
    R = [[min(x, med * 1.6) for x in rr] for rr in R]   # a ray that ran on into another part of the body is clipped
    for _ in range(grow):   # bridge hollows: each radius at least its neighbours'
        R = [[max(R[ii][jj] for ii in range(max(0, i - 1), min(n, i + 2)) for jj in range(max(0, j - 1), min(m, j + 2))) for j in range(m)] for i in range(n)]
    for _ in range(2):
        R = [[(R[i][j] * 2 + sum(R[ii][jj] for ii, jj in ((i - 1, j), (i + 1, j), (i, j - 1), (i, j + 1)) if 0 <= ii < n and 0 <= jj < m)) /
              (2 + sum(1 for ii, jj in ((i - 1, j), (i + 1, j), (i, j - 1), (i, j + 1)) if 0 <= ii < n and 0 <= jj < m)) for j in range(m)] for i in range(n)]
    R = [[max(R[i][j], R0[i][j]) for j in range(m)] for i in range(n)]
    return [[P[i][0] + D[i][j] * (R[i][j] + off) for j in range(m)] for i in range(n)], [[D[i][j] for j in range(m)] for i in range(n)]


def shell(grid, dirs, thick, bone, mat, uvscale=None, closed=False):   # a thick sheet over a grid of points (rows x cols), UVs by arc length
    n, m = len(grid), len(grid[0]); bm = bmesh.new(); s = UVS[mat] if uvscale is None else uvscale
    O = [[bm.verts.new(grid[i][j] + dirs[i][j] * thick) for j in range(m)] for i in range(n)]
    I = [[bm.verts.new(grid[i][j]) for j in range(m)] for i in range(n)]
    uv = {}
    vv = [0.0]
    for i in range(1, n): vv.append(vv[-1] + (grid[i][m // 2] - grid[i - 1][m // 2]).length)
    for i in range(n):
        uu = [0.0]
        for j in range(1, m): uu.append(uu[-1] + (grid[i][j] - grid[i][j - 1]).length)
        for j in range(m): uv[O[i][j]] = uv[I[i][j]] = (uu[j] * s, vv[i] * s)
    F = []
    jr = range(m) if closed else range(m - 1)
    for i in range(n - 1):
        for j in jr:
            j2 = (j + 1) % m
            F.append(bm.faces.new((O[i][j], O[i][j2], O[i + 1][j2], O[i + 1][j]))); F.append(bm.faces.new((I[i + 1][j], I[i + 1][j2], I[i][j2], I[i][j])))
    for j in jr:
        j2 = (j + 1) % m
        F.append(bm.faces.new((O[0][j2], O[0][j], I[0][j], I[0][j2]))); F.append(bm.faces.new((O[-1][j], O[-1][j2], I[-1][j2], I[-1][j])))
    if not closed:
        for i in range(n - 1):
            F.append(bm.faces.new((O[i][0], O[i + 1][0], I[i + 1][0], I[i][0]))); F.append(bm.faces.new((O[i + 1][-1], O[i][-1], I[i][-1], I[i + 1][-1])))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    lay = bm.loops.layers.uv.new('UVMap')
    for f in bm.faces:
        for l in f.loops: l[lay].uv = uv[l.vert]
    return finish(bm, bone, mat, uv=False)


def tube(pts, r, bone, mat, seg=6, closed=False):   # trim, straps, reins, rings
    pts = [Vector(p) for p in pts]; n = len(pts); bm = bmesh.new(); rows = []
    for i, p in enumerate(pts):
        if closed: t = (pts[(i + 1) % n] - pts[i - 1]).normalized()
        else: t = (pts[min(i + 1, n - 1)] - pts[max(i - 1, 0)]).normalized()
        a = t.orthogonal().normalized(); b = t.cross(a)
        rows.append([bm.verts.new(p + (a * math.cos(2 * math.pi * k / seg) + b * math.sin(2 * math.pi * k / seg)) * r) for k in range(seg)])
    for i in range(n if closed else n - 1):
        r0, r1 = rows[i], rows[(i + 1) % n]
        for k in range(seg): bm.faces.new((r0[k], r0[(k + 1) % seg], r1[(k + 1) % seg], r1[k]))
    if not closed: bm.faces.new(list(reversed(rows[0]))); bm.faces.new(rows[-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return finish(bm, bone, mat)


def band(pts, normals, w, t, bone, mat):   # a flat strap following points, lying on the surface (normals out), w wide
    n = len(pts); bm = bmesh.new(); rows = []
    for i in range(n):
        tg = (pts[min(i + 1, n - 1)] - pts[max(i - 1, 0)]).normalized(); nn = normals[i]; sd = tg.cross(nn).normalized()
        rows.append([bm.verts.new(pts[i] + sd * (w / 2) * a + nn * t * b) for a, b in ((-1, 0), (1, 0), (1, 1), (-1, 1))])
    for i in range(n - 1):
        for k in range(4): bm.faces.new((rows[i][k], rows[i][(k + 1) % 4], rows[i + 1][(k + 1) % 4], rows[i + 1][k]))
    bm.faces.new(list(reversed(rows[0]))); bm.faces.new(rows[-1]); bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return finish(bm, bone, mat)


def cone(base, d, r, h, bone, mat='steel', seg=8):
    d = Vector(d).normalized(); a = d.orthogonal().normalized(); b = d.cross(a); bm = bmesh.new()
    ring = [bm.verts.new(base + (a * math.cos(2 * math.pi * k / seg) + b * math.sin(2 * math.pi * k / seg)) * r) for k in range(seg)]
    tip = bm.verts.new(base + d * h)
    for k in range(seg): bm.faces.new((ring[k], ring[(k + 1) % seg], tip))
    bm.faces.new(list(reversed(ring))); bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return finish(bm, bone, mat)


def ring(c, axis, R, r, bone, mat='brass', seg=12):
    axis = Vector(axis).normalized(); a = axis.orthogonal().normalized(); b = axis.cross(a)
    return tube([c + (a * math.cos(2 * math.pi * k / seg) + b * math.sin(2 * math.pi * k / seg)) * R for k in range(seg)], r, bone, mat, seg=5, closed=True)


def edge_trim(grid, dirs, which, bone, r=0.006, thick=0.0):   # a brass bead along one edge of a fitted sheet: 'top', 'bottom', 'left', 'right'
    if which == 'top': pts = [grid[0][j] + dirs[0][j] * thick for j in range(len(grid[0]))]
    elif which == 'bottom': pts = [grid[-1][j] + dirs[-1][j] * thick for j in range(len(grid[0]))]
    elif which == 'left': pts = [grid[i][0] + dirs[i][0] * thick for i in range(len(grid))]
    else: pts = [grid[i][-1] + dirs[i][-1] * thick for i in range(len(grid))]
    return tube(pts, r, bone, 'brass', seg=5)



# ---------------------------------------------------------------- EK3 detail: rivets, etched gold borders, ridges, flutes, fringe
def gp(g, d, i, j, off): return g[i][j] + d[i][j] * off
def row(g, d, i, off): return [gp(g, d, i, j, off) for j in range(len(g[0]))]
def col(g, d, j, off): return [gp(g, d, i, j, off) for i in range(len(g))]
def outside(p, gap):   # a point moved out of the skin (and gap clear of it) along the nearest skin's normal
    loc, nrm, fi, dist = BVH.find_nearest(p, 1.0)
    if loc is not None and (p - loc).dot(nrm) < gap: return loc + nrm * gap
    return p
def rivet(p, n, bone, r=0.0075, mat='brass'): cone(p - n * 0.002, n, r, r * 0.9, bone, mat, seg=6)
def rivet_edges(g, d, thick, bone, which=('top', 'bottom', 'left', 'right'), every=2, inset=1, r=0.0075, mat='brass'):
    n, m = len(g), len(g[0])
    for w in which:
        if w in ('top', 'bottom'):
            i = inset if w == 'top' else n - 1 - inset
            for j in range(inset, m - inset, every): rivet(gp(g, d, i, j, thick), d[i][j], bone, r, mat)
        else:
            j = inset if w == 'left' else m - 1 - inset
            for i in range(inset, n - inset, every): rivet(gp(g, d, i, j, thick), d[i][j], bone, r, mat)
def etched(g, d, thick, bone, inset=1, w=0.012):   # a thin gold band inset from every edge: the etched border of a plate
    n, m = len(g), len(g[0])
    if n - 2 * inset < 2 or m - 2 * inset < 2: return
    lines = [([gp(g, d, inset, j, thick + 0.001) for j in range(inset, m - inset)], [d[inset][j] for j in range(inset, m - inset)]),
             ([gp(g, d, n - 1 - inset, j, thick + 0.001) for j in range(inset, m - inset)], [d[n - 1 - inset][j] for j in range(inset, m - inset)]),
             ([gp(g, d, i, inset, thick + 0.001) for i in range(inset, n - inset)], [d[i][inset] for i in range(inset, n - inset)]),
             ([gp(g, d, i, m - 1 - inset, thick + 0.001) for i in range(inset, n - inset)], [d[i][m - 1 - inset] for i in range(inset, n - inset)])]
    for pts, nn in lines: band(pts, nn, w, 0.002, bone, 'brass')
def ridge(pts, bone, r=0.009, mat='steel'): tube(pts, r, bone, mat, seg=6)
def fringe(hem, side, bone, step=0.024, L=0.07):   # gold fringe: short cords hanging from a hem, each tipped with a bead
    pts = [p for p, t, s_ in resample(hem, max(2, int(sum((b - a).length for a, b in zip(hem, hem[1:])) / step)))]
    for k, p in enumerate(pts):
        l = L * (0.85 + 0.3 * ((k * 7) % 5) / 4); q = p + Vector((0.004 * side, 0, -l))
        tube([p, q], 0.0035, bone, 'brass', seg=4); cone(q + Vector((0, 0, 0.008)), Vector((0, 0, -1)), 0.007, 0.016, bone, 'brass', seg=5)
def rondel(c, n, R, bone, spike=0.05):   # a round boss: steel disc, gold ring, a ring of rivets, a gold or steel point
    cone(c, n, R, R * 0.2, bone, 'steel', seg=16); ring(c + n * 0.004, n, R, 0.008, bone, 'brass', seg=16)
    a = n.orthogonal().normalized(); b = n.cross(a)
    for k in range(8): rivet(c + n * (R * 0.1) + (a * math.cos(k * math.pi / 4) + b * math.sin(k * math.pi / 4)) * R * 0.7, n, bone, 0.006)
    cone(c + n * R * 0.15, n, R * 0.28, spike, bone, 'brass', seg=10)

# ---------------------------------------------------------------- EK4 ornament: patterns laid on a fitted plate's grid (row fi, column fj)
def SP(g, d, fi, fj, off):   # a point and normal anywhere on a grid, bilinear
    n, m = len(g), len(g[0]); fi = min(max(fi, 0), n - 1); fj = min(max(fj, 0), m - 1)
    i0, j0 = min(int(fi), n - 2), min(int(fj), m - 2); u, v = fi - i0, fj - j0
    w = ((i0, j0, (1 - u) * (1 - v)), (i0 + 1, j0, u * (1 - v)), (i0, j0 + 1, (1 - u) * v), (i0 + 1, j0 + 1, u * v))
    p = sum((g[i][j] * k for i, j, k in w), Vector()); nn = sum((d[i][j] * k for i, j, k in w), Vector()).normalized()
    return p + nn * off, nn
def gold_line(g, d, ab, off, bone, w=0.008):   # a thin gold inlay along (fi, fj) pairs
    P = [SP(g, d, a, b, off) for a, b in ab]
    if len(P) > 1: band([p for p, _ in P], [n for _, n in P], w, 0.002, bone, 'brass')
def lattice(g, d, off, bone, k=4, margin=0.08, studs=True):   # diamonds of gold inlay, a stud at every crossing
    n, m = len(g), len(g[0]); lo, hi = margin, 1 - margin
    for fam in (1, -1):
        for c in range(-k, 2 * k + 1):
            ab = []
            for s in range(41):
                a = s / 40; b = fam * a + c / k
                if lo <= a <= hi and lo <= b <= hi: ab.append((a * (n - 1), b * (m - 1)))
            if len(ab) > 3: gold_line(g, d, ab, off, bone)
    if studs:
        for c1 in range(-k, 2 * k + 1):
            for c2 in range(-k, 2 * k + 1):
                a = (c2 - c1) / (2 * k); b = (c1 + c2) / (2 * k)
                if lo + 0.02 <= a <= hi - 0.02 and lo + 0.02 <= b <= hi - 0.02:
                    p, nn = SP(g, d, a * (n - 1), b * (m - 1), off + 0.002); rivet(p, nn, bone, 0.008)
def scroll(g, d, off, bone, along, at, f0, f1, amp, waves):   # a vine scroll: a gold wave with a bead in every curl
    ab = []
    for s in range(61):
        t = f0 + (f1 - f0) * s / 60; q = at + amp * math.sin(waves * 2 * math.pi * s / 60); ab.append((t, q) if along == 'rows' else (q, t))
    gold_line(g, d, ab, off, bone, 0.007)
    for k in range(2 * waves):
        s = (k + 0.5) / (2 * waves); t = f0 + (f1 - f0) * s; q = at + amp * 1.6 * (1 if k % 2 == 0 else -1)
        p, nn = SP(g, d, *((t, q) if along == 'rows' else (q, t)), off + 0.002); rivet(p, nn, bone, 0.006)
def lappets(hem, out, down, bone, n, depth, thick=0.008):   # a row of pointed steel lappets hanging from a hem, gold-edged (dagged)
    pts = [p for p, _, _ in resample(hem, 4 * n + 1)]   # each lappet follows the hem's own curve (4 spans), its point at the middle
    for k in range(n):
        q = pts[4 * k: 4 * k + 5]; nrm = [(BVH.find_nearest(x, 1.0)[1] or out) for x in q]
        tip = q[2] + down * depth + nrm[2] * 0.006
        f = (0.8, 0.9, 1.0, 0.9, 0.8); row2 = [x.lerp(tip, f_) for x, f_ in zip(q, f)]
        shell([q, row2], [nrm, nrm], thick, bone, 'steel'); tube([q[0] + nrm[0] * thick, tip + nrm[2] * thick, q[4] + nrm[4] * thick], 0.0045, bone, 'brass', seg=5)
def escutcheon(g, d, fi0, fj0, hi, hj, off, bone, charge='cross'):   # a heraldic shield plate on a plate, gold-bordered, a gold charge
    rows, cols = 8, 7; grid, dirs = [], []
    for r in range(rows):
        v = r / (rows - 1); w = 1.0 if v < 0.5 else max(0.04, 1 - ((v - 0.5) / 0.5) ** 1.6)
        rr = [SP(g, d, fi0 + v * hi, fj0 + (c / (cols - 1) * 2 - 1) * hj * w, off) for c in range(cols)]
        grid.append([p for p, _ in rr]); dirs.append([nn for _, nn in rr])
    shell(grid, dirs, 0.008, bone, 'steel')
    outline = [grid[0][c] for c in range(cols)] + [grid[r][-1] for r in range(1, rows)] + [grid[r][0] for r in range(rows - 2, 0, -1)]
    tube([p + dirs[0][0] * 0.008 for p in outline] + [grid[0][0] + dirs[0][0] * 0.008], 0.006, bone, 'brass', seg=5)
    on = lambda r, c: (grid[r][c] + dirs[r][c] * 0.0095, dirs[r][c])
    if charge == 'cross':
        for path in ([on(r, cols // 2) for r in range(1, rows - 1)], [on(3, c) for c in range(1, cols - 1)]):
            band([p for p, _ in path], [n for _, n in path], 0.03, 0.004, bone, 'brass')
    else:   # a chevron and a stud
        path = [on(5, 1), on(2, cols // 2), on(5, cols - 2)]; band([p for p, _ in path], [n for _, n in path], 0.025, 0.004, bone, 'brass')
        p, nn = on(5, cols // 2); rondel(p, nn, 0.03, bone, 0.02)
def sunburst(g, d, fi0, fj0, off, bone, rays=9, a0=-80, a1=80, r0=1.2, scale=(1.0, 1.0)):   # flutes radiating from one point
    n, m = len(g), len(g[0])
    for k in range(rays):
        a = math.radians(a0 + (a1 - a0) * k / (rays - 1)); ab = []
        for s in range(40):
            r = r0 + s * 0.4; fi, fj = fi0 - r * math.cos(a) * scale[0], fj0 + r * math.sin(a) * scale[1]
            if not (0.6 <= fi <= n - 1.6 and 0.6 <= fj <= m - 1.6): break
            ab.append((fi, fj))
        if len(ab) > 2:
            P = [SP(g, d, a_, b_, off) for a_, b_ in ab]; tube([p for p, _ in P], 0.006 if k % 2 else 0.008, bone, 'steel' if k % 2 else 'brass', seg=6)

Z = Vector((0, 0, 1)); X = Vector((1, 0, 0)); Y = Vector((0, 1, 0))
sp3, sp2, sp1, sp0, pel = J(B['Spine_03']), J(B['Spine_02']), J(B['Spine_01']), J(B['Spine_00']), J(B['Pelvis'])
n0, n1, n2, n3, hd, h1 = J(B['Neck_00']), J(B['Neck_01']), J(B['Neck_02']), J(B['Neck_03']), J(B['Head_00']), J(B['Head_01'])
tail = J(B['Tail_00'])
BODY_Z = sp1.z - 0.2   # the barrel's centre line, inside the body
body_axis = [Vector((0, y, BODY_Z)) for y in (sp3.y - 0.1, sp2.y, sp1.y, sp0.y, pel.y, tail.y + 0.06)]
body_axis = [q for k, q in enumerate(body_axis) if k == 0 or abs(q.y - body_axis[k - 1].y) > 0.05]   # rigs that share a spine joint: no zero-length span


# ---------------------------------------------------------------- MK1 the moose knight's barding, after the user's reference (layout only):
# a full face plate, plates round the front and sides of the neck, a chest plate with a round boss on the left shoulder, a saddle
# on a blue cloth hanging down both flanks (gold hem), one smooth steel shell over the rump with a blue skirt, blue reins; bare legs
SB = 0.07
def hang(top, side, depth, bone, mat, trim=True):   # a cloth curtain hanging from a row of points, gold hem
    grid = [top, [q + Vector((0.02 * side, 0, -depth * 0.5)) for q in top], [q + Vector((0.03 * side, 0, -depth)) for q in top]]
    shell(grid, [[Vector((side, 0, 0))] * len(top)] * 3, 0.007, bone, mat)
    if trim: tube(grid[-1], 0.009, bone, 'brass', seg=5); fringe(grid[-1], side, bone)
    return grid
SECTION = 'cloth'
# saddle cloth: blue over the back and down both flanks
for t0, t1, bone in ((0.04 + SB, 0.33 + SB, 'Spine_02'), (0.31 + SB, 0.6 + SB, 'Spine_01')):
    g, d = surface(body_axis, 8, t0, t1, -84, 84, 20, Z, 0.014); shell(g, d, 0.008, bone, 'blue')
    for jj, side in ((0, -1), (len(g[0]) - 1, 1)):
        top = [g[i][jj] for i in range(len(g))]; side = 1 if top[0].x > 0 else -1
        hang(top, side, 0.38, bone, 'blue')
SECTION = 'saddle'
# the saddle: leather seat, steel pommel and cantle plates (the rider's seat as on the horse)
g, d = surface(body_axis, 6, 0.17 + SB, 0.37 + SB, -26, 26, 10, Z, 0.036); shell(g, d, 0.035, 'Spine_01', 'leather')
SEAT = g[3][5] + d[3][5] * 0.035
HANDS = {1: SEAT + Vector((0.12, -0.18, 0.3)), -1: SEAT + Vector((-0.12, -0.18, 0.3))}
TREAD = {}
seat_front = resample(body_axis, 2, 0.15 + SB, 0.15 + SB)[0][0]; seat_back = resample(body_axis, 2, 0.39 + SB, 0.39 + SB)[0][0]
def cross_plate(y_c, a0, a1, off, height, lean, bone, mat='steel', rows=4):
    axis = [Vector((0, y_c - 0.01, BODY_Z)), Vector((0, y_c + 0.01, BODY_Z))]
    g, d = surface(axis, 2, 0.5, 0.5, a0, a1, 14, Z, off); base = g[0]; grid = []
    for k in range(rows):
        t = k / (rows - 1); grid.append([q + Vector((0, lean * t, height * t)) + (q - Vector((0, q.y, BODY_Z))).normalized() * 0.03 * t * t for q in base])
    gd = [[Vector((0, -1 if lean < 0 else 1, 0)) for _ in base] for _ in grid]; shell(grid, gd, 0.012, bone, mat); tube(grid[-1], 0.007, bone, 'brass', seg=5)
    etched(grid, gd, 0.012, bone, 1, 0.01); rivet_edges(grid, gd, 0.012, bone, ('left', 'right'), 1, 0, 0.006)
cross_plate(seat_front.y, -30, 30, 0.05, 0.12, -0.04, 'Spine_02'); cross_plate(seat_back.y, -32, 32, 0.05, 0.14, 0.05, 'Spine_01')
g, d = surface(body_axis, 2, 0.2 + SB, 0.2 + SB, -179, 179, 36, Z, 0.012); band(g[0] + [g[0][0]], d[0] + [d[0][0]], 0.07, 0.006, 'Spine_02', 'leather')   # girth
for S, side in (('L', 1), ('R', -1)):   # stirrups, as on the horse (the rider's feet)
    g, d = surface(body_axis, 2, 0.27 + SB, 0.27 + SB, 66 * side, 70 * side, 2, Z, 0.05)
    top = g[0][0]; bot = top + Vector((0.03 * side, 0, -0.42))
    band([top, top.lerp(bot, 0.5) + Vector((0.01 * side, 0, 0)), bot], [Vector((side, 0, 0))] * 3, 0.035, 0.005, S + '_Stirrup', 'leather')
    pts = [bot + Vector((0.0, 0.065 * math.cos(math.radians(a)), -0.06 - 0.06 * math.sin(math.radians(a)))) for a in range(0, 361, 30)]
    tube(pts[:-1], 0.008, S + '_Stirrup', 'iron', seg=6, closed=True); tube([bot + Vector((0, -0.06, -0.12)), bot + Vector((0, 0.06, -0.12))], 0.012, S + '_Stirrup', 'iron', seg=6)
    TREAD[S] = bot + Vector((0, 0, -0.12))
SECTION = 'crupper'
# crupper (EK4): the reference's one smooth shell over the rump, in the horse crupper's frame, now ornamented rather than ribbed:
# a keel ridge, gold diamond lattice with a stud at every crossing, a heraldic shield on the croup, a rondel on each haunch,
# dagged steel lappets over the blue fringed skirt, a tail guard
g, d = surface(body_axis, 12, 0.6, 1.0, -84, 84, 24, Z, 0.022, grow=1)
shell(g, d, 0.012, 'Pelvis', 'steel')
for w_ in ('top', 'left', 'right'): edge_trim(g, d, w_, 'Pelvis', 0.011, 0.012)
etched(g, d, 0.012, 'Pelvis', 1); rivet_edges(g, d, 0.012, 'Pelvis', ('top', 'left', 'right'), 2, 1)
n_, m_ = len(g), len(g[0]); mid_j = m_ // 2
ridge(col(g, d, mid_j, 0.014)[:4], 'Pelvis', 0.013)
lattice(g, d, 0.0125, 'Pelvis', k=3, margin=0.1)
escutcheon(g, d, 4.0, mid_j, 5.5, 3.2, 0.016, 'Pelvis', 'cross')
for jj in (4, m_ - 5): rondel(gp(g, d, 6, jj, 0.016), d[6][jj], 0.075, 'Pelvis', 0.045)
for jj in (0, m_ - 1):
    top = [g[i][jj] for i in range(n_)]; side = 1 if top[0].x > 0 else -1; out = Vector((side, 0, 0))
    hang(top, side, 0.26, 'Pelvis', 'blue')
    lappets([q + out * 0.014 for q in top], out, Vector((0, 0, -1)), 'Pelvis', 9, 0.07)
try:   # tail guard: a fluted half-tube over the root of the tail, gold-ridged
    t0_, t1_ = J(B['Tail_00']), JT(B['Tail_00'])
    tg, td = surface([t0_ - (t1_ - t0_) * 0.4, t1_ + (t1_ - t0_) * 0.3], 4, 0.0, 1.0, -95, 95, 10, Z, 0.012, maxr=0.25)
    shell(tg, td, 0.008, 'Tail_00', 'steel'); edge_trim(tg, td, 'bottom', 'Tail_00', 0.006, 0.008); ridge(col(tg, td, 5, 0.01), 'Tail_00', 0.006, 'brass')
except Exception as e: print('tail guard skipped', e)
SECTION = 'upperback'
# upper back (EK4): two withers lames with a shield on the hump, then loin lames behind the saddle, each different
withers_axis = [Vector((0, n0.y + 0.06, n0.z - 0.16)), Vector((0, sp3.y + 0.02, BODY_Z + 0.02)), Vector((0, sp2.y - 0.05, BODY_Z))]
for k, (t0, t1) in enumerate(((0.0, 0.45), (0.38, 1.0))):
    bone = 'Neck_00' if k == 0 else 'Spine_03'
    g, d = surface(withers_axis, 6, t0, t1, -78, 78, 16, Z, 0.034 - 0.008 * k, grow=1, maxr=0.8)
    shell(g, d, 0.011, bone, 'steel')
    for w_ in ('top', 'bottom', 'left', 'right'): edge_trim(g, d, w_, bone, 0.007, 0.011)
    if k == 0: scroll(g, d, 0.012, bone, 'cols', 2.5, 2, 13, 0.8, 4); rivet_edges(g, d, 0.011, bone, ('left', 'right'), 1, 1, 0.006)
    else: escutcheon(g, d, 0.8, 7.5, 4.0, 3.0, 0.015, bone, 'chevron'); rivet_edges(g, d, 0.011, bone, ('top', 'bottom'), 2, 1, 0.006)
for k in range(3):
    t0 = 0.455 + k * 0.05; bone = 'Spine_01' if k < 2 else 'Pelvis'
    g, d = surface(body_axis, 3, t0, t0 + 0.07, -62, 62, 14, Z, 0.026 + 0.006 * k, grow=1)
    shell(g, d, 0.01, bone, 'steel'); edge_trim(g, d, 'bottom', bone, 0.007, 0.01)
    if k == 1: rondel(gp(g, d, 1, 7, 0.012), d[1][7], 0.04, bone, 0.025)
    else: gold_line(g, d, [(1, j) for j in range(1, 13)], 0.011, bone, 0.01)
    rivet_edges(g, d, 0.01, bone, ('bottom',), 2, 0)
SECTION = 'peytral'
# peytral (EK4): sunburst flutes fanning down from the top centre, a gold vine scroll across the top, dagged lappets along the
# bottom, the rings and straps of the horse's peytral, and the reference's round boss on the left shoulder
chest_axis = [Vector((0, sp3.y - 0.05, BODY_Z + 0.0)), Vector((0, sp3.y - 0.05, BODY_Z + 0.13))]   # the brisket between the shoulders' points (they sweep its sides; above it is the neck's underside)
g, d = surface(chest_axis, 7, 0.0, 1.0, -20, 20, 24, lambda p: Vector((0, -1, 0)), 0.022, grow=1, maxr=0.7)
shell(g, d, 0.012, 'Spine_03', 'steel')
for w_ in ('top', 'left', 'right'): edge_trim(g, d, w_, 'Spine_03', 0.009, 0.012)
etched(g, d, 0.012, 'Spine_03'); rivet_edges(g, d, 0.012, 'Spine_03', ('top',), 2)
mj = len(g[0]) // 2
sunburst(g, d, len(g) - 1.2, mj, 0.012, 'Spine_03', rays=11, a0=-75, a1=75, r0=1.0, scale=(1.0, 2.2))
scroll(g, d, 0.013, 'Spine_03', 'cols', len(g) - 1.8, 2, len(g[0]) - 3, 0.35, 6)
jb = len(g[0]) - 6 if g[3][-4].x > 0 else 5   # the left side of the breast (+x), clear of the shoulder's point
rondel(gp(g, d, 4, jb, 0.025), d[4][jb], 0.08, 'Spine_03', 0.04)
# (no straps back to the saddle: they cross the shoulders, which swing through anything laid over them)
SECTION = 'crinet'
# crinet (EK4): throat plates and crest lames, no two alike in a row: scrolls, rondels, inlay, pointed (cusped) leading edges
NECK_IN = Vector((0, -0.45, -1.0)).normalized() * 0.1
neck_axis = [n0 + NECK_IN * 0.6, n1 + NECK_IN, n2 + NECK_IN, n3 + NECK_IN, hd + Vector((0, 0.03, -0.06))]
def nbone(t0): return 'Neck_00' if t0 < 0.22 else 'Neck_01' if t0 < 0.45 else 'Neck_02' if t0 < 0.7 else 'Neck_03'
NL = 6
neck_down = lambda p: Vector((0, -0.55, -1.0))
for k in range(NL):
    t0, t1 = 0.34 + k / NL * 0.6, 0.34 + (k + 1.3) / NL * 0.6; bone = nbone(t0)   # clear of the shoulders' points (they rise into the throat at the gallop)
    g, d = surface(neck_axis, 4, t0, min(t1, 0.8), -72, 72, 20, neck_down, 0.016 + 0.003 * (NL - k), maxr=0.5)
    shell(g, d, 0.01, bone, 'steel'); edge_trim(g, d, 'bottom', bone, 0.006, 0.01)
    if k % 2 == 0: scroll(g, d, 0.011, bone, 'cols', 1.5, 2, 17, 0.45, 5)
    else: gold_line(g, d, [(1.2, j) for j in range(1, 19)], 0.011, bone, 0.012); rivet_edges(g, d, 0.01, bone, ('top',), 2, 1, 0.006)
    hem = row(g, d, len(g) - 1, 0.01); fw = (row(g, d, 3, 0)[10] - row(g, d, 2, 0)[10]).normalized()
    for j in (6, 13): rivet(gp(g, d, 2, j, 0.01), d[2][j], bone, 0.007)
SECTION = 'crest'
NT = 8
neck_up = lambda p: Vector((0, 0.55, 1.0))
for k in range(NT):
    t0, t1 = k / NT * 0.92, (k + 1.4) / NT * 0.92; bone = nbone(t0)
    g, d = surface(neck_axis, 4, t0, min(t1, 0.98), -72, 72, 14, neck_up, 0.024 + 0.003 * (NT - k), maxr=0.45)
    shell(g, d, 0.01, bone, 'steel'); edge_trim(g, d, 'left', bone, 0.006, 0.01); edge_trim(g, d, 'right', bone, 0.006, 0.01)
    fw = (g[-1][7] - g[-2][7]).normalized()   # the lame's leading edge (toward the head) gets cusps: a gothic point at its crest
    lappets([gp(g, d, 3, j, 0.01) for j in range(14)], d[3][7], fw, bone, 3 if k % 2 else 1, 0.04 if k % 2 else 0.06)
    if k % 3 == 0: rondel(gp(g, d, 1, 7, 0.012), d[1][7], 0.03, bone, 0.03)
    elif k % 3 == 1: ridge(col(g, d, 7, 0.012), bone, 0.008); scroll(g, d, 0.011, bone, 'cols', 1.2, 1, 6, 0.35, 2); scroll(g, d, 0.011, bone, 'cols', 1.2, 8, 13, 0.35, 2)
    else: gold_line(g, d, [(i, 7 + 4 * math.sin(i * 1.3)) for i in (0.4, 1.0, 1.6, 2.2, 2.8)], 0.011, bone, 0.008); rivet_edges(g, d, 0.01, bone, ('top',), 2, 0, 0.006)
SECTION = 'chanfron'
# chanfron (EK4): the face plate with a medial ridge, vine scrolls either side, an etched border; hinged cheek plates; pierced
# domed eye guards; a nose band; a forehead rondel
head_dir = (h1 - hd).normalized(); muzzle = hd + head_dir * 0.55
if 'HEAD_TIP' in globals(): head_dir = (HEAD_TIP - hd).normalized(); muzzle = HEAD_TIP - head_dir * 0.03
head_axis = [hd + Vector((0, 0.02, 0.0)), hd.lerp(muzzle, 0.5), muzzle]
face_up = lambda p: head_dir.cross(X)
g, d = surface(head_axis, 12, 0.16, 0.9, -100, 100, 20, face_up, 0.012, grow=1, maxr=0.35, outer=True)   # starts in front of the ears (they twitch)
shell(g, d, 0.01, 'Head_00', 'steel')
for w_ in ('left', 'right', 'bottom', 'top'): edge_trim(g, d, w_, 'Head_00', 0.006, 0.01)
etched(g, d, 0.01, 'Head_00'); rivet_edges(g, d, 0.01, 'Head_00', ('left', 'right'), 2, 1, 0.006)
mj = len(g[0]) // 2; ridge(col(g, d, mj, 0.012)[2:-1], 'Head_00', 0.008)
for c_ in (mj - 4, mj + 4): scroll(g, d, 0.011, 'Head_00', 'rows', c_, 4.5, 10.5, 0.9, 3)
rondel(gp(g, d, 1, mj, 0.014), d[1][mj], 0.055, 'Head_00', 0.07)
for jj in (3, len(g[0]) - 4):   # eye guards: a domed cup with three slots, on a gold ring
    c = gp(g, d, 3, jj, 0.012); n = d[3][jj]; cone(c, n, 0.042, 0.03, 'Head_00', 'steel', seg=14); ring(c, n, 0.042, 0.007, 'Head_00', 'brass', seg=14)
    a = n.cross(head_dir).normalized()
    for k in (-1, 0, 1): tube([c + n * 0.016 + head_dir * 0.012 * k - a * 0.025, c + n * 0.016 + head_dir * 0.012 * k + a * 0.025], 0.003, 'Head_00', 'iron', seg=4)
nb = surface(head_axis, 3, 0.7, 0.78, -125, 125, 18, face_up, 0.026, maxr=0.35, outer=True)
shell(*nb, 0.008, 'Head_00', 'steel'); edge_trim(*nb, 'top', 'Head_00', 0.005, 0.008); edge_trim(*nb, 'bottom', 'Head_00', 0.005, 0.008)
for j in range(1, 18, 3): rivet(gp(*nb, 1, j, 0.008), nb[1][1][j], 'Head_00', 0.006)
for side in (1, -1):   # cheek plates below the face plate's edges, on two hinges each, a gold lozenge on each
    a0, a1 = (100, 150) if side > 0 else (-150, -100)
    g2, d2 = surface(head_axis, 6, 0.08, 0.6, a0, a1, 7, face_up, 0.026, maxr=0.4, outer=True)
    shell(g2, d2, 0.009, 'Head_00', 'steel')
    for w_ in ('left', 'right', 'bottom', 'top'): edge_trim(g2, d2, w_, 'Head_00', 0.005, 0.009)
    gold_line(g2, d2, [(2.5, 1.5), (1.0, 3.0), (2.5, 4.5), (4.0, 3.0), (2.5, 1.5)], 0.01, 'Head_00', 0.008); rivet(*SP(g2, d2, 2.5, 3.0, 0.011), 'Head_00', 0.01)
    jj = 0 if side * (g2[0][0].x - g2[0][-1].x) > 0 else 6; jj = 6 - jj
    for i in (1, 4): q = gp(g2, d2, i, jj, 0.01); tube([q - head_dir * 0.02, q + head_dir * 0.02], 0.007, 'Head_00', 'brass', seg=6)
SECTION = 'poll'
# poll and nape (EK4): a cap over the back of the head between and behind the antlers and ears, side plates below the ears,
# a gold-ringed plume socket on the cap, all lapping onto the crest lames
back_up = Vector((0, 0.45, 1.0)).normalized()
poll_axis = [Vector((hd.x, ANTLER_Y + 0.045, hd.z + 0.03)), Vector((hd.x, hd.y + 0.09, hd.z - 0.1)), Vector((n3.x, n3.y + 0.01, n3.z + 0.02))]
g, d = surface(poll_axis, 6, 0.0, 1.0, -30, 30, 10, lambda p: back_up, 0.014, grow=1, maxr=0.3, outer=True)
shell(g, d, 0.009, 'Head_00', 'steel')
for w_ in ('top', 'bottom', 'left', 'right'): edge_trim(g, d, w_, 'Head_00', 0.005, 0.009)
ridge(col(g, d, 5, 0.011)[1:], 'Head_00', 0.007, 'brass'); rivet_edges(g, d, 0.009, 'Head_00', ('left', 'right'), 1, 1, 0.006)
pc, pn = SP(g, d, 1.2, 4.5, 0.011); cone(pc, pn, 0.022, 0.05, 'Head_00', 'steel', seg=10); ring(pc + pn * 0.04, pn, 0.016, 0.004, 'Head_00', 'brass')
for side in (1, -1):
    a0, a1 = (88, 132) if side > 0 else (-132, -88)   # below and behind the ears, which move
    g2, d2 = surface(poll_axis, 5, 0.35, 1.0, a0, a1, 7, lambda p: back_up, 0.016, grow=1, maxr=0.3, outer=True)
    shell(g2, d2, 0.009, 'Head_00', 'steel')
    for w_ in ('top', 'bottom', 'left', 'right'): edge_trim(g2, d2, w_, 'Head_00', 0.005, 0.009)
    escutcheon(g2, d2, 0.6, 3.0, 3.0, 2.0, 0.011, 'Head_00', 'chevron')
SECTION = 'ears'
# ear guards (EK4): fluted open cones along each ear, gold-ringed at the root
for S_ in (('L', 'R') if os.environ.get('EAR_GUARDS') else ()):   # off: the ear flaps flex through any rigid cup
    if S_ not in EARS: continue
    e0, e1 = EARS[S_]; ed = (e1 - e0).normalized(); L = (e1 - e0).length; grid, dd = [], []
    for k in range(5):
        t = k / 4; c = e0 + ed * (0.22 * L + 0.5 * L * t); r = 0.08 - 0.03 * t; a = ed.orthogonal().normalized(); b = ed.cross(a)
        rr = [c + (a * math.cos(2 * math.pi * j / 12) + b * math.sin(2 * math.pi * j / 12)) * r * (1.0 if j % 2 else 1.08) for j in range(12)]
        grid.append(rr); dd.append([(q - c).normalized() for q in rr])
    shell(grid, dd, 0.005, S_ + '_Ear_00', 'steel', closed=True); tube(grid[0] + [grid[0][0]], 0.005, S_ + '_Ear_00', 'brass', seg=5); tube(grid[-1] + [grid[-1][0]], 0.004, S_ + '_Ear_00', 'brass', seg=5)
SECTION = 'reins'
# bridle and blue reins: bit to the pommel (the rider holds his lance; the reins lie on the neck)
pom = {}
for side in (1, -1): pom[side] = surface(body_axis, 2, 0.15 + SB, 0.15 + SB, 40 * side, 41 * side, 2, Z, 0.07)[0][0][0]
for side in (1, -1):
    bit = muzzle - head_dir * 0.06 + Vector((0.06 * side, 0, -0.02)); ring(bit, X, 0.024, 0.005, 'Head_00', 'iron')
    pts = [bit, n3 + Vector((0.1 * side, -0.05, -0.12)), n2 + Vector((0.14 * side, 0, -0.1)), n1 + Vector((0.16 * side, 0, -0.06)), pom[side]]
    pts = [pts[0]] + [outside(q, 0.025) for q in pts[1:-1]] + [pts[-1]]
    for (a, b), bone in zip(zip(pts, pts[1:]), ('Head_00', 'Neck_03', 'Neck_02', 'Neck_01')):
        band([a, a.lerp(b, 0.5) + Vector((0, 0, -0.03)), b], [Vector((side, 0, 0.3)).normalized()] * 3, 0.022, 0.005, bone, 'blue')
