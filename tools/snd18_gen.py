import sys, os, glob
sys.path += [r'C:\Users\50bmg\AppData\Roaming\Python\Python312\site-packages', r'C:\Users\50bmg\AppData\Local\Programs\Python\Python312\Lib\site-packages']
import glob, os, re, numpy as np, soundfile as sf
SR = 44100; O = 'out'
def lv(f, w=0.15):
    x, sr = sf.read(f, always_2d=True); m = x.mean(1) * 0.95 / (np.abs(x).max() + 1e-9)
    a = int(np.argmax(np.abs(m) > 0.3 * np.abs(m).max())); return 20 * np.log10(np.sqrt(np.mean(m[a:a + int(w * SR)] ** 2)) + 1e-9)
CLS = {'pistol': -13.7, 'smg': -11.7, 'rifle': -8.7, 'mg': -7.2, 'fifty': -5.7}
TAIL = {'pistol': 1.6, 'smg': 1.4, 'rifle': 2.0, 'mg': 1.6, 'fifty': 1.6}
# pack file, class, [(game id, pitch)], automatic
M = [
 ('luger','pistol',[('luger',1.0)],0), ('m1911','pistol',[('m1911a1',1.0)],0), ('makarov','pistol',[('modern_makarov',1.0)],0),
 ('hk_mk23','pistol',[('modern_mark23',1.0)],0), ('deagle','pistol',[('modern_deagle',1.0)],0),
 ('glock','pistol',[('modern_pistol3',1.0),('modern_glock34',1.05)],0), ('beretta','pistol',[('modern_pistol4',0.94)],0),
 ('mp40','smg',[('mp40',1.0)],1), ('thompson','smg',[('m1_thompson',1.0)],1), ('m3_grease_gun','smg',[('m3_grease_gun',1.0)],1),
 ('sten','smg',[('sten',1.0)],1), ('cz_scorpion','smg',[('modern_scorpion',1.0)],1), ('hk_mp7','smg',[('modern_mp7',1.0)],1),
 ('ump','smg',[('modern_smg45',1.0)],1), ('uzi','smg',[('modern_uzi',1.0)],1),
 ('m1_carbine','rifle',[('m1_carbine',1.0)],0), ('m1_garand','rifle',[('m1_garand',1.0),('m1c_garand',0.97)],0),
 ('kar98k','rifle',[('kar98k',1.0),('kar98k_zf39',0.96)],0), ('stg44','rifle',[('stg44',1.0)],1),
 ('fg42','rifle',[('fg42',1.03)],1), ('bar','rifle',[('m1918a2_bar',0.97)],1), ('m4','rifle',[('modern_m4_irons',1.0)],1),
 ('m16','rifle',[('modern_m16',1.0)],1), ('scar_h','rifle',[('modern_scar',1.0)],1),
 ('shotgun_remington870','rifle',[('modern_shotgun',1.0)],0), ('shotgun_spas12','rifle',[('modern_shotgun2',1.0)],0),
 ('mg42','mg',[('mg42',1.0)],1), ('m249','mg',[('modern_lmg',1.0)],1),
 ('m2_browning','fifty',[('m2hb_carry',1.0),('m2hb',1.0)],1), ('30mm_cannon','fifty',[('cannon30',1.0)],2),
]
used = set(); rows = []; table = []
def G(db): return round(float(np.clip(10 ** (db / 20), 0.5, 2.2)), 2)
for f, c, ids, auto in M:
    T = CLS[c]; tail = TAIL[c] if f != 'kar98k' else 2.5
    sl = lv(O + '/%s_single.mp3' % f); gs = G(T - sl); used.add('%s_single.mp3' % f)
    av = sorted(glob.glob(O + '/%s_auto_*.mp3' % f)); n = len(av)
    ga = G(T - float(np.mean([lv(x) for x in av]))) if auto and n else None
    if auto: used.update(os.path.basename(x) for x in av)
    for gid, p in ids:
        if auto != 2: rows.append('    {"snd18/%s_single.mp3", false, {"%s"}, -1, %.2ff, 0, %.1ff, 0.0f, %.2ff},' % (f, gid + ('_semi' if auto else ''), p, tail, gs))
        if auto: rows.append('    {"snd18/%s_auto_%%d.mp3", false, {"%s"}, -1, %.2ff, 0, %.1ff, 0.0f, %.2ff, %d},' % (f, gid, p, tail, ga, n))
        table.append('%-18s %-22s %-6s pitch %.2f  single gain %.2f%s' % (gid, f, c, p, gs, ('  auto %d vars gain %.2f' % (n, ga)) if auto else ''))
for x in sorted(os.listdir(O)):
    if x not in used and not x.startswith(('bazooka', 'panzerschreck', 'rpg7')): os.remove(O + '/' + x)
open('rows.txt', 'w').write('\n'.join(rows) + '\n'); open('table.txt', 'w').write('\n'.join(table) + '\n')
print('\n'.join(table)); print(len(rows), 'rows;', len(os.listdir(O)), 'files')
