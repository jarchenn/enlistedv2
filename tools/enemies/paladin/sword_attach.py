# PM (user): a sword for the re-rigged platemail, held as Knight 4 holds his great sword: the new sword's grip centre on the
# point of Knight 4's blade line nearest his right hand (moved to this rig's right hand), its blade along that line, its flat
# turned as Knight 4's; skinned weight 1 to mixamorig:Weapon (the clips swing it). glTF level.
#   python -I sword_attach.py <retarget_mx.py> <rig.glb> <knight_4.glb> <sword.glb> <out.glb> <length m>
import json, struct, sys
import numpy as np
RT, RIG, K4, SW, OUT = sys.argv[1:6]; LENGTH = float(sys.argv[6])
src = open(RT).read(); g = {'__name__': 'rt'}; exec(src[:src.index("if sys.argv[1] == '--check'")], g)
Rig, load = g['Rig'], g['load']
def ibm(R):
    sk = R.J['skins'][0]; a = R.J['accessors'][sk['inverseBindMatrices']]; v = R.J['bufferViews'][a['bufferView']]
    return np.frombuffer(bytes(R.BIN[v.get('byteOffset', 0) + a.get('byteOffset', 0):][:a['count'] * 64]), np.float32).reshape(-1, 4, 4).transpose(0, 2, 1).astype(float)
def jpos(R, name): sk = R.J['skins'][0]; k = sk['joints'].index(next(i for i, n in enumerate(R.N) if n.get('name') == name)); return np.linalg.inv(ibm(R)[k])[:3, 3], k
# Knight 4's sword in its bind space (a rigid node under its Weapon joint: rest world x the joint's bind correction)
K = Rig(*load(K4)); wn = next(i for i, n in enumerate(K.N) if n.get('name') == 'Weapon'); wj = K.par[wn]; IBk = ibm(K)
kj = K.J['skins'][0]['joints'].index(wj); Sk = K.rest(wj) @ IBk[kj]
Pk = np.concatenate([K.acc(p['attributes']['POSITION']) for p in K.J['meshes'][K.N[wn]['mesh']]['primitives']])
Pk = (np.c_[Pk, np.ones(len(Pk))] @ (Sk @ K.rest(wn)).T)[:, :3]
ck = Pk.mean(0); _, _, Vk = np.linalg.svd(Pk - ck, full_matrices=False); dk = Vk[0]; wk = Vk[1]
handK, _ = jpos(K, 'mixamorig:RightHand'); tk = (Pk - ck) @ dk
rad = np.linalg.norm((Pk - ck) - np.outer(tk, dk), axis=1); span = tk.max() - tk.min()
gripK = ck + dk * ((handK - ck) @ dk)   # Knight 4's grip: the line's point by his hand
tg = (gripK - ck) @ dk
if tk.max() - tg < tg - tk.min(): dk = -dk   # toward the blade: the longer side of the grip
print('Knight 4 sword %.2f m, grip %s, blade toward %s' % (span, gripK.round(3), dk.round(2)))
# the new sword: its own long axis, blade end, flat, and grip centre (between the pommel end and the guard)
WJ, WB = load(SW)
par = {c: i for i, n in enumerate(WJ['nodes']) for c in n.get('children', [])}
def loc(n):
    if 'matrix' in n: return np.array(n['matrix'], float).reshape(4, 4).T
    M = np.eye(4); x, y, z, w = n.get('rotation', [0, 0, 0, 1])
    R = np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)], [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)], [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
    M[:3, :3] = R * np.array(n.get('scale', [1, 1, 1])); M[:3, 3] = n.get('translation', [0, 0, 0]); return M
def wworld(i): return wworld(par[i]) @ loc(WJ['nodes'][i]) if i in par else loc(WJ['nodes'][i])
def wacc(ai):
    a = WJ['accessors'][ai]; v = WJ['bufferViews'][a['bufferView']]; k = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}[a['type']]
    dt = {5126: np.float32, 5125: np.uint32, 5123: np.uint16, 5121: np.uint8}[a['componentType']]
    return np.frombuffer(WB, dt, a['count'] * k, v.get('byteOffset', 0) + a.get('byteOffset', 0)).reshape(a['count'], k).astype(float)
prims = [(wworld(i), p) for i, n in enumerate(WJ['nodes']) if 'mesh' in n for p in WJ['meshes'][n['mesh']]['primitives']]
Pw = np.concatenate([(np.c_[wacc(p['attributes']['POSITION']), np.ones(WJ['accessors'][p['attributes']['POSITION']]['count'])] @ M.T)[:, :3] for M, p in prims])
cw = Pw.mean(0); _, _, Vw = np.linalg.svd(Pw - cw, full_matrices=False); dw = Vw[0]; tw = (Pw - cw) @ dw; spw = tw.max() - tw.min()
radw = np.linalg.norm((Pw - cw) - np.outer(tw, dw), axis=1)
u = (tw - tw.min()) / spw; ug = float(u[np.argmax(radw)])   # the guard: the widest point (the hilt is on its short side)
if ug > 0.5: grip_u = (ug + 1.0) / 2; dw = -dw   # hilt toward u = 1: the grip between guard and pommel; the blade the other way
else: grip_u = ug / 2
gripW = cw + (Vw[0]) * (tw.min() + grip_u * spw)   # (on the original axis)
guard = ug * 60
side = (Pw - cw) - np.outer(tw, dw); flat = Vw[1] if abs(Vw[1] @ dw) < 0.5 else Vw[2]   # the blade's width direction
flat = flat - dw * (flat @ dw); flat /= np.linalg.norm(flat)
print('sword %.2f units, guard at %.0f %%, grip at %.0f %% of its length' % (spw, guard / 60 * 100, grip_u * 100))
# the frame: its axis on Knight 4's line, its flat on Knight 4's flat
wk_ = wk - dk * (wk @ dk); wk_ /= np.linalg.norm(wk_)
Rw = np.c_[dk, wk_, np.cross(dk, wk_)] @ np.c_[dw, flat, np.cross(dw, flat)].T; sc = LENGTH / spw
D = Rig(*load(RIG)); J, BIN = D.J, D.BIN; handD, _ = jpos(D, 'mixamorig:RightHand'); _, wkD = jpos(D, 'mixamorig:Weapon')
target = gripK - handK + handD   # Knight 4's grip offset from his hand, on this rig's hand (both in a T-pose bind)
import os
target = target + np.array(json.loads(os.environ.get('SHIFT_XYZ', '[0, 0, 0]')))   # PM2 (user): the hilt seated in both fists (grip_check.py)
def add(arr, typ, comp=5126, tgt=None, minmax=False):
    data = np.ascontiguousarray(arr).tobytes()
    while len(BIN) % 4: BIN.append(0)
    bv = {'buffer': 0, 'byteOffset': len(BIN), 'byteLength': len(data)}
    if tgt: bv['target'] = tgt
    J['bufferViews'].append(bv); BIN.extend(data)
    a = {'bufferView': len(J['bufferViews']) - 1, 'componentType': comp, 'count': len(arr), 'type': typ}
    if minmax: a['min'] = [float(x) for x in arr.min(0)]; a['max'] = [float(x) for x in arr.max(0)]
    J['accessors'].append(a); return len(J['accessors']) - 1
toff = len(J.get('textures', [])); ioff = len(J.setdefault('images', [])); soff = len(J.setdefault('samplers', [])); moff = len(J['materials'])
for im in WJ.get('images', []):
    v = WJ['bufferViews'][im['bufferView']]; data = WB[v.get('byteOffset', 0): v.get('byteOffset', 0) + v['byteLength']]
    while len(BIN) % 4: BIN.append(0)
    J['bufferViews'].append({'buffer': 0, 'byteOffset': len(BIN), 'byteLength': len(data)}); BIN.extend(data)
    J['images'].append({'bufferView': len(J['bufferViews']) - 1, 'mimeType': im['mimeType'], 'name': 'sword_%d' % len(J['images'])})
for s_ in WJ.get('samplers', []): J['samplers'].append(dict(s_))
for t in WJ.get('textures', []):
    t2 = dict(t); t2['source'] = t['source'] + ioff
    if 'sampler' in t: t2['sampler'] = t['sampler'] + soff
    J.setdefault('textures', []).append(t2)
def retex(d):
    if isinstance(d, dict): return {k: (v + toff if k == 'index' else retex(v)) for k, v in d.items()}
    return d
for m in WJ['materials']: m2 = retex(json.loads(json.dumps(m))); m2['name'] = 'sword_' + m.get('name', 'm'); J['materials'].append(m2)
newp = []
for M, p in prims:
    A = p['attributes']; P0 = wacc(A['POSITION']); nv = len(P0)
    P = (np.c_[P0, np.ones(nv)] @ M.T)[:, :3]; P = ((P - gripW) * sc) @ Rw.T + target
    R3 = M[:3, :3] / np.linalg.norm(M[:3, :3], axis=0)
    at = {'POSITION': add(P.astype(np.float32), 'VEC3', tgt=34962, minmax=True)}
    if 'NORMAL' in A: N = wacc(A['NORMAL']) @ R3.T @ Rw.T; N /= np.linalg.norm(N, axis=1, keepdims=True) + 1e-12; at['NORMAL'] = add(N.astype(np.float32), 'VEC3', tgt=34962)
    if 'TANGENT' in A: Tg = wacc(A['TANGENT']); Tg[:, :3] = Tg[:, :3] @ R3.T @ Rw.T; at['TANGENT'] = add(Tg.astype(np.float32), 'VEC4', tgt=34962)
    if 'TEXCOORD_0' in A: at['TEXCOORD_0'] = add(wacc(A['TEXCOORD_0']).astype(np.float32), 'VEC2', tgt=34962)
    at['JOINTS_0'] = add(np.tile(np.array([wkD, 0, 0, 0], np.uint16), (nv, 1)), 'VEC4', 5123, 34962)
    at['WEIGHTS_0'] = add(np.tile(np.array([1, 0, 0, 0], np.float32), (nv, 1)), 'VEC4', tgt=34962)
    pr = {'attributes': at, 'material': p.get('material', 0) + moff, 'mode': 4}
    if 'indices' in p: pr['indices'] = add(wacc(p['indices']).reshape(-1).astype(np.uint32), 'SCALAR', 5125, 34963)
    newp.append(pr)
J['meshes'].append({'name': 'sword', 'primitives': newp}); J['nodes'].append({'name': 'sword', 'mesh': len(J['meshes']) - 1, 'skin': 0})
J['scenes'][J.get('scene', 0)]['nodes'].append(len(J['nodes']) - 1)
while len(BIN) % 4: BIN.append(0)
J['buffers'][0]['byteLength'] = len(BIN); js = json.dumps(J, separators=(',', ':')).encode()
while len(js) % 4: js += b' '
with open(OUT, 'wb') as f:
    f.write(struct.pack('<III', 0x46546C67, 2, 28 + len(js) + len(BIN))); f.write(struct.pack('<II', len(js), 0x4E4F534A)); f.write(js); f.write(struct.pack('<II', len(BIN), 0x004E4942)); f.write(BIN)
print('wrote', OUT, 'sword grip at', target.round(3), 'length %.2f m' % LENGTH)
