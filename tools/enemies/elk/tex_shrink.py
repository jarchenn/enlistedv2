# EK: a GLB's images at most MAX px on a side, re-encoded as JPEG (quality 88) unless the image has real alpha (kept PNG); glTF level
#   python -I tex_shrink.py <in.glb> <out.glb> [max px]
import json, struct, sys, io
import numpy as np
from PIL import Image
P, OUT = sys.argv[1:3]; MAX = int(sys.argv[3]) if len(sys.argv) > 3 else 2048
b = open(P, 'rb').read(); jl = struct.unpack('<I', b[12:16])[0]; bl = struct.unpack('<I', b[20 + jl:24 + jl])[0]
J = json.loads(b[20:20 + jl]); BIN = b[28 + jl:28 + jl + bl]
views = J['bufferViews']; imgv = {im['bufferView'] for im in J.get('images', []) if 'bufferView' in im}
new = bytearray(); remap = {}; before = after = 0
def put(data):
    while len(new) % 4: new.append(0)
    o = len(new); new.extend(data); return o
for i, v in enumerate(views):   # every non-image view copied as is
    if i in imgv: continue
    o = v.get('byteOffset', 0); remap[i] = put(BIN[o:o + v['byteLength']])
for im in J.get('images', []):
    v = views[im['bufferView']]; o = v.get('byteOffset', 0); data = BIN[o:o + v['byteLength']]; before += len(data)
    I = Image.open(io.BytesIO(data)); I.load()
    alpha = I.mode in ('RGBA', 'LA') and np.asarray(I.getchannel('A')).min() < 250
    if max(I.size) > MAX: s = MAX / max(I.size); I = I.resize((max(1, int(I.size[0] * s)), max(1, int(I.size[1] * s))), Image.LANCZOS)
    buf = io.BytesIO()
    if alpha: I.save(buf, 'PNG', optimize=True); im['mimeType'] = 'image/png'
    else: I.convert('RGB').save(buf, 'JPEG', quality=88); im['mimeType'] = 'image/jpeg'
    d2 = buf.getvalue(); after += len(d2); remap[im['bufferView']] = put(d2); v['byteLength'] = len(d2)
for i, v in enumerate(views): v['byteOffset'] = remap[i]; v['buffer'] = 0
while len(new) % 4: new.append(0)
J['buffers'] = [{'byteLength': len(new)}]; js = json.dumps(J, separators=(',', ':')).encode()
while len(js) % 4: js += b' '
with open(OUT, 'wb') as f:
    f.write(struct.pack('<III', 0x46546C67, 2, 28 + len(js) + len(new))); f.write(struct.pack('<II', len(js), 0x4E4F534A)); f.write(js); f.write(struct.pack('<II', len(new), 0x004E4942)); f.write(new)
print('images %.1f MB -> %.1f MB; file %.1f MB' % (before / 1e6, after / 1e6, (28 + len(js) + len(new)) / 1e6))
