# EK10 (user): the shield keeps its own design (its textures) in the armour's colour and lustre: its base colour scaled to the
# armour steel's mean and tinted to its hue, its roughness channel scaled to the steel's; a new material on the shield only.
#   python -I shield_recolor.py <in.glb> <out.glb> <shield material> <steel glb> <steel material>
import json, struct, sys, io
import numpy as np
from PIL import Image
def load(p):
    b = open(p, 'rb').read(); jl = struct.unpack('<I', b[12:16])[0]; bl = struct.unpack('<I', b[20 + jl:24 + jl])[0]
    return json.loads(b[20:20 + jl]), bytearray(b[28 + jl:28 + jl + bl])
J, BIN = load(sys.argv[1]); SJ, SB = load(sys.argv[4]); SH = sys.argv[3]
def img(JJ, BB, ti):
    im = JJ['images'][JJ['textures'][ti]['source']]; v = JJ['bufferViews'][im['bufferView']]
    return np.asarray(Image.open(io.BytesIO(bytes(BB[v.get('byteOffset', 0): v.get('byteOffset', 0) + v['byteLength']]))).convert('RGB')).astype(float)
sm = next(m for m in SJ['materials'] if m['name'] == sys.argv[5])
steel = img(SJ, SB, sm['pbrMetallicRoughness']['baseColorTexture']['index']).reshape(-1, 3).mean(0)
steel_r = img(SJ, SB, sm['pbrMetallicRoughness']['metallicRoughnessTexture']['index'])[..., 1].mean()
mi = [m['name'] for m in J['materials']].index(SH); m = json.loads(json.dumps(J['materials'][mi]))
base = img(J, BIN, m['pbrMetallicRoughness']['baseColorTexture']['index']); mr = img(J, BIN, m['pbrMetallicRoughness']['metallicRoughnessTexture']['index'])
used = base.sum(-1) > 6   # the atlas's empty areas stay black
lum = base @ [0.2126, 0.7152, 0.0722]; k = (steel @ [0.2126, 0.7152, 0.0722]) / lum[used].mean()
out = np.clip(lum[..., None] * k * (steel / steel.mean()), 0, 255); out[~used] = 0
mr2 = mr.copy(); mr2[..., 1] = np.clip(mr[..., 1] * steel_r / max(mr[..., 1][used].mean(), 1), 0, 255)
print('shield base mean %.0f -> %.0f (steel %s), roughness %.0f -> %.0f' % (base[used].mean(), out[used].mean(), steel.round(), mr[..., 1][used].mean(), mr2[..., 1][used].mean()))
def add_img(a, fmt):
    buf = io.BytesIO(); Image.fromarray(a.astype(np.uint8)).save(buf, fmt, quality=90) if fmt == 'JPEG' else Image.fromarray(a.astype(np.uint8)).save(buf, fmt)
    data = buf.getvalue()
    while len(BIN) % 4: BIN.append(0)
    J['bufferViews'].append({'buffer': 0, 'byteOffset': len(BIN), 'byteLength': len(data)}); BIN.extend(data)
    J['images'].append({'bufferView': len(J['bufferViews']) - 1, 'mimeType': 'image/jpeg' if fmt == 'JPEG' else 'image/png', 'name': 'shield_steel_%d' % len(J['images'])})
    t = dict(J['textures'][m['pbrMetallicRoughness']['baseColorTexture']['index']]); t['source'] = len(J['images']) - 1; J['textures'].append(t); return len(J['textures']) - 1
m['pbrMetallicRoughness']['baseColorTexture']['index'] = add_img(out, 'JPEG'); m['pbrMetallicRoughness']['metallicRoughnessTexture']['index'] = add_img(mr2, 'PNG')
m['name'] = 'shield_steel'; J['materials'].append(m); ni = len(J['materials']) - 1
for mm in J['meshes']:
    for p in mm['primitives']:
        if p.get('material') == mi: p['material'] = ni
Image.fromarray(out.astype(np.uint8)).resize((512, 512)).save(sys.argv[2] + '.base.png')
while len(BIN) % 4: BIN.append(0)
J['buffers'][0]['byteLength'] = len(BIN); js = json.dumps(J, separators=(',', ':')).encode()
while len(js) % 4: js += b' '
with open(sys.argv[2], 'wb') as f:
    f.write(struct.pack('<III', 0x46546C67, 2, 28 + len(js) + len(BIN))); f.write(struct.pack('<II', len(js), 0x4E4F534A)); f.write(js); f.write(struct.pack('<II', len(BIN), 0x004E4942)); f.write(BIN)
print('wrote', sys.argv[2])
