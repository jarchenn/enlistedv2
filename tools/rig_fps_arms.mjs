// FA1: rig the first-person arms model ("Fps arms" by DJMaesen, CC-BY-4.0; docs/credits.md, docs/fps-arms.md).
//   node tools/rig_fps_arms.mjs <fps_arms.glb> [outDir = content/viewmodel]
// The source is one static mesh (both arms, no skeleton). This tool, deterministically:
//  1. bakes the node transforms and converts to metres (source units are cm); arms lie along +z (hand forward),
//     palms facing each other, thumbs up / inward;
//  2. splits the two arms (the two connected components; the right arm is the one at x < 0: see SIDES);
//  3. per arm, builds 9 bones and smooth weights along the arm:
//       0 upper arm (stub up to the elbow), 1 forearm, 2 forearm twist (weight rising elbow -> wrist), 3 hand,
//       4 / 5 fingers (proximal / distal, middle-ring-pinky), 6 / 7 index finger, 8 thumb;
//     the elbow sits one forearm length (0.26 m) behind the wrist (the skin strip between cuff and glove);
//  4. recolours the sleeve per faction (US olive drab, German field grey) keeping the cloth detail, and the gloves to the
//     skin tone of the bare strip at the wrist (bare hands, as most soldiers); no metallic-roughness texture (the source's
//     reads as metallic 1): matte, roughness 0.85; one primitive per arm (right, left);
//  5. writes fps_arms_us.glb / fps_arms_de.glb (skinned: 18 joints, inverse binds = the rest frames) and
//     fps_arms.json (rest landmarks the viewmodel poses from; src/fps_arms.cpp).
import fs from "fs";
import path from "path";
import zlib from "zlib";

const SRC = process.argv[2], OUT = process.argv[3] || "content/viewmodel";
if (!SRC) { console.error("usage: node tools/rig_fps_arms.mjs <fps_arms.glb> [outDir]"); process.exit(2); }

// ---------------------------------------------------------------- PNG (8-bit RGB / RGBA / palette) decode, RGB encode
const crcT = new Int32Array(256).map((_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; return c; });
const crc = b => { let c = -1; for (const x of b) c = crcT[(c ^ x) & 255] ^ (c >>> 8); return (c ^ -1) >>> 0; };
function pngDecode(buf) {
  let p = 8, w, h, ct, plte, idat = [];
  while (p < buf.length) { const len = buf.readUInt32BE(p), t = buf.toString("ascii", p + 4, p + 8), d = buf.subarray(p + 8, p + 8 + len); p += 12 + len;
    if (t === "IHDR") { w = d.readUInt32BE(0); h = d.readUInt32BE(4); ct = d[9]; if (d[8] !== 8 || d[12]) throw new Error("png: 8-bit non-interlaced only"); } else if (t === "PLTE") plte = d; else if (t === "IDAT") idat.push(d); }
  const raw = zlib.inflateSync(Buffer.concat(idat)), ch = ct === 2 ? 3 : ct === 6 ? 4 : ct === 4 ? 2 : 1, stride = w * ch;
  const out = Buffer.alloc(w * h * 4), cur = Buffer.alloc(stride), prev = Buffer.alloc(stride);
  for (let y = 0; y < h; y++) { const f = raw[y * (stride + 1)], s = raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1));
    for (let x = 0; x < stride; x++) { const a = x >= ch ? cur[x - ch] : 0, b = prev[x], c = x >= ch ? prev[x - ch] : 0; let v = s[x];
      if (f === 1) v += a; else if (f === 2) v += b; else if (f === 3) v += (a + b) >> 1; else if (f === 4) { const q = a + b - c, pa = Math.abs(q - a), pb = Math.abs(q - b), pc = Math.abs(q - c); v += pa <= pb && pa <= pc ? a : pb <= pc ? b : c; }
      cur[x] = v & 255; }
    for (let x = 0; x < w; x++) { const o = (y * w + x) * 4;
      if (ct === 3) { const i = cur[x]; out[o] = plte[i * 3]; out[o + 1] = plte[i * 3 + 1]; out[o + 2] = plte[i * 3 + 2]; }
      else if (ch <= 2) out[o] = out[o + 1] = out[o + 2] = cur[x * ch];
      else { out[o] = cur[x * ch]; out[o + 1] = cur[x * ch + 1]; out[o + 2] = cur[x * ch + 2]; }
      out[o + 3] = 255; }
    cur.copy(prev); }
  return { w, h, data: out };
}
function pngEncodeRGB(w, h, rgba) {
  const raw = Buffer.alloc((w * 3 + 1) * h);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) for (let c = 0; c < 3; c++) raw[y * (w * 3 + 1) + 1 + x * 3 + c] = rgba[(y * w + x) * 4 + c];
  const chunk = (t, d) => { const l = Buffer.alloc(4); l.writeUInt32BE(d.length); const td = Buffer.concat([Buffer.from(t), d]); const c = Buffer.alloc(4); c.writeUInt32BE(crc(td)); return Buffer.concat([l, td, c]); };
  const ih = Buffer.alloc(13); ih.writeUInt32BE(w); ih.writeUInt32BE(h, 4); ih[8] = 8; ih[9] = 2;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk("IHDR", ih), chunk("IDAT", zlib.deflateSync(raw, { level: 9 })), chunk("IEND", Buffer.alloc(0))]);
}

// ---------------------------------------------------------------- read the source GLB
const glb = fs.readFileSync(SRC);
const jl = glb.readUInt32LE(12), J = JSON.parse(glb.subarray(20, 20 + jl).toString()), BIN = glb.subarray(20 + jl + 8);
const acc = i => { const a = J.accessors[i], v = J.bufferViews[a.bufferView]; const n = { SCALAR: 1, VEC2: 2, VEC3: 3, VEC4: 4 }[a.type];
  const T = { 5126: Float32Array, 5125: Uint32Array, 5123: Uint16Array }[a.componentType];
  const off = (v.byteOffset || 0) + (a.byteOffset || 0), st = v.byteStride ? v.byteStride / T.BYTES_PER_ELEMENT : n, out = [];
  const arr = new T(BIN.buffer, BIN.byteOffset + off, (a.count - 1) * st + n); for (let k = 0; k < a.count; k++) out.push(Array.from(arr.slice(k * st, k * st + n))); return out; };
const image = i => { const v = J.bufferViews[J.images[i].bufferView]; return BIN.subarray(v.byteOffset || 0, (v.byteOffset || 0) + v.byteLength); };
// node world matrices (column-major 4x4)
const mul = (a, b) => { const o = new Array(16).fill(0); for (let c = 0; c < 4; c++) for (let r = 0; r < 4; r++) for (let k = 0; k < 4; k++) o[c * 4 + r] += a[k * 4 + r] * b[c * 4 + k]; return o; };
const I4 = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
let meshWorld = null;
const walk = (n, M) => { const nd = J.nodes[n]; const W = mul(M, nd.matrix || I4); if (nd.mesh === 0) meshWorld = W; for (const c of nd.children || []) walk(c, W); };
for (const r of J.scenes[J.scene || 0].nodes) walk(r, I4);
const xf = (M, p, w) => [0, 1, 2].map(r => M[r] * p[0] + M[4 + r] * p[1] + M[8 + r] * p[2] + (w ? M[12 + r] : 0));
const pr = J.meshes[0].primitives[0];
const P = acc(pr.attributes.POSITION).map(p => xf(meshWorld, p, 1).map(v => v * 0.01));   // metres
const N = acc(pr.attributes.NORMAL).map(n => { const q = xf(meshWorld, n, 0); const l = Math.hypot(...q) || 1; return q.map(v => v / l); });
const UV = acc(pr.attributes.TEXCOORD_0), IDX = acc(pr.indices).map(x => x[0]);

// ---------------------------------------------------------------- geometry helpers
const sub = (a, b) => a.map((v, i) => v - b[i]), add = (a, b) => a.map((v, i) => v + b[i]), scl = (a, s) => a.map(v => v * s);
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2], len = a => Math.hypot(...a), nrm = a => scl(a, 1 / (len(a) || 1));
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const ss = (e0, e1, x) => { const t = Math.min(1, Math.max(0, (x - e0) / (e1 - e0))); return t * t * (3 - 2 * t); };
const centroid = pts => scl(pts.reduce((s, p) => add(s, p), [0, 0, 0]), 1 / Math.max(1, pts.length));
// a frame: origin, X along the bone, Y the given reference made perpendicular, Z = X x Y; column-major mat4
const frame = (o, X, Yref) => { X = nrm(X); const Y = nrm(sub(Yref, scl(X, dot(Yref, X)))), Z = cross(X, Y); return [...X, 0, ...Y, 0, ...Z, 0, ...o, 1]; };
const inv = m => {   // rigid inverse
  const R = [[m[0], m[1], m[2]], [m[4], m[5], m[6]], [m[8], m[9], m[10]]], t = [m[12], m[13], m[14]];
  const o = [R[0][0], R[1][0], R[2][0], 0, R[0][1], R[1][1], R[2][1], 0, R[0][2], R[1][2], R[2][2], 0, 0, 0, 0, 1];
  for (let r = 0; r < 3; r++) o[12 + r] = -(o[r] * t[0] + o[4 + r] * t[1] + o[8 + r] * t[2]);
  return o;
};

// ---------------------------------------------------------------- per arm: landmarks, bones, weights
// The source points its arms along +z, the camera looks along -z: the arm at x < 0 is the RIGHT arm (looking down +z,
// -x is on the right; its palm faces +x, thumb up). Inward = toward the other arm.
const SIDES = { R: { pick: p => p[0] <= 0, inward: 1, off: 0 }, L: { pick: p => p[0] > 0, inward: -1, off: 9 } };   // the two components sit either side of x = 0
const WRIST_Z = 0.195, FOREARM = 0.26, KNUCKLE_Z = 0.34, MID_Z = 0.395;
const joints = new Array(P.length).fill(null).map(() => []);
const rest = {}, frames = new Array(18);
for (const [s, S] of Object.entries(SIDES)) {
  const ids = P.map((p, i) => i).filter(i => S.pick(P[i]));
  const slice = (z0, z1, f = () => true) => centroid(ids.filter(i => P[i][2] >= z0 && P[i][2] < z1 && f(i)).map(i => P[i]));
  const zmin = Math.min(...ids.map(i => P[i][2]));
  const end = slice(zmin, zmin + 0.03), wrist = slice(WRIST_Z - 0.01, WRIST_Z + 0.01), elbowZ = WRIST_Z - FOREARM;
  const elbow = slice(elbowZ - 0.02, elbowZ + 0.02);
  const cxW = slice(0.20, 0.25)[0];
  const thumbW = i => ss(0.04, 0.065, (P[i][0] - cxW) * S.inward) * ss(0.26, 0.29, P[i][2]) * (1 - ss(0.385, 0.40, P[i][2])) * ss(-0.075, -0.06, P[i][1]);   // not the curled-in fingertips (the pinky sits below the thumb)
  const fingersOnly = i => thumbW(i) < 0.5;
  const knuckle = slice(KNUCKLE_Z - 0.01, KNUCKLE_Z + 0.01, fingersOnly), mid = slice(MID_Z - 0.01, MID_Z + 0.01, fingersOnly);
  const tip = slice(0.42, 0.47, fingersOnly);
  const thumbIds = ids.filter(i => thumbW(i) > 0.5);
  const thumbBase = centroid(thumbIds.filter(i => P[i][2] < 0.30).map(i => P[i])), thumbTip = centroid(thumbIds.filter(i => P[i][2] > 0.32).map(i => P[i]));
  // the index finger: the top 2.8 cm of the fingers in each slice
  const topY = z => Math.max(...ids.filter(i => fingersOnly(i) && Math.abs(P[i][2] - z) < 0.006).map(i => P[i][1]), -1);
  const yTop = []; for (let z = 0.33; z <= 0.47; z += 0.005) yTop.push([z, topY(z)]);
  const topAt = z => { let best = yTop[0]; for (const q of yTop) if (Math.abs(q[0] - z) < Math.abs(best[0] - z)) best = q; return best[1]; };
  const down = [0, -1, 0];   // rest: elbow point and pinky side both face -y
  frames[S.off + 0] = frame(elbow, sub(end, elbow), down);
  frames[S.off + 1] = frame(elbow, sub(wrist, elbow), down);
  frames[S.off + 2] = frames[S.off + 1];
  frames[S.off + 3] = frame(wrist, sub(knuckle, wrist), down);
  for (const k of [4, 5, 6, 7, 8]) frames[S.off + k] = frames[S.off + 3];   // fingers / thumb: hand frame (the pivots are landmarks)
  rest[s] = { end, elbow, wrist, knuckle, mid, tip, thumbBase, thumbTip, forearm: len(sub(wrist, elbow)), upperStub: len(sub(end, elbow)) };
  for (const i of ids) {
    const z = P[i][2], w = {};
    const tE = ss(elbowZ - 0.04, elbowZ + 0.04, z), u = Math.min(1, Math.max(0, (z - elbowZ) / (WRIST_Z - elbowZ)));
    const h = ss(WRIST_Z - 0.015, WRIST_Z + 0.02, z);
    w[0] = 1 - tE;
    w[1] = tE * (1 - u) * (1 - h);
    w[2] = tE * u * (1 - h);
    const th = thumbW(i), f1 = ss(KNUCKLE_Z - 0.01, KNUCKLE_Z + 0.012, z), f2 = ss(MID_Z - 0.008, MID_Z + 0.008, z);
    const ix = z > 0.33 ? ss(topAt(z) - 0.032, topAt(z) - 0.024, P[i][1]) : 0;
    w[3] = h * (1 - th) * (1 - f1);
    w[4] = h * (1 - th) * f1 * (1 - f2) * (1 - ix); w[5] = h * (1 - th) * f1 * f2 * (1 - ix);
    w[6] = h * (1 - th) * f1 * (1 - f2) * ix; w[7] = h * (1 - th) * f1 * f2 * ix;
    w[8] = h * th;
    const top = Object.entries(w).map(([k, v]) => [S.off + +k, v]).filter(q => q[1] > 1e-4).sort((a, b) => b[1] - a[1]).slice(0, 4);
    const sum = top.reduce((t, q) => t + q[1], 0);
    joints[i] = top.map(q => [q[0], q[1] / sum]);
  }
}

// ---------------------------------------------------------------- sleeve recolour (UV raster of the sleeve triangles)
const alb = pngDecode(image(J.materials[0].pbrMetallicRoughness.baseColorTexture.index));
const mask = new Uint8Array(alb.w * alb.h);
for (let t = 0; t < IDX.length; t += 3) {
  const v = [IDX[t], IDX[t + 1], IDX[t + 2]];
  if (!v.every(i => P[i][2] < WRIST_Z)) continue;   // sleeve and its cuff hem (the skin strip is skipped by its colour below)
  const q = v.map(i => [UV[i][0] * alb.w, UV[i][1] * alb.h]);
  const d = (q[1][0] - q[0][0]) * (q[2][1] - q[0][1]) - (q[2][0] - q[0][0]) * (q[1][1] - q[0][1]); if (Math.abs(d) < 1e-9) continue;
  const x0 = Math.max(0, Math.floor(Math.min(...q.map(a => a[0])) - 1)), x1 = Math.min(alb.w - 1, Math.ceil(Math.max(...q.map(a => a[0])) + 1));
  const y0 = Math.max(0, Math.floor(Math.min(...q.map(a => a[1])) - 1)), y1 = Math.min(alb.h - 1, Math.ceil(Math.max(...q.map(a => a[1])) + 1));
  const e = (a, b, x, y) => (b[0] - a[0]) * (y - a[1]) - (b[1] - a[1]) * (x - a[0]);
  for (let y = y0; y <= y1; y++) for (let x = x0; x <= x1; x++) {
    const w0 = e(q[1], q[2], x + 0.5, y + 0.5) / d, w1 = e(q[2], q[0], x + 0.5, y + 0.5) / d, w2 = e(q[0], q[1], x + 0.5, y + 0.5) / d;
    if (w0 >= -0.02 && w1 >= -0.02 && w2 >= -0.02) mask[y * alb.w + x] = 1;
  }
}
const lum = (r, g, b) => 0.2126 * r + 0.7152 * g + 0.0722 * b;
let lsum = 0, ln = 0;
for (let k = 0; k < mask.length; k++) if (mask[k]) { const o = k * 4; lsum += lum(alb.data[o], alb.data[o + 1], alb.data[o + 2]); ++ln; }
const lmean = lsum / Math.max(1, ln);
// the gloves become bare hands: the hand triangles' texels take the skin tone of the strip at the wrist (the glove's
// seams and folds kept faintly as luminance)
const hmask = new Uint8Array(alb.w * alb.h);
for (let t = 0; t < IDX.length; t += 3) {
  const v = [IDX[t], IDX[t + 1], IDX[t + 2]];
  if (!v.every(i => P[i][2] >= WRIST_Z + 0.004)) continue;
  const q = v.map(i => [UV[i][0] * alb.w, UV[i][1] * alb.h]);
  const d = (q[1][0] - q[0][0]) * (q[2][1] - q[0][1]) - (q[2][0] - q[0][0]) * (q[1][1] - q[0][1]); if (Math.abs(d) < 1e-9) continue;
  const x0 = Math.max(0, Math.floor(Math.min(...q.map(a => a[0])) - 1)), x1 = Math.min(alb.w - 1, Math.ceil(Math.max(...q.map(a => a[0])) + 1));
  const y0 = Math.max(0, Math.floor(Math.min(...q.map(a => a[1])) - 1)), y1 = Math.min(alb.h - 1, Math.ceil(Math.max(...q.map(a => a[1])) + 1));
  const e = (a, b, x, y) => (b[0] - a[0]) * (y - a[1]) - (b[1] - a[1]) * (x - a[0]);
  for (let y = y0; y <= y1; y++) for (let x = x0; x <= x1; x++) {
    const w0 = e(q[1], q[2], x + 0.5, y + 0.5) / d, w1 = e(q[2], q[0], x + 0.5, y + 0.5) / d, w2 = e(q[0], q[1], x + 0.5, y + 0.5) / d;
    if (w0 >= -0.02 && w1 >= -0.02 && w2 >= -0.02) hmask[y * alb.w + x] = 1;
  }
}
const sat = o => Math.max(alb.data[o], alb.data[o + 1], alb.data[o + 2]) - Math.min(alb.data[o], alb.data[o + 1], alb.data[o + 2]);
let skin = [0, 0, 0], sn = 0, hsum = 0, hn = 0;
for (let k = 0; k < hmask.length; k++) { const o = k * 4; if (sat(o) > 60) { for (let c = 0; c < 3; c++) skin[c] += alb.data[o + c]; ++sn; } else if (hmask[k]) { hsum += lum(alb.data[o], alb.data[o + 1], alb.data[o + 2]); ++hn; } }
skin = skin.map(v => v / Math.max(1, sn)); const hmean = hsum / Math.max(1, hn);
const recolour = target => {   // target: linear-ish albedo the sleeve's mean maps to (detail kept as luminance ratio)
  const out = Buffer.from(alb.data);
  for (let k = 0; k < mask.length; k++) {
    const o = k * 4, r = alb.data[o], g = alb.data[o + 1], b = alb.data[o + 2];
    if (sat(o) > 40) continue;   // saturated texels (the skin strip) stay
    if (mask[k]) { const f = Math.min(2.2, lum(r, g, b) / lmean); for (let c = 0; c < 3; c++) out[o + c] = Math.min(255, Math.round(Math.pow(target[c], 1 / 2.2) * 255 * Math.pow(f, 0.8))); }
    else if (hmask[k]) { const f = Math.min(1.6, lum(r, g, b) / hmean); for (let c = 0; c < 3; c++) out[o + c] = Math.min(255, Math.round(skin[c] * 0.92 * Math.pow(f, 0.3))); }
  }
  return pngEncodeRGB(alb.w, alb.h, out);
};

// ---------------------------------------------------------------- write the skinned GLBs
const f32 = a => Buffer.from(new Float32Array(a).buffer), u16 = a => Buffer.from(new Uint16Array(a).buffer), u32 = a => Buffer.from(new Uint32Array(a).buffer);
function writeGlb(file, albedoPng) {
  const views = [], accs = [], chunks = []; let off = 0;
  const view = (buf, target) => { const pad = (4 - (off % 4)) % 4; if (pad) { chunks.push(Buffer.alloc(pad)); off += pad; } views.push({ buffer: 0, byteOffset: off, byteLength: buf.length, ...(target ? { target } : {}) }); chunks.push(buf); off += buf.length; return views.length - 1; };
  const accessor = (buf, type, ct, count, target, minmax) => { accs.push({ bufferView: view(buf, target), componentType: ct, count, type, ...(minmax || {}) }); return accs.length - 1; };
  const flat = P.flat(), lo = [0, 1, 2].map(a => Math.min(...P.map(p => p[a]))), hi = [0, 1, 2].map(a => Math.max(...P.map(p => p[a])));
  const aP = accessor(f32(flat), "VEC3", 5126, P.length, 34962, { min: lo, max: hi });
  const aN = accessor(f32(N.flat()), "VEC3", 5126, N.length, 34962);
  const aUV = accessor(f32(UV.flat()), "VEC2", 5126, UV.length, 34962);
  const JN = [], WT = [];
  for (const j of joints) { for (let q = 0; q < 4; q++) { JN.push(j[q] ? j[q][0] : 0); WT.push(j[q] ? j[q][1] : 0); } }
  const aJ = accessor(u16(JN), "VEC4", 5123, P.length, 34962), aW = accessor(f32(WT), "VEC4", 5126, P.length, 34962);
  // one primitive per arm (right first): the viewmodel can hide one arm
  const side = [[], []]; for (let t = 0; t < IDX.length; t += 3) side[P[IDX[t]][0] <= 0 ? 0 : 1].push(IDX[t], IDX[t + 1], IDX[t + 2]);
  const aI = side.map(s => accessor(u32(s), "SCALAR", 5125, s.length, 34963));
  const aIB = accessor(f32(frames.map(inv).flat()), "MAT4", 5126, 18);
  // no metallic-roughness texture: the source's has metallic 1 everywhere (blue 255), which made glossy black metal arms
  const imgs = [albedoPng, image(J.materials[0].normalTexture.index)].map(b => ({ bufferView: view(b), mimeType: "image/png" }));
  const names = ["UpperArm", "ForeArm", "ForeArmTwist", "Hand", "Fingers1", "Fingers2", "Index1", "Index2", "Thumb"];
  const jointNodes = frames.map((m, k) => ({ name: (k < 9 ? "Right" : "Left") + names[k % 9], matrix: m }));
  const nodes = [{ name: "fps_arms", children: jointNodes.map((_, k) => k + 2) }, { name: "fps_arms_mesh", mesh: 0, skin: 0 }, ...jointNodes];
  const gj = {
    asset: { version: "2.0", generator: "tools/rig_fps_arms.mjs", extras: J.asset.extras },
    scene: 0, scenes: [{ nodes: [0, 1] }], nodes,
    skins: [{ joints: jointNodes.map((_, k) => k + 2), inverseBindMatrices: aIB }],
    meshes: [{ name: "fps_arms", primitives: aI.map(ai => ({ attributes: { POSITION: aP, NORMAL: aN, TEXCOORD_0: aUV, JOINTS_0: aJ, WEIGHTS_0: aW }, indices: ai, material: 0, mode: 4 })) }],
    materials: [{ name: "fps_arms", pbrMetallicRoughness: { baseColorTexture: { index: 0 }, metallicFactor: 0, roughnessFactor: 0.85 }, normalTexture: { index: 1 } }],
    textures: [{ source: 0, sampler: 0 }, { source: 1, sampler: 0 }], samplers: [J.samplers[0]], images: imgs,
    accessors: accs, bufferViews: views, buffers: [{ byteLength: off }],
  };
  let js = Buffer.from(JSON.stringify(gj)); js = Buffer.concat([js, Buffer.alloc((4 - js.length % 4) % 4, 0x20)]);
  let bin = Buffer.concat(chunks); bin = Buffer.concat([bin, Buffer.alloc((4 - bin.length % 4) % 4)]);
  const hdr = Buffer.alloc(12); hdr.write("glTF"); hdr.writeUInt32LE(2, 4); hdr.writeUInt32LE(12 + 8 + js.length + 8 + bin.length, 8);
  const ch = (b, t) => { const h = Buffer.alloc(8); h.writeUInt32LE(b.length); h.writeUInt32LE(t, 4); return Buffer.concat([h, b]); };
  fs.writeFileSync(file, Buffer.concat([hdr, ch(js, 0x4E4F534A), ch(bin, 0x004E4942)]));
}
fs.mkdirSync(OUT, { recursive: true });
writeGlb(path.join(OUT, "fps_arms_us.glb"), recolour([0.11, 0.10, 0.055]));   // olive drab wool
writeGlb(path.join(OUT, "fps_arms_de.glb"), recolour([0.12, 0.13, 0.11]));   // field grey
const r3 = v => v.map(x => Math.round(x * 1e4) / 1e4);
const side = s => Object.fromEntries(Object.entries(rest[s]).map(([k, v]) => [k, Array.isArray(v) ? r3(v) : Math.round(v * 1e4) / 1e4]));
fs.writeFileSync(path.join(OUT, "fps_arms.json"), JSON.stringify({
  _doc: "tools/rig_fps_arms.mjs output: rest landmarks of the rigged first-person arms (metres, model space: arms along +z, y up). Joints per arm (right 0-8, left 9-17): upper arm, forearm, forearm twist, hand, fingers proximal / distal, index proximal / distal, thumb. Regenerate with the tool; do not edit.",
  right: side("R"), left: side("L"), sleevePixels: ln,
}, null, 1));
console.log("fps arms rigged:", P.length, "verts,", IDX.length / 3, "tris; sleeve texels", ln, "; forearm R", rest.R.forearm.toFixed(3), "L", rest.L.forearm.toFixed(3), "; upper stub R", rest.R.upperStub.toFixed(3));
