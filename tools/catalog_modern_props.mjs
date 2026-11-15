// MOD1: modern-day outdoor / urban props (ids mod_*): concrete barriers, HESCO and sandbag fortifications, fences and
// gates, road obstacles, generic vehicles (intact, wrecked, burned), shipping containers and industrial cover, small
// cover, street furniture, construction kit, terrain obstacles, military equipment and destruction debris.
// Original generic shapes only: no logos, brands or readable text (signs and plates are blank coloured panels).
// Written to content/assets/<id>.json by tools/build_mpu_assets.mjs (integrator). Units metres, +y up, front = +z.
import { Asset } from "./kit.mjs";
import { setNav, cover, room, fire, rng } from "./catalog_atlantic.mjs";

const DEG = 180 / Math.PI;
const D = { k: 0, lod: 1 };                      // fine detail: no collider, may drop at distance
const V = { k: 0 };                              // visible mass whose collider is a separate simple box
const H = { v: 0 };                              // collider only
const dv = (bv) => ({ k: 0, lod: 1, bv });       // bevelled detail
const vb = (bv) => ({ k: 0, bv });               // bevelled visible mass, no collider

// ------------------------------------------------------------------------------------------------- materials
// [albedo, roughness, pattern, extra]; fin() registers only the ones an asset uses
const T = (tex, alb, r, pat, ex = {}) => [alb, r, pat, tex ? { texture: tex, ...ex } : { ...ex }];   // MOD1 review: tex null = clean paint (the paint_metal_* sets carry rust)
const MT = {
  conc: T("concrete_aged", [0.66, 0.64, 0.6], 0.9, "concrete"),
  conc_l: T("concrete_light", [0.74, 0.73, 0.7], 0.88, "concrete"),
  conc_r: T("concrete_rough", [0.58, 0.57, 0.54], 0.95, "concrete"),
  conc_st: T("concrete_stained", [0.6, 0.58, 0.55], 0.92, "concrete"),
  conc_ag: T("concrete_aggregate", [0.62, 0.6, 0.56], 0.95, "concrete"),
  conc_fw: T("concrete_formwork", [0.64, 0.63, 0.6], 0.9, "concrete"),
  conc_dk: T("concrete_rough", [0.36, 0.35, 0.33], 0.97, "concrete"),       // fractured faces, chips, joints
  asphalt: T("asphalt_worn", [0.13, 0.13, 0.13], 0.95, "concrete"),
  asphalt_dk: T("asphalt_dark", [0.08, 0.08, 0.08], 0.97, "concrete"),
  soot: T("ground_burnt", [0.07, 0.065, 0.06], 1, "plain"),
  burnt: T("rust_heavy", [0.3, 0.2, 0.15], 0.95, "metal", { metallic: 0.15 }),
  burnt_dk: T("rust_heavy", [0.12, 0.09, 0.08], 0.97, "metal", { metallic: 0.1 }),
  rust: T("steel_rust_light", [0.55, 0.42, 0.33], 0.8, "metal", { metallic: 0.2 }),
  rust_h: T("rust_heavy", [0.42, 0.26, 0.17], 0.9, "metal", { metallic: 0.1 }),
  steel: T("steel_scratched", [0.58, 0.59, 0.6], 0.45, "metal", { metallic: 0.6 }),
  steel_dk: T("steel_scratched", [0.2, 0.2, 0.21], 0.5, "metal", { metallic: 0.5 }),
  iron: T("iron_worn", [0.3, 0.29, 0.28], 0.65, "metal", { metallic: 0.35 }),
  galv: T("steel_scratched", [0.62, 0.64, 0.65], 0.5, "metal", { metallic: 0.55 }),
  galv_c: T("corrugated_galv", [0.66, 0.67, 0.68], 0.5, "metal", { metallic: 0.5 }),
  wire: [[0.5, 0.51, 0.52], 0.5, "metal", { metallic: 0.6 }],
  chrome: [[0.78, 0.79, 0.8], 0.15, "metal", { metallic: 0.9 }],
  rim: T("steel_scratched", [0.66, 0.67, 0.69], 0.3, "metal", { metallic: 0.75 }),
  rim_dk: [[0.07, 0.07, 0.075], 0.5, "metal", { metallic: 0.4 }],
  rubber: [[0.035, 0.035, 0.037], 0.92, "plain"],
  rubber_side: [[0.055, 0.055, 0.058], 0.88, "plain"],
  seam: [[0.02, 0.02, 0.02], 0.9, "plain"],
  p_grey: T(null, [0.52, 0.53, 0.54], 0.6, "metal"),
  p_dgrey: T(null, [0.25, 0.26, 0.27], 0.6, "metal"),
  p_green: T(null, [0.2, 0.3, 0.2], 0.65, "metal"),
  p_white: T(null, [0.86, 0.86, 0.84], 0.6, "metal"),
  p_tan: T(null, [0.64, 0.54, 0.38], 0.75, "metal"),
  p_od: T(null, [0.27, 0.29, 0.18], 0.75, "metal"),
  p_yellow: T(null, [0.88, 0.66, 0.08], 0.55, "metal"),
  p_red: T(null, [0.62, 0.08, 0.06], 0.55, "metal"),
  p_orange: T(null, [0.85, 0.36, 0.06], 0.55, "metal"),
  p_blue: T(null, [0.1, 0.22, 0.45], 0.55, "metal"),
  p_black: T(null, [0.06, 0.06, 0.065], 0.55, "metal"),
  c_red: T("corrugated_grey", [0.5, 0.14, 0.09], 0.65, "metal", { metallic: 0.2 }),
  c_blue: T("corrugated_grey", [0.12, 0.24, 0.42], 0.65, "metal", { metallic: 0.2 }),
  c_green: T("corrugated_grey", [0.16, 0.32, 0.22], 0.65, "metal", { metallic: 0.2 }),
  c_tan: T("corrugated_grey", [0.66, 0.55, 0.38], 0.7, "metal", { metallic: 0.2 }),
  c_grey: T("corrugated_galv", [0.46, 0.48, 0.5], 0.65, "metal", { metallic: 0.2 }),
  c_rust: T("rust_heavy", [0.48, 0.25, 0.15], 0.85, "metal", { metallic: 0.15 }),
  car_silver: T(null, [0.62, 0.63, 0.65], 0.3, "metal", { metallic: 0.55 }),
  car_red: T(null, [0.55, 0.05, 0.05], 0.3, "metal", { metallic: 0.35 }),
  car_blue: T(null, [0.08, 0.16, 0.36], 0.3, "metal", { metallic: 0.45 }),
  car_white: T(null, [0.88, 0.88, 0.87], 0.3, "metal", { metallic: 0.2 }),
  car_black: T(null, [0.05, 0.05, 0.055], 0.25, "metal", { metallic: 0.4 }),
  car_green: T(null, [0.12, 0.25, 0.18], 0.3, "metal", { metallic: 0.4 }),
  car_dirty: T("paint_metal_worn_white", [0.5, 0.47, 0.42], 0.7, "metal", { metallic: 0.15 }),
  glass: [[0.2, 0.25, 0.28], 0.06, "glass"],
  glass_t: [[0.06, 0.08, 0.09], 0.06, "glass"],
  glass_crack: [[0.55, 0.6, 0.62], 0.4, "glass"],
  lamp_w: [[0.84, 0.86, 0.87], 0.12, "glass"],
  lamp_r: [[0.62, 0.03, 0.03], 0.15, "plain"],
  lamp_a: [[0.92, 0.46, 0.04], 0.15, "plain"],
  lamp_g: [[0.05, 0.45, 0.2], 0.15, "plain"],
  lamp_off: [[0.12, 0.1, 0.08], 0.2, "plain"],
  refl_w: [[0.93, 0.93, 0.9], 0.35, "plain"],
  refl_r: [[0.75, 0.06, 0.05], 0.35, "plain"],
  plate: [[0.88, 0.88, 0.84], 0.5, "plain"],
  trim: [[0.045, 0.045, 0.05], 0.6, "plain"],
  dash: T("leather_brown", [0.09, 0.09, 0.095], 0.75, "fabric"),
  seat: T("cloth_grey", [0.24, 0.24, 0.26], 0.95, "fabric"),
  seat_tan: T("cloth_grey", [0.52, 0.46, 0.36], 0.95, "fabric"),
  pl_orange: [[0.92, 0.34, 0.04], 0.5, "plain"],
  pl_white: [[0.88, 0.88, 0.86], 0.5, "plain"],
  pl_red: [[0.72, 0.07, 0.05], 0.5, "plain"],
  pl_yellow: [[0.9, 0.72, 0.06], 0.5, "plain"],
  pl_blue: [[0.06, 0.25, 0.6], 0.45, "plain"],
  pl_black: [[0.05, 0.05, 0.055], 0.6, "plain"],
  pl_grey: [[0.42, 0.43, 0.44], 0.6, "plain"],
  pl_green: [[0.1, 0.32, 0.18], 0.55, "plain"],
  pl_olive: [[0.25, 0.27, 0.17], 0.7, "plain"],
  pl_tan: [[0.6, 0.52, 0.38], 0.7, "plain"],
  hdpe: [[0.86, 0.85, 0.8], 0.55, "plain"],
  wood: T("planks_038", [0.62, 0.52, 0.4], 0.85, "wood"),
  wood_raw: T("planks_raw", [0.76, 0.64, 0.48], 0.88, "wood"),
  wood_dk: T("wood_dark", [0.4, 0.32, 0.24], 0.85, "wood"),
  wood_oak: T("wood_oak", [0.55, 0.42, 0.3], 0.8, "wood"),
  ply: T("planks_raw", [0.7, 0.58, 0.42], 0.9, "wood"),
  bark: T("bark_fir", [0.42, 0.36, 0.3], 0.95, "wood"),
  heartwood: T("wood_oak", [0.7, 0.56, 0.38], 0.85, "wood"),
  leaves: T("leaf_litter", [0.24, 0.36, 0.14], 0.95, "fabric"),
  soil: T("soil_brown", [0.4, 0.32, 0.24], 1, "plain"),
  dirt: T("dirt_compacted", [0.5, 0.43, 0.34], 1, "plain"),
  gravel: T("gravel_fine", [0.55, 0.53, 0.5], 1, "stone"),
  sand: T("sand_dirty", [0.62, 0.54, 0.42], 1, "plain"),
  hesco_fab: T("canvas_rough", [0.66, 0.6, 0.47], 0.95, "fabric"),
  sandbag: T("canvas_rough", [0.58, 0.5, 0.36], 0.95, "fabric"),
  sandbag_dk: T("canvas_rough", [0.4, 0.34, 0.24], 0.95, "fabric"),
  sandbag_od: T("canvas_olive", [0.34, 0.34, 0.22], 0.95, "fabric"),
  camo: T("canvas_olive", [0.3, 0.32, 0.2], 0.95, "fabric"),
  camo_b: T("canvas_olive", [0.38, 0.33, 0.22], 0.95, "fabric"),
  canvas: T("canvas_olive", [0.36, 0.37, 0.25], 0.92, "fabric"),
  rock: T("rock_grey", [0.55, 0.54, 0.5], 0.95, "stone"),
  rock_m: T("rock_mossy", [0.5, 0.5, 0.44], 0.95, "stone"),
  rock_d: T("rock_cliff", [0.45, 0.43, 0.4], 0.95, "stone"),
  cmu: T("concrete_aggregate", [0.66, 0.65, 0.62], 0.95, "concrete"),
  mortar: T("concrete_light", [0.6, 0.58, 0.55], 0.95, "concrete"),
  rebar: T("steel_rust_light", [0.42, 0.27, 0.18], 0.8, "metal", { metallic: 0.3 }),
  rubble: T("rubble_grey", [0.6, 0.58, 0.55], 1, "stone"),
  debris: T("debris_brick", [0.55, 0.45, 0.38], 1, "stone"),
  plaster: T("plaster_painted", [0.82, 0.8, 0.76], 0.9, "plaster"),
  brick: T("brick_factory", [0.6, 0.4, 0.32], 0.9, "brick"),
  cable: [[0.04, 0.04, 0.045], 0.7, "plain"],
  cable_or: [[0.8, 0.35, 0.06], 0.6, "plain"],
  fab_blk: T("cloth_grey", [0.08, 0.08, 0.09], 0.95, "fabric"),
  poly_blue: T("cloth_grey", [0.1, 0.25, 0.55], 0.7, "fabric"),
  screen_off: [[0.03, 0.04, 0.05], 0.15, "glass"],
  ad_panel: [[0.75, 0.78, 0.8], 0.3, "plain", { emissive: [0.18, 0.19, 0.2] }],
};

// ------------------------------------------------------------------------------------------------- framework
function mk(id, name, cat, description, footprint, o = {}) {
  return new Asset({ id, version: 1, name, category: `Modern: ${cat}`, description, footprint, floors: 0, enterable: false,
    plinthDepth: 0, maxGrade: 0.5, noAutoPanels: true, ...o });
}
function fin(A, nav = {}) {
  const used = new Set();
  const walk = (ps) => { for (const p of ps) used.add(p.m); };
  walk(A.parts); for (const f of A.furniture) walk(f.parts);
  for (const m of used) {
    if (!MT[m]) throw new Error(`${A.meta.id}: unknown material ${m}`);
    const [alb, r, pat, ex] = MT[m];
    A.mat(m, alb, r, pat, ex || {});
  }
  const { cuts, ...n } = nav;
  setNav(A, { effect: "carve", rooms: [], doors: [], entrances: [], corners: [], firingPoints: [], cover: [], areas: [], ...n }, cuts || null);
  return A;
}

// cover points round a box of half sizes hx, hz (local axes): points 0.55 m off each long face facing the face, spaced
// ~step along it; end points too when the short side is wide enough. h: obstacle height
function boxCover(hx, hz, h, { step = 1.6, ends = true, off = 0.55, cx = 0, cz = 0 } = {}) {
  const ht = h >= 1.35 ? "high" : "low", out = [];
  const pk = (edge) => (ht === "high" ? edge : ["over", ...edge]);
  const along = (half) => { const n = Math.max(1, Math.round((2 * half) / step)); return Array.from({ length: n }, (_, i) => -half + ((i + 0.5) * 2 * half) / n); };
  for (const x of along(hx)) {
    const e = Math.abs(x) > hx - step * 0.6 ? [x > 0 ? "right" : "left"] : [];
    out.push(cover([cx + x, 0, cz - hz - off], 0, ht, pk(ht === "high" ? (e.length ? e : ["left", "right"]) : e)));
    out.push(cover([cx + x, 0, cz + hz + off], 180, ht, pk(ht === "high" ? (e.length ? e : ["left", "right"]) : e)));
  }
  if (ends && hz > 0.35) {
    out.push(cover([cx - hx - off, 0, cz], 90, ht, pk(["left", "right"])));
    out.push(cover([cx + hx + off, 0, cz], 270, ht, pk(["left", "right"])));
  }
  return out;
}

// ------------------------------------------------------------------------------------------------- geometry helpers
const add = (p, d, s = 1) => [p[0] + d[0] * s, p[1] + d[1] * s, p[2] + d[2] * s];
const lerp3 = (a, b, t) => [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];
// euler (Rz Ry Rx, degrees) turning local +y onto d
const aimY = (d) => { const L = Math.hypot(d[0], d[1], d[2]); return [Math.acos(Math.max(-1, Math.min(1, d[1] / L))) * DEG, Math.atan2(d[0], d[2]) * DEG, 0]; };
// round rod (cylinder) from a to b
function rod(A, a, b, dia, m, o = {}) {
  const d = [b[0] - a[0], b[1] - a[1], b[2] - a[2]], L = Math.hypot(...d);
  if (L < 1e-4) return null;
  return A.cyl(lerp3(a, b, 0.5), [dia, L, dia], m, { k: 0, lod: 1, seg: 8, ...o, r: aimY(d) });
}
// rectangular bar from a to b: w across (local x), t thick (local y), long axis local z
function bar(A, a, b, w, t, m, o = {}) {
  const d = [b[0] - a[0], b[1] - a[1], b[2] - a[2]], L = Math.hypot(...d);
  if (L < 1e-4) return null;
  const yaw = Math.atan2(d[0], d[2]) * DEG, pitch = -Math.asin(Math.max(-1, Math.min(1, d[1] / L))) * DEG;
  return A.obox(lerp3(a, b, 0.5), [w, t, L], [pitch, yaw, 0], m, o);
}
// convex polygon in (z, y) extruded along x (fan of prisms), and in (x, y) extruded along z
function extrudeX(A, x0, x1, poly, m, o = {}) { for (let i = 1; i + 1 < poly.length; i++) A.prism(0, x0, x1, [poly[0], poly[i], poly[i + 1]], m, o); }
function extrudeZ(A, z0, z1, poly, m, o = {}) { for (let i = 1; i + 1 < poly.length; i++) A.prism(2, z0, z1, [poly[0], poly[i], poly[i + 1]], m, o); }
// ring of n boxes radius R (to the box centre), radial thickness t, depth dep along the ring axis ('x' | 'y' | 'z')
function ring(A, c, R, n, t, dep, axis, m, o = {}, a0 = 0, a1 = 360) {
  const span = a1 - a0, full = Math.abs(span - 360) < 1e-6, cnt = full ? n : n, ch = 2 * (R + t / 2) * Math.sin(Math.PI * (span / 360) / n) * 1.04;
  for (let i = 0; i < cnt; i++) {
    const a = (a0 + (span * (i + 0.5)) / n) / DEG, ad = a * DEG;
    if (axis === "z") A.obox([c[0] + R * Math.cos(a), c[1] + R * Math.sin(a), c[2]], [ch, t, dep], [0, 0, ad - 90], m, o);
    else if (axis === "x") A.obox([c[0], c[1] + R * Math.cos(a), c[2] + R * Math.sin(a)], [dep, t, ch], [ad, 0, 0], m, o);
    else A.obox([c[0] + R * Math.sin(a), c[1], c[2] + R * Math.cos(a)], [ch, dep, t], [0, ad, 0], m, o);
  }
}
// bolt / rivet heads in a row from a to b, facing along axis ('x' | 'y' | 'z')
function boltRow(A, a, b, n, axis, m = "steel_dk", dia = 0.022, h = 0.012) {
  const r = axis === "x" ? [0, 0, 90] : axis === "z" ? [90, 0, 0] : [0, 0, 0];
  for (let i = 0; i < n; i++) A.cyl(n === 1 ? lerp3(a, b, 0.5) : lerp3(a, b, i / (n - 1)), [dia, h, dia], m, { k: 0, lod: 1, seg: 6, r });
}
// panel seam (a recessed joint read as a dark line)
function seam(A, min, max, m = "seam") { return A.box(min, max, m, D); }
// thin wire lines over a rectangle mapped to world by toW(u, v): diamond (chain link) or square grid
function meshDiamond(A, u0, u1, v0, v1, toW, s, m = "wire", dia = 0.005) {
  for (let c = u0 - v1 + s / 2; c < u1 - v0; c += s) {     // u - v = c
    const va = Math.max(v0, u0 - c), vb = Math.min(v1, u1 - c);
    if (vb - va > 0.02) rod(A, toW(va + c, va), toW(vb + c, vb), dia, m, { seg: 4 });
  }
  for (let c = u0 + v0 + s / 2; c < u1 + v1; c += s) {     // u + v = c
    const va = Math.max(v0, c - u1), vb = Math.min(v1, c - u0);
    if (vb - va > 0.02) rod(A, toW(c - va, va), toW(c - vb, vb), dia, m, { seg: 4 });
  }
}
function meshGrid(A, u0, u1, v0, v1, toW, su, sv, m = "wire", dia = 0.005) {
  if (su) for (let u = u0 + su / 2; u < u1; u += su) rod(A, toW(u, v0), toW(u, v1), dia, m, { seg: 4 });
  if (sv) for (let v = v0 + sv / 2; v < v1; v += sv) rod(A, toW(u0, v), toW(u1, v), dia, m, { seg: 4 });
}
// sandbag: rounded pillow, long axis along yaw, with tied ears
function sandbag(A, c, yaw, { L = 0.6, W = 0.34, Hh = 0.14, m = "sandbag", k = 0 } = {}) {
  const f = [Math.sin(yaw / DEG), 0, Math.cos(yaw / DEG)];
  A.rbox([c[0] - W / 2, c[1] - Hh / 2, c[2] - L * 0.42], [c[0] + W / 2, c[1] + Hh / 2, c[2] + L * 0.42], Hh * 0.45, m, { k, r: [0, yaw, 0] });
  // rbox rotates about its own centre, which is c
  for (const s of [-1, 1]) A.cone(add(c, f, s * L * 0.45), [Hh * 0.7, L * 0.1, Hh * 0.7], "sandbag_dk", { k: 0, lod: 1, top: 0.4, r: aimY([f[0] * s, 0, f[2] * s]) });
}
// wheel on an axle along x: c centre, D diameter, tw tread width, sx +1 / -1 the outboard side
function wheel(A, c, D, tw, sx, o = {}) {
  const { rim = "rim", tyre = "rubber", lugs = 5, spokes = 5, flat = false, rimD = 0.62, dual = false } = o;
  const [x, y, z] = c, R90 = [0, 0, 90];
  if (tyre) {
    A.cyl([x, y, z], flat ? [D * 0.84, tw * 1.12, D * 1.02] : [D, tw, D], tyre, { k: 0, seg: 24, r: R90 });
    A.cyl([x + sx * tw * 0.5, y, z], [D * 0.86, 0.016, D * 0.86], "rubber_side", { ...D_, seg: 24, r: R90 });
    if (!flat) for (let i = 0; i < 18; i++) {   // tread blocks round the crown
      const a = (i / 18) * 2 * Math.PI;
      A.obox([x, y + Math.cos(a) * D * 0.5, z + Math.sin(a) * D * 0.5], [tw * 0.86, 0.012, D * 0.09], [a * DEG, 0, 0], "rubber_side", D_);
    }
  }
  const rd = D * rimD, xo = x + sx * (tw * 0.5 + 0.004);
  A.cyl([xo - sx * 0.01, y, z], [rd, 0.02, rd], rim, { k: 0, seg: 20, r: R90 });                 // rim lip
  A.cyl([xo - sx * 0.002, y, z], [rd * 0.84, 0.012, rd * 0.84], "rim_dk", { ...D_, seg: 20, r: R90 });   // dark well between spokes
  for (let i = 0; i < spokes; i++) {
    const a = (i / spokes) * 2 * Math.PI + 0.3;
    A.obox([xo + sx * 0.006, y + Math.cos(a) * rd * 0.25, z + Math.sin(a) * rd * 0.25], [0.012, rd * 0.42, rd * 0.11], [a * DEG, 0, 0], rim, D_);
  }
  A.cyl([xo + sx * 0.012, y, z], [rd * 0.28, 0.02, rd * 0.28], rim, { ...D_, seg: 12, r: R90 });    // hub
  for (let i = 0; i < lugs; i++) {
    const a = (i / lugs) * 2 * Math.PI;
    A.cyl([xo + sx * 0.022, y + Math.cos(a) * rd * 0.1, z + Math.sin(a) * rd * 0.1], [0.018, 0.014, 0.018], "steel_dk", { ...D_, seg: 6, r: R90 });
  }
  if (dual && tyre) A.cyl([x - sx * tw * 1.05, y, z], [D, tw, D], tyre, { k: 0, seg: 24, r: R90 });
}
const D_ = D;
// seat: cushion top at y, centre x, z (front edge toward +z), width w; recline backrest; headrest
function seat(A, x, y, z, w, m = "seat", { back = 0.62, head = true, depth = 0.5 } = {}) {
  A.rbox([x - w / 2, y - 0.14, z - depth], [x + w / 2, y, z], 0.05, m, V);
  A.obox([x, y + back / 2 - 0.02, z - depth - 0.02], [w * 0.96, back, 0.12], [-12, 0, 0], m, { k: 0, bv: 0.03 });
  if (head) A.obox([x, y + back + 0.08, z - depth - 0.09], [Math.min(w * 0.5, 0.28), 0.16, 0.09], [-12, 0, 0], m, dv(0.03));
}
// lifting eye / loop on top of a block
function liftEye(A, x, y, z, yaw = 0, m = "rust", d = 0.12) {
  A.arch([x, y, z], [d, d * 0.8, 0.02], m, { k: 0, lod: 1, th: 0.018, r: [0, yaw, 0] });
}

// ================================================================================================= BARRIERS
// jersey (New Jersey shape) section: 0.61 m base, 75 mm toe, 55 degree lower face to 0.33 m, 84 degree upper face to
// 0.81 m, 0.15 m top
const JERSEY = [[-0.305, 0], [0.305, 0], [0.305, 0.075], [0.126, 0.33], [0.076, 0.81], [-0.076, 0.81], [-0.126, 0.33], [-0.305, 0.075]];
const JERSEY_LOW = [[-0.305, 0], [0.305, 0], [0.305, 0.075], [0.126, 0.33], [-0.126, 0.33], [-0.305, 0.075]];
const scaleY = (poly, sy) => poly.map(([z, y]) => [z, y <= 0.075 ? y : 0.075 + (y - 0.075) * sy]);
function jerseyDress(A, x0, x1, m, R) {
  A.box([x0 + 0.01, 0.8, -0.078], [x1 - 0.01, 0.815, 0.078], m, dv(0.012));                     // top arris
  for (const x of [x0 + 0.45, x1 - 0.45]) A.box([x - 0.16, 0, -0.307], [x + 0.16, 0.07, 0.307], "conc_dk", D);   // drain / forklift slot
  A.box([x0, 0, -0.307], [x1, 0.02, 0.307], "conc_st", D);                                       // grime at the toe
  for (const x of [x0 + 0.55, x1 - 0.55]) A.cyl([x, 0.6, 0], [0.07, 0.24, 0.07], "conc_dk", { ...D, seg: 10, r: [90, 0, 0] });   // lifting hole
  for (let i = 0; i < 4; i++) {   // chipped arrises
    const x = x0 + 0.2 + R() * (x1 - x0 - 0.4), s = R() > 0.5 ? 1 : -1;
    A.obox([x, 0.79, s * 0.07], [0.06 + R() * 0.08, 0.035, 0.03], [0, R() * 30, s * 20], "conc_dk", D);
  }
  for (let i = 0; i < 2; i++) { const x = x0 + 0.3 + R() * (x1 - x0 - 0.6); A.box([x - 0.03, 0.2, 0.2], [x + 0.05, 0.28, 0.23], "conc_dk", D); }
}
function jerseyEnds(A, x0, x1) {
  // pin-and-loop connectors: a steel loop pair at +x, a pin socket at -x
  for (const y of [0.22, 0.6]) {
    A.box([x1, y - 0.03, -0.06], [x1 + 0.05, y + 0.03, 0.06], "rust", dv(0.006));
    A.arch([x1 + 0.03, y - 0.03, 0], [0.12, 0.08, 0.02], "rust", { k: 0, lod: 1, th: 0.016, r: [90, 90, 0] });
  }
  A.box([x0 - 0.004, 0.12, -0.05], [x0, 0.7, 0.05], "conc_dk", D);
}
function jersey(id, name, desc, kind) {
  const L = kind === "end" ? 2.0 : 3.0, A = mk(id, name, "Barriers", desc, [L + 0.1, 0.65]);
  const R = rng(id.length * 97 + 11), x0 = -L / 2, x1 = L / 2;
  if (kind === "end") {
    extrudeX(A, x0, x1 - 0.75, JERSEY, "conc_l");
    extrudeX(A, x1 - 0.75, x1, scaleY(JERSEY, 0.42), "conc_l");
    A.prism(2, -0.125, 0.125, [[x1 - 0.75, 0.42], [x1, 0.42], [x1 - 0.75, 0.81]], "conc_l");   // sloped nose
    jerseyDress(A, x0, x1 - 0.75, "conc_l", R);
    A.box([x1 - 0.7, 0.4, -0.12], [x1 - 0.04, 0.44, 0.12], "conc_st", D);
    for (const s of [-1, 1]) A.box([x1 - 0.4, 0.2, s * 0.21 - 0.001], [x1 - 0.1, 0.32, s * 0.21 + 0.001], "refl_w", D);   // end marker
    A.box([x0 - 0.004, 0.12, -0.05], [x0, 0.7, 0.05], "conc_dk", D);   // pin socket at the joining end
    return fin(A, { cover: boxCover(L / 2 - 0.4, 0.3, 0.81, { ends: false, cx: -0.3 }) });
  }
  if (kind === "damaged") {
    extrudeX(A, x0, -0.35, JERSEY, "conc_l");
    extrudeX(A, -0.35, 0.5, JERSEY_LOW, "conc_l");
    extrudeX(A, 0.5, x1, JERSEY, "conc_l");
    jerseyDress(A, x0, -0.35, "conc_l", R); jerseyDress(A, 0.5, x1, "conc_l", R);
    // broken top: fracture faces, a slumped remnant, bent bars, chunks on the ground
    A.obox([-0.34, 0.56, 0], [0.04, 0.46, 0.2], [0, 0, -8], "conc_dk", V);
    A.obox([0.49, 0.58, 0], [0.04, 0.48, 0.2], [0, 0, 10], "conc_dk", V);
    A.obox([-0.12, 0.38, 0.01], [0.32, 0.12, 0.22], [6, 12, 9], "conc_l", { bv: 0.02 });
    A.obox([0.24, 0.36, -0.02], [0.26, 0.09, 0.2], [-8, -20, -6], "conc_dk", { k: 0, bv: 0.015 });
    for (const [x, z, lx, lz] of [[-0.25, -0.04, -0.1, 0.25], [-0.05, 0.05, 0.05, 0.35], [0.2, -0.05, 0.25, 0.3], [0.35, 0.04, 0.1, 0.42]]) {
      rod(A, [x, 0.3, z], [x + lx * 0.4, 0.62, z + 0.02], 0.016, "rebar");
      rod(A, [x + lx * 0.4, 0.62, z + 0.02], [x + lx, 0.62 + lz * 0.2, z + 0.06], 0.016, "rebar");
    }
    for (let i = 0; i < 7; i++) {
      const s = 0.06 + R() * 0.14, z = (R() > 0.5 ? 1 : -1) * (0.38 + R() * 0.12);
      A.obox([-0.3 + R() * 0.9, s * 0.4, z], [s * 1.3, s * 0.8, s], [R() * 40, R() * 180, R() * 40], R() > 0.4 ? "conc_l" : "conc_dk", { k: 0, bv: 0.01 });
    }
    jerseyEnds(A, x0, x1);
    return fin(A, { cover: boxCover(1.5, 0.3, 0.6) });
  }
  extrudeX(A, x0, x1, JERSEY, "conc_l");
  jerseyDress(A, x0, x1, "conc_l", R);
  jerseyEnds(A, x0, x1);
  return fin(A, { cover: boxCover(1.5, 0.3, 0.81) });
}

function highwayDivider() {
  const A = mk("mod_highway_divider", "Highway median divider (6 m)", "Barriers",
    "Cast-in-place single-slope concrete median barrier, 6 m run, 1.07 m high, 0.61 m base: construction joints every 3 m, drainage scuppers, amber / white delineators on the top and a scuffed grime band at the toe.", [6.1, 0.7]);
  const P = [[-0.305, 0], [0.305, 0], [0.305, 0.05], [0.1, 1.07], [-0.1, 1.07], [-0.305, 0.05]];
  extrudeX(A, -3, 3, P, "conc");
  A.box([-2.99, 1.06, -0.102], [2.99, 1.075, 0.102], "conc", dv(0.01));
  for (const x of [0]) for (const s of [-1, 1]) bar(A, [x, 0.05, s * 0.306], [x, 1.07, s * 0.101], 0.012, 0.004, "conc_dk", D);   // joint
  for (const x of [-2.2, 0.8, 2.2]) A.box([x - 0.2, 0, -0.308], [x + 0.2, 0.1, 0.308], "conc_dk", D);
  for (const x of [-2.4, -1.2, 0, 1.2, 2.4]) {
    A.box([x - 0.03, 1.075, -0.02], [x + 0.03, 1.16, 0.02], "pl_black", D);
    for (const s of [-1, 1]) A.box([x - 0.025, 1.09, s * 0.02], [x + 0.025, 1.15, s * 0.024], s > 0 ? "refl_w" : "lamp_a", D);
  }
  for (const s of [-1, 1]) bar(A, [-3, 0.04, s * 0.3], [3, 0.04, s * 0.3], 0.06, 0.06, "conc_st", D);
  return fin(A, { cover: boxCover(3, 0.3, 1.07, { step: 2 }) });
}

function ecologyBlock() {
  const A = mk("mod_ecology_block", "Ecology block (interlocking concrete)", "Barriers",
    "Interlocking 'lego' ecology block of recycled concrete, 1.6 x 0.8 x 0.8 m (~2.4 t): two locating studs on top, matching sockets below, a cast-in lifting loop, rough aggregate faces with chipped arrises.", [1.62, 0.82]);
  const R = rng(4411);
  A.box([-0.8, 0, -0.4], [0.8, 0.8, 0.4], "conc_ag", { bv: 0.03 });
  for (const x of [-0.4, 0.4]) {
    A.cyl([x, 0.85, 0], [0.3, 0.1, 0.3], "conc_ag", { k: 0, seg: 16 });
    A.cyl([x, 0.9, 0], [0.26, 0.004, 0.26], "conc_dk", { ...D, seg: 16 });
    A.box([x - 0.16, -0.001, -0.16], [x + 0.16, 0.002, 0.16], "conc_dk", D);
  }
  liftEye(A, 0, 0.8, 0, 0, "rust", 0.14);
  for (const s of [-1, 1]) A.box([-0.78, 0.38, s * 0.401 - 0.001], [0.78, 0.4, s * 0.401 + 0.001], "conc_dk", D);   // pour line
  for (let i = 0; i < 6; i++) {
    const x = (R() - 0.5) * 1.5, s = R() > 0.5 ? 1 : -1, top = R() > 0.5;
    A.obox([x, top ? 0.79 : 0.02, s * 0.39], [0.08 + R() * 0.1, 0.04, 0.04], [R() * 20, 0, R() * 20], "conc_dk", D);
  }
  return fin(A, { cover: boxCover(0.8, 0.4, 0.8, { ends: true }) });
}

function tWall() {
  const A = mk("mod_twall", "T-wall blast barrier (3.7 m)", "Barriers",
    "Precast reinforced concrete T-wall blast barrier, 3.66 m tall, 1.52 m wide on a 1.22 m inverted-T footing: tapered 0.3 to 0.2 m stem with haunches, two lifting eyes, formwork-textured faces, joint keys and grime streaks.", [1.55, 1.25]);
  const R = rng(7331);
  A.box([-0.76, 0, -0.61], [0.76, 0.3, 0.61], "conc_fw", { bv: 0.02 });
  extrudeX(A, -0.76, 0.76, [[-0.15, 0.3], [0.15, 0.3], [0.1, 3.66], [-0.1, 3.66]], "conc_fw");
  for (const s of [-1, 1]) extrudeX(A, -0.76, 0.76, s > 0 ? [[0.15, 0.3], [0.42, 0.3], [0.15, 0.55]] : [[-0.42, 0.3], [-0.15, 0.3], [-0.15, 0.55]], "conc_fw");
  A.box([-0.755, 3.64, -0.1], [0.755, 3.68, 0.1], "conc_fw", dv(0.015));
  for (const x of [-0.4, 0.4]) liftEye(A, x, 3.68, 0, 90, "rust", 0.14);
  for (const x of [-0.76, 0.76]) A.box([x - 0.006, 0.4, -0.03], [x + 0.006, 3.5, 0.03], "conc_dk", D);   // shear key groove
  for (let i = 0; i < 5; i++) {   // run-off streaks on both faces
    const x = -0.6 + R() * 1.2, s = R() > 0.5 ? 1 : -1, y1 = 3.6 - R() * 0.4, y0 = 1 + R() * 1.5;
    bar(A, [x, y0, s * (0.15 - 0.05 * (y0 - 0.3) / 3.36 + 0.004)], [x, y1, s * (0.15 - 0.05 * (y1 - 0.3) / 3.36 + 0.004)], 0.04 + R() * 0.05, 0.003, "conc_st", D);
  }
  for (const s of [-1, 1]) A.box([-0.762, 0, s * 0.612 - 0.002], [0.762, 0.05, s * 0.612], "conc_st", D);
  return fin(A, { cover: [cover([0, 0, 0.95], 180, "high", ["left", "right"]), cover([0, 0, -0.95], 0, "high", ["left", "right"])] });
}

function bollard(kind) {
  const ids = { concrete: "mod_bollard_concrete", steel: "mod_bollard_steel", up: "mod_bollard_retractable", down: "mod_bollard_retracted" };
  const names = { concrete: "Bollard (concrete)", steel: "Bollard (steel post)", up: "Retractable bollard (raised)", down: "Retractable bollard (lowered)" };
  const desc = {
    concrete: "Precast concrete security bollard, 0.4 m diameter, 0.9 m high, with a domed top and two reflective bands.",
    steel: "Painted steel tube bollard, 168 mm diameter, 1.0 m high, domed cap, yellow sleeve band, bolted base plate.",
    up: "Hydraulic retractable security bollard raised: 275 mm stainless sleeve 0.8 m proud of a flush ground flange, red reflective band.",
    down: "Hydraulic retractable bollard lowered flush: only the ground flange and the cap plate show (walk / drive over).",
  };
  const A = mk(ids[kind], names[kind], "Barriers", desc[kind], kind === "concrete" ? [0.5, 0.5] : [0.45, 0.45]);
  if (kind === "concrete") {
    A.cyl([0, 0.4, 0], [0.4, 0.8, 0.4], "conc_l", { seg: 20 });
    A.cone([0, 0.85, 0], [0.4, 0.1, 0.4], "conc_l", { k: 0, seg: 20, top: 0.55 });
    for (const y of [0.62, 0.72]) A.cyl([0, y, 0], [0.405, 0.05, 0.405], "refl_w", { ...D, seg: 20 });
    A.cyl([0, 0.01, 0], [0.46, 0.02, 0.46], "conc_dk", { ...D, seg: 20 });
    A.box([-0.04, 0.3, 0.19], [0.03, 0.36, 0.203], "conc_dk", D);
    return fin(A, { cover: [cover([0, 0, -0.55], 0, "low", ["left", "right"])] });
  }
  if (kind === "steel") {
    A.cyl([0, 0.5, 0], [0.168, 1.0, 0.168], "p_black", { seg: 16 });
    A.cone([0, 1.02, 0], [0.17, 0.05, 0.17], "p_black", { k: 0, seg: 16, top: 0.4 });
    A.cyl([0, 0.82, 0], [0.172, 0.1, 0.172], "p_yellow", { ...D, seg: 16 });
    A.box([-0.15, 0, -0.15], [0.15, 0.015, 0.15], "steel_dk", dv(0.004));
    for (const [x, z] of [[-1, -1], [1, -1], [1, 1], [-1, 1]]) A.cyl([x * 0.11, 0.022, z * 0.11], [0.03, 0.014, 0.03], "steel", { ...D, seg: 6 });
    return fin(A);
  }
  A.cyl([0, 0.006, 0], [0.42, 0.012, 0.42], "steel_dk", { ...(kind === "down" ? V : {}), seg: 24 });
  for (let i = 0; i < 6; i++) { const a = (i / 6) * 2 * Math.PI; A.cyl([Math.cos(a) * 0.175, 0.014, Math.sin(a) * 0.175], [0.022, 0.008, 0.022], "steel", { ...D, seg: 6 }); }
  if (kind === "down") {
    A.cyl([0, 0.016, 0], [0.27, 0.008, 0.27], "steel", { ...V, seg: 24 });
    A.cyl([0, 0.021, 0], [0.12, 0.004, 0.12], "steel_dk", { ...D, seg: 16 });
    return fin(A, { effect: "none" });
  }
  A.cyl([0, 0.41, 0], [0.275, 0.8, 0.275], "steel", { seg: 20 });
  A.cyl([0, 0.8, 0], [0.26, 0.02, 0.26], "steel_dk", { ...D, seg: 20 });
  A.cyl([0, 0.66, 0], [0.28, 0.08, 0.28], "refl_r", { ...D, seg: 20 });
  A.cyl([0, 0.81, 0], [0.08, 0.01, 0.08], "lamp_a", { ...D, seg: 12 });
  return fin(A);
}

// ================================================================================================= FORTIFICATIONS
const HB = 1.06, HH = 1.37;   // HESCO-type cell: 1.06 m square, 1.37 m high
function hesco(nx, ny, half = false) {
  const id = `mod_hesco_${nx}x${ny}${half ? "_half" : ""}`;
  const X = nx * HB, Y = ny * HH, hx = X / 2, hz = HB / 2;
  const A = mk(id, `HESCO barrier ${nx} bay${nx > 1 ? "s" : ""}${ny > 1 ? ", 2 high" : ""}${half ? " (half filled)" : ""}`, "Fortifications",
    `Collapsible welded-mesh gabion (HESCO-type) defensive wall: ${nx} x ${HB} m cell${nx > 1 ? "s" : ""}, ${ny} high (${Y.toFixed(2)} m), geotextile liner, galvanised mesh with coil joints, ${half ? "only half filled with sand (the upper liner sags empty)" : "filled with compacted sand and gravel"}.`,
    [X + 0.06, HB + 0.06]);
  const fill = half ? HH * 0.5 : HH - 0.07;
  for (let j = 0; j < ny; j++) {
    const y0 = j * HH, y1 = y0 + HH, top = j === ny - 1;
    // liner faces just inside the mesh, end faces, dividers between cells
    for (const s of [-1, 1]) A.box([-hx + 0.004, y0, s * (hz - 0.012) - 0.006], [hx - 0.004, y1 - 0.01, s * (hz - 0.012) + 0.006], "hesco_fab", V);
    for (const s of [-1, 1]) A.box([s * (hx - 0.012) - 0.006, y0, -hz + 0.004], [s * (hx - 0.012) + 0.006, y1 - 0.01, hz - 0.004], "hesco_fab", V);
    for (let i = 1; i < nx; i++) A.box([-hx + i * HB - 0.006, y0, -hz + 0.01], [-hx + i * HB + 0.006, y1 - 0.01, hz - 0.01], "hesco_fab", V);
    // fill surface (slightly mounded) when it shows
    const fy = top ? y0 + fill : y1 - 0.01;
    if (top) for (let i = 0; i < nx; i++) {
      const cx = -hx + (i + 0.5) * HB;
      A.box([cx - hz + 0.02, fy - 0.04, -hz + 0.02], [cx + hz - 0.02, fy, hz - 0.02], "sand", V);
      A.rbox([cx - 0.36, fy - 0.03, -0.36], [cx + 0.36, fy + 0.035, 0.36], 0.03, "sand", D);
    }
    if (top && half) for (const s of [-1, 1]) for (let i = 0; i < nx * 3; i++) {   // the empty liner above the fill sags inward
      const x = -hx + (i + 0.5) * (HB / 3);
      A.obox([x, y1 - 0.12, s * (hz - 0.06)], [HB / 3 - 0.01, 0.2, 0.01], [s * 14, 0, 0], "hesco_fab", D);
    }
    // mesh: square grid on the long faces and the ends, a heavier top / bottom edge wire, coil joints at cell corners
    for (const s of [-1, 1]) {
      meshGrid(A, -hx, hx, y0, y1, (u, v) => [u, v, s * hz], 0.153, 0.153, "wire", 0.0055);
      rod(A, [-hx, y1 - 0.01, s * hz], [hx, y1 - 0.01, s * hz], 0.012, "galv", { seg: 6 });
      meshGrid(A, -hz, hz, y0, y1, (u, v) => [s * hx, v, u], 0.153, 0.153, "wire", 0.0055);
    }
    for (let i = 0; i <= nx; i++) for (const s of [-1, 1]) {
      const x = -hx + i * HB;
      rod(A, [x, y0 + 0.02, s * hz], [x, y1 - 0.02, s * hz], 0.024, "galv", { seg: 6 });
      for (let k = 0; k < 9; k++) A.cyl([x, y0 + 0.08 + k * 0.152, s * hz], [0.04, 0.012, 0.04], "galv", { ...D, seg: 8 });
    }
  }
  A.box([-hx, 0, -hz], [hx, Y - (half ? HH - fill - 0.25 : 0), hz], "sand", H);
  if (half && ny > 1) A.box([-hx, 0, -hz], [hx, Y, hz], "sand", H);
  return fin(A, { cover: boxCover(hx, hz, half && ny === 1 ? 0.8 : Y, { step: HB }) });
}

function sandbagPosition() {
  const A = mk("mod_sandbag_position", "Sandbag fighting position (wood framed)", "Fortifications",
    "Modern U-shaped sandbag fighting position, 2.6 x 1.9 m: polypropylene sandbags laid in staggered courses to 1.15 m against a 2x4 timber frame with plywood revetment on the inside, open to the rear.", [2.8, 2.1]);
  const W = 1.2, ZF = 0.82, ZB = -0.95, C = 8, h = 0.142;
  for (let c = 0; c < C; c++) {
    const y = c * h + h / 2, o = c % 2 ? 0.3 : 0;
    for (let x = -W - 0.17 + 0.3 + o; x < W + 0.17 - 0.2; x += 0.6) sandbag(A, [x, y, ZF + 0.17], 90, { m: c % 3 ? "sandbag" : "sandbag_od" });
    for (const s of [-1, 1]) for (let z = ZF - 0.3 - o; z > ZB + 0.1; z -= 0.6) sandbag(A, [s * (W + 0.17), y, z], 0, { m: (c + (s > 0 ? 1 : 0)) % 3 ? "sandbag" : "sandbag_od" });
  }
  // timber frame and ply revetment inside
  for (const [x, z] of [[-W + 0.05, ZF - 0.03], [W - 0.05, ZF - 0.03], [-W + 0.05, ZB + 0.3], [W - 0.05, ZB + 0.3], [0, ZF - 0.03]]) A.box([x - 0.045, 0, z - 0.02], [x + 0.045, 1.3, z + 0.02], "wood_raw", vb(0.006));
  A.box([-W + 0.01, 0.05, ZF - 0.01], [W - 0.01, 1.12, ZF + 0.008], "ply", V);
  for (const s of [-1, 1]) A.box([s * W - 0.009, 0.05, ZB + 0.25], [s * W + 0.009, 1.12, ZF], "ply", V);
  for (const y of [0.3, 1.0]) A.box([-W + 0.1, y, ZF - 0.06], [W - 0.1, y + 0.09, ZF - 0.01], "wood_raw", vb(0.005));
  A.box([-W - 0.34, 1.14, ZF - 0.01], [W + 0.34, 1.17, ZF + 0.36], "wood_raw", vb(0.006));   // sill board for resting a weapon
  // colliders
  A.box([-W - 0.34, 0, ZF], [W + 0.34, 1.17, ZF + 0.36], "sandbag", H);
  for (const s of [-1, 1]) A.box([s > 0 ? W : -W - 0.34, 0, ZB], [s > 0 ? W + 0.34 : -W, 1.14, ZF], "sandbag", H);
  return fin(A, {
    cover: [-0.7, 0, 0.7].map((x) => cover([x, 0, ZF - 0.45], 0, "low", ["over"])).concat([cover([0, 0, ZF + 0.95], 180, "low", ["over"])]),
    firingPoints: [-0.7, 0.7].map((x) => fire([x, 0, ZF - 0.35], 0, "firestep")),
  });
}

function guardTower() {
  const PY = 3.2, ZT = 1.65, L = 1.35, RY = PY + 2.25;
  const A = mk("mod_guard_tower", "Steel guard tower", "Fortifications",
    "Modern steel guard tower: four square-tube legs with cross bracing carrying a 2.7 m chequer-plate platform 3.2 m up, enclosed by a steel kick wall and sandbags to chest height with a corrugated roof on posts; a steel stair with handrails climbs from the rear, and a caged ladder runs up one leg to a roof hatch. The platform is walkable.",
    [3.3, 6.6], { enterable: true, floors: 1 });
  const at = (x, y, z) => [x, y, z + ZT];
  // legs, girts, bracing
  for (const [sx, sz] of [[-1, -1], [1, -1], [1, 1], [-1, 1]]) {
    A.box(at(sx * L - 0.075, 0, sz * L - 0.075), at(sx * L + 0.075, RY, sz * L + 0.075), "p_tan", { bv: 0.008 });
    A.box(at(sx * L - 0.16, 0, sz * L - 0.16), at(sx * L + 0.16, 0.02, sz * L + 0.16), "steel_dk", dv(0.004));
    A.box(at(sx * L - 0.25, -0.3, sz * L - 0.25), at(sx * L + 0.25, 0.04, sz * L + 0.25), "conc", vb(0.02));   // pad footing
    boltRow(A, at(sx * L - 0.12, 0.03, sz * L - 0.12), at(sx * L + 0.12, 0.03, sz * L - 0.12), 2, "y");
    boltRow(A, at(sx * L - 0.12, 0.03, sz * L + 0.12), at(sx * L + 0.12, 0.03, sz * L + 0.12), 2, "y");
  }
  for (const y of [1.6, PY - 0.1]) {
    for (const s of [-1, 1]) {
      A.box(at(-L, y - 0.05, s * L - 0.04), at(L, y + 0.05, s * L + 0.04), "p_tan", vb(0.006));
      A.box(at(s * L - 0.04, y - 0.05, -L), at(s * L + 0.04, y + 0.05, L), "p_tan", vb(0.006));
    }
  }
  for (const [y0, y1] of [[0.1, 1.6], [1.6, PY - 0.1]]) for (const s of [-1, 1]) {
    bar(A, at(-L, y0, s * L), at(L, y1, s * L), 0.05, 0.05, "p_tan"); bar(A, at(L, y0, s * L), at(-L, y1, s * L), 0.05, 0.05, "p_tan");
    if (s > 0) { bar(A, at(s * L, y0, -L), at(s * L, y1, L), 0.05, 0.05, "p_tan"); bar(A, at(s * L, y0, L), at(s * L, y1, -L), 0.05, 0.05, "p_tan"); }
  }
  // platform: chequer deck on channels, kick wall, sandbags, posts, roof
  A.box(at(-L - 0.05, PY - 0.06, -L - 0.05), at(L + 0.05, PY, L + 0.05), "steel", { bv: 0.01 });
  for (let i = 0; i < 26; i++) for (let j = 0; j < 26; j++) if ((i + j) % 2 === 0 && i % 3 === 0 && j % 3 === 0) A.obox(at(-L + 0.1 + i * 0.1, PY + 0.003, -L + 0.1 + j * 0.1), [0.04, 0.006, 0.012], [0, (i % 2) * 90 + 45, 0], "steel", D);
  for (const s of [-1, 1]) A.box(at(-L - 0.05, PY - 0.22, s * L - 0.05), at(L + 0.05, PY - 0.06, s * L + 0.05), "p_tan", vb(0.006));
  const gap0 = 0.35, gap1 = 1.2;   // rear opening where the stair arrives (x from gap0 to gap1)
  const kw = (min, max) => { A.box(at(...min), at(...max), "p_tan", { bv: 0.006 }); };
  kw([-L - 0.05, PY, L - 0.01], [L + 0.05, PY + 1.05, L + 0.05]);
  for (const s of [-1, 1]) kw([s * L - 0.03, PY, -L - 0.05], [s * L + 0.03, PY + 1.05, L + 0.05]);
  kw([-L - 0.05, PY, -L - 0.05], [gap0, PY + 1.05, -L + 0.01]); kw([gap1, PY, -L - 0.05], [L + 0.05, PY + 1.05, -L + 0.01]);
  for (const s of [-1, 1]) for (const y of [PY + 0.35, PY + 0.7]) A.box(at(-L - 0.06, y, s * L - 0.06), at(L + 0.06, y + 0.025, s * L + 0.06), "p_tan", D);
  for (let c = 0; c < 6; c++) {   // sandbags inside the kick wall: front and both sides, front of the rear wall
    const y = PY + 0.07 + c * 0.14, o = c % 2 ? 0.3 : 0;
    for (let x = -L + 0.45 + o; x < L - 0.3; x += 0.6) sandbag(A, at(x, y, L - 0.2), 90, { m: c % 2 ? "sandbag" : "sandbag_od" });
    for (const s of [-1, 1]) for (let z = L - 0.65 - o; z > -L + 0.3; z -= 0.6) sandbag(A, at(s * (L - 0.2), y, z), 0);
    for (let x = -L + 0.5 + o; x < gap0 - 0.2; x += 0.6) sandbag(A, at(x, y, -L + 0.2), 90);
  }
  for (const [sx, sz] of [[-1, -1], [1, -1], [1, 1], [-1, 1]]) A.box(at(sx * L - 0.06, PY + 1.05, sz * L - 0.06), at(sx * L + 0.06, PY + 1.12, sz * L + 0.06), "steel_dk", D);
  for (const s of [-1, 1]) A.box(at(-L - 0.1, RY - 0.12, s * L - 0.05), at(L + 0.1, RY, s * L + 0.05), "p_tan", vb(0.006));
  for (const x of [-0.7, 0, 0.7]) A.box(at(x - 0.03, RY - 0.1, -L - 0.1), at(x + 0.03, RY, L + 0.1), "p_tan", D);
  A.obox(at(0, RY + 0.05, 0), [3.2, 0.03, 3.2], [-3, 0, 0], "galv_c", { bv: 0.01 });
  A.obox(at(0, RY + 0.1, L + 0.25), [3.25, 0.12, 0.03], [0, 0, 0], "p_tan", D);
  A.box(at(-0.95, RY + 0.06, -0.95), at(-0.35, RY + 0.12, -0.35), "steel_dk", vb(0.008));   // roof hatch
  // caged ladder up the inside of the -x,-z leg to the hatch (visual)
  for (const s of [-1, 1]) A.box(at(-0.65 + s * 0.22 - 0.02, PY, -0.98 - 0.02), at(-0.65 + s * 0.22 + 0.02, RY + 0.05, -0.98 + 0.02), "steel", D);
  for (let y = PY + 0.25; y < RY; y += 0.28) rod(A, at(-0.87, y, -0.98), at(-0.43, y, -0.98), 0.025, "steel", { seg: 6 });
  for (let y = PY + 1.2; y < RY; y += 0.45) A.arch(at(-0.65, y, -0.98), [0.56, 0.38, 0.03], "steel", { k: 0, lod: 1, th: 0.02, r: [90, 0, 180] });
  // stair: 16 risers of 0.2 m, 0.215 m goings, from the rear (z = ZT - L - 3.45) up to the platform edge
  const n = 16, r = PY / n, g = 3.44 / n, zs = ZT - L - 0.02, zg = zs - n * g;
  for (let i = 0; i < n; i++) {
    const top = (i + 1) * r, za = zg + i * g;
    A.box([gap0 + 0.03, top - 0.035, za], [gap1 - 0.03, top, za + g + 0.02], "steel", { k: 0, bv: 0.004 });
    seam(A, [gap0 + 0.05, top + 0.001, za + 0.03], [gap1 - 0.05, top + 0.003, za + 0.04]);
  }
  const ang = Math.atan2(PY, zs - zg) * DEG, Ls = Math.hypot(PY, zs - zg);
  A.obox([(gap0 + gap1) / 2, PY / 2 - 0.15, (zs + zg) / 2], [gap1 - gap0, 0.3, Ls + 0.1], [-ang, 0, 0], "steel", H);   // walking ramp
  for (const x of [gap0, gap1]) {
    bar(A, [x, -0.05, zg - 0.1], [x, PY, zs], 0.03, 0.2, "p_tan", D);                      // stringers
    bar(A, [x + (x === gap0 ? -0.03 : 0.03), 0.95, zg], [x + (x === gap0 ? -0.03 : 0.03), PY + 0.95, zs], 0.04, 0.04, "steel", D);   // handrail
    for (let i = 0; i <= 4; i++) { const t = i / 4; rod(A, [x, t * PY, zg + t * (zs - zg)], [x + (x === gap0 ? -0.03 : 0.03), t * PY + 0.95, zg + t * (zs - zg)], 0.035, "steel", { seg: 6 }); }
  }
  // colliders: legs above are solid boxes already; kick walls; roof
  A.box(at(-L - 0.1, RY, -L - 0.1), at(L + 0.1, RY + 0.12, L + 0.1), "galv_c", H);
  const yaw = (dx, dz) => Math.round(Math.atan2(dx, dz) * DEG);
  return fin(A, {
    rooms: [room("platform", at(-L + 0.05, PY, -L + 0.05), at(L - 0.05, RY - 0.15, L - 0.05))],
    entrances: [{ p: [(gap0 + gap1) / 2, 0, zg - 0.6], dir: [0, 0, 1], room: "platform" }],
    corners: [],
    firingPoints: [fire(at(0, PY, L - 0.55), 0, "firestep"), fire(at(L - 0.55, PY, 0), 90, "firestep"), fire(at(-L + 0.55, PY, 0), 270, "firestep"), fire(at(-0.5, PY, -L + 0.55), 180, "firestep")],
    cover: [cover(at(0, PY, L - 0.55), 0, "low", ["over"]), cover(at(L - 0.55, PY, 0.3), yaw(1, 0), "low", ["over"]), cover(at(-L + 0.55, PY, 0.3), yaw(-1, 0), "low", ["over"])],
  });
}

function overheadPosition() {
  const A = mk("mod_fighting_position_oh", "Fighting position with overhead cover", "Fortifications",
    "Sandbagged two-man fighting position with overhead cover, 3.2 x 3.0 m: sandbag walls to 1.3 m on the front and flanks, a 0.5 m firing slit under a roof of 4x4 posts, 2x8 stringers, plywood and two courses of sandbags; entered from the open rear.", [3.3, 3.1], { enterable: true });
  const W = 1.25, ZF = 1.05, ZB = -1.3, WT = 1.3, RT = 1.85, h = 0.142;
  for (let c = 0; c < 9; c++) {
    const y = c * h + h / 2, o = c % 2 ? 0.3 : 0;
    for (let x = -W - 0.17 + 0.3 + o; x < W + 0.1; x += 0.6) sandbag(A, [x, y, ZF + 0.17], 90, { m: (c + Math.round(x * 3)) % 4 ? "sandbag" : "sandbag_od" });
    for (const s of [-1, 1]) for (let z = ZF - 0.3 - o; z > ZB + 0.15; z -= 0.6) sandbag(A, [s * (W + 0.17), y, z], 0);
  }
  for (const [x, z] of [[-W + 0.06, ZF - 0.06], [W - 0.06, ZF - 0.06], [-W + 0.06, ZB + 0.15], [W - 0.06, ZB + 0.15]]) A.box([x - 0.045, 0, z - 0.045], [x + 0.045, RT, z + 0.045], "wood_raw", { bv: 0.006 });
  for (const s of [-1, 1]) A.box([-W - 0.4, RT, s > 0 ? ZF - 0.1 : ZB + 0.1], [W + 0.4, RT + 0.2, s > 0 ? ZF : ZB + 0.2], "wood_raw", vb(0.008));   // headers
  for (let x = -W; x <= W + 0.01; x += 0.42) A.box([x - 0.025, RT + 0.2, ZB], [x + 0.025, RT + 0.4, ZF + 0.4], "wood_raw", vb(0.005));
  A.box([-W - 0.4, RT + 0.4, ZB], [W + 0.4, RT + 0.42, ZF + 0.42], "ply", V);
  for (let c = 0; c < 2; c++) for (let z = ZB + 0.15 + (c ? 0.15 : 0); z < ZF + 0.35; z += 0.36) for (let x = -W - 0.15 + (c ? 0.3 : 0); x < W + 0.3; x += 0.6) sandbag(A, [x, RT + 0.49 + c * h, z], 90, { m: c ? "sandbag_od" : "sandbag" });
  A.box([-W + 0.02, 0.05, ZF - 0.012], [W - 0.02, WT - 0.05, ZF], "ply", V);
  // colliders: walls, roof
  A.box([-W - 0.34, 0, ZF], [W + 0.34, WT, ZF + 0.36], "sandbag", H);
  for (const s of [-1, 1]) A.box([s > 0 ? W : -W - 0.34, 0, ZB], [s > 0 ? W + 0.34 : -W, WT, ZF], "sandbag", H);
  A.box([-W - 0.4, RT, ZB], [W + 0.4, RT + 0.72, ZF + 0.42], "sandbag", H);
  return fin(A, {
    rooms: [room("position", [-W + 0.1, 0, ZB + 0.25], [W - 0.1, RT - 0.05, ZF - 0.1])],
    entrances: [{ p: [0, 0, ZB - 0.6], dir: [0, 0, 1], room: "position" }],
    firingPoints: [fire([-0.55, 0, ZF - 0.4], 0, "embrasure"), fire([0.55, 0, ZF - 0.4], 0, "embrasure")],
    cover: [cover([-0.55, 0, ZF - 0.4], 0, "low", ["over"]), cover([0.55, 0, ZF - 0.4], 0, "low", ["over"])],
  });
}

// ================================================================================================= FENCES / GATES
// concertina coil along x from x0 to x1 at height y (coil centre), z: two opposite-hand helices clipped together
function concertina(A, x0, x1, y, z, dia, pitch = 0.3, seg = 10, m = "galv") {
  const turns = Math.max(2, Math.round((x1 - x0) / pitch)), r = dia / 2;
  for (const hand of [1, -1]) {
    let prev = null;
    for (let i = 0; i <= turns * seg; i++) {
      const t = i / seg, a = hand * t * 2 * Math.PI + (hand < 0 ? Math.PI : 0);
      const p = [x0 + (t / turns) * (x1 - x0), y + Math.cos(a) * r, z + Math.sin(a) * r];
      if (prev) rod(A, prev, p, 0.006, m, { seg: 4 });
      prev = p;
    }
  }
  for (let i = 0; i < turns; i += 2) A.box([x0 + (i / turns) * (x1 - x0) - 0.01, y + r - 0.02, z - 0.01], [x0 + (i / turns) * (x1 - x0) + 0.01, y + r + 0.01, z + 0.01], "steel", D);   // clips
}
function chainLinkPanel(razor) {
  const W = 3.0, HM = 1.8, HP = razor ? 2.0 : 1.85;
  const A = mk(razor ? "mod_chainlink_razor" : "mod_chainlink_panel", razor ? "Chain-link fence with razor wire (3 m)" : "Chain-link fence panel (3 m)", "Fences",
    razor ? "Security chain-link fence bay, 3 m between galvanised posts: 1.8 m diamond mesh on a top rail and bottom tension wire, 45 degree outrigger arms carrying three strands of barbed wire and a 0.45 m razor-wire coil (2.5 m overall)."
      : "Galvanised chain-link fence bay, 3 m between posts: 1.8 m diamond mesh with a top rail, bottom tension wire, post caps, tie wires and a concrete footing at each post.", [3.15, razor ? 0.7 : 0.2]);
  for (const x of [-W / 2, W / 2]) {
    A.cyl([x, HP / 2, 0], [0.06, HP, 0.06], "galv", { seg: 12 });
    A.cone([x, HP + 0.025, 0], [0.072, 0.05, 0.072], "galv", { ...D, seg: 12, top: 0.6 });
    A.cyl([x, 0.03, 0], [0.26, 0.06, 0.26], "conc_r", { ...D, seg: 12 });
    for (let y = 0.2; y < HM; y += 0.4) A.box([x - 0.035, y, -0.03], [x + 0.035, y + 0.02, 0.03], "galv", D);   // tension bands
  }
  rod(A, [-W / 2, HM, 0.035], [W / 2, HM, 0.035], 0.042, "galv", { seg: 10 });
  rod(A, [-W / 2, 0.05, 0.03], [W / 2, 0.05, 0.03], 0.006, "wire", { seg: 4 });
  meshDiamond(A, -W / 2 + 0.03, W / 2 - 0.03, 0.05, HM, (u, v) => [u, v, 0.03], 0.12, "wire", 0.0045);
  for (let x = -1.2; x < 1.3; x += 0.6) A.box([x - 0.006, HM - 0.03, 0.02], [x + 0.006, HM + 0.03, 0.055], "wire", D);   // ties
  A.box([-W / 2, 0, -0.03], [W / 2, HM, 0.06], "wire", H);
  if (razor) {
    for (const x of [-W / 2, W / 2]) {
      bar(A, [x, HP - 0.02, 0], [x, HP + 0.42, -0.42], 0.05, 0.05, "galv");
      A.box([x - 0.03, HP - 0.06, -0.03], [x + 0.03, HP + 0.04, 0.03], "galv", D);
    }
    for (let i = 1; i <= 3; i++) { const t = i / 3.3; rod(A, [-W / 2, HP + 0.42 * t, -0.42 * t], [W / 2, HP + 0.42 * t, -0.42 * t], 0.005, "wire", { seg: 4 }); }
    concertina(A, -W / 2, W / 2, HP + 0.2, -0.05, 0.45, 0.22, 8);
    A.box([-W / 2, HM, -0.5], [W / 2, HP + 0.45, 0.06], "wire", H);
  }
  return fin(A, { effect: "carve" });
}
// a gate leaf from hinge (x, z) running along unit dir (dx, dz) for width w: tube frame, mesh, diagonal brace
function meshLeaf(A, hx, hz, dx, dz, w, h, m = "galv") {
  const at = (u, v) => [hx + dx * u, v, hz + dz * u];
  for (const v of [0.08, h]) rod(A, at(0.02, v), at(w, v), 0.042, m, { k: 0, seg: 10 });
  for (const u of [0.02, w]) rod(A, at(u, 0.08), at(u, h), 0.042, m, { k: 0, seg: 10 });
  rod(A, at(0.02, h / 2 + 0.04), at(w, h / 2 + 0.04), 0.034, m, { seg: 8 });
  rod(A, at(0.05, 0.1), at(w - 0.03, h - 0.03), 0.02, m, { seg: 6 });
  meshDiamond(A, 0.04, w - 0.02, 0.1, h - 0.02, at, 0.12, "wire", 0.0045);
  for (const v of [0.3, h - 0.25]) A.cyl(at(-0.02, v), [0.07, 0.12, 0.07], "steel_dk", { ...D, seg: 10 });   // hinge collars
  bar(A, at(0, h / 2), at(w, h / 2), 0.05, h, m, H);
  return at;
}
function chainLinkGate(open) {
  const W = 3.6, Hh = 1.8, zo = open ? -0.9 : 0;
  const A = mk(open ? "mod_chainlink_gate_open" : "mod_chainlink_gate", open ? "Chain-link double gate (open)" : "Chain-link double gate (closed)", "Fences",
    `Galvanised chain-link double drive gate, 3.6 m clear between 100 mm posts, two 1.8 m tube-frame leaves with diamond mesh and diagonal braces, hinge collars, a drop rod and a fork latch; ${open ? "both leaves swung fully open" : "closed and latched"}.`, [3.95, open ? 1.95 : 0.3]);
  for (const s of [-1, 1]) {
    A.cyl([s * (W / 2 + 0.06), 1.0, zo], [0.1, 2.0, 0.1], "galv", { seg: 12 });
    A.cone([s * (W / 2 + 0.06), 2.03, zo], [0.115, 0.06, 0.115], "galv", { ...D, seg: 12, top: 0.6 });
    A.cyl([s * (W / 2 + 0.06), 0.03, zo], [0.32, 0.06, 0.32], "conc_r", { ...D, seg: 12 });
  }
  const w = W / 2 - 0.04;
  if (open) {
    meshLeaf(A, -W / 2 + 0.01, zo + 0.06, 0, 1, w, Hh);
    meshLeaf(A, W / 2 - 0.01, zo + 0.06, 0, 1, w, Hh);
  } else {
    meshLeaf(A, -W / 2 + 0.01, 0, 1, 0, w, Hh);
    const at = meshLeaf(A, W / 2 - 0.01, 0, -1, 0, w, Hh);
    rod(A, at(w - 0.06, 0.0), at(w - 0.06, 1.0), 0.016, "steel", { seg: 6 });   // drop rod
    A.box([-0.06, 1.0, -0.03], [0.06, 1.06, 0.03], "steel_dk", D);              // fork latch
  }
  return fin(A, { effect: "carve" });
}
function slidingGate() {
  const ox = -0.38, at = (x, y, z) => [x + ox, y, z];
  const A = mk("mod_sliding_gate", "Security sliding gate (6 m)", "Fences",
    "Steel security sliding gate for a 6 m opening: a 2.1 m painted box-section leaf with square-bar infill and spiked top rail running on a ground track on V-wheels, a guide post with nylon top rollers, a receiving post, an operator motor housing on a concrete pad with a warning beacon and a safety-edge strip.", [8.0, 1.4]);
  const x0 = -3.1, x1 = 3.3, Hh = 2.1;
  A.box(at(-3.6, 0, -0.06), at(3.9, 0.01, 0.06), "steel_dk", D);
  A.box(at(-3.5, 0.01, -0.012), at(3.8, 0.04, 0.012), "steel", D);                       // track rail
  A.box(at(x0, 0.09, -0.04), at(x1, 0.19, 0.04), "p_dgrey", { bv: 0.006 });
  A.box(at(x0, Hh - 0.1, -0.04), at(x1, Hh, 0.04), "p_dgrey", { bv: 0.006 });
  A.box(at(x0, 0.19, -0.03), at(x0 + 0.08, Hh - 0.1, 0.03), "p_dgrey", { bv: 0.006 });
  A.box(at(x1 - 0.08, 0.19, -0.03), at(x1, Hh - 0.1, 0.03), "p_dgrey", { bv: 0.006 });
  A.box(at(x0, 1.0, -0.03), at(x1, 1.06, 0.03), "p_dgrey", vb(0.004));
  for (let x = x0 + 0.16; x < x1 - 0.1; x += 0.13) {
    A.box(at(x - 0.0125, 0.19, -0.0125), at(x + 0.0125, Hh + 0.08, 0.0125), "p_dgrey", D);
    A.cone(at(x, Hh + 0.12, 0), [0.03, 0.08, 0.03], "p_dgrey", { ...D, n: 4, top: 0.01 });
  }
  for (const x of [x0 + 0.5, x1 - 0.5]) { A.cyl(at(x, 0.07, 0), [0.13, 0.04, 0.13], "steel", { ...D, seg: 14, r: [90, 0, 0] }); A.box(at(x - 0.08, 0.07, -0.05), at(x + 0.08, 0.12, 0.05), "steel_dk", D); }
  bar(A, at(x0, 0.19, -0.03), at(x1, Hh - 0.1, -0.03), 0.02, 0.02, "p_dgrey", D);
  A.box(at(x0 - 0.02, 0.2, -0.05), at(x0, 1.9, 0.05), "pl_black", D);                    // safety edge
  A.box(at(x0, 0, -0.06), at(x1, Hh, 0.06), "p_dgrey", H);
  // posts: receiver at -x, guide pair at +x
  A.box(at(-3.35, 0, -0.1), at(-3.15, 2.3, 0.1), "p_dgrey", { bv: 0.01 });
  A.box(at(-3.16, 0.3, -0.07), at(-3.12, 1.9, 0.07), "pl_black", D);
  for (const z of [-0.12, 0.12]) A.box(at(2.95, 0, z - 0.06), at(3.07, 2.35, z + 0.06), "p_dgrey", { bv: 0.008 });
  for (const z of [-0.06, 0.06]) A.cyl(at(3.01, Hh - 0.05, z), [0.06, 0.08, 0.06], "pl_white", { ...D, seg: 10 });
  // operator
  A.box(at(3.35, 0, -0.65), at(4.15, 0.1, -0.15), "conc", vb(0.01));
  A.rbox(at(3.45, 0.1, -0.55), at(3.9, 0.55, -0.22), 0.03, "pl_grey");
  A.box(at(3.5, 0.3, -0.221), at(3.75, 0.45, -0.215), "pl_black", D);
  A.cyl(at(3.65, 0.6, -0.4), [0.12, 0.1, 0.12], "lamp_a", { ...D, seg: 12 });
  return fin(A, { cover: boxCover(3.1, 0.06, 2.1, { step: 2.2, ends: false, cx: 0.1 + ox }) });
}
function razorCoil() {
  const A = mk("mod_razor_coil", "Razor wire concertina (5 m)", "Fences",
    "A 0.9 m concertina coil of galvanised razor wire stretched 5 m along the ground, clipped at intervals and held by three U-section steel pickets with tie wires.", [5.2, 1.0]);
  concertina(A, -2.5, 2.5, 0.45, 0, 0.9, 0.3, 10);
  for (const x of [-2.2, 0, 2.2]) {
    A.box([x - 0.02, 0, 0.42], [x + 0.02, 1.0, 0.46], "rust", vb(0.004));
    A.box([x - 0.02, 0, -0.46], [x + 0.02, 1.0, -0.42], "rust", vb(0.004));
    rod(A, [x, 0.9, -0.44], [x, 0.9, 0.44], 0.004, "wire", { seg: 4 });
  }
  A.box([-2.5, 0, -0.45], [2.5, 0.9, 0.45], "wire", H);
  return fin(A);
}
function constructionFence() {
  const A = mk("mod_construction_fence", "Temporary construction fence panel", "Fences",
    "Temporary mesh fence panel, 3.5 x 2.0 m: galvanised tube frame with welded 100 x 250 mm mesh, standing in two moulded rubber-and-concrete feet, with a clamp coupler at each top corner and a brace stay.", [4.2, 1.6]);
  const W = 3.5, Hh = 2.0, x0 = -W / 2, x1 = W / 2;
  for (const x of [x0 + 0.02, x1 - 0.02]) rod(A, [x, 0.08, 0], [x, Hh, 0], 0.04, "galv", { k: 0, seg: 10 });
  for (const y of [0.1, Hh - 0.02]) rod(A, [x0, y, 0], [x1, y, 0], 0.04, "galv", { k: 0, seg: 10 });
  meshGrid(A, x0 + 0.04, x1 - 0.04, 0.12, Hh - 0.04, (u, v) => [u, v, 0], 0.1, 0.25, "wire", 0.004);
  for (const x of [x0 + 0.02, x1 - 0.02]) {
    A.rbox([x - 0.33, 0, -0.1], [x + 0.33, 0.14, 0.1], 0.03, "conc_r");
    for (const dx of [-0.12, 0.12]) A.cyl([x + dx, 0.142, 0], [0.05, 0.004, 0.05], "rubber", { ...D, seg: 10 });
    A.box([x - 0.05, Hh - 0.12, -0.035], [x + 0.05, Hh - 0.05, 0.035], "steel", dv(0.005));
    boltRow(A, [x, Hh - 0.085, 0.036], [x, Hh - 0.085, 0.036], 1, "z");
  }
  rod(A, [x1 - 0.1, 1.2, 0], [x1 - 0.4, 0.05, -0.6], 0.03, "galv", { seg: 8 });   // stay
  A.rbox([x1 - 0.65, 0, -0.75], [x1 - 0.15, 0.12, -0.5], 0.02, "conc_r");
  A.box([x0, 0, -0.03], [x1, Hh, 0.03], "wire", H);
  return fin(A);
}
function palisade() {
  const A = mk("mod_palisade_fence", "Steel palisade fence panel", "Fences",
    "Steel palisade security fence bay, 2.75 m: W-section pales 2.4 m tall with triple-pointed heads bolted to two angle rails between I-section posts, finished in green paint.", [2.9, 0.25]);
  const W = 2.75, Hh = 2.4;
  for (const x of [-W / 2, W / 2]) {
    A.box([x - 0.05, 0, -0.04], [x + 0.05, Hh + 0.05, -0.034], "p_green", vb(0.003));
    A.box([x - 0.05, 0, 0.034], [x + 0.05, Hh + 0.05, 0.04], "p_green", vb(0.003));
    A.box([x - 0.004, 0, -0.04], [x + 0.004, Hh + 0.05, 0.04], "p_green", vb(0.002));
    A.box([x - 0.06, Hh + 0.05, -0.05], [x + 0.06, Hh + 0.06, 0.05], "p_green", D);
  }
  for (const y of [0.35, Hh - 0.3]) {
    A.box([-W / 2, y, 0.04], [W / 2, y + 0.05, 0.045], "p_green", vb(0.002));
    A.box([-W / 2, y + 0.045, 0.04], [W / 2, y + 0.05, 0.09], "p_green", vb(0.002));
  }
  for (let x = -W / 2 + 0.12; x < W / 2 - 0.06; x += 0.155) {
    A.box([x - 0.012, 0.03, 0.092], [x + 0.012, Hh, 0.1], "p_green", D);
    for (const s of [-1, 1]) A.obox([x + s * 0.025, Hh / 2 + 0.015, 0.11], [0.006, Hh - 0.03, 0.035], [0, s * 35, 0], "p_green", D);
    for (const s of [-1, 0, 1]) A.cone([x + s * 0.025, Hh + 0.04, 0.1 + Math.abs(s) * 0.01], [0.022, 0.08, 0.012], "p_green", { ...D, n: 4, top: 0.05 });
    for (const y of [0.375, Hh - 0.275]) boltRow(A, [x, y, 0.101], [x, y, 0.101], 1, "z", "steel");
  }
  A.box([-W / 2, 0, 0], [W / 2, Hh, 0.12], "p_green", H);
  return fin(A, { cover: [cover([0, 0, -0.6], 0, "high", ["left", "right"]), cover([0, 0, 0.7], 180, "high", ["left", "right"])] });
}

// ================================================================================================= ROAD OBSTACLES
function cone(tall) {
  const h = tall ? 0.71 : 0.45, b = tall ? 0.38 : 0.28, d = tall ? 0.3 : 0.22;
  const A = mk(tall ? "mod_traffic_cone" : "mod_traffic_cone_small", tall ? "Traffic cone (710 mm)" : "Traffic cone (450 mm)", "Road",
    `Fluorescent orange PVC traffic cone, ${Math.round(h * 1000)} mm, on a square black rubber base with ${tall ? "two" : "one"} retro-reflective collar${tall ? "s" : ""}.`, [b + 0.02, b + 0.02]);
  A.rbox([-b / 2, 0, -b / 2], [b / 2, 0.035, b / 2], 0.02, "rubber");
  A.cone([0, 0.035 + (h - 0.035) / 2, 0], [d, h - 0.035, d], "pl_orange", { seg: 20, top: 0.17 });
  const bands = tall ? [[0.46, 0.13], [0.3, 0.07]] : [[0.27, 0.08]];
  for (const [y, bh] of bands) { const t = (y - 0.035) / (h - 0.035), dd = d * (1 - t * 0.83) + 0.004; A.cone([0, y, 0], [dd, bh, dd], "refl_w", { ...D, seg: 20, top: 1 - (bh / (h - 0.035)) * 0.83 / (1 - t * 0.83) }); }
  return fin(A, { effect: "none" });
}
function drum() {
  const A = mk("mod_traffic_drum", "Traffic channelizer drum", "Road",
    "Orange polyethylene channelizer drum, 0.9 m tall, with four retro-reflective bands, a moulded carry handle and a black recycled-rubber ballast ring.", [0.8, 0.8]);
  A.cyl([0, 0.04, 0], [0.76, 0.08, 0.76], "rubber", { seg: 24 });
  A.cone([0, 0.5, 0], [0.58, 0.84, 0.58], "pl_orange", { seg: 24, top: 0.88 });
  for (let i = 0; i < 4; i++) { const y = 0.28 + i * 0.16, t = (y - 0.08) / 0.84, dd = 0.58 * (1 - 0.12 * t) + 0.006; A.cyl([0, y, 0], [dd, 0.1, dd], i % 2 ? "refl_w" : "pl_orange", { ...D, seg: 24 }); }
  for (let i = 0; i < 3; i++) { const y = 0.22 + i * 0.32; A.cyl([0, y, 0], [0.585 - i * 0.02, 0.02, 0.585 - i * 0.02], "pl_orange", { ...D, seg: 24 }); }
  A.cyl([0, 0.93, 0], [0.42, 0.03, 0.42], "pl_orange", { ...D, seg: 20 });
  A.box([-0.1, 0.94, -0.02], [0.1, 1.0, 0.02], "pl_orange", dv(0.01));
  return fin(A, { effect: "carve" });
}
function sawhorse() {
  const A = mk("mod_sawhorse_barricade", "Sawhorse barricade", "Road",
    "Folding police / works sawhorse barricade, 2.4 m: two hinged timber A-frame legs carrying a top rail and a lower rail with alternating orange and white reflective sheeting, a steel hinge bracket at each end.", [2.5, 0.75]);
  for (const x of [-1.05, 1.05]) {
    for (const s of [-1, 1]) bar(A, [x, 0.0, s * 0.33], [x, 1.02, 0], 0.07, 0.035, "wood_raw", { bv: 0.004 });
    A.box([x - 0.07, 0.92, -0.06], [x + 0.07, 1.06, 0.06], "steel", dv(0.004));
    rod(A, [x - 0.04, 0.35, -0.22], [x - 0.04, 0.35, 0.22], 0.012, "steel", { seg: 6 });
  }
  for (const [y, hh] of [[0.92, 0.2], [0.45, 0.15]]) {
    A.box([-1.2, y - hh / 2, 0.035], [1.2, y + hh / 2, 0.06], "wood_raw", { bv: 0.004 });
    for (let i = 0; i < 8; i++) A.box([-1.2 + i * 0.3 + 0.005, y - hh / 2 + 0.01, 0.06], [-1.2 + (i + 1) * 0.3 - 0.005, y + hh / 2 - 0.01, 0.063], i % 2 ? "refl_w" : "pl_orange", D);
  }
  return fin(A, { cover: [] });
}
// water-filled barrier body along x, length L, at (cx, cz)
function waterBarrierBody(A, cx, cz, L, m) {
  const P = [[-0.25, 0], [0.25, 0], [0.25, 0.1], [0.14, 0.32], [0.11, 0.78], [0.07, 0.82], [-0.07, 0.82], [-0.11, 0.78], [-0.14, 0.32], [-0.25, 0.1]].map(([z, y]) => [z + cz, y]);
  extrudeX(A, cx - L / 2 + 0.08, cx + L / 2 - 0.08, P, m);
  for (const s of [-1, 1]) for (let i = 0; i < 3; i++) { const x = cx - L / 2 + 0.35 + i * (L - 0.7) / 2; A.box([x - 0.22, 0.38, cz + s * 0.135 - 0.006], [x + 0.22, 0.7, cz + s * 0.135 + 0.006], m, dv(0.02)); }
  for (const x of [cx - L / 2 + 0.5, cx + L / 2 - 0.5]) A.box([x - 0.16, 0, cz - 0.252], [x + 0.16, 0.08, cz + 0.252], "pl_black", D);
  A.cyl([cx, 0.83, cz], [0.13, 0.04, 0.13], m, { ...D, seg: 14 });
  A.cyl([cx, 0.855, cz], [0.1, 0.01, 0.1], "pl_white", { ...D, seg: 14 });
  A.cyl([cx + L / 2 - 0.3, 0.06, cz + 0.25], [0.06, 0.03, 0.06], "pl_black", { ...D, seg: 10, r: [90, 0, 0] });   // drain plug
  for (const s of [-1, 1]) A.box([cx - L / 2 + 0.2, 0.74, cz + s * 0.115 - 0.004], [cx + L / 2 - 0.2, 0.77, cz + s * 0.115 + 0.004], "refl_w", D);
  // interlocking knuckles and a steel pin at each end
  for (const e of [-1, 1]) for (const y of e < 0 ? [0.12, 0.52] : [0.32, 0.72]) A.cyl([cx + e * (L / 2 - 0.06), y, cz], [0.14, 0.18, 0.14], m, { k: 0, seg: 14 });
  rod(A, [cx + L / 2 - 0.06, 0.1, cz], [cx + L / 2 - 0.06, 0.88, cz], 0.025, "steel", { seg: 8 });
  A.box([cx - L / 2, 0, cz - 0.25], [cx + L / 2, 0.82, cz + 0.25], m, H);
}
function waterBarrier(colour) {
  const m = colour === "red" ? "pl_red" : "pl_white";
  const A = mk(`mod_water_barrier_${colour}`, `Water-filled barrier (${colour})`, "Road",
    `Roto-moulded ${colour} polyethylene water-filled road barrier, 2.0 x 0.5 x 0.82 m, with recessed side panels, fork slots, a fill cap, drain plug, reflective strip and interlocking end knuckles with a steel pin.`, [2.05, 0.55]);
  waterBarrierBody(A, 0, 0, 2.0, m);
  return fin(A, { cover: boxCover(1, 0.25, 0.82, { ends: false }) });
}
function spikeStrip(A, x0, x1, z) {
  for (let x = x0; x < x1 - 0.01; x += 0.25) {
    A.obox([x + 0.125, 0.025, z], [0.24, 0.04, 0.09], [0, (Math.round(x * 4) % 2 ? 8 : -8), 0], "pl_black", { k: 0, bv: 0.008 });
    for (const dz of [-0.025, 0.025]) for (const dx of [0.06, 0.18]) A.cone([x + dx, 0.07, z + dz], [0.012, 0.05, 0.012], "steel", { ...D, seg: 6 });
  }
  A.cyl([x1 + 0.08, 0.03, z], [0.14, 0.05, 0.14], "pl_yellow", { ...D, seg: 12 });
}
function roadblock() {
  const A = mk("mod_roadblock", "Roadblock (barriers and spike strip)", "Road",
    "Police / military vehicle checkpoint roadblock across a 7 m carriageway: two staggered concrete jersey barriers forming a chicane, a deployed tyre-deflation spike strip with its reel, four cones and a stop-paddle stand with a blank red disc.", [8.2, 6.2]);
  const R = rng(3333);
  for (const [cx, cz] of [[-2.4, -1.4], [2.4, 1.4]]) {
    extrudeX(A, cx - 1.5, cx + 1.5, JERSEY.map(([z, y]) => [z + cz, y]), "conc_l");
    A.box([cx - 1.49, 0.8, cz - 0.078], [cx + 1.49, 0.815, cz + 0.078], "conc_l", dv(0.012));
    for (const x of [cx - 1.05, cx + 1.05]) A.box([x - 0.16, 0, cz - 0.307], [x + 0.16, 0.07, cz + 0.307], "conc_dk", D);
    for (let i = 0; i < 3; i++) { const x = cx - 1.3 + R() * 2.6; A.obox([x, 0.79, cz + 0.07], [0.1, 0.035, 0.03], [0, 0, 20], "conc_dk", D); }
    for (const x of [cx - 1.2, cx, cx + 1.2]) A.box([x - 0.08, 0.7, cz + 0.087], [x + 0.08, 0.76, cz + 0.092], "refl_w", D);
  }
  spikeStrip(A, -3.6, 3.4, -2.8);
  for (const [x, z] of [[-0.6, -2.3], [0.6, -2.3], [-3.6, 0.2], [3.6, -0.2]]) {
    A.rbox([x - 0.19, 0, z - 0.19], [x + 0.19, 0.035, z + 0.19], 0.02, "rubber");
    A.cone([x, 0.37, z], [0.3, 0.675, 0.3], "pl_orange", { k: 0, seg: 16, top: 0.17 });
    A.cone([x, 0.46, z], [0.215, 0.13, 0.215], "refl_w", { ...D, seg: 16, top: 0.86 });
  }
  A.cyl([0, 0.04, 2.6], [0.45, 0.08, 0.45], "rubber", { seg: 16 });
  rod(A, [0, 0.08, 2.6], [0, 1.6, 2.6], 0.05, "galv", { k: 1, seg: 10 });
  A.cyl([0, 1.85, 2.6], [0.5, 0.02, 0.5], "refl_r", { k: 0, seg: 24, r: [90, 0, 0] });
  A.cyl([0, 1.85, 2.588], [0.42, 0.004, 0.42], "pl_white", { ...D, seg: 24, r: [90, 0, 0] });
  A.cyl([0, 1.85, 2.586], [0.4, 0.004, 0.4], "refl_r", { ...D, seg: 24, r: [90, 0, 0] });
  return fin(A, { cover: boxCover(1.5, 0.3, 0.81, { cx: -2.4, cz: -1.4 }).concat(boxCover(1.5, 0.3, 0.81, { cx: 2.4, cz: 1.4 })) });
}
function tyrePile() {
  const A = mk("mod_tyre_pile", "Pile of scrap tyres", "Road",
    "Heap of scrap car and light-truck tyres, about 2.4 x 1.8 m: three stacked columns and several tyres leaning against and lying around them, used as a barricade or cover.", [2.6, 2.0]);
  const R = rng(9087);
  const flat = (x, y, z, d = 0.64, w = 0.2) => {
    A.cyl([x, y + w / 2, z], [d, w, d], "rubber", { k: 0, seg: 20 });
    A.cyl([x, y + w + 0.001, z], [d * 0.58, 0.004, d * 0.58], "seam", { ...D, seg: 16 });
    A.cyl([x, y + w / 2, z], [d + 0.006, w * 0.5, d + 0.006], "rubber_side", { ...D, seg: 20 });
  };
  const cols = [[-0.7, -0.3, 5], [0.05, -0.35, 4], [0.75, -0.25, 3]];
  for (const [x, z, n] of cols) for (let i = 0; i < n; i++) flat(x + (R() - 0.5) * 0.08, i * 0.2, z + (R() - 0.5) * 0.08, 0.62 + R() * 0.08);
  for (const [x, z, yaw, lean] of [[-0.6, 0.4, 10, 15], [0.25, 0.45, -5, 20], [0.95, 0.5, 25, 70], [-1.05, 0.6, 80, 85]]) {
    const d = 0.66, c = [x, Math.cos(lean / DEG) * d / 2 + 0.1 * Math.sin(lean / DEG), z];
    A.cyl(c, [d, 0.2, d], "rubber", { k: 0, seg: 20, r: [90 - lean, yaw, 0] });
    A.cyl(c, [d * 0.58, 0.205, d * 0.58], "seam", { ...D, seg: 16, r: [90 - lean, yaw, 0] });
  }
  A.box([-1.05, 0, -0.65], [1.1, 0.95, 0.1], "rubber", H);
  return fin(A, { cover: [cover([-0.4, 0, -1.2], 0, "low", ["over", "left"]), cover([0.3, 0, 1.05], 180, "low", ["over", "right"])] });
}
function speedBump() {
  const A = mk("mod_speed_bump", "Rubber speed bump (3 modules)", "Road",
    "Bolt-down recycled-rubber speed bump, 1.8 m of three black and yellow modules 50 mm high, with rounded end caps and recessed anchor bolts.", [2.1, 0.38]);
  const P = [[-0.175, 0], [0.175, 0], [0.09, 0.05], [-0.09, 0.05]];
  for (let i = 0; i < 3; i++) {
    const x0 = -0.9 + i * 0.6;
    extrudeX(A, x0 + 0.003, x0 + 0.597, P, i % 2 ? "pl_yellow" : "rubber");
    for (const dx of [0.15, 0.45]) A.cyl([x0 + dx, 0.051, 0], [0.04, 0.004, 0.04], "steel_dk", { ...D, seg: 8 });
    for (let k = 0; k < 4; k++) A.box([x0 + 0.06 + k * 0.14, 0.05, -0.06], [x0 + 0.1 + k * 0.14, 0.054, 0.06], i % 2 ? "pl_yellow" : "rubber", D);
  }
  for (const s of [-1, 1]) A.prism(2, -0.15, 0.15, s > 0 ? [[0.9, 0], [1.05, 0], [0.9, 0.05]] : [[-1.05, 0], [-0.9, 0], [-0.9, 0.05]], "pl_yellow");
  return fin(A, { effect: "none" });
}

// ================================================================================================= VEHICLES
// move parts [from..] by dy (boxes / cylinders / cones / arches / wedges by centre, prisms by their y coordinates)
function shiftY(A, from, dy) {
  const r4 = (x) => Math.round(x * 10000) / 10000;
  for (let i = from; i < A.parts.length; i++) {
    const p = A.parts[i];
    if (p.t === "p") { if (p.axis === 1) p.a = p.a.map((v) => r4(v + dy)); else p.tri = p.tri.map(([u, w]) => [u, r4(w + dy)]); }
    else p.c[1] = r4(p.c[1] + dy);
  }
}
// generic passenger car / light vehicle body. v: L, W, Dw (wheel dia), tw, wf / wr (axle z), clear, belt, roof, hood
// (nose top), deck (rear top), zA (windscreen base), zB (roof front), zC (roof rear), zD (rear glass base), paint;
// optional pickup (zCab), van (zBp), pillars, doors, seatsF / seatR, roofRails, state "wreck" | "burn"
function carBody(A, v) {
  const burn = v.state === "burn", wreck = v.state === "wreck", R = rng(v.seed || 5);
  const { L, W, Dw, tw, wf, wr, clear, belt, roof, hood, deck, zA, zB, zC, zD } = v;
  const zN = L / 2, zT = -L / 2, hx = W / 2, cx = (W - (v.tumble ?? 0.14)) / 2;
  const paint = burn ? "burnt" : v.paint, trimM = burn ? "burnt_dk" : "trim", bump = burn ? "burnt_dk" : v.bumper || paint;
  const glassM = (pane) => (burn || (wreck && (v.broken || []).includes(pane)) ? null : wreck && (v.cracked || []).includes(pane) ? "glass_crack" : pane === "rear" && v.tint ? "glass_t" : "glass");
  const lampM = (m, side) => (burn ? "burnt_dk" : wreck && side === 1 && m === "lamp_w" ? "seam" : m);
  const gap = Dw + 0.08, yC = Dw / 2, yW = Dw + 0.06, start = A.parts.length;
  // lower body under the wheel tops, cut round the wheels; arch spandrels, flares and dark well liners
  const seg = (z0, z1) => { if (z1 - z0 > 0.02) A.box([-hx, clear, z0], [hx, yW, z1], paint, vb(0.03)); };
  seg(wf + gap / 2, zN); seg(wr + gap / 2, wf - gap / 2); seg(zT, wr - gap / 2);
  for (const wz of [wf, wr]) {
    A.arch([0, yC + (yW - yC) / 2, wz], [gap, yW - yC, W], paint, { k: 0, fill: 1, th: 0.02, r: [0, 90, 0] });
    A.arch([0, yC + (yW - yC + 0.03) / 2, wz], [gap + 0.06, yW - yC + 0.03, W + 0.02], trimM, { k: 0, lod: 1, th: 0.035, r: [0, 90, 0] });
    A.box([-hx + tw + 0.06, yC - 0.05, wz - gap / 2], [hx - tw - 0.06, yW - 0.02, wz + gap / 2], "seam", V);
  }
  // upper lower-body: side profile from the nose over the bonnet and the waist to the tail
  const prof = v.pickup ? [[v.zCab, yW], [zN, yW], [zN, hood - 0.1], [zN - 0.15, hood], [zA, belt], [v.zCab, belt]]
    : [[zT, yW], [zN, yW], [zN, hood - 0.1], [zN - 0.15, hood], [zA, belt], [zT + 0.1, deck], [zT, deck - 0.12]];
  extrudeX(A, -hx, hx, prof, paint, V);
  // greenhouse: glass, roof, pillars
  const ws = glassM("ws");
  if (ws) bar(A, [0, belt + 0.01, zA - 0.02], [0, roof - 0.04, zB], cx * 2 - 0.08, 0.012, ws, V);
  const rg = glassM("rear");
  if (rg && !v.van) bar(A, [0, roof - 0.04, zC], [0, belt + 0.01, zD], cx * 2 - 0.1, 0.012, rg, V);
  for (const s of [-1, 1]) {
    const g = glassM(s > 0 ? "left" : "right"), zEnd = v.van ? v.zBp : zD, zTop = v.van ? v.zBp : zC;
    if (g) extrudeX(A, s * cx - 0.004, s * cx + 0.004, [[zEnd, belt], [zA, belt], [zB, roof - 0.05], [zTop, roof - 0.05]], g, V);
    bar(A, [s * (cx - 0.03), belt, zA - 0.02], [s * (cx - 0.03), roof - 0.03, zB + 0.02], 0.075, 0.07, paint, V);       // A pillar
    if (!v.van) {
      bar(A, [s * (cx - 0.03), roof - 0.03, zC], [s * (cx - 0.03), belt, zD], 0.075, 0.09, paint, V);                     // C pillar
      if (v.cWide) extrudeX(A, s * cx - 0.012, s * cx + 0.012, [[zD, belt], [zD + v.cWide, belt], [zC + 0.02, roof - 0.05], [zC, roof - 0.05]], paint, V);
    } else {
      A.box([s * cx - 0.02, belt, zD], [s * cx + 0.02, roof - 0.04, v.zBp], paint, V);                                   // van load-box side
    }
    for (const z of v.pillars || []) A.box([s * cx - 0.03, belt, z - 0.05], [s * cx + 0.03, roof - 0.04, z + 0.05], v.van ? paint : trimM, V);
    A.box([s * cx - 0.016, belt - 0.01, Math.max(zD, zT + 0.05)], [s * cx + 0.016, belt + 0.025, zA], burn ? "burnt_dk" : "chrome", D);   // waist trim
    A.box([s * cx - 0.02, roof - 0.07, zC], [s * cx + 0.02, roof - 0.04, zB], trimM, D);                                   // drip rail
    if (v.roofRails) {
      for (const z of [zB - 0.1, zC + 0.1]) A.box([s * (cx - 0.12) - 0.02, roof, z - 0.04], [s * (cx - 0.12) + 0.02, roof + 0.07, z + 0.04], trimM, D);
      A.box([s * (cx - 0.12) - 0.018, roof + 0.05, zC + 0.05], [s * (cx - 0.12) + 0.018, roof + 0.08, zB - 0.05], burn ? "burnt_dk" : "chrome", D);
    }
  }
  if (v.van) { A.box([-cx, belt, zT + 0.02], [cx, roof - 0.04, zT + 0.08], paint, V); A.box([-cx, roof - 0.06, zT + 0.02], [cx, roof, zB], paint, { k: 0, bv: 0.03 }); }
  else A.rbox([-cx, roof - 0.06, zC], [cx, roof, zB], 0.03, paint, V);
  // interior seen through the glass
  const seatM = burn ? "burnt_dk" : v.seat || "seat", iy = belt - 0.42;
  A.box([-cx + 0.06, belt - 0.25, zA - 0.5], [cx - 0.06, belt + 0.06, zA - 0.08], burn ? "burnt_dk" : "dash", V);
  if (!burn) A.box([-cx + 0.2, belt + 0.06, zA - 0.3], [cx - 0.2, belt + 0.065, zA - 0.12], "screen_off", D);
  A.cyl([0.37, belt + 0.07, zA - 0.56], [0.38, 0.03, 0.38], burn ? "burnt_dk" : "rubber", { ...D, seg: 16, r: [-62, 0, 0] });
  A.cyl([0.37, belt + 0.07, zA - 0.56], [0.12, 0.05, 0.12], trimM, { ...D, seg: 10, r: [-62, 0, 0] });
  for (const x of [-0.37, 0.37]) seat(A, x, iy, v.seatsF, 0.5, seatM);
  if (v.seatR != null) seat(A, 0, iy, v.seatR, cx * 2 - 0.22, seatM, { head: true });
  if (v.van) A.box([-cx + 0.02, belt, v.seatsF - 0.75], [cx - 0.02, roof - 0.1, v.seatsF - 0.72], "p_grey", V);           // bulkhead
  // front end: bumper, plate, grille, lamps; rear end; mirrors; seams and handles
  A.rbox([-hx + 0.01, clear + 0.04, zN - 0.12], [hx - 0.01, clear + 0.34, zN + 0.05], 0.05, bump, V);
  A.box([-hx + 0.3, clear + 0.08, zN + 0.05], [hx - 0.3, clear + 0.17, zN + 0.056], trimM, D);
  if (!burn) A.box([-0.26, clear + 0.18, zN + 0.05], [0.26, clear + 0.3, zN + 0.058], "plate", D);
  A.box([-hx + 0.42, hood - 0.27, zN - 0.02], [hx - 0.42, hood - 0.11, zN + 0.006], trimM, D);
  for (let i = 0; i < 3; i++) A.box([-hx + 0.44, hood - 0.25 + i * 0.05, zN + 0.006], [hx - 0.44, hood - 0.235 + i * 0.05, zN + 0.014], burn ? "burnt_dk" : "chrome", D);
  for (const s of [-1, 1]) {
    A.box([s > 0 ? hx - 0.42 : -hx + 0.1, hood - 0.24, zN - 0.04], [s > 0 ? hx - 0.1 : -hx + 0.42, hood - 0.11, zN + 0.008], lampM("lamp_w", s), D);
    A.box([s * hx - 0.006, hood - 0.24, zN - 0.28], [s * hx + 0.006, hood - 0.13, zN - 0.04], lampM("lamp_a", s), D);
    A.cyl([s * (hx - 0.25), clear + 0.12, zN + 0.052], [0.08, 0.01, 0.08], lampM("lamp_w", 0), { ...D, seg: 10, r: [90, 0, 0] });
    A.box([s > 0 ? hx - 0.36 : -hx + 0.02, deck - 0.28, zT - 0.008], [s > 0 ? hx - 0.02 : -hx + 0.36, deck - 0.14, zT + 0.04], lampM("lamp_r", 0), D);
    A.box([s > 0 ? hx - 0.36 : -hx + 0.26, deck - 0.24, zT - 0.012], [s > 0 ? hx - 0.26 : -hx + 0.36, deck - 0.18, zT], burn ? "burnt_dk" : "lamp_w", D);
    A.box([s * hx - 0.006, deck - 0.28, zT + 0.03], [s * hx + 0.006, deck - 0.14, zT + 0.22], lampM("lamp_r", 0), D);
    // mirror on the door
    A.box([s > 0 ? hx : -hx - 0.05, belt + 0.02, zA - 0.14], [s > 0 ? hx + 0.05 : -hx, belt + 0.06, zA - 0.06], trimM, D);
    A.rbox([s > 0 ? hx + 0.04 : -hx - 0.2, belt + 0.03, zA - 0.17], [s > 0 ? hx + 0.2 : -hx - 0.04, belt + 0.16, zA - 0.06], 0.025, paint, V);
    if (!burn) A.box([s > 0 ? hx + 0.05 : -hx - 0.19, belt + 0.045, zA - 0.172], [s > 0 ? hx + 0.19 : -hx - 0.05, belt + 0.145, zA - 0.168], "glass_t", D);
    const dz = v.doors || [];
    for (const z of dz) seam(A, [s * hx - 0.003, clear + 0.08, z - 0.004], [s * hx + 0.003, belt, z + 0.004]);
    for (let i = 1; i < dz.length; i++) A.box([s * hx - 0.004, belt - 0.11, dz[i] + 0.06], [s * hx + 0.014, belt - 0.08, dz[i] + 0.2], burn ? "burnt_dk" : "chrome", D);
    A.box([s * hx - 0.004, clear, wr + gap / 2 + 0.05], [s * hx + 0.012, clear + 0.09, wf - gap / 2 - 0.05], trimM, D);       // sill
  }
  A.rbox([-hx + 0.01, clear + 0.04, zT - 0.05], [hx - 0.01, clear + 0.32, zT + 0.1], 0.05, bump, V);
  if (!burn) A.box([-0.26, deck - 0.4, zT - 0.006], [0.26, deck - 0.3, zT], "plate", D);
  for (const s of [-1, 1]) A.box([s * (hx - 0.3) - 0.05, clear + 0.14, zT - 0.052], [s * (hx - 0.3) + 0.05, clear + 0.18, zT - 0.048], "refl_r", D);
  rod(A, [0.45, clear + 0.02, zT + 0.6], [0.45, clear + 0.02, zT - 0.06], 0.06, burn ? "burnt_dk" : "steel", { seg: 8 });   // exhaust
  A.box([-hx + 0.25, clear - 0.04, wr], [hx - 0.25, clear + 0.01, wf], "seam", V);                                         // underbody
  if (!v.van && ws) for (const x of [-0.3, 0.32]) bar(A, [x - 0.3, belt + 0.03, zA], [x + 0.25, belt + 0.03, zA - 0.08], 0.014, 0.01, "trim", D);   // wipers
  // pickup bed
  if (v.pickup) {
    const z0 = zT, z1 = v.zCab - 0.04, yt = belt - 0.05;
    A.box([-hx + 0.05, yW - 0.02, z0 + 0.04], [hx - 0.05, yW + 0.02, z1], burn ? "burnt_dk" : "trim", V);
    for (const s of [-1, 1]) A.box([s > 0 ? hx - 0.05 : -hx, yW, z0], [s > 0 ? hx : -hx + 0.05, yt, z1], paint, { k: 0, bv: 0.01 });
    for (const s of [-1, 1]) A.box([s > 0 ? hx - 0.07 : -hx, yt, z0], [s > 0 ? hx : -hx + 0.07, yt + 0.03, z1], trimM, D);
    A.box([-hx + 0.05, yW, z1 - 0.05], [hx - 0.05, yt, z1], paint, V);
    A.box([-hx + 0.05, yW, z0], [hx - 0.05, yt, z0 + 0.05], paint, { k: 0, bv: 0.01 });                                 // tailgate
    A.box([-0.15, yt - 0.12, z0 - 0.006], [0.15, yt - 0.07, z0], trimM, D);
    for (const s of [-1, 1]) A.box([s > 0 ? 0.55 : -hx + 0.05, yW, wr - 0.48], [s > 0 ? hx - 0.05 : -0.55, yW + 0.28, wr + 0.48], paint, { k: 0, bv: 0.04 });   // wheel tubs
    for (let x = -0.7; x <= 0.71; x += 0.35) A.box([x - 0.03, yW + 0.02, z0 + 0.06], [x + 0.03, yW + 0.035, z1 - 0.06], trimM, D);   // bed ribs
    A.box([-hx, yW, z0], [hx, yt, z0 + 0.06], paint, H);
    for (const s of [-1, 1]) A.box([s > 0 ? hx - 0.06 : -hx, yW, z0], [s > 0 ? hx : -hx + 0.06, yt, z1], paint, H);
    A.box([-hx, clear, z0], [hx, yW + 0.02, z1], paint, H);
    A.box([-hx, clear, v.zCab], [hx, belt, zN], paint, H);
  } else A.box([-hx, clear, zT], [hx, belt, zN], paint, H);
  A.box([-cx, belt, Math.min(zC, zD)], [cx, roof, zA], paint, H);
  // wreck extras: popped bonnet, hanging bumper, rust and grime
  if (wreck) {
    bar(A, [0, belt + 0.03, zA + 0.05], [0, hood + 0.16, zN - 0.2], W - 0.1, 0.03, paint, V);
    A.obox([-0.2, clear + 0.12, zN - 0.02], [W - 0.2, 0.26, 0.15], [0, 6, -9], bump, V);
    for (let i = 0; i < 6; i++) { const s = R() > 0.5 ? 1 : -1; A.box([s * hx - 0.004, clear + 0.05 + R() * 0.3, -1.5 + R() * 3], [s * hx + 0.004, clear + 0.25 + R() * 0.3, -1.3 + R() * 3], "rust", D); }
  }
  // wheels (a burned car sits down on its rims)
  const wx = hx - tw / 2 - 0.03, ws4 = [[wx, wf, 1], [-wx, wf, -1], [wx, wr, 1], [-wx, wr, -1]];
  ws4.forEach(([x, z, s], i) => wheel(A, [x, yC, z], Dw, tw, s, { tyre: burn ? null : "rubber", rim: burn ? "burnt_dk" : v.rim || "rim", flat: wreck && (v.flat || []).includes(i) }));
  if (burn) {
    shiftY(A, start, -(Dw * (1 - 0.62)) / 2);
    A.cyl([0, 0.004, 0], [W + 1.6, 0.008, L + 1.2], "soot", { ...D, seg: 20 });
    for (let i = 0; i < 8; i++) A.obox([(R() - 0.5) * W * 1.4, 0.01, (R() - 0.5) * L], [0.1 + R() * 0.15, 0.015, 0.05 + R() * 0.1], [0, R() * 180, 0], "burnt_dk", D);
  } else if (wreck) shiftY(A, start, -0.03);
  return A;
}
// cover round a vehicle running along z
function vehicleCover(L, W, h = 1.0) {
  const out = [], ht = h >= 1.35 ? "high" : "low", n = Math.max(1, Math.round((L - 0.6) / 1.6));
  for (let i = 0; i < n; i++) {
    const z = -L / 2 + 0.3 + ((i + 0.5) * (L - 0.6)) / n;
    out.push(cover([W / 2 + 0.55, 0, z], 270, ht, ht === "high" ? ["left", "right"] : ["over"]));
    out.push(cover([-W / 2 - 0.55, 0, z], 90, ht, ht === "high" ? ["left", "right"] : ["over"]));
  }
  out.push(cover([0, 0, L / 2 + 0.55], 180, ht, ["left", "right"]), cover([0, 0, -L / 2 - 0.55], 0, ht, ["left", "right"]));
  return out;
}
const SEDAN = { L: 4.7, W: 1.82, Dw: 0.65, tw: 0.215, wf: 1.38, wr: -1.42, clear: 0.17, belt: 0.96, roof: 1.45, hood: 0.8, deck: 0.97,
  zA: 0.95, zB: 0.2, zC: -0.85, zD: -1.5, pillars: [-0.2], doors: [0.95, -0.2, -1.1], seatsF: 0.15, seatR: -0.55, cWide: 0.25, tint: true };
const HATCH = { L: 4.1, W: 1.76, Dw: 0.63, tw: 0.205, wf: 1.3, wr: -1.27, clear: 0.16, belt: 0.95, roof: 1.48, hood: 0.78, deck: 0.95,
  zA: 0.75, zB: 0.05, zC: -1.6, zD: -1.95, pillars: [-0.35], doors: [0.75, -0.35, -1.25], seatsF: 0.0, seatR: -0.8, cWide: 0.32, tint: true };
const SUV = { L: 4.8, W: 1.95, Dw: 0.76, tw: 0.255, wf: 1.45, wr: -1.4, clear: 0.22, belt: 1.12, roof: 1.8, hood: 1.0, deck: 1.12,
  zA: 1.05, zB: 0.35, zC: -2.05, zD: -2.25, pillars: [-0.25, -1.45], doors: [1.05, -0.25, -1.3], seatsF: 0.25, seatR: -0.55, roofRails: true, bumper: "trim", tint: true, cWide: 0.12 };
const PICKUP = { L: 5.5, W: 2.0, Dw: 0.8, tw: 0.265, wf: 1.8, wr: -1.75, clear: 0.26, belt: 1.22, roof: 1.9, hood: 1.12, deck: 1.22,
  zA: 1.35, zB: 0.7, zC: -0.85, zD: -0.9, zCab: -0.95, pickup: true, pillars: [-0.1], doors: [1.35, -0.1, -0.9], seatsF: 0.55, seatR: -0.3, bumper: "chrome" };
const VAN = { L: 5.9, W: 2.05, Dw: 0.74, tw: 0.235, wf: 2.0, wr: -1.65, clear: 0.2, belt: 1.12, roof: 2.62, hood: 1.08, deck: 1.12,
  zA: 1.75, zB: 0.95, zC: -2.9, zD: -2.95, zBp: 0.2, van: true, tumble: 0.06, pillars: [0.2], doors: [1.75, 0.2, -1.05], seatsF: 0.65, seatR: null, bumper: "trim" };
function car(id, name, desc, spec, paint, state, extra = {}) {
  const s = { ...spec, paint, state, seed: id.length * 31, ...extra };
  const A = mk(id, name, "Vehicles", desc, [s.W + 0.45, s.L + 0.1], { maxGrade: 0.35 });
  carBody(A, s);
  if (spec.van && state !== "burn") {   // van rear doors and sliding-door track
    const zt = -s.L / 2 + (state === "wreck" ? 0 : 0);
    seam(A, [-0.003, s.clear + 0.1, zt - 0.004], [0.003, s.roof - 0.1, zt + 0.004]);
    for (const x of [-0.12, 0.12]) A.box([x - 0.08, 1.15, zt - 0.02], [x + 0.08, 1.18, zt], "trim", D);
    A.box([-s.W / 2 - 0.012, s.roof - 0.65, -1.05], [-s.W / 2 + 0.004, s.roof - 0.62, 0.2], "trim", D);
    for (const x of [-0.5, 0.5]) A.box([x - 0.4, 1.6, zt - 0.006], [x + 0.4, 2.3, zt + 0.004], state === "wreck" && x > 0 ? "glass_crack" : "glass_t", D);
  }
  return fin(A, { cover: vehicleCover(s.L, s.W, spec.van ? 2.4 : 1.0) });
}

// ---- large vehicles: shared pieces
function cabGlass(A, x0, x1, y0, y1, z, m = "glass") { A.box([x0, y0, z - 0.006], [x1, y1, z + 0.006], m, V); }
function cabGlassX(A, x, y0, y1, z0, z1, m = "glass") { A.box([x - 0.006, y0, z0], [x + 0.006, y1, z1], m, V); }
function truckLamps(A, hx, y, z, m = "lamp_w") {
  for (const s of [-1, 1]) {
    A.box([s > 0 ? hx - 0.42 : -hx + 0.08, y, z - 0.03], [s > 0 ? hx - 0.08 : -hx + 0.42, y + 0.16, z + 0.01], m, D);
    A.box([s > 0 ? hx - 0.42 : -hx + 0.08, y - 0.08, z - 0.03], [s > 0 ? hx - 0.28 : -hx + 0.22, y - 0.02, z + 0.01], "lamp_a", D);
  }
}
function bigMirror(A, s, x, y, z) {
  rod(A, [x, y, z], [x + s * 0.3, y + 0.15, z - 0.05], 0.025, "steel", { seg: 6 });
  const mx = x + s * 0.28;
  A.rbox([mx - 0.06, y - 0.2, z - 0.09], [mx + 0.06, y + 0.25, z - 0.02], 0.02, "trim", V);
  A.box([mx - 0.05, y - 0.18, z - 0.095], [mx + 0.05, y + 0.23, z - 0.09], "glass_t", D);
}
const dualWheel = (A, x, y, z, Dw, tw, s, rim, dual) => wheel(A, [x, y, z], Dw, tw, s, { rim, lugs: 10, spokes: 0, rimD: 0.6, dual });

function cityBus() {
  const L = 12, W = 2.55, hx = W / 2, Hh = 3.05, zN = 6, zT = -6, Dw = 1.0, tw = 0.29, wf = 3.3, wr = -2.6, clear = 0.3;
  const A = mk("mod_city_bus", "City bus (12 m, generic)", "Vehicles",
    "Generic 12 m low-floor city bus: a deep raked windscreen and a blank destination display, continuous tinted side glazing over seat rows and yellow stanchions, two glazed double doors on the kerb side, a roof air-conditioning pod and hatches, wheel arches over 1 m wheels (twin rear), mirrors on stalks, bumpers and lamp clusters.", [W + 0.7, L + 0.2], { maxGrade: 0.25 });
  const paint = "car_white", yWin0 = 1.05, yWin1 = 2.55, gap = Dw + 0.1, yW = Dw + 0.08, yC = Dw / 2;
  const doors = [[4.45, 5.75], [-0.2, 1.1]];   // kerb (-x) side door openings (z ranges)
  const cuts = [[wf - gap / 2, wf + gap / 2], [wr - gap / 2, wr + gap / 2]];
  const span = (z0, z1, x0, x1, holes) => {   // skirt below the windows, cut for the wheels and the doors
    const hs = holes.filter(([a, b]) => b > z0 && a < z1).sort((p, q) => p[0] - q[0]);
    let z = z0;
    for (const [a, b] of hs) { if (a > z + 0.01) A.box([x0, clear, z], [x1, yWin0, a], paint, vb(0.02)); z = Math.max(z, b); }
    if (z1 > z + 0.01) A.box([x0, clear, z], [x1, yWin0, z1], paint, vb(0.02));
  };
  span(zT, zN - 0.12, 0.4, hx, cuts);
  span(zT, zN - 0.12, -hx, -0.4, cuts.concat(doors));
  A.box([-0.4, clear, zT], [0.4, 0.36, zN - 0.12], paint, V);
  for (const wz of [wf, wr]) {
    A.box([-hx, yW, wz - gap / 2], [hx, yWin0, wz + gap / 2], paint, V);
    A.arch([0, yC + (yW - yC) / 2, wz], [gap, yW - yC, W], paint, { k: 0, fill: 1, th: 0.03, r: [0, 90, 0] });
    const inset = tw * (wz < 0 ? 2.1 : 1) + 0.06;
    A.box([-hx + inset, yC - 0.1, wz - gap / 2], [hx - inset, yW, wz + gap / 2], "seam", V);
  }
  for (const s of [-1, 1]) {
    cabGlassX(A, s * hx, yWin0 + 0.05, yWin1, zT + 0.4, zN - 0.5, "glass_t");
    for (let z = zT + 0.4; z <= zN - 0.4; z += 1.32) A.box([s * hx - 0.03, yWin0, z - 0.05], [s * hx + 0.03, yWin1, z + 0.05], "trim", V);
    A.box([s * hx - 0.03, yWin0, zT], [s * hx + 0.03, yWin0 + 0.06, zN], "trim", D);
    A.box([s * hx - 0.04, clear + 0.2, zT + 0.2], [s * hx + 0.04, clear + 0.26, zN - 0.2], "trim", D);   // rub strip
  }
  A.box([-hx, yWin1, zT], [hx, Hh - 0.08, zN], paint, { k: 0, bv: 0.04 });
  A.rbox([-hx + 0.02, Hh - 0.12, zT + 0.05], [hx - 0.02, Hh, zN - 0.1], 0.06, paint, V);
  A.rbox([-0.95, Hh, -4.6], [0.95, Hh + 0.32, -1.6], 0.08, "p_grey", V);                                  // HVAC pod
  for (let i = 0; i < 4; i++) A.box([-0.8 + i * 0.42, Hh + 0.32, -4.4], [-0.5 + i * 0.42, Hh + 0.33, -1.8], "trim", D);
  for (const z of [2.0, -0.6]) A.box([-0.4, Hh, z - 0.4], [0.4, Hh + 0.05, z + 0.4], "pl_grey", D);   // roof hatches
  // front: windscreen, destination box, lamps, bumper; rear: engine louvres, lamps
  bar(A, [0, clear + 0.95, zN - 0.02], [0, Hh - 0.42, zN - 0.2], W - 0.12, 0.014, "glass", V);
  A.box([-hx + 0.05, Hh - 0.42, zN - 0.24], [hx - 0.05, Hh - 0.1, zN - 0.12], "trim", V);
  A.box([-hx + 0.25, Hh - 0.36, zN - 0.12], [hx - 0.25, Hh - 0.16, zN - 0.115], "lamp_a", D);
  A.box([-hx, clear, zN - 0.12], [hx, clear + 0.95, zN], paint, { k: 0, bv: 0.05 });
  truckLamps(A, hx, clear + 0.55, zN + 0.005);
  A.rbox([-hx + 0.02, clear - 0.05, zN - 0.05], [hx - 0.02, clear + 0.3, zN + 0.1], 0.06, "trim", V);
  A.box([-0.26, clear + 0.32, zN], [0.26, clear + 0.44, zN + 0.006], "plate", D);
  A.box([-hx, clear, zT], [hx, Hh - 0.1, zT + 0.1], paint, { k: 0, bv: 0.05 });
  cabGlass(A, -hx + 0.2, hx - 0.2, 2.0, 2.7, zT - 0.006, "glass_t");
  for (let i = 0; i < 6; i++) A.box([-0.8, 0.65 + i * 0.09, zT - 0.012], [0.8, 0.69 + i * 0.09, zT - 0.006], "trim", D);
  for (const s of [-1, 1]) A.box([s > 0 ? hx - 0.25 : -hx + 0.05, 0.9, zT - 0.012], [s > 0 ? hx - 0.05 : -hx + 0.25, 1.6, zT - 0.005], "lamp_r", D);
  A.rbox([-hx + 0.02, clear - 0.05, zT - 0.1], [hx - 0.02, clear + 0.3, zT + 0.05], 0.06, "trim", V);
  for (const s of [-1, 1]) bigMirror(A, s, s * hx, 2.3, zN - 0.15);
  // kerb-side doors: glazed leaves with frames, step
  for (const [z0, z1] of doors) {
    const q = (z1 - z0) / 4;
    for (const zz of [z0 + q, z1 - q]) {
      A.box([-hx - 0.01, clear + 0.08, zz - q + 0.02], [-hx + 0.01, yWin1, zz + q - 0.02], "glass", V);
      for (const e of [-1, 1]) A.box([-hx - 0.02, clear + 0.08, zz + e * (q - 0.03) - 0.025], [-hx + 0.02, yWin1, zz + e * (q - 0.03) + 0.025], "trim", D);
      A.box([-hx - 0.02, 1.15, zz - q + 0.03], [-hx + 0.02, 1.2, zz + q - 0.03], "trim", D);
    }
    A.box([-hx + 0.02, clear, z0], [-hx + 0.4, clear + 0.06, z1], "pl_grey", D);
  }
  // interior: floor, seat pairs, stanchions, driver
  A.box([-hx + 0.05, 0.36, zT + 0.2], [hx - 0.05, 0.4, zN - 0.3], "pl_grey", V);
  for (let z = zN - 2.4; z > zT + 0.8; z -= 0.82) for (const s of [-1, 1]) {
    if (s < 0 && z > doors[1][0] - 0.3 && z < doors[1][1] + 0.4) continue;
    seat(A, s * 0.78, 0.86, z, 0.85, "poly_blue", { back: 0.58, head: false, depth: 0.42 });
    A.box([s * 0.78 - 0.45, 1.44, z - 0.47], [s * 0.78 + 0.45, 1.47, z - 0.44], "pl_grey", D);
  }
  for (let z = zN - 1.5; z > zT + 1; z -= 1.65) for (const s of [-1, 1]) rod(A, [s * 0.32, 0.4, z], [s * 0.32, Hh - 0.15, z], 0.035, "pl_yellow", { seg: 8 });
  seat(A, 0.75, 0.9, zN - 1.0, 0.5, "seat", {});
  A.cyl([0.75, 1.25, zN - 0.55], [0.5, 0.04, 0.5], "rubber", { ...D, seg: 16, r: [-35, 0, 0] });
  const wx = hx - tw / 2 - 0.04;
  for (const [x, z, s] of [[wx, wf, 1], [-wx, wf, -1], [wx, wr, 1], [-wx, wr, -1]]) dualWheel(A, x, yC, z, Dw, tw, s, "steel", z < 0);
  A.box([-hx, clear, zT], [hx, Hh, zN], paint, H);
  return fin(A, { cover: vehicleCover(L, W, 3.0) });
}
// medium / heavy truck cab: conventional (hoodLen > 0) or short-nose; front at zf; returns the rear z of the cab
function truckCab(A, o) {
  const { zf, W, hoodLen = 0, hoodY = 1.6, floor = 0.95, roof = 2.9, len = 1.9, paint, sleeper = 0 } = o;
  const hx = W / 2, cx = hx - 0.05, zc = zf - hoodLen, zr = zc - len;
  if (hoodLen > 0) {
    A.rbox([-hx + 0.25, floor - 0.25, zc - 0.05], [hx - 0.25, hoodY, zf - 0.05], 0.12, paint, V);   // bonnet
    A.box([-hx + 0.4, floor - 0.1, zf - 0.07], [hx - 0.4, hoodY - 0.08, zf + 0.01], "trim", V);    // grille
    const gs = (hoodY - floor - 0.1) / 9;
    for (let i = 0; i < 9; i++) A.box([-hx + 0.42, floor + i * gs, zf + 0.01], [hx - 0.42, floor + i * gs + 0.03, zf + 0.025], "chrome", D);
    for (const s of [-1, 1]) {   // fenders over the steer axle, headlamps
      A.rbox([s > 0 ? hx - 0.42 : -hx, floor - 0.05, zf - 1.3], [s > 0 ? hx : -hx + 0.42, floor + 0.1, zf - 0.1], 0.05, paint, V);
      A.box([s > 0 ? hx - 0.4 : -hx + 0.08, floor + 0.12, zf - 0.14], [s > 0 ? hx - 0.08 : -hx + 0.4, floor + 0.28, zf - 0.1], "lamp_w", D);
    }
  } else truckLamps(A, hx, floor - 0.15, zf + 0.005);
  A.box([-hx, floor - 0.3, zr], [hx, floor + 0.7, zc], paint, { k: 0, bv: 0.04 });
  for (const s of [-1, 1]) A.box([s > 0 ? cx - 0.04 : -cx, floor + 0.7, zr], [s > 0 ? cx : -cx + 0.04, roof - 0.1, zr + len * 0.35], paint, V);
  A.box([-cx, floor + 0.7, zr], [cx, roof - 0.1, zr + 0.05], paint, V);
  A.rbox([-cx - 0.02, roof - 0.12, zr], [cx + 0.02, roof, zc], 0.05, paint, V);
  bar(A, [0, floor + 0.72, zc - 0.02], [0, roof - 0.12, zc - 0.25], W - 0.2, 0.014, "glass", V);
  for (const s of [-1, 1]) {
    bar(A, [s * (cx - 0.03), floor + 0.7, zc - 0.02], [s * (cx - 0.03), roof - 0.1, zc - 0.25], 0.07, 0.08, paint, V);
    cabGlassX(A, s * cx, floor + 0.72, roof - 0.18, zr + len * 0.35, zc - 0.25);
    extrudeX(A, s * cx - 0.004, s * cx + 0.004, [[zc - 0.25, floor + 0.72], [zc - 0.02, floor + 0.72], [zc - 0.25, roof - 0.12]], "glass", V);
    seam(A, [s * hx - 0.003, floor - 0.25, zr + len * 0.33], [s * hx + 0.003, floor + 0.7, zr + len * 0.35]);
    A.box([s > 0 ? hx - 0.003 : -hx - 0.02, floor + 0.55, zr + len * 0.38], [s > 0 ? hx + 0.02 : -hx + 0.003, floor + 0.58, zr + len * 0.38 + 0.18], "chrome", D);
    for (const y of [floor - 0.55, floor - 0.2]) A.box([s > 0 ? hx - 0.3 : -hx - 0.02, y, zr + len * 0.4], [s > 0 ? hx + 0.02 : -hx + 0.3, y + 0.04, zr + len * 0.8], "steel", dv(0.005));   // steps
    bigMirror(A, s, s * hx, floor + 1.2, zc - 0.1);
  }
  A.box([-0.7, roof, zr + 0.2], [0.7, roof + 0.12, zr + 0.3], "lamp_a", D);
  for (const x of [0.5, -0.5]) seat(A, x, floor + 0.45, zc - 0.55, 0.52, "seat");
  A.box([-cx + 0.05, floor + 0.35, zc - 0.32], [cx - 0.05, floor + 0.8, zc - 0.05], "dash", V);
  A.cyl([0.5, floor + 0.85, zc - 0.5], [0.45, 0.03, 0.45], "rubber", { ...D, seg: 16, r: [-50, 0, 0] });
  if (sleeper) {
    A.box([-hx, floor - 0.3, zr - sleeper], [hx, roof + 0.5, zr], paint, { k: 0, bv: 0.05 });
    bar(A, [0, roof, zc - 0.3], [0, roof + 0.5, zr - 0.1], W - 0.1, 0.05, paint, V);   // roof fairing
    for (const s of [-1, 1]) A.box([s > 0 ? hx - 0.002 : -hx - 0.004, floor + 0.3, zr - sleeper + 0.2], [s > 0 ? hx + 0.004 : -hx + 0.002, floor + 0.9, zr - 0.2], "seam", D);
  }
  return zr - sleeper;
}
function boxTruck() {
  const W = 2.45, hx = W / 2, Dw = 0.95, tw = 0.28, L = 7.6, zf = L / 2, wf = zf - 1.15, wr = -1.85;
  const A = mk("mod_box_truck", "Box truck (7.5 t, generic)", "Vehicles",
    "Generic 7.5 t box truck: a short-nose cab with deep glazing, west-coast mirrors and steps on a ladder chassis, a 5.4 m dry-freight box body with corner posts, top / bottom rails, side ribs, marker lamps and a slatted roll-up rear door over a step bumper; twin rear wheels, fuel tank and side guards.", [W + 0.8, L + 0.2], { maxGrade: 0.3 });
  const paint = "car_white";
  const zr = truckCab(A, { zf, W: 2.3, floor: 1.15, roof: 2.85, len: 1.85, paint });
  A.rbox([-1.15, 0.35, zf - 0.12], [1.15, 0.65, zf + 0.06], 0.05, "p_grey", V);
  for (const s of [-1, 1]) A.box([s * 0.45 - 0.05, 0.6, -L / 2], [s * 0.45 + 0.05, 0.85, zr + 0.2], "steel_dk", V);
  A.cyl([-0.85, 0.65, zr - 0.6], [0.5, 1.0, 0.5], "steel", { k: 0, seg: 16, r: [90, 0, 0] });
  for (const s of [-1, 1]) for (const y of [0.55, 0.8]) A.box([s * hx - 0.02, y, wr + 0.65], [s * hx + 0.01, y + 0.06, zr - 1.2], "steel", D);
  const zb0 = -L / 2, zb1 = zr - 0.1, y0 = 1.1, y1 = 3.55;
  A.box([-hx, 0.95, zb0], [hx, y0, zb1], "steel_dk", V);
  A.box([-hx + 0.02, y0, zb0 + 0.02], [hx - 0.02, y1 - 0.02, zb1 - 0.02], "p_white", V);
  for (const [x, z] of [[-1, zb0], [1, zb0], [-1, zb1], [1, zb1]]) A.box([x * hx - 0.04, y0, z - 0.04], [x * hx + 0.04, y1, z + 0.04], "steel", dv(0.008));
  for (const s of [-1, 1]) {
    for (const y of [y0, y1 - 0.08]) A.box([s * hx - 0.04, y, zb0], [s * hx + 0.04, y + 0.1, zb1], "steel", dv(0.008));
    for (let z = zb0 + 0.6; z < zb1 - 0.3; z += 0.6) A.box([s * hx - 0.012, y0 + 0.1, z - 0.02], [s * hx + 0.012, y1 - 0.08, z + 0.02], "p_white", D);
    for (const z of [zb0 + 0.2, (zb0 + zb1) / 2, zb1 - 0.2]) A.box([s * hx - 0.03, y1 - 0.2, z - 0.04], [s * hx + 0.03, y1 - 0.14, z + 0.04], "lamp_a", D);
  }
  for (let i = 0; i < 11; i++) A.box([-hx + 0.08, y0 + 0.1 + i * 0.21, zb0 - 0.03], [hx - 0.08, y0 + 0.28 + i * 0.21, zb0 - 0.01], "p_white", dv(0.006));
  A.box([-0.15, y0 + 0.12, zb0 - 0.05], [0.15, y0 + 0.2, zb0 - 0.03], "steel_dk", D);
  for (const s of [-1, 1]) A.box([s > 0 ? hx - 0.25 : -hx + 0.05, 0.75, zb0 - 0.04], [s > 0 ? hx - 0.05 : -hx + 0.25, 0.95, zb0 - 0.01], "lamp_r", D);
  A.box([-hx + 0.1, 0.5, zb0 - 0.15], [hx - 0.1, 0.6, zb0 + 0.1], "steel", dv(0.01));   // step bumper
  A.box([-0.26, 0.62, zb0 - 0.152], [0.26, 0.74, zb0 - 0.146], "plate", D);
  const wx = hx - tw / 2 - 0.03;
  for (const [x, z, s] of [[wx, wf, 1], [-wx, wf, -1], [wx - 0.05, wr, 1], [-wx + 0.05, wr, -1]]) dualWheel(A, x, Dw / 2, z, Dw, tw, s, "steel", z < 0);
  for (const s of [-1, 1]) A.rbox([s > 0 ? hx - 0.35 : -hx, Dw + 0.05, wr - 0.6], [s > 0 ? hx : -hx + 0.35, Dw + 0.1, wr + 0.6], 0.02, "trim", D);
  A.box([-hx, 0.35, zb0], [hx, y1, zb1], "p_white", H);
  A.box([-1.15, 0.35, zr], [1.15, 2.85, zf], "p_white", H);
  return fin(A, { cover: vehicleCover(L, W, 2.5) });
}
function semiTractor() {
  const W = 2.5, hx = W / 2, Dw = 1.04, tw = 0.29, L = 6.6, zf = L / 2, wf = zf - 1.05, wr1 = -1.4, wr2 = -2.7;
  const A = mk("mod_semi_tractor", "Semi tractor unit (generic)", "Vehicles",
    "Generic long-nose semi-trailer tractor (cab only, no trailer): a long bonnet with a tall chrome-slatted grille, set-back steer axle under fenders, a day cab with a sleeper box and roof fairing, twin chrome exhaust stacks, air intake, cylindrical fuel tanks, a fifth wheel on exposed frame rails over tandem twin-tyred drive axles with mud flaps.", [W + 0.8, L + 0.2], { maxGrade: 0.3 });
  const paint = "car_red";
  const zr = truckCab(A, { zf, W: 2.3, hoodLen: 1.9, hoodY: 1.75, floor: 1.25, roof: 3.0, len: 1.6, paint, sleeper: 1.0 });
  A.rbox([-hx, 0.4, zf - 0.2], [hx, 0.75, zf + 0.08], 0.06, "chrome", V);
  for (const s of [-1, 1]) A.box([s * 0.45 - 0.06, 0.75, -L / 2], [s * 0.45 + 0.06, 1.05, zr], "steel_dk", V);
  A.box([-0.55, 1.05, -2.35], [0.55, 1.2, -1.3], "steel_dk", { k: 0, bv: 0.02 });                          // fifth wheel
  A.cyl([0, 1.21, -1.82], [0.9, 0.04, 0.9], "steel", { ...D, seg: 20 });
  A.box([-0.08, 1.2, -1.8], [0.08, 1.23, -1.3], "seam", D);
  for (const s of [-1, 1]) {
    A.cyl([s * 0.95, 0.75, zr + 0.55], [0.6, 1.2, 0.6], "chrome", { k: 0, seg: 18, r: [90, 0, 0] });
    for (const dz of [-0.45, 0.45]) A.cyl([s * 0.95, 0.75, zr + 0.55 + dz], [0.62, 0.04, 0.62], "steel_dk", { ...D, seg: 18, r: [90, 0, 0] });
    rod(A, [s * 1.1, 1.3, zr + 0.05], [s * 1.1, 4.1, zr + 0.05], 0.13, "chrome", { seg: 12 });
    A.cyl([s * 1.1, 2.4, zr + 0.05], [0.17, 0.9, 0.17], "steel", { ...D, seg: 12 });
    A.box([s * hx - 0.02, 0.35, wr2 - 0.65], [s * hx + 0.02, 0.95, wr2 - 0.6], "rubber", D);
    A.rbox([s > 0 ? hx - 0.6 : -hx, Dw + 0.06, wr2 - 0.6], [s > 0 ? hx : -hx + 0.6, Dw + 0.1, wr1 + 0.6], 0.02, "steel", D);
  }
  A.cyl([-1.0, 2.0, zr + 1.3], [0.42, 1.2, 0.42], "chrome", { k: 0, seg: 16 });   // air intake
  const wx = hx - tw / 2 - 0.03;
  for (const [x, z, s, du] of [[wx - 0.1, wf, 1, 0], [-wx + 0.1, wf, -1, 0], [wx, wr1, 1, 1], [-wx, wr1, -1, 1], [wx, wr2, 1, 1], [-wx, wr2, -1, 1]])
    dualWheel(A, x, Dw / 2, z, Dw, tw, s, "chrome", !!du);
  A.box([-1.15, 0.4, zr], [1.15, 3.5, zf - 1.9], paint, H);
  A.box([-1.0, 0.4, zf - 1.9], [1.0, 1.75, zf + 0.08], paint, H);
  A.box([-hx, 0.3, -L / 2], [hx, 1.2, zr], "steel_dk", H);
  return fin(A, { cover: vehicleCover(L, W, 1.6) });
}
function semiTrailer() {
  const W = 2.55, hx = W / 2, L = 13.6, Dw = 1.0, tw = 0.29, y0 = 1.2, y1 = 4.0;
  const A = mk("mod_semi_trailer", "Semi trailer (13.6 m box van, generic)", "Vehicles",
    "Generic 13.6 m dry-van semi trailer, 4.0 m high: smooth side panels between top and bottom rails with posts, marker lamps and rub rails, a crank-down landing gear and king-pin plate at the front, a tandem axle bogie with twin tyres and mudflaps at the rear, side under-run guards and two rear doors with four locking bars and cam keepers over an under-ride bar.", [W + 0.2, L + 0.2], { maxGrade: 0.25 });
  const z0 = -L / 2, z1 = L / 2;
  A.box([-hx + 0.03, y0 + 0.12, z0 + 0.03], [hx - 0.03, y1 - 0.12, z1 - 0.03], "p_white", V);
  for (const s of [-1, 1]) {
    for (const y of [y0, y1 - 0.12]) A.box([s * hx - 0.04, y, z0], [s * hx + 0.02, y + 0.12, z1], "steel", dv(0.01));
    for (let z = z0 + 0.6; z < z1 - 0.3; z += 0.6) A.box([s * (hx - 0.03) - 0.004, y0 + 0.12, z - 0.02], [s * (hx - 0.03) + 0.004, y1 - 0.12, z + 0.02], "steel", D);
    for (let z = z0 + 0.4; z < z1; z += 2.8) A.box([s * hx - 0.03, y0 + 0.02, z - 0.04], [s * hx + 0.03, y0 + 0.08, z + 0.04], "lamp_a", D);
    A.box([s * hx - 0.03, 0.55, -2.0], [s * hx + 0.01, 0.65, 4.5], "steel", D);
    for (const z of [-1.8, 1.0, 3.8]) A.box([s * hx - 0.03, 0.55, z - 0.03], [s * hx, y0, z + 0.03], "steel", D);
  }
  for (const [x, z] of [[-1, z0], [1, z0], [-1, z1], [1, z1]]) A.box([x * hx - 0.05, y0, z - 0.05], [x * hx + 0.05, y1, z + 0.05], "steel", dv(0.008));
  A.box([-hx, y0 - 0.25, z0], [hx, y0, z1], "steel_dk", V);
  for (const s of [-1, 1]) {   // landing gear
    A.box([s * 0.95 - 0.07, 0.25, 4.4 - 0.07], [s * 0.95 + 0.07, y0 - 0.25, 4.4 + 0.07], "steel_dk", V);
    A.box([s * 0.95 - 0.15, 0.0, 4.4 - 0.15], [s * 0.95 + 0.15, 0.06, 4.4 + 0.15], "steel_dk", D);
    A.box([s * 0.95 - 0.05, 0.06, 4.4 - 0.05], [s * 0.95 + 0.05, 0.3, 4.4 + 0.05], "steel", D);
  }
  rod(A, [-0.95, 0.65, 4.4], [0.95, 0.65, 4.4], 0.05, "steel_dk");
  A.box([-0.6, y0 - 0.3, 5.6], [0.6, y0 - 0.25, 6.6], "steel", D);
  A.cyl([0, y0 - 0.36, 5.9], [0.09, 0.1, 0.09], "steel_dk", { ...D, seg: 10 });
  for (const x of [-0.25, 0, 0.25]) A.cyl([x, 2.2, z1 + 0.03], [0.06, 0.06, 0.06], x ? "pl_red" : "pl_blue", { ...D, seg: 10, r: [90, 0, 0] });
  seam(A, [-0.004, y0 + 0.1, z0 - 0.012], [0.004, y1 - 0.1, z0 - 0.004]);
  for (const x of [-0.95, -0.35, 0.35, 0.95]) {
    rod(A, [x, y0 + 0.1, z0 - 0.04], [x, y1 - 0.1, z0 - 0.04], 0.034, "steel", { seg: 8 });
    for (const y of [y0 + 0.12, y1 - 0.12]) A.box([x - 0.04, y - 0.04, z0 - 0.06], [x + 0.04, y + 0.04, z0 - 0.01], "steel_dk", D);
    A.box([Math.min(x - 0.015, x + 0.25 * Math.sign(x)), 1.8, z0 - 0.07], [Math.max(x - 0.015, x + 0.25 * Math.sign(x)), 1.83, z0 - 0.05], "steel", D);
  }
  for (const s of [-1, 1]) for (let i = 0; i < 4; i++) A.box([s * (hx - 0.01) - 0.012, y0 + 0.4 + i * 0.75, z0 - 0.012], [s * (hx - 0.01) + 0.012, y0 + 0.55 + i * 0.75, z0 + 0.06], "steel_dk", D);
  A.box([-hx + 0.15, 0.45, z0 + 0.2], [hx - 0.15, 0.6, z0 + 0.32], "p_red", dv(0.01));
  for (const s of [-1, 1]) A.box([s * 0.9 - 0.04, 0.6, z0 + 0.22], [s * 0.9 + 0.04, y0 - 0.25, z0 + 0.3], "steel_dk", D);
  for (const s of [-1, 1]) A.box([s > 0 ? hx - 0.3 : -hx + 0.05, 0.85, z0 + 0.19], [s > 0 ? hx - 0.05 : -hx + 0.3, 1.05, z0 + 0.23], "lamp_r", D);
  const wx = hx - tw / 2 - 0.02;
  for (const z of [-4.35, -5.7]) for (const s of [-1, 1]) dualWheel(A, s * wx, Dw / 2, z, Dw, tw, s, "steel", true);
  for (const s of [-1, 1]) A.box([s * 0.45 - 0.06, 0.55, -6.3], [s * 0.45 + 0.06, y0 - 0.25, -3.8], "steel_dk", V);
  for (const s of [-1, 1]) A.box([s * hx - 0.02, 0.3, -6.45], [s * hx + 0.02, 0.9, -6.4], "rubber", D);
  A.box([-hx, y0 - 0.25, z0], [hx, y1, z1], "p_white", H);
  A.box([-hx, 0.2, -6.3], [hx, y0, -3.8], "steel_dk", H);
  return fin(A, { cover: vehicleCover(L, W, 2.0) });
}
function milUtility(colour) {
  const W = 2.18, hx = W / 2, L = 4.7, zN = L / 2, zT = -L / 2, Dw = 0.94, tw = 0.32, wf = 1.62, wr = -1.62;
  const paint = colour === "tan" ? "p_tan" : "p_od", id = colour === "tan" ? "mod_mil_utility" : "mod_mil_utility_green";
  const A = mk(id, `Military utility vehicle (${colour})`, "Vehicles",
    `Generic wide-track 4x4 military utility vehicle in ${colour === "tan" ? "desert tan" : "olive drab"}: a broad flat bonnet over a slatted grille, flared fenders over 0.94 m run-flat tyres, four armoured doors with small thick windows, a split flat windscreen, a hard-top with a roof ring and three-plate gun shield (no weapon), brush guard, tow shackles, blackout lamps and jerry-can brackets.`, [W + 0.5, L + 0.5], { maxGrade: 0.45 });
  const xin = hx - tw - 0.04, clear = 0.42, belt = 1.2, roof = 1.88, zA = 0.72, zR = -1.9;
  A.box([-xin, clear, zT + 0.1], [xin, belt, zN - 0.05], paint, vb(0.03));                                         // tub
  for (const s of [-1, 1]) {
    const xo = s > 0 ? [xin, hx] : [-hx, -xin];
    A.box([xo[0], 0.62, wr + 0.6], [xo[1], belt, wf - 0.6], paint, vb(0.02));                                        // doors
    for (const wz of [wf, wr]) {
      A.box([xo[0], Dw + 0.06, wz - 0.62], [xo[1], belt - 0.02, wz + 0.62], paint, vb(0.03));                      // fender
      A.box([xo[0] + (s > 0 ? 0 : -0.03), Dw + 0.02, wz - 0.66], [xo[1] + (s > 0 ? 0.03 : 0), Dw + 0.08, wz + 0.66], paint, dv(0.015));   // flare lip
    }
    A.box([xo[0], 0.62, wf + 0.62], [xo[1], belt, zN - 0.05], paint, vb(0.02));
    A.box([xo[0], 0.62, zT + 0.1], [xo[1], belt, wr - 0.62], paint, vb(0.02));
    for (const z of [zA - 0.05, -0.45, zR + 0.05]) seam(A, [s * hx - 0.004, 0.65, z - 0.004], [s * hx + 0.004, belt, z + 0.004]);
    for (const z of [zA - 0.25, -0.65]) {
      A.box([s > 0 ? hx - 0.004 : -hx - 0.025, 1.05, z - 0.12], [s > 0 ? hx + 0.025 : -hx + 0.004, 1.09, z], "steel_dk", D);
      A.box([s > 0 ? hx : -hx - 0.012, 0.75, z - 0.05], [s > 0 ? hx + 0.012 : -hx, 0.95, z + 0.05], "steel_dk", D);
    }
    const xc = s * (hx - 0.06);
    A.box([xc - 0.05, belt, zR], [xc + 0.05, roof - 0.06, zA], paint, V);
    for (const [z0, z1] of [[-0.35, zA - 0.12], [zR + 0.15, -0.55]]) A.box([xc - 0.056, belt + 0.18, z0], [xc + 0.056, roof - 0.22, z1], "glass", V);
    A.rbox([s > 0 ? hx + 0.02 : -hx - 0.22, 1.32, zA + 0.02], [s > 0 ? hx + 0.22 : -hx - 0.02, 1.6, zA + 0.1], 0.02, "trim", V);
    A.box([s > 0 ? hx - 0.08 : -hx - 0.04, 1.4, zA + 0.04], [s > 0 ? hx + 0.04 : -hx + 0.08, 1.46, zA + 0.08], "trim", D);
    A.box([s * 0.75 - 0.12, belt - 0.15, zN - 0.04], [s * 0.75 + 0.12, belt - 0.04, zN + 0.01], "steel_dk", D);
    A.cyl([s * 0.75, belt - 0.1, zN + 0.012], [0.16, 0.015, 0.16], "lamp_w", { ...D, seg: 12, r: [90, 0, 0] });
    A.box([s * 0.85 - 0.06, 0.75, zN + 0.12], [s * 0.85 + 0.06, 0.85, zN + 0.2], "steel_dk", D);
    A.arch([s * 0.6, 0.62, zN + 0.2], [0.1, 0.08, 0.03], "steel", { ...D, th: 0.02, r: [0, 90, 0] });
    A.box([s * 0.7 - 0.08, 0.95, zT - 0.03], [s * 0.7 + 0.08, 1.05, zT], "lamp_r", D);
  }
  bar(A, [0, belt - 0.02, zN - 0.1], [0, belt + 0.06, zA + 0.05], W - 0.1, 0.05, paint, { k: 0, bv: 0.02 });       // bonnet
  A.box([-0.62, 0.6, zN - 0.06], [0.62, belt - 0.12, zN + 0.01], "trim", D);
  for (let i = 0; i < 12; i++) A.box([-0.6 + i * 0.105, 0.62, zN], [-0.57 + i * 0.105, belt - 0.14, zN + 0.03], paint, D);
  A.box([-0.95, 0.45, zN - 0.05], [0.95, 0.65, zN + 0.2], "steel_dk", vb(0.02));                                   // bumper
  for (const s of [-1, 1]) rod(A, [s * 0.5, 0.65, zN + 0.18], [s * 0.5, 1.1, zN + 0.05], 0.05, "steel_dk");
  rod(A, [-0.5, 1.1, zN + 0.05], [0.5, 1.1, zN + 0.05], 0.05, "steel_dk");
  for (const s of [-1, 1]) A.obox([s * 0.45, (belt + roof) / 2, zA + 0.03], [0.82, roof - belt - 0.16, 0.03], [-10, 0, 0], "glass", V);
  A.obox([0, (belt + roof) / 2, zA + 0.04], [0.08, roof - belt - 0.06, 0.08], [-10, 0, 0], paint, V);
  for (const s of [-1, 1]) A.obox([s * (hx - 0.1), (belt + roof) / 2, zA + 0.04], [0.12, roof - belt - 0.06, 0.08], [-10, 0, 0], paint, V);
  A.box([-hx + 0.04, roof - 0.07, zR], [hx - 0.04, roof, zA + 0.12], paint, vb(0.03));
  A.box([-hx + 0.04, belt, zR - 0.02], [hx - 0.04, roof - 0.06, zR + 0.06], paint, V);
  A.box([-xin, belt, zT + 0.1], [xin, belt + 0.35, zT + 0.18], paint, vb(0.01));
  for (const s of [-1, 1]) A.box([s > 0 ? xin - 0.05 : -xin, belt, zT + 0.1], [s > 0 ? xin : -xin + 0.05, belt + 0.35, zR], paint, vb(0.01));
  ring(A, [0, roof + 0.05, -0.6], 0.48, 16, 0.07, 0.1, "y", "steel_dk", D);
  for (const a of [-50, 0, 50]) { const t = a / DEG; A.obox([Math.sin(t) * 0.6, roof + 0.4, -0.6 + Math.cos(t) * 0.6], [0.62, 0.55, 0.04], [0, a, 0], paint, { k: 0, bv: 0.01 }); }
  for (const x of [0.6, -0.6]) A.cyl([x, belt + 0.38, zT + 0.15], [0.07, 0.06, 0.07], "steel_dk", { ...D, seg: 8 });
  for (const x of [-0.25, 0.25]) {   // jerry cans in rear brackets
    A.box([x - 0.08, belt + 0.02, zT - 0.1], [x + 0.08, belt + 0.36, zT + 0.06], paint, dv(0.01));
    A.box([x - 0.09, belt + 0.02, zT - 0.12], [x + 0.09, belt + 0.38, zT - 0.1], "steel_dk", D);
  }
  for (const [x, z] of [[0.45, -0.05], [-0.45, -0.05], [0.45, -1.2], [-0.45, -1.2]]) seat(A, x, belt - 0.32, z, 0.5, "seat_tan");
  A.box([-0.9, belt - 0.1, zA - 0.4], [0.9, belt + 0.12, zA - 0.05], "dash", V);
  const wx = hx - tw / 2;
  for (const [x, z, s] of [[wx, wf, 1], [-wx, wf, -1], [wx, wr, 1], [-wx, wr, -1]]) wheel(A, [x, Dw / 2, z], Dw, tw, s, { rim: paint, lugs: 8, spokes: 0, rimD: 0.5 });
  A.box([-hx, clear, zT], [hx, belt, zN + 0.2], paint, H);
  A.box([-hx + 0.04, belt, zR], [hx - 0.04, roof, zA + 0.12], paint, H);
  return fin(A, { cover: vehicleCover(L, W, 1.2) });
}
function apc() {
  const W = 2.9, L = 7.8, Dw = 1.2, tw = 0.38, zN = L / 2, zT = -L / 2, axles = [2.35, 0.95, -1.15, -2.55];
  const A = mk("mod_apc_wheeled", "Wheeled APC (8x8, generic)", "Vehicles",
    "Generic 8x8 wheeled armoured personnel carrier: a faceted welded hull with a steep glacis and sloping upper sides over eight 1.2 m run-flat tyres, a driver's hatch with vision blocks, a small remote weapon station with a sensor head and short barrel, smoke-grenade dischargers, troop hatches, a rear ramp with a door, stowage bins, tow eyes, guarded lamps and appliqué armour bolts. Olive green.", [W + 0.7, L + 0.3], { maxGrade: 0.5 });
  const P = "p_od", hw = 1.25;
  extrudeX(A, -hw, hw, [[zT, 0.55], [zN - 0.6, 0.55], [zN, 1.1], [zN, 1.45], [zT, 1.45]], P, V);
  extrudeZ(A, zT, zN - 1.25, [[-1.45, 1.45], [1.45, 1.45], [1.2, 2.25], [-1.2, 2.25]], P, V);
  bar(A, [0, 1.42, zN - 0.02], [0, 2.22, zN - 1.25], 2.4, 0.08, P, V);                                               // glacis
  for (const s of [-1, 1]) extrudeX(A, s > 0 ? hw : -1.45, s > 0 ? 1.45 : -hw, [[zN - 1.25, 1.45], [zN - 0.05, 1.45], [zN - 1.25, 2.2]], P, V);
  for (const s of [-1, 1]) for (const z of axles) {   // wheel stations and sponson lips
    A.box([s > 0 ? hw - 0.25 : -hw, 0.5, z - 0.12], [s > 0 ? hw : -hw + 0.25, 0.75, z + 0.12], "steel_dk", D);
    A.box([s > 0 ? 1.2 : -1.45, 1.4, z - 0.68], [s > 0 ? 1.45 : -1.2, 1.46, z + 0.68], P, dv(0.01));
  }
  for (const s of [-1, 1]) for (let i = 0; i < 6; i++) boltRow(A, [s * 1.335, 1.85, zT + 0.5 + i * 1.0], [s * 1.335, 1.85, zT + 0.9 + i * 1.0], 3, "x", "steel_dk", 0.03, 0.02);
  A.cyl([0.55, 2.27, zN - 1.6], [0.6, 0.06, 0.6], P, { k: 0, seg: 16 });
  for (let i = 0; i < 3; i++) A.obox([0.55 + (i - 1) * 0.18, 2.28, zN - 1.32], [0.14, 0.08, 0.05], [-20, 0, 0], "glass_t", D);
  for (const [x, z] of [[-0.6, -1.2], [0.6, -1.2]]) { A.box([x - 0.4, 2.25, z - 0.5], [x + 0.4, 2.3, z + 0.5], P, dv(0.01)); A.box([x - 0.05, 2.3, z - 0.3], [x + 0.05, 2.34, z - 0.1], "steel_dk", D); }
  A.cyl([-0.2, 2.32, 0.2], [0.95, 0.12, 0.95], "steel_dk", { k: 0, seg: 20 });
  A.box([-0.55, 2.38, -0.15], [0.15, 2.78, 0.5], P, { k: 0, bv: 0.03 });
  A.box([0.15, 2.48, 0.05], [0.42, 2.75, 0.4], P, dv(0.02));
  A.box([0.17, 2.55, 0.4], [0.4, 2.7, 0.41], "glass_t", D);
  rod(A, [-0.25, 2.55, 0.5], [-0.25, 2.55, 1.4], 0.06, "steel_dk", { seg: 10 });
  rod(A, [-0.25, 2.55, 1.3], [-0.25, 2.55, 1.42], 0.085, "steel_dk", { seg: 10 });
  A.box([-0.62, 2.4, 0.0], [-0.56, 2.65, 0.35], "steel_dk", D);
  for (const s of [-1, 1]) for (let i = 0; i < 4; i++) A.cyl([s * 1.05, 2.32, zN - 1.5 + i * 0.13], [0.08, 0.2, 0.08], "steel_dk", { ...D, seg: 8, r: [60, s * 30, 0] });
  for (const s of [-1, 1]) {
    A.box([s * 0.9 - 0.1, 1.3, zN - 0.12], [s * 0.9 + 0.1, 1.42, zN + 0.01], "lamp_w", D);
    A.arch([s * 0.9, 1.28, zN + 0.05], [0.28, 0.18, 0.03], "steel_dk", { ...D, th: 0.02 });
    A.arch([s * 0.6, 0.75, zN - 0.32], [0.14, 0.1, 0.04], "steel_dk", { ...D, th: 0.03, r: [-30, 0, 0] });
    A.box([s * 0.95 - 0.08, 1.7, zT - 0.02], [s * 0.95 + 0.08, 1.84, zT], "lamp_r", D);
    A.box([s > 0 ? 1.45 : -1.75, 1.5, -3.2], [s > 0 ? 1.75 : -1.45, 1.9, -2.0], P, vb(0.02));   // stowage bins
  }
  seam(A, [-0.85, 0.6, zT - 0.006], [0.85, 0.61, zT]); seam(A, [-0.85, 2.0, zT - 0.006], [0.85, 2.01, zT]);
  for (const s of [-1, 1]) seam(A, [s * 0.85 - 0.004, 0.6, zT - 0.006], [s * 0.85 + 0.004, 2.0, zT]);
  A.box([-0.35, 0.9, zT - 0.012], [0.35, 1.8, zT - 0.006], P, D);
  seam(A, [-0.35, 0.9, zT - 0.014], [0.35, 0.905, zT - 0.012]);
  const wx = 1.45 - tw / 2 + 0.05;
  for (const z of axles) for (const s of [-1, 1]) wheel(A, [s * wx, Dw / 2, z], Dw, tw, s, { rim: P, lugs: 8, spokes: 0, rimD: 0.5 });
  A.box([-1.45, 0.5, zT], [1.45, 2.25, zN], P, H);
  return fin(A, { cover: vehicleCover(L, W, 2.0) });
}
function vehicles() {
  return [
    car("mod_sedan", "Sedan (generic, silver)", "Generic four-door saloon car, 4.7 x 1.82 x 1.45 m: three-box body with a sloping bonnet, raked glazing with A/B/C pillars, tinted rear glass, alloy wheels on 0.65 m tyres, front and rear lamp clusters as coloured panels, door seams and handles, mirrors, wipers, blank plates and seats visible through the glass.", SEDAN, "car_silver", "ok"),
    car("mod_sedan_red", "Sedan (generic, red)", "The generic four-door saloon in a dark red finish.", SEDAN, "car_red", "ok"),
    car("mod_hatchback", "Hatchback (generic)", "Generic five-door hatchback, 4.1 x 1.76 x 1.48 m: short bonnet, long roof to a steeply raked hatch, broad C pillars, alloy wheels, lamp clusters, mirrors, door seams, seats through the glass.", HATCH, "car_blue", "ok"),
    car("mod_suv", "SUV (generic)", "Generic mid-size SUV, 4.8 x 1.95 x 1.8 m: tall bonnet, upright glasshouse with roof rails, black plastic bumpers, 0.76 m tyres, five seats visible through the glass.", SUV, "car_black", "ok"),
    car("mod_pickup", "Pickup truck (crew cab, generic)", "Generic full-size crew-cab pickup, 5.5 x 2.0 x 1.9 m: tall bonnet and chrome bumper, four-door cab, a 1.75 m open load bed with wheel tubs, ribbed floor and tailgate; 0.8 m tyres.", PICKUP, "car_white", "ok"),
    car("mod_delivery_van", "Delivery van (high roof, generic)", "Generic high-roof panel van, 5.9 x 2.05 x 2.62 m: short bonnet and steep windscreen, glazed cab doors, solid load-box sides with a sliding side-door track, twin rear doors with small windows, a cab bulkhead and two seats.", VAN, "car_white", "ok"),
    car("mod_sedan_wrecked", "Sedan (wrecked)", "Abandoned wrecked saloon: smashed side and rear glass, crazed windscreen, popped bonnet, a bumper hanging off, two flat tyres, rust and grime on the paint.", SEDAN, "car_dirty", "wreck", { broken: ["left", "rear"], cracked: ["ws", "right"], flat: [0, 3] }),
    car("mod_sedan_burned", "Sedan (burned out)", "Burned-out saloon shell: blistered rust-brown metal, no glass, plastics melted away, seats burned to their frames, sitting on its bare rims in a scorch mark.", SEDAN, "car_silver", "burn"),
    car("mod_van_wrecked", "Delivery van (wrecked)", "Abandoned wrecked panel van: broken cab glass, cracked windscreen, popped bonnet, a hanging bumper, flat tyres, rust streaks.", VAN, "car_dirty", "wreck", { broken: ["right"], cracked: ["ws", "left"], flat: [1, 2] }),
    car("mod_van_burned", "Delivery van (burned out)", "Burned-out high-roof van shell on its bare rims in a scorch mark: no glass or tyres, blistered rust-brown panels, burned seats.", VAN, "car_white", "burn"),
    cityBus(), boxTruck(), semiTractor(), semiTrailer(), milUtility("tan"), milUtility("green"), apc(),
  ];
}

// ================================================================================================= INDUSTRIAL COVER
// ISO dry container, length along z, doors at +z. o: len, m (wall colour), open (doors swung to 90 degrees, collider
// walls, walkable floor), damage, y0 (stacking base), zo (z offset), x0 (x offset), into-collider flag solid
const CW = 2.438, CH = 2.591;
function container(A, o) {
  const { len, m, open = false, damage = false, y0 = 0, zo = 0, solid = true } = o;
  const hx = CW / 2, hz = len / 2, R = rng(Math.round(len * 100) + (damage ? 7 : 0) + y0 * 13);
  const at = (x, y, z) => [x, y + y0, z + zo];
  const B = (a, b, mm, opt = {}) => A.box(at(...a), at(...b), mm, opt);
  const frame = damage ? "rust_h" : m;
  // frame: bottom / top side rails, end sills and headers, corner posts, castings
  for (const s of [-1, 1]) {
    B([s * hx - 0.08 * (s > 0 ? 1 : 0), 0, -hz], [s * hx + 0.08 * (s < 0 ? 1 : 0), 0.16, hz], frame, vb(0.01));
    B([s * hx - 0.06 * (s > 0 ? 1 : 0), CH - 0.1, -hz], [s * hx + 0.06 * (s < 0 ? 1 : 0), CH, hz], frame, vb(0.01));
  }
  for (const e of [-1, 1]) {
    B([-hx, 0, e * hz - (e > 0 ? 0.12 : 0)], [hx, 0.16, e * hz + (e < 0 ? 0.12 : 0)], frame, vb(0.01));
    B([-hx, CH - 0.2, e * hz - (e > 0 ? 0.12 : 0)], [hx, CH, e * hz + (e < 0 ? 0.12 : 0)], frame, vb(0.01));
    for (const s of [-1, 1]) {
      B([s > 0 ? hx - 0.15 : -hx, 0, e > 0 ? hz - 0.15 : -hz], [s > 0 ? hx : -hx + 0.15, CH, e > 0 ? hz : -hz + 0.15], frame, vb(0.012));
      for (const y of [0, CH - 0.118]) {
        B([s > 0 ? hx - 0.178 : -hx - 0.002, y, e > 0 ? hz - 0.162 : -hz - 0.002], [s > 0 ? hx + 0.002 : -hx + 0.178, y + 0.118, e > 0 ? hz + 0.002 : -hz + 0.162], "steel_dk", dv(0.008));
        B([s > 0 ? hx - 0.12 : -hx + 0.05, y + 0.03, e > 0 ? hz + 0.002 : -hz - 0.004], [s > 0 ? hx - 0.05 : -hx + 0.12, y + 0.08, e > 0 ? hz + 0.004 : -hz - 0.002], "seam", D);
        B([s > 0 ? hx + 0.002 : -hx - 0.004, y + 0.03, e > 0 ? hz - 0.12 : -hz + 0.05], [s > 0 ? hx + 0.004 : -hx - 0.002, y + 0.08, e > 0 ? hz - 0.05 : -hz + 0.12], "seam", D);
      }
    }
  }
  // corrugated side walls with raised ribs (a damaged box has a stove-in panel and rust patches)
  const dent = damage ? [-hz * 0.35, -hz * 0.05] : null;
  for (const s of [-1, 1]) {
    const xw = s * (hx - 0.03);
    if (dent && s > 0) {
      B([xw - 0.012, 0.16, -hz + 0.15], [xw + 0.012, CH - 0.1, dent[0]], m, V);
      B([xw - 0.012, 0.16, dent[1]], [xw + 0.012, CH - 0.1, hz - 0.15], m, V);
      A.obox(at(xw - 0.12, 1.25, (dent[0] + dent[1]) / 2), [0.024, CH - 0.5, dent[1] - dent[0] + 0.05], [0, -8, 6], "c_rust", V);
      B([xw - 0.012, 0.16, dent[0]], [xw + 0.012, 0.5, dent[1]], "c_rust", V);
    } else B([xw - 0.012, 0.16, -hz + 0.15], [xw + 0.012, CH - 0.1, hz - 0.15], m, V);
    for (let z = -hz + 0.3; z < hz - 0.2; z += 0.278) {
      if (dent && s > 0 && z > dent[0] && z < dent[1]) continue;
      B([s > 0 ? hx - 0.02 : -hx + 0.003, 0.17, z - 0.07], [s > 0 ? hx - 0.003 : -hx + 0.02, CH - 0.11, z + 0.07], damage && R() < 0.2 ? "c_rust" : m, D);
    }
  }
  B([-hx + 0.03, CH - 0.06, -hz + 0.15], [hx - 0.03, CH - 0.03, hz - 0.15], damage ? "c_rust" : m, V);           // roof
  for (let z = -hz + 0.6; z < hz - 0.4; z += 0.9) B([-hx + 0.1, CH - 0.035, z - 0.2], [hx - 0.1, CH - 0.02, z + 0.2], m, D);
  B([-hx + 0.15, 0.16, -hz + 0.02], [hx - 0.15, CH - 0.2, -hz + 0.05], m, V);                                    // front end wall
  for (let x = -hx + 0.3; x < hx - 0.2; x += 0.278) B([x - 0.07, 0.17, -hz - 0.0], [x + 0.07, CH - 0.21, -hz + 0.02], m, D);
  B([-hx + 0.05, 0.12, -hz + 0.05], [hx - 0.05, 0.16, hz - 0.05], "wood_dk", V);                                  // floor
  for (const s of [-1, 1]) for (const z of [-hz + 0.5, hz - 0.5]) B([s > 0 ? hx - 0.3 : -hx + 0.1, CH - 0.35, z - 0.08], [s > 0 ? hx - 0.1 : -hx + 0.3, CH - 0.25, z + 0.08], "seam", D);   // vents
  if (len < 7) for (const s of [-1, 1]) for (const z of [-1.03, 1.03]) B([s > 0 ? hx - 0.002 : -hx - 0.004, 0.03, z - 0.18], [s > 0 ? hx + 0.004 : -hx + 0.002, 0.13, z + 0.18], "seam", D);   // fork pockets
  // doors
  const lw = hx - 0.01, dh0 = 0.16, dh1 = CH - 0.2;
  const leaf = (s, ang) => {   // s: hinge side; ang: swing in degrees (0 closed)
    const hzg = hz + 0.02, t = (ang * Math.PI) / 180, dir = [-s * Math.cos(t), Math.sin(t)], hxg = hx + (ang ? 0.03 : 0);
    const pt = (u, y) => at(s * hxg + dir[0] * u, y, hzg + dir[1] * u);
    const rot = s > 0 ? ang : -ang;   // turns the leaf's local x onto dir
    A.obox(pt(lw / 2, (dh0 + dh1) / 2), [lw, dh1 - dh0, 0.04], [0, rot, 0], m, open || damage ? {} : V);
    for (const y of [0.55, 1.2, 1.85]) A.obox(pt(lw / 2, y), [lw - 0.12, 0.22, 0.05], [0, rot, 0], m, D);
    for (const u of [0.3, 0.9]) {
      A.obox(pt(u, (dh0 + dh1) / 2 - 0.02), [0.034, dh1 - dh0 + 0.12, 0.034], [0, rot, 0], "steel", D);   // locking bar
      for (const y of [dh0 - 0.02, dh1 + 0.02]) A.obox(pt(u, y), [0.07, 0.07, 0.07], [0, rot, 0], "steel_dk", D);
      A.obox(pt(u + 0.12, 1.15), [0.26, 0.035, 0.05], [0, rot, 0], "steel", D);
    }
    for (const y of [0.4, 1.0, 1.6, 2.2]) A.obox(pt(0.03, y), [0.07, 0.14, 0.07], [0, rot, 0], "steel_dk", D);
    if (ang === 0) A.obox(pt(lw - 0.01, (dh0 + dh1) / 2), [0.02, dh1 - dh0, 0.05], [0, rot, 0], "rubber", D);
  };
  if (open) { leaf(1, 90); leaf(-1, 90); }
  else if (damage) { leaf(1, 0); leaf(-1, 28); }
  else { leaf(1, 0); leaf(-1, 0); }
  if (!open && !damage) B([0.3, 1.9, hz + 0.045], [0.55, 2.05, hz + 0.05], "plate", D);
  // colliders
  if (open) {
    for (const s of [-1, 1]) B([s > 0 ? hx - 0.05 : -hx, 0, -hz], [s > 0 ? hx : -hx + 0.05, CH, hz], m, H);
    B([-hx, CH - 0.1, -hz], [hx, CH, hz], m, H);
    B([-hx, 0, -hz], [hx, 0.16, hz], m, H);
    B([-hx, 0, -hz], [hx, CH, -hz + 0.06], m, H);
  } else if (solid) B([-hx, 0, -hz], [hx, CH, hz + 0.06], m, H);
}
// boxCover is laid out along x: turn it to run along z (x <-> z, yaw mirrored), shifted by zo
const swapCover = (list, zo = 0) => list.map((c) => ({ ...c, p: [c.p[2], 0, Math.round((c.p[0] + zo) * 1000) / 1000], yaw: (450 - c.yaw) % 360 }));
function containerAsset(id, name, desc, o) {
  const len = o.len === 40 ? 12.192 : 6.058, extra = o.open ? 1.3 : o.damage ? 0.6 : 0.15;
  const A = mk(id, name, "Industrial", desc, [CW + 0.1, len + extra], { maxGrade: 0.15, enterable: !!o.open });
  const zo = o.open ? -0.6 : o.damage ? -0.25 : 0;
  container(A, { len, m: o.m, open: o.open, damage: o.damage, zo });
  if (o.stack) {
    container(A, { len, m: o.m2, y0: CH, zo });
    for (const s of [-1, 1]) for (const e of [-1, 1]) A.box([s * (CW / 2 - 0.12) - 0.03, CH - 0.01, zo + e * (len / 2 - 0.1) - 0.03], [s * (CW / 2 - 0.12) + 0.03, CH + 0.03, zo + e * (len / 2 - 0.1) + 0.03], "steel", D);   // twistlocks
  }
  const hz = len / 2, nav = { cover: swapCover(boxCover(hz, CW / 2, CH * (o.stack ? 2 : 1), { step: 2.4 }), zo) };
  if (o.open) {
    nav.rooms = [room("container", [-CW / 2 + 0.1, 0.16, -hz + zo + 0.1], [CW / 2 - 0.1, CH - 0.15, hz + zo - 0.05])];
    nav.entrances = [{ p: [0, 0, hz + zo + 0.8], dir: [0, 0, -1], room: "container" }];
  }
  return fin(A, nav);
}

function dumpsterFront() {
  const A = mk("mod_dumpster_frontload", "Dumpster (front-load, 3 m3)", "Industrial",
    "Front-load steel waste container, 1.83 x 1.07 x 1.2 m: sloped front, vertical side ribs, fork pockets on both sides, a lip round the top, two black plastic lids hinged at the back, swivel casters.", [1.95, 1.2]);
  const m = "p_green";
  extrudeX(A, -0.9, 0.9, [[-0.5, 0.12], [0.36, 0.12], [0.52, 1.12], [-0.5, 1.12]], m);
  for (const x of [-0.9, 0.9]) {
    A.box([x - 0.015, 0.12, -0.5], [x + 0.015, 1.12, 0.36], m, D);
    for (const z of [-0.3, 0.1]) A.box([x - 0.03, 0.2, z - 0.03], [x + 0.03, 1.08, z + 0.03], m, D);
    A.box([x > 0 ? x : x - 0.18, 0.82, -0.45], [x > 0 ? x + 0.18 : x, 0.98, 0.45], "steel_dk", dv(0.01));       // fork pocket
    A.box([x > 0 ? x + 0.02 : x - 0.17, 0.85, -0.451], [x > 0 ? x + 0.17 : x - 0.02, 0.95, 0.451], "seam", D);
  }
  A.box([-0.93, 1.1, -0.53], [0.93, 1.15, 0.55], m, dv(0.01));
  for (const s of [-1, 1]) {
    A.obox([s * 0.45, 1.19, 0.01], [0.88, 0.06, 1.1], [-3, 0, 0], "pl_black", { k: 0, bv: 0.02 });
    for (const z of [-0.2, 0.2]) A.box([s * 0.45 - 0.35, 1.22, z - 0.02], [s * 0.45 + 0.35, 1.24, z + 0.02], "pl_black", D);
    A.box([s * 0.45 - 0.08, 1.15, 0.54], [s * 0.45 + 0.08, 1.2, 0.58], "pl_black", D);
  }
  for (const x of [-0.75, 0.75]) for (const z of [-0.4, 0.25]) { A.box([x - 0.05, 0.08, z - 0.05], [x + 0.05, 0.12, z + 0.05], "steel_dk", D); A.cyl([x, 0.05, z], [0.1, 0.04, 0.1], "rubber", { ...D, seg: 10, r: [0, 0, 90] }); }
  A.box([-0.93, 0, -0.53], [0.93, 1.22, 0.55], m, H);
  for (const p of A.parts) if (p.t === "p") p.k = 0;
  return fin(A, { cover: boxCover(0.9, 0.53, 1.2) });
}
function dumpsterRolloff() {
  const A = mk("mod_dumpster_rolloff", "Roll-off skip container (20 yd)", "Industrial",
    "Open-top roll-off construction container, 6.7 x 2.4 x 1.5 m: plate walls stiffened by vertical channels, a tube top rail, a rear barn door on heavy hinges with a ratchet latch, long-sill rails and rollers underneath and a cable hook bracket at the front; partly filled with debris.", [2.5, 6.8]);
  const m = "p_blue", hx = 1.2, hz = 3.35, y0 = 0.2, y1 = 1.55, R = rng(77);
  for (const s of [-1, 1]) {
    A.box([s > 0 ? hx - 0.04 : -hx, y0, -hz], [s > 0 ? hx : -hx + 0.04, y1, hz], m, { bv: 0.008 });
    for (let z = -hz + 0.4; z < hz; z += 0.6) A.box([s > 0 ? hx : -hx - 0.06, y0, z - 0.04], [s > 0 ? hx + 0.06 : -hx, y1 - 0.05, z + 0.04], m, D);
    rod(A, [s * (hx + 0.03), y1, -hz], [s * (hx + 0.03), y1, hz], 0.1, m, { seg: 10 });
    A.box([s * 0.55 - 0.08, 0, -hz], [s * 0.55 + 0.08, y0, hz], "steel_dk", V);
    A.cyl([s * 0.55, 0.12, -hz + 0.2], [0.24, 0.14, 0.24], "steel_dk", { ...D, seg: 12, r: [0, 0, 90] });
  }
  A.box([-hx, y0 - 0.04, -hz], [hx, y0, hz], m, { bv: 0.008 });
  A.box([-hx, y0, hz - 0.04], [hx, y1 + 0.3, hz], m, { bv: 0.008 });
  bar(A, [0, y1 + 0.3, hz - 0.02], [0, y1 - 0.1, hz - 0.5], 2.3, 0.04, m, D);
  A.box([-0.25, 0.4, hz], [0.25, 1.0, hz + 0.15], "steel_dk", dv(0.01));
  A.box([-0.08, 0.6, hz + 0.15], [0.08, 0.68, hz + 0.3], "steel", D);
  A.box([-hx, y0, -hz], [hx, y1, -hz + 0.05], m, { bv: 0.008 });                                         // rear door
  seam(A, [-0.003, y0 + 0.05, -hz - 0.004], [0.003, y1 - 0.05, -hz]);
  for (const y of [0.45, 1.25]) for (const s of [-1, 1]) A.cyl([s * (hx + 0.02), y, -hz], [0.08, 0.2, 0.08], "steel_dk", { ...D, seg: 10 });
  A.box([-0.4, 0.8, -hz - 0.08], [0.4, 0.9, -hz - 0.02], "steel", D);
  A.box([-hx + 0.05, y0, -hz + 0.05], [hx - 0.05, 0.6, hz - 0.05], "rubble", V);
  for (let i = 0; i < 14; i++) A.obox([(R() - 0.5) * 2, 0.6 + R() * 0.25, (R() - 0.5) * 6], [0.2 + R() * 0.5, 0.05 + R() * 0.2, 0.2 + R() * 0.8], [R() * 30, R() * 180, R() * 30], R() > 0.5 ? "wood_raw" : R() > 0.5 ? "conc_dk" : "debris", D);
  return fin(A, { cover: swapCover(boxCover(hz, hx, 1.55, { step: 2.2 })) });
}
function louvres(A, x0, x1, y0, y1, z, sz, m) {   // horizontal louvre slats on a face at z (sz outward sign)
  for (let y = y0 + 0.04; y < y1 - 0.02; y += 0.07) A.obox([(x0 + x1) / 2, y, z + sz * 0.012], [x1 - x0, 0.05, 0.012], [sz * 35, 0, 0], m, D);
  A.box([x0 - 0.02, y0 - 0.02, z - 0.004], [x1 + 0.02, y1 + 0.02, z + sz * 0.004], "seam", D);
}
function generatorSet() {
  const A = mk("mod_generator_set", "Diesel generator set (enclosed)", "Industrial",
    "Skid-mounted enclosed diesel generator set, 3.2 x 1.2 x 1.7 m: a steel base frame with fork pockets, an acoustic canopy with gull-wing access doors, inlet and outlet louvres, a roof exhaust stack with rain cap, lifting eyes, a control panel window and a fuel filler.", [3.3, 1.3]);
  const m = "p_yellow", hx = 1.6, hz = 0.6;
  A.box([-hx, 0, -hz], [hx, 0.2, hz], "steel_dk", { bv: 0.01 });
  for (const x of [-0.8, 0.8]) for (const s of [-1, 1]) A.box([x - 0.15, 0.04, s * hz - 0.002], [x + 0.15, 0.14, s * hz + 0.002], "seam", D);
  A.rbox([-hx + 0.02, 0.2, -hz + 0.02], [hx - 0.02, 1.68, hz - 0.02], 0.04, m);
  for (const s of [-1, 1]) {
    for (const x of [-1.0, 0, 1.0]) {
      seam(A, [x - 0.48, 0.3, s * (hz - 0.02) - 0.003], [x + 0.48, 0.305, s * (hz - 0.02) + 0.003]);
      seam(A, [x - 0.48, 1.55, s * (hz - 0.02) - 0.003], [x + 0.48, 1.555, s * (hz - 0.02) + 0.003]);
      seam(A, [x + 0.48, 0.3, s * (hz - 0.02) - 0.003], [x + 0.485, 1.555, s * (hz - 0.02) + 0.003]);
      A.box([x + 0.3, 0.9, s * (hz - 0.02)], [x + 0.36, 1.0, s * (hz - 0.02) + s * 0.03], "steel_dk", D);
    }
  }
  louvres(A, -0.45, 0.45, 0.45, 1.45, 0, 1, m);
  for (const e of [-1, 1]) {
    const x = e * (hx - 0.02);
    for (let y = 0.4; y < 1.5; y += 0.07) A.obox([x + e * 0.012, y, 0], [0.012, 0.05, 0.9], [0, 0, e * -35], m, D);
  }
  A.box([0.8, 0.95, hz - 0.019], [1.25, 1.35, hz - 0.012], "glass_t", D);
  A.cyl([1.1, 1.85, -0.2], [0.2, 0.36, 0.2], "steel", { k: 0, seg: 12 });
  A.cone([1.1, 2.06, -0.2], [0.3, 0.06, 0.3], "steel", { ...D, seg: 12, top: 0.2 });
  for (const x of [-1.2, 1.2]) liftEye(A, x, 1.68, 0, 90, "steel_dk", 0.12);
  A.cyl([-1.2, 0.5, hz - 0.01], [0.1, 0.04, 0.1], "steel_dk", { ...D, seg: 10, r: [90, 0, 0] });
  return fin(A, { cover: boxCover(hx, hz, 1.7, { step: 1.6 }) });
}
function compressor() {
  const A = mk("mod_air_compressor", "Towable air compressor", "Industrial",
    "Portable diesel air compressor on a single-axle trailer, 3.6 x 1.6 x 1.4 m: a rounded lift-up canopy with side louvres, an A-frame drawbar with a towing eye and jockey wheel, mudguards, air outlet valves and hose at the rear, tail lamps.", [1.7, 3.9]);
  const m = "p_yellow";
  A.rbox([-0.7, 0.5, -1.25], [0.7, 1.35, 0.95], 0.12, m);
  for (const s of [-1, 1]) {
    for (const z of [-0.9, 0.5]) for (let y = 0.65; y < 1.15; y += 0.07) A.obox([s * 0.703, y, z], [0.012, 0.05, 0.45], [0, 0, s * 35], m, D);
    seam(A, [s * 0.703 - 0.003, 1.2, -1.2], [s * 0.703 + 0.003, 1.205, 0.9]);
    A.rbox([s > 0 ? 0.7 : -0.85, 0.75, -0.55], [s > 0 ? 0.85 : -0.7, 0.8, 0.25], 0.02, "p_black");
    wheel(A, [s * 0.78, 0.33, -0.15], 0.66, 0.18, s, { rim: "p_dgrey" });
    bar(A, [s * 0.5, 0.48, 0.95], [s * 0.05, 0.45, 1.85], 0.07, 0.07, "steel_dk");
  }
  A.box([-0.6, 0.38, -1.25], [0.6, 0.5, 0.95], "steel_dk", V);
  rod(A, [0, 0.45, 1.85], [0, 0.45, 1.92], 0.08, "steel_dk", { seg: 10 });
  A.cyl([0, 0.45, 1.95], [0.1, 0.03, 0.1], "steel", { ...D, seg: 12, r: [0, 0, 90] });
  rod(A, [0.25, 0.1, 1.3], [0.25, 0.7, 1.3], 0.06, "steel");
  A.cyl([0.25, 0.08, 1.3], [0.16, 0.06, 0.16], "rubber", { ...D, seg: 12, r: [0, 0, 90] });
  for (const x of [-0.3, 0, 0.3]) { A.cyl([x, 0.8, -1.27], [0.05, 0.06, 0.05], "steel", { ...D, seg: 8, r: [90, 0, 0] }); A.box([x - 0.015, 0.83, -1.3], [x + 0.015, 0.9, -1.29], "pl_red", D); }
  rod(A, [0.3, 0.8, -1.3], [0.5, 0.2, -1.6], 0.035, "cable", { seg: 6 });
  for (const s of [-1, 1]) A.box([s * 0.55 - 0.08, 0.55, -1.27], [s * 0.55 + 0.08, 0.65, -1.25], "lamp_r", D);
  A.box([-0.85, 0.3, -1.25], [0.85, 1.35, 0.95], m, H);
  return fin(A, { cover: swapCover(boxCover(1.1, 0.85, 1.35), -0.15) });
}
function hvacUnit() {
  const A = mk("mod_hvac_unit", "HVAC condenser unit", "Industrial",
    "Commercial air-cooled condensing unit, 2.2 x 1.15 x 1.3 m on a concrete pad: galvanised casing with finned coil faces behind guard wires, two top discharge fans under ring guards, an access panel, insulated refrigerant lines and an electrical isolator.", [2.4, 1.35]);
  const hx = 1.1, hz = 0.575, Hh = 1.3;
  A.box([-hx - 0.1, 0, -hz - 0.1], [hx + 0.1, 0.1, hz + 0.1], "conc", { bv: 0.015 });
  for (const s of [-1, 1]) A.box([-hx + 0.05, 0.1, s * (hz - 0.3) - 0.04], [hx - 0.05, 0.16, s * (hz - 0.3) + 0.04], "galv", D);
  A.box([-hx, 0.16, -hz], [hx, Hh, hz], "galv", { bv: 0.01 });
  for (const s of [-1, 1]) {
    A.box([-hx + 0.08, 0.25, s * hz - 0.004], [hx - 0.45, Hh - 0.1, s * hz + 0.004], "steel_dk", D);
    for (let x = -hx + 0.1; x < hx - 0.46; x += 0.035) A.box([x - 0.004, 0.25, s * hz - 0.008], [x + 0.004, Hh - 0.1, s * hz + 0.008], "galv", D);
    for (const y of [0.45, 0.75, 1.05]) rod(A, [-hx + 0.08, y, s * (hz + 0.015)], [hx - 0.45, y, s * (hz + 0.015)], 0.006, "wire", { seg: 4 });
  }
  seam(A, [hx - 0.43, 0.2, hz - 0.002], [hx - 0.425, Hh - 0.05, hz + 0.004]);
  A.box([hx - 0.3, 0.7, hz], [hx - 0.15, 0.95, hz + 0.08], "pl_grey", dv(0.01));
  for (const x of [-0.5, 0.5]) {
    A.cyl([x, Hh + 0.005, 0], [0.85, 0.01, 0.85], "seam", { ...D, seg: 24 });
    for (let k = 0; k < 3; k++) A.obox([x, Hh - 0.05, 0], [0.75, 0.01, 0.16], [12, k * 60, 0], "steel_dk", D);
    for (const r of [0.12, 0.25, 0.38]) ring(A, [x, Hh + 0.04, 0], r, 16, 0.008, 0.008, "y", "wire", D);
    for (let k = 0; k < 4; k++) A.obox([x, Hh + 0.04, 0], [0.8, 0.012, 0.012], [0, k * 45, 0], "wire", D);
  }
  rod(A, [hx - 0.1, 0.45, -hz + 0.1], [hx + 0.12, 0.45, -hz + 0.1], 0.06, "cable", { seg: 8 });
  rod(A, [hx + 0.12, 0.45, -hz + 0.1], [hx + 0.12, 0.12, -hz + 0.1], 0.06, "cable", { seg: 8 });
  return fin(A, { cover: boxCover(hx, hz, 1.3, { step: 1.4 }) });
}
function cableSpool(large) {
  const Dd = large ? 2.0 : 0.95, Wd = large ? 1.2 : 0.6, core = Dd * 0.78;
  const A = mk(large ? "mod_cable_spool_large" : "mod_cable_spool_small", large ? "Cable drum (2 m, on edge)" : "Cable reel (0.95 m, lying flat)", "Industrial",
    large ? "Large timber cable drum standing on its flanges, 2.0 m diameter x 1.2 m wide: planked flanges through-bolted with steel rods, an arbor hole with a steel plate, black power cable wound on the barrel and a tail lashed off, chocked with timber wedges."
      : "Empty-ish timber cable reel lying flat like a table, 0.95 m diameter: planked flanges, a barrel with a few turns of orange cable, through-bolts and an arbor hole.", large ? [1.35, 2.1] : [1.0, 1.0]);
  const plank = (c, axis, rot) => A.obox(c, axis, rot, "ply", D);
  if (large) {
    const yc = Dd / 2;
    for (const s of [-1, 1]) {
      A.cyl([s * (Wd / 2 - 0.04), yc, 0], [Dd, 0.08, Dd], "ply", { seg: 24, r: [0, 0, 90] });
      for (let i = -3; i <= 3; i++) plank([s * (Wd / 2 + 0.002), yc, i * 0.27], [0.004, Dd * Math.sqrt(Math.max(0.05, 1 - ((i * 0.27) / (Dd / 2)) ** 2)) * 0.98, 0.008], [0, 0, 0]);
      A.cyl([s * (Wd / 2 + 0.003), yc, 0], [0.3, 0.012, 0.3], "steel_dk", { ...D, seg: 12, r: [0, 0, 90] });
      A.cyl([s * (Wd / 2 + 0.008), yc, 0], [0.12, 0.012, 0.12], "seam", { ...D, seg: 12, r: [0, 0, 90] });
      for (let k = 0; k < 6; k++) { const a = (k / 6) * 2 * Math.PI; A.cyl([s * (Wd / 2 + 0.005), yc + Math.cos(a) * 0.45, Math.sin(a) * 0.45], [0.04, 0.02, 0.04], "steel_dk", { ...D, seg: 6, r: [0, 0, 90] }); }
    }
    A.cyl([0, yc, 0], [core, Wd - 0.16, core], "cable", { k: 0, seg: 24, r: [0, 0, 90] });
    for (let x = -Wd / 2 + 0.12; x < Wd / 2 - 0.08; x += 0.06) A.cyl([x, yc, 0], [core + 0.01, 0.025, core + 0.01], "cable", { ...D, seg: 24, r: [0, 0, 90] });
    rod(A, [0.3, yc - core / 2, 0.1], [0.45, 0.04, 0.9], 0.05, "cable", { seg: 8 });
    for (const z of [-1, 1]) A.wedge([0, 0.07, z * (Dd / 2 * 0.62)], [Wd + 0.1, 0.14, 0.3], "wood_raw", { r: [0, z > 0 ? 180 : 0, 0], k: 0 });
    return fin(A, { cover: [cover([0, 0, -1.4], 0, "high", ["left", "right"]), cover([0, 0, 1.4], 180, "high", ["left", "right"])] });
  }
  for (const y of [0.035, Wd - 0.035]) {
    A.cyl([0, y, 0], [Dd, 0.07, Dd], "ply", { seg: 24 });
    for (let i = -1; i <= 1; i++) plank([i * 0.3, y + 0.037, 0], [0.008, 0.004, Dd * Math.sqrt(1 - ((i * 0.3) / (Dd / 2)) ** 2) * 0.95], [0, 0, 0]);
  }
  A.cyl([0, Wd / 2, 0], [core, Wd - 0.14, core], "ply", { k: 0, seg: 20 });
  for (let y = 0.1; y < 0.35; y += 0.04) A.cyl([0, y, 0], [core + 0.05, 0.035, core + 0.05], "cable_or", { ...D, seg: 20 });
  A.cyl([0, Wd + 0.002, 0], [0.14, 0.004, 0.14], "seam", { ...D, seg: 12 });
  for (let k = 0; k < 4; k++) { const a = (k / 4) * 2 * Math.PI; A.cyl([Math.cos(a) * 0.25, Wd + 0.005, Math.sin(a) * 0.25], [0.035, 0.01, 0.035], "steel_dk", { ...D, seg: 6 }); }
  return fin(A, { cover: [cover([0, 0, -0.95], 0, "low", ["over"])] });
}
function ibcTote() {
  const A = mk("mod_ibc_tote", "IBC tote (1000 l)", "Industrial",
    "1000-litre intermediate bulk container: a translucent white HDPE bottle in a galvanised tubular grid cage on a steel / timber pallet base, with a screw lid on top and a butterfly drain valve at the front.", [1.25, 1.05]);
  const hx = 0.6, hz = 0.5, y0 = 0.15, y1 = 1.16;
  A.box([-hx, 0, -hz], [hx, 0.03, hz], "galv", { bv: 0.005 });
  for (const x of [-0.5, 0, 0.5]) A.box([x - 0.06, 0.03, -hz], [x + 0.06, 0.12, hz], "wood_raw", vb(0.006));
  A.box([-hx, 0.12, -hz], [hx, y0, hz], "galv", vb(0.005));
  A.rbox([-hx + 0.03, y0, -hz + 0.03], [hx - 0.03, y1 - 0.04, hz - 0.03], 0.06, "hdpe");
  for (const s of [-1, 1]) {
    meshGrid(A, -hx, hx, y0, y1, (u, v) => [u, v, s * (hz + 0.005)], 0.2, 0.25, "galv", 0.02);
    meshGrid(A, -hz, hz, y0, y1, (u, v) => [s * (hx + 0.005), v, u], 0.2, 0.25, "galv", 0.02);
  }
  for (const [x, z] of [[-1, -1], [1, -1], [1, 1], [-1, 1]]) rod(A, [x * hx, y0, z * hz], [x * hx, y1, z * hz], 0.035, "galv", { seg: 8 });
  for (const s of [-1, 1]) { rod(A, [-hx, y1, s * hz], [hx, y1, s * hz], 0.03, "galv", { seg: 8 }); rod(A, [s * hx, y1, -hz], [s * hx, y1, hz], 0.03, "galv", { seg: 8 }); }
  A.cyl([0, y1 - 0.02, 0], [0.18, 0.05, 0.18], "pl_white", { ...D, seg: 14 });
  A.cyl([0, 0.24, hz + 0.02], [0.1, 0.12, 0.1], "pl_black", { ...D, seg: 12, r: [90, 0, 0] });
  A.box([-0.02, 0.24, hz + 0.08], [0.02, 0.36, hz + 0.1], "pl_red", D);
  return fin(A, { cover: boxCover(hx, hz, 1.16, { step: 1.2 }) });
}

// ================================================================================================= SMALL COVER
function palletParts(A, x0, y0, z0, yaw = 0, m = "wood_raw") {
  const L = 1.2, Wp = 0.8, r = (x, z) => { const t = yaw / DEG; return [x0 + x * Math.cos(t) + z * Math.sin(t), z0 - x * Math.sin(t) + z * Math.cos(t)]; };
  const ob = (x, y, z, s, mm = m) => { const [wx, wz] = r(x, z); A.obox([wx, y0 + y, wz], s, [0, yaw, 0], mm, { k: 0, bv: 0.004 }); };
  for (let i = 0; i < 5; i++) ob(0, 0.133, -0.35 + i * 0.175, [L, 0.022, 0.1]);
  for (const z of [-0.35, 0, 0.35]) ob(0, 0.111, z, [L, 0.022, z ? 0.1 : 0.145]);
  for (const x of [-0.525, 0, 0.525]) for (const z of [-0.35, 0, 0.35]) ob(x, 0.061, z, [x ? 0.145 : 0.145, 0.078, z ? 0.1 : 0.145], R_PAL() > 0.7 ? "wood_dk" : m);
  for (const x of [-0.525, 0, 0.525]) ob(x, 0.011, 0, [x ? 0.1 : 0.145, 0.022, Wp]);
}
let R_PAL = rng(1201);
function pallet() {
  const A = mk("mod_pallet", "Wooden pallet", "Small Cover", "Four-way entry block pallet, 1.2 x 0.8 m, 144 mm high: five top deck boards over three stringer boards, nine blocks and three bottom boards in raw softwood.", [1.25, 0.85]);
  R_PAL = rng(1201); palletParts(A, 0, 0, 0);
  A.box([-0.6, 0, -0.4], [0.6, 0.144, 0.4], "wood_raw", H);
  return fin(A, { effect: "none" });
}
function palletStack() {
  const A = mk("mod_pallet_stack", "Stack of pallets", "Small Cover", "Nine wooden block pallets stacked 1.3 m high, slightly out of square, with a loose one leaning against the side.", [2.0, 0.9]);
  const R = rng(1402); R_PAL = rng(1402);
  for (let i = 0; i < 9; i++) palletParts(A, (R() - 0.5) * 0.06, i * 0.145, (R() - 0.5) * 0.05, (R() - 0.5) * 6);
  A.box([-0.62, 0, -0.42], [0.62, 1.305, 0.42], "wood_raw", H);
  const t = 72 / DEG;   // a loose pallet (deck boards and blocks) leaning on the +x side, top against the stack
  for (let i = 0; i < 5; i++) A.obox([0.63 + Math.cos(t) * 0.6, Math.sin(t) * 0.6, -0.35 + i * 0.175], [0.022, 1.2, 0.1], [0, 0, 18], "wood_raw", { k: 0, bv: 0.004 });
  for (const v of [-0.5, 0, 0.5]) for (const z of [-0.35, 0, 0.35]) A.obox([0.63 + Math.cos(t) * (0.6 + v) + 0.05, Math.sin(t) * (0.6 + v), z], [0.078, 0.14, 0.1], [0, 0, 18], "wood_raw", { k: 0, bv: 0.004 });
  return fin(A, { cover: boxCover(0.62, 0.42, 1.3) });
}
function plasticCrate(A, x, y, z, yaw, m) {
  const t = yaw / DEG, at = (dx, dy, dz) => [x + dx * Math.cos(t) + dz * Math.sin(t), y + dy, z - dx * Math.sin(t) + dz * Math.cos(t)];
  const ob = (c, s, mm = m, o = D) => A.obox(at(...c), s, [0, yaw, 0], mm, o);
  ob([0, 0.2, 0], [0.78, 0.36, 0.58], m, { k: 0, bv: 0.02 });
  ob([0, 0.41, 0], [0.8, 0.06, 0.6], m, dv(0.015));
  ob([0, 0.445, 0], [0.7, 0.012, 0.5], m, D);
  for (const s of [-1, 1]) {
    for (let i = -3; i <= 3; i++) ob([i * 0.1, 0.2, s * 0.293], [0.025, 0.32, 0.01]);
    ob([s * 0.395, 0.3, 0], [0.012, 0.06, 0.22], "seam");
    ob([s * 0.402, 0.3, 0], [0.012, 0.025, 0.18], m);
    for (const dx of [-0.2, 0.2]) ob([dx, 0.36, 0.3 * s], [0.07, 0.08, 0.025], "pl_black");
  }
  for (const dx of [-0.32, 0.32]) for (const dz of [-0.22, 0.22]) ob([dx, 0.01, dz], [0.08, 0.02, 0.08], m);
  A.obox(at(0, 0.23, 0), [0.8, 0.46, 0.6], [0, yaw, 0], m, H);
}
function crates(kind) {
  if (kind === "plastic" || kind === "plastic_stack") {
    const stack = kind === "plastic_stack";
    const A = mk(stack ? "mod_crate_plastic_stack" : "mod_crate_plastic", stack ? "Plastic supply crates (stack of 3)" : "Plastic supply crate", "Small Cover",
      stack ? "Three olive and tan moulded polypropylene stackable supply crates: two side by side with one on top." : "Moulded polypropylene stackable supply crate, 0.8 x 0.6 x 0.46 m: ribbed walls, a recessed lid, two over-centre latches each side, end grab handles and stacking feet.",
      stack ? [1.7, 0.75] : [0.85, 0.65]);
    if (!stack) { plasticCrate(A, 0, 0, 0, 0, "pl_olive"); return fin(A, { cover: [] }); }
    plasticCrate(A, -0.42, 0, 0, 3, "pl_olive"); plasticCrate(A, 0.42, 0, 0.02, -2, "pl_tan"); plasticCrate(A, 0.05, 0.47, -0.02, 8, "pl_olive");
    return fin(A, { cover: boxCover(0.84, 0.32, 0.93) });
  }
  const A = mk("mod_crate_wood", "Wooden shipping crate", "Small Cover", "Modern plywood export crate, 1.2 x 0.8 x 0.8 m: plywood panels framed by softwood battens with diagonal braces, steel corner protectors, two skids for forklift entry and a blank stencil panel.", [1.25, 0.85]);
  A.box([-0.6, 0.1, -0.4], [0.6, 0.8, 0.4], "ply", { bv: 0.005 });
  for (const z of [-0.3, 0.3]) A.box([-0.6, 0, z - 0.05], [0.6, 0.1, z + 0.05], "wood_raw", { bv: 0.006 });
  const bat = (a, b) => bar(A, a, b, 0.08, 0.02, "wood_raw", D);
  for (const s of [-1, 1]) {
    const z = s * 0.41;
    for (const y of [0.14, 0.76]) A.box([-0.6, y - 0.04, Math.min(z, z - s * 0.02)], [0.6, y + 0.04, Math.max(z, z - s * 0.02)], "wood_raw", D);
    for (const x of [-0.56, 0.56]) A.box([x - 0.04, 0.1, Math.min(z, z - s * 0.02)], [x + 0.04, 0.8, Math.max(z, z - s * 0.02)], "wood_raw", D);
    A.obox([0, 0.45, z - s * 0.01], [1.25, 0.08, 0.02], [0, 0, 28], "wood_raw", D);
    for (const x of [-0.6, 0.6]) for (const y of [0.1, 0.8]) A.box([x - 0.05, y - 0.05, z - 0.05], [x + 0.05, y + 0.05, z + 0.012 * s], "steel", D);
  }
  for (const s of [-1, 1]) {
    const x = s * 0.61;
    for (const y of [0.14, 0.76]) A.box([Math.min(x, x - s * 0.02), y - 0.04, -0.4], [Math.max(x, x - s * 0.02), y + 0.04, 0.4], "wood_raw", D);
    for (const z of [-0.36, 0.36]) A.box([Math.min(x, x - s * 0.02), 0.1, z - 0.04], [Math.max(x, x - s * 0.02), 0.8, z + 0.04], "wood_raw", D);
  }
  A.box([-0.2, 0.3, 0.42], [0.2, 0.5, 0.425], "plate", D);
  for (const x of [-0.3, 0.3]) bat([x, 0.81, -0.4], [x, 0.81, 0.4]);
  return fin(A, { cover: boxCover(0.6, 0.4, 0.8, { ends: true }) });
}
function drumParts(A, x, z, kind, R) {
  const m = kind === "rust" ? "rust_h" : kind === "plastic" ? "pl_blue" : "p_blue";
  if (kind === "plastic") {
    A.cyl([x, 0.45, z], [0.58, 0.88, 0.58], m, { seg: 20 });
    A.cone([x, 0.91, z], [0.58, 0.04, 0.58], m, { k: 0, seg: 20, top: 0.85 });
    for (const y of [0.3, 0.62]) A.cyl([x, y, z], [0.6, 0.05, 0.6], m, { ...D, seg: 20 });
    for (const dx of [-0.15, 0.15]) A.cyl([x + dx, 0.935, z], [0.07, 0.03, 0.07], "pl_white", { ...D, seg: 10 });
    return;
  }
  A.cyl([x, 0.44, z], [0.572, 0.86, 0.572], m, { seg: 20 });
  for (const y of [0.02, 0.86]) A.cyl([x, y, z], [0.585, 0.035, 0.585], m, { ...D, seg: 20 });
  for (const y of [0.3, 0.58]) A.cyl([x, y, z], [0.592, 0.03, 0.592], m, { ...D, seg: 20 });
  A.cyl([x, 0.871, z], [0.54, 0.004, 0.54], kind === "rust" ? "rust" : "steel_dk", { ...D, seg: 20 });
  A.cyl([x + 0.17, 0.875, z], [0.06, 0.012, 0.06], "steel", { ...D, seg: 8 });
  A.cyl([x - 0.2, 0.875, z], [0.035, 0.012, 0.035], "steel", { ...D, seg: 8 });
  if (kind === "rust") for (let i = 0; i < 3; i++) { const a = R() * 6.28; A.obox([x + Math.sin(a) * 0.288, 0.2 + R() * 0.5, z + Math.cos(a) * 0.288], [0.12, 0.1, 0.02], [R() * 20, a * DEG, 0], "rust_h", D); }
}
function barrel(kind) {
  const ids = { steel: "mod_barrel_steel", rust: "mod_barrel_steel_rust", plastic: "mod_barrel_plastic", group: "mod_barrel_group" };
  const names = { steel: "Steel drum (200 l, blue)", rust: "Steel drum (rusted)", plastic: "Plastic drum (220 l, blue HDPE)", group: "Drums on a pallet (4)" };
  const desc = {
    steel: "Painted 200-litre steel drum, 0.58 m diameter x 0.88 m: rolled chimes, two rolling hoops, a lid with 2 inch and 3/4 inch bungs.",
    rust: "Weathered 200-litre steel drum, rusted through its paint, dented, with its bungs in place.",
    plastic: "Blue HDPE tight-head drum, 0.58 m x 0.93 m, with two moulded rolling rings and two white bung caps.",
    group: "Four drums, steel and plastic, standing on a wooden block pallet.",
  };
  const A = mk(ids[kind], names[kind], "Small Cover", desc[kind], kind === "group" ? [1.25, 1.45] : [0.62, 0.62]);
  const R = rng(kind.length * 17);
  if (kind === "group") {   // two drums on a pallet, two more on the ground in front of it
    R_PAL = rng(77);
    palletParts(A, 0, 0, -0.3, 0);
    A.box([-0.6, 0, -0.7], [0.6, 0.144, 0.1], "wood_raw", H);
    for (const [x, kk] of [[-0.3, "steel"], [0.3, "plastic"]]) { const s = A.parts.length; drumParts(A, x, -0.3, kk, R); shiftY(A, s, 0.144); }
    drumParts(A, -0.3, 0.42, "rust", R); drumParts(A, 0.31, 0.42, "steel", R);
    return fin(A, { cover: [cover([0, 0, -1.0], 0, "low", ["over", "left", "right"]), cover([0, 0, 1.0], 180, "low", ["over", "left", "right"])] });
  }
  drumParts(A, 0, 0, kind, R);
  return fin(A, { cover: [] });
}
function toolChest() {
  const A = mk("mod_tool_chest", "Rolling tool chest", "Small Cover", "Red steel workshop tool chest, 0.68 x 0.46 x 1.0 m: a six-drawer roller cabinet on swivel casters with a three-drawer top chest and hinged lid, aluminium drawer pulls, a side push handle and a rubber top mat.", [0.85, 0.5]);
  const m = "p_red", hx = 0.34, hz = 0.23;
  A.box([-hx, 0.1, -hz], [hx, 0.7, hz], m, { bv: 0.008 });
  A.box([-hx + 0.01, 0.72, -hz + 0.03], [hx - 0.01, 1.0, hz - 0.02], m, { bv: 0.008 });
  A.box([-hx - 0.005, 0.7, -hz], [hx + 0.005, 0.72, hz], "rubber", D);
  const drawers = (y0, y1, n, z) => { for (let i = 0; i < n; i++) { const a = y0 + (i * (y1 - y0)) / n, b = y0 + ((i + 1) * (y1 - y0)) / n; seam(A, [-hx + 0.02, b - 0.004, z], [hx - 0.02, b, z + 0.003]); A.box([-0.15, b - 0.035, z], [0.15, b - 0.02, z + 0.02], "chrome", D); } };
  drawers(0.12, 0.68, 6, hz); drawers(0.74, 0.92, 3, hz - 0.02);
  seam(A, [-hx + 0.01, 0.93, hz - 0.02], [hx - 0.01, 0.935, hz - 0.017]);
  for (const s of [-1, 1]) A.box([s * hx - 0.005, 0.1, hz - 0.01], [s * hx + 0.005, 0.68, hz + 0.004], "steel", D);
  rod(A, [hx + 0.06, 0.45, -0.15], [hx + 0.06, 0.45, 0.15], 0.025, "steel", { seg: 8 });
  for (const z of [-0.15, 0.15]) rod(A, [hx, 0.45, z], [hx + 0.06, 0.45, z], 0.02, "steel", { seg: 6 });
  for (const x of [-0.28, 0.28]) for (const z of [-0.18, 0.18]) { A.box([x - 0.04, 0.085, z - 0.04], [x + 0.04, 0.1, z + 0.04], "steel_dk", D); A.cyl([x, 0.045, z], [0.09, 0.035, 0.09], "rubber", { ...D, seg: 12, r: [0, 0, 90] }); }
  A.box([-hx, 0, -hz], [hx, 1.0, hz], m, H);
  return fin(A, { cover: [cover([0, 0, -0.75], 0, "low", ["over", "left", "right"])] });
}
function jerryCans() {
  const A = mk("mod_jerry_cans", "Jerry cans (group of 3)", "Small Cover", "Three 20-litre steel jerry cans (two olive, one red): pressed X-ribs on both faces, triple carry handles, a cam-lever spout cap and a welded seam round the body.", [0.75, 0.55]);
  const can = (x, z, yaw, m) => {
    const t = yaw / DEG, at = (dx, dy, dz) => [x + dx * Math.cos(t) + dz * Math.sin(t), dy, z - dx * Math.sin(t) + dz * Math.cos(t)];
    A.obox(at(0, 0.235, 0), [0.345, 0.43, 0.165], [0, yaw, 0], m, { bv: 0.015 });
    for (const s of [-1, 1]) for (const a of [-1, 1]) A.obox(at(0, 0.22, s * 0.084), [0.4, 0.035, 0.008], [0, yaw, a * 46], m, D);
    A.obox(at(0, 0.235, 0), [0.35, 0.43, 0.012], [0, yaw, 0], "seam", D);
    for (const dx of [-0.1, 0, 0.1]) A.obox(at(dx, 0.48, 0), [0.025, 0.04, 0.1], [0, yaw, 0], m, D);
    A.obox(at(-0.13, 0.475, 0), [0.07, 0.05, 0.07], [0, yaw, 0], m, D);
    A.obox(at(-0.13, 0.5, 0.04), [0.02, 0.02, 0.1], [0, yaw, 0], "steel", D);
  };
  can(-0.2, 0.05, 4, "p_od"); can(0.17, 0.08, -6, "p_od"); can(0.0, -0.17, 85, "p_red");
  return fin(A, { effect: "carve" });
}

// ================================================================================================= URBAN
function busShelter() {
  const A = mk("mod_bus_shelter", "Bus shelter", "Urban",
    "Glazed steel bus shelter, 3.6 x 1.5 x 2.5 m: four box-section posts, a toughened-glass back wall and one glazed end with a blank backlit advert panel in the other, a cantilevered roof canopy with fascia and gutter, a steel perch bench and a litter bin on a post.", [3.9, 1.8]);
  const hx = 1.8, zb = -0.62, zf = 0.62, Hh = 2.42;
  for (const [x, z] of [[-hx, zb], [hx, zb], [-hx, zf], [hx, zf]]) A.box([x - 0.04, 0, z - 0.04], [x + 0.04, Hh, z + 0.04], "p_dgrey", { bv: 0.006 });
  A.box([-hx + 0.04, 0.12, zb - 0.008], [hx - 0.04, Hh - 0.1, zb + 0.008], "glass");
  for (const y of [0.1, Hh - 0.1]) A.box([-hx, y - 0.03, zb - 0.03], [hx, y + 0.03, zb + 0.03], "p_dgrey", vb(0.004));
  for (const x of [-0.6, 0.6]) A.box([x - 0.025, 0.12, zb - 0.02], [x + 0.025, Hh - 0.1, zb + 0.02], "p_dgrey", D);
  A.box([-hx - 0.008, 0.12, zb + 0.04], [-hx + 0.008, Hh - 0.1, zf - 0.04], "glass");
  A.box([hx - 0.08, 0.12, zb + 0.04], [hx + 0.08, Hh - 0.1, zf - 0.04], "p_dgrey", { bv: 0.01 });
  for (const s of [-1, 1]) A.box([hx + s * 0.081 - 0.002, 0.3, zb + 0.1], [hx + s * 0.081 + 0.002, Hh - 0.25, zf - 0.1], "ad_panel", D);
  A.obox([0, Hh + 0.06, 0.05], [3.9, 0.08, 1.75], [3, 0, 0], "p_dgrey", { bv: 0.01 });
  A.box([-1.95, Hh + 0.0, 0.9], [1.95, Hh + 0.12, 0.94], "p_dgrey", D);
  A.box([-1.75, Hh - 0.005, -0.4], [1.75, Hh + 0.0, -0.3], "lamp_w", D);
  rod(A, [-hx + 0.05, 0.6, zb + 0.15], [hx - 0.15, 0.6, zb + 0.15], 0.05, "steel", { k: 0, seg: 8 });
  for (let i = 0; i < 4; i++) A.box([-1.5 + i * 0.95, 0.68, zb + 0.06], [-1.2 + i * 0.95, 0.72, zb + 0.4], "steel", dv(0.006));
  for (const x of [-1.3, 0, 1.3]) A.box([x - 0.03, 0, zb + 0.18], [x + 0.03, 0.7, zb + 0.24], "steel", D);
  A.cyl([-hx - 0.25, 0.75, zf], [0.38, 0.6, 0.38], "p_dgrey", { k: 0, seg: 14 });
  A.cyl([-hx - 0.25, 1.06, zf], [0.4, 0.04, 0.4], "p_dgrey", { ...D, seg: 14 });
  rod(A, [-hx - 0.04, 0.9, zf], [-hx - 0.06, 0.9, zf], 0.06, "p_dgrey");
  return fin(A, { cover: [cover([0.9, 0, zb - 0.55], 0, "high", ["left", "right"]), cover([hx + 0.55, 0, 0], 270, "high", ["left", "right"])] });
}
function streetLight(double) {
  const Hh = 8.0;
  const A = mk(double ? "mod_streetlight_double" : "mod_streetlight", double ? "Street light (double arm)" : "Street light (single arm)", "Urban",
    `Galvanised tapered steel lighting column, 8 m, on a flanged base with an access door, carrying ${double ? "two 1.6 m outreach arms with LED lanterns, for a central median" : "a 1.6 m outreach arm with a flat LED lantern"}.`, double ? [4.5, 0.7] : [0.7, 2.4]);
  const zo = double ? 0 : -1.0;
  A.cyl([0, 0.35, zo], [0.26, 0.7, 0.26], "galv", { seg: 14 });
  A.cone([0, 0.7 + (Hh - 0.7) / 2, zo], [0.2, Hh - 0.7, 0.2], "galv", { seg: 14, top: 0.45 });
  A.box([-0.2, 0, zo - 0.2], [0.2, 0.025, zo + 0.2], "steel_dk", dv(0.004));
  for (const [x, z] of [[-1, -1], [1, -1], [1, 1], [-1, 1]]) A.cyl([x * 0.15, 0.04, zo + z * 0.15], [0.035, 0.03, 0.035], "steel", { ...D, seg: 6 });
  A.box([-0.05, 0.45, zo + 0.128], [0.05, 0.75, zo + 0.134], "galv", D);
  boltRow(A, [0, 0.72, zo + 0.135], [0, 0.72, zo + 0.135], 1, "z");
  const arms = double ? [[1, 0], [-1, 0]] : [[0, 1]];
  for (const [ax, az] of arms) {
    const p0 = [0, Hh - 0.2, zo], p1 = [ax * 1.6, Hh + 0.15, zo + az * 1.6];
    rod(A, p0, p1, 0.07, "galv", { seg: 10 });
    rod(A, [0, Hh - 0.8, zo], [ax * 0.6, Hh - 0.05, zo + az * 0.6], 0.04, "galv", { seg: 8 });
    const c = [ax * 1.85, Hh + 0.1, zo + az * 1.85], yaw = Math.atan2(ax, az) * DEG;
    A.obox(c, [0.32, 0.1, 0.65], [5 * (az || ax), yaw, 0], "p_dgrey", { k: 0, bv: 0.02 });
    A.obox([c[0], c[1] - 0.055, c[2]], [0.26, 0.012, 0.5], [5 * (az || ax), yaw, 0], "lamp_w", D);
    for (let i = 0; i < 4; i++) A.obox([c[0] - ax * 0.15 + ax * i * 0.06, c[1] + 0.055, c[2] - az * 0.15 + az * i * 0.06], [0.28, 0.012, 0.025], [0, yaw + 90, 0], "p_dgrey", D);
  }
  A.cone([0, Hh + 0.02, zo], [0.1, 0.06, 0.1], "galv", { ...D, seg: 10, top: 0.3 });
  return fin(A);
}
function trafficLight() {
  const A = mk("mod_traffic_light", "Traffic signal pole", "Urban",
    "Traffic signal on a 3.6 m galvanised pole: a three-aspect signal head (red, amber, green lenses as coloured panels) with sun visors and a backboard, a two-aspect pedestrian head below, a push-button unit and a base cabinet collar.", [0.8, 0.8]);
  A.cyl([0, 1.8, 0], [0.114, 3.6, 0.114], "galv", { seg: 12 });
  A.cyl([0, 0.15, 0], [0.2, 0.3, 0.2], "galv", { k: 0, seg: 12 });
  A.cone([0, 3.62, 0], [0.12, 0.04, 0.12], "galv", { ...D, seg: 12, top: 0.4 });
  const head = (y, n, lens, w = 0.3) => {
    const hgt = n * 0.3 + 0.1, z = 0.16;
    A.box([-w / 2, y, z - 0.12], [w / 2, y + hgt, z + 0.12], "p_black", { k: 0, bv: 0.015 });
    A.box([-w / 2 - 0.12, y - 0.08, z - 0.13], [w / 2 + 0.12, y + hgt + 0.08, z - 0.12], "p_black", D);
    for (const s of [-1, 1]) A.box([s * (w / 2 + 0.1) - 0.02, y - 0.08, z - 0.131], [s * (w / 2 + 0.1) + 0.02, y + hgt + 0.08, z - 0.128], "refl_w", D);
    for (let i = 0; i < n; i++) {
      const yy = y + hgt - 0.2 - i * 0.3;
      A.cyl([0, yy, z + 0.121], [0.21, 0.01, 0.21], lens[i], { ...D, seg: 16, r: [90, 0, 0] });
      A.arch([0, yy + 0.02, z + 0.2], [0.26, 0.13, 0.16], "p_black", { ...D, th: 0.012 });
    }
    rod(A, [0, y + hgt / 2, 0.04], [0, y + hgt / 2, 0.06], 0.06, "galv");
  };
  head(2.45, 3, ["lamp_r", "lamp_a", "lamp_g"]);
  head(1.6, 2, ["lamp_r", "lamp_g"], 0.26);
  A.box([-0.08, 1.0, 0.055], [0.08, 1.25, 0.12], "pl_yellow", dv(0.01));
  A.cyl([0, 1.1, 0.125], [0.05, 0.01, 0.05], "p_black", { ...D, seg: 10, r: [90, 0, 0] });
  return fin(A);
}
function utilityPole() {
  const A = mk("mod_utility_pole", "Utility pole with transformer", "Urban",
    "Creosoted timber distribution pole, 11 m: a 2.4 m crossarm with three pin insulators and conductor stubs, a lower double crossarm with suspension insulators, a pole-mounted transformer can on a bracket with bushings and cutout fuses, a ground wire, step bolts and a pole tag.", [2.6, 2.3]);
  const Hh = 11;
  A.cone([0, 4.9, 0], [0.32, 11.4, 0.32], "wood_dk", { seg: 14, top: 0.7 });   // 0.8 m of it set in the ground
  const arm = (y, len, ins) => {
    A.box([-len / 2, y - 0.05, -0.18], [len / 2, y + 0.05, -0.08], "wood_dk", { k: 0, bv: 0.01 });
    for (const s of [-1, 1]) bar(A, [s * 0.05, y - 0.6, -0.15], [s * len * 0.35, y - 0.05, -0.13], 0.02, 0.04, "galv", D);
    for (const x of ins) {
      if (y > 9.8) {
        A.cyl([x, y + 0.08, -0.13], [0.03, 0.12, 0.03], "galv", { ...D, seg: 6 });
        A.cone([x, y + 0.2, -0.13], [0.12, 0.16, 0.12], "pl_white", { ...D, seg: 10, top: 0.5 });
        rod(A, [x, y + 0.28, -0.13], [x, y + 0.28, 0.9], 0.012, "cable", { seg: 4 });
        rod(A, [x, y + 0.28, -0.13], [x, y + 0.26, -1.1], 0.012, "cable", { seg: 4 });
      } else {
        for (let k = 0; k < 4; k++) A.cyl([x, y - 0.12 - k * 0.07, -0.13], [0.15, 0.03, 0.15], "pl_grey", { ...D, seg: 10 });
        rod(A, [x, y - 0.42, -0.13], [x, y - 0.5, 0.9], 0.012, "cable", { seg: 4 });
      }
    }
  };
  arm(10.2, 2.4, [-1.05, 0, 1.05]);
  arm(9.3, 2.0, [-0.85, 0.85]);
  // transformer
  A.box([-0.08, 7.3, 0.12], [0.08, 8.3, 0.2], "galv", D);
  A.cyl([0, 7.75, 0.55], [0.55, 0.95, 0.55], "p_grey", { k: 0, seg: 16 });
  A.cyl([0, 8.25, 0.55], [0.58, 0.05, 0.58], "p_grey", { ...D, seg: 16 });
  for (const x of [-0.15, 0.15]) { A.cone([x, 8.38, 0.55], [0.08, 0.2, 0.08], "pl_grey", { ...D, seg: 8, top: 0.5 }); rod(A, [x, 8.48, 0.55], [x * 2, 9.0, -0.13], 0.01, "cable", { seg: 4 }); }
  for (const [x, y] of [[-0.15, 7.55], [0, 7.5], [0.15, 7.55]]) A.cone([x, y, 0.84], [0.06, 0.1, 0.06], "pl_grey", { ...D, seg: 8, top: 0.5, r: [90, 0, 0] });
  for (const x of [-0.6, 0.6]) { bar(A, [x, 8.9, -0.12], [x, 9.15, -0.12], 0.04, 0.04, "pl_grey", D); A.cyl([x, 8.85, -0.12], [0.05, 0.25, 0.05], "pl_grey", { ...D, seg: 8 }); }
  rod(A, [0, 7.3, 0.36], [0.06, 0.2, 0.17], 0.014, "cable", { seg: 4 });
  rod(A, [0.16, 0.2, 0], [0.16, 10.5, 0], 0.012, "wire", { seg: 4 });
  A.box([0.14, 0.2, -0.02], [0.18, 2.6, 0.02], "steel_dk", D);
  for (let y = 2.6, i = 0; y < 9; y += 0.45, i++) { const a = (i % 2 ? 1 : -1) * 90; rod(A, [0, y, 0], [Math.sin(a / DEG) * 0.3, y, Math.cos(a / DEG) * 0.3], 0.02, "galv", { seg: 6 }); }
  A.box([-0.06, 1.9, 0.157], [0.06, 2.0, 0.162], "plate", D);
  return fin(A, { cover: [] });
}
function electricalCabinet() {
  const A = mk("mod_electrical_cabinet", "Electrical street cabinet", "Urban",
    "Green steel roadside electrical / telecom cabinet, 1.2 x 0.45 x 1.4 m on a concrete plinth: double doors with a three-point lock handle, a rain hood, side louvres and a blank warning plate.", [1.35, 0.6]);
  const hx = 0.6, hz = 0.225;
  A.box([-hx - 0.05, 0, -hz - 0.05], [hx + 0.05, 0.15, hz + 0.05], "conc", { bv: 0.01 });
  A.box([-hx, 0.15, -hz], [hx, 1.38, hz], "p_green", { bv: 0.01 });
  A.box([-hx - 0.03, 1.38, -hz - 0.05], [hx + 0.03, 1.43, hz + 0.05], "p_green", dv(0.01));
  seam(A, [-0.003, 0.2, hz], [0.003, 1.33, hz + 0.003]);
  for (const s of [-1, 1]) {
    seam(A, [s * (hx - 0.03) - 0.003, 0.2, hz], [s * (hx - 0.03) + 0.003, 1.33, hz + 0.003]);
    for (let y = 0.4; y < 1.2; y += 0.06) A.obox([s * (hx + 0.008), y, 0], [0.01, 0.035, 0.25], [0, 0, s * -30], "p_green", D);
  }
  seam(A, [-hx + 0.03, 1.33, hz], [hx - 0.03, 1.335, hz + 0.003]);
  A.box([0.05, 0.75, hz], [0.08, 0.95, hz + 0.04], "steel_dk", D);
  A.box([-0.4, 1.05, hz], [-0.2, 1.2, hz + 0.004], "pl_yellow", D);
  return fin(A, { cover: boxCover(hx, hz, 1.4) });
}
function fireHydrant() {
  const A = mk("mod_fire_hydrant", "Fire hydrant", "Urban",
    "Cast-iron dry-barrel fire hydrant, 0.85 m: a bolted ground flange, red barrel, yellow bonnet with a pentagon operating nut, two hose nozzles and a large pumper nozzle with chained caps.", [0.5, 0.5]);
  A.cyl([0, 0.03, 0], [0.32, 0.06, 0.32], "p_red", { seg: 16 });
  for (let i = 0; i < 6; i++) { const a = (i / 6) * 2 * Math.PI; A.cyl([Math.cos(a) * 0.13, 0.065, Math.sin(a) * 0.13], [0.025, 0.02, 0.025], "steel_dk", { ...D, seg: 6 }); }
  A.cyl([0, 0.35, 0], [0.22, 0.6, 0.22], "p_red", { seg: 16 });
  A.cyl([0, 0.66, 0], [0.26, 0.04, 0.26], "p_red", { ...D, seg: 16 });
  A.cone([0, 0.74, 0], [0.24, 0.12, 0.24], "p_yellow", { k: 0, seg: 16, top: 0.4 });
  A.cyl([0, 0.82, 0], [0.06, 0.05, 0.06], "p_yellow", { ...D, seg: 5 });
  for (const [x, z, d] of [[0.15, 0, 0.09], [-0.15, 0, 0.09], [0, 0.15, 0.14]]) {
    const r = x ? [0, 0, 90] : [90, 0, 0], dir = x ? [Math.sign(x), 0, 0] : [0, 0, 1];
    A.cyl([x, 0.45, z], [d, 0.08, d], "p_red", { ...D, seg: 12, r });
    A.cyl([x + dir[0] * 0.05, 0.45, z + dir[2] * 0.05], [d + 0.02, 0.03, d + 0.02], "p_yellow", { ...D, seg: 6, r });
    rod(A, [x * 0.8 + dir[0] * 0.05, 0.4, z * 0.8 + dir[2] * 0.05], [x * 0.6, 0.55, z * 0.6], 0.008, "steel", { seg: 4 });
  }
  return fin(A);
}
function mailbox() {
  const A = mk("mod_mailbox", "Mail collection box", "Urban",
    "Generic dark-green steel mail collection box on a square pedestal, 0.5 x 0.45 x 1.25 m: domed top, a hooded letter slot, a front door with a lock and a blank collection-times plate.", [0.55, 0.5]);
  A.box([-0.1, 0, -0.1], [0.1, 0.35, 0.1], "p_green", { bv: 0.008 });
  A.box([-0.16, 0, -0.16], [0.16, 0.02, 0.16], "p_green", D);
  A.box([-0.25, 0.35, -0.22], [0.25, 1.08, 0.22], "p_green", { bv: 0.015 });
  A.cyl([0, 1.08, 0], [0.5, 0.3, 0.44], "p_green", { k: 0, seg: 20, r: [0, 0, 90], top: 1 });
  A.box([-0.25, 1.08, -0.22], [0.25, 1.09, 0.22], "p_green", D);
  A.box([-0.15, 0.95, 0.22], [0.15, 0.99, 0.224], "seam", D);
  A.obox([0, 1.0, 0.25], [0.34, 0.012, 0.07], [30, 0, 0], "p_green", D);
  seam(A, [-0.2, 0.42, 0.22], [0.2, 0.425, 0.224]); seam(A, [-0.2, 0.85, 0.22], [0.2, 0.855, 0.224]);
  A.cyl([0.14, 0.75, 0.226], [0.035, 0.01, 0.035], "steel", { ...D, seg: 8, r: [90, 0, 0] });
  A.box([-0.12, 0.6, 0.22], [0.08, 0.75, 0.225], "plate", D);
  return fin(A);
}
function trashBin() {
  const A = mk("mod_trash_bin", "Litter bin (slatted steel)", "Urban",
    "Round municipal litter bin, 0.55 m diameter x 1.0 m: vertical steel slats round a black liner, a domed lid with a side opening, a cast base ring bolted down.", [0.6, 0.6]);
  A.cyl([0, 0.04, 0], [0.56, 0.08, 0.56], "p_dgrey", { seg: 20 });
  A.cyl([0, 0.5, 0], [0.48, 0.84, 0.48], "fab_blk", { seg: 16 });
  ring(A, [0, 0.5, 0], 0.255, 22, 0.012, 0.84, "y", "p_dgrey", V);
  for (const y of [0.15, 0.85]) A.cyl([0, y, 0], [0.54, 0.04, 0.54], "p_dgrey", { ...D, seg: 20 });
  A.cone([0, 0.98, 0], [0.56, 0.12, 0.56], "p_dgrey", { k: 0, seg: 20, top: 0.35 });
  A.box([-0.12, 0.92, 0.18], [0.12, 0.98, 0.29], "seam", D);
  return fin(A);
}
function newspaperBox() {
  const A = mk("mod_newspaper_box", "Newspaper vending box", "Urban",
    "Generic coin-operated newspaper vending box on legs, 0.5 x 0.45 x 1.15 m: a window in the drop-front door, a coin mechanism hood and pull handle; no lettering.", [0.55, 0.5]);
  for (const [x, z] of [[-1, -1], [1, -1], [1, 1], [-1, 1]]) A.box([x * 0.2 - 0.02, 0, z * 0.18 - 0.02], [x * 0.2 + 0.02, 0.35, z * 0.18 + 0.02], "steel_dk", D);
  A.box([-0.25, 0.35, -0.22], [0.25, 1.05, 0.22], "p_blue", { bv: 0.012 });
  A.box([-0.2, 0.55, 0.22], [0.2, 0.92, 0.226], "glass", D);
  seam(A, [-0.23, 0.5, 0.22], [0.23, 0.505, 0.224]);
  A.box([-0.08, 0.5, 0.222], [0.08, 0.53, 0.25], "steel", D);
  A.rbox([-0.18, 1.05, -0.05], [0.18, 1.15, 0.2], 0.03, "steel_dk");
  A.box([0.04, 1.08, 0.2], [0.12, 1.11, 0.205], "seam", D);
  A.box([-0.25, 0.35, -0.22], [0.25, 0.35, 0.22], "steel_dk", H);
  return fin(A);
}
function parkBench() {
  const A = mk("mod_bench", "Park bench (steel and timber)", "Urban",
    "Street / park bench, 1.8 m: two cast-steel end frames with armrests, five hardwood seat slats and three back slats on bolted brackets.", [1.9, 0.75]);
  for (const x of [-0.82, 0.82]) {
    bar(A, [x, 0, 0.25], [x, 0.43, 0.18], 0.06, 0.05, "p_black", { bv: 0.006 });
    bar(A, [x, 0, -0.25], [x, 0.45, -0.2], 0.06, 0.05, "p_black", { bv: 0.006 });
    bar(A, [x, 0.42, 0.2], [x, 0.42, -0.25], 0.06, 0.05, "p_black", { bv: 0.006 });
    bar(A, [x, 0.42, -0.2], [x, 0.88, -0.3], 0.06, 0.05, "p_black", { bv: 0.006 });
    bar(A, [x, 0.66, 0.24], [x, 0.66, -0.25], 0.06, 0.04, "p_black", dv(0.01));
    bar(A, [x, 0.42, 0.22], [x, 0.66, 0.24], 0.05, 0.04, "p_black", D);
  }
  for (let i = 0; i < 5; i++) A.box([-0.9, 0.43, 0.2 - i * 0.09 - 0.04], [0.9, 0.465, 0.2 - i * 0.09 + 0.035], "wood_oak", { k: i % 2 ? 0 : 1, bv: 0.006 });
  for (let i = 0; i < 3; i++) { const y = 0.55 + i * 0.12, z = -0.22 - (y - 0.42) * 0.2; A.obox([0, y, z], [1.8, 0.09, 0.03], [-12, 0, 0], "wood_oak", { k: 0, bv: 0.006 }); }
  for (const x of [-0.82, 0.82]) for (let i = 0; i < 5; i++) A.cyl([x * 1.04, 0.47, 0.2 - i * 0.09], [0.015, 0.01, 0.015], "steel", { ...D, seg: 6 });
  return fin(A, { effect: "carve", cover: [cover([0, 0, -0.75], 0, "low", ["over"])] });
}
function bikeRack() {
  const A = mk("mod_bike_rack", "Bike rack (5 hoops)", "Urban",
    "Galvanised steel cycle stands: five inverted-U hoops, 0.75 m high, on two bolted ground rails, 4 m run.", [4.2, 0.75]);
  for (let i = 0; i < 5; i++) {
    const x = -1.6 + i * 0.8;
    for (const s of [-1, 1]) rod(A, [x, 0.04, s * 0.3], [x, 0.5, s * 0.3], 0.048, "galv", { k: 1, seg: 8 });
    A.arch([x, 0.665, 0], [0.648, 0.33, 0.048], "galv", { k: 0, th: 0.048, r: [0, 90, 0] });
    rod(A, [x, 0.3, -0.3], [x, 0.3, 0.3], 0.03, "galv", { seg: 6 });
  }
  for (const s of [-1, 1]) A.box([-2.0, 0, s * 0.3 - 0.04], [2.0, 0.04, s * 0.3 + 0.04], "galv", vb(0.005));
  for (let i = 0; i < 6; i++) for (const s of [-1, 1]) A.cyl([-2 + 0.1 + i * 0.76, 0.045, s * 0.3], [0.03, 0.012, 0.03], "steel_dk", { ...D, seg: 6 });
  return fin(A, { effect: "carve" });
}
function parkingMeter() {
  const A = mk("mod_parking_meter", "Parking meter", "Urban",
    "Single-space electronic parking meter, 1.5 m: a steel post with a domed grey head, dark display window, coin slot and card reader.", [0.35, 0.35]);
  A.cyl([0, 0.55, 0], [0.07, 1.1, 0.07], "p_dgrey", { seg: 10 });
  A.cyl([0, 0.02, 0], [0.18, 0.04, 0.18], "p_dgrey", { ...D, seg: 10 });
  A.rbox([-0.11, 1.1, -0.09], [0.11, 1.42, 0.09], 0.04, "p_grey");
  A.cyl([0, 1.42, 0], [0.22, 0.16, 0.18], "p_grey", { k: 0, seg: 16, r: [0, 0, 90] });
  A.box([-0.07, 1.25, 0.09], [0.07, 1.36, 0.095], "screen_off", D);
  A.box([-0.03, 1.18, 0.09], [0.03, 1.2, 0.096], "seam", D);
  A.box([-0.06, 1.13, 0.09], [0.06, 1.16, 0.1], "p_black", D);
  return fin(A);
}
function signPost(kind) {
  const A = mk(kind === "stop" ? "mod_sign_post_stop" : "mod_sign_post_street", kind === "stop" ? "Sign post (octagonal, blank)" : "Sign post (street blades, blank)", "Urban",
    kind === "stop" ? "Galvanised sign post with a blank red octagonal regulatory plate with a white border and a small blank white supplementary plate below."
      : "Corner sign post with two blank green street-name blades crossed at the top, a blank blue round plate and a blank red-bordered triangular warning plate.", [0.9, 0.9]);
  A.cyl([0, 1.4, 0], [0.06, 2.8, 0.06], "galv", { seg: 10 });
  A.cyl([0, 2.81, 0], [0.065, 0.02, 0.065], "galv", { ...D, seg: 10 });
  const clamp = (y) => A.box([-0.04, y - 0.03, 0.025], [0.04, y + 0.03, 0.045], "steel", D);
  if (kind === "stop") {
    A.cyl([0, 2.25, 0.05], [0.75, 0.012, 0.75], "refl_w", { k: 0, n: 8, r: [90, 0, 22.5] });
    A.cyl([0, 2.25, 0.057], [0.69, 0.006, 0.69], "refl_r", { ...D, n: 8, r: [90, 0, 22.5] });
    A.box([-0.25, 1.65, 0.044], [0.25, 1.82, 0.056], "refl_w", D);
    for (const y of [1.98, 2.5, 1.73]) clamp(y);
    return fin(A);
  }
  for (const [y, yaw] of [[2.72, 0], [2.6, 90]]) {
    A.obox([0, y, 0], [0.8, 0.15, 0.012], [0, yaw, 0], "pl_green", { k: 0, bv: 0.004 });
    A.obox([0, y, 0], [0.78, 0.13, 0.016], [0, yaw, 0], "refl_w", D);
    A.obox([0, y, 0], [0.76, 0.11, 0.02], [0, yaw, 0], "pl_green", D);
  }
  A.cyl([0, 2.1, 0.05], [0.6, 0.012, 0.6], "pl_blue", { k: 0, seg: 24, r: [90, 0, 0] });
  A.cyl([0, 2.1, 0.057], [0.56, 0.004, 0.56], "refl_w", { ...D, seg: 24, r: [90, 0, 0] });
  A.cyl([0, 2.1, 0.06], [0.52, 0.004, 0.52], "pl_blue", { ...D, seg: 24, r: [90, 0, 0] });
  A.prism(2, 0.044, 0.056, [[-0.35, 1.25], [0.35, 1.25], [0, 1.86]], "refl_r", { k: 0 });
  A.prism(2, 0.056, 0.06, [[-0.27, 1.3], [0.27, 1.3], [0, 1.77]], "refl_w", { k: 0 });
  for (const y of [1.4, 1.75, 1.95, 2.3]) clamp(y);
  return fin(A);
}
function manhole() {
  const A = mk("mod_manhole_cover", "Manhole cover", "Urban",
    "Cast-iron manhole cover, 0.65 m, in its frame set in an asphalt patch ring, with a raised grip pattern and two lifting keyholes. Flush: walk and drive over.", [0.9, 0.9]);
  A.cyl([0, 0.004, 0], [0.88, 0.008, 0.88], "asphalt", { ...V, seg: 24 });
  A.cyl([0, 0.009, 0], [0.72, 0.008, 0.72], "iron", { ...V, seg: 24 });
  A.cyl([0, 0.013, 0], [0.65, 0.006, 0.65], "iron", { ...V, seg: 24 });
  for (const r of [0.12, 0.2, 0.28]) ring(A, [0, 0.017, 0], r, 18, 0.012, 0.004, "y", "iron", D);
  for (let k = 0; k < 8; k++) A.obox([0, 0.017, 0], [0.012, 0.004, 0.6], [0, k * 22.5, 0], "iron", D);
  for (const x of [-0.24, 0.24]) A.box([x - 0.02, 0.0165, -0.008], [x + 0.02, 0.0175, 0.008], "seam", D);
  return fin(A, { effect: "none" });
}

// ================================================================================================= CONSTRUCTION
function scaffoldBay() {
  const A = mk("mod_scaffold_bay", "Scaffold bay (2 lifts)", "Construction",
    "Tube-and-fitting access scaffold bay, 2.6 x 1.3 m, two working lifts at 2.0 and 4.0 m: galvanised standards on base plates and sole boards, ledgers, transoms, a facade brace, couplers at every joint, five-board timber decks with toe boards, double guard rails on the long faces and a tied ladder up the end. The decks are walkable.", [3.4, 1.6], { enterable: true });
  const hx = 1.3, hz = 0.65, lifts = [2.0, 4.0], top = 5.05, T = 0.048;
  const coup = (x, y, z) => A.box([x - 0.045, y - 0.04, z - 0.045], [x + 0.045, y + 0.04, z + 0.045], "steel", D);
  for (const x of [-hx, hx]) for (const z of [-hz, hz]) {
    rod(A, [x, 0.06, z], [x, top, z], T, "galv", { k: 1, seg: 8 });
    A.box([x - 0.08, 0.04, z - 0.08], [x + 0.08, 0.06, z + 0.08], "steel_dk", D);
    A.box([x - 0.12, 0, z - 0.25], [x + 0.12, 0.04, z + 0.25], "wood_raw", vb(0.004));
  }
  for (const y of [0.15, ...lifts]) {
    for (const z of [-hz, hz]) { rod(A, [-hx - 0.1, y, z], [hx + 0.1, y, z], T, "galv", { seg: 8 }); coup(-hx, y, z); coup(hx, y, z); }
    for (const x of [-hx, -0.43, 0.43, hx]) rod(A, [x, y + 0.05, -hz - 0.15], [x, y + 0.05, hz + 0.15], T, "galv", { seg: 8 });
  }
  for (const y of lifts) {
    for (let i = 0; i < 5; i++) A.box([-hx - 0.05, y + 0.075, -hz + 0.05 + i * 0.24], [hx + 0.05, y + 0.113, -hz + 0.27 + i * 0.24], "wood", { k: 0, bv: 0.004 });
    for (const x of [-hx - 0.05, hx + 0.05]) for (let i = 0; i < 5; i++) A.box([x - 0.004, y + 0.075, -hz + 0.05 + i * 0.24], [x + 0.004, y + 0.113, -hz + 0.27 + i * 0.24], "steel", D);   // board end bands
    A.box([-hx - 0.05, y + 0.075, -hz + 0.05], [hx + 0.05, y + 0.113, hz - 0.03], "wood", H);
    for (const z of [-hz, hz]) {
      A.box([-hx, y + 0.113, z - (z > 0 ? 0.03 : -0.0)], [hx, y + 0.27, z + (z > 0 ? 0 : 0.03)], "wood", { k: 0, bv: 0.004 });
      for (const dy of [0.5, 1.0]) { rod(A, [-hx - 0.1, y + dy, z], [hx + 0.1, y + dy, z], T, "galv", { seg: 8 }); coup(-hx, y + dy, z); coup(hx, y + dy, z); }
      A.box([-hx, y + 0.1, z - 0.03], [hx, y + 1.05, z + 0.03], "galv", H);
    }
  }
  rod(A, [-hx, 0.2, -hz - 0.06], [hx, 3.9, -hz - 0.06], T, "galv", { seg: 8 });
  rod(A, [hx, 0.2, -hz - 0.06], [-hx, 3.9, -hz - 0.06], T, "galv", { seg: 8 });
  for (const s of [-1, 1]) rod(A, [hx + 0.3, 0, s * 0.22], [hx + 0.3, 5.0, s * 0.22], 0.06, "galv", { seg: 8 });
  for (let y = 0.3; y < 5; y += 0.3) rod(A, [hx + 0.3, y, -0.22], [hx + 0.3, y, 0.22], 0.03, "galv", { seg: 6 });
  for (const y of [1.5, 3.5]) rod(A, [hx, y, -0.22], [hx + 0.3, y, -0.22], 0.012, "wire", { seg: 4 });
  return fin(A, {
    rooms: lifts.map((y, i) => room(`lift${i + 1}`, [-hx + 0.05, y + 0.11, -hz + 0.1], [hx - 0.05, y + 1.9, hz - 0.1])),
    cover: [], firingPoints: lifts.map((y) => fire([0, y + 0.11, hz - 0.3], 0, "firestep")),
  });
}
function concretePipe(stack) {
  const A = mk(stack ? "mod_concrete_pipe_stack" : "mod_concrete_pipe", stack ? "Concrete pipes (stack of 3)" : "Concrete drainage pipe (1.2 m)", "Construction",
    stack ? "Three 1.2 m bore spigot-and-socket concrete drainage pipes stacked two-and-one on timber bearers and chocks." : "Precast reinforced concrete drainage pipe, 1.2 m bore, 0.12 m wall, 2.5 m long, with a socket end and lifting holes, lying on the ground.", stack ? [2.7, 3.0] : [2.7, 1.7]);
  const pipe = (y, z) => {
    ring(A, [-0.1, y, z], 0.66, 20, 0.12, 2.3, "x", "conc_r", {});
    ring(A, [1.1, y, z], 0.76, 20, 0.12, 0.3, "x", "conc_r", {});
    A.cyl([1.249, y, z], [1.64, 0.004, 1.64], "conc_dk", { ...D, seg: 20, r: [0, 0, 90] });
    A.cyl([-0.1, y, z], [1.21, 2.3, 1.21], "conc_dk", { ...D, seg: 20, r: [0, 0, 90] });
    for (const x of [-0.6, 0.4]) A.cyl([x, y + 0.66, z], [0.06, 0.13, 0.06], "seam", { ...D, seg: 8 });
  };
  if (!stack) { pipe(0.72, 0); return fin(A, { cover: [cover([0, 0, -1.3], 0, "low", ["over"]), cover([0, 0, 1.3], 180, "low", ["over"])] }); }
  for (const x of [-0.8, 0.8]) {
    A.box([x - 0.08, 0, -1.45], [x + 0.08, 0.1, 1.45], "wood_raw", { bv: 0.006 });
    for (const z of [-1.4, -0.06, 0.06, 1.4]) A.wedge([x, 0.17, z], [0.14, 0.14, 0.18], "wood_raw", { r: [0, z < 0 ? (Math.abs(z) > 1 ? 0 : 180) : Math.abs(z) > 1 ? 180 : 0, 0], k: 0 });
  }
  for (const z of [-0.74, 0.74]) pipe(0.82, z);
  pipe(0.82 + 1.25, 0);
  return fin(A, { cover: [cover([0, 0, -2.0], 0, "high", ["left", "right"]), cover([0, 0, 2.0], 180, "high", ["left", "right"])] });
}
function excavator() {
  const A = mk("mod_excavator", "Tracked excavator (20 t, generic)", "Construction",
    "Generic 20 t crawler excavator: steel tracks with grousers over idlers and sprockets, a swing house with a glazed left-hand cab, engine hood louvres, exhaust, handrails and a counterweight, and a raised boom, stick and digging bucket with teeth on hydraulic rams and hoses. Yellow with dark running gear.", [3.1, 7.4], { maxGrade: 0.3 });
  const zo = -1.4, at = (x, y, z) => [x, y, z + zo], Y = "p_yellow";
  for (const s of [-1, 1]) {
    const x = s * 1.1, z0 = -2.0, z1 = 2.0, r = 0.42;
    A.box(at(x - 0.3, 0.05, z0), at(x + 0.3, 0.85, z1), "steel_dk", V);
    for (const z of [z0, z1]) A.cyl(at(x, r + 0.02, z), [0.86, 0.6, 0.86], "steel_dk", { k: 0, seg: 16, r: [0, 0, 90] });
    for (let z = z0 + 0.1; z < z1; z += 0.22) for (const y of [0.0, 0.85]) A.box(at(x - 0.31, y, z - 0.04), at(x + 0.31, y + 0.04, z + 0.04), "steel", D);
    for (let k = 0; k < 6; k++) { const a = -Math.PI / 2 + (k / 5) * Math.PI; for (const z of [z0, z1]) { const sg = z > 0 ? 1 : -1; A.obox(at(x, 0.44 + Math.sin(a) * 0.45, z + sg * Math.cos(a) * 0.45), [0.62, 0.04, 0.09], [Math.atan2(sg * Math.cos(a), Math.sin(a)) * DEG, 0, 0], "steel", D); } }   // shoes round the idler / sprocket
    A.box(at(x - 0.32, 0.3, z0 + 0.4), at(x + 0.32, 0.6, z1 - 0.4), Y, D);
    for (let z = z0 + 0.6; z < z1 - 0.5; z += 0.55) A.cyl(at(s * 1.43, 0.3, z), [0.2, 0.02, 0.2], "steel_dk", { ...D, seg: 10, r: [0, 0, 90] });
  }
  A.box(at(-0.8, 0.45, -0.9), at(0.8, 0.9, 0.9), "steel_dk", V);
  A.cyl(at(0, 0.97, 0), [1.5, 0.14, 1.5], "steel_dk", { k: 0, seg: 20 });
  // house, counterweight, cab
  A.rbox(at(-1.25, 1.05, -1.5), at(1.25, 2.05, 1.1), 0.08, Y, V);
  A.rbox(at(-1.25, 1.05, -2.0), at(1.25, 2.0, -1.4), 0.15, Y, V);
  for (let x = -1.0; x < 0.0; x += 0.08) A.box(at(x, 2.05, -1.2), at(x + 0.04, 2.06, -0.2), "seam", D);
  for (let y = 1.25; y < 1.85; y += 0.07) A.obox(at(-1.258, y, -0.7), [0.012, 0.04, 0.9], [0, 0, 35], Y, D);
  rod(A, at(-0.7, 2.05, -0.6), at(-0.7, 2.6, -0.6), 0.12, "steel_dk", { seg: 10 });
  for (const z of [-1.3, 0.5]) for (const x of [-1.2, 0.0]) rod(A, at(x, 2.05, z), at(x, 2.45, z), 0.03, Y, { seg: 6 });
  rod(A, at(-1.2, 2.45, -1.3), at(-1.2, 2.45, 0.5), 0.03, Y, { seg: 6 });
  const cx0 = 0.3, cx1 = 1.22, cz0 = 0.05, cz1 = 1.08, cy0 = 2.05, cy1 = 3.05;
  for (const [x, z] of [[cx0, cz0], [cx1, cz0], [cx0, cz1], [cx1, cz1]]) A.box(at(x - 0.04, cy0, z - 0.04), at(x + 0.04, cy1, z + 0.04), "p_black", V);
  cabGlass(A, cx0, cx1, cy0 + 0.05, cy1 - 0.05, cz1 + zo);
  cabGlass(A, cx0, cx1, cy0 + 0.4, cy1 - 0.05, cz0 + zo, "glass");
  for (const x of [cx0, cx1]) cabGlassX(A, x, cy0 + 0.05, cy1 - 0.05, cz0 + zo, cz1 + zo);
  A.rbox(at(cx0 - 0.05, cy1 - 0.02, cz0 - 0.06), at(cx1 + 0.05, cy1 + 0.06, cz1 + 0.08), 0.03, Y, V);
  A.box(at(cx0, cy0 - 0.02, cz0), at(cx1, cy0 + 0.02, cz1), Y, V);
  seat(A, 0.76, cy0 + 0.45, 0.75 + zo, 0.5, "seat");
  A.box(at(cx0 - 0.01, cy0 - 0.9, cz0), at(cx1, cy0, cz1), Y, V);
  // boom, stick, bucket
  const b0 = [0.0, 1.9, 0.9], b1 = [0.0, 4.4, 3.4], s1 = [0.0, 1.3, 4.8];
  bar(A, at(...b0), at(...b1), 0.4, 0.55, Y, {});
  bar(A, at(...b1), at(...s1), 0.3, 0.42, Y, {});
  for (const s of [-1, 1]) {
    rod(A, at(s * 0.18, 1.4, 1.15), at(s * 0.18, 3.1, 2.3), 0.16, Y, { seg: 10 });
    rod(A, at(s * 0.18, 2.6, 1.85), at(s * 0.18, 3.4, 2.45), 0.09, "chrome", { seg: 8 });
    rod(A, at(s * 0.22, 2.2, 1.5), at(s * 0.22, 4.25, 3.1), 0.04, "cable", { seg: 6 });
  }
  rod(A, at(0, 4.0, 2.4), at(0, 4.8, 3.6), 0.16, Y, { seg: 10 });
  rod(A, at(0, 4.4, 3.0), at(0, 4.7, 3.5), 0.09, "chrome", { seg: 8 });
  rod(A, at(0, 3.6, 3.95), at(0, 2.0, 4.6), 0.13, Y, { seg: 10 });
  A.obox(at(0, 0.85, 4.95), [1.0, 0.75, 0.7], [-25, 0, 0], Y, { bv: 0.03 });
  A.obox(at(0, 0.85, 4.95), [0.92, 0.66, 0.62], [-25, 0, 0], "steel_dk", D);
  for (let i = 0; i < 5; i++) A.cone(at(-0.4 + i * 0.2, 0.36, 5.32), [0.08, 0.16, 0.06], "steel", { ...D, n: 4, top: 0.2, r: [150, 0, 0] });
  A.box(at(-1.4, 0, -2.05), at(1.4, 0.9, 2.05), "steel_dk", H);
  A.box(at(-1.25, 0.9, -2.0), at(1.25, 2.05, 1.1), Y, H);
  A.box(at(cx0 - 0.05, cy0, cz0 - 0.05), at(cx1 + 0.05, cy1 + 0.06, cz1 + 0.08), Y, H);
  return fin(A, { cover: swapCover(boxCover(2.0, 1.4, 2.0, { step: 1.6 }), zo) });
}
function lumberStack() {
  const A = mk("mod_lumber_stack", "Stacked lumber bundles", "Construction",
    "Two banded bundles of 3.6 m 2x4 softwood studs stacked on timber dunnage with stickers between, the board ends showing, steel strapping at three points.", [3.8, 1.3]);
  const bundle = (y0, z) => {
    const w = 0.9, h = 0.5, L = 3.6, nx = 8, ny = 6;
    A.box([-L / 2, y0, z - w / 2], [L / 2, y0 + h, z + w / 2], "wood_raw", { bv: 0.004 });
    for (const e of [-1, 1]) for (let i = 0; i < nx; i++) for (let j = 0; j < ny; j++) {
      const zz = z - w / 2 + (i + 0.5) * (w / nx), yy = y0 + (j + 0.5) * (h / ny);
      A.box([e * (L / 2 - 0.002), yy - h / ny / 2 + 0.004, zz - w / nx / 2 + 0.004], [e * (L / 2 + 0.004 + ((i + j) % 3) * 0.006), yy + h / ny / 2 - 0.004, zz + w / nx / 2 - 0.004], "heartwood", D);
    }
    for (const s of [-1, 1]) for (let j = 1; j < ny; j++) seam(A, [-L / 2, y0 + j * (h / ny) - 0.002, s * w / 2 + z - (s > 0 ? 0 : 0.002)], [L / 2, y0 + j * (h / ny) + 0.002, s * w / 2 + z + (s > 0 ? 0.002 : 0)]);
    for (const x of [-1.4, 0, 1.4]) { A.box([x - 0.016, y0 - 0.003, z - w / 2 - 0.003], [x + 0.016, y0 + h + 0.003, z + w / 2 + 0.003], "steel", D); }
  };
  for (const x of [-1.4, 0, 1.4]) A.box([x - 0.05, 0, -0.62], [x + 0.05, 0.1, 0.62], "wood_dk", vb(0.005));
  bundle(0.1, -0.05);
  for (const x of [-1.2, 0, 1.2]) A.box([x - 0.02, 0.6, -0.55], [x + 0.02, 0.64, 0.55], "wood_dk", D);
  bundle(0.64, 0.05);
  return fin(A, { cover: boxCover(1.8, 0.55, 1.14, { step: 1.2 }) });
}
function rebarBundle() {
  const A = mk("mod_rebar_bundle", "Rebar bundles", "Construction",
    "Two bundles of 6 m deformed steel reinforcing bars, wire-tied, lying on timber dunnage, a few bars bent and splayed at one end.", [6.3, 1.4]);
  const R = rng(616);
  for (const x of [-2.4, 0, 2.4]) A.box([x - 0.05, 0, -0.6], [x + 0.05, 0.1, 0.6], "wood_dk", vb(0.005));
  for (const zc of [-0.3, 0.3]) {
    for (let i = 0; i < 26; i++) {
      const a = (i / 26) * 2 * Math.PI * 3, rr = 0.03 + (i % 9) * 0.012;
      const y = 0.2 + Math.sin(a) * rr, z = zc + Math.cos(a) * rr;
      const bend = i % 9 === 0 && zc > 0;
      rod(A, [-3.0, y, z], [bend ? 2.4 : 3.0, y, z], 0.02, "rebar", { seg: 6 });
      if (bend) rod(A, [2.4, y, z], [2.9, y + 0.25 + R() * 0.2, z + (R() - 0.5) * 0.5], 0.02, "rebar", { seg: 6 });
    }
    for (const x of [-2.0, 0, 2.0]) ring(A, [x, 0.2, zc], 0.13, 8, 0.006, 0.01, "x", "wire", D);
  }
  A.box([-3, 0.1, -0.45], [3, 0.32, 0.45], "rebar", H);
  return fin(A, { effect: "carve" });
}
function cinderWall() {
  const A = mk("mod_cinder_wall", "Unfinished cinder-block wall", "Construction",
    "Section of an unfinished concrete-block wall, 3.2 m long: 390 x 190 mm hollow blocks in running bond on a concrete footing, racked back in steps from 1.8 m to 0.6 m, open cores on the top course with rebar dowels sticking up and a few loose blocks on the ground.", [3.9, 1.8]);
  const bw = 0.4, bh = 0.2, th = 0.19, R = rng(919);
  A.box([-1.65, -0.05, -0.2], [1.65, 0.1, 0.2], "conc_r", { bv: 0.01 });
  const heights = [9, 9, 9, 9, 8, 7, 6, 5, 4, 3];
  for (let c = 0; c < 9; c++) {
    const off = c % 2 ? bw / 2 : 0;
    for (let i = 0; i < 9; i++) {
      const x0 = Math.max(-1.6, -1.6 + off + i * bw - (c % 2 ? bw : 0)), x1 = Math.min(1.6, -1.6 + off + (i + 1) * bw - (c % 2 ? bw : 0) - 0.01);
      if (x1 - x0 < 0.1) continue;
      const col = Math.floor((x0 + 1.6) / 0.32);
      if (c >= heights[Math.min(9, col)]) continue;
      const y0 = 0.1 + c * bh;
      A.box([x0, y0, -th / 2], [x1, y0 + bh - 0.01, th / 2], "cmu", { k: 0, bv: 0.004 });
      const topC = c + 1 >= heights[Math.min(9, col)] || c === 8;
      if (topC) for (const dx of [0.1, 0.29]) A.box([x0 + dx - 0.065, y0 + bh - 0.0105, -0.06], [x0 + dx + 0.065, y0 + bh - 0.009, 0.06], "seam", D);
      if (topC && R() > 0.5) rod(A, [x0 + 0.1, y0, 0], [x0 + 0.1, y0 + bh + 0.5, 0], 0.014, "rebar", { seg: 6 });
    }
    // bed joint mortar strip
    A.box([-1.6, 0.1 + c * bh + bh - 0.01, -th / 2 + 0.01], [1.6 - (9 - heights.filter((h) => h > c).length) * 0.32, 0.1 + (c + 1) * bh, th / 2 - 0.01], "mortar", D);
  }
  for (let col = 0; col < 10; col++) A.box([-1.6 + col * 0.32, 0.1, -th / 2], [-1.6 + (col + 1) * 0.32, 0.1 + heights[col] * bh, th / 2], "cmu", H);
  for (let i = 0; i < 3; i++) A.obox([0.5 + i * 0.45, 0.1, 0.55 + R() * 0.2], [0.39, 0.19, 0.19], [0, R() * 60, R() > 0.6 ? 90 : 0], "cmu", { bv: 0.004 });
  return fin(A, { cover: [cover([-0.9, 0, -0.65], 0, "high", ["left", "right"]), cover([-0.9, 0, 0.65], 180, "high", ["left", "right"]), cover([0.9, 0, -0.65], 0, "low", ["over"]), cover([0.9, 0, 0.75], 180, "low", ["over"])] });
}
function portableToilet() {
  const A = mk("mod_portable_toilet", "Portable toilet", "Construction",
    "Moulded polyethylene portable toilet cabin, 1.12 x 1.12 x 2.3 m: ribbed blue walls on a skid base, a front door with vents, a pull handle and an occupancy latch, a translucent white roof with a vent stack.", [1.2, 1.2]);
  const h = 0.56;
  A.box([-h, 0, -h], [h, 0.1, h], "pl_grey", { bv: 0.01 });
  A.box([-h + 0.01, 0.1, -h + 0.01], [h - 0.01, 2.15, h - 0.01], "pl_blue", { bv: 0.02 });
  for (const s of [-1, 1]) for (let i = -2; i <= 2; i++) {
    A.box([s * h - 0.012, 0.2, i * 0.2 - 0.03], [s * h + 0.012, 2.05, i * 0.2 + 0.03], "pl_blue", D);
    if (s > 0) A.box([i * 0.2 - 0.03, 0.2, -h - 0.012], [i * 0.2 + 0.03, 2.05, -h + 0.012], "pl_blue", D);
  }
  A.box([-0.45, 0.15, h - 0.01], [0.45, 2.05, h + 0.02], "pl_blue", dv(0.01));
  for (let i = 0; i < 5; i++) A.box([-0.3, 1.75 + i * 0.05, h + 0.02], [0.3, 1.77 + i * 0.05, h + 0.03], "seam", D);
  A.box([0.3, 1.0, h + 0.02], [0.34, 1.2, h + 0.07], "pl_grey", D);
  A.box([0.3, 1.3, h + 0.02], [0.38, 1.34, h + 0.03], "pl_red", D);
  for (const y of [0.4, 1.6]) A.box([-0.47, y, h + 0.02], [-0.42, y + 0.12, h + 0.04], "pl_grey", D);
  A.rbox([-h - 0.02, 2.12, -h - 0.02], [h + 0.02, 2.3, h + 0.02], 0.08, "hdpe");
  rod(A, [-0.35, 2.2, -0.35], [-0.35, 2.55, -0.35], 0.1, "pl_grey", { seg: 8 });
  return fin(A, { cover: boxCover(h, h, 2.3, { step: 1.2 }) });
}
function builderSkip() {
  const A = mk("mod_skip", "Builder's skip (loaded)", "Construction",
    "Steel builder's skip, 3.6 x 1.7 x 1.2 m: sloping ends over a 2.6 m floor, stiffened sides with a top rail, four lifting lugs with chain shackles; loaded with rubble, timber and plasterboard.", [3.75, 1.8]);
  const R = rng(4242), m = "p_yellow", hz = 0.82;
  for (const s of [-1, 1]) {
    extrudeZ(A, s * hz - 0.03, s * hz + 0.03, [[-1.3, 0.06], [1.3, 0.06], [1.8, 1.2], [-1.8, 1.2]], m, V);
    for (const x of [-1.0, -0.35, 0.35, 1.0]) A.box([x - 0.05, 0.1, s * hz - (s > 0 ? 0 : 0.06)], [x + 0.05, 1.15, s * hz + (s > 0 ? 0.06 : 0)], m, D);
    rod(A, [-1.8, 1.2, s * hz], [1.8, 1.2, s * hz], 0.08, m, { seg: 8 });
    for (const x of [-1.62, 1.62]) { A.box([x - 0.06, 0.85, s * hz], [x + 0.06, 1.0, s * (hz + 0.12)], "steel_dk", D); A.cyl([x, 0.92, s * (hz + 0.12)], [0.06, 0.12, 0.06], "steel", { ...D, seg: 8, r: [0, 0, 90] }); }
  }
  for (const e of [-1, 1]) bar(A, [e * 1.3, 0.06, 0], [e * 1.8, 1.2, 0], 2 * hz, 0.05, m, {});
  A.box([-1.3, 0, -hz], [1.3, 0.08, hz], m, { bv: 0.01 });
  for (const z of [-0.5, 0.5]) A.box([-1.25, -0.0, z - 0.06], [1.25, 0.03, z + 0.06], "steel_dk", D);
  A.box([-1.45, 0.08, -hz + 0.04], [1.45, 0.75, hz - 0.04], "rubble", V);
  for (let i = 0; i < 16; i++) A.obox([(R() - 0.5) * 2.8, 0.75 + R() * 0.3, (R() - 0.5) * 1.3], [0.2 + R() * 0.6, 0.04 + R() * 0.15, 0.2 + R() * 0.5], [R() * 40, R() * 180, R() * 40], ["wood_raw", "conc_dk", "debris", "plaster"][i % 4], D);
  for (const s of [-1, 1]) A.box([-1.8, 0, s > 0 ? hz - 0.03 : -hz - 0.03], [1.8, 1.2, s > 0 ? hz + 0.03 : -hz + 0.03], m, H);
  A.box([-1.45, 0, -hz], [1.45, 0.75, hz], m, H);
  return fin(A, { cover: boxCover(1.8, hz, 1.2, { step: 1.8 }) });
}

// ================================================================================================= TERRAIN OBSTACLES
function rock(size) {
  const S = { small: 0.6, medium: 1.4, large: 3.0 }[size], R = rng(S * 1000 + 3);
  const A = mk(`mod_rock_${size}`, `Boulder (${size})`, "Terrain",
    `Weathered granite ${size === "large" ? "boulder outcrop" : size === "medium" ? "boulder" : "rock"}, about ${S} m across: a main rounded mass with split-off facets and lichen-stained patches, bedded into the ground with loose stones round its foot.`, [S * 1.25, S * 1.1]);
  const hgt = S * (size === "large" ? 0.62 : 0.55);
  A.rbox([-S * 0.48, -S * 0.08, -S * 0.4], [S * 0.48, hgt, S * 0.4], S * 0.18, "rock", { r: [R() * 8 - 4, R() * 40, R() * 8 - 4] });
  for (let i = 0; i < (size === "small" ? 2 : 4); i++) {
    const a = R() * 2 * Math.PI, d = S * (0.2 + R() * 0.15), s = S * (0.3 + R() * 0.25);
    A.rbox([Math.cos(a) * d - s / 2, -S * 0.05, Math.sin(a) * d - s / 2 * 0.8], [Math.cos(a) * d + s / 2, hgt * (0.55 + R() * 0.4), Math.sin(a) * d + s / 2 * 0.8], s * 0.25, i % 2 ? "rock_m" : "rock_d", { r: [R() * 20 - 10, R() * 90, R() * 20 - 10], k: size === "small" ? 0 : 1 });
  }
  for (let i = 0; i < 3; i++) { const a = R() * 6.28; A.obox([Math.cos(a) * S * 0.25, hgt * (0.7 + R() * 0.25), Math.sin(a) * S * 0.2], [S * 0.25, S * 0.04, S * 0.2], [R() * 30, R() * 180, R() * 30], "rock_d", D); }
  for (let i = 0; i < 7; i++) { const a = R() * 6.28, d = S * (0.5 + R() * 0.12), s = 0.05 + R() * S * 0.06; A.obox([Math.cos(a) * d, s * 0.3, Math.sin(a) * d * 0.85], [s * 1.3, s, s], [R() * 60, R() * 180, R() * 60], "rock", { k: 0, bv: s * 0.2 }); }
  return fin(A, size === "small" ? {} : { cover: size === "large" ? [cover([0, 0, -S * 0.65], 0, "high", ["left", "right"]), cover([0, 0, S * 0.65], 180, "high", ["left", "right"]), cover([-S * 0.75, 0, 0], 90, "high", ["left", "right"])] : [cover([0, 0, -S * 0.65], 0, "low", ["over"]), cover([0, 0, S * 0.65], 180, "low", ["over"])] });
}
function retainingWall() {
  const A = mk("mod_retaining_wall", "Retaining wall section", "Terrain",
    "Cast-in-place reinforced concrete cantilever retaining wall, 4 m section, 2.2 m exposed: board-marked face with formwork tie holes, a cast coping with drip, an expansion joint, a row of weep-hole pipes with run-off stains, a toe slab, and the earth bank it holds up behind.", [4.1, 2.7]);
  const zf = 0.95, zb = 0.65, H2 = 2.2, R = rng(2222);
  A.box([-2, -0.3, zb - 0.35], [2, 0.08, 1.3], "conc_r", { bv: 0.01 });
  A.box([-2, 0.08, zb], [2, H2, zf], "conc_fw");
  A.box([-2.02, H2, zb - 0.03], [2.02, H2 + 0.1, zf + 0.05], "conc", { bv: 0.012 });
  A.box([-2.02, H2 - 0.015, zf + 0.03], [2.02, H2, zf + 0.05], "conc_dk", D);
  seam(A, [-0.008, 0.08, zf], [0.008, H2, zf + 0.003]);
  for (let x = -1.8; x < 2; x += 0.6) for (const y of [0.6, 1.3, 1.95]) A.cyl([x, y, zf + 0.001], [0.03, 0.004, 0.03], "conc_dk", { ...D, seg: 6, r: [90, 0, 0] });
  for (const x of [-1.5, -0.3, 0.9]) {
    A.cyl([x, 0.32, zf + 0.04], [0.075, 0.1, 0.075], "pl_grey", { ...D, seg: 10, r: [90, 0, 0] });
    A.cyl([x, 0.32, zf + 0.092], [0.055, 0.004, 0.055], "seam", { ...D, seg: 10, r: [90, 0, 0] });
    A.box([x - 0.06 - R() * 0.04, 0.08, zf + 0.001], [x + 0.06, 0.28, zf + 0.004], "conc_st", D);
    A.box([x - 0.12, 0.08, zf + 0.06], [x + 0.18, 0.09, zf + 0.4], "soil", D);
  }
  for (let i = 0; i < 4; i++) { const x = -1.8 + R() * 3.6; A.box([x, 0.9 + R(), zf + 0.001], [x + 0.08, H2 - 0.02, zf + 0.004], "conc_st", D); }
  extrudeX(A, -2, 2, [[-1.3, 0.0], [zb, 0.0], [zb, H2 - 0.1], [-0.2, H2 - 0.1], [-1.3, 0.25]], "soil");
  for (let i = 0; i < 6; i++) A.obox([-1.8 + R() * 3.6, H2 - 0.08, zb - 0.2 - R() * 0.6], [0.4, 0.06, 0.3], [0, R() * 180, 0], "leaves", D);
  return fin(A, { cover: [-1.2, 0, 1.2].map((x) => cover([x, 0, zf + 0.6], 180, "high", ["left", "right"])) });
}
function fallenTree() {
  const A = mk("mod_fallen_tree", "Fallen park tree", "Terrain",
    "Uprooted park plane / lime tree lying on its side, 9 m: a tapering 0.55 m trunk with bark, an upturned root plate with soil and torn roots, broken limbs and leafy branch ends, a snapped branch stub.", [12.2, 6.6]);
  const R = rng(5150), pts = [[-3.9, 0.3, 0.0], [-1.5, 0.28, 0.15], [1.2, 0.24, 0.05], [3.6, 0.2, -0.25], [4.6, 0.25, -0.4]], dias = [0.56, 0.46, 0.36, 0.22, 0.12];
  for (let i = 0; i < pts.length - 1; i++) {
    rod(A, pts[i], pts[i + 1], (dias[i] + dias[i + 1]) / 2, "bark", { k: i < 3 ? 1 : 0, lod: 0, seg: 14 });
    A.cyl(pts[i + 1], [dias[i + 1] * 1.04, 0.1, dias[i + 1] * 1.04], "bark", { k: 0, seg: 14, r: aimY([pts[i + 1][0] - pts[i][0], pts[i + 1][1] - pts[i][1], pts[i + 1][2] - pts[i][2]]) });
  }
  A.cyl([-4.15, 0.75, 0], [2.4, 0.45, 2.1], "soil", { seg: 18, r: [0, 0, 90] });
  A.cyl([-3.95, 0.75, 0], [1.4, 0.1, 1.2], "heartwood", { ...D, seg: 14, r: [0, 0, 90] });
  for (let i = 0; i < 12; i++) { const a = R() * 6.28, rr = 0.7 + R() * 0.5; rod(A, [-4.2, 0.75 + Math.cos(a) * 0.3, Math.sin(a) * 0.3], [-4.45 - R() * 0.4, 0.75 + Math.cos(a) * rr, Math.sin(a) * rr], 0.05 + R() * 0.05, "bark", { seg: 6 }); }
  for (let i = 0; i < 6; i++) A.obox([-4.6 - R() * 0.4, 0.1, (R() - 0.5) * 1.6], [0.4, 0.15, 0.35], [R() * 30, R() * 180, 0], "soil", D);
  const branch = (t, side, up, len, dia) => {
    const i = Math.min(3, Math.floor(t * 4)), f = t * 4 - i, b = lerp3(pts[i], pts[i + 1], f);
    const e = [b[0] + len * 0.5, b[1] + up * len, b[2] + side * len];
    rod(A, b, e, dia, "bark", { seg: 8 });
    const e2 = [e[0] + len * 0.4, e[1] + up * len * 0.3 + 0.1, e[2] + side * len * 0.35];
    rod(A, e, e2, dia * 0.55, "bark", { seg: 6 });
    for (let k = 0; k < 3; k++) { const c = lerp3(e, e2, 0.4 + k * 0.3), s = 0.5 + R() * 0.5; A.rbox([c[0] - s, c[1] - s * 0.5, c[2] - s * 0.8], [c[0] + s, c[1] + s * 0.6, c[2] + s * 0.8], s * 0.4, "leaves", { k: 0, r: [R() * 30, R() * 90, R() * 30] }); }
  };
  branch(0.45, 1, 0.35, 1.6, 0.16); branch(0.55, -1, 0.3, 1.4, 0.14); branch(0.65, 1, 0.55, 1.3, 0.12); branch(0.75, -1, 0.15, 1.2, 0.1); branch(0.85, 0.4, 0.8, 1.0, 0.08); branch(0.95, -0.6, 0.4, 0.8, 0.06);
  rod(A, [0.2, 0.42, 0.18], [0.35, 0.95, 0.2], 0.13, "bark", { seg: 8 });
  A.cyl([0.35, 0.95, 0.2], [0.13, 0.02, 0.13], "heartwood", { ...D, seg: 8, r: aimY([0.15, 0.53, 0.02]) });
  return fin(A, { cover: [cover([-1.5, 0, -0.75], 0, "low", ["over"]), cover([0.5, 0, -0.7], 0, "low", ["over"]), cover([-1.0, 0, 0.9], 180, "low", ["over"]), cover([-4.2, 0, -1.4], 0, "high", ["left", "right"])] });
}
function culvertHeadwall() {
  const A = mk("mod_culvert_headwall", "Drainage culvert headwall", "Terrain",
    "Precast concrete culvert headwall, 3.0 x 1.8 m, round a 0.9 m pipe outlet: angled wingwalls, a cast coping, a concrete apron with a cut-off wall, a damp silt fan at the outlet and the road embankment behind.", [4.9, 3.6]);
  const T = 0.3, z0 = -0.15, z1 = 0.15, hw = 1.5, Hh = 1.8, pr = 0.45, py = pr + 0.12;
  A.box([-hw, 0, z0], [-pr - 0.02, Hh, z1], "conc");
  A.box([pr + 0.02, 0, z0], [hw, Hh, z1], "conc");
  A.box([-pr - 0.02, py + pr, z0], [pr + 0.02, Hh, z1], "conc");
  A.box([-pr - 0.02, 0, z0], [pr + 0.02, 0.12, z1], "conc");
  ring(A, [0, py, z1 - 0.8], pr + 0.05, 18, 0.1, 1.9, "z", "conc_r", {});
  A.cyl([0, py, -1.6], [pr * 2, 0.02, pr * 2], "seam", { ...D, seg: 18, r: [90, 0, 0] });
  A.box([-pr + 0.02, 0.1, z0 - 1.6], [pr - 0.02, 0.16, z1 + 0.02], "soil", D);
  A.box([-hw - 0.03, Hh, z0 - 0.04], [hw + 0.03, Hh + 0.1, z1 + 0.05], "conc", { bv: 0.01 });
  for (const s of [-1, 1]) {
    A.obox([s * (hw + 0.42), Hh * 0.38, 0.55], [T, Hh * 0.76, 1.3], [0, s * -30, 0], "conc", { bv: 0.01 });
    A.obox([s * (hw + 0.42), Hh * 0.76 + 0.05, 0.55], [T + 0.06, 0.1, 1.34], [0, s * -30, 0], "conc", dv(0.01));
  }
  A.box([-hw, -0.1, z1], [hw, 0.05, 1.6], "conc_r", { bv: 0.01 });
  A.box([-hw, -0.5, 1.5], [hw, 0.06, 1.65], "conc_r", { bv: 0.01 });
  A.box([-0.6, 0.05, z1], [0.6, 0.058, 1.3], "soil", D);
  extrudeX(A, -hw - 0.3, hw + 0.3, [[-1.8, 0], [z0, 0], [z0, Hh + 0.1], [-1.0, Hh + 0.1]], "soil");
  return fin(A, { cover: [cover([-1.0, 0, 0.6], 180, "high", ["left", "right"]), cover([1.0, 0, 0.6], 180, "high", ["left", "right"])] });
}

// ================================================================================================= MILITARY EQUIPMENT
function ammoCan(A, x, y, z, yaw, m = "p_od") {
  const t = yaw / DEG, at = (dx, dy, dz) => [x + dx * Math.cos(t) + dz * Math.sin(t), y + dy, z - dx * Math.sin(t) + dz * Math.cos(t)];
  const ob = (c, s, mm, o = D) => A.obox(at(...c), s, [0, yaw, 0], mm, o);
  ob([0, 0.09, 0], [0.28, 0.17, 0.14], m, { k: 0, bv: 0.008 });
  ob([0, 0.18, 0], [0.29, 0.025, 0.15], m, dv(0.006));
  ob([0, 0.196, 0], [0.12, 0.012, 0.02], "steel_dk");
  ob([0.145, 0.15, 0], [0.012, 0.07, 0.05], "steel_dk");
  ob([0, 0.12, 0.071], [0.12, 0.05, 0.004], "pl_yellow");
}
function ammoStack() {
  const A = mk("mod_ammo_box_stack", "Ammunition can stack", "Military",
    "Stack of olive steel ammunition cans on a pallet, 1.2 x 0.8 x 0.85 m: eighteen cans in three tiers with lid latches, carry handles and blank yellow marking bands, strapped down, with two loose cans beside.", [1.5, 0.9]);
  R_PAL = rng(31); palletParts(A, 0, 0, 0, 0);
  for (let l = 0; l < 3; l++) for (let i = 0; i < 4; i++) for (let j = 0; j < 5; j++) {
    if (l === 2 && (i + j) % 3 === 0) continue;
    ammoCan(A, -0.45 + i * 0.3, 0.144 + l * 0.21, -0.32 + j * 0.16, 0);
  }
  for (const x of [-0.3, 0.3]) A.box([x - 0.02, 0.14, -0.41], [x + 0.02, 0.78, 0.41], "pl_black", D);
  ammoCan(A, 0.72, 0, 0.1, 80); ammoCan(A, 0.72, 0.21, 0.08, 72);
  A.box([-0.6, 0, -0.4], [0.6, 0.78, 0.4], "p_od", H);
  return fin(A, { cover: boxCover(0.6, 0.4, 0.78) });
}
function fieldRadio() {
  const A = mk("mod_field_radio", "Field radio station", "Military",
    "Tactical radio set-up on a folding table: a ruggedised manpack radio with knobs and a handset on a coiled lead, a battery box, a closed rugged laptop and a 2 m whip antenna, with a folding camp chair.", [1.3, 1.3]);
  const ty = 0.74;
  A.box([-0.45, ty - 0.03, -0.3], [0.45, ty, 0.3], "pl_olive", { k: 0, bv: 0.006 });
  for (const s of [-1, 1]) { bar(A, [s * 0.4, 0, -0.27], [s * 0.4, ty - 0.03, 0.27], 0.02, 0.02, "steel", D); bar(A, [s * 0.4, 0, 0.27], [s * 0.4, ty - 0.03, -0.27], 0.02, 0.02, "steel", D); }
  A.box([-0.45, 0, -0.3], [0.45, ty, 0.3], "pl_olive", H);
  A.box([-0.35, ty, -0.2], [0.0, ty + 0.3, 0.05], "p_od", { k: 0, bv: 0.01 });
  A.box([-0.33, ty + 0.18, 0.05], [-0.02, ty + 0.28, 0.056], "screen_off", D);
  for (let i = 0; i < 4; i++) A.cyl([-0.3 + i * 0.08, ty + 0.1, 0.06], [0.03, 0.02, 0.03], "pl_black", { ...D, seg: 8, r: [90, 0, 0] });
  rod(A, [-0.05, ty + 0.3, -0.1], [-0.05, ty + 2.3, -0.12], 0.008, "pl_black", { seg: 4 });
  A.cyl([-0.05, ty + 0.32, -0.1], [0.03, 0.04, 0.03], "steel_dk", { ...D, seg: 8 });
  A.rbox([0.08, ty, 0.05], [0.28, ty + 0.05, 0.15], 0.015, "pl_black");
  rod(A, [-0.18, ty + 0.1, 0.06], [0.12, ty + 0.03, 0.1], 0.008, "cable", { seg: 4 });
  A.box([0.05, ty, -0.25], [0.4, ty + 0.035, 0.0], "pl_black", { k: 0, bv: 0.006 });
  A.box([0.15, 0, -0.75], [0.4, 0.25, -0.5], "p_od", { bv: 0.01 });
  A.box([-0.2, 0.45, 0.45], [0.25, 0.48, 0.85], "canvas", { k: 0 });
  A.obox([0.02, 0.72, 0.88], [0.45, 0.45, 0.03], [-10, 0, 0], "canvas", { k: 0 });
  for (const s of [-1, 1]) { bar(A, [s * 0.2, 0, 0.45], [s * 0.2, 0.46, 0.85], 0.02, 0.02, "steel", D); bar(A, [s * 0.2, 0, 0.85], [s * 0.2, 0.95, 0.9], 0.02, 0.02, "steel", D); }
  return fin(A, { cover: [] });
}
function milGenerator() {
  const A = mk("mod_mil_generator", "Tactical generator", "Military",
    "Skid-mounted tactical diesel generator, 1.3 x 0.75 x 0.9 m in olive drab: a tubular lifting frame, louvred housing, a control panel with gauges and output sockets, an exhaust with rain flap, a fuel cap and a coiled power cable.", [1.4, 0.85]);
  const hx = 0.62, hz = 0.35;
  for (const s of [-1, 1]) A.box([-hx, 0, s * hz - 0.04], [hx, 0.06, s * hz + 0.04], "steel_dk", vb(0.006));
  A.rbox([-hx + 0.04, 0.06, -hz + 0.03], [hx - 0.04, 0.82, hz - 0.03], 0.04, "p_od");
  for (const [x, z] of [[-1, -1], [1, -1], [1, 1], [-1, 1]]) rod(A, [x * (hx - 0.02), 0.06, z * hz], [x * (hx - 0.02), 0.9, z * hz], 0.03, "steel_dk", { seg: 6 });
  for (const s of [-1, 1]) rod(A, [-hx + 0.02, 0.9, s * hz], [hx - 0.02, 0.9, s * hz], 0.03, "steel_dk", { seg: 6 });
  A.box([-0.3, 0.35, hz - 0.03], [0.2, 0.7, hz - 0.02], "trim", D);
  for (let i = 0; i < 3; i++) A.cyl([-0.22 + i * 0.12, 0.6, hz - 0.015], [0.06, 0.01, 0.06], "screen_off", { ...D, seg: 10, r: [90, 0, 0] });
  for (let i = 0; i < 2; i++) A.cyl([-0.1 + i * 0.14, 0.43, hz - 0.015], [0.05, 0.02, 0.05], "pl_black", { ...D, seg: 8, r: [90, 0, 0] });
  for (let y = 0.2; y < 0.75; y += 0.06) A.obox([hx - 0.035, y, 0], [0.01, 0.035, 0.5], [0, 0, -35], "p_od", D);
  rod(A, [-0.4, 0.82, -0.15], [-0.4, 1.0, -0.15], 0.06, "steel_dk", { seg: 8 });
  A.cyl([0.35, 0.83, 0.1], [0.08, 0.03, 0.08], "steel_dk", { ...D, seg: 8 });
  for (let k = 0; k < 4; k++) A.cyl([-hx - 0.12, 0.12 + k * 0.035, 0.15], [0.28, 0.03, 0.28], "cable", { ...D, seg: 14 });
  return fin(A, { cover: [] });
}
function antennaMast() {
  const A = mk("mod_antenna_mast", "Telescopic antenna mast", "Military",
    "Guyed telescopic communications mast, 9 m: a tripod base with spiked feet, four nested aluminium sections with clamp collars, three guy lines to ground stakes, a crossed-dipole array and a small panel antenna at the top, a feed cable down to a drum.", [8.2, 8.2]);
  for (let i = 0; i < 3; i++) { const a = (i / 3) * 2 * Math.PI; rod(A, [0, 0.9, 0], [Math.sin(a) * 0.8, 0, Math.cos(a) * 0.8], 0.04, "steel", { k: 1, seg: 6 }); }
  const secs = [[0, 2.6, 0.1], [2.5, 4.8, 0.085], [4.7, 7.0, 0.07], [6.9, 9.0, 0.055]];
  for (const [y0, y1, d] of secs) { A.cyl([0, (y0 + y1) / 2, 0], [d, y1 - y0, d], "galv", { seg: 10, k: y0 === 0 ? 1 : 0 }); A.cyl([0, y1 - 0.03, 0], [d + 0.04, 0.08, d + 0.04], "steel_dk", { ...D, seg: 10 }); }
  for (let i = 0; i < 3; i++) {
    const a = (i / 3) * 2 * Math.PI + 0.5, g = [Math.sin(a) * 3.9, 0.02, Math.cos(a) * 3.9];
    rod(A, [0, 6.0, 0], g, 0.006, "cable", { seg: 4 });
    rod(A, g, [g[0] * 1.02, 0.35, g[2] * 1.02], 0.025, "steel", { seg: 6 });
    A.box([g[0] - 0.08, 0, g[2] - 0.08], [g[0] + 0.08, 0.02, g[2] + 0.08], "pl_orange", D);
  }
  for (const yaw of [0, 90]) A.obox([0, 9.05, 0], [1.4, 0.025, 0.025], [0, yaw, 0], "steel", D);
  for (const [x, z] of [[0.7, 0], [-0.7, 0], [0, 0.7], [0, -0.7]]) rod(A, [x, 9.05, z], [x, 9.6, z], 0.012, "pl_black", { seg: 4 });
  A.box([-0.15, 8.6, 0.06], [0.15, 8.95, 0.1], "pl_grey", D);
  rod(A, [0.05, 9.0, 0.04], [0.06, 0.1, 0.05], 0.015, "cable", { seg: 4 });
  A.cyl([0.6, 0.25, 0.5], [0.5, 0.4, 0.5], "cable", { k: 0, seg: 14, r: [0, 0, 90] });
  return fin(A, { effect: "none" });
}
function weaponRack() {
  const A = mk("mod_weapon_rack", "Weapon rack (modern, loaded)", "Military",
    "Free-standing steel arms rack, 1.4 x 0.45 x 1.3 m: a butt tray with eight slots, a notched upper barrel rest and a lockable hinged retaining bar, holding five generic black rifles (stock, receiver, magazine, rail and barrel as simple shapes, no real model).", [1.5, 0.55]);
  const hx = 0.7, m = "p_dgrey";
  A.box([-hx, 0, -0.22], [hx, 0.06, 0.22], m, { bv: 0.006 });
  A.box([-hx, 0.06, -0.2], [hx, 0.2, 0.2], m, { k: 0, bv: 0.006 });
  for (const x of [-hx, hx]) { A.box([x - 0.025, 0, -0.18], [x + 0.025, 1.3, -0.13], m, vb(0.004)); bar(A, [x, 0.06, 0.2], [x, 1.2, -0.15], 0.04, 0.04, m, D); }
  A.box([-hx, 1.05, -0.2], [hx, 1.12, -0.08], m, vb(0.004));
  for (let i = 0; i < 8; i++) { const x = -0.6 + i * 0.17; A.box([x - 0.03, 1.08, -0.081], [x + 0.03, 1.121, -0.079], "seam", D); A.box([x - 0.05, 0.199, -0.12], [x + 0.05, 0.201, 0.12], "seam", D); }
  rod(A, [-hx, 0.9, -0.06], [hx, 0.9, -0.06], 0.03, "steel", { seg: 6 });
  A.box([hx - 0.05, 0.85, -0.04], [hx + 0.03, 0.95, 0.0], "steel_dk", D);
  for (const i of [0, 1, 3, 4, 6]) {
    const x = -0.6 + i * 0.17, base = [x, 0.2, 0.05], top = [x, 1.1, -0.14];
    const d = [0, top[1] - base[1], top[2] - base[2]], L = Math.hypot(d[1], d[2]), u = [0, d[1] / L, d[2] / L];
    const p = (t, off = 0) => [x, base[1] + u[1] * t + off * u[2], base[2] + u[2] * t - off * u[1]];
    bar(A, p(0), p(0.3), 0.045, 0.1, "pl_black", D);
    bar(A, p(0.3), p(0.62), 0.05, 0.075, "trim", D);
    bar(A, p(0.62), p(0.85), 0.045, 0.055, "pl_black", D);
    rod(A, p(0.85), p(1.02), 0.022, "steel_dk", { seg: 6 });
    bar(A, p(0.42, 0.07), p(0.5, 0.2), 0.03, 0.06, "pl_black", D);
    bar(A, p(0.45, -0.07), p(0.6, -0.07), 0.03, 0.04, "pl_black", D);
  }
  A.box([-hx, 0, -0.22], [hx, 1.3, 0.22], m, H);
  return fin(A, { cover: [] });
}
function hardCases() {
  const A = mk("mod_hard_cases", "Rugged hard cases (stack)", "Military",
    "Stack of rugged moulded transport cases: a long 1.2 m case lying flat, a medium case on top and a small case beside, each with a reinforced rim, ribbed lids, latches, folding handles and a pressure-relief valve.", [1.35, 0.9]);
  const caseAt = (x0, y0, z0, w, h, d, m) => {
    A.rbox([x0 - w / 2, y0, z0 - d / 2], [x0 + w / 2, y0 + h, z0 + d / 2], 0.03, m);
    A.box([x0 - w / 2 - 0.006, y0 + h * 0.62, z0 - d / 2 - 0.006], [x0 + w / 2 + 0.006, y0 + h * 0.68, z0 + d / 2 + 0.006], m, dv(0.006));
    seam(A, [x0 - w / 2 - 0.007, y0 + h * 0.6, z0 - d / 2 - 0.007], [x0 + w / 2 + 0.007, y0 + h * 0.605, z0 + d / 2 + 0.007]);
    const nl = w > 0.8 ? 4 : 2;
    for (let i = 0; i < nl; i++) { const x = x0 - w / 2 + (i + 0.5) * (w / nl); A.box([x - 0.035, y0 + h * 0.48, z0 + d / 2], [x + 0.035, y0 + h * 0.66, z0 + d / 2 + 0.02], "pl_black", D); }
    for (let i = 1; i < 4; i++) A.box([x0 - w / 2 + 0.06, y0 + h + 0.001, z0 - d / 2 + i * (d / 4) - 0.012], [x0 + w / 2 - 0.06, y0 + h + 0.008, z0 - d / 2 + i * (d / 4) + 0.012], m, D);
    A.box([x0 - 0.08, y0 + h * 0.75, z0 + d / 2 + 0.01], [x0 + 0.08, y0 + h * 0.8, z0 + d / 2 + 0.035], "pl_black", D);
    A.cyl([x0 + w / 2 - 0.1, y0 + h * 0.8, z0 + d / 2 + 0.004], [0.04, 0.008, 0.04], "steel", { ...D, seg: 8, r: [90, 0, 0] });
  };
  caseAt(0, 0, 0, 1.2, 0.22, 0.48, "pl_black");
  caseAt(-0.15, 0.22, 0, 0.62, 0.32, 0.45, "pl_tan");
  caseAt(0.42, 0.22, 0.02, 0.36, 0.2, 0.3, "pl_olive");
  return fin(A, { cover: [cover([0, 0, -0.75], 0, "low", ["over"])] });
}
function camoNet() {
  const A = mk("mod_camo_net", "Camouflage net on poles", "Military",
    "Woodland camouflage net, 6 x 5 m, stretched over four 2.3 m corner poles and a 2.8 m centre pole with spreader discs, sagging between them, its garnished edges hanging in drops and tied off with guy lines to pegs. Walk under it.", [7.2, 6.2]);
  const hx = 3, hz = 2.5, h = (x, z) => 2.3 + 0.45 * Math.max(0, 1 - Math.hypot(x / 1.6, z / 1.4)) - 0.25 * Math.sin((Math.PI * (x + hx)) / (2 * hx)) * Math.sin((Math.PI * (z + hz)) / (2 * hz)) * (1 - Math.max(0, 1 - Math.hypot(x / 1.6, z / 1.4)));
  const R = rng(808);
  for (const [x, z] of [[-hx, -hz], [hx, -hz], [hx, hz], [-hx, hz]]) {
    rod(A, [x, 0, z], [x, 2.32, z], 0.05, "wood_dk", { k: 1, seg: 8 });
    A.cyl([x, 2.33, z], [0.2, 0.02, 0.2], "pl_olive", { ...D, seg: 10 });
    const g = [x * 1.15, 0.02, z * 1.18];
    rod(A, [x, 2.3, z], g, 0.006, "cable", { seg: 4 });
    A.box([g[0] - 0.02, 0, g[2] - 0.02], [g[0] + 0.02, 0.2, g[2] + 0.02], "steel", D);
  }
  rod(A, [0, 0, 0], [0, 2.78, 0], 0.06, "wood_dk", { k: 1, seg: 8 });
  A.cyl([0, 2.79, 0], [0.4, 0.03, 0.4], "pl_olive", { ...D, seg: 12 });
  const n = 8, m = 7;
  for (let i = 0; i < n; i++) for (let j = 0; j < m; j++) {
    const x0 = -hx + (i * 2 * hx) / n, x1 = -hx + ((i + 1) * 2 * hx) / n, z0 = -hz + (j * 2 * hz) / m, z1 = -hz + ((j + 1) * 2 * hz) / m;
    const xc = (x0 + x1) / 2, zc = (z0 + z1) / 2, yc = h(xc, zc);
    const ax = (Math.atan2(h(x1, zc) - h(x0, zc), x1 - x0)) * DEG, az = (Math.atan2(h(xc, z1) - h(xc, z0), z1 - z0)) * DEG;
    A.obox([xc, yc + 0.06, zc], [x1 - x0 + 0.08, 0.025, z1 - z0 + 0.08], [-az, 0, ax], (i + j) % 3 ? "camo" : "camo_b", { k: 0 });
    if (R() > 0.5) A.obox([xc + (R() - 0.5) * 0.3, yc + 0.09, zc + (R() - 0.5) * 0.3], [0.35, 0.04, 0.25], [R() * 20, R() * 180, R() * 20], "camo_b", D);
  }
  for (const s of [-1, 1]) for (let k = 0; k < 9; k++) {
    const x = -hx + 0.3 + k * 0.68; A.box([x - 0.2, 1.75 + R() * 0.25, s * (hz + 0.05) - 0.02], [x + 0.2, h(x, s * hz) + 0.05, s * (hz + 0.05) + 0.02], k % 2 ? "camo" : "camo_b", D);
    const z = -hz + 0.3 + k * 0.55; if (z < hz) A.box([s * (hx + 0.05) - 0.02, 1.8 + R() * 0.2, z - 0.2], [s * (hx + 0.05) + 0.02, h(s * hx, z) + 0.05, z + 0.2], k % 2 ? "camo_b" : "camo", D);
  }
  return fin(A, { effect: "none" });
}

// ================================================================================================= DESTRUCTION
function rubblePile(large) {
  const S = large ? 2.4 : 1.2, R = rng(large ? 9001 : 9002);
  const A = mk(large ? "mod_rubble_large" : "mod_rubble_small", large ? "Concrete rubble pile (large)" : "Concrete rubble pile (small)", "Destruction",
    `Heap of demolished reinforced concrete, about ${2 * S + 0.5} m across and ${large ? "1.6" : "0.8"} m high: broken slabs and blocks with bent rebar sticking out, smaller lumps, dust and brick fragments.`, [2 * S + 1.4, 2 * S + 1.0]);
  A.rbox([-S, -0.2, -S * 0.85], [S, S * 0.38, S * 0.85], S * 0.35, "rubble", { r: [0, R() * 30, 0] });
  A.rbox([-S * 0.6, 0, -S * 0.5], [S * 0.5, S * 0.62, S * 0.45], S * 0.25, "rubble", { r: [0, R() * 60, 0] });
  for (let i = 0; i < (large ? 7 : 4); i++) {
    const a = R() * 6.28, d = R() * S * 0.6, l = S * (0.4 + R() * 0.5), w = S * (0.25 + R() * 0.35), t = 0.12 + R() * 0.12;
    const c = [Math.cos(a) * d, S * 0.3 + R() * S * 0.25, Math.sin(a) * d * 0.8], r = [R() * 50 - 25, R() * 180, R() * 50 - 25];
    A.obox(c, [l, t, w], r, R() > 0.5 ? "conc" : "conc_st", { k: i < 3 ? 1 : 0, bv: 0.02 });
    for (let k = 0; k < 3; k++) rod(A, [c[0] + (R() - 0.5) * l * 0.6, c[1], c[2] + (R() - 0.5) * w * 0.6], [c[0] + (R() - 0.5) * l * 1.4, c[1] + 0.2 + R() * 0.4, c[2] + (R() - 0.5) * w * 1.4], 0.016, "rebar", { seg: 6 });
  }
  for (let i = 0; i < (large ? 30 : 16); i++) {
    const a = R() * 6.28, d = S * (0.3 + R() * 0.75), s = 0.06 + R() * 0.22;
    A.obox([Math.cos(a) * d, s * 0.35 + (d < S * 0.6 ? S * 0.2 : 0), Math.sin(a) * d * 0.85], [s * 1.4, s, s * 1.1], [R() * 60, R() * 180, R() * 60], ["conc", "conc_dk", "debris", "brick", "conc_l"][i % 5], { k: 0, bv: s * 0.15 });
  }
  A.cyl([0, 0.005, 0], [S * 2.5, 0.01, S * 2.2], "rubble", { ...D, seg: 18 });
  return fin(A, { cover: large ? [cover([0, 0, -S - 0.4], 0, "low", ["over", "left", "right"]), cover([0, 0, S + 0.3], 180, "low", ["over", "left", "right"]), cover([-S - 0.4, 0, 0], 90, "low", ["over"])] : [cover([0, 0, -S - 0.3], 0, "low", ["over"])] });
}
function wallChunks() {
  const A = mk("mod_wall_chunks", "Broken wall chunks", "Destruction",
    "Three large broken pieces of a rendered brick-and-concrete wall: one leaning up against another, one face-down, with plaster on one face and brick showing on the other, ragged edges with rebar and brick fragments round them.", [4.0, 3.8]);
  const R = rng(4040);
  const chunk = (c, s, r) => {
    A.obox(c, s, r, "brick", { bv: 0.02 });
    const t = r.map((x) => x / DEG);
    const n = [Math.sin(t[1]) * Math.cos(t[0]), -Math.sin(t[0]), Math.cos(t[1]) * Math.cos(t[0])];
    A.obox(add(c, n, s[2] / 2 + 0.006), [s[0] - 0.06, s[1] - 0.06, 0.012], r, "plaster", D);
    for (let k = 0; k < 4; k++) rod(A, add(c, [(R() - 0.5) * s[0], (R() - 0.5) * s[1] * 0.4, 0], 1), add(c, [(R() - 0.5) * s[0] * 1.3, s[1] * 0.55 + R() * 0.3, (R() - 0.5) * 0.3], 1), 0.016, "rebar", { seg: 6 });
  };
  chunk([-0.6, 0.85, 0.2], [1.8, 1.4, 0.25], [-70, 15, 0]);
  chunk([0.9, 0.62, -0.3], [1.5, 1.3, 0.25], [-18, -30, 6]);
  chunk([-0.3, 0.14, -0.9], [1.6, 1.1, 0.25], [-88, 60, 0]);
  for (let i = 0; i < 18; i++) { const s = 0.06 + R() * 0.16; A.obox([(R() - 0.5) * 3.6, s * 0.3, (R() - 0.5) * 2.6], [s * 1.6, s * 0.7, s], [R() * 40, R() * 180, R() * 40], i % 3 ? "debris" : "plaster", { k: 0, bv: s * 0.15 }); }
  return fin(A, { cover: [cover([-0.6, 0, 1.1], 180, "low", ["over"]), cover([1.0, 0, -1.2], 0, "low", ["over", "right"])] });
}
function collapsedSlab() {
  const A = mk("mod_collapsed_slab", "Collapsed ceiling slab", "Destruction",
    "A 4 x 3 m section of reinforced concrete floor slab fallen in: one edge on the ground, the broken edge propped 1.7 m up on a rubble heap, the plastered ceiling underside showing, a ragged end bristling with bent bar mesh, a floor finish on top and dust and debris beneath.", [4.6, 4.7]);
  const R = rng(6060), ang = Math.asin(1.6 / 3.6) * DEG;
  const c = [0, 0.95, 0];
  A.obox(c, [4.0, 0.2, 3.6], [-ang, 0, 0], "conc", { bv: 0.02 });
  const n = [0, Math.cos(ang / DEG), Math.sin(ang / DEG)];
  A.obox(add(c, n, -0.105), [3.9, 0.012, 3.5], [-ang, 0, 0], "plaster", D);
  A.obox(add(c, n, 0.106), [3.95, 0.012, 3.55], [-ang, 0, 0], "conc_l", D);
  const ez = -Math.cos(ang / DEG) * 1.8, ey = 0.95 + Math.sin(ang / DEG) * 1.8;
  for (let i = 0; i < 14; i++) { const x = -1.9 + i * 0.29; rod(A, [x, ey - 0.05, ez + 0.05], [x + (R() - 0.5) * 0.3, ey - 0.1 - R() * 0.4, ez - 0.25 - R() * 0.3], 0.012, "rebar", { seg: 4 }); }
  for (let i = 0; i < 4; i++) rod(A, [-2, ey - 0.05 - i * 0.02, ez + 0.05 + i * 0.01], [2, ey - 0.05 - i * 0.02, ez + 0.05 + i * 0.01], 0.012, "rebar", { seg: 4 });
  A.rbox([-1.8, -0.1, ez - 0.7], [1.8, ey - 0.1, ez + 0.5], 0.35, "rubble");
  for (let i = 0; i < 16; i++) { const s = 0.08 + R() * 0.2; A.obox([(R() - 0.5) * 4, s * 0.35, ez - 0.5 + R() * 2.5], [s * 1.5, s * 0.8, s], [R() * 40, R() * 180, R() * 40], ["conc_dk", "debris", "plaster", "conc"][i % 4], { k: 0, bv: s * 0.15 }); }
  return fin(A, { cover: [cover([-1.0, 0, 1.6], 180, "low", ["over"]), cover([1.0, 0, 1.6], 180, "low", ["over"]), cover([-1.0, 0, ez - 1.0], 0, "high", ["left", "right"])] });
}
function glassPile() {
  const A = mk("mod_glass_pile", "Shattered glass", "Destruction",
    "Scatter of shattered window glass on the ground, about 2.5 x 2 m: large and small shards, crumbs of toughened glass and a bent aluminium glazing bead. Purely decorative (no collision).", [2.6, 2.1]);
  const R = rng(1717);
  for (let i = 0; i < 60; i++) {
    const a = R() * 6.28, d = Math.sqrt(R()) * 1.1, s = i < 12 ? 0.12 + R() * 0.2 : 0.02 + R() * 0.06;
    A.obox([Math.cos(a) * d, 0.004 + R() * 0.01, Math.sin(a) * d * 0.8], [s, 0.005, s * (0.4 + R() * 0.6)], [R() * 8, R() * 180, R() * 8], i % 4 ? "glass_crack" : "glass", D);
  }
  bar(A, [-0.8, 0.012, 0.3], [0.4, 0.015, 0.55], 0.03, 0.02, "steel", D);
  bar(A, [0.4, 0.015, 0.55], [0.7, 0.05, 0.7], 0.03, 0.02, "steel", D);
  return fin(A, { effect: "none" });
}
function burnedShell() {
  const A = mk("mod_car_shell_burned", "Burned car shell (on its roof)", "Destruction",
    "Generic burned-out car shell overturned on its crushed roof: rust-brown blistered panels, empty window frames, the floor pan, exhaust, axles and bare rims pointing up, soot on the ground round it.", [2.4, 5.1]);
  const R = rng(2525), W = 1.8, hx = W / 2, L = 4.6, zN = L / 2, yb0 = 0.6, yb1 = 1.25;
  A.cyl([0, 0.004, 0], [W + 1.6, 0.008, L + 1.0], "soot", { ...D, seg: 20 });
  A.box([-hx + 0.12, 0, -1.0], [hx - 0.12, 0.12, 0.6], "burnt_dk", { bv: 0.02 });
  for (const s of [-1, 1]) {
    bar(A, [s * (hx - 0.12), 0.1, 0.6], [s * (hx - 0.06), yb0, 1.1], 0.06, 0.06, "burnt");
    bar(A, [s * (hx - 0.12), 0.1, -1.0], [s * (hx - 0.06), yb0, -1.5], 0.06, 0.08, "burnt");
    bar(A, [s * (hx - 0.12), 0.1, -0.2], [s * (hx - 0.08), yb0, -0.25], 0.06, 0.06, "burnt");
  }
  extrudeX(A, -hx, hx, [[-zN, yb0], [zN, yb0], [zN, yb1 - 0.15], [zN - 0.15, yb1], [-zN + 0.1, yb1], [-zN, yb1 - 0.1]], "burnt");
  extrudeX(A, -hx, hx, [[zN - 1.4, yb0 - 0.02], [zN, yb0 - 0.02], [zN, yb0 - 0.0], [zN - 1.4, yb0]], "burnt");
  A.obox([0, yb0 - 0.02, zN - 0.7], [W - 0.05, 0.1, 1.35], [6, 0, 0], "burnt", V);
  for (const s of [-1, 1]) for (const z of [-0.95, -0.2]) seam(A, [s * hx - 0.004, yb0 + 0.1, z - 0.004], [s * hx + 0.004, yb1 - 0.05, z + 0.004]);
  A.box([-hx + 0.25, yb1, -1.5], [hx - 0.25, yb1 + 0.05, 1.4], "rust_h", V);
  rod(A, [0.4, yb1 + 0.08, -zN - 0.05], [0.4, yb1 + 0.08, 0.8], 0.06, "rust_h", { seg: 8 });
  A.cyl([0.4, yb1 + 0.1, -1.3], [0.2, 0.5, 0.2], "rust_h", { ...D, seg: 10, r: [90, 0, 0] });
  for (const z of [1.4, -1.45]) {
    rod(A, [-hx + 0.1, yb1 - 0.05, z], [hx - 0.1, yb1 - 0.05, z], 0.07, "rust_h", { seg: 8 });
    for (const s of [-1, 1]) wheel(A, [s * (hx - 0.13), yb1 - 0.05, z], 0.63, 0.2, s, { tyre: null, rim: "burnt_dk" });
  }
  for (let i = 0; i < 10; i++) A.obox([(R() - 0.5) * 2.4, 0.01, (R() - 0.5) * 4.6], [0.1 + R() * 0.15, 0.015, 0.05 + R() * 0.1], [0, R() * 180, 0], "burnt_dk", D);
  A.box([-hx, 0, -zN], [hx, yb1, zN], "burnt", H);
  for (const p of A.parts) if (p.t === "p") p.k = 0;
  return fin(A, { cover: vehicleCover(L, W, 1.25) });
}
function asphaltCrater() {
  const A = mk("mod_asphalt_crater", "Cratered asphalt patch", "Destruction",
    "Shell / IED crater blown in an asphalt road, about 4.5 m: the surface cut down 0.6 m into the base course, broken asphalt slabs heaved up round the lip, loose gravel and lumps, and a scorch ring. Carves the terrain.", [4.8, 4.8]);
  const R = rng(3131), r0 = 1.6;
  const outline = Array.from({ length: 14 }, (_, i) => { const a = (i / 14) * 2 * Math.PI, rr = r0 * (0.92 + R() * 0.16); return [Math.cos(a) * rr, Math.sin(a) * rr]; });
  A.cyl([0, 0.003, 0], [4.6, 0.006, 4.6], "soot", { ...D, seg: 24 });
  for (let i = 0; i < 16; i++) {
    const a = (i / 16) * 2 * Math.PI + R() * 0.2, d = r0 + 0.05 + R() * 0.25, s = 0.4 + R() * 0.35, tilt = 18 + R() * 25;
    A.obox([Math.cos(a) * d, 0.08 + R() * 0.05, Math.sin(a) * d], [s, 0.09, s * 0.7], [0, -a * DEG + 90, 0], "asphalt", { k: 0, bv: 0.02 });
    A.obox([Math.cos(a) * (d - 0.05), 0.1, Math.sin(a) * (d - 0.05)], [s * 0.8, 0.08, s * 0.5], [tilt, 90 - a * DEG, 0], "asphalt", { k: 0, bv: 0.015 });
  }
  for (let i = 0; i < 24; i++) { const a = R() * 6.28, d = R() * 2.2, s = 0.05 + R() * 0.12; A.obox([Math.cos(a) * d, s * 0.3 - (d < r0 ? 0.35 * (1 - d / r0) : 0), Math.sin(a) * d], [s * 1.4, s * 0.6, s], [R() * 40, R() * 180, R() * 40], i % 3 ? "asphalt_dk" : "gravel", { k: 0, bv: s * 0.15 }); }
  return fin(A, { effect: "none", cuts: [{ outline, depth: 0.6, bank: 0.7 }] });
}

// ================================================================================================= CATALOG
export function modernPropsAssets() {
  return [
    // barriers
    jersey("mod_jersey_barrier", "Jersey barrier (3 m)", "Precast New Jersey profile concrete barrier, 3.0 m x 0.61 m x 0.81 m: 55 / 84 degree faces, drain / forklift slots, lifting holes, pin-and-loop connectors, chipped arrises and toe grime.", "straight"),
    jersey("mod_jersey_barrier_end", "Jersey barrier end section", "Terminal 2.0 m jersey barrier section whose last 0.75 m slopes down to a low nose with a reflective end marker; pin socket at the joining end.", "end"),
    jersey("mod_jersey_barrier_damaged", "Jersey barrier (damaged)", "Shot-up / rammed 3 m jersey barrier: a section of the upper profile broken away to expose bent reinforcing bars, a slumped remnant, fracture faces and chunks on the ground.", "damaged"),
    highwayDivider(), ecologyBlock(), tWall(), bollard("concrete"), bollard("steel"), bollard("up"), bollard("down"),
    // fortifications
    hesco(1, 1), hesco(2, 1), hesco(3, 1), hesco(1, 2), hesco(3, 2), hesco(2, 1, true),
    sandbagPosition(), guardTower(), overheadPosition(),
    // fences / gates
    chainLinkPanel(false), chainLinkPanel(true), chainLinkGate(false), chainLinkGate(true), slidingGate(), razorCoil(), constructionFence(), palisade(),
    // road
    cone(true), cone(false), drum(), sawhorse(), waterBarrier("red"), waterBarrier("white"), roadblock(), tyrePile(), speedBump(),
    // vehicles
    ...vehicles(),
    // industrial
    containerAsset("mod_container_20", "Shipping container 20 ft (closed)", "ISO 20 ft dry cargo container, 6.06 x 2.44 x 2.59 m in red: corrugated sides and end, frame rails and corner posts, eight corner castings, fork pockets, roof stiffeners, vents, two cargo doors with four locking bars, cam keepers, hinges and gasket, a blank data plate.", { len: 20, m: "c_red" }),
    containerAsset("mod_container_40", "Shipping container 40 ft (closed)", "ISO 40 ft dry cargo container, 12.19 x 2.44 x 2.59 m in blue, closed: corrugated walls, frame, corner castings, vents and cargo doors with locking bars.", { len: 40, m: "c_blue" }),
    containerAsset("mod_container_20_open", "Shipping container 20 ft (doors open)", "Green 20 ft container with both cargo doors swung open: a walkable timber floor and bare corrugated interior. Enterable.", { len: 20, m: "c_green", open: true }),
    containerAsset("mod_container_40_open", "Shipping container 40 ft (doors open)", "Tan 40 ft container with both cargo doors swung open: a 12 m walkable interior with a timber floor. Enterable.", { len: 40, m: "c_tan", open: true }),
    containerAsset("mod_container_40_stack", "Shipping containers 40 ft (stacked double)", "Two 40 ft containers stacked on twistlocks, grey under blue: a 5.2 m high wall of steel.", { len: 40, m: "c_grey", m2: "c_blue", stack: true }),
    containerAsset("mod_container_20_damaged", "Shipping container 20 ft (damaged)", "Abandoned 20 ft container: a stove-in side panel, rust breaking through the paint, rusted frame and one door hanging ajar.", { len: 20, m: "c_blue", damage: true }),
    dumpsterFront(), dumpsterRolloff(), generatorSet(), compressor(), hvacUnit(), cableSpool(true), cableSpool(false), ibcTote(),
    // small cover
    pallet(), palletStack(), crates("plastic"), crates("plastic_stack"), crates("wood"), barrel("steel"), barrel("rust"), barrel("plastic"), barrel("group"), toolChest(), jerryCans(),
    // urban
    busShelter(), streetLight(false), streetLight(true), trafficLight(), utilityPole(), electricalCabinet(), fireHydrant(), mailbox(), trashBin(), newspaperBox(), parkBench(), bikeRack(), parkingMeter(), signPost("stop"), signPost("street"), manhole(),
    // construction
    scaffoldBay(), concretePipe(false), concretePipe(true), excavator(), lumberStack(), rebarBundle(), cinderWall(), portableToilet(), builderSkip(),
    // terrain
    rock("small"), rock("medium"), rock("large"), retainingWall(), fallenTree(), culvertHeadwall(),
    // military
    ammoStack(), fieldRadio(), milGenerator(), antennaMast(), weaponRack(), hardCases(), camoNet(),
    // destruction
    rubblePile(false), rubblePile(true), wallChunks(), collapsedSlab(), glassPile(), burnedShell(), asphaltCrater(),
  ];
}
