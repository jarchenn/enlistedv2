// Ammunition dressing shared by every bunker / emplacement generator (SPEC 10.2). Part builders only: each call adds
// one furniture group (visible parts, one invisible collider) to an Asset, so the boxes appear wherever the prefab is
// placed, in creative too. Two kinds:
//   smallArmsCache  - 7.92 mm MG ammunition boxes, a wooden rifle-ammunition crate and stripper-clip bandoliers
//                     (German positions), or M1 .30 cal cans and a wooden carbine/rifle crate (US-held positions)
//   gunAmmoStack    - stacked wooden shell crates, wicker shell carriers and a few loose rounds next to a gun
//                     (PaK casemate, R667 / R612, Flak 18 emplacement)
// Sizes from period dimensions (Patronenkasten 34: 0.28 x 0.11 x 0.19 m; M1 .30 can: 0.27 x 0.10 x 0.18 m; 7.5 cm
// shell crate for 3 rounds ~0.86 x 0.32 x 0.20 m; 8.8 cm Flak crate for 3 rounds ~1.05 x 0.42 x 0.24 m).
// The resupply interaction stays on the existing crate assets (crate_pak40_he / crate_mixed_supply via "companions");
// these boxes are dressing ([DECISION] "decorative plus resupply if a resupply system exists": resupply is the crates).
// Usage: import { smallArmsCache, gunAmmoStack } from "./catalog_ammo.mjs"; smallArmsCache(A, "ammo", x, y, z, 90);

const MATS = {
  ammo_steel_de: [[0.19, 0.2, 0.17], 0.55, "metal", { metallic: 0.6 }],     // feldgrau steel box
  ammo_steel_us: [[0.2, 0.22, 0.13], 0.6, "metal", { metallic: 0.55 }],     // olive drab can
  ammo_wood: [[0.42, 0.33, 0.21], 0.85, "wood", { grain: 0 }],
  ammo_wood_dark: [[0.3, 0.25, 0.17], 0.88, "wood", { grain: 0 }],
  ammo_stencil: [[0.78, 0.76, 0.68], 0.8, "plain"],
  ammo_wicker: [[0.45, 0.37, 0.22], 0.9, "fabric"],
  ammo_brass: [[0.62, 0.48, 0.22], 0.35, "metal", { metallic: 0.9 }],
  ammo_shell_steel: [[0.27, 0.27, 0.24], 0.5, "metal", { metallic: 0.7 }],
  ammo_canvas: [[0.36, 0.33, 0.24], 0.95, "fabric"],
  ammo_rope: [[0.5, 0.44, 0.32], 0.95, "plain"],
};
function ensure(A, name) {
  if (A.materials.some((m) => m.name === name)) return name;
  const [albedo, rough, pattern, extra] = MATS[name];
  A.mat(name, albedo, rough, pattern, extra || {});
  return name;
}
// place a box given in the group's local frame (origin o, yaw deg about +Y); centre c, size s, extra local yaw
function put(A, o, yaw, c, s, m, ry = 0) {
  const r = (yaw * Math.PI) / 180, cs = Math.cos(r), sn = Math.sin(r);
  const wx = o[0] + c[0] * cs + c[2] * sn, wz = o[2] - c[0] * sn + c[2] * cs;
  A.obox([wx, o[1] + c[1], wz], s, [0, yaw + ry, 0], ensure(A, m));
}
function putR(A, o, yaw, c, s, m, rot, opt = {}) {   // full local rotation (lying rounds / carriers)
  const r = (yaw * Math.PI) / 180, cs = Math.cos(r), sn = Math.sin(r);
  const wx = o[0] + c[0] * cs + c[2] * sn, wz = o[2] - c[0] * sn + c[2] * cs;
  return A.obox([wx, o[1] + c[1], wz], s, [rot[0], yaw + rot[1], rot[2]], ensure(A, m), opt);
}
// P1 (B1 kit): detail parts. putB: a chamfered box that only draws (k:0; endGroup keeps it out of the group's bounding
// collider, which endFurniture takes from every plain box); putC: a cylinder / frustum lying along the item's local x
// (turned by ry about y), d its diameter; putA: an arch (handles), local rotation like putR.
const W_ = (o, yaw, c) => { const r = (yaw * Math.PI) / 180; return [o[0] + c[0] * Math.cos(r) + c[2] * Math.sin(r), o[1] + c[1], o[2] - c[0] * Math.sin(r) + c[2] * Math.cos(r)]; };
function putB(A, o, yaw, c, s, m, bv, ry = 0) { A._target.push({ t: "b", c: W_(o, yaw, c).map(R4), s: s.map(R4), r: [0, R4(yaw + ry), 0], m: ensure(A, m), bv: R4(Math.min(bv, Math.min(...s) / 2 - 1e-4)), k: 0, _d: 1 }); }
// endFurniture with the detail boxes hidden from its bounds, so the group collider is exactly version 1's
function endGroup(A) {
  const d = A._furn.parts.filter((p) => p._d);
  for (const p of d) p.t = "_";
  A.endFurniture(true);
  for (const p of d) { p.t = "b"; delete p._d; }
}
function putC(A, o, yaw, c, d, len, m, ry = 0, opt = {}) { A.cyl(W_(o, yaw, c), [d, len, d], ensure(A, m), { r: [90, yaw + ry + 90, 0], k: 0, ...opt }); }
function putA(A, o, yaw, c, s, m, rot, th) { A.arch(W_(o, yaw, c), s, ensure(A, m), { r: [rot[0], yaw + rot[1], rot[2]], th, k: 0 }); }
const R4 = (v) => Math.round(v * 10000) / 10000;
// the version 1 box of an item kept as an invisible bounds-keeper, so the group's bounding collider is unchanged
const keep = { v: 0 };

// ---- single items (local frame: x along the long side, y up from the base, z depth)
export function mgAmmoBox(A, o, yaw, c, us = false) {
  const m = us ? "ammo_steel_us" : "ammo_steel_de";
  const [L, W, H] = us ? [0.27, 0.1, 0.18] : [0.28, 0.11, 0.19];
  put(A, o, yaw, [c[0], c[1] + H / 2, c[2]], [L, H, W], m);
  put(A, o, yaw, [c[0], c[1] + H + 0.012, c[2]], [L * 0.55, 0.024, 0.02], m, 0);   // carry handle: bounds only now
  A._target[A._target.length - 1].v = 0;
  put(A, o, yaw, [c[0] + L * 0.42, c[1] + H * 0.8, c[2]], [0.03, 0.04, W + 0.006], m);   // lid latch
  A._target[A._target.length - 3].bv = 0.006; A._target[A._target.length - 1].bv = 0.004;
  putB(A, o, yaw, [c[0], c[1] + H * 0.86, c[2]], [L + 0.006, 0.012, W + 0.006], m, 0.003);              // lid seam
  for (const x of [-L * 0.25, L * 0.25]) putB(A, o, yaw, [c[0] + x, c[1] + H * 0.42, c[2]], [0.012, H * 0.6, W + 0.004], m, 0.003);   // pressed ribs
  putA(A, o, yaw, [c[0], c[1] + H + 0.002, c[2]], [L * 0.5, 0.03, 0.012], m, [0, 0, 0], 0.008);       // wire carry handle
  for (const x of [-L * 0.25, L * 0.25]) putB(A, o, yaw, [c[0] + x, c[1] + H + 0.004, c[2]], [0.02, 0.012, 0.024], m, 0.003);   // handle lugs
}
export function rifleCrate(A, o, yaw, c, dark = false) {
  const m = dark ? "ammo_wood_dark" : "ammo_wood", L = 0.46, W = 0.3, H = 0.19;
  put(A, o, yaw, [c[0], c[1] + H / 2, c[2]], [L, H, W], m);
  A._target[A._target.length - 1].bv = 0.006;
  for (const x of [-L / 2 + 0.03, L / 2 - 0.03]) { put(A, o, yaw, [c[0] + x, c[1] + H / 2, c[2]], [0.04, H + 0.01, W + 0.012], m); A._target[A._target.length - 1].bv = 0.006; }   // end battens
  put(A, o, yaw, [c[0], c[1] + H * 0.55, c[2] + W / 2 + 0.004], [0.16, 0.05, 0.004], "ammo_stencil");                              // stencil
  for (const x of [-L / 2 - 0.012, L / 2 + 0.012]) { put(A, o, yaw, [c[0] + x, c[1] + H * 0.6, c[2]], [0.012, 0.03, 0.1], "ammo_rope"); A._target[A._target.length - 1].v = 0; }   // bounds of the old handles
  for (const sx of [-1, 1]) putA(A, o, yaw, [c[0] + sx * (L / 2 + 0.008), c[1] + H * 0.6 - 0.035, c[2]], [0.1, 0.04, 0.012], "ammo_rope", [180, 90, 0], 0.012);   // rope loops
  putB(A, o, yaw, [c[0], c[1] + H - 0.004, c[2]], [L - 0.1, 0.012, W + 0.004], m, 0.003);   // lid board seam
}
export function bandolier(A, o, yaw, c, ry = 0) {
  put(A, o, yaw, [c[0], c[1] + 0.015, c[2]], [0.34, 0.03, 0.075], "ammo_canvas", ry);
  A._target[A._target.length - 1].bv = 0.008;
  for (const x of [-0.11, 0, 0.11]) { put(A, o, yaw, [c[0] + x, c[1] + 0.035, c[2]], [0.07, 0.012, 0.07], "ammo_canvas", ry); A._target[A._target.length - 1].bv = 0.004; }
}
export function shellCrate(A, o, yaw, c, cal = "75") {
  const [L, W, H] = cal === "88" ? [1.05, 0.42, 0.24] : cal === "50" ? [0.72, 0.28, 0.17] : [0.86, 0.32, 0.2];
  put(A, o, yaw, [c[0], c[1] + H / 2, c[2]], [L, H, W], "ammo_wood");
  A._target[A._target.length - 1].bv = 0.008;
  for (const x of [-L / 2 + 0.04, L / 2 - 0.04]) { put(A, o, yaw, [c[0] + x, c[1] + H / 2, c[2]], [0.05, H + 0.012, W + 0.014], "ammo_wood_dark"); A._target[A._target.length - 1].bv = 0.008; }
  put(A, o, yaw, [c[0], c[1] + H + 0.004, c[2]], [L * 0.36, 0.006, W * 0.5], "ammo_stencil");
  for (const z of [-W * 0.25, W * 0.25]) putB(A, o, yaw, [c[0], c[1] + H - 0.003, c[2] + z], [L - 0.14, 0.008, 0.012], "ammo_wood_dark", 0.003);   // lid board seams
  for (const sx of [-1, 1]) putA(A, o, yaw, [c[0] + sx * (L / 2 + 0.006), c[1] + H * 0.62 - 0.04, c[2]], [0.12, 0.045, 0.014], "ammo_rope", [180, 90, 0], 0.013);   // rope handles
  for (const x of [-L * 0.2, L * 0.2]) putB(A, o, yaw, [c[0] + x, c[1] + H * 0.75, c[2] + W / 2 + 0.006], [0.03, 0.06, 0.008], "ammo_steel_de", 0.002);   // latches
}
export function wickerCarrier(A, o, yaw, c, cal = "75") {
  const len = cal === "88" ? 1.0 : 0.78, r = cal === "88" ? 0.075 : 0.06;
  putR(A, o, yaw, [c[0], c[1] + r, c[2]], [len, r * 2, r * 2], "ammo_wicker", [0, 0, 0], keep);   // bounds (the octagon boxes of v1)
  putC(A, o, yaw, [c[0], c[1] + r, c[2]], r * 2, len, "ammo_wicker", 0, { seg: 12 });               // woven tube
  for (const t of [-0.5, 0.5]) putC(A, o, yaw, [c[0] + t * (len - 0.03), c[1] + r, c[2]], r * 2 + 0.008, 0.03, "ammo_wicker", 0, { seg: 12 });   // rims
  for (const t of [-0.2, 0.2]) putC(A, o, yaw, [c[0] + t * len, c[1] + r, c[2]], r * 2 + 0.006, 0.02, "ammo_canvas", 0, { seg: 12 });           // binding bands
  putC(A, o, yaw, [c[0] + len / 2 + 0.01, c[1] + r, c[2]], r * 1.6, 0.02, "ammo_rope", 0, { seg: 10 });                                         // lid
  putA(A, o, yaw, [c[0], c[1] + r * 2 - 0.004, c[2]], [len * 0.4, 0.035, 0.012], "ammo_canvas", [0, 0, 0], 0.012);                               // carrying strap
}
export function looseRound(A, o, yaw, c, cal = "75", ry = 0) {
  const len = cal === "88" ? 0.93 : cal === "50" ? 0.42 : 0.68, r = cal === "88" ? 0.044 : cal === "50" ? 0.025 : 0.038;
  putR(A, o, yaw, [c[0], c[1] + r, c[2]], [len * 0.62, r * 2, r * 2], "ammo_brass", [0, ry, 0], keep);   // bounds of the v1 case
  const rr = (ry * Math.PI) / 180, dx = Math.cos(rr) * len * 0.5, dz = -Math.sin(rr) * len * 0.5;
  putR(A, o, yaw, [c[0] + dx, c[1] + r, c[2] + dz], [len * 0.38, r * 1.7, r * 1.7], "ammo_shell_steel", [0, ry, 0], keep);   // bounds of the v1 projectile
  const at = (t) => [c[0] + Math.cos(rr) * t, c[1] + r, c[2] - Math.sin(rr) * t];   // a point t along the round's axis
  putC(A, o, yaw, at(-len * 0.3), r * 2.15, 0.012, "ammo_brass", ry, { seg: 14 });                       // rim
  putC(A, o, yaw, at(0), r * 2, len * 0.6, "ammo_brass", ry, { seg: 14 });                               // case
  putC(A, o, yaw, at(len * 0.33), r * 1.7, len * 0.07, "ammo_shell_steel", ry, { seg: 14 });             // projectile body
  putC(A, o, yaw, at(len * 0.375), r * 1.78, 0.014, "ammo_brass", ry, { seg: 14 });                      // driving band
  A.cone(W_(o, yaw, at(len * 0.53)), [r * 1.7, len * 0.25, r * 1.7], ensure(A, "ammo_shell_steel"), { r: [90, yaw + ry + 90, 0], top: 0.18, seg: 14, k: 0 });   // ogive
}

// ---- groups (one furniture group each)
// small-arms cache: origin o = [x, y_floor, z], footprint about 1.0 x 0.5 m; us = US-held position (.30 cal cans)
export function smallArmsCache(A, name, x, y, z, yaw = 0, { us = false, boxes = 3, crate = true, bandoliers = 2 } = {}) {
  const o = [x, y, z];
  A.beginFurniture(name);
  for (let k = 0; k < boxes; k++) mgAmmoBox(A, o, yaw, [-0.32 + (k % 2) * 0.3, Math.floor(k / 2) * (us ? 0.18 : 0.19), -0.08 + (k % 2) * 0.01], us);
  if (crate) rifleCrate(A, o, yaw, [0.28, 0, 0.06], us);
  for (let k = 0; k < bandoliers; k++) bandolier(A, o, yaw, [0.28, 0.19 + 0.03 * k, 0.04 - 0.05 * k], 12 * k - 6);
  endGroup(A);
}
// gun ammunition next to a gun: origin o = [x, y_floor, z]; cal "75" (PaK 40 / 5 cm too), "88" (Flak 18), "50" (5 cm KwK)
export function gunAmmoStack(A, name, x, y, z, yaw = 0, { cal = "75", crates = 4, carriers = 2, loose = 2 } = {}) {
  const o = [x, y, z];
  const H = cal === "88" ? 0.24 : cal === "50" ? 0.17 : 0.2, W = cal === "88" ? 0.44 : cal === "50" ? 0.3 : 0.34;
  A.beginFurniture(name);
  for (let k = 0; k < crates; k++) shellCrate(A, o, yaw, [0, Math.floor(k / 2) * H, (k % 2) * W - W / 2], cal);
  for (let k = 0; k < carriers; k++) wickerCarrier(A, o, yaw, [0.05, 0, W + 0.12 + k * 0.17], cal);
  for (let k = 0; k < loose; k++) looseRound(A, o, yaw, [0.75 + 0.1 * k, 0, -0.1 + 0.22 * k], cal, 20 - 35 * k);
  endGroup(A);
}
