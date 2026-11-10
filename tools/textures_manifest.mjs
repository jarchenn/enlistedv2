// MA1 texture manifest: every ambientCG set in tools/texture_sets.json with its real id, source URL, licence,
// resolution, the maps actually packed (content/textures/<set>/meta.json), physical size and which slots use it
// (base and imgmat tables). Writes content/textures/manifest.json and the table body of docs/textures-manifest.md.
//   node tools/textures_manifest.mjs
import fs from 'fs';
const root = new URL('..', import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1');
const L = JSON.parse(fs.readFileSync(root + 'tools/texture_sets.json', 'utf8'));
const T = JSON.parse(fs.readFileSync(root + 'content/textures/textures.json', 'utf8'));
const I = T.imgmat || {slots: {}};
const useBy = {};
const add = (set, what) => (useBy[set] = useBy[set] || new Set()).add(what);
for (const [k, v] of Object.entries(T.slots)) add(v.set, k);
for (const [k, v] of Object.entries(I.slots)) add(v.set, 'imgmat:' + k);
const out = [];
for (const s of L.sets) {
  const dir = root + 'content/textures/' + s.set + '/';
  let meta = null; try { meta = JSON.parse(fs.readFileSync(dir + 'meta.json', 'utf8')); } catch {}
  out.push({set: s.set, id: s.id, url: `https://ambientcg.com/a/${s.id}`, license: 'CC0 1.0 Universal', resolution: L.resolution,
    downloaded: !!meta && fs.existsSync(dir + 'color.jpg'), maps: meta ? meta.maps : [], sizeM: (meta && meta.size) || s.size,
    sizeSource: s.ma1 ? 'estimated from the preview (ambientCG lists no dimensions)' : 'L4 curation', family: s.family, ma1: !!s.ma1,
    slots: [...(useBy[s.set] || [])].sort()});
}
fs.writeFileSync(root + 'content/textures/manifest.json', JSON.stringify({source: 'https://ambientcg.com', api: 'https://ambientcg.com/api/v2/full_json', license: 'CC0 1.0 Universal', sets: out}, null, 1) + '\n');
const md = ['| Set | ambientCG id | Family | Maps | Tile (m) | Slots (base; imgmat:) | Batch |', '|---|---|---|---|---|---|---|'];
for (const e of out) md.push(`| ${e.set} | [${e.id}](${e.url}) | ${e.family} | ${e.downloaded ? e.maps.map(m => m.replace('.jpg', '')).join(', ') : '**not downloaded**'} | ${e.sizeM.join(' x ')} | ${e.slots.join(', ') || '(unused)'} | ${e.ma1 ? 'MA1' : 'L4'} |`);
fs.writeFileSync(process.argv[2] || root + 'docs/textures-manifest.table.md', md.join('\n') + '\n');
console.log(out.length, 'sets,', out.filter(e => e.ma1).length, 'MA1,', out.filter(e => !e.downloaded).length, 'not downloaded,', out.filter(e => !e.slots.length).length, 'unused');
