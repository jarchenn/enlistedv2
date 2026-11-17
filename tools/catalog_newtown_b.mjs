// Newtown set B (docs/modern-assets-brief.md): fourteen modern town assets for the map generator, ids mod_nt_*: a hotel,
// an open multi-deck parking garage with straight ramps, a civic clock tower, a corner shop, a row of three shops with
// flats over, a cafe with a terrace, a car dealership, a two-storey motel with exterior walkways, a fire station with a
// drill tower, a town hall, a covered passage, a kiosk, an ambulance and a fire engine. Footprints are fixed (the map
// generator is written against them). Written to content/assets/<id>.json by tools/build_mpu_assets.mjs (integrator).
//
// The enterable buildings use the modern wing builder of tools/catalog_modern_buildings_b.mjs (copied below: that file
// exports only its assets): two-layer walls with real openings, ribbon glazing whose units are window interactables,
// aluminium doors and roller doors, switchback stair cores (roof bulkhead and door where a building has roof access),
// lifts, corridor plans, nav rooms with A.meta.roomTypes for the auto-furnisher. Only fixed fittings are modelled.
// Material rule (MOD1 review): "painted" is the painted-wood-plank shader, so painted steel is "metal", signs and flat
// colour are "plain", and no paint_metal_* texture set (they carry rust). Signs are blank coloured panels.
import { Asset, cutRect, wbox, P, swingSign } from "./kit.mjs";
import { wall, partition, hinged, aabb, fbox, railing } from "./parts.mjs";
import { mats as baseMats, comp } from "./catalog_chateau.mjs";
import { cover, fire, room, corners, setNav, setTour } from "./catalog_atlantic.mjs";
import { tdTown, tdAwning, tdRoof, tdPilaster, tdBars, tdMats, tdMerge, tdCornice } from "./catalog_modern_buildings_a.mjs";
import { ntMats, ntText, ntShutter, ntPosters, ntCctv, ntAlarm, ntMeter, ntDish, ntLamp, ntBoard, ruinOnly } from "./nt7_kit.mjs";   // NT7: street detail from the references
const GF = 0.3, SLAB = 0.25, PT = 0.03, CW = 1.25, SP = 0.12, ENC = 0.15, LAND = 1.4, LT = 0.2;
const SIDES = ["front", "rear", "east", "west"];
const K0 = { k: 0 }, L1 = { k: 0, lod: 1 };
const q4 = (v) => Math.round(v * 1e4) / 1e4;
const zr = (a, b) => [Math.min(a, b), Math.max(a, b)];

// ============================================================================ materials
function modMats(A, accent = [0.08, 0.30, 0.38]) {
  baseMats(A);
  const re = (name, albedo, roughness, pattern, extra = {}) => Object.assign(A.materials.find((m) => m.name === name), { albedo, roughness, pattern, metallic: 0, ...extra });
  // the shared kit names keep their roles (interior.cpp reads floor_x / tread_x / terracotta / plank_x as floors and
  // stairs) with a modern look: tiles and vinyl, precast stairs, aluminium frames, painted steel railings
  re("floor_x", [0.42, 0.42, 0.40], 0.55, "tile", { texture: "floor_cement_tile", grain: 0 });
  re("concrete_floor", [0.06, 0.06, 0.065], 0.85, "concrete", { texture: "concrete_light" });   // rubber sports matting
  re("terracotta", [0.66, 0.67, 0.66], 0.3, "tile", { texture: "pooltile_backrooms", grain: 0 });
  re("plank_x", [0.46, 0.30, 0.16], 0.4, "wood", { texture: "wood_oak", grain: 0 });
  re("tread_x", [0.42, 0.41, 0.39], 0.7, "concrete", { texture: "concrete_light", grain: 0 });
  re("landing_x", [0.42, 0.41, 0.39], 0.7, "concrete", { texture: "concrete_light", grain: 0 });
  for (const g of ["oak_x", "oak_y", "oak_z"]) re(g, [0.12, 0.125, 0.13], 0.45, "metal", { metallic: 0.3 });
  re("window_paint", [0.10, 0.105, 0.11], 0.4, "metal", { metallic: 0.6 });
  re("stone_dressed", [0.56, 0.55, 0.51], 0.8, "concrete", { texture: "concrete_light" });
  re("door_paint", [0.16, 0.22, 0.27], 0.5, "metal");
  re("plaster", [0.70, 0.69, 0.66], 0.9, "interior", { grain: 0 });
  re("concrete", [0.40, 0.39, 0.37], 0.9, "concrete", { texture: "concrete_aged" });
  re("concrete_wash", [0.52, 0.51, 0.48], 0.9, "concrete", { texture: "concrete_light" });
  re("brick", [0.30, 0.16, 0.10], 0.86, "brick", { texture: "brick_factory" });
  A.mat("plinth", [0.30, 0.29, 0.27], 0.9, "concrete", { texture: "concrete_formwork" });
  A.mat("precast", [0.60, 0.59, 0.55], 0.82, "concrete", { texture: "concrete_grey_cast" });
  A.mat("render_w", [0.74, 0.73, 0.70], 0.9, "plaster", { texture: "render_lime" });
  A.mat("brick_grey", [0.33, 0.32, 0.30], 0.85, "brick", { texture: "brick_muted" });
  A.mat("stone_clad", [0.62, 0.58, 0.50], 0.7, "stone", { texture: "limestone_smooth" });
  A.mat("panel_lt", [0.62, 0.63, 0.63], 0.45, "metal", { metallic: 0.2 });
  A.mat("panel_dk", [0.07, 0.08, 0.09], 0.4, "metal", { metallic: 0.3 });
  A.mat("panel_acc", accent, 0.45, "metal");
  A.mat("corrugated", [0.48, 0.50, 0.51], 0.45, "metal", { texture: "corrugated_grey", metallic: 0.7 });
  A.mat("alu", [0.20, 0.21, 0.22], 0.35, "metal", { metallic: 0.8 });
  A.mat("alu_lt", [0.55, 0.56, 0.57], 0.3, "metal", { metallic: 0.9 });
  A.mat("bronze", [0.20, 0.14, 0.09], 0.35, "metal", { metallic: 0.8 });
  A.mat("glass_t", [0.30, 0.42, 0.46], 0.04, "glass");
  A.mat("glass_int", [0.75, 0.80, 0.80], 0.15, "glass");
  A.mat("ceiling", [0.74, 0.74, 0.72], 0.9, "tile", { texture: "ceiling_tiles_office" });
  A.mat("door_frame", [0.80, 0.80, 0.78], 0.45, "metal", { metallic: 0.15 });   // AS1: door frames / linings
  A.mat("rainwater", [0.16, 0.17, 0.18], 0.45, "metal", { metallic: 0.3 });   // AS1: downpipes, hoppers
  A.mat("light_panel", [0.9, 0.9, 0.86], 0.5, "plain", { emissive: [2.2, 2.15, 2.0] });
  A.mat("roof_mem", [0.17, 0.17, 0.18], 0.85, "concrete", { texture: "asphalt_dark" });
  A.mat("gravel", [0.42, 0.40, 0.37], 0.95, "stone", { texture: "gravel_fine" });
  A.mat("paving", [0.45, 0.44, 0.41], 0.85, "stone", { texture: "paving_worn" });
  A.mat("asphalt", [0.08, 0.08, 0.085], 0.92, "concrete", { texture: "asphalt_plain" });
  A.mat("ballast", [0.30, 0.28, 0.26], 0.95, "stone", { texture: "gravel_coarse" });
  A.mat("rubber", [0.04, 0.04, 0.045], 0.9, "plain");
  A.mat("lift_steel", [0.62, 0.62, 0.63], 0.3, "metal", { texture: "steel_scratched", metallic: 1.0 });
  A.mat("rail_steel", [0.42, 0.40, 0.38], 0.4, "metal", { texture: "steel_rust_light", metallic: 0.9 });
  A.mat("safety_yel", [0.62, 0.48, 0.05], 0.6, "metal");
  A.mat("orange", [0.62, 0.18, 0.03], 0.6, "metal");
  A.mat("paint_w", [0.80, 0.80, 0.78], 0.6, "metal");
  A.mat("hvac", [0.55, 0.56, 0.55], 0.5, "metal", { metallic: 0.3 });
  A.mat("louvre", [0.25, 0.26, 0.27], 0.5, "metal", { metallic: 0.4 });
  A.mat("laminate", [0.55, 0.53, 0.48], 0.5, "wood", { texture: "floor_laminate_pale" });
  A.mat("sign_blank", accent.map((v) => v * 0.8), 0.5, "plain", { emissive: accent.map((v) => v * 0.6) });
  A.mat("lamp_g", [0.1, 0.5, 0.15], 0.4, "plain", { emissive: [0.3, 2.0, 0.5] });
  A.mat("screen", [0.02, 0.03, 0.04], 0.2, "plain", { emissive: [0.25, 0.45, 0.6] });
  A.mat("water_proxy", [0.05, 0.1, 0.1], 0.1, "glass");
  A.mat("pool_tile", [0.18, 0.42, 0.52], 0.25, "tile", { texture: "tiles_pool_cyan" });
  A.mat("soil", [0.12, 0.09, 0.06], 1.0, "plain", { texture: "soil_brown" });
  A.mat("leaf", [0.05, 0.11, 0.03], 0.85, "plain");
}

function mk(id, name, description, floors, footprint, accent) {
  const A = new Asset({ id, version: 1, name, category: "Modern", description, footprint, floors, enterable: true, plinthDepth: 0, maxGrade: 0.6, noAutoPanels: true });
  modMats(A, accent);
  tdMats(A);   // TD1 facade kit
  A._nav = { effect: "block", rooms: [], doors: [], entrances: [], corners: [], firingPoints: [], cover: [], areas: [] };
  A.meta.roomTypes = {};
  return A;
}
function finish(A, cuts = null) { tdMerge(A); as1Run(A); tdMerge(A); setNav(A, A._nav, cuts); delete A._nav; return A; }
function addRoom(A, id, type, x0, y, z0, x1, top, z1) {
  A._nav.rooms.push(room(id, [x0, y, z0], [x1, top, z1]));
  A._nav.corners.push(...corners(id, x0, x1, z0, z1, y, Math.min(0.45, (x1 - x0) / 4, (z1 - z0) / 4)));
  A.meta.roomTypes[id] = type;
}
const navDoor = (A, a, b, p) => A._nav.doors.push({ a, b, p: p.map(q4) });
function coverRing(A, x0, x1, z0, z1) {
  const xm = (x0 + x1) / 2, zm = (z0 + z1) / 2;
  A._nav.cover.push(cover([x1 + 0.6, 0, zm], 270, "high", ["left", "right"]), cover([x0 - 0.6, 0, zm], 90, "high", ["left", "right"]),
    cover([xm, 0, z0 - 0.6], 0, "high", ["left", "right"]), cover([x1 + 0.6, 0, z1 - 1.0], 270, "high", ["right"]), cover([x0 - 0.6, 0, z1 - 1.0], 90, "high", ["left"]));
}

// ============================================================================ openings
const D = (F, e) => F.outer - F.out * e;   // a depth measured into the wall from its outer face
const WB = (A, F, a0, a1, y0, y1, e0, e1, m, o) => wbox(A, F.axis, a0, a1, y0, y1, D(F, e0), D(F, e1), m, o);

// Ribbon glazing in a0..a1 between y0 and y1 on the wall frame F: aluminium head and sill, mullions with exterior fins,
// and per unit a fixed light plus a side-hung opening sash. Each unit is one window interactable whose aperture covers
// the whole unit. floor: the storey's walking level (firing points). Returns the wall hole.
// NT2b: the frame sits r (18 cm in a 0.3 m wall) back in the reveal, the glass at r; a precast sill projects under it and
// a lintel band (lintel: material or null) spans the head
function ribbon(A, F, { id, a0, a1, y0, y1, unit = 3.6, floor, fin = 0.16, finMat = "alu", glass = "glass_t", sill = "precast", lintel = "precast" }) {
  const T = Math.abs(F.outer - F.inner), r = Math.min(0.18, T - 0.1), f0e = r - 0.06, f1e = Math.min(T - 0.02, r + 0.08);
  const n = Math.max(1, Math.round((a1 - a0) / unit)), uw = (a1 - a0) / n, mw = 0.07, g0 = r, g1 = r + 0.02;
  WB(A, F, a0, a1, y1 - 0.08, y1, f0e, f1e, "alu", K0);                                 // head
  WB(A, F, a0, a1, y0, y0 + 0.08, f0e, f1e, "alu");                                     // sill frame
  WB(A, F, a0 - 0.06, a1 + 0.06, y0 - 0.08, y0 + 0.01, -0.07, f0e, sill, { k: 0, bv: 0.01 });   // projecting sill
  if (lintel) WB(A, F, a0 - 0.12, a1 + 0.12, y1, y1 + 0.16, -0.025, 0.02, lintel, { k: 0, bv: 0.008 });
  else as1(A, () => WB(A, F, a0 - 0.03, a1 + 0.03, y1 - 0.012, y1 + 0.035, -0.045, 0.03, "alu_lt", L1), true);   // AS1: head drip
  const mull = (a, w) => { WB(A, F, a - w / 2, a + w / 2, y0 + 0.08, y1 - 0.08, f0e, f1e, "alu"); if (fin) WB(A, F, a - 0.025, a + 0.025, y0, y1, -fin, f0e, finMat, L1); };
  for (let i = 0; i < n; i++) {
    const ua0 = a0 + i * uw, ua1 = ua0 + uw;
    mull(i === 0 ? ua0 + mw / 2 : ua0, mw);
    const right = i === n - 1 ? ua1 - mw : ua1 - mw / 2;
    const sw = uw > 1.8 ? Math.min(1.1, uw * 0.45) : right - (i === 0 ? ua0 + mw : ua0 + mw / 2);
    const s1 = right, s0 = right - sw;
    if (uw > 1.8) {
      const f0 = i === 0 ? ua0 + mw : ua0 + mw / 2;
      WB(A, F, s0 - mw, s0, y0 + 0.08, y1 - 0.08, f0e + 0.01, f1e - 0.02, "alu");       // sash mullion
      WB(A, F, f0, s0 - mw, y0 + 0.08, y1 - 0.08, g0, g1, glass);                       // fixed light
    }
    const L = { parts: [] }, T = { into: L.parts, k: 0 };
    WB(A, F, s0, s1, y0 + 0.08, y1 - 0.08, g0, g1, glass, T);
    WB(A, F, s0, s1, y0 + 0.08, y0 + 0.16, r - 0.035, r + 0.015, "alu", T);             // sash bottom rail
    WB(A, F, s0 + 0.04, s0 + 0.06, (y0 + y1) / 2 - 0.1, (y0 + y1) / 2 + 0.1, r + 0.015, r + 0.04, "alu_lt", T);   // handle
    WB(A, F, s0, s1, y0 + 0.08, y1 - 0.08, r - 0.035, r + 0.015, "alu", { into: L.parts, v: 0 });
    L.hinge = P(F.axis, s1, 0, D(F, r + 0.015));
    L.sign = swingSign(F.axis, P(F.axis, (s0 + s1) / 2, 0, D(F, r - 0.01)), L.hinge, -F.out);
    A.interactables.push({ id: `${id}_${i}_casement`, kind: "window", label: "window", sound: "glass", axis: [0, 1, 0], openAngle: 85, duration: 0.8,
      defaultOpen: false, leaves: [L], aperture: aabb(P(F.axis, ua0, y0, F.inner), P(F.axis, ua1, y1, D(F, 0.08))) });
    const c = (ua0 + ua1) / 2, ins = F.inner - F.out * 0.6;
    A._nav.firingPoints.push(fire(F.axis === "x" ? [c, floor, ins] : [ins, floor, c], F.yaw, "window"));
  }
  mull(a1 - mw / 2, mw);
  return { a0, a1, y0, y1 };
}

// Aluminium door (glazed or solid, single or double) hung inside the reveal and opening inward. Returns the wall hole.
function mdoor(A, F, { id, c, floor, w = 1.0, h = 2.2, double = false, glass = false, hinge = 1, label = "door", depth = 0.3 }) {
  const a0 = c - w / 2, a1 = c + w / 2, top = floor + h, fw = 0.06, ld0 = F.inner + F.out * 0.03, ld1 = F.inner + F.out * 0.08;
  WB(A, F, a0, a0 + fw, floor, top - fw, depth - 0.2, depth - 0.02, "door_frame");
  WB(A, F, a1 - fw, a1, floor, top - fw, depth - 0.2, depth - 0.02, "door_frame");
  WB(A, F, a0, a1, top - fw, top, depth - 0.2, depth - 0.02, "door_frame");
  WB(A, F, a0, a1, floor - 0.04, floor + 0.008, -0.02, depth, "alu_lt");                        // threshold
  const la0 = a0 + fw, la1 = a1 - fw, ly0 = floor + 0.01, ly1 = top - fw - 0.005;
  const spans = double ? [[la0, (la0 + la1) / 2 - 0.004, -1], [(la0 + la1) / 2 + 0.004, la1, 1]] : [[la0, la1, hinge]];
  const leaves = [];
  for (const [s0, s1, hs] of spans) {
    const L = { parts: [] }, T = { into: L.parts, k: 0 }, B = (b0, b1, y0, y1, d0, d1, m) => wbox(A, F.axis, b0, b1, y0, y1, d0, d1, m, T);
    const hingeA = hs < 0 ? s0 : s1, dirA = hs < 0 ? 1 : -1, la = hingeA + dirA * (s1 - s0 - 0.1);
    if (glass) {
      B(s0, s0 + 0.09, ly0, ly1, ld0, ld1, "alu"); B(s1 - 0.09, s1, ly0, ly1, ld0, ld1, "alu");
      B(s0 + 0.09, s1 - 0.09, ly1 - 0.09, ly1, ld0, ld1, "alu"); B(s0 + 0.09, s1 - 0.09, ly0, ly0 + 0.2, ld0, ld1, "alu");
      B(s0 + 0.09, s1 - 0.09, ly0 + 0.2, ly1 - 0.09, ld0 + F.out * 0.02, ld0 + F.out * 0.03, "glass_t");
      B(Math.min(la, hingeA + dirA * 0.15), Math.max(la, hingeA + dirA * 0.15), floor + 1.0, floor + 1.04, ld0 - F.out * 0.05, ld0 - F.out * 0.01, "alu_lt");   // push bar
    } else {
      B(s0, s1, ly0, ly1, ld0, ld1, "door_paint");
      B(s0 + 0.04, s1 - 0.04, ly0 + 0.02, ly0 + 0.3, ld0 - F.out * 0.008, ld0, "alu_lt");                // kick plate
      B(la - 0.015, la + 0.015, floor + 0.9, floor + 1.15, ld0 - F.out * 0.05, ld0, "alu_lt");            // pull handle
      B(Math.min(la, la - dirA * 0.15), Math.max(la, la - dirA * 0.15), floor + 1.0, floor + 1.02, ld1, ld1 + F.out * 0.04, "alu_lt");   // lever (outside)
    }
    wbox(A, F.axis, s0, s1, ly0, ly1, ld0, ld1, "door_paint", { into: L.parts, v: 0 });   // leaf collider
    L.hinge = P(F.axis, hingeA, 0, ld0);
    L.sign = swingSign(F.axis, P(F.axis, (s0 + s1) / 2, 0, (ld0 + ld1) / 2), L.hinge, -F.out);
    leaves.push(L);
  }
  A.interactables.push({ id, kind: "door", label, sound: "steel", axis: [0, 1, 0], openAngle: 95, duration: double ? 1.2 : 1.0, defaultOpen: false, leaves,
    aperture: aabb(P(F.axis, a0, floor, F.inner), P(F.axis, a1, top, F.inner + F.out * 0.14)) });
  return { a0, a1, y0: -10, y1: top };
}

// Sectional loading door: one panel hinged at its head swinging up and in under the ceiling, steel guide frame, a
// motor housing and a dock shelter outside. Returns the wall hole.
function roller(A, F, { id, c, floor, w = 3.6, h = 4.2 }) {
  const a0 = c - w / 2, a1 = c + w / 2, top = floor + h, ld0 = F.inner + F.out * 0.05, ld1 = F.inner + F.out * 0.11;
  WB(A, F, a0 - 0.1, a0 + 0.012, floor, top - 0.012, 0.1, 0.36, "safety_yel"); WB(A, F, a1 - 0.012, a1 + 0.1, floor, top - 0.012, 0.1, 0.36, "safety_yel");
  WB(A, F, a0 - 0.1, a1 + 0.1, top - 0.012, top + 0.1, 0.1, 0.36, "panel_dk");
  WB(A, F, a0 - 0.1, a1 + 0.1, top + 0.15, top + 0.6, 0.3, 0.7, "panel_dk", L1);           // drum housing (inside)
  WB(A, F, a0 - 0.45, a0 - 0.1, floor - 0.2, top + 0.1, -0.45, 0, "rubber", L1);           // dock shelter
  WB(A, F, a1 + 0.1, a1 + 0.45, floor - 0.2, top + 0.1, -0.45, 0, "rubber", L1);
  WB(A, F, a0 - 0.45, a1 + 0.45, top + 0.1, top + 0.6, -0.45, 0, "rubber", L1);
  WB(A, F, a0 - 0.06, a1 + 0.06, floor - 0.04, floor + 0.008, -0.3, 0.6, "steel_plate");           // dock leveller lip
  const axis = F.axis === "x" ? "x" : "z", hp = P(F.axis, c, top, ld0);
  hinged(A, { id, kind: "door", label: "roller door", sound: "steel", axis, openAngle: 88, duration: 2.4,
    leaves: [{ hinge: hp, want: F.axis === "x" ? [0, 0, -F.out] : [-F.out, 0, 0],
      build: (T) => {
        wbox(A, F.axis, a0 + 0.02, a1 - 0.02, floor + 0.01, top - 0.02, ld0, ld1, "corrugated", T);
        for (let y = floor + 0.55; y < top - 0.2; y += 0.55) wbox(A, F.axis, a0 + 0.01, a1 - 0.01, y - 0.015, y + 0.015, ld1, ld1 + F.out * 0.012, "panel_dk", T);
        wbox(A, F.axis, c - 0.25, c + 0.25, floor + 0.15, floor + 0.2, ld0 - F.out * 0.03, ld0, "alu_lt", T);
      },
      collider: [P(F.axis, a0 + 0.01, floor + 0.01, ld0), P(F.axis, a1 - 0.01, top - 0.01, ld1)].map((p, i, arr) => [0, 1, 2].map((k) => (i ? Math.max : Math.min)(arr[0][k], arr[1][k]))) }],
    aperture: aabb(P(F.axis, a0, floor, F.inner), P(F.axis, a1, top, F.inner + F.out * 0.14)) });
  return { a0, a1, y0: -10, y1: top };
}

// ============================================================================ stairs, cores, lifts
// Straight precast flight along axis ("z" or "x"): solid step blocks (tread_x, read as stairs by interior.cpp), the
// invisible smooth collision ramp through the tread centres (as parts.mjs stairs()), a soffit slab, optional glass guards.
function flight(A, { axis = "z", b0, b1, a0, dir, y0, rise, n, run, m = "tread_x", soffit = "concrete_wash", guard = false }) {
  const r = rise / n, d = run / n, aAt = (i) => a0 + dir * i * d;
  const B = (aa, ab, ya, yb, mm, o) => (axis === "z" ? A.box([b0, ya, Math.min(aa, ab)], [b1, yb, Math.max(aa, ab)], mm, o) : A.box([Math.min(aa, ab), ya, b0], [Math.max(aa, ab), yb, b1], mm, o));
  for (let i = 0; i < n; i++) { const tp = y0 + (i + 1) * r; B(aAt(i), aAt(i + 1), tp - 0.2, tp, m, { k: i === n - 1 ? 1 : 0 }); }
  const th = Math.atan2(r, d), deg = (th * 180) / Math.PI;
  const pA = [aAt(-0.5) - dir * 0.15 * Math.cos(th), y0 - 0.15 * Math.sin(th)], pB = [aAt(n - 0.5), y0 + rise];
  const Lr = Math.hypot(pB[0] - pA[0], pB[1] - pA[1]), ma = (pA[0] + pB[0]) / 2, my = (pA[1] + pB[1]) / 2, nA = -dir * Math.sin(th), nY = Math.cos(th);
  const ramp = (thick, up, mm, o, c0 = b0, c1 = b1) => {
    const ca = ma + nA * up, cy = my + nY * up;
    return axis === "z" ? A.obox([(c0 + c1) / 2, cy, ca], [c1 - c0, thick, Lr], [dir < 0 ? deg : -deg, 0, 0], mm, o) : A.obox([ca, cy, (c0 + c1) / 2], [Lr, thick, c1 - c0], [0, 0, dir > 0 ? deg : -deg], mm, o);
  };
  ramp(0.4, -0.2, m, { v: 0 });
  ramp(0.3, -0.3, soffit, K0, b0 + 0.012, b1 - 0.012);
  if (guard) for (const [c0, c1] of [[b0 - 0.04, b0], [b1, b1 + 0.04]]) {
    ramp(1.0, 0.55, "glass_t", {}, c0, c1);
    ramp(0.05, 1.08, "alu_lt", L1, c0 - 0.02, c1 + 0.02);
  }
}

// a switchback stair core (house() layout): flight 1 at xa..xa+CW from the corridor edge zA along dir, a half landing,
// flight 2 back beside it. ys: the levels it serves (the roof included when roof); enc: enclosure walls each side.
function mkCore({ xa, zA, dir = 1, ys, encL = false, encR = true, encTo = null, roof = false, tag = "core" }) {
  const hs = ys[1] - ys[0], nr = Math.round(hs / 2 / 0.18), R = nr * 0.27, len = 2 * CW + SP, zEnd = zA + dir * (R + LAND);
  const [h0, h1] = zr(zA, zEnd);
  const k = { xa, zA, dir, ys, encL, encR, encTo: encTo ?? zEnd + dir * ENC, roof, tag, nr, R, len, zEnd, hole: null, x0: xa - (encL ? ENC : 0), x1: xa + len + (encR ? ENC : 0) };
  { const [g0, g1] = zr(zA, encTo ?? zEnd + dir * ENC); k.hole = { a0: k.x0, a1: k.x1, y0: g0, y1: g1 }; }   // the enclosure walls stand in the hole: no slab edge on their faces
  k.cuts = (y) => ys.slice(1).some((v) => Math.abs(v - y) < 0.01);
  k.zF = zA - dir * 1.9;   // the bulkhead's door face on the roof
  k.obs = roof ? [xa - 0.3, Math.min(k.zF, zEnd + dir * ENC), xa + len + 0.3, Math.max(k.zF, zEnd + dir * ENC)] : null;
  return k;
}
// rooms: the nav room the stair leaves from on each level (linked level to level at the foot of each flight pair)
function buildCore(A, k, ceil, rooms = [], bulkMat = "panel_lt") {
  const { xa, zA, dir, ys, nr, R, len, zEnd } = k, hs = ys[1] - ys[0];
  for (let i = 0; i + 1 < rooms.length; i++) navDoor(A, rooms[i], rooms[i + 1], [xa + CW / 2, ys[i], zA + dir * 0.3]);
  for (let i = 0; i + 1 < ys.length; i++) {
    const y = ys[i], hh = ys[i + 1] - y, [l0, l1] = zr(zA + dir * R, zEnd);
    flight(A, { b0: xa, b1: xa + CW, a0: zA, dir, y0: y, rise: hh / 2, n: nr, run: R });
    A.box([xa, y + hh / 2 - 0.2, l0], [xa + len, y + hh / 2, l1], "landing_x");
    A.box([xa + 0.05, y + hh / 2 - 0.015, zEnd - dir * 0.08], [xa + len - 0.05, y + hh / 2 + 0.008, zEnd - dir * 0.03], "rubber", L1);   // landing edge nosing strip
    flight(A, { b0: xa + CW + SP, b1: xa + len, a0: zA + dir * R, dir: -dir, y0: y + hh / 2, rise: hh / 2, n: nr, run: R });
  }
  const yTop = k.roof ? ys.at(-1) + 2.75 : ceil, yBot = ys[0];
  const [s0, s1] = zr(zA, zA + dir * R), [e0, e1] = zr(zEnd, zEnd + dir * ENC), [c0, c1] = zr(zA, k.encTo);
  A.box([xa + CW, yBot, s0], [xa + CW + SP, yTop, s1], "plaster");            // spine
  A.box([xa, yBot, e0], [xa + len, yTop, e1], "plaster");                     // end wall
  if (k.encR) A.box([xa + len, yBot, c0], [xa + len + ENC, yTop, c1], "plaster");
  if (k.encL) A.box([xa - ENC, yBot, c0], [xa, yTop, c1], "plaster");
  A.box([xa + CW + SP, ys[0], s0], [xa + len, ys[0] + hs / 2 - 0.6, s1], "plaster");   // closes the void under the first return
  railing(A, "x", xa, xa + CW, zA + dir * 0.04, ys.at(-1));
  if (!k.roof) return;
  // roof bulkhead: three walls round the core head, the door wall on the roof side, a slab and a coping
  const yr = ys.at(-1), bx0 = xa - 0.3, bx1 = xa + len + 0.3, zB = zEnd + dir * ENC, [b0, b1] = zr(k.zF, zB), yb = yr + 2.75;
  A.box([bx0, yr, b0], [xa, yb, b1], bulkMat); A.box([xa + len, yr, b0], [bx1, yb, b1], bulkMat);
  A.box([xa, yr, e0], [xa + len, yb, e1], bulkMat, { k: 0 });
  const F = { axis: "x", out: -dir, outer: k.zF, inner: k.zF + dir * 0.2, yaw: dir > 0 ? 180 : 0 };
  const hole = mdoor(A, F, { id: `${k.tag}_roof_door`, c: xa + len / 2, floor: yr, w: 1.0, h: 2.15, depth: 0.2, label: "roof door" });
  for (const [a0, a1, y0, y1] of cutRect(xa, xa + len, yr, yb, [hole])) wbox(A, "x", a0, a1, y0, y1, k.zF, k.zF + dir * 0.2, bulkMat);
  A.box([bx0 - 0.1, yb, b0 - 0.1], [bx1 + 0.1, yb + 0.22, b1 + 0.1], "roof_mem");
  A.box([bx0 - 0.14, yb + 0.22, b0 - 0.14], [bx1 + 0.14, yb + 0.28, b1 + 0.14], "alu", { k: 0, bv: 0.01 });
  wbox(A, "x", xa + len / 2 - 0.9, xa + len / 2 + 0.9, yr + 2.3, yr + 2.36, k.zF - dir * 0.9, k.zF, "alu_lt", L1);   // door canopy
  wbox(A, "x", xa + len / 2 + 0.75, xa + len / 2 + 0.95, yr + 2.0, yr + 2.15, k.zF - dir * 0.08, k.zF, "light_panel", L1);
  A.box([xa + 0.2, yb - 0.05, Math.min(e0, e0 - dir * 2) + 0.3], [xa + len - 0.2, yb - 0.02, Math.max(e1, e1 - dir * 2) - 0.3], "light_panel", L1);
  // landing in front of the door
  wbox(A, "x", xa - 0.2, xa + len + 0.2, yr, yr + 0.04, k.zF - dir * 1.4, k.zF, "paving");
}

function mkLift({ x0, x1, z0, z1, door, ys, tag = "lift" }) {
  const L = { x0, x1, z0, z1, door, ys, tag, hole: { a0: x0, a1: x1, y0: z0, y1: z1 }, obs: [x0, z0, x1, z1] };
  L.cuts = (y) => y > ys[0] + 0.01;   // every slab above the ground, the roof included
  return L;
}
function buildLift(A, L, roofY) {
  const yb = L.ys[0], yt = roofY + 1.5, cx = (L.x0 + L.x1) / 2, s = L.door === "front" ? 1 : -1, zf = s > 0 ? L.z1 : L.z0;
  A.box([L.x0, yb, L.z0], [L.x0 + LT, yt, L.z1], "concrete_wash");
  A.box([L.x1 - LT, yb, L.z0], [L.x1, yt, L.z1], "concrete_wash");
  A.box([L.x0 + LT, yb, s > 0 ? L.z0 : L.z1 - LT], [L.x1 - LT, yt, s > 0 ? L.z0 + LT : L.z1], "concrete_wash");
  const holes = L.ys.map((y) => ({ a0: cx - 0.55, a1: cx + 0.55, y0: y, y1: y + 2.1 }));
  for (const [a0, a1, y0, y1] of cutRect(L.x0 + LT, L.x1 - LT, yb, yt, holes)) wbox(A, "x", a0, a1, y0, y1, zf - s * LT, zf, "plaster");
  for (const y of L.ys) {
    wbox(A, "x", cx - 0.55, cx + 0.55, y, y + 2.1, zf - s * 0.13, zf - s * 0.08, "lift_steel");
    wbox(A, "x", cx - 0.004, cx + 0.004, y + 0.02, y + 2.08, zf - s * 0.08, zf - s * 0.072, "panel_dk", L1);
    for (const e of [-1, 1]) wbox(A, "x", cx + e * 0.55 - (e > 0 ? 0 : 0.08), cx + e * 0.55 + (e > 0 ? 0.08 : 0), y, y + 2.1, zf, zf + s * 0.025, "lift_steel", L1);
    wbox(A, "x", cx - 0.63, cx + 0.63, y + 2.1, y + 2.18, zf, zf + s * 0.025, "lift_steel", L1);
    wbox(A, "x", cx + 0.75, cx + 0.87, y + 1.0, y + 1.25, zf, zf + s * 0.02, "lift_steel", L1);   // call button panel
    wbox(A, "x", cx - 0.3, cx + 0.3, y + 2.25, y + 2.4, zf, zf + s * 0.03, "screen", L1);         // floor indicator
  }
  A.box([L.x0 - 0.05, yt, L.z0 - 0.05], [L.x1 + 0.05, yt + 0.15, L.z1 + 0.05], "alu", { bv: 0.01 });
  for (let x = L.x0 + 0.3; x < L.x1 - 0.2; x += 0.25) wbox(A, "x", x - 0.04, x + 0.04, roofY + 0.4, roofY + 1.2, zf, zf + s * 0.03, "louvre", L1);   // overrun vent
}

// ============================================================================ wings
function geom(w) {
  const t = (s) => (w.omit.includes(s) ? 0 : w.T);
  w.ix0 = w.x0 + t("west"); w.ix1 = w.x1 - t("east"); w.iz0 = w.z0 + t("rear"); w.iz1 = w.z1 - t("front");
}
function sideF(w, s) {
  const { x0, x1, z0, z1, T } = w, om = (k) => w.omit.includes(k);
  const ez0 = om("rear") ? z0 : z0 + T - PT, ez1 = om("front") ? z1 : z1 - T + PT;
  return {
    front: { side: s, axis: "x", out: 1, outer: z1, inner: z1 - T, d: z1, aOut: [x0, x1], aIn: [w.ix0, w.ix1], yaw: 0 },
    rear: { side: s, axis: "x", out: -1, outer: z0, inner: z0 + T, d: z0, aOut: [x0, x1], aIn: [w.ix0, w.ix1], yaw: 180 },
    east: { side: s, axis: "z", out: 1, outer: x1, inner: x1 - T, d: x1, aOut: [ez0, ez1], aIn: [w.iz0, w.iz1], yaw: 90 },
    west: { side: s, axis: "z", out: -1, outer: x0, inner: x0 + T, d: x0, aOut: [ez0, ez1], aIn: [w.iz0, w.iz1], yaw: 270 },
  }[s];
}
const RH = (r) => ({ a0: r[0], a1: r[2], y0: r[1], y1: r[3] });   // [x0, z0, x1, z1] as a cutRect hole

// A rectangular wing. cfg: tag, x0 x1 z0 z1 (outer), n storeys of h, gf (ground floor level), T, omit (sides left to a
// taller neighbour), base (false: stands on another wing's roof), skin(f, side), band (floor band material or null),
// fm (floor material) / special [[x0, z0, x1, z1, m]] ground floor areas, groundHoles, roofHoles, cores, lifts (those
// passing through), doors [{id, side, c, w, h, double, glass, roller, room, to, f}], links (open holes to a neighbour:
// {side, c, w, h, a, b}), glaze(f) -> {side: [[a0, a1, sill, top, unit]]}, plan(f, ctx) -> {rooms, walls, doors},
// par (parapet height), parSkip {side: [[a0, a1]]}, roof(A, w) (plant), dress(A, w).
function wing(A, cfg) {
  const w = { T: 0.3, gf: GF, omit: [], n: 1, base: true, par: 1.1, cores: [], lifts: [], doors: [], links: [], special: [], band: "panel_dk", ...cfg, _h: {} };
  if (w.n > 1 && !w.band) w.band = "precast";   // NT2b: a floor band between storeys on every multi-storey wing
  geom(w);
  const { x0, x1, z0, z1, n, h, gf } = w, yAt = (f) => gf + f * h, top = yAt(n);
  Object.assign(w, { yAt, top });
  const shafts = (y) => [...w.cores, ...w.lifts].filter((k) => k.cuts(y)).map((k) => k.hole);
  const slab = (y0, y1, m, holes, o = {}, r = [w.ix0, w.iz0, w.ix1, w.iz1]) => { for (const [a, b, c, d] of cutRect(r[0], r[2], r[1], r[3], holes)) A.box([a, y0, c], [b, y1, d], m, o); };
  // ---- ground, floors, ceilings, roof
  const gh = (w.groundHoles ?? []).map(RH);
  if (w.base) {
    slab(-0.5, gf - 0.05, "plinth", gh, {}, [x0, z0, x1, z1]);
    slab(gf - 0.05, gf, w.fm ?? "floor_x", gh.concat(w.special.map(RH)));
    for (const [a, b, c, d, m] of w.special) slab(gf - 0.05, gf, m, gh, {}, [a, b, c, d]);
  } else slab(gf - SLAB, gf, w.fm ?? "floor_x", shafts(gf));
  for (let f = 1; f <= n; f++) {
    const y = yAt(f), hs = shafts(y), roof = f === n;
    slab(y - SLAB, y, roof ? "roof_mem" : "floor_x", roof ? hs.concat((w.roofHoles ?? []).map(RH)) : hs);
    slab(y - SLAB - 0.02, y - SLAB, "ceiling", hs, K0);
    for (let x = w.ix0 + 3; x < w.ix1 - 1.5; x += 6) for (let z = w.iz0 + 2.6; z < w.iz1 - 1.5; z += 5.6) {   // recessed light panels
      if (hs.some((q) => x > q.a0 - 0.8 && x < q.a1 + 0.8 && z > q.y0 - 0.8 && z < q.y1 + 0.8)) continue;
      A.box([x - 0.3, y - SLAB - 0.03, z - 0.6], [x + 0.3, y - SLAB - 0.019, z + 0.6], "light_panel", L1);
    }
  }
  // ---- walls (one per side and storey), doors, links, glazing, floor bands
  for (let f = 0; f < n; f++) {
    const y = yAt(f), yb = f === 0 ? (w.base ? gf - 0.05 : gf) : y - SLAB, yt = f === n - 1 ? top : yAt(f + 1) - SLAB;
    for (const s of SIDES) {
      if (w.omit.includes(s)) continue;
      const F = sideF(w, s), H = [];
      for (const d of w.doors.filter((q) => q.side === s && (q.f ?? 0) === f)) {
        H.push(d.roller ? roller(A, F, { ...d, floor: y }) : mdoor(A, F, { ...d, floor: y }));
        const p = F.axis === "x" ? [d.c, y, F.outer - F.out * 0.15] : [F.outer - F.out * 0.15, y, d.c];
        navDoor(A, d.room, d.to ?? "outside", p);
        if (!d.to) {
          const e = F.axis === "x" ? [d.c, 0, F.outer + F.out * 1.0] : [F.outer + F.out * 1.0, 0, d.c];
          A._nav.entrances.push({ p: e.map(q4), dir: F.axis === "x" ? [0, 0, -F.out] : [-F.out, 0, 0], room: d.room });
          if (d.apron !== false && y < 0.6) { const hw = (d.w ?? 1) / 2 + 0.6; fbox(A, F, d.c - hw, d.c + hw, -0.2, y - 0.03, 0, 1.4, "paving", { bv: 0.015 }); }
        }
      }
      for (const l of w.links.filter((q) => q.side === s && (q.f ?? 0) === f)) {
        const lh = y + (l.h ?? 2.4), a0 = l.c - l.w / 2, a1 = l.c + l.w / 2;
        H.push({ a0, a1, y0: -10, y1: lh });
        WB(A, F, a0, a1, y - 0.04, y + 0.008, -0.02, w.T + 0.02, "alu_lt");                                      // threshold strip
        for (const [p0, p1] of [[a0 - 0.05, a0 + 0.01], [a1 - 0.01, a1 + 0.05]]) WB(A, F, p0, p1, y, lh - 0.01, -0.02, w.T + 0.02, "door_frame", L1);
        WB(A, F, a0 - 0.05, a1 + 0.05, lh - 0.01, lh + 0.05, -0.02, w.T + 0.02, "door_frame", L1);
        navDoor(A, l.a, l.b, F.axis === "x" ? [l.c, y, F.outer - F.out * w.T / 2] : [F.outer - F.out * w.T / 2, y, l.c]);
      }
      const G = w.glaze?.(f)?.[s] ?? [];
      G.forEach((g, i) => H.push(ribbon(A, F, { id: `${w.tag}${s}${f}_${i}`, a0: g[0], a1: g[1], y0: y + g[2], y1: y + g[3], unit: g[4] ?? w.unit ?? 3.6, floor: y, fin: w.fin ?? 0.16, finMat: w.finMat, glass: w.glass, sill: w.sill, lintel: w.lintel })));
      wall(A, { axis: F.axis, outer: F.outer, inner: F.inner, aOut: F.aOut, aIn: F.aIn, y0: yb, y1: yt, holes: H, stone: w.skin(f, s), plaster: w.plaster ?? "plaster", pt: PT });
      (w._h[s] ??= []).push(...H.map((q) => ({ ...q })));
      if (f > 0 && w.band) {
        const [a0, a1] = F.axis === "x" ? [x0 - 0.094, x1 + 0.094] : [z0 + 0.005, z1 - 0.005];   // NT2b: 9 cm proud
        fbox(A, F, a0, a1, y - SLAB - 0.06, y + 0.04, -0.005, 0.09, w.band, { k: 0, bv: 0.015 });
      }
    }
  }
  // ---- parapets and copings (left out where a bulkhead or a lift overrun stands on the wall, or where told)
  const obs = [...w.cores, ...w.lifts].map((k) => k.obs).filter(Boolean);
  for (const s of SIDES) {
    if (w.omit.includes(s) || w.par <= 0) continue;
    const F = sideF(w, s), isX = F.axis === "x";
    const [a0, a1] = isX ? [x0, x1] : [w.z0 + (w.omit.includes("rear") ? 0 : w.T), w.z1 - (w.omit.includes("front") ? 0 : w.T)];
    const [d0, d1] = zr(F.inner, F.outer);
    const skip = (w.parSkip?.[s] ?? []).concat(obs.filter((o) => (isX ? o[1] < d1 && o[3] > d0 : o[0] < d1 && o[2] > d0)).map((o) => (isX ? [o[0], o[2]] : [o[1], o[3]])));
    for (const [p0, p1] of cutRect(a0, a1, 0, 1, skip.map(([u, v]) => ({ a0: u, a1: v, y0: -1, y1: 2 }))).map((r) => [r[0], r[1]])) {
      wbox(A, F.axis, p0, p1, top, top + w.par, F.inner, F.outer, w.parMat ?? w.skin(n - 1, s), { bv: 0.012 });
      const e = isX ? 0.04 : -0.03;
      wbox(A, F.axis, p0 - (p0 === a0 ? e : 0), p1 + (p1 === a1 ? e : 0), top + w.par, top + w.par + 0.06, F.inner - F.out * 0.03, F.outer + F.out * 0.04, "alu", { bv: 0.01 });
    }
  }
  // ---- plans
  for (let f = 0; f < n; f++) {
    const y = yAt(f), tp = f === n - 1 ? top - SLAB : yAt(f + 1) - SLAB;
    const pl = w.plan?.(f, { y, tp, w });
    if (!pl) continue;
    for (const r of pl.rooms) addRoom(A, r.id, r.type, r.x0, y, r.z0, r.x1, tp, r.z1);
    for (const p of pl.walls) pwall(A, p, y, tp - 0.02);
    for (const d of pl.doors ?? []) navDoor(A, d.a, d.b, [d.p[0], y, d.p[1]]);
  }
  wingDetail(A, w);
  w.roof?.(A, w);
  w.dress?.(A, w);
  if (w.fac) wingTown(A, w);
  return w;
}
// TD1 town facade pass (tools/catalog_modern_buildings_a.mjs tdTown) on a finished wing. fac: { m (cornice), prof, sides,
// wall (crest / pier material), crest [{side, a, w, kind, rise}], piers (material), quoin (material), courses (floor
// indices: a string course under each), fe {side, len, from, f0 (lowest landing floor), lite}, roof [kinds], cornice }
function wingTown(A, w) {
  const o = w.fac;
  // NT2b pilasters: { m, at: [a...], side, f0 (lowest floor), w, p } through the floor bands, up to the wall top
  const autoAt = (pl) => {   // between the openings of the floors from f0 up, and at both corners
    const s = pl.side ?? "front", F = sideF(w, s), [lo, hi] = F.axis === "x" ? [w.x0, w.x1] : [w.z0, w.z1], pw = pl.w ?? 0.5, y0 = w.yAt(pl.f0 ?? 0);
    const iv = (w._h[s] ?? []).filter((q) => q.y1 > y0 + 0.1).map((q) => [q.a0 - 0.2, q.a1 + 0.2]).sort((p, q) => p[0] - q[0]), out = [lo + pw / 2 + 0.02, hi - pw / 2 - 0.02];
    for (let i = 0; i + 1 < iv.length; i++) { const g0 = Math.max(...iv.slice(0, i + 1).map((q) => q[1])), g1 = iv[i + 1][0]; if (g1 - g0 >= pw + 0.3) out.push((g0 + g1) / 2); }
    return out;
  };
  for (const pl of [o.pil ?? []].flat()) for (const a of pl.at === "auto" ? autoAt(pl) : pl.at) tdPilaster(A, sideF(w, pl.side ?? "front"), a, pl.f0 ? w.yAt(pl.f0) + 0.05 : w.gf + 0.4, w.top - 0.02, pl.m ?? w.skin(w.n - 1, pl.side ?? "front"), { w: pl.w ?? 0.5, p: pl.p ?? 0.13, ignore: [w.band] });
  const R = { x0: w.x0, x1: w.x1, z0: w.z0, z1: w.z1 }, live = (c) => !w.omit.includes(c[0] === "f" ? "front" : "rear") && !w.omit.includes(c[1] === "e" ? "east" : "west");
  let fe = null;
  if (o.fe) {
    const F = sideF(w, o.fe.side), [lo, hi] = F.axis === "x" ? [w.x0, w.x1] : [w.z0, w.z1], f0 = o.fe.f0 ?? 1;
    fe = { ...o.fe, lo: lo + 0.6, hi: hi - 0.6, len: o.fe.len ?? Math.max(4.2, 1.4 + 0.8 * w.h), ys: Array.from({ length: w.n - f0 }, (_, i) => w.yAt(i + f0)) };
  }
  tdTown(A, R, { ...o, sides: (o.sides ?? SIDES).filter((s) => !w.omit.includes(s)), wall: o.wall ?? w.parMat ?? w.skin(w.n - 1, "front"),
    yW: w.top, yd: w.top, yt: w.top + w.par, t: w.T, fe, courses: (o.courses ?? []).map((f) => w.yAt(f) - SLAB - 0.08), yq0: o.yq0 ?? w.gf + 0.75,
    pierCorners: o.pierCorners ?? ["fe", "fw", "re", "rw"].filter(live), quoinCorners: o.quoinCorners ?? ["fe", "fw", "re", "rw"].filter(live) });
}

// interior partition with door openings (head trim per opening); hole y0 set = a hatch (no nav door, no trim)
function pwall(A, p, y, tp) {
  const t = p.t ?? 0.12, holes = (p.holes ?? []).map((q) => ({ a0: q.c - q.w / 2, a1: q.c + q.w / 2, y0: q.y0 !== undefined ? y + q.y0 : y - 1, y1: y + (q.h ?? 2.1) }));
  partition(A, { axis: p.axis, d0: p.d - t / 2, d1: p.d + t / 2, a0: p.a0, a1: p.a1, y0: y, y1: tp, holes, m: p.m ?? "plaster" });
  for (const q of p.holes ?? []) {
    if (q.y0 !== undefined) continue;
    const hh = y + (q.h ?? 2.1);
    wbox(A, p.axis, q.c - q.w / 2 - 0.06, q.c + q.w / 2 + 0.06, hh - 0.01, hh + 0.06, p.d - t / 2 - 0.015, p.d + t / 2 + 0.015, "door_frame", L1);
    as1(A, () => { for (const s of [-1, 1]) {   // AS1: lining jambs (a frame with depth), never into a door leaf's aperture
      const e = q.c + s * q.w / 2, [u0, u1] = s < 0 ? [e - 0.06, e + 0.012] : [e - 0.012, e + 0.06];
      const mn = P(p.axis, u0, y, p.d - t / 2 - 0.015), mx = P(p.axis, u1, hh - 0.01, p.d + t / 2 + 0.015);
      if (!apHit(A, mn, mx)) A.box(mn, mx, "door_frame", L1);
    } }, true);
    if (q.a) navDoor(A, q.a, q.b, p.axis === "x" ? [q.c, y, p.d] : [p.d, y, q.c]);
  }
}

// Double-loaded floor: a corridor zc = [c0, c1] along x, bands of items in front (z > c1) and behind (z < c0). Items
// left to right: {x1, id, type, open (no corridor wall), dc / dw / dh (door), m (wall material), side: {c, w} (a door
// into the next room)} or {x1, skip} (a stair core or lift shaft: no walls, its own enclosure).
function dl(w, { pre, zc, front = [], rear = [], t = 0.12 }) {
  const rooms = [], walls = [], doors = [], [c0, c1] = zc, cid = pre + "corridor";
  rooms.push({ id: cid, type: "hall", x0: w.ix0, z0: c0, x1: w.ix1, z1: c1 });
  const band = (items, sgn) => {
    const zIn = sgn > 0 ? c1 : c0, zP = zIn + sgn * t, zOut = sgn > 0 ? w.iz1 : w.iz0;
    let a = w.ix0;
    items.forEach((it, i) => {
      const b = it.x1 ?? w.ix1, prev = items[i - 1], next = items[i + 1];
      if (!it.skip) {
        const id = pre + it.id, zA = it.open ? zIn : zP, rx0 = prev && !prev.skip ? a + t / 2 : a, rx1 = next && !next.skip ? b - t / 2 : b;
        const [rz0, rz1] = zr(zA, zOut);
        rooms.push({ id, type: it.type, x0: rx0, z0: rz0, x1: rx1, z1: rz1 });
        const dc = it.dc ?? (rx0 + rx1) / 2;
        if (it.open) doors.push({ a: id, b: cid, p: [dc, zIn] });
        else walls.push({ axis: "x", d: zIn + (sgn * t) / 2, a0: prev?.open ? a - t / 2 : a, a1: next?.open ? b + t / 2 : b, t, m: it.m, holes: [{ c: dc, w: it.dw ?? 1.0, h: it.dh ?? 2.1, a: id, b: cid }] });
        if (next && !next.skip) {
          const [p0, p1] = zr(it.open && next.open ? zIn : zP, zOut);
          walls.push({ axis: "z", d: b, a0: p0, a1: p1, t, holes: it.side ? [{ c: it.side.c, w: it.side.w ?? 0.9, a: id, b: pre + next.id }] : [] });
        }
      }
      a = b;
    });
  };
  band(front, 1); band(rear, -1);
  return { rooms, walls, doors };
}

// ============================================================================ AS1 exterior detail pass
// Detail queued while a building is laid out and added once it is complete (finish), only while the asset stays under the
// part budget: entrance canopies and sign boards, a plinth band, downpipes, roof extract fans and dishes (wing), head drips
// over ribbon glazing (low), lining jambs on partition doorways (low). Every piece is k:0 / lod:1, goes only where nothing
// stands yet (isFree) and never into a door or window aperture.
const AS1_MAX = 1480;
function as1(A, fn, low = false) { (low ? (A._as1b ??= []) : (A._as1 ??= [])).push(fn); }
function as1Run(A) { for (const q of [A._as1, A._as1b]) for (const fn of q?.splice(0) ?? []) if (A.parts.length < AS1_MAX) fn(); }
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
// world box of a facade-frame box (a along the wall, e out of the face)
const fbb = (F, a0, a1, y0, y1, e0, e1) => { const d0 = F.d + F.out * e0, d1 = F.d + F.out * e1; return [P(F.axis, Math.min(a0, a1), y0, Math.min(d0, d1)), P(F.axis, Math.max(a0, a1), y1, Math.max(d0, d1))]; };
// rainwater downpipe on face F at a from the ground to a hopper head under a parapet outlet at the roof level top
function dpipeW(A, F, a, top) {
  const yh = top - 0.28, Q = (yy, e) => (F.axis === "x" ? [a, yy, F.d + F.out * e] : [F.d + F.out * e, yy, a]);
  A.cyl(Q((0.05 + yh) / 2, 0.11), [0.1, yh - 0.05, 0.1], "rainwater", { seg: 10, ...L1 });
  fbox(A, F, a - 0.07, a + 0.07, 0.02, 0.12, 0.04, 0.24, "rainwater", L1);                          // shoe
  fbox(A, F, a - 0.13, a + 0.13, yh, top + 0.03, 0.03, 0.22, "rainwater", { ...L1, bv: 0.01 });     // hopper head
  fbox(A, F, a - 0.06, a + 0.06, top + 0.02, top + 0.1, -0.01, 0.16, "rainwater", L1);             // outlet through the parapet
  for (let y = 1.0; y < yh - 0.5; y += 3.0) fbox(A, F, a - 0.07, a + 0.07, y - 0.025, y + 0.025, -0.01, 0.17, "rainwater", L1);   // brackets
}
// roof plant in the first free spots of x0..x1, z0..z1 (from the rear): a mushroom extract fan and a satellite dish
function roofKit(A, x0, x1, z0, z1, y, { fan = true, dish = true, ignore = [] } = {}) {
  const spot = (r) => { for (let z = z0 + r; z <= z1 - r; z += 0.8) for (let x = x0 + r; x <= x1 - r; x += 0.8) if (isFree(A, [x - r, y + 0.01, z - r], [x + r, y + 1.7, z + r], ignore)) return [x, z]; return null; };
  let p;
  if (fan && (p = spot(0.55))) {
    const [x, z] = p;
    A.box([x - 0.4, y, z - 0.4], [x + 0.4, y + 0.3, z + 0.4], "hvac", { ...L1, bv: 0.015 });   // kerb upstand
    A.cyl([x, y + 0.5, z], [0.56, 0.4, 0.56], "louvre", { seg: 14, ...L1 });
    A.cone([x, y + 0.82, z], [0.8, 0.24, 0.8], "hvac", { seg: 14, top: 0.15, ...L1 });
  }
  if (dish && (p = spot(0.6))) {
    const [x, z] = p;
    A.box([x - 0.45, y, z - 0.45], [x + 0.45, y + 0.08, z + 0.45], "concrete_wash", L1);     // ballast frame
    A.cyl([x, y + 0.6, z], [0.06, 1.04, 0.06], "alu_lt", { seg: 8, ...L1 });                  // mast
    A.cyl([x, y + 1.1, z + 0.1], [0.75, 0.04, 0.75], "paint_w", { r: [70, 0, 0], seg: 16, ...L1 });
    A.obox([x, y + 1.15, z + 0.38], [0.025, 0.025, 0.5], [-20, 0, 0], "alu_lt", L1);         // feed arm
    A.box([x - 0.04, y + 1.2, z + 0.58], [x + 0.04, y + 1.28, z + 0.68], "panel_dk", L1);   // LNB
  }
}
// entrance canopies, sign boards, a plinth band, downpipes and roof plant on a finished wing (queued)
function wingDetail(A, w) {
  const { gf, top } = w;
  for (const s of SIDES) {
    if (w.omit.includes(s)) continue;
    const F = sideF(w, s), isX = F.axis === "x", H = w._h[s] ?? [];
    for (const d of w.doors.filter((q) => q.side === s && (q.f ?? 0) === 0 && !q.to && !q.roller && gf < 0.6)) as1(A, () => {
      const hw = (d.w ?? 1) / 2, yt = gf + (d.h ?? 2.2), a0 = d.c - hw - 0.45, a1 = d.c + hw + 0.45;
      if (yt + 0.6 > top || !isFree(A, ...fbb(F, a0, a1, yt + 0.05, yt + 1.6, 0.01, 1.3))) return;
      fbox(A, F, a0, a1, yt + 0.25, yt + 0.37, -0.01, 1.1, "panel_lt", { ...L1, bv: 0.01 });
      fbox(A, F, a0 - 0.02, a1 + 0.02, yt + 0.21, yt + 0.41, 1.1, 1.14, "alu_lt", L1);
      fbox(A, F, d.c - 0.3, d.c + 0.3, yt + 0.235, yt + 0.25, 0.45, 0.75, "light_panel", L1);
      if (d.glass && yt + 1.1 < top - 0.1 && !H.some((q) => q.a1 > a0 && q.a0 < a1 && q.y1 > yt + 0.5 && q.y0 < yt + 1.1)) {
        fbox(A, F, a0 + 0.1, a1 - 0.1, yt + 0.55, yt + 1.05, -0.005, 0.06, "alu", L1);
        fbox(A, F, a0 + 0.15, a1 - 0.15, yt + 0.6, yt + 1.0, 0.06, 0.09, "sign_blank", L1);
      }
    });
    if (w.base) as1(A, () => {
      const holes = H.filter((q) => q.y0 < gf + 0.3).map((q) => ({ a0: q.a0 - 0.05, a1: q.a1 + 0.05, y0: q.y0 - 0.06, y1: q.y1 }));
      const [a0, a1] = isX ? [w.x0 - 0.04, w.x1 + 0.04] : [w.z0, w.z1];   // side bands end inside the front / rear ones, 1 cm lower: no shared faces
      for (const [b0, b1, y0, y1] of cutRect(a0, a1, -0.15, gf + (isX ? 0.25 : 0.24), holes))
        if (b1 - b0 > 0.3 && isFree(A, ...fbb(F, b0 + 0.1, b1 - 0.1, y0, y1, 0.008, 0.03), ["paving"])) fbox(A, F, b0, b1, y0, y1, -0.005, 0.035, "plinth", { ...L1, bv: 0.008 });
    });
    if (w.par > 0 && w.dp !== false) {
      const [lo, hi] = isX ? [w.x0, w.x1] : [w.z0, w.z1];
      for (const a of s === "rear" ? [lo + 0.3, hi - 0.3] : s === "front" ? [] : [hi - 0.3]) as1(A, () => {
        if (H.some((q) => q.a0 - 0.25 < a && q.a1 + 0.25 > a) || !isFree(A, ...fbb(F, a - 0.13, a + 0.13, 0.05, top + 0.15, 0.05, 0.24))) return;
        dpipeW(A, F, a, top);
      });
    }
  }
  if (w.par > 0 && (w.ix1 - w.ix0) * (w.iz1 - w.iz0) > 40) as1(A, () => roofKit(A, w.ix0 + 0.4, w.ix1 - 0.4, w.iz0 + 0.4, w.iz1 - 0.4, top));
}

// ============================================================================ dressing helpers
function hvac(A, x, z, y, sx = 2.4, sz = 1.4, hh = 1.3) {
  A.box([x - sx / 2 - 0.1, y, z - sz / 2 - 0.1], [x + sx / 2 + 0.1, y + 0.15, z + sz / 2 + 0.1], "concrete_wash", { bv: 0.02 });
  A.box([x - sx / 2, y + 0.15, z - sz / 2], [x + sx / 2, y + 0.15 + hh, z + sz / 2], "hvac", { bv: 0.03 });
  for (const s of [-1, 1]) A.box([x - sx / 2 + 0.15, y + 0.35, s > 0 ? z + sz / 2 - 0.005 : z - sz / 2 - 0.015], [x + sx / 2 - 0.15, y + hh - 0.05, s > 0 ? z + sz / 2 + 0.015 : z - sz / 2 + 0.005], "louvre", L1);
  for (const dx of [-sx / 4, sx / 4]) {
    A.cyl([x + dx, y + 0.15 + hh + 0.04, z], [sz * 0.75, 0.08, sz * 0.75], "louvre", { k: 0, seg: 16 });
    A.cyl([x + dx, y + 0.15 + hh + 0.09, z], [0.18, 0.04, 0.18], "panel_dk", { k: 0, lod: 1, n: 8 });
  }
  A.box([x + sx / 2, y + 0.4, z - 0.15], [x + sx / 2 + 0.6, y + 0.7, z + 0.15], "alu_lt", L1);   // pipe stub
}
function tank(A, x, z, y, d = 2.2, hh = 2.4) {
  for (const [ox, oz] of [[-1, -1], [1, -1], [1, 1], [-1, 1]]) A.box([x + ox * d * 0.35 - 0.06, y, z + oz * d * 0.35 - 0.06], [x + ox * d * 0.35 + 0.06, y + 0.9, z + oz * d * 0.35 + 0.06], "steel_plate");
  A.box([x - d / 2, y + 0.9, z - d / 2], [x + d / 2, y + 1.0, z + d / 2], "steel_plate", { bv: 0.01 });
  A.cyl([x, y + 1.0 + hh / 2, z], [d - 0.1, hh, d - 0.1], "hvac", { seg: 20 });
  A.cone([x, y + 1.0 + hh + 0.15, z], [d - 0.1, 0.3, d - 0.1], "hvac", { k: 0, top: 0.2, seg: 20 });
  for (let k = 0; k < 3; k++) A.cyl([x, y + 1.3 + k * 0.7, z], [d - 0.06, 0.05, d - 0.06], "alu", { k: 0, lod: 1, seg: 20 });
  A.box([x + d / 2 - 0.05, y + 1.0, z - 0.2], [x + d / 2 + 0.05, y + 1.0 + hh, z - 0.16], "alu_lt", L1);   // ladder rails
  A.box([x + d / 2 - 0.05, y + 1.0, z + 0.16], [x + d / 2 + 0.05, y + 1.0 + hh, z + 0.2], "alu_lt", L1);
}
function plantScreen(A, x0, x1, z0, z1, y, hh = 1.8) {
  for (const [a, b, c, d] of [[x0, x1, z0, z0 + 0.06], [x0, x1, z1 - 0.06, z1], [x0, x0 + 0.06, z0 + 0.06, z1 - 0.06], [x1 - 0.06, x1, z0 + 0.06, z1 - 0.06]]) {
    A.box([a, y, c], [b, y + hh, d], "louvre", { v: 0 });
    A.box([a, y + hh - 0.08, c], [b, y + hh, d], "alu", K0);
    const along = b - a > d - c, n = Math.floor((along ? b - a : d - c) / 0.4);
    for (let i = 0; i <= n; i++) { const t = (along ? a : c) + (i * (along ? b - a : d - c)) / Math.max(n, 1); A.box(along ? [t - 0.03, y, c] : [a, y, t - 0.03], along ? [t + 0.03, y + hh - 0.08, d] : [b, y + hh - 0.08, t + 0.03], "louvre", L1); }
  }
}
function canopy(A, { x0, x1, z0, z1, y, cols = [], m = "panel_lt", fascia = "panel_acc" }) {
  A.box([x0, y, z0], [x1, y + 0.22, z1], m, { bv: 0.02 });
  A.box([x0 - 0.03, y - 0.08, z1 - 0.06], [x1 + 0.03, y + 0.32, z1 + 0.03], fascia, { k: 0, bv: 0.01 });
  for (let x = x0 + 1.2; x < x1 - 0.8; x += 2.4) A.box([x - 0.5, y - 0.012, (z0 + z1) / 2 - 0.1], [x + 0.5, y - 0.002, (z0 + z1) / 2 + 0.1], "light_panel", L1);
  for (const [cx, cz] of cols) { A.cyl([cx, y / 2, cz], [0.22, y, 0.22], "alu_lt", { seg: 12 }); A.cyl([cx, 0.03, cz], [0.4, 0.06, 0.4], "alu", { k: 0, seg: 12 }); }
}
function trusses(A, x0, x1, z0, z1, y, sp = 4.5, alongZ = true) {
  if (alongZ) for (let x = x0 + sp / 2; x < x1 - 0.5; x += sp) { A.box([x - 0.1, y - 0.7, z0], [x + 0.1, y - 0.62, z1], "panel_dk", L1); A.box([x - 0.1, y - 0.08, z0], [x + 0.1, y, z1], "panel_dk", L1); A.box([x - 0.02, y - 0.62, z0], [x + 0.02, y - 0.08, z1], "panel_dk", L1); }
  else for (let z = z0 + sp / 2; z < z1 - 0.5; z += sp) { A.box([x0, y - 0.7, z - 0.1], [x1, y - 0.62, z + 0.1], "panel_dk", L1); A.box([x0, y - 0.08, z - 0.1], [x1, y, z + 0.1], "panel_dk", L1); A.box([x0, y - 0.62, z - 0.02], [x1, y - 0.08, z + 0.02], "panel_dk", L1); }
}
function tour(A, start, steps) { setTour(A, { pos: start, yaw: 0 }, steps); }
function doorTour(A, id, x, zOut, zIn, gf, shot) {
  tour(A, [x, 0, zOut + 7], [{ goto: [x, zOut + 1.2], note: "approach the entrance" }, { interact: id, aim: [x + 0.3, gf + 1.1, zIn + 0.02], expectOpen: true },
    { goto: [x, zIn - 2.0], expectY: gf, note: "into the lobby" }, { look: [x, gf + 1.4, zIn - 8], hold: 0.5 }, { shot }]);
}

// ============================================================================ newtown helpers
const DEG = 180 / Math.PI;
const lerp3 = (a, b, t) => [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];
const aimY = (d) => { const L = Math.hypot(d[0], d[1], d[2]); return [Math.acos(Math.max(-1, Math.min(1, d[1] / L))) * DEG, Math.atan2(d[0], d[2]) * DEG, 0]; };
// round rod from a to b (catalog_modern_props.mjs)
function rod(A, a, b, dia, m, o = {}) {
  const d = [b[0] - a[0], b[1] - a[1], b[2] - a[2]], L = Math.hypot(...d);
  if (L < 1e-4) return null;
  return A.cyl(lerp3(a, b, 0.5), [dia, L, dia], m, { k: 0, lod: 1, seg: 8, ...o, r: aimY(d) });
}
// rectangular bar from a to b (w across, t thick)
function bar(A, a, b, w, t, m, o = {}) {
  const d = [b[0] - a[0], b[1] - a[1], b[2] - a[2]], L = Math.hypot(...d);
  if (L < 1e-4) return null;
  const yaw = Math.atan2(d[0], d[2]) * DEG, pitch = -Math.asin(Math.max(-1, Math.min(1, d[1] / L))) * DEG;
  return A.obox(lerp3(a, b, 0.5), [w, t, L], [pitch, yaw, 0], m, o);
}

// extra finishes. Painted steel is "metal", flat colour and signs "plain"; the copied kit's remaining "painted" names
// (shutter_paint, crate_olive: the painted-wood shader) are switched to metal so none can slip through.
function nt(A) {
  for (const m of A.materials) if (m.pattern === "painted") Object.assign(m, { pattern: "metal", texture: undefined });
  const add = (n, alb, r, pat, ex = {}) => { if (!A.materials.some((m) => m.name === n)) A.mat(n, alb, r, pat, ex); };
  add("conc_fw", [0.58, 0.57, 0.54], 0.9, "concrete", { texture: "concrete_formwork" });
  add("deck_conc", [0.50, 0.49, 0.46], 0.9, "concrete", { texture: "concrete_stained" });
  add("line_w", [0.82, 0.82, 0.78], 0.6, "plain");
  add("line_y", [0.80, 0.60, 0.06], 0.6, "plain");
  add("sign_red", [0.55, 0.08, 0.06], 0.5, "plain");
  add("sign_blue", [0.06, 0.20, 0.48], 0.5, "plain");
  add("sign_green", [0.06, 0.32, 0.16], 0.5, "plain");
  add("sign_yel", [0.78, 0.58, 0.07], 0.5, "plain");
  add("sign_wht", [0.85, 0.85, 0.82], 0.5, "plain");
  add("awn_red", [0.45, 0.06, 0.05], 0.9, "fabric", { texture: "cloth_grey" });
  add("awn_green", [0.07, 0.26, 0.14], 0.9, "fabric", { texture: "cloth_grey" });
  add("awn_blue", [0.06, 0.16, 0.38], 0.9, "fabric", { texture: "cloth_grey" });
  add("awn_cream", [0.70, 0.64, 0.50], 0.9, "fabric", { texture: "cloth_grey" });
  add("decking", [0.42, 0.30, 0.20], 0.8, "wood", { texture: "planks_038" });
  add("timber", [0.30, 0.20, 0.12], 0.7, "wood", { texture: "wood_dark" });
  add("bench_wood", [0.45, 0.32, 0.20], 0.75, "wood", { texture: "wood_oak" });
  add("patina", [0.22, 0.40, 0.34], 0.6, "metal", { metallic: 0.5 });
  add("clock_face", [0.88, 0.87, 0.82], 0.5, "plain");
  add("hand", [0.03, 0.03, 0.035], 0.4, "metal", { metallic: 0.5 });
  add("steel_red", [0.52, 0.05, 0.04], 0.35, "metal", { metallic: 0.3 });
  add("steel_wht", [0.86, 0.86, 0.84], 0.35, "metal", { metallic: 0.2 });
  add("steel_blk", [0.05, 0.05, 0.055], 0.5, "metal", { metallic: 0.3 });
  add("chrome", [0.78, 0.79, 0.80], 0.15, "metal", { metallic: 0.9 });
  add("car_red", [0.50, 0.04, 0.04], 0.25, "metal", { metallic: 0.4 });
  add("car_silver", [0.60, 0.61, 0.63], 0.25, "metal", { metallic: 0.6 });
  add("car_blue", [0.06, 0.14, 0.34], 0.25, "metal", { metallic: 0.5 });
  add("car_glass", [0.05, 0.07, 0.08], 0.05, "glass");
  add("tyre", [0.035, 0.035, 0.037], 0.92, "plain");
  add("lamp_w", [0.85, 0.86, 0.87], 0.12, "glass");
  add("lamp_r", [0.62, 0.03, 0.03], 0.2, "plain", { emissive: [0.4, 0.01, 0.01] });
  add("lamp_a", [0.92, 0.46, 0.04], 0.2, "plain");
  add("beacon_b", [0.05, 0.15, 0.70], 0.2, "plain", { emissive: [0.1, 0.3, 1.4] });
  add("beacon_r", [0.70, 0.04, 0.03], 0.2, "plain", { emissive: [1.4, 0.08, 0.05] });
  add("stripe_r", [0.62, 0.06, 0.05], 0.5, "plain");
  add("stripe_b", [0.05, 0.18, 0.55], 0.5, "plain");
  add("refl_w", [0.90, 0.90, 0.86], 0.35, "plain");
  add("seat_fab", [0.18, 0.18, 0.20], 0.95, "fabric", { texture: "cloth_grey" });
  add("goods_a", [0.60, 0.25, 0.10], 0.7, "plain");
  add("goods_b", [0.15, 0.35, 0.55], 0.7, "plain");
  add("goods_c", [0.75, 0.70, 0.25], 0.7, "plain");
}

// windows in the rooms between boundaries xs ([a0, a1, a2, ...]): width w, sill s0, head s1, as many as fit at pitch
function wins(xs, w, s0, s1, pitch = Infinity) {
  const out = [];
  for (let i = 0; i + 1 < xs.length; i++) {
    const a = xs[i], b = xs[i + 1], k = Math.max(1, Math.floor((b - a) / pitch));
    if (b - a < w + 0.6) continue;
    for (let j = 0; j < k; j++) { const c = a + ((j + 0.5) * (b - a)) / k; out.push([q4(c - w / 2), q4(c + w / 2), s0, s1]); }
  }
  return out;
}
// short door apron inside the footprint margin (the wing's own apron runs 1.4 m out)
function apron(A, F, c, w) { fbox(A, F, c - w / 2 - 0.3, c + w / 2 + 0.3, -0.2, F.floorY - 0.03, 0, 0.45, "paving", { bv: 0.015 }); }
// modern metal railing: posts, top and bottom rails, glass infill, one invisible collider
function mrail(A, axis, a0, a1, d, y, h = 1.05, post = 1.5) {
  const R = (p0, p1, ya, yb, e0, e1, m, o) => wbox(A, axis, p0, p1, ya, yb, d + e0, d + e1, m, o);
  R(a0, a1, y + h - 0.05, y + h, -0.03, 0.03, "alu_lt", K0);
  R(a0, a1, y + 0.08, y + 0.12, -0.02, 0.02, "alu", L1);
  R(a0 + 0.04, a1 - 0.04, y + 0.12, y + h - 0.05, -0.006, 0.006, "glass_t", K0);
  const n = Math.max(1, Math.round((a1 - a0) / post));
  for (let i = 0; i <= n; i++) { const a = a0 + ((a1 - a0) * i) / n; R(a - 0.025, a + 0.025, y, y + h, -0.025, 0.025, "alu", L1); }
  R(a0, a1, y, y + h, -0.04, 0.04, "alu", { v: 0 });
}
// a wheel on an axle along x (outboard side s); dual: a second tyre inboard
function vwheel(A, x, y, z, D, tw, s, dual = false) {
  const R = [0, 0, 90];
  A.cyl([x, y, z], [D, tw, D], "tyre", { k: 0, seg: 24, r: R });
  if (dual) A.cyl([x - s * tw * 1.05, y, z], [D, tw, D], "tyre", { k: 0, seg: 24, r: R });
  A.cyl([x + s * (tw / 2 + 0.003), y, z], [D * 0.6, 0.02, D * 0.6], "alu_lt", { k: 0, seg: 20, r: R });
  A.cyl([x + s * (tw / 2 + 0.015), y, z], [D * 0.22, 0.03, D * 0.22], "alu", { k: 0, lod: 1, seg: 10, r: R });
  for (let i = 0; i < 6; i++) {
    const a = (i / 6) * 2 * Math.PI;
    A.cyl([x + s * (tw / 2 + 0.02), y + Math.cos(a) * D * 0.15, z + Math.sin(a) * D * 0.15], [0.03, 0.02, 0.03], "chrome", { k: 0, lod: 1, seg: 6, r: R });
  }
}
// cover round a vehicle running along z (catalog_modern_props.mjs vehicleCover)
function vcover(L, W, h) {
  const out = [], ht = h >= 1.35 ? "high" : "low", n = Math.max(1, Math.round((L - 0.6) / 1.6));
  for (let i = 0; i < n; i++) {
    const z = -L / 2 + 0.3 + ((i + 0.5) * (L - 0.6)) / n;
    out.push(cover([W / 2 + 0.55, 0, z], 270, ht, ht === "high" ? ["left", "right"] : ["over"]));
    out.push(cover([-W / 2 - 0.55, 0, z], 90, ht, ht === "high" ? ["left", "right"] : ["over"]));
  }
  out.push(cover([0, 0, L / 2 + 0.55], 180, ht, ["left", "right"]), cover([0, 0, -L / 2 - 0.55], 0, ht, ["left", "right"]));
  return out;
}
function propAsset(id, name, description, footprint, accent) {
  const A = new Asset({ id, version: 1, name, category: "Modern", description, footprint, floors: 0, enterable: false, plinthDepth: 0, maxGrade: 0.5, noAutoPanels: true });
  modMats(A, accent);
  nt(A);
  return A;
}
const frame = (w, s) => ({ ...sideF(w, s), floorY: w.gf });
function wingAprons(A, w, list) { for (const [s, c, wd] of list) apron(A, frame(w, s), c, wd); }

// simple modern display car, origin on its floor at (cx, y0, cz), length along "z" or "x" (front toward +)
function dcar(A, cx, cz, along, paint, y0 = GF) {
  const sw = along === "x";
  const B = (u0, u1, ya, yb, v0, v1, m, o = K0) => (sw ? A.box([cx + v0, y0 + ya, cz + u0], [cx + v1, y0 + yb, cz + u1], m, o) : A.box([cx + u0, y0 + ya, cz + v0], [cx + u1, y0 + yb, cz + v1], m, o));
  const RB = (u0, u1, ya, yb, v0, v1, r, m) => (sw ? A.rbox([cx + v0, y0 + ya, cz + u0], [cx + v1, y0 + yb, cz + u1], r, m, K0) : A.rbox([cx + u0, y0 + ya, cz + v0], [cx + u1, y0 + yb, cz + v1], r, m, K0));
  const PR = (u0, u1, tri, m) => (sw ? A.prism(2, cz + u0, cz + u1, tri.map(([v, y]) => [cx + v, y0 + y]), m, K0) : A.prism(0, cx + u0, cx + u1, tri.map(([v, y]) => [cz + v, y0 + y]), m, K0));
  RB(-0.9, 0.9, 0.3, 0.86, -2.25, 2.25, 0.08, paint);                                   // lower body
  PR(-0.87, 0.87, [[2.2, 0.86], [1.35, 0.86], [2.2, 0.72]], paint);                      // bonnet fall to the nose
  const gh = [[1.35, 0.86], [0.35, 1.40], [-0.95, 1.40], [-1.9, 0.88]];                  // greenhouse
  PR(-0.76, 0.76, [gh[0], gh[1], gh[2]], "car_glass"); PR(-0.76, 0.76, [gh[0], gh[2], gh[3]], "car_glass");
  RB(-0.74, 0.74, 1.37, 1.45, -0.92, 0.32, 0.03, paint);                                  // roof
  for (const s of [-1, 1]) {
    B(s > 0 ? 0.74 : -0.79, s > 0 ? 0.79 : -0.74, 0.86, 1.39, -0.36, -0.24, paint);       // B pillar
    B(s > 0 ? 0.9 : -0.905, s > 0 ? 0.905 : -0.9, 0.36, 0.85, 0.946, 0.954, "panel_dk", L1);   // door seams
    B(s > 0 ? 0.9 : -0.905, s > 0 ? 0.905 : -0.9, 0.36, 0.85, -0.304, -0.296, "panel_dk", L1);
    B(s > 0 ? 0.9 : -1.03, s > 0 ? 1.03 : -0.9, 0.95, 1.06, 1.0, 1.14, paint);           // mirror
    B(s > 0 ? 0.48 : -0.84, s > 0 ? 0.84 : -0.48, 0.62, 0.74, 2.2, 2.262, "lamp_w");      // headlamp
    B(s > 0 ? 0.5 : -0.86, s > 0 ? 0.86 : -0.5, 0.68, 0.8, -2.262, -2.2, "lamp_r");       // tail lamp
    for (const wv of [1.38, -1.38]) {
      const wu = s * 0.8, p = sw ? [cx + wv, y0 + 0.33, cz + wu] : [cx + wu, y0 + 0.33, cz + wv], r = sw ? [90, 0, 0] : [0, 0, 90];
      A.cyl(p, [0.66, 0.22, 0.66], "tyre", { k: 0, seg: 20, r });
      const q = sw ? [p[0], p[1], p[2] + s * 0.112] : [p[0] + s * 0.112, p[1], p[2]];
      A.cyl(q, [0.42, 0.02, 0.42], "alu_lt", { k: 0, seg: 16, r });
    }
  }
  for (const wv of [1.38, -1.38]) B(-0.91, 0.91, 0.26, 0.66, wv - 0.4, wv + 0.4, "rubber", L1);   // arch liners
  B(-0.4, 0.4, 0.45, 0.62, 2.22, 2.27, "panel_dk", L1);                                    // grille
  B(-0.88, 0.88, 0.26, 0.44, 2.18, 2.3, "panel_dk");                                       // bumpers
  B(-0.88, 0.88, 0.26, 0.44, -2.3, -2.18, "panel_dk");
  B(-0.9, 0.9, 0.0, 1.42, -2.3, 2.3, paint, { v: 0 });                                    // collider
}

// ============================================================================ 1. hotel
export function hotel() {
  const n = 5, h = 3.3, ys = Array.from({ length: n + 1 }, (_, f) => GF + f * h), zc = [-1.0, 1.0];
  const A = mk("mod_nt_hotel", "Town hotel", "Five-storey town hotel, 24 x 16 m: a limestone-clad ground floor with a glazed lobby, a flat entrance canopy and a vertical blank sign blade, four rendered upper floors with Juliet balconies and a warm accent band. Inside: the lobby with a fixed reception desk, a dining room across the front, kitchen, office, store and toilets behind; four upper floors of guest rooms either side of a central corridor with a linen room; a stair core rising to a roof door behind a parapet, a lift, roof plant.",
    5, [24, 16], [0.42, 0.24, 0.10]);
  nt(A);
  const k = mkCore({ xa: -11.7, zA: 1.12, ys, encR: true, encTo: 7.7, roof: true, tag: "core" });
  const L = mkLift({ x0: -11.7, x1: -9.3, z0: -7.7, z1: -1.0, door: "front", ys: ys.slice(0, n) });
  const fr = [-8.93, -4.8, -0.7, 3.4, 7.5, 11.7], rr = [-7.0, -2.3, 2.35, 7.0, 11.7];
  const w = wing(A, {
    tag: "h", x0: -12, x1: 12, z0: -8, z1: 8, n, h, cores: [k], lifts: [L], band: "panel_acc", fin: 0.08,
    fac: { wall: "render_w", crest: [{ a: 1.0, w: 7.0, kind: "step", rise: 1.2 }], piers: "render_w",   // TD1
      pil: [{ at: "auto", f0: 1, w: 0.55 }, { side: "east", at: "auto", f0: 1, w: 0.55 }] },   // NT2b
    skin: (f) => (f === 0 ? "stone_clad" : "render_w"),
    doors: [{ id: "main_entrance", side: "front", c: -3.5, w: 2.0, h: 2.5, double: true, glass: true, room: "f0_lobby", apron: false },
      { id: "service_door", side: "rear", c: 10.4, w: 1.0, room: "f0_kitchen", apron: false },
      { id: "west_exit", side: "west", c: 0, w: 1.0, room: "f0_corridor", apron: false }],
    glaze: (f) => (f === 0 ? { front: [[-8.6, -4.8, 0.1, 3.0], [-2.2, 1.2, 0.1, 3.0], [1.9, 11.3, 0.5, 3.0, 3.1]], rear: [[-5.9, -3.0, 0.9, 2.6], [4.4, 8.4, 1.1, 2.4]], east: [[2.4, 6.6, 0.5, 3.0], [-6.4, -2.6, 1.1, 2.4]] }
      : { front: wins(fr, 1.8, 0.4, 2.6), rear: wins(rr, 1.8, 0.9, 2.5), east: [[3.5, 5.3, 0.9, 2.5], [-5.3, -3.5, 0.9, 2.5]] }),
    plan: (f, { w }) => dl(w, { pre: `f${f}_`, zc,
      front: f === 0 ? [{ x1: -8.93, skip: 1 }, { x1: 1.5, id: "lobby", type: "lobby", open: 1 }, { id: "dining", type: "dining", dw: 1.8 }]
        : [{ x1: -8.93, skip: 1 }, ...fr.slice(1).map((x1, i) => ({ x1, id: `room_f${i + 1}`, type: "bedroom_modern" }))],
      rear: f === 0 ? [{ x1: -9.3, skip: 1 }, { x1: -6.6, id: "wc", type: "bathroom_modern" }, { x1: -2.4, id: "office", type: "office_modern" }, { x1: 1.2, id: "store", type: "utility" }, { id: "kitchen", type: "prokitchen" }]
        : [{ x1: -9.3, skip: 1 }, { x1: -7.0, id: "linen", type: "utility" }, ...rr.slice(1).map((x1, i) => ({ x1, id: `room_r${i + 1}`, type: "bedroom_modern" }))] }),
    roof: (A, w) => { hvac(A, 3, -4.5, w.top); hvac(A, 7, -4.5, w.top); plantScreen(A, 1.2, 8.8, -6.0, -3.0, w.top, 1.6); tank(A, 9.5, 4.0, w.top, 2.0, 2.0); },
    dress: (A, w) => {
      const F = sideF(w, "front");
      // Juliet balconies on the guest-room windows
      for (let f = 1; f < n; f++) for (const [a0, a1] of wins(fr, 1.8, 0.4, 2.6)) {
        const y = GF + f * h;
        fbox(A, F, a0 - 0.05, a1 + 0.05, y + 0.1, y + 1.05, 0.06, 0.075, "glass_t", K0);
        fbox(A, F, a0 - 0.08, a1 + 0.08, y + 1.02, y + 1.08, 0.04, 0.1, "alu_lt", L1);
      }
      canopy(A, { x0: -6.2, x1: -0.8, z0: 8, z1: 8.45, y: 3.35, cols: [], fascia: "sign_blank" });
      fbox(A, F, 10.75, 10.95, 6.0, 14.5, 0, 0.45, "panel_acc", { k: 0, bv: 0.01 });      // sign blade
      fbox(A, F, 10.72, 10.98, 6.4, 14.1, 0.1, 0.42, "sign_blank", L1);
      for (const s of SIDES) { const G = sideF(w, s), isX = G.axis === "x"; fbox(A, G, isX ? -12.03 : -7.995, isX ? 12.03 : 7.995, -0.3, GF + 0.6, -0.01, 0.04, "precast", { k: 0, bv: 0.01 }); }   // base course
      // reception desk against the stair enclosure, a timber feature panel behind it
      A.box([-8.2, GF, 2.0], [-7.3, GF + 1.05, 5.8], "laminate", { bv: 0.01 });
      A.box([-8.25, GF + 1.05, 1.95], [-7.25, GF + 1.1, 5.85], "panel_acc", K0);
      A.box([-7.3, GF + 0.9, 2.1], [-7.05, GF + 0.95, 5.7], "laminate", L1);
      A.box([-8.92, GF + 0.1, 1.4], [-8.88, GF + 3.0, 7.4], "laminate", L1);
      A.box([-8.0, GF + 1.1, 3.0], [-7.6, GF + 1.45, 3.04], "screen", L1);
      // kitchen line (fixed): steel counters and an extract hood along the rear wall
      A.box([1.6, GF, -7.7], [9.6, GF + 0.9, -7.05], "lift_steel", { bv: 0.01 });
      A.box([4.0, GF + 2.0, -7.7], [7.6, GF + 2.5, -6.8], "lift_steel", L1);
      A.box([5.4, GF + 2.5, -7.6], [6.2, w.yAt(1) - SLAB - 0.02, -7.2], "lift_steel", L1);
    },
  });
  wingAprons(A, w, [["front", -3.5, 2.0], ["rear", 10.4, 1.0], ["west", 0, 1.0]]);
  buildCore(A, k, 0, ys.slice(0, n).map((_, f) => `f${f}_corridor`));
  buildLift(A, L, w.top);
  addRoom(A, "roof", "utility", w.ix0, w.top, w.iz0, w.ix1, w.top + 2.5, w.iz1);
  navDoor(A, `f${n - 1}_corridor`, "roof", [k.xa + k.len / 2, w.top, k.zF]);
  coverRing(A, -12, 12, -8, 8);
  doorTour(A, "main_entrance", -3.5, 8, 7.7, GF, "mod_nt_hotel_lobby");
  return finish(A);
}

// ============================================================================ 2. parking garage
export function parkingGarage() {
  const A = mk("mod_nt_parking_garage", "Parking garage", "Open three-deck concrete parking garage, 40 x 30 m: a ground level and two decks under an open roof deck (four parking levels), linked by a straight stacked ramp bay along the east side between full-height ramp walls, two stair cores in the west corners rising to roof doors, square columns with yellow guard paint, downstand beams, low precast parapets with coping and open bands above, painted bays, wheel stops, direction arrows, ceiling lights and roof lamp posts. The street entrance has two entry lanes and an exit lane with barrier arms, a pay station and a height bar.",
    4, [40, 30], [0.08, 0.30, 0.55]);
  nt(A);
  const X = 20, Z = 15, ys = [0.15, 3.15, 6.15, 9.15], SX0 = 13.0, SX1 = 19.6, RZ = 9, SC = (SX0 + SX1) / 2, ST = 0.3, PH = 1.05;
  const kS = mkCore({ xa: -19.5, zA: -11.0, dir: -1, ys, encL: true, encR: true, roof: true, tag: "stair_s" });
  const kN = mkCore({ xa: -19.5, zA: 11.0, dir: 1, ys, encL: true, encR: true, roof: true, tag: "stair_n" });
  // ground slab and decks (holes: the ramp bay and the stair wells)
  A.box([-X, -0.3, -Z], [X, ys[0], Z], "asphalt");
  for (let L = 1; L < 4; L++) {
    const y = ys[L], holes = [{ a0: 12.7, a1: X, y0: -RZ, y1: RZ }, ...[kS, kN].filter((k) => k.cuts(y)).map((k) => k.hole)];
    for (const [a, b, c, d] of cutRect(-X, X, -Z, Z, holes)) A.box([a, y - ST, c], [b, y, d], L === 3 ? "deck_conc" : "concrete");
  }
  // ramp bay walls (full height: the ramps run between them)
  for (const [x0, x1] of [[12.7, SX0], [SX1, X]]) {
    A.box([x0, 0, -RZ], [x1, ys[3] + PH, RZ], "conc_fw");
    A.box([x0 - 0.03, ys[3] + PH, -RZ - 0.03], [x1 + 0.03, ys[3] + PH + 0.07, RZ + 0.03], "precast", { k: 0, bv: 0.01 });
  }
  for (let z = -RZ + 1.5; z < RZ; z += 1.5) A.box([X, 0.2, z - 0.02], [X + 0.012, ys[3] + PH, z + 0.02], "concrete", L1);   // panel joints on the east face
  A.box([X, 4.0, -3.0], [X + 0.06, 8.0, 3.0], "sign_blue", K0);                          // blank wayfinding panel
  A.box([X + 0.02, 4.3, -2.7], [X + 0.07, 7.7, 2.7], "sign_wht", L1);
  // ramps: a solid wedge from the ground, sloped slabs above; each rises 3 m over 18 m (9.5 deg), stacked in the bay
  const th = Math.atan2(3, 2 * RZ), deg = th * DEG, Lr = Math.hypot(2 * RZ, 3), W = SX1 - SX0, cs = Math.cos(th), sn = Math.sin(th);
  A.wedge([SC, ys[0] + 1.5, 0], [W, 3.0, 2 * RZ], "concrete", { r: [0, 180, 0] });
  for (let i = 1; i < 3; i++) A.obox([SC, ys[i] + 1.5 - cs * ST / 2, sn * ST / 2], [W, ST, Lr], [-deg, 0, 0], "concrete");
  for (let i = 0; i < 3; i++) {
    for (const x of [SX0 + 0.25, SX1 - 0.25]) A.obox([x, ys[i] + 1.5 + cs * 0.004, -sn * 0.004], [0.1, 0.008, Lr - 0.2], [-deg, 0, 0], "line_y", L1);
    for (let j = -2; j <= 2; j++) {   // centre dashes
      const zz = j * 3.4, yy = ys[i] + 1.5 + zz * (3 / (2 * RZ));
      A.obox([SC, yy + cs * 0.004 + 0.002, zz - sn * 0.004], [0.12, 0.008, 1.6], [-deg, 0, 0], "line_w", L1);
    }
  }
  A.box([SX0, ys[3], -RZ - 0.25], [SX1, ys[3] + PH, -RZ], "precast");                    // guard where the roof meets the ramp void
  // parapets: precast panels with 0.12 m joints at the column lines, coping; gaps for the entrance and footpaths
  const para = (y, axis, a0, a1, d0, d1, gaps) => {
    const brk = axis === "x" ? [-12, -4.5, 3, 10.5, 16.3] : [-5, 5];
    const holes = gaps.map(([g0, g1]) => ({ a0: g0, a1: g1, y0: -1, y1: 2 })).concat(brk.map((b) => ({ a0: b - 0.06, a1: b + 0.06, y0: -1, y1: 2 })));
    for (const [p0, p1] of cutRect(a0, a1, 0, 1, holes).map((r) => [r[0], r[1]])) {
      wbox(A, axis, p0, p1, y, y + PH, d0, d1, "precast", { bv: 0.01 });
      wbox(A, axis, p0, p1, y + PH, y + PH + 0.06, d0 - 0.02, d1 + 0.02, "concrete_wash", { k: 0, bv: 0.01 });
    }
  };
  for (let L = 0; L < 4; L++) {
    const y = ys[L], g = L === 0;
    para(y, "x", -X, X, Z - 0.25, Z, g ? [[-1.0, 9.0]] : []);
    para(y, "x", -X, X, -Z, -Z + 0.25, g ? [[-8.0, -6.5]] : []);
    para(y, "z", -Z + 0.25, Z - 0.25, -X, -X + 0.2, g ? [[-0.8, 0.8]] : []);
    para(y, "z", RZ, Z - 0.25, X - 0.25, X, []);
    para(y, "z", -Z + 0.25, -RZ, X - 0.25, X, []);
  }
  // NT2b: precast piers on the column lines up the long faces and at the corners, rising past the top parapet
  for (const s of [-1, 1]) for (const x of [-X + 0.3, -12, -4.5, 3, 10.5, X - 0.3]) {
    const g = s > 0 ? [[-1.0, 9.0]] : [[-8.0, -6.5]], y0 = g.some(([a, b]) => x > a - 0.4 && x < b + 0.4) ? ys[1] - ST - 0.45 : 0;
    A.box([x - 0.3, y0, Math.min(s * Z, s * (Z + 0.22))], [x + 0.3, ys[3] + PH + 0.55, Math.max(s * Z, s * (Z + 0.22))], "precast", { k: 0, bv: 0.02 });
    A.box([x - 0.36, ys[3] + PH + 0.55, Math.min(s * (Z - 0.05), s * (Z + 0.28))], [x + 0.36, ys[3] + PH + 0.65, Math.max(s * (Z - 0.05), s * (Z + 0.28))], "concrete_wash", { k: 0, bv: 0.01 });
  }
  for (const z of [-Z + 0.3, -5, 5, Z - 0.3]) {
    A.box([-X - 0.22, z === 0 ? 0 : 0, z - 0.3], [-X, ys[3] + PH + 0.55, z + 0.3], "precast", { k: 0, bv: 0.02 });
    A.box([-X - 0.28, ys[3] + PH + 0.55, z - 0.36], [-X + 0.05, ys[3] + PH + 0.65, z + 0.36], "concrete_wash", { k: 0, bv: 0.01 });
  }
  // columns (guard paint at the foot), downstand beams, ceiling lights
  const colX = [-12, -4.5, 3, 10.5], cols = [];
  for (const x of colX) for (const z of [-14.5, -5, 5, 14.5]) cols.push([x, z]);
  cols.push([-19.5, -5], [-19.5, 5], [16.3, 14.5], [16.3, -14.5]);
  for (let L = 0; L < 3; L++) {
    const y = ys[L], yc = ys[L + 1] - ST;
    for (const [x, z] of cols) {
      A.box([x - 0.25, y, z - 0.25], [x + 0.25, yc, z + 0.25], "conc_fw", { bv: 0.02 });
      A.box([x - 0.262, y, z - 0.262], [x + 0.262, y + 0.9, z + 0.262], "line_y", L1);
    }
    for (const x of colX) A.box([x - 0.2, yc - 0.45, -Z + 0.25], [x + 0.2, yc, Z - 0.25], "concrete", L1);
    for (const z of [-7.2, 7.2]) for (let x = -15; x <= 10; x += 5) A.box([x - 0.6, yc - 0.05, z - 0.08], [x + 0.6, yc - 0.01, z + 0.08], "light_panel", L1);
    for (const z of [-5, 5]) A.box([SX0 + 0.3, yc - 0.04, z - 0.08], [SX1 - 0.3, yc, z + 0.08], "light_panel", L1);   // ramp bay strips
  }
  // painted bays, centre line, wheel stops, arrows, level signs
  for (let L = 0; L < 4; L++) {
    const y = ys[L], Ln = (x0, x1, z0, z1, m = "line_w") => A.box([x0, y, z0], [x1, y + 0.006, z1], m, L1);
    for (let i = 0; i <= 12; i++) { const x = -19.5 + i * 2.5; Ln(x - 0.05, x + 0.05, -4.8, 4.8); }
    Ln(-19.5, 10.5, -0.05, 0.05);
    for (let i = 2; i <= 12; i++) { const x = -19.5 + i * 2.5; Ln(x - 0.05, x + 0.05, 9.6, 14.6); Ln(x - 0.05, x + 0.05, -14.6, -9.6); }
    for (let i = 2; i < 12; i++) {
      const x = -19.5 + (i + 0.5) * 2.5;
      for (const z of [13.9, -13.9]) A.box([x - 0.8, y, z - 0.08], [x + 0.8, y + 0.12, z + 0.08], "precast", { k: 0, lod: 1, bv: 0.02 });
    }
    for (const [x, z, s] of [[-8, 7.2, 1], [4, 7.2, 1], [-8, -7.2, -1], [4, -7.2, -1]]) {   // aisle arrows (east on the north aisle, west on the south)
      Ln(x - 1.2 * s, x + 0.4 * s, z - 0.12, z + 0.12);
      A.prism(1, y, y + 0.006, [[x + 0.4 * s, z - 0.45], [x + 0.4 * s, z + 0.45], [x + 1.2 * s, z]], "line_w", K0);
    }
    A.box([12.66, y + 1.6, -2], [12.7, y + 2.6, 2], ["sign_green", "sign_yel", "sign_red", "sign_blue"][L], L1);   // level colour panel
  }
  // entrance: islands, barrier arms (striped), pay station, height bar
  const isl = [[2.0, 2.6], [5.4, 6.0]];
  for (const [x0, x1] of isl) A.box([x0, ys[0], 11.6], [x1, ys[0] + 0.15, 14.0], "precast", { bv: 0.03 });
  A.box([8.6, ys[0], 12.8], [9.0, ys[0] + 0.15, 14.0], "precast", { bv: 0.03 });
  const arm = (px, pz, x1) => {
    A.box([px - 0.15, ys[0] + 0.15, pz - 0.15], [px + 0.15, ys[0] + 1.15, pz + 0.15], "line_y", { bv: 0.02 });
    A.box([px - 0.16, ys[0] + 1.15, pz - 0.16], [px + 0.16, ys[0] + 1.2, pz + 0.16], "steel_blk", L1);
    const s = Math.sign(x1 - px), L = Math.abs(x1 - px) - 0.15, nseg = Math.round(L / 0.5);
    for (let i = 0; i < nseg; i++) { const a = px + s * (0.15 + (i * L) / nseg), b = px + s * (0.15 + ((i + 1) * L) / nseg); A.box([Math.min(a, b), ys[0] + 0.98, pz - 0.04], [Math.max(a, b), ys[0] + 1.06, pz + 0.04], i % 2 ? "stripe_r" : "refl_w", K0); }
  };
  arm(2.3, 13.6, -0.9); arm(5.7, 13.6, 2.7); arm(8.8, 13.6, 6.1);
  for (const [x0, x1] of isl) {   // ticket / pay stations
    A.box([x0 + 0.05, ys[0] + 0.15, 12.0], [x1 - 0.05, ys[0] + 1.55, 12.5], "panel_acc", { bv: 0.02 });
    A.box([x0 + 0.08, ys[0] + 1.0, 12.5], [x1 - 0.08, ys[0] + 1.3, 12.52], "screen", L1);
  }
  for (const x of [-1.2, 9.2]) A.cyl([x, ys[0] + 1.3, 14.85], [0.14, 2.6, 0.14], "steel_blk", { seg: 10 });
  for (let i = 0; i < 10; i++) A.box([-1.2 + i * 1.04, ys[0] + 2.2, 14.8], [-1.2 + (i + 1) * 1.04, ys[0] + 2.32, 14.9], i % 2 ? "steel_blk" : "line_y", K0);
  // roof lamp posts
  for (const [x, z] of [[-10, 7.2], [4, 7.2], [-10, -7.2], [4, -7.2]]) {
    A.cyl([x, ys[3] + 0.1, z], [0.5, 0.2, 0.5], "concrete_wash", { seg: 12 });
    A.cyl([x, ys[3] + 3.1, z], [0.14, 6.0, 0.14], "steel_blk", { seg: 10 });
    A.box([x - 0.5, ys[3] + 6.0, z - 0.15], [x + 0.5, ys[3] + 6.15, z + 0.15], "steel_blk", L1);
    A.box([x - 0.45, ys[3] + 5.98, z - 0.12], [x + 0.45, ys[3] + 6.0, z + 0.12], "light_panel", L1);
  }
  for (const k of [kS, kN]) {
    buildCore(A, k, 0, ["d0", "d1", "d2"], "conc_fw");
    navDoor(A, "d2", "d3", [k.xa + k.len / 2, ys[3], k.zF]);
  }
  // nav: a room per level, the two ramp-bay ends, a room per ramp linking its foot and head
  for (let L = 0; L < 4; L++) {
    const y = ys[L], t = y + (L < 3 ? 2.6 : 2.5);
    addRoom(A, `d${L}`, "utility", -19.8, y, -14.75, 12.7, t, 14.75);
    addRoom(A, `d${L}_n`, "utility", SX0, y, RZ, SX1, t, 14.75);
    addRoom(A, `d${L}_s`, "utility", SX0, y, -14.75, SX1, t, -RZ);
    navDoor(A, `d${L}`, `d${L}_n`, [12.85, y, 12]); navDoor(A, `d${L}`, `d${L}_s`, [12.85, y, -12]);
    for (const x of [-14, -8, -2, 4, 10]) {
      if (L === 0 && x > -2 && x < 10) continue;
      A._nav.cover.push(cover([x, y, 14.25], 0, "low", ["over"]), cover([x, y, -14.25], 180, "low", ["over"]));
      if (L > 0) A._nav.firingPoints.push(fire([x, y, 14.35], 0, "window"), fire([x, y, -14.35], 180, "window"));
    }
    for (const z of [-7.5, 2.5]) A._nav.cover.push(cover([-19.2, y, z], 270, "low", ["over"]));
    for (const [x, z] of [[-4.5, -5], [3, 5]]) A._nav.cover.push(cover([x + 0.6, y, z], 270, "high", ["left", "right"]));
  }
  for (let i = 0; i < 3; i++) {
    addRoom(A, `ramp_${i}`, "utility", SX0, ys[i], -RZ, SX1, ys[i + 1] + 2.5, RZ);
    navDoor(A, `ramp_${i}`, `d${i}_s`, [SC, ys[i], -RZ]); navDoor(A, `ramp_${i}`, `d${i + 1}_n`, [SC, ys[i + 1], RZ]);
  }
  for (const [p, dir, e] of [[[4, ys[0], Z], [0, 0, -1], [4, 0, Z + 1]], [[-X, ys[0], 0], [1, 0, 0], [-X - 1, 0, 0]], [[-7.25, ys[0], -Z], [0, 0, 1], [-7.25, 0, -Z - 1]]]) {
    navDoor(A, "d0", "outside", p); A._nav.entrances.push({ p: e, dir, room: "d0" });
  }
  coverRing(A, -X, X, -Z, Z);
  tour(A, [4, 0, 22], [{ goto: [4, 13], note: "in past the barriers" }, { goto: [16.3, -12], expectY: ys[0], note: "to the ramp foot" }, { goto: [16.3, 12], expectY: ys[1], note: "up the first ramp" }, { look: [0, ys[1] + 1.4, 0], hold: 0.5 }, { shot: "mod_nt_parking_garage_deck1" }]);
  return finish(A);
}

// ============================================================================ 3. clock tower
function clockFace(A, side, Xo, y) {
  const C = { front: [0, y, Xo], rear: [0, y, -Xo], east: [Xo, y, 0], west: [-Xo, y, 0] }[side];
  const N = { front: [0, 0, 1], rear: [0, 0, -1], east: [1, 0, 0], west: [-1, 0, 0] }[side];
  const U = { front: [1, 0, 0], rear: [-1, 0, 0], east: [0, 0, -1], west: [0, 0, 1] }[side];
  const pt = (u, v, e) => [C[0] + U[0] * u + N[0] * e, C[1] + v, C[2] + U[2] * u + N[2] * e];
  const r = N[2] ? [90, 0, 0] : [0, 0, 90];
  A.cyl(pt(0, 0, 0.06), [3.0, 0.12, 3.0], "stone_dressed", { k: 0, seg: 32, r });
  A.cyl(pt(0, 0, 0.14), [2.7, 0.04, 2.7], "bronze", { k: 0, seg: 32, r });
  A.cyl(pt(0, 0, 0.165), [2.5, 0.03, 2.5], "clock_face", { k: 0, seg: 32, r });
  for (let i = 0; i < 12; i++) {   // hour marks (no numerals)
    const a = (i / 12) * 2 * Math.PI, s = i % 3 ? 0.08 : 0.13, c = pt(Math.sin(a) * 1.06, Math.cos(a) * 1.06, 0.19), hs = [N[2] ? s / 2 : 0.012, s / 2, N[2] ? 0.012 : s / 2];
    A.box([c[0] - hs[0], c[1] - hs[1], c[2] - hs[2]], [c[0] + hs[0], c[1] + hs[1], c[2] + hs[2]], "hand", L1);
  }
  bar(A, pt(0, 0, 0.2), pt(Math.sin(-1.05) * 0.7, Math.cos(-1.05) * 0.7, 0.2), 0.07, 0.07, "hand", K0);   // hour hand at ten
  bar(A, pt(0, 0, 0.23), pt(Math.sin(1.05) * 1.0, Math.cos(1.05) * 1.0, 0.23), 0.045, 0.045, "hand", K0);   // minute hand at two
  A.cyl(pt(0, 0, 0.25), [0.14, 0.04, 0.14], "bronze", { k: 0, lod: 1, seg: 12, r });
}
export function clockTower() {
  const A = mk("mod_nt_clock_tower", "Clock tower", "Civic clock tower landmark, 8 x 8 m and about 25 m tall: a battered limestone plinth and a 7.2 m square brick shaft with stone quoins and string courses, a timber double door, slit windows, four blank clock faces (bronze bezels, hour marks, hands; no numerals) round the clock room, an open belfry with twin round-arched openings on each side above a parapet, a hung bell, a stone cornice and a patinated copper pyramid spire with a finial. Inside: a switchback stair through the ground hall, two storage levels and the clock room (clock mechanism) up to the belfry lookout.",
    5, [8, 8], [0.20, 0.14, 0.09]);
  nt(A);
  Object.assign(A.materials.find((m) => m.name === "door_paint"), { albedo: [0.20, 0.12, 0.07], roughness: 0.6, pattern: "wood", texture: "wood_dark", metallic: 0 });
  const h = 3.6, n = 4, ys = [0, 1, 2, 3, 4].map((f) => GF + f * h), Xo = 3.6, T = 0.45;
  const k = mkCore({ xa: -3.15, zA: -1.1, dir: 1, ys, encR: true, encTo: 3.15, tag: "core" });
  const w = wing(A, {
    tag: "t", x0: -Xo, x1: Xo, z0: -Xo, z1: Xo, n, h, T, cores: [k], band: "stone_dressed", par: 0, skin: () => "brick", fin: 0.05, finMat: "stone_dressed",
    doors: [{ id: "tower_door", side: "front", c: 1.35, w: 1.3, h: 2.5, double: true, room: "l0", apron: false }],
    glaze: (f) => (f === 0 ? { east: [[-0.35, 0.35, 1.2, 2.7]], rear: [[1.0, 1.7, 1.2, 2.7]] } : f < 3 ? { front: [[1.0, 1.7, 1.0, 2.7]], east: [[-0.35, 0.35, 1.0, 2.7]], rear: [[1.0, 1.7, 1.0, 2.7]] } : {}),
    plan: (f) => ({ rooms: [{ id: `l${f}`, type: f === 0 ? "hall" : "utility", x0: -0.38, z0: -3.15, x1: 3.15, z1: 3.15 }, { id: `l${f}_ld`, type: "hall", x0: -3.15, z0: -3.15, x1: -0.38, z1: -1.1 }],
      walls: [], doors: [{ a: `l${f}_ld`, b: `l${f}`, p: [-0.38, -2.1] }] }),
  });
  for (const p of A.parts) if (p.m === "roof_mem") p.m = "precast";   // the top slab is the belfry floor
  // plinth, step, quoins
  for (const [x0, x1, z0, z1] of [[-4, 4, -4, -Xo], [Xo, 4, -Xo, Xo], [-4, -Xo, -Xo, Xo], [-4, 0.55, Xo, 4], [2.15, 4, Xo, 4]]) A.box([x0, -0.3, z0], [x1, 0.9, z1], "stone_clad", { bv: 0.03 });
  A.box([0.55, -0.3, Xo], [2.15, GF - 0.04, 4.0], "precast", { bv: 0.015 });
  for (const s of SIDES) {
    const F = sideF(w, s), isX = F.axis === "x", [a0, a1] = isX ? [-Xo, Xo] : [-Xo, Xo];
    for (const a of [a0 + 0.3, a1 - 0.3]) fbox(A, F, a - 0.3, a + 0.3, 0.9, w.top, -0.005, 0.14, "stone_clad", { k: 0, bv: 0.015 });   // NT2b: 14 cm corner pilasters (were 4 cm strips)
    fbox(A, F, isX ? a0 - 0.12 : a0 + 0.005, isX ? a1 + 0.12 : a1 - 0.005, w.top - 0.25, w.top + 0.05, -0.005, 0.12, "stone_dressed", { k: 0, bv: 0.015 });
  }
  tdCornice(A, { x0: -Xo, x1: Xo, z0: -Xo, z1: Xo }, ys[3] - 0.32, SIDES, { m: "stone_dressed", prof: [[0.12, 0.17], [0.14, 0.24], [0.16, 0.36]] });   // NT2b: the clock stage
  // belfry: parapet, corner and centre piers, twin round arches, lintel band, cornice, roof slab, spire
  const yb = w.top, yS = yb + 1.05, yA = yb + 2.9, yC = yb + 3.6, yR = yb + 4.2;
  for (const s of SIDES) {
    const F = sideF(w, s), isX = F.axis === "x", [a0, a1] = isX ? [-Xo, Xo] : [-Xo + T, Xo - T], pw = isX ? 0.9 : 0.45, [d0, d1] = zr(F.inner, F.outer);
    const B = (p0, p1, y0, y1, m, o) => wbox(A, F.axis, p0, p1, y0, y1, d0, d1, m, o);
    B(a0, a1, yb, yS, "stone_clad");
    wbox(A, F.axis, a0 - (isX ? 0.06 : 0), a1 + (isX ? 0.06 : 0), yS, yS + 0.08, d0 - 0.06, d1 + 0.06, "stone_dressed", { bv: 0.01 });
    for (const [p0, p1] of [[a0, a0 + pw], [-0.3, 0.3], [a1 - pw, a1]]) B(p0, p1, yS + 0.08, yR, "brick");
    for (const [p0, p1] of [[a0 + pw, -0.3], [0.3, a1 - pw]]) {
      B(p0, p1, yC, yR, "brick");
      const c = (p0 + p1) / 2, dm = (d0 + d1) / 2;
      A.arch(isX ? [c, (yA + yC) / 2, dm] : [dm, (yA + yC) / 2, c], [p1 - p0, yC - yA, T], "stone_dressed", { fill: 1, th: 0.18, r: isX ? [0, 0, 0] : [0, 90, 0] });
      if (s !== "west") {
        const ins = F.inner - F.out * 0.5;
        A._nav.firingPoints.push(fire(isX ? [c, yb, ins] : [ins, yb, c], F.yaw, "window"));
      }
    }
    fbox(A, F, isX ? a0 - 0.25 : a0 - T + 0.005, isX ? a1 + 0.25 : a1 + T - 0.005, yR - 0.2, yR, -0.005, 0.25, "stone_dressed", { k: 0, bv: 0.015 });
  }
  A.box([-Xo - 0.25, yR, -Xo - 0.25], [Xo + 0.25, yR + 0.35, Xo + 0.25], "stone_dressed", { bv: 0.02 });
  A.box([-Xo + 0.45, yR - 0.02, -Xo + 0.45], [Xo - 0.45, yR, Xo - 0.45], "timber", K0);
  A.cone([0, yR + 0.35 + 2.4, 0], [6.4, 4.8, 6.4], "patina", { n: 4 });
  rod(A, [0, yR + 4.9, 0], [0, yR + 6.1, 0], 0.07, "bronze", { seg: 8 });
  A.cyl([0, yR + 5.3, 0], [0.22, 0.22, 0.22], "bronze", { k: 0, seg: 12 });
  bar(A, [-0.45, yR + 5.85, 0], [0.45, yR + 5.85, 0], 0.03, 0.03, "bronze", K0);           // vane arm
  A.prism(2, -0.01, 0.01, [[0.45, yR + 5.75], [0.45, yR + 5.95], [0.62, yR + 5.85]], "bronze", K0);
  // bell on a timber headstock between two posts
  for (const z of [-1.5, 1.5]) A.box([1.3, yb, z - 0.1], [1.5, yb + 3.45, z + 0.1], "timber", { bv: 0.01 });
  A.box([1.25, yb + 3.3, -1.6], [1.55, yb + 3.5, 1.6], "timber", { k: 0, bv: 0.01 });
  A.cyl([1.4, yb + 3.2, 0], [0.3, 0.2, 0.3], "bronze", { k: 0, seg: 12 });
  A.cone([1.4, yb + 2.6, 0], [1.15, 1.0, 1.15], "bronze", { k: 0, top: 0.5, seg: 24 });
  A.cyl([1.4, yb + 2.12, 0], [1.22, 0.06, 1.22], "bronze", { k: 0, seg: 24 });
  rod(A, [1.4, yb + 3.0, 0], [1.4, yb + 2.15, 0], 0.05, "iron");
  // clock faces and the mechanism in the clock room
  for (const s of SIDES) clockFace(A, s, Xo, ys[3] + 1.8);
  const yk = ys[3];
  A.box([1.0, yk, -0.5], [2.2, yk + 1.25, 0.5], "steel_blk", { bv: 0.02 });
  for (const z of [-0.2, 0.25]) A.cyl([1.6, yk + 0.8, z], [0.6, 0.04, 0.6], "bronze", { k: 0, lod: 1, seg: 16, r: [90, 0, 0] });
  for (const p of [[0, yk + 1.8, Xo - T], [0, yk + 1.8, -Xo + T], [Xo - T, yk + 1.8, 0]]) rod(A, [1.6, yk + 1.25, 0], p, 0.04, "iron");
  addRoom(A, "bel", "utility", -0.38, yb, -3.15, 3.15, yb + 2.6, 3.15);
  addRoom(A, "bel_ld", "hall", -3.15, yb, -3.15, -0.38, yb + 2.6, -1.1);
  navDoor(A, "bel_ld", "bel", [-0.38, yb, -2.1]);
  buildCore(A, k, yb + 1.1, ["l0_ld", "l1_ld", "l2_ld", "l3_ld", "bel_ld"]);
  coverRing(A, -4, 4, -4, 4);
  doorTour(A, "tower_door", 1.35, 4, 3.15, GF, "mod_nt_clock_tower_hall");
  return finish(A);
}

// ============================================================================ 4. corner shop
function gondola(A, x, z0, z1, y, cap = "panel_acc") {
  A.box([x - 0.5, y, z0], [x + 0.5, y + 0.12, z1], "panel_dk");
  A.box([x - 0.04, y + 0.12, z0], [x + 0.04, y + 1.6, z1], "panel_lt");
  for (const sy of [0.45, 0.85, 1.25]) {
    A.box([x - 0.45, y + sy, z0 + 0.05], [x + 0.45, y + sy + 0.03, z1 - 0.05], "panel_lt", L1);
    for (const s of [-1, 1]) A.box([s > 0 ? x + 0.1 : x - 0.4, y + sy + 0.03, z0 + 0.15], [s > 0 ? x + 0.4 : x - 0.1, y + sy + 0.25, z1 - 0.15], ["goods_a", "goods_b", "goods_c"][Math.round(sy * 3) % 3], L1);
  }
  for (const z of [z0, z1]) A.box([x - 0.5, y, Math.min(z, z + (z === z0 ? -0.35 : 0.35))], [x + 0.5, y + 1.4, Math.max(z, z + (z === z0 ? -0.35 : 0.35))], cap, { bv: 0.01 });
}
function wallShelf(A, axis, a0, a1, d, s, y, h = 2.0) {   // shelving run against a wall at depth d, fronts facing s
  wbox(A, axis, a0, a1, y, y + h, d, d + s * 0.45, "panel_lt", { v: 0 });
  wbox(A, axis, a0, a1, y, y + h, d, d + s * 0.03, "panel_lt", K0);
  for (const sy of [0.1, 0.55, 1.0, 1.45, 1.9]) {
    wbox(A, axis, a0, a1, y + sy, y + sy + 0.03, d, d + s * 0.45, "panel_lt", L1);
    if (sy < 1.8) wbox(A, axis, a0 + 0.05, a1 - 0.05, y + sy + 0.03, y + sy + 0.3, d + s * 0.05, d + s * 0.38, ["goods_b", "goods_c", "goods_a"][Math.round(sy * 2) % 3], L1);
  }
  for (let a = a0; a <= a1 + 1e-6; a += (a1 - a0) / Math.max(1, Math.round((a1 - a0) / 1.0))) wbox(A, axis, a - 0.02, a + 0.02, y, y + h, d, d + s * 0.45, "alu", L1);
}
export function shopSmall() {
  const A = mk("mod_nt_shop_small", "Corner convenience store", "Single-storey corner convenience store, 10 x 12 m: grey brick with full-height shopfront glazing on the street front and round the corner, a glazed double door, a green blank fascia sign band on both street faces and a projecting blank disc sign. Inside: the shop with three fixed shelving gondolas, a wall shelving run, a glass-door chiller line, a counter with till and a back gantry; a back store room with racking and a rear door.",
    1, [10, 12], [0.06, 0.36, 0.20]);
  nt(A);
  const w = wing(A, {
    tag: "s", x0: -5, x1: 5, z0: -6, z1: 6, n: 1, h: 3.8, band: null, par: 0.9, skin: () => "brick_grey", fin: 0.05,
    fac: { crest: [{ a: -1.6, w: 3.6, kind: "step", rise: 0.9 }], piers: "brick_grey", quoin: "td_cast_stone", roof: ["ac", "vent"] },   // TD1
    doors: [{ id: "shop_door", side: "front", c: 2.6, w: 1.8, h: 2.4, double: true, glass: true, room: "shop", apron: false },
      { id: "back_door", side: "rear", c: 3.0, w: 1.0, room: "store", apron: false }],
    glaze: () => ({ front: [[-4.4, 1.4, 0.4, 3.0, 1.95]], east: [[1.6, 5.4, 0.4, 3.0, 1.9]], rear: [[-3.4, -2.2, 1.4, 2.4]] }),
    plan: () => ({ rooms: [{ id: "shop", type: "shop", x0: -4.7, z0: -1.94, x1: 4.7, z1: 5.7 }, { id: "store", type: "utility", x0: -4.7, z0: -5.7, x1: 4.7, z1: -2.06 }],
      walls: [{ axis: "x", d: -2.0, a0: -4.7, a1: 4.7, holes: [{ c: -3.6, w: 1.0, a: "shop", b: "store" }] }] }),
    roof: (A, w) => { hvac(A, -1.5, -3.5, w.top, 2.0, 1.2, 1.0); hvac(A, 2.0, -3.5, w.top, 1.6, 1.2, 0.9); },
    dress: (A, w) => {
      const y = GF;
      for (const x of [-2.9, -0.9, 1.1]) gondola(A, x, -0.3, 2.9, y);
      wallShelf(A, "z", -1.5, 5.2, -4.7, 1, y);
      // chiller line on the partition: carcass, glass doors, lit header
      A.box([-4.5, y, -1.94], [0.9, y + 2.0, -1.25], "panel_lt", { bv: 0.01 });
      for (let x = -4.4; x < 0.85; x += 0.9) { A.box([x, y + 0.15, -1.25], [x + 0.86, y + 1.9, -1.23], "glass_int", K0); A.box([x + 0.78, y + 0.8, -1.23], [x + 0.8, y + 1.3, -1.19], "alu_lt", L1); }
      A.box([-4.5, y + 1.95, -1.24], [0.9, y + 2.1, -1.2], "light_panel", L1);
      // counter, till, back gantry
      A.box([3.0, y, 0.4], [3.6, y + 1.0, 3.0], "laminate", { bv: 0.01 });
      A.box([2.95, y + 1.0, 0.35], [3.65, y + 1.04, 3.05], "panel_acc", K0);
      A.box([3.1, y + 1.04, 1.0], [3.5, y + 1.18, 1.4], "panel_dk", L1);
      A.box([3.15, y + 1.18, 1.1], [3.18, y + 1.45, 1.35], "screen", L1);
      wallShelf(A, "z", -1.7, 0.4, 4.7, -1, y + 0.9, 1.4);
      A.box([4.25, y, -1.7], [4.7, y + 0.9, 0.4], "laminate", { bv: 0.01 });
      // store racking
      wallShelf(A, "x", -4.6, -2.4, -5.7, 1, y, 2.2); wallShelf(A, "x", -1.6, 2.3, -5.7, 1, y, 2.2);
      wallShelf(A, "z", -5.0, -2.6, 4.7, -1, y, 2.2);
      // fascia sign band, projecting disc sign, base course
      const F = sideF(w, "front"), E = sideF(w, "east");
      fbox(A, F, -4.95, 4.95, 3.45, 4.15, 0, 0.12, "sign_green", { k: 0, bv: 0.01 });
      fbox(A, F, -4.6, 4.6, 3.55, 4.05, 0.12, 0.14, "sign_wht", L1);
      fbox(A, E, 1.0, 5.95, 3.45, 4.15, 0, 0.12, "sign_green", { k: 0, bv: 0.01 });
      fbox(A, E, 1.3, 5.6, 3.55, 4.05, 0.12, 0.14, "sign_wht", L1);
      A.box([5.0, 3.0, -1.0], [5.45, 3.06, -0.96], "alu", L1);
      A.cyl([5.3, 2.75, -0.98], [0.5, 0.05, 0.5], "sign_red", { k: 0, seg: 24, r: [0, 0, 90] });
      for (const s of SIDES) { const G = sideF(w, s), isX = G.axis === "x"; fbox(A, G, isX ? -5.03 : -5.995, isX ? 5.03 : 5.995, -0.3, 0.7, -0.01, 0.04, "precast", { k: 0, bv: 0.01 }); }
      // NT7 (convenience-store references): lettered fascias, shutter boxes over the glazing, posters and price cards on
      // the glass, a CCTV camera, an alarm box and a meter cabinet; abandoned: one shutter part down, one shut and tagged
      ntMats(A);
      const Wf = sideF(w, "west");
      ntText(A, F, -0.2, 3.8, "MINI MARKET", 0.36, 0.14, "nt_letter_dk");
      ntText(A, E, 3.45, 3.8, "OPEN 7 DAYS", 0.3, 0.14, "nt_letter_dk");
      ntShutter(A, F, -4.4, 1.4, 0.4, 3.0, 0);
      ntShutter(A, E, 1.6, 5.4, 0.4, 3.0, 0);
      ntPosters(A, F, -4.4, 1.4, 0.45, 2.9, 3);
      ntPosters(A, E, 1.6, 5.4, 0.45, 2.9, 7);
      ntCctv(A, F, 4.45, 3.15);
      ntAlarm(A, E, -1.2, 3.0);
      ntMeter(A, Wf, 4.2, 0.55);
      ruinOnly(A, () => {
        ntShutter(A, F, -4.4, 1.4, 0.4, 3.0, 0.7, { box: false });
        ntShutter(A, E, 1.6, 5.4, 0.4, 3.0, 1.0, { box: false });
        ntText(A, E, 3.5, 1.6, "KEEP OUT", 0.32, 0.075, "nt_spray");
        ntText(A, F, -2.9, 2.35, "ZEPH", 0.5, 0.075, "nt_poster_b");   // tags on the shutters
        ntText(A, F, -0.6, 1.9, "KRS", 0.42, 0.075, "nt_poster_r");
        ntText(A, E, 2.6, 2.4, "DOZE", 0.4, 0.075, "nt_poster_y");
      });
      // TD1: a sound awning over the street glazing, a bent and torn one round the corner
      tdAwning(A, F, -4.5, 1.5, 3.38, { dep: 1.3, drop: 0.5, m: "awn_green" });
      tdAwning(A, E, 1.5, 5.5, 3.38, { dep: 1.2, drop: 0.5, m: "awn_green", sag: 0.55, torn: [1], hang: [2] });
    },
  });
  wingAprons(A, w, [["front", 2.6, 1.8], ["rear", 3.0, 1.0]]);
  coverRing(A, -5, 5, -6, 6);
  doorTour(A, "shop_door", 2.6, 6, 5.7, GF, "mod_nt_shop_small_floor");
  return finish(A);
}

// ============================================================================ 5. shop row with flats above
export function shopRow() {
  const h = 3.4, ys = [GF, GF + h];
  const A = mk("mod_nt_shop_row", "Shop row with flats", "Two-storey row of three small shops, 24 x 12 m: dark panel shopfronts with glazed doors under fabric awnings (red, blue, green) and blank fascia signs in matching colours, brick flats above. Each unit: a shop at the front and a store room behind it; a rear door to a lobby and a stair up to a flat with a sitting room across the front, a bedroom and a bathroom off a short hall.",
    2, [24, 12], [0.30, 0.18, 0.10]);
  nt(A);
  const units = [[-11.7, -4.1], [-3.9, 3.9], [4.1, 11.7]], cores = units.map(([a], i) => mkCore({ xa: a, zA: -4.3, dir: 1, ys, encL: false, encR: true, tag: `u${i}_core` }));
  const col = [["sign_red", "awn_red"], ["sign_blue", "awn_blue"], ["sign_green", "awn_green"]];
  const doors = [], g0 = { front: [], rear: [], east: [], west: [] }, g1 = { front: [], rear: [], east: [], west: [] };
  units.forEach(([a, b], i) => {
    doors.push({ id: `u${i}_shop_door`, side: "front", c: b - 1.3, w: 1.1, h: 2.3, glass: true, room: `u${i}_shop`, apron: false },
      { id: `u${i}_flat_door`, side: "rear", c: a + 1.4, w: 1.0, room: `u${i}_lobby`, apron: false },
      { id: `u${i}_store_door`, side: "rear", c: a + 5.6, w: 1.0, room: `u${i}_store`, apron: false });
    g0.front.push([a + 0.25, b - 2.1, 0.45, 2.75, 2.6]); g0.rear.push([a + 6.5, b - 0.35, 1.4, 2.3]);
    g1.front.push([a + 0.6, a + 3.4, 0.9, 2.5], [a + 4.4, b - 0.6, 0.9, 2.5]); g1.rear.push([a + 0.8, a + 2.0, 1.2, 2.4], [a + 4.6, b - 0.6, 1.0, 2.4]);
  });
  g0.west.push([0.8, 3.8, 0.45, 2.75]); g0.east.push([0.8, 3.8, 0.45, 2.75]); g1.west.push([1.0, 4.0, 0.9, 2.5]); g1.east.push([1.0, 4.0, 0.9, 2.5]);
  const w = wing(A, {
    tag: "r", x0: -12, x1: 12, z0: -6, z1: 4.8, n: 2, h, cores, band: null, par: 0.9, fin: 0.06,
    fac: { wall: "brick", crest: [{ a: -7.9, w: 4.2, kind: "pediment", rise: 1.0 }, { a: 0, w: 3.0, kind: "block", rise: 0.6 }, { a: 7.9, w: 4.6, kind: "step", rise: 1.1 }], piers: "brick", courses: [1], roof: ["tank", "ac", "vent"] },   // TD1
    skin: (f) => (f === 0 ? "panel_dk" : "brick"), doors, glaze: (f) => (f === 0 ? g0 : g1),
    plan: (f) => {
      const rooms = [], walls = [], doors = [];
      for (const d of [-4, 4]) walls.push({ axis: "z", d, a0: -5.7, a1: 4.5, t: 0.2, holes: [] });
      units.forEach(([a, b], i) => {
        const p = `u${i}_`;
        if (f === 0) {
          rooms.push({ id: p + "shop", type: "shop", x0: a, z0: -0.24, x1: b, z1: 4.5 }, { id: p + "store", type: "utility", x0: a + 2.89, z0: -5.7, x1: b, z1: -0.36 }, { id: p + "lobby", type: "hall", x0: a, z0: -5.7, x1: a + 2.77, z1: -4.3 });
          walls.push({ axis: "z", d: a + 2.83, a0: -5.7, a1: -4.3, holes: [] }, { axis: "x", d: -0.3, a0: a + 2.77, a1: b, holes: [{ c: a + 5.6, w: 1.0, a: p + "store", b: p + "shop" }] });
        } else {
          rooms.push({ id: p + "sitting", type: "sitting_modern", x0: a, z0: -0.24, x1: b, z1: 4.5 }, { id: p + "landing", type: "hall", x0: a, z0: -5.7, x1: a + 3.94, z1: -4.3 },
            { id: p + "passage", type: "hall", x0: a + 2.77, z0: -4.3, x1: a + 3.94, z1: -0.36 }, { id: p + "bed", type: "bedroom_modern", x0: a + 4.06, z0: -5.7, x1: b, z1: -3.06 },
            { id: p + "bath", type: "bathroom_modern", x0: a + 4.06, z0: -2.94, x1: b, z1: -0.36 });
          walls.push({ axis: "x", d: -0.3, a0: a + 2.77, a1: b, holes: [{ c: a + 3.36, w: 0.9, a: p + "passage", b: p + "sitting" }] },
            { axis: "z", d: a + 4.0, a0: -5.7, a1: -0.36, holes: [{ c: -4.95, w: 0.85, a: p + "landing", b: p + "bed" }, { c: -1.6, w: 0.8, a: p + "passage", b: p + "bath" }] },
            { axis: "x", d: -3.0, a0: a + 4.06, a1: b, holes: [] });
          doors.push({ a: p + "landing", b: p + "passage", p: [a + 3.36, -4.3] });
        }
      });
      return { rooms, walls, doors };
    },
    roof: (A, w) => { for (const [a, b] of units) hvac(A, (a + b) / 2 + 1.0, -3.4, w.top, 1.8, 1.1, 0.9); },
    dress: (A, w) => {
      const F = sideF(w, "front");
      units.forEach(([a, b], i) => {
        const [sg, aw] = col[i], x0 = a + 0.1, x1 = b - 0.1;
        fbox(A, F, x0, x1, 3.27, 3.66, 0, 0.1, sg, { k: 0, bv: 0.01 });                    // blank fascia sign
        fbox(A, F, x0 + 0.25, x1 - 0.25, 3.33, 3.6, 0.1, 0.115, "sign_wht", L1);
        // awning: sloped fabric, valance, side cheeks, arms
        if (i === 0) {   // TD1: this one's frame is bent and half its cloth is gone
          tdAwning(A, F, a + 0.15, b - 0.15, 3.2, { dep: 1.15, drop: 0.4, m: aw, sag: 0.45, torn: [3], hang: [1] });
          fbox(A, F, a + 0.3, b - 0.3, -0.3, GF + 0.42, -0.005, 0.06, "precast", { k: 0, bv: 0.01 });   // stall riser
          return;
        }
        const th = Math.atan2(0.4, 1.15), L = Math.hypot(1.15, 0.4), xc = (a + b) / 2, wd = b - a - 0.3;
        A.obox([xc, 3.0, 4.8 + 0.575], [wd, 0.03, L], [th * DEG, 0, 0], aw, { k: 0, bv: 0.005 });
        A.box([xc - wd / 2, 2.55, 5.93], [xc + wd / 2, 2.82, 5.96], aw, K0);
        for (const s of [-1, 1]) {
          const xe = xc + s * wd / 2;
          A.prism(0, xe - 0.01, xe + 0.01, [[4.8, 3.2], [5.95, 2.8], [4.8, 2.8]], aw, K0);
          rod(A, [xe - s * 0.1, 2.5, 4.8], [xe - s * 0.1, 2.83, 5.9], 0.03, "alu");
        }
        A.box([xc - wd / 2, 3.18, 4.8], [xc + wd / 2, 3.24, 4.88], "alu", L1);                 // roller box
        fbox(A, F, a + 0.3, b - 0.3, -0.3, GF + 0.42, -0.005, 0.06, "precast", { k: 0, bv: 0.01 });   // stall riser
      });
      fbox(A, F, -12.04, 12.04, w.top - 0.12, w.top + 0.02, -0.005, 0.06, "precast", { k: 0, bv: 0.01 });
      for (const s of ["east", "west"]) fbox(A, sideF(w, s), -5.995, 4.795, -0.3, GF + 0.42, -0.005, 0.05, "precast", { k: 0, bv: 0.01 });
      A.box([-12, -0.2, 4.8], [12, 0.12, 6.0], "paving", { bv: 0.01 });                     // footway under the awnings
      for (const x of [-11.8, -4.0, 4.0, 11.8]) tdPilaster(A, F, x, 0.13, w.top - 0.14, "brick", { w: 0.3, p: 0.07 });   // TD1: brick pilasters between the shops
    },
  });
  for (const d of doors) apron(A, frame(w, d.side), d.c, d.w);
  cores.forEach((k, i) => buildCore(A, k, w.top - SLAB, [`u${i}_lobby`, `u${i}_landing`]));
  coverRing(A, -12, 12, -6, 4.8);
  doorTour(A, "u1_shop_door", 2.6, 4.8, 4.5, GF, "mod_nt_shop_row_unit");
  return finish(A);
}

// ============================================================================ 6. cafe
export function cafe() {
  const A = mk("mod_nt_cafe", "Cafe with terrace", "One-storey cafe, 12 x 10 m: a rendered box with tall glazing, a glazed double door onto a timber-decked front terrace under a steel-framed canopy with a blank fascia sign, glass windbreak screens and planters. Inside: the cafe dining room with a fixed service counter (display case, coffee machine, pass hatch), a small kitchen with a fixed cooking line and hood, a store and a toilet.",
    1, [12, 10], [0.45, 0.28, 0.12]);
  nt(A);
  const w = wing(A, {
    tag: "c", x0: -6, x1: 6, z0: -5, z1: 2.5, n: 1, h: 3.6, band: null, par: 0.8, skin: () => "render_w", fin: 0.06,
    fac: { crest: [{ a: 3.2, w: 3.2, kind: "block", rise: 0.55 }], piers: "render_w", roof: ["ac", "vent"] },   // TD1
    doors: [{ id: "cafe_door", side: "front", c: -2.0, w: 1.8, h: 2.4, double: true, glass: true, room: "dining", to: "terrace" },
      { id: "kitchen_door", side: "rear", c: 4.0, w: 1.0, room: "kitchen", apron: false }],
    glaze: () => ({ front: [[-5.4, -3.2, 0.1, 3.0], [-0.8, 5.4, 0.1, 3.0, 2.1]], west: [[-1.4, 1.8, 0.9, 2.8]], east: [[-1.4, 1.8, 0.9, 2.8]], rear: [[2.0, 3.1, 1.3, 2.3], [-5.0, -4.2, 1.6, 2.3]] }),
    plan: () => ({
      rooms: [{ id: "dining", type: "dining", x0: -5.7, z0: -1.84, x1: 5.7, z1: 2.2 }, { id: "wc", type: "bathroom_modern", x0: -5.7, z0: -4.7, x1: -3.06, z1: -1.96 },
        { id: "store", type: "utility", x0: -2.94, z0: -4.7, x1: 1.24, z1: -1.96 }, { id: "kitchen", type: "kitchen_modern", x0: 1.36, z0: -4.7, x1: 5.7, z1: -1.96 }],
      walls: [{ axis: "x", d: -1.9, a0: -5.7, a1: 5.7, holes: [{ c: -4.4, w: 0.8, a: "dining", b: "wc" }, { c: -0.9, w: 0.9, a: "dining", b: "store" }, { c: 1.9, w: 0.9, a: "dining", b: "kitchen" }, { c: 4.0, w: 1.6, y0: 1.0, h: 2.0 }] },
        { axis: "z", d: -3.0, a0: -4.7, a1: -1.96, holes: [] }, { axis: "z", d: 1.3, a0: -4.7, a1: -1.96, holes: [] }],
    }),
    roof: (A, w) => hvac(A, 3.5, -3.0, w.top, 1.8, 1.2, 0.9),
    dress: (A, w) => {
      const y = GF;
      // service counter with display case, coffee machine and the pass hatch behind
      A.box([2.6, y, -1.84], [5.5, y + 1.0, -1.25], "laminate", { bv: 0.01 });
      A.box([2.55, y + 1.0, -1.89], [5.55, y + 1.04, -1.2], "precast", K0);
      A.box([2.7, y + 1.04, -1.7], [3.9, y + 1.4, -1.3], "glass_int", K0);
      A.box([2.7, y + 1.04, -1.7], [3.9, y + 1.08, -1.3], "goods_c", L1);
      A.box([4.4, y + 1.04, -1.8], [5.0, y + 1.5, -1.45], "lift_steel", { k: 0, bv: 0.02 });
      A.box([2.6, y + 2.1, -1.84], [5.5, y + 2.7, -1.8], "screen", L1);                     // blank menu board
      // kitchen line along the east wall
      A.box([5.1, y, -4.6], [5.7, y + 0.9, -2.1], "lift_steel", { bv: 0.01 });
      A.box([5.15, y + 0.9, -4.0], [5.6, y + 0.93, -3.0], "panel_dk", L1);
      A.box([4.9, y + 2.0, -4.3], [5.7, y + 2.45, -2.9], "lift_steel", L1);
      // terrace: deck, canopy on posts, screens, planters, heaters
      A.box([-6, -0.3, 2.5], [6, GF, 5.0], "decking", { bv: 0.01 });
      for (let x = -5.8; x < 6; x += 0.15) A.box([x, GF - 0.004, 2.5], [x + 0.006, GF + 0.001, 5.0], "timber", L1);
      for (const x of [-5.75, -2.0, 2.0, 5.75]) A.box([x - 0.06, GF, 4.69], [x + 0.06, 3.2, 4.81], "steel_blk", { bv: 0.01 });
      A.box([-6, 3.2, 2.5], [6, 3.32, 5.0], "panel_lt", { bv: 0.01 });
      A.box([-6.02, 3.12, 4.94], [6.02, 3.5, 5.0], "panel_acc", { k: 0, bv: 0.01 });
      A.box([-2.5, 3.18, 4.99], [2.5, 3.44, 5.02], "sign_blank", L1);
      for (const x of [-4, 0, 4]) A.box([x - 0.6, 3.17, 3.6], [x + 0.6, 3.2, 3.8], "light_panel", L1);
      for (const [a0, a1] of [[-5.8, -0.9], [0.9, 5.8]]) mrail(A, "x", a0, a1, 4.88, GF, 1.15, 1.6);
      for (const s of [-1, 1]) mrail(A, "z", 2.6, 4.8, s * 5.92, GF, 1.15, 1.1);
      for (const s of [-1, 1]) { A.box([s * 5.5 - 0.35, GF, 2.55], [s * 5.5 + 0.35, GF + 0.6, 3.25], "precast", { bv: 0.02 }); A.box([s * 5.5 - 0.3, GF + 0.6, 2.6], [s * 5.5 + 0.3, GF + 0.62, 3.2], "soil", K0); A.cone([s * 5.5, GF + 1.1, 2.9], [0.6, 1.0, 0.6], "leaf", { k: 0, top: 0.3, n: 8 }); }
      for (const x of [-3.8, 3.8]) A.box([x - 0.5, 3.0, 2.6], [x + 0.5, 3.12, 2.75], "lift_steel", L1);
      for (const s of SIDES) { const G = sideF(w, s), isX = G.axis === "x"; fbox(A, G, isX ? -6.03 : -4.995, isX ? 6.03 : 2.495, -0.3, 0.6, -0.01, 0.04, "precast", { k: 0, bv: 0.01 }); }
      // NT7 (cafe references): the name on the canopy fascia, an A-frame menu board at the terrace gap, bulkhead lamps
      // either side of the door, an alarm box and a meter cabinet; abandoned: the side windows boarded and sprayed
      ntMats(A);
      const Fc = { axis: "x", d: 5.02, out: 1 }, Ff = sideF(w, "front"), Fe = sideF(w, "east"), Fw = sideF(w, "west");
      ntText(A, Fc, 0, 3.31, "CAFE", 0.2, 0.0, "nt_letter");
      for (const [sx, rx] of [[-1, 14], [1, -14]]) A.box([-0.85, GF, 5.38 + sx * 0.13], [-0.35, GF + 0.95, 5.42 + sx * 0.13], "nt_letter_dk", { k: 0, r: [rx, 0, 0], bv: 0.01 });
      A.box([-0.88, GF + 0.92, 5.36], [-0.32, GF + 0.98, 5.44], "timber", { k: 0, lod: 1 });
      for (const a of [-4.3, 0.2]) ntLamp(A, Ff, a, 2.75, 0.0);
      ntAlarm(A, Fe, -3.6, 3.0);
      ntMeter(A, Fw, -3.8, 0.55);
      ruinOnly(A, () => {
        ntBoard(A, Fe, -1.4, 1.8, 0.9, 2.8, { mark: "CLOSED" });
        ntBoard(A, Fw, -1.4, 1.8, 0.9, 2.8, { mark: "07" });
      });
    },
  });
  wingAprons(A, w, [["rear", 4.0, 1.0]]);
  addRoom(A, "terrace", "dining", -5.85, GF, 2.5, 5.85, 3.1, 4.8);
  navDoor(A, "terrace", "outside", [0, GF, 4.9]);
  A._nav.entrances.push({ p: [0, 0, 5.8], dir: [0, 0, -1], room: "terrace" });
  A._nav.cover.push(cover([-3, 0, 5.6], 180, "low", ["over"]), cover([3, 0, 5.6], 180, "low", ["over"]));
  coverRing(A, -6, 6, -5, 2.5);
  tour(A, [0, 0, 9], [{ goto: [0, 4.0], note: "onto the terrace" }, { interact: "cafe_door", aim: [-1.7, GF + 1.1, 2.22], expectOpen: true }, { goto: [-2, 0.5], expectY: GF, note: "into the cafe" }, { look: [3, GF + 1.2, -1.5], hold: 0.5 }, { shot: "mod_nt_cafe_counter" }]);
  return finish(A);
}

// ============================================================================ 7. car dealership
export function carDealership() {
  const A = mk("mod_nt_car_dealership", "Car dealership", "Car dealership, 30 x 20 m: a 6 m tall showroom glazed on three sides under a white metal-panel fascia with a blank sign, three display cars inside (one on a turntable), a sales desk and a polished tile floor; behind it a single-storey office wing (corridor, three offices, toilet) and a ribbed-metal service workshop with two roller doors at the back, a roller door through to the showroom, a two-post vehicle lift, workbench and tool chests, a tyre rack, compressor and bay markings.",
    1, [30, 20], [0.06, 0.20, 0.42]);
  nt(A);
  const s = wing(A, {
    tag: "s", x0: -15, x1: 15, z0: 0, z1: 10, n: 1, h: 6.0, band: null, par: 1.0, fm: "terracotta", skin: () => "panel_lt", fin: 0.1, finMat: "alu_lt",
    fac: { m: "td_cast_dark", prof: [[0.14, 0.08], [0.2, 0.24]], crest: [{ a: -2.0, w: 6.0, kind: "block", rise: 1.4 }], piers: "panel_lt", roof: ["ac", "ac", "vent"] },   // NT2b
    doors: [{ id: "showroom_door", side: "front", c: -2.0, w: 2.2, h: 2.6, double: true, glass: true, room: "showroom", apron: false },
      { id: "showroom_roller", side: "rear", c: 6, w: 3.6, h: 4.0, roller: true, room: "showroom", to: "bay" }],
    links: [{ side: "rear", c: -9, w: 1.6, h: 2.4, a: "showroom", b: "o_corridor" }],
    glaze: () => ({ front: [[-14.4, -3.5, 0.1, 5.6, 3.6], [-0.5, 14.4, 0.1, 5.6, 3.7], [-3.1, -0.9, 3.05, 5.6]], east: [[0.8, 9.2, 0.1, 5.6, 4.2]], west: [[0.8, 9.2, 0.1, 5.6, 4.2]] }),
    plan: () => ({ rooms: [{ id: "showroom", type: "lobby", x0: -14.7, z0: 0.3, x1: 14.7, z1: 9.7 }], walls: [] }),
    roof: (A, w) => { hvac(A, -8, 4, w.top, 2.6, 1.4); hvac(A, 8, 4, w.top, 2.6, 1.4); },
    dress: (A, w) => {
      const y = GF, F = sideF(w, "front");
      dcar(A, -10, 5.0, "z", "car_red");
      A.cyl([3.5, y + 0.02, 5.2], [5.0, 0.04, 5.0], "alu_lt", { seg: 32 });
      A.cyl([3.5, y + 0.045, 5.2], [4.9, 0.006, 4.9], "panel_dk", { k: 0, seg: 32 });
      dcar(A, 3.5, 5.2, "x", "car_silver", y + 0.04);
      dcar(A, 10.5, 5.0, "z", "car_blue");
      A.box([-13.5, y, 1.0], [-10.5, y + 1.05, 1.8], "laminate", { bv: 0.01 });             // sales desk
      A.box([-13.55, y + 1.05, 0.95], [-10.45, y + 1.1, 1.85], "panel_acc", K0);
      A.box([-12.6, y + 1.1, 1.3], [-12.2, y + 1.45, 1.33], "screen", L1);
      A.box([-14.7, y + 0.1, 0.32], [-11.0, y + 4.5, 0.36], "laminate", L1);                 // timber feature wall
      fbox(A, F, -9, 9, w.top + 0.15, w.top + 0.9, 0, 0.06, "sign_blank", L1);
      fbox(A, F, -15.03, 15.03, w.top - 0.35, w.top - 0.05, -0.005, 0.08, "panel_acc", { k: 0, bv: 0.01 });
      for (let x = -12; x <= 12; x += 6) A.box([x - 0.12, w.top - SLAB - 0.5, 0.3], [x + 0.12, w.top - SLAB, 9.7], "panel_lt", L1);   // ceiling beams
      for (const x of [-12, -6, 0, 6, 12]) for (const z of [3, 7]) A.cyl([x + 3, w.top - SLAB - 0.3, z], [0.35, 0.6, 0.35], "alu", { k: 0, lod: 1, seg: 12 });
      for (const x of [-12, -6, 0, 6, 12]) for (const z of [3, 7]) A.cyl([x + 3, w.top - SLAB - 0.61, z], [0.3, 0.02, 0.3], "light_panel", { k: 0, lod: 1, seg: 12 });
    },
  });
  wing(A, {
    tag: "o", x0: -15, x1: -3, z0: -10, z1: 0, omit: ["front", "east"], n: 1, h: 3.6, band: null, par: 0.8, skin: () => "panel_dk",
    doors: [{ id: "staff_door", side: "west", c: -1.0, w: 1.0, room: "o_corridor", apron: false }],
    glaze: () => ({ rear: [[-14.0, -11.8, 0.9, 2.6], [-10.3, -7.9, 0.9, 2.6], [-6.9, -5.9, 0.9, 2.6], [-4.6, -3.8, 1.5, 2.3]], west: [[-8.0, -4.0, 0.9, 2.6]] }),
    plan: (f, { w }) => dl(w, { pre: "o_", zc: [-2.0, 0], rear: [{ x1: -11, id: "office_1", type: "office_modern" }, { x1: -7.2, id: "office_2", type: "office_modern" }, { x1: -5.4, id: "office_3", type: "office_modern" }, { id: "wc", type: "bathroom_modern" }] }),
  });
  const wk = wing(A, {
    tag: "w", x0: -3, x1: 15, z0: -10, z1: 0, omit: ["front"], n: 1, h: 5.0, band: null, par: 0.8, fm: "concrete_wash", skin: () => "corrugated",
    fac: { cornice: false, piers: "precast", roof: ["vent", "vent", "ac"] },   // NT2b
    doors: [{ id: "service_door_1", side: "rear", c: 2.5, w: 3.6, h: 4.2, roller: true, room: "bay", apron: false },
      { id: "service_door_2", side: "rear", c: 9.5, w: 3.6, h: 4.2, roller: true, room: "bay", apron: false },
      { id: "workshop_door", side: "rear", c: 13.5, w: 1.0, room: "bay", apron: false },
      { id: "office_link", side: "west", c: -1.0, w: 1.0, room: "bay", to: "o_corridor" }],
    glaze: () => ({ east: [[-8.5, -1.5, 2.6, 4.4, 3.5]] }),
    plan: () => ({ rooms: [{ id: "bay", type: "workshop", x0: -2.7, z0: -9.7, x1: 14.7, z1: 0 }], walls: [] }),
    dress: (A, w) => {
      const y = GF;
      // two-post lift in line with the first roller door
      for (const x of [0.6, 4.4]) {
        A.box([x - 0.25, y, -5.8], [x + 0.25, y + 3.4, -5.2], "panel_acc", { bv: 0.02 });
        A.box([x - 0.35, y, -6.0], [x + 0.35, y + 0.02, -5.0], "steel_plate", L1);
        for (const dz of [-1.0, 1.0]) bar(A, [x, y + 0.5, -5.5], [x + (x < 2.5 ? 0.9 : -0.9), y + 0.5, -5.5 + dz], 0.1, 0.08, "steel_blk", K0);
      }
      A.box([0.35, y + 3.4, -5.7], [4.65, y + 3.55, -5.3], "panel_acc", L1);
      // workbench, tool chests, tyre rack, compressor, oil drums
      A.box([14.0, y, -9.0], [14.7, y + 0.9, -4.0], "steel_blk", { bv: 0.01 });
      A.box([13.95, y + 0.9, -9.05], [14.7, y + 0.95, -3.95], "laminate", K0);
      A.box([14.66, y + 1.2, -8.8], [14.7, y + 2.2, -4.2], "panel_lt", L1);
      for (const z of [-3.6, -2.8]) { A.box([14.0, y, z - 0.35], [14.6, y + 1.1, z + 0.35], "steel_red", { bv: 0.02 }); for (let k = 0; k < 4; k++) A.box([13.98, y + 0.15 + k * 0.24, z - 0.3], [14.0, y + 0.19 + k * 0.24, z + 0.3], "alu_lt", L1); }
      for (const yy of [0.5, 1.4]) A.box([-2.6, y + yy, -9.6], [2.0, y + yy + 0.05, -8.9], "steel_blk", L1);
      for (let i = 0; i < 6; i++) for (const yy of [0.85, 1.75]) A.cyl([-2.2 + i * 0.75, y + yy, -9.25], [0.66, 0.22, 0.66], "tyre", { k: 0, lod: 1, seg: 16, r: [0, 0, 90] });
      A.box([-2.6, y, -9.65], [2.0, y + 2.0, -8.85], "steel_blk", { v: 0 });
      A.cyl([13.8, y + 0.55, -1.0], [0.6, 1.1, 0.6], "steel_red", { seg: 16 });
      for (const x of [12.4, 13.0]) A.cyl([x, y + 0.45, -0.6], [0.58, 0.9, 0.58], "panel_acc", { seg: 16 });
      // bay markings, drain, trusses, extract hoses
      for (const [x0, x1] of [[-0.6, 5.6], [6.4, 12.6]]) for (const [a, b, c, d] of [[x0, x0 + 0.1, -9.7, -1.0], [x1 - 0.1, x1, -9.7, -1.0]]) A.box([a, y, c], [b, y + 0.006, d], "line_y", L1);
      A.box([-2.4, y - 0.01, -7.8], [14.4, y + 0.003, -7.6], "steel_plate", L1);
      trusses(A, -2.7, 14.7, -9.7, 0, w.top - SLAB - 0.02, 4.5);
      for (const x of [2.5, 9.5]) rod(A, [x, w.top - SLAB - 0.7, -4.0], [x, y + 1.2, -4.0], 0.12, "rubber");
    },
  });
  coverRing(A, -15, 15, -10, 10);
  void wk;
  wingAprons(A, s, [["front", -2.0, 2.2]]);
  doorTour(A, "showroom_door", -2.0, 10, 9.7, GF, "mod_nt_car_dealership_showroom");
  return finish(A);
}

// ============================================================================ 8. motel
export function motel() {
  const h = 3.2, top = GF + 2 * h, yU = GF + h;
  const A = mk("mod_nt_motel", "Motel", "Two-storey motel, 36 x 12 m: a 33 m block of rooms in render and brick, every room with its door and window facing the front onto an exterior walkway (paved below, a concrete deck with glass-and-aluminium railings above on steel posts, a canopy over), an open precast stair at each end with glass guards up to the upper walkway, a parking strip with bay lines and wheel stops in front. The reception office is at the west end (glazed front, counter, back office). Fifteen guest rooms, each a bedroom with a bathroom behind; through-wall air-conditioning grilles under the windows.",
    2, [36, 12], [0.10, 0.32, 0.38]);
  nt(A);
  const b = [-16.2, -12.0, ...Array.from({ length: 7 }, (_, i) => q4(-12 + ((i + 1) * 28.2) / 7))];
  const bay = (i) => [b[i] + (i > 0 ? 0.075 : 0), b[i + 1] - (i < 7 ? 0.075 : 0)];
  const doors = [], G = [{ front: [], rear: [] }, { front: [], rear: [] }];
  for (let f = 0; f < 2; f++) for (let i = 0; i < 8; i++) {
    const [a0, a1] = bay(i), p = f ? "u_" : "g_", walk = f ? "walk_u" : "walk_g";
    if (f === 0 && i === 0) { doors.push({ id: "office_door", side: "front", c: -13.2, w: 1.2, h: 2.3, glass: true, room: "g_lobby", to: walk }); G[0].front.push([-16.0, -14.1, 0.1, 2.8]); continue; }
    doors.push({ id: `${p}r${i}_door`, side: "front", f, c: a0 + 0.8, w: 0.95, h: 2.15, room: `${p}r${i}`, to: walk });
    G[f].front.push([a0 + 1.6, a1 - 0.35, 1.0, 2.3]); G[f].rear.push([a0 + 0.5, a0 + 1.3, 1.5, 2.2]);
  }
  G[0].rear.push([-15.6, -13.4, 0.9, 2.4]);
  const w = wing(A, {
    tag: "m", x0: -16.5, x1: 16.5, z0: -6, z1: 2.4, n: 2, h, band: "panel_acc", par: 0.9, fin: 0.05, skin: (f) => (f === 0 ? "brick_grey" : "render_w"),
    fac: { wall: "render_w", m: "td_cast_dark", prof: [[0.12, 0.06], [0.16, 0.18]], crest: [{ a: 0, w: 5.0, kind: "block", rise: 0.8 }], piers: "render_w", roof: ["tank", "ac", "ac"] },   // TD1
    doors, glaze: (f) => G[f],
    plan: (f) => {
      const rooms = [], walls = [], dd = [], p = f ? "u_" : "g_";
      for (let i = 1; i < 8; i++) walls.push({ axis: "z", d: b[i], a0: -5.7, a1: 2.1, t: 0.15, holes: [] });
      for (let i = 0; i < 8; i++) {
        const [a0, a1] = bay(i);
        if (f === 0 && i === 0) {
          rooms.push({ id: "g_lobby", type: "lobby", x0: a0, z0: -1.44, x1: a1, z1: 2.1 }, { id: "g_office", type: "office_modern", x0: a0, z0: -5.7, x1: a1, z1: -1.56 });
          walls.push({ axis: "x", d: -1.5, a0, a1, holes: [{ c: -13.0, w: 0.9, a: "g_lobby", b: "g_office" }] });
          continue;
        }
        rooms.push({ id: `${p}r${i}`, type: "bedroom_modern", x0: a0, z0: -3.24, x1: a1, z1: 2.1 }, { id: `${p}b${i}`, type: "bathroom_modern", x0: a0, z0: -5.7, x1: a1, z1: -3.36 });
        walls.push({ axis: "x", d: -3.3, a0, a1, holes: [{ c: a0 + 0.75, w: 0.8, a: `${p}r${i}`, b: `${p}b${i}` }] });
      }
      return { rooms, walls, doors: dd };
    },
    roof: (A, w) => { for (const x of [-10, 0, 10]) hvac(A, x, -3.5, w.top, 2.0, 1.2, 1.0); },
    dress: (A, w) => {
      const F = sideF(w, "front");
      // reception counter
      A.box([-16.1, GF, -1.0], [-13.9, GF + 1.05, -0.4], "laminate", { bv: 0.01 });
      A.box([-16.15, GF + 1.05, -1.05], [-13.85, GF + 1.1, -0.35], "panel_acc", K0);
      A.box([-15.2, GF + 1.1, -0.85], [-14.8, GF + 1.4, -0.82], "screen", L1);
      // through-wall AC grilles under the room windows
      for (let f = 0; f < 2; f++) for (let i = 1; i < 8; i++) { const [a0, a1] = bay(i), y = GF + f * h; fbox(A, F, a0 + 2.0, a1 - 0.75, y + 0.45, y + 0.85, 0, 0.08, "louvre", { k: 0, lod: 1, bv: 0.01 }); }
      // walkways: ground paving, upper deck, posts, railing, canopy, lights
      A.box([-18, -0.3, 2.4], [18, GF, 4.2], "paving", { bv: 0.015 });
      A.box([-18, -0.3, -6], [-16.5, GF, 2.4], "paving", { bv: 0.015 }); A.box([16.5, -0.3, -6], [18, GF, 2.4], "paving", { bv: 0.015 });
      A.box([-18, -0.25, 4.2], [18, 0.0, 6.0], "asphalt");
      for (let x = -16.2; x <= 16.3; x += 2.7) A.box([x - 0.05, 0.0, 4.4], [x + 0.05, 0.006, 6.0], "line_w", L1);
      for (let x = -14.85; x < 16.2; x += 2.7) A.box([x - 0.8, 0, 4.55], [x + 0.8, 0.12, 4.7], "precast", { k: 0, lod: 1, bv: 0.02 });
      A.box([-16.5, yU - 0.25, 2.4], [16.5, yU, 4.2], "concrete_wash", { bv: 0.02 });
      A.box([-16.5, yU - 0.32, 4.2], [16.5, yU + 0.05, 4.26], "panel_acc", { k: 0, bv: 0.01 });
      for (const x of [...b.slice(1, 8), -16.45, 16.45, -17.9, 17.9]) A.box([x - 0.075, GF, 3.95], [x + 0.075, top - 0.25, 4.1], "steel_blk", { bv: 0.01 });
      mrail(A, "x", -18, 18, 4.15, yU, 1.05, 1.5);
      A.box([-18, top - 0.25, 2.4], [18, top - 0.08, 4.45], "panel_lt", { bv: 0.01 });
      A.box([-18.02, top - 0.35, 4.42], [18.02, top + 0.05, 4.48], "panel_acc", { k: 0, bv: 0.01 });
      A.box([-16.4, top + 0.05, 4.42], [-12.2, top + 0.9, 4.5], "sign_blank", L1);
      for (let i = 0; i < 8; i++) { const c = (b[i] + b[i + 1]) / 2; for (const yy of [yU - 0.27, top - 0.27]) A.box([c - 0.3, yy, 3.2], [c + 0.3, yy + 0.02, 3.4], "light_panel", L1); }
      // end stairs: precast flights with glass guards, landings onto the upper walkway
      for (const s of [-1, 1]) {
        const [b0, b1] = s < 0 ? [-18, -16.6] : [16.6, 18];
        flight(A, { axis: "z", b0, b1, a0: -3.0, dir: 1, y0: GF, rise: h, n: 18, run: 4.86, m: "precast", soffit: "panel_dk", guard: true });
        A.box([s < 0 ? -18 : 16.5, yU - 0.25, 1.86], [s < 0 ? -16.5 : 18, yU, 4.2], "concrete_wash", { bv: 0.02 });
        mrail(A, "z", 1.9, 4.12, s * 17.96, yU, 1.05, 1.2);
      }
    },
  });
  for (const d of doors) if (!d.to) apron(A, frame(w, d.side), d.c, d.w);
  addRoom(A, "walk_g", "hall", -16.5, GF, 2.4, 16.5, yU - 0.3, 4.2);
  addRoom(A, "walk_u", "hall", -16.5, yU, 2.4, 16.5, top - 0.3, 4.1);
  for (const s of [-1, 1]) {
    const id = s < 0 ? "stair_w" : "stair_e", x = s * 17.3;
    addRoom(A, id, "hall", s < 0 ? -18 : 16.6, GF, -3.6, s < 0 ? -16.6 : 18, top - 0.3, 4.1);
    navDoor(A, id, "walk_u", [s * 16.5, yU, 3.3]); navDoor(A, id, "outside", [x, GF, -3.6]);
    A._nav.entrances.push({ p: [x, 0, -4.6], dir: [0, 0, 1], room: id });
  }
  for (const x of [-12, -4, 4, 12]) { navDoor(A, "walk_g", "outside", [x, GF, 4.2]); A._nav.entrances.push({ p: [x, 0, 5.2], dir: [0, 0, -1], room: "walk_g" }); }
  for (const x of [-14, -7, 0, 7, 14]) A._nav.cover.push(cover([x, yU, 3.7], 0, "low", ["over"]));
  coverRing(A, -16.5, 16.5, -6, 2.4);
  doorTour(A, "office_door", -13.2, 2.4, 2.1, GF, "mod_nt_motel_office");
  return finish(A);
}

// ============================================================================ 9. fire station
// a narrow switchback (two 0.85 m flights round a spine) for the drill tower
function miniStair(A, { xa, zA, dir, ys, cw = 0.85, gap = 0.1, tread = 0.25, land = 0.9, rooms }) {
  const len = 2 * cw + gap;
  for (let i = 0; i + 1 < ys.length; i++) {
    const y = ys[i], hh = ys[i + 1] - y, n = Math.round(hh / 2 / 0.18), R = n * tread, zL = zA + dir * R, zE = zL + dir * land;
    flight(A, { b0: xa, b1: xa + cw, a0: zA, dir, y0: y, rise: hh / 2, n, run: R, m: "precast", soffit: "concrete_wash" });
    A.box([xa, y + hh / 2 - 0.2, Math.min(zL, zE)], [xa + len, y + hh / 2, Math.max(zL, zE)], "landing_x");
    flight(A, { b0: xa + cw + gap, b1: xa + len, a0: zL, dir: -dir, y0: y + hh / 2, rise: hh / 2, n, run: R, m: "precast", soffit: "concrete_wash" });
    A.box([xa + cw, y, Math.min(zA, zL)], [xa + cw + gap, ys[i + 1] + (i + 2 === ys.length ? 1.05 : 0), Math.max(zA, zL)], "concrete_wash");
    if (rooms) navDoor(A, rooms[i], rooms[i + 1], [xa + cw / 2, y, zA + dir * 0.3]);
    // guard along the open side of the well on the level above, the head of the second flight left open
    const yu = ys[i + 1], [g0, g1] = zr(zA + dir * 0.9, zE);
    A.box([xa + len, yu, g0], [xa + len + 0.05, yu + 1.05, g1], "rail_steel", { v: 0 });
    A.box([xa + len, yu + 1.0, g0], [xa + len + 0.05, yu + 1.05, g1], "rail_steel", L1);
    for (const z of [g0, (g0 + g1) / 2, g1]) A.box([xa + len, yu, z - 0.025], [xa + len + 0.05, yu + 1.0, z + 0.025], "rail_steel", L1);
  }
  const yt = ys.at(-1);   // the top of the well (the roof): rail across the first flight
  A.box([xa, yt, zA - dir * 0.05], [xa + cw, yt + 1.05, zA], "rail_steel");
  return len;
}
export function fireStation() {
  const A = mk("mod_nt_fire_station", "Fire station", "Fire station, 26 x 18 m: a 16 x 18 m brick apparatus bay with three tall sectional doors on the front (the centre one open, a fire engine placed behind it as a companion), a drive-through door at the back, clerestory glazing, gear lockers, hose racks and lane markings; a two-storey wing with the office and watch room, a gear store and toilets below and a day room, kitchen, dormitory and washroom above off a stair; and a three-storey concrete drill tower with open window openings, an internal stair and a parapeted roof on the back corner.",
    2, [26, 18], [0.55, 0.06, 0.04]);
  nt(A);
  const bayW = wing(A, {
    tag: "b", x0: -13, x1: 3, z0: -9, z1: 9, n: 1, h: 6.8, band: null, par: 1.0, skin: () => "brick",
    fac: { crest: [{ a: -5, w: 7.0, kind: "pediment", rise: 1.5 }], piers: "brick", quoin: "td_cast_stone", sides: ["front", "rear", "west"] },   // TD1
    doors: [...[-10, -5, 0].map((c, i) => ({ id: `bay_door_${i + 1}`, side: "front", c, w: 4.0, h: 4.6, roller: true, room: "bay", apron: false })),
      { id: "bay_door_rear", side: "rear", c: -5, w: 4.0, h: 4.6, roller: true, room: "bay", apron: false },
      { id: "bay_rear_door", side: "rear", c: -11, w: 1.0, room: "bay", apron: false },
      { id: "bay_to_wing", side: "east", c: 1.8, w: 1.0, room: "bay", to: "w0_corridor" },
      { id: "yard_door", side: "east", c: -7.0, w: 1.0, room: "bay", apron: false }],
    glaze: () => ({ front: [[-12.3, 2.3, 5.2, 6.4, 3.6]], west: [[-7, 7, 4.8, 6.4, 3.5]] }),
    plan: () => ({ rooms: [{ id: "bay", type: "workshop", x0: -12.7, z0: -8.7, x1: 2.7, z1: 8.7 }], walls: [] }),
    roof: (A, w) => { rod(A, [-11, w.top, -7], [-11, w.top + 4.5, -7], 0.08, "alu_lt", { k: 1 }); A.box([-11.3, w.top + 3.4, -7.2], [-10.7, w.top + 3.8, -6.8], "steel_blk", L1); },
    dress: (A, w) => {
      const y = GF, F = sideF(w, "front");
      // gear lockers along the west wall: open cubbies with hooks
      A.box([-12.7, y, -8.0], [-12.2, y + 2.1, -0.5], "steel_red", { v: 0 });
      for (let z = -8.0; z < -0.45; z += 0.75) {
        A.box([-12.7, y, z], [-12.2, y + 2.1, z + 0.04], "steel_red", K0);
        A.box([-12.7, y + 0.45, z], [-12.2, y + 0.48, z + 0.75], "steel_red", L1);
        A.box([-12.7, y + 1.75, z], [-12.2, y + 1.78, z + 0.75], "steel_red", L1);
        A.box([-12.5, y + 0.48, z + 0.15], [-12.3, y + 1.4, z + 0.6], "awn_cream", L1);
      }
      A.box([-12.7, y + 2.1, -8.0], [-12.2, y + 2.14, -0.46], "steel_red", L1);
      // hose rack on the rear wall, tool board, lane markings, drains, trusses, exhaust drops
      for (let i = 0; i < 6; i++) A.cyl([-2.0 + i * 0.6, y + 1.2, -8.45], [0.5, 0.2, 0.5], "panel_acc", { k: 0, lod: 1, seg: 16, r: [90, 0, 0] });
      A.box([-2.4, y + 0.6, -8.7], [1.4, y + 0.64, -8.3], "steel_blk", L1);
      A.box([-9, y + 1.0, -8.72], [-7, y + 2.2, -8.68], "panel_lt", L1);
      for (const c of [-10, -5, 0]) {
        for (const e of [-2.0, 2.0]) A.box([c + e - 0.05, y, -8.5], [c + e + 0.05, y + 0.006, 8.6], "line_y", L1);
        A.box([c - 0.15, y - 0.01, -6], [c + 0.15, y + 0.003, 6], "steel_plate", L1);
        rod(A, [c, w.top - SLAB - 0.7, -4.5], [c, y + 2.4, -4.5], 0.15, "rubber");
      }
      trusses(A, -12.7, 2.7, -8.7, 8.7, w.top - SLAB - 0.02, 4.0);
      fbox(A, F, -12.5, 2.5, 5.0, 5.15, 0, 0.12, "panel_acc", { k: 0, bv: 0.01 });           // door head band
      for (const c of [-10, -5, 0]) A.wedge([c, 0.15, 9.225], [4.0, 0.3, 0.45], "concrete_wash", {});   // threshold ramps
      fbox(A, F, -12.95, 2.95, -0.3, GF + 0.5, -0.005, 0.04, "precast", { k: 0, bv: 0.01 });
    },
  });
  const doorOpen = A.interactables.find((it) => it.id === "bay_door_2");
  doorOpen.defaultOpen = true;
  const ysW = [GF, GF + 3.4];
  const k = mkCore({ xa: 10.08, zA: 0.88, dir: -1, ys: ysW, encL: true, encR: false, encTo: -4.7, tag: "core" });
  const wg = wing(A, {
    tag: "w", x0: 3, x1: 13, z0: -5, z1: 9, omit: ["west"], n: 2, h: 3.4, cores: [k], band: "panel_acc", par: 1.0, skin: (f) => (f === 0 ? "brick" : "panel_lt"),
    fac: { wall: "panel_lt", m: "td_cast_dark", prof: [[0.12, 0.06], [0.14, 0.15]], roof: ["ac", "vent"] },   // TD1
    doors: [{ id: "office_door", side: "front", c: 5.3, w: 1.1, glass: true, room: "w0_office", apron: false }, { id: "wing_exit", side: "east", c: 1.8, w: 1.0, room: "w0_corridor", apron: false }],
    glaze: (f) => (f === 0 ? { front: [[6.1, 7.3, 0.9, 2.5], [8.2, 12.2, 0.9, 2.5, 2.0]], rear: [[4.0, 5.6, 1.2, 2.4], [7.6, 8.6, 1.5, 2.3]], east: [[3.5, 8.0, 0.9, 2.5]] }
      : { front: [[3.6, 7.6, 0.9, 2.5, 2.0], [8.8, 12.2, 0.9, 2.5]], rear: [[3.8, 6.2, 1.0, 2.5], [7.6, 8.8, 1.5, 2.3]], east: [[3.5, 8.0, 0.9, 2.5]] }),
    plan: (f, { w }) => dl(w, { pre: `w${f}_`, zc: [1.0, 2.6],
      front: f === 0 ? [{ x1: 7.6, id: "office", type: "office_modern" }, { id: "watch", type: "office_modern" }] : [{ x1: 8.2, id: "day", type: "sitting_modern" }, { id: "kitchen", type: "kitchen_modern" }],
      rear: f === 0 ? [{ x1: 6.5, id: "gear", type: "utility" }, { x1: 9.93, id: "wc", type: "bathroom_modern" }, { skip: 1 }] : [{ x1: 6.8, id: "dorm", type: "bedroom_modern" }, { x1: 9.93, id: "wash", type: "bathroom_modern" }, { skip: 1 }] }),
    roof: (A, w) => hvac(A, 6.5, 5.5, w.top, 2.0, 1.2, 1.0),
    dress: (A, w) => {
      const F = sideF(w, "front");
      fbox(A, F, 3.0, 12.95, 3.35, 3.75, 0, 0.08, "sign_blank", L1);
      fbox(A, F, 3.02, 12.98, -0.3, GF + 0.5, -0.005, 0.04, "precast", { k: 0, bv: 0.01 });
    },
  });
  wingAprons(A, wg, [["front", 5.3, 1.1], ["east", 1.8, 1.0]]);
  wingAprons(A, bayW, [["rear", -11, 1.0]]);
  buildCore(A, k, wg.top - SLAB, ["w0_corridor", "w1_corridor"]);
  // yard between the bay, the wing and the tower
  A.box([3, -0.2, -9], [9, 0.02, -5], "asphalt");
  // drill tower: 4 x 4 m, three storeys, open window openings, parapeted roof
  const tx0 = 9, tx1 = 13, tz0 = -9, tz1 = -5, Tt = 0.2, ty = [GF, GF + 3, GF + 6, GF + 9];
  A.box([tx0, -0.3, tz0], [tx1, GF, tz1], "concrete");
  const hole = { a0: 9.2, a1: 11.0, y0: -8.8, y1: -5.9 };
  for (let i = 1; i < 4; i++) for (const [a, bb, c, d] of cutRect(9.2, 12.8, -8.8, -5.2, [hole])) A.box([a, ty[i] - 0.2, c], [bb, ty[i], d], i === 3 ? "deck_conc" : "concrete");
  const faces = [
    { s: "front", F: { axis: "x", out: 1, outer: tz1, inner: tz1 - Tt, yaw: 0 }, a: [tx0, tx1], op: [] },
    { s: "rear", F: { axis: "x", out: -1, outer: tz0, inner: tz0 + Tt, yaw: 180 }, a: [tx0, tx1], op: [[11.9, 1], [11.9, 2]] },
    { s: "east", F: { axis: "z", out: 1, outer: tx1, inner: tx1 - Tt, yaw: 90 }, a: [tz0 + Tt, tz1 - Tt], op: [[-7.0, 0], [-7.0, 1], [-7.0, 2]] },
    { s: "west", F: { axis: "z", out: -1, outer: tx0, inner: tx0 + Tt, yaw: 270 }, a: [tz0 + Tt, tz1 - Tt], op: [[-7.0, 1], [-7.0, 2]] },
  ];
  for (const { s, F, a, op } of faces) {
    const [d0, d1] = zr(F.inner, F.outer), holes = [];
    if (s === "rear") holes.push(mdoor(A, F, { id: "drill_door", c: 11.9, floor: GF, w: 1.0, h: 2.1, depth: 0.2 }));
    for (const [c, lv] of op) {
      const y0 = ty[lv] + 0.9, y1 = ty[lv] + 2.2;
      holes.push({ a0: c - 0.5, a1: c + 0.5, y0, y1 });
      wbox(A, F.axis, c - 0.56, c + 0.56, y0 - 0.06, y0, d0 - 0.03, d1 + 0.03, "steel_blk", L1);
      for (const e of [-0.53, 0.53]) wbox(A, F.axis, c + e - 0.03, c + e + 0.03, y0, y1, d0 - 0.02, d1 + 0.02, "steel_blk", L1);
      wbox(A, F.axis, c - 0.56, c + 0.56, y1, y1 + 0.06, d0 - 0.02, d1 + 0.02, "steel_blk", L1);
      const ins = F.inner - F.out * 0.5;
      A._nav.firingPoints.push(fire(F.axis === "x" ? [c, ty[lv], ins] : [ins, ty[lv], c], F.yaw, "window"));
    }
    for (const [p0, p1, y0, y1] of cutRect(a[0], a[1], GF - 0.05, ty[3] + 1.05, holes)) wbox(A, F.axis, p0, p1, y0, y1, d0, d1, "conc_fw");
    wbox(A, F.axis, a[0] - (F.axis === "x" ? 0.03 : 0), a[1] + (F.axis === "x" ? 0.03 : 0), ty[3] + 1.05, ty[3] + 1.12, d0 - 0.03, d1 + 0.03, "precast", { k: 0, bv: 0.01 });
    for (let i = 1; i < 4; i++) wbox(A, F.axis, a[0], a[1], ty[i] - 0.22, ty[i] - 0.18, F.outer, F.outer + F.out * 0.02, "concrete", L1);   // floor-line grooves
  }
  miniStair(A, { xa: 9.2, zA: -5.9, dir: -1, ys: ty, rooms: ["dt0", "dt1", "dt2", "dt_roof"] });
  bar(A, [12.6, ty[3] + 1.05, -8.6], [12.6, ty[3] + 2.6, -8.6], 0.1, 0.1, "steel_blk", K0);   // rappel davit
  bar(A, [12.6, ty[3] + 2.6, -8.6], [13.3, ty[3] + 2.6, -8.6], 0.1, 0.1, "steel_blk", K0);
  for (let i = 0; i < 4; i++) addRoom(A, i < 3 ? `dt${i}` : "dt_roof", "utility", 11.0, ty[i], -8.8, 12.8, ty[i] + 2.6, -5.2);
  navDoor(A, "dt0", "outside", [11.9, GF, -9.0]); A._nav.entrances.push({ p: [11.9, 0, -9.8], dir: [0, 0, 1], room: "dt0" });
  apron(A, { axis: "x", out: -1, d: tz0, floorY: GF }, 11.9, 1.0);
  A.meta.companions = [{ asset: "mod_nt_fire_engine", pos: [-5, GF, 2.6], yaw: 0 }];
  A._nav.cover.push(cover([6, 0, -7], 270, "high", ["left", "right"]));
  coverRing(A, -13, 13, -9, 9);
  doorTour(A, "office_door", 5.3, 9, 8.7, GF, "mod_nt_fire_station_office");
  return finish(A);
}

// ============================================================================ 10. town hall
export function municipal() {
  const n = 3, h = 3.8, gf = 0.9, ys = Array.from({ length: n + 1 }, (_, f) => gf + f * h), zc = [-1.6, 0.8];
  const A = mk("mod_nt_municipal", "Town hall", "Three-storey municipal offices and town hall, 30 x 18 m: limestone ground floor and brick upper floors with stone bands, pilasters and tall windows, a raised entrance under a two-storey portico of six columns with an entablature and a pediment (blank roundel), broad steps. Inside: a lobby with a reception counter, registry, information and back offices, toilets on each floor, offices and a meeting room above, the mayor's office and a council chamber with a dais and a fixed horseshoe desk on the top floor; a stair core with a roof door, a lift, roof plant and a flagpole.",
    3, [30, 18], [0.10, 0.18, 0.30]);
  nt(A);
  const k = mkCore({ xa: -14.7, zA: 0.92, ys, encR: true, encTo: 5.7, roof: true, tag: "core" });
  const L = mkLift({ x0: -14.7, x1: -12.3, z0: -8.7, z1: -1.6, door: "front", ys: ys.slice(0, n) });
  const FR = [[-11.93, -6, 6, 14.7], [-11.93, -6, 0, 6, 14.7], [-11.93, -5, 3, 14.7]], RR = [[-12.3, -9, -2, 5, 10, 14.7], [-12.3, -9, -2, 5, 14.7], [-12.3, -9, -3, 14.7]];
  const w = wing(A, {
    tag: "m", x0: -15, x1: 15, z0: -9, z1: 6, n, h, gf, cores: [k], lifts: [L], band: "stone_dressed", fin: 0.06, finMat: "stone_dressed",
    fac: { m: "stone_dressed", prof: [[0.14, 0.08], [0.16, 0.18], [0.18, 0.34]], crest: [{ a: 0, w: 10.0, kind: "pediment", rise: 1.7 }], piers: "stone_dressed",   // TD1
      pil: [{ at: "auto", f0: 1, w: 0.6, m: "stone_dressed" }] },   // NT2b
    skin: (f) => (f === 0 ? "stone_clad" : "brick"),
    doors: [{ id: "main_entrance", side: "front", c: 0, w: 2.4, h: 2.8, double: true, glass: true, room: "f0_lobby" }, { id: "east_exit", side: "east", c: -0.4, w: 1.0, room: "f0_corridor", apron: false }],
    glaze: (f) => ({
      front: f === 0 ? [...wins([-11.93, -6], 1.5, 0.9, 3.2, 2.8), [-4.6, -3.1, 0.9, 3.2], [3.1, 4.6, 0.9, 3.2], [-2.1, -1.5, 0.1, 2.8], [1.5, 2.1, 0.1, 2.8], ...wins([6, 14.7], 1.5, 0.9, 3.2, 2.8)] : wins(FR[f], 1.5, 0.9, 3.2, 2.8),
      rear: wins(RR[f], 1.5, 0.9, 3.2, 2.8),
      east: [[2.0, 4.6, 0.9, 3.2], [-6.6, -3.6, 0.9, 3.2]], west: f ? [[-1.2, 0.4, 0.9, 3.2]] : [],
    }),
    plan: (f, { w }) => dl(w, { pre: `f${f}_`, zc,
      front: f === 0 ? [{ x1: -11.93, skip: 1 }, { x1: -6, id: "registry", type: "office_modern" }, { x1: 6, id: "lobby", type: "lobby", open: 1 }, { id: "info", type: "office_modern" }]
        : f === 1 ? [{ x1: -11.93, skip: 1 }, { x1: -6, id: "office_1", type: "office_modern" }, { x1: 0, id: "office_2", type: "office_modern" }, { x1: 6, id: "office_3", type: "office_modern" }, { id: "office_4", type: "office_modern" }]
          : [{ x1: -11.93, skip: 1 }, { x1: -5, id: "mayor", type: "office_modern" }, { x1: 3, id: "office_7", type: "office_modern" }, { id: "committee", type: "office_modern" }],
      rear: f === 0 ? [{ x1: -12.3, skip: 1 }, { x1: -9, id: "wc", type: "bathroom_modern" }, { x1: -2, id: "office_a", type: "office_modern" }, { x1: 5, id: "office_b", type: "office_modern" }, { x1: 10, id: "office_c", type: "office_modern" }, { id: "archive", type: "utility" }]
        : f === 1 ? [{ x1: -12.3, skip: 1 }, { x1: -9, id: "wc", type: "bathroom_modern" }, { x1: -2, id: "office_5", type: "office_modern" }, { x1: 5, id: "office_6", type: "office_modern" }, { id: "meeting", type: "office_modern" }]
          : [{ x1: -12.3, skip: 1 }, { x1: -9, id: "wc", type: "bathroom_modern" }, { x1: -3, id: "clerk", type: "office_modern" }, { id: "chamber", type: "hall", dw: 1.8 }] }),
    roof: (A, w) => {
      hvac(A, 4, -5.5, w.top); hvac(A, 8.5, -5.5, w.top); plantScreen(A, 2.2, 10.3, -7.2, -3.8, w.top, 1.6);
      A.box([0.5, w.top, 2.5], [1.1, w.top + 0.3, 3.1], "precast", { bv: 0.02 });
      A.cyl([0.8, w.top + 4.3, 2.8], [0.1, 8.0, 0.1], "alu_lt", { seg: 10 });
      A.cyl([0.8, w.top + 8.35, 2.8], [0.18, 0.1, 0.18], "bronze", { k: 0, seg: 10 });
    },
    dress: (A, w) => {
      const y = gf, F = sideF(w, "front");
      A.box([-3.0, y, 3.4], [3.0, y + 1.05, 4.1], "laminate", { bv: 0.01 });                 // reception counter
      A.box([-3.05, y + 1.05, 3.35], [3.05, y + 1.1, 4.15], "stone_dressed", K0);
      // council chamber: dais, mayor's bench, horseshoe desk
      const yc = ys[2];
      A.box([12.2, yc, -8.7], [14.7, yc + 0.3, -1.72], "plank_x", { bv: 0.01 });
      A.box([13.4, yc + 0.3, -6.8], [14.0, yc + 1.05, -3.6], "laminate", { bv: 0.01 });
      for (const [a, bb, c, d] of [[2.5, 10.5, -3.1, -2.5], [2.5, 3.1, -7.8, -3.1], [9.9, 10.5, -7.8, -3.1]]) {
        A.box([a, yc, c], [bb, yc + 0.76, d], "laminate", { bv: 0.01 });
        A.box([a - 0.03, yc + 0.76, c - 0.03], [bb + 0.03, yc + 0.8, d + 0.03], "panel_acc", K0);
      }
      // pilasters between the bays of the brick floors
      for (const a of [-14.6, -6.0, 6.0, 14.6]) fbox(A, F, a - 0.3, a + 0.3, ys[1], w.top - 0.3, -0.005, 0.08, "stone_clad", { k: 0, lod: 1, bv: 0.01 });
      fbox(A, F, -15.15, 15.15, w.top - 0.45, w.top - 0.1, -0.005, 0.25, "stone_dressed", { k: 0, bv: 0.015 });
      // portico: platform, steps, six columns, entablature, pediment with a blank roundel
      A.box([-6.4, -0.3, 6], [6.4, gf, 8.0], "stone_clad", { bv: 0.02 });
      for (let i = 0; i < 5; i++) A.box([-6.4, -0.3, 8.0 + i * 0.2], [6.4, gf - (i + 1) * 0.18, 9.0], "precast", { bv: 0.01 });
      for (const s of [-1, 1]) A.box([s * 6.4 - (s > 0 ? 0 : 0.35), -0.3, 6], [s * 6.4 + (s > 0 ? 0.35 : 0), gf + 0.25, 9.0], "stone_clad", { bv: 0.02 });
      const yt = ys[2] - 0.2;
      for (const x of [-5.2, -3.1, -1.05, 1.05, 3.1, 5.2]) {
        A.box([x - 0.42, gf, 7.2], [x + 0.42, gf + 0.3, 8.0], "stone_dressed", { bv: 0.02 });
        A.cyl([x, (gf + 0.3 + yt - 0.3) / 2, 7.6], [0.62, yt - 0.3 - gf - 0.3, 0.62], "stone_clad", { seg: 20 });
        A.box([x - 0.42, yt - 0.3, 7.2], [x + 0.42, yt, 8.0], "stone_dressed", { bv: 0.02 });
      }
      A.box([-6.3, yt, 6.0], [6.3, yt + 0.9, 8.3], "stone_dressed", { bv: 0.02 });
      A.box([-6.45, yt + 0.9, 5.95], [6.45, yt + 1.05, 8.45], "stone_dressed", { k: 0, bv: 0.01 });
      A.prism(2, 7.6, 8.3, [[-6.3, yt + 1.05], [6.3, yt + 1.05], [0, yt + 2.4]], "stone_clad");
      A.cyl([0, yt + 1.55, 8.32], [0.8, 0.04, 0.8], "bronze", { k: 0, seg: 24, r: [90, 0, 0] });
      A.box([-6.2, gf + 0.02, 6.05], [6.2, gf + 0.025, 7.9], "stone_dressed", L1);
      fbox(A, F, -1.6, 1.6, 3.95, 4.5, 0, 0.05, "bronze", L1);                                 // blank plaque over the door
      for (const x of [-4, 4]) A.box([x - 0.3, yt - 0.02, 6.6], [x + 0.3, yt, 7.0], "light_panel", L1);
      for (const s of SIDES) { const G = sideF(w, s), isX = G.axis === "x"; fbox(A, G, isX ? -15.03 : -8.995, isX ? 15.03 : 5.995, -0.3, gf + 0.4, -0.01, 0.05, "precast", { k: 0, bv: 0.01 }); }
    },
  });
  wingAprons(A, w, [["east", -0.4, 1.0]]);
  buildCore(A, k, 0, ys.slice(0, n).map((_, f) => `f${f}_corridor`));
  buildLift(A, L, w.top);
  addRoom(A, "roof", "utility", w.ix0, w.top, w.iz0, w.ix1, w.top + 2.5, w.iz1);
  navDoor(A, `f${n - 1}_corridor`, "roof", [k.xa + k.len / 2, w.top, k.zF]);
  addRoom(A, "portico", "hall", -6.3, gf, 6.0, 6.3, gf + 3, 8.0);
  A._nav.cover.push(...[-5.2, -3.1, -1.05, 1.05, 3.1, 5.2].map((x) => cover([x, gf, 6.8], 0, "high", ["left", "right"])));
  coverRing(A, -15, 15, -9, 6);
  doorTour(A, "main_entrance", 0, 9, 5.7, gf, "mod_nt_municipal_lobby");
  A.meta.tour.steps.unshift({ goto: [0, 10], note: "to the steps" });
  return finish(A);
}

// ============================================================================ 11. covered passage
export function coveredPassage() {
  const A = propAsset("mod_nt_covered_passage", "Covered passage", "Covered pedestrian passage, 6 x 16 m, to link two buildings: a flat metal roof with a central glazed rooflight strip, standing seams, gutters and fascia on two rows of round steel columns with base plates, cross beams, linear lights, two fixed timber benches and a litter bin on a paved floor. Walk-through; the columns and benches give cover.",
    [6, 16], [0.08, 0.22, 0.32]);
  const yr = 3.3;
  A.box([-3, 0, -8], [3, 0.03, 8], "paving", K0);
  for (const x of [-2.65, 2.65]) for (const z of [-7.5, -2.5, 2.5, 7.5]) {
    A.cyl([x, yr / 2, z], [0.3, yr, 0.3], "alu", { seg: 16 });
    A.box([x - 0.25, 0.0, z - 0.25], [x + 0.25, 0.04, z + 0.25], "steel_blk", K0);
    A.box([x - 0.18, yr - 0.12, z - 0.18], [x + 0.18, yr, z + 0.18], "steel_blk", L1);
  }
  for (const x of [-2.65, 2.65]) A.box([x - 0.12, yr, -8], [x + 0.12, yr + 0.3, 8], "steel_blk", { bv: 0.01 });
  for (const z of [-7.5, -2.5, 2.5, 7.5]) A.box([-2.65, yr, z - 0.1], [2.65, yr + 0.25, z + 0.1], "steel_blk", L1);
  for (const [x0, x1] of [[-3, -0.8], [0.8, 3]]) A.box([x0, yr + 0.3, -8], [x1, yr + 0.42, 8], "panel_lt", { bv: 0.01 });
  A.box([-0.8, yr + 0.36, -8], [0.8, yr + 0.38, 8], "glass_t", K0);
  for (let z = -7.5; z <= 7.5; z += 1.5) A.box([-0.8, yr + 0.3, z - 0.03], [0.8, yr + 0.42, z + 0.03], "alu", L1);
  for (const [x0, x1] of [[-3, -0.8], [0.8, 3]]) for (let x = x0 + 0.4; x < x1; x += 0.45) A.box([x - 0.012, yr + 0.42, -8], [x + 0.012, yr + 0.46, 8], "alu_lt", L1);
  for (const s of [-1, 1]) {
    A.box([s * 3 - 0.06, yr + 0.22, -8.02], [s * 3 + 0.06, yr + 0.55, 8.02], "panel_acc", { k: 0, bv: 0.01 });
    A.box([s * 2.88 - 0.08, yr + 0.38, -8], [s * 2.88 + 0.08, yr + 0.44, 8], "alu", L1);       // gutter
    rod(A, [s * 2.85, yr + 0.38, 7.5], [s * 2.85, 0.1, 7.5], 0.1, "alu");
    for (const z of [-5, 0, 5]) A.box([s * 1.6 - 0.08, yr + 0.26, z - 1.0], [s * 1.6 + 0.08, yr + 0.3, z + 1.0], "light_panel", L1);
  }
  for (const zc of [-5, 5]) {
    const x = -2.1;
    for (const dz of [-0.8, 0.8]) A.box([x - 0.25, 0, zc + dz - 0.04], [x + 0.25, 0.45, zc + dz + 0.04], "steel_blk", L1);
    for (let i = 0; i < 4; i++) A.box([x - 0.24 + i * 0.125, 0.43, zc - 1.0], [x - 0.14 + i * 0.125, 0.47, zc + 1.0], "bench_wood", K0);
    A.box([x - 0.26, 0, zc - 1.0], [x + 0.26, 0.47, zc + 1.0], "bench_wood", { v: 0 });
  }
  A.cyl([-2.3, 0.45, 0], [0.45, 0.9, 0.45], "panel_dk", { seg: 14 });
  A.cyl([-2.3, 0.92, 0], [0.48, 0.04, 0.48], "alu", { k: 0, seg: 14 });
  setNav(A, { effect: "carve", rooms: [], doors: [], entrances: [], corners: [], firingPoints: [],
    cover: [cover([-1.4, 0, -5], 270, "low", ["over"]), cover([-1.4, 0, 5], 270, "low", ["over"]), cover([-1.7, 0, 0], 270, "low", ["over"]),
      ...[-7.5, -2.5, 2.5, 7.5].flatMap((z) => [cover([-2.65, 0, z + 0.5], 180, "high", ["left", "right"]), cover([2.65, 0, z - 0.5], 0, "high", ["left", "right"])])], areas: [] });
  return A;
}

// ============================================================================ 12. kiosk
export function kiosk() {
  const A = propAsset("mod_nt_kiosk", "Newspaper and snack kiosk", "Street kiosk, 3 x 3 m: a green metal-panel booth on a concrete base with a wide serving hatch and counter shelf under a rolled-up shutter, angled magazine racks on both sides, shelves of goods inside, a flat overhanging roof with a blank fascia sign all round, a lamp and a rear door.",
    [3, 3], [0.06, 0.30, 0.18]);
  const hw = 1.25, H = 2.55;
  A.box([-1.4, 0, -1.4], [1.4, 0.12, 1.4], "precast", { bv: 0.02 });
  const holes = [{ a0: -0.9, a1: 0.9, y0: 0.95, y1: 2.05 }];
  for (const [a0, a1, y0, y1] of cutRect(-hw, hw, 0.12, H, holes)) A.box([a0, y0, hw - 0.06], [a1, y1, hw], "panel_acc");
  A.box([-hw, 0.12, -hw], [hw, H, -hw + 0.06], "panel_acc");
  for (const s of [-1, 1]) A.box([s * hw - 0.03, 0.12, -hw], [s * hw + 0.03, H, hw], "panel_acc");
  for (const s of [-1, 1]) for (const y of [0.6, 1.4, 2.2]) A.box([s * (hw + 0.03) - 0.005, y, -hw], [s * (hw + 0.03) + 0.005, y + 0.02, hw], "panel_dk", L1);
  A.box([-0.95, 0.9, hw], [0.95, 0.95, hw + 0.3], "laminate", { bv: 0.01 });
  for (const s of [-1, 1]) A.obox([s * 0.95, 0.75, hw + 0.15], [0.03, 0.3, 0.3], [45 * 0, 0, 0], "alu", L1);
  A.box([-1.0, 2.05, hw - 0.02], [1.0, 2.3, hw + 0.2], "alu", { k: 0, bv: 0.02 });               // rolled-up shutter box
  A.box([-0.9, 2.04, hw - 0.04], [0.9, 2.06, hw], "alu_lt", L1);
  // inside: shelves of goods seen through the hatch
  for (const y of [0.9, 1.35, 1.8]) {
    A.box([-1.15, y, -1.15], [1.15, y + 0.03, -0.75], "panel_lt", L1);
    for (let i = 0; i < 5; i++) A.box([-1.1 + i * 0.45, y + 0.03, -1.1], [-0.75 + i * 0.45, y + 0.3, -0.8], ["goods_a", "goods_b", "goods_c"][(i + Math.round(y * 2)) % 3], L1);
  }
  A.box([-0.3, 0.12, -0.2], [0.3, 0.9, 0.3], "panel_dk", K0);
  A.box([-0.8, 1.1, 0.6], [0.8, 1.13, 1.18], "panel_lt", L1);
  // magazine racks on the sides
  for (const s of [-1, 1]) {
    A.box([s * (hw + 0.03), 0.3, -0.9], [s * (hw + 0.38), 1.6, 0.9], "alu", { v: 0 });
    for (let i = 0; i < 3; i++) {
      const y = 0.45 + i * 0.4;
      A.obox([s * (hw + 0.2), y, 0], [0.3, 0.02, 1.8], [0, 0, s * 30], "alu", L1);
      for (let j = 0; j < 4; j++) A.obox([s * (hw + 0.2), y + 0.13, -0.68 + j * 0.45], [0.02, 0.26, 0.38], [0, 0, s * 30], ["goods_b", "sign_wht", "goods_a", "goods_c"][(i + j) % 4], L1);
    }
  }
  // roof, fascia sign, lamp, rear door
  A.box([-1.5, H, -1.5], [1.5, H + 0.12, 1.5], "panel_lt", { bv: 0.01 });
  for (const [x0, x1, z0, z1] of [[-1.5, 1.5, 1.44, 1.5], [-1.5, 1.5, -1.5, -1.44], [-1.5, -1.44, -1.44, 1.44], [1.44, 1.5, -1.44, 1.44]]) A.box([x0, H + 0.12, z0], [x1, H + 0.5, z1], "sign_red", { k: 0, bv: 0.005 });
  A.box([-0.3, 2.35, hw], [0.3, 2.4, hw + 0.25], "light_panel", L1);
  A.box([-0.4, 0.14, -hw - 0.012], [0.4, 2.1, -hw], "panel_dk", L1);
  A.box([0.25, 1.0, -hw - 0.03], [0.32, 1.06, -hw - 0.012], "alu_lt", L1);
  setNav(A, { effect: "carve", rooms: [], doors: [], entrances: [], corners: [], firingPoints: [],
    cover: [cover([0, 0, -2.0], 0, "high", ["left", "right"]), cover([0, 0, 2.1], 180, "high", ["left", "right"]), cover([-2.1, 0, 0], 90, "high", ["left", "right"]), cover([2.1, 0, 0], 270, "high", ["left", "right"])], areas: [] });
  return A;
}

// ============================================================================ 13. ambulance
export function ambulance() {
  const A = propAsset("mod_nt_ambulance", "Ambulance", "Modern box-body ambulance, 2.3 x 6.2 m: a white short-nose cab with deep windscreen, mirrors and a roof light bar, a 4.3 m white box body with a red waist stripe and blue lower band, corner beacons, a roof air-conditioning unit, rear doors with windows and a step bumper, a kerb-side door, side lockers, lamps; twin rear wheels.",
    [2.3, 6.2], [0.05, 0.2, 0.5]);
  const hx = 1.1, zf = 3.05, zb = 1.2, Dw = 0.76, tw = 0.23, wf = 2.2, wr = -1.6, yC = Dw / 2, paint = "steel_wht";
  // cab: bonnet, lower cab, glass, roof
  A.rbox([-1.0, 0.45, zb], [1.0, 1.25, zf - 0.05], 0.08, paint, K0);
  A.prism(0, -0.98, 0.98, [[zb + 0.6, 1.25], [zf - 0.05, 1.25], [zb + 0.6, 1.5]], paint, K0);
  bar(A, [0, 1.5, zb + 0.6], [0, 2.18, zb + 0.1], 1.9, 0.012, "car_glass", K0);
  A.rbox([-1.0, 2.15, zb], [1.0, 2.3, zb + 0.15], 0.03, paint, K0);
  for (const s of [-1, 1]) {
    A.box([s * 1.0 - 0.006, 1.3, zb + 0.05], [s * 1.0 + 0.006, 2.1, zb + 0.55], "car_glass", K0);
    A.box([s * 1.0 - 0.02, 0.5, zb + 0.6], [s * 1.0 + 0.02, 1.25, zb + 0.605], "panel_dk", L1);
    A.box([s > 0 ? 1.0 : -1.2, 1.55, zb + 0.6], [s > 0 ? 1.2 : -1.0, 1.95, zb + 0.66], "steel_blk", K0);   // mirror
    A.box([s > 0 ? 0.62 : -0.95, 0.85, zf - 0.06], [s > 0 ? 0.95 : -0.62, 1.0, zf - 0.03], "lamp_w", L1);
    A.box([s > 0 ? 0.95 : -1.01, 0.7, zf - 0.4], [s > 0 ? 1.01 : -0.95, 0.8, zf - 0.2], "lamp_a", L1);
  }
  A.box([-0.55, 0.6, zf - 0.07], [0.55, 0.82, zf - 0.03], "panel_dk", L1);                     // grille
  A.rbox([-1.05, 0.35, zf - 0.15], [1.05, 0.58, zf + 0.02], 0.04, "steel_blk", K0);              // bumper
  A.box([-0.95, 0.6, zb + 0.15], [0.95, 1.3, zb + 0.55], "seat_fab", L1);                        // seats seen through the glass
  // light bar on the cab roof
  A.box([-0.85, 2.3, zb + 0.02], [0.85, 2.34, zb + 0.2], "steel_blk", L1);
  for (let i = 0; i < 6; i++) A.box([-0.84 + i * 0.28, 2.34, zb + 0.03], [-0.6 + i * 0.28, 2.44, zb + 0.19], i < 3 ? "beacon_b" : "beacon_r", L1);
  // box body
  const z0 = -3.05, z1 = zb + 0.05, y0 = 0.55, y1 = 2.75;
  A.rbox([-hx - 0.02, y0, z0], [hx + 0.02, y1, z1], 0.05, paint, K0);
  for (const s of [-1, 1]) {
    A.box([s * (hx + 0.02) - 0.004, 1.1, z0 + 0.05], [s * (hx + 0.02) + 0.004, 1.32, z1 - 0.05], "stripe_r", L1);
    A.box([s * (hx + 0.02) - 0.004, 0.62, z0 + 0.05], [s * (hx + 0.02) + 0.004, 0.86, z1 - 0.05], "stripe_b", L1);
    A.box([s * (hx + 0.02) - 0.004, 2.55, z0 + 0.05], [s * (hx + 0.02) + 0.004, 2.65, z1 - 0.05], "stripe_r", L1);
    for (const z of [-2.2, -0.4]) A.box([s * (hx + 0.02) - 0.006, 0.6, z - 0.004], [s * (hx + 0.02) + 0.006, 2.4, z + 0.004], "panel_dk", L1);   // locker seams
    A.box([s * (hx + 0.02) - 0.006, 1.65, -1.7], [s * (hx + 0.02) + 0.006, 2.2, -0.9], "car_glass", L1);
  }
  // kerb-side (x -) door, step
  A.box([-hx - 0.028, 0.6, 0.1], [-hx - 0.02, 2.45, 0.104], "panel_dk", L1);
  A.box([-hx - 0.028, 0.6, 0.96], [-hx - 0.02, 2.45, 0.964], "panel_dk", L1);
  A.box([-hx - 0.03, 1.15, 0.2], [-hx - 0.02, 1.2, 0.35], "chrome", L1);
  A.box([-hx - 0.25, 0.35, 0.15], [-hx, 0.4, 0.9], "steel_plate", L1);
  // rear: doors, windows, lamps, step bumper
  A.box([-0.004, y0 + 0.05, z0 - 0.012], [0.004, y1 - 0.1, z0], "panel_dk", L1);
  for (const s of [-1, 1]) {
    A.box([s > 0 ? 0.15 : -0.85, 1.75, z0 - 0.012], [s > 0 ? 0.85 : -0.15, 2.3, z0 - 0.004], "car_glass", L1);
    A.box([s > 0 ? 0.95 : -1.1, 0.9, z0 - 0.02], [s > 0 ? 1.1 : -0.95, 1.6, z0], "lamp_r", L1);
    A.box([s > 0 ? 0.95 : -1.1, 1.62, z0 - 0.02], [s > 0 ? 1.1 : -0.95, 1.8, z0], "lamp_a", L1);
    A.box([s * hx - 0.06, y1 - 0.02, z0 + 0.02], [s * hx + 0.06, y1 + 0.1, z0 + 0.15], s > 0 ? "beacon_r" : "beacon_b", L1);
    A.box([s * hx - 0.06, y1 - 0.02, z1 - 0.15], [s * hx + 0.06, y1 + 0.1, z1 - 0.02], s > 0 ? "beacon_b" : "beacon_r", L1);
  }
  A.box([-0.9, 0.38, z0 - 0.3], [0.9, 0.48, z0], "steel_plate", { k: 0, bv: 0.01 });
  // roof AC unit, antenna, chassis, wheels
  A.rbox([-0.6, y1, -2.4], [0.6, y1 + 0.28, -1.4], 0.05, "hvac", K0);
  rod(A, [0.6, y1, 0.5], [0.6, y1 + 0.4, 0.5], 0.02, "steel_blk");
  for (const s of [-1, 1]) A.box([s * 0.45 - 0.06, 0.3, -3.0], [s * 0.45 + 0.06, 0.5, 2.6], "steel_blk", K0);
  for (const s of [-1, 1]) A.box([s > 0 ? hx - 0.1 : -hx - 0.02, 0.25, wr + 0.5], [s > 0 ? hx + 0.02 : -hx + 0.1, 0.55, wf - 0.5], "panel_dk", K0);   // skirts
  const wx = hx - tw / 2 - 0.02;
  vwheel(A, wx, yC, wf, Dw, tw, 1); vwheel(A, -wx, yC, wf, Dw, tw, -1);
  vwheel(A, wx, yC, wr, Dw, tw, 1, true); vwheel(A, -wx, yC, wr, Dw, tw, -1, true);
  A.box([-hx, 0.3, z0], [hx, y1, z1], paint, { v: 0 });
  A.box([-1.0, 0.3, zb], [1.0, 2.3, zf], paint, { v: 0 });
  setNav(A, { effect: "carve", rooms: [], doors: [], entrances: [], corners: [], firingPoints: [], cover: vcover(6.1, 2.25, 2.7), areas: [] });
  return A;
}

// ============================================================================ 14. fire engine
export function fireEngine() {
  const A = propAsset("mod_nt_fire_engine", "Fire engine", "Modern fire engine, 2.6 x 9.5 m: a red crew cab with a deep windscreen, four doors, mirrors and a roof light bar; a red body with roller-shutter lockers along both sides, a pump panel, a white lower stripe, grab rails, a rear hose reel and step; an extension ladder on a gantry across the roof; chrome bumper, warning lamps, single front and twin rear wheels.",
    [2.6, 9.5], [0.55, 0.06, 0.04]);
  const hx = 1.25, zf = 4.7, zcab = 2.15, Dw = 1.04, tw = 0.32, wf = 3.25, wr = -2.0, yC = Dw / 2, paint = "steel_red";
  // crew cab
  A.rbox([-hx, 0.7, zcab], [hx, 1.6, zf - 0.05], 0.06, paint, K0);
  A.rbox([-hx, 1.6, zcab], [hx, 3.0, zf - 0.45], 0.06, paint, K0);
  A.prism(0, -hx + 0.02, hx - 0.02, [[zf - 0.45, 1.6], [zf - 0.05, 1.6], [zf - 0.45, 2.9]], paint, K0);
  bar(A, [0, 1.65, zf - 0.08], [0, 2.85, zf - 0.42], 2.3, 0.012, "car_glass", K0);
  for (const s of [-1, 1]) {
    for (const [za, zz] of [[zf - 1.55, zf - 0.55], [zcab + 0.1, zf - 1.65]]) A.box([s * hx - 0.006, 1.75, za], [s * hx + 0.006, 2.75, zz], "car_glass", K0);
    for (const z of [zf - 1.6, zcab + 0.05]) A.box([s * hx - 0.01, 0.75, z - 0.004], [s * hx + 0.01, 2.85, z + 0.004], "panel_dk", L1);
    A.box([s > 0 ? hx : -hx - 0.3, 2.1, zf - 0.4], [s > 0 ? hx + 0.3 : -hx, 2.2, zf - 0.35], "steel_blk", K0);
    A.box([s > 0 ? hx + 0.22 : -hx - 0.34, 1.7, zf - 0.42], [s > 0 ? hx + 0.34 : -hx - 0.22, 2.25, zf - 0.32], "steel_blk", K0);
    for (const y of [0.5, 0.95]) A.box([s > 0 ? hx - 0.35 : -hx - 0.02, y, zf - 1.5], [s > 0 ? hx + 0.02 : -hx + 0.35, y + 0.05, zf - 0.7], "alu_lt", L1);   // steps
    A.box([s > 0 ? 0.75 : -1.15, 1.0, zf - 0.06], [s > 0 ? 1.15 : -0.75, 1.22, zf - 0.03], "lamp_w", L1);
    A.box([s > 0 ? 0.75 : -1.15, 1.25, zf - 0.06], [s > 0 ? 1.15 : -0.75, 1.32, zf - 0.03], "beacon_b", L1);
  }
  A.box([-0.65, 0.95, zf - 0.07], [0.65, 1.45, zf - 0.03], "chrome", L1);
  A.rbox([-hx - 0.02, 0.45, zf - 0.12], [hx + 0.02, 0.75, zf + 0.06], 0.04, "chrome", K0);
  A.box([-0.3, 0.5, zf + 0.06], [0.3, 0.7, zf + 0.1], "steel_blk", L1);                          // siren speaker
  A.box([-1.1, 3.0, zf - 0.9], [1.1, 3.05, zf - 0.6], "steel_blk", L1);
  for (let i = 0; i < 8; i++) A.box([-1.08 + i * 0.27, 3.05, zf - 0.88], [-0.84 + i * 0.27, 3.15, zf - 0.62], i % 2 ? "beacon_r" : "beacon_b", L1);
  A.box([-1.1, 1.3, zf - 1.4], [1.1, 2.0, zf - 0.7], "seat_fab", L1);
  // body with lockers
  const z0 = -4.7, z1 = zcab - 0.05, y0 = 0.75, y1 = 2.9;
  A.rbox([-hx, y0, z0], [hx, y1, z1], 0.04, paint, K0);
  const lockers = [[-4.6, -2.75, y0 + 0.05], [-1.25, 0.35, y0 + 0.05], [0.45, 2.0, y0 + 0.05], [-2.65, -1.35, 1.7]];
  for (const s of [-1, 1]) {
    for (const [la, lb, ly] of lockers) {
      A.box([s * hx - 0.01, ly, la], [s * hx + 0.01, y1 - 0.15, lb], "alu_lt", K0);
      for (let y = ly + 0.15; y < y1 - 0.2; y += 0.18) A.box([s * hx - 0.016, y, la + 0.02], [s * hx + 0.016, y + 0.015, lb - 0.02], "alu", L1);
      A.box([s * hx - 0.02, ly + 0.05, (la + lb) / 2 - 0.15], [s * hx + 0.02, ly + 0.1, (la + lb) / 2 + 0.15], "chrome", L1);
    }
    A.box([s * hx - 0.008, y0 + 0.02, z0 + 0.05], [s * hx + 0.008, y0 + 0.15, z1 - 0.05], "sign_wht", L1);
    rod(A, [s * (hx + 0.04), 1.2, z0 + 0.3], [s * (hx + 0.04), 2.6, z0 + 0.3], 0.04, "chrome");
    A.box([s > 0 ? hx - 0.3 : -hx - 0.04, 0.5, -0.9], [s > 0 ? hx + 0.04 : -hx + 0.3, 0.55, 2.0], "alu_lt", L1);   // running board
  }
  // pump panel (kerb side) with gauges
  for (let i = 0; i < 4; i++) A.cyl([-hx - 0.02, 2.2, 1.95 - i * 0.12 - 1.4], [0.09, 0.02, 0.09], "chrome", { k: 0, lod: 1, seg: 10, r: [0, 0, 90] });
  // rear: hose reel, lamps, step, ladder gantry and ladder
  A.cyl([0, 1.9, z0 - 0.12], [0.7, 0.22, 0.7], "panel_acc", { k: 0, seg: 20, r: [0, 0, 90] });
  A.cyl([0, 1.9, z0 - 0.12], [0.78, 0.03, 0.78], "chrome", { k: 0, seg: 20, r: [0, 0, 90] });
  A.box([-hx + 0.1, 0.5, z0 - 0.35], [hx - 0.1, 0.6, z0], "alu_lt", { k: 0, bv: 0.01 });
  for (const s of [-1, 1]) {
    A.box([s > 0 ? 0.9 : -1.2, 1.0, z0 - 0.02], [s > 0 ? 1.2 : -0.9, 1.5, z0], "lamp_r", L1);
    A.box([s > 0 ? 0.9 : -1.2, 1.52, z0 - 0.02], [s > 0 ? 1.2 : -0.9, 1.7, z0], "lamp_a", L1);
    A.box([s * 1.15 - 0.06, y1, z0 + 0.05], [s * 1.15 + 0.06, y1 + 0.12, z0 + 0.18], "beacon_b", L1);
  }
  for (const z of [-4.1, 1.6]) A.box([-0.8, y1, z - 0.1], [0.8, y1 + 0.25, z + 0.1], "alu", { k: 0, bv: 0.01 });
  for (const x of [-0.45, 0.45]) A.box([x - 0.04, y1 + 0.25, -4.6], [x + 0.04, y1 + 0.38, 4.1], "alu_lt", K0);
  for (let z = -4.4; z < 4.1; z += 0.35) A.box([-0.42, y1 + 0.29, z - 0.02], [0.42, y1 + 0.33, z + 0.02], "alu_lt", L1);
  // chassis, wheels, colliders
  for (const s of [-1, 1]) A.box([s * 0.5 - 0.08, 0.45, -4.6], [s * 0.5 + 0.08, 0.75, 4.4], "steel_blk", K0);
  for (const s of [-1, 1]) A.box([s > 0 ? hx - 0.12 : -hx - 0.02, 0.4, wr + 0.65], [s > 0 ? hx + 0.02 : -hx + 0.12, 0.75, wf - 0.65], "steel_blk", K0);
  const wx = hx - tw / 2 - 0.03;
  vwheel(A, wx, yC, wf, Dw, tw, 1); vwheel(A, -wx, yC, wf, Dw, tw, -1);
  vwheel(A, wx, yC, wr, Dw, tw, 1, true); vwheel(A, -wx, yC, wr, Dw, tw, -1, true);
  A.box([-hx, 0.4, z0], [hx, y1, z1], paint, { v: 0 });
  A.box([-hx, 0.4, zcab], [hx, 3.0, zf], paint, { v: 0 });
  setNav(A, { effect: "carve", rooms: [], doors: [], entrances: [], corners: [], firingPoints: [], cover: vcover(9.4, 2.5, 2.9), areas: [] });
  return A;
}

// ============================================================================ export + self-check
function check(A) {
  const used = new Set(), walk = (ps) => { for (const p of ps) used.add(p.m); };
  walk(A.parts); for (const f of A.furniture) walk(f.parts); for (const it of A.interactables) for (const L of it.leaves) walk(L.parts);
  for (const n of used) {
    const m = A.materials.find((q) => q.name === n);
    if (!m) throw new Error(`${A.meta.id}: unknown material ${n}`);
    if (m.pattern === "painted" || /^paint_metal/.test(m.texture ?? "")) throw new Error(`${A.meta.id}: material ${n} uses ${m.pattern} / ${m.texture}`);
  }
  if (A.parts.length > 1500) throw new Error(`${A.meta.id}: ${A.parts.length} parts`);
  return A;
}
export function newtownAssetsB() {
  return [hotel(), parkingGarage(), clockTower(), shopSmall(), shopRow(), cafe(), carDealership(), motel(), fireStation(), municipal(), coveredPassage(), kiosk(), ambulance(), fireEngine()].map(tdMerge).map(check);
}
