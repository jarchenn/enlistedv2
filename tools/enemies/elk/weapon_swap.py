# EK8 (user): Knight 10 rearmed at the glTF level: the spear's primitive (by material) replaced by another weapon's meshes
# laid along the spear's own line (butt on the butt, tip on the tip, scaled to the same length; the weapon's blade side
# turned toward world up), skinned to the joint the spear was on; and a primitive's material swapped (the shield in the
# knight's own plate metal). The weapon GLB's node transforms are baked; its long axis is the one its vertices run along.
#   python -I weapon_swap.py <rider.glb> <weapon.glb> <out.glb> <spear material> <shield material> <new shield material>
import json, struct, sys
import numpy as np
RIDER, WEAPON, OUT, SPEAR, SHIELD, SHIELD_TO = sys.argv[1:7]
def load(p):
    b = open(p, 'rb').read(); jl = struct.unpack('<I', b[12:16])[0]; bl = struct.unpack('<I', b[20 + jl:24 + jl])[0]
    return json.loads(b[20:20 + jl]), bytearray(b[28 + jl:28 + jl + bl])
J, BIN = load(RIDER); WJ, WB = load(WEAPON)
def acc(JJ, B, ai):
    a = JJ['accessors'][ai]; v = JJ['bufferViews'][a['bufferView']]; k = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}[a['type']]
    dt = {5126: np.float32, 5125: np.uint32, 5123: np.uint16, 5121: np.uint8}[a['componentType']]
    st = v.get('byteStride'); n = a['count']; off = v.get('byteOffset', 0) + a.get('byteOffset', 0); isz = np.dtype(dt).itemsize
    if st and st != k * isz: return np.array([np.frombuffer(B, dt, k, off + i * st) for i in range(n)])
    return np.frombuffer(B, dt, n * k, off).reshape(n, k).copy()   # a copy: the buffer grows below
def add(arr, typ, comp=5126, target=None, minmax=False):
    data = np.ascontiguousarray(arr).tobytes()
    while len(BIN) % 4: BIN.append(0)
    bv = {'buffer': 0, 'byteOffset': len(BIN), 'byteLength': len(data)}
    if target: bv['target'] = target
    J['bufferViews'].append(bv); BIN.extend(data)
    a = {'bufferView': len(J['bufferViews']) - 1, 'componentType': comp, 'count': len(arr), 'type': typ}
    if minmax: a['min'] = [float(x) for x in arr.min(0)]; a['max'] = [float(x) for x in arr.max(0)]
    J['accessors'].append(a); return len(J['accessors']) - 1
def q2m(q):
    x, y, z, w = q; return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)], [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)], [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
def node_world(JJ):
    par = {c: i for i, n in enumerate(JJ['nodes']) for c in n.get('children', [])}
    def loc(n):
        if 'matrix' in n: return np.array(n['matrix'], float).reshape(4, 4).T
        T = np.eye(4); T[:3, :3] = q2m(n.get('rotation', [0, 0, 0, 1])) * np.array(n.get('scale', [1, 1, 1])); T[:3, 3] = n.get('translation', [0, 0, 0]); return T
    def w(i): return w(par[i]) @ loc(JJ['nodes'][i]) if i in par else loc(JJ['nodes'][i])
    return w
mats = [m.get('name') for m in J['materials']]
# the spear: its primitive, its line, its joint
sp = next((n, p) for n in J['nodes'] if 'mesh' in n for p in J['meshes'][n['mesh']]['primitives'] if mats[p['material']] == SPEAR)
spnode, sprim = sp; Psp = acc(J, BIN, sprim['attributes']['POSITION']).astype(float)
import os
if os.environ.get('LINE_GLB'):   # EK9: the weapon's line taken from another model on the same rig (the Spear Knight's spear), not from the dropped one
    LJ, LB = load(os.environ['LINE_GLB']); lm = [m.get('name') for m in LJ['materials']]
    lp = next(p for n in LJ['nodes'] if 'mesh' in n for p in LJ['meshes'][n['mesh']]['primitives'] if lm[p['material']] == os.environ['LINE_MAT'])
    Psp = acc(LJ, LB, lp['attributes']['POSITION']).astype(float)
c0 = Psp.mean(0); U, S, Vt = np.linalg.svd(Psp - c0, full_matrices=False); d = Vt[0]
if d[2] < 0: d = -d   # the tip: forward (+z, the way he faces)
t = (Psp - c0) @ d; butt, tip = c0 + d * t.min(), c0 + d * t.max(); L = t.max() - t.min()
par = {c: i for i, n in enumerate(J['nodes']) for c in n.get('children', [])}
RIGID = 'JOINTS_0' not in sprim['attributes']   # a prop on a bone node (its vertices in the same bind space), not skinned
if RIGID: joint = J['skins'][0]['joints'].index(par[J['nodes'].index(spnode)])
else:
    JI = acc(J, BIN, sprim['attributes']['JOINTS_0']); WI = acc(J, BIN, sprim['attributes']['WEIGHTS_0']).astype(float)
    joint = int(np.bincount(JI[np.arange(len(JI)), WI.argmax(1)].astype(int)).argmax())
print('spear %s: butt %s tip %s length %.2f m, joint %d (%s)' % (SPEAR, butt.round(3), tip.round(3), L, joint, J['nodes'][J['skins'][0]['joints'][joint]].get('name')))
IBM = acc(J, BIN, J['skins'][0]['inverseBindMatrices']).reshape(-1, 4, 4).transpose(0, 2, 1).astype(float)
if os.environ.get('LENGTH'):   # EK9: a shorter weapon, scaled about the grip (the point of the line nearest the hand), so the hand keeps its hold
    hand = np.linalg.inv(IBM[joint])[:3, 3]; g_ = butt + d * np.clip((hand - butt) @ d, 0, L); k = float(os.environ['LENGTH']) / L
    butt = g_ + (butt - g_) * k; tip = g_ + (tip - g_) * k; L = L * k; print('  grip %s, now %.2f m: butt %s tip %s' % (g_.round(3), L, butt.round(3), tip.round(3)))
if os.environ.get('SHIFT'):   # EK10 (user): his hand further down the shaft: the weapon slid forward along its line by SHIFT m
    sh = float(os.environ['SHIFT']); butt = butt + d * sh; tip = tip + d * sh; print('  slid %.2f m forward: butt %s tip %s' % (sh, butt.round(3), tip.round(3)))
# the weapon: baked vertices, its long axis, its blade side
ww = node_world(WJ); prims = [(ww(i), p) for i, n in enumerate(WJ['nodes']) if 'mesh' in n for p in WJ['meshes'][n['mesh']]['primitives']]
allW = np.concatenate([(np.c_[acc(WJ, WB, p['attributes']['POSITION']), np.ones(WJ['accessors'][p['attributes']['POSITION']]['count'])] @ Mw.T)[:, :3] for Mw, p in prims])
cw = allW.mean(0); _, _, Vw = np.linalg.svd(allW - cw, full_matrices=False); ah = Vw[0]
tw = (allW - cw) @ ah
rad = np.linalg.norm((allW - cw) - np.outer(tw, ah), axis=1); span = tw.max() - tw.min()
if rad[tw > tw.max() - 0.25 * span].max() < rad[tw < tw.min() + 0.25 * span].max(): ah = -ah; tw = -tw   # the head: the end with the widest part (the blade)
wbutt = cw + ah * tw.min(); Lw = tw.max() - tw.min()
side = allW - (cw + np.outer((allW - cw) @ ah, ah)); bh = side[np.argmax(np.linalg.norm(side, axis=1))]; bh = bh / np.linalg.norm(bh)   # the blade: the farthest point off the axis
ch = np.cross(ah, bh); bh = np.cross(ch, ah)
up = np.array([0.0, 1.0, 0.0]); bt = up - d * (up @ d); bt /= np.linalg.norm(bt); ct = np.cross(d, bt)
if os.environ.get('ROLL'):   # EK11 (user): the hold turned about the shaft: ROLL degrees counter-clockwise seen from his hand looking down the shaft to the head
    r_ = -np.radians(float(os.environ['ROLL'])); bt, ct = bt * np.cos(r_) + ct * np.sin(r_), -bt * np.sin(r_) + ct * np.cos(r_)
Rw = np.c_[d, bt, ct] @ np.c_[ah, bh, ch].T; sc = L / Lw
print('weapon length %.2f (model units), scaled x%.4f to %.2f m' % (Lw, sc, L))
# drop the spear primitive (a rigid prop: the weapon goes on a node of its own beside it, same bone, same matrix)
m = J['meshes'][spnode['mesh']]; m['primitives'] = [p for p in m['primitives'] if p is not sprim]
if RIGID:
    newmesh = {'name': 'weapon', 'primitives': []}; J['meshes'].append(newmesh)
    nd = {'name': 'KNB_RightHand_weapon', 'mesh': len(J['meshes']) - 1}
    for k in ('matrix', 'translation', 'rotation', 'scale'):
        if k in spnode: nd[k] = spnode[k]
    J['nodes'].append(nd); J['nodes'][par[J['nodes'].index(spnode)]].setdefault('children', []).append(len(J['nodes']) - 1)
    if not m['primitives']: del spnode['mesh']
# the shield in the plate metal
si = mats.index(SHIELD_TO)
for n in J['nodes']:
    if 'mesh' in n:
        for p in J['meshes'][n['mesh']]['primitives']:
            if mats[p['material']] != SHIELD: continue
            p['material'] = si; print('shield ->', SHIELD_TO)
            if os.environ.get('FLIP_SHIELD'):   # EK9 (user): the other face toward the knight: a half turn about the shield's long axis, its inner face kept at the arm
                A_ = p['attributes']; P = acc(J, BIN, A_['POSITION']).astype(float); c = P.mean(0); _, _, V = np.linalg.svd(P - c, full_matrices=False)
                a_ = V[0]; nn = V[2]
                if 'JOINTS_0' in A_: jl = acc(J, BIN, A_['JOINTS_0'])[:, 0]; arm = np.linalg.inv(IBM[int(np.bincount(jl.astype(int)).argmax())])[:3, 3]
                else: arm = np.linalg.inv(IBM[J['skins'][0]['joints'].index(par[J['nodes'].index(n)])])[:3, 3]
                if (c - arm) @ nn < 0: nn = -nn   # outward
                Rf = 2 * np.outer(a_, a_) - np.eye(3); Q = (P - c) @ Rf.T + c; Q += nn * (((P - c) @ nn).min() - ((Q - c) @ nn).min())
                A_['POSITION'] = add(Q.astype(np.float32), 'VEC3', target=34962, minmax=True)
                if 'NORMAL' in A_: A_['NORMAL'] = add((acc(J, BIN, A_['NORMAL']).astype(float) @ Rf.T).astype(np.float32), 'VEC3', target=34962)
                if 'TANGENT' in A_: Tg = acc(J, BIN, A_['TANGENT']).astype(float); Tg[:, :3] = Tg[:, :3] @ Rf.T; A_['TANGENT'] = add(Tg.astype(np.float32), 'VEC4', target=34962)
                print('  flipped about', a_.round(2), 'outward', nn.round(2))
# the weapon's materials and textures
moff = len(J['materials']); toff = len(J.get('textures', [])); ioff = len(J.setdefault('images', [])); soff = len(J.setdefault('samplers', []))
for im in WJ.get('images', []):
    v = WJ['bufferViews'][im['bufferView']]; data = WB[v.get('byteOffset', 0): v.get('byteOffset', 0) + v['byteLength']]
    while len(BIN) % 4: BIN.append(0)
    J['bufferViews'].append({'buffer': 0, 'byteOffset': len(BIN), 'byteLength': len(data)}); BIN.extend(data)
    J['images'].append({'bufferView': len(J['bufferViews']) - 1, 'mimeType': im['mimeType'], 'name': 'weapon_%d' % len(J['images'])})
for smp in WJ.get('samplers', []): J['samplers'].append(dict(smp))
for tx in WJ.get('textures', []):
    t2 = dict(tx); t2['source'] = tx['source'] + ioff
    if 'sampler' in tx: t2['sampler'] = tx['sampler'] + soff
    J.setdefault('textures', []).append(t2)
def retex(dd):
    if isinstance(dd, dict): return {k: (v + toff if k == 'index' else retex(v)) for k, v in dd.items()}
    return dd
for mm in WJ['materials']:
    m2 = retex(json.loads(json.dumps(mm))); m2['name'] = 'weapon_' + mm.get('name', 'm')
    if m2.get('alphaMode') == 'BLEND': m2['alphaMode'] = 'MASK'; m2['alphaCutoff'] = 0.4
    J['materials'].append(m2)
for Mw, p in prims:
    A = p['attributes']; Pn = acc(WJ, WB, A['POSITION']).astype(float); nv = len(Pn)
    P = (np.c_[Pn, np.ones(nv)] @ Mw.T)[:, :3]; P = ((P - wbutt) * sc) @ Rw.T + butt
    R3 = Mw[:3, :3] / np.linalg.norm(Mw[:3, :3], axis=0)
    at = {'POSITION': add(P.astype(np.float32), 'VEC3', target=34962, minmax=True)}
    if 'NORMAL' in A:
        N = acc(WJ, WB, A['NORMAL']).astype(float) @ R3.T @ Rw.T; N /= np.linalg.norm(N, axis=1, keepdims=True) + 1e-12; at['NORMAL'] = add(N.astype(np.float32), 'VEC3', target=34962)
    if 'TANGENT' in A:
        Tg = acc(WJ, WB, A['TANGENT']).astype(float); Tg[:, :3] = Tg[:, :3] @ R3.T @ Rw.T; at['TANGENT'] = add(Tg.astype(np.float32), 'VEC4', target=34962)
    for k in ('TEXCOORD_0',):
        if k in A: at[k] = add(acc(WJ, WB, A[k]).astype(np.float32), 'VEC2', target=34962)
    if not RIGID:
        at['JOINTS_0'] = add(np.tile(np.array([joint, 0, 0, 0], np.uint16), (nv, 1)), 'VEC4', 5123, 34962)
        at['WEIGHTS_0'] = add(np.tile(np.array([1, 0, 0, 0], np.float32), (nv, 1)), 'VEC4', target=34962)
    pr = {'attributes': at, 'material': p['material'] + moff, 'mode': p.get('mode', 4)}
    if 'indices' in p: pr['indices'] = add(acc(WJ, WB, p['indices']).reshape(-1).astype(np.uint32), 'SCALAR', 5125, 34963)
    (m if not RIGID else newmesh)['primitives'].append(pr)
while len(BIN) % 4: BIN.append(0)
J['buffers'][0]['byteLength'] = len(BIN); js = json.dumps(J, separators=(',', ':')).encode()
while len(js) % 4: js += b' '
with open(OUT, 'wb') as f:
    f.write(struct.pack('<III', 0x46546C67, 2, 28 + len(js) + len(BIN))); f.write(struct.pack('<II', len(js), 0x4E4F534A)); f.write(js); f.write(struct.pack('<II', len(BIN), 0x004E4942)); f.write(BIN)
print('wrote', OUT)
