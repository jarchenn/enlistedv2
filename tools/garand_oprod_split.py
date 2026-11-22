"""GR1: split the operating rod (and its handle) out of the baked one-mesh Garand (content/weapons/models/garand2) as a
'handle' part, so the view model slides it back on each shot (weapons.cpp WP_HANDLE; m1_garand.json adsRecoil.handle_m).
The source m1_garand_asset.glb is not in the repository, so this edits the bake in place: re-run it after any re-import.
The rod is the connected island (vertices welded at 0.01 mm) that holds the handle knob, the only geometry at x < -0.03 m.
Usage: python tools/garand_oprod_split.py content/weapons/models/garand2"""
import json, struct, sys
import numpy as np
from scipy.sparse import coo_matrix
from scipy.sparse.csgraph import connected_components

d = sys.argv[1]
raw = open(d + '/mesh.bin', 'rb').read()
magic, nv, ni = struct.unpack('<3I', raw[:12])
assert magic == 0x31574D4E, 'not a weapon mesh.bin'
vb = raw[12:12 + nv * 48]
P = np.frombuffer(vb, np.float32).reshape(nv, 12)[:, :3]
I = np.frombuffer(raw[12 + nv * 48:12 + nv * 48 + ni * 4], np.uint32).reshape(-1, 3).copy()
M = json.load(open(d + '/model.json'))
assert len(M['parts']) == 1, 'already split (or an unexpected bake): %d parts' % len(M['parts'])
key = np.round(P / 1e-5).astype(np.int64); _, wid = np.unique(key, axis=0, return_inverse=True); T = wid.ravel()[I]
nt = len(T); A = coo_matrix((np.ones(nt * 3), (np.repeat(np.arange(nt), 3), T.ravel()))).tocsr()
_, lab = connected_components(A @ A.T, directed=False)
knob = np.where(P[I].mean(1)[:, 0] < -0.03)[0]
isl = set(lab[knob].tolist()); assert len(isl) == 1, isl
rod = lab == isl.pop()
q = P[I[rod]].reshape(-1, 3)
assert 200 < rod.sum() < 400 and q[:, 2].min() > 0.45 and q[:, 2].max() < 0.75, (rod.sum(), q[:, 2].min(), q[:, 2].max())
body = I[~rod]; I2 = np.concatenate([body, I[rod]])
b0 = M['parts'][0]; pb = P[body].reshape(-1, 3)
body_p = dict(b0, count=int(body.size), bmin=pb.min(0).tolist(), bmax=pb.max(0).tolist())
rod_p = dict(b0, name='operating_rod', role='handle', first=int(body.size), count=int(I[rod].size), bmin=q.min(0).tolist(), bmax=q.max(0).tolist())
M['parts'] = [body_p, rod_p]
M['note_gr1'] = 'operating rod split out as a handle part by tools/garand_oprod_split.py (%d triangles)' % rod.sum()
open(d + '/mesh.bin', 'wb').write(raw[:12] + vb + I2.astype(np.uint32).tobytes() + raw[12 + nv * 48 + ni * 4:])
json.dump(M, open(d + '/model.json', 'w'), indent=1)
print('rod triangles', int(rod.sum()), 'z %.3f..%.3f x %.3f..%.3f' % (q[:, 2].min(), q[:, 2].max(), q[:, 0].min(), q[:, 0].max()))
