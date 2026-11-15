// MOD1 modern outdoor / terrain set (docs/modern-assets-brief.md): road, sidewalk, fence and hedge line pieces for the
// lasso Line mode (content/settings/line_types.json mod_* types, hidden carriers content/assets/line_mod_*.json), plus
// placeable road junctions, parking lots, playground, courts, an outdoor pool, fountains and garden pieces.
// Line pieces (src/lines_ln2.cpp bays builder): +x is the bay length, every long part is split along x so the mesh bends
// with the spline; parts tagged cap: 1 are the end post drawn only where a run ends. A road bay is wider than it is
// long, so its footprint is declared square (bay length both ways) to keep x the stretch axis (footprint.x >= .y).
// Written by tools/build_mpu_assets.mjs (the integrator wires modernOutdoorAssets() in).
import { Asset, swingSign } from "./kit.mjs";
import { setNav, cover, rng, r3 } from "./catalog_atlantic.mjs";

// ---------------------------------------------------------------- materials (added to an asset when its parts use them)
const T = (texture, more = {}) => ({ texture, ...more });
const MATS = {
  asphalt: [[0.12, 0.12, 0.12], 0.92, "concrete", T("concrete_aggregate")],
  asphalt_patch: [[0.085, 0.085, 0.09], 0.9, "concrete", T("paving_worn")],
  asphalt_light: [[0.17, 0.17, 0.165], 0.9, "concrete", T("paving_worn")],
  sealant: [[0.03, 0.03, 0.03], 0.6, "plain"],
  line_white: [[0.82, 0.82, 0.78], 0.65, "painted", T("concrete_light")],
  line_yellow: [[0.8, 0.6, 0.08], 0.65, "painted", T("concrete_light")],
  line_blue: [[0.07, 0.2, 0.52], 0.65, "painted", T("concrete_light")],
  kerb: [[0.56, 0.55, 0.52], 0.85, "concrete", T("concrete_light")],
  channel: [[0.47, 0.46, 0.44], 0.85, "concrete", T("concrete_aged")],
  slab: [[0.62, 0.61, 0.58], 0.85, "concrete", T("concrete_aged")],
  slab_dark: [[0.5, 0.49, 0.47], 0.85, "concrete", T("concrete_stained")],
  paver: [[0.45, 0.42, 0.4], 0.85, "stone", T("cobbles_grey")],
  deck_paving: [[0.72, 0.68, 0.62], 0.8, "tile", T("floor_cement_tile")],
  grass: [[0.16, 0.26, 0.08], 0.95, "plain", T("grass_lawn")],
  mulch: [[0.16, 0.1, 0.06], 0.95, "plain", T("soil_brown")],
  gravel: [[0.55, 0.52, 0.47], 0.9, "stone", T("gravel_fine")],
  leaf: [[0.07, 0.16, 0.05], 0.85, "plain", T("leaves")],
  leaf_dark: [[0.045, 0.11, 0.035], 0.85, "plain", T("leaves")],
  leaf_light: [[0.13, 0.23, 0.06], 0.85, "plain", T("leaves")],
  leaf_red: [[0.32, 0.08, 0.06], 0.85, "plain", T("leaves")],
  blossom: [[0.78, 0.45, 0.55], 0.8, "plain", T("leaves")],
  grass_blade: [[0.32, 0.36, 0.14], 0.85, "plain"],
  bark: [[0.22, 0.17, 0.12], 0.95, "wood", T("bark_fir")],
  white_wood: [[0.86, 0.86, 0.83], 0.6, "painted", T("paint_wood_white")],
  timber: [[0.45, 0.32, 0.2], 0.8, "wood", T("wood_oak")],
  timber_grey: [[0.42, 0.39, 0.35], 0.85, "wood", T("planks_038")],
  decking: [[0.46, 0.32, 0.2], 0.8, "wood", T("planks_038")],
  sleeper: [[0.3, 0.22, 0.15], 0.9, "wood", T("wood_dark")],
  shed_paint: [[0.3, 0.38, 0.42], 0.7, "painted", T("paint_wood_bluegrey")],
  felt: [[0.12, 0.12, 0.13], 0.95, "slate", T("slate_grey")],
  shingle: [[0.2, 0.2, 0.22], 0.9, "slate", T("slate_roof")],
  galv: [[0.62, 0.63, 0.64], 0.45, "metal", T("steel_scratched", { metallic: 0.85 })],
  alu: [[0.78, 0.79, 0.8], 0.35, "metal", T("steel_scratched", { metallic: 0.9 })],
  steel_dark: [[0.12, 0.13, 0.14], 0.5, "metal", { metallic: 0.4 }],
  steel_green: [[0.07, 0.24, 0.12], 0.5, "painted", {}],
  steel_red: [[0.62, 0.09, 0.05], 0.5, "painted", {}],
  steel_blue: [[0.07, 0.22, 0.55], 0.5, "painted", {}],
  steel_yellow: [[0.85, 0.62, 0.05], 0.5, "painted", {}],
  iron: [[0.1, 0.1, 0.1], 0.6, "metal", T("iron_worn", { metallic: 0.6 })],
  plastic_red: [[0.7, 0.08, 0.05], 0.4, "plain"],
  plastic_yellow: [[0.9, 0.66, 0.06], 0.4, "plain"],
  plastic_blue: [[0.06, 0.25, 0.62], 0.4, "plain"],
  plastic_green: [[0.12, 0.5, 0.15], 0.4, "plain"],
  plastic_orange: [[0.9, 0.38, 0.05], 0.4, "plain"],
  hpl_green: [[0.1, 0.36, 0.16], 0.5, "painted", T("concrete_light")],
  rubber_red: [[0.45, 0.13, 0.09], 0.95, "concrete", T("gravel_fine")],
  rubber_red2: [[0.4, 0.11, 0.08], 0.95, "concrete", T("gravel_fine")],
  rubber_green: [[0.14, 0.32, 0.12], 0.95, "concrete", T("gravel_fine")],
  rubber_green2: [[0.12, 0.28, 0.1], 0.95, "concrete", T("gravel_fine")],
  rubber_black: [[0.05, 0.05, 0.05], 0.8, "plain"],
  sand: [[0.7, 0.62, 0.46], 0.95, "plain", T("sand_054")],
  court_green: [[0.16, 0.33, 0.2], 0.7, "painted", T("concrete_light")],
  court_blue: [[0.12, 0.25, 0.44], 0.7, "painted", T("concrete_light")],
  court_red: [[0.52, 0.2, 0.13], 0.7, "painted", T("concrete_light")],
  pad_blue: [[0.08, 0.2, 0.5], 0.8, "fabric", T("cloth_grey")],
  pool_tile: [[0.5, 0.74, 0.82], 0.3, "tile", T("pooltile_backrooms")],
  tile_band: [[0.07, 0.2, 0.42], 0.3, "tile", T("pooltile_backrooms")],
  tile_dark: [[0.06, 0.12, 0.3], 0.3, "tile", T("pooltile_backrooms")],
  basin_tile: [[0.18, 0.3, 0.33], 0.4, "tile", T("slate_square")],
  coping: [[0.8, 0.78, 0.72], 0.7, "concrete", T("concrete_light")],
  stone_light: [[0.74, 0.71, 0.65], 0.75, "stone", T("limestone_smooth")],
  stone_inset: [[0.55, 0.52, 0.47], 0.75, "stone", T("limestone_dressed")],
  slate: [[0.3, 0.31, 0.32], 0.8, "slate", T("slate_square")],
  render: [[0.78, 0.76, 0.71], 0.85, "plaster", T("render_lime")],
  brick: [[0.5, 0.3, 0.22], 0.85, "brick", T("brick_muted")],
  concrete: [[0.6, 0.59, 0.56], 0.85, "concrete", T("concrete_light")],
  corten: [[0.36, 0.17, 0.08], 0.8, "metal", T("steel_rust_light")],
  pot_fibre: [[0.28, 0.28, 0.29], 0.7, "concrete", T("concrete_aged")],
  terracotta: [[0.62, 0.32, 0.2], 0.85, "plain", T("clay_flat")],
  water_proxy: [[0.05, 0.1, 0.1], 0.1, "glass"],
  water_fall: [[0.62, 0.76, 0.82], 0.05, "glass"],
  glass: [[0.62, 0.72, 0.74], 0.05, "glass"],
  mesh_screen: [[0.3, 0.32, 0.32], 0.4, "glass"],
  net_white: [[0.85, 0.85, 0.82], 0.85, "fabric", T("cloth_grey")],
  net_dark: [[0.05, 0.05, 0.05], 0.8, "fabric"],
  rope: [[0.55, 0.45, 0.28], 0.9, "fabric"],
  canvas_white: [[0.86, 0.84, 0.78], 0.9, "fabric", T("cloth_grey")],
  canvas_red: [[0.66, 0.1, 0.08], 0.9, "fabric", T("cloth_grey")],
  canvas_navy: [[0.08, 0.12, 0.28], 0.9, "fabric", T("cloth_grey")],
  float_blue: [[0.05, 0.2, 0.6], 0.4, "plain"],
  float_white: [[0.85, 0.85, 0.85], 0.4, "plain"],
  lamp_glow: [[1, 0.96, 0.86], 0.3, "plain", { emissive: [6, 5.6, 4.6] }],
  pool_glow: [[0.8, 0.95, 1], 0.3, "plain", { emissive: [3, 4.2, 5] }],
  petal_red: [[0.55, 0.05, 0.04], 0.7, "plain"],
  petal_yellow: [[0.66, 0.46, 0.04], 0.7, "plain"],
  petal_white: [[0.75, 0.74, 0.68], 0.7, "plain"],
  petal_purple: [[0.3, 0.12, 0.45], 0.7, "plain"],
  petal_pink: [[0.66, 0.24, 0.36], 0.7, "plain"],
};
function partsOf(A) {
  const out = [...A.parts];
  for (const f of A.furniture) out.push(...f.parts);
  for (const it of A.interactables) for (const l of it.leaves || []) out.push(...l.parts);
  return out;
}
function finish(A) {
  A.meta.footprint = A.meta.footprint.map(r3);
  const used = new Set(partsOf(A).map((p) => p.m));
  for (const n of used) {
    if (!MATS[n]) throw new Error(`${A.meta.id}: unknown material ${n}`);
    const [al, ro, pa, ex] = MATS[n];
    A.mat(n, al, ro, pa, ex || {});
  }
  return A;
}
function base(meta) {
  return new Asset({ version: 1, category: "Modern outdoor", floors: 0, enterable: false, plinthDepth: 0, maxGrade: 0.6, noAutoPanels: true, ...meta });
}

// ---------------------------------------------------------------- geometry helpers
const D = { k: 0, lod: 1 }, K = { k: 0 };
const R4 = (v) => Math.round(v * 10000) / 10000, R6 = (v) => Math.round(v * 1e6) / 1e6;
const deg = (r) => (r * 180) / Math.PI, rad = (d) => (d * Math.PI) / 180;
const yawOf = (dx, dz) => deg(Math.atan2(-dz, dx));   // the yaw that turns local +x onto (dx, dz) (kit rotY convention)
// quaternions [x, y, z, w]
const qmul = (a, b) => [
  a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
  a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
  a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
  a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]];
const qY = (d) => [0, Math.sin(rad(d) / 2), 0, Math.cos(rad(d) / 2)];
function qEuler([rx, ry, rz]) {   // glm::quat(eulerRadians), as the loader reads "r"
  const h = Math.PI / 360, cx = Math.cos(rx * h), cy = Math.cos(ry * h), cz = Math.cos(rz * h), sx = Math.sin(rx * h), sy = Math.sin(ry * h), sz = Math.sin(rz * h);
  return [sx * cy * cz - cx * sy * sz, cx * sy * cz + sx * cy * sz, cx * cy * sz - sx * sy * cz, cx * cy * cz + sx * sy * sz];
}
function qFromY(v) {   // the rotation taking +y onto v
  const L = Math.hypot(...v), [x, y, z] = v.map((c) => c / L);
  if (y < -0.99999) return [1, 0, 0, 0];
  const q = [z, 0, -x, 1 + y], n = Math.hypot(...q);
  return q.map((c) => c / n);
}
// a round rod between two points (cylinders run along local y)
function rod(A, a, b, d, m, o = {}) {
  const v = [b[0] - a[0], b[1] - a[1], b[2] - a[2]];
  const p = A.cyl([(a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2], [d, Math.hypot(...v), d], m, { seg: 8, ...o });
  p.q = qFromY(v).map(R6);
  return p;
}
// a flat bar on the ground plane between (x, z) points a and b: width w across, y0..y1
function bar(A, a, b, w, y0, y1, m, o = {}) {
  const dx = b[0] - a[0], dz = b[1] - a[1];
  return A.obox([(a[0] + b[0]) / 2, (y0 + y1) / 2, (a[1] + b[1]) / 2], [Math.hypot(dx, dz), y1 - y0, w], [0, yawOf(dx, dz), 0], m, o);
}
// a local frame at (ox, oy, oz) turned by yaw: every part placed through it is moved and rotated as one sub-model
function G(A, ox, oz, yaw = 0, oy = 0) {
  const c = Math.cos(rad(yaw)), s = Math.sin(rad(yaw)), qy = qY(yaw);
  const P = ([x, y, z]) => [ox + x * c + z * s, oy + y, oz - x * s + z * c];
  const turn = (p, o) => { if (p && (yaw || o.r)) { p.q = qmul(qy, qEuler(o.r || [0, 0, 0])).map(R6); delete p.r; } return p; };
  return {
    P,
    box(mn, mx, m, o = {}) {
      if (!yaw) return A.box(P(mn), P(mx), m, o);
      const sz = mn.map((v, i) => Math.abs(mx[i] - v));
      if (sz.some((v) => v < 1e-4)) return null;
      return A._put({ t: "b", c: P(mn.map((v, i) => (v + mx[i]) / 2)).map(R4), s: sz.map(R4), q: qy.map(R6), m }, o);
    },
    obox(c0, sz, r, m, o = {}) { return A._put({ t: "b", c: P(c0).map(R4), s: sz.map(R4), q: qmul(qy, qEuler(r)).map(R6), m }, o); },
    cyl(c0, sz, m, o = {}) { return turn(A.cyl(P(c0), sz, m, o), o); },
    cone(c0, sz, m, o = {}) { return turn(A.cone(P(c0), sz, m, o), o); },
    wedge(c0, sz, m, o = {}) { return turn(A.wedge(P(c0), sz, m, o), o); },
    arch(c0, sz, m, o = {}) { return turn(A.arch(P(c0), sz, m, o), o); },
    rbox(mn, mx, r, m, o = {}) {
      const ctr = mn.map((v, i) => (v + mx[i]) / 2), h = mn.map((v, i) => Math.abs(mx[i] - v) / 2), pc = P(ctr);
      return turn(A.rbox(pc.map((v, i) => v - h[i]), pc.map((v, i) => v + h[i]), r, m, o), o);
    },
    rod(a, b, d, m, o = {}) { return rod(A, P(a), P(b), d, m, o); },
  };
}
// segments of a chain-link / hatch lattice: both 45 degree diagonals at pitch p, clipped to [u0, u1] x [v0, v1]
function diagSegs(u0, u1, v0, v1, p) {
  const out = [], H = v1 - v0;
  for (const sg of [1, -1]) for (let c = u0 - H; c <= u1 + 1e-6; c += p) {
    const ua = Math.max(u0, c), ub = Math.min(u1, c + H);
    if (ub - ua < 0.01) continue;
    const v = (u) => (sg > 0 ? v0 + (u - c) : v1 - (u - c));
    out.push({ a: [ua, v(ua)], b: [ub, v(ub)], s: sg });
  }
  return out;
}
// polygon helpers
const circ = (r, n, cx = 0, cz = 0, a0 = 0) => Array.from({ length: n }, (_, i) => { const a = a0 + (i / n) * Math.PI * 2; return [cx + Math.cos(a) * r, cz + Math.sin(a) * r]; });
const rectO = (x0, z0, x1, z1) => [[x0, z0], [x1, z0], [x1, z1], [x0, z1]];
const area2 = (p) => { let s = 0; for (let i = 0, j = p.length - 1; i < p.length; j = i++) s += (p[j][0] - p[i][0]) * (p[j][1] + p[i][1]); return s / 2; };
// a convex water surface (src/water.cpp) with its 1 cm glass proxy (bounds, thumbnail); clear chlorinated / fountain water
function water(A, outline, level, extra = {}) {
  const o = (area2(outline) < 0 ? outline.slice().reverse() : outline).map((p) => p.map(r3));
  (A.meta.water ??= { surfaces: [] }).surfaces.push({ outline: o, level: r3(level), kind: "pond", clarity: 4, waves: 0.35, tint: [0.02, 0.075, 0.095], ...extra });
  for (let i = 1; i + 1 < o.length; i++) A.prism(1, level - 0.01, level, [o[0], o[i], o[i + 1]], "water_proxy", { k: 0 });
}
// ring of n boxes between radii r0..r1 (walls, kerbs, copings); off: angular offset in fractions of a segment
function ring(A, cx, cz, r0, r1, y0, y1, n, m, o = {}, a0 = 0, a1 = Math.PI * 2, off = 0.5) {
  const da = (a1 - a0) / n, rm = (r0 + r1) / 2, chord = 2 * r1 * Math.tan(Math.abs(da) / 2) * 1.01;
  const out = [];
  for (let i = 0; i < n; i++) {
    const a = a0 + (i + off) * da;
    if (o.skip && o.skip(a)) continue;
    const oo = { ...o }; delete oo.skip;
    out.push(A.obox([cx + Math.cos(a) * rm, (y0 + y1) / 2, cz + Math.sin(a) * rm], [chord, y1 - y0, r1 - r0], [0, yawOf(-Math.sin(a), Math.cos(a)), 0], m, oo));
  }
  return out;
}
// painted line along an arc (radius r, angles a0..a1), width w, top at y
function arcLine(A, cx, cz, r, a0, a1, w, y, m = "line_white", n = 0) {
  n = n || Math.max(4, Math.ceil((Math.abs(a1 - a0) * r) / 0.5));
  ring(A, cx, cz, r - w / 2, r + w / 2, y - 0.01, y, n, m, K, a0, a1);
}
// straight painted line between (x, z) points, width w, top at y
const lineP = (A, a, b, w, y, m = "line_white") => bar(A, a, b, w, y - 0.01, y, m, K);
// prism fan from point c to an arc of points (asphalt fillets)
function fan(A, c, pts, y0, y1, m) { for (let i = 0; i + 1 < pts.length; i++) A.prism(1, y0, y1, [c, pts[i], pts[i + 1]].map((p) => p.map(r3)), m); }

// ---------------------------------------------------------------- shared sub-models
// ornamental tree: tapered trunk, scaffold limbs and layered leafy crowns (stacked discs + blobs; no canopy block, so it
// draws as parts). kind: round | column | spread
function tree(A, R, x, z, { h = 5, cr = 1.6, kind = "round", leaf = "leaf", y = 0 } = {}) {
  const clear = kind === "column" ? h * 0.22 : h * 0.4, tw = 0.07 + h * 0.022;
  A.cyl([x, y + clear / 2 - 0.1, z], [tw * 2, clear + 0.2, tw * 2], "bark", { seg: 9, top: 0.8 });
  A.cone([x, y + 0.08, z], [tw * 3.2, 0.2, tw * 3.2], "bark", { seg: 9, top: 0.55, k: 0 });
  const ch = h - clear, cy0 = y + clear;
  for (let i = 0; i < 4; i++) {   // scaffold limbs into the crown
    const a = (i / 4) * Math.PI * 2 + R() * 0.8, out = cr * (kind === "column" ? 0.3 : 0.55);
    rod(A, [x, cy0 - 0.05, z], [x + Math.cos(a) * out, cy0 + ch * 0.45, z + Math.sin(a) * out], tw * 0.9, "bark", { ...D, seg: 6 });
  }
  A.cyl([x, cy0 + ch * 0.35, z], [tw * 1.4, ch * 0.7, tw * 1.4], "bark", { seg: 7, top: 0.4, ...D });
  const layers = kind === "column" ? 7 : 5;
  for (let i = 0; i < layers; i++) {
    const f = (i + 0.5) / layers;
    let r = kind === "column" ? cr * (0.55 + 0.45 * Math.sin(Math.PI * Math.min(1, f * 1.15))) * (1 - f * 0.35)
      : kind === "spread" ? cr * Math.sqrt(Math.max(0.05, 1 - Math.pow(2 * f - 1.3, 2))) : cr * Math.sqrt(1 - Math.pow(2 * f - 1, 2));
    r = Math.max(r, cr * 0.25);
    A.cyl([x + (R() - 0.5) * 0.1, cy0 + ch * f, z + (R() - 0.5) * 0.1], [2 * r, ch / layers + 0.12, 2 * r], i % 2 ? leaf : (leaf === "leaf" ? "leaf_dark" : leaf), { k: 0, seg: 11 });
  }
  const blobs = kind === "column" ? 10 : 14;
  for (let i = 0; i < blobs; i++) {   // leafy clumps breaking up the silhouette
    const a = R() * Math.PI * 2, f = 0.15 + R() * 0.75;
    const rr = (kind === "column" ? cr * 0.8 : cr) * (0.65 + R() * 0.3) * Math.sqrt(1 - Math.pow(2 * f - 1, 2) * 0.6), s = cr * (0.28 + R() * 0.22);
    const cxp = x + Math.cos(a) * rr, czp = z + Math.sin(a) * rr, cyp = cy0 + ch * f;
    A.rbox([cxp - s, cyp - s * 0.8, czp - s], [cxp + s, cyp + s * 0.8, czp + s], s * 0.8, R() < 0.3 ? "leaf_light" : leaf, { k: 0, lod: i > blobs / 2 ? 1 : 0 });
  }
}
// street / car-park light column with 1 or 2 LED heads; returns the light spec(s) for A.meta.light
function lightPole(A, x, z, yaw, h = 8, heads = 1) {
  A.cyl([x, -0.25, z], [0.55, 0.7, 0.55], "concrete", { seg: 10 });                    // cast footing
  A.cyl([x, 0.15, z], [0.3, 0.1, 0.3], "galv", { seg: 8, k: 0 });                         // base flange
  A.cyl([x, h / 2, z], [0.16, h, 0.16], "galv", { seg: 10, top: 0.55 });
  A.box([x - 0.08, 0.6, z - 0.08], [x + 0.08, 0.95, z + 0.08], "galv", { ...D, bv: 0.01 });   // service door
  const L = [];
  for (let k = 0; k < heads; k++) {
    const g = G(A, x, z, yaw + k * 180);
    g.rod([0, h - 0.15, 0], [0.9, h + 0.05, 0], 0.07, "galv", K);
    g.box([0.75, h - 0.02, -0.17], [1.45, h + 0.1, 0.17], "steel_dark", { k: 0, bv: 0.03 });
    g.box([0.8, h - 0.04, -0.13], [1.4, h - 0.02, 0.13], "lamp_glow", K);
    L.push({ offset: g.P([1.1, h - 0.15, 0]).map(r3), use: "street", color: [1, 0.95, 0.85], intensity: 3.2, radius: 18, shadow: k === 0 ? 1 : 0 });
  }
  return L;
}
// kerbed island / verge: kerb all round, fill top at 0.12
function island(A, x0, z0, x1, z1, fill = "grass", K2 = 0.15) {
  A.box([x0, -0.15, z0], [x1, 0.15, z0 + K2], "kerb", { bv: 0.02 });
  A.box([x0, -0.15, z1 - K2], [x1, 0.15, z1], "kerb", { bv: 0.02 });
  A.box([x0, -0.15, z0 + K2], [x0 + K2, 0.15, z1 - K2], "kerb", { bv: 0.02 });
  A.box([x1 - K2, -0.15, z0 + K2], [x1, 0.15, z1 - K2], "kerb", { bv: 0.02 });
  A.box([x0 + K2, -0.1, z0 + K2], [x1 - K2, 0.12, z1 - K2], fill);
}
// a slatted bench (back = with backrest on cast ends), local front +z
function bench(g, back = true, L = 1.6) {
  for (const x of [-L / 2 + 0.12, L / 2 - 0.12]) {
    g.box([x - 0.03, 0, -0.22], [x + 0.03, 0.43, -0.17], "steel_dark", K);
    g.box([x - 0.03, 0, 0.17], [x + 0.03, 0.43, 0.22], "steel_dark", K);
    g.box([x - 0.03, 0.38, -0.24], [x + 0.03, 0.43, 0.24], "steel_dark", K);
    g.box([x - 0.04, 0, -0.26], [x + 0.04, 0.02, 0.26], "steel_dark", K);
    if (back) {
      g.obox([x, 0.66, -0.24], [0.05, 0.5, 0.04], [-12, 0, 0], "steel_dark", K);
      g.box([x - 0.03, 0.6, -0.2], [x + 0.03, 0.64, 0.26], "steel_dark", K);   // armrest
      g.cyl([x, 0.5, 0.24], [0.04, 0.2, 0.04], "steel_dark", K);
    }
  }
  for (let i = 0; i < 5; i++) { const z = -0.22 + i * 0.09; g.box([-L / 2, 0.43, z], [L / 2, 0.465, z + 0.07], "timber", { k: 0, bv: 0.008 }); }
  if (back) for (let i = 0; i < 3; i++) g.obox([0, 0.55 + i * 0.12, -0.25 + i * 0.026], [L, 0.08, 0.03], [-12, 0, 0], "timber", { k: 0, bv: 0.008 });
  g.box([-L / 2, 0, -0.26], [L / 2, back ? 0.9 : 0.47, 0.26], "timber", { v: 0 });
}
// sun lounger, local head end -z, length along z
function lounger(g) {
  for (const x of [-0.3, 0.3]) {
    g.box([x - 0.02, 0.28, -0.95], [x + 0.02, 0.32, 0.95], "alu", { k: 0, bv: 0.008 });
    for (const z of [-0.85, 0.85]) g.box([x - 0.02, 0, z - 0.02], [x + 0.02, 0.3, z + 0.02], "alu", K);
    g.cyl([x, 0.06, -0.95], [0.12, 0.03, 0.12], "rubber_black", { k: 0, r: [0, 0, 90] });   // wheels
  }
  for (let i = 0; i < 9; i++) { const z = -0.2 + i * 0.13; g.box([-0.31, 0.32, z], [0.31, 0.34, z + 0.1], "float_white", { k: 0, bv: 0.005 }); }
  g.box([-0.3, 0.34, -0.2], [0.3, 0.39, 1.0], "canvas_navy", { k: 0, bv: 0.02 });
  g.obox([0, 0.58, -0.55], [0.62, 0.05, 0.8], [-40, 0, 0], "canvas_navy", { k: 0, bv: 0.02 });
  g.obox([0, 0.56, -0.57], [0.6, 0.03, 0.82], [-40, 0, 0], "alu", K);
  g.box([-0.32, 0, -0.95], [0.32, 0.45, 0.95], "alu", { v: 0 });
}
// court / pool perimeter fence: posts every ~3 m, rails, a mesh screen per span; openSide's middle span left open
function perimFence(A, hx, hz, H, post = "steel_green", openSide = "nz") {
  const sides = [["nz", [-hx, -hz], [hx, -hz]], ["px", [hx, -hz], [hx, hz]], ["pz", [hx, hz], [-hx, hz]], ["nx", [-hx, hz], [-hx, -hz]]];
  for (const [name, a, b] of sides) {
    const L = Math.hypot(b[0] - a[0], b[1] - a[1]);
    let n = Math.ceil(L / 3); if (n % 2 === 0) n++;
    const at = (t) => [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t];
    for (let i = 0; i < n; i++) {
      const p = at(i / n), q = at((i + 1) / n);
      A.cyl([p[0], (H - 0.45) / 2, p[1]], [0.065, H + 0.45, 0.065], post, { seg: 8 });
      A.cyl([p[0], H + 0.01, p[1]], [0.08, 0.03, 0.08], post, K);
      if (name === openSide && i === (n - 1) / 2) continue;
      for (const y of [0.06, 1.2, H - 0.03]) rod(A, [p[0], y, p[1]], [q[0], y, q[1]], 0.04, post, K);
      bar(A, p, q, 0.012, 0.05, H - 0.03, "mesh_screen");
    }
  }
}

// ============================================================================ line pieces
const RD = { HW: 3.75, KW: 0.15 };
function piece(id, name, L, desc, extra = {}) {
  return base({ id, name, description: desc, hidden: true, footprint: [L, L], maxGrade: 3, ...extra });
}
// a long box split along x (the bay axis) so it bends with the line
function strip(A, L, n, z0, z1, y0, y1, m, o = {}) { const out = []; for (let i = 0; i < n; i++) out.push(A.box([-L / 2 + (i * L) / n, y0, z0], [-L / 2 + ((i + 1) * L) / n, y1, z1], m, o)); return out; }
const capAll = (ps) => { for (const p of ps.flat()) if (p) p.cap = 1; };
// gully grate in the channel at u, side s
function gully(A, u, z) {
  A.box([u - 0.25, 0.018, z - 0.16], [u + 0.25, 0.026, z + 0.16], "iron", { k: 0, bv: 0.004 });
  for (let i = 0; i < 6; i++) A.box([u - 0.2 + i * 0.08, 0.025, z - 0.12], [u - 0.17 + i * 0.08, 0.027, z + 0.12], "sealant", D);
}
function manhole(A, u, z) {
  A.cyl([u, 0.022, z], [0.72, 0.012, 0.72], "iron", { k: 0, seg: 16 });
  for (let i = -2; i <= 2; i++) A.box([u - 0.25, 0.026, z + i * 0.1 - 0.012], [u + 0.25, 0.029, z + i * 0.1 + 0.012], "iron", D);
}
function roadBay2(id, v) {
  const L = 6, { HW, KW } = RD, A = piece(id, "Road bay, 2-lane", L, "6 m bay of a 7.5 m two-lane asphalt carriageway with white edge lines, a dashed centre line, concrete drainage channels and kerbs both sides (the mod_road_2lane line piece).");
  strip(A, L, 3, -HW, HW, -0.3, 0.02, "asphalt");
  for (const s of [-1, 1]) {
    strip(A, L, 3, s > 0 ? HW - 0.3 : -HW, s > 0 ? HW : -HW + 0.3, -0.1, 0.024, "channel", K);
    strip(A, L, 6, s > 0 ? HW : -HW - KW, s > 0 ? HW + KW : -HW, -0.25, 0.15, "kerb", { bv: 0.015 });
    strip(A, L, 3, s * (HW - 0.45) - 0.06, s * (HW - 0.45) + 0.06, 0.016, 0.026, "line_white", K);
  }
  A.box([-1.5, 0.016, -0.06], [1.5, 0.026, 0.06], "line_white", K);
  const R = rng(9100 + v);
  if (v === 1) { gully(A, 1.8, HW - 0.15); gully(A, 1.8, -HW + 0.15); manhole(A, -1.2, 1.8); }
  if (v >= 1) for (let i = 0; i < 2 + v; i++) {   // patch repairs and sealed cracks
    const u = -2.2 + R() * 4.4, z = (R() - 0.5) * 5.5, w = 0.6 + R() * 1.2, d = 0.5 + R() * 0.9;
    A.box([u - w / 2, 0.019, z - d / 2], [u + w / 2, 0.0235, z + d / 2], "asphalt_patch", D);
  }
  if (v === 2) for (let i = 0; i < 5; i++) A.obox([-2 + R() * 4, 0.0215, (R() - 0.5) * 6], [0.6 + R() * 1.4, 0.004, 0.02], [0, R() * 180, 0], "sealant", D);
  setNav(A, { effect: "none" });
  return A;
}
function roadBay4(id, v) {
  const L = 6, KW = 0.15, A = piece(id, "Road bay, dual carriageway", L, "6 m bay of a four-lane dual carriageway: two 7 m carriageways of two 3.5 m lanes with dashed lane lines and edge lines, a 2 m kerbed grass median and outer kerbs (the mod_road_4lane line piece).");
  for (const s of [-1, 1]) {
    const z0 = s > 0 ? 1.0 : -8.0, z1 = s > 0 ? 8.0 : -1.0;
    strip(A, L, 3, z0, z1, -0.3, 0.02, "asphalt");
    strip(A, L, 3, s > 0 ? 7.7 : -8.0, s > 0 ? 8.0 : -7.7, -0.1, 0.024, "channel", K);
    strip(A, L, 6, s > 0 ? 8.0 : -8.0 - KW, s > 0 ? 8.0 + KW : -8.0, -0.25, 0.15, "kerb", { bv: 0.015 });
    strip(A, L, 6, s > 0 ? 0.85 : -1.0, s > 0 ? 1.0 : -0.85, -0.25, 0.15, "kerb", { bv: 0.015 });
    strip(A, L, 3, s * 1.5 - 0.05, s * 1.5 + 0.05, 0.016, 0.026, "line_white", K);
    strip(A, L, 3, s * 7.45 - 0.075, s * 7.45 + 0.075, 0.016, 0.026, "line_white", K);
    A.box([-1, 0.016, s * 4.5 - 0.05], [1, 0.026, s * 4.5 + 0.05], "line_white", K);
  }
  strip(A, L, 3, -0.85, 0.85, -0.2, 0.12, "grass");
  if (v === 1) {   // a median lighting column with two arms, and gullies
    A.cyl([0, -0.2, 0], [0.5, 0.6, 0.5], "concrete", { seg: 10, k: 0 });
    A.cyl([0, 5, 0], [0.18, 10, 0.18], "galv", { seg: 10, top: 0.55 });
    for (const s of [-1, 1]) {
      A.box([-0.05, 9.85, 0], [0.05, 9.95, s * 1.8], "galv", K);
      A.box([-0.17, 9.82, s * 1.6 - 0.35], [0.17, 9.94, s * 1.6 + 0.35], "steel_dark", { k: 0, bv: 0.03 });
      A.box([-0.13, 9.8, s * 1.6 - 0.3], [0.13, 9.82, s * 1.6 + 0.3], "lamp_glow", K);
      gully(A, 1.8, s * 7.85);
    }
  }
  if (v === 2) { const R = rng(9200); for (let i = 0; i < 6; i++) { const u = -2.4 + R() * 4.8, z = (R() - 0.5) * 1.3, s = 0.2 + R() * 0.15; A.rbox([u - s, 0.05, z - s], [u + s, 0.12 + s * 1.2, z + s], s * 0.8, R() < 0.5 ? "leaf" : "leaf_dark", D); } }
  setNav(A, { effect: "none" });
  return A;
}
function sidewalkBay(id, v) {
  const L = 2.5, A = piece(id, "Sidewalk bay", L, "2.5 m bay of a 2.5 m concrete flag sidewalk: four 1.25 m slabs with open joints and a precast kerb along its +v edge (flip the line to swap the kerb side). The mod_sidewalk line piece.");
  const R = rng(9300 + v);
  for (const [u0, u1] of [[-1.25, 0], [0, 1.25]]) for (const [z0, z1] of [[-1.25, -0.075], [-0.075, 1.1]]) {
    const dark = v === 2 && u0 < 0 && z0 < -1;
    A.box([u0 + 0.003, -0.1, z0 + 0.003], [u1 - 0.003, 0.15 - (dark ? 0.004 : 0), z1 - 0.003], dark ? "slab_dark" : "slab", { k: 0, bv: 0.01 });
  }
  strip(A, L, 2, 1.1, 1.25, -0.25, 0.155, "kerb", { bv: 0.02 });
  A.box([-L / 2, -0.25, -1.25], [L / 2, 0.15, 1.25], "slab", { v: 0 });
  if (v === 1) {   // utility cover and a stained flag
    A.box([0.35, 0.146, 0.25], [0.95, 0.153, 0.85], "iron", { k: 0, bv: 0.006 });
    for (let i = 0; i < 4; i++) A.box([0.4, 0.152, 0.3 + i * 0.15], [0.9, 0.155, 0.33 + i * 0.15], "steel_dark", D);
  }
  for (let i = 0; i < 2; i++) { const x = -1 + R() * 1.8, z = -1 + R() * 1.6; A.box([x, 0.149, z], [x + 0.2, 0.152, z + 0.2], "slab_dark", D); }
  setNav(A, { effect: "none" });
  return A;
}
function fenceWhiteBay() {
  const L = 2.4, H = 1.2, A = piece("mod_fence_white_bay", "White timber fence bay", L, "2.4 m bay of a whitewashed timber fence: square posts with pyramid caps, three horizontal boards and a top cap rail (the mod_fence_white line piece).");
  const post = (u) => [A.box([u - 0.05, -0.5, -0.05], [u + 0.05, H, 0.05], "white_wood", { k: 0, bv: 0.008 }), A.cone([u, H + 0.065, 0], [0.11, 0.07, 0.11], "white_wood", { n: 4, k: 0 })];
  post(-L / 2 + 0.05);
  capAll([post(L / 2 - 0.05)]);
  for (const y of [0.3, 0.65, 1.0]) strip(A, L, 2, 0.05, 0.075, y - 0.07, y + 0.07, "white_wood", { k: 0, bv: 0.005 });
  for (const y of [0.3, 0.65, 1.0]) for (const dz of [-0.02, 0.02]) A.box([-L / 2 + 0.03, y + dz - 0.006, 0.075], [-L / 2 + 0.042, y + dz + 0.006, 0.079], "steel_dark", D);   // screw heads
  A.box([-L / 2, -0.25, -0.06], [L / 2, 0.02, 0.09], "mulch", D);
  A.box([-L / 2, 0, -0.05], [L / 2, H, 0.08], "white_wood", { v: 0 });
  setNav(A, { effect: "carve" });
  return A;
}
function picketBay() {
  const L = 2.4, A = piece("mod_fence_white_picket_bay", "White picket fence bay", L, "2.4 m bay of a white picket fence, 1.05 m high: posts with caps, two back rails and pointed pickets at 140 mm centres (the mod_fence_white_picket line piece).");
  const post = (u) => [A.box([u - 0.045, -0.45, -0.045], [u + 0.045, 1.05, 0.045], "white_wood", { k: 0, bv: 0.008 }), A.box([u - 0.06, 1.05, -0.06], [u + 0.06, 1.08, 0.06], "white_wood", { k: 0, bv: 0.006 }), A.cone([u, 1.12, 0], [0.09, 0.08, 0.09], "white_wood", { n: 4, k: 0 })];
  post(-L / 2 + 0.045);
  capAll([post(L / 2 - 0.045)]);
  for (const y of [0.25, 0.78]) strip(A, L, 2, 0.045, 0.075, y - 0.035, y + 0.035, "white_wood", { k: 0, bv: 0.004 });
  for (let u = -L / 2 + 0.16; u < L / 2 - 0.1; u += 0.14) {
    A.box([u - 0.036, 0.06, 0.075], [u + 0.036, 0.92, 0.095], "white_wood", { k: 0, bv: 0.004 });
    A.cone([u, 0.96, 0.085], [0.072, 0.08, 0.02], "white_wood", { n: 4, k: 0 });
  }
  A.box([-L / 2, 0, -0.045], [L / 2, 1.0, 0.095], "white_wood", { v: 0 });
  setNav(A, { effect: "carve" });
  return A;
}
function hedgeBay(id, H, v) {
  const L = 2.0, W = H > 1.5 ? 0.9 : 0.75, tall = H > 1.5;
  const A = piece(id, `Box hedge bay (${H} m)`, L, `2 m bay of a neatly clipped rectangular box hedge, ${H} m high and ${W} m thick, dense leaf with softened shoulders over a mulch strip (the ${tall ? "mod_hedge_modern_tall" : "mod_hedge_modern"} line piece).`);
  strip(A, L, 2, -W / 2, W / 2, 0, H - 0.07, v ? "leaf_dark" : "leaf");
  strip(A, L, 2, -W / 2 + 0.05, W / 2 - 0.05, H - 0.1, H, "leaf", K);
  for (const s of [-1, 1]) rod(A, [-L / 2, H - 0.08, s * (W / 2 - 0.05)], [L / 2, H - 0.08, s * (W / 2 - 0.05)], 0.11, "leaf", { k: 0, seg: 8 });
  A.box([-L / 2, -0.05, -W / 2 - 0.12], [L / 2, 0.025, W / 2 + 0.12], "mulch", K);
  const R = rng(9400 + v * 7 + (tall ? 3 : 0));
  for (let i = 0; i < (tall ? 14 : 10); i++) {   // clipped lumps standing a few cm proud
    const s = 0.12 + R() * 0.14, u = -L / 2 + s + R() * (L - 2 * s), face = R();
    const m = R() < 0.35 ? "leaf_light" : R() < 0.5 ? "leaf_dark" : "leaf";
    if (face < 0.3) { const zc = (R() - 0.5) * (W - 0.3); A.rbox([u - s, H - s * 0.9, zc - s * 0.6], [u + s, H + s * 0.22, zc + s * 0.6], s * 0.5, m, D); }
    else { const zs = face < 0.65 ? -1 : 1, y = 0.15 + R() * (H - 0.4), zc = zs * (W / 2 - s * 0.4); A.rbox([u - s, y - s, zc - s * 0.6], [u + s, y + s, zc + s * 0.6], s * 0.6, m, D); }
  }
  setNav(A, { effect: "carve", cover: [cover([0, 0, -W / 2 - 0.45], 0, tall ? "high" : "low", tall ? ["left", "right"] : ["over"]), cover([0, 0, W / 2 + 0.45], 180, tall ? "high" : "low", tall ? ["left", "right"] : ["over"])] });
  return A;
}
function chainlinkBay() {
  const L = 3.0, H = 2.0, A = piece("mod_chainlink_bay", "Chain-link fence bay", L, "3 m bay of a 2 m galvanised chain-link fence: line post with dome cap, top rail, bottom tension wire and diamond mesh; the run ends on a heavier braced terminal post (the mod_chainlink line piece).");
  rod(A, [-L / 2 + 0.03, -0.5, 0], [-L / 2 + 0.03, H + 0.02, 0], 0.05, "galv", K);
  A.cyl([-L / 2 + 0.03, H + 0.04, 0], [0.065, 0.05, 0.065], "galv", K);
  capAll([rod(A, [L / 2 - 0.04, -0.6, 0], [L / 2 - 0.04, H + 0.03, 0], 0.075, "galv", K), A.cyl([L / 2 - 0.04, H + 0.06, 0], [0.09, 0.06, 0.09], "galv", K),
    rod(A, [L / 2 - 0.08, H * 0.55, 0], [L / 2 - 1.4, 0.15, 0], 0.04, "galv", K), A.cyl([L / 2 - 0.04, -0.3, 0], [0.3, 0.6, 0.3], "concrete", K)]);
  rod(A, [-L / 2, H - 0.03, 0.04], [0, H - 0.03, 0.04], 0.035, "galv", K);
  rod(A, [0, H - 0.03, 0.04], [L / 2, H - 0.03, 0.04], 0.035, "galv", K);
  rod(A, [-L / 2, 0.06, 0.035], [L / 2, 0.06, 0.035], 0.006, "galv", K);
  for (const sg of diagSegs(-L / 2, L / 2, 0.06, H - 0.05, 0.15)) {
    const [ua, va] = sg.a, [ub, vb] = sg.b;
    A.obox([(ua + ub) / 2, (va + vb) / 2, 0.035], [(ub - ua) * Math.SQRT2, 0.005, 0.005], [0, 0, sg.s * 45], "galv", K);
  }
  for (const y of [0.4, 1.0, 1.6]) A.box([-L / 2 + 0.01, y, 0.02], [-L / 2 + 0.05, y + 0.012, 0.04], "galv", D);   // wire ties
  A.box([-L / 2, 0, -0.04], [L / 2, H, 0.05], "galv", { v: 0 });
  setNav(A, { effect: "carve" });
  return A;
}

// ============================================================================ gates
function gateWhite() {
  const A = base({ id: "mod_gate_white", name: "White timber garden gate", footprint: [1.4, 0.25], description: "1.0 m whitewashed timber pedestrian gate matching the white fence: capped posts, a ledged-and-braced leaf of vertical boards, black hinges and a thumb latch. Opens like a door." });
  for (const x of [-0.63, 0.63]) { A.box([x - 0.06, -0.55, -0.06], [x + 0.06, 1.3, 0.06], "white_wood", { bv: 0.008 }); A.cone([x, 1.37, 0], [0.13, 0.08, 0.13], "white_wood", { n: 4, k: 0 }); }
  const L = { parts: [] }, o = { into: L.parts, k: 0 };
  for (let x = -0.55; x < 0.54; x += 0.11) A.box([x + 0.004, 0.08, 0.06], [x + 0.106, 1.1, 0.08], "white_wood", { ...o, bv: 0.004 });
  for (const y of [0.22, 0.95]) A.box([-0.55, y, 0.03], [0.55, y + 0.1, 0.06], "white_wood", { ...o, bv: 0.005 });
  A.obox([0, 0.6, 0.045], [0.95, 0.09, 0.03], [0, 0, 38], "white_wood", o);
  for (const y of [0.27, 1.0]) A.box([-0.56, y - 0.015, 0.08], [-0.25, y + 0.015, 0.085], "steel_dark", o);
  A.box([0.42, 0.7, 0.08], [0.47, 0.78, 0.1], "steel_dark", o);
  A.box([-0.55, 0.08, 0.03], [0.55, 1.1, 0.08], "white_wood", { into: L.parts, v: 0 });
  L.hinge = [-0.57, 0, 0.055]; L.sign = swingSign("x", [0, 0, 0.055], L.hinge, -1);
  A.interactables.push({ id: "gate", kind: "door", label: "gate", sound: "wood", axis: [0, 1, 0], openAngle: 95, duration: 1.2, defaultOpen: false, leaves: [L], aperture: { min: [-0.57, 0.05, -0.05], max: [0.57, 1.15, 0.12] } });
  setNav(A, { effect: "none" });
  return A;
}
function gateChainlink() {
  const A = base({ id: "mod_gate_chainlink", name: "Chain-link pedestrian gate", footprint: [1.5, 0.2], description: "1.2 m galvanised chain-link gate between two heavy gate posts, 2 m high: tubular frame, diamond mesh, drop latch. Opens like a door." });
  for (const x of [-0.68, 0.68]) { rod(A, [x, -0.6, 0], [x, 2.05, 0], 0.075, "galv"); A.cyl([x, 2.08, 0], [0.09, 0.06, 0.09], "galv", K); }
  const L = { parts: [] }, o = { into: L.parts, k: 0 }, x0 = -0.6, x1 = 0.6, y0 = 0.07, y1 = 1.95;
  for (const [a, b] of [[[x0, y0, 0], [x1, y0, 0]], [[x0, y1, 0], [x1, y1, 0]], [[x0, y0, 0], [x0, y1, 0]], [[x1, y0, 0], [x1, y1, 0]], [[x0, 1.0, 0], [x1, 1.0, 0]]]) rod(A, a, b, 0.035, "galv", o);
  for (const sg of diagSegs(x0, x1, y0, y1, 0.15)) A.obox([(sg.a[0] + sg.b[0]) / 2, (sg.a[1] + sg.b[1]) / 2, 0.012], [(sg.b[0] - sg.a[0]) * Math.SQRT2, 0.005, 0.005], [0, 0, sg.s * 45], "galv", o);
  A.box([0.5, 1.05, -0.03], [0.62, 1.12, 0.03], "steel_dark", o);
  A.box([x0, y0, -0.02], [x1, y1, 0.02], "galv", { into: L.parts, v: 0 });
  L.hinge = [-0.64, 0, 0]; L.sign = swingSign("x", [0, 0, 0], L.hinge, 1);
  A.interactables.push({ id: "gate", kind: "door", label: "gate", sound: "steel", axis: [0, 1, 0], openAngle: 100, duration: 1.2, defaultOpen: false, leaves: [L], aperture: { min: [-0.64, 0.05, -0.05], max: [0.64, 1.98, 0.05] } });
  setNav(A, { effect: "none" });
  return A;
}

// ============================================================================ road pieces
// a junction square of side 16 with arms (unit (x, z) directions) carrying the 2-lane road section to the edges
function junction(id, name, desc, arms) {
  const S = 8, { HW, KW } = RD, Rf = 4, Y = 0.026;
  const A = base({ id, name, description: desc, footprint: [2 * S, 2 * S], maxGrade: 1.5 });
  const has = (d) => arms.some((a) => a[0] === d[0] && a[1] === d[1]);
  const W = (Dv, s, o) => { const E = [-Dv[1], Dv[0]]; return [s * Dv[0] + o * E[0], s * Dv[1] + o * E[1]]; };
  const abox = (Dv, s0, s1, o0, o1, y0, y1, m, op = {}) => {
    const p = [W(Dv, s0, o0), W(Dv, s1, o1)];
    return A.box([Math.min(p[0][0], p[1][0]), y0, Math.min(p[0][1], p[1][1])], [Math.max(p[0][0], p[1][0]), y1, Math.max(p[0][1], p[1][1])], m, op);
  };
  A.box([-HW, -0.3, -HW], [HW, 0.02, HW], "asphalt");
  const dirs = [[1, 0], [0, 1], [-1, 0], [0, -1]];
  for (const Dv of dirs) {
    const E = [-Dv[1], Dv[0]];
    if (!has(Dv)) {   // closed side: a kerb across the square and its channel
      abox(Dv, HW, HW + KW, -HW, HW, -0.25, 0.15, "kerb", { bv: 0.015 });
      abox(Dv, HW - 0.3, HW, -HW, HW, -0.1, 0.024, "channel", K);
      lineP(A, W(Dv, HW - 0.45, -HW), W(Dv, HW - 0.45, HW), 0.12, Y);
      continue;
    }
    abox(Dv, HW, S, -HW, HW, -0.3, 0.02, "asphalt");
    for (const sd of [1, -1]) {   // the two kerb lines of this arm
      const Es = [E[0] * sd, E[1] * sd], fil = has(Es), s0 = fil ? HW + Rf : HW;
      abox(Dv, s0, S, sd * HW, sd * (HW + KW), -0.25, 0.15, "kerb", { bv: 0.015 });
      abox(Dv, s0, S, sd * (HW - 0.3), sd * HW, -0.1, 0.024, "channel", K);
      lineP(A, W(Dv, s0, sd * (HW - 0.45)), W(Dv, S, sd * (HW - 0.45)), 0.12, Y);
      if (fil && sd > 0) {   // fillet at the corner between this arm and the arm on its +E side (each corner once)
        const c = [HW * (Dv[0] + Es[0]), HW * (Dv[1] + Es[1])], o = [(HW + Rf) * (Dv[0] + Es[0]), (HW + Rf) * (Dv[1] + Es[1])];
        let a0 = Math.atan2(-Es[1], -Es[0]), a1 = Math.atan2(-Dv[1], -Dv[0]);
        if (a1 - a0 > Math.PI) a1 -= 2 * Math.PI; if (a0 - a1 > Math.PI) a1 += 2 * Math.PI;
        const pts = Array.from({ length: 9 }, (_, i) => { const a = a0 + ((a1 - a0) * i) / 8; return [o[0] + Math.cos(a) * Rf, o[1] + Math.sin(a) * Rf]; });
        fan(A, c, pts, -0.3, 0.02, "asphalt");
        ring(A, o[0], o[1], Rf - KW, Rf, -0.25, 0.15, 8, "kerb", {}, Math.min(a0, a1), Math.max(a0, a1));
        ring(A, o[0], o[1], Rf, Rf + 0.3, -0.1, 0.024, 8, "channel", K, Math.min(a0, a1), Math.max(a0, a1));
        arcLine(A, o[0], o[1], Rf + 0.45, Math.min(a0, a1), Math.max(a0, a1), 0.12, Y);
      }
    }
    // centre line on the arm, and a stop line / give-way across the approach lane (right-hand traffic)
    for (let s = HW + 2.5; s + 1.5 <= S + 0.01; s += 3) lineP(A, W(Dv, s, 0), W(Dv, s + 1.5, 0), 0.12, Y);
    const R = [Dv[1], -Dv[0]], ro = (o) => [R[0] * o, R[1] * o];
    const st = (o, s) => [s * Dv[0] + ro(o)[0], s * Dv[1] + ro(o)[1]];
    if (arms.priority && arms.priority.some((p) => p[0] === Dv[0] && p[1] === Dv[1])) continue;
    lineP(A, st(0.1, HW + 1.0), st(HW - 0.3, HW + 1.0), 0.3, Y);
    lineP(A, W(Dv, HW + 1.15, 0), W(Dv, HW + 2.5, 0), 0.12, Y);
  }
  // a gully at each kerbed corner and a manhole in the box
  manhole(A, 1.6, -1.4);
  setNav(A, { effect: "none" });
  return A;
}
function crosswalk() {
  const { HW } = RD, A = base({ id: "mod_crosswalk", name: "Zebra crosswalk (road overlay)", footprint: [5.4, 7.5], maxGrade: 1.5,
    description: "Zebra crosswalk markings for a 7.5 m road, laid on top of a road line or junction: seven 0.5 m white bars across the carriageway, a 3 m wide crossing, with dashed give-way lines 1 m either side. Walk-over decal." });
  for (let i = 0; i < 7; i++) { const z = -HW + 0.3 + i * 1.0; A.box([-1.5, 0.02, z], [1.5, 0.031, z + 0.5], "line_white", K); }
  for (const s of [-1, 1]) for (let z = -HW + 0.3; z < HW - 0.3; z += 0.9) A.box([s * 2.5 - 0.1, 0.02, z], [s * 2.5 + 0.1, 0.031, z + 0.6], "line_white", K);
  setNav(A, { effect: "none" });
  return A;
}
function roadEnd() {
  const { HW, KW } = RD, A = base({ id: "mod_road_end", name: "Road end (rounded dead end)", footprint: [7.6, 7.8], maxGrade: 1.5,
    description: "Closed end for the 2-lane road: 3.75 m of carriageway finishing in a semicircular kerbed end, channels and edge lines following the curve. Join its -x end to a road line." });
  const x0 = -3.8, xc = -0.05;
  A.box([x0, -0.3, -HW], [xc, 0.02, HW], "asphalt");
  A.cyl([xc, -0.14, 0], [2 * HW, 0.32, 2 * HW], "asphalt", { seg: 24 });
  for (const s of [-1, 1]) {
    A.box([x0, -0.25, s > 0 ? HW : -HW - KW], [xc, 0.15, s > 0 ? HW + KW : -HW], "kerb", { bv: 0.015 });
    A.box([x0, -0.1, s > 0 ? HW - 0.3 : -HW], [xc, 0.024, s > 0 ? HW : -HW + 0.3], "channel", K);
    lineP(A, [x0, s * (HW - 0.45)], [xc, s * (HW - 0.45)], 0.12, 0.026);
  }
  ring(A, xc, 0, HW, HW + KW, -0.25, 0.15, 14, "kerb", {}, -Math.PI / 2, Math.PI / 2);
  ring(A, xc, 0, HW - 0.3, HW, -0.1, 0.024, 14, "channel", K, -Math.PI / 2, Math.PI / 2);
  arcLine(A, xc, 0, HW - 0.45, -Math.PI / 2, Math.PI / 2, 0.12, 0.026);
  lineP(A, [x0, 0], [x0 + 1.5, 0], 0.12, 0.026);
  gully(A, 2.2, 2.3);
  setNav(A, { effect: "none" });
  return A;
}
function roundabout() {
  const { HW, KW } = RD, Ro = 13, Ri = 6, AP = 1.2, S = 18, Y = 0.026;
  const A = base({ id: "mod_roundabout", name: "Roundabout (4 arms)", footprint: [2 * S, 2 * S], maxGrade: 1.5,
    description: "Single-lane roundabout 26 m across: a 5.8 m circulating carriageway round a kerbed central island (granite-sett overrun apron, grassed mound, ornamental tree and planting), four 2-lane arms to the edges with kerbed splitter islands, keep-left bollards and give-way lines." });
  A.cyl([0, -0.14, 0], [2 * Ro, 0.32, 2 * Ro], "asphalt", { seg: 48 });
  const half = Math.asin((HW + KW) / Ro) + 0.06, sx = Math.sqrt(Ro * Ro - (HW + KW) ** 2);
  const near = (a) => [0, Math.PI / 2, Math.PI, -Math.PI / 2, 2 * Math.PI].some((c) => Math.abs(Math.atan2(Math.sin(a - c), Math.cos(a - c))) < half);
  ring(A, 0, 0, Ro, Ro + KW, -0.25, 0.15, 64, "kerb", { skip: near });
  ring(A, 0, 0, Ro - 0.3, Ro, -0.1, 0.024, 64, "channel", { ...K, skip: near });
  // central island
  ring(A, 0, 0, Ri, Ri + AP, -0.1, 0.07, 40, "paver");
  ring(A, 0, 0, Ri + AP, Ri + AP + 0.12, -0.15, 0.08, 40, "kerb", K);
  ring(A, 0, 0, Ri - KW, Ri, -0.25, 0.25, 36, "kerb", { bv: 0.01 });
  A.cyl([0, 0.02, 0], [2 * (Ri - KW), 0.4, 2 * (Ri - KW)], "grass", { seg: 36 });
  A.cone([0, 0.42, 0], [7.5, 0.5, 7.5], "grass", { seg: 24, top: 0.45, k: 0 });
  const R = rng(9500);
  tree(A, R, 0, 0, { h: 6, cr: 2.2, y: 0.6 });
  for (let i = 0; i < 14; i++) { const a = (i / 14) * Math.PI * 2, r = 4.4, s = 0.35; A.rbox([Math.cos(a) * r - s, 0.15, Math.sin(a) * r - s], [Math.cos(a) * r + s, 0.75, Math.sin(a) * r + s], 0.3, i % 2 ? "leaf_dark" : "leaf_light", D); }
  arcLine(A, 0, 0, Ri + AP + 0.4, 0, Math.PI * 2, 0.1, Y);
  // arms
  for (const Dv of [[1, 0], [0, 1], [-1, 0], [0, -1]]) {
    const E = [-Dv[1], Dv[0]], W = (s, o) => [s * Dv[0] + o * E[0], s * Dv[1] + o * E[1]];
    const abox = (s0, s1, o0, o1, y0, y1, m, op = {}) => { const p = [W(s0, o0), W(s1, o1)]; return A.box([Math.min(p[0][0], p[1][0]), y0, Math.min(p[0][1], p[1][1])], [Math.max(p[0][0], p[1][0]), y1, Math.max(p[0][1], p[1][1])], m, op); };
    abox(Ro - 1, S, -HW, HW, -0.3, 0.02, "asphalt");
    for (const sd of [-1, 1]) {
      abox(sx, S, sd * HW, sd * (HW + KW), -0.25, 0.15, "kerb", { bv: 0.015 });
      abox(sx, S, sd * (HW - 0.3), sd * HW, -0.1, 0.024, "channel", K);
      lineP(A, W(sx + 0.3, sd * (HW - 0.45)), W(S, sd * (HW - 0.45)), 0.12, Y);
    }
    // splitter island, bollard, centre line, give-way dashes across the entry lane
    const g = G(A, (Ro + 3.2) * Dv[0], (Ro + 3.2) * Dv[1], yawOf(Dv[0], Dv[1]));
    g.rbox([-2.0, -0.15, -0.65], [2.0, 0.15, 0.65], 0.5, "kerb");
    g.rbox([-1.85, -0.05, -0.5], [1.85, 0.17, 0.5], 0.4, "grass", K);
    g.cyl([-1.4, 0.55, 0], [0.22, 0.8, 0.22], "float_white", { seg: 10 });
    g.cyl([-1.4, 0.75, 0.11], [0.18, 0.18, 0.02], "line_blue", { k: 0, seg: 12, r: [90, 0, 0] });
    for (let s = Ro + 6; s + 1.5 <= S; s += 3) lineP(A, W(s, 0), W(s + 1.5, 0), 0.12, Y);
    const Rv = [Dv[1], -Dv[0]];
    for (let o = 0.8; o < HW - 0.4; o += 0.9) lineP(A, [(Ro + 0.6) * Dv[0] + Rv[0] * o, (Ro + 0.6) * Dv[1] + Rv[1] * o], [(Ro + 0.6) * Dv[0] + Rv[0] * (o + 0.6), (Ro + 0.6) * Dv[1] + Rv[1] * (o + 0.6)], 0.3, Y);
  }
  setNav(A, { effect: "none" });
  return A;
}

// ============================================================================ parking lots
function parkingLot(id, name, per, modules, mid, desc) {
  const SW = 2.5, SL = 5, AI = 6.5, MED = 2.0, EI = 2.5, V = 1.5, MOD = 2 * SL + AI, Y = 0.026;
  const rows = modules * 2, specs = [];
  for (let r = 0; r < rows; r++) {
    const sp = [], reg = r === 0 ? per - 1 : per, at = r === 0 ? Math.floor(per / 2) - 1 : Math.floor(per / 2);
    if (r === 0) sp.push({ w: 3.6, kind: "acc" }, { w: 1.5, kind: "hatch" });
    else sp.push({ w: 2.6, kind: "pad" });   // aligns this row with row 0's wider accessible bay
    for (let i = 0; i < reg; i++) { if (mid && i === at) sp.push({ w: 2.5, kind: "island" }); sp.push({ w: SW, kind: "std" }); }
    specs.push(sp);
  }
  const rowLen = specs[0].reduce((s, q) => s + q.w, 0), X = rowLen + 2 * EI, ZL = modules * MOD + (modules - 1) * MED, Z = ZL + 2 * V;
  const A = base({ id, name, description: desc, footprint: [r3(X), r3(Z)], maxGrade: 1.0 });
  const R = rng(9600 + per * 3 + modules);
  A.box([-X / 2, -0.3, -ZL / 2], [X / 2, 0.02, ZL / 2], "asphalt");
  for (let i = 0; i < Math.round(X * ZL / 120); i++) { const x = (R() - 0.5) * (X - 3), z = (R() - 0.5) * (ZL - 3), w = 0.8 + R() * 2, d = 0.6 + R() * 1.5; A.box([x - w / 2, 0.019, z - d / 2], [x + w / 2, 0.023, z + d / 2], R() < 0.5 ? "asphalt_patch" : "asphalt_light", D); }
  const lights = [];
  // verges front and back with a kerb on the lot side, light columns along them
  for (const s of [-1, 1]) {
    const z0 = s > 0 ? ZL / 2 : -ZL / 2 - V, z1 = s > 0 ? ZL / 2 + V : -ZL / 2;
    A.box([-X / 2, -0.25, s > 0 ? z0 : z1 - 0.15], [X / 2, 0.15, s > 0 ? z0 + 0.15 : z1], "kerb", { bv: 0.02 });
    A.box([-X / 2, -0.2, s > 0 ? z0 + 0.15 : z0], [X / 2, 0.12, s > 0 ? z1 : z1 - 0.15], "grass");
    const np = Math.max(1, Math.round(X / 20));
    for (let k = 0; k < np; k++) { const x = -X / 2 + (X * (k + 0.5)) / np; lights.push(...lightPole(A, x, s * (ZL / 2 + V / 2), s > 0 ? 90 : -90, 8, 1)); }
  }
  for (let m = 0; m < modules; m++) {
    const zm = -ZL / 2 + m * (MOD + MED);
    for (const [ri, head, dir] of [[2 * m, zm, 1], [2 * m + 1, zm + MOD, -1]]) {
      const sp = specs[ri], zs = [head, head + dir * SL].sort((a, b) => a - b);
      let x = -X / 2 + EI;
      island(A, -X / 2, zs[0], -X / 2 + EI, zs[1], "mulch");
      island(A, X / 2 - EI, zs[0], X / 2, zs[1], "mulch");
      tree(A, R, -X / 2 + EI / 2, (zs[0] + zs[1]) / 2, { h: 4.5, cr: 1.4 });
      tree(A, R, X / 2 - EI / 2, (zs[0] + zs[1]) / 2, { h: 4.2, cr: 1.3, kind: R() < 0.5 ? "round" : "column" });
      let prevLine = true;
      for (const q of sp) {
        const x0 = x, x1 = x + q.w; x = x1;
        if (q.kind === "pad") { island(A, x0, zs[0], x1, zs[1], "grass"); prevLine = false; continue; }
        if (q.kind === "island") { island(A, x0, zs[0], x1, zs[1], "mulch"); tree(A, R, (x0 + x1) / 2, (zs[0] + zs[1]) / 2, { h: 5, cr: 1.5 }); prevLine = false; continue; }
        if (!prevLine) lineP(A, [x0 + 0.05, zs[0]], [x0 + 0.05, zs[1]], 0.1, Y);
        lineP(A, [x1, zs[0]], [x1, zs[1]], 0.1, Y);
        prevLine = true;
        if (q.kind === "hatch") {
          for (const sg of diagSegs(zs[0] + 0.1, zs[1] - 0.1, x0 + 0.1, x1 - 0.1, 0.55)) lineP(A, [sg.a[1], sg.a[0]], [sg.b[1], sg.b[0]], 0.1, Y);
          lineP(A, [x0, head + dir * (SL - 0.05)], [x1, head + dir * (SL - 0.05)], 0.1, Y);
          continue;
        }
        if (q.kind === "acc") {
          A.box([x0 + 0.06, 0.016, zs[0] + 0.05], [x1 - 0.06, 0.024, zs[1] - 0.05], "line_blue", K);
          A.cyl([(x0 + x1) / 2, 0.025, head + dir * 2.6], [0.9, 0.006, 0.9], "line_white", { k: 0, seg: 16 });   // blank symbol roundel
          const sx = (x0 + x1) / 2, sz = head - dir * 0.35;
          A.cyl([sx, 1.0, sz], [0.06, 2.0, 0.06], "galv", { seg: 8 });
          A.box([sx - 0.22, 1.6, sz - 0.01], [sx + 0.22, 2.05, sz + 0.01], "line_blue", { k: 0, bv: 0.01 });
        }
        if (R() < 0.35) A.box([(x0 + x1) / 2 - 0.3 - R() * 0.2, 0.0205, head + dir * (1.5 + R()) - 0.3], [(x0 + x1) / 2 + 0.3, 0.0225, head + dir * (2.3 + R())], "sealant", D);   // oil stain
        const wz = head + dir * 0.8;   // wheel stop with two yellow bands
        A.box([(x0 + x1) / 2 - 0.9, 0.0, wz - 0.075], [(x0 + x1) / 2 + 0.9, 0.12, wz + 0.075], "kerb", { k: 0, bv: 0.02, lod: 1 });
        for (const dx of [-0.5, 0.5]) A.box([(x0 + x1) / 2 + dx - 0.15, 0.121, wz - 0.06], [(x0 + x1) / 2 + dx + 0.15, 0.124, wz + 0.06], "line_yellow", D);
      }
    }
    // aisle: arrows both ways, a drain, centre gully
    const za = zm + SL + AI / 2;
    for (const s of [-1, 1]) {
      const x = s * X / 4;
      lineP(A, [x - s * 1.0, za + s * 1.2], [x + s * 1.0, za + s * 1.2], 0.15, Y);
      for (const a of [35, -35]) { const t = rad(a); lineP(A, [x + s * 1.0, za + s * 1.2], [x + s * 1.0 - s * Math.cos(t) * 0.7, za + s * 1.2 + Math.sin(t) * 0.7], 0.15, Y); }
    }
    gully(A, 0, za);
    if (m + 1 < modules) {   // planted median between modules
      const z0 = zm + MOD, z1 = z0 + MED;
      island(A, -X / 2, z0, X / 2, z1, "grass");
      const nt = Math.max(2, Math.round(X / 8));
      for (let k = 0; k < nt; k++) tree(A, R, -X / 2 + (X * (k + 0.5)) / nt, (z0 + z1) / 2, { h: 5 + R(), cr: 1.5, kind: k % 3 === 2 ? "column" : "round" });
      for (const x of [-X / 4, X / 4]) lights.push(...lightPole(A, x + X / (2 * nt), (z0 + z1) / 2, 0, 9, 2));
    }
  }
  A.meta.light = lights.slice(0, 16);
  setNav(A, { effect: "none" });
  return A;
}
function parkingSurface() {
  const X = 30, Z = 20, A = base({ id: "mod_parking_surface", name: "Asphalt hardstanding (unmarked)", footprint: [X + 0.3, Z + 0.3], maxGrade: 1.0,
    description: "30 x 20 m unmarked asphalt hardstanding for open parking, yards or a market: kerbed all round with a 7 m dropped-kerb entrance on the front, two gullies and patch repairs. Lay your own stalls with the parking lots or decals." });
  A.box([-X / 2, -0.3, -Z / 2], [X / 2, 0.02, Z / 2], "asphalt");
  const R = rng(9700);
  for (let i = 0; i < 9; i++) { const x = (R() - 0.5) * (X - 3), z = (R() - 0.5) * (Z - 3), w = 0.8 + R() * 2.5, d = 0.6 + R() * 1.8; A.box([x - w / 2, 0.019, z - d / 2], [x + w / 2, 0.023, z + d / 2], R() < 0.5 ? "asphalt_patch" : "asphalt_light", D); }
  for (let i = 0; i < 8; i++) A.obox([(R() - 0.5) * X * 0.9, 0.0215, (R() - 0.5) * Z * 0.9], [1 + R() * 2.5, 0.004, 0.02], [0, R() * 180, 0], "sealant", D);
  const kb = (x0, z0, x1, z1, h = 0.15) => A.box([x0, -0.25, z0], [x1, h, z1], "kerb", { bv: 0.015 });
  kb(-X / 2 - 0.15, -Z / 2 - 0.15, X / 2 + 0.15, -Z / 2);
  kb(-X / 2 - 0.15, -Z / 2, -X / 2, Z / 2 + 0.15); kb(X / 2, -Z / 2, X / 2 + 0.15, Z / 2 + 0.15);
  kb(-X / 2, Z / 2, -3.5, Z / 2 + 0.15); kb(3.5, Z / 2, X / 2, Z / 2 + 0.15); kb(-3.5, Z / 2, 3.5, Z / 2 + 0.15, 0.04);   // dropped kerb at the entrance
  gully(A, -X / 4, 0); gully(A, X / 4, 0);
  setNav(A, { effect: "none" });
  return A;
}

// ============================================================================ playground
function climbingFrame() {
  const A = base({ id: "mod_play_climbing_frame", name: "Play tower with slide", footprint: [6.6, 3.2],
    description: "Playground climbing frame: 1.4 m square deck tower at 1.4 m on green steel posts with a red pitched roof, guard panels, a climbing wall with coloured holds, a rung ladder, a fireman's pole and a 3 m yellow slide with a run-out." });
  const tx = -1.0, d = 0.7, DY = 1.4;
  for (const [sx, sz] of [[-1, -1], [1, -1], [1, 1], [-1, 1]]) { A.cyl([tx + sx * d, 1.25, sz * d], [0.1, 3.0, 0.1], "steel_green", { seg: 10 }); A.cyl([tx + sx * d, -0.25, sz * d], [0.35, 0.5, 0.35], "concrete", K); }
  A.box([tx - d, DY - 0.05, -d], [tx + d, DY, d], "hpl_green", { bv: 0.01 });
  A.box([tx - d - 0.02, DY - 0.12, -d - 0.02], [tx + d + 0.02, DY - 0.05, d + 0.02], "steel_green", { k: 0, bv: 0.01 });
  for (const s of [-1, 1]) A.obox([tx, 2.95, s * 0.42], [1.7, 0.04, 1.0], [s * 30, 0, 0], "plastic_red", { k: 0, bv: 0.015 });
  A.box([tx - 0.85, 3.17, -0.04], [tx + 0.85, 3.23, 0.04], "steel_green", K);
  A.prism(0, tx - d - 0.03, tx - d + 0.0, [[-d, 2.6], [d, 2.6], [0, 3.15]], "hpl_green", { k: 0 });
  A.prism(0, tx + d - 0.0, tx + d + 0.03, [[-d, 2.6], [d, 2.6], [0, 3.15]], "hpl_green", { k: 0 });
  // +z guard panel, -x rails over the ladder, -z climbing wall
  A.box([tx - d + 0.05, DY + 0.1, d - 0.02], [tx + d - 0.05, DY + 0.95, d + 0.0], "hpl_green", { bv: 0.01 });
  for (const y of [DY + 0.1, DY + 0.95]) rod(A, [tx - d, y, d + 0.02], [tx + d, y, d + 0.02], 0.04, "steel_yellow", K);
  for (const z of [-0.45, 0.45]) rod(A, [tx - d, DY + 0.9, z], [tx - d, DY + 0.9, z * 0.3], 0.04, "steel_yellow", K);
  A.obox([tx, DY / 2, -d - 0.32], [1.3, 1.6, 0.05], [22, 0, 0], "plastic_blue", { bv: 0.02 });
  const R = rng(9800), holds = ["plastic_yellow", "plastic_red", "plastic_orange", "plastic_green"];
  for (let i = 0; i < 12; i++) { const hx = tx - 0.5 + R() * 1.0, hy = 0.15 + R() * 1.15, hz = -d - 0.32 + (hy - DY / 2) * Math.tan(rad(22)) - 0.05; A.rbox([hx - 0.06, hy - 0.04, hz - 0.03], [hx + 0.06, hy + 0.05, hz + 0.04], 0.025, holds[i % 4], D); }
  for (const z of [-0.3, 0.3]) rod(A, [tx - d - 0.9, 0, z], [tx - d, DY + 0.9, z], 0.05, "steel_green", K);
  for (let i = 1; i <= 5; i++) { const f = i / 6.2; rod(A, [tx - d - 0.9 + 0.9 * f, (DY + 0.9) * f, -0.3], [tx - d - 0.9 + 0.9 * f, (DY + 0.9) * f, 0.3], 0.035, "steel_yellow", K); }
  A.obox([tx - d - 0.45, DY / 2 + 0.25, 0], [0.06, 2.45, 0.66], [0, 0, -21], "steel_green", { v: 0 });
  rod(A, [tx + d + 0.45, 0, d + 0.45], [tx + d + 0.45, 2.6, d + 0.45], 0.05, "galv");
  rod(A, [tx + d, 2.55, d], [tx + d + 0.45, 2.55, d + 0.45], 0.05, "galv", K);
  // slide off the +x edge
  const x0 = tx + d, x1 = 2.6, y0 = DY, y1 = 0.35, th = Math.atan2(y0 - y1, x1 - x0), Ls = Math.hypot(x1 - x0, y0 - y1);
  A.obox([(x0 + x1) / 2, (y0 + y1) / 2, 0], [Ls, 0.04, 0.55], [0, 0, -deg(th)], "plastic_yellow", { bv: 0.01 });
  for (const s of [-1, 1]) A.obox([(x0 + x1) / 2, (y0 + y1) / 2 + 0.1, s * 0.3], [Ls, 0.24, 0.04], [0, 0, -deg(th)], "plastic_yellow", { k: 0, bv: 0.015 });
  A.box([x1 - 0.02, y1 - 0.06, -0.3], [3.25, y1 - 0.02, 0.3], "plastic_yellow", { bv: 0.01 });
  for (const s of [-1, 1]) { A.box([x1 - 0.02, y1 - 0.06, s * 0.3 - 0.02], [3.25, y1 + 0.12, s * 0.3 + 0.02], "plastic_yellow", { k: 0, bv: 0.01 }); rod(A, [3.1, 0, s * 0.25], [3.1, y1 - 0.06, s * 0.25], 0.05, "steel_green", K); }
  for (const s of [-1, 1]) { rod(A, [x0, DY, s * 0.32], [x0 + 0.05, DY + 0.7, s * 0.32], 0.04, "steel_yellow", K); rod(A, [x0 + 0.05, DY + 0.7, s * 0.32], [x0 - 0.2, DY + 0.7, s * 0.32], 0.04, "steel_yellow", K); }
  rod(A, [(x0 + x1) / 2, 0, 0], [(x0 + x1) / 2, (y0 + y1) / 2 - 0.04, 0], 0.07, "steel_green");
  setNav(A, { effect: "carve" });
  return A;
}
function swings(id, n) {
  const L = n === 2 ? 3.2 : 6.4, H = 2.3;
  const A = base({ id, name: `Swing set (${n} seats)`, footprint: [L + 0.4, 2.0],
    description: `Playground swing frame ${L} m long: green steel A-frame legs with cross braces and ground anchors, a 2.3 m top beam with hangers, ${n} rubber seats on galvanised chains${n === 4 ? " (one a toddler cradle seat)" : ""}.` });
  rod(A, [-L / 2, H, 0], [L / 2, H, 0], 0.12, "steel_green");
  const legs = n === 2 ? [-L / 2, L / 2] : [-L / 2, 0, L / 2];
  for (const x of legs) {
    for (const s of [-1, 1]) { rod(A, [x, 0, s * 0.95], [x, H + 0.03, 0], 0.09, "steel_green"); A.cyl([x, -0.25, s * 0.95], [0.35, 0.5, 0.35], "concrete", K); }
    rod(A, [x, 0.75, -0.62], [x, 0.75, 0.62], 0.05, "steel_green", K);
    A.box([x - 0.08, H - 0.12, -0.08], [x + 0.08, H + 0.1, 0.08], "steel_green", { k: 0, bv: 0.02 });
  }
  const seats = n === 2 ? [-0.75, 0.75] : [-2.4, -0.8, 0.8, 2.4];
  seats.forEach((x, i) => {
    for (const s of [-1, 1]) { A.box([x - 0.04, H - 0.1, s * 0.22 - 0.03], [x + 0.04, H - 0.05, s * 0.22 + 0.03], "galv", D); rod(A, [x, H - 0.1, s * 0.22], [x, 0.5, s * 0.2], 0.014, "galv", K); }
    if (n === 4 && i === 3) {   // cradle seat
      A.rbox([x - 0.2, 0.38, -0.2], [x + 0.2, 0.6, 0.2], 0.06, "rubber_black", K);
      A.box([x - 0.16, 0.5, -0.16], [x + 0.16, 0.62, 0.16], "plastic_red", { k: 0, bv: 0.03 });
    } else A.box([x - 0.09, 0.46, -0.24], [x + 0.09, 0.5, 0.24], "rubber_black", { k: 0, bv: 0.012 });
  });
  setNav(A, { effect: "carve" });
  return A;
}
function seesaw() {
  const A = base({ id: "mod_play_seesaw", name: "Seesaw", footprint: [3.8, 0.8],
    description: "Two-seat playground seesaw: a 3.6 m yellow steel beam on a green pivot frame, moulded seats with T-handles and half-buried tyre bumpers under each end." });
  A.box([-0.25, 0, -0.3], [0.25, 0.08, 0.3], "steel_green", { bv: 0.01 });
  for (const s of [-1, 1]) A.obox([0, 0.27, s * 0.17], [0.3, 0.5, 0.06], [s * 10, 0, 0], "steel_green", { bv: 0.01 });
  rod(A, [0, 0.5, -0.22], [0, 0.5, 0.22], 0.06, "galv", K);
  const tilt = 9, g = G(A, 0, 0, 0, 0.55);
  g.obox([0, 0, 0], [3.6, 0.1, 0.16], [0, 0, tilt], "steel_yellow", { bv: 0.015 });
  for (const s of [-1, 1]) {
    const x = s * 1.6, cx = Math.cos(rad(tilt)), sy = Math.sin(rad(tilt));
    g.obox([x * cx, x * sy + 0.08, 0], [0.4, 0.06, 0.34], [0, 0, tilt], "plastic_red", { k: 0, bv: 0.02 });
    g.obox([(x - s * 0.35) * cx, (x - s * 0.35) * sy + 0.2, 0], [0.04, 0.3, 0.04], [0, 0, tilt], "steel_green", K);
    g.obox([(x - s * 0.35) * cx, (x - s * 0.35) * sy + 0.35, 0], [0.04, 0.04, 0.4], [0, 0, tilt], "rubber_black", K);
    A.cyl([s * 1.6, 0.02, 0], [0.6, 0.18, 0.6], "rubber_black", { seg: 14 });
  }
  setNav(A, { effect: "carve" });
  return A;
}
function springRider(id, twin) {
  const A = base({ id, name: twin ? "Spring rider (two-seat)" : "Spring rider", footprint: [twin ? 1.4 : 1.0, 0.6],
    description: twin ? "Two-seat playground spring rider: a long green moulded body with two saddles, two handlebars and footrests on a heavy coil spring and buried ground plate." : "Playground spring rider: a stylised yellow animal body with a red saddle, handlebar, footrests and ears on a heavy coil spring and buried ground plate." });
  A.cyl([0, -0.02, 0], [0.5, 0.06, 0.5], "steel_dark", { seg: 14 });
  A.cyl([0, 0.25, 0], [0.13, 0.5, 0.13], "steel_dark", { seg: 8, k: 0 });
  for (let i = 0; i < 7; i++) A.cyl([0, 0.06 + i * 0.065, 0], [0.27, 0.034, 0.27], "steel_red", { seg: 12, k: 0 });
  const body = twin ? "plastic_green" : "plastic_yellow", bl = twin ? 1.3 : 0.85;
  A.rbox([-bl / 2, 0.5, -0.15], [bl / 2, 0.8, 0.15], 0.12, body);
  A.rbox([bl / 2 - 0.25, 0.7, -0.12], [bl / 2 + 0.05, 1.05, 0.12], 0.1, body, K);   // head
  for (const s of [-1, 1]) A.rbox([bl / 2 - 0.15, 1.0, s * 0.08 - 0.03], [bl / 2 - 0.05, 1.15, s * 0.08 + 0.03], 0.02, body, D);
  A.rbox([-bl / 2 - 0.08, 0.55, -0.05], [-bl / 2 + 0.05, 0.75, 0.05], 0.04, body, D);   // tail
  for (const x of twin ? [-0.3, 0.25] : [0]) {
    A.rbox([x - 0.15, 0.78, -0.13], [x + 0.12, 0.84, 0.13], 0.025, twin ? "plastic_yellow" : "plastic_red", K);
    rod(A, [x + 0.2, 0.85, -0.17], [x + 0.2, 0.85, 0.17], 0.03, "rubber_black", K);
    rod(A, [x + 0.2, 0.78, 0], [x + 0.2, 0.86, 0], 0.03, "steel_dark", K);
    rod(A, [x + 0.05, 0.5, -0.24], [x + 0.05, 0.5, 0.24], 0.03, "steel_dark", K);
  }
  A.cyl([bl / 2 - 0.05, 0.92, 0.125], [0.05, 0.01, 0.05], "steel_dark", { k: 0, seg: 8, r: [90, 0, 0] });
  A.cyl([bl / 2 - 0.05, 0.92, -0.125], [0.05, 0.01, 0.05], "steel_dark", { k: 0, seg: 8, r: [90, 0, 0] });
  setNav(A, { effect: "carve" });
  return A;
}
function playRoundabout() {
  const A = base({ id: "mod_play_roundabout", name: "Playground roundabout", footprint: [2.2, 2.2],
    description: "Playground roundabout 2 m across: a non-slip steel deck with a red rim on a sealed bearing hub, four hooped yellow grab rails meeting at a centre post." });
  A.cyl([0, 0.08, 0], [0.5, 0.16, 0.5], "steel_dark", { seg: 12 });
  A.cyl([0, 0.2, 0], [2.0, 0.07, 2.0], "rubber_green", { seg: 28 });
  ring(A, 0, 0, 0.94, 1.0, 0.16, 0.25, 28, "steel_red", K);
  A.cyl([0, 0.65, 0], [0.1, 0.8, 0.1], "steel_yellow", { seg: 10 });
  for (let i = 0; i < 4; i++) {
    const a = (i / 4) * Math.PI * 2 + 0.4, c = Math.cos(a), s = Math.sin(a);
    rod(A, [c * 0.85, 0.24, s * 0.85], [c * 0.85, 0.85, s * 0.85], 0.04, "steel_yellow", K);
    rod(A, [c * 0.85, 0.85, s * 0.85], [c * 0.05, 0.85, s * 0.05], 0.04, "steel_yellow", K);
    rod(A, [c * 0.45, 0.24, s * 0.45], [c * 0.45, 0.85, s * 0.45], 0.035, "steel_yellow", K);
    for (const t of [-0.15, 0.15]) A.box([c * 0.6 + t * -s - 0.02, 0.237, s * 0.6 + t * c - 0.02], [c * 0.6 + t * -s + 0.02, 0.24, s * 0.6 + t * c + 0.02], "steel_dark", D);
  }
  setNav(A, { effect: "carve" });
  return A;
}
function monkeyBars() {
  const L = 3.2, A = base({ id: "mod_play_monkey_bars", name: "Monkey bars", footprint: [L + 0.3, 0.8],
    description: "Overhead ladder 3.2 m long at 2 m: blue steel side rails and eleven rungs, a climbing ladder at each end on concreted feet." });
  for (const x of [-L / 2, L / 2]) {
    for (const z of [-0.28, 0.28]) { rod(A, [x, -0.4, z], [x, 2.05, z], 0.06, "steel_blue"); A.cyl([x, -0.25, z], [0.3, 0.5, 0.3], "concrete", K); }
    for (let y = 0.35; y < 1.9; y += 0.35) rod(A, [x, y, -0.28], [x, y, 0.28], 0.035, "steel_yellow", K);
  }
  for (const z of [-0.28, 0.28]) rod(A, [-L / 2, 2.0, z], [L / 2, 2.0, z], 0.06, "steel_blue", K);
  for (let i = 0; i <= 10; i++) { const x = -L / 2 + 0.1 + (i * (L - 0.2)) / 10; rod(A, [x, 2.0, -0.28], [x, 2.0, 0.28], 0.035, "steel_yellow", K); }
  setNav(A, { effect: "carve" });
  return A;
}
function sandpit() {
  const S = 3.0, A = base({ id: "mod_play_sandpit", name: "Sandpit", footprint: [S + 0.1, S + 0.1],
    description: "3 m square sandpit edged with timber sleepers, corner seats, soft play sand heaped in places, a forgotten bucket and spade." });
  for (const s of [-1, 1]) {
    A.box([-S / 2, -0.15, s > 0 ? S / 2 - 0.2 : -S / 2], [S / 2, 0.3, s > 0 ? S / 2 : -S / 2 + 0.2], "sleeper", { bv: 0.01 });
    A.box([s > 0 ? S / 2 - 0.2 : -S / 2, -0.15, -S / 2 + 0.2], [s > 0 ? S / 2 : -S / 2 + 0.2, 0.3, S / 2 - 0.2], "sleeper", { bv: 0.01 });
  }
  for (const [sx, sz] of [[-1, -1], [1, 1]]) A.prism(1, 0.3, 0.34, [[sx * S / 2, sz * S / 2], [sx * (S / 2 - 0.6), sz * S / 2], [sx * S / 2, sz * (S / 2 - 0.6)]].map((p) => p.map(r3)), "timber");
  A.box([-S / 2 + 0.2, -0.1, -S / 2 + 0.2], [S / 2 - 0.2, 0.16, S / 2 - 0.2], "sand", K);
  const R = rng(9900);
  for (let i = 0; i < 5; i++) { const x = (R() - 0.5) * 1.8, z = (R() - 0.5) * 1.8, s = 0.25 + R() * 0.3; A.rbox([x - s, 0.05, z - s], [x + s, 0.2 + R() * 0.12, z + s], 0.1, "sand", D); }
  A.cone([0.5, 0.25, -0.4], [0.18, 0.18, 0.18], "plastic_blue", { seg: 10, top: 0.75, k: 0, r: [180, 0, 0] });
  A.obox([0.75, 0.19, -0.2], [0.3, 0.02, 0.08], [0, 30, 8], "plastic_red", D);
  setNav(A, { effect: "carve" });
  return A;
}
function safetyTiles(id, c1, c2, label) {
  const S = 4, A = base({ id, name: `Rubber safety tiles (${label}, 4 x 4 m)`, footprint: [S + 0.4, S + 0.4],
    description: `4 x 4 m patch of 500 mm ${label} rubber crumb safety tiles, 45 mm thick with bevelled edges and a ramped surround, for under play equipment. Walk-over.` });
  const n = S / 0.5;
  for (let i = 0; i < n; i++) for (let j = 0; j < n; j++) A.box([-S / 2 + i * 0.5 + 0.002, 0, -S / 2 + j * 0.5 + 0.002], [-S / 2 + (i + 1) * 0.5 - 0.002, 0.045, -S / 2 + (j + 1) * 0.5 - 0.002], (i + j) % 3 ? c1 : c2, { k: 0, bv: 0.006 });
  for (let k = 0; k < 4; k++) G(A, 0, 0, k * 90).wedge([0, 0.0225, S / 2 + 0.1], [S + 0.4, 0.045, 0.2], c2, { k: 0 });
  A.box([-S / 2, -0.05, -S / 2], [S / 2, 0.045, S / 2], c1, { v: 0 });
  setNav(A, { effect: "none" });
  return A;
}
function playFence(gate) {
  const W = gate ? 1.2 : 2.5, A = base({ id: gate ? "mod_play_fence_gate" : "mod_play_fence", name: gate ? "Playground fence gate" : "Playground fence panel (2.5 m)", footprint: [W + 0.1, 0.15],
    description: gate ? "1 m self-closing bow-top playground gate in green steel between two posts, with a high latch. Opens like a door." : "2.5 m bow-top playground fence panel, 1 m high, in green painted steel: square posts with caps, flat rails, round bars with looped bow tops. Panels chain end to end." });
  for (const x of [-W / 2 + 0.03, W / 2 - 0.03]) { A.box([x - 0.03, -0.4, -0.03], [x + 0.03, 1.05, 0.03], "steel_green", { bv: 0.005 }); A.box([x - 0.04, 1.05, -0.04], [x + 0.04, 1.08, 0.04], "steel_green", K); }
  const leaf = { parts: [] }, o = gate ? { into: leaf.parts, k: 0 } : K, x0 = -W / 2 + 0.07, x1 = W / 2 - 0.07;
  for (const y of [0.1, 0.85]) A.box([x0, y - 0.02, -0.01], [x1, y + 0.02, 0.01], "steel_green", o);
  const nb = Math.round((x1 - x0) / 0.125);
  for (let i = 0; i <= nb; i++) { const x = x0 + (i * (x1 - x0)) / nb; A.cyl([x, 0.475, 0], [0.016, 0.75, 0.016], "steel_green", { ...o, seg: 6 }); }
  for (let i = 0; i < nb; i += 2) { const xa = x0 + (i * (x1 - x0)) / nb, xb = x0 + ((i + 2) * (x1 - x0)) / nb; A.arch([(xa + xb) / 2, 0.92, 0], [xb - xa, 0.13, 0.016], "steel_green", { ...o, th: 0.016 }); }
  if (gate) {
    A.box([x1 - 0.12, 0.95, -0.03], [x1, 1.0, 0.03], "steel_red", { into: leaf.parts, k: 0 });
    A.box([x0, 0.08, -0.02], [x1, 1.05, 0.02], "steel_green", { into: leaf.parts, v: 0 });
    leaf.hinge = [x0 - 0.03, 0, 0]; leaf.sign = swingSign("x", [0, 0, 0], leaf.hinge, 1);
    A.interactables.push({ id: "gate", kind: "door", label: "gate", sound: "steel", axis: [0, 1, 0], openAngle: 95, duration: 1.0, defaultOpen: false, leaves: [leaf], aperture: { min: [x0 - 0.03, 0.05, -0.04], max: [x1, 1.05, 0.04] } });
  } else A.box([-W / 2, 0, -0.03], [W / 2, 1.0, 0.03], "steel_green", { v: 0 });
  setNav(A, { effect: gate ? "none" : "carve" });
  return A;
}

// ============================================================================ sports courts and pool
function hoop(A, bx, sgn) {   // basket at x = bx (rim centre), board between it and the baseline side sgn
  const px = bx + sgn * 2.6, fx = bx + sgn * 0.375;
  A.box([px - 0.1, -0.5, -0.1], [px + 0.1, 3.55, 0.1], "steel_dark", { bv: 0.015 });
  A.box([px - 0.13, 0, -0.13], [px + 0.13, 1.8, 0.13], "pad_blue", { k: 0, bv: 0.03 });
  A.box([Math.min(px, fx), 3.35, -0.06], [Math.max(px, fx), 3.47, 0.06], "steel_dark", { k: 0, bv: 0.01 });
  rod(A, [px, 2.6, 0], [fx + sgn * 0.6, 3.36, 0], 0.06, "steel_dark", K);
  A.box([Math.min(fx, fx + sgn * 0.03), 2.9, -0.9], [Math.max(fx, fx + sgn * 0.03), 3.95, 0.9], "glass", { bv: 0.01 });
  for (const [a, b] of [[[2.9, -0.9], [2.95, 0.9]], [[3.9, -0.9], [3.95, 0.9]], [[2.9, -0.9], [3.95, -0.85]], [[2.9, 0.85], [3.95, 0.9]]]) A.box([fx - sgn * 0.005 - 0.005, a[0], a[1]], [fx - sgn * 0.005 + 0.005, b[0], b[1]], "net_white", K);
  for (const [a, b] of [[[3.0, -0.3], [3.05, 0.3]], [[3.4, -0.3], [3.45, 0.3]], [[3.0, -0.3], [3.45, -0.25]], [[3.0, 0.25], [3.45, 0.3]]]) A.box([fx - sgn * 0.005 - 0.005, a[0], a[1]], [fx - sgn * 0.005 + 0.005, b[0], b[1]], "net_white", K);
  ring(A, bx, 0, 0.215, 0.235, 3.04, 3.06, 14, "steel_red", K);
  A.box([Math.min(bx + sgn * 0.22, fx), 3.0, -0.06], [Math.max(bx + sgn * 0.22, fx), 3.06, 0.06], "steel_red", K);
  for (let i = 0; i < 12; i++) { const a = (i / 12) * Math.PI * 2, b = a + 0.5; rod(A, [bx + Math.cos(a) * 0.22, 3.04, Math.sin(a) * 0.22], [bx + Math.cos(b) * 0.14, 2.65, Math.sin(b) * 0.14], 0.008, "net_white", { ...D, seg: 4 }); }
  ring(A, bx, 0, 0.14, 0.15, 2.65, 2.66, 10, "net_white", D);
}
function basketball(full) {
  const CL = full ? 28 : 14, CW = 15, RO = 2, hx = (full ? CL : CL) / 2 + RO, hz = CW / 2 + RO, Y = 0.056;
  const A = base({ id: full ? "mod_basketball_court" : "mod_basketball_half", name: full ? "Outdoor basketball court" : "Outdoor basketball half court", footprint: [2 * hx + 0.2, 2 * hz + 0.2], maxGrade: 0.8,
    description: full ? "Full 28 x 15 m outdoor basketball court on acrylic-coated tarmac with 2 m run-off: full line markings (centre circle, keys, free-throw circles, three-point arcs), two padded posts with glass backboards, rims and nets, and a 3 m mesh fence with an opening." : "Half court (15 x 14 m) for streetball: acrylic-coated tarmac, key, free-throw circle and three-point arc, one padded post with glass backboard, rim and net, 3 m mesh fence with an opening." });
  A.box([-hx, -0.15, -hz], [hx, 0.05, hz], "court_green");
  const x0 = full ? -CL / 2 : -CL / 2, x1 = CL / 2;
  A.box([x0, 0.03, -CW / 2], [x1, 0.052, CW / 2], "court_blue", K);
  const ln = (a, b, w = 0.05) => lineP(A, a, b, w, Y);
  ln([x0, -CW / 2], [x1, -CW / 2]); ln([x0, CW / 2], [x1, CW / 2]); ln([x0, -CW / 2], [x0, CW / 2]); ln([x1, -CW / 2], [x1, CW / 2]);
  if (full) { arcLine(A, 0, 0, 1.8, 0, Math.PI * 2, 0.05, Y); }
  const ends = full ? [1, -1] : [1];
  for (const s of ends) {
    const base0 = s * CL / 2, bx = s * (CL / 2 - 1.575), ft = base0 - s * 5.8;
    A.box([Math.min(base0, ft), 0.04, -2.45], [Math.max(base0, ft), 0.054, 2.45], "court_red", K);   // painted key
    ln([base0, -2.45], [ft, -2.45]); ln([base0, 2.45], [ft, 2.45]); ln([ft, -2.45], [ft, 2.45]);
    const fa = s > 0 ? Math.PI / 2 : -Math.PI / 2;
    arcLine(A, ft, 0, 1.8, fa, fa + Math.PI, 0.05, Y);
    const th = Math.asin(6.6 / 6.75), xe = bx - s * 6.75 * Math.cos(th);
    ln([base0, -6.6], [xe, -6.6]); ln([base0, 6.6], [xe, 6.6]);
    const c = s > 0 ? Math.PI : 0;
    arcLine(A, bx, 0, 6.75, c - th, c + th, 0.05, Y, "line_white", 40);
    arcLine(A, bx, 0, 1.25, c - Math.PI / 2, c + Math.PI / 2, 0.05, Y);
    hoop(A, bx, s);
  }
  perimFence(A, hx, hz, 3.0);
  setNav(A, { effect: "carve" });
  return A;
}
function tennis() {
  const L = 23.77, W = 10.97, SWh = 4.115, hx = 18.29, hz = 9.145, Y = 0.056;
  const A = base({ id: "mod_tennis_court", name: "Tennis court", footprint: [2 * hx + 0.2, 2 * hz + 0.2], maxGrade: 0.6,
    description: "Hard tennis court to full size (23.77 x 10.97 m doubles) with run-off: blue acrylic court on green surround, full line markings, galvanised net posts with winder, net with white headband and centre strap, two courtside benches and a 3 m mesh fence with an opening." });
  A.box([-hx, -0.15, -hz], [hx, 0.05, hz], "court_green");
  A.box([-L / 2 - 0.5, 0.03, -W / 2 - 0.5], [L / 2 + 0.5, 0.052, W / 2 + 0.5], "court_blue", K);
  const ln = (a, b, w = 0.05) => lineP(A, a, b, w, Y);
  for (const s of [-1, 1]) {
    ln([s * L / 2, -W / 2], [s * L / 2, W / 2], 0.1);
    ln([-L / 2, s * W / 2], [L / 2, s * W / 2]); ln([-L / 2, s * SWh], [L / 2, s * SWh]);
    ln([s * 6.4, -SWh], [s * 6.4, SWh]);
    ln([s * L / 2, 0], [s * (L / 2 - 0.1), 0]);
  }
  ln([-6.4, 0], [6.4, 0]);
  for (const s of [-1, 1]) {
    A.cyl([0, 0.55, s * 6.4], [0.08, 1.1, 0.08], "galv", { seg: 10 });
    A.cyl([0, -0.25, s * 6.4], [0.3, 0.5, 0.3], "concrete", K);
    A.cyl([0, 1.1, s * 6.4], [0.1, 0.03, 0.1], "galv", K);
    if (s > 0) A.box([-0.08, 0.3, s * 6.4 - 0.03], [-0.04, 0.4, s * 6.4 + 0.03], "steel_dark", D);
    const t = Math.atan2(1.07 - 0.914, 6.4), Ln = 6.4 / Math.cos(t);
    const top = (1.07 + 0.914) / 2;
    A.obox([0, top, s * 3.2], [0.025, 0.06, Ln], [-s * deg(t), 0, 0], "net_white", K);
    A.obox([0, (top - 0.03 + 0.06) / 2, s * 3.2], [0.012, top - 0.03 - 0.06, Ln], [-s * deg(t), 0, 0], "net_dark", K);
  }
  A.box([-0.025, 0.06, -0.025], [0.025, 0.914, 0.025], "net_white", K);
  A.box([-0.03, 0.06, -6.4], [0.03, 1.0, 6.4], "net_dark", { v: 0 });
  for (const x of [-2.5, 2.5]) bench(G(A, x, hz - 0.8, 180), true, 1.8);
  perimFence(A, hx, hz, 3.0);
  setNav(A, { effect: "carve" });
  return A;
}
function lifeguardChair(g) {
  for (const [sx, sz] of [[-1, -1], [1, -1], [1, 1], [-1, 1]]) g.rod([sx * 0.55, 0, sz * 0.45], [sx * 0.32, 1.75, sz * 0.3], 0.05, "float_white");
  for (const y of [0.6, 1.2]) { g.rod([-0.45, y, 0.38], [0.45, y, 0.38], 0.035, "float_white", K); g.rod([-0.45, y, -0.38], [0.45, y, -0.38], 0.035, "float_white", K); }
  g.box([-0.36, 1.75, -0.32], [0.36, 1.8, 0.32], "decking", { bv: 0.01 });
  g.obox([0, 2.1, -0.33], [0.7, 0.6, 0.04], [-8, 0, 0], "decking", { k: 0, bv: 0.01 });
  for (const s of [-1, 1]) g.box([s * 0.36 - 0.02, 1.8, -0.3], [s * 0.36 + 0.02, 2.05, 0.3], "float_white", K);
  for (let y = 0.3; y < 1.7; y += 0.3) g.rod([-0.42, y, 0.5 - y * 0.03], [0.42, y, 0.5 - y * 0.03], 0.03, "alu", K);
  g.box([-0.3, 1.0, 0.36], [0.3, 1.03, 0.6], "decking", K);
  g.rod([0.4, 1.8, -0.35], [0.4, 3.4, -0.35], 0.04, "alu", K);
  g.cone([0.4, 3.3, -0.35], [2.0, 0.4, 2.0], "canvas_red", { seg: 12, k: 0, top: 0.05 });
}
function pool() {
  const PX = 12.5, PZ = 5.0, WT = 0.25, DX = 15.8, DZ = 8.3, LV = -0.15;
  const A = base({ id: "mod_swimming_pool", name: "Outdoor swimming pool (25 m)", footprint: [2 * DX + 0.2, 2 * DZ + 0.2], maxGrade: 0.4,
    description: "Sunken 25 x 10 m outdoor pool, 1.1 m shallow end sloping to 1.9 m: tiled shell with a dark waterline band, lane lines and three floating lane ropes, underwater lights, corner steps with handrail, two stainless ladders, bullnose coping, 3 m paved deck, lifeguard chair, six sun loungers, two parasols, a life-ring post and a 1.2 m frameless glass pool fence with an opening." });
  // deck and shell
  A.box([-DX, -0.3, PZ + WT], [DX, 0, DZ], "deck_paving"); A.box([-DX, -0.3, -DZ], [DX, 0, -PZ - WT], "deck_paving");
  A.box([-DX, -0.3, -PZ - WT], [-PX - WT, 0, PZ + WT], "deck_paving"); A.box([PX + WT, -0.3, -PZ - WT], [DX, 0, PZ + WT], "deck_paving");
  for (let x = -12; x <= 12; x += 4) for (const s of [-1, 1]) A.box([x - 0.006, -0.001, s > 0 ? PZ + 0.4 : -DZ], [x + 0.006, 0.002, s > 0 ? DZ : -PZ - 0.4], "sealant", D);
  for (const s of [-1, 1]) {
    A.box([s > 0 ? PX : -PX - WT, -2.1, -PZ - WT], [s > 0 ? PX + WT : -PX, -0.06, PZ + WT], "pool_tile");
    A.box([-PX, -2.1, s > 0 ? PZ : -PZ - WT], [PX, -0.06, s > 0 ? PZ + WT : -PZ], "pool_tile");
    A.box([s > 0 ? PX - 0.005 : -PX, -0.35, -PZ], [s > 0 ? PX : -PX + 0.005, -0.06, PZ], "tile_band", K);
    A.box([-PX, -0.35, s > 0 ? PZ - 0.005 : -PZ], [PX, -0.06, s > 0 ? PZ : -PZ + 0.005], "tile_band", K);
    A.box([-PX - 0.4, -0.06, s > 0 ? PZ - 0.05 : -PZ - 0.4], [PX + 0.4, 0.03, s > 0 ? PZ + 0.4 : -PZ + 0.05], "coping", { bv: 0.03 });
    A.box([s > 0 ? PX - 0.05 : -PX - 0.4, -0.06, -PZ + 0.05], [s > 0 ? PX + 0.4 : -PX + 0.05, 0.03, PZ - 0.05], "coping", { bv: 0.03 });
  }
  A.box([-PX, -1.3, -PZ], [-2, -1.1, PZ], "pool_tile");
  A.box([-2, -2.1, -PZ], [4, -1.9, PZ], "pool_tile");
  A.wedge([1, -1.5, 0], [2 * PZ, 0.8, 6], "pool_tile", { r: [0, 90, 0] });
  A.box([4, -2.1, -PZ], [PX, -1.9, PZ], "pool_tile");
  const slope = -deg(Math.atan2(0.8, 6));
  for (const z of [-3.75, -1.25, 1.25, 3.75]) {
    A.box([-11.5, -1.1, z - 0.125], [-2, -1.095, z + 0.125], "tile_dark", K);
    A.obox([1, -1.5 + 0.004, z], [Math.hypot(6, 0.8), 0.006, 0.25], [0, 0, slope], "tile_dark", K);
    A.box([4, -1.9, z - 0.125], [11.5, -1.895, z + 0.125], "tile_dark", K);
    A.box([-11.5, -1.1, z - 0.5], [-11.25, -1.095, z + 0.5], "tile_dark", K);
    A.box([11.25, -1.9, z - 0.5], [11.5, -1.895, z + 0.5], "tile_dark", K);
  }
  // steps, ladders, lights, skimmers, ropes
  for (let k = 0; k < 3; k++) { const top = -0.35 - k * 0.25, x1 = -PX + 0.4 * (k + 1); A.box([-PX, -1.1, -PZ], [x1, top, -3.0], "pool_tile"); A.box([x1 - 0.06, top - 0.002, -PZ], [x1, top + 0.002, -3.0], "coping", K); }
  rod(A, [-PX - 0.3, 0.9, -2.85], [-PX + 0.2, 0.9, -2.85], 0.045, "alu", K); rod(A, [-PX + 0.2, 0.9, -2.85], [-PX + 1.2, -0.6, -2.85], 0.045, "alu", K); rod(A, [-PX - 0.3, 0, -2.85], [-PX - 0.3, 0.9, -2.85], 0.045, "alu", K);
  for (const [x, z, y] of [[10.5, PZ, 180], [10.5, -PZ, 0]]) {
    const g = G(A, x, z, y);
    for (const s of [-1, 1]) { g.rod([s * 0.25, -1.3, 0.12], [s * 0.25, 0.55, 0.12], 0.042, "alu", K); g.rod([s * 0.25, 0.55, 0.12], [s * 0.25, 0.8, -0.1], 0.042, "alu", K); g.rod([s * 0.25, 0.8, -0.1], [s * 0.25, 0.0, -0.32], 0.042, "alu", K); }
    for (const yy of [-0.35, -0.65, -0.95]) g.box([-0.24, yy - 0.02, 0.04], [0.24, yy + 0.02, 0.18], "alu", { k: 0, bv: 0.005 });
  }
  for (let x = -9; x <= 9; x += 6) for (const s of [-1, 1]) rod(A, [x, -0.75, s * PZ], [x, -0.75, s * (PZ - 0.015)], 0.26, "pool_glow", { k: 0, seg: 12 });
  for (const s of [-1, 1]) A.box([s * PX - (s > 0 ? 0.006 : 0), -0.3, -2.5], [s * PX + (s > 0 ? 0 : 0.006), -0.08, -2.0], "steel_dark", K);
  for (const z of [-2.5, 0, 2.5]) for (let i = 0; i < 25; i++) rod(A, [-PX + i, LV + 0.01, z], [-PX + i + 0.98, LV + 0.01, z], 0.11, i % 5 < 2 ? "float_blue" : "float_white", { k: 0, seg: 8 });
  water(A, rectO(-PX, -PZ, PX, PZ), LV);
  // lifeguard chair, loungers, parasols, life ring, glass fence
  lifeguardChair(G(A, 0, PZ + 1.4, 180));
  for (const x of [-12, -9, -6, 4.5, 7.5, 10.5]) lounger(G(A, x, 6.85, 180));
  for (const x of [-7.5, 9]) { A.cyl([x, 1.2, 7.5], [0.05, 2.4, 0.05], "alu", { seg: 8 }); A.cyl([x, 0.05, 7.5], [0.5, 0.1, 0.5], "concrete", { seg: 10 }); A.cone([x, 2.3, 7.5], [2.6, 0.5, 2.6], "canvas_white", { seg: 12, top: 0.04, k: 0 }); }
  A.box([14.2, 0, 7.4], [14.3, 1.6, 7.5], "galv", { bv: 0.01 });
  ring(A, 14.25, 7.6, 0.2, 0.32, 1.1, 1.18, 12, "float_white", D, 0, Math.PI * 2);
  ring(A, 14.25, 7.6, 0.195, 0.325, 1.095, 1.185, 4, "canvas_red", D, 0, Math.PI * 2, 0.2);
  const FX = DX - 0.1, FZ = DZ - 0.1;
  for (const [a, b, gap] of [[[-FX, -FZ], [FX, -FZ], true], [[FX, -FZ], [FX, FZ]], [[FX, FZ], [-FX, FZ]], [[-FX, FZ], [-FX, -FZ]]]) {
    const L = Math.hypot(b[0] - a[0], b[1] - a[1]), n = Math.round(L / 1.5), u = [(b[0] - a[0]) / L, (b[1] - a[1]) / L];
    for (let i = 0; i < n; i++) {
      const s0 = (i * L) / n + 0.02, s1 = ((i + 1) * L) / n - 0.02, mid = (s0 + s1) / 2;
      if (gap && Math.abs(a[0] + u[0] * mid) < 0.9) continue;
      bar(A, [a[0] + u[0] * s0, a[1] + u[1] * s0], [a[0] + u[0] * s1, a[1] + u[1] * s1], 0.012, 0.1, 1.3, "glass");
      for (const t of [0.25, 0.75]) { const s = s0 + (s1 - s0) * t; A.cyl([a[0] + u[0] * s, 0.12, a[1] + u[1] * s], [0.06, 0.26, 0.06], "alu", { k: 0, seg: 8 }); A.cyl([a[0] + u[0] * s, 0.005, a[1] + u[1] * s], [0.12, 0.01, 0.12], "alu", { k: 0, seg: 8 }); }
    }
  }
  setNav(A, { effect: "carve", areas: [{ outline: rectO(-PX, -PZ, PX, PZ).map((p) => p.map(r3)), area: "water" }] });
  A.meta.terrainCut = [{ outline: rectO(-PX - WT, -PZ - WT, PX + WT, PZ + WT).map((p) => p.map(r3)), depth: 2.1, bank: 0.1 }];
  return A;
}
function sunLounger() {
  const A = base({ id: "mod_sun_lounger", name: "Sun lounger", footprint: [0.7, 2.0], description: "Aluminium sun lounger with white resin slats, a navy cushion and raised back rest, wheels at the head end." });
  lounger(G(A, 0, 0, 0));
  setNav(A, { effect: "carve" });
  return A;
}

// ============================================================================ fountains
function fountainTiered() {
  const A = base({ id: "mod_fountain_tiered", name: "Tiered plaza fountain", footprint: [7.8, 7.8],
    description: "Three-tier plaza fountain: a 6.4 m round stone basin with a moulded coping and paved apron, an octagonal plinth carrying two bowls on columns and a finial jet; water falls from each bowl lip in a curtain." });
  ring(A, 0, 0, 3.2, 3.9, -0.05, 0.05, 32, "paver", K);
  ring(A, 0, 0, 2.85, 3.2, -0.1, 0.5, 32, "stone_light");
  ring(A, 0, 0, 2.78, 3.3, 0.5, 0.6, 32, "stone_light", { k: 0, bv: 0.02 });
  ring(A, 0, 0, 3.18, 3.26, 0.05, 0.12, 32, "stone_inset", D);
  A.cyl([0, -0.1, 0], [5.8, 0.3, 5.8], "basin_tile", { seg: 32 });
  A.cyl([0, 0.12, 0], [1.45, 0.2, 1.45], "stone_light", { n: 8 });
  A.cyl([0, 0.45, 0], [1.3, 0.5, 1.3], "stone_light", { n: 8 });
  A.cyl([0, 0.73, 0], [1.42, 0.08, 1.42], "stone_light", { n: 8, k: 0 });
  A.cyl([0, 1.15, 0], [0.42, 0.85, 0.42], "stone_light", { seg: 14, top: 0.8 });
  A.cyl([0, 0.82, 0], [0.55, 0.1, 0.55], "stone_inset", { seg: 14, k: 0 });
  A.cone([0, 1.72, 0], [2.8, 0.35, 2.8], "stone_light", { seg: 24, top: 0.3, r: [180, 0, 0] });
  ring(A, 0, 0, 1.35, 1.46, 1.84, 2.02, 24, "stone_light", K);
  A.cyl([0, 2.2, 0], [0.3, 0.5, 0.3], "stone_light", { seg: 12 });
  A.cyl([0, 2.47, 0], [0.4, 0.06, 0.4], "stone_inset", { seg: 12, k: 0 });
  A.cone([0, 2.63, 0], [1.6, 0.25, 1.6], "stone_light", { seg: 18, top: 0.3, r: [180, 0, 0] });
  ring(A, 0, 0, 0.74, 0.82, 2.7, 2.86, 18, "stone_light", K);
  A.cyl([0, 2.95, 0], [0.16, 0.3, 0.16], "stone_light", { seg: 10, k: 0 });
  A.rbox([-0.12, 3.08, -0.12], [0.12, 3.32, 0.12], 0.1, "stone_light", K);
  A.cone([0, 3.6, 0], [0.07, 0.55, 0.07], "water_fall", { seg: 8, top: 0.4, k: 0 });
  A.cone([0, 3.6, 0], [0.45, 0.35, 0.45], "water_fall", { seg: 10, top: 0.1, k: 0, r: [180, 0, 0] });
  ring(A, 0, 0, 1.43, 1.45, 0.42, 1.98, 24, "water_fall", K);
  ring(A, 0, 0, 0.81, 0.83, 1.98, 2.84, 18, "water_fall", K);
  water(A, circ(2.86, 32), 0.42);
  water(A, circ(1.36, 20), 1.98, { waves: 0.6 });
  water(A, circ(0.75, 14), 2.84, { waves: 0.6 });
  setNav(A, { effect: "carve", cover: Array.from({ length: 8 }, (_, i) => { const a = (i / 8) * Math.PI * 2; return cover([Math.cos(a) * 3.8, 0, Math.sin(a) * 3.8], deg(Math.atan2(-Math.cos(a), -Math.sin(a))), "low"); }) });
  return A;
}
function fountainRound() {
  const A = base({ id: "mod_fountain_round", name: "Round basin fountain", footprint: [6, 6],
    description: "Low round basin fountain 5 m across in pale stone with a granite-sett apron: a central jet rising 2.5 m into a spray crown, ringed by eight arching jets." });
  ring(A, 0, 0, 2.5, 3.0, -0.05, 0.05, 32, "paver", K);
  ring(A, 0, 0, 2.2, 2.5, -0.1, 0.45, 32, "stone_light");
  ring(A, 0, 0, 2.14, 2.58, 0.45, 0.53, 32, "stone_light", { k: 0, bv: 0.02 });
  A.cyl([0, -0.1, 0], [4.5, 0.3, 4.5], "basin_tile", { seg: 32 });
  A.cyl([0, 0.18, 0], [0.5, 0.36, 0.5], "stone_inset", { n: 8 });
  A.cone([0, 1.45, 0], [0.13, 2.2, 0.13], "water_fall", { seg: 8, top: 0.35, k: 0 });
  A.cone([0, 2.65, 0], [0.9, 0.45, 0.9], "water_fall", { seg: 12, top: 0.15, k: 0, r: [180, 0, 0] });
  ring(A, 0, 0, 0.25, 0.85, 0.36, 0.4, 12, "water_fall", D);
  for (let i = 0; i < 8; i++) {
    const a = (i / 8) * Math.PI * 2, c = Math.cos(a), s = Math.sin(a), pts = [[1.6, 0.38], [1.25, 1.05], [0.85, 1.22], [0.45, 0.85], [0.3, 0.4]];
    A.cyl([c * 1.6, 0.38, s * 1.6], [0.07, 0.05, 0.07], "steel_dark", { k: 0, seg: 8 });
    for (let k = 0; k + 1 < pts.length; k++) rod(A, [c * pts[k][0], pts[k][1], s * pts[k][0]], [c * pts[k + 1][0], pts[k + 1][1], s * pts[k + 1][0]], 0.035, "water_fall", { k: 0, seg: 6 });
  }
  water(A, circ(2.21, 32), 0.36, { waves: 0.8 });
  setNav(A, { effect: "carve", cover: Array.from({ length: 6 }, (_, i) => { const a = (i / 6) * Math.PI * 2; return cover([Math.cos(a) * 2.95, 0, Math.sin(a) * 2.95], deg(Math.atan2(-Math.cos(a), -Math.sin(a))), "low"); }) });
  return A;
}
function fountainWall() {
  const o = -0.12, A = base({ id: "mod_fountain_wall", name: "Wall fountain", footprint: [3.4, 1.9],
    description: "Free-standing wall fountain: a 3.2 m rendered wall with stone pilasters, coping and a dressed inset panel; a steel spout pours a stream into a raised stone trough basin." });
  A.box([-1.6, -0.3, -0.9 - o], [1.6, 2.4, -0.5 - o], "render");
  A.box([-1.7, 2.4, -0.96 - o], [1.7, 2.52, -0.44 - o], "stone_light", { bv: 0.02 });
  for (const x of [-1.45, 1.45]) A.box([x - 0.2, 0, -0.98 - o], [x + 0.2, 2.42, -0.42 - o], "stone_light", { bv: 0.015 });
  A.box([-0.8, 0.7, -0.52 - o], [0.8, 2.0, -0.48 - o], "stone_inset", { k: 0, bv: 0.01 });
  A.box([-0.9, 2.0, -0.53 - o], [0.9, 2.08, -0.44 - o], "stone_light", { k: 0, bv: 0.01 });
  A.box([-0.06, 1.25, -0.48 - o], [0.06, 1.33, -0.25 - o], "steel_dark", { k: 0, bv: 0.01 });
  A.cyl([0, 1.3, -0.48 - o], [0.2, 0.02, 0.2], "steel_dark", { k: 0, seg: 12, r: [90, 0, 0] });
  const pts = [[1.27, -0.25], [1.15, -0.18], [0.9, -0.12], [0.5, -0.08]];
  for (let k = 0; k + 1 < pts.length; k++) rod(A, [0, pts[k][0], pts[k][1] - o], [0, pts[k + 1][0], pts[k + 1][1] - o], 0.05, "water_fall", { k: 0, seg: 6 });
  const bz0 = -0.5 - o, bz1 = 0.6 - o;
  A.box([-1.3, -0.1, bz1 - 0.2], [1.3, 0.6, bz1], "stone_light", { bv: 0.015 });
  for (const s of [-1, 1]) A.box([s > 0 ? 1.1 : -1.3, -0.1, bz0], [s > 0 ? 1.3 : -1.1, 0.6, bz1 - 0.2], "stone_light", { bv: 0.015 });
  A.box([-1.36, 0.6, bz1 - 0.26], [1.36, 0.67, bz1 + 0.06], "stone_light", { k: 0, bv: 0.015 });
  A.box([-1.1, -0.1, bz0], [1.1, 0.1, bz1 - 0.2], "basin_tile");
  water(A, rectO(-1.11, bz0, 1.11, bz1 - 0.19), 0.5, { waves: 0.7 });
  setNav(A, { effect: "carve", cover: [cover([-0.9, 0, -1.4 - o], 0, "high", ["left"]), cover([0.9, 0, -1.4 - o], 0, "high", ["right"]), cover([0, 0, 1.1 - o], 180, "low")] });
  return A;
}

// ============================================================================ garden
function planterConcrete() {
  const A = base({ id: "mod_planter_concrete", name: "Concrete raised planter", footprint: [2.5, 1.0],
    description: "2.4 x 0.9 m precast concrete raised planter, 0.6 m high with a chamfered rim, filled with clipped shrubs and ornamental grasses over bark mulch. Sit-on height edge; low cover." });
  const W = 1.2, Dd = 0.45, H = 0.6, t = 0.08;
  for (const s of [-1, 1]) { A.box([-W, 0, s > 0 ? Dd - t : -Dd], [W, H, s > 0 ? Dd : -Dd + t], "concrete", { bv: 0.012 }); A.box([s > 0 ? W - t : -W, 0, -Dd + t], [s > 0 ? W : -W + t, H, Dd - t], "concrete", { bv: 0.012 }); }
  A.box([-W - 0.01, -0.03, -Dd - 0.01], [W + 0.01, 0.02, Dd + 0.01], "slab_dark", K);
  A.box([-W + t, 0.1, -Dd + t], [W - t, 0.5, Dd - t], "mulch", K);
  const R = rng(10100);
  for (let i = 0; i < 4; i++) { const x = -0.8 + i * 0.53, s = 0.22 + R() * 0.06; A.rbox([x - s, 0.45, -s], [x + s, 0.5 + 2 * s, s], s * 0.9, i % 2 ? "leaf" : "leaf_dark", K); }
  for (let i = 0; i < 5; i++) { const x = -0.95 + i * 0.47, z = (i % 2 ? 1 : -1) * 0.2; for (let b = 0; b < 6; b++) A.obox([x, 0.75, z], [0.02, 0.55, 0.008], [(R() - 0.5) * 30, b * 30, (R() - 0.5) * 30], "grass_blade", D); }
  setNav(A, { effect: "carve", cover: [cover([0, 0, -0.9], 0, "low"), cover([0, 0, 0.9], 180, "low")] });
  return A;
}
function planterCorten() {
  const A = base({ id: "mod_planter_corten", name: "Corten planter with tree", footprint: [1.3, 1.3],
    description: "1.2 m square weathering-steel planter, 0.8 m high, holding a small multi-stem ornamental tree with an underplanting of low shrubs." });
  const S = 0.6, t = 0.012;
  for (const s of [-1, 1]) { A.box([-S, 0, s > 0 ? S - t : -S], [S, 0.8, s > 0 ? S : -S + t], "corten"); A.box([s > 0 ? S - t : -S, 0, -S + t], [s > 0 ? S : -S + t, 0.8, S - t], "corten"); }
  for (const s of [-1, 1]) { A.box([-S - 0.01, 0.78, s * S - 0.025], [S + 0.01, 0.8, s * S + 0.025], "corten", K); A.box([s * S - 0.025, 0.78, -S], [s * S + 0.025, 0.8, S], "corten", K); }
  A.box([-S + t, 0.3, -S + t], [S - t, 0.72, S - t], "mulch", K);
  const R = rng(10200);
  tree(A, R, 0, 0, { h: 3.2, cr: 1.0, y: 0.7, leaf: "leaf_light" });
  for (let i = 0; i < 4; i++) { const a = (i / 4) * Math.PI * 2 + 0.7, s = 0.16; A.rbox([Math.cos(a) * 0.38 - s, 0.68, Math.sin(a) * 0.38 - s], [Math.cos(a) * 0.38 + s, 0.95, Math.sin(a) * 0.38 + s], 0.14, "leaf_dark", D); }
  setNav(A, { effect: "carve", cover: [cover([0, 0, -1.0], 0, "low"), cover([0, 0, 1.0], 180, "low")] });
  return A;
}
function raisedBed() {
  const A = base({ id: "mod_raised_bed", name: "Timber raised bed", footprint: [2.5, 1.3],
    description: "2.4 x 1.2 m raised vegetable bed of three courses of timber sleepers with lapped corners, planted with rows of lettuces, herbs and a few marigolds." });
  const W = 1.2, Dd = 0.6;
  for (let c = 0; c < 3; c++) {
    const y0 = c * 0.15, y1 = y0 + 0.15, e = c % 2 ? 0 : 0.1;
    for (const s of [-1, 1]) A.box([-W - e, y0, s > 0 ? Dd - 0.1 : -Dd], [W + e, y1 - 0.004, s > 0 ? Dd : -Dd + 0.1], "sleeper", { bv: 0.01 });
    for (const s of [-1, 1]) A.box([s > 0 ? W - 0.1 : -W, y0, -Dd - (c % 2 ? 0.1 : 0)], [s > 0 ? W : -W + 0.1, y1 - 0.004, Dd + (c % 2 ? 0.1 : 0)], "sleeper", { bv: 0.01 });
  }
  A.box([-W + 0.1, 0.1, -Dd + 0.1], [W - 0.1, 0.4, Dd - 0.1], "mulch", K);
  const R = rng(10300);
  for (let r = 0; r < 3; r++) for (let i = 0; i < 7; i++) {
    const x = -0.9 + i * 0.3, z = -0.3 + r * 0.3, s = 0.08 + R() * 0.04;
    if (r === 2 && i % 2) { A.rbox([x - 0.06, 0.38, z - 0.06], [x + 0.06, 0.55, z + 0.06], 0.05, "leaf", D); A.box([x - 0.04, 0.55, z - 0.04], [x + 0.04, 0.58, z + 0.04], "petal_yellow", D); }
    else A.rbox([x - s, 0.38, z - s], [x + s, 0.42 + s, z + s], s * 0.9, r === 1 ? "leaf_red" : "leaf_light", D);
  }
  setNav(A, { effect: "carve", cover: [cover([0, 0, -1.05], 0, "low"), cover([0, 0, 1.05], 180, "low")] });
  return A;
}
function flowerBed() {
  const A = base({ id: "mod_flower_bed", name: "Flower bed (steel edged)", footprint: [3.1, 1.6],
    description: "3 x 1.5 m modern flower bed edged in weathering-steel strip: bark mulch with drifts of red, yellow, white, purple and pink perennials among leafy mounds. Walk-over." });
  const W = 1.5, Dd = 0.75;
  for (const s of [-1, 1]) { A.box([-W, -0.08, s * Dd - 0.004], [W, 0.06, s * Dd + 0.004], "corten", K); A.box([s * W - 0.004, -0.08, -Dd], [s * W + 0.004, 0.06, Dd], "corten", K); }
  A.box([-W + 0.005, -0.05, -Dd + 0.005], [W - 0.005, 0.04, Dd - 0.005], "mulch", K);
  const R = rng(10400), pet = ["petal_red", "petal_yellow", "petal_white", "petal_purple", "petal_pink"];
  for (let i = 0; i < 9; i++) for (let j = 0; j < 4; j++) {
    const x = -1.3 + i * 0.32 + (R() - 0.5) * 0.08, z = -0.55 + j * 0.37 + (R() - 0.5) * 0.08, s = 0.1 + R() * 0.06, h = 0.18 + R() * 0.2;
    A.rbox([x - s, 0.02, z - s], [x + s, h, z + s], s * 0.8, (i + j) % 3 ? "leaf" : "leaf_dark", D);
    if ((i * 7 + j * 3) % 4 !== 0) { const m = pet[Math.floor((i / 9) * 5 + j * 0.3) % 5]; for (let k = 0; k < 3; k++) { const fx = x + (R() - 0.5) * s * 1.4, fz = z + (R() - 0.5) * s * 1.4; A.box([fx - 0.025, h - 0.01, fz - 0.025], [fx + 0.025, h + 0.03 + R() * 0.04, fz + 0.025], m, D); } }
  }
  setNav(A, { effect: "none" });
  return A;
}
function ornamentalTree(id, name, opt, desc) {
  const A = base({ id, name, footprint: [opt.cr * 2 + 0.4, opt.cr * 2 + 0.4], description: desc });
  tree(A, rng(10500 + opt.h * 10), 0, 0, opt);
  A.cyl([0, 0.0, 0], [1.2, 0.04, 1.2], "mulch", { k: 0, seg: 14 });
  setNav(A, { effect: "carve", cover: [cover([0, 0, -0.7], 0, "high", ["left", "right"]), cover([0, 0, 0.7], 180, "high", ["left", "right"])] });
  return A;
}
function pot(A, d = 0.6) {
  A.cone([0, 0.3, 0], [d, 0.6, d], "pot_fibre", { seg: 16, top: 0.75, r: [180, 0, 0] });
  A.cyl([0, 0.58, 0], [d + 0.04, 0.04, d + 0.04], "pot_fibre", { seg: 16, k: 0 });
  A.cyl([0, 0.595, 0], [d - 0.06, 0.02, d - 0.06], "mulch", { seg: 14, k: 0 });
}
function topiary(id, kind) {
  const A = base({ id, name: kind === "ball" ? "Topiary ball in pot" : kind === "cone" ? "Topiary cone in pot" : "Topiary balls (group of three)", footprint: kind === "group" ? [2.2, 1.4] : [0.7, 0.7],
    description: kind === "ball" ? "Clipped box topiary ball 0.8 m across on a short stem in a tapered grey planter." : kind === "cone" ? "Clipped yew topiary cone 1.6 m tall in a tapered grey planter." : "Three clipped box balls (0.5, 0.75 and 1.0 m) planted in a bark-mulched bed. Low cover." });
  const R = rng(10600 + id.length);
  if (kind === "ball") {
    pot(A);
    A.cyl([0, 0.7, 0], [0.04, 0.2, 0.04], "bark", { seg: 6, k: 0 });
    A.rbox([-0.4, 0.75, -0.4], [0.4, 1.55, 0.4], 0.4, "leaf");
    for (let i = 0; i < 6; i++) { const a = R() * 6.28, b = (R() - 0.5) * 2.2, r = 0.36, s = 0.11; const x = Math.cos(a) * Math.cos(b) * r, y = 1.15 + Math.sin(b) * r, z = Math.sin(a) * Math.cos(b) * r; A.rbox([x - s, y - s, z - s], [x + s, y + s, z + s], s, i % 2 ? "leaf_dark" : "leaf_light", D); }
  } else if (kind === "cone") {
    pot(A);
    A.cone([0, 1.42, 0], [0.75, 1.6, 0.75], "leaf_dark", { seg: 16 });
    for (let i = 0; i < 4; i++) A.cyl([0, 0.75 + i * 0.32, 0], [0.7 - i * 0.16, 0.12, 0.7 - i * 0.16], "leaf", { seg: 14, k: 0 });
  } else {
    A.cyl([0, 0.0, 0], [2.0, 0.04, 1.2], "mulch", { seg: 16, k: 0 });
    for (const [x, z, d] of [[-0.6, 0.1, 1.0], [0.3, -0.2, 0.75], [0.85, 0.3, 0.5]]) {
      A.rbox([x - d / 2, -0.05, z - d / 2], [x + d / 2, d - 0.05, z + d / 2], d / 2, "leaf");
      for (let i = 0; i < 4; i++) { const a = R() * 6.28, s = d * 0.13; A.rbox([x + Math.cos(a) * d * 0.42 - s, d * 0.5 - s, z + Math.sin(a) * d * 0.42 - s], [x + Math.cos(a) * d * 0.42 + s, d * 0.5 + s, z + Math.sin(a) * d * 0.42 + s], s, "leaf_dark", D); }
    }
  }
  setNav(A, { effect: "carve", cover: kind === "group" ? [cover([-0.6, 0, -1.0], 0, "low"), cover([-0.6, 0, 1.1], 180, "low")] : [] });
  return A;
}
function pergola() {
  const X = 2.0, Z = 1.45, H = 2.4, A = base({ id: "mod_pergola", name: "Timber pergola", footprint: [4.7, 3.5],
    description: "4 x 3 m oak pergola: four 150 mm posts on steel shoes, twin beams, notched rafters at 400 mm with shaped ends, knee braces and a climbing rose scrambling over one corner." });
  for (const sx of [-1, 1]) for (const sz of [-1, 1]) {
    A.box([sx * X - 0.075, 0, sz * Z - 0.075], [sx * X + 0.075, H, sz * Z + 0.075], "timber", { bv: 0.01 });
    A.box([sx * X - 0.09, 0, sz * Z - 0.09], [sx * X + 0.09, 0.12, sz * Z + 0.09], "steel_dark", { k: 0, bv: 0.005 });
    A.obox([sx * (X - 0.25), H - 0.3, sz * Z], [0.5, 0.07, 0.07], [0, 0, sx * 45], "timber", K);
  }
  for (const sz of [-1, 1]) A.box([-X - 0.35, H - 0.02, sz * Z - 0.04], [X + 0.35, H + 0.2, sz * Z + 0.04], "timber", { k: 0, bv: 0.01 });
  for (let x = -X - 0.2; x <= X + 0.21; x += 0.4) {
    A.box([x - 0.025, H + 0.12, -Z - 0.3], [x + 0.025, H + 0.3, Z + 0.3], "timber", { k: 0, bv: 0.006 });
    for (const sz of [-1, 1]) A.wedge([x, H + 0.06, sz * (Z + 0.38)], [0.05, 0.12, 0.16], "timber", { k: 0, r: [0, sz > 0 ? 0 : 180, 0] });
  }
  const R = rng(10700);
  for (let i = 0; i < 9; i++) { const t = i / 8, s = 0.18 + R() * 0.12; const x = X + (R() - 0.5) * 0.3 - (t > 0.6 ? (t - 0.6) * 3 : 0), y = Math.min(H + 0.35, 0.3 + t * 2.6), z = Z + (R() - 0.5) * 0.3; A.rbox([x - s, y - s, z - s], [x + s, y + s, z + s], s * 0.8, "leaf", D); if (R() < 0.6) A.box([x - 0.03, y + s * 0.6, z + s * 0.6], [x + 0.03, y + s * 0.6 + 0.04, z + s * 0.6 + 0.04], "petal_pink", D); }
  setNav(A, { effect: "carve" });
  return A;
}
function gazebo() {
  const r = 1.85, A = base({ id: "mod_gazebo", name: "Octagonal gazebo", footprint: [4.5, 4.5],
    description: "Octagonal timber gazebo 3.7 m across: raised decked floor with a front step, eight posts, a low balustrade on seven sides, ring beam and an octagonal shingled roof with a finial." });
  A.cyl([0, 0.15, 0], [2 * r + 0.2, 0.3, 2 * r + 0.2], "decking", { n: 8 });
  A.box([-0.7, 0, r - 0.02], [0.7, 0.15, r + 0.35], "decking", { bv: 0.01 });
  const pa = (i) => ((i + 0.5) / 8) * Math.PI * 2;
  for (let i = 0; i < 8; i++) { const a = pa(i); A.box([Math.cos(a) * r - 0.06, 0.3, Math.sin(a) * r - 0.06], [Math.cos(a) * r + 0.06, 2.45, Math.sin(a) * r + 0.06], "white_wood", { bv: 0.008 }); }
  ring(A, 0, 0, r - 0.08, r + 0.06, 2.35, 2.55, 8, "white_wood", K, 0, Math.PI * 2, 0);
  A.cone([0, 3.2, 0], [2 * r + 0.9, 1.3, 2 * r + 0.9], "shingle", { n: 8, top: 0.04, r: [0, 22.5, 0] });
  A.cone([0, 4.0, 0], [0.12, 0.45, 0.12], "steel_dark", { k: 0, seg: 8 });
  for (let i = 0; i < 8; i++) {
    const a0 = pa(i - 1), a1 = pa(i), am = (a0 + a1) / 2;
    if (Math.abs(Math.atan2(Math.sin(am - Math.PI / 2), Math.cos(am - Math.PI / 2))) < 0.2) continue;   // the entrance side (+z)
    const p = [Math.cos(a0) * r, Math.sin(a0) * r], q = [Math.cos(a1) * r, Math.sin(a1) * r];
    bar(A, p, q, 0.07, 0.95, 1.0, "white_wood", { bv: 0.008 });
    bar(A, p, q, 0.05, 0.38, 0.43, "white_wood", K);
    for (let k = 1; k < 7; k++) { const t = k / 7; A.box([p[0] + (q[0] - p[0]) * t - 0.02, 0.43, p[1] + (q[1] - p[1]) * t - 0.02], [p[0] + (q[0] - p[0]) * t + 0.02, 0.95, p[1] + (q[1] - p[1]) * t + 0.02], "white_wood", K); }
  }
  setNav(A, { effect: "carve" });
  return A;
}
function benchAsset(back) {
  const A = base({ id: back ? "mod_garden_bench" : "mod_bench_backless", name: back ? "Garden bench" : "Backless park bench", footprint: back ? [1.7, 0.7] : [2.1, 0.6],
    description: back ? "1.6 m slatted hardwood garden bench with backrest and armrests on black steel ends." : "2 m backless hardwood bench on two precast concrete plinths, for parks and plazas." });
  if (back) bench(G(A, 0, 0, 0), true, 1.6);
  else {
    for (const x of [-0.75, 0.75]) A.box([x - 0.12, 0, -0.22], [x + 0.12, 0.4, 0.22], "concrete", { bv: 0.02 });
    for (let i = 0; i < 5; i++) { const z = -0.24 + i * 0.1; A.box([-1.0, 0.4, z], [1.0, 0.45, z + 0.085], "timber", { k: 0, bv: 0.008 }); }
    A.box([-1, 0, -0.25], [1, 0.45, 0.25], "timber", { v: 0 });
  }
  setNav(A, { effect: "carve" });
  return A;
}
function birdBath() {
  const A = base({ id: "mod_bird_bath", name: "Bird bath", footprint: [0.7, 0.7], description: "Cast-stone bird bath: octagonal foot, fluted tapering pedestal and a shallow 0.6 m bowl of water." });
  A.cyl([0, 0.05, 0], [0.46, 0.1, 0.46], "stone_light", { n: 8 });
  A.cyl([0, 0.42, 0], [0.22, 0.64, 0.22], "stone_light", { seg: 10, top: 0.7 });
  for (let i = 0; i < 8; i++) { const a = (i / 8) * Math.PI * 2; A.box([Math.cos(a) * 0.1 - 0.012, 0.15, Math.sin(a) * 0.1 - 0.012], [Math.cos(a) * 0.1 + 0.012, 0.65, Math.sin(a) * 0.1 + 0.012], "stone_inset", D); }
  A.cone([0, 0.8, 0], [0.64, 0.12, 0.64], "stone_light", { seg: 16, top: 0.4, r: [180, 0, 0] });
  ring(A, 0, 0, 0.27, 0.32, 0.82, 0.88, 16, "stone_light", K);
  water(A, circ(0.28, 16), 0.86, { waves: 0.2 });
  setNav(A, { effect: "carve" });
  return A;
}
function steppingStones() {
  const A = base({ id: "mod_stepping_stones", name: "Stepping-stone path (4.5 m)", footprint: [4.8, 0.9], description: "4.5 m path of seven irregular slate stepping stones set flush in the lawn along a gentle curve, with grass tufts at the joints. Walk-over." });
  const R = rng(10800);
  for (let i = 0; i < 7; i++) {
    const x = -2.1 + i * 0.7, z = Math.sin(i * 0.9) * 0.15, w = 0.5 + R() * 0.1, d = 0.4 + R() * 0.1;
    A._put({ t: "r", c: [x, -0.01, z].map(R4), s: [w, 0.07, d].map(R4), rad: 0.03, m: R() < 0.5 ? "slate" : "stone_inset", r: [0, R4((R() - 0.5) * 30), 0] }, K);
    for (let b = 0; b < 3; b++) A.obox([x + 0.35, 0.04, z + (R() - 0.5) * 0.4], [0.015, 0.1, 0.006], [(R() - 0.5) * 40, R() * 180, 0], "grass_blade", D);
  }
  setNav(A, { effect: "none" });
  return A;
}
function decking() {
  const X = 2.5, Z = 2.0, H = 0.45, A = base({ id: "mod_decking_terrace", name: "Timber decking terrace", footprint: [5.2, 4.5],
    description: "5 x 4 m hardwood decking terrace 0.45 m off the ground: grooved boards with gaps on hidden joists, fascia boards, two full-width steps at the front and a post-and-rail balustrade on the other three sides." });
  for (let x = -X + 0.3; x <= X - 0.29; x += 1.1) for (const z of [-Z + 0.2, 0, Z - 0.2]) A.box([x - 0.05, -0.2, z - 0.05], [x + 0.05, H - 0.17, z + 0.05], "timber_grey", D);
  for (let z = -Z + 0.2; z <= Z - 0.19; z += 0.45) A.box([-X + 0.05, H - 0.2, z - 0.025], [X - 0.05, H - 0.03, z + 0.025], "timber_grey", D);
  for (let z = -Z; z < Z - 0.01; z += 0.146) A.box([-X, H - 0.03, z + 0.003], [X, H, z + 0.143], "decking", { k: 0, bv: 0.004 });
  for (const s of [-1, 1]) { A.box([-X - 0.02, 0.0, s > 0 ? Z : -Z - 0.02], [X + 0.02, H - 0.005, s > 0 ? Z + 0.02 : -Z], "decking", K); A.box([s > 0 ? X : -X - 0.02, 0.0, -Z], [s > 0 ? X + 0.02 : -X, H - 0.005, Z], "decking", K); }
  A.box([-X, 0, -Z], [X, H, Z], "decking", { v: 0 });
  for (const [z0, z1, y] of [[Z + 0.02, Z + 0.32, 0.3], [Z + 0.32, Z + 0.62, 0.15]]) { A.box([-1.2, 0, z0], [1.2, y, z1], "decking", { bv: 0.006 }); }
  const rail = (a, b) => { bar(A, a, b, 0.09, H + 0.92, H + 0.96, "decking", { bv: 0.008 }); bar(A, a, b, 0.04, H + 0.5, H + 0.54, "decking", K); bar(A, a, b, 0.04, H + 0.2, H + 0.24, "decking", K); bar(A, a, b, 0.05, H, H + 0.95, "decking", { v: 0 }); };
  const posts = [[-X + 0.05, -Z + 0.05], [X - 0.05, -Z + 0.05], [X - 0.05, Z - 0.05], [-X + 0.05, Z - 0.05], [-1.25, Z - 0.05], [1.25, Z - 0.05], [0, -Z + 0.05], [-X + 0.05, 0], [X - 0.05, 0]];
  for (const [x, z] of posts) A.box([x - 0.045, H - 0.2, z - 0.045], [x + 0.045, H + 0.96, z + 0.045], "decking", { k: 0, bv: 0.006 });
  rail([-X + 0.05, -Z + 0.05], [X - 0.05, -Z + 0.05]); rail([-X + 0.05, -Z + 0.05], [-X + 0.05, Z - 0.05]); rail([X - 0.05, -Z + 0.05], [X - 0.05, Z - 0.05]);
  rail([-X + 0.05, Z - 0.05], [-1.25, Z - 0.05]); rail([1.25, Z - 0.05], [X - 0.05, Z - 0.05]);
  setNav(A, { effect: "carve" });
  return A;
}
function lampPost(street) {
  const A = base({ id: street ? "mod_street_lamp" : "mod_lamp_post", name: street ? "Street light (8 m)" : "Garden lamp post", footprint: street ? [1.8, 0.5] : [0.4, 0.4],
    description: street ? "8 m galvanised tapered street-light column on a cast footing with a service door and a single outreach arm carrying a flat LED lantern. Lights at dusk." : "3.2 m black steel garden lamp post with a fluted base, slim shaft and a square glazed lantern under a pyramid cap. Lights at dusk." });
  if (street) { A.meta.light = lightPole(A, -0.55, 0, 0, 8, 1); }
  else {
    A.cyl([0, 0.2, 0], [0.24, 0.4, 0.24], "steel_dark", { n: 8 });
    A.cyl([0, 0.42, 0], [0.18, 0.06, 0.18], "steel_dark", { seg: 10, k: 0 });
    A.cyl([0, 1.5, 0], [0.08, 2.2, 0.08], "steel_dark", { seg: 10 });
    A.box([-0.13, 2.6, -0.13], [0.13, 2.64, 0.13], "steel_dark", { k: 0, bv: 0.005 });
    A.box([-0.11, 2.64, -0.11], [0.11, 2.98, 0.11], "glass", K);
    for (const [sx, sz] of [[-1, -1], [1, -1], [1, 1], [-1, 1]]) A.box([sx * 0.11 - 0.012, 2.64, sz * 0.11 - 0.012], [sx * 0.11 + 0.012, 2.98, sz * 0.11 + 0.012], "steel_dark", K);
    A.cyl([0, 2.75, 0], [0.08, 0.16, 0.08], "lamp_glow", { k: 0, seg: 8 });
    A.cone([0, 3.06, 0], [0.32, 0.16, 0.32], "steel_dark", { n: 4, k: 0, r: [0, 45, 0] });
    A.cyl([0, 3.18, 0], [0.04, 0.08, 0.04], "steel_dark", { k: 0, seg: 6 });
    A.meta.light = { offset: [0, 2.8, 0], use: "street", color: [1, 0.86, 0.66], intensity: 1.6, radius: 9, shadow: 1 };
  }
  setNav(A, { effect: "carve" });
  return A;
}
function shed() {
  const X = 1.2, Z = 0.9, E = 2.0, RG = 2.45, t = 0.05, A = base({ id: "mod_garden_shed", name: "Garden shed", footprint: [2.7, 2.1],
    description: "2.4 x 1.8 m painted shiplap garden shed on bearers: apex felt roof with barge boards, ledged-and-braced door (opens), a side window and a potting shelf inside." });
  A.box([-X - 0.05, 0, -Z - 0.05], [X + 0.05, 0.15, Z + 0.05], "timber_grey", { bv: 0.01 });
  A.box([-X, 0.15, -Z], [X, 0.17, Z], "timber_grey", K);
  A.box([-X, 0.15, -Z], [X, E, -Z + t], "shed_paint");
  A.box([-X, 0.15, Z - t], [-0.42, E, Z], "shed_paint"); A.box([0.42, 0.15, Z - t], [X, E, Z], "shed_paint"); A.box([-0.42, 1.95, Z - t], [0.42, E, Z], "shed_paint");
  A.box([-X, 0.15, -Z + t], [-X + t, E, Z - t], "shed_paint");
  A.box([X - t, 0.15, -Z + t], [X, 0.9, Z - t], "shed_paint"); A.box([X - t, 1.6, -Z + t], [X, E, Z - t], "shed_paint");
  A.box([X - t, 0.9, -Z + t], [X, 1.6, -0.4], "shed_paint"); A.box([X - t, 0.9, 0.4], [X, 1.6, Z - t], "shed_paint");
  A.box([X - t / 2 - 0.003, 0.9, -0.4], [X - t / 2 + 0.003, 1.6, 0.4], "glass");
  for (const [a, b] of [[[0.88, -0.44], [0.92, 0.44]], [[1.58, -0.44], [1.62, 0.44]], [[0.88, -0.44], [1.62, -0.4]], [[0.88, 0.4], [1.62, 0.44]], [[0.88, -0.02], [1.62, 0.02]]]) A.box([X - 0.002, a[0], a[1]], [X + 0.02, b[0], b[1]], "white_wood", K);
  for (const sx of [-1, 1]) A.prism(0, sx > 0 ? X - t : -X, sx > 0 ? X : -X + t, [[-Z, E], [Z, E], [0, RG]], "shed_paint");
  for (let y = 0.3; y < E; y += 0.15) {   // shiplap shadow lines
    A.box([-X, y, Z + 0.001], [-0.42, y + 0.012, Z + 0.006], "felt", D); A.box([0.42, y, Z + 0.001], [X, y + 0.012, Z + 0.006], "felt", D);
    A.box([-X, y, -Z - 0.006], [X, y + 0.012, -Z - 0.001], "felt", D);
    A.box([-X - 0.006, y, -Z], [-X - 0.001, y + 0.012, Z], "felt", D);
  }
  const th = Math.atan2(RG - E, Z), Ls = Math.hypot(Z, RG - E) + 0.18;
  for (const s of [-1, 1]) {
    A.obox([0, (E + RG) / 2 + 0.03, s * (Z / 2 + 0.07)], [2 * X + 0.3, 0.035, Ls], [s * deg(th), 0, 0], "felt", { bv: 0.005 });
    for (const sx of [-1, 1]) A.obox([sx * (X + 0.16), (E + RG) / 2 + 0.0, s * (Z / 2 + 0.07)], [0.025, 0.14, Ls], [s * deg(th), 0, 0], "white_wood", K);
    A.box([-X - 0.15, E - 0.12, s * (Z + 0.14) - 0.012], [X + 0.15, E - 0.02, s * (Z + 0.14) + 0.012], "white_wood", K);
  }
  A.box([-X + t, 0.85, -Z + t], [X - t, 0.89, -Z + 0.5], "timber_grey", { k: 0, bv: 0.005 });
  for (const x of [-0.7, 0.2]) A.cyl([x, 0.98, -Z + 0.3], [0.16, 0.18, 0.16], "terracotta", { seg: 10, top: 0.75, k: 0 });
  const L = { parts: [] }, o = { into: L.parts, k: 0 };
  for (let x = -0.4; x < 0.39; x += 0.1) A.box([x + 0.003, 0.17, Z - 0.03], [x + 0.097, 1.93, Z - 0.005], "shed_paint", { ...o, bv: 0.003 });
  for (const y of [0.35, 1.0, 1.7]) A.box([-0.38, y, Z - 0.005], [0.38, y + 0.1, Z + 0.015], "shed_paint", { ...o, bv: 0.004 });
  for (const yc of [0.68, 1.35]) A.obox([0, yc, Z + 0.005], [0.1, Math.hypot(0.62, 0.55), 0.02], [0, 0, 41], "shed_paint", o);
  for (const y of [0.4, 1.75]) A.box([-0.41, y - 0.015, Z + 0.015], [-0.12, y + 0.015, Z + 0.02], "steel_dark", o);
  A.box([0.28, 1.0, Z + 0.015], [0.34, 1.12, Z + 0.04], "steel_dark", o);
  A.box([-0.4, 0.17, Z - 0.03], [0.4, 1.93, Z + 0.015], "shed_paint", { into: L.parts, v: 0 });
  L.hinge = [-0.41, 0, Z]; L.sign = swingSign("x", [0, 0, Z], L.hinge, 1);
  A.interactables.push({ id: "door", kind: "door", label: "shed door", sound: "wood", axis: [0, 1, 0], openAngle: 100, duration: 1.1, defaultOpen: false, leaves: [L], aperture: { min: [-0.42, 0.15, Z - t], max: [0.42, 1.95, Z] } });
  setNav(A, { effect: "carve", cover: [cover([-X - 0.5, 0, 0], 90, "high", ["left", "right"]), cover([0, 0, -Z - 0.5], 0, "high", ["left", "right"])] });
  return A;
}
function greenhouse() {
  const X = 1.25, Z = 1.9, E = 1.6, RG = 2.4, A = base({ id: "mod_greenhouse", name: "Greenhouse", footprint: [2.7, 4.0],
    description: "2.5 x 3.8 m aluminium-framed greenhouse on a low brick dwarf wall: glazed walls and apex roof with glazing bars at 0.63 m, a roof vent, a hinged door at the front gable, and a slatted staging bench with pots and seedlings inside." });
  A.box([-X, 0, -Z], [X, 0.35, -Z + 0.1], "brick", { bv: 0.01 });
  for (const s of [-1, 1]) { A.box([s > 0 ? 0.42 : -X, 0, Z - 0.1], [s > 0 ? X : -0.42, 0.35, Z], "brick", { bv: 0.01 }); A.box([s > 0 ? X - 0.1 : -X, 0, -Z + 0.1], [s > 0 ? X : -X + 0.1, 0.35, Z - 0.1], "brick", { bv: 0.01 }); }
  A.box([-0.42, 0, Z - 0.1], [0.42, 0.04, Z], "slab", { bv: 0.005 });
  A.box([-X + 0.1, -0.02, -Z + 0.1], [X - 0.1, 0.03, Z - 0.1], "gravel", K);
  const nb = 6;
  for (let i = 0; i <= nb; i++) { const z = -Z + (i * 2 * Z) / nb; for (const sx of [-1, 1]) { A.box([sx * X - 0.02, 0.35, z - 0.02], [sx * X + 0.02, E, z + 0.02], "alu", K); rod(A, [sx * X, E, z], [0, RG, z], 0.035, "alu", K); } }
  for (const sx of [-1, 1]) {
    A.box([sx * X - 0.004, 0.36, -Z], [sx * X + 0.004, E, Z], "glass");
    const th = Math.atan2(RG - E, X), Lr = Math.hypot(X, RG - E);
    A.obox([sx * X / 2, (E + RG) / 2, 0], [Lr, 0.008, 2 * Z], [0, 0, -sx * deg(th)], "glass");
    A.box([sx * X - 0.03, E - 0.03, -Z], [sx * X + 0.03, E + 0.02, Z], "alu", K);
  }
  rod(A, [0, RG, -Z], [0, RG, Z], 0.05, "alu", K);
  A.obox([-X / 2 + 0.05, (E + RG) / 2 + 0.06, -0.4], [Math.hypot(X, RG - E) * 0.5, 0.012, 0.6], [0, 0, deg(Math.atan2(RG - E, X)) + 8], "glass", K);
  for (const s of [-1, 1]) {
    const z = s * Z;
    A.prism(2, z - 0.004, z + 0.004, [[-X, E], [X, E], [0, RG]], "glass");
    if (s < 0) A.box([-X, 0.35, z - 0.004], [X, E, z + 0.004], "glass");
    else { A.box([-X, 0.35, z - 0.004], [-0.42, E, z + 0.004], "glass"); A.box([0.42, 0.35, z - 0.004], [X, E, z + 0.004], "glass"); A.box([-0.42, 1.95, z - 0.004], [0.42, E, z + 0.004], "glass"); }
    for (const x of s > 0 ? [-X, -0.42, 0.42, X] : [-X, -0.42, 0.42, X]) A.box([x - 0.02, 0.35, z - 0.02], [x + 0.02, E, z + 0.02], "alu", K);
    A.box([-X, E - 0.02, z - 0.02], [X, E + 0.02, z + 0.02], "alu", K);
  }
  for (let z = -Z + 0.2; z < Z - 0.6; z += 0.11) A.box([-X + 0.12, 0.78, z], [-X + 0.62, 0.8, z + 0.08], "timber", { k: 0, bv: 0.003 });
  for (const z of [-Z + 0.2, Z - 0.65]) for (const x of [-X + 0.15, -X + 0.6]) A.box([x - 0.02, 0, z - 0.02], [x + 0.02, 0.78, z + 0.02], "alu", K);
  const R = rng(10900);
  for (let i = 0; i < 8; i++) { const z = -Z + 0.35 + i * 0.32, x = -X + 0.3 + (i % 2) * 0.15; A.cyl([x, 0.87, z], [0.14, 0.14, 0.14], "terracotta", { seg: 10, top: 0.75, k: 0, r: [180, 0, 0] }); const s = 0.06 + R() * 0.05; A.rbox([x - s, 0.92, z - s], [x + s, 0.96 + s * 1.5, z + s], s * 0.8, R() < 0.5 ? "leaf_light" : "leaf", D); }
  A.box([-X + 0.12, 0.6, -Z + 0.15], [-X + 0.62, 0.8, Z - 0.6], "timber", { v: 0 });
  const L = { parts: [] };
  A.box([-0.4, 0.06, Z + 0.006], [0.4, 1.93, Z + 0.014], "glass", { into: L.parts, k: 0 });
  for (const [a, b] of [[[-0.42, 0.04], [-0.38, 1.95]], [[0.38, 0.04], [0.42, 1.95]], [[-0.42, 0.04], [0.42, 0.12]], [[-0.42, 1.88], [0.42, 1.95]], [[-0.42, 1.0], [0.42, 1.04]]]) A.box([a[0], a[1], Z + 0.003], [b[0], b[1], Z + 0.03], "alu", { into: L.parts, k: 0 });
  A.box([0.3, 1.05, Z + 0.03], [0.34, 1.15, Z + 0.06], "steel_dark", { into: L.parts, k: 0 });
  A.box([-0.42, 0.04, Z], [0.42, 1.95, Z + 0.03], "alu", { into: L.parts, v: 0 });
  L.hinge = [-0.42, 0, Z + 0.015]; L.sign = swingSign("x", [0, 0, Z + 0.015], L.hinge, 1);
  A.interactables.push({ id: "door", kind: "door", label: "greenhouse door", sound: "steel", axis: [0, 1, 0], openAngle: 100, duration: 1.0, defaultOpen: false, leaves: [L], aperture: { min: [-0.42, 0.04, Z - 0.1], max: [0.42, 1.95, Z + 0.03] } });
  setNav(A, { effect: "carve" });
  return A;
}
function picnicTable() {
  const A = base({ id: "mod_picnic_table", name: "Picnic table", footprint: [1.9, 1.6], description: "1.8 m timber picnic table with integral bench seats on bolted A-frame legs." });
  for (let i = 0; i < 5; i++) { const z = -0.37 + i * 0.15; A.box([-0.9, 0.72, z + 0.004], [0.9, 0.765, z + 0.146], "timber", { k: 0, bv: 0.006 }); }
  for (const s of [-1, 1]) for (let i = 0; i < 2; i++) { const z = s * 0.6 - 0.13 + i * 0.13; A.box([-0.9, 0.42, z + 0.004], [0.9, 0.46, z + 0.126], "timber", { k: 0, bv: 0.006 }); }
  for (const x of [-0.65, 0.65]) {
    for (const s of [-1, 1]) A.obox([x, 0.37, s * 0.36], [0.05, 0.84, 0.09], [s * 32, 0, 0], "timber", { k: 0, bv: 0.006 });
    A.box([x - 0.025, 0.33, -0.78], [x + 0.025, 0.42, 0.78], "timber", { k: 0, bv: 0.006 });
    A.box([x - 0.025, 0.64, -0.38], [x + 0.025, 0.72, 0.38], "timber", { k: 0, bv: 0.006 });
    for (const z of [-0.6, 0.6, -0.2, 0.2]) A.cyl([x + 0.03, z === -0.2 || z === 0.2 ? 0.68 : 0.375, z], [0.03, 0.01, 0.03], "galv", { ...D, r: [0, 0, 90] });
  }
  A.box([-0.9, 0, -0.37], [0.9, 0.765, 0.37], "timber", { v: 0 });
  for (const s of [-1, 1]) A.box([-0.9, 0, s > 0 ? 0.47 : -0.73], [0.9, 0.46, s > 0 ? 0.73 : -0.47], "timber", { v: 0 });
  setNav(A, { effect: "carve", cover: [cover([0, 0, -1.2], 0, "low"), cover([0, 0, 1.2], 180, "low")] });
  return A;
}

// ============================================================================ the set
export function modernOutdoorAssets() {
  const T2 = "mod_road_t_junction", X4 = "mod_road_crossroads";
  const tArms = [[1, 0], [-1, 0], [0, 1]]; tArms.priority = [[1, 0], [-1, 0]];
  const out = [
    // line pieces (hidden; drawn by the mod_* line types)
    roadBay2("mod_road_2lane_bay", 0), roadBay2("mod_road_2lane_bay_b", 1), roadBay2("mod_road_2lane_bay_c", 2),
    roadBay4("mod_road_4lane_bay", 0), roadBay4("mod_road_4lane_bay_b", 1), roadBay4("mod_road_4lane_bay_c", 2),
    sidewalkBay("mod_sidewalk_bay", 0), sidewalkBay("mod_sidewalk_bay_b", 1), sidewalkBay("mod_sidewalk_bay_c", 2),
    fenceWhiteBay(), picketBay(),
    hedgeBay("mod_hedge_box_bay", 1.2, 0), hedgeBay("mod_hedge_box_bay_b", 1.2, 1),
    hedgeBay("mod_hedge_box_tall_bay", 1.8, 0), hedgeBay("mod_hedge_box_tall_bay_b", 1.8, 1),
    chainlinkBay(),
    // gates
    gateWhite(), gateChainlink(),
    // roads
    crosswalk(),
    junction(T2, "Road T-junction", "T-junction of 2-lane roads, 16 m square: kerbed 4 m corner radii with channels, edge and centre lines, a stop line and give-way marking on the side road, a manhole in the box. Arms join road lines at the square's edges.", tArms),
    junction(X4, "Road crossroads", "Four-way crossroads of 2-lane roads, 16 m square: kerbed 4 m corner radii with channels, edge and centre lines, a stop line on every approach. Arms join road lines at the square's edges.", [[1, 0], [-1, 0], [0, 1], [0, -1]]),
    roundabout(), roadEnd(),
    // parking
    parkingLot("mod_parking_small", "Parking lot, small (10 stalls)", 5, 1, false, "Small surface car park for 10 cars: one aisle between two rows of 2.5 x 5 m stalls (one an accessible bay with a hatched access aisle and sign), wheel stops, kerbed tree islands at the row ends, grass verges with light columns, aisle arrows and a gully."),
    parkingLot("mod_parking_medium", "Parking lot, medium (24 stalls)", 6, 2, false, "Medium surface car park for 24 cars: two aisles and four rows of stalls (one accessible bay with hatched aisle and sign), wheel stops, kerbed tree islands, a planted central median with trees and twin-arm light columns, verges with lights, aisle arrows and gullies."),
    parkingLot("mod_parking_large", "Parking lot, large (48 stalls)", 12, 2, true, "Large surface car park for 48 cars: two aisles and four rows of twelve stalls broken by planted islands, one accessible bay with hatched aisle and sign, wheel stops, a tree-lined central median with twin-arm light columns, verges with lights, aisle arrows and gullies."),
    parkingSurface(),
    // playground
    climbingFrame(), swings("mod_play_swings_2", 2), swings("mod_play_swings_4", 4), seesaw(),
    springRider("mod_play_spring_rider", false), springRider("mod_play_spring_rider_twin", true),
    playRoundabout(), monkeyBars(), sandpit(),
    safetyTiles("mod_play_safety_tiles_red", "rubber_red", "rubber_red2", "red"), safetyTiles("mod_play_safety_tiles_green", "rubber_green", "rubber_green2", "green"),
    playFence(false), playFence(true),
    // sport and pool
    basketball(true), basketball(false), tennis(), pool(), sunLounger(),
    // fountains
    fountainTiered(), fountainRound(), fountainWall(),
    // garden
    planterConcrete(), planterCorten(), raisedBed(), flowerBed(),
    ornamentalTree("mod_tree_ornamental", "Ornamental tree (round crown)", { h: 5.5, cr: 1.9 }, "Ornamental street / garden tree 5.5 m tall with a clear stem and a dense round layered crown."),
    ornamentalTree("mod_tree_columnar", "Ornamental tree (columnar)", { h: 6.5, cr: 1.1, kind: "column" }, "Columnar (fastigiate) ornamental tree 6.5 m tall with a narrow upright crown, for avenues and tight spaces."),
    ornamentalTree("mod_tree_blossom", "Blossom tree", { h: 4.5, cr: 2.1, kind: "spread", leaf: "blossom" }, "Spreading ornamental cherry 4.5 m tall in full pink blossom, a low wide layered crown on a short trunk."),
    topiary("mod_topiary_ball", "ball"), topiary("mod_topiary_cone", "cone"), topiary("mod_topiary_balls", "group"),
    pergola(), gazebo(), benchAsset(true), benchAsset(false), birdBath(), steppingStones(), decking(),
    lampPost(false), lampPost(true), shed(), greenhouse(), picnicTable(),
  ];
  return out.map(finish);
}
