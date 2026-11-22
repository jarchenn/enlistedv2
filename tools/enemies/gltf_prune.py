# K10: drop what no node uses from a GLB (a mesh whose node lost it, its materials, their textures / images, their accessors
# and buffer views) and repack the binary. Knight 10 is built on Knight 3, whose flail (CC-BY-NC-ND, local-only) and Roman
# shield leave their nodes in the merge but must not ship inside the committed file.
#   python -I tools/enemies/gltf_prune.py <in.glb> <out.glb>
import json, struct, sys
SRC, OUT = sys.argv[1:3]
b = open(SRC, 'rb').read(); assert b[:4] == b'glTF'
jl = struct.unpack('<I', b[12:16])[0]; J = json.loads(b[20:20 + jl]); bl = struct.unpack('<I', b[20 + jl:24 + jl])[0]; BIN = b[28 + jl:28 + jl + bl]
def texrefs(d, out):   # every {"index": t} under a *Texture key (core and extensions)
    for k, v in d.items():
        if isinstance(v, dict):
            if k.endswith('Texture') and 'index' in v: out.append(v)
            else: texrefs(v, out)
    return out
meshes = sorted({n['mesh'] for n in J['nodes'] if 'mesh' in n})
mats = sorted({p['material'] for m in meshes for p in J['meshes'][m]['primitives'] if 'material' in p})
texs = sorted({r['index'] for m in mats for r in texrefs(J['materials'][m], [])})
imgs = sorted({J['textures'][t]['source'] for t in texs if 'source' in J['textures'][t]})
samps = sorted({J['textures'][t]['sampler'] for t in texs if 'sampler' in J['textures'][t]})
acc = set()
for m in meshes:
    for p in J['meshes'][m]['primitives']:
        acc |= set(p['attributes'].values()); acc |= {p['indices']} if 'indices' in p else set()
        for t in p.get('targets', []): acc |= set(t.values())
for s in J.get('skins', []):
    if 'inverseBindMatrices' in s: acc.add(s['inverseBindMatrices'])
for a in J.get('animations', []):
    for s in a['samplers']: acc |= {s['input'], s['output']}
acc = sorted(acc)
views = sorted({J['accessors'][a]['bufferView'] for a in acc if 'bufferView' in J['accessors'][a]} | {J['images'][i]['bufferView'] for i in imgs if 'bufferView' in J['images'][i]})
R = lambda keep: {o: n for n, o in enumerate(keep)}
rm, rmat, rtex, rimg, rsam, racc, rview = R(meshes), R(mats), R(texs), R(imgs), R(samps), R(acc), R(views)
nb = bytearray(); nviews = []
for v in views:
    x = dict(J['bufferViews'][v]); o = x.get('byteOffset', 0)
    while len(nb) % 4: nb.append(0)
    data = BIN[o:o + x['byteLength']]; x['byteOffset'] = len(nb); x['buffer'] = 0; nb.extend(data); nviews.append(x)
naccs = []
for a in acc:
    x = dict(J['accessors'][a])
    if 'bufferView' in x: x['bufferView'] = rview[x['bufferView']]
    naccs.append(x)
nimgs = []
for i in imgs:
    x = dict(J['images'][i])
    if 'bufferView' in x: x['bufferView'] = rview[x['bufferView']]
    nimgs.append(x)
ntex = []
for t in texs:
    x = dict(J['textures'][t])
    if 'source' in x: x['source'] = rimg[x['source']]
    if 'sampler' in x: x['sampler'] = rsam[x['sampler']]
    ntex.append(x)
nmat = []
for m in mats:
    x = json.loads(json.dumps(J['materials'][m]))
    for r in texrefs(x, []): r['index'] = rtex[r['index']]
    nmat.append(x)
nmesh = []
for m in meshes:
    x = json.loads(json.dumps(J['meshes'][m]))
    for p in x['primitives']:
        p['attributes'] = {k: racc[v] for k, v in p['attributes'].items()}
        if 'indices' in p: p['indices'] = racc[p['indices']]
        if 'material' in p: p['material'] = rmat[p['material']]
        if 'targets' in p: p['targets'] = [{k: racc[v] for k, v in t.items()} for t in p['targets']]
    nmesh.append(x)
for n in J['nodes']:
    if 'mesh' in n: n['mesh'] = rm[n['mesh']]
for s in J.get('skins', []):
    if 'inverseBindMatrices' in s: s['inverseBindMatrices'] = racc[s['inverseBindMatrices']]
for a in J.get('animations', []):
    for s in a['samplers']: s['input'] = racc[s['input']]; s['output'] = racc[s['output']]
gone = dict(meshes=len(J['meshes']) - len(meshes), materials=len(J['materials']) - len(mats), images=len(J.get('images', [])) - len(imgs), kb=(len(BIN) - len(nb)) // 1024)
J.update(meshes=nmesh, materials=nmat, textures=ntex, images=nimgs, accessors=naccs, bufferViews=nviews, buffers=[{'byteLength': len(nb)}])
if samps or 'samplers' in J: J['samplers'] = [J['samplers'][s] for s in samps]
for k in ('textures', 'images', 'samplers'):
    if not J.get(k): J.pop(k, None)
js = json.dumps(J, separators=(',', ':')).encode(); js += b' ' * (-len(js) % 4)
while len(nb) % 4: nb.append(0)
open(OUT, 'wb').write(b'glTF' + struct.pack('<II', 2, 28 + len(js) + len(nb)) + struct.pack('<I', len(js)) + b'JSON' + js + struct.pack('<I', len(nb)) + b'BIN\0' + bytes(nb))
print('pruned: %(meshes)d meshes, %(materials)d materials, %(images)d images, %(kb)d KB' % gone, '->', OUT)
