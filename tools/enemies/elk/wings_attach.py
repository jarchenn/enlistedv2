# EK7 (user): a pair of wings on a skinned model's back, at the glTF level: every mesh of the wings GLB (its node
# transforms baked in) is placed by a rigid transform (scale, tilt, turn, root anchor -> a point behind the joint) in the
# model's bind space and skinned weight 1 to that joint. Materials, textures, images and samplers come along; alpha
# BLEND becomes MASK (the game's cut-out path).
#   python -I wings_attach.py <retarget_clips.py> <model.glb> <wings.glb> <out.glb> <joint> <scale> <tilt deg> <back m> <up m>
import json, struct, sys
import numpy as np
RT, MODEL, WINGS, OUT, JOINT = sys.argv[1:6]; SCALE, TILT, BACK, UP = map(float, sys.argv[6:10])
FLUSH = float(sys.argv[10]) if len(sys.argv) > 10 else None   # the bind-space z the wings' front must reach (the back plate's surface): roots touching it
src = open(RT).read(); g = {'__name__': 'rt'}; exec(src[:src.index("if sys.argv[1] == '--check'")], g)
load, Rig, q2m = g['load'], g['Rig'], g['q2m']
M = Rig(*load(MODEL)); WJ, WB = load(WINGS)
J, BIN = M.J, M.BIN
def node_world(JJ):
    par = {c: i for i, n in enumerate(JJ['nodes']) for c in n.get('children', [])}
    def loc(n):
        if 'matrix' in n: return np.array(n['matrix'], float).reshape(4, 4).T
        T = np.eye(4); T[:3, :3] = q2m(np.array(n.get('rotation', [0, 0, 0, 1]), float)) * np.array(n.get('scale', [1, 1, 1])); T[:3, 3] = n.get('translation', [0, 0, 0]); return T
    def w(i): return w(par[i]) @ loc(JJ['nodes'][i]) if i in par else loc(JJ['nodes'][i])
    return w
def acc(JJ, B, ai):
    a = JJ['accessors'][ai]; v = JJ['bufferViews'][a['bufferView']]; k = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}[a['type']]
    dt = {5126: np.float32, 5125: np.uint32, 5123: np.uint16, 5121: np.uint8}[a['componentType']]
    st = v.get('byteStride'); n = a['count']; off = v.get('byteOffset', 0) + a.get('byteOffset', 0)
    if st and st != k * np.dtype(dt).itemsize: return np.array([np.frombuffer(B, dt, k, off + i * st) for i in range(n)])
    return np.frombuffer(B, dt, n * k, off).reshape(n, k)
def add(arr, typ, comp=5126, target=None, minmax=False):
    data = np.ascontiguousarray(arr).tobytes()
    while len(BIN) % 4: BIN.append(0)
    bv = {'buffer': 0, 'byteOffset': len(BIN), 'byteLength': len(data)}
    if target: bv['target'] = target
    J['bufferViews'].append(bv); BIN.extend(data)
    a = {'bufferView': len(J['bufferViews']) - 1, 'componentType': comp, 'count': len(arr), 'type': typ}
    if minmax: a['min'] = [float(x) for x in arr.min(0)]; a['max'] = [float(x) for x in arr.max(0)]
    J['accessors'].append(a); return len(J['accessors']) - 1
# the joint's bind frame and the placement
sk = J['skins'][0]; ji = next(i for i, n in enumerate(J['nodes']) if n.get('name') == JOINT); jk = sk['joints'].index(ji)
IB = acc(J, BIN, sk['inverseBindMatrices']).reshape(-1, 4, 4).transpose(0, 2, 1).astype(float); Bj = np.linalg.inv(IB[jk])
ww = node_world(WJ); prims = []
for i, n in enumerate(WJ['nodes']):
    if 'mesh' in n:
        for p in WJ['meshes'][n['mesh']]['primitives']: prims.append((ww(i), p))
allP = np.concatenate([(np.c_[acc(WJ, WB, p['attributes']['POSITION']), np.ones(WJ['accessors'][p['attributes']['POSITION']]['count'])] @ Mw.T)[:, :3] for Mw, p in prims])
# the roots: the wings' inner, upper edge: centre x 0, the top third's mean height, the nearest depth
anchor = np.array([0.0, np.percentile(allP[:, 1], 80), allP[:, 2].min()])
c, s = np.cos(np.radians(TILT)), np.sin(np.radians(TILT))
Rt = np.array([[1, 0, 0], [0, c, -s], [0, s, c]])   # tilt about x: the tips go up and back
Ry = np.diag([-1.0, 1.0, -1.0])                     # half turn: they sweep behind (the model faces +z)
Rw = Rt @ Ry
target = np.array([Bj[0, 3], Bj[1, 3] + UP, -abs(BACK) + Bj[2, 3]])
print('joint', JOINT, Bj[:3, 3].round(3), 'anchor', anchor.round(3), 'target', target.round(3))
# the wings' materials and their textures
moff = len(J['materials']); toff = len(J.get('textures', [])); ioff = len(J.get('images', [])); soff = len(J.setdefault('samplers', []))
for im in WJ.get('images', []):
    v = WJ['bufferViews'][im['bufferView']]; data = WB[v.get('byteOffset', 0): v.get('byteOffset', 0) + v['byteLength']]
    while len(BIN) % 4: BIN.append(0)
    J['bufferViews'].append({'buffer': 0, 'byteOffset': len(BIN), 'byteLength': len(data)}); BIN.extend(data)
    J.setdefault('images', []).append({'bufferView': len(J['bufferViews']) - 1, 'mimeType': im['mimeType'], 'name': 'wings_%d' % len(J['images'])})
for smp in WJ.get('samplers', []): J['samplers'].append(dict(smp))
for t in WJ.get('textures', []):
    t2 = dict(t); t2['source'] = t['source'] + ioff
    if 'sampler' in t: t2['sampler'] = t['sampler'] + soff
    J.setdefault('textures', []).append(t2)
def retex(d):
    if isinstance(d, dict): return {k: (v + toff if k == 'index' else retex(v)) for k, v in d.items()}
    return d
for m in WJ['materials']:
    m2 = retex(json.loads(json.dumps(m))); m2['name'] = 'wings_' + m.get('name', 'm')
    if m2.get('alphaMode') == 'BLEND': m2['alphaMode'] = 'MASK'; m2['alphaCutoff'] = 0.4
    J['materials'].append(m2)
# the meshes, skinned to the joint
if FLUSH is not None:   # slide the wings in until their front touches the back plate
    front = (((allP - anchor) * SCALE) @ Rw.T + target)[:, 2].max(); target[2] += FLUSH - front; print('flush: front moved %.3f m to z %.3f' % (FLUSH - front, FLUSH))
newp = []
for Mw, p in prims:
    A = p['attributes']; Pn = acc(WJ, WB, A['POSITION']).astype(float); nv = len(Pn)
    P = (np.c_[Pn, np.ones(nv)] @ Mw.T)[:, :3]; P = ((P - anchor) * SCALE) @ Rw.T + target
    R3 = Mw[:3, :3] / np.linalg.norm(Mw[:3, :3], axis=0)
    at = {'POSITION': add(P.astype(np.float32), 'VEC3', target=34962, minmax=True)}
    if 'NORMAL' in A:
        N = acc(WJ, WB, A['NORMAL']).astype(float) @ R3.T @ Rw.T; N /= np.linalg.norm(N, axis=1, keepdims=True) + 1e-12
        at['NORMAL'] = add(N.astype(np.float32), 'VEC3', target=34962)
    if 'TANGENT' in A:
        Tg = acc(WJ, WB, A['TANGENT']).astype(float); Tg[:, :3] = Tg[:, :3] @ R3.T @ Rw.T; Tg[:, 3] *= np.sign(np.linalg.det(Rw))
        at['TANGENT'] = add(Tg.astype(np.float32), 'VEC4', target=34962)
    if 'TEXCOORD_0' in A: at['TEXCOORD_0'] = add(acc(WJ, WB, A['TEXCOORD_0']).astype(np.float32), 'VEC2', target=34962)
    at['JOINTS_0'] = add(np.tile(np.array([jk, 0, 0, 0], np.uint16), (nv, 1)), 'VEC4', 5123, 34962)
    at['WEIGHTS_0'] = add(np.tile(np.array([1, 0, 0, 0], np.float32), (nv, 1)), 'VEC4', target=34962)
    pr = {'attributes': at, 'material': p['material'] + moff, 'mode': p.get('mode', 4)}
    if 'indices' in p:
        I = acc(WJ, WB, p['indices']).reshape(-1).astype(np.uint32)
        if np.linalg.det(Rw) < 0: I = I.reshape(-1, 3)[:, ::-1].reshape(-1)   # a mirror flips the winding
        pr['indices'] = add(I, 'SCALAR', 5125, 34963)
    newp.append(pr)
J['meshes'].append({'name': 'wings', 'primitives': newp})
J['nodes'].append({'name': 'wings', 'mesh': len(J['meshes']) - 1, 'skin': 0})
J['scenes'][J.get('scene', 0)]['nodes'].append(len(J['nodes']) - 1)
while len(BIN) % 4: BIN.append(0)
J['buffers'][0]['byteLength'] = len(BIN); js = json.dumps(J, separators=(',', ':')).encode()
while len(js) % 4: js += b' '
with open(OUT, 'wb') as f:
    f.write(struct.pack('<III', 0x46546C67, 2, 28 + len(js) + len(BIN))); f.write(struct.pack('<II', len(js), 0x4E4F534A)); f.write(js); f.write(struct.pack('<II', len(BIN), 0x004E4942)); f.write(BIN)
print('wrote', OUT, len(newp), 'primitives,', len(allP), 'vertices')
