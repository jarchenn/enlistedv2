"""Builds content/audio/manifest.json: the selected samples of the supplied FMOD banks per game event (complete
update A1). Deterministic: the same banks give the same manifest. Run, then extract:

  python tools/audio_manifest.py <banks dir>                  -> content/audio/manifest.json
  python tools/fsb5.py extract content/audio/manifest.json <banks dir> content/audio/samples

Selection is by family name in the bank (bank + index + name recorded; names repeat, so the index is the identity),
confirmed by the measured envelope (single shots: short, sharp onset; tails: energy after 0.3 s). These files were
NOT auditioned by a person: every mapping carries a status - "named" (the family name matches the weapon), "approx"
(a named stand-in for an absent recording, explained in "note"), "provisional" (no recording of that weapon exists in
the banks; a distinct placeholder, explained) - and stays replaceable here. Redistribution status: the banks come from
an Enlisted sound mod; no reuse permission was supplied, so the extracted samples are git-ignored and not shipped.
"""
import json, os, re, sys
sys.path.insert(0, os.path.dirname(__file__))
import fsb5

BANKS = sys.argv[1]
OUT = os.path.join(os.path.dirname(__file__), '..', 'content', 'audio', 'manifest.json')
cache = {}


def bank(name):
    if name not in cache: cache[name] = fsb5.read_bank(os.path.join(BANKS, name + '.assets.bank'))
    return cache[name]


def family(bk, fam, limit=8):
    """samples named <fam>_<n> (n = variation number), in variation order"""
    b, ss = bank(bk)
    pat = re.compile('^' + re.escape(fam) + r'_(\d+)$')
    hits = sorted(((int(pat.match(s['name']).group(1)), s) for s in ss if pat.match(s['name'])), key=lambda x: (x[0], x[1]['index']))
    out = []
    for _, s in hits[:limit]:
        st = fsb5.stats(b, s)
        out.append({'bank': bk, 'index': s['index'], 'name': s['name'], 'dur': round(st['dur'], 3), 'peak': st['peak']})
    return out


events = {}


def ev(name, kind, gain, variants, status, note=''):
    if not variants: print('  (no samples for %s)' % name); return
    events[name] = {'kind': kind, 'gain': gain, 'status': status, 'note': note, 'variants': variants}


R, MG, MT, CN, AV, FX, RF, MF = ('ww2_weapon_rifle', 'ww2_weapon_mgun', 'ww2_weapon_mgun_mounted', 'ww2_weapon_cannon',
                                 'ww2_weapon_antivehicle', 'cmn_fx_impact', 'ww2_weapon_rifle_foley', 'ww2_weapon_mgun_foley')
# ---- personal weapons: one shot sample per fired round (AUTO cadence comes from the game), a tail once on release
ev('shot_kar98k', 'shot', 0.95, family(R, 'mauser98k_body'), 'named')
ev('shot_kar98k_far', 'far', 0.9, family(R, 'mauser98k_e_far_body'), 'named')
ev('shot_kar98k_zf39', 'shot', 0.95, family(R, 'kar98k_scope_body'), 'named', 'the bank\'s scoped-Kar98k family')
ev('shot_m1_garand', 'shot', 0.95, family(R, 'm1_body'), 'approx', 'rifle-bank "m1" family (semi-automatic, no loop): taken as the M1 Garand; the name alone does not prove it')
ev('shot_m1c_garand', 'shot', 0.95, family(R, 'm1_body'), 'approx', 'same rifle as the M1 Garand: shares its family on purpose')
ev('shot_m1_carbine', 'shot', 0.85, family(MG, 'm1_body'), 'provisional', 'no M1 Carbine firing recording exists in the banks (only its reload foley); the automatic-bank "m1" family is used as a distinct placeholder - replace when a carbine recording is available')
ev('shot_m1_thompson', 'shot', 0.9, family(MG, 'thompson_body'), 'named')
ev('tail_m1_thompson', 'tail', 0.8, family(MG, 'thompson_tail'), 'named')
ev('shot_mp40', 'shot', 0.9, family(MG, 'mp40_body'), 'named')
ev('tail_mp40', 'tail', 0.8, family(MG, 'mp40_tail'), 'named')
ev('shot_stg44', 'shot', 0.9, family(MG, 'stg_44_body'), 'named')
ev('tail_stg44', 'tail', 0.8, family(MG, 'stg_44_tail'), 'named')
ev('shot_m3_grease_gun', 'shot', 0.9, family(MG, 'm3_body'), 'named', "the automatic bank's M3 family (M3 grease gun)")
ev('tail_m3_grease_gun', 'tail', 0.8, family(MG, 'm3_tail'), 'named')
ev('shot_fg42', 'shot', 0.9, family(MG, 'fg42_body'), 'named', 'plain fg42 family (fg42_m2_* is another labelled variant, not used)')
ev('tail_fg42', 'tail', 0.8, family(MG, 'fg42_tail'), 'named')
ev('shot_m1918a2_bar', 'shot', 0.95, family(MG, 'm1918_body'), 'named', 'm1918 family (BAR), not the browning_auto_5 shotgun')
ev('tail_m1918a2_bar', 'tail', 0.85, family(MG, 'm1918_tail'), 'named')
ev('shot_mg42', 'shot', 0.95, family(MG, 'mg42_body', 16), 'named')
ev('tail_mg42', 'tail', 0.85, family(MG, 'mg42_tail'), 'named')
ev('shot_bazooka_m9', 'shot', 1.0, family(AV, 'm1_bazooka_body'), 'approx', 'labelled M1 bazooka: an approximation for the M9')
ev('shot_panzerschreck', 'shot', 1.0, family(AV, 'panzerschreck_body'), 'named')
ev('shot_panzerschreck_far', 'far', 1.0, family(AV, 'panzerschreck_e_far_body'), 'named')
# ---- crew-served
ev('shot_m2hb', 'shot', 1.0, family(MT, '12_7mm_m2_body'), 'named', 'mounted-bank 12_7mm_m2 (12_7mm_browning50_* is a second candidate, not used)')
ev('pak_fire', 'shot', 1.0, family(CN, '75mm_kwk40_body'), 'approx', 'no PaK 40 recording; the 7.5 cm KwK 40 (the PaK 40\'s tank-gun relative, same calibre and cartridge) is used instead of the 76mm_pak36 family')
ev('flak_fire', 'shot', 1.0, family(CN, '88mm_flak36_e_body'), 'approx', '8.8 cm Flak 36 recordings stand in for the Flak 18')
ev('flak_fire_far', 'far', 1.0, family(CN, '88mm_flak36_e_far_body'), 'approx', 'Flak 36, distant')
# ---- explosions by size, impacts by material, knife
ev('explosion', 'blast', 1.0, family(FX, 'shell_mid_explosion'), 'named', 'shell / HE')
ev('explosion_far', 'far', 1.0, family(FX, 'shell_mid_explosion_far', 10), 'named')
ev('explosion_grenade', 'blast', 0.9, family(FX, 'grenade_blast'), 'named')
ev('explosion_grenade_far', 'far', 0.9, family(FX, 'grenade_blast_far'), 'named')
ev('explosion_rocket', 'blast', 1.0, family(FX, 'rocket_explosion'), 'named')
ev('explosion_big', 'blast', 1.0, family(FX, 'shell_big_explosion'), 'named', 'AT shell / heavy blast')
for m in ('concrete', 'dirt', 'metal', 'wood', 'water'):
    ev('impact_' + m, 'impact', 0.55, family(FX, 'bullet_hit_' + m), 'named')
for m, f in (('concrete', 'impact_knife_cocncrete'), ('metal', 'impact_knife_metal'), ('wood', 'impact_knife_wood'), ('generic', 'impact_knife_generic')):
    ev('knife_' + m, 'impact', 0.7, family(FX, f), 'named', 'bank spelling kept: "cocncrete"' if 'cocncrete' in f else '')
# ---- reload foley: start / completion share one pool per weapon (stage order not auditioned)
ev('reload_kar98k', 'foley', 0.8, family(RF, 'change_ammo_mauser98k'), 'named', 'stripper-clip reload; stages are not ordered by audition')
ev('clip_kar98k', 'foley', 0.8, family(RF, 'change_ammo_mauser98k_clip'), 'named')
ev('reload_m1_garand', 'foley', 0.8, family(RF, 'change_ammo_m1garand'), 'named')
ev('clip_m1_garand', 'foley', 0.85, family(RF, 'm1garand_clip'), 'named', 'en-bloc clip: played when the clip seats')
ev('reload_m1_carbine', 'foley', 0.8, family(MF, 'change_ammo_m1carbine'), 'named')
ev('reload_mp40', 'foley', 0.8, family(MF, 'change_ammo_mp40'), 'named')
ev('reload_stg44', 'foley', 0.8, family(MF, 'change_ammo_stg_44'), 'named')
ev('reload_m3_grease_gun', 'foley', 0.8, family(MF, 'change_ammo_m3'), 'named')
ev('reload_m1918a2_bar', 'foley', 0.8, family(MF, 'change_ammo_m1918'), 'named')
ev('reload_mg42', 'foley', 0.8, family(MF, 'change_ammo_mg42'), 'named', 'belt; stages are not ordered by audition')
ev('reload_m1_thompson', 'foley', 0.8, family(MF, 'change_ammo_thompson'), 'named', 'box-magazine Thompson (drum reloads not used)')
ev('reload_fg42', 'foley', 0.8, family(MF, 'change_ammo_fg42'), 'named')
ev('bolt', 'foley', 0.7, family(RF, 'shutter_out_rifle_bolt'), 'approx', 'generic rifle-bolt foley for bolt actions (Kar98k / ZF39)')

man = {'note': __doc__.strip().splitlines()[0], 'redistribution': 'NOT cleared: Enlisted sound-mod banks supplied without reuse permission; samples are git-ignored (content/audio/samples) and extracted locally from the user\'s banks',
       'banks': sorted(cache.keys()), 'events': events}
os.makedirs(os.path.dirname(OUT), exist_ok=True)
json.dump(man, open(OUT, 'w', encoding='utf-8', newline=''), indent=1)
print('%d events, %d samples' % (len(events), sum(len(e['variants']) for e in events.values())))
