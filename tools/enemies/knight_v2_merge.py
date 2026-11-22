# Knight 2: knight_ss.glb + the parts of knight_v2_parts.py -> knight_v2.glb, at the glTF level (the skinned body and its 51
# clips stay byte for byte; nothing goes through a Blender round trip).
#   python -I tools/enemies/knight_v2_merge.py <knight_ss.glb> <kn2_parts.glb> <metal_base_clean.png> <out.glb> [<body_edits.json> | - [<credits>]]
# KN6: parts named KNB_<bone>_* hang on mixamorig:<bone>; body_edits.json {"moves": [[rest xyz, new rest xyz], ...]} (glTF
# coordinates, tools/enemies/knight_parts.py) moves the matching skinned body vertices: the new rest position is taken back
# through the vertex's own blended skin matrix into the mesh's bind space, so the clips deform it as before
# - the sword and the heater shield nodes lose their meshes (the nodes stay: nothing else is renumbered)
# - the knight's own head and helmet: the triangles touching a vertex with over 0.5 weight on mixamorig:Head + HeadTop_End
#   leave the body's index buffers
# - the parts hang on their bones with the bone's inverse rest transform (their vertices are in world rest coordinates):
#   KN2_mace on RightHand, KN2_helm_* on Head, KN2_shield on LeftForeArm (rigid; the game binds an unskinned mesh to its
#   nearest joint ancestor)
# - the armour material "Metal" takes the de-reddened base colour (the user: "get rid of the red streaks")
import json, struct, sys, io
import numpy as np
from PIL import Image
SRC, PARTS, METAL, OUT = sys.argv[1:5]
EDITS = sys.argv[5] if len(sys.argv) > 5 and sys.argv[5] != '-' else None; CREDITS = sys.argv[6] if len(sys.argv) > 6 else None
def load(p):
    b = open(p, 'rb').read(); assert b[:4] == b'glTF'
    jl = struct.unpack('<I', b[12:16])[0]; J = json.loads(b[20:20 + jl]); bl = struct.unpack('<I', b[20 + jl:24 + jl])[0]
    return J, bytearray(b[28 + jl:28 + jl + bl])
J, BIN = load(SRC); P, PB = load(PARTS)
def add_view(data, target=None):
    while len(BIN) % 4: BIN.append(0)
    v = {'buffer': 0, 'byteOffset': len(BIN), 'byteLength': len(data)}
    if target: v['target'] = target
    BIN.extend(data); J['bufferViews'].append(v); return len(J['bufferViews']) - 1
# ---- the parts' data appended (views, accessors, images, samplers, textures, materials, meshes), indices shifted
off = {k: len(J.get(k, [])) for k in ('bufferViews', 'accessors', 'images', 'samplers', 'textures', 'materials', 'meshes')}
for v in P['bufferViews']:
    data = PB[v.get('byteOffset', 0):v.get('byteOffset', 0) + v['byteLength']]
    i = add_view(bytes(data), v.get('target'));
    if 'byteStride' in v: J['bufferViews'][i]['byteStride'] = v['byteStride']
for a in P['accessors']:
    a2 = dict(a); a2['bufferView'] += off['bufferViews']; J['accessors'].append(a2)
for im in P.get('images', []):
    im2 = dict(im); im2['bufferView'] += off['bufferViews']; im2['name'] = 'KN2_' + im2.get('name', 'img'); J['images'].append(im2)
J.setdefault('samplers', []).extend(P.get('samplers', []))
for t in P.get('textures', []):
    t2 = dict(t); t2['source'] += off['images']
    if 'sampler' in t2: t2['sampler'] += off['samplers']
    J['textures'].append(t2)
def shift_tex(d):
    for k, v in list(d.items()):
        if isinstance(v, dict):
            if k.endswith('Texture') and 'index' in v: v['index'] += off['textures']
            else: shift_tex(v)
for m in P['materials']: m2 = json.loads(json.dumps(m)); shift_tex(m2); J['materials'].append(m2)
for me in P['meshes']:
    me2 = json.loads(json.dumps(me))
    for pr in me2['primitives']:
        pr['attributes'] = {k: v + off['accessors'] for k, v in pr['attributes'].items()}
        if 'indices' in pr: pr['indices'] += off['accessors']
        if 'material' in pr: pr['material'] += off['materials']
    J['meshes'].append(me2)
# ---- rest world transforms of the bones
N = J['nodes']; par = {c: i for i, n in enumerate(N) for c in n.get('children', [])}
def local(n):
    if 'matrix' in n: return np.array(n['matrix'], float).reshape(4, 4).T
    M = np.eye(4); x, y, z, w = n.get('rotation', [0, 0, 0, 1])
    R = np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)], [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)], [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
    M[:3, :3] = R * np.array(n.get('scale', [1, 1, 1])); M[:3, 3] = n.get('translation', [0, 0, 0]); return M
def world(i):
    M = local(N[i])
    while i in par: i = par[i]; M = local(N[i]) @ M
    return M
idx = {n.get('name'): i for i, n in enumerate(N)}
for nm in ('Sword', 'Shield'): N[idx[nm]].pop('mesh', None)
hang = []
for pn in P['nodes']:
    if 'mesh' not in pn: continue
    nm = pn['name']; assert 'translation' not in pn and 'rotation' not in pn and 'scale' not in pn and 'matrix' not in pn, nm   # applied in Blender
    bone = 'mixamorig:' + nm.split('_')[1] if nm.startswith('KNB_') else 'mixamorig:RightHand' if nm.startswith('KN2_mace') else 'mixamorig:Head' if nm.startswith('KN2_helm') else 'mixamorig:LeftForeArm' if nm.startswith('KN2_shield') else None
    assert bone, nm
    bi = idx[bone]; inv = np.linalg.inv(world(bi))
    N.append({'name': nm, 'mesh': pn['mesh'] + off['meshes'], 'matrix': [float(x) for x in inv.T.ravel()]})
    N[bi].setdefault('children', []).append(len(N) - 1); hang.append((nm, bone))
# ---- the head out of the body
skin = J['skins'][0]; jn = [N[j]['name'] for j in skin['joints']]
headj = [k for k, n in enumerate(jn) if n in ('mixamorig:Head', 'mixamorig:HeadTop_End')]; assert len(headj) == 2, jn
CT = {5121: np.uint8, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
def read(ai):
    a = J['accessors'][ai]; v = J['bufferViews'][a['bufferView']]; n = {'SCALAR': 1, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}[a['type']]; dt = np.dtype(CT[a['componentType']])
    st = v.get('byteStride', dt.itemsize * n); assert st == dt.itemsize * n
    out = np.frombuffer(bytes(BIN), dt, count=a['count'] * n, offset=v.get('byteOffset', 0) + a.get('byteOffset', 0)).reshape(-1, n)
    return out
body = next(n for n in N if n.get('name') == 'Knight_Tpose'); killed = 0; tris0 = tris1 = 0; moved = 0
if EDITS:
    from scipy.spatial import cKDTree
    E = np.array(json.load(open(EDITS))['moves'], float); tree = cKDTree(E[:, 0]); mv_new = E[:, 1]
    ib_ = read(skin['inverseBindMatrices']).astype(np.float64).reshape(-1, 4, 4).transpose(0, 2, 1)
    Sk = np.array([world(j) @ ib_[k] for k, j in enumerate(skin['joints'])])   # rest skin matrices
for pr in J['meshes'][body['mesh']]['primitives']:
    jo = read(pr['attributes']['JOINTS_0']).astype(int)
    wa = J['accessors'][pr['attributes']['WEIGHTS_0']]; w = read(pr['attributes']['WEIGHTS_0']).astype(np.float32)
    if wa['componentType'] == 5121: w = w / 255.0
    elif wa['componentType'] == 5123: w = w / 65535.0
    hw = sum(np.where(jo == h, w, 0).sum(1) for h in headj)
    dead = hw > 0.5; killed += int(dead.sum())
    if EDITS:   # KN6: the chest pulled in (positions in the bind space of each vertex's own skin blend)
        pa = J['accessors'][pr['attributes']['POSITION']]; Pm = read(pr['attributes']['POSITION']).astype(np.float64)
        Mv = np.einsum('vk,vkij->vij', w, Sk[jo]); Pw = np.einsum('vij,vj->vi', Mv[:, :3, :3], Pm) + Mv[:, :3, 3]
        d, nb = tree.query(Pw); hit = d < 2e-4; moved += int(hit.sum())
        if hit.any():
            Pn = Pw.copy(); Pn[hit] = mv_new[nb[hit]]
            Pb = Pm.copy(); Pb[hit] = np.einsum('vij,vj->vi', np.linalg.inv(Mv[hit, :3, :3]), Pn[hit] - Mv[hit, :3, 3])
            Pb32 = np.ascontiguousarray(Pb, np.float32); pv = add_view(Pb32.tobytes(), 34962)
            J['accessors'].append({'bufferView': pv, 'componentType': 5126, 'count': len(Pb32), 'type': 'VEC3', 'min': [float(x) for x in Pb32.min(0)], 'max': [float(x) for x in Pb32.max(0)]})
            pr['attributes']['POSITION'] = len(J['accessors']) - 1
    ia = J['accessors'][pr['indices']]; I = read(pr['indices']).reshape(-1, 3)
    keep = ~dead[I].any(1); I2 = np.ascontiguousarray(I[keep]).astype(np.uint32 if ia['componentType'] == 5125 else CT[ia['componentType']])
    tris0 += len(I); tris1 += len(I2)
    vi = add_view(I2.tobytes(), 34963)
    J['accessors'].append({'bufferView': vi, 'componentType': ia['componentType'], 'count': int(I2.size), 'type': 'SCALAR'}); pr['indices'] = len(J['accessors']) - 1
# ---- the armour's de-reddened base colour
mm = next(m for m in J['materials'] if m['name'] == 'Metal')
buf = io.BytesIO(); Image.open(METAL).convert('RGB').save(buf, 'JPEG', quality=92)
J['images'].append({'name': 'knight_metal_basecolor_clean', 'mimeType': 'image/jpeg', 'bufferView': add_view(buf.getvalue())})
J['textures'].append({'source': len(J['images']) - 1, **({'sampler': J['textures'][mm['pbrMetallicRoughness']['baseColorTexture']['index']]['sampler']} if 'sampler' in J['textures'][mm['pbrMetallicRoughness']['baseColorTexture']['index']] else {})})
mm['pbrMetallicRoughness']['baseColorTexture']['index'] = len(J['textures']) - 1
J['buffers'] = [{'byteLength': len(BIN)}]
J['asset'].setdefault('extras', {})['credits'] = CREDITS if CREDITS else (J['asset']['extras'].get('credits', '') + '; Knight 2: Frogmouth Helmet by lesnykh3d (CC-BY-4.0, '
    'sketchfab.com/3d-models/frogmouth-helmet-2fe4b0c62b09416bbd41816c58afdb12); Knights Shield Practice by jamiul.98 (Sketchfab Standard: '
    'not redistributable); mace: source without licence information (local-only)')
js = json.dumps(J, separators=(',', ':')).encode(); js += b' ' * (-len(js) % 4)
while len(BIN) % 4: BIN.append(0)
open(OUT, 'wb').write(b'glTF' + struct.pack('<II', 2, 28 + len(js) + len(BIN)) + struct.pack('<I', len(js)) + b'JSON' + js + struct.pack('<I', len(BIN)) + b'BIN\0' + bytes(BIN))
assert killed == 4141, killed   # the preview's count (every Knight_Tpose vertex over 0.5 on the head joints)
if EDITS: assert moved >= len(E), (moved, len(E))   # every edited vertex found (seams may duplicate one)
print('head: %d vertices, triangles %d -> %d; body vertices moved %d; parts %s; wrote %s (%d KB)' % (killed, tris0, tris1, moved, hang, OUT, (28 + len(js) + len(BIN)) // 1024))
