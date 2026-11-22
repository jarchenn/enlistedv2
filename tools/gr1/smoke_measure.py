# GR10: the reference video's muzzle smoke as an opacity map per frame. Each frame against the last frame before its shot:
# per 24 px tile the base is lined up on the tile's own structure (gradient match, +-10 px about the camera's measured kick),
# then the smoke's opacity is the veil a = (1 - alpha) b + alpha W (W the smoke's grey) fitted over the tile's pixels; the
# rifle / hands (dark in either frame) and the muzzle flash (orange) are left out. Maps: <dir>/smoke/a_<frame>.png (alpha x 255,
# screen space, 1920 x 1080); stats per frame in smoke.json.   python -I smoke_measure.py <garand_ref dir>
import json, sys, numpy as np
from PIL import Image
from scipy import ndimage
R = sys.argv[1]; C = json.load(open(R + '/track2.json')); G = json.load(open(R + '/track.json'))
W = np.array([0.80, 0.80, 0.78], np.float32); T = 24; S = 10
def rgb(i): return np.asarray(Image.open(R + '/frames/f_%04d.png' % i).convert('RGB')).astype(np.float32) / 255
def grad(a): g = a.mean(2); return np.hypot(ndimage.sobel(g, 0), ndimage.sobel(g, 1))
out = []
for shot, base, frames in ((1, 8, range(9, 28)), (2, 27, range(28, 44))):
    b = rgb(base); gb = grad(b); cb = C[base - 1]['cam']
    for i in frames:
        a = rgb(i); ga = grad(a); ca = C[i - 1]['cam']
        gdx = int(round(0.5 * ((ca[0][0] - cb[0][0]) + (ca[1][0] - cb[1][0])))); gdy = int(round(0.5 * ((ca[0][1] - cb[0][1]) + (ca[1][1] - cb[1][1]))))
        H, Wd = a.shape[:2]; amap = np.zeros((H // T, Wd // T), np.float32); wmap = np.zeros_like(amap)
        def gunbox(k):   # the receiver block (its tracked centre and scale; frame 8's box 720-1220 x 470-1080) and everything under it
            g = G[k - 1]['gun']; s_ = g['scale']; return (g['cx'] - 270 * s_, g['cy'] - 190 * s_, g['cx'] + 270 * s_)
        boxes = [gunbox(i), gunbox(base)]
        for ty in range(0, H // T):
            for tx in range(24, 63):   # x 576..1512: the window (the wallpaper margins left out)
                y0, x0 = ty * T, tx * T
                if y0 >= 1000: continue
                if any(bx0 - 12 < x0 + T and x0 < bx1 + 12 and y0 + T > by0 - 12 for bx0, by0, bx1 in boxes): continue   # the rifle (moving): left out
                A = a[y0:y0 + T, x0:x0 + T]; GA = ga[y0:y0 + T, x0:x0 + T]
                dark = A.max(2) < 0.22; flash = (A[..., 0] - A[..., 2]) > 0.18
                if (dark | flash).mean() > 0.4: continue
                best = None
                for dy in range(gdy - S, gdy + S + 1, 2):
                    for dx in range(gdx - S, gdx + S + 1, 2):
                        ys, xs = y0 - dy, x0 - dx
                        if ys < 0 or xs < 0 or ys + T > H or xs + T > Wd: continue
                        e = ((GA - gb[ys:ys + T, xs:xs + T]) ** 2).mean()
                        if best is None or e < best[0]: best = (e, dy, dx)
                if best is None: continue
                Bt = b[y0 - best[1]:y0 - best[1] + T, x0 - best[2]:x0 - best[2] + T]
                ok = ~(dark | flash | (Bt.max(2) < 0.22))
                if ok.sum() < T * T * 0.4: continue
                d = (A - Bt)[ok]; v = (W - Bt)[ok]
                al = float((d * v).sum() / max((v * v).sum(), 1e-6))
                amap[ty, tx] = np.clip(al, 0, 1); wmap[ty, tx] = 1
        vals = amap[wmap > 0]
        if vals.size: amap = np.where(wmap > 0, np.clip(amap - np.percentile(vals, 40), 0, 1), 0)   # the scene-wide lift (exposure after the flash) out
        # fill the masked tiles (the rifle) from their neighbours, smooth, up to full size
        num = ndimage.gaussian_filter(amap * wmap, 1.2); den = ndimage.gaussian_filter(wmap, 1.2); am = np.where(den > 0.05, num / np.maximum(den, 1e-6), 0)
        am[am < 0.04] = 0
        full = np.asarray(Image.fromarray((np.clip(am, 0, 1) * 255).astype(np.uint8)).resize((Wd, H), Image.BICUBIC))
        Image.fromarray(full).save(R + '/smoke/a_%04d.png' % i)
        s = full.astype(np.float32) / 255; tot = s.sum(); yy, xx = np.mgrid[0:H, 0:Wd]
        cx, cy = ((s * xx).sum() / tot - 960, (s * yy).sum() / tot - 540) if tot > 0 else (0, 0)
        out.append(dict(frame=i, shot=shot, age=i - base - 0.5, total=float(tot), cx=float(cx), cy=float(cy), peak=float(s.max())))
        print('shot %d f %2d age %4.1f  opacity sum %8.0f  centre %+5.0f %+5.0f  peak %.2f' % (shot, i, i - base - 0.5, tot, cx, cy, s.max()))
json.dump(out, open(R + '/smoke.json', 'w'), indent=1)
