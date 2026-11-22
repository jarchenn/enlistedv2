"""GR4 (the user): taller sights on the baked Garand, as on the reference video's rifle: the peep ring and the front blade
6.5 mm higher over the barrel, so the barrel and handguard stay >= 28 px under the hole's centre at the ADS zoom (the
reference's clearance; ours was ~13 px). The peep (its own island: ring and slide) rises with its slide stretched from
its foot; the front sight rises above its barrel clamp, stretched between. Move m1_garand.json sights.rear / front up by
the same RAISE so the sight line keeps its angle. Run after garand_oprod_split.py, garand_peep_thin.py and
garand_front_sight.py (they find the sights at the old height); idempotent (model.json gr4_raise).
Usage: python tools/garand_sight_raise.py content/weapons/models/garand2"""
import json, struct, sys
import numpy as np
from scipy.sparse import coo_matrix
from scipy.sparse.csgraph import connected_components
RAISE = 0.0065
d = sys.argv[1]; M = json.load(open(d + '/model.json'))
if M.get('gr4_raise'): print('already raised'); sys.exit(0)
raw = bytearray(open(d + '/mesh.bin', 'rb').read())
magic, nv, ni = struct.unpack('<3I', raw[:12]); assert magic == 0x31574D4E
V = np.frombuffer(bytes(raw[12:12 + nv * 48]), np.float32).reshape(nv, 12).copy(); P = V[:, :3]
I = np.frombuffer(bytes(raw[12 + nv * 48:12 + nv * 48 + ni * 4]), np.uint32).reshape(-1, 3)
cx, cy = -0.0005, 0.0956   # the peep centre before (sights.rear)
key = np.round(P / 1e-5).astype(np.int64); _, wid = np.unique(key, axis=0, return_inverse=True); T = wid.ravel()[I]
nt = len(T); A = coo_matrix((np.ones(nt * 3), (np.repeat(np.arange(nt), 3), T.ravel()))).tocsr(); _, lab = connected_components(A @ A.T, directed=False)
c = P[I].mean(1); rt = np.where((np.hypot(c[:, 0] - cx, c[:, 1] - cy) < 0.0045) & (c[:, 2] > 0.36) & (c[:, 2] < 0.39))[0]
isl = set(lab[rt].tolist()); assert len(isl) == 1, isl
peep = np.zeros(nv, bool); peep[np.unique(I[lab == isl.pop()])] = True
foot, rb = P[peep, 1].min(), cy - 0.0042                                # the slide's foot; the ring's bottom
assert 0.083 < foot < 0.087 and P[peep, 2].min() > 0.365 and P[peep, 2].max() < 0.375, (foot, P[peep, 2].min(), P[peep, 2].max())
V[peep, 1] += RAISE * np.clip((P[peep, 1] - foot) / (rb - foot), 0, 1)
front = (P[:, 2] > 1.06) & (P[:, 2] < 1.081) & (P[:, 1] > 0.045) & (np.abs(P[:, 0] + 0.0004) < 0.011)
V[front, 1] += RAISE * np.clip((P[front, 1] - 0.070) / 0.015, 0, 1)
raw[12:12 + nv * 48] = V.astype(np.float32).tobytes(); open(d + '/mesh.bin', 'wb').write(raw)
M['gr4_raise'] = 'sights raised %.1f mm by tools/garand_sight_raise.py' % (RAISE * 1000)
for p in M['parts']:   # the part bounds grow with the taller sights
    tri = np.frombuffer(bytes(raw[12 + nv * 48:12 + nv * 48 + ni * 4]), np.uint32)[p['first']:p['first'] + p['count']]
    q = V[np.unique(tri), :3]; p['bmin'] = q.min(0).tolist(); p['bmax'] = q.max(0).tolist()
M['bmax'] = np.max([p['bmax'] for p in M['parts']], 0).tolist()
json.dump(M, open(d + '/model.json', 'w'), indent=1)
print('peep %d verts, front %d verts raised %.1f mm' % (peep.sum(), front.sum(), RAISE * 1000))
