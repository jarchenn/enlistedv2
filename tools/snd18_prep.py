import sys, os, glob
sys.path += [r'C:\Users\50bmg\AppData\Roaming\Python\Python312\site-packages', r'C:\Users\50bmg\AppData\Local\Programs\Python\Python312\Lib\site-packages']
# SND18: the per-gun pack -> content/audio/custom/snd18/: <gun>_single.mp3 (lead-in trimmed) and <gun>_auto_<n>.mp3, the distinct
# rounds of the 5-round burst, each from 2 ms before its onset to 3 ms before the next round, cross-faded (8 ms, level-matched)
# into the single's ring-out at the same offset, so each variant ends on its own and the last round of a burst rings out.
# Prints per gun the level the game's shot pipeline would see (peak to 0.95, rms of the first 60 ms) for the gain table.
import os, glob, json
import numpy as np, soundfile as sf
D, OUT = sys.argv[1], sys.argv[2]; SR = 44100
os.makedirs(OUT, exist_ok=True)
sp = {}
for l in open(D + '/sources.txt'):
    p = l.split()
    if 'ms' in p: i = p.index('ms'); sp[p[0]] = int(p[i - 1])
def rd(f):
    x, sr = sf.read(f, always_2d=True); assert sr == SR
    return x if x.shape[1] == 2 else np.repeat(x[:, :1], 2, 1)
def onset(x, lo=0, hi=None):
    m = np.abs(x[lo:hi]).max(1); return lo + int(np.argmax(m > 0.3 * m.max()))
def lvl(x):   # the pipeline's view: peak to 0.95, mono rms of the first 60 ms
    m = x.mean(1); m = m * 0.95 / (np.abs(x).max() + 1e-9); return 20 * np.log10(np.sqrt(np.mean(m[:int(0.06 * SR)] ** 2)) + 1e-9)
def fin(s, maxlen):
    s = s[:int(maxlen * SR)].copy(); fo = int(0.05 * SR); s[-fo:] *= np.linspace(1, 0, fo)[:, None]
    fi = int(0.001 * SR); s[:fi] *= np.linspace(0, 1, fi)[:, None]
    return s * (10 ** (-1 / 20) / (np.abs(s).max() + 1e-9))
def corr(a, b):
    a = a.mean(1); b = b.mean(1); a = a / (np.linalg.norm(a) + 1e-9); b = b / (np.linalg.norm(b) + 1e-9)
    return np.abs(np.correlate(np.pad(a, (150, 150)), b, 'valid')).max()
pre, fx = int(0.002 * SR), int(0.008 * SR); rep = {}
for g in sorted(sp):
    x = rd(D + '/single/%s_single.mp3' % g); a = max(0, onset(x) - pre); single = x[a:]
    s = fin(single, 3.0); sf.write(OUT + '/%s_single.mp3' % g, s, SR, format='MP3', subtype='MPEG_LAYER_III', compression_level=0.1)
    b = rd(D + '/burst_5_rounds/%s_burst5.mp3' % g); S = sp[g] * SR // 1000; a0 = onset(b); ons = []
    for k in range(5):
        c = a0 + k * S; ons.append(onset(b, max(0, c - 660), c + 660))
    L = min(int(0.6 * S), 2646); keep = []
    for i in range(5):
        if all(corr(b[max(0, ons[i] - pre):max(0, ons[i] - pre) + L], b[max(0, ons[j] - pre):max(0, ons[j] - pre) + L]) < 0.93 for j in keep): keep.append(i)
    vl = []
    if sp[g] < 1000:   # automatic guns only (launchers / bolt / pump: the single)
        for n, i in enumerate(keep, 1):
            nxt = ons[i + 1] if i + 1 < 5 else ons[i] + S
            head = b[max(0, ons[i] - pre):nxt - int(0.003 * SR)]; H = len(head)
            r = single[H - fx:]
            if len(r) <= fx: continue
            gm = min(4.0, (np.sqrt(np.mean(head[-fx:] ** 2)) + 1e-9) / (np.sqrt(np.mean(r[:fx] ** 2)) + 1e-9))
            w = np.linspace(0, 1, fx)[:, None]
            v = np.concatenate([head[:-fx], head[-fx:] * (1 - w) + r[:fx] * gm * w, r[fx:] * gm])
            v = fin(v, 2.6); sf.write(OUT + '/%s_auto_%d.mp3' % (g, n), v, SR, format='MP3', subtype='MPEG_LAYER_III', compression_level=0.1); vl.append(lvl(v))
    rep[g] = {'single_db': round(lvl(s), 2), 'auto_n': len(vl), 'auto_db': round(float(np.mean(vl)), 2) if vl else None, 'lead_ms': round(1000 * a / SR, 1), 'spacing_ms': sp[g]}
    print('%-21s lead %5.1f ms  single %6.2f dB  auto %d %s' % (g, 1000 * a / SR, rep[g]['single_db'], len(vl), rep[g]['auto_db']))
json.dump(rep, open(OUT + '/../snd18_levels.json', 'w'), indent=1)
