# the platemail's steel as lustrous as Knight 10's (its metal/roughness map rescaled to Knight 10's means on the steel; the
# leather, by its brown base colour, kept); its silver base colour unchanged. glTF level.
#   python -I lustre.py <in.glb> <out.glb>
import json, struct, sys, io
import numpy as np
from PIL import Image
b = open(sys.argv[1], 'rb').read(); jl = struct.unpack('<I', b[12:16])[0]; bl = struct.unpack('<I', b[20 + jl:24 + jl])[0]
J = json.loads(b[20:20 + jl]); BIN = bytearray(b[28 + jl:28 + jl + bl])
def img(ti):
    im = J['images'][J['textures'][ti]['source']]; v = J['bufferViews'][im['bufferView']]
    return np.asarray(Image.open(io.BytesIO(bytes(BIN[v.get('byteOffset', 0): v.get('byteOffset', 0) + v['byteLength']]))).convert('RGB')).astype(float)
K_ROUGH, K_METAL = 66.6, 225.4   # Knight 10's 'Metal': roughness (G) and metalness (B) means
done = {}
for m in J['materials']:
    p = m.get('pbrMetallicRoughness', {})
    if 'metallicRoughnessTexture' not in p or 'baseColorTexture' not in p: continue
    ti = p['metallicRoughnessTexture']['index']
    if ti in done: p['metallicRoughnessTexture']['index'] = done[ti]; continue
    base = img(p['baseColorTexture']['index']); mr = img(ti)
    if base.shape != mr.shape: base = np.asarray(Image.fromarray(base.astype(np.uint8)).resize(mr.shape[1::-1])).astype(float)
    mx, mn = base.max(-1), base.min(-1); sat = (mx - mn) / np.maximum(mx, 1)
    steel = (sat < 0.18) & (mx > 12)   # grey: the steel (leather and cloth are warmer)
    out = mr.copy(); g, bch = mr[..., 1], mr[..., 2]
    out[..., 1] = np.where(steel, np.clip(g * K_ROUGH / max(g[steel].mean(), 1), 8, 255), g)
    out[..., 2] = np.where(steel, np.clip(bch * K_METAL / max(bch[steel].mean(), 1), 0, 255), bch)
    print('%s: steel %.0f %% of texels; roughness %.0f -> %.0f, metalness %.0f -> %.0f' % (m['name'], 100 * steel.mean(), g[steel].mean(), out[..., 1][steel].mean(), bch[steel].mean(), out[..., 2][steel].mean()))
    buf = io.BytesIO(); Image.fromarray(out.astype(np.uint8)).save(buf, 'PNG'); data = buf.getvalue()
    while len(BIN) % 4: BIN.append(0)
    J['bufferViews'].append({'buffer': 0, 'byteOffset': len(BIN), 'byteLength': len(data)}); BIN.extend(data)
    J['images'].append({'bufferView': len(J['bufferViews']) - 1, 'mimeType': 'image/png', 'name': 'lustre_mr'})
    t = dict(J['textures'][ti]); t['source'] = len(J['images']) - 1; J['textures'].append(t); done[ti] = len(J['textures']) - 1
    p['metallicRoughnessTexture']['index'] = done[ti]
while len(BIN) % 4: BIN.append(0)
J['buffers'][0]['byteLength'] = len(BIN); js = json.dumps(J, separators=(',', ':')).encode()
while len(js) % 4: js += b' '
with open(sys.argv[2], 'wb') as f:
    f.write(struct.pack('<III', 0x46546C67, 2, 28 + len(js) + len(BIN))); f.write(struct.pack('<II', len(js), 0x4E4F534A)); f.write(js); f.write(struct.pack('<II', len(BIN), 0x004E4942)); f.write(BIN)
print('wrote', sys.argv[2])
