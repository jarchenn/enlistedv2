# GM1 / GM2: bake the user's character exports with the gun built in (blender_anim/exports/<character>.zip, extracted
# outside the repo) into one game GLB per weapon: content/characters/<character>_<weapon>.glb.
#   python -I tools/bake_gunbody.py <character> <out dir> <extracted zip dir> [<more extracted dirs>...]
#     [--skip Clip,Clip...]   clips not baked (the game has no state that plays them)
#   e.g. gasmask <out> gas_mask_soldier gas_mask_soldier_new_anims; german <out> german_soldier; desert <out> desert_soldier
#   PNG textures with no transparency are re-encoded as JPEG (PIL); channels that never leave the rest pose are dropped and
#   identical key data is stored once.
# Each export has one folder per weapon, one GLB per clip, each a full copy of the mesh, rig and textures. Per weapon:
#   - the mesh, rig, materials and textures of the folder's first GLB in the last source dir that has the weapon, with the
#     clips of every GLB of that weapon folder in every source dir appended (name = file stem; a later dir's clip of the same
#     name replaces an earlier one). Every file must have the same node list;
#   - the joints renamed to the game's semantic names (character.cpp kSemanticNames), so hit zones and joint heads work;
#   - the ejected casing copies (Weapon_Eject*, a 55 k vertex case mesh each) and the muzzle-flash sprite dropped: the game
#     makes its own casings and flash (the flash at the 'flash' joint, which stays); normal maps and extra UV sets dropped;
#   - the root motion of the locomotion clips removed (_rootJoint translation): the game moves the body, the clips play in place.
# Plain JSON / binary handling (no Blender, nothing executed from the export). Output units as the export: metres, feet at 0,
# facing glTF +z.
import json, struct, sys, os, io, hashlib

SEM = {'Bone': 'Hips', 'spine1': 'Spine', 'spine2': 'Spine1', 'spine3': 'Spine2', 'neck': 'Neck', 'head': 'Head',
       'arm1.L': 'LeftShoulder', 'arm2.L': 'LeftArm', 'arm3.L': 'LeftForeArm', 'wrist.L': 'LeftHand',
       'arm1.R': 'RightShoulder', 'arm2.R': 'RightArm', 'arm3.R': 'RightForeArm', 'wrist.R': 'RightHand',
       'leg1.L': 'LeftUpLeg', 'leg2.L': 'LeftLeg', 'leg3.L': 'LeftFoot', 'leg4.L': 'LeftToeBase',
       'leg1.R': 'RightUpLeg', 'leg2.R': 'RightLeg', 'leg3.R': 'RightFoot', 'leg4.R': 'RightToeBase'}
SHORT = {'FN_FAL': 'fal', 'Pistol_9mm': 'pistol', 'Remington870': 'r870', 'P08_Luger': 'luger', 'CZ_Scorpion_Evo': 'scorpion', 'SCAR_L': 'scar'}
def short(folder): return SHORT.get(folder) or ''.join(c for c in folder.lower() if c.isalnum())

def strip_num(n):
    while '_' in n and n.rsplit('_', 1)[1].isdigit(): n = n.rsplit('_', 1)[0]
    return n

def load(p):
    b = open(p, 'rb').read()
    assert b[:4] == b'glTF', p
    n = struct.unpack_from('<I', b, 12)[0]; j = json.loads(b[20:20 + n])
    o = 20 + n; bl = struct.unpack_from('<I', b, o)[0]
    return j, b[o + 8:o + 8 + bl]

def dropped(name):
    return name.startswith('Weapon_Eject') or name.endswith('MuzzleFlash')

class Out:
    def __init__(self): self.bin = bytearray(); self.views = []; self.accs = []; self.memo = {}
    def view(self, key, j, bin, vi):
        if (key, vi) in self.memo: return self.memo[(key, vi)]
        v = j['bufferViews'][vi]; o = v.get('byteOffset', 0)
        while len(self.bin) % 4: self.bin += b'\0'
        nv = {'buffer': 0, 'byteOffset': len(self.bin), 'byteLength': v['byteLength']}
        for k in ('byteStride', 'target'):
            if k in v: nv[k] = v[k]
        self.bin += bin[o:o + v['byteLength']]
        self.views.append(nv); self.memo[(key, vi)] = len(self.views) - 1
        return len(self.views) - 1
    def acc(self, key, j, bin, ai, share=False):
        if (key, 'a', ai) in self.memo: return self.memo[(key, 'a', ai)]
        a = dict(j['accessors'][ai]); assert 'sparse' not in a
        if share:   # clip data: one accessor per distinct content (every channel of a clip shares its key times)
            v = j['bufferViews'][a['bufferView']]; o = v.get('byteOffset', 0) + a.get('byteOffset', 0)
            n = a['count'] * {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}[a['type']] * {5126: 4, 5123: 2, 5121: 1}[a['componentType']]
            assert 'byteStride' not in v
            h = hashlib.sha1(bin[o:o + n] + a['type'].encode() + str(a['componentType']).encode()).hexdigest()
            if ('h', h) in self.memo: self.memo[(key, 'a', ai)] = self.memo[('h', h)]; return self.memo[('h', h)]
            while len(self.bin) % 4: self.bin += b'\0'
            self.views.append({'buffer': 0, 'byteOffset': len(self.bin), 'byteLength': n}); self.bin += bin[o:o + n]
            a['bufferView'] = len(self.views) - 1; a.pop('byteOffset', None)
            self.accs.append(a); self.memo[(key, 'a', ai)] = self.memo[('h', h)] = len(self.accs) - 1
            return len(self.accs) - 1
        a['bufferView'] = self.view(key, j, bin, a['bufferView'])
        self.accs.append(a); self.memo[(key, 'a', ai)] = len(self.accs) - 1
        return len(self.accs) - 1

def floats(j, bin, ai):
    a = j['accessors'][ai]; v = j['bufferViews'][a['bufferView']]; o = v.get('byteOffset', 0) + a.get('byteOffset', 0)
    nc = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}[a['type']]
    if a['componentType'] != 5126: return None
    return [struct.unpack_from('<%df' % nc, bin, o + k * nc * 4) for k in range(a['count'])]

def at_rest(node, path, vals):   # a channel that never leaves the node's rest value: nothing to animate
    rest = {'translation': node.get('translation', [0, 0, 0]), 'rotation': node.get('rotation', [0, 0, 0, 1]), 'scale': node.get('scale', [1, 1, 1])}.get(path)
    if rest is None or vals is None or not vals: return False
    for v in vals:
        if path == 'rotation':
            if min(sum((v[k] - rest[k]) ** 2 for k in range(4)), sum((v[k] + rest[k]) ** 2 for k in range(4))) > 1e-8: return False
        elif any(abs(v[k] - rest[k]) > 1e-5 for k in range(3)): return False
    return True

def jpeg_if_opaque(data, mime, alpha_used):   # a PNG whose alpha nothing reads (opaque materials, metal / rough maps): JPEG
    if mime != 'image/png': return data, mime
    from PIL import Image
    im = Image.open(io.BytesIO(data)); im.load()
    if alpha_used and im.mode in ('RGBA', 'LA', 'P') and im.convert('RGBA').getextrema()[3][0] < 255: return data, mime
    out = io.BytesIO(); im.convert('RGB').save(out, 'JPEG', quality=90); return out.getvalue(), 'image/jpeg'

def bake(srcs, out_path, skip=()):
    files = {}   # clip name -> path (a later source dir replaces an earlier one)
    for src in srcs:
        for f in sorted(os.listdir(src)):
            if f.endswith('.glb') and f[:-4] not in skip: files[f[:-4]] = os.path.join(src, f)
    last = srcs[-1]
    base, bbin = load(os.path.join(last, sorted(f for f in os.listdir(last) if f.endswith('.glb'))[0]))
    names = [n.get('name', '') for n in base['nodes']]
    O = Out()
    j = {'asset': {'version': '2.0', 'generator': 'NormandyMapmaker tools/bake_gunbody.py'}, 'scene': 0, 'scenes': base['scenes']}
    nodes = []
    for n in base['nodes']:
        n = dict(n); nm = n.get('name', '')
        if dropped(nm): n.pop('mesh', None)
        if strip_num(nm) in SEM: n['name'] = SEM[strip_num(nm)]
        nodes.append(n)
    used = sorted({n['mesh'] for n in nodes if 'mesh' in n}); remap = {m: k for k, m in enumerate(used)}
    for n in nodes:
        if 'mesh' in n: n['mesh'] = remap[n['mesh']]
    j['nodes'] = nodes
    meshes = []
    for m in used:
        m = json.loads(json.dumps(base['meshes'][m]))
        for pr in m['primitives']:
            pr['attributes'] = {k: O.acc('b', base, bbin, a) for k, a in pr['attributes'].items() if k not in ('TEXCOORD_1', 'TEXCOORD_2')}   # unused
            if 'indices' in pr: pr['indices'] = O.acc('b', base, bbin, pr['indices'])
            pr.pop('targets', None)
        meshes.append(m)
    j['meshes'] = meshes
    skins = []
    for s in base['skins']:
        s = dict(s)
        if 'inverseBindMatrices' in s: s['inverseBindMatrices'] = O.acc('b', base, bbin, s['inverseBindMatrices'])
        skins.append(s)
    j['skins'] = skins
    # the materials without their normal maps (character.cpp reads them for first-person rigs only); only the images still used
    j['materials'] = json.loads(json.dumps(base.get('materials', [])))
    for m in j['materials']: m.pop('normalTexture', None); m.pop('occlusionTexture', None)
    j['samplers'] = base.get('samplers', [])
    used_m = {pr['material'] for m in meshes for pr in m['primitives'] if 'material' in pr}
    for i, m in enumerate(j['materials']):   # a material no kept mesh uses (the flash sprite's): no textures
        if i not in used_m: m.pop('emissiveTexture', None); m.get('pbrMetallicRoughness', {}).pop('baseColorTexture', None); m.get('pbrMetallicRoughness', {}).pop('metallicRoughnessTexture', None)
    used_t = sorted({m[k]['index'] for m in j['materials'] for m in [m, m.get('pbrMetallicRoughness', {})] for k in ('baseColorTexture', 'metallicRoughnessTexture', 'emissiveTexture') if k in m})
    tmap = {t: k for k, t in enumerate(used_t)}
    for m in j['materials']:
        for d in (m, m.get('pbrMetallicRoughness', {})):
            for k in ('baseColorTexture', 'metallicRoughnessTexture', 'emissiveTexture'):
                if k in d: d[k]['index'] = tmap[d[k]['index']]
    j['textures'] = [dict(base['textures'][t]) for t in used_t]
    used_i = sorted({t['source'] for t in j['textures']}); imap = {i: k for k, i in enumerate(used_i)}
    for t in j['textures']: t['source'] = imap[t['source']]
    alpha_imgs = set()   # character.cpp draws every material opaque (alphaMode is not read): no image's alpha is used
    j['images'] = []
    for i in used_i:
        im = dict(base['images'][i]); v = base['bufferViews'][im['bufferView']]; o = v.get('byteOffset', 0)
        data, im['mimeType'] = jpeg_if_opaque(bytes(bbin[o:o + v['byteLength']]), im.get('mimeType', ''), imap[i] in alpha_imgs)
        while len(O.bin) % 4: O.bin += b'\0'
        O.views.append({'buffer': 0, 'byteOffset': len(O.bin), 'byteLength': len(data)}); O.bin += data
        im['bufferView'] = len(O.views) - 1; j['images'].append(im)
    anims, report = [], []
    for clip in sorted(files):
        f = files[clip]
        cj, cbin = load(f)
        assert [n.get('name', '') for n in cj['nodes']] == names, f + ': a different node list from the base file'
        for a in cj.get('animations', []):
            na = {'name': clip, 'channels': [], 'samplers': []}
            dur = 0.0
            for ch in a['channels']:
                t = ch['target']; nm = names[t['node']]
                if dropped(nm) or (nm == '_rootJoint' and t['path'] == 'translation'): continue   # in place
                s = a['samplers'][ch['sampler']]
                dur = max(dur, cj['accessors'][s['input']].get('max', [0])[0])
                if at_rest(cj['nodes'][t['node']], t['path'], floats(cj, cbin, s['output'])): continue
                na['samplers'].append({'input': O.acc(f, cj, cbin, s['input'], True), 'output': O.acc(f, cj, cbin, s['output'], True), 'interpolation': s.get('interpolation', 'LINEAR')})
                na['channels'].append({'sampler': len(na['samplers']) - 1, 'target': dict(t)})
            if not na['channels']:   # a clip of the rest pose only: keep it findable (one channel)
                ch = a['channels'][0]; s = a['samplers'][ch['sampler']]
                na['samplers'].append({'input': O.acc(f, cj, cbin, s['input'], True), 'output': O.acc(f, cj, cbin, s['output'], True)}); na['channels'].append({'sampler': 0, 'target': dict(ch['target'])})
            anims.append(na); report.append('%s %.2fs' % (clip, dur))
    j['animations'] = anims
    j['accessors'] = O.accs; j['bufferViews'] = O.views
    while len(O.bin) % 4: O.bin += b'\0'
    j['buffers'] = [{'byteLength': len(O.bin)}]
    js = json.dumps(j, separators=(',', ':')).encode()
    while len(js) % 4: js += b' '
    total = 12 + 8 + len(js) + 8 + len(O.bin)
    with open(out_path, 'wb') as fo:
        fo.write(struct.pack('<III', 0x46546C67, 2, total))
        fo.write(struct.pack('<II', len(js), 0x4E4F534A)); fo.write(js)
        fo.write(struct.pack('<II', len(O.bin), 0x004E4942)); fo.write(O.bin)
    print(os.path.basename(out_path), '%.1f MB' % (total / 1e6), ', '.join(report))


# DF1: Mixamo-rig clips (skeleton + keys only, one file each) retargeted onto a body already in the game.
#   python -I tools/bake_gunbody.py --retarget <body.glb> <out.glb> <clip.glb>...   (clip name = file stem, '_' -> ' ')
#   Joints are matched by name (the part after ':'). Each matched joint takes the clip's model-space rotation change from its
#   own rest (world delta = posed world x rest world^-1, applied over the body's rest world), so bone axes and rest poses may
#   differ; joints the clips do not have keep their rest. The Hips travel is scaled by the hip heights (body / clip). The
#   locomotion clips (walk / run / sprint / strafe in the name; the standing throw) lose their horizontal drift (the straight line from the first
#   key to the last): they play in place, the sway stays. Output: the body's mesh, skin, materials and textures untouched,
#   plus the clips. Both files: metres, y up, +z forward, left +x (checked on the hips / legs).
def _qm(a, b):
    ax, ay, az, aw = a; bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx, aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)
def _qi(q): return (-q[0], -q[1], -q[2], q[3])
def _qn(q):
    n = sum(c * c for c in q) ** 0.5 or 1.0
    return tuple(c / n for c in q)
def _q2m(q):
    x, y, z, w = q
    return [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)], [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)], [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
def _mv(m, v): return [sum(m[i][k] * v[k] for k in range(3)) for i in range(3)]
def _mm(a, b): return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
def _inv3(m):
    a, b, c = m[0]; d, e, f = m[1]; g, h, i = m[2]
    det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g)
    return [[(e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det], [(f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det], [(d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det]]

class _Skel:
    def __init__(self, j):
        self.N = j['nodes']; self.par = {c: i for i, n in enumerate(self.N) for c in n.get('children', [])}
        self.order = []; seen = set()
        def visit(i):
            if i in seen: return
            if i in self.par: visit(self.par[i])
            seen.add(i); self.order.append(i)
        for i in range(len(self.N)): visit(i)
        self.name = [n.get('name', '').split(':')[-1] for n in self.N]
        self.rq = [_qn(tuple(n.get('rotation', [0, 0, 0, 1]))) for n in self.N]
        self.rt = [list(n.get('translation', [0, 0, 0])) for n in self.N]
        self.wq0 = self.world_rot(self.rq)
    def world_rot(self, loc):
        w = [None] * len(self.N)
        for i in self.order: w[i] = _qm(w[self.par[i]], loc[i]) if i in self.par else loc[i]
        return w
    def affine(self, i):   # rest: node -> model (3x3 with scale, translation)
        n = self.N[i]; R = _q2m(self.rq[i]); s = n.get('scale', [1, 1, 1]); L = [[R[r][c] * s[c] for c in range(3)] for r in range(3)]; t = self.rt[i]
        if i in self.par: PL, Pt = self.affine(self.par[i]); return _mm(PL, L), [a + b for a, b in zip(_mv(PL, t), Pt)]
        return L, list(t)

def bake_retarget(base_path, clip_paths, out_path):
    base, bbin = load(base_path)
    B = _Skel(base)
    O = Out()
    j = {'asset': {'version': '2.0', 'generator': 'NormandyMapmaker tools/bake_gunbody.py --retarget'}, 'scene': 0, 'scenes': base['scenes'], 'nodes': base['nodes']}
    meshes = []
    for m in base['meshes']:
        m = json.loads(json.dumps(m))
        for pr in m['primitives']:
            pr['attributes'] = {k: O.acc('b', base, bbin, a) for k, a in pr['attributes'].items()}
            if 'indices' in pr: pr['indices'] = O.acc('b', base, bbin, pr['indices'])
        meshes.append(m)
    j['meshes'] = meshes
    j['skins'] = []
    for s in base['skins']:
        s = dict(s); s['inverseBindMatrices'] = O.acc('b', base, bbin, s['inverseBindMatrices']); j['skins'].append(s)
    for k in ('materials', 'samplers', 'textures'):
        if k in base: j[k] = base[k]
    j['images'] = []
    for im in base.get('images', []):
        im = dict(im); im['bufferView'] = O.view('b', base, bbin, im['bufferView']); j['images'].append(im)
    joints = set(base['skins'][0]['joints'])
    bh = B.name.index('Hips'); BhL, BhT = B.affine(bh); BpL, BpT = B.affine(B.par[bh]) if bh in B.par else ([[1, 0, 0], [0, 1, 0], [0, 0, 1]], [0, 0, 0]); BpLi = _inv3(BpL)
    def raw(vals, typ):
        data = b''.join(struct.pack('<%df' % len(v), *v) for v in vals)
        while len(O.bin) % 4: O.bin += b'\0'
        O.views.append({'buffer': 0, 'byteOffset': len(O.bin), 'byteLength': len(data)}); O.bin += data
        nc = len(vals[0])
        O.accs.append({'bufferView': len(O.views) - 1, 'componentType': 5126, 'count': len(vals), 'type': typ, 'min': [min(v[c] for v in vals) for c in range(nc)], 'max': [max(v[c] for v in vals) for c in range(nc)]})
        return len(O.accs) - 1
    anims, report = [], []
    for f in clip_paths:
        clip = os.path.basename(f)[:-4].replace('_', ' ')
        cj, cbin = load(f); S = _Skel(cj); a = cj['animations'][0]
        ch = {}; times = None; tin = None
        for c in a['channels']:
            smp = a['samplers'][c['sampler']]; tv = [t[0] for t in floats(cj, cbin, smp['input'])]
            if times is None or len(tv) > len(times): times, tin = tv, smp['input']
            ch[(c['target']['node'], c['target']['path'])] = (tv, floats(cj, cbin, smp['output']))
        def at(key, t, n):   # linear sample (rotations nlerp)
            tv, vv = ch[key]
            if t <= tv[0]: return vv[0]
            if t >= tv[-1]: return vv[-1]
            k = max(0, min(len(tv) - 2, next(i for i in range(len(tv) - 1) if tv[i + 1] >= t))); u = (t - tv[k]) / ((tv[k + 1] - tv[k]) or 1)
            v0, v1 = vv[k], vv[k + 1]
            if n == 4 and sum(x * y for x, y in zip(v0, v1)) < 0: v1 = tuple(-x for x in v1)
            v = tuple(x + (y - x) * u for x, y in zip(v0, v1))
            return _qn(v) if n == 4 else v
        sh = S.name.index('Hips'); ShL, ShT = S.affine(sh); SpL, SpT = S.affine(S.par[sh]) if sh in S.par else ([[1, 0, 0], [0, 1, 0], [0, 0, 1]], [0, 0, 0])
        k_h = BhT[1] / ShT[1]
        smap = {S.name[i]: i for i in range(len(S.N))}
        mapped = [i for i in B.order if i in joints and B.name[i] in smap]
        rot = {i: [] for i in mapped}; hipT = []
        for t in times:
            sl = [at((i, 'rotation'), t, 4) if (i, 'rotation') in ch else S.rq[i] for i in range(len(S.N))]
            sw = S.world_rot(sl)
            bl = list(B.rq); bw = [None] * len(B.N)
            for i in B.order:
                pw = bw[B.par[i]] if i in B.par else (0, 0, 0, 1)
                if i in rot:
                    si = smap[B.name[i]]
                    w = _qn(_qm(_qm(sw[si], _qi(S.wq0[si])), B.wq0[i]))
                    bl[i] = _qn(_qm(_qi(pw), w)); rot[i].append(bl[i])
                bw[i] = _qm(pw, bl[i])
            st = at((sh, 'translation'), t, 3) if (sh, 'translation') in ch else S.rt[sh]
            sp = [x + y for x, y in zip(_mv(SpL, st), SpT)]
            hipT.append([BhT[c] + (sp[c] - ShT[c]) * k_h for c in range(3)])
        loco = any(w in clip for w in ('walk', 'run', 'sprint', 'strafe', 'Throwstand'))   # the standing throw walks 2.7 m forward
        drift = 0.0
        if loco and len(hipT) > 1:
            n = len(hipT) - 1; d0 = [(hipT[-1][0] - hipT[0][0]) / n, (hipT[-1][2] - hipT[0][2]) / n]; drift = (d0[0] ** 2 + d0[1] ** 2) ** 0.5 * n
            for i, v in enumerate(hipT): v[0] -= d0[0] * i; v[2] -= d0[1] * i
        hipL = [tuple(_mv(BpLi, [v[c] - BpT[c] for c in range(3)])) for v in hipT]
        na = {'name': clip, 'channels': [], 'samplers': []}
        tacc = O.acc(f, cj, cbin, tin, True)
        for i in mapped:
            if at_rest(base['nodes'][i], 'rotation', rot[i]): continue
            na['samplers'].append({'input': tacc, 'output': raw(rot[i], 'VEC4'), 'interpolation': 'LINEAR'}); na['channels'].append({'sampler': len(na['samplers']) - 1, 'target': {'node': i, 'path': 'rotation'}})
        na['samplers'].append({'input': tacc, 'output': raw(hipL, 'VEC3'), 'interpolation': 'LINEAR'}); na['channels'].append({'sampler': len(na['samplers']) - 1, 'target': {'node': bh, 'path': 'translation'}})
        anims.append(na); report.append('%s %.2fs %d joints%s' % (clip, times[-1], len(mapped), ' drift %.2f' % drift if loco else ''))
    j['animations'] = anims
    j['accessors'] = O.accs; j['bufferViews'] = O.views
    while len(O.bin) % 4: O.bin += b'\0'
    j['buffers'] = [{'byteLength': len(O.bin)}]
    js = json.dumps(j, separators=(',', ':')).encode()
    while len(js) % 4: js += b' '
    total = 12 + 8 + len(js) + 8 + len(O.bin)
    with open(out_path, 'wb') as fo:
        fo.write(struct.pack('<III', 0x46546C67, 2, total))
        fo.write(struct.pack('<II', len(js), 0x4E4F534A)); fo.write(js)
        fo.write(struct.pack('<II', len(O.bin), 0x004E4942)); fo.write(O.bin)
    print(os.path.basename(out_path), '%.1f MB' % (total / 1e6), 'hips x %.3f' % k_h); print('\n'.join(report))

if __name__ == '__main__':
    args = sys.argv[1:]; skip = ()
    if args and args[0] == '--retarget': bake_retarget(args[1], args[3:], args[2]); sys.exit(0)
    if '--skip' in args: k = args.index('--skip'); skip = tuple(args[k + 1].split(',')); del args[k:k + 2]   # clips the game does not play
    char, out, srcs = args[0], args[1], args[2:]
    os.makedirs(out, exist_ok=True)
    weapons = sorted({d for s in srcs for d in os.listdir(s) if os.path.isdir(os.path.join(s, d))})
    for w in weapons:
        bake([os.path.join(s, w) for s in srcs if os.path.isdir(os.path.join(s, w))], os.path.join(out, '%s_%s.glb' % (char, short(w))), skip)
