// MOD1 "new town" batch A (docs/modern-assets-brief.md): a modern military base and an industrial / rail estate.
// Military: a two-storey barracks, a reinforced armory with an issue counter and weapon cages, a two-storey HQ with a roof
// door, an open-fronted motor pool shed with a workshop, an 18 m lattice radar tower with caged switchback stairs to two
// platforms, precast compound wall bays (plain, breached, with a culvert), the main gate with a guard booth and lifting
// barriers, and obstacle-course pieces. Industrial: a portal-frame warehouse with a loading dock and an office mezzanine, a
// small workshop unit, a bunded storage tank, track, a buffer stop, a boxcar and a loaded flatcar.
// Footprints are fixed (the map generator is written against them). Written to content/assets/<id>.json by
// tools/build_mpu_assets.mjs (integrator). Units metres, +y up, front = +z.
//
// The enterable buildings use the catalog_modern_buildings_b.mjs wing framework (two-layer walls with real openings,
// ribbon windows as window interactables, aluminium / steel doors, sectional roller doors, switchback stair cores with an
// optional roof bulkhead, double-loaded floor plans, nav rooms with roomTypes). Those helpers are not exported from that
// catalog, so the block between the two "framework" rules below is a copy of them (two local changes: roller() takes
// `open` for a door that starts open, and door aprons are 0.9 m deep so they stay inside the fixed footprints).
// shortcut: copied framework; dedupe by exporting it from catalog_modern_buildings_b.mjs when that file is next touched.
// Material rule (MOD1 review): "painted" is the painted-wood-plank shader, so painted steel is "metal", flat colour is
// "plain", and no paint_metal_* set is used (they carry rust). Signs are blank coloured panels.
import { Asset, cutRect, wbox, P, swingSign } from "./kit.mjs";
import { wall, partition, hinged, aabb, fbox, railing } from "./parts.mjs";
import { mats as baseMats } from "./catalog_chateau.mjs";
import { cover, fire, room, corners, setNav, setTour } from "./catalog_atlantic.mjs";

// ============================================================================ framework (copied, see above)
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
  A._nav = { effect: "block", rooms: [], doors: [], entrances: [], corners: [], firingPoints: [], cover: [], areas: [] };
  A.meta.roomTypes = {};
  return A;
}
function finish(A, cuts = null) { as1Run(A); setNav(A, A._nav, cuts); delete A._nav; return A; }
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
function ribbon(A, F, { id, a0, a1, y0, y1, unit = 3.6, floor, fin = 0.16, finMat = "alu", glass = "glass_t" }) {
  const n = Math.max(1, Math.round((a1 - a0) / unit)), uw = (a1 - a0) / n, mw = 0.07, g0 = 0.10, g1 = 0.12;
  WB(A, F, a0, a1, y1 - 0.08, y1, 0.04, 0.2, "alu", K0);                                // head
  WB(A, F, a0, a1, y0, y0 + 0.08, 0.04, 0.2, "alu");                                    // sill frame
  WB(A, F, a0 - 0.03, a1 + 0.03, y0 - 0.05, y0 + 0.01, -0.06, 0.04, "alu", L1);        // pressed sill flashing
  as1(A, () => WB(A, F, a0 - 0.03, a1 + 0.03, y1 - 0.012, y1 + 0.035, -0.045, 0.03, "alu_lt", L1), true);   // AS1: head drip
  const mull = (a, w) => { WB(A, F, a - w / 2, a + w / 2, y0 + 0.08, y1 - 0.08, 0.04, 0.2, "alu"); if (fin) WB(A, F, a - 0.025, a + 0.025, y0, y1, -fin, 0.04, finMat, L1); };
  for (let i = 0; i < n; i++) {
    const ua0 = a0 + i * uw, ua1 = ua0 + uw;
    mull(i === 0 ? ua0 + mw / 2 : ua0, mw);
    const right = i === n - 1 ? ua1 - mw : ua1 - mw / 2;
    const sw = uw > 1.8 ? Math.min(1.1, uw * 0.45) : right - (i === 0 ? ua0 + mw : ua0 + mw / 2);
    const s1 = right, s0 = right - sw;
    if (uw > 1.8) {
      const f0 = i === 0 ? ua0 + mw : ua0 + mw / 2;
      WB(A, F, s0 - mw, s0, y0 + 0.08, y1 - 0.08, 0.05, 0.18, "alu");                   // sash mullion
      WB(A, F, f0, s0 - mw, y0 + 0.08, y1 - 0.08, g0, g1, glass);                       // fixed light
    }
    const L = { parts: [] }, T = { into: L.parts, k: 0 };
    WB(A, F, s0, s1, y0 + 0.08, y1 - 0.08, g0, g1, glass, T);
    WB(A, F, s0, s1, y0 + 0.08, y0 + 0.16, 0.085, 0.135, "alu", T);                     // sash bottom rail
    WB(A, F, s0 + 0.04, s0 + 0.06, (y0 + y1) / 2 - 0.1, (y0 + y1) / 2 + 0.1, 0.135, 0.16, "alu_lt", T);   // handle
    WB(A, F, s0, s1, y0 + 0.08, y1 - 0.08, 0.085, 0.135, "alu", { into: L.parts, v: 0 });
    L.hinge = P(F.axis, s1, 0, D(F, 0.135));
    L.sign = swingSign(F.axis, P(F.axis, (s0 + s1) / 2, 0, D(F, 0.11)), L.hinge, -F.out);
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
function roller(A, F, { id, c, floor, w = 3.6, h = 4.2, open = false }) {
  const a0 = c - w / 2, a1 = c + w / 2, top = floor + h, ld0 = F.inner + F.out * 0.05, ld1 = F.inner + F.out * 0.11;
  WB(A, F, a0 - 0.1, a0 + 0.012, floor, top - 0.012, 0.1, 0.36, "safety_yel"); WB(A, F, a1 - 0.012, a1 + 0.1, floor, top - 0.012, 0.1, 0.36, "safety_yel");
  WB(A, F, a0 - 0.1, a1 + 0.1, top - 0.012, top + 0.1, 0.1, 0.36, "panel_dk");
  WB(A, F, a0 - 0.1, a1 + 0.1, top + 0.15, top + 0.6, 0.3, 0.7, "panel_dk", L1);           // drum housing (inside)
  WB(A, F, a0 - 0.45, a0 - 0.1, floor - 0.2, top + 0.1, -0.45, 0, "rubber", L1);           // dock shelter
  WB(A, F, a1 + 0.1, a1 + 0.45, floor - 0.2, top + 0.1, -0.45, 0, "rubber", L1);
  WB(A, F, a0 - 0.45, a1 + 0.45, top + 0.1, top + 0.6, -0.45, 0, "rubber", L1);
  WB(A, F, a0 - 0.06, a1 + 0.06, floor - 0.04, floor + 0.008, -0.3, 0.6, "steel_plate");           // dock leveller lip
  const axis = F.axis === "x" ? "x" : "z", hp = P(F.axis, c, top, ld0);
  hinged(A, { id, kind: "door", label: "roller door", sound: "steel", axis, openAngle: 88, duration: 2.4, defaultOpen: open,
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
          if (d.apron !== false && y < 0.6) { const hw = (d.w ?? 1) / 2 + 0.6; fbox(A, F, d.c - hw, d.c + hw, -0.2, y - 0.03, 0, 0.9, "paving", { bv: 0.015 }); }
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
      G.forEach((g, i) => H.push(ribbon(A, F, { id: `${w.tag}${s}${f}_${i}`, a0: g[0], a1: g[1], y0: y + g[2], y1: y + g[3], unit: g[4] ?? w.unit ?? 3.6, floor: y, fin: w.fin ?? 0.16, finMat: w.finMat, glass: w.glass })));
      wall(A, { axis: F.axis, outer: F.outer, inner: F.inner, aOut: F.aOut, aIn: F.aIn, y0: yb, y1: yt, holes: H, stone: w.skin(f, s), plaster: w.plaster ?? "plaster", pt: PT });
      (w._h[s] ??= []).push(...H.map((q) => ({ ...q })));
      if (f > 0 && w.band) {
        const [a0, a1] = F.axis === "x" ? [x0 - 0.045, x1 + 0.045] : [z0 + 0.005, z1 - 0.005];
        fbox(A, F, a0, a1, y - SLAB - 0.06, y + 0.04, -0.005, 0.045, w.band, { k: 0, lod: 1, bv: 0.012 });
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
      wbox(A, F.axis, p0, p1, top, top + w.par, F.inner, F.outer, w.parMat ?? w.skin(n - 1, s));
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
  return w;
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
function canopy(A, { x0, x1, z0, z1, y, cols = [], m = "panel_lt", fascia = "panel_acc" }) {
  A.box([x0, y, z0], [x1, y + 0.22, z1], m, { bv: 0.02 });
  A.box([x0 - 0.03, y - 0.08, z1 - 0.06], [x1 + 0.03, y + 0.32, z1 + 0.03], fascia, { k: 0, bv: 0.01 });
  for (let x = x0 + 1.2; x < x1 - 0.8; x += 2.4) A.box([x - 0.5, y - 0.012, (z0 + z1) / 2 - 0.1], [x + 0.5, y - 0.002, (z0 + z1) / 2 + 0.1], "light_panel", L1);
  for (const [cx, cz] of cols) { A.cyl([cx, y / 2, cz], [0.22, y, 0.22], "alu_lt", { seg: 12 }); A.cyl([cx, 0.03, cz], [0.4, 0.06, 0.4], "alu", { k: 0, seg: 12 }); }
}
function tour(A, start, steps) { setTour(A, { pos: start, yaw: 0 }, steps); }
function doorTour(A, id, x, zOut, zIn, gf, shot) {
  tour(A, [x, 0, zOut + 7], [{ goto: [x, zOut + 1.2], note: "approach the entrance" }, { interact: id, aim: [x + 0.3, gf + 1.1, zIn + 0.02], expectOpen: true },
    { goto: [x, zIn - 2.0], expectY: gf, note: "into the lobby" }, { look: [x, gf + 1.4, zIn - 8], hold: 0.5 }, { shot }]);
}

// ============================================================================ end of framework: newtown helpers
const OLIVE = [0.20, 0.25, 0.14], IND = [0.08, 0.22, 0.42], RED = [0.45, 0.08, 0.05];
const DEG = 180 / Math.PI;
const H0 = { v: 0 };   // collider only

function ntMats(A) {
  // painted steel uses the "metal" pattern (clean paint, no rust sets); concrete / timber / fabric their textures
  A.mat("slab", [0.55, 0.54, 0.50], 0.88, "concrete", { texture: "concrete_stained" });
  A.mat("rconc", [0.52, 0.51, 0.48], 0.9, "concrete", { texture: "concrete_formwork" });
  A.mat("conc_fw", [0.62, 0.61, 0.58], 0.9, "concrete", { texture: "concrete_formwork" });
  A.mat("conc_fw2", [0.57, 0.56, 0.53], 0.9, "concrete", { texture: "concrete_formwork" });
  A.mat("conc_ag", [0.60, 0.58, 0.54], 0.92, "concrete", { texture: "concrete_aggregate" });
  A.mat("conc_rough", [0.50, 0.49, 0.46], 0.95, "concrete", { texture: "concrete_rough" });
  A.mat("conc_break", [0.38, 0.37, 0.35], 0.97, "concrete", { texture: "concrete_rough" });
  A.mat("galv", [0.60, 0.62, 0.63], 0.45, "metal", { texture: "steel_scratched", metallic: 0.6 });
  A.mat("chequer", [0.50, 0.51, 0.52], 0.45, "metal", { texture: "diamond_plate", metallic: 0.7 });
  A.mat("grating", [0.30, 0.31, 0.32], 0.5, "metal", { texture: "steel_scratched", metallic: 0.6 });
  A.mat("steel_dk", [0.14, 0.15, 0.16], 0.5, "metal", { metallic: 0.4 });
  A.mat("olive", [0.24, 0.27, 0.17], 0.65, "metal");
  A.mat("sand_paint", [0.58, 0.52, 0.40], 0.7, "metal");
  A.mat("red_paint", [0.52, 0.07, 0.05], 0.55, "metal");
  A.mat("blue_paint", [0.08, 0.20, 0.42], 0.55, "metal");
  A.mat("wire", [0.50, 0.51, 0.52], 0.5, "metal", { metallic: 0.6 });
  A.mat("rebar", [0.42, 0.27, 0.18], 0.8, "metal", { texture: "steel_rust_light", metallic: 0.3 });
  A.mat("rubble", [0.60, 0.58, 0.55], 1.0, "stone", { texture: "rubble_grey" });
  A.mat("timber", [0.60, 0.50, 0.38], 0.85, "wood", { texture: "planks_038" });
  A.mat("timber_raw", [0.74, 0.62, 0.46], 0.88, "wood", { texture: "planks_raw" });
  A.mat("post_wood", [0.42, 0.33, 0.24], 0.9, "wood", { texture: "wood_dark" });
  A.mat("rope", [0.52, 0.44, 0.30], 0.95, "fabric", { texture: "canvas_rough" });
  A.mat("netting", [0.20, 0.22, 0.14], 0.95, "fabric", { texture: "canvas_olive" });
  A.mat("radome", [0.86, 0.86, 0.84], 0.5, "plain");
  A.mat("line_yel", [0.85, 0.65, 0.08], 0.7, "plain");
  A.mat("line_w", [0.85, 0.85, 0.82], 0.7, "plain");
  A.mat("line_blk", [0.03, 0.03, 0.03], 0.7, "plain");
  A.mat("tank_paint", [0.78, 0.78, 0.75], 0.55, "metal", { metallic: 0.2 });
  A.mat("car_oxide", [0.32, 0.12, 0.07], 0.75, "metal", { metallic: 0.15 });
  A.mat("car_grey", [0.36, 0.37, 0.38], 0.7, "metal", { metallic: 0.2 });
  A.mat("car_inner", [0.10, 0.08, 0.06], 0.9, "wood", { texture: "planks_raw" });
  A.mat("bogie", [0.10, 0.10, 0.10], 0.7, "metal", { texture: "iron_worn", metallic: 0.4 });
  A.mat("wheel_steel", [0.38, 0.36, 0.34], 0.5, "metal", { texture: "steel_rust_light", metallic: 0.8 });
  A.mat("sleeper", [0.56, 0.55, 0.52], 0.9, "concrete", { texture: "concrete_aged" });
  A.mat("spool_wood", [0.55, 0.43, 0.30], 0.85, "wood", { texture: "wood_oak" });
  A.mat("cable_blk", [0.04, 0.04, 0.045], 0.7, "plain");
  A.mat("strap", [0.85, 0.40, 0.05], 0.6, "fabric");
  A.mat("tarp", [0.18, 0.28, 0.20], 0.9, "fabric", { texture: "canvas_olive" });
  A.mat("dirt", [0.50, 0.43, 0.34], 1.0, "plain", { texture: "dirt_compacted" });
  A.mat("sand", [0.62, 0.54, 0.42], 1.0, "plain", { texture: "sand_dirty" });
  A.mat("rack_or", [0.80, 0.35, 0.06], 0.55, "metal");
  A.mat("rack_bl", [0.08, 0.20, 0.42], 0.55, "metal");
  A.mat("pallet", [0.62, 0.52, 0.38], 0.9, "wood", { texture: "planks_raw" });
  A.mat("carton", [0.55, 0.42, 0.28], 0.95, "plain");
  A.mat("shrink", [0.70, 0.72, 0.72], 0.25, "glass");
  A.mat("lamp_r", [0.6, 0.05, 0.03], 0.3, "plain", { emissive: [1.6, 0.1, 0.05] });
  A.mat("lamp_w", [0.9, 0.9, 0.85], 0.2, "plain", { emissive: [2.0, 1.95, 1.8] });
}
function mkN(id, name, description, floors, footprint, accent) { const A = mk(id, name, description, floors, footprint, accent); ntMats(A); return A; }
function mkP(id, name, cat, description, footprint, o = {}) {
  const A = new Asset({ id, version: 1, name, category: `Modern: ${cat}`, description, footprint, floors: 0, enterable: false, plinthDepth: 0, maxGrade: 0.5, noAutoPanels: true, ...o });
  modMats(A, OLIVE); ntMats(A);
  return A;
}
// drop the shared palette entries an asset never uses
function prune(A) {
  const used = new Set(), walk = (ps) => { for (const p of ps ?? []) used.add(p.m); };
  walk(A.parts); for (const f of A.furniture) walk(f.parts); for (const it of A.interactables) for (const L of it.leaves ?? []) walk(L.parts);
  for (const m of used) if (!A.materials.some((q) => q.name === m)) throw new Error(`${A.meta.id}: unknown material ${m}`);
  A.materials = A.materials.filter((q) => used.has(q.name));
}
function done(A, cuts = null) { as1Run(A); prune(A); return finish(A, cuts); }
function pdone(A, nav = {}) {
  prune(A);
  const { cuts, ...n } = nav;
  setNav(A, { effect: "carve", rooms: [], doors: [], entrances: [], corners: [], firingPoints: [], cover: [], areas: [], ...n }, cuts || null);
  return A;
}
const wins = (cs, w = 1.6, s = 0.9, t = 2.4) => cs.map((c) => [c - w / 2, c + w / 2, s, t, w]);

// geometry helpers (as catalog_modern_props.mjs)
const lerp3 = (a, b, t) => [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];
const aimY = (d) => { const L = Math.hypot(d[0], d[1], d[2]); return [Math.acos(Math.max(-1, Math.min(1, d[1] / L))) * DEG, Math.atan2(d[0], d[2]) * DEG, 0]; };
function rod(A, a, b, dia, m, o = {}) {
  const d = [b[0] - a[0], b[1] - a[1], b[2] - a[2]], L = Math.hypot(...d);
  if (L < 1e-4) return null;
  return A.cyl(lerp3(a, b, 0.5), [dia, L, dia], m, { k: 0, lod: 1, seg: 8, ...o, r: aimY(d) });
}
function bar(A, a, b, w, t, m, o = {}) {
  const d = [b[0] - a[0], b[1] - a[1], b[2] - a[2]], L = Math.hypot(...d);
  if (L < 1e-4) return null;
  const yaw = Math.atan2(d[0], d[2]) * DEG, pitch = -Math.asin(Math.max(-1, Math.min(1, d[1] / L))) * DEG;
  return A.obox(lerp3(a, b, 0.5), [w, t, L], [pitch, yaw, 0], m, o);
}
function extrudeX(A, x0, x1, poly, m, o = {}) { for (let i = 1; i + 1 < poly.length; i++) A.prism(0, x0, x1, [poly[0], poly[i], poly[i + 1]], m, o); }
function extrudeZ(A, z0, z1, poly, m, o = {}) { for (let i = 1; i + 1 < poly.length; i++) A.prism(2, z0, z1, [poly[0], poly[i], poly[i + 1]], m, o); }
function boxCover(hx, hz, h, { step = 1.6, ends = true, off = 0.55, cx = 0, cz = 0 } = {}) {
  const ht = h >= 1.35 ? "high" : "low", out = [];
  const pk = (edge) => (ht === "high" ? edge : ["over", ...edge]);
  const along = (half) => { const n = Math.max(1, Math.round((2 * half) / step)); return Array.from({ length: n }, (_, i) => -half + ((i + 0.5) * 2 * half) / n); };
  for (const x of along(hx)) {
    const e = Math.abs(x) > hx - step * 0.6 ? [x > 0 ? "right" : "left"] : [];
    out.push(cover([cx + x, 0, cz - hz - off], 0, ht, pk(ht === "high" ? (e.length ? e : ["left", "right"]) : e)));
    out.push(cover([cx + x, 0, cz + hz + off], 180, ht, pk(ht === "high" ? (e.length ? e : ["left", "right"]) : e)));
  }
  if (ends && hz > 0.35) { out.push(cover([cx - hx - off, 0, cz], 90, ht, pk(["left", "right"]))); out.push(cover([cx + hx + off, 0, cz], 270, ht, pk(["left", "right"]))); }
  return out;
}
// galvanised mesh panel in the plane d of axis ("x": runs along x at z = d), welded square mesh as thin bars, one collider
function meshPanel(A, axis, a0, a1, d, y0, y1, { sp = 0.15, frame = "galv", collide = true } = {}) {
  const B = (p0, p1, q0, q1, e, m, o) => wbox(A, axis, p0, p1, q0, q1, d - e, d + e, m, o);
  B(a0, a1, y1 - 0.04, y1, 0.02, frame, K0); B(a0, a1, y0, y0 + 0.04, 0.02, frame, K0);
  for (const a of [a0, a1]) B(a - 0.02, a + 0.02, y0, y1, 0.02, frame, K0);
  for (let a = a0 + sp; a < a1 - 0.03; a += sp) B(a - 0.004, a + 0.004, y0 + 0.04, y1 - 0.04, 0.004, "wire", L1);
  for (let y = y0 + sp; y < y1 - 0.03; y += sp) B(a0 + 0.02, a1 - 0.02, y - 0.004, y + 0.004, 0.007, "wire", L1);
  if (collide) B(a0, a1, y0, y1, 0.02, "wire", H0);
}
// level guard rail: posts, top / mid rail, toe board and a 1.1 m collider (axis as wbox)
function guard(A, axis, a0, a1, d, y, { m = "safety_yel", h = 1.1, posts = 1.4 } = {}) {
  const B = (p0, p1, q0, q1, e0, e1, mm, o) => wbox(A, axis, p0, p1, q0, q1, d + e0, d + e1, mm, o);
  B(a0, a1, y + h - 0.05, y + h, -0.025, 0.025, m, K0);
  B(a0, a1, y + 0.5, y + 0.54, -0.015, 0.015, m, L1);
  B(a0, a1, y, y + 0.12, -0.006, 0.006, m, L1);
  const n = Math.max(1, Math.round((a1 - a0) / posts));
  for (let i = 0; i <= n; i++) { const a = a0 + ((a1 - a0) * i) / n; B(a - 0.02, a + 0.02, y, y + h, -0.02, 0.02, m, L1); }
  B(a0, a1, y, y + h, -0.03, 0.03, m, H0);
}
// sloped handrail with posts along a stair (two end points at walking level), collider below the rail
function stairRail(A, p0, p1, m = "safety_yel", collide = true) {
  bar(A, [p0[0], p0[1] + 0.95, p0[2]], [p1[0], p1[1] + 0.95, p1[2]], 0.045, 0.045, m, K0);
  bar(A, [p0[0], p0[1] + 0.5, p0[2]], [p1[0], p1[1] + 0.5, p1[2]], 0.03, 0.03, m, L1);
  for (let i = 0; i <= 3; i++) { const p = lerp3(p0, p1, i / 3); A.box([p[0] - 0.02, p[1], p[2] - 0.02], [p[0] + 0.02, p[1] + 0.95, p[2] + 0.02], m, L1); }
  if (collide) bar(A, [p0[0], p0[1] + 0.55, p0[2]], [p1[0], p1[1] + 0.55, p1[2]], 0.05, 1.1, m, H0);
}
// wall-mounted bulkhead light / camera / blank sign on a face frame F at (a, y)
function wlamp(A, F, a, y) { fbox(A, F, a - 0.12, a + 0.12, y, y + 0.16, 0, 0.1, "alu", L1); fbox(A, F, a - 0.1, a + 0.1, y + 0.02, y + 0.14, 0.1, 0.12, "lamp_w", L1); }
function camera(A, F, a, y) { fbox(A, F, a - 0.04, a + 0.04, y - 0.05, y + 0.05, 0, 0.25, "paint_w", L1); fbox(A, F, a - 0.08, a + 0.08, y - 0.08, y + 0.04, 0.2, 0.5, "paint_w", L1); fbox(A, F, a - 0.05, a + 0.05, y - 0.06, y + 0.02, 0.5, 0.52, "screen", L1); }
// window bars outside a window on frame F
function bars(A, F, a0, a1, y0, y1, n = 6) {
  for (let i = 1; i < n; i++) { const a = a0 + ((a1 - a0) * i) / n; fbox(A, F, a - 0.012, a + 0.012, y0 - 0.06, y1 + 0.06, 0.035, 0.06, "steel_dk", K0); }
  for (const y of [y0 + 0.06, (y0 + y1) / 2, y1 - 0.06]) fbox(A, F, a0 - 0.08, a1 + 0.08, y - 0.02, y + 0.02, 0.03, 0.065, "steel_dk", K0);
  for (const a of [a0 - 0.08, a1 + 0.05]) fbox(A, F, a, a + 0.03, y0 - 0.08, y1 + 0.08, 0, 0.065, "steel_dk", L1);
  fbox(A, F, a0, a1, y0, y1, 0.03, 0.065, "steel_dk", H0);
}
// steel I-section column from y0 to y1 at (x, z), flanges facing ±z
function ucol(A, x, z, y0, y1, m = "olive", s = 0.26) {
  A.box([x - s / 2, y0, z - s / 2], [x + s / 2, y1, z - s / 2 + 0.02], m);
  A.box([x - s / 2, y0, z + s / 2 - 0.02], [x + s / 2, y1, z + s / 2], m);
  A.box([x - 0.006, y0, z - s / 2 + 0.02], [x + 0.006, y1, z + s / 2 - 0.02], m);
  A.box([x - s / 2 - 0.08, y0, z - s / 2 - 0.08], [x + s / 2 + 0.08, y0 + 0.025, z + s / 2 + 0.08], "steel_dk", L1);   // base plate
  for (const dx of [-1, 1]) for (const dz of [-1, 1]) A.cyl([x + dx * (s / 2 + 0.04), y0 + 0.04, z + dz * (s / 2 + 0.04)], [0.03, 0.04, 0.03], "steel_dk", { k: 0, lod: 1, n: 6 });
}
function apron(A, F, c, w, depth = 0.9, y = GF) { fbox(A, F, c - w / 2 - 0.6, c + w / 2 + 0.6, -0.2, y - 0.03, 0, depth, "paving", { bv: 0.015 }); }

// ============================================================================ MILITARY: barracks
export function barracks() {
  const A = mkN("mod_nt_barracks", "Barracks block", "Two-storey modern barracks, 30 x 11 m: grey brick ground floor and precast panel upper floor with a dark floor band, many punched windows (each an opening sash), a glazed double front door under a steel canopy, a rear door, a central switchback stair core and a central corridor on both floors. Ground floor: entry hall, day room, washroom and five dormitories; upper floor: washroom and eight dormitories. Flat roof with parapet, plant and a small mast.",
    2, [30, 12], OLIVE);
  const h = 3.3, ys = [GF, GF + h], zc = [-0.9, 0.7], Q = "mil_quarters";
  const core = mkCore({ xa: -1.31, zA: -1.02, dir: -1, ys, encL: true, encR: true, encTo: -5.2, tag: "core" });
  const side = [[-0.55, 0.55, 0.9, 2.4, 1.1], [2.2, 3.8, 0.9, 2.4, 1.6], [-4.0, -2.4, 0.9, 2.4, 1.6]];
  const w = wing(A, {
    tag: "b", x0: -15, x1: 15, z0: -5.5, z1: 5.5, n: 2, h, cores: [core], band: "panel_dk", skin: (f) => (f === 0 ? "brick_grey" : "precast"),
    doors: [{ id: "front_door", side: "front", c: 0, w: 1.8, h: 2.3, double: true, glass: true, room: "g_entry" },
      { id: "rear_door", side: "rear", c: 2.75, w: 1.0, h: 2.2, room: "g_rear_hall" }],
    glaze: (f) => (f === 0 ? {
      front: [...wins([-12.5, -8.5, -4.5]), [3.5, 6.5, 1.6, 2.4, 1.5], ...wins([10.5, 13.0])],
      rear: wins([-12.8, -9.9, -4.7, 6.5, 11.0, 13.3]), west: side, east: side,
    } : {
      front: [...wins([-12.8, -9.9, -5.0]), [-0.9, 1.9, 1.6, 2.4, 1.4], ...wins([6.0, 11.0, 13.3])],
      rear: wins([-12.8, -9.9, -4.7, 3.5, 6.2, 10.5, 13.0]), west: side, east: side,
    }),
    plan: (f, { w }) => (f === 0 ? dl(w, { pre: "g_", zc,
      front: [{ x1: -2, id: "dayroom", type: "sitting_modern", dc: -3.0 }, { x1: 2, id: "entry", type: "lobby", open: 1 }, { x1: 8, id: "washroom", type: "bathroom_modern" }, { id: "dorm_1", type: Q }],
      rear: [{ x1: -8, id: "dorm_2", type: Q }, { x1: -1.46, id: "dorm_3", type: Q }, { x1: 1.46, skip: 1 }, { x1: 4, id: "rear_hall", type: "hall", open: 1 }, { x1: 9, id: "dorm_4", type: Q }, { id: "dorm_5", type: Q }] })
      : dl(w, { pre: "f1_", zc,
        front: [{ x1: -8, id: "dorm_6", type: Q }, { x1: -2, id: "dorm_7", type: Q }, { x1: 3, id: "washroom", type: "bathroom_modern" }, { x1: 9, id: "dorm_8", type: Q }, { id: "dorm_9", type: Q }],
        rear: [{ x1: -8, id: "dorm_10", type: Q }, { x1: -1.46, id: "dorm_11", type: Q }, { x1: 1.46, skip: 1 }, { x1: 8, id: "dorm_12", type: Q }, { id: "dorm_13", type: Q }] })),
    roof: (A, w) => {
      hvac(A, -9, 2.5, w.top); hvac(A, 9, 2.5, w.top);
      for (const x of [-5, 5]) { A.cyl([x, w.top + 0.5, -3.5], [0.45, 1.0, 0.45], "louvre", { seg: 12 }); A.cone([x, w.top + 1.1, -3.5], [0.65, 0.2, 0.65], "alu", { k: 0, top: 0.1, seg: 12 }); }
      A.cyl([12.5, w.top + 2.0, -4.0], [0.08, 4.0, 0.08], "galv", { k: 0, seg: 8 });   // small comms mast
      for (const a of [0, 120, 240]) rod(A, [12.5, w.top + 3.6, -4.0], [12.5 + 1.6 * Math.sin(a / DEG), w.top + 0.05, -4.0 + 1.6 * Math.cos(a / DEG)], 0.008, "wire", { seg: 4 });
      A.box([12.0, w.top, -4.5], [13.0, w.top + 0.1, -3.5], "concrete_wash", { bv: 0.01 });
    },
    dress: (A, w) => {
      const Ff = sideF(w, "front"), Fr = sideF(w, "rear");
      canopy(A, { x0: -2.0, x1: 2.0, z0: 5.5, z1: 6.4, y: 2.75, m: "panel_lt", fascia: "panel_acc" });
      fbox(A, Ff, -2.4, -1.4, 1.2, 1.9, 0, 0.04, "sign_blank", L1);
      wlamp(A, Fr, 2.75, 2.65); camera(A, Ff, 2.6, 3.0); camera(A, Fr, -14.6, 6.2);
      for (const F of [Ff, Fr]) for (const a of [-14.75, 14.75]) fbox(A, F, a - 0.06, a + 0.06, 0, w.top + 0.9, 0, 0.12, "alu", L1);   // downpipes
      for (const a of [-7, 7]) { fbox(A, Fr, a - 0.45, a + 0.45, 2.2, 2.75, 0, 0.3, "hvac", L1); fbox(A, Fr, a - 0.35, a + 0.35, 2.3, 2.65, 0.3, 0.31, "louvre", L1); }   // split AC units
      for (const s of SIDES) { const G = sideF(w, s), isX = G.axis === "x"; fbox(A, G, isX ? -15.02 : -5.495, isX ? 15.02 : 5.495, -0.3, GF + 0.15, -0.012, 0.03, "plinth", { k: 0, bv: 0.01 }); }
      A.box([1.6, 0, 6.0], [4.6, 0.5, 6.4], "concrete_wash", { bv: 0.03 });   // planter kerb by the door
      A.box([1.7, 0.4, 6.05], [4.5, 0.48, 6.35], "soil", L1);
    },
  });
  buildCore(A, core, w.top - SLAB, ["g_corridor", "f1_corridor"]);
  coverRing(A, -15, 15, -5.5, 5.5);
  doorTour(A, "front_door", 0, 5.5, 5.2, GF, "mod_nt_barracks_entry");
  return done(A);
}

// ============================================================================ MILITARY: armory
export function armory() {
  const A = mkN("mod_nt_armory", "Reinforced armory", "Single-storey reinforced armory, 15.2 x 10 m: 0.4 m cast concrete walls on a formwork-textured finish, thick roof slab with a low parapet, steel double front door and a steel rear loading door, small barred high windows. Inside: an issue room with a fixed steel-and-laminate issue counter under a mesh security screen with a lift-up flap, two lockable weapon cages of welded mesh with rifle racks behind the counter, and two ammunition stores behind steel doors. Blast bollards, warning panels, lights and cameras outside.",
    1, [16, 12], RED);
  const x0 = -7.6, x1 = 7.6, z0 = -5, z1 = 5, h = 3.6;
  const sideWins = [[2.4, 3.4, 1.5, 2.3, 1.0], [-3.4, -2.4, 2.3, 2.9, 1.0]];
  const w = wing(A, {
    tag: "a", x0, x1, z0, z1, n: 1, h, T: 0.4, band: null, par: 0.5, parMat: "rconc", skin: () => "rconc", fm: "slab",
    doors: [{ id: "front_door", side: "front", c: 0, w: 1.6, h: 2.2, double: true, room: "issue" },
      { id: "rear_door", side: "rear", c: -4, w: 1.4, h: 2.2, room: "ammo_a" }],
    glaze: () => ({ front: [[-5.6, -4.6, 1.5, 2.3, 1.0], [4.6, 5.6, 1.5, 2.3, 1.0]], east: sideWins, west: sideWins }),
    plan: (f, { w }) => ({
      rooms: [{ id: "issue", type: "office_modern", x0: w.ix0, z0: 0.1, x1: w.ix1, z1: w.iz1 },
        { id: "ammo_a", type: "mil_ammo", x0: w.ix0, z0: w.iz0, x1: -0.1, z1: -0.1 }, { id: "ammo_b", type: "mil_ammo", x0: 0.1, z0: w.iz0, x1: w.ix1, z1: -0.1 }],
      walls: [{ axis: "x", d: 0, a0: w.ix0, a1: w.ix1, t: 0.2, m: "rconc", holes: [{ c: 5.0, w: 1.2, h: 2.2, a: "ammo_b", b: "issue" }] },
        { axis: "z", d: 0, a0: w.iz0, a1: -0.1, t: 0.2, m: "rconc", holes: [{ c: -2.5, w: 1.2, h: 2.2, a: "ammo_a", b: "ammo_b" }] }],
    }),
    roof: (A, w) => {
      for (const x of [-4, 4]) { A.box([x - 0.5, w.top, -2.5], [x + 0.5, w.top + 0.7, -1.5], "louvre", { bv: 0.01 }); A.box([x - 0.6, w.top + 0.7, -2.6], [x + 0.6, w.top + 0.78, -1.4], "alu", K0); }
      A.cyl([6.8, w.top + 1.5, -4.3], [0.03, 3.0, 0.03], "alu_lt", { k: 0, seg: 6 });   // lightning conductor
    },
    dress: (A, w) => {
      const y = GF, Ff = sideF(w, "front"), Fr = sideF(w, "rear"), Fe = sideF(w, "east"), Fw = sideF(w, "west");
      for (const a of [-5.1, 5.1]) bars(A, Ff, a - 0.5, a + 0.5, y + 1.5, y + 2.3);
      for (const F of [Fe, Fw]) { bars(A, F, 2.4, 3.4, y + 1.5, y + 2.3); bars(A, F, -3.4, -2.4, y + 2.3, y + 2.9); }
      // steel door surrounds, canopy, red lamp, warning panels, cameras
      for (const [F, c, wd] of [[Ff, 0, 1.6], [Fr, -4, 1.4]]) {
        fbox(A, F, c - wd / 2 - 0.15, c - wd / 2, 0, y + 2.35, 0, 0.08, "steel_dk", { k: 0, bv: 0.01 });
        fbox(A, F, c + wd / 2, c + wd / 2 + 0.15, 0, y + 2.35, 0, 0.08, "steel_dk", { k: 0, bv: 0.01 });
        fbox(A, F, c - wd / 2 - 0.15, c + wd / 2 + 0.15, y + 2.2, y + 2.35, 0, 0.08, "steel_dk", { k: 0, bv: 0.01 });
        fbox(A, F, c - wd / 2 - 0.6, c + wd / 2 + 0.6, y + 2.7, y + 2.85, 0, 0.9, "steel_dk", { bv: 0.01 });
        fbox(A, F, c - 0.08, c + 0.08, y + 2.9, y + 3.08, 0, 0.12, "lamp_r", L1);
        fbox(A, F, c + wd / 2 + 0.4, c + wd / 2 + 0.9, y + 1.3, y + 1.8, 0, 0.02, "safety_yel", L1);
        fbox(A, F, c + wd / 2 + 0.4, c + wd / 2 + 0.9, y + 0.7, y + 1.2, 0, 0.02, "red_paint", L1);
        fbox(A, F, c + wd / 2 + 0.3, c + wd / 2 + 0.5, y + 0.95, y + 1.15, 0, 0.06, "panel_dk", L1);   // keypad
        camera(A, F, c - wd / 2 - 0.6, y + 3.1);
      }
      for (const F of [Fe, Fw]) for (const a of [-3.8, 3.8]) wlamp(A, F, a, y + 3.0);
      apron(A, Ff, 0, 1.6); apron(A, Fr, -4, 1.4);
      for (const x of [-2.2, 2.2, -4.6, 4.6]) { A.cyl([x, 0.5, 5.75], [0.3, 1.0, 0.3], "safety_yel", { seg: 12 }); A.cyl([x, 1.01, 5.75], [0.32, 0.03, 0.32], "steel_dk", { k: 0, seg: 12 }); }
      for (const s of SIDES) { const G = sideF(w, s), isX = G.axis === "x"; fbox(A, G, isX ? x0 - 0.02 : z0 + 0.005, isX ? x1 + 0.02 : z1 - 0.005, -0.3, y + 0.5, -0.012, 0.04, "plinth", { k: 0, bv: 0.01 }); }
      for (const F of [Ff, Fr]) for (const a of [-6.5, -2, 2, 6.5]) for (const yy of [1.2, 2.6]) fbox(A, F, a - 0.04, a + 0.04, yy - 0.04, yy + 0.04, 0, 0.012, "steel_dk", L1);   // tie-bolt cones
      // interior steel doors on the partition openings
      mdoor(A, { axis: "x", out: 1, outer: 0.1, inner: -0.1, yaw: 0 }, { id: "ammo_b_door", c: 5.0, floor: y, w: 1.2, h: 2.2, depth: 0.2, label: "steel door" });
      mdoor(A, { axis: "z", out: 1, outer: 0.1, inner: -0.1, yaw: 90 }, { id: "ammo_ab_door", c: -2.5, floor: y, w: 1.2, h: 2.2, depth: 0.2, label: "steel door" });
      // issue counter: steel base, laminate top, mesh security screen with a lift-up flap at the staff gap (x 3.9 - 5.1)
      for (const [a, b] of [[w.ix0, 3.9], [5.1, w.ix1]]) {
        A.box([a, y, 2.3], [b, y + 0.95, 2.9], "panel_lt", { bv: 0.01 });
        A.box([a, y, 2.88], [b, y + 0.12, 2.9], "panel_dk", L1);
        A.box([a - 0.01, y + 0.95, 2.25], [b + 0.01, y + 1.0, 2.95], "laminate", { bv: 0.008 });
        meshPanel(A, "x", a + 0.02, b - 0.02, 2.6, y + 1.0, y + 2.6, { sp: 0.1, frame: "steel_dk" });
        for (let x = a + 1.2; x < b - 0.6; x += 2.4) A.box([x - 0.35, y + 1.0, 2.55], [x + 0.35, y + 1.25, 2.65], "steel_dk", K0);   // pass-through slots
      }
      A.obox([3.92, y + 1.5, 2.6], [0.04, 1.0, 0.65], [0, 0, 0], "laminate", K0);   // flap, raised
      for (const x of [3.95, 5.05]) A.box([x - 0.03, y + 1.0, 2.25], [x + 0.03, y + 1.06, 2.35], "steel_dk", L1);
      A.box([-6.8, y + 1.0, 2.3], [-6.3, y + 1.4, 2.7], "panel_dk", L1); A.box([-6.75, y + 1.15, 2.69], [-6.35, y + 1.38, 2.7], "screen", L1);   // issue terminal
      // two weapon cages behind the counter: mesh fronts with open gates, side panels, rifle racks on the back wall
      const cy1 = y + 2.4, cz = 2.0;
      for (const [c0, c1, g0, g1] of [[w.ix0, -3.2, -5.9, -4.9], [-2.8, 1.6, -1.1, -0.1]]) {
        meshPanel(A, "x", c0, g0, cz, y, cy1); meshPanel(A, "x", g1, c1, cz, y, cy1);
        meshPanel(A, "z", 0.12, cz, c1, y, cy1);
        if (c0 > w.ix0 + 0.1) meshPanel(A, "z", 0.12, cz, c0, y, cy1);
        meshPanel(A, "x", c0, c1, cz - 0.95, cy1, cy1 + 0.02, { sp: 0.3, collide: false });   // top tie
        A.obox([g0 + 0.36, y + 1.2, cz + 0.36], [0.04, 2.3, 1.0], [0, -45, 0], "galv", K0);    // gate leaf, swung out
        for (let yy = 0.2; yy < 2.3; yy += 0.15) A.obox([g0 + 0.36, y + yy, cz + 0.36], [0.05, 0.006, 0.98], [0, -45, 0], "wire", L1);
        A.box([g1 - 0.04, y + 1.0, cz - 0.06], [g1, y + 1.15, cz + 0.06], "steel_dk", L1);     // hasp
        for (let x = c0 + 0.3; x < c1 - 0.3; x += 1.0) {                                         // rifle racks
          A.box([x - 0.45, y, 0.12], [x + 0.45, y + 0.12, 0.55], "steel_dk", { bv: 0.005 });
          A.box([x - 0.45, y + 0.12, 0.12], [x + 0.45, y + 0.16, 0.55], "rubber", L1);
          A.box([x - 0.45, y + 1.05, 0.12], [x + 0.45, y + 1.15, 0.3], "steel_dk", { bv: 0.005 });
          for (let k = 0; k < 6; k++) A.box([x - 0.4 + k * 0.16, y + 1.15, 0.12], [x - 0.37 + k * 0.16, y + 1.22, 0.3], "steel_dk", L1);
          for (const xx of [x - 0.45, x + 0.45]) A.box([xx - 0.02, y, 0.12], [xx + 0.02, y + 1.6, 0.16], "steel_dk", L1);
          A.box([x - 0.45, y + 1.5, 0.12], [x + 0.45, y + 1.54, 0.2], "steel_dk", L1);           // locking bar
        }
      }
      // ammo stores: steel shelving runs along the walls (fixed)
      for (const [a, b] of [[-7.0, -0.4], [0.4, 7.0]]) {
        A.box([a, y, -4.6], [b, y + 2.2, -4.0], "steel_dk", { v: 0 });
        for (const yy of [0.1, 0.8, 1.5, 2.15]) A.box([a, y + yy, -4.6], [b, y + yy + 0.04, -4.0], "galv", K0);
        for (let x = a; x <= b + 0.01; x += (b - a) / 4) A.box([x - 0.03, y, -4.6], [x + 0.03, y + 2.2, -4.55], "galv", L1), A.box([x - 0.03, y, -4.05], [x + 0.03, y + 2.2, -4.0], "galv", L1);
      }
      A.box([-7.15, y + 0.4, -1.4], [-7.08, y + 0.9, -0.9], "red_paint", L1);   // fire panel
    },
  });
  coverRing(A, x0, x1, z0, z1);
  doorTour(A, "front_door", 0, z1, z1 - 0.4, GF, "mod_nt_armory_issue");
  return done(A);
}

// ============================================================================ MILITARY: HQ
export function hq() {
  const A = mkN("mod_nt_hq", "Command HQ", "Two-storey command headquarters, 19 x 12 m: white render over a grey brick ground floor, punched windows, a glazed entrance with a canopy, a flagpole and blank sign panel. Inside: lobby, operations room, radio room, offices and store on the ground floor; a command briefing room, a second radio room and offices upstairs off a central corridor; a stair core rising to a roof bulkhead with a door onto the flat roof, which has a parapet, plant, a guyed lattice antenna mast, whip antennas and a dish.",
    2, [20, 14], OLIVE);
  const h = 3.5, ys = [GF, GF + h], roofY = GF + 2 * h, zc = [-0.8, 0.8];
  const core = mkCore({ xa: 6.58, zA: 0.92, ys: [...ys, roofY], encL: true, encR: false, encTo: 5.7, roof: true, tag: "core" });
  const sw = [[2.5, 4.1, 0.9, 2.5, 1.6], [-4.1, -2.5, 0.9, 2.5, 1.6]];
  const w = wing(A, {
    tag: "h", x0: -9.5, x1: 9.5, z0: -6, z1: 6, n: 2, h, cores: [core], band: "panel_acc", skin: (f) => (f === 0 ? "brick_grey" : "render_w"),
    doors: [{ id: "main_entrance", side: "front", c: 0, w: 1.8, h: 2.4, double: true, glass: true, room: "g_lobby" },
      { id: "west_door", side: "west", c: 0, w: 1.0, h: 2.2, room: "g_corridor" }],
    glaze: (f) => (f === 0 ? {
      front: [...wins([-7.5, -4.8], 1.8), [-2.6, -1.2, 0.1, 2.6, 1.4], [1.2, 2.6, 0.1, 2.6, 1.4], ...wins([4.7], 1.8)],
      rear: [...wins([-6.6, -1.0, 7.1], 1.8), [3.0, 4.0, 1.6, 2.5, 1.0]], west: sw, east: [[-4.1, -2.5, 0.9, 2.5, 1.6], [-0.55, 0.55, 0.9, 2.5, 1.1]],
    } : {
      front: wins([-7.5, -4.8, -2.0, 1.8, 4.6], 1.8), rear: wins([-7.4, -4.8, 0, 5.0, 7.6], 1.8),
      west: [...sw, [-0.55, 0.55, 0.9, 2.5, 1.1]], east: [[-4.1, -2.5, 0.9, 2.5, 1.6], [-0.55, 0.55, 0.9, 2.5, 1.1]],
    }),
    plan: (f, { w }) => (f === 0 ? dl(w, { pre: "g_", zc,
      front: [{ x1: -3, id: "ops", type: "mil_command" }, { x1: 3, id: "lobby", type: "lobby", open: 1 }, { x1: 6.43, id: "office_1", type: "office_modern" }, { skip: 1 }],
      rear: [{ x1: -4, id: "office_2", type: "office_modern" }, { x1: 2, id: "radio", type: "mil_radio" }, { x1: 5, id: "store", type: "utility" }, { id: "office_3", type: "office_modern" }] })
      : dl(w, { pre: "f1_", zc,
        front: [{ x1: 0, id: "command", type: "mil_command" }, { x1: 6.43, id: "office_4", type: "office_modern" }, { skip: 1 }],
        rear: [{ x1: -3, id: "radio", type: "mil_radio" }, { x1: 3, id: "office_5", type: "office_modern" }, { id: "office_6", type: "office_modern" }] })),
    roof: (A, w) => {
      const y = w.top;
      hvac(A, -1, -3.8, y, 2.4, 1.4); hvac(A, 2.5, -3.8, y, 2.4, 1.4);
      // guyed square lattice mast, 9 m, on a plinth at the west end
      const mx = -6.5, mz = 2.5, ms = 0.22, mh = 9;
      A.box([mx - 0.5, y, mz - 0.5], [mx + 0.5, y + 0.25, mz + 0.5], "concrete_wash", { bv: 0.02 });
      for (const [dx, dz] of [[-1, -1], [1, -1], [1, 1], [-1, 1]]) A.cyl([mx + dx * ms, y + 0.25 + mh / 2, mz + dz * ms], [0.04, mh, 0.04], "galv", { k: 0, seg: 6 });
      for (let yy = 0.6; yy < mh; yy += 0.6) for (const [a, b] of [[[-1, -1], [1, -1]], [[1, -1], [1, 1]], [[1, 1], [-1, 1]], [[-1, 1], [-1, -1]]])
        rod(A, [mx + a[0] * ms, y + 0.25 + yy, mz + a[1] * ms], [mx + b[0] * ms, y + 0.25 + yy + 0.6, mz + b[1] * ms], 0.015, "galv", { seg: 4 });
      A.box([mx - 0.3, y + 0.25, mz - 0.3], [mx + 0.3, y + 0.25 + mh, mz + 0.3], "galv", H0);
      for (const [gx, gz] of [[-8.6, 5.0], [-8.6, -0.5], [-3.4, 2.5]]) { rod(A, [mx, y + 0.25 + mh * 0.85, mz], [gx, y + 0.1, gz], 0.008, "wire", { seg: 4 }); A.box([gx - 0.12, y, gz - 0.12], [gx + 0.12, y + 0.1, gz + 0.12], "steel_dk", L1); }
      A.cyl([mx, y + mh + 1.2, mz], [0.05, 2.0, 0.05], "galv", { k: 0, seg: 6 });
      for (const a of [0, 90, 180, 270]) A.box([mx - 0.4 * Math.sin(a / DEG) - 0.02, y + mh - 0.6, mz - 0.4 * Math.cos(a / DEG) - 0.02], [mx + 0.02, y + mh - 0.56, mz + 0.02], "galv", L1);
      A.box([mx + ms, y + mh - 1.4, mz - 0.3], [mx + ms + 0.08, y + mh - 0.6, mz + 0.3], "paint_w", L1);   // panel antenna
      for (const [x, z] of [[-3, 4.8], [-1.8, 4.8], [-0.6, 4.8]]) { A.cyl([x, y + 1.5, z], [0.03, 3.0, 0.03], "steel_dk", { k: 0, seg: 6 }); A.box([x - 0.12, y, z - 0.12], [x + 0.12, y + 0.15, z + 0.12], "steel_dk", L1); }   // whips
      A.box([3.4, y, 2.6], [4.6, y + 0.15, 3.8], "concrete_wash", { bv: 0.01 });   // dish on a ballast frame
      A.cyl([4.0, y + 0.6, 3.2], [0.12, 0.9, 0.12], "paint_w", { k: 0, seg: 8 });
      A.cone([4.0, y + 1.35, 3.0], [1.4, 0.35, 1.4], "paint_w", { k: 0, seg: 20, top: 0.85, r: [-60, 0, 0] });
      A.box([3.4, y + 0.15, 2.6], [4.6, y + 1.9, 3.8], "paint_w", H0);
      rod(A, [4.0, y + 1.35, 3.0], [4.0, y + 1.6, 3.9], 0.02, "paint_w");
    },
    dress: (A, w) => {
      const Ff = sideF(w, "front"), Fw = sideF(w, "west"), Fr = sideF(w, "rear");
      canopy(A, { x0: -3.0, x1: 3.0, z0: 6, z1: 6.9, y: 2.95 });
      fbox(A, Ff, -2.6, 2.6, 3.15, 3.65, 0.9, 0.95, "sign_blank", L1);
      A.cyl([-6.5, 4.0, 6.7], [0.1, 8.0, 0.1], "alu_lt", { seg: 10 });   // flagpole and a plain flag
      A.cyl([-6.5, 0.05, 6.7], [0.5, 0.1, 0.5], "concrete_wash", { k: 0, seg: 12 });
      A.box([-6.45, 6.9, 6.69], [-5.0, 7.85, 6.71], "panel_acc", L1);
      A.cyl([-6.5, 8.04, 6.7], [0.14, 0.08, 0.14], "brass", { k: 0, seg: 8 });
      for (const F of [Ff, Fr]) for (const a of [-9.25, 9.25]) fbox(A, F, a - 0.06, a + 0.06, 0, w.top + 0.9, 0, 0.12, "alu", L1);
      wlamp(A, Fw, 0, 2.75); camera(A, Ff, 3.2, 3.4); camera(A, Fr, -9.0, 6.6);
      for (const s of SIDES) { const G = sideF(w, s), isX = G.axis === "x"; fbox(A, G, isX ? -9.52 : -5.995, isX ? 9.52 : 5.995, -0.3, GF + 0.15, -0.012, 0.03, "plinth", { k: 0, bv: 0.01 }); }
      for (const x of [-4.5, 4.5]) { A.box([x - 0.9, 0, 6.15], [x + 0.9, 0.45, 6.85], "concrete_wash", { bv: 0.03 }); A.box([x - 0.82, 0.38, 6.22], [x + 0.82, 0.43, 6.78], "soil", L1); }   // planters (also a vehicle stop)
      for (const a of [-6.5, -3.5, 3.5]) { fbox(A, Fr, a - 0.4, a + 0.4, 0.3, 1.5, 0, 0.35, "hvac", { bv: 0.01 }); fbox(A, Fr, a - 0.3, a + 0.3, 0.45, 1.35, 0.35, 0.36, "louvre", L1); }   // generator / AC units behind
    },
  });
  buildCore(A, core, 0, ["g_corridor", "f1_corridor"]);
  addRoom(A, "roof", "utility", -9.2, roofY, -5.7, 9.2, roofY + 2.5, 5.7);
  navDoor(A, "f1_corridor", "roof", [core.xa + core.len / 2, roofY, core.zF]);
  A._nav.firingPoints.push(fire([-9.0, roofY, 5.4], 0, "firestep"), fire([-3, roofY, 5.4], 0, "firestep"), fire([-9.0, roofY, -5.4], 180, "firestep"), fire([-8.9, roofY, 0], 270, "firestep"));
  A._nav.cover.push(cover([-7, roofY, 5.3], 0, "low", ["over"]), cover([0, roofY, 5.3], 0, "low", ["over"]), cover([-4, roofY, -5.3], 180, "low", ["over"]), cover([-8.9, roofY, -3], 270, "low", ["over"]));
  coverRing(A, -9.5, 9.5, -6, 6);
  doorTour(A, "main_entrance", 0, 6, 5.7, GF, "mod_nt_hq_lobby");
  return done(A);
}

// ============================================================================ MILITARY: motor pool
export function motorPool() {
  const A = mkN("mod_nt_motor_pool", "Motor pool shed", "Open-fronted steel vehicle shed, 24 x 14 m: three 8 m drive-in bays open to the front between I-section columns, a mono-pitch corrugated steel roof (5.6 m at the front falling to 4.4 m) on rafters and purlins with roof lights, clad sides on a precast plinth wall, a concrete floor with painted bay lines, inspection-pit outlines and wheel stops. Along the back a closed block workshop with a door and windows onto the shed, workbenches, and a parts store.",
    1, [24, 14], OLIVE);
  const y0 = 0.15, FE = 5.6, RE = 4.4, zF = 6.85, zR = -6.95, ry = (z) => RE + ((FE - RE) * (z - zR)) / (zF - zR);
  wing(A, {
    tag: "w", x0: -11.8, x1: 11.8, z0: -6.8, z1: -3.05, n: 1, h: 3.4, T: 0.25, par: 0, band: null, skin: () => "brick_grey", fm: "slab",
    doors: [{ id: "workshop_door", side: "front", c: -6, w: 1.0, h: 2.2, room: "workshop", to: "shed", apron: false }],
    glaze: () => ({ front: [[-3.5, -0.5, 1.0, 2.4, 1.5], [2.0, 5.0, 1.0, 2.4, 1.5]], rear: [[-9.5, -7.5, 1.7, 2.6, 2.0], [-2, 0, 1.7, 2.6, 2.0], [4, 6, 1.7, 2.6, 2.0]], east: [[-5.6, -4.4, 1.7, 2.6, 1.2]] }),
    plan: (f, { w }) => ({
      rooms: [{ id: "workshop", type: "workshop", x0: w.ix0, z0: w.iz0, x1: 6.0, z1: w.iz1 }, { id: "store", type: "utility", x0: 6.12, z0: w.iz0, x1: w.ix1, z1: w.iz1 }],
      walls: [{ axis: "z", d: 6.06, a0: w.iz0, a1: w.iz1, holes: [{ c: -4.9, w: 0.9, a: "workshop", b: "store" }] }],
    }),
    dress: (A, w) => {
      const y = GF;
      for (const [a, b] of [[-11.3, -7.2], [-4.6, 2.6]]) {   // workbenches against the rear wall, pegboards over them
        A.box([a, y, -6.55], [b, y + 0.85, -5.85], "olive", { bv: 0.01 });
        A.box([a - 0.02, y + 0.85, -6.57], [b + 0.02, y + 0.9, -5.8], "laminate", { bv: 0.005 });
        A.box([a + 0.05, y + 0.12, -5.86], [b - 0.05, y + 0.75, -5.84], "panel_dk", L1);
        A.box([a, y + 1.2, -6.55], [b, y + 2.2, -6.53], "timber_raw", L1);
        for (let x = a + 0.3; x < b; x += 0.45) A.box([x - 0.08, y + 1.4 + ((x * 7) % 0.5), -6.53], [x + 0.08, y + 1.55 + ((x * 7) % 0.5), -6.48], "steel_dk", L1);
        A.box([b - 0.5, y + 0.9, -6.0], [b - 0.2, y + 1.05, -5.75], "steel_dk", L1);   // vice
        A.box([a + 0.1, y + 0.9, -6.4], [a + 0.6, y + 1.2, -6.05], "red_paint", L1);   // tool box
      }
      A.box([6.4, y, -6.5], [11.4, y + 2.2, -5.9], "galv", { v: 0 });   // store shelving
      for (const yy of [0.1, 0.8, 1.5, 2.15]) A.box([6.4, y + yy, -6.5], [11.4, y + yy + 0.04, -5.9], "galv", K0);
      for (let x = 6.4; x <= 11.41; x += 1.25) A.box([x - 0.03, y, -6.5], [x + 0.03, y + 2.2, -5.9], "galv", L1);
      A.box([-11.3, y, -4.2], [-10.5, y + 1.8, -3.35], "red_paint", { bv: 0.01 });   // tool cabinet
    },
  });
  // shed floor, columns, rafters, purlins, roof sheet with roof lights, gutter, fascia
  A.box([-11.9, -0.3, -3.05], [11.9, y0, 7.0], "slab", { bv: 0.03 });
  for (const x of [-11.75, -4, 4, 11.75]) {
    ucol(A, x, zF, y0, ry(zF) - 0.3); ucol(A, x, -2.85, y0, ry(-2.85) - 0.3); ucol(A, x, zR, GF, ry(zR) - 0.3);
    bar(A, [x, ry(zR - 0.2) - 0.16, zR - 0.2], [x, ry(zF + 0.2) - 0.16, zF + 0.2], 0.16, 0.3, "olive", K0);   // rafter
    A.box([x - 0.25, ry(zF) - 0.75, zF - 0.4], [x + 0.25, ry(zF) - 0.3, zF - 0.1], "olive", L1);           // haunch
  }
  for (const [z, d] of [[zF, 0.2], [zR, 0.2]]) A.box([-11.95, ry(z) - 0.62, z - d / 2], [11.95, ry(z) - 0.3, z + d / 2], "olive");   // eaves beams
  for (let z = zR + 0.4; z < zF; z += 1.38) bar(A, [-12.05, ry(z) - 0.06, z], [12.05, ry(z) - 0.06, z], 0.07, 0.14, "galv", L1);
  bar(A, [0, ry(-7.2) + 0.03, -7.2], [0, ry(7.2) + 0.03, 7.2], 24.2, 0.06, "corrugated");
  for (const x of [-8, 0, 8]) bar(A, [x, ry(-1) + 0.065, -1], [x, ry(4.5) + 0.065, 4.5], 1.2, 0.012, "glass_int", K0);
  A.box([-12.1, ry(7.2) - 0.3, 7.15], [12.1, ry(7.2) + 0.12, 7.25], "panel_acc", { k: 0, bv: 0.01 });
  for (const x of [-8, 0, 8]) A.box([x - 1.6, ry(7.2) - 0.25, 7.25], [x + 1.6, ry(7.2) + 0.05, 7.27], "sign_blank", L1);
  A.box([-12.1, ry(-7.2) - 0.18, -7.32], [12.1, ry(-7.2) + 0.02, -7.16], "alu", L1);   // rear gutter
  for (const x of [-11.6, 11.6]) A.box([x - 0.06, 0, -7.32], [x + 0.06, ry(-7.2) - 0.1, -7.2], "alu", L1);
  // clad sides on a precast plinth wall, cladding above the workshop
  for (const s of [-1, 1]) {
    const xa = s * 11.9, xb = s * 12.0, [p0, p1] = [Math.min(xa, xb), Math.max(xa, xb)];
    A.box([p0, 0, -3.05], [p1, 1.2, 6.7], "conc_ag", { bv: 0.01 });
    A.box([p0, 1.2, -6.8], [p1, ry(-6.8), 6.7], "corrugated");
    A.prism(0, p0, p1, [[-6.8, ry(-6.8)], [6.7, ry(-6.8)], [6.7, ry(6.7)]], "corrugated");
    for (let z = -2.5; z < 6.6; z += 3.0) A.box([s > 0 ? p1 : p0 - 0.05, 1.2, z - 0.05], [s > 0 ? p1 + 0.05 : p0, ry(z) - 0.1, z + 0.05], "olive", L1);
  }
  A.box([-11.9, GF + 3.4, -6.95], [11.9, ry(-6.95), -6.85], "corrugated");
  A.box([-11.9, GF + 3.4, -3.15], [11.9, GF + 3.6, -3.0], "olive", L1);   // flashing over the workshop front
  // floor markings: bay lines, inspection-pit outlines with hatching, wheel stops, drip stains; lights, hose reels
  const L = (x0, x1, z0, z1, m = "line_w") => A.box([x0, y0, z0], [x1, y0 + 0.004, z1], m, L1);
  for (const x of [-4, 4]) L(x - 0.06, x + 0.06, -2.7, 6.9);
  for (const c of [-8, 0, 8]) {
    L(c - 0.7, c + 0.7, 0.0, 0.1, "line_yel"); L(c - 0.7, c + 0.7, 5.0, 5.1, "line_yel"); L(c - 0.7, c - 0.6, 0.1, 5.0, "line_yel"); L(c + 0.6, c + 0.7, 0.1, 5.0, "line_yel");
    for (let z = 0.4; z < 4.9; z += 0.6) A.obox([c, y0 + 0.002, z], [0.08, 0.004, 1.3], [0, 45, 0], "line_blk", L1);
    for (const e of [-1.1, 1.1]) A.box([c + e - 0.45, y0, -2.55], [c + e + 0.45, y0 + 0.15, -2.35], "concrete_wash", { bv: 0.02 });   // wheel stops
    A.cyl([c + 0.3, y0 + 0.003, 2.4], [1.1, 0.004, 0.8], "line_blk", { k: 0, lod: 1, seg: 12 });   // oil stain
    A.box([c - 1.5, ry(2) - 0.62, 1.0], [c + 1.5, ry(2) - 0.56, 1.2], "light_panel", L1);
    A.box([c - 1.5, ry(5) - 0.62, 4.0], [c + 1.5, ry(5) - 0.56, 4.2], "light_panel", L1);
  }
  for (const x of [-4, 4]) { A.cyl([x, 1.6, 7.08], [0.5, 0.15, 0.5], "red_paint", { k: 0, lod: 1, seg: 12, r: [90, 0, 0] }); A.box([x - 0.2, 0.9, 6.98], [x + 0.2, 1.3, 7.12], "red_paint", L1); }
  addRoom(A, "shed", "utility", -11.8, y0, -2.95, 11.8, RE - 0.4, 6.9);
  for (const c of [-8, 0, 8]) A._nav.entrances.push({ p: [c, 0, 7.6], dir: [0, 0, -1], room: "shed" });
  A._nav.cover.push(...[-8, 0, 8].flatMap((c) => [cover([c + 1.0, y0, -2.9], 0, "low", ["over"])]), cover([-12.5, 0, 2], 90, "high", ["left", "right"]), cover([12.5, 0, 2], 270, "high", ["left", "right"]),
    cover([0, 0, -7.6], 0, "high", ["left", "right"]), ...[-11.75, -4, 4, 11.75].map((x) => cover([x, y0, 6.3], 180, "high", [x < 0 ? "left" : "right"])));
  return done(A);
}

// ============================================================================ MILITARY: radar tower
export function radarTower() {
  const A = mkP("mod_nt_radar_tower", "Radar tower", "Military", "Square galvanised lattice radar tower, 5.6 m on plan and 17.8 m to its top deck, carrying a white radome (about 21.7 m overall). Four box legs on concrete footings with girts every 3 m and X-bracing, a caged steel switchback stair (ten flights of eight risers with landings and handrails) up the west side to a chequer-plate intermediate platform at 9 m and a top platform at 15 m under the radome deck; guard rails all round, louvred screening panels on part of the top platform's edge, equipment cabinets and cable trays.",
    [6, 6], { enterable: true, floors: 2 });
  const LG = 2.7, PY1 = 9.0, PY2 = 15.0, TOP = 17.8, E = 2.85;
  const lane = { A: [-2.35, -1.45], B: [-1.35, -0.45] }, H = [-2.4, -0.4, -1.0, 2.15];   // stair hole: x0 x1 z0 z1
  // legs, footings, girts, X-bracing (the lowest south panel left open as the stair entrance)
  for (const [sx, sz] of [[-1, -1], [1, -1], [1, 1], [-1, 1]]) {
    const x = sx * LG, z = sz * LG;
    A.box([x - 0.11, 0, z - 0.11], [x + 0.11, TOP, z + 0.11], "galv", { bv: 0.008 });
    A.box([x - 0.45, -0.4, z - 0.45], [x + 0.45, 0.15, z + 0.45], "conc_ag", { bv: 0.03 });
    A.box([x - 0.2, 0.15, z - 0.2], [x + 0.2, 0.18, z + 0.2], "steel_dk", L1);
    for (const [dx, dz] of [[-1, -1], [1, -1], [1, 1], [-1, 1]]) A.cyl([x + dx * 0.15, 0.2, z + dz * 0.15], [0.035, 0.05, 0.035], "steel_dk", { k: 0, lod: 1, n: 6 });
    for (let y = 3; y <= TOP - 0.5; y += 3) A.box([x - 0.16, y - 0.12, z - 0.16], [x + 0.16, y + 0.12, z + 0.16], "galv", L1);   // splice plates
  }
  const faces = [["x", -LG, "S"], ["x", LG, "N"], ["z", -LG, "W"], ["z", LG, "E"]];
  for (const [axis, d, nm] of faces) {
    for (let y = 3; y <= TOP; y += 3) wbox(A, axis, -LG, LG, y - 0.06, y + 0.06, d - 0.05, d + 0.05, "galv", K0);
    for (let y = 0.3; y + 2.6 <= TOP; y += 3) {
      if (nm === "S" && y < 1) continue;
      const p = (a, yy) => (axis === "x" ? [a, yy, d] : [d, yy, a]);
      rod(A, p(-LG, y), p(LG, y + 2.7), 0.07, "galv", { seg: 6 }); rod(A, p(LG, y), p(-LG, y + 2.7), 0.07, "galv", { seg: 6 });
    }
  }
  wbox(A, "x", -LG, -0.3, 2.2, 2.35, -LG - 0.05, -LG + 0.05, "galv", K0);   // entrance portal head
  // stair: ten flights (lanes A +z / B -z), landings north and south, stringers, rails, cage
  for (let k = 0; k < 10; k++) {
    const y = 1.5 * k, up = k % 2 === 0, [b0, b1] = up ? lane.A : lane.B, a0 = up ? -1.0 : 1.0, dir = up ? 1 : -1;
    flight(A, { axis: "z", b0, b1, a0, dir, y0: y, rise: 1.5, n: 8, run: 2.0, m: "chequer", soffit: "grating" });
    for (const x of [b0 - 0.02, b1 + 0.02]) bar(A, [x, y - 0.12, a0], [x, y + 1.38, a0 + dir * 2.0], 0.03, 0.22, "galv", L1);
    stairRail(A, [b0 - 0.03, y, a0], [b0 - 0.03, y + 1.5, a0 + dir * 2.0], "safety_yel", false);
    stairRail(A, [b1 + 0.03, y, a0], [b1 + 0.03, y + 1.5, a0 + dir * 2.0], "safety_yel", up);   // spine rail collides on lane A's flights
    const ly = y + 1.5, [l0, l1] = up ? [1.0, 2.15] : [-2.15, -1.0];
    if (ly === PY1 || ly === PY2) continue;   // those landings are part of the platforms
    A.box([-2.4, ly - 0.08, l0], [-0.4, ly, l1], "chequer");
    A.box([-2.4, ly - 0.2, up ? l1 - 0.08 : l0], [-0.4, ly - 0.08, up ? l1 : l0 + 0.08], "galv", L1);
    if (!up) { guard(A, "x", -2.4, -0.4, -2.17, ly); guard(A, "z", -2.15, -1.0, -0.38, ly); }
  }
  meshPanel(A, "z", -2.15, 2.2, -2.47, 0.0, 16.2, { sp: 0.3 });                   // west cage
  meshPanel(A, "x", -2.4, -0.4, 2.22, 0.0, 16.2, { sp: 0.3 });                    // north cage
  meshPanel(A, "z", -1.0, 2.2, -0.37, 0.0, 16.2, { sp: 0.3 });                    // east cage (between the lanes and the tower core)
  // platforms: chequer deck with the stair hole, edge channels, guard rails all round
  for (const py of [PY1, PY2]) {
    for (const [a, b, c, d] of cutRect(-E, E, -E, E, [{ a0: H[0], a1: H[1], y0: H[2], y1: H[3] }])) A.box([a, py - 0.08, c], [b, py, d], "chequer");
    for (const s of [-1, 1]) { A.box([-E, py - 0.3, s * E - 0.05], [E, py - 0.08, s * E + 0.05], "galv", K0); A.box([s * E - 0.05, py - 0.3, -E], [s * E + 0.05, py - 0.08, E], "galv", K0); }
    for (let x = -2; x <= 2; x += 1) A.box([x - 0.04, py - 0.25, -E], [x + 0.04, py - 0.08, E], "galv", L1);   // joists
    guard(A, "x", H[0], H[1], H[3] + 0.03, py, { posts: 1.0 });
    if (py === PY1) for (const [axis, d] of [["x", -E], ["x", E], ["z", -E], ["z", E]]) guard(A, axis, -E, E, d, py);
  }
  // top platform: louvred screening panels on part of the edge, guard rails elsewhere, cabinets, cable tray
  const scr = { S: [[-E, -1.0], [0.9, E]], N: [[-0.9, 1.1]], W: [[-E, -0.6], [1.4, E]], E: [[-1.8, 1.0]] };
  const yS = PY2 + 0.15, yT = TOP - 0.3;
  for (const [axis, d, nm] of [["x", -E, "S"], ["x", E, "N"], ["z", -E, "W"], ["z", E, "E"]]) {
    const holes = scr[nm].map(([a0, a1]) => ({ a0, a1, y0: -1, y1: 9 }));
    for (const [a0, a1] of cutRect(-E, E, 0, 1, holes).map((r) => [r[0], r[1]])) guard(A, axis, a0, a1, d, PY2);
    for (const [a0, a1] of scr[nm]) {
      wbox(A, axis, a0, a1, yS, yT, d - 0.04, d + 0.04, "louvre");
      for (let y = yS + 0.15; y < yT; y += 0.15) wbox(A, axis, a0, a1, y - 0.02, y + 0.02, d - 0.07, d + 0.07, "panel_dk", L1);
      for (const a of [a0, a1]) wbox(A, axis, a - 0.03, a + 0.03, PY2, yT + 0.1, d - 0.06, d + 0.06, "galv", L1);
      wbox(A, axis, a0, a1, PY2, yS, d - 0.05, d + 0.05, "galv", L1);   // toe plate
    }
  }
  for (const [x, z] of [[1.2, -2.2], [2.1, -2.2]]) { A.box([x - 0.4, PY2, z - 0.3], [x + 0.4, PY2 + 1.8, z + 0.3], "panel_lt", { bv: 0.01 }); A.box([x - 0.35, PY2 + 1.0, z + 0.3], [x + 0.35, PY2 + 1.7, z + 0.31], "louvre", L1); A.box([x - 0.05, PY2 + 1.2, z + 0.31], [x + 0.05, PY2 + 1.25, z + 0.34], "steel_dk", L1); }
  A.box([1.8, PY1, 1.6], [2.5, PY1 + 1.5, 2.4], "panel_lt", { bv: 0.01 });   // junction cabinet at 9 m
  A.box([1.65, PY1 + 1.5, -2.5], [1.85, PY2 - 0.3, -2.3], "steel_dk", K0);   // cable riser
  A.box([1.65, 0.2, -2.6], [1.85, PY1, -2.4], "steel_dk", K0);
  A.box([-2.0, TOP - 0.55, -2.5], [2.5, TOP - 0.45, -2.3], "galv", L1);      // cable tray under the deck
  for (const [x, z] of [[0, 0], [2, 2], [-2, -2], [2, -2]]) A.box([x - 0.2, TOP - 0.36, z - 0.2], [x + 0.2, TOP - 0.3, z + 0.2], "light_panel", L1);
  // radome deck and radome (stacked frustums approximating a truncated sphere), base ring, lightning finial, obstruction lamps
  A.box([-E - 0.1, TOP - 0.3, -E - 0.1], [E + 0.1, TOP, E + 0.1], "steel_dk", { bv: 0.01 });
  A.box([-E - 0.12, TOP, -E - 0.12], [E + 0.12, TOP + 0.06, E + 0.12], "galv", { k: 0, bv: 0.01 });
  A.cyl([0, TOP + 0.21, 0], [3.7, 0.3, 3.7], "panel_lt", { seg: 32 });
  const R = 2.0, cy = TOP + 0.36 + Math.sqrt(R * R - 1.8 * 1.8), ph0 = Math.asin(-(cy - TOP - 0.36) / R), N = 9;
  for (let i = 0; i < N; i++) {
    const a = ph0 + ((Math.PI / 2 - ph0) * i) / N, b = ph0 + ((Math.PI / 2 - ph0) * (i + 1)) / N;
    const ya = cy + R * Math.sin(a), yb = cy + R * Math.sin(b), ra = R * Math.cos(a), rb = R * Math.cos(b) + (i === N - 1 ? 0.001 : 0);
    if (rb <= ra) A.cone([0, (ya + yb) / 2, 0], [2 * ra, yb - ya, 2 * ra], "radome", { k: 0, seg: 32, top: Math.max(rb / ra, 0.001) });
    else A.cone([0, (ya + yb) / 2, 0], [2 * rb, yb - ya, 2 * rb], "radome", { k: 0, seg: 32, top: ra / rb, r: [180, 0, 0] });
    if (i % 3 === 1) A.cyl([0, ya, 0], [2 * ra + 0.02, 0.02, 2 * ra + 0.02], "paint_w", { k: 0, lod: 1, seg: 32 });
  }
  A.box([-1.8, TOP + 0.06, -1.8], [1.8, cy + R, 1.8], "radome", H0);
  A.cyl([0, cy + R + 0.5, 0], [0.03, 1.0, 0.03], "steel_dk", { k: 0, seg: 6 });
  for (const [x, z] of [[-E, -E], [E, E], [E, -E], [-E, E]]) A.box([x - 0.08, TOP + 0.06, z - 0.08], [x + 0.08, TOP + 0.22, z + 0.08], "lamp_r", L1);
  // nav: the stair as two rooms (below / above the 9 m platform), the two platforms
  const rooms = [room("stair_lo", [-2.4, 0, -2.15], [-0.4, PY1, 2.2]), room("mid", [-E, PY1, -E], [E, PY1 + 2.4, E]), room("stair_hi", [-2.4, PY1, -2.15], [-0.4, PY2, 2.2]), room("top", [-E, PY2, -E], [E, TOP - 0.3, E])];
  const fp = (py, kind) => [fire([0, py, E - 0.4], 0, kind), fire([E - 0.4, py, 0], 90, kind), fire([0.5, py, -E + 0.4], 180, kind), fire([-E + 0.4, py, -2.4], 270, kind)];
  return pdone(A, {
    effect: "block", rooms,
    doors: [{ a: "stair_lo", b: "mid", p: [-0.9, PY1, -1.5] }, { a: "mid", b: "stair_hi", p: [-1.9, PY1, -1.2] }, { a: "stair_hi", b: "top", p: [-0.9, PY2, -1.5] }],
    entrances: [{ p: [-1.9, 0, -3.5], dir: [0, 0, 1], room: "stair_lo" }],
    corners: [...corners("mid", -E, E, -E, E, PY1, 0.4), ...corners("top", -E, E, -E, E, PY2, 0.4)],
    firingPoints: [...fp(PY1, "firestep"), ...fp(PY2, "window")],
    cover: [cover([-0.1, PY2, -E + 0.45], 180, "high", ["left", "right"]), cover([E - 0.45, PY2, 1.6], 90, "high", ["left"]), cover([-E + 0.45, PY2, 0.3], 270, "high", ["left", "right"]), cover([0.2, PY2, E - 0.45], 0, "high", ["left", "right"]),
      cover([2.1, PY1, 1.1], 0, "low", ["over"]), ...[[-LG, -LG], [LG, -LG], [LG, LG], [-LG, LG]].map(([x, z]) => cover([x + Math.sign(x) * 0.6, 0, z], x < 0 ? 90 : 270, "high", ["left", "right"]))],
  });
}

// ============================================================================ MILITARY: compound wall bays
const WH = 3.0, WT = 0.18;
function wallPost(A, x, h = WH + 0.12) {
  A.box([x - 0.16, -0.3, -0.16], [x + 0.16, h, 0.16], "conc_ag", { bv: 0.02 });
  A.box([x - 0.2, h, -0.2], [x + 0.2, h + 0.08, 0.2], "precast", { bv: 0.015 });
  for (const s of [-1, 1]) A.box([x - 0.012, 0.1, s * 0.16 - 0.002], [x + 0.012, h - 0.1, s * 0.16 + 0.002], "conc_break", L1);   // casting joint
  for (const s of [-1, 1]) A.box([x - 0.04, h - 0.3, s * 0.16], [x + 0.04, h - 0.22, s * (0.16 + 0.01)], "steel_dk", L1);   // lifting insert
}
// precast plank panels between x0 and x1, holes = cutRect holes (a0 a1 y0 y1), tops = coping spans [[a0, a1]]
function wallPanels(A, x0, x1, holes = [], tops = [[x0, x1]]) {
  for (let i = 0; i < 3; i++) {
    const ya = i * 1.0 + (i ? 0.008 : 0), yb = (i + 1) * 1.0 - 0.008;
    for (const [a, b, c, d] of cutRect(x0, x1, ya, yb, holes)) A.box([a, c, -WT / 2], [b, d, WT / 2], i % 2 ? "conc_fw2" : "conc_fw");
    if (i) for (const [a, b] of cutRect(x0, x1, ya - 0.012, ya, holes).map((r) => [r[0], r[1]])) for (const s of [-1, 1]) A.box([a, ya - 0.012, s * WT / 2 - 0.003], [b, ya + 0.004, s * WT / 2 + 0.003], "conc_break", L1);
  }
  for (const [a, b] of tops) {
    A.box([a, WH, -0.15], [b, WH + 0.08, 0.15], "precast", { bv: 0.015 });
    A.box([a, WH - 0.02, -0.16], [b, WH, 0.16], "conc_break", L1);   // drip groove shadow
  }
  for (const [a, b, c, d] of cutRect(x0, x1, -0.3, 0.05, holes.map((h) => ({ ...h, y0: -1 })))) A.box([a, c, -0.25], [b, d, 0.25], "conc_rough", { bv: 0.01 });   // footing
  for (let x = x0 + 0.7; x < x1 - 0.3; x += 1.4) if (!holes.some((h) => x > h.a0 - 0.1 && x < h.a1 + 0.1 && h.y0 < 0.2)) for (const s of [-1, 1]) A.box([x - 0.04, 0.06, s * WT / 2 - 0.004], [x + 0.04, 0.12, s * WT / 2 + 0.004], "line_blk", L1);   // weep holes
}
// concertina razor coil (k: 0 rods), as catalog_modern_props.mjs concertina()
function concertina(A, x0, x1, y, z, dia, pitch = 0.3, seg = 6, m = "galv") {
  const turns = Math.max(2, Math.round((x1 - x0) / pitch)), r = dia / 2;
  for (const hand of [1, -1]) {
    let prev = null;
    for (let i = 0; i <= turns * seg; i++) {
      const t = i / seg, a = hand * t * 2 * Math.PI + (hand < 0 ? Math.PI : 0);
      const p = [x0 + (t / turns) * (x1 - x0), y + Math.cos(a) * r, z + Math.sin(a) * r];
      if (prev) rod(A, prev, p, 0.006, m, { seg: 4 });
      prev = p;
    }
  }
}
// Y-arms on a post top with barbed strands along [x0, x1] and a razor coil in the cradle
function razorTop(A, posts, spans) {
  const y = WH + 0.2;
  for (const x of posts) for (const s of [-1, 1]) { bar(A, [x, y - 0.1, 0], [x, y + 0.45, s * 0.4], 0.04, 0.04, "galv", L1); A.box([x - 0.05, y - 0.12, -0.05], [x + 0.05, y, 0.05], "galv", L1); }
  for (const [x0, x1] of spans) {
    for (const s of [-1, 1]) for (const t of [0.35, 0.7, 1.0]) rod(A, [x0, y - 0.1 + 0.55 * t, s * 0.4 * t], [x1, y - 0.1 + 0.55 * t, s * 0.4 * t], 0.005, "wire", { seg: 4 });
    concertina(A, x0, x1, y + 0.2, 0, 0.45, 0.3, 6);
  }
}
function wallNav(x0, x1, more = []) {
  const cv = [];
  for (let x = x0 + 0.8; x < x1 - 0.5; x += 1.6) { const e = x < x0 + 1.6 ? ["left"] : x > x1 - 1.6 ? ["right"] : ["left", "right"]; cv.push(cover([x, 0, 0.65], 180, "high", e), cover([x, 0, -0.65], 0, "high", e)); }
  return [...cv, ...more];
}

export function compoundWall() {
  const A = mkP("mod_nt_compound_wall", "Compound wall bay (6 m)", "Military", "One 6 m bay of a 3 m precast concrete perimeter wall: three formwork-textured planks slotted between square concrete posts on a strip footing, a precast coping, Y-arms on the posts carrying three barbed strands each side and a razor-wire coil. Not climbable: a collider tops the wall.",
    [6, 0.5]);
  wallPost(A, -3.0); wallPost(A, 3.0); wallPanels(A, -2.84, 2.84); razorTop(A, [-3, 3], [[-3, 3]]);
  A.box([-3.1, WH, -0.2], [3.1, WH + 0.65, 0.2], "wire", H0);
  return pdone(A, { cover: wallNav(-3, 3) });
}

export function compoundWallBreach() {
  const A = mkP("mod_nt_compound_wall_breach", "Compound wall bay, breached", "Military", "The 6 m precast perimeter wall bay blasted open: a 2.5 m walkable gap between ragged panel ends with exposed and bent rebar, a broken coping, cut razor wire hanging, and a rubble mound with slabs and chunks ramping up from both sides (about 0.55 m at the wall line).",
    [6, 3]);
  wallPost(A, -3.0); wallPost(A, 3.0);
  const ends = { L: [-1.3, -1.6, -2.1], R: [1.3, 1.45, 2.0] };
  for (const [s, e] of [[-1, ends.L], [1, ends.R]]) {
    for (let i = 0; i < 3; i++) {
      const ya = i * 1.0 + (i ? 0.008 : 0), yb = (i + 1) * 1.0 - 0.008, a = s < 0 ? -2.84 : e[i], b = s < 0 ? e[i] : 2.84;
      A.box([a, ya, -WT / 2], [b, yb, WT / 2], i % 2 ? "conc_fw2" : "conc_fw");
      const xe = e[i];
      for (let k = 0; k < 3; k++) A.obox([xe + s * 0.06, ya + 0.2 + k * 0.28, (k - 1) * 0.04], [0.16, 0.24, WT * 0.8], [k * 9, 0, 20 - k * 15], "conc_break", K0);   // ragged face
      for (let k = 0; k < 4; k++) {                                                                                                     // rebar out of the break
        const yy = ya + 0.15 + k * 0.23, zz = k % 2 ? 0.045 : -0.045, len = 0.25 + ((k * 37 + i * 11) % 30) / 100;
        rod(A, [xe - s * 0.05, yy, zz], [xe + s * len, yy + (k % 2 ? -0.1 : 0.12), zz + (k - 1.5) * 0.06], 0.016, "rebar", { seg: 6 });
      }
    }
    const ce = s < 0 ? [-2.84, -2.1] : [2.0, 2.84];
    A.box([ce[0], WH, -0.15], [ce[1], WH + 0.08, 0.15], "precast", { bv: 0.015 });
    for (const x of s < 0 ? [-2.3, -1.6] : [1.7, 2.4]) A.box([x - 0.04, 0.06, WT / 2 - 0.004], [x + 0.04, 0.12, WT / 2 + 0.004], "line_blk", L1);
  }
  A.box([-2.84, -0.3, -0.25], [-1.3, 0.05, 0.25], "conc_rough", { bv: 0.01 }); A.box([1.3, -0.3, -0.25], [2.84, 0.05, 0.25], "conc_rough", { bv: 0.01 });
  razorTop(A, [-3, 3], [[-3, -2.1], [2.0, 3]]);
  for (const [x0, x1, dy] of [[-2.1, -1.2, -1.4], [2.0, 1.0, -1.9]]) rod(A, [x0, WH + 0.4, 0], [x1, WH + dy, 0.35], 0.006, "galv", { seg: 4 });   // cut strands hanging
  concertina(A, -2.1, -1.4, 1.5, 0.45, 0.4, 0.3, 6);
  A.box([-3.1, WH, -0.2], [-2.1, WH + 0.65, 0.2], "wire", H0); A.box([2.0, WH, -0.2], [3.1, WH + 0.65, 0.2], "wire", H0);
  // rubble mound: a stump of broken panel on the footing and walkable ramps both sides, slabs and chunks over it
  A.box([-1.45, 0, -0.16], [1.45, 0.55, 0.16], "rubble", { bv: 0.04 });
  for (const s of [-1, 1]) A.wedge([0, 0.275, s * 0.83], [3.0, 0.55, 1.34], "rubble", { r: [0, s > 0 ? 0 : 180, 0] });
  const chunks = [[-0.9, 0.5, 0.35, 0.7, 0.16, 0.5, 15, 30, 8], [0.5, 0.42, 0.6, 0.9, 0.18, 0.6, -12, -20, 5], [0.1, 0.45, -0.5, 0.8, 0.18, 0.55, 10, 70, -10],
    [-0.4, 0.3, -1.0, 0.5, 0.2, 0.4, 20, 10, 25], [1.0, 0.3, -0.9, 0.4, 0.25, 0.35, -15, 45, 10], [-1.2, 0.25, 1.1, 0.45, 0.22, 0.4, 5, -30, 20], [0.9, 0.2, 1.2, 0.35, 0.2, 0.3, 25, 15, -5]];
  for (const [x, y, z, sx, sy, sz, rx, ry, rz] of chunks) A.obox([x, y, z], [sx, sy, sz], [rx, ry, rz], "conc_break", K0);
  for (let i = 0; i < 18; i++) { const t = i * 2.399, r = 0.3 + (i % 6) * 0.22; A.obox([Math.cos(t) * r * 1.2, 0.08 + ((i * 13) % 5) * 0.04, Math.sin(t) * r], [0.12 + (i % 3) * 0.05, 0.08, 0.1 + (i % 4) * 0.04], [i * 17, i * 41, i * 29], "rubble", L1); }
  A.obox([-2.3, 0.18, 1.0], [1.3, 0.18, 0.75], [0, 25, 12], "conc_fw", { bv: 0.01 });   // fallen plank section (low cover)
  A.obox([2.2, 0.2, -1.0], [1.1, 0.2, 0.7], [-8, -15, 0], "conc_fw2", { bv: 0.01 });
  A.obox([-1.0, 0.06, -1.3], [0.9, 0.08, 0.3], [0, 12, 4], "precast", K0);             // broken coping
  for (const [x, z] of [[-2.3, 1.0], [2.2, -1.0]]) rod(A, [x - 0.6, 0.25, z], [x + 0.7, 0.35, z + 0.1], 0.016, "rebar", { seg: 6 });
  return pdone(A, {
    effect: "block",
    cover: [cover([-2.0, 0, 0.6], 180, "high", ["right"]), cover([-2.0, 0, -0.6], 0, "high", ["left"]), cover([2.1, 0, 0.6], 180, "high", ["left"]), cover([2.1, 0, -0.6], 0, "high", ["right"]),
      cover([-2.3, 0, 1.6], 180, "low", ["over"]), cover([2.2, 0, -1.6], 0, "low", ["over"])],
  });
}

export function compoundWallCulvert() {
  const A = mkP("mod_nt_compound_wall_culvert", "Compound wall bay with culvert", "Military", "The 6 m precast perimeter wall bay built over a drainage culvert: a 1.4 x 1.6 m precast box culvert runs through the wall base, with headwalls and splayed wing walls both sides, a silted floor, and its steel bar grate unbolted and leaning against the headwall, so a soldier can crouch through under the wall.",
    [6, 3]);
  const CX = 0.7, CW2 = 0.9, CY = 0.05, CT = 1.65, CL = 1.25;
  wallPost(A, -3.0); wallPost(A, 3.0);
  wallPanels(A, -2.84, 2.84, [{ a0: -CW2, a1: CW2, y0: -1, y1: CT + 0.25 }]);
  razorTop(A, [-3, 3], [[-3, 3]]);
  A.box([-3.1, WH, -0.2], [3.1, WH + 0.65, 0.2], "wire", H0);
  // box culvert: base, walls, roof, joints
  A.box([-CW2, -0.25, -CL], [CW2, CY, CL], "conc_ag", { bv: 0.01 });
  for (const s of [-1, 1]) A.box([Math.min(s * CX, s * CW2), CY, -CL], [Math.max(s * CX, s * CW2), CT, CL], "conc_ag");
  A.box([-CW2, CT, -CL], [CW2, CT + 0.25, CL], "conc_ag", { bv: 0.01 });
  for (const z of [-0.6, 0.6]) { A.box([-CX, CY, z - 0.01], [-CX + 0.01, CT, z + 0.01], "conc_break", L1); A.box([CX - 0.01, CY, z - 0.01], [CX, CT, z + 0.01], "conc_break", L1); A.box([-CX, CT - 0.01, z - 0.01], [CX, CT, z + 0.01], "conc_break", L1); }
  A.box([-CX + 0.05, CY, -CL], [CX - 0.05, CY + 0.03, CL], "dirt", K0);   // silt
  for (let i = 0; i < 8; i++) A.obox([((i * 37) % 10) / 10 - 0.5, CY + 0.035, -1.1 + i * 0.3], [0.12, 0.02, 0.08], [0, i * 50, 0], "rubble", L1);
  // headwalls with splayed wing walls, aprons
  for (const s of [-1, 1]) {
    const z0 = s * CL, z1 = s * (CL + 0.2), [h0, h1] = [Math.min(z0, z1), Math.max(z0, z1)];
    A.box([-1.7, -0.25, h0], [-CX, CT + 0.5, h1], "conc_ag", { bv: 0.015 }); A.box([CX, -0.25, h0], [1.7, CT + 0.5, h1], "conc_ag", { bv: 0.015 });
    A.box([-CX, CT, h0], [CX, CT + 0.5, h1], "conc_ag", { bv: 0.015 });
    A.box([-1.75, CT + 0.5, h0 - 0.03], [1.75, CT + 0.58, h1 + 0.03], "precast", { bv: 0.01 });
    for (const e of [-1, 1]) bar(A, [e * 1.6, (CT + 0.5) / 2 - 0.15, s * (CL + 0.1)], [e * 2.1, (CT + 0.5) / 2 - 0.4, s * 1.48], 0.2, CT + 0.2, "conc_ag", {});   // wing walls
    A.box([-CX, -0.1, Math.min(s * (CL + 0.2), s * 1.5)], [CX, CY, Math.max(s * (CL + 0.2), s * 1.5)], "conc_rough", { bv: 0.01 });
    for (const e of [-1, 1]) for (const y of [0.3, 1.3]) A.box([e * CX - 0.03, y, h1 - (s > 0 ? 0 : 0.0) - 0.02], [e * CX + 0.03, y + 0.1, h1 + 0.02], "steel_dk", L1);   // grate brackets
  }
  // the grate: frame and bars, unbolted and leaning against the +z headwall beside the opening
  const g = { c: [1.35, 0.8, CL + 0.33], s: [1.35, 1.6] };
  A.obox(g.c, [g.s[0], g.s[1], 0.05], [-14, 0, 4], "steel_dk", K0);
  A.obox([g.c[0], g.c[1], g.c[2] + 0.002], [g.s[0] - 0.1, g.s[1] - 0.1, 0.06], [-14, 0, 4], "steel_dk", { k: 0, v: 0 });
  for (let i = -5; i <= 5; i++) A.obox([g.c[0] + i * 0.12, g.c[1], g.c[2] + 0.03], [0.025, g.s[1] - 0.08, 0.025], [-14, 0, 4], "galv", L1);
  A.obox([g.c[0], g.c[1] + 0.3, g.c[2] + 0.04], [g.s[0] - 0.05, 0.04, 0.02], [-14, 0, 4], "galv", L1);
  return pdone(A, {
    effect: "block",
    rooms: [room("culvert", [-CX, CY, -CL - 0.2], [CX, CT, CL + 0.2])],
    entrances: [{ p: [0, 0, 2.1], dir: [0, 0, -1], room: "culvert" }, { p: [0, 0, -2.1], dir: [0, 0, 1], room: "culvert" }],
    cover: wallNav(-3, 3, [cover([-1.5, 0, 1.75], 180, "high", ["right"]), cover([1.5, 0, -1.75], 0, "high", ["left"])]).filter((c) => Math.abs(c.p[0]) > 1.6 || Math.abs(c.p[2]) > 1),
  });
}

// ============================================================================ MILITARY: main gate
export function compoundGate() {
  const A = mkN("mod_nt_compound_gate", "Compound main gate", "Main entrance of the base, 14 x 6 m: two 1.5 m precast wall stubs with razor wire ending at tall concrete gate piers, a 9 m opening split by a kerbed island into two lanes, a glazed guard booth on the island (enterable), two red-and-white lifting barrier arms (raised) on motor housings, rubber speed bumps, a drainage channel, lamp heads on the piers, a floodlight mast and blank sign panels.",
    1, [14, 6], RED);
  A.meta.category = "Modern: Military";
  // wall stubs and piers
  for (const s of [-1, 1]) {
    wallPost(A, s * 7.0);
    const [a, b] = s < 0 ? [-6.84, -5.5] : [5.5, 6.84];
    wallPanels(A, a, b);
    razorTop(A, [s * 7.0], [[Math.min(s * 7, s * 5.5), Math.max(s * 7, s * 5.5)]]);
    A.box([Math.min(s * 7.1, s * 5.5), WH, -0.2], [Math.max(s * 7.1, s * 5.5), WH + 0.65, 0.2], "wire", H0);
    const [p0, p1] = s < 0 ? [-5.5, -4.5] : [4.5, 5.5];
    A.box([p0, -0.3, -0.35], [p1, 3.4, 0.35], "conc_ag", { bv: 0.03 });
    A.box([p0 - 0.06, 3.4, -0.41], [p1 + 0.06, 3.52, 0.41], "precast", { bv: 0.02 });
    for (const y of [1.0, 2.0]) A.box([p0 - 0.005, y - 0.01, -0.355], [p1 + 0.005, y + 0.01, 0.355], "conc_break", L1);
    A.box([(p0 + p1) / 2 - 0.45, 1.6, 0.35], [(p0 + p1) / 2 + 0.45, 2.4, 0.37], "blue_paint", L1);       // blank sign panels
    A.box([(p0 + p1) / 2 - 0.35, 0.9, 0.35], [(p0 + p1) / 2 + 0.35, 1.4, 0.37], "red_paint", L1);
    A.cyl([(p0 + p1) / 2, 3.75, 0], [0.12, 0.45, 0.12], "steel_dk", { k: 0, seg: 8 });                    // lamp on the pier
    A.box([(p0 + p1) / 2 - 0.25, 3.95, -0.25], [(p0 + p1) / 2 + 0.25, 4.25, 0.25], "lamp_w", { k: 0, bv: 0.02 });
    A.box([(p0 + p1) / 2 - 0.3, 4.25, -0.3], [(p0 + p1) / 2 + 0.3, 4.32, 0.3], "steel_dk", { k: 0, bv: 0.01 });
    A.box([s * 4.45 - 0.05, 0.6, 1.95], [s * 4.45 + 0.05, 1.0, 2.05], "steel_dk", L1);                   // arm rest post
    A.box([s * 4.45 - 0.08, 1.0, 1.92], [s * 4.45 + 0.08, 1.05, 2.08], "safety_yel", L1);
    A.box([s * 4.45 - 0.06, 0, 1.94], [s * 4.45 + 0.06, 0.6, 2.06], "steel_dk", K0);
  }
  // island with kerbs, nose bollards, chevron paint
  A.box([-1.1, -0.2, -2.8], [1.1, 0.15, 2.8], "precast", { bv: 0.03 });
  for (const z of [-2.8, 2.8]) for (let i = 0; i < 4; i++) A.box([-1.1 + i * 0.55, 0.0, z - 0.012], [-0.85 + i * 0.55, 0.15, z + 0.012], i % 2 ? "line_blk" : "safety_yel", L1);
  for (const [x, z] of [[-0.75, 2.55], [0.75, 2.55], [-0.75, -2.55], [0.75, -2.55]]) { A.cyl([x, 0.65, z], [0.2, 1.0, 0.2], "safety_yel", { seg: 12 }); A.cyl([x, 1.0, z], [0.21, 0.08, 0.21], "line_blk", { k: 0, lod: 1, seg: 12 }); }
  A.cyl([0, 1.2, 2.65], [0.06, 2.1, 0.06], "galv", { k: 0, seg: 8 });   // stop sign post with a blank octagon
  A.cyl([0, 2.2, 2.69], [0.6, 0.02, 0.6], "red_paint", { k: 0, n: 8, r: [90, 0, 22.5] });
  // booth (enterable, glass all round, door at the rear)
  const w = wing(A, {
    tag: "g", x0: -0.85, x1: 0.85, z0: -1.5, z1: 0.7, n: 1, h: 2.6, gf: 0.27, T: 0.12, par: 0.12, band: null, skin: () => "panel_lt", unit: 1.4, fin: 0.04,
    doors: [{ id: "booth_door", side: "rear", c: 0, w: 0.85, h: 2.1, glass: true, room: "booth", apron: false }],
    glaze: () => ({ front: [[-0.72, 0.72, 0.9, 2.3, 1.44]], east: [[-1.37, 0.57, 0.9, 2.3, 0.97]], west: [[-1.37, 0.57, 0.9, 2.3, 0.97]] }),
    plan: (f, { w }) => ({ rooms: [{ id: "booth", type: "office_modern", x0: w.ix0, z0: w.iz0, x1: w.ix1, z1: w.iz1 }], walls: [] }),
    roof: (A, w) => {
      A.box([-1.05, w.top + 0.12, -1.7], [1.05, w.top + 0.3, 0.9], "panel_dk", { bv: 0.02 });
      A.cyl([0.6, w.top + 0.5, 0.6], [0.06, 0.4, 0.06], "steel_dk", { k: 0, seg: 6 });
      A.obox([0.6, w.top + 0.75, 0.75], [0.35, 0.22, 0.12], [-25, 0, 0], "lamp_w", K0);   // floodlight
      A.box([-0.7, w.top + 0.3, -1.4], [-0.3, w.top + 0.6, -1.0], "hvac", { k: 0, bv: 0.01 });
    },
    dress: (A, w) => {
      A.box([-0.73, 0.27, 0.25], [0.73, 1.0, 0.58], "laminate", { bv: 0.01 });   // built-in desk under the front window
      A.box([-0.75, 1.0, 0.22], [0.75, 1.04, 0.6], "panel_dk", K0);
      A.box([0.1, 1.04, 0.4], [0.5, 1.3, 0.42], "screen", L1);
      camera(A, sideF(w, "front"), -0.6, 2.75);
    },
  });
  // barrier housings and raised arms (red / white segments)
  for (const s of [-1, 1]) {
    const x = s * 0.8, z = 2.0;
    A.box([x - 0.18, 0.15, z - 0.4], [x + 0.18, 1.05, z + 0.4], "paint_w", { bv: 0.02 });
    A.box([x - 0.2, 1.05, z - 0.42], [x + 0.2, 1.1, z + 0.42], "steel_dk", { k: 0, bv: 0.01 });
    const a = s < 0 ? 95 : 85, d = [Math.cos(a / DEG), Math.sin(a / DEG)], piv = [x + s * 0.22, 0.95], L = 3.6;
    A.cyl([piv[0], piv[1], z], [0.14, 0.12, 0.14], "steel_dk", { k: 0, seg: 10, r: [0, 0, 90] });
    for (let i = 0; i < 6; i++) { const t = (L * (i + 0.5)) / 6; A.obox([piv[0] + d[0] * t, piv[1] + d[1] * t, z], [L / 6 - 0.01, 0.1, 0.07], [0, 0, a], i % 2 ? "paint_w" : "red_paint", { k: 0, bv: 0.01 }); }
  }
  // speed bumps (yellow / black rubber modules) and a drainage channel across the lanes
  for (const [a, b] of [[-4.4, -1.15], [1.15, 4.4]]) {
    const n = Math.round((b - a) / 0.6);
    for (let i = 0; i < n; i++) extrudeX(A, a + (i * (b - a)) / n + 0.003, a + ((i + 1) * (b - a)) / n - 0.003, [[2.55, 0], [2.95, 0], [2.85, 0.06], [2.65, 0.06]], i % 2 ? "safety_yel" : "rubber");
    A.box([a, -0.1, -0.45], [b, 0.005, -0.15], "conc_rough", K0);
    for (let x = a + 0.1; x < b; x += 0.12) A.box([x - 0.02, 0.005, -0.43], [x + 0.02, 0.012, -0.17], "grating", L1);
  }
  // floodlight mast at the island tail
  A.cyl([0, 3.0, -2.55], [0.12, 5.8, 0.12], "galv", { seg: 10 });
  for (const s of [-1, 1]) A.obox([s * 0.35, 5.8, -2.55], [0.45, 0.25, 0.15], [0, 0, s * 20], "lamp_w", K0);
  A.box([-0.5, 5.75, -2.6], [0.5, 5.85, -2.5], "galv", L1);
  coverRing(A, -0.85, 0.85, -1.5, 0.7);
  A._nav.cover.push(...[-1, 1].flatMap((s) => [cover([s * 5.0, 0, 0.95], 180, "high", [s < 0 ? "right" : "left"]), cover([s * 5.0, 0, -0.95], 0, "high", [s < 0 ? "left" : "right"])]),
    cover([0, 0, 3.3], 180, "low", ["over", "left", "right"]));
  return done(A);
}

// ============================================================================ MILITARY: obstacle course
export function trainingWall() {
  const A = mkP("mod_nt_training_wall", "Obstacle climbing wall", "Military", "Obstacle-course climbing wall, 2.9 m wide and 2.4 m high: horizontal timber boards on two square posts, a rounded top rail, a foot board and diagonal braces at the back, galvanised bolts and post shoes.",
    [3, 1]);
  for (const x of [-1.38, 1.38]) {
    A.box([x - 0.08, -0.5, -0.08], [x + 0.08, 2.55, 0.08], "post_wood", { bv: 0.01 });
    A.cone([x, 2.6, 0], [0.18, 0.1, 0.18], "post_wood", { k: 0, n: 4, top: 0.2 });
    A.box([x - 0.1, 0, -0.1], [x + 0.1, 0.25, 0.1], "galv", L1);
    bar(A, [x, 1.9, -0.06], [x, 0.0, -0.44], 0.1, 0.1, "post_wood", K0);
    A.box([x - 0.06, -0.3, -0.5], [x + 0.06, 0.05, -0.38], "post_wood", L1);
  }
  for (let i = 0; i < 12; i++) A.box([-1.46, i * 0.2 + 0.005, 0.08], [1.46, (i + 1) * 0.2 - 0.005, 0.125], "timber", { k: 0, bv: 0.006 });
  A.box([-1.46, 0, 0.0], [1.46, 2.4, 0.13], "timber", H0);
  A.cyl([0, 2.45, 0.04], [0.12, 2.94, 0.12], "post_wood", { seg: 10, r: [0, 0, 90] });
  A.box([-1.3, 0.55, -0.22], [1.3, 0.6, -0.08], "timber", { bv: 0.005 });   // foot board at the back
  for (const x of [-1.0, 1.0]) A.box([x - 0.05, 0.4, -0.13], [x + 0.05, 0.6, -0.08], "post_wood", L1);
  for (const x of [-1.38, 1.38]) for (const y of [0.3, 1.2, 2.1]) A.cyl([x, y, 0.13], [0.03, 0.012, 0.03], "galv", { k: 0, lod: 1, n: 6, r: [90, 0, 0] });
  return pdone(A, { cover: [cover([-0.7, 0, 0.7], 180, "high", ["left"]), cover([0.7, 0, 0.7], 180, "high", ["right"]), cover([-0.7, 0, -0.75], 0, "high", ["left"]), cover([0.7, 0, -0.75], 0, "high", ["right"])] });
}

export function trainingFrame() {
  const A = mkP("mod_nt_training_frame", "Obstacle climbing frame", "Military", "Obstacle-course climbing frame, 5.8 x 2.6 m and 4 m high: six timber posts with steel shoes, top beams and cross beams, a cargo net of tarred rope hung down the front face, a horizontal crawl net across the top, a rung ladder up one end, a balance log at 2 m and low log hurdles at both ends.",
    [6, 3]);
  const X = [-2.8, 0, 2.8], Z = [-1.2, 1.2], HT = 4.0;
  for (const x of X) for (const z of Z) {
    A.cyl([x, (HT + 0.1) / 2 - 0.25, z], [0.2, HT + 0.35, 0.2], "post_wood", { seg: 10 });
    A.box([x - 0.13, 0, z - 0.13], [x + 0.13, 0.3, z + 0.13], "galv", L1);
  }
  for (const z of Z) A.box([-2.95, HT - 0.1, z - 0.09], [2.95, HT + 0.08, z + 0.09], "post_wood", { bv: 0.01 });
  for (const x of X) A.box([x - 0.09, HT - 0.28, -1.3], [x + 0.09, HT - 0.1, 1.3], "post_wood", { bv: 0.01 });
  for (const x of X) for (const z of Z) for (const s of [-1, 1]) if (Math.abs(x + s * 0.6) < 2.95) bar(A, [x, HT - 0.7, z], [x + s * 0.6, HT - 0.1, z], 0.08, 0.08, "post_wood", K0);   // knee braces
  // cargo net on the front, sloping out to ground pegs
  const top = 3.9, zt = 1.32, zb = 1.45;
  for (let x = -2.6; x <= 2.61; x += 0.32) rod(A, [x, top, zt], [x, 0.05, zb], 0.025, "rope", { seg: 5 });
  for (let y = 0.3; y < top; y += 0.32) { const z = zt + ((top - y) / top) * (zb - zt); rod(A, [-2.65, y, z], [2.65, y, z], 0.025, "rope", { seg: 5 }); }
  for (let x = -2.6; x <= 2.61; x += 1.3) A.box([x - 0.03, 0, zb - 0.03], [x + 0.03, 0.1, zb + 0.03], "galv", L1);
  // crawl net over the top between the beams
  for (let x = -2.6; x <= 2.61; x += 0.4) rod(A, [x, HT + 0.05, -1.15], [x, HT - 0.1, 1.15], 0.022, "netting", { seg: 4 });
  for (let z = -1.0; z <= 1.01; z += 0.4) rod(A, [-2.7, HT + 0.0, z], [2.7, HT + 0.0, z], 0.022, "netting", { seg: 4 });
  // end ladder, balance log, hurdles
  for (let y = 0.4; y < HT - 0.3; y += 0.4) A.cyl([-2.8, y, 0], [0.05, 2.3, 0.05], "post_wood", { k: 0, seg: 6, r: [90, 0, 0] });
  A.cyl([0, 2.0, 0], [0.24, 5.4, 0.24], "post_wood", { seg: 12, r: [0, 0, 90] });
  for (const x of [-2.6, 2.6]) for (const s of [-1, 1]) A.box([x + s * 0.0 - 0.06, 1.75, s * 1.2 - 0.06], [x + 0.06, 2.0, s * 1.2 + 0.06], "galv", L1);
  for (const s of [-1, 1]) {
    const xh = s * 2.4;
    for (const z of [-0.8, 0.8]) A.box([xh - 0.08, 0, z - 0.08], [xh + 0.08, 0.9, z + 0.08], "post_wood", { bv: 0.01 });
    for (const y of [0.3, 0.62]) A.cyl([xh, y, 0], [0.3, 1.9, 0.3], "post_wood", { seg: 10, r: [90, 0, 0] });
  }
  return pdone(A, { cover: [cover([-1.9, 0, 0], 270, "low", ["over"]), cover([1.9, 0, 0], 90, "low", ["over"]), cover([-3.0, 0, 0], 90, "low", ["over"]), cover([3.0, 0, 0], 270, "low", ["over"]),
    cover([-2.8, 0, 1.6], 180, "high", ["left", "right"]), cover([2.8, 0, -1.6], 0, "high", ["left", "right"])] });
}

export function crawlFrame() {
  const A = mkP("mod_nt_crawl_frame", "Barbed-wire crawl frame", "Military", "Low barbed-wire crawl obstacle, 5.6 x 2.6 m: a sand bed edged with timber kerbs, a grid of short steel pickets carrying barbed wire strands 0.5 m above the sand, criss-crossed, and low earth banks along both sides.",
    [6, 3]);
  A.box([-2.8, -0.05, -1.1], [2.8, 0.06, 1.1], "sand", { bv: 0.02 });
  for (const s of [-1, 1]) { A.box([-2.85, 0, s * 1.15 - 0.07], [2.85, 0.15, s * 1.15 + 0.07], "timber", { bv: 0.01 }); A.rbox([-2.9, -0.1, Math.min(s * 1.25, s * 1.5)], [2.9, 0.75, Math.max(s * 1.25, s * 1.5)], 0.12, "dirt"); }
  for (const x of [-2.85, 2.85]) A.box([x - 0.07, 0, -1.1], [x + 0.07, 0.15, 1.1], "timber", { bv: 0.01 });
  const XS = [-2.6, -1.3, 0, 1.3, 2.6], ZS = [-1.0, 0, 1.0], wy = 0.5;
  for (const x of XS) for (const z of ZS) { A.box([x - 0.02, 0, z - 0.02], [x + 0.02, wy + 0.08, z + 0.02], "steel_dk", K0); A.box([x - 0.03, wy - 0.03, z - 0.03], [x + 0.03, wy + 0.02, z + 0.03], "steel_dk", L1); }
  const strand = (a, b) => {
    rod(A, a, b, 0.005, "wire", { seg: 4 }); rod(A, [a[0], a[1] + 0.006, a[2]], [b[0], b[1] + 0.006, b[2]], 0.004, "wire", { seg: 4 });
    const L = Math.hypot(b[0] - a[0], b[2] - a[2]);
    for (let t = 0.12; t < L; t += 0.15) { const p = lerp3(a, b, t / L); A.box([p[0] - 0.012, p[1] - 0.012, p[2] - 0.012], [p[0] + 0.012, p[1] + 0.012, p[2] + 0.012], "wire", L1); }
  };
  for (const z of ZS) strand([-2.6, wy, z], [2.6, wy, z]);
  for (const x of XS) strand([x, wy, -1.0], [x, wy, 1.0]);
  for (let i = 0; i < 4; i++) { strand([XS[i], wy - 0.02, -1.0], [XS[i + 1], wy - 0.02, 1.0]); strand([XS[i], wy - 0.02, 1.0], [XS[i + 1], wy - 0.02, -1.0]); }
  return pdone(A, { cover: [cover([-1.2, 0, 1.0], 0, "low", ["over"]), cover([1.2, 0, 1.0], 0, "low", ["over"]), cover([-1.2, 0, -1.0], 180, "low", ["over"]), cover([1.2, 0, -1.0], 180, "low", ["over"]),
    cover([0, 0, 1.95], 180, "low", ["over"]), cover([0, 0, -1.95], 0, "low", ["over"])] });
}

// ============================================================================ INDUSTRIAL: warehouse
export function warehouse() {
  const A = mkN("mod_nt_warehouse", "Portal-frame warehouse", "Large portal-frame warehouse, 36 x 21 m with 9.2 m eaves and a low-pitch corrugated roof with roof lights: ribbed steel cladding over a precast base, clerestory glazing, three 4 m roller doors (one open) and a personnel door onto a built-in 1.2 m raised loading dock along the front, with a ramp at the west end, steps at the east end, bumpers and a steel canopy. Inside: an open floor at dock height with painted aisles and four runs of pallet racking, steel portal frames, and an office block in the rear-west corner (two ground-floor offices) whose roof is a mezzanine with an upper office, reached by a guarded stair.",
    1, [36, 24], IND);
  A.mat("roof_liner", [0.55, 0.56, 0.57], 0.5, "metal", { texture: "corrugated_galv", metallic: 0.5 });   // AS1: the shed roof underside (it used to re-tint "ceiling", which darkened the office ceilings too)
  const gf = 1.2, h = 8.0, x0 = -18, x1 = 18, z0 = -12, z1 = 9, MZ = gf + 3.4, OF = [-17.7, -9.6, -11.7, -5.6];
  const w = wing(A, {
    tag: "wh", x0, x1, z0, z1, n: 1, h, gf, par: 0, band: null, skin: () => "corrugated", fm: "slab",
    doors: [
      { id: "roller_1", side: "front", c: -10, w: 4.0, h: 4.5, roller: true, open: true, room: "floor_w", to: "dock" },
      { id: "roller_2", side: "front", c: -2, w: 4.0, h: 4.5, roller: true, room: "floor_main", to: "dock" },
      { id: "roller_3", side: "front", c: 6, w: 4.0, h: 4.5, roller: true, room: "floor_main", to: "dock" },
      { id: "dock_door", side: "front", c: 12.5, w: 1.0, h: 2.2, room: "floor_main", to: "dock" }],
    glaze: () => ({
      front: [[-17, -13.5, 6.0, 7.6, 3.5], [14, 17, 6.0, 7.6, 3.0]],
      rear: [[-16.6, -14.8, 0.9, 2.4, 1.8], [-12.6, -10.8, 0.9, 2.4, 1.8], [-16.6, -14.8, 4.3, 5.8, 1.8], [-8, 16, 6.0, 7.6, 4.0]],
      east: [[-10, 7, 6.0, 7.6, 3.4]], west: [[-11.2, -9.6, 0.9, 2.4, 1.6], [-11.0, -9.4, 4.3, 5.8, 1.6], [-4, 7.5, 6.0, 7.6, 3.8]],
    }),
    plan: (f, { w }) => ({ rooms: [{ id: "floor_main", type: "utility", x0: -9.6, z0: w.iz0, x1: w.ix1, z1: w.iz1 }, { id: "floor_w", type: "workshop", x0: w.ix0, z0: OF[3] + 0.1, x1: -9.6, z1: w.iz1 }], walls: [] }),
  });
  for (const p of A.parts) if (p.m === "ceiling") p.m = "roof_liner";   // the wing ceiling slab is the shed roof underside
  const top = w.top, ceil = top - SLAB;
  // pitched roof over the flat ceiling slab: two sheets, ridge, gables, gutters, roof lights
  const RI = 10.6, zc = (z0 + z1) / 2, half = (z1 - z0) / 2 + 0.3, ang = Math.atan2(RI - top, half);
  for (const s of [-1, 1]) {
    const za = zc + s * half, zb = zc;
    bar(A, [0, top + 0.03, za], [0, RI + 0.03, zb], 36.4, 0.06, "corrugated");
    for (let x = -14; x <= 14; x += 7) bar(A, [x, top + 0.12 + (RI - top) * 0.3, za - s * half * 0.3], [x, top + 0.12 + (RI - top) * 0.7, za - s * half * 0.7], 1.4, 0.02, "glass_int", K0);
    A.box([-18.2, top - 0.2, Math.min(za, za + s * 0.2)], [18.2, top + 0.05, Math.max(za, za + s * 0.2)], "alu", L1);   // gutter
  }
  A.box([-18.2, RI - 0.02, zc - 0.25], [18.2, RI + 0.12, zc + 0.25], "alu_lt", { k: 0, bv: 0.01 });
  for (const [p0, p1] of [[-18.0, -17.7], [17.7, 18.0]]) A.prism(0, p0, p1, [[z0, top], [z1, top], [zc, RI]], "corrugated");
  for (const [x, z] of [[-17.9, z0 + 0.1], [17.9, z0 + 0.1], [-17.9, z1 - 0.1], [17.9, z1 - 0.1]]) A.box([x - 0.07, 0, z - 0.07], [x + 0.07, top, z + 0.07], "alu", L1);   // downpipes
  // exterior: precast base course, cladding ribs, lights, cameras
  for (const s of SIDES) {
    const G = sideF(w, s), isX = G.axis === "x";
    fbox(A, G, isX ? x0 - 0.03 : z0 + 0.005, isX ? x1 + 0.03 : z1 - 0.005, -0.3, gf + 1.3, -0.012, 0.05, "precast", { k: 0, bv: 0.01 });
    for (let a = (isX ? x0 : z0) + 3; a < (isX ? x1 : z1) - 1; a += 6) fbox(A, G, a - 0.1, a + 0.1, gf + 1.3, top, 0, 0.1, "panel_dk", L1);
  }
  const Ff = sideF(w, "front");
  for (const c of [-10, -2, 6]) { wlamp(A, Ff, c - 2.6, 5.9); fbox(A, Ff, c - 0.9, c + 0.9, 7.0, 7.6, 0, 0.04, "sign_blank", L1); }
  camera(A, Ff, 15.5, 6.5);
  // loading dock: platform, nosing, bumpers, ramp (west), steps (east), kerbs and handrails, canopy, bollards
  const DX0 = -13.5, DX1 = 15.0, DZ = 12.0;
  A.box([DX0, -0.3, z1], [DX1, gf, DZ], "conc_ag", { bv: 0.02 });
  A.box([DX0, gf - 0.08, DZ - 0.08], [DX1, gf + 0.004, DZ + 0.01], "steel_dk", L1);
  for (let x = DX0 + 1.2; x < DX1; x += 4.5) for (const e of [-0.6, 0.6]) A.box([x + e - 0.15, 0.55, DZ], [x + e + 0.15, 1.05, DZ + 0.15], "rubber", { bv: 0.02 });
  for (let x = DX0 + 0.5; x < DX1; x += 2.0) A.box([x - 0.6, gf, DZ - 0.25], [x + 0.6, gf + 0.004, DZ - 0.15], "line_yel", L1);
  A.wedge([-15.75, gf / 2, 10.7], [2.6, gf, 4.5], "conc_ag", { r: [0, -90, 0] });
  for (const z of [9.35, 12.05]) A.prism(2, z - 0.05, z + 0.05, [[-18, 0], [DX0, gf], [DX0, gf + 0.12]], "precast");
  stairRail(A, [-17.8, 0.05, 12.0], [DX0, gf, 12.0], "safety_yel");
  const nS = 7, rS = gf / nS, gS = 0.3, SZ0 = 9.2, SZ1 = 10.7;
  for (let i = 0; i < nS - 1; i++) A.box([DX1 + i * gS, 0, SZ0], [DX1 + (i + 1) * gS, gf - (i + 1) * rS, SZ1], "conc_ag", { k: 0, bv: 0.01 });
  bar(A, [DX1 + 0.15, gf - 0.2, (SZ0 + SZ1) / 2], [DX1 + nS * gS - 0.15, -0.2 + 0.0, (SZ0 + SZ1) / 2], SZ1 - SZ0, 0.4, "conc_ag", H0);
  for (const z of [SZ1 + 0.03]) stairRail(A, [DX1, gf, z], [DX1 + (nS - 1) * gS, rS, z]);
  guard(A, "z", SZ1 + 0.05, DZ, DX1 + 0.02, gf);
  canopy(A, { x0: DX0, x1: DX1, z0: z1, z1: 11.4, y: 6.2, m: "panel_lt", fascia: "panel_acc" });
  for (let x = DX0 + 2; x < DX1; x += 6) bar(A, [x, 9.0, z1], [x, 6.45, 11.2], 0.04, 0.04, "alu", L1);
  for (const x of [-17.6, 17.6]) { A.cyl([x, 0.5, 11.6], [0.2, 1.0, 0.2], "safety_yel", { seg: 10 }); }
  // interior: portal frames, highbay lights, aisle paint, racking runs
  for (let z = z0 + 0.3; z < z1; z += 6.9) {
    for (const x of [-17.55, 17.55]) A.box([x - 0.12, gf, z - 0.25], [x + 0.12, ceil, z + 0.25], "olive");
    A.box([-17.5, ceil - 0.6, z - 0.12], [17.5, ceil, z + 0.12], "olive", L1);
    for (const s of [-1, 1]) A.prism(2, z - 0.12, z + 0.12, [[s * 17.43, ceil - 0.6], [s * 17.43, ceil - 1.6], [s * 15.4, ceil - 0.6]], "olive", { k: 0 });
  }
  const PL = (a, b, c, d) => A.box([a, gf, c], [b, gf + 0.004, d], "line_yel", L1);
  for (const x of [-1.0, 3.9, 9.1, 14.0]) PL(x - 0.05, x + 0.05, -11.0, 3.0);
  PL(-9.0, 17.0, 3.6, 3.7); PL(-9.0, 17.0, 6.9, 7.0);
  for (const [xc, dd] of [[1.5, 2], [6.5, 2], [11.5, 2], [16.6, 1]]) {
    const xa = dd === 2 ? xc - 1.1 : xc - 0.5, xb = dd === 2 ? xc + 1.1 : 17.5, za = -11.0, zb = 2.6;
    A.box([xa, gf, za], [xb, gf + 6.0, zb], "rack_bl", H0);
    const ups = dd === 2 ? [xa, xc - 0.1, xc + 0.1, xb] : [xa, xb - 0.05];
    for (let z = za; z <= zb + 0.01; z += (zb - za) / 5) {
      for (const x of ups) A.box([x - 0.04, gf, z - 0.04], [x + 0.04, gf + 6.0, z + 0.04], "rack_bl", K0);
      for (let y = 0.6; y < 6; y += 1.2) A.box([xa, gf + y - 0.015, z - 0.02], [xb, gf + y + 0.015, z + 0.02], "rack_bl", L1);   // frame bracing
      A.box([xa - 0.02, gf, z - 0.12], [xa + 0.02, gf + 0.4, z + 0.12], "safety_yel", L1);   // upright guard
    }
    for (const by of [1.6, 3.2, 4.8]) for (const x of [xa, xb, ...(dd === 2 ? [xc - 0.1, xc + 0.1] : [])]) A.box([x - 0.05, gf + by - 0.12, za], [x + 0.05, gf + by, zb], "rack_or", K0);
    let k = 0;
    for (let z = za + 0.1; z < zb - 1.3; z += (zb - za) / 5) for (const by of [0, 1.6, 3.2, 4.8]) for (const [pa, pb] of dd === 2 ? [[xa + 0.05, xc - 0.15], [xc + 0.15, xb - 0.05]] : [[xa + 0.05, xb - 0.1]]) {
      if ((k++ * 7) % 5 === 0) continue;
      const zz = z + 0.15, hb = 0.9 + ((k * 3) % 4) * 0.1;
      A.box([pa, gf + by, zz], [pb, gf + by + 0.14, zz + 2.2], "pallet", K0);
      A.box([pa + 0.04, gf + by + 0.14, zz + 0.05], [pb - 0.04, gf + by + 0.14 + hb, zz + 2.15], k % 3 ? "carton" : "shrink", K0);
    }
  }
  // office block (two offices) under a mezzanine deck with an upper office; guarded stair; glazed hatches
  const [ox0, ox1, oz0, oz1] = OF, ot = 0.15;
  pwall(A, { axis: "x", d: oz1 - 0.05, a0: ox0, a1: ox1, t: ot, holes: [{ c: -15.0, w: 1.0, a: "office_g1", b: "floor_w" }, { c: -12.4, w: 1.0, a: "office_g2", b: "floor_w" }, { c: -16.8, w: 1.2, y0: 0.9, h: 2.2 }] }, gf, MZ - SLAB);
  pwall(A, { axis: "z", d: ox1 - 0.05, a0: oz0, a1: oz1 + 0.025, t: ot, holes: [{ c: -8.6, w: 1.6, y0: 0.9, h: 2.2 }] }, gf, MZ - SLAB);
  pwall(A, { axis: "z", d: -13.7, a0: oz0, a1: oz1 - 0.125, t: 0.12, holes: [] }, gf, MZ - SLAB);
  A.box([ox0, MZ - SLAB, oz0], [ox1, MZ, oz1 + 0.03], "floor_x");
  A.box([ox0, MZ - SLAB - 0.02, oz0], [ox1, MZ - SLAB, oz1 + 0.03], "ceiling", K0);
  A.box([ox0, MZ - SLAB - 0.05, oz1 + 0.03], [ox1, MZ + 0.08, oz1 + 0.08], "olive", { bv: 0.005 });   // deck edge beam
  pwall(A, { axis: "z", d: -13.7, a0: oz0, a1: oz1 - 0.125, t: 0.12, holes: [{ c: -7.0, w: 1.0, a: "office_m", b: "mezz" }] }, MZ, MZ + 2.85);
  pwall(A, { axis: "x", d: oz1 - 0.05, a0: ox0, a1: -13.64, t: ot, holes: [{ c: -15.8, w: 2.0, y0: 0.9, h: 2.1 }] }, MZ, MZ + 2.85);
  A.box([ox0, MZ + 2.85, oz0], [-13.64, MZ + 3.0, oz1 + 0.025], "panel_lt", { bv: 0.01 });
  A.box([ox0, MZ + 2.83, oz0], [-13.64, MZ + 2.85, oz1 + 0.025], "ceiling", K0);
  for (const [axis, c, d, ww, y] of [["x", -16.8, oz1 - 0.05, 1.2, gf + 0.9], ["z", -8.6, ox1 - 0.05, 1.6, gf + 0.9], ["x", -15.8, oz1 - 0.05, 2.0, MZ + 0.9]]) {
    wbox(A, axis, c - ww / 2, c + ww / 2, y, y + (axis === "x" && y > MZ ? 2.1 : 2.2), d - 0.008, d + 0.008, "glass_int");
    wbox(A, axis, c - ww / 2 - 0.04, c + ww / 2 + 0.04, y - 0.04, y, d - 0.1, d + 0.1, "alu", L1);
  }
  const sx0 = -11.2, sx1 = -10.1, nR = 19, run = nR * 0.26, sa0 = oz1 + run;
  flight(A, { axis: "z", b0: sx0, b1: sx1, a0: sa0, dir: -1, y0: gf, rise: MZ - gf, n: nR, run, m: "chequer", soffit: "olive" });
  for (const x of [sx0 - 0.03, sx1 + 0.03]) stairRail(A, [x, gf, sa0], [x, MZ, oz1]);
  guard(A, "x", -13.64, sx0, oz1 + 0.06, MZ); guard(A, "x", sx1, ox1, oz1 + 0.06, MZ); guard(A, "z", oz0, oz1 + 0.06, ox1 + 0.06, MZ);
  addRoom(A, "office_g1", "office_modern", ox0, gf, oz0, -13.76, MZ - SLAB, oz1 - 0.125);
  addRoom(A, "office_g2", "office_modern", -13.64, gf, oz0, ox1 - 0.125, MZ - SLAB, oz1 - 0.125);
  addRoom(A, "mezz", "office_modern", -13.64, MZ, oz0, ox1, MZ + 2.8, oz1);
  addRoom(A, "office_m", "office_modern", ox0, MZ, oz0, -13.76, MZ + 2.83, oz1 - 0.125);
  addRoom(A, "dock", "utility", DX0, gf, z1 + 0.05, DX1, gf + 3, DZ);
  navDoor(A, "floor_w", "mezz", [(sx0 + sx1) / 2, gf, sa0 + 0.3]);
  navDoor(A, "floor_w", "floor_main", [-9.6, gf, 2.0]);
  A._nav.entrances.push({ p: [-18.6, 0, 10.7], dir: [1, 0, 0], room: "dock" }, { p: [DX1 + 2.6, 0, (SZ0 + SZ1) / 2], dir: [-1, 0, 0], room: "dock" });
  A._nav.cover.push(...[-13, -6, 2, 10].map((x) => cover([x, 0, DZ + 0.6], 180, "low", ["over"])));
  coverRing(A, x0, x1, z0, z1);
  return done(A);
}

// ============================================================================ INDUSTRIAL: workshop unit
export function workshopUnit() {
  const A = mkN("mod_nt_workshop", "Industrial workshop unit", "Small industrial unit, 18 x 10.4 m and 5.3 m to the parapet: ribbed steel cladding over a brick base, a 4 m roller door (open) and a personnel door on the front, clerestory and office windows. Inside: an open workshop floor with fixed steel workbenches, a pegboard wall, a pillar drill stand and floor markings, and a small partitioned office in the rear-east corner.",
    1, [18, 12], IND);
  const x0 = -9, x1 = 9, z0 = -5.2, z1 = 5.2;
  const w = wing(A, {
    tag: "ws", x0, x1, z0, z1, n: 1, h: 5.0, par: 0.6, band: null, skin: () => "corrugated", fm: "slab",
    doors: [{ id: "roller", side: "front", c: -3.5, w: 4.0, h: 4.0, roller: true, open: true, room: "shop" },
      { id: "front_door", side: "front", c: 3.5, w: 1.0, h: 2.2, room: "shop" }],
    glaze: () => ({ front: [[5.4, 8.2, 1.0, 2.6, 2.8], [-8.5, -6.0, 3.2, 4.4, 2.5]], rear: [[5.4, 8.2, 1.0, 2.6, 2.8], [-8.0, 3.0, 3.2, 4.4, 3.7]], east: [[-4.0, -1.8, 1.0, 2.6, 2.2], [0.5, 4.5, 3.2, 4.4, 4.0]], west: [[-4.5, 4.5, 3.2, 4.4, 3.0]] }),
    plan: (f, { w }) => ({
      rooms: [{ id: "shop", type: "workshop", x0: w.ix0, z0: -0.94, x1: w.ix1, z1: w.iz1 }, { id: "shop_rear", type: "workshop", x0: w.ix0, z0: w.iz0, x1: 4.54, z1: -0.94 },
        { id: "office", type: "office_modern", x0: 4.66, z0: w.iz0, x1: w.ix1, z1: -1.06 }],
      walls: [{ axis: "x", d: -1.0, a0: 4.6, a1: w.ix1, holes: [{ c: 6.6, w: 0.9, a: "office", b: "shop" }] }, { axis: "z", d: 4.6, a0: w.iz0, a1: -1.06, holes: [{ c: -3.0, w: 1.6, y0: 1.0, h: 2.1 }] }],
      doors: [{ a: "shop", b: "shop_rear", p: [-2, -0.94] }],
    }),
    roof: (A, w) => { hvac(A, 6.5, -3, w.top, 2.0, 1.2); A.cyl([-6, w.top + 0.6, -2], [0.4, 1.2, 0.4], "louvre", { seg: 12 }); A.cone([-6, w.top + 1.3, -2], [0.6, 0.2, 0.6], "alu", { k: 0, top: 0.1, seg: 12 }); },
    dress: (A, w) => {
      const y = GF, Ff = sideF(w, "front");
      for (const s of SIDES) { const G = sideF(w, s), isX = G.axis === "x"; fbox(A, G, isX ? x0 - 0.02 : z0 + 0.005, isX ? x1 + 0.02 : z1 - 0.005, -0.3, 1.4, -0.012, 0.04, "brick_grey", { k: 0, bv: 0.01 }); }
      for (let a = -7.5; a < 9; a += 3) if (Math.abs(a + 3.5) > 2.3 && Math.abs(a - 3.5) > 0.7) fbox(A, Ff, a - 0.08, a + 0.08, 1.4, w.top, 0, 0.08, "panel_dk", L1);
      fbox(A, Ff, -1.2, 2.4, 4.6, 5.1, 0, 0.05, "sign_blank", L1); wlamp(A, Ff, 3.5, 2.8); wlamp(A, Ff, -6.0, 4.6);
      for (const a of [-8.8, 8.8]) fbox(A, Ff, a - 0.06, a + 0.06, 0, w.top + 0.5, 0, 0.12, "alu", L1);
      for (const x of [-6.3, -0.7]) { A.cyl([x, 0.5, 5.75], [0.2, 1.0, 0.2], "safety_yel", { seg: 10 }); }
      // fixed workbenches along the west wall and the rear wall, pegboards, a pillar drill, floor lines
      for (const [ax, a0, a1, d, s] of [["z", -4.6, 3.6, -8.7, 1], ["x", -8.2, 3.8, -4.9, 1]]) {
        const B = (p0, p1, yy0, yy1, e0, e1, m, o) => wbox(A, ax, p0, p1, yy0, yy1, d + s * e0, d + s * e1, m, o);
        B(a0, a1, y, y + 0.86, 0, 0.75, "olive", { bv: 0.01 }); B(a0 - 0.02, a1 + 0.02, y + 0.86, y + 0.92, -0.01, 0.78, "laminate", { bv: 0.005 });
        B(a0 + 0.05, a1 - 0.05, y + 0.12, y + 0.76, 0.74, 0.76, "panel_dk", L1);
        B(a0, a1, y + 1.25, y + 2.3, 0, 0.02, "timber_raw", L1);
        for (let a = a0 + 0.35; a < a1; a += 0.5) B(a - 0.07, a + 0.07, y + 1.45 + ((a * 13) % 0.6), y + 1.6 + ((a * 13) % 0.6), 0.02, 0.06, "steel_dk", L1);
        B(a1 - 0.5, a1 - 0.2, y + 0.92, y + 1.06, 0.45, 0.75, "steel_dk", L1);   // vice
      }
      A.box([-2.0, y, -4.5], [-1.5, y + 0.06, -4.0], "steel_dk", L1); A.cyl([-1.75, y + 0.9, -4.3], [0.1, 1.8, 0.1], "steel_dk", { seg: 8 }); A.box([-2.0, y + 1.5, -4.55], [-1.5, y + 1.95, -4.05], "olive", { k: 0, bv: 0.01 });
      const Lm = (a, b, c, d) => A.box([a, y, c], [b, y + 0.004, d], "line_yel", L1);
      Lm(-7.6, 3.4, -3.9, -3.8); Lm(-7.6, -7.5, -3.8, 4.8); Lm(-5.6, -1.4, 0.2, 0.3); Lm(-5.6, -5.5, 0.3, 4.8); Lm(-1.5, -1.4, 0.3, 4.8);
    },
  });
  coverRing(A, x0, x1, z0, z1);
  doorTour(A, "front_door", 3.5, z1, z1 - 0.3, GF, "mod_nt_workshop_floor");
  return done(A);
}

// ============================================================================ INDUSTRIAL: storage tank
export function storageTank() {
  const A = mkP("mod_nt_storage_tank", "Bunded storage tank", "Industrial", "Vertical welded steel storage tank, 4 m diameter and 7.2 m tall, on a concrete ring base inside a 1.1 m precast bund wall (5.8 m square): plate courses with weld seams, a shallow cone roof with a vent and a guard-railed roof walkway, a caged ladder from the bund floor, a level gauge board, an outlet pipe with flange and valve through the bund, a step-over stile and blank hazard panels.",
    [6, 6]);
  const R = 2.0, TH = 7.2, BW = 2.9, BH = 1.1;
  A.box([-BW, -0.2, -BW], [BW, 0.05, BW], "conc_rough", { bv: 0.02 });
  for (const [a, b, c, d] of [[-BW, BW, -BW, -BW + 0.2], [-BW, BW, BW - 0.2, BW], [-BW, -BW + 0.2, -BW + 0.2, BW - 0.2], [BW - 0.2, BW, -BW + 0.2, BW - 0.2]]) {
    A.box([a, 0.05, c], [b, BH, d], "conc_fw", { bv: 0.015 });
    A.box([a - 0.02, BH, c - 0.02], [b + 0.02, BH + 0.06, d + 0.02], "precast", { k: 0, bv: 0.01 });
  }
  for (const s of [-1, 1]) for (const t of [-1.0, 1.0]) { A.box([t - 0.01, 0.05, s * BW - 0.005], [t + 0.01, BH, s * BW + 0.005], "conc_break", L1); A.box([s * BW - 0.005, 0.05, t - 0.01], [s * BW + 0.005, BH, t + 0.01], "conc_break", L1); }
  A.box([-0.4, 0.4, BW], [0.4, 0.9, BW + 0.02], "safety_yel", L1); A.box([0.6, 0.4, BW], [1.4, 0.9, BW + 0.02], "red_paint", L1);
  A.cyl([0, 0.15, 0], [2 * R + 0.3, 0.2, 2 * R + 0.3], "conc_ag", { seg: 32 });
  A.cyl([0, 0.25 + TH / 2, 0], [2 * R, TH, 2 * R], "tank_paint", { seg: 32 });
  for (let y = 0.25 + 1.5; y < TH; y += 1.5) A.cyl([0, y, 0], [2 * R + 0.012, 0.012, 2 * R + 0.012], "steel_dk", { k: 0, lod: 1, seg: 32 });
  for (let i = 0; i < 8; i++) { const a = (i / 8) * 2 * Math.PI + (i % 2) * 0.2; A.obox([R * Math.sin(a), 0.25 + TH / 2, R * Math.cos(a)], [0.012, TH, 0.012], [0, a * DEG, 0], "steel_dk", L1); }
  A.cone([0, 0.25 + TH + 0.2, 0], [2 * R + 0.06, 0.4, 2 * R + 0.06], "tank_paint", { seg: 32, top: 0.08 });
  A.cyl([0, 0.25 + TH + 0.5, 0], [0.3, 0.3, 0.3], "steel_dk", { k: 0, seg: 10 }); A.cone([0, 0.25 + TH + 0.72, 0], [0.45, 0.15, 0.45], "steel_dk", { k: 0, seg: 10, top: 0.1 });
  // roof walkway guard ring (posts and rails), caged ladder on the +x side
  const ry = 0.25 + TH, lx = R + 0.25;
  for (let i = 0; i < 12; i++) { const a = (i / 12) * 2 * Math.PI; A.box([R * 0.95 * Math.sin(a) - 0.02, ry, R * 0.95 * Math.cos(a) - 0.02], [R * 0.95 * Math.sin(a) + 0.02, ry + 1.1, R * 0.95 * Math.cos(a) + 0.02], "safety_yel", L1); }
  for (const y of [0.55, 1.1]) for (let i = 0; i < 12; i++) { const a0 = (i / 12) * 2 * Math.PI, a1 = ((i + 1) / 12) * 2 * Math.PI, r = R * 0.95; bar(A, [r * Math.sin(a0), ry + y, r * Math.cos(a0)], [r * Math.sin(a1), ry + y, r * Math.cos(a1)], 0.04, 0.04, "safety_yel", L1); }
  for (const s of [-1, 1]) A.box([lx - 0.03, 0.05, s * 0.23 - 0.03], [lx + 0.03, ry + 1.1, s * 0.23 + 0.03], "galv", K0);
  for (let y = 0.35; y < ry; y += 0.3) A.cyl([lx, y, 0], [0.025, 0.46, 0.025], "galv", { k: 0, lod: 1, seg: 6, r: [90, 0, 0] });
  for (let y = 2.4; y < ry + 1.0; y += 0.7) A.arch([lx + 0.05, y, 0], [0.7, 0.45, 0.04], "galv", { k: 0, lod: 1, th: 0.03, r: [90, 90, 0] });
  for (const s of [-1, 1]) for (const yy of [0.2, TH * 0.5, TH - 0.2]) A.box([R - 0.02, yy, s * 0.23 - 0.03], [lx, yy + 0.06, s * 0.23 + 0.03], "galv", L1);
  // outlet pipe, flange, valve through the bund (+z), level gauge
  A.cyl([0.8, 0.6, (R + BW + 0.3) / 2], [0.2, BW - R + 0.3, 0.2], "steel_dk", { k: 0, seg: 12, r: [90, 0, 0] });
  A.cyl([0.8, 0.6, R + 0.08], [0.36, 0.06, 0.36], "steel_dk", { k: 0, lod: 1, seg: 12, r: [90, 0, 0] });
  A.cyl([0.8, 0.6, BW + 0.25], [0.36, 0.06, 0.36], "steel_dk", { k: 0, lod: 1, seg: 12, r: [90, 0, 0] });
  A.box([0.65, 0.45, BW + 0.08], [0.95, 0.75, BW + 0.24], "red_paint", L1); A.cyl([0.8, 0.95, BW + 0.16], [0.3, 0.03, 0.3], "red_paint", { k: 0, lod: 1, seg: 12 });
  A.box([-R - 0.02, 0.8, -0.1], [-R + 0.02, TH - 0.2, 0.1], "paint_w", L1); A.box([-R - 0.04, 3.4, -0.06], [-R, 3.6, 0.06], "safety_yel", L1);
  // step-over stile on the +x bund wall
  for (const s of [-1, 1]) for (let i = 0; i < 3; i++) A.box([s > 0 ? BW + i * 0.18 : BW - 0.2 - (i + 1) * 0.18, 0, -0.45], [s > 0 ? BW + (i + 1) * 0.18 : BW - 0.2 - i * 0.18, BH - (i + 1) * 0.3 + 0.05, 0.45], "chequer", { k: 0, bv: 0.005 });
  A.box([BW - 0.74, 0, -0.45], [BW + 0.54, BH - 0.25, 0.45], "chequer", H0);
  for (const z of [-0.47, 0.47]) stairRail(A, [BW - 0.75, 0.05, z], [BW + 0.55, 0.05, z], "safety_yel", false);
  return pdone(A, { cover: boxCover(BW, BW, BH, { step: 1.9 }) });
}

// ============================================================================ RAIL
const RT = 0.572, GAUGE = 0.7525;
function track(A, x0, x1, hz = 2.0) {
  extrudeX(A, x0, x1, [[-hz, 0], [hz, 0], [1.55, 0.35], [-1.55, 0.35]], "ballast");
  for (let x = x0 + 0.3; x < x1; x += 0.6) {
    A.box([x - 0.13, 0.22, -1.25], [x + 0.13, 0.4, 1.25], "sleeper", { k: 0, bv: 0.015 });
    for (const e of [-GAUGE, GAUGE]) {
      A.box([x - 0.09, 0.4, e - 0.1], [x + 0.09, 0.41, e + 0.1], "rubber", L1);
      for (const s of [-1, 1]) { A.box([x - 0.04, 0.41, e + s * 0.085 - 0.02], [x + 0.04, 0.45, e + s * 0.085 + 0.02], "steel_dk", L1); A.cyl([x, 0.43, e + s * 0.12], [0.04, 0.05, 0.04], "steel_dk", { k: 0, lod: 1, n: 6 }); }
    }
  }
  for (const e of [-GAUGE, GAUGE]) {
    A.box([x0, 0.41, e - 0.075], [x1, 0.425, e + 0.075], "rail_steel", K0);
    A.box([x0, 0.425, e - 0.009], [x1, RT - 0.045, e + 0.009], "rail_steel", K0);
    A.box([x0, RT - 0.045, e - 0.036], [x1, RT, e + 0.036], "rail_steel", K0);
    A.box([x0, RT - 0.004, e - 0.03], [x1, RT + 0.001, e + 0.03], "lift_steel", L1);   // polished running band
  }
}
export function railTrack() {
  const A = mkP("mod_nt_rail_track", "Rail track (12 m)", "Rail", "One straight 12 m length of standard-gauge (1435 mm) track: a crushed-stone ballast bed with sloped shoulders, twenty concrete sleepers at 0.6 m, flat-bottom rails on rubber pads with spring clips and bolts, a polished running band on the railheads. Walk-over: only the ballast collides.",
    [12, 4]);
  track(A, -6, 6);
  return pdone(A, { effect: "none" });
}
export function railBuffer() {
  const A = mkP("mod_nt_rail_buffer", "Rail buffer stop", "Rail", "Buffer stop at the end of a 4 m stub of standard-gauge track: a steel frame of rail-section struts clamped to the running rails, a buffer beam painted in red and white blocks carrying two spring buffers at 1.75 m centres, a red lamp, and a short rubble end bank.",
    [4, 4]);
  track(A, -2, 2);
  const bx = 1.2, by = RT + 1.06;
  A.box([bx, by - 0.4, -1.35], [bx + 0.3, by + 0.35, 1.35], "red_paint", { bv: 0.02 });
  for (let i = 0; i < 5; i++) A.box([bx - 0.005, by - 0.4, -1.35 + i * 0.54 + 0.27 * 0], [bx, by + 0.35, -1.35 + i * 0.54 + 0.27], "paint_w", L1);
  for (const z of [-0.875, 0.875]) {
    A.cyl([bx - 0.2, by, z], [0.36, 0.4, 0.36], "steel_dk", { seg: 14, r: [0, 0, 90] });
    A.cyl([bx - 0.43, by, z], [0.5, 0.06, 0.5], "galv", { seg: 16, r: [0, 0, 90] });
  }
  for (const e of [-GAUGE, GAUGE]) {
    A.box([bx + 0.05, RT, e - 0.06], [bx + 0.25, by - 0.4, e + 0.06], "steel_dk");
    bar(A, [bx + 0.2, by + 0.2, e], [1.95, RT, e], 0.12, 0.12, "steel_dk", K0);
    bar(A, [bx + 0.2, by - 0.2, e], [-0.6, RT, e], 0.1, 0.1, "steel_dk", K0);
    for (const x of [-0.5, 1.85]) A.box([x - 0.12, RT - 0.08, e - 0.1], [x + 0.12, RT + 0.04, e + 0.1], "steel_dk", L1);   // clamps
  }
  A.box([bx + 0.3, RT, -1.2], [1.98, by + 0.2, 1.2], "steel_dk", H0);
  A.cyl([bx + 0.15, by + 0.55, 0], [0.18, 0.12, 0.18], "lamp_r", { k: 0, seg: 10, r: [0, 0, 90] });
  A.box([bx + 0.2, by + 0.35, -0.08], [bx + 0.3, by + 0.6, 0.08], "steel_dk", L1);
  A.rbox([1.5, 0.2, -1.9], [2.0, 0.8, 1.9], 0.2, "ballast", K0);
  return pdone(A, { cover: [cover([2.4, 0, -0.9], 270, "low", ["over", "left"]), cover([2.4, 0, 0.9], 270, "low", ["over", "right"]), cover([0.4, 0, -0.9], 90, "low", ["over"])] });
}
// rolling stock: wheels sit on the rail head at RT (as mod_nt_rail_track)
function bogie(A, x) {
  const ay = RT + 0.42;
  for (const dx of [-0.9, 0.9]) {
    for (const e of [-0.72, 0.72]) { A.cyl([x + dx, ay, e], [0.84, 0.13, 0.84], "wheel_steel", { k: 0, seg: 20, r: [90, 0, 0] }); A.cyl([x + dx, ay, e + Math.sign(e) * 0.07], [0.5, 0.02, 0.5], "bogie", { k: 0, lod: 1, seg: 16, r: [90, 0, 0] }); }
    A.cyl([x + dx, ay, 0], [0.16, 1.9, 0.16], "bogie", { k: 0, seg: 10, r: [90, 0, 0] });
    for (const e of [-1.0, 1.0]) A.cyl([x + dx, ay, e], [0.26, 0.2, 0.26], "bogie", { k: 0, lod: 1, seg: 10, r: [90, 0, 0] });
  }
  for (const e of [-1.0, 1.0]) {
    A.box([x - 1.4, ay - 0.15, e - 0.08], [x + 1.4, ay + 0.2, e + 0.08], "bogie", { k: 0, bv: 0.02 });
    A.box([x - 0.4, ay - 0.3, e - 0.08], [x + 0.4, ay + 0.25, e + 0.08], "bogie", { k: 0, bv: 0.02 });
    for (const dz of [-0.1, 0.1]) A.cyl([x, ay + 0.3, e + dz], [0.12, 0.25, 0.12], "safety_yel", { k: 0, lod: 1, seg: 8 });   // springs
  }
  A.box([x - 0.3, ay + 0.15, -1.1], [x + 0.3, ay + 0.45, 1.1], "bogie", { k: 0, bv: 0.02 });   // bolster
  A.box([x - 1.4, RT + 0.05, -1.1], [x + 1.4, ay + 0.45, 1.1], "bogie", H0);
}
function coupler(A, x, s) {
  const y = RT + 0.88;
  A.box([Math.min(x, x + s * 0.5), y - 0.12, -0.12], [Math.max(x, x + s * 0.5), y + 0.12, 0.12], "bogie", { k: 0, bv: 0.02 });
  A.box([Math.min(x + s * 0.45, x + s * 0.65), y - 0.2, -0.2], [Math.max(x + s * 0.45, x + s * 0.65), y + 0.2, 0.15], "bogie", { k: 0, bv: 0.03 });
  A.cyl([x + s * 0.3, y - 0.25, 0.5], [0.06, 0.5, 0.06], "rubber", { k: 0, lod: 1, seg: 6, r: [0, 0, 90] });   // air hose
}
export function boxcar() {
  const A = mkP("mod_nt_boxcar", "Freight boxcar", "Rail", "Steel freight boxcar on two-axle bogies, 15 m over couplers, 3 m wide and 4.4 m above rail: ribbed oxide-red sides with a 3 m sliding door on each side, slid half open on its top track, ribbed ends with a ladder and a brake wheel, a low curved roof, side sills, a centre sill with couplers and air hoses, brake cylinder and reservoir. Not enterable (the dark load space shows through the doorway).",
    [15, 3.2]);
  const L = 7.0, W = 1.5, yb = RT + 0.78, yr = RT + 3.8, dz = W + 0.05;
  for (const x of [-5.0, 5.0]) bogie(A, x);
  coupler(A, -L, -1); coupler(A, L, 1);
  A.box([-L, yb - 0.1, -0.25], [L, yb + 0.15, 0.25], "bogie", { k: 0 });   // centre sill
  for (const s of [-1, 1]) A.box([-L, yb, s * W - (s > 0 ? 0.25 : 0)], [L, yb + 0.25, s * W + (s > 0 ? 0 : 0.25)], "car_oxide", { k: 0, bv: 0.01 });
  A.cyl([1.5, yb - 0.35, -0.6], [0.5, 1.6, 0.5], "bogie", { k: 0, lod: 1, seg: 12, r: [0, 0, 90] }); A.box([-1.2, yb - 0.4, 0.3], [-0.6, yb - 0.05, 0.7], "bogie", L1);
  // body: sides with the door opening, ribs, ends, roof; dark interior; the one collider is the whole box
  const D0 = -1.5, D1 = 1.5;
  for (const s of [-1, 1]) {
    const z0 = s > 0 ? W - 0.04 : -W, z1 = s > 0 ? W : -W + 0.04;
    for (const [a, b, c, d] of cutRect(-L, L, yb + 0.25, yr, [{ a0: D0, a1: D1, y0: yb, y1: yr - 0.35 }])) A.box([a, c, z0], [b, d, z1], "car_oxide", K0);
    for (let x = -L + 0.6; x < L - 0.3; x += 0.6) if (x < D0 - 0.05 || x > D1 + 0.05) A.box([x - 0.04, yb + 0.25, s > 0 ? W : -W - 0.035], [x + 0.04, yr - 0.05, s > 0 ? W + 0.035 : -W], "car_oxide", L1);
    // door leaf slid +x by half its width on top and bottom tracks
    const lz0 = s > 0 ? dz : -dz - 0.06, lz1 = s > 0 ? dz + 0.06 : -dz, sh = 1.5;
    A.box([D0 + sh, yb + 0.2, lz0], [D1 + sh + 0.05, yr - 0.3, lz1], "car_oxide", { k: 0, bv: 0.01 });
    for (let y = yb + 0.6; y < yr - 0.4; y += 0.6) A.box([D0 + sh, y - 0.03, s > 0 ? lz1 : lz0 - 0.02], [D1 + sh + 0.05, y + 0.03, s > 0 ? lz1 + 0.02 : lz0], "car_oxide", L1);
    for (const y of [yb + 0.15, yr - 0.32]) A.box([D0 - 0.2, y - 0.04, s > 0 ? W : -dz - 0.08], [D1 + 3.2, y + 0.04, s > 0 ? dz + 0.08 : -W], "steel_dk", L1);
    A.box([D0 + sh + 0.2, yb + 1.0, s > 0 ? lz1 : lz0 - 0.05], [D0 + sh + 0.25, yb + 1.6, s > 0 ? lz1 + 0.05 : lz0], "steel_dk", L1);   // handle
    A.box([-L + 0.5, yb + 1.2, s > 0 ? W : -W - 0.02], [-L + 1.3, yb + 1.6, s > 0 ? W + 0.02 : -W], "paint_w", L1);   // blank data panel
    for (const x of [-L + 0.15, L - 0.15]) for (let y = yb - 0.3; y < yb + 1.6; y += 0.4) A.box([x - 0.02, y, s > 0 ? W : -W - 0.15], [x + 0.02, y + 0.03, s > 0 ? W + 0.15 : -W], "steel_dk", L1);   // corner steps / grabs
  }
  for (const s of [-1, 1]) {
    const x = s * L;
    A.box([Math.min(x, x - s * 0.04), yb + 0.25, -W], [Math.max(x, x - s * 0.04), yr, W], "car_oxide", K0);
    for (let y = yb + 0.6; y < yr; y += 0.5) A.box([Math.min(x, x + s * 0.035), y - 0.04, -W + 0.05], [Math.max(x, x + s * 0.035), y + 0.04, W - 0.05], "car_oxide", L1);
    for (let y = yb + 0.4; y < yr; y += 0.38) A.box([Math.min(x, x + s * 0.1) , y, 0.95], [Math.max(x, x + s * 0.1), y + 0.03, 1.4], "steel_dk", L1);   // end ladder
  }
  A.cyl([-L - 0.08, yb + 2.6, -0.6], [0.45, 0.04, 0.45], "steel_dk", { k: 0, lod: 1, seg: 14, r: [0, 0, 90] });   // brake wheel
  A.box([-L, yr, -W - 0.03], [L, yr + 0.12, W + 0.03], "car_oxide", { k: 0, bv: 0.04 });
  A.box([-L + 0.05, yr + 0.12, -W + 0.3], [L - 0.05, yr + 0.22, W - 0.3], "car_oxide", { k: 0, bv: 0.06 });
  for (let x = -L + 1; x < L; x += 1.0) A.box([x - 0.03, yr + 0.1, -W], [x + 0.03, yr + 0.2, W], "car_oxide", L1);
  A.box([-L + 0.05, yb + 0.2, -W + 0.05], [L - 0.05, yr - 0.05, W - 0.05], "car_inner", K0);
  A.box([-L, yb, -W], [L, yr + 0.22, W], "car_oxide", H0);
  return pdone(A, { cover: boxCover(L, W, 4.2, { step: 2.4 }) });
}
export function flatcar() {
  const A = mkP("mod_nt_flatcar", "Loaded flatcar", "Rail", "Steel flatcar on two-axle bogies, 15 m over couplers: a timber deck on fish-belly side sills with stake pockets and a few stakes, carrying two timber crates, a tarpaulined crate and two cable spools, chocked with timber wedges and strapped down with orange ratchet straps.",
    [15, 3]);
  const L = 7.2, W = 1.4, yd = RT + 0.78;
  for (const x of [-5.0, 5.0]) bogie(A, x);
  coupler(A, -L, -1); coupler(A, L, 1);
  for (const s of [-1, 1]) {
    const [z0, z1] = s > 0 ? [W - 0.12, W] : [-W, -W + 0.12];
    extrudeZ(A, z0, z1, [[-L, yd], [L, yd], [L, yd - 0.3], [4, yd - 0.65], [-4, yd - 0.65], [-L, yd - 0.3]], "car_grey", { k: 0 });
    for (let x = -L + 0.6; x < L; x += 1.2) { A.box([x - 0.08, yd - 0.25, s > 0 ? W : -W - 0.08], [x + 0.08, yd - 0.02, s > 0 ? W + 0.08 : -W], "car_grey", L1); }
    for (const x of [-6.0, -1.2, 3.6]) A.box([x - 0.05, yd - 0.1, s > 0 ? W + 0.01 : -W - 0.07], [x + 0.05, yd + 0.9, s > 0 ? W + 0.07 : -W - 0.01], "post_wood", L1);   // stakes
  }
  A.box([-L, yd - 0.3, -0.25], [L, yd - 0.05, 0.25], "bogie", { k: 0 });
  A.box([-L, yd - 0.05, -W], [L, yd, W], "car_grey", { k: 0 });
  for (let z = -W + 0.15; z < W - 0.1; z += 0.3) A.box([-L + 0.02, yd, z - 0.14], [L - 0.02, yd + 0.05, z + 0.14], "timber", { k: 0, bv: 0.004 });
  A.box([-L, yd - 0.3, -W], [L, yd + 0.05, W], "car_grey", H0);
  const y = yd + 0.05;
  const strap = (x, hgt, wd = 2.8) => { A.box([x - 0.025, y + hgt, -wd / 2], [x + 0.025, y + hgt + 0.01, wd / 2], "strap", L1); for (const s of [-1, 1]) A.box([x - 0.025, y, s * wd / 2 - 0.005], [x + 0.025, y + hgt, s * wd / 2 + 0.005], "strap", L1); A.box([x - 0.04, y + 0.3, W - 0.02], [x + 0.04, y + 0.42, W + 0.04], "steel_dk", L1); };
  for (const [x0, x1, hh, tarp] of [[-6.6, -4.4, 1.3, false], [-4.2, -2.4, 1.1, true], [4.4, 6.6, 1.4, false]]) {
    A.box([x0, y, -1.0], [x1, y + hh, 1.0], tarp ? "tarp" : "timber_raw", { bv: 0.02 });
    if (!tarp) { for (const xx of [x0 + 0.05, x1 - 0.05]) A.box([xx - 0.06, y, -1.02], [xx + 0.06, y + hh, 1.02], "timber", L1); A.box([x0, y + hh - 0.1, -1.02], [x1, y + hh, 1.02], "timber", L1); A.box([x0, y, -1.02], [x1, y + 0.1, 1.02], "timber", L1); }
    else for (const xx of [x0 + 0.3, x1 - 0.3]) A.box([xx - 0.01, y + 0.1, -1.01], [xx + 0.01, y + hh, 1.01], "rope", L1);
    strap(x0 + 0.4, hh + 0.005, 2.06); strap(x1 - 0.4, hh + 0.005, 2.06);
  }
  for (const xc of [-0.6, 2.2]) {
    const D = 1.6, ww = 1.0;
    for (const s of [-1, 1]) A.cyl([xc, y + D / 2, s * (ww / 2 - 0.04)], [D, 0.08, D], "spool_wood", { seg: 20, r: [90, 0, 0] });
    A.cyl([xc, y + D / 2, 0], [D - 0.25, ww - 0.16, D - 0.25], "cable_blk", { seg: 20, r: [90, 0, 0] });
    for (let i = 0; i < 6; i++) { const a = (i / 6) * 2 * Math.PI; for (const s of [-1, 1]) A.cyl([xc + Math.cos(a) * 0.35, y + D / 2 + Math.sin(a) * 0.35, s * (ww / 2 + 0.005)], [0.05, 0.02, 0.05], "steel_dk", { k: 0, lod: 1, n: 6, r: [90, 0, 0] }); }
    A.cyl([xc, y + D / 2, 0], [0.2, ww + 0.04, 0.2], "spool_wood", { k: 0, lod: 1, seg: 10, r: [90, 0, 0] });
    for (const s of [-1, 1]) A.wedge([xc + s * 0.82, y + 0.15, 0], [0.9, 0.3, 0.25], "timber", { r: [0, s > 0 ? -90 : 90, 0] });
    for (const s of [-1, 1]) { bar(A, [xc + s * 0.75, y + 0.02, -0.45], [xc + s * 0.1, y + D - 0.05, -0.45], 0.05, 0.01, "strap", L1); bar(A, [xc + s * 0.75, y + 0.02, 0.45], [xc + s * 0.1, y + D - 0.05, 0.45], 0.05, 0.01, "strap", L1); }
  }
  return pdone(A, { cover: boxCover(L, W, 2.5, { step: 2.4 }) });
}

// ============================================================================ catalog
export function newtownAssetsA() {
  return [barracks(), armory(), hq(), motorPool(), radarTower(), compoundWall(), compoundWallBreach(), compoundWallCulvert(), compoundGate(),
    trainingWall(), trainingFrame(), crawlFrame(), warehouse(), workshopUnit(), storageTank(), railTrack(), railBuffer(), boxcar(), flatcar()];
}
