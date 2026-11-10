// MA1 material audit: every asset material -> its L4 slot, set, mode and mapping, as src/textures.cpp resolves them
// (a material's "texture" field, then textures.json "materials", then the pattern's slot). Usage:
//   node tools/materials_audit.mjs            -> JSON summary on stdout
//   node tools/materials_audit.mjs --md FILE  -> the family x material table (docs/materials-coverage.md body)
import fs from 'fs';
const root = new URL('..', import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1');
const T = JSON.parse(fs.readFileSync(root + 'content/textures/textures.json', 'utf8'));
const PAT = {plain:0,stone:1,wood:2,plaster:3,slate:4,tile:5,metal:6,glass:7,fabric:8,brick:9,painted:10,concrete:11,bunker:15,net:16,
  limewash:17,brickwork:18,ashlar:19,interior:20,floorfin:21,rubble:22,rug:23,ceramic:24,joinery:25,paintwood:26,paper:27,textile:28,rock:29};
function resolve(m) {
  const pat = PAT[m.pattern ?? 'plain'] ?? 0;
  let slot = null, via = '';
  if (m.texture) { slot = m.texture === 'none' ? 'none' : (T.slots[m.texture] ? m.texture : 'set:' + m.texture); via = 'texture'; }
  else if (T.materials[m.name] !== undefined) { slot = T.materials[m.name]; via = 'material'; }
  else if (T.patterns[String(pat)] !== undefined) { slot = T.patterns[String(pat)]; via = 'pattern'; }
  else { slot = 'none'; via = 'no rule'; }
  const s = T.slots[slot] || (slot.startsWith('set:') ? {set: slot.slice(4)} : null);
  return {pat: m.pattern ?? 'plain', slot, via, set: s ? s.set : '-', mode: s ? (s.mode || 'detail') : 'procedural', space: s ? (s.space || 'asset') : '-'};
}
const fam = id => id.split('_')[0];
const rows = new Map();   // family|material -> row
for (const f of fs.readdirSync(root + 'content/assets').filter(f => f.endsWith('.json'))) {
  let a; try { a = JSON.parse(fs.readFileSync(root + 'content/assets/' + f, 'utf8')); } catch { continue; }
  if (!Array.isArray(a.materials)) continue;
  for (const m of a.materials) {
    const r = resolve(m), k = (a.category || fam(a.id)) + '|' + m.name + '|' + r.slot;
    const row = rows.get(k) || {family: a.category || fam(a.id), material: m.name, ...r, assets: new Set()};
    row.assets.add(a.id); rows.set(k, row);
  }
}
const list = [...rows.values()].map(r => ({...r, assets: [...r.assets]}));
if (process.argv[2] === '--md') {
  const out = [];
  const fams = [...new Set(list.map(r => r.family))].sort();
  for (const F of fams) {
    const L = list.filter(r => r.family === F).sort((x, y) => x.material.localeCompare(y.material));
    out.push(`\n### ${F} (${new Set(L.flatMap(r => r.assets)).size} assets)\n`, '| Material | Pattern | Slot (via) | Set | Mode | Mapping | Assets |', '|---|---|---|---|---|---|---|');
    for (const r of L) out.push(`| ${r.material} | ${r.pat} | ${r.slot} (${r.via}) | ${r.set} | ${r.mode} | ${r.space} | ${r.assets.length} |`);
  }
  fs.writeFileSync(process.argv[3], out.join('\n') + '\n');
} else {
  const by = k => Object.entries(list.reduce((o, r) => (o[r[k]] = (o[r[k]] || 0) + r.assets.length, o), {})).sort((a, b) => b[1] - a[1]);
  console.log(JSON.stringify({rows: list.length, byMode: by('mode'), bySlot: by('slot').slice(0, 40), byFamily: by('family').length, procedural: list.filter(r => r.mode === 'procedural').map(r => r.material + ':' + r.pat).slice(0, 80)}, null, 1));
}
