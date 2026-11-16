# VH7: write the vehicle spawns proposed by `NormandyMapmaker --gen-vehicle-spawns` (its log on stdin or as argv[1]) into
# content/templates/<id>.nmap.json: a "vehicleSpawns" array inserted at the start of the "scenario" object (the rest of the
# file is left byte for byte; an existing array is replaced). Run from the repo root: python -I tools/vehicles/apply_vspawns.py log.txt
import sys, json, re, os
src = open(sys.argv[1], encoding='utf-8', errors='replace') if len(sys.argv) > 1 else sys.stdin
for line in src:
    if '[vspawn] ' not in line: continue
    name, rest = line.strip().split('[vspawn] ', 1)[1].split(' ', 1)
    if not rest.startswith('['): continue
    spawns = json.loads(rest)
    if not spawns: continue
    path = os.path.join('content', 'templates', name + '.nmap.json')
    s = open(path, encoding='utf-8', newline='').read()
    k = s.find('"scenario":{')
    if k < 0: print(name, 'no scenario object'); continue
    out, n = [], {0: 0, 1: 0}
    for v in spawns:
        n[v['team']] += 1
        out.append({'id': 'vspawn_%s%d' % ('ab'[v['team']], n[v['team']]), 'team': v['team'], 'pos': v['pos'], 'yaw': v['yaw']})
    arr = '"vehicleSpawns":' + json.dumps(out, separators=(',', ':')) + ','
    j0 = s.find('"vehicleSpawns":')
    if j0 >= 0:   # drop the old array (and its comma)
        _, end = json.JSONDecoder().raw_decode(s, j0 + len('"vehicleSpawns":'))
        s = s[:j0] + s[end + (1 if s[end:end + 1] == ',' else 0):]
    k = s.find('"scenario":{') + len('"scenario":{')
    s = s[:k] + arr + s[k:]
    json.loads(s)
    open(path, 'w', encoding='utf-8', newline='').write(s)
    print(name, len(out), 'vehicle spawns')
