// MP1 (Hollow Village, tools/mapgen/maps/hollow_village.py): the two structures the kit lacked, generated from box parts:
// a timber watchtower on log legs with a stair to its walled platform, and a plank footbridge for the gully.
// Registered in tools/build_mpu_assets.mjs; `node tools/catalog_mp1.mjs` writes just these two files the same way.
// Units metres, +y up, front = +z.
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { Asset } from "./kit.mjs";
import { setNav, cover, room, fire } from "./catalog_atlantic.mjs";
import { dezfight } from "./audit_zfight.mjs";

const DEG = 180 / Math.PI;
const D = { k: 0, lod: 1 };   // detail: no collider
const H = { v: 0 };           // collider only
const K0 = { k: 0 };

function mats(A) {
  A.mat("log", [0.36, 0.3, 0.24], 0.9, "wood", { texture: "bark_007", grain: 1 });
  A.mat("beam", [0.4, 0.34, 0.27], 0.88, "wood", { texture: "wood_old_planks", grain: 1 });
  A.mat("plank", [0.44, 0.39, 0.33], 0.9, "wood", { texture: "boards_weathered", grain: 1 });
  A.mat("deck", [0.38, 0.33, 0.27], 0.92, "wood", { texture: "planks_038", grain: 1 });
  A.mat("shingle", [0.3, 0.28, 0.26], 0.9, "wood", { texture: "boards_weathered", grain: 1 });
  A.mat("rope", [0.5, 0.43, 0.32], 0.95, "fabric");
  A.mat("iron", [0.2, 0.17, 0.15], 0.6, "metal", { metallic: 0.5 });
  A.mat("stone", [0.45, 0.44, 0.41], 0.95, "stone", { texture: "stone_block_mossy" });
}
const mk = (id, name, category, description, footprint, o) => {
  const A = new Asset({ id, version: 1, name, category, description, footprint, floors: 0, enterable: false, plinthDepth: 0, maxGrade: 0.6, noAutoPanels: true, ...o });
  mats(A);
  return A;
};
const nav0 = (effect) => ({ effect, rooms: [], doors: [], entrances: [], corners: [], firingPoints: [], cover: [], areas: [] });
// a beam between two points (square section w x t), long axis along the segment
function bar(A, a, b, w, t, m, o = {}) {
  const d = [b[0] - a[0], b[1] - a[1], b[2] - a[2]], L = Math.hypot(...d);
  const yaw = Math.atan2(d[0], d[2]) * DEG, pitch = -Math.asin(d[1] / L) * DEG;
  A.obox([(a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2], [w, t, L], [pitch, yaw, 0], m, o);
}

// ------------------------------------------------------------------ timber watchtower
export function watchtower() {
  const PY = 4.4, L = 1.45, ZT = 1.9, RY = PY + 2.3;   // platform height, leg half spacing, platform centre z, roof eaves
  const A = mk("mp1_watchtower", "Timber watchtower", "Small buildings",
    "A village lookout of rough timber: four mossy log legs with cross bracing carry a 3 m square plank platform 4.4 m up, walled to chest height with weathered boards, under a steep shingled pyramid roof on corner posts. A steep plank stair with a rope rail climbs from the rear to a gap in the parapet. The platform is walkable.",
    [3.6, 8.0], { enterable: true, floors: 1 });
  const at = (x, y, z) => [x, y, z + ZT];
  for (const [sx, sz] of [[-1, -1], [1, -1], [1, 1], [-1, 1]]) {
    A.cyl(at(sx * L, RY / 2, sz * L), [0.24, RY, 0.24], "log", { seg: 8 });
    A.box(at(sx * L - 0.3, -0.25, sz * L - 0.3), at(sx * L + 0.3, 0.12, sz * L + 0.3), "stone", { bv: 0.04 });   // footing stone
  }
  for (const y of [1.5, PY - 0.12]) for (const s of [-1, 1]) {   // girts
    A.box(at(-L, y - 0.08, s * L - 0.07), at(L, y + 0.08, s * L + 0.07), "beam", { bv: 0.01 });
    A.box(at(s * L - 0.07, y - 0.08, -L), at(s * L + 0.07, y + 0.08, L), "beam", { bv: 0.01 });
  }
  for (const [y0, y1] of [[0.15, 1.5], [1.5, PY - 0.15]]) for (const s of [-1, 1]) {   // X bracing on the four faces
    bar(A, at(-L, y0, s * L), at(L, y1, s * L), 0.1, 0.07, "beam", K0);
    bar(A, at(L, y0, s * L), at(-L, y1, s * L), 0.1, 0.07, "beam", K0);
    bar(A, at(s * L, y0, -L), at(s * L, y1, L), 0.07, 0.1, "beam", K0);
    if (s > 0) bar(A, at(s * L, y0, L), at(s * L, y1, -L), 0.07, 0.1, "beam", K0);
  }
  // deck: planks across joists
  A.box(at(-L - 0.12, PY - 0.08, -L - 0.12), at(L + 0.12, PY, L + 0.12), "deck", { bv: 0.01 });
  for (let x = -L; x <= L + 0.01; x += 0.36) A.box(at(x - 0.05, PY - 0.26, -L), at(x + 0.05, PY - 0.08, L), "beam", D);
  for (let z = -L - 0.1; z < L + 0.1; z += 0.24) A.box(at(-L - 0.12, PY + 0.001, z), at(L + 0.12, PY + 0.004, z + 0.012), "beam", D);   // plank seams
  // parapet: boards to 1.1 m, a gap at the rear left where the stair arrives (x from g0 to g1)
  const g0 = -1.15, g1 = -0.25, top = PY + 1.1;
  const pw = (min, max) => A.box(at(...min), at(...max), "plank", { bv: 0.008 });
  pw([-L - 0.12, PY, L + 0.04], [L + 0.12, top, L + 0.12]);
  for (const s of [-1, 1]) pw([s * L - 0.04 + (s < 0 ? -0.08 : 0), PY, -L - 0.12], [s * L + 0.04 + (s > 0 ? 0.08 : 0), top, L + 0.12]);
  pw([-L - 0.12, PY, -L - 0.12], [g0, top, -L - 0.04]); pw([g1, PY, -L - 0.12], [L + 0.12, top, -L - 0.04]);
  for (const s of [-1, 1]) A.box(at(-L - 0.16, top, s * (L + 0.08) - 0.07), at(L + 0.16, top + 0.07, s * (L + 0.08) + 0.07), "beam", D);   // cap rails
  for (let x = -L; x < L; x += 0.27) A.box(at(x, PY + 0.05, L + 0.12), at(x + 0.01, top - 0.05, L + 0.13), "beam", D);   // board joints
  // roof: posts, a pyramid of four shingled slopes, a finial
  for (const [sx, sz] of [[-1, -1], [1, -1], [1, 1], [-1, 1]]) A.box(at(sx * L - 0.07, top, sz * L - 0.07), at(sx * L + 0.07, RY, sz * L + 0.07), "beam", { bv: 0.01 });
  const ov = 0.45, RH = 1.6, half = L + ov, slope = Math.atan2(RH, half) * DEG, sl = Math.hypot(RH, half);
  for (let k = 0; k < 4; k++) {
    const yaw = k * 90, dx = Math.sin(yaw / DEG), dz = Math.cos(yaw / DEG);
    A.obox(at(dx * half / 2, RY + RH / 2, dz * half / 2), [2 * half, 0.08, sl], [slope, yaw, 0], "shingle", K0);
    for (let t = 0.15; t < 1; t += 0.17) A.obox(at(dx * half * (1 - t), RY + RH * t + 0.05, dz * half * (1 - t)), [2 * half * (1 - t) + 0.05, 0.03, 0.06], [slope, yaw, 0], "beam", D);   // shingle courses
  }
  A.cyl(at(0, RY + RH + 0.2, 0), [0.1, 0.45, 0.1], "beam", { k: 0, seg: 6 });
  A.box(at(-half, RY - 0.05, -half), at(half, RY + 0.05, half), "shingle", H);   // roof collider
  // stair: 22 risers of 0.2 m from the rear up to the parapet gap
  const n = 22, r = PY / n, g = 0.24, zs = ZT - L - 0.12, zg = zs - n * g;
  for (let i = 0; i < n; i++) {
    const ty = (i + 1) * r, za = zg + i * g;
    A.box([g0 + 0.04, ty - 0.05, za], [g1 - 0.04, ty, za + g + 0.03], "plank", { k: 0, bv: 0.005 });
  }
  const ang = Math.atan2(PY, zs - zg) * DEG, Ls = Math.hypot(PY, zs - zg);
  A.obox([(g0 + g1) / 2, PY / 2 - 0.15, (zs + zg) / 2], [g1 - g0, 0.3, Ls + 0.1], [-ang, 0, 0], "plank", H);   // walking ramp
  for (const x of [g0, g1]) bar(A, [x, -0.05, zg - 0.05], [x, PY, zs], 0.07, 0.24, "beam", D);   // stringers
  for (let i = 0; i <= 3; i++) { const t = i / 3; A.box([g1 + 0.02, t * PY, zg + t * (zs - zg) - 0.04], [g1 + 0.09, t * PY + 1.0, zg + t * (zs - zg) + 0.04], "beam", D); }
  bar(A, [g1 + 0.06, 1.0, zg], [g1 + 0.06, PY + 1.0, zs], 0.035, 0.035, "rope", D);   // rope rail
  A.cyl(at(-L + 0.35, PY + 0.25, L - 0.35), [0.32, 0.5, 0.32], "beam", { k: 0, seg: 8 });   // a bucket of the watch
  return (setNav(A, {
    ...nav0("carve"),
    rooms: [room("platform", at(-L + 0.05, PY, -L + 0.05), at(L - 0.05, RY - 0.1, L - 0.05))],
    entrances: [{ p: [(g0 + g1) / 2, 0, zg - 0.6], dir: [0, 0, 1], room: "platform" }],
    firingPoints: [fire(at(0, PY, L - 0.5), 0, "firestep"), fire(at(L - 0.5, PY, 0), 90, "firestep"), fire(at(-L + 0.5, PY, 0.3), 270, "firestep"), fire(at(0.6, PY, -L + 0.5), 180, "firestep")],
    cover: [cover(at(0, PY, L - 0.5), 0, "low", ["over"]), cover(at(L - 0.5, PY, 0), 90, "low", ["over"]), cover(at(-L + 0.5, PY, 0.3), -90, "low", ["over"])],
  }), A);
}

// ------------------------------------------------------------------ plank footbridge
export function footbridge() {
  const HZ = 7.5, ZG = 6.0, YD = 0.35, HX = 0.9;
  const A = mk("mp1_footbridge", "Plank footbridge", "Small buildings",
    "A narrow rural footbridge, 2 x 15 m, spanning along z: two mossy log stringers on stone abutments carry a deck of weathered planks 0.35 m above the banks with short plank ramps at the ends; split-pole posts and a sagging rope rail on both sides. Walkable deck.",
    [2.0, 15.0], { maxGrade: 0.8 });
  for (const s of [-1, 1]) {
    A.box([-HX - 0.1, -1.6, s > 0 ? ZG - 0.4 : -ZG - 0.8], [HX + 0.1, 0.0, s > 0 ? ZG + 0.8 : -ZG + 0.4], "stone", { bv: 0.05 });   // abutment
    const th = Math.atan2(YD, HZ - ZG) * DEG, Lr = Math.hypot(YD, HZ - ZG);
    A.obox([0, YD / 2 - 0.04, s * (ZG + (HZ - ZG) / 2)], [2 * HX, 0.08, Lr], [s * th, 0, 0], "deck");   // ramp
  }
  for (const x of [-0.6, 0.6]) A.cyl([x, YD - 0.2, 0], [0.32, 2 * ZG + 0.6, 0.32], "log", { seg: 8, r: [90, 0, 0] });
  A.box([-HX, YD - 0.06, -ZG], [HX, YD, ZG], "deck");
  for (let z = -ZG; z < ZG; z += 0.3) A.box([-HX + 0.02 * ((z * 7) % 3), YD + 0.001, z], [HX - 0.02 * ((z * 5) % 2), YD + 0.004, z + 0.012], "beam", D);
  for (const sx of [-1, 1]) {
    const x = sx * (HX - 0.05);
    for (let z = -ZG; z <= ZG + 0.01; z += 2.4) A.cyl([x, YD + 0.5, z], [0.1, 1.05, 0.1], "beam", { k: 0, seg: 6 });
    for (let z = -ZG; z < ZG - 0.1; z += 2.4) {   // rope sagging between posts
      bar(A, [x, YD + 1.0, z], [x, YD + 0.88, z + 1.2], 0.03, 0.03, "rope", D);
      bar(A, [x, YD + 0.88, z + 1.2], [x, YD + 1.0, z + 2.4], 0.03, 0.03, "rope", D);
    }
    A.box([x - 0.05, YD, -ZG], [x + 0.05, YD + 1.0, ZG], "beam", H);   // rail collider
  }
  setNav(A, { ...nav0("block"), cover: [cover([0, 0, HZ + 0.6], 180, "low", ["over"]), cover([0, 0, -HZ - 0.6], 0, "low", ["over"])] });
  A.meta.notes = "Deck at y = 0.35 over a gully up to 11 m wide (|z| < 5.5); the ends ramp to the banks at y = 0.";
  return A;
}

export function mp1Assets() { return [watchtower(), footbridge()]; }

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const out = path.join(path.dirname(fileURLToPath(import.meta.url)), "..", "content", "assets");
  for (const A of mp1Assets()) {
    const z = JSON.parse(JSON.stringify(A.json()));
    dezfight(z);
    fs.writeFileSync(path.join(out, z.id + ".json"), JSON.stringify(z));
    console.log(z.id, z.parts.length, "parts");
  }
}
