// RB1: three reference buildings recreated as the game's own enterable buildings (docs/rb1-buildings.md). The reference
// meshes (E:\nmk\incoming\rb1, licences unknown) were only rendered and measured; nothing of them is imported. Each is
// built through the house() generator (rooms, doors, windows, nav, panels, colliders, tour) at the measured proportions,
// scaled to real size, with every surface on an image-based "replace" texture slot (content/textures/textures.json rb_*)
// and the reference's facade depth and wear story added as parts.
import { P } from "./kit.mjs";
import { fbox } from "./parts.mjs";
import { mats, face, house, meta } from "./catalog_chateau.mjs";

const GF = 0.3;
const K0 = { k: 0 }, L1 = { k: 0, lod: 1 };
// a deterministic hash for the scatter (stains, streaks)
const rnd = (seed, k) => { let x = (seed ^ Math.imul(k + 1, 0x9e3779b1)) >>> 0; x ^= x >>> 16; x = Math.imul(x, 0x7feb352d); x ^= x >>> 15; x = Math.imul(x, 0x846ca68b); x ^= x >>> 16; return (x >>> 0) / 4294967296; };

function rbMats(A, defs) {
  for (const [name, albedo, rough, slot, pattern = "plain", extra = {}] of defs) {
    const m = A.materials.find((q) => q.name === name);
    const tex = slot ? { texture: slot } : {};   // a flat material has no texture key at all (null broke the loader)
    if (m) { Object.assign(m, { albedo, roughness: rough, pattern, metallic: 0, ...tex, ...extra }); if (!slot) delete m.texture; }
    else A.mat(name, albedo, rough, pattern, { ...tex, ...extra });
  }
}
function roomTypes(A, map) {
  const t = {};
  for (const r of A.meta.nav.rooms) {
    if (!(r.id in map)) throw new Error(`${A.meta.id}: no room type for ${r.id}`);
    t[r.id] = map[r.id];
  }
  A.meta.roomTypes = t;
}
// openings on a face as [a0, a1, y0, y1] (to keep streaks and stains off them)
const clear = (ops, a, y0, y1, pad = 0.1) => !ops.some(([a0, a1, b0, b1]) => a > a0 - pad && a < a1 + pad && y1 > b0 - pad && y0 < b1 + pad);
function streaks(A, F, a0, a1, top, ops, seed, { n = 14, m = "rb_rust_streak", minL = 0.3, maxL = 1.6 } = {}) {
  for (let k = 0; k < n; k++) {
    const a = a0 + 0.15 + rnd(seed, k) * (a1 - a0 - 0.3), L = minL + rnd(seed, k + 50) * (maxL - minL), w = 0.04 + rnd(seed, k + 90) * 0.1;
    if (!clear(ops, a, top - L, top)) continue;
    fbox(A, F, a - w / 2, a + w / 2, top - L, top - 0.02, 0.004, 0.007, m, L1);
    fbox(A, F, a - w * 0.3, a + w * 0.3, top - L - 0.25, top - L, 0.004, 0.007, m, L1);   // the thinning tail
  }
}
// planks over an opening inside a frame (the reference's boarded windows): frame boards, planks with gaps
function boarded(A, F, a0, a1, y0, y1, { e = 0.0, frame = "rb_board_frame", plank = "rb_board", n = 5, solid = true } = {}) {
  const t = 0.07;
  fbox(A, F, a0 - t, a1 + t, y1, y1 + t, e, e + 0.07, frame, { bv: 0.01, k: 0 });
  fbox(A, F, a0 - t, a1 + t, y0 - t, y0, e, e + 0.07, frame, { bv: 0.01, k: 0 });
  for (const a of [a0 - t, a1]) fbox(A, F, a, a + t, y0, y1, e, e + 0.07, frame, { bv: 0.01, k: 0 });
  const ph = (y1 - y0 - 0.02 * (n - 1)) / n;
  for (let i = 0; i < n; i++) {
    const yb = y0 + i * (ph + 0.02);
    fbox(A, F, a0 + 0.005, a1 - 0.005, yb, yb + ph, e + 0.01, e + 0.04, plank, { bv: 0.006, k: solid ? 1 : 0 });   // each plank solid: the gaps stay see-through
  }
}

// ============================================================================ 1. the container cabin
// Reference: building-container-metal (FBX), one sheet-steel cabin on a timber skirt, modelled ~1.8x over size;
// recreated x0.56: 7.84 x 3.34 m, 2.8 m floor to roof. One doorway (2.22 m) near the east end of the front with a rusty
// steel frame and header beam over a timber step; three window openings boarded with weathered planks in frames (one
// front, two rear); a slatted vent in the west gable; skirt boards in 1.76 m bays between posts; rust run-off from the roof.
export function containerCabin() {
  const X = 3.92, Z = 1.67, T = 0.12;
  const A = meta("rb1_container_cabin", "Abandoned site cabin", "Small buildings", "A sheet-steel site cabin on a timber skirt, painted olive and streaked with rust from the roof edge, its windows boarded with weathered planks, an open-framed doorway with a timber step and a slatted vent in the west end. Two rooms: an office and the entry store.", 1, [8.2, 3.8], { plinthDepth: 0.4, maxGrade: 0.6, noAutoPanels: true });
  mats(A);
  rbMats(A, [
    ["rb_cab_ext", [0.264, 0.226, 0.135], 0.62, "rb_ref_cab_wall", "metal"],   // the reference's own paint (CC BY 4.0, YadroGames)
    ["plaster", [0.32, 0.28, 0.17], 0.7, "rb_ref_cab_wall", "metal"],
    ["door_paint", [0.5, 0.47, 0.43], 0.85, "rb_planks", "wood"],         // the door leaf: weathered planks           // the painted steel lining (walls, ceiling, partition)
    ["terracotta", [0.42, 0.4, 0.37], 0.75, "rb_floor_old", "wood"],         // the plank floor
    ["stone_plinth", [0.42, 0.4, 0.36], 0.85, "rb_boards_grey", "wood"],     // the skirt's backing
    ["rb_skirt", [0.48, 0.45, 0.4], 0.85, "rb_boards_grey", "wood"],
    ["rb_board", [0.279, 0.232, 0.197], 0.85, "rb_ref_cab_boards", "wood"],   // the reference's window boarding
    ["rb_board_frame", [0.46, 0.44, 0.4], 0.85, "rb_boards_grey", "wood"],
    ["rb_iron", [0.45, 0.3, 0.2], 0.7, "rb_iron_rust", "metal"],
    ["rb_roof", [0.35, 0.34, 0.3], 0.6, "rb_cab_paint_b", "metal"],
    ["rb_rust_streak", [0.42, 0.22, 0.1], 0.8, "rb_rust", "metal"],
    ["rb_dark", [0.03, 0.03, 0.03], 0.9, null],
  ]);
  const ops = { front: [[-2.79, -1.68, GF + 1.2, GF + 2.06], [1.72, 2.92, GF, GF + 2.15]], rear: [[1.12, 2.23, GF + 1.2, GF + 2.06], [-2.79, -1.68, GF + 1.2, GF + 2.06]], west: [[-1.0, 1.0, GF + 1.0, GF + 2.2]], east: [] };
  const k = house(A, {
    X, Z, T, h: 2.8, n: 1, plinthDepth: 0.4, shot: "rb1_cabin", noRear: true, doorW: 1.2, doorH: 2.15, downpipes: false,
    style: () => ({ m: "rb_cab_ext", shutters: false, panel: true }),
    wd: () => ({ w: 1.11, h: 0.86, sill: 1.2 }),
    win: () => ({ front: [{ c: -2.23 }], rear: [{ c: 1.67 }, { c: -2.23 }] }),
    parts: () => [0.82], names: (f, r) => ["office", "entry"][r], hall: 1,
    roof: { type: "none", rise: 0 },
    dress: (A, k) => {
      const { EAVE } = k;
      // the flat roof: a steel deck with a rim and a slight fall to the rear
      A.box([-X - 0.08, EAVE, -Z - 0.08], [X + 0.08, EAVE + 0.08, Z + 0.08], "rb_roof", { bv: 0.01 });
      for (const [x0, x1, z0, z1] of [[-X - 0.1, X + 0.1, Z + 0.02, Z + 0.1], [-X - 0.1, X + 0.1, -Z - 0.1, -Z - 0.02], [X + 0.02, X + 0.1, -Z, Z], [-X - 0.1, -X - 0.02, -Z, Z]])
        A.box([x0, EAVE - 0.06, z0], [x1, EAVE + 0.14, z1], "rb_roof", { bv: 0.01, k: 0 });
      // the skirt: weathered boards in 1.76 m bays between posts, round the plinth
      for (const s of ["front", "rear", "east", "west"]) {
        const F = face(s, X, Z), L = s === "front" || s === "rear" ? X : Z;
        fbox(A, F, -L - 0.06, L + 0.06, -0.02, GF + 0.06, 0.07, 0.1, "rb_skirt", { bv: 0.006, k: 0 });
        for (let a = -L; a <= L + 0.01; a += (2 * L) / Math.max(1, Math.round((2 * L) / 1.76))) fbox(A, F, a - 0.05, a + 0.05, -0.02, GF + 0.06, 0.1, 0.16, "rb_skirt", { bv: 0.01, k: 0 });
      }
      // boarded windows (the openings stay; planks in frames close them)
      const Ff = face("front", X, Z), Fr = face("rear", X, Z), Fw = face("west", X, Z);
      boarded(A, Ff, -2.785, -1.675, GF + 1.2, GF + 2.06);
      boarded(A, Fr, 1.115, 2.225, GF + 1.2, GF + 2.06);
      boarded(A, Fr, -2.785, -1.675, GF + 1.2, GF + 2.06);
      // the doorway: rusty steel posts, the long header beam, a timber step
      const dc = k.fdx;
      for (const a of [dc - 0.67, dc + 0.67]) fbox(A, Ff, a - 0.04, a + 0.04, GF, GF + 2.2, 0.0, 0.09, "rb_iron", { bv: 0.008, k: 0 });
      fbox(A, Ff, dc - 1.13, dc + 1.13, GF + 2.2, GF + 2.45, 0.0, 0.03, "rb_iron", { bv: 0.008, k: 0 });
      fbox(A, Ff, dc - 0.67, dc + 0.67, -0.02, 0.16, 0.0, 0.3, "rb_board", { bv: 0.01 });
      for (let i = 0; i < 10; i++) fbox(A, Ff, dc - 1.1 + i * 0.24, dc - 1.08 + i * 0.24, GF + 2.25, GF + 2.4, 0.03, 0.035, "rb_dark", L1);   // rivets on the header
      // the slatted vent in the west gable: a frame, six slats, a dark void behind
      fbox(A, Fw, -1.0, 1.0, GF + 1.0, GF + 2.2, -0.002, 0.004, "rb_dark", K0);
      fbox(A, Fw, -1.03, 1.03, GF + 0.94, GF + 1.03, 0.0, 0.1, "rb_board_frame", { bv: 0.01, k: 0 });
      fbox(A, Fw, -1.03, 1.03, GF + 2.17, GF + 2.27, 0.0, 0.1, "rb_board_frame", { bv: 0.01, k: 0 });
      for (const a of [-1.03, 0.95]) fbox(A, Fw, a, a + 0.08, GF + 1.03, GF + 2.17, 0.0, 0.1, "rb_board_frame", { bv: 0.01, k: 0 });
      for (let i = 0; i < 6; i++) { const y = GF + 1.15 + i * 0.17; fbox(A, Fw, -0.94, 0.94, y, y + 0.045, 0.02, 0.06, "rb_board", { bv: 0.005, k: 0, r: undefined }); }
      fbox(A, Fw, -1.13, 1.13, GF + 2.32, GF + 2.56, 0.0, 0.04, "rb_iron", { bv: 0.008, k: 0 });
      // rust run-off from the roof edge, every side
      streaks(A, Ff, -X, X, EAVE, ops.front, 11, { n: 30, minL: 0.6, maxL: 2.4 });
      streaks(A, Fr, -X, X, EAVE, ops.rear, 23, { n: 30, minL: 0.6, maxL: 2.4 });
      streaks(A, Fw, -Z, Z, EAVE, ops.west, 37, { n: 12, minL: 0.6, maxL: 2.2 });
      streaks(A, face("east", X, Z), -Z, Z, EAVE, ops.east, 41, { n: 12, minL: 0.6, maxL: 2.2 });
    },
  });
  roomTypes(A, { office: "office", entry: "utility" });
  return A;
}

// ============================================================================ 2. the log house
// Reference: older_house_2.blend, a Russian izba at real size: 7.52 x 5.32 m log box of 0.26 m round logs with notched
// corners (the ends cross 0.25 m), log walls 0.75-3.1 m over a board-clad plinth, a steep board roof (ridge 5.58 m,
// along the long side) with board-clad gables, a rendered chimney through the rear slope, six windows in carved white
// surrounds with board shutters, one low door at the front under a small porch on posts.
const LOG = 0.26;
// horizontal logs over a face between a0..a1, rows from y0 to y1, cut round the openings; ends past the corners
function logCourses(A, F, a0, a1, y0, y1, ops, { off = 0, over = 0.25, m = "rb_log", end = "rb_log_end" } = {}) {
  const r = LOG / 2, isX = F.axis === "x";
  for (let y = y0 + r + off; y < y1 - r * 0.6; y += LOG) {
    const cuts = ops.filter(([b0, b1, c0, c1]) => y + r > c0 + 0.02 && y - r < c1 - 0.02).map(([b0, b1]) => [b0, b1]).sort((p, q) => p[0] - q[0]);
    const segs = []; let s = a0 - over;
    for (const [b0, b1] of cuts) { if (b0 - s > 0.15) segs.push([s, b0]); s = Math.max(s, b1); }
    if (a1 + over - s > 0.15) segs.push([s, a1 + over]);
    for (const [s0, s1] of segs) {
      const L = s1 - s0, c = (s0 + s1) / 2, d = F.d + F.out * 0.03;
      const pos = isX ? [c, y, d] : [d, y, c];
      A.cyl(pos, [LOG, L, LOG], m, { seg: 10, r: isX ? [0, 0, 90] : [90, 0, 0], bv: 0 });
      for (const e of [s0, s1]) {   // end grain on the cut faces and the corner ends
        const pe = isX ? [e + (e === s0 ? -0.006 : 0.006), y, d] : [d, y, e + (e === s0 ? -0.006 : 0.006)];
        A.cyl(pe, [LOG * 0.96, 0.012, LOG * 0.96], end, { seg: 10, r: isX ? [0, 0, 90] : [90, 0, 0], k: 0, lod: 1 });
      }
    }
  }
}
// a carved window surround (nalichnik): side boards, a sill apron, a head board under a peaked crown, open shutters
function nalichnik(A, F, c, w, y0, y1, { shutters = true } = {}) {
  const a0 = c - w / 2, a1 = c + w / 2, e = 0.08;
  for (const a of [a0 - 0.13, a1]) fbox(A, F, a, a + 0.13, y0 - 0.05, y1 + 0.05, e, e + 0.05, "rb_trim", { bv: 0.01, k: 0 });
  fbox(A, F, a0 - 0.2, a1 + 0.2, y0 - 0.18, y0 - 0.05, e, e + 0.1, "rb_trim", { bv: 0.012, k: 0 });                  // sill board
  fbox(A, F, a0 - 0.1, a1 + 0.1, y0 - 0.34, y0 - 0.18, e, e + 0.04, "rb_trim", { bv: 0.01, k: 0 });                  // apron
  for (let i = 0; i < 4; i++) { const a = a0 + (i + 0.5) * (w / 4); fbox(A, F, a - 0.03, a + 0.03, y0 - 0.42, y0 - 0.34, e, e + 0.04, "rb_trim", L1); }   // apron drops
  fbox(A, F, a0 - 0.22, a1 + 0.22, y1 + 0.05, y1 + 0.22, e, e + 0.08, "rb_trim", { bv: 0.012, k: 0 });               // head board
  const isX = F.axis === "x", d = F.d + F.out * (e + 0.03);
  const tri = [[a0 - 0.18, y1 + 0.22], [a1 + 0.18, y1 + 0.22], [c, y1 + 0.55]];
  A.prism(isX ? 2 : 0, d - 0.025, d + 0.025, isX ? tri : tri.map(([u, v]) => [u, v]), "rb_trim", { k: 0 });          // the peaked crown
  fbox(A, F, c - 0.05, c + 0.05, y1 + 0.3, y1 + 0.42, e + 0.05, e + 0.08, "rb_shutter", L1);                         // the carved boss
  if (shutters) for (const [s0, s1] of [[a0 - 0.13 - w / 2, a0 - 0.15], [a1 + 0.15, a1 + 0.13 + w / 2]]) {
    fbox(A, F, s0, s1, y0 - 0.02, y1 + 0.02, 0.18, 0.22, "rb_shutter", { bv: 0.008, k: 0 });
    for (const y of [y0 + 0.15, y1 - 0.15]) fbox(A, F, s0 + 0.03, s1 - 0.03, y - 0.03, y + 0.03, 0.22, 0.235, "rb_shutter", L1);   // ledges
  }
}
export function logHouse() {
  const X = 3.76, Z = 2.66, T = 0.26;
  const A = meta("rb1_log_house", "Abandoned log house", "Houses", "An old Russian log house (izba): round-log walls with notched corners on a board-clad plinth, a steep weathered board roof, a rendered chimney, carved white window surrounds with board shutters, a low front door under a small porch. Three rooms: the stove room, the entry kitchen and a store.", 1, [8.6, 7.6], { plinthDepth: 0.6, maxGrade: 0.6, noAutoPanels: true });
  mats(A);
  rbMats(A, [
    ["rb_log_wall", [0.38, 0.3, 0.24], 0.85, "rb_log", "wood"],
    ["rb_log", [0.42, 0.34, 0.27], 0.85, "rb_log", "wood"],
    ["rb_log_end", [0.62, 0.55, 0.45], 0.9, "rb_log_end", "wood"],
    ["rb_log_in", [0.48, 0.39, 0.31], 0.85, "rb_log", "wood"],
    ["plaster", [0.46, 0.38, 0.3], 0.85, "rb_log", "wood"],             // the log walls and the board ceiling inside
    ["terracotta", [0.4, 0.36, 0.31], 0.8, "rb_floor_old", "wood"],
    ["stone_plinth", [0.36, 0.33, 0.29], 0.85, "rb_planks", "wood"],
    ["rb_plinth", [0.4, 0.37, 0.33], 0.85, "rb_planks", "wood"],
    ["rb_roof_boards", [0.2, 0.16, 0.12], 0.85, "rb_planks", "wood"],
    ["rb_roof_boards_b", [0.3, 0.26, 0.21], 0.85, "rb_boards_grey", "wood"],
    ["rb_gable_boards", [0.42, 0.4, 0.37], 0.85, "rb_boards_grey", "wood"],
    ["rb_trim", [0.78, 0.77, 0.73], 0.7, "rb_wood_paint_old", "wood"],
    ["rb_shutter", [0.3, 0.27, 0.24], 0.85, "rb_planks", "wood"],
    ["rb_chimney", [0.7, 0.69, 0.66], 0.9, "rb_render", "plaster"],
    ["rb_post", [0.4, 0.34, 0.27], 0.85, "rb_beam", "wood"],
    ["rb_dark", [0.03, 0.03, 0.03], 0.9, null],
  ]);
  const winC = { front: [2.05, -2.03], rear: [2.12], east: [-1.16, 1.09], west: [-1.12, 1.12] };
  const WW = 1.0, WH = 0.95, SILL = 1.28;
  const k = house(A, {
    X, Z, T, h: 2.8, n: 1, plinthDepth: 0.6, shot: "rb1_log", noRear: true, doorW: 0.9, downpipes: false,
    style: () => ({ m: "rb_log_wall", shutters: false, panel: true, family: "timber" }),
    wd: () => ({ w: WW, h: WH, sill: SILL }),
    win: () => ({ front: winC.front.map((c) => ({ c })), rear: winC.rear.map((c) => ({ c })), east: winC.east.map((c) => ({ c })), west: winC.west.map((c) => ({ c })) }),
    parts: () => [-1.0, 1.93], names: (f, r) => ["stove_room", "entry_kitchen", "store"][r], hall: 1,
    roof: { type: "gable", rise: 2.48, m: "rb_roof_boards", ov: 0.6, gov: 0.5, gable: "rb_gable_boards" },
    dress: (A, k) => {
      const { EAVE, fdx } = k, y1 = GF + SILL, y2 = y1 + WH;
      const ops = { front: [...winC.front.map((c) => [c - WW / 2, c + WW / 2, y1, y2]), [fdx - 0.45, fdx + 0.45, GF, GF + 2.25]],
        rear: winC.rear.map((c) => [c - WW / 2, c + WW / 2, y1, y2]), east: winC.east.map((c) => [c - WW / 2, c + WW / 2, y1, y2]), west: winC.west.map((c) => [c - WW / 2, c + WW / 2, y1, y2]) };
      // log courses over every face; the side walls' logs half a log up (they interlock at the corners)
      for (const s of ["front", "rear"]) logCourses(A, face(s, X, Z), -X, X, 0.75, EAVE + 0.05, ops[s]);
      for (const s of ["east", "west"]) logCourses(A, face(s, X, Z), -Z, Z, 0.75, EAVE + 0.05, ops[s], { off: LOG / 2 });
      // inside: the same log courses on the inner faces of the outer walls (the reference's interior is all log)
      const XI = X - T, ZI = Z - T;
      for (const [s, L, off] of [["front", XI, 0], ["rear", XI, 0], ["east", ZI, LOG / 2], ["west", ZI, LOG / 2]]) {
        const Fo = face(s, X, Z), Fi = { axis: Fo.axis, d: Fo.d - Fo.out * T, out: -Fo.out };
        logCourses(A, Fi, -L, L, GF, EAVE - 0.2, ops[s], { off, over: -0.12, m: "rb_log_in", end: "rb_log_in" });
      }
      // an attic hatch of vertical boards in each gable
      for (const s of ["east", "west"]) {
        const F = face(s, X, Z);
        fbox(A, F, -0.35, 0.35, EAVE + 0.75, EAVE + 1.55, 0.02, 0.06, "rb_shutter", { bv: 0.008, k: 0 });
        for (const a of [-0.42, 0.35]) fbox(A, F, a, a + 0.07, EAVE + 0.7, EAVE + 1.6, 0.02, 0.08, "rb_trim", { bv: 0.008, k: 0 });
        for (const y of [EAVE + 0.65, EAVE + 1.55]) fbox(A, F, -0.42, 0.42, y, y + 0.07, 0.02, 0.08, "rb_trim", { bv: 0.008, k: 0 });
      }
      // the plinth: vertical boards from the ground to the first log
      for (const s of ["front", "rear", "east", "west"]) {
        const F = face(s, X, Z), L = s === "front" || s === "rear" ? X : Z;
        fbox(A, F, -L - 0.08, L + 0.08, -0.05, 0.76, 0.07, 0.1, "rb_plinth", { bv: 0.006, k: 0 });
      }
      // carved surrounds and shutters
      for (const s of ["front", "rear", "east", "west"]) for (const c of winC[s]) nalichnik(A, face(s, X, Z), c, WW, y1, y2, { shutters: s !== "rear" });
      // the porch: a board deck at the floor with a step, two posts, a small board lean-to roof
      const zf = Z, pd = 1.2, px0 = fdx - 0.7, px1 = fdx + 0.7;
      A.box([px0, -0.05, zf + 0.1], [px1, GF, zf + pd], "rb_plinth", { bv: 0.01 });
      A.box([fdx - 0.5, -0.05, zf + pd], [fdx + 0.5, GF / 2, zf + pd + 0.32], "rb_plinth", { bv: 0.01 });
      for (const x of [px0 + 0.07, px1 - 0.07]) A.box([x - 0.07, GF, zf + pd - 0.14], [x + 0.07, 2.62, zf + pd], "rb_post", { bv: 0.012 });
      A.box([px0, 2.55, zf + pd - 0.16], [px1, 2.68, zf + pd + 0.02], "rb_post", { bv: 0.01 });
      A.obox([fdx, 2.86, zf + pd / 2 + 0.05], [px1 - px0 + 0.4, 0.06, pd + 0.45], [-18, 0, 0], "rb_roof_boards", { bv: 0.008, k: 0 });
      // the board roof: weathered boards laid down both slopes over the deck, ragged at the eaves (the reference's look)
      const th = Math.atan2(k.RIDGE - EAVE, Z), deg = th * 180 / Math.PI, ov = 0.6, gov = 0.5, t = 0.14;
      for (const sg of [1, -1]) {
        let i = 0;
        for (let x = -X - gov + 0.11; x < X + gov - 0.05; x += 0.215, i++) {
          const ext = rnd(sg > 0 ? 71 : 83, i) * 0.22, z0 = -0.05 * sg, z1 = sg * (Z + ov + ext), zm = (z0 + z1) / 2;
          const len = Math.abs(z1 - z0) / Math.cos(th), ym = k.RIDGE + (t + 0.025) / Math.cos(th) - Math.abs(zm) * Math.tan(th);
          A.obox([x, ym, zm], [0.2 - rnd(97, i) * 0.03, 0.025, len], [sg > 0 ? deg : -deg, 0, 0], i % 5 === 3 ? "rb_roof_boards_b" : "rb_roof_boards", { k: 0, bv: 0.004 });
        }
      }
      // the chimney: rendered, through the rear slope, a projecting cap
      const cx = -0.82, cz = -1.135;
      A.box([cx - 0.26, EAVE - 0.2, cz - 0.285], [cx + 0.26, 5.85, cz + 0.285], "rb_chimney", { bv: 0.015 });
      A.box([cx - 0.33, 5.75, cz - 0.355], [cx + 0.33, 5.85, cz + 0.355], "rb_chimney", { bv: 0.01, k: 0 });
      A.box([cx - 0.15, 5.85, cz - 0.15], [cx + 0.15, 5.86, cz + 0.15], "rb_dark", L1);
    },
  });
  roomTypes(A, { stove_room: "sitting", entry_kitchen: "kitchen", store: "utility" });
  return A;
}

// ============================================================================ 3. the weapon shop
// Reference: abandoned__building__shop__old__house.glb (Sketchfab GLB with a ground plane), a weatherboard shop under a
// skillion of corrugated sheet, modelled ~2x over size; recreated x0.5 and turned so its door gable is the front:
// 7 x 10 m, walls to 3.65 m, the roof rising to 4.85 m over the counter side, where a band of wire-mesh clerestory
// panels sits above the wall and a timber verandah on rusted steel posts runs 3 m out over green steel counters with
// boarded hatches, wrapping round the front corner. The front gable has an orange double door under a WEAPONS sign;
// the low side two white double windows. Inside: the shop floor between a store and the counter room.
import { ntText } from "./nt7_kit.mjs";
function meshPanel(A, F, a0, a1, y0, y1, e, sp = 0.12, back = true) {
  if (back) fbox(A, F, a0, a1, y0, y1, e - 0.12, e - 0.1, "rb_dark", K0);             // the dark void behind a mesh in a wall (not a free gate)
  for (let a = a0 + sp; a < a1 - 0.02; a += sp) fbox(A, F, a - 0.007, a + 0.007, y0, y1, e, e + 0.012, "rb_mesh", L1);
  for (let y = y0 + sp; y < y1 - 0.02; y += sp) fbox(A, F, a0, a1, y - 0.007, y + 0.007, e + 0.012, e + 0.024, "rb_mesh", L1);
  for (const a of [a0, a1 - 0.06]) fbox(A, F, a, a + 0.06, y0, y1, e - 0.02, e + 0.05, "rb_frame_wood", { bv: 0.008, k: 0 });
  for (const y of [y0, y1 - 0.06]) fbox(A, F, a0, a1, y, y + 0.06, e - 0.02, e + 0.05, "rb_frame_wood", { bv: 0.008, k: 0 });
}
export function weaponShop() {
  const X = 3.5, Z = 5.0, T = 0.15, HI = 4.85;
  const A = meta("rb1_weapon_shop", "Abandoned weapon shop", "Small buildings", "An abandoned weatherboard weapon shop under a corrugated skillion roof: a band of wire-mesh clerestory panels and a timber verandah on rusted steel posts over green steel counters with boarded hatches along the high side, an orange double door under a WEAPONS sign in the front gable, two white double windows in the low side. Inside: the shop floor between a store and the counter room.", 1, [13.4, 14.6], { plinthDepth: 0.5, maxGrade: 0.6, noAutoPanels: true });
  mats(A);
  rbMats(A, [
    ["rb_siding", [0.366, 0.293, 0.205], 0.85, "rb_ref_shop_wall", "wood"],    // the reference's own weatherboard (CC BY 4.0, YadroGames)
    ["plaster", [0.62, 0.58, 0.5], 0.85, "rb_siding_b", "wood"],
    ["terracotta", [0.36, 0.3, 0.25], 0.8, "rb_floor_dirty", "wood"],
    ["stone_plinth", [0.42, 0.4, 0.37], 0.9, "rb_render", "plaster"],
    ["rb_roof", [0.329, 0.299, 0.241], 0.6, "rb_ref_shop_roof", "metal"],
    ["rb_roof_patch", [0.46, 0.27, 0.15], 0.75, "rb_rust", "metal"],
    ["rb_ver_roof", [0.4, 0.36, 0.3], 0.85, "rb_planks", "wood"],
    ["rb_post", [0.42, 0.3, 0.22], 0.7, "rb_steel_rusty", "metal"],
    ["rb_green", [0.24, 0.299, 0.143], 0.7, "rb_ref_shop_green", "metal"],  // the reference's counter panels (their own colour: a neutral tint)
    ["rb_orange", [0.78, 0.78, 0.78], 0.7, "rb_orange", "metal"],
    ["rb_frame_wood", [0.42, 0.37, 0.3], 0.85, "rb_beam", "wood"],
    ["rb_mesh", [0.3, 0.29, 0.27], 0.6, "rb_iron_rust", "metal"],
    ["rb_trim", [0.78, 0.77, 0.73], 0.7, "rb_wood_paint_old", "wood"],
    ["rb_board", [0.5, 0.47, 0.43], 0.85, "rb_planks", "wood"],
    ["rb_board_frame", [0.46, 0.44, 0.4], 0.85, "rb_boards_grey", "wood"],
    ["rb_sign", [0.324, 0.209, 0.17], 0.6, "rb_ref_shop_sign", "plain"],      // the reference's WEAPON sign board
    ["rb_sign_red", [0.6, 0.08, 0.06], 0.6, null],
    ["rb_rust_streak", [0.42, 0.22, 0.1], 0.8, "rb_rust", "metal"],
    ["rb_dark", [0.03, 0.03, 0.03], 0.9, null],
    ["rb_damp", [0.13, 0.11, 0.08], 0.95, "rb_boards_grey", "wood"],
    ["rb_board_new", [0.46, 0.38, 0.27], 0.85, "rb_siding_b", "wood"],
    ["rb_board_old", [0.24, 0.21, 0.17], 0.9, "rb_planks", "wood"],
    ["rb_roof_b", [0.38, 0.24, 0.14], 0.7, "rb_ref_shop_roof", "metal"],
    ["rb_roof_c", [0.27, 0.27, 0.26], 0.6, "rb_ref_shop_roof", "metal"],
    ["rb_crate", [0.36, 0.29, 0.2], 0.85, "rb_planks", "wood"],
    ["rb_drum", [0.36, 0.18, 0.09], 0.7, "rb_rust", "metal"],
    ["rb_tyre", [0.04, 0.04, 0.045], 0.9, null],
  ]);
  const hatches = [-3.0, 0.0, 3.0];
  const k = house(A, {
    X, Z, T, h: 3.35, n: 1, plinthDepth: 0.5, shot: "rb1_shop", noRear: true, doorW: 1.6, frontDouble: true, doorH: 2.15, downpipes: false,
    style: () => ({ m: "rb_siding", shutters: false, panel: true, family: "timber" }),
    wd: () => ({ w: 1.42, h: 1.25, sill: 0.87 }),
    win: () => ({ west: [{ c: -1.88 }, { c: 2.12 }], east: hatches.map((c) => ({ c, w: 1.6, h: 0.9, sill: 1.05 })) }),
    parts: () => [], names: () => "shop", hall: 0,
    roof: { type: "none", rise: HI - (GF + 3.35) },
    dress: (A, k) => {
      const { EAVE } = k, rise = HI - EAVE, ang = Math.atan2(rise, 2 * X + 0.6) * 180 / Math.PI;
      const Fe = face("east", X, Z), Ff = face("front", X, Z), Fw = face("west", X, Z), Fr = face("rear", X, Z);
      // the skillion: corrugated sheet from the low (west) eave to the high (east) one, patched
      const L = Math.hypot(2 * X + 0.6, rise);
      A.obox([0, (EAVE + HI) / 2 + 0.06, 0], [L, 0.06, 2 * Z + 0.5], [0, 0, ang], "rb_roof", { bv: 0.005 });
      for (const [pz, pw, pl] of [[-2.2, 1.0, 1.4], [1.6, 0.8, 1.0], [3.4, 1.2, 0.9]]) A.obox([0.6, (EAVE + HI) / 2 + 0.1 + 0.6 * Math.tan(ang * Math.PI / 180), pz], [pl, 0.012, pw], [0, 0, ang], "rb_roof_patch", L1);
      // the gable infills between the eave and the slope (front and rear), weatherboard
      for (const F of [Ff, Fr]) { const d = F.d + F.out * 0.0; A.prism(2, Math.min(d, d - F.out * T), Math.max(d, d - F.out * T), [[-X, EAVE], [X, EAVE], [X, HI]], "rb_siding", {}); }
      // the clerestory band on the high (east) side: mesh panels in timber frames between the wall top and the roof
      for (let i = 0; i < 5; i++) { const a0 = -Z + i * 2.0; meshPanel(A, Fe, a0 + 0.02, a0 + 1.98, EAVE, HI - 0.05, 0.0); }
      // green steel counter panelling below the hatches, the hatches boarded
      fbox(A, Fe, -Z, Z, 0.0, 1.0, 0.0, 0.04, "rb_green", { bv: 0.008, k: 0 });
      for (const c of hatches) fbox(A, Fe, c - 1.0, c + 1.0, 0.95, 1.02, 0.0, 0.55, "rb_frame_wood", { bv: 0.01 });   // counter boards
      // the verandah: rusted steel posts 3 m out, a plank lean-to from the clerestory sill down to them, wrapping the front corner
      const vx = X + 3.0, vy0 = EAVE - 0.5, vy1 = 2.46;
      for (let z = -Z; z <= Z + 2.01; z += 2.5) A.box([vx - 0.06, 0, z - 0.06], [vx + 0.06, vy1, z + 0.06], "rb_post", { bv: 0.01 });
      A.box([vx - 0.06, vy1 - 0.12, -Z - 0.1], [vx + 0.06, vy1, Z + 2.1], "rb_post", { bv: 0.008 });
      const vL = Math.hypot(3.1, vy0 - vy1), va = Math.atan2(vy0 - vy1, 3.1) * 180 / Math.PI;
      A.obox([X + 1.55, (vy0 + vy1) / 2 + 0.05, 0.5], [vL, 0.05, 2 * Z + 3.2], [0, 0, -va], "rb_ver_roof", { bv: 0.005 });
      for (let z = -Z + 0.6; z < Z + 2.0; z += 1.2) A.obox([X + 1.55, (vy0 + vy1) / 2 - 0.04, z], [vL, 0.1, 0.07], [0, 0, -va], "rb_frame_wood", L1);   // rafters
      // the corner porch beside the door: posts, a lean-to, a framed wire-mesh gate (clear of the door and its sign)
      const gx0 = 1.2;
      A.box([gx0, vy1 - 0.12, Z + 1.95], [vx + 0.06, vy1, Z + 2.07], "rb_post", { bv: 0.008 });
      A.box([gx0, 0, Z + 1.95], [gx0 + 0.12, vy1, Z + 2.07], "rb_post", { bv: 0.008 });
      A.obox([(gx0 + vx) / 2, vy1 + 0.25, Z + 1.0], [vx - gx0, 0.05, 2.3], [16, 0, 0], "rb_ver_roof", { bv: 0.005 });
      meshPanel(A, { axis: "x", d: Z + 2.0, out: 1 }, gx0 + 0.14, vx - 0.08, 0.1, 1.9, 0.0, 0.1, false);
      A.box([gx0 + 0.12, 0.0, Z + 1.96], [vx - 0.06, 1.95, Z + 2.04], "rb_dark", { v: 0 });   // the mesh gate is solid (an invisible collider)
      // the door: orange leaves (house() door, re-skinned below), a frame, the WEAPONS sign board above
      fbox(A, Ff, -1.0, 1.0, GF + 2.2, GF + 2.65, 0.02, 0.07, "rb_sign", { bv: 0.01, k: 0 });
      for (const a of [-0.88, 0.88]) fbox(A, Ff, a - 0.06, a + 0.06, GF, GF + 2.2, 0.0, 0.06, "rb_frame_wood", { bv: 0.008, k: 0 });
      // white window frames on the low side
      for (const c of [-1.88, 2.12]) {
        for (const a of [c - 0.78, c + 0.71]) fbox(A, Fw, a, a + 0.07, GF + 0.8, GF + 2.2, 0.0, 0.06, "rb_trim", { bv: 0.008, k: 0 });
        for (const y of [GF + 0.8, GF + 2.13]) fbox(A, Fw, c - 0.78, c + 0.78, y, y + 0.07, 0.0, 0.06, "rb_trim", { bv: 0.008, k: 0 });
      }
      // inside: a long timber counter down the counter side, the loft over the rear third on beams and a post
      A.box([X - T - 0.75, GF, -3.2], [X - T - 0.15, GF + 1.0, 3.2], "rb_frame_wood", { bv: 0.01 });
      A.box([X - T - 0.8, GF + 1.0, -3.25], [X - T - 0.1, GF + 1.05, 3.25], "rb_board", { bv: 0.008 });
      const ly = GF + 2.45, lz = -Z + T + 3.0;
      A.box([-X + T, ly, -Z + T], [X - T, ly + 0.08, lz], "rb_board", { bv: 0.008 });
      for (let x = -X + T + 0.3; x < X - T; x += 0.9) A.box([x - 0.07, ly - 0.2, -Z + T], [x + 0.07, ly, lz + 0.05], "rb_frame_wood", { bv: 0.01 });
      A.box([-X + T, ly - 0.24, lz - 0.08], [X - T, ly, lz + 0.08], "rb_frame_wood", { bv: 0.01 });
      A.box([-0.08, GF, lz - 0.08], [0.08, ly - 0.24, lz + 0.08], "rb_frame_wood", { bv: 0.01 });
      // ---- RB1 finish (against the reference renders): facade depth, weathering variation, roof variety, clutter
      // rusted steel columns either side of the door gable (the reference's uprights), corner boards, eave fascias
      for (const x of [-2.0, 2.0]) A.box([x - 0.09, 0, Z + 0.02], [x + 0.09, 2.9, Z + 0.2], "rb_post", { bv: 0.01 });
      A.box([-2.1, 2.84, Z + 0.02], [2.1, 2.96, Z + 0.22], "rb_post", { bv: 0.008, k: 0 });
      for (const [cx, cz] of [[-X, -Z], [X, -Z], [-X, Z], [X, Z]]) {
        A.box([cx - 0.09, 0.0, cz - 0.09], [cx + 0.09, cx > 0 ? HI : EAVE, cz + 0.09], "rb_frame_wood", { bv: 0.01, k: 0 });
      }
      for (const [x0, x1, y] of [[-X - 0.12, -X + 0.02, EAVE - 0.05], [X - 0.02, X + 0.12, HI - 0.05]]) A.box([x0, y - 0.22, -Z - 0.3], [x1, y, Z + 0.3], "rb_frame_wood", { bv: 0.01, k: 0 });
      // the damp band at the wall foot, ragged on top; a few replaced boards in another tone; mould under the hatches
      const wo = { front: [[-1.15, 1.15, 0.0, GF + 2.6], [-2.15, -1.85, 0.0, 3.0], [1.85, 2.15, 0.0, 3.0]], rear: [],
        west: [[-2.7, -1.06, GF + 0.75, GF + 2.25], [1.3, 2.94, GF + 0.75, GF + 2.25]], east: hatches.map((c) => [c - 0.95, c + 0.95, GF + 0.95, GF + 2.05]) };
      const ok = (o, a0, a1, y0, y1) => [a0, (a0 + a1) / 2, a1].every((a) => clear(o, a, y0, y1, 0.05));
      for (const [F, L, seed, o] of [[Ff, X, 3, wo.front], [Fr, X, 5, wo.rear], [Fw, Z, 7, wo.west], [Fe, Z, 9, wo.east]]) {
        for (let a = -L; a < L - 0.05; a += 0.5) { const h = 0.25 + rnd(seed, Math.round(a * 10 + 100)) * 0.45, a1 = Math.min(a + 0.5, L); if (ok(o, a, a1, 0.02, h)) fbox(A, F, a, a1, 0.02, h, 0.003, 0.006, "rb_damp", L1); }
        for (let k = 0; k < 4; k++) { const a = -L + 0.6 + rnd(seed + 11, k) * (2 * L - 1.6), y = 0.9 + rnd(seed + 13, k) * 2.0, w = 0.8 + rnd(seed + 17, k) * 1.4, a1 = Math.min(a + w, L - 0.1);
          if (ok(o, a, a1, y, y + 0.19)) fbox(A, F, a, a1, y, y + 0.19, 0.004, 0.008, k % 2 ? "rb_board_new" : "rb_board_old", L1); }
      }
      for (const c of hatches) fbox(A, Fe, c - 0.7, c + 0.7, 1.02, 1.32, 0.045, 0.048, "rb_damp", L1);
      // the roof: sheets of different weathering laid over the deck, one lifted at the low eave
      for (let k = 0; k < 9; k++) {
        const z = -Z + 0.6 + k * 1.15 + (rnd(29, k) - 0.5) * 0.2, m = ["rb_roof_b", "rb_roof_c", "rb_roof"][k % 3];
        A.obox([0.0, (EAVE + HI) / 2 + 0.105, z], [L * 0.98, 0.012, 1.0 + rnd(31, k) * 0.15], [0, 0, ang], m, { k: 0, lod: 1 });
      }
      A.obox([-X + 0.4, EAVE + 0.32, 1.3], [1.5, 0.015, 1.0], [0, 0, ang + 9], "rb_roof_c", { k: 0 });
      // inside: board shelving on the low side with a few boxes, a gun rack board with pegs on the rear wall
      for (const y of [GF + 0.35, GF + 1.05, GF + 1.75]) A.box([-X + T, y, -0.9], [-X + T + 0.45, y + 0.04, 1.1], "rb_board", { bv: 0.006 });   // between the windows
      for (const z of [-0.9, 0.1, 1.1]) A.box([-X + T, GF, z - 0.03], [-X + T + 0.45, GF + 2.0, z + 0.03], "rb_frame_wood", { bv: 0.006 });
      A.box([-1.6, GF + 1.0, -Z + T], [1.6, GF + 1.9, -Z + T + 0.04], "rb_board", { bv: 0.006, k: 0 });
      for (let i = 0; i < 9; i++) A.box([-1.45 + i * 0.36, GF + 1.25, -Z + T + 0.04], [-1.42 + i * 0.36, GF + 1.31, -Z + T + 0.16], "rb_post", { k: 0, lod: 1 });
      // stains: rust run-off under the clerestory and from the counters
      streaks(A, Fe, -Z, Z, 1.0, [], 53, { n: 10, minL: 0.2, maxL: 0.6 });
      streaks(A, Fw, -Z, Z, EAVE, [[-2.66, -1.1, GF + 0.8, GF + 2.2], [1.34, 2.9, GF + 0.8, GF + 2.2]], 61, { n: 8, m: "rb_dark_stain", minL: 0.4, maxL: 1.4 });
    },
  });
  // the door leaves: the reference's orange rusted leaf (the door_paint material is the leaves' own)
  rbMats(A, [["door_paint", [0.39, 0.169, 0.088], 0.7, "rb_ref_shop_door", "metal"], ["rb_dark_stain", [0.2, 0.18, 0.15], 0.9, "rb_boards_grey", "wood"]]);
  roomTypes(A, { shop: "shop" });
  return A;
}

export function rb1Assets() {
  return [containerCabin(), logHouse(), weaponShop()];
}
