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
SHORT = {'FN_FAL': 'fal', 'Pistol_9mm': 'pistol', 'Remington870': 'r870', 'P08_Luger': 'luger', 'CZ_Scorpion_Evo': 'scorpion', 'SCAR_L': 'scar',
         'BAR_M1918A2': 'bar', 'M1Garand': 'garand', 'M3_GreaseGun': 'm3', 'M1911': 'm1911'}
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

import re
# NA1: --names delta: the clips' CamelCase file names as the engine's clip names (gunbody.cpp dRigOf, the Mixamo pack's own):
# "WalkCrouchingForwardLeft" -> "walk crouching forward left", "Turn90Left" -> "turn 90 left"; a pistol set (no WalkForward)
# prefixes its stance / locomotion clips with "pistol " ("pistol walk", "pistol kneeling idle"); a pistol strafe is named by
# the side it travels to ("pistol strafe left / right", from its root drift).
PISTOL_LOCO = ('Idle', 'Walk', 'Run', 'KneelingIdle', 'Jump', 'KneelToStand', 'StandToKneel', 'Strafe')
def camel(n): return ' '.join(w.lower() for w in re.findall(r'[A-Z][a-z]*|[0-9]+|[a-z]+', n))
def delta_name(clip, pistol_set, side=None):
    if pistol_set and clip.startswith(PISTOL_LOCO):
        if clip.startswith('Strafe') and side: return 'pistol strafe ' + side   # Strafe / StrafeB: the two sides
        return 'pistol ' + camel(clip)
    return camel(clip)

def _lin(n):   # node -> its parent-space linear part (3x3) and translation
    R = _q2m(n.get('rotation', [0, 0, 0, 1])); s = n.get('scale', [1, 1, 1])
    return [[R[i][k] * s[k] for k in range(3)] for i in range(3)], list(n.get('translation', [0, 0, 0]))
def _mul(a, b): return _mm(a, b)

def root_drift_x(cj, cbin):   # the clip's _rootJoint travel along the model's x (left +), metres; 0 without one
    names = [n.get('name', '') for n in cj['nodes']]
    if '_rootJoint' not in names: return 0.0
    ri = names.index('_rootJoint'); par = {c: i for i, n in enumerate(cj['nodes']) for c in n.get('children', [])}
    P = [[1, 0, 0], [0, 1, 0], [0, 0, 1]]; k = par.get(ri)
    while k is not None: P = _mul(_lin(cj['nodes'][k])[0], P); k = par.get(k)
    for a in cj.get('animations', [])[:1]:
        for ch in a['channels']:
            if ch['target']['node'] == ri and ch['target']['path'] == 'translation':
                v = floats(cj, cbin, a['samplers'][ch['sampler']]['output'])
                return sum(P[0][c] * (v[-1][c] - v[0][c]) for c in range(3))
    return 0.0

def _ibms(jj, bb):   # skin 0's inverse binds as row-major 4x4 lists, per joint
    A = jj['accessors'][jj['skins'][0]['inverseBindMatrices']]; v = jj['bufferViews'][A['bufferView']]; o = v.get('byteOffset', 0) + A.get('byteOffset', 0)
    out = []
    for i in range(A['count']):
        f = struct.unpack_from('<16f', bb, o + i * 64); out.append([[f[c * 4 + r] for c in range(4)] for r in range(4)])
    return out
def _m4(a, b): return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]
def _inv4(m):   # affine inverse (3x3 linear + translation)
    L = _inv3([row[:3] for row in m[:3]]); t = [m[0][3], m[1][3], m[2][3]]; ti = [-sum(L[i][k] * t[k] for k in range(3)) for i in range(3)]
    return [L[0] + [ti[0]], L[1] + [ti[1]], L[2] + [ti[2]], [0, 0, 0, 1]]

def add_body(j, O, body_path, base=None, bbin_base=None):
    # NA1 --body: the skinned meshes (with their materials and textures) of another bake on the same rig in place of this
    # export's; their joints remapped by name. Returns a report line.
    bj, bbin = load(body_path)
    newj = {strip_num(j['nodes'][k].get('name', '')): i for i, k in enumerate(j['skins'][0]['joints'])}
    bnames = [strip_num(bj['nodes'][k].get('name', '')) for k in bj['skins'][0]['joints']]
    bpar = {c: i for i, n in enumerate(bj['nodes']) for c in n.get('children', [])}
    jmap, missing = [], 0
    for bi, k in enumerate(bj['skins'][0]['joints']):
        q = k
        while q is not None and strip_num(bj['nodes'][q].get('name', '')) not in newj: q = bpar.get(q)
        if q is None: jmap.append(0); missing += 1
        else: jmap.append(newj[strip_num(bj['nodes'][q].get('name', ''))]); missing += q != k
    m0 = len(j['materials'])
    # the body was bound in its own pose: each vertex is posed onto this rig's bind pose by its own weights (linear blend of
    # the new rest x its old inverse bind per joint), so the one skin (this rig's inverse binds) holds both body and gun
    ib_old, ib_new = _ibms(bj, bbin), _ibms(base, bbin_base)
    Mj = [_m4(_inv4(ib_new[jmap[i]]), ib_old[i]) for i in range(len(jmap))]
    used_t = {}
    for m in bj.get('materials', []):
        m = json.loads(json.dumps(m)); m.pop('normalTexture', None); m.pop('occlusionTexture', None)
        for d in (m, m.get('pbrMetallicRoughness', {})):
            for key in ('baseColorTexture', 'metallicRoughnessTexture', 'emissiveTexture'):
                if key in d:
                    t = d[key]['index']
                    if t not in used_t:
                        tx = dict(bj['textures'][t]); im = dict(bj['images'][tx['source']]); v = bj['bufferViews'][im['bufferView']]; o = v.get('byteOffset', 0)
                        while len(O.bin) % 4: O.bin += b'\0'
                        O.views.append({'buffer': 0, 'byteOffset': len(O.bin), 'byteLength': v['byteLength']}); O.bin += bbin[o:o + v['byteLength']]
                        im['bufferView'] = len(O.views) - 1; j['images'].append(im); tx['source'] = len(j['images']) - 1; tx.pop('sampler', None)
                        j['textures'].append(tx); used_t[t] = len(j['textures']) - 1
                    d[key]['index'] = used_t[t]
        j['materials'].append(m)
    nb = 0
    for n in bj['nodes']:
        if 'mesh' not in n or 'skin' not in n: continue
        m = json.loads(json.dumps(bj['meshes'][n['mesh']]))
        for pr in m['primitives']:
            at = {}
            A = pr['attributes']
            def vec(a, nc):
                X = bj['accessors'][a]; v = bj['bufferViews'][X['bufferView']]; o = v.get('byteOffset', 0) + X.get('byteOffset', 0)
                fmt = {5126: 'f', 5121: 'B', 5123: 'H'}[X['componentType']]; st = v.get('byteStride', nc * struct.calcsize(fmt))
                arr = [struct.unpack_from('<%d%s' % (nc, fmt), bbin, o + i * st) for i in range(X['count'])]
                if X.get('normalized'): arr = [tuple(x / (255.0 if fmt == 'B' else 65535.0) for x in q) for q in arr]
                return arr
            Jn, Wt, P = vec(A['JOINTS_0'], 4), vec(A['WEIGHTS_0'], 4), vec(A['POSITION'], 3)
            Nr = vec(A['NORMAL'], 3) if 'NORMAL' in A else None
            Pn, Nn = [], []
            for vi in range(len(P)):
                ws = sum(Wt[vi]) or 1.0; M = [[0.0] * 4 for _ in range(3)]
                for k in range(4):
                    w = Wt[vi][k] / ws
                    if w: Q = Mj[Jn[vi][k]]; M = [[M[r][c] + w * Q[r][c] for c in range(4)] for r in range(3)]
                x, y, z = P[vi]; Pn.append(tuple(M[r][0] * x + M[r][1] * y + M[r][2] * z + M[r][3] for r in range(3)))
                if Nr:
                    a1, b1, c1 = Nr[vi]; nv = [M[r][0] * a1 + M[r][1] * b1 + M[r][2] * c1 for r in range(3)]; l = sum(q * q for q in nv) ** 0.5 or 1.0; Nn.append(tuple(q / l for q in nv))
            def raw(arr):
                data = b''.join(struct.pack('<3f', *q) for q in arr)
                while len(O.bin) % 4: O.bin += b'\0'
                O.views.append({'buffer': 0, 'byteOffset': len(O.bin), 'byteLength': len(data)}); O.bin += data
                O.accs.append({'bufferView': len(O.views) - 1, 'componentType': 5126, 'count': len(arr), 'type': 'VEC3', 'min': [min(q[c] for q in arr) for c in range(3)], 'max': [max(q[c] for q in arr) for c in range(3)]})
                return len(O.accs) - 1
            at['POSITION'] = raw(Pn)
            if Nr: at['NORMAL'] = raw(Nn)
            for key, a in pr['attributes'].items():
                if key in ('POSITION', 'NORMAL'): continue
                if key in ('TEXCOORD_1', 'TEXCOORD_2', 'TANGENT'): continue
                if key.startswith('JOINTS_'):
                    A = bj['accessors'][a]; v = bj['bufferViews'][A['bufferView']]; o = v.get('byteOffset', 0) + A.get('byteOffset', 0)
                    fmt = {5121: 'B', 5123: 'H'}[A['componentType']]; st = v.get('byteStride', 4 * struct.calcsize(fmt))
                    vals = [struct.unpack_from('<4' + fmt, bbin, o + i * st) for i in range(A['count'])]
                    data = b''.join(struct.pack('<4H', *[jmap[x] for x in q]) for q in vals)
                    while len(O.bin) % 4: O.bin += b'\0'
                    O.views.append({'buffer': 0, 'byteOffset': len(O.bin), 'byteLength': len(data)}); O.bin += data
                    O.accs.append({'bufferView': len(O.views) - 1, 'componentType': 5123, 'count': A['count'], 'type': 'VEC4'}); at[key] = len(O.accs) - 1
                else: at[key] = O.acc('body', bj, bbin, a)
            pr['attributes'] = at
            if 'indices' in pr: pr['indices'] = O.acc('body', bj, bbin, pr['indices'])
            if 'material' in pr: pr['material'] += m0
            pr.pop('targets', None)
        j['meshes'].append(m); nn = {'name': n.get('name', 'body'), 'mesh': len(j['meshes']) - 1, 'skin': 0}
        j['nodes'].append(nn); j['scenes'][0]['nodes'].append(len(j['nodes']) - 1); nb += 1
    return '%d body meshes from %s (%d joints mapped to a parent)' % (nb, os.path.basename(body_path), missing)

def bake(srcs, out_path, skip=(), names_mode=None, body=None):
    files = {}   # clip name -> path (a later source dir replaces an earlier one)
    for src in srcs:
        for f in sorted(os.listdir(src)):
            if f.endswith('.glb') and f[:-4] not in skip: files[f[:-4]] = os.path.join(src, f)
    last = srcs[-1]
    base, bbin = load(os.path.join(last, sorted(f for f in os.listdir(last) if f.endswith('.glb'))[0]))
    names = [n.get('name', '') for n in base['nodes']]
    O = Out()
    j = {'asset': {'version': '2.0', 'generator': 'NormandyMapmaker tools/bake_gunbody.py'}, 'scene': 0, 'scenes': json.loads(json.dumps(base['scenes']))}
    nodes = []
    for n in base['nodes']:
        n = dict(n); nm = n.get('name', '')
        if dropped(nm) or (body and 'skin' in n): n.pop('mesh', None); n.pop('skin', None)   # NA1 --body: the export's own body out
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
    bodyRep = add_body(j, O, body, base, bbin) if body else ''
    anims, report = [], []
    pistol_set = 'WalkForward' not in files
    for clip in sorted(files):
        f = files[clip]
        cj, cbin = load(f)
        assert [n.get('name', '') for n in cj['nodes']] == names, f + ': a different node list from the base file'
        side = None
        if names_mode and clip.startswith('Strafe'): dx = root_drift_x(cj, cbin); side = 'left' if dx > 0 else 'right'
        for a in cj.get('animations', []):
            na = {'name': delta_name(clip, pistol_set, side) if names_mode else clip, 'channels': [], 'samplers': []}
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
            anims.append(na); report.append('%s %.2fs' % (na['name'], dur))
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
    print(os.path.basename(out_path), '%.1f MB' % (total / 1e6), bodyRep, ', '.join(report))


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
def _between(a, b):   # the shortest rotation taking direction a onto b (x, y, z, w)
    la = sum(x * x for x in a) ** 0.5; lb = sum(x * x for x in b) ** 0.5
    if la < 1e-9 or lb < 1e-9: return (0, 0, 0, 1)
    a = [x / la for x in a]; b = [x / lb for x in b]
    c = [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]; d = sum(x * y for x, y in zip(a, b))
    if d < -0.9999: return (1, 0, 0, 0) if abs(a[0]) < 0.9 else (0, 1, 0, 0)
    return _qn((c[0], c[1], c[2], 1 + d))
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

# NA1: joint names as the engine's (and Mixamo's), whatever the rig: the part after ':' without a numbered suffix; FigRig's
# (Bone_026, arm2.L_033 ...) through SEM, its fingers (big / pointer / middle / noname / little, the first segment of the
# four-segment fingers being the palm bone) onto Mixamo's LeftHandThumb1 / Index1 ...
FIG_FINGER = {'big': ('Thumb', 0), 'pointer': ('Index', 1), 'middle': ('Middle', 1), 'noname': ('Ring', 1), 'little': ('Pinky', 1)}
def rig_name(raw):
    n = strip_num(raw.split(':')[-1])
    if n in SEM: return SEM[n]
    m = re.match(r'(big|pointer|middle|noname|little)(\d)\.([LR])$', n)
    if m:
        fn, off = FIG_FINGER[m.group(1)]; seg = int(m.group(2)) - off
        if seg >= 1: return ('Left' if m.group(3) == 'L' else 'Right') + 'Hand' + fn + str(seg)
    return n

class _Skel:
    def __init__(self, j):
        self.N = j['nodes']; self.par = {c: i for i, n in enumerate(self.N) for c in n.get('children', [])}
        self.order = []; seen = set()
        def visit(i):
            if i in seen: return
            if i in self.par: visit(self.par[i])
            seen.add(i); self.order.append(i)
        for i in range(len(self.N)): visit(i)
        self.name = [rig_name(n.get('name', '')) for n in self.N]
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

# NA1 --rig <preset>: a body rig that needs help: joint renames onto the engine's names (matched and written so), joints
# carried by another (an IK foot at the root follows its lower leg: its translation from that leg's pose, its turn the clip's),
# and the body's own clips kept (by name)
RIGS = {'robotx': {   # RobotExpressive (Tomas Laulhe / Quaternius, CC0): legs hang off Body above Hips; feet are root-level IK bones
    'rename': {'Bone': 'RigRoot', 'Body': 'Hips', 'Hips': 'Pelvis', 'Abdomen': 'Spine', 'Torso': 'Spine2', 'Shoulder.L': 'LeftShoulder', 'UpperArm.L': 'LeftArm',
               'LowerArm.L': 'LeftForeArm', 'Palm2.L': 'LeftHand', 'Shoulder.R': 'RightShoulder', 'UpperArm.R': 'RightArm', 'LowerArm.R': 'RightForeArm',
               'Palm2.R': 'RightHand', 'UpperLeg.L': 'LeftUpLeg', 'LowerLeg.L': 'LeftLeg', 'Foot.L': 'LeftFoot', 'UpperLeg.R': 'RightUpLeg',
               'LowerLeg.R': 'RightLeg', 'Foot.R': 'RightFoot'},
    'follow': {'LeftFoot': 'LeftLeg', 'RightFoot': 'RightLeg'}, 'keep': ['Death']}}

def bake_retarget(base_path, clip_paths, out_path, names_mode=None, rig=None):
    base, bbin = load(base_path)
    R = RIGS.get(rig) if rig else None
    if R:   # rename the skin's joints (mesh nodes may share a joint's name: left as they are)
        base = json.loads(json.dumps(base)); js0 = set(base['skins'][0]['joints'])
        for k in js0:
            nm = base['nodes'][k].get('name', '')
            if nm in R['rename']: base['nodes'][k]['name'] = R['rename'][nm]
    B = _Skel(base)
    def fk(rq, rt=None):   # world linear (3x3 incl. scale) and position of every body node for local turns rq (rest translations / scales; rt: some overridden)
        L = [None] * len(B.N); P = [None] * len(B.N)
        for i in B.order:
            Rm = _q2m(rq[i]); sc = B.N[i].get('scale', [1, 1, 1]); Ll = [[Rm[a][c] * sc[c] for c in range(3)] for a in range(3)]
            ti = rt[i] if rt and i in rt else B.rt[i]
            if i in B.par: pL, pP = L[B.par[i]], P[B.par[i]]; L[i] = _mm(pL, Ll); P[i] = [x + y for x, y in zip(_mv(pL, ti), pP)]
            else: L[i] = Ll; P[i] = list(ti)
        return L, P
    follow = {}
    if R:
        L0, P0 = fk(B.rq)
        for a, bname in R['follow'].items():
            ia = next((k for k in base['skins'][0]['joints'] if B.name[k] == a), None); ib = next((k for k in base['skins'][0]['joints'] if B.name[k] == bname), None)
            if ia is not None and ib is not None: follow[ia] = (ib, _mv(_inv3(L0[ib]), [x - y for x, y in zip(P0[ia], P0[ib])]))
    O = Out()
    j = {'asset': {'version': '2.0', 'generator': 'NormandyMapmaker tools/bake_gunbody.py --retarget'}, 'scene': 0, 'scenes': base['scenes'], 'nodes': base['nodes']}
    meshes = []
    for m in base['meshes']:
        m = json.loads(json.dumps(m))
        for pr in m['primitives']:
            pr['attributes'] = {k: O.acc('b', base, bbin, a) for k, a in pr['attributes'].items()}
            if 'indices' in pr: pr['indices'] = O.acc('b', base, bbin, pr['indices'])
            pr.pop('targets', None)   # morph targets (a face's expressions): the game draws none
        m.pop('weights', None); m.get('extras', {}).pop('targetNames', None)
        meshes.append(m)
    j['meshes'] = meshes
    j['skins'] = []
    for s in base['skins']:
        s = dict(s); s['inverseBindMatrices'] = O.acc('b', base, bbin, s['inverseBindMatrices']); j['skins'].append(s)
    if R:   # rigid parts on bones, a rig whose inverse binds are not its rest (RobotExpressive: x100): each becomes a skinned part,
        # weight 1 on its bone, its vertices in that bone's bind space (the game skins a rigid part with the bone's inverse bind)
        ib = _ibms(base, bbin); jl = base['skins'][0]['joints']; nodes2 = json.loads(json.dumps(base['nodes']))
        def w4(i): L, P = B.affine(i); return [L[0] + [P[0]], L[1] + [P[1]], L[2] + [P[2]], [0, 0, 0, 1]]
        for ni, n in enumerate(base['nodes']):
            if 'mesh' not in n or 'skin' in n: continue
            q = B.par.get(ni)
            while q is not None and q not in jl: q = B.par.get(q)
            if q is None: continue
            ji = jl.index(q); M = _m4(_m4(_inv4(ib[ji]), _inv4(w4(q))), w4(ni)); m = meshes[n['mesh']]
            for pr in m['primitives']:
                a = pr['attributes']
                def put(arr, typ, ctype=5126):
                    data = b''.join(struct.pack('<%d%s' % (len(arr[0]), 'f' if ctype == 5126 else 'H'), *q2) for q2 in arr)
                    while len(O.bin) % 4: O.bin += b'\0'
                    O.views.append({'buffer': 0, 'byteOffset': len(O.bin), 'byteLength': len(data)}); O.bin += data
                    acc = {'bufferView': len(O.views) - 1, 'componentType': ctype, 'count': len(arr), 'type': typ}
                    if typ == 'VEC3': acc['min'] = [min(v[c] for v in arr) for c in range(3)]; acc['max'] = [max(v[c] for v in arr) for c in range(3)]
                    O.accs.append(acc); return len(O.accs) - 1
                src = base['meshes'][n['mesh']]['primitives'][m['primitives'].index(pr)]['attributes']
                P = [tuple(sum(M[r2][c] * v[c] for c in range(3)) + M[r2][3] for r2 in range(3)) for v in floats(base, bbin, src['POSITION'])]
                a['POSITION'] = put(P, 'VEC3')
                if 'NORMAL' in src:
                    Nn = []
                    for v in floats(base, bbin, src['NORMAL']):
                        q3 = [sum(M[r2][c] * v[c] for c in range(3)) for r2 in range(3)]; l = sum(x * x for x in q3) ** 0.5 or 1.0; Nn.append(tuple(x / l for x in q3))
                    a['NORMAL'] = put(Nn, 'VEC3')
                a['JOINTS_0'] = put([(ji, 0, 0, 0)] * len(P), 'VEC4', 5123); a['WEIGHTS_0'] = put([(1.0, 0.0, 0.0, 0.0)] * len(P), 'VEC4')
            nodes2[ni]['skin'] = 0
        base['nodes'] = nodes2
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
        if names_mode:   # NA1: CamelCase per-gun sets (a pistol set: no WalkForward beside it)
            pset = not os.path.exists(os.path.join(os.path.dirname(f), 'WalkForward.glb'))
            side = None
            if clip.startswith('Strafe'): cj0, cb0 = load(f); side = 'left' if root_drift_x(cj0, cb0) > 0 else 'right'
            clip = delta_name(os.path.basename(f)[:-4], pset, side)
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
        # rest-pose match: a joint's bone (to its first child both rigs have) turned from the body's rest direction onto the clip
        # rig's rest direction (world), so a T-posed body follows an A-posed rig (and the reverse) without its limbs set off
        if not hasattr(B, 'wpos'): B.wpos = [B.affine(i)[1] for i in range(len(B.N))]
        S.wpos = [S.affine(i)[1] for i in range(len(S.N))]
        bkids = {}
        for c, pi in B.par.items(): bkids.setdefault(pi, []).append(c)
        corr = {}
        for i in B.order:
            if i not in joints or B.name[i] not in smap: continue
            kid = next((c for c in sorted(bkids.get(i, [])) if c in joints and B.name[c] in smap), None)
            if kid is None: corr[i] = (0, 0, 0, 1); continue
            dt = [a - b for a, b in zip(B.wpos[kid], B.wpos[i])]; ds = [a - b for a, b in zip(S.wpos[smap[B.name[kid]]], S.wpos[smap[B.name[i]]])]
            corr[i] = _between(dt, ds)
        mapped = [i for i in B.order if i in joints and B.name[i] in smap]
        rot = {i: [] for i in mapped}; hipT = []; folT = {i: [] for i in follow}; bls = []
        for t in times:
            sl = [at((i, 'rotation'), t, 4) if (i, 'rotation') in ch else S.rq[i] for i in range(len(S.N))]
            sw = S.world_rot(sl)
            bl = list(B.rq); bw = [None] * len(B.N)
            for i in B.order:
                pw = bw[B.par[i]] if i in B.par else (0, 0, 0, 1)
                if i in rot:
                    si = smap[B.name[i]]
                    w = _qn(_qm(_qm(_qm(sw[si], _qi(S.wq0[si])), corr.get(i, (0, 0, 0, 1))), B.wq0[i]))
                    bl[i] = _qn(_qm(_qi(pw), w)); rot[i].append(bl[i])
                bw[i] = _qm(pw, bl[i])
            if follow: bls.append(list(bl))
            st = at((sh, 'translation'), t, 3) if (sh, 'translation') in ch else S.rt[sh]
            sp = [x + y for x, y in zip(_mv(SpL, st), SpT)]
            hipT.append([BhT[c] + (sp[c] - ShT[c]) * k_h for c in range(3)])
        loco = any(w in clip for w in ('walk', 'run', 'sprint', 'strafe', 'Throwstand'))   # the standing throw walks 2.7 m forward
        drift = 0.0
        if loco and len(hipT) > 1:
            n = len(hipT) - 1; d0 = [(hipT[-1][0] - hipT[0][0]) / n, (hipT[-1][2] - hipT[0][2]) / n]; drift = (d0[0] ** 2 + d0[1] ** 2) ** 0.5 * n
            for i, v in enumerate(hipT): v[0] -= d0[0] * i; v[2] -= d0[1] * i
        hipL = [tuple(_mv(BpLi, [v[c] - BpT[c] for c in range(3)])) for v in hipT]
        for fi, blf in enumerate(bls):   # the carried joints: where their carrier's pose puts them, the hips' travel included
            Lp, Pp = fk(blf, {bh: list(hipL[fi])})
            for ia, (ib, K) in follow.items():
                w = [x + y for x, y in zip(Pp[ib], _mv(Lp[ib], K))]; pa = B.par.get(ia)
                folT[ia].append(tuple(_mv(_inv3(Lp[pa]), [x - y for x, y in zip(w, Pp[pa])])) if pa is not None else tuple(w))
        na = {'name': clip, 'channels': [], 'samplers': []}
        tacc = O.acc(f, cj, cbin, tin, True)
        for i in mapped:
            if at_rest(base['nodes'][i], 'rotation', rot[i]): continue
            na['samplers'].append({'input': tacc, 'output': raw(rot[i], 'VEC4'), 'interpolation': 'LINEAR'}); na['channels'].append({'sampler': len(na['samplers']) - 1, 'target': {'node': i, 'path': 'rotation'}})
        na['samplers'].append({'input': tacc, 'output': raw(hipL, 'VEC3'), 'interpolation': 'LINEAR'}); na['channels'].append({'sampler': len(na['samplers']) - 1, 'target': {'node': bh, 'path': 'translation'}})
        for ia, v in folT.items():
            na['samplers'].append({'input': tacc, 'output': raw(v, 'VEC3'), 'interpolation': 'LINEAR'}); na['channels'].append({'sampler': len(na['samplers']) - 1, 'target': {'node': ia, 'path': 'translation'}})
        anims.append(na); report.append('%s %.2fs %d joints%s' % (clip, times[-1], len(mapped), ' drift %.2f' % drift if loco else ''))
    for a in base.get('animations', []) if R else []:   # the body's own clips the preset keeps (on its own rig)
        if a.get('name') not in R['keep']: continue
        na = {'name': a['name'], 'channels': [], 'samplers': []}
        for c in a['channels']:
            if c['target'].get('path') not in ('translation', 'rotation', 'scale'): continue   # morph weights: none drawn
            sm = a['samplers'][c['sampler']]
            na['samplers'].append({'input': O.acc('b', base, bbin, sm['input'], True), 'output': O.acc('b', base, bbin, sm['output'], True), 'interpolation': sm.get('interpolation', 'LINEAR')})
            na['channels'].append({'sampler': len(na['samplers']) - 1, 'target': dict(c['target'])})
        anims.append(na); report.append('%s (its own)' % a['name'])
    if R: j['nodes'] = base['nodes']
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
    if args and args[0] == '--retarget':
        nm = rg = None
        if '--names' in args: k = args.index('--names'); nm = args[k + 1]; del args[k:k + 2]
        if '--rig' in args: k = args.index('--rig'); rg = args[k + 1]; del args[k:k + 2]
        bake_retarget(args[1], args[3:], args[2], nm, rg); sys.exit(0)
    if '--skip' in args: k = args.index('--skip'); skip = tuple(args[k + 1].split(',')); del args[k:k + 2]   # clips the game does not play
    names_mode = body = None; only = None
    if '--only' in args: k = args.index('--only'); only = args[k + 1].split(','); del args[k:k + 2]   # NA1: some weapon folders
    if '--names' in args: k = args.index('--names'); names_mode = args[k + 1]; del args[k:k + 2]   # NA1: delta (the engine's clip names)
    if '--body' in args: k = args.index('--body'); body = args[k + 1]; del args[k:k + 2]   # NA1: another bake's body on the same rig
    char, out, srcs = args[0], args[1], args[2:]
    os.makedirs(out, exist_ok=True)
    weapons = sorted({d for s in srcs for d in os.listdir(s) if os.path.isdir(os.path.join(s, d))})
    if only: weapons = [w for w in weapons if w in only]
    for w in weapons:
        bake([os.path.join(s, w) for s in srcs if os.path.isdir(os.path.join(s, w))], os.path.join(out, '%s_%s.glb' % (char, short(w))), skip, names_mode, body)
