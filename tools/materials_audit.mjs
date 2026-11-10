// MA1 material audit (docs/materials-coverage.md): every asset material -> its L4 slot, set, mode and mapping, as
// src/textures.cpp resolves them, in the base table and in the imgmat table (Renderer::Perf imgmat), plus the decision.
// Resolution order: a material's own "texture" field, then textures.json "materials", then (imgmat only) the
// "imgmat.materials" rules for materials without a base rule, then the pattern's slot ("imgmat.patterns" overrides).
//   node tools/materials_audit.mjs            -> JSON summary on stdout
//   node tools/materials_audit.mjs --md FILE  -> the family x material tables
import fs from 'fs';
const root = new URL('..', import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1');
const T = JSON.parse(fs.readFileSync(root + 'content/textures/textures.json', 'utf8'));
const I = T.imgmat || {slots: {}, patterns: {}, materials: {}};
const PAT = {plain:0,stone:1,wood:2,plaster:3,slate:4,tile:5,metal:6,glass:7,fabric:8,brick:9,painted:10,concrete:11,bunker:15,net:16,
  limewash:17,brickwork:18,ashlar:19,interior:20,floorfin:21,rubble:22,rug:23,ceramic:24,joinery:25,paintwood:26,paper:27,textile:28,rock:29};
const slotDef = (name, img) => (img && I.slots[name]) || T.slots[name] || (name && name.startsWith('set:') ? {set: name.slice(4)} : null);
function resolve(m, img) {
  const pat = PAT[m.pattern ?? 'plain'] ?? 0;
  let slot, via;
  if (m.texture) { slot = m.texture === 'none' ? 'none' : (T.slots[m.texture] || (img && I.slots[m.texture]) ? m.texture : 'set:' + m.texture); via = 'texture'; }
  else if (T.materials[m.name] !== undefined) { slot = T.materials[m.name]; via = 'material'; }
  else if (img && I.materials[m.name]) { slot = I.materials[m.name]; via = 'imgmat material'; }
  else if (img && I.patterns[String(pat)]) { slot = I.patterns[String(pat)]; via = 'imgmat pattern'; }
  else if (T.patterns[String(pat)] !== undefined) { slot = T.patterns[String(pat)]; via = 'pattern'; }
  else { slot = 'none'; via = 'no rule'; }
  const s = slot === 'none' ? null : slotDef(slot, img);
  return {slot, via, set: s ? s.set : '-', mode: s ? (s.mode || 'detail') : 'procedural', space: s ? (s.space || 'asset') : '-', under: s && s.under ? slotDef(s.under, img).set : ''};
}
function decision(m, b, i) {
  const p = m.pattern ?? 'plain';
  if (p === 'floorfin') return 'room floor finish: image per finish code (imgmat floors), parquet stays procedural';
  if (['glass', 'net'].includes(p)) return 'kept: glass / camouflage net shading';
  if (i.mode === 'procedural') return p === 'plain' ? 'kept flat: small detail colour (soot, stencils, water, lamps, flowers)' : 'kept procedural (prints, glazes, dials)';
  if (b.set === i.set && b.mode === i.mode) return 'kept: existing assignment';
  return `${i.mode === 'replace' ? 'image replaces the pattern' : 'image detail on the pattern'}: ${i.set}${i.under ? ' over ' + i.under : ''}`;
}
const fam = id => id.split('_')[0];
const rows = new Map();
for (const f of fs.readdirSync(root + 'content/assets').filter(f => f.endsWith('.json'))) {
  let a; try { a = JSON.parse(fs.readFileSync(root + 'content/assets/' + f, 'utf8')); } catch { continue; }
  if (!Array.isArray(a.materials)) continue;
  for (const m of a.materials) {
    const b = resolve(m, false), i = resolve(m, true), F = a.category || fam(a.id), k = F + '|' + m.name + '|' + b.slot + '|' + i.slot;
    const row = rows.get(k) || {family: F, material: m.name, pat: m.pattern ?? 'plain', b, i, decision: decision(m, b, i), assets: new Set()};
    row.assets.add(a.id); rows.set(k, row);
  }
}
const list = [...rows.values()].map(r => ({...r, assets: [...r.assets]}));
const uses = f => list.reduce((n, r) => n + (f(r) ? r.assets.length : 0), 0);
const summary = {
  rows: list.length, families: new Set(list.map(r => r.family)).size, uses: uses(() => true),
  base: {replace: uses(r => r.b.mode === 'replace'), detail: uses(r => r.b.mode === 'detail'), procedural: uses(r => r.b.mode === 'procedural')},
  imgmat: {replace: uses(r => r.i.mode === 'replace'), detail: uses(r => r.i.mode === 'detail'), procedural: uses(r => r.i.mode === 'procedural'), substrate: uses(r => !!r.i.under)},
};
if (process.argv[2] === '--md') {
  const out = [];
  for (const F of [...new Set(list.map(r => r.family))].sort()) {
    const L = list.filter(r => r.family === F).sort((x, y) => x.material.localeCompare(y.material));
    out.push(`\n### ${F} (${new Set(L.flatMap(r => r.assets)).size} assets)\n`,
      '| Material | Pattern | Base slot: set, mode, mapping | imgmat slot: set, mode, mapping | Decision | Uses |', '|---|---|---|---|---|---|');
    for (const r of L) out.push(`| ${r.material} | ${r.pat} | ${r.b.slot}: ${r.b.set}, ${r.b.mode}, ${r.b.space} | ${r.i.slot} (${r.i.via}): ${r.i.set}, ${r.i.mode}, ${r.i.space}${r.i.under ? ', under ' + r.i.under : ''} | ${r.decision} | ${r.assets.length} |`);
  }
  fs.writeFileSync(process.argv[3], out.join('\n') + '\n');
}
console.log(JSON.stringify(summary));
