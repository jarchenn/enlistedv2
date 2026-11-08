// D1 detail kit (defences, guns, ruins): round parts on the B1 geometry kit (docs/asset-format.md#b1-geometry-kit).
// Every helper is cosmetic (k: 0) unless the caller passes k, so colliders, bays and nav never change. Parts are
// appended after the existing ones, so auto-panel ids (part indices) stay put.
const DEG = 180 / Math.PI;
const MATS = {
  rebar: [[0.24, 0.12, 0.06], 0.75, "metal", { metallic: 0.4 }],
  rust: [[0.2, 0.095, 0.045], 0.8, "metal", { metallic: 0.2 }],
  steel_dark: [[0.09, 0.09, 0.085], 0.6, "metal", { metallic: 0.5 }],
  spoil: [[0.2, 0.16, 0.11], 0.97, "plain"],
  hessian: [[0.42, 0.36, 0.24], 0.95, "fabric"],
  hessian_dk: [[0.36, 0.3, 0.2], 0.95, "fabric"],
  charred: [[0.05, 0.042, 0.035], 0.95, "wood", { grain: -1 }],
};
// add a D1 material once (stdMaterials or the catalogue may already have it)
export function dmat(A, name) {
  if (!A.materials.some((m) => m.name === name)) { const [a, r, p, e] = MATS[name]; A.mat(name, a, r, p, e || {}); }
  return name;
}
export function seeded(str) {   // xorshift seeded by a string (stable across runs)
  let h = 2166136261;
  for (let i = 0; i < str.length; i++) { h ^= str.charCodeAt(i); h = Math.imul(h, 16777619); }
  return () => { h ^= h << 13; h ^= h >>> 17; h ^= h << 5; return ((h >>> 0) % 100000) / 100000; };
}
// euler [rx, ry, 0] (src/assets.cpp: quat(radians(r)) = Rz Ry Rx) that turns local +y onto direction d
export function aim(d) { const L = Math.hypot(d[0], d[1], d[2]); return [Math.acos(Math.max(-1, Math.min(1, d[1] / L))) * DEG, Math.atan2(d[0], d[2]) * DEG, 0]; }
// cylinder from a to b (o.top < 1: a frustum narrowing toward b), diameter dia
export function rod(A, a, b, dia, m, o = {}) {
  const d = [b[0] - a[0], b[1] - a[1], b[2] - a[2]], L = Math.hypot(d[0], d[1], d[2]);
  return A.cyl([(a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2], [dia, L, dia], m, { k: 0, ...o, r: aim(d) });
}
const add = (p, d, s = 1) => [p[0] + d[0] * s, p[1] + d[1] * s, p[2] + d[2] * s];
const norm = (d) => { const L = Math.hypot(d[0], d[1], d[2]); return [d[0] / L, d[1] / L, d[2] / L]; };
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
// two unit vectors spanning the plane normal to n
function basis(n) { const u = norm(cross(n, Math.abs(n[1]) < 0.9 ? [0, 1, 0] : [1, 0, 0])); return [u, cross(n, u)]; }

// rebar stubs breaking out of a concrete edge from p0 to p1, pointing along dir: n bars, each a straight stub with a
// bent, rusted tip; jitter in length and lean
export function rebar(A, p0, p1, dir, { n = 6, len = 0.35, seed = "rebar", dia = 0.018 } = {}) {
  dmat(A, "rebar"); dmat(A, "rust");
  const R = seeded(seed + p0.join(",")), D = norm(dir), [u, v] = basis(D);
  for (let i = 0; i < n; i++) {
    const t = (i + 0.2 + 0.6 * R()) / n, a = [p0[0] + (p1[0] - p0[0]) * t, p0[1] + (p1[1] - p0[1]) * t, p0[2] + (p1[2] - p0[2]) * t];
    const l = len * (0.5 + 0.8 * R()), lean = add(add(D, u, (R() - 0.5) * 0.5), v, (R() - 0.5) * 0.5);
    const b = add(a, norm(lean), l);
    rod(A, add(a, norm(lean), -0.06), b, dia, "rebar", { seg: 6 });
    const bend = add(add(norm(lean), u, (R() - 0.5) * 2.2), v, (R() - 0.5) * 2.2);
    rod(A, b, add(b, norm(bend), l * (0.25 + 0.4 * R())), dia * 0.95, R() < 0.5 ? "rust" : "rebar", { seg: 6 });
  }
}
// hinge knuckles down a door edge: vertical barrels at x / z, from y0 to y1 (n knuckles), dia
export function hinges(A, x, z, y0, y1, { n = 3, dia = 0.06, h = 0.14, m = "steel_plate" } = {}) {
  for (let i = 0; i < n; i++) {
    const y = y0 + ((y1 - y0) * (i + 0.5)) / n;
    A.cyl([x, y, z], [dia, h, dia], m, { k: 0, seg: 10 });
    A.cyl([x, y + h / 2 + 0.012, z], [dia * 0.55, 0.024, dia * 0.55], m, { k: 0, seg: 8 });   // pin head
  }
}
// hinge knuckles on the hinge axis of a parts.mjs door() leaf (same options as the door call): they sit on the axis the
// leaf turns about, so they read right open or shut
export function doorKnuckles(A, { axis, outer, inner, c, w, floor, h, hingeSide, double = false, steel = false }) {
  const out = Math.sign(outer - inner), fw = steel ? 0.06 : 0.07, la0 = c - w / 2 + fw, la1 = c + w / 2 - fw;
  const ly0 = floor + 0.006, ly1 = floor + h - fw - 0.004, d = inner + out * 0.035;
  const axes = double ? [la0, la1] : [hingeSide < 0 ? la0 : la1];
  for (const a of axes) for (const y of [ly0 + 0.2, ly1 - 0.2]) {
    const [x, z] = axis === "x" ? [a, d] : [d, a];
    A.cyl([x, y, z], [0.05, 0.13, 0.05], "steel_bare", { k: 0, seg: 10 });
    A.cyl([x, y + 0.075, z], [0.028, 0.02, 0.028], "steel_bare", { k: 0, seg: 8 });
  }
}
// roof ventilation pipe with a mushroom cap (y0: roof top)
export function ventPipe(A, x, y0, z, { h = 0.45, dia = 0.16, m = "steel_dark" } = {}) {
  dmat(A, m);
  A.cyl([x, y0 + h / 2 - 0.05, z], [dia, h + 0.1, dia], m, { k: 0 });
  A.cyl([x, y0 + h + 0.01, z], [dia * 1.9, 0.025, dia * 1.9], m, { k: 0 });
  A.cone([x, y0 + h + 0.065, z], [dia * 1.9, 0.09, dia * 1.9], m, { k: 0, top: 0.25 });
  A.cyl([x, y0 + 0.02, z], [dia * 1.5, 0.06, dia * 1.5], m, { k: 0 });   // flange
}
// observation periscope through the roof: tube, collar and a bevelled head with a dark window facing yaw
export function periscope(A, x, y0, z, yawDeg = 0, { h = 0.6, m = "steel_dark" } = {}) {
  dmat(A, m);
  A.cyl([x, y0 + h / 2 - 0.05, z], [0.09, h + 0.1, 0.09], m, { k: 0 });
  A.cyl([x, y0 + 0.03, z], [0.2, 0.07, 0.2], m, { k: 0 });
  A.obox([x, y0 + h + 0.04, z], [0.14, 0.12, 0.16], [0, yawDeg, 0], m, { k: 0, bv: 0.015 });
  const f = [Math.sin(yawDeg / DEG), 0, Math.cos(yawDeg / DEG)];
  A.obox([x + f[0] * 0.081, y0 + h + 0.05, z + f[2] * 0.081], [0.1, 0.035, 0.006], [0, yawDeg, 0], "glass", { k: 0 });
}
// wheel turning about axis (unit [1,0,0] or [0,0,1]) at centre c: tyre, rim, hub dome and lug nuts on the out side
export function wheel(A, c, ax, { dia = 0.86, w = 0.16, out = 1, tyre = "rubber", rim = "gun_paint", hub = "gun_dark", lugs = 6, o = {} } = {}) {
  const r = aim(ax), O = add([0, 0, 0], ax, out);
  A.cyl(c, [dia, w * 0.8, dia], tyre, { k: 0, ...o, r, seg: 36 });
  A.cyl(c, [dia - 0.05, w, dia - 0.05], tyre, { k: 0, ...o, r, seg: 36 });   // rounded shoulders
  A.cyl(c, [dia * 0.74, w + 0.012, dia * 0.74], rim, { k: 0, ...o, r, seg: 28 });
  const face = add(c, O, w / 2 + 0.006);
  rod(A, add(face, O, -0.01), add(face, O, 0.05), dia * 0.36, rim, { ...o, top: 0.6, seg: 20 });   // dished centre
  rod(A, add(face, O, 0.04), add(face, O, 0.075), dia * 0.14, hub, { ...o, seg: 12 });
  const [u, v] = basis(ax);
  for (let i = 0; i < lugs; i++) {
    const a = (i / lugs) * Math.PI * 2, p = add(add(face, u, Math.cos(a) * dia * 0.13), v, Math.sin(a) * dia * 0.13);
    rod(A, add(p, O, 0.035), add(p, O, 0.055), 0.024, hub, { ...o, seg: 6 });
  }
}
// handwheel: rim of n rods, three spokes, a hub and a crank handle; axis ax (unit), diameter dia
export function handwheel(A, c, ax, dia, m, o = {}) {
  const [u, v] = basis(ax), r = dia / 2, n = 10, pt = (a, k = r) => add(add(c, u, Math.cos(a) * k), v, Math.sin(a) * k);
  for (let i = 0; i < n; i++) rod(A, pt((i / n) * 6.2832), pt(((i + 1) / n) * 6.2832), 0.018, m, { ...o, seg: 6 });
  for (let i = 0; i < 3; i++) rod(A, c, pt((i / 3) * 6.2832 + 0.4, r * 0.95), 0.014, m, { ...o, seg: 6 });
  rod(A, add(c, ax, -0.03), add(c, ax, 0.03), 0.045, m, { ...o, seg: 10 });
  const h = pt(0.4, r * 0.95);
  rod(A, h, add(h, ax, 0.08), 0.022, "rubber", { ...o, seg: 8 });
}
// rivet heads on a plane: points pts, the face normal nrm (unit), proud by 8 mm
export function rivets(A, pts, nrm, m, o = {}) { for (const p of pts) rod(A, p, add(p, nrm, 0.008), 0.022, m, { ...o, seg: 6 }); }

// sandbag: a soft pillow lying along yaw (deg), at centre c: rounded body plus tied ends
export function sandbag(A, c, yawDeg, { L = 0.55, W = 0.3, H = 0.13, m = "hessian", k = 0 } = {}) {
  dmat(A, "hessian"); dmat(A, "hessian_dk");
  const f = [Math.sin(yawDeg / DEG), 0, Math.cos(yawDeg / DEG)];
  const R4 = (x) => Math.round(x * 10000) / 10000;
  A._put({ t: "r", c: c.map(R4), s: [W, H, L * 0.84].map(R4), rad: R4(H * 0.45), m }, { k, r: [0, yawDeg, 0] });   // long axis = local z
  for (const s of [-1, 1]) A.cone(add(c, f, s * L * 0.44), [H * 0.75, L * 0.12, H * 0.75], "hessian_dk", { k: 0, top: 0.45, r: aim([f[0] * s, 0, f[2] * s]) });
}
// rotate v by euler degrees r as the loader does (quat(radians(r)) = Rz Ry Rx)
function rotv(r, v) {
  const [a, b, c] = (r || [0, 0, 0]).map((x) => x / DEG);
  let [x, y, z] = v;
  [y, z] = [y * Math.cos(a) - z * Math.sin(a), y * Math.sin(a) + z * Math.cos(a)];
  [x, z] = [x * Math.cos(b) + z * Math.sin(b), -x * Math.sin(b) + z * Math.cos(b)];
  [x, y] = [x * Math.cos(c) - y * Math.sin(c), x * Math.sin(c) + y * Math.cos(c)];
  return [x, y, z];
}
const CHUNKY = new Set(["stone", "stone_dressed", "brick", "brick_rubble", "rubble_core", "concrete", "concrete_core", "stone_plinth"]);
const TIMBER = new Set(["oak_x", "oak_y", "oak_z", "charred", "floor_x", "log_x", "log_z", "plank_x", "plank_z", "plank_y"]);
// Ruin / rubble pass with its own RNG (the generator's random sequence and every collider stay as they were):
// loose non-colliding stone, brick and timber boxes get a mesh chamfer (chunky, not razor-edged), and beams, joists and
// rafters (boxes >= 0.5 m long, <= 0.3 m thick) get splintered ends. Appends only.
export function debrisDetail(A, { maxSplinter = 40 } = {}) {
  const R = seeded(A.meta.id + "/d1"), ends = [];
  for (const p of A.parts) {
    if (p.t !== "b" || p.v === 0) continue;
    const mn = Math.min(...p.s);
    if (p.k === 0 && !p.bv && mn >= 0.06 && (CHUNKY.has(p.m) || TIMBER.has(p.m))) p.bv = Math.round(Math.min(0.03, mn * 0.18) * 10000) / 10000;
    const L = p.s.indexOf(Math.max(...p.s)), th = p.s.filter((_, i) => i !== L);
    if (!TIMBER.has(p.m) || p.s[L] < 0.5 || Math.max(...th) > 0.3 || Math.min(...th) < 0.04) continue;
    const e = [0, 0, 0]; e[L] = 1;
    const ax = rotv(p.r, e);
    for (const s of R() < 0.3 ? [-1, 1] : [R() < 0.5 ? -1 : 1]) ends.push([add(p.c, ax, (s * p.s[L]) / 2), ax.map((x) => x * s), th[0], th[1], p.m]);
  }
  for (let i = ends.length - 1; i > 0; i--) { const j = Math.floor(R() * (i + 1)); [ends[i], ends[j]] = [ends[j], ends[i]]; }
  ends.slice(0, maxSplinter).forEach(([e, d, w, h, m], i) => splinters(A, e, d, w, h, m, A.meta.id + i));
}
// splintered beam end: a few tapering slivers continuing the beam from end point e along dir, beam size w x h
export function splinters(A, e, dir, w, h, m, seed) {
  const R = seeded(seed), D = norm(dir), [u, v] = basis(D);
  for (let i = 0; i < 3; i++) {   // short, thick slivers: a torn end, not spikes
    const p = add(add(e, u, (R() - 0.5) * w * 0.5), v, (R() - 0.5) * h * 0.5);
    const tip = add(add(add(p, D, 0.04 + 0.1 * R()), u, (R() - 0.5) * 0.03), v, (R() - 0.5) * 0.03);
    rod(A, add(p, D, -0.04), tip, Math.min(w, h) * (0.4 + 0.25 * R()), m, { top: 0.3, seg: 5 });
  }
}
