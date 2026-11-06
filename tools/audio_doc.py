"""Writes docs/audio.md from content/audio/manifest.json (+ content/audio/samples/extracted.json when present)."""
import json, os, sys
sys.path.insert(0, os.path.dirname(__file__))
import fsb5
ROOT = os.path.join(os.path.dirname(__file__), '..')
m = json.load(open(os.path.join(ROOT, 'content/audio/manifest.json'), encoding='utf-8'))
xp = os.path.join(ROOT, 'content/audio/samples/extracted.json')
x = json.load(open(xp, encoding='utf-8')) if os.path.exists(xp) else {}
L = ['# Audio (complete update A1)', '',
     '**Banks and decoding.**',
     '- All 12 supplied FMOD banks were read. Every one is FSB5 with PCM16 samples, so `tools/fsb5.py` reads them directly: no FMOD runtime and no third-party decoder.',
     '- Rates (44.1 / 48 kHz), channels, frame counts and loop chunks come from the FSB5 sample headers.',
     '- At load, the runtime downmixes each sample to mono and resamples it to the 44.1 kHz engine rate.', '',
     '**Pipeline.**',
     '- `tools/audio_manifest.py <banks dir>[;<dir2>]` writes `content/audio/manifest.json`, a deterministic selection by family name.',
     '- `tools/fsb5.py extract content/audio/manifest.json <banks dir>[;<dir2>] content/audio/samples` converts the selection to 16-bit PCM WAV and records each SHA-256 (of the PCM) in `samples/extracted.json`. Several bank folders can be given, separated by `;`.',
     '- `src/audio.cpp` loads the events at start-up.', '',
     '**Bigote Sound mod V9 (2026-11-06).**',
     '- The user re-supplied the pack as two zips (Part 1: 5 banks, Part 2: 7 banks). Each was extracted into its own empty folder and only read as data.',
     '- All 12 banks are byte-identical to the banks already mapped, so the earlier events are unchanged.',
     '- This pass filled every remaining slot the game plays: far layers, tank guns, generic foley, the bolt cycle, the smoke pop and wading steps.',
     '- To rebuild the samples from the two V9 folders:', '',
     '```', 'python -I tools/audio_manifest.py "<part1 dir>;<part2 dir>"', 'python -I tools/fsb5.py extract content/audio/manifest.json "<part1 dir>;<part2 dir>" content/audio/samples', '```', '',
     '**Redistribution: not cleared.** The banks are from an Enlisted sound mod, and no reuse permission was supplied.',
     '- `content/audio/samples/` is git-ignored and must be extracted locally from your own copy of the banks.',
     '- Only the manifest and the tools are in the repository.',
     '- Without the samples, every event falls back to the existing procedural sounds.', '',
     '**Not auditioned by a person.**',
     '- Roles were chosen from the bank\'s family names and checked against each sample\'s measured envelope:',
     '  - shot bodies: sharp onset (≤ 0.05 s);',
     '  - tails: energy after 0.3 s;',
     '  - reload foley: short.',
     '- Each mapping carries a status:',
     '  - **named**: the family name matches the weapon;',
     '  - **approx**: a named stand-in for an absent recording;',
     '  - **provisional**: no recording of that weapon exists in the banks.',
     '- Please listen before relying on the mapping. Everything is replaceable in `tools/audio_manifest.py`.', '',
     '## Playback',
     '- **Variations.** Each event has several variations and never plays the same one twice running.',
     '- **Far layer.** Events with a `_far` layer use it beyond 90 m: every firearm, launcher, gun station and tank gun, and the explosions.',
     '  - Named distant recordings where the bank has them (Kar98k / ZF39, Thompson, MP 40, Panzerschreck, Flak); otherwise the bank\'s generic distant rifle, automatic or cannon shots.',
     '  - Tails have far layers for the Thompson, MP 40 and MG 42.',
     '- **Bolt actions.** After each Kar98k / ZF39 shot the bolt is worked: open at 25 % and close at 70 % of the bolt time (`bolt`, `bolt_close`). Single rounds load with `clip_in`; the Kar98k\'s stripper clip with `clip_kar98k`.',
     '- **MG 42 embrasure mount.** Plays the handheld MG 42\'s events: shot bodies, the tail once on release, the belt reload (`reload_mg42`).',
     '- **Smoke grenade.** Ignites with `smoke_pop` (no blast).',
     '- **Footsteps.** Below the water level the player wades with `step_water`: one recorded stride per step, and the previous stride\'s water tail fades out over 250 ms. Dry ground keeps the procedural grass / stone / wood steps, because the pack has no dry-ground footsteps.',
     '- **Automatic fire.**',
     '  - One shot body plays per fired round, at the game\'s real cadence, and a new body of the same gun fades the previous one out in 40 ms, so bursts do not stack or clip.',
     '  - The recorded tail plays once when the trigger is released after a burst.',
     '  - Measured: a 1 s MG 42 burst gives 21 rounds and 21 bodies (13 variations, no repeats), then 1 tail.',
     '- **Reloads.** The start and the completion each play from the weapon\'s reload pool. The Garand plays its clip sound when the clip seats; the Kar98k has its own clip set.',
     '- **Explosions** are sized: shell, AT shell (`explosion_big`), rocket and grenade.',
     '- **Impacts.** Bullet impacts play a material sound (concrete, dirt, metal, wood) at the contact, within 120 m. The knife plays its own material set.',
     '- **Spatial.** Sounds are spatialised by miniaudio with distance rolloff, from a 48-voice pool (the oldest is stolen when all are busy).',
     '- **Not done.** There is no occlusion or reverb model, and no ambience from the banks: the procedural wind stays.', '',
     '## Selected samples', '',
     '| Event | Kind | Status | Samples (bank: index name) | Dur. (s) | Note |', '|---|---|---|---|---|---|']
for ev, e in m['events'].items():
    names = ', '.join('%s:%d `%s`' % (v['bank'].replace('ww2_weapon_', '').replace('cmn_', ''), v['index'], v['name']) for v in e['variants'][:4])
    if len(e['variants']) > 4: names += ' (+%d)' % (len(e['variants']) - 4)
    ds = [x.get(fsb5.wav_name(v['bank'], v['index']), {}).get('dur') for v in e['variants']]
    ds = [d for d in ds if d]
    dur = '%.2f-%.2f' % (min(ds), max(ds)) if ds else ''
    L.append('| `%s` | %s | %s | %s | %s | %s |' % (ev, e['kind'], e['status'], names, dur, e.get('note', '')))
L += ['', '## Gaps reported (no recording in the supplied banks)',
      'Checked against the full sample list of all 12 banks (V9 pass). These slots have no recording, so they keep their procedural sound or use the stand-in shown.', '',
      '- **Footsteps on dry ground** (grass, dirt, gravel, concrete, stone, wood). The only footsteps in the pack are deep-water wading strides (`footstep_waterdeep`), so dry surfaces stay procedural.',
      '- **Landing, jump, crawl / prone movement, gear rattle while moving.** None: the `cloth_*` / `rattl_*` foley is reload handling. Not played.',
      '- **M1 Carbine firing.** Only its reload foley exists. A distinct provisional family stands in (see the table); distant: the generic distant rifle.',
      '- **Distant fire of the M1 / M1C Garand, StG 44, M3, FG 42, BAR, MG 42 and M2HB.** No named far recordings; the generic distant rifle or automatic shots stand in.',
      '- **PaK 40.** The 7.5 cm KwK 40 stands in; the `76mm_pak36` family was not used. Distant: the generic medium cannon.',
      '- **M9 Bazooka.** The family is labelled "m1_bazooka", an approximation. Distant: the generic rocket launcher.',
      '- **Flak 18.** Flak 36 recordings stand in.',
      '- **Tank guns.** No 17-pounder: the US 76 mm M1 stands in. No KwK 36: the Tiger shares the Flak 36 family. Distant: the generic medium / heavy cannon.',
      '- **Rocket loading** (Bazooka, Panzerschreck). No foley; the procedural clicks stay (`rocket_ready`, `rocket_in`).',
      '- **Gun-station loading** (PaK 40 / Flak 18 breech, ammunition crate). No breech sample (`cannon_m_mech` is a 5-6 s mechanism of unknown role, not used); the procedural `breech` / `crate` stay.',
      '- **M1 Garand en-bloc ping.** No labelled ping; procedural.',
      '- **Dry fire (empty click).** Only a Mosin dry fire exists; the procedural click stays (it is also the UI and grenade-pin click).',
      '- **Grenade pin, throw and knife swing.** No suitable samples; the existing click is used.',
      '- **Weapon switch / draw, ADS cloth, bipod deploy.** None.',
      '- **Tank engines, tracks, turret drive.** Not in these banks (the tank bank holds impacts only); procedural.', '',
      '## In the pack, but no slot in the game',
      '- Bullet crack / whiz (near misses), ricochets, shell casings falling by surface, tank-armour hits, flesh hits.',
      '- Battle ambience and weather loops.',
      '- The pistol bank (M1911, P38, Walther PP and others) and rifles not in the arsenal (M1903 Springfield, Gewehr 43, Lee-Enfield and others): ready to map when the weapons are added.', '']
open(os.path.join(ROOT, 'docs/audio.md'), 'w', encoding='utf-8', newline='').write('\n'.join(L))
print('docs/audio.md: %d events' % len(m['events']))
