// MOD1 batch "newtown C" (docs/modern-assets-brief.md): small modern loot homes, a construction site, a lakeside /
// drainage-channel set. Written to content/assets/<id>.json by tools/build_mpu_assets.mjs (integrator; never run
// build_assets.mjs). Footprints are fixed: a map generator places these by footprint.
//
// Homes use the B3 house builder (tools/catalog_chateau.mjs house(): two-layer walls with real openings, door and
// casement interactables, two stair cores off a rear corridor, nav, tour) with the modern finishes of
// catalog_modern_buildings_a.mjs (its helpers are not exported, so the few needed are copied here). Bespoke buildings
// (barn, site office, pump station, boathouse) are box shells with parts.mjs wall() / door() / window(). The
// construction frames carry nav rooms but no floor-material slabs, so the interior system finds no rooms there and
// never furnishes or finishes them. Walkable props (pier, pontoon, bridges, channel steps) are non-enterable with
// effect "block" (the navmesh walks their colliders); the pier and pontoon also carry nav rooms.
import { Asset, wbox, cutRect, P } from "./kit.mjs";
import { wall, partition, door, window, stairs, railing, fbox, fpart, hinged, aabb, chimney } from "./parts.mjs";
import { mats, face, house, meta } from "./catalog_chateau.mjs";
import { cover, room, corners, fire, setNav, setTour } from "./catalog_atlantic.mjs";
import { rod } from "./defence_detail.mjs";

const GF = 0.3, L1 = { k: 0, lod: 1 }, K0 = { k: 0 }, HID = { v: 0 }, BV = { bv: 0.012 };
const SIDES = ["front", "rear", "east", "west"];
const DEG = 180 / Math.PI;
const q4 = (v) => Math.round(v * 1e4) / 1e4;

// ============================================================================ materials
// The house kit's names keep their roles (interior.cpp reads floor_x / terracotta / tread_x / concrete_floor / plank_x
// by name). "painted" is the painted-timber plank shader: only on painted wood. Painted steel uses "metal".
function ntMats(A) {
  mats(A);
  const re = (n, o) => Object.assign(A.materials.find((m) => m.name === n), o);
  re("stone_dressed", { albedo: [0.60, 0.59, 0.56], roughness: 0.85, pattern: "concrete", texture: "concrete_light" });
  re("stone_plinth", { albedo: [0.42, 0.41, 0.39], roughness: 0.9, pattern: "concrete", texture: "concrete_aged" });
  for (const n of ["oak_x", "oak_y", "oak_z"]) re(n, { albedo: [0.80, 0.79, 0.76], roughness: 0.5, pattern: "painted", texture: "paint_wood_white" });
  re("window_paint", { albedo: [0.075, 0.08, 0.085], roughness: 0.4, pattern: "metal", metallic: 0.3 });
  re("door_paint", { albedo: [0.10, 0.11, 0.12], roughness: 0.45, pattern: "metal" });
  for (const n of ["floor_x", "landing_x", "tread_x"]) re(n, { albedo: [0.46, 0.35, 0.23], roughness: 0.45, texture: "wood_oak" });
  re("terracotta", { albedo: [0.56, 0.55, 0.53], roughness: 0.45, texture: "floor_cement_tile" });
  re("plaster", { albedo: [0.80, 0.79, 0.76], texture: "plaster_painted" });
  re("iron", { albedo: [0.05, 0.05, 0.05] });
  re("zinc", { texture: "steel_scratched" });
  re("concrete_floor", { albedo: [0.45, 0.44, 0.42], texture: "concrete_light" });
  re("plank_x", { albedo: [0.42, 0.33, 0.23], roughness: 0.75, texture: "planks_038" });
  for (const [n, g] of [["trim_x", 0], ["trim_y", 1], ["trim_z", 2]]) A.mat(n, [0.82, 0.81, 0.78], 0.5, "painted", { texture: "paint_wood_white", grain: g });
  A.mat("render_m", [0.80, 0.79, 0.76], 0.9, "plaster", { texture: "render_lime" });
  A.mat("render_grey", [0.33, 0.34, 0.35], 0.9, "plaster", { texture: "render_lime" });
  A.mat("render_dk", [0.22, 0.23, 0.24], 0.9, "plaster", { texture: "render_lime" });
  A.mat("render_warm", [0.76, 0.70, 0.58], 0.9, "plaster", { texture: "render_lime" });
  A.mat("brick_slip", [0.46, 0.30, 0.22], 0.85, "brick", { texture: "brick_muted" });
  A.mat("brick_grey", [0.42, 0.40, 0.38], 0.85, "brick", { texture: "brick_factory" });
  A.mat("clad_wood", [0.44, 0.30, 0.18], 0.7, "wood", { texture: "wood_dark", grain: 1 });
  A.mat("clad_light", [0.62, 0.48, 0.33], 0.7, "wood", { texture: "wood_oak", grain: 1 });
  A.mat("clad_board", [0.42, 0.37, 0.31], 0.85, "wood", { texture: "boards_weathered", grain: 1 });
  A.mat("metal_panel", [0.20, 0.21, 0.22], 0.45, "metal", { metallic: 0.4 });
  A.mat("alu", [0.62, 0.63, 0.64], 0.35, "metal", { texture: "steel_scratched", metallic: 0.9 });
  A.mat("stainless", [0.70, 0.70, 0.70], 0.25, "metal", { texture: "steel_scratched", metallic: 1.0 });
  A.mat("hvac", [0.66, 0.66, 0.64], 0.5, "metal", { metallic: 0.2 });
  A.mat("grille", [0.28, 0.28, 0.28], 0.5, "metal", { texture: "steel_scratched", metallic: 0.6 });
  A.mat("membrane", [0.12, 0.12, 0.12], 0.9, "concrete", { texture: "concrete_rough" });
  A.mat("gravel_roof", [0.42, 0.41, 0.39], 0.95, "stone", { texture: "gravel_fine" });
  A.mat("concrete_m", [0.55, 0.54, 0.52], 0.88, "concrete", { texture: "concrete_formwork" });
  A.mat("paving", [0.47, 0.46, 0.44], 0.9, "stone", { texture: "paving_worn" });
  A.mat("asphalt", [0.11, 0.11, 0.11], 0.92, "concrete", { texture: "concrete_aggregate" });
  A.mat("deck", [0.40, 0.28, 0.17], 0.75, "wood", { texture: "planks_038", grain: 0 });
  A.mat("glass_rail", [0.70, 0.80, 0.80], 0.05, "glass");
  A.mat("glass_dark", [0.04, 0.05, 0.06], 0.1, "glass");
  A.mat("rubber", [0.04, 0.04, 0.04], 0.9, "plain");
  A.mat("roof_m", [0.16, 0.17, 0.18], 0.7, "slate", { texture: "slate_square", grain: 2 });
  A.mat("roof_seam", [0.13, 0.14, 0.15], 0.45, "metal", { texture: "steel_scratched", metallic: 0.5 });
  A.mat("sign_w", [0.85, 0.85, 0.82], 0.5, "plain");
  A.mat("light_panel", [0.90, 0.90, 0.86], 0.3, "plain", { emissive: [2.4, 2.35, 2.2] });
  A.mat("screen", [0.05, 0.08, 0.10], 0.2, "glass", { emissive: [0.25, 0.35, 0.45] });
  A.mat("line_yellow", [0.80, 0.62, 0.05], 0.6, "plain");
  A.mat("line_white", [0.85, 0.85, 0.82], 0.6, "plain");
  // newtown C
  A.mat("steel_frame", [0.17, 0.26, 0.21], 0.5, "metal", { metallic: 0.3 });     // painted steel portal frames
  A.mat("steel_grey", [0.30, 0.32, 0.34], 0.5, "metal", { metallic: 0.3 });
  A.mat("galv", [0.62, 0.64, 0.65], 0.5, "metal", { texture: "steel_scratched", metallic: 0.55 });
  A.mat("corr_green", [0.20, 0.31, 0.24], 0.6, "metal", { texture: "corrugated_galv", metallic: 0.2 });
  A.mat("corr_grey", [0.52, 0.54, 0.55], 0.55, "metal", { texture: "corrugated_galv", metallic: 0.4 });
  A.mat("corr_clear", [0.78, 0.80, 0.74], 0.3, "glass");
  A.mat("conc_panel", [0.56, 0.55, 0.52], 0.9, "concrete", { texture: "concrete_aged" });
  A.mat("slab", [0.58, 0.57, 0.54], 0.9, "concrete", { texture: "concrete_formwork" });       // not a floor name: frames stay unfurnished
  A.mat("slab_top", [0.50, 0.49, 0.46], 0.92, "concrete", { texture: "concrete_rough" });
  A.mat("stair_conc", [0.57, 0.56, 0.53], 0.85, "concrete", { texture: "concrete_light" });
  A.mat("cmu", [0.63, 0.62, 0.59], 0.95, "concrete", { texture: "concrete_aggregate" });
  A.mat("rebar", [0.42, 0.27, 0.18], 0.8, "metal", { texture: "steel_rust_light", metallic: 0.3 });   // site steel weathers
  A.mat("ply", [0.66, 0.55, 0.40], 0.9, "wood", { texture: "planks_raw" });
  A.mat("form_red", [0.36, 0.16, 0.09], 0.6, "wood", { texture: "planks_raw" });                     // phenolic formwork ply
  A.mat("prop_steel", [0.80, 0.42, 0.07], 0.55, "metal", { metallic: 0.2 });
  A.mat("scaf_board", [0.60, 0.50, 0.37], 0.85, "wood", { texture: "planks_038" });
  A.mat("hay", [0.62, 0.52, 0.28], 0.95, "fabric", { texture: "straw_thatch" });
  A.mat("hay_wrap", [0.07, 0.08, 0.07], 0.45, "fabric", { texture: "cloth_grey" });
  A.mat("deck_comp", [0.34, 0.32, 0.30], 0.8, "wood", { texture: "planks_038", grain: 0 });
  A.mat("pile", [0.30, 0.24, 0.18], 0.9, "wood", { texture: "beam_weathered", grain: 1 });
  A.mat("hull_w", [0.86, 0.86, 0.84], 0.25, "metal", { metallic: 0.05 });                         // gelcoat
  A.mat("hull_blue", [0.06, 0.14, 0.32], 0.25, "metal", { metallic: 0.05 });
  A.mat("antifoul", [0.32, 0.07, 0.05], 0.8, "concrete", { texture: "concrete_light" });
  A.mat("deck_grip", [0.78, 0.77, 0.73], 0.8, "concrete", { texture: "concrete_light" });
  A.mat("vinyl_w", [0.80, 0.78, 0.72], 0.6, "fabric", { texture: "leather_brown" });
  A.mat("motor", [0.06, 0.06, 0.065], 0.35, "metal", { metallic: 0.2 });
  A.mat("safety_yel", [0.85, 0.65, 0.06], 0.55, "metal");
  A.mat("hazard_blk", [0.05, 0.05, 0.05], 0.6, "metal");
  A.mat("crane_yel", [0.85, 0.60, 0.05], 0.5, "metal", { metallic: 0.2 });
  A.mat("sheetpile", [0.36, 0.26, 0.18], 0.75, "metal", { texture: "steel_rust_light", metallic: 0.4 });   // deliberately weathered
  A.mat("pipe_blue", [0.10, 0.22, 0.42], 0.5, "metal", { metallic: 0.2 });
  A.mat("pump_green", [0.12, 0.30, 0.20], 0.5, "metal", { metallic: 0.2 });
  A.mat("panel_cab", [0.72, 0.73, 0.72], 0.45, "metal", { metallic: 0.2 });
  A.mat("cabin_w", [0.80, 0.81, 0.80], 0.5, "metal", { texture: "corrugated_galv", metallic: 0.1 });
  A.mat("kerb", [0.60, 0.59, 0.56], 0.85, "concrete", { texture: "concrete_light" });
  A.mat("float_hdpe", [0.16, 0.17, 0.18], 0.7, "concrete", { texture: "concrete_light" });
  A.mat("lamp_red", [0.7, 0.04, 0.03], 0.3, "plain", { emissive: [3.0, 0.1, 0.05] });
  A.mat("lamp_green", [0.04, 0.6, 0.1], 0.3, "plain", { emissive: [0.1, 2.5, 0.3] });
  A.mat("buoy", [0.88, 0.32, 0.05], 0.5, "plain");
}
function mmeta(id, name, description, floors, footprint) {
  const A = meta(id, name, "Modern", description, floors, footprint, { version: 1, plinthDepth: 0, noAutoPanels: true, maxGrade: 0.5 });
  ntMats(A);
  return A;
}
const sub = (id) => { const B = new Asset({ id, version: 1, name: id, category: "Modern", floors: 1, enterable: true, footprint: [1, 1] }); ntMats(B); return B; };
function nt(id, name, category, description, footprint, { floors = 1, enterable = true, maxGrade = 0.5 } = {}) {
  const A = new Asset({ id, version: 1, name, category, description, footprint, floors, enterable, plinthDepth: 0, maxGrade, noAutoPanels: true });
  ntMats(A);
  return A;
}
const nav0 = (effect = "block") => ({ effect, rooms: [], doors: [], entrances: [], corners: [], firingPoints: [], cover: [], areas: [] });
function addRoom(N, id, x0, y, z0, x1, top, z1) {
  N.rooms.push(room(id, [x0, y, z0], [x1, top, z1]));
  N.corners.push(...corners(id, x0, x1, z0, z1, y, Math.min(0.45, (x1 - x0) / 4, (z1 - z0) / 4)));
}
const ndoor = (N, a, b, p) => N.doors.push({ a, b, p: p.map(q4) });
const nent = (N, p, dir, r) => N.entrances.push({ p: p.map(q4), dir, room: r });
const tourAt = (A, x, z, steps) => setTour(A, { pos: [x, 0, z], yaw: 0 }, steps);

// ============================================================================ sub-building placement (copy of catalog_modern_buildings_a.mjs placeSub)
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

// ============================================================================ house() fixes and facade detail (copies of catalog_modern_buildings_a.mjs)
function noBeams(A, EAVE) {
  const keep = A.parts.filter((p) => !(p.m === "oak_z" && p.k === 0 && Math.abs(p.c[1] - (EAVE - 0.28)) < 0.01 && p.s[0] < 0.15));
  A.parts.length = 0; A.parts.push(...keep);
}
function noRail(A, k) {
  const ys = k.levels.map((l) => l.y), rail = (p) => p.m?.startsWith("trim_") && p.s[1] < 0.2 && ys.some((y) => Math.abs(p.c[1] - y - 0.95) < 0.08);
  const strip = (L) => { const keep = L.filter((p) => !rail(p)); L.length = 0; L.push(...keep); };
  strip(A.parts); for (const pn of A.panels) pn.states.forEach(strip);
}
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
const modHouse = (A, c) => { const k = house(A, c); noRail(A, k); return k; };
const HALLS = [/(^|_)c\d$/, "hall"];

function flatRoof(A, x0, x1, z0, z1, y, { ph = 0.9, t = 0.25, m = "render_m", vents = [] } = {}) {
  A.box([x0, y, z0], [x1, y + 0.12, z1], "membrane");
  A.box([x0 + t, y + 0.12, z0 + t], [x1 - t, y + 0.16, z1 - t], "gravel_roof");
  const yt = y + ph;
  A.box([x0, y, z1 - t], [x1, yt, z1], m, BV); A.box([x0, y, z0], [x1, yt, z0 + t], m, BV);
  A.box([x0, y, z0 + t], [x0 + t, yt, z1 - t], m, BV); A.box([x1 - t, y, z0 + t], [x1, yt, z1 - t], m, BV);
  const c = { k: 0, bv: 0.01 };
  A.box([x0 - 0.04, yt, z1 - t - 0.03], [x1 + 0.04, yt + 0.05, z1 + 0.04], "alu", c); A.box([x0 - 0.04, yt, z0 - 0.04], [x1 + 0.04, yt + 0.05, z0 + t + 0.03], "alu", c);
  A.box([x0 - 0.04, yt, z0 + t + 0.03], [x0 + t + 0.03, yt + 0.05, z1 - t - 0.03], "alu", c); A.box([x1 - t - 0.03, yt, z0 + t + 0.03], [x1 + 0.04, yt + 0.05, z1 - t - 0.03], "alu", c);
  A.box([x0 + t, y + 0.12, z0 + t], [x1 - t, y + 0.3, z0 + t + 0.03], "membrane", L1);
  A.box([x0 + t, y + 0.12, z1 - t - 0.03], [x1 - t, y + 0.3, z1 - t], "membrane", L1);
  for (const [x, z] of [[x0 + t + 0.25, z0 + t + 0.25], [x1 - t - 0.25, z0 + t + 0.25]]) A.cyl([x, y + 0.17, z], [0.22, 0.03, 0.22], "grille", { seg: 10, ...L1 });
  for (const [x, z] of vents) { A.cyl([x, y + 0.45, z], [0.11, 0.6, 0.11], "metal_panel", { seg: 10, k: 0 }); A.cyl([x, y + 0.77, z], [0.16, 0.06, 0.16], "metal_panel", { seg: 10, ...L1 }); }
  return yt;
}
function signPanel(A, F, a0, a1, y0, y1, m, { e = 0, depth = 0.12, frame = "alu" } = {}) {
  fbox(A, F, a0 - 0.04, a1 + 0.04, y0 - 0.04, y1 + 0.04, e, e + 0.05, frame, { k: 0, bv: 0.01 });
  fbox(A, F, a0, a1, y0, y1, e + 0.05, e + depth, m, { k: 0, bv: 0.008 });
}
function canopy(A, F, a0, a1, y, depth = 1.2, m = "metal_panel") {
  fbox(A, F, a0, a1, y, y + 0.16, 0, depth, m, { bv: 0.015 });
  fbox(A, F, a0 - 0.02, a1 + 0.02, y - 0.04, y + 0.2, depth - 0.04, depth + 0.02, "alu", { k: 0, bv: 0.008 });
  fbox(A, F, a0 + 0.2, a1 - 0.2, y - 0.012, y, depth * 0.4, depth * 0.6, "light_panel", L1);
  const Pf = (a, yy, e) => (F.axis === "x" ? [a, yy, F.d + F.out * e] : [F.d + F.out * e, yy, a]);
  for (const a of [a0 + 0.2, a1 - 0.2]) rod(A, Pf(a, y + 1.0, 0.02), Pf(a, y + 0.16, depth - 0.1), 0.03, "stainless", L1);
}
function glassBalcony(A, F, a0, a1, y, depth = 1.3) {
  fbox(A, F, a0, a1, y - 0.2, y, -0.02, depth, "concrete_m", { bv: 0.02 });
  fbox(A, F, a0 + 0.04, a1 - 0.04, y - 0.23, y - 0.2, depth - 0.08, depth - 0.05, "concrete_m", L1);
  fbox(A, F, a0 + 0.05, a1 - 0.05, y + 0.06, y + 1.02, depth - 0.07, depth - 0.05, "glass_rail", { k: 0 });
  fbox(A, F, a0, a1, y + 1.0, y + 1.06, depth - 0.09, depth - 0.03, "alu", { k: 0, bv: 0.008 });
  fbox(A, F, a0, a1, y, y + 1.06, depth - 0.09, depth - 0.03, "alu", HID);
  for (const a of [a0, a1 - 0.02]) {
    fbox(A, F, a, a + 0.02, y + 0.06, y + 1.0, 0.03, depth - 0.09, "glass_rail", { k: 0 });
    fbox(A, F, a, a + 0.02, y, y + 1.06, 0, depth - 0.09, "alu", HID);
  }
  for (const a of [a0 + 0.25, a1 - 0.25]) fbox(A, F, a - 0.03, a + 0.03, y + 0.02, y + 0.08, depth - 0.1, depth - 0.02, "stainless", L1);
}
function dpipe(A, F, a, y0, y1) {
  fpart(A, "cyl", F, a, (y0 + y1) / 2, 0.08, [0.09, y1 - y0, 0.09], "metal_panel", { k: 0 });
  for (let y = y0 + 0.8; y < y1 - 0.3; y += 1.8) fbox(A, F, a - 0.06, a + 0.06, y - 0.02, y + 0.02, 0, 0.13, "metal_panel", L1);
  fpart(A, "wedge", F, a, y0 + 0.05, 0.16, [0.09, 0.1, 0.16], "metal_panel", L1);
}
// heat-pump unit on wall brackets (face F at a, sitting at y)
function heatPump(A, F, a, y) {
  fbox(A, F, a - 0.45, a + 0.45, y, y + 0.7, 0.05, 0.38, "hvac", { k: 0, bv: 0.015 });
  fbox(A, F, a - 0.4, a + 0.1, y + 0.08, y + 0.62, 0.38, 0.39, "grille", L1);
  for (const s of [-1, 1]) fbox(A, F, a + s * 0.35 - 0.02, a + s * 0.35 + 0.02, y - 0.06, y + 0.02, 0, 0.4, "steel_plate", L1);
}
// standing-seam ribs on the two slopes of a house() gable roof (ridge along x)
function seamRibs(A, X, Z, EAVE, RIDGE, ov = 0.4, gov = 0.28, t = 0.14) {
  const th = Math.atan2(RIDGE - EAVE, Z), deg = th * DEG;
  for (const s of [1, -1]) {
    const zA = -0.12 * s, zB = s * (Z + ov), zm = (zA + zB) / 2, len = Math.abs(zB - zA) / Math.cos(th);
    const ym = (RIDGE + 0.12 * Math.tan(th) + (RIDGE - (Z + ov) * Math.tan(th))) / 2, a = s > 0 ? deg : -deg;
    const nrm = [Math.cos(a / DEG), Math.sin(a / DEG)];
    for (let x = -X - gov + 0.25; x < X + gov - 0.1; x += 0.5) A.obox([x, ym + nrm[0] * (t + 0.02), zm + nrm[1] * (t + 0.02)], [0.03, 0.05, len], [a, 0, 0], "roof_seam", L1);
  }
}

// ============================================================================ bespoke building kit
// wall frames of a box shell (kit wall() / door() / window() frame plus the B1 facade frame d / out)
const wf = (s, x0, x1, z0, z1, T) => ({
  front: { axis: "x", out: 1, outer: z1, inner: z1 - T, d: z1 },
  rear: { axis: "x", out: -1, outer: z0, inner: z0 + T, d: z0 },
  east: { axis: "z", out: 1, outer: x1, inner: x1 - T, d: x1 },
  west: { axis: "z", out: -1, outer: x0, inner: x0 + T, d: x0 },
}[s]);
// four two-layer walls (cladding m outside, lining pl inside) of the box x0..x1, z0..z1 between y0 and y1;
// ops[side](F) builds that side's doors and windows on the frame F and returns their wall holes
function shell(A, { x0, x1, z0, z1, y0, y1, T = 0.3, m, pl = "plaster", ops = {}, skip = [] }) {
  const F = {};
  for (const s of SIDES) {
    F[s] = wf(s, x0, x1, z0, z1, T);
    if (skip.includes(s)) continue;
    const isX = F[s].axis === "x", holes = ops[s]?.(F[s]) ?? [];
    wall(A, { axis: F[s].axis, outer: F[s].outer, inner: F[s].inner, aOut: isX ? [x0, x1] : [z0 + T - 0.03, z1 - T + 0.03], aIn: isX ? [x0 + T, x1 - T] : [z0 + T, z1 - T], y0, y1, holes, stone: m, plaster: pl, pt: 0.03 });
  }
  return F;
}
const winOn = (A, F, id, c, sill, w, h) => window(A, { id, axis: F.axis, outer: F.outer, inner: F.inner, c, sill, w, h, shutters: false });
const doorOn = (A, F, id, c, floor, w = 1.0, h = 2.15, o = {}) => door(A, { id, axis: F.axis, outer: F.outer, inner: F.inner, c, floor, w, h, hingeSide: 1, leafMat: "door_paint", frameMat: "alu", ...o });
// inward firing point behind a window centre c on frame F at floor y
function winFire(N, F, c, y) {
  const d = F.inner - F.out * 0.6, yaw = F.axis === "x" ? (F.out > 0 ? 0 : 180) : (F.out > 0 ? 90 : 270);
  N.firingPoints.push(fire(F.axis === "x" ? [c, y, d] : [d, y, c], yaw, "window"));
}
// sectional up-and-over door (one panel hinged at its head, swinging up and in), steel guides and a drum housing
function upDoor(A, F, { id, c, floor, w = 3.2, h = 3.6 }) {
  const a0 = c - w / 2, a1 = c + w / 2, top = floor + h, ld0 = F.inner + F.out * 0.05, ld1 = F.inner + F.out * 0.11;
  const WB = (b0, b1, y0, y1, e0, e1, m, o) => wbox(A, F.axis, b0, b1, y0, y1, F.outer - F.out * e0, F.outer - F.out * e1, m, o);
  WB(a0 - 0.1, a0 + 0.012, floor, top - 0.012, 0.1, 0.34, "safety_yel"); WB(a1 - 0.012, a1 + 0.1, floor, top - 0.012, 0.1, 0.34, "safety_yel");
  WB(a0 - 0.1, a1 + 0.1, top - 0.012, top + 0.1, 0.1, 0.34, "metal_panel");
  WB(a0 - 0.1, a1 + 0.1, top + 0.15, top + 0.6, 0.36, 0.75, "metal_panel", L1);
  WB(a0 - 0.06, a1 + 0.06, floor - 0.04, floor + 0.008, -0.3, 0.34, "steel_plate");
  const cl = [P(F.axis, a0 + 0.01, floor + 0.01, ld0), P(F.axis, a1 - 0.01, top - 0.01, ld1)];
  hinged(A, { id, kind: "door", label: "roller door", sound: "steel", axis: F.axis === "x" ? "x" : "z", openAngle: 88, duration: 2.4,
    leaves: [{ hinge: P(F.axis, c, top, ld0), want: F.axis === "x" ? [0, 0, -F.out] : [-F.out, 0, 0],
      build: (T) => {
        wbox(A, F.axis, a0 + 0.02, a1 - 0.02, floor + 0.01, top - 0.02, ld0, ld1, "corr_grey", T);
        for (let y = floor + 0.55; y < top - 0.2; y += 0.55) wbox(A, F.axis, a0 + 0.01, a1 - 0.01, y - 0.015, y + 0.015, ld1, ld1 + F.out * 0.012, "metal_panel", T);
        wbox(A, F.axis, c - 0.25, c + 0.25, floor + 0.15, floor + 0.2, ld0 - F.out * 0.03, ld0, "alu", T);
      },
      collider: [[0, 1, 2].map((k) => Math.min(cl[0][k], cl[1][k])), [0, 1, 2].map((k) => Math.max(cl[0][k], cl[1][k]))] }],
    aperture: aabb(P(F.axis, a0, floor, F.inner), P(F.axis, a1, top, F.inner + F.out * 0.14)) });
  return { a0, a1, y0: -10, y1: top };
}
// precast stair flight along z (or x): solid step blocks, the smooth invisible collision ramp through the tread centres,
// a soffit slab (catalog_modern_buildings_b.mjs flight)
function flight(A, { axis = "z", b0, b1, a0, dir, y0, rise, n, run, m = "stair_conc", soffit = "stair_conc" }) {
  const r = rise / n, d = run / n, aAt = (i) => a0 + dir * i * d;
  const B = (aa, ab, ya, yb, mm, o) => (axis === "z" ? A.box([b0, ya, Math.min(aa, ab)], [b1, yb, Math.max(aa, ab)], mm, o) : A.box([Math.min(aa, ab), ya, b0], [Math.max(aa, ab), yb, b1], mm, o));
  for (let i = 0; i < n; i++) { const tp = y0 + (i + 1) * r; B(aAt(i), aAt(i + 1), tp - 0.2, tp, m, { k: i === n - 1 ? 1 : 0 }); }
  const th = Math.atan2(r, d), deg = th * DEG;
  const pA = [aAt(-0.5) - dir * 0.15 * Math.cos(th), y0 - 0.15 * Math.sin(th)], pB = [aAt(n - 0.5), y0 + rise];
  const Lr = Math.hypot(pB[0] - pA[0], pB[1] - pA[1]), ma = (pA[0] + pB[0]) / 2, my = (pA[1] + pB[1]) / 2, nA = -dir * Math.sin(th), nY = Math.cos(th);
  const ramp = (thick, up, mm, o, c0 = b0, c1 = b1) => {
    const ca = ma + nA * up, cy = my + nY * up;
    return axis === "z" ? A.obox([(c0 + c1) / 2, cy, ca], [c1 - c0, thick, Lr], [dir < 0 ? deg : -deg, 0, 0], mm, o) : A.obox([ca, cy, (c0 + c1) / 2], [Lr, thick, c1 - c0], [0, 0, dir > 0 ? deg : -deg], mm, o);
  };
  ramp(0.4, -0.2, m, HID);
  ramp(0.3, -0.3, soffit, K0, b0 + 0.012, b1 - 0.012);
  return { ramp };
}
// horizontal board / beam between plan points (x0, z0) and (x1, z1), y0..y1, thickness th
function seg(A, x0, z0, x1, z1, y0, y1, th, m, o = {}) {
  const dx = x1 - x0, dz = z1 - z0;
  return A.obox([(x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2], [Math.hypot(dx, dz), y1 - y0, th], [0, Math.atan2(-dz, dx) * DEG, 0], m, o);
}
// tube handrail run: posts every `sp` from a to b at plan line, rails at the given heights above y, an invisible guard
function tubeRail(A, x0, z0, x1, z1, y, { h = 1.1, sp = 1.5, rails = [0.5], m = "galv", dia = 0.048, guard = true } = {}) {
  const L = Math.hypot(x1 - x0, z1 - z0), n = Math.max(1, Math.round(L / sp));
  for (let i = 0; i <= n; i++) { const t = i / n, x = x0 + (x1 - x0) * t, z = z0 + (z1 - z0) * t; rod(A, [x, y, z], [x, y + h, z], dia, m, { seg: 8 }); A.cyl([x, y + 0.005, z], [0.12, 0.01, 0.12], m, { seg: 8, ...L1 }); }
  for (const hh of [h, ...rails]) rod(A, [x0, y + hh, z0], [x1, y + hh, z1], dia, m, { seg: 8 });
  if (guard) seg(A, x0, z0, x1, z1, y, y + h, 0.06, m, HID);
}

// ============================================================================ 1. small single-storey house
export function houseSmall() {
  const X = 4.5, Z = 3.6, dz = -0.35;
  const A = mmeta("mod_nt_house_small", "Small modern house", "Single-storey modern house, 9 x 7.2 m: white render with a dark vertical timber-clad bedroom wing, a flat roof behind a low parapet with an aluminium coping, a steel door canopy, aluminium windows with precast sills, a full-height glazed slider to the rear. Enterable: bedroom, bathroom, living room and kitchen in a row joined near the rear wall; front door into the living room, back door from the kitchen.", 1, [10, 9]);
  const B = sub("nt_small");
  modHouse(B, {
    X, Z, T: 0.3, h: 2.9, n: 1, plinthDepth: 0.4, shot: "mod_nt_small",
    style: () => ({ m: "render_m", shutters: false, split: [{ a0: -X, a1: -1.9, m: "clad_wood" }, { a0: -1.9, a1: X, m: "render_m" }], west: "clad_wood" }),
    wd: () => ({ w: 1.2, h: 1.4, sill: 0.8 }),
    win: () => ({ front: [{ c: -3.0, w: 1.4 }, { c: -1.15, w: 0.6, h: 0.6, sill: 1.5 }, { c: 3.1, w: 1.2, h: 1.1, sill: 1.0 }],
      rear: [{ c: -3.0, w: 1.0 }, { c: 0.7, w: 2.0, h: 2.1, sill: 0.1 }], east: [{ c: -0.6, w: 1.0 }], west: [{ c: 0.6, w: 1.0 }] }),
    parts: () => [-1.9, -0.4, 2.0], names: (f, r) => ["bedroom", "bathroom", "sitting", "kitchen"][r], hall: 2, rdx: 3.1, doorW: 1.0,
    roof: { type: "none", rise: 1 },
    dress: (B, k) => {
      const { EAVE } = k, fr = face("front", X, Z), re = face("rear", X, Z), we = face("west", X, Z);
      noBeams(B, EAVE);
      flatRoof(B, -X, X, -Z, Z, EAVE, { ph: 0.45, vents: [[-1.2, -2.0], [2.6, -2.2]] });
      fbox(B, fr, -X - 0.02, -1.9, EAVE - 0.05, EAVE + 0.5, -0.01, 0.06, "clad_wood", { k: 0, bv: 0.01 });   // timber fascia over the clad wing
      canopy(B, fr, 0.1, 1.5, GF + 2.4, 1.1);
      signPanel(B, fr, 1.62, 1.82, GF + 1.5, GF + 1.7, "sign_w", { depth: 0.04 });
      dpipe(B, re, -X + 0.2, 0.05, EAVE + 0.3); dpipe(B, fr, X - 0.2, 0.05, EAVE + 0.3);
      heatPump(B, re, -1.2, GF + 0.2);
      fbox(B, fr, 2.2, 4.0, -0.02, 0.45, 0.25, 0.75, "concrete_m", { bv: 0.02 });   // planter under the kitchen window
      fbox(B, fr, 2.25, 3.95, 0.45, 0.48, 0.3, 0.7, "earth", L1);
      fbox(B, re, -0.6, 2.0, -0.02, 0.06, 0, 0.5, "paving", { bv: 0.01 });          // patio slab at the slider
      fbox(B, we, -2.6, -1.0, 0.3, 1.25, 0, 0.6, "clad_wood", { bv: 0.01 });         // bin store
      fbox(B, we, -2.6, -1.0, 1.25, 1.3, -0.01, 0.62, "metal_panel", L1);
    },
  });
  const steps = placeSub(A, B, { dz });
  A.box([0.25, -0.02, Z + dz + 0.55], [1.35, 0.05, 4.5], "paving", { bv: 0.01 });
  roomTypes(A, [[/sitting/, "sitting"], [/kitchen/, "kitchen"], [/bedroom/, "bedroom"], [/bathroom/, "bathroom"]]);
  tourFrom(A, steps);
  return A;
}

// ============================================================================ 2. two-storey gable house with porch
export function houseTwo() {
  const X = 5.8, Z = 4.0, dz = -0.6;
  const A = mmeta("mod_nt_house_two", "Two-storey modern house", "Two-storey modern family house, 11.6 x 8 m: brick-slip ground floor, white render above, a dark slate gable roof with three solar panels, aluminium windows with precast sills, an open timber porch with a standing-seam lean-to roof. Enterable: living room, entrance hall and kitchen below, two bedrooms and a bathroom above, two internal stairs off a rear landing corridor.", 2, [12, 10]);
  const B = sub("nt_two");
  const k = modHouse(B, {
    X, Z, T: 0.35, h: 3.0, n: 2, corridor: 1.8, plinthDepth: 0.5, shot: "mod_nt_two",
    style: (f) => ({ m: f === 0 ? "brick_slip" : "render_m", shutters: false }),
    wd: () => ({ w: 1.2, h: 1.4, sill: 0.85 }),
    win: (f) => (f === 0 ? { front: [{ c: -4.3, w: 1.0 }, { c: -1.7, w: 1.6, h: 1.9, sill: 0.4 }, { c: 1.85, w: 1.2 }, { c: 4.3, w: 1.0 }], rear: [{ c: -2.0, w: 1.2, h: 1.2, sill: 1.0 }, { c: 2.0, w: 1.2, h: 1.2, sill: 1.0 }], east: [{ c: 2.7, w: 0.8 }], west: [{ c: 2.7, w: 0.8 }] }
      : { front: [{ c: -4.3, w: 0.8 }, { c: -1.75, w: 1.4 }, { c: 0, w: 0.6, h: 0.8, sill: 1.3 }, { c: 1.75, w: 1.4 }, { c: 4.3, w: 0.8 }], rear: [{ c: -2.0, w: 1.0 }, { c: 2.0, w: 1.0 }], east: [{ c: 2.7, w: 0.8 }], west: [{ c: 2.7, w: 0.8 }] }),
    parts: (f) => (f === 0 ? [-0.6, 0.9] : [-0.7, 0.7]),
    names: (f, r) => (f === 0 ? ["sitting", "entry", "kitchen"] : ["bedroom_1", "bathroom", "bedroom_2"])[r], hall: 1, doorW: 1.1,
    roof: { type: "gable", rise: 2.6, m: "roof_m", ov: 0.4 },
    dress: (B, k) => {
      const { EAVE, RIDGE, yAt } = k, fr = face("front", X, Z), re = face("rear", X, Z), th = Math.atan2(RIDGE - EAVE, Z);
      for (const F of [fr, re]) fbox(B, F, -X - 0.02, X + 0.02, yAt(1) - 0.3, yAt(1) - 0.05, -0.01, 0.06, "concrete_m", { bv: 0.01 });   // floor band
      // porch: concrete deck and step, two timber posts on steel shoes, a beam and a standing-seam lean-to roof
      B.box([-1.3, -0.05, Z], [1.6, GF - 0.02, Z + 1.4], "concrete_m", { bv: 0.02 });
      B.box([-0.4, -0.05, Z + 1.4], [0.7, 0.14, Z + 1.72], "concrete_m", BV);
      for (const x of [-1.15, 1.45]) { B.box([x - 0.07, GF - 0.02, Z + 1.18], [x + 0.07, 2.78, Z + 1.32], "clad_light", BV); B.box([x - 0.09, GF - 0.02, Z + 1.16], [x + 0.09, GF + 0.12, Z + 1.34], "steel_plate", L1); }
      B.box([-1.3, 2.78, Z + 1.14], [1.6, 2.98, Z + 1.36], "clad_light", BV);
      B.obox([0.15, 3.1, Z + 0.72], [3.2, 0.1, 1.72], [10, 0, 0], "roof_seam", { bv: 0.01 });
      for (let x = -1.35; x < 1.7; x += 0.4) B.obox([x, 3.17, Z + 0.72], [0.03, 0.04, 1.72], [10, 0, 0], "roof_seam", L1);
      B.box([-1.45, 3.15, Z - 0.02], [1.75, 3.3, Z + 0.07], "zinc", K0);           // flashing
      B.box([-0.1, 2.97, Z + 0.6], [0.4, 2.99, Z + 0.9], "light_panel", L1);
      signPanel(B, fr, 0.85, 1.05, GF + 1.5, GF + 1.7, "sign_w", { depth: 0.04 });
      // solar panels on the front slope, a flue through the rear slope
      const yS = (z) => RIDGE - Math.abs(z) * Math.tan(th) + 0.14 / Math.cos(th);
      for (const x of [-3.7, -2.55, -1.4]) {
        B.obox([x, yS(2.0) + 0.06, 2.0], [1.05, 0.04, 1.75], [th * DEG, 0, 0], "glass_dark", K0);
        B.obox([x, yS(2.0) + 0.04, 2.0], [1.09, 0.035, 1.79], [th * DEG, 0, 0], "alu", L1);
      }
      B.cyl([3.5, yS(-2.0) + 0.4, -2.0], [0.15, 0.9, 0.15], "stainless", { seg: 10, k: 0 });
      B.cyl([3.5, yS(-2.0) + 0.9, -2.0], [0.24, 0.1, 0.24], "stainless", { seg: 10, ...L1 });
      heatPump(B, re, 3.6, GF + 0.2);
    },
  });
  const steps = placeSub(A, B, { dz });
  A.box([-0.3, -0.02, Z + dz + 1.72], [0.6, 0.05, 5.0], "paving", { bv: 0.01 });
  roomTypes(A, [HALLS, [/sitting/, "sitting"], [/entry/, "hall"], [/kitchen/, "kitchen"], [/bedroom/, "bedroom"], [/bathroom/, "bathroom"]]);
  tourFrom(A, steps);
  return A;
}

// ============================================================================ 3. two-storey house with an attached garage
export function houseSplit() {
  const X = 5.2, Z = 4.5, hx = -1.7;
  const A = mmeta("mod_nt_house_split", "Modern house with garage", "Two-storey modern house, 10.4 x 9 m, with an attached single garage on the east side (13.8 x 9 m overall): grey brick ground floor, dark vertical timber cladding above with charcoal-rendered ends, a flat parapet roof, a steel door canopy, a sectional garage door. Enterable: living room and kitchen below, two bedrooms above, two stairs off the rear landing; the garage (utility) opens from the kitchen and by its own door.", 2, [14, 11]);
  const B = sub("nt_split");
  modHouse(B, {
    X, Z, T: 0.35, h: 3.0, n: 2, corridor: 1.8, plinthDepth: 0.5, shot: "mod_nt_split",
    style: (f) => (f === 0 ? { m: "brick_grey", shutters: false } : { m: "clad_wood", shutters: false, east: "render_dk", west: "render_dk" }),
    wd: () => ({ w: 1.3, h: 1.5, sill: 0.8 }),
    win: (f) => (f === 0 ? { front: [{ c: -3.9, w: 1.0 }, { c: -1.9, w: 0.5, h: 2.1, sill: 0.15 }, { c: 1.2, w: 1.6, h: 1.2, sill: 0.95 }, { c: 3.9, w: 1.0 }], rear: [{ c: -1.2, w: 1.6, h: 2.1, sill: 0.1 }, { c: 3.6, w: 0.8, h: 0.8, sill: 1.3 }], west: [{ c: 2.8, w: 1.0 }] }
      : { front: [{ c: -3.9, w: 1.0 }, { c: -1.1, w: 1.8, h: 1.5, sill: 0.6 }, { c: 1.1, w: 1.8, h: 1.5, sill: 0.6 }, { c: 3.9, w: 1.0 }], rear: [{ c: -1.1, w: 1.2 }, { c: 1.1, w: 1.2 }], east: [{ c: 2.8, w: 1.0 }], west: [{ c: 2.8, w: 1.0 }] }),
    parts: (f) => (f === 0 ? [0.2] : [0.0]),
    names: (f, r) => (f === 0 ? ["sitting", "kitchen"] : ["bedroom_1", "bedroom_2"])[r], hall: 0, rdx: 1.2, doorW: 1.0,
    doors: [{ id: "garage_link", side: "east", c: 2.6, w: 0.9, h: 2.1, room: "kitchen" }],
    roof: { type: "none", rise: 1 },
    dress: (B, k) => {
      const { EAVE, yAt } = k, fr = face("front", X, Z), re = face("rear", X, Z);
      noBeams(B, EAVE);
      flatRoof(B, -X, X, -Z, Z, EAVE, { m: "render_dk", ph: 0.7, vents: [[-2.5, -2.5], [2.0, -3.0]] });
      for (const F of [fr, re]) fbox(B, F, -X - 0.02, X + 0.02, yAt(1) - 0.3, yAt(1) - 0.05, -0.01, 0.07, "metal_panel", { bv: 0.01 });
      canopy(B, fr, -1.75, -0.25, GF + 2.45, 1.2);
      signPanel(B, fr, -0.2, 0.0, GF + 1.5, GF + 1.7, "sign_w", { depth: 0.04 });
      dpipe(B, re, -X + 0.2, 0.05, EAVE + 0.3); dpipe(B, fr, -X + 0.2, 0.05, EAVE + 0.3);
      heatPump(B, re, -3.6, GF + 0.2);
    },
  });
  annexDoor(B, X - 0.175, 2.6, "garage");
  const steps = placeSub(A, B, { dx: hx });
  // garage: concrete floor, brick walls, sectional door, flat roof
  const gx0 = hx + X, gx1 = gx0 + 3.4, gz0 = -2.2, gz1 = Z, gt = 0.3, gh = 3.0, gxc = (gx0 + gx1) / 2;
  A.box([gx0, -0.3, gz0], [gx1, 0.05, gz1], "concrete_m");
  A.box([gx0, 0.05, gz0 + gt], [gx1 - gt, 0.15, gz1 - gt], "concrete_floor");
  A.box([gx1 - gt, 0.05, gz0], [gx1, gh, gz1], "brick_grey");
  A.box([gx0, 0.05, gz0], [gx1 - gt, gh, gz0 + gt], "brick_grey");
  A.box([gx0, 0.05, gz1 - gt], [gx0 + 0.3, gh, gz1], "brick_grey"); A.box([gx1 - gt - 0.05, 0.05, gz1 - gt], [gx1 - gt, gh, gz1], "brick_grey");
  A.box([gx0 + 0.3, 2.4, gz1 - gt], [gx1 - gt - 0.05, gh, gz1], "brick_grey");
  door(A, { id: "garage_door", axis: "x", outer: gz1, inner: gz1 - gt, c: (gx0 + 0.3 + gx1 - gt - 0.05) / 2, floor: 0.15, w: 2.75, h: 2.25, hingeSide: 1, double: true, leafMat: "metal_panel", frameMat: "alu", step: false, lintel: false, label: "garage door", sound: "steel" });
  A.box([gx0 + 0.3, gh - 0.2, gz0 + gt], [gx1 - gt, gh, gz1 - gt], "plaster");
  flatRoof(A, gx0, gx1, gz0, gz1, gh, { ph: 0.45, m: "brick_grey" });
  A.box([gx0 + 0.6, 2.4, gz1 + 0.01], [gx1 - 0.6, 2.47, gz1 + 0.08], "light_panel", L1);
  for (let x = gx0 + 0.6; x < gx1 - 0.4; x += 0.55) A.box([x - 0.006, 0.6, gz1 - gt + 0.1], [x + 0.006, 2.1, gz1 - gt + 0.115], "alu", L1);   // door panel ribs
  A.box([gx0 + 0.2, -0.02, gz1], [gx1 - 0.1, 0.06, 5.5], "paving", { bv: 0.01 });   // drive
  A.box([hx - 1.55, -0.02, Z + 0.55], [hx - 0.45, 0.05, 5.5], "paving", { bv: 0.01 });   // front path
  const nav = A.meta.nav;
  nav.rooms.push(room("garage", [gx0 + 0.3, 0.15, gz0 + gt], [gx1 - gt, gh - 0.2, gz1 - gt]));
  nav.corners.push(...corners("garage", gx0 + 0.3, gx1 - gt, gz0 + gt, gz1 - gt, 0.15));
  nav.doors.push({ a: "garage", b: "outside", p: [q4(gxc), 0.15, q4(gz1 - gt / 2)] });
  nav.entrances.push({ p: [q4(gxc), 0, q4(gz1 + 1.0)], dir: [0, 0, -1], room: "garage" });
  roomTypes(A, [HALLS, [/sitting/, "sitting"], [/kitchen/, "kitchen"], [/bedroom/, "bedroom"], [/garage/, "utility"]]);
  tourFrom(A, steps, [{ goto: [gx1 + 0.8, -Z - 1.2] }, { goto: [gx1 + 0.8, gz1 + 1.8] }, { goto: [gxc, gz1 + 1.8] }, { interact: "garage_door", aim: [gxc, 1.2, gz1 - gt - 0.05], expectOpen: true }, { goto: [gxc, gz1 - 1.5], note: "into the garage" }]);
  return A;
}

// ============================================================================ 4. lakeside house (+z faces the water)
export function waterfrontHouse() {
  const X = 6.0, Z = 4.0, dz = -1.0;
  const A = mmeta("mod_nt_waterfront_house", "Lakeside house", "Two-storey lakeside house, 12 x 8 m: the water side (+z, the asset front) is almost all floor-to-ceiling glazing under a deep flat roof overhang, with a full-width glass-railed balcony on the upper floor and a timber deck along the front on posts with steps down to the shore; white render below, light timber cladding above, a small-windowed land side with the back door. Enterable: living room and kitchen below, two bedrooms and a bathroom above, two stairs off the land-side corridor.", 2, [14, 12]);
  const B = sub("nt_water");
  const gl = (c, w, h = 2.3, sill = 0.05) => ({ c, w, h, sill });
  modHouse(B, {
    X, Z, T: 0.35, h: 3.0, n: 2, corridor: 1.8, plinthDepth: 0.5, shot: "mod_nt_water",
    style: (f) => ({ m: f === 0 ? "render_m" : "clad_light", shutters: false }),
    wd: () => ({ w: 1.0, h: 1.2, sill: 1.0 }),
    win: (f) => (f === 0 ? { front: [gl(-4.4, 1.6), gl(-2.45, 0.9), gl(-0.1, 0.8), gl(1.7, 2.2), gl(4.4, 1.6)], rear: [{ c: -2.0 }, { c: 2.0 }], east: [gl(2.7, 0.9, 2.0, 0.2)], west: [gl(2.7, 0.9, 2.0, 0.2)] }
      : { front: [gl(-4.4, 1.6, 2.0, 0.3), gl(-1.85, 2.0), { c: 0, w: 0.8, h: 1.0, sill: 1.2 }, gl(1.85, 2.0), gl(4.4, 1.6, 2.0, 0.3)], rear: [{ c: -2.0 }, { c: 2.0 }], east: [{ c: 2.7, w: 0.9 }], west: [{ c: 2.7, w: 0.9 }] }),
    parts: (f) => (f === 0 ? [0.4] : [-0.7, 0.7]),
    names: (f, r) => (f === 0 ? ["sitting", "kitchen"] : ["bedroom_1", "bathroom", "bedroom_2"])[r], hall: 0, doorW: 1.0,
    roof: { type: "none", rise: 1 },
    dress: (B, k) => {
      const { EAVE, yAt } = k, fr = face("front", X, Z), re = face("rear", X, Z);
      noBeams(B, EAVE);
      flatRoof(B, -X, X, -Z, Z, EAVE, { ph: 0.35, m: "render_m", vents: [[-3.0, -2.5], [3.5, -3.0]] });
      // deep roof overhang over the balcony: concrete slab, timber soffit, aluminium fascia, downlights
      fbox(B, fr, -X - 0.3, X + 0.3, EAVE - 0.05, EAVE + 0.3, -0.02, 1.6, "concrete_m", { bv: 0.015 });
      fbox(B, fr, -X - 0.28, X + 0.28, EAVE - 0.08, EAVE - 0.05, 0.0, 1.58, "clad_light", L1);
      fbox(B, fr, -X - 0.32, X + 0.32, EAVE - 0.1, EAVE + 0.36, 1.58, 1.64, "alu", { k: 0, bv: 0.008 });
      for (let a = -X + 0.6; a < X; a += 1.6) fpart(B, "cyl", fr, a, EAVE - 0.09, 0.8, [0.1, 0.02, 0.1], "light_panel", L1);
      glassBalcony(B, fr, -X + 0.1, X - 0.1, yAt(1), 1.4);
      for (const F of [fr, re]) fbox(B, F, -X - 0.02, X + 0.02, yAt(1) - 0.3, yAt(1) - 0.21, -0.01, 0.06, "alu", { k: 0, bv: 0.005 });
      dpipe(B, re, -X + 0.2, 0.05, EAVE + 0.3); dpipe(B, re, X - 0.2, 0.05, EAVE + 0.3);
      heatPump(B, re, 3.8, GF + 0.2);
      signPanel(B, re, 0.75, 0.95, GF + 1.5, GF + 1.7, "sign_w", { depth: 0.04 });
      // timber deck on posts along the water side, glass balustrade, steps down to the shore at the east end
      const dz0 = Z, dz1 = Z + 2.4, yd = GF - 0.02;
      B.box([-X, yd - 0.05, dz0], [X, yd, dz1], "deck", { bv: 0.008 });
      for (let x = -X + 0.15; x < X; x += 0.145) B.box([x - 0.004, yd, dz0 + 0.02], [x + 0.004, yd + 0.002, dz1 - 0.02], "rubber", L1);
      for (const z of [dz0 + 0.3, dz1 - 0.2]) B.box([-X + 0.05, yd - 0.3, z - 0.07], [X - 0.05, yd - 0.05, z + 0.07], "pile", K0);   // bearers
      for (let x = -X + 0.3; x < X; x += 0.6) B.box([x - 0.025, yd - 0.22, dz0], [x + 0.025, yd - 0.05, dz1 - 0.05], "pile", L1);   // joists
      for (const x of [-X + 0.15, -3.0, 0, 3.0, X - 0.15]) {
        B.cyl([x, (yd - 1.3) / 2 - 0.15, dz1 - 0.2], [0.16, yd + 1.0, 0.16], "pile", { seg: 10 });
        B.box([x - 0.1, yd - 0.5, dz1 - 0.3], [x + 0.1, yd - 0.3, dz1 - 0.1], "steel_plate", L1);
      }
      B.box([-X, yd - 0.3, dz1 - 0.04], [X, yd - 0.02, dz1], "clad_light", K0);   // fascia board
      const rx = (x0, x1) => {
        B.box([x0, yd + 0.06, dz1 - 0.06], [x1, yd + 1.0, dz1 - 0.04], "glass_rail", K0);
        B.box([x0, yd + 1.0, dz1 - 0.08], [x1, yd + 1.05, dz1 - 0.02], "alu", { k: 0, bv: 0.006 });
        B.box([x0, yd, dz1 - 0.08], [x1, yd + 1.05, dz1 - 0.02], "alu", HID);
      };
      rx(-X, 3.4); rx(4.9, X);
      for (const s of [-1, 1]) { B.box([s * X - 0.03, yd + 0.06, dz0], [s * X - 0.01, yd + 1.0, dz1 - 0.08], "glass_rail", K0); B.box([s * X - 0.08, yd, dz0], [s * X, yd + 1.05, dz1 - 0.08], "alu", HID); }
      for (let i = 0; i < 2; i++) B.box([3.45, -0.05, dz1 + i * 0.3 - 0.0], [4.85, yd - 0.17 - i * 0.14, dz1 + 0.3 + i * 0.3], "deck", { bv: 0.008 });
    },
  });
  const steps = placeSub(A, B, { dz });
  A.box([-0.5, -0.02, -6.0], [0.5, 0.05, -Z + dz - 0.55], "paving", { bv: 0.01 });   // land-side path to the back door
  roomTypes(A, [HALLS, [/sitting/, "sitting"], [/kitchen/, "kitchen"], [/bedroom/, "bedroom"], [/bathroom/, "bathroom"]]);
  A.meta.nav.cover.push(cover([-4.0, 0.28, 5.2], 180, "low", ["over"]), cover([1.5, 0.28, 5.2], 180, "low", ["over"]));
  A.meta.notes = "Place with +z facing the lake: the glazed front, balcony and deck face the water; the back door (-z) faces the street.";
  tourFrom(A, steps);
  return A;
}

// ============================================================================ 5. modern farmhouse
export function farmhouse() {
  const X = 7.0, Z = 4.5, dz = -0.9;
  const A = mmeta("mod_nt_farmhouse", "Modern farmhouse", "Modern rural two-storey farmhouse, 14 x 9 m: warm render over a brick plinth band, a steep standing-seam metal gable roof with a brick chimney, aluminium windows with precast sills, a full-width timber veranda with a lean-to metal roof on square posts. Enterable: living room, entrance hall and farm kitchen below, two bedrooms and a bathroom above, two stairs off the rear landing corridor.", 2, [16, 12]);
  const B = sub("nt_farm");
  modHouse(B, {
    X, Z, T: 0.35, h: 3.0, n: 2, corridor: 1.8, plinthDepth: 0.5, shot: "mod_nt_farm",
    style: () => ({ m: "render_warm", shutters: false }),
    wd: () => ({ w: 1.2, h: 1.5, sill: 0.8 }),
    win: (f) => (f === 0 ? { front: [{ c: -5.5, w: 1.0 }, { c: -2.6, w: 1.6 }, { c: 2.6, w: 1.6 }, { c: 5.5, w: 1.0 }], rear: [{ c: -2.6 }, { c: 2.6 }], east: [{ c: 3.0, w: 1.0 }], west: [{ c: 3.0, w: 1.0 }] }
      : { front: [{ c: -5.5, w: 1.0 }, { c: -2.5, w: 1.4 }, { c: 0, w: 0.8, h: 1.0, sill: 1.2 }, { c: 2.5, w: 1.4 }, { c: 5.5, w: 1.0 }], rear: [{ c: -2.6, w: 1.0 }, { c: 2.6, w: 1.0 }], east: [{ c: 3.0, w: 1.0 }], west: [{ c: 3.0, w: 1.0 }] }),
    parts: (f) => (f === 0 ? [-1.2, 1.2] : [-1.0, 1.0]),
    names: (f, r) => (f === 0 ? ["sitting", "entry", "kitchen"] : ["bedroom_1", "bathroom", "bedroom_2"])[r], hall: 1, doorW: 1.1,
    roof: { type: "gable", rise: 3.4, m: "roof_seam", ov: 0.45 },
    dress: (B, k) => {
      const { EAVE, RIDGE } = k;
      for (const s of SIDES) { const F = face(s, X, Z), ax = s === "front" || s === "rear"; fbox(B, F, ax ? -X - 0.03 : -Z, ax ? X + 0.03 : Z, -0.02, 0.62, -0.01, 0.035, "brick_slip", { k: 0, bv: 0.008 }); }
      seamRibs(B, X, Z, EAVE, RIDGE, 0.45);
      chimney(B, { x: -4.6, z: -1.2, y0: EAVE + 1.0, y1: RIDGE + 1.0, w: 0.75, d: 0.55, m: "brick_slip" });
      // veranda: timber deck, posts, beam, lean-to standing-seam roof, rails, steps
      const vz = Z + 2.0, yd = GF - 0.04;
      B.box([-5.2, -0.05, Z], [5.2, yd, vz], "deck", { bv: 0.008 });
      for (let x = -5.0; x < 5.2; x += 0.145) B.box([x - 0.004, yd, Z + 0.02], [x + 0.004, yd + 0.002, vz - 0.02], "rubber", L1);
      const posts = [-5.05, -2.5, 2.5, 5.05];
      for (const x of posts) { B.box([x - 0.08, yd, vz - 0.24], [x + 0.08, 2.75, vz - 0.08], "clad_light", BV); B.box([x - 0.1, yd, vz - 0.26], [x + 0.1, yd + 0.12, vz - 0.06], "steel_plate", L1); }
      B.box([-5.2, 2.75, vz - 0.26], [5.2, 2.97, vz - 0.06], "clad_light", BV);
      const ang = Math.atan2(0.5, 2.2) * DEG;
      B.obox([0, 3.2, Z + 1.05], [10.6, 0.1, 2.32], [ang, 0, 0], "roof_seam", { bv: 0.01 });
      for (let x = -5.1; x < 5.2; x += 0.45) B.obox([x, 3.27, Z + 1.05], [0.03, 0.04, 2.32], [ang, 0, 0], "roof_seam", L1);
      B.box([-5.3, 3.35, Z - 0.02], [5.3, 3.5, Z + 0.07], "zinc", K0);
      B.box([-5.3, 2.86, vz + 0.0], [5.3, 2.98, vz + 0.12], "zinc", K0);                // gutter
      for (const [x0, x1] of [[-5.05, -2.5], [2.5, 5.05], [-2.5, -0.75], [0.75, 2.5]]) {
        B.box([x0, yd + 0.85, vz - 0.2], [x1, yd + 0.92, vz - 0.12], "clad_light", { k: 0, bv: 0.006 });
        for (let x = x0 + 0.12; x < x1 - 0.06; x += 0.12) B.box([x - 0.02, yd + 0.1, vz - 0.18], [x + 0.02, yd + 0.85, vz - 0.14], "clad_light", L1);
        B.box([x0, yd + 0.08, vz - 0.2], [x1, yd + 0.12, vz - 0.12], "clad_light", L1);
        B.box([x0, yd, vz - 0.2], [x1, yd + 0.92, vz - 0.12], "clad_light", HID);
      }
      for (let i = 0; i < 2; i++) B.box([-0.75, -0.05, vz + i * 0.3], [0.75, yd - 0.09 - i * 0.09, vz + 0.3 + i * 0.3], "deck", { bv: 0.008 });
      B.box([-0.25, 2.96, Z + 0.7], [0.25, 2.98, Z + 1.0], "light_panel", L1);
      signPanel(B, face("front", X, Z), 0.85, 1.05, GF + 1.5, GF + 1.7, "sign_w", { depth: 0.04 });
      heatPump(B, face("rear", X, Z), 4.8, GF + 0.2);
    },
  });
  const steps = placeSub(A, B, { dz });
  A.box([-0.6, -0.02, Z + dz + 2.6], [0.6, 0.05, 6.0], "paving", { bv: 0.01 });
  roomTypes(A, [HALLS, [/sitting/, "sitting"], [/entry/, "hall"], [/kitchen/, "kitchen"], [/bedroom/, "bedroom"], [/bathroom/, "bathroom"]]);
  A.meta.nav.cover.push(cover([-3.8, 0.26, Z + dz + 1.3], 180, "low", ["over"]), cover([3.8, 0.26, Z + dz + 1.3], 180, "low", ["over"]));
  tourFrom(A, steps);
  return A;
}

// ============================================================================ 6. steel-frame barn / machine shed
export function barnSteel() {
  const A = nt("mod_nt_barn_steel", "Steel-frame barn", "Modern", "Steel portal-frame farm barn and machine shed, 20 x 14 m, 5 m to the eaves: green-painted I-section frames, precast concrete panels to 1.5 m with green profiled steel sheeting above, a grey sheeted duo-pitch roof with clear roof lights, gutters and downpipes. A 6 x 4.6 m opening in the front with its two sliding leaves parked open on an outside track, steel bump posts at the jambs, and a personnel door in the west gable. Inside: a concrete floor, big square hay bales stacked three high in the west bay, black-wrapped round bales in the east bay and an empty, line-marked tractor bay behind the doors. Enterable: one room (barn).", [20, 14]);
  const X = 10, Z = 7, EAVE = 5.0, RIDGE = 6.6, FL = 0.15, th = Math.atan2(RIDGE - EAVE, Z), tn = Math.tan(th), N = nav0();
  const yR = (z) => RIDGE - Math.abs(z) * tn;   // underside of the roof sheets
  A.box([-X, -0.4, -Z], [X, 0.0, Z], "concrete_m");
  A.box([-X + 0.15, 0.0, -Z + 0.15], [X - 0.15, FL, Z - 0.15], "concrete_floor");
  A.box([-3.4, -0.05, Z], [3.4, 0.08, Z + 0.45], "concrete_m", { bv: 0.02 });   // apron
  // portal frames: I-section columns and rafters (visual flanges + one collider each), haunches, wind posts
  const fx = [-9.8, -3.3, 3.3, 9.8];
  const ibeamV = (x, z, y0, y1, d = 0.36) => {
    for (const s of [-1, 1]) A.box([x - 0.1, y0, z + s * d / 2 - 0.008], [x + 0.1, y1, z + s * d / 2 + 0.008], "steel_frame", K0);
    A.box([x - 0.006, y0, z - d / 2], [x + 0.006, y1, z + d / 2], "steel_frame", K0);
    A.box([x - 0.1, y0, z - d / 2], [x + 0.1, y1, z + d / 2], "steel_frame", HID);
    A.box([x - 0.18, y0, z - 0.26], [x + 0.18, y0 + 0.02, z + 0.26], "steel_frame", L1);   // base plate
    for (const s of [-1, 1]) for (const t of [-1, 1]) A.cyl([x + s * 0.13, y0 + 0.04, z + t * 0.2], [0.03, 0.05, 0.03], "galv", { seg: 6, ...L1 });
  };
  for (const x of fx) {
    for (const s of [-1, 1]) {
      ibeamV(x, s * (Z - 0.32), FL, EAVE);
      const L = (Z - 0.14) / Math.cos(th), zc = s * (Z - 0.14) / 2, yc = (EAVE + RIDGE) / 2 - 0.3, a = s * th * DEG;
      for (const e of [-0.2, 0.2]) A.obox([x, yc + e * Math.cos(th), zc + e * s * Math.sin(th)], [0.2, 0.016, L], [a, 0, 0], "steel_frame", K0);
      A.obox([x, yc, zc], [0.012, 0.4, L], [a, 0, 0], "steel_frame", K0);
      A.obox([x, EAVE - 0.35, s * (Z - 0.85)], [0.012, 0.7, 1.0], [a, 0, 0], "steel_frame", L1);   // haunch
    }
    A.box([x - 0.12, RIDGE - 0.75, -0.25], [x + 0.12, RIDGE - 0.1, 0.25], "steel_frame", L1);     // apex plate
  }
  for (const s of [-1, 1]) for (const z of [-2.4, 2.4]) ibeamV(s * (X - 0.3), z, FL, yR(z) - 0.45, 0.26);   // gable wind posts
  for (const s of [-1, 1]) for (let i = 0; i < 5; i++) {   // purlins
    const z = s * (0.5 + i * 1.55);
    A.box([-X, yR(z) - 0.25, z - 0.035], [X, yR(z) - 0.04, z + 0.035], "galv", L1);
  }
  for (const s of [-1, 1]) for (const y of [2.6, 3.9]) A.box([-X + 0.2, y - 0.08, s * (Z - 0.12) - 0.03], [X - 0.2, y + 0.08, s * (Z - 0.12) + 0.03], "galv", L1);   // side rails
  // walls: precast panels to 1.5 m, profiled sheeting above; front opening, west personnel door
  const DO = { a0: -3.0, a1: 3.0, y0: -1, y1: 4.6 }, PD = { c: 3.0, w: 1.0, h: 2.1 };
  const pd = { a0: PD.c - 0.5, a1: PD.c + 0.5, y0: -1, y1: FL + PD.h };
  for (const s of [-1, 1]) {
    const holes = s > 0 ? [DO] : [];
    for (const [a0, a1, y0, y1] of cutRect(-X, X, 0, 1.5, holes)) A.box([a0, y0, s > 0 ? Z - 0.15 : -Z], [a1, y1, s > 0 ? Z : -Z + 0.15], "conc_panel");
    for (const [a0, a1, y0, y1] of cutRect(-X, X, 1.5, EAVE, holes)) A.box([a0, y0, s > 0 ? Z - 0.05 : -Z], [a1, y1, s > 0 ? Z : -Z + 0.05], "corr_green");
    A.box([-X - 0.02, 1.45, s * Z - (s > 0 ? 0.0 : 0.04)], [X + 0.02, 1.53, s * Z + (s > 0 ? 0.04 : 0.0)], "galv", L1);   // flashing at the panel line
    for (let x = -X + 3.33; x < X - 1; x += 3.33) if (s < 0 || x < DO.a0 || x > DO.a1) A.box([x - 0.01, 0, s * Z - 0.005], [x + 0.01, 1.45, s * Z + 0.005], "hazard_blk", L1);   // panel joints
  }
  for (const s of [-1, 1]) {
    const holes = s < 0 ? [pd] : [], xo = s * X, xi = s * (X - 0.15), xs = s * (X - 0.05);
    for (const [a0, a1, y0, y1] of cutRect(-Z + 0.15, Z - 0.15, 0, 1.5, holes)) A.box([Math.min(xo, xi), y0, a0], [Math.max(xo, xi), y1, a1], "conc_panel");
    for (const [a0, a1, y0, y1] of cutRect(-Z + 0.05, Z - 0.05, 1.5, EAVE, holes)) A.box([Math.min(xo, xs), y0, a0], [Math.max(xo, xs), y1, a1], "corr_green");
    A.prism(0, Math.min(xo, xs), Math.max(xo, xs), [[-Z, EAVE], [Z, EAVE], [0, RIDGE]], "corr_green");
    A.box([Math.min(xo, xo + s * 0.04), 1.45, -Z], [Math.max(xo, xo + s * 0.04), 1.53, Z], "galv", L1);
  }
  door(A, { id: "side_door", axis: "z", outer: -X, inner: -X + 0.15, c: PD.c, floor: FL, w: PD.w, h: PD.h, hingeSide: 1, leafMat: "metal_panel", frameMat: "galv", step: false, lintel: false, label: "door", sound: "steel" });
  A.box([-X - 0.35, -0.05, PD.c - 0.8], [-X, 0.12, PD.c + 0.8], "concrete_m", { bv: 0.015 });
  // big opening: header, jamb trims, bump posts, two sliding leaves parked open on an outside top track
  A.box([DO.a0 - 0.1, 4.6, Z - 0.3], [DO.a1 + 0.1, 4.9, Z - 0.05], "steel_frame", BV);
  for (const x of [DO.a0, DO.a1]) {
    A.box([x - 0.06, 0, Z - 0.16], [x + 0.06, 4.6, Z + 0.02], "galv", K0);
    A.cyl([x + (x < 0 ? -0.35 : 0.35), 0.6, Z + 0.25], [0.16, 1.2, 0.16], "safety_yel", { seg: 12 });
    A.cyl([x + (x < 0 ? -0.35 : 0.35), 1.21, Z + 0.25], [0.17, 0.02, 0.17], "hazard_blk", { seg: 12, ...L1 });
  }
  A.box([-6.6, 4.98, Z + 0.02], [6.6, 5.08, Z + 0.14], "galv", K0);   // track
  for (const s of [-1, 1]) {
    const x0 = s > 0 ? 3.15 : -6.35, x1 = s > 0 ? 6.35 : -3.15;
    A.box([x0, 0.08, Z + 0.06], [x1, 4.92, Z + 0.12], "corr_green");
    for (const y of [0.15, 2.5, 4.85]) A.box([x0, y - 0.06, Z + 0.12], [x1, y + 0.06, Z + 0.16], "steel_frame", L1);
    for (const x of [x0 + 0.05, x1 - 0.05]) A.box([x - 0.05, 0.08, Z + 0.12], [x + 0.05, 4.92, Z + 0.16], "steel_frame", L1);
    for (const x of [x0 + 0.4, x1 - 0.4]) A.cyl([x, 4.98, Z + 0.09], [0.12, 0.12, 0.12], "galv", { seg: 8, r: [90, 0, 0], ...L1 });   // track wheels
    A.box([s > 0 ? x0 + 0.2 : x1 - 0.32, 1.0, Z + 0.16], [s > 0 ? x0 + 0.32 : x1 - 0.2, 1.4, Z + 0.2], "galv", L1);   // handle
  }
  // roof: two sheeted slopes, clear roof lights, ridge cap, gutters and downpipes
  for (const s of [-1, 1]) {
    const L = (Z + 0.3) / Math.cos(th), zc = s * (Z + 0.3) / 2, a = s * th * DEG, yc = RIDGE - ((Z + 0.3) / 2) * tn + 0.025 / Math.cos(th);
    A.obox([0, yc, zc], [2 * X + 0.4, 0.05, L], [a, 0, 0], "corr_grey");
    for (const x of [-6.6, 0, 6.6]) A.obox([x, yc + 0.015, s * 3.6], [1.1, 0.04, 3.0 / Math.cos(th)], [a, 0, 0], "corr_clear", K0);
    const ye = RIDGE - (Z + 0.3) * tn;
    A.box([-X - 0.2, ye - 0.2, s > 0 ? Z + 0.28 : -Z - 0.45], [X + 0.2, ye - 0.02, s > 0 ? Z + 0.45 : -Z - 0.28], "galv", K0);
    for (const x of [-X + 0.3, X - 0.3]) A.cyl([x, (ye - 0.2) / 2, s * (Z + 0.37)], [0.1, ye - 0.2, 0.1], "galv", { seg: 10, k: 0 });
  }
  A.obox([0, RIDGE + 0.09, 0], [2 * X + 0.42, 0.04, 0.6], [0, 0, 0], "corr_grey", K0);
  for (const s of [-1, 1]) A.obox([0, RIDGE + 0.04, s * 0.28], [2 * X + 0.42, 0.03, 0.12], [s * th * DEG, 0, 0], "corr_grey", L1);
  // hay: big square bales in the west bay (three high), wrapped round bales in the east bay
  for (let i = 0; i < 2; i++) for (let j = 0; j < 3; j++) for (let l = 0; l < 3; l++) {
    if (l === 2 && i === 1 && j === 2) continue;   // a gap at the top of the stack
    const x0 = -9.55 + i * 2.45, z0 = -6.75 + j * 1.25, y0 = FL + l * 0.9;
    A.box([x0, y0, z0], [x0 + 2.4, y0 + 0.88, z0 + 1.2], "hay", { bv: 0.04 });
    for (const t of [0.33, 0.66]) A.box([x0 + 2.4 * t - 0.01, y0 - 0.002, z0 - 0.005], [x0 + 2.4 * t + 0.01, y0 + 0.882, z0 + 1.205], "line_yellow", L1);   // twine
  }
  A.box([-6.2, FL, -2.5], [-3.8, FL + 0.88, -1.3], "hay", { bv: 0.04 });   // one bale pulled off the stack
  for (const [x, z, y] of [[5.0, -6.0, 0], [6.6, -6.0, 0], [8.2, -6.0, 0], [5.8, -6.0, 1], [7.4, -6.0, 1], [8.6, -3.6, 0]]) {
    const yc = FL + 0.75 + y * 1.28;
    A.cyl([x, yc, z], [1.5, 1.2, 1.5], "hay_wrap", { seg: 18, r: [90, 0, 0] });
    A.cyl([x, yc, z + 0.61], [1.2, 0.02, 1.2], "hay_wrap", { seg: 18, r: [90, 0, 0], ...L1 });
  }
  // tractor bay markings behind the doors
  for (const x of [-3.2, 3.2]) A.box([x - 0.05, FL, -5.5], [x + 0.05, FL + 0.004, 6.5], "line_yellow", L1);
  A.box([-3.2, FL, -5.55], [3.2, FL + 0.004, -5.45], "line_yellow", L1);
  A.box([-0.6, FL, -6.85], [0.6, FL + 0.25, -6.6], "rubber", BV);   // wheel stop
  for (const x of [-6.6, 0, 6.6]) { A.box([x - 0.6, EAVE - 0.6, -0.15], [x + 0.6, EAVE - 0.56, 0.15], "light_panel", L1); rod(A, [x, EAVE - 0.56, 0], [x, yR(0) - 0.3, 0], 0.01, "galv", L1); }
  A.box([-X + 0.2, 1.3, 4.4], [-X + 0.5, 1.8, 4.8], "metal_panel", L1);   // switch board by the door
  addRoom(N, "barn", -X + 0.15, FL, -Z + 0.15, X - 0.15, EAVE - 0.3, Z - 0.15);
  ndoor(N, "barn", "outside", [0, FL, Z - 0.1]); ndoor(N, "barn", "outside", [-X + 0.08, FL, PD.c]);
  nent(N, [0, 0, Z + 1.2], [0, 0, -1], "barn"); nent(N, [-X - 1.0, 0, PD.c], [1, 0, 0], "barn");
  N.cover.push(cover([-7.1, FL, -2.6], 180, "high", ["left", "right"]), cover([-5.0, FL, -0.8], 180, "low", ["over"]), cover([6.6, FL, -4.6], 180, "high", ["left", "right"]),
    cover([X + 0.6, 0, 0], 270, "high", ["left", "right"]), cover([-X - 0.6, 0, -2.0], 90, "high", ["left", "right"]), cover([5.0, 0, -Z - 0.6], 0, "high", ["left", "right"]), cover([-5.0, 0, Z + 0.6], 180, "high", ["right"]));
  A.meta.roomTypes = { barn: "barn" };
  setNav(A, N);
  tourAt(A, 0, 12, [{ goto: [0, 9] }, { goto: [0, 3], expectY: FL, note: "into the barn" }, { goto: [-8.6, PD.c] }, { interact: "side_door", aim: [-X + 0.05, 1.2, PD.c + 0.3], expectOpen: true }, { goto: [-X - 1.4, PD.c], note: "out by the side door" }]);
  return A;
}

// ============================================================================ 7-8. construction frames
// Unfinished reinforced-concrete frame: slabs on columns and downstand beams, a precast switchback stair outside each
// end (temporary tube rails on the landings only), partial blockwork with window openings, starter bars, formwork props,
// a tube-and-fitting scaffold along the front face. nav: a room per level and per stair storey, doors at the stair feet.
function frameBlock({ id, name, desc, fp, x0, x1, z0, z1, n, cores, walls, props, piles }) {
  const A = nt(id, name, "Modern: Construction", desc, fp, { floors: n + 1 });
  const H = 3.15, ST = 0.25, BD = 0.4, Y = (f) => 0.15 + f * H, zc = (z0 + z1) / 2, N = nav0();
  const NR = 9, RUN = 9 * 0.27, LND = 1.3, CWID = 2.6;
  const ks = cores.map((k) => ({ ...k, zA: zc - 1.9, xe: k.side === "east" ? x1 : x0, xa: k.side === "east" ? x1 : x0 - CWID }));
  const gx0 = Math.min(x0, ...ks.map((k) => k.xa)), gx1 = Math.max(x1, ...ks.map((k) => k.xa + CWID));
  A.box([gx0, -0.35, z0], [gx1, 0.15, z1], "slab_top", { bv: 0.02 });
  // columns, beams, slabs
  const nx = Math.max(2, Math.round((x1 - x0 - 0.35) / 3.7)), xs = Array.from({ length: nx + 1 }, (_, i) => x0 + 0.175 + (i * (x1 - x0 - 0.35)) / nx), zs = [z0 + 0.175, zc, z1 - 0.175];
  for (let f = 1; f <= n; f++) {
    const y = Y(f);
    A.box([x0, y - ST, z0], [x1, y, z1], f === n ? "slab_top" : "slab", { bv: 0.015 });
    for (const z of [z0, z1 - 0.3]) A.box([x0, y - ST - BD, z], [x1, y - ST, z + 0.3], "slab", { bv: 0.01 });
    for (const x of xs) A.box([x - 0.15, y - ST - BD, z0 + 0.3], [x + 0.15, y - ST, z1 - 0.3], "slab", { bv: 0.01 });
    for (const x of [x0 + 0.02, x1 - 0.02]) A.box([x - 0.02, y - ST - 0.03, z0], [x + 0.02, y - ST, z1], "slab_top", L1);   // drip line at the slab edge
  }
  for (let f = 0; f < n; f++) for (const x of xs) for (const z of zs) A.box([x - 0.175, Y(f), z - 0.175], [x + 0.175, Y(f + 1) - ST - BD, z + 0.175], "slab", { bv: 0.01 });
  // the next lift: column stubs with starter bars on the roof slab, bars sticking out of one slab edge
  const yr = Y(n);
  xs.forEach((x, i) => zs.forEach((z, j) => {
    const stub = (i + j) % 2 === 0;
    if (stub) A.box([x - 0.175, yr, z - 0.175], [x + 0.175, yr + 0.6, z + 0.175], "slab", { bv: 0.01 });
    const yb = yr + (stub ? 0.6 : 0);
    for (const [u, v] of [[-0.11, -0.11], [0.11, -0.11], [-0.11, 0.11], [0.11, 0.11]]) {
      rod(A, [x + u, yb - 0.05, z + v], [x + u * 1.15, yb + 0.95, z + v * 1.15], 0.022, "rebar", { seg: 6, lod: 1 });
      if (!stub) rod(A, [x + u * 1.15, yb + 0.95, z + v * 1.15], [x + u * 1.15 + 0.12, yb + 1.0, z + v * 1.15], 0.022, "rebar", { seg: 6, lod: 1 });
    }
    if (!stub) A.cyl([x, yb + 0.02, z], [0.36, 0.04, 0.36], "slab_top", { seg: 10, ...L1 });
  }));
  for (let z = z0 + 0.4; z < z1 - 0.3; z += 0.3) for (const y of [yr - 0.08, yr - 0.18]) rod(A, [x0 + 0.1, y, z], [x0 - 0.45, y, z], 0.018, "rebar", { seg: 6, lod: 1 });
  // stairs
  for (const k of ks) {
    const { xa, zA, xe } = k, out = k.side === "east" ? 1 : -1, xo = k.side === "east" ? xa + CWID : xa;
    for (let f = 0; f < n; f++) {
      const y = Y(f), hh = H;
      flight(A, { b0: xa + 0.1, b1: xa + 1.3, a0: zA, dir: 1, y0: y, rise: hh / 2, n: NR, run: RUN });
      A.box([xa + 0.05, y + hh / 2 - 0.2, zA + RUN], [xa + CWID, y + hh / 2, zA + RUN + LND], "stair_conc", { bv: 0.01 });
      flight(A, { b0: xa + 1.4, b1: xa + CWID, a0: zA + RUN, dir: -1, y0: y + hh / 2, rise: hh / 2, n: NR, run: RUN });
      if (f > 0) A.box([xa, y - ST, zA - LND], [xa + CWID, y, zA], "stair_conc", { bv: 0.01 });
      // temporary tube rails on the half landing's outer edges
      tubeRail(A, xa + 0.1, zA + RUN + LND - 0.05, xa + CWID - 0.05, zA + RUN + LND - 0.05, y + hh / 2, { h: 1.0, sp: 2.5 });
      tubeRail(A, xo - out * 0.05, zA + RUN, xo - out * 0.05, zA + RUN + LND - 0.05, y + hh / 2, { h: 1.0, sp: 2.5 });
      addRoom(N, `${k.tag}${f}`, xa, y, zA - LND, xa + CWID, Y(f + 1), zA + RUN + LND);
      ndoor(N, f === 0 ? "level_0" : `level_${f}`, `${k.tag}${f}`, [xe, y, zA - LND / 2]);
      ndoor(N, `${k.tag}${f}`, f + 1 === n ? "roof" : `level_${f + 1}`, [xe, Y(f + 1), zA - LND / 2]);
    }
    A.box([xa, Y(n) - ST, zA - LND], [xa + CWID, Y(n), zA], "stair_conc", { bv: 0.01 });   // roof-level landing
    for (const z of [zA - LND + 0.125, zA + RUN + LND - 0.125]) A.box([xo - 0.125 - (out > 0 ? 0.0 : -0.0), 0.15, z - 0.125], [xo + 0.125, Y(n), z + 0.125], "slab", { bv: 0.01 });
    tubeRail(A, xo - out * 0.05, zA - LND + 0.05, xo - out * 0.05, zA - 0.05, Y(n), { h: 1.0, sp: 1.2 });   // roof landing edge
  }
  // blockwork: {f, side, a0, a1, h (height), holes: [[c, w, sill, top]], ragged}
  const yb = (f) => Y(f + 1) - ST - BD;
  for (const w of walls) {
    const F = wf(w.side, x0, x1, z0, z1, 0.2), y0 = Y(w.f), y1 = Math.min(yb(w.f), y0 + (w.h ?? 9));
    const holes = (w.holes ?? []).map(([c, ww, sill, top]) => ({ a0: c - ww / 2, a1: c + ww / 2, y0: y0 + sill, y1: y0 + top }));
    for (const [a0, a1, ya, yb2] of cutRect(w.a0, w.a1, y0, y1, holes)) wbox(A, F.axis, a0, a1, ya, yb2, F.inner, F.outer, "cmu");
    for (const [c, ww, sill, top] of w.holes ?? []) {
      if (y0 + top + 0.2 <= y1) wbox(A, F.axis, c - ww / 2 - 0.15, c + ww / 2 + 0.15, y0 + top, y0 + top + 0.2, F.inner - F.out * 0.005, F.outer + F.out * 0.005, "slab", L1);   // lintel
      if (sill > 0) { wbox(A, F.axis, c - ww / 2, c + ww / 2, y0 + sill - 0.05, y0 + sill, F.inner, F.outer + F.out * 0.04, "slab", L1); winFire(N, F, c, y0); }
    }
    if (w.ragged) for (let i = 1; i <= 4; i++) wbox(A, F.axis, w.a0, w.a1 - i * 0.45, y1 + (i - 1) * 0.2, y1 + i * 0.2, F.inner, F.outer, "cmu");   // racked-back courses
    if (w.cover) N.cover.push(cover(F.axis === "x" ? [(w.a0 + w.a1) / 2, y0, F.inner - F.out * 0.55] : [F.inner - F.out * 0.55, y0, (w.a0 + w.a1) / 2], F.axis === "x" ? (F.out > 0 ? 0 : 180) : (F.out > 0 ? 90 : 270), (w.h ?? 3) > 1.35 ? "high" : "low", (w.h ?? 3) > 1.35 ? ["left", "right"] : ["over"]));
  }
  // formwork props under the roof slab in one area, plywood soffit and bearers, stacked panels and bars on the roof
  {
    const [px0, px1, pz0, pz1] = props, f = n - 1, y = Y(f), top = Y(n) - ST;
    A.box([px0, top - 0.02, pz0], [px1, top, pz1], "form_red", L1);
    for (let z = pz0 + 0.3; z < pz1; z += 1.2) A.box([px0, top - 0.24, z - 0.04], [px1, top - 0.02, z + 0.04], "ply", K0);
    for (let x = px0 + 0.3; x < px1; x += 1.2) for (let z = pz0 + 0.3; z < pz1; z += 1.2) {
      A.box([x - 0.08, y, z - 0.08], [x + 0.08, y + 0.01, z + 0.08], "prop_steel", L1);
      A.cyl([x, y + 0.85, z], [0.06, 1.7, 0.06], "prop_steel", { seg: 8 });
      A.cyl([x, (y + 1.7 + top - 0.25) / 2, z], [0.045, top - 0.25 - y - 1.7, 0.045], "galv", { seg: 8, k: 0 });
      A.box([x - 0.08, top - 0.25, z - 0.08], [x + 0.08, top - 0.24, z + 0.08], "prop_steel", L1);
      A.box([x - 0.035, y + 1.62, z - 0.006], [x + 0.035, y + 1.66, z + 0.006], "galv", L1);   // pin
    }
    const rx = px0 + 0.5, rz = pz0 + 0.5;
    for (let i = 0; i < 6; i++) A.box([rx, yr + i * 0.025, rz], [rx + 2.4, yr + 0.022 + i * 0.025, rz + 1.2], i % 2 ? "ply" : "form_red", i === 5 ? { bv: 0.003 } : K0);
    A.box([rx, yr, rz], [rx + 2.4, yr + 0.15, rz + 1.2], "ply", HID);
    for (let i = 0; i < 12; i++) rod(A, [rx + 3.0, yr + 0.05 + (i % 3) * 0.03, rz + 0.1 + Math.floor(i / 3) * 0.04], [rx + 9.0, yr + 0.05 + (i % 3) * 0.03, rz + 0.1 + Math.floor(i / 3) * 0.04], 0.02, "rebar", { seg: 6, lod: 1 });
    for (const x of [rx + 3.5, rx + 8.5]) A.box([x - 0.05, yr, rz], [x + 0.05, yr + 0.05, rz + 0.3], "ply", L1);   // bearers under the bars
  }
  // pallets of blocks on the ground floor (cover)
  for (const [bx, bz] of piles) {
    A.box([bx - 0.6, 0.15, bz - 0.5], [bx + 0.6, 0.29, bz + 0.5], "ply", { bv: 0.005 });
    for (let l = 0; l < 4; l++) A.box([bx - 0.55, 0.29 + l * 0.2, bz - 0.45], [bx + 0.55, 0.485 + l * 0.2, bz + 0.45], "cmu", { bv: 0.008 });
    A.box([bx - 0.56, 0.3, bz - 0.46], [bx + 0.56, 1.1, bz - 0.455], "glass_rail", L1);   // shrink wrap
    N.cover.push(cover([bx, 0.15, bz + 1.05], 180, "low", ["over"]), cover([bx, 0.15, bz - 1.05], 0, "low", ["over"]));
  }
  // scaffold along the front face: standards on sole boards, ledgers, transoms, boarded lifts at each slab level,
  // double guard rails and toe boards, facade braces
  {
    const zi = z1 + 0.1, zo = z1 + (z1 - z0 > 9 ? 1.35 : 0.95), sx = Array.from({ length: Math.round((x1 - x0) / 2.5) + 1 }, (_, i) => x0 + (i * (x1 - x0)) / Math.round((x1 - x0) / 2.5));
    for (const x of sx) for (const z of [zi, zo]) {
      rod(A, [x, 0.16, z], [x, Y(n) + 1.1, z], 0.048, "galv", { seg: 8, k: 1 });
      A.box([x - 0.08, 0.15, z - 0.08], [x + 0.08, 0.17, z + 0.08], "steel_grey", L1);
      A.box([x - 0.12, 0.15, z - 0.25], [x + 0.12, 0.155, z + 0.25], "scaf_board", L1);
    }
    for (let f = 1; f <= n; f++) {
      const y = Y(f);
      for (const z of [zi, zo]) rod(A, [x0 - 0.1, y - 0.1, z], [x1 + 0.1, y - 0.1, z], 0.048, "galv", { seg: 8 });
      for (const x of sx) rod(A, [x, y - 0.06, zi - 0.1], [x, y - 0.06, zo + 0.15], 0.048, "galv", { seg: 8 });
      A.box([x0, y - 0.04, z1 + 0.02], [x1, y, zo - 0.02], "scaf_board", { bv: 0.004 });
      for (let x = x0 + 3.9; x < x1 - 0.5; x += 3.9) A.box([x - 0.02, y, z1 + 0.03], [x + 0.02, y + 0.004, zo - 0.03], "galv", L1);   // board end bands
      A.box([x0, y, zo - 0.03], [x1, y + 0.15, zo], "scaf_board", K0);
      for (const dy of [0.5, 1.0]) rod(A, [x0 - 0.1, y + dy, zo], [x1 + 0.1, y + dy, zo], 0.048, "galv", { seg: 8 });
      A.box([x0, y, zo - 0.03], [x1, y + 1.05, zo + 0.03], "galv", HID);
      for (const x of [x0, x1]) A.box([x - 0.03, y, z1 + 0.02], [x + 0.03, y + 1.05, zo], "galv", HID);   // end guards
    }
    for (let i = 0; i + 1 < sx.length; i += 3) rod(A, [sx[i], 0.2, zo + 0.05], [sx[i + 1], Y(Math.min(n, 2)) - 0.2, zo + 0.05], 0.048, "galv", { seg: 8, lod: 1 });
  }
  // nav: a room per level (the roof included) and per stair storey
  for (let f = 0; f <= n; f++) addRoom(N, f === n ? "roof" : `level_${f}`, x0, Y(f), z0, x1, f === n ? Y(f) + 2.2 : Y(f + 1) - ST - BD, z1);
  for (const [p, dir] of [[[0, 0, z1 + 1.8], [0, 0, -1]], [[0, 0, z0 - 1.0], [0, 0, 1]], [[x0 + 1.5, 0, z0 - 1.0], [0, 0, 1]]]) { nent(N, p, dir, "level_0"); ndoor(N, "level_0", "outside", [p[0], 0.15, dir[2] < 0 ? z1 : z0]); }
  for (const x of xs.filter((_, i) => i % 2)) N.cover.push(cover([x, 0.15, z0 + 0.75], 0, "high", ["left", "right"]), cover([x, Y(1), zc + 0.6], 180, "high", ["left", "right"]));
  A.meta.roomTypes = {};
  A.meta.notes = "Unfinished frame: slabs use non-floor materials so the interior system never furnishes or finishes it; nav rooms per level and stair storey.";
  setNav(A, N);
  const k = ks[0], steps = [{ goto: [0, z0 - 1.0] }, { goto: [0, z0 + 1.2], expectY: 0.15, note: "onto the ground slab" }, { goto: [k.xe + (k.side === "east" ? -0.8 : 0.8), k.zA - 0.65] }];
  const l1 = k.xa + 0.7, l2 = k.xa + 2.0;
  for (let f = 0; f < n; f++) steps.push({ goto: [l1, k.zA - 0.5] }, { goto: [l1, k.zA + RUN + 0.65], expectY: Y(f) + H / 2 }, { goto: [l2, k.zA + RUN + 0.65] }, { goto: [l2, k.zA - 0.5], expectY: Y(f + 1), note: `stair reaches level ${f + 1}` });
  steps.push({ goto: [k.xe + (k.side === "east" ? -2.0 : 2.0), k.zA - 0.65], expectY: Y(n), note: "out onto the roof slab" });
  tourAt(A, 0, z0 - 4.0, steps);
  return A;
}
export function constructionFrame() {
  const x0 = -11, x1 = 11, z0 = -5.5, z1 = 5.5, H = 3.15;
  const bays = [-10.825, -7.217, -3.608, 0, 3.608, 7.217, 10.825], bc = bays.slice(1).map((b, i) => (bays[i] + b) / 2);
  return frameBlock({
    id: "mod_nt_construction_frame", name: "Unfinished apartment frame", fp: [28, 14], x0, x1, z0, z1, n: 3,
    desc: "Unfinished three-storey apartment block, 22 x 11 m frame (28 x 14 m with stairs and scaffold): in-situ concrete slabs on columns and downstand beams, ground floor, two upper floors and a roof slab all walkable; a precast switchback stair outside each end with temporary tube rails on the landings only; part-built blockwork on some bays with window openings and precast lintels; column stubs and starter bars on the roof, bars sticking out of the west slab edge; adjustable steel props and formwork under the roof slab in one corner; stacked formwork panels and bar on the roof; a boarded tube scaffold along the front face; pallets of blocks on the ground slab. Open frame: no doors, never furnished.",
    cores: [{ side: "east", tag: "stair_e" }, { side: "west", tag: "stair_w" }],
    walls: [
      { f: 0, side: "rear", a0: x0, a1: x1, holes: bc.map((c, i) => (i === 3 ? [c, 1.0, -1, 2.1] : [c, 1.5, 0.9, 2.1])) },
      { f: 0, side: "front", a0: x0, a1: bays[2], holes: [[bc[0], 1.5, 0.9, 2.1], [bc[1], 1.5, 0.9, 2.1]], cover: true, h: 3 },
      { f: 0, side: "west", a0: z0, a1: -3.25, holes: [] }, { f: 0, side: "west", a0: -1.85, a1: z1, holes: [[1.8, 1.2, 0.9, 2.1]] },
      { f: 1, side: "rear", a0: x0, a1: bays[3], holes: [[bc[0], 1.5, 0.9, 2.1], [bc[1], 1.5, 0.9, 2.1], [bc[2], 1.5, 0.9, 2.1]] },
      { f: 1, side: "rear", a0: bays[3], a1: bays[5], h: 1.2, ragged: true, cover: true, holes: [] },
      { f: 1, side: "east", a0: z0, a1: -3.25, h: 1.0, cover: true, holes: [] },
      { f: 2, side: "rear", a0: x0, a1: bays[1] + 0.2, h: 0.8, ragged: true, cover: true, holes: [] },
    ],
    props: [2.0, 9.6, -4.8, 3.0], piles: [[-5.5, 2.5], [4.5, -2.0], [7.5, 3.0]],
  });
}
export function constructionFrameSmall() {
  const x0 = -7.0, x1 = 4.4, z0 = -3.9, z1 = 3.9;
  return frameBlock({
    id: "mod_nt_construction_frame_small", name: "Unfinished two-storey frame", fp: [14, 10], x0, x1, z0, z1, n: 2,
    desc: "Unfinished two-storey building frame, 11.4 x 7.8 m (14 x 10 m with the stair and scaffold): concrete ground slab, first floor and roof slab on columns and beams, all walkable; one precast switchback stair outside the east end with temporary tube rails on the landings; part-built blockwork with window openings; starter bars and column stubs on the roof; steel props and formwork under the roof slab in one bay; a boarded tube scaffold along the front; pallets of blocks. Open frame: no doors, never furnished.",
    cores: [{ side: "east", tag: "stair" }],
    walls: [
      { f: 0, side: "rear", a0: x0, a1: x1, holes: [[-5.2, 1.4, 0.9, 2.1], [-1.4, 1.0, -1, 2.1], [2.2, 1.4, 0.9, 2.1]] },
      { f: 0, side: "west", a0: z0, a1: z1, holes: [[0, 1.4, 0.9, 2.1]] },
      { f: 1, side: "rear", a0: x0, a1: -1.3, holes: [[-5.2, 1.4, 0.9, 2.1]] },
      { f: 1, side: "rear", a0: -1.3, a1: 2.6, h: 1.0, ragged: true, cover: true, holes: [] },
    ],
    props: [-6.6, -1.4, -3.4, 2.0], piles: [[-3.5, 1.8], [1.6, -1.6]],
  });
}

// ============================================================================ 9. portacabin site office
export function siteOffice() {
  const A = nt("mod_nt_site_office", "Site office cabin", "Modern: Construction", "Portable site office cabin, 6.8 x 2.5 m: a white profiled-steel box with dark corner posts on a steel chassis and timber sleepers, a flat steel roof with a drip edge, a steel door up a galvanised step unit with handrails, three windows with steel security grilles, a split air-conditioning unit and a hook-up box. Enterable: one office room.", [7, 3]);
  const X = 3.4, z0 = -1.55, z1 = 0.95, FL = 0.55, TOP = 3.05, T = 0.12, N = nav0();
  // chassis on sleepers and pads
  for (const x of [-3.0, -1.0, 1.0, 3.0]) { A.box([x - 0.2, 0, z0 + 0.1], [x + 0.2, 0.12, z1 - 0.1], "pile", { bv: 0.01 }); }
  for (const z of [z0 + 0.35, z1 - 0.35]) A.box([-X + 0.05, 0.12, z - 0.1], [X - 0.05, 0.32, z + 0.1], "steel_grey", BV);
  A.box([-X, 0.32, z0], [X, FL - 0.02, z1], "steel_grey");
  A.box([-X + T, FL - 0.02, z0 + T], [X - T, FL, z1 - T], "floor_x");
  for (const [x, z] of [[-X, z0], [X, z0], [-X, z1], [X, z1]]) A.box([x - 0.08, 0.32, z - 0.08], [x + 0.08, TOP + 0.15, z + 0.08], "metal_panel", BV);   // corner posts
  const DX = -2.2;
  const F = shell(A, { x0: -X, x1: X, z0, z1, y0: FL - 0.02, y1: TOP, T, m: "cabin_w", ops: {
    front: (F) => [doorOn(A, F, "door", DX, FL, 0.9, 2.0, { step: false, lintel: false, sound: "steel" }), winOn(A, F, "front0_0", 0.3, FL + 0.95, 1.2, 1.0), winOn(A, F, "front0_1", 2.3, FL + 0.95, 1.2, 1.0)],
    rear: (F) => [winOn(A, F, "rear0_0", -0.6, FL + 0.95, 1.2, 1.0)],
    east: (F) => [winOn(A, F, "east0_0", -0.3, FL + 0.95, 0.8, 1.0)],
  } });
  for (const [s, c, w] of [["front", 0.3, 1.2], ["front", 2.3, 1.2], ["rear", -0.6, 1.2], ["east", -0.3, 0.8]]) {
    const Fs = F[s];
    for (let a = c - w / 2 + 0.1; a < c + w / 2 - 0.05; a += 0.12) fbox(A, Fs, a - 0.008, a + 0.008, FL + 0.9, FL + 2.0, 0.08, 0.1, "metal_panel", L1);   // security bars
    for (const y of [FL + 0.9, FL + 2.0]) fbox(A, Fs, c - w / 2 - 0.02, c + w / 2 + 0.02, y - 0.02, y + 0.02, 0.06, 0.1, "metal_panel", L1);
    winFire(N, Fs, c, FL);
  }
  A.box([-X - 0.06, TOP, z0 - 0.06], [X + 0.06, TOP + 0.15, z1 + 0.06], "cabin_w", BV);
  A.box([-X - 0.08, TOP + 0.12, z0 - 0.08], [X + 0.08, TOP + 0.17, z1 + 0.08], "metal_panel", { k: 0, bv: 0.008 });
  A.box([-X + T, TOP - 0.03, z0 + T], [X - T, TOP, z1 - T], "plaster", K0);
  A.box([-0.6, TOP - 0.05, -0.6], [0.6, TOP - 0.03, 0.0], "light_panel", L1);
  // galvanised step unit with handrails
  const sx0 = DX - 0.6, sx1 = DX + 0.6;
  A.box([sx0, 0.3, z1], [sx1, FL - 0.03, z1 + 0.42], "grille", { bv: 0.005 });
  A.box([sx0, 0.15, z1 + 0.42], [sx1, 0.36, z1 + 0.67], "grille", { bv: 0.005 });
  A.box([sx0, 0.0, z1 + 0.67], [sx1, 0.18, z1 + 0.92], "grille", { bv: 0.005 });
  for (const x of [sx0, sx1]) { A.box([x - 0.02, 0, z1 + 0.02], [x + 0.02, FL - 0.03, z1 + 0.92], "galv", L1); rod(A, [x, FL, z1 + 0.4], [x, FL + 0.95, z1 + 0.4], 0.04, "galv", { seg: 8 }); rod(A, [x, 0.18, z1 + 0.9], [x, 1.1, z1 + 0.9], 0.04, "galv", { seg: 8 }); rod(A, [x, FL + 0.95, z1 + 0.4], [x, 1.1, z1 + 0.9], 0.04, "galv", { seg: 8 }); }
  A.obox([DX, 0.25, z1 + 0.6], [1.2, 0.1, Math.hypot(0.75, 0.52)], [Math.atan2(0.52, 0.75) * DEG, 0, 0], "grille", HID);
  // services: split AC unit, hook-up box and cable, blank site board, extinguisher point
  const we = F.west;
  fbox(A, we, -1.0, -0.2, 0.6, 1.2, 0.05, 0.35, "hvac", { k: 0, bv: 0.015 });
  fbox(A, we, -0.95, -0.25, 0.65, 1.15, 0.35, 0.36, "grille", L1);
  fbox(A, we, -0.55, -0.45, 1.2, TOP - 0.4, 0.0, 0.06, "hvac", L1);
  fbox(A, we, 0.3, 0.7, 0.8, 1.3, 0.0, 0.15, "metal_panel", { k: 0, bv: 0.01 });
  rod(A, [-X - 0.08, 0.8, 0.5], [-X - 0.08, 0.05, 0.5], 0.03, "rubber", L1); rod(A, [-X - 0.08, 0.05, 0.5], [-X - 0.5, 0.02, 0.6], 0.03, "rubber", L1);
  signPanel(A, F.front, -1.4, -0.7, FL + 1.3, FL + 1.8, "sign_w", { depth: 0.03 });
  fbox(A, F.front, -1.2, -0.9, FL + 0.3, FL + 0.75, 0, 0.12, "safety_yel", L1);
  for (const s of [-1, 1]) fbox(A, F.front, -X, X, TOP + 0.02, TOP + 0.05, -0.01, 0.1, "metal_panel", L1);
  addRoom(N, "office", -X + T, FL, z0 + T, X - T, TOP - 0.05, z1 - T);
  ndoor(N, "office", "outside", [DX, FL, z1 - T / 2]); nent(N, [DX, 0, z1 + 1.5], [0, 0, -1], "office");
  N.cover.push(cover([X + 0.6, 0, -0.3], 270, "high", ["left", "right"]), cover([-X - 0.6, 0, -0.3], 90, "high", ["left", "right"]), cover([1.5, 0, z0 - 0.6], 0, "high", ["left", "right"]), cover([1.5, 0, z1 + 0.6], 180, "high", ["left", "right"]));
  A.meta.roomTypes = { office: "office" };   // "office" resolves to the modern office recipe for a mod_ host (interior.cpp modernOf)
  setNav(A, N);
  tourAt(A, DX, 4.5, [{ goto: [DX, z1 + 1.3] }, { interact: "door", aim: [DX + 0.25, FL + 1.1, z1 - T - 0.05], expectOpen: true }, { goto: [DX, z1 - 0.9], expectY: FL, note: "into the office" }, { goto: [1.5, -0.3] }]);
  return A;
}

// ============================================================================ 10. tower crane
export function towerCrane() {
  const A = nt("mod_nt_tower_crane", "Tower crane", "Modern: Construction", "Static hammerhead tower crane, about 37 m to the tower head: a cast base with a cruciform steel base frame and stacked concrete ballast blocks, a 1.6 m square yellow lattice mast to 30 m with an internal ladder and rest platforms, a slewing ring and turntable, a glazed operator cab, an A-frame tower head with pendant ties, a 40 m triangular lattice jib with a trolley and hook block, a 13 m counter-jib with walkway, hoist winch and counterweights, red warning lights. Visual only above the base: the mast is the only collider (not climbable).", [6, 6], { enterable: false, floors: 0 });
  const MT = 30.0, S = 0.75, Y0 = 0.75;
  A.box([-3, -0.6, -3], [3, 0.25, 3], "concrete_m", { bv: 0.03 });
  for (const r of [0, 90]) A.obox([0, 0.5, 0], [5.9, 0.5, 0.6], [0, r, 0], "crane_yel", { bv: 0.01 });
  for (const sx of [-1, 1]) for (const sz of [-1, 1]) for (const l of [0, 1]) for (const h of [0, 1]) {
    const xa = sx * 0.55, xb = sx * 2.85, za = sz * (0.55 + h * 1.15), zb = sz * (0.55 + h * 1.15 + 1.12);
    A.box([Math.min(xa, xb), 0.25 + l * 0.75, Math.min(za, zb)], [Math.max(xa, xb), 0.98 + l * 0.75, Math.max(za, zb)], "concrete_m", { bv: 0.02 });
    if (l === 1) A.cyl([sx * 1.7, 1.75, (za + zb) / 2], [0.12, 0.06, 0.12], "rebar", { seg: 8, ...L1 });   // lifting loops
  }
  A.box([-0.85, 0.75, -0.85], [0.85, 1.0, 0.85], "crane_yel", BV);   // mast foot
  A.box([-S - 0.05, Y0, -S - 0.05], [S + 0.05, MT, S + 0.05], "crane_yel", HID);   // the only collider above the base
  for (const [x, z] of [[-S, -S], [S, -S], [S, S], [-S, S]]) rod(A, [x, Y0, z], [x, MT, z], 0.16, "crane_yel", { seg: 8 });
  const faces = [[[-S, -S], [S, -S]], [[S, -S], [S, S]], [[S, S], [-S, S]], [[-S, S], [-S, -S]]];
  for (let y = Y0, i = 0; y < MT - 0.1; y += 2.5, i++) {
    const y1 = Math.min(MT, y + 2.5);
    for (const [[ax, az], [bx, bz]] of faces) {
      rod(A, [ax, y, az], [bx, y, bz], 0.08, "crane_yel", { seg: 6, lod: 1 });
      if (i % 2) rod(A, [ax, y, az], [bx, y1, bz], 0.07, "crane_yel", { seg: 6, lod: 1 }); else rod(A, [bx, y, bz], [ax, y1, az], 0.07, "crane_yel", { seg: 6, lod: 1 });
    }
    if (i % 3 === 2) A.box([-S + 0.05, y - 0.03, -S + 0.05], [S - 0.05, y + 0.02, S - 0.05], "grille", L1);   // rest platform
  }
  for (const x of [-0.25, 0.25]) rod(A, [x, Y0, 0.5], [x, MT, 0.5], 0.04, "galv", { seg: 6, lod: 1 });
  for (let y = Y0 + 0.3; y < MT; y += 0.6) rod(A, [-0.25, y, 0.5], [0.25, y, 0.5], 0.025, "galv", { seg: 4, lod: 1 });
  // slewing unit, turntable, cab
  A.cyl([0, MT + 0.25, 0], [2.0, 0.5, 2.0], "crane_yel", { seg: 20, k: 0 });
  A.cyl([0, MT + 0.52, 0], [2.2, 0.06, 2.2], "steel_grey", { seg: 20, ...L1 });
  A.box([-1.5, MT + 0.55, -1.2], [1.5, MT + 0.85, 1.2], "crane_yel", { k: 0, bv: 0.02 });
  A.box([0.8, MT + 0.85, 1.25], [2.5, MT + 3.0, 2.8], "crane_yel", { k: 0, bv: 0.03 });
  A.box([0.82, MT + 1.6, 2.8], [2.48, MT + 2.8, 2.83], "glass_dark", K0);
  A.box([2.5, MT + 1.6, 1.3], [2.53, MT + 2.8, 2.75], "glass_dark", K0);
  A.box([0.8, MT + 0.85, 2.83], [2.5, MT + 1.5, 2.86], "crane_yel", L1);
  A.box([0.7, MT + 3.0, 1.15], [2.6, MT + 3.08, 2.9], "steel_grey", L1);
  // tower head (A-frame) to the apex
  const AP = MT + 7.5;
  for (const [x, z] of [[-0.8, -0.8], [0.8, -0.8], [0.8, 0.8], [-0.8, 0.8]]) rod(A, [x, MT + 0.85, z], [x * 0.15, AP, z * 0.15], 0.14, "crane_yel", { seg: 8 });
  for (const y of [MT + 3.0, MT + 5.3]) { const f = 1 - (y - MT - 0.85) / (AP - MT - 0.85) * 0.85; for (const [[ax, az], [bx, bz]] of faces) rod(A, [ax / S * 0.8 * f, y, az / S * 0.8 * f], [bx / S * 0.8 * f, y, bz / S * 0.8 * f], 0.06, "crane_yel", { seg: 6, lod: 1 }); }
  A.cyl([0, AP + 0.2, 0], [0.12, 0.4, 0.12], "lamp_red", { seg: 8, k: 0 });
  A.cyl([0.3, AP + 0.5, 0], [0.03, 1.0, 0.03], "galv", { seg: 6, ...L1 });   // anemometer mast
  A.cyl([0.3, AP + 1.0, 0], [0.25, 0.06, 0.25], "galv", { seg: 8, ...L1 });
  // jib: triangular lattice along +x
  const J0 = 1.5, J1 = 40, yb = MT + 0.85, yt = MT + 2.45, hw = 0.7;
  for (const z of [-hw, hw]) rod(A, [J0, yb, z], [J1, yb, z], 0.12, "crane_yel", { seg: 6 });
  rod(A, [J0, yt, 0], [J1 - 0.8, yt, 0], 0.12, "crane_yel", { seg: 6 });
  for (let x = J0, i = 0; x < J1 - 0.5; x += 2.5, i++) {
    const xn = Math.min(J1, x + 2.5);
    rod(A, [x, yb, -hw], [x, yb, hw], 0.06, "crane_yel", { seg: 5, lod: 1 });
    for (const z of [-hw, hw]) rod(A, [x, yb, z], [Math.min(J1 - 0.8, (x + xn) / 2), yt, 0], 0.06, "crane_yel", { seg: 5, lod: 1 });
    for (const z of [-hw, hw]) rod(A, [Math.min(J1 - 0.8, (x + xn) / 2), yt, 0], [xn, yb, z], 0.06, "crane_yel", { seg: 5, lod: 1 });
  }
  A.box([J1 - 0.15, yb - 0.1, -hw - 0.1], [J1 + 0.1, yb + 0.5, hw + 0.1], "crane_yel", K0);
  A.cyl([J1, yb + 0.7, 0], [0.12, 0.3, 0.12], "lamp_red", { seg: 8, k: 0 });
  // counter-jib with walkway, rails, winch and counterweights
  const C1 = -13, yc = MT + 0.9;
  for (const z of [-0.9, 0.9]) A.box([C1, yc - 0.3, z - 0.12], [-1.4, yc, z + 0.12], "crane_yel", { k: 0, bv: 0.01 });
  for (let x = C1 + 0.3; x < -1.5; x += 1.5) A.box([x - 0.06, yc - 0.25, -0.9], [x + 0.06, yc - 0.05, 0.9], "crane_yel", L1);
  A.box([C1, yc, -0.8], [-1.4, yc + 0.03, 0.8], "grille", L1);
  for (const z of [-0.95, 0.95]) { rod(A, [C1, yc + 1.0, z], [-1.4, yc + 1.0, z], 0.04, "crane_yel", { seg: 6, lod: 1 }); for (let x = C1 + 0.2; x < -1.4; x += 2.3) rod(A, [x, yc, z], [x, yc + 1.0, z], 0.04, "crane_yel", { seg: 6, lod: 1 }); }
  A.box([-7.8, yc, -0.6], [-5.8, yc + 1.2, 0.6], "crane_yel", { k: 0, bv: 0.02 });   // hoist winch house
  A.cyl([-6.8, yc + 0.6, 0.65], [0.8, 0.1, 0.8], "steel_grey", { seg: 14, r: [90, 0, 0], ...L1 });
  for (let i = 0; i < 4; i++) A.box([C1 + 0.3 + i * 0.62, yc - 2.0, -0.85], [C1 + 0.88 + i * 0.62, yc - 0.3, 0.85], "concrete_m", { k: 0, bv: 0.02 });
  A.cyl([C1, yc + 0.3, 0], [0.12, 0.3, 0.12], "lamp_red", { seg: 8, k: 0 });
  // pendant ties, trolley, hoist ropes, hook block
  rod(A, [0, AP, 0], [24, yt, 0], 0.05, "steel_grey", { seg: 5 });
  rod(A, [0, AP, 0], [12, yt, 0], 0.05, "steel_grey", { seg: 5 });
  for (const z of [-0.9, 0.9]) rod(A, [0, AP, 0], [C1 + 0.5, yc, z], 0.05, "steel_grey", { seg: 5 });
  const TX = 17, HY = 13;
  A.box([TX - 0.8, yb - 0.45, -0.9], [TX + 0.8, yb - 0.1, 0.9], "crane_yel", { k: 0, bv: 0.01 });
  for (const x of [TX - 0.6, TX + 0.6]) for (const z of [-hw, hw]) A.cyl([x, yb - 0.05, z], [0.18, 0.08, 0.18], "steel_grey", { seg: 8, r: [90, 0, 0], ...L1 });
  for (const z of [-0.15, 0.15]) rod(A, [TX, yb - 0.45, z], [TX, HY + 0.9, z], 0.025, "steel_grey", { seg: 4 });
  A.box([TX - 0.3, HY + 0.3, -0.25], [TX + 0.3, HY + 0.95, 0.25], "crane_yel", { k: 0, bv: 0.02 });
  A.box([TX - 0.31, HY + 0.45, -0.26], [TX + 0.31, HY + 0.55, 0.26], "hazard_blk", L1);
  A.arch([TX, HY - 0.35, 0], [0.4, 0.35, 0.08], "steel_grey", { th: 0.07, r: [180, 0, 0], k: 0 });
  A.cyl([TX, HY + 0.15, 0], [0.08, 0.3, 0.08], "steel_grey", { seg: 8, k: 0 });
  setNav(A, { ...nav0("carve"), cover: [cover([0, 0, 3.6], 180, "high", ["left", "right"]), cover([0, 0, -3.6], 0, "high", ["left", "right"]), cover([3.6, 0, 0], 270, "high", ["left", "right"]), cover([-3.6, 0, 0], 90, "high", ["left", "right"])] });
  return A;
}

// ============================================================================ 11. excavation shoring
export function excavationShoring() {
  const A = nt("mod_nt_excavation_shoring", "Sheet-pile shoring", "Modern: Construction", "Trench shoring section, 8 m: interlocking steel sheet piles (rust-weathered, hazard-striped tops) standing 1.1 m proud of the ground and driven 3 m down, a steel H-section waler on welded brackets on the excavation side (+z), a scaffold-tube safety rail clamped along the top, a spoil berm on the ground side. Low cover from both sides.", [8, 2], { enterable: false, floors: 0 });
  const L = 8, n = 13, pw = L / n, TOP = 1.1, BOT = -3.0, dpan = 0.14;
  const zAt = (i) => (i % 2 ? dpan : -dpan);
  for (let i = 0; i < n; i++) {
    const xc = -L / 2 + (i + 0.5) * pw, z = zAt(i);
    A.box([xc - 0.15, BOT, z - 0.006], [xc + 0.15, TOP, z + 0.006], "sheetpile", K0);
    A.box([xc - 0.15, TOP - 0.18, z - 0.008], [xc + 0.15, TOP, z + 0.008], i % 2 ? "safety_yel" : "hazard_blk", L1);
    if (i + 1 < n) {
      const xa = xc + 0.15, xb = xc + pw - 0.15, za = z, zb = zAt(i + 1);
      seg(A, xa, za, xb, zb, BOT, TOP, 0.012, "sheetpile", K0);
      A.cyl([xb + 0.0, (BOT + TOP) / 2, zb], [0.035, TOP - BOT, 0.035], "sheetpile", { seg: 6, ...L1 });   // clutch
    }
  }
  A.box([-L / 2, BOT, -0.17], [L / 2, TOP, 0.17], "sheetpile", HID);
  // waler and brackets on the excavation side
  const zw = dpan + 0.02;
  A.box([-L / 2, 0.28, zw], [L / 2, 0.3, zw + 0.3], "steel_grey", K0); A.box([-L / 2, 0.58, zw], [L / 2, 0.6, zw + 0.3], "steel_grey", K0);
  A.box([-L / 2, 0.3, zw + 0.14], [L / 2, 0.58, zw + 0.16], "steel_grey", K0);
  A.box([-L / 2, 0.28, zw], [L / 2, 0.6, zw + 0.3], "steel_grey", HID);
  for (let x = -3.5; x <= 3.6; x += 1.75) A.box([x - 0.08, 0.1, zw - 0.01], [x + 0.08, 0.28, zw + 0.25], "steel_grey", L1);
  for (let x = -3.9; x <= 3.95; x += 0.6) A.cyl([x, 0.61, zw + 0.08], [0.04, 0.02, 0.04], "galv", { seg: 6, ...L1 });
  // safety rail clamped to the pile tops
  for (const x of [-3.7, -1.25, 1.25, 3.7]) { rod(A, [x, TOP - 0.25, -dpan - 0.05], [x, TOP + 1.0, -dpan - 0.05], 0.048, "galv", { seg: 8 }); A.box([x - 0.05, TOP - 0.3, -dpan - 0.1], [x + 0.05, TOP - 0.15, -dpan], "galv", L1); }
  for (const y of [TOP + 0.5, TOP + 1.0]) rod(A, [-L / 2, y, -dpan - 0.05], [L / 2, y, -dpan - 0.05], 0.048, "galv", { seg: 8 });
  A.box([-L / 2, TOP, -dpan - 0.08], [L / 2, TOP + 1.0, -dpan - 0.02], "galv", HID);
  for (let x = -3.5; x < 3.6; x += 1.0) A.box([x - 0.3, TOP + 0.62, -dpan - 0.09], [x + 0.3, TOP + 0.88, -dpan - 0.075], (Math.round(x + 4) % 2) ? "safety_yel" : "sign_w", L1);   // barrier boards (blank)
  // spoil berm on the ground side
  A.obox([0, 0.05, -0.75], [L, 0.35, 0.8], [8, 0, 0], "earth", K0);
  A.box([-L / 2, -0.1, -0.95], [L / 2, 0.22, -0.3], "earth", HID);
  const cv = [];
  for (const x of [-3, -1, 1, 3]) cv.push(cover([x, 0, -1.25], 0, "low", ["over"]), cover([x, 0, 0.85], 180, "low", ["over"]));
  setNav(A, { ...nav0("carve"), cover: cv });
  return A;
}

// ============================================================================ 12. boathouse (origin on the shoreline)
export function boathouse() {
  const A = nt("mod_nt_boathouse", "Timber boathouse", "Modern", "Timber boathouse, 12 x 16 m: weathered vertical board cladding on a concrete base on the land half (z < 0) and timber piles on the water half (z > 0), a dark standing-seam gable roof with its ridge running out over the water, a 6 m open boat bay in the water end over an open slip with a concrete slipway, fenders and cleats, side walkways either side of the slip, a front door in the land end, windows, a winch at the head of the slip. Enterable: boat store (workshop) and walkways at +0.6 m, a loft over the land half up a timber stair, railed along its open edge over the slip head.", [12, 16], { floors: 2 });
  const X = 6, Z = 8, T = 0.18, FL = 0.6, LOFT = 3.4, EAVE = 4.6, RIDGE = 6.4, SX = 3.3, N = nav0();
  A.box([-X, -0.5, -Z], [X, FL - 0.15, 0], "concrete_m", { bv: 0.02 });
  A.box([-X + T, FL - 0.15, -Z + T], [X - T, FL, 0], "plank_x");
  A.box([-X + 0.05, -0.5, 0], [X - 0.05, 0.05, 0.6], "concrete_m", { bv: 0.02 });   // shore wall under the slip head
  // water half: piles, bearers, walkway decks, slip edges and fenders, slipway
  for (const x of [-5.85, -SX - 0.1, SX + 0.1, 5.85]) for (const z of [0.9, 3.2, 5.5, 7.75]) {
    A.cyl([x, (FL - 0.27 - 3.0) / 2, z], [0.3, FL - 0.27 + 3.0, 0.3], "pile", { seg: 10 });
    A.cyl([x, FL - 0.27, z], [0.32, 0.03, 0.32], "galv", { seg: 10, ...L1 });
  }
  for (const s of [-1, 1]) {
    const xa = s * X, xb = s * SX;
    for (const z of [0.9, 3.2, 5.5, 7.75]) A.box([Math.min(xa, xb) - (s > 0 ? 0.2 : 0), FL - 0.42, z - 0.1], [Math.max(xa, xb) + (s < 0 ? 0.2 : 0), FL - 0.15, z + 0.1], "pile", K0);
    A.box([Math.min(xa - s * T, xb), FL - 0.15, 0], [Math.max(xa - s * T, xb), FL, Z - T], "plank_x");
    A.box([Math.min(xb, xb + s * 0.15), FL - 0.45, 0], [Math.max(xb, xb + s * 0.15), FL + 0.12, Z], "pile", BV);   // kerb beam
    for (let z = 0.6; z < Z - 0.3; z += 1.4) A.box([Math.min(xb - s * 0.1, xb), FL - 0.5, z - 0.1], [Math.max(xb - s * 0.1, xb), FL + 0.05, z + 0.1], "rubber", L1);   // fenders
    for (const z of [1.8, 4.6, 7.0]) { A.box([xb + s * 0.25 - 0.12, FL, z - 0.04], [xb + s * 0.25 + 0.12, FL + 0.08, z + 0.04], "stainless", L1); }
  }
  const sl = Math.atan2(1.8, Z) * DEG;
  A.obox([0, -1.05, Z / 2], [2 * SX - 0.2, 0.3, Math.hypot(Z, 1.8)], [sl, 0, 0], "concrete_m");
  for (const x of [-0.8, 0.8]) A.obox([x, -0.88, Z / 2], [0.08, 0.06, Math.hypot(Z, 1.8)], [sl, 0, 0], "steel_grey", L1);
  // winch at the slip head
  A.box([-0.5, FL, -1.6], [0.5, FL + 0.12, -0.9], "steel_grey", BV);
  for (const x of [-0.4, 0.4]) A.box([x - 0.04, FL + 0.12, -1.5], [x + 0.04, FL + 0.7, -1.0], "steel_grey", L1);
  A.cyl([0, FL + 0.5, -1.25], [0.32, 0.72, 0.32], "steel_grey", { seg: 14, r: [0, 0, 90], ...L1 });
  A.box([-0.55, FL, -1.6], [0.55, FL + 0.75, -0.9], "steel_grey", HID);
  // walls
  const F = shell(A, { x0: -X, x1: X, z0: -Z, z1: Z, y0: FL - 0.15, y1: EAVE, T, m: "clad_board", pl: "clad_light", ops: {
    rear: (F) => [doorOn(A, F, "front_door", 0, FL, 1.0, 2.1, { hingeSide: -1, step: false, leafMat: "clad_board", frameMat: "oak_y", sound: "wood" }), winOn(A, F, "rear0_0", -3.5, FL + 0.9, 1.2, 1.2), winOn(A, F, "rear0_1", 3.5, FL + 0.9, 1.2, 1.2), winOn(A, F, "rear1_0", 0, LOFT + 0.3, 0.9, 0.8)],
    front: () => [{ a0: -3.0, a1: 3.0, y0: -10, y1: 3.2 }],
    east: (F) => [winOn(A, F, "east0_0", -5.5, FL + 0.9, 1.2, 1.2), winOn(A, F, "east0_1", -2.0, FL + 0.9, 1.2, 1.2), winOn(A, F, "east0_2", 4.0, FL + 0.9, 1.2, 1.2), winOn(A, F, "east1_0", -4.0, LOFT + 0.35, 0.8, 0.6)],
    west: (F) => [winOn(A, F, "west0_0", -1.8, FL + 0.9, 1.2, 1.2), winOn(A, F, "west0_1", 4.0, FL + 0.9, 1.2, 1.2), winOn(A, F, "west1_0", -2.0, LOFT + 0.35, 0.8, 0.6)],
  } });
  for (const [s, c, f] of [["rear", -3.5, 0], ["rear", 3.5, 0], ["east", -5.5, 0], ["east", -2.0, 0], ["east", 4.0, 0], ["west", -1.8, 0], ["west", 4.0, 0], ["rear", 0, 1], ["east", -4.0, 1], ["west", -2.0, 1]]) winFire(N, F[s], c, f ? LOFT : FL);
  for (const s of [-1, 1]) A.box([s * 3.0 - 0.12, FL - 0.15, Z - 0.3], [s * 3.0 + 0.12, 3.35, Z + 0.02], "pile", BV);   // bay posts
  A.box([-3.15, 3.2, Z - 0.32], [3.15, 3.5, Z + 0.04], "pile", BV);                 // bay header
  for (const z of [-Z, Z]) A.prism(2, z < 0 ? -Z : Z - T, z < 0 ? -Z + T : Z, [[-X, EAVE], [X, EAVE], [0, RIDGE]], "clad_board");
  for (const s of [-1, 1]) for (let a = -X + 0.15; a < X; a += 0.6) fbox(A, s > 0 ? F.front : F.rear, a - 0.02, a + 0.02, FL - 0.1, EAVE - 0.05, -0.01, 0.025, "pile", L1);   // battens
  // roof: standing-seam slopes along x, barge boards, gutters
  const th = Math.atan2(RIDGE - EAVE, X), deg = th * DEG, OV = 0.35, LZ = 2 * Z + 0.6;
  for (const sx of [-1, 1]) {
    const L = (X + OV) / Math.cos(th), xc = sx * (X + OV) / 2, yc = RIDGE - ((X + OV) / 2) * Math.tan(th) + 0.06 / Math.cos(th);
    A.obox([xc, yc, 0], [L, 0.12, LZ], [0, 0, -sx * deg], "roof_seam");
    for (let z = -Z - 0.2; z < Z + 0.3; z += 0.5) A.obox([xc, yc + 0.07, z], [L, 0.04, 0.03], [0, 0, -sx * deg], "roof_seam", L1);
    for (const z of [-Z - 0.31, Z + 0.31]) A.obox([xc, yc - 0.08, z], [L, 0.26, 0.04], [0, 0, -sx * deg], "clad_light", K0);
    const ye = RIDGE - (X + OV) * Math.tan(th);
    A.box([sx > 0 ? X + OV - 0.02 : -X - OV - 0.14, ye - 0.15, -Z - 0.3], [sx > 0 ? X + OV + 0.14 : -X - OV + 0.02, ye, Z + 0.3], "metal_panel", K0);
  }
  A.box([-0.12, RIDGE + 0.08, -Z - 0.3], [0.12, RIDGE + 0.2, Z + 0.3], "roof_seam", K0);
  // loft floor over the land half with the stair hole, joists, railings
  const HOLE = { a0: -X + T, a1: -4.5, y0: -7.0, y1: -2.85 };
  for (const [a0, a1, b0, b1] of cutRect(-X + T, X - T, -Z + T, -0.15, [HOLE])) A.box([a0, LOFT - 0.2, b0], [a1, LOFT, b1], "floor_x");
  for (let x = -X + 0.6; x < X - 0.3; x += 1.2) if (x > HOLE.a1 + 0.1) A.box([x - 0.06, LOFT - 0.42, -Z + T], [x + 0.06, LOFT - 0.2, -0.15], "pile", K0);
  for (const x of [-3.0, 0, 3.0]) A.box([x - 0.12, FL, -0.4], [x + 0.12, LOFT - 0.2, -0.16], "pile", BV);   // posts under the loft edge
  A.box([-X + T, LOFT - 0.5, -0.4], [X - T, LOFT - 0.2, -0.15], "pile", BV);
  railing(A, "x", -X + T, X - T, -0.19, LOFT);
  railing(A, "z", HOLE.y0, HOLE.y1, HOLE.a1 + 0.035, LOFT);
  railing(A, "x", HOLE.a0, HOLE.a1, HOLE.y1 + 0.035, LOFT);
  stairs(A, { x0: -X + T + 0.02, x1: HOLE.a1 - 0.1, z0: -3.0, dir: -1, y0: FL, rise: LOFT - FL, n: 16, run: 4.0, openSide: 1 });
  for (const x of [-4.0, 0, 4.0]) A.box([x - 0.3, LOFT - 0.22, -4.0], [x + 0.3, LOFT - 0.2, -3.4], "light_panel", L1);
  A.box([-0.3, 3.2, Z - 0.6], [0.3, 3.22, Z - 0.3], "light_panel", L1);
  // lifebuoy and lamp by the door, mooring rings
  const re = F.rear;
  fbox(A, re, -1.4, -0.9, FL + 1.1, FL + 1.6, 0, 0.08, "buoy", { k: 0, bv: 0.02 });
  fbox(A, re, 0.65, 0.85, FL + 2.3, FL + 2.5, 0, 0.18, "light_panel", L1);
  A.box([-0.9, -0.05, -Z - 0.25], [0.9, FL - 0.2, -Z], "concrete_m", { bv: 0.015 });   // door steps
  A.box([-0.9, -0.05, -Z - 0.5], [0.9, FL - 0.4, -Z - 0.25], "concrete_m", { bv: 0.015 });
  // nav
  addRoom(N, "boat_store", -X + T, FL, -Z + T, X - T, LOFT - 0.2, -0.45);
  addRoom(N, "walk_w", -X + T, FL, 0, -SX, EAVE - 0.4, Z - T);
  addRoom(N, "walk_e", SX, FL, 0, X - T, EAVE - 0.4, Z - T);
  addRoom(N, "loft", -X + T, LOFT, -Z + T, X - T, LOFT + 2.2, -0.2);
  ndoor(N, "boat_store", "outside", [0, FL, -Z + T / 2]); nent(N, [0, 0, -Z - 1.2], [0, 0, 1], "boat_store");
  ndoor(N, "boat_store", "walk_w", [-4.6, FL, -0.2]); ndoor(N, "boat_store", "walk_e", [4.6, FL, -0.2]);
  ndoor(N, "boat_store", "loft", [-5.25, FL, -2.7]);
  N.cover.push(cover([0, FL, -2.0], 180, "low", ["over"]), cover([X + 0.6, 0, -4.0], 270, "high", ["left", "right"]), cover([-X - 0.6, 0, -4.0], 90, "high", ["left", "right"]));
  A.meta.roomTypes = { boat_store: "workshop", walk_w: "hall", walk_e: "hall", loft: "utility" };
  A.meta.notes = "Origin on the shoreline (z = 0 at the water's edge): z < 0 stands on land on a concrete base, z > 0 on piles over the water with an open slip. All floors at +0.6 m above the origin; loft at +3.4 m.";
  setNav(A, N);
  tourAt(A, 0, -Z - 4, [{ goto: [0, -Z - 1.0] }, { interact: "front_door", aim: [0.25, FL + 1.1, -Z + T + 0.05], expectOpen: true }, { goto: [0, -5.0], expectY: FL, note: "into the boat store" }, { goto: [-5.25, -2.6] }, { goto: [-5.25, -7.4], expectY: LOFT, note: "up to the loft" }, { goto: [0, -6.0] }]);
  return A;
}

// ============================================================================ 13. pier
export function pier() {
  const A = nt("mod_nt_pier", "Lake pier", "Modern: Waterfront", "Timber and composite lake pier, 4 x 24 m: concrete steps at the land end up to the deck 1.0 m above the ground, grey composite decking on timber stringers and pile bents every 2.8 m with cross bracing, steel-post timber-capped railings both sides along the inner 13 m, an open mooring section beyond with a timber wheel kerb, cleats, bollards, rubber pile fenders, two steel ladders down to the water, lamp posts and a lifebuoy stand. Walkable deck (nav room).", [4, 24], { enterable: false, floors: 0 });
  const HX = 2, Z0 = -12, Z1 = 12, YD = 1.0, NS = 6, GO = 0.28, zS = Z0 + NS * GO, N = nav0();
  // steps on a concrete abutment
  A.box([-HX, -0.6, Z0], [HX, 0.0, zS + 0.6], "concrete_m", { bv: 0.02 });
  for (let i = 0; i < NS; i++) A.box([-1.9, 0, Z0 + i * GO], [1.9, ((i + 1) * YD) / NS, Z0 + (i + 1) * GO], "concrete_m", { bv: 0.01 });
  for (let i = 0; i < NS; i++) A.box([-1.85, ((i + 1) * YD) / NS - 0.005, Z0 + i * GO + 0.02], [1.85, ((i + 1) * YD) / NS + 0.002, Z0 + i * GO + 0.07], "safety_yel", L1);   // nosings
  A.obox([0, YD / 2 - 0.12, Z0 + (NS * GO) / 2], [3.8, 0.2, Math.hypot(NS * GO, YD)], [-Math.atan2(YD, NS * GO) * DEG, 0, 0], "concrete_m", HID);
  for (const s of [-1, 1]) A.box([s * HX - (s > 0 ? 0.12 : 0), 0, Z0], [s * HX + (s < 0 ? 0.12 : 0), YD + 0.1, zS + 0.6], "concrete_m", { bv: 0.02 });   // cheeks
  A.box([-1.9, YD - 0.25, zS], [1.9, YD - 0.08, zS + 0.6], "concrete_m");
  // deck, fascias, stringers
  A.box([-HX, YD - 0.08, zS], [HX, YD, Z1], "deck_comp");
  for (let z = zS + 1.2; z < Z1; z += 1.2) A.box([-HX + 0.02, YD - 0.004, z - 0.004], [HX - 0.02, YD + 0.002, z + 0.004], "rubber", L1);   // board butt joints
  for (const s of [-1, 1]) A.box([s * HX - (s > 0 ? 0.03 : 0), YD - 0.32, zS + 0.6], [s * HX + (s < 0 ? 0.03 : 0), YD - 0.02, Z1], "deck_comp", K0);
  for (const x of [-1.5, -0.5, 0.5, 1.5]) A.box([x - 0.06, YD - 0.33, zS + 0.6], [x + 0.06, YD - 0.08, Z1 - 0.05], "pile", K0);
  A.box([-HX, YD - 0.32, Z1 - 0.04], [HX, YD - 0.02, Z1], "deck_comp", K0);
  // pile bents
  for (let z = zS + 1.4; z < Z1; z += 2.8) {
    for (const x of [-1.75, 1.75]) { A.cyl([x, (YD - 0.6 - 4.0) / 2, z], [0.32, YD - 0.6 + 4.0, 0.32], "pile", { seg: 10 }); A.box([x - 0.08, YD - 0.6, z - 0.3], [x + 0.08, YD - 0.33, z + 0.3], "pile", K0); }
    A.box([-1.95, YD - 0.6, z - 0.12], [1.95, YD - 0.33, z + 0.12], "pile", BV);
    for (const s of [-1, 1]) A.obox([0, -0.4, z + s * 0.17], [3.6, 0.15, 0.06], [0, 0, s * 18], "pile", L1);   // cross bracing
    for (const x of [-1.92, 1.92]) A.box([x - 0.04, -0.6, z - 0.1], [x + 0.04, YD - 0.1, z + 0.1], "rubber", L1);
  }
  // railings on the inner section, open mooring section beyond
  const zR = 1.0;
  for (const s of [-1, 1]) {
    const x = s * (HX - 0.08);
    for (let z = zS + 0.1; z <= zR + 0.01; z += 1.5) { A.box([x - 0.03, YD, z - 0.03], [x + 0.03, YD + 1.05, z + 0.03], "steel_grey", { k: 0, bv: 0.005 }); A.box([x - 0.06, YD, z - 0.06], [x + 0.06, YD + 0.01, z + 0.06], "steel_grey", L1); }
    A.box([x - 0.07, YD + 1.05, zS], [x + 0.07, YD + 1.1, zR], "clad_light", { k: 0, bv: 0.008 });
    for (const y of [0.4, 0.75]) rod(A, [x, YD + y, zS], [x, YD + y, zR], 0.03, "stainless", { seg: 6 });
    A.box([x - 0.04, YD, zS], [x + 0.04, YD + 1.1, zR], "steel_grey", HID);
    A.box([x - (s > 0 ? 0.12 : 0), YD, zR], [x + (s < 0 ? 0.12 : 0) + (s > 0 ? 0.08 : -0.08) * 0, YD + 0.12, Z1], "pile", { bv: 0.01 });   // wheel kerb
    for (const z of [3.5, 6.5, 9.5]) { A.box([s * 1.7 - 0.13, YD, z - 0.04], [s * 1.7 + 0.13, YD + 0.03, z + 0.04], "stainless", L1); A.box([s * 1.7 - 0.03, YD + 0.03, z - 0.03], [s * 1.7 + 0.03, YD + 0.1, z + 0.03], "stainless", L1); A.box([s * 1.7 - 0.16, YD + 0.1, z - 0.04], [s * 1.7 + 0.16, YD + 0.13, z + 0.04], "stainless", L1); }
    for (const z of [2.0, 11.4]) { A.cyl([s * 1.55, YD + 0.25, z], [0.26, 0.5, 0.26], "iron", { seg: 12 }); A.cyl([s * 1.55, YD + 0.52, z], [0.34, 0.06, 0.34], "iron", { seg: 12, ...L1 }); }
    const zl = 8.0;   // ladder down to the water
    for (const dz of [-0.22, 0.22]) rod(A, [s * (HX + 0.06), YD + 0.9, zl + dz], [s * (HX + 0.06), -1.6, zl + dz], 0.045, "stainless", { seg: 8 });
    for (let y = YD - 0.2; y > -1.5; y -= 0.3) rod(A, [s * (HX + 0.06), y, zl - 0.22], [s * (HX + 0.06), y, zl + 0.22], 0.03, "stainless", { seg: 6, lod: 1 });
  }
  for (const z of [-6.0, 3.5, 10.5]) {
    const x = z < 0 ? 1.85 : -1.85;
    A.cyl([x, YD + 1.75, z], [0.1, 3.5, 0.1], "metal_panel", { seg: 10 });
    A.box([x - 0.15, YD + 3.5, z - 0.2], [x + 0.15, YD + 3.62, z + 0.2], "metal_panel", { k: 0, bv: 0.01 });
    A.box([x - 0.12, YD + 3.48, z - 0.16], [x + 0.12, YD + 3.5, z + 0.16], "light_panel", L1);
  }
  rod(A, [-1.8, YD, -2.0], [-1.8, YD + 1.4, -2.0], 0.06, "metal_panel", { seg: 8 });   // lifebuoy stand
  A.box([-1.92, YD + 0.85, -2.25], [-1.86, YD + 1.35, -1.75], "buoy", { k: 0, bv: 0.03 });
  addRoom(N, "pier", -1.9, YD, zS, 1.9, YD + 2.2, Z1 - 0.1);
  ndoor(N, "pier", "outside", [0, YD, zS]); nent(N, [0, 0, Z0 - 0.8], [0, 0, 1], "pier");
  N.cover.push(cover([1.55, YD, 2.6], 180, "low", ["over"]), cover([-1.55, YD, 10.8], 0, "low", ["over"]), cover([-HX - 0.5, 0, Z0 + 0.8], 90, "low", ["over"]));
  setNav(A, N);
  A.meta.notes = "Land end at z = -12 (steps from the ground); the deck runs out along +z over the water, 1.0 m above the origin.";
  return A;
}

// ============================================================================ 14. floating finger pontoon
export function pontoonFinger() {
  const A = nt("mod_nt_pontoon_finger", "Pontoon finger", "Modern: Waterfront", "Floating marina finger pontoon, 2 x 10 m: composite decking flush with the placement height on an aluminium frame with rubber rubbing strakes, black polyethylene float drums below, six stainless cleats, a utility pedestal (blank panels) half way along, a hinged connection plate at the land end (-z) and a pile guide hoop round a steel pile at the far end. Walkable (nav room).", [2, 10], { enterable: false, floors: 0 });
  const HX = 1.0, HZ = 5.0, N = nav0();
  A.box([-HX + 0.06, -0.06, -HZ], [HX - 0.06, 0, HZ], "deck_comp");
  for (let z = -HZ + 1.2; z < HZ; z += 1.2) A.box([-HX + 0.08, -0.004, z - 0.004], [HX - 0.08, 0.002, z + 0.004], "rubber", L1);
  for (const s of [-1, 1]) {
    A.box([s * HX - (s > 0 ? 0.06 : 0), -0.25, -HZ], [s * HX + (s < 0 ? 0.06 : 0), 0.02, HZ], "alu", BV);
    A.box([s * HX + (s > 0 ? 0 : -0.06), -0.2, -HZ + 0.05], [s * HX + (s > 0 ? 0.06 : 0), -0.05, HZ - 0.05], "rubber", { k: 0, bv: 0.02 });
    for (const z of [-3.8, 0.0, 3.8]) { A.box([s * 0.8 - 0.13, 0, z - 0.04], [s * 0.8 + 0.13, 0.03, z + 0.04], "stainless", L1); A.box([s * 0.8 - 0.03, 0.03, z - 0.03], [s * 0.8 + 0.03, 0.1, z + 0.03], "stainless", L1); A.box([s * 0.8 - 0.16, 0.1, z - 0.04], [s * 0.8 + 0.16, 0.13, z + 0.04], "stainless", L1); }
  }
  for (const z of [-HZ + 0.03, HZ - 0.03]) A.box([-HX, -0.25, z - 0.03], [HX, 0.02, z + 0.03], "alu", K0);
  for (let z = -HZ + 1.0; z < HZ; z += 2.0) A.box([-HX + 0.05, -0.25, z - 0.05], [HX - 0.05, -0.08, z + 0.05], "alu", L1);
  for (const z of [-3.6, -1.2, 1.2, 3.6]) A.rbox([-0.85, -0.6, z - 1.0], [0.85, -0.08, z + 1.0], 0.12, "float_hdpe", K0);
  A.box([-0.4, 0, 0.5], [0.4, 0.25, 0.9], "metal_panel", L1);   // pedestal footing
  A.box([-0.15, 0.25, 0.55], [0.15, 1.1, 0.85], "panel_cab", { bv: 0.02 });
  A.box([-0.16, 0.8, 0.54], [0.16, 1.0, 0.55], "metal_panel", L1);
  A.box([-0.08, 1.1, 0.62], [0.08, 1.14, 0.78], "light_panel", L1);
  A.box([-0.9, -0.03, -HZ - 0.25], [0.9, 0.0, -HZ + 0.02], "steel_plate", { bv: 0.004 });   // connection plate
  for (const x of [-0.7, 0.7]) A.cyl([x, -0.06, -HZ], [0.08, 0.1, 0.08], "galv", { seg: 8, r: [0, 0, 90], ...L1 });
  A.cyl([0, -0.5, HZ + 0.25], [0.36, 4.0, 0.36], "galv", { seg: 14 });
  A.cyl([0, 1.53, HZ + 0.25], [0.38, 0.05, 0.38], "galv", { seg: 14, ...L1 });
  A.arch([0, -0.12, HZ + 0.02], [0.6, 0.45, 0.12], "alu", { th: 0.06, r: [90, 0, 0], k: 0 });
  addRoom(N, "pontoon", -0.95, 0, -HZ, 0.95, 2.0, HZ);
  ndoor(N, "pontoon", "outside", [0, 0, -HZ]); nent(N, [0, 0, -HZ - 0.8], [0, 0, 1], "pontoon");
  setNav(A, N);
  A.meta.notes = "Deck top at the placement height (y = 0): place on the water surface; the land end (-z) meets the pier side.";
  return A;
}

// ============================================================================ 15-16. motor boat and boat on trailer
// a small centre-console motor boat, keel at (ox, oy, oz), bow toward +z, 6.5 m long including the outboard
function boat(A, ox, oy, oz) {
  const b = (mn, mx, m, o) => A.box([mn[0] + ox, mn[1] + oy, mn[2] + oz], [mx[0] + ox, mx[1] + oy, mx[2] + oz], m, o);
  const ob = (c, s, r, m, o) => A.obox([c[0] + ox, c[1] + oy, c[2] + oz], s, r, m, o);
  const cy = (c, s, m, o) => A.cyl([c[0] + ox, c[1] + oy, c[2] + oz], s, m, o);
  const pr = (axis, a0, a1, tri, m, o) => A.prism(axis, a0 + (axis === 0 ? ox : axis === 1 ? oy : oz), a1 + (axis === 0 ? ox : axis === 1 ? oy : oz), tri.map(([u, v]) => (axis === 0 ? [u + oz, v + oy] : axis === 1 ? [u + ox, v + oz] : [u + ox, v + oy])), m, o);
  const B = 1.15, ZT = -3.0, ZB = 1.4, ZN = 3.1, YC = 0.35, YG = 1.05;
  pr(2, ZT, ZB, [[-B + 0.03, YC], [B - 0.03, YC], [0, 0]], "antifoul");
  pr(1, 0.12, YC, [[-B + 0.15, ZB], [B - 0.15, ZB], [0, ZN - 0.15]], "antifoul");
  pr(1, 0.0, 0.12, [[-0.45, ZB], [0.45, ZB], [0, 2.4]], "antifoul");
  pr(0, -0.05, 0.05, [[ZB, 0.0], [ZN, YG - 0.25], [ZB, YG - 0.25]], "antifoul", K0);
  for (const s of [-1, 1]) {
    b([s > 0 ? B - 0.06 : -B, YC, ZT], [s > 0 ? B : -B + 0.06, YG, ZB], "hull_w");
    b([s > 0 ? B : -B - 0.008, YC + 0.08, ZT], [s > 0 ? B + 0.008 : -B, YC + 0.3, ZB], "hull_blue", L1);
    b([s > 0 ? B - 0.14 : -B - 0.02, YG, ZT], [s > 0 ? B + 0.02 : -B + 0.14, YG + 0.04, ZB], "hull_w", { k: 0, bv: 0.01 });
    b([s > 0 ? B : -B - 0.05, YG - 0.08, ZT], [s > 0 ? B + 0.05 : -B, YG - 0.01, ZB], "rubber", L1);
    const dx = -s * B, dz = ZN - ZB, L = Math.hypot(dx, dz), yaw = Math.atan2(-dz, -s * B) * DEG;
    ob([s * B / 2, YC + 0.19, (ZB + ZN) / 2], [L, 0.22, 0.012], [0, yaw, 0], "hull_blue", L1);
    ob([s * B / 2 + s * 0.012, YG - 0.045, (ZB + ZN) / 2], [L, 0.07, 0.05], [0, yaw, 0], "rubber", L1);
    for (const t of [0.2, 0.55]) cy([s * B * (1 - t) - s * 0.1, YG + 0.35, ZB + dz * t], [0.025, 0.7, 0.025], "stainless", { seg: 6, ...L1 });
    ob([s * B * 0.6 - s * 0.1, YG + 0.7, ZB + dz * 0.38], [L * 0.6, 0.025, 0.025], [0, yaw, 0], "stainless", L1);
    b([s * 0.95 - 0.1, YG + 0.04, -2.7], [s * 0.95 + 0.1, YG + 0.08, -2.62], "stainless", L1);   // stern cleats
    b([s * B * 0.7 - 0.05, YG - 0.25, ZB + 0.5], [s * B * 0.7 + 0.05, YG - 0.15, ZB + 0.6], s < 0 ? "lamp_red" : "lamp_green", L1);
  }
  pr(1, YC, YG, [[-B, ZB], [B, ZB], [0, ZN]], "hull_w");
  pr(1, YG, YG + 0.02, [[-B + 0.15, ZB + 0.05], [B - 0.15, ZB + 0.05], [0, ZN - 0.25]], "deck_grip", K0);
  b([-0.05, YG, ZN - 0.6], [0.05, YG + 0.07, ZN - 0.45], "stainless", L1);   // bow cleat
  b([-B, 0.25, ZT - 0.08], [B, YG - 0.05, ZT], "hull_w");
  b([-B + 0.06, YC, ZT], [B - 0.06, YC + 0.1, ZB], "deck_grip");
  // console, screen, wheel, seats
  b([-0.42, YC + 0.1, 0.05], [0.42, YC + 1.0, 0.75], "hull_w", { bv: 0.03 });
  ob([0, YC + 1.05, 0.3], [0.84, 0.12, 0.55], [-12, 0, 0], "hull_w", { k: 0, bv: 0.02 });
  ob([0, YC + 1.32, 0.62], [0.84, 0.4, 0.015], [-25, 0, 0], "glass", K0);
  ob([0, YC + 1.32, 0.63], [0.88, 0.44, 0.01], [-25, 0, 0], "stainless", L1);
  ob([0, YC + 1.08, 0.36], [0.5, 0.012, 0.25], [-12, 0, 0], "screen", L1);
  cy([0.1, YC + 1.08, 0.0], [0.34, 0.03, 0.34], "motor", { seg: 14, r: [-70, 0, 0], ...L1 });
  b([0.3, YC + 1.0, 0.1], [0.36, YC + 1.18, 0.2], "motor", L1);
  b([-0.5, YC + 0.1, -1.05], [0.5, YC + 0.55, -0.65], "hull_w", { bv: 0.03 });
  b([-0.48, YC + 0.55, -1.05], [0.48, YC + 0.66, -0.67], "vinyl_w", { k: 0, bv: 0.03 });
  b([-0.48, YC + 0.66, -1.08], [0.48, YC + 1.05, -0.98], "vinyl_w", { k: 0, bv: 0.03 });
  b([-B + 0.08, YC + 0.1, ZT + 0.05], [B - 0.08, YC + 0.5, ZT + 0.55], "hull_w", { bv: 0.03 });
  b([-B + 0.1, YC + 0.5, ZT + 0.07], [B - 0.1, YC + 0.6, ZT + 0.53], "vinyl_w", { k: 0, bv: 0.03 });
  cy([0.75, YG + 0.5, ZT + 0.2], [0.03, 1.0, 0.03], "stainless", { seg: 6, ...L1 });
  cy([0.75, YG + 1.02, ZT + 0.2], [0.07, 0.06, 0.07], "light_panel", { seg: 8, ...L1 });
  // outboard
  b([-0.18, YG - 0.45, ZT - 0.2], [0.18, YG - 0.1, ZT - 0.08], "motor", L1);
  A.rbox([ox - 0.27, oy + YG - 0.1, oz + ZT - 0.72], [ox + 0.27, oy + YG + 0.55, oz + ZT - 0.18], 0.1, "motor", K0);
  b([-0.28, YG - 0.12, ZT - 0.74], [0.28, YG - 0.05, ZT - 0.16], "steel_grey", L1);
  b([-0.12, 0.25, ZT - 0.6], [0.12, YG - 0.1, ZT - 0.3], "motor", K0);
  b([-0.08, -0.35, ZT - 0.6], [0.08, 0.25, ZT - 0.36], "motor", K0);
  b([-0.2, 0.1, ZT - 0.72], [0.2, 0.13, ZT - 0.3], "motor", L1);
  cy([0, -0.22, ZT - 0.66], [0.11, 0.14, 0.11], "steel_grey", { seg: 8, r: [90, 0, 0], ...L1 });
  for (let i = 0; i < 3; i++) ob([0, -0.22, ZT - 0.7], [0.03, 0.36, 0.12], [0, 0, i * 120], "steel_grey", L1);
  b([-0.28, 0.0, ZT - 0.75], [0.28, YG + 0.6, ZT - 0.08], "motor", HID);
}
export function boatMotor() {
  const A = nt("mod_nt_boat_motor", "Small motor boat", "Modern: Waterfront", "Small centre-console motor boat, 6.5 x 2.3 m: white gelcoat hull with a blue boot stripe and a red antifouled V bottom, a self-draining cockpit with a console, windscreen, wheel and blank display, a leaning-post seat and a stern bench, bow rails, cleats, navigation lights and a black outboard motor. Placed afloat: the origin is the waterline (keel 0.3 m below).", [2.4, 6.5], { enterable: false, floors: 0 });
  boat(A, 0, -0.3, 0.05);
  setNav(A, { ...nav0("carve"), cover: [cover([1.75, 0, -0.5], 270, "low", ["over"]), cover([-1.75, 0, -0.5], 90, "low", ["over"]), cover([0, 0, -4.0], 0, "low", ["over"])] });
  return A;
}
export function boatTrailer() {
  const A = nt("mod_nt_boat_trailer", "Boat on trailer", "Modern: Waterfront", "The small motor boat on a galvanised single-axle road trailer, 7.5 x 2.6 m: box-section main rails and A-frame drawbar with coupling and jockey wheel, a winch post with bow stop, carpeted bunks and keel rollers, mudguards over two road wheels, a rear lighting board with a blank plate. High cover.", [2.6, 7.5], { enterable: false, floors: 0 });
  const KY = 0.78, oz = -0.25;
  boat(A, 0, KY, oz);
  for (const s of [-1, 1]) {
    A.box([s * 0.55 - 0.05, 0.46, -3.2], [s * 0.55 + 0.05, 0.58, 2.6], "galv", BV);
    seg(A, s * 0.55, 2.6, 0, 3.55, 0.46, 0.58, 0.1, "galv", BV);
    A.box([s * 0.62 - 0.05, KY + 0.12, -2.8], [s * 0.62 + 0.05, KY + 0.2, 0.9], "hay_wrap", { k: 0, bv: 0.01 });   // carpeted bunks
    for (const z of [-2.4, -0.6, 0.6]) A.box([s * 0.62 - 0.03, 0.58, z - 0.03], [s * 0.62 + 0.03, KY + 0.12, z + 0.03], "galv", L1);
    A.cyl([s * 1.12, 0.33, -0.9], [0.66, 0.2, 0.66], "rubber", { seg: 18, r: [0, 0, 90] });
    A.cyl([s * 1.13, 0.33, -0.9], [0.42, 0.21, 0.42], "galv", { seg: 14, r: [0, 0, 90], ...L1 });
    A.arch([s * 1.12, 0.5, -0.9], [0.86, 0.42, 0.26], "galv", { th: 0.02, r: [0, 90, 0], k: 0 });
    A.box([s * 0.55 - 0.04, 0.3, -1.5], [s * 0.55 + 0.04, 0.46, -0.3], "steel_grey", L1);   // spring
    A.box([s * 0.6 - 0.12, 0.52, -3.25], [s * 0.6 + 0.12, 0.66, -3.15], "lamp_red", L1);
  }
  rod(A, [-1.15, 0.33, -0.9], [1.15, 0.33, -0.9], 0.07, "galv", { seg: 8 });
  for (const z of [-2.6, -1.4, -0.1, 1.4]) { A.box([-0.55, 0.46, z - 0.04], [0.55, 0.56, z + 0.04], "galv", L1); A.cyl([0, KY - 0.05, z], [0.12, 0.2, 0.12], "rubber", { seg: 10, r: [0, 0, 90], ...L1 }); }
  A.box([-0.75, 0.48, -3.3], [0.75, 0.6, -3.2], "galv", K0);
  A.box([-0.18, 0.48, -3.34], [0.18, 0.6, -3.3], "sign_w", L1);
  A.box([-0.06, 0.4, 3.5], [0.06, 0.56, 3.75], "galv", BV);   // coupling
  A.cyl([0, 0.56, 3.68], [0.12, 0.06, 0.12], "galv", { seg: 10, ...L1 });
  rod(A, [0.12, 0.1, 3.2], [0.12, 0.8, 3.2], 0.06, "galv", { seg: 8, k: 1 });
  A.cyl([0.12, 0.08, 3.2], [0.16, 0.06, 0.16], "rubber", { seg: 10, r: [0, 0, 90], ...L1 });
  A.box([-0.05, 0.56, 2.95], [0.05, KY + 0.75, 3.05], "galv", BV);   // winch post
  A.box([-0.1, KY + 0.6, 2.85], [0.1, KY + 0.75, 2.95], "rubber", L1);
  A.cyl([0.0, KY + 0.35, 3.1], [0.14, 0.18, 0.14], "steel_grey", { seg: 10, r: [0, 0, 90], ...L1 });
  rod(A, [0, KY + 0.35, 3.0], [0, KY + 0.62, 2.82], 0.02, "safety_yel", L1);
  A.box([-1.25, 0, -3.6], [1.25, KY + 1.15, 3.0], "galv", HID);
  setNav(A, { ...nav0("carve"), cover: [cover([1.9, 0, -1.5], 270, "high", ["left", "right"]), cover([-1.9, 0, -1.5], 90, "high", ["left", "right"]), cover([1.9, 0, 1.2], 270, "high", ["left", "right"]), cover([-1.9, 0, 1.2], 90, "high", ["left", "right"])] });
  return A;
}

// ============================================================================ 17. promenade railing bay
export function promenadeRail() {
  const A = nt("mod_nt_promenade_rail", "Promenade railing", "Modern: Waterfront", "Lakeside promenade railing bay, 6 m along x: a precast concrete upstand 0.55 m high with a bevelled coping, galvanised steel posts on bolted base plates every 2 m, a round top rail at 1.1 m, a mid rail and vertical bar infill. Bays tile end to end. Low cover from both sides.", [6, 0.6], { enterable: false, floors: 0 });
  const HX = 3, UP = 0.55, TOP = 1.1;
  A.box([-HX, -0.15, -0.15], [HX, UP, 0.15], "conc_panel", { bv: 0.01 });
  A.box([-HX, UP, -0.2], [HX, UP + 0.08, 0.2], "kerb", { bv: 0.015 });
  A.box([-HX, -0.02, -0.25], [HX, 0.02, 0.25], "kerb", L1);
  for (const x of [-HX + 0.005, HX - 0.005]) A.box([x - 0.004, -0.15, -0.151], [x + 0.004, UP, 0.151], "membrane", L1);   // joints
  const yb = UP + 0.08;
  for (const x of [-2, 0, 2]) {
    A.box([x - 0.03, yb, -0.03], [x + 0.03, TOP, 0.03], "galv", { k: 0, bv: 0.005 });
    A.box([x - 0.08, yb, -0.08], [x + 0.08, yb + 0.012, 0.08], "galv", L1);
    for (const [u, v] of [[-0.055, -0.055], [0.055, -0.055], [-0.055, 0.055], [0.055, 0.055]]) A.cyl([x + u, yb + 0.02, v], [0.02, 0.02, 0.02], "steel_grey", { seg: 6, ...L1 });
  }
  rod(A, [-HX, TOP, 0], [HX, TOP, 0], 0.05, "galv", { seg: 10 });
  rod(A, [-HX, yb + 0.1, 0], [HX, yb + 0.1, 0], 0.03, "galv", { seg: 8 });
  rod(A, [-HX, TOP - 0.1, 0], [HX, TOP - 0.1, 0], 0.03, "galv", { seg: 8 });
  for (let x = -HX + 0.06; x < HX; x += 0.12) if ([-2, 0, 2].every((p) => Math.abs(x - p) > 0.05)) A.box([x - 0.007, yb + 0.1, -0.007], [x + 0.007, TOP - 0.1, 0.007], "galv", L1);
  A.box([-HX, yb, -0.03], [HX, TOP, 0.03], "galv", HID);
  const cv = [];
  for (const x of [-2.0, 0, 2.0]) cv.push(cover([x, 0, 0.6], 180, "low", ["over"]), cover([x, 0, -0.6], 0, "low", ["over"]));
  setNav(A, { ...nav0("carve"), cover: cv });
  return A;
}

// ============================================================================ 18. drainage pumping station
function pump(A, x, z, y, zOut) {
  A.box([x - 0.8, y, z - 0.7], [x + 0.8, y + 0.3, z + 0.7], "concrete_m", BV);
  A.cyl([x, y + 0.65, z], [1.1, 0.7, 1.1], "pump_green", { seg: 18 });
  A.cyl([x, y + 1.0, z], [1.2, 0.05, 1.2], "pump_green", { seg: 18, ...L1 });
  A.cyl([x, y + 1.25, z], [0.7, 0.5, 0.7], "pump_green", { seg: 14 });
  A.cyl([x, y + 2.0, z], [0.82, 1.0, 0.82], "pipe_blue", { seg: 16 });
  for (let yy = y + 1.6; yy < y + 2.45; yy += 0.12) A.cyl([x, yy, z], [0.88, 0.025, 0.88], "pipe_blue", { seg: 16, ...L1 });   // cooling fins
  A.cyl([x, y + 2.6, z], [0.86, 0.2, 0.86], "grille", { seg: 16, k: 0 });
  A.box([x + 0.38, y + 1.8, z - 0.15], [x + 0.6, y + 2.15, z + 0.15], "pipe_blue", L1);   // terminal box
  rod(A, [x + 0.6, y + 1.95, z], [x + 0.9, y + 4.2, z], 0.06, "rubber", L1);
  for (const s of [-1, 1]) A.box([x - 0.06, y + 2.7, z + s * 0.2 - 0.02], [x + 0.06, y + 2.85, z + s * 0.2 + 0.02], "steel_grey", L1);
  for (const [u, v] of [[-0.65, -0.55], [0.65, -0.55], [-0.65, 0.55], [0.65, 0.55]]) A.cyl([x + u, y + 0.32, z + v], [0.05, 0.05, 0.05], "galv", { seg: 6, ...L1 });
  // discharge: horizontal main to the front wall with flanges, a non-return valve and a gate valve with a hand wheel
  const yp = y + 0.65, za = z + 0.5, L = zOut - za;
  A.cyl([x, yp, (za + zOut) / 2], [0.5, L, 0.5], "pipe_blue", { seg: 14, r: [90, 0, 0] });
  for (let zz = za + 0.1; zz < zOut; zz += 1.1) A.cyl([x, yp, zz], [0.64, 0.05, 0.64], "pipe_blue", { seg: 14, r: [90, 0, 0], ...L1 });
  A.box([x - 0.38, yp - 0.38, z + 1.0], [x + 0.38, yp + 0.38, z + 1.5], "pump_green", { bv: 0.03 });
  A.box([x - 0.3, yp - 0.3, z + 1.8], [x + 0.3, yp + 0.55, z + 2.2], "pump_green", { bv: 0.03 });
  A.cyl([x, yp + 0.85, z + 2.0], [0.05, 0.6, 0.05], "steel_grey", { seg: 6, ...L1 });
  A.cyl([x, yp + 1.15, z + 2.0], [0.45, 0.03, 0.45], "safety_yel", { seg: 14, ...L1 });
  A.box([x - 0.06, y, z + 2.55], [x + 0.06, yp - 0.25, z + 2.65], "steel_grey", L1);
  A.box([x - 0.35, yp - 0.27, z + 2.55], [x + 0.35, yp - 0.23, z + 2.65], "steel_grey", L1);
  A.box([x - 0.5, y, z - 1.7], [x + 0.5, y + 0.012, z - 0.9], "steel_plate", L1);   // wet-well hatch
  for (let u = -0.4; u < 0.45; u += 0.2) A.box([x + u - 0.02, y + 0.012, z - 1.65], [x + u + 0.02, y + 0.018, z - 0.95], "grille", L1);
}
export function pumpStation() {
  const A = nt("mod_nt_pump_station", "Drainage pumping station", "Modern", "Drainage pumping station, 13 x 7.4 m and 5 m high: brick walls on a concrete plinth, a flat roof behind a brick parapet with an aluminium coping, high-level windows, a sectional roller door and a steel personnel door. Inside: a pump room with three large vertical pumps on plinths (volutes, finned motors, wet-well hatches), blue discharge mains with valves and hand wheels running out through the front wall, an overhead runway beam with a chain hoist; a control room with a fixed run of control cabinets behind a partition with a door. Outside on the +z side the three outfall pipes drop into a concrete headwall with flap valves facing the channel, behind a tube rail.", [14, 10], { floors: 1 });
  const x0 = -6.5, x1 = 6.5, z0 = -4.8, z1 = 2.6, T = 0.35, FL = GF, TOP = 5.0, XP = 2.3, N = nav0();
  A.box([x0 - 0.05, -0.6, z0 - 0.05], [x1 + 0.05, FL - 0.05, z1 + 0.05], "concrete_m", { bv: 0.02 });
  A.box([x0 + T, FL - 0.05, z0 + T], [x1 - T, FL, z1 - T], "concrete_floor");
  const RD = { c: -1.4, w: 3.2, h: 3.6 }, PD = 4.4;
  const F = shell(A, { x0, x1, z0, z1, y0: FL - 0.05, y1: TOP, T, m: "brick_slip", ops: {
    west: (F) => [upDoor(A, F, { id: "roller_door", c: RD.c, floor: FL, w: RD.w, h: RD.h })],
    rear: (F) => [winOn(A, F, "rear0_0", -4.6, FL + 2.7, 1.6, 1.0), winOn(A, F, "rear0_1", -1.2, FL + 2.7, 1.6, 1.0), doorOn(A, F, "door", PD, FL, 1.0, 2.2, { hingeSide: -1, steel: true, frameMat: "alu", leafMat: "door_paint" })],
    east: (F) => [winOn(A, F, "east0_0", -1.2, FL + 0.9, 1.4, 1.2)],
    front: (F) => [winOn(A, F, "front0_0", 4.3, FL + 0.9, 1.4, 1.2), winOn(A, F, "front0_1", -3.4, FL + 2.7, 1.2, 1.0)],
  } });
  for (const [s, c, sill] of [["rear", -4.6, 2.7], ["rear", -1.2, 2.7], ["east", -1.2, 0.9], ["front", 4.3, 0.9], ["front", -3.4, 2.7]]) if (sill < 1.5) winFire(N, F[s], c, FL);
  A.box([x0, TOP, z0], [x1, TOP + 0.25, z1], "concrete_m");
  flatRoof(A, x0, x1, z0, z1, TOP + 0.25, { ph: 0.6, t: 0.3, m: "brick_slip", vents: [[-4.6, -3.0], [-2.1, -3.0], [0.4, -3.0]] });
  for (const s of SIDES) { const Fs = F[s], ax = Fs.axis === "x"; fbox(A, Fs, ax ? x0 - 0.03 : z0, ax ? x1 + 0.03 : z1, -0.02, 0.5, -0.01, 0.03, "concrete_m", { k: 0, bv: 0.008 }); fbox(A, Fs, ax ? x0 - 0.03 : z0, ax ? x1 + 0.03 : z1, TOP - 0.4, TOP - 0.25, -0.01, 0.04, "concrete_m", { k: 0, bv: 0.008 }); }
  for (const s of [-1, 1]) A.box([s > 0 ? x1 - 0.2 : x0 + 0.05, 0.05, z0 - 0.12], [s > 0 ? x1 - 0.05 : x0 + 0.2, TOP + 0.3, z0 - 0.03], "metal_panel", L1);   // downpipes
  fbox(A, F.rear, 3.2, 3.9, FL + 1.0, FL + 2.0, 0, 0.3, "panel_cab", { k: 0, bv: 0.015 });   // kiosk on the wall
  fbox(A, F.rear, 3.25, 3.85, FL + 1.85, FL + 1.95, 0.3, 0.31, "lamp_green", L1);
  signPanel(A, F.rear, 3.6, 5.2, FL + 2.5, FL + 2.9, "sign_w", { depth: 0.04 });
  fbox(A, F.rear, PD - 0.9, PD + 0.9, FL + 2.45, FL + 2.5, 0, 0.55, "metal_panel", { bv: 0.01 });   // door canopy
  // partition and door between the pump room and the control room, control room ceiling and cabinets
  partition(A, { axis: "z", d0: XP - 0.075, d1: XP + 0.075, a0: z0 + T, a1: z1 - T, y0: FL, y1: TOP, holes: [{ a0: -3.0, a1: -2.0, y0: FL - 1, y1: FL + 2.1 }] });
  door(A, { id: "control_door", axis: "z", outer: XP - 0.075, inner: XP + 0.075, c: -2.5, floor: FL, w: 1.0, h: 2.1, hingeSide: 1, leafMat: "door_paint", frameMat: "alu", step: false, lintel: false, sound: "steel" });
  A.box([XP + 0.075, FL + 2.9, z0 + T], [x1 - T, FL + 3.0, z1 - T], "plaster");
  for (const z of [-2.8, -0.4, 1.6]) A.box([4.0, FL + 2.88, z - 0.3], [4.6, FL + 2.9, z + 0.3], "light_panel", L1);
  for (let i = 0; i < 4; i++) {
    const za = -1.6 + i * 0.62;
    A.box([XP + 0.08, FL, za], [XP + 0.68, FL + 2.1, za + 0.6], "panel_cab", { bv: 0.01 });
    A.box([XP + 0.68, FL + 0.1, za + 0.299], [XP + 0.69, FL + 2.0, za + 0.301], "steel_grey", L1);
    A.box([XP + 0.68, FL + 1.4, za + 0.1], [XP + 0.69, FL + 1.7, za + 0.5], i === 1 ? "screen" : "metal_panel", L1);
    for (let k = 0; k < 3; k++) A.cyl([XP + 0.69, FL + 1.2, za + 0.15 + k * 0.15], [0.04, 0.02, 0.04], k === 0 ? "lamp_green" : k === 1 ? "lamp_red" : "safety_yel", { seg: 6, r: [0, 0, 90], ...L1 });
  }
  A.box([XP + 0.08, FL + 2.1, -1.6], [XP + 0.4, FL + 2.3, 0.88], "grille", L1);   // cable tray
  // pumps, discharge mains, runway beam and hoist
  for (const x of [-4.6, -2.1, 0.4]) pump(A, x, -1.4, FL, z1 + 0.05);
  A.box([x0 + T, TOP - 0.55, -1.5], [XP - 0.1, TOP - 0.53, -1.3], "steel_grey", K0);
  A.box([x0 + T, TOP - 0.25, -1.5], [XP - 0.1, TOP - 0.23, -1.3], "steel_grey", K0);
  A.box([x0 + T, TOP - 0.53, -1.41], [XP - 0.1, TOP - 0.25, -1.39], "steel_grey", K0);
  A.box([-2.4, TOP - 0.8, -1.6], [-1.8, TOP - 0.55, -1.2], "safety_yel", { k: 0, bv: 0.01 });
  for (const dz of [-0.06, 0.06]) rod(A, [-2.1, TOP - 0.8, -1.4 + dz], [-2.1, FL + 3.2, -1.4 + dz], 0.02, "steel_grey", L1);
  A.box([-2.25, FL + 3.0, -1.5], [-1.95, FL + 3.2, -1.3], "safety_yel", L1);
  for (const s of [-1, 1]) A.box([x0 + T, FL + 3.4, s > 0 ? z1 - T - 0.3 : z0 + T], [XP - 0.1, FL + 3.45, s > 0 ? z1 - T : z0 + T + 0.3], "grille", L1);   // cable trays
  for (const x of [-4.6, -2.1, 0.4]) A.box([x - 0.3, TOP - 0.02, z0 + T + 1.0], [x + 0.3, TOP, z0 + T + 2.0], "light_panel", L1);
  // outfalls: mains out of the front wall, drop elbows, concrete headwall with flap valves on the channel face, rail
  const HZ0 = 4.2, HZ1 = 5.0;
  for (const x of [-4.6, -2.1, 0.4]) {
    A.cyl([x, FL + 0.65, (z1 + 3.6) / 2], [0.5, 3.6 - z1, 0.5], "pipe_blue", { seg: 14, r: [90, 0, 0] });
    A.cyl([x, FL + 0.65, z1 + 0.1], [0.66, 0.06, 0.66], "pipe_blue", { seg: 14, r: [90, 0, 0], ...L1 });
    A.cyl([x, (FL + 0.65 + 0.05) / 2, 3.9], [0.5, Math.hypot(0.6, FL + 0.6), 0.5], "pipe_blue", { seg: 14, r: [45, 0, 0] });
    A.box([x - 0.35, 0.0, 3.0], [x + 0.35, FL + 0.38, 3.2], "concrete_m", L1);   // pipe plinth
    A.cyl([x, -1.0, HZ1 + 0.04], [0.7, 0.08, 0.7], "steel_grey", { seg: 14, r: [90, 0, 0], k: 0 });
    A.box([x - 0.12, -0.62, HZ1], [x + 0.12, -0.55, HZ1 + 0.1], "steel_grey", L1);
  }
  A.box([-6.0, -2.0, HZ0], [2.0, 0.3, HZ1], "concrete_m", { bv: 0.02 });
  A.box([-6.05, 0.3, HZ0 - 0.05], [2.05, 0.38, HZ1 + 0.05], "kerb", { bv: 0.015 });
  tubeRail(A, -5.9, HZ1 - 0.1, 1.9, HZ1 - 0.1, 0.38, { h: 1.0, sp: 1.95 });
  A.box([-6.0, -0.05, z1], [2.0, 0.02, HZ0], "gravel_roof", L1);
  // nav
  addRoom(N, "pump_room", x0 + T, FL, z0 + T, XP - 0.075, TOP - 0.6, z1 - T);
  addRoom(N, "control_room", XP + 0.075, FL, z0 + T, x1 - T, FL + 2.9, z1 - T);
  ndoor(N, "pump_room", "outside", [x0 + T / 2, FL, RD.c]); nent(N, [x0 - 1.4, 0, RD.c], [1, 0, 0], "pump_room");
  ndoor(N, "control_room", "outside", [PD, FL, z0 + T / 2]); nent(N, [PD, 0, z0 - 1.2], [0, 0, 1], "control_room");
  ndoor(N, "pump_room", "control_room", [XP, FL, -2.5]);
  N.cover.push(cover([-4.6, FL, -0.4], 180, "high", ["left", "right"]), cover([-2.1, FL, -2.4], 0, "high", ["left", "right"]), cover([-3.35, FL, 1.3], 180, "low", ["over"]),
    cover([x1 + 0.6, 0, -1.0], 270, "high", ["left", "right"]), cover([0, 0, z0 - 0.6], 0, "high", ["left", "right"]), cover([-1.0, 0.38, HZ0 - 0.5], 0, "low", ["over"]));
  A.meta.roomTypes = { pump_room: "workshop", control_room: "office" };   // "office" resolves to the modern office recipe (mod_ host)
  setNav(A, N);
  tourAt(A, x0 - 4, RD.c, [{ goto: [x0 - 1.2, RD.c] }, { interact: "roller_door", aim: [x0 + T + 0.05, 1.5, RD.c + 0.4], expectOpen: true }, { goto: [-3.4, 0.6], expectY: FL, note: "into the pump room" }, { goto: [1.5, -2.5] }, { interact: "control_door", aim: [XP, 1.2, -2.25], expectOpen: true }, { goto: [3.6, -2.5], note: "into the control room" }]);
  return A;
}

// ============================================================================ 19. concrete road bridge over the channel
export function roadBridge() {
  const A = nt("mod_nt_road_bridge", "Channel road bridge", "Modern: Waterfront", "Short concrete road bridge, 12 m wide over a 10 m span along the road (z), crossing an 8 m wide, 2 m deep drainage channel that runs along x: six precast beams on bearing shelves of two concrete abutments (their faces are the channel walls under the deck), a deck slab with a 0.3 m crown, asphalt carriageway with blank white lane markings, raised sidewalks with kerbs, concrete parapets with a steel handrail, expansion joints and kerb gullies; the deck ramps down to the ground at both ends. The channel space under the deck is open. Walkable deck.", [12, 10], { enterable: false, floors: 0, maxGrade: 0.8 });
  const HX = 6, HZ = 5, CH = 4, DEP = 2, YD = 0.3, ZF = 3.2, KW = 4.0, PW = 5.6;
  for (const s of [-1, 1]) {
    const za = s > 0 ? CH : -HZ, zb = s > 0 ? HZ : -CH;
    A.box([-HX, -DEP - 0.4, za], [HX, -1.05, zb], "concrete_m", { bv: 0.02 });
    A.box([-HX, -1.05, s > 0 ? CH + 0.6 : -HZ], [HX, -0.45, s > 0 ? HZ : -CH - 0.6], "concrete_m", BV);
    A.box([-HX, -DEP - 0.4, s > 0 ? CH - 0.6 : -CH], [HX, -DEP + 0.15, s > 0 ? CH : -CH + 0.6], "concrete_m", L1);   // toe
    for (const x of [-5, -3, -1, 1, 3, 5]) A.box([x - 0.25, -1.08, s * (CH + 0.3) - 0.15], [x + 0.25, -1.05, s * (CH + 0.3) + 0.15], "rubber", L1);
    A.box([-HX + 0.3, -0.48, s * (CH + 0.6) - 0.02], [HX - 0.3, 0.005, s * (CH + 0.6) + 0.02], "steel_grey", L1);   // expansion joint
    for (let x = -HX + 0.8; x < HX; x += 1.6) A.cyl([x, -1.6, s * CH - s * 0.01], [0.1, 0.02, 0.1], "membrane", { seg: 8, r: [90, 0, 0], ...L1 });   // weep holes
  }
  A.box([-HX, -DEP - 0.1, -CH], [HX, -DEP, CH], "concrete_m");   // channel invert under the bridge
  for (const x of [-5, -3, -1, 1, 3, 5]) A.box([x - 0.3, -1.05, -CH - 0.55], [x + 0.3, -0.45, CH + 0.55], "concrete_m", { bv: 0.02 });
  A.box([-HX, -0.45, -HZ], [HX, 0.0, HZ], "concrete_m", BV);
  // carriageway: crowned surfacing with ramps to the ground at both ends
  A.box([-KW, 0, -ZF], [KW, YD, ZF], "asphalt");
  for (const s of [-1, 1]) A.wedge([0, YD / 2, s * (ZF + (HZ - ZF) / 2)], [2 * KW, YD, HZ - ZF], "asphalt", { r: [0, s > 0 ? 0 : 180, 0] });
  for (let z = -ZF + 0.3; z < ZF - 0.5; z += 2.0) A.box([-0.06, YD, z], [0.06, YD + 0.004, z + 1.0], "line_white", L1);
  for (const x of [-KW + 0.25, KW - 0.25]) A.box([x - 0.05, YD, -ZF], [x + 0.05, YD + 0.004, ZF], "line_white", L1);
  // sidewalks, kerbs, parapets with handrails, gullies, lamp
  for (const s of [-1, 1]) {
    const xa = s > 0 ? KW : -PW, xb = s > 0 ? PW : -KW;
    A.box([xa, 0, -ZF], [xb, YD + 0.15, ZF], "paving");
    A.wedge([(xa + xb) / 2, (YD + 0.15) / 2, ZF + (HZ - ZF) / 2], [PW - KW, YD + 0.15, HZ - ZF], "paving", {});
    A.wedge([(xa + xb) / 2, (YD + 0.15) / 2, -ZF - (HZ - ZF) / 2], [PW - KW, YD + 0.15, HZ - ZF], "paving", { r: [0, 180, 0] });
    A.box([s > 0 ? KW : -KW - 0.15, YD - 0.1, -ZF], [s > 0 ? KW + 0.15 : -KW, YD + 0.16, ZF], "kerb", { bv: 0.012 });
    const pa = s > 0 ? PW : -HX, pb = s > 0 ? HX : -PW;
    A.box([pa, 0, -HZ], [pb, 1.0, HZ], "concrete_m", { bv: 0.015 });
    A.box([pa - 0.03, 1.0, -HZ], [pb + 0.03, 1.08, HZ], "kerb", { bv: 0.015 });
    for (const z of [-HZ + 0.1, HZ - 0.4]) A.box([pa - 0.05, 0, z], [pb + 0.05, 1.15, z + 0.3], "concrete_m", { bv: 0.02 });   // end pilasters
    const xr = s * (HX - 0.2);
    for (let z = -4.5; z <= 4.51; z += 1.5) { A.box([xr - 0.03, 1.08, z - 0.03], [xr + 0.03, 1.45, z + 0.03], "galv", { k: 0, bv: 0.005 }); A.box([xr - 0.07, 1.08, z - 0.07], [xr + 0.07, 1.095, z + 0.07], "galv", L1); }
    rod(A, [xr, 1.45, -HZ + 0.4], [xr, 1.45, HZ - 0.4], 0.05, "galv", { seg: 8 });
    A.box([xr - 0.04, 1.08, -HZ + 0.4], [xr + 0.04, 1.45, HZ - 0.4], "galv", HID);
    for (const z of [-2.0, 2.0]) A.box([s * (KW - 0.35) - 0.2, YD - 0.002, z - 0.25], [s * (KW - 0.35) + 0.2, YD + 0.003, z + 0.25], "grille", L1);
    A.box([s * (HX + 0.0) - 0.02 * s, -0.4, -HZ], [s * HX + 0.02 * s, -0.05, HZ], "concrete_m", L1);   // edge beam fascia
  }
  A.cyl([-5.3, (YD + 0.15 + 8.9) / 2, 0], [0.12, 8.9 - YD - 0.15, 0.12], "metal_panel", { seg: 10 });
  A.box([-5.35, 8.9, -0.1], [-4.4, 9.0, 0.1], "metal_panel", { k: 0, bv: 0.01 });
  A.box([-4.9, 8.88, -0.08], [-4.45, 8.9, 0.08], "light_panel", L1);
  const cv = [];
  for (const z of [-2.5, 0, 2.5]) cv.push(cover([4.95, YD + 0.15, z], 90, "low", ["over"]), cover([-4.95, YD + 0.15, z], 270, "low", ["over"]));
  setNav(A, { ...nav0("block"), cover: cv });
  A.meta.notes = "Deck top at y = 0.3 at the centre; the banks are at y = 0 beyond |z| = 4 and the channel (8 m wide along x, 2 m deep) must be carved by the map: the asset adds the abutments and a channel invert slab under the deck.";
  return A;
}

// ============================================================================ 20. steel footbridge
export function footbridge() {
  const A = nt("mod_nt_footbridge", "Steel footbridge", "Modern: Waterfront", "Steel pedestrian footbridge, 3 x 12 m, spanning along z over the 8 m drainage channel: two grey painted I-girders with cross beams on concrete bank seats, a composite deck 0.3 m above the banks with short ramps down at both ends, kick plates, and railings both sides with a round top rail, mid rails and vertical bar infill. Walkable deck.", [3, 12], { enterable: false, floors: 0 });
  const HX = 1.5, HZ = 6, CH = 4, YD = 0.3, ZG = 4.6;
  for (const s of [-1, 1]) {
    A.box([-HX, -2.2, s > 0 ? CH : -CH - 0.6], [HX, -0.35, s > 0 ? CH + 0.6 : -CH], "concrete_m", { bv: 0.02 });
    A.box([-HX, -0.6, s > 0 ? ZG : -HZ], [HX, 0.0, s > 0 ? HZ : -ZG], "concrete_m", BV);
    A.wedge([0, YD / 2, s * (ZG + (HZ - ZG) / 2)], [2.3, YD, HZ - ZG], "deck_comp", { r: [0, s > 0 ? 0 : 180, 0] });
    for (const x of [-1.25, 1.25]) A.box([x - 0.15, -0.37, s * (CH + 0.3) - 0.15], [x + 0.15, -0.35, s * (CH + 0.3) + 0.15], "rubber", L1);
  }
  for (const x of [-1.25, 1.25]) {
    for (const y of [-0.35, 0.18]) A.box([x - 0.1, y, -ZG], [x + 0.1, y + 0.02, ZG], "steel_grey", K0);
    A.box([x - 0.006, -0.33, -ZG], [x + 0.006, 0.18, ZG], "steel_grey", K0);
    A.box([x - 0.1, -0.35, -ZG], [x + 0.1, 0.2, ZG], "steel_grey", HID);
    for (let z = -ZG + 0.6; z < ZG; z += 1.2) A.box([x - 0.08, -0.33, z - 0.006], [x + 0.08, 0.18, z + 0.006], "steel_grey", L1);   // stiffeners
  }
  for (let z = -ZG + 0.3; z < ZG; z += 1.5) A.box([-1.15, 0.05, z - 0.06], [1.15, 0.2, z + 0.06], "steel_grey", L1);
  A.box([-1.4, 0.2, -ZG], [1.4, YD, ZG], "deck_comp");
  for (let z = -ZG + 1.2; z < ZG; z += 1.2) A.box([-1.38, YD - 0.004, z - 0.004], [1.38, YD + 0.002, z + 0.004], "rubber", L1);
  const th = Math.atan2(YD, HZ - ZG) * DEG, Lr = Math.hypot(YD, HZ - ZG);
  for (const sx of [-1, 1]) {
    const x = sx * 1.42;
    A.box([x - 0.02, YD, -ZG], [x + 0.02, YD + 0.12, ZG], "steel_grey", L1);   // kick plate
    for (let z = -ZG; z <= ZG + 0.01; z += 1.533) { A.box([x - 0.03, YD, z - 0.03], [x + 0.03, YD + 1.1, z + 0.03], "steel_grey", { k: 0, bv: 0.005 }); A.box([x - 0.08, YD - 0.1, z - 0.05], [x + 0.08, YD, z + 0.05], "steel_grey", L1); }
    rod(A, [x, YD + 1.1, -ZG], [x, YD + 1.1, ZG], 0.05, "galv", { seg: 8 });
    for (const y of [0.15, 1.0]) rod(A, [x, YD + y, -ZG], [x, YD + y, ZG], 0.03, "galv", { seg: 6 });
    for (let z = -ZG + 0.12; z < ZG; z += 0.13) A.box([x - 0.007, YD + 0.15, z - 0.007], [x + 0.007, YD + 1.0, z + 0.007], "galv", L1);
    A.box([x - 0.04, YD, -ZG], [x + 0.04, YD + 1.1, ZG], "galv", HID);
    for (const s of [-1, 1]) {   // rails down the ramps
      A.obox([x, YD / 2 + 1.1, s * (ZG + (HZ - ZG) / 2)], [0.05, 0.05, Lr], [s * th, 0, 0], "galv", K0);
      A.box([x - 0.03, 0, s * HZ - 0.03 - (s > 0 ? 0.03 : -0.03)], [x + 0.03, 1.1, s * HZ + 0.03 - (s > 0 ? 0.03 : -0.03)], "steel_grey", { k: 0, bv: 0.005 });
      A.obox([x, YD / 2 + 0.55, s * (ZG + (HZ - ZG) / 2)], [0.06, 1.1, Lr], [s * th, 0, 0], "galv", HID);
    }
  }
  setNav(A, { ...nav0("block"), cover: [cover([0, 0, HZ + 0.6], 180, "low", ["over"]), cover([0, 0, -HZ - 0.6], 0, "low", ["over"])] });
  A.meta.notes = "Deck at y = 0.3 over the channel (8 m wide, |z| < 4, 2 m deep, carved by the map); the ends ramp to the banks at y = 0.";
  return A;
}

// ============================================================================ 21. channel maintenance steps and ramp
export function channelSteps() {
  const A = nt("mod_nt_channel_steps", "Channel access steps", "Modern: Waterfront", "Concrete maintenance access built against the drainage channel wall, 3 x 6 m, going down 2 m along z (top landing at z = -3 on the bank, bottom at z = +3 on the channel floor): two flights of steps with a half landing beside a ribbed concrete barrow ramp, a retaining wall on the -x side, a solid concrete body below, galvanised handrails on the open side and between steps and ramp, yellow nosings. Walkable.", [3, 6], { enterable: false, floors: 0, maxGrade: 0.8 });
  const Z0 = -3, Z1 = 3, D = 2, W0 = -1.35, W1 = 0.1, R0 = 0.2, R1 = 1.35;
  const prof = [[-3.0, -2.4, 0, 0], [-0.48, 0.6, -1.0, -1.0], [2.52, 3.0, -2.0, -2.0]];   // flat parts [z0, z1, y]
  A.box([-1.5, -D - 0.3, Z0], [-1.35, 0.05, Z1], "concrete_m", { bv: 0.015 });   // retaining wall (channel side wall)
  A.box([1.35, -D - 0.3, Z0], [1.5, 0.05, Z1], "concrete_m", { bv: 0.015 });
  for (const [za, zb, y] of prof) A.box([W0, -D - 0.3, za], [W1, y, zb], "concrete_m", BV);
  const run = (za, y0) => {   // 6 risers of 1/6 m down along +z, goings 0.32
    for (let i = 0; i < 6; i++) {
      const top = y0 - (i * 1.0) / 6 - 1 / 6, z = za + i * 0.32;
      A.box([W0, -D - 0.3, z], [W1, top, z + 0.32], "concrete_m", { bv: 0.008 });
      A.box([W0 + 0.02, top - 0.005, z - 0.0], [W1 - 0.02, top + 0.003, z + 0.05], "safety_yel", L1);
    }
    const L = Math.hypot(6 * 0.32, 1.0);
    A.obox([(W0 + W1) / 2, y0 - 0.5 - 0.12, za + 0.96], [W1 - W0, 0.2, L], [Math.atan2(1.0, 1.92) * DEG, 0, 0], "concrete_m", HID);
  };
  run(-2.4, 0); run(0.6, -1.0);
  // ramp: a single slope beside the steps, ribbed
  const rL = Math.hypot(Z1 - Z0, D), rth = Math.atan2(D, Z1 - Z0) * DEG;
  A.obox([(R0 + R1) / 2, -D / 2 - 0.2, 0], [R1 - R0, 0.4, rL], [rth, 0, 0], "concrete_m");
  for (let t = 0.05; t < 1; t += 0.06) A.obox([(R0 + R1) / 2, -D * t + 0.005, Z0 + (Z1 - Z0) * t], [R1 - R0 - 0.1, 0.02, 0.05], [rth, 0, 0], "concrete_m", L1);
  A.box([W1, -D - 0.3, Z0], [R0, -0.15, Z1], "concrete_m", L1);
  A.prism(0, W1, R0, [[Z0, 0.0], [Z1, -D], [Z0, -D]], "concrete_m");
  A.prism(0, R0, R1, [[Z0, -0.2], [Z1 - 0.2, -D], [Z0, -D]], "concrete_m", K0);
  // handrails: on the open side (+x cheek) and between steps and ramp, following the slope
  for (const x of [0.15, 1.4]) {
    const pts = [[Z0 + 0.1, 0.0], [Z1 - 0.1, -D]];
    for (let i = 0; i <= 4; i++) { const t = i / 4, z = pts[0][0] + (pts[1][0] - pts[0][0]) * t, y = pts[0][1] + (pts[1][1] - pts[0][1]) * t; rod(A, [x, y, z], [x, y + 1.0, z], 0.048, "galv", { seg: 8 }); }
    rod(A, [x, 1.0, pts[0][0]], [x, 1.0 - D, pts[1][0]], 0.048, "galv", { seg: 8 });
    rod(A, [x, 0.5, pts[0][0]], [x, 0.5 - D, pts[1][0]], 0.035, "galv", { seg: 8 });
    A.obox([x, -D / 2 + 0.5, 0], [0.06, 1.0, Math.hypot(Z1 - Z0 - 0.2, D)], [rth, 0, 0], "galv", HID);
  }
  rod(A, [-1.3, 0.9, Z0 + 0.6], [-1.3, 0.9 - D, Z1 - 0.4], 0.04, "galv", { seg: 8 });   // wall rail
  for (const z of [Z0 + 0.6, 0.0, Z1 - 0.4]) A.box([-1.35, -D * (z - Z0) / (Z1 - Z0) + 0.85, z - 0.03], [-1.27, -D * (z - Z0) / (Z1 - Z0) + 0.9, z + 0.03], "galv", L1);
  setNav(A, { ...nav0("block"), cover: [cover([-0.6, -D, 2.75], 0, "high", ["left", "right"])] });
  A.meta.notes = "Top landing at z = -3 on the bank (y = 0), bottom at z = +3 on the channel floor (y = -2); the -x side stands against the channel wall. The channel must be carved by the map.";
  return A;
}

// ============================================================================ catalog
export function newtownAssetsC() {
  const out = [houseSmall(), houseTwo(), houseSplit(), waterfrontHouse(), farmhouse(), barnSteel(), constructionFrame(), constructionFrameSmall(),
    siteOffice(), towerCrane(), excavationShoring(), boathouse(), pier(), pontoonFinger(), boatMotor(), boatTrailer(), promenadeRail(), pumpStation(),
    roadBridge(), footbridge(), channelSteps()];
  const ids = new Set();
  for (const A of out) {   // self-check: unique mod_nt_ ids, room ids unique and typed, doors to known rooms, part budget
    const id = A.meta.id;
    if (!id.startsWith("mod_nt_") || ids.has(id)) throw new Error(`bad or duplicate id ${id}`);
    ids.add(id);
    const nav = A.meta.nav;
    if (!nav) throw new Error(`${id}: no nav`);
    const rooms = (nav.rooms ?? []).map((r) => r.id);
    if (new Set(rooms).size !== rooms.length) throw new Error(`${id}: duplicate nav room ids`);
    if (A.meta.roomTypes && Object.keys(A.meta.roomTypes).length) for (const r of rooms) if (!A.meta.roomTypes[r]) throw new Error(`${id}: room ${r} untyped`);
    for (const d of nav.doors ?? []) for (const r of [d.a, d.b]) if (r !== "outside" && !rooms.includes(r)) throw new Error(`${id}: door to unknown room ${r}`);
    const iid = A.interactables.map((i) => i.id);
    if (new Set(iid).size !== iid.length) throw new Error(`${id}: duplicate interactable ids`);
    const names = new Set(A.materials.map((m) => m.name));
    for (const p of A.parts) if (!names.has(p.m)) throw new Error(`${id}: unknown material ${p.m}`);
    if (A.parts.length > 1600) throw new Error(`${id}: ${A.parts.length} parts`);
  }
  return out;
}
