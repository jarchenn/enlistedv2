# KN9b: make one material of a GLB read as another (Knight 9's recoloured helmet against his armour's steel).
# - base colour: the target's painted texels (luminance > 8 / 255; the atlas's empty black is left) histogram-matched in
#   luminance to the reference's, so the darks, mid-tones and highlights land where the reference's do (detail kept: the
#   order of every texel's brightness is unchanged), then given the reference's mean tint (its colour over its luminance)
# - metallic / roughness: a 4x4 metallicRoughness texture at the reference map's medians, factors 1. Without a map the game
#   draws a material as cloth (metal <= 0.05, roughness >= 0.75: character.cpp), which is why the helmet looked matte
#   next to polished armour
#   python -I tools/enemies/glb_match_material.py <in.glb> <out.glb> <target material> <reference material> [<colour share 0..1, default 1>]
import json, struct, sys, io
import numpy as np
from PIL import Image
SRC, OUT, TGT, REF = sys.argv[1:5]; K = float(sys.argv[5]) if len(sys.argv) > 5 else 1.0   # K: how far the colour moves (1 = all the way to the reference's)
b = open(SRC, 'rb').read(); jl = struct.unpack('<I', b[12:16])[0]; J = json.loads(b[20:20 + jl]); bl = struct.unpack('<I', b[20 + jl:24 + jl])[0]; BIN = bytearray(b[28 + jl:28 + jl + bl])
mats = {m['name']: m for m in J['materials']}; T, R = mats[TGT], mats[REF]
def img(ti):
    v = J['bufferViews'][J['images'][J['textures'][ti]['source']]['bufferView']]; o = v.get('byteOffset', 0); return Image.open(io.BytesIO(bytes(BIN[o:o + v['byteLength']])))
def put(im, mime='image/png'):
    buf = io.BytesIO(); im.save(buf, 'JPEG', quality=92) if mime == 'image/jpeg' else im.save(buf, 'PNG'); data = buf.getvalue()
    while len(BIN) % 4: BIN.append(0)
    J['bufferViews'].append({'buffer': 0, 'byteOffset': len(BIN), 'byteLength': len(data)}); BIN.extend(data)
    J['images'].append({'bufferView': len(J['bufferViews']) - 1, 'mimeType': mime}); J['textures'].append({'source': len(J['images']) - 1}); return len(J['textures']) - 1
LW = np.array([0.2126, 0.7152, 0.0722], np.float32)
rp, tp = R['pbrMetallicRoughness'], T['pbrMetallicRoughness']
ra = np.asarray(img(rp['baseColorTexture']['index']).convert('RGB'), np.float32).reshape(-1, 3); rl = ra @ LW
ti = tp['baseColorTexture']['index']; tim = img(ti); ta = np.asarray(tim.convert('RGB'), np.float32); tl = ta @ LW; used = tl > 8
src_sorted = np.sort(tl[used]); ref_q = np.quantile(rl, np.linspace(0, 1, 1025))
rank = np.searchsorted(src_sorted, tl[used]) / max(len(src_sorted) - 1, 1)   # each painted texel's brightness rank 0..1
newL = np.interp(rank, np.linspace(0, 1, 1025), ref_q)
tint = ra.mean(0) / max(float(rl.mean()), 1e-3)                              # the reference's colour per unit luminance
out = ta.copy(); out[used] = np.clip((1 - K) * ta[used] + K * newL[:, None] * tint[None, :], 0, 255)
tp['baseColorTexture'] = {'index': put(Image.fromarray(out.astype(np.uint8)), J['images'][J['textures'][ti]['source']].get('mimeType', 'image/png'))}   # the source's format (Knight 9's helmet: JPEG)
mr = np.asarray(img(rp['metallicRoughnessTexture']['index']).convert('RGB'), np.float32) / 255
rough, metal = float(np.median(mr[..., 1])), float(np.median(mr[..., 2]))
c = np.zeros((4, 4, 3), np.uint8); c[..., 0] = 255; c[..., 1] = round(rough * 255); c[..., 2] = round(metal * 255)
tp['metallicRoughnessTexture'] = {'index': put(Image.fromarray(c))}; tp['metallicFactor'] = 1.0; tp['roughnessFactor'] = 1.0; tp.pop('baseColorFactor', None)
# compact: only the buffer views something still uses (accessors, images in use), textures / images no material uses dropped
used_tex = sorted({t['index'] for m in J['materials'] for t in [m.get('normalTexture'), m.get('occlusionTexture'), m.get('emissiveTexture'), m.get('pbrMetallicRoughness', {}).get('baseColorTexture'), m.get('pbrMetallicRoughness', {}).get('metallicRoughnessTexture')] if t})
tex_map = {o: n for n, o in enumerate(used_tex)}; used_img = sorted({J['textures'][t]['source'] for t in used_tex}); img_map = {o: n for n, o in enumerate(used_img)}
J['textures'] = [dict(J['textures'][t], source=img_map[J['textures'][t]['source']]) for t in used_tex]; J['images'] = [J['images'][i] for i in used_img]
for m in J['materials']:
    for t in [m.get('normalTexture'), m.get('occlusionTexture'), m.get('emissiveTexture'), m.get('pbrMetallicRoughness', {}).get('baseColorTexture'), m.get('pbrMetallicRoughness', {}).get('metallicRoughnessTexture')]:
        if t: t['index'] = tex_map[t['index']]
NB = bytearray(); views = []; vmap = {}
def keep(k):
    if k not in vmap:
        v = J['bufferViews'][k]; o = v.get('byteOffset', 0)
        while len(NB) % 4: NB.append(0)
        nv = {'buffer': 0, 'byteOffset': len(NB), 'byteLength': v['byteLength']}; nv.update({x: v[x] for x in ('byteStride', 'target') if x in v})
        NB.extend(BIN[o:o + v['byteLength']]); views.append(nv); vmap[k] = len(views) - 1
    return vmap[k]
for a in J['accessors']:
    if 'bufferView' in a: a['bufferView'] = keep(a['bufferView'])
    for part in ('indices', 'values'):
        if part in a.get('sparse', {}): a['sparse'][part]['bufferView'] = keep(a['sparse'][part]['bufferView'])
for im in J['images']: im['bufferView'] = keep(im['bufferView'])
J['bufferViews'] = views; BIN = NB
while len(BIN) % 4: BIN.append(0)
J['buffers'][0]['byteLength'] = len(BIN); js = json.dumps(J, separators=(',', ':')).encode()
while len(js) % 4: js += b' '
with open(OUT, 'wb') as f:
    f.write(struct.pack('<III', 0x46546C67, 2, 28 + len(js) + len(BIN))); f.write(struct.pack('<II', len(js), 0x4E4F534A)); f.write(js); f.write(struct.pack('<II', len(BIN), 0x004E4942)); f.write(BIN)
nl = out[used] @ LW
print('%s -> %s: luminance median %.0f -> %.0f (reference %.0f), tint %s, metal %.2f rough %.2f; wrote %s' % (TGT, REF, np.median(tl[used]), np.median(nl), np.median(rl), tint.round(3), metal, rough, OUT))
