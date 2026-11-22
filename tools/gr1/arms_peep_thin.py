# GR7 (the user): the arms rig rifle's rear peep (gar.blend: Realistic M1 Garand, mesh Garand001_03) rebuilt as a perfect
# ring like the reference video's: the source ring's outside is not centred on its pinhole (0.14 radius at the top, 0.21 at
# the stem), so a circle is fitted to the outside away from the stem and every ring vertex is snapped onto two exact circles
# about that centre: the outside 0.158 units (the reference's 84 px at the game's ADS zoom and eye relief), the hole 0.69 of
# it; the stem's top eases onto the new outside. Writes the new centre (Garand001 local) to <out>.peep.json for the ADS
# camera (arms_ads.py / arms_full.py). Saves a copy (the BG4 file is left as it is).
#   blender -b --factory-startup --python arms_peep_thin.py -- <gar.blend> <out.blend>
import bpy, sys, math, json
import numpy as np
from mathutils import Vector
A = sys.argv[sys.argv.index('--') + 1:]; SRC, DST = A[0], A[1]
RO2 = 0.158; RI2 = 0.69 * RO2; STEM = 35.0; FADE = 0.06
bpy.ops.wm.open_mainfile(filepath=SRC)
me = bpy.data.objects['Garand001_03 - Default_0']; Mg = bpy.data.objects['Garand001'].matrix_world.inverted() @ me.matrix_world; Mi = Mg.inverted()
V = np.array([tuple(Mg @ v.co) for v in me.data.vertices])   # Garand001 local: muzzle -z, up -y (the stem below: +y)
h0 = (-0.0031, -9.1615)                                       # the pinhole's centre (its innermost vertices)
near = (V[:, 2] > 8.22) & (V[:, 2] < 8.40) & (np.hypot(V[:, 0] - h0[0], V[:, 1] - h0[1]) < 0.26)
r0 = np.hypot(V[:, 0] - h0[0], V[:, 1] - h0[1]); a0 = np.degrees(np.arctan2(V[:, 1] - h0[1], V[:, 0] - h0[0]))
fit = near & (r0 > 0.1) & (np.abs(a0 - 90) > STEM + 15)      # the outside, away from the stem
X, Y = V[fit, 0], V[fit, 1]
Am = np.column_stack([2 * X, 2 * Y, np.ones_like(X)]); b = X * X + Y * Y
(cx, cy, c), *_ = np.linalg.lstsq(Am, b, rcond=None); R = math.sqrt(c + cx * cx + cy * cy)
res = np.hypot(X - cx, Y - cy) - R
print('FIT centre %.4f %.4f radius %.4f (residual rms %.4f, %d verts)' % (cx, cy, R, np.sqrt((res ** 2).mean()), fit.sum()))
r = np.hypot(V[:, 0] - cx, V[:, 1] - cy); a = np.degrees(np.arctan2(V[:, 1] - cy, V[:, 0] - cx))
inner = near & (r0 < 0.1)                                     # the pinhole's walls
stem = near & ~inner & (np.abs(a - 90) < STEM) & (r > R + 0.004)
outer = near & ~inner & ~stem
nr = r.copy(); nr[inner] = RI2; nr[outer] = RO2
w = np.clip(1 - (r[stem] - R) / FADE, 0, 1); nr[stem] = r[stem] - (R - RO2) * w
mv = inner | outer | stem
dx, dy = V[mv, 0] - cx, V[mv, 1] - cy; rr = np.maximum(r[mv], 1e-9)
V2 = V.copy(); V2[mv, 0] = cx + dx / rr * nr[mv]; V2[mv, 1] = cy + dy / rr * nr[mv]
for i in np.where(mv)[0]: me.data.vertices[i].co = Mi @ Vector(V2[i])
me.data.update()
chk = np.hypot(V2[inner | outer, 0] - cx, V2[inner | outer, 1] - cy)
print('RING hole %.4f outside %.4f (hole / outside %.2f); radii now %.4f..%.4f / %.4f..%.4f; %d inner, %d outer, %d stem verts' % (RI2, RO2, RI2 / RO2,
      chk[chk < 0.13].min(), chk[chk < 0.13].max(), chk[chk >= 0.13].min(), chk[chk >= 0.13].max(), inner.sum(), outer.sum(), stem.sum()))
json.dump({'centre': [float(cx), float(cy)], 'outside': RO2, 'hole': RI2}, open(DST + '.peep.json', 'w'))
bpy.ops.wm.save_as_mainfile(filepath=DST, copy=True)
