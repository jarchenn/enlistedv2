# WS1: the user's approved grips (blender_anim/gun_in_clip.py GIC_SAVEPOSE: both wrists and every finger, relative to the gun,
# captured on the Mixamo skeleton) -> content/weapons/<id>.json "hold"."pose", which pose.cpp applies rigidly.
#   python -I tools/grip_to_game.py <dir with *_grip.json>...
# Capture space: Blender world (z up, the soldier faces -y), gun-local x right / y muzzle / z up, origin the grip centre.
# Game: model space y up / +z forward / +x left; weapon space x left / y up / z muzzle.
#   r / l .p: the wrist joint from its anchor, weapon space (r: the rightGrip socket; l: "grip" = the rightGrip socket too, so
#             the approved hand spacing is kept, or "mag" = the magazine's centre for hold.left = magazine)
#   r / l .q: the hand's turn from its rest, in weapon space (model-space turn = weapon rotation * q), xyzw
#   fingers:  each finger joint's turn from rest against its parent's, model axes, xyzw (joint name without its prefix)
import json, sys, os, glob, re
import numpy as np
IDS = {'Garand': ['m1_garand', 'm1c_garand'], 'Thompson': ['m1_thompson'], 'M3': ['m3_grease_gun'], 'BAR': ['m1918a2_bar'], 'MP40': ['mp40'],
       'StG44': ['stg44'], 'Kar98k': ['kar98k', 'kar98k_zf39'], 'FG42': ['fg42'], 'MG42': ['mg42'], 'Panzerschreck': ['panzerschreck'], 'M1911': ['m1911a1'], 'P08': ['luger']}
WRIST = 0.065   # m: the game bodies' wrist joints were moved from mid-forearm (Mixamo's placement) to the cuff; the same shift here keeps the hands where they were approved
CW = np.array([[-1, 0, 0], [0, 0, 1], [0, 1, 0]], float)     # gun-local -> weapon space
CM = np.array([[1, 0, 0], [0, 0, 1], [0, -1, 0]], float)     # Blender world -> model space
def quat(m):   # rotation matrix -> xyzw
    t = np.trace(m)
    if t > 0: s = np.sqrt(t + 1) * 2; q = [(m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s, s / 4]
    else:
        i = int(np.argmax(np.diag(m))); j, k = (i + 1) % 3, (i + 2) % 3; s = np.sqrt(1 + m[i, i] - m[j, j] - m[k, k]) * 2
        q = [0, 0, 0, (m[k, j] - m[j, k]) / s]; q[i] = s / 4; q[j] = (m[j, i] + m[i, j]) / s; q[k] = (m[k, i] + m[i, k]) / s
    return [round(float(v), 4) for v in q]
def rot(wxyz):
    w, x, y, z = wxyz
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)], [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)], [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
def ortho(m): u, _, v = np.linalg.svd(np.array(m, float)); return u @ v
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'content', 'weapons')
def hand(g, side, anchor):   # one wrist: its place from `anchor` (gun-local) and its turn, weapon space
    M = np.array(g['R' if side == 'r' else 'L'], float); rest = ortho(g['rest']['mixamorig:' + ('Right' if side == 'r' else 'Left') + 'Hand'])
    wrist = M[:3, 3] + ortho(M[:3, :3]) @ np.array([0, WRIST, 0])   # the body's wrist joint sits WRIST further down its own axis than the capture rig's (see WRIST)
    return {'p': [round(float(v), 4) for v in CW @ (wrist - np.array(anchor, float))], 'q': quat(CW @ ortho(M[:3, :3]) @ rest.T @ CM.T)}
def fingers(g, which):       # the finger turns of one hand ('Left' / 'Right')
    return {n.split(':')[-1]: quat(CM @ ortho(g['rest'][n]) @ rot(q) @ ortho(g['rest'][n]).T @ CM.T) for n, q in g['fingers'].items() if which in n}
def butt(g): b = CW @ np.array(g.get('butt') or (0, 0, 0), float); return [round(float(b[0]), 4), round(float(b[1]), 4)]   # the butt plate from the grip centre, weapon x / y (its z is the model's rear end)
# per-gun corrections from the user's review, on top of the grip or template. l / r: that hand on the gun, weapon space metres
# (x left, y up, z toward the muzzle); gun: the whole gun from its ruled place (x left, y up, z along the aim); scale: the gun
# drawn larger in these soldiers' hands (about the grip; the sockets follow).
ADJUST = {   # two review rounds, summed
    'modern_m4_irons': {'l': (0, 0, 0.11)},                                        # support hand forward on the handguard
    'kar98k': {'scale': 1.25},                                                     # 25 % larger (the scoped Kar98k stays as it is)
    'm1_garand': {'l': (0, 0.02, 0)},                                              # left hand higher
    'm1918a2_bar': {'l': (0, 0.035, 0), 'r': (0, 0.015, 0)},
    'm1c_garand': {'l': (0, 0.015, 0), 'r': (0, -0.01, 0)},                        # right hand lower again
    'mg42': {'l': (0, -0.02, 0)}, 'modern_fal': {'l': (0, -0.07, 0), 'scale': 1.15}, 'modern_m16': {'l': (0, -0.02, 0)},
    'm3_grease_gun': {'scale': 0.65},                                              # 35 % smaller
    'modern_ak': {'gun': (0, 0, 0.05), 'l': (0, -0.07, -0.13)},                    # gun forward; left hand back and lower
    'modern_ak12': {'scale': 1.12, 'gun': (0, 0, 0.0798), 'l': (0, -0.03, -0.10)},                  # (Blender review: the hand was out at the gas block)
    'modern_ak47': {'scale': 1.288, 'gun': (0, 0, 0.052), 'l': (0, -0.04, -0.13)},       # left hand down under the wood
    'modern_ar15': {'scale': 1.15}, 'modern_glauncher': {'scale': 1.15, 'gun': (0, 0, 0.0698)}, 'modern_sniper_50': {'l': (-0.03, 0, 0)}, 'modern_sniper_3': {'scale': 0.98},
    'modern_pistol2': {'scale': 1.15}, 'modern_fn502': {'scale': 1.15}, 'sten': {'scale': 1.15},
    'stg44': {'scale': 0.92, 'gun': (-0.03, -0.03, 0.04), 'r': (0, 0, -0.08)},    # smaller, back / right / down; right hand 8 cm back
    'modern_lmg': {'scale': 1.316, 'gun': (0, 0, -0.05)}, 'modern_makarov': {'scale': 1.755},   # M249: +35 %, +30 %, then -25 % and back
    'modern_scorpion': {'scale': 1.69, 'gun': (0, 0, -0.05)},                      # +35 %, then +25 %; 5 cm back
    'modern_mp7': {'gun': (0, 0, 0.11), 'l': (0, 0, 0.07)}, 'modern_mp5': {'gun': (0, 0, 0.07)}, 'modern_sawnoff': {'gun': (0, 0, 0.04)}, 'modern_smg9': {'gun': (0, 0, 0.11)}, 'modern_shotgun2': {'gun': (0, 0, 0.10)},
    'modern_shotgun': {'l': (0, 0, 0.12), 'r': (0.03, 0, -0.03)},                  # right hand back and in
    'modern_scar': {'l': (0, -0.07, 0), 'gun': (0, 0, 0.02)},                                            # (Blender review: the hand lay over the top of the handguard)
    'modern_rpg7': {'scale': 1.35, 'gun': (-0.107, 0.06, 0)}, 'bazooka_m9': {'gun': (-0.09, 0.07, 0.10)},                           # out to the right and up: clear of the head
    'modern_smg45': {'l': (0, -0.02, 0.04)},
    'modern_uzi': {'scale': 1.8, 'gun': (0, 0.05, 0)},
    'modern_vsk': {'scale': 1.35, 'gun': (-0.02, 0.07, 0.30)},
    'modern_sniper_semi': {'scale': 0.85, 'gun': (0, 0.03, 0.1061)},
    'modern_pistol3': {'scale': 1.3, 'l': (0, -0.025, 0), 'r': (0, -0.025, 0)},
    # the pistol 2.5 cm higher in the hands
    'modern_pistol4': {'l': (0, -0.025, 0), 'r': (0, -0.025, 0)}, 'modern_revolver': {'l': (0, -0.025, 0), 'r': (0, -0.025, 0)},
    'modern_pistol': {'l': (0, -0.025, 0), 'r': (0, -0.025, 0)},
    'modern_glock34': {'l': (0, -0.025, 0), 'r': (0, -0.025, 0), 'scale': 1.15}, 'modern_mark23': {'l': (0, -0.025, 0), 'r': (0, -0.025, 0)},
}
# the support hand taken from another approved grip, anchored on this gun's leftSupport socket (review: the Thompson's and the
# M249's left hands redone as a palm-up hold under the foregrip / handguard, as on the Garand)
LEFT_FROM = {}
def left_from(wid, pose):
    if wid not in LEFT_FROM: return pose, None
    g = G[LEFT_FROM[wid]]; fg = {k: v for k, v in pose['fingers'].items() if 'Left' not in k}; fg.update(fingers(g, 'Left'))
    return dict(pose, l=dict(hand(g, 'l', g['s_loc']), anchor='support'), fingers=fg), False
# hold.kind / hold.left per gun: launchers ride the shoulder as the Panzerschreck does; "tube": the support hand in a C over the tube
# beside the face (fingers on top, thumb under) instead of a stored left hand
HOLD = {'bazooka_m9': {'kind': 'launcher', 'left': 'tube'}, 'modern_rpg7': {'kind': 'launcher'}, 'panzerschreck': {'left': 'tube'}}
# hands fitted in Blender against each gun's own mesh (blender_anim/holdview.py fit(): the palm on the surface, every finger closed
# until it meets the gun): {weapon: {l: {p}, r: {p}, fingers: {...}}}; replaces the wrist places and finger turns above. Made from
# the weapon files as written with ADJUST, so re-run the fit after changing ADJUST for a gun.
FIT = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'grip_fit.json'))) if os.path.exists(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'grip_fit.json')) and not os.environ.get('GRIP_NOFIT') else {}
def write(wid, pose, left_mag):
    for side, d in ADJUST.get(wid, {}).items():
        if side in ('l', 'r'): pose[side] = dict(pose[side], p=[round(a + b, 4) for a, b in zip(pose[side]['p'], d)])
        elif side == 'gun': pose['gun'] = list(d)
        elif side == 'scale': pose['scale'] = d
    if wid in FIT:
        for side in ('l', 'r'):
            if side in FIT[wid]: pose[side] = dict(pose[side], **{k: v for k, v in FIT[wid][side].items() if v is not None})   # p, and q / anchor where the hand was turned in Blender
        fg = pose.pop('fingers'); fg.update(FIT[wid].get('fingers', {})); pose['fingers'] = fg
    p = os.path.join(root, wid + '.json'); s = open(p, encoding='utf-8', newline='').read()
    while '"pose":{' in s:                                                                          # earlier exports out (by brace matching)
        i = s.index('"pose":{'); j = i + 7; depth = 0
        while True:
            depth += s[j] == '{'; depth -= s[j] == '}'; j += 1
            if depth == 0: break
        s = s[:i] + s[j + (s[j] == ','):]
    s = s.replace(',}', '}')
    for k in ('kind', 'left'): s = re.sub(r'"%s":"[a-z]*",?' % k, '', s) if (k == 'kind' and wid in HOLD) or (k == 'left' and (not left_mag or wid in HOLD)) else s
    s = s.replace(',}', '}')
    for k, v in HOLD.get(wid, {}).items(): s = s.replace('"hold":{', '"hold":{"%s":"%s",' % (k, v), 1) if '"hold":{' in s else s.replace('"sockets":', '"hold":{"%s":"%s"},"sockets":' % (k, v), 1)
    s = s.replace(',}', '}')
    if left_mag and '"left":"magazine"' not in s: s = s.replace('"hold":{', '"hold":{"left":"magazine",', 1) if '"hold":{' in s else s.replace('"sockets":', '"hold":{"left":"magazine"},"sockets":', 1)
    txt = '"pose":' + json.dumps(pose, separators=(',', ':'))
    s = s.replace('"hold":{', '"hold":{' + txt + (',' if not s.split('"hold":{', 1)[1].startswith('}') else ''), 1) if '"hold":{' in s else s.replace('"sockets":', '"hold":{' + txt + '},"sockets":', 1)
    json.loads(s); open(p, 'w', encoding='utf-8', newline='').write(s)
G = {}
for d in sys.argv[1:]:
    for f in sorted(glob.glob(os.path.join(d, '*_grip.json'))):
        g = json.load(open(f))
        if 'rest' in g: G[os.path.basename(f)[:-10]] = g
done = set()
for tag, ids in IDS.items():           # 1: the grips approved on the gun itself
    if tag not in G: continue
    g = G[tag]
    for wid in ids:
        mag = json.load(open(os.path.join(root, wid + '.json'), encoding='utf-8')).get('hold', {}).get('left') == 'magazine' and g.get('mag_c') is not None
        pose = {'r': hand(g, 'r', (0, 0, 0)), 'l': dict(hand(g, 'l', g['mag_c'] if mag else (0, 0, 0)), anchor='mag' if mag else 'grip'), 'butt': butt(g), 'fingers': {**fingers(g, 'Left'), **fingers(g, 'Right')}}
        pose, m2 = left_from(wid, pose)
        write(wid, pose, mag if m2 is None else m2); done.add(wid); print(wid, 'own grip, l anchor', pose['l']['anchor'])
# 2: every other gun from an approved grip of its family, each hand anchored on that gun's own sockets (r: rightGrip; l: the
# magazine's centre or the leftSupport socket). (right-hand source, left-hand source, left anchor); marked "template".
# (right-hand source, left-hand source, the gun's left anchor, the source point the left hand is measured from)
FAM = {'mag': ('StG44', 'StG44', 'mag', 'mag_c'), 'pg': ('StG44', 'FG42', 'support', 's_loc'), 'straight': ('Garand', 'Garand', 'support', 's_loc'),
       'launcher': ('Panzerschreck', 'Panzerschreck', 'support', 's_loc'), 'pistol': ('M1911', 'M1911', 'grip', None),
       'pg_under': ('StG44', 'MG42', 'support', 's_loc'),    # a pistol-grip rifle held underhand at the handguard like the MG 42 (the AKs)
       'cgrip': ('StG44', 'StG44', 'support', 'mag_c')}      # a vertical foregrip at the support socket: the C grip (the .50 sniper)
def family(wid, wj):
    n = wid.replace('modern_', '')
    if any(k in wid for k in ('blaster', 'flame', 'flammen', 'm2hb', 'grenade', 'smoke', 'molotov', 'knife', 'hammer', 'explosion', 'handling')): return None
    if any(k in n for k in ('rpg', 'bazooka', 'glauncher', 'panzer')): return 'launcher'
    if any(k in n for k in ('pistol', 'glock', 'deagle', 'fn502', 'makarov', 'mark23', 'revolver', 'luger', 'm1911')): return 'pistol'   # the approved two-handed pistol grip (the postures are pose.cpp's)
    if n in ('ak', 'ak47', 'ak12'): return 'pg_under'
    if n in ('sniper_50', 'mp7'): return 'cgrip'   # MP7: its magazine is in the pistol grip; the support hand takes the foregrip as on the MP5
    if n in ('uzi', 'vsk'): return 'pg'           # magazine in / right at the pistol grip: the support hand goes under the forend
    if any(k in n for k in ('ak', 'mp5', 'mp7', 'uzi', 'smg', 'scorpion', 'ump', 'vsk')): return 'mag'
    if any(k in n for k in ('carbine', 'sten', 'shotgun', 'sawnoff', 'sniper_bolt', 'sniper_3', 'sniper_50', 'sniper_semi')): return 'straight'
    return 'pg'
for f in sorted(glob.glob(os.path.join(root, '*.json'))):
    wid = os.path.basename(f)[:-5]
    if wid in done: continue
    try: wj = json.load(open(f, encoding='utf-8'))
    except Exception: continue
    fam = family(wid, wj)
    if not fam or 'sockets' not in wj: continue
    rs, ls, anc, src = FAM[fam]
    if rs not in G or ls not in G: continue
    gl = G[ls]
    pose = {'r': hand(G[rs], 'r', (0, 0, 0)), 'l': dict(hand(gl, 'l', gl[src] if src else (0, 0, 0)), anchor=anc), 'template': fam, 'butt': butt(G[rs]), 'fingers': {**fingers(gl, 'Left'), **fingers(G[rs], 'Right')}}
    pose, m2 = left_from(wid, pose)
    write(wid, pose, (anc == 'mag') if m2 is None else m2); print(wid, 'template', fam)
