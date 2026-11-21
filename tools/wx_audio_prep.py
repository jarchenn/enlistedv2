"""Weather sound (W1 presets; weather_audio.cpp): CC0 freesound previews -> content/audio/custom/wx/.
Rain beds: trimmed and levelled to a target RMS, the loop seam cross-faded by Audio's loopBuf. Thunder: each clap cut from
its first loud onset with its tail, peak-levelled. Sources and licences: docs/credits.md.
    <python> -I tools/wx_audio_prep.py <raw dir> <out dir>"""
import os
import sys
import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

RAW, OUT = sys.argv[1], sys.argv[2]
SR = 44100
os.makedirs(OUT, exist_ok=True)


def load(name):
    x, sr = sf.read(os.path.join(RAW, name + ".mp3"), dtype="float32", always_2d=True)
    if sr != SR:
        x = resample_poly(x, SR, sr, axis=0).astype(np.float32)
    return x if x.shape[1] == 2 else np.repeat(x, 2, 1)


def save(name, x):
    x = np.clip(x, -0.98, 0.98)
    p = os.path.join(OUT, name + ".mp3")
    sf.write(p, x, SR, format="MP3", subtype="MPEG_LAYER_III")
    print(f"{name:16s} {len(x) / SR:6.1f} s  {os.path.getsize(p) // 1024} KB")


def level(x, rms_db):
    return x * (10 ** (rms_db / 20) / (np.sqrt((x ** 2).mean()) + 1e-9))


def fade(x, s_in, s_out):
    n_in, n_out = int(s_in * SR), int(s_out * SR)
    if n_in:
        x[:n_in] *= np.linspace(0, 1, n_in)[:, None]
    if n_out:
        x[-n_out:] *= np.linspace(1, 0, n_out)[:, None]
    return x


# rain beds: (out, raw, RMS dBFS, skip s, keep s)
for out, raw, db, skip, keep in [("rain_light", "rain_light", -24, 2, 60), ("rain_heavy", "rain_heavy", -20, 2, 60),
                                  ("rain_roof", "rain_roof", -24, 1, 60)]:
    x = load(raw)
    x = x[int(skip * SR): int((skip + keep) * SR)]
    save(out, level(x, db))

# thunder: from 0.15 s before the first frame over 30 % of the peak, up to `keep` s, faded out over the last third
for out, raw, keep in [("thunder_near1", "thunder_near1", 9), ("thunder_near2", "thunder_near2", 8), ("thunder_far1", "thunder_far1", 12)]:
    x = load(raw)
    env = np.abs(x).max(1)
    on = int(np.argmax(env > 0.3 * env.max()))
    x = x[max(0, on - int(0.15 * SR)): on + int(keep * SR)].copy()
    x = fade(x, 0.01, len(x) / SR / 3)
    save(out, x * (0.9 / (np.abs(x).max() + 1e-9)))
