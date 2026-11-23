# LT1: take a rigid part (a node with a mesh, its materials and textures) from one character GLB and hang it under a joint of
# another, where it sat on the same joint in the source: the sword-and-shield knight's Chevalier sword into Letholdus's right
# hand. Both rigs Mixamo's (joints matched with Mixamo's prefix taken off). The part's local translation and scale are taken
# into the destination joint's frame: turned by the two joints' rest-pose difference (the clips are matched by world turns from
# rest, retarget_clips.py, so the part then turns with the hand as it did in the source) and scaled by the two characters' size
# (hips height at rest) and the joints' world scales (the knight's rig is in centimetres).
#   python -I tools/enemies/glb_graft_part.py <src.glb> <part node> <dst.glb> <joint> <out.glb> [<size x, default 1>]
import json, struct, sys, re
import numpy as np
SRC, PART, DST, JOINT, OUT = sys.argv[1:6]; SIZE = float(sys.argv[6]) if len(sys.argv) > 6 else 1.0   # SIZE: the part's size x this, about the joint (the grip stays in the hand)
def load(p):
    b = open(p, 'rb').read(); jl = struct.unpack('<I', b[12:16])[0]; J = json.loads(b[20:20 + jl]); bl = struct.unpack('<I', b[20 + jl:24 + jl])[0]
    return J, bytearray(b[28 + jl:28 + jl + bl])
S, SB = load(SRC); D, DB = load(DST)
key = lambda n: re.sub(r'^(b_mixamorig\d*_|mixamorig\d*:)', '', n or '')
si = next(i for i, n in enumerate(S['nodes']) if n.get('name') == PART)
spar = {c: i for i, n in enumerate(S['nodes']) for c in n.get('children', [])}
sj = spar[si]; dj = next(i for i, n in enumerate(D['nodes']) if key(n.get('name')) == key(S['nodes'][sj].get('name')) == key(JOINT))
def q2m(q):
    x, y, z, w = q; return np.array([[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)], [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)], [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]])
def m2q(m):
    t = np.trace(m)
    if t > 0: s = 2 * np.sqrt(t + 1); return [(m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s, s / 4]
    i = int(np.argmax(np.diag(m))); j, k2 = (i + 1) % 3, (i + 2) % 3; s = 2 * np.sqrt(1 + m[i, i] - m[j, j] - m[k2, k2]); q = [0.0] * 4
    q[i] = s / 4; q[j] = (m[j, i] + m[i, j]) / s; q[k2] = (m[k2, i] + m[i, k2]) / s; q[3] = (m[k2, j] - m[j, k2]) / s; return q
def rest_world(G, i):   # the joint's rest world matrix (its glTF node chain)
    par = {c: p for p, n in enumerate(G['nodes']) for c in n.get('children', [])}; M = np.eye(4)
    while True:
        n = G['nodes'][i]; L = np.eye(4)
        if 'matrix' in n: L = np.array(n['matrix'], float).reshape(4, 4).T
        else: L[:3, :3] = q2m(n.get('rotation', [0, 0, 0, 1])) * np.array(n.get('scale', [1, 1, 1])); L[:3, 3] = n.get('translation', [0, 0, 0])
        M = L @ M
        if i not in par: return M
        i = par[i]
def hips_h(G): return abs(rest_world(G, next(i for i, n in enumerate(G['nodes']) if key(n.get('name')) == 'Hips'))[1, 3])
Ms, Md = rest_world(S, sj), rest_world(D, dj); ss, sd = np.linalg.norm(Ms[:3, 0]), np.linalg.norm(Md[:3, 0]); Rs0, Rd0 = Ms[:3, :3] / ss, Md[:3, :3] / sd
ratio = hips_h(D) / hips_h(S)          # the two characters' size in their own world units (hips height at rest)
C = Rd0.T @ Rs0                         # the source hand's rest frame seen from the destination hand's: the clips match world turns from rest (retarget_clips.py)
k = ss * ratio / sd * SIZE
amap, vmap, tmap, imap, mmap, smap = {}, {}, {}, {}, {}, {}
def view(v):
    if v not in vmap:
        bv = S['bufferViews'][v]; o = bv.get('byteOffset', 0); data = SB[o:o + bv['byteLength']]
        while len(DB) % 4: DB.append(0)
        nv = {'buffer': 0, 'byteOffset': len(DB), 'byteLength': bv['byteLength']}; nv.update({x: bv[x] for x in ('byteStride', 'target') if x in bv})
        DB.extend(data); D['bufferViews'].append(nv); vmap[v] = len(D['bufferViews']) - 1
    return vmap[v]
def acc(a):
    if a not in amap:
        x = dict(S['accessors'][a]); x['bufferView'] = view(x['bufferView']); D['accessors'].append(x); amap[a] = len(D['accessors']) - 1
    return amap[a]
def tex(t):
    if t not in tmap:
        T = dict(S['textures'][t]); src = T.get('source')
        if src is not None:
            if src not in imap:
                im = dict(S['images'][src]); im['bufferView'] = view(im['bufferView']); D.setdefault('images', []).append(im); imap[src] = len(D['images']) - 1
            T['source'] = imap[src]
        if 'sampler' in T:
            sm = T['sampler']
            if sm not in smap: D.setdefault('samplers', []).append(S['samplers'][sm]); smap[sm] = len(D['samplers']) - 1
            T['sampler'] = smap[sm]
        D.setdefault('textures', []).append(T); tmap[t] = len(D['textures']) - 1
    return {'index': tmap[t]}
def mat(m):
    if m not in mmap:
        M = json.loads(json.dumps(S['materials'][m])); pb = M.get('pbrMetallicRoughness', {})
        for kk in ('baseColorTexture', 'metallicRoughnessTexture'):
            if kk in pb: pb[kk] = dict(pb[kk], **tex(pb[kk]['index']))
        for kk in ('normalTexture', 'occlusionTexture', 'emissiveTexture'):
            if kk in M: M[kk] = dict(M[kk], **tex(M[kk]['index']))
        D['materials'].append(M); mmap[m] = len(D['materials']) - 1
    return mmap[m]
me = S['meshes'][S['nodes'][si]['mesh']]; prims = []
for p in me['primitives']:
    q = {'attributes': {a: acc(i) for a, i in p['attributes'].items() if a in ('POSITION', 'NORMAL', 'TEXCOORD_0', 'TANGENT')}, 'indices': acc(p['indices'])}
    if 'material' in p: q['material'] = mat(p['material'])
    if 'mode' in p: q['mode'] = p['mode']
    prims.append(q)
D['meshes'].append({'name': me.get('name', PART), 'primitives': prims})
n = S['nodes'][si]; node = {'name': PART, 'mesh': len(D['meshes']) - 1, 'translation': [float(x) for x in k * (C @ np.array(n.get('translation', [0, 0, 0]), float))], 'rotation': [float(x) for x in m2q(C @ q2m(n.get('rotation', [0, 0, 0, 1])))], 'scale': [float(x) * k for x in n.get('scale', [1, 1, 1])]}
D['nodes'].append(node); D['nodes'][dj].setdefault('children', []).append(len(D['nodes']) - 1)
cred = S['asset'].get('extras', {}).get('credits')
while len(DB) % 4: DB.append(0)
D['buffers'][0]['byteLength'] = len(DB); js = json.dumps(D, separators=(',', ':')).encode()
while len(js) % 4: js += b' '
with open(OUT, 'wb') as f:
    f.write(struct.pack('<III', 0x46546C67, 2, 28 + len(js) + len(DB))); f.write(struct.pack('<II', len(js), 0x4E4F534A)); f.write(js); f.write(struct.pack('<II', len(DB), 0x004E4942)); f.write(DB)
print('grafted %s under %s (x%.5f): %d primitives, %d materials, %d images -> %s' % (PART, D['nodes'][dj]['name'], k, len(prims), len(mmap), len(imap), OUT))
