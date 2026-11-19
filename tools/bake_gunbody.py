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


# DF1: one body, clips from separate files matched by node NAME (Mixamo exports: the clip files carry the skeleton only).
#   python -I tools/bake_gunbody.py --named <character.glb> <out.glb> <clip.glb>...   (clip name = file stem, '_' -> ' ')
#   The Hips translation of the locomotion clips (walk / run / sprint / strafe in the name) loses its horizontal drift (the
#   straight line from the first key to the last, in model space): the clip plays in place, the sway stays.
def _q2m(q):
    x, y, z, w = q
    return [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)], [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)], [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
def _lin(n):   # node -> its parent-space linear part (3x3) and translation
    r = _q2m(n.get('rotation', [0, 0, 0, 1])); s = n.get('scale', [1, 1, 1])
    return [[r[i][k] * s[k] for k in range(3)] for i in range(3)], list(n.get('translation', [0, 0, 0]))
def _mul(a, b): return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
def _inv(m):
    a, b, c = m[0]; d, e, f = m[1]; g, h, i = m[2]
    det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g)
    return [[(e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det], [(f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det], [(d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det]]

def bake_named(base_path, clip_paths, out_path):
    base, bbin = load(base_path)
    names = [n.get('name', '') for n in base['nodes']]
    parent = {c: i for i, n in enumerate(base['nodes']) for c in n.get('children', [])}
    tmp = os.path.join(os.path.dirname(out_path), '_named_tmp'); os.makedirs(tmp, exist_ok=True)
    O = Out()
    j = {'asset': {'version': '2.0', 'generator': 'NormandyMapmaker tools/bake_gunbody.py --named'}, 'scene': 0, 'scenes': base['scenes'], 'nodes': base['nodes']}
    meshes = []
    for m in base['meshes']:
        m = json.loads(json.dumps(m))
        for pr in m['primitives']:
            pr['attributes'] = {k: O.acc('b', base, bbin, a) for k, a in pr['attributes'].items() if k not in ('TEXCOORD_1', 'TEXCOORD_2', 'TANGENT')}
            if 'indices' in pr: pr['indices'] = O.acc('b', base, bbin, pr['indices'])
            pr.pop('targets', None)
        meshes.append(m)
    j['meshes'] = meshes
    j['skins'] = []
    for s in base['skins']:
        s = dict(s); s['inverseBindMatrices'] = O.acc('b', base, bbin, s['inverseBindMatrices']); j['skins'].append(s)
    j['materials'] = json.loads(json.dumps(base.get('materials', [])))
    for m in j['materials']: m.pop('normalTexture', None); m.pop('occlusionTexture', None); m.pop('alphaMode', None); m.pop('alphaCutoff', None)
    used_t = sorted({d[k]['index'] for m in j['materials'] for d in (m, m.get('pbrMetallicRoughness', {})) for k in ('baseColorTexture', 'metallicRoughnessTexture', 'emissiveTexture') if k in d})
    tmap = {t: k for k, t in enumerate(used_t)}
    for m in j['materials']:
        for d in (m, m.get('pbrMetallicRoughness', {})):
            for k in ('baseColorTexture', 'metallicRoughnessTexture', 'emissiveTexture'):
                if k in d: d[k]['index'] = tmap[d[k]['index']]
    j['samplers'] = base.get('samplers', [])
    j['textures'] = [dict(base['textures'][t]) for t in used_t]
    used_i = sorted({t['source'] for t in j['textures']}); imap = {i: k for k, i in enumerate(used_i)}
    for t in j['textures']: t['source'] = imap[t['source']]
    j['images'] = []
    for i in used_i:
        im = dict(base['images'][i]); v = base['bufferViews'][im['bufferView']]; o = v.get('byteOffset', 0)
        data, im['mimeType'] = jpeg_if_opaque(bytes(bbin[o:o + v['byteLength']]), im.get('mimeType', ''), False)
        while len(O.bin) % 4: O.bin += b'\0'
        O.views.append({'buffer': 0, 'byteOffset': len(O.bin), 'byteLength': len(data)}); O.bin += data
        im['bufferView'] = len(O.views) - 1; j['images'].append(im)
    hips = next(i for i, n in enumerate(names) if n.split(':')[-1] == 'Hips')
    P = [[1, 0, 0], [0, 1, 0], [0, 0, 1]]; k = parent.get(hips)
    while k is not None: P = _mul(_lin(base['nodes'][k])[0], P); k = parent.get(k)
    Pi = _inv(P)
    anims, report = [], []
    for f in clip_paths:
        clip = os.path.basename(f)[:-4].replace('_', ' ')
        cj, cbin = load(f); cn = [n.get('name', '') for n in cj['nodes']]
        loco = any(w in clip for w in ('walk', 'run', 'sprint', 'strafe'))
        for a in cj.get('animations', [])[:1]:
            na = {'name': clip, 'channels': [], 'samplers': []}; dur = 0.0; drift = 0.0
            for ch in a['channels']:
                t = ch['target']; nm = cn[t['node']]
                if nm not in names or t['path'] == 'scale': continue
                bi = names.index(nm); s = a['samplers'][ch['sampler']]
                dur = max(dur, cj['accessors'][s['input']].get('max', [0])[0])
                vals = floats(cj, cbin, s['output'])
                if at_rest(base['nodes'][bi], t['path'], vals): continue
                outa = O.acc(f, cj, cbin, s['output'], True)
                if bi == hips and t['path'] == 'translation' and loco:
                    w = [[sum(P[r][c] * v[c] for c in range(3)) for r in range(3)] for v in vals]
                    n = len(w) - 1 or 1; d0 = [(w[-1][0] - w[0][0]) / n, (w[-1][2] - w[0][2]) / n]; drift = (d0[0] ** 2 + d0[1] ** 2) ** 0.5 * n
                    for i, v in enumerate(w): v[0] -= d0[0] * i; v[2] -= d0[1] * i
                    back = [tuple(sum(Pi[r][c] * v[c] for c in range(3)) for r in range(3)) for v in w]
                    data = b''.join(struct.pack('<3f', *v) for v in back)
                    while len(O.bin) % 4: O.bin += b'\0'
                    O.views.append({'buffer': 0, 'byteOffset': len(O.bin), 'byteLength': len(data)}); O.bin += data
                    acc = dict(cj['accessors'][s['output']]); acc['bufferView'] = len(O.views) - 1; acc.pop('byteOffset', None)
                    acc['min'] = [min(v[c] for v in back) for c in range(3)]; acc['max'] = [max(v[c] for v in back) for c in range(3)]
                    O.accs.append(acc); outa = len(O.accs) - 1
                na['samplers'].append({'input': O.acc(f, cj, cbin, s['input'], True), 'output': outa, 'interpolation': s.get('interpolation', 'LINEAR')})
                na['channels'].append({'sampler': len(na['samplers']) - 1, 'target': {'node': bi, 'path': t['path']}})
            anims.append(na); report.append('%s %.2fs%s' % (clip, dur, ' drift %.2f' % drift if loco else ''))
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
    print(os.path.basename(out_path), '%.1f MB' % (total / 1e6)); print('\n'.join(report))

if __name__ == '__main__':
    args = sys.argv[1:]; skip = ()
    if args and args[0] == '--named': bake_named(args[1], args[3:], args[2]); sys.exit(0)
    if '--skip' in args: k = args.index('--skip'); skip = tuple(args[k + 1].split(',')); del args[k:k + 2]   # clips the game does not play
    char, out, srcs = args[0], args[1], args[2:]
    os.makedirs(out, exist_ok=True)
    weapons = sorted({d for s in srcs for d in os.listdir(s) if os.path.isdir(os.path.join(s, d))})
    for w in weapons:
        bake([os.path.join(s, w) for s in srcs if os.path.isdir(os.path.join(s, w))], os.path.join(out, '%s_%s.glb' % (char, short(w))), skip)
