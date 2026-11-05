// Bunkers: machine-gun casemate, observation post, field shelter, command shelter.
// Floors sit just above grade (the terrain is not excavated); earth berms bank the sides.
import { Asset } from "./kit.mjs";
import { stdMaterials, wall, partition, door, hinged, aabb, chair, table } from "./parts.mjs";
import { shelfX, shelfZ, crates, bunkZ, stove, stool, lamp, crateBox } from "./furniture2.mjs";

function concreteShell(A, { X, Z, T, GF, CEIL, ROOF, front = [], back = [], east = [], west = [], ov = 0.1 }) {
  const XI = X - T, ZI = Z - T, y0 = GF - 0.02, pt = 0.02;
  A.box([-X, -0.6, -Z], [X, y0, Z], "concrete");
  A.box([-XI, y0, -ZI], [XI, GF, ZI], "concrete_floor");
  const m = { stone: "concrete", plaster: "concrete_wash", pt };
  const pn = (side) => ({ side, family: "concrete", bands: [[y0, CEIL, 0, GF]], collar: 0.3, minW: 1.1 });
  wall(A, { axis: "x", outer: Z, inner: ZI, aOut: [-X, X], aIn: [-XI, XI], y0, y1: CEIL, holes: front, ...m, panel: pn("front") });
  wall(A, { axis: "x", outer: -Z, inner: -ZI, aOut: [-X, X], aIn: [-XI, XI], y0, y1: CEIL, holes: back, ...m, panel: pn("rear") });
  wall(A, { axis: "z", outer: X, inner: XI, aOut: [-ZI - pt, ZI + pt], aIn: [-ZI, ZI], y0, y1: CEIL, holes: east, ...m, panel: pn("east") });
  wall(A, { axis: "z", outer: -X, inner: -XI, aOut: [-ZI - pt, ZI + pt], aIn: [-ZI, ZI], y0, y1: CEIL, holes: west, ...m, panel: pn("west") });
  A.box([-X - ov, CEIL, -Z - ov], [X + ov, ROOF, Z + ov], "concrete");
  return { XI, ZI };
}
function berms(A, { X, Z, ROOFY, front, sideH, sideRun = 3.0, frontRun = 2.5, zr = null }) {
  const [z0, z1] = zr || [-Z, Z];
  for (const s of [-1, 1]) A.prism(2, z0, z1, [[s * X, -0.3], [s * X, sideH], [s * (X + sideRun), -0.3]], "turf");
  if (front != null) A.prism(0, -X, X, [[Z, -0.3], [Z, front], [Z + frontRun, -0.3]], "turf");
  void ROOFY;
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
export function mgCasemate() {
  const A = new Asset({
    id: "bunker_mg_casemate", version: 1, name: "Machine-Gun Casemate", category: "Bunkers",
    description: "Reinforced-concrete casemate with a wide embrasure closed by a two-leaf steel shutter. Entry through a steel door behind a blast wall; earth banked against the flanks. MG table, ammunition and a bench inside.",
    floors: 1, enterable: true, footprint: [12.4, 11.2], plinthDepth: 0.6, maxGrade: 0.6,
  });
  stdMaterials(A);
  const X = 3, Z = 3, T = 1.0, GF = 0.2, CEIL = 2.4, ROOF = 3.4, XI = X - T, ZI = Z - T;
  const emb = { a0: -0.7, a1: 0.7, y0: GF + 1.05, y1: GF + 1.5 };
  const rear = door(A, { id: "casemate_door", axis: "x", outer: -Z, inner: -ZI, c: -1.0, floor: GF, w: 1.0, h: 1.9, hingeSide: -1, steel: true, lintel: false, step: false, label: "steel door" });
  concreteShell(A, { X, Z, T, GF, CEIL, ROOF, front: [emb], back: [rear] });
  // steel embrasure shutter: two leaves on the interior face, opening inward
  const sy0 = emb.y0 - 0.06, sy1 = emb.y1 + 0.06, d0 = ZI - 0.05, d1 = ZI - 0.008;
  hinged(A, {
    id: "embrasure_shutter", kind: "shutter", label: "steel shutter", sound: "steel", axis: "y", openAngle: 100, duration: 1.3,
    aperture: aabb([emb.a0, emb.y0, ZI - 0.1], [emb.a1, emb.y1, Z]),
    leaves: [-1, 1].map((s) => {
      const outerA = s * (emb.a1 + 0.06);
      const p0 = [Math.min(outerA, 0), sy0, d0], p1 = [Math.max(outerA, 0), sy1, d1];
      return { hinge: [outerA, 0, d0], want: [0, 0, -1], collider: [p0, p1],
        build: plate(A, p0, p1, 0, 1, [[s * 0.12 - 0.02, (sy0 + sy1) / 2 - 0.02, d0 - 0.05], [s * 0.12 + 0.02, (sy0 + sy1) / 2 + 0.02, d0]]) };
    }),
  });
  // blast wall + return forming the protected entrance passage
  A.box([-2.4, -0.3, -4.4], [0.6, 2.2, -3.9], "concrete");
  A.box([-X, -0.3, -4.4], [-2.4, 2.2, -Z], "concrete");
  A.box([-2.4, -0.25, -3.9], [1.0, 0.05, -Z], "concrete_floor");
  berms(A, { X, Z, front: GF + 0.8, sideH: ROOF - 0.2, sideRun: 3.2, frontRun: 2.6 });
  // interior
  A.beginFurniture("MG table");
  A.box([-0.6, GF, 1.35], [0.6, GF + 0.9, ZI], "concrete");
  A.box([-0.65, GF + 0.9, 1.3], [0.65, GF + 0.96, ZI], "oak_x");
  A.box([-0.04, GF + 0.96, 1.7], [0.04, GF + 1.12, 1.78], "steel_bare");
  A.endFurniture();
  crates(A, "Ammunition boxes", [[-1.3, GF, 1.6, 0.45, 0.22, 0.28], [-1.3, GF + 0.22, 1.6, 0.45, 0.22, 0.28], [-1.3, GF, 1.2, 0.45, 0.22, 0.28, 8]], "crate_olive");
  A.beginFurniture("Bench");
  A.box([-XI, GF + 0.42, -1.0], [-1.62, GF + 0.47, 1.0], "plank_z");
  for (const z of [-0.85, 0.85]) A.box([-1.95, GF, z - 0.04], [-1.68, GF + 0.42, z + 0.04], "plank_y");
  A.endFurniture();
  shelfZ(A, "Shelf", -1.5, 0.5, XI, -1, GF + 0.0, 1.8, 0.35, 3, "plank_z", (y, xa, xx, i) => {
    if (i > 0) for (let z = -1.3; z < 0.4; z += 0.5) crateBox(A, (xa + xx) / 2, y, z, 0.28, 0.2, 0.4, "crate_olive");
  });
  A.box([-1.5, CEIL - 0.25, -0.1], [1.5, CEIL - 0.1, 0.05], "steel_plate", { k: 0 }); // ventilation duct
  lamp(A, 0.4, CEIL, -0.6);
  A.walkthrough = {
    start: { pos: [2.2, 0, -6.2], yaw: 180 },
    steps: [
      { goto: [1.6, -3.45], note: "enter protected passage" },
      { goto: [-1.0, -3.45], note: "behind the blast wall" },
      { interact: "casemate_door", aim: [-0.8, 1.1, -2.04], expectOpen: true },
      { goto: [-0.9, -0.8], expectY: GF, note: "enter casemate" },
      { goto: [0, 0.9], expectY: GF, note: "at the embrasure" },
      { interact: "embrasure_shutter", aim: [0.35, 1.47, 1.97], expectOpen: true },
      { look: [0, 1.45, 14], hold: 0.8 }, { shot: "13_casemate_embrasure" },
      { look: [-1.6, 0.8, -1.6], hold: 0.6 }, { shot: "14_casemate_interior" },
      { goto: [-0.9, -0.8] }, { goto: [-1.0, -3.45] }, { goto: [1.6, -3.45] }, { goto: [2.2, -5.8], note: "leave casemate" },
    ],
  };
  return A;
}

// ----------------------------------------------------------------------------- observation post
export function observationPost() {
  const A = new Asset({
    id: "bunker_observation_post", version: 1, name: "Observation Post", category: "Bunkers",
    description: "Small concrete observation post with a narrow eye-level slit closed by a top-hung armoured flap, operated from inside. Map table, field telephone and stool.",
    floors: 1, enterable: true, footprint: [10.0, 8.9], plinthDepth: 0.6, maxGrade: 0.6,
  });
  stdMaterials(A);
  const X = 2.25, Z = 2.25, T = 0.8, GF = 0.2, CEIL = 2.3, ROOF = 3.1, XI = X - T, ZI = Z - T;
  const slit = { a0: -0.6, a1: 0.6, y0: GF + 1.42, y1: GF + 1.67 };
  const rear = door(A, { id: "op_door", axis: "x", outer: -Z, inner: -ZI, c: 0.6, floor: GF, w: 1.0, h: 1.9, hingeSide: 1, steel: true, lintel: false, step: false, label: "steel door" });
  concreteShell(A, { X, Z, T, GF, CEIL, ROOF, front: [slit], back: [rear] });
  A.box([slit.a0, slit.y0 - 0.12, ZI - 0.25], [slit.a1, slit.y0, ZI], "concrete");   // elbow ledge under the slit
  // armoured flap on the exterior face, hinged along its top edge, lifts outward
  const fy0 = slit.y0 - 0.06, fy1 = slit.y1 + 0.08, fz0 = Z + 0.004, fz1 = Z + 0.045;
  const p0 = [slit.a0 - 0.1, fy0, fz0], p1 = [slit.a1 + 0.1, fy1, fz1];
  hinged(A, {
    id: "viewing_flap", kind: "flap", label: "viewing flap", sound: "steel", axis: "x", openAngle: 78, duration: 1.4, apertureAlways: true,
    aperture: aabb([slit.a0, slit.y0, ZI], [slit.a1, slit.y1, Z + 0.05]),
    leaves: [{ hinge: [0, fy1 + 0.02, fz1 + 0.01], want: [0, 1, 1], collider: [p0, p1],
      build: (T) => { plate(A, p0, p1, 0, 2)(T); for (const x of [slit.a0 + 0.05, slit.a1 - 0.15]) A.box([x, fy1 - 0.02, fz1], [x + 0.1, fy1 + 0.04, fz1 + 0.03], "steel_bare", T); } }],
  });
  for (const x of [slit.a0 - 0.05, slit.a1 - 0.05]) A.box([x, fy1 + 0.0, Z], [x + 0.1, fy1 + 0.06, Z + 0.05], "steel_bare", { k: 0 });
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
  A.walkthrough = {
    start: { pos: [0.6, 0, -4.2], yaw: 180 },
    steps: [
      { goto: [0.6, -3.0], note: "approach observation post" },
      { interact: "op_door", aim: [0.4, 1.1, -1.49], expectOpen: true },
      { goto: [0.4, -0.3], expectY: GF, note: "enter observation post" },
      { goto: [0, 0.6], expectY: GF, note: "at the slit" },
      { look: [0, 1.75, 1.7], hold: 0.4 }, { shot: "15a_op_slit_closed" },
      { interact: "viewing_flap", aim: [0, 1.75, 1.7], expectOpen: true },
      { look: [0, 1.6, 16], hold: 1.0 }, { shot: "15b_op_flap_view" },
      { interact: "viewing_flap", aim: [0, 1.75, 1.7], expectOpen: false, note: "close the flap from inside" },
      { goto: [0.4, -0.3] }, { goto: [0.6, -3.0] }, { goto: [0.6, -4.3], note: "leave observation post" },
    ],
  };
  return A;
}

// ----------------------------------------------------------------------------- field shelter
export function fieldShelter() {
  const A = new Asset({
    id: "bunker_field_shelter", version: 1, name: "Field Shelter", category: "Bunkers",
    description: "Log-built field shelter (Unterstand) under an earth cover. Plank door, a small aperture with a top-hung timber shutter, two-tier bunks, stove and bench.",
    floors: 1, enterable: true, footprint: [9.2, 7.4], plinthDepth: 0.5, maxGrade: 0.6,
  });
  stdMaterials(A);
  const X = 2.5, Z = 2.0, T = 0.4, GF = 0.15, CEIL = 2.3, XI = X - T, ZI = Z - T, y0 = GF - 0.02;
  const ap = { a0: 0.5, a1: 1.1, y0: GF + 1.05, y1: GF + 1.4 };
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
  A.walkthrough = {
    start: { pos: [0, 0, -3.8], yaw: 180 },
    steps: [
      { goto: [0, -2.6], note: "approach shelter door" },
      { interact: "shelter_door", aim: [0.2, 1.1, -1.63], expectOpen: true },
      { goto: [0.3, -0.3], expectY: GF, note: "enter shelter" },
      { goto: [0.6, 0.6], expectY: GF, note: "at the aperture" },
      { interact: "firing_shutter", aim: [0.8, 1.38, 1.58], expectOpen: true },
      { look: [0.8, 1.3, 10], hold: 0.8 }, { shot: "16_shelter_aperture" },
      { look: [1.6, 0.9, -0.9], hold: 0.6 }, { shot: "17_shelter_bunks" },
      { goto: [0.3, -0.3] }, { goto: [0, -2.6] }, { goto: [0, -3.9], note: "leave shelter" },
    ],
  };
  return A;
}

// ----------------------------------------------------------------------------- command shelter
export function commandShelter() {
  const A = new Asset({
    id: "bunker_command_shelter", version: 1, name: "Command Shelter", category: "Bunkers",
    description: "Two-room concrete command shelter. Protected entrance behind a blast wall, steel outer door, gas lock and gas-tight inner door. Map room with radio desk and bunks; a steel-flapped loophole covers the approach.",
    floors: 1, enterable: true, footprint: [15.0, 13.0], plinthDepth: 0.6, maxGrade: 0.6,
  });
  stdMaterials(A);
  const X = 4.5, Z = 3.5, T = 1.0, GF = 0.2, CEIL = 2.4, ROOF = 3.6, XI = X - T, ZI = Z - T;
  const lh = { a0: 1.25, a1: 1.75, y0: GF + 1.25, y1: GF + 1.55 };
  const ent = door(A, { id: "entrance_door", axis: "x", outer: -Z, inner: -ZI, c: -2.5, floor: GF, w: 1.0, h: 1.9, hingeSide: -1, steel: true, lintel: false, step: false, label: "steel door" });
  concreteShell(A, { X, Z, T, GF, CEIL, ROOF, back: [ent, lh] });
  // gas lock partition with a gas-tight steel door
  const gd = door(A, { id: "gaslock_door", axis: "z", outer: -1.5, inner: -1.2, c: -1.2, floor: GF, w: 1.0, h: 1.9, hingeSide: -1, steel: true, lintel: false, step: false, label: "gas-tight door" });
  partition(A, { axis: "z", d0: -1.5, d1: -1.2, a0: -ZI, a1: ZI, y0: GF, y1: CEIL, holes: [gd], m: "concrete_wash" });
  // loophole flap (top-hung, interior)
  const p0 = [lh.a0 - 0.07, lh.y0 - 0.07, -ZI + 0.005], p1 = [lh.a1 + 0.07, lh.y1 + 0.07, -ZI + 0.045];
  hinged(A, {
    id: "loophole_flap", kind: "flap", label: "loophole flap", sound: "steel", axis: "x", openAngle: 80, duration: 1.1, apertureAlways: true,
    aperture: aabb([lh.a0, lh.y0, -Z], [lh.a1, lh.y1, -ZI + 0.06]),
    leaves: [{ hinge: [0, lh.y1 + 0.09, -ZI + 0.045], want: [0, 1, 1], collider: [p0, p1],
      build: plate(A, p0, p1, 1, 1, [[1.47, lh.y0 - 0.04, p1[2]], [1.53, lh.y0 + 0.02, p1[2] + 0.05]]) }],
  });
  // protected entrance
  A.box([-4.0, -0.3, -4.9], [-1.0, 2.2, -4.4], "concrete");
  A.box([-X, -0.3, -4.9], [-4.0, 2.2, -Z], "concrete");
  A.box([-4.0, -0.25, -4.4], [-0.6, 0.05, -Z], "concrete_floor");
  berms(A, { X, Z, front: ROOF - 1.4, sideH: ROOF - 0.6, sideRun: 3.0, frontRun: 3.0 });
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
  // gas lock fittings
  A.beginFurniture("Bench");
  A.box([-XI, GF + 0.42, -1.0], [-3.1, GF + 0.47, 1.5], "plank_z");
  for (const z of [-0.9, 1.4]) A.box([-3.4, GF, z - 0.04], [-3.15, GF + 0.42, z + 0.04], "plank_y");
  A.endFurniture();
  A.beginFurniture("Gas-mask hooks");
  A.box([-XI, GF + 1.6, -0.8], [-XI + 0.03, GF + 1.66, 1.2], "plank_z");
  for (const z of [-0.6, -0.1, 0.4, 0.9]) A.box([-XI + 0.03, GF + 1.35, z - 0.08], [-XI + 0.12, GF + 1.6, z + 0.08], "crate_olive");
  A.endFurniture(false);
  for (const [x, z] of [[1.0, 0.3], [-2.5, 0.0], [3.0, -1.5]]) lamp(A, x, CEIL, z);
  A.walkthrough = {
    start: { pos: [-0.2, 0, -6.4], yaw: 180 },
    steps: [
      { goto: [-0.4, -3.95], note: "enter protected entrance" },
      { goto: [-2.5, -3.95], note: "behind the blast wall" },
      { interact: "entrance_door", aim: [-2.3, 1.1, -2.54], expectOpen: true },
      { goto: [-2.5, -2.0] }, { goto: [-2.2, -1.0], expectY: GF, note: "gas lock" },
      { interact: "gaslock_door", aim: [-1.25, 1.1, -1.0], expectOpen: true },
      { goto: [-1.8, -1.2] }, { goto: [-0.6, -1.2], expectY: GF, note: "through the gas-tight door" },
      { goto: [1.5, -1.6], expectY: GF, note: "at the loophole" },
      { interact: "loophole_flap", aim: [1.5, 1.6, -2.47], expectOpen: true },
      { look: [1.5, 1.5, -12], hold: 0.8 }, { shot: "18_command_loophole" },
      { look: [1.0, 0.9, 1.6], hold: 0.6 }, { shot: "19_command_map_room" },
      { goto: [-0.6, -1.2] }, { goto: [-1.8, -1.2] }, { goto: [-2.5, -2.0] }, { goto: [-2.5, -3.95] },
      { goto: [-0.4, -3.95] }, { goto: [-0.2, -6.0], note: "leave command shelter" },
    ],
  };
  return A;
}
