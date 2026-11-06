// Bunkers: machine-gun casemate, observation post, field shelter, command shelter, large casemate (docs/bunkers.md).
// Floors sit just above grade (the terrain is not excavated). The MG casemate, command shelter and large casemate are
// free-standing poured concrete (no earth wedges); the observation post and the field shelter keep their earth cover.
// Regenerate only these files with `node tools/build_bunkers.mjs` (build_assets.mjs rewrites every catalogue asset).
import { Asset, wbox } from "./kit.mjs";
import { stdMaterials, wall, partition, door, hinged, aabb, chair, table } from "./parts.mjs";
import { shelfX, shelfZ, crates, bunkZ, stove, stool, lamp, crateBox } from "./furniture2.mjs";

// ----------------------------------------------------------------------------- shared bunker vocabulary
// Viewing-slit height, shared by every slit in every bunker (docs/bunkers.md#slit-height). Floor-relative, metres.
// Measured on the old command-shelter loophole: 0.50 x 0.30 m, 1.25-1.55 m above the floor, which left the standing
// eye (1.62 m) 7 cm above the slit. The 0.30 m opening is kept and centred on the standing eye instead.
export const SLIT = { sill: 1.47, top: 1.77, w: 0.5, eye: 1.62 };
// stepped reveal (Treppenscharte): the opening widens by dy (top and bottom) and da (each closed end) per depth band
// toward the outside, so a slit is narrowest at the inner face
const STEP = { dy: 0.065, da: 0.12 };

function bunkerMaterials(A, lip) {
  stdMaterials(A);
  const set = (name, albedo, roughness, grain) => Object.assign(A.materials.find((m) => m.name === name), { albedo, roughness, pattern: "bunker", grain });
  set("concrete", [0.27, 0.26, 0.235], 0.93, Math.round(lip * 10));   // grain: roof-lip height (dm) for the run-off stains
  set("concrete_wash", [0.5, 0.49, 0.45], 0.92, 0);
  A.mat("sand", [0.24, 0.21, 0.15], 0.98, "plain");
}
// A slit through a wall of depth outer..inner, given by its throat (the inner-face opening). Adds the stepped liners
// and returns the hole to cut through the full wall (the outermost, largest opening). open0/open1: that end runs on
// through a corner (no end step). n depth bands share the wall thickness.
function slit(A, { axis, outer, inner, a0, a1, y0, y1, open0 = false, open1 = false, n = 3, step = {} }) {
  const st = { da: STEP.da, dyT: STEP.dy, dyB: STEP.dy, ...step };   // a gun embrasure widens more, mostly upward
  const out = Math.sign(outer - inner), t = Math.abs(outer - inner) / n, g = n - 1;
  const H = { a0: open0 ? a0 : a0 - g * st.da, a1: open1 ? a1 : a1 + g * st.da, y0: y0 - g * st.dyB, y1: y1 + g * st.dyT };
  for (let k = 0; k < g; k++) {   // band k counts from the inner face; its opening is the throat grown by k steps
    const d0 = inner + out * k * t, d1 = inner + out * (k + 1) * t;
    const oa0 = open0 ? a0 : a0 - k * st.da, oa1 = open1 ? a1 : a1 + k * st.da, oy0 = y0 - k * st.dyB, oy1 = y1 + k * st.dyT;
    if (oy0 > H.y0 + 1e-4) wbox(A, axis, H.a0, H.a1, H.y0, oy0, d0, d1, "concrete");
    wbox(A, axis, H.a0, H.a1, oy1, H.y1, d0, d1, "concrete");
    if (!open0) wbox(A, axis, H.a0, oa0, oy0, oy1, d0, d1, "concrete");
    if (!open1) wbox(A, axis, oa1, H.a1, oy0, oy1, d0, d1, "concrete");
  }
  return H;
}
// rectangular poured shell: four walls of thickness T (front/back own the corners), the floor and the base under it
function shell(A, { X, Z, T, GF, CEIL, front = [], back = [], east = [], west = [] }) {
  const XI = X - T, ZI = Z - T, y0 = GF - 0.02, pt = 0.02;
  A.box([-X, -0.6, -Z], [X, y0, Z], "concrete");
  A.box([-XI, y0, -ZI], [XI, GF, ZI], "concrete_floor");
  const m = { stone: "concrete", plaster: "concrete_wash", pt };
  const pn = (side) => ({ side, family: "concrete", bands: [[y0, CEIL, 0, GF]], collar: 0.3, minW: 1.1 });
  wall(A, { axis: "x", outer: Z, inner: ZI, aOut: [-X, X], aIn: [-XI, XI], y0, y1: CEIL, holes: front, ...m, panel: pn("front") });
  wall(A, { axis: "x", outer: -Z, inner: -ZI, aOut: [-X, X], aIn: [-XI, XI], y0, y1: CEIL, holes: back, ...m, panel: pn("rear") });
  wall(A, { axis: "z", outer: X, inner: XI, aOut: [-ZI - pt, ZI + pt], aIn: [-ZI, ZI], y0, y1: CEIL, holes: east, ...m, panel: pn("east") });
  wall(A, { axis: "z", outer: -X, inner: -XI, aOut: [-ZI - pt, ZI + pt], aIn: [-ZI, ZI], y0, y1: CEIL, holes: west, ...m, panel: pn("west") });
  return { XI, ZI };
}
// footing step at ground level, a little wider than the walls
function footing(A, x0, x1, z0, z1, top = 0.22, out = 0.28) { A.rbox([x0 - out, -0.6, z0 - out], [x1 + out, top, z1 + out], 0.1, "concrete"); }
// heavy roof slab with rounded arrises; ov = [west, east, rear, front] overhangs
function roofSlab(A, x0, x1, z0, z1, y0, y1, ov, r = 0.2) { A.rbox([x0 - ov[0], y0, z0 - ov[2]], [x1 + ov[1], y1, z1 + ov[3]], r, "concrete"); }
// corner pier / pilaster / wing pier: a rounded block standing proud of the wall faces
function pier(A, x0, x1, z0, z1, y0, y1, r = 0.12) { A.rbox([x0, y0, z0], [x1, y1, z1], r, "concrete"); }

// Camouflage netting (visual only: no collision, rounds and view rays pass): draped over a roof top in a few slightly
// tilted sheets, hung from roof edges as skirts that flare out toward the ground, and short festoons along a firing
// front's fascia. Never hung in front of a slit. hang: [side, a0, a1, edge, yTop, yBottom], side "x+"/"x-"/"z+"/"z-".
function camoNet(A, { roof = null, hang = [] }) {
  if (!A.materials.find((m) => m.name === "camo_net")) A.mat("camo_net", [0.12, 0.12, 0.06], 0.95, "net");
  const o = { k: 0 };
  if (roof) {
    const [x0, x1, z0, z1, y] = roof, nx = Math.max(1, Math.round((x1 - x0) / 3.2)), nz = Math.max(1, Math.round((z1 - z0) / 3.2));
    for (let i = 0; i < nx; i++) for (let j = 0; j < nz; j++) {
      const ax = x0 + ((x1 - x0) * i) / nx, bx = x0 + ((x1 - x0) * (i + 1)) / nx, az = z0 + ((z1 - z0) * j) / nz, bz = z0 + ((z1 - z0) * (j + 1)) / nz;
      const tilt = (((i * 7 + j * 3) % 5) - 2) * 0.7;
      A.obox([(ax + bx) / 2, y + 0.05 + 0.02 * ((i + j) % 2), (az + bz) / 2], [bx - ax + 0.25, 0.035, bz - az + 0.25], [tilt, 0, -tilt * 0.6], "camo_net", o);
    }
  }
  for (const [side, a0, a1, edge, yTop, yBot] of hang) {
    const n = Math.max(1, Math.round((a1 - a0) / 1.6)), sgn = side[1] === "+" ? 1 : -1, h = yTop - yBot, flare = Math.min(0.5, h * 0.18);
    for (let i = 0; i < n; i++) {
      const b0 = a0 + ((a1 - a0) * i) / n, b1 = a0 + ((a1 - a0) * (i + 1)) / n, jit = ((i * 5) % 3 - 1) * 2.5;
      const lean = (Math.atan2(flare, h) * 180) / Math.PI + jit * 0.4;
      const d = edge + sgn * (0.03 + flare / 2);
      if (side[0] === "z") A.obox([(b0 + b1) / 2, (yTop + yBot) / 2, d], [b1 - b0 + 0.12, h, 0.035], [sgn * lean, jit, 0], "camo_net", o);
      else A.obox([d, (yTop + yBot) / 2, (b0 + b1) / 2], [0.035, h, b1 - b0 + 0.12], [0, jit, -sgn * lean], "camo_net", o);
    }
  }
}
// sand / earth drifted against a wall face at `w` (axis 0: wall along x at z = w; axis 2: along z at x = w), running
// out `run` toward `dir`: a low wedge whose height tapers over the last metre at each end (no steep cut-off faces)
function drift(A, axis, a0, a1, w, dir, h, run) {
  const seg = (b0, b1, f) => A.prism(axis, b0, b1, [[w, -0.2], [w, -0.2 + (h + 0.2) * f], [w + dir * run * (0.4 + 0.6 * f), -0.2]], "sand");
  const L = Math.min(1.6, (a1 - a0) / 4), N = 6;
  for (let i = 0; i < N; i++) { const f = (i + 1) / (N + 1); seg(a0 + (L * i) / N, a0 + (L * (i + 1)) / N, f); seg(a1 - (L * (i + 1)) / N, a1 - (L * i) / N, f); }
  seg(a0 + L, a1 - L, 1);
}
// pre-overhaul concrete shell + earth berms (observation post)
function concreteShell(A, { X, Z, T, GF, CEIL, ROOF, front = [], back = [], east = [], west = [], ov = 0.1 }) {
  const { XI, ZI } = shell(A, { X, Z, T, GF, CEIL, front, back, east, west });
  A.box([-X - ov, CEIL, -Z - ov], [X + ov, ROOF, Z + ov], "concrete");
  return { XI, ZI };
}
function berms(A, { X, Z, front, sideH, sideRun = 3.0, frontRun = 2.5, zr = null }) {
  const [z0, z1] = zr || [-Z, Z];
  for (const s of [-1, 1]) A.prism(2, z0, z1, [[s * X, -0.3], [s * X, sideH], [s * (X + sideRun), -0.3]], "turf");
  if (front != null) A.prism(0, -X, X, [[Z, -0.3], [Z, front], [Z + frontRun, -0.3]], "turf");
}
// steel plate leaf builder helper: returns build fn adding plate + ribs between p0..p1
const plate = (A, p0, p1, ribAxis, ribs = 2, handle = null) => (T) => {
  A.box(p0, p1, "steel_plate", T);
  const mn = [0, 1, 2].map((i) => Math.min(p0[i], p1[i])), mx = [0, 1, 2].map((i) => Math.max(p0[i], p1[i]));
  for (let r = 1; r <= ribs; r++) {
    const a = mn.slice(), b = mx.slice();
    const f = r / (ribs + 1), i = ribAxis;
    const c = mn[i] + (mx[i] - mn[i]) * f;
    a[i] = c - 0.025; b[i] = c + 0.025;
    A.box(a, b, "steel_plate", T);
  }
  if (handle) A.box(handle[0], handle[1], "steel_bare", T);
};

// ----------------------------------------------------------------------------- MG casemate
// Long front with a continuous viewing slit that wraps both front corners and runs back over the front half of each
// short side (corner posts carry the lintel). Rear entrance recessed between wing piers, behind a blast wall.
export function mgCasemate() {
  const X = 4.0, Z = 2.9, T = 1.2, GF = 0.3, CEIL = GF + 2.5, ROOF = CEIL + 1.0, XI = X - T, ZI = Z - T, pt = 0.02;
  const A = new Asset({
    id: "bunker_mg_casemate", version: 2, name: "Machine-Gun Casemate", category: "Bunkers",
    description: "Free-standing reinforced-concrete MG casemate, 8 x 5.8 m, 1.2 m walls under a heavy overhanging roof slab. A continuous stepped viewing slit runs the full front and wraps both corners over the front half of each side. Recessed steel rear door behind a blast wall, camouflage netting over the roof and rear. Placed with its MG 42 embrasure mount; ammunition, bench and shelf inside.",
    floors: 1, enterable: true, footprint: [10.0, 10.4], plinthDepth: 0.6, maxGrade: 0.6,
  });
  bunkerMaterials(A, CEIL);
  const s0 = GF + SLIT.sill, s1 = GF + SLIT.top;
  // viewing slits: the front over its whole length (through both corners), the sides from the front corner to z = 0
  const fr = slit(A, { axis: "x", outer: Z, inner: ZI, a0: -X, a1: X, y0: s0, y1: s1, open0: true, open1: true });
  const sides = [X, -X].map((xo) => slit(A, { axis: "z", outer: xo, inner: xo - Math.sign(xo) * T, a0: 0, a1: ZI + pt + 0.001, y0: s0, y1: s1, open1: true }));
  const rear = door(A, { id: "casemate_door", axis: "x", outer: -Z, inner: -ZI, c: -1.6, floor: GF, w: 1.0, h: 1.9, hingeSide: -1, steel: true, lintel: false, step: false, label: "steel door" });
  shell(A, { X, Z, T, GF, CEIL, front: [fr], back: [rear], east: [sides[0]], west: [sides[1]] });
  // corner posts in the wrapping slit: the lintel and the roof visibly bear on them
  for (const sx of [-1, 1]) A.box([sx > 0 ? X - 0.8 : -X + 0.4, fr.y0, Z - 0.8], [sx > 0 ? X - 0.4 : -X + 0.8, fr.y1, Z - 0.4], "concrete");
  footing(A, -X, X, -Z, Z);
  roofSlab(A, -X, X, -Z, Z, CEIL, ROOF, [0.6, 0.6, 0.25, 0.7]);
  // rounded corner piers: rear corners full height, front corners up to the slit
  const P = 0.15, W = 0.55;
  for (const sx of [-1, 1]) {
    const xa = sx > 0 ? X - W : -X - P, xb = sx > 0 ? X + P : -X + W;
    pier(A, xa, xb, -Z - P, -Z + W, 0.1, CEIL);
    pier(A, xa, xb, Z - W, Z + P, 0.1, fr.y0);
  }
  pier(A, 1.75, 2.35, -Z - 0.22, -Z + 0.05, 0.1, CEIL - 0.05, 0.1);   // pilaster breaking the long rear wall
  // recessed entrance: wing piers either side of the door with a hood over it, a step, and the blast wall
  pier(A, rear.a0 - 0.45, rear.a0, -Z - 0.6, -Z + 0.05, 0.1, GF + 2.25, 0.1);
  pier(A, rear.a1, rear.a1 + 0.45, -Z - 0.6, -Z + 0.05, 0.1, GF + 2.25, 0.1);
  A.rbox([rear.a0 - 0.5, GF + 2.0, -Z - 0.65], [rear.a1 + 0.5, GF + 2.45, -Z + 0.05], 0.1, "concrete");
  A.box([rear.a0, -0.2, -Z - 0.85], [rear.a1, 0.12, -Z - 0.28], "concrete");
  A.box([-3.0, -0.3, -Z - 2.3], [-0.2, 2.2, -Z - 1.8], "concrete");
  // interior
  // the MG 42 on its embrasure pedestal mount (a crew station, gun_mg42_embrasure) is placed with the casemate
  A.meta.companions = [{ asset: "gun_mg42_embrasure", pos: [0, GF, 1.5], yaw: 0 }];   // pivot 0.2 m behind the slit
  camoNet(A, { roof: [-X - 0.6, X + 0.6, -Z - 0.25, Z + 0.7, ROOF],
    hang: [["z-", -X - 0.6, X + 0.6, -Z - 0.25, ROOF, 0.45], ["x+", -Z - 0.25, -0.3, X + 0.6, ROOF, 0.5], ["x-", -Z - 0.25, -0.3, -X - 0.6, ROOF, 0.5],
           ["z+", -X - 0.6, X + 0.6, Z + 0.7, ROOF + 0.02, ROOF - 0.5]] });
  crates(A, "Ammunition boxes", [[-1.25, GF, 1.4, 0.45, 0.22, 0.28], [-1.25, GF + 0.22, 1.4, 0.45, 0.22, 0.28], [-1.3, GF, 1.0, 0.45, 0.22, 0.28, 8]], "crate_olive");
  A.beginFurniture("Bench");
  A.box([-XI, GF + 0.42, -1.6], [-2.22, GF + 0.47, -0.2], "plank_z");
  for (const z of [-1.45, -0.35]) A.box([-2.75, GF, z - 0.04], [-2.28, GF + 0.42, z + 0.04], "plank_y");
  A.endFurniture();
  shelfX(A, "Shelf", 0.4, 2.6, -ZI, 1, GF, 1.8, 0.35, 3, "plank_x", (y, za, zz, i) => {
    if (i > 0) for (let x = 0.6; x < 2.4; x += 0.5) crateBox(A, x, y, (za + zz) / 2, 0.4, 0.2, 0.28, "crate_olive");
  });
  A.box([-2.5, CEIL - 0.25, -0.1], [2.5, CEIL - 0.1, 0.05], "steel_plate", { k: 0 }); // ventilation duct
  lamp(A, -1.2, CEIL, 0.2); lamp(A, 1.4, CEIL, 0.2);
  const eye = GF + SLIT.eye;
  A.walkthrough = {
    start: { pos: [1.5, 0, -7.4], yaw: 180 },
    steps: [
      { goto: [1.5, -4.0], note: "approach the rear" },
      { goto: [-1.6, -4.0], note: "behind the blast wall" },
      { interact: "casemate_door", aim: [-1.4, GF + 1.1, -ZI - 0.08], expectOpen: true },
      { goto: [-1.6, -0.9], expectY: GF, note: "enter casemate" },
      { goto: [1.9, 1.15], expectY: GF, note: "at the front slit" },
      { look: [1.9, eye, 16], hold: 0.8 }, { shot: "13_casemate_embrasure" },
      { look: [12, eye, 4], hold: 0.6 }, { shot: "13b_casemate_side_slit" },
      { look: [-1.6, 0.8, -1.6], hold: 0.6 }, { shot: "14_casemate_interior" },
      { goto: [-1.6, -0.9] }, { goto: [-1.6, -4.0] }, { goto: [1.5, -4.0] }, { goto: [1.5, -7.0], note: "leave casemate" },
    ],
  };
  return A;
}

// ----------------------------------------------------------------------------- observation post
export function observationPost() {
  const A = new Asset({
    id: "bunker_observation_post", version: 2, name: "Observation Post", category: "Bunkers",
    description: "Small concrete observation post with an eye-level slit closed by a top-hung armoured flap, operated from inside. Map table, field telephone and stool.",
    floors: 1, enterable: true, footprint: [10.0, 8.9], plinthDepth: 0.6, maxGrade: 0.6,
  });
  const X = 2.25, Z = 2.25, T = 0.8, GF = 0.2, CEIL = 2.3, ROOF = 3.1, XI = X - T, ZI = Z - T;
  bunkerMaterials(A, CEIL);
  // 1.5 m wide at the shared slit height (SLIT), cut through the full wall depth (geometry and collision)
  const slitR = { a0: -0.75, a1: 0.75, y0: GF + SLIT.sill, y1: GF + SLIT.top };
  const rear = door(A, { id: "op_door", axis: "x", outer: -Z, inner: -ZI, c: 0.6, floor: GF, w: 1.0, h: 1.9, hingeSide: 1, steel: true, lintel: false, step: false, label: "steel door" });
  concreteShell(A, { X, Z, T, GF, CEIL, ROOF, front: [slitR], back: [rear] });
  A.box([slitR.a0, slitR.y0 - 0.12, ZI - 0.25], [slitR.a1, slitR.y0, ZI], "concrete");   // elbow ledge under the slit
  // armoured flap on the exterior face, hinged along its top edge, lifts outward
  const fy0 = slitR.y0 - 0.06, fy1 = slitR.y1 + 0.08, fz0 = Z + 0.004, fz1 = Z + 0.045;
  const p0 = [slitR.a0 - 0.1, fy0, fz0], p1 = [slitR.a1 + 0.1, fy1, fz1];
  hinged(A, {
    id: "viewing_flap", kind: "flap", label: "viewing flap", sound: "steel", axis: "x", openAngle: 78, duration: 1.4, apertureAlways: true,
    aperture: aabb([slitR.a0, slitR.y0, ZI], [slitR.a1, slitR.y1, Z + 0.05]),
    leaves: [{ hinge: [0, fy1 + 0.02, fz1 + 0.01], want: [0, 1, 1], collider: [p0, p1],
      build: (T) => { plate(A, p0, p1, 0, 2)(T); for (const x of [slitR.a0 + 0.05, slitR.a1 - 0.15]) A.box([x, fy1 - 0.02, fz1], [x + 0.1, fy1 + 0.04, fz1 + 0.03], "steel_bare", T); } }],
  });
  for (const x of [slitR.a0 - 0.05, slitR.a1 - 0.05]) A.box([x, fy1 + 0.0, Z], [x + 0.1, fy1 + 0.06, Z + 0.05], "steel_bare", { k: 0 });
  berms(A, { X, Z, front: GF + 1.0, sideH: ROOF - 0.3, sideRun: 2.7, frontRun: 2.2 });
  // interior
  A.beginFurniture("Map table");
  A.box([-XI, GF + 0.9, -0.8], [-1.0, GF + 0.95, 0.4], "plank_z");
  A.box([-1.1, GF, -0.75], [-1.04, GF + 0.9, -0.69], "plank_y");
  A.box([-1.1, GF, 0.29], [-1.04, GF + 0.9, 0.35], "plank_y");
  A.box([-1.4, GF + 0.95, -0.7], [-1.05, GF + 0.955, 0.25], "paper");
  A.endFurniture();
  stool(A, "Stool", -0.75, -0.2, GF, 0.55);
  A.beginFurniture("Field telephone");
  A.box([XI - 0.2, GF + 1.15, 0.15], [XI, GF + 1.4, 0.45], "crate_olive");
  A.box([XI - 0.25, GF + 1.3, 0.2], [XI - 0.2, GF + 1.35, 0.4], "blackboard");
  A.endFurniture(false);
  shelfX(A, "Shelf", -XI, -0.6, -ZI, 1, GF, 1.7, 0.3, 3, "plank_x");
  lamp(A, 0, CEIL, -0.3);
  const eye = GF + SLIT.eye;
  A.walkthrough = {
    start: { pos: [0.6, 0, -4.2], yaw: 180 },
    steps: [
      { goto: [0.6, -3.0], note: "approach observation post" },
      { interact: "op_door", aim: [0.4, 1.1, -1.49], expectOpen: true },
      { goto: [0.4, -0.3], expectY: GF, note: "enter observation post" },
      { goto: [0, 0.6], expectY: GF, note: "at the slit" },
      { look: [0, eye, 1.7], hold: 0.4 }, { shot: "15a_op_slit_closed" },
      { interact: "viewing_flap", aim: [0, eye, 1.7], expectOpen: true },
      { look: [0, eye, 16], hold: 1.0 }, { shot: "15b_op_flap_view" },
      { interact: "viewing_flap", aim: [0, eye, 1.7], expectOpen: false, note: "close the flap from inside" },
      { goto: [0.4, -0.3] }, { goto: [0.6, -3.0] }, { goto: [0.6, -4.3], note: "leave observation post" },
    ],
  };
  return A;
}

// ----------------------------------------------------------------------------- field shelter
export function fieldShelter() {
  const A = new Asset({
    id: "bunker_field_shelter", version: 2, name: "Field Shelter", category: "Bunkers",
    description: "Log-built field shelter (Unterstand) under an earth cover. Plank door, a small eye-level aperture with a top-hung timber shutter, two-tier bunks, stove and bench.",
    floors: 1, enterable: true, footprint: [9.2, 7.4], plinthDepth: 0.5, maxGrade: 0.6,
  });
  stdMaterials(A);
  const X = 2.5, Z = 2.0, T = 0.4, GF = 0.15, CEIL = 2.3, XI = X - T, ZI = Z - T, y0 = GF - 0.02;
  const ap = { a0: 0.55, a1: 0.55 + SLIT.w, y0: GF + SLIT.sill, y1: GF + SLIT.top };   // shared slit height (SLIT)
  A.box([-X, -0.5, -Z], [X, y0, Z], "earth");
  A.box([-XI, y0, -ZI], [XI, GF, ZI], "plank_x");
  const rear = door(A, { id: "shelter_door", axis: "x", outer: -Z, inner: -ZI, c: 0, floor: GF, w: 1.0, h: 1.98, hingeSide: -1, leafMat: "plank_y", frameMat: "plank_y", thresholdMat: "plank_x", lintel: false, step: false });
  const lw = (axis, outer, inner, aOut, aIn, holes, m, side) => wall(A, { axis, outer, inner, aOut, aIn, y0, y1: CEIL, holes, stone: m, plaster: m, pt: 0.02, panel: { side, family: "timber", bands: [[y0, CEIL, 0, GF]], collar: 0.25 } });
  lw("x", Z, ZI, [-X, X], [-XI, XI], [ap], "log_x", "front");
  lw("x", -Z, -ZI, [-X, X], [-XI, XI], [rear], "log_x", "rear");
  lw("z", X, XI, [-ZI - 0.02, ZI + 0.02], [-ZI, ZI], [], "log_z", "east");
  lw("z", -X, -XI, [-ZI - 0.02, ZI + 0.02], [-ZI, ZI], [], "log_z", "west");
  for (const x of [-XI, 0, XI - 0.18]) for (const z of [-ZI, ZI - 0.18]) if (!(x === 0 && z === -ZI)) A.box([x, GF, z], [x + 0.18, CEIL, z + 0.18], "log_z", { k: 0 });
  for (let x = -X - 0.1; x < X + 0.1; x += 0.26) A.box([x, CEIL, -Z - 0.1], [x + 0.25, CEIL + 0.26, Z + 0.1], "log_z");
  A.box([-X - 0.1, CEIL - 0.14, -0.1], [X + 0.1, CEIL, 0.1], "log_x", { k: 0 });
  A.prism(0, -X - 0.15, X + 0.15, [[-Z - 0.3, CEIL + 0.26], [Z + 0.3, CEIL + 0.26], [0, CEIL + 0.85]], "turf");
  berms(A, { X, Z, front: GF + 0.75, sideH: CEIL + 0.3, sideRun: 2.0, frontRun: 1.7 });
  // top-hung timber shutter inside the aperture
  const p0 = [ap.a0 - 0.06, ap.y0 - 0.06, ZI - 0.045], p1 = [ap.a1 + 0.06, ap.y1 + 0.06, ZI - 0.005];
  hinged(A, {
    id: "firing_shutter", kind: "shutter", label: "shutter", sound: "shutter", axis: "x", openAngle: 85, duration: 0.8, apertureAlways: true,
    aperture: aabb([ap.a0, ap.y0, ZI - 0.1], [ap.a1, ap.y1, Z]), colliderMat: "plank_x",
    leaves: [{ hinge: [0, ap.y1 + 0.08, ZI - 0.045], want: [0, 1, -1], collider: [p0, p1],
      build: (T) => {
        A.box(p0, p1, "plank_x", T);
        A.box([ap.a0 - 0.04, ap.y0, p0[2] - 0.025], [ap.a1 + 0.04, ap.y0 + 0.07, p0[2]], "plank_x", T);
        A.box([(ap.a0 + ap.a1) / 2 - 0.05, ap.y0 - 0.03, p0[2] - 0.05], [(ap.a0 + ap.a1) / 2 + 0.05, ap.y0 + 0.0, p0[2]], "iron", T);
      } }],
  });
  bunkZ(A, "Two-tier bunk", 1.3, 2.05, -1.55, 0.4, GF, 2);
  stove(A, "Trench stove", -1.65, -1.15, GF, CEIL + 1.0);
  A.beginFurniture("Bench");
  A.box([-XI, GF + 0.42, -0.4], [-1.75, GF + 0.47, 1.1], "plank_z");
  for (const z of [-0.3, 1.0]) A.box([-2.0, GF, z - 0.04], [-1.8, GF + 0.42, z + 0.04], "plank_y");
  A.endFurniture();
  table(A, "Table", -1.2, 0.35, 0.8, 0.6, GF, 0.72, true);
  shelfX(A, "Shelf", -2.0, -0.6, ZI, -1, GF + 0.9, 0.8, 0.28, 2, "plank_x", null);
  A.beginFurniture("Lantern");
  A.box([-1.25, GF + 0.72, 0.3], [-1.15, GF + 0.74, 0.4], "iron");
  A.box([-1.24, GF + 0.74, 0.31], [-1.16, GF + 0.86, 0.39], "glass");
  A.endFurniture(false);
  const eye = GF + SLIT.eye, ac = (ap.a0 + ap.a1) / 2;
  A.walkthrough = {
    start: { pos: [0, 0, -3.8], yaw: 180 },
    steps: [
      { goto: [0, -2.6], note: "approach shelter door" },
      { interact: "shelter_door", aim: [0.2, 1.1, -1.63], expectOpen: true },
      { goto: [0.3, -0.3], expectY: GF, note: "enter shelter" },
      { goto: [ac, 0.6], expectY: GF, note: "at the aperture" },
      { interact: "firing_shutter", aim: [ac, eye, 1.58], expectOpen: true },
      { look: [ac, eye, 10], hold: 0.8 }, { shot: "16_shelter_aperture" },
      { look: [1.6, 0.9, -0.9], hold: 0.6 }, { shot: "17_shelter_bunks" },
      { goto: [0.3, -0.3] }, { goto: [0, -2.6] }, { goto: [0, -3.9], note: "leave shelter" },
    ],
  };
  return A;
}

// ----------------------------------------------------------------------------- command shelter
export function commandShelter() {
  const X = 4.7, Z = 3.7, T = 1.2, GF = 0.3, CEIL = GF + 2.3, ROOF = CEIL + 1.2, XI = X - T, ZI = Z - T;
  const A = new Asset({
    id: "bunker_command_shelter", version: 2, name: "Command Shelter", category: "Bunkers",
    description: "Free-standing two-room concrete command shelter, 9.4 x 7.4 m, 1.2 m walls under a heavy overhanging roof slab. Recessed steel entrance behind a blast wall, gas lock with a gas-tight inner door. Map room with radio desk and bunks; a stepped observation slit in the front and a steel-flapped loophole covering the entrance.",
    floors: 1, enterable: true, footprint: [12.4, 12.2], plinthDepth: 0.6, maxGrade: 0.6,
  });
  bunkerMaterials(A, CEIL);
  const s0 = GF + SLIT.sill, s1 = GF + SLIT.top;
  const lh = { a0: 1.25, a1: 1.25 + SLIT.w, y0: s0, y1: s1 };   // loophole throat (rear wall, covers the approach)
  const lhHole = slit(A, { axis: "x", outer: -Z, inner: -ZI, ...lh });
  const fs = slit(A, { axis: "x", outer: Z, inner: ZI, a0: 1.95, a1: 1.95 + SLIT.w, y0: s0, y1: s1 });   // front observation slit
  const ent = door(A, { id: "entrance_door", axis: "x", outer: -Z, inner: -ZI, c: -2.5, floor: GF, w: 1.0, h: 1.9, hingeSide: -1, steel: true, lintel: false, step: false, label: "steel door" });
  shell(A, { X, Z, T, GF, CEIL, front: [fs], back: [ent, lhHole] });
  footing(A, -X, X, -Z, Z);
  roofSlab(A, -X, X, -Z, Z, CEIL, ROOF, [0.45, 0.45, 0.45, 0.6]);
  const P = 0.15, W = 0.55;
  for (const sx of [-1, 1]) for (const sz of [-1, 1])
    pier(A, sx > 0 ? X - W : -X - P, sx > 0 ? X + P : -X + W, sz > 0 ? Z - W : -Z - P, sz > 0 ? Z + P : -Z + W, 0.1, CEIL);
  for (const x of [-2.6, 0.2]) pier(A, x - 0.3, x + 0.3, Z - 0.05, Z + 0.22, 0.1, CEIL - 0.05, 0.1);   // front pilasters
  // gas lock partition with a gas-tight steel door
  const gd = door(A, { id: "gaslock_door", axis: "z", outer: -1.5, inner: -1.2, c: -1.2, floor: GF, w: 1.0, h: 1.9, hingeSide: -1, steel: true, lintel: false, step: false, label: "gas-tight door" });
  partition(A, { axis: "z", d0: -1.5, d1: -1.2, a0: -ZI, a1: ZI, y0: GF, y1: CEIL, holes: [gd], m: "concrete_wash" });
  // loophole flap (top-hung, on the inner face over the throat)
  const p0 = [lh.a0 - 0.07, lh.y0 - 0.07, -ZI + 0.005], p1 = [lh.a1 + 0.07, lh.y1 + 0.07, -ZI + 0.045];
  hinged(A, {
    id: "loophole_flap", kind: "flap", label: "loophole flap", sound: "steel", axis: "x", openAngle: 80, duration: 1.1, apertureAlways: true,
    aperture: aabb([lh.a0, lh.y0, -Z], [lh.a1, lh.y1, -ZI + 0.06]),
    leaves: [{ hinge: [0, lh.y1 + 0.09, -ZI + 0.045], want: [0, 1, 1], collider: [p0, p1],
      build: plate(A, p0, p1, 1, 1, [[(lh.a0 + lh.a1) / 2 - 0.03, lh.y0 - 0.04, p1[2]], [(lh.a0 + lh.a1) / 2 + 0.03, lh.y0 + 0.02, p1[2] + 0.05]]) }],
  });
  // recessed entrance: wing piers and a hood, a step, then the blast wall and its return forming the protected passage
  pier(A, ent.a0 - 0.4, ent.a0, -Z - 0.45, -Z + 0.05, 0.1, GF + 2.2, 0.1);
  pier(A, ent.a1, ent.a1 + 0.4, -Z - 0.45, -Z + 0.05, 0.1, GF + 2.2, 0.1);
  A.rbox([ent.a0 - 0.45, GF + 2.0, -Z - 0.5], [ent.a1 + 0.45, GF + 2.4, -Z + 0.05], 0.1, "concrete");
  A.box([ent.a0, -0.2, -Z - 0.75], [ent.a1, 0.12, -Z - 0.28], "concrete");
  A.box([-4.2, -0.3, -Z - 2.2], [-1.0, 2.2, -Z - 1.7], "concrete");
  A.box([-X, -0.3, -Z - 2.2], [-4.2, 2.2, -Z - 0.3], "concrete");
  // map room
  table(A, "Map table", 1.0, 0.3, 1.6, 1.0, GF, 0.8);
  A.box([0.35, GF + 0.8, -0.1], [1.65, GF + 0.805, 0.7], "paper", { k: 0 });
  chair(A, "Chair", 0.6, -0.55, GF, "+z");
  chair(A, "Chair", 1.4, -0.55, GF, "+z");
  A.beginFurniture("Radio desk");
  A.box([-0.9, GF + 0.74, 1.95], [1.3, GF + 0.78, ZI], "plank_x");
  for (const x of [-0.85, 1.22]) A.box([x, GF, 2.0], [x + 0.06, GF + 0.74, ZI - 0.05], "plank_y");
  A.box([-0.7, GF + 0.78, 2.1], [0.0, GF + 1.15, ZI], "steel_plate");
  A.box([0.1, GF + 0.78, 2.15], [0.6, GF + 1.0, ZI], "steel_plate");
  for (let i = 0; i < 4; i++) A.box([-0.6 + i * 0.16, GF + 0.95, 2.09], [-0.56 + i * 0.16, GF + 0.99, 2.1], "steel_bare");
  A.box([0.8, GF + 0.78, 2.2], [1.1, GF + 0.8, 2.4], "paper");
  A.endFurniture();
  chair(A, "Radio chair", -0.3, 1.5, GF, "+z");
  bunkZ(A, "Bunks", 2.7, 3.45, -2.45, -0.5, GF, 2);
  A.beginFurniture("Steel cabinet");
  A.box([3.0, GF, 0.5], [XI, GF + 1.8, 1.5], "steel_plate");
  A.box([2.98, GF + 0.9, 0.95], [3.0, GF + 1.05, 1.05], "steel_bare");
  A.endFurniture();
  A.beginFurniture("Bench");
  A.box([-XI, GF + 0.42, -1.0], [-3.1, GF + 0.47, 1.5], "plank_z");
  for (const z of [-0.9, 1.4]) A.box([-3.4, GF, z - 0.04], [-3.15, GF + 0.42, z + 0.04], "plank_y");
  A.endFurniture();
  A.beginFurniture("Gas-mask hooks");
  A.box([-XI, GF + 1.6, -0.8], [-XI + 0.03, GF + 1.66, 1.2], "plank_z");
  for (const z of [-0.6, -0.1, 0.4, 0.9]) A.box([-XI + 0.03, GF + 1.35, z - 0.08], [-XI + 0.12, GF + 1.6, z + 0.08], "crate_olive");
  A.endFurniture(false);
  for (const [x, z] of [[1.0, 0.3], [-2.5, 0.0], [3.0, -1.5]]) lamp(A, x, CEIL, z);
  camoNet(A, { roof: [-X - 0.45, X + 0.45, -Z - 0.45, Z + 0.6, ROOF],
    hang: [["x+", -Z - 0.45, Z + 0.6, X + 0.45, ROOF, 0.5], ["x-", -Z - 0.45, Z + 0.6, -X - 0.45, ROOF, 0.5], ["z+", -X - 0.45, X + 0.45, Z + 0.6, ROOF + 0.02, ROOF - 0.5]] });
  const eye = GF + SLIT.eye, pz = -Z - 1.0;
  A.walkthrough = {
    start: { pos: [-0.2, 0, -8.0], yaw: 180 },
    steps: [
      { goto: [-0.4, pz], note: "enter protected entrance" },
      { goto: [-2.5, pz], note: "behind the blast wall" },
      { interact: "entrance_door", aim: [-2.3, GF + 1.1, -ZI - 0.08], expectOpen: true },
      { goto: [-2.5, -2.0] }, { goto: [-2.2, -1.0], expectY: GF, note: "gas lock" },
      { interact: "gaslock_door", aim: [-1.25, GF + 1.1, -1.0], expectOpen: true },
      { goto: [-1.8, -1.2] }, { goto: [-0.6, -1.2], expectY: GF, note: "through the gas-tight door" },
      { goto: [1.5, -1.65], expectY: GF, note: "at the loophole" },
      { interact: "loophole_flap", aim: [1.5, eye, -ZI + 0.05], expectOpen: true },
      { look: [1.5, eye, -14], hold: 0.8 }, { shot: "18_command_loophole" },
      { look: [1.0, 0.9, 1.6], hold: 0.6 }, { shot: "19_command_map_room" },
      { goto: [2.4, -0.4] }, { goto: [2.2, 1.85], expectY: GF, note: "at the front slit" },
      { look: [2.2, eye, 16], hold: 0.8 }, { shot: "19b_command_front_slit" },
      { goto: [2.4, -0.4] }, { goto: [-0.6, -1.2] }, { goto: [-1.8, -1.2] }, { goto: [-2.5, -2.0] }, { goto: [-2.5, pz] },
      { goto: [-0.4, pz] }, { goto: [-0.2, -7.6], note: "leave command shelter" },
    ],
  };
  return A;
}

// ----------------------------------------------------------------------------- large casemate
// Atlantic Wall gun casemate: a tall rear main block (crew room) with a heavy overhanging roof slab, and a lower
// forward casemate (firing room) with a pillowed nose, a deep hood and a wide stepped embrasure in the shadowed recess
// under it, on a rounded plinth. A half-buried concrete dome on the east flank, gentle sand drifts. Rounded parts are
// static; the box walls (main block, casemate flanks, embrasure wall, blast wall) take impact-centred damage.
export function largeBunker() {
  const GF = 0.5, eye = GF + SLIT.eye, pt = 0.02, MAIN_TOP = 4.9, HOOD = 2.95, HOOD_TOP = 3.85;
  const A = new Asset({
    id: "bunker_large_casemate", version: 1, name: "Large Bunker (Casemate)", category: "Bunkers",
    description: "Large Atlantic Wall gun casemate, about 18 x 12 m and 6 m tall. A tall main block with a heavy overhanging roof slab holds the crew room; the lower forward casemate has a pillowed nose, a deep overhanging hood and a 4.4 m stepped embrasure in shadow under it. Rear steel entrance between wing piers behind a blast wall, half-buried dome on the flank, sand drifted against the walls.",
    floors: 1, enterable: true, footprint: [24.0, 17.0], plinthDepth: 0.6, maxGrade: 0.6,
  });
  bunkerMaterials(A, MAIN_TOP);
  A.mat("concrete_hood", [0.27, 0.26, 0.235], 0.93, "bunker", { grain: Math.round(HOOD * 10) });
  const m = { stone: "concrete", plaster: "concrete_wash", pt };
  // ---- main block: x -2.5..9.0, z -6.0..-0.3, 1.2 m walls; crew room x -1.3..7.8, z -4.8..-1.5
  A.box([-2.5, -0.6, -6.0], [9.0, GF - 0.02, -0.3], "concrete");
  A.box([-1.3, GF - 0.02, -4.8], [7.8, GF, -1.5], "concrete_floor");
  const rd = door(A, { id: "bunker_door", axis: "x", outer: -6.0, inner: -4.8, c: 3.0, floor: GF, w: 1.0, h: 2.0, hingeSide: -1, steel: true, lintel: false, step: false, label: "steel door" });
  const idr = door(A, { id: "casemate_inner_door", axis: "x", outer: -0.3, inner: -1.5, c: -0.25, floor: GF, w: 1.0, h: 1.9, hingeSide: -1, steel: true, lintel: false, step: false, label: "steel door" });
  const pn = (side) => ({ side, family: "concrete", bands: [[GF - 0.02, MAIN_TOP, 0, GF]], collar: 0.3, minW: 1.1 });
  wall(A, { axis: "x", outer: -0.3, inner: -1.5, aOut: [-2.5, 9.0], aIn: [-1.3, 7.8], y0: GF - 0.02, y1: MAIN_TOP, holes: [idr], ...m, panel: pn("front") });
  wall(A, { axis: "x", outer: -6.0, inner: -4.8, aOut: [-2.5, 9.0], aIn: [-1.3, 7.8], y0: GF - 0.02, y1: MAIN_TOP, holes: [rd], ...m, panel: pn("rear") });
  wall(A, { axis: "z", outer: 9.0, inner: 7.8, aOut: [-4.8 - pt, -1.5 + pt], aIn: [-4.8, -1.5], y0: GF - 0.02, y1: MAIN_TOP, holes: [], ...m, panel: pn("east") });
  wall(A, { axis: "z", outer: -2.5, inner: -1.3, aOut: [-4.8 - pt, -1.5 + pt], aIn: [-4.8, -1.5], y0: GF - 0.02, y1: MAIN_TOP, holes: [], ...m, panel: pn("west") });
  A.box([-1.3, GF + 2.6, -4.8], [7.8, MAIN_TOP, -1.5], "concrete");   // the roof mass over the crew room
  A.rbox([-3.3, MAIN_TOP, -6.4], [9.8, 6.0, 0.5], 0.4, "concrete");     // heavy roof slab: 0.8 m overhang front and flanks
  A.rbox([-2.8, -0.6, -6.3], [9.3, 0.35, 0.0], 0.12, "concrete");      // footing
  for (const [x, z] of [[9.0, -6.0], [-2.5, -6.0], [9.0, -0.3]])        // rounded corner piers
    pier(A, x > 0 ? x - 0.6 : x - 0.15, x > 0 ? x + 0.15 : x + 0.6, z < -1 ? z - 0.15 : z - 0.6, z < -1 ? z + 0.6 : z + 0.15, 0.1, MAIN_TOP);
  for (const x of [-0.4, 5.95, 7.6]) pier(A, x - 0.35, x + 0.35, -6.25, -5.9, 0.1, MAIN_TOP - 0.1, 0.1);   // rear pilasters
  pier(A, 8.9, 9.22, -3.5, -2.8, 0.1, MAIN_TOP - 0.1, 0.1);                                           // flank pilaster
  // recessed rear entrance: wing piers, hood, steps; blast wall in front
  pier(A, rd.a0 - 0.45, rd.a0, -6.75, -5.95, 0.1, GF + 2.35, 0.1);
  pier(A, rd.a1, rd.a1 + 0.45, -6.75, -5.95, 0.1, GF + 2.35, 0.1);
  A.rbox([rd.a0 - 0.5, GF + 2.1, -6.8], [rd.a1 + 0.5, GF + 2.6, -5.95], 0.12, "concrete");
  A.box([rd.a0, -0.2, -6.95], [rd.a1, 0.18, -6.3], "concrete");
  A.box([1.2, -0.3, -9.6], [4.8, 2.4, -9.0], "concrete");
  // ---- forward casemate: x -8..2, z -1.5..4.6; firing room x -6.8..0.8, z -0.3..2.9, ceiling = hood underside
  A.box([-8.0, -0.6, -1.5], [2.0, GF - 0.02, 4.6], "concrete");
  A.box([-6.8, GF - 0.02, -0.3], [0.8, GF, 2.9], "concrete_floor");
  wall(A, { axis: "z", outer: -8.0, inner: -6.8, aOut: [-1.5, 2.9], aIn: [-0.3, 2.9], y0: GF - 0.02, y1: HOOD, holes: [], ...m });
  wall(A, { axis: "z", outer: 2.0, inner: 0.8, aOut: [-0.3, 2.9], aIn: [-0.3, 2.9], y0: GF - 0.02, y1: HOOD, holes: [], ...m });
  wall(A, { axis: "x", outer: -1.5, inner: -0.3, aOut: [-6.8, -2.5], aIn: [-6.8, -2.5], y0: GF - 0.02, y1: HOOD, holes: [], ...m });
  // front: rounded plinth, pillowed lower nose, the embrasure wall at the back of the recess, cheeks, and the hood
  A.rbox([-8.4, -0.6, 3.0], [2.4, 1.0, 5.0], 0.5, "concrete");
  A.rbox([-8.4, -0.6, -1.5], [-7.4, 1.0, 5.0], 0.45, "concrete");
  A.rbox([1.4, -0.6, -0.3], [2.4, 1.0, 5.0], 0.45, "concrete");
  A.rbox([-8.4, -0.6, -1.9], [-2.5, 0.6, -1.0], 0.3, "concrete");
  A.rbox([-8.0, 0.6, 2.9], [2.0, 1.8, 4.6], 0.55, "concrete_hood");
  A.box([-6.8, GF - 0.02, 2.9], [0.8, 1.8, 3.45], "concrete_wash");   // inner face of the nose
  const emb = slit(A, { axis: "x", outer: 3.5, inner: 2.9, a0: -5.4, a1: -1.0, y0: GF + SLIT.sill, y1: GF + SLIT.top, n: 2 });
  wall(A, { axis: "x", outer: 3.5, inner: 2.9, aOut: [-7.2, 1.2], aIn: [-7.2, 1.2], y0: 1.8, y1: HOOD, holes: [emb], ...m });
  A.rbox([-7.95, 1.62, 2.9], [-7.2, 3.0, 4.45], 0.2, "concrete_hood");   // cheeks closing the recess ends
  A.rbox([1.2, 1.62, 2.9], [1.95, 3.0, 4.45], 0.2, "concrete_hood");
  A.rbox([-8.5, HOOD, -1.5], [2.4, HOOD_TOP, 5.7], 0.45, "concrete_hood");   // hood: 1.1 m forward of the nose
  // ---- half-buried dome on the east flank and drifted sand (gentle, walkable)
  A.rbox([3.6, -1.0, -0.3], [8.6, 1.7, 4.8], 1.3, "concrete_hood");
  drift(A, 2, -6.0, -0.3, 9.0, 1, 0.7, 3.0);
  drift(A, 2, -1.5, 4.6, -8.4, -1, 0.85, 2.8);
  drift(A, 0, -2.8, 1.3, -6.3, -1, 0.55, 2.3);
  drift(A, 2, -0.3, 4.8, 8.6, 1, 0.45, 1.8);
  drift(A, 0, -8.4, 2.4, 5.0, 1, 0.42, 1.8);
  camoNet(A, { roof: [-3.3, 9.8, -6.4, 0.5, 6.0],
    hang: [["x+", -6.4, 0.5, 9.8, 6.0, 0.9], ["z-", 4.4, 9.8, -6.4, 6.0, 0.9], ["z+", 2.4, 9.8, 0.5, 6.02, 5.5]] });
  A.obox([-3.05, HOOD_TOP + 0.05, 2.1], [10.6, 0.035, 7.0], [0.8, 0, -0.5], "camo_net", { k: 0 });   // over the hood
  // ---- interiors
  crates(A, "Ammunition", [[-6.3, GF, -0.05, 0.45, 0.22, 0.28], [-6.3, GF + 0.22, -0.05, 0.45, 0.22, 0.28], [-6.3, GF, 0.35, 0.45, 0.22, 0.28], [-5.7, GF, -0.05, 0.45, 0.22, 0.28, 6]], "crate_olive");
  shelfZ(A, "Shell rack", 1.0, 2.6, -6.8, 1, GF, 1.6, 0.4, 3, "plank_z", (y, xa, xx, i) => {
    if (i > 0) for (let z = 1.2; z < 2.5; z += 0.45) crateBox(A, (xa + xx) / 2, y, z, 0.3, 0.2, 0.38, "crate_olive");
  });
  A.beginFurniture("Bench");
  A.box([-5.5, GF + 0.42, -0.3], [-3.5, GF + 0.47, 0.12], "plank_x");
  for (const x of [-5.35, -3.65]) A.box([x - 0.04, GF, -0.25], [x + 0.04, GF + 0.42, 0.07], "plank_y");
  A.endFurniture();
  bunkZ(A, "Bunks", 7.0, 7.75, -4.6, -2.0, GF, 2);
  table(A, "Table", 4.4, -2.3, 1.2, 0.7, GF, 0.76);
  chair(A, "Chair", 4.4, -3.0, GF, "+z");
  shelfX(A, "Shelf", -1.1, 1.5, -4.8, 1, GF, 1.8, 0.35, 3, "plank_x", null);
  for (const [x, z, y] of [[-4.5, 1.0, HOOD], [-1.5, 1.0, HOOD], [1.0, -3.2, GF + 2.6], [5.0, -3.2, GF + 2.6]]) lamp(A, x, y, z);
  A.walkthrough = {
    start: { pos: [6.5, 0, -11.0], yaw: 180 },
    steps: [
      { goto: [6.5, -8.0], note: "approach the rear" },
      { goto: [3.0, -7.8], note: "behind the blast wall" },
      { goto: [3.0, -6.2] },
      { interact: "bunker_door", aim: [3.2, GF + 1.1, -4.86], expectOpen: true },
      { goto: [3.0, -3.6], expectY: GF, note: "crew room" },
      { goto: [0.3, -2.6] },
      { interact: "casemate_inner_door", aim: [-0.25, GF + 1.1, -1.42], expectOpen: true },
      { goto: [-0.2, -2.1] }, { goto: [-0.25, 0.4], expectY: GF, note: "firing room" },
      { goto: [-3.2, 2.35], expectY: GF, note: "at the embrasure" },
      { look: [-3.2, eye, 20], hold: 0.8 }, { shot: "40_large_embrasure" },
      { look: [-1.0, 0.9, -0.3], hold: 0.6 }, { shot: "41_large_firing_room" },
      { goto: [-0.25, 0.4] }, { goto: [-0.2, -2.1] }, { goto: [3.0, -3.6] }, { goto: [3.0, -6.2] }, { goto: [3.0, -7.8] },
      { goto: [6.5, -8.0], note: "leave the casemate" },
    ],
  };
  return A;
}

// ----------------------------------------------------------------------------- MG 42 embrasure mount (crew station)
// Steel pedestal mount for the MG 42 behind a slit. The supplied MG 42 GLB is drawn in its cradle (src/app.cpp); the
// mount is the only geometry here. Bore and sight line sit in the shared slit band (SLIT) for a standing gunner.
export function mg42Mount() {
  const A = new Asset({
    id: "gun_mg42_embrasure", version: 1, name: "MG 42 on embrasure mount", category: "Field guns",
    description: "MG 42 on a steel pedestal (embrasure) mount, crewed standing from the grips: 55 deg either side, -5/+7 deg (the slit band limits it 0.2 m behind a 1.2 m wall), 50-round belts with 250 in reserve. Placed with the MG casemate behind its slit; stands behind any slit at the shared slit height.",
    floors: 0, enterable: false, footprint: [0.8, 1.6], plinthDepth: 0, maxGrade: 0.4,
  });
  stdMaterials(A);
  A.meta.faction = "DE";
  const YP = [0, 1.5, 0], EP = [0, SLIT.eye - 0.036, 0];   // sight line 3.6 cm above the bore, at the standing eye
  A.box([-0.3, 0, -0.3], [0.3, 0.035, 0.3], "steel_plate", { k: 0 });   // base plate
  A.box([-0.055, 0.035, -0.055], [0.055, YP[1] - 0.06, 0.055], "steel_plate", { k: 0 });   // pedestal post
  for (const s of [-1, 1]) {   // gussets
    A.box([s > 0 ? 0.055 : -0.22, 0.035, -0.012], [s > 0 ? 0.22 : -0.055, 0.22, 0.012], "steel_plate", { k: 0 });
    A.box([-0.012, 0.035, s > 0 ? 0.055 : -0.22], [0.012, 0.22, s > 0 ? 0.22 : -0.055], "steel_plate", { k: 0 });
  }
  A.box([-0.17, 0, -0.17], [0.17, YP[1] - 0.1, 0.17], "steel_plate", { v: 0 });   // the player cannot walk through the post
  const Y = { into: [], k: 0 }, E = { into: [], k: 0 };
  A.box([-0.07, YP[1] - 0.06, -0.08], [0.07, YP[1] + 0.02, 0.08], "steel_bare", Y);   // swivel head
  for (const s of [-1, 1]) A.box([s * 0.05 - 0.012, YP[1], -0.06], [s * 0.05 + 0.012, EP[1] - 0.04, 0.06], "steel_plate", Y);   // fork
  A.box([-0.035, EP[1] - 0.095, -0.14], [0.035, EP[1] - 0.07, 0.16], "steel_bare", E);   // cradle under the receiver
  const rel = (parts, p) => parts.map((q) => ({ ...q, c: [q.c[0] - p[0], q.c[1] - p[1], q.c[2] - p[2]] }));
  A.meta.station = {
    kind: "mg42", yawPivot: YP, elevPivot: EP, muzzle: [0, EP[1], 0.77], optic: [0, EP[1] + 0.036, -0.2],
    gunner: [0, 0, -0.85], handler: [0, 0, -0.85], traverse: [-55, 55], elevation: [-5, 7], traverseRate: 90, elevateRate: 60,
    recoil: 0.015, reloadTime: 4.6, loaded: 50, reserve: 250, ammo: "792_belt50", moveHalf: [0.3, 0.8, 0.3], serviceRadius: 4,
    yawParts: rel(Y.into, YP), elevParts: rel(E.into, EP), recoilParts: [],
  };
  return A;
}

// ----------------------------------------------------------------------------- PaK casemate (R612 type)
// Regelbau-612-style gun casemate: a 1.5 m front with one wide stepped gun embrasure, a gun room sized for the PaK 40
// (placed with it, plus an ammunition crate), a commander's slit at the shared slit height, and double steel gun doors
// in the rear through which the gun is run in. The floor is flush with the ground so the gun stands level.
export function pakCasemate() {
  const GF = 0.02, CEIL = 2.7, ROOF = CEIL + 1.2, XO = 4.2, XI = 3.0, ZF = 3.5, ZFI = 2.0, ZB = -4.2, ZBI = -3.0, pt = 0.02;
  const A = new Asset({
    id: "bunker_pak_casemate", version: 1, name: "PaK Casemate (R612 type)", category: "Bunkers",
    description: "Reinforced-concrete gun casemate after the Regelbau 612: 8.4 x 7.7 m, 1.5 m front with a wide stepped embrasure, gun room for the PaK 40 (placed with the casemate, with an ammunition crate), commander's slit, double steel gun doors at the rear. Camouflage netting over the roof.",
    floors: 1, enterable: true, footprint: [11.0, 11.0], plinthDepth: 0.6, maxGrade: 0.4,
  });
  bunkerMaterials(A, CEIL);
  // gun embrasure: trunnion 0.93 m behind the inner face, bore 0.95 m. Each 0.5 m band is sized for the bore at its
  // far end with traverse and elevation combined (path factor 1 / (cos e cos y)): +-32.5 deg, +22 / -5 deg, 6 cm spare
  const gun = slit(A, { axis: "x", outer: ZF, inner: ZFI, a0: -1.0, a1: 1.0, y0: 0.62, y1: 1.7, step: { da: 0.33, dyT: 0.24, dyB: 0 } });
  const cmd = slit(A, { axis: "x", outer: ZF, inner: ZFI, a0: 2.05, a1: 2.05 + SLIT.w, y0: GF + SLIT.sill, y1: GF + SLIT.top });
  const gd = door(A, { id: "gun_doors", axis: "x", outer: ZB, inner: ZBI, c: 0, floor: GF, w: 2.6, h: 2.2, hingeSide: -1, double: true, steel: true, lintel: false, step: false, label: "gun doors", openAngle: 172 });   // leaves fold flat against the rear wall
  A.box([-XO, -0.6, ZB], [XO, GF - 0.02, ZF], "concrete");
  A.box([-XI, GF - 0.02, ZBI], [XI, GF, ZFI], "concrete_floor");
  const m = { stone: "concrete", plaster: "concrete_wash", pt };
  const pn = (side) => ({ side, family: "concrete", bands: [[0, CEIL, 0, GF]], collar: 0.3, minW: 1.1 });
  wall(A, { axis: "x", outer: ZF, inner: ZFI, aOut: [-XO, XO], aIn: [-XI, XI], y0: 0, y1: CEIL, holes: [gun, cmd], ...m, panel: pn("front") });
  wall(A, { axis: "x", outer: ZB, inner: ZBI, aOut: [-XO, XO], aIn: [-XI, XI], y0: 0, y1: CEIL, holes: [gd], ...m, panel: pn("rear") });
  wall(A, { axis: "z", outer: XO, inner: XI, aOut: [ZBI - pt, ZFI + pt], aIn: [ZBI, ZFI], y0: 0, y1: CEIL, holes: [], ...m, panel: pn("east") });
  wall(A, { axis: "z", outer: -XO, inner: -XI, aOut: [ZBI - pt, ZFI + pt], aIn: [ZBI, ZFI], y0: 0, y1: CEIL, holes: [], ...m, panel: pn("west") });
  roofSlab(A, -XO, XO, ZB, ZF, CEIL, ROOF, [0.5, 0.5, 0.3, 0.8]);
  // footing step on three sides and beside the gun doors (the gun runs in level over the threshold)
  A.rbox([-XO - 0.28, -0.6, ZF - 0.4], [XO + 0.28, 0.22, ZF + 0.28], 0.1, "concrete");
  for (const s of [-1, 1]) {
    A.rbox([s > 0 ? XO - 0.4 : -XO - 0.28, -0.6, ZB - 0.28], [s > 0 ? XO + 0.28 : -XO + 0.4, 0.22, ZF], 0.1, "concrete");
    A.rbox([s > 0 ? 1.75 : -XO - 0.28, -0.6, ZB - 0.28], [s > 0 ? XO + 0.28 : -1.75, 0.22, ZB + 0.4], 0.1, "concrete");
  }
  const P = 0.15, W = 0.55;
  for (const sx of [-1, 1]) for (const sz of [-1, 1])
    pier(A, sx > 0 ? XO - W : -XO - P, sx > 0 ? XO + P : -XO + W, sz > 0 ? ZF - W : ZB - P, sz > 0 ? ZF + P : ZB + W, 0.1, CEIL);
  for (const s of [-1, 1]) {
    pier(A, s > 0 ? 1.3 : -1.75, s > 0 ? 1.75 : -1.3, ZB - 0.6, ZB + 0.05, 0.1, GF + 2.45, 0.1);   // wing piers
    pier(A, s > 0 ? XO - 0.05 : -XO - 0.27, s > 0 ? XO + 0.27 : -XO + 0.05, -1.0, -0.3, 0.1, CEIL - 0.1, 0.1);   // flank pilasters
  }
  A.rbox([-1.8, GF + 2.25, ZB - 0.65], [1.8, GF + 2.7, ZB + 0.05], 0.12, "concrete");   // hood over the gun doors
  // interior: shell racks along the east wall, a bench; the gun and a PaK 40 HE crate come with the casemate
  A.meta.companions = [{ asset: "gun_pak40", pos: [0, 0, 0.95], yaw: 0 }, { asset: "crate_pak40_he", pos: [-2.35, 0, -1.6], yaw: 90 }];
  shelfZ(A, "Shell rack", -2.6, -0.6, XI, -1, GF, 1.6, 0.4, 3, "plank_z", (y, xa, xx, i) => {
    if (i > 0) for (let z = -2.4; z < -0.8; z += 0.42) crateBox(A, (xa + xx) / 2, y, z, 0.3, 0.18, 0.36, "crate_olive");
  });
  A.beginFurniture("Bench");
  A.box([-XI, GF + 0.42, -0.6], [-2.6, GF + 0.47, 1.4], "plank_z");
  for (const z of [-0.45, 1.25]) A.box([-2.95, GF, z - 0.04], [-2.65, GF + 0.42, z + 0.04], "plank_y");
  A.endFurniture();
  for (const [x, z] of [[-1.5, 0.0], [1.5, -1.5]]) lamp(A, x, CEIL, z);
  camoNet(A, { roof: [-XO - 0.5, XO + 0.5, ZB - 0.3, ZF + 0.8, ROOF],
    hang: [["x+", ZB - 0.3, ZF + 0.8, XO + 0.5, ROOF, 0.5], ["x-", ZB - 0.3, ZF + 0.8, -XO - 0.5, ROOF, 0.5], ["z+", -XO - 0.5, XO + 0.5, ZF + 0.8, ROOF + 0.02, ROOF - 0.45]] });
  const eye = GF + SLIT.eye;
  A.walkthrough = {
    start: { pos: [2.0, 0, -8.0], yaw: 180 },
    steps: [
      { goto: [1.0, -5.3], note: "approach the gun doors" },
      { interact: "gun_doors", aim: [0.65, GF + 1.1, ZBI - 0.08], expectOpen: true },
      { goto: [0.55, -2.75], expectY: GF, note: "through the gun doors, behind the trail" },
      { goto: [2.2, -2.55] }, { goto: [2.3, 1.4], expectY: GF, note: "at the commander's slit" },
      { look: [2.3, eye, 16], hold: 0.8 }, { shot: "42_pak_casemate_slit" },
      { look: [0, 0.9, 0.8], hold: 0.6 }, { shot: "43_pak_casemate_gun_room" },
      { goto: [2.2, -2.55] }, { goto: [0.55, -2.75] }, { goto: [0.8, -5.3] }, { goto: [2.0, -7.6], note: "leave the casemate" },
    ],
  };
  return A;
}
