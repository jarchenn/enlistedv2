"""CV1: synthesise the Caverns ambience bed (content/audio/ambience/cave.wav, our own sound, no third-party source).

40 s mono, 32 kHz: a low cavern rumble (brown noise, slowly breathing), distant moving water (band-passed noise
swells), and water drips (short rising plinks, random pitch / level / timing) through a long cavern reverb (four
damped feedback combs and two all-passes). The engine cross-fades the loop seam (Audio::setBed).

    <python> -I tools/synth_cave_ambience.py"""
import math
import os
import random
import struct
import wave

SR, SEC = 32000, 40
N = SR * SEC
R = random.Random(19440612)


def onepole(x, a):
    y, out = 0.0, [0.0] * len(x)
    for i, v in enumerate(x):
        y += a * (v - y)
        out[i] = y
    return out


# rumble: brown noise, low-passed twice, breathing over ~11 s
w, b = 0.0, [0.0] * N
for i in range(N):
    w = 0.995 * w + R.uniform(-1, 1) * 0.06
    b[i] = w
rum = onepole(onepole(b, 0.02), 0.03)
rum = [v * (0.75 + 0.25 * math.sin(2 * math.pi * i / (SR * 11.0))) for i, v in enumerate(rum)]

# distant water: band-limited noise (low-pass minus a lower low-pass), slow swells
wn = [R.uniform(-1, 1) for _ in range(N)]
lp1, lp2 = onepole(wn, 0.09), onepole(wn, 0.012)
water = [(a - c) for a, c in zip(lp1, lp2)]
water = [v * (0.35 + 0.65 * (0.5 + 0.5 * math.sin(2 * math.pi * i / (SR * 7.3) + 1.3 * math.sin(2 * math.pi * i / (SR * 17.0))))) for i, v in enumerate(water)]

# drips: rising plinks, 0.5-3 s apart, some in quick pairs
dr = [0.0] * N
t = 0.4
while t < SEC - 0.5:
    f0 = R.uniform(700, 2300)
    amp = R.uniform(0.25, 1.0) * (0.35 if R.random() < 0.5 else 1.0)
    dec = R.uniform(0.025, 0.07)
    s0 = int(t * SR)
    ph = 0.0
    for k in range(int(SR * 0.35)):
        tt = k / SR
        f = f0 * (1.0 + 0.6 * (1 - math.exp(-tt / 0.012)))   # the pitch rises as the drop's cavity closes
        ph += 2 * math.pi * f / SR
        v = math.sin(ph) * math.exp(-tt / dec)
        if k < 40:
            v += R.uniform(-1, 1) * 0.4 * (1 - k / 40)     # the impact click
        if s0 + k < N:
            dr[s0 + k] += v * amp
    t += R.uniform(0.15, 0.4) if R.random() < 0.18 else R.uniform(0.5, 3.0)

# cavern reverb on the drips and some of the water: four damped combs in parallel, two all-passes in series
def comb(x, ms, fb, damp):
    d = int(SR * ms / 1000)
    buf, out, lp, j = [0.0] * d, [0.0] * len(x), 0.0, 0
    for i, v in enumerate(x):
        y = buf[j]
        lp += damp * (y - lp)
        buf[j] = v + lp * fb
        out[i] = y
        j = (j + 1) % d
    return out


def allpass(x, ms, g):
    d = int(SR * ms / 1000)
    buf, out, j = [0.0] * d, [0.0] * len(x), 0
    for i, v in enumerate(x):
        y = buf[j]
        buf[j] = v + y * g
        out[i] = y - v * g
        j = (j + 1) % d
    return out


src = [a + 0.25 * c for a, c in zip(dr, water)]
wet = [0.0] * N
for ms, fb in ((61.3, 0.86), (73.7, 0.85), (89.1, 0.84), (103.9, 0.83)):
    for i, v in enumerate(comb(src, ms, fb, 0.35)):
        wet[i] += v * 0.25
wet = allpass(allpass(wet, 12.7, 0.6), 4.3, 0.6)

mix = [0.55 * a + 0.18 * c + 0.30 * d + 0.55 * e for a, c, d, e in zip(rum, water, dr, wet)]
peak = max(abs(v) for v in mix) or 1.0
g = 0.5 / peak   # -6 dB peak; the bed volume is set in the map (extra.audio.bedVolume)
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "content", "audio", "ambience", "cave.wav")
with wave.open(out, "wb") as f:
    f.setnchannels(1)
    f.setsampwidth(2)
    f.setframerate(SR)
    f.writeframes(b"".join(struct.pack("<h", max(-32767, min(32767, int(v * g * 32767)))) for v in mix))
print("wrote %s (%d s, peak gain %.3f)" % (os.path.normpath(out), SEC, g))
