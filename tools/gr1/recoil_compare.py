# GR14: the Garand's shot in the game against the approved take, per channel, three shots 0.667 s apart (the take's spacing):
# the take (the fireCurve rows summed, as arms_full.py), the game before (hip: the random climb + the fxrecoil spring every
# rifle shares; ADS in the 197a687b build with the current local json: the rig got no curve, only the camera's kick) and
# after (ads false: the curve at the hip and in ADS, nothing else), from the same rows and the engine's formulas.
#   python -I recoil_compare.py <m1_garand.json> <out.png>
import json, math, sys
import numpy as np
from PIL import Image, ImageDraw
W = json.load(open(sys.argv[1], encoding='utf-8')); R = np.array(W['fireCurve']['frames'], float); FT = np.arange(len(R)) / W['fireCurve']['fps']
SH = [0.0, 0.667, 1.333]; T = np.arange(0, 2.6, 1 / 60)
def row(a): return np.zeros(10) if a <= 0 or a >= FT[-1] else np.array([np.interp(a, FT, R[:, k]) for k in range(10)])
take = np.array([sum((row(t - s) for s in SH if t > s), np.zeros(10)) for t in T])
# the shared rifle recoil (weapons.cpp, fxrecoil on): per shot the aim climbs recoil.pitch (x0.85-1.15; mean shown) and stays;
# the view model's spring (fxdata small_arms_spring 'carbine': 6 Hz, zeta 0.5) kicked so its first peak is vmRecoil rifle's back_m / pitch_deg
fn, z = 6.0, 0.5; w = 2 * math.pi * fn; peak = math.exp(-z * math.atan2(math.sqrt(1 - z * z), z) / math.sqrt(1 - z * z))
def spring(amp):
    x = v = 0.0; out = []; h = 1 / 240; k = 0
    for t in np.arange(0, 2.6, h):
        while k < len(SH) and t >= SH[k]: v += amp * w / peak; k += 1
        v += (-w * w * x - 2 * z * w * v) * h; x += v * h; out.append(x)
    return np.array(out[::4][:len(T)])
before_hip = {'cam': np.array([W['recoil']['pitch'] * sum(1 for s in SH if t > s) for t in T]), 'back': np.minimum(spring(0.11), 0.13) * 1000, 'pitch': spring(1.05)}
CH = [('camera pitch (deg)', 'cam', 4, 1.0), ('rifle back toward the eye (mm)', 'back', 0, 1000.0), ('muzzle pitch (deg)', 'pitch', 6, 1.0)]
Wd, Hp = 1500, 300; img = Image.new('RGB', (Wd, Hp * 3 + 70), (250, 250, 250)); d = ImageDraw.Draw(img)
for i, (title, key, col, mul) in enumerate(CH):
    y0 = 10 + i * Hp; x0, x1 = 60, Wd - 20; h = Hp - 40
    series = [('the approved take', take[:, col] * mul, (0, 0, 0), 3), ('game before, hip (the shared recoil)', before_hip[key], (220, 40, 40), 2),
              ('game before, ADS in your build (no rifle motion)', take[:, col] * mul if key == 'cam' else np.zeros(len(T)), (240, 150, 0), 2),
              ('game now, hip and ADS', take[:, col] * mul, (40, 110, 230), 2)]
    lo = min(s.min() for _, s, _, _ in series); hi = max(s.max() for _, s, _, _ in series); lo, hi = min(lo, 0), hi + 1e-6
    X = lambda t: x0 + t / T[-1] * (x1 - x0); Y = lambda v: y0 + 20 + h - (v - lo) / (hi - lo) * h
    d.rectangle([x0, y0 + 20, x1, y0 + 20 + h], outline=(180, 180, 180)); d.text((x0, y0 + 4), title, fill=(0, 0, 0))
    d.line([X(0), Y(0), X(T[-1]), Y(0)], fill=(200, 200, 200))
    for s in SH: d.line([X(s), y0 + 20, X(s), y0 + 20 + h], fill=(225, 225, 225))
    for k in range(int(lo) - 1, int(hi) + 2, max(1, int((hi - lo) / 5) or 1)):
        if lo <= k <= hi: d.text((8, Y(k) - 6), str(k), fill=(90, 90, 90))
    for n, (lab, s, c, wd) in enumerate(series):
        pts = [(X(t), Y(v)) for t, v in zip(T, s)]
        if n == 3: pts = [(x, y + 0) for x, y in pts]; [d.line(pts[k:k + 2], fill=c, width=wd) for k in range(0, len(pts) - 1, 2)]
        else: d.line(pts, fill=c, width=wd)
y = Hp * 3 + 15; x = 60
for lab, c in (('the approved take', (0, 0, 0)), ('game before, hip (the shared recoil)', (220, 40, 40)), ('game before, ADS in your build (no rifle motion)', (240, 150, 0)), ('game now, hip and ADS (dashed: the take exactly)', (40, 110, 230))):
    d.rectangle([x, y + 4, x + 24, y + 10], fill=c); d.text((x + 30, y), lab, fill=(0, 0, 0)); x += 360
img.save(sys.argv[2])
def pk(a): return float(np.max(a))
print('peaks per shot  take: camera %.2f deg (returns), back %.1f mm, muzzle %.2f deg' % (pk(take[:41, 4]), pk(take[:41, 0]) * 1000, pk(take[:41, 6])))
print('before at hip: camera +%.2f deg per shot (stays: %.1f after 3), back %.1f mm, muzzle %.2f deg' % (W['recoil']['pitch'], before_hip['cam'][-1], pk(before_hip['back'][:41]), pk(before_hip['pitch'][:41])))
print('game now = the take by construction (the same rows and formula: fireCurveSum, gain 1 at the hip and in ADS); the in-game capture is the check')
