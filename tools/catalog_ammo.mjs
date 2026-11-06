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
function putR(A, o, yaw, c, s, m, rot) {   // full local rotation (lying rounds / carriers)
  const r = (yaw * Math.PI) / 180, cs = Math.cos(r), sn = Math.sin(r);
  const wx = o[0] + c[0] * cs + c[2] * sn, wz = o[2] - c[0] * sn + c[2] * cs;
  A.obox([wx, o[1] + c[1], wz], s, [rot[0], yaw + rot[1], rot[2]], ensure(A, m));
}

// ---- single items (local frame: x along the long side, y up from the base, z depth)
export function mgAmmoBox(A, o, yaw, c, us = false) {
  const m = us ? "ammo_steel_us" : "ammo_steel_de";
  const [L, W, H] = us ? [0.27, 0.1, 0.18] : [0.28, 0.11, 0.19];
  put(A, o, yaw, [c[0], c[1] + H / 2, c[2]], [L, H, W], m);
  put(A, o, yaw, [c[0], c[1] + H + 0.012, c[2]], [L * 0.55, 0.024, 0.02], m);           // carry handle
  put(A, o, yaw, [c[0] + L * 0.42, c[1] + H * 0.8, c[2]], [0.03, 0.04, W + 0.006], m);   // lid latch
}
export function rifleCrate(A, o, yaw, c, dark = false) {
  const m = dark ? "ammo_wood_dark" : "ammo_wood", L = 0.46, W = 0.3, H = 0.19;
  put(A, o, yaw, [c[0], c[1] + H / 2, c[2]], [L, H, W], m);
  for (const x of [-L / 2 + 0.03, L / 2 - 0.03]) put(A, o, yaw, [c[0] + x, c[1] + H / 2, c[2]], [0.04, H + 0.01, W + 0.012], m);   // end battens
  put(A, o, yaw, [c[0], c[1] + H * 0.55, c[2] + W / 2 + 0.004], [0.16, 0.05, 0.004], "ammo_stencil");                              // stencil
  for (const x of [-L / 2 - 0.012, L / 2 + 0.012]) put(A, o, yaw, [c[0] + x, c[1] + H * 0.6, c[2]], [0.012, 0.03, 0.1], "ammo_rope");   // rope handles
}
export function bandolier(A, o, yaw, c, ry = 0) {
  put(A, o, yaw, [c[0], c[1] + 0.015, c[2]], [0.34, 0.03, 0.075], "ammo_canvas", ry);
  for (const x of [-0.11, 0, 0.11]) put(A, o, yaw, [c[0] + x, c[1] + 0.035, c[2]], [0.07, 0.012, 0.07], "ammo_canvas", ry);
}
export function shellCrate(A, o, yaw, c, cal = "75") {
  const [L, W, H] = cal === "88" ? [1.05, 0.42, 0.24] : cal === "50" ? [0.72, 0.28, 0.17] : [0.86, 0.32, 0.2];
  put(A, o, yaw, [c[0], c[1] + H / 2, c[2]], [L, H, W], "ammo_wood");
  for (const x of [-L / 2 + 0.04, L / 2 - 0.04]) put(A, o, yaw, [c[0] + x, c[1] + H / 2, c[2]], [0.05, H + 0.012, W + 0.014], "ammo_wood_dark");
  put(A, o, yaw, [c[0], c[1] + H + 0.004, c[2]], [L * 0.36, 0.006, W * 0.5], "ammo_stencil");
}
export function wickerCarrier(A, o, yaw, c, cal = "75") {
  const len = cal === "88" ? 1.0 : 0.78, r = cal === "88" ? 0.075 : 0.06;
  putR(A, o, yaw, [c[0], c[1] + r, c[2]], [len, r * 2, r * 2], "ammo_wicker", [0, 0, 0]);
  putR(A, o, yaw, [c[0], c[1] + r, c[2]], [len * 0.98, r * 1.42, r * 2.08], "ammo_wicker", [45, 0, 0]);   // octagonal look
  put(A, o, yaw, [c[0] + len / 2 + 0.01, c[1] + r, c[2]], [0.02, r * 1.6, r * 1.6], "ammo_rope");
}
export function looseRound(A, o, yaw, c, cal = "75", ry = 0) {
  const len = cal === "88" ? 0.93 : cal === "50" ? 0.42 : 0.68, r = cal === "88" ? 0.044 : cal === "50" ? 0.025 : 0.038;
  putR(A, o, yaw, [c[0], c[1] + r, c[2]], [len * 0.62, r * 2, r * 2], "ammo_brass", [0, ry, 0]);   // case
  const rr = (ry * Math.PI) / 180, dx = Math.cos(rr) * len * 0.5, dz = -Math.sin(rr) * len * 0.5;
  putR(A, o, yaw, [c[0] + dx, c[1] + r, c[2] + dz], [len * 0.38, r * 1.7, r * 1.7], "ammo_shell_steel", [0, ry, 0]);   // projectile
}

// ---- groups (one furniture group each)
// small-arms cache: origin o = [x, y_floor, z], footprint about 1.0 x 0.5 m; us = US-held position (.30 cal cans)
export function smallArmsCache(A, name, x, y, z, yaw = 0, { us = false, boxes = 3, crate = true, bandoliers = 2 } = {}) {
  const o = [x, y, z];
  A.beginFurniture(name);
  for (let k = 0; k < boxes; k++) mgAmmoBox(A, o, yaw, [-0.32 + (k % 2) * 0.3, Math.floor(k / 2) * (us ? 0.18 : 0.19), -0.08 + (k % 2) * 0.01], us);
  if (crate) rifleCrate(A, o, yaw, [0.28, 0, 0.06], us);
  for (let k = 0; k < bandoliers; k++) bandolier(A, o, yaw, [0.28, 0.19 + 0.03 * k, 0.04 - 0.05 * k], 12 * k - 6);
  A.endFurniture(true);
}
// gun ammunition next to a gun: origin o = [x, y_floor, z]; cal "75" (PaK 40 / 5 cm too), "88" (Flak 18), "50" (5 cm KwK)
export function gunAmmoStack(A, name, x, y, z, yaw = 0, { cal = "75", crates = 4, carriers = 2, loose = 2 } = {}) {
  const o = [x, y, z];
  const H = cal === "88" ? 0.24 : cal === "50" ? 0.17 : 0.2, W = cal === "88" ? 0.44 : cal === "50" ? 0.3 : 0.34;
  A.beginFurniture(name);
  for (let k = 0; k < crates; k++) shellCrate(A, o, yaw, [0, Math.floor(k / 2) * H, (k % 2) * W - W / 2], cal);
  for (let k = 0; k < carriers; k++) wickerCarrier(A, o, yaw, [0.05, 0, W + 0.12 + k * 0.17], cal);
  for (let k = 0; k < loose; k++) looseRound(A, o, yaw, [0.75 + 0.1 * k, 0, -0.1 + 0.22 * k], cal, 20 - 35 * k);
  A.endFurniture(true);
}
