# comparison frames: reference | ours (grid = our camera kick) / 50 % overlay of the sights | curves with a time cursor
import json, math, glob
import numpy as np
from PIL import Image, ImageDraw
R = 'E:/nmk/garand_ref'
O = json.load(open(R + '/ours/ours.json')); CU = json.load(open(R + '/curves.json'))
BG = np.array([120, 135, 150])
FW = 540 / math.tan(math.radians(29))
# curve panel base: measured (two shots averaged) vs ours, per channel
def panel(i):
    im = Image.new('RGB', (960, 540), (20, 22, 26)); d = ImageDraw.Draw(im)
    chans = [('back_m', 'back (mm)', 1000, 40), ('pitch_deg', 'muzzle pitch (deg)', 1, 3.5), ('cam_pitch_deg', 'camera pitch (deg)', 1, 2.5)]
    for r, (k, name, mul, rng) in enumerate(chans):
        y0 = 20 + r * 175; h = 150; x0, x1 = 50, 940
        d.rectangle([x0, y0, x1, y0 + h], outline=(70, 70, 80)); d.text((x0 + 6, y0 + 4), name, fill=(220, 220, 220))
        zy = y0 + h / 2; d.line([x0, zy, x1, zy], fill=(60, 60, 70))
        X = lambda f: x0 + (f - 1) / 42 * (x1 - x0); Y = lambda v: zy - v * mul / rng * (h / 2 - 6)
        pts = [(X(o['frame']), Y(o[k])) for o in O]; d.line(pts, fill=(90, 200, 255), width=3)
        key = {'back_m': 'back', 'pitch_deg': 'pitch', 'cam_pitch_deg': 'camP'}[k]
        for n, u in enumerate((9, 29)):   # the measured reference, each shot on its own frames
            for kk, row in enumerate(CU):
                vals = row['raw'].get(key, [])
                if len(vals) > n: d.ellipse([X(u + kk) - 3, Y(vals[n]) - 3, X(u + kk) + 3, Y(vals[n]) + 3], fill=(255, 170, 60))
        d.line([X(i), y0, X(i), y0 + h], fill=(255, 255, 255))
    d.text((50, 528), 'orange: measured in the video   blue: ours (engine curve)', fill=(200, 200, 200))
    return im
out = R + '/cmp'
import os; os.makedirs(out, exist_ok=True)
for o in O:
    i = o['frame']
    ref = Image.open(R + '/frames/f_%04d.png' % i).convert('RGB')
    oi = Image.open(R + '/ours/o_%04d.png' % i).convert('RGBA'); bgi = Image.new('RGBA', oi.size, (120, 135, 150, 255)); bgi.alpha_composite(oi)
    ours = np.asarray(bgi.convert('RGB')).astype(np.int16)
    mask = (np.abs(ours - BG).sum(2) > 6)
    # ours over a grid that moves with our camera kick (pitch up = the scene down, roll = the picture clockwise)
    g = Image.new('RGB', (1920, 1080), (120, 135, 150)); gd = ImageDraw.Draw(g)
    dy = FW * math.tan(math.radians(o['cam_pitch_deg'])); a = math.radians(o['cam_roll_deg'])
    for k in range(-20, 21):
        for (x, y, x2, y2) in ((-2000, 540 + k * 100, 4000, 540 + k * 100), (960 + k * 100, -2000, 960 + k * 100, 3000)):
            def tr(px, py):
                px, py = px - 960, py - 540 + dy
                return 960 + px * math.cos(a) - py * math.sin(a), 540 + px * math.sin(a) + py * math.cos(a)
            gd.line([tr(x, y), tr(x2, y2)], fill=(150, 165, 180) if k else (230, 120, 120), width=2)
    gn = np.asarray(g).copy(); gn[mask] = ours[mask]
    oursI = Image.fromarray(gn.astype(np.uint8))
    blend = np.asarray(ref).copy(); blend[mask] = (0.5 * blend[mask] + 0.5 * ours[mask]).astype(np.uint8)
    F = Image.new('RGB', (1920, 1080))
    F.paste(ref.resize((960, 540)), (0, 0)); F.paste(oursI.resize((960, 540)), (960, 0))
    F.paste(Image.fromarray(blend).crop((480, 270, 1440, 810)), (0, 540)); F.paste(panel(i), (960, 540))
    d = ImageDraw.Draw(F)
    for (x, y, t) in ((8, 6, 'reference video  frame %d' % i), (968, 6, 'ours: engine ADS pose + GR1 curve (grid = our camera kick)'), (8, 546, '50 %% overlay, sights at 1:1 (frame %d)' % i)):
        d.rectangle([x - 4, y - 2, x + 7 * len(t), y + 14], fill=(0, 0, 0)); d.text((x, y), t, fill=(255, 230, 120))
    F.save(out + '/c_%04d.png' % i)
print('composed', len(O))
