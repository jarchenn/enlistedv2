# KD1: the nine added deaths (tools/enemies/death_add_all.sh) appended to the named types' death lists in a zombie.json /
# zombie_local.json, edited as text so the rest of the file is untouched. Run after a type's model has the clips.
#   python -I tools/enemies/death_lists.py <zombie.json> <type id> ...
import json, re, sys
KD1 = ["death front", "death back", "death front headshot", "death back headshot", "death right", "death falling forward", "death flying back", "death backward", "death forward"]
P = sys.argv[1]; s = open(P, encoding='utf-8').read()
for t in sys.argv[2:]:
    m = re.search(r'"id":\s*"%s"' % re.escape(t), s)
    if not m: print('absent:', t); continue
    a = s.rfind('{', 0, m.start()); nx = re.search(r'"id":\s*"', s[m.end():]); b = m.end() + nx.start() if nx else len(s)
    seg = s[a:b]; d = re.search(r'"death":\s*(\[[^\]]*\]|"[^"]*")', seg)
    if not d: print('no death list:', t); continue
    have = json.loads(d.group(1)); have = have if isinstance(have, list) else [have]
    s = s[:a] + seg[:d.start()] + '"death": ' + json.dumps(have + [c for c in KD1 if c not in have]) + seg[d.end():] + s[b:]; print(t, 'ok')
json.loads(s); open(P, 'w', encoding='utf-8', newline='\n').write(s)
