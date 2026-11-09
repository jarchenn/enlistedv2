// L1 leaf textures (docs/leaves.md): procedural, reproducible species leaf images for the tree / shrub canopies.
//   node tools/gen_leaf_textures.mjs            writes content/textures/leaves/{leaves.png, leaves.json, contact_sheet.png}
// No dependencies (node zlib for PNG). Every image is a pure function of the species tables and seeds below.
// Each species: >= 3 single-leaf variants (compound leaves / gorse sprays count as one "leaf") and 2 sprig layers
// (several leaves on a twig, composited from the variants). The engine draws the sprig layer on far cards and the
// same leaves as fitted meshes near (leaves.json carries each sprig's leaf transforms and each leaf's fitted hull),
// so a card and its leaves line up across the LOD switch.
// Layers: 288 x 288 texels, 16 texel gutter (leaf long side 256). base colour sRGB, alpha = coverage (linear);
// outside the silhouette the colour is dilated from the nearest leaf texel (no white / black fringes under filtering).
import fs from 'fs';
import path from 'path';
import zlib from 'zlib';
import { fileURLToPath } from 'url';

const S = 288, G = 16, N = S - 2 * G, COLS = 8, SS = 4;   // layer size, gutter, leaf extent, atlas columns, supersampling
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const outDir = path.join(root, 'content', 'textures', 'leaves');

// ------------------------------------------------------------------ small helpers
const rngOf = (seed) => () => { seed |= 0; seed = (seed + 0x6D2B79F5) | 0; let t = Math.imul(seed ^ (seed >>> 15), 1 | seed); t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
const clamp = (x, a, b) => Math.min(b, Math.max(a, x));
const smooth = (a, b, x) => { const t = clamp((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); };
const mix = (a, b, t) => a.map((v, i) => v + (b[i] - v) * t);
const h2 = (x, y) => { const s = Math.sin(x * 127.1 + y * 311.7) * 43758.5453; return s - Math.floor(s); };
const vnoise = (x, y) => { const xi = Math.floor(x), yi = Math.floor(y), fx = x - xi, fy = y - yi, ux = fx * fx * (3 - 2 * fx), uy = fy * fy * (3 - 2 * fy);
  const a = h2(xi, yi), b = h2(xi + 1, yi), c = h2(xi, yi + 1), d = h2(xi + 1, yi + 1); return a + (b - a) * ux + (c - a) * uy + (a - b - c + d) * ux * uy; };
const voronoiEdge = (x, y) => {   // distance to the nearest cell border (reticulate tertiary veins)
  const xi = Math.floor(x), yi = Math.floor(y); let d1 = 9, d2 = 9;
  for (let j = -1; j <= 1; j++) for (let i = -1; i <= 1; i++) {
    const cx = xi + i + 0.15 + 0.7 * h2(xi + i, yi + j), cy = yi + j + 0.15 + 0.7 * h2(yi + j + 17.3, xi + i - 4.1);
    const d = Math.hypot(x - cx, y - cy); if (d < d1) { d2 = d1; d1 = d; } else if (d < d2) d2 = d;
  }
  return d2 - d1;
};
const toLin = (c) => c.map(v => { v /= 255; return v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4); });

// ------------------------------------------------------------------ leaf model
// A leaf is a set of blades (lamina with midrib, secondary veins, reticulate tertiaries, toothed / lobed margin),
// stems (petiole, rachis, twig, prickles: capsules) and optional flowers (gorse). Leaf space: x in [-0.5, 0.5],
// y in [0, 1] (petiole base at y = 0, tip toward y = 1).
// blade: o [x,y] base, ang (rad, clockwise from +y), L length, W max half width / L, prof(v) 0..1,
//        serr {n, d, d2, v0, round}, lobes {n, d, ph: [left, right], v0}, veins {vk: [[left v..], [right v..]], slope, curve},
//        mid, sec (vein widths, leaf units), ter (tertiary strength), vmin (cordate base below v = 0), notch(v)
const ovate = (p, q) => (v) => Math.pow(Math.max(0, Math.sin(Math.PI * Math.pow(clamp(v, 0, 1), p))), q);
const evenVeins = (n, v0, v1, alt = 0) => [Array.from({ length: n }, (_, k) => v0 + (v1 - v0) * (k + 0.5) / n - alt), Array.from({ length: n }, (_, k) => v0 + (v1 - v0) * (k + 0.5) / n + alt)];

function bladeWidth(b, v, side) {   // half width (blade units of L) at v on side (0 left, 1 right)
  let w = b.W * b.prof(v) * (side ? b.aR ?? 1 : b.aL ?? 1);
  if (b.lobes && v > b.lobes.v0) {
    const m = 0.5 + 0.5 * Math.cos(2 * Math.PI * (b.lobes.n * (v - b.lobes.v0) + b.lobes.ph[side]));
    w *= 1 - b.lobes.d * Math.pow(m, 3) * smooth(b.lobes.v0, b.lobes.v0 + 0.08, v);
  }
  if (b.serr && v > (b.serr.v0 ?? 0.06) && v < 0.985) {
    const t = (v * b.serr.n + side * 0.37) % 1;
    const tooth = b.serr.round ? 1 - Math.sin(Math.PI * t) : t * t;   // round: crenate; else sharp teeth pointing at the tip
    let d = b.serr.d * tooth;
    if (b.serr.d2) { const t2 = (v * b.serr.n * 3 + side * 0.21) % 1; d += b.serr.d2 * t2 * t2 * (0.4 + 0.6 * tooth); }
    w *= 1 - d * smooth(b.serr.v0 ?? 0.06, (b.serr.v0 ?? 0.06) + 0.1, v);
  }
  return w;
}

function hitBlade(b, px, py) {   // -> null | {v, x, w, side}
  const dx = px - b.o[0], dy = py - b.o[1], s = Math.sin(b.ang), c = Math.cos(b.ang);
  const v = (dx * s + dy * c) / b.L, x = (dx * c - dy * s) / b.L;
  if (v < (b.vmin ?? 0) || v > 1) return null;
  const side = x < 0 ? 0 : 1, w = bladeWidth(b, v, side), ax = Math.abs(x);
  if (ax > w) return null;
  if (b.notch && ax < b.notch(v)) return null;
  return { v, x, ax, w, side };
}

function hitStem(st, px, py) {   // capsule from a to b, width w0 -> w1 (half widths)
  const [ax, ay] = st.a, [bx, by] = st.b, vx = bx - ax, vy = by - ay, l2 = vx * vx + vy * vy;
  const t = clamp(((px - ax) * vx + (py - ay) * vy) / l2, 0, 1), qx = ax + vx * t - px, qy = ay + vy * t - py;
  const w = st.w0 + (st.w1 - st.w0) * t, d = Math.hypot(qx, qy);
  return d <= w ? { t, d, w } : null;
}

function veinStrength(b, h, pxw) {   // 0..1 vein coverage at a blade hit (pxw: one texel in leaf units)
  const L = b.L, aa = (d, w) => clamp((w - d) / pxw + 0.5, 0, 1);
  if (b.noVeins) return { mid: aa(h.ax * L, (b.mid ?? 0.006) * (1 - 0.7 * h.v)), sec: 0, ter: 0 };
  const mid = aa(h.ax * L, (b.mid ?? 0.008) * (1 - 0.65 * h.v) + pxw * 0.2);
  let sec = 0;
  const reach = h.ax / Math.max(1e-4, h.w);   // 0 at the midrib, 1 at the margin
  for (const vk of b.veins.vk[h.side]) {
    const vc = vk + b.veins.slope * h.ax + (b.veins.curve ?? 0) * h.ax * h.ax;
    const d = Math.abs(h.v - vc) * L / Math.sqrt(1 + b.veins.slope * b.veins.slope);
    const w = (b.sec ?? 0.0045) * (1 - 0.75 * reach);
    if (reach < 0.93) sec = Math.max(sec, aa(d, w) * (1 - smooth(0.8, 0.93, reach)));
  }
  const ter = b.ter ? clamp(1 - voronoiEdge(h.x * L * 34 + 3.1, h.v * L * 34) / 0.12, 0, 1) * b.ter * smooth(0.0, 0.08, h.ax * L) : 0;
  return { mid, sec, ter };
}

// ------------------------------------------------------------------ species (variant v: 0..n-1, R: seeded rng)
const deg = Math.PI / 180;
const SPECIES = {
  oak: { name: 'Pedunculate oak (Quercus robur): obovate, 4-6 rounded lobes per side, auricled base, short petiole',
    variants: 4, col: { leaf: [68, 96, 40], vein: [132, 146, 78], stem: [112, 104, 60] }, sprig: { k: 6, len: 0.44, spread: 58 },
    make(v, R) {
      const n = 4.2 + R() * 1.4, phL = R() * 0.3, phR = phL + 0.3 + R() * 0.25, L = 0.9 + R() * 0.04;
      const vk = (ph) => { const a = []; for (let k = 0; k < 8; k++) { const vv = 0.1 + (k + 0.5 - ph) / n; if (vv > 0.14 && vv < 0.95) a.push(vv - 0.04); } return a; };
      return { stems: [{ a: [0, 0], b: [0, 0.07], w0: 0.010, w1: 0.008 }],
        blades: [{ o: [0, 0.06], ang: (R() - 0.5) * 4 * deg, L, W: 0.34 + R() * 0.05, aL: 0.95 + R() * 0.1, aR: 0.95 + R() * 0.1,
          prof: (u) => ovate(1.45, 0.85)(u) + 0.22 * Math.exp(-Math.pow((u - 0.06) / 0.05, 2)),
          lobes: { n, d: 0.42 + R() * 0.12, ph: [phL, phR], v0: 0.1 }, serr: null,
          veins: { vk: [vk(phL), vk(phR)], slope: 0.55, curve: 0.3 }, mid: 0.011, sec: 0.005, ter: 0.35 }],
        wear: { spots: v === 1 ? 2 : v === 3 ? 1 : 0, hole: v === 2, rim: v === 3 } };
    } },
  ash: { name: 'Common ash (Fraxinus excelsior): pinnate, 7-11 lanceolate serrulate leaflets on a rachis',
    variants: 3, col: { leaf: [84, 116, 48], vein: [148, 162, 92], stem: [104, 112, 62] }, sprig: { k: 3, len: 0.66, spread: 50 },
    make(v, R) {
      const pairs = v === 1 ? 3 : 4, blades = [], stems = [{ a: [0, 0], b: [0, 0.72], w0: 0.009, w1: 0.005 }];
      const leaflet = (o, ang, L) => ({ o, ang, L, W: 0.19 + R() * 0.03, aL: 0.9 + R() * 0.2, aR: 0.9 + R() * 0.2,
        prof: (u) => ovate(0.9, 0.7)(u) * (1 - 0.35 * smooth(0.6, 1, u)), serr: { n: 15, d: 0.1, v0: 0.15 },
        veins: { vk: evenVeins(9, 0.06, 0.92, 0.015), slope: 0.55, curve: 1.2 }, mid: 0.006, sec: 0.0028, ter: 0.25 });
      for (let p = 0; p < pairs; p++) {
        const y = 0.12 + p * (0.56 / pairs) + R() * 0.02, L = (v === 1 ? 0.31 : 0.27) * (0.85 + 0.15 * p / pairs), a = (58 + R() * 12 - p * 3) * deg;
        blades.push(leaflet([0, y], -a, L), leaflet([0, y + 0.01], a, L));
      }
      blades.push(leaflet([0, 0.7], (R() - 0.5) * 8 * deg, 0.29));
      return { stems, blades, wear: { spots: v === 2 ? 2 : 0 } };
    } },
  apple: { name: 'Apple (Malus domestica): ovate-elliptic, crenate-serrate, short-acuminate tip',
    variants: 3, col: { leaf: [78, 104, 52], vein: [146, 156, 100], stem: [124, 112, 70] }, sprig: { k: 6, len: 0.42, spread: 50 },
    make(v, R) {
      return { stems: [{ a: [0, 0], b: [0, 0.13], w0: 0.009, w1: 0.007 }],
        blades: [{ o: [0, 0.12], ang: (R() - 0.5) * 6 * deg, L: 0.86, W: 0.29 + R() * 0.05, aL: 0.92 + R() * 0.16, aR: 0.92 + R() * 0.16,
          prof: (u) => ovate(0.82, 0.85)(u) * (1 - 0.3 * smooth(0.78, 1, u)), serr: { n: 24 + R() * 6, d: 0.05, round: true, v0: 0.12 },
          veins: { vk: evenVeins(7, 0.08, 0.85, 0.02), slope: 0.75, curve: 1.6 }, mid: 0.009, sec: 0.0042, ter: 0.3 }],
        wear: { spots: v === 1 ? 3 : 0, rim: v === 2 } };
    } },
  poplar: { name: 'Black poplar (Populus nigra): deltoid, truncate base, acuminate tip, crenate margin, long petiole',
    variants: 3, col: { leaf: [88, 120, 46], vein: [168, 172, 96], stem: [150, 136, 72] }, sprig: { k: 6, len: 0.42, spread: 62 },
    make(v, R) {
      const W = 0.52 + R() * 0.06;
      return { stems: [{ a: [0, 0], b: [0, 0.25], w0: 0.008, w1: 0.007 }],
        blades: [{ o: [0, 0.23], ang: (R() - 0.5) * 6 * deg, L: 0.76, W, aL: 0.94 + R() * 0.12, aR: 0.94 + R() * 0.12,
          prof: (u) => (0.72 + 0.28 * smooth(0, 0.12, u)) * Math.pow(1 - u, 1.05) * (1 + 0.32 * Math.sin(Math.PI * (1 - u))) / 1.12,
          serr: { n: 17 + R() * 4, d: 0.06, round: true, v0: 0.04 },
          veins: { vk: evenVeins(6, 0.02, 0.8, 0.02), slope: 0.42, curve: 1.0 }, mid: 0.009, sec: 0.0045, ter: 0.25 }],
        wear: { spots: v === 2 ? 2 : 0 } };
    } },
  hawthorn: { name: 'Hawthorn (Crataegus monogyna): small, deeply 3-5 lobed, lobes toothed toward the tips',
    variants: 3, col: { leaf: [64, 98, 40], vein: [128, 148, 80], stem: [100, 96, 56] }, sprig: { k: 6, len: 0.38, spread: 55 },
    make(v, R) {
      const lobe = (ang, L, W) => ({ o: [0, 0.15], ang, L, W, prof: ovate(0.95, 0.8), serr: { n: 7, d: 0.2, v0: 0.5 },
        veins: { vk: evenVeins(3, 0.15, 0.8, 0.02), slope: 0.6, curve: 0.8 }, mid: 0.008, sec: 0.0038, ter: 0.25 });
      const blades = [{ o: [0, 0.14], ang: 0, L: 0.3, W: 0.5, prof: ovate(1, 0.6), veins: { vk: [[], []], slope: 0.5 }, mid: 0.008, ter: 0.25 },
        lobe((R() - 0.5) * 6 * deg, 0.8 + R() * 0.04, 0.26), lobe(-(30 + R() * 10) * deg, 0.6, 0.27), lobe((30 + R() * 10) * deg, 0.6, 0.27)];
      if (v !== 1) blades.push(lobe(-(68 + R() * 12) * deg, 0.38, 0.27), lobe((68 + R() * 12) * deg, 0.38, 0.27));
      return { stems: [{ a: [0, 0], b: [0, 0.16], w0: 0.009, w1: 0.008 }], blades, wear: { spots: v === 2 ? 1 : 0 } };
    } },
  blackthorn: { name: 'Blackthorn (Prunus spinosa): small, elliptic-obovate, finely crenate-serrate',
    variants: 3, col: { leaf: [56, 80, 38], vein: [108, 124, 70], stem: [82, 72, 52] }, sprig: { k: 8, len: 0.4, spread: 50 },
    make(v, R) {
      return { stems: [{ a: [0, 0], b: [0, 0.11], w0: 0.010, w1: 0.008 }],
        blades: [{ o: [0, 0.1], ang: (R() - 0.5) * 6 * deg, L: 0.88, W: 0.24 + R() * 0.05, aL: 0.92 + R() * 0.16, aR: 0.92 + R() * 0.16,
          prof: ovate(1.3, 0.75), serr: { n: 22 + R() * 6, d: 0.06, round: true, v0: 0.1 },
          veins: { vk: evenVeins(6, 0.1, 0.85, 0.02), slope: 0.8, curve: 1.5 }, mid: 0.011, sec: 0.005, ter: 0.25 }],
        wear: { spots: v === 1 ? 2 : 0 } };
    } },
  gorse: { name: 'Gorse (Ulex europaeus): spine sprays (green branched spines), some sprays with yellow pea flowers',
    variants: 3, col: { leaf: [56, 82, 42], vein: [104, 126, 70], stem: [92, 86, 50], flower: [232, 186, 36] }, sprig: { k: 7, len: 0.62, spread: 40 },
    make(v, R) {
      const bend = (R() - 0.5) * 0.12, stems = [{ a: [0, 0], b: [bend, 0.97], w0: 0.012, w1: 0.006 }], blades = [], flowers = [];
      const spine = (o, ang, L) => ({ o, ang, L, W: 0.1, prof: (u) => Math.pow(1 - u, 0.9) * Math.min(1, u * 10 + 0.5), noVeins: true, mid: 0.004 });
      for (let y = 0.05, k = 0; y < 0.95; y += 0.03 + R() * 0.012, k++) {
        const side = k % 2 ? 1 : -1, x = bend * y, a = side * (32 + R() * 38) * deg, L = 0.13 + R() * 0.11;
        blades.push(spine([x, y], a, L));
        if (L > 0.15) blades.push(spine([x + Math.sin(a) * L * 0.45, y + Math.cos(a) * L * 0.45], a - side * 35 * deg, L * 0.45));
      }
      blades.push(spine([bend * 0.96, 0.95], 0, 0.06));
      const nf = v === 2 ? 0 : 2 + (R() * 2 | 0);
      for (let f = 0; f < nf; f++) { const y = 0.45 + R() * 0.45; flowers.push({ c: [bend * y + (R() - 0.5) * 0.08, y], r: 0.045 + R() * 0.012, rot: R() * 6.28 }); }
      return { stems, blades, flowers, wear: {} };
    } },
  hazel: { name: 'Hazel (Corylus avellana): rounded, cordate base, abrupt short tip, doubly serrate, deep veins',
    variants: 3, col: { leaf: [82, 110, 50], vein: [144, 156, 92], stem: [108, 98, 60] }, sprig: { k: 5, len: 0.44, spread: 60 },
    make(v, R) {
      const W = 0.47 + R() * 0.05;
      return { stems: [{ a: [0, 0], b: [0, 0.14], w0: 0.010, w1: 0.009 }],
        blades: [{ o: [0, 0.13], ang: (R() - 0.5) * 8 * deg, L: 0.84, W, vmin: -0.08, aL: 0.93 + R() * 0.12, aR: 0.93 + R() * 0.12,
          prof: (u) => Math.sqrt(Math.max(0, 1 - Math.pow((u - 0.42) / 0.5, 2))) * (1 - 0.5 * smooth(0.74, 1, u)) + 0.04 * smooth(0.9, 1, u) * 0,
          notch: (u) => u < 0.03 ? 0.08 * (0.03 - u) / 0.11 + 0.012 : 0,
          serr: { n: 16 + R() * 4, d: 0.07, d2: 0.035, v0: 0.0 },
          veins: { vk: evenVeins(7, 0.04, 0.82, 0.02), slope: 0.5, curve: 0.6 }, mid: 0.010, sec: 0.0052, ter: 0.45 }],
        wear: { spots: v === 0 ? 1 : v === 2 ? 2 : 0, hole: v === 1 } };
    } },
  bramble: { name: 'Bramble (Rubus fruticosus agg.): compound, 3 or 5 ovate sharply doubly serrate leaflets, prickly petiole',
    variants: 3, col: { leaf: [60, 86, 40], vein: [124, 138, 80], stem: [124, 64, 50] }, sprig: { k: 3, len: 0.58, spread: 55 },
    make(v, R) {
      const leaflet = (o, ang, L, W) => ({ o, ang, L, W, aL: 0.9 + R() * 0.2, aR: 0.9 + R() * 0.2,
        prof: (u) => ovate(0.75, 0.8)(u) * (1 - 0.35 * smooth(0.72, 1, u)), serr: { n: 17, d: 0.1, d2: 0.05, v0: 0.06 },
        veins: { vk: evenVeins(8, 0.05, 0.85, 0.02), slope: 0.5, curve: 0.8 }, mid: 0.007, sec: 0.0035, ter: 0.3 });
      const blades = [leaflet([0, 0.34], (R() - 0.5) * 6 * deg, 0.64, 0.3), leaflet([0, 0.33], -(52 + R() * 10) * deg, 0.45, 0.29), leaflet([0, 0.33], (52 + R() * 10) * deg, 0.45, 0.29)];
      if (v === 2) blades.push(leaflet([0, 0.31], -112 * deg, 0.3, 0.3), leaflet([0, 0.31], 112 * deg, 0.3, 0.3));
      for (let k = 0; k < 4; k++) blades.push({ o: [k % 2 ? 0.008 : -0.008, 0.06 + k * 0.07], ang: (k % 2 ? 1 : -1) * 120 * deg, L: 0.035, W: 0.45, prof: (u) => 1 - u, noVeins: true, mid: 0, prickle: true });
      return { stems: [{ a: [0, 0], b: [0, 0.35], w0: 0.012, w1: 0.010 }], blades, wear: { spots: v === 1 ? 2 : 0 } };
    } },
};

// ------------------------------------------------------------------ raster
const mkLayer = () => ({ rgb: new Float32Array(S * S * 3), a: new Float32Array(S * S), cls: new Uint8Array(S * S) });   // cls 1 leaf, 2 flower
const toLeaf = (i, j) => [(i + 0.5 - S / 2) / N, (j + 0.5 - G) / N];   // texel (row j up from the bottom) -> leaf space

function renderLeaf(sp, leaf, R) {
  const lay = mkLayer(), pxw = 1 / N, col = sp.col;
  const spots = [], holes = [];
  const main = leaf.blades[0];
  const inBlade = (u, x) => { const h = hitBlade(main, main.o[0] + Math.sin(main.ang) * u * main.L + Math.cos(main.ang) * x * main.L, main.o[1] + Math.cos(main.ang) * u * main.L - Math.sin(main.ang) * x * main.L); return h; };
  for (let k = 0; k < (leaf.wear.spots || 0); k++) for (let t = 0; t < 20; t++) {
    const u = 0.2 + R() * 0.7, x = (R() - 0.5) * main.W * 1.2, h = inBlade(u, x);
    if (h && h.ax < h.w * 0.8) { spots.push({ c: [main.o[0] + Math.sin(main.ang) * u * main.L + Math.cos(main.ang) * x * main.L, main.o[1] + Math.cos(main.ang) * u * main.L - Math.sin(main.ang) * x * main.L], r: 0.006 + R() * 0.012 }); break; }
  }
  if (leaf.wear.hole) for (let t = 0; t < 20; t++) {
    const u = 0.3 + R() * 0.5, x = (R() - 0.5) * main.W, h = inBlade(u, x);
    if (h && h.ax < h.w * 0.7 && h.ax > 0.03) { holes.push({ c: [main.o[0] + Math.sin(main.ang) * u * main.L + Math.cos(main.ang) * x * main.L, main.o[1] + Math.cos(main.ang) * u * main.L - Math.sin(main.ang) * x * main.L], r: 0.009 + R() * 0.006 }); break; }
  }
  const pig = R() * 100, rim = !!leaf.wear.rim;
  const sample = (x, y) => {   // -> null | {c: [r,g,b] sRGB, cls}
    for (const hl of holes) if (Math.hypot(x - hl.c[0], y - hl.c[1]) < hl.r) return null;
    for (const f of leaf.flowers || []) {
      const dx = x - f.c[0], dy = y - f.c[1], r = Math.hypot(dx, dy), a = Math.atan2(dy, dx) + f.rot;
      const petal = f.r * (0.55 + 0.45 * Math.pow(Math.abs(Math.cos(a * 2.5)), 0.6));
      if (r < petal) { const sh = 0.82 + 0.18 * (r / petal) + 0.06 * (vnoise(x * 90, y * 90) - 0.5); return { c: mix(col.flower, [196, 120, 20], r < f.r * 0.25 ? 0.5 : 0).map(c => c * sh), cls: 2 }; }
    }
    for (let bi = leaf.blades.length - 1; bi >= 0; bi--) {
      const b = leaf.blades[bi], h = hitBlade(b, x, y); if (!h) continue;
      if (b.prickle) return { c: mix(col.stem, [200, 170, 120], 0.3 * h.v), cls: 1 };
      const vs = veinStrength(b, h, pxw);
      let c = col.leaf.slice();
      const n1 = vnoise(x * 9 + pig, y * 9), n2 = vnoise(x * 31 + pig, y * 31 + 7);
      c = c.map(v => v * (0.93 + 0.12 * n1 + 0.05 * (n2 - 0.5)));                   // subtle pigmentation, no baked lighting
      c = mix(c, [c[0] * 1.12, c[1] * 1.04, c[2] * 0.8], 0.35 * smooth(0.55, 0.85, n1) * smooth(0.2, 1.0, h.v));   // a warmer patch toward the tip
      const edge = (h.w - h.ax) * b.L / pxw;                                        // texels to the margin
      c = mix(c, c.map(v => v * 0.9), 0.5 * (1 - smooth(0, 3, edge)));            // the margin reads very slightly darker
      if (rim && h.v > 0.82) c = mix(c, [120, 98, 52], 0.55 * (1 - smooth(0, 6, edge)) * smooth(0.82, 0.95, h.v));
      c = mix(c, col.vein, Math.min(1, 0.85 * vs.mid + 0.6 * vs.sec + 0.22 * vs.ter));
      for (const s of spots) { const d = Math.hypot(x - s.c[0], y - s.c[1]) / s.r; if (d < 1.6) c = mix(c, d < 1 ? [104, 78, 44] : [150, 140, 60], d < 1 ? 0.85 : 0.5 * (1.6 - d) / 0.6); }
      return { c, cls: 1 };
    }
    for (const st of leaf.stems) { const h = hitStem(st, x, y); if (h) { const sh = 1 - 0.08 * (h.d / h.w); return { c: col.stem.map(v => v * sh), cls: 1 }; } }
    return null;
  };
  for (let j = 0; j < S; j++) for (let i = 0; i < S; i++) {
    let cov = 0, cs = [0, 0, 0], cls = 0;
    for (let sy = 0; sy < SS; sy++) for (let sx = 0; sx < SS; sx++) {
      const [x, y] = toLeaf(i - 0.5 + (sx + 0.5) / SS, j - 0.5 + (sy + 0.5) / SS), s = sample(x, y);
      if (s) { cov++; cs[0] += s.c[0]; cs[1] += s.c[1]; cs[2] += s.c[2]; cls = Math.max(cls, s.cls); }
    }
    const k = j * S + i;
    if (cov) { lay.a[k] = cov / (SS * SS); for (let q = 0; q < 3; q++) lay.rgb[k * 3 + q] = cs[q] / cov; lay.cls[k] = cls; }
  }
  return lay;
}

function dilate(lay) {   // breadth-first: every transparent texel takes the colour of its nearest leaf texel
  const seen = new Uint8Array(S * S); let q = [];
  for (let k = 0; k < S * S; k++) if (lay.a[k] > 0) { seen[k] = 1; q.push(k); }
  if (!q.length) return;
  while (q.length) {
    const nq = [];
    for (const k of q) { const i = k % S, j = (k / S) | 0;
      for (const [di, dj] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) { const ii = i + di, jj = j + dj; if (ii < 0 || jj < 0 || ii >= S || jj >= S) continue;
        const n = jj * S + ii; if (seen[n]) continue; seen[n] = 1; for (let c = 0; c < 3; c++) lay.rgb[n * 3 + c] = lay.rgb[k * 3 + c]; nq.push(n); } }
    q = nq;
  }
}

function sampleLayer(lay, u, v) {   // bilinear, uv over the whole layer; -> [r,g,b,a]
  const x = u * S - 0.5, y = v * S - 0.5, i = Math.floor(x), j = Math.floor(y), fx = x - i, fy = y - j, out = [0, 0, 0, 0];
  for (const [di, dj, w] of [[0, 0, (1 - fx) * (1 - fy)], [1, 0, fx * (1 - fy)], [0, 1, (1 - fx) * fy], [1, 1, fx * fy]]) {
    const ii = clamp(i + di, 0, S - 1), jj = clamp(j + dj, 0, S - 1), k = jj * S + ii;
    for (let c = 0; c < 3; c++) out[c] += lay.rgb[k * 3 + c] * w; out[3] += lay.a[k] * w;
  }
  return out;
}

// ------------------------------------------------------------------ fitted hulls (engine near / mid leaf meshes)
// Rows of the alpha give the leaf's half width each side of the midrib column (u = 0.5). near: hexagon base B, L1, L2,
// tip T, R2, R1 around the midrib (6 triangles fanned from a midrib vertex M), mid: diamond B, L, T, R (2 triangles).
// Both contain every texel with alpha > 0, so the alpha test, not the mesh, draws the outline.
function fitHull(lay) {
  const wl = new Float32Array(S), wr = new Float32Array(S); let j0 = S, j1 = -1;
  for (let j = 0; j < S; j++) for (let i = 0; i < S; i++) if (lay.a[j * S + i] > 0.01) {
    const du = (i + 1.5) / S - 0.5, dl = 0.5 - (i - 0.5) / S;
    if (du > 0) wr[j] = Math.max(wr[j], du); if (dl > 0) wl[j] = Math.max(wl[j], dl); j0 = Math.min(j0, j); j1 = Math.max(j1, j);
  }
  const vb0 = j0 / S - 0.02, vt = Math.min(1, (j1 + 1) / S + 0.025);
  const rows = []; for (let j = j0; j <= j1; j++) rows.push({ v0: j / S, v1: (j + 1) / S, l: wl[j], r: wr[j] });
  const seg = (side, a, b, wa) => {   // min width at b so the line (a, wa) -> (b, x) covers rows in [a, b]
    let x = 0;
    for (const r of rows) for (const v of [r.v0, r.v1]) { if (v <= a || v > b) continue; const t = (v - a) / (b - a), w = r[side]; x = Math.max(x, (w - wa * (1 - t)) / t); }
    return x;
  };
  const segDown = (side, a, b) => {   // min width at a so the line (a, x) -> (b, 0) covers rows in [a, b)
    let x = 0;
    for (const r of rows) for (const v of [r.v0, r.v1]) { if (v < a || v >= b) continue; x = Math.max(x, r[side] * (b - a) / (b - v)); }
    return x;
  };
  // the base vertex may sit below the leaf (cordate / wide bases), even below the tile: the sampler clamps to the
  // empty gutter there. Widths are not clamped, so the hull always contains the leaf.
  let best = null, mid = null;
  const steps = 24;
  for (const e of [0, 0.04, 0.08, 0.13, 0.2]) {
    const vb = vb0 - e;
    for (let p = 1; p < steps - 1; p++) for (let q = p + 1; q < steps; q++) {
      const v1 = vb + (vt - vb) * p / steps, v2 = vb + (vt - vb) * q / steps;
      let area = 0; const res = {};
      for (const side of ['l', 'r']) {
        let bs = null;
        for (const f of [1, 1.1, 1.25, 1.45, 1.7]) {
          const a1 = seg(side, vb, v1, 0) * f, a2 = Math.max(seg(side, v1, v2, a1), segDown(side, v2, vt));
          const ar = 0.5 * (v1 - vb) * a1 + 0.5 * (a1 + a2) * (v2 - v1) + 0.5 * a2 * (vt - v2);
          if (!bs || ar < bs.ar) bs = { a1, a2, ar };
        }
        res[side] = bs; area += bs.ar;
      }
      if (!best || area < best.area) best = { area, vb, v1, v2, l1: res.l.a1, l2: res.l.a2, r1: res.r.a1, r2: res.r.a2 };
    }
    for (let p = 1; p < steps; p++) {
      const vm = vb + (vt - vb) * p / steps, l = Math.max(seg('l', vb, vm, 0), segDown('l', vm, vt)), r = Math.max(seg('r', vb, vm, 0), segDown('r', vm, vt));
      const area = 0.5 * (vt - vb) * (l + r); if (!mid || area < mid.area) mid = { area, vb, vm, l, r };
    }
  }
  let opaque = 0; for (let k = 0; k < S * S; k++) opaque += lay.a[k];
  const r4 = (x) => Math.round(x * 1e4) / 1e4;
  const outline = []; for (const r of rows) if (r.l > 0 || r.r > 0) outline.push([0.5 - r.l, r.v0], [0.5 + r.r, r.v0], [0.5 - r.l, r.v1], [0.5 + r.r, r.v1]);
  return { vt: r4(vt), near: [best.vb, best.v1, best.v2, best.l1, best.l2, best.r1, best.r2].map(r4), mid: [mid.vb, mid.vm, mid.l, mid.r].map(r4),
    fillNear: r4(opaque / (S * S) / best.area), fillMid: r4(opaque / (S * S) / mid.area), outline };
}

// ------------------------------------------------------------------ sprigs: leaves on a twig (far cards)
function makeSprig(sp, leafLayers, hulls, firstLayer, R) {
  const k = sp.sprig.k, len0 = sp.sprig.len, alpha = R() * Math.PI * 2, leaves = [];
  const ax = [Math.sin(alpha), Math.cos(alpha)];   // twig direction in card uv (clockwise angle from +v)
  const t0 = -0.2, span = k > 3 ? 0.26 : 0.12;
  for (let i = 0; i < k; i++) {
    const last = i === k - 1, t = t0 + 0.06 + span * (k === 1 ? 1 : i / (k - 1));
    const side = i % 2 ? 1 : -1, a = last ? alpha + (R() - 0.5) * 10 * deg : alpha + side * (sp.sprig.spread + (R() - 0.5) * 24) * deg;
    const vi = (R() * leafLayers.length) | 0;
    leaves.push({ layer: firstLayer + vi, x: 0.5 + ax[0] * t, y: 0.5 + ax[1] * t, ang: a, len: len0 * (0.88 + 0.24 * R()) * (last ? 1.05 : 1) });
  }
  let twig = { a: [0.5 + ax[0] * t0, 0.5 + ax[1] * t0], b: [leaves[k - 1].x, leaves[k - 1].y], w: 0.0065 };
    const pts = (lf) => hulls[lf.layer - firstLayer].outline.map(([u, v]) => cardOf(lf, u, v));   // the leaf texels (not the looser hull) stay on the card
  const all = () => [twig.a, twig.b, ...leaves.flatMap(pts)];
  // recentre, then scale about the centre until every leaf stays inside the gutter
  let P = all(), mnx = Math.min(...P.map(p => p[0])), mxx = Math.max(...P.map(p => p[0])), mny = Math.min(...P.map(p => p[1])), mxy = Math.max(...P.map(p => p[1]));
  const sx = 0.5 - (mnx + mxx) / 2, sy = 0.5 - (mny + mxy) / 2;
  for (const lf of leaves) { lf.x += sx; lf.y += sy; } twig.a = [twig.a[0] + sx, twig.a[1] + sy]; twig.b = [twig.b[0] + sx, twig.b[1] + sy];
  const m = 0.5 - (G + 2) / S; let f = 1;
  for (const p of all()) f = Math.min(f, m / Math.max(1e-4, Math.abs(p[0] - 0.5)), m / Math.max(1e-4, Math.abs(p[1] - 0.5)));
  const sc = (p) => [0.5 + (p[0] - 0.5) * f, 0.5 + (p[1] - 0.5) * f];
  for (const lf of leaves) { [lf.x, lf.y] = sc([lf.x, lf.y]); lf.len *= f; }
  twig = { a: sc(twig.a), b: sc(twig.b), w: twig.w };
  return { leaves, twig };
}
function cardOf(lf, u, v) {   // leaf layer uv -> card uv (anchor at the petiole base, leaf axis at angle ang)
  const lx = (u - 0.5) * lf.len, ly = (v - G / S) * lf.len, s = Math.sin(lf.ang), c = Math.cos(lf.ang);
  return [lf.x + lx * c + ly * s, lf.y - lx * s + ly * c];
}
function renderSprig(sp, sprig, layers, firstLayer) {
  const lay = mkLayer();
  for (let j = 0; j < S; j++) for (let i = 0; i < S; i++) {
    const cu = (i + 0.5) / S, cv = (j + 0.5) / S, k = j * S + i;
    let rgb = [0, 0, 0], a = 0;
    { const t = hitStem({ a: sprig.twig.a, b: sprig.twig.b, w0: sprig.twig.w, w1: sprig.twig.w * 0.7 }, cu, cv);   // twig under the leaves
      if (t) { a = clamp((t.w - t.d) * S + 0.5, 0, 1); rgb = sp.col.stem.map(c => c * 0.8); } }
    for (const lf of sprig.leaves) {   // painter's order: later leaves on top
      const s = Math.sin(lf.ang), c = Math.cos(lf.ang), dx = cu - lf.x, dy = cv - lf.y;
      const lx = dx * c - dy * s, ly = dx * s + dy * c, u = lx / lf.len + 0.5, v = ly / lf.len + G / S;
      if (u < 0 || v < 0 || u > 1 || v > 1) continue;
      const t = sampleLayer(layers[lf.layer - firstLayer], u, v);
      if (t[3] <= 0) continue;
      const na = t[3] + a * (1 - t[3]);
      rgb = rgb.map((x, q) => (t[q] * t[3] + x * a * (1 - t[3])) / na); a = na;
      if (t[3] > 0.5) lay.cls[k] = 1;
    }
    lay.a[k] = a; for (let q = 0; q < 3; q++) lay.rgb[k * 3 + q] = rgb[q];
  }
  return lay;
}

// ------------------------------------------------------------------ PNG
const crcT = new Uint32Array(256).map((_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
const crc = (b) => { let c = 0xFFFFFFFF; for (const x of b) c = crcT[(c ^ x) & 255] ^ (c >>> 8); return (c ^ 0xFFFFFFFF) >>> 0; };
function png(w, h, rgba) {   // rgba: Uint8Array, rows top-down
  const chunk = (t, d) => { const len = Buffer.alloc(4); len.writeUInt32BE(d.length); const td = Buffer.concat([Buffer.from(t), d]); const c = Buffer.alloc(4); c.writeUInt32BE(crc(td)); return Buffer.concat([len, td, c]); };
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 6;
  const raw = Buffer.alloc((w * 4 + 1) * h); for (let y = 0; y < h; y++) { raw[y * (w * 4 + 1)] = 0; Buffer.from(rgba.buffer, rgba.byteOffset + y * w * 4, w * 4).copy(raw, y * (w * 4 + 1) + 1); }
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0))]);
}

// ------------------------------------------------------------------ 5x7 label font (contact sheet)
const FONT = { A: '01110100011000111111100011000110001', B: '11110100011000111110100011000111110', C: '01110100011000010000100001000101110', D: '11110100011000110001100011000111110',
  E: '11111100001000011110100001000011111', F: '11111100001000011110100001000010000', G: '01110100011000010111100011000101111', H: '10001100011000111111100011000110001',
  I: '01110001000010000100001000010001110', J: '00111000100001000010000101001001100', K: '10001100101010011000101001001010001', L: '10000100001000010000100001000011111',
  M: '10001110111010110101100011000110001', N: '10001100011100110101100111000110001', O: '01110100011000110001100011000101110', P: '11110100011000111110100001000010000',
  Q: '01110100011000110001101011001001101', R: '11110100011000111110101001001010001', S: '01111100001000001110000010000111110', T: '11111001000010000100001000010000100',
  U: '10001100011000110001100011000101110', V: '10001100011000110001100010101000100', W: '10001100011000110101101011010101010', X: '10001100010101000100010101000110001',
  Y: '10001100010101000100001000010000100', Z: '11111000010001000100010001000011111', 0: '01110100011001110101110011000101110', 1: '00100011000010000100001000010001110',
  2: '01110100010000100010001000100011111', 3: '11111000100010000010000011000101110', 4: '00010001100101010010111110001000010', 5: '11111100001111000001000011000101110',
  6: '00110010001000011110100011000101110', 7: '11111000010001000100010000100001000', 8: '01110100011000101110100011000101110', 9: '01110100011000101111000010001001100',
  '-': '00000000000000011111000000000000000', ' ': '00000000000000000000000000000000000' };
function text(img, W, x0, y0, str, sc, col) {
  let x = x0;
  for (const ch of str.toUpperCase()) { const g = FONT[ch] || FONT[' '];
    for (let r = 0; r < 7; r++) for (let c = 0; c < 5; c++) if (g[r * 5 + c] === '1')
      for (let dy = 0; dy < sc; dy++) for (let dx = 0; dx < sc; dx++) { const k = ((y0 + r * sc + dy) * W + x + c * sc + dx) * 4; img[k] = col[0]; img[k + 1] = col[1]; img[k + 2] = col[2]; img[k + 3] = 255; }
    x += 6 * sc; }
}

// ------------------------------------------------------------------ build
const ASSETS = {   // asset name (or prefix*) -> leaf species (several: per-cluster mix, docs/leaves.md)
  tree_oak: ['oak'], nat_sapling_oak: ['oak'], tree_ash: ['ash'], nat_sapling_ash: ['ash'], tree_apple: ['apple'], tree_poplar: ['poplar'],
  nat_hawthorn_bush: ['hawthorn'], nat_blackthorn_low: ['blackthorn'], nat_gorse_shrub: ['gorse'], shrub_hazel: ['hazel'],
  shrub_bramble: ['bramble'], nat_bramble_thicket: ['bramble'],
  hedgerow_bocage: ['hawthorn', 'blackthorn', 'hazel'], 'bcg_hedgerow_*': ['hawthorn', 'blackthorn', 'hazel'], bcg_rampart: ['hawthorn', 'blackthorn', 'hazel'], bcg_sunken_lane: ['hawthorn', 'blackthorn', 'hazel'],
};

function main() {
  fs.mkdirSync(outDir, { recursive: true });
  const layers = [], labels = [], hullsAll = [], species = {};
  let seed = 1000;
  for (const [id, sp] of Object.entries(SPECIES)) {
    const first = layers.length, leafLayers = [], hulls = [];
    for (let v = 0; v < sp.variants; v++) {
      const R = rngOf(++seed * 7919), leaf = sp.make(v, R), lay = renderLeaf(sp, leaf, R);
      hulls.push(fitHull(lay)); dilate(lay); leafLayers.push(lay); layers.push(lay); labels.push(`${id} ${v + 1}`); hullsAll.push(hulls[hulls.length - 1]);
    }
    const sprigs = [];
    for (let s = 0; s < 2; s++) {
      const R = rngOf(++seed * 104729), sg = makeSprig(sp, leafLayers, hulls, first, R), lay = renderSprig(sp, sg, leafLayers, first);
      dilate(lay); sprigs.push({ layer: layers.length, twig: [...sg.twig.a, ...sg.twig.b, sg.twig.w].map(x => Math.round(x * 1e4) / 1e4),
        leaves: sg.leaves.map(l => ({ layer: l.layer, x: +l.x.toFixed(4), y: +l.y.toFixed(4), ang: +l.ang.toFixed(4), len: +l.len.toFixed(4) })) });
      layers.push(lay); labels.push(`${id} sprig ${s + 1}`); hullsAll.push(null);
    }
    // mean leaf colour (linear, leaf texels only): the engine tints a layer by the asset's leafColor / this
    let m = [0, 0, 0], w = 0;
    for (const lay of leafLayers) for (let k = 0; k < S * S; k++) if (lay.cls[k] === 1 && lay.a[k] > 0.5) { const l = toLin([lay.rgb[k * 3], lay.rgb[k * 3 + 1], lay.rgb[k * 3 + 2]]); m = m.map((x, q) => x + l[q]); w++; }
    species[id] = { name: sp.name, base: m.map(x => +(x / w).toFixed(5)), leaves: leafLayers.map((_, i) => first + i), sprigs };
    console.log(`${id.padEnd(11)} layers ${first}-${layers.length - 1}  fill near ${hulls.map(h => h.fillNear).join('/')}  mid ${hulls.map(h => h.fillMid).join('/')}`);
  }
  // atlas: COLS x rows layers, rows top-down in the file; each layer's texel row 0 (leaf base) is its bottom row
  const rowsN = Math.ceil(layers.length / COLS), AW = COLS * S, AH = rowsN * S, atlas = new Uint8Array(AW * AH * 4);
  layers.forEach((lay, n) => { const cx = (n % COLS) * S, cy = ((n / COLS) | 0) * S;
    for (let j = 0; j < S; j++) for (let i = 0; i < S; i++) { const k = j * S + i, o = ((cy + S - 1 - j) * AW + cx + i) * 4;
      for (let q = 0; q < 3; q++) atlas[o + q] = clamp(Math.round(lay.rgb[k * 3 + q]), 0, 255); atlas[o + 3] = clamp(Math.round(lay.a[k] * 255), 0, 255); } });
  fs.writeFileSync(path.join(outDir, 'leaves.png'), png(AW, AH, atlas));
  // contact sheet: each layer over bright sky (left half) and a dark wall (right half), labelled
  const T = S, LH = 26, CW = COLS * T, CH = rowsN * (T + LH), sheet = new Uint8Array(CW * CH * 4);
  layers.forEach((lay, n) => { const cx = (n % COLS) * T, cy = ((n / COLS) | 0) * (T + LH);
    for (let j = 0; j < T; j++) for (let i = 0; i < T; i++) { const k = j * S + i, a = lay.a[k], bg = i < T / 2 ? [186, 208, 232] : [44, 38, 34], o = ((cy + T - 1 - j) * CW + cx + i) * 4;
      for (let q = 0; q < 3; q++) sheet[o + q] = clamp(Math.round(lay.rgb[k * 3 + q] * a + bg[q] * (1 - a)), 0, 255); sheet[o + 3] = 255; }
    for (let j = 0; j < LH; j++) for (let i = 0; i < T; i++) { const o = ((cy + T + j) * CW + cx + i) * 4; sheet[o] = sheet[o + 1] = sheet[o + 2] = 235; sheet[o + 3] = 255; }
    text(sheet, CW, cx + 6, cy + T + 6, labels[n], 2, [30, 30, 30]); });
  fs.writeFileSync(path.join(outDir, 'contact_sheet.png'), png(CW, CH, sheet));
  const doc = { _doc: 'L1 species leaf textures (docs/leaves.md), written by tools/gen_leaf_textures.mjs: do not edit by hand. atlas: layers of layerSize texels, cols per row, top-down; '
      + 'layer texel row 0 = the bottom of its tile (leaf base). hulls[layer] (leaf layers; null = sprig): vt tip v, near [vb, v1, v2, l1, l2, r1, r2], mid [vb, vm, l, r] (uv, widths from u = 0.5). '
      + 'sprigs: the card layer plus its leaves (card uv anchor x/y at the petiole base, ang clockwise from +v, len = layer extent in card uv) and twig [ax, ay, bx, by, halfWidth]. '
      + 'base: mean linear leaf colour (asset leafColor / base = tint). assets: asset name or prefix* -> species (several: chosen per canopy cluster).',
    layerSize: S, gutter: G, cols: COLS, atlas: 'leaves.png', layers: layers.length, species, hulls: hullsAll.map(h => h && { vt: h.vt, near: h.near, mid: h.mid }), assets: ASSETS };
  fs.writeFileSync(path.join(outDir, 'leaves.json'), JSON.stringify(doc, null, 1) + '\n');
  // self-check: every sprig leaf stays inside the card
  for (const [id, s] of Object.entries(species)) for (const sg of s.sprigs) for (const lf of sg.leaves) for (const [u, v] of hullsAll[lf.layer].outline) {
    const p = cardOf(lf, u, v); if (p[0] < G / S - 1e-3 || p[1] < G / S - 1e-3 || p[0] > 1 - G / S + 1e-3 || p[1] > 1 - G / S + 1e-3) throw new Error(`${id}: sprig leaf leaves the card`); }
  console.log(`wrote ${layers.length} layers (${AW}x${AH}) to ${path.relative(root, outDir)}`);
}
main();
