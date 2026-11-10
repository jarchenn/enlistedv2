// GT1 grass plant atlas (docs/grass-atlas.md): packs Poly Haven's grass_bermuda_01 (and the usable billboards of
// grass_medium_01, colour and dry colour) into one texture-array atlas for the pasture grass cards.
//   node tools/pack_grass_atlas.mjs [sourceDir]   sourceDir default: ../incoming/ground_ph_small (1k/ and 2k/ inside)
// Writes content/textures/grass_atlas/{grass_atlas.png, grass_atlas.json, contact_sheet.png}. No dependencies.
// Cells: connected components of the bermuda alpha (nearby blade tips merged into their plant); hand-picked rectangles
// for grass_medium_01, whose sheet is mostly unwrapped mesh islands (only its five bottom clumps and the seed plumes
// are billboards). Each cell goes to its own LAYERxLAYER layer, scaled to fit with its aspect kept, centred, the
// plant's base on the bottom gutter. Colour sRGB, dilated outside the alpha (no black fringes under filtering);
// alpha = the source alpha (linear). The engine builds coverage-preserving mips.
import fs from 'fs';
import path from 'path';
import zlib from 'zlib';
import { fileURLToPath } from 'url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const src = path.resolve(process.argv[2] || path.join(root, '..', 'incoming', 'ground_ph_small'));
const outDir = path.join(root, 'content', 'textures', 'grass_atlas');
const L = 256, G = 6, COLS = 8;   // layer size, gutter, atlas columns

// ---------------------------------------------------------------- PNG (8-bit gray / RGB / RGBA in, RGBA out)
function decode(f) {
  const b = fs.readFileSync(f); let p = 8, ch = [], W, H, ct;
  while (p < b.length) { const l = b.readUInt32BE(p), t = b.toString('ascii', p + 4, p + 8);
    if (t === 'IHDR') { W = b.readUInt32BE(p + 8); H = b.readUInt32BE(p + 12); ct = b[p + 17]; if (b[p + 16] !== 8) throw new Error(f + ': 8-bit only'); }
    if (t === 'IDAT') ch.push(b.slice(p + 8, p + 8 + l)); p += 12 + l; }
  const bpp = ct === 6 ? 4 : ct === 2 ? 3 : ct === 4 ? 2 : 1, raw = zlib.inflateSync(Buffer.concat(ch)), st = W * bpp + 1;
  const img = new Uint8Array(W * H * 4); let prev = Buffer.alloc(W * bpp);
  for (let y = 0; y < H; y++) { const f2 = raw[y * st], row = raw.subarray(y * st + 1, (y + 1) * st), out = Buffer.alloc(W * bpp);
    for (let i = 0; i < W * bpp; i++) { const a = i >= bpp ? out[i - bpp] : 0, u = prev[i], c = i >= bpp ? prev[i - bpp] : 0; let v = row[i];
      if (f2 === 1) v += a; else if (f2 === 2) v += u; else if (f2 === 3) v += (a + u) >> 1; else if (f2 === 4) { const pp = a + u - c, pa = Math.abs(pp - a), pb = Math.abs(pp - u), pc = Math.abs(pp - c); v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? u : c); }
      out[i] = v & 255; }
    for (let x = 0; x < W; x++) { const o = (y * W + x) * 4;
      if (bpp >= 3) { img[o] = out[x * bpp]; img[o + 1] = out[x * bpp + 1]; img[o + 2] = out[x * bpp + 2]; img[o + 3] = bpp === 4 ? out[x * bpp + 3] : 255; }
      else { img[o] = img[o + 1] = img[o + 2] = out[x * bpp]; img[o + 3] = bpp === 2 ? out[x * bpp + 1] : 255; } }
    prev = out; }
  return { W, H, img };
}
const crcT = new Uint32Array(256).map((_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
const crc = (b) => { let c = 0xFFFFFFFF; for (const x of b) c = crcT[(c ^ x) & 255] ^ (c >>> 8); return (c ^ 0xFFFFFFFF) >>> 0; };
function png(w, h, rgba) {
  const chunk = (t, d) => { const len = Buffer.alloc(4); len.writeUInt32BE(d.length); const td = Buffer.concat([Buffer.from(t), d]); const c = Buffer.alloc(4); c.writeUInt32BE(crc(td)); return Buffer.concat([len, td, c]); };
  const ih = Buffer.alloc(13); ih.writeUInt32BE(w, 0); ih.writeUInt32BE(h, 4); ih[8] = 8; ih[9] = 6;
  const raw = Buffer.alloc((w * 4 + 1) * h); for (let y = 0; y < h; y++) Buffer.from(rgba.buffer, rgba.byteOffset + y * w * 4, w * 4).copy(raw, y * (w * 4 + 1) + 1);
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ih), chunk('IDAT', zlib.deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0))]);
}

// ---------------------------------------------------------------- sources
const pick = (set, file) => { for (const r of ['2k', '1k']) { const f = path.join(src, r, set, file); if (fs.existsSync(f)) return f; } throw new Error('missing ' + set + '/' + file); };
const load = (set, file) => decode(pick(set, file));
const berC = load('grass_bermuda_01', 'color.png'), berA = load('grass_bermuda_01', 'alpha.png');
const medC = load('grass_medium_01', 'color.png'), medD = load('grass_medium_01', 'color_dry.png'), medA = load('grass_medium_01', 'alpha.png');
const alphaOf = (A, x, y) => A.img[(y * A.W + x) * 4] / 255;

// connected components (8-neighbour) of alpha > thr inside a rectangle; returns boxes with pixel counts
function components(A, thr, rx0 = 0, ry0 = 0, rx1 = A.W, ry1 = A.H) {
  const W = A.W, lab = new Int32Array(W * A.H).fill(-1), out = [];
  for (let y = ry0; y < ry1; y++) for (let x = rx0; x < rx1; x++) {
    if (lab[y * W + x] >= 0 || alphaOf(A, x, y) <= thr) continue;
    const id = out.length, q = [y * W + x]; lab[y * W + x] = id; const b = { x0: x, y0: y, x1: x, y1: y, n: 0 };
    while (q.length) { const k = q.pop(), cx = k % W, cy = (k / W) | 0; b.n++; b.x0 = Math.min(b.x0, cx); b.x1 = Math.max(b.x1, cx); b.y0 = Math.min(b.y0, cy); b.y1 = Math.max(b.y1, cy);
      for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) { const nx = cx + dx, ny = cy + dy;
        if (nx < rx0 || ny < ry0 || nx >= rx1 || ny >= ry1) continue; const n = ny * W + nx; if (lab[n] >= 0 || alphaOf(A, nx, ny) <= thr) continue; lab[n] = id; q.push(n); } }
    out.push(b);
  }
  return out;
}
function mergeNear(boxes, pad) {   // blade tips that float free of their plant join the plant whose box they touch
  let changed = true;
  while (changed) { changed = false;
    for (let i = 0; i < boxes.length && !changed; i++) for (let j = i + 1; j < boxes.length && !changed; j++) {
      const a = boxes[i], b = boxes[j];
      if (a.x0 - pad <= b.x1 && b.x0 - pad <= a.x1 && a.y0 - pad <= b.y1 && b.y0 - pad <= a.y1) {
        a.x0 = Math.min(a.x0, b.x0); a.y0 = Math.min(a.y0, b.y0); a.x1 = Math.max(a.x1, b.x1); a.y1 = Math.max(a.y1, b.y1); a.n += b.n; boxes.splice(j, 1); changed = true; } } }
  return boxes;
}

// bermuda: every plant, the seed head and the two single blades (row-major order: top row first); the lone seed head
// (aspect ~0.12) is dropped: a bare spike reads as a stick on its own card (the seed-head grass keeps its procedural stems)
const sc = berA.W / 1024;
let ber = mergeNear(components(berA, 0.2).filter(b => b.n > 120 * sc * sc), 10 * sc).filter(b => { const a = (b.x1 - b.x0 + 1) / (b.y1 - b.y0 + 1); return a > 0.16; });   // also the hair-thin single blades: a few texels wide, the coverage-preserving mips fill their whole column (floating slivers)
ber.sort((a, b) => (Math.round(a.y1 / (200 * sc)) - Math.round(b.y1 / (200 * sc))) || (a.x0 - b.x0));
// grass_medium_01 (1k coordinates): the five clumps along the bottom and three seed plumes; the base rows that carry
// the sheet's stretched UV-dilation bars are cut off
const ms = medA.W / 1024;
const MED = [
  { name: 'med_clump_a', r: [200, 755, 510, 905] }, { name: 'med_clump_b', r: [595, 765, 835, 885] }, { name: 'med_clump_c', r: [22, 885, 205, 992] },
  { name: 'med_clump_d', r: [250, 905, 500, 1004] }, { name: 'med_clump_e', r: [588, 875, 805, 1008] },
];   // its seed plumes are left out: their islands carry dilation streaks that the coverage-preserving mips turn opaque at range

// ---------------------------------------------------------------- cells -> layers
const cells = [];
function addCell(name, C, A, box, thr, keepLargest) {
  let { x0, y0, x1, y1 } = box;
  if (keepLargest) {   // inside a hand-picked rectangle: the largest component and whatever touches its box
    const cs = mergeNear(components(A, thr, x0, y0, x1, y1), 4 * ms).sort((a, b) => b.n - a.n);
    if (!cs.length) return;
    ({ x0, y0, x1, y1 } = cs[0]); box = cs[0];
  }
  const bw = x1 - x0 + 1, bh = y1 - y0 + 1, s = Math.min((L - 2 * G) / bw, (L - 2 * G) / bh);
  const dw = bw * s, dh = bh * s, ox = (L - dw) / 2, oy = G;   // texel space, y up from the layer bottom
  const rgb = new Float32Array(L * L * 3), a = new Float32Array(L * L);
  for (let j = 0; j < L; j++) for (let i = 0; i < L; i++) {
    // area sample: the source footprint of this texel (s < 1: several source texels)
    const sx0 = x0 + (i - ox) / s, sx1 = x0 + (i + 1 - ox) / s, syT = y1 + 1 - (j + 1 - oy) / s, syB = y1 + 1 - (j - oy) / s;
    if (sx1 <= x0 || sx0 >= x1 + 1 || syB <= y0 || syT >= y1 + 1) continue;
    let ca = 0, cr = 0, cg = 0, cb = 0, n = 0;
    for (let yy = Math.max(y0, Math.floor(syT)); yy < Math.min(y1 + 1, Math.ceil(syB)); yy++) for (let xx = Math.max(x0, Math.floor(sx0)); xx < Math.min(x1 + 1, Math.ceil(sx1)); xx++) {
      const al = alphaOf(A, xx, yy), o = (yy * C.W + xx) * 4; ca += al; cr += C.img[o] * al; cg += C.img[o + 1] * al; cb += C.img[o + 2] * al; n++; }
    if (!n) continue;
    const k = j * L + i; a[k] = ca / n; if (ca > 0) { rgb[k * 3] = cr / ca; rgb[k * 3 + 1] = cg / ca; rgb[k * 3 + 2] = cb / ca; }
  }
  // dilate the colour outward (breadth first) so filtering never pulls in the black background
  const seen = new Uint8Array(L * L); let q = [];
  for (let k = 0; k < L * L; k++) if (a[k] > 0.02) { seen[k] = 1; q.push(k); }
  while (q.length) { const nq = []; for (const k of q) { const i = k % L, j = (k / L) | 0;
    for (const [di, dj] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) { const ii = i + di, jj = j + dj; if (ii < 0 || jj < 0 || ii >= L || jj >= L) continue; const n = jj * L + ii; if (seen[n]) continue; seen[n] = 1;
      for (let c = 0; c < 3; c++) rgb[n * 3 + c] = rgb[k * 3 + c]; nq.push(n); } } q = nq; }
  // mean colour of the opaque texels (linear): the engine tints by clump colour / this
  let m = [0, 0, 0], w = 0; const lin = (v) => { v /= 255; return v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4); };
  for (let k = 0; k < L * L; k++) if (a[k] > 0.5) { for (let c = 0; c < 3; c++) m[c] += lin(rgb[k * 3 + c]); w++; }
  cells.push({ name, rgb, a, aspect: bw / bh, u0: ox / L, u1: (ox + dw) / L, v0: oy / L, v1: (oy + dh) / L, mean: m.map(x => +(x / Math.max(1, w)).toFixed(4)),
    src: [Math.round(x0 / sc), Math.round(y0 / sc), Math.round(x1 / sc), Math.round(y1 / sc)] });
}
ber.forEach((b, k) => addCell(`bermuda_${String(k).padStart(2, '0')}`, berC, berA, b, 0.2, false));
for (const m of MED) addCell(m.name, medC, medA, { x0: Math.round(m.r[0] * ms), y0: Math.round(m.r[1] * ms), x1: Math.round(m.r[2] * ms) - 1, y1: Math.round(m.r[3] * ms) - 1 }, 0.5, true);
for (const m of MED.filter(m => m.name.includes('clump'))) addCell(m.name + '_dry', medD, medA, { x0: Math.round(m.r[0] * ms), y0: Math.round(m.r[1] * ms), x1: Math.round(m.r[2] * ms) - 1, y1: Math.round(m.r[3] * ms) - 1 }, 0.5, true);
for (const c of cells) console.log(c.name.padEnd(18), 'src', c.src.join(','), 'aspect', c.aspect.toFixed(2), 'mean', c.mean.join(' '));

// ---------------------------------------------------------------- species: pasture species id -> cells (grass.h / kGrassVS)
// 0 turf: the low spreading tufts; 1 medium meadow: the fuller tufts and leafy stems, the medium clumps; 2 seed-head
// grass (its leafy cards): the tall stems and the plumes; 4 straw: the dry clumps and the browned bermuda tuft;
// 18 marram: the upright double blade and single blades. Clover / shrub / plantain cards keep their own shapes.
const by = (f) => cells.map((c, i) => [c, i]).filter(([c]) => f(c)).map(([, i]) => i);
const isBer = (c) => c.name.startsWith('bermuda');
const brown = (c) => c.mean[0] > c.mean[1] * 0.95;
const species = {
  0: by(c => isBer(c) && !brown(c) && c.aspect > 1.0),
  1: by(c => (isBer(c) && !brown(c) && c.aspect > 0.45 && c.aspect <= 1.0) || (c.name.startsWith('med_clump') && !c.name.endsWith('_dry'))),
  2: by(c => isBer(c) && c.aspect <= 0.45 && c.aspect > 0.12),
  4: by(c => c.name.endsWith('_dry') || (isBer(c) && brown(c) && c.aspect > 1.0)),
  18: by(c => isBer(c) && c.aspect <= 0.45),   // marram: the upright stems (no single-blade cells)
};
for (const [k, v] of Object.entries(species)) if (!v.length) throw new Error('species ' + k + ' has no cell');
console.log('species', JSON.stringify(species));

// ---------------------------------------------------------------- atlas, JSON, contact sheet
fs.mkdirSync(outDir, { recursive: true });
const rows = Math.ceil(cells.length / COLS), AW = COLS * L, AH = rows * L, atlas = new Uint8Array(AW * AH * 4);
cells.forEach((c, n) => { const cx = (n % COLS) * L, cy = ((n / COLS) | 0) * L;
  for (let j = 0; j < L; j++) for (let i = 0; i < L; i++) { const k = j * L + i, o = ((cy + L - 1 - j) * AW + cx + i) * 4;
    for (let q = 0; q < 3; q++) atlas[o + q] = Math.max(0, Math.min(255, Math.round(c.rgb[k * 3 + q]))); atlas[o + 3] = Math.round(Math.min(1, c.a[k]) * 255); } });
fs.writeFileSync(path.join(outDir, 'grass_atlas.png'), png(AW, AH, atlas));
const r4 = (x) => Math.round(x * 1e4) / 1e4;
fs.writeFileSync(path.join(outDir, 'grass_atlas.json'), JSON.stringify({
  _doc: 'GT1 grass plant atlas (docs/grass-atlas.md), written by tools/pack_grass_atlas.mjs from Poly Haven grass_bermuda_01 and grass_medium_01 (CC0): do not edit by hand. '
    + 'layers of layerSize texels, cols per row, rows top-down, texel row 0 = the bottom of its tile (the plant base). cells[k] = layer k: uv rectangle u0..u1 x v0..v1 of the plant inside it, aspect = width / height, '
    + 'mean = linear mean colour of its opaque texels. species: pasture species id -> cell indices.',
  source: ['https://polyhaven.com/a/grass_bermuda_01', 'https://polyhaven.com/a/grass_medium_01'], license: 'CC0 1.0 Universal',
  layerSize: L, cols: COLS, atlas: 'grass_atlas.png', layers: cells.length,
  cells: cells.map(c => ({ name: c.name, u0: r4(c.u0), u1: r4(c.u1), v0: r4(c.v0), v1: r4(c.v1), aspect: r4(c.aspect), mean: c.mean, src: c.src })),
  species }, null, 1) + '\n');
// contact sheet: each cell over bright sky (left) and dark soil (right), its rectangle outlined, labelled by index
const SW = COLS * L, SH = rows * L, sheet = new Uint8Array(SW * SH * 4);
cells.forEach((c, n) => { const cx = (n % COLS) * L, cy = ((n / COLS) | 0) * L;
  for (let j = 0; j < L; j++) for (let i = 0; i < L; i++) { const k = j * L + i, al = Math.min(1, c.a[k]), bg = i < L / 2 ? [190, 210, 232] : [58, 46, 34], o = ((cy + L - 1 - j) * SW + cx + i) * 4;
    const edge = (Math.abs(i / L - c.u0) < 0.004 || Math.abs(i / L - c.u1) < 0.004) && j / L > c.v0 && j / L < c.v1 || (Math.abs(j / L - c.v1) < 0.004 || Math.abs(j / L - c.v0) < 0.004) && i / L > c.u0 && i / L < c.u1;
    for (let q = 0; q < 3; q++) sheet[o + q] = edge ? [230, 60, 40][q] : Math.round(c.rgb[k * 3 + q] * al + bg[q] * (1 - al)); sheet[o + 3] = 255; } });
fs.writeFileSync(path.join(outDir, 'contact_sheet.png'), png(SW, SH, sheet));
console.log(`wrote ${cells.length} cells (${AW}x${AH}) to ${path.relative(root, outDir)}`);
