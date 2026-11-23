# BC3: less luster on a character's clothes only. For the named materials, a metallicRoughness map (G roughness, B metal,
# factors set to 1) is drawn from the material's own base colour image: skin and gold texels (warm, mid-saturated, not
# dark) keep the material's metallic / roughness factors, every other texel (cloth, leather, lace, cuffs) gets the cloth
# values given. Run after glb_restore_textures.py (the factors it restored are the skin's values here).
#   python -I tools/enemies/glb_cloth_luster.py <in.glb> <out.glb> <material,...> <cloth metal> <cloth rough>
import json, struct, sys, io, re
import numpy as np
from PIL import Image, ImageFilter
SRC, OUT, MATS, CM, CR = sys.argv[1], sys.argv[2], sys.argv[3].split(','), float(sys.argv[4]), float(sys.argv[5])
b = open(SRC, 'rb').read(); jl = struct.unpack('<I', b[12:16])[0]; J = json.loads(b[20:20 + jl]); bl = struct.unpack('<I', b[20 + jl:24 + jl])[0]; BIN = bytearray(b[28 + jl:28 + jl + bl])
def img(ti):
    v = J['bufferViews'][J['images'][J['textures'][ti]['source']]['bufferView']]; o = v.get('byteOffset', 0); return Image.open(io.BytesIO(bytes(BIN[o:o + v['byteLength']])))
def keep_mask(im):   # 1 = skin / gold (keeps the material's values), 0 = clothes
    w, h = im.size; a = np.asarray(im.convert('RGB').resize((w // 2, h // 2), Image.LANCZOS)).astype(np.int32)
    R, G, B = a[..., 0], a[..., 1], a[..., 2]; mx = a.max(-1); sat = (mx - a.min(-1)) / np.maximum(mx, 1)
    k = (R > G) & (G >= B) & (R - B > 35) & (mx > 90) & (sat > 0.2) & (sat < 0.85)
    m = Image.fromarray((k * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(3)).filter(ImageFilter.MinFilter(5)).filter(ImageFilter.MaxFilter(3)).filter(ImageFilter.GaussianBlur(1.5))
    return np.asarray(m, np.float32) / 255.0
done = []
for m in J['materials']:
    name = re.sub(r'\.\d+$', '', m['name'])
    if name not in MATS: continue
    pb = m['pbrMetallicRoughness']; km, kr = pb.get('metallicFactor', 1.0), pb.get('roughnessFactor', 1.0); k = keep_mask(img(pb['baseColorTexture']['index']))
    mr = np.zeros(k.shape + (3,), np.float32); mr[..., 0] = 1; mr[..., 1] = k * kr + (1 - k) * CR; mr[..., 2] = k * km + (1 - k) * CM
    buf = io.BytesIO(); Image.fromarray(np.clip(mr * 255 + 0.5, 0, 255).astype(np.uint8)).save(buf, 'PNG'); data = buf.getvalue()
    while len(BIN) % 4: BIN.append(0)
    J['bufferViews'].append({'buffer': 0, 'byteOffset': len(BIN), 'byteLength': len(data)}); BIN.extend(data)
    J['images'].append({'bufferView': len(J['bufferViews']) - 1, 'mimeType': 'image/png'}); J['textures'].append({'source': len(J['images']) - 1})
    pb['metallicRoughnessTexture'] = {'index': len(J['textures']) - 1}; pb['metallicFactor'] = 1.0; pb['roughnessFactor'] = 1.0
    done.append('%s: skin %.2f / %.2f, clothes %.2f / %.2f, %d%% of the image kept' % (name, km, kr, CM, CR, 100 * k.mean()))
assert len(done) == len(MATS), (done, MATS)
while len(BIN) % 4: BIN.append(0)
J['buffers'][0]['byteLength'] = len(BIN); js = json.dumps(J, separators=(',', ':')).encode()
while len(js) % 4: js += b' '
with open(OUT, 'wb') as f:
    f.write(struct.pack('<III', 0x46546C67, 2, 28 + len(js) + len(BIN))); f.write(struct.pack('<II', len(js), 0x4E4F534A)); f.write(js); f.write(struct.pack('<II', len(BIN), 0x004E4942)); f.write(BIN)
print('\n'.join(done)); print('wrote', OUT)
