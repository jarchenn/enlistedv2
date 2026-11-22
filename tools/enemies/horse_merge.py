# HK1: the mounted Knight 10 as one game model: the materials session's pair (horse_armoured.glb + knight10_rider.glb, one shared
# origin, the same clip names; E:/nmk/horse/export) merged at the glTF level, because the game draws one skinned model per
# soldier (character.cpp reads skins[0] only):
# - the rider's nodes, meshes, materials, textures, images, accessors appended to the horse's (indices shifted)
# - one skin: the horse's joints, then the rider's (inverse binds concatenated); the rider's JOINTS_0 shifted by the horse's count
# - each clip name: the horse's channels and the rider's (retargeted to the shifted nodes) in one animation
# - Blender's default cube (a 2 m "Cube" mesh at the origin, left in both exports) is dropped
#   python -I tools/enemies/horse_merge.py <horse.glb> <rider.glb> <out.glb> [<credits>]   (then tools/enemies/gltf_prune.py)
import json, struct, sys
import numpy as np
H, Rp, OUT = sys.argv[1:4]; CREDITS = sys.argv[4] if len(sys.argv) > 4 else None
def load(p):
    b = open(p, 'rb').read(); assert b[:4] == b'glTF'
    jl = struct.unpack('<I', b[12:16])[0]; bl = struct.unpack('<I', b[20 + jl:24 + jl])[0]
    return json.loads(b[20:20 + jl]), bytearray(b[28 + jl:28 + jl + bl])
J, BIN = load(H); R, RB = load(Rp)
off = {k: len(J.get(k, [])) for k in ('nodes', 'meshes', 'accessors', 'bufferViews', 'materials', 'textures', 'images', 'samplers')}
nj_h = len(J['skins'][0]['joints'])
CT = {5121: np.uint8, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}; NUM = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}
def add_view(data, target=None):
    while len(BIN) % 4: BIN.append(0)
    v = {'buffer': 0, 'byteOffset': len(BIN), 'byteLength': len(data)}
    if target: v['target'] = target
    BIN.extend(data); J['bufferViews'].append(v); return len(J['bufferViews']) - 1
# ---- the rider's data
for v in R['bufferViews']:
    o = v.get('byteOffset', 0); i = add_view(bytes(RB[o:o + v['byteLength']]), v.get('target'))
    if 'byteStride' in v: J['bufferViews'][i]['byteStride'] = v['byteStride']
for a in R['accessors']: a2 = dict(a); a2['bufferView'] += off['bufferViews']; J['accessors'].append(a2)
for im in R.get('images', []): im2 = dict(im); im2['bufferView'] += off['bufferViews']; J.setdefault('images', []).append(im2)
J.setdefault('samplers', []).extend(R.get('samplers', []))
for t in R.get('textures', []):
    t2 = dict(t); t2['source'] += off['images']
    if 'sampler' in t2: t2['sampler'] += off['samplers']
    J.setdefault('textures', []).append(t2)
def shift_tex(d):
    for k, v in d.items():
        if isinstance(v, dict):
            if k.endswith('Texture') and 'index' in v: v['index'] += off['textures']
            else: shift_tex(v)
for m in R['materials']: m2 = json.loads(json.dumps(m)); shift_tex(m2); J['materials'].append(m2)
def read(G, B, i):
    a = G['accessors'][i]; v = G['bufferViews'][a['bufferView']]; n = NUM[a['type']]; dt = np.dtype(CT[a['componentType']])
    return np.frombuffer(bytes(B), dt, count=a['count'] * n, offset=v.get('byteOffset', 0) + a.get('byteOffset', 0)).reshape(-1, n)
for me in R['meshes']:
    me2 = json.loads(json.dumps(me))
    for pr in me2['primitives']:
        at = pr['attributes']
        if 'JOINTS_0' in at:   # the rider's joints follow the horse's in the one skin
            jo = read(R, RB, at['JOINTS_0']).astype(np.uint32) + nj_h; ct = 5121 if jo.max() < 256 else 5123
            vi = add_view(np.ascontiguousarray(jo, CT[ct]).tobytes(), 34962); J['accessors'].append({'bufferView': vi, 'componentType': ct, 'count': len(jo), 'type': 'VEC4'})
            at['JOINTS_0'] = len(J['accessors']) - 1 - off['accessors']   # shifted back below with the others
        pr['attributes'] = {k: v + off['accessors'] for k, v in at.items()}
        if 'indices' in pr: pr['indices'] += off['accessors']
        if 'material' in pr: pr['material'] += off['materials']
    J['meshes'].append(me2)
for n in R['nodes']:
    n2 = dict(n)
    if 'children' in n2: n2['children'] = [c + off['nodes'] for c in n2['children']]
    if 'mesh' in n2: n2['mesh'] += off['meshes']
    if 'skin' in n2: n2['skin'] = 0
    J['nodes'].append(n2)
for n in J['nodes'][:off['nodes']]:
    if 'skin' in n: n['skin'] = 0
J['scenes'][0]['nodes'] += [r + off['nodes'] for r in R['scenes'][R.get('scene', 0)]['nodes']]
# ---- one skin
sh, sr = J['skins'][0], R['skins'][0]
ib = np.vstack([read(J, BIN, sh['inverseBindMatrices']), read(R, RB, sr['inverseBindMatrices'])]).astype(np.float32)
sh['joints'] = sh['joints'] + [j + off['nodes'] for j in sr['joints']]
sh['inverseBindMatrices'] = len(J['accessors']); J['accessors'].append({'bufferView': add_view(ib.tobytes()), 'componentType': 5126, 'count': len(ib), 'type': 'MAT4'})
J['skins'] = [sh]
# ---- the clips, by name
byname = {a['name']: a for a in J['animations']}
for a in R['animations']:
    assert a['name'] in byname, 'the rider has a clip the horse lacks: ' + a['name']
    t = byname[a['name']]; s0 = len(t['samplers'])
    for s in a['samplers']: t['samplers'].append({'input': s['input'] + off['accessors'], 'output': s['output'] + off['accessors'], 'interpolation': s.get('interpolation', 'LINEAR')})
    for c in a['channels']: t['channels'].append({'sampler': c['sampler'] + s0, 'target': {'node': c['target']['node'] + off['nodes'], 'path': c['target']['path']}})
# ---- Blender's default cube out
cubes = [n for n in J['nodes'] if n.get('name') == 'Cube' and 'mesh' in n and 'skin' not in n]
for n in cubes: n.pop('mesh')
J['buffers'] = [{'byteLength': len(BIN)}]
if CREDITS: J['asset'].setdefault('extras', {})['credits'] = CREDITS
js = json.dumps(J, separators=(',', ':')).encode(); js += b' ' * (-len(js) % 4)
while len(BIN) % 4: BIN.append(0)
open(OUT, 'wb').write(b'glTF' + struct.pack('<II', 2, 28 + len(js) + len(BIN)) + struct.pack('<I', len(js)) + b'JSON' + js + struct.pack('<I', len(BIN)) + b'BIN\0' + bytes(BIN))
print('merged: %d + %d joints, %d nodes, %d meshes, clips %s; dropped %d default cubes -> %s' % (nj_h, len(sr['joints']), len(J['nodes']), len(J['meshes']), [a['name'] for a in J['animations']], len(cubes), OUT))
