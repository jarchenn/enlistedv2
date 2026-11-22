# KH8: a knight's armour-finish tiles cut from its own "Metal" textures (base colour, metal-rough) for helm_finish.py: the
# 400 px window of plain plate (inside the Metal triangles' own UV islands, then the lowest gradient energy among the windows
# whose mean is near the plate's median: no padding, seam, rivet or edge highlight) is cut from both maps at the same place.
#   python -I tools/enemies/metal_tile.py <knight.glb> <out dir> [material=Metal] [size=400]
import io, json, struct, sys
import numpy as np
from PIL import Image
GLB, OUT = sys.argv[1:3]; MAT = sys.argv[3] if len(sys.argv) > 3 else 'Metal'; S = int(sys.argv[4]) if len(sys.argv) > 4 else 400
b = open(GLB, 'rb').read(); jl = struct.unpack('<I', b[12:16])[0]; J = json.loads(b[20:20 + jl]); BIN = b[28 + jl:]
m = next(x for x in J['materials'] if x['name'] == MAT)['pbrMetallicRoughness']
def img(k):
    im = J['images'][J['textures'][m[k]['index']]['source']]; v = J['bufferViews'][im['bufferView']]
    return Image.open(io.BytesIO(BIN[v.get('byteOffset', 0):v.get('byteOffset', 0) + v['byteLength']])).convert('RGB')
base, mr = img('baseColorTexture'), img('metallicRoughnessTexture')
L = np.asarray(base.convert('L'), float); gy, gx = np.gradient(L); E = np.hypot(gx, gy)
# where the Metal triangles really are in UV space (the rest is the bake's edge padding: smeared streaks that read as calm)
from PIL import ImageDraw
mi = next(i for i, x in enumerate(J['materials']) if x['name'] == MAT); H_, W_ = L.shape; cov = Image.new('L', (W_, H_), 0); dr = ImageDraw.Draw(cov)
def acc(i):
    a = J['accessors'][i]; v = J['bufferViews'][a['bufferView']]; n = {'SCALAR': 1, 'VEC2': 2}[a['type']]; dt = {5121: np.uint8, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}[a['componentType']]
    return np.frombuffer(BIN, dt, count=a['count'] * n, offset=v.get('byteOffset', 0) + a.get('byteOffset', 0)).reshape(-1, n)
for me in J['meshes']:
    for pr in me['primitives']:
        if pr.get('material') != mi: continue
        uv = acc(pr['attributes']['TEXCOORD_0']).astype(float) * [W_, H_]; I = acc(pr['indices']).reshape(-1, 3)
        for t in I: dr.polygon([tuple(uv[k]) for k in t], fill=255)
C = np.asarray(cov) > 0; print('Metal UV coverage %.0f %%' % (100 * C.mean())); med = np.median(L[C])
best = None
for y in range(0, L.shape[0] - S + 1, S // 8):
    for x in range(0, L.shape[1] - S + 1, S // 8):
        w = L[y:y + S, x:x + S]
        if C[y:y + S, x:x + S].mean() < 0.995 or abs(w.mean() - med) > 0.25 * med: continue   # inside the plate's own UV islands
        e = E[y:y + S, x:x + S].mean()
        if best is None or e < best[0]: best = (e, x, y)
assert best, 'no plain window'
_, x, y = best; box = (x, y, x + S, y + S)
base.crop(box).save(OUT + '/tile_base.png'); mr.crop(box).save(OUT + '/tile_mr.png')
print('tile %s at (%d, %d) %d px: base mean %s, metal-rough mean %s' % (MAT, x, y, S, np.asarray(base.crop(box), float).reshape(-1, 3).mean(0).round(1), np.asarray(mr.crop(box), float).reshape(-1, 3).mean(0).round(1)))
