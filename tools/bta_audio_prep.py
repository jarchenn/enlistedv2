"""BTA: Blackwater Thaw sounds (CC0, freesound previews in E:/nmk/incoming_bta/raw) -> content/audio/custom/bta/.
Loops: trimmed, levelled to a target RMS (point sources mono). Footsteps: the clearest single steps re-cut and joined with
silence, so Audio::loadCustom's onset cutter finds one step per onset. Creaks: each its own file, peak-levelled.
    <python> -I tools/bta_audio_prep.py <raw dir> <out dir>"""
import sys, os
import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

RAW, OUT = sys.argv[1], sys.argv[2]
SR = 44100

def load(name, mono):
    x, sr = sf.read(os.path.join(RAW, name + ".mp3"), dtype="float32", always_2d=True)
    if sr != SR: x = resample_poly(x, SR, sr, axis=0).astype(np.float32)
    return x.mean(1, keepdims=True) if mono else (x if x.shape[1] == 2 else np.repeat(x, 2, 1))

def save(name, x):
    x = np.clip(x, -0.98, 0.98)
    sf.write(os.path.join(OUT, name + ".mp3"), x, SR, format="MP3", subtype="MPEG_LAYER_III")
    print(f"{name:18s} {len(x) / SR:6.1f} s {x.shape[1]} ch  {os.path.getsize(os.path.join(OUT, name + '.mp3')) // 1024} KB")

def level(x, rms_db):
    r = np.sqrt((x ** 2).mean()) + 1e-9
    return x * (10 ** (rms_db / 20) / r)

# loops: (out name, raw, mono, target RMS dBFS, keep seconds)
for out, raw, mono, db, keep in [("wind_low", "wind_low", False, -24, 75), ("wind_ridge", "wind_ridge", False, -22, 113),
                                 ("creek", "creek", True, -20, 59), ("flume", "flume", True, -22, 11),
                                 ("drain", "drain", True, -24, 18), ("fire", "fire", True, -22, 80)]:
    x = load(raw, mono)[: int(keep * SR)]
    save(out, level(x, db))

for k in range(1, 5):   # creaks
    x = load("creak%d" % k, True); save("creak_%d" % k, x * (0.5 / (np.abs(x).max() + 1e-9)))

def steps(raw, out, n=14, gap=0.38, clip=0.40):
    x = load(raw, True)[:, 0]
    W = SR // 100
    env = np.abs(x[: len(x) // W * W]).reshape(-1, W).max(1)
    env = np.convolve(env, np.ones(3) / 3, "same")
    thr = 0.3 * np.percentile(env, 99.5)
    on = []
    for k in range(1, len(env)):
        if env[k] > thr and env[k - 1] <= thr and (not on or k - on[-1] >= gap * 100): on.append(k)
    on = sorted(on, key=lambda k: -env[k:k + 10].max())[:n]   # the clearest steps
    parts, fl = [], int(0.04 * SR)
    for k in sorted(on):
        a = max(0, k * W - int(0.015 * SR)); s = x[a: a + int(clip * SR)].copy()
        s[-fl:] *= np.linspace(1, 0, fl); s *= 0.5 / (np.abs(s).max() + 1e-9)
        parts += [s, np.zeros(int(0.55 * SR), np.float32)]
    y = np.concatenate([np.zeros(int(0.3 * SR), np.float32)] + parts)
    # check: the game's cutter (10 ms windows, > 0.3 peak after a quiet window, 0.25 s apart) sees one onset per step
    e = np.abs(y[: len(y) // W * W]).reshape(-1, W).max(1); pk = e.max(); got = []
    for k in range(1, len(e)):
        if e[k] > 0.3 * pk and e[k - 1] <= 0.3 * pk and (not got or k - got[-1] >= 25): got.append(k)
    print(f"  {out}: {len(on)} steps cut, the game's cutter finds {len(got)}")
    save(out, y[:, None])

for raw, out in [("steps_snow", "steps_snow"), ("steps_snow_deep", "steps_snow_deep"), ("steps_slush", "steps_slush"),
                 ("steps_mud", "steps_mud"), ("steps_shallow", "steps_shallow")]:
    steps(raw, out)
