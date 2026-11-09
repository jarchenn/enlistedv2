// GX6 (graphics overhaul): seven new Normandy 1944 houses, four large houses and the `mil_stronghold` bunker complex.
// Exterior design references (CC-BY-4.0, docs/credits.md; the meshes are not shipped): EddyNL's European buildings
// asset packs 1 and 3 (townhouse proportions, window rhythm, cornices, stepped gables, the red-brick villa),
// Beardmonkey's Little Rock Inn (the roadside inn with its veranda and rubble chimney), artfletch's Shoeburyness
// small building and Tilbury Fort spigot-mortar emplacement scans (concrete weathering, slab lips, earth banks,
// railings, steps in the bank). Regenerate with `node tools/build_mpu_assets.mjs` (never build_assets.mjs).
//
// The houses use the B3 house builder (tools/catalog_chateau.mjs `house`: two-layer walls with real openings,
// destructible bays, casements and shutters, two stair cores, nav, tour) and dress the facades with the B1 kit plus
// the helpers below. Detail that can drop at distance carries "lod": 1 (PF2's building LOD).
import { Asset, cutRect, wbox } from "./kit.mjs";
import {
  stdMaterials, wall, partition, door, stairs, railing, quoins, pilaster, stringCourse, cornice, surround, archOpening,
  balcony, dormer, hipRoof, chimney, chimneyCap, fbox, fpart, weather, timberFrame, turret, hinged, aabb,
} from "./parts.mjs";
import { mats, face, fireplace, crossGable, house, masonry, comp, meta, turretWindow } from "./catalog_chateau.mjs";
import { cover, fire, room, corners, setNav, setTour } from "./catalog_atlantic.mjs";
import { SLIT, bunkerMaterials, slit, camoNet, earthMaterials, earthBank, sandbagWall, mossPatch, roofEarth, pier } from "./catalog_bunkers.mjs";
import { rod, rebar, ventPipe, periscope, doorKnuckles, dmat } from "./defence_detail.mjs";

const GF = 0.3, SIDES = ["front", "rear", "east", "west"];
const L1 = { k: 0, lod: 1 };   // visual detail, droppable at distance
const q4 = (v) => Math.round(v * 1e4) / 1e4;
function rnd(seed) { let h = 2166136261; for (let i = 0; i < seed.length; i++) { h ^= seed.charCodeAt(i); h = Math.imul(h, 16777619); } return () => { h ^= h << 13; h ^= h >>> 17; h ^= h << 5; return ((h >>> 0) % 100000) / 100000; }; }

function gxMats(A) {
  mats(A);
  A.mat("tile_orange", [0.44, 0.15, 0.06], 0.66, "slate", { grain: 2 });      // pantiles (pack 1 townhouse)
  A.mat("brick_buff", [0.40, 0.31, 0.20], 0.86, "brickwork");
  A.mat("brick_dark", [0.15, 0.07, 0.045], 0.86, "brickwork");
  A.mat("render_ochre", [0.60, 0.45, 0.26], 0.92, "limewash");
  A.mat("render_white", [0.70, 0.68, 0.62], 0.9, "limewash");
  A.mat("canvas_red", [0.34, 0.055, 0.04], 0.95, "fabric");
  A.mat("canvas_cream", [0.62, 0.58, 0.46], 0.95, "fabric");
  A.mat("sign_paint", [0.05, 0.11, 0.08], 0.55, "painted", { grain: 0 });
  A.mat("sign_red", [0.30, 0.05, 0.03], 0.6, "painted", { grain: 0 });
  A.mat("gilt", [0.62, 0.45, 0.16], 0.35, "metal", { metallic: 0.8 });
  A.mat("geranium", [0.52, 0.035, 0.03], 0.8, "plain");
  A.mat("leaf", [0.05, 0.11, 0.03], 0.85, "plain");
}
const shutterColour = (A, rgb) => Object.assign(A.materials.find((m) => m.name === "shutter_paint"), { albedo: rgb });

// ============================================================================ facade detail helpers
// windows of storey f in house config c, with absolute sills (the same reading as house() does)
function wins(c, f) {
  const W = c.win(f), wd = { w: 1.0, h: 1.4, sill: 0.9, ...(c.wd?.(f) ?? {}) }, out = [];
  for (const side of SIDES) (W[side] ?? []).forEach((p) => {
    const o = typeof p === "number" ? { c: p } : p, sill = GF + f * c.h + (o.sill ?? wd.sill), h = o.h ?? wd.h;
    out.push({ side, c: o.c, w: o.w ?? wd.w, h, sill, top: sill + h, f });
  });
  return out;
}
// zinc rainwater downpipe on a wall face at a (hopper head under the eaves, brackets, a shoe at the foot)
function downpipe(A, F, a, y0, y1) {
  fpart(A, "cyl", F, a, (y0 + y1) / 2, 0.11, [0.085, y1 - y0, 0.085], "zinc", { k: 0 });
  fbox(A, F, a - 0.13, a + 0.13, y1 - 0.08, y1 + 0.2, 0.04, 0.22, "zinc", { k: 0, bv: 0.01 });
  for (let y = y0 + 0.6; y < y1 - 0.4; y += 1.7) fbox(A, F, a - 0.06, a + 0.06, y - 0.02, y + 0.02, -0.01, 0.16, "iron", L1);
  fpart(A, "wedge", F, a, y0 + 0.05, 0.2, [0.1, 0.1, 0.2], "zinc", L1);
}
// planter under a window with geraniums
function windowBox(A, F, a, w, sill) {
  fbox(A, F, a - w / 2, a + w / 2, sill - 0.32, sill - 0.12, 0.08, 0.3, "shutter_paint", { k: 0, bv: 0.01 });
  for (const s of [-1, 1]) fpart(A, "wedge", F, a + s * (w / 2 - 0.1), sill - 0.4, 0.13, [0.05, 0.12, 0.26], "iron", L1);
  const R = rnd(`${A.meta.id}/wb/${a}/${sill}`);
  for (let x = a - w / 2 + 0.1; x < a + w / 2 - 0.05; x += 0.13) {
    fpart(A, "cyl", F, x, sill - 0.07, 0.19 + (R() - 0.5) * 0.06, [0.15, 0.12, 0.12], "leaf", L1);
    if (R() < 0.75) fpart(A, "cyl", F, x + (R() - 0.5) * 0.05, sill + 0.0 + R() * 0.05, 0.19 + (R() - 0.5) * 0.08, [0.07, 0.06, 0.07], "geranium", L1);
  }
}
// triangular pediment in the facade plane (proud e0..e1)
function pediment(A, F, a0, a1, y, rise, e0, e1, m, o = {}) {
  const d0 = F.d + F.out * e0, d1 = F.d + F.out * e1, ac = (a0 + a1) / 2;
  A.prism(F.axis === "x" ? 2 : 0, Math.min(d0, d1), Math.max(d0, d1), [[a0, y], [a1, y], [ac, y + rise]], m, o);
}
// stone door case: pilasters, entablature, a pediment (tri / seg / none) and a carved tympanum panel
function doorCase(A, F, a, w, top, { ped = "tri", m = "stone_dressed" } = {}) {
  for (const s of [-1, 1]) pilaster(A, F, a + s * (w / 2 + 0.2), GF - 0.02, top + 0.05, { w: 0.26, proj: 0.08, m });
  fbox(A, F, a - w / 2 - 0.4, a + w / 2 + 0.4, top + 0.05, top + 0.3, -0.01, 0.12, m, { bv: 0.012 });
  fbox(A, F, a - w / 2 - 0.46, a + w / 2 + 0.46, top + 0.3, top + 0.38, -0.01, 0.17, m, { bv: 0.012 });
  if (ped === "tri") pediment(A, F, a - w / 2 - 0.42, a + w / 2 + 0.42, top + 0.38, 0.5, -0.01, 0.15, m);
  else if (ped === "seg") fpart(A, "arch", F, a, top + 0.38 + 0.18, 0.07, [w + 0.84, 0.36, 0.15], m, { th: 99 });
  fbox(A, F, a - 0.18, a + 0.18, top + 0.1, top + 0.26, 0.11, 0.15, m, { ...L1, bv: 0.01 });   // carved boss
}
// striped canvas awning over a0..a1 hung at y, `depth` out, falling `drop`; iron arms and a scalloped valance
function awning(A, F, a0, a1, y, depth = 1.1, drop = 0.55, stripes = ["canvas_red", "canvas_cream"]) {
  const L = Math.hypot(depth, drop), ang = (Math.atan2(drop, depth) * 180) / Math.PI, n = Math.max(2, Math.round((a1 - a0) / 0.24));
  const sw = (a1 - a0) / n, yc = y - drop / 2, ec = depth / 2;
  for (let i = 0; i < n; i++) {
    const a = a0 + sw * (i + 0.5), m = stripes[i % stripes.length];
    if (F.axis === "x") A.obox([a, yc, F.d + F.out * ec], [sw + 0.004, 0.025, L], [F.out * ang, 0, 0], m, { k: 0 });
    else A.obox([F.d + F.out * ec, yc, a], [L, 0.025, sw + 0.004], [0, 0, -F.out * ang], m, { k: 0 });
    fbox(A, F, a - sw / 2, a + sw / 2, y - drop - 0.22 + (i % 2) * 0.03, y - drop + 0.01, depth - 0.012, depth + 0.012, m, L1);   // valance
  }
  fbox(A, F, a0 - 0.05, a1 + 0.05, y - 0.06, y + 0.06, -0.01, 0.15, "iron", { k: 0, bv: 0.01 });     // roller box
  for (const a of [a0 + 0.05, a1 - 0.05]) {
    const p0 = F.axis === "x" ? [a, y - drop - 0.6, F.d + F.out * 0.02] : [F.d + F.out * 0.02, y - drop - 0.6, a];
    const p1 = F.axis === "x" ? [a, y - drop, F.d + F.out * depth] : [F.d + F.out * depth, y - drop, a];
    rod(A, p0, p1, 0.025, "iron", L1);
  }
}
// Painted sign lettering in a 14-segment stroke font (capitals), centred on a0..a1 at baseline y, cap height h.
// Segments on a unit glyph: T / B / M top, bottom and middle bars (ML / MR halves), L1 L2 R1 R2 the upper / lower
// left and right stems, C1 C2 the centre stem halves, D1 D2 the upper diagonals into the centre, D4 the lower-right
// diagonal out of it (R, N), VL VR the lower diagonals meeting at the foot (V)
const GLYPH = {
  A: "T L1 L2 R1 R2 M", B: "TS BS MS L1 L2 R1 R2", C: "T B L1 L2", D: "TS BS L1 L2 R1 R2", E: "T B ML L1 L2", F: "T ML L1 L2", G: "T B L1 L2 R2 MR",
  H: "L1 L2 R1 R2 M", I: "T B C1 C2", L: "L1 L2 B", M: "L1 L2 R1 R2 D1 D2", N: "L1 L2 R1 R2 D1 D4", O: "T B L1 L2 R1 R2", P: "T L1 L2 R1 M",
  R: "T L1 L2 R1 M D4", S: "T M B L1 R2", T: "T C1 C2", U: "L1 L2 R1 R2 B", V: "L1 R1 VL VR",
};
const SEG = {   // [x0, y0, x1, y1] on the unit glyph
  T: [0, 1, 1, 1], B: [0, 0, 1, 0], M: [0, 0.5, 1, 0.5], ML: [0, 0.5, 0.55, 0.5], MR: [0.45, 0.5, 1, 0.5], L1: [0, 0.5, 0, 1], L2: [0, 0, 0, 0.5],
  R1: [1, 0.5, 1, 1], R2: [1, 0, 1, 0.5], C1: [0.5, 0.5, 0.5, 1], C2: [0.5, 0, 0.5, 0.5], D1: [0, 1, 0.5, 0.5], D2: [1, 1, 0.5, 0.5], D4: [0.5, 0.5, 1, 0],
  VL: [0, 0.5, 0.5, 0], VR: [1, 0.5, 0.5, 0], TS: [0, 1, 0.78, 1], BS: [0, 0, 0.78, 0], MS: [0, 0.5, 0.78, 0.5],
};
function sign(A, F, a0, a1, y, h, text, m = "gilt") {
  const w = h * 0.6, t = h * 0.15, gap = h * 0.25, sp = h * 0.45, len = [...text].reduce((s, ch) => s + (ch === " " ? sp : w + gap), -gap);
  const dir = F.axis === "x" ? F.out : -F.out;   // the viewer's left-to-right along a (mirrored on rear / east faces)
  let a = (a0 + a1) / 2 - (dir * len) / 2;
  for (const ch of text) {
    if (ch === " ") { a += dir * sp; continue; }
    for (const s of (GLYPH[ch] ?? "").split(" ").filter(Boolean)) {
      const [x0, y0, x1, y1] = SEG[s], px = (x) => a + dir * (x * (w - t) + t / 2);
      const p0 = [px(x0), y + y0 * (h - t) + t / 2], p1 = [px(x1), y + y1 * (h - t) + t / 2];
      if (p0[0] === p1[0] || p0[1] === p1[1]) fbox(A, F, Math.min(p0[0], p1[0]) - t / 2, Math.max(p0[0], p1[0]) + t / 2, Math.min(p0[1], p1[1]) - t / 2, Math.max(p0[1], p1[1]) + t / 2, 0, 0.012, m, L1);
      else {
        const L = Math.hypot(p1[0] - p0[0], p1[1] - p0[1]), ang = (Math.atan2(p1[1] - p0[1], p1[0] - p0[0]) * 180) / Math.PI;
        const c = [(p0[0] + p1[0]) / 2, (p0[1] + p1[1]) / 2], d = F.d + F.out * 0.006;
        if (F.axis === "x") A.obox([c[0], c[1], d], [L, t, 0.012], [0, 0, ang], m, L1);
        else A.obox([d, c[1], c[0]], [0.012, t, L], [-ang, 0, 0], m, L1);
      }
    }
    a += dir * (w + gap);
  }
}
// painted timber shopfront: end pilasters, fascia with a moulded cornice and lettering, stall riser under the windows
function shopFront(A, F, a0, a1, y1, { word = "", stall = [], m = "door_paint" } = {}) {
  for (const a of [a0, a1]) {
    fbox(A, F, a - 0.16, a + 0.16, GF - 0.02, y1 - 0.5, -0.01, 0.1, m, { k: 0, bv: 0.01 });
    fbox(A, F, a - 0.2, a + 0.2, y1 - 0.62, y1 - 0.5, -0.01, 0.14, m, { k: 0, bv: 0.01 });   // console
  }
  fbox(A, F, a0 - 0.2, a1 + 0.2, y1 - 0.5, y1, -0.01, 0.1, m, { k: 0, bv: 0.012 });
  fbox(A, F, a0 - 0.26, a1 + 0.26, y1, y1 + 0.1, -0.01, 0.18, m, { k: 0, bv: 0.012 });
  fpart(A, "wedge", F, (a0 + a1) / 2, y1 + 0.14, 0.085, [a1 - a0 + 0.5, 0.08, 0.17], "zinc", { k: 0 });
  fbox(A, F, a0 + 0.1, a1 - 0.1, y1 - 0.42, y1 - 0.08, 0.1, 0.11, "sign_paint", { k: 0 });
  sign(A, { ...F, d: F.d + F.out * 0.11 }, a0 + 0.35, a1 - 0.35, y1 - 0.38, 0.26, word);
  for (const [s0, s1, top] of stall) {   // panelled stall riser under a shop window
    fbox(A, F, s0, s1, GF + 0.02, top, -0.01, 0.05, m, { k: 0 });
    fbox(A, F, s0 + 0.08, s1 - 0.08, GF + 0.1, top - 0.08, 0.04, 0.07, m, L1);
  }
}
// small brackets (modillions) under a cornice line
function modillions(A, F, a0, a1, y, sp = 0.5, m = "stone_dressed") {
  for (let a = a0 + sp / 2; a < a1 - 0.1; a += sp) fpart(A, "wedge", F, a, y - 0.09, 0.12, [0.12, 0.18, 0.24], m, { ...L1, top: 0.3 });
}
// segmental relieving arch over an opening (springing at y)
function segArch(A, F, a0, a1, y, { rise = 0.24, w = 0.15, m = "stone_dressed" } = {}) {
  fpart(A, "arch", F, (a0 + a1) / 2, y + (rise + w) / 2, 0.025, [a1 - a0 + 2 * w, rise + w, 0.05], m, { th: w, k: 0 });
  fbox(A, F, (a0 + a1) / 2 - 0.08, (a0 + a1) / 2 + 0.08, y + rise - 0.02, y + rise + w + 0.04, -0.01, 0.065, m, { k: 0, bv: 0.008 });
}
// rusticated ashlar bands (horizontal channelled courses) on a wall face, cut around holes
function rustication(A, F, a0, a1, y0, y1, holes, { course = 0.38, proj = 0.035, m = "stone_dressed" } = {}) {
  for (let y = y0; y < y1 - 0.05; y += course) {
    const yb = Math.min(y1, y + course - 0.035);
    for (const [s0, s1, ya, yb2] of cutRect(a0, a1, y, yb, holes)) if (s1 - s0 > 0.15 && yb2 - ya > 0.1) fbox(A, F, s0, s1, ya, yb2, -0.01, proj, m, { k: 0, bv: 0.012 });
  }
}
// crow-stepped gable on the end wall at x = sx * X (gable from EAVE at |z| = Z to RIDGE at z = 0): stone steps, copings
function steppedGable(A, sx, X, Z, EAVE, RIDGE, { n = 6, m = "stone_dressed", t = 0.45 } = {}) {
  const slope = (RIDGE - EAVE) / Z, x0 = sx > 0 ? X - t : -X - 0.04, x1 = sx > 0 ? X + 0.04 : -X + t, dz = Z / n;
  for (const s of [-1, 1]) for (let i = 0; i < n; i++) {
    const za = s * (Z - i * dz), zb = s * (Z - (i + 1) * dz), ytop = EAVE + slope * (i + 1) * dz + 0.42;
    A.box([x0, EAVE - 0.1, Math.min(za, zb)], [x1, ytop, Math.max(za, zb)], "brick", {});
    A.box([x0 - 0.04, ytop, Math.min(za, zb) - 0.03], [x1 + 0.04, ytop + 0.1, Math.max(za, zb) + 0.03], m, { k: 0, bv: 0.012 });
  }
  A.box([x0, EAVE, -dz / 2], [x1, RIDGE + 0.9, dz / 2], "brick", {});
  A.box([x0 - 0.05, RIDGE + 0.9, -dz / 2 - 0.04], [x1 + 0.05, RIDGE + 1.02, dz / 2 + 0.04], m, { k: 0, bv: 0.012 });
  A.cone([(x0 + x1) / 2, RIDGE + 1.32, 0], [0.24, 0.6, 0.24], m, { ...L1, n: 8 });   // finial
}
// close-studded timbering on a gable triangle at x = sx * X: posts every sp clipped to the rake, a collar and braces
function gableTimber(A, sx, X, Z, EAVE, RIDGE, { sp = 0.45, t = 0.15 } = {}) {
  const F = { axis: "z", d: sx * X, out: sx }, slope = (RIDGE - EAVE) / Z;
  fbox(A, F, -Z, Z, EAVE, EAVE + t, -0.03, 0.045, "oak_x", { k: 0 });
  for (let z = -Z + sp; z < Z - sp * 0.5; z += sp) {
    const top = EAVE + slope * (Z - Math.abs(z)) - 0.22;
    if (top > EAVE + t + 0.2) fbox(A, F, z - t / 2, z + t / 2, EAVE + t, top, -0.03, 0.04, "oak_y", { k: 0 });
  }
  const yc = EAVE + (RIDGE - EAVE) * 0.55, hw = (Z * (1 - 0.55)) - 0.2;
  fbox(A, F, -hw, hw, yc - t / 2, yc + t / 2, -0.03, 0.05, "oak_x", { k: 0 });
}
// tiled door hood (auvent) on two curved oak brackets
function doorHood(A, F, a, w, top, { m = "tile" } = {}) {
  const d = 0.85, ang = 24, L = d / Math.cos((ang * Math.PI) / 180);
  if (F.axis === "x") A.obox([a, top + 0.45 - (d / 2) * Math.tan((ang * Math.PI) / 180), F.d + F.out * d / 2], [w + 0.7, 0.07, L], [F.out * ang, 0, 0], m, { k: 0 });
  else A.obox([F.d + F.out * d / 2, top + 0.45 - (d / 2) * Math.tan((ang * Math.PI) / 180), a], [L, 0.07, w + 0.7], [0, 0, -F.out * ang], m, { k: 0 });
  for (const s of [-1, 1]) {
    fpart(A, "wedge", F, a + s * (w / 2 + 0.25), top + 0.05, 0.32, [0.1, 0.5, 0.64], "oak_x", { k: 0 });
    fpart(A, "arch", F, a + s * (w / 2 + 0.25), top - 0.2, 0.3, [0.1, 0.3, 0.55], "oak_x", L1);
  }
}
// glass-and-iron canopy (marquise) over a door on iron brackets
function marquise(A, F, a, w, top, depth = 1.0) {
  const ang = 12;
  if (F.axis === "x") A.obox([a, top + 0.4, F.d + F.out * depth / 2], [w + 0.9, 0.025, depth], [F.out * ang, 0, 0], "glass", { k: 0 });
  else A.obox([F.d + F.out * depth / 2, top + 0.4, a], [depth, 0.025, w + 0.9], [0, 0, -F.out * ang], "glass", { k: 0 });
  fbox(A, F, a - w / 2 - 0.45, a + w / 2 + 0.45, top + 0.22, top + 0.28, depth - 0.06, depth, "iron", { k: 0 });
  for (const s of [-1, 1]) {
    fpart(A, "wedge", F, a + s * (w / 2 + 0.4), top + 0.05, depth / 2, [0.04, 0.5, depth], "iron", { k: 0 });
    for (let e = 0.2; e < depth - 0.1; e += 0.2) fpart(A, "cyl", F, a + s * (w / 2 + 0.4), top + 0.3 - e * 0.2, e, [0.02, 0.06, 0.02], "iron", L1);
  }
  for (let x = a - w / 2 - 0.3; x < a + w / 2 + 0.35; x += 0.3) fpart(A, "cyl", F, x, top + 0.2, depth - 0.02, [0.03, 0.12, 0.03], "iron", L1);   // fringe
}
// wall lantern on a scroll bracket
function wallLantern(A, F, a, y) {
  fbox(A, F, a - 0.03, a + 0.03, y + 0.1, y + 0.14, -0.01, 0.42, "iron", L1);
  fpart(A, "arch", F, a, y - 0.05, 0.24, [0.03, 0.18, 0.3], "iron", L1);
  fpart(A, "cyl", F, a, y - 0.12, 0.4, [0.16, 0.26, 0.16], "glass", { k: 0, n: 4 });
  fpart(A, "cone", F, a, y + 0.08, 0.4, [0.22, 0.12, 0.22], "iron", { k: 0, n: 4 });
}
// timber lean-to over a gable end (woodshed / cart shed): oak posts on stones, a plate, a tiled roof and stacked logs
function leanTo(A, sx, X, z0, z1, yWall, { depth = 2.4, yFront = 2.3, m = "tile", logs = true } = {}) {
  const xo = sx * (X + depth), xi = sx * X;
  for (let z = z0 + 0.15; z <= z1 - 0.1; z += Math.max(1.5, (z1 - z0 - 0.3) / Math.round((z1 - z0 - 0.3) / 2.4))) {
    A.box([xo - 0.2, -0.2, z - 0.2], [xo + 0.2, 0.15, z + 0.2], "stone_dressed", { bv: 0.02 });
    A.box([xo - 0.09, 0.15, z - 0.09], [xo + 0.09, yFront, z + 0.09], "oak_y", { bv: 0.012 });
    fpart(A, "wedge", { axis: "z", d: xo, out: -sx }, z, yFront - 0.25, 0.25, [0.12, 0.5, 0.5], "oak_x", L1);
  }
  A.box([Math.min(xo, xo) - 0.11, yFront, z0], [xo + 0.11, yFront + 0.2, z1], "oak_z", { bv: 0.01 });
  const run = Math.abs(xo - xi) + 0.4, rise = yWall - yFront, ang = (Math.atan2(rise, run) * 180) / Math.PI;
  A.obox([(xo + xi) / 2 + sx * 0.2, (yWall + yFront) / 2 + 0.16, (z0 + z1) / 2], [Math.hypot(run, rise) + 0.1, 0.1, z1 - z0 + 0.4], [0, 0, -sx * ang], m);
  if (!logs) return;
  const R = rnd(A.meta.id + "/logs" + sx);
  for (let r = 0; r < 5; r++) for (let z = z0 + 0.4 + (r % 2) * 0.11; z < z1 - 0.4; z += 0.23)
    A.cyl([xi + sx * (0.55 + R() * 0.05), 0.12 + r * 0.2, z], [0.2, 0.8 + R() * 0.2, 0.2], "bark", { r: [0, 0, 90], k: 0, lod: r > 2 ? 1 : 0, n: 8 });
  A.box([Math.min(xi + sx * 0.12, xi + sx * 1.0), 0, z0 + 0.35], [Math.max(xi + sx * 0.12, xi + sx * 1.0), 1.0, z1 - 0.35], "bark", { v: 0 });
}

// dormers are roof dressing: visual only, so their cheeks never hang into an attic at head height
function dormerV(A, o) { const n = A.parts.length; dormer(A, o); for (const p of A.parts.slice(n)) p.k = 0; }
// glazing on solid masonry (a cross gable over the roof, a solid tower) gets a dark backing, or it reads as a pale panel
function gableWin(A, o) {
  crossGable(A, o);
  const w = o.w, ww = Math.min(0.55, w / 4), y0 = o.y0 + 0.35, y1 = Math.min(o.y0 + 1.6, o.y0 + o.rise * 0.4), s = o.s ?? 1;
  fbox(A, { axis: "x", d: o.zf + s * 0.02, out: s }, o.x - ww, o.x + ww, y0, y1, -0.012, 0.02, "soot", { k: 0 });
}
function blindTurret(A, cx, cz, r, a, y, w, h) {
  const ar = (a * Math.PI) / 180, dx = Math.cos(ar), dz = Math.sin(ar), yaw = (Math.atan2(dx, dz) * 180) / Math.PI;
  A.obox([cx + dx * (r - 0.002), y, cz + dz * (r - 0.002)], [w - 0.02, h - 0.02, 0.02], [0, yaw, 0], "soot", { k: 0 });
}

// ============================================================================ sub-building placement
// Copies asset B into A turned by rot (0, 90, 180 or 270 degrees about +Y; 90 turns B's front +Z to +X) and moved by
// (dx, dz). Ids (interactables, panels, nav rooms, tour targets) get the prefix `pre`. Axis-aligned boxes and round
// parts stay axis-aligned (their sizes swap), so B's wall bays stay damage bays; turned parts get a quaternion.
const QY = (deg) => { const t = (deg * Math.PI) / 360; return [0, Math.sin(t), 0, Math.cos(t)]; };
function qmul(a, b) {
  return [a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
    a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]];
}
function qEuler([rx, ry, rz]) {   // glm::quat(radians(vec3)) = Rz * Ry * Rx (src/assets.cpp)
  const h = (v) => (v * Math.PI) / 360, [cx, cy, cz] = [Math.cos(h(rx)), Math.cos(h(ry)), Math.cos(h(rz))], [sx, sy, sz] = [Math.sin(h(rx)), Math.sin(h(ry)), Math.sin(h(rz))];
  return [sx * cy * cz - cx * sy * sz, cx * sy * cz + sx * cy * sz, cx * cy * sz - sx * sy * cz, cx * cy * cz + sx * sy * sz];
}
function placeSub(A, B, { dx = 0, dz = 0, rot = 0, pre = "" }) {
  const c = Math.round(Math.cos((rot * Math.PI) / 180)), s = Math.round(Math.sin((rot * Math.PI) / 180)), swap = Math.abs(s) === 1;
  const P2 = (x, z) => [c * x + s * z + dx, -s * x + c * z + dz];
  const P3 = (p) => { const [x, z] = P2(p[0], p[2]); return [q4(x), p[1], q4(z)]; };
  const D3 = (v) => [q4(c * v[0] + s * v[2]), v[1], q4(-s * v[0] + c * v[2])];
  const box = (mn, mx) => { const a = P3(mn), b = P3(mx); return { min: [0, 1, 2].map((i) => Math.min(a[i], b[i])), max: [0, 1, 2].map((i) => Math.max(a[i], b[i])) }; };
  const qy = QY(rot);
  const part = (p0) => {
    const p = JSON.parse(JSON.stringify(p0));
    if (p.t === "p") {
      const tri = p.tri, a = p.a;
      if (p.axis === 1) p.tri = tri.map(([x, z]) => P2(x, z).map(q4));
      else {
        // prismPoint: axis 0 -> (a, v, u); axis 2 -> (u, v, a)
        const pts = tri.map(([u, v]) => a.map((aa) => (p.axis === 0 ? [aa, v, u] : [u, v, aa])).map(P3));
        const nAxis = swap ? 2 - p.axis : p.axis;
        const along = nAxis === 0 ? 0 : 2, across = 2 - along;
        const av = pts.flat().map((q) => q[along]);
        p.axis = nAxis; p.a = [q4(Math.min(...av)), q4(Math.max(...av))];
        p.tri = pts.map((pp) => [q4(pp[0][across]), pp[0][1]]);
      }
      return p;
    }
    p.c = P3(p.c);
    if (p.q) p.q = qmul(qy, p.q).map(q4);
    else if (p.r) { p.q = qmul(qy, qEuler(p.r)).map(q4); delete p.r; }
    else if (p.t === "b" || p.t === "r" || p.t === "c" || p.t === "k") { if (swap) p.s = [p.s[2], p.s[1], p.s[0]]; }
    else if (rot) p.r = [0, rot, 0];
    return p;
  };
  const ren = (id) => (id === "outside" ? id : pre + id);
  for (const p of B.parts) A.parts.push(part(p));
  for (const f of B.furniture) A.furniture.push({ ...f, parts: f.parts.map(part) });
  for (const it of B.interactables) {
    const n = JSON.parse(JSON.stringify(it));
    n.id = pre + it.id; n.axis = D3(it.axis);
    n.leaves = it.leaves.map((L) => ({ ...L, parts: L.parts.map(part), hinge: P3(L.hinge) }));
    n.aperture = box(it.aperture.min, it.aperture.max);
    A.interactables.push(n);
  }
  for (const pn of B.panels) {
    const b = box(pn.min, pn.max);
    A.panels.push({ ...pn, id: pn.id.replace("/", "/" + pre), min: b.min, max: b.max, normal: D3(pn.normal), states: pn.states.map((st) => st.map(part)) });
  }
  const nv = B.meta.nav, out = A.meta.nav ?? (A.meta.nav = { effect: "block", rooms: [], doors: [], entrances: [], corners: [], firingPoints: [], cover: [] });
  for (const r of nv.rooms) { const b = box(r.min, r.max); out.rooms.push({ id: ren(r.id), min: b.min, max: b.max }); }
  for (const d of nv.doors) out.doors.push({ a: ren(d.a), b: ren(d.b), p: P3(d.p) });
  for (const e of nv.entrances) out.entrances.push({ p: P3(e.p), dir: D3(e.dir), room: ren(e.room) });
  for (const k of nv.corners) out.corners.push({ p: P3(k.p), room: ren(k.room) });
  for (const f of nv.firingPoints) out.firingPoints.push({ ...f, p: P3(f.p), yaw: (f.yaw + rot + 360) % 360 });
  for (const k of nv.cover ?? []) out.cover.push({ ...k, p: P3(k.p), yaw: (k.yaw + rot + 360) % 360 });
  // the sub-building's tour, ready to append (its start position becomes a goto)
  const steps = [{ goto: P2(B.meta.tour.start.pos[0], B.meta.tour.start.pos[2]).map(q4) }];
  for (const st of B.meta.tour.steps) {
    const n = { ...st };
    if (st.goto) n.goto = P2(st.goto[0], st.goto[1]).map(q4);
    if (st.look) n.look = P3(st.look);
    if (st.aim) n.aim = P3(st.aim);
    if (st.interact) n.interact = pre + st.interact;
    steps.push(n);
  }
  return steps;
}

// a fresh Asset (materials only) for a sub-building that placeSub copies in
const subAsset = (id) => { const B = new Asset({ id, version: 1, name: id, category: "Houses", floors: 1, enterable: true, footprint: [1, 1] }); gxMats(B); return B; };

// ============================================================================ 1. Pays d'Auge colombage house
// Pack 1's half-timbered house and the Norman Pays d'Auge type: a rubble sill wall and rendered timbering below,
// close-studded timbering with brick nogging above, jettied 30 cm on oak joist ends, a steep tiled roof with timbered
// gables, a tiled door hood, a dormer and a central brick stack.
export function colombageAuge() {
  const X = 6.4, Z = 4.3;
  const A = meta("vil_colombage_auge", "Pays d'Auge colombage house", "Houses",
    "Two-storey Pays d'Auge house, 12.8 x 8.6 m: a rubble-stone sill wall under rendered timbering on the ground floor, close-studded oak timbering with herringbone brick nogging on the jettied upper floor (oak bressummer and joist ends), close-studded gables under a steep 50-degree tiled roof, a tiled hood on curved brackets over the door, a dormer, a brick ridge stack, geranium window boxes. Salle and kitchen, two chambers, two stairs.",
    2, [14.2, 11.4], { ivy: [{ face: "-x", a0: -3.5, a1: 0.5, y0: 0.4, y1: 2.6, density: 0.7 }] });
  gxMats(A);
  const c = {
    X, Z, T: 0.4, h: 3.0, n: 2, jetty: 0.3, shot: "gx_auge", timberSp: 0.42,
    style: (f) => (f === 0 ? { m: "render", timber: true, solin: 0.7 } : { m: "brick", timber: true }),
    wd: () => ({ w: 0.9, h: 1.25, sill: 0.95 }),
    win: (f) => (f === 0 ? { front: [-4.7, 0.9, 2.6, 4.7], rear: [-4.5, -2.0, 2.0, 4.5], east: [2.4], west: [2.4] }
      : { front: [-4.7, -1.7, 1.7, 4.7], rear: [-4.5, -1.5, 1.5, 4.5], east: [2.4], west: [2.4] }),
    parts: () => [0], names: (f, r) => (f === 0 ? ["salle", "kitchen"][r] : `chamber_${r + 1}`), hall: 0,
    roof: { type: "gable", rise: 5.0, m: "tile", gable: "render", ov: 0.5 },
  };
  c.dress = (A, k) => {
    const { EAVE, RIDGE, yAt, zA, Zr } = k, fr = face("front", X, Z);
    for (const sx of [-1, 1]) gableTimber(A, sx, X, Zr, EAVE, RIDGE);
    doorHood(A, fr, k.fdx, 1.1, GF + 2.25);
    for (const w of wins(c, 1).filter((w) => w.side === "front")) windowBox(A, face("front", X, Z, 0.3), w.c, w.w + 0.1, w.sill);
    dormerV(A, { x: 0, z: Zr - 0.9, y: RIDGE - (Zr - 0.9) * ((RIDGE - EAVE) / Zr) - 0.05, w: 1.0, h: 1.1, depth: 1.7, s: 1, roof: "tile", m: "render" });
    chimney(A, { x: 1.4, z: -0.3, y0: yAt(2) - 0.2, y1: RIDGE + 1.1, w: 0.62, d: 0.9, m: "brick", pots: 2 });
    fireplace(A, { axis: "z", d: 0.075, s: 1, a: 1.6, y: GF, top: yAt(1) - 0.2, w: 1.5 });   // on the cross wall: the corridor wall has the doorways
    for (const sx of [-1, 1]) downpipe(A, face("front", X, Z, 0.3), sx * (X - 0.25), 0.05, EAVE - 0.3);
    A.box([-2.6, -0.3, Z + 0.6], [-0.8, 0.12, Z + 1.2], "stone_dressed", { bv: 0.02 });   // mounting stone by the door
  };
  house(A, c);
  A.meta.companions = [comp("tree_apple", -9.5, 7.5), comp("tree_apple", 8.5, 8.6, 60), comp("tree_apple", 10.5, -4, 140), comp("nat_flowers_cowparsley", 4.2, 5.6), comp("shrub_hazel", -9.6, -2)];
  return A;
}

// ============================================================================ 2. granite longere farmhouse
// A long low stone farmhouse (longere): dressed granite surrounds and quoins on rubble walls, slate roof with three
// stone eaves dormers, massive gable stacks, an open woodshed lean-to on the east gable, a stone bench by the door.
export function graniteFarmhouse() {
  const X = 9.2, Z = 3.9;
  const A = meta("vil_granite_longere", "Granite longere farmhouse", "Houses",
    "Long low Norman stone farmhouse (longere), 18.4 x 7.8 m: rubble granite walls with dressed quoins and window surrounds, a slate roof with three stone eaves dormers and massive stone stacks on both gables, an open oak woodshed lean-to with stacked logs on the east gable, a stone bench and a water butt by the door. Salle, kitchen with its hearth and a dairy; an open attic under the roof; two stairs.",
    1, [24.4, 10.4], { ivy: "auto" });
  gxMats(A);
  const c = {
    X, Z, T: 0.6, h: 3.0, n: 1, attic: true, knee: 0.7, shot: "gx_longere",
    style: () => ({ m: "stone", surround: true, shutters: false }), wd: () => ({ w: 0.95, h: 1.25, sill: 0.9 }),
    win: (f) => (f === 1 ? {} : { front: [-7.4, -3.8, 2.3, 4.6, 7.6], rear: [-7.4, -3.8, 3.4, 7.4], east: [1.8], west: [-1.8] }),
    parts: () => [-1.6, 2.2], names: (f, r) => ["salle", "kitchen", "dairy"][r], hall: 1,
    roof: { type: "gable", rise: 4.4, m: "slate", gable: "stone", ov: 0.35 },
  };
  c.dress = (A, k) => {
    const { EAVE, RIDGE, zA } = k, fr = face("front", X, Z), pitch = (RIDGE - EAVE) / Z;
    masonry(A, k, { sc: false });
    for (const x of [-3.8, 2.3, 6.0]) dormerV(A, { x, z: Z + 0.05, y: EAVE + 0.2, w: 1.15, h: 1.1, depth: 1.5, s: 1, roof: "slate", m: "stone_dressed" });   // stone eaves dormers
    for (const sx of [-1, 1]) chimney(A, { x: sx * (X - 0.45), z: 0, y0: EAVE - 0.3, y1: RIDGE + 1.3, w: 0.9, d: 1.15, m: "stone", pots: 2 });
    fireplace(A, { axis: "x", d: zA + 0.15, s: 1, a: 1.55, y: GF, top: EAVE - 0.2, w: 1.2, deep: 0.55 });
    leanTo(A, 1, X, -Z + 0.1, Z - 0.1, EAVE - 0.6, { depth: 2.6, yFront: 2.2, m: "slate" });
    A.box([-1.3, 0.0, Z + 0.25], [-0.5, 0.45, Z + 0.7], "stone_dressed", { bv: 0.02 });   // stone bench
    A.box([-1.45, 0.45, Z + 0.2], [-0.35, 0.53, Z + 0.75], "stone_dressed", { bv: 0.02 });
    A.cyl([-2.2, 0.48, Z + 0.55], [0.7, 0.96, 0.7], "oak_y", { n: 0 });                    // water butt
    for (const y of [0.15, 0.8]) A.cyl([-2.2, y, Z + 0.55], [0.73, 0.05, 0.73], "iron", L1);
    void pitch;
  };
  house(A, c);
  A.meta.companions = [comp("tree_apple", -12.5, 6.5), comp("shrub_hazel", 13.5, -3.2), comp("nat_nettle_clump", -11.2, -2.5), comp("nat_flowers_foxglove", 5.0, 5.4), comp("nat_drygrass_tall", -6.0, -6.0)];
  return A;
}

// ============================================================================ 3. brick townhouse (pack 1 narrow townhouse)
// Three bays, tall ground-floor windows, stone architraves with keystones, a stone string course and cornice, a
// pedimented door case, orange pantiles on a hipped roof; party walls left blind for a terrace.
export function brickTownhouse() {
  const X = 4.7, Z = 5.6;
  const A = meta("vil_brick_townhouse", "Brick townhouse", "Houses",
    "Narrow three-bay brick townhouse, 9.4 x 11.2 m, after the Dutch-Flemish townhouses of the reference pack: tall ground-floor windows and three first-floor windows in stone architraves with keystones, a stone plinth, string course and moulded cornice with brackets, a pedimented stone door case, an orange pantiled hipped roof, blind party walls, zinc downpipes and a wall lantern. One room on each floor behind the stairs.",
    2, [10.4, 12.6]);
  gxMats(A);
  const c = {
    X, Z, T: 0.45, h: 3.2, n: 2, corridor: 2.2, shot: "gx_btown",
    style: () => ({ m: "brick", surround: true, key: true, shutters: false }),
    wd: (f) => (f === 0 ? { w: 1.2, h: 2.1, sill: 0.4 } : { w: 1.1, h: 1.75, sill: 0.7 }),
    win: (f) => (f === 0 ? { front: [-3.0, 3.0], rear: [-3.0, 3.0] } : { front: [-3.0, 0, 3.0], rear: [-3.0, 0, 3.0] }),
    parts: () => [], names: (f) => ["front_room", "bedroom"][f], doorW: 1.1,
    roof: { type: "hip", rise: 4.0, m: "tile_orange" },
  };
  c.dress = (A, k) => {
    const fr = face("front", X, Z), { EAVE } = k;
    masonry(A, k, { cor: true });
    modillions(A, fr, -X + 0.2, X - 0.2, EAVE - 0.36, 0.55);
    doorCase(A, fr, 0, 1.1, GF + 2.25, { ped: "tri" });
    A.box([-1.0, -0.3, Z + 0.55], [1.0, 0.02, Z + 0.95], "stone_dressed", { bv: 0.02 });   // second step
    for (const sx of [-1, 1]) downpipe(A, fr, sx * (X - 0.12), 0.05, EAVE - 0.3);
    wallLantern(A, fr, 1.25, GF + 2.4);
    chimney(A, { x: -X + 0.45, z: -2.0, y0: EAVE, y1: EAVE + 0.42 + 4.0 + 0.4, w: 0.7, d: 0.55, m: "brick", pots: 3 });
    fbox(A, fr, 0.5, 0.75, GF + 0.02, GF + 0.12, -0.01, 0.12, "iron", L1);   // boot scraper
  };
  house(A, c);
  A.meta.companions = [comp("nat_flowers_daisy", 3.4, 6.4)];
  return A;
}

// ============================================================================ 4. rendered village house with shutters
export function renderedVillage() {
  const X = 6.2, Z = 4.6;
  const A = meta("vil_rendered_shutters", "Rendered village house", "Houses",
    "Symmetrical two-storey village house, 12.4 x 9.2 m: lime render with dressed-stone quoins and string course, eight pairs of painted plank shutters, a glass-and-iron marquise over the door, geranium window boxes, a slate roof between rendered gables with two capped stacks, zinc gutters and downpipes, a stone bench. One large ground-floor room, two bedrooms above, two stairs.",
    2, [13.6, 11.4], { ivy: [{ face: "-x", a0: -3.6, a1: 1.0, y0: 0.4, y1: 5.2, density: 0.8 }] });
  gxMats(A);
  shutterColour(A, [0.16, 0.24, 0.30]);
  const c = {
    X, Z, T: 0.5, h: 3.1, n: 2, shot: "gx_render",
    style: () => ({ m: "render" }), wd: () => ({ w: 1.0, h: 1.45, sill: 0.85 }),
    win: (f) => (f === 0 ? { front: [-4.6, -1.7, 1.7, 4.6], rear: [-4.4, -1.8, 1.8, 4.4] }
      : { front: [-4.6, -1.7, 1.7, 4.6], rear: [-4.4, -1.4, 1.4, 4.4], east: [1.8], west: [1.8] }),
    parts: (f) => (f === 0 ? [] : [0]), names: (f, r) => (f === 0 ? "living" : `bedroom_${r + 1}`), hall: 0, doorW: 1.1,
    roof: { type: "gable", rise: 4.2, m: "slate", gable: "render", ov: 0.4 },
  };
  c.dress = (A, k) => {
    const fr = face("front", X, Z), { EAVE, RIDGE } = k;
    masonry(A, k);
    marquise(A, fr, 0, 1.1, GF + 2.25);
    for (const w of wins(c, 1).filter((w) => w.side === "front")) windowBox(A, fr, w.c, w.w + 0.1, w.sill);
    for (const sx of [-1, 1]) {
      chimney(A, { x: sx * (X - 0.4), z: 0, y0: EAVE - 0.3, y1: RIDGE + 0.9, w: 0.6, d: 0.95, m: "render", pots: 2 });
      downpipe(A, fr, sx * (X - 0.12), 0.05, EAVE - 0.3);
      downpipe(A, face("rear", X, Z), sx * (X - 0.12), 0.05, EAVE - 0.3);
    }
    for (const x of [-3.05, 3.05]) fireplace(A, { axis: "x", d: k.zA + 0.15, s: 1, a: x * 0.6, y: GF, top: k.yAt(1) - 0.2, w: 1.3 });
    A.box([2.6, 0.0, Z + 0.25], [3.8, 0.42, Z + 0.62], "stone_dressed", { bv: 0.02 });
  };
  house(A, c);
  A.meta.companions = [comp("shrub_hazel", -8.8, 4.2), comp("nat_flowers_poppy", 3.2, 6.2), comp("nat_flowers_daisy", -3.2, 6.2), comp("tree_ash", 10.5, -6, 20)];
  return A;
}

// ============================================================================ 5. corner cafe (mansard)
export function cornerCafe() {
  const X = 6.0, Z = 5.2;
  const A = meta("vil_cafe_corner", "Corner cafe (cafe-tabac)", "Houses",
    "Corner cafe-tabac, 12 x 10.4 m: a painted timber shopfront with tall glazed bays, a lettered fascia and red-and-cream striped awnings on the street and the corner side, two cafe tables with chairs and a chalk menu board on the pavement, ochre render with iron balconettes above, a modillion cornice and a slate mansard roof with zinc rolls and dormers. The cafe room below with a street door and a corner door, two rooms above, two stairs.",
    2, [13.6, 13.4]);
  gxMats(A);
  shutterColour(A, [0.28, 0.30, 0.22]);
  const c = {
    X, Z, T: 0.45, h: 3.4, n: 2, shot: "gx_cafe",
    style: (f) => (f === 0 ? { m: "render_ochre", shutters: false } : { m: "render_ochre" }),
    wd: (f) => (f === 0 ? { w: 1.3, h: 2.2, sill: 0.45 } : { w: 1.0, h: 1.6, sill: 0.6 }),
    win: (f) => (f === 0 ? { front: [{ c: -4.0, w: 1.8 }, -1.75, 1.75, { c: 4.0, w: 1.8 }], rear: [-3.6, 3.6], east: [{ c: -0.6, w: 1.8 }] }
      : { front: [-4.2, -1.5, 1.5, 4.2], rear: [-3.6, 0, 3.6], east: [-1.6, 2.0], west: [2.0] }),
    parts: (f) => (f === 0 ? [] : [0]), names: (f, r) => (f === 0 ? "cafe" : `room_${r + 1}`), doorW: 1.1,
    doors: [{ id: "corner_door", side: "east", c: 3.0, w: 1.0, h: 2.25, room: "cafe" }],
    roof: { type: "none", rise: 3.2 },
  };
  c.dress = (A, k) => {
    const fr = face("front", X, Z), ea = face("east", X, Z), { EAVE } = k;
    masonry(A, k, { cor: true });
    modillions(A, fr, -X, X, EAVE - 0.36, 0.45); modillions(A, ea, -Z, Z, EAVE - 0.36, 0.45);
    // mansard: a steep slate frustum to the break (zinc roll), a low hipped top, dormers in the steep slopes
    const yb = EAVE + 0.42, H1 = 2.2, Xo = X + 0.28, Zo = Z + 0.28, t = (Xo - 0.75) / Xo;
    A.box([-X, EAVE - 0.01, -Z], [X, yb + 0.01, Z], "render_ochre");   // frieze between the cornice and the mansard
    A.cone([0, yb + H1 / 2, 0], [2 * Xo, H1, 2 * Zo], "slate", { n: 4, top: t });
    A.box([-Xo * t - 0.04, yb + H1 - 0.02, -Zo * t - 0.04], [Xo * t + 0.04, yb + H1 + 0.1, Zo * t + 0.04], "zinc", { bv: 0.03 });
    hipRoof(A, { X: Xo * t - 0.05, Z: Zo * t - 0.05, EAVE: yb + H1 + 0.08, RIDGE: yb + H1 + 1.1, ov: 0.08, m: "slate" });
    for (const x of [-4.2, -1.5, 1.5, 4.2]) dormerV(A, { x, z: Z + 0.3, y: yb + 0.35, w: 1.0, h: 1.2, depth: 1.2, s: 1, roof: "zinc", m: "render_white" });
    for (const x of [-2.6, 2.6]) chimney(A, { x, z: -1.2, y0: yb + H1, y1: yb + H1 + 2.0, w: 0.75, d: 0.55, m: "brick", pots: 3 });
    // shopfront, awnings, balconettes
    shopFront(A, fr, -X + 0.25, X - 0.25, GF + 3.15, { word: "CAFE TABAC", stall: [[-4.9, -3.1, GF + 0.43], [-2.4, -1.1, GF + 0.43], [1.1, 2.4, GF + 0.43], [3.1, 4.9, GF + 0.43]] });
    shopFront(A, ea, -Z + 0.3, Z - 0.3, GF + 3.15, { word: "BIERE VINS", stall: [[-1.5, 0.3, GF + 0.43]] });
    for (const [a, w] of [[-4.0, 1.8], [-1.75, 1.3], [1.75, 1.3], [4.0, 1.8]]) awning(A, fr, a - w / 2 - 0.1, a + w / 2 + 0.1, GF + 2.62, 1.25, 0.6);
    awning(A, ea, -1.6, 0.4, GF + 2.62, 1.25, 0.6);
    for (const w of wins(c, 1).filter((w) => w.side === "front")) balcony(A, fr, { a0: w.c - w.w / 2 - 0.15, a1: w.c + w.w / 2 + 0.15, y: w.sill, depth: 0.32, t: 0.1, h: 0.85 });
    // pavement furniture: two round tables with chairs, a chalk board
    for (const [x, z] of [[-3.0, Z + 1.7], [3.0, Z + 1.7]]) {
      // fixed street furniture (plain parts with one invisible collider each: not auto-furnish pieces)
      A.cyl([x, 0.73, z], [0.7, 0.03, 0.7], "faience", { seg: 20, k: 0 });
      A.cyl([x, 0.37, z], [0.06, 0.7, 0.06], "iron", { n: 8, k: 0 });
      A.cyl([x, 0.03, z], [0.42, 0.04, 0.42], "iron", { seg: 16, k: 0 });
      A.box([x - 0.33, 0, z - 0.33], [x + 0.33, 0.75, z + 0.33], "iron", { v: 0 });
      for (const s of [-1, 1]) {
        const cx = x + s * 0.62;
        A.box([cx - 0.2, 0.44, z - 0.2], [cx + 0.2, 0.47, z + 0.2], "rush", { k: 0 });
        for (const ox of [-0.17, 0.17]) for (const oz of [-0.17, 0.17]) A.cyl([cx + ox, 0.22, z + oz], [0.03, 0.44, 0.03], "cherry_y", { n: 8, k: 0, lod: 1 });
        A.box([cx + s * 0.2 - 0.02, 0.47, z - 0.2], [cx + s * 0.2 + 0.02, 0.92, z + 0.2], "cherry_y", { k: 0 });
        A.box([cx - 0.21, 0, z - 0.21], [cx + 0.21, 0.92, z + 0.21], "rush", { v: 0 });
      }
    }
    A.obox([5.2, 0.55, Z + 1.1], [0.6, 1.1, 0.04], [-12, 30, 0], "blackboard", { k: 0 });
    A.obox([5.2, 0.55, Z + 1.32], [0.6, 1.1, 0.04], [12, 30, 0], "blackboard", { k: 0 });
    fireplace(A, { axis: "z", d: -0.075, s: -1, a: 1.5, y: k.yAt(1), top: k.yAt(2) - 0.2, w: 1.2 });
    for (const sx of [-1, 1]) downpipe(A, face("rear", X, Z), sx * (X - 0.12), 0.05, EAVE - 0.3);
  };
  house(A, c);
  A.meta.companions = [comp("tree_ash", -10.0, 7.5, 50), comp("nat_flowers_daisy", -7.2, 3.5)];
  return A;
}

// ============================================================================ 6. baker's shop (boulangerie)
export function bakery() {
  const X = 6.0, Z = 5.0;
  const A = meta("vil_bakery", "Baker's shop (boulangerie)", "Houses",
    "Village bakery, 12 x 10 m: an ashlar ground floor with a painted shopfront (two small-paned display windows, a lettered fascia, a striped awning, an iron sign bracket) under a rendered, shuttered upper floor and a steep tiled roof; at the back a domed brick bread oven projects from the bakehouse with its own tall stack. Shop and bakehouse with the oven mouth, two rooms above, two stairs.",
    2, [13.6, 14.6], { ivy: [{ face: "+x", a0: -4.5, a1: -1.0, y0: 0.4, y1: 4.2, density: 0.7 }] });
  gxMats(A);
  shutterColour(A, [0.30, 0.20, 0.14]);
  const c = {
    X, Z, T: 0.5, h: 3.1, n: 2, shot: "gx_bakery",
    style: (f) => (f === 0 ? { m: "stone_dressed", shutters: false } : { m: "render" }),
    wd: (f) => (f === 0 ? { w: 1.5, h: 1.9, sill: 0.55 } : { w: 0.95, h: 1.4, sill: 0.85 }),
    win: (f) => (f === 0 ? { front: [-4.0, 0.6, { c: 3.9, w: 1.1, h: 1.4, sill: 0.9 }], rear: [-4.0, -2.2] }
      : { front: [-4.2, -1.4, 1.4, 4.2], rear: [-4.0, -1.3, 1.3, 4.0], east: [2.2], west: [2.2] }),
    parts: () => [0], names: (f, r) => (f === 0 ? ["shop", "bakehouse"][r] : `room_${r + 1}`), hall: 0, doorW: 1.0,
    roof: { type: "gable", rise: 4.6, m: "tile", gable: "render", ov: 0.4 },
  };
  c.dress = (A, k) => {
    const fr = face("front", X, Z), { EAVE, RIDGE, zA, yAt } = k;
    quoins(A, X, Z, GF - 0.02, EAVE);
    stringCourse(A, fr, -X - 0.06, X + 0.06, yAt(1) - 0.3);
    shopFront(A, fr, -X + 0.3, 1.6, GF + 2.8, { word: "BOULANGERIE", stall: [[-4.75, -3.25, GF + 0.53], [-0.15, 1.35, GF + 0.53]] });
    awning(A, fr, -4.9, 1.5, GF + 2.27, 1.1, 0.5, ["canvas_cream", "sign_red"]);
    // iron sign bracket with a hanging board
    fbox(A, fr, 2.0, 2.06, GF + 2.9, GF + 2.96, -0.01, 0.95, "iron", { k: 0 });
    fpart(A, "arch", fr, 2.03, GF + 2.55, 0.45, [0.04, 0.35, 0.7], "iron", L1);
    fbox(A, fr, 2.0, 2.06, GF + 2.0, GF + 2.85, 0.35, 0.9, "sign_red", { k: 0 });
    // the bread oven: stone base, brick barrel, dome, a stack through the eaves
    const ox = 3.2, oz = -Z - 0.95;
    A.cyl([ox, 0.55, oz], [2.6, 1.3, 2.0], "stone", { seg: 28 });
    A.cyl([ox, 1.6, oz], [2.4, 0.8, 1.85], "brick", { seg: 28 });
    A.cone([ox, 2.3, oz], [2.4, 0.6, 1.85], "tile", { top: 0.25, seg: 28 });
    chimney(A, { x: ox + 0.5, z: -Z - 0.35, y0: 1.5, y1: RIDGE + 0.6, w: 0.7, d: 0.55, m: "brick", pots: 1 });
    fireplace(A, { axis: "z", d: 0.075, s: 1, a: 2.0, y: GF, top: yAt(1) - 0.2, w: 1.3, deep: 0.55 });   // the oven mouth
    A.box([0.065, GF + 0.55, 1.65], [0.13, GF + 0.98, 2.35], "cast_iron", { k: 0 });
    for (const sx of [-1, 1]) downpipe(A, fr, sx * (X - 0.12), 0.05, EAVE - 0.3);
    chimney(A, { x: -X + 0.4, z: 0.6, y0: EAVE - 0.3, y1: RIDGE + 0.4, w: 0.55, d: 0.9, m: "brick", pots: 2 });
    A.cyl([-5.4, 0.2, Z + 0.6], [0.4, 0.4, 0.4], "sacking", { k: 0 });   // flour sacks by the door
    A.cyl([-5.0, 0.2, Z + 0.75], [0.4, 0.4, 0.4], "sacking", { k: 0 });
  };
  house(A, c);
  A.meta.companions = [comp("nat_flowers_poppy", 5.5, 6.4), comp("shrub_hazel", -8.4, -3.0), comp("nat_nettle_clump", 6.8, -6.5)];
  return A;
}

// ============================================================================ 7. roadside inn (Little Rock Inn)
// The Little Rock Inn's long low body, deep veranda and big rubble chimney, in Norman materials: rubble walls,
// boarded gables, a slate roof, a lean-to veranda on oak posts with a long painted sign board on its roof.
export function relaisInn() {
  const X = 9.0, Z = 4.6;
  const A = meta("vil_relais_inn", "Roadside inn (relais)", "Houses",
    "Roadside coaching inn (relais), 18 x 9.2 m, after the Little Rock Inn: a long single storey of rubble stone with boarded gables under a slate roof, a full-length veranda on chamfered oak posts with a plank deck and a lean-to tiled roof carrying a long painted sign board, a massive outside rubble chimney on the west gable, small-paned windows and two rear dormers. Kitchen with the big hearth and the inn's salle; an open attic; two stairs.",
    1, [22.4, 14.4], { ivy: [{ face: "-z", a0: 2.0, a1: 8.0, y0: 0.4, y1: 3.0 }] });
  gxMats(A);
  const c = {
    X, Z, T: 0.5, h: 3.1, n: 1, attic: true, knee: 0.8, shot: "gx_inn", startOut: 7,
    style: () => ({ m: "stone", shutters: true }), wd: () => ({ w: 0.9, h: 1.3, sill: 0.9 }),
    win: (f) => (f === 1 ? {} : { front: [-7.6, -4.6, -2.0, 5.2, 7.4], rear: [-7.0, -3.4, 3.4, 7.0], east: [1.8], west: [-2.4, 2.4] }),
    parts: () => [-0.5], names: (f, r) => ["kitchen", "salle"][r], hall: 1, doorW: 1.2,
    roof: { type: "gable", rise: 4.0, m: "slate", gable: "plank_y", ov: 0.45 },
  };
  shutterColour(A, [0.22, 0.12, 0.08]);
  c.dress = (A, k) => {
    const { EAVE, RIDGE, Zr, zA } = k, pitch = (RIDGE - EAVE) / Zr;
    quoins(A, X, Z, GF - 0.02, EAVE);
    // boarded gables: vertical battens over the plank gable
    for (const sx of [-1, 1]) for (let z = -Zr + 0.25; z < Zr - 0.2; z += 0.3) {
      const top = EAVE + pitch * (Zr - Math.abs(z)) - 0.1;
      if (top > EAVE + 0.3) fbox(A, { axis: "z", d: sx * X, out: sx }, z - 0.03, z + 0.03, EAVE + 0.05, top, -0.01, 0.025, "oak_y", L1);
    }
    // veranda: stone kerb, plank deck, posts with brackets, a plate, the lean-to roof, a sign board on the roof
    const D = 2.6, ze = Z + D, yRoofW = EAVE - 0.55, yRoofF = 2.75;
    A.box([-X + 0.1, -0.4, Z], [X - 0.1, GF - 0.1, ze], "stone_plinth", { bv: 0.02 });
    A.box([-X + 0.2, GF - 0.12, Z], [X - 0.2, GF - 0.02, ze - 0.1], "plank_x");
    for (const x of [-8.6, -5.8, -3.0, -0.2, 1.4, 4.0, 6.3, 8.6]) {
      A.box([x - 0.11, GF - 0.02, ze - 0.42], [x + 0.11, yRoofF - 0.2, ze - 0.2], "oak_y", { bv: 0.025 });
      for (const s of [-1, 1]) A.obox([x + s * 0.3, yRoofF - 0.42, ze - 0.31], [0.55, 0.09, 0.09], [0, 0, s * -38], "oak_x", L1);
    }
    A.box([-X + 0.2, yRoofF - 0.22, ze - 0.45], [X - 0.2, yRoofF - 0.02, ze - 0.17], "oak_x", { bv: 0.012 });
    const run = D + 0.35, rise = yRoofW - yRoofF, ang = (Math.atan2(rise, run) * 180) / Math.PI;
    A.obox([0, (yRoofW + yRoofF) / 2 + 0.06, Z + run / 2 - 0.05], [2 * X - 0.1, 0.1, Math.hypot(run, rise) + 0.1], [ang, 0, 0], "tile");
    for (let x = -X + 0.6; x < X - 0.4; x += 0.9) A.obox([x, (yRoofW + yRoofF) / 2 - 0.06, Z + run / 2 - 0.05], [0.08, 0.12, Math.hypot(run, rise)], [ang, 0, 0], "oak_z", L1);   // rafters
    A.box([k.fdx - 0.8, -0.3, ze - 0.1], [k.fdx + 0.8, 0.14, ze + 0.35], "stone_dressed", { bv: 0.02 });   // step up onto the deck
    const sy = 3.45, sz = Z + 1.25;
    for (const x of [-4.6, 4.6]) A.box([x - 0.06, sy - 0.4, sz - 0.06], [x + 0.06, sy + 0.2, sz + 0.06], "oak_y", { k: 0 });
    A.box([-5.3, sy + 0.02, sz - 0.04], [5.3, sy + 1.07, sz + 0.04], "sign_red", { k: 0, bv: 0.01 });
    A.box([-5.0, sy + 0.14, sz + 0.03], [5.0, sy + 0.95, sz + 0.06], "canvas_cream", { k: 0 });
    sign(A, { axis: "x", d: sz + 0.06, out: 1 }, -4.6, 4.6, sy + 0.3, 0.46, "RELAIS DE LA POSTE", "sign_red");
    // outside rubble chimney on the west gable
    A.box([-X - 1.15, -0.4, -1.0], [-X + 0.05, EAVE - 0.6, 1.0], "stone", {});
    A.wedge([-X - 0.55, EAVE - 0.6 + 0.3, 0], [2.0, 0.6, 1.2], "stone", { r: [0, -90, 0], top: 0.45 });
    chimney(A, { x: -X - 0.55, z: 0, y0: EAVE - 0.35, y1: RIDGE + 1.0, w: 1.0, d: 1.3, m: "stone", pots: 2 });
    fireplace(A, { axis: "z", d: -0.575, s: -1, a: 1.2, y: GF, top: EAVE - 0.2, w: 2.0, deep: 0.55 });
    for (const x of [-4.0, 4.0]) dormerV(A, { x, z: -(Zr - 0.9), y: RIDGE + 0.14 - (Zr - 0.9) * pitch - 0.05, w: 1.0, h: 1.1, depth: 1.5, s: -1, roof: "slate", m: "plank_y" });
    A.cyl([7.6, GF + 0.42, Z + 1.0], [0.62, 0.84, 0.62], "oak_y", {});   // a cask on the veranda
    for (const y of [GF + 0.15, GF + 0.7]) A.cyl([7.6, y, Z + 1.0], [0.65, 0.05, 0.65], "iron", L1);
  };
  house(A, c);
  A.meta.companions = [comp("tree_oak", -14.5, 8.5), comp("tree_ash", 13.5, -7, 120), comp("nat_flowers_cornflower", 10.4, 4.0), comp("nat_drygrass_tall", -12.0, -5.0)];
  return A;
}

// ============================================================================ L1. merchant's townhouse (pack 1 grand townhouse)
export function merchantHouse() {
  const X = 7.2, Z = 5.6;
  const A = meta("vil_merchant_house", "Merchant's townhouse", "Houses",
    "Grand merchant's townhouse, 14.4 x 11.2 m, three storeys and an attic, after the reference pack's Brussels townhouse: a rusticated ashlar ground floor with two arched shop windows, a rendered piano nobile with pedimented windows, iron balconettes and a stone balcony on consoles, a second floor with architraves, a modillion cornice, a slate roof between crow-stepped brick gables with an ornate central gable and dormers. Shop, salon and dining room, two bedrooms, an open attic; two stairs.",
    4, [15.6, 13.0], { ivy: [{ face: "-z", a0: -6.5, a1: -2.0, y0: 0.4, y1: 5.5, density: 0.7 }] });
  gxMats(A);
  const c = {
    X, Z, T: 0.55, h: 3.4, n: 3, attic: true, knee: 0.8, corridor: 2.0, shot: "gx_merchant", startOut: 8,
    style: (f) => (f === 0 ? { m: "stone_dressed", shutters: false, east: "brick", west: "brick" } : { m: "render_cream", shutters: false, east: "brick", west: "brick", panel: f < 3 ? undefined : false }),
    wd: (f) => (f === 0 ? { w: 1.4, h: 2.3, sill: 0.35 } : f === 1 ? { w: 1.15, h: 2.1, sill: 0.45 } : { w: 1.1, h: 1.7, sill: 0.7 }),
    win: (f) => (f === 3 ? {} : f === 0 ? { front: [-5.3, -2.6, 2.6, 5.3], rear: [-4.8, -2.4, 2.4, 4.8] }
      : { front: [-5.3, -2.6, 0, 2.6, 5.3], rear: [-4.8, -2.4, 0, 2.4, 4.8] }),
    parts: (f) => (f === 0 ? [] : [0]), names: (f, r) => (f === 0 ? "shop" : f === 1 ? ["salon", "dining"][r] : `bedroom_${r + 1}`),
    doorW: 1.4, frontDouble: true,
    roof: { type: "gable", rise: 5.4, m: "slate", gable: "brick", ov: 0.35, gov: 0.02 },
  };
  c.dress = (A, k) => {
    const fr = face("front", X, Z), { EAVE, RIDGE, yAt } = k, pitch = (RIDGE - EAVE) / Z, surf = (z) => RIDGE + 0.14 - Math.abs(z) * pitch;
    const w0 = wins(c, 0).filter((w) => w.side === "front"), w1 = wins(c, 1).filter((w) => w.side === "front"), w2 = wins(c, 2).filter((w) => w.side === "front");
    rustication(A, fr, -X, X, GF, yAt(1) - 0.36, [...w0.map((w) => ({ a0: w.c - w.w / 2 - 0.06, a1: w.c + w.w / 2 + 0.06, y0: -9, y1: w.top + 0.5 })), { a0: -0.95, a1: 0.95, y0: -9, y1: GF + 2.6 }]);
    for (const w of w0) archOpening(A, fr, { a0: w.c - w.w / 2, a1: w.c + w.w / 2, y1: w.top + 0.02, rise: 0.32, w: 0.2, proj: 0.06 });
    doorCase(A, fr, 0, 1.4, GF + 2.25, { ped: "none" });
    quoins(A, X, Z, yAt(1) - 0.3, EAVE);
    for (const f of [1, 2]) for (const s of SIDES.slice(0, 2)) stringCourse(A, face(s, X, Z), -X - 0.06, X + 0.06, yAt(f) - 0.36, { h: 0.22, proj: 0.09 });
    for (const w of w1) {
      surround(A, fr, { a0: w.c - w.w / 2, a1: w.c + w.w / 2, y0: w.sill, y1: w.top, key: false });
      if (w.c === 0) pediment(A, fr, w.c - w.w / 2 - 0.3, w.c + w.w / 2 + 0.3, w.top + 0.16, 0.55, -0.01, 0.13, "stone_dressed");
      else fpart(A, "arch", fr, w.c, w.top + 0.16 + 0.17, 0.065, [w.w + 0.55, 0.34, 0.13], "stone_dressed", { th: 99, k: 0 });
      fbox(A, fr, w.c - w.w / 2 - 0.28, w.c + w.w / 2 + 0.28, w.top + 0.12, w.top + 0.18, -0.01, 0.15, "stone_dressed", { k: 0, bv: 0.01 });
      if (w.c !== 0) balcony(A, fr, { a0: w.c - w.w / 2 - 0.12, a1: w.c + w.w / 2 + 0.12, y: w.sill, depth: 0.3, t: 0.1, h: 0.85 });
    }
    balcony(A, fr, { a0: -1.6, a1: 1.6, y: yAt(1), depth: 0.95, t: 0.18 });
    for (const w of w2) surround(A, fr, { a0: w.c - w.w / 2, a1: w.c + w.w / 2, y0: w.sill, y1: w.top, key: true });
    cornice(A, fr, -X, X, EAVE - 0.5, { h: 0.5, proj: 0.42 }); cornice(A, face("rear", X, Z), -X, X, EAVE - 0.4, { h: 0.4, proj: 0.3 });
    modillions(A, fr, -X + 0.1, X - 0.1, EAVE - 0.5, 0.42);
    for (const sx of [-1, 1]) steppedGable(A, sx, X, Z, EAVE, RIDGE, { n: 6 });
    gableWin(A, { x: 0, w: 3.6, zf: Z, y0: EAVE, rise: 2.8, back: 2.8 / pitch + 0.6, m: "render_cream", roof: "slate", timber: false });
    for (const sx of [-1, 1]) {   // scroll volutes beside the central gable
      fpart(A, "arch", fr, sx * 2.05, EAVE + 0.32, 0.12, [0.7, 0.62, 0.22], "stone_dressed", { th: 0.18, k: 0 });
      fpart(A, "cone", fr, sx * 1.9, EAVE + 2.95, 0.1, [0.18, 0.45, 0.18], "stone_dressed", { ...L1, n: 8 });
    }
    for (const x of [-4.6, 4.6]) dormerV(A, { x, z: Z - 0.9, y: surf(Z - 0.9) - 0.05, w: 1.05, h: 1.25, depth: 1.7, s: 1, roof: "slate", m: "render_cream" });
    for (const x of [-3.5, 0, 3.5]) dormerV(A, { x, z: -(Z - 0.9), y: surf(Z - 0.9) - 0.05, w: 1.0, h: 1.15, depth: 1.6, s: -1, roof: "slate", m: "render_cream" });
    for (const sx of [-1, 1]) for (const z of [-2.6, 2.6]) chimney(A, { x: sx * (X - 0.25), z, y0: EAVE + 1.0, y1: surf(z) + 1.4, w: 0.45, d: 0.8, m: "brick", pots: 2 });
    for (const sx of [-1, 1]) downpipe(A, face("rear", X, Z), sx * (X - 0.12), 0.05, EAVE - 0.3);
    for (const f of [1, 2]) for (const x of [-3.25, 3.25]) fireplace(A, { axis: "x", d: k.zA + 0.15, s: 1, a: x, y: yAt(f), top: yAt(f + 1) - 0.2, w: 1.1 });
    wallLantern(A, fr, -1.25, GF + 2.5); wallLantern(A, fr, 1.25, GF + 2.5);
  };
  house(A, c);
  A.meta.companions = [comp("nat_flowers_daisy", -6.2, 6.8)];
  return A;
}

// ============================================================================ L2. notary's house (pack 3 red-brick villa)
export function notaryHouse() {
  const X = 8.6, Z = 5.6;
  const A = meta("vil_notary_house", "Notary's house", "Houses",
    "Notary's villa, 17.2 x 11.2 m, two storeys and an attic, after the reference pack's red-brick villa: brick walls with dressed-stone quoins, plinth and bands, segmental relieving arches and keystones over the windows, louvred green shutters above, a brick cross gable over the entrance with a stone perron and an arched door hood, a square corner tower with a steep slate pyramid, a steep slate roof with finialed dormers and tall stone-capped stacks, ivy. Study, hall and salon; three bedrooms; an attic; two stairs.",
    3, [21.6, 15.4], { ivy: [{ face: "-x", a0: -5.0, a1: 2.0, y0: 0.4, y1: 6.0 }, { face: "-z", a0: -8.0, a1: -3.5, y0: 0.4, y1: 4.0, density: 0.7 }] });
  gxMats(A);
  shutterColour(A, [0.12, 0.20, 0.13]);
  const c = {
    X, Z, T: 0.5, h: 3.4, n: 2, attic: true, knee: 0.9, corridor: 2.1, shot: "gx_notary", startOut: 8,
    style: (f) => (f === 0 ? { m: "brick", shutters: false } : f === 1 ? { m: "brick", shutters: true } : { m: "brick", panel: false }),
    wd: (f) => (f === 0 ? { w: 1.15, h: 2.0, sill: 0.6 } : { w: 1.05, h: 1.7, sill: 0.75 }),
    win: (f) => (f === 2 ? {} : f === 0 ? { front: [-6.8, -3.5, 3.6, 6.4], rear: [-6.5, -3.0, 3.0, 6.5], east: [-2.5, 1.4], west: [-2.5, 2.5] }
      : { front: [-6.8, -3.5, 0.1, 3.6, 6.4], rear: [-6.5, -3.0, 0, 3.0, 6.5], east: [-2.5, 1.4], west: [-2.5, 2.5] }),
    parts: () => [-1.6, 1.8], names: (f, r) => (f === 0 ? ["study", "hall", "salon"][r] : `bedroom_${r + 1}`), hall: 1, doorW: 1.2,
    roof: { type: "gable", rise: 6.2, m: "slate", gable: "brick", ov: 0.5 },
  };
  c.dress = (A, k) => {
    const fr = face("front", X, Z), { EAVE, RIDGE, yAt, fdx } = k, pitch = (RIDGE - EAVE) / Z, surf = (z) => RIDGE + 0.14 - Math.abs(z) * pitch;
    quoins(A, X, Z, GF - 0.02, EAVE);
    for (const s of SIDES) {
      const isX = s === "front" || s === "rear", F = face(s, X, Z), a0 = isX ? -X - 0.06 : -Z, a1 = isX ? X + 0.06 : Z;
      const gaps = s === "front" ? [[fdx - 0.8, fdx + 0.8]] : s === "rear" ? [[-0.7, 0.7]] : [];
      let at = a0; for (const [g0, g1] of [...gaps, [a1, a1]]) { if (g0 - at > 0.2) stringCourse(A, F, at, g0, GF + 0.55, { h: 0.18 }); at = g1; }
      stringCourse(A, F, a0, a1, yAt(1) - 0.32, { h: 0.2, proj: 0.08 });
      fbox(A, F, a0 + 0.3, a1 - 0.3, yAt(1) + 0.55, yAt(1) + 0.7, -0.01, 0.006, "brick_dark", L1);   // polychrome band
      cornice(A, F, isX ? -X : -Z, isX ? X : Z, EAVE - 0.34, { h: 0.34, proj: 0.24, ext: isX, m: "brick" });
    }
    for (const f of [0, 1]) for (const w of wins(c, f)) segArch(A, face(w.side, X, Z), w.c - w.w / 2, w.c + w.w / 2, w.top + 0.24);
    // entrance: perron, arched hood, cross gable
    for (let i = 0; i < 3; i++) A.box([fdx - 1.6 + i * 0.12, -0.3, Z + 0.55 + i * 0.32], [fdx + 1.6 - i * 0.12, 0.14 - i * 0.14, Z + 0.95 + i * 0.32], "stone_dressed", { bv: 0.02 });
    archOpening(A, fr, { a0: fdx - 0.6, a1: fdx + 0.6, y1: GF + 2.27, rise: 0.6, w: 0.24, proj: 0.08 });
    gableWin(A, { x: fdx, w: 4.6, zf: Z, y0: EAVE, rise: 4.2, back: 4.2 / pitch + 0.6, m: "brick", roof: "slate", timber: true });
    // corner tower: square, banded, a steep slate pyramid with a finial
    const tx = X + 1.0, tz = Z + 1.0, tr = 1.7;
    turret(A, { x: tx, z: tz, y0: -0.2, y1: EAVE + 2.6, r: tr, spire: 6.0, sides: 4, roof: "slate", m: "brick", band: "stone_dressed" });
    for (const y of [GF + 0.55, yAt(1) - 0.32, EAVE - 0.3]) A.cyl([tx, y, tz], [2 * tr + 0.1, 0.2, 2 * tr + 0.1], "stone_dressed", { n: 4, k: 0 });
    for (const f of [0, 1, 2]) for (const a of [0, 90]) { const h = f < 2 ? 1.5 : 1.0; turretWindow(A, tx, tz, tr, a, yAt(f) + 1.5, 0.8, h); blindTurret(A, tx, tz, tr, a, yAt(f) + 1.5, 0.8, h); }
    // dormers with finials, tall stacks
    for (const [x, s] of [[-5.4, 1], [5.0, 1], [-4.5, -1], [0, -1], [4.5, -1]]) {
      dormerV(A, { x, z: s * (Z - 1.0), y: surf(Z - 1.0) - 0.05, w: 1.05, h: 1.25, depth: 1.9, s, roof: "slate", m: "brick" });
      A.cone([x, surf(Z - 1.0) + 1.25 + 0.75, s * (Z - 1.0 + 0.05)], [0.08, 0.5, 0.08], "iron", { ...L1, n: 8 });
    }
    for (const x of [-5.6, 5.6]) chimney(A, { x, z: -1.6, y0: surf(1.6) - 1.0, y1: RIDGE + 1.6, w: 0.75, d: 0.6, m: "brick", pots: 2 });
    for (const x of [-4.75, 4.75]) fireplace(A, { axis: "x", d: k.zA + 0.15, s: 1, a: x, y: GF, top: yAt(1) - 0.2, w: 1.2 });
    for (const sx of [-1, 1]) downpipe(A, face("rear", X, Z), sx * (X - 0.12), 0.05, EAVE - 0.3);
    downpipe(A, fr, -X + 0.12, 0.05, EAVE - 0.3);
    fbox(A, fr, fdx + 0.85, fdx + 1.15, GF + 1.5, GF + 1.75, -0.01, 0.025, "brass", L1);   // the notary's brass plate
  };
  house(A, c);
  A.meta.companions = [comp("tree_oak", -13.5, -4.5), comp("shrub_hazel", -10.5, 7.2), comp("shrub_hazel", 6.0, 8.6, 90), comp("nat_flowers_foxglove", -5.0, 7.4), comp("tree_poplar", 13.5, -7.0)];
  return A;
}

// ============================================================================ L3. courtyard farm (main house + two wings + gate)
export function farmCourtyard() {
  const MX = 8.4, MZ = 5.0, MZc = -6.5;   // main house half extents and its centre z (the whole farm is centred)
  const A = meta("vil_farm_courtyard", "Courtyard farm (cour fermee)", "Houses",
    "Norman courtyard farm, about 27 x 23 m: a two-storey stone farmhouse with an attic, dressed quoins and a slate roof across the back, a stone stable-and-byre wing and a cider-press barn wing either side of a cobbled yard, a front wall with a slate-capped carriage arch and a pedestrian door closing it, a roofed well, a mounting block and a dung heap in the yard. Every range is enterable: kitchen, salle and dairy below, three bedrooms and an attic above; the stable and byre; the press barn and its store.",
    2, [28.6, 24.0], { ivy: "auto" });
  gxMats(A);
  A.meta.nav = { effect: "block", rooms: [], doors: [], entrances: [], corners: [], firingPoints: [], cover: [] };
  // main house, built in its own asset at the origin and moved back by MZc
  const M = subAsset("main");
  const mc = {
    X: MX, Z: MZ, T: 0.6, h: 3.1, n: 2, attic: true, knee: 0.8, shot: "gx_farm",
    style: (f) => ({ m: "stone", surround: true, shutters: false, panel: f < 2 ? undefined : false }), wd: () => ({ w: 1.0, h: 1.4, sill: 0.85 }),
    win: (f) => (f === 2 ? {} : f === 0 ? { front: [-6.2, -3.4, 3.3, 6.2], rear: [-6.0, -3.0, 3.0, 6.0], east: [2.0], west: [2.0] }
      : { front: [-6.2, -3.4, 0.0, 3.3, 6.2], rear: [-6.0, -3.0, 0, 3.0, 6.0], east: [2.0], west: [2.0] }),
    parts: () => [-1.5, 1.6], names: (f, r) => (f === 0 ? ["kitchen", "salle", "dairy"][r] : `bedroom_${r + 1}`), hall: 1,
    roof: { type: "gable", rise: 5.0, m: "slate", gable: "stone", ov: 0.4 },
  };
  mc.dress = (B, k) => {
    masonry(B, k, { sc: true });
    for (const sx of [-1, 1]) chimney(B, { x: sx * (MX - 0.4), z: 0, y0: k.EAVE - 0.3, y1: k.RIDGE + 1.1, w: 0.75, d: 1.1, m: "stone", pots: 2 });
    fireplace(B, { axis: "x", d: k.zA + 0.15, s: 1, a: -4.55, y: GF, top: k.yAt(1) - 0.2, w: 1.1, deep: 0.55 });
    for (const x of [-4.2, 4.2]) dormerV(B, { x, z: MZ - 0.9, y: k.RIDGE + 0.14 - (MZ - 0.9) * ((k.RIDGE - k.EAVE) / MZ) - 0.05, w: 1.0, h: 1.15, depth: 1.6, s: 1, roof: "slate", m: "stone_dressed" });
    doorHood(B, face("front", MX, MZ), k.fdx, 1.1, GF + 2.25, { m: "slate" });
  };
  house(M, mc);
  const mainSteps = placeSub(A, M, { dz: MZc });
  // the two wings: single-storey ranges turned to face the yard, open to the rafters, earth floors
  const wing = (id, X, Z, names, hall, doors, dress) => {
    const W = subAsset(id);
    const k = house(W, {
      X, Z, T: 0.55, h: 3.2, n: 1, shot: "gx_farm_" + id, ceilingTo: -(X - 0.55),
      floorMats: [[-X + 0.55, X - 0.55, "earth"]],
      style: () => ({ m: "stone", shutters: true }), wd: () => ({ w: 0.8, h: 0.9, sill: 1.2 }),
      win: () => ({ front: [-4.2, 4.6], rear: [-2.0, 2.4], east: [{ c: 0, w: 0.5, h: 0.6, sill: 1.7, shutters: false }], west: [{ c: 0, w: 0.5, h: 0.6, sill: 1.7, shutters: false }] }), parts: () => [0.6], names: (f, r) => names[r], hall, rdx: 3.6,
      doors, roof: { type: "gable", rise: 3.4, m: "tile", gable: "stone", ov: 0.35 },
      dress: (B, k) => {
        quoins(B, X, Z, GF - 0.02, k.EAVE);
        for (let x = -X + 1.0; x < X - 0.7; x += 1.15) B.box([x - 0.08, k.EAVE - 0.34, -Z + 0.55], [x + 0.08, k.EAVE - 0.16, Z - 0.55], "oak_z", { k: 0 });   // tie beams
        dress?.(B, k);
      },
    });
    // the tour leaves by the front door into the yard (not the back door), so the wings are walked from the yard
    const st = W.meta.tour.steps, zD = -k.ZI + 1.1, cut = st.findIndex((q) => q.interact === "rear_door");
    st.splice(cut - 1, st.length, { goto: [k.fdx, zD] }, { goto: [k.fdx, k.ZI - 1.4] }, { goto: [k.fdx, Z + 1.3], note: "back out into the yard" });
    return W;
  };
  const WX = 6.0, WZ = 3.0, wingX = MX + WZ - 1.0, wingZ = MZc + MZ + 0.4 + WX;
  const west = wing("byre", WX, WZ, ["stable", "byre"], 0, [{ id: "byre_doors", side: "front", c: 2.4, w: 2.2, h: 2.4, double: true, room: "byre" }]);
  const east = wing("press", WX, WZ, ["press_barn", "store"], 1, [{ id: "press_doors", side: "front", c: -2.4, w: 2.4, h: 2.6, double: true, room: "press_barn" }],
    (B) => { B.cyl([-3.9, GF + 0.45, 0.6], [1.6, 0.9, 1.6], "stone_dressed", { seg: 24 }); B.cyl([-3.9, GF + 1.6, 0.6], [0.2, 1.6, 0.2], "oak_y", {}); });   // the press bed and screw
  const westSteps = placeSub(A, west, { dx: -wingX, dz: wingZ, rot: 90, pre: "w_" });
  const eastSteps = placeSub(A, east, { dx: wingX, dz: wingZ, rot: 270, pre: "e_" });
  const nSub = A.parts.length;
  // yard: cobbles, a front wall between the wings with the carriage arch and a door, a well, a mounting block
  const yx = wingX - WZ, yz0 = MZc + MZ, yz1 = wingZ + WX, gx = 1.7, wallZ = yz1 + 0.3, dc = 4.6;
  A.box([-yx, -0.25, yz0], [yx, 0.06, yz1], "stone_plinth", {});
  const gw = (x0, x1) => { A.box([x0, -0.4, wallZ - 0.3], [x1, 2.6, wallZ + 0.3], "stone"); A.box([x0 - 0.04, 2.6, wallZ - 0.36], [x1 + 0.04, 2.78, wallZ + 0.36], "stone_dressed", { bv: 0.03 }); };
  gw(-yx - 0.3, -gx - 0.5); gw(gx + 0.5, dc - 0.5); gw(dc + 0.5, yx + 0.3);
  A.box([dc - 0.52, 2.17, wallZ - 0.3], [dc + 0.52, 2.6, wallZ + 0.3], "stone");
  A.box([dc - 0.54, 2.6, wallZ - 0.36], [dc + 0.54, 2.78, wallZ + 0.36], "stone_dressed", { bv: 0.03 });
  door(A, { id: "yard_door", axis: "x", outer: wallZ + 0.3, inner: wallZ - 0.3, c: dc, floor: 0.06, w: 1.0, h: 2.1, hingeSide: 1, step: false });
  for (const s of [-1, 1]) A.box([s > 0 ? gx : -gx - 0.5, -0.4, wallZ - 0.4], [s > 0 ? gx + 0.5 : -gx, 4.95, wallZ + 0.4], "stone_dressed", { bv: 0.02 });
  A.arch([0, 3.6 + 0.675, wallZ], [2 * gx, 1.35, 0.8], "stone_dressed", { th: 0.6, fill: true });
  archOpening(A, { axis: "x", d: wallZ + 0.4, out: 1 }, { a0: -gx, a1: gx, y1: 3.6, rise: 0.75, w: 0.24, proj: 0.04 });
  A.box([-gx - 0.6, 4.95, wallZ - 0.5], [gx + 0.6, 5.15, wallZ + 0.5], "stone_dressed", { bv: 0.03 });
  A.prism(0, -gx - 0.65, gx + 0.65, [[wallZ - 0.6, 5.15], [wallZ + 0.6, 5.15], [wallZ, 5.8]], "slate");
  A.cyl([-2.6, 0.45, 1.4], [1.5, 0.9, 1.5], "stone_dressed", { seg: 24 });   // well
  A.cyl([-2.6, 0.86, 1.4], [1.2, 0.1, 1.2], "water", { k: 0 });
  for (const s of [-1, 1]) A.box([-2.6 + s * 0.6 - 0.07, 0.9, 1.33], [-2.6 + s * 0.6 + 0.07, 2.4, 1.47], "oak_y", { bv: 0.01 });
  A.cyl([-2.6, 2.2, 1.4], [0.14, 1.3, 0.14], "oak_x", { r: [0, 0, 90], k: 0 });
  A.prism(2, 1.0, 1.8, [[-3.45, 2.35], [-1.75, 2.35], [-2.6, 2.95]], "slate");
  A.box([3.0, 0.0, 0.2], [3.8, 0.55, 0.8], "stone_dressed", { bv: 0.02 });      // mounting block
  A.box([3.0, 0.0, 0.2], [3.4, 0.85, 0.8], "stone_dressed", { bv: 0.02 });
  A.rbox([-6.4, -0.1, -0.8], [-4.6, 0.55, 0.6], 0.25, "earth", { k: 0 });       // dung heap
  weather(A, { keep: A.parts.slice(0, nSub) });   // the ranges were weathered on their own; only the yard pieces are new
  A.meta.nav.cover.push(cover([-2.6, 0, 0.4], 0, "low", ["over"]), cover([dc + 1.5, 0, wallZ + 0.9], 180, "high", ["left", "right"]));
  A.meta.roomTypes = { w_stable: "barn", w_byre: "barn", e_press_barn: "workshop", e_store: "utility" };
  // the tour: in at the carriage arch, through the main house to its back garden, round the west side and in at the
  // arch again, through each wing from the yard and out of its back door
  const back = MZc - MZ - 1.6, wx = -(wingX + WZ + 1.5);
  setTour(A, { pos: [0, 0, wallZ + 6], yaw: 0 }, [
    { goto: [0, wallZ + 2.0] }, { goto: [0, wallZ - 1.5], note: "through the carriage arch" }, { look: [0, 2.0, yz0], hold: 0.5 }, { shot: "gx_farm_yard" },
    { goto: [0, yz0 + 1.6] }, ...mainSteps.slice(1),
    { goto: [wx, back] }, { goto: [wx, wallZ + 2.0] }, { goto: [0, wallZ + 2.0], note: "back to the carriage arch" }, { goto: [0, wallZ - 1.5] },
    ...westSteps.slice(1), ...eastSteps.slice(1)]);
  A.meta.companions = [comp("tree_apple", -15.5, 9.0), comp("tree_apple", 15.8, 10.0, 80), comp("tree_oak", 15.0, -12.0), comp("nat_nettle_clump", -14.5, -6.0), comp("shrub_bramble", 15.0, 1.0)];
  return A;
}

// ============================================================================ L4. small hotel / pension
export function pensionHotel() {
  const X = 9.6, Z = 5.6;
  const A = meta("vil_pension_hotel", "Hotel-pension", "Houses",
    "Small seaside hotel-pension, 19.2 x 11.2 m, three storeys and an attic: buff brick with white-rendered quoins, bands, architraves and a deep white modillion cornice (after the reference pack's corner townhouse), a glazed double entrance under a glass-and-iron marquise, a lettered sign band, a long iron balcony across the first floor, a slate roof with a rendered central gable, six dormers and four stacks. Dining room, reception hall and salon; six guest rooms; an attic; two stairs.",
    4, [20.8, 13.6], { ivy: [{ face: "+x", a0: -5.0, a1: -1.0, y0: 0.4, y1: 4.0, density: 0.6 }] });
  gxMats(A);
  shutterColour(A, [0.62, 0.60, 0.54]);
  const c = {
    X, Z, T: 0.5, h: 3.2, n: 3, attic: true, knee: 0.9, corridor: 2.1, shot: "gx_pension", startOut: 9,
    style: (f) => ({ m: "brick_buff", shutters: f > 0 && f < 3, panel: f < 3 ? undefined : false }),
    wd: (f) => (f === 0 ? { w: 1.3, h: 2.1, sill: 0.5 } : { w: 1.0, h: 1.65, sill: 0.7 }),
    win: (f) => (f === 3 ? {} : f === 0 ? { front: [-8.0, -4.4, 4.4, 8.0], rear: [-8.0, -4.5, 4.5, 8.0], east: [2.6], west: [2.6] }
      : { front: [-8.0, -5.4, -2.4, 0, 2.4, 5.4, 8.0], rear: [-8.0, -4.5, -1.5, 1.5, 4.5, 8.0], east: [2.6], west: [2.6] }),
    parts: (f) => (f === 0 ? [-1.5, 1.5] : [-2.2, 2.2]), names: (f, r) => (f === 0 ? ["dining", "reception", "salon"][r] : `guest_${f}_${r + 1}`),
    hall: 1, doorW: 1.4, frontDouble: true,
    roof: { type: "gable", rise: 5.4, m: "slate", gable: "brick_buff", ov: 0.45 },
  };
  c.dress = (A, k) => {
    const fr = face("front", X, Z), { EAVE, RIDGE, yAt } = k, pitch = (RIDGE - EAVE) / Z, surf = (z) => RIDGE + 0.14 - Math.abs(z) * pitch, W = "render_white";
    quoins(A, X, Z, GF - 0.02, EAVE);
    for (const p of A.parts) if (p.m === "stone_dressed" && p.t === "b" && p.s[1] <= 0.3) p.m = W;   // white quoins, lintels and sills
    for (const f of [1, 2]) for (const s of SIDES) { const isX = s === "front" || s === "rear"; stringCourse(A, face(s, X, Z), isX ? -X - 0.06 : -Z, isX ? X + 0.06 : Z, yAt(f) - 0.34, { h: 0.2, m: W }); }
    for (const w of wins(c, 0)) surround(A, face(w.side, X, Z), { a0: w.c - w.w / 2, a1: w.c + w.w / 2, y0: w.sill, y1: w.top, key: true, m: W });   // shuttered floors: lintel and sill only
    for (const s of SIDES) { const isX = s === "front" || s === "rear"; cornice(A, face(s, X, Z), isX ? -X : -Z, isX ? X : Z, EAVE - 0.48, { h: 0.48, proj: 0.4, ext: isX, m: W }); }
    modillions(A, fr, -X, X, EAVE - 0.48, 0.45, W); modillions(A, face("rear", X, Z), -X, X, EAVE - 0.48, 0.45, W);
    fbox(A, fr, -3.4, 3.4, yAt(1) - 0.95, yAt(1) - 0.45, -0.01, 0.06, W, { k: 0, bv: 0.01 });   // sign band under the balcony
    sign(A, { ...fr, d: fr.d + fr.out * 0.06 }, -3.0, 3.0, yAt(1) - 0.86, 0.3, "HOTEL DE LA PLAGE", "sign_paint");
    balcony(A, fr, { a0: -3.1, a1: 3.1, y: yAt(1), depth: 0.9, t: 0.16 });
    for (const x of [-8.0, -5.4, 5.4, 8.0]) balcony(A, fr, { a0: x - 0.62, a1: x + 0.62, y: yAt(2) + 0.7, depth: 0.28, t: 0.1, h: 0.85 });
    gableWin(A, { x: 0, w: 5.2, zf: Z, y0: EAVE, rise: 3.6, back: 3.6 / pitch + 0.6, m: W, roof: "slate", timber: false });
    for (const x of [-7.4, -4.2, 4.2, 7.4]) dormerV(A, { x, z: Z - 0.9, y: surf(Z - 0.9) - 0.05, w: 1.0, h: 1.2, depth: 1.8, s: 1, roof: "slate", m: W });
    for (const x of [-5.0, 0, 5.0]) dormerV(A, { x, z: -(Z - 0.9), y: surf(Z - 0.9) - 0.05, w: 1.0, h: 1.2, depth: 1.8, s: -1, roof: "slate", m: W });
    for (const x of [-6.6, -2.3, 2.3, 6.6]) chimney(A, { x, z: -1.4, y0: surf(1.4) - 1.0, y1: RIDGE + 1.3, w: 0.8, d: 0.55, m: "brick_buff", pots: 3 });
    for (const x of [-5.7, 5.7]) fireplace(A, { axis: "x", d: k.zA + 0.15, s: 1, a: x, y: GF, top: yAt(1) - 0.2, w: 1.2 });
    for (const sx of [-1, 1]) { downpipe(A, fr, sx * (X - 0.12), 0.05, EAVE - 0.4); downpipe(A, face("rear", X, Z), sx * (X - 0.12), 0.05, EAVE - 0.4); }
    for (const x of [-1.6, 1.6]) { A.cyl([x, 0.32, Z + 0.7], [0.55, 0.64, 0.55], "faience_blue", { k: 0, top: 0.8 }); A.cone([x, 1.0, Z + 0.7], [0.75, 0.9, 0.75], "leaf", { k: 0, top: 0.0, n: 8 }); }   // potted bay trees
  };
  house(A, c);
  A.meta.companions = [comp("tree_poplar", -14.0, -6.0), comp("tree_poplar", 14.0, -6.0, 40), comp("nat_flowers_poppy", -6.5, 7.2), comp("nat_flowers_daisy", 6.5, 7.2)];
  return A;
}

// ============================================================================ mil_stronghold
// Thin facade skin (paint, shuttering-board lines) on a wall face: the pieces over a destructible bay go into that
// bay's intact state (the damage field takes them as a skin layer; they go when the bay is damaged), the rest are
// plain visual parts.
function panelsOn(A, F) {
  return A.panels.filter((pn) => {
    const n = pn.normal, on = F.axis === "x" ? Math.abs(n[2] - F.out) < 0.01 : Math.abs(n[0] - F.out) < 0.01;
    if (!on) return false;
    const i = F.axis === "x" ? 2 : 0, outer = F.out > 0 ? pn.max[i] : pn.min[i];
    return Math.abs(outer - F.d) < 0.03;
  }).map((pn) => ({ pn, a0: F.axis === "x" ? pn.min[0] : pn.min[2], a1: F.axis === "x" ? pn.max[0] : pn.max[2], y0: pn.min[1], y1: pn.max[1] }));
}
function skin(A, F, a0, a1, y0, y1, e0, e1, m, o = {}, ps = panelsOn(A, F)) {
  for (const r of ps) {
    const b0 = Math.max(a0, r.a0), b1 = Math.min(a1, r.a1), c0 = Math.max(y0, r.y0), c1 = Math.min(y1, r.y1);
    if (b1 - b0 > 0.01 && c1 - c0 > 0.005) fbox(A, F, b0, b1, c0, c1, e0, e1, m, { ...o, k: 0, into: r.pn.states[0] });
  }
  for (const [b0, b1, c0, c1] of cutRect(a0, a1, y0, y1, ps)) if (b1 - b0 > 0.01 && c1 - c0 > 0.005) fbox(A, F, b0, b1, c0, c1, e0, e1, m, { ...o, k: 0 });
}
// shuttering-board imprint: a slightly darker groove line every 0.3 m (a heavier lift joint every 1.2 m), cut round holes
function boardLines(A, F, a0, a1, y0, y1, holes = [], seed = "b") {
  const ps = panelsOn(A, F), R = rnd(A.meta.id + seed);
  for (let y = y0 + 0.3, i = 1; y < y1 - 0.05; y += 0.3, i++) {
    const lift = i % 4 === 0, h = lift ? 0.035 : 0.018;
    for (const [b0, b1, c0, c1] of cutRect(a0, a1, y - h / 2, y + h / 2, holes)) skin(A, F, b0, b1, c0, c1, -0.01, lift ? 0.005 : 0.003, "concrete_board", L1, ps);
    for (let a = a0 + R() * 1.2; a < a1 - 0.3; a += 1.8 + R() * 1.4) {   // board ends: short vertical joints
      if (holes.some((q) => a > q.a0 - 0.05 && a < q.a1 + 0.05 && y + 0.15 > q.y0 && y - 0.15 < q.y1)) continue;
      skin(A, F, a - 0.008, a + 0.008, y + 0.01, Math.min(y + 0.29, y1), -0.01, 0.0025, "concrete_board", L1, ps);
    }
  }
}
// camouflage paint: n soft blotches (ochre / green / brown) in a band of the face, each rasterised as horizontal strokes
// 12-20 cm high whose length follows a wobbling ellipse, so the edges read as brushed rather than rectangular; cut round
// the openings. Later blotches stand a little prouder, so overlaps never share a plane.
function camo(A, F, a0, a1, y0, y1, n, seed, holes = []) {
  const R = rnd(A.meta.id + "/camo/" + seed), ps = panelsOn(A, F), M = ["camo_ochre", "camo_green", "camo_brown"];
  for (let i = 0; i < n; i++) {
    const ac = a0 + R() * (a1 - a0), yc = y0 + R() * (y1 - y0), m = M[i % 3], rx = 0.7 + R() * 1.7, ry = 0.4 + R() * 0.8, e = 0.005 + 0.0007 * (i % 8);
    const ph = R() * 6.28, sk = (R() - 0.5) * 0.8;
    for (let y = yc - ry; y < yc + ry; ) {
      const hs = 0.08 + R() * 0.04, v = (y + hs / 2 - yc) / ry, half = rx * Math.sqrt(Math.max(0, 1 - v * v)) * (0.8 + 0.35 * Math.sin(ph + v * 3.1));
      const mid = ac + sk * v * rx + (R() - 0.5) * 0.08, b0 = Math.max(a0, mid - half), b1 = Math.min(a1, mid + half), c0 = Math.max(y0, y), c1 = Math.min(y1, y + hs);
      if (b1 - b0 > 0.08 && c1 - c0 > 0.04) for (const [q0, q1, r0, r1] of cutRect(b0, b1, c0, c1, holes)) if (q1 - q0 > 0.04) skin(A, F, q0, q1, r0, r1, -0.01, e, m, {}, ps);
      y += hs;
    }
  }
}
// galvanised tube railing between points (posts every ~1.6 m, top and mid rails) with an invisible guard collider
function tubeRail(A, x0, z0, x1, z1, y, h = 1.0) {
  const L = Math.hypot(x1 - x0, z1 - z0), n = Math.max(1, Math.round(L / 1.6));
  for (let i = 0; i <= n; i++) { const t = i / n, x = x0 + (x1 - x0) * t, z = z0 + (z1 - z0) * t; rod(A, [x, y - 0.05, z], [x, y + h, z], 0.045, "zinc", { k: 0 }); }
  rod(A, [x0, y + h, z0], [x1, y + h, z1], 0.045, "zinc", { k: 0 });
  rod(A, [x0, y + h * 0.5, z0], [x1, y + h * 0.5, z1], 0.035, "zinc", { k: 0, lod: 1 });
  const cx = (x0 + x1) / 2, cz = (z0 + z1) / 2, yaw = (Math.atan2(x1 - x0, z1 - z0) * 180) / Math.PI;
  A.obox([cx, y + 0.55, cz], [0.08, 1.1, L], [0, yaw, 0], "zinc", { v: 0 });
}

export function stronghold() {
  const G = 0.4, CEIL = 3.4, ROOF = 5.0, XO = 20, ZF = 8, ZR = -12, XI = 18.6, ZFI = 6.4, ZRI = -10.8, CZ0 = -3.2, CZ1 = -1.6, CY = 7.8;
  const RW = [-3.7, CZ0], FW = [CZ1, -1.1], EYE = G + SLIT.eye, CF = ROOF + 0.04;
  const desc = "Atlantic Wall command stronghold, a 41 x 21 m reinforced-concrete block (about 60 x 34 m with its earthworks): 1.6 m front and 1.2-1.4 m side and rear walls with stepped MG and gun embrasures, a 1.6 m roof slab with rounded lips, shuttering-board imprint and lift joints, ochre-green-brown camouflage paint, run-off stains, a spalled corner with bent rebar. Earth banks bury the flanks and the rear to the roof and grass the rear half of the roof; a sunken rear entrance between retaining walls. Inside: gas lock and guard room behind two steel doors, a 37 m corridor, command room, radio room, observation post with a periscope, generator room, ammunition store, two crew quarters and three fighting rooms (west and east MG casemates with front and flank embrasures, a central gun room); a stair rises to the armoured observation cupola on the roof, whose steel door opens onto a railed roof deck with an MG nest, an aerial mast and concrete steps down the east bank.";
  const S = new Asset({ id: "mil_stronghold", version: 1, name: "Command stronghold (Atlantic Wall)", category: "Bunkers", description: desc,
    floors: 2, enterable: true, footprint: [60, 34], plinthDepth: 0.8, maxGrade: 0.6 });
  bunkerMaterials(S, ROOF); earthMaterials(S); dmat(S, "steel_dark");
  S.mat("concrete_board", [0.205, 0.198, 0.178], 0.94, "bunker", { grain: 50 });
  S.mat("camo_ochre", [0.33, 0.27, 0.15], 0.93, "bunker", { grain: 50 });
  S.mat("camo_green", [0.13, 0.15, 0.075], 0.93, "bunker", { grain: 50 });
  S.mat("camo_brown", [0.17, 0.11, 0.065], 0.93, "bunker", { grain: 50 });
  S.mat("crack", [0.05, 0.048, 0.044], 0.97, "plain");
  S.mat("lumi", [0.55, 0.58, 0.48], 0.6, "painted", { grain: 0 });   // luminous guide stripes
  const m = { stone: "concrete", plaster: "concrete_wash", pt: 0.02 };
  const pn = (side, floor = 0, y0 = G - 0.02, y1 = CEIL, walkY = G) => ({ side, family: "concrete", bands: [[y0, y1, floor, walkY]], collar: 0.3, minW: 1.1 });

  // ---- base, floor
  S.box([-XO, -0.8, ZR], [XO, G - 0.02, ZF], "concrete");
  S.box([-XI, G - 0.02, ZRI], [XI, G, ZFI], "concrete_floor");
  S.rbox([-XO - 0.3, -0.8, ZR - 0.3], [XO + 0.3, 0.22, ZF + 0.3], 0.1, "concrete");   // footing step

  // ---- embrasures (stepped reveals) and the outer walls
  const mgStep = { da: 0.18, dyT: 0.1, dyB: 0.05 }, sl = (o) => slit(S, { y0: G + SLIT.sill, y1: G + SLIT.top, ...o });
  // throats (the inner-face openings): face, a0..a1, y0..y1, MG pintle
  const sy0 = G + SLIT.sill, sy1 = G + SLIT.top;
  const throats = [
    { f: "front", a0: -16.0, a1: -15.4, y0: sy0, y1: sy1, step: mgStep, mg: true }, { f: "front", a0: 3.95, a1: 4.45, y0: sy0, y1: sy1 },
    { f: "front", a0: 8.7, a1: 10.3, y0: G + 1.2, y1: G + 1.75, step: { da: 0.3, dyT: 0.16, dyB: 0.03 } },
    { f: "front", a0: 15.2, a1: 15.8, y0: sy0, y1: sy1, step: mgStep, mg: true },
    { f: "west", a0: 2.2, a1: 2.8, y0: sy0, y1: sy1, step: mgStep, mg: true }, { f: "east", a0: 2.2, a1: 2.8, y0: sy0, y1: sy1, step: mgStep, mg: true },
  ];
  const wallOf = { front: { axis: "x", outer: ZF, inner: ZFI }, west: { axis: "z", outer: -XO, inner: -XI }, east: { axis: "z", outer: XO, inner: XI } };
  for (const t of throats) t.hole = slit(S, { ...wallOf[t.f], a0: t.a0, a1: t.a1, y0: t.y0, y1: t.y1, step: t.step });
  const front = throats.filter((t) => t.f === "front").map((t) => t.hole), west = throats.filter((t) => t.f === "west").map((t) => t.hole), east = throats.filter((t) => t.f === "east").map((t) => t.hole);
  const loop = sl({ axis: "x", outer: ZR, inner: ZRI, a0: -5.45, a1: -4.95 });
  const sd = (id, axis, outer, inner, c, floor, o = {}) => {
    const h = door(S, { id, axis, outer, inner, c, floor, w: 1.0, h: 2.0, hingeSide: -1, steel: true, lintel: false, step: false, label: "steel door", ...o });
    doorKnuckles(S, { axis, outer, inner, c, floor, w: 1.0, h: 2.0, hingeSide: o.hingeSide ?? -1, steel: true });
    return h;
  };
  const ent = sd("entrance_door", "x", ZR, ZRI, -3.8, G, { label: "blast door" });
  wall(S, { axis: "x", outer: ZF, inner: ZFI, aOut: [-XO, XO], aIn: [-XI, XI], y0: G - 0.02, y1: CEIL, holes: front, ...m, panel: pn("front") });
  wall(S, { axis: "x", outer: ZR, inner: ZRI, aOut: [-XO, XO], aIn: [-XI, XI], y0: G - 0.02, y1: CEIL, holes: [ent, loop], ...m, panel: pn("rear") });
  wall(S, { axis: "z", outer: -XO, inner: -XI, aOut: [ZRI - 0.02, ZFI + 0.02], aIn: [ZRI, ZFI], y0: G - 0.02, y1: CEIL, holes: west, ...m, panel: pn("west") });
  wall(S, { axis: "z", outer: XO, inner: XI, aOut: [ZRI - 0.02, ZFI + 0.02], aIn: [ZRI, ZFI], y0: G - 0.02, y1: CEIL, holes: east, ...m, panel: pn("east") });
  for (const sx of [-1, 1]) pier(S, sx > 0 ? XO - 0.6 : -XO - 0.18, sx > 0 ? XO + 0.18 : -XO + 0.6, ZF - 0.6, ZF + 0.18, 0.1, CEIL);   // rounded front arrises

  // ---- rooms: corridor walls with steel doors, cross walls
  const rooms = [
    ["generator", -XI, -12.6, ZRI, RW[0], -15.6, "mil_plant"], ["ammo_store", -12.2, -6.2, ZRI, RW[0], -9.2, "mil_ammo"],
    ["guard_room", -5.8, -1.8, -7.2, RW[0], -3.8, "mil_passage"], ["stair_hall", -1.4, 2.4, ZRI, RW[0], -0.4, "mil_passage"],
    ["quarters_a", 2.8, 10.2, ZRI, RW[0], 6.5, "mil_quarters"], ["quarters_b", 10.6, XI, ZRI, RW[0], 14.6, "mil_quarters"],
    ["fighting_w", -XI, -12.0, FW[1], ZFI, -15.3, "mil_fighting"], ["command", -11.6, -4.0, FW[1], ZFI, -7.8, "mil_command"],
    ["radio", -3.6, 1.4, FW[1], ZFI, -1.1, "mil_radio"], ["observation", 1.8, 6.6, FW[1], ZFI, 4.2, "mil_command"],
    ["fighting_c", 7.0, 12.0, FW[1], ZFI, 9.5, "mil_fighting"], ["fighting_e", 12.4, XI, FW[1], ZFI, 15.5, "mil_fighting"],
  ];
  const rearHoles = [], frontHoles = [];
  for (const [id, , , z0, , c] of rooms) {
    const rear = z0 < CZ0;
    (rear ? rearHoles : frontHoles).push(sd(id + "_door", "x", rear ? RW[1] : FW[0], rear ? RW[0] : FW[1], c, G, { open: true }));
  }
  partition(S, { axis: "x", d0: RW[0], d1: RW[1], a0: -XI, a1: XI, y0: G, y1: CEIL, holes: rearHoles, m: "concrete_wash" });
  partition(S, { axis: "x", d0: FW[0], d1: FW[1], a0: -XI, a1: XI, y0: G, y1: CEIL, holes: frontHoles, m: "concrete_wash" });
  for (const [x0, x1] of [[-12.6, -12.2], [-6.2, -5.8], [-1.8, -1.4], [2.4, 2.8], [10.2, 10.6]]) S.box([x0, G, ZRI], [x1, CEIL, RW[0]], "concrete_wash");
  for (const [x0, x1] of [[-12.0, -11.6], [-4.0, -3.6], [1.4, 1.8], [6.6, 7.0], [12.0, 12.4]]) S.box([x0, G, FW[1]], [x1, CEIL, ZFI], "concrete_wash");
  const gas = sd("gaslock_door", "x", -7.6, -7.2, -3.8, G, { h: 1.9, label: "gas-tight door" });
  partition(S, { axis: "x", d0: -7.6, d1: -7.2, a0: -5.8, a1: -1.8, y0: G, y1: CEIL, holes: [gas], m: "concrete_wash" });

  // ---- roof slab (four rounded slabs round the stair shaft), stair, the shaft housing and the observation cupola
  S.rbox([-XO - 0.6, CEIL, -4.4], [XO + 0.6, ROOF, ZF + 0.6], 0.25, "concrete");
  S.rbox([-XO - 0.6, CEIL, ZR - 0.6], [XO + 0.6, ROOF, -10.1], 0.25, "concrete");
  S.rbox([-XO - 0.6, CEIL, -10.1], [-1.4, ROOF, -4.4], 0.25, "concrete");
  S.rbox([2.4, CEIL, -10.1], [XO + 0.6, ROOF, -4.4], 0.25, "concrete");
  stairs(S, { x0: 1.0, x1: 2.3, z0: -10.55, dir: 1, y0: G, rise: CF - G, n: 23, run: 5.4, openSide: 1 });
  {   // lane side: closed under the flight, guarded from the fourth tread up (the foot is stepped onto from the side)
    const r = (CF - G) / 23, d = 5.4 / 23, z0 = -10.55;
    S.prism(0, 0.98, 1.03, [[z0 + 0.45, G], [-5.15, G], [-5.15, CF - 0.32]], "concrete_wash");
    for (let i = 3; i < 23; i++) S.box([0.93, G + (i + 1) * r - 0.05, z0 + i * d], [0.99, G + (i + 1) * r + 1.1, z0 + (i + 1) * d], "steel_plate", { v: 0 });
    rod(S, [0.96, G + 4 * r + 0.95, z0 + 3 * d], [0.96, CF + 0.95, -5.15], 0.045, "steel_dark", { k: 0 });
    for (let i = 3; i < 23; i += 4) rod(S, [0.96, G + (i + 1) * r, z0 + (i + 0.5) * d], [0.96, G + (i + 1) * r + 0.95, z0 + (i + 0.5) * d], 0.035, "steel_dark", { k: 0 });
  }
  S.box([-1.4, ROOF, -5.15], [2.4, CF, -0.5], "concrete_floor");
  railing(S, "x", -1.4, 1.0, -5.12, CF);
  const CH = 7.2;   // the stair housing's ceiling (the cupola's is CY)
  const cupF = [slit(S, { axis: "x", outer: 0.0, inner: -0.5, a0: -0.8, a1: 1.8, y0: CF + SLIT.sill, y1: CF + SLIT.top, n: 2, step: { da: 0.15, dyT: 0.08, dyB: 0.04 } })];
  const cupW = [sl({ axis: "z", outer: -1.9, inner: -1.4, a0: -2.7, a1: -2.3, y0: CF + SLIT.sill, y1: CF + SLIT.top, n: 2 })];
  const rd = sd("roof_door", "z", 2.9, 2.4, -2.5, CF, { hingeSide: 1, label: "steel door" });
  wall(S, { axis: "x", outer: 0.0, inner: -0.5, aOut: [-1.9, 2.9], aIn: [-1.4, 2.4], y0: ROOF, y1: CY, holes: cupF, ...m, panel: pn("cupola_front", 1, ROOF, CY, CF) });
  wall(S, { axis: "x", outer: -10.5, inner: -10.1, aOut: [-1.9, 2.9], aIn: [-1.4, 2.4], y0: ROOF, y1: CH, holes: [], ...m, panel: null });
  wall(S, { axis: "z", outer: -1.9, inner: -1.4, aOut: [-5.15, -0.48], aIn: [-5.15, -0.5], y0: ROOF, y1: CY, holes: cupW, ...m, panel: pn("cupola_west", 1, ROOF, CY, CF) });
  wall(S, { axis: "z", outer: 2.9, inner: 2.4, aOut: [-5.15, -0.48], aIn: [-5.15, -0.5], y0: ROOF, y1: CY, holes: [rd], ...m, panel: pn("cupola_east", 1, ROOF, CY, CF) });
  for (const [o, i] of [[-1.9, -1.4], [2.9, 2.4]]) wall(S, { axis: "z", outer: o, inner: i, aOut: [-10.12, -5.15], aIn: [-10.1, -5.15], y0: ROOF, y1: CH, holes: [], ...m, panel: null });
  S.rbox([-2.15, CH, -10.85], [3.15, CH + 0.4, -5.05], 0.15, "concrete");                       // housing roof, grassed over
  roofEarth(S, -2.0, 3.0, -10.7, -5.3, CH + 0.4, 0.35, 18);
  S.box([-1.9, CH + 0.3, -5.4], [2.9, CY, -4.95], "concrete");                                   // the cupola's back wall over the housing
  S.rbox([-2.4, CY, -5.6], [3.4, CY + 0.55, 1.0], 0.22, "concrete");                             // visor roof, 1 m over the slit
  const by = CY + 0.55, bx = 0.5, bz = -2.8;
  S.cyl([bx, by + 0.06, bz], [2.7, 0.12, 2.7], "steel_plate", { seg: 32 });                      // the steel bell on its collar
  S.cone([bx, by + 0.12 + 0.55, bz], [2.4, 1.1, 2.4], "steel_plate", { top: 0.55, seg: 32 });
  S.cyl([bx, by + 1.25, bz], [1.36, 0.08, 1.36], "steel_plate", { seg: 24, k: 0 });
  for (let i = 0; i < 6; i++) { const a = (i * 60 * Math.PI) / 180, r = 1.0; S.obox([bx + Math.sin(a) * r, by + 0.62, bz + Math.cos(a) * r], [0.36, 0.1, 0.1], [0, i * 60, 0], "soot", L1); }
  for (let i = 0; i < 16; i++) { const a = (i * 22.5 * Math.PI) / 180; S.cyl([bx + Math.sin(a) * 1.3, by + 0.14, bz + Math.cos(a) * 1.3], [0.06, 0.05, 0.06], "steel_bare", L1); }   // ring bolts
  for (const h of cupF) {   // armoured frame round the observation slit
    fbox(S, { axis: "x", d: 0.0, out: 1 }, h.a0 - 0.14, h.a1 + 0.14, h.y1, h.y1 + 0.12, -0.01, 0.05, "steel_plate", { k: 0 });
    fbox(S, { axis: "x", d: 0.0, out: 1 }, h.a0 - 0.14, h.a1 + 0.14, h.y0 - 0.12, h.y0, -0.01, 0.05, "steel_plate", { k: 0 });
  }

  // ---- deck: kerb, railings, cracks, the MG nest, aerial, periscope head, vents
  S.box([-XO - 0.6, ROOF, ZF + 0.35], [XO + 0.6, ROOF + 0.12, ZF + 0.6], "concrete", { bv: 0.03 });
  tubeRail(S, -XO - 0.35, ZF + 0.25, XO + 0.35, ZF + 0.25, ROOF);
  tubeRail(S, -XO - 0.35, -4.2, -XO - 0.35, ZF + 0.25, ROOF);
  tubeRail(S, XO + 0.35, -4.2, XO + 0.35, -3.75, ROOF);
  tubeRail(S, XO + 0.35, -1.9, XO + 0.35, ZF + 0.25, ROOF);
  {
    const R = rnd("stronghold/cracks");
    for (let c = 0; c < 14; c++) {
      let x = -19 + R() * 38, z = -3.8 + R() * 11.6, yaw = R() * 360;
      for (let s = 0, n = 3 + Math.floor(R() * 5); s < n; s++) {
        const L = 0.4 + R() * 1.2, a = (yaw * Math.PI) / 180, nx = x + Math.sin(a) * L, nz = z + Math.cos(a) * L;
        if (Math.abs(nx) > XO + 0.3 || nz < -4.2 || nz > ZF + 0.3) break;
        S.obox([(x + nx) / 2, ROOF + 0.0015, (z + nz) / 2], [0.018 + R() * 0.012, 0.004, L + 0.02], [0, yaw, 0], "crack", L1);
        x = nx; z = nz; yaw += (R() - 0.5) * 70;
      }
    }
  }
  sandbagWall(S, 0, -XO + 0.1, -16.4, ZF - 0.1, ROOF, 0.9, 21);
  sandbagWall(S, 2, 4.8, ZF - 0.4, -XO + 0.3, ROOF, 0.9, 22);
  rod(S, [-8.0, ROOF, -3.0], [-8.0, ROOF + 7.5, -3.0], 0.07, "steel_dark", { k: 0 });   // aerial mast and its stays
  S.cyl([-8.0, ROOF + 0.08, -3.0], [0.5, 0.16, 0.5], "concrete", { k: 0 });
  for (const [dx, dz] of [[2.6, 1.6], [-2.6, 1.6], [0, -1.2]]) rod(S, [-8.0, ROOF + 6.0, -3.0], [-8.0 + dx, ROOF + 0.05, -3.0 + dz], 0.012, "steel_dark", L1);
  for (const y of [ROOF + 5.0, ROOF + 6.6]) rod(S, [-8.5, y, -3.0], [-7.5, y, -3.0], 0.03, "steel_dark", L1);
  periscope(S, 4.2, ROOF, 4.6, 0, { h: 0.75 });
  for (const [x, z, y] of [[-15.6, -7.4, ROOF + 0.5], [-9.0, -8.0, ROOF + 0.5], [7.0, -8.6, ROOF + 0.5], [14.0, -6.8, ROOF + 0.5], [-4.0, 3.6, ROOF], [13.0, 3.2, ROOF]]) ventPipe(S, x, y, z, { h: 0.55, dia: 0.18 });
  ventPipe(S, -16.4, ROOF + 0.5, -8.6, { h: 1.3, dia: 0.24 });   // generator exhaust
  camoNet(S, { hang: [["z+", -XO - 0.6, -6.0, ZF + 0.6, ROOF - 0.02, ROOF - 0.55], ["z+", 12.0, XO + 0.6, ZF + 0.6, ROOF - 0.02, ROOF - 0.5]] });

  // ---- earthworks: banks to the roof on the flanks and rear, a low glacis in front, grassed earth on the rear roof,
  // the sunken rear entrance between retaining walls, concrete steps down the east bank
  const H = ROOF + 0.4, RUN = 9.0;
  earthBank(S, 0, -XO, -6.6, ZR, -1, H, RUN, 11, { taper: [false, false] });
  earthBank(S, 0, -1.0, XO, ZR, -1, H, RUN, 12, { taper: [false, false] });
  earthBank(S, 2, ZR, -1.5, -XO, -1, H, RUN, 13, { taper: [false, true] });
  earthBank(S, 2, ZR, -3.76, XO, 1, H, RUN, 14, { taper: [false, false] });
  earthBank(S, 2, -1.84, 1.2, XO, 1, H, RUN, 19, { taper: [false, true] });
  for (const sx of [-1, 1]) for (let deg = 5; deg < 90; deg += 10) {   // rear corners: radial wedges with the banks' profile
    const t = (deg * Math.PI) / 180, d = [sx * Math.cos(t), -Math.sin(t)], yaw = (Math.atan2(d[0], d[1]) * 180) / Math.PI, y0 = -0.3, yS = y0 + (H - y0) * 0.8;
    const at = (r) => [sx * XO + d[0] * r, ZR + d[1] * r], r1 = RUN * 0.35, c1 = at(r1 / 2), c2 = at((r1 + RUN) / 2);
    S.wedge([c1[0], (y0 + H) / 2, c1[1]], [1.6, H - y0, r1], "earth_raw", { r: [0, yaw, 0], top: (yS - y0) / (H - y0) });
    S.wedge([c2[0], (y0 + yS) / 2, c2[1]], [1.6, yS - y0, RUN - r1], (deg / 10) % 2 ? "grass_a" : "grass_b", { r: [0, yaw, 0] });
  }
  earthBank(S, 0, -XO - 1.2, XO + 1.2, ZF, 1, 0.85, 4.0, 15, { taper: [true, true] });
  roofEarth(S, -XO - 0.4, -2.3, ZR - 0.4, -4.0, ROOF, 0.5, 16);
  roofEarth(S, 3.3, XO + 0.4, ZR - 0.4, -4.0, ROOF, 0.5, 17);
  for (const [x0, x1] of [[-6.6, -6.0], [-1.6, -1.0]]) {
    S.prism(0, x0, x1, [[ZR + 0.05, -0.4], [ZR + 0.05, H + 0.3], [ZR - RUN - 0.4, -0.4]], "concrete");
    S.prism(0, x0 - 0.05, x1 + 0.05, [[ZR + 0.05, H + 0.25], [ZR + 0.05, H + 0.4], [ZR - RUN - 0.4, -0.25]], "concrete", { k: 0 });   // coping
  }
  S.box([-6.0, -0.3, ZR - RUN - 0.4], [-1.6, 0.12, ZR], "concrete");
  S.box([-4.6, 0.12, ZR - 0.55], [-3.0, 0.26, ZR], "concrete");
  S.box([-3.9, 0.12, ZR - RUN], [-3.7, 0.125, ZR - 0.6], "soot", L1);   // drainage gully
  {
    const ZS0 = -3.6, ZS1 = -2.0, x0 = 19.9, xt = 29.7, slope = (ROOF + 0.4) / (xt - x0);
    S.prism(2, ZS0, ZS1, [[x0, -0.4], [x0, ROOF], [xt, -0.4]], "concrete", { v: 0 });
    for (const [a0, a1] of [[ZS0 - 0.16, ZS0], [ZS1, ZS1 + 0.16]]) {   // cheek walls, retaining the bank (its shoulder profile)
      const xs = XO + RUN * 0.35, ys = -0.3 + (H + 0.3) * 0.8 + 0.15;
      S.prism(2, a0, a1, [[x0, -0.4], [x0, H + 0.15], [xs, ys]], "concrete");
      S.prism(2, a0, a1, [[x0, -0.4], [xs, ys], [XO + RUN + 0.3, -0.4]], "concrete");
    }
    for (let x = XO + 0.6; x < xt - 0.2; x += 0.34) {
      const y = ROOF - (x + 0.17 - x0) * slope + 0.09;
      S.box([x, y - 0.25, ZS0], [x + 0.37, y, ZS1], "concrete", { k: 0, bv: 0.015 });
    }
    rod(S, [XO + 0.7, ROOF + 0.95, ZS1 + 0.08], [xt - 0.2, 0.3 + 0.95, ZS1 + 0.08], 0.045, "zinc", { k: 0 });
    for (let x = XO + 0.7; x < xt; x += 2.2) rod(S, [x, ROOF - (x - x0) * slope + 0.4, ZS1 + 0.08], [x, ROOF - (x - x0) * slope + 1.0, ZS1 + 0.08], 0.04, "zinc", L1);
  }

  // ---- fixed fittings inside: corridor duct and cables, luminous stripes, the generator's tank and exhaust, the
  // observation periscope, the gas-filter unit, embrasure frames and MG pintles
  S.box([-XI + 0.4, CEIL - 0.42, CZ0 + 0.12], [XI - 0.4, CEIL - 0.08, CZ0 + 0.52], "steel_plate", { k: 0 });
  for (let x = -XI + 2.0; x < XI - 1; x += 4.0) S.box([x - 0.25, CEIL - 0.44, CZ0 + 0.15], [x + 0.25, CEIL - 0.41, CZ0 + 0.49], "steel_dark", L1);
  S.cyl([0, CEIL - 0.15, CZ1 - 0.12], [0.09, 2 * XI - 0.8, 0.09], "steel_dark", { r: [0, 0, 90], k: 0 });
  for (const [d0, d1, hs] of [[CZ0 - 0.004, CZ0 + 0.006, rearHoles], [CZ1 - 0.006, CZ1 + 0.004, frontHoles]])   // luminous guide stripes
    for (const [a0, a1] of cutRect(-XI + 0.3, XI - 0.3, 0, 1, hs.map((h) => ({ a0: h.a0 - 0.05, a1: h.a1 + 0.05, y0: -1, y1: 2 }))).map((r) => [r[0], r[1]])) S.box([a0, G + 1.0, d0], [a1, G + 1.06, d1], "lumi", L1);
  S.cyl([-14.0, G + 0.7, -9.9], [0.95, 2.4, 0.95], "steel_plate", { r: [0, 0, 90], k: 0 });   // day tank on saddles
  for (const x of [-15.0, -13.0]) S.box([x - 0.15, G, -10.4], [x + 0.15, G + 0.45, -9.4], "concrete", {});
  S.box([-15.25, G, -10.4], [-12.75, G + 1.2, -9.4], "steel_plate", { v: 0 });
  rod(S, [-16.4, G + 1.6, -8.6], [-16.4, CEIL + 0.05, -8.6], 0.16, "steel_dark", { k: 0 });
  S.box([-XI, G + 1.1, -6.2], [-XI + 0.14, G + 2.1, -4.6], "steel_plate", { k: 0, bv: 0.01 });   // switchboard
  for (let i = 0; i < 5; i++) S.box([-XI + 0.14, G + 1.5, -6.0 + i * 0.3], [-XI + 0.2, G + 1.62, -5.94 + i * 0.3], "steel_bare", L1);
  S.cyl([4.2, (CEIL + G + 1.35) / 2, 4.6], [0.1, CEIL - G - 1.35, 0.1], "steel_dark", { k: 0 });   // the observation periscope
  S.box([4.05, G + 1.25, 4.42], [4.35, G + 1.5, 4.78], "steel_dark", { k: 0, bv: 0.02 });
  for (const s of [-1, 1]) S.cyl([4.2 + s * 0.26, G + 1.36, 4.6], [0.05, 0.22, 0.05], "steel_dark", { r: [0, 0, 90], k: 0, lod: 1 });
  S.box([-5.75, G, -5.6], [-5.15, G + 1.35, -4.4], "steel_plate", { bv: 0.02 });   // gas-filter ventilator
  S.cyl([-5.1, G + 0.9, -5.0], [0.3, 0.06, 0.3], "steel_dark", { r: [0, 0, 90], k: 0, lod: 1 });
  rod(S, [-5.45, G + 1.35, -5.0], [-5.45, CEIL, -5.0], 0.22, "steel_plate", { k: 0 });
  for (const t of throats) {   // steel liner frame round each throat on the inner face, an MG pintle post below it
    const F = t.f === "front" ? { axis: "x", d: ZFI, out: -1 } : t.f === "west" ? { axis: "z", d: -XI, out: 1 } : { axis: "z", d: XI, out: -1 };
    fbox(S, F, t.a0 - 0.12, t.a0, t.y0 - 0.12, t.y1 + 0.12, -0.01, 0.03, "steel_plate", L1);
    fbox(S, F, t.a1, t.a1 + 0.12, t.y0 - 0.12, t.y1 + 0.12, -0.01, 0.03, "steel_plate", L1);
    fbox(S, F, t.a0 - 0.12, t.a1 + 0.12, t.y1, t.y1 + 0.12, -0.01, 0.035, "steel_plate", L1);
    fbox(S, F, t.a0 - 0.12, t.a1 + 0.12, t.y0 - 0.12, t.y0, -0.01, 0.035, "steel_plate", L1);
    const mid = (t.a0 + t.a1) / 2, at = (e, y) => (F.axis === "x" ? [mid, y, F.d + F.out * e] : [F.d + F.out * e, y, mid]);
    if (t.mg) { rod(S, at(0.3, G), at(0.3, t.y0 - 0.05), 0.09, "steel_dark", { k: 0 }); fpart(S, "cyl", F, mid, t.y0 - 0.04, 0.3, [0.22, 0.04, 0.22], "steel_dark", L1); }
  }

  // ---- surface: shuttering lines, camouflage, moss, the spalled corner with rebar
  const outerHoles = (hs) => hs.map((q) => ({ a0: q.a0 - 0.1, a1: q.a1 + 0.1, y0: q.y0 - 0.1, y1: q.y1 + 0.1 }));
  const Ff = { axis: "x", d: ZF, out: 1 }, Fw = { axis: "z", d: -XO, out: -1 }, Fe = { axis: "z", d: XO, out: 1 };
  boardLines(S, Ff, -XO, XO, 0.25, CEIL, outerHoles(front), "f");
  boardLines(S, Fw, -1.6, ZF, 0.25, CEIL, outerHoles(west), "w");
  boardLines(S, Fe, -1.6, ZF, 0.25, CEIL, outerHoles(east), "e");
  boardLines(S, { axis: "x", d: ZR, out: -1 }, -6.0, -1.6, 0.25, CEIL, outerHoles([ent, loop]), "r");
  for (const [F, a0, a1] of [[{ axis: "x", d: ZF + 0.6, out: 1 }, -XO - 0.4, XO + 0.4], [{ axis: "z", d: -XO - 0.6, out: -1 }, -4.2, ZF + 0.4], [{ axis: "z", d: XO + 0.6, out: 1 }, -4.2, ZF + 0.4]])
    boardLines(S, F, a0, a1, CEIL + 0.1, ROOF - 0.12, [], "lip" + F.d);
  boardLines(S, { axis: "x", d: 0.0, out: 1 }, -1.9, 2.9, ROOF + 0.1, CY, outerHoles(cupF), "cf");
  for (const t of throats) {   // concrete hoods over the embrasures (splash and shell deflectors)
    const h = t.hole, top = h.y1 + 0.06;
    if (t.f === "front") S.rbox([h.a0 - 0.45, top, ZF - 0.05], [h.a1 + 0.45, top + 0.3, ZF + 0.42], 0.09, "concrete");
    else { const sx = t.f === "east" ? 1 : -1, x0 = sx * XO - (sx > 0 ? 0.05 : 0.42), x1 = sx * XO + (sx > 0 ? 0.42 : 0.05); S.rbox([x0, top, h.a0 - 0.45], [x1, top + 0.3, h.a1 + 0.45], 0.09, "concrete"); }
  }
  for (const x of [-12.0, -3.8, 1.6, 6.8, 12.2]) skin(S, Ff, x - 0.015, x + 0.015, 0.25, CEIL, -0.01, 0.004, "concrete_board", L1);   // expansion joints
  camo(S, Ff, -XO, XO, 0.3, CEIL, 26, "f", outerHoles(front)); camo(S, { axis: "x", d: ZF + 0.6, out: 1 }, -XO - 0.4, XO + 0.4, CEIL + 0.1, ROOF - 0.12, 16, "lip");
  camo(S, Fw, -1.6, ZF, 0.3, CEIL, 5, "w", outerHoles(west)); camo(S, Fe, -1.6, ZF, 0.3, CEIL, 5, "e", outerHoles(east));
  camo(S, { axis: "x", d: 0.0, out: 1 }, -1.9, 2.9, ROOF + 0.1, CY, 4, "cf", outerHoles(cupF)); camo(S, { axis: "z", d: 2.9, out: 1 }, -5.15, 0, ROOF + 0.1, CY, 4, "ce", [{ a0: -3.05, a1: -1.95, y0: 0, y1: CF + 2.1 }]); camo(S, { axis: "z", d: -1.9, out: -1 }, -5.15, 0, ROOF + 0.1, CY, 4, "cw", outerHoles(cupW));
  {
    const R = rnd("stronghold/moss");
    for (let i = 0; i < 16; i++) { const x = -XO + R() * 2 * XO; mossPatch(S, [x, ROOF + 0.004, ZF + 0.02 + R() * 0.2], [0.4 + R() * 1.4, 0.008, 0.2 + R() * 0.2], [0, R() * 8 - 4, 0]); }
    for (let i = 0; i < 10; i++) { const z = -3 + R() * 10; mossPatch(S, [-XO - 0.3 - R() * 0.25, ROOF + 0.004, z], [0.2 + R() * 0.25, 0.008, 0.4 + R() * 1.0], [0, R() * 8 - 4, 0]); }
  }
  // the spalled front-west corner of the roof lip: exposed core, bent bars, broken lumps below
  S.box([-XO - 0.62, CEIL + 0.3, ZF - 0.2], [-XO - 0.5, ROOF - 0.35, ZF + 0.62], "concrete_core", { k: 0 });
  S.box([-XO - 0.62, CEIL + 0.3, ZF + 0.5], [-XO + 0.3, ROOF - 0.35, ZF + 0.62], "concrete_core", { k: 0 });
  rebar(S, [-XO - 0.55, CEIL + 0.45, ZF - 0.1], [-XO - 0.55, ROOF - 0.45, ZF + 0.5], [-1, -0.3, 0.2], { n: 8, len: 0.4, seed: "lipW" });
  rebar(S, [-XO - 0.4, CEIL + 0.45, ZF + 0.56], [-XO + 0.2, ROOF - 0.45, ZF + 0.56], [0, -0.3, 1], { n: 6, len: 0.35, seed: "lipF" });
  {
    const R = rnd("stronghold/lumps");
    for (let i = 0; i < 9; i++) { const s = 0.15 + R() * 0.35; S.obox([-XO - 0.6 - R() * 1.6, s * 0.35, ZF + 0.4 + R() * 1.8], [s * 1.3, s * 0.7, s], [R() * 30, R() * 180, R() * 30], "concrete_core", { k: 0 }); }
  }
  weather(S, { pocks: [{ F: { axis: "x", d: ZF + 0.6, out: 1 }, a0: -18, a1: 18, y0: CEIL + 0.3, y1: ROOF - 0.3, n: 7 }, { F: { axis: "z", d: XO + 0.6, out: 1 }, a0: -3, a1: 7, y0: CEIL + 0.3, y1: ROOF - 0.3, n: 2 }] });

  // ---- nav, tour
  const nav = { effect: "block", rooms: [], doors: [], entrances: [], corners: [], firingPoints: [], cover: [] };
  const addRoom = (id, x0, x1, z0, z1, y, top) => { nav.rooms.push(room(id, [x0, y, z0], [x1, top, z1])); nav.corners.push(...corners(id, x0, x1, z0, z1, y)); };
  addRoom("corridor", -XI, XI, CZ0, CZ1, G, CEIL);
  addRoom("gaslock", -5.8, -1.8, ZRI, -7.6, G, CEIL);
  for (const [id, x0, x1, z0, z1, c] of rooms) {
    addRoom(id, x0, x1, z0, z1, G, CEIL);
    nav.doors.push({ a: id, b: "corridor", p: [c, G, z0 < CZ0 ? -3.45 : -1.35] });
  }
  addRoom("cupola", -1.4, 2.4, -5.15, -0.5, CF, CY);   // with the stair head under the housing
  nav.doors.push({ a: "gaslock", b: "outside", p: [-3.8, G, ZR + 0.6] }, { a: "gaslock", b: "guard_room", p: [-3.8, G, -7.4] },
    { a: "stair_hall", b: "cupola", p: [1.65, CF, -5.15] }, { a: "cupola", b: "outside", p: [2.65, CF, -2.5] });
  nav.entrances.push({ p: [-3.8, 0.26, ZR - 0.9], dir: [0, 0, 1], room: "gaslock" }, { p: [3.8, ROOF, -2.5], dir: [-1, 0, 0], room: "cupola" });
  nav.firingPoints.push(fire([-15.7, G, ZFI - 0.6], 0, "mg"), fire([-XI + 0.6, G, 2.5], 270, "mg"), fire([4.2, G, ZFI - 0.6], 0, "embrasure"),
    fire([9.5, G, ZFI - 0.6], 0, "embrasure"), fire([15.5, G, ZFI - 0.6], 0, "mg"), fire([XI - 0.6, G, 2.5], 90, "mg"), fire([-5.2, G, ZRI + 0.6], 180, "embrasure"),
    fire([-0.45, CF, -1.1], 0, "embrasure"), fire([0.5, CF, -1.1], 0, "embrasure"), fire([1.45, CF, -1.1], 0, "embrasure"), fire([-0.8, CF, -2.5], 270, "embrasure"),
    fire([-18.4, ROOF, 6.9], 0, "mg"), fire([-19.0, ROOF, 5.6], 270, "mg"));
  nav.cover.push(cover([XO + 0.9, 0, ZF - 0.4], 270, "high", ["right"]), cover([-XO - 0.9, 0, ZF - 0.4], 90, "high", ["left"]),
    cover([-17.6, ROOF, 7.2], 0, "low", ["over"]), cover([-18.9, ROOF, 6.0], 270, "low", ["over"]));
  setNav(S, nav);
  const into = (id, c, rear, shot, look) => {
    const zIn = rear ? -6.5 : 2.4, zW = rear ? -4.4 : -0.4, s = [{ goto: [c, -2.4] }, { goto: [c, zW] }, { goto: [c, zIn], expectY: G, note: "into the " + id }];
    if (shot) s.push({ look: look ?? [c, G + 1.0, rear ? ZRI : ZFI], hold: 0.45 }, { shot });
    return [...s, { goto: [c, zW] }, { goto: [c, -2.4] }];
  };
  setTour(S, { pos: [-3.8, 0, ZR - 11], yaw: 0 }, [
    { goto: [-3.8, ZR - 8.0], note: "into the sunken entrance" }, { goto: [-3.8, ZR - 1.0] },
    { interact: "entrance_door", aim: [-3.6, G + 1.1, ZRI - 0.06], expectOpen: true },
    { goto: [-3.8, -9.6], expectY: G, note: "the gas lock" }, { look: [-3.8, G + 1.3, -7.4], hold: 0.4 }, { shot: "gx_strong_a_gaslock" }, { goto: [-3.8, -8.4] },
    { interact: "gaslock_door", aim: [-3.6, G + 1.1, -7.26], expectOpen: true },
    { goto: [-3.8, -6.4], expectY: G, note: "the guard room" }, { goto: [-3.8, -4.4] }, { goto: [-3.8, -2.4], expectY: G, note: "the corridor" },
    { look: [12, G + 1.4, -2.4], hold: 0.5 }, { shot: "gx_strong_b_corridor" },
    ...into("command", -7.8, false, "gx_strong_c_command"),
    ...into("ammo_store", -9.2, true, "gx_strong_d_ammo"),
    { goto: [-15.6, -2.4] }, { goto: [-15.6, -4.4] }, { goto: [-15.6, -7.6], expectY: G, note: "the generator room" }, { look: [-15.6, G + 0.9, ZRI], hold: 0.45 }, { shot: "gx_strong_e_generator" }, { goto: [-15.6, -4.4] },
    { goto: [-15.3, -2.4] }, { goto: [-15.3, -0.4] }, { goto: [-15.7, ZFI - 0.7], expectY: G, note: "at the west MG embrasure" },
    { look: [-15.7, EYE, 30], hold: 0.6 }, { shot: "gx_strong_f_embrasure" }, { look: [-14.0, G + 1.0, -1.1], hold: 0.4 }, { shot: "gx_strong_g_fighting" },
    { goto: [-15.3, -0.4] }, { goto: [-15.3, -2.4] },
    ...into("radio", -1.1, false, "gx_strong_h_radio"),
    ...into("observation", 4.2, false, "gx_strong_i_observation", [4.2, G + 1.4, 4.6]),
    ...into("quarters_a", 6.5, true, "gx_strong_j_quarters"),
    ...into("fighting_c", 9.5, false, null), ...into("quarters_b", 14.6, true, null), ...into("fighting_e", 15.5, false, "gx_strong_k_fighting_e"),
    { goto: [-0.4, -2.4] }, { goto: [-0.4, -4.4] }, { goto: [-0.4, -10.1], expectY: G, note: "the stair hall" }, { goto: [0.6, -10.42] }, { goto: [1.65, -10.42] },
    { goto: [1.65, -4.7], expectY: CF, note: "up the stair to the cupola" }, { goto: [0.5, -1.4], expectY: CF },
    { look: [0.5, CF + 1.62, 20], hold: 0.6 }, { shot: "gx_strong_l_cupola" },
    { goto: [1.15, -2.5] }, { interact: "roof_door", aim: [2.46, CF + 1.1, -2.3], expectOpen: true },
    { goto: [3.9, -2.5], expectY: ROOF, note: "out onto the roof deck" }, { goto: [19.4, -2.8] }, { look: [0, ROOF + 1.2, 5], hold: 0.5 }, { shot: "gx_strong_m_deck" },
    { goto: [21.0, -2.8] }, { goto: [31.0, -2.8], note: "down the steps in the east bank" },
  ]);
  S.meta.roomTypes = Object.fromEntries([["corridor", "mil_passage"], ["gaslock", "mil_passage"], ["cupola", "mil_fighting"], ...rooms.map((r) => [r[0], r[6]])]);

  // centre the footprint (about 60 x 34 m): the asset's origin sits in the middle of block and earthworks
  const A = new Asset({ ...S.meta, nav: undefined, tour: undefined, roomTypes: undefined });
  A.materials = S.materials;
  const OZ = 4.5, steps = placeSub(A, S, { dz: OZ });
  A.meta.roomTypes = S.meta.roomTypes;
  setTour(A, { pos: [S.meta.tour.start.pos[0], 0, S.meta.tour.start.pos[2] + OZ], yaw: 0 }, steps.slice(1));
  // earthwork lines for terrain berm stamps (GX1): the toe and crest of each bank, asset space
  A.meta.earthworks = [
    { kind: "berm", from: [-XO - RUN, ZR + OZ], to: [XO + RUN, ZR + OZ], dir: [0, -1], crest: H, run: RUN, gap: [-6.6, -1.0] },
    { kind: "berm", from: [-XO, ZR - RUN + OZ], to: [-XO, -1.5 + OZ], dir: [-1, 0], crest: H, run: RUN },
    { kind: "berm", from: [XO, ZR - RUN + OZ], to: [XO, -3.75 + OZ], dir: [1, 0], crest: H, run: RUN },
    { kind: "glacis", from: [-XO - 1.2, ZF + OZ], to: [XO + 1.2, ZF + OZ], dir: [0, 1], crest: 0.85, run: 4.0 },
  ];
  A.meta.companions = [comp("nat_hawthorn_bush", -31, -2), comp("nat_drygrass_tall", 26, 9), comp("nat_bracken_clump", -26, -16), comp("nat_nettle_clump", 3.5, -18.5)];
  return A;
}

// ============================================================================ military room furniture props
// (written by tools/gen_furniture_mil.py; listed here so the asset list in docs stays in one place)
export const MIL_PROPS = ["furn_mil_bunk", "furn_mil_locker", "furn_mil_radio", "furn_mil_map", "furn_mil_ammo_rack", "furn_mil_crates", "furn_mil_table",
  "furn_mil_gasmask_hooks", "furn_mil_rifle_rack", "furn_mil_generator", "furn_mil_jerrycans", "furn_mil_phone", "furn_mil_fusebox"];

export function gxHouseAssets() {
  return [colombageAuge(), graniteFarmhouse(), brickTownhouse(), renderedVillage(), cornerCafe(), bakery(), relaisInn(),
    merchantHouse(), notaryHouse(), farmCourtyard(), pensionHotel(), stronghold()];
}
