# BR10: part-built stand-ins for the Battle Royale items, for checkouts without the local-only bakes
# (tools/bake_br_assets.py). Each item keeps its bake's size (the model block and fit data are untouched); only
# "materials" and "parts" are rewritten, and "standinParts": true tells the bake script to keep them.
#   %TEMP%\nmk\py\python.exe -I tools/br_standins.py
import json, os

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'content', 'assets')

MATS = {
    'steel':  {'name': 'steel', 'albedo': [0.07, 0.07, 0.075], 'roughness': 0.45, 'pattern': 'plain', 'metallic': 0.8, 'grain': 1},
    'black':  {'name': 'black', 'albedo': [0.025, 0.025, 0.028], 'roughness': 0.6, 'pattern': 'plain', 'metallic': 0.3, 'grain': 1},
    'lens':   {'name': 'lens', 'albedo': [0.08, 0.16, 0.3], 'roughness': 0.1, 'pattern': 'plain', 'metallic': 0.2, 'grain': 1},
    'rubber': {'name': 'rubber', 'albedo': [0.03, 0.03, 0.03], 'roughness': 0.9, 'pattern': 'plain', 'metallic': 0, 'grain': 1},
    'white':  {'name': 'white', 'albedo': [0.85, 0.85, 0.82], 'roughness': 0.7, 'pattern': 'plain', 'metallic': 0, 'grain': 1},
    'red':    {'name': 'red', 'albedo': [0.6, 0.04, 0.04], 'roughness': 0.6, 'pattern': 'plain', 'metallic': 0, 'grain': 1},
    'cloth':  {'name': 'cloth', 'albedo': [0.82, 0.78, 0.68], 'roughness': 0.95, 'pattern': 'plain', 'metallic': 0, 'grain': 1},
}

def r4(v): return [round(x, 4) for x in v]
def box(c, s, m): return {'t': 'b', 'c': r4(c), 's': r4(s), 'm': m, 'k': 0}
def tube(c, d, L, m, n=16, top=1.0):   # a cylinder along z
    p = {'t': 'c', 'c': r4(c), 's': r4([d, L, d]), 'r': [90, 0, 0], 'n': n, 'm': m, 'k': 0}
    if top != 1.0: p['top'] = top
    return p
def post(c, d, h, m, n=12):            # a cylinder along y
    return {'t': 'c', 'c': r4(c), 's': r4([d, h, d]), 'n': n, 'm': m, 'k': 0}

def suppressor(sx, sy, sz):
    r = sy / 2
    return [tube([0, r, 0], sy * 0.98, sz * 0.86, 'black', 20),
            tube([0, r, -sz * 0.465], sy * 0.7, sz * 0.07, 'steel'),            # thread collar
            tube([0, r, sz * 0.445], sy, sz * 0.11, 'steel', 20),               # end cap
            tube([0, r, sz * 0.5], sy * 0.35, 0.004, 'black', 12)]              # bore
def brake(sx, sy, sz):
    r = sy / 2; parts = [tube([0, r, 0], sy * 0.95, sz, 'steel', 16)]
    for z in (-0.18, 0.06, 0.3):                                                 # ports on both sides
        for sgn in (-1, 1): parts.append(box([sgn * sy * 0.42, r, z * sz], [sy * 0.22, sy * 0.55, sz * 0.14], 'black'))
    return parts
def magazine(sx, sy, sz):   # lying along z, slightly curved: three segments and a base plate
    parts, n = [], 3
    for i in range(n):
        seg = sz / n; z = -sz / 2 + seg * (i + 0.5)
        parts.append({'t': 'b', 'c': r4([0, sy * 0.5 - 0.004 * i, z]), 's': r4([sx * 0.42, sy * 0.88, seg * 1.02]), 'r': [-4 * i, 0, 0], 'm': 'black', 'k': 0})
    parts.append(box([0, sy * 0.5, sz * 0.48], [sx * 0.5, sy, sz * 0.05], 'steel'))   # base plate
    parts.append(box([0, sy * 0.5, -sz * 0.47], [sx * 0.3, sy * 0.6, sz * 0.06], 'steel'))   # feed lips
    return parts
def drum(sx, sy, sz):
    r = sy / 2
    return [{'t': 'c', 'c': [0, r, 0], 's': r4([sy, sx * 0.35, sy]), 'r': [0, 0, 90], 'n': 24, 'm': 'black', 'k': 0},
            box([0, sy * 0.9, -sz * 0.3], [sx * 0.15, sy * 0.2, sz * 0.25], 'steel')]   # the feed tower
def reddot(sx, sy, sz):
    return [box([0, sy * 0.1, 0], [sx, sy * 0.2, sz], 'black'),                          # base / mount
            box([sx * 0.42, sy * 0.6, 0], [sx * 0.16, sy * 0.8, sz * 0.6], 'black'),       # hood sides
            box([-sx * 0.42, sy * 0.6, 0], [sx * 0.16, sy * 0.8, sz * 0.6], 'black'),
            box([0, sy * 0.95, 0], [sx, sy * 0.1, sz * 0.6], 'black'),                     # hood top
            box([0, sy * 0.6, sz * 0.1], [sx * 0.68, sy * 0.66, 0.003], 'lens'),           # the window
            post([sx * 0.5, sy * 0.4, -sz * 0.25], sy * 0.25, sx * 0.2, 'steel')]          # brightness knob
def scope(sx, sy, sz, eyeY, objective=1.6, turrets=True):   # tube with rings, bells, lenses and turrets
    d = min(sx, (sy - 0) * 0.9) / objective
    parts = [tube([0, eyeY, 0], d, sz * 0.62, 'black', 20),
             tube([0, eyeY, sz * 0.38], d * objective, sz * 0.22, 'black', 24, top=0.75),   # objective bell
             tube([0, eyeY, -sz * 0.41], d * 1.25, sz * 0.18, 'black', 20),                # eyepiece
             tube([0, eyeY, sz * 0.495], d * objective * 0.86, 0.003, 'lens', 24),
             tube([0, eyeY, -sz * 0.5], d * 1.05, 0.003, 'lens', 20)]
    for z in (-0.18, 0.16):                                                                 # rings and their bases
        parts.append(tube([0, eyeY, z * sz], d * 1.25, sz * 0.06, 'steel', 20))
        parts.append(box([0, (eyeY - d * 0.6) / 2, z * sz], [sx * 0.55, max(0.004, eyeY - d * 0.6), sz * 0.07], 'steel'))
    if turrets:
        parts.append(post([0, eyeY + d * 0.75, 0], d * 0.6, d * 0.5, 'steel'))
        parts.append({'t': 'c', 'c': r4([d * 0.75, eyeY, 0]), 's': r4([d * 0.6, d * 0.5, d * 0.6]), 'r': [0, 0, 90], 'n': 12, 'm': 'steel', 'k': 0})
    return parts
def acog(sx, sy, sz, eyeY):
    d = sy * 0.5
    return [box([0, sy * 0.12, 0], [sx * 0.8, sy * 0.24, sz * 0.8], 'black'),                # mount
            {'t': 'r', 'c': r4([0, eyeY, -sz * 0.05]), 's': r4([d, d, sz * 0.75]), 'rad': d * 0.3, 'm': 'black', 'k': 0},   # the body
            tube([0, eyeY, sz * 0.38], d * 1.15, sz * 0.24, 'black', 20),                     # objective
            tube([0, eyeY, sz * 0.5], d, 0.003, 'lens', 20),
            tube([0, eyeY, -sz * 0.46], d * 0.85, sz * 0.08, 'black', 16),
            box([0, eyeY + d * 0.6, 0], [sx * 0.2, sy * 0.12, sz * 0.4], 'steel'),            # fibre on top
            post([d * 0.65, eyeY, 0], d * 0.35, d * 0.3, 'steel')]
def recoil_pad(sx, sy, sz):
    return [{'t': 'r', 'c': r4([0, sy / 2, 0]), 's': r4([sx, sy, sz * 0.9]), 'rad': min(sx, sy) * 0.3, 'm': 'rubber', 'k': 0},
            box([0, sy / 2, sz * 0.47], [sx * 0.9, sy * 0.9, sz * 0.06], 'black')] + \
           [box([sx * 0.51, sy * (0.2 + 0.15 * i), 0], [0.002, sy * 0.05, sz * 0.8], 'black') for i in range(5)]   # ribs
def medkit(sx, sy, sz):
    return [{'t': 'r', 'c': [0, sy / 2, 0], 's': [sx, sy, sz], 'rad': 0.012, 'm': 'white', 'k': 0},
            box([0, sy + 0.001, 0], [sx * 0.42, 0.003, sz * 0.14], 'red'), box([0, sy + 0.001, 0], [sx * 0.14, 0.003, sz * 0.42], 'red'),
            box([0, sy * 0.85, -sz * 0.5], [sx * 0.3, sy * 0.15, 0.012], 'black')]       # the handle
def bandage(sx, sy, sz):
    return [{'t': 'c', 'c': [0, sy / 2, 0], 's': [sy, sx, sy], 'r': [0, 0, 90], 'n': 20, 'm': 'cloth', 'k': 0},
            {'t': 'c', 'c': [0, sy / 2, 0], 's': [sy * 0.35, sx * 1.02, sy * 0.35], 'r': [0, 0, 90], 'n': 12, 'm': 'black', 'k': 0},
            box([0, sy * 0.02, sz * 0.35], [sx * 0.9, 0.003, sz * 0.4], 'cloth')]        # the loose end

def size_of(a):
    p = a['parts'][0] if a.get('parts') else {'s': [0.1, 0.1, 0.1]}
    return p['s'] if not a.get('standinParts') else a['standinSize']

def rewrite(idn, build, mats, extra_eye=False):
    p = os.path.join(ROOT, idn + '.json')
    a = json.load(open(p))
    sx, sy, sz = size_of(a)
    eyeY = a.get('sight', {}).get('eye', [0, sy * 0.6, 0])[1]
    a['parts'] = build(sx, sy, sz, eyeY) if extra_eye else build(sx, sy, sz)
    a['materials'] = [MATS[m] for m in mats]
    a['standinParts'] = True; a['standinSize'] = r4([sx, sy, sz])
    a['description'] = a['description'].replace('Stand-in box;', 'Part-built stand-in (tools/br_standins.py);')
    with open(p, 'w', newline='\n') as f: json.dump(a, f, indent=1)
    print('stand-in', idn, len(a['parts']), 'parts')

def new_asset(idn, name, size, build, mats):   # an item that has no supplied model at all
    sx, sy, sz = size
    a = {'id': idn, 'version': 1, 'name': name, 'category': 'Battle Royale', 'hidden': True,
         'description': 'BR10 Battle Royale item, part-built (tools/br_standins.py); no supplied model.',
         'floors': 0, 'enterable': False, 'footprint': [max(sx, 0.05), max(sz, 0.05)], 'plinthDepth': 0, 'maxGrade': 1, 'noAutoPanels': True,
         'furniture': [], 'interactables': [], 'panels': [], 'materials': [MATS[m] for m in mats], 'parts': build(sx, sy, sz),
         'nav': {'effect': 'none'}, 'standinParts': True, 'standinSize': r4([sx, sy, sz])}
    with open(os.path.join(ROOT, idn + '.json'), 'w', newline='\n') as f: json.dump(a, f, indent=1)
    print('new', idn, len(a['parts']), 'parts')

rewrite('br_suppressor', suppressor, ['black', 'steel'])
rewrite('br_muzzle_brake', brake, ['steel', 'black'])
for m in ('br_mag_ext', 'br_mag_ext_rifle', 'br_mag_ext_pistol', 'br_mag_ext_sniper'): rewrite(m, magazine, ['black', 'steel'])
rewrite('br_mag_ext_smg', drum, ['black', 'steel'])
rewrite('br_sight_reddot', reddot, ['black', 'lens', 'steel'])
rewrite('br_sight_acog', acog, ['black', 'lens', 'steel'], extra_eye=True)
rewrite('br_sight_4x', lambda sx, sy, sz, e: scope(sx, sy, sz, e, 1.5), ['black', 'lens', 'steel'], extra_eye=True)
rewrite('br_sight_10x', lambda sx, sy, sz, e: scope(sx, sy, sz, e, 1.9), ['black', 'lens', 'steel'], extra_eye=True)
rewrite('br_recoil_pad', recoil_pad, ['rubber', 'black'])
new_asset('br_medkit', 'Medkit', [0.3, 0.1, 0.22], medkit, ['white', 'red', 'black'])
new_asset('br_bandage', 'Bandage', [0.09, 0.09, 0.09], bandage, ['cloth', 'black'])
