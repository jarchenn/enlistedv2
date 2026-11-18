# GM1: bake the user's gas-mask soldier export (blender_anim/exports/gas_mask_soldier.zip, extracted outside the repo) into
# one game GLB per weapon: content/characters/gasmask_<weapon>.glb.
#   python -I tools/bake_gasmask.py <extracted zip dir> <out dir>
# The export has one folder per weapon, one GLB per clip, each a full copy of the mesh, rig and textures. Per weapon:
#   - the mesh, rig, materials and textures of Walk.glb, with the clips of every GLB of the folder appended (name = file stem);
#   - the joints renamed to the game's semantic names (character.cpp kSemanticNames), so hit zones and joint heads work;
#   - the ejected casing copies (Weapon_Eject*, a 55 k vertex case mesh each) and the muzzle-flash sprite dropped: the game
#     makes its own casings and flash (the flash at the 'flash' joint, which stays);
#   - the root motion of the locomotion clips removed (_rootJoint translation): the game moves the body, the clips play in place.
# Plain JSON / binary handling (no Blender, nothing executed from the export). Output units as the export: metres, feet at 0,
# facing glTF +z.
import json, struct, sys, os

SEM = {'Bone': 'Hips', 'spine1': 'Spine', 'spine2': 'Spine1', 'spine3': 'Spine2', 'neck': 'Neck', 'head': 'Head',
       'arm1.L': 'LeftShoulder', 'arm2.L': 'LeftArm', 'arm3.L': 'LeftForeArm', 'wrist.L': 'LeftHand',
       'arm1.R': 'RightShoulder', 'arm2.R': 'RightArm', 'arm3.R': 'RightForeArm', 'wrist.R': 'RightHand',
       'leg1.L': 'LeftUpLeg', 'leg2.L': 'LeftLeg', 'leg3.L': 'LeftFoot', 'leg4.L': 'LeftToeBase',
       'leg1.R': 'RightUpLeg', 'leg2.R': 'RightLeg', 'leg3.R': 'RightFoot', 'leg4.R': 'RightToeBase'}
WEAPONS = {'AK47': 'ak47', 'FN_FAL': 'fal', 'M24': 'm24', 'M249': 'm249', 'MP7': 'mp7', 'Pistol_9mm': 'pistol',
           'RPG7': 'rpg7', 'Remington870': 'r870', 'UMP45': 'ump45'}

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
    def acc(self, key, j, bin, ai):
        if (key, 'a', ai) in self.memo: return self.memo[(key, 'a', ai)]
        a = dict(j['accessors'][ai]); assert 'sparse' not in a
        a['bufferView'] = self.view(key, j, bin, a['bufferView'])
        self.accs.append(a); self.memo[(key, 'a', ai)] = len(self.accs) - 1
        return len(self.accs) - 1

def bake(src, out_path):
    base, bbin = load(os.path.join(src, 'Walk.glb'))
    names = [n.get('name', '') for n in base['nodes']]
    O = Out()
    j = {'asset': {'version': '2.0', 'generator': 'NormandyMapmaker tools/bake_gasmask.py'}, 'scene': 0, 'scenes': base['scenes']}
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
            pr['attributes'] = {k: O.acc('b', base, bbin, a) for k, a in pr['attributes'].items() if k != 'TEXCOORD_1'}   # unused
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
    used_t = sorted({m[k]['index'] for m in j['materials'] for m in [m, m.get('pbrMetallicRoughness', {})] for k in ('baseColorTexture', 'metallicRoughnessTexture', 'emissiveTexture') if k in m})
    tmap = {t: k for k, t in enumerate(used_t)}
    for m in j['materials']:
        for d in (m, m.get('pbrMetallicRoughness', {})):
            for k in ('baseColorTexture', 'metallicRoughnessTexture', 'emissiveTexture'):
                if k in d: d[k]['index'] = tmap[d[k]['index']]
    j['textures'] = [dict(base['textures'][t]) for t in used_t]
    used_i = sorted({t['source'] for t in j['textures']}); imap = {i: k for k, i in enumerate(used_i)}
    for t in j['textures']: t['source'] = imap[t['source']]
    j['images'] = []
    for i in used_i:
        im = dict(base['images'][i]); im['bufferView'] = O.view('b', base, bbin, im['bufferView']); j['images'].append(im)
    anims, report = [], []
    for f in sorted(os.listdir(src)):
        if not f.endswith('.glb'): continue
        cj, cbin = load(os.path.join(src, f))
        assert [n.get('name', '') for n in cj['nodes']] == names, f + ': a different node list from Walk.glb'
        for a in cj.get('animations', []):
            na = {'name': f[:-4], 'channels': [], 'samplers': []}
            dur = 0.0
            for ch in a['channels']:
                t = ch['target']; nm = names[t['node']]
                if dropped(nm) or (nm == '_rootJoint' and t['path'] == 'translation'): continue   # in place
                s = a['samplers'][ch['sampler']]
                dur = max(dur, cj['accessors'][s['input']].get('max', [0])[0])
                na['samplers'].append({'input': O.acc(f, cj, cbin, s['input']), 'output': O.acc(f, cj, cbin, s['output']), 'interpolation': s.get('interpolation', 'LINEAR')})
                na['channels'].append({'sampler': len(na['samplers']) - 1, 'target': dict(t)})
            anims.append(na); report.append('%s %.2fs' % (f[:-4], dur))
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

if __name__ == '__main__':
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    for d, w in WEAPONS.items():
        if os.path.isdir(os.path.join(src, d)): bake(os.path.join(src, d), os.path.join(out, 'gasmask_%s.glb' % w))
