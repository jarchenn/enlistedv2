"""FSB5 reader for the supplied FMOD banks (complete update A1). Every supplied bank is PCM16 (checked), so samples are
read directly - no FMOD runtime, no third-party decoder.

  python tools/fsb5.py list <bank> [prefix]            index, name, rate, channels, frames, duration, peak, rms, onset
  python tools/fsb5.py extract <manifest.json> <banks dir> <out dir>
        writes every manifest sample as 16-bit WAV (native rate and channels) and records its SHA-256 of the PCM
        in <out dir>/extracted.json; the runtime loads those WAVs by manifest event (src/audio.cpp)

A sample is identified by bank + index + name (names repeat inside a bank); extract refuses a name / index mismatch.
"""
import json, os, struct, sys, hashlib, math

FREQ = {0: 4000, 1: 8000, 2: 11000, 3: 11025, 4: 16000, 5: 22050, 6: 24000, 7: 32000, 8: 44100, 9: 48000, 10: 96000}


def read_bank(path):
    b = open(path, 'rb').read()
    base = b.find(b'FSB5')
    if base < 0: raise ValueError('no FSB5 in ' + path)
    ver, ns, shs, nts, dsz, mode = struct.unpack_from('<IIIIII', b, base + 4)
    if mode != 2: raise ValueError('codec %d is not PCM16' % mode)
    hdr = 0x3C if ver == 1 else 0x40
    p = base + hdr
    samples = []
    for k in range(ns):
        raw = struct.unpack_from('<Q', b, p)[0]; p += 8
        nxt = raw & 1; fi = (raw >> 1) & 0xF; chs = (raw >> 5) & 0x3
        off = ((raw >> 7) & 0x7FFFFFF) << 5; frames = (raw >> 34) & 0x3FFFFFFF
        s = {'index': k, 'rate': FREQ.get(fi, 44100), 'channels': [1, 2, 6, 8][chs], 'offset': off, 'frames': frames, 'loop': None}
        while nxt:
            c = struct.unpack_from('<I', b, p)[0]; p += 4
            nxt = c & 1; size = (c >> 1) & 0xFFFFFF; typ = (c >> 25) & 0x7F
            if typ == 1: s['channels'] = b[p]
            elif typ == 2: s['rate'] = struct.unpack_from('<I', b, p)[0]
            elif typ == 3: s['loop'] = list(struct.unpack_from('<II', b, p))
            p += size
        samples.append(s)
    nt = base + hdr + shs
    for k, s in enumerate(samples):
        o = struct.unpack_from('<I', b, nt + 4 * k)[0]
        e = b.index(b'\0', nt + o); s['name'] = b[nt + o:e].decode('utf-8', 'replace')
    data = base + hdr + shs + nts
    for k, s in enumerate(samples):
        end = samples[k + 1]['offset'] if k + 1 < ns else dsz
        s['bytes'] = (data + s['offset'], data + min(end, s['offset'] + s['frames'] * 2 * s['channels']))
    return b, samples


def stats(b, s):
    a, e = s['bytes']; n = (e - a) // 2
    v = struct.unpack_from('<%dh' % n, b, a)
    ch = s['channels']; peak = max((abs(x) for x in v), default=0) / 32768
    rms = math.sqrt(sum(x * x for x in v) / max(1, n)) / 32768
    # onset: time to reach half the peak; tail: fraction of energy after 0.3 s (loops / tails vs single shots)
    onset = next((i // ch for i, x in enumerate(v) if abs(x) >= peak * 16384), 0) / s['rate']
    cut = int(0.3 * s['rate']) * ch
    late = sum(x * x for x in v[cut:]) / max(1, sum(x * x for x in v))
    return {'dur': s['frames'] / s['rate'], 'peak': round(peak, 3), 'rms': round(rms, 4), 'onset': round(onset, 4), 'late': round(late, 3)}


def wav_name(bank, index):
    # short names (Windows MAX_PATH): the bank tag and the sample index; the manifest keeps the full identity
    return '%s_%d.wav' % (bank.replace('ww2_weapon_', '').replace('ww2_', '').replace('cmn_', '').replace('.assets', ''), index)


def write_wav(path, b, s):
    a, e = s['bytes']; pcm = b[a:e]
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 36 + len(pcm)) + b'WAVEfmt ' + struct.pack('<IHHIIHH', 16, 1, s['channels'], s['rate'], s['rate'] * 2 * s['channels'], 2 * s['channels'], 16))
        f.write(b'data' + struct.pack('<I', len(pcm)) + pcm)
    return hashlib.sha256(pcm).hexdigest()


if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'list':
        b, ss = read_bank(sys.argv[2]); pre = sys.argv[3] if len(sys.argv) > 3 else ''
        for s in ss:
            if not s['name'].startswith(pre): continue
            st = stats(b, s)
            print('%5d %-44s %5d Hz %dch %6.3f s peak %.3f rms %.4f onset %.3f late %.2f%s' % (s['index'], s['name'], s['rate'], s['channels'], st['dur'], st['peak'], st['rms'], st['onset'], st['late'], ' loop %s' % s['loop'] if s['loop'] else ''))
    elif cmd == 'extract':
        man = json.load(open(sys.argv[2], encoding='utf-8')); bdir, out = sys.argv[3], sys.argv[4]
        os.makedirs(out, exist_ok=True); cache = {}; rec = {}
        for ev, spec in man['events'].items():
            for v in spec['variants']:
                bank = v['bank']
                if bank not in cache: cache[bank] = read_bank(os.path.join(bdir, bank + '.assets.bank'))
                b, ss = cache[bank]; s = ss[v['index']]
                if s['name'] != v['name']: raise SystemExit('%s: index %d is %s, manifest says %s' % (ev, v['index'], s['name'], v['name']))
                fn = wav_name(bank, v['index'])
                h = write_wav(os.path.join(out, fn), b, s)
                rec[fn] = {'sha256': h, 'rate': s['rate'], 'channels': s['channels'], 'frames': s['frames'], **stats(b, s)}
        json.dump(rec, open(os.path.join(out, 'extracted.json'), 'w', encoding='utf-8'), indent=1)
        print('extracted %d samples' % len(rec))
