// Beach structures for the MP & Creative update (SPEC 3.3 / 3.6, docs/assets-mpu.md): the obstacle belts (Belgian gate
// / Element C, log stake with a Teller mine, mined log ramp; the Czech hedgehog is the existing def_tank_trap), the
// shingle bank, a wooden seawall section, an anti-tank ditch segment, a log / steel roadblock, the minefield fence and
// "Achtung Minen" sign, and the LCVP in three variants (a procedural static prop, never drivable: the user's
// exception to the GLB-only rule for vehicles). Seaward / enemy side = local +Z throughout.
import { Asset } from "./kit.mjs";
import { stdMaterials, hinged, aabb } from "./parts.mjs";
import { M, mats, rng, rect, fire, cover, room, corners, setNav, setTour, tellerMine, r3 } from "./catalog_atlantic.mjs";

// ---------------------------------------------------------------- helpers
// rod between two points (square section): a steel angle, a log, a strut
function rod(A, p0, p1, w, m, o = {}) {
  const d = [p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]], L = Math.hypot(...d);
  const c = [(p0[0] + p1[0]) / 2, (p0[1] + p1[1]) / 2, (p0[2] + p1[2]) / 2];
  // box long axis = local Z; pitch about X (Rx first), then yaw about Y
  const yaw = (Math.atan2(d[0], d[2]) * 180) / Math.PI, pitch = (-Math.atan2(d[1], Math.hypot(d[0], d[2])) * 180) / Math.PI;
  return A.obox(c, [w, w, L], [pitch, yaw, 0], m, o);
}
// a round-ish log: two crossed square rods
function log(A, p0, p1, r, m = "log_raw", o = {}) {
  rod(A, p0, p1, r * 1.8, m, o);
  const d = [p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]], L = Math.hypot(...d);
  const c = [(p0[0] + p1[0]) / 2, (p0[1] + p1[1]) / 2, (p0[2] + p1[2]) / 2];
  const yaw = (Math.atan2(d[0], d[2]) * 180) / Math.PI, pitch = (-Math.atan2(d[1], Math.hypot(d[0], d[2])) * 180) / Math.PI;
  // second box rolled 45 deg about the log axis: built as a quaternion so the roll is about the log's own axis
  const q = qmul(qeuler([pitch, yaw, 0]), qaxis([0, 0, 1], 45));
  A._target.push({ t: "b", c: c.map(r3), s: [r3(r * 1.8), r3(r * 1.8), r3(L)], q: q.map((v) => Math.round(v * 1e5) / 1e5), m, k: 0 });
}
// quaternions [x, y, z, w]; qeuler matches glm::quat(radians(euler)) (R = Rz * Ry * Rx)
function qaxis(ax, deg) { const h = (deg * Math.PI) / 360, s = Math.sin(h); return [ax[0] * s, ax[1] * s, ax[2] * s, Math.cos(h)]; }
function qmul(a, b) {
  return [a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
    a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]];
}
function qeuler(e) { return qmul(qaxis([0, 0, 1], e[2]), qmul(qaxis([0, 1, 0], e[1]), qaxis([1, 0, 0], e[0]))); }
function qrot(q, v) {
  const u = [q[0], q[1], q[2]], s = q[3];
  const dot = u[0] * v[0] + u[1] * v[1] + u[2] * v[2], uu = u[0] * u[0] + u[1] * u[1] + u[2] * u[2];
  const cr = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]];
  return [0, 1, 2].map((i) => 2 * dot * u[i] + (s * s - uu) * v[i] + 2 * s * cr[i]);
}
// rigidly transform a list of box parts (rotation q about the origin, then translate t); prisms are not allowed
function xform(parts, q, t) {
  for (const p of parts) {
    if (p.t === "p") throw new Error("xform: prism");
    const c = qrot(q, p.c);
    p.c = [0, 1, 2].map((i) => r3(c[i] + t[i]));
    const pq = p.q ? p.q : p.r ? qeuler(p.r) : [0, 0, 0, 1];
    p.q = qmul(q, pq).map((v) => Math.round(v * 1e5) / 1e5);
    delete p.r;
    if (p.t === "r") p.t = "b";   // a rotated rounded box is drawn as a plain box
  }
}

// 5 x 7 pixel glyphs for the minefield signs
const GLYPH = {
  A: [".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"], C: [".####", "#....", "#....", "#....", "#....", "#....", ".####"],
  H: ["#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"], T: ["#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."],
  U: ["#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."], N: ["#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#", "#...#"],
  G: [".####", "#....", "#....", "#..##", "#...#", "#...#", ".####"], M: ["#...#", "##.##", "#.#.#", "#...#", "#...#", "#...#", "#...#"],
  I: [".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."], E: ["#####", "#....", "#....", "####.", "#....", "#....", "#####"],
  "!": ["..#..", "..#..", "..#..", "..#..", "..#..", ".....", "..#.."],
};
// text on a vertical plane z = zf facing dir (+1: readable from +Z, -1: from -Z), centred at (cx, cy), pixel size px
function pixelText(A, text, cx, cy, zf, dir, px, m) {
  const w = text.length * 6 - 1;
  for (let k = 0; k < text.length; k++) {
    const g = GLYPH[text[k]]; if (!g) continue;
    for (let row = 0; row < 7; row++) {
      const line = g[row];
      for (let c0 = 0; c0 < 5; c0++) {
        if (line[c0] !== "#" || (c0 > 0 && line[c0 - 1] === "#")) continue;
        let c1 = c0; while (c1 + 1 < 5 && line[c1 + 1] === "#") c1++;
        const col0 = k * 6 + c0, col1 = k * 6 + c1 + 1;
        // reading left to right: from -Z the text runs toward +X; from +Z toward -X
        const xa = dir < 0 ? cx + (col0 - w / 2) * px : cx - (col1 - w / 2) * px, xb = xa + (col1 - col0) * px;
        const y1 = cy + (3.5 - row) * px;
        A.box([xa, y1 - px, Math.min(zf, zf + dir * 0.004)], [xb, y1, Math.max(zf, zf + dir * 0.004)], m, { k: 0 });
      }
    }
  }
}
// "Achtung Minen!" sign board (both faces) centred at (x, y) on a post at z
function minenSign(A, x, y, z) {
  mats(A, "sign_white", "sign_black", "sign_red", "picket");
  A.box([x - 0.36, y - 0.22, z - 0.012], [x + 0.36, y + 0.22, z + 0.012], "sign_white", { k: 0 });
  for (const s of [-1, 1]) {
    A.box([x - 0.36, y + 0.19, z + s * 0.012], [x + 0.36, y + 0.22, z + s * 0.016], "sign_red", { k: 0 });
    A.box([x - 0.36, y - 0.22, z + s * 0.012], [x + 0.36, y - 0.19, z + s * 0.016], "sign_red", { k: 0 });
    pixelText(A, "ACHTUNG", x, y + 0.065, z + s * 0.012, s, 0.016, "sign_black");
    pixelText(A, "MINEN!", x, y - 0.075, z + s * 0.012, s, 0.016, "sign_black");
  }
}
const obstacleDef = (hp, mult, slope = 25) => ({ kind: "tank_trap", hp, smallArmsMultiplier: mult, blocksVehicles: true, maxSlopeDeg: slope });

// ============================================================================ obstacles
// Belgian gate (Element C / Cointet): 3.0 m wide, 2.5 m high steel-angle front lattice on rollers, braced back to
// three 4 m base girders; Teller mines lashed along the top. Front (+Z) faces the sea.
export function belgianGate() {
  const A = new Asset({ id: "bch_belgian_gate", version: 1, name: "Belgian gate (Element C)", category: "Beach obstacles",
    description: "Element C (Cointet gate) as in the outer Omaha belt: a 3.0 m wide, 2.5 m high riveted steel-angle lattice on concrete rollers, braced back to three 4 m base girders, Teller mines lashed along the top. Front faces the sea. Immune to small arms; HE / AT knocks it over; blocks vehicles and craft.",
    floors: 0, enterable: false, footprint: [3.3, 4.4], plinthDepth: 0, maxGrade: 0.6, noAutoPanels: true });
  stdMaterials(A); mats(A, "rust", "steel_dark", "cement");
  const S = "rust", W = 0.09, ZF = 1.7, ZB = -2.3, H = 2.5, Y0 = 0.3;
  rod(A, [0, Y0, ZF], [0, H, ZF], W, S);   // first part: its centre is the knock-over pivot (src/assets.cpp)
  for (const x of [-1.5, -0.75, 0.75, 1.5]) rod(A, [x, Y0, ZF], [x, H, ZF], W, S);
  for (const y of [Y0, 0.95, 1.6, H]) rod(A, [-1.55, y, ZF], [1.55, y, ZF], W, S);
  for (const [xa, xb] of [[-1.5, -0.75], [-0.75, 0], [0, 0.75], [0.75, 1.5]]) { rod(A, [xa, Y0, ZF], [xb, 1.6, ZF], 0.06, S); rod(A, [xb, 0.95, ZF], [xa, H, ZF], 0.06, S); }
  for (const x of [-1.5, 0, 1.5]) {
    rod(A, [x, Y0, ZF + 0.1], [x, Y0, ZB], 0.12, S);                // base girder
    rod(A, [x, H - 0.05, ZF], [x, Y0 + 0.05, ZB + 0.2], W, S);      // main strut
    rod(A, [x, 1.3, ZF], [x, Y0 + 0.05, ZF - 1.6], 0.07, S);
  }
  rod(A, [-1.55, Y0, ZB + 0.1], [1.55, Y0, ZB + 0.1], 0.1, S);
  rod(A, [-1.5, Y0, ZF - 1.5], [1.5, Y0, ZF - 1.5], 0.08, S);
  rod(A, [-1.5, Y0 + 0.05, ZB + 0.3], [1.5, 1.2, ZF - 0.6], 0.06, S);
  for (const [x, z] of [[-1.5, ZF], [1.5, ZF], [0, ZB + 0.2]]) for (const a of [0, 45]) A.obox([x, 0.16, z], [0.28, 0.32, 0.32], [a, 0, 0], "cement");   // rollers
  for (const x of [-1.0, 0.3, 1.2]) tellerMine(A, [x, H + 0.06, ZF], 15 * x, 90);
  A.meta.defense = obstacleDef(2500, 0, 20);
  setNav(A, { effect: "block", cover: [cover([0, 0, ZF - 0.45], 0, "low", ["left", "right"])] });
  setTour(A, { pos: [3.5, 0, 4.5], yaw: 0 }, [{ goto: [2.6, 3.2] }, { look: [0, 1.4, ZF], hold: 0.5 }, { shot: "mpu_belgian_gate_close" }]);
  return A;
}
// log stake (Pfahl) with a Teller mine: a 3.6 m pine log driven in leaning toward the sea, the mine on its tip
export function logStakeMine() {
  const A = new Asset({ id: "bch_log_stake_mine", version: 1, name: "Log stake with Teller mine", category: "Beach obstacles",
    description: "Seaward-leaning log stake of the second Omaha belt: a 0.3 m pine log 3.6 m long driven in at about 60 deg, a Teller mine 35 fixed to its tip. Small arms barely mark it; HE / AT knocks it over.",
    floors: 0, enterable: false, footprint: [0.8, 2.2], plinthDepth: 0, maxGrade: 0.8, noAutoPanels: true });
  stdMaterials(A); mats(A, "log_raw", "bark_grey");
  const base = [0, -0.4, -0.7], tip = [0, 2.75, 1.1];
  rod(A, base, tip, 0.27, "log_raw");
  log(A, base, tip, 0.15, "bark_grey");
  tellerMine(A, [0, tip[1] + 0.07, tip[2] + 0.04], 0, -30);
  A.box([-0.06, tip[1] - 0.05, tip[2] - 0.06], [0.06, tip[1] + 0.05, tip[2] + 0.06], "steel_dark", { k: 0 });
  A.meta.defense = obstacleDef(600, 0.05, 30);
  setNav(A, { effect: "carve", cover: [] });
  setTour(A, { pos: [1.8, 0, 3.0], yaw: 0 }, [{ look: [0, 2.4, 1.0], hold: 0.4 }, { shot: "mpu_log_stake" }]);
  return A;
}
// mined log ramp (Hemmbalken): two 5 m logs rising from the seaward end to 1.9 m on two legs, mines on the high end
export function logRampMined() {
  const A = new Asset({ id: "bch_log_ramp_mined", version: 1, name: "Mined log ramp", category: "Beach obstacles",
    description: "Log ramp of the third Omaha belt: two 5 m logs rising from the sand at the seaward end to 1.9 m on a pair of legs, braced and capped with Teller mines, so a craft riding in on the tide climbs it onto the mines. Small arms barely mark it; HE / AT knocks it down.",
    floors: 0, enterable: false, footprint: [1.8, 5.4], plinthDepth: 0, maxGrade: 0.8, noAutoPanels: true });
  stdMaterials(A); mats(A, "log_raw", "bark_grey");
  const lo = 2.4, hi = -2.2, H = 1.9;
  log(A, [-0.45, H, hi], [-0.45, 0.05, lo], 0.16, "log_raw");          // first part: knock-over pivot height
  log(A, [0.45, H, hi], [0.45, 0.05, lo], 0.16, "log_raw");
  for (const x of [-0.45, 0.45]) log(A, [x, -0.4, hi + 0.3], [x, H - 0.05, hi + 0.15], 0.15, "bark_grey");   // legs
  log(A, [-0.75, H - 0.25, hi + 0.25], [0.75, H - 0.25, hi + 0.25], 0.12, "bark_grey");
  log(A, [-0.45, 0.2, hi + 0.45], [0.45, H - 0.4, hi + 0.2], 0.08, "bark_grey");
  for (const x of [-0.45, 0.45]) tellerMine(A, [x, H + 0.2, hi + 0.2], 10 * x, -20);
  A.meta.defense = obstacleDef(900, 0.05, 25);
  setNav(A, { effect: "carve", cover: [cover([0, 0, hi - 0.6], 0, "low", ["left", "right"])] });
  setTour(A, { pos: [2.6, 0, 3.4], yaw: 0 }, [{ look: [0, 1.0, 0], hold: 0.4 }, { shot: "mpu_log_ramp" }]);
  return A;
}

// ============================================================================ beach top
// shingle bank piece: 12 m along the beach, 12 m deep, a 2.5 m crest; gentle seaward face, steeper landward face
export function shingleBank() {
  const A = new Asset({ id: "bch_shingle_bank", version: 1, name: "Shingle bank (12 m)", category: "Beach",
    description: "12 m piece of the Omaha shingle bank: rounded stones heaped 2.5 m high and 12 m deep, a long seaward slope (about 20 deg) and a steeper landward face (about 35 deg) that gives the first real cover off the beach. Pieces join side by side along local X.",
    floors: 0, enterable: false, footprint: [12, 12], plinthDepth: 0, maxGrade: 1.5, noAutoPanels: true });
  stdMaterials(A); mats(A, "shingle", "rock", "rock_dark", "sand");
  const X = 6, zs = 6, zc0 = -0.8, zc1 = -2.4, zl = -6, H = 2.5;
  A.prism(0, -X, X, [[zs, -0.3], [zc0, H], [zl, -0.3]], "shingle");
  A.prism(0, -X, X, [[zc0, H], [zc1, H - 0.05], [zl, -0.3]], "shingle");
  const R = rng(4419);
  const yAt = (z) => z > zc0 ? -0.3 + (H + 0.3) * (zs - z) / (zs - zc0) : z > zc1 ? H : -0.3 + (H + 0.05) * (z - zl) / (zc1 - zl);
  for (let k = 0; k < 170; k++) {
    const x = (R() * 2 - 1) * (X - 0.2), z = zl + 0.3 + R() * (zs - zl - 0.6), s = 0.12 + R() * R() * 0.32;
    const y = yAt(z) - s * 0.35;
    A.rbox([x - s / 2, y - s * 0.3, z - s * 0.6], [x + s / 2, y + s * 0.45, z + s * 0.6], s * 0.3, R() < 0.4 ? "rock_dark" : "rock", { k: 0 });
  }
  A.box([-X, -0.3, zs - 0.2], [X, -0.18, zs + 1.2], "sand", { k: 0 });   // sand skirt along the seaward toe
  const cv = [];
  for (const x of [-4.5, -1.5, 1.5, 4.5]) cv.push(cover([x, r3(yAt(-5.2)), -5.2], 0, "high", ["over"]));
  setNav(A, { effect: "area", area: "sand", cover: cv });
  setTour(A, { pos: [0.5, 0, 9.0], yaw: 0 }, [
    { goto: [0.5, 2.0], note: "up the seaward slope" }, { goto: [0.5, -1.6], expectY: H, note: "on the crest" },
    { goto: [0.5, -5.4], note: "down the landward face" }, { look: [0.5, 1.2, 4.0], hold: 0.5 }, { shot: "mpu_shingle_cover" },
  ]);
  return A;
}
// wooden seawall section: piles every 2 m, horizontal planking 1.4 m above the sand on the seaward face, raking braces
export function seawallWood() {
  const A = new Asset({ id: "bch_seawall_wood", version: 1, name: "Wooden seawall (10 m)", category: "Beach",
    description: "10 m of the timber seawall behind the Omaha shingle (east half of Easy Red): creosoted piles every 2 m, planking standing 1.4 m above the sand on the seaward face with shingle drifted against it, raking braces behind. Crouching cover; the planking bays splinter under fire.",
    floors: 0, enterable: false, footprint: [10.4, 2.6], plinthDepth: 0, maxGrade: 0.8 });
  stdMaterials(A); mats(A, "shingle", "picket");
  const plank = A.materials.find((m) => m.name === "plank_x"); Object.assign(plank, { albedo: [0.13, 0.1, 0.07] });
  const H = 1.4;
  for (let x = -5; x < 5 - 0.01; x += 2) A.box([x + 0.12, -0.4, 0.0], [x + 1.88, H, 0.08], "plank_x");   // planking bays (timber panels)
  for (let x = -5; x <= 5.01; x += 2) {
    A.box([x - 0.12, -0.8, -0.24], [x + 0.12, H + 0.15, 0.0], "log_z");
    A.obox([x, 0.5, -0.9], [0.14, 0.14, 1.9], [-38, 0, 0], "picket", { k: 0 });
  }
  A.box([-5.1, H - 0.08, -0.26], [5.1, H + 0.04, 0.1], "log_x", { k: 0 });   // capping waler
  A.prism(0, -5, 5, [[0.08, -0.3], [0.08, 0.7], [1.3, -0.3]], "shingle");
  setNav(A, { effect: "block", cover: [-3.5, -1.0, 1.5, 4.0].map((x) => cover([x, 0, -0.7], 0, "low", ["over"])) });
  setTour(A, { pos: [1.0, 0, -3.0], yaw: 180 }, [{ goto: [1.0, -0.8] }, { look: [1.0, 1.2, 6], hold: 0.5 }, { shot: "mpu_seawall" }]);
  return A;
}
// anti-tank ditch segment: 12 m long, 7 m wide at the top, 3 m deep, steep revetted faces (not climbable), water in the
// bottom, spoil thrown up on the defender's side. Segments join end to end along local X.
export function atDitch() {
  const A = new Asset({ id: "bch_at_ditch", version: 1, name: "Anti-tank ditch (12 m)", category: "Beach obstacles",
    description: "12 m segment of the anti-tank ditch across the mouth of E-1: 7 m wide at the top and 3 m deep, both faces cut steep (about 75 deg) and revetted with logs, too high to climb, jump or vault out of; standing water in the bottom, spoil thrown up on the defender's side (-Z). Join segments end to end; the ends are open.",
    floors: 0, enterable: false, footprint: [12, 7.4], plinthDepth: 0, maxGrade: 0.8, noAutoPanels: true });
  stdMaterials(A); mats(A, "spoil", "mud", "water", "log_raw");
  const X = 6, D = 3.0, ZB = 1.5, run = D / Math.tan((75 * Math.PI) / 180), ZT = ZB + run, ang = 90 - 75;
  A.box([-X, -D - 0.3, -ZB - 0.2], [X, -D, ZB + 0.2], "mud");
  A.box([-X, -D + 0.02, -ZB + 0.1], [X, -D + 0.05, ZB - 0.1], "water", { k: 0 });
  for (const s of [-1, 1]) {
    // the face: a slab battered back at 75 deg, logs laid along it, earth fill out to the cut edge
    const zc = s * (ZB + run / 2 + 0.2), h = D + 0.3;
    A.obox([0, -D / 2 - 0.1, zc], [2 * X, h / Math.cos((ang * Math.PI) / 180), 0.4], [s * ang, 0, 0], "spoil");
    for (let y = -D + 0.25; y < -0.1; y += 0.42) { const z = s * (ZB + (y + D) * Math.tan((ang * Math.PI) / 180) + 0.02); A.obox([0, y, z], [2 * X, 0.32, 0.32], [s * ang, 0, 0], "log_raw", { k: 0 }); }
    A.box([-X, -D - 0.3, s > 0 ? ZT + 0.05 : -ZT - 0.7], [X, 0.0, s > 0 ? ZT + 0.7 : -ZT - 0.05], "spoil");
  }
  A.prism(0, -X, X, [[-ZT - 0.6, -0.2], [-ZT - 1.8, 1.0], [-ZT - 4.2, -0.2]], "spoil");   // defender's spoil bank
  setNav(A, { effect: "carve", cover: [-4, 0, 4].map((x) => cover([x, 0, -ZT - 4.6], 0, "low", ["over"])),
    areas: [{ outline: rect(-X, -ZB, X, ZB), area: "water" }] },
    [{ outline: rect(-X - 0.8, -ZT - 0.75, X + 0.8, ZT + 0.75), depth: D + 0.3, bank: 0.3 }]);
  setTour(A, { pos: [1.0, 0, -9.5], yaw: 180 }, [
    { goto: [1.0, -ZT - 0.9], note: "edge of the ditch" }, { look: [1.0, -D, 1.0], hold: 0.5 }, { shot: "mpu_at_ditch" },
  ], { cut: true, climb: [
    { from: [0, 0], to: [0, 8], floorY: -D, note: "seaward face" },
    { from: [1, 0], to: [1, -8], floorY: -D, note: "landward face" },
  ] });
  return A;
}
// roadblock: a log crib filled with earth across the left half, steel rails in concrete sockets across the right
export function roadblock() {
  const A = new Asset({ id: "bch_roadblock", version: 1, name: "Log and steel roadblock", category: "Beach obstacles",
    description: "Draw-road block as at the head of E-1: an earth-filled log crib 1.5 m high across 4.6 m, and five railway rails set in concrete sockets across the rest (on foot you squeeze between them; vehicles cannot). The crib's log faces splinter under fire.",
    floors: 0, enterable: false, footprint: [9.0, 2.4], plinthDepth: 0, maxGrade: 0.6 });
  stdMaterials(A); mats(A, "spoil", "rust", "cement", "log_raw");
  const H = 1.5;
  for (const z of [-0.6, 0.6]) A.box([-4.4, 0, z - 0.15], [0.2, H, z + 0.15], "log_x");     // crib faces (timber panels)
  for (let y = 0.15; y < H; y += 0.3) for (const z of [-0.6, 0.6]) A.obox([-2.1, y, z + Math.sign(z) * 0.14], [4.7, 0.28, 0.06], [0, 0, 0], "log_raw", { k: 0 });
  for (const x of [-4.4, -2.1, 0.2]) A.box([x - 0.15, 0, -0.75], [x + 0.15, H + 0.1, 0.75], "log_z");
  A.box([-4.25, 0, -0.45], [0.05, H - 0.05, 0.45], "spoil");
  for (let k = 0; k < 5; k++) {
    const x = 1.0 + k * 0.85;
    A.rbox([x - 0.3, -0.3, -0.3], [x + 0.3, 0.12, 0.3], 0.06, "cement");
    A.obox([x, 0.75, 0.05], [0.13, 1.5, 0.15], [8, 0, 0], "rust");
  }
  A.obox([2.6, 0.08, -1.0], [3.6, 0.13, 0.15], [0, 6, 0], "rust", { k: 0 });   // a spare rail on the ground
  setNav(A, { effect: "block", cover: [-3.5, -2.1, -0.7].map((x) => cover([x, 0, -1.1], 0, "high", ["over"])) });
  setTour(A, { pos: [-1.0, 0, -3.5], yaw: 180 }, [{ goto: [-1.0, -1.3] }, { look: [-1.0, 1.4, 4], hold: 0.4 }, { shot: "mpu_roadblock" }]);
  return A;
}
// minefield fence (10 m) with the sign, and the sign alone on a stake
export function minefieldFence() {
  const A = new Asset({ id: "bch_minefield_fence", version: 1, name: "Minefield fence with sign (10 m)", category: "Beach obstacles",
    description: "Single-strand wire fence on pickets marking a minefield, with an \"Achtung Minen!\" board. The marked field (10 x 10 m on the +Z side) is a minefield nav area: bots never path through it.",
    floors: 0, enterable: false, footprint: [10, 0.5], plinthDepth: 0, maxGrade: 1.0, noAutoPanels: true });
  stdMaterials(A); mats(A, "picket", "steel_dark");
  for (let x = -5; x <= 5.01; x += 2.5) A.box([x - 0.04, 0, -0.04], [x + 0.04, 1.15, 0.04], "picket");
  for (const y of [0.55, 1.0]) A.box([-5, y - 0.004, -0.004], [5, y + 0.004, 0.004], "steel_dark", { k: 0 });
  minenSign(A, 1.25, 0.82, -0.05);
  setNav(A, { effect: "area", area: "minefield", areas: [{ outline: rect(-5, 0.2, 5, 10.2), area: "minefield" }], cover: [] });
  setTour(A, { pos: [1.25, 0, -3.0], yaw: 180 }, [{ goto: [1.25, -1.2] }, { look: [1.25, 0.82, 0], hold: 0.5 }, { shot: "mpu_minefield_sign" }]);
  return A;
}
export function minenSignStake() {
  const A = new Asset({ id: "bch_minen_sign", version: 1, name: "\"Achtung Minen\" sign", category: "Beach obstacles",
    description: "\"Achtung Minen!\" warning board on a stake, for the bluff paths and the forest edge. Dressing only (place a minefield fence to mark the mined ground for the bots).",
    floors: 0, enterable: false, footprint: [0.8, 0.3], plinthDepth: 0, maxGrade: 1.2, noAutoPanels: true });
  stdMaterials(A);
  minenSign(A, 0, 1.15, 0.05);
  A.box([-0.04, -0.3, -0.04], [0.04, 1.35, 0.04], "picket");
  setNav(A, { effect: "none" });
  setTour(A, { pos: [0, 0, -2.2], yaw: 180 }, [{ look: [0, 1.15, 0], hold: 0.5 }, { shot: "mpu_minen_sign" }]);
  return A;
}

// ============================================================================ LCVP (Higgins boat), static prop
// 11.0 x 3.3 m, plywood hull painted navy grey, steel bow ramp (2.0 m) hinged at the well deck, the open cargo well
// (5.9 m), the raised engine deck aft with the coxswain's station and two .30 cal gun tubs (empty pintles: no gun
// models). The ramp is an interactable leaf (E lowers / raises it). Hull side bays are timber panels (rifle fire
// holes them). Variants: intact (ramp up), beached (ramp down on the sand), wrecked (holed, burnt, listing, ramp torn
// away, half sunk in the sand).
function lcvpHull(A, { ramp = true, holes = [] } = {}) {
  stdMaterials(A); mats(A, "steel_dark", "rust", "burnt");
  Object.assign(A.materials.find((m) => m.name === "plank_y"), { albedo: [0.2, 0.215, 0.22], roughness: 0.7, pattern: "painted" });   // hull sides
  A.mat("lcvp_deck", [0.16, 0.15, 0.13], 0.85, "wood", { grain: 2 });
  A.mat("lcvp_grey", [0.2, 0.215, 0.22], 0.7, "painted", { grain: 1 });
  const XO = 1.65, XI = 1.5, ZS = -5.5, ZB = 4.95, DECK = 0.45, GUN = 1.95, AFT = -1.0, AFTY = 1.3;
  // bottom and well deck
  A.box([-XO, -0.15, ZS], [XO, DECK - 0.06, ZB], "lcvp_grey");
  A.box([-XI, DECK - 0.06, AFT], [XI, DECK, ZB - 0.05], "lcvp_deck");
  for (let z = AFT + 0.3; z < ZB - 0.2; z += 0.6) A.box([-XI, DECK, z - 0.03], [XI, DECK + 0.02, z + 0.03], "lcvp_deck", { k: 0 });
  // sides: panel bays (plank_y = hull grey) between frames; holes leave a bay out
  const bays = [];
  const NB = 8, BW = (ZB - ZS) / NB;
  for (let i = 0; i < NB; i++) bays.push([ZS + i * BW, ZS + (i + 1) * BW]);
  bays.forEach(([z0, z1], i) => {
    for (const s of [-1, 1]) {
      if (holes.includes(s * (i + 1))) { A.box([s > 0 ? XI : -XO, 0.0, z0], [s > 0 ? XO : -XI, 0.55, z1], "plank_y"); continue; }   // only the bottom strake left
      A.box([s > 0 ? XI : -XO, -0.1, z0], [s > 0 ? XO : -XI, GUN, z1], "plank_y");
    }
  });
  for (let z = ZS; z <= ZB + 0.01; z += BW) for (const s of [-1, 1]) A.box([s * XO - 0.03, -0.1, z - 0.05], [s * XO + 0.03, GUN + 0.04, z + 0.05], "lcvp_grey", { k: 0 });   // frames
  for (const s of [-1, 1]) {
    A.box([s > 0 ? XI - 0.05 : -XO - 0.06, GUN, ZS], [s > 0 ? XO + 0.06 : -XI + 0.05, GUN + 0.08, ZB], "lcvp_grey");   // gunwale
    A.box([s * XO - 0.05, 0.5, ZS], [s * XO + 0.05, 0.62, ZB], "lcvp_grey", { k: 0 });                               // rubbing strake
    A.box([s > 0 ? XI - 0.06 : -XI, DECK, 2.2], [s > 0 ? XI : -XI + 0.06, GUN - 0.1, ZB - 0.1], "steel_dark", { k: 0 });   // armour plate (forward well)
  }
  A.box([-XO, -0.1, ZS - 0.12], [XO, GUN, ZS], "lcvp_grey");                // transom
  // bow: ramp housing posts and the hinge
  for (const s of [-1, 1]) A.box([s > 0 ? 1.08 : -XO, DECK, ZB - 0.05], [s > 0 ? XO : -1.08, GUN + 0.08, ZB + 0.1], "lcvp_grey");
  A.box([-XO, -0.15, ZB - 0.05], [XO, DECK, ZB + 0.2], "lcvp_grey");
  // aft: engine deck, coxswain's shield, gun tubs with empty pintles, the engine hatch
  A.box([-XI, DECK - 0.06, ZS], [XI, AFTY, AFT], "lcvp_grey");
  A.box([-XI, AFTY, AFT - 0.06], [XI, AFTY + 0.04, AFT], "lcvp_deck", { k: 0 });
  A.box([0.2, AFTY, -2.25], [1.2, AFTY + 0.9, -2.15], "steel_dark");
  for (const s of [-1, 1]) {
    for (let k = 0; k < 8; k++) { const t = (k * Math.PI) / 4; A.obox([s * 0.95 + Math.sin(t) * 0.45, AFTY + 0.35, -3.4 + Math.cos(t) * 0.45], [0.36, 0.7, 0.04], [0, (k * 45), 0], "steel_dark", { k: 0 }); }
    A.box([s * 0.95 - 0.04, AFTY, -3.44], [s * 0.95 + 0.04, AFTY + 0.95, -3.36], "steel_dark", { k: 0 });   // pintle (no gun)
  }
  A.box([-0.6, AFTY, -4.8], [0.6, AFTY + 0.08, -3.95], "lcvp_deck", { k: 0 });
  if (!ramp) return { XO, XI, ZS, ZB, DECK, GUN, AFT, AFTY };
  return { XO, XI, ZS, ZB, DECK, GUN, AFT, AFTY };
}
// the bow ramp as a hinged leaf: closed it stands upright, open it lies 104 deg forward on the sand
function lcvpRamp(A, g, open) {
  const W0 = -1.05, W1 = 1.05, L = 1.95, T = 0.1, hz = g.ZB + 0.05, hy = g.DECK;
  const p0 = [W0, hy, hz], p1 = [W1, hy + L, hz + T];
  hinged(A, { id: "lcvp_ramp", kind: "flap", label: "bow ramp", sound: "steel", axis: "x", openAngle: 104, duration: 2.2, defaultOpen: open,
    aperture: aabb([W0, hy + 0.4, hz - 0.3], [W1, hy + L - 0.1, hz + 0.4]), colliderMat: "lcvp_grey",
    leaves: [{ hinge: [0, hy, hz], want: [0, -1, 1], collider: [p0, p1],
      build: (Tg) => {
        A.box(p0, p1, "lcvp_grey", Tg);
        for (let y = hy + 0.2; y < hy + L - 0.1; y += 0.25) A.box([W0 + 0.1, y, hz - 0.03], [W1 - 0.1, y + 0.04, hz], "steel_dark", Tg);   // cleats (inner face)
        for (const x of [W0 + 0.05, W1 - 0.15]) A.box([x, hy + L - 0.25, hz + T], [x + 0.1, hy + L - 0.05, hz + T + 0.06], "steel_dark", Tg);   // chain lugs
      } }],
  });
}
function lcvpNav(A, g, open, extraCover = []) {
  setNav(A, {
    effect: "block",
    rooms: [room("well", [-g.XI, g.DECK, g.AFT], [g.XI, g.GUN + 0.6, g.ZB])],
    doors: [{ a: "well", b: "outside", p: [0, g.DECK, g.ZB + 0.05] }],
    entrances: [{ p: [0, 0, g.ZB + 2.6], dir: [0, 0, -1], room: "well" }],
    corners: corners("well", -g.XI, g.XI, g.AFT, g.ZB, g.DECK, 0.4),
    firingPoints: [fire([-0.95, g.AFTY, -3.4], 0, "mg"), fire([0.95, g.AFTY, -3.4], 0, "mg")],
    cover: [cover([1.0, g.DECK, 1.0], 90, "low", ["over"]), cover([-1.0, g.DECK, 1.0], 270, "low", ["over"]),
            cover([g.XO + 0.6, 0, -1.0], 270, "high", ["left", "right"]), cover([-g.XO - 0.6, 0, -1.0], 90, "high", ["left", "right"])].concat(extraCover),
  });
  void open;
}
export function lcvpIntact() {
  const A = new Asset({ id: "bch_lcvp_intact", version: 1, name: "LCVP (intact, ramp up)", category: "Landing craft",
    description: "LCVP (Higgins boat), 11.0 x 3.3 m: navy-grey plywood hull, armoured forward well, steel bow ramp raised, the coxswain's shield and two empty .30 cal gun tubs aft. A static prop, not drivable; E on the ramp lowers it. Stands on the sand (the engine does not place structures in open water).",
    floors: 0, enterable: true, footprint: [3.4, 11.4], plinthDepth: 0.6, maxGrade: 0.6 });
  const g = lcvpHull(A);
  lcvpRamp(A, g, false);
  lcvpNav(A, g, false, [cover([0, g.DECK, 3.6], 0, "high", ["over"])]);
  setTour(A, { pos: [0.3, 0, 9.5], yaw: 0 }, [
    { goto: [0.3, 7.6], note: "in front of the raised ramp" },
    { interact: "lcvp_ramp", aim: [0.2, g.DECK + 1.1, g.ZB + 0.1], expectOpen: true, note: "lower the ramp" },
    { goto: [0.2, 3.2], expectY: g.DECK, note: "up the ramp into the well" },
    { look: [0, 1.5, -5], hold: 0.5 }, { shot: "mpu_lcvp_well" },
    { goto: [0.2, 7.8], note: "back out over the ramp" },
  ]);
  return A;
}
export function lcvpBeached() {
  const A = new Asset({ id: "bch_lcvp_beached", version: 1, name: "LCVP (beached, ramp down)", category: "Landing craft",
    description: "LCVP grounded on the sand with its bow ramp down: spawn cover for the first wave. 11.0 x 3.3 m navy-grey plywood hull, armoured forward well, empty .30 cal gun tubs aft. A static prop, not drivable; E raises the ramp.",
    floors: 0, enterable: true, footprint: [3.4, 11.4], plinthDepth: 0.6, maxGrade: 0.6 });
  const g = lcvpHull(A);
  lcvpRamp(A, g, true);
  lcvpNav(A, g, true);
  setTour(A, { pos: [0.3, 0, 10.0], yaw: 0 }, [
    { goto: [0.2, 8.2], note: "at the ramp foot" },
    { goto: [0.2, 3.0], expectY: g.DECK, note: "up the ramp into the well" },
    { goto: [0.2, -0.5], expectY: g.DECK, note: "aft end of the well" },
    { look: [0, 1.4, 9], hold: 0.5 }, { shot: "mpu_lcvp_beached_view" },
    { goto: [0.2, 3.0] }, { goto: [0.2, 8.4], note: "back out" },
  ]);
  return A;
}
export function lcvpWrecked() {
  const A = new Asset({ id: "bch_lcvp_wrecked", version: 1, name: "LCVP (wrecked)", category: "Landing craft",
    description: "Wrecked LCVP: holed by fire, burnt out amidships, listing 7 deg and settled half a metre into the sand, the bow ramp torn away and lying beside the hull. A static prop; the well is still enterable over the bow sill.",
    floors: 0, enterable: true, footprint: [4.4, 11.6], plinthDepth: 0.3, maxGrade: 0.8 });
  const g = lcvpHull(A, { holes: [3, -4, 5, -2] });
  // burn scars and debris inside the well (before the list is applied, so they lie in the hull frame)
  for (const [x, z, sx, sz] of [[0.2, 1.5, 2.2, 1.6], [-0.6, -0.3, 1.1, 0.9]]) A.box([x - sx / 2, g.DECK, z - sz / 2], [x + sx / 2, g.DECK + 0.012, z + sz / 2], "burnt", { k: 0 });
  for (const s of [-1, 1]) A.box([s > 0 ? g.XO : -g.XO - 0.01, 0.9, 0.3], [s > 0 ? g.XO + 0.01 : -g.XO, 1.8, 2.5], "burnt", { k: 0 });
  A.obox([0.6, g.DECK + 0.1, 0.8], [0.25, 0.2, 2.2], [0, 25, 0], "plank_x", { k: 0 });
  A.obox([-0.8, g.DECK + 0.12, 2.6], [0.3, 0.25, 0.45], [0, 30, 0], "steel_dark");   // jerrycan-sized box
  // list 7 deg to starboard, bow down 2 deg, sunk 0.5 m: every part so far turns with the hull
  xform(A.parts, qeuler([2, 0, -7]), [0, -0.5, 0]);
  // the torn-off ramp lies on the sand to port of the bow
  A.obox([-2.6, 0.12, 5.4], [2.1, 0.1, 1.95], [0, 18, 4], "lcvp_grey");
  A.obox([-2.6, 0.2, 5.4], [1.9, 0.06, 1.75], [0, 18, 4], "steel_dark", { k: 0 });
  lcvpNav(A, g, true);
  A.meta.nav.rooms[0].min[1] = r3(g.DECK - 0.4); A.meta.nav.corners.forEach((q) => { q.p[1] = r3(g.DECK - 0.5); });
  A.meta.nav.firingPoints = [];
  A.meta.nav.cover = [cover([g.XO + 0.6, 0, 2.5], 270, "low", ["left", "right"]), cover([-g.XO - 0.6, 0, 2.5], 90, "low", ["left", "right"])];   // beside intact side bays
  setTour(A, { pos: [0.2, 0, 9.5], yaw: 0 }, [
    { goto: [0.1, 6.0], note: "to the open bow" },
    { goto: [0.1, 3.0], note: "over the sill into the burnt well" },
    { look: [0.4, 0.6, -3], hold: 0.5 }, { shot: "mpu_lcvp_wreck_well" },
    { goto: [0.1, 6.4], note: "back out" },
  ]);
  return A;
}

export function beachAssets() {
  return [belgianGate(), logStakeMine(), logRampMined(), shingleBank(), seawallWood(), atDitch(), roadblock(), minefieldFence(), minenSignStake(), lcvpIntact(), lcvpBeached(), lcvpWrecked()];
}
