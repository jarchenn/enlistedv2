// MOD1 (docs/modern-assets-brief.md): enterable modern buildings, set A. Five residential types, three mom-and-pop
// restaurants, a generic burger drive-through, a petrol station and a bus station. Regenerate with
// `node tools/build_mpu_assets.mjs` (never build_assets.mjs).
//
// Every building is laid out by the B3 house builder (tools/catalog_chateau.mjs `house`: two-layer walls with real
// openings, destructible bays, door and casement interactables, two stair cores off a rear corridor, nav, tour) with
// the house kit's materials re-tinted to modern finishes (precast concrete sills and steps, anthracite aluminium frames,
// white joinery, oak laminate, porcelain tile) and flat roofs behind parapets. Buildings that need an off-centre
// footprint (garage, drive-through lane, forecourt, platforms) are built at the origin and moved in with placeSub (a
// copy of the one in catalog_gx_buildings.mjs, which does not export it). Commercial fittings that define a building
// (counters, booths, fryer line, hoods, pumps, benches, ticket desk) are fixed parts; the auto-furnisher dresses the
// rest by A.meta.roomTypes. Signs are blank coloured panels only.
import { variedWalls } from "./wall_variety.mjs";
import { Asset, wbox, P, cutRect } from "./kit.mjs";
import { partition, door, fbox, fpart } from "./parts.mjs";
import { mats, face, house, meta } from "./catalog_chateau.mjs";
import { cover, room, corners, setTour } from "./catalog_atlantic.mjs";
import { rod } from "./defence_detail.mjs";

const GF = 0.3, L1 = { k: 0, lod: 1 }, BV = { bv: 0.012 };
const q4 = (v) => Math.round(v * 1e4) / 1e4;

// ============================================================================ materials
function modMats(A) {
  mats(A);
  const re = (n, o) => Object.assign(A.materials.find((m) => m.name === n), o);
  // the house kit's names stay (interior.cpp reads floor_x / terracotta / tread_x by name); only their finish changes
  re("stone_dressed", { albedo: [0.60, 0.59, 0.56], roughness: 0.85, pattern: "concrete", texture: "concrete_light" });   // sills, lintels, steps
  re("stone_plinth", { albedo: [0.42, 0.41, 0.39], roughness: 0.9, pattern: "concrete", texture: "concrete_aged" });
  for (const n of ["oak_x", "oak_y", "oak_z"]) re(n, { albedo: [0.80, 0.79, 0.76], roughness: 0.5, pattern: "painted", texture: "paint_wood_white" });
  re("window_paint", { albedo: [0.075, 0.08, 0.085], roughness: 0.4, pattern: "metal", metallic: 0.3 });
  re("door_paint", { albedo: [0.10, 0.11, 0.12], roughness: 0.45, pattern: "metal" });
  for (const n of ["floor_x", "landing_x", "tread_x"]) re(n, { albedo: [0.46, 0.35, 0.23], roughness: 0.45, texture: "wood_oak" });
  re("terracotta", { albedo: [0.56, 0.55, 0.53], roughness: 0.45, texture: "floor_cement_tile" });
  re("plaster", { albedo: [0.80, 0.79, 0.76], texture: "plaster_painted" });
  re("iron", { albedo: [0.05, 0.05, 0.05], });
  re("zinc", { texture: "steel_scratched" });
  re("concrete_floor", { albedo: [0.45, 0.44, 0.42], texture: "concrete_light" });
  for (const [n, g] of [["trim_x", 0], ["trim_y", 1], ["trim_z", 2]]) A.mat(n, [0.82, 0.81, 0.78], 0.5, "painted", { texture: "paint_wood_white", grain: g });   // interiorTrim keeps these
  A.mat("render_m", [0.80, 0.79, 0.76], 0.9, "plaster", { texture: "render_lime" });
  A.mat("render_grey", [0.33, 0.34, 0.35], 0.9, "plaster", { texture: "render_lime" });
  A.mat("brick_slip", [0.46, 0.30, 0.22], 0.85, "brick", { texture: "brick_muted" });
  A.mat("brick_grey", [0.42, 0.40, 0.38], 0.85, "brick", { texture: "brick_factory" });
  A.mat("clad_wood", [0.44, 0.30, 0.18], 0.7, "wood", { texture: "wood_dark", grain: 1 });
  A.mat("metal_panel", [0.20, 0.21, 0.22], 0.45, "metal", { metallic: 0.4 });
  A.mat("alu", [0.62, 0.63, 0.64], 0.35, "metal", { texture: "steel_scratched", metallic: 0.9 });
  A.mat("stainless", [0.70, 0.70, 0.70], 0.25, "metal", { texture: "steel_scratched", metallic: 1.0 });
  A.mat("hvac", [0.66, 0.66, 0.64], 0.5, "metal", { metallic: 0.2 });
  A.mat("grille", [0.28, 0.28, 0.28], 0.5, "metal", { texture: "steel_scratched", metallic: 0.6 });
  A.mat("membrane", [0.12, 0.12, 0.12], 0.9, "concrete", { texture: "asphalt_dark" });
  A.mat("gravel_roof", [0.42, 0.41, 0.39], 0.95, "stone", { texture: "gravel_fine" });
  A.mat("concrete_m", [0.55, 0.54, 0.52], 0.88, "concrete", { texture: "concrete_formwork" });
  A.mat("paving", [0.47, 0.46, 0.44], 0.9, "stone", { texture: "paving_worn" });
  A.mat("asphalt", [0.11, 0.11, 0.11], 0.92, "concrete", { texture: "asphalt_plain" });
  A.mat("deck", [0.40, 0.28, 0.17], 0.75, "wood", { texture: "planks_038", grain: 0 });
  A.mat("laminate", [0.55, 0.42, 0.30], 0.4, "wood", { texture: "floor_laminate_pale", grain: 0 });
  A.mat("glass_rail", [0.70, 0.80, 0.80], 0.05, "glass");
  A.mat("glass_dark", [0.04, 0.05, 0.06], 0.1, "glass");
  A.mat("tile_white", [0.86, 0.86, 0.84], 0.3, "tile", { texture: "tiles_white_grid" });
  A.mat("vinyl_red", [0.42, 0.05, 0.04], 0.5, "fabric", { texture: "leather_brown" });
  A.mat("fabric_awning", [0.10, 0.20, 0.16], 0.9, "fabric", { texture: "cloth_grey" });
  A.mat("rubber", [0.04, 0.04, 0.04], 0.9, "plain");
  A.mat("brush", [0.10, 0.22, 0.55], 0.95, "fabric");
  A.mat("roof_m", [0.16, 0.17, 0.18], 0.7, "slate", { texture: "slate_square", grain: 2 });
  A.mat("sign_a", [0.62, 0.07, 0.04], 0.5, "plain");   // signage: blank coloured panels
  A.mat("sign_b", [0.85, 0.60, 0.06], 0.5, "plain");
  A.mat("sign_c", [0.05, 0.17, 0.45], 0.5, "plain");
  A.mat("sign_d", [0.07, 0.36, 0.16], 0.5, "plain");
  A.mat("sign_w", [0.85, 0.85, 0.82], 0.5, "plain");
  A.mat("light_panel", [0.90, 0.90, 0.86], 0.3, "plain", { emissive: [2.4, 2.35, 2.2] });
  A.mat("screen", [0.05, 0.08, 0.10], 0.2, "glass", { emissive: [0.25, 0.35, 0.45] });
  A.mat("line_yellow", [0.80, 0.62, 0.05], 0.6, "plain");
  A.mat("ceiling", [0.82, 0.81, 0.78], 0.9, "plaster", { texture: "plaster_painted" });   // AS1: ceilings under upper floors
  A.mat("door_frame", [0.80, 0.80, 0.78], 0.45, "metal", { metallic: 0.15 });   // AS1: light painted-steel door frames
  tdMats(A);   // TD1: facade kit (the outer asset of a placeSub build needs them too)
  variedWalls(A);   // TX3: the user's plaster / brick / concrete sets by building (tools/wall_variety.mjs)
}
function mmeta(id, name, category, desc, floors, footprint) {
  const A = meta(id, name, category, desc, floors, footprint, { version: 1, plinthDepth: 0, noAutoPanels: true, maxGrade: 0.5 });
  modMats(A);
  return A;
}
const sub = (id) => { const B = new Asset({ id, version: 1, name: id, category: "Modern", floors: 1, enterable: true, footprint: [1, 1] }); modMats(B); return B; };
const modStyle = (m, extra = {}) => () => ({ m, shutters: false, ...extra });

// ============================================================================ sub-building placement (copy of catalog_gx_buildings.mjs placeSub)
const QY = (deg) => { const t = (deg * Math.PI) / 360; return [0, Math.sin(t), 0, Math.cos(t)]; };
function qmul(a, b) {
  return [a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
    a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]];
}
function qEuler([rx, ry, rz]) {
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
        const pts = tri.map(([u, v]) => a.map((aa) => (p.axis === 0 ? [aa, v, u] : [u, v, aa])).map(P3));
        const nAxis = swap ? 2 - p.axis : p.axis, along = nAxis === 0 ? 0 : 2, across = 2 - along, av = pts.flat().map((q) => q[along]);
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
const tourFrom = (A, steps, extra = []) => setTour(A, { pos: [steps[0].goto[0], 0, steps[0].goto[1]], yaw: 0 }, [...steps.slice(1), ...extra]);

// ============================================================================ shared fixes on a house() build
// the house kit hangs oak beams under the top ceiling: a modern flat ceiling has none
function noBeams(A, EAVE) {
  const keep = A.parts.filter((p) => !(p.m === "oak_z" && p.k === 0 && Math.abs(p.c[1] - (EAVE - 0.28)) < 0.01 && p.s[0] < 0.15));
  A.parts.length = 0; A.parts.push(...keep);
}
// a modern interior has skirting but no dado rail: drop interiorTrim's rail pieces (short trims centred ~0.95 m up)
function noRail(A, k) {
  const ys = k.levels.map((l) => l.y), rail = (p) => p.m?.startsWith("trim_") && p.s[1] < 0.2 && ys.some((y) => Math.abs(p.c[1] - y - 0.95) < 0.08);
  const strip = (L) => { const keep = L.filter((p) => !rail(p)); L.length = 0; L.push(...keep); };
  strip(A.parts); for (const pn of A.panels) pn.states.forEach(strip);
}
// glazed aluminium entrance door: the leaf slab turns to glass, its rails to aluminium, its ironmongery to steel
function glazeDoor(A, id) {
  const it = A.interactables.find((i) => i.id === id);
  for (const L of it.leaves) L.parts.forEach((p, i) => { if (p.v === 0) return; p.m = i === 0 ? "glass" : p.m === "iron" ? "stainless" : "alu"; });
}
// Split a single-storey nav room at x with a partition built here (dress time, so interiorTrim frames it): a doorway
// at the corridor line zD like house()'s own partitions, plus `holes` (a counter hatch, a ticket window).
function splitRoom(A, k, x, keep, add, holes = []) {
  const { ZI, EAVE } = k, top = EAVE - 0.2, zD = -ZI + 1.1, nav = A.meta.nav;
  partition(A, { axis: "z", d0: x - 0.075, d1: x + 0.075, a0: -ZI, a1: ZI, y0: GF, y1: top, holes: [{ a0: zD - 0.5, a1: zD + 0.5, y0: GF - 1, y1: GF + 2.05 }, ...holes] });
  A.box([x - 0.09, GF + 2.05, zD - 0.6], [x + 0.09, GF + 2.2, zD + 0.6], "oak_z");
  const r = nav.rooms.find((q) => q.id === keep), nr = room(add, [x + 0.075, r.min[1], r.min[2]], r.max);
  r.max = [q4(x - 0.075), r.max[1], r.max[2]];
  nav.rooms.push(nr);
  nav.corners = nav.corners.filter((c) => c.room !== keep).concat(corners(keep, r.min[0], r.max[0], r.min[2], r.max[2], GF), corners(add, nr.min[0], nr.max[0], nr.min[2], nr.max[2], GF));
  for (const d of nav.doors) if (d.p[0] > x) { if (d.a === keep) d.a = add; if (d.b === keep) d.b = add; }
  for (const e of nav.entrances) if (e.room === keep && e.p[0] > x) e.room = add;
  nav.doors.push({ a: keep, b: add, p: [q4(x), GF, q4(zD)] });
}
// an outside door of house() that leads into an annex instead: the nav door goes to `into`, its entrance goes
function annexDoor(A, x, z, into) {
  const nav = A.meta.nav, d = nav.doors.find((q) => q.b === "outside" && Math.abs(q.p[0] - x) < 0.3 && Math.abs(q.p[2] - z) < 0.3);
  d.b = into;
  nav.entrances = nav.entrances.filter((e) => !(Math.abs(e.p[0] - x) < 1.2 && Math.abs(e.p[2] - z) < 1.2 && e.room === d.a));
}
function roomTypes(A, rules) {
  const t = {};
  for (const r of A.meta.nav.rooms) {
    const hit = rules.find(([re]) => re.test(r.id));
    if (!hit) throw new Error(`${A.meta.id}: no room type for nav room ${r.id}`);
    t[r.id] = hit[1];
  }
  A.meta.roomTypes = t;
}
// TD1 town pass on a house() build under a flatRoof (parapet 0.25 thick, coping underside yt)
const hTown = (A, k, yt, o = {}) => tdTown(A, { x0: -k.X, x1: k.X, z0: -k.Z, z1: k.Z }, { yW: k.EAVE, yd: k.EAVE + 0.16, yt, t: 0.25, ...o });
const modHouse = (A, c) => { const k = house(A, c); noRail(A, k); ceilings(A); tdMerge(A); as1Run(A); return k; };
const HALLS = [/(^|_)c\d$/, "hall"];

// ============================================================================ AS1 detail pass
// A plastered ceiling under every upper floor (house() leaves the oak boards of the floor above showing from below), and
// roof plant queued by flatRoof and added once the building is laid out, only where nothing stands yet (isFree), never in a
// door or window aperture, while the asset stays under the part budget (A.lean: none at all).
const AS1_MAX = 1480;
function as1(A, fn) { (A._as1 ??= []).push(fn); }
function as1Run(A) { for (const fn of A._as1?.splice(0) ?? []) if (!A.lean && A.parts.length < AS1_MAX) fn(); }
const hits = (c, he, mn, mx) => [0, 1, 2].every((i) => c[i] - he[i] < mx[i] && c[i] + he[i] > mn[i]);
const apHit = (A, mn, mx) => A.interactables.some((it) => hits(it.aperture.min.map((v, i) => (v + it.aperture.max[i]) / 2), it.aperture.min.map((v, i) => (it.aperture.max[i] - v) / 2), mn, mx));
function isFree(A, mn, mx, ignore = []) {
  for (const p of A.parts) {
    if (!p.s || ignore.includes(p.m)) continue;   // prisms carry no box
    let he = p.s.map((v) => v / 2);
    if (p.r || p.q) { const e = Math.hypot(...he); he = [e, e, e]; }
    if (hits(p.c, he, mn, mx)) return false;
  }
  return !apHit(A, mn, mx);
}
// roof plant in the first free spot of x0..x1, z0..z1 (from the rear) on the roof at y: a mushroom extract fan or a dish
function roofKit(A, x0, x1, z0, z1, y, kit) {
  const r = kit === "dish" ? 0.6 : 0.55;
  let p = null;
  for (let z = z0 + r; !p && z <= z1 - r; z += 0.6) for (let x = x0 + r; !p && x <= x1 - r; x += 0.6) if (isFree(A, [x - r, y + 0.01, z - r], [x + r, y + 1.7, z + r], ["gravel_roof", "membrane"])) p = [x, z];
  if (!p) return;
  const [x, z] = p;
  if (kit === "dish") {
    A.box([x - 0.45, y, z - 0.45], [x + 0.45, y + 0.08, z + 0.45], "concrete_m", L1);       // ballast frame
    A.cyl([x, y + 0.6, z], [0.06, 1.04, 0.06], "alu", { seg: 8, ...L1 });                    // mast
    A.cyl([x, y + 1.1, z + 0.1], [0.75, 0.04, 0.75], "sign_w", { r: [70, 0, 0], seg: 16, ...L1 });
    A.obox([x, y + 1.15, z + 0.38], [0.025, 0.025, 0.5], [-20, 0, 0], "alu", L1);           // feed arm
    A.box([x - 0.04, y + 1.2, z + 0.58], [x + 0.04, y + 1.28, z + 0.68], "metal_panel", L1);
  } else {
    A.box([x - 0.4, y, z - 0.4], [x + 0.4, y + 0.3, z + 0.4], "hvac", { ...L1, bv: 0.015 });   // kerb upstand
    A.cyl([x, y + 0.5, z], [0.56, 0.4, 0.56], "grille", { seg: 14, ...L1 });
    A.cone([x, y + 0.82, z], [0.8, 0.24, 0.8], "hvac", { seg: 14, top: 0.15, ...L1 });
  }
}
function ceilings(A) {
  const add = [];
  for (const p of A.parts) if (p.m === "floor_x" && p.t === "b" && !p.r && Math.abs(p.s[1] - 0.2) < 1e-3 && p.c[1] > 1) {
    const yb = p.c[1] - 0.1, i = 0.001;   // 1 mm inside the floor's edges, 5 mm up into it: no shared faces
    add.push([[p.c[0] - p.s[0] / 2 + i, yb - 0.015, p.c[2] - p.s[2] / 2 + i], [p.c[0] + p.s[0] / 2 - i, yb + 0.005, p.c[2] + p.s[2] / 2 - i]]);
  }
  for (const [mn, mx] of add) A.box(mn, mx, "ceiling", { k: 0 });
}

// ============================================================================ facade and roof detail
// flat roof on walls topping at y over x0..x1, z0..z1: membrane deck, gravel ballast, parapet with aluminium coping,
// outlets and soil vents. Returns the parapet top.
function flatRoof(A, x0, x1, z0, z1, y, { ph = 0.9, t = 0.25, m = "render_m", vents = [], kit = "fan" } = {}) {
  if (kit) as1(A, () => roofKit(A, x0 + t + 0.1, x1 - t - 0.1, z0 + t + 0.1, z1 - t - 0.1, y + 0.16, kit));   // AS1
  A.box([x0 + 0.005, y, z0 + 0.005], [x1 - 0.005, y + 0.12, z1 - 0.005], "membrane");   // TD1: 5 mm inside the parapet faces (was coplanar)
  A.box([x0 + t, y + 0.12, z0 + t], [x1 - t, y + 0.16, z1 - t], "gravel_roof");
  const yt = y + ph;
  A.box([x0, y, z1 - t], [x1, yt, z1], m, BV); A.box([x0, y, z0], [x1, yt, z0 + t], m, BV);
  A.box([x0, y, z0 + t], [x0 + t, yt, z1 - t], m, BV); A.box([x1 - t, y, z0 + t], [x1, yt, z1 - t], m, BV);
  const c = { k: 0, bv: 0.01 };
  A.box([x0 - 0.04, yt, z1 - t - 0.03], [x1 + 0.04, yt + 0.05, z1 + 0.04], "alu", c); A.box([x0 - 0.04, yt, z0 - 0.04], [x1 + 0.04, yt + 0.05, z0 + t + 0.03], "alu", c);
  A.box([x0 - 0.04, yt, z0 + t + 0.03], [x0 + t + 0.03, yt + 0.05, z1 - t - 0.03], "alu", c); A.box([x1 - t - 0.03, yt, z0 + t + 0.03], [x1 + 0.04, yt + 0.05, z1 - t - 0.03], "alu", c);
  A.box([x0 + t, y + 0.12, z0 + t], [x1 - t, y + 0.3, z0 + t + 0.03], "membrane", L1);   // upstand flashings
  A.box([x0 + t, y + 0.12, z1 - t - 0.03], [x1 - t, y + 0.3, z1 - t], "membrane", L1);
  for (const [x, z] of [[x0 + t + 0.25, z0 + t + 0.25], [x1 - t - 0.25, z0 + t + 0.25]]) A.cyl([x, y + 0.17, z], [0.22, 0.03, 0.22], "grille", { seg: 10, ...L1 });
  for (const [x, z] of vents) { A.cyl([x, y + 0.45, z], [0.11, 0.6, 0.11], "metal_panel", { seg: 10, k: 0 }); A.cyl([x, y + 0.77, z], [0.16, 0.06, 0.16], "metal_panel", { seg: 10, ...L1 }); }
  return yt;
}
// packaged rooftop air-conditioning unit on rails: casing, louvred grilles, two fan discs, a supply duct into the roof
function hvac(A, x, z, y, w = 1.8, d = 1.2, h = 1.1) {
  for (const s of [-1, 1]) A.box([x - w / 2 - 0.05, y, z + s * d * 0.35 - 0.05], [x + w / 2 + 0.05, y + 0.15, z + s * d * 0.35 + 0.05], "steel_plate", L1);
  A.box([x - w / 2, y + 0.15, z - d / 2], [x + w / 2, y + 0.15 + h, z + d / 2], "hvac", { bv: 0.02 });
  for (const s of [-1, 1]) A.box([x - w / 2 + 0.1, y + 0.3, z + s * (d / 2 + 0.005) - 0.01], [x + w / 2 - 0.1, y + h - 0.05, z + s * (d / 2 + 0.005) + 0.01], "grille", L1);
  for (const s of [-1, 1]) { A.cyl([x + s * w * 0.25, y + 0.15 + h + 0.03, z], [d * 0.6, 0.06, d * 0.6], "grille", { seg: 16, ...L1 }); A.cyl([x + s * w * 0.25, y + 0.15 + h + 0.01, z], [0.12, 0.08, 0.12], "steel_plate", { seg: 8, ...L1 }); }
  A.box([x - 0.3, y - 0.05, z + d / 2], [x + 0.3, y + 0.55, z + d / 2 + 0.45], "alu", { k: 0, bv: 0.01 });
  A.box([x + w / 2 - 0.25, y + 0.5, z + d / 2], [x + w / 2 - 0.05, y + 0.75, z + d / 2 + 0.03], "steel_plate", L1);   // isolator
}
// blank coloured sign panel on a wall face (aluminium tray, coloured face, top and bottom trims)
function signPanel(A, F, a0, a1, y0, y1, m, { e = 0, depth = 0.12, frame = "alu" } = {}) {
  fbox(A, F, a0 - 0.04, a1 + 0.04, y0 - 0.04, y1 + 0.04, e, e + 0.05, frame, { k: 0, bv: 0.01 });
  fbox(A, F, a0, a1, y0, y1, e + 0.05, e + depth, m, { k: 0, bv: 0.008 });
  fbox(A, F, a0 + 0.06, a1 - 0.06, y0 + 0.04, y0 + 0.07, e + depth, e + depth + 0.008, "sign_w", L1);
}
// flat steel canopy on a wall face: slab, aluminium fascia, LED soffit strip, two tie rods back to the wall
function canopy(A, F, a0, a1, y, depth = 1.2, m = "metal_panel") {
  fbox(A, F, a0, a1, y, y + 0.16, 0, depth, m, { bv: 0.015 });
  fbox(A, F, a0 - 0.02, a1 + 0.02, y - 0.04, y + 0.2, depth - 0.04, depth + 0.02, "alu", { k: 0, bv: 0.008 });
  fbox(A, F, a0 + 0.2, a1 - 0.2, y - 0.012, y, depth * 0.4, depth * 0.6, "light_panel", L1);
  const P = (a, yy, e) => (F.axis === "x" ? [a, yy, F.d + F.out * e] : [F.d + F.out * e, yy, a]);
  for (const a of [a0 + 0.2, a1 - 0.2]) rod(A, P(a, y + 1.0, 0.02), P(a, y + 0.16, depth - 0.1), 0.03, "stainless", L1);
}
// fabric awning on a wall face (sloped cloth, steel frame front bar)
function awningM(A, F, a0, a1, y, depth = 1.0, drop = 0.45) {
  const L = Math.hypot(depth, drop), ang = (Math.atan2(drop, depth) * 180) / Math.PI, ac = (a0 + a1) / 2;
  if (F.axis === "x") A.obox([ac, y - drop / 2, F.d + F.out * depth / 2], [a1 - a0, 0.025, L], [F.out * ang, 0, 0], "fabric_awning", { k: 0 });
  else A.obox([F.d + F.out * depth / 2, y - drop / 2, ac], [L, 0.025, a1 - a0], [0, 0, -F.out * ang], "fabric_awning", { k: 0 });
  fbox(A, F, a0, a1, y - drop - 0.25, y - drop, depth - 0.01, depth + 0.01, "fabric_awning", L1);
  fbox(A, F, a0 - 0.03, a1 + 0.03, y - 0.06, y + 0.06, -0.01, 0.12, "metal_panel", { k: 0, bv: 0.01 });
}
// glass-balustraded concrete balcony (depth out of face F) at floor level y
function glassBalcony(A, F, a0, a1, y, depth = 1.3) {
  fbox(A, F, a0, a1, y - 0.2, y, -0.02, depth, "concrete_m", { bv: 0.02 });
  fbox(A, F, a0 + 0.04, a1 - 0.04, y - 0.23, y - 0.2, depth - 0.08, depth - 0.05, "concrete_m", L1);   // drip
  fbox(A, F, a0 + 0.05, a1 - 0.05, y + 0.06, y + 1.02, depth - 0.07, depth - 0.05, "glass_rail", { k: 0 });
  fbox(A, F, a0, a1, y + 1.0, y + 1.06, depth - 0.09, depth - 0.03, "alu", { k: 0, bv: 0.008 });
  fbox(A, F, a0, a1, y, y + 1.06, depth - 0.09, depth - 0.03, "alu", { v: 0 });
  for (const a of [a0, a1 - 0.02]) {
    fbox(A, F, a, a + 0.02, y + 0.06, y + 1.0, 0.03, depth - 0.09, "glass_rail", { k: 0 });
    fbox(A, F, a, a + 0.02, y, y + 1.06, 0, depth - 0.09, "alu", { v: 0 });
  }
  for (const a of [a0 + 0.25, a1 - 0.25]) fbox(A, F, a - 0.03, a + 0.03, y + 0.02, y + 0.08, depth - 0.1, depth - 0.02, "stainless", L1);   // clamps
}
// rainwater downpipe on a wall face
function dpipe(A, F, a, y0, y1) {
  fpart(A, "cyl", F, a, (y0 + y1) / 2, 0.08, [0.09, y1 - y0, 0.09], "metal_panel", { k: 0 });
  for (let y = y0 + 0.8; y < y1 - 0.3; y += 1.8) fbox(A, F, a - 0.06, a + 0.06, y - 0.02, y + 0.02, 0, 0.13, "metal_panel", L1);
  fpart(A, "wedge", F, a, y0 + 0.05, 0.16, [0.09, 0.1, 0.16], "metal_panel", L1);
  fbox(A, F, a - 0.12, a + 0.12, y1 - 0.24, y1 + 0.02, 0.02, 0.2, "metal_panel", { ...L1, bv: 0.01 });   // AS1: hopper head
}
// LED panel lights on a ceiling at y over a rectangle
function ceilLights(A, x0, x1, z0, z1, y, sp = 2.4) {
  for (let x = x0 + sp / 2; x < x1 - 0.3; x += sp) for (let z = z0 + sp / 2; z < z1 - 0.3; z += sp) A.box([x - 0.3, y - 0.025, z - 0.3], [x + 0.3, y, z + 0.3], "light_panel", L1);
}
// wall tiles (a thin skin on the interior face at x = d or z = d, toward s) up to y + h
function tiles(A, axis, d, s, a0, a1, y, h = 1.8) { wbox(A, axis, a0, a1, y + 0.15, y + h, d, d + s * 0.012, "tile_white", L1); }

// ============================================================================ fixed fittings
// counter: body, recessed plinth, overhanging top
function counter(A, x0, x1, z0, z1, y, { h = 1.0, top = "laminate", body = "metal_panel" } = {}) {
  A.box([x0 + 0.05, y, z0 + 0.05], [x1 - 0.05, y + 0.1, z1 - 0.05], "rubber", L1);
  A.box([x0, y + 0.1, z0], [x1, y + h - 0.04, z1], body, { bv: 0.01 });
  A.box([x0 - 0.04, y + h - 0.04, z0 - 0.04], [x1 + 0.04, y + h, z1 + 0.04], top, { bv: 0.008 });
}
// till: base, cash drawer, tilted screen (facing +x / -x / +z / -z: f = [fx, fz])
function register(A, x, z, y, [fx, fz]) {
  A.box([x - 0.2, y, z - 0.2], [x + 0.2, y + 0.1, z + 0.2], "metal_panel", L1);
  const yaw = Math.atan2(fx, fz) * 180 / Math.PI;
  A.obox([x - fx * 0.05, y + 0.28, z - fz * 0.05], [0.36, 0.26, 0.03], [-15, yaw, 0], "screen", L1);
  A.cyl([x - fx * 0.08, y + 0.15, z - fz * 0.08], [0.05, 0.12, 0.05], "metal_panel", { n: 8, ...L1 });
}
// bar stool fixed to the floor
function stool(A, x, z, y) {
  A.cyl([x, y + 0.36, z], [0.07, 0.72, 0.07], "stainless", { n: 8 });
  A.cyl([x, y + 0.75, z], [0.38, 0.07, 0.38], "vinyl_red", { seg: 14, k: 0 });
  A.cyl([x, y + 0.015, z], [0.4, 0.03, 0.4], "stainless", { seg: 14, ...L1 });
  A.cyl([x, y + 0.3, z], [0.32, 0.025, 0.32], "stainless", { seg: 12, ...L1 });   // foot ring
}
// fixed booth: two upholstered benches facing each other across a table along `sep` ("x" or "z"); w the width
function booth(A, x, z, y, sep = "x", w = 1.3) {
  const bx = (u0, u1, y0, y1, v0, v1, m, o) => (sep === "x" ? A.box([x + u0, y + y0, z + v0], [x + u1, y + y1, z + v1], m, o) : A.box([x + v0, y + y0, z + u0], [x + v1, y + y1, z + u1], m, o));
  bx(-0.38, 0.38, 0.72, 0.76, -w / 2 + 0.05, w / 2 - 0.05, "laminate", { bv: 0.01 });
  bx(-0.06, 0.06, 0, 0.72, -0.06, 0.06, "stainless");
  bx(-0.25, 0.25, 0, 0.02, -0.25, 0.25, "stainless", L1);
  for (const s of [-1, 1]) {
    const u = (a, b) => (s > 0 ? [a, b] : [-b, -a]);
    bx(...u(0.45, 1.0), 0, 0.38, -w / 2, w / 2, "metal_panel");
    bx(...u(0.45, 0.98), 0.38, 0.47, -w / 2 + 0.02, w / 2 - 0.02, "vinyl_red", { k: 0, bv: 0.03 });
    bx(...u(0.88, 1.05), 0.38, 1.2, -w / 2, w / 2, "vinyl_red", { bv: 0.03 });
    bx(...u(0.86, 0.9), 0.55, 1.12, -w / 2 + 0.06, w / 2 - 0.06, "vinyl_red", L1);   // tufting seam
  }
}
// commercial kitchen line along x from x0 against a wall at z = zw, standing toward s (+1 / -1 along z)
const zr = (zw, s, e0, e1) => { const a = zw + s * e0, b = zw + s * e1; return [Math.min(a, b), Math.max(a, b)]; };
function kitLine(A, x0, zw, s, y, items) {
  let x = x0;
  for (const [kind, w] of items) {
    const xa = x + 0.01, xb = x + w - 0.01, [za, zb] = zr(zw, s, 0.02, 0.82), [fa, fb] = zr(zw, s, 0.82, 0.84);
    const hh = kind === "hold" ? 1.5 : 0.88;
    A.box([xa, y + 0.1, za], [xb, y + hh, zb], "stainless", { bv: 0.01 });
    A.box([xa + 0.03, y, za + 0.03], [xb - 0.03, y + 0.1, zb - 0.03], "steel_plate", L1);
    A.box([xa + 0.04, y + 0.18, fa], [xb - 0.04, y + 0.66, fb], "steel_bare", L1);              // door panel
    A.box([xa + 0.04, y + 0.72, fa], [xb - 0.04, y + 0.8, fb], "rubber", L1);                   // control strip
    for (let k = xa + 0.1; k < xb - 0.05; k += 0.16) A.box([k - 0.02, y + 0.74, fb - 0.002], [k + 0.02, y + 0.78, fb + 0.02], "stainless", L1);   // knobs
    const [ta, tb] = zr(zw, s, 0.1, 0.72);
    if (kind === "fryer") {
      A.box([xa + 0.06, y + 0.8, ta], [xb - 0.06, y + 0.885, tb], "soot", { k: 0 });   // oil well
      for (const f of [0.3, 0.7]) { const bx0 = xa + (xb - xa) * f; A.box([bx0 - 0.09, y + 0.86, ta + 0.05], [bx0 + 0.09, y + 0.95, tb - 0.1], "grille", L1); const [ha, hb] = zr(zw, s, 0.72, 0.9); A.box([bx0 - 0.015, y + 0.94, ha], [bx0 + 0.015, y + 0.97, hb], "rubber", L1); }
    } else if (kind === "griddle") {
      A.box([xa + 0.03, y + 0.88, ta - 0.05], [xb - 0.03, y + 0.92, tb + 0.05], "steel_plate", { k: 0 });
      const [ba, bb] = zr(zw, s, 0.02, 0.08); A.box([xa, y + 0.88, ba], [xb, y + 1.2, bb], "stainless", L1);
      const [ga, gb] = zr(zw, s, 0.74, 0.8); A.box([xa + 0.05, y + 0.85, ga], [xb - 0.05, y + 0.88, gb], "soot", L1);   // grease trough
    } else if (kind === "range") {
      for (const f of [0.28, 0.72]) for (const g of [0.3, 0.68]) { const [c0, c1] = zr(zw, s, g * 0.8 - 0.12, g * 0.8 + 0.12); A.cyl([xa + (xb - xa) * f, y + 0.9, (c0 + c1) / 2], [0.24, 0.04, 0.24], "cast_iron", { seg: 10, ...L1 }); }
    } else if (kind === "sink") {
      A.box([xa + 0.08, y + 0.7, ta], [xb - 0.08, y + 0.885, tb], "soot", { k: 0 });
      const [pa, pb] = zr(zw, s, 0.04, 0.06); const xm = (xa + xb) / 2;
      rod(A, [xm, y + 0.88, (pa + pb) / 2], [xm, y + 1.3, (pa + pb) / 2], 0.03, "stainless", L1);
    } else if (kind === "hold") {
      for (let yy = y + 0.95; yy < y + 1.45; yy += 0.17) A.box([xa + 0.05, yy, fa], [xb - 0.05, yy + 0.1, fb], "glass_dark", L1);
    }
    x += w;
  }
  return x;
}
// extraction hood over a kitchen line (x0..x1 against the wall at zw, toward s), its duct up to yTop and a roof fan
function hood(A, x0, x1, zw, s, y, ductX, yTop) {
  const [za, zb] = zr(zw, s, 0, 1.15), [la, lb] = zr(zw, s, 1.08, 1.15), [fa, fb] = zr(zw, s, 0.12, 0.7);
  A.box([x0, y + 2.05, za], [x1, y + 2.65, zb], "stainless", { bv: 0.02 });
  A.box([x0, y + 1.95, la], [x1, y + 2.05, lb], "stainless", L1);                          // front gutter lip
  for (let x = x0 + 0.1; x < x1 - 0.35; x += 0.5) A.box([x, y + 2.0, fa], [x + 0.46, y + 2.05, fb], "grille", L1);   // baffle filters
  A.box([x0 + 0.1, y + 1.99, za + 0.02], [x1 - 0.1, y + 2.0, zb - 0.6], "light_panel", L1);
  const [da, db] = zr(zw, s, 0.15, 0.65), dz = (da + db) / 2;
  A.box([ductX - 0.25, y + 2.65, da], [ductX + 0.25, yTop, db], "stainless", { bv: 0.01 });
  for (let yy = y + 3.0; yy < yTop - 0.2; yy += 1.0) A.box([ductX - 0.28, yy, da - 0.03], [ductX + 0.28, yy + 0.05, db + 0.03], "steel_bare", L1);   // flanges
  A.box([ductX - 0.45, yTop, dz - 0.45], [ductX + 0.45, yTop + 0.3, dz + 0.45], "alu", { bv: 0.02 });
  A.cyl([ductX, yTop + 0.55, dz], [0.85, 0.5, 0.85], "alu", { seg: 18 });
  A.cone([ductX, yTop + 0.95, dz], [1.0, 0.3, 1.0], "alu", { seg: 18, top: 0.2, k: 0 });
}
// restroom: cubicle panels against the wall at zw (toward s) over x0..x1, n cubicles, WC pans, and a vanity
function restroom(A, x0, x1, zw, s, y, n = 2) {
  const w = (x1 - x0) / n, [za, zb] = zr(zw, s, 0, 1.5), [pa, pb] = zr(zw, s, 0.1, 0.6);
  for (let i = 0; i <= n; i++) if (i > 0 && i < n) A.box([x0 + i * w - 0.015, y + 0.15, za], [x0 + i * w + 0.015, y + 2.0, zb], "metal_panel");
  for (let i = 0; i < n; i++) {
    const xc = x0 + (i + 0.5) * w;
    const [fa, fb] = zr(zw, s, 1.48, 1.51);
    A.box([x0 + i * w + 0.03, y + 0.15, fa], [xc - 0.33, y + 2.0, fb], "metal_panel");
    A.box([xc + 0.33, y + 0.15, fa], [x0 + (i + 1) * w - 0.03, y + 2.0, fb], "metal_panel");
    A.box([x0 + i * w, y + 2.0, fa - 0.02], [x0 + (i + 1) * w, y + 2.05, fb + 0.02], "stainless", L1);   // head rail
    A.cyl([xc, y + 0.2, (pa + pb) / 2], [0.38, 0.4, 0.5], "faience", { seg: 12, k: 0 });
    A.box([xc - 0.2, y + 0.4, pa - 0.05], [xc + 0.2, y + 0.43, pb + 0.05], "faience", L1);
  }
}
// fixed metal bench (perforated seat and back) along x at z, facing f (+1 / -1 along z)
function bench(A, x0, x1, z, y, f) {
  const zs = (e0, e1) => [Math.min(z + f * e0, z + f * e1), Math.max(z + f * e0, z + f * e1)];
  const [sa, sb] = zs(-0.22, 0.22), [ba, bb] = zs(-0.26, -0.22);
  A.box([x0, y + 0.42, sa], [x1, y + 0.46, sb], "grille", { bv: 0.01 });
  A.obox([(x0 + x1) / 2, y + 0.72, (ba + bb) / 2 - f * 0.03], [x1 - x0, 0.5, 0.03], [f * -10, 0, 0], "grille", { k: 0 });
  for (const x of [x0 + 0.12, (x0 + x1) / 2, x1 - 0.12]) {
    A.box([x - 0.03, y, sa + 0.05], [x + 0.03, y + 0.42, sb - 0.05], "metal_panel");
    A.box([x - 0.025, y + 0.46, ba - 0.03], [x + 0.025, y + 1.0, bb], "metal_panel", L1);
    A.box([x - 0.06, y, sa + 0.02], [x + 0.06, y + 0.01, sb - 0.02], "steel_bare", L1);   // base plate
  }
}

// ============================================================================ 1a. flat-roof family house with garage and terrace
export function familyHouse() {
  const X = 6.5, Z = 5.0, hx = -1.75, hz = 1.75;
  const A = mmeta("mod_bld_family_house", "Modern family house", "Modern",
    "Flat-roofed two-storey family house, 13 x 10 m plus an attached single garage and a rear timber terrace (16.5 x 13.5 m overall): brick-slip ground floor, white render and a vertical timber-clad stair bay above, large aluminium windows with precast sills, a steel entrance canopy, a parapet roof with aluminium coping. Enterable: sitting room, entrance hall and kitchen below, two bedrooms and a bathroom above, two stairs off the landing corridor; the garage opens from the kitchen.",
    2, [17.5, 15.0]);
  const B = sub("fam");
  const c = {
    X, Z, T: 0.35, h: 3.0, n: 2, corridor: 1.9, plinthDepth: 0.5, shot: "mod_fam",
    style: (f) => (f === 0 ? { m: "brick_slip", shutters: false } : { m: "render_m", shutters: false, split: [{ a0: -X, a1: -1.0, m: "render_m" }, { a0: -1.0, a1: 1.0, m: "clad_wood" }, { a0: 1.0, a1: X, m: "render_m" }] }),
    wd: () => ({ w: 1.4, h: 1.5, sill: 0.8 }),
    win: (f) => (f === 0 ? { front: [{ c: -2.3, w: 2.2, h: 1.9, sill: 0.5 }, { c: 2.3, w: 1.6, h: 1.2, sill: 1.0 }, { c: -5.2, w: 1.0 }], rear: [{ c: -2.3, w: 2.2, h: 2.1, sill: 0.15 }, { c: 2.3, w: 1.4, h: 1.2, sill: 1.0 }], east: [{ c: -3.4, w: 0.8 }], west: [{ c: 1.5, w: 1.2 }] }
      : { front: [{ c: -2.2, w: 1.8 }, { c: 0, w: 0.6, h: 2.0, sill: 0.5 }, { c: 2.2, w: 1.8 }, { c: -5.2, w: 1.0 }], rear: [{ c: -2.2, w: 1.4 }, { c: 2.2, w: 0.8, h: 0.8, sill: 1.4 }], east: [{ c: 2.5, w: 1.0 }], west: [{ c: 2.5, w: 1.0 }] }),
    parts: (f) => (f === 0 ? [-1.0, 1.0] : [-0.6, 1.4]),
    names: (f, r) => (f === 0 ? ["sitting", "entry", "kitchen"] : ["bedroom_1", "bathroom", "bedroom_2"])[r], hall: 1, doorW: 1.1,
    doors: [{ id: "garage_link", side: "east", c: 2.5, w: 0.9, h: 2.1, room: "kitchen" }],
    roof: { type: "none", rise: 1 },
    dress: (B, k) => {
      const { EAVE, yAt } = k, fr = face("front", X, Z), re = face("rear", X, Z);
      noBeams(B, EAVE);
      flatRoof(B, -X, X, -Z, Z, EAVE, { vents: [[-3, -2], [2.5, -1.5]], kit: "dish" });
      hvac(B, 3.6, -3.4, EAVE + 0.16, 1.2, 0.8, 0.8);
      canopy(B, fr, -1.0, 1.0, GF + 2.55, 1.3);
      signPanel(B, fr, 0.75, 0.95, GF + 1.6, GF + 1.8, "sign_w", { depth: 0.04 });   // house number plate (blank)
      for (const s of ["front", "rear"]) { const F = face(s, X, Z); fbox(B, F, -X - 0.02, X + 0.02, yAt(1) - 0.3, yAt(1) - 0.05, -0.01, 0.06, "concrete_m", { bv: 0.01 }); }   // floor band
      for (const sx of [-1, 1]) dpipe(B, re, sx * (X - 0.2), 0.05, EAVE + 0.4);
      fbox(B, re, -X + 0.5, -X + 1.3, GF + 1.0, GF + 1.6, 0, 0.25, "hvac", { k: 0, bv: 0.01 });   // heat pump on wall brackets
      fbox(B, re, -X + 0.55, -X + 1.25, GF + 1.05, GF + 1.55, 0.25, 0.26, "grille", L1);
      hTown(B, k, EAVE + 0.9, { m: "concrete_m", prof: [[0.1, 0.05], [0.12, 0.12]], piers: "render_m", pierCorners: ["fw", "rw"], roof: ["ac"] });   // TD1
    },
  };
  modHouse(B, c);
  annexDoor(B, X - 0.175, 2.5, "garage");
  const steps = placeSub(A, B, { dx: hx, dz: hz });
  // garage: concrete floor, brick walls, a sectional door, its own flat roof
  const gx0 = hx + X, gx1 = gx0 + 3.5, gz0 = hz - 1.5, gz1 = hz + Z, gt = 0.3, gh = 3.0, gxc = (gx0 + gx1) / 2;
  A.box([gx0, -0.3, gz0], [gx1, 0.05, gz1], "concrete_m");
  A.box([gx0, 0.05, gz0 + gt], [gx1 - gt, 0.15, gz1 - gt], "concrete_floor");
  A.box([gx1 - gt, 0.05, gz0], [gx1, gh, gz1], "brick_slip");
  A.box([gx0, 0.05, gz0], [gx1 - gt, gh, gz0 + gt], "brick_slip");
  A.box([gx0, 0.05, gz1 - gt], [gx0 + 0.3, gh, gz1], "brick_slip"); A.box([gx1 - gt - 0.05, 0.05, gz1 - gt], [gx1 - gt, gh, gz1], "brick_slip");
  A.box([gx0 + 0.3, 2.4, gz1 - gt], [gx1 - gt - 0.05, gh, gz1], "brick_slip");
  door(A, { id: "garage_door", axis: "x", outer: gz1, inner: gz1 - gt, c: (gx0 + 0.3 + gx1 - gt - 0.05) / 2, floor: 0.15, w: 2.85, h: 2.25, hingeSide: 1, double: true, leafMat: "metal_panel", frameMat: "door_frame", step: false, lintel: false, label: "garage door", sound: "steel" });
  A.box([gx0 + 0.3, gh - 0.2, gz0 + gt], [gx1 - gt, gh, gz1 - gt], "plaster");
  flatRoof(A, gx0, gx1, gz0, gz1, gh, { ph: 0.45, m: "brick_slip", kit: null });
  A.box([gx0 + 0.6, 2.4, gz1 + 0.01], [gx1 - 0.6, 2.47, gz1 + 0.08], "light_panel", L1);
  A.box([gx0 + 0.2, -0.02, gz1], [gx1 - 0.1, 0.06, gz1 + 3.0], "paving", { bv: 0.01 });   // drive
  // rear terrace: timber deck, steps, a glass balustrade and a steel pergola
  const tz0 = hz - Z - 3.5, tz1 = hz - Z, tx0 = hx - 5.8, tx1 = hx + 3.2;
  A.box([tx0, 0.0, tz0], [tx1, 0.22, tz1], "deck", { bv: 0.01 });
  for (let x = tx0 + 0.6; x < tx1 - 0.3; x += 0.9) A.box([x - 0.006, 0.22, tz0 + 0.02], [x + 0.006, 0.224, tz1 - 0.02], "rubber", L1);   // board joints
  A.box([tx0 + 2.5, -0.02, tz0 - 0.35], [tx0 + 4.5, 0.11, tz0], "deck", { bv: 0.01 });
  fbox(A, { axis: "z", d: tx0, out: -1 }, tz0, tz1, 0.22, 1.22, -0.06, -0.04, "glass_rail", { k: 0 });
  fbox(A, { axis: "z", d: tx0, out: -1 }, tz0, tz1, 0.22, 1.25, -0.08, -0.02, "alu", { v: 0 });
  A.box([tx0 + 0.02, 1.2, tz0], [tx0 + 0.1, 1.25, tz1], "alu", { k: 0, bv: 0.008 });
  for (const [x, z] of [[tx0 + 0.3, tz0 + 0.3], [tx0 + 4.0, tz0 + 0.3]]) A.box([x - 0.06, 0.22, z - 0.06], [x + 0.06, 2.75, z + 0.06], "metal_panel", BV);
  for (const z of [tz0 + 0.3]) A.box([tx0 + 0.2, 2.6, z - 0.06], [tx0 + 4.1, 2.8, z + 0.06], "metal_panel", BV);
  A.box([tx0 + 0.2, 2.6, tz1 - 0.12], [tx0 + 4.1, 2.8, tz1], "metal_panel", BV);
  for (let x = tx0 + 0.35; x < tx0 + 4.05; x += 0.3) A.box([x - 0.03, 2.8, tz0 + 0.2], [x + 0.03, 2.92, tz1], "clad_wood", L1);   // louvres
  for (const x of [tx1 - 1.0]) { A.box([x - 0.5, 0.22, tz0 + 0.2], [x + 0.5, 0.7, tz0 + 0.7], "concrete_m", { bv: 0.02 }); A.box([x - 0.45, 0.7, tz0 + 0.25], [x + 0.45, 0.74, tz0 + 0.65], "earth", L1); }
  A.box([hx - 0.8, -0.02, hz + Z], [hx + 0.8, 0.05, hz + Z + 2.4], "paving", { bv: 0.01 });   // front path
  const nav = A.meta.nav;
  nav.rooms.push(room("garage", [gx0 + 0.3, 0.15, gz0 + gt], [gx1 - gt, gh - 0.2, gz1 - gt]));
  nav.corners.push(...corners("garage", gx0 + 0.3, gx1 - gt, gz0 + gt, gz1 - gt, 0.15));
  nav.doors.push({ a: "garage", b: "outside", p: [q4(gxc), 0.15, q4(gz1 - gt / 2)] });
  nav.entrances.push({ p: [q4(gxc), 0, q4(gz1 + 1.2)], dir: [0, 0, -1], room: "garage" });
  nav.cover.push(cover([tx0 + 4.5, 0, tz0 + 0.5], 180, "low", ["over"]));
  roomTypes(A, [HALLS, [/sitting/, "sitting"], [/entry/, "hall"], [/kitchen/, "kitchen"], [/bedroom/, "bedroom"], [/bathroom/, "bathroom"], [/garage/, "utility"]]);
  tourFrom(A, steps, [{ goto: [gx1 + 1.5, hz - Z - 1.6] }, { goto: [gx1 + 1.5, gz1 + 2.5] }, { goto: [gxc, gz1 + 2.5] }, { interact: "garage_door", aim: [gxc, 1.2, gz1 - gt - 0.05], expectOpen: true }, { goto: [gxc, gz1 - 1.5], note: "into the garage" }]);
  return A;
}

// ============================================================================ 1b. three-storey townhouse row (3 units)
export function townhouseRow() {
  const X = 4.6, Z = 5.5, W = 2 * X;
  const A = mmeta("mod_bld_townhouse_row", "Modern townhouse row", "Modern",
    "Terrace of three three-storey townhouses, 27.6 x 11 m: each unit in its own finish (brick slip, white render with timber cladding, grey brick), floor-to-ceiling first-floor windows with glass Juliet balconies, a steel door canopy, a parapet roof. Enterable: every unit has a kitchen-diner, a first-floor living room and a top-floor bedroom off a landing corridor with two stairs.",
    3, [28.4, 13.5]);
  const units = [["u1_", -W, "brick_slip", "west"], ["u2_", 0, "render_m", null], ["u3_", W, "brick_grey", "east"]];
  let steps = [];
  units.forEach(([pre, dx, m, endSide], i) => {
    const B = sub("th" + i);
    B.lean = true;   // AS1: the row is already over the part budget: ceilings only
    const side = (o) => (endSide ? { [endSide]: o } : {});
    modHouse(B, {
      X, Z, T: 0.35, h: 3.0, n: 3, corridor: 1.9, plinthDepth: 0.5, shot: "mod_th" + i,
      style: (f) => ({ m: f === 2 && m === "render_m" ? "clad_wood" : m, shutters: false }), wd: () => ({ w: 1.2, h: 1.5, sill: 0.8 }),
      win: (f) => (f === 0 ? { front: [{ c: -3.0, w: 1.6, h: 1.9, sill: 0.4 }, { c: 2.9, w: 1.0 }], rear: [{ c: -2.6, w: 1.4, h: 2.0, sill: 0.2 }], ...side([{ c: 2.5, w: 0.8 }]) }
        : f === 1 ? { front: [{ c: 0, w: 2.4, h: 2.3, sill: 0.05 }], rear: [{ c: 0, w: 1.6 }], ...side([{ c: 2.5, w: 0.8 }]) }
          : { front: [{ c: -1.6, w: 1.6 }, { c: 1.6, w: 1.6 }], rear: [{ c: 0, w: 1.4 }], ...side([{ c: 2.5, w: 0.8 }]) }),
      parts: () => [], names: (f) => ["kitchen", "sitting", "bedroom"][f], doorW: 1.0,
      roof: { type: "none", rise: 1 },
      dress: (B, k) => {
        const { EAVE, yAt } = k, fr = face("front", X, Z);
        noBeams(B, EAVE);
        const ph = [0.8, 1.3, 1.05][i];   // TD1: three parapet heights along the row
        flatRoof(B, -X, X, -Z, Z, EAVE, { m, ph, vents: [[-1.5, -3.5]] });
        canopy(B, fr, -0.9, 0.9, GF + 2.45, 1.1);
        glassBalcony(B, fr, -1.25, 1.25, yAt(1), 0.25);
        for (const f of [1, 2]) fbox(B, fr, -X, X, yAt(f) - 0.3, yAt(f) - 0.08, -0.01, 0.05, "concrete_m", { bv: 0.01 });
        dpipe(B, fr, X - 0.2, 0.05, EAVE + 0.3);
        signPanel(B, fr, 0.65, 0.85, GF + 1.5, GF + 1.7, "sign_w", { depth: 0.04 });
        fbox(B, fr, -X + 0.3, -1.0, -0.02, 0.6, 1.4, 1.65, "concrete_m", { bv: 0.02 });   // garden wall with planter
        fbox(B, fr, -X + 0.3, -1.0, 0.6, 0.64, 1.42, 1.63, "earth", L1);
        fbox(B, fr, 1.0, 2.6, -0.02, 1.25, 0.9, 1.6, "clad_wood", { bv: 0.01 });           // bin store
        fbox(B, fr, 1.0, 2.6, 1.25, 1.3, 0.88, 1.62, "metal_panel", L1);
        hTown(B, k, EAVE + ph, { wall: m, sides: endSide ? ["front", endSide] : ["front"], crest: { w: [3.2, 2.2, 4.0][i], kind: ["step", "block", "pediment"][i], rise: [0.9, 0.6, 1.0][i] } });   // TD1
      },
    });
    tdOpenRisers(B);   // NT2b: open-riser stairs keep the row under the part budget
    const st = placeSub(A, B, { dx, pre });
    steps = steps.length ? [...steps, { goto: [-W - X - 2.0, -Z - 2.5] }, { goto: [-W - X - 2.0, Z + 3.0] }, ...st] : st;
  });
  roomTypes(A, [HALLS, [/kitchen/, "kitchen"], [/sitting/, "sitting"], [/bedroom/, "bedroom"]]);
  tourFrom(A, steps);
  return A;
}

// ============================================================================ 1c. four-storey apartment block
export function apartmentBlock() {
  const X = 9.0, Z = 6.5;
  const A = mmeta("mod_bld_apartment_block", "Apartment block", "Modern",
    "Four-storey apartment block, 18 x 13 m: a grey-rendered ground floor with a glazed double entrance under a steel canopy, brick-slip upper floors with white-rendered centre bays, concrete balconies with glass balustrades for every flat, a parapet roof with a lift overrun and plant. Enterable: an entrance lobby and two ground-floor flats; two flats per upper floor (living room and bedroom each), off a central stair hall corridor with two stair cores.",
    4, [19.0, 16.0]);
  const fx = { w: 1.4, h: 1.5, sill: 0.8 };
  modHouse(A, {
    X, Z, T: 0.35, h: 3.0, n: 4, corridor: 2.0, plinthDepth: 0.5, shot: "mod_apt",
    style: (f) => (f === 0 ? { m: "render_grey", shutters: false } : { m: "brick_slip", shutters: false, split: [{ a0: -X, a1: -6.0, m: "brick_slip" }, { a0: -6.0, a1: 6.0, m: "render_m" }, { a0: 6.0, a1: X, m: "brick_slip" }] }),
    wd: () => fx,
    win: (f) => (f === 0 ? { front: [{ c: -7.4, w: 1.0 }, { c: -4.0, w: 1.8 }, { c: 4.0, w: 1.8 }, { c: 7.4, w: 1.0 }], rear: [{ c: -4.0 }, { c: 4.0 }], east: [{ c: 2.5, w: 1.2 }], west: [{ c: 2.5, w: 1.2 }] }
      : { front: [{ c: -7.4, w: 1.0 }, { c: -4.5 }, { c: -1.5, w: 1.6, h: 2.2, sill: 0.1 }, { c: 1.5, w: 1.6, h: 2.2, sill: 0.1 }, { c: 4.5 }, { c: 7.4, w: 1.0 }], rear: [{ c: -4.5 }, { c: 4.5 }], east: [{ c: 2.5, w: 1.0 }], west: [{ c: 2.5, w: 1.0 }] }),
    parts: (f) => (f === 0 ? [-2.0, 2.0] : [-3.0, 0, 3.0]),
    names: (f, r) => (f === 0 ? ["flat_0a", "lobby", "flat_0b"][r] : [`flat_${f}a_bed`, `flat_${f}a_living`, `flat_${f}b_living`, `flat_${f}b_bed`][r]),
    hall: 1, doorW: 1.6, frontDouble: true,
    roof: { type: "none", rise: 1 },
    dress: (A, k) => {
      const { EAVE, yAt, zA } = k, fr = face("front", X, Z);
      noBeams(A, EAVE);
      flatRoof(A, -X, X, -Z, Z, EAVE, { m: "brick_slip", vents: [[-5, -2], [5, -2], [0, 3]], kit: "dish" });
      A.box([-1.6, EAVE, zA - 0.2], [1.6, EAVE + 2.4, zA + 2.2], "concrete_m", { bv: 0.02 });   // lift overrun
      A.box([-1.65, EAVE + 2.4, zA - 0.25], [1.65, EAVE + 2.48, zA + 2.25], "alu", { k: 0, bv: 0.01 });
      A.box([-0.6, EAVE + 1.4, zA + 2.2], [0.6, EAVE + 1.9, zA + 2.23], "grille", L1);
      hvac(A, -5.5, 2.5, EAVE + 0.16); hvac(A, 5.5, 2.5, EAVE + 0.16);
      canopy(A, fr, -1.8, 1.8, GF + 2.6, 1.6);
      signPanel(A, fr, 1.1, 1.6, GF + 2.0, GF + 2.3, "sign_c", { depth: 0.05 });
      for (let f = 1; f < 4; f++) for (const [a0, a1] of [[-2.6, -0.4], [0.4, 2.6]]) glassBalcony(A, fr, a0, a1, yAt(f), 1.4);
      for (let f = 1; f < 4; f++) for (const s of ["front", "rear"]) fbox(A, face(s, X, Z), -X - 0.02, X + 0.02, yAt(f) - 0.3, yAt(f) - 0.08, -0.01, 0.06, "concrete_m", { bv: 0.01 });
      for (const sx of [-1, 1]) { dpipe(A, fr, sx * (X - 0.2), 0.05, EAVE + 0.4); dpipe(A, face("rear", X, Z), sx * (X - 0.2), 0.05, EAVE + 0.4); }
      // lobby: a bank of letter boxes on the west wall, LED panels
      A.box([-1.925, GF + 0.9, 1.0], [-1.6, GF + 1.9, 3.0], "alu", { bv: 0.01 });
      for (let z = 1.1; z < 2.95; z += 0.37) for (let y = GF + 0.95; y < GF + 1.85; y += 0.3) A.box([-1.6, y, z], [-1.59, y + 0.25, z + 0.33], "metal_panel", L1);
      ceilLights(A, -1.9, 1.9, zA + 0.2, 6.0, yAt(1) - 0.2, 2.0);
      A.box([-1.2, GF, Z - 0.35 + 0.02], [1.2, GF + 0.012, Z - 0.35 + 1.2], "rubber", L1);   // entrance mat (inside)
      hTown(A, k, EAVE + 0.9, { wall: "brick_slip", crest: { w: 6.4, kind: "pediment", rise: 1.1 }, piers: "brick_slip", pierCorners: ["fe", "fw"],
        fe: { side: "west", lo: -Z + 0.4, hi: Z - 0.4, len: 4.2, ys: [yAt(1), yAt(2), yAt(3)], from: "hi", lite: true } });   // TD1
    },
  });
  roomTypes(A, [HALLS, [/lobby/, "lobby"], [/_bed$/, "bedroom"], [/_living$|flat_0/, "sitting"]]);
  return A;
}

// ============================================================================ 1d. single-storey bungalow with porch
export function bungalow() {
  const X = 7.5, Z = 5.0;
  const A = mmeta("mod_bld_bungalow", "Modern bungalow", "Modern",
    "Single-storey bungalow, 15 x 10 m: brick-slip walls on a concrete plinth, a low hipped roof of grey concrete tiles, aluminium windows with precast sills, a front porch with a concrete deck, steps and a flat steel canopy on posts. Enterable: two bedrooms, a bathroom, a living room and a kitchen in a row joined by doorways along the rear.",
    1, [16.5, 14.0]);
  modHouse(A, {
    X, Z, T: 0.35, h: 2.9, n: 1, plinthDepth: 0.5, shot: "mod_bung",
    style: modStyle("brick_slip"), wd: () => ({ w: 1.4, h: 1.4, sill: 0.8 }),
    win: () => ({ front: [{ c: -5.4, w: 1.6 }, { c: -2.6, w: 0.6, h: 0.6, sill: 1.5 }, { c: -0.9, w: 1.0 }, { c: 3.6, w: 1.2, h: 1.1, sill: 1.0 }, { c: 6.0 }],
      rear: [{ c: -5.4 }, { c: 0.4, w: 2.0, h: 2.0, sill: 0.2 }, { c: 6.0 }], east: [{ c: 0, w: 1.2 }], west: [{ c: 0, w: 1.2 }] }),
    parts: () => [-3.6, -1.6, 2.4, 4.8], names: (f, r) => ["bedroom_1", "bathroom", "sitting", "kitchen", "bedroom_2"][r], hall: 2, rdx: 3.6, doorW: 1.0,
    roof: { type: "hip", rise: 1.7, m: "roof_m", ov: 0.55 },
    dress: (A, k) => {
      const { EAVE } = k, fr = face("front", X, Z);
      noBeams(A, EAVE);
      for (const s of ["front", "rear", "east", "west"]) { const F = face(s, X, Z), ax = s === "front" || s === "rear"; fbox(A, F, ax ? -X - 0.04 : -Z, ax ? X + 0.04 : Z, EAVE - 0.25, EAVE + 0.42, -0.01, 0.04, "render_m", { bv: 0.01 }); }   // fascia band under the eaves
      // porch: concrete deck, steps, posts, flat canopy
      A.box([-1.8, -0.05, Z], [2.6, GF - 0.02, Z + 2.0], "concrete_m", { bv: 0.02 });
      A.box([-0.6, -0.05, Z + 2.0], [1.4, 0.14, Z + 2.35], "concrete_m", { bv: 0.02 });
      for (const x of [-1.6, 2.4]) { A.box([x - 0.08, GF - 0.02, Z + 1.72], [x + 0.08, 2.75, Z + 1.88], "metal_panel", BV); A.box([x - 0.12, GF - 0.02, Z + 1.68], [x + 0.12, GF + 0.06, Z + 1.92], "stainless", L1); }
      A.box([-1.85, 2.75, Z], [2.65, 2.9, Z + 2.05], "metal_panel", { bv: 0.015 });
      A.box([-1.88, 2.7, Z + 1.98], [2.68, 2.95, Z + 2.07], "alu", { k: 0, bv: 0.008 });
      A.box([-0.2, 2.735, Z + 0.7], [1.0, 2.75, Z + 1.0], "light_panel", L1);
      signPanel(A, fr, 1.0, 1.2, GF + 1.5, GF + 1.7, "sign_w", { depth: 0.04 });
      for (const sx of [-1, 1]) dpipe(A, fr, sx * (X - 0.2), 0.05, EAVE + 0.3);
      A.box([-6.8, -0.02, Z + 0.3], [-3.8, 0.45, Z + 0.8], "concrete_m", { bv: 0.02 }); A.box([-6.75, 0.45, Z + 0.35], [-3.85, 0.48, Z + 0.75], "earth", L1);   // planter
      A.box([-0.1, -0.02, Z + 2.35], [0.9, 0.04, Z + 4.0], "paving", { bv: 0.01 });
      tdQuoins(A, { x0: -X, x1: X, z0: -Z, z1: Z }, GF + 0.02, EAVE - 0.27, "td_cast_stone");   // TD1
      A.box([-X + 0.4, 0.5, -Z - 0.6], [-X + 1.3, 1.3, -Z - 0.05], "hvac", { bv: 0.02 }); A.box([-X + 0.45, 0.6, -Z - 0.61], [-X + 1.25, 1.2, -Z - 0.6], "grille", L1);   // heat pump
    },
  });
  roomTypes(A, [[/bedroom/, "bedroom"], [/bathroom/, "bathroom"], [/sitting/, "sitting"], [/kitchen/, "kitchen"]]);
  return A;
}

// ============================================================================ 1e. three-storey mixed use (shop, flats above)
export function mixedUse() {
  const X = 6.5, Z = 6.0;
  const A = mmeta("mod_bld_mixed_use", "Mixed-use shop and flats", "Modern",
    "Three-storey corner-plot building, 13 x 12 m: a glazed shopfront in a dark-grey rendered ground floor with a blank fascia sign panel and a steel canopy, two floors of flats in brick slip above with tall windows and glass Juliet balconies, a parapet roof with plant. Enterable: the shop with a fixed counter, a rear stair corridor with two stairs, and a flat on each upper floor (kitchen, living room and bedroom).",
    3, [14.0, 14.5]);
  const up = { w: 1.2, h: 1.5, sill: 0.8 };
  modHouse(A, {
    X, Z, T: 0.35, h: 3.1, n: 3, corridor: 2.0, plinthDepth: 0.5, shot: "mod_mixed",
    style: (f) => ({ m: f === 0 ? "render_grey" : "brick_slip", shutters: false }), wd: (f) => (f === 0 ? { w: 1.8, h: 2.4, sill: 0.35 } : up),
    win: (f) => (f === 0 ? { front: [{ c: -4.7, w: 2.0 }, { c: -1.9 }, { c: 1.9 }, { c: 4.7, w: 2.0 }], rear: [{ c: -4.0, w: 1.2, h: 1.4, sill: 0.9 }, { c: 4.0, w: 1.2, h: 1.4, sill: 0.9 }] }
      : { front: [{ c: -4.7 }, { c: -2.25, w: 1.0 }, { c: 0.1, w: 1.4, h: 2.2, sill: 0.1 }, { c: 2.35 }, { c: 4.7 }], rear: [{ c: -4.0 }, { c: 0 }, { c: 4.0 }], east: [{ c: 2.0, w: 1.0 }], west: [{ c: 2.0, w: 1.0 }] }),
    parts: (f) => (f === 0 ? [] : [-1.0, 1.2]),
    names: (f, r) => (f === 0 ? "shop" : [`flat${f}_kitchen`, `flat${f}_sitting`, `flat${f}_bedroom`][r]), doorW: 1.4,
    roof: { type: "none", rise: 1 },
    dress: (A, k) => {
      const { EAVE, yAt } = k, fr = face("front", X, Z);
      noBeams(A, EAVE);
      flatRoof(A, -X, X, -Z, Z, EAVE, { m: "brick_slip", vents: [[-3, -3.5]], kit: "dish" });
      hvac(A, 3.5, -3.0, EAVE + 0.16, 1.4, 1.0, 0.9);
      signPanel(A, fr, -X + 0.3, X - 0.3, yAt(1) - 0.2, yAt(1) + 0.45, "sign_d", { depth: 0.14 });
      canopy(A, fr, -X + 0.4, X - 0.4, GF + 2.85, 1.2);
      for (const f of [1, 2]) glassBalcony(A, fr, -0.65, 0.85, yAt(f), 0.25);
      fbox(A, fr, -X - 0.02, X + 0.02, yAt(2) - 0.3, yAt(2) - 0.08, -0.01, 0.06, "concrete_m", { bv: 0.01 });
      for (const a of [-1.9, 1.9, -4.7, 4.7]) fbox(A, fr, a - (Math.abs(a) > 4 ? 1.0 : 0.9), a + (Math.abs(a) > 4 ? 1.0 : 0.9), GF - 0.02, GF + 0.33, -0.01, 0.08, "metal_panel", { k: 0, bv: 0.01 });   // stall risers
      for (const sx of [-1, 1]) dpipe(A, face("rear", X, Z), sx * (X - 0.2), 0.05, EAVE + 0.4);
      counter(A, 1.6, 3.2, 2.6, 3.2, GF);   // shop counter beside the door
      register(A, 2.4, 2.8, GF + 1.0, [0, 1]);
      ceilLights(A, -3.5, 3.5, k.zA + 0.2, 5.6, yAt(1) - 0.2, 2.2);
      hTown(A, k, EAVE + 0.9, { wall: "brick_slip", crest: { w: 5.0, kind: "step", rise: 1.1 }, piers: "brick_slip", pierCorners: ["fe", "fw"], quoin: "td_cast_stone", yq0: yAt(1) + 0.05,
        courses: [yAt(1) + 0.66], fe: { side: "east", lo: -Z + 0.4, hi: Z - 0.4, len: 4.2, ys: [yAt(1), yAt(2)], from: "hi" }, roof: ["tank", "ac"] });   // TD1
    },
  });
  roomTypes(A, [HALLS, [/shop/, "shop"], [/kitchen/, "kitchen"], [/sitting/, "sitting"], [/bedroom/, "bedroom"]]);
  return A;
}

// ============================================================================ 2a. small corner diner
export function dinerSmall() {
  const X = 5.0, Z = 4.0;
  const A = mmeta("mod_bld_diner_small", "Corner diner", "Modern",
    "Small corner diner, 10 x 8 m: white render over a red-tiled stall riser, wrap-round windows with a fabric awning, a blank sign panel on the parapet, a flat roof with an extract fan and a packaged air-conditioning unit. Enterable: the dining room with a counter, fixed stools and two booths, a street door and a side door, and the kitchen behind with a cooking line under a stainless hood ducted through the roof.",
    1, [11.5, 10.5]);
  modHouse(A, {
    X, Z, T: 0.3, h: 3.2, n: 1, plinthDepth: 0.5, shot: "mod_diner",
    style: modStyle("render_m"), wd: () => ({ w: 1.6, h: 1.4, sill: 0.9 }),
    win: () => ({ front: [{ c: -3.6, w: 1.8 }, { c: 0.2, w: 1.2 }, { c: 3.0, w: 0.9, h: 0.8, sill: 1.5 }], west: [{ c: -1.2, w: 1.4 }], rear: [{ c: -3.0, w: 1.0, h: 1.0, sill: 1.2 }] }),
    parts: () => [1.6], names: (f, r) => ["dining", "prokitchen"][r], hall: 0, rdx: 3.1, doorW: 1.0,
    doors: [{ id: "side_door", side: "west", c: 1.0, w: 1.0, h: 2.2, room: "dining" }],
    roof: { type: "none", rise: 1 },
    dress: (A, k) => {
      const { EAVE, XI, ZI } = k, fr = face("front", X, Z), we = face("west", X, Z);
      noBeams(A, EAVE);
      const yt = flatRoof(A, -X, X, -Z, Z, EAVE, { ph: 1.0, vents: [[-3, -2.5]] });
      hvac(A, -2.0, -1.5, EAVE + 0.16, 1.4, 1.0, 0.9);
      signPanel(A, fr, -3.5, 1.0, EAVE + 0.1, yt - 0.1, "sign_a", { depth: 0.12 });
      signPanel(A, we, -2.5, 1.5, EAVE + 0.1, yt - 0.1, "sign_a", { depth: 0.12 });
      for (const F of [fr, we]) { const ax = F === fr; fbox(A, F, ax ? -X : -Z, ax ? X : Z, GF - 0.02, GF + 0.85, -0.01, 0.025, "vinyl_red", { k: 0, bv: 0.005 }); }   // tiled riser
      tdAwning(A, fr, -4.6, 0.9, GF + 2.75, { dep: 1.05, drop: 0.4, m: "fabric_awning", sag: 0.35, torn: [2], hang: [3] }); awningM(A, we, -2.0, -0.4, GF + 2.75, 0.9, 0.4);   // TD1: bent and torn
      // counter with stools, a back counter, two booths
      counter(A, 0.4, 1.0, -1.6, 2.6, GF, { h: 1.05 });
      for (let z = -1.3; z < 2.5; z += 0.65) stool(A, 0.0, z, GF);
      register(A, 0.7, 2.2, GF + 1.05, [-1, 0]);
      counter(A, 1.15, 1.5, -1.6, 2.6, GF, { h: 0.9, top: "stainless" });
      booth(A, -3.6, ZI - 0.62, GF, "x", 1.2); booth(A, -3.6, -1.2, GF, "x", 1.2);
      // kitchen line on the east wall, hood and duct
      const xl = XI;
      for (const [z0, kind, w] of [[-1.8, "sink", 0.6], [-1.2, "griddle", 0.9], [-0.3, "range", 0.9], [0.6, "fryer", 0.5], [1.1, "hold", 0.6]]) {
        kitLineZ(A, z0, xl, -1, GF, kind, w);
      }
      hoodZ(A, -1.2, 1.1, xl, -1, GF, 0.0, EAVE + 0.3);
      hTown(A, k, yt, { wall: "render_m", piers: "render_m", pierCorners: ["fe", "fw", "re"], roof: ["ac", "vent"] });   // TD1
      tiles(A, "z", xl, -1, -ZI, ZI, GF); tiles(A, "x", ZI, -1, 1.7, XI, GF); tiles(A, "x", -ZI, 1, 1.7, XI, GF);
      ceilLights(A, -XI, 1.5, -ZI, ZI, EAVE - 0.2, 2.2); ceilLights(A, 1.7, XI, -ZI, ZI, EAVE - 0.2, 2.0);
    },
  });
  roomTypes(A, [[/dining/, "dining"], [/prokitchen/, "prokitchen"]]);
  return A;
}
// kitchen line pieces against a wall at x = xw facing s along x (a z-running line): kitLine turned by swapping axes
function kitLineZ(A, z0, xw, s, y, kind, w) {
  const n = A.parts.length;
  kitLine(A, z0, xw, s, y, [[kind, w]]);
  swapXZ(A.parts, n);
}
function hoodZ(A, z0, z1, xw, s, y, ductZ, yTop) {
  const n = A.parts.length;
  hood(A, z0, z1, xw, s, y, ductZ, yTop);
  swapXZ(A.parts, n);
}
// mirror the parts from index n across the x = z plane (box centres and sizes; rod / cylinder rotations re-aimed)
function swapXZ(list, n) {
  for (const p of list.slice(n)) {
    p.c = [p.c[2], p.c[1], p.c[0]];
    if (p.s) p.s = [p.s[2], p.s[1], p.s[0]];
    if (p.r) p.r = [-p.r[2], -p.r[1], -p.r[0]];   // shortcut: exact for single-axis turns (all the kitchen pieces use); compound eulers would need a quaternion
  }
}

// ============================================================================ 2b. medium family restaurant
export function restaurantFamily() {
  const X = 9.0, Z = 6.0;
  const A = mmeta("mod_bld_restaurant_family", "Family restaurant", "Modern",
    "Mom-and-pop family restaurant, 18 x 12 m: timber-clad and rendered single storey with fabric awnings over the dining-room windows, a blank sign panel over the entrance, a parapet roof with an extract fan and air-conditioning plant. Enterable: restrooms with cubicles, a dining room with its own street door, a bar room with a fixed bar counter, stools and back bar, and a commercial kitchen with a cooking line under a stainless hood, a prep island and a back door.",
    1, [19.5, 14.5]);
  modHouse(A, {
    X, Z, T: 0.35, h: 3.4, n: 1, plinthDepth: 0.5, shot: "mod_rest_fam",
    style: modStyle("render_m", { split: [{ a0: -X, a1: 0.0, m: "clad_wood" }, { a0: 0.0, a1: X, m: "render_m" }] }), wd: () => ({ w: 2.0, h: 1.8, sill: 0.7 }),
    win: () => ({ front: [{ c: -5.2 }, { c: -1.3, w: 1.8 }, { c: 3.5, w: 1.0 }, { c: 7.4, w: 1.0, h: 0.8, sill: 1.5 }], rear: [{ c: -3.3, w: 1.4 }, { c: -7.6, w: 0.6, h: 0.6, sill: 1.7 }, { c: 2.1, w: 1.2, h: 1.2, sill: 1.0 }], west: [{ c: 1.0, w: 0.6, h: 0.6, sill: 1.7 }] }),
    parts: () => [-6.6, 0.0, 4.2], names: (f, r) => ["restrooms", "dining", "bar", "prokitchen"][r], hall: 2, rdx: 6.4, doorW: 1.1,
    doors: [{ id: "dining_door", side: "front", c: -3.3, w: 1.0, h: 2.2, room: "dining" }],
    roof: { type: "none", rise: 1 },
    dress: (A, k) => {
      const { EAVE, XI, ZI } = k, fr = face("front", X, Z), y = GF;
      noBeams(A, EAVE);
      const yt = flatRoof(A, -X, X, -Z, Z, EAVE, { ph: 0.9, m: "render_m", vents: [[-7.5, -4.5]] });
      hvac(A, -3.0, -3.0, EAVE + 0.16); hvac(A, 1.5, -3.5, EAVE + 0.16, 1.4, 1.0, 0.9);
      signPanel(A, fr, 0.4, 4.0, EAVE + 0.05, yt - 0.05, "sign_c", { depth: 0.14 });
      awningM(A, fr, -1.3 - 1.1, -1.3 + 1.1, y + 2.8, 1.0, 0.45);
      tdAwning(A, fr, -5.2 - 1.1, -5.2 + 1.1, y + 2.8, { dep: 1.05, drop: 0.45, m: "fabric_awning", sag: 0.3, torn: [1] });   // TD1: sagging, half the cloth gone
      canopy(A, fr, 1.3, 2.9, y + 2.6, 1.1); canopy(A, fr, -4.1, -2.5, y + 2.6, 1.0);
      dpipe(A, face("rear", X, Z), -X + 0.2, 0.05, EAVE + 0.4); dpipe(A, face("rear", X, Z), X - 0.2, 0.05, EAVE + 0.4);
      hTown(A, k, yt, { wall: "render_m", crest: { a: -4.5, w: 4.2, kind: "block", rise: 0.55 }, piers: "render_m", pierCorners: ["fe", "fw"], roof: ["vent", "ac"] });   // TD1
      // bar: counter along z, stools, back bar with shelves against the kitchen partition
      counter(A, 2.9, 3.5, -2.8, 2.2, y, { h: 1.1 });
      A.box([2.86, y + 1.06, -2.84], [2.9, y + 1.1, 2.24], "stainless", L1);   // foot rail line
      for (let z = -2.5; z < 2.1; z += 0.62) stool(A, 2.5, z, y);
      register(A, 3.2, 1.8, y + 1.1, [-1, 0]);
      counter(A, 3.75, 4.12, -2.8, 2.2, y, { h: 0.9 });
      for (const yy of [y + 1.4, y + 1.85]) A.box([3.85, yy, -2.6], [4.12, yy + 0.03, 2.0], "glass", { k: 0 });
      A.box([4.1, y + 1.2, -2.6], [4.123, y + 2.3, 2.0], "glass_dark", L1);   // mirror back
      for (const z of [-2.0, -0.6, 0.8]) A.box([3.3, y + 2.3, z - 0.4], [3.6, y + 2.33, z + 0.4], "light_panel", L1);
      // kitchen: line on the east wall, hood, prep island, tiled walls; restrooms
      let z = -2.5;
      for (const [kind, w] of [["sink", 0.6], ["range", 0.9], ["griddle", 0.9], ["fryer", 0.5], ["fryer", 0.5], ["hold", 0.6]]) { kitLineZ(A, z, XI, -1, y, kind, w); z += w; }
      hoodZ(A, -1.9, 1.3, XI, -1, y, -0.3, EAVE + 0.3);
      counter(A, 5.2, 6.0, -0.5, 2.0, y, { h: 0.9, top: "stainless", body: "stainless" });
      tiles(A, "z", XI, -1, -ZI, ZI, y); tiles(A, "x", ZI, -1, 4.3, XI, y); tiles(A, "x", -ZI, 1, 4.3, XI, y);
      restroom(A, -8.6, -6.7, ZI, -1, y, 2);
      tiles(A, "z", -XI, 1, -ZI, ZI, y);
      ceilLights(A, -6.5, -0.1, -ZI, ZI, EAVE - 0.2, 2.4); ceilLights(A, 4.3, XI, -ZI, ZI, EAVE - 0.2, 2.0);
    },
  });
  roomTypes(A, [[/restrooms/, "bathroom"], [/dining|bar/, "dining"], [/prokitchen/, "prokitchen"]]);
  return A;
}

// ============================================================================ 2c. large two-storey restaurant
export function restaurantLarge() {
  const X = 10.0, Z = 6.5;
  const A = mmeta("mod_bld_restaurant_large", "Two-storey restaurant", "Modern",
    "Large two-storey restaurant, 20 x 13 m: brick slip with tall aluminium windows, fabric awnings, a first-floor glass-railed terrace over the entrance, a blank sign panel on the parapet, a flat roof with plant and the kitchen extract fan. Enterable: a main dining room, an entrance hall and a commercial kitchen (cooking line, hood and duct, prep island) below; an upstairs dining room and restrooms; two stairs off a rear corridor on both floors.",
    2, [21.0, 16.5]);
  modHouse(A, {
    X, Z, T: 0.35, h: 3.5, n: 2, corridor: 2.0, plinthDepth: 0.5, shot: "mod_rest_big",
    style: (f) => ({ m: f === 0 ? "brick_slip" : "brick_grey", shutters: false }), wd: () => ({ w: 2.2, h: 2.2, sill: 0.5 }),
    win: (f) => (f === 0 ? { front: [{ c: -8.3, w: 1.4 }, { c: -5.6 }, { c: -3.0, w: 1.8 }, { c: 8.3, w: 1.0, h: 1.0, sill: 1.4 }], rear: [{ c: -4.25, w: 1.4, h: 1.6, sill: 0.8 }, { c: 4.75, w: 1.0, h: 1.0, sill: 1.2 }], west: [{ c: 2.5, w: 1.4, h: 2.0, sill: 0.6 }] }
      : { front: [{ c: -8.3, w: 1.4, h: 2.0, sill: 0.6 }, { c: -5.4, h: 2.0, sill: 0.6 }, { c: -2.4, w: 1.6, h: 2.3, sill: 0.1 }, { c: 0.2, w: 1.6, h: 2.3, sill: 0.1 }, { c: 5.2, w: 0.8, h: 0.8, sill: 1.6 }], rear: [{ c: -4.0, w: 1.4, h: 1.6, sill: 0.8 }, { c: 4.0, w: 0.8, h: 0.8, sill: 1.6 }], west: [{ c: 2.5, w: 1.4, h: 2.0, sill: 0.6 }] }),
    parts: (f) => (f === 0 ? [-1.5, 2.5] : [3.5]),
    names: (f, r) => (f === 0 ? ["dining_main", "entrance", "prokitchen"] : ["dining_upper", "restrooms"])[r], hall: 1, doorW: 1.6, frontDouble: true,
    roof: { type: "none", rise: 1 },
    dress: (A, k) => {
      const { EAVE, yAt, ZI, zA } = k, fr = face("front", X, Z), y = GF;
      noBeams(A, EAVE);
      const yt = flatRoof(A, -X, X, -Z, Z, EAVE, { ph: 1.0, m: "brick_grey", vents: [[-8, -4]] });
      hvac(A, -5.0, -2.5, EAVE + 0.16); hvac(A, -1.5, -2.5, EAVE + 0.16);
      signPanel(A, fr, -6.0, 4.0, EAVE + 0.1, yt - 0.1, "sign_a", { depth: 0.14 });
      glassBalcony(A, fr, -3.6, 1.4, yAt(1), 1.8);
      A.box([-3.2, yAt(1) - 0.235, Z + 0.6], [1.0, yAt(1) - 0.22, Z + 1.2], "light_panel", L1);
      awningM(A, fr, -5.6 - 1.2, -5.6 + 1.2, y + 2.95, 1.0, 0.45);
      tdAwning(A, fr, -8.3 - 1.2, -8.3 + 1.2, y + 2.95, { dep: 1.05, drop: 0.45, m: "fabric_awning", sag: 0.4, hang: [0] });   // TD1: frame bent, a panel hanging loose
      fbox(A, fr, -X - 0.02, X + 0.02, yAt(1) - 0.32, yAt(1) - 0.08, -0.01, 0.06, "concrete_m", { bv: 0.01 });
      for (const sx of [-1, 1]) dpipe(A, face("rear", X, Z), sx * (X - 0.2), 0.05, EAVE + 0.4);
      hTown(A, k, yt, { wall: "brick_grey", crest: { a: 7.0, w: 3.6, kind: "step", rise: 0.9 }, piers: "brick_grey", quoin: "td_cast_stone", roof: ["ac"] });   // TD1
      // kitchen: line along the front wall, hood with its duct through the restrooms above to the roof
      kitLine(A, 3.1, ZI, -1, y, [["range", 0.9], ["griddle", 0.9], ["fryer", 0.5], ["fryer", 0.5], ["sink", 0.6]]);
      hood(A, 3.1, 5.9, ZI, -1, y, 6.15, EAVE + 0.3);
      counter(A, 3.5, 5.5, 1.0, 1.8, y, { h: 0.9, top: "stainless", body: "stainless" });
      tiles(A, "x", ZI, -1, 2.6, 7.0, y); tiles(A, "z", 2.575, 1, zA + 0.2, ZI, y);
      // upstairs: a fixed banquette along the rear partition of the dining room, restrooms
      for (let x = -6.6; x < 2.6; x += 3.0) { A.box([x, yAt(1), zA + 0.15], [x + 2.6, yAt(1) + 0.45, zA + 0.7], "metal_panel"); A.box([x + 0.02, yAt(1) + 0.45, zA + 0.17], [x + 2.58, yAt(1) + 0.55, zA + 0.68], "vinyl_red", { k: 0, bv: 0.03 }); A.box([x, yAt(1) + 0.45, zA + 0.15], [x + 2.6, yAt(1) + 1.15, zA + 0.3], "vinyl_red", { bv: 0.03 }); }
      restroom(A, 3.6, 5.7, ZI, -1, yAt(1), 2);   // clear of the kitchen duct at x 6.15
      ceilLights(A, -7.0, -1.6, zA + 0.2, ZI, yAt(1) - 0.2, 2.4); ceilLights(A, 2.6, 7.0, zA + 0.2, ZI, yAt(1) - 0.2, 2.0); ceilLights(A, -7.0, 3.4, zA + 0.2, ZI, EAVE - 0.2, 2.4);
    },
  });
  roomTypes(A, [HALLS, [/dining/, "dining"], [/entrance/, "hall"], [/prokitchen/, "prokitchen"], [/restrooms/, "bathroom"]]);
  return A;
}

// ============================================================================ 3. fast-food drive-through
export function fastFood() {
  const X = 10.0, Z = 7.0, dx = -2.0;
  const A = mmeta("mod_bld_fastfood", "Burger drive-through", "Modern",
    "Generic burger drive-through restaurant, 20 x 14 m building with a drive-through lane down the east side (25 x 18 m overall): red and yellow fascia bands as blank sign panels, a glazed front with fixed booths, a steel entrance canopy, rooftop air-conditioning units and the kitchen extract fan. Enterable: a dining room with fixed booths facing a service counter with three registers and menu panels in a hatch to the kitchen, a commercial kitchen with fryer bank, griddle and range under a stainless extraction hood ducted up through the roof, holding cabinets, a prep table and the drive-through window, a walk-in cooler annex, and restrooms with cubicles.",
    1, [25.5, 19.0]);
  const B = sub("ff");
  modHouse(B, {
    X, Z, T: 0.3, h: 3.6, n: 1, plinthDepth: 0.5, shot: "mod_ff",
    style: modStyle("brick_grey"), wd: () => ({ w: 2.4, h: 2.0, sill: 0.6 }),
    win: () => ({ front: [{ c: -4.4 }, { c: -1.6 }, { c: 3.6, w: 1.2 }], rear: [{ c: -3.0, w: 1.6, h: 1.4, sill: 0.9 }, { c: -8.0, w: 0.6, h: 0.6, sill: 1.7 }], west: [{ c: 2.0, w: 0.6, h: 0.6, sill: 1.7 }], east: [{ c: 1.0, w: 1.4, h: 1.1, sill: 0.95 }] }),
    parts: () => [-6.0], names: (f, r) => ["restrooms", "main"][r], hall: 1, rdx: 6.0, doorW: 1.8, frontDouble: true,
    doors: [{ id: "cooler_door", side: "rear", c: 8.6, w: 0.9, h: 2.1, room: "main", leafMat: "stainless" }],
    roof: { type: "none", rise: 1 },
    dress: (B, k) => {
      const { EAVE, XI, ZI } = k, y = GF, fr = face("front", X, Z), ea = face("east", X, Z);
      noBeams(B, EAVE);
      glazeDoor(B, "front_door");
      splitRoom(B, k, 4.5, "main", "prokitchen", [{ a0: -2.0, a1: 3.0, y0: y + 1.0, y1: y + 2.4 }]);
      B.meta.nav.rooms.find((r) => r.id === "main").id = "dining";
      for (const d of B.meta.nav.doors) { if (d.a === "main") d.a = "dining"; if (d.b === "main") d.b = "dining"; }
      for (const e of B.meta.nav.entrances) if (e.room === "main") e.room = "dining";
      for (const c of B.meta.nav.corners) if (c.room === "main") c.room = "dining";
      // walk-in cooler annex behind the kitchen, reached through cooler_door
      annexDoor(B, 8.6, -Z + 0.15, "cooler");
      const cx0 = 7.4, cx1 = 9.9, cz0 = -Z - 2.6, cz1 = -Z, ct = 0.12, ch = 2.7;
      B.box([cx0, -0.3, cz0], [cx1, y - 0.1, cz1], "concrete_m");
      B.box([cx0 + ct, y - 0.1, cz0 + ct], [cx1 - ct, y, cz1], "concrete_floor");
      B.box([cx0, y - 0.1, cz0], [cx0 + ct, y + ch, cz1], "hvac", { bv: 0.01 }); B.box([cx1 - ct, y - 0.1, cz0], [cx1, y + ch, cz1], "hvac", { bv: 0.01 });
      B.box([cx0 + ct, y - 0.1, cz0], [cx1 - ct, y + ch, cz0 + ct], "hvac", { bv: 0.01 });
      B.box([cx0 - 0.05, y + ch, cz0 - 0.05], [cx1 + 0.05, y + ch + 0.15, cz1], "hvac", { bv: 0.02 });
      for (let x = cx0 + 0.6; x < cx1 - 0.2; x += 0.6) B.box([x - 0.01, y, cz0 - 0.01], [x + 0.01, y + ch, cz0], "grille", L1);   // panel joints
      for (let z = cz0 + 0.6; z < cz1 - 0.2; z += 0.6) B.box([cx1, y, z - 0.01], [cx1 + 0.01, y + ch, z + 0.01], "grille", L1);
      hvac(B, (cx0 + cx1) / 2, (cz0 + cz1) / 2, y + ch + 0.15, 1.2, 0.8, 0.6);
      for (const z of [cz0 + 0.5, cz1 - 0.5]) B.box([cx0 + ct, y, z - 0.25], [cx0 + 0.6, y + 2.0, z + 0.25], "stainless", { bv: 0.01 });   // shelving
      B.meta.nav.rooms.push(room("cooler", [cx0 + ct, y, cz0 + ct], [cx1 - ct, y + ch - 0.1, cz1]));
      B.meta.nav.corners.push(...corners("cooler", cx0 + ct, cx1 - ct, cz0 + ct, cz1, y, 0.35));
      // service counter in the hatch (dining face in red, kitchen face in steel), registers, menu panels above
      B.box([3.85, y + 0.1, -2.0], [4.425, y + 1.0, 3.0], "sign_a", { bv: 0.01 });
      B.box([4.575, y + 0.1, -2.0], [5.1, y + 0.9, 3.0], "stainless", { bv: 0.01 });
      B.box([3.8, y + 1.0, -2.04], [5.15, y + 1.06, 3.04], "stainless", { bv: 0.008 });
      B.box([3.9, y, -1.95], [4.4, y + 0.1, 2.95], "rubber", L1);
      for (const z of [-1.0, 0.5, 2.0]) register(B, 4.3, z, y + 1.06, [-1, 0]);
      for (const [z0, z1, m] of [[-2.0, -0.4, "light_panel"], [-0.3, 1.3, "light_panel"], [1.4, 3.0, "light_panel"]]) { B.box([4.405, y + 2.55, z0], [4.425, y + 3.25, z1], "metal_panel", { k: 0 }); B.box([4.395, y + 2.6, z0 + 0.05], [4.405, y + 3.2, z1 - 0.05], m, L1); }
      B.box([4.6, y + 1.06, 1.5], [5.05, y + 1.75, 2.9], "stainless", { bv: 0.01 });   // drinks tower behind the counter
      for (let z = 1.65; z < 2.85; z += 0.3) B.box([4.58, y + 1.3, z - 0.05], [4.6, y + 1.4, z + 0.05], "rubber", L1);
      // booths along the front glazing and the restroom partition
      booth(B, -4.4, ZI - 0.62, y, "x", 1.2); booth(B, -1.6, ZI - 0.62, y, "x", 1.2);
      booth(B, -5.3, 2.2, y, "z", 1.2); booth(B, -5.3, -0.6, y, "z", 1.2);
      // kitchen: fryer line along the front wall, hood and duct up through the roof, prep table, bagging counter at the window
      kitLine(B, 5.0, ZI, -1, y, [["hold", 0.6], ["fryer", 0.5], ["fryer", 0.5], ["fryer", 0.5], ["griddle", 1.0], ["range", 0.9], ["sink", 0.6]]);
      hood(B, 5.5, 9.1, ZI, -1, y, 7.3, EAVE + 0.3);
      counter(B, 5.8, 8.2, -0.6, 0.6, y, { h: 0.9, top: "stainless", body: "stainless" });
      B.box([5.85, y + 0.25, -0.55], [8.15, y + 0.28, 0.55], "stainless", L1);
      counter(B, 9.2, XI, 0.2, 1.8, y, { h: 0.9, top: "stainless", body: "stainless" });
      register(B, 9.45, 1.0, y + 0.9, [1, 0]);
      counter(B, 9.1, XI, 2.5, 4.5, y, { h: 0.9, top: "stainless", body: "stainless" });
      tiles(B, "x", ZI, -1, 4.6, XI, y); tiles(B, "x", -ZI, 1, 4.6, XI, y); tiles(B, "z", XI, -1, -ZI, ZI, y);
      restroom(B, -9.6, -6.1, ZI, -1, y, 3);
      tiles(B, "z", -XI, 1, -ZI, ZI, y);
      ceilLights(B, -5.9, 4.4, -ZI, ZI, EAVE - 0.2, 2.4); ceilLights(B, 4.6, XI, -ZI, ZI, EAVE - 0.2, 1.8);
      // exterior: fascia bands, entrance canopy, roof plant, drive-through window canopy and shelf
      const yt = flatRoof(B, -X, X, -Z, Z, EAVE, { ph: 1.1, m: "brick_grey", vents: [[-8, -5], [-7, 5]] });
      for (const s of ["front", "east", "west"]) { const F = face(s, X, Z), ax = s === "front"; signPanel(B, F, ax ? -X + 0.2 : -Z + 0.2, ax ? X - 0.2 : Z - 0.2, EAVE - 0.55, yt - 0.05, "sign_a", { depth: 0.14 }); fbox(B, F, ax ? -X + 0.2 : -Z + 0.2, ax ? X - 0.2 : Z - 0.2, EAVE - 0.5, EAVE - 0.38, 0.19, 0.21, "sign_b", L1); }
      signPanel(B, fr, -0.2, 3.9, yt + 0.0, yt + 1.2, "sign_b", { depth: 0.2 });
      canopy(B, fr, 0.6, 3.1, y + 2.7, 1.6, "sign_a");
      hvac(B, -4.0, -3.0, EAVE + 0.16, 2.2, 1.4, 1.2); hvac(B, 0.0, -3.0, EAVE + 0.16, 2.2, 1.4, 1.2);
      canopy(B, ea, -0.8, 2.8, y + 2.5, 1.5, "sign_a");
      fbox(B, ea, 0.35, 1.65, y + 0.88, y + 0.93, 0, 0.35, "stainless", { bv: 0.005 });
      for (const sx of [-1, 1]) dpipe(B, face("rear", X, Z), sx * (X - 0.2), 0.05, EAVE + 0.4);
      hTown(B, k, yt, { wall: "brick_grey", m: "td_cast_dark", piers: "brick_grey", pierCorners: ["re", "rw"], roof: ["vent", "ac"] });   // TD1
    },
  });
  const steps = placeSub(A, B, { dx });
  // drive-through lane down the east side: concrete lane between kerbs, a height bar, menu board, speaker post
  const lx0 = dx + X + 0.4, lx1 = lx0 + 3.6;
  A.box([lx0, -0.05, -9.2], [lx1, 0.03, 9.2], "asphalt");
  A.box([dx + X, -0.05, -9.2], [lx0, 0.15, 9.2], "concrete_m", { bv: 0.02 });
  A.box([lx1, -0.05, -9.2], [lx1 + 0.25, 0.15, 9.2], "concrete_m", { bv: 0.02 });
  for (const x of [lx0 - 0.15, lx1 + 0.12]) { A.box([x - 0.1, 0.15, -8.6], [x + 0.1, 3.0, -8.4], "sign_b", BV); }
  for (let i = 0; i < 6; i++) { const x0 = lx0 - 0.25 + i * (lx1 - lx0 + 0.6) / 6; A.box([x0, 2.6, -8.58], [x0 + (lx1 - lx0 + 0.6) / 6, 2.78, -8.42], i % 2 ? "sign_b" : "rubber", { k: 0 }); }
  const mz = -4.5, mx = lx1 + 0.12;
  for (const z of [mz - 0.8, mz + 0.8]) A.box([mx - 0.07, 0.15, z - 0.07], [mx + 0.07, 1.4, z + 0.07], "metal_panel", BV);
  A.box([mx - 0.12, 1.0, mz - 1.0], [mx + 0.05, 2.4, mz + 1.0], "metal_panel", { bv: 0.01 });
  A.box([mx - 0.14, 1.06, mz - 0.94], [mx - 0.12, 2.34, mz + 0.94], "light_panel", L1);
  A.box([mx - 0.15, 2.4, mz - 1.1], [mx + 0.1, 2.5, mz + 1.1], "sign_a", { k: 0, bv: 0.01 });
  A.box([mx - 0.1, 0.15, -2.6], [mx + 0.1, 1.3, -2.4], "metal_panel", BV);
  A.box([mx - 0.14, 1.0, -2.6], [mx - 0.1, 1.25, -2.4], "grille", L1);
  const nav = A.meta.nav;
  nav.cover.push(cover([mx + 0.5, 0, mz], 270, "high", ["left", "right"]), cover([lx0 - 0.3, 0, -8], 90, "low", ["over"]));
  roomTypes(A, [[/restrooms/, "bathroom"], [/cooler/, "utility"], [/dining/, "dining"], [/prokitchen/, "prokitchen"]]);
  tourFrom(A, steps);
  return A;
}

// ============================================================================ 4. petrol station
export function gasStation() {
  const X = 7.0, Z = 4.5, sdx = -2.0, sdz = -7.0;
  const A = mmeta("mod_bld_gas_station", "Petrol station", "Modern",
    "Petrol station, 24 x 23 m: a 20 x 11.5 m steel canopy on four clad columns with a coloured fascia (blank sign panels) and recessed soffit lights over four fuel islands with two dispensers each, a concrete forecourt, a single-bay drive-through car wash with brush gantry, and a flat-roofed convenience store behind. Enterable: the shop with a checkout counter and registers, shelving gondolas and a bank of glass-door chillers, and a stock room with shelving; the car wash bay is open at both ends.",
    1, [25.0, 24.0]);
  const B = sub("gas");
  modHouse(B, {
    X, Z, T: 0.3, h: 3.4, n: 1, plinthDepth: 0.5, shot: "mod_gas",
    style: modStyle("brick_slip"), wd: () => ({ w: 2.4, h: 2.2, sill: 0.4 }),   // TD1: a brick shop
    win: () => ({ front: [{ c: -4.8 }, { c: 1.3 }], east: [{ c: 1.0, w: 0.6, h: 0.6, sill: 1.6 }], rear: [{ c: -3.0, w: 1.0, h: 0.8, sill: 1.4 }] }),
    parts: () => [3.2], names: (f, r) => ["store", "storeroom"][r], hall: 0, rdx: 4.95, doorW: 1.8, frontDouble: true,
    roof: { type: "none", rise: 1 },
    dress: (B, k) => {
      const { EAVE, XI, ZI } = k, y = GF, fr = face("front", X, Z);
      noBeams(B, EAVE);
      glazeDoor(B, "front_door");
      const yt = flatRoof(B, -X, X, -Z, Z, EAVE, { ph: 0.9, m: "brick_slip", vents: [[5, -3]] });
      // TD1: long fascia sign box (blank) with a lit soffit, steel bars over every window
      fbox(B, fr, -X + 0.15, X - 0.15, EAVE - 0.5, yt - 0.08, -0.01, 0.42, "sign_c", { k: 0, bv: 0.015 });
      fbox(B, fr, -X + 0.1, X - 0.1, yt - 0.08, yt - 0.02, -0.01, 0.46, "alu", { k: 0, bv: 0.008 });
      fbox(B, fr, -X + 0.25, X - 0.25, EAVE - 0.512, EAVE - 0.5, 0.1, 0.36, "light_panel", L1);
      fbox(B, fr, -X + 0.3, X - 0.3, EAVE - 0.38, EAVE - 0.26, 0.42, 0.432, "sign_a", L1);
      for (const c of [-4.8, 1.3]) tdBars(B, fr, c - 1.2, c + 1.2, y + 0.4, y + 2.6);
      tdBars(B, face("east", X, Z), 0.7, 1.3, y + 1.6, y + 2.2, { sp: 0.12 }); tdBars(B, face("rear", X, Z), -3.5, -2.5, y + 1.4, y + 2.2, { sp: 0.12 });
      canopy(B, fr, -3.4, -0.1, y + 2.75, 1.3);
      hvac(B, -2.0, -2.0, EAVE + 0.16, 1.6, 1.1, 1.0);
      for (const sx of [-1, 1]) dpipe(B, face("rear", X, Z), sx * (X - 0.2), 0.05, EAVE + 0.3);
      // checkout: counter along z right of the door, registers, back shelving against the partition
      counter(B, 1.4, 2.0, -1.0, 2.6, y, { h: 0.95 });
      register(B, 1.75, 1.8, y + 0.95, [-1, 0]); register(B, 1.75, 0.2, y + 0.95, [-1, 0]);
      B.box([2.75, y, -1.0], [3.12, y + 2.1, 2.6], "metal_panel", { bv: 0.01 });
      for (let yy = y + 0.9; yy < y + 2.05; yy += 0.38) B.box([2.72, yy, -0.95], [2.75, yy + 0.03, 2.55], "alu", L1);
      // gondolas (double-sided shelving runs)
      for (const gx of [-4.65, -3.15]) {
        B.box([gx - 0.35, y, -1.6], [gx + 0.35, y + 0.12, 1.6], "rubber", L1);
        B.box([gx - 0.04, y, -1.6], [gx + 0.04, y + 1.6, 1.6], "metal_panel");
        for (const s of [-1, 1]) for (let yy = y + 0.15; yy < y + 1.55; yy += 0.36) B.box([gx + Math.min(0, s * 0.33), yy, -1.58], [gx + Math.max(0, s * 0.33), yy + 0.025, 1.58], "alu", { k: 0 });
        for (const z of [-1.62, 1.62]) B.box([gx - 0.36, y, z - 0.03], [gx + 0.36, y + 1.6, z + 0.03], "sign_c", { bv: 0.01 });
        B.box([gx - 0.36, y, -1.6], [gx + 0.36, y + 1.6, 1.6], "metal_panel", { v: 0 });
      }
      // chillers on the west wall: insulated cabinet, five glass doors, a lit header
      B.box([-XI, y, -2.8], [-XI + 0.75, y + 2.2, 3.0], "hvac", { bv: 0.01 });
      for (let z = -2.8; z < 2.9; z += 1.16) {
        B.box([-XI + 0.75, y + 0.15, z + 0.03], [-XI + 0.78, y + 2.0, z + 1.13], "alu", L1);
        B.box([-XI + 0.78, y + 0.2, z + 0.08], [-XI + 0.785, y + 1.95, z + 1.08], "glass", L1);
        B.box([-XI + 0.78, y + 0.9, z + 0.95], [-XI + 0.83, y + 1.5, z + 1.0], "stainless", L1);
      }
      B.box([-XI + 0.75, y + 2.03, -2.8], [-XI + 0.8, y + 2.2, 3.0], "light_panel", L1);
      // stock room shelving on the east wall
      for (const z of [-2.0, 3.6]) B.box([XI - 0.5, y, z - 0.03], [XI, y + 2.0, z + 0.03], "steel_plate");
      for (let yy = y + 0.3; yy < y + 2.0; yy += 0.45) B.box([XI - 0.5, yy, -2.0], [XI, yy + 0.03, 3.6], "steel_plate", { k: 0 });
      B.box([XI - 0.5, y, -2.0], [XI, y + 2.0, 3.6], "steel_plate", { v: 0 });
      ceilLights(B, -XI, 3.1, -ZI, ZI, EAVE - 0.2, 2.2);
      hTown(B, k, yt, { wall: "brick_slip", m: "td_cast_dark", prof: [[0.12, 0.06], [0.14, 0.14]], corniceSides: ["rear", "east", "west"], piers: "brick_slip", roof: ["ac"] });   // TD1
    },
  });
  const steps = placeSub(A, B, { dx: sdx, dz: sdz, pre: "s_" });
  // forecourt
  A.box([-12, -0.05, sdz + Z], [12, 0.03, 11.6], "concrete_m");
  for (let x = -8; x < 12; x += 4) A.box([x - 0.01, 0.03, sdz + Z], [x + 0.01, 0.034, 11.6], "rubber", L1);
  for (let z = 2; z < 11.6; z += 4) A.box([-12, 0.03, z - 0.01], [12, 0.034, z + 0.01], "rubber", L1);
  A.box([-12, 0.03, 11.2], [12, 0.045, 11.4], "grille", L1);   // drain channel
  // TD1 canopy: a deep flat deck on six red steel columns (one per island), a 1.1 m fascia band all round, square lights
  // recessed into the soffit; six kerbed islands with two dispensers each, a price sign pylon by the street
  const cy = 5.0, cx0 = -12, cx1 = 8, cz0 = 0, cz1 = 11.5, isl = [];
  for (const z of [3.0, 8.5]) for (const x of [-8.6, -2.0, 4.6]) isl.push([x, z]);
  A.box([cx0, cy + 0.35, cz0], [cx1, cy + 1.2, cz1], "metal_panel");
  A.box([cx0 + 0.3, cy + 1.2, cz0 + 0.3], [cx1 - 0.3, cy + 1.26, cz1 - 0.3], "membrane", L1);
  const lights = [];
  for (let x = cx0 + 1.65; x < cx1; x += 3.3) for (const z of [1.9, 5.75, 9.6]) lights.push({ a0: x - 0.45, a1: x + 0.45, y0: z - 0.45, y1: z + 0.45 });
  for (const [a, b, c, d] of cutRect(cx0 + 0.05, cx1 - 0.05, cz0 + 0.05, cz1 - 0.05, lights)) A.box([a, cy + 0.25, c], [b, cy + 0.35, d], "alu", { k: 0 });   // soffit
  for (const L of lights) {
    A.box([L.a0 + 0.01, cy + 0.33, L.y0 + 0.01], [L.a1 - 0.01, cy + 0.345, L.y1 - 0.01], "light_panel", L1);
    A.box([L.a0 + 0.06, cy + 0.318, L.y0 + 0.06], [L.a1 - 0.06, cy + 0.33, L.y1 - 0.06], "glass_rail", L1);   // diffuser set up in the recess
  }
  for (const [a, b, c, d] of [[cx0, cx1, cz1, cz1 + 0.1], [cx0, cx1, cz0 - 0.1, cz0]]) {
    A.box([a - 0.1, cy + 0.2, c], [b + 0.1, cy + 1.3, d], "sign_w", { k: 0, bv: 0.01 });
    A.box([a - 0.11, cy + 0.72, Math.min(c, d) - 0.012], [b + 0.11, cy + 1.02, Math.max(c, d) + 0.012], "sign_a", L1);
    A.box([a - 0.12, cy + 0.17, Math.min(c, d) - 0.02], [b + 0.12, cy + 0.26, Math.max(c, d) + 0.02], "alu", { k: 0, bv: 0.008 });
  }
  for (const [a, b] of [[cx0 - 0.1, cx0], [cx1, cx1 + 0.1]]) {
    A.box([a, cy + 0.2, cz0], [b, cy + 1.3, cz1], "sign_w", { k: 0, bv: 0.01 });
    A.box([a - 0.012, cy + 0.72, cz0], [b + 0.012, cy + 1.02, cz1], "sign_a", L1);
    A.box([a - 0.02, cy + 0.17, cz0], [b + 0.02, cy + 0.26, cz1], "alu", { k: 0, bv: 0.008 });
  }
  for (const [x, z] of isl) {
    A.box([x - 2.3, 0.03, z - 0.65], [x + 2.3, 0.2, z + 0.65], "concrete_m", { bv: 0.03 });   // raised island
    for (const s of [-1, 1]) A.box([x + s * 2.3 - (s > 0 ? 0.3 : 0), 0.025, z - 0.67], [x + s * 2.3 + (s < 0 ? 0.3 : 0), 0.215, z + 0.67], "line_yellow", { k: 0, bv: 0.02 });   // painted kerb noses
    A.box([x - 0.2, 0.2, z - 0.2], [x + 0.2, cy + 0.25, z + 0.2], "td_steel_red", { bv: 0.02 });   // column
    A.box([x - 0.26, 0.2, z - 0.26], [x + 0.26, 0.75, z + 0.26], "stainless", { k: 0, bv: 0.01 });   // impact sleeve
    A.box([x - 0.3, cy + 0.17, z - 0.3], [x + 0.3, cy + 0.25, z + 0.3], "td_steel_red", { k: 0, bv: 0.01 });   // cap plate under the soffit
    for (const s of [-1, 1]) {
      A.cyl([x + s * 2.05, 0.7, z], [0.16, 1.0, 0.16], "sign_b", { seg: 10 });   // bollards at the island ends
      A.cyl([x + s * 2.05, 1.18, z], [0.17, 0.05, 0.17], "rubber", { seg: 10, ...L1 });
      const px = x + s * 1.15;   // PR1 dispenser: the modelled pump (tools/props/props_build.py pr1_fuel_dispenser) on the island, its collider here
      A.box([px - 0.48, 0.2, z - 0.32], [px + 0.48, 2.3, z + 0.32], "stainless", { v: 0 });
      (A.meta.models ||= []).push({ dir: "pr1_fuel_dispenser", pos: [px, 0.2, z], yaw: 0, bake: { simplify: 0.45, lod1: 0.15, jpgMaps: true } });
    }
    A.box([x - 0.75, 0.2, z + 0.34], [x - 0.4, 1.0, z + 0.58], "metal_panel", { bv: 0.01 });   // bin and screen wash
  }
  // price sign pylon: plinth, steel post, a cabinet of stacked blank price panels on both faces
  const sx = 10.4, sz = 10.0;
  A.box([sx - 0.7, -0.05, sz - 0.5], [sx + 0.7, 0.5, sz + 0.5], "concrete_m", { bv: 0.03 });
  A.box([sx - 0.18, 0.5, sz - 0.18], [sx + 0.18, 4.7, sz + 0.18], "td_steel_red", { bv: 0.02 });
  A.box([sx - 0.95, 4.7, sz - 0.24], [sx + 0.95, 8.6, sz + 0.24], "metal_panel", { bv: 0.03 });
  A.box([sx - 1.0, 8.6, sz - 0.28], [sx + 1.0, 8.7, sz + 0.28], "alu", { k: 0, bv: 0.01 });
  for (const f of [-1, 1]) {
    const zp = (a, b) => [Math.min(sz + f * a, sz + f * b), Math.max(sz + f * a, sz + f * b)];
    A.box([sx - 0.85, 7.5, zp(0.24, 0.255)[0]], [sx + 0.85, 8.45, zp(0.24, 0.255)[1]], "sign_a", L1);          // brand panel (blank)
    for (let k = 0; k < 3; k++) {
      const y0 = 5.0 + k * 0.78;
      A.box([sx - 0.85, y0, zp(0.24, 0.252)[0]], [sx + 0.85, y0 + 0.66, zp(0.24, 0.252)[1]], ["sign_w", "sign_b", "sign_d"][k], L1);
      A.box([sx - 0.1, y0 + 0.1, zp(0.252, 0.26)[0]], [sx + 0.78, y0 + 0.56, zp(0.252, 0.26)[1]], "screen", L1);   // blank price window
    }
    A.box([sx - 0.85, 4.8, zp(0.24, 0.25)[0]], [sx + 0.85, 4.92, zp(0.24, 0.25)[1]], "sign_c", L1);
  }
  // car wash: two walls, a roof, a brush gantry, guide rails; open at both ends
  const wx0 = 7.4, wx1 = 12.0, wz0 = -11.5, wz1 = -1.5, wh = 4.0;
  A.box([wx0, -0.05, wz0], [wx1, 0.05, wz1], "concrete_floor");
  A.box([wx0, 0.0, wz0], [wx0 + 0.3, wh, wz1], "concrete_m", BV); A.box([wx1 - 0.3, 0.0, wz0], [wx1, wh, wz1], "concrete_m", BV);
  A.box([wx0 - 0.1, wh, wz0 - 0.2], [wx1 + 0.1, wh + 0.35, wz1 + 0.2], "metal_panel", { bv: 0.02 });
  for (const z of [wz1 + 0.2, wz0 - 0.2]) A.box([wx0 - 0.1, wh - 0.5, Math.min(z, z + (z > wz0 ? 0.06 : -0.06))], [wx1 + 0.1, wh + 0.35, Math.max(z, z + (z > wz0 ? 0.06 : -0.06))], "sign_c", { k: 0, bv: 0.01 });
  A.box([wx0 + 0.3, wh - 0.03, wz0], [wx1 - 0.3, wh, wz1], "light_panel", { k: 0 });
  A.box([(wx0 + wx1) / 2 - 0.1, 0.05, wz0], [(wx0 + wx1) / 2 + 0.1, 0.06, wz1], "grille", L1);
  for (const x of [wx0 + 1.2, wx1 - 1.2]) A.box([x - 0.06, 0.05, wz0 + 0.5], [x + 0.06, 0.17, wz1 - 0.5], "stainless", { bv: 0.01 });
  const gz = (wz0 + wz1) / 2;
  for (const x of [wx0 + 0.45, wx1 - 0.45]) A.box([x - 0.12, 0.05, gz - 0.15], [x + 0.12, 3.5, gz + 0.15], "stainless", { bv: 0.01 });
  A.box([wx0 + 0.3, 3.3, gz - 0.15], [wx1 - 0.3, 3.55, gz + 0.15], "stainless", { bv: 0.01 });
  for (const x of [wx0 + 0.9, wx1 - 0.9]) A.cyl([x, 1.6, gz + 0.5], [0.8, 2.8, 0.8], "brush", { seg: 14 });
  A.cyl([(wx0 + wx1) / 2, 2.7, gz - 0.6], [0.7, wx1 - wx0 - 1.2, 0.7], "brush", { seg: 14, r: [0, 0, 90], k: 0 });
  A.box([wx0 + 0.6, 3.0, gz + 1.5], [wx1 - 0.6, 3.2, gz + 1.8], "hvac", { k: 0, bv: 0.01 });   // dryer bar
  A.box([wx1 - 0.2, 0.05, wz1 + 1.4], [wx1 + 0.05, 1.3, wz1 + 1.65], "metal_panel", BV);   // pay terminal
  A.box([wx1 - 0.25, 0.9, wz1 + 1.42], [wx1 - 0.2, 1.2, wz1 + 1.63], "screen", L1);
  const nav = A.meta.nav, wxc = (wx0 + wx1) / 2;
  nav.rooms.push(room("wash_bay", [wx0 + 0.3, 0.05, wz0], [wx1 - 0.3, wh - 0.1, wz1]));
  nav.corners.push(...corners("wash_bay", wx0 + 0.3, wx1 - 0.3, wz0, wz1, 0.05, 0.6));
  nav.doors.push({ a: "wash_bay", b: "outside", p: [wxc, 0.05, wz1] }, { a: "wash_bay", b: "outside", p: [wxc, 0.05, wz0] });
  nav.entrances.push({ p: [wxc, 0, wz1 + 1.5], dir: [0, 0, -1], room: "wash_bay" }, { p: [wxc, 0, wz0 - 1.5], dir: [0, 0, 1], room: "wash_bay" });
  for (const [x, z] of isl) nav.cover.push(cover([x + 1.25, 0, z - 0.8], 0, "high", ["left", "right"]), cover([x + 1.25, 0, z + 0.8], 180, "high", ["left", "right"]), cover([x - 1.25, 0, z + 0.8], 180, "high", ["left", "right"]));
  roomTypes(A, [[/s_store$/, "shop"], [/storeroom|wash_bay/, "utility"]]);
  tourFrom(A, steps, [{ goto: [6.2, sdz - Z - 1.6] }, { goto: [6.2, wz1 + 2.5] }, { goto: [wxc, wz1 + 2.5] }, { goto: [wxc, gz + 2.5], note: "into the car wash" }]);
  return A;
}

// ============================================================================ 5. bus station
export function busStation() {
  const X = 11.0, Z = 5.0, zW = 3.5, hdz = zW + Z;
  const A = mmeta("mod_bld_bus_station", "Bus station", "Modern",
    "Town bus station, 23 x 27 m: a tall glazed single-storey hall with a deep steel entrance canopy and blank sign panels; behind it a 5.5 m platform under a cantilevered canopy with four bus bays, a 7.5 m bus road and an island platform with a butterfly canopy, glass wind screens and bay posts; fixed metal benches throughout. Enterable: the waiting hall with fixed bench rows and a departure board, the ticket office behind a glazed ticket window with a counter, and restrooms; doors out to the platform.",
    1, [23.5, 27.5]);
  const B = sub("bus");
  modHouse(B, {
    X, Z, T: 0.35, h: 4.2, n: 1, plinthDepth: 0.5, shot: "mod_bus",
    style: modStyle("concrete_m"), wd: () => ({ w: 2.0, h: 3.0, sill: 0.6 }),
    win: () => ({ front: [{ c: -8.3, w: 1.4, h: 2.4, sill: 0.8 }, { c: -4.4 }, { c: 0.4 }, { c: 3.8 }, { c: 8.6, w: 0.8, h: 0.8, sill: 1.8 }],
      rear: [{ c: -8.3, w: 1.2, h: 1.6, sill: 0.9 }, { c: -5.0, w: 1.4, h: 2.6, sill: 0.6 }, { c: 5.2, w: 1.4, h: 2.6, sill: 0.6 }], west: [{ c: 0, w: 1.4, h: 1.6, sill: 0.9 }], east: [{ c: 0, w: 0.8, h: 0.8, sill: 1.8 }] }),
    parts: () => [6.5], names: (f, r) => ["main", "restrooms"][r], hall: 0, rdx: -2.075, doorW: 1.8, frontDouble: true,
    doors: [{ id: "platform_door", side: "rear", c: 2.5, w: 1.8, h: 2.4, double: true, room: "main", leafMat: "glass" }],
    roof: { type: "none", rise: 1 },
    dress: (B, k) => {
      const { EAVE, XI, ZI } = k, y = GF, fr = face("front", X, Z);
      noBeams(B, EAVE);
      glazeDoor(B, "front_door"); glazeDoor(B, "platform_door");
      splitRoom(B, k, -6.0, "main", "waiting_hall", [{ a0: -1.5, a1: 2.5, y0: y + 1.0, y1: y + 2.2 }]);
      const nv = B.meta.nav;
      // splitRoom keeps the west half under the old id: rename it ticket_office
      for (const r of nv.rooms) if (r.id === "main") r.id = "ticket_office";
      for (const d of nv.doors) { if (d.a === "main") d.a = "ticket_office"; if (d.b === "main") d.b = "ticket_office"; }
      for (const e of nv.entrances) if (e.room === "main") e.room = "ticket_office";
      for (const c of nv.corners) if (c.room === "main") c.room = "ticket_office";
      // ticket window: glazing with a mullion, speech grilles, the desk under it in the office
      B.box([-6.01, y + 1.0, -1.5], [-5.99, y + 2.2, 2.5], "glass");
      B.box([-6.04, y + 1.0, 0.45], [-5.96, y + 2.2, 0.55], "alu", { k: 0 });
      for (const z of [-0.5, 1.5]) B.cyl([-5.98, y + 1.45, z], [0.14, 0.02, 0.14], "grille", { r: [0, 0, 90], seg: 10, ...L1 });
      counter(B, -6.9, -6.075, -1.5, 2.5, y, { h: 1.0 });
      B.box([-6.1, y + 0.98, -1.5], [-5.6, y + 1.03, 2.5], "laminate", { bv: 0.008 });   // customer ledge through the wall
      for (const z of [-0.5, 1.5]) register(B, -6.6, z, y + 1.0, [1, 0]);
      // departure board and clock over the ticket window; fixed bench rows
      B.box([-5.925, y + 2.45, -1.2], [-5.86, y + 3.45, 2.2], "metal_panel", { k: 0, bv: 0.01 });
      B.box([-5.86, y + 2.52, -1.12], [-5.855, y + 3.38, 2.12], "screen", L1);
      B.cyl([-5.88, y + 3.75, 0.5], [0.5, 0.06, 0.5], "sign_w", { r: [0, 0, 90], seg: 18, ...L1 });
      for (const x0 of [0.2, 2.6]) { bench(B, x0, x0 + 1.9, 0.3, y, -1); bench(B, x0, x0 + 1.9, 0.9, y, 1); }
      bench(B, -5.0, -3.5, ZI - 0.4, y, -1);
      restroom(B, 6.6, 10.6, ZI, -1, y, 3);
      tiles(B, "z", XI, -1, -ZI, ZI, y);
      ceilLights(B, -5.9, 6.4, -ZI, ZI, EAVE - 0.2, 2.5); ceilLights(B, -XI, -6.1, -ZI, ZI, EAVE - 0.2, 2.2);
      // exterior: entrance canopy, sign panels, roof
      const yt = flatRoof(B, -X, X, -Z, Z, EAVE, { ph: 0.8, m: "concrete_m", vents: [[8, -3]] });
      canopy(B, fr, -6.0, 6.0, y + 3.6, 2.5);
      for (const x of [-5.6, 5.6]) B.box([x - 0.1, -0.02, Z + 2.25], [x + 0.1, y + 3.6, Z + 2.45], "metal_panel", BV);
      signPanel(B, fr, -4.0, 4.0, EAVE + 0.05, yt - 0.05, "sign_c", { depth: 0.14 });
      fbox(B, fr, -4.0, 4.0, EAVE + 0.15, EAVE + 0.25, 0.19, 0.2, "sign_b", L1);
      hvac(B, -6.0, -2.0, EAVE + 0.16, 2.0, 1.3, 1.1); hvac(B, 0.0, -2.0, EAVE + 0.16, 2.0, 1.3, 1.1);
      for (const sx of [-1, 1]) dpipe(B, fr, sx * (X - 0.2), 0.05, EAVE + 0.3);
      hTown(B, k, yt, { wall: "concrete_m", m: "concrete_m", prof: [[0.15, 0.08], [0.2, 0.22]], piers: "concrete_m", crest: { a: 8.0, w: 3.0, kind: "block", rise: 1.4 }, roof: ["ac", "vent"] });   // TD1
    },
  });
  const steps = placeSub(A, B, { dz: hdz, pre: "h_" });
  // platform A behind the hall: raised paving, kerb, tactile strip, cantilevered canopy, bay posts, benches
  const pz0 = -2.0, rz0 = -9.75, iz0 = -13.5, iz1 = rz0;
  A.box([-11.5, -0.05, pz0], [11.5, 0.15, zW], "paving", { bv: 0.01 });
  A.box([-11.5, -0.05, pz0 - 0.25], [11.5, 0.16, pz0], "concrete_m", { bv: 0.02 });
  A.box([-11.5, 0.15, pz0 + 0.2], [11.5, 0.155, pz0 + 0.6], "line_yellow", L1);
  A.box([-11.5, -0.05, rz0], [11.5, 0.02, pz0 - 0.25], "asphalt");
  const columns = (xs, z, h) => { for (const x of xs) { A.cyl([x, 0.15 + h / 2, z], [0.3, h, 0.3], "metal_panel", { seg: 12 }); A.cyl([x, 0.25, z], [0.4, 0.2, 0.4], "stainless", { seg: 12, ...L1 }); } };
  columns([-9, -3, 3, 9], pz0 + 0.8, 3.45);
  A.box([-11.5, 3.6, pz0 - 0.6], [11.5, 3.85, zW], "metal_panel", { bv: 0.02 });
  A.box([-11.5, 3.5, pz0 - 0.65], [11.5, 3.95, pz0 - 0.57], "sign_c", { k: 0, bv: 0.01 });
  A.box([-11.4, 3.57, pz0 - 0.5], [11.4, 3.6, zW - 0.1], "alu", { k: 0 });
  for (let x = -9; x < 10; x += 3.6) A.box([x - 0.6, 3.555, 0.0], [x + 0.6, 3.57, 0.4], "light_panel", L1);
  const bays = [-8.25, -2.75, 2.75, 8.25], post = (x, z) => {
    A.cyl([x, 1.4, z], [0.09, 2.5, 0.09], "metal_panel", { seg: 8 });
    A.box([x - 0.25, 2.25, z - 0.03], [x + 0.25, 2.75, z + 0.03], "sign_c", { k: 0, bv: 0.01 });
    A.box([x - 0.18, 2.35, z - 0.035], [x + 0.18, 2.65, z + 0.035], "sign_w", L1);
    A.box([x - 0.2, 1.1, z - 0.03], [x + 0.2, 1.7, z + 0.03], "glass_dark", L1);   // timetable case
  };
  for (const x of bays) post(x, pz0 + 0.35);
  for (const x of [-8.0, -5.5, 5.5, 8.0]) bench(A, x - 0.9, x + 0.9, -0.2, 0.15, -1);
  // island platform with a butterfly canopy, wind screens and benches
  A.box([-11.5, -0.05, iz0], [11.5, 0.15, iz1 - 0.25], "paving", { bv: 0.01 });
  A.box([-11.5, -0.05, iz1 - 0.25], [11.5, 0.16, iz1], "concrete_m", { bv: 0.02 });
  A.box([-11.5, 0.15, iz1 - 0.85], [11.5, 0.155, iz1 - 0.45], "line_yellow", L1);
  const icz = (iz0 + iz1) / 2;
  columns([-8, 0, 8], icz, 3.3);
  for (const s of [-1, 1]) A.obox([0, 3.62, icz + s * 1.05], [23, 0.18, 2.2], [s * -7, 0, 0], "metal_panel", { bv: 0.01 });
  A.box([-11.5, 3.38, icz - 0.2], [11.5, 3.52, icz + 0.2], "stainless", { bv: 0.01 });   // valley gutter
  for (let x = -9; x < 10; x += 4.5) A.box([x - 0.6, 3.36, icz + 0.4], [x + 0.6, 3.38, icz + 0.7], "light_panel", L1);
  for (const x of [-6, 6]) {
    A.box([x - 1.4, 0.15, iz0 + 0.3], [x + 1.4, 2.2, iz0 + 0.34], "glass_rail");
    A.box([x - 1.45, 0.15, iz0 + 0.27], [x + 1.45, 2.25, iz0 + 0.37], "alu", { k: 0 });
    for (const s of [-1, 1]) A.box([x + s * 1.4 - 0.02, 0.15, iz0 + 0.34], [x + s * 1.4 + 0.02, 2.2, iz0 + 1.6], "glass_rail");
    bench(A, x - 1.0, x + 1.0, iz0 + 0.85, 0.15, 1);
  }
  for (const x of bays) post(x, iz1 - 0.35);
  for (const x of [-10, 10, 0]) { A.cyl([x, 0.6, icz - 1.0], [0.4, 0.9, 0.4], "metal_panel", { seg: 12 }); A.cyl([x, 1.07, icz - 1.0], [0.42, 0.04, 0.42], "stainless", { seg: 12, ...L1 }); }   // litter bins
  const nav = A.meta.nav;
  for (const x of [-8.0, -5.5, 5.5, 8.0]) nav.cover.push(cover([x, 0.15, 0.3], 180, "low", ["over"]));
  for (const x of [-9, -3, 3, 9]) nav.cover.push(cover([x, 0.15, pz0 + 1.3], 180, "high", ["left", "right"]));
  for (const x of [-6, 6]) nav.cover.push(cover([x, 0.15, iz0 - 0.4], 0, "high", ["left", "right"]));
  roomTypes(A, [[/ticket_office/, "office"], [/waiting_hall/, "lobby"], [/restrooms/, "bathroom"]]);
  tourFrom(A, steps, [{ goto: [-2.0, -5.5], note: "across the bus road" }, { goto: [-2.0, icz], note: "onto the island platform" }]);
  return A;
}

// ============================================================================ TD1 facade / silhouette kit
// Shared with catalog_modern_buildings_b, catalog_newtown_b and catalog_newtown_c (they import it). Town dressing against
// the box look: stepped cornices and string courses, parapet crests and corner piers, quoins, steel fire escapes, sagging
// or torn awnings, window bars, roof water tanks and condensers. F is a face frame { axis, d, out } as fbox takes it; R a
// rectangle { x0, x1, z0, z1 }. Trims go only where their projecting volume (more than 3 mm out of the face) is free of
// other parts and never touch a door / window aperture (tdRuns, tdFree); nothing is coplanar with an existing face.
const TDL = { k: 0, lod: 1 }, TDK = { k: 0 };
export function tdMats(A) {
  const add = (n, alb, r, pat, ex = {}) => { if (!A.materials.some((m) => m.name === n)) A.mat(n, alb, r, pat, ex); };
  add("td_cast_stone", [0.64, 0.62, 0.57], 0.85, "concrete", { texture: "concrete_light" });
  add("td_cast_dark", [0.40, 0.39, 0.37], 0.85, "concrete", { texture: "concrete_aged" });
  add("td_fe_steel", [0.07, 0.07, 0.075], 0.6, "metal", { texture: "iron_worn", metallic: 0.3 });
  add("td_fe_walk_grid", [0.16, 0.16, 0.16], 0.7, "plain", { texture: "metal_walkway_grate" });   // keeps its grate texture in the ruin pass
  add("td_tank_wood", [0.36, 0.27, 0.19], 0.85, "wood", { texture: "boards_weathered" });
  add("td_ac", [0.62, 0.62, 0.60], 0.5, "metal", { texture: "steel_worn_grey", metallic: 0.2 });
  add("td_ac_grille", [0.25, 0.25, 0.25], 0.5, "metal", { texture: "metal_mesh_grid", metallic: 0.5 });
  add("td_downpipe", [0.18, 0.19, 0.19], 0.5, "metal", { texture: "steel_scratched", metallic: 0.4 });
  add("td_steel_red", [0.55, 0.06, 0.05], 0.4, "metal", { texture: "metal_painted_red", metallic: 0.3 });
}
export const tdFace = (R, s) => ({ front: { axis: "x", d: R.z1, out: 1 }, rear: { axis: "x", d: R.z0, out: -1 }, east: { axis: "z", d: R.x1, out: 1 }, west: { axis: "z", d: R.x0, out: -1 } }[s]);
const tdWB = (F, a0, a1, y0, y1, e0, e1) => { const d0 = F.d + F.out * e0, d1 = F.d + F.out * e1; return [P(F.axis, Math.min(a0, a1), y0, Math.min(d0, d1)), P(F.axis, Math.max(a0, a1), y1, Math.max(d0, d1))]; };
const tdP = (F, a, y, e) => P(F.axis, a, y, F.d + F.out * e);
const ovl = (a0, a1, b0, b1) => a0 < b1 - 1e-4 && a1 > b0 + 1e-4;
// a part's world box: the exact box round its rotated extents (euler r applied x, then y, then z as glm::quat(vec3) does,
// or the quaternion q of a placeSub copy)
const DGR = Math.PI / 360;
function bbOf(p) {
  let q = p.q;
  if (!q && p.r) {
    const [cx, cy, cz] = p.r.map((v) => Math.cos(v * DGR)), [sx, sy, sz] = p.r.map((v) => Math.sin(v * DGR));
    q = [sx * cy * cz - cx * sy * sz, cx * sy * cz + sx * cy * sz, cx * cy * sz - sx * sy * cz, cx * cy * cz + sx * sy * sz];
  }
  let he = p.s.map((v) => v / 2);
  if (q) {
    const [x, y, z, w] = q, M = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)], [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)], [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]];
    he = M.map((row) => row.reduce((s, m, i) => s + Math.abs(m) * he[i], 0));
  }
  return [p.c.map((v, i) => v - he[i]), p.c.map((v, i) => v + he[i])];
}
const bHit = (a, b) => [0, 1, 2].every((i) => ovl(a[0][i], a[1][i], b[0][i], b[1][i]));
const apBoxes = (A) => A.interactables.map((it) => [it.aperture.min, it.aperture.max]);
// no part in any of the boxes `rs`, no aperture in `ap` (default: the first box)
export function tdFree(A, rs, ap = rs[0]) {
  for (const p of A.parts) if (p.s) { const b = bbOf(p); if (rs.some((r) => bHit(b, r))) return false; }
  return !apBoxes(A).some((b) => bHit(b, ap));
}
// free stretches of a0..a1 on face F for a trim over y0..y1 reaching e0..e1 out of the face
export function tdRuns(A, F, a0, a1, y0, y1, e0, e1, { step = 0.1, min = 0.3, ignore = [] } = {}) {
  const pr = tdWB(F, a0, a1, y0, y1, Math.max(e0, 0.003), e1), full = tdWB(F, a0, a1, y0, y1, e0, e1), L = F.axis === "x" ? 0 : 2;
  const bl = A.parts.filter((p) => p.s && !ignore.includes(p.m)).map(bbOf).filter((b) => bHit(b, pr)).concat(apBoxes(A).filter((b) => bHit(b, full)));
  const n = Math.max(1, Math.round((a1 - a0) / step)), st = (a1 - a0) / n, runs = [];
  let s = null;
  for (let i = 0; i <= n; i++) {
    const c0 = a0 + i * st, free = i < n && !bl.some((b) => ovl(b[0][L], b[1][L], c0, c0 + st));
    if (free && s === null) s = c0;
    if (!free && s !== null) { if (c0 - s >= min - 1e-6) runs.push([s, c0]); s = null; }
  }
  return runs;
}
// rectangular bar from a to b (w across, t thick), the catalog_newtown_b convention
export function tdBar(A, a, b, w, t, m, o = {}) {
  const d = [b[0] - a[0], b[1] - a[1], b[2] - a[2]], L = Math.hypot(...d), DG = 180 / Math.PI;
  if (L < 1e-4) return null;
  return A.obox([(a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2], [w, t, L], [-Math.asin(Math.max(-1, Math.min(1, d[1] / L))) * DG, Math.atan2(d[0], d[2]) * DG, 0], m, o);
}

// Stepped cornice (bed mould, fascia, corona: prof [[height, projection]...] from the bottom) topping at y on the `sides`
// of R. Front / rear runs return round a corner onto a dressed side (each layer by its own projection); side runs stop
// 12 mm short of a dressed front / rear. All runs are measured before any is built.
const PROF = [[0.12, 0.08], [0.16, 0.2], [0.2, 0.38]];
export function tdCornice(A, R, y, sides, { m = "td_cast_stone", prof = PROF, lod = false } = {}) {
  const H = prof.reduce((s, q) => s + q[0], 0), pm = Math.max(...prof.map((q) => q[1])), jobs = [], has = (s) => sides.includes(s);
  for (const s of sides) {
    const F = tdFace(R, s), isX = F.axis === "x", lo = isX ? R.x0 : R.z0, hi = isX ? R.x1 : R.z1;
    const a0 = isX ? lo - (has("west") ? pm + 0.004 : 0) : lo + (has("rear") ? 0.012 : 0), a1 = isX ? hi + (has("east") ? pm + 0.004 : 0) : hi - (has("front") ? 0.012 : 0);
    for (const [b0, b1] of tdRuns(A, F, a0, a1, y - H, y, -0.01, pm)) jobs.push([F, b0, b1, lo, hi]);
  }
  for (const [F, b0, b1, lo, hi] of jobs) {
    let yy = y - H;
    for (const [h, p] of prof) {
      fbox(A, F, Math.max(b0, lo - p - 0.004), Math.min(b1, hi + p + 0.004), yy, yy + h, -0.01, p, m, { k: 0, bv: 0.01, lod });
      yy += h;
    }
  }
  return jobs.length;
}
// a string course: a band with a drip on top
export const tdCourse = (A, R, y, sides, m = "td_cast_stone", p = 0.07) => tdCornice(A, R, y, sides, { m, prof: [[0.13, p], [0.05, p + 0.03]], lod: true });
// Raised parapet panel centred at a (width w) on face F whose parapet is t thick with its coping underside at yt:
// "step" two tiers, "block" one, "pediment" a gable; each tier with its own coping. Masonry: these keep colliders.
export function tdCrest(A, F, a, w, yt, t, { m, kind = "step", rise = 0.9, cap = "td_cast_stone" } = {}) {
  const tier = (a0, a1, yb, y1, e) => { fbox(A, F, a0, a1, yb, y1, -t - e, e, m, { bv: 0.01 }); fbox(A, F, a0 - 0.05, a1 + 0.05, y1, y1 + 0.08, -t - e - 0.05, e + 0.05, cap, { k: 0, bv: 0.012 }); };
  if (kind === "pediment") {
    const yb = yt + 0.25, [d0, d1] = [F.d - F.out * (t + 0.05), F.d + F.out * 0.05].sort((p, q) => p - q);
    tier(a - w / 2, a + w / 2, yt - 0.25, yb, 0.05);
    A.prism(F.axis === "x" ? 2 : 0, d0, d1, [[a - w / 2, yb + 0.06], [a + w / 2, yb + 0.06], [a, yb + rise]], m);
    for (const s of [-1, 1]) tdBar(A, tdP(F, a + s * (w / 2 + 0.06), yb + 0.1, -t / 2), tdP(F, a, yb + rise + 0.06, -t / 2), t + 0.2, 0.08, cap, { k: 0 });
  } else {
    tier(a - w / 2, a + w / 2, yt - 0.25, yt + (kind === "step" ? rise * 0.5 : rise), 0.05);
    if (kind === "step") tier(a - w * 0.28, a + w * 0.28, yt + rise * 0.5 + 0.06, yt + rise, 0.07);
  }
}
// corner piers rising `rise` over the coping underside yt, from the roof deck yd; corners as tdCorners gives them
export function tdPiers(A, R, yd, yt, t, m, { corners = tdCorners(R), rise = 0.45, w = 0.7, cap = "td_cast_stone" } = {}) {
  for (const [x, z, sx, sz] of corners) {
    const inner = [[Math.min(x - sx * w, x - sx * (t + 0.01)), yd + 0.2, Math.min(z - sz * w, z - sz * (t + 0.01))], [Math.max(x - sx * w, x - sx * (t + 0.01)), yt, Math.max(z - sz * w, z - sz * (t + 0.01))]];
    if (!tdFree(A, [inner])) continue;   // a roof bulkhead or plant stands in the corner
    A.box([Math.min(x - sx * w, x + sx * 0.05), yd - 0.02, Math.min(z - sz * w, z + sz * 0.05)], [Math.max(x - sx * w, x + sx * 0.05), yt + rise, Math.max(z - sz * w, z + sz * 0.05)], m, { bv: 0.012 });
    A.box([Math.min(x - sx * (w + 0.05), x + sx * 0.1), yt + rise, Math.min(z - sz * (w + 0.05), z + sz * 0.1)], [Math.max(x - sx * (w + 0.05), x + sx * 0.1), yt + rise + 0.09, Math.max(z - sz * (w + 0.05), z + sz * 0.1)], cap, { k: 0, bv: 0.015 });
  }
}
// corners of R as [x, z, sx, sz]: which = any of "fe", "fw", "re", "rw" (front / rear, east / west)
export const tdCorners = (R, which = ["fe", "fw", "re", "rw"]) => which.map((c) => [c[1] === "e" ? R.x1 : R.x0, c[0] === "f" ? R.z1 : R.z0, c[1] === "e" ? 1 : -1, c[0] === "f" ? 1 : -1]);
// quoins: alternating long and short blocks round each corner, y0..y1, 35 mm proud; blocks that would hit a part or an
// opening are left out
export function tdQuoins(A, R, y0, y1, m, { corners = tdCorners(R), h = 0.3, gap = 0.05, lo = 0.6, sh = 0.34, p = 0.035 } = {}) {
  for (const [x, z, sx, sz] of corners) for (let y = y0, i = 0; y + h <= y1 + 1e-6; y += h + gap, i++) {
    const lx = i % 2 ? sh : lo, lz = i % 2 ? lo : sh;
    const X0 = Math.min(x - sx * lx, x + sx * p), X1 = Math.max(x - sx * lx, x + sx * p), Z0 = Math.min(z - sz * lz, z + sz * p), Z1 = Math.max(z - sz * lz, z + sz * p);
    const rx = [[X0, y, Math.min(z + sz * 0.003, z + sz * p)], [X1, y + h, Math.max(z + sz * 0.003, z + sz * p)]];
    const rz = [[Math.min(x + sx * 0.003, x + sx * p), y, Z0], [Math.max(x + sx * 0.003, x + sx * p), y + h, Z1]];
    if (tdFree(A, [rx, rz], [[X0, y, Z0], [X1, y + h, Z1]])) A.box([X0, y, Z0], [X1, y + h, Z1], m, { k: 0, lod: 1, bv: 0.008 });
  }
}
// flat pilaster strip on face F at a (w wide) from y0 to y1 with a plinth block and a capital
export function tdPilaster(A, F, a, y0, y1, m, { w = 0.5, p = 0.08, cap = "td_cast_stone", ignore = [] } = {}) {
  if (tdRuns(A, F, a - w / 2 - 0.05, a + w / 2 + 0.05, y0, y1, -0.01, p + 0.05, { min: w + 0.1 - 1e-3, ignore }).length !== 1) return false;
  fbox(A, F, a - w / 2 - 0.04, a + w / 2 + 0.04, y0, y0 + 0.35, -0.01, p + 0.03, cap, { k: 0, bv: 0.01 });
  fbox(A, F, a - w / 2, a + w / 2, y0 + 0.35, y1 - 0.25, -0.01, p, m, { k: 0, bv: 0.01 });
  fbox(A, F, a - w / 2 - 0.05, a + w / 2 + 0.05, y1 - 0.25, y1, -0.01, p + 0.05, cap, { k: 0, bv: 0.01 });
  return true;
}

// Steel fire escape on face F: a landing at every deck level in ys (lowest first) over a0..a1, dep deep, a straight flight
// between consecutive landings (from the a0 end up to the a1 end, in the outer half through a hatch in the landing
// above), a counterweighted drop ladder under the lowest landing and a ladder from the top landing to the parapet top
// yRoof. Returns false (and builds nothing) when its volume is not free.
export function tdFireEscape(A, F, a0, a1, ys, { dep = 1.25, yRoof = null, m = "td_fe_steel", deck = "td_fe_walk_grid", lite = false } = {}) {
  const yTop = (yRoof ?? ys.at(-1)) + 1.0, env = tdWB(F, a0 - 0.1, a1 + 0.1, ys[0] - 2.6, yTop, 0.09, dep + 0.1);
  if (!tdFree(A, [env])) return false;
  const Q = (a, y, e) => tdP(F, a, y, e), hm = dep / 2, hatch = 0.75;
  ys.forEach((y, i) => {
    const full = i === 0;   // the lowest landing has no flight coming up through it
    fbox(A, F, a0, a1, y - 0.04, y, 0.04, full ? dep - 0.02 : hm, deck, TDK);
    if (!full) for (const [p0, p1] of [[a0, a0 + hatch], [a1 - hatch, a1]]) fbox(A, F, p0, p1, y - 0.04, y, hm, dep - 0.02, deck, TDK);
    fbox(A, F, a0, a1, y - 0.16, y + 0.012, dep - 0.07, dep, m, TDK);                         // fascia channel
    if (!lite) fbox(A, F, a0, a1, y - 0.16, y + 0.012, -0.01, 0.06, m, TDL);                  // wall angle
    if (!lite) for (const a of [a0, a1]) fbox(A, F, a - (a === a0 ? 0 : 0.06), a + (a === a0 ? 0.06 : 0), y - 0.16, y + 0.012, 0.06, dep - 0.07, m, TDL);
    for (const a of [a0 + 0.9, a1 - 0.9]) rod(A, Q(a, y - 1.0, 0.03), Q(a, y - 0.14, dep - 0.12), 0.05, m, TDL);   // brackets
    // guard: posts, top and mid rails along the front and both ends
    const posts = lite ? 2 : Math.max(2, Math.round((a1 - a0) / 1.5) + 1);
    for (let k = 0; k < posts; k++) { const a = a0 + 0.03 + ((a1 - a0 - 0.06) * k) / (posts - 1); fbox(A, F, a - 0.02, a + 0.02, y, y + 1.0, dep - 0.06, dep - 0.02, m, TDL); }
    for (const yy of lite ? [y + 0.96] : [y + 0.96, y + 0.5]) {
      fbox(A, F, a0, a1, yy, yy + 0.04, dep - 0.065, dep - 0.015, m, yy > y + 0.9 ? TDK : TDL);
      for (const a of [a0 + 0.02, a1 - 0.02]) fbox(A, F, a - 0.02, a + 0.02, yy, yy + 0.04, 0.03, dep - 0.065, m, TDL);
    }
    if (i === 0) return;
    // flight from the landing below (a0 end) to this one (a1 end)
    const yl = ys[i - 1], s0 = a0 + hatch, s1 = a1 - hatch, n = Math.max(4, Math.round((y - yl) / (lite ? 0.3 : 0.22)));
    for (const e of [hm + 0.04, dep - 0.12]) tdBar(A, Q(s0, yl - 0.1, e), Q(s1, y - 0.1, e), 0.04, 0.2, m, TDK);   // stringers
    if (lite) tdBar(A, Q(s0, yl - 0.03, (hm + dep - 0.08) / 2), Q(s1, y - 0.03, (hm + dep - 0.08) / 2), dep - hm - 0.2, 0.03, deck, TDK);   // one grated plate for the treads
    else for (let k = 1; k < n; k++) { const a = s0 + ((s1 - s0) * k) / n, yy = yl + ((y - yl) * k) / n; fbox(A, F, a - 0.11, a + 0.11, yy - 0.03, yy, hm + 0.06, dep - 0.14, deck, TDL); }
    tdBar(A, Q(s0, yl + 0.9, dep - 0.1), Q(s1, y + 0.9, dep - 0.1), 0.04, 0.04, m, TDL);   // handrail
  });
  // drop ladder (raised) under the lowest landing, its counterweight arm
  const y0 = ys[0], la = a1 - 0.5;
  for (const s of [-1, 1]) fbox(A, F, la + s * 0.22 - 0.02, la + s * 0.22 + 0.02, y0 - 2.3, y0 + 0.9, dep - 0.24, dep - 0.18, m, TDK);
  for (let yy = y0 - 2.15; yy < y0 - 0.1; yy += lite ? 0.6 : 0.3) fbox(A, F, la - 0.2, la + 0.2, yy, yy + 0.03, dep - 0.225, dep - 0.195, m, TDL);
  if (!lite) { tdBar(A, Q(la - 0.26, y0 + 0.3, dep - 0.21), Q(la - 0.9, y0 + 0.7, dep - 0.21), 0.05, 0.05, m, TDL); fbox(A, F, la - 1.0, la - 0.8, y0 + 0.55, y0 + 0.8, dep - 0.28, dep - 0.14, m, TDL); }
  // ladder from the top landing up past the parapet
  if (yRoof !== null) {
    const ya = ys.at(-1), ra = a0 + 0.4;
    for (const s of [-1, 1]) fbox(A, F, ra + s * 0.22 - 0.02, ra + s * 0.22 + 0.02, ya, yRoof + 1.0, 0.16, 0.22, m, TDK);
    for (let yy = ya + 0.3; yy < yRoof + 0.9; yy += lite ? 0.6 : 0.3) fbox(A, F, ra - 0.2, ra + 0.2, yy, yy + 0.03, 0.175, 0.205, m, TDL);
    for (const s of [-1, 1]) fbox(A, F, ra + s * 0.22 - 0.02, ra + s * 0.22 + 0.02, yRoof + 0.96, yRoof + 1.0, -0.4, 0.16, m, TDL);   // grab rails over the coping
  }
  return true;
}
// first free spot for a fire escape len long within lo..hi on F (tried from `from`: "lo" or "hi")
export function tdFireEscapeFit(A, F, lo, hi, len, ys, o = {}) {
  const n = Math.floor((hi - lo - len) / 0.5);
  for (let i = 0; i <= n; i++) { const a = o.from === "hi" ? hi - len - i * 0.5 : lo + i * 0.5; if (tdFireEscape(A, F, a, a + len, ys, o)) return a; }
  return null;
}

// fabric awning on face F over a0..a1 hanging from y: a steel wall rail, two arms and a front bar `dep` out and `drop`
// lower; sag lowers the a1 end of the front bar (a bent frame); panels ~1.2 m wide, each with a valance; torn: indices of
// panels missing (the cloth gone) and hang: of panels hanging loose off the wall rail
export function tdAwning(A, F, a0, a1, y, { dep = 1.3, drop = 0.55, m, frame = "td_fe_steel", sag = 0, torn = [], hang = [], val = 0.25 } = {}) {
  const yf = (a) => y - drop - sag * ((a - a0) / (a1 - a0)), Q = (a, yy, e) => tdP(F, a, yy, e);
  fbox(A, F, a0 - 0.03, a1 + 0.03, y - 0.06, y + 0.05, -0.01, 0.09, frame, { k: 0, bv: 0.008 });
  for (const a of [a0 + 0.04, a1 - 0.04]) rod(A, Q(a, y - 0.03, 0.08), Q(a, yf(a) + 0.02, dep), 0.035, frame, TDK);
  tdBar(A, Q(a0, yf(a0), dep), Q(a1, yf(a1), dep), 0.05, 0.05, frame, TDK);
  const n = Math.max(1, Math.round((a1 - a0) / 1.2)), pw = (a1 - a0) / n;
  for (let i = 0; i < n; i++) {
    const ac = a0 + (i + 0.5) * pw;
    if (torn.includes(i)) continue;
    if (hang.includes(i)) { tdBar(A, Q(ac, y - 0.05, 0.1), Q(ac + pw * 0.08, y - 1.05, dep * 0.28), pw * 0.8, 0.015, m, TDK); continue; }
    tdBar(A, Q(ac, y, 0.09), Q(ac, yf(ac) + 0.03, dep - 0.01), pw - 0.012, 0.02, m, TDK);
    fbox(A, F, ac - pw / 2 + 0.006, ac + pw / 2 - 0.006, yf(ac) - val, yf(ac), dep + 0.03, dep + 0.045, m, TDK);
  }
}
// security bars across an opening a0..a1, y0..y1 on face F, `e` out of the face: verticals every sp, two flat rails
export function tdBars(A, F, a0, a1, y0, y1, { sp = 0.16, e = 0.05, m = "td_fe_steel" } = {}) {
  for (const yy of [y0 + 0.12, y1 - 0.16]) fbox(A, F, a0 - 0.05, a1 + 0.05, yy, yy + 0.05, e - 0.012, e + 0.012, m, TDK);
  const n = Math.max(2, Math.round((a1 - a0) / sp));
  for (let i = 1; i < n; i++) { const a = a0 + ((a1 - a0) * i) / n; fbox(A, F, a - 0.012, a + 0.012, y0 + 0.02, y1 - 0.02, e - 0.03, e - 0.006, m, TDL); }
}
// roof water tank on a steel stand (timber staves, hoops, a conical lid) at x, z on the roof at y
export function tdTank(A, x, z, y, { d = 2.2, h = 2.4, leg = 1.6 } = {}) {
  const r = d / 2 - 0.15;
  for (const [ox, oz] of [[-1, -1], [1, -1], [1, 1], [-1, 1]]) A.box([x + ox * r - 0.07, y, z + oz * r - 0.07], [x + ox * r + 0.07, y + leg, z + oz * r + 0.07], "td_fe_steel", { bv: 0.01 });
  for (const s of [-1, 1]) rod(A, [x - r, y + 0.2, z + s * r], [x + r, y + leg - 0.2, z + s * r], 0.05, "td_fe_steel", TDL);   // cross braces
  A.box([x - d / 2 - 0.05, y + leg, z - d / 2 - 0.05], [x + d / 2 + 0.05, y + leg + 0.12, z + d / 2 + 0.05], "td_fe_steel", { bv: 0.01 });
  A.cyl([x, y + leg + 0.12 + h / 2, z], [d, h, d], "td_tank_wood", { seg: 18 });
  for (let k = 0; k < 4; k++) A.cyl([x, y + leg + 0.35 + k * (h - 0.5) / 3, z], [d + 0.04, 0.05, d + 0.04], "td_fe_steel", { seg: 18, ...TDL });
  A.cone([x, y + leg + 0.12 + h + 0.3, z], [d + 0.2, 0.6, d + 0.2], "td_tank_wood", { seg: 18, top: 0.06, k: 0 });
  A.cyl([x, y + leg + h + 0.78, z], [0.12, 0.2, 0.12], "td_fe_steel", { seg: 8, ...TDL });
  for (const s of [-1, 1]) A.box([x + d / 2 + 0.02, y, z + s * 0.2 - 0.02], [x + d / 2 + 0.06, y + leg + h, z + s * 0.2 + 0.02], "td_fe_steel", TDL);   // ladder rails
}
// packaged condenser on rails: casing, fan grille on top, side coil grille
export function tdAC(A, x, z, y, { w = 1.1, d = 0.8, h = 0.9 } = {}) {
  for (const s of [-1, 1]) A.box([x - w / 2 - 0.05, y, z + s * d * 0.3 - 0.04], [x + w / 2 + 0.05, y + 0.1, z + s * d * 0.3 + 0.04], "td_fe_steel", TDL);
  A.box([x - w / 2, y + 0.1, z - d / 2], [x + w / 2, y + 0.1 + h, z + d / 2], "td_ac", { bv: 0.02 });
  A.cyl([x, y + 0.1 + h + 0.02, z], [Math.min(w, d) * 0.85, 0.05, Math.min(w, d) * 0.85], "td_ac_grille", { seg: 16, ...TDL });
  A.box([x - w / 2 + 0.08, y + 0.2, z + d / 2], [x + w / 2 - 0.08, y + h, z + d / 2 + 0.012], "td_ac_grille", TDL);
}
// first free roof spot of half size r (height hh) in x0..x1, z0..z1 on the roof at y
export function tdSpot(A, x0, x1, z0, z1, y, r, hh = 2.0, step = 0.6) {
  for (let z = z0 + r; z <= z1 - r; z += step) for (let x = x0 + r; x <= x1 - r; x += step) if (tdFree(A, [[[x - r, y + 0.03, z - r], [x + r, y + hh, z + r]]])) return [x, z];
  return null;
}
// roof clutter in x0..x1, z0..z1 at y: kinds "tank", "ac", "vent" placed in the first free spots
export function tdRoof(A, x0, x1, z0, z1, y, kinds) {
  for (const k of kinds) {
    const r = k === "tank" ? 1.25 : k === "ac" ? 0.65 : 0.25, p = tdSpot(A, x0, x1, z0, z1, y, r, k === "tank" ? 4.8 : 1.3);
    if (!p) continue;
    if (k === "tank") tdTank(A, p[0], p[1], y);
    else if (k === "ac") tdAC(A, p[0], p[1], y);
    else { A.cyl([p[0], y + 0.45, p[1]], [0.16, 0.9, 0.16], "td_downpipe", { seg: 10, k: 0 }); A.cone([p[0], y + 0.98, p[1]], [0.3, 0.16, 0.3], "td_downpipe", { seg: 10, top: 0.2, ...TDL }); }
  }
}
// The full town pass on a flat-roofed box R: walls to yW (roof deck), parapet of thickness t with its coping underside at
// yt; o: m (cornice), sides (dressed faces), crest {side, a, w, kind, rise, m}, piers (mat), quoin (mat) from yq0,
// courses: [y...], fe: {side, lo, hi, len, ys, from}, roof: [kinds] (on the deck yd).
export function tdTown(A, R, o) {
  tdMats(A);
  const sides = o.sides ?? ["front", "rear", "east", "west"];
  if (o.fe) { const f = o.fe, F = tdFace(R, f.side); tdFireEscapeFit(A, F, f.lo, f.hi, f.len, f.ys, { yRoof: o.yt + 0.05, from: f.from, dep: f.dep, lite: f.lite }); }
  if (o.roof) tdRoof(A, R.x0 + (o.t ?? 0.3) + 0.3, R.x1 - (o.t ?? 0.3) - 0.3, R.z0 + (o.t ?? 0.3) + 0.3, R.z1 - (o.t ?? 0.3) - 0.3, o.yd, o.roof);
  for (const y of o.courses ?? []) tdCourse(A, R, y, o.courseSides ?? sides, o.courseM ?? o.m);
  if (o.cornice !== false) tdCornice(A, R, o.yt - 0.012, o.corniceSides ?? sides, { m: o.m ?? "td_cast_stone", prof: o.prof });
  for (const c of [o.crest ?? []].flat()) tdCrest(A, tdFace(R, c.side ?? "front"), c.a ?? 0, c.w, o.yt, o.t ?? 0.3, { m: c.m ?? o.wall, kind: c.kind, rise: c.rise, cap: o.m ?? "td_cast_stone" });
  if (o.piers) tdPiers(A, R, o.yd, o.yt, o.t ?? 0.3, o.piers, { corners: o.pierCorners ? tdCorners(R, o.pierCorners) : undefined, cap: o.m ?? "td_cast_stone" });
  if (o.quoin) tdQuoins(A, R, o.yq0 ?? 0.35, o.yq1 ?? o.yW, o.quoin, { corners: o.quoinCorners ? tdCorners(R, o.quoinCorners) : undefined });
}

// NT2b: merge axis-aligned boxes that tile exactly (same material, flags and cross-section, touching end to end) into
// one part: the look is unchanged, the part count drops. In place (A._target keeps pointing at A.parts).
const k4 = (v) => Math.round(v * 1e4);
export function tdMerge(A) {
  let list = A.parts, changed = true;
  while (changed) {
    changed = false;
    for (let a = 0; a < 3; a++) {
      const groups = new Map(), out = [];
      for (const p of list) {
        if (p.t !== "b" || p.r || p.q) { out.push(p); continue; }
        const key = [p.m, p.k ?? 1, p.v ?? 1, p.lod ?? 0, p.bv ?? 0, ...[0, 1, 2].filter((i) => i !== a).flatMap((i) => [k4(p.c[i] - p.s[i] / 2), k4(p.c[i] + p.s[i] / 2)])].join("|");
        (groups.get(key) ?? groups.set(key, []).get(key)).push(p);
      }
      for (const g of groups.values()) {
        g.sort((p, q) => p.c[a] - p.s[a] / 2 - (q.c[a] - q.s[a] / 2));
        let cur = g[0];
        for (let i = 1; i < g.length; i++) {
          const q = g[i], lo = cur.c[a] - cur.s[a] / 2, hi = q.c[a] + q.s[a] / 2;
          if (Math.abs(cur.c[a] + cur.s[a] / 2 - (q.c[a] - q.s[a] / 2)) < 2e-4) {
            cur = { ...cur, c: [...cur.c], s: [...cur.s] };
            cur.c[a] = Math.round(((lo + hi) / 2) * 1e4) / 1e4; cur.s[a] = Math.round((hi - lo) * 1e4) / 1e4; changed = true;
          } else { out.push(cur); cur = q; }
        }
        out.push(cur);
      }
      list = out;
    }
  }
  A.parts.length = 0; A.parts.push(...list);
  return A;
}
// NT2b: open-riser stairs (drops the thin riser boards stairs() puts under each tread): a lighter modern stair
export function tdOpenRisers(A) {
  const keep = A.parts.filter((p) => !(p.t === "b" && p.m === "oak_x" && p.k === 0 && !p.r && p.s[1] < 0.2 && Math.abs(Math.min(p.s[0], p.s[2]) - 0.02) < 0.002));
  A.parts.length = 0; A.parts.push(...keep);
  return A;
}

// ============================================================================ catalog
export function modernBuildingsA() {
  const out = [familyHouse(), townhouseRow(), apartmentBlock(), bungalow(), mixedUse(), dinerSmall(), restaurantFamily(), restaurantLarge(), fastFood(), gasStation(), busStation()];
  for (const A of out) { as1Run(A); tdMerge(A); }   // AS1: detail queued on the outer asset (garage roofs); NT2b merge
  const ids = new Set();
  for (const A of out) {   // self-check: unique mod_ ids, unique nav room ids, every room typed, every door interactable framed
    const id = A.meta.id;
    if (!id.startsWith("mod_bld_") || ids.has(id)) throw new Error(`bad or duplicate id ${id}`);
    ids.add(id);
    const rooms = A.meta.nav.rooms.map((r) => r.id);
    if (new Set(rooms).size !== rooms.length) throw new Error(`${id}: duplicate nav room ids`);
    for (const r of rooms) if (!A.meta.roomTypes[r]) throw new Error(`${id}: room ${r} untyped`);
    for (const d of A.meta.nav.doors) for (const r of [d.a, d.b]) if (r !== "outside" && !rooms.includes(r)) throw new Error(`${id}: door to unknown room ${r}`);
    const iid = A.interactables.map((i) => i.id);
    if (new Set(iid).size !== iid.length) throw new Error(`${id}: duplicate interactable ids`);
  }
  return out;
}
