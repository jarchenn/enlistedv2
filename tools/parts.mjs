// Reusable architectural and furniture components for Norman buildings.
import { P, wbox, cutRect, swingSign, rotY } from "./kit.mjs";
// GX4: geometry the exterior pass (tools/exterior.mjs) refines; not serialised (Asset.json() lists its own fields)
export const extOf = (A) => (A._ext ??= { sills: [], timber: [], hips: [], gables: [], cornices: [] });

export function stdMaterials(A) {
  // albedo values are linear-space
  A.mat("stone", [0.40, 0.36, 0.285], 0.88, "rubble");   // patterns 17-22: docs/interiors.md (tools/retarget_materials.py)
  A.mat("stone_dressed", [0.52, 0.48, 0.38], 0.82, "ashlar");
  A.mat("stone_plinth", [0.27, 0.255, 0.22], 0.9, "rubble");
  A.mat("hearthstone", [0.22, 0.21, 0.19], 0.8, "stone");
  A.mat("plaster", [0.64, 0.61, 0.54], 0.93, "interior", { grain: 0 });
  A.mat("plaster_smoke", [0.50, 0.46, 0.39], 0.93, "interior", { grain: 6 });
  A.mat("soot", [0.025, 0.022, 0.02], 0.97, "plain");
  A.mat("oak_x", [0.15, 0.095, 0.055], 0.72, "wood", { grain: 0 });
  A.mat("oak_y", [0.15, 0.095, 0.055], 0.72, "wood", { grain: 1 });
  A.mat("oak_z", [0.15, 0.095, 0.055], 0.72, "wood", { grain: 2 });
  A.mat("floor_x", [0.24, 0.16, 0.095], 0.62, "floorfin", { grain: 0 });
  A.mat("tread_x", [0.20, 0.13, 0.075], 0.55, "wood", { grain: 0 });
  A.mat("cherry_x", [0.26, 0.11, 0.06], 0.55, "wood", { grain: 0 });
  A.mat("cherry_y", [0.26, 0.11, 0.06], 0.55, "wood", { grain: 1 });
  A.mat("cherry_z", [0.26, 0.11, 0.06], 0.55, "wood", { grain: 2 });
  A.mat("bark", [0.10, 0.075, 0.05], 0.95, "wood", { grain: 0 });
  A.mat("door_paint", [0.085, 0.14, 0.11], 0.62, "painted", { grain: 1 });
  A.mat("shutter_paint", [0.17, 0.25, 0.25], 0.66, "painted", { grain: 1 });
  A.mat("window_paint", [0.58, 0.56, 0.50], 0.5, "painted", { grain: 1 });
  A.mat("slate", [0.055, 0.062, 0.07], 0.55, "slate", { grain: 2 });
  A.mat("ridge_tile", [0.30, 0.12, 0.06], 0.7, "plain");
  A.mat("terracotta", [0.36, 0.14, 0.075], 0.72, "floorfin", { grain: 2 });
  A.mat("iron", [0.05, 0.048, 0.046], 0.55, "metal", { metallic: 0.15 });   // black-painted wrought iron
  A.mat("cast_iron", [0.055, 0.053, 0.05], 0.65, "metal", { metallic: 0.25 });
  A.mat("zinc", [0.52, 0.53, 0.55], 0.5, "metal", { metallic: 1.0 });
  A.mat("brass", [0.62, 0.44, 0.18], 0.32, "metal", { metallic: 1.0 });
  A.mat("copper", [0.65, 0.32, 0.18], 0.38, "metal", { metallic: 1.0 });
  A.mat("glass", [0.8, 0.85, 0.82], 0.05, "glass");
  A.mat("linen", [0.72, 0.69, 0.61], 0.9, "fabric");
  A.mat("ticking", [0.50, 0.47, 0.40], 0.9, "fabric");
  A.mat("wool_red", [0.26, 0.05, 0.035], 0.95, "fabric");
  A.mat("wool_green", [0.06, 0.10, 0.06], 0.95, "fabric");
  A.mat("faience", [0.72, 0.70, 0.62], 0.22, "plain");
  A.mat("faience_blue", [0.12, 0.20, 0.42], 0.22, "plain");
  A.mat("rush", [0.42, 0.33, 0.16], 0.9, "fabric");
  A.mat("concrete", [0.33, 0.32, 0.29], 0.9, "concrete");
  A.mat("concrete_wash", [0.50, 0.49, 0.45], 0.92, "concrete");
  A.mat("concrete_floor", [0.24, 0.235, 0.22], 0.8, "concrete");
  A.mat("steel_plate", [0.13, 0.14, 0.12], 0.6, "metal", { metallic: 0.0 });   // painted steel (field grey): dielectric paint
  A.mat("steel_bare", [0.56, 0.57, 0.58], 0.38, "metal", { metallic: 1.0 });
  A.mat("turf", [0.085, 0.12, 0.04], 0.98, "plain");
  A.mat("earth", [0.12, 0.09, 0.06], 1.0, "plain");
  A.mat("log_x", [0.16, 0.12, 0.08], 0.9, "wood", { grain: 0 });
  A.mat("log_z", [0.16, 0.12, 0.08], 0.9, "wood", { grain: 2 });
  A.mat("plank_y", [0.26, 0.19, 0.12], 0.8, "wood", { grain: 1 });
  A.mat("plank_x", [0.26, 0.19, 0.12], 0.8, "wood", { grain: 0 });
  A.mat("plank_z", [0.26, 0.19, 0.12], 0.8, "wood", { grain: 2 });
  A.mat("render", [0.58, 0.53, 0.43], 0.92, "limewash");
  A.mat("brick", [0.25, 0.13, 0.09], 0.86, "brickwork");   // the schoolhouse walls (was undefined: default grey)
  A.mat("crate_olive", [0.11, 0.12, 0.06], 0.7, "painted", { grain: 0 });
  A.mat("canvas", [0.30, 0.28, 0.20], 0.95, "fabric");
  A.mat("paper", [0.68, 0.64, 0.52], 0.9, "plain");
  A.mat("blackboard", [0.04, 0.05, 0.045], 0.8, "plain");
  A.mat("sacking", [0.40, 0.32, 0.20], 0.95, "fabric");
  A.mat("rubble_core", [0.30, 0.27, 0.215], 0.97, "stone");
  A.mat("concrete_core", [0.27, 0.26, 0.24], 0.97, "concrete");
  A.mat("splinter", [0.33, 0.24, 0.15], 0.9, "wood", { grain: 1 });
}

// --------------------------------------------------------------------------- walls
// A two-layer wall: rubble stone outside, lime plaster inside, with real openings.
// out: +1/-1 = direction of the exterior along the wall's depth axis.
// panel: optional destructible bays { side, family, bands:[[b0, b1, floorIdx, walkY]], collar, minW, maxW, core }
//   Each bay between openings (kept clear of door/window frames by `collar`) becomes ONE logical damage panel
//   owning both layers, with intact / damaged / breached geometry and collision. Everything else stays in the
//   immutable core (corners, frame collars, lintels, spandrels, gables).
export function wall(A, { axis, outer, inner, aOut, aIn, y0, y1, holes, stone = "stone", plaster = "plaster", pt = 0.03, panel = null }) {
  const out = Math.sign(outer - inner);
  const mid = inner + out * pt;
  const rects = panel ? panelRects(aIn, holes, panel) : [];
  const coreHoles = holes.concat(rects.map((r) => ({ a0: r.a0, a1: r.a1, y0: r.b0, y1: r.b1 })));
  for (const [a0, a1, ya, yb] of cutRect(aOut[0], aOut[1], y0, y1, coreHoles)) wbox(A, axis, a0, a1, ya, yb, mid, outer, stone);
  for (const [a0, a1, ya, yb] of cutRect(aIn[0], aIn[1], y0, y1, coreHoles)) wbox(A, axis, a0, a1, ya, yb, inner, mid, plaster);
  for (const r of rects) emitPanel(A, { axis, outer, inner, mid, stone, plaster, r, panel });
}

function panelRects(aIn, holes, p) {
  const collar = p.collar ?? 0.32, minW = p.minW ?? 1.0, maxW = p.maxW ?? 2.7, out = [];
  for (const [b0, b1, floor, walkY] of p.bands) {
    const block = holes.filter((h) => h.y1 > b0 + 0.05 && h.y0 < b1 - 0.05).map((h) => [h.a0 - collar, h.a1 + collar]).sort((u, v) => u[0] - v[0]);
    let a = aIn[0] + (p.edge ?? 0.05);
    const end = aIn[1] - (p.edge ?? 0.05), spans = [];
    for (const [h0, h1] of block) { if (h0 > a) spans.push([a, Math.min(h0, end)]); a = Math.max(a, h1); }
    if (a < end) spans.push([a, end]);
    for (const [s0, s1] of spans) {
      const w = s1 - s0;
      if (w < minW) continue;
      const n = Math.ceil(w / maxW);
      for (let i = 0; i < n; i++) out.push({ a0: s0 + (w * i) / n, a1: s0 + (w * (i + 1)) / n, b0, b1, floor, walkY: walkY ?? b0 });
    }
  }
  // bay numbers count along the wall per floor, from the low-a end
  const per = {};
  for (const r of out) { per[r.floor] = (per[r.floor] || 0) + 1; r.bay = per[r.floor]; }
  return out;
}

// deterministic per-panel random numbers (stable across generator runs)
function rng(seedStr) {
  let h = 2166136261;
  for (let i = 0; i < seedStr.length; i++) { h ^= seedStr.charCodeAt(i); h = Math.imul(h, 16777619); }
  return () => { h ^= h << 13; h ^= h >>> 17; h ^= h << 5; return ((h >>> 0) % 100000) / 100000; };
}

const FAMILY = {
  masonry: { hp: 200, core: "rubble_core" },
  concrete: { hp: 450, core: "concrete_core" },
  timber: { hp: 90, core: "splinter" },
};

function emitPanel(A, { axis, outer, inner, mid, stone, plaster, r, panel }) {
  const fam = FAMILY[panel.family] || FAMILY.masonry;
  const id = `floor-${String(r.floor).padStart(2, "0")}/${panel.side}/bay-${String(r.bay).padStart(2, "0")}`;
  const R = rng(A.meta.id + "/" + id);
  const out = Math.sign(outer - inner);
  const st = [[], [], []];
  const layer = (rects, into) => {
    for (const [a0, a1, ya, yb] of rects) {
      wbox(A, axis, a0, a1, ya, yb, mid, outer, stone, { into });
      wbox(A, axis, a0, a1, ya, yb, inner, mid, plaster, { into });
    }
  };
  const full = [[r.a0, r.a1, r.b0, r.b1]];
  const w = r.a1 - r.a0, h = r.b1 - r.b0, ac = (r.a0 + r.a1) / 2;
  // ---- intact
  layer(full, st[0]);
  // ---- damaged: shell crater in the exterior skin, interior intact, collision unchanged in practice
  {
    const sw = Math.min(1.3, w * 0.55), sh = Math.min(1.0, h * 0.32), sy = r.walkY + Math.min(1.25, h * 0.42);
    const scar = { a0: ac - sw / 2, a1: ac + sw / 2, y0: sy - sh / 2, y1: sy + sh / 2 };
    for (const [a0, a1, ya, yb] of cutRect(r.a0, r.a1, r.b0, r.b1, [scar])) wbox(A, axis, a0, a1, ya, yb, mid, outer, stone, { into: st[1] });
    wbox(A, axis, scar.a0, scar.a1, scar.y0, scar.y1, mid, outer - out * 0.12, fam.core, { into: st[1] });
    wbox(A, axis, r.a0, r.a1, r.b0, r.b1, inner, mid, plaster, { into: st[1] });
    for (let i = 0; i < 6; i++) {   // chipped dressing around the crater
      const a = scar.a0 + R() * sw, y = scar.y0 + R() * sh, s = 0.08 + R() * 0.12;
      const p = axis === "x" ? [a, y, outer - out * 0.05] : [outer - out * 0.05, y, a];
      A.obox(p, [s, s * 0.8, s], [R() * 40, R() * 90, R() * 40], fam.core, { into: st[1], k: 0 });
    }
  }
  // ---- breached: irregular opening built from stacked ragged rows, exposed wall core, rubble at the foot
  {
    const holeW = Math.max(0.5, Math.min(w - 0.45, 1.7));
    const top = r.walkY + Math.min(h - 0.28 - (r.walkY - r.b0), 2.3);
    const rows = [];
    const nRows = Math.max(3, Math.round((top - r.walkY) / 0.2));
    const cx = ac + (R() - 0.5) * Math.min(0.2, (w - holeW) * 0.5);
    for (let i = 0; i < nRows; i++) {
      const t = i / nRows, ya = r.walkY + ((top - r.walkY) * i) / nRows, yb = r.walkY + ((top - r.walkY) * (i + 1)) / nRows;
      const f = t < 0.55 ? 1 : 1 - (t - 0.55) * 1.15;
      const half = (holeW * f) / 2;
      const jl = (R() - 0.5) * 0.16, jr = (R() - 0.5) * 0.16;
      rows.push({ a0: Math.max(r.a0 + 0.12, cx - half + jl), a1: Math.min(r.a1 - 0.12, cx + half + jr), y0: i === 0 ? r.b0 - 0.01 : ya, y1: yb });
    }
    layer(cutRect(r.a0, r.a1, r.b0, r.b1, rows), st[2]);
    // exposed core on the fracture faces: chunks set into the cut edges (visual only)
    for (const row of rows) for (const side of [0, 1]) {
      const a = side ? row.a1 : row.a0, s = 0.1 + R() * 0.12;
      const d = inner + (outer - inner) * (0.2 + R() * 0.6);
      const p = axis === "x" ? [a + (side ? -1 : 1) * s * 0.25, (row.y0 + row.y1) / 2, d] : [d, (row.y0 + row.y1) / 2, a + (side ? -1 : 1) * s * 0.25];
      A.obox(p, [s, (row.y1 - row.y0) * 0.9, s * 1.3], [R() * 20, R() * 60, R() * 20], fam.core, { into: st[2], k: 0 });
    }
    if (panel.family === "concrete") {  // bent, broken reinforcement across the opening
      for (let a = rows[0].a0 + 0.18; a < rows[0].a1 - 0.1; a += 0.24) {
        if (R() < 0.45) continue;
        const len = 0.25 + R() * 0.7, d = inner + (outer - inner) * (R() < 0.5 ? 0.25 : 0.75);
        const p = axis === "x" ? [a, top - len / 2, d] : [d, top - len / 2, a];
        A.obox(p, [0.018, len, 0.018], [axis === "x" ? (R() - 0.5) * 30 : 0, 0, axis === "z" ? (R() - 0.5) * 30 : 0], "steel_bare", { into: st[2], k: 0 });
      }
    }
    for (let i = 0; i < 7; i++) {   // rubble spill either side of the wall foot (walkable, visual only)
      const a = cx + (R() - 0.5) * holeW * 1.3, s = 0.12 + R() * 0.2;
      const d = (R() < 0.6 ? outer + out * (0.15 + R() * 0.7) : inner - out * (0.1 + R() * 0.5));
      const y = (d * out > outer * out ? Math.min(0, r.walkY) : r.walkY) + s * 0.3;
      const p = axis === "x" ? [a, y, d] : [d, y, a];
      A.obox(p, [s * 1.4, s * 0.6, s], [R() * 25, R() * 180, R() * 25], fam.core, { into: st[2], k: 0 });
    }
  }
  const mn = axis === "x" ? [r.a0, r.b0, Math.min(inner, outer)] : [Math.min(inner, outer), r.b0, r.a0];
  const mx = axis === "x" ? [r.a1, r.b1, Math.max(inner, outer)] : [Math.max(inner, outer), r.b1, r.a1];
  const normal = axis === "x" ? [0, 0, out] : [out, 0, 0];
  A.panels.push({ id, family: panel.family || "masonry", hp: panel.hp ?? fam.hp, min: mn, max: mx, normal, states: st });
}
export function partition(A, { axis, d0, d1, a0, a1, y0, y1, holes, m = "plaster" }) {
  for (const [p0, p1, ya, yb] of cutRect(a0, a1, y0, y1, holes)) wbox(A, axis, p0, p1, ya, yb, d0, d1, m);
  if (!PARTITION_HOLES.has(A)) PARTITION_HOLES.set(A, []);
  for (const h of holes) PARTITION_HOLES.get(A).push({ axis, d0: Math.min(d0, d1), d1: Math.max(d0, d1), y0, y1, h });   // GX2: doorway jambs (interiorTrim)
}
const PARTITION_HOLES = new WeakMap();

// --------------------------------------------------------------------------- GX2 interior trim
// Real 3D joinery on the interior faces, added after a building is built (graphics brief section 1 / GX2):
//  - a skirting board (board + moulded top, 2 cm proud) along every plastered wall face at each floor level, where a
//    floor runs along it, stopping at door openings;
//  - a dado / wainscot top rail as a lip at `rail` m (the interior shader's wainscot cap at 0.955 m), stopping at
//    windows whose sill is below it;
//  - jamb liners and architraves round the doorways of partition walls (unless the builder already framed them).
// Interior faces: "plaster" boxes (the wall() lining, partitions, chimney breasts) whose face is not against another
// solid. Trim pieces go in the same part list as the plaster they sit on (bay states included: they break with the
// bay); visual only (k: 0), so colliders, panels and nav are unchanged. levels: walking surface heights (asset space).
// Returns the number of pieces added.
const TRIM = [0.13, 0.08, 0.045];
export function interiorTrim(A, { levels, rail = 0.955, skirt = 0.15, plaster = ["plaster"], minLen = 0.12 } = {}) {
  ensureMat(A, "trim_x", TRIM, 0.55, "wood", { grain: 0 });
  ensureMat(A, "trim_y", TRIM, 0.55, "wood", { grain: 1 });
  ensureMat(A, "trim_z", TRIM, 0.55, "wood", { grain: 2 });
  const lists = [A.parts]; for (const p of A.panels) for (const st of p.states) lists.push(st);
  const isBox = (p) => p.t === "b" && !p.r;
  const lo = (p, i) => p.c[i] - p.s[i] / 2, hi = (p, i) => p.c[i] + p.s[i] / 2;
  // faces of every other solid box, by axis and plane (1 cm buckets): a plaster face lying on one is not interior
  const faces = new Map(), key = (i, v) => `${i}:${Math.round(v * 100)}`;
  const addFace = (i, v, p) => { for (const d of [-1, 0, 1]) { const k = `${i}:${Math.round(v * 100) + d}`; if (!faces.has(k)) faces.set(k, []); faces.get(k).push(p); } };
  for (const L of [A.parts, ...A.panels.map((p) => p.states[0])]) for (const p of L) {
    if (!isBox(p) || p.m.startsWith("trim_") || p.k === 0 && p.v === 0) continue;   // plaster too: back-to-back linings hide each other
    for (const i of [0, 2]) { addFace(i, lo(p, i), p); addFace(i, hi(p, i), p); }
  }
  const FLOOR = new Set(["floor_x", "floor_z", "terracotta", "concrete_floor", "plank_x", "plank_z"]), FLAG = new Set(["stone_dressed", "hearthstone", "earth"]);
  const floors = A.parts.filter((p) => isBox(p) && p.s[0] > 0.5 && p.s[2] > 0.5 && ((FLOOR.has(p.m) && p.s[1] <= 0.4) || (FLAG.has(p.m) && p.s[1] <= 0.06 && p.s[0] > 1.5 && p.s[2] > 1.5)));   // as src/interior.cpp rooms()
  const covered = (pl, i, v, out) => {   // is the face of plaster box pl at plane i = v (facing out) against a solid?
    const j = 2 - i, a0 = lo(pl, j), a1 = hi(pl, j), y0 = lo(pl, 1), y1 = hi(pl, 1);
    for (const q of faces.get(key(i, v)) || []) {
      if (q === pl) continue;
      if (out > 0 ? hi(q, i) < v + 0.02 : lo(q, i) > v - 0.02) continue;   // q must lie beyond the face
      const ov = Math.min(a1, hi(q, j)) - Math.max(a0, lo(q, j)), oy = Math.min(y1, hi(q, 1)) - Math.max(y0, lo(q, 1));
      if (ov > 0.5 * (a1 - a0) && oy > 0.3 * (y1 - y0)) return true;
    }
    return false;
  };
  // where a floor at level L runs along the face (sampled 10 cm in front of it): [a0, a1] intervals
  const floorRuns = (i, v, out, a0, a1, L) => {
    const j = 2 - i, runs = []; let s = null;
    for (let a = a0 + 0.05; a <= a1 - 0.05 + 1e-6; a += 0.1) {
      const pt = []; pt[i] = v + out * 0.1; pt[j] = a;
      const ok = floors.some((f) => Math.abs(hi(f, 1) - L) < 0.04 && pt[0] >= lo(f, 0) && pt[0] <= hi(f, 0) && pt[2] >= lo(f, 2) && pt[2] <= hi(f, 2));
      if (ok && s === null) s = a - 0.05;
      if (!ok && s !== null) { runs.push([s, a - 0.05]); s = null; }
    }
    if (s !== null) runs.push([s, a1]);
    return runs.map(([p, q]) => [Math.max(p, a0), Math.min(q, a1)]).filter(([p, q]) => q - p >= minLen);
  };
  let n = 0;
  const made = [];
  // every solid already in the building except plaster: a run stops 1.5 mm short of one standing in front of the face
  // (stair treads, beams, a builder's own door casing), so trim never passes through or lies flush with it
  const solids = [A.parts, ...A.panels.map((p) => p.states[0])].flatMap((L) => L.filter((p) => isBox(p) && !plaster.includes(p.m)));
  const put = (L, i, v, out, a0, a1, y0, y1, e0, e1, bv, ei = 0.0006) => {   // a trim box on face (axis i, plane v), e0..e1 out of it
    const j = 2 - i, mn = [], mx = [];
    mn[i] = Math.min(v + out * e0, v + out * e1); mx[i] = Math.max(v + out * e0, v + out * e1);
    mn[1] = y0; mx[1] = y1;
    const p0 = Math.min(v, v + out * e1), p1 = Math.max(v, v + out * e1);   // the proud part only
    const yb = y1 - y0 > 0.1 ? y0 + 0.0055 : y0;   // the board's foot is buried in the floor: not a hit
    let iv = [[a0 + ei, a1 - ei]];   // short of each end: never flush with the next run, a wall or the piece below
    for (const s of solids) {
      if (Math.min(p1, hi(s, i)) - Math.max(p0, lo(s, i)) < -0.0006 || Math.min(y1, hi(s, 1)) - Math.max(yb, lo(s, 1)) < -0.0006) continue;   // touching counts
      const c0 = lo(s, j) - 0.0009 - ei, c1 = hi(s, j) + 0.0009 + ei;   // per-piece margin: board and moulding ends differ
      iv = iv.flatMap(([b0, b1]) => c1 <= b0 || c0 >= b1 ? [[b0, b1]] : [[b0, c0], [c1, b1]]);
    }
    for (const [b0, b1] of iv) {
      if (b1 - b0 < 0.04) continue;
      mn[j] = b0; mx[j] = b1;
      const p = A.box(mn, mx, j === 0 ? "trim_x" : "trim_z", { k: 0, into: L, bv });
      if (p) { ++n; made.push([p, j]); }
    }
  };
  const runs = new Map();
  for (const L of lists) {
    const pls = L.filter((p) => isBox(p) && plaster.includes(p.m) && p.s[1] > 0.5);   // walls, not plaster ceilings
    for (const pl of pls) {
      for (const i of [0, 2]) {
        const j = 2 - i;
        if (hi(pl, j) - lo(pl, j) < minLen || pl.s[i] > hi(pl, j) - lo(pl, j) + 0.3) continue;   // only along its length
        for (const out of [-1, 1]) {
          const v = out > 0 ? hi(pl, i) : lo(pl, i);
          if (covered(pl, i, v, out)) continue;
          for (const lv of levels) {
            const by = lo(pl, 1), ty = hi(pl, 1);
            if (by > lv + 0.03 || ty < lv + skirt) continue;
            const hasRail = ty >= lv + rail + 0.05 && by <= lv + rail - 0.03;
            for (const r of floorRuns(i, v, out, lo(pl, j), hi(pl, j), lv)) {
              for (const kind of hasRail ? [0, 1] : [0]) {   // collected per face plane first: abutting / overlapping plaster pieces give one run
                const k = [lists.indexOf(L), i, Math.round(v * 2000), out, lv, kind].join('|');
                if (!runs.has(k)) runs.set(k, { L, i, v, out, lv, kind, iv: [] });
                runs.get(k).iv.push(r);
              }
            }
          }
        }
      }
    }
  }
  // doorways that get architraves below: skirting and rail stop 1 mm short of them (the architrave stands prouder)
  const doors = (PARTITION_HOLES.get(A) || []).filter(({ y0, y1, h }) => !(h.y0 > y0 + 0.05 || h.a1 - h.a0 < 0.55 || h.a1 - h.a0 > 2.4 || Math.min(h.y1, y1) - y0 < 1.8));
  for (const { L, i, v, out, lv, kind, iv } of runs.values()) {
    iv.sort((x, y) => x[0] - y[0]);
    let merged = [];
    for (const r of iv) { const m = merged[merged.length - 1]; if (m && r[0] <= m[1] + 0.003) m[1] = Math.max(m[1], r[1]); else merged.push([...r]); }
    for (const { axis, d0, d1, h } of doors) {
      if ((axis === "x") === (i === 0) && Math.min(Math.abs(v - h.a0), Math.abs(v - h.a1)) < 0.01) {   // a reveal of that doorway: the jamb liner covers it
        merged = merged.filter(([a0, a1]) => a0 < d0 - 0.02 || a1 > d1 + 0.02);
        continue;
      }
      if ((axis === "x") !== (i === 2) || Math.min(Math.abs(v - d0), Math.abs(v - d1)) > 0.03 || h.y0 > lv + 1.2) continue;
      const c0 = h.a0 - 0.071, c1 = h.a1 + 0.071;
      merged = merged.flatMap(([a0, a1]) => [[a0, Math.min(a1, c0)], [Math.max(a0, c1), a1]]).filter(([a0, a1]) => a1 - a0 >= minLen);
    }
    const q = i === 2 ? 0 : 0.0015;   // z-runs sit 1.5 mm off the x-runs (no coplanar faces at corners)
    for (const [a0, a1] of merged) {
      if (kind === 0) {
        // board: back and foot buried 7-9 mm, so where a run ends at an open edge (a stair opening, a door post) neither
        // lies within 6 mm of the wall's or the floor slab's own faces; the moulding stops 7 mm short of the board's ends
        put(L, i, v, out, a0, a1, lv - 0.009 - q, lv + skirt - 0.018 + q, -0.007, 0.016 + q, 0);      // board (back and foot buried)
        put(L, i, v, out, a0, a1, lv + skirt - 0.024 + q, lv + skirt + q, -0.0028, 0.022 + q, 0, 0.0076); // moulded top edge
      } else {
        put(L, i, v, out, a0, a1, lv + rail - 0.012 + q, lv + rail + 0.034 + q, -0.0052, 0.024 + q, 0.006);   // dado rail lip
        put(L, i, v, out, a0, a1, lv + rail - 0.027 + q, lv + rail - 0.009 + q, -0.0028, 0.012 + q, 0, 0.0076);     // its cove (into the lip, 7 mm short of its ends)
      }
    }
  }
  // doorway jambs and architraves in partition walls (holes from the floor up), unless already framed
  for (const { axis, d0, d1, y0, y1, h } of PARTITION_HOLES.get(A) || []) {
    if (h.y0 > y0 + 0.05 || h.a1 - h.a0 < 0.55 || h.a1 - h.a0 > 2.4 || Math.min(h.y1, y1) - y0 < 1.8) continue;
    const top = Math.min(h.y1, y1), w = 0.07, t = 0.022;
    const framed = A.parts.some((p) => isBox(p) && !plaster.includes(p.m) && !p.m.startsWith("trim_") && [h.a0, h.a1].some((a) => {
      const pa0 = axis === "x" ? lo(p, 0) : lo(p, 2), pa1 = axis === "x" ? hi(p, 0) : hi(p, 2);
      const pd0 = axis === "x" ? lo(p, 2) : lo(p, 0), pd1 = axis === "x" ? hi(p, 2) : hi(p, 0);
      return pa0 < a + 0.03 && pa1 > a - 0.03 && pd0 < d1 && pd1 > d0 && lo(p, 1) < y0 + 1.0 && hi(p, 1) > y0 + 1.0;
    }));
    if (framed) continue;
    const m = "trim_y";
    for (const [a, s] of [[h.a0, 1], [h.a1, -1]]) {
      wbox(A, axis, a - s * 0.004, a + s * t, y0 - 0.0045, top + 0.004, d0 - 0.01, d1 + 0.01, m, { k: 0 }); ++n;   // jamb liner in the reveal (buried 4 mm)
      for (const [dd, o] of [[d0, -1], [d1, 1]]) { wbox(A, axis, a - s * w, a + s * 0.006, y0 - 0.003, top + w, dd - o * 0.004, dd + o * 0.031, m, { k: 0, bv: 0.004 }); ++n; }   // architraves (prouder than skirting and rail, which stop at them)
    }
    wbox(A, axis, h.a0 + 0.004, h.a1 - 0.004, top - t - 0.002, top + 0.006, d0 - 0.013, d1 + 0.013, axis === "x" ? "trim_x" : "trim_z", { k: 0 }); ++n;   // head liner
    for (const [dd, o] of [[d0, -1], [d1, 1]]) { wbox(A, axis, h.a0 - w - 0.003, h.a1 + w + 0.003, top - 0.004, top + w + 0.005, dd - o * 0.0028, dd + o * 0.0345, axis === "x" ? "trim_x" : "trim_z", { k: 0, bv: 0.004 }); ++n; }
  }
  // last: a run end still lying in the plane of another face it overlaps (a buried back at a corner, by coincidence)
  // is shortened 1.2 mm, until none is
  const all = lists.flatMap((L) => L.filter(isBox));
  const R = (x) => Math.round(x * 10000) / 10000;
  for (let pass = 0; pass < 4; ++pass) {
    let moved = 0;
    for (const [p, j] of made) for (const e of [-1, 1]) {
      const f = e < 0 ? lo(p, j) : hi(p, j);
      const hit = all.some((q) => q !== p && [lo(q, j), hi(q, j)].some((g) => Math.abs(g - f) < 0.0005) &&
        [0, 1, 2].every((a) => a === j || Math.min(hi(p, a), hi(q, a)) - Math.max(lo(p, a), lo(q, a)) > 0.001));
      if (!hit) continue;
      p.s[j] = R(p.s[j] - 0.0012); p.c[j] = R(p.c[j] - e * 0.0006); ++moved;
    }
    if (!moved) break;
  }
  return n;
}

// --------------------------------------------------------------------------- windows
// Casement pair opening inward + exterior shutters, sill, lintel. Returns the wall hole.
export function window(A, o) {
  const { id, axis, c, sill, w, h, outer, inner } = o;
  const out = Math.sign(outer - inner);
  const a0 = c - w / 2, a1 = c + w / 2, top = sill + h;
  const fw = 0.06, fd0 = inner + out * 0.03, fd1 = inner + out * 0.10;
  // fixed frame (dormant) in the reveal near the interior face
  wbox(A, axis, a0, a0 + fw, sill, top, fd0, fd1, "window_paint");
  wbox(A, axis, a1 - fw, a1, sill, top, fd0, fd1, "window_paint");
  wbox(A, axis, a0 + fw, a1 - fw, top - fw, top, fd0, fd1, "window_paint");
  wbox(A, axis, a0 + fw, a1 - fw, sill, sill + fw, fd0, fd1 + out * 0.02, "window_paint");
  // stone sill: interior ledge + protruding nose; lintel
  wbox(A, axis, a0, a1, sill - 0.08, sill, inner + out * 0.0, outer, "stone_dressed");
  wbox(A, axis, a0 - 0.08, a1 + 0.08, sill - 0.085, sill - 0.005, outer, outer + out * 0.06, "stone_dressed");
  extOf(A).sills.push({ axis, d: outer + out * 0.06, out, a0, a1, y: sill - 0.085 });   // GX4: rain-streak anchor under the sill nose
  wbox(A, axis, a0 - 0.16, a1 + 0.16, top - 0.005, top + 0.24, outer - out * 0.12, outer + out * 0.02, "stone_dressed");
  wbox(A, axis, a0 - 0.12, a1 + 0.12, top - 0.004, top + 0.16, inner - out * 0.012, inner + out * 0.2, "oak_x");

  // ---- casement leaves (glazed, 3 panes each), open inward
  const la0 = a0 + fw, la1 = a1 - fw, ly0 = sill + fw, ly1 = top - fw, lm = (la0 + la1) / 2;
  const ld0 = inner + out * 0.035, ld1 = inner + out * 0.08;
  const leaves = [];
  for (const side of [0, 1]) {
    const L = { parts: [] };
    const s0 = side === 0 ? la0 : lm, s1 = side === 0 ? lm : la1;
    const hingeA = side === 0 ? la0 : la1;
    const T = { into: L.parts, k: 0 };
    wbox(A, axis, s0, s0 + 0.05, ly0, ly1, ld0, ld1, "window_paint", T);
    wbox(A, axis, s1 - 0.05, s1, ly0, ly1, ld0, ld1, "window_paint", T);
    wbox(A, axis, s0 + 0.05, s1 - 0.05, ly1 - 0.06, ly1, ld0, ld1, "window_paint", T);
    wbox(A, axis, s0 + 0.05, s1 - 0.05, ly0, ly0 + 0.085, ld0, ld1, "window_paint", T);
    const gh = (ly1 - 0.06) - (ly0 + 0.085);
    for (const f of [1 / 3, 2 / 3]) {
      const y = ly0 + 0.085 + gh * f;
      wbox(A, axis, s0 + 0.05, s1 - 0.05, y - 0.0125, y + 0.0125, ld0 + out * 0.005, ld1 - out * 0.005, "window_paint", T);
    }
    const gd = (ld0 + ld1) / 2;
    wbox(A, axis, s0 + 0.04, s1 - 0.04, ly0 + 0.07, ly1 - 0.045, gd - 0.003, gd + 0.003, "glass", T);
    if (side === 1) { // espagnolette bolt and lever on the meeting stile
      const ia = s0 + 0.025;
      wbox(A, axis, ia - 0.008, ia + 0.008, ly0 + 0.02, ly1 - 0.02, ld0 - out * 0.012, ld0, "iron", T);
      wbox(A, axis, ia - 0.012, ia + 0.06, (ly0 + ly1) / 2 - 0.01, (ly0 + ly1) / 2 + 0.01, ld0 - out * 0.035, ld0 - out * 0.012, "brass", T);
    }
    wbox(A, axis, s0, s1, ly0, ly1, ld0, ld1, "window_paint", { into: L.parts, v: 0 }); // collider
    const hinge = P(axis, hingeA, 0, ld0);
    const mid = P(axis, (s0 + s1) / 2, 0, (ld0 + ld1) / 2);
    L.hinge = hinge;
    L.sign = swingSign(axis, mid, hinge, -out);
    leaves.push(L);
  }
  A.interactables.push({
    id: id + "_casement", kind: "window", label: "window", sound: "glass", axis: [0, 1, 0], openAngle: 100, duration: 0.85,
    defaultOpen: !!o.casementOpen, leaves,
    aperture: apBox(axis, a0, a1, sill, top, inner, inner + out * 0.12),
  });

  // ---- exterior shutters (persiennes pleines), open outward against the facade. Every layer stands >= 2 cm off the
  // wall face (battens 2 cm, boards 3.5 cm, strap hinges on the boards): 5 mm layers shimmered at range (docs/rendering.md)
  if (o.shutters !== false) {
    const sl = [];
    const sy0 = sill - 0.03, sy1 = top + 0.03;
    const sd0 = outer + out * 0.035, sd1 = outer + out * 0.07;
    for (const side of [0, 1]) {
      const L = { parts: [] };
      const s0 = side === 0 ? a0 - 0.03 : c, s1 = side === 0 ? c : a1 + 0.03;
      const T = { into: L.parts, k: 0 };
      // vertical boards
      wbox(A, axis, s0, s1, sy0, sy1, sd0, sd1, "shutter_paint", T);
      // battens and Z-brace on the inner face
      for (const y of [sy0 + 0.18, sy1 - 0.18]) wbox(A, axis, s0 + 0.03, s1 - 0.03, y - 0.05, y + 0.05, sd0 - out * 0.015, sd0, "shutter_paint", T);
      // strap hinges (exterior face)
      const hingeA = side === 0 ? a0 - 0.03 : a1 + 0.03;
      for (const y of [sy0 + 0.2, sy1 - 0.2]) {
        const e0 = hingeA, e1 = hingeA + (side === 0 ? 0.3 : -0.3);
        wbox(A, axis, e0, e1, y - 0.02, y + 0.02, sd1, sd1 + out * 0.008, "iron", T);
      }
      wbox(A, axis, s0, s1, sy0, sy1, sd0, sd1, "shutter_paint", { into: L.parts, v: 0 });
      const hinge = P(axis, hingeA, 0, outer + out * 0.075);
      const mid = P(axis, (s0 + s1) / 2, 0, (sd0 + sd1) / 2);
      L.hinge = hinge;
      L.sign = swingSign(axis, mid, hinge, out);
      sl.push(L);
    }
    A.interactables.push({
      id: id + "_shutters", kind: "shutters", label: "shutters", sound: "shutter", axis: [0, 1, 0], openAngle: 172, duration: 1.0,
      defaultOpen: o.shuttersOpen !== false, leaves: sl,
      aperture: apBox(axis, a0, a1, sill, top, outer - out * 0.12, outer + out * 0.09),
    });
  }
  return { a0, a1, y0: sill - 0.08, y1: top };
}

function apBox(axis, a0, a1, y0, y1, d0, d1) {
  const p0 = P(axis, a0, y0, Math.min(d0, d1)), p1 = P(axis, a1, y1, Math.max(d0, d1));
  return { min: [0, 1, 2].map((i) => Math.min(p0[i], p1[i])), max: [0, 1, 2].map((i) => Math.max(p0[i], p1[i])) };
}

// --------------------------------------------------------------------------- doors
// Plank door hung on the interior side of the reveal, opening inward. Options:
//   double: two leaves meeting in the middle (cart/workshop doors)
//   steel: riveted steel plate leaf, steel frame, heavy sound
//   leafMat / frameMat / thresholdMat, lintel:false, step:false, duration, label, sound
export function door(A, o) {
  const { id, axis, c, floor, w, h, outer, inner, hingeSide } = o;
  const out = Math.sign(outer - inner);
  const a0 = c - w / 2, a1 = c + w / 2, top = floor + h;
  const steel = !!o.steel;
  const fw = steel ? 0.06 : 0.07, fd0 = inner + out * 0.03, fd1 = inner + out * (steel ? 0.16 : 0.13);
  const fy = o.frameMat || (steel ? "steel_plate" : "oak_y"), fx = o.frameMat || (steel ? "steel_plate" : "oak_x");
  const leafMat = o.leafMat || (steel ? "steel_plate" : "door_paint");
  wbox(A, axis, a0, a0 + fw, floor, top, fd0, fd1, fy);
  wbox(A, axis, a1 - fw, a1, floor, top, fd0, fd1, fy);
  wbox(A, axis, a0, a1, top - fw, top, fd0, fd1, fx);
  wbox(A, axis, a0, a1, floor - 0.04, floor, inner, outer + out * 0.04, o.thresholdMat || (steel ? "concrete" : "stone_dressed"));
  if (o.step !== false) wbox(A, axis, a0 - 0.25, a1 + 0.25, -0.35, floor - 0.15, outer + out * 0.04, outer + out * 0.55, "stone_dressed");
  if (o.lintel !== false) {
    wbox(A, axis, a0 - 0.2, a1 + 0.2, top - 0.005, top + 0.26, outer - out * 0.12, outer + out * 0.02, "stone_dressed");
    wbox(A, axis, a0 - 0.14, a1 + 0.14, top - 0.004, top + 0.18, inner - out * 0.012, inner + out * 0.22, "oak_x");
  }
  const la0 = a0 + fw, la1 = a1 - fw, ly0 = floor + 0.006, ly1 = top - fw - 0.004;
  const ld0 = inner + out * 0.035, ld1 = inner + out * (steel ? 0.075 : 0.085);
  const spans = o.double ? [[la0, (la0 + la1) / 2 - 0.004, -1], [(la0 + la1) / 2 + 0.004, la1, 1]] : [[la0, la1, hingeSide]];
  const leaves = [];
  for (const [s0, s1, hs] of spans) {
    const L = { parts: [] };
    const T = { into: L.parts, k: 0 };
    const hingeA = hs < 0 ? s0 : s1, dirA = hs < 0 ? 1 : -1;
    const lo = ld0 - out * 0.028, li = ld0;
    wbox(A, axis, s0, s1, ly0, ly1, ld0, ld1, leafMat, T);
    if (steel) {
      for (const [d0, d1] of [[lo + out * 0.016, li], [ld1, ld1 + out * 0.012]]) {
        for (const y of [ly0 + 0.08, (ly0 + ly1) / 2, ly1 - 0.08]) wbox(A, axis, s0 + 0.03, s1 - 0.03, y - 0.03, y + 0.03, d0, d1, leafMat, T);
      }
      for (const y of [ly0 + 0.2, ly1 - 0.2]) wbox(A, axis, Math.min(hingeA - dirA * 0.02, hingeA + dirA * 0.07), Math.max(hingeA - dirA * 0.02, hingeA + dirA * 0.07), y - 0.06, y + 0.06, lo, ld1 + out * 0.02, "steel_bare", T);
      const la = hingeA + dirA * (s1 - s0 - 0.12), ly = floor + 1.05;
      for (const [d0, d1] of [[lo - out * 0.03, lo], [ld1, ld1 + out * 0.03]]) {
        wbox(A, axis, la - 0.02, la + 0.02, ly - 0.03, ly + 0.03, d0, d1, "steel_bare", T);
        wbox(A, axis, Math.min(la, la - dirA * 0.18), Math.max(la, la - dirA * 0.18), ly - 0.015, ly + 0.015, d0, d1, "steel_bare", T);
      }
    } else {
      for (const y of [ly0 + 0.25, (ly0 + ly1) / 2, ly1 - 0.25]) wbox(A, axis, s0 + 0.04, s1 - 0.04, y - 0.07, y + 0.07, lo, li, leafMat, T);
      for (const y of [ly0 + 0.25, ly1 - 0.25]) wbox(A, axis, Math.min(hingeA, hingeA + dirA * Math.min(0.6, s1 - s0 - 0.1)), Math.max(hingeA, hingeA + dirA * Math.min(0.6, s1 - s0 - 0.1)), y - 0.022, y + 0.022, lo - out * 0.006, lo, "iron", T);
      const latchA = hingeA + dirA * (s1 - s0 - 0.12), ly = floor + 1.0;
      wbox(A, axis, latchA - 0.09, latchA + 0.09, ly - 0.012, ly + 0.012, lo - out * 0.03, lo - out * 0.006, "iron", T);
      wbox(A, axis, latchA - 0.015, latchA + 0.015, ly - 0.10, ly + 0.08, ld1, ld1 + out * 0.03, "iron", T);
      wbox(A, axis, latchA - 0.03, latchA + 0.03, ly - 0.17, ly - 0.11, ld1 + out * 0.02, ld1 + out * 0.04, "iron", T);
    }
    wbox(A, axis, s0, s1, ly0, ly1, ld0, ld1, leafMat, { into: L.parts, v: 0 });
    const hinge = P(axis, hingeA, 0, ld0);
    const mid = P(axis, (s0 + s1) / 2, 0, (ld0 + ld1) / 2);
    L.hinge = hinge;
    L.sign = swingSign(axis, mid, hinge, -out);
    leaves.push(L);
  }
  A.interactables.push({
    id, kind: "door", label: o.label || "door", sound: o.sound || (steel ? "steel" : "wood"), axis: [0, 1, 0],
    openAngle: o.openAngle || 100, duration: o.duration || (steel ? 1.7 : o.double ? 1.4 : 1.1),
    defaultOpen: !!o.open, leaves,
    aperture: apBox(axis, a0, a1, floor, top, inner, inner + out * 0.14),
  });
  return { a0, a1, y0: -10, y1: top };
}

// Generic hinged leaf group about a principal axis ("x","y","z"), e.g. top-hung armoured flaps.
// leaves: [{ hinge:[x,y,z], build(T) pushes visual parts via A.box(..., T), collider:[min,max], want:[dx,dy,dz] }]
const rot90 = (p, c, ax) => {
  const x = p[0] - c[0], y = p[1] - c[1], z = p[2] - c[2];
  if (ax === "x") return [p[0], c[1] - z, c[2] + y];
  if (ax === "z") return [c[0] - y, c[1] + x, p[2]];
  return [c[0] + z, p[1], c[2] - x];
};
export function hinged(A, o) {
  const leaves = [];
  for (const lf of o.leaves) {
    const L = { parts: [] };
    lf.build({ into: L.parts, k: 0 });
    A.box(lf.collider[0], lf.collider[1], o.colliderMat || "steel_plate", { into: L.parts, v: 0 });
    const mid = [0, 1, 2].map((i) => (lf.collider[0][i] + lf.collider[1][i]) / 2);
    const p = rot90(mid, lf.hinge, o.axis);
    const d = [0, 1, 2].map((i) => p[i] - mid[i]);
    L.hinge = lf.hinge;
    L.sign = d[0] * lf.want[0] + d[1] * lf.want[1] + d[2] * lf.want[2] > 0 ? 1 : -1;
    leaves.push(L);
  }
  const ax = { x: [1, 0, 0], y: [0, 1, 0], z: [0, 0, 1] }[o.axis];
  A.interactables.push({ id: o.id, kind: o.kind, label: o.label, sound: o.sound, axis: ax, openAngle: o.openAngle, duration: o.duration,
    defaultOpen: !!o.defaultOpen, apertureAlways: !!o.apertureAlways, leaves, aperture: o.aperture });
}
export function aabb(p0, p1) { return { min: [0, 1, 2].map((i) => Math.min(p0[i], p1[i])), max: [0, 1, 2].map((i) => Math.max(p0[i], p1[i])) }; }

// --------------------------------------------------------------------------- stairs
// Straight flight along Z. Visual treads/risers/stringers; smooth invisible collision ramp
// through the tread centres; sloped guard on the open side; spandrel closes the void below.
export function stairs(A, o) {
  const { x0, x1, z0, dir, y0, rise, n, run, openSide } = o; // z0 = first nosing line, dir = ±1 along +Z
  const r = rise / n, d = run / n;
  const zAt = (i) => z0 + dir * i * d;
  for (let i = 0; i < n; i++) {
    const top = y0 + (i + 1) * r;
    const za = zAt(i) - dir * 0.03, zb = zAt(i + 1);
    A.box([x0 + 0.02, top - 0.045, Math.min(za, zb)], [x1 - 0.02, top, Math.max(za, zb)], "tread_x", { k: i === n - 1 ? 1 : 0 });
    A.box([x0 + 0.03, y0 + i * r, Math.min(zAt(i) + dir * 0.012, zAt(i) + dir * 0.032)], [x1 - 0.03, top - 0.045, Math.max(zAt(i) + dir * 0.012, zAt(i) + dir * 0.032)], "oak_x", { k: 0 });
  }
  const theta = Math.atan2(r, d), deg = (theta * 180) / Math.PI;
  const alpha = dir < 0 ? deg : -deg;               // Euler X rotation that tilts local +Z down-slope
  const nrm = [0, Math.cos((alpha * Math.PI) / 180), Math.sin((alpha * Math.PI) / 180)];
  const pA = [zAt(-0.5) - dir * 0.15 * Math.cos(theta), y0 - 0.15 * Math.sin(theta)];
  const pB = [zAt(n - 0.5), y0 + rise];
  const L = Math.hypot(pB[0] - pA[0], pB[1] - pA[1]);
  const mz = (pA[0] + pB[0]) / 2, my = (pA[1] + pB[1]) / 2;
  const ramp = (thick, up, xa, xb, m, o2) => A.obox([(xa + xb) / 2, my + nrm[1] * up, mz + nrm[2] * up], [xb - xa, thick, L], [alpha, 0, 0], m, o2);
  ramp(0.4, -0.2, x0, x1, "tread_x", { v: 0 });                       // collision ramp
  // stringers (visual)
  const sdrop = 0.06;
  ramp(0.3, -sdrop, x0 - 0.0, x0 + 0.045, "oak_y", { k: 0 });
  ramp(0.3, -sdrop, x1 - 0.045, x1, "oak_y", { k: 0 });
  // core: flight enclosed by walls and spine; no balustrade, newels or under-flight panels
  // (those would hang into the flight below when flights are stacked)
  if (!o.core) {
  // open-side balustrade
  const xo = openSide < 0 ? x0 : x1, so = openSide < 0 ? -1 : 1;
  const xr0 = Math.min(xo, xo + so * 0.05), xr1 = Math.max(xo, xo + so * 0.05);
  ramp(0.07, 0.9 * Math.cos(theta), xr0, xr1, "oak_x", { k: 0 });                      // handrail
  for (let i = 0; i < n; i++) {
    const zc = zAt(i + 0.5), top = y0 + (i + 1) * r;
    A.box([xr0 + 0.008, top, zc - 0.018], [xr1 - 0.008, top + 0.86, zc + 0.018], "oak_y", { k: 0 });
    const za = zAt(i), zb = zAt(i + 1);   // vertical-sided guard collider per tread (no leaning ends)
    A.box([Math.min(xo, xo + so * 0.06), top - 0.05, Math.min(za, zb)], [Math.max(xo, xo + so * 0.06), top + 1.1, Math.max(za, zb)], "oak_x", { v: 0 });
  }
  for (const [zp, ht] of [[zAt(0) - dir * 0.08, 1.15], [zAt(n) - dir * 0.05, rise + 1.0]]) {
    A.box([xr0 - 0.03, y0, zp - 0.055], [xr1 + 0.03, y0 + ht, zp + 0.055], "oak_y", { k: 1 });
  }
  // spandrel panel and end panel close the void under the flight
  const zs = zAt(0) + dir * 0.06, ze = zAt(n);
  const xs0 = openSide < 0 ? x0 + 0.045 : x1 - 0.085, xs1 = xs0 + 0.04;
  A.prism(0, xs0, xs1, [[zs, y0], [ze, y0], [ze, y0 + rise - 0.32]], "oak_y");
  A.box([x0 + 0.045, y0, Math.min(ze, ze - dir * 0.04)], [x1 - 0.045, y0 + rise - 0.25, Math.max(ze, ze - dir * 0.04)], "plaster");
  }
  return { holeZ: [Math.min(zAt(0), zAt(n)), Math.max(zAt(0), zAt(n))] };
}

// level balustrade along a stairwell edge (axis "x" or "z")
export function railing(A, axis, a0, a1, d, y, h = 1.0) {
  const R = (aa0, aa1, ya, yb, dd0, dd1, m, o) => wbox(A, axis, aa0, aa1, ya, yb, dd0, dd1, m, o);
  R(a0, a1, y + h - 0.07, y + h, d - 0.03, d + 0.03, "oak_x", { k: 0 });
  R(a0, a1, y + 0.05, y + 0.1, d - 0.025, d + 0.025, "oak_x", { k: 0 });
  for (let a = a0 + 0.06; a < a1 - 0.04; a += 0.14) R(a - 0.017, a + 0.017, y + 0.1, y + h - 0.07, d - 0.017, d + 0.017, "oak_y", { k: 0 });
  R(a0, a1, y, y + h + 0.1, d - 0.035, d + 0.035, "oak_x", { v: 0 });
}

// --------------------------------------------------------------------------- furniture
export function table(A, name, x, z, w, dd, y, h = 0.76, rot = false) {
  A.beginFurniture(name);
  const hw = rot ? dd / 2 : w / 2, hd = rot ? w / 2 : dd / 2;
  A.box([x - hw, y + h - 0.06, z - hd], [x + hw, y + h, z + hd], rot ? "oak_z" : "oak_x");
  for (const sx of [-1, 1]) for (const sz of [-1, 1]) {
    const lx = x + sx * (hw - 0.09), lz = z + sz * (hd - 0.09);
    A.box([lx - 0.04, y, lz - 0.04], [lx + 0.04, y + h - 0.06, lz + 0.04], "oak_y");
  }
  A.box([x - hw + 0.06, y + h - 0.16, z - hd + 0.07], [x + hw - 0.06, y + h - 0.06, z - hd + 0.1], "oak_x");
  A.box([x - hw + 0.06, y + h - 0.16, z + hd - 0.1], [x + hw - 0.06, y + h - 0.06, z + hd - 0.07], "oak_x");
  A.box([x - hw + 0.12, y + 0.12, z - 0.03], [x + hw - 0.12, y + 0.17, z + 0.03], "oak_x");
  A.endFurniture();
}
// rush-seated chair; facing: direction the sitter faces ("+z","-z","+x","-x")
export function chair(A, name, x, z, y, facing) {
  A.beginFurniture(name);
  const s = 0.21;
  A.box([x - s, y + 0.43, z - s], [x + s, y + 0.47, z + s], "rush");
  for (const sx of [-1, 1]) for (const sz of [-1, 1]) A.box([x + sx * (s - 0.03) - 0.02, y, z + sz * (s - 0.03) - 0.02], [x + sx * (s - 0.03) + 0.02, y + 0.45, z + sz * (s - 0.03) + 0.02], "cherry_y");
  const back = { "+z": [0, -1], "-z": [0, 1], "+x": [-1, 0], "-x": [1, 0] }[facing];
  const bx = x + back[0] * (s - 0.03), bz = z + back[1] * (s - 0.03);
  const along = back[0] === 0 ? "x" : "z";
  for (const sgn of [-1, 1]) {
    const px = along === "x" ? x + sgn * (s - 0.03) : bx, pz = along === "x" ? bz : z + sgn * (s - 0.03);
    A.box([px - 0.022, y + 0.45, pz - 0.022], [px + 0.022, y + 0.98, pz + 0.022], "cherry_y");
  }
  for (const hy of [0.66, 0.8, 0.93]) {
    if (along === "x") A.box([x - s + 0.02, y + hy - 0.035, bz - 0.012], [x + s - 0.02, y + hy + 0.035, bz + 0.012], "cherry_x");
    else A.box([bx - 0.012, y + hy - 0.035, z - s + 0.02], [bx + 0.012, y + hy + 0.035, z + s - 0.02], "cherry_z");
  }
  A.endFurniture();
}
export function bench(A, name, x0, x1, z, y) {
  A.beginFurniture(name);
  A.box([x0, y + 0.42, z - 0.15], [x1, y + 0.47, z + 0.15], "oak_x");
  for (const xx of [x0 + 0.12, x1 - 0.12]) A.box([xx - 0.03, y, z - 0.12], [xx + 0.03, y + 0.42, z + 0.12], "oak_y");
  A.endFurniture();
}
export function bed(A, name, x0, x1, z0, z1, y, headAt) {
  // headAt: "x0" | "x1" | "z0" | "z1" — which end the headboard is on
  A.beginFurniture(name);
  const H = 0.34, mt = 0.22;
  A.box([x0 + 0.04, y + 0.2, z0 + 0.04], [x1 - 0.04, y + H, z1 - 0.04], "cherry_x");
  A.box([x0 + 0.07, y + H, z0 + 0.07], [x1 - 0.07, y + H + mt, z1 - 0.07], "ticking");
  A.box([x0 + 0.05, y + H + mt, z0 + 0.05], [x1 - 0.05, y + H + mt + 0.035, z1 - 0.05], "linen");
  const lenX = headAt[0] === "x";
  const headLo = headAt.endsWith("0");
  if (lenX) {
    const hx = headLo ? x0 : x1, s = headLo ? 1 : -1;
    const bl = headLo ? [x0 + 0.55, x1 - 0.05] : [x0 + 0.05, x1 - 0.55];
    A.box([bl[0], y + H + mt + 0.03, z0 + 0.04], [bl[1], y + H + mt + 0.075, z1 - 0.04], "wool_red");
    const pl = headLo ? [x0 + 0.08, x0 + 0.45] : [x1 - 0.45, x1 - 0.08];
    A.box([pl[0], y + H + mt + 0.03, z0 + 0.12], [pl[1], y + H + mt + 0.17, z1 - 0.12], "linen");
    A.box([Math.min(hx, hx + s * 0.07), y, z0], [Math.max(hx, hx + s * 0.07), y + 1.25, z1], "cherry_z");
    const fx = headLo ? x1 : x0;
    A.box([Math.min(fx, fx - s * 0.07), y, z0], [Math.max(fx, fx - s * 0.07), y + 0.75, z1], "cherry_z");
  } else {
    const hz = headLo ? z0 : z1, s = headLo ? 1 : -1;
    A.box([x0 + 0.04, y + H + mt + 0.03, Math.min(hz + s * 0.55, hz + s * (z1 - z0 - 0.05))], [x1 - 0.04, y + H + mt + 0.075, Math.max(hz + s * 0.55, hz + s * (z1 - z0 - 0.05))], "wool_red");
    A.box([x0 + 0.12, y + H + mt + 0.03, Math.min(hz + s * 0.08, hz + s * 0.45)], [x1 - 0.12, y + H + mt + 0.17, Math.max(hz + s * 0.08, hz + s * 0.45)], "linen");
    A.box([x0, y, Math.min(hz, hz + s * 0.07)], [x1, y + 1.25, Math.max(hz, hz + s * 0.07)], "cherry_x");
    const fz = headLo ? z1 : z0;
    A.box([x0, y, Math.min(fz, fz - s * 0.07)], [x1, y + 0.75, Math.max(fz, fz - s * 0.07)], "cherry_x");
  }
  A.endFurniture();
}
// tall carved cupboard (armoire normande); back against a wall along X at zb, front toward +s
export function armoire(A, name, x0, x1, zb, s, y, depth = 0.6, h = 2.25) {
  A.beginFurniture(name);
  const zf = zb + s * depth;
  A.box([x0, y + 0.12, Math.min(zb, zf)], [x1, y + h - 0.15, Math.max(zb, zf)], "cherry_y");
  A.box([x0 - 0.04, y + h - 0.15, Math.min(zb, zf + s * 0.05)], [x1 + 0.04, y + h, Math.max(zb, zf + s * 0.05)], "cherry_x");
  A.box([x0 + 0.02, y, Math.min(zb, zf)], [x1 - 0.02, y + 0.12, Math.max(zb, zf)], "cherry_x");
  const xm = (x0 + x1) / 2;
  for (const [a, b] of [[x0 + 0.06, xm - 0.015], [xm + 0.015, x1 - 0.06]]) {
    A.box([a, y + 0.25, Math.min(zf, zf + s * 0.02)], [b, y + h - 0.3, Math.max(zf, zf + s * 0.02)], "cherry_y");
    A.box([a + 0.06, y + 0.4, Math.min(zf, zf + s * 0.032)], [b - 0.06, y + h - 0.45, Math.max(zf, zf + s * 0.032)], "cherry_y");
  }
  for (const xx of [xm - 0.05, xm + 0.05]) A.box([xx - 0.012, y + 1.05, Math.min(zf, zf + s * 0.05)], [xx + 0.012, y + 1.25, Math.max(zf, zf + s * 0.05)], "brass");
  A.endFurniture();
}
// dresser with plate rack (vaisselier) against a wall along X
export function dresser(A, name, x0, x1, zb, s, y) {
  A.beginFurniture(name);
  const zf = zb + s * 0.52, zu = zb + s * 0.28;
  const zr = (a, b) => [Math.min(a, b), Math.max(a, b)];
  let [za, zz] = zr(zb, zf);
  A.box([x0, y + 0.1, za], [x1, y + 0.88, zz], "cherry_y");
  A.box([x0 - 0.03, y + 0.88, za], [x1 + 0.03, y + 0.93, Math.max(za, zz) + (s > 0 ? 0.03 : 0)], "cherry_x");
  A.box([x0 + 0.02, y, za], [x1 - 0.02, y + 0.1, zz], "cherry_x");
  [za, zz] = zr(zb, zu);
  A.box([x0, y + 0.93, za], [x0 + 0.04, y + 2.05, zz], "cherry_y");
  A.box([x1 - 0.04, y + 0.93, za], [x1, y + 2.05, zz], "cherry_y");
  A.box([x0, y + 0.93, Math.min(zb, zb + s * 0.02)], [x1, y + 2.05, Math.max(zb, zb + s * 0.02)], "cherry_y");
  A.box([x0 - 0.04, y + 2.05, za], [x1 + 0.04, y + 2.13, zz + (s > 0 ? 0.03 : 0)], "cherry_x");
  for (const sh of [1.3, 1.68]) {
    A.box([x0 + 0.04, y + sh, za], [x1 - 0.04, y + sh + 0.025, zz], "cherry_x");
    for (let px = x0 + 0.16, k = 0; px < x1 - 0.12; px += 0.24, k++) {
      const zp = zb + s * 0.07;
      A.box([px - 0.11, y + sh + 0.025, Math.min(zp, zp + s * 0.015)], [px + 0.11, y + sh + 0.245, Math.max(zp, zp + s * 0.015)], k % 3 === 1 ? "faience_blue" : "faience");
    }
  }
  const xm = (x0 + x1) / 2;
  A.box([x0 + 0.05, y + 0.18, Math.min(zf, zf + s * 0.015)], [xm - 0.01, y + 0.82, Math.max(zf, zf + s * 0.015)], "cherry_y");
  A.box([xm + 0.01, y + 0.18, Math.min(zf, zf + s * 0.015)], [x1 - 0.05, y + 0.82, Math.max(zf, zf + s * 0.015)], "cherry_y");
  A.endFurniture();
}
// chest (coffre) along X
export function chest(A, name, x0, x1, z0, z1, y, h = 0.6) {
  A.beginFurniture(name);
  A.box([x0, y + 0.05, z0], [x1, y + h - 0.04, z1], "oak_x");
  A.box([x0 - 0.02, y + h - 0.04, z0 - 0.02], [x1 + 0.02, y + h, z1 + 0.02], "oak_x");
  A.box([x0 + 0.03, y, z0 + 0.03], [x1 - 0.03, y + 0.05, z1 - 0.03], "oak_x");
  for (const xx of [x0 + 0.12, x1 - 0.12]) A.box([xx - 0.02, y + 0.1, z0 - 0.006], [xx + 0.02, y + h - 0.02, z1 + 0.006], "iron");
  A.endFurniture();
}

// --------------------------------------------------------------------------- roofs
// Slate gable roof with clay ridge, fascia, gutters and barge boards; ridge along X.
export function gableRoof(A, { X, Z, XI, EAVE, RIDGE, ov = 0.35, gov = 0.28, t = 0.14, gableMat = "stone", roofMat = "slate" }) {
  const th = Math.atan2(RIDGE - EAVE, Z), deg = (th * 180) / Math.PI;
  extOf(A).gables.push({ x0: -X, x1: X, xi: XI + 0.03, Z, EAVE, RIDGE, ov, gov, t, gableMat, roofMat });   // GX4 (tools/exterior.mjs)
  for (const sx of [-1, 1]) A.prism(0, sx > 0 ? XI + 0.03 : -X, sx > 0 ? X : -XI - 0.03, [[-Z, EAVE], [Z, EAVE], [0, RIDGE]], gableMat);
  for (const s of [1, -1]) {
    const zA = -0.12 * s, zB = s * (Z + ov);
    const zm = (zA + zB) / 2, ym = (RIDGE + 0.12 * Math.tan(th) + (RIDGE - (Z + ov) * Math.tan(th))) / 2;
    const len = Math.abs(zB - zA) / Math.cos(th);
    const a = s > 0 ? deg : -deg;
    const nrm = [0, Math.cos((a * Math.PI) / 180), Math.sin((a * Math.PI) / 180)];
    A.obox([0, ym + nrm[1] * t / 2, zm + nrm[2] * t / 2], [2 * X + 2 * gov, t, len], [a, 0, 0], roofMat);
    const ye = EAVE - ov * Math.tan(th);
    A.box([-X - gov, ye - 0.24, Math.min(s * (Z + ov - 0.03), s * (Z + ov))], [X + gov, ye + 0.02, Math.max(s * (Z + ov - 0.03), s * (Z + ov))], "oak_x");
    A.box([-X - gov, ye - 0.2, Math.min(s * (Z + ov), s * (Z + ov + 0.13))], [X + gov, ye - 0.1, Math.max(s * (Z + ov), s * (Z + ov + 0.13))], "zinc", { k: 0 });
    for (const sx of [-1, 1]) {
      const l2 = (Z + ov) / Math.cos(th);
      A.obox([sx * (X + gov - 0.02), (RIDGE + ye) / 2 + 0.02, s * (Z + ov) / 2], [0.04, 0.24, l2], [a, 0, 0], "oak_x", { k: 0 });
    }
  }
  A.obox([0, RIDGE + t / Math.cos(th) - 0.02, 0], [2 * X + 2 * gov + 0.04, 0.2, 0.2], [45, 0, 0], "ridge_tile");
}
export function quoins(A, X, Z, y0, y1) {
  for (const sx of [-1, 1]) for (const sz of [-1, 1])
    for (let i = 0, y = y0; y < y1 - 0.1; y += 0.31, i++) {
      const longX = i % 2 === 0, lx = longX ? 0.55 : 0.32, lz = longX ? 0.32 : 0.55;
      const xa = sx * (X + 0.015), xb = sx * (X - lx), za = sz * (Z + 0.015), zb = sz * (Z - lz);
      A.box([Math.min(xa, xb), y, Math.min(za, zb)], [Math.max(xa, xb), Math.min(y + 0.29, y1), Math.max(za, zb)], "stone_dressed");
    }
}

// =========================================================================== B1 architectural kit
// Facade depth, arches, columns, roofs and turrets from the B1 part types (Asset.cyl / cone / arch / wedge and the box
// "bv" chamfer; docs/asset-format.md#b1-geometry-kit). Shimmer rule (docs/rendering.md): no coplanar faces between parts, so every
// member is embedded 1 cm or more into the surface it sits on, and members that meet differ in projection.
// Wall frame F = { axis, d, out }: axis "x" = a wall along X (depth z), "z" = along Z (depth x); d = the exterior face;
// out = +1 / -1, the exterior direction along the depth axis. In F, a runs along the wall, y is up and e is the
// distance out from the face (negative = into the wall).
// None of these are destructible wall bays: put them on walls built with wall(), or on non-destructible cladding.
export const faceYaw = (axis, out) => (axis === "x" ? (out > 0 ? 0 : 180) : out > 0 ? 90 : -90);
const fpt = (F, a, y, e) => P(F.axis, a, y, F.d + F.out * e);
export function fbox(A, F, a0, a1, y0, y1, e0, e1, m, o = {}) { return wbox(A, F.axis, a0, a1, y0, y1, F.d + F.out * e0, F.d + F.out * e1, m, o); }
// a B1 part turned to face out of the wall: s = [along the wall, height, out of the wall]
export function fpart(A, kind, F, a, y, e, s, m, o = {}) { return A[kind](fpt(F, a, y, e), s, m, { ...o, r: [0, faceYaw(F.axis, F.out), 0] }); }
function ensureMat(A, name, ...def) { if (!A.materials.some((m) => m.name === name)) A.mat(name, ...def); }
const q4 = (v) => Math.round(v * 1e4) / 1e4;

// flat pilaster with a bevelled base and capital
export function pilaster(A, F, a, y0, y1, { w = 0.42, proj = 0.07, m = "stone_dressed", bv = 0.012 } = {}) {
  const bh = Math.min(0.3, (y1 - y0) * 0.1);
  fbox(A, F, a - w / 2 - 0.04, a + w / 2 + 0.04, y0, y0 + bh, -0.01, proj + 0.04, m, { bv });
  fbox(A, F, a - w / 2, a + w / 2, y0 + bh, y1 - bh, -0.01, proj, m, { bv });
  fbox(A, F, a - w / 2 - 0.05, a + w / 2 + 0.05, y1 - bh, y1, -0.01, proj + 0.05, m, { bv });
}
// a string course at height y: a bevelled band with a sloped weathering on top
export function stringCourse(A, F, a0, a1, y, { h = 0.16, proj = 0.06, m = "stone_dressed", bv = 0.01 } = {}) {
  fbox(A, F, a0, a1, y, y + h, -0.01, proj, m, { bv });
  fpart(A, "wedge", F, (a0 + a1) / 2, y + h * 1.2, (proj - 0.01) / 2, [a1 - a0 - 0.02, h * 0.4, proj + 0.01], m, { top: 0.2, k: 0 });
}
// stepped cornice under the eaves: `steps` bevelled bands growing out to `proj`, topped by a weathering.
// ext: the bands run past a0 / a1 by their projection (walls along X own the corners; the end walls pass ext: false)
export function cornice(A, F, a0, a1, y, { steps = 3, h = 0.36, proj = 0.3, m = "stone_dressed", bv = 0.012, ext = true } = {}) {
  extOf(A).cornices.push({ F, a0, a1, y, h, proj, steps, m });   // GX4: dentils (tools/exterior.mjs)
  for (let i = 0; i < steps; i++) {
    const p = (proj * (i + 1)) / steps, x = ext ? p : 0;
    fbox(A, F, a0 - x, a1 + x, y + (h * i) / steps, y + (h * (i + 1)) / steps, -0.01, p, m, { bv });
  }
  const x = ext ? proj - 0.01 : -0.01;
  fpart(A, "wedge", F, (a0 + a1) / 2, y + h + 0.04, (proj - 0.02) / 2, [a1 - a0 + 2 * x, 0.08, proj], m, { top: 0.3, k: 0 });
}
// window or door surround: architrave jambs and head proud of the wall, a sloped projecting sill, optional keystone
export function surround(A, F, { a0, a1, y0, y1, w = 0.14, proj = 0.05, m = "stone_dressed", sill = true, key = false, bv = 0.01 }) {
  fbox(A, F, a0 - w, a0, y0, y1, -0.01, proj, m, { bv });
  fbox(A, F, a1, a1 + w, y0, y1, -0.01, proj, m, { bv });
  fbox(A, F, a0 - w - 0.02, a1 + w + 0.02, y1, y1 + w, -0.01, proj + 0.015, m, { bv });
  if (sill) fpart(A, "wedge", F, (a0 + a1) / 2, y0 - 0.05, (proj + 0.04) / 2, [a1 - a0 + 2 * w + 0.12, 0.1, proj + 0.06], m, { top: 0.55 });
  if (key) fbox(A, F, (a0 + a1) / 2 - 0.09, (a0 + a1) / 2 + 0.09, y1 - 0.03, y1 + w + 0.07, -0.01, proj + 0.035, m, { bv });
}
// Arched opening a0..a1 springing at y1 with a semicircular (rise = half the span) or elliptical head:
//  - the voussoir ring (width w) proud of the face, with a keystone;
//  - wall: { e0, e1, m }: the spandrel that closes the wall's rectangular hole (cut it up to y1 + rise + 0.02) down to
//    the arch, inside the wall slab e0..e1 (e <= 0);
//  - jambs: y0 of dressed jambs below the springing; sill: a projecting sill under the jambs;
//  - fan: e of a fixed glass fanlight in the arch (with a transom bar at the springing).
export function archOpening(A, F, { a0, a1, y1, rise = (a1 - a0) / 2, w = 0.2, proj = 0.05, m = "stone_dressed", key = true, wall = null, jambs = null, sill = false, fan = null }) {
  const span = a1 - a0, ac = (a0 + a1) / 2, H = rise + w + 0.005;
  fpart(A, "arch", F, ac, y1 + H / 2, (proj - 0.01) / 2, [span + 2 * w + 0.01, H, proj + 0.01], m, { th: w });
  if (key) fbox(A, F, ac - 0.1, ac + 0.1, y1 + rise - 0.02, y1 + rise + w + 0.06, -0.01, proj + 0.025, m, { bv: 0.01 });
  if (wall) fpart(A, "arch", F, ac, y1 + (rise + 0.02) / 2, (wall.e0 + wall.e1) / 2, [span, rise + 0.02, Math.abs(wall.e1 - wall.e0)], wall.m || "stone", { th: 0.02, fill: true });
  if (jambs !== null) {
    fbox(A, F, a0 - w, a0, jambs, y1, -0.01, proj - 0.005, m, { bv: 0.01 });
    fbox(A, F, a1, a1 + w, jambs, y1, -0.01, proj - 0.005, m, { bv: 0.01 });
  }
  if (sill) fpart(A, "wedge", F, ac, (jambs ?? y1) - 0.05, (proj + 0.04) / 2, [span + 2 * w + 0.12, 0.1, proj + 0.06], m, { top: 0.55 });
  if (fan !== null) {
    fpart(A, "arch", F, ac, y1 + rise / 2, fan, [span, rise, 0.02], "glass", { th: 99, k: 0 });
    fbox(A, F, a0, a1, y1 - 0.03, y1 + 0.03, fan - 0.03, fan + 0.03, "window_paint", { k: 0 });
  }
}
export const archWindow = (A, F, o) => archOpening(A, F, { sill: true, fan: -0.12, ...o });
export const archDoor = (A, F, o) => archOpening(A, F, { jambs: 0, ...o });
// Arcade along a line (axis as wall(), at depth d): n round (or `sides`-sided) columns from a0 to a1 on bevelled
// plinths with capitals, semicircular arches from column to column, recessed spandrels and an entablature beam up to
// y1. Returns the springing height.
export function colonnade(A, { axis, a0, a1, d, y0, y1, n, r = 0.2, depth = 0.5, th = 0.3, beam = 0.4, sides = 0, m = "stone_dressed" }) {
  const sp = (a1 - a0) / (n - 1), ys = y1 - beam - sp / 2 - 0.01, F = { axis, d, out: 1 }, cd = Math.max(1.2 * r, depth / 2 + 0.01);
  for (let i = 0; i < n; i++) {
    const a = a0 + sp * i;
    A.box(P(axis, a - 1.3 * r, y0, d - 1.3 * r), P(axis, a + 1.3 * r, y0 + 0.25, d + 1.3 * r), m, { bv: 0.015 });
    A.cyl(P(axis, a, (y0 + 0.25 + ys - 0.22) / 2, d), [2 * r, ys - 0.22 - y0 - 0.25, 2 * r], m, { n: sides });
    const p0 = P(axis, a - 1.2 * r, ys - 0.22, d - cd), p1 = P(axis, a + 1.2 * r, ys, d + cd);
    A.box(p0, p1, m, { bv: 0.015 });
  }
  for (let i = 0; i + 1 < n; i++) {
    const ac = a0 + sp * (i + 0.5);
    fpart(A, "arch", F, ac, ys + sp / 4, 0, [sp, sp / 2, depth], m, { th });
    fpart(A, "arch", F, ac, ys + (sp / 2 + 0.01) / 2, 0, [sp, sp / 2 + 0.01, depth - 0.02], m, { th: 0.01, fill: true });
  }
  A.box(P(axis, a0 - 1.4 * r, y1 - beam, d - depth / 2 - 0.03), P(axis, a1 + 1.4 * r, y1, d + depth / 2 + 0.03), m, { bv: 0.015 });
  return ys;
}
// Balcony on a wall: a bevelled slab on stone consoles with an iron railing (hidden colliders keep people on it)
export function balcony(A, F, { a0, a1, y, depth = 0.85, t = 0.14, h = 0.95, m = "stone_dressed", rail = "iron" }) {
  fbox(A, F, a0, a1, y - t, y, -0.02, depth, m, { bv: 0.015 });
  const nb = Math.max(2, Math.round((a1 - a0) / 1.4) + 1), bh = Math.min(0.5, depth * 0.6);
  for (let i = 0; i < nb; i++) {
    const a = a0 + 0.15 + ((a1 - a0 - 0.3) * i) / (nb - 1);
    fpart(A, "wedge", F, a, y - t - bh / 2, (depth * 0.75 - 0.02) / 2, [0.16, bh, depth * 0.75 + 0.02], m, { top: 0.25 });
  }
  const e1 = depth - 0.05, yb = (2 * y + h - 0.05) / 2;
  fbox(A, F, a0 + 0.02, a1 - 0.02, y + h - 0.05, y + h, e1 - 0.03, e1 + 0.03, rail, { k: 0 });
  fbox(A, F, a0 + 0.04, a1 - 0.04, y + 0.06, y + 0.1, e1 - 0.02, e1 + 0.02, rail, { k: 0 });
  for (const aa of [a0 + 0.05, a1 - 0.05]) {
    fbox(A, F, aa - 0.03, aa + 0.03, y + h - 0.05, y + h, 0, e1 - 0.03, rail, { k: 0 });
    for (let e = 0.12; e < e1 - 0.06; e += 0.12) fpart(A, "cyl", F, aa, yb, e, [0.022, h - 0.05, 0.022], rail, { n: 4, k: 0 });
  }
  for (let a = a0 + 0.17; a < a1 - 0.12; a += 0.12) fpart(A, "cyl", F, a, yb, e1, [0.022, h - 0.05, 0.022], rail, { n: 4, k: 0 });
  fbox(A, F, a0, a1, y, y + h, e1 - 0.03, e1 + 0.03, rail, { v: 0 });
  for (const aa of [a0, a1 - 0.06]) fbox(A, F, aa, aa + 0.06, y, y + h, 0, e1, rail, { v: 0 });
}
// Gabled dormer with its front at z (facing s = +1 / -1 along z) and sill height y, w wide, reaching `depth` back
// into the roof: cheeks, a glazed front with frame, a small gable roof with a round ridge
export function dormer(A, { x, z, y, w = 1.2, h = 1.3, depth = 1.6, s = 1, m = "plaster", roof = "slate", frame = "window_paint", ridge = "ridge_tile" }) {
  const zb = z - s * depth, z0 = Math.min(z, zb), z1 = Math.max(z, zb), x0 = x - w / 2, x1 = x + w / 2, t = 0.1;
  A.box([x0, y - 0.12, z0], [x0 + t, y + h, z1], m);
  A.box([x1 - t, y - 0.12, z0], [x1, y + h, z1], m);
  const F = { axis: "x", d: z, out: s };
  fbox(A, F, x0 + t, x1 - t, y - 0.12, y, -0.14, -0.005, m);                          // apron under the window
  fbox(A, F, x0 + t, x1 - t, y, y + 0.07, -0.08, 0, frame);
  fbox(A, F, x0 + t, x1 - t, y + h - 0.07, y + h, -0.08, 0, frame);
  fbox(A, F, x - 0.03, x + 0.03, y + 0.07, y + h - 0.07, -0.07, -0.01, frame, { k: 0 });
  fbox(A, F, x0 + t, x1 - t, y + 0.07, y + h - 0.07, -0.05, -0.04, "glass", { k: 0 });
  const half = w / 2 + 0.15, rh = half * 0.9, yr = y + h, th = Math.atan2(rh, half), deg = (th * 180) / Math.PI, L = (half + 0.02) / Math.cos(th);
  A.prism(2, z0 + 0.02, z1 - 0.02, [[x0 + 0.005, yr], [x1 - 0.005, yr], [x, yr + rh - 0.06]], m, { k: 0 });   // gable infill
  for (const sx of [-1, 1]) {
    const nx = -sx * Math.sin(th), ny = Math.cos(th);
    A.obox([x + (sx * half) / 2 + nx * 0.04, yr + rh / 2 + ny * 0.04, (z0 + z1) / 2 + s * 0.1], [L, 0.08, depth + 0.2], [0, 0, -sx * deg], roof);
  }
  A.cyl([x, yr + rh + 0.05, (z0 + z1) / 2 + s * 0.1], [0.14, depth + 0.24, 0.14], ridge, { r: [90, 0, 0], k: 0 });
}
// Hip roof over |x| <= X, |z| <= Z with eaves at EAVE and ridge at RIDGE (equal pitch on all four sides, the ridge
// along the longer side): a solid prism over the middle, square pyramids for the hips (2 cm in, so their faces stay
// under the main slopes) and a round ridge tile
export function hipRoof(A, { X, Z, EAVE, RIDGE, ov = 0.4, m = "slate", ridge = "ridge_tile" }) {
  const alongX = X >= Z, L = alongX ? X : Z, S = alongX ? Z : X;
  const pitch = (RIDGE - EAVE) / S, Lo = L + ov, So = S + ov, ye = EAVE - ov * pitch, run = Lo - So;
  extOf(A).hips.push({ X, Z, EAVE, RIDGE, ov, m });   // GX4: tile courses (tools/exterior.mjs)
  const pt = (l, y, s) => (alongX ? [l, y, s] : [s, y, l]);
  if (run > 0.01) A.prism(alongX ? 0 : 2, -run, run, [[-So, ye], [So, ye], [0, RIDGE]], m);
  const Sp = So - 0.02, Hp = pitch * Sp;
  for (const sg of run > 0.01 ? [-1, 1] : [0]) A.cone(pt(sg * run, ye + 0.005 + Hp / 2, 0), [2 * Sp, Hp, 2 * Sp], m, { n: 4 });
  if (run > 0.01) A.cyl(pt(0, RIDGE + 0.02, 0), [0.2, 2 * run + 0.1, 0.2], ridge, { r: alongX ? [0, 0, 90] : [90, 0, 0], k: 0 });
}
// Thatched gable roof (ridge along X): thick rounded slopes with deep eaves, a round ridge roll, plastered gables
export function thatchRoof(A, { X, Z, EAVE, RIDGE, ov = 0.5, t = 0.45, gov = 0.35, m = "thatch", gable = "plaster" }) {
  ensureMat(A, "thatch", [0.30, 0.235, 0.13], 0.97, "fabric", { grain: 2 });
  const th = Math.atan2(RIDGE - EAVE, Z), deg = (th * 180) / Math.PI;
  for (const sx of [-1, 1]) A.prism(0, sx > 0 ? X - 0.3 : -X, sx > 0 ? X : -X + 0.3, [[-Z, EAVE], [Z, EAVE], [0, RIDGE]], gable);
  for (const s of [1, -1]) {
    const len = (Z + ov + 0.15) / Math.cos(th), zm = (s * (Z + ov) - s * 0.15) / 2, ym = RIDGE - Math.abs(zm) * Math.tan(th);
    const ny = Math.cos(th), nz = s * Math.sin(th);
    A._put({ t: "r", c: [0, ym + (ny * t) / 2, zm + (nz * t) / 2].map(q4), s: [2 * X + 2 * gov, t, len].map(q4), rad: q4(t * 0.45), m }, { r: [s > 0 ? deg : -deg, 0, 0] });
  }
  A.cyl([0, RIDGE + t / Math.cos(th) - 0.12, 0], [t * 1.3, 2 * X + 2 * gov + 0.1, t * 1.3], m, { r: [0, 0, 90] });
}
// Round (sides 0) or polygonal turret: plinth ring, body, a cornice band and a conical spire with an iron finial.
// windows [{a, y, w, h}] (a: degrees from +x toward +z, y: the window's centre) make the body a hollow ring (wall t)
// with real openings; floors: visual floor discs inside at those heights.
export function turret(A, { x, z, y0, y1, r = 1.4, spire = 4.5, sides = 0, ov = 0.25, m = "stone", band = "stone_dressed", roof = "slate", finial = "iron", windows = [], t = Math.min(0.45, r * 0.25), floors = [] }) {
  if (windows.length) turretRing(A, { x, z, y0, y1: y1 - 0.01, r, sides, m, t, windows, floors });
  else A.cyl([x, (y0 + y1 - 0.01) / 2, z], [2 * r, y1 - 0.01 - y0, 2 * r], m, { n: sides });
  A.cyl([x, y0 + 0.18, z], [2 * r + 0.12, 0.36, 2 * r + 0.12], band, { n: sides });
  A.cyl([x, y1 - 0.125, z], [2 * r + 0.18, 0.24, 2 * r + 0.18], band, { n: sides });
  A.cone([x, y1 - 0.02 + spire / 2, z], [2 * (r + ov), spire, 2 * (r + ov)], roof, { n: sides });   // base 1.5 cm into the band
  A.cyl([x, y1 + spire + 0.35, z], [0.05, 1.0, 0.05], finial, { n: 8, k: 0 });
  A.cyl([x, y1 + spire + 0.3, z], [0.14, 0.14, 0.14], finial, { n: 8, k: 0 });
}
// The hollow turret body: flat wall facets (a polygon's faces; round: facets of <= 9 degrees, inscribed in r), each
// built in a local frame (a wall along x facing +z) and turned into place. A facet with windows gets the kit window
// (fixed casements: a turned aperture box cannot be an opening, so the leaves stay closed as static parts, no
// shutters) and a dressed surround, its wall cut round the reveals.
function turretRing(A, { x, z, y0, y1, r, sides, m, t, windows, floors }) {
  const wrap = (a) => ((a % 360) + 540) % 360 - 180, need = (w) => w.w / 2 + 0.22;   // lintel / surround + a pier
  let facets = [];
  if (sides) for (let i = 0; i < sides; i++) facets.push([(i - 0.5) * (360 / sides), (i + 0.5) * (360 / sides)]);
  else {
    const at = new Map();   // window facets: one per angle, as wide as its widest window needs
    for (const w of windows) { const a = wrap(w.a), h = (Math.asin(Math.min(1, need(w) / r)) * 180) / Math.PI; at.set(a, Math.max(at.get(a) ?? 0, h)); }
    const ws = [...at].sort((p, q) => p[0] - q[0]).map(([a, h]) => [a - h, a + h]);
    ws.forEach((f, i) => { const nx = ws[(i + 1) % ws.length][0] + (i + 1 === ws.length ? 360 : 0); if (f[1] > nx + 1e-6) throw new Error(`turret windows at ${f} overlap`); });
    ws.forEach((f, i) => {
      facets.push(f);
      const g0 = f[1], g1 = ws[(i + 1) % ws.length][0] + (i + 1 === ws.length ? 360 : 0), n = Math.ceil((g1 - g0) / 9 - 1e-6);
      for (let k = 0; k < n; k++) facets.push([g0 + ((g1 - g0) * k) / n, g0 + ((g1 - g0) * (k + 1)) / n]);
    });
  }
  const Rv = sides ? r / Math.cos(Math.PI / sides) : r;   // vertex radius (a polygon's faces stand at r, as the cylinder's)
  let ri = 1e9, id = 0;
  for (const [f0, f1] of facets) {
    const mid = (f0 + f1) / 2, hf = ((f1 - f0) * Math.PI) / 360, ap = Rv * Math.cos(hf), hl = Rv * Math.sin(hf);
    ri = Math.min(ri, ap - t);
    const n0 = A.parts.length, i0 = A.interactables.length, s0 = extOf(A).sills.length, holes = [];
    for (const w of windows.filter((q) => Math.abs(wrap(q.a - mid)) < 1e-3 * (f1 - f0) + 1e-6)) {
      if (need(w) > hl + 1e-6) throw new Error(`turret window ${w.w} m too wide for a ${(2 * hl).toFixed(2)} m facet`);
      const sill = w.y - w.h / 2;
      holes.push(window(A, { id: `turret_${id++}`, axis: "x", c: 0, sill, w: w.w, h: w.h, outer: ap, inner: ap - t, shutters: false }));
      surround(A, { axis: "x", d: ap, out: 1 }, { a0: -w.w / 2, a1: w.w / 2, y0: sill, y1: sill + w.h });
    }
    for (const it of A.interactables.splice(i0)) for (const L of it.leaves) A.parts.push(...L.parts);   // leaves closed, fixed
    extOf(A).sills.splice(s0);   // GX4 rain-streak anchors are axis-aligned wall faces: none on a turned facet
    for (const [a0, a1, ya, yb] of cutRect(-hl, hl, y0, y1, holes)) wbox(A, "x", a0, a1, ya, yb, ap - t, ap, m);
    const yaw = 90 - mid;   // local +z onto the facet's outward direction (cos mid, sin mid)
    for (const p of A.parts.slice(n0)) {
      if (p.r && (p.r[0] || p.r[2])) throw new Error("turretRing: only parts turned about y can be turned into place");
      const c = rotY(p.c, [0, 0, 0], yaw);
      p.c = [x + c[0], c[1], z + c[2]].map((v) => Math.round(v * 10000) / 10000);
      p.r = [0, Math.round(((p.r?.[1] ?? 0) + yaw) * 10000) / 10000, 0];
    }
  }
  for (const fy of floors) A.cyl([x, fy - 0.06, z], [2 * ri + 0.04, 0.12, 2 * ri + 0.04], "oak_x", { n: sides, k: 0 });   // 2 cm into the walls
}
// Half-timbering on a wall face (Norman colombage): rails at y0 (sill), y1 (plate) and each `rails` height, posts
// every `sp` between them and beside each hole {a0, a1, y0, y1}, head and sill rails over and under the holes, braces
// in the end bays; members proud of the infill by `proj` and embedded `embed` into it
export function timberFrame(A, F, { a0, a1, y0, y1, holes = [], rails = [], t = 0.16, sp = 0.85, proj = 0.04, embed = 0.03, braces = true, m = "oak_y", mx = "oak_x" }) {
  const box = (aa0, aa1, ya, yb, mat) => fbox(A, F, aa0, aa1, ya, yb, -embed, proj, mat);
  const bands = [[y0, y0 + t], ...rails.map((r) => [r - t / 2, r + t / 2]), [y1 - t, y1]].sort((p, q) => p[0] - q[0]);
  const n0 = A.parts.length, rec = () => { extOf(A).timber.push({ F, proj, embed, parts: A.parts.slice(n0) }); };   // GX4 exterior pass (tools/exterior.mjs)
  const hv = holes.map((h) => ({ ...h, c0: h.y0 - t * 0.8, c1: h.y1 + t * 0.8 }));
  const inWall = (a) => a > a0 + t / 2 - 1e-6 && a < a1 - t / 2 + 1e-6;
  for (const [b0, b1] of bands) {   // rails, cut where a hole crosses the band (over the hole and its jamb posts)
    const cut = hv.filter((h) => h.y0 < b1 && h.y1 > b0).map((h) => ({ a0: h.a0 - t, a1: h.a1 + t, y0: b0, y1: b1 }));
    for (const [aa0, aa1, ya, yb] of cutRect(a0, a1, b0, b1, cut)) box(aa0, aa1, ya, yb, mx);
  }
  for (const h of hv) {
    // head and sill rails between the jamb posts, unless a rail band is already there
    if (h.c1 < y1 && !bands.some(([b0, b1]) => b0 < h.c1 && b1 > h.y1)) box(h.a0, h.a1, h.y1, h.c1, mx);
    if (h.c0 > y0 && !bands.some(([b0, b1]) => b0 < h.y0 && b1 > h.c0)) box(h.a0, h.a1, h.c0, h.y0, mx);
    // jamb posts run from the band under the hole to the band over it, through the bands the hole cuts
    const below = bands.filter(([, b1]) => b1 <= h.y0 + 1e-6).map((b) => b[1]), above = bands.filter(([b0]) => b0 >= h.y1 - 1e-6).map((b) => b[0]);
    const ya = below.length ? Math.max(...below) : y0, yb = above.length ? Math.min(...above) : y1;
    for (const a of [h.a0 - t / 2, h.a1 + t / 2]) if (inWall(a)) box(a - t / 2, a + t / 2, ya, yb, m);
  }
  const jambs = holes.flatMap((h) => [h.a0 - t / 2, h.a1 + t / 2]), posts = [a0 + t / 2, a1 - t / 2].filter((a) => jambs.every((q) => Math.abs(q - a) > t * 1.5));
  for (let a = a0 + sp; a < a1 - sp * 0.5; a += sp) if ([...posts, ...jambs].every((q) => Math.abs(q - a) > t * 1.5)) posts.push(a);
  for (const a of posts) {
    if (!inWall(a)) continue;
    const cut = hv.filter((h) => a > h.a0 - t / 2 && a < h.a1 + t / 2).map((h) => ({ a0: a - t, a1: a + t, y0: h.c0, y1: h.c1 }));
    for (let i = 0; i + 1 < bands.length; i++)
      for (const [, , ya, yb] of cutRect(a - t / 2, a + t / 2, bands[i][1], bands[i + 1][0], cut)) if (yb - ya > 0.05) box(a - t / 2, a + t / 2, ya, yb, m);
  }
  if (!braces) return rec();
  for (let i = 0; i + 1 < bands.length; i++) {   // one brace in each end bay of each storey, unless a hole is there
    const g0 = bands[i][1], g1 = bands[i + 1][0], bw = Math.min(sp, (a1 - a0) / 3);
    for (const [p0, p1] of [[a0 + t, a0 + bw], [a1 - t, a1 - bw]]) {
      const lo = Math.min(p0, p1), hi = Math.max(p0, p1);
      if (g1 - g0 < 0.4 || hv.some((h) => h.a1 + t > lo && h.a0 - t < hi && h.c1 > g0 && h.c0 < g1)) continue;
      const da = p1 - p0, dy = g1 - g0, Lb = Math.hypot(da, dy) - t * 0.4, ang = (Math.atan2(F.axis === "x" ? -da : da, dy) * 180) / Math.PI;
      const dep = proj - 0.008 + embed, c = fpt(F, (p0 + p1) / 2, (g0 + g1) / 2, (proj - 0.008 - embed) / 2);
      A.obox(c, F.axis === "x" ? [t * 0.85, Lb, dep] : [dep, Lb, t * 0.85], F.axis === "x" ? [0, 0, ang] : [ang, 0, 0], m);
    }
  }
  rec();
}
// cap on a chimney stack (x, z, top y, w x d): a bevelled oversailing course, a cap slab and clay pots
export function chimneyCap(A, { x, z, y, w = 0.7, d = 0.5, m = "stone_dressed", pots = 2, pot = "terracotta" }) {
  A.box([x - w / 2 - 0.05, y - 0.16, z - d / 2 - 0.05], [x + w / 2 + 0.05, y - 0.04, z + d / 2 + 0.05], m, { bv: 0.012 });
  A.box([x - w / 2 - 0.09, y - 0.04, z - d / 2 - 0.09], [x + w / 2 + 0.09, y + 0.06, z + d / 2 + 0.09], m, { bv: 0.015 });
  for (let i = 0; i < pots; i++) A.cone([x - w / 2 + (w * (i + 0.5)) / pots, y + 0.05 + 0.22, z], [0.2, 0.44, 0.2], pot, { top: 0.78, k: 0 });
}
export function chimney(A, { x, z, y0, y1, w = 0.7, d = 0.5, m = "stone", ...o }) {
  A.box([x - w / 2, y0, z - d / 2], [x + w / 2, y1 - 0.15, z + d / 2], m);
  chimneyCap(A, { x, z, y: y1, w, d, ...o });
}

// --------------------------------------------------------------------------- B1 imperfections
// Seeded by the asset id (the same every generator run). Call it last, after every trim is placed; keep: parts left alone.
//  - jitter: every bevelled trim (box with bv) slides up to `jitter` m along its length;
//  - chips: that share of bevelled trims (>= 25 cm long) loses a top corner at one end: the end piece is lowered and a
//    broken wedge fills the notch (the pieces never share a face plane);
//  - pocks: [{F, a0, a1, y0, y1, n}] n shell scars on non-destructible cladding in that wall rectangle: a spalled patch
//    of stone where the render fell, ringed by small dark pocks, all a few mm proud and embedded.
export function weather(A, { jitter = 0.015, chips = 0.15, pocks = [], seed = A.meta.id, keep = [] } = {}) {
  let h = 2166136261;
  for (let i = 0; i < seed.length; i++) { h ^= seed.charCodeAt(i); h = Math.imul(h, 16777619); }
  const R = () => { h ^= h << 13; h ^= h >>> 17; h ^= h << 5; return ((h >>> 0) % 100000) / 100000; };
  const out = [];
  for (const p of A.parts) {
    if (p.t !== "b" || !(p.bv > 0) || p.r || p.q || keep.includes(p)) { out.push(p); continue; }
    const L = p.s[0] >= p.s[2] ? 0 : 2, T = 2 - L;
    p.c[L] = q4(p.c[L] + (R() - 0.5) * 2 * jitter);
    if (!(R() < chips && p.s[L] >= 0.25 && p.s[1] >= 0.08)) { out.push(p); continue; }
    const s = R() < 0.5 ? -1 : 1, lo = p.c[L] - p.s[L] / 2, hi = p.c[L] + p.s[L] / 2, top = p.c[1] + p.s[1] / 2;
    const c = Math.min(0.04 + 0.08 * R(), p.s[L] * 0.3), dh = Math.min(0.03 + 0.06 * R(), p.s[1] * 0.45);
    const piece = (l0, l1, yTop) => {
      const q = { ...p, c: [...p.c], s: [...p.s] };
      q.c[L] = q4((l0 + l1) / 2); q.s[L] = q4(l1 - l0); q.c[1] = q4((top - p.s[1] + yTop) / 2); q.s[1] = q4(yTop - (top - p.s[1]));
      q.bv = q4(Math.min(p.bv, ...q.s.map((x) => x / 2)));
      return q;
    };
    const e0 = s > 0 ? hi - c : lo, e1 = s > 0 ? hi : lo + c;
    out.push(s > 0 ? piece(lo, hi - c, top) : piece(lo + c, hi, top), piece(e0, e1, top - dh));
    const wc = [...p.c]; wc[L] = q4((e0 + e1) / 2); wc[1] = q4(top - dh / 2);
    const yaw = L === 0 ? (s > 0 ? 90 : -90) : s > 0 ? 0 : 180;   // the wedge's full height toward the trim, falling to its end
    out.push({ t: "w", c: wc, s: [p.s[T], dh, c].map(q4), r: [0, yaw, 0], top: q4(0.15 + 0.35 * R()), m: p.m, k: 0 });
  }
  A.parts.length = 0; A.parts.push(...out);
  // shell scars: discs facing out of the wall (cylinder axis turned onto the wall normal)
  for (const { F, a0, a1, y0, y1, n = 3 } of pocks) {
    const rot = F.axis === "x" ? [F.out > 0 ? 90 : -90, 0, 0] : [0, 0, F.out > 0 ? -90 : 90];
    const disc = (a, y, e, r, th, m) => A.cyl(P(F.axis, a, y, F.d + F.out * (e - th / 2)), [2 * r, th, 2 * r], m, { r: rot, k: 0 });
    for (let i = 0; i < n; i++) {
      const a = a0 + (0.1 + 0.8 * R()) * (a1 - a0), y = y0 + (0.1 + 0.8 * R()) * (y1 - y0), r = 0.07 + 0.13 * R();
      if (R() < 0.6) disc(a, y, 0.002 + 0.003 * R(), r, 0.014, "stone");   // render spalled off: the stone shows
      for (let k = 0, m = 2 + Math.floor(R() * 4); k < m; k++) {
        const ang = R() * 6.2832, d = r * (0.3 + 0.9 * R());
        disc(a + Math.cos(ang) * d, y + Math.sin(ang) * d, 0.006 + 0.004 * R(), 0.012 + 0.022 * R(), 0.016, "rubble_core");
      }
    }
  }
}

// --------------------------------------------------------------------------- VF1 facade trim on wall bays
// A facade trim box (built into A.parts) that stands on a wall bay's outer face moves into that bay's intact state,
// cut at the bay's edges along y: it is drawn with the bay and goes when the bay is damaged, so it never floats over a
// breach. The pieces between bays (a floor band, a static corner) stay static. side: "front" (+z) or "rear" (-z).
export function bindToBays(A, p, side) {
  const s = side === "front" ? 1 : -1, back = p.c[2] - (s * p.s[2]) / 2, a0 = p.c[0] - p.s[0] / 2, a1 = p.c[0] + p.s[0] / 2;
  let y0 = p.c[1] - p.s[1] / 2;
  const y1 = p.c[1] + p.s[1] / 2;
  const bays = A.panels.filter((b) => b.normal && b.normal[2] === s && b.states?.[0]?.length && Math.abs((s > 0 ? b.max[2] : b.min[2]) - back) < 0.08
    && b.min[0] <= a0 + 1e-4 && b.max[0] >= a1 - 1e-4 && b.max[1] > y0 && b.min[1] < y1).sort((q, r) => q.min[1] - r.min[1]);
  if (!bays.length) return 0;
  const i = A.parts.indexOf(p); if (i >= 0) A.parts.splice(i, 1);
  const piece = (ya, yb, into) => { if (yb - ya > 0.02) A.box([a0, ya, p.c[2] - p.s[2] / 2], [a1, yb, p.c[2] + p.s[2] / 2], p.m, { k: 0, lod: p.lod, into }); };
  for (const b of bays) {
    const ya = Math.max(y0, b.min[1]), yb = Math.min(y1, b.max[1]);
    if (ya > y0 + 1e-4) piece(y0, ya, A.parts);   // the gap below this bay stays static
    piece(ya, yb, b.states[0]);
    y0 = Math.max(y0, yb);
  }
  if (y1 > y0 + 1e-4) piece(y0, y1, A.parts);
  return bays.length;
}
// Zinc downpipes from the eaves gutters of a gable roof (gableRoof) to the plinth, square section, one toward each end
// of the front and rear facades: at the outermost x where wall bays run unbroken from the pipe's foot to its head (the
// piers between openings; the corners are static slabs that become bays only at load), so every length of pipe is bound
// to the bay behind it and goes with it. A swan neck from the gutter outlet back to the wall, brackets, a shoe over the
// plinth; all far-LOD detail (lod 1). Skipped on a facade that already has a downpipe. Returns the number of pipes.
export function downpipes(A, { X, Z, Zr, y0, EAVE, RIDGE, ov = 0.4, openings = [], m = "zinc" }) {
  const th = Math.atan2(RIDGE - EAVE, Zr), ye = EAVE - ov * Math.tan(th), gy = ye - 0.2, w = 0.08, D = { k: 0, lod: 1 };
  const has = (s) => A.parts.some((p) => (p.m === "zinc" || p.m === "iron") && (p.t === "b" || p.t === "c") && p.s[1] > 2 && p.s[0] < 0.16 && p.s[2] < 0.16 && Math.sign(p.c[2]) === s && Math.abs(Math.abs(p.c[2]) - Z) < 0.3);
  let n = 0;
  for (const [side, s] of [["front", 1], ["rear", -1]]) {
    if (has(s)) continue;
    const ops = openings.filter((o) => o.side === side);
    const zf = s * Z, zp = zf + s * (0.035 + w / 2), zg = s * (Zr + ov + 0.065);   // wall face, pipe centre, gutter outlet
    const top = gy - 0.32, bot = y0 + 0.2;
    if (top - bot < 1.5) continue;
    const bays = A.panels.filter((b) => b.normal && b.normal[2] === s && Math.abs((s > 0 ? b.max[2] : b.min[2]) - zf) < 0.05);
    const covered = (a) => {   // bays under [a - 0.1, a + 0.1] from bot to top, with no gap
      const rows = bays.filter((b) => b.min[0] <= a - 0.1 && b.max[0] >= a + 0.1).map((b) => [b.min[1], b.max[1]]).sort((p, q) => p[0] - q[0]);
      let y = bot;
      for (const [r0, r1] of rows) { if (r0 > y + 0.01) return false; y = Math.max(y, r1); }
      return y >= top - 0.01;
    };
    for (const e of [-1, 1]) {
      let a = null;
      for (let c = e * (X - 0.3); e * c > 0.6 && a === null; c -= e * 0.05) if (covered(c) && ops.every((o) => Math.abs(c - o.c) > o.w / 2 + 0.2)) a = c;
      if (a === null) continue;
      const pipe = A.box([a - w / 2, bot, zp - w / 2], [a + w / 2, top, zp + w / 2], m, D);
      bindToBays(A, pipe, side);
      // swan neck: outlet drop, the sloped offset back to the wall, the head of the pipe
      A.box([a - w / 2, gy - 0.1, zg - w / 2], [a + w / 2, gy + 0.005, zg + w / 2], m, D);
      const dz = zp - zg, dy = -0.22, L = Math.hypot(dz, dy), ang = (Math.atan2(dz, -dy) * 180) / Math.PI;
      A.obox([a, gy - 0.1 + dy / 2, zg + dz / 2], [w, L + 0.04, w], [-ang, 0, 0], m, D);
      A.box([a - w / 2, top - 0.005, zp - w / 2], [a + w / 2, gy - 0.3, zp + w / 2], m, D);
      // shoe over the plinth
      A.obox([a, bot - 0.06, zp + s * 0.04], [w, 0.16, w], [-s * 40, 0, 0], m, D);
      // brackets into the wall, every ~1.7 m
      for (let y = bot + 0.5; y < top - 0.2; y += 1.7) {
        const b = A.box([a - w / 2 - 0.015, y - 0.02, Math.min(zf - s * 0.006, zp - s * w / 2 + s * 0.004)], [a + w / 2 + 0.015, y + 0.02, Math.max(zf - s * 0.006, zp - s * w / 2 + s * 0.004)], m, D);
        if (b) bindToBays(A, b, side);
      }
      ++n;
    }
  }
  return n;
}
