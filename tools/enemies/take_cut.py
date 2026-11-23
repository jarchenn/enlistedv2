# FK2: a GLB holding one long baked take -> a game enemy GLB, at the glTF level (the model is not re-exported):
#   - clips cut from the take by key index (spec "clips": [{"name", "f0", "f1", "root"}]); every clip starts over the rest hips;
#     root "drift": the hips' floor travel from the first key to the last taken off linearly (a walk cycle in place, its sway
#     kept); "pin": the hips' floor position held at the first key's (an attack that leaps: the AI moves the body); "keep": as
#     keyed (a death keeps its fall)
#   - spec "props": {node name: decimated .glb from prop_decimate.py}: that node's mesh replaced (materials matched by name)
#   - images larger than spec "max_tex" (2048) scaled down; an image without alpha re-encoded as JPEG
#   - asset.extras.nmkNormals: the game loads the normal maps
# Then run tools/enemies/gltf_prune.py (drops the take's and the dense props' orphaned data) and tools/enemies/clip_loop.py
# on the looping clips.
#   python -I tools/enemies/take_cut.py <in.glb> <spec.json> <out.glb>
import json, struct, sys, io
import numpy as np
from PIL import Image
IN, SPEC, OUT = sys.argv[1:4]; spec = json.load(open(SPEC))
def load(p):
    b = open(p, 'rb').read(); jl = struct.unpack('<I', b[12:16])[0]; bl = struct.unpack('<I', b[20 + jl:24 + jl])[0]
    return json.loads(b[20:20 + jl]), bytearray(b[28 + jl:28 + jl + bl])
J, BIN = load(IN); N = J['nodes']; NUM = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}; CT = {5126: np.float32, 5125: np.uint32, 5123: np.uint16, 5121: np.uint8}
def read(j, b, i):
    a = j['accessors'][i]; v = j['bufferViews'][a['bufferView']]; assert 'byteStride' not in v or v['byteStride'] == NUM[a['type']] * np.dtype(CT[a['componentType']]).itemsize
    return np.frombuffer(bytes(b), CT[a['componentType']], a['count'] * NUM[a['type']], v.get('byteOffset', 0) + a.get('byteOffset', 0)).reshape(a['count'], -1)
def blob(data, target=None):
    while len(BIN) % 4: BIN.append(0)
    v = {'buffer': 0, 'byteOffset': len(BIN), 'byteLength': len(data)}
    if target: v['target'] = target
    J['bufferViews'].append(v); BIN.extend(data); return len(J['bufferViews']) - 1
def add(arr, typ, ct=5126, mm=False, target=None):
    arr = np.ascontiguousarray(arr, CT[ct]); a = {'bufferView': blob(arr.tobytes(), target), 'componentType': ct, 'count': len(arr), 'type': typ}
    if mm: a['min'] = [float(x) for x in arr.reshape(len(arr), -1).min(0)]; a['max'] = [float(x) for x in arr.reshape(len(arr), -1).max(0)]
    J['accessors'].append(a); return len(J['accessors']) - 1
# ---- clips
par = {c: i for i, n in enumerate(N) for c in n.get('children', [])}
def local(i):
    n = N[i]; M = np.eye(4)
    if 'matrix' in n: return np.array(n['matrix'], float).reshape(4, 4).T
    x, y, z, w = n.get('rotation', [0, 0, 0, 1]); R = np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)], [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)], [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
    M[:3, :3] = R * np.array(n.get('scale', [1, 1, 1])); M[:3, 3] = n.get('translation', [0, 0, 0]); return M
def world(i): return world(par[i]) @ local(i) if i in par else local(i)
hips = next(i for i, n in enumerate(N) if n.get('name', '').endswith(':Hips')); PA = world(par[hips]); PAi = np.linalg.inv(PA)
rest_w = (PA @ np.r_[N[hips]['translation'], 1])[:3]
take = J['animations'][0]; anims = []
TF = max((read(J, BIN, s['input'])[:, 0].astype(np.float64) for s in take['samplers']), key=len)   # the take's full key grid (a constant channel is exported with two keys)
for c in spec['clips']:
    f0, f1 = c['f0'], c['f1']; a = {'name': c['name'], 'samplers': [], 'channels': []}; tin = {}
    for ch in take['channels']:
        s = take['samplers'][ch['sampler']]; t = read(J, BIN, s['input'])[:, 0].astype(np.float64); y = read(J, BIN, s['output']).astype(np.float64)
        if len(t) != len(TF): y = np.stack([np.interp(TF, t, y[:, k]) for k in range(y.shape[1])], 1); t = TF
        y = y[f0:f1 + 1].copy()
        if ch['target']['node'] == hips and ch['target']['path'] == 'translation':
            w = (PA @ np.c_[y, np.ones(len(y))].T).T[:, :3]; u = np.linspace(0, 1, len(w))[:, None]; fl = [0, 2]   # world floor axes (y up)
            if c['root'] == 'drift': w[:, fl] -= u * (w[-1, fl] - w[0, fl])
            elif c['root'] == 'pin': w[:, fl] = w[0, fl]
            w[:, fl] += rest_w[fl] - w[0, fl]
            y = (PAi @ np.c_[w, np.ones(len(w))].T).T[:, :3]
        key = (f0, f1)
        if key not in tin: tt = t[f0:f1 + 1] - t[f0]; tin[key] = add(tt[:, None], 'SCALAR', mm=True)
        a['samplers'].append({'input': tin[key], 'output': add(y, 'VEC4' if y.shape[1] == 4 else 'VEC3'), 'interpolation': s.get('interpolation', 'LINEAR')})
        a['channels'].append({'sampler': len(a['samplers']) - 1, 'target': dict(ch['target'])})
    anims.append(a); print('CLIP %-14s keys %3d..%3d  %.2f s  root %s' % (c['name'], f0, f1, (f1 - f0) / 30.0, c['root']))
J['animations'] = anims
# ---- props
mname = {m.get('name'): i for i, m in enumerate(J['materials'])}
for node, path in spec.get('props', {}).items():
    PJ, PB = load(path); n = next(x for x in N if x.get('name') == node); old = J['meshes'][n['mesh']]; pm = PJ['meshes'][0]; prims = []
    for k, pr in enumerate(pm['primitives']):
        mat = PJ['materials'][pr['material']].get('name') if 'material' in pr else None
        o = {'attributes': {}, 'material': mname.get(mat, old['primitives'][min(k, len(old['primitives']) - 1)].get('material'))}
        for at, ai in pr['attributes'].items():
            if at in ('POSITION', 'NORMAL', 'TEXCOORD_0', 'TANGENT'): o['attributes'][at] = add(read(PJ, PB, ai), PJ['accessors'][ai]['type'], mm=at == 'POSITION', target=34962)
        idx = read(PJ, PB, pr['indices']).astype(np.uint32); o['indices'] = add(idx, 'SCALAR', ct=5125, target=34963); prims.append(o)
    tri0 = sum(J['accessors'][p['indices']]['count'] for p in old['primitives']) // 3; old['primitives'] = prims
    print('PROP %-10s %d -> %d triangles' % (node, tri0, sum(J['accessors'][p['indices']]['count'] for p in prims) // 3))
# ---- images
MAXT = spec.get('max_tex', 2048)
for im in J.get('images', []):
    v = J['bufferViews'][im['bufferView']]; raw = bytes(BIN[v.get('byteOffset', 0):v.get('byteOffset', 0) + v['byteLength']]); I = Image.open(io.BytesIO(raw)); I.load()
    alpha = I.mode in ('RGBA', 'LA', 'P') and np.asarray(I.convert('RGBA'))[:, :, 3].min() < 250
    if max(I.size) <= MAXT and (im.get('mimeType') == 'image/jpeg' or alpha): continue
    if max(I.size) > MAXT: k = MAXT / max(I.size); I = I.resize((max(1, round(I.size[0] * k)), max(1, round(I.size[1] * k))), Image.LANCZOS)
    o = io.BytesIO()
    if alpha: I.save(o, 'PNG', optimize=True); mt = 'image/png'
    else: I.convert('RGB').save(o, 'JPEG', quality=90); mt = 'image/jpeg'
    print('IMAGE %-48s %5d kB -> %5d kB %s %s' % (im.get('name'), len(raw) // 1024, len(o.getvalue()) // 1024, I.size, mt)); im['bufferView'] = blob(o.getvalue()); im['mimeType'] = mt
J.setdefault('asset', {}).setdefault('extras', {})['nmkNormals'] = True
while len(BIN) % 4: BIN.append(0)
J['buffers'][0]['byteLength'] = len(BIN); js = json.dumps(J, separators=(',', ':')).encode(); js += b' ' * ((4 - len(js) % 4) % 4)
open(OUT, 'wb').write(struct.pack('<III', 0x46546C67, 2, 28 + len(js) + len(BIN)) + struct.pack('<II', len(js), 0x4E4F534A) + js + struct.pack('<II', len(BIN), 0x004E4942) + BIN)
print('wrote', OUT, '(prune next)')
